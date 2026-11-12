/* cpucheck [seconds] [threads]: the same integer and floating point work
 * again and again on every CPU, each result compared with the first one.
 * A CPU supply too low for its clock shows up as a wrong result or a crash. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OS.h>

#define WORDS (256 * 1024)

static volatile int sStop;
static volatile int sErrors;
static volatile int sRounds;

static unsigned int work(unsigned int* buffer, double* fresult)
{
	unsigned int x = 12345, sum = 0;
	for (int i = 0; i < WORDS; i++) {
		x = x * 1664525 + 1013904223;
		buffer[i] = x ^ (x >> 7);
	}
	for (int pass = 0; pass < 4; pass++) {
		for (int i = 1; i < WORDS; i++) {
			buffer[i] += buffer[i - 1] * 2654435761u;
			sum = (sum << 5 | sum >> 27) ^ buffer[i];
		}
	}
	double f = 0.5;
	for (int i = 0; i < 200000; i++)
		f = sin(f + i * 1e-6) * 1.0001 + sqrt(fabs(f) + 1.0) * 0.25;
	*fresult = f;
	return sum;
}

static status_t worker(void* data)
{
	unsigned int* buffer = malloc(WORDS * sizeof(unsigned int));
	double reference, f;
	unsigned int expected = work(buffer, &reference);
	while (!sStop) {
		unsigned int sum = work(buffer, &f);
		if (sum != expected || f != reference)
			atomic_add((int32*)&sErrors, 1);
		atomic_add((int32*)&sRounds, 1);
	}
	free(buffer);
	return 0;
}

int main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 20;
	int count = argc > 2 ? atoi(argv[2]) : 2;
	thread_id threads[8];
	for (int i = 0; i < count && i < 8; i++) {
		threads[i] = spawn_thread(worker, "cpucheck", B_NORMAL_PRIORITY, NULL);
		resume_thread(threads[i]);
	}
	snooze(seconds * 1000000LL);
	sStop = 1;
	for (int i = 0; i < count && i < 8; i++) {
		status_t result;
		wait_for_thread(threads[i], &result);
	}
	printf("%d rounds, %d errors\n", sRounds, sErrors);
	return sErrors != 0;
}
