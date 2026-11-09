import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Hot source lines of one thread in a prof.ps1 capture: proflines.py <prof.txt> <host tid> [--top N] [--context K]"""
import collections
import re
import subprocess
import sys

SYM = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\llvm-symbolizer.exe"
EXE = (GT5_DIR + r"\build\gt5.exe")
BASE = 0x140000000


def arg(n, d):
    return int(sys.argv[sys.argv.index(n) + 1]) if n in sys.argv else d


tid = int(sys.argv[2])
rows = []
for line in open(sys.argv[1], errors="replace"):
    p = line.split()
    if len(p) >= 4 and not line.startswith("#") and int(p[0]) == tid and " ".join(p[1:-2]).lower() == "gt5.exe":
        rows.append((int(p[-2], 16), int(p[-1])))
text = "\n".join(hex(BASE + a) for a, _ in rows) + "\n"
out = subprocess.run([SYM, "--obj=" + EXE, "--no-inlines", "--output-style=GNU"], input=text, capture_output=True, text=True).stdout.split("\n")
c = collections.Counter()
for i, (a, n) in enumerate(rows):
    c[(out[i * 2].strip(), out[i * 2 + 1].strip())] += n
tot = sum(c.values()) or 1
files = {}
for (fn, loc), n in c.most_common(arg("--top", 14)):
    print("%5.1f%% %s %s" % (100.0 * n / tot, fn[:30], loc[-36:]))
    m = re.match(r"(.*):(\d+)", loc)
    k = arg("--context", 0)
    if m and k and "generated" in m[1]:
        ls = files.setdefault(m[1], open(m[1], errors="replace").readlines())
        ln = int(m[2])
        for l in ls[max(0, ln - 1 - k):ln + k]:
            print("        " + l.strip()[:100])
