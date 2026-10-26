/*
 * Copyright 2026, Andrea Bernardi
 * SPDX-License-Identifier: MIT
 */

// dctest: checks /dev/graphics/tegra_dc without changing the display mode:
// the panel timing it reads, the clone of the scanout memory, and the rate
// of the retrace semaphore released by the vertical blank interrupt.

#include <OS.h>
#include <graphic_driver.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "tegra_dc_info.h"


int
main(int argc, char** argv)
{
	int fd = open("/dev/graphics/tegra_dc", O_RDWR);
	if (fd < 0) {
		perror("open /dev/graphics/tegra_dc");
		return 1;
	}

	char signature[B_FILE_NAME_LENGTH];
	if (ioctl(fd, B_GET_ACCELERANT_SIGNATURE, signature, sizeof(signature))
			== 0) {
		printf("accelerant: %s\n", signature);
	}

	area_id sharedArea;
	if (ioctl(fd, TEGRA_DC_GET_PRIVATE_DATA, &sharedArea, sizeof(sharedArea))
			!= 0) {
		perror("TEGRA_DC_GET_PRIVATE_DATA");
		return 1;
	}
	tegra_dc_shared_info* info;
	area_id clone = clone_area("dctest shared", (void**)&info, B_ANY_ADDRESS,
		B_READ_AREA, sharedArea);
	if (clone < 0) {
		fprintf(stderr, "cannot clone the shared info: %s\n", strerror(clone));
		return 1;
	}

	const display_timing& t = info->timing;
	printf("timing: %u kHz, H %u %u %u %u, V %u %u %u %u (%.2f Hz)\n",
		(unsigned)t.pixel_clock, t.h_display, t.h_sync_start, t.h_sync_end,
		t.h_total, t.v_display, t.v_sync_start, t.v_sync_end, t.v_total,
		t.pixel_clock * 1000.0 / ((double)t.h_total * t.v_total));
	printf("mode: %ux%u space %#x, %u bytes per row, EDID %s, scanout %u KB\n",
		info->current_mode.virtual_width, info->current_mode.virtual_height,
		(unsigned)info->current_mode.space, (unsigned)info->bytes_per_row,
		info->has_edid ? "yes" : "no", (unsigned)info->frame_buffer_size / 1024);

	area_info frameBuffer;
	if (ioctl(fd, TEGRA_DC_CLONE_FRAME_BUFFER, &frameBuffer,
			sizeof(frameBuffer)) == 0) {
		// read the first pixels (the screen is not changed)
		const uint16* pixels = (const uint16*)frameBuffer.address;
		printf("scanout clone: area %d, %lu bytes at %p, first pixels %04x "
			"%04x %04x\n", (int)frameBuffer.area, frameBuffer.size,
			frameBuffer.address, pixels[0], pixels[1], pixels[2]);
		delete_area(frameBuffer.area);
	} else
		perror("TEGRA_DC_CLONE_FRAME_BUFFER");

	uint32 before = info->vblank_count;
	snooze(1000000);
	printf("interrupts in 1 s without waiting: %u\n", (unsigned)(info->vblank_count - before));
	// Retrace semaphore: count the vertical blanks in one second
	sem_id sem = info->vblank_sem;
	int count = 0;
	bigtime_t start = system_time();
	while (system_time() - start < 1000000) {
		status_t error = acquire_sem_etc(sem, 1, B_RELATIVE_TIMEOUT, 100000);
		if (error != B_OK) {
			printf("acquire failed: %s (sem %d)\n", strerror(error), (int)sem);
			break;
		}
		count++;
	}
	printf("interrupts counted by the driver: %u\n", (unsigned)info->vblank_count);
	printf("retraces in %.2f s: %d\n", (system_time() - start) / 1000000.0,
		count);

	if (argc > 1)
		sleep(atoi(argv[1]));
	delete_area(clone);
	close(fd);
	return 0;
}
