/*
 * An unaligned floating point load must end the program with SIGBUS, not
 * the whole system. Output prefixed "aligntest:" in the kernel log.
 */
#include <OS.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>


static void
handler(int signal, siginfo_t* info, void* context)
{
	debug_printf("aligntest: got signal %d (SIGBUS %d), code %d, address %p\n",
		signal, SIGBUS, info->si_code, info->si_addr);
	_exit(0);
}


int
main()
{
	struct sigaction action = {};
	action.sa_sigaction = handler;
	action.sa_flags = SA_SIGINFO;
	sigaction(SIGBUS, &action, NULL);

	static char buffer[16] __attribute__((aligned(8)));
	volatile float* unaligned = (volatile float*)(buffer + 1);
	float value;
	debug_printf("aligntest: loading a float from %p\n", unaligned);
	asm volatile("vldr s0, [%1]\n\tvmov %0, s0" : "=r" (value)
		: "r" (unaligned) : "s0");
	debug_printf("aligntest: no fault (%d)\n", (int)value);
	return 1;
}
