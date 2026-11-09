import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Per-second activity of the sound buffers in a running gt5.exe: audioprobe.py --sec 20 [--wait 12] --out f

Polls from outside: any voice-level meter above zero, nonzero samples in the mix buffers, and in the port ring.
"""
import ctypes
import ctypes.wintypes as w
import struct
import subprocess
import sys
import time

import numpy as np

k = ctypes.windll.kernel32
k.OpenProcess.restype = w.HANDLE
k.VirtualQueryEx.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
k.VirtualQueryEx.restype = ctypes.c_size_t
k.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
ELF = (GT5_DIR + r"\input\EBOOT.elf")


def arg(n, d=None):
    return sys.argv[sys.argv.index(n) + 1] if n in sys.argv else d


time.sleep(float(arg("--wait", 0)))
pid = int(subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-Process gt5).Id"], capture_output=True, text=True).stdout.split()[0])
h = k.OpenProcess(0x0410, False, pid)
buf = ctypes.create_string_buffer(1 << 20)


def read(addr, n):
    got = ctypes.c_size_t(0)
    b = buf if n <= len(buf) else ctypes.create_string_buffer(n)
    if not k.ReadProcessMemory(h, ctypes.c_void_p(addr), b, n, ctypes.byref(got)) or got.value != n:
        return None
    return b.raw[:n]


class MBI(ctypes.Structure):
    _fields_ = [("Base", ctypes.c_uint64), ("Alloc", ctypes.c_uint64), ("AllocProt", w.DWORD), ("pad0", w.DWORD),
                ("Size", ctypes.c_uint64), ("State", w.DWORD), ("Protect", w.DWORD), ("Type", w.DWORD), ("pad1", w.DWORD)]


sig = open(ELF, "rb").read()[0x177F200 - 0x10000:0x177F200 - 0x10000 + 32]
base = None
a = 0x10000
m = MBI()
while a < 0x7FFFFFFFFFFF and base is None:
    if not k.VirtualQueryEx(h, ctypes.c_void_p(a), ctypes.byref(m), ctypes.sizeof(m)):
        break
    b0, size = m.Base, m.Size
    a = b0 + size
    if m.State != 0x1000 or not (m.Protect & 0x66) or size >= 0x20000000 or size < 0x10000:
        continue
    d = read(b0, size)
    if not d:
        continue
    o = d.find(sig)
    while o >= 0:
        if ((b0 + o - 0x177F200) & 0xFFFF) == 0:
            base = b0 + o - 0x177F200
            break
        o = d.find(sig, o + 16)
regions = {"mixA": (0x6FFA0000, 0x2000), "mixB": (0x6FF9E000, 0x2000), "ring": (0x58000000, 0x20000)}
out = open(arg("--out"), "w")
t0 = time.perf_counter()
sec = float(arg("--sec", 20))
polls = 0
hits = {n: 0 for n in regions}
peak = {n: 0.0 for n in regions}
meter = 0
last = t0
dumped = set()
while True:
    now = time.perf_counter()
    if now - t0 > sec:
        break
    polls += 1
    for n, (ea, ln) in regions.items():
        d = read(base + ea, ln)
        if d is None:
            continue
        v = np.frombuffer(d, dtype=">f4")
        p = float(np.abs(v).max())
        if p > 1e-6:
            hits[n] += 1
        peak[n] = max(peak[n], p)
    d = read(base + 0x6FFB0000, 0x1B00)
    if d:
        ms = [struct.unpack(">f", d[i * 0x48 + 0x3C:i * 0x48 + 0x40])[0] for i in range(96)]
        if max(ms) > 1e-3:
            meter += 1
    if now - last >= 1.0:
        state = "bad" if meter > polls / 2 and hits["ring"] == 0 else ("good" if meter > polls / 2 else "")
        if state and state not in dumped:
            dumped.add(state)
            with open(arg("--out") + "." + state, "w") as f:
                for ea, ln in ((0x60044B00, 0x600), (0x1933410, 0x300), (0x19332A8, 0x100), (0x60040000, 0x400), (0x60044280, 0x80)):
                    d = read(base + ea, ln)
                    for i in range(0, ln, 32):
                        f.write("%08X: %s\n" % (ea + i, " ".join(d[i + j:i + j + 4].hex() for j in range(0, 32, 4))))
        out.write("%5.1f polls=%d voices=%d %s\n" % (now - t0, polls, meter, " ".join("%s=%d/%.3f" % (n, hits[n], peak[n]) for n in regions)))
        out.flush()
        polls = 0
        meter = 0
        hits = {n: 0 for n in regions}
        peak = {n: 0.0 for n in regions}
        last = now
out.close()
