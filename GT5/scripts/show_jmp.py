"""Dump the setjmp/longjmp region and every bl into the entry points it finds."""
import struct
import sys

d = open(sys.argv[1], "rb").read()
lo, hi = int(sys.argv[2], 0), int(sys.argv[3], 0)
code_end = 0x111c684
BASE = 0x10000
n = (code_end - BASE) // 4
words = struct.unpack(f">{n}I", d[:n * 4])

ENTRY_MARK = 0x3863000F  # addi r3,r3,15 -- both routines align the buffer first
BLR = 0x4E800020
entries = []
for a in range(lo, hi, 4):
    w = words[(a - BASE) // 4]
    tag = ""
    if w == ENTRY_MARK:
        tag = "  <-- addi r3,r3,15 (entry)"
        entries.append(a)
    if w == BLR:
        tag = "  <-- blr"
    print(f"{a:08X}: {w:08X}{tag}")


def callers(target):
    out = []
    for i, w in enumerate(words):
        if w >> 26 == 18 and w & 3 == 1:
            li = w & 0x03FFFFFC
            if li & 0x02000000:
                li -= 0x04000000
            if BASE + i * 4 + li == target:
                out.append(BASE + i * 4)
    return out


for e in entries:
    cs = callers(e)
    print(f"entry 0x{e:08X}: {len(cs)} bl callers: " + " ".join(f"0x{c:08X}" for c in cs[:20]))
