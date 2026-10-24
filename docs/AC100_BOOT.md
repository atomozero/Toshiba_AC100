# Primo boot di Haiku sull'AC100

Tutta la procedura **non scrive nulla sull'eMMC**: U-Boot viene caricato in RAM via USB
(RCM) e Haiku si avvia dalla microSD. Spegnendo l'AC100 torna tutto come prima.

Fonti: guida "Migrate to U-Boot" di paz00.ru (APX, `tegrarcm readbct`, indirizzo di
caricamento) e documentazione U-Boot `doc/board/lg/star.rst` (stesso comando per un altro
Tegra20). I passi **(?)** non sono ancora stati provati sull'hardware.

## Cosa serve

| Cosa | Dove |
|---|---|
| AC100 carico, cavo mini-USB, microSD ≥ 512 MB | da procurare |
| `tegrarcm` 1.8 (compilato) | `out/tegrarcm` + `out/lib/libcrypto++.so.8` (lo script imposta il percorso; serve `libusb-1.0` di sistema) |
| U-Boot `paz00` con **ums** e **fastboot** sulla porta mini-USB | `out/uboot-ums/u-boot-dtb-tegra.bin` |
| U-Boot `paz00` standard | `out/u-boot-dtb-tegra.bin` |
| Immagine Haiku per la microSD | `out/ac100-sd.image` |
| Script | `scripts/ac100-rcm.sh` |

**`nvflash` non serve**: il BCT si legge con `tegrarcm readbct` e il backup dell'eMMC si
fa con il comando `ums` di U-Boot.

## Accesso USB dal PC

Due possibilità:
- **WSL**: installare su Windows *usbipd-win* (`winget install usbipd`), poi da un
  PowerShell amministratore `usbipd list`, `usbipd bind --busid <id>` e
  `usbipd attach --wsl --busid <id>`. Il dispositivo APX si "riconnette" a ogni passaggio
  (RCM → U-Boot), quindi l'attach va ripetuto (`usbipd attach --auto-attach` aiuta).
  In WSL l'accesso a `/dev/bus/usb` può richiedere `sudo` **(?)**.
- **Linux live su chiavetta** (più semplice per l'USB): copiare `out/` e `scripts/`.

## Passi

1. **microSD**: scrivere `out/ac100-sd.image` con Rufus / balenaEtcher e inserirla
   nell'AC100.
2. **Modalità APX**: AC100 spento, mini-USB collegato al PC, tenere premuti **Ctrl+Esc** e
   premere **accensione**. Lo schermo resta nero; il PC vede il dispositivo `0955:7820` (lo script lo controlla da solo).
3. **Leggere il BCT** (sola lettura):
   ```sh
   scripts/ac100-rcm.sh readbct      # → out/ac100.bct, conservarne una copia
   ```
4. **Avviare U-Boot in RAM** (rientrare in APX se l'AC100 si è riavviato):
   ```sh
   scripts/ac100-rcm.sh boot         # U-Boot con ums/fastboot
   ```
   Lo schermo dovrebbe accendersi con la console di U-Boot.
5. **Backup completo dell'eMMC** (consigliato prima di qualsiasi esperimento futuro). Con
   una tastiera USB sull'AC100, al prompt di U-Boot (premere un tasto durante il conto alla
   rovescia):
   ```
   ums 0 mmc 0
   ```
   Sul PC compare un disco USB (l'eMMC): `sudo dd if=/dev/sdX of=ac100-emmc.img bs=4M
   status=progress`. Le partizioni hardware `boot1`/`boot2` (BCT e bootloader) vanno salvate
   a parte con `ums 0 mmc 0.1` e `ums 0 mmc 0.2` **(?)**. Ctrl+C sull'AC100 interrompe `ums`.
6. **Boot di Haiku**: `boot` al prompt, oppure si lascia partire l'autoboot. U-Boot cerca
   in `usb mmc1 mmc0`; `mmc1` è la microSD con `EFI/BOOT/BOOTARM.EFI` e
   `tegra20-paz00.dtb`.

## Cosa aspettarsi

- Il loader Haiku dovrebbe comparire sullo schermo (EFI GOP). Con **Spazio** all'avvio si
  apre il menu, dove si può attivare il debug a schermo.
- Il kernel dovrebbe usare `gicv1`, il timer `tegra20` e la UART `0x70006000`, come
  verificato in QEMU con il DTB dell'AC100.
- Con il driver SDHCI per Tegra (patch `0011`, non ancora provato sull'hardware) il kernel
  dovrebbe vedere la microSD (`disk/mmc/...`), montare la partizione BFS e proseguire verso
  lo userland. Se invece si ferma con "did not find any boot partitions", il problema è nel
  driver SD: CPU, FPU D16, GICv1 e timer Tegra funzionano già, se si arriva fin lì.
- Tastiera USB (driver EHCI Tegra, patch `0012`, non ancora provato sull'hardware):
  **collegarla prima di accendere**. Per ora i PHY USB li prepara U-Boot, che avvia l'USB
  durante la ricerca del boot (`boot_targets = usb mmc1 mmc0`).

## Se qualcosa non va

| Sintomo | Cosa provare |
|---|---|
| Nessun `0955:7820` | ripetere la combinazione tenendo i tasti più a lungo; controllare l'inoltro usbipd |
| `readbct` fallisce | provare `--usb-timeout=0`; annotare l'errore esatto |
| U-Boot non parte (schermo nero) | provare `out/u-boot-dtb-tegra.bin` (U-Boot standard) |
| U-Boot non trova la SD | a mano: `load mmc 1:1 ${fdt_addr_r} tegra20-paz00.dtb; load mmc 1:1 ${kernel_addr_r} EFI/BOOT/BOOTARM.EFI; bootefi ${kernel_addr_r} ${fdt_addr_r}` |
| Haiku si blocca | fotografare lo schermo: l'ultima riga dice in quale fase |
