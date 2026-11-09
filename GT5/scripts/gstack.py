import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Guest call stacks of a running gt5.exe, read from outside: gstack.py [--depth N] [--tid a,b] [--mem ea,len ...]

Finds guest address 0 by the Wws_Job signature and each ppu_context by its TOC and stack
pointer, then walks the PowerPC back chain (saved lr at sp+0x10). Thread numbers are the
TLS ordinal (r13), which is what `sys_ppu_thread_create` order gives.
"""
import ctypes
import ctypes.wintypes as w
import struct
import subprocess
import sys

k = ctypes.windll.kernel32
k.OpenProcess.restype = w.HANDLE
k.VirtualQueryEx.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
k.VirtualQueryEx.restype = ctypes.c_size_t
k.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
ELF = (GT5_DIR + r"\input\EBOOT.elf")
TOCS = {0x1846AF0, 0x1866624, 0x18765B0, 0x185682C}


def arg(n, d=None):
    return sys.argv[sys.argv.index(n) + 1] if n in sys.argv else d


pid = int(subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-Process gt5).Id"], capture_output=True, text=True).stdout.split()[0])
h = k.OpenProcess(0x0410, False, pid)


def read(addr, n):
    buf = ctypes.create_string_buffer(n)
    got = ctypes.c_size_t(0)
    if not k.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)) or got.value != n:
        return None
    return buf.raw


class MBI(ctypes.Structure):
    _fields_ = [("Base", ctypes.c_uint64), ("Alloc", ctypes.c_uint64), ("AllocProt", w.DWORD), ("pad0", w.DWORD),
                ("Size", ctypes.c_uint64), ("State", w.DWORD), ("Protect", w.DWORD), ("Type", w.DWORD), ("pad1", w.DWORD)]


def regions():
    a = 0x10000
    m = MBI()
    while a < 0x7FFFFFFFFFFF:
        if not k.VirtualQueryEx(h, ctypes.c_void_p(a), ctypes.byref(m), ctypes.sizeof(m)):
            break
        if m.State == 0x1000 and (m.Protect & 0x66) and m.Size < 0x20000000:
            yield m.Base, m.Size
        a = m.Base + m.Size


sig = open(ELF, "rb").read()[0x177F200 - 0x10000:0x177F200 - 0x10000 + 32]
base = None
ctxs = []
for b, size in regions():
    if size < 0x400:
        continue
    d = read(b, size)
    if not d:
        continue
    if base is None and size >= 0x10000:
        o = d.find(sig)
        while o >= 0:
            if ((b + o - 0x177F200) & 0xFFFF) == 0:
                base = b + o - 0x177F200
                break
            o = d.find(sig, o + 16)
    if size < 0x4000000:
        for o in range(0, len(d) - 256, 8):
            r2 = struct.unpack_from("<Q", d, o + 16)[0]
            if r2 not in TOCS:
                continue
            r1, r13 = struct.unpack_from("<Q", d, o + 8)[0], struct.unpack_from("<Q", d, o + 13 * 8)[0]
            if ((0xD0000000 <= r1 < 0xD0800000 and 0x10F00000 <= r13 < 0x10F80000) or (0x0FE00000 <= r1 < 0x0FF00000 and r13 == 0x0E007000)) and r1 % 16 == 0:
                ctxs.append((b + o, r1, r13, d[o:o + 256]))
if base is None:
    sys.exit("guest base not found")


def g64(ea):
    b = read(base + ea, 8)
    return struct.unpack(">Q", b)[0] if b else None


depth = int(arg("--depth", 10))
want = {int(x) for x in arg("--tid", "").split(",") if x} if arg("--tid") else None
for ctx, r1, r13, raw in sorted(ctxs, key=lambda c: c[2]):
    n = 0 if r13 == 0x0E007000 else (r13 - 0x10F07000) >> 12
    if want and n not in want:
        continue
    sp = r1
    chain = []
    for _ in range(depth):
        nxt = g64(sp)
        if not nxt or not (0xD0000000 <= nxt < 0xD0800000 or 0x0FE00000 <= nxt < 0x0FF00000) or nxt <= sp:
            break
        lr = g64(nxt + 0x10)
        if lr:
            chain.append("%X" % lr)
        sp = nxt
    regs = struct.unpack("<8Q", raw[3 * 8:11 * 8])
    print("t%-2d sp=%X r2=%X r3=%X r4=%X r5=%X : %s" % (n, r1, struct.unpack("<Q", raw[16:24])[0], regs[0], regs[1], regs[2], " ".join(chain)))
for a in sys.argv[1:]:
    if "," in a and a != arg("--tid"):
        ea, ln = a.split(",")
        d = read(base + int(ea, 16), int(ln))
        print(ea, d.hex() if d else "unreadable")
