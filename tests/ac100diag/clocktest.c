/* clocktest: at each CPU clock, how fast is a fixed loop and how long do
 * snooze(1, 10, 100 ms) really take? Sets the clock through tegra_dvfs. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <OS.h>

static int set_clock(int mhz)
{
	char command[32];
	int fd = open("/dev/power/tegra_dvfs", O_RDWR);
	if (fd < 0)
		return -1;
	int length = snprintf(command, sizeof(command), "cclk %d", mhz);
	int result = write(fd, command, length) == length ? 0 : -1;
	close(fd);
	return result;
}

static double loop_ms(void)
{
	volatile unsigned int value = 1;
	bigtime_t start = system_time();
	for (int i = 0; i < 20000000; i++)
		value = value * 1664525 + 1013904223;
	return (system_time() - start) / 1000.0;
}

static void snoozes(double* out)
{
	static const bigtime_t kTimes[] = { 1000, 10000, 100000 };
	for (int i = 0; i < 3; i++) {
		bigtime_t worst = 0, total = 0;
		int rounds = i == 2 ? 5 : 20;
		for (int r = 0; r < rounds; r++) {
			bigtime_t start = system_time();
			snooze(kTimes[i]);
			bigtime_t took = system_time() - start;
			total += took;
			if (took > worst)
				worst = took;
		}
		out[i * 2] = total / (double)rounds / 1000.0;
		out[i * 2 + 1] = worst / 1000.0;
	}
}

int main(int argc, char** argv)
{
	static const int kClocks[] = { 1000, 760, 456, 216, 608, 912, 312, 816,
		1000 };
	int rounds = argc > 1 ? atoi(argv[1]) : 1;
	printf("  MHz  loop ms  speed   snooze 1ms avg/max  10ms avg/max  100ms avg/max\n");
	double base = 0;
	for (int round = 0; round < rounds; round++) {
		for (unsigned i = 0; i < sizeof(kClocks) / sizeof(kClocks[0]); i++) {
			if (set_clock(kClocks[i]) != 0) {
				printf("%5d  cannot set the clock\n", kClocks[i]);
				return 1;
			}
			double ms = loop_ms();
			if (base == 0)
				base = ms;
			double s[6];
			snoozes(s);
			printf("%5d %8.1f %5.0f MHz  %6.2f/%-6.2f %7.2f/%-7.2f %7.1f/%.1f\n",
				kClocks[i], ms, 1000.0 * base / ms, s[0], s[1], s[2], s[3],
				s[4], s[5]);
			fflush(stdout);
		}
	}
	return 0;
}
