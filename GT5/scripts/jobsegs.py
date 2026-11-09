import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""State of each GPUSyncArray job's command segment in a running gt5.exe: jobsegs.py <list ea, hex>

A job's segment starts with a jump to itself until the job has finished. Prints runs of parked and done jobs,
then each done segment's way out (the last jump in it) when --walk is given.
"""
import ctypes
import ctypes.wintypes as w
import re
import struct
import subprocess
import sys

sys.path.insert(0, (GT5_DIR + r"\scripts"))
out = subprocess.run(["python", (GT5_DIR + r"\scripts\gmem.py"), "save %s,0x800,E:/Games/gt5-recomp/GT5/logs/_list.bin" % sys.argv[1], "save 60600000,0x400000,E:/Games/gt5-recomp/GT5/logs/_jobs.bin", "save 61000000,0x1000000,E:/Games/gt5-recomp/GT5/logs/_seg.bin"], capture_output=True, text=True)
lst = open((GT5_DIR + r"\logs\_list.bin"), "rb").read()
jobs = open((GT5_DIR + r"\logs\_jobs.bin"), "rb").read()
seg = open((GT5_DIR + r"\logs\_seg.bin"), "rb").read()
rows = []
for i in range(0, len(lst), 8):
    size, valid, job = struct.unpack_from(">HHI", lst, i)
    if not valid:
        break
    o = job - 0x60600000
    if not 0 <= o < len(jobs) - 0x300:
        continue
    start = None
    for w_ in struct.unpack_from(">64I", jobs, o):
        if 0x61000000 <= w_ < 0x62000000:
            start = w_
            break
    first = struct.unpack_from(">I", seg, start - 0x61000000)[0] if start else 0
    parked = first == (0x20000000 | (start - 0x60000000)) if start else None
    rows.append((i // 8, job, start, first, parked))
print("jobs", len(rows))
run = None
for r in rows:
    state = "parked" if r[4] else "done"
    if run and run[0] == state:
        run[2] = r[0]
    else:
        if run:
            print("%-7s %3d..%3d" % (run[0], run[1], run[2]))
        run = [state, r[0], r[0]]
if run:
    print("%-7s %3d..%3d" % (run[0], run[1], run[2]))
for r in rows:
    print("%3d job=%08X seg=%08X first=%08X %s" % (r[0], r[1], r[2] or 0, r[3], "PARKED" if r[4] else ""))
