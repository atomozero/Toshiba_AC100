// pngtest: PNG -> Be bitmap stream -> PNG -> Be bitmap stream through the
// Translation Kit, kept in memory (no BBitmap, so no app_server needed).
#include <ByteOrder.h>
#include <DataIO.h>
#include <File.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>
#include <stdio.h>
#include <string.h>

static status_t
to_bitmap(BTranslatorRoster* roster, BPositionIO* in, BMallocIO* out,
	const char* what)
{
	status_t status = roster->Translate(in, NULL, NULL, out,
		B_TRANSLATOR_BITMAP);
	printf("PNGTEST %s -> bitmap: %s, %ld bytes\n", what, strerror(status),
		(long)out->BufferLength());
	if (status != B_OK || out->BufferLength() < sizeof(TranslatorBitmap))
		return B_ERROR;

	TranslatorBitmap header;
	memcpy((void*)&header, out->Buffer(), sizeof(header));
	BRect bounds(B_BENDIAN_TO_HOST_FLOAT(header.bounds.left),
		B_BENDIAN_TO_HOST_FLOAT(header.bounds.top),
		B_BENDIAN_TO_HOST_FLOAT(header.bounds.right),
		B_BENDIAN_TO_HOST_FLOAT(header.bounds.bottom));
	uint32 rowBytes = B_BENDIAN_TO_HOST_INT32(header.rowBytes);
	const uint8* bits = (const uint8*)out->Buffer() + sizeof(header);
	const uint8* p = bits + 20 * rowBytes + 20 * 4;
	printf("PNGTEST   %gx%g, pixel 0 (BGR) %u %u %u, pixel (20,20) %u %u %u\n",
		bounds.Width() + 1, bounds.Height() + 1, bits[0], bits[1], bits[2],
		p[0], p[1], p[2]);
	return B_OK;
}

int
main(int argc, char** argv)
{
	const char* input = argc > 1 ? argv[1] : "/boot/home/t.png";
	const char* output = argc > 2 ? argv[2] : "/boot/home/t2.png";
	BTranslatorRoster* roster = BTranslatorRoster::Default();

	BFile file(input, B_READ_ONLY);
	BMallocIO bitmap;
	if (file.InitCheck() != B_OK || to_bitmap(roster, &file, &bitmap, input)
			!= B_OK)
		return 1;

	bitmap.Seek(0, SEEK_SET);
	BFile png(output, B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = roster->Translate(&bitmap, NULL, NULL, &png,
		B_PNG_FORMAT);
	off_t size = 0;
	png.GetSize(&size);
	printf("PNGTEST bitmap -> PNG %s: %s, %lld bytes\n", output,
		strerror(status), (long long)size);
	if (status != B_OK)
		return 1;

	png.Seek(0, SEEK_SET);
	BMallocIO again;
	return to_bitmap(roster, &png, &again, output) == B_OK ? 0 : 1;
}
