"""Read an AUDIO_RINGLOG file: ringlog.py <file> [--port n] [--from sec] [--to sec] [--dump a,b]

Prints the writer's lead over the read cursor, the time between blocks, and every run of
blocks read while empty. --dump prints the raw lines around those times (seconds from start).
"""
import collections
import sys


def arg(n, d=None):
    return sys.argv[sys.argv.index(n) + 1] if n in sys.argv else d


rows = []
for line in open(sys.argv[1]):
    f = line.split()
    if len(f) < 5:
        continue
    rows.append((int(f[0]), int(f[1]), int(f[2]), int(f[3]), int(f[4]), f[5] if len(f) > 5 else ""))
ports = collections.Counter(r[1] for r in rows)
print("ports", dict(ports))
want = int(arg("--port", ports.most_common(1)[0][0]))
rows = [r for r in rows if r[1] == want]
t0 = rows[0][0]
lo, hi = float(arg("--from", 0)), float(arg("--to", 1e9))
rows = [r for r in rows if lo <= (r[0] - t0) / 1e6 <= hi]
print("blocks", len(rows), "span %.1f s" % ((rows[-1][0] - rows[0][0]) / 1e6), "ring", len(rows[0][5]))
leads = collections.Counter(r[4] for r in rows)
print("lead", " ".join("%d:%.1f%%" % (k, 100.0 * leads[k] / len(rows)) for k in sorted(leads)))
gaps = [b[0] - a[0] for a, b in zip(rows, rows[1:])]
hist = collections.Counter(min(g // 1000, 40) for g in gaps)
print("ms between blocks", " ".join("%d:%d" % (k, hist[k]) for k in sorted(hist)))
runs = []
i = 0
while i < len(rows):
    if rows[i][4] == 0:
        j = i
        while j + 1 < len(rows) and rows[j + 1][4] == 0:
            j += 1
        runs.append((i, j))
        i = j + 1
    else:
        i += 1
lens = collections.Counter(b - a + 1 for a, b in runs)
print("empty runs", len(runs), "lengths", " ".join("%d:%d" % (k, lens[k]) for k in sorted(lens)))
print("empty blocks %.1f%%" % (100.0 * sum(b - a + 1 for a, b in runs) / len(rows)))
for a, b in runs[:12]:
    print("  at %.3f s, %d blocks, slot %d..%d, lead before: %s" % (
        (rows[a][0] - t0) / 1e6, b - a + 1, rows[a][3], rows[b][3], " ".join(str(r[4]) for r in rows[max(0, a - 12):a])))
if "--dump" in sys.argv:
    x, y = [float(v) for v in arg("--dump").split(",")]
    for r in rows:
        if x <= (r[0] - t0) / 1e6 <= y:
            print("%9.3f r=%d slot=%2d lead=%2d %s" % ((r[0] - t0) / 1e6, r[2], r[3], r[4], r[5]))
