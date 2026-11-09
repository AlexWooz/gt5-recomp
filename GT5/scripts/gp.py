import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Drive a running test game by hand and look at it: gp.py <button>[*n] ... [--wait s] [--snap name] [--tag lp] [--full]

    gp.py cross --wait 3 --snap gtlife      press Cross, wait 3 s, save analysis\\frames_<tag>\\<name>.png
    gp.py right*2 cross                     press Right twice, then Cross
    gp.py --snap now                        just look
    gp.py --state                           is it alive, frame rate, last log lines

Buttons: cross circle square triangle start select up down left right l1 r1 l2 r2.
Works with liveplay.ps1, which starts the game and removes old frame dumps. The picture is cropped to
the 1280x720 screen and halved unless --full.
"""
import glob
import os
import re
import subprocess
import sys
import time

from PIL import Image

ROOT = GT5_DIR
MASK = {"cross": 0x4000, "circle": 0x2000, "square": 0x8000, "triangle": 0x1000, "start": 0x0008, "select": 0x0001,
        "up": 0x0010, "right": 0x0020, "down": 0x0040, "left": 0x0080, "l2": 0x0100, "r2": 0x0200, "l1": 0x0400, "r1": 0x0800}
# On C:. The game opens this file on every controller poll, and on the USB drive an open waits
# behind the game's own streaming: 130 ms polls that stalled the main loop twice a second.
PAD = os.path.join(os.environ["LOCALAPPDATA"], "Temp", "gt5_pad.txt")


def opt(name, default=None):
    if name in sys.argv:
        i = sys.argv.index(name)
        v = sys.argv[i + 1]
        del sys.argv[i:i + 2]
        return v
    return default


tag = opt("--tag", "lq")
wait = float(opt("--wait", "1.5"))
snap = opt("--snap")
hold = int(opt("--polls", "40"))
full = "--full" in sys.argv
if full:
    sys.argv.remove("--full")
state = "--state" in sys.argv
if state:
    sys.argv.remove("--state")
frames = os.path.join(os.environ["LOCALAPPDATA"], "Temp", "gt5_frames_" + tag)
snaps = os.path.join(ROOT, "analysis", "frames_" + tag)
err = os.path.join(ROOT, "logs", tag + ".err")


def read_ppm(path):
    d = open(path, "rb").read()
    fields, pos = [], 0
    while len(fields) < 4:
        while d[pos] in b" \t\r\n":
            pos += 1
        end = pos
        while d[end] not in b" \t\r\n":
            end += 1
        fields.append(d[pos:end])
        pos = end
    w, h = int(fields[1]), int(fields[2])
    if len(d) - pos - 1 < w * h * 3:
        raise ValueError("partial")
    return Image.frombuffer("RGB", (w, h), d[pos + 1:pos + 1 + w * h * 3], "raw", "RGB", 0, 1)


def newest():
    for _ in range(40):
        fs = sorted(glob.glob(os.path.join(frames, "frame_*.ppm")), key=os.path.getmtime)
        for f in reversed(fs):
            try:
                return read_ppm(f), f
            except (ValueError, OSError, IndexError):
                continue
        time.sleep(0.25)
    return None, None


# --raw "<mask> <polls> <lx> <ly> <rx> <ry> <l2> <r2>": a pad line with sticks and triggers, each 0-255.
raw = opt("--raw")
if raw:
    with open(PAD, "w") as f:
        f.write(raw)
    time.sleep(0.2)
for b in sys.argv[1:]:
    name, _, n = b.partition("*")
    for _ in range(int(n or 1)):
        with open(PAD, "w") as f:
            f.write("0x%04X %d" % (MASK[name.lower()], hold))
        time.sleep(0.7)
if wait:
    time.sleep(wait)
if state:
    alive = subprocess.run(["tasklist", "/FI", "IMAGENAME eq gt5.exe"], capture_output=True, text=True).stdout
    print("alive" if "gt5.exe" in alive else "NOT RUNNING")
    txt = open(err, errors="replace").read()[-60000:]
    print("".join(re.findall(r"\[fps\][^\n]*\n", txt)[-2:]).strip()[:200])
    for line in re.findall(r"\[(?:CRASH|hle\] unresolved|taskset\] task|spu_workload\] async dispatch MISS)[^\n]*", txt)[-8:]:
        print(line[:150])
if snap:
    img, f = newest()
    if img is None:
        print("no frame")
    else:
        os.makedirs(snaps, exist_ok=True)
        pic = img.crop((0, 0, 1280, 720))
        if not full:
            pic = pic.resize((960, 540))
        out = os.path.join(snaps, snap + ".png")
        pic.save(out)
        print(out)
