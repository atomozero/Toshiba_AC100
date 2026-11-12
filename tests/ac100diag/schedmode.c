/* schedmode [latency|power]: shows or sets the scheduler mode, as the
 * ProcessController menu does. */
#include <stdio.h>
#include <string.h>
#include <scheduler.h>

int main(int argc, char** argv)
{
	if (argc > 1) {
		int32 mode = strcmp(argv[1], "power") == 0
			? SCHEDULER_MODE_POWER_SAVING : SCHEDULER_MODE_LOW_LATENCY;
		if (set_scheduler_mode(mode) != B_OK) {
			printf("cannot set the mode\n");
			return 1;
		}
	}
	printf("%s\n", get_scheduler_mode() == SCHEDULER_MODE_POWER_SAVING
		? "power saving" : "low latency");
	return 0;
}
