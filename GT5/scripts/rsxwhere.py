import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""What the RSX thread is doing right now, sampled: rsxwhere.py [samples] [gap seconds]

Runs hoststack.py several times and tallies the top frames of the thread running vblank_ticker.
"""
import collections
import subprocess
import sys
import time

n = int(sys.argv[1]) if len(sys.argv) > 1 else 8
gap = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5
tally = collections.Counter()
for _ in range(n):
    out = subprocess.run(["python", "-I", (GT5_DIR + r"\scripts\hoststack.py"), "--all", "--depth", "24"],
                         capture_output=True, text=True, errors="replace").stdout
    for block in out.split("== ")[1:]:
        lines = [l.strip() for l in block.splitlines()[1:]]
        if any("vblank_ticker" in l for l in lines):
            top = [l.split("  ")[0].split("+")[0] for l in lines[:6]]
            tally[" < ".join(top)] += 1
    time.sleep(gap)
for k, v in tally.most_common():
    print(v, k[:170])
