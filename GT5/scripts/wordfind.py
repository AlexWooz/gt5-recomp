"""Find big-endian words in a dump saved by gmem.py: wordfind.py <file> <ea of first byte, hex> <value, hex> [mask, hex]"""
import struct
import sys

d = open(sys.argv[1], "rb").read()
base = int(sys.argv[2], 16)
val = int(sys.argv[3], 16)
mask = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0xFFFFFFFF
n = len(d) // 4
for i, w in enumerate(struct.unpack(">%dI" % n, d[:n * 4])):
    if w & mask == val & mask:
        print("%08X: %08X" % (base + i * 4, w))
