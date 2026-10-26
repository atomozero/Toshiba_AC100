/*
 * Copyright 2026, Andrea Bernardi
 * SPDX-License-Identifier: MIT
 */

// fbbench: how fast the CPU writes the screen, for the native display
// driver plan. Clones the frame buffer of /dev/graphics/framebuffer (the
// mapping app_server uses, write combining), saves what is on screen, and
// times writing it back the way app_server does at 16 bit (converting each
// 32 bit pixel) and a plain copy as at 32 bit. The same pixels are written
// back, so the screen does not change.

#include <Accelerant.h>
#include <OS.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <vesa_info.h>


static double
seconds(bigtime_t time)
{
	return time / 1000000.0;
}


int
main()
{
	int fd = open("/dev/graphics/framebuffer", O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	area_id sharedArea;
	if (ioctl(fd, VESA_GET_PRIVATE_DATA, &sharedArea, sizeof(sharedArea))
			!= 0) {
		perror("VESA_GET_PRIVATE_DATA");
		return 1;
	}
	vesa_shared_info* shared;
	area_id sharedClone = clone_area("fbbench shared", (void**)&shared,
		B_ANY_ADDRESS, B_READ_AREA, sharedArea);
	if (sharedClone < 0) {
		fprintf(stderr, "cannot clone the shared info\n");
		return 1;
	}

	area_info frameBufferInfo;
	if (ioctl(fd, VESA_CLONE_FRAME_BUFFER, &frameBufferInfo,
			sizeof(frameBufferInfo)) != 0) {
		perror("VESA_CLONE_FRAME_BUFFER");
		return 1;
	}

	const display_mode& mode = shared->current_mode;
	uint32 width = mode.virtual_width, height = mode.virtual_height;
	uint32 bytesPerRow = shared->bytes_per_row;
	uint8* frameBuffer = (uint8*)frameBufferInfo.address;
	printf("frame buffer %" B_PRIu32 "x%" B_PRIu32 ", space %#" B_PRIx32
		", %" B_PRIu32 " bytes per row, area %" B_PRId32 " (%lu bytes)\n",
		width, height, mode.space, bytesPerRow, frameBufferInfo.area,
		frameBufferInfo.size);
	if (mode.space != B_RGB16) {
		fprintf(stderr, "expected B_RGB16\n");
		return 1;
	}

	size_t screenSize = (size_t)bytesPerRow * height;
	uint16* saved = (uint16*)malloc(screenSize);
	uint32* back = (uint32*)malloc((size_t)width * height * 4);
	uint8* cached = (uint8*)malloc(width * height * 4);
	if (saved == NULL || back == NULL || cached == NULL)
		return 1;

	// Read what is on screen (reads from write combining memory are slow)
	bigtime_t start = system_time();
	memcpy(saved, frameBuffer, screenSize);
	bigtime_t readTime = system_time() - start;

	// The 32 bit back buffer app_server would hold for this content
	for (uint32 y = 0; y < height; y++) {
		const uint16* row = (const uint16*)((uint8*)saved + y * bytesPerRow);
		for (uint32 x = 0; x < width; x++) {
			uint16 pixel = row[x];
			uint32 r = (pixel >> 11) & 0x1f, g = (pixel >> 5) & 0x3f,
				b = pixel & 0x1f;
			back[y * width + x] = 0xff000000 | (r << 19) | (r << 14)
				| (g << 10) | (g << 4) | (b << 3) | (b >> 2);
		}
	}

	const int kRounds = 20;

	// 16 bit: convert each 32 bit pixel and write it, as app_server does
	start = system_time();
	for (int round = 0; round < kRounds; round++) {
		for (uint32 y = 0; y < height; y++) {
			const uint32* source = back + y * width;
			uint16* target = (uint16*)(frameBuffer + y * bytesPerRow);
			for (uint32 x = 0; x < width; x++) {
				uint32 pixel = source[x];
				target[x] = ((pixel >> 8) & 0xf800) | ((pixel >> 5) & 0x07e0)
					| ((pixel >> 3) & 0x001f);
			}
		}
	}
	bigtime_t convertTime = (system_time() - start) / kRounds;

	// Plain copy into the frame buffer (what 32 bit scanout would need, per
	// byte), done here on the 16 bit content so the screen does not change
	start = system_time();
	for (int round = 0; round < kRounds; round++)
		memcpy(frameBuffer, saved, screenSize);
	bigtime_t copyTime = (system_time() - start) / kRounds;

	// Copy between cached buffers, for comparison
	start = system_time();
	uint32 sink = 0;
	for (int round = 0; round < kRounds; round++) {
		back[round] ^= round;
		memcpy(cached, back, (size_t)width * height * 4);
		sink += cached[round * 4097];
	}
	bigtime_t cachedTime = (system_time() - start) / kRounds;
	if (sink == 0x12345678)
		printf(" ");

	// Leave the screen as it was
	memcpy(frameBuffer, saved, screenSize);

	double screenMB = screenSize / 1048576.0;
	double screen32MB = width * height * 4 / 1048576.0;
	printf("read screen (%.2f MB): %.1f ms, %.0f MB/s\n", screenMB,
		readTime / 1000.0, screenMB / seconds(readTime));
	printf("16 bit, convert + write (as app_server): %.1f ms per screen, "
		"%.0f MB/s of 32 bit input\n", convertTime / 1000.0,
		screen32MB / seconds(convertTime));
	printf("plain copy into the frame buffer: %.0f MB/s -> %.1f ms for a "
		"32 bit screen (%.2f MB)\n", screenMB / seconds(copyTime),
		screen32MB / (screenMB / seconds(copyTime)) * 1000, screen32MB);
	printf("cached copy, 32 bit screen: %.1f ms, %.0f MB/s\n",
		cachedTime / 1000.0, screen32MB / seconds(cachedTime));

	delete_area(frameBufferInfo.area);
	delete_area(sharedClone);
	close(fd);
	return 0;
}
