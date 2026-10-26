/*
 * Copyright 2026, Andrea Bernardi
 * SPDX-License-Identifier: MIT
 */

// overlaytest: a YCbCr 4:2:2 overlay bitmap with color bars and a moving
// bar, scaled to the window. Resize the window to test the scaler, put
// another window over it to test the color key.
//
// overlaytest [width height] [rgb]

#include <Application.h>
#include <Bitmap.h>
#include <View.h>
#include <Window.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int32 sWidth = 640;
static int32 sHeight = 360;
static bool sRGB = false;


static void
rgb_to_yuv(uint8 r, uint8 g, uint8 b, uint8& y, uint8& u, uint8& v)
{
	y = (uint8)((66 * r + 129 * g + 25 * b + 128) / 256 + 16);
	u = (uint8)((-38 * r - 74 * g + 112 * b + 128) / 256 + 128);
	v = (uint8)((112 * r - 94 * g - 18 * b + 128) / 256 + 128);
}


static void
draw_frame(BBitmap* bitmap, int32 frame)
{
	static const uint8 kBars[8][3] = {
		{ 255, 255, 255 }, { 255, 255, 0 }, { 0, 255, 255 }, { 0, 255, 0 },
		{ 255, 0, 255 }, { 255, 0, 0 }, { 0, 0, 255 }, { 0, 0, 0 }
	};

	uint8* bits = (uint8*)bitmap->Bits();
	int32 bpr = bitmap->BytesPerRow();
	int32 barX = (frame * 4) % sWidth;

	for (int32 line = 0; line < sHeight; line++) {
		uint8* row = bits + line * bpr;
		for (int32 x = 0; x < sWidth; x += 2) {
			uint8 r, g, b;
			if (line > sHeight * 3 / 4) {
				// gray ramp, to see the filters and banding
				r = g = b = (uint8)(x * 255 / sWidth);
			} else {
				const uint8* bar = kBars[x * 8 / sWidth];
				r = bar[0];
				g = bar[1];
				b = bar[2];
			}
			if (x >= barX && x < barX + 16)
				r = g = b = 128;

			if (sRGB) {
				for (int32 i = 0; i < 2; i++) {
					row[(x + i) * 4 + 0] = b;
					row[(x + i) * 4 + 1] = g;
					row[(x + i) * 4 + 2] = r;
					row[(x + i) * 4 + 3] = 255;
				}
			} else {
				uint8 y, u, v;
				rgb_to_yuv(r, g, b, y, u, v);
				// B_YCbCr422: Y0 Cb Y1 Cr
				row[x * 2 + 0] = y;
				row[x * 2 + 1] = u;
				row[x * 2 + 2] = y;
				row[x * 2 + 3] = v;
			}
		}
	}
}


class OverlayWindow : public BWindow {
public:
	OverlayWindow(BRect frame)
		:
		BWindow(frame, "Overlay test", B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS | B_QUIT_ON_WINDOW_CLOSE),
		fQuit(false)
	{
		fView = new BView(Bounds(), "video", B_FOLLOW_ALL, B_WILL_DRAW);
		AddChild(fView);

		fBitmap = new BBitmap(BRect(0, 0, sWidth - 1, sHeight - 1),
			B_BITMAP_WILL_OVERLAY | B_BITMAP_RESERVE_OVERLAY_CHANNEL,
			sRGB ? B_RGB32 : B_YCbCr422);
		status_t status = fBitmap->InitCheck();
		printf("overlay bitmap: %s, bits %p, %" B_PRId32 " bytes per row\n",
			strerror(status), status == B_OK ? fBitmap->Bits() : NULL,
			fBitmap->BytesPerRow());
		if (status != B_OK || fBitmap->Bits() == NULL) {
			fView->SetViewColor(255, 0, 0);
			return;
		}

		draw_frame(fBitmap, 0);
		rgb_color key;
		status = fView->SetViewOverlay(fBitmap, fBitmap->Bounds(),
			fView->Bounds(), &key, B_FOLLOW_ALL,
			B_OVERLAY_FILTER_HORIZONTAL | B_OVERLAY_FILTER_VERTICAL);
		printf("SetViewOverlay: %s, key %d,%d,%d\n", strerror(status),
			key.red, key.green, key.blue);

		fThread = spawn_thread(&_Animate, "animate", B_DISPLAY_PRIORITY,
			this);
		resume_thread(fThread);
	}

	virtual bool QuitRequested()
	{
		fQuit = true;
		status_t result;
		wait_for_thread(fThread, &result);
		return true;
	}

private:
	static status_t _Animate(void* data)
	{
		OverlayWindow* window = (OverlayWindow*)data;
		bigtime_t start = system_time();
		for (int32 frame = 1; !window->fQuit; frame++) {
			if (window->fBitmap->LockBits() == B_OK) {
				draw_frame(window->fBitmap, frame);
				window->fBitmap->UnlockBits();
			}
			if (frame % 100 == 0) {
				printf("%.1f frames/s\n",
					100 * 1000000.0 / (system_time() - start));
				start = system_time();
			}
			snooze(16000);
		}
		return B_OK;
	}

	BView*		fView;
	BBitmap*	fBitmap;
	thread_id	fThread;
	volatile bool fQuit;
};


int
main(int argc, char** argv)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "rgb") == 0)
			sRGB = true;
		else if (i + 1 < argc) {
			sWidth = atoi(argv[i]) & ~1;
			sHeight = atoi(argv[i + 1]);
			i++;
		}
	}

	BApplication app("application/x-vnd.ac100-overlaytest");
	(new OverlayWindow(BRect(100, 80, 100 + 480, 80 + 270)))->Show();
	app.Run();
	return 0;
}
