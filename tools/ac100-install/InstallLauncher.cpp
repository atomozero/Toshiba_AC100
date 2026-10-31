/*
 * Copyright 2026, Andrea Bernardi
 * SPDX-License-Identifier: MIT
 *
 * "Install to internal memory": opens a Terminal with the guided installation
 * of RenkuOS on the internal eMMC of the Toshiba AC100.
 */


#include <Alert.h>
#include <Application.h>
#include <Roster.h>


int
main()
{
	BApplication app("application/x-vnd.atomozero-AC100-InstallLauncher");

	const char* args[] = { "-t", "Install RenkuOS on the internal memory",
		"/bin/sh", "/boot/system/bin/ac100-install-wizard", NULL };
	status_t status = be_roster->Launch("application/x-vnd.Haiku-Terminal",
		4, args);
	if (status != B_OK && status != B_ALREADY_RUNNING) {
		BAlert* alert = new BAlert("Install",
			"Could not open the Terminal for the installation.", "OK");
		alert->Go();
	}
	return 0;
}
