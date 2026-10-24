#!/bin/bash
# Avvia U-Boot sull'AC100 via USB (modalità APX/RCM), senza scrivere nulla sull'eMMC.
#
# Uso:
#   ac100-rcm.sh readbct            legge il BCT dell'AC100 in ac100.bct (solo lettura)
#   ac100-rcm.sh boot [u-boot.bin]  carica U-Boot in RAM e lo avvia
#
# Prima: AC100 spento, cavo mini-USB collegato, tenere premuti Ctrl+Esc e premere
# il tasto di accensione. Lo schermo resta nero e il PC vede il dispositivo
# USB 0955:7820 "APX". In WSL va prima inoltrato con usbipd-win.
set -e

HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=$HERE/out
BCT=$OUT/ac100.bct
TEGRARCM=${TEGRARCM:-$OUT/tegrarcm}
export LD_LIBRARY_PATH=$OUT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

apx_present()
{
	local dev
	for dev in /sys/bus/usb/devices/*; do
		[ "$(cat "$dev/idVendor" 2>/dev/null)" = 0955 ] \
			&& [ "$(cat "$dev/idProduct" 2>/dev/null)" = 7820 ] && return 0
	done
	return 1
}

if ! apx_present; then
	echo "AC100 in modalità APX (0955:7820) non trovato." >&2
	echo "Controlla cavo, combinazione Ctrl+Esc+accensione e inoltro USB (usbipd)." >&2
	exit 1
fi

case "$1" in
	readbct)
		if [ -e "$BCT" ]; then
			echo "$BCT esiste già, non lo sovrascrivo." >&2
			exit 1
		fi
		"$TEGRARCM" --bct="$BCT" readbct
		echo "BCT salvato in $BCT ($(stat -c %s "$BCT") byte). Conservane una copia."
		;;
	boot)
		UBOOT=${2:-$OUT/uboot-ums/u-boot-dtb-tegra.bin}
		[ -e "$BCT" ] || { echo "Manca $BCT: esegui prima 'readbct'." >&2; exit 1; }
		"$TEGRARCM" --bct="$BCT" --bootloader="$UBOOT" --loadaddr=0x108000
		;;
	*)
		sed -n '2,10p' "$0"
		exit 1
		;;
esac
