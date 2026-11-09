"""Frame rate against draw load in a run's race: racefps.py <err log> ... [--last n]

Takes the last n windows of each log (default 30, about 150 s), so a menu before the race doesn't count. Prints the
median frame rate per band of draws per flip, and overall.
"""
import re
import sys

last = 30
if "--last" in sys.argv:
    i = sys.argv.index("--last")
    last = int(sys.argv[i + 1])
    del sys.argv[i:i + 2]


def rows(path):
    out, fps = [], None
    for line in open(path, errors="replace"):
        m = re.match(r"\[fps\] ([\d.]+)", line)
        if m:
            fps = float(m[1])
        m = re.match(r"\[gpu-work\] per flip: draws=(\d+) verts=(\d+)", line)
        if m and fps is not None:
            out.append((int(m[1]), int(m[2]), fps))
    return out[-last:]


for path in sys.argv[1:]:
    r = rows(path)
    bands = {}
    for d, _, f in r:
        bands.setdefault(min(d // 200 * 200, 1200), []).append(f)
    med = lambda v: sorted(v)[len(v) // 2]
    print(path.split("\\")[-1].split("/")[-1], "windows", len(r), "median fps %.1f" % med([x[2] for x in r]), "median draws %d" % med([x[0] for x in r]),
          {k: (len(v), med(v)) for k, v in sorted(bands.items())})
