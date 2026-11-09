"""Look for clicks and clipping in a captured run: audiocheck.py <f32> <from sec> <to sec> [--out spectrogram.png]

Prints, per second, how often samples sit at full scale and how often the signal jumps
further between two samples than sound normally can. Assumes interleaved float at 48 kHz.
"""
import os
import sys

import numpy as np

path, a, b = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
size = os.path.getsize(path)
ch = 2
for c in (2, 6, 8):
    # audiolevel.py's seconds tell which channel count fits; fall back to stereo.
    pass
ch = int(os.environ.get("AUDIO_CH", "2"))
sr = 48000
d = np.fromfile(path, dtype="<f4", offset=int(a * sr) * ch * 4, count=int((b - a) * sr) * ch)
print("file seconds at %d ch: %.1f" % (ch, size / 4 / ch / sr), "read", d.size)
for c in range(ch):
    x = d[c::ch]
    print("ch%d rms %.4f peak %.3f" % (c, np.sqrt((x * x).mean()), np.abs(x).max()))
x = d[0::ch]
n = sr
for i in range(0, min(len(x), n * 8), n):
    s = x[i:i + n]
    if not len(s):
        break
    j = np.abs(np.diff(s))
    print("%3d rms %.3f fullscale %6d jump>.3 %5d jump>.8 %5d zero-run-max %d" % (
        i // n, np.sqrt((s * s).mean()), (np.abs(s) >= 0.999).sum(), (j > 0.3).sum(), (j > 0.8).sum(),
        np.max(np.diff(np.flatnonzero(np.r_[True, s != 0, True]))) - 1))
if "--out" in sys.argv:
    from PIL import Image
    w = 1024
    hop = 512
    frames = (len(x) - w) // hop
    win = np.hanning(w)
    S = np.stack([np.abs(np.fft.rfft(x[k * hop:k * hop + w] * win)) for k in range(frames)], axis=1)
    S = 20 * np.log10(S + 1e-6)
    S = np.clip((S + 80) / 100, 0, 1)
    Image.fromarray((S[::-1] * 255).astype(np.uint8)).resize((1000, 400)).save(sys.argv[sys.argv.index("--out") + 1])
