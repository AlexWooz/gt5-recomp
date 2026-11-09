"""Contact sheet of raw I420 dumps from VDEC_DUMP: yuvview.py <dir> <out.png> [--every n] [--cols n]"""
import glob
import os
import re
import sys

import numpy as np
from PIL import Image

d, out = sys.argv[1], sys.argv[2]
every = int(sys.argv[sys.argv.index("--every") + 1]) if "--every" in sys.argv else 1
cols = int(sys.argv[sys.argv.index("--cols") + 1]) if "--cols" in sys.argv else 4
names = sorted(glob.glob(os.path.join(d, "*.yuv")))[::every]
tiles = []
for n in names:
    m = re.search(r"(\d+)_(\d+)x(\d+)\.yuv$", n)
    w, h = int(m.group(2)), int(m.group(3))
    raw = np.fromfile(n, dtype=np.uint8)
    cw, ch = (w + 1) // 2, (h + 1) // 2
    y = raw[: w * h].reshape(h, w).astype(np.float32)
    u = raw[w * h: w * h + cw * ch].reshape(ch, cw).astype(np.float32)
    v = raw[w * h + cw * ch: w * h + 2 * cw * ch].reshape(ch, cw).astype(np.float32)
    u = np.repeat(np.repeat(u, 2, 0), 2, 1)[:h, :w] - 128
    v = np.repeat(np.repeat(v, 2, 0), 2, 1)[:h, :w] - 128
    yy = (y - 16) * 1.164
    rgb = np.stack([yy + 1.793 * v, yy - 0.213 * u - 0.533 * v, yy + 2.112 * u], 2).clip(0, 255).astype(np.uint8)
    print(os.path.basename(n), "luma min %d mean %.1f max %d" % (y.min(), y.mean(), y.max()))
    tiles.append(Image.fromarray(rgb).resize((480, 270)))
rows = (len(tiles) + cols - 1) // cols
sheet = Image.new("RGB", (cols * 480, rows * 270))
for i, t in enumerate(tiles):
    sheet.paste(t, ((i % cols) * 480, (i // cols) * 270))
sheet.save(out)
print(out, len(tiles))
