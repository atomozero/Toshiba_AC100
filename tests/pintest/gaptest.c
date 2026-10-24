/*
 * A thread pinned to each CPU in turn spins on system_time() for a second
 * and records the gaps in which it did not run. Results go to the kernel
 * log, prefixed "gaptest:".
 */
#include <OS.h>
#include <pthread.h>
#include <string.h>

extern status_t _kern_set_thread_affinity(thread_id id, const void* userMask,
	size_t size);


static void*
work(void* data)
{
	int cpu = (int)(addr_t)data;
	uint32 mask[8];
	memset(mask, 0, sizeof(mask));
	mask[0] = 1u << cpu;
	_kern_set_thread_affinity(find_thread(NULL), mask, sizeof(mask));
	snooze(10000);

	bigtime_t gaps[10];
	bigtime_t ran[10];
	int count = 0, total = 0;
	bigtime_t lost = 0, runStart;
	bigtime_t start = system_time(), last = start;
	runStart = start;
	while (last - start < 1000000) {
		bigtime_t now = system_time();
		if (now - last > 200) {
			if (count < 10) {
				gaps[count] = now - last;
				ran[count] = last - runStart;
				count++;
			}
			total++;
			lost += now - last;
			runStart = now;
		}
		last = now;
	}
	debug_printf("gaptest: CPU %d: %d gaps, %lld us lost of %lld\n", cpu,
		total, (long long)lost, (long long)(last - start));
	for (int i = 0; i < count; i++)
		debug_printf("gaptest:   ran %lld us, then gap %lld us\n",
			(long long)ran[i], (long long)gaps[i]);
	return NULL;
}


int
main()
{
	system_info info;
	snooze(40000000);
	get_system_info(&info);
	for (int cpu = 0; cpu < (int)info.cpu_count && cpu < 32; cpu++) {
		pthread_t thread;
		pthread_create(&thread, NULL, work, (void*)(addr_t)cpu);
		pthread_join(thread, NULL);
	}
	return 0;
}
