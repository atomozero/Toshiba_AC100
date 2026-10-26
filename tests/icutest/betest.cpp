/*
 * Copyright 2026, Andrea Bernardi
 * SPDX-License-Identifier: MIT
 *
 * Same check through the Haiku kits: BTextEncoding (libbe), then the
 * translation roster on a file, as StyledEdit does.
 * Usage: betest file
 */
#include <stdio.h>
#include <string.h>

#include <File.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>
#include <TextEncoding.h>


int
main(int argc, char** argv)
{
	if (argc < 2)
		return 1;
	char data[4096];
	FILE* file = fopen(argv[1], "rb");
	size_t length = file != NULL ? fread(data, 1, sizeof(data), file) : 0;
	if (file != NULL)
		fclose(file);

	BPrivate::BTextEncoding encoding(data, length);
	printf("BTextEncoding: \"%s\", InitCheck %s\n", encoding.GetName().String(),
		strerror(encoding.InitCheck()));

	BFile input(argv[1], B_READ_ONLY);
	translator_info info;
	status_t status = BTranslatorRoster::Default()->Identify(&input, NULL,
		&info, 0, NULL, B_STYLED_TEXT_FORMAT);
	printf("Identify for STXT: %s (translator %ld, type %.4s, %s)\n",
		strerror(status), (long)info.translator, (char*)&info.type,
		status == B_OK ? info.name : "-");
	return 0;
}
