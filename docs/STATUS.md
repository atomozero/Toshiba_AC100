# Stato del porting: diario dei lavori

## 2026-10-18: primi boot sull'hardware

Branch di lavoro: `ac100-markers` (contiene anche la strumentazione "not for merging").

### Sull'AC100
- Il kernel parte dalla microSD, monta la BFS e carica i driver (log 49, foto del 18/10).
- **Userland**: la `libstdc++` precompilata esegue `vldr d16` nel costruttore statico, quindi
  ogni programma C++ riceve SIGILL sul Tegra 2. Anche `libgcc_s`, `freetype`, `zlib`, `zstd`,
  `coreutils`, `bash`, `ncurses` e ICU 57 usano d16-d31. Le immagini ora usano le versioni D16 del
  bootstrap build (e `zstd` ricompilato); **ICU 57 resta D32** (nessuna build D16 per ora).
- **USB**: un dispositivo full-speed dietro un hub high-speed riceve STALL sul SETUP di
  `SET_ADDRESS` (qTD `0x00080e40`). Causa non trovata; c'è un dump del QH per la prossima prova.
- **eMMC (SDMMC4)**: non risponde all'inizializzazione.

### Desktop Haiku in QEMU (immagine 64): `out/qemu-desktop-64.png`
Dopo la correzione del TLB, Tracker, Deskbar e input_server restavano bloccati in un page
fault su pagine `busy_io` di librerie in packagefs. Causa: negli unmap ARM il flag
"modified" era invertito (AP[2] acceso = pagina pulita), quindi le pagine pulite dei file in
sola lettura venivano messe in coda per la scrittura, che in packagefs non termina mai.
Corretto in `ARMVMTranslationMap32Bit` (UnmapPage/UnmapPages). Ora il desktop completo parte.

### Il desktop non partiva: TLB utente mai invalidato (corretto, immagine 62)
- Sull'AC100 (immagini 59-60) lo userland si fermava prima di `app_server`: l'elenco dei
  thread (nuovo strumento, a 90 e 180 s dall'avvio) mostrava `launch_daemon baron` fermo su
  `sh -c . SetupEnvironment` e sotto-shell bloccate o in crash.
- `tests/forktest` (lanciato all'avvio, risultati nel log) ha mostrato che dopo `fork()` il
  figlio riprendeva con lo stack del padre (`fork()` restituiva il PID invece di 0).
- Causa: su ARM `active_on_cpus` delle strutture di paginazione non veniva mai impostato,
  quindi `Flush()` non invalidava mai il TLB delle pagine utente; il padre continuava a
  scrivere sulle pagine condivise copy-on-write. Corretto in `arch_thread_context_switch`
  (come x86). In QEMU ora partono app_server, Tracker e Deskbar.

### Log dell'immagine 58 sull'AC100 (`out/AC100LOG-hw.TXT`)
- Il log viene scritto sulla SD (62 scritture): scritture SD funzionanti.
- **Lo userland parte**: registrar, package_daemon (first boot processing di tutti i pacchetti),
  stack di rete, launch_daemon; nessun crash registrato.
- **NVEC funziona**: `EC firmware version 01.20.00 / f9`, tutti i comandi confermati (tastiera,
  touchpad `fa`).
- **eMMC**: inizializzata (`MMC CMD1: replied, OCR 0xc0ff8080`), ma `mmc_disk` rifiutava le
  schede MMC → corretto (immagine 59).
- USB: hub SMSC `0424:2513`, porta 2 con un dispositivo full speed che va in STALL (QH corretto
  secondo il dump); la tastiera Apple `05ac:0221` dietro il suo hub `05ac:1006` viene enumerata.
- Flood di `sdhci: Remaining interrupts at end of handler: 8` (interrupt DMA) → corretto.

### Correzioni
- `sdhci`: errori di dati senza interrupt → attesa infinita (ora timeout e reset); scritture
  `TRANSFER_MODE`+`COMMAND` in un solo accesso a 32 bit su Tegra; registro degli interrupt
  cancellato con `|=` (perdeva "transfer complete"); eventi di card detect ignorati quando la
  scheda è forzata presente (prima facevano ripartire l'inizializzazione della SD in uso).
- `mmc_bus`: nessuna nuova scansione di una scheda già in uso; CMD55 fallito non più stampato
  con un valore non inizializzato.
- `ehci`: parametri del transceiver UTMI di Tegra 2, soglia `TXFILLTUNING`, porta nel QH per
  il TT integrato (come Linux).
- Log: `AC100LOG.TXT` preallocato nella partizione EFI e scritto direttamente nei suoi
  cluster ogni 10 righe; stato ogni 15 s a schermo.

### Nuovi componenti (da provare sull'hardware)
- **Riavvio**: il loader passa al kernel i registri del PMC (`nvidia,tegra20-pmc`) e
  `arch_cpu_shutdown(true)` resetta il chip (`PMC_CNTRL.MAIN_RST`). Prima un riavvio bloccava
  la macchina.
- **NVEC** (`src/add-ons/kernel/busses/i2c/nvec`): controller I2C3 in modalità slave
  (clock, reset, pinmux `dtf` e GPIO V2 di richiesta impostati dal driver), protocollo di
  `drivers/staging/nvec`. Pubblica `input/keyboard/nvec/0` (scancode set 1 → `kATKeycodeMap`,
  ripetizione software) e `input/mouse/nvec/0` (pacchetti PS/2 a 3 byte), e spegne la macchina
  (`gArmPowerOffHook`). Stampa la versione del firmware dell'EC e i messaggi non gestiti
  (`nvec: ...`). Test sul PC: `tests/nvec/run.sh`.
- **Batteria e alimentatore** (NVEC): interrogati ogni 5 s e pubblicati come
  `power/nvec_battery/0` con l'interfaccia della batteria ACPI, quindi visibili in PowerStatus.
- **Ora di sistema**: su ARM l'offset dell'ora non veniva mai memorizzato (l'ora era sempre
  il tempo dall'avvio, anche dopo averla impostata); ora sì. Su Tegra il kernel legge e scrive
  l'RTC `nvidia,tegra20-rtc` (registri passati dal loader).
- **ICU 57.2 D16** ricompilata da sorgente (`~/haiku-arm/icu57`), stessi soname e simboli;
  `libicudata` originale. Resta D32 solo `libicutest` (non usata).

### Immagini
`scripts/make-ac100-image.sh out/ac100-sd-markersN.img` (dopo `jam -q -j8 @minimum-mmc`).

## 2026-10-17: prima sessione

### Risultato principale

**Haiku ARM32, compilato con la toolchain per Tegra 2 (VFPv3-D16) e con le nostre patch,
arriva fino allo userland in QEMU** (`virt`, Cortex-A15): loader EFI → kernel → VM,
scheduler, bus FDT, PCI, virtio → `launch_daemon` e server → CPU in idle.

Prima delle correzioni il boot upstream si fermava due volte:
1. **data abort nel loader** subito prima di saltare nel kernel;
2. **loop infinito di abort nel kernel** subito dopo l'avvio.

Entrambi i bug sono di Haiku ARM in generale, non specifici dell'AC100 (dettagli più sotto).

**Aggiornamento in giornata:** dopo altre 5 correzioni (patch `0004`-`0008`) il kernel
completa il boot: lo **splash di Haiku arriva all'ultima icona** (`out/qemu-bootsplash.png`)
e lo userland parte senza `mutex was not actually locked!`, senza `abort()` e senza panic.
Restano due *segment violation* (`package_daemon` durante un'eccezione C++ e `sh`), vedi
"Problemi noti". Lo schermo poi diventa bianco: probabilmente è `app_server`, che non va in
crash, ma non è ancora confermato.

### Ambiente (tutto senza root)

| Cosa | Dove |
|---|---|
| Sorgenti Haiku (branch `ac100`) | `~/haiku-arm/haiku` |
| Buildtools | `~/haiku-arm/buildtools` |
| Toolchain `arm-unknown-haiku` GCC 13.3, `-mfpu=vfpv3-d16` | `~/haiku-arm/haiku/generated.arm/cross-tools-arm` |
| U-Boot (`qemu_arm`, `vexpress_ca9x4`, `paz00`) | `~/haiku-arm/build-uboot-*` |
| Tool host estratti da .deb (QEMU 7.2, gawk, dtc, mkimage, swig, gcc ARM) | `~/haiku-arm/sysroot`, attivare con `source ~/haiku-arm/env.sh` |
| Log di boot QEMU | `~/haiku-arm/test` |
| Script di test (`qemu-virt.sh`; `probe.sh` che campiona il PC di una CPU bloccata e fa screenshot con `SHOT=file.ppm`; `dtbtest.py` che fa leggere al loader un DTB arbitrario; `bfsget.sh` che estrae file da una partizione BFS) | `scripts/` |

Comandi principali:
```sh
source ~/haiku-arm/env.sh
cd ~/haiku-arm/haiku/generated.arm && jam -q -j8 @minimum-mmc   # → haiku-mmc.image
/mnt/d/Sorgenti/ac100/scripts/qemu-virt.sh                      # boot in QEMU
```

### Patch (branch `ac100`, copie in `patches/`)

1. **`0001` Supporto Tegra 2**
   - toolchain con `--with-fpu=vfpv3-d16`;
   - `arm_save_fpu`/`arm_restore_fpu` salvano `d16-d31` solo se `MVFR0` indica 32
     registri (`gARMHasVFPD32`);
   - il `memcpy` ARM di libroot e del kernel usava **NEON**, che il Tegra 2 non ha: sostituito
     con `generic_memcpy.c` più gli alias `__aeabi_memcpy*`;
   - il kernel accetta il **GICv1** del Cortex-A9;
   - nuovo driver **`TegraTimer`** (`soc_tegra.cpp`): TIMERUS a 1 MHz come orologio, TMR1
     one-shot, TMR2 periodico per non perdere il wrap a 32 bit;
   - UART `nvidia,tegra20-uart` riconosciuta. Se il DTB non indica il clock, il loader non
     reimposta il baud rate e lascia quello di U-Boot.
2. **`0002` libgcc del kernel**: la `libgcc-kernel.a` precompilata (pacchetto
   `gcc_syslibs_devel`) usa `d16-d31`, per esempio in `__aeabi_d2ulz`, e su Tegra 2 andrebbe
   in crash. Il kernel ARM ora si linka con la `libgcc.a` della toolchain D16. Contiene anche
   una correzione al build host (`RPattern.cpp`: manca `<stddef.h>`).
3. **`0003` Due bug di boot di Haiku ARM (valgono per tutte le board)**
   - loader EFI: il commit upstream `97675f2f` (dic. 2025) faceva usare al loader
     l'indirizzo *virtuale* della UART mentre girava ancora con la mappa di U-Boot, quindi
     data abort al primo `dprintf`;
   - kernel: gli stack delle modalità eccezione in `arch_exceptions.S` non erano allineati
     e finivano a indirizzi dispari, quindi il primo abort rientrava all'infinito su `stmdb`.

4. **`0004` CAS *strong* in `SupportDefs.h`**: `atomic_test_and_set()` usava
   `__atomic_compare_exchange_n(..., weak=1, ...)` e restituiva il valore atteso. Su ARM una
   `strex` che fallisce spuriamente sembrava un successo, quindi `__mutex_lock()` credeva di
   avere il lock senza averlo scritto. Era la vera causa di "mutex was not actually locked!"
   (verificato nel disassemblato: il risultato della `strex` non veniva controllato). Vale
   anche per ARM64; x86 non è colpito.
5. **`0005` TLB e atomiche nel kernel ARM**
   - `arch_cpu_user_tlb_invalidate()` era **vuota** (`#warning WRITEME`): sopra le 64 pagine
     modificate non veniva invalidato nulla;
   - atomiche a 32 bit del kernel con `ldrex`/`strex` su ARMv7 invece di disabilitare gli
     interrupt (un page fault su memoria utente con interrupt disabilitati non si risolve);
   - `clrex` a ogni cambio di contesto.
6. **`0006` Accessi alla commpage**: `arch_thread_enter_userspace()` leggeva la commpage dopo
   `disable_interrupts()` → panic "page fault, but interrupts were disabled"; in
   `arch_setup_signal_frame()` la `user_memcpy` stava dentro `ASSERT()` (sparisce senza
   KDEBUG).
7. **`0007` `fenv` ARM**: l'header era dell'era FPA, con `fegetround()` che restituiva sempre
   -1 e `__rfs`/`__wfs` vuoti. `printf("%f")` → `round_away()` → `abort()` (era il crash di
   `package_daemon`). Ora legge e scrive FPSCR con `vmrs`/`vmsr`.
8. **`0008` `system_time()` nello userland**: era un segnaposto che restituiva un contatore
   incrementato di 1 a ogni chiamata, quindi timeout e `snooze()` erano tutti sbagliati.
   Ora usa la syscall `_kern_system_time()`, perché il Cortex-A9 non ha un contatore
   leggibile dallo userland. Con questa correzione spariscono gli errori di `consoled`.
9. **buildtools `0001`** (`patches/buildtools/`): in `unwind-arm.h` Haiku era trattato come
   piattaforma "TARGET2 assoluto", mentre il suo `ld` usa `got-rel`. Corretto nel sorgente,
   ma **ha effetto solo ricompilando `gcc_syslibs`** (libstdc++.so, libgcc_s.so), che oggi
   arrivano precompilati da HaikuPorts.

10. **`0009` Traduzione degli indirizzi DTB**: `dtb_get_reg()` applicava solo i `ranges`
    del genitore diretto. Su vexpress la UART finiva a `0x9000` invece di `0x10009000` e il
    loader si bloccava. Ora risale tutti i bus. Inoltre non si riprogramma la UART quando
    il clock vale -1 (sconosciuto).
11. **`0010` Driver TWD (timer privato del Cortex-A9)**: usato come ripiego quando il DTB
    non offre un timer migliore. La frequenza viene misurata dal loader con `Stall()` e
    passata in `arch_args.timerFrequency`. Sull'AC100 resta scelto il timer Tegra.

12. **`0011` Driver SD/eMMC per Tegra 2** (`sdhci_fdt.cpp`): attachment device tree per
    `nvidia,tegra20-sdhci` (solo nodi abilitati). Abilita, toglie dal reset e imposta a
    48 MHz il clock SDMMC tramite il CAR (trovato dal phandle `clocks`), con i quirk di
    Linux (versione forzata a 2.00, timeout massimo). Rilevamento scheda via GPIO non
    ancora supportato: si assume la scheda presente. Corregge anche un bug generico: il
    divisore del clock veniva arrotondato per difetto (scheda clockata oltre il richiesto).

13. **`0012` USB EHCI per Tegra 2**: costruttore EHCI separato tra parte PCI e parte
    comune; attachment device tree per `nvidia,tegra20-ehci` (registri EHCI a +0x100);
    modalità **TT integrato** (ChipIdea/TDI): tastiere e mouse full/low-speed restano
    all'EHCI invece di essere ceduti a un controller companion che il Tegra non ha,
    velocità della porta da PORTSC, `USBMODE` in modalità host dopo ogni reset. I PHY
    (UTMI/ULPI) per ora li inizializza U-Boot.

### Verifiche fatte

| Verifica | Esito |
|---|---|
| Toolchain di default `-mfpu=vfpv3-d16`, `libgcc.a` tutta `Tag_FP_arch: VFPv3-D16` | ✅ |
| Kernel e loader senza istruzioni su `d16-d31` (a parte il salvataggio FPU protetto) e senza NEON | ✅ (`objdump`) |
| Boot in QEMU `virt` fino allo userland | ✅ |
| Percorso FPU D16 forzato (`gARMHasVFPD32 = false`) in QEMU: il boot arriva allo stesso punto | ✅ |
| Test A/B con il vecchio memcpy NEON: stesso comportamento dello userland | ✅ quindi il problema dei mutex non veniva da noi (era il bug CAS, patch `0004`) |
| Boot finale (`hrev60260+7`): nessun `DEBUGGER`, `abort` o `PANIC` nel log seriale fino al caricamento dei driver | ✅ |
| Il loader analizza il **DTB dell'AC100** (in QEMU, con la sola UARTA rediretta sulla PL011; `scripts/dtbtest.py`): 2 CPU, `gicv1` @ `0x50041000`/`0x50040100`, timer `tegra20` @ `0x60005000` IRQ 32, poi il kernel parte | ✅ |
| **QEMU `vexpress-a9` (Cortex-A9, GICv1, TWD)**: loader → kernel → scheduler fino alla ricerca del volume di boot; TWD calibrato a ~100,25 MHz; dal KDL (`scripts/kdltest.py`) si vedono interrupt TWD gestiti e timer programmati | ✅ |
| Con il DTB dell'AC100 il loader sceglie ancora `tegra20`, non il TWD | ✅ |
| SDHCI, percorso PCI in QEMU (`-device sdhci-pci` + scheda SD): la scheda risponde e viene pubblicato `disk/mmc/0/raw`; a 25 MHz richiesti ora usa 13 MHz invece di 26 MHz | ✅ |
| SDHCI, percorso FDT/Tegra | ⏳ solo sull'hardware |
| EHCI, percorso PCI in QEMU (`usb-ehci` + chiavetta): `disk/usb/0/0/raw` pubblicato | ✅ |
| EHCI, percorso FDT/Tegra con TT integrato | ⏳ solo sull'hardware (QEMU non emula TT integrato per dispositivi full/low-speed) |
| Funzionamento reale di GICv1, timer Tegra, UART Tegra | ⏳ **solo sull'hardware**: QEMU non emula Tegra. In QEMU il Cortex-A15 ha il Generic Timer, che il kernel preferisce sempre, quindi `TegraTimer` non viene istanziato |
| QEMU 7.2 non permette di disattivare i registri D32 (`MVFR0` resta a 32 registri anche con `neon=off`) | ℹ️ |

### Problemi noti / prossimi passi

- **Eccezioni C++ nello userland ARM**: con `TRACE_FAULTS` attivo si vede che
  `package_daemon` (thread "job runner") salta a eseguire `typeinfo for std::exception`
  (`_ZTISt9exception`, nella libstdc++ precompilata) e `sh` salta a `0x80`. Sembra una
  chiamata virtuale con un vptr sfasato durante l'unwinding EHABI. Ho trovato
  un'incongruenza reale su TARGET2 (patch buildtools), ma **non ho dimostrato** che sia la
  causa: rilinkare il solo `package_daemon` con `--target2=abs` non ha cambiato nulla, forse
  perché il `catch` sta in un'altra libreria. Il passo successivo è il **bootstrap build**
  con `gcc_syslibs` ricompilato dalla toolchain corretta, che serve comunque anche per il
  D16.
- Il KDL stampa male i valori a 64 bit (`%lld`): la parte alta contiene il valore vero,
  quella bassa l'argomento successivo. Il parsing in `kernel_vsprintf.cpp` sembra corretto;
  da cercare nel percorso di output del KDL.
- Per lo schermo bianco in QEMU (`-device bochs-display,romfile=`) va verificato se
  `app_server` è attivo.
- **Pacchetti precompilati per ARM** (`gcc_syslibs`: libstdc++, libgcc_s…) sono compilati
  per D32: sull'AC100 potrebbero andare in crash se usano `d16-d31`. Servirà un build
  bootstrap o la ricompilazione dei pacchetti.
- **BCT dell'AC100**: per avviare U-Boot via USB (RCM) serve il BCT (configurazione della
  DRAM). Non è in nessun repository pubblico e va letto dall'eMMC del dispositivo, vedi
  [AC100_BOOT.md](AC100_BOOT.md).
- Il timer per QEMU `vexpress-a9` (SP804 o global timer A9) non è stato scritto: lo si può
  rinviare, perché l'AC100 usa il timer Tegra.

### Artefatti pronti (`out/`)

| File | Uso |
|---|---|
| `ac100-sd.image` | immagine per microSD: partizione EFI (`BOOTARM.EFI`, `tegra20-paz00.dtb`) + BFS |
| `u-boot-dtb-tegra.bin` | U-Boot `paz00` da avviare via RCM o da flashare |
| `u-boot.bin` | U-Boot senza DTB/BCT |
| `tegra20-paz00.dtb` | device tree Linux mainline dell'AC100 |
