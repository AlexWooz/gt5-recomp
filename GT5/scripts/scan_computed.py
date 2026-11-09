import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""List lifted `bctr`s that are computed jumps inside a function, not tail calls.

A tail call through an OPD reloads r2 first. One that doesn't is either a jump through
a label table the lifter couldn't see, or a tail call through a plain code pointer.
"""
import glob
import re

head = re.compile(r"^void (?:gt5_orig_)?func_([0-9A-F]{8})\(")
tail = re.compile(r"ps3_indirect_call\(ctx\); return;")
sites = []
for path in sorted(glob.glob((GT5_DIR + r"\generated\ppu_recomp_0*.cpp"))):
    cur = None
    prev = []
    for line in open(path, encoding="utf-8", errors="replace"):
        m = head.match(line)
        if m:
            cur = m.group(1)
        if tail.search(line) and "switch (" not in line:
            ctx = prev[-6:]
            if not any("ctx->gpr[2] = vm_read" in p for p in ctx):
                sites.append((cur, [p.strip()[:80] for p in ctx]))
        prev.append(line)
        prev = prev[-6:]
print(len(sites), "sites")
seen = {}
for fn, ctx in sites:
    seen.setdefault(fn, []).append(ctx)
for fn, lst in seen.items():
    print(f"func_{fn}: {len(lst)}")
    for l in lst[0][-4:]:
        print("    ", l)
