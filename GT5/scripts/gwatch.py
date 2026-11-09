import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Log changes to guest memory of a running gt5.exe, polled from outside:
gwatch.py --out file --sec 30 [--wait 12] EA,LEN [EA,LEN ...]   (hex). Word-sized changes, big endian.
"""
import ctypes
import ctypes.wintypes as w
import struct
import subprocess
import sys
import time

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
rng = []
for s in sys.argv[1:]:
    if "," in s and not s.startswith("-"):
        e, n = s.split(",")
        rng.append((int(e, 16), int(n, 16)))
last = {}
out = open(arg("--out"), "w")
t0 = time.perf_counter()
end = t0 + float(arg("--sec", 30))
lines = 0
limit = int(arg("--limit", 200000))
while time.perf_counter() < end and lines < limit:
    for ea, n in rng:
        d = read(base + ea, n)
        if d is None:
            continue
        p = last.get(ea)
        if p is not None and p != d:
            tt = time.perf_counter() - t0
            a1 = struct.unpack(">%dI" % (n // 4), d)
            a0 = struct.unpack(">%dI" % (n // 4), p)
            for i, (x, y) in enumerate(zip(a0, a1)):
                if x != y:
                    out.write("%.4f %08X %08X %08X\n" % (tt, ea + i * 4, x, y))
                    lines += 1
        last[ea] = d
out.close()
print("lines", lines)
