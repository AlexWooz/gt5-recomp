import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Feed made-up DualSense reports through the real parser and check what the game receives.

Starts a muted test run behind other windows on test_save, with PAD_NO_DUALSENSE so the real pad
stays out of it, and holds each report for a while. PAD_TRACE lines in logs/padsim.err show the
payload cellPadGetData handed the game. Nothing is sent to a controller.
"""
import os
import re
import subprocess
import sys
import tempfile
import time

ROOT = GT5_DIR
ERR = os.path.join(ROOT, "logs", "padsim.err")
OUT = os.path.join(ROOT, "logs", "padsim.out")
REP = os.path.join(tempfile.gettempdir(), "gt5_padsim_report.hex")


def report(kind, lx=128, ly=128, rx=128, ry=128, l2=0, r2=0, b0=8, b1=0):
    if kind == "usb":
        r = bytearray(64)
        r[0:9] = bytes([0x01, lx, ly, rx, ry, l2, r2, 1, b0])
        r[9] = b1
    elif kind == "bt_simple":      # what Windows returns: the 10-byte report padded to 78
        r = bytearray(78)
        r[0:10] = bytes([0x01, lx, ly, rx, ry, b0, b1, 0, l2, r2])
    elif kind == "bt_full":
        r = bytearray(78)
        r[0:11] = bytes([0x31, 0x41, lx, ly, rx, ry, l2, r2, 1, b0, b1])
    return bytes(r)


# name, report, what the game should get
CASES = [
    ("usb: Cross, part R2, stick left", report("usb", lx=0x40, l2=32, r2=144, b0=0x28, b1=0x08),
     dict(d1=0x00, d2=0x42, L=(0x40, 128), l2=32, r2=144, cross=255)),
    ("usb: nothing", report("usb"), dict(d1=0, d2=0, l2=0, r2=0)),
    ("bluetooth simple: Square, part L2, stick up", report("bt_simple", ly=0x10, l2=85, b0=0x18, b1=0x04),
     dict(d1=0x00, d2=0x81, L=(128, 0x10), l2=85, r2=0, square=255)),
    ("bluetooth simple: nothing", report("bt_simple"), dict(d1=0, d2=0, l2=0, r2=0)),
    ("bluetooth full: D-pad right, part R2", report("bt_full", r2=96, b0=0x02, b1=0x08),
     dict(d1=0x20, d2=0x02, l2=0, r2=96)),
    ("bluetooth full: Cross, Options", report("bt_full", b0=0x28, b1=0x20),
     dict(d1=0x08, d2=0x40, l2=0, r2=0, cross=255)),
    ("bluetooth full: light R2 touch", report("bt_full", r2=20, b1=0x08),
     dict(d1=0, d2=0x02, l2=0, r2=20)),
    ("bluetooth full: nothing", report("bt_full"), dict(d1=0, d2=0, l2=0, r2=0)),
]

LINE = re.compile(r"\[pad\] t=(\d+) tid=(\d+) d1=(\w+) d2=(\w+) L=(\d+),(\d+) R=(\d+),(\d+) l2=(\d+) r2=(\d+) cross=(\d+) square=(\d+)")


def put(rep):
    tmp = REP + ".tmp"
    with open(tmp, "w") as f:
        f.write(rep.hex(" "))
    os.replace(tmp, REP)


def lines():
    try:
        with open(ERR, errors="replace") as f:
            s = f.read()
    except OSError:
        return []
    out = []
    for m in LINE.finditer(s):
        g = m.groups()
        out.append(dict(t=int(g[0]), tid=g[1], d1=int(g[2], 16), d2=int(g[3], 16), L=(int(g[4]), int(g[5])),
                        R=(int(g[6]), int(g[7])), l2=int(g[8]), r2=int(g[9]), cross=int(g[10]), square=int(g[11])))
    return out


def main():
    hold = float(sys.argv[1]) if len(sys.argv) > 1 else 2.0
    put(report("bt_full"))
    env = '@{PAD_TRACE="1";PAD_NO_DUALSENSE="1";PAD_DS_REPORT_FILE="%s"}' % REP
    secs = int(90 + hold * len(CASES) + 30)
    p = subprocess.Popen(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
                          "& '%s\\scripts\\run.ps1' -Seconds %d -Tag padsim -Env %s" % (ROOT, secs, env)],
                         stdout=subprocess.DEVNULL, cwd=ROOT)
    t0 = time.time()
    while time.time() - t0 < 90:
        time.sleep(1)
        try:
            if "GetData polling begins" in open(OUT, errors="replace").read():
                break
        except OSError:
            pass
    else:
        print("the game never polled the pad")
    print(f"polling after {time.time() - t0:.0f} s")
    time.sleep(3)
    bad = 0
    for name, rep, want in CASES:
        before = len(lines())
        put(rep)
        time.sleep(hold)
        new = lines()[before:]
        last = new[-1] if new else None
        ok = bool(last) and all(last[k] == v for k, v in want.items())
        # A held report must stay put: more than the change itself means it flickered.
        steady = len(new) <= 2
        bad += (not ok) + (not steady)
        shown = {k: last[k] for k in ("d1", "d2", "L", "l2", "r2", "cross", "square")} if last else None
        print(("ok  " if ok else "FAIL"), name, "| lines", len(new), "" if steady else "FLICKER", "|", shown)
    subprocess.run(["powershell", "-NoProfile", "-Command",
                    "Get-Process gt5 -ErrorAction SilentlyContinue | Stop-Process -Force -Confirm:$false"])
    p.wait()
    all_lines = lines()
    print("trace lines", len(all_lines), "reader threads", sorted({x["tid"] for x in all_lines}))
    print("failures", bad)


if __name__ == "__main__":
    main()
