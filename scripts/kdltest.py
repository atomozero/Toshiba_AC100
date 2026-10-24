#!/usr/bin/env python3
"""Boot Haiku on QEMU vexpress-a9 (Cortex-A9, GICv1, TWD), wait for the kernel
debugger after the expected boot panic and run a few KDL commands on it."""
import os, socket, subprocess, sys, time

image = os.path.expanduser("~/haiku-arm/test/a9.image")
sock = os.path.expanduser("~/haiku-arm/test/ser9.sock")
uboot = os.path.expanduser("~/haiku-arm/build-uboot-vexpress_ca9x4/u-boot")
commands = sys.argv[1:] or ["timers", "ints"]

if os.path.exists(sock):
    os.unlink(sock)
qemu = subprocess.Popen(["qemu-system-arm", "-M", "vexpress-a9", "-m", "1024",
    "-kernel", uboot, "-sd", image, "-nic", "none", "-display", "none",
    "-monitor", "none", "-serial", f"unix:{sock},server"],
    env=dict(os.environ, QEMU_AUDIO_DRV="none"),
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
                return False
            log += data
            if pattern and pattern in log[-4096:]:
                return True
        except socket.timeout:
            pass
    return False

ok = read_until(b"kdebug>", 400)
for command in commands if ok else []:
    for ch in command + "\n":
        s.send(ch.encode())
        time.sleep(0.2)
    read_until(b"kdebug>", 20)
qemu.kill()
text = log.decode(errors="replace").replace("\r", "")
start = text.find("PANIC")
sys.stdout.write(text[start:] if start >= 0 else text[-3000:])
