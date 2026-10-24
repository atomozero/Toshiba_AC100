#!/bin/bash
# Avvia l'immagine Haiku ARM in QEMU "virt" (Cortex-A15) con U-Boot come firmware EFI.
# Uso: qemu-virt.sh [immagine.mmc] [opzioni qemu extra...]
set -e
source "$HOME/haiku-arm/env.sh"
export QEMU_AUDIO_DRV=none

IMAGE=${1:-$HOME/haiku-arm/haiku/generated.arm/haiku-mmc.image}
shift || true
UBOOT=$HOME/haiku-arm/build-uboot-qemu_arm/u-boot.bin

exec qemu-system-arm -M virt -cpu cortex-a15 -m 1024 \
	-bios "$UBOOT" \
	-drive file="$IMAGE",if=none,format=raw,id=disk0 \
	-device virtio-blk-device,drive=disk0 \
	-nic none -display none -serial mon:stdio \
	"$@"
