/*
 * Checks C++ exceptions on Haiku ARM: thrown and caught in the same
 * function, across frames, from libstdc++ (std::vector::at) and from a
 * secondary thread. Results go to the kernel log, prefixed "exctest:".
 */
#include <OS.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <image.h>
#include <stdexcept>
#include <vector>


static void __attribute__((noinline))
thrower(int depth)
{
	if (depth == 0)
		throw std::runtime_error("deep");
	thrower(depth - 1);
}


static void*
exc_thread(void*)
{
	try {
		thrower(5);
	} catch (const std::exception& e) {
		debug_printf("exctest: thread: caught \"%s\"\n", e.what());
	}
	return NULL;
}


static void
describe(const char* what, uint32 address)
{
	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		if (address >= (addr_t)info.text
			&& address < (addr_t)info.text + info.text_size) {
			debug_printf("exctest:   %s %#lx = %s + %#lx\n", what,
				(unsigned long)address, info.name,
				(unsigned long)(address - (addr_t)info.text));
			return;
		}
	}
	debug_printf("exctest:   %s %#lx\n", what, (unsigned long)address);
}


static void
segv_handler(int signal, siginfo_t* info, void* _context)
{
	ucontext_t* context = (ucontext_t*)_context;
	debug_printf("exctest: SIGSEGV at %p\n", info->si_addr);
	describe("pc", context->uc_mcontext.r15);
	describe("lr", context->uc_mcontext.r14);
	uint32* sp = (uint32*)context->uc_mcontext.r13;
	for (int i = 0; i < 24; i++)
		describe("stack", sp[i]);
	_exit(1);
}


int
main()
{
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_sigaction = segv_handler;
	action.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &action, NULL);

	debug_printf("exctest: start\n");
	try {
		throw 42;
	} catch (int value) {
		debug_printf("exctest: local: caught %d\n", value);
	}
	try {
		thrower(10);
	} catch (const std::exception& e) {
		debug_printf("exctest: frames: caught \"%s\"\n", e.what());
	}
	try {
		std::vector<int> v;
		v.at(3);
	} catch (const std::out_of_range& e) {
		debug_printf("exctest: libstdc++: caught out_of_range\n");
	}
	pthread_t thread;
	pthread_create(&thread, NULL, exc_thread, NULL);
	pthread_join(thread, NULL);
	debug_printf("exctest: done\n");
	return 0;
}
