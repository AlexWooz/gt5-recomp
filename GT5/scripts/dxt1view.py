"""Show a raw DXT1 level as an image, from each block's first colour: dxt1view.py <raw> <w> <h> <out.png> [--be]"""
import sys

import numpy as np
from PIL import Image

d = open(sys.argv[1], "rb").read()
w, h = int(sys.argv[2]), int(sys.argv[3])
bw, bh = (w + 3) // 4, (h + 3) // 4
a = np.frombuffer(d[: bw * bh * 8], dtype="<u8" if "--be" not in sys.argv else ">u8").reshape(bh, bw)
c = (a & 0xFFFF).astype(np.uint32)
rgb = np.stack([((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31], axis=-1).astype(np.uint8)
print("mean rgb", rgb.reshape(-1, 3).mean(axis=0), "zero blocks %.1f%%" % (100.0 * (a == 0).mean()))
Image.fromarray(rgb).save(sys.argv[4])
