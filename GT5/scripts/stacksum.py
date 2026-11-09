import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Add up host call stacks of a running gt5.exe: stacksum.py [samples] [gap s] [--name text]

Runs hoststack.py repeatedly and, for each kind of thread, counts where it was: the ntdll leaf it sat in and the
first frame above ntdll. Shows whether "ntdll time" is idle waiting or contention on a lock.
"""
import collections
import re
import subprocess
import sys
import time

HS = (GT5_DIR + r"\scripts\hoststack.py")
n = int(sys.argv[1]) if len(sys.argv) > 1 else 20
gap = float(sys.argv[2]) if len(sys.argv) > 2 else 0.7
NT = re.compile(r"^(Zw|Nt|Rtl|Ldr|_NLG|KiUser|Wait|Sleep|Enter|Leave|Cnd_|Mtx_|Switch|Query|BaseThread|Etw|Tp)")
by_kind = collections.defaultdict(collections.Counter)
by_kind_top = collections.defaultdict(collections.Counter)
samples = collections.Counter()
for s in range(n):
    out = subprocess.run(["python", HS, "--all", "--depth", "12"], capture_output=True, text=True, errors="replace").stdout
    blocks = re.split(r"^== ", out, flags=re.M)[1:]
    for b in blocks:
        lines = b.splitlines()
        head = lines[0].split(None, 1)
        kind = head[1].strip() if len(head) > 1 else "(unnamed)"
        kind = re.sub(r"\d+$", "", kind).strip() or "(unnamed)"
        frames = [l.strip().split("  ")[0] for l in lines[1:] if l.strip()]
        if not frames:
            continue
        samples[kind] += 1
        leaf = re.sub(r"\+[0-9A-Fa-f]+$", "", frames[0])
        first = next((re.sub(r"\(.*$", "", f) for f in frames if not NT.match(f)), "?")
        by_kind[kind][(leaf if NT.match(frames[0]) else "(running)") + " <- " + first] += 1
    time.sleep(gap)
for kind, c in sorted(samples.items(), key=lambda kv: -kv[1]):
    print("== %s  (%d thread samples)" % (kind, c))
    for what, k in by_kind[kind].most_common(12):
        print("   %5.1f%%  %s" % (100.0 * k / c, what[:96]))
