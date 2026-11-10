"""Follow gt5.exe's screen and press pad buttons when the expected menu is up.

    screendrive.py --dir <LD_FRAME_DUMP dir> --pad logs/pad.txt [--race] [--log file]
    screendrive.py --check <dir of old PNG frames>     shows which frames match each step
    screendrive.py --make-refs <analysis dir>          cuts scripts/refs/*.png from a good run

Each step waits for a crop of the screen to match its reference, presses, and moves on once
the screen has left that state. Run with LD_FRAME_DUMP_MS so frames keep coming in slow
scenes. Dumps are read and deleted, keeping a PNG per step and one every --keep seconds, so
a run leaves a record without filling the disk.
"""
import argparse
import glob
import os
import subprocess
import sys
import time

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REFS = os.path.join(HERE, "refs")
CROSS, RIGHT = 0x4000, 0x0020
MASK = {"cross": 0x4000, "circle": 0x2000, "square": 0x8000, "triangle": 0x1000, "start": 0x0008, "select": 0x0001,
        "up": 0x0010, "right": 0x0020, "down": 0x0040, "left": 0x0080, "l2": 0x0100, "r2": 0x0200, "l1": 0x0400, "r1": 0x0800}
# name, source frame for the reference, crop in the 1280x720 picture, press, polls, largest difference
STEPS = [
    ("dialog", "frames_p1/frame_000097.png", (360, 184, 920, 544), CROSS, 16, 10),
    ("top_gt", "frames_p1/frame_000582.png", (280, 530, 420, 680), RIGHT, 16, 24),
    ("top_arc", "frames_p1/frame_000776.png", (280, 530, 560, 680), CROSS, 16, 24),
    ("arcade", "frames_p1/frame_001164.png", (360, 500, 920, 640), CROSS, 16, 5),
    ("level", "frames_p1/frame_001940.png", (360, 500, 920, 640), CROSS, 16, 5),
    ("track", "frames_p1/frame_002522.png", (0, 620, 1280, 720), CROSS, 16, 3),
    ("car", "frames_p1/frame_003104.png", (0, 620, 1280, 720), CROSS, 16, 3),
    ("color", "frames_ref/color.png", (480, 640, 900, 690), CROSS, 16, 6),
    ("car_ok", "frames_p1/frame_003201.png", (500, 500, 780, 610), CROSS, 16, 8),
    ("prerace", "frames_live2/frame_005141.png", (440, 600, 840, 690), CROSS, 16, 12),
]
REF_CACHE = {}


def thumb(img):
    return np.asarray(img.convert("L"), dtype=np.int16)[::2, ::2]


# The top menu's selected icon sits on an orange base; the 3D scene behind it changes, this doesn't.
ICON_BASE = {"top_gt": (320, 622, 376, 660), "top_arc": (434, 622, 490, 660)}


def orange(img, box):
    a = np.asarray(img.convert("RGB").crop(box), dtype=np.int16)
    hit = (a[..., 0] > 170) & (a[..., 1] > 80) & (a[..., 1] < 190) & (a[..., 2] < 90)
    return float(hit.mean())


def diff(img, step):
    if step[0] in ICON_BASE:
        # A difference score, so the callers treat it like the others: small when selected.
        # Only one icon is ever selected: an orange scene behind the menu lights up both.
        other = ICON_BASE["top_gt" if step[0] == "top_arc" else "top_arc"]
        return 0.0 if orange(img, ICON_BASE[step[0]]) > 0.07 and orange(img, other) < 0.02 else 30.0
    ref = REF_CACHE.get(step[0])
    if ref is None:
        ref = REF_CACHE[step[0]] = thumb(Image.open(os.path.join(REFS, step[0] + ".png")))
    return float(np.abs(thumb(img.crop(step[2])) - ref).mean())


def make_refs(analysis):
    os.makedirs(REFS, exist_ok=True)
    for s in STEPS:
        Image.open(os.path.join(analysis, s[1])).convert("L").crop(s[2]).save(os.path.join(REFS, s[0] + ".png"))
    print("refs written")


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


def newest(dirname):
    """The newest complete frame; older dumps are deleted."""
    fs = sorted(glob.glob(os.path.join(dirname, "frame_*.ppm")))
    for f in fs[:-1]:
        try:
            os.remove(f)
        except OSError:
            pass
    if not fs:
        return None
    try:
        return read_ppm(fs[-1])
    except (ValueError, OSError, IndexError):
        return None


TEST_EXE = os.path.join(os.path.dirname(HERE), "build", "gt5.exe")


def stop_test_game():
    """Stop only the test build (GT5\build\gt5.exe here); a gt5.exe from anywhere else is the player's own game."""
    ps = ("Get-CimInstance Win32_Process -Filter \"Name='gt5.exe'\" | "
          "Where-Object { $_.ExecutablePath -and $_.ExecutablePath -ieq '%s' } | "
          "ForEach-Object { Stop-Process -Id $_.ProcessId -Force }" % TEST_EXE)
    subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True)


def press(pad, mask, polls):
    with open(pad, "w") as f:
        f.write("0x%04X %d" % (mask, polls))


def drive(a):
    log = open(a.log, "a", buffering=1) if a.log else sys.stdout
    t0 = time.time()

    def say(msg):
        log.write("%6.0f %s\n" % (time.time() - t0, msg))

    def profile(name):
        say("profile " + name)
        subprocess.Popen(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", os.path.join(HERE, "prof.ps1"),
                          "-Seconds", "12", "-Out", "%s_%s.txt" % (a.prof, name)], stdout=subprocess.DEVNULL)

    def capture(name):
        # The runtime's RSX_DRAW_CSV_GATE / LD_SURF_DUMP_GATE act while this file exists.
        say("capture " + name)
        open(a.gate, "w").close()
        time.sleep(a.gate_secs)
        os.remove(a.gate)

    i, pressed_at, last_keep, armed, done_at = 0, 0.0, 0.0, False, 0.0
    captured = False
    race_at, profiled = 0.0, False
    tries = {}
    left_at = 0.0
    skipped = 0.0
    while time.time() - t0 < a.timeout:
        time.sleep(0.25)
        img = newest(a.dir)
        if img is None:
            continue
        now = time.time()
        if now - last_keep >= a.keep:
            last_keep = now
            img.save(os.path.join(a.dir, "frame_t%05d.png" % (now - t0)))
        # The opening movie's frames can look like a menu (an orange steel mill matches the selected
        # icon), so past the first dialog nothing is read until the movie has been skipped.
        if a.skip_movie and i > 0 and not skipped:
            # Counted from the dialog as well as from launch: a slow boot shows the dialog as late as 60 s, a press
            # before the movie has started is lost, and its orange frames then fool the icon test below.
            if now - t0 >= a.skip_movie and now - left_at >= 35:
                say("skip the movie, press 0x%04X" % CROSS)
                press(a.pad, CROSS, 16)
                skipped = now
                pressed_at = now
            continue
        if skipped and now - skipped < 12:  # the movie stays up while the player stops
            continue
        # A skip press that landed before the movie took input is lost, and the movie then plays for
        # minutes (its frames capture black). Still black 20 s after a press, with no step reached:
        # press again, up to three more times.
        if skipped and i == 1 and tries.get("reskip", 0) < 3 and now - pressed_at >= 20:
            if float(np.asarray(img.convert("L"), dtype=np.float32).mean()) < 2.0:
                tries["reskip"] = tries.get("reskip", 0) + 1
                say("still black after the skip, press 0x%04X again" % CROSS)
                press(a.pad, CROSS, 16)
                pressed_at = now
                continue
        if i < len(STEPS):
            s = STEPS[i]
            d = diff(img, s)
            if i + 1 < len(STEPS) and diff(img, STEPS[i + 1]) <= STEPS[i + 1][5]:
                say("at %s already, past %s" % (STEPS[i + 1][0], s[0]))
                i, armed, pressed_at = i + 1, False, 0.0
                continue
            # A press lost during a load: the screen came back to the previous step.
            if i and s[0] != "top_gt" and d > s[5] and diff(img, STEPS[i - 1]) <= STEPS[i - 1][5] and now - left_at >= 8:
                say("back at %s" % STEPS[i - 1][0])
                i, armed, pressed_at = i - 1, False, 0.0
                continue
            # The top menu ignores input while it animates in, and then idles into the attract demo.
            # Only while the cursor still sits on GT Mode, and never after Cross has gone in: a late Right
            # overwrites the Cross in the pad file or pushes the cursor past Arcade.
            if (s[0] == "top_arc" and not armed and d > s[5] and now - left_at >= 15 and tries.get("top_arc_right", 0) < 2
                    and diff(img, STEPS[i - 1]) <= STEPS[i - 1][5]):
                tries["top_arc_right"] = tries.get("top_arc_right", 0) + 1
                say("top_arc not reached, press right again")
                press(a.pad, RIGHT, 16)
                left_at = now
                continue
            if d <= s[5]:
                # The top menu's icon bar fades about 10 s after the last press, so don't wait out the usual gap there.
                if now - pressed_at >= (3 if s[0] == "top_arc" else a.gap):
                    say("%s matched (%.1f), press 0x%04X" % (s[0], d, s[3]))
                    if s[0] == "color" and a.gate and a.capture in ("car", "both"):
                        capture("car")
                    if s[0] == "car_ok" and a.prof:
                        profile("car")
                        time.sleep(14)
                    img.save(os.path.join(a.dir, "step_%02d_%s.png" % (i, s[0])))
                    tries[i] = tries.get(i, 0) + 1
                    if tries[i] in (5, 9) and a.prof:
                        # The game isn't taking this press: record what every thread is doing.
                        say("stuck at %s, recording stacks" % s[0])
                        with open(a.prof + "_stuck%d.txt" % tries[i], "w") as out:
                            subprocess.run([sys.executable, os.path.join(HERE, "gstack.py"), "--depth", "12"], stdout=out, stderr=subprocess.STDOUT)
                        profile("stuck%d" % tries[i])
                    if s[0] == "track" and a.track_keys and not tries.get("track_keys"):
                        # Another track: move the cursor first, keeping a frame after each press.
                        tries["track_keys"] = 1
                        for n, key in enumerate(a.track_keys.split(",")):
                            say("track key %s" % key)
                            if key != "wait":   # "wait": a pause, e.g. while the list first appears and ignores input
                                press(a.pad, MASK[key], 16)
                            time.sleep(a.key_gap)
                            shot = newest(a.dir)
                            if shot is not None:
                                shot.save(os.path.join(a.dir, "step_05_key%02d_%s.png" % (n, key)))
                        if a.explore:
                            say("explored the track list, stopping the game")
                            stop_test_game()
                            return
                    press(a.pad, s[3], s[4])
                    pressed_at, armed = now, True
                    if s[3] == RIGHT:  # stays on the same screen, with the cursor moved
                        i, armed = i + 1, False
                        left_at = now
            elif armed and d > s[5] + 4:
                say("%s left (%.1f)" % (s[0], d))
                left_at = now
                i, armed = i + 1, False
                pressed_at = 0.0
                if i == len(STEPS):
                    done_at = now
        elif a.race and now - done_at >= 30 and now - pressed_at >= 30:
            # Past the start flag: hold the accelerator, repeating so the hold survives loads.
            say("accelerate")
            press(a.pad, CROSS, 4000)
            pressed_at = now
            race_at = race_at or now
        if race_at and a.gate and a.capture in ("race", "both") and not captured and now - race_at >= 20:
            capture("race")
            captured = True
        if race_at and a.prof and not profiled and now - race_at >= 30:
            profile("race")
            profiled = True
        if race_at and a.end_after and now - race_at >= a.end_after:
            say("done, stopping the game")
            stop_test_game()
            return


def check(dirname):
    print("             " + " ".join(s[0][:5].rjust(5) for s in STEPS))
    for f in sorted(glob.glob(os.path.join(dirname, "frame_*.png"))):
        img = Image.open(f).convert("RGB")
        ds = [diff(img, s) for s in STEPS]
        best = int(np.argmin(ds))
        mark = STEPS[best][0] if ds[best] <= STEPS[best][5] else "-"
        print(os.path.basename(f)[6:-4], mark.ljust(8)[:8], " ".join("%5.1f" % x for x in ds))


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--dir")
    p.add_argument("--pad")
    p.add_argument("--log")
    p.add_argument("--race", action="store_true")
    p.add_argument("--check")
    p.add_argument("--make-refs")
    p.add_argument("--keep", type=float, default=10.0)
    p.add_argument("--gap", type=float, default=12.0, help="seconds between presses of one step")
    p.add_argument("--timeout", type=float, default=900.0)
    p.add_argument("--prof", help="prefix for prof.ps1 samples of car select and the race")
    p.add_argument("--gate", help="file to create while capturing")
    p.add_argument("--gate-secs", type=float, default=6.0)
    p.add_argument("--skip-movie", type=float, default=0, help="press Cross at this many seconds to skip the opening movie; menus are not read before it")
    p.add_argument("--capture", default="", help="car, race or both")
    p.add_argument("--end-after", type=float, default=0, help="stop the game this long after the race starts")
    p.add_argument("--track-keys", default="", help="buttons pressed on the track list before Cross, e.g. up,right,down;"
                   " wait pauses. The first press only wakes the cursor. Tsukuba: wait,wait,up,wait,select,right,cross,wait,wait,select,right,right,right,right")
    p.add_argument("--key-gap", type=float, default=2.5, help="seconds after each --track-keys press")
    p.add_argument("--explore", action="store_true", help="stop the game after the --track-keys presses")
    a = p.parse_args()
    if a.make_refs:
        make_refs(a.make_refs)
    elif a.check:
        check(a.check)
    else:
        drive(a)
