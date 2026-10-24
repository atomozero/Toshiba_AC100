#!/usr/bin/env python3
"""Writes per-page checksums of the kernel's read-only segment into the
gArmTextCheck table of kernel_arm (bring-up aid for the ac100-markers branch).
Pages touched by relocations get 0 (not checked)."""
import struct, subprocess, sys

path = sys.argv[1]
data = bytearray(open(path, 'rb').read())
e_phoff, = struct.unpack_from('<I', data, 28)
e_phentsize, e_phnum = struct.unpack_from('<HH', data, 42)
loads = []
for i in range(e_phnum):
    p_type, p_offset, p_vaddr, _, p_filesz, p_memsz, p_flags, _ = \
        struct.unpack_from('<8I', data, e_phoff + i * e_phentsize)
    if p_type == 1:
        loads.append((p_offset, p_vaddr, p_filesz, p_flags))
text = [l for l in loads if not l[3] & 2][0]


def file_offset(va):
    for off, vaddr, filesz, _ in loads:
        if vaddr <= va < vaddr + filesz:
            return off + va - vaddr
    raise SystemExit('address %#x not in file' % va)


nm = subprocess.run(['arm-linux-gnueabihf-nm', path], capture_output=True,
    text=True, check=True).stdout
table = [int(l.split()[0], 16) for l in nm.splitlines()
    if l.split()[-1] == 'gArmTextCheck'][0]

relocPages = set()
relocs = subprocess.run(['arm-linux-gnueabihf-readelf', '-rW', path],
    capture_output=True, text=True, check=True).stdout
for l in relocs.splitlines():
    f = l.split()
    if f and len(f[0]) == 8:
        try:
            a = int(f[0], 16)
        except ValueError:
            continue
        if text[1] <= a < text[1] + text[2]:
            relocPages.add((a - text[1]) // 4096)

words = text[2] // 4
sums = []
for page in range((words + 1023) // 1024):
    s = 0
    for i in range(page * 1024, min(words, (page + 1) * 1024)):
        w, = struct.unpack_from('<I', data, text[0] + i * 4)
        s = ((s << 1) | (s >> 31)) & 0xffffffff
        s ^= w
    sums.append(0 if page in relocPages else (s or 1))

off = file_offset(table)
magic, = struct.unpack_from('<I', data, off)
assert magic == 0x7e57c0de, hex(magic)
struct.pack_into('<II', data, off + 4, text[1], words)
struct.pack_into('<%dI' % len(sums), data, off + 12, *sums)
open(path, 'wb').write(data)
print('base %#x, %d words, %d pages, %d skipped' % (text[1], words, len(sums),
    len(relocPages)))
