/*
 * ac100audio — diagnostics for the audio of the Toshiba AC100 (Tegra 2 I2S1,
 * Realtek ALC5632 codec, TPA6017 speaker amplifier).
 *
 * Talks to /dev/audio/tegra_audio_diag (tegra_audio bring-up device): reads
 * and writes Tegra and codec registers and plays tones without the media kit.
 *
 *   ac100audio dump                 registers, decoded
 *   ac100audio tone [left|right|both] [ms] [Hz] [amplitude]
 *   ac100audio codec REG [VALUE]    read or write a codec register (hex)
 *   ac100audio reg ADDR [VALUE]     read or write a Tegra register (hex)
 *   ac100audio preset NAME          apply a codec configuration (see "presets")
 *   ac100audio stop-media | start-media
 *   ac100audio test                 guided listening test, writes a report
 */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <OS.h>


#define TEGRA_AUDIO_DIAG	0x41433130	// 'AC10'

enum {
	TEGRA_DIAG_READ32 = 1,
	TEGRA_DIAG_WRITE32,
	TEGRA_DIAG_CODEC_READ,
	TEGRA_DIAG_CODEC_WRITE,
	TEGRA_DIAG_TONE,
	TEGRA_DIAG_STATUS,
};

struct tegra_audio_diag {
	uint32	command;
	uint32	args[4];
	uint32	result[4];
};

static const char* kDevice = "/dev/audio/tegra_audio_diag";
static const char* kReport = "/boot/home/ac100audio-report.txt";
static const uint32 kSampleRate = 44100;

static int sDevice = -1;
static FILE* sReport = NULL;


// Everything printed also goes into the report, when there is one.
static void
out(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	if (sReport != NULL) {
		va_start(args, format);
		vfprintf(sReport, format, args);
		va_end(args);
		fflush(sReport);
	}
}


static status_t
diag(uint32 command, uint32 a0 = 0, uint32 a1 = 0, uint32 a2 = 0,
	uint32 a3 = 0, tegra_audio_diag* _result = NULL)
{
	tegra_audio_diag request;
	memset(&request, 0, sizeof(request));
	request.command = command;
	request.args[0] = a0;
	request.args[1] = a1;
	request.args[2] = a2;
	request.args[3] = a3;
	status_t status = ioctl(sDevice, TEGRA_AUDIO_DIAG, &request,
		sizeof(request)) < 0 ? errno : B_OK;
	if (_result != NULL)
		*_result = request;
	return status;
}


static uint32
reg_read(uint32 address)
{
	tegra_audio_diag result;
	if (diag(TEGRA_DIAG_READ32, address, 0, 0, 0, &result) != B_OK)
		return 0xdeadbeef;
	return result.result[0];
}


static void
reg_write(uint32 address, uint32 value)
{
	diag(TEGRA_DIAG_WRITE32, address, value);
}


static uint16
codec_read(uint8 reg)
{
	tegra_audio_diag result;
	if (diag(TEGRA_DIAG_CODEC_READ, reg, 0, 0, 0, &result) != B_OK)
		return 0xffff;
	return result.result[0];
}


static void
codec_write(uint8 reg, uint16 value)
{
	diag(TEGRA_DIAG_CODEC_WRITE, reg, value);
}


static void
codec_update(uint8 reg, uint16 mask, uint16 value)
{
	codec_write(reg, (codec_read(reg) & ~mask) | (value & mask));
}


static uint16
codec_private_read(uint8 index)
{
	codec_write(0x6a, index);
	return codec_read(0x6c);
}


// #pragma mark - dump


struct named_register {
	const char*	name;
	uint32		address;
};

static const named_register kTegraRegisters[] = {
	{ "CAR CLK_OUT_ENB_L", 0x60006010 },
	{ "CAR CLK_OUT_ENB_H", 0x60006014 },
	{ "CAR CLK_OUT_ENB_U", 0x60006018 },
	{ "CAR RST_DEVICES_L", 0x60006004 },
	{ "CAR PLLA_BASE", 0x600060b0 },
	{ "CAR PLLA_OUT", 0x600060b4 },
	{ "CAR PLLA_MISC", 0x600060bc },
	{ "CAR CLK_SOURCE_I2S1", 0x60006100 },
	{ "PINMUX TRISTATE_A", 0x70000014 },
	{ "PINMUX CTL_C", 0x70000088 },
	{ "DAS DAP_CTRL_SEL1", 0x70000c00 },
	{ "DAS DAC_INPUT_SEL1", 0x70000c40 },
	{ "I2S1 CTRL", 0x70002800 },
	{ "I2S1 STATUS", 0x70002804 },
	{ "I2S1 TIMING", 0x70002808 },
	{ "I2S1 FIFO_SCR", 0x7000280c },
	{ "GPIO W CNF", 0x6000d288 },
	{ "GPIO W OE", 0x6000d298 },
	{ "GPIO W IN", 0x6000d2b8 },
};

struct codec_name {
	uint8		reg;
	const char*	name;
};

static const codec_name kCodecNames[] = {
	{ 0x00, "reset" },
	{ 0x02, "speaker volume" },
	{ 0x04, "headphone volume" },
	{ 0x06, "mono/aux volume" },
	{ 0x08, "line in volume" },
	{ 0x0a, "phone volume" },
	{ 0x0c, "DAC (master) volume" },
	{ 0x0e, "mic volume" },
	{ 0x10, "mic routing / DAC to mixers" },
	{ 0x12, "ADC record gain" },
	{ 0x14, "ADC record mixer" },
	{ 0x1c, "output mixer control" },
	{ 0x22, "mic control" },
	{ 0x24, "digital boost" },
	{ 0x26, "power down control/status" },
	{ 0x34, "digital audio interface" },
	{ 0x36, "voice interface" },
	{ 0x3a, "power management 1" },
	{ 0x3c, "power management 2" },
	{ 0x3e, "power management 3" },
	{ 0x40, "general purpose 1 (clock)" },
	{ 0x42, "general purpose 2 (PLL source)" },
	{ 0x44, "PLL1 control" },
	{ 0x46, "PLL2 control" },
	{ 0x4c, "GPIO config" },
	{ 0x54, "GPIO status" },
	{ 0x5e, "misc control" },
	{ 0x60, "DAC clock control" },
	{ 0x62, "voice DAC clock" },
	{ 0x66, "pseudo stereo / spatial" },
	{ 0x68, "EQ control" },
	{ 0x6a, "private index" },
	{ 0x6c, "private data" },
	{ 0x7c, "vendor id 1" },
	{ 0x7e, "vendor id 2" },
};


static const char*
codec_name_of(int reg)
{
	for (size_t i = 0; i < sizeof(kCodecNames) / sizeof(kCodecNames[0]); i++) {
		if (kCodecNames[i].reg == reg)
			return kCodecNames[i].name;
	}
	return "";
}


static void
dump()
{
	tegra_audio_diag status;
	if (diag(TEGRA_DIAG_STATUS, 0, 0, 0, 0, &status) == B_OK) {
		out("driver: device %s by the media server, playback %s, %u buffers "
			"of %u frames\n", status.result[0] ? "opened" : "not opened",
			status.result[1] ? "running" : "stopped", status.result[3],
			status.result[2]);
	}

	out("\nTegra registers:\n");
	for (size_t i = 0; i < sizeof(kTegraRegisters) / sizeof(kTegraRegisters[0]);
			i++) {
		out("  %-22s %#010x = %#010x\n", kTegraRegisters[i].name,
			kTegraRegisters[i].address,
			reg_read(kTegraRegisters[i].address));
	}

	uint32 i2sSource = reg_read(0x60006100);
	uint32 gpioIn = reg_read(0x6000d2b8);
	out("\n  I2S1 clock: source %u (0 PLLA_OUT0, 3 clk_m), divisor %u.%u, "
		"master clock %s\n", i2sSource >> 30, (i2sSource & 0xff) / 2 + 1,
		(i2sSource & 1) * 5, (i2sSource & (1 << 28)) ? "on" : "OFF");
	out("  CDEV1 (codec MCLK) clock %s, pin %s, function %u (1 = PLLA_OUT)\n",
		(reg_read(0x60006018) & (1 << 30)) ? "on" : "OFF",
		(reg_read(0x70000014) & (1 << 4)) ? "TRISTATED" : "driven",
		(reg_read(0x70000088) >> 2) & 3);
	out("  headphone detect (GPIO W2): %u\n", (gpioIn >> 2) & 1);

	out("\nALC5632 registers:\n");
	for (int reg = 0; reg < 0x80; reg += 2) {
		uint16 value = codec_read(reg);
		out("  %02x = %04x  %s\n", reg, value, codec_name_of(reg));
	}
	out("  private 46 (class D generators) = %04x\n", codec_private_read(0x46));
}


// #pragma mark - tones


static int32
tone(uint32 channels, uint32 ms, uint32 frequency, uint32 amplitude)
{
	if (frequency < 50)
		frequency = 50;
	tegra_audio_diag result;
	status_t status = diag(TEGRA_DIAG_TONE, ms, kSampleRate / frequency,
		amplitude, channels, &result);
	if (status == B_BUSY) {
		out("il media server sta usando l'audio: prima esegui "
			"\"ac100audio stop-media\"\n");
		return -1;
	}
	if (status != B_OK) {
		out("tone failed: %s\n", strerror(status));
		return -1;
	}
	uint32 expected = ms * kSampleRate / 1000 / 1024;
	out("  played %u ms at %u Hz, %u DMA interrupts (about %u expected)\n",
		ms, frequency, result.result[0], expected);
	return result.result[0];
}


// #pragma mark - presets


struct preset {
	const char*	name;
	const char*	description;
	void		(*apply)();
};


static void
preset_default()
{
	// what the driver sets: speaker from the HP mixers, RP/+R, class AB
	codec_write(0x1c, 0x4708);
	codec_update(0x3c, 3 << 14, 1 << 15);
	codec_update(0x40, 1 << 15, 0);
}


static void
preset_loud()
{
	codec_write(0x02, 0x0000);
	codec_write(0x04, 0x0000);
	codec_write(0x0c, 0x0808);
}


static void
preset_class_d()
{
	// speaker from the speaker mixer, LN/-R, class D, with its generators
	codec_write(0x1c, 0xab08);
	codec_write(0x6a, 0x46);
	codec_write(0x6c, 0xffff);
	codec_update(0x3c, 3 << 14, 1 << 14);
}


static void
preset_ab_bit15_off()
{
	// class AB without bit 15 of register 3c (which Linux' header calls the
	// PLL1 power and its DAPM the class AB amplifier)
	codec_write(0x1c, 0x4708);
	codec_update(0x3c, 3 << 14, 0);
}


static void
preset_clock_bclk()
{
	// codec system clock from PLL1 fed by BCLK (2.8224 MHz -> 22.5792 MHz,
	// Linux' slave table), independent of MCLK
	codec_update(0x42, 3 << 12, 2 << 12);
	codec_write(0x44, 0x1ea0);
	codec_update(0x3c, 1 << 15, 1 << 15);
	snooze(10000);
	codec_update(0x40, 1 << 15, 1 << 15);
}


static void
preset_clock_mclk()
{
	codec_update(0x40, 1 << 15, 0);
}


static void
preset_all_outputs_on()
{
	// every output stage powered and unmuted: headphone amps, speaker
	// outputs, both amplifier classes, all mixers
	codec_update(0x3a, 0xcf03, 0xcf03);
	codec_update(0x3c, 0xc33c, 0xc33c);
	codec_update(0x3e, 0x3c00, 0x3c00);
	codec_update(0x10, 0x000e, 0);
	codec_update(0x02, 0x8080, 0);
	codec_update(0x04, 0x8080, 0);
	codec_update(0x0c, 0x8080, 0);
}


static const preset kPresets[] = {
	{ "default", "driver configuration (class AB, from the HP mixers)",
		preset_default },
	{ "loud", "speaker, headphones and DAC at full volume", preset_loud },
	{ "class-d", "speaker through the class D amplifier", preset_class_d },
	{ "ab-bit15-off", "class AB with bit 15 of 3c cleared",
		preset_ab_bit15_off },
	{ "clock-bclk", "codec clocked from BCLK through its PLL1",
		preset_clock_bclk },
	{ "clock-mclk", "codec clocked from MCLK (default)", preset_clock_mclk },
	{ "all-on", "every output stage powered and unmuted",
		preset_all_outputs_on },
};


static const preset*
find_preset(const char* name)
{
	for (size_t i = 0; i < sizeof(kPresets) / sizeof(kPresets[0]); i++) {
		if (strcmp(kPresets[i].name, name) == 0)
			return &kPresets[i];
	}
	return NULL;
}


// #pragma mark - media server


static void
stop_media()
{
	system("hey -s application/x-vnd.Be.media-server quit > /dev/null 2>&1");
	system("hey -s application/x-vnd.Be.addon-host quit > /dev/null 2>&1");
	for (int i = 0; i < 50; i++) {
		tegra_audio_diag status;
		if (diag(TEGRA_DIAG_STATUS, 0, 0, 0, 0, &status) == B_OK
			&& !status.result[0]) {
			out("media server fermato\n");
			return;
		}
		snooze(100000);
	}
	out("il media server tiene ancora occupato l'audio\n");
}


static void
start_media()
{
	system("/boot/system/servers/media_server > /dev/null 2>&1 &");
	out("media server riavviato\n");
}


// #pragma mark - guided test


static bool
ask(const char* question)
{
	out("%s [s/n] ", question);
	fflush(stdout);
	char line[32];
	if (fgets(line, sizeof(line), stdin) == NULL)
		line[0] = 'n';
	bool yes = line[0] == 'y' || line[0] == 'Y' || line[0] == 's'
		|| line[0] == 'S';
	if (sReport != NULL)
		fprintf(sReport, "%s\n", yes ? "s" : "n");
	return yes;
}


static void
wait_enter(const char* message)
{
	out("%s (Invio) ", message);
	fflush(stdout);
	char line[32];
	fgets(line, sizeof(line), stdin);
	if (sReport != NULL)
		fprintf(sReport, "\n");
}


struct test_step {
	const char*	preset;
	const char*	description;
	uint32		channels;
	uint32		frequency;
};

static const test_step kSteps[] = {
	{ "default", "configurazione del driver, due canali", 3, 440 },
	{ "loud", "uguale, volume massimo", 3, 440 },
	{ NULL, "solo canale sinistro", 1, 660 },
	{ NULL, "solo canale destro", 2, 880 },
	{ "class-d", "altoparlante con l'amplificatore in classe D", 3, 440 },
	{ "ab-bit15-off", "classe AB, bit 15 di 3c spento", 3, 440 },
	{ "all-on", "tutti gli stadi di uscita accesi", 3, 440 },
	{ "clock-bclk", "codec col clock da BCLK (senza MCLK)", 3, 440 },
};


static void
run_steps(const char* output, bool* heard)
{
	for (size_t i = 0; i < sizeof(kSteps) / sizeof(kSteps[0]); i++) {
		const test_step& step = kSteps[i];
		out("\n[%s %zu] %s\n", output, i + 1, step.description);
		if (step.preset != NULL)
			find_preset(step.preset)->apply();
		out("  codec 02=%04x 04=%04x 0c=%04x 1c=%04x 3a=%04x 3c=%04x 3e=%04x "
			"40=%04x 26=%04x, headphone detect %u\n", codec_read(0x02),
			codec_read(0x04), codec_read(0x0c), codec_read(0x1c),
			codec_read(0x3a), codec_read(0x3c), codec_read(0x3e),
			codec_read(0x40), codec_read(0x26),
			(reg_read(0x6000d2b8) >> 2) & 1);
		tone(step.channels, 2000, step.frequency, 12000);
		heard[i] = ask("  hai sentito il tono?");
	}
	// back to the driver's own clock
	preset_clock_mclk();
}


static void
guided_test()
{
	sReport = fopen(kReport, "w");
	out("ac100audio: test guidato\n========================\n\n");
	stop_media();
	dump();

	const size_t count = sizeof(kSteps) / sizeof(kSteps[0]);
	bool speaker[count];
	bool headphones[count];

	wait_enter("\nSTACCA le cuffie: i toni escono dall'altoparlante.");
	run_steps("altoparlante", speaker);

	// the steps leave the codec changed: start every pass from the driver's
	// configuration again
	preset_default();
	codec_write(0x02, 0x0404);
	codec_write(0x04, 0x0404);
	codec_write(0x0c, 0x1010);

	wait_enter("\nORA COLLEGA le cuffie.");
	out("rilevamento cuffie (GPIO W2) ora: %u\n",
		(reg_read(0x6000d2b8) >> 2) & 1);
	run_steps("cuffie", headphones);

	out("\nRiepilogo\n---------\n");
	for (size_t i = 0; i < count; i++) {
		out("  %-48s altoparlante %-2s cuffie %s\n", kSteps[i].description,
			speaker[i] ? "si" : "no", headphones[i] ? "si" : "no");
	}
	out("\nRapporto scritto in %s\n", kReport);
	fclose(sReport);
	sReport = NULL;

	start_media();
}


// #pragma mark -


static void
usage()
{
	printf("usage: ac100audio dump\n"
		"       ac100audio tone [left|right|both] [ms] [Hz] [amplitude]\n"
		"       ac100audio codec REG [VALUE]\n"
		"       ac100audio reg ADDRESS [VALUE]\n"
		"       ac100audio preset NAME | presets\n"
		"       ac100audio stop-media | start-media\n"
		"       ac100audio test\n");
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		usage();
		return 1;
	}

	sDevice = open(kDevice, O_RDWR);
	if (sDevice < 0) {
		fprintf(stderr, "cannot open %s: %s\n", kDevice, strerror(errno));
		return 1;
	}

	const char* command = argv[1];
	if (strcmp(command, "dump") == 0)
		dump();
	else if (strcmp(command, "tone") == 0) {
		uint32 channels = 3;
		int arg = 2;
		if (argc > arg && strcmp(argv[arg], "left") == 0)
			channels = 1, arg++;
		else if (argc > arg && strcmp(argv[arg], "right") == 0)
			channels = 2, arg++;
		else if (argc > arg && strcmp(argv[arg], "both") == 0)
			arg++;
		uint32 ms = argc > arg ? atoi(argv[arg++]) : 2000;
		uint32 frequency = argc > arg ? atoi(argv[arg++]) : 440;
		uint32 amplitude = argc > arg ? atoi(argv[arg++]) : 12000;
		tone(channels, ms, frequency, amplitude);
	} else if (strcmp(command, "codec") == 0 && argc >= 3) {
		uint8 reg = strtoul(argv[2], NULL, 16);
		if (argc >= 4)
			codec_write(reg, strtoul(argv[3], NULL, 16));
		printf("codec %02x = %04x\n", reg, codec_read(reg));
	} else if (strcmp(command, "reg") == 0 && argc >= 3) {
		uint32 address = strtoul(argv[2], NULL, 16);
		if (argc >= 4)
			reg_write(address, strtoul(argv[3], NULL, 16));
		printf("%#010x = %#010x\n", address, reg_read(address));
	} else if (strcmp(command, "presets") == 0) {
		for (size_t i = 0; i < sizeof(kPresets) / sizeof(kPresets[0]); i++)
			printf("  %-14s %s\n", kPresets[i].name, kPresets[i].description);
	} else if (strcmp(command, "preset") == 0 && argc >= 3) {
		const preset* preset = find_preset(argv[2]);
		if (preset == NULL) {
			fprintf(stderr, "unknown preset %s\n", argv[2]);
			return 1;
		}
		preset->apply();
		printf("%s: %s\n", preset->name, preset->description);
	} else if (strcmp(command, "stop-media") == 0)
		stop_media();
	else if (strcmp(command, "start-media") == 0)
		start_media();
	else if (strcmp(command, "test") == 0)
		guided_test();
	else {
		usage();
		return 1;
	}

	close(sDevice);
	return 0;
}
