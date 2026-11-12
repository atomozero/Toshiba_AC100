# RenkuOS / Haiku on the Toshiba AC100

Port of [RenkuOS](https://github.com/RenkuOS/Source) (a Haiku fork) to the Toshiba AC100
netbook (Compal PAZ00): NVIDIA Tegra 2, two Cortex-A9 cores at 1 GHz with a VFPv3-D16
FPU and no NEON, 512 MB of RAM, 16 GB eMMC.

## Download

A ready-to-use microSD image is published on the
[releases page](https://github.com/atomozero/Toshiba_AC100/releases). Write it to a
microSD card of 8 GB or more and start the AC100 from it with U-Boot: the steps are in
[INSTALL.md](INSTALL.md); how to use it day by day in [USING.md](USING.md). Nothing is
written to the internal eMMC unless you choose to.

## What works

| Component | Status |
|---|---|
| Boot from microSD or eMMC (U-Boot in the eMMC, power button; or over USB/RCM), Haiku's EFI loader | works |
| Two CPUs (SMP), L2 cache (optional, `arm_l2_cache true`) | works |
| CPU frequency and voltage scaling (216 MHz-1 GHz, `tegra20_cpufreq` with a governor) | works |
| 1024x600 screen: native 32-bit `tegra_dc` driver, hardware cursor, video overlay, panel off with the screen saver | works |
| HDMI output (screen mirror at 720p, audio, cursor, full-screen video) | works |
| Keyboard and touchpad (EC over NVEC), battery, power button, lid, brightness | works |
| microSD and eMMC (SDHCI, ADMA2 scatter-gather) | works |
| USB (EHCI, UTMI and ULPI PHYs) | works |
| Ralink RT3070 Wi-Fi (`ralinkwifi`), WPA2 | works |
| Audio (ALC5632 over I2S): speakers, headphones with jack detection, built-in digital microphone and headset microphone, controls in the Media preferences | works |
| Battery-backed clock (TPS6586x) | works |
| Power off | works |
| Restart | needs U-Boot in the eMMC |
| 2D GPU (GR2D via host1x) | first tests, not yet used by the system |
| Bluetooth: HID keyboards, mice and gamepads (tested with an Xbox Wireless Controller and the internal CSR adapter), speakers as the audio output | works |
| Webcam, 3D GPU, suspend | to do |

Details in [docs/STATUS.md](docs/STATUS.md) and [docs/ROADMAP.md](docs/ROADMAP.md) (in
Italian), and in [docs/AC100_ENERGIA_E_SCOPERTE.md](docs/AC100_ENERGIA_E_SCOPERTE.md)
(Italian) for what we measured and learnt about the hardware.

## Contents

| Folder | What it holds |
|---|---|
| `docs/` | status, roadmap, hardware map, boot procedure, driver inventory (mostly in Italian) |
| `patches/renkuos/` | the changes to RenkuOS as a patch series, base in `BASE.txt` |
| `scripts/` | microSD image creation, RCM boot, QEMU tests, BFS tools |
| `tests/` | test and diagnostic programs to run on the AC100 (`ac100audio`, `ac100usb`, `ac100ec`, `ac100power`, `mictest`, `memtest`, ...) |
| `packaging/` | scripts for packages built on Linux, and ARM ports (doomgeneric, rtelevision) |

## Cross-compiling from Debian (x86_64)

Reconstructed from the environment used for the port (Debian 12 in WSL): steps 4-5 depend
on packages whose build is not automated yet, see below. Everything lives in `~/haiku-arm`:
the scripts of this repository use those paths (`haiku/`, `haiku-bs/`, `out/`), so it is
best to keep them.

### 1. Debian packages

```sh
sudo apt install git build-essential autoconf automake libtool texinfo flex bison \
    gawk nasm bc zlib1g-dev libzstd-dev python3 python3-venv unzip zip wget xz-utils \
    mtools xorriso u-boot-tools device-tree-compiler \
    gcc-arm-linux-gnueabihf libusb-1.0-0-dev libcrypto++-dev pkg-config \
    qemu-system-arm
```

`gcc-arm-linux-gnueabihf` is only needed for U-Boot, `libusb`/`libcrypto++` for `tegrarcm`,
`qemu-system-arm` for tests without the hardware.

### 2. Sources

```sh
mkdir -p ~/haiku-arm/bin ~/haiku-arm/out && cd ~/haiku-arm
git clone https://github.com/atomozero/Toshiba_AC100.git
git clone https://review.haiku-os.org/buildtools
git clone https://github.com/RenkuOS/Source.git haiku

# buildtools: the fix for C++ exceptions on ARM
cd buildtools
git checkout $(cat ../Toshiba_AC100/patches/buildtools/BASE.txt)
git am ../Toshiba_AC100/patches/buildtools/*.patch

# RenkuOS with the AC100 support
cd ../haiku
git checkout $(cut -d' ' -f2 ../Toshiba_AC100/patches/renkuos/BASE.txt)
git apply --index ../Toshiba_AC100/patches/renkuos/0001-*.patch
git commit -m "AC100 tree at the merge of RenkuOS main"
git am $(ls ../Toshiba_AC100/patches/renkuos/0*.patch | tail -n +2)
```

### 3. jam and the ARM toolchain (VFPv3-D16)

```sh
cd ~/haiku-arm/buildtools/jam
make && cp bin.linuxx86/jam ~/haiku-arm/bin/
export PATH=~/haiku-arm/bin:$PATH

cd ~/haiku-arm/haiku
mkdir -p generated.arm && cd generated.arm
../configure --build-cross-tools arm --cross-tools-source ../../buildtools -j$(nproc)
```

The toolchain is created in `generated.arm/cross-tools-arm`. The patches build GCC with
`--with-fpu=vfpv3-d16 --with-float=hard`, so the generated code does not use the
registers d16-d31, which the Tegra 2 does not have. It takes about an hour.

### 4. D16 base packages (bootstrap)

The official ARM packages of HaikuPorts use d16-d31 and stop with SIGILL on the Tegra 2.
The base packages are rebuilt with our toolchain:

```sh
cd ~/haiku-arm
git clone https://github.com/haikuports/haikuporter.git
git clone https://github.com/haikuports/haikuports.cross.git
git clone https://github.com/haikuports/haikuports.git
git clone https://github.com/RenkuOS/Source.git haiku-bs
cd haiku-bs
git checkout $(cut -d' ' -f2 ../Toshiba_AC100/patches/renkuos/BASE.txt)
git apply --index ../Toshiba_AC100/patches/renkuos/0001-*.patch
git commit -m "AC100 tree at the merge of RenkuOS main"
git am $(ls ../Toshiba_AC100/patches/renkuos/0*.patch | tail -n +2)
mkdir generated.arm && cd generated.arm
../configure --cross-tools-prefix ~/haiku-arm/haiku/generated.arm/cross-tools-arm/bin/arm-unknown-haiku- \
    --bootstrap ../../haikuporter/haikuporter ../../haikuports.cross ../../haikuports
jam -q @bootstrap-raw
```

A few packages must then be rebuilt separately, with the same toolchain, and passed to the
image script:
- zstd and ICU 57.2 built for D16 (variables `ZSTD_HPKG` and `ICU_HPKG`);
- `gcc_bootstrap_syslibs` with the libstdc++ fixed for exceptions (`SYSLIBS_HPKG`).

These steps are not automated yet.

### 5. System and microSD image

```sh
cd ~/haiku-arm/haiku/generated.arm
echo 'HAIKU_IMAGE_SIZE = 2000 ;' >> ../build/jam/UserBuildConfig
jam -q -j$(nproc) @minimum-mmc

cd ~/haiku-arm/Toshiba_AC100
APPS=0 G=~/haiku-arm/haiku/generated.arm scripts/make-ac100-image.sh ~/haiku-arm/out/ac100-sd.img
```

`APPS=0` creates the image without the D16 applications built separately
(`~/haiku-arm/apps-d16`). For tests without the hardware:
`scripts/qemu-virt.sh ~/haiku-arm/out/ac100-sd.img -smp 2`.

### 6. U-Boot and tegrarcm (boot over USB)

```sh
cd ~/haiku-arm
git clone https://source.denx.de/u-boot/u-boot.git
cd u-boot
make O=../build-uboot-paz00 paz00_defconfig
cat ../Toshiba_AC100/u-boot/paz00-ums.config >> ../build-uboot-paz00/.config
make O=../build-uboot-paz00 olddefconfig
make O=../build-uboot-paz00 CROSS_COMPILE=arm-linux-gnueabihf- -j$(nproc)
mkdir -p ~/haiku-arm/Toshiba_AC100/out/uboot-ums
cp ../build-uboot-paz00/u-boot-dtb-tegra.bin ~/haiku-arm/Toshiba_AC100/out/uboot-ums/

cd ~/haiku-arm
git clone https://github.com/NVIDIA/tegrarcm.git
cd tegrarcm && ./autogen.sh && ./configure --prefix=$HOME/haiku-arm/tegrarcm-install && make install
cp ~/haiku-arm/tegrarcm-install/bin/tegrarcm ~/haiku-arm/Toshiba_AC100/out/
```

`paz00-ums.config` adds `ums` and fastboot on the mini-USB port to U-Boot: they are used to
back up the eMMC from the PC. For reading the BCT and booting see
[docs/AC100_BOOT.md](docs/AC100_BOOT.md) (Italian) and `scripts/ac100-rcm.sh`.

### 7. On the AC100

Write `~/haiku-arm/out/ac100-sd.img` to the microSD card (from Windows:
`scripts/write-sd.ps1`) and insert it. Then, at every start, put the AC100 in APX mode
(switched off, mini-USB cable to the PC, hold Ctrl+Esc and press the power button) and
from the PC:

```sh
cd ~/haiku-arm/Toshiba_AC100
scripts/ac100-rcm.sh readbct   # first time only: reads the BCT into out/ac100.bct
scripts/ac100-rcm.sh boot      # loads U-Boot into RAM; U-Boot starts Haiku from the microSD
```

Nothing is written to the eMMC: switch the AC100 off and Android is back as before.

## License

The patches follow the license of the files they change (MIT for Haiku/RenkuOS). The
scripts, tests and documentation of this repository are released under the MIT license.
