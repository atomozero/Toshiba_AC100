#!/usr/bin/env python3
"""Genera i file di prova audio/video per l'AC100.

Uso: make-test-media.py <cartella di uscita>

- prova-audio.wav: 44,1 kHz, 16 bit, stereo. 2 s di La (440 Hz) solo a
  sinistra, 2 s di Mi (660 Hz) solo a destra, 2 s di tutti e due insieme.
- prova-video.avi: 320x240, 25 fotogrammi/s, 8 s, Motion JPEG con audio
  PCM. Barre di colore, una barra che scorre, il numero del fotogramma e
  un "bip" all'inizio di ogni secondo, quando il riquadro diventa bianco:
  se il suono arriva insieme al bianco, audio e video sono sincronizzati.
"""

import io
import math
import struct
import sys
import wave
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

RATE = 44100


def tone(frequency, seconds, left, right, volume=0.3):
	samples = bytearray()
	for i in range(int(RATE * seconds)):
		value = int(32767 * volume * math.sin(2 * math.pi * frequency * i / RATE))
		samples += struct.pack('<hh', value if left else 0, value if right else 0)
	return bytes(samples)


def write_wav(path):
	data = (tone(440, 2, True, False) + tone(660, 2, False, True)
		+ tone(440, 1, True, True) + tone(660, 1, True, True))
	with wave.open(str(path), 'wb') as out:
		out.setnchannels(2)
		out.setsampwidth(2)
		out.setframerate(RATE)
		out.writeframes(data)


def chunk(fourcc, data):
	padding = b'\0' if len(data) & 1 else b''
	return fourcc + struct.pack('<I', len(data)) + data + padding


def riff_list(kind, fourcc, data):
	return kind + struct.pack('<I', len(data) + 4) + fourcc + data


def video_frame(index, fps, width, height, font):
	image = Image.new('RGB', (width, height))
	draw = ImageDraw.Draw(image)
	colors = [(192, 192, 192), (192, 192, 0), (0, 192, 192), (0, 192, 0),
		(192, 0, 192), (192, 0, 0), (0, 0, 192)]
	barWidth = width // len(colors)
	for i, color in enumerate(colors):
		draw.rectangle([i * barWidth, 0, (i + 1) * barWidth, height * 2 // 3],
			fill=color)
	# the first 4 frames of every second: white, with the beep
	if index % fps < 4:
		draw.rectangle([0, 0, width, height * 2 // 3], fill=(255, 255, 255))
	x = (index * 4) % width
	draw.rectangle([x, height * 2 // 3, x + 20, height], fill=(255, 128, 0))
	draw.text((10, height * 2 // 3 + 10),
		'AC100  %d s  fotogramma %d' % (index // fps, index), fill=(255, 255, 255),
		font=font)
	buffer = io.BytesIO()
	image.save(buffer, 'JPEG', quality=80)
	return buffer.getvalue()


def write_avi(path, width=320, height=240, fps=25, seconds=8):
	frames = fps * seconds
	samplesPerFrame = RATE // fps
	font = ImageFont.load_default()

	movi = bytearray()
	index = bytearray()
	biggest = 0
	for i in range(frames):
		jpeg = video_frame(i, fps, width, height, font)
		biggest = max(biggest, len(jpeg))
		index += b'00dc' + struct.pack('<III', 0x10, len(movi) + 4, len(jpeg))
		movi += chunk(b'00dc', jpeg)

		# a 1 kHz beep during the white frames
		audio = bytearray()
		for s in range(samplesPerFrame):
			t = i * samplesPerFrame + s
			value = 0
			if i % fps < 4:
				value = int(9000 * math.sin(2 * math.pi * 1000 * t / RATE))
			audio += struct.pack('<hh', value, value)
		index += b'01wb' + struct.pack('<III', 0x10, len(movi) + 4, len(audio))
		movi += chunk(b'01wb', bytes(audio))

	microSeconds = 1000000 // fps
	avih = struct.pack('<IIIIIIIIIIIIII', microSeconds, biggest * fps, 0, 0x10,
		frames, 0, 2, biggest, width, height, 0, 0, 0, 0)
	videoHeader = struct.pack('<4s4sIHHIIIIIIIIhhhh', b'vids', b'MJPG', 0, 0, 0,
		0, 1, fps, 0, frames, biggest, 0xffffffff, 0, 0, 0, width, height)
	videoFormat = struct.pack('<IiiHH4sIiiII', 40, width, height, 1, 24, b'MJPG',
		width * height * 3, 0, 0, 0, 0)
	audioHeader = struct.pack('<4s4sIHHIIIIIIIIhhhh', b'auds', b'\0\0\0\0', 0, 0,
		0, 0, 4, RATE * 4, 0, frames * samplesPerFrame, samplesPerFrame * 4,
		0xffffffff, 4, 0, 0, 0, 0)
	audioFormat = struct.pack('<HHIIHH', 1, 2, RATE, RATE * 4, 4, 16)

	hdrl = riff_list(b'LIST', b'hdrl', chunk(b'avih', avih)
		+ riff_list(b'LIST', b'strl', chunk(b'strh', videoHeader)
			+ chunk(b'strf', videoFormat))
		+ riff_list(b'LIST', b'strl', chunk(b'strh', audioHeader)
			+ chunk(b'strf', audioFormat)))
	body = (hdrl + riff_list(b'LIST', b'movi', bytes(movi))
		+ chunk(b'idx1', bytes(index)))
	Path(path).write_bytes(riff_list(b'RIFF', b'AVI ', body))


def main():
	out = Path(sys.argv[1] if len(sys.argv) > 1 else '.')
	out.mkdir(parents=True, exist_ok=True)
	write_wav(out / 'prova-audio.wav')
	write_avi(out / 'prova-video.avi')


if __name__ == '__main__':
	main()
