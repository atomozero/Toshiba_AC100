/*
 * Copyright 2026, Andrea Bernardi
 * SPDX-License-Identifier: MIT
 *
 * Checks ICU's charset detection (what BTextEncoding and so StyledEdit's
 * text translator use) and data loading: prints every step's error.
 * Usage: icutest [file]
 */
#include <stdio.h>
#include <string.h>

#include <unicode/ucnv.h>
#include <unicode/ucsdet.h>
#include <unicode/uclean.h>
#include <unicode/uversion.h>


int
main(int argc, char** argv)
{
	char data[4096] = "mkdir -p /boot/system/settings/ssh\ncp a b\n";
	size_t length = strlen(data);
	if (argc > 1) {
		FILE* file = fopen(argv[1], "rb");
		if (file != NULL) {
			length = fread(data, 1, sizeof(data), file);
			fclose(file);
		}
	}

	UVersionInfo version;
	u_getVersion(version);
	printf("ICU %d.%d\n", version[0], version[1]);

	UErrorCode error = U_ZERO_ERROR;
	u_init(&error);
	printf("u_init: %s\n", u_errorName(error));

	error = U_ZERO_ERROR;
	UConverter* converter = ucnv_open("ISO-8859-1", &error);
	printf("ucnv_open(ISO-8859-1): %s\n", u_errorName(error));
	if (converter != NULL)
		ucnv_close(converter);

	error = U_ZERO_ERROR;
	UCharsetDetector* detector = ucsdet_open(&error);
	printf("ucsdet_open: %s\n", u_errorName(error));
	ucsdet_setText(detector, data, length, &error);
	printf("ucsdet_setText (%zu bytes): %s\n", length, u_errorName(error));
	const UCharsetMatch* match = ucsdet_detect(detector, &error);
	printf("ucsdet_detect: %s, match %p\n", u_errorName(error), match);
	const char* name = ucsdet_getName(match, &error);
	printf("ucsdet_getName: %s -> \"%s\", confidence %d\n", u_errorName(error),
		name != NULL ? name : "(null)",
		match != NULL ? ucsdet_getConfidence(match, &error) : -1);
	ucsdet_close(detector);
	return 0;
}
