import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Skip to the attract demo and keep small pictures of it: demosheet.py <name> [count] [gap s] [start s]

Presses through the dialog and movie, waits for the demo, then saves <count> half-size frames from the
running game and a contact sheet at analysis\frames_lq\<name>_sheet.png.
"""
import glob
import os
import subprocess
import sys
import time

from PIL import Image

ROOT = GT5_DIR
GP = os.path.join(ROOT, "scripts", "gp.py")
name = sys.argv[1]
count = int(sys.argv[2]) if len(sys.argv) > 2 else 18
gap = float(sys.argv[3]) if len(sys.argv) > 3 else 9
start = float(sys.argv[4]) if len(sys.argv) > 4 else 150
snaps = os.path.join(ROOT, "analysis", "frames_lq")


def gp(*a):
    return subprocess.run(["python", GP, *a], capture_output=True, text=True)


time.sleep(30)
time.sleep(10)
gp("cross", "--wait", "20")
gp("cross", "--wait", "2")   # skips the movie to the title screen; a third press would enter My Home, which has no demo
time.sleep(start)
pics = []
for i in range(count):
    n = "%s_%02d" % (name, i)
    gp("--snap", n, "--wait", "0")
    p = os.path.join(snaps, n + ".png")
    if os.path.exists(p):
        pics.append(p)
    print(n, flush=True)
    time.sleep(gap)
if pics:
    w, h = 480, 270
    cols = 5
    rows = (len(pics) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * w, rows * h))
    for i, p in enumerate(pics):
        sheet.paste(Image.open(p).convert("RGB").resize((w, h)), ((i % cols) * w, (i // cols) * h))
    sheet.save(os.path.join(snaps, name + "_sheet.png"))
    print("sheet done", flush=True)
