/*
 * mixerparam [name|#index [value]] — lists the continuous parameters of the
 * "Toshiba AC100 audio" node (the Media preferences' controls), or sets the
 * one whose name contains NAME, or the INDEXth one (as listed, from 0), to
 * VALUE (for every channel). The media
 * server saves the change like one made in the preferences.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <MediaRoster.h>
#include <ParameterWeb.h>


int
main(int argc, char** argv)
{
	BApplication app("application/x-vnd.ac100-mixerparam");
	BMediaRoster* roster = BMediaRoster::Roster();

	live_node_info nodes[32];
	int32 count = 32;
	if (roster->GetLiveNodes(nodes, &count) != B_OK)
		return 1;
	media_node node;
	bool found = false;
	for (int32 i = 0; i < count; i++) {
		if (strstr(nodes[i].name, "AC100 audio") != NULL) {
			node = nodes[i].node;
			found = true;
			break;
		}
	}
	if (!found) {
		printf("no \"AC100 audio\" node\n");
		return 1;
	}

	BParameterWeb* web;
	if (roster->GetParameterWebFor(node, &web) != B_OK) {
		printf("no parameters\n");
		return 1;
	}
	int32 index = -1;
	for (int32 i = 0; i < web->CountParameters(); i++) {
		BParameter* parameter = web->ParameterAt(i);
		if (parameter->Type() != BParameter::B_CONTINUOUS_PARAMETER)
			continue;
		BContinuousParameter* continuous = (BContinuousParameter*)parameter;
		index++;
		float values[8];
		size_t size = sizeof(values);
		bigtime_t when;
		continuous->GetValue(values, &size, &when);
		printf("#%" B_PRId32 " %-24s %6.1f %s (%.1f - %.1f)\n", index,
			parameter->Name(), values[0],
			continuous->Unit(), continuous->MinValue(),
			continuous->MaxValue());
		bool wanted = argc > 2 && (argv[1][0] == '#'
			? atoi(argv[1] + 1) == index
			: strstr(parameter->Name(), argv[1]) != NULL);
		if (wanted) {
			float value = atof(argv[2]);
			for (int32 c = 0; c < 8; c++)
				values[c] = value;
			status_t status = continuous->SetValue(values,
				sizeof(float) * continuous->CountChannels(), 0);
			printf("  -> %.1f: %s\n", value, strerror(status));
		}
	}
	delete web;
	return 0;
}
