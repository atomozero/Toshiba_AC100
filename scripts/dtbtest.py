#!/usr/bin/env python3
"""Run the Haiku EFI loader in QEMU virt via U-Boot, but hand it a foreign DTB
(e.g. the AC100's) to check how the loader parses it. Prints the serial log."""
import os, socket, subprocess, sys, time

dtb = sys.argv[1]
image = os.path.expanduser("~/haiku-arm/test/dtbtest.image")
sock = os.path.expanduser("~/haiku-arm/test/ser.sock")
uboot = os.path.expanduser("~/haiku-arm/build-uboot-qemu_arm/u-boot.bin")

# ESP-only disk with the loader and the DTB
# full Haiku image (ESP + BFS) with the DTB added to the ESP
subprocess.run(f"""set -e
cp ~/haiku-arm/haiku/generated.arm/haiku-mmc.image {image}
mcopy -o -i {image}@@4194304 {dtb} ::/test.dtb""", shell=True, check=True)

if os.path.exists(sock):
    os.unlink(sock)
env = dict(os.environ, QEMU_AUDIO_DRV="none")
qemu = subprocess.Popen(["qemu-system-arm", "-M", "virt", "-cpu", "cortex-a15",
    "-m", "1024", "-bios", uboot, "-drive", f"file={image},if=none,format=raw,id=d0",
    "-device", "virtio-blk-device,drive=d0", "-nic", "none", "-display", "none",
    "-device", "bochs-display,romfile=",
    "-monitor", "unix:" + sock + ".mon,server,nowait",
    "-serial", f"unix:{sock},server"], env=env,
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

for _ in range(50):
    if os.path.exists(sock):
        break
    time.sleep(0.1)
s = socket.socket(socket.AF_UNIX)
s.connect(sock)
s.settimeout(0.5)

log = b""
def read_until(pattern, timeout):
    global log
    end = time.time() + timeout
    while time.time() < end:
        try:
            data = s.recv(65536)
            if not data:
                break
            log += data
            if pattern and pattern in log:
                return True
        except socket.timeout:
            pass
    return False

read_until(b"Hit any key", 60)
s.send(b"\n")
read_until(b"=>", 10)
for cmd in ["virtio scan",
        "load virtio 0:1 0x48000000 test.dtb",
        "load virtio 0:1 ${kernel_addr_r} EFI/BOOT/BOOTARM.EFI",
        "bootefi ${kernel_addr_r} 0x48000000"]:
    s.send(cmd.encode() + b"\n")
    time.sleep(1)
read_until(b"enter_kernel", 400)
read_until(None, 60)
if len(sys.argv) > 2:
    m = socket.socket(socket.AF_UNIX)
    m.connect(sock + ".mon"); time.sleep(0.3); m.recv(65536)
    m.send(("screendump %s\n" % sys.argv[2]).encode()); time.sleep(2); m.close()
qemu.kill()
sys.stdout.write(log.decode(errors="replace"))
