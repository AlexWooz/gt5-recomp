"""RMS and peak of a captured mix per N seconds: audiorms.py <f32> [--step 5]"""
import os
import sys
import numpy as np

step = float(sys.argv[sys.argv.index("--step") + 1]) if "--step" in sys.argv else 5.0
d = np.memmap(sys.argv[1], dtype="<f4", mode="r")
n = int(48000 * 2 * step)
out = []
for i in range(0, len(d) - n, n):
    c = np.asarray(d[i:i + n])
    out.append("%d:%.3f/%.2f" % (i // 96000, float(np.sqrt(np.mean(c * c))), float(np.abs(c).max())))
print(" ".join(out))
