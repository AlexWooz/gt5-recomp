import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""What script-visible name is a native function registered under? native_name.py 00747C28 ...

Finds the OPD for the code address, the TOC slots holding that OPD, and the lifted
code that loads those slots, then prints the strings loaded just before each one.
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


def cstr(va):
    for v, o, f in SEGS:
        if v <= va < v + f:
            e = d.find(b"\0", o + va - v)
            s = d[o + va - v:e]
            if 2 <= len(s) < 80 and all(32 <= c < 127 for c in s):
                return s.decode()
    return None


base, off, size = SEGS[1]
words = struct.unpack(f">{size // 4}I", d[off:off + size // 4 * 4])
load = re.compile(r"vm_read32\(ctx->gpr\[2\] \+ (-?0x[0-9A-F]+)\)")
files = {}


def lines(path):
    if path not in files:
        files[path] = open(path, encoding="utf-8", errors="replace").readlines()
    return files[path]


for arg in sys.argv[1:]:
    code = int(arg, 16)
    opds = [base + i * 4 for i, w in enumerate(words) if w == code]
    print(f"== {code:08X}: OPDs {[hex(o) for o in opds]}")
    for opd in opds:
        slots = [base + i * 4 for i, w in enumerate(words) if w == opd]
        for slot in slots:
            for toc in TOCS:
                disp = slot - toc
                if abs(disp) >= 0x8000:
                    continue
                tag = f"ctx->gpr[2] + {'-' if disp < 0 else ''}0x{abs(disp):X})"
                for path in sorted(glob.glob((GT5_DIR + r"\generated\ppu_recomp_0*.cpp"))):
                    src = lines(path)
                    for n, l in enumerate(src):
                        if tag in l and "vm_read32" in l:
                            near = []
                            for w in src[max(0, n - 14):n + 15]:
                                m = load.search(w)
                                if m:
                                    s = cstr(u32(toc + int(m.group(1), 16)) or 0)
                                    if s:
                                        near.append(s)
                            print(f"   slot {slot:#x} toc {toc:#x} {path[-8:-4]}:{n} strings near: {near[:8]}")
