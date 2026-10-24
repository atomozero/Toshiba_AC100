# Driver dell'AC100: stato e piano

Inventario fatto dal device tree `tegra20-paz00.dtb` e dai log dell'hardware (immagini 71–75),
aggiornato al 18 ottobre 2026.

## Funzionano

| Componente | Hardware | Driver Haiku |
|---|---|---|
| CPU core 0 | Cortex-A9 1 GHz, VFPv3-D16 | kernel ARM (con le correzioni di questo branch) |
| Timer, interrupt | TWD, GIC + ictlr Tegra | `arch_timer_a9_twd`, GIC |
| Cache L2 | PL310 | abilitata dal loader |
| Schermo | pannello Samsung LTN101NT05 1024x600, framebuffer di U-Boot | `framebuffer` (16 bit, nessuna accelerazione) |
| Scheda SD | SDHCI 0xc8000000 | `sdhci` + `mmc_disk` |
| eMMC interna 16 GB | SDHCI 0xc8000600 | `sdhci` + `mmc_disk` (`/dev/disk/mmc/0`) |
| USB host | EHCI 0xc5000000, 0xc5008000 | `ehci` (PHY UTMI, TT integrato) |
| Tastiera, touchpad | NVEC (EC su I2C3 slave) | `nvec` |
| Batteria, alimentatore | NVEC | `nvec` → `power/nvec_battery/0` (PowerStatus) |
| Spegnimento | NVEC | `nvec` (`gArmPowerOffHook`) |
| Riavvio | PMC | `arch_cpu_shutdown` |

## Aggiunti oggi, da verificare sull'hardware (immagine 82)

| Componente | Cosa fa | Cosa guardare nel log |
|---|---|---|
| Orologio con batteria tampone | TPS6586x via I2C DVC (0x7000d000, 0x34): al boot imposta l'orologio del Tegra, quando cambi l'ora la scrive anche nel TPS | `AC100: TPS6586x clock: control ..., N seconds since 2009` |
| Tasto di accensione | evento NVEC `e0 5e` → `power/button/power_nvec` → power_daemon chiede lo spegnimento | premere il tasto: deve comparire la richiesta di spegnimento |
| Luminosità | tasti `e0 39` / `e0 3a` (retroilluminazione su/giù, Fn+F4/F5 circa) → duty del PWM Tegra canale 0, 16 livelli; solo se U-Boot ha lasciato il PWM con il clock attivo | `nvec: backlight PWM 0x..., level N` oppure `backlight PWM not running` |
| Secondo core (SMP) | loader: SCU, vettore EVP, flow controller e CAR come Linux; kernel: stack di eccezione, GIC e TWD per CPU, ICI. Provato in QEMU con 2 CPU (speedup 1,84) | `smp: 2 CPUs up`, poi `sysinfo` con 2 CPU. Se l'avvio si blocca: immagine `-1cpu` |
| Bus I2C1 e I2C2 | driver `tegra_i2c` + bus manager + comando `i2c` | `tegra_i2c: controller 0x7000c000 ...: No error`; da Terminal `i2c /dev/bus/i2c/0/bus_raw` deve trovare il codec 0x1e |
| Alimentazione Wi-Fi | GPIO rfkill PD1 (reset) e PK5 (shutdown) portati alti dal loader | `radio: GPIO 25 set high`, `radio: GPIO 85 set high`, poi un nuovo dispositivo USB al posto di "error while setting device address" |

Nota sull'ora: Haiku considera l'orologio hardware come ora locale, a meno di spuntare
"GMT" nelle preferenze Time. Linux/Android scrivono il TPS6586x in UTC: la prima volta
l'ora può risultare spostata del fuso; dopo averla impostata da Haiku resta corretta.

## Da fare, in ordine di utilità

1. **Luminosità nelle preferenze Screen** (piccolo, dopo la verifica dei tasti). Esporre
   il PWM all'accelerant del framebuffer (`B_SET_BRIGHTNESS`/`B_GET_BRIGHTNESS`). Se il
   log dice che il PWM non è attivo: abilitarne clock, pinmux e GPIO PU4 (164).
2. **Coperchio** (piccolo, ma oggi inutile). L'NVEC segnala `LID_SWITCH`; il power_daemon
   di Haiku però alla chiusura stampa solo un messaggio. Utile insieme alla luminosità
   (spegnere lo schermo a coperchio chiuso).
3. ~~Driver I2C master Tegra~~ fatto per I2C1/I2C2 (il DVC resta al kernel per l'orologio;
   per il sensore di temperatura andrà condiviso).
4. **Audio** (in prova dall'immagine 99): driver `tegra_audio` (multi_audio), solo
   riproduzione 16 bit stereo 44,1 kHz; all'avvio suona un breve tono di prova e scrive
   nel log clock, pin, DAS, registri del codec e quante interruzioni DMA ha visto.
   Prima idea: Catena: APB DMA (0x6000a000) → I2S1 (0x70002800) →
   DAS (0x70000c00) → codec Realtek ALC5632 su I2C1 (0x1e), clock dal CAR. Driver
   `hmulti_audio` in Haiku. Uscita casse/cuffie, microfono interno.
5. ~~Secondo core (SMP)~~ fatto e verificato sull'AC100 (immagine 82, 18 ottobre 2026).
6. **Wi-Fi Atheros AR9271** (molto grande). Dopo l'accensione via rfkill il modulo
   dovrebbe enumerarsi su USB, ma Haiku non ha un driver `ath9k_htc`: andrebbe portato
   (da OpenBSD `athn` o da Linux) con il firmware `htc_9271.fw`. In alternativa,
   nell'immediato: una chiavetta Wi-Fi USB già supportata da Haiku (Realtek/Ralink).
7. **Sensore di temperatura** ADT7461 (piccolo, dopo il punto 3).
8. **HDMI** (grande): seconda uscita del display controller + HDMI Tegra.
9. Non previsti: accelerazione 2D/3D (gr2d/gr3d), decodifica video (VDE), USB device
   mode (udc), S/PDIF.

## Da sapere

- **Fn + F9 accende e spegne il touchpad** nell'EC, che ricorda lo stato anche a macchina
  spenta: se il touchpad non risponde, premere Fn + F9. Il driver non lo può riattivare.

## Altri problemi aperti

- USB: un dispositivo full-speed dietro l'hub SMSC 0424:2513 va in STALL; da ricontrollare
  dopo l'accensione del Wi-Fi, potrebbe essere proprio il modulo.
- Devices andava in crash con un double free sull'hardware (immagine 71); in QEMU con le
  correzioni di segnali ed eccezioni non succede più, da ricontrollare.
