/*
 * ac100ec — talks to the embedded controller of the Toshiba AC100 (NVEC
 * protocol, Tegra Embedded Controller Interface Specification 1.0) through
 * /dev/misc/nvec_diag.
 *
 *   ac100ec info        read-only: product, firmware, capabilities, status
 *   ac100ec gpio        read-only: the level of the GPIOs the AP may use
 *   ac100ec raw BYTE... sends any request (asks before anything but reads)
 *
 * info and gpio also write /boot/home/ac100ec-report.txt.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <OS.h>


#define NVEC_DIAG_REQUEST	0x4e563130	// 'NV10'
#define NVEC_MSG_SIZE		34

struct nvec_diag_request {
	uint8	size;
	uint8	request[NVEC_MSG_SIZE - 1];
	uint8	replySize;
	uint8	reply[NVEC_MSG_SIZE];
};

static const char* kDevice = "/dev/misc/nvec_diag";
static const char* kReport = "/boot/home/ac100ec-report.txt";

static int sDevice = -1;
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


/*!	Sends a request; returns the number of payload bytes after the type,
	count, sub-command and status bytes, or -1.
*/
static int
request(const uint8* bytes, int size, uint8* payload, uint8* _status = NULL,
	bool quiet = false)
{
	nvec_diag_request message;
	memset(&message, 0, sizeof(message));
	message.size = size;
	memcpy(message.request, bytes, size);
	if (ioctl(sDevice, NVEC_DIAG_REQUEST, &message, sizeof(message)) < 0) {
		if (!quiet) {
			out("  request %02x %02x: %s\n", bytes[0], bytes[1],
				strerror(errno));
		}
		return -1;
	}

	if (!quiet) {
		out("  ->");
		for (int i = 0; i < size; i++)
			out(" %02x", bytes[i]);
		out("   <-");
		for (int i = 0; i < message.replySize; i++)
			out(" %02x", message.reply[i]);
		out("\n");
	}
	if (message.replySize < 4)
		return -1;
	if (_status != NULL)
		*_status = message.reply[3];
	int length = message.replySize - 4;
	if (payload != NULL)
		memcpy(payload, message.reply + 4, length);
	return length;
}


static int
system_request(uint8 subCommand, uint8* payload, const char* what)
{
	out("%s\n", what);
	uint8 bytes[] = { 0x07, subCommand };
	uint8 status = 0xff;
	int length = request(bytes, sizeof(bytes), payload, &status);
	if (length >= 0 && status != 0)
		out("  status %#04x (not successful)\n", status);
	return status == 0 ? length : -1;
}


static int
gpio_count()
{
	uint8 payload[NVEC_MSG_SIZE];
	uint8 bytes[] = { 0x07, 0x11 };
	uint8 status;
	if (request(bytes, sizeof(bytes), payload, &status, true) < 2
		|| status != 0) {
		return -1;
	}
	return payload[0];
}


static void
info()
{
	uint8 payload[NVEC_MSG_SIZE];
	int length;

	length = system_request(0x14, payload, "EC product name (07 14):");
	if (length > 0)
		out("  \"%.*s\"\n", length, (const char*)payload);

	length = system_request(0x02, payload, "EC firmware version (07 02):");
	if (length >= 4) {
		out("  %02x.%02x.%02x / %02x\n", payload[0], payload[1], payload[2],
			payload[3]);
	}

	length = system_request(0x10, payload,
		"EC interface spec version (07 10):");
	if (length >= 1)
		out("  %u.%u\n", payload[0] >> 4, payload[0] & 0xf);

	length = system_request(0x11, payload, "System capabilities (07 11):");
	if (length >= 5) {
		out("  GPIOs the AP may control: %u\n", payload[0]);
		out("  capabilities %#06x, OEM capabilities %#06x\n",
			payload[1] | (payload[2] << 8), payload[3] | (payload[4] << 8));
	}

	length = system_request(0x12, payload, "System configuration (07 12):");
	if (length >= 4) {
		out("  configuration %#06x, OEM configuration %#06x\n",
			payload[0] | (payload[1] << 8), payload[2] | (payload[3] << 8));
	}

	out("System status (01 00):\n");
	uint8 statusRequest[] = { 0x01, 0x00 };
	uint8 status;
	length = request(statusRequest, sizeof(statusRequest), payload, &status);
	if (length >= 4) {
		out("  system state %#06x, OEM state %#06x\n",
			payload[0] | (payload[1] << 8), payload[2] | (payload[3] << 8));
	}
}


static void
gpio()
{
	int count = gpio_count();
	out("GPIOs the AP may control: %d\n", count);
	if (count <= 0)
		return;

	out("Get Pin (vector) (03 22):\n");
	uint8 payload[NVEC_MSG_SIZE];
	uint8 bytes[] = { 0x03, 0x22 };
	uint8 status;
	int length = request(bytes, sizeof(bytes), payload, &status);
	if (length > 0 && status == 0) {
		for (int pin = 0; pin < count && pin / 8 < length; pin++) {
			out("  GPIO %2d: %u\n", pin, (payload[pin / 8] >> (pin % 8)) & 1);
		}
	}

	out("Get Pin (scalar) (03 02 n), one by one:\n");
	for (int pin = 0; pin < count; pin++) {
		uint8 scalar[] = { 0x03, 0x02, (uint8)pin };
		length = request(scalar, sizeof(scalar), payload, &status, true);
		out("  GPIO %2d: %s", pin, length < 0 ? "no reply" : "");
		if (length >= 0) {
			if (status != 0)
				out("status %#04x", status);
			else
				out("%u", length > 0 ? payload[0] : 0);
		}
		out("\n");
	}
}


static bool
is_read_only(const uint8* bytes, int size)
{
	if (size < 2)
		return false;
	switch (bytes[0]) {
		case 0x01:	// system: get status
			return bytes[1] == 0x00;
		case 0x03:	// GPIO: get pin, get event report
			return bytes[1] == 0x02 || bytes[1] == 0x22 || bytes[1] == 0x06
				|| bytes[1] == 0x26;
		case 0x07:	// control: version, capabilities, configuration, name
			return bytes[1] == 0x02 || bytes[1] == 0x10 || bytes[1] == 0x11
				|| bytes[1] == 0x12 || bytes[1] == 0x14;
	}
	return false;
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		printf("usage: ac100ec info | gpio | raw BYTE...\n");
		return 1;
	}

	sDevice = open(kDevice, O_RDWR);
	if (sDevice < 0) {
		fprintf(stderr, "cannot open %s: %s\n", kDevice, strerror(errno));
		return 1;
	}

	if (strcmp(argv[1], "info") == 0 || strcmp(argv[1], "gpio") == 0) {
		sReport = fopen(kReport, strcmp(argv[1], "info") == 0 ? "w" : "a");
		if (strcmp(argv[1], "info") == 0)
			info();
		else
			gpio();
		if (sReport != NULL) {
			fclose(sReport);
			printf("\nRapporto in %s\n", kReport);
		}
	} else if (strcmp(argv[1], "raw") == 0 && argc >= 4) {
		uint8 bytes[NVEC_MSG_SIZE - 1];
		int size = 0;
		for (int i = 2; i < argc && size < (int)sizeof(bytes); i++)
			bytes[size++] = strtoul(argv[i], NULL, 16);
		if (!is_read_only(bytes, size)) {
			printf("ATTENZIONE: questa richiesta puo' cambiare lo stato "
				"dell'EC, che resta anche dopo il riavvio. Continuare? [s/n] ");
			fflush(stdout);
			char line[16];
			if (fgets(line, sizeof(line), stdin) == NULL
				|| (line[0] != 's' && line[0] != 'S')) {
				return 1;
			}
		}
		uint8 payload[NVEC_MSG_SIZE];
		request(bytes, size, payload);
	} else {
		printf("usage: ac100ec info | gpio | raw BYTE...\n");
		return 1;
	}

	close(sDevice);
	return 0;
}
