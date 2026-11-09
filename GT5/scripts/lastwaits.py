"""Each thread's last lwcond wait, from a run with PS3_LWC_BT=0: lastwaits.py <err log> [tid ...]"""
import re
import sys

last = {}
count = {}
tid = None
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    m = re.match(r"\[GSTACK:lwc\] sp=\S+ .*tid=(\d+)", line)
    if m:
        tid = int(m.group(1))
        count[tid] = count.get(tid, 0) + 1
    elif line.startswith("[GSTACK:lwc] chain:") and tid is not None:
        last[tid] = line.split("chain:", 1)[1].strip()
want = {int(a) for a in sys.argv[2:]}
for t in sorted(last):
    if not want or t in want:
        print(f"tid {t:3d} waits={count[t]:6d}: {last[t][:330]}")
