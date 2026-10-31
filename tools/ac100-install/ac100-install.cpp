/*
 * Copyright 2026, Andrea Bernardi
 * SPDX-License-Identifier: MIT
 *
 * ac100-install: installs Haiku on the internal eMMC of the Toshiba AC100,
 * from a Haiku booted from the microSD card.
 *
 * The NVIDIA Tegra 2 boots from the start of the eMMC (BCT, partition table,
 * bootloader), followed by Android's recovery and boot images and its
 * partition table. Nothing is ever written in those first 16 MB: Haiku
 * replaces the rest of Android, from 16 MB to the end, described by a GPT
 * that only exists as the backup table in the last sectors of the disk
 * (Haiku and U-Boot both read it when the primary table is missing).
 *
 *   ac100-install                 checks the eMMC and shows the plan
 *   ac100-install partition       phase 1: writes the backup GPT
 *   ac100-install install         phase 2, after a restart: formats the
 *                                 partitions, copies the system and the home
 *                                 folder, sets up the EFI partition
 *
 * Every step checks the previous one and stops at the first surprise. Both
 * writing phases ask to type a confirmation, and compare a checksum of the
 * protected area before and after.
 */


#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Drivers.h>
#include <fs_info.h>
#include <OS.h>

#include <string>
#include <vector>


static const uint32 kSectorSize = 512;
static const off_t kMB = 1024 * 1024;
static const off_t kProtectedSize = 16 * kMB;
	// never written, and checked before and after every phase
static const off_t kEfiSize = 32 * kMB;

// the stock Android layout of the AC100 eMMC
static const off_t kAndroidMbr = 14 * kMB + kMB / 2;
static const off_t kAndroidApp = 15 * kMB + kMB / 2;
static const off_t kAndroidCache = 315 * kMB + kMB / 2;
static const off_t kAndroidExtended = 717 * kMB + kMB / 2;

static const uint32 kEntryCount = 128;
static const uint32 kEntrySize = 128;
static const uint32 kEntrySectors = kEntryCount * kEntrySize / kSectorSize;

static const char* kTargetMount = "/AC100-Haiku";
static const char* kEfiMount = "/AC100-EFI";
static const char* kSourceEfiMount = "/AC100-SD-EFI";
static const char* kStateFile
	= "/boot/home/config/settings/ac100-install";
static const char* kLogFile = "/boot/home/ac100-install.log";

static FILE* sLog;
static bool sTest;
	// AC100_INSTALL_TEST=1, for QEMU: the disk may be removable and the
	// system may run from another kind of disk


// #pragma mark - output


static void
say(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	fflush(stdout);
	if (sLog != NULL) {
		va_start(args, format);
		vfprintf(sLog, format, args);
		va_end(args);
		fflush(sLog);
	}
}


static int
fail(const char* format, ...)
{
	char text[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	say("\nSTOP: %s\nNothing else was written. Log: %s\n", text, kLogFile);
	return 1;
}


static int
run(const char* format, ...)
{
	char command[2048];
	va_list args;
	va_start(args, format);
	vsnprintf(command, sizeof(command), format, args);
	va_end(args);
	say("  $ %s\n", command);
	int status = system(command);
	if (status != 0)
		say("    -> exit status %d\n", status);
	return status;
}


static bool
confirm(const char* word)
{
	say("\nType %s to go on, anything else stops: ", word);
	char line[64];
	if (fgets(line, sizeof(line), stdin) == NULL)
		return false;
	line[strcspn(line, "\r\n")] = '\0';
	if (sLog != NULL)
		fprintf(sLog, "%s\n", line);
	return strcmp(line, word) == 0;
}


// #pragma mark - disk access


struct Disk {
	std::string	path;		// raw device
	std::string	directory;	// /dev/disk/mmc/N
	int			fd;
	off_t		size;
	uint64		sectors;
};


static bool
read_at(int fd, off_t offset, void* buffer, size_t size)
{
	return read_pos(fd, offset, buffer, size) == (ssize_t)size;
}


/*!	The only function that writes to the eMMC. It refuses anything in the
	protected area, whoever computed the offset.
*/
static bool
write_at(const Disk& disk, off_t offset, const void* buffer, size_t size)
{
	if (offset < kProtectedSize || offset + (off_t)size > disk.size) {
		say("refused: write of %zu bytes at %lld is outside the Haiku area\n",
			size, (long long)offset);
		return false;
	}
	return write_pos(disk.fd, offset, buffer, size) == (ssize_t)size;
}


static uint16
le16(const uint8* data)
{
	return data[0] | (data[1] << 8);
}


static uint32
le32(const uint8* data)
{
	return le16(data) | ((uint32)le16(data + 2) << 16);
}


static void
put32(uint8* data, uint32 value)
{
	for (int i = 0; i < 4; i++)
		data[i] = value >> (8 * i);
}


static void
put64(uint8* data, uint64 value)
{
	for (int i = 0; i < 8; i++)
		data[i] = value >> (8 * i);
}


static uint32
crc32(const uint8* data, size_t size)
{
	uint32 crc = 0xffffffff;
	for (size_t i = 0; i < size; i++) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xedb88320 & -(crc & 1));
	}
	return ~crc;
}


/*!	A checksum of the protected area, to show it did not change. */
static bool
protected_checksum(int fd, uint32* _checksum)
{
	const size_t kChunk = 1024 * 1024;
	uint8* buffer = (uint8*)malloc(kChunk);
	if (buffer == NULL)
		return false;
	uint32 crc = 0xffffffff;
	for (off_t offset = 0; offset < kProtectedSize; offset += kChunk) {
		if (!read_at(fd, offset, buffer, kChunk)) {
			free(buffer);
			return false;
		}
		for (size_t i = 0; i < kChunk; i++) {
			crc ^= buffer[i];
			for (int bit = 0; bit < 8; bit++)
				crc = (crc >> 1) ^ (0xedb88320 & -(crc & 1));
		}
		if ((offset / kChunk) % 128 == 127)
			say("    %lld MB\n", (long long)((offset + kChunk) / kMB));
	}
	free(buffer);
	*_checksum = ~crc;
	return true;
}


// #pragma mark - finding the eMMC


static std::string
boot_device()
{
	fs_info info;
	if (fs_stat_dev(dev_for_path("/boot"), &info) != 0)
		return "";
	return info.device_name;
}


/*!	The eMMC is the MMC disk that is not the boot disk and has the size of
	the AC100's 16 GB eMMC.
*/
static bool
find_emmc(Disk& disk, std::string& error)
{
	std::string boot = boot_device();
	say("Boot partition: %s\n", boot.c_str());
	if (boot.compare(0, 14, "/dev/disk/mmc/") != 0 && !sTest) {
		error = "Haiku must run from the microSD card (/dev/disk/mmc/...)";
		return false;
	}

	DIR* dir = opendir("/dev/disk/mmc");
	if (dir == NULL) {
		error = "no /dev/disk/mmc";
		return false;
	}
	int found = 0;
	while (dirent* entry = readdir(dir)) {
		if (entry->d_name[0] == '.')
			continue;
		std::string directory = std::string("/dev/disk/mmc/") + entry->d_name;
		std::string raw = directory + "/raw";
		if (boot.compare(0, directory.size() + 1, directory + "/") == 0) {
			say("  %s: the boot disk, left alone\n", raw.c_str());
			continue;
		}
		int fd = open(raw.c_str(), O_RDONLY);
		if (fd < 0)
			continue;
		device_geometry geometry;
		if (ioctl(fd, B_GET_GEOMETRY, &geometry, sizeof(geometry)) != 0) {
			close(fd);
			continue;
		}
		off_t size = (off_t)geometry.bytes_per_sector
			* geometry.sectors_per_track * geometry.cylinder_count
			* geometry.head_count;
		close(fd);
		say("  %s: %lld MB, %s\n", raw.c_str(), (long long)(size / kMB),
			geometry.removable ? "removable" : "fixed");
		if (geometry.bytes_per_sector != kSectorSize || size < 14000 * kMB
			|| size > 17000 * kMB || (geometry.removable && !sTest)) {
			continue;
		}
		disk.path = raw;
		disk.directory = directory;
		disk.size = size;
		disk.sectors = size / kSectorSize;
		found++;
	}
	closedir(dir);

	if (found != 1) {
		error = found == 0 ? "no 16 GB eMMC found"
			: "more than one disk looks like the eMMC";
		return false;
	}
	return true;
}


/*!	Checks the eMMC holds what the stock AC100 has there: Android's partition
	table at 14.5 MB, its ext file systems at 15.5 and 315.5 MB, the extended
	partition at 717.5 MB.
*/
static bool
check_android_layout(int fd, std::string& error)
{
	uint8 sector[kSectorSize];
	if (!read_at(fd, kAndroidMbr, sector, kSectorSize)
		|| sector[510] != 0x55 || sector[511] != 0xaa) {
		error = "no Android partition table at 14.5 MB: this is not the "
			"stock AC100 layout";
		return false;
	}
	static const struct {
		off_t		offset;
		const char*	name;
	} kExt[] = {
		{ kAndroidApp, "Android system (APP) at 15.5 MB" },
		{ kAndroidCache, "Android cache (CAC) at 315.5 MB" },
	};
	for (size_t i = 0; i < sizeof(kExt) / sizeof(kExt[0]); i++) {
		uint8 superblock[kSectorSize];
		if (!read_at(fd, kExt[i].offset + 1024, superblock, kSectorSize)
			|| le16(superblock + 56) != 0xef53) {
			error = std::string("no ext file system: ") + kExt[i].name;
			return false;
		}
		say("  found %s\n", kExt[i].name);
	}
	if (!read_at(fd, kAndroidExtended, sector, kSectorSize)
		|| sector[510] != 0x55 || sector[511] != 0xaa) {
		error = "no Android extended partition at 717.5 MB";
		return false;
	}
	say("  found the Android data area (extended partition) at 717.5 MB\n");
	return true;
}


// #pragma mark - the GPT


struct Layout {
	uint64	lastSector;
	uint64	entriesSector;
	uint64	firstUsable;
	uint64	lastUsable;
	uint64	efiStart;
	uint64	efiEnd;		// inclusive
	uint64	haikuStart;
	uint64	haikuEnd;	// inclusive
};


static Layout
compute_layout(const Disk& disk)
{
	Layout layout;
	layout.lastSector = disk.sectors - 1;
	layout.entriesSector = layout.lastSector - kEntrySectors;
	layout.firstUsable = kProtectedSize / kSectorSize;
	layout.lastUsable = layout.entriesSector - 1;
	layout.efiStart = layout.firstUsable;
	layout.efiEnd = layout.efiStart + kEfiSize / kSectorSize - 1;
	layout.haikuStart = layout.efiEnd + 1;
	// end on a MB boundary
	uint64 perMB = kMB / kSectorSize;
	layout.haikuEnd = (layout.lastUsable + 1) / perMB * perMB - 1;
	return layout;
}


static void
put_guid(uint8* data, const char* text)
{
	// "C12A7328-F81F-11D2-BA4B-00A0C93EC93B": the first three groups are
	// stored little endian
	unsigned int b[16];
	sscanf(text, "%2x%2x%2x%2x-%2x%2x-%2x%2x-%2x%2x-%2x%2x%2x%2x%2x%2x",
		&b[3], &b[2], &b[1], &b[0], &b[5], &b[4], &b[7], &b[6], &b[8], &b[9],
		&b[10], &b[11], &b[12], &b[13], &b[14], &b[15]);
	for (int i = 0; i < 16; i++)
		data[i] = b[i];
}


static void
put_name(uint8* data, const char* name)
{
	for (int i = 0; name[i] != '\0' && i < 36; i++) {
		data[i * 2] = name[i];
		data[i * 2 + 1] = 0;
	}
}


static void
random_guid(uint8* data)
{
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0 || read(fd, data, 16) != 16) {
		for (int i = 0; i < 16; i++)
			data[i] = (uint8)(system_time() >> (i % 8 * 8)) ^ (i * 37);
	}
	if (fd >= 0)
		close(fd);
	data[7] = (data[7] & 0x0f) | 0x40;	// version 4
	data[8] = (data[8] & 0x3f) | 0x80;
}


/*!	Builds the entries and the backup header. */
static void
build_gpt(const Layout& layout, uint8* entries, uint8* header)
{
	memset(entries, 0, kEntryCount * kEntrySize);

	uint8* efi = entries;
	put_guid(efi, "C12A7328-F81F-11D2-BA4B-00A0C93EC93B");
	random_guid(efi + 16);
	put64(efi + 32, layout.efiStart);
	put64(efi + 40, layout.efiEnd);
	put64(efi + 48, 1ULL << 2);
		// legacy BIOS bootable: U-Boot looks for the EFI loader there
	put_name(efi + 56, "AC100 EFI");

	uint8* haiku = entries + kEntrySize;
	put_guid(haiku, "42465331-3BA3-10F1-802A-4861696B7521");
	random_guid(haiku + 16);
	put64(haiku + 32, layout.haikuStart);
	put64(haiku + 40, layout.haikuEnd);
	put_name(haiku + 56, "Haiku");

	memset(header, 0, kSectorSize);
	memcpy(header, "EFI PART", 8);
	put32(header + 8, 0x00010000);
	put32(header + 12, 92);
	put64(header + 24, layout.lastSector);	// this header
	put64(header + 32, 1);					// where the primary would be
	put64(header + 40, layout.firstUsable);
	put64(header + 48, layout.lastUsable);
	random_guid(header + 56);
	put64(header + 72, layout.entriesSector);
	put32(header + 80, kEntryCount);
	put32(header + 84, kEntrySize);
	put32(header + 88, crc32(entries, kEntryCount * kEntrySize));
	put32(header + 16, crc32(header, 92));
}


/*!	Reads the backup GPT back and checks it describes our two partitions. */
static bool
check_gpt(const Disk& disk, const Layout& layout, std::string& error)
{
	uint8 header[kSectorSize];
	if (!read_at(disk.fd, layout.lastSector * kSectorSize, header,
			kSectorSize) || memcmp(header, "EFI PART", 8) != 0) {
		error = "no backup GPT header in the last sector";
		return false;
	}
	uint32 headerCRC = le32(header + 16);
	put32(header + 16, 0);
	if (crc32(header, 92) != headerCRC) {
		error = "the backup GPT header has a wrong checksum";
		return false;
	}
	std::vector<uint8> entries(kEntryCount * kEntrySize);
	if (!read_at(disk.fd, layout.entriesSector * kSectorSize, entries.data(),
			entries.size())
		|| crc32(entries.data(), entries.size()) != le32(header + 88)) {
		error = "the backup GPT entries have a wrong checksum";
		return false;
	}
	uint64 efiStart = le32(&entries[32]) | ((uint64)le32(&entries[36]) << 32);
	uint64 haikuStart = le32(&entries[kEntrySize + 32])
		| ((uint64)le32(&entries[kEntrySize + 36]) << 32);
	if (efiStart != layout.efiStart || haikuStart != layout.haikuStart) {
		error = "the backup GPT does not hold the expected partitions";
		return false;
	}
	return true;
}


// #pragma mark - state between the phases


static bool
save_state(const char* phase, uint32 checksum)
{
	FILE* file = fopen(kStateFile, "w");
	if (file == NULL)
		return false;
	fprintf(file, "phase %s\nprotected_crc %08lx\n", phase,
		(unsigned long)checksum);
	fflush(file);
	fsync(fileno(file));
	fclose(file);
	sync();
	return true;
}


static bool
load_state(std::string& phase, uint32& checksum)
{
	FILE* file = fopen(kStateFile, "r");
	if (file == NULL)
		return false;
	char name[64];
	unsigned long crc = 0;
	bool ok = fscanf(file, "phase %63s protected_crc %lx", name, &crc) == 2;
	fclose(file);
	phase = name;
	checksum = crc;
	return ok;
}


// #pragma mark - phases


static void
show_plan(const Disk& disk, const Layout& layout)
{
	say("\nPlan for %s (%llu sectors of 512 bytes):\n", disk.path.c_str(),
		(unsigned long long)disk.sectors);
	say("  0 - 16 MB           BCT, bootloader, recovery, boot: never written\n");
	say("  %llu - %llu MB   EFI partition (FAT, U-Boot reads the loader "
		"from it)\n", (unsigned long long)(layout.efiStart * 512 / kMB),
		(unsigned long long)((layout.efiEnd + 1) * 512 / kMB));
	say("  %llu - %llu MB  Haiku (BFS)\n",
		(unsigned long long)(layout.haikuStart * 512 / kMB),
		(unsigned long long)((layout.haikuEnd + 1) * 512 / kMB));
	say("  last 33 sectors     backup GPT describing the two partitions\n");
	say("Android (system, cache and user data) is overwritten; the bootloader "
		"stays.\n");
}


static int
phase_partition(Disk& disk, const Layout& layout, bool dryRun)
{
	show_plan(disk, layout);
	if (dryRun) {
		say("\nNothing written. \"ac100-install partition\" writes the "
			"backup GPT.\n");
		return 0;
	}

	say("\nBefore going on, the whole eMMC must be backed up on the PC "
		"(U-Boot \"ums\", see docs/AC100_BOOT.md).\n");
	if (!confirm("ERASE-ANDROID-DATA"))
		return fail("not confirmed");

	close(disk.fd);
	disk.fd = open(disk.path.c_str(), O_RDWR);
	if (disk.fd < 0)
		return fail("cannot open %s for writing: %s", disk.path.c_str(),
			strerror(errno));

	say("\nChecksum of the protected first 16 MB:\n");
	uint32 before;
	if (!protected_checksum(disk.fd, &before))
		return fail("cannot read the protected area");
	say("  %08lx\n", (unsigned long)before);

	uint8 entries[kEntryCount * kEntrySize];
	uint8 header[kSectorSize];
	build_gpt(layout, entries, header);

	say("Writing the backup GPT entries at sector %llu and its header at "
		"sector %llu\n", (unsigned long long)layout.entriesSector,
		(unsigned long long)layout.lastSector);
	if (!write_at(disk, layout.entriesSector * kSectorSize, entries,
			sizeof(entries))
		|| !write_at(disk, layout.lastSector * kSectorSize, header,
			sizeof(header))) {
		return fail("writing the GPT failed: %s", strerror(errno));
	}
	fsync(disk.fd);

	std::string error;
	if (!check_gpt(disk, layout, error))
		return fail("%s", error.c_str());
	say("  read back: correct\n");

	uint32 after;
	if (!protected_checksum(disk.fd, &after) || after != before)
		return fail("THE PROTECTED AREA CHANGED (%08lx -> %08lx)",
			(unsigned long)before, (unsigned long)after);
	say("  protected area unchanged (%08lx)\n", (unsigned long)after);

	if (!save_state("partitioned", before))
		return fail("cannot save %s", kStateFile);

	say("\nPhase 1 done. Shut down, start Haiku from the microSD again, "
		"then run \"ac100-install install\".\n");
	return 0;
}


static bool
partition_offset(const std::string& path, off_t* _offset, off_t* _size)
{
	int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return false;
	partition_info info;
	bool ok = ioctl(fd, B_GET_PARTITION_INFO, &info, sizeof(info)) == 0;
	close(fd);
	if (ok) {
		*_offset = info.offset;
		*_size = info.size;
	}
	return ok;
}


static bool
find_sd_efi(std::string& path)
{
	std::string boot = boot_device();
	size_t slash = boot.rfind('/');
	if (slash == std::string::npos)
		return false;
	path = boot.substr(0, slash) + "/0";
	return access(path.c_str(), F_OK) == 0;
}


static int
phase_install(Disk& disk, const Layout& layout)
{
	// The backup GPT is what tells phase 1 was done; the state file only
	// adds the checksum taken before it wrote anything.
	std::string error;
	if (!check_gpt(disk, layout, error)) {
		return fail("%s: run \"ac100-install partition\" and restart first",
			error.c_str());
	}

	say("\nChecksum of the protected first 16 MB:\n");
	uint32 savedChecksum;
	if (!protected_checksum(disk.fd, &savedChecksum))
		return fail("cannot read the protected area");
	std::string phase;
	uint32 phase1Checksum;
	if (load_state(phase, phase1Checksum)) {
		if (phase1Checksum != savedChecksum) {
			return fail("THE PROTECTED AREA CHANGED since phase 1 (%08lx -> "
				"%08lx)", (unsigned long)phase1Checksum,
				(unsigned long)savedChecksum);
		}
		say("  %08lx, as after phase 1\n", (unsigned long)savedChecksum);
	} else
		say("  %08lx (no record of phase 1 found)\n",
			(unsigned long)savedChecksum);

	std::string efiDevice = disk.directory + "/0";
	std::string haikuDevice = disk.directory + "/1";
	off_t offset, size;
	if (!partition_offset(efiDevice, &offset, &size)
		|| offset != (off_t)(layout.efiStart * kSectorSize)) {
		return fail("Haiku does not see the EFI partition at the right place "
			"(%s); was the machine restarted?", efiDevice.c_str());
	}
	if (!partition_offset(haikuDevice, &offset, &size)
		|| offset != (off_t)(layout.haikuStart * kSectorSize)) {
		return fail("Haiku does not see the BFS partition at the right place "
			"(%s)", haikuDevice.c_str());
	}
	say("  %s and %s are where they should be\n", efiDevice.c_str(),
		haikuDevice.c_str());

	std::string sourceEfi;
	if (!find_sd_efi(sourceEfi))
		return fail("cannot find the EFI partition of the microSD card");

	say("\nThis formats %s and %s, then copies the system and the home "
		"folder.\n", efiDevice.c_str(), haikuDevice.c_str());
	if (!confirm("INSTALL"))
		return fail("not confirmed");

	if (run("mkfs -q -t fat -o 'fat 16' %s 'AC100 EFI'", efiDevice.c_str())
			!= 0
		|| run("mkfs -q -t bfs %s Haiku", haikuDevice.c_str()) != 0) {
		return fail("formatting failed");
	}

	mkdir(kTargetMount, 0755);
	mkdir(kEfiMount, 0755);
	mkdir(kSourceEfiMount, 0755);
	if (run("mount %s %s", haikuDevice.c_str(), kTargetMount) != 0
		|| run("mount %s %s", efiDevice.c_str(), kEfiMount) != 0
		|| run("mount -ro %s %s", sourceEfi.c_str(), kSourceEfiMount) != 0) {
		return fail("mounting failed");
	}

	say("\nCopying (this takes a while):\n");
	int failed = 0;
	// The layout of a fresh image: /boot/system and ~/config are packagefs
	// views, only their writable parts are copied, the rest comes from the
	// packages. The package state is left out, so that the new system does
	// its first boot processing like a fresh image.
	failed |= run("mkdir -p %s/system/cache/package-repositories "
		"%s/system/cache/tmp %s/system/var/empty %s/system/var/log",
		kTargetMount, kTargetMount, kTargetMount, kTargetMount);
	failed |= run("copyattr -d -r /boot/system/packages %s/system/",
		kTargetMount);
	failed |= run("rm -rf %s/system/packages/administrative", kTargetMount);
	failed |= run("copyattr -d -r /boot/system/settings %s/system/",
		kTargetMount);
	failed |= run("copyattr -d -r /boot/system/non-packaged %s/system/",
		kTargetMount);
	failed |= run("mkdir -p %s/home/config/cache %s/home/config/var",
		kTargetMount, kTargetMount);
	failed |= run("cd /boot/home && for f in * .[!.]*; do "
		"[ \"$f\" = config ] || [ ! -e \"$f\" ] || "
		"copyattr -d -r \"$f\" %s/home/ || exit 1; done", kTargetMount);
	failed |= run("for d in settings packages non-packaged; do "
		"copyattr -d -r /boot/home/config/$d %s/home/config/ || exit 1; done",
		kTargetMount);
	failed |= run("rm -rf %s/home/config/packages/administrative "
		"%s/home/config/settings/ac100-install", kTargetMount, kTargetMount);
	failed |= run("cp -r %s/EFI %s/", kSourceEfiMount, kEfiMount);
	failed |= run("for f in %s/*.dtb %s/*.scr; do [ -e \"$f\" ] && cp \"$f\" "
		"%s/; done; true", kSourceEfiMount, kSourceEfiMount, kEfiMount);
	// the kernel writes its log to a preallocated file on the EFI partition
	failed |= run("dd if=/dev/zero bs=1024 count=256 2>/dev/null | tr '\\0' "
		"' ' > %s/AC100LOG.TXT", kEfiMount);
	if (failed != 0)
		return fail("copying failed");

	run("sync");
	run("unmount %s", kSourceEfiMount);
	run("unmount %s", kEfiMount);
	run("unmount %s", kTargetMount);

	say("\nChecksum of the protected first 16 MB:\n");
	uint32 after;
	if (!protected_checksum(disk.fd, &after) || after != savedChecksum)
		return fail("THE PROTECTED AREA CHANGED (%08lx -> %08lx)",
			(unsigned long)savedChecksum, (unsigned long)after);
	say("  unchanged (%08lx)\n", (unsigned long)after);

	save_state("installed", after);
	say("\nHaiku is installed on the eMMC. To start it from there, remove "
		"the microSD card before loading U-Boot from the PC.\n");
	return 0;
}


int
main(int argc, char** argv)
{
	const char* command = argc > 1 ? argv[1] : "check";
	if (strcmp(command, "check") != 0 && strcmp(command, "partition") != 0
		&& strcmp(command, "install") != 0) {
		fprintf(stderr, "usage: %s [check | partition | install]\n", argv[0]);
		return 2;
	}

	sTest = getenv("AC100_INSTALL_TEST") != NULL
		&& strcmp(getenv("AC100_INSTALL_TEST"), "1") == 0;
	sLog = fopen(kLogFile, "a");
	if (sTest)
		say("TEST MODE (AC100_INSTALL_TEST=1)\n");
	say("ac100-install %s\n", command);

	Disk disk;
	std::string error;
	if (!find_emmc(disk, error))
		return fail("%s", error.c_str());
	disk.fd = open(disk.path.c_str(), O_RDONLY);
	if (disk.fd < 0)
		return fail("cannot open %s: %s", disk.path.c_str(), strerror(errno));
	say("eMMC: %s\n", disk.path.c_str());

	if (!check_android_layout(disk.fd, error))
		return fail("%s", error.c_str());

	Layout layout = compute_layout(disk);

	if (strcmp(command, "install") == 0)
		return phase_install(disk, layout);

	if (check_gpt(disk, layout, error)) {
		say("\nThe backup GPT is already there.\n");
		if (strcmp(command, "partition") == 0)
			return 0;
	}
	return phase_partition(disk, layout, strcmp(command, "check") == 0);
}
