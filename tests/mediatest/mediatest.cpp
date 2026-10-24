// Opens media files with the Media Kit and decodes all of their tracks.
#include <stdio.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <Entry.h>
#include <OS.h>

int main(int argc, char** argv)
{
	for (int a = 1; a < argc; a++) {
		entry_ref ref;
		get_ref_for_path(argv[a], &ref);
		BMediaFile file(&ref);
		printf("MEDIATEST %s: %s, %ld tracks\n", argv[a], strerror(file.InitCheck()),
			file.CountTracks());
		if (file.InitCheck() != B_OK)
			continue;
		for (int32 t = 0; t < file.CountTracks(); t++) {
			BMediaTrack* track = file.TrackAt(t);
			media_format format;
			track->EncodedFormat(&format);
			char name[256];
			string_for_format(format, name, sizeof(name));
			media_format decoded;
			decoded.type = format.type;
			status_t status = track->DecodedFormat(&decoded);
			static char buffer[4 * 1024 * 1024];
			int64 frames = 0, count;
			media_header header;
			bigtime_t start = system_time();
			while (status == B_OK
				&& track->ReadFrames(buffer, &count, &header) == B_OK)
				frames += count;
			printf("MEDIATEST   track %ld: %s -> %s, %lld frames decoded in %lld ms\n",
				t, name, strerror(status), frames, (system_time() - start) / 1000);
			file.ReleaseTrack(track);
		}
	}
	return 0;
}
