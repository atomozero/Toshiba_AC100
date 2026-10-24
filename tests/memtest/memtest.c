/*
 * Checks memcpy() and memset() for all small sizes and alignments against a
 * byte by byte reference, then measures them. Results go to the kernel log,
 * prefixed "memtest:".
 */
#include <OS.h>
#include <stdlib.h>
#include <string.h>


static uint8 sSource[4096], sDest[4096], sExpected[4096];


static void
fill(uint8* buffer, size_t size, uint32 seed)
{
	for (size_t i = 0; i < size; i++) {
		seed = seed * 1664525 + 1013904223;
		buffer[i] = seed >> 24;
	}
}


int
main()
{
	uint32 errors = 0;
	for (size_t size = 0; size <= 300; size++) {
		for (int sa = 0; sa < 8; sa++) {
			for (int da = 0; da < 8; da++) {
				fill(sSource, 512, size * 64 + sa * 8 + da);
				fill(sDest, 512, ~size);
				memcpy(sExpected, sDest, 512);
				for (size_t i = 0; i < size; i++)
					sExpected[64 + da + i] = sSource[64 + sa + i];
				void* result = memcpy(sDest + 64 + da, sSource + 64 + sa, size);
				if (result != sDest + 64 + da
					|| memcmp(sDest, sExpected, 512) != 0) {
					if (errors++ < 5) {
						debug_printf("memtest: memcpy size %d, alignments %d/%d "
							"WRONG\n", (int)size, sa, da);
					}
				}
			}
			fill(sDest, 512, size + sa);
			memcpy(sExpected, sDest, 512);
			for (size_t i = 0; i < size; i++)
				sExpected[64 + sa + i] = 0xa5;
			if (memset(sDest + 64 + sa, 0xa5, size) != sDest + 64 + sa
				|| memcmp(sDest, sExpected, 512) != 0) {
				if (errors++ < 5) {
					debug_printf("memtest: memset size %d, alignment %d WRONG\n",
						(int)size, sa);
				}
			}
		}
	}
	debug_printf("memtest: correctness: %lu errors%s\n", (unsigned long)errors,
		errors != 0 ? "  <-- WRONG" : "  OK");

	// speed: 4 MB buffers (beyond the caches), aligned and misaligned
	const size_t kSize = 4 * 1024 * 1024;
	uint8* a = (uint8*)malloc(kSize + 64);
	uint8* b = (uint8*)malloc(kSize + 64);
	memset(a, 1, kSize + 64);
	memset(b, 2, kSize + 64);
	const struct { const char* name; int sa, da; } kRuns[] = {
		{ "memcpy aligned", 0, 0 }, { "memcpy misaligned", 1, 3 } };
	for (int r = 0; r < 2; r++) {
		bigtime_t start = system_time();
		for (int i = 0; i < 16; i++)
			memcpy(b + kRuns[r].da, a + kRuns[r].sa, kSize);
		bigtime_t time = system_time() - start;
		debug_printf("memtest: %s: %lld MB/s\n", kRuns[r].name,
			(long long)(16LL * 4 * 1000000 / (time > 0 ? time : 1)));
	}
	bigtime_t start = system_time();
	for (int i = 0; i < 16; i++)
		memset(b, i, kSize);
	bigtime_t time = system_time() - start;
	debug_printf("memtest: memset: %lld MB/s\n",
		(long long)(16LL * 4 * 1000000 / (time > 0 ? time : 1)));
	return errors != 0;
}
