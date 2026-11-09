import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Print lifted functions by address, without the noise: showfn.py 00010230 000105A8 [--max N]"""
import glob
import re
import sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
maxl = 120
if "--max" in sys.argv:
    maxl = int(sys.argv[sys.argv.index("--max") + 1])
    args.remove(str(maxl))
want = {a.upper().rjust(8, "0") for a in args}
head = re.compile(r"^void func_([0-9A-F]{8})\(ppu_context\* ctx\) \{")
cr = re.compile(r"\{ int64_t a = \(.*?\)ctx->gpr\[(\d+)\]; int64_t b = (.*?); uint32_t cr_val.*?\(0xFu << (\d+)\)\).*")
for path in sorted(glob.glob((GT5_DIR + r"\generated\ppu_recomp_0*.cpp"))):
    on = False
    n = 0
    for line in open(path, encoding="utf-8", errors="replace"):
        m = head.match(line)
        if m:
            on = m.group(1) in want
            n = 0
            if on:
                print(f"--- {path[-18:]} func_{m.group(1)}")
        if on:
            n += 1
            if n <= maxl:
                s = line.rstrip()[4:]
                s = cr.sub(lambda c: f"cmp r{c.group(1)}, {c.group(2)} -> cr<<{c.group(3)}", s)
                s = s.replace("ctx->gpr[", "r[").replace("DRAIN_TRAMPOLINE(ctx);", "").replace("(ctx);", "()")
                print(s[:170])
            if line.startswith("}"):
                on = False
