import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Grab tinted scene surfaces from a LD_SHADER_TINT run: tintgrab.py <surf dir> <count> [gap s]

Opens the LD_SURF_DUMP gate, waits for the dump, closes it, and saves each scene surface as
analysis\\frames_lq\\tint_<n>.png at half size. tintgrab.py --key <surf ppm> <x> <y> <err log> names the shader at a
point given in that half-size picture.
"""
import glob
import os
import re
import sys
import time

from PIL import Image

OUT = (GT5_DIR + r"\analysis\frames_lq")


def read_ppm(path):
    d = open(path, "rb").read()
    p = d.split(maxsplit=4)
    w, h = int(p[1]), int(p[2])
    return Image.frombuffer("RGB", (w, h), d[len(d) - w * h * 3:], "raw", "RGB", 0, 1)


if sys.argv[1] == "--key":
    im = read_ppm(sys.argv[2])
    x, y = int(sys.argv[3]) * 2, int(sys.argv[4]) * 2
    r, g, b = im.getpixel((x, y))
    code = (round(r / 255 * 7), round(g / 255 * 7), round(b / 255 * 3))
    keys = set()
    for line in open(sys.argv[5], errors="replace"):
        m = re.match(r"\[tint\] ([0-9A-F]{16}) (\d) (\d) (\d)", line)
        if m and (int(m[2]), int(m[3]), int(m[4])) == code:
            keys.add(m[1])
    print((r, g, b), code, sorted(keys))
    sys.exit()

d = sys.argv[1]
gate = os.path.join(d, "gate")
gap = float(sys.argv[3]) if len(sys.argv) > 3 else 8
for n in range(int(sys.argv[2])):
    before = set(glob.glob(os.path.join(d, "*01980000_2560x2592.ppm")))
    open(gate, "w").close()
    for _ in range(80):
        time.sleep(0.5)
        new = set(glob.glob(os.path.join(d, "*01980000_2560x2592.ppm"))) - before
        if new:
            break
    time.sleep(3)
    os.remove(gate)
    if not new:
        print("no dump")
        continue
    f = sorted(new)[-1]
    im = read_ppm(f).crop((0, 0, 2304, 1296)).resize((1152, 648), Image.NEAREST)
    out = os.path.join(OUT, "tint_%d.png" % n)
    im.save(out)
    print(out, os.path.basename(f))
    time.sleep(gap)
