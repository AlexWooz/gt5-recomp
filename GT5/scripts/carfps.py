import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Frame rate on the car screen under one setting, by fixed timings rather than by reading the screen:
carfps.py <tag> [NAME=VALUE ...]

Starts the game muted and behind other windows, presses through to car select, waits, prints the
[fps] and [drain] lines from there and saves a picture, then stops the game.
"""
import os
import re
import subprocess
import sys
import time

import numpy as np
from PIL import Image

ROOT = GT5_DIR
tag = sys.argv[1]
envs = dict(a.split("=", 1) for a in sys.argv[2:])
envs.setdefault("LD_FRAME_DUMP_MS", "2500")
hash_env = "@{ " + "; ".join('%s = "%s"' % kv for kv in envs.items()) + " }"
game = subprocess.Popen(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
                         "& '%s\\scripts\\liveplay.ps1' -Tag %s -Seconds 400 -Env %s" % (ROOT, tag, hash_env)],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def orange(pic, box):
    a = np.asarray(pic.crop(box), dtype=np.int16)
    return float(((a[..., 0] > 170) & (a[..., 1] > 80) & (a[..., 1] < 190) & (a[..., 2] < 90)).mean())


def gp(*args):
    subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "gp.py"), "--tag", tag] + list(args), stdout=subprocess.DEVNULL)


err = os.path.join(ROOT, "logs", tag + ".err")
t0 = time.time()
# The dialog: wait for the first frames, then a settled screen.
while not os.path.exists(err) or os.path.getsize(err) < 20000:
    time.sleep(1)
    if time.time() - t0 > 120:
        break
gp("--wait", "22", "--snap", "s0")
gp("cross", "--wait", "36", "--snap", "s1")  # into the movie
gp("cross", "--wait", "34", "--snap", "s2")  # skipped; top menu
# The menu ignores input during its opening animation, so press until the second icon is selected.
for attempt in range(12):
    gp("right", "--wait", "4", "--full", "--snap", "s3")
    pic = Image.open(os.path.join(ROOT, "analysis", "frames_" + tag, "s3.png")).convert("RGB")
    if orange(pic, (434, 622, 490, 660)) > 0.07 and orange(pic, (320, 622, 376, 660)) < 0.02:
        break
    gp("--wait", "5")
gp("cross", "--wait", "16", "--snap", "s4")  # Arcade
gp("cross", "--wait", "5")                   # Single Race
gp("cross", "--wait", "12", "--snap", "s5")  # difficulty; track select
gp("cross", "--wait", "16", "--snap", "s6")  # the track
gp("--wait", "30", "--snap", "s7")           # let car select settle
txt = open(err, errors="replace").read()
print(tag, envs)
print("\n".join(re.findall(r"\[fps\][^\n]*", txt)[-4:]))
print("\n".join(x[:110] for x in re.findall(r"\[drain\][^\n]*", txt)[-3:]))
print("\n".join(x[:100] for x in re.findall(r"\[gpu-work\] per flip[^\n]*", txt)[-1:]))
subprocess.run(["taskkill", "/F", "/IM", "gt5.exe"], capture_output=True)
game.wait(timeout=30)
