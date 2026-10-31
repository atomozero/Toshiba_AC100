#!/bin/bash
# Copyright 2026, Andrea Bernardi
# SPDX-License-Identifier: MIT
#
# build-release-packages.sh <output folder>
#
# Builds the AC100 packages of the release image that have no recipe
# elsewhere:
#   ac100_install  the installer for the internal eMMC (tools/ac100-install),
#                  its guided wizard and the "Install to internal memory" app
#   salagiochi     SalaGiochi and "Add games", with the libretro cores
#
# Needs the cross tools environment of apps-extra (scripts/env.sh there), the
# SalaGiochi sources in $SALAGIOCHI and the built cores in $CORES.
set -e
OUTDIR=$(realpath "${1:?usage: $0 <output folder>}")
HERE=$(cd "$(dirname "$0")/.." && pwd)
. "$HOME/haiku-arm/apps-extra/scripts/env.sh"
SALAGIOCHI=${SALAGIOCHI:-$HOME/haiku-arm/salagiochi}
CORES=${CORES:?the folder with the *_libretro.so cores}
FIXATTR=$HERE/packaging/fix-attributes.sh
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
mkdir -p "$OUTDIR"

# make execs these by name; on Linux they cannot set attributes, which
# fix-attributes.sh does on the packages afterwards
mkdir -p "$W/hostbin"
printf '#!/bin/sh\nexit 0\n' > "$W/hostbin/mimeset"
printf '#!/bin/sh\nexit 0\n' > "$W/hostbin/addattr"
chmod +x "$W/hostbin/"*

# -------------------------------------------------------------- ac100_install
S=$HERE/tools/ac100-install
ST=$W/ac100_install
mkdir -p "$ST/bin" "$ST/apps" "$ST/data/deskbar/menu/Applications"
$CXX -O2 -Wall $ARCHFLAGS -s -o "$ST/bin/ac100-install" "$S/ac100-install.cpp"
cp "$S/ac100-install-wizard.sh" "$ST/bin/ac100-install-wizard"
chmod +x "$ST/bin/ac100-install-wizard"
APP="$ST/apps/Install to internal memory"
$CXX -O2 -Wall $ARCHFLAGS -s -o "$APP" "$S/InstallLauncher.cpp" -lbe
"$HAIKU_TOOLS/rc/rc" -o "$W/launcher.rsrc" "$S/InstallLauncher.rdef"
"$HAIKU_TOOLS/xres" -o "$APP" "$W/launcher.rsrc"
ln -s "../../../../apps/Install to internal memory" \
	"$ST/data/deskbar/menu/Applications/Install to internal memory"
cat > "$W/ac100_install.PackageInfo" <<EOF
name			ac100_install
version			1.0.0-1
architecture	arm
summary			"Installs RenkuOS on the internal memory of the Toshiba AC100"
description		"ac100-install copies the system started from the microSD card to the internal eMMC of the Toshiba AC100, in two phases with a restart in between, without touching the NVIDIA boot data and the original bootloader in the first 16 MB. \"Install to internal memory\" guides through it in a Terminal."
packager		"atomozero <bernardi.andrea@gmail.com>"
vendor			"atomozero"
copyrights		{
	"2026 Andrea Bernardi"
}
licenses		{
	"MIT"
}
urls			{
	"https://github.com/atomozero/Toshiba_AC100"
}
provides		{
	ac100_install = 1.0.0
	cmd:ac100_install = 1.0.0
	app:Install_to_internal_memory = 1.0.0
}
requires		{
	haiku
	cmd:bash
}
EOF
bash "$HOME/haiku-arm/apps-extra/scripts/mkpkg.sh" "$ST" "$W/ac100_install.PackageInfo" \
	"$OUTDIR/ac100_install-1.0.0-1-arm.hpkg"

# ----------------------------------------------------------------- salagiochi
B=$W/salagiochi-build
git clone -q "$SALAGIOCHI" "$B"
COMMIT=$(git -C "$B" rev-parse --short=12 HEAD)
(cd "$B" && PATH=$W/hostbin:$PATH make -j8 CXX="$CXX" ARCHFLAGS="$ARCHFLAGS" \
	RC="$HAIKU_TOOLS/rc/rc" XRES="$HAIKU_TOOLS/xres" \
	LINKCATKEYS="$HAIKU_TOOLS/locale/linkcatkeys" \
	COLLECTCATKEYS="$HAIKU_TOOLS/locale/collectcatkeys" > "$W/salagiochi.log" 2>&1) \
	|| { cat "$W/salagiochi.log"; exit 1; }
ST=$W/salagiochi
A=$ST/apps/SalaGiochi
mkdir -p "$A/cores" "$ST/data/deskbar/menu/Applications" \
	"$ST/data/locale/catalogs/x-vnd.atomozero-SalaGiochi" \
	"$ST/data/locale/catalogs/x-vnd.atomozero-SalaGiochi-Installer" \
	"$ST/documentation/packages/salagiochi"
cp "$B/objects/SalaGiochi" "$B/objects/AddGames" "$A/"
cp "$CORES"/*_libretro.so "$A/cores/"
cp "$B/objects/catalogs/"*.catalog \
	"$ST/data/locale/catalogs/x-vnd.atomozero-SalaGiochi/"
cp "$B/objects/catalogs-installer/"*.catalog \
	"$ST/data/locale/catalogs/x-vnd.atomozero-SalaGiochi-Installer/"
ln -s ../../../../apps/SalaGiochi/SalaGiochi \
	"$ST/data/deskbar/menu/Applications/SalaGiochi"
cp "$HERE/packaging/salagiochi/CORES.md" "$ST/documentation/packages/salagiochi/"
# the licenses that are not among Haiku's common ones
mkdir -p "$ST/data/licenses"
cp "$HERE/packaging/salagiochi/licenses/"* "$ST/data/licenses/"
cat > "$W/salagiochi.PackageInfo" <<EOF
name			salagiochi
version			1.0.0-1
architecture	arm
summary			"Games for children, with the gamepad or the keyboard"
description		"SalaGiochi shows the games in big pictures and starts them with the libretro cores it comes with: NES, Game Boy (Color, Advance), Mega Drive, Master System, Game Gear, Super Nintendo, arcade, Vectrex, PlayStation, Nintendo DS and TIC-80. No games are included: \"Add games\" puts the game files dropped on it in the right place. Built from commit $COMMIT."
packager		"atomozero <bernardi.andrea@gmail.com>"
vendor			"atomozero"
copyrights		{
	"2026 Andrea Bernardi"
}
licenses		{
	"MIT"
	"GNU GPL v2"
	"GNU GPL v3"
	"MAME"
	"Snes9x"
	"PicoDrive"
}
urls			{
	"https://github.com/atomozero/Toshiba_AC100"
}
provides		{
	salagiochi = 1.0.0
	app:SalaGiochi = 1.0.0
}
requires		{
	haiku
	lib:libstdc++
}
EOF
bash "$HOME/haiku-arm/apps-extra/scripts/mkpkg.sh" "$ST" "$W/salagiochi.PackageInfo" \
	"$OUTDIR/salagiochi-1.0.0-1-arm.hpkg"

bash "$FIXATTR" "$OUTDIR/ac100_install-1.0.0-1-arm.hpkg" \
	"$OUTDIR/salagiochi-1.0.0-1-arm.hpkg"
bash "$HOME/haiku-arm/apps-extra/scripts/check-d16.sh" -q "$OUTDIR"/*.hpkg \
	2>/dev/null || true
ls -l "$OUTDIR"
