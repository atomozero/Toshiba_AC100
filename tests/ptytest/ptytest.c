/*
 * Reproduces what Terminal does with the pseudo terminal: a shell on the
 * slave side, a thread reading the master. Results go to the kernel log
 * (debug_printf), prefixed "ptytest:".
 */
#include <OS.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>


static int sMaster;


static void*
reader(void* data)
{
	char buffer[256];
	for (int i = 0; i < 40; i++) {
		errno = 0;
		ssize_t n = read(sMaster, buffer, sizeof(buffer) - 1);
		int error = errno;
		if (n > 0) {
			buffer[n] = '\0';
			for (ssize_t j = 0; j < n; j++) {
				if (buffer[j] == '\r' || buffer[j] == '\n'
					|| buffer[j] == '\x1b')
					buffer[j] = '|';
			}
		} else
			buffer[0] = '\0';
		sigset_t pending, mask;
		sigpending(&pending);
		pthread_sigmask(SIG_BLOCK, NULL, &mask);
		debug_printf("ptytest: read %ld, errno %d (%s), pending %#llx, "
			"mask %#llx: %s\n", (long)n, error, strerror(error),
			(unsigned long long)pending, (unsigned long long)mask, buffer);
		if (n < 0 && error == EINTR)
			continue;
		if (n <= 0)
			break;
	}
	return NULL;
}


static void*
cleanup(void* data)
{
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGCHLD);
	int signal;
	sigwait(&set, &signal);
	debug_printf("ptytest: got signal %d\n", signal);
	return NULL;
}


int
main()
{
	sigset_t blocked;
	sigemptyset(&blocked);
	sigaddset(&blocked, SIGCHLD);
	pthread_sigmask(SIG_BLOCK, &blocked, NULL);

	sMaster = posix_openpt(O_RDWR | O_NOCTTY);
	if (sMaster < 0 || grantpt(sMaster) != 0 || unlockpt(sMaster) != 0) {
		debug_printf("ptytest: posix_openpt failed: %s\n", strerror(errno));
		return 1;
	}
	const char* name = ptsname(sMaster);
	debug_printf("ptytest: master %d, slave %s\n", sMaster, name);

	pid_t pid = fork();
	if (pid == 0) {
		pthread_sigmask(SIG_UNBLOCK, &blocked, NULL);
		setsid();
		int slave = open(name, O_RDWR);
		if (slave < 0) {
			debug_printf("ptytest: child: open %s: %s\n", name, strerror(errno));
			_exit(1);
		}
		close(sMaster);
		dup2(slave, 0);
		dup2(slave, 1);
		dup2(slave, 2);
		if (slave > 2)
			close(slave);
		struct termios tio;
		tcgetattr(0, &tio);
		tio.c_lflag |= ECHO | ICANON | ISIG;
		if (tcsetattr(0, TCSANOW, &tio) != 0)
			debug_printf("ptytest: child: tcsetattr: %s\n", strerror(errno));
		tcsetpgrp(0, getpgrp());
		execl("/bin/sh", "sh", "-c",
			"echo hello from sh; read line; echo got: $line; sleep 1", NULL);
		debug_printf("ptytest: child: exec failed: %s\n", strerror(errno));
		_exit(1);
	}
	debug_printf("ptytest: child %d\n", (int)pid);

	pthread_t cleanupThread, readerThread;
	pthread_create(&cleanupThread, NULL, cleanup, NULL);
	pthread_create(&readerThread, NULL, reader, NULL);

	snooze(1000000);
	const char* line = "typed text\n";
	ssize_t written = write(sMaster, line, strlen(line));
	debug_printf("ptytest: write %ld (%s)\n", (long)written,
		written < 0 ? strerror(errno) : "ok");

	pthread_join(readerThread, NULL);
	pthread_join(cleanupThread, NULL);
	debug_printf("ptytest: done\n");
	return 0;
}
