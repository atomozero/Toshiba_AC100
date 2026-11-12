# Toshiba AC100 (Tegra 2): consumi, risparmio energetico e altre scoperte

Questo documento raccoglie quello che abbiamo misurato e capito sull'AC100
(Compal PAZ00, NVIDIA Tegra 2 / T20) tra il 10 e l'11 novembre 2026, portando
Haiku/RenkuOS su questa macchina. È scritto per chi vuole studiare l'AC100 o
fare lo stesso lavoro su un altro sistema operativo: i numeri sono misure
vere, fatte sulla macchina, e per ogni affermazione è indicato da dove viene.

Fonti di riferimento usate (nella cartella `docs/` e `upstream/` del
progetto): Tegra 2 Technical Reference Manual (TRM, DP-04508-001_v01p),
schema elettrico Compal LA-6352P, device tree Linux `tegra20-paz00.dts`, e i
sorgenti Linux citati per nome.

---

## 1. Come misurare i consumi senza strumenti

L'AC100 non ha un sensore di potenza accessibile, ma l'**embedded controller
(EC, protocollo NVEC)** conosce la corrente della batteria.

- Richiesta NVEC classe `0x02` (batteria), sotto-comandi utili:
  `00` stato dello slot, `01` tensione (mV), `03` **corrente istantanea**
  (mA, con segno: negativa in scarica), `04` corrente media, `05` intervallo
  della media (60 s), `06` carica residua (mAh), `07` ultima carica piena,
  `08` capacità di progetto.
- Stato dello slot: bit 0 = batteria presente, bit 2:1 = 1 in carica,
  2 in scarica, 0 ferma; il secondo byte è la percentuale.
- **In carica l'EC riporta la corrente di ricarica** (circa 905 mA fissi),
  non il consumo del sistema: per misurare bisogna staccare il caricatore.
  Con batteria al 100% e caricatore attaccato la corrente è 0.
- La corrente istantanea ha risoluzione di 1 mA ed è molto stabile (±2 mA
  a riposo): bastano 30–60 s di campioni ogni 2 s per confrontare due
  configurazioni. Moltiplicata per la tensione dà la potenza.
- Conviene sempre fare **prove A/B ripetute** (A, B, A, B) e una misura
  finale uguale alla prima: la tensione della batteria scende e la scheda si
  scalda, e questo da solo sposta qualche decina di mA.
- Attenzione allo **screensaver**: se spegne lo schermo durante una prova
  falsa tutto (è successo a noi a metà di due misure).

Lo strumento che abbiamo scritto, `tests/ac100diag/ac100power.cpp`, fa tutto
questo (`watch`, `bench`, `floor`, `dpms`).

La batteria della nostra macchina: capacità di progetto 2250 mAh, ultima
carica piena 1399 mAh (62%, batteria usurata), pacco da circa 11 V
(3 celle in serie).

---

## 2. Dove vanno i watt

Misure a riposo (CPU idle al 99%), batteria, Wi-Fi associato, CPU a 1 GHz e
1,1 V, prima delle ottimizzazioni:

| Voce | Potenza |
|---|---|
| Totale a riposo, luminosità al 27% | ~4,65 W |
| Retroilluminazione al 27% | ~0,55 W (al massimo: ~2,2 W) |
| Pannello LCD acceso anche a retroilluminazione spenta | **~0,93 W** |
| Radio Wi-Fi (associata, inattiva) | ~0,18 W |
| CPU sotto carico, in più rispetto al riposo | +0,27 W un core, +0,64 W due core |
| Partizioni del SoC inutilizzate (3D, MPE, encoder, decoder video) | ~0,03–0,05 W |
| Codec audio ALC5632 acceso ma muto | ~0,07 W |
| Tensione CPU 1,1 V invece di 1,0 V, a riposo | ~0,15 W |
| Resto (SoC, DRAM, USB, EC, scheda) | ~3 W |

Conclusioni principali:

- **La CPU a riposo pesa poco.** Senza un modulo cpuidle il kernel esegue già
  `WFI` nel loop di idle, e anche al 100% su due core aggiunge solo 0,64 W.
  Gli stati di idle profondi del Tegra 2 (LP2, CPU spente) possono dare al
  massimo qualche decina di mW in più: non valgono il rischio.
- **Il pannello è la voce più grossa che si può spegnere**: quasi 1 W anche
  con la retroilluminazione a zero.
- Spegnere la seconda CPU (in Haiku `_kern_set_cpu_enabled`) **non** fa
  risparmiare: la CPU resta in WFI, non viene tolta l'alimentazione.

---

## 3. Lo schermo: il vero DPMS off (−1,5 W)

Il pannello è un Samsung LTN101NT05 (1024×600) su RGB parallelo → bridge LVDS
SN75LVDS83. Dal device tree:

- la retroilluminazione è il canale 0 del PWM (`0x7000a000`), con enable su
  GPIO U4;
- l'alimentazione del pannello è un `regulator-fixed` "+3VS,vdd_pnl" comandato
  dal **GPIO A4** (pin 4, attivo alto); sullo schema è `EN_VDD_PNL`, che
  tramite Q6/Q11 commuta `+LCD_VDD`;
- `enable-gpios` del pannello è M6 (LCD_D22), che però sull'AC100 è in modalità
  SFIO e non serve toccarlo.

Sequenza che funziona (driver `tegra_dc` di Haiku):

1. spegnere la retroilluminazione (PWM a zero);
2. disabilitare la finestra A del display controller (`WIN_OPTIONS`, bit 30),
   così il controller smette di leggere il framebuffer dalla memoria; il
   controller resta acceso e l'interrupt di vblank continua (i programmi che
   aspettano il retrace non si bloccano);
3. dopo un frame, GPIO A4 a 0.

Riaccensione: A4 a 1, 50 ms, finestra A riabilitata, altri 50 ms, poi la
retroilluminazione. Misura: **4,65 W → 3,14 W** (−1,5 W). Spegnimento in
37 ms, riaccensione in ~0,1 s. Il pannello si riaccende sempre correttamente.

Prima di questo il DPMS spegneva solo la retroilluminazione (−0,55 W).

---

## 4. Frequenza e tensione della CPU

### Punti di lavoro del chip

Linux (`tegra20-cpu-opp.dtsi` + `tegra20-cpu-opp-microvolt.dtsi`) dà una
tabella di frequenze con la tensione minima per ogni combinazione di
**CPU process** e **SoC speedo ID**, letti dai fusibili come in
`speedo-tegra20.c`. Per T20 la maschera `opp-supported-hw` è
`<BIT(cpu_process) BIT(soc_speedo_id)>`.

La nostra AC100: revisione A02, SKU 8, speedo ID 0, CPU speedo 352 →
**processo 1**. Punti di lavoro risultanti:

| MHz | 216 | 312 | 456 | 608 | 760 | 816 | 912 | 1000 |
|---|---|---|---|---|---|---|---|---|
| mV CPU | 750 | 750 | 825 | 900 | 975 | 1000 | 1050 | 1100 |

Tutti verificati stabili con un test di calcolo intero e in virgola mobile su
due CPU, confrontato col risultato di riferimento (`tests/ac100diag/cpucheck.c`).

Nota: il firmware (U-Boot paz00) lascia la CPU a 1 GHz con 1,0 V e il core a
1,2 V, valori di accensione del PMIC. La tabella chiede 1,1 V a 1 GHz per
questo chip: noi alziamo le tensioni all'avvio.

### Vincoli tra le tensioni

Da `drivers/soc/tegra/regulators-tegra20.c`: core e RTC devono stare almeno
120 mV sopra la CPU, e core e RTC non possono differire più di 170 mV. Tenendo
core e RTC a 1,225 V basta muovere solo la tensione della CPU (SM1 del PMIC
TPS6586x, indirizzo I²C 0x34 sul bus DVC, registro SM1V1 0x23, bit GO in VCC1).
Regola: **in salita prima la tensione poi la frequenza, in discesa il
contrario.**

### Cambiare la frequenza

- `CCLK_BURST_POLICY` (CAR `0x60006020`): bit 31:28 stato della CPU, poi una
  sorgente di 4 bit per stato; 4 = PLLP_OUT0 (216 MHz), 8 = PLLX_OUT0.
- Per riprogrammare PLLX (CAR `0xe0`): portare la CPU su PLLP, disabilitare
  PLLX, scrivere M = 12 (oscillatore 12 MHz), N = MHz, P = 1 sotto 600 MHz
  (VCO tenuto ≥ 600 MHz), abilitare, attendere il bit LOCK (27) e ~300 µs,
  tornare su PLLX. I parametri di PLLX_MISC (pompa di carica 12, LOCK_ENABLE)
  impostati dal firmware vanno bene per tutti i punti.
- A 216 MHz la CPU resta su PLLP e PLLX si spegne.

### La trappola del timer

Il timer privato del Cortex-A9 (TWD), che molti kernel usano per gli eventi
dello scheduler, **gira a frequenza CPU / 4** (Linux: `twd` = `cclk`/4).
Cambiando la frequenza della CPU, i timeout cambiano durata (a 216 MHz un
timeout dura 4,6 volte di più). Bisogna aggiornare la frequenza del timer e
riscalare il conto alla rovescia in corso su ogni CPU, come fa Linux con
`clockevents_update_freq`. Il contatore a 1 MHz TIMERUS (`0x60005010`), usato
come orologio, non è toccato.

### Il riavvio

Il riavvio via PMC (`MAIN_RST`) resetta il SoC ma **non il PMIC**: se il
sistema si riavvia mentre la CPU è a 0,75 V, il bootloader riparte a 1 GHz con
la tensione bassa. Prima del reset vanno rialzate le tensioni ai valori
nominali (Linux lo fa in `regulators-tegra20.c`).

### Risultati

A riposo, schermo acceso, batteria:

| Punto | Potenza |
|---|---|
| 1000 MHz / 1,1 V | 4,66 W |
| 760 MHz / 0,975 V | 4,48 W |
| 608 MHz / 0,9 V | 4,36 W |
| 456 MHz / 0,825 V | 4,31 W |
| 312 MHz / 0,75 V | 4,27 W |
| 216 MHz / 0,75 V | **4,23 W (−0,43 W)** |

Abbassare solo la frequenza a 1,1 V risparmia appena 0,1 W: il guadagno vero
viene dalla tensione.

### Governor (in Haiku)

Il modulo `power/cpufreq/tegra20_cpufreq` campiona il carico ogni 50 ms e
segue la modalità dello scheduler (bassa latenza / risparmio energetico di
ProcessController). Lo scheduler di Haiku chiama gli agganci cpufreq a ogni
cambio di contesto con gli interrupt disabilitati: lì si può solo annotare,
il lavoro (I²C del PMIC, PLL) va fatto in un thread. Un'idea che sfrutta
Haiku: negli agganci il thread uscente è ancora quello corrente, quindi si
può notare a costo quasi nullo quando girano thread utente a priorità tempo
reale (audio, video, input) e tenere una frequenza minima alta per non far
scattare la riproduzione. Serve però un minimo di attività (3 esecuzioni ogni
50 ms): un thread periodico isolato, come un orologio multimediale, non deve
bastare. Proprio così abbiamo scoperto un thread dell'add-on audio Bluetooth
che ripubblicava l'orario ogni 50 ms a priorità 110, anche nel silenzio.
Toglierlo del tutto ha rotto l'audio (il nodo non riceveva più buffer dal
mixer, causa non ancora capita): ora pubblica ogni 500 ms a priorità 20, e a
riposo la CPU sta a 456 MHz / 0,825 V.

---

## 5. Altre parti del SoC

- **Power gating**: all'avvio `PMC PWRGATE_STATUS` (`0x7000e438`) vale `0x7f`,
  tutte le partizioni accese (CPU, 3D, VE, PCIe, VDE, L2, MPE). Sequenza di
  spegnimento: accendere i clock del blocco, metterlo in reset (CAR
  `RST_DEV_x_SET`), spegnere i clock, scrivere `0x100 | id` in
  `PWRGATE_TOGGLE` (`0x7000e430`). ID: 1 3D, 2 VE, 3 PCIe, 4 VDE, 6 MPE. Per
  riaccendere: toggle, clock, `REMOVE_CLAMPING` (`0x7000e434`), rilascio del
  reset. **Attenzione: nei bit di REMOVE_CLAMPING VDE e PCIe sono invertiti**
  rispetto agli ID (VDE bit 3, PCIe bit 4, come dice il TRM). Risparmio
  misurato: solo 25–50 mW.
- **Clock accesi senza uso** dopo U-Boot: USBD (OTG), VDE + BSEA + BSEV,
  SNOR, CSITE, SDMMC1 anche con lo slot vuoto. PLLC a 600 MHz serve solo
  host1x (150 MHz). La memoria è fissa a 666 MHz (DDR2-667, PLLM).
- **Codec ALC5632**: i registri di power management `0x3a/0x3c/0x3e` a zero
  spengono tutto (~70 mW). L'altoparlante funziona solo dopo la richiesta
  NVEC OEM `0d 10 59 95` ("unmute speakers"), come in Linux `nvec.c`.
- **HDMI**: LDO7 (3,3 V) e LDO8 (1,8 V) del PMIC servono solo con uno
  schermo collegato.
- **Sensore ADT7461**: il canale remoto (CPU) sulla nostra macchina resta
  fermo (48,5–54 °C, non segue il carico); il canale locale funziona.

---

## 6. Webcam

La webcam interna (Realtek, UVC) è sulla porta 2 dell'hub SMSC 2513 sul
controller USB3. Sul nostro esemplare:

- l'hub la vede collegata ma la negozia sempre in **full speed**, mai high
  speed (16 reset su 16, anche dopo 3 s di porta spenta, sospensione e
  ripresa);
- il primo pacchetto SETUP all'indirizzo 0 riceve STALL (qTD `0x00080e40`:
  halted con contatore errori a 3, cioè l'hub che non ottiene risposta);
- un mouse sulla porta 3 dello stesso hub funziona, quindi lo stack USB è a
  posto;
- dallo schema (pagina 13) la webcam prende `+3VS_LVDS_CAM` = `+3VS`
  attraverso una resistenza da 0 Ω: **nessun GPIO o regolatore da accendere**.
  D+/D− passano nel cavo del display (connettore JLVDS1, pin 24–26, con il
  filtro L4 sulla scheda madre).

Conclusione: guasto hardware probabile (cavo nella cerniera, connettore o
modulo). Per escludere il software senza Linux: `usb tree` da U-Boot.

---

## 7. Note varie dal syslog

- `gpt: only the backup table ... is valid`: normale sull'eMMC dell'AC100, i
  primi 16 MB contengono BCT e bootloader originali e non vanno scritti.
- `Failed to open packages activation file .../activated-packages`: innocuo
  su un sistema nuovo, i pacchetti vengono caricati tutti.
- `usb error ehci 1: qtd ... 0x00048141` seguito da `usb_hid: Unable to
  unstall device` in serie: è un dispositivo USB scollegato, il driver ci
  mette qualche tentativo ad accorgersene.
- `nvec: the EC does not answer ..., restarting the controller`: capita
  raramente, il driver si riprende.
- Riavvio dopo un aggiornamento: nel syslog sono finiti pezzi di altri file
  (impostazioni di Tracker), segno di dati non scritti su disco prima del
  reset.

---

## 8. Strumenti scritti per queste prove

Tutti in `tests/ac100diag/` (compilati con l'ambiente `apps-extra`):

| Strumento | Cosa fa |
|---|---|
| `ac100power` | corrente/tensione dall'EC, temperature, idle; `bench`, `floor`, `dpms` |
| `clocktest.c` | per ogni frequenza: velocità reale e durata di `snooze` 1/10/100 ms |
| `cpucheck.c` | calcolo intero e virgola mobile su tutte le CPU, verificato |
| `ac100usb` | richieste di classe hub alla porta della webcam |
| `ac100audio` | registri del codec e del SoC (solo `0x6000xxxx`/`0x7000xxxx`) |
| `ac100ec` | richieste NVEC grezze all'embedded controller |

I commit corrispondenti (ramo `ac100-test`): DPMS off vero `b42c9cab73`,
cambio di frequenza con timer riscalato `34c0509462`, modulo cpufreq
`79c44155e1`, governor `4f24b9f6a7`, orologio dell'add-on audio
Bluetooth più lento `8e44485117`.

---

## 9. Il blocco in modalità "risparmio energetico" (bug dello scheduler su ARM)

Passando alla modalità risparmio energetico di ProcessController, l'AC100 si
bloccava entro un secondo, anche senza il modulo cpufreq. Causa: in quella
modalità, una CPU che va in idle sposta tutti i suoi interrupt sul core dei
piccoli task (`pack_irqs()` in `kernel/scheduler/power_saving.cpp`), in un
ciclo che finisce quando la sua lista è vuota. Il GIC però non può instradare
SGI e PPI (interrupt 0–31, propri di ogni CPU), tra cui il timer privato del
Cortex-A9: l'interrupt resta dov'è, è sempre il primo della lista e il ciclo
non finisce mai, con il lock dello scheduler preso.

Correzioni: il driver GIC registra 0–31 come interrupt locali
(`INTERRUPT_TYPE_LOCAL_IRQ`, come il timer APIC su x86), e `pack_irqs()` si
ferma se un interrupt non si lascia spostare. Dopo la correzione, 30 s in
risparmio energetico: la CPU sta a 216 MHz per 26 s.

Attenzione nelle misure: leggere lo stato del governor da
`/dev/power/tegra_dvfs` interroga il PMIC via I²C e falsa il carico che il
governor vede. Per giudicare il comportamento a riposo, non leggere niente
durante la prova e confrontare solo i secondi passati a ogni frequenza.
