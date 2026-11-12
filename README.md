# RenkuOS / Haiku on the Toshiba AC100

Port of [RenkuOS](https://github.com/RenkuOS/Source) (a Haiku fork) to the Toshiba AC100
netbook (Compal PAZ00): NVIDIA Tegra 2, two Cortex-A9 cores at 1 GHz with a VFPv3-D16
FPU and no NEON, 512 MB of RAM, 16 GB eMMC.

*Il resto della documentazione è in italiano.*

## Download

A ready-to-use microSD image is published on the
[releases page](https://github.com/atomozero/Toshiba_AC100/releases). Write it to a
microSD card of 8 GB or more and start the AC100 from it with U-Boot: the steps are in
[INSTALL.md](INSTALL.md). Nothing is written to the internal eMMC unless you choose to.

## Cosa funziona

| Componente | Stato |
|---|---|
| Avvio da microSD o da eMMC (U-Boot nell'eMMC, tasto di accensione; o via USB/RCM), loader EFI di Haiku | funziona |
| Due CPU (SMP), cache L2 (opzionale, `arm_l2_cache true`) | funziona |
| Schermo 1024x600: driver nativo `tegra_dc` a 32 bit, cursore hardware, overlay video | funziona |
| Uscita HDMI (copia dello schermo a 720p, audio, cursore, video a schermo intero) | funziona |
| Tastiera e touchpad (EC via NVEC), batteria, tasto di accensione, coperchio, luminosità | funziona |
| microSD ed eMMC (SDHCI, ADMA2 scatter-gather) | funziona |
| USB (EHCI, PHY UTMI e ULPI) | funziona |
| Wi-Fi Ralink RT3070 (`ralinkwifi`), WPA2 | funziona |
| Audio (ALC5632 su I2S): casse, cuffie con rilevamento, microfono interno e delle cuffie, controlli nelle preferenze Media | funziona |
| Orologio con batteria tampone (TPS6586x) | funziona |
| Spegnimento | funziona |
| Riavvio | richiede U-Boot sull'eMMC |
| GPU 2D (GR2D via host1x) | prime prove, non ancora usata dal sistema |
| Bluetooth: tastiere, mouse e gamepad HID (provato con un Xbox Wireless Controller e l'adattatore CSR interno) | funziona |
| Webcam, GPU 3D, sospensione | da fare |

Dettagli in [docs/STATUS.md](docs/STATUS.md) e [docs/ROADMAP.md](docs/ROADMAP.md).

## Contenuto

| Cartella | Cosa contiene |
|---|---|
| `docs/` | stato dei lavori, roadmap, mappa dell'hardware, procedura di avvio, inventario dei driver |
| `patches/renkuos/` | le modifiche a RenkuOS come serie di patch (`git am`), base in `BASE.txt` |
| `scripts/` | creazione dell'immagine per la microSD, avvio via RCM, prove in QEMU, strumenti BFS |
| `tests/` | programmi di prova e diagnostica da usare sull'AC100 (`ac100audio`, `ac100usb`, `ac100ec`, `memtest`, ...) |
| `packaging/` | script per i pacchetti compilati su Linux e port per ARM (doomgeneric, rtelevision) |

## Compilazione incrociata da Debian (x86_64)

Ricostruita dall'ambiente usato per il porting (Debian 12 in WSL): i passi 4-5 dipendono
da pacchetti non ancora automatizzati, vedi sotto. Tutto sta in `~/haiku-arm`: gli script di questo repository usano
quei percorsi (`haiku/`, `haiku-bs/`, `out/`), quindi conviene mantenerli.

### 1. Pacchetti di Debian

```sh
sudo apt install git build-essential autoconf automake libtool texinfo flex bison \
    gawk nasm bc zlib1g-dev libzstd-dev python3 python3-venv unzip zip wget xz-utils \
    mtools xorriso u-boot-tools device-tree-compiler \
    gcc-arm-linux-gnueabihf libusb-1.0-0-dev libcrypto++-dev pkg-config \
    qemu-system-arm
```

`gcc-arm-linux-gnueabihf` serve solo per U-Boot, `libusb`/`libcrypto++` per `tegrarcm`,
`qemu-system-arm` per le prove senza hardware.

### 2. Sorgenti

```sh
mkdir -p ~/haiku-arm/bin ~/haiku-arm/out && cd ~/haiku-arm
git clone https://github.com/atomozero/Toshiba_AC100.git
git clone https://review.haiku-os.org/buildtools
git clone https://github.com/RenkuOS/Source.git haiku

# buildtools: la correzione delle eccezioni C++ su ARM
cd buildtools
git checkout $(cat ../Toshiba_AC100/patches/buildtools/BASE.txt)
git am ../Toshiba_AC100/patches/buildtools/*.patch

# RenkuOS con il supporto all'AC100
cd ../haiku
git checkout $(cut -d' ' -f2 ../Toshiba_AC100/patches/renkuos/BASE.txt)
git apply --index ../Toshiba_AC100/patches/renkuos/0001-*.patch
git commit -m "AC100 tree at the merge of RenkuOS main"
git am $(ls ../Toshiba_AC100/patches/renkuos/0*.patch | tail -n +2)
```

### 3. jam e toolchain ARM (VFPv3-D16)

```sh
cd ~/haiku-arm/buildtools/jam
make && cp bin.linuxx86/jam ~/haiku-arm/bin/
export PATH=~/haiku-arm/bin:$PATH

cd ~/haiku-arm/haiku
mkdir -p generated.arm && cd generated.arm
../configure --build-cross-tools arm --cross-tools-source ../../buildtools -j$(nproc)
```

La toolchain viene creata in `generated.arm/cross-tools-arm`. Le patch fanno compilare GCC
con `--with-fpu=vfpv3-d16 --with-float=hard`, quindi il codice generato non usa i
registri d16-d31, che il Tegra 2 non ha. Ci vuole circa un'ora.

### 4. Pacchetti di base D16 (bootstrap)

I pacchetti ARM ufficiali di HaikuPorts usano d16-d31 e sul Tegra 2 si fermano con
SIGILL. Si ricompila la base con la nostra toolchain:

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

Alcuni pacchetti vanno poi ricompilati a parte, con la stessa toolchain, e passati allo
script dell'immagine:
- zstd e ICU 57.2 in versione D16 (variabili `ZSTD_HPKG` e `ICU_HPKG`);
- `gcc_bootstrap_syslibs` con la libstdc++ corretta per le eccezioni (`SYSLIBS_HPKG`).

Questi passi non sono ancora automatizzati.

### 5. Sistema e immagine per la microSD

```sh
cd ~/haiku-arm/haiku/generated.arm
echo 'HAIKU_IMAGE_SIZE = 2000 ;' >> ../build/jam/UserBuildConfig
jam -q -j$(nproc) @minimum-mmc

cd ~/haiku-arm/Toshiba_AC100
APPS=0 G=~/haiku-arm/haiku/generated.arm scripts/make-ac100-image.sh ~/haiku-arm/out/ac100-sd.img
```

`APPS=0` crea l'immagine senza le applicazioni D16 compilate a parte (`~/haiku-arm/apps-d16`).
Per le prove senza hardware: `scripts/qemu-virt.sh ~/haiku-arm/out/ac100-sd.img -smp 2`.

### 6. U-Boot e tegrarcm (avvio via USB)

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

`paz00-ums.config` aggiunge a U-Boot `ums` e fastboot sulla porta mini-USB: servono per
fare il backup dell'eMMC dal PC. Per la lettura del BCT e l'avvio vedi
[docs/AC100_BOOT.md](docs/AC100_BOOT.md) e `scripts/ac100-rcm.sh`.

### 7. Sull'AC100

Scrivere `~/haiku-arm/out/ac100-sd.img` sulla microSD (da Windows: `scripts/write-sd.ps1`)
e inserirla. Poi, a ogni avvio, mettere l'AC100 in modalità APX (spento, cavo mini-USB al
PC, Ctrl+Esc tenuti premuti e tasto di accensione) e dal PC:

```sh
cd ~/haiku-arm/Toshiba_AC100
scripts/ac100-rcm.sh readbct   # solo la prima volta: legge il BCT in out/ac100.bct
scripts/ac100-rcm.sh boot      # carica U-Boot in RAM; U-Boot avvia Haiku dalla microSD
```

Niente viene scritto sull'eMMC: spento l'AC100, Android torna come prima.

## Licenza

Le patch seguono la licenza dei file che modificano (MIT per Haiku/RenkuOS). Gli script,
i test e la documentazione di questo repository sono rilasciati con licenza MIT.
