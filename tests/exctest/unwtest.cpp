/*
 * Diagnoses the ARM EHABI unwinder: the exception index lookup and a
 * backtrace through _Unwind_Backtrace(). Output prefixed "unwtest:".
 */
#include <OS.h>
#include <stdint.h>
#include <unwind.h>
#include <vector>


extern "C" uintptr_t __gnu_Unwind_Find_exidx(uintptr_t pc, int* count)
	__attribute__((weak));


static _Unwind_Reason_Code
trace(_Unwind_Context* context, void* data)
{
	int* depth = (int*)data;
	debug_printf("unwtest:   frame %d: ip %#lx\n", *depth,
		(unsigned long)_Unwind_GetIP(context));
	if (++*depth > 10)
		return _URC_END_OF_STACK;
	return _URC_NO_REASON;
}


static void __attribute__((noinline))
level2()
{
	int depth = 0;
	_Unwind_Reason_Code result = _Unwind_Backtrace(trace, &depth);
	debug_printf("unwtest: backtrace result %d, %d frames\n", (int)result,
		depth);
}


static void __attribute__((noinline))
level1()
{
	level2();
	asm volatile("");
}


int
main()
{
	int count = 0;
	uintptr_t table = __gnu_Unwind_Find_exidx((uintptr_t)&main, &count);
	debug_printf("unwtest: main %p: exidx %#lx, %d entries\n", (void*)&main,
		(unsigned long)table, count);
	const uint32_t* entry = (const uint32_t*)table;
	for (int i = 0; i < count && i < 4; i++)
		debug_printf("unwtest:   [%d] %#x %#x\n", i, entry[2 * i],
			entry[2 * i + 1]);



	count = 0;
	table = __gnu_Unwind_Find_exidx((uintptr_t)&_Unwind_Backtrace, &count);
	debug_printf("unwtest: libgcc %p: exidx %#lx, %d entries\n",
		(void*)&_Unwind_Backtrace, (unsigned long)table, count);

	level1();
	return 0;
}
