# RenkuOS / Haiku on the Toshiba AC100

Port of [RenkuOS](https://github.com/RenkuOS/Source) (a Haiku fork) to the Toshiba AC100
netbook (Compal PAZ00): NVIDIA Tegra 2, two Cortex-A9 cores at 1 GHz with a VFPv3-D16
FPU and no NEON, 512 MB of RAM, 16 GB eMMC.

*Il resto della documentazione è in italiano.*

## Cosa funziona

| Componente | Stato |
|---|---|
| Avvio da microSD (U-Boot caricato via USB/RCM, loader EFI di Haiku) | funziona |
| Due CPU (SMP), cache L2 (opzionale, `arm_l2_cache true`) | funziona |
| Schermo 1024x600 (framebuffer di U-Boot, 16 bit) | funziona, senza accelerazione |
| Tastiera e touchpad (EC via NVEC), batteria, tasto di accensione, coperchio, luminosità | funziona |
| microSD ed eMMC (SDHCI, ADMA2 scatter-gather) | funziona |
| USB (EHCI, PHY UTMI e ULPI) | funziona |
| Wi-Fi Ralink RT3070 (`ralinkwifi`), WPA2 | funziona |
| Audio (ALC5632 su I2S): casse, cuffie con rilevamento | funziona |
| Orologio con batteria tampone (TPS6586x) | funziona |
| Spegnimento | funziona |
| Riavvio | richiede U-Boot sull'eMMC |
| Microfono, Bluetooth, HDMI, webcam, GPU, sospensione | da fare |

Dettagli in [docs/STATUS.md](docs/STATUS.md) e [docs/ROADMAP.md](docs/ROADMAP.md).

## Contenuto

| Cartella | Cosa contiene |
|---|---|
| `docs/` | stato dei lavori, roadmap, mappa dell'hardware, procedura di avvio, inventario dei driver |
| `patches/renkuos/` | le modifiche a RenkuOS come serie di patch (`git am`), base in `BASE.txt` |
| `scripts/` | creazione dell'immagine per la microSD, avvio via RCM, prove in QEMU, strumenti BFS |
| `tests/` | programmi di prova e diagnostica da usare sull'AC100 (`ac100audio`, `ac100usb`, `ac100ec`, `memtest`, ...) |
| `packaging/` | script per i pacchetti compilati su Linux e port per ARM (doomgeneric, rtelevision) |

## Come si costruisce

1. Clonare RenkuOS e applicare le patch:
   ```sh
   git clone https://github.com/RenkuOS/Source.git haiku
   cd haiku
   git checkout $(cut -d' ' -f2 ../Toshiba_AC100/patches/renkuos/BASE.txt)
   git am ../Toshiba_AC100/patches/renkuos/*.patch
   ```
2. Compilare i buildtools e il sistema per ARM (`configure --cross-tools-source ...
   --build-cross-tools arm`, poi `jam -q @minimum-mmc`). La toolchain deve generare codice
   VFPv3-D16 (`-mfpu=vfpv3-d16`).
3. Creare l'immagine: `scripts/make-ac100-image.sh out/ac100-sd.img`.
4. Scrivere l'immagine sulla microSD e avviare l'AC100 come descritto in
   [docs/AC100_BOOT.md](docs/AC100_BOOT.md).

I pacchetti delle applicazioni vanno ricompilati per VFPv3-D16: quelli ARM ufficiali
usano i registri d16-d31, che il Tegra 2 non ha.

## Licenza

Le patch seguono la licenza dei file che modificano (MIT per Haiku/RenkuOS). Gli script,
i test e la documentazione di questo repository sono rilasciati con licenza MIT.
