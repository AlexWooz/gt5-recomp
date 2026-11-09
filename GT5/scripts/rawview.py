"""Turn raw guest frame dumps into PNGs: rawview.py <file> <out.png> <w> <h> <rgba|argb|y> [--stride n] [--scale n]"""
import sys

import numpy as np
from PIL import Image

path, out, w, h, kind = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
stride = int(sys.argv[sys.argv.index("--stride") + 1]) if "--stride" in sys.argv else None
scale = int(sys.argv[sys.argv.index("--scale") + 1]) if "--scale" in sys.argv else 2
raw = np.fromfile(path, dtype=np.uint8)
if kind == "y":
    s = stride or w
    a = raw[: s * h].reshape(h, s)[:, :w]
    print("luma min %d mean %.1f max %d" % (a.min(), a.mean(), a.max()))
    im = Image.fromarray(a)
else:
    s = stride or w * 4
    a = raw[: s * h].reshape(h, s)[:, : w * 4].reshape(h, w, 4)
    print("channels mean", [round(float(a[:, :, i].mean()), 1) for i in range(4)], "max", [int(a[:, :, i].max()) for i in range(4)])
    rgb = a[:, :, :3] if kind == "rgba" else a[:, :, 1:4]
    im = Image.fromarray(rgb)
im.resize((w // scale, h // scale)).save(out)
print(out)
