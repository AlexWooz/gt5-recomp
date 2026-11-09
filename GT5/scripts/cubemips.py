"""Do the reflection cube's mip rows change? cubemips.py <surface dump dir>

Reads the half-float 128x255 cube faces from each LD_SURF_DUMP frame and prints, for each face, the brightness of the
main image and of its mip rows, and whether they changed since the frame before. A row that never changes while the
main image does is a stale mip chain.
"""
import glob
import os
import re
import sys

import numpy as np

D = sys.argv[1]
FACES = ["04D40000", "04D7FC00", "04DBF800", "04DFF400", "04E3F000", "04E7EC00"]
ROWS = [(0, 128), (128, 192), (192, 224), (224, 240), (240, 248), (248, 255)]


def read(f):
    d = open(f, "rb").read()
    p = d.split(maxsplit=4)
    w, h = int(p[1]), int(p[2])
    return np.frombuffer(d[len(d) - w * h * 3:], dtype=np.uint8).reshape(h, w, 3).astype(int)


frames = sorted(set(re.match(r"(f\d+)_", os.path.basename(f)).group(1) for f in glob.glob(os.path.join(D, "f*_128x255.ppm"))))
print(len(frames), "frames")
for face in FACES:
    prev = None
    print("face", face)
    for fr in frames:
        fs = glob.glob(os.path.join(D, fr + "_*_" + face + "_128x255.ppm"))
        if not fs:
            continue
        a = read(fs[0])
        cells = " ".join("%3d" % a[y0:y1].max() for y0, y1 in ROWS)
        ch = "" if prev is None else " changed:" + "".join("Y" if (a[y0:y1] != prev[y0:y1]).any() else "." for y0, y1 in ROWS)
        print("  ", fr, cells, ch)
        prev = a
