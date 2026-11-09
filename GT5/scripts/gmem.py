import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Read guest memory of a running gt5.exe by pointer chains: gmem.py "name=*186E860" "dump name+0,256" ...

A step `x=*EXPR` reads a big-endian u32 at EXPR into x. `dump EXPR,LEN` prints LEN bytes as 32-bit words. `save EXPR,LEN,PATH` writes them to a file.
EXPR is hex numbers and names joined with + or -. Names not set are an error.
"""
import ctypes
import ctypes.wintypes as w
import re
import struct
import subprocess
import sys

k = ctypes.windll.kernel32
k.OpenProcess.restype = w.HANDLE
k.VirtualQueryEx.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
k.VirtualQueryEx.restype = ctypes.c_size_t
k.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
ELF = (GT5_DIR + r"\input\EBOOT.elf")
pid = int(subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-Process gt5).Id"], capture_output=True, text=True).stdout.split()[0])
h = k.OpenProcess(0x0410, False, pid)


def read(addr, n):
    got = ctypes.c_size_t(0)
    b = ctypes.create_string_buffer(n)
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
names = {}


def ev(e):
    total = 0
    for sign, tok in re.findall(r"([+-]?)([0-9A-Za-z_]+)", e):
        v = names[tok] if tok in names else int(tok, 16)
        total += -v if sign == "-" else v
    return total & 0xFFFFFFFF


for step in sys.argv[1:]:
    if step.startswith("save "):
        e, n, path = step[5:].split(",", 2)
        d = read(base + ev(e), int(n, 0))
        print("save %s: %s" % (e, "unreadable" if d is None else path))
        if d is not None:
            open(path, "wb").write(d)
    elif step.startswith("dump "):
        e, n = step[5:].split(",")
        ea = ev(e)
        n = int(n, 0)
        d = read(base + ea, n)
        if d is None:
            print("%s %X unreadable" % (e, ea))
            continue
        for i in range(0, n, 32):
            print("%08X: %s" % (ea + i, " ".join(d[i + j:i + j + 4].hex() for j in range(0, min(32, n - i), 4))))
    else:
        nm, e = step.split("=*")
        d = read(base + ev(e), 4)
        names[nm] = struct.unpack(">I", d)[0] if d else 0
        print("%s = %X" % (nm, names[nm]))
