"""Levels of an AUDIO_WAV capture: audiolevel.py <file.f32> [--wav out.wav] [--rate 48000]

Prints RMS and peak per 5 seconds, so a muted run can still show whether the game made
sound, and optionally writes a 16-bit WAV to listen to later.
"""
import array
import math
import sys
import wave

path = sys.argv[1]
rate = int(sys.argv[sys.argv.index("--rate") + 1]) if "--rate" in sys.argv else 48000
out = sys.argv[sys.argv.index("--wav") + 1] if "--wav" in sys.argv else None
wav = None
if out:
    wav = wave.open(out, "wb")
    wav.setnchannels(2)
    wav.setsampwidth(2)
    wav.setframerate(rate)
block = rate * 2 * 5
t = 0
with open(path, "rb") as f:
    while True:
        data = f.read(block * 4)
        if len(data) < 8:
            break
        s = array.array("f")
        s.frombytes(data[: len(data) // 4 * 4])
        total = 0.0
        peak = 0.0
        for v in s:
            total += v * v
            if abs(v) > peak:
                peak = abs(v)
        rms = math.sqrt(total / len(s))
        print(f"{t:5d}s  rms {rms:7.4f}  peak {peak:7.4f}")
        if wav:
            pcm = array.array("h", (max(-32768, min(32767, int(v * 32767))) for v in s))
            wav.writeframes(pcm.tobytes())
        t += 5
if wav:
    wav.close()
print("seconds captured:", t)
