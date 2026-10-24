/*
 * Like smptest, but every thread is pinned to its own CPU: tells whether the
 * other CPUs cannot run user threads at all or the scheduler does not put
 * them there. Results go to the kernel log, prefixed "pintest:".
 */
#include <OS.h>
#include <pthread.h>
#include <string.h>

extern status_t _kern_set_thread_affinity(thread_id id, const void* userMask,
	size_t size);


static volatile uint32 sSink;


static void*
work(void* data)
{
	int cpu = (int)(addr_t)data;
	if (cpu >= 0) {
		uint32 mask[8];
		memset(mask, 0, sizeof(mask));
		mask[cpu / 32] = 1u << (cpu % 32);
		status_t status = _kern_set_thread_affinity(find_thread(NULL), mask,
			sizeof(mask));
		if (status != B_OK)
			debug_printf("pintest: affinity to CPU %d: %s\n", cpu,
				strerror(status));
	}
	uint32 x = (uint32)cpu;
	for (uint32 i = 0; i < 200000000; i++)
		x = x * 1664525 + 1013904223;
	sSink = x;
	return NULL;
}


static bigtime_t
run(int threads, int pin)
{
	pthread_t thread[8];
	bigtime_t start = system_time();
	for (int i = 0; i < threads; i++)
		pthread_create(&thread[i], NULL, work, (void*)(addr_t)(pin ? i : -1));
	for (int i = 0; i < threads; i++)
		pthread_join(thread[i], NULL);
	return system_time() - start;
}


int
main()
{
	system_info info;
	snooze(40000000);
	get_system_info(&info);
	int cpus = info.cpu_count > 8 ? 8 : info.cpu_count;
	for (int cpu = 0; cpu < cpus; cpu++) {
		bigtime_t start = system_time();
		pthread_t thread;
		pthread_create(&thread, NULL, work, (void*)(addr_t)cpu);
		pthread_join(thread, NULL);
		debug_printf("pintest: 1 thread on CPU %d: %lld ms\n", cpu,
			(long long)(system_time() - start) / 1000);
	}
	debug_printf("pintest: %d threads, one per CPU: %lld ms\n", cpus,
		(long long)run(cpus, 1) / 1000);
	debug_printf("pintest: %d threads, free: %lld ms\n", cpus,
		(long long)run(cpus, 0) / 1000);
	return 0;
}
