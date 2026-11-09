import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Which native is registered under a script name? native_by_name.py WaitAdvertiseEnd ...

Finds the string, the TOC slots pointing at it, the lifted code loading them, and every
OPD loaded nearby, since registration passes the name and the OPD together.
"""
import glob
import re
import struct
import sys

d = open((GT5_DIR + r"\input\EBOOT.elf"), "rb").read()
SEGS = [(65536, 0, 24585320), (24707072, 24641536, 1363804)]
TOCS = [0x1846AF0, 0x1866624, 0x18765B0, 0x185682C]


def u32(va):
    for v, o, f in SEGS:
        if v <= va < v + f - 3:
            return struct.unpack(">I", d[o + va - v:o + va - v + 4])[0]
    return None


base, off, size = SEGS[1]
words = struct.unpack(f">{size // 4}I", d[off:off + size // 4 * 4])
load = re.compile(r"vm_read32\(ctx->gpr\[2\] \+ (-?0x[0-9A-F]+)\)")
files = {p: open(p, encoding="utf-8", errors="replace").readlines()
         for p in sorted(glob.glob((GT5_DIR + r"\generated\ppu_recomp_0*.cpp")))}
head = re.compile(r"^void func_([0-9A-F]{8})\(")
for name in sys.argv[1:]:
    pat = name.encode() + b"\0"
    i = -1
    while True:
        i = d.find(pat, i + 1)
        if i < 0:
            break
        if d[i - 1] != 0:  # part of a longer string
            continue
        va = 0x10000 + i
        slots = [base + k * 4 for k, w in enumerate(words) if w == va]
        print(f"== {name} string {va:#x} slots {[hex(s) for s in slots]}")
        for slot in slots:
            for toc in TOCS:
                disp = slot - toc
                if abs(disp) >= 0x8000:
                    continue
                tag = f"ctx->gpr[2] + {'-' if disp < 0 else ''}0x{abs(disp):X})"
                for path, src in files.items():
                    for n, l in enumerate(src):
                        if tag in l and "vm_read32" in l:
                            fn = next((head.match(src[k]).group(1) for k in range(n, 0, -1) if head.match(src[k])), "?")
                            found = []
                            for w in src[n:n + 10]:
                                m = load.search(w)
                                if m:
                                    v = u32(toc + int(m.group(1), 16))
                                    c = u32(v) if v else None
                                    if c and 0x10200 <= c < 0x111C684:
                                        found.append(f"opd {v:#x} code {c:08X}")
                            print(f"   in func_{fn} ({path[-8:-4]}:{n}): {found[:3]}")
