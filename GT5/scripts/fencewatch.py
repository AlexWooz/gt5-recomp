"""Readable timeline of the frame fence in a running gt5.exe: fencewatch.py <gwatch output> [--every s]

Folds gwatch.py lines into a per-interval row: put, get, list index, fence flag, the job heads and the ring labels.
Rows repeat only when something changed.
"""
import sys

every = 0.25
if "--every" in sys.argv:
    every = float(sys.argv[sys.argv.index("--every") + 1])
NAMES = {0x20002000: "put", 0x20002004: "get", 0x1A13F48: "list", 0x1A3AD88: "fence", 0x1A13EC0: "h0", 0x1A13ED0: "h1",
         0x20000480: "A0", 0x20000490: "A1", 0x200004A0: "A2", 0x200004B0: "A3", 0x200004C0: "B0", 0x200004D0: "B1",
         0x200004E0: "B2", 0x200004F0: "B3", 0x19E632C: "tick"}
state = {}
last = None
next_t = 0.0
order = list(NAMES.values())
for line in open(sys.argv[1]):
    t, ea, old, new = line.split()
    t = float(t)
    ea = int(ea, 16)
    if ea in NAMES:
        state.setdefault(NAMES[ea], old)
    if t >= next_t:
        row = " ".join("%s=%s" % (k, state.get(k, "-")) for k in order)
        if row != last:
            print("%8.2f %s" % (t, row))
            last = row
        next_t = t + every
    if ea in NAMES:
        state[NAMES[ea]] = new
