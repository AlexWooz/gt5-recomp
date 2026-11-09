import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Which lifted function holds a return address, and who calls a function: fnat.py 008FA0BC [--callers 008F9DE8]"""
import bisect
import glob
import re
import sys

head = re.compile(r"^void func_([0-9A-F]{8})\(ppu_context\* ctx\) \{")
starts = []
paths = sorted(glob.glob((GT5_DIR + r"\generated\ppu_recomp_0*.cpp")))
callers = []
if "--callers" in sys.argv:
    i = sys.argv.index("--callers")
    callers = ["func_%s(" % a.upper().rjust(8, "0") for a in sys.argv[i + 1:]]
    args = sys.argv[1:i]
else:
    args = sys.argv[1:]
for path in paths:
    cur = None
    for line in open(path, encoding="utf-8", errors="replace"):
        m = head.match(line)
        if m:
            cur = m.group(1)
            starts.append(int(cur, 16))
        elif callers and any(c in line for c in callers):
            lr = re.search(r"lr = 0x([0-9A-Fa-f]+)", line)
            print("%s called in func_%s (lr %s)" % (next(c for c in callers if c in line)[:-1], cur, lr.group(1) if lr else "?"))
starts.sort()
for a in args:
    v = int(a, 16)
    print("%X is in func_%08X" % (v, starts[bisect.bisect_right(starts, v) - 1]))
