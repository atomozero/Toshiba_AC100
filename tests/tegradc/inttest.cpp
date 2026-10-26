// inttest: bring-up, which display controller interrupts fire, per setting
// usage: inttest <enable> <type> <polarity>  (hex bit masks of INT_*)

#include <OS.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tegra_dc_info.h"


int
main(int argc, char** argv)
{
	if (argc < 4)
		return 1;
	int fd = open("/dev/graphics/tegra_dc", O_RDWR);
	if (fd < 0)
		return 1;
	area_id sharedArea;
	ioctl(fd, TEGRA_DC_GET_PRIVATE_DATA, &sharedArea, sizeof(sharedArea));
	tegra_dc_shared_info* info;
	area_id clone = clone_area("inttest", (void**)&info, B_ANY_ADDRESS,
		B_READ_AREA, sharedArea);
	uint32 values[3] = { (uint32)strtoul(argv[1], NULL, 16),
		(uint32)strtoul(argv[2], NULL, 16), (uint32)strtoul(argv[3], NULL, 16) };
	if (ioctl(fd, TEGRA_DC_DEBUG_INTERRUPTS, values, sizeof(values)) != 0)
		perror("ioctl");
	snooze(1000000);
	printf("enable %s type %s polarity %s, in 1 s:", argv[1], argv[2], argv[3]);
	for (int i = 0; i < 8; i++)
		printf(" b%d=%u", i, (unsigned)info->interrupt_counts[i]);
	printf(", other %u, last %#x\n", (unsigned)info->other_interrupts,
		(unsigned)info->last_status);
	delete_area(clone);
	close(fd);
	return 0;
}
