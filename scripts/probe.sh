#!/bin/bash
# Avvia QEMU, attende la riga $1 nel log seriale, poi campiona i registri della CPU.
source ~/haiku-arm/env.sh; export QEMU_AUDIO_DRV=none
cd ~/haiku-arm/test
WAIT_FOR=$1; LOG=${2:-probe.log}; EXTRA=${3:-}
rm -f mon.sock $LOG
cp ~/haiku-arm/haiku/generated.arm/haiku-mmc.image run.image
qemu-system-arm -M virt -cpu cortex-a15 -m 1024 -bios ~/haiku-arm/build-uboot-qemu_arm/u-boot.bin \
	-drive file=run.image,if=none,format=raw,id=d0 -device virtio-blk-device,drive=d0 \
	-nic none -display none -serial file:$LOG -monitor unix:mon.sock,server,nowait $EXTRA >/dev/null 2>&1 &
QPID=$!
for i in $(seq 1 120); do
	grep -a -q -- "$WAIT_FOR" $LOG 2>/dev/null && break
	sleep 3
done
sleep ${EXTRA_WAIT:-15}
SHOT=${SHOT:-} python3 - <<'PY'
import os,socket,time
shot=os.environ.get("SHOT")
if shot:
    s=socket.socket(socket.AF_UNIX); s.connect('mon.sock'); time.sleep(0.3); s.recv(65536)
    s.send(("screendump %s\n" % shot).encode()); time.sleep(2); s.close()
for n in range(4):
    s=socket.socket(socket.AF_UNIX); s.connect('mon.sock'); time.sleep(0.3); s.recv(65536)
    s.send(b'info registers\n'); time.sleep(0.5)
    out=s.recv(65536).decode(errors='replace')
    print([l for l in out.splitlines() if 'R12' in l or 'PSR' in l])
    s.close(); time.sleep(1)
PY
kill $QPID
