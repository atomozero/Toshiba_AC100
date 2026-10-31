RenkuOS, a fork of Haiku, for the Toshiba AC100 (Tegra 2, 512 MB RAM), on a microSD card. Nothing is written to the internal eMMC: switch the AC100 off and Android is still there.

### What works
Display (1024x600, native driver, HDMI mirror at 720p), keyboard and touchpad, battery, both CPU cores, microSD and eMMC, USB, the internal Wi-Fi with WPA2, sound (speakers, headphones, microphones), clock, power off. Bluetooth keyboards, mice and gamepads, tested with an Xbox Wireless Controller and the internal CSR adapter.

Applications: WebPositive, LocalSend, Genio, BePDF, WonderBrush, Atomo123, R Television, Cricket, a SIP softphone, SalaGiochi (games for children with libretro cores; no games are included, "Add games" puts the ones you own in place), and git, curl, ssh, nano and other command line tools.

### Not yet
Webcam, 3D acceleration, suspend. Restarting needs U-Boot in the eMMC; with the USB boot below, switch off and start again instead. The L2 cache is off.

### Install
You need a microSD card of 8 GB or more, a mini-USB cable and a PC with Linux. In short: write the image to the card (balenaEtcher and Rufus take the `.img.xz` as it is), put the AC100 in recovery mode (switched off, hold Ctrl+Esc and press the power button), read its boot configuration once with `tegrarcm --bct=ac100.bct readbct`, then start U-Boot with `tegrarcm --bct=ac100.bct --bootloader=u-boot-ac100.bin --loadaddr=0x108000`; U-Boot starts RenkuOS from the card. The full steps are in [INSTALL.md](https://github.com/atomozero/Toshiba_AC100/blob/main/INSTALL.md).

"Install to internal memory", on the desktop, copies the system to the eMMC and **replaces Android**; make a backup of the eMMC first, as INSTALL.md explains.

The SSH server does not start by itself. Check the files against the `.sha256` files.

### Sources
The system is RenkuOS `aae7d9f7be` plus the 63 patches in [`patches/renkuos`](https://github.com/atomozero/Toshiba_AC100/tree/main/patches/renkuos). U-Boot is upstream `8d534cec` plus the patches and the configuration in [`u-boot`](https://github.com/atomozero/Toshiba_AC100/tree/main/u-boot) (GPL-2.0+). The README explains how to build both.
