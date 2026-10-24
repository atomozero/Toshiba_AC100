/*
 * Copyright 2026, Andrea Bernardi
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * doomgeneric for Haiku, with the native BeAPI only: a BWindow showing the
 * engine's frame buffer through a BBitmap, keys from the view, and the
 * sound effects mixed into a BSoundPlayer. No music.
 */

#include <Application.h>
#include <Bitmap.h>
#include <Directory.h>
#include <Autolock.h>
#include <Locker.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>
#include <Screen.h>
#include <SoundPlayer.h>
#include <View.h>
#include <Window.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern "C" {
#include "doomgeneric.h"
#include "doomkeys.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"
#include "i_system.h"
}


static const char* kSignature = "application/x-vnd.atomozero-doomgeneric";

static int sArgc;
static char** sArgv;
static BWindow* sWindow;
static BView* sView;
static BBitmap* sBitmap;
static volatile bool sQuit;


//	#pragma mark - keys


#define KEYQUEUE_SIZE 64

static uint16 sKeyQueue[KEYQUEUE_SIZE];
// written by the window thread, read by the game thread
static volatile int32 sKeyWrite;
static volatile int32 sKeyRead;
static uint32 sModifiers;


static void
add_key(bool pressed, unsigned char key)
{
	if (key == 0)
		return;
	int32 next = (sKeyWrite + 1) % KEYQUEUE_SIZE;
	if (next == sKeyRead)
		return;
	sKeyQueue[sKeyWrite] = (pressed ? 0x100 : 0) | key;
	sKeyWrite = next;
}


static unsigned char
doom_key(const char* bytes)
{
	switch ((uint8)bytes[0]) {
		case B_ENTER:		return KEY_ENTER;
		case B_ESCAPE:		return KEY_ESCAPE;
		case B_LEFT_ARROW:	return KEY_LEFTARROW;
		case B_RIGHT_ARROW:	return KEY_RIGHTARROW;
		case B_UP_ARROW:	return KEY_UPARROW;
		case B_DOWN_ARROW:	return KEY_DOWNARROW;
		case B_TAB:			return KEY_TAB;
		case B_BACKSPACE:	return KEY_BACKSPACE;
		case B_SPACE:		return KEY_USE;
		case B_FUNCTION_KEY:	return 0;
	}
	if ((uint8)bytes[0] >= 0x80)
		return 0;
	return tolower(bytes[0]);
}


static unsigned char
function_key(int32 key)
{
	if (key >= B_F1_KEY && key <= B_F12_KEY)
		return KEY_F1 + (key - B_F1_KEY);
	return 0;
}


class DoomView : public BView {
public:
	DoomView(BRect frame)
		:
		BView(frame, "doom", B_FOLLOW_ALL, B_WILL_DRAW)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	void Draw(BRect)
	{
		if (sBitmap != NULL)
			DrawBitmap(sBitmap, sBitmap->Bounds(), Bounds());
	}

	void KeyDown(const char* bytes, int32 numBytes)
	{
		int32 key = 0;
		if (bytes[0] == B_FUNCTION_KEY)
			Window()->CurrentMessage()->FindInt32("key", &key);
		add_key(true, bytes[0] == B_FUNCTION_KEY ? function_key(key)
			: doom_key(bytes));
	}

	void KeyUp(const char* bytes, int32 numBytes)
	{
		int32 key = 0;
		if (bytes[0] == B_FUNCTION_KEY)
			Window()->CurrentMessage()->FindInt32("key", &key);
		add_key(false, bytes[0] == B_FUNCTION_KEY ? function_key(key)
			: doom_key(bytes));
	}
};


class DoomWindow : public BWindow {
public:
	DoomWindow(BRect frame)
		:
		BWindow(frame, "Doom", B_TITLED_WINDOW,
			B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_QUIT_ON_WINDOW_CLOSE)
	{
	}

	void MessageReceived(BMessage* message)
	{
		if (message->what == B_MODIFIERS_CHANGED) {
			int32 modifiers = 0;
			message->FindInt32("modifiers", &modifiers);
			uint32 changed = modifiers ^ sModifiers;
			if (changed & B_CONTROL_KEY)
				add_key(modifiers & B_CONTROL_KEY, KEY_FIRE);
			if (changed & B_SHIFT_KEY)
				add_key(modifiers & B_SHIFT_KEY, KEY_RSHIFT);
			if (changed & (B_COMMAND_KEY | B_OPTION_KEY)) {
				add_key(modifiers & (B_COMMAND_KEY | B_OPTION_KEY),
					KEY_RALT);
			}
			sModifiers = modifiers;
			return;
		}
		BWindow::MessageReceived(message);
	}

	bool QuitRequested()
	{
		// the game thread shuts the engine (and the sound player) down in
		// order, then quits; nothing is torn down under its feet
		sQuit = true;
		return false;
	}
};


//	#pragma mark - doomgeneric platform functions


extern "C" void
DG_Init()
{
	BRect frame(0, 0, DOOMGENERIC_RESX - 1, DOOMGENERIC_RESY - 1);
	sBitmap = new BBitmap(frame, B_RGB32);

	sWindow = new DoomWindow(frame);
	sView = new DoomView(frame);
	sWindow->AddChild(sView);
	sWindow->CenterOnScreen();
	sView->MakeFocus(true);
	sWindow->Show();
}


extern "C" void
DG_DrawFrame()
{
	if (sQuit)
		I_Quit();

	// 0x00RRGGBB words are B_RGB32's layout on a little endian CPU
	uint8* bits = (uint8*)sBitmap->Bits();
	int32 bytesPerRow = sBitmap->BytesPerRow();
	for (int y = 0; y < DOOMGENERIC_RESY; y++) {
		memcpy(bits + y * bytesPerRow, DG_ScreenBuffer + y * DOOMGENERIC_RESX,
			DOOMGENERIC_RESX * 4);
	}

	if (sWindow->LockWithTimeout(100000) == B_OK) {
		sView->DrawBitmap(sBitmap, sBitmap->Bounds(), sView->Bounds());
		sView->Sync();
		sWindow->Unlock();
	}
}


extern "C" void
DG_SleepMs(uint32_t ms)
{
	snooze(ms * 1000LL);
}


extern "C" uint32_t
DG_GetTicksMs()
{
	return system_time() / 1000;
}


extern "C" int
DG_GetKey(int* pressed, unsigned char* key)
{
	if (sKeyRead == sKeyWrite)
		return 0;
	uint16 data = sKeyQueue[sKeyRead];
	sKeyRead = (sKeyRead + 1) % KEYQUEUE_SIZE;
	*pressed = data >> 8;
	*key = data & 0xff;
	return 1;
}


extern "C" void
DG_SetWindowTitle(const char* title)
{
	if (sWindow != NULL && sWindow->Lock()) {
		sWindow->SetTitle(title);
		sWindow->Unlock();
	}
}


//	#pragma mark - sound effects


#define NUM_CHANNELS 16
#define OUTPUT_RATE 44100

struct channel {
	const uint8*	samples;	// unsigned 8 bit
	uint32			length;
	uint32			position;	// 16.16 fixed point
	uint32			step;		// 16.16 fixed point
	float			left;
	float			right;
};

static BSoundPlayer* sPlayer;
static BLocker sChannelLock("doom channels");
static channel sChannels[NUM_CHANNELS];
static boolean sUseSfxPrefix;


static void
play_buffer(void*, void* buffer, size_t size,
	const media_raw_audio_format& format)
{
	float* out = (float*)buffer;
	size_t frames = size / (2 * sizeof(float));
	memset(buffer, 0, size);

	BAutolock _(sChannelLock);
	for (int c = 0; c < NUM_CHANNELS; c++) {
		channel& ch = sChannels[c];
		const uint8* samples = ch.samples;
		if (samples == NULL)
			continue;
		uint32 position = ch.position;
		uint32 end = ch.length << 16;
		for (size_t i = 0; i < frames && position < end; i++) {
			float value = (samples[position >> 16] - 128) / 128.0f;
			out[i * 2] += value * ch.left;
			out[i * 2 + 1] += value * ch.right;
			position += ch.step;
		}
		ch.position = position;
		if (position >= end)
			ch.samples = NULL;
	}

	for (size_t i = 0; i < frames * 2; i++) {
		if (out[i] > 1.0f)
			out[i] = 1.0f;
		else if (out[i] < -1.0f)
			out[i] = -1.0f;
	}
}


static void
set_params(channel& ch, int vol, int sep)
{
	// vol 0-127, sep 0 (left) - 254 (right)
	float volume = vol / 127.0f * 0.5f;
	ch.left = volume * (254 - sep) / 127.0f;
	ch.right = volume * sep / 127.0f;
}


static boolean
haiku_init_sound(boolean useSfxPrefix)
{
	sUseSfxPrefix = useSfxPrefix;

	media_raw_audio_format format = media_raw_audio_format::wildcard;
	format.frame_rate = OUTPUT_RATE;
	format.channel_count = 2;
	format.format = media_raw_audio_format::B_AUDIO_FLOAT;
	format.byte_order = B_MEDIA_HOST_ENDIAN;
	format.buffer_size = 2048 * 2 * sizeof(float);

	sPlayer = new BSoundPlayer(&format, "Doom", play_buffer);
	if (sPlayer->InitCheck() != B_OK) {
		fprintf(stderr, "doom: no sound player, running silently\n");
		delete sPlayer;
		sPlayer = NULL;
		return false;
	}
	sPlayer->Start();
	sPlayer->SetHasData(true);
	return true;
}


static void
haiku_shutdown_sound()
{
	if (sPlayer == NULL)
		return;
	sPlayer->Stop();
	delete sPlayer;
	sPlayer = NULL;
}


static int
haiku_get_sfx_lump_num(sfxinfo_t* sfx)
{
	char name[9];
	if (sfx->link != NULL)
		sfx = sfx->link;
	if (sUseSfxPrefix)
		snprintf(name, sizeof(name), "ds%s", sfx->name);
	else
		snprintf(name, sizeof(name), "%s", sfx->name);
	return W_GetNumForName(name);
}


static void
haiku_update()
{
}


static void
haiku_update_params(int c, int vol, int sep)
{
	if (c < 0 || c >= NUM_CHANNELS || sPlayer == NULL)
		return;
	sChannelLock.Lock();
	set_params(sChannels[c], vol, sep);
	sChannelLock.Unlock();
}


static int
haiku_start_sound(sfxinfo_t* sfx, int c, int vol, int sep)
{
	if (c < 0 || c >= NUM_CHANNELS || sPlayer == NULL)
		return -1;

	int lump = sfx->lumpnum;
	const uint8* data = (const uint8*)W_CacheLumpNum(lump, PU_STATIC);
	int lumpLength = W_LumpLength(lump);
	// DMX sound: format 3, rate, length, 16 bytes of padding on both sides
	if (lumpLength < 8 || data[0] != 3 || data[1] != 0)
		return -1;
	uint32 rate = data[2] | (data[3] << 8);
	uint32 length = data[4] | (data[5] << 8) | (data[6] << 16)
		| ((uint32)data[7] << 24);
	if (length > (uint32)lumpLength - 8 || length <= 32)
		return -1;

	sChannelLock.Lock();
	channel& ch = sChannels[c];
	ch.samples = data + 8 + 16;
	ch.length = length - 32;
	ch.position = 0;
	ch.step = (uint32)(((uint64)rate << 16) / OUTPUT_RATE);
	set_params(ch, vol, sep);
	sChannelLock.Unlock();
	return c;
}


static void
haiku_stop_sound(int c)
{
	if (c < 0 || c >= NUM_CHANNELS || sPlayer == NULL)
		return;
	sChannelLock.Lock();
	sChannels[c].samples = NULL;
	sChannelLock.Unlock();
}


static boolean
haiku_sound_is_playing(int c)
{
	if (c < 0 || c >= NUM_CHANNELS)
		return false;
	return sChannels[c].samples != NULL;
}


static void
haiku_cache_sounds(sfxinfo_t* sounds, int count)
{
}


static snddevice_t sSoundDevices[] = {
	SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER,
	SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32,
};

extern "C" sound_module_t DG_sound_module = {
	sSoundDevices,
	sizeof(sSoundDevices) / sizeof(sSoundDevices[0]),
	haiku_init_sound,
	haiku_shutdown_sound,
	haiku_get_sfx_lump_num,
	haiku_update,
	haiku_update_params,
	haiku_start_sound,
	haiku_stop_sound,
	haiku_sound_is_playing,
	haiku_cache_sounds,
};


// no music: the module accepts the songs and stays silent

static boolean music_init() { return true; }
static void music_void() {}
static void music_volume(int) {}
static void* music_register(void*, int) { return (void*)1; }
static void music_unregister(void*) {}
static void music_play(void*, boolean) {}
static boolean music_playing() { return false; }

extern "C" music_module_t DG_music_module = {
	sSoundDevices,
	sizeof(sSoundDevices) / sizeof(sSoundDevices[0]),
	music_init,
	music_void,
	music_volume,
	music_void,
	music_void,
	music_register,
	music_unregister,
	music_play,
	music_void,
	music_playing,
	music_void,
};


//	#pragma mark - application


static status_t
game_thread(void*)
{
	doomgeneric_Create(sArgc, sArgv);
	while (true)
		doomgeneric_Tick();
	return B_OK;
}


class DoomApp : public BApplication {
public:
	DoomApp()
		:
		BApplication(kSignature)
	{
	}

	void ReadyToRun()
	{
		thread_id thread = spawn_thread(game_thread, "doom", B_NORMAL_PRIORITY,
			NULL);
		resume_thread(thread);
	}
};


int
main(int argc, char** argv)
{
	DoomApp app;

	// the WAD next to the program; settings and saves in
	// ~/config/settings/doomgeneric, since the package is read-only
	app_info info;
	BPath appDir;
	if (app.GetAppInfo(&info) == B_OK) {
		BPath path(&info.ref);
		path.GetParent(&appDir);
	}

	BPath settings;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settings) == B_OK) {
		settings.Append("doomgeneric");
		create_directory(settings.Path(), 0755);
		chdir(settings.Path());
	}

	bool haveIwad = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-iwad") == 0)
			haveIwad = true;
	}

	sArgv = new char*[argc + 3];
	sArgc = 0;
	for (int i = 0; i < argc; i++)
		sArgv[sArgc++] = argv[i];
	if (!haveIwad && appDir.InitCheck() == B_OK) {
		BPath wad(appDir);
		wad.Append("doom1.wad");
		sArgv[sArgc++] = strdup("-iwad");
		sArgv[sArgc++] = strdup(wad.Path());
	}
	sArgv[sArgc] = NULL;

	app.Run();
	return 0;
}
