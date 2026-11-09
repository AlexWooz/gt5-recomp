import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Name a prof.ps1 capture: profsym.py <prof.txt> [--threads N] [--top M] [--log <run .err>]

Prints the busiest functions overall, then the top functions for each busy thread, with the
guest thread id where the run's log gives one.
"""
import collections
import re
import subprocess
import sys

SYM = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\llvm-symbolizer.exe"
EXE = (GT5_DIR + r"\build\gt5.exe")
BASE = 0x140000000


def arg(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


nthreads = int(arg("--threads", 10))
top = int(arg("--top", 12))
log = arg("--log", None)
guest = {}
if log:
    for line in open(log, encoding="utf8", errors="replace"):
        m = re.match(r"\[THREAD (\d+)\] host thread started.*hosttid=(\d+)", line)
        if m:
            guest[int(m.group(2))] = m.group(1)

cpu = {}
rows = []
for line in open(sys.argv[1], encoding="utf8", errors="replace"):
    p = line.split()
    if line.startswith("#cpu"):
        cpu[int(p[1])] = int(p[2])
    elif len(p) >= 4:
        rows.append((int(p[0]), " ".join(p[1:-2]), int(p[-2], 16), int(p[-1])))

addrs = sorted({r[2] for r in rows if r[1].lower() == "gt5.exe"})
names = {}
if addrs:
    text = "\n".join(hex(BASE + a) for a in addrs) + "\n"
    out = subprocess.run([SYM, "--obj=" + EXE, "--no-inlines", "--output-style=GNU", "--basenames"],
                         input=text, capture_output=True, text=True).stdout.split("\n")
    # GNU style prints two lines per address: function, then file:line.
    for i, a in enumerate(addrs):
        names[a] = out[i * 2].strip() if i * 2 < len(out) else "?"


def label(mod, rva):
    if mod.lower() == "gt5.exe":
        return names.get(rva, "?")
    return "[" + mod + "]"


total = collections.Counter()
per = collections.defaultdict(collections.Counter)
for tid, mod, rva, n in rows:
    per[tid][label(mod, rva)] += n
    # Idle threads sit in ntdll/kernel waits; those samples say nothing about load.
    total[label(mod, rva)] += n

busy = sorted(cpu, key=lambda t: -cpu[t])[:nthreads]
print("total cpu %d%% of one core across %d threads above 5%%" % (sum(v for v in cpu.values()), sum(1 for v in cpu.values() if v > 5)))
print("\n== busiest threads")
for t in busy:
    print("\nthread %d  guest %s  cpu %d%%" % (t, guest.get(t, "-"), cpu[t]))
    all_n = sum(per[t].values()) or 1
    for name, n in per[t].most_common(top):
        print("  %5.1f%%  %s" % (100.0 * n / all_n, name[:90]))
print("\n== across the busy threads")
merged = collections.Counter()
for t in cpu:
    if cpu[t] > 20:
        merged.update(per[t])
all_n = sum(merged.values()) or 1
for name, n in merged.most_common(top * 3):
    print("  %5.1f%%  %s" % (100.0 * n / all_n, name[:90]))
