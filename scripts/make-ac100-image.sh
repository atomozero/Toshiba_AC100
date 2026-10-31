#!/bin/bash
# Prepara un'immagine per la microSD dell'AC100 a partire da haiku-mmc.image.
# Uso: make-ac100-image.sh <immagine di uscita>
#
# - AC100LOG.TXT preallocato (256 KB) nella partizione EFI: il kernel ci scrive
#   il syslog direttamente (branch ac100-markers);
# - con DEBUG_SCREEN=1, output di debug a schermo forzato (nel loader non
#   c'e' una tastiera); di default resta la grafica di boot di Haiku;
# - pacchetti precompilati per VFPv3-D32 sostituiti da versioni D16: il Tegra 2
#   non ha i registri d16-d31 (libstdc++ li usa gia' nel costruttore statico).
#   Le versioni D16 vengono dal bootstrap build (~/haiku-arm/haiku-bs) e, per
#   zstd e ICU, da una ricompilazione con la toolchain D16 (ZSTD_HPKG, ICU_HPKG).
# - con RELEASE=<elenco> (release/packages.txt) l'immagine da distribuire: solo
#   i pacchetti dell'elenco e quelli che servono loro (scripts/release-packages.py,
#   cartelle in RELEASE_DIRS), niente file e programmi di prova, sulla
#   Scrivania SalaGiochi, "Add games" e "Install to internal memory".
set -e
OUT=${1:?uso: $0 <immagine di uscita>}
# G=~/haiku-arm/haiku-renku/generated.arm per l'immagine da RenkuOS
G=${G:-$HOME/haiku-arm/haiku/generated.arm}
R=$HOME/haiku-arm/haiku-bs/generated.arm/objects/haiku/arm/packaging/repositories/HaikuPortsCross-build/packages
ZSTD_HPKG=${ZSTD_HPKG:-$HOME/haiku-arm/out/zstd-1.5.5-1-arm-d16.hpkg}
# ICU 57.2 ricompilato D16 (~/haiku-arm/icu57), con la libicudata originale
ICU_HPKG=${ICU_HPKG:-$HOME/haiku-arm/out/icu-57.2-1-arm-d16.hpkg}
# gcc_syslibs del bootstrap con libstdc++ ricompilata: legge i type_info
# delle eccezioni C++ tramite la GOT, come li genera il linker (TARGET2)
SYSLIBS_HPKG=${SYSLIBS_HPKG:-$HOME/haiku-arm/out/gcc_bootstrap_syslibs-13.3.0_2026_03_29-1-arm-eh.hpkg}
ESP=4194304		# offset della partizione EFI
BFS=37748736	# offset della partizione BFS
T=$HOME/haiku-arm/tmp-image
mkdir -p "$T"

T_TOOLS=$G/objects/linux/x86_64/release/tools
export LD_LIBRARY_PATH=$G/objects/linux/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

# esegue comandi fs_shell sulla partizione BFS
bfs() {
	local fifo=$T/fifo-$$
	mkfifo $fifo-to $fifo-from
	sleep 3<$fifo-from 1 &
	exec 6>$fifo-from 3<$fifo-from
	sleep 5<$fifo-to 1 &
	exec 4>$fifo-to 5<$fifo-to
	rm -f $fifo-to $fifo-from
	$T_TOOLS/bfs_shell/bfs_shell 3>&5 4<&6 --start-offset $BFS -n "$OUT" &
	local pid=$!
	for c in "$@"; do
		eval "$T_TOOLS/fs_shell/fs_shell_command 3<&3 4>&4 5>&- 6>&- $c"
	done
	$T_TOOLS/fs_shell/fs_shell_command 3<&3 4>&4 5>&- 6>&- sync || true
	$T_TOOLS/fs_shell/fs_shell_command 3<&3 4>&4 5>&- 6>&- quit || true
	wait $pid 2>/dev/null || true
	exec 3<&- 4>&- 5<&- 6>&-
}

cp "$G/haiku-mmc.image" "$OUT"

# The build sometimes leaves BOOTARM.EFI empty (copying the BeOS version
# attribute of a rebuilt loader to the FAT partition fails): copy it again.
LOADER=$G/objects/haiku/arm/release/system/boot/efi/haiku_loader.efi
mcopy -o -i "$OUT@@$ESP" "$LOADER" ::EFI/BOOT/BOOTARM.EFI
size=$(mdir -i "$OUT@@$ESP" ::EFI/BOOT/BOOTARM.EFI | awk '/BOOTARM/ {print $3}')
[ "$size" = "$(stat -c %s "$LOADER")" ] \
	|| { echo "BOOTARM.EFI non copiato correttamente ($size byte)"; exit 1; }

python3 -c "
t = b'AC100: nessun log scritto, il kernel non e arrivato a scriverlo.\r\n'
open('$T/AC100LOG.TXT', 'wb').write(t + b' ' * (262144 - len(t)))"
mcopy -o -i "$OUT@@$ESP" "$T/AC100LOG.TXT" ::AC100LOG.TXT

rm -f "$T/kernel"
bfs "cp -f /myfs/home/config/settings/kernel/drivers/kernel :$T/kernel"
# DEBUG_SCREEN=1 mostra il log del kernel a schermo (resta comunque su
# AC100LOG.TXT)
# SMP=0 usa un solo core (se il secondo dovesse bloccare l'avvio: nel loader
# non c'e' una tastiera per il menu delle opzioni)
if [ "${SMP:-1}" = 0 ]; then
	printf '\n# AC100: one CPU only\ndisable_smp true\n' >> "$T/kernel"
fi
# PMIC=0 non tocca il chip di alimentazione TPS6586x (orologio)
if [ "${PMIC:-1}" = 0 ]; then
	printf '\n# AC100: leave the TPS6586x alone\ntps6586x_clock false\n' >> "$T/kernel"
fi
# L2=1 accende la cache L2 (sperimentale: sull'AC100 il sistema si e' bloccato)
if [ "${L2:-0}" = 1 ]; then
	printf '\n# AC100: L2 cache on (experimental)\narm_l2_cache true\n' >> "$T/kernel"
fi
if [ "${DEBUG_SCREEN:-0}" != 0 ]; then
	printf '\n# AC100 bring-up: no keyboard in the boot loader\ndebug_screen true\ndisable_onscreen_paging true\n' \
		>> "$T/kernel"
fi

P=/myfs/system/packages
cmds=("cp -f :$T/kernel /myfs/home/config/settings/kernel/drivers/kernel")
replace() {	# <pacchetto da togliere> <pacchetto D16>
	cmds+=("rm $P/$1" "cp -f :$2 $P/$(basename "$2")")
}
cmds+=("rm $P/gcc_syslibs-13.2.0_2023_08_10-1-arm.hpkg"
	"cp -f :$SYSLIBS_HPKG $P/gcc_bootstrap_syslibs-13.3.0_2026_03_29-1-arm.hpkg")
# APPS=0 non aggiunge le applicazioni D16 compilate a parte (~/haiku-arm/apps-d16)
APPS_DIR=${APPS_DIR:-$HOME/haiku-arm/apps-d16/packages}
[ "${APPS:-1}" != 0 ] && [ -d "$APPS_DIR" ] || APPS_DIR=
# programmi Haiku compilati qui per ARM (Vision, Pe, BePDF, wpa_supplicant, ...)
EXTRA_DIR=${EXTRA_DIR:-$HOME/haiku-arm/apps-extra/packages}
d16() {	# <nome> <ripiego>: il pacchetto completo delle app D16, se c'e'
	local f
	for f in ${APPS_DIR:+"$APPS_DIR"/$1-[0-9]*-arm.hpkg}; do
		[ -f "$f" ] && { echo "$f"; return; }
	done
	echo "$2"
}
replace zlib-1.2.13-1-arm.hpkg "$(d16 zlib $R/zlib_bootstrap-1.3.2-1-arm.hpkg)"
replace freetype-2.6.3-1-arm.hpkg $R/freetype_bootstrap-2.13.3-1-arm.hpkg
replace bash-4.4.023-1-arm.hpkg "$(d16 bash $R/bash_bootstrap-5.3-1-arm.hpkg)"
replace ncurses6-6.2-1-arm.hpkg $R/ncurses6_bootstrap-6.6-1-arm.hpkg
replace coreutils-8.22-1-arm.hpkg "$(d16 coreutils $R/coreutils_bootstrap-9.9-1-arm.hpkg)"
replace zstd-1.5.5-1-arm.hpkg "$(d16 zstd $ZSTD_HPKG)"
cmds+=("cp -f :$ICU_HPKG $P/icu-57.2-1-arm.hpkg")
if [ -n "${RELEASE:-}" ]; then
	PP=$G/objects/haiku/arm/packaging/packages
	export PACKAGE_TOOL=$T_TOOLS/package/package
	base=("$PP/haiku.hpkg" "$PP/haiku_datatranslators.hpkg" "$PP/haiku_loader.hpkg"
		"$G"/download/noto-*-any.hpkg "$(d16 zlib $R/zlib_bootstrap-1.3.2-1-arm.hpkg)"
		"$(d16 bash $R/bash_bootstrap-5.3-1-arm.hpkg)"
		"$(d16 coreutils $R/coreutils_bootstrap-9.9-1-arm.hpkg)"
		"$(d16 zstd $ZSTD_HPKG)" "$ICU_HPKG" "$SYSLIBS_HPKG"
		$R/freetype_bootstrap-2.13.3-1-arm.hpkg $R/ncurses6_bootstrap-6.6-1-arm.hpkg)
	for f in "$APPS_DIR"/openssl-[0-9]*-arm.hpkg; do base+=("$f"); done
	list=$(python3 "$(dirname "$0")/release-packages.py" "$RELEASE" "${base[@]}" \
		-- $RELEASE_DIRS "$R")
	for f in $list; do
		case "$(basename "$f")" in
			zlib-[0-9]*|bash-[0-9]*|coreutils-[0-9]*|zstd-[0-9]*|openssl-[0-9]*) continue ;;
		esac
		cmds+=("cp -f :$f $P/$(basename "$f")")
	done
	echo "$list" > "$T/release-packages.txt"
elif [ -n "$APPS_DIR" ]; then
	for f in "$APPS_DIR"/*.hpkg "$EXTRA_DIR"/*.hpkg; do
		[ -f "$f" ] || continue
		# zlib, bash, coreutils e zstd sono gia' stati messi sopra
		case "$(basename "$f")" in
			zlib-[0-9]*|bash-[0-9]*|coreutils-[0-9]*|zstd-[0-9]*) continue ;;
		esac
		if ! "$T_TOOLS/package/package" list "$f" > /dev/null 2>&1; then
			echo "pacchetto non valido, saltato: $(basename "$f")" >&2
			continue
		fi
		cmds+=("cp -f :$f $P/$(basename "$f")")
	done
	# GLU per i programmi OpenGL (compilata a parte, ~/haiku-arm/ac100-gl)
	for f in "$HOME"/haiku-arm/ac100-gl/glu-*-arm.hpkg; do
		[ -f "$f" ] && cmds+=("cp -f :$f $P/$(basename "$f")")
	done
	# userland_fs (lo richiede Campiello): dalla build di Haiku solo se non
	# e' gia' tra le applicazioni; la build va fatta a parte con
	# jam -q userland_fs.hpkg
	f=$G/objects/haiku/arm/packaging/packages/userland_fs.hpkg
	if [ -f "$f" ] && ! ls "$APPS_DIR"/userland_fs-*.hpkg > /dev/null 2>&1; then
		v=$("$T_TOOLS/package/package" list "$f" | sed -n 's/^\tversion: //p')
		cmds+=("cp -f :$f $P/userland_fs-$v-arm.hpkg")
	fi
	# per git: libcurl e libexpat dal bootstrap
	[ -f "$APPS_DIR"/curl-[0-9]*-arm.hpkg ] \
		|| cmds+=("cp -f :$R/curl_bootstrap-8.19.0-1-arm.hpkg $P/curl_bootstrap-8.19.0-1-arm.hpkg")
	cmds+=("cp -f :$R/expat_bootstrap-2.5.0-1-arm.hpkg $P/expat_bootstrap-2.5.0-1-arm.hpkg")
	# per python3.11: libffi dal bootstrap
	ls "$APPS_DIR"/libffi-[0-9]*-arm.hpkg > /dev/null 2>&1 \
		|| cmds+=("cp -f :$R/libffi_bootstrap-3.4.6-1-arm.hpkg $P/libffi_bootstrap-3.4.6-1-arm.hpkg")
fi

if [ -n "${RELEASE:-}" ]; then
	A=/boot/system/apps
	cmds+=("mkdir -p /myfs/home/Desktop"
		"ln -s $A/SalaGiochi/SalaGiochi /myfs/home/Desktop/SalaGiochi"
		"ln -s $A/SalaGiochi/AddGames '/myfs/home/Desktop/Add games'"
		"ln -s '$A/Install to internal memory' '/myfs/home/Desktop/Install to internal memory'")
	bfs "${cmds[@]}" "ls $P" "ls -l /myfs/home/Desktop"
	exit 0
fi

# file audio e video di prova sulla Scrivania (scripts/make-test-media.py)
MEDIA=$(cd "$(dirname "$0")/.." && pwd)/out/test-media
[ -f "$MEDIA/prova-audio.wav" ] || python3 "$(dirname "$0")/make-test-media.py" "$MEDIA"
cmds+=("mkdir -p /myfs/home/Desktop")
for f in "$MEDIA"/prova-*; do
	cmds+=("cp -f :$f /myfs/home/Desktop/$(basename "$f")")
done

# lo script delle prove nella home, i programmi di test in non-packaged
TESTS=$(cd "$(dirname "$0")/../tests" && pwd)
cmds+=("cp -f :$TESTS/ac100-test.sh /myfs/home/ac100-test.sh"
	"mkdir -p /myfs/system/non-packaged/bin")
for t in exctest/exctest sigtest/sigtest ptytest/ptytest smptest/smptest memtest/memtest aligntest/aligntest mediatest/mediatest exctest/libroot-catch pngtest/pngtest picturetest/picturetest ac100diag/ac100audio ac100diag/ac100usb ac100diag/ac100ec; do
	[ -f "$TESTS/$t" ] \
		&& cmds+=("cp -f :$TESTS/$t /myfs/system/non-packaged/bin/$(basename $t)")
done
bfs "${cmds[@]}" "ls $P" "ls -l /myfs/home /myfs/system/non-packaged/bin"
