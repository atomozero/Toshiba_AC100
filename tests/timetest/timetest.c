/*
 * Checks time keeping and timeouts from userland on Haiku ARM: system_time(),
 * snooze(), snooze_until() and semaphore timeouts. Results go to the kernel
 * log (debug_printf), prefixed "timetest:".
 */
#include <OS.h>
#include <stdio.h>


static void
check(const char* what, bigtime_t expected, bigtime_t start)
{
	bigtime_t took = system_time() - start;
	debug_printf("timetest: %s: expected %lld us, took %lld us%s\n", what,
		(long long)expected, (long long)took,
		took < expected || took > expected + 200000 ? "  <-- WRONG" : "");
}


int
main()
{
	bigtime_t now = system_time();
	bigtime_t again = system_time();
	debug_printf("timetest: system_time %lld (%#llx), then %lld, real time "
		"%lld\n", (long long)now, (unsigned long long)now, (long long)again,
		(long long)real_time_clock_usecs());

	bigtime_t start = system_time();
	snooze(100000);
	check("snooze 100 ms", 100000, start);

	start = system_time();
	snooze_until(start + 100000, B_SYSTEM_TIMEBASE);
	check("snooze_until +100 ms", 100000, start);

	sem_id sem = create_sem(0, "timetest");
	start = system_time();
	status_t status = acquire_sem_etc(sem, 1, B_RELATIVE_TIMEOUT, 100000);
	check(status == B_TIMED_OUT ? "relative sem timeout 100 ms"
		: "relative sem timeout (not timed out!)", 100000, start);

	start = system_time();
	status = acquire_sem_etc(sem, 1, B_ABSOLUTE_TIMEOUT, start + 100000);
	check(status == B_TIMED_OUT ? "absolute sem timeout +100 ms"
		: "absolute sem timeout (not timed out!)", 100000, start);

	port_id port = create_port(1, "timetest");
	int32 code;
	start = system_time();
	ssize_t size = read_port_etc(port, &code, NULL, 0, B_RELATIVE_TIMEOUT,
		100000);
	check(size == B_TIMED_OUT ? "port timeout 100 ms"
		: "port timeout (not timed out!)", 100000, start);

	debug_printf("timetest: done\n");
	return 0;
}
