"""Real frame rate across a LD_FRAME_TRACE run: ftblocks.py <err log>

A real frame is one with draws. Prints each 300-frame trace block's real frames per second and how uneven they were.
"""
import re
import sys

rows = []
for line in open(sys.argv[1], errors="replace"):
    m = re.match(r"\[ft\] gap=([\d.]+) walk=([\d.]+) idle=([\d.]+) gpuwait=([\d.]+) draws=(\d+) fullwaits=(\d+)", line)
    if m:
        rows.append(tuple(float(x) for x in m.groups()))
for i in range(0, len(rows), 300):
    b = rows[i:i + 300]
    real = [x for x in b if x[4] > 0]
    t = sum(x[0] for x in b) / 1000
    if not real or not t:
        continue
    g = sorted(x[0] for x in real)
    n = len(real)
    print("block %2d: %5.1f real fps, %3d empty of %3d | real frame ms p50 %.0f p90 %.0f p99 %.0f max %.0f | walk %.0f idle %.0f gpu %.1f draws %.0f waits %.2f" % (
        i // 300, n / t, len(b) - n, len(b), g[n // 2], g[n * 9 // 10], g[n * 99 // 100], g[-1],
        sum(x[1] for x in real) / n, sum(x[2] for x in real) / n, sum(x[3] for x in real) / n, sum(x[4] for x in real) / n, sum(x[5] for x in real) / n))
