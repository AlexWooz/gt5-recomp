"""Summarize a LD_FRAME_TRACE run: ftsum.py <err log> [--tail n]

Groups the last trace block's frames by length and shows what each group spent its time on, then prints a stretch of
frames in order, because a repeating pattern (fast, fast, slow) points at something the game does on a cycle.
"""
import re
import sys

tail = 300
if "--tail" in sys.argv:
    i = sys.argv.index("--tail")
    tail = int(sys.argv[i + 1])
    del sys.argv[i:i + 2]
rows = []
for line in open(sys.argv[1], errors="replace"):
    m = re.match(r"\[ft\] gap=([\d.]+) walk=([\d.]+) idle=([\d.]+) gpuwait=([\d.]+) draws=(\d+) fullwaits=(\d+)", line)
    if m:
        rows.append(tuple(float(x) for x in m.groups()))
rows = rows[-tail:]
print(len(rows), "frames")
bands = [("<20 ms", 0, 20), ("20-37", 20, 37), ("37-54", 37, 54), ("54-100", 54, 100), (">100", 100, 1e9)]
print("%-8s %5s | avg: %6s %6s %6s %8s %6s %10s" % ("frame", "count", "gap", "walk", "idle", "gpuwait", "draws", "fullwaits"))
for name, lo, hi in bands:
    r = [x for x in rows if lo <= x[0] < hi]
    if not r:
        continue
    n = len(r)
    print("%-8s %5d |      %6.1f %6.1f %6.1f %8.1f %6.0f %10.2f" % (name, n, sum(x[0] for x in r) / n, sum(x[1] for x in r) / n,
          sum(x[2] for x in r) / n, sum(x[3] for x in r) / n, sum(x[4] for x in r) / n, sum(x[5] for x in r) / n))
print("in order (gap ms / draws):")
print(" ".join("%.0f/%.0f" % (x[0], x[4]) for x in rows[-90:]))
