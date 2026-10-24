# Roadmap: Haiku su Toshiba AC100 (Tegra 2)

Riferimento hardware: [HARDWARE_MAP.md](HARDWARE_MAP.md). I riferimenti `§x.y` puntano lì.

Principio guida: **portare in QEMU tutto quello che si può**, e passare all'AC100 solo per
ciò che è davvero specifico di Tegra. Ogni fase ha un criterio di completamento
verificabile.

**Stato al 2026-10-17:** fasi 0 e 1 completate. In QEMU il kernel completa il boot e lo
userland parte; restano crash legati alle eccezioni C++. Fasi 2 e 3 scritte nel codice,
ma per GICv1, timer Tegra e percorso FPU D16 serve l'hardware per verificarle. Il dettaglio è in
[STATUS.md](STATUS.md).

---

## Fase 0: ambiente di sviluppo

**Obiettivo:** compilare un'immagine Haiku ARM32 partendo dai sorgenti.

- [x] Clonare per intero `haiku` e `buildtools` **sul filesystem Linux di WSL**
      (es. `~/haiku-arm/`), non su `/mnt/d`: il build di Haiku richiede un filesystem
      case-sensitive, e su `/mnt/d` è anche molto più lento.
- [x] Creare un branch di lavoro `ac100` nel repo `haiku`.
- [x] Cambiare la FPU della toolchain da `vfpv3` a **`vfpv3-d16`** in
      `build/scripts/build_cross_tools_gcc4` (§6.1).
- [x] `configure --cross-tools-source ../buildtools --build-cross-tools arm`.
- [x] `jam -q @minimum-mmc` → `haiku-mmc.image`. L'immagine contiene una partizione EFI
      (FAT, con `EFI/BOOT/BOOTARM.EFI`) e una partizione BFS.
- [x] Installare `qemu-system-arm` e compilare U-Boot `qemu_arm_defconfig` e
      `vexpress_ca9x4_defconfig`.

**Fatto quando:** `haiku-minimum.mmc` viene generato senza errori con la toolchain
`vfpv3-d16`. Va verificato con `readelf -A` che i binari riportino `Tag_FP_arch: VFPv3-D16`.

## Fase 1: baseline in QEMU `virt`

**Obiettivo:** capire **dove arriva oggi** Haiku ARM32 upstream, prima di toccare qualunque
cosa. Così i problemi già presenti non si confondono con quelli introdotti da noi.

- [x] Boot: `qemu-system-arm -M virt -cpu cortex-a15` + U-Boot `qemu_arm` →
      `bootefi` → `BOOTARM.EFI`.
- [x] Annotare dove si ferma: loader, kernel, KDL, `app_server`?
- [x] Preparare uno script di avvio riproducibile (`scripts/qemu-virt.sh`).

**Fatto quando:** è documentato, con il log seriale, il punto esatto in cui si ferma il boot
upstream.

## Fase 2: Cortex-A9 in QEMU `vexpress-a9`

**Obiettivo:** risolvere in emulazione i problemi propri dell'A9, che l'AC100 condivide.

- [x] **FPU a 16 registri** (§6.1): `arm_save_fpu`/`arm_restore_fpu` scelgono il percorso
      D16 o D32 in base a `MVFR0`.
- [x] **GICv1** (§6.2): il kernel accetta `INTC_KIND_GICV1` usando
      `GICv2InterruptController`.
- [x] **Timer senza Generic Timer** (§6.3): driver per il timer Tegra (AC100) e per il TWD
      del Cortex-A9, provato su QEMU `vexpress-a9`. Per QEMU va scritto un driver per il timer dell'A9 (global
      timer / TWD) o per l'SP804 (vedi la decisione D1).
- [ ] UART: `arm,pl011` è già supportato su vexpress, non serve altro.

**Fatto quando:** su `vexpress-a9` il kernel arriva almeno al prompt KDL e il cambio di
thread funziona, cioè passa `arm_save_fpu`.

✅ **Raggiunto**: su `vexpress-a9` il kernel gira con GICv1 e TWD (interrupt del timer
gestiti, verificati dal KDL) fino alla ricerca del volume di boot, che fallisce perché manca
un driver per il controller SD PL181.

## Fase 3: bring-up sull'AC100

**Obiettivo:** il kernel Haiku arriva al KDL sullo schermo dell'AC100.

- [ ] **Backup completo dell'eMMC** prima di qualunque scrittura, in modalità recovery/APX.
- [ ] Compilare U-Boot `paz00_defconfig` (**compilato**, da provare). Avviarlo prima **in RAM via USB** (RCM, senza
      flashare), poi installarlo sull'eMMC solo quando è collaudato.
- [ ] Verificare che U-Boot inizializzi il pannello e offra `bootefi` (EFI GOP).
- [x] Caricare il **DTB di Linux** (`tegra20-paz00.dtb` da `upstream/linux`) invece del DT
      interno di U-Boot: sono due versioni diverse, 775 righe contro 619.
- [x] UART: aggiungere `nvidia,tegra20-uart` a `kSupportedUarts` (§6.4).
- [x] Timer Tegra `nvidia,tegra20-timer`: TIMERUS a 1 MHz + TMR1 one-shot (§6.3).
- [ ] Output di debug sul **framebuffer** (KDL a schermo), perché la seriale dell'AC100 non è
      facilmente accessibile (§6.4 (?)).

**Fatto quando:** dalla microSD, `BOOTARM.EFI` si avvia, il kernel parte e il KDL compare
sul display dell'AC100.

## Fase 4: storage e input USB → desktop

**Obiettivo:** il desktop Haiku sull'AC100, con tastiera USB.

- [ ] **Bootstrap build** di Haiku ARM: ricompilare `gcc_syslibs` (libstdc++.so, libgcc_s.so)
      e gli altri pacchetti base con la toolchain D16 e la patch TARGET2 di buildtools
      (STATUS.md, "Problemi noti").
- [~] **SDHCI Tegra** (scritto, `0011`; da provare sull'AC100; manca il card detect via GPIO): attachment FDT per il driver SDHCI di Haiku, più i quirk di
      `sdhci-tegra.c`. Si comincia dalla microSD (SDMMC1), poi l'eMMC (SDMMC4).
- [~] **EHCI Tegra** (scritto, `0012`; da provare; PHY inizializzati da U-Boot): attachment FDT per il driver EHCI, più l'init del PHY UTMI e ULPI
      (`phy-tegra-usb.c`).
- [ ] Boot della partizione BFS da SD e `app_server` sul framebuffer GOP.

**Fatto quando:** il desktop Tracker è visibile e si usa con tastiera e mouse USB.

## Fase 5: Embedded Controller (NVEC)

**Obiettivo:** tastiera e touchpad integrati, batteria, spegnimento.

- [ ] Driver I²C **slave** Tegra con la macchina a stati di `nvec.c`, incluso il workaround
      `udelay(33)`, e la request su GPIO V2.
- [ ] Sotto-driver: tastiera (`nvec-keytable.h` → keymap Haiku), touchpad (PS/2 incapsulato
      → driver mouse PS/2), batteria (→ API power di Haiku), power off.
- [ ] Per collaudarlo **serve l'hardware**: il protocollo non è emulato in QEMU.

**Fatto quando:** l'AC100 si usa senza periferiche USB e si spegne da Haiku.

## Fase 6: completamento

In ordine di valore: SMP (CPU1 via flow controller, §6.5) · retroilluminazione PWM ·
eMMC come disco di sistema · audio ALC5632 + I2S · Wi-Fi USB · HDMI · DVFS e
temperatura.

---

## Decisioni aperte

| # | Decisione | Proposta |
|---|---|---|
| D1 | Timer: global timer dell'A9 (testabile in QEMU e presente su Tegra, ma la frequenza dipende dalla CPU) o timer Tegra (frequenza fissa a 1 MHz, solo su hardware) | Timer Tegra come principale sull'AC100. Global timer A9 solo se serve per QEMU |
| D2 | Bootloader: U-Boot mainline flashato o caricato via RCM a ogni avvio | RCM durante lo sviluppo, flash a fine fase 3 |
| D3 | Patch locali o contributi upstream (Gerrit di Haiku) | Patch piccole e separate (FPU, GICv1, UART, timer), proposte upstream appena funzionano in QEMU |
| D4 | Pacchetti HaikuPorts ARM: sono compilati per D32 | Per le fasi 1-4 bastano il bootstrap e `minimum`; ricompilare i port quando servono |

## Rischi

- **Toolchain e pacchetti D32**: qualunque binario precompilato per ARM può contenere
  istruzioni `d16-d31`. Va controllato con `readelf -A` prima di includerlo.
- **Maturità di Haiku ARM32**: lo stato reale del port si saprà solo con la fase 1, e potrebbe
  richiedere correzioni generiche prima di quelle per l'AC100.
- **Nessuna seriale comoda**: il debug sull'hardware dipende dal framebuffer. Saldare un
  cavo sulla UARTA, se si trovano i pad, accelera molto la fase 3.
- **Flash dell'eMMC**: senza backup si rischia di non poter più avviare l'AC100. Il backup
  in APX è obbligatorio.
