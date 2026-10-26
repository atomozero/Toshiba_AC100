// jpegtest: JPEG -> Be bitmap stream -> JPEG through the JPEG translator,
// kept in memory (no BBitmap, so no app_server needed). The translator can be
// given as a path, to test it before its package is active.
// usage: jpegtest in.jpg out.jpg [JPEGTranslator]
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
	printf("JPEGTEST %s -> bitmap: %s, %ld bytes\n", what, strerror(status),
		(long)out->BufferLength());
	if (status != B_OK || out->BufferLength() < sizeof(TranslatorBitmap))
		return B_ERROR;

	TranslatorBitmap header;
	memcpy((void*)&header, out->Buffer(), sizeof(header));
	uint32 rowBytes = B_BENDIAN_TO_HOST_INT32(header.rowBytes);
	const uint8* bits = (const uint8*)out->Buffer() + sizeof(header);
	const uint8* p = bits + 20 * rowBytes + 20 * 4;
	printf("JPEGTEST   %gx%g, pixel 0 (BGR) %u %u %u, pixel (20,20) %u %u %u\n",
		B_BENDIAN_TO_HOST_FLOAT(header.bounds.right) + 1,
		B_BENDIAN_TO_HOST_FLOAT(header.bounds.bottom) + 1,
		bits[0], bits[1], bits[2], p[0], p[1], p[2]);
	return B_OK;
}

int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s in.jpg out.jpg [translator]\n", argv[0]);
		return 1;
	}
	BTranslatorRoster* roster;
	if (argc > 3) {
		roster = new BTranslatorRoster;
		status_t status = roster->AddTranslators(argv[3]);
		printf("JPEGTEST add %s: %s\n", argv[3], strerror(status));
	} else
		roster = BTranslatorRoster::Default();

	BFile file(argv[1], B_READ_ONLY);
	BMallocIO bitmap;
	if (file.InitCheck() != B_OK
		|| to_bitmap(roster, &file, &bitmap, argv[1]) != B_OK)
		return 1;

	bitmap.Seek(0, SEEK_SET);
	BFile jpeg(argv[2], B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = roster->Translate(&bitmap, NULL, NULL, &jpeg,
		B_JPEG_FORMAT);
	off_t size = 0;
	jpeg.GetSize(&size);
	printf("JPEGTEST bitmap -> JPEG %s: %s, %lld bytes\n", argv[2],
		strerror(status), (long long)size);
	if (status != B_OK)
		return 1;

	jpeg.Seek(0, SEEK_SET);
	BMallocIO again;
	return to_bitmap(roster, &jpeg, &again, argv[2]) == B_OK ? 0 : 1;
}
