import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Where a function's address is stored: opdrefs.py <hex addr>... lists OPDs naming it and the vtable slots naming each OPD."""
import struct
import sys

d = open((GT5_DIR + r"\input\EBOOT.elf"), "rb").read()
BASE = 0x10000


def find_all(b):
    i = d.find(b)
    while i >= 0:
        yield i
        i = d.find(b, i + 1)


for a in sys.argv[1:]:
    ea = int(a, 16)
    for i in find_all(struct.pack(">I", ea)):
        print("%08X stored at %08X (next word %08X)" % (ea, i + BASE, struct.unpack(">I", d[i + 4:i + 8])[0]))
        for j in find_all(struct.pack(">I", i + BASE)):
            w = struct.unpack(">8I", d[(j & ~31):(j & ~31) + 32])
            print("   slot at %08X; around: %s" % (j + BASE, " ".join("%08X" % x for x in w)))
