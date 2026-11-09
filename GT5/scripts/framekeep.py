"""Keep only the newest frame dumps of a running game: framekeep.py <dir> <seconds>"""
import glob
import os
import sys
import time

d, end = sys.argv[1], time.time() + float(sys.argv[2])
while time.time() < end:
    for f in sorted(glob.glob(os.path.join(d, "frame_*.ppm")), key=os.path.getmtime)[:-3]:
        try:
            os.remove(f)
        except OSError:
            pass
    time.sleep(0.5)
