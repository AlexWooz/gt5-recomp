"""Keep only find_functions.py results that start in executable sections.

The ELF has one RX segment holding both code and .rodata, so the heuristic passes
also "find" functions in .rodata. The section headers say where code really ends.

  python filter_functions.py input/EBOOT.elf analysis/functions.json analysis/functions_code.json
"""
import json
import struct
import sys

elf, src, dst = sys.argv[1:4]
d = open(elf, "rb").read()
e_shoff = struct.unpack(">Q", d[40:48])[0]
e_shentsize, e_shnum = struct.unpack(">HH", d[58:62])

SHF_EXECINSTR = 4
execs = []
for i in range(e_shnum):
    o = e_shoff + i * e_shentsize
    _, _, flags, addr, _, size = struct.unpack(">IIQQQQ", d[o:o + 40])
    if flags & SHF_EXECINSTR and size:
        execs.append((addr, addr + size))
        print(f"exec section 0x{addr:x}-0x{addr + size:x} ({size:,} bytes)")

code_end = max(e for _, e in execs)
print(f"code end: 0x{code_end:x}")

fns = json.load(open(src))
items = fns["functions"] if isinstance(fns, dict) else fns


def start(f):
    s = f["start"]
    return int(s, 0) if isinstance(s, str) else s


kept = [f for f in items if any(a <= start(f) < e for a, e in execs)]
print(f"functions: {len(items):,} -> {len(kept):,}")
out = dict(fns, functions=kept) if isinstance(fns, dict) else kept
json.dump(out, open(dst, "w"))
