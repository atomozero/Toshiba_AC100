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

## Fase 7: prestazioni e sviluppo nativo

- [x] Tensioni nominali per 1 GHz (25/10): la CPU girava a 1,0 V invece di 1,1 V.
      `tegra_nominal_voltages true` le alza all'avvio; driver `tegra_dvfs` per leggerle.
- [x] **gcc nativo per ARMv7-A** (25/10): `gcc_bootstrap` compilava per default per ARMv4T
      (codice lento, niente atomiche a 64 bit, serviva `libatomic`). Il file `specs` della
      revisione 3 non bastava: i default di configure si applicano prima. Revisione 4 nel
      devel-kit, con driver e compilatori ricompilati con `--with-arch=armv7-a
      --with-tune=cortex-a9 --with-fpu=vfpv3-d16`; stessi flag nella ricetta di `haikuports.cross`.
- [~] **Bluetooth**: il modulo USB `0db0:a871` è un CSR BlueCore4. All'immagine `minimum`
      mancavano `bluetooth_server`, lo stack HCI/L2CAP e il driver `h2generic`: aggiunti
      (25/10, hrev 428). Il modulo va online e risponde (indirizzo e nome letti). Il server
      parte su richiesta della preferenza Bluetooth. Da provare: ricerca e abbinamento di un
      dispositivo; poi eventualmente il profilo HID dei GSoC (Gerrit 11406 + 11688).
- [ ] `hzram` (swap compresso in RAM): il core (allocatore + LZ4) passa i test sull'AC100,
      LZ4 scrive 33–65 MB/s e legge 80–125 MB/s (30–120 µs a pagina). L'integrazione nel
      kernel non è ancora utilizzabile: il loro `VMAnonymousCache.cpp` non sostituisce quello
      di Haiku e il pool è tutto memoria bloccata.
- [ ] Sensore ADT7461: il canale remoto (CPU) resta fermo a 48,5 °C.
- [ ] cpufreq e SCLK oltre 72 MHz, con le tabelle OPP di Linux.
- [x] Traduttore JPEG nell'immagine (25/10, hrev 430), con la correzione di `__swap_float`
      e `__swap_double` in libroot: con l'ABI hard-float non scambiavano nulla.
- [x] Rapporti di crash (25/10, hrev 431): Debugger non esiste per ARM, quindi "Salva
      rapporto" non salvava nulla. Ora, se Debugger manca, debug_server scrive da sé sulla
      Scrivania un rapporto di testo (motivo, registri, stack di tutti i thread, librerie).
- [ ] Crash occasionali: `campiello_daemon` all'avvio (in `BApplication`, lettura a 0x4 in
      `BTokenSpace`), preferenza Bluetooth e NetSurf (`double free`).
- [ ] `hey`: SIGBUS (accesso non allineato) stampando un `double`.
- [ ] Applicazioni di atomozero: **Pippo** (server MCP nativo) e **Pluto** (client AI con
      CLI, GUI e add-on di Tracker), compilate sull'AC100 e incluse nell'immagine.
- [ ] HaikuBench: patch per i 32 bit (`fCommandPool` come `uint64`, `-march=armv7-a`).

## Fase 8: driver video nativo (controller video del Tegra 2)

Codice MIT; Linux (GPL) solo come documentazione, grate/opentegra (MIT) riutilizzabili.
app_server non usa più le funzioni 2D degli accelerant (dal 2024): il guadagno viene
dal controller video (32 bit, overlay, cursore), il motore 2D serve solo con modifiche
ad app_server.

- [x] **Fase 0, misure** (25/10). Strumenti: `tegra_dc_diag` (driver di sola lettura,
      `/dev/misc/tegra_dc_diag`) e `tests/fbbench`.
      - DC A da PLL_P, divisore 6 = 54 MHz; tempi da EDID: H sync 32, bp 330, fp 48;
        V sync 6, bp 21, fp 3 (59,8 Hz). Finestra A B5G6R5 a `0x1f800000`, passo 2048.
        Gli indirizzi per finestra di Linux (0xa00/0xc00/0xe00) **non** sono finestre
        separate: raggiungono tutti la finestra scelta con `DC_CMD_DISPLAY_WINDOW_HEADER`.
      - host1x acceso a 150 MHz; 2D, 3D, DC B, HDMI spenti; PLL_D spento.
      - Memoria `0x1f800000`–`0x1ffd0000` (7,8 MB) riservata e libera; gli ultimi 192 KB
        sono del kernel ("physical page pool").
      - Schermata intera: oggi a 16 bit 13,9 ms (conversione come app_server); a 32 bit
        copia semplice circa 3,5 ms.
- [x] Fase 1: driver `tegra_dc` e accelerant (25/10). 16/32 bit (B8G8R8A8, "24 bit" nella
      preferenza Schermo), vsync a 60 Hz, luminosità (PWM, 256 livelli) e DPMS, nel pacchetto
      `haiku` per ARM. Dithering già attivo da U-Boot (error diffusion, 6 bit). Nessun clock o
      tempo toccato; ripiego sul framebuffer. GLTeapot circa 110 FPS.
      - Il semaforo del vsync va assegnato al team che apre il device (come intel_extreme).
      - nvec: i tasti Fn ripartono dal valore del PWM; il coperchio ripristina ciò che trova;
        se l'EC non manda eventi per 30 s rimanda l'abilitazione di eventi, tastiera e
        touchpad (sblocca l'input muto).
      - Preferenza Schermo: lo slider segue i tasti Fn; frequenza unica arrotondata come quella
        del modo (mostrava "refresh rate").
      - Corretto un double free di `BMenuField` alla chiusura delle finestre (patch in
        `patches/0001-BMenuField-...`).
- [x] Fase 2: cursore a colori sulla finestra C (25/10, commit `f2eed36425`, `bb84d765d5`).
      Immagine 64×64 B8G8R8A8 fusa sulla finestra A con l'alfa dei pixel (C in ALPHA_WEIGHT,
      A in DEPENDENT_WEIGHT); ritaglio ai bordi con gli offset di lettura; finestre scelte
      con il registro header. `hardware_cursor false` nelle impostazioni del driver
      `tegra_dc` torna al cursore software. Provato: un solo cursore, trasparenza corretta.
- [x] Fase 3: overlay video sulla finestra B (25/10, commit `9b3ff7cb28`, `a3ae77c584`,
  `4bc9f8d9ad`, provata con overlaytest su hrev 534). Buffer contigui WC allocati dal driver e clonati in app_server;
  finestra B con YCbCr 4:2:2 (UYVY + SWAP2 = YUYV), CSC BT.601, filtri lineari H/V,
  color key 0 sulla finestra A. app_server passa al client area e offset del buffer:
  BBitmap::Bits() ne clona l'area (prima il client riceveva un indirizzo valido solo in
  app_server). Test: `tests/overlay/overlaytest.cpp`, poi MediaPlayer.
- [~] Fase 4: HDMI (26/10, commit locali `3b8644425c`..`ad3ba92b63`). Specchio funzionante: hotplug
  (GPIO N7), LDO7/LDO8, EDID su I2C2, PLL_D = 2x pclk, DC B scala il frame buffer (pillarbox),
  cursore (finestra C) e overlay (finestra B) anche sulla TV; 1080p stabile su TV LG 4K, HDMI e
  DVI. Audio HDMI **pulito** (26/10, `48efa5ff5b`, `6fcfa4053f`): tegra_audio duplica su S/PDIF e rende
  un buffer al media server solo quando l'hanno finito sia I2S sia S/PDIF (prima S/PDIF leggeva
  campioni già sovrascritti: suono metallico); ACR 44,1 kHz, sorgente audio automatica; il mirror
  usa 720p (a 1080p il collegamento era al limite); altoparlante muto con HDMI collegato.
  Da fare: solo-esterno/esteso (richiede app_server).
- [~] Fase 5: host1x e motore 2D (26/10, `3f1389183b`). `tegra_gr2d` (bring-up, /dev/misc):
  canale host1x 7 + sync point 25, riempimento e copia corretti; 512x300x32: fill 0,29 ms,
  copia 0,68 ms (CPU 0,78 / 3,1 ms su memoria WC). Trappola: DMAPUT/GET/END sono offset da
  DMASTART e DMAINITGET copia DMAPUT in DMAGET (con indirizzi assoluti il DMA legge al doppio
  dell'indirizzo -> decode error MC di host1xdmar). Per usarlo davvero servono buffer visti dalla
  GPU: o il back buffer di app_server nella carveout (ma la CPU ci disegna male, è WC), o la GART
  (apertura 32 MB a 0x58000000) per mappare pagine sparse. app_server non ha più hook 2D.
- [ ] Fase 6: 3D (GR3D): accensione e test, poi driver Gallium (fork di grate).

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
