"""Tally arguments of an RSX method in a gmem.py dump: methodfind.py <file> <ea hex> <method hex> [method hex ...]

Scans for one-argument headers of each method (a cheap pattern; it can catch look-alike data) and counts values.
"""
import collections
import struct
import sys

d = open(sys.argv[1], "rb").read()
base = int(sys.argv[2], 16)
n = len(d) // 4
w = struct.unpack(">%dI" % n, d[:n * 4])
for ms in sys.argv[3:]:
    m = int(ms, 16)
    head = (1 << 18) | m
    c = collections.Counter(w[i + 1] for i in range(n - 1) if w[i] == head)
    first = [base + i * 4 for i in range(n - 1) if w[i] == head][:3]
    print("%04X: %d hits, first at %s" % (m, sum(c.values()), " ".join("%08X" % a for a in first)))
    for v, k in c.most_common(10):
        print("   %08X x%d" % (v, k))
