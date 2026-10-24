/*
 * Copyright 2026, Andrea Bernardi
 * SPDX-License-Identifier: MIT
 *
 * ARM (AC100) build glue: libcurl verifies TLS peers but this libcurl has no
 * CA bundle baked in, and the minimal images may lack ca_root_certificates.
 * CurlHttpClient honours CURL_CA_BUNDLE, so point it - before main() runs -
 * at the system bundle if there is one, else at the copy the package ships
 * in data/RTelevision (or one next to the executable). An explicit
 * CURL_CA_BUNDLE from the user always wins.
 */

#include <image.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>


namespace {

std::string
ExecutableDir()
{
	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		if (info.type != B_APP_IMAGE)
			continue;
		const char* slash = strrchr(info.name, '/');
		if (slash == NULL)
			return ".";
		return std::string(info.name, slash - info.name);
	}
	return ".";
}


__attribute__((constructor)) void
SetUpCaBundle()
{
	const char* current = getenv("CURL_CA_BUNDLE");
	if (current != NULL && current[0] != '\0')
		return;

	const std::string candidates[] = {
		"/boot/system/data/ssl/CARootCertificates.pem",
		"/boot/system/data/ssl/cert.pem",
		"/boot/system/data/RTelevision/CARootCertificates.pem",
		ExecutableDir() + "/CARootCertificates.pem",
	};
	for (const std::string& path : candidates) {
		if (access(path.c_str(), R_OK) == 0) {
			setenv("CURL_CA_BUNDLE", path.c_str(), 1);
			return;
		}
	}
}

}	// namespace
