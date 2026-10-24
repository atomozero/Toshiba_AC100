#!/bin/bash
# Prove dell'AC100: lanciare da Terminal con  ~/ac100-test.sh
# Ogni risultato va a schermo e nel log di sistema (righe "AC100TEST:"),
# che finisce in AC100LOG.TXT sulla partizione EFI della microSD.

DATA=~/ac100-test-data
mkdir -p "$DATA"

log()
{
	echo "$*"
	echo "AC100TEST: $*" > /dev/dprintf
}

run()
{
	log "\$ $*"
	"$@" 2>&1 | while IFS= read -r line; do
		log "  $line"
	done
}

section()
{
	log ""
	log "=== $* ==="
}

ask()
{
	log ">>> $*"
	read -r -p "    (Invio quando hai fatto) " answer
	log "<<< fatto ${answer:+($answer)}"
}

section "AC100 test, $(date)"
run uname -a

section "Processori"
run sysinfo -cpu

section "Ora (dopo un riavvio deve essere andata avanti, non dal 1970)"
run date

section "Tasti (una cosa alla volta, aspetta 2-3 secondi dopo ognuna)"
log "Per l'Invio usa la tastiera INTERNA dell'AC100, non quella USB."
ask "Premi UNA volta il tasto di accensione (se compare la richiesta di spegnimento, annullala)"
ask "Premi Fn + il tasto per AUMENTARE la luminosita'"
ask "Premi Fn + il tasto per DIMINUIRE la luminosita'"
# Fn+F9 accende e spegne il touchpad nell'EC, che lo ricorda anche a macchina
# spenta: non chiederlo, lo spegnerebbe
log "(Non premere Fn+F9: spegne il touchpad; ripremerlo lo riaccende.)"
ask "Chiudi e riapri il coperchio"
ask "Muovi un po' il dito sul touchpad"

section "Audio"
run ls -R /dev/audio
ask "All'avvio hai sentito un breve suono dagli altoparlanti? (rispondi si/no e Invio)"
if [ -x /bin/beep ]; then
	log "beep:"
	beep
fi
ask "Hai sentito il beep appena suonato? (si/no)"

section "USB (il Wi-Fi interno sarebbe 0cf3:9271)"
run listusb
if listusb | grep -q "0cf3:9271"; then
	log "Wi-Fi Atheros AR9271: TROVATO"
else
	log "Wi-Fi Atheros AR9271: non trovato"
fi

# Dopo la scansione dei bus I2C l'EC sembra non mandare piu' eventi: i tasti
# sono provati prima, e qui li si riprova dopo
section "Bus I2C (sul bus 0 ci aspettiamo il codec audio a 1e)"
run ls /dev/bus/i2c
for bus in /dev/bus/i2c/*/bus_raw; do
	[ -e "$bus" ] && run i2c "$bus"
done

ask "Dopo la scansione I2C: premi Fn + il tasto per AUMENTARE la luminosita' e muovi il dito sul touchpad"

section "Dispositivi di alimentazione"
run ls -R /dev/power

section "Integrita' dei dati sulla SD (cache L2 e DMA)"
if [ -f "$DATA/md5" ]; then
	log "Controllo i file scritti in una prova precedente:"
	if (cd "$DATA" && md5sum -c md5) > "$DATA/check.txt" 2>&1; then
		log "Integrita' dopo il riavvio: OK"
	else
		log "Integrita' dopo il riavvio: ERRORE"
	fi
	while IFS= read -r line; do log "  $line"; done < "$DATA/check.txt"
fi
log "Scrivo 50 MB casuali, li copio e confronto:"
dd if=/dev/urandom of="$DATA/prova" bs=1M count=50 2> /dev/null
cp "$DATA/prova" "$DATA/prova2"
sync
(cd "$DATA" && md5sum prova prova2) > "$DATA/md5"
while IFS= read -r line; do log "  $line"; done < "$DATA/md5"
if [ "$(cut -d' ' -f1 "$DATA/md5" | sort -u | wc -l)" = 1 ]; then
	log "Copia: OK (al prossimo avvio rilancia lo script per il controllo)"
else
	log "Copia: ERRORE, i due file sono diversi"
fi

section "Velocita'"
log "Ciclo di bash da 200000 giri:"
start=$(date +%s%N)
for i in $(seq 1 200000); do :; done
end=$(date +%s%N)
log "  $(( (end - start) / 1000000 )) ms"
for test in smptest memtest exctest sigtest ptytest; do
	if [ -x /boot/system/non-packaged/bin/$test ]; then
		log "$test (risultato nel log):"
		/boot/system/non-packaged/bin/$test
	fi
done

section "Fine: spegni, rimetti la microSD nel PC e mandami AC100LOG.TXT"
