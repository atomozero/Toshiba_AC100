/*
 * mictest [seconds] — records from the system's audio input (the media
 * server's default input) through BMediaRecorder, prints the peak and the
 * average level of every half second, and saves /boot/home/mictest.wav.
 * Speak, clap or tap near the microphone while it runs.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <File.h>
#include <MediaRecorder.h>
#include <MediaRoster.h>
#include <TimeSource.h>
#include <OS.h>


static BFile* sFile;
static uint32 sBytes;
static media_raw_audio_format sFormat;
static double sSum;
static double sPeak;
static uint64 sSamples;
static uint64 sTotalSamples;
static uint32 sBuffers;


static void
record(void* cookie, bigtime_t timestamp, void* data, size_t size,
	const media_format& format)
{
	sFormat = format.u.raw_audio;
	sBuffers++;
	uint32 type = sFormat.format;
	size_t count = 0;
	for (size_t i = 0;; i++) {
		double value;
		if (type == media_raw_audio_format::B_AUDIO_SHORT) {
			if ((i + 1) * 2 > size)
				break;
			value = ((int16*)data)[i] / 32768.0;
		} else if (type == media_raw_audio_format::B_AUDIO_FLOAT) {
			if ((i + 1) * 4 > size)
				break;
			value = ((float*)data)[i];
		} else if (type == media_raw_audio_format::B_AUDIO_INT) {
			if ((i + 1) * 4 > size)
				break;
			value = ((int32*)data)[i] / 2147483648.0;
		} else
			break;
		sSum += value * value;
		if (fabs(value) > sPeak)
			sPeak = fabs(value);
		count++;
	}
	sSamples += count;
	sTotalSamples += count;
	if (sFile != NULL) {
		sFile->Write(data, size);
		sBytes += size;
	}
}


static double
to_db(double value)
{
	return value > 0 ? 20 * log10(value) : -200;
}


static void
write_wav_header(uint32 dataBytes)
{
	uint32 rate = (uint32)sFormat.frame_rate;
	uint16 channels = sFormat.channel_count;
	uint16 bits = (sFormat.format & 0xf) * 8;
	uint16 formatTag = sFormat.format == media_raw_audio_format::B_AUDIO_FLOAT
		? 3 : 1;
	uint8 header[44];
	memcpy(header, "RIFF", 4);
	uint32 value = 36 + dataBytes;
	memcpy(header + 4, &value, 4);
	memcpy(header + 8, "WAVEfmt ", 8);
	value = 16;
	memcpy(header + 16, &value, 4);
	memcpy(header + 20, &formatTag, 2);
	memcpy(header + 22, &channels, 2);
	memcpy(header + 24, &rate, 4);
	value = rate * channels * bits / 8;
	memcpy(header + 28, &value, 4);
	uint16 align = channels * bits / 8;
	memcpy(header + 32, &align, 2);
	memcpy(header + 34, &bits, 2);
	memcpy(header + 36, "data", 4);
	memcpy(header + 40, &dataBytes, 4);
	sFile->WriteAt(0, header, sizeof(header));
}


int
main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 6;
	BApplication app("application/x-vnd.ac100-mictest");

	BMediaRoster* roster = BMediaRoster::Roster();
	media_node input;
	if (roster->GetAudioInput(&input) == B_OK) {
		live_node_info info;
		roster->GetLiveNodeInfo(input, &info);
		printf("default audio input: node %" B_PRId32 " \"%s\", kinds %#"
			B_PRIx64 "\n", input.node, info.name, (uint64)input.kind);
		media_output outputs[8];
		int32 count = 0;
		roster->GetAllOutputsFor(input, outputs, 8, &count);
		for (int32 i = 0; i < count; i++) {
			printf("  output %" B_PRId32 " \"%s\": %s, format type %" B_PRId32
				", %.0f Hz, %" B_PRIu32 " channels\n", i, outputs[i].name,
				outputs[i].destination == media_destination::null
					? "free" : "connected", (int32)outputs[i].format.type,
				outputs[i].format.u.raw_audio.frame_rate,
				outputs[i].format.u.raw_audio.channel_count);
		}
	} else
		printf("no default audio input\n");

	BMediaRecorder recorder("mictest", B_MEDIA_RAW_AUDIO);
	if (recorder.InitCheck() != B_OK) {
		printf("BMediaRecorder: %s\n", strerror(recorder.InitCheck()));
		return 1;
	}
	recorder.SetHooks(record, NULL, NULL);

	// Connect(format) would take the mixer's output (what plays): connect to
	// the free output of the audio input node instead
	media_format format;
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = media_raw_audio_format::wildcard;
	media_output freeOutput;
	int32 freeCount = 0;
	status_t status = roster->GetFreeOutputsFor(input, &freeOutput, 1,
		&freeCount, B_MEDIA_RAW_AUDIO);
	if (status == B_OK && freeCount == 1)
		status = recorder.Connect(input, &freeOutput, &format);
	else if (status == B_OK)
		status = B_MEDIA_ALREADY_CONNECTED;
	if (status != B_OK) {
		printf("cannot connect to the audio input: %s\n", strerror(status));
		return 1;
	}
	const media_raw_audio_format& raw = recorder.Format().u.raw_audio;
	printf("input: %.0f Hz, %" B_PRIu32 " channels, format %#" B_PRIx32
		", buffer %zu bytes\n", raw.frame_rate, raw.channel_count, raw.format,
		raw.buffer_size);

	BFile file("/boot/home/mictest.wav", B_WRITE_ONLY | B_CREATE_FILE
		| B_ERASE_FILE);
	if (file.InitCheck() == B_OK) {
		uint8 empty[44] = {};
		file.Write(empty, sizeof(empty));
		sFile = &file;
	}

	// the input node only produces while it runs: when the mixer plays
	// through another output, nobody else starts it
	status = roster->StartNode(input, BTimeSource::RealTime());
	if (status != B_OK)
		printf("cannot start the input node: %s\n", strerror(status));

	status = recorder.Start();
	if (status != B_OK) {
		printf("cannot start: %s\n", strerror(status));
		return 1;
	}
	for (int i = 0; i < seconds * 2; i++) {
		snooze(500000);
		double rms = sSamples > 0 ? sqrt(sSum / sSamples) : 0;
		printf("%4.1f s: %3" B_PRIu32 " buffers, peak %6.1f dBFS, average "
			"%6.1f dBFS\n", (i + 1) / 2.0, sBuffers, to_db(sPeak), to_db(rms));
		fflush(stdout);
		sSum = 0;
		sPeak = 0;
		sSamples = 0;
		sBuffers = 0;
	}
	recorder.Stop();
	recorder.Disconnect();

	if (sFile != NULL) {
		write_wav_header(sBytes);
		printf("%" B_PRIu64 " samples, saved /boot/home/mictest.wav\n",
			sTotalSamples);
	}
	return 0;
}
