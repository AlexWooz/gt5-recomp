import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Frame rate against draw count over a run, for comparing builds: fpscmp.py <tag> [<tag> ...] [--from n]

Prints fps/draws for each 5 s of the run, and the mean fps of the scenes with many draws.
"""
import re
import sys

args = sys.argv[1:]
start = int(args[args.index("--from") + 1]) if "--from" in args else 8
if "--from" in args:
    i = args.index("--from")
    del args[i:i + 2]
for tag in args:
    rows = []
    d = None
    for l in open((GT5_DIR + r"\logs\%s.err") % tag, errors="replace"):
        m = re.search(r"gpu-work\] per flip: draws=(\d+)", l)
        if m:
            d = int(m.group(1))
        m = re.search(r"\[fps\] ([\d.]+)", l)
        if m and d is not None:
            rows.append((float(m.group(1)), d))
    print(tag, " ".join("%.0f/%d" % r for r in rows[start:start + 44]))
    for lo, hi, name in ((900, 99999, "over 900 draws"), (400, 900, "400-900 draws")):
        v = [f for f, dr in rows if lo <= dr < hi]
        if v:
            print("   %s: %d windows, mean %.1f fps, min %.1f, max %.1f" % (name, len(v), sum(v) / len(v), min(v), max(v)))
