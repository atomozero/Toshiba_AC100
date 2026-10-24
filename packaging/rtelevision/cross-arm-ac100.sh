#!/bin/bash
# Copyright 2026, Andrea Bernardi
# SPDX-License-Identifier: MIT
#
# Cross-compile the RenkuOS front end for 32-bit ARM (Toshiba AC100: Tegra 2,
# Cortex-A9, VFPv3-D16, no NEON) into dist/arm/, and stage an hpkg tree.
#
# Media backend: FFmpeg, linked against the system ffmpeg package (6.1.x,
# built for VFPv3-D16, with openssl for https) instead of bundling one.
# HTTP backend: libcurl (system curl package, with openssl). The upstream
# packages use RenkuOS network services instead, but the ARM image builds
# libbnetapi without SSL, and the playlist is only served over https.
# CaBundleGlue.cpp points libcurl at a CA bundle.
#
#   bash platforms/renkuos/cross-arm-ac100.sh
#
# Uses the apps-extra cross environment (~/haiku-arm/apps-extra/scripts/env.sh).
set -eu

. "$HOME/haiku-arm/apps-extra/scripts/env.sh"

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
H="$SYSROOT/boot/system/develop/headers"
OUT=dist/arm
WORK=$OUT/obj
rm -rf "$OUT"; mkdir -p "$WORK"

CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-psabi $ARCHFLAGS
	-Ishared -Iplatforms/renkuos"

SRC="shared/core/StringUtil.cpp shared/core/Aes128.cpp shared/core/Country.cpp
shared/core/Strings.cpp shared/core/M3UParser.cpp shared/core/Paths.cpp
shared/core/PlaylistStore.cpp shared/core/ChannelIndex.cpp shared/core/Favorites.cpp
shared/core/MediaPlayer.cpp shared/core/HlsRelay.cpp shared/core/RelayedMediaPlayer.cpp
shared/core/AppController.cpp shared/core/FFmpegMediaPlayer.cpp shared/core/CurlHttpClient.cpp
platforms/renkuos/main.cpp platforms/renkuos/MainWindow.cpp platforms/renkuos/VideoView.cpp
platforms/renkuos/Icons.cpp platforms/renkuos/AudioOutput.cpp platforms/renkuos/ResolverGuard.cpp
platforms/renkuos/SocketGuard.cpp platforms/renkuos/CaBundleGlue.cpp"

for f in $SRC; do
	echo "  CXX $f"
	$CXX $CXXFLAGS -c "$f" -o "$WORK/$(basename "$f" .cpp).o"
done

echo "  LD  $OUT/RTelevision"
$CXX $ARCHFLAGS "$WORK"/*.o -o "$OUT/RTelevision" \
	-lbe -lmedia -lnetwork -lcurl \
	-lavformat -lavcodec -lswscale -lswresample -lavutil

# headless tool: null media backend
CLI_SRC="shared/core/StringUtil.cpp shared/core/Aes128.cpp shared/core/Country.cpp
shared/core/Strings.cpp shared/core/M3UParser.cpp shared/core/Paths.cpp
shared/core/PlaylistStore.cpp shared/core/ChannelIndex.cpp shared/core/Favorites.cpp
shared/core/MediaPlayer.cpp shared/core/HlsRelay.cpp shared/core/RelayedMediaPlayer.cpp
shared/core/AppController.cpp shared/core/NullMediaPlayer.cpp shared/core/CurlHttpClient.cpp
shared/tools/cli_main.cpp platforms/renkuos/CaBundleGlue.cpp"
mkdir -p "$WORK/cli"
for f in $CLI_SRC; do
	o="$WORK/cli/$(basename "$f" .cpp).o"
	if [ -f "$WORK/$(basename "$f" .cpp).o" ]; then
		cp "$WORK/$(basename "$f" .cpp).o" "$o"
	else
		echo "  CXX $f"
		$CXX $CXXFLAGS -c "$f" -o "$o"
	fi
done
echo "  LD  $OUT/rtv-cli"
$CXX $ARCHFLAGS "$WORK"/cli/*.o -o "$OUT/rtv-cli" \
	-lbe -lnetwork -lcurl

$STRIP "$OUT/RTelevision" "$OUT/rtv-cli"

# resources: signature, version, vector icon
(cd platforms/renkuos && rc -o "$ROOT/$WORK/RTelevision.rsrc" RTelevision.rdef)
xres -o "$OUT/RTelevision" "$WORK/RTelevision.rsrc"
rm -rf "$WORK"
echo "built: $OUT/RTelevision $OUT/rtv-cli"
