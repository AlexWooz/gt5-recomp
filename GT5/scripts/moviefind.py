import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Find a displayed frame in the movie stream by an independent decode: moviefind.py <stream> <shot.png> <out.png> [x0 y0 x1 y1]

The box is where the picture sits in the shot. Writes the shot and the best matching movie frame side by side.
"""
import subprocess
import sys

import numpy as np
from PIL import Image

FF = (BASE_DIR + r"\ffmpeg\ffmpeg-n9.0-latest-win64-lgpl-shared-9.0\bin\ffmpeg.exe")
stream, shot, out = sys.argv[1:4]
box = tuple(int(x) for x in sys.argv[4:8]) if len(sys.argv) > 7 else (64, 37, 1216, 684)
W, H = 240, 135
want = np.asarray(Image.open(shot).convert("RGB").crop(box).resize((W, H)), dtype=np.float32)
p = subprocess.Popen([FF, "-hide_banner", "-loglevel", "error", "-f", "mpeg", "-i", stream, "-map", "0:v:0", "-fps_mode", "passthrough",
                      "-vf", "scale=%d:%d" % (W, H), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], stdout=subprocess.PIPE)
best = (1e9, -1)
n = 0
size = W * H * 3
while True:
    d = p.stdout.read(size)
    if len(d) < size:
        break
    a = np.frombuffer(d, dtype=np.uint8).reshape(H, W, 3).astype(np.float32)
    e = float(np.abs(a - want).mean())
    if e < best[0]:
        best = (e, n)
    n += 1
print("frames %d, best frame %d, mean difference %.2f" % (n, best[1], best[0]))
tmp = out + ".ref.png"
subprocess.run([FF, "-hide_banner", "-loglevel", "error", "-y", "-f", "mpeg", "-i", stream, "-map", "0:v:0", "-vf",
                "select='eq(n\\,%d)',scale=960:540" % best[1], "-fps_mode", "passthrough", "-frames:v", "1", tmp])
sheet = Image.new("RGB", (1920, 540))
sheet.paste(Image.open(shot).convert("RGB").crop(box).resize((960, 540)), (0, 0))
sheet.paste(Image.open(tmp).convert("RGB"), (960, 0))
sheet.save(out)
print(out)
