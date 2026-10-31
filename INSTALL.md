# Installing RenkuOS on the Toshiba AC100

This guide is for the image published on the
[releases page](https://github.com/atomozero/Toshiba_AC100/releases). It starts RenkuOS
from a microSD card and leaves the internal memory (eMMC) and Android as they are.

## What you need

- A Toshiba AC100 (Compal PAZ00) with its charger.
- A microSD card of 8 GB or more. Everything on it is erased.
- A mini-USB cable and a PC with Linux. A virtual machine works if it can take over a
  USB device; on Windows, WSL with [usbipd-win](https://github.com/dorssel/usbipd-win)
  works too.
- From the release: `renkuos-ac100-<version>.img.xz` and `u-boot-ac100.bin`, with their
  `.sha256` files.

Check the downloads:

```sh
sha256sum -c renkuos-ac100-*.img.xz.sha256 u-boot-ac100.bin.sha256
```

## 1. Write the microSD card

With [balenaEtcher](https://etcher.balena.io/) or [Rufus](https://rufus.ie/): choose the
`.img.xz` file as it is (no need to unpack it), then the card, then write.

From a Linux terminal, after finding the card with `lsblk` (here `/dev/sdX`; choosing
the wrong disk erases it):

```sh
xz -dc renkuos-ac100-*.img.xz | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
```

Put the card in the slot of the AC100.

## 2. Install tegrarcm on the PC

The AC100 cannot start from the microSD card by itself: its boot ROM only starts the
bootloader of the eMMC. `tegrarcm` sends U-Boot to it over USB instead, and U-Boot starts
RenkuOS from the card.

```sh
sudo apt install tegrarcm        # Debian, Ubuntu
```

On other systems build it from [NVIDIA/tegrarcm](https://github.com/NVIDIA/tegrarcm).

## 3. Put the AC100 in recovery mode

1. Switch the AC100 off and connect the mini-USB cable to the PC.
2. Hold **Ctrl** and **Esc**, press the **power button**, then let go after a second.

The screen stays black. The PC now sees a USB device `0955:7820` (NVIDIA APX); check
with `lsusb`. If nothing appears, switch the AC100 off by holding the power button for
ten seconds and try again.

## 4. Read the boot configuration (first time only)

The boot configuration table (BCT) describes the memory of your AC100; U-Boot needs it to
start. Read it once and keep the file:

```sh
sudo tegrarcm --bct=ac100.bct readbct
```

This only reads: nothing is written to the AC100. If the next command does not find the
AC100, put it in recovery mode again (step 3).

## 5. Start RenkuOS

```sh
sudo tegrarcm --bct=ac100.bct --bootloader=u-boot-ac100.bin --loadaddr=0x108000
```

U-Boot shows a menu for three seconds and then starts RenkuOS from the microSD card. The
arrow keys and Enter choose another entry:

| Entry | What it does |
|---|---|
| Boot (SD card if present, then internal eMMC) | the default |
| Boot from SD card / from internal eMMC | only that one |
| Internal eMMC / SD card as USB disk | shows the memory to the PC over the mini-USB cable, to back it up; any key stops it |
| Reboot | restarts the AC100 |

The first start takes longer: the system sets itself up. Repeat steps 3 and 5 every time
you start the AC100; the cable can be removed once U-Boot is running.

## After the first start

- **Wi-Fi**: Preferences > Network.
- **Games**: SalaGiochi comes without games. Drop the game files you own on "Add games",
  on the desktop.
- **Bluetooth**: pair keyboards, mice and gamepads in Preferences > Bluetooth. A gamepad
  appears in Preferences > Input with its battery level.
- **Restarting** does not come back by itself: U-Boot was loaded over USB, so switch the
  AC100 off and start it again from step 3.
- **SSH**: the server is installed but does not start by itself.

## Installing on the internal memory

"Install to internal memory", on the desktop, copies the running system, home folder
included, to the eMMC. **It replaces Android**, or whatever is installed there; only the
first 16 MB of the eMMC, with the NVIDIA boot data and the original bootloader, are left
alone. It works in two phases with a restart in between, and explains each one.

Before using it, make a backup of the whole eMMC: start U-Boot as in step 5, choose
"Internal eMMC as USB disk" and copy the disk that appears on the PC to a file. With that
copy the AC100 can always be brought back as it was.

Even when installed on the eMMC, RenkuOS is started with U-Boot over USB (steps 3 and 5),
without the microSD card.

## If something goes wrong

- **Black screen after U-Boot**: check that the card is fully inserted and that the image
  was written completely (write it again and compare the checksum).
- **The PC does not see 0955:7820**: try another cable or port; the combination is
  Ctrl+Esc held while pressing the power button, with the AC100 switched off.
- **The log of the last start** is written to `AC100LOG.TXT` on the small FAT partition
  of the card, readable from any PC.

Problems and questions: [issues](https://github.com/atomozero/Toshiba_AC100/issues).
