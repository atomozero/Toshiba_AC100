// picturetest: plays picture data whose float operand is not 4-byte aligned
// (as in the packed BPicture stream) through BPrivate::PicturePlayer.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SupportDefs.h>
#include <PicturePlayer.h>

static float sPenSize = -1;

static void
set_pen_size(void*, float size)
{
	sPenSize = size;
}

int
main()
{
	// two ops: pen size 1.5, then pen size 2.5 -- header is 6 bytes (packed)
	uint8* data = (uint8*)malloc(64);
	size_t offset = 0;
	for (int i = 0; i < 2; i++) {
		uint16 op = 0x0304;
		uint32 size = 4;
		float value = 1.5f + i;
		memcpy(data + offset, &op, 2);
		memcpy(data + offset + 2, &size, 4);
		memcpy(data + offset + 6, &value, 4);
		offset += 10;
	}

	void* table[72] = {};
	table[32] = (void*)set_pen_size;
	BPrivate::PicturePlayer player(data, offset, NULL);
	status_t status = player.Play(table, 72, NULL);
	printf("PICTEST play: %s, pen size %g (expected 2.5)\n", strerror(status),
		sPenSize);
	return sPenSize == 2.5f ? 0 : 1;
}
