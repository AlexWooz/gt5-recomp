"""Check layouts of dumped 3D textures: volview.py <dir> [out.png]

For A8R8G8B8 dumps, tries 3D Morton orders and plain slices, scores each by how smooth neighbouring texels are, and
saves a sheet of slices in the best one. For DXT dumps, decodes slices stored one after another (D3D) and VTC's
4-slice groups (blocks interleaved in z), and scores those the same way.
"""
import glob
import os
import re
import struct
import sys

import numpy as np
from PIL import Image


def morton3(w, h, d):
    """Offsets of each (z,y,x) with x,y,z bits interleaved, exhausted axes skipped: x first."""
    lw, lh, ld = [int(np.log2(v)) for v in (w, h, d)]
    off = np.zeros((d, h, w), dtype=np.int64)
    for z in range(d):
        for y in range(h):
            for x in range(w):
                o = 0
                bit = 0
                for i in range(max(lw, lh, ld)):
                    if i < lw:
                        o |= ((x >> i) & 1) << bit
                        bit += 1
                    if i < lh:
                        o |= ((y >> i) & 1) << bit
                        bit += 1
                    if i < ld:
                        o |= ((z >> i) & 1) << bit
                        bit += 1
                off[z, y, x] = o
    return off


def score(a):
    """Mean absolute difference between neighbours along each axis; smaller is smoother."""
    a = a.astype(np.int32)
    return tuple(float(np.abs(np.diff(a, axis=ax)).mean()) for ax in (0, 1, 2))


def dxt_colors(c0, c1):
    def rgb(c):
        return np.array([((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31], dtype=np.int32)

    a, b = rgb(c0), rgb(c1)
    if c0 > c1:
        return [a, b, (2 * a + b) // 3, (a + 2 * b) // 3]
    return [a, b, (a + b) // 2, np.zeros(3, dtype=np.int32)]


def dxt_block(data, off, bytes_per):
    if bytes_per == 16:
        off += 8
    c0, c1, bits = struct.unpack_from("<HHI", data, off)
    cols = dxt_colors(c0, c1)
    out = np.zeros((4, 4, 3), dtype=np.int32)
    for py in range(4):
        for px in range(4):
            out[py, px] = cols[(bits >> (2 * (py * 4 + px))) & 3]
    return out


def dxt_volume(data, w, h, d, bytes_per, order):
    bw, bh = w // 4, h // 4
    vol = np.zeros((d, h, w, 3), dtype=np.int32)
    for z in range(d):
        for by in range(bh):
            for bx in range(bw):
                if order == "slices":
                    idx = z * bh * bw + by * bw + bx
                else:  # vtc: groups of 4 slices, each 4x4 cell holding its 4 blocks in z order
                    group, within = z // 4, z % 4
                    groups_before = group * 4 * bh * bw
                    idx = groups_before + (by * bw + bx) * 4 + within
                vol[z, by * 4:by * 4 + 4, bx * 4:bx * 4 + 4] = dxt_block(data, idx * bytes_per, bytes_per)
    return vol


def sheet(vol, path):
    d, h, w = vol.shape[:3]
    scale = max(1, 128 // max(w, h)) * 3
    im = Image.new("RGB", (4 * (w * scale + 4), ((d + 3) // 4) * (h * scale + 4)), (40, 0, 40))
    for z in range(d):
        s = Image.fromarray(vol[z].astype(np.uint8), "RGB").resize((w * scale, h * scale), Image.NEAREST)
        im.paste(s, ((z % 4) * (w * scale + 4), (z // 4) * (h * scale + 4)))
    im.save(path)


out_dir = sys.argv[2] if len(sys.argv) > 2 else sys.argv[1]
for f in sorted(glob.glob(os.path.join(sys.argv[1], "vol_*.bin"))):
    m = re.search(r"f([0-9A-F]{2})_(\d+)x(\d+)x(\d+)", f)
    fmt, w, h, d = int(m[1], 16), int(m[2]), int(m[3]), int(m[4])
    data = open(f, "rb").read()
    print(os.path.basename(f), len(data))
    base = os.path.splitext(os.path.basename(f))[0]
    if fmt == 0x85:
        raw = np.frombuffer(data, dtype=np.uint8).reshape(-1, 4)
        off = morton3(w, h, d)
        for name, order in [("morton", off), ("slices", np.arange(w * h * d).reshape(d, h, w))]:
            vol = raw[order]
            print("   %-7s rgb smooth z,y,x = %s  alpha %s" % (name, ["%.1f" % v for v in score(vol[..., 1:])], ["%.1f" % v for v in score(vol[..., :1])]))
        vol = raw[off][..., 1:]
        print("   channels A %s R %s G %s B %s" % tuple((int(raw[:, i].min()), int(raw[:, i].max())) for i in range(4)))
        sheet(vol, os.path.join(out_dir, base + "_morton.png"))
    elif fmt in (0x86, 0x88):
        bp = 8 if fmt == 0x86 else 16
        for order in ("slices", "vtc"):
            vol = dxt_volume(data, w, h, d, bp, order)
            print("   %-7s rgb smooth z,y,x = %s" % (order, ["%.1f" % v for v in score(vol)]))
            sheet(vol, os.path.join(out_dir, base + "_" + order + ".png"))
