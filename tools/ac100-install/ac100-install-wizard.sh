#!/bin/sh
# Copyright 2026, Andrea Bernardi
# SPDX-License-Identifier: MIT
#
# Guides through the installation of RenkuOS on the internal eMMC of the
# Toshiba AC100, with ac100-install, from the system started on the microSD
# card. The installation has two phases with a restart in between; this
# script finds out which one is next.

state=/boot/home/config/settings/ac100-install

pause()
{
	printf '\nPress Enter to close this window. '
	read answer
	exit $1
}

cat <<'EOF'
Install RenkuOS on the internal memory
======================================

This replaces Android (or whatever is installed there) on the internal
eMMC of this AC100 with the system running now from the microSD card,
including your home folder. The first 16 MB of the eMMC, with the NVIDIA
boot data and the original bootloader, are never touched.

EOF

phase=
[ -f "$state" ] && phase=$(sed -n 's/^phase //p' "$state")

case "$phase" in
installed)
	echo "RenkuOS has already been installed on the internal memory."
	echo "To install it again, delete $state first."
	pause 0
	;;
partitioned)
	echo "Phase 2 of 2: copying the system to the internal memory."
	echo
	ac100-install install || pause 1
	echo
	echo "Done. Switch the AC100 off, remove the microSD card and start it"
	echo "again: U-Boot then starts RenkuOS from the internal memory."
	pause 0
	;;
esac

echo "Phase 1 of 2: checking the internal memory."
echo
ac100-install check || pause 1
printf '\nContinue with the partitioning? [y/N] '
read answer
case "$answer" in
	y|Y|yes|YES) ;;
	*) echo "Nothing was changed."; pause 0 ;;
esac
ac100-install partition || pause 1
cat <<'EOF'

Phase 1 is done. Restart the AC100 from the microSD card now (Haiku must
read the new partition table), then open "Install to internal memory"
again for phase 2.
EOF
pause 0
