"""Which of the title's firmware imports have an HLE handler in the generated table?

  python hle_coverage.py analysis/EBOOT.imports.json src/gen/ppu_hle_nids.cpp
"""
import collections
import json
import re
import sys

imports = json.load(open(sys.argv[1]))
table = open(sys.argv[2], encoding="utf-8", errors="replace").read()
have = {int(m, 16) for m in re.findall(r"ps3_hle_register\(0x([0-9A-Fa-f]{1,8})u", table)}

per = collections.defaultdict(lambda: [0, 0, []])
for e in imports:
    nid = int(e["nid"], 0) & 0xFFFFFFFF
    p = per[e["library"]]
    p[0] += 1
    if nid in have:
        p[1] += 1
    else:
        p[2].append(f"0x{nid:08X}")

tot = sum(p[0] for p in per.values())
hit = sum(p[1] for p in per.values())
print(f"covered {hit}/{tot} ({hit * 100 // tot}%)")
for lib, (n, h, miss) in sorted(per.items(), key=lambda kv: kv[1][1] - kv[1][0]):
    flag = "" if h == n else f"  MISSING {n - h}"
    print(f"  {lib:<26}{h:>4}/{n:<4}{flag}")
