/*
 * ac100power — how much power does the Toshiba AC100 draw, and where does it
 * go? Step 0 of the cpuidle work: a baseline before writing any kernel code.
 *
 *   ac100power watch [seconds]   battery current, voltage, temperatures and
 *                                CPU idle time every 2 seconds
 *   ac100power bench [seconds]   runs the phases below (default 150 s each)
 *                                and writes /boot/home/ac100power-report.txt
 *
 * The current comes straight from the embedded controller (NVEC battery
 * request 03, instantaneous, in mA) through /dev/misc/nvec_diag; the nvec
 * driver itself asks for it only once every 20 seconds. The bench needs the
 * AC100 on battery: on the charger the EC reports the charging current.
 *
 * Phases: idle; idle with the backlight at its lowest and at its highest;
 * one and two busy threads; CPU 1 disabled, idle and with one busy thread.
 * Each phase waits 20 s for things to settle, then samples every 2 s.
 * Run it detached (nohup ... &) and leave the machine alone meanwhile.
 *
 *   ac100power floor [seconds]   where does the idle floor go? With the
 *                                backlight off throughout: Wi-Fi down, then
 *                                also the Bluetooth server stopped, then also
 *                                the panel's power (GPIO A4, vdd_pnl) off,
 *                                then everything back on. Writes
 *                                /boot/home/ac100power-floor.txt. The SSH
 *                                connection drops while Wi-Fi is down.
 */

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Application.h>
#include <OS.h>
#include <Screen.h>


extern "C" status_t _kern_set_cpu_enabled(int32 cpu, bool enabled);


#define NVEC_DIAG_REQUEST	0x4e563130	// 'NV10'
#define NVEC_MSG_SIZE		34

struct nvec_diag_request {
	uint8	size;
	uint8	request[NVEC_MSG_SIZE - 1];
	uint8	replySize;
	uint8	reply[NVEC_MSG_SIZE];
};

enum {
	BAT_SLOT_STATUS		= 0,
	BAT_VOLTAGE			= 1,
	BAT_CURRENT			= 3,
	BAT_AVERAGE_CURRENT	= 4,
};

static const char* kReportPath = "/boot/home/ac100power-report.txt";
static const char* kFloorReportPath = "/boot/home/ac100power-floor.txt";
static const char* kWifiDevice = "/dev/net/ralinkwifi/0";
static const char* kBluetoothService = "x-vnd.haiku-bluetooth_server";

// Register access through the tegra_audio bring-up device (0x6000xxxx and
// 0x7000xxxx only), as ac100audio does.
#define TEGRA_AUDIO_DIAG	0x41433130	// 'AC10'
#define TEGRA_DIAG_READ32	1
#define TEGRA_DIAG_WRITE32	2

struct tegra_audio_diag {
	uint32	command;
	uint32	args[4];
	uint32	result[4];
};

// GPIO A4 (port A, bank 0, bit 4) switches vdd_pnl, the panel's supply
#define GPIO_A_CNF			0x6000d000
#define GPIO_A_OE			0x6000d010
#define GPIO_A_OUT			0x6000d020
#define GPIO_A_MSK_OUT		0x6000d820
#define PANEL_POWER_BIT		4
static const bigtime_t kSampleInterval = 2000000;
static const bigtime_t kSettleTime = 20000000;

static int sDiag = -1;
static FILE* sReport;
static volatile bool sStopBusy;


static void
out(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	fflush(stdout);
	if (sReport != NULL) {
		va_start(args, format);
		vfprintf(sReport, format, args);
		va_end(args);
		fflush(sReport);
	}
}


/*!	Asks the EC for one battery value. Returns the reply's payload size, or
	-1; the payload starts at reply[4] (class, size, request, status).
*/
static int
battery_request(uint8 value, uint8* payload, int maxSize)
{
	nvec_diag_request request = {};
	request.size = 2;
	request.request[0] = 0x02;	// NVEC_BAT
	request.request[1] = value;
	if (ioctl(sDiag, NVEC_DIAG_REQUEST, &request, sizeof(request)) != 0)
		return -1;
	if (request.replySize < 4 || request.reply[2] != value
		|| request.reply[3] != 0) {
		return -1;
	}
	int size = request.replySize - 4;
	if (size > maxSize)
		size = maxSize;
	memcpy(payload, request.reply + 4, size);
	return size;
}


static bool
battery_value16(uint8 value, int32& result)
{
	uint8 payload[NVEC_MSG_SIZE];
	if (battery_request(value, payload, sizeof(payload)) < 2)
		return false;
	result = (int16)(payload[0] | (payload[1] << 8));
	return true;
}


/*!	0 idle (full or not charging), 1 charging, 2 discharging, -1 no battery
	or no answer.
*/
static int
battery_state(int32* percent)
{
	uint8 payload[NVEC_MSG_SIZE];
	if (battery_request(BAT_SLOT_STATUS, payload, sizeof(payload)) < 2)
		return -1;
	if ((payload[0] & 1) == 0)
		return -1;
	if (percent != NULL)
		*percent = payload[1];
	return (payload[0] >> 1) & 3;
}


static const char*
state_name(int state)
{
	switch (state) {
		case 0:
			return "not charging";
		case 1:
			return "charging";
		case 2:
			return "discharging";
	}
	return "no battery";
}


/*!	/dev/sensor/adt7461 prints "cpu 51.50 C" and "board 41.00 C". */
static void
read_temperatures(float& cpu, float& board)
{
	cpu = board = NAN;
	int fd = open("/dev/sensor/adt7461", O_RDONLY);
	if (fd < 0)
		return;
	char buffer[256];
	ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	if (bytes <= 0)
		return;
	buffer[bytes] = '\0';
	const char* line = strstr(buffer, "cpu ");
	if (line != NULL)
		cpu = strtof(line + 4, NULL);
	line = strstr(buffer, "board ");
	if (line != NULL)
		board = strtof(line + 6, NULL);
}


struct CPUTimes {
	bigtime_t	when;
	bigtime_t	active[8];
	bool		enabled[8];
	int32		count;
};


static void
read_cpu_times(CPUTimes& times)
{
	system_info info;
	get_system_info(&info);
	times.count = min_c(info.cpu_count, 8);
	cpu_info cpus[8];
	get_cpu_info(0, times.count, cpus);
	times.when = system_time();
	for (int32 i = 0; i < times.count; i++) {
		times.active[i] = cpus[i].active_time;
		times.enabled[i] = cpus[i].enabled;
	}
}


/*!	Idle share of the enabled CPUs between two readings, in percent. */
static float
idle_percent(const CPUTimes& before, const CPUTimes& after)
{
	bigtime_t elapsed = after.when - before.when;
	if (elapsed <= 0)
		return NAN;
	bigtime_t total = 0;
	bigtime_t active = 0;
	for (int32 i = 0; i < after.count; i++) {
		if (!after.enabled[i])
			continue;
		total += elapsed;
		active += after.active[i] - before.active[i];
	}
	if (total == 0)
		return NAN;
	return 100.0f * (total - active) / total;
}


struct Sample {
	int32	current;	// mA, negative when discharging
	int32	voltage;	// mV
	float	cpuTemp;
	float	boardTemp;
	float	idle;
};


static bool
take_sample(Sample& sample, const CPUTimes& before, CPUTimes& after)
{
	read_cpu_times(after);
	sample.idle = idle_percent(before, after);
	read_temperatures(sample.cpuTemp, sample.boardTemp);
	return battery_value16(BAT_CURRENT, sample.current)
		&& battery_value16(BAT_VOLTAGE, sample.voltage);
}


static int
watch(int seconds)
{
	int32 percent = 0;
	int state = battery_state(&percent);
	out("battery: %s, %" B_PRId32 " %%\n", state_name(state), percent);
	out("  time  current   avg     voltage  power    cpu C  board C  idle\n");

	CPUTimes before;
	read_cpu_times(before);
	bigtime_t start = system_time();
	while (seconds <= 0 || system_time() - start < seconds * 1000000LL) {
		snooze(kSampleInterval);
		CPUTimes after;
		Sample sample;
		int32 average = 0;
		if (!take_sample(sample, before, after)
			|| !battery_value16(BAT_AVERAGE_CURRENT, average)) {
			out("no answer from the EC\n");
			before = after;
			continue;
		}
		out("%5.0f s %5" B_PRId32 " mA %5" B_PRId32 " mA %6" B_PRId32 " mV"
			" %6.0f mW %6.1f %7.1f %6.1f %%\n",
			(system_time() - start) / 1e6, sample.current, average,
			sample.voltage, sample.current * (float)sample.voltage / 1000,
			sample.cpuTemp, sample.boardTemp, sample.idle);
		before = after;
	}
	return 0;
}


static status_t
busy_thread(void*)
{
	volatile uint32 value = 1;
	while (!sStopBusy) {
		for (int i = 0; i < 100000; i++)
			value = value * 1664525 + 1013904223;
	}
	return B_OK;
}


struct Phase {
	const char*	name;
	int			busyThreads;
	bool		cpu1Off;
	float		brightness;		// < 0: leave it as it is
};


struct PhaseResult {
	int		samples;
	float	current;		// mean, mA (discharging is positive here)
	float	deviation;
	float	power;			// mW
	float	cpuTemp;
	float	boardTemp;
	float	idle;
};


static PhaseResult
run_phase(const Phase& phase, int seconds, bool& onBattery)
{
	PhaseResult result = {};
	out("\n== %s\n", phase.name);

	BScreen screen;
	float oldBrightness = -1;
	if (phase.brightness >= 0) {
		if (screen.GetBrightness(&oldBrightness) != B_OK
			|| screen.SetBrightness(phase.brightness) != B_OK) {
			out("   cannot change the brightness, phase skipped\n");
			result.samples = 0;
			return result;
		}
	}
	if (phase.cpu1Off) {
		status_t status = _kern_set_cpu_enabled(1, false);
		if (status != B_OK) {
			out("   cannot disable CPU 1: %s, phase skipped\n",
				strerror(status));
			return result;
		}
	}

	sStopBusy = false;
	thread_id threads[4];
	for (int i = 0; i < phase.busyThreads; i++) {
		threads[i] = spawn_thread(busy_thread, "busy", B_NORMAL_PRIORITY,
			NULL);
		resume_thread(threads[i]);
	}

	snooze(kSettleTime);

	CPUTimes before;
	read_cpu_times(before);
	double sum = 0, sumSquares = 0, power = 0, cpuTemp = 0, boardTemp = 0,
		idle = 0;
	bigtime_t start = system_time();
	while (system_time() - start < seconds * 1000000LL) {
		snooze(kSampleInterval);
		if (battery_state(NULL) != 2) {
			out("   the AC100 is not running on battery any more\n");
			onBattery = false;
			break;
		}
		CPUTimes after;
		Sample sample;
		if (!take_sample(sample, before, after)) {
			before = after;
			continue;
		}
		before = after;
		float drawn = -sample.current;
		sum += drawn;
		sumSquares += drawn * drawn;
		power += drawn * sample.voltage / 1000.0;
		cpuTemp += sample.cpuTemp;
		boardTemp += sample.boardTemp;
		idle += sample.idle;
		result.samples++;
		out("   %4" B_PRId32 " mA %5" B_PRId32 " mV  cpu %.1f C  idle %.1f %%\n",
			-sample.current, sample.voltage, sample.cpuTemp, sample.idle);
	}

	sStopBusy = true;
	for (int i = 0; i < phase.busyThreads; i++) {
		status_t exitValue;
		wait_for_thread(threads[i], &exitValue);
	}
	if (phase.cpu1Off)
		_kern_set_cpu_enabled(1, true);
	if (oldBrightness >= 0)
		screen.SetBrightness(oldBrightness);

	if (result.samples > 0) {
		int n = result.samples;
		result.current = sum / n;
		result.deviation = sqrt(fmax(0, sumSquares / n
			- result.current * result.current));
		result.power = power / n;
		result.cpuTemp = cpuTemp / n;
		result.boardTemp = boardTemp / n;
		result.idle = idle / n;
	}
	return result;
}


static int
bench(int seconds)
{
	int32 percent = 0;
	int state = battery_state(&percent);
	if (state != 2) {
		printf("The AC100 must run on battery (now: %s). Unplug the charger "
			"and start again.\n", state_name(state));
		return 1;
	}

	sReport = fopen(kReportPath, "w");
	time_t now = time(NULL);
	out("ac100power bench, %s", ctime(&now));
	out("battery %" B_PRId32 " %%, %d s per phase, sampling every 2 s after "
		"20 s of settling\n", percent, seconds);

	BScreen screen;
	float brightness = -1;
	screen.GetBrightness(&brightness);
	out("brightness now %.2f\n", brightness);

	static const Phase kPhases[] = {
		{ "idle", 0, false, -1 },
		{ "idle, backlight lowest", 0, false, 0.0f },
		{ "idle, backlight highest", 0, false, 1.0f },
		{ "1 busy thread", 1, false, -1 },
		{ "2 busy threads", 2, false, -1 },
		{ "CPU 1 off, idle", 0, true, -1 },
		{ "CPU 1 off, 1 busy thread", 1, true, -1 },
		{ "idle again", 0, false, -1 },
	};
	const int count = B_COUNT_OF(kPhases);
	PhaseResult results[count];
	bool onBattery = true;
	int done = 0;
	for (; done < count && onBattery; done++)
		results[done] = run_phase(kPhases[done], seconds, onBattery);

	out("\nSummary (current drawn from the battery)\n");
	out("%-26s %8s %7s %8s %7s %7s %6s %4s\n", "phase", "mA", "+-", "mW",
		"cpu C", "board", "idle", "n");
	for (int i = 0; i < done; i++) {
		const PhaseResult& r = results[i];
		if (r.samples == 0) {
			out("%-26s (no samples)\n", kPhases[i].name);
			continue;
		}
		out("%-26s %8.0f %7.0f %8.0f %7.1f %7.1f %5.1f%% %4d\n",
			kPhases[i].name, r.current, r.deviation, r.power, r.cpuTemp,
			r.boardTemp, r.idle, r.samples);
	}
	if (sReport != NULL) {
		fclose(sReport);
		printf("Report written to %s\n", kReportPath);
	}
	return 0;
}


// #pragma mark - floor


static bool
register_access(uint32 command, uint32 address, uint32 value, uint32* result)
{
	int fd = open("/dev/audio/tegra_audio_diag", O_RDWR);
	if (fd < 0)
		return false;
	tegra_audio_diag request = {};
	request.command = command;
	request.args[0] = address;
	request.args[1] = value;
	bool ok = ioctl(fd, TEGRA_AUDIO_DIAG, &request, sizeof(request)) == 0;
	close(fd);
	if (ok && result != NULL)
		*result = request.result[0];
	return ok;
}


/*!	Switches vdd_pnl through GPIO A4, which the firmware left as an output.
	Refuses if the pin is not configured that way.
*/
static bool
set_panel_power(bool on)
{
	uint32 cnf, oe, level;
	if (!register_access(TEGRA_DIAG_READ32, GPIO_A_CNF, 0, &cnf)
		|| !register_access(TEGRA_DIAG_READ32, GPIO_A_OE, 0, &oe)) {
		out("   cannot read the GPIO registers\n");
		return false;
	}
	uint32 bit = 1 << PANEL_POWER_BIT;
	if ((cnf & bit) == 0 || (oe & bit) == 0) {
		out("   GPIO A4 is not a GPIO output (CNF %#" B_PRIx32 ", OE %#"
			B_PRIx32 "), leaving it alone\n", cnf, oe);
		return false;
	}
	register_access(TEGRA_DIAG_WRITE32, GPIO_A_MSK_OUT,
		(bit << 8) | (on ? bit : 0), NULL);
	register_access(TEGRA_DIAG_READ32, GPIO_A_OUT, 0, &level);
	out("   panel power %s (GPIO A OUT %#" B_PRIx32 ")\n", on ? "on" : "off",
		level);
	return ((level & bit) != 0) == on;
}


static bool
team_running(const char* name)
{
	int32 cookie = 0;
	team_info info;
	size_t length = strlen(name);
	while (get_next_team_info(&cookie, &info) == B_OK) {
		// the program itself, not one that has the name among its arguments
		const char* end = strchr(info.args, ' ');
		size_t programLength = end != NULL ? end - info.args
			: strlen(info.args);
		if (programLength >= length
			&& strncmp(info.args + programLength - length, name, length) == 0
			&& (programLength == length
				|| info.args[programLength - length - 1] == '/')) {
			return true;
		}
	}
	return false;
}


static void
run_command(const char* format, ...)
{
	char command[256];
	va_list args;
	va_start(args, format);
	vsnprintf(command, sizeof(command), format, args);
	va_end(args);
	int result = system(command);
	out("   $ %s -> %d\n", command, result);
}


static void
set_wifi(bool up)
{
	run_command("ifconfig %s %s", kWifiDevice, up ? "up" : "down");
	if (!up)
		return;

	// wait until it has joined the network again
	for (int i = 0; i < 60; i++) {
		snooze(1000000);
		FILE* pipe = popen("ifconfig /dev/net/ralinkwifi/0", "r");
		if (pipe == NULL)
			continue;
		char line[256];
		bool linked = false, address = false;
		while (fgets(line, sizeof(line), pipe) != NULL) {
			if (strstr(line, " link") != NULL)
				linked = true;
			if (strstr(line, "inet addr:") != NULL
				&& strstr(line, "inet addr: 0.0.0.0") == NULL) {
				address = true;
			}
		}
		pclose(pipe);
		if (linked && address) {
			out("   Wi-Fi back after %d s\n", i + 1);
			return;
		}
		if (i == 30)
			run_command("ifconfig %s up", kWifiDevice);
	}
	out("   Wi-Fi did not come back within 60 s\n");
}


static void
set_bluetooth(bool on)
{
	run_command("launch_roster %s %s", on ? "start" : "stop",
		kBluetoothService);
	snooze(2000000);
	out("   bluetooth_server %s\n",
		team_running("bluetooth_server") ? "running" : "not running");
}


struct FloorState {
	bool	wifi;
	bool	bluetooth;
	bool	panel;
};


static PhaseResult
measure(int seconds, bool& onBattery)
{
	PhaseResult result = {};
	snooze(kSettleTime);

	CPUTimes before;
	read_cpu_times(before);
	double sum = 0, sumSquares = 0, power = 0, cpuTemp = 0, boardTemp = 0,
		idle = 0;
	bigtime_t start = system_time();
	while (system_time() - start < seconds * 1000000LL) {
		snooze(kSampleInterval);
		if (battery_state(NULL) != 2) {
			out("   the AC100 is not running on battery any more\n");
			onBattery = false;
			break;
		}
		CPUTimes after;
		Sample sample;
		if (!take_sample(sample, before, after)) {
			before = after;
			continue;
		}
		before = after;
		float drawn = -sample.current;
		sum += drawn;
		sumSquares += drawn * drawn;
		power += drawn * sample.voltage / 1000.0;
		cpuTemp += sample.cpuTemp;
		boardTemp += sample.boardTemp;
		idle += sample.idle;
		result.samples++;
		out("   %4" B_PRId32 " mA %5" B_PRId32 " mV  board %.1f C  idle "
			"%.1f %%\n", -sample.current, sample.voltage, sample.boardTemp,
			sample.idle);
	}

	if (result.samples > 0) {
		int n = result.samples;
		result.current = sum / n;
		result.deviation = sqrt(fmax(0, sumSquares / n
			- result.current * result.current));
		result.power = power / n;
		result.cpuTemp = cpuTemp / n;
		result.boardTemp = boardTemp / n;
		result.idle = idle / n;
	}
	return result;
}


static int
floor_bench(int seconds)
{
	int32 percent = 0;
	int state = battery_state(&percent);
	if (state != 2) {
		printf("The AC100 must run on battery (now: %s). Unplug the charger "
			"and start again.\n", state_name(state));
		return 1;
	}

	sReport = fopen(kFloorReportPath, "w");
	time_t now = time(NULL);
	out("ac100power floor, %s", ctime(&now));
	out("battery %" B_PRId32 " %%, %d s per phase, sampling every 2 s after "
		"20 s of settling, backlight off throughout\n", percent, seconds);

	BScreen screen;
	float brightness = -1;
	screen.GetBrightness(&brightness);
	out("brightness now %.2f, switching the backlight off\n", brightness);
	screen.SetBrightness(0.0f);

	struct FloorPhase {
		const char*	name;
		FloorState	state;
	};
	static const FloorPhase kPhases[] = {
		{ "everything on", { true, true, true } },
		{ "Wi-Fi down", { false, true, true } },
		{ "Wi-Fi down, Bluetooth stopped", { false, false, true } },
		{ "... and panel power off", { false, false, false } },
		{ "everything on again", { true, true, true } },
	};
	const int count = B_COUNT_OF(kPhases);
	PhaseResult results[count];
	FloorState current = { true, true, true };
	bool onBattery = true;
	int done = 0;
	for (; done < count && onBattery; done++) {
		const FloorState& wanted = kPhases[done].state;
		out("\n== %s\n", kPhases[done].name);
		// switch things on again in the reverse order
		if (wanted.panel && !current.panel)
			set_panel_power(true);
		if (wanted.bluetooth && !current.bluetooth)
			set_bluetooth(true);
		if (wanted.wifi && !current.wifi)
			set_wifi(true);
		if (!wanted.wifi && current.wifi)
			set_wifi(false);
		if (!wanted.bluetooth && current.bluetooth)
			set_bluetooth(false);
		if (!wanted.panel && current.panel && !set_panel_power(false)) {
			// the pin is not what we think: do not count this phase
			results[done] = PhaseResult();
			current = wanted;
			current.panel = true;
			continue;
		}
		current = wanted;
		results[done] = measure(seconds, onBattery);
	}

	// whatever happened, everything back on
	out("\n== restoring\n");
	if (!current.panel)
		set_panel_power(true);
	if (!current.bluetooth)
		set_bluetooth(true);
	if (!current.wifi)
		set_wifi(true);
	if (brightness >= 0)
		screen.SetBrightness(brightness);

	out("\nSummary (current drawn from the battery, backlight off)\n");
	out("%-32s %8s %7s %8s %7s %6s %4s\n", "phase", "mA", "+-", "mW",
		"board", "idle", "n");
	for (int i = 0; i < done; i++) {
		const PhaseResult& r = results[i];
		if (r.samples == 0) {
			out("%-32s (no samples)\n", kPhases[i].name);
			continue;
		}
		out("%-32s %8.0f %7.0f %8.0f %7.1f %5.1f%% %4d\n", kPhases[i].name,
			r.current, r.deviation, r.power, r.boardTemp, r.idle, r.samples);
	}
	if (sReport != NULL) {
		fclose(sReport);
		printf("Report written to %s\n", kFloorReportPath);
	}
	return 0;
}


/*!	Screen on, then asleep through DPMS (B_DPMS_OFF), then on again: what
	does the display driver save while the screen sleeps?
*/
static int
dpms_bench(int seconds)
{
	int32 percent = 0;
	int state = battery_state(&percent);
	bool onBattery = state == 2;
	out("ac100power dpms, battery %s %" B_PRId32 " %%, %d s per phase\n",
		state_name(state), percent, seconds);
	if (!onBattery)
		out("not on battery: only checking that the screen sleeps and wakes\n");

	BScreen screen;
	uint32 capabilities = screen.DPMSCapabilites();
	out("DPMS capabilities %#" B_PRIx32 ", state %#" B_PRIx32 "\n",
		capabilities, screen.DPMSState());

	PhaseResult on = {}, off = {}, again = {};
	out("\n== screen on\n");
	if (onBattery)
		on = measure(seconds, onBattery);
	else
		snooze(seconds * 1000000LL);

	out("\n== DPMS off\n");
	bigtime_t start = system_time();
	status_t status = screen.SetDPMS(B_DPMS_OFF);
	out("   SetDPMS(off): %s in %.0f ms, state %#" B_PRIx32 "\n",
		strerror(status), (system_time() - start) / 1000.0, screen.DPMSState());
	if (onBattery)
		off = measure(seconds, onBattery);
	else
		snooze(seconds * 1000000LL);

	out("\n== screen on again\n");
	start = system_time();
	status = screen.SetDPMS(B_DPMS_ON);
	out("   SetDPMS(on): %s in %.0f ms, state %#" B_PRIx32 "\n",
		strerror(status), (system_time() - start) / 1000.0, screen.DPMSState());
	if (onBattery)
		again = measure(seconds, onBattery);

	if (on.samples > 0 && off.samples > 0) {
		out("\nscreen on        %5.0f mA %6.0f mW\n", on.current, on.power);
		out("DPMS off         %5.0f mA %6.0f mW\n", off.current, off.power);
		if (again.samples > 0) {
			out("screen on again  %5.0f mA %6.0f mW\n", again.current,
				again.power);
		}
		out("saved            %5.0f mA %6.0f mW\n", on.current - off.current,
			on.power - off.power);
	}
	return 0;
}


int
main(int argc, char** argv)
{
	if (argc < 2 || (strcmp(argv[1], "watch") != 0
			&& strcmp(argv[1], "bench") != 0
			&& strcmp(argv[1], "floor") != 0
			&& strcmp(argv[1], "dpms") != 0)) {
		printf("usage: ac100power watch [seconds] | bench [seconds per "
			"phase] | floor [seconds per phase] | dpms [seconds per phase]\n");
		return 1;
	}

	sDiag = open("/dev/misc/nvec_diag", O_RDWR);
	if (sDiag < 0) {
		printf("cannot open /dev/misc/nvec_diag: %s\n", strerror(errno));
		return 1;
	}

	int seconds = argc > 2 ? atoi(argv[2]) : 0;
	if (strcmp(argv[1], "watch") == 0)
		return watch(seconds);

	// BScreen needs a connection to app_server
	BApplication app("application/x-vnd.ac100-power");
	if (strcmp(argv[1], "floor") == 0)
		return floor_bench(seconds > 0 ? seconds : 120);
	if (strcmp(argv[1], "dpms") == 0)
		return dpms_bench(seconds > 0 ? seconds : 60);
	return bench(seconds > 0 ? seconds : 150);
}
