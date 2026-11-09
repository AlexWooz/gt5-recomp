import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Are the pictures the game decoded the same as an independent decode of the same stream?
moviecmp.py <stream from DMUX_DUMP> <dir from VDEC_DUMP>   (FFmpeg demuxes and decodes the stream itself)
"""
import glob
import hashlib
import os
import subprocess
import sys

FF = (BASE_DIR + r"\ffmpeg\ffmpeg-n9.0-latest-win64-lgpl-shared-9.0\bin\ffmpeg.exe")
stream, dumps = sys.argv[1], sys.argv[2]
out = subprocess.run([FF, "-hide_banner", "-loglevel", "error", "-f", "mpeg", "-i", stream, "-map", "0:v:0", "-fps_mode", "passthrough",
                      "-f", "framemd5", "-"], capture_output=True, text=True)
errors = [x for x in out.stderr.splitlines() if x.strip()]
print("independent decode: %d log lines; first: %s" % (len(errors), errors[:3]))
frames = {}
n = 0
for line in out.stdout.splitlines():
    if line.startswith("#") or not line.strip():
        continue
    f = [x.strip() for x in line.split(",")]
    frames[f[-1]] = n
    n += 1
print("frames in the stream: %d, distinct: %d" % (n, len(frames)))
hit = miss = 0
offsets = []
missed = []
for path in sorted(glob.glob(os.path.join(dumps, "*.yuv"))):
    tok = int(os.path.basename(path).split("_")[0])
    h = hashlib.md5(open(path, "rb").read()).hexdigest()
    if h in frames:
        hit += 1
        offsets.append(frames[h] - tok)
    else:
        miss += 1
        missed.append(tok)
print("game pictures found in it: %d, not found: %d %s" % (hit, miss, missed[:12]))
if offsets:
    print("display index minus AU number: min %d max %d" % (min(offsets), max(offsets)))
