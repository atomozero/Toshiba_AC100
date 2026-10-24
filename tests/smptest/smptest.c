/*
 * Checks that threads run in parallel on several CPUs: the same amount of
 * computation with 1 thread on each CPU, then with as many threads as CPUs,
 * free and pinned one per CPU. Every thread reports on which CPUs it ran.
 * Results go to the kernel log, prefixed "smptest:".
 */
#include <OS.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

extern int _kern_get_cpu();
extern status_t _kern_set_thread_affinity(thread_id id, const void* userMask,
	size_t size);


#define CHUNKS		200
#define MAX_CPUS	8

static volatile uint32 sSink;


static void*
work(void* data)
{
	int pin = (int)(addr_t)data;
	if (pin >= 0) {
		uint32 mask[8];
		memset(mask, 0, sizeof(mask));
		mask[0] = 1u << pin;
		status_t status = _kern_set_thread_affinity(find_thread(NULL), mask,
			sizeof(mask));
		if (status != B_OK)
			debug_printf("smptest:   pinning to CPU %d: %s\n", pin,
				strerror(status));
	}

	int chunksOn[MAX_CPUS] = {};
	uint32 x = (uint32)pin;
	bigtime_t start = system_time();
	for (int chunk = 0; chunk < CHUNKS; chunk++) {
		for (uint32 i = 0; i < 1000000; i++)
			x = x * 1664525 + 1013904223;
		int cpu = _kern_get_cpu();
		if (cpu >= 0 && cpu < MAX_CPUS)
			chunksOn[cpu]++;
	}
	sSink = x;
	bigtime_t elapsed = system_time() - start;

	thread_info info;
	get_thread_info(find_thread(NULL), &info);
	char where[128] = "";
	for (int cpu = 0; cpu < MAX_CPUS; cpu++) {
		if (chunksOn[cpu] == 0)
			continue;
		char part[32];
		snprintf(part, sizeof(part), " CPU %d %d%%", cpu,
			chunksOn[cpu] * 100 / CHUNKS);
		strlcat(where, part, sizeof(where));
	}
	char pinned[32] = "";
	if (pin >= 0)
		snprintf(pinned, sizeof(pinned), " pinned to CPU %d", pin);
	debug_printf("smptest:   thread%s: %lld ms, user %lld ms, kernel %lld ms;"
		" ran on%s\n", pinned, (long long)elapsed / 1000, (long long)info.user_time / 1000,
		(long long)info.kernel_time / 1000, where);
	return NULL;
}


static bigtime_t
run(int threads, bool pin, int firstCPU)
{
	pthread_t thread[MAX_CPUS];
	bigtime_t start = system_time();
	for (int i = 0; i < threads; i++) {
		pthread_create(&thread[i], NULL, work,
			(void*)(addr_t)(pin ? firstCPU + i : -1));
	}
	for (int i = 0; i < threads; i++)
		pthread_join(thread[i], NULL);
	return system_time() - start;
}


int
main()
{
	system_info info;
	get_system_info(&info);
	int cpus = info.cpu_count > MAX_CPUS ? MAX_CPUS : info.cpu_count;

	debug_printf("smptest: one thread pinned to each CPU in turn\n");
	for (int cpu = 0; cpu < cpus; cpu++)
		run(1, true, cpu);

	debug_printf("smptest: free threads\n");
	bigtime_t one = run(1, false, 0);
	bigtime_t all = run(cpus, false, 0);
	debug_printf("smptest: %d CPUs; 1 thread %lld ms, %d threads %lld ms: "
		"speedup %d.%02d%s\n", cpus, (long long)one / 1000, cpus,
		(long long)all / 1000, (int)(one * cpus / all),
		(int)(one * cpus * 100 / all % 100),
		cpus > 1 && one * cpus < all * 3 / 2 ? "  <-- NOT PARALLEL" : "  OK");

	if (cpus > 1) {
		bigtime_t pinned = run(cpus, true, 0);
		debug_printf("smptest: %d threads pinned one per CPU: %lld ms\n", cpus,
			(long long)pinned / 1000);
	}
	return 0;
}
