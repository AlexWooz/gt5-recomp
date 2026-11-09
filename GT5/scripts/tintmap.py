import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Name the shaders in a LD_SHADER_TINT capture: tintmap.py <tag> [--out sheet.png]

Reads the scene surface dump, buckets its pixels by colour, maps colours to pipeline keys
through the [tint] log lines, and prints each key's share of the picture with where it sits.
"""
import collections
import glob
import os
import re
import sys

import numpy as np
from PIL import Image

ROOT = GT5_DIR
tag = sys.argv[1]


def read_ppm(p):
    d = open(p, "rb").read()
    f, pos = [], 0
    while len(f) < 4:
        while d[pos] in b" \t\r\n":
            pos += 1
        e = pos
        while d[e] not in b" \t\r\n":
            e += 1
        f.append(d[pos:e])
        pos = e
    w, h = int(f[1]), int(f[2])
    return Image.frombuffer("RGB", (w, h), d[pos + 1:pos + 1 + w * h * 3], "raw", "RGB", 0, 1)


fs = sorted(glob.glob(os.path.join(ROOT, "analysis", "surf_" + tag, "*_01980000_*.ppm")))
fs = [f for f in fs if "2304x1296" in f] or fs
img = read_ppm(fs[-1]).convert("RGB")
print(os.path.basename(fs[-1]), img.size)
a = np.asarray(img)
keys = collections.defaultdict(list)
for line in open(os.path.join(ROOT, "logs", tag + ".err"), errors="replace"):
    m = re.match(r"\[tint\] ([0-9A-F]{16}) (\d) (\d) (\d)", line)
    if m:
        keys[(int(m[2]), int(m[3]), int(m[4]))].append(m[1])
# Colour levels back from 8 bit.
r = np.rint(a[..., 0] / 255.0 * 7).astype(int)
g = np.rint(a[..., 1] / 255.0 * 7).astype(int)
b = np.rint(a[..., 2] / 255.0 * 3).astype(int)
code = r * 100 + g * 10 + b
vals, counts = np.unique(code, return_counts=True)
total = a.shape[0] * a.shape[1]
for v, c in sorted(zip(vals, counts), key=lambda x: -x[1])[:16]:
    rr, gg, bb = v // 100, (v // 10) % 10, v % 10
    ys, xs = np.where(code == v)
    print("%5.1f%% rgb=%d,%d,%d y=%d..%d x=%d..%d keys=%s" % (100.0 * c / total, rr, gg, bb, ys.min(), ys.max(), xs.min(), xs.max(), len(keys.get((rr, gg, bb), []))))
if "--out" in sys.argv:
    img.crop((0, 0, 2304, 1296)).resize((1152, 648)).save(sys.argv[sys.argv.index("--out") + 1])
