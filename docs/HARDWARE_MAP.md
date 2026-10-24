# Toshiba AC100 (Compal PAZ00): mappa hardware per il porting di Haiku

Fonti principali, tutte in `upstream/` (versioni in `upstream/VERSIONS.txt`):

- `upstream/linux/arch/arm/boot/dts/nvidia/tegra20-paz00.dts`: descrizione della board
- `upstream/linux/arch/arm/boot/dts/nvidia/tegra20.dtsi`: descrizione del SoC
- `upstream/linux/drivers/staging/nvec/`: Embedded Controller (tastiera, touchpad, batteria)
- `upstream/u-boot/configs/paz00_defconfig`: configurazione U-Boot della board
- `upstream/haiku/`: port ARM di Haiku (kernel arch, boot loader EFI e U-Boot)

Legenda: **[DTS]** verificato nel device tree · **[SRC]** verificato nei sorgenti ·
**(?)** da memoria o da verificare sull'hardware.

---

## 1. SoC: NVIDIA Tegra 2 (T20 / AP20H)

| Voce | Valore | Note |
|---|---|---|
| CPU | 2× Cortex-A9 MPCore, 1 GHz | `compatible = "arm,cortex-a9"` [DTS] |
| FPU | **VFPv3-D16, senza NEON** | caratteristica nota di Tegra 2, vedi §6.1 |
| Cache L2 | PL310 @ `0x50043000` | [DTS]; U-Boot paz00 ha `CONFIG_SYS_L2CACHE_OFF=y` [SRC] |
| GIC | **GICv1** (`arm,cortex-a9-gic`): distributor `0x50041000`, CPU interface `0x50040100` | [DTS] |
| Private timer A9 (TWD) | `0x50040600`, `arm,cortex-a9-twd-timer` | [DTS] |
| Legacy ICTLR | `0x60004000` | serve solo per wake e suspend; gli IRQ normali passano dal GIC [SRC irq-tegra.c] |
| Timer Tegra | `0x60005000`: TMR1-4 e TIMERUS (contatore a 1 µs, `+0x10`) | IRQ SPI 0, 1, 41, 42 [DTS/SRC timer-tegra.c] |
| CAR (clock & reset) | `0x60006000` | |
| Flow controller | `0x60007000` | serve per avviare la CPU1 (vedi `mach-tegra/platsmp.c`) |
| Exception vectors / EVP | `0x6000F000`; reset vector CPU1 a `+0x100` | [SRC reset.c] |
| GPIO | `0x6000d000`, porte A..X×8 | [DTS] |
| Pinmux | `0x70000014` | |
| PMC | `0x7000e400` | |
| MC / EMC | `0x7000f000` / `0x7000f400` | tabelle EMC per RAM-code 0 e 1 nel DTS |
| host1x / display | host1x `0x50000000`, DC A `0x54200000`, HDMI `0x54280000` | |

### Mappa periferiche (base fisiche)

| Periferica | Base | Uso sull'AC100 |
|---|---|---|
| UARTA | `0x70006000` | `serial0`, console 115200n8 (pin `irrx/irtx`) [DTS] |
| UARTC | `0x70006200` | `serial1` (pin `uca/ucb`) [DTS] |
| PWM | `0x7000a000` | canale 0 = retroilluminazione [DTS] |
| I2C1 | `0x7000c000` @ 400 kHz | codec ALC5632 `0x1e` + DDC del pannello [DTS] |
| I2C2 | `0x7000c400` @ 100 kHz | DDC HDMI [DTS] |
| I2C3 | `0x7000c500` | **NVEC, Tegra in modalità I²C slave** [DTS] |
| DVC I2C (PWR) | `0x7000d000` @ 400 kHz | PMIC TPS6586x `0x34` + sensore temperatura ADT7461 `0x4c` [DTS] |
| SDMMC1 | `0xc8000000` | microSD, 4 bit (CD=V5, WP=H1, PWR=V1) [DTS] |
| SDMMC4 | `0xc8000600` | **eMMC**, 8 bit, non rimovibile [DTS] |
| USB1 | `0xc5000000` | OTG / mini-USB, in Linux configurato come `peripheral` [DTS] |
| USB2 | `0xc5004000` | PHY ULPI, reset su GPIO V0 [DTS]; probabilmente le periferiche interne (?) |
| USB3 | `0xc5008000` | PHY UTMI [DTS]; probabilmente la porta USB-A esterna (?) |
| I2S1 / SPDIF | `0x70002800` / `0x70002400` | audio [DTS] |

---

## 2. Memoria e storage

- **RAM**: 512 MB DDR2 a `0x00000000-0x1FFFFFFF` (`memory@0`, `0x20000000`) [DTS].
- **eMMC** su SDMMC4: 8, 16 o 32 GB secondo il modello (?). U-Boot tiene l'ambiente nella
  partizione hardware *boot2* dell'eMMC (`CONFIG_ENV_MMC_EMMC_HW_PARTITION=2`) [SRC].
- **microSD** su SDMMC1, comoda per le prime prove di boot.

## 3. Alimentazione: PMIC TI TPS6586x (I²C DVC, `0x34`)

| Rail | Tensione | Uso |
|---|---|---|
| SM0 | 0.95–1.3 V | `vdd_core`, accoppiato con RTC e CPU |
| SM1 | 0.75–1.1 V | `vdd_cpu` |
| SM2 | 3.7 V | ingresso degli LDO |
| LDO1 | 1.1 V | PLL |
| LDO2 | 0.95–1.3 V | `vdd_rtc` |
| LDO3 | 3.3 V | `avdd_usb` |
| LDO4 | 1.8 V | `vddio_sys`, oscillatore |
| LDO5 | 2.85 V | `vcore_mmc` |
| LDO7 / LDO8 | 3.3 / 1.8 V | HDMI / HDMI PLL (non sempre accesi) |
| LDO9 | 2.85 V | `vdd_ddr_rx` |

Il PMIC contiene anche un RTC (`rtc0`). U-Boot accende già i rail necessari, quindi nelle
prime fasi Haiku **non deve gestire il PMIC**. Il DVFS (scalare la frequenza della CPU) può
aspettare.

## 4. Periferiche della board

### 4.1 Display

- Pannello **Samsung LTN101NT05**, 10.1" a 1024×600 [DTS], collegato alla **parallel RGB**
  del display controller A (pin `ld0..ld17`, funzione `displaya`). Il bridge RGB→LVDS non è
  descritto nel DTS (?).
- Enable del pannello: GPIO **M6**. Rail `vdd_pnl`: GPIO **A4**.
- Retroilluminazione: PWM canale 0 (periodo 5 ms) + enable su GPIO **U4**.
- HDMI: DDC su I2C2, hot-plug detect su GPIO **N7**.
- U-Boot paz00 ha `CONFIG_VIDEO_TEGRA=y` [SRC], quindi inizializza già il pannello.
  **Strategia per Haiku**: riusare il framebuffer di U-Boot tramite EFI GOP, poi
  `app_server` con un driver framebuffer generico. Non serve un driver Tegra DC
  all'inizio.

### 4.2 Embedded Controller: protocollo NVEC

È il componente più specifico dell'AC100. **Tastiera, touchpad, batteria, power button,
switch del coperchio, LED e spegnimento passano tutti dall'EC.**

- Bus: I2C3 `0x7000c500`. **Il Tegra fa da I²C slave**: indirizzo `slave-addr = 138`
  (`0x8A` a 8 bit, `0x45` a 7 bit) a 80 kHz [DTS].
- Per avviare una richiesta verso l'EC si abbassa la linea GPIO **V2**
  (`request-gpios`) [DTS]; poi è l'EC a leggere il messaggio dal Tegra.
- Registri slave usati: `I2C_SL_CNFG 0x20`, `I2C_SL_RCVD 0x24`, `I2C_SL_STATUS 0x28`,
  `I2C_SL_ADDR1/2 0x2c/0x30`, `I2C_SL_DELAY_COUNT 0x3c` [SRC nvec.c].
- La macchina a stati dell'interrupt (`nvec_interrupt()` in `nvec.c`) ha 5 stati:
  start → byte di comando → primo byte → TX (block read dell'EC) / RX (write dell'EC).
  **Bug hardware di AP20**: prima dell'ACK del primo byte trasmesso serve `udelay(33)`.
- Messaggi: massimo 34 byte. Tipi: `SYS=1, BAT=2, GPIO=3, SLEEP=4, KBD=5, PS2=6, CNTL=7,
  OEM0=0x0d`; eventi asincroni `KB_EVT=0x80`, `PS2_EVT=0x81` [SRC nvec.h].
- Sequenza di init in Linux: `GET_FIRMWARE_VERSION`, unmute degli speaker
  (`OEM0 10 59 95`), abilitazione degli eventi globali (lid, power button).
- Tastiera (`nvec_kbd.c`): `KBD ENABLE(0xf4)`, LED con `0xed`; scancode nella tabella
  `nvec-keytable.h`.
- Touchpad (`nvec_ps2.c`): comandi PS/2 incapsulati (`0xf4` enable, `0xff` reset),
  pacchetti da 6 byte. Si può collegare al driver PS/2 mouse di Haiku.
- Spegnimento: `{NVEC_SLEEP, AP_PWR_DOWN}`.

> Il protocollo non ha documentazione pubblica: `nvec.c` (971 righe) è l'unica fonte.
> In Haiku servirà un **bus/driver "nvec"** con sotto-driver keyboard, mouse e battery.
> È il primo driver davvero nuovo da scrivere: senza l'EC la tastiera integrata non
> funziona, quindi nel frattempo serve una tastiera USB.

### 4.3 Audio

Codec **Realtek ALC5632** su I2C1 `0x1e`, controller I2S1, MCLK da `cdev1`
(`pll_a_out0`), rilevamento cuffie su GPIO **W2** [DTS]. Ha bassa priorità.

### 4.4 Wi-Fi, LED, tasti

- RF-kill del Wi-Fi: reset GPIO **D1**, shutdown GPIO **K5**. LED Wi-Fi: GPIO **D0** [DTS].
- Il chip Wi-Fi è un Atheros AR9271 su USB interno (`ath9k_htc`) (?), quindi in Haiku
  passerebbe dallo stack USB più un driver wifi FreeBSD-compat.
- Tasto wakeup: GPIO **J7** (attivo basso) [DTS].
- Sensore termico ADT7461 `0x4c`, interrupt su GPIO **N6**. Trip a 80 °C (throttle) e
  85 °C (spegnimento) [DTS].

---

## 5. Catena di boot

1. BootROM Tegra → BCT + bootloader dall'eMMC. In **recovery/APX** (USB) si può scrivere
   con `nvflash` o `tegrarcm` (?: la procedura per l'AC100 è nei wiki della community).
2. **U-Boot mainline** (`paz00_defconfig`, `TEXT_BASE 0x00110000`, SPL `0x00108000`) [SRC].
3. U-Boot → `bootefi` → **`haiku_loader.efi` (ARM32)** con il DTB.
   Il loader EFI ARM di Haiku esiste già (`src/system/boot/platform/efi/arch/arm/`).
4. `haiku_loader` legge il DTB e sceglie UART, interrupt controller e timer, poi avvia
   il kernel.

Il percorso via EFI è quello consigliato. Il vecchio `platform/u-boot` di Haiku (uImage) è
ormai mantenuto solo per PPC.

---

## 6. Gap tra Haiku ARM e AC100 (verificati nei sorgenti)

### 6.1 FPU: VFPv3-D32 contro VFPv3-D16 (**bloccante**)
`build/scripts/build_cross_tools_gcc4` configura gcc e binutils con
`--with-cpu=cortex-a8 --with-fpu=vfpv3`, cioè **32 registri D**. Tegra 2 ne ha solo 16:
qualunque codice che usa `d16`-`d31` genera un'*undefined instruction*.
**Il kernel è colpito direttamente**: `arm_save_fpu`/`arm_restore_fpu`
(`kernel/arch/arm/arch_asm.S:78-93`) eseguono sempre `fstmiad/fldmiad {d16-d31}` e vengono
chiamate a ogni context switch (`arch_thread.cpp:162`), quindi il crash arriva al primo
cambio di thread. Correzione: leggere `MVFR0[3:0]` all'avvio (1 = 16 registri, 2 = 32) e
saltare `d16-d31` quando mancano.
→ Ricompilare i cross-tools (e tutti i pacchetti) con **`--with-fpu=vfpv3-d16`**, oppure
aggiungere `-mfpu=vfpv3-d16` in `ArchitectureRules`. Anche i pacchetti HaikuPorts ARM
precompilati vanno ricompilati.

### 6.2 Interrupt controller: GICv1 non gestito dal kernel (**bloccante**)
Il loader EFI riconosce `arm,cortex-a9-gic` come `INTC_KIND_GICV1`
(`efi/arch/arm/arch_dtb.cpp:26`), ma `arch_int_init_post_vm()` nel kernel
(`kernel/arch/arm/arch_int.cpp:156`) istanzia solo `INTC_KIND_GICV2`, quindi va in
`panic("No interrupt controllers found!")`.
→ Accettare anche `GICV1` nel kernel usando `GICv2InterruptController`: i registri base
(distributor e CPU interface) sono compatibili.

### 6.3 Timer: Cortex-A9 non ha l'ARM Generic Timer (**bloccante**)
`arch_init_timer()` cerca prima `ARMGenericTimer`, che esiste solo da Cortex-A7/A15, poi
OMAP3 e PXA, altrimenti va in `panic("No hardware timer found!")`. Il loader conosce solo
`arm,armv7-timer`, `ti,omap3430-timer` e `marvell,pxa-timers`.
→ Scrivere un `TegraTimer` (`soc_tegra.cpp`) basato su `nvidia,tegra20-timer`
@ `0x60005000`: TIMERUS (`+0x10`) come contatore a 1 MHz per `system_time()` e TMR1
(`+0x00`, IRQ SPI 0) one-shot per `SetTimeout()`. In alternativa si può usare il global
timer + TWD dell'A9, che sono per-CPU e quindi più utili per lo SMP.

### 6.4 UART: compatible non riconosciuto (facile)
Il nodo è `compatible = "nvidia,tegra20-uart"` con `reg-shift = <2>`. Haiku ARM accede
già all'8250 con passo di 4 byte (`DebugUART::Out8`), quindi basta aggiungere
`{ "nvidia,tegra20-uart", UART_KIND_8250, ... }` a `kSupportedUarts`
(`efi/dtb.cpp:74`). Il nodo non ha `clock-frequency`: o lo si aggiunge al DTB
(clock UART da PLLP = 216 MHz (?)), oppure si lascia il divisore impostato da U-Boot.
Dove raggiungere fisicamente UARTA sulla scheda madre resta da verificare (pad di test?) (?).

### 6.5 SMP (rimandabile)
Il loader EFI ARM forza `num_cpus = 1` (`efi/arch/arm/arch_smp.cpp:58`). Si può partire con
una sola CPU. Per la seconda servono il reset vector EVP, il clock CPU1 in CAR e il flow
controller (vedi `linux/arch/arm/mach-tegra/platsmp.c`).

### 6.6 Driver da scrivere dopo, in ordine di priorità

1. **SDHCI Tegra** per eMMC e SD: c'è il driver SDHCI generico di Haiku, ma Tegra ha quirk
   propri (`sdhci-tegra.c`) e serve un attachment FDT invece di PCI.
2. **EHCI Tegra** (USB): EHCI standard con PHY specifico (`phy-tegra-usb.c`). Dà tastiera,
   rete e storage USB.
3. **NVEC** (tastiera, touchpad, batteria), vedi §4.2.
4. GPIO, I²C, PWM Tegra (retroilluminazione).
5. Audio ALC5632 + I2S, Wi-Fi, HDMI, accelerazione 2D/3D: molto più avanti.

---

## 7. Roadmap

La roadmap dettagliata, con fasi e criteri di completamento, è in [ROADMAP.md](ROADMAP.md).
