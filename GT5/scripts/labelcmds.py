"""Find RSX semaphore commands that write given labels, in dumps saved by gmem.py: labelcmds.py <file> <ea, hex> [low hex] [high hex]

Looks for method 0x1D6C (offset) with a following 0x1D70/0x1D74 release, in any command header shape, and
prints the address, label offset and value. Reports only offsets in low..high (default 0x480..0x4F0).
"""
import struct
import sys

d = open(sys.argv[1], "rb").read()
base = int(sys.argv[2], 16)
low = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x480
high = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x4F0
n = len(d) // 4
w = struct.unpack(">%dI" % n, d[:n * 4])
hits = 0
i = 0
while i < n - 3:
    x = w[i]
    if x & 0xE0030003 == 0 and x & 0xFFFC == 0x1D6C:
        cnt = (x >> 18) & 0x7FF
        off = w[i + 1]
        if low <= off <= high and cnt:
            if cnt >= 2:
                print("%08X off=%X value=%08X (m=1D70, one header)" % (base + i * 4, off, w[i + 2]))
                hits += 1
            else:
                y = w[i + 2]
                m = y & 0xFFFC
                print("%08X off=%X next header %08X value=%08X" % (base + i * 4, off, y, w[i + 3]))
                hits += 1
    i += 1
print("hits", hits)
