/*
 * Checks fork() on Haiku ARM: return values, copy-on-write of globals, heap
 * and stack, vfork() + exec as used by posix_spawn(). Results go to the
 * kernel log (debug_printf), prefixed "forktest:".
 */
#include <OS.h>
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile int sGlobal = 1;
static int sFailures = 0;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		sFailures++; \
		debug_printf("forktest: FAIL " __VA_ARGS__); \
	} \
} while (0)


static void
test_fork_cow(int round)
{
	volatile int onStack = 10;
	volatile int* onHeap = malloc(sizeof(int));
	*onHeap = 100;
	sGlobal = 1;

	int pipes[2];
	pipe(pipes);

	pid_t pid = fork();
	if (pid == 0) {
		// child: wait for the parent to change everything, then check that
		// we still see the values of the fork
		char c;
		read(pipes[0], &c, 1);
		int result = (sGlobal == 1 ? 0 : 1) | (onStack == 10 ? 0 : 2)
			| (*onHeap == 100 ? 0 : 4);
		_exit(result);
	}
	CHECK(pid > 0, "round %d: fork returned %d\n", round, (int)pid);

	sGlobal = 2;
	onStack = 20;
	*onHeap = 200;
	write(pipes[1], "x", 1);

	int status = 0;
	pid_t waited = waitpid(pid, &status, 0);
	CHECK(waited == pid, "round %d: waitpid %d != %d (%s)\n", round,
		(int)waited, (int)pid, strerror(errno));
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
		"round %d: child saw the parent's writes, status %#x "
		"(1 global, 2 stack, 4 heap)\n", round, status);
	CHECK(sGlobal == 2 && onStack == 20 && *onHeap == 200,
		"round %d: parent lost its own writes\n", round);

	close(pipes[0]);
	close(pipes[1]);
	free((void*)onHeap);
}


static void
test_vfork_return(int round)
{
	pid_t parent = getpid();
	pid_t pid = vfork();
	if (pid == 0) {
		_exit(getpid() != parent ? 0 : 3);
	}
	CHECK(pid > 0, "round %d: vfork returned %d\n", round, (int)pid);
	int status = 0;
	waitpid(pid, &status, 0);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
		"round %d: vfork child status %#x\n", round, status);
}


static void
test_spawn_output(int round)
{
	// like launch_daemon: run a shell command, read its output through a pipe
	int pipes[2];
	pipe(pipes);
	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO);
	posix_spawn_file_actions_addclose(&actions, pipes[0]);
	posix_spawn_file_actions_addclose(&actions, pipes[1]);

	const char* argv[] = { "/bin/echo", "hello", NULL };
	pid_t pid;
	int error = posix_spawn(&pid, argv[0], &actions, NULL, (char**)argv,
		NULL);
	CHECK(error == 0, "round %d: posix_spawn: %s\n", round, strerror(error));
	close(pipes[1]);

	char buffer[64];
	ssize_t total = 0;
	for (int i = 0; i < 100; i++) {
		ssize_t bytes = read(pipes[0], buffer + total,
			sizeof(buffer) - 1 - total);
		if (bytes <= 0)
			break;
		total += bytes;
	}
	buffer[total] = '\0';
	close(pipes[0]);
	posix_spawn_file_actions_destroy(&actions);

	int status = 0;
	waitpid(pid, &status, 0);
	CHECK(strcmp(buffer, "hello\n") == 0, "round %d: spawn output \"%s\", "
		"status %#x\n", round, buffer, status);
}


int
main()
{
	debug_printf("forktest: start, pid %d\n", (int)getpid());
	for (int round = 0; round < 20; round++) {
		test_fork_cow(round);
		test_vfork_return(round);
		test_spawn_output(round);
	}
	debug_printf("forktest: done, %d failures\n", sFailures);
	return sFailures != 0;
}
