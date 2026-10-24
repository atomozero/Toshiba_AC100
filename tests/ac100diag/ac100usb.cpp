/*
 * ac100usb — diagnostics for the internal USB hub of the Toshiba AC100
 * (SMSC 0424:2513 on the third EHCI controller; port 2 is the webcam,
 * port 3 the external USB port).
 *
 * Sends hub class requests through usb_raw:
 *
 *   ac100usb status                 hub descriptor and port status
 *   ac100usb reset PORT [times] [settle ms]
 *   ac100usb power PORT off|on|cycle [ms off]
 *   ac100usb suspend PORT           suspend, then resume and reset
 *   ac100usb test                   guided test of the webcam port, report
 */

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <OS.h>
#include <ByteOrder.h>
#include <USBKit.h>


static const uint16 kHubVendor = 0x0424;
static const uint16 kHubProduct = 0x2513;
static const char* kReport = "/boot/home/ac100usb-report.txt";

// hub class requests (USB 2.0, 11.24)
static const uint8 kGetStatus = 0x00;
static const uint8 kClearFeature = 0x01;
static const uint8 kSetFeature = 0x03;
static const uint8 kGetDescriptor = 0x06;
static const uint8 kToPort = 0x23;			// class, other, out
static const uint8 kFromPort = 0xa3;		// class, other, in
static const uint8 kFromHub = 0xa0;			// class, device, in

static const uint16 kPortSuspend = 2;
static const uint16 kPortReset = 4;
static const uint16 kPortPower = 8;
static const uint16 kCPortConnection = 16;
static const uint16 kCPortEnable = 17;
static const uint16 kCPortSuspend = 18;
static const uint16 kCPortReset = 20;

static FILE* sReport = NULL;


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


static BUSBDevice*
find_hub(const char* directory)
{
	DIR* dir = opendir(directory);
	if (dir == NULL)
		return NULL;

	BUSBDevice* hub = NULL;
	struct dirent* entry;
	while (hub == NULL && (entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;
		char path[B_PATH_NAME_LENGTH];
		snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
		struct stat st;
		if (stat(path, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			hub = find_hub(path);
			continue;
		}
		BUSBDevice* device = new BUSBDevice(path);
		// AC100USB_ANY_HUB=1 takes the first external hub (testing in QEMU)
		bool anyHub = getenv("AC100USB_ANY_HUB") != NULL;
		if (device->InitCheck() == B_OK
			&& ((device->VendorID() == kHubVendor
					&& device->ProductID() == kHubProduct)
				|| (anyHub && device->Class() == 9
					&& device->VendorID() != 0))) {
			hub = device;
		} else
			delete device;
	}
	closedir(dir);
	return hub;
}


static bool
port_status(BUSBDevice* hub, int port, uint16* status, uint16* change)
{
	uint16 data[2];
	ssize_t length = hub->ControlTransfer(kFromPort, kGetStatus, 0, port,
		sizeof(data), data);
	if (length != (ssize_t)sizeof(data))
		return false;
	*status = B_LENDIAN_TO_HOST_INT16(data[0]);
	*change = B_LENDIAN_TO_HOST_INT16(data[1]);
	return true;
}


static const char*
speed_of(uint16 status)
{
	if ((status & 0x0001) == 0)
		return "nothing connected";
	if ((status & 0x0200) != 0)
		return "LOW speed";
	if ((status & 0x0400) != 0)
		return "HIGH speed";
	return "FULL speed";
}


static void
print_port(BUSBDevice* hub, int port)
{
	uint16 status, change;
	if (!port_status(hub, port, &status, &change)) {
		out("  port %d: status not readable\n", port);
		return;
	}
	out("  port %d: status %#06x change %#06x: %s%s%s%s%s%s%s%s\n", port,
		status, change, speed_of(status),
		(status & 0x0002) ? ", enabled" : ", disabled",
		(status & 0x0004) ? ", suspended" : "",
		(status & 0x0008) ? ", OVERCURRENT" : "",
		(status & 0x0010) ? ", in reset" : "",
		(status & 0x0100) ? ", powered" : ", NOT powered",
		(status & 0x0800) ? ", test mode" : "",
		(status & 0x1000) ? ", indicator" : "");
}


static void
print_status(BUSBDevice* hub)
{
	uint8 descriptor[16];
	ssize_t length = hub->ControlTransfer(kFromHub, kGetDescriptor, 0x2900, 0,
		sizeof(descriptor), descriptor);
	int ports = 4;
	if (length >= 7) {
		ports = descriptor[2];
		uint16 characteristics = descriptor[3] | (descriptor[4] << 8);
		out("hub %04x:%04x, %d ports, characteristics %#06x (power %s, "
			"overcurrent %s%s), power on to good %u ms, removable mask %#04x\n",
			hub->VendorID(), hub->ProductID(), ports, characteristics,
			(characteristics & 3) == 1 ? "per port"
				: (characteristics & 3) == 0 ? "ganged" : "not switched",
			((characteristics >> 3) & 3) == 1 ? "per port" : "global",
			(characteristics & 4) ? ", compound device" : "",
			descriptor[5] * 2, length > 7 ? descriptor[7] : 0);
	}
	for (int port = 1; port <= ports; port++)
		print_port(hub, port);
}


static void
clear_changes(BUSBDevice* hub, int port)
{
	static const uint16 kChanges[] = { kCPortConnection, kCPortEnable,
		kCPortSuspend, kCPortReset };
	for (size_t i = 0; i < sizeof(kChanges) / sizeof(kChanges[0]); i++)
		hub->ControlTransfer(kToPort, kClearFeature, kChanges[i], port, 0, NULL);
}


//! Resets the port and returns the status it settles in.
static uint16
reset_port(BUSBDevice* hub, int port, int settle)
{
	clear_changes(hub, port);
	bigtime_t start = system_time();
	if (hub->ControlTransfer(kToPort, kSetFeature, kPortReset, port, 0, NULL)
			< 0) {
		out("    reset request failed\n");
		return 0;
	}

	uint16 status = 0, change = 0;
	for (int i = 0; i < 100; i++) {
		snooze(5000);
		if (port_status(hub, port, &status, &change)
			&& (change & 0x0010) != 0) {
			break;
		}
	}
	bigtime_t took = system_time() - start;
	hub->ControlTransfer(kToPort, kClearFeature, kCPortReset, port, 0, NULL);
	if (settle > 0) {
		snooze(settle * 1000LL);
		port_status(hub, port, &status, &change);
	}
	out("    reset done in %lld ms: status %#06x, %s%s\n", took / 1000, status,
		speed_of(status), (status & 0x0002) ? ", enabled" : ", NOT enabled");
	return status;
}


static void
power_port(BUSBDevice* hub, int port, bool on)
{
	hub->ControlTransfer(kToPort, on ? kSetFeature : kClearFeature, kPortPower,
		port, 0, NULL);
}


static void
guided_test(BUSBDevice* hub)
{
	sReport = fopen(kReport, "w");
	out("ac100usb: test della porta della webcam (porta 2)\n"
		"=================================================\n\n");
	print_status(hub);

	int highSpeed = 0, fullSpeed = 0;

	out("\n1. Dieci reset di seguito, senza attesa:\n");
	for (int i = 0; i < 10; i++) {
		uint16 status = reset_port(hub, 2, 0);
		if (status & 0x0400)
			highSpeed++;
		else if (status & 0x0001)
			fullSpeed++;
	}

	out("\n2. Reset con attese crescenti dopo il reset (10, 50, 200, 1000 ms):\n");
	static const int kSettle[] = { 10, 50, 200, 1000 };
	for (size_t i = 0; i < sizeof(kSettle) / sizeof(kSettle[0]); i++) {
		out("  attesa %d ms\n", kSettle[i]);
		uint16 status = reset_port(hub, 2, kSettle[i]);
		if (status & 0x0400)
			highSpeed++;
		else if (status & 0x0001)
			fullSpeed++;
	}

	out("\n3. Porta spenta 3 s, riaccesa, poi reset:\n");
	power_port(hub, 2, false);
	snooze(3000000);
	print_port(hub, 2);
	power_port(hub, 2, true);
	snooze(500000);
	print_port(hub, 2);
	uint16 status = reset_port(hub, 2, 100);
	if (status & 0x0400)
		highSpeed++;
	else if (status & 0x0001)
		fullSpeed++;

	out("\n4. Sospensione e ripresa, poi reset:\n");
	hub->ControlTransfer(kToPort, kSetFeature, kPortSuspend, 2, 0, NULL);
	snooze(500000);
	print_port(hub, 2);
	hub->ControlTransfer(kToPort, kClearFeature, kPortSuspend, 2, 0, NULL);
	snooze(100000);
	status = reset_port(hub, 2, 100);
	if (status & 0x0400)
		highSpeed++;
	else if (status & 0x0001)
		fullSpeed++;

	out("\n5. Per confronto, la porta esterna (3):\n");
	print_port(hub, 3);

	out("\nRiepilogo: la webcam si e' presentata in high speed %d volte, in "
		"full speed %d volte\n", highSpeed, fullSpeed);
	out("Rapporto scritto in %s\n", kReport);
	fclose(sReport);
	sReport = NULL;
}


static void
usage()
{
	printf("usage: ac100usb status\n"
		"       ac100usb reset PORT [times] [settle ms]\n"
		"       ac100usb power PORT off|on|cycle [ms off]\n"
		"       ac100usb suspend PORT\n"
		"       ac100usb test\n");
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		usage();
		return 1;
	}

	BUSBDevice* hub = find_hub("/dev/bus/usb");
	if (hub == NULL) {
		fprintf(stderr, "internal hub %04x:%04x not found under /dev/bus/usb\n",
			kHubVendor, kHubProduct);
		return 1;
	}

	const char* command = argv[1];
	if (strcmp(command, "status") == 0)
		print_status(hub);
	else if (strcmp(command, "reset") == 0 && argc >= 3) {
		int port = atoi(argv[2]);
		int times = argc >= 4 ? atoi(argv[3]) : 1;
		int settle = argc >= 5 ? atoi(argv[4]) : 0;
		for (int i = 0; i < times; i++)
			reset_port(hub, port, settle);
	} else if (strcmp(command, "power") == 0 && argc >= 4) {
		int port = atoi(argv[2]);
		if (strcmp(argv[3], "off") == 0)
			power_port(hub, port, false);
		else if (strcmp(argv[3], "on") == 0)
			power_port(hub, port, true);
		else {
			int ms = argc >= 5 ? atoi(argv[4]) : 2000;
			power_port(hub, port, false);
			snooze(ms * 1000LL);
			power_port(hub, port, true);
			snooze(500000);
			reset_port(hub, port, 100);
		}
		print_port(hub, port);
	} else if (strcmp(command, "suspend") == 0 && argc >= 3) {
		int port = atoi(argv[2]);
		hub->ControlTransfer(kToPort, kSetFeature, kPortSuspend, port, 0, NULL);
		snooze(500000);
		print_port(hub, port);
		hub->ControlTransfer(kToPort, kClearFeature, kPortSuspend, port, 0,
			NULL);
		snooze(100000);
		reset_port(hub, port, 100);
	} else if (strcmp(command, "test") == 0)
		guided_test(hub);
	else {
		usage();
		return 1;
	}

	delete hub;
	return 0;
}
