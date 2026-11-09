"""Walk RSX commands in a saved FIFO dump and list label writes: fifofind.py <file> <io of first byte, hex>

Prints each semaphore offset (0x1D6C and friends) and release with its value, jumps and calls, and the last
position it could decode.
"""
import struct
import sys

d = open(sys.argv[1], "rb").read()
base = int(sys.argv[2], 16)
pos = 0
n = len(d) // 4
words = struct.unpack(">%dI" % n, d[:n * 4])
i = 0
sem = None
count = {}
while i < n:
    w = words[i]
    io = base + i * 4
    if w == 0:
        i += 1
        continue
    if w & 0xE0000003 == 0x20000000:
        print("%08X jump %08X%s" % (io, w & 0x1FFFFFFC, " (self)" if (w & 0x1FFFFFFC) == io else ""))
        i += 1
        continue
    if w & 3 == 1:
        print("%08X jump %08X" % (io, w & ~3))
        i += 1
        continue
    if w & 3 == 2:
        print("%08X call %08X" % (io, w & ~3))
        i += 1
        continue
    if w == 0x00020000:
        print("%08X return" % io)
        i += 1
        continue
    cnt = (w >> 18) & 0x7FF
    method = w & 0xFFFC
    ni = bool(w & 0x40000000)
    if w & 0xA0030003:
        print("%08X bad word %08X" % (io, w))
        i += 1
        continue
    for a in range(cnt):
        if i + 1 + a >= n:
            break
        m = method if ni else method + a * 4
        v = words[i + 1 + a]
        count[m] = count.get(m, 0) + 1
        if m in (0x1D6C, 0x0064):
            sem = v
            print("%08X semaphore offset %X (m=%04X)" % (io, v, m))
        elif m in (0x1D70, 0x1D74, 0x006C, 0x0068):
            print("%08X semaphore %s value %08X at offset %s (m=%04X)" % (io, "release" if m != 0x0068 else "acquire", v, "%X" % sem if sem is not None else "?", m))
        elif m == 0x1808:
            count["draws"] = count.get("draws", 0) + 1
    i += 1 + cnt
print("decoded to %08X, draw begin/ends %d" % (base + i * 4, count.get("draws", 0)))
print("methods:", " ".join("%04X:%d" % (k, v) for k, v in sorted(count.items(), key=lambda kv: -kv[1])[:12] if k != "draws"))
