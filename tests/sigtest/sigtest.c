/*
 * Checks that signals delivered while a thread runs userland code (not in a
 * syscall) leave its registers intact: a timer sends SIGALRM every
 * millisecond while the main loop computes with 64 bit integers (r0/r1) and
 * the VFP. Results go to the kernel log (debug_printf), prefixed "sigtest:".
 */
#include <OS.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>


static volatile int32 sSignals;


static void
handler(int signal)
{
	// clobber what the handler may: integer and VFP registers
	volatile uint64 x = (uint64)signal * 0x123456789ULL;
	volatile double d = signal * 3.25;
	x *= 7;
	d *= 1.5;
	sSignals++;
}


int
main()
{
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_handler = handler;
	sigaction(SIGALRM, &action, NULL);

	struct itimerval timer;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 1000;
	timer.it_value = timer.it_interval;
	setitimer(ITIMER_REAL, &timer, NULL);

	uint32 errors = 0;
	bigtime_t end = system_time() + 3000000;
	uint64 rounds = 0;
	while (system_time() < end) {
		for (int32 round = 0; round < 100000; round++) {
			uint64 a = 0x0123456789abcdefULL + (uint64)round;
			uint64 b = a * 3 + 0x1111111111111111ULL;
			uint64 c = (b - 0x1111111111111111ULL) / 3;
			double d = (double)round * 0.5;
			double e = d * 4.0 / 2.0;
			if (c != a || e != (double)round)
				errors++;
		}
		rounds++;
	}

	timer.it_value.tv_usec = 0;
	timer.it_interval.tv_usec = 0;
	setitimer(ITIMER_REAL, &timer, NULL);

	debug_printf("sigtest: %ld signals, %llu rounds, %lu errors%s\n",
		(long)sSignals, (unsigned long long)rounds, (unsigned long)errors,
		errors != 0 || sSignals < 100 ? "  <-- WRONG" : "  OK");
	return 0;
}
