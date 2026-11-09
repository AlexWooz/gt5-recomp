import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Sample guest call stacks of chosen threads from a live gt5.exe: gsample.py --tid 18,28 [--n 2000] [--ms 2] [--depth 5]

Finds the contexts once, like gstack.py, then only re-reads r1 and the back chain, so it can
sample fast enough to see short waits. Prints each thread's most common stacks.
"""
import collections
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
TOCS = {0x1846AF0, 0x1866624, 0x18765B0, 0x185682C}


def arg(n, d=None):
    return sys.argv[sys.argv.index(n) + 1] if n in sys.argv else d


pid = int(subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-Process gt5).Id"], capture_output=True, text=True).stdout.split()[0])
h = k.OpenProcess(0x0410, False, pid)
small = ctypes.create_string_buffer(64)


def read(addr, n):
    got = ctypes.c_size_t(0)
    b = small if n <= 64 else ctypes.create_string_buffer(n)
    if not k.ReadProcessMemory(h, ctypes.c_void_p(addr), b, n, ctypes.byref(got)) or got.value != n:
        return None
    return b.raw[:n]


class MBI(ctypes.Structure):
    _fields_ = [("Base", ctypes.c_uint64), ("Alloc", ctypes.c_uint64), ("AllocProt", w.DWORD), ("pad0", w.DWORD),
                ("Size", ctypes.c_uint64), ("State", w.DWORD), ("Protect", w.DWORD), ("Type", w.DWORD), ("pad1", w.DWORD)]


sig = open(ELF, "rb").read()[0x177F200 - 0x10000:0x177F200 - 0x10000 + 32]
base = None
ctxs = {}
main_ctx = []
a = 0x10000
m = MBI()
while a < 0x7FFFFFFFFFFF:
    if not k.VirtualQueryEx(h, ctypes.c_void_p(a), ctypes.byref(m), ctypes.sizeof(m)):
        break
    b0, size = m.Base, m.Size
    a = b0 + size
    if m.State != 0x1000 or not (m.Protect & 0x66) or size >= 0x20000000 or size < 0x400:
        continue
    d = read(b0, size)
    if not d:
        continue
    if base is None and size >= 0x10000:
        o = d.find(sig)
        while o >= 0:
            if ((b0 + o - 0x177F200) & 0xFFFF) == 0:
                base = b0 + o - 0x177F200
                break
            o = d.find(sig, o + 16)
    if size < 0x4000000:
        for o in range(0, len(d) - 256, 8):
            if struct.unpack_from("<Q", d, o + 16)[0] not in TOCS:
                continue
            r1, r13 = struct.unpack_from("<Q", d, o + 8)[0], struct.unpack_from("<Q", d, o + 13 * 8)[0]
            if ((0xD0000000 <= r1 < 0xD0800000 and 0x10F00000 <= r13 < 0x10F80000) or (0x0FE00000 <= r1 < 0x0FF00000 and r13 == 0x0E007000)) and r1 % 16 == 0:
                (ctxs.__setitem__((r13 - 0x10F07000) >> 12, b0 + o) if r13 != 0x0E007000 else main_ctx.append(b0 + o))
# The main thread's context has stale copies: take the one whose stack pointer moves.
if main_ctx:
    seen = {c: set() for c in main_ctx}
    for _ in range(40):
        for c in main_ctx:
            v = read(c + 8, 8)
            if v:
                seen[c].add(v)
        time.sleep(0.01)
    ctxs[0] = max(main_ctx, key=lambda c: len(seen[c]))
want = [int(x) for x in arg("--tid").split(",")]
n = int(arg("--n", 2000))
ms = float(arg("--ms", 2)) / 1000
depth = int(arg("--depth", 5))
hist = {t: collections.Counter() for t in want}
for _ in range(n):
    for t in want:
        c = read(ctxs[t] + 8, 8)
        if not c:
            continue
        sp = struct.unpack("<Q", c)[0]
        chain = []
        for _d in range(depth):
            f = read(base + sp, 8)
            if not f:
                break
            nxt = struct.unpack(">Q", f)[0]
            if not (0xD0000000 <= nxt < 0xD0800000 or 0x0FE00000 <= nxt < 0x0FF00000) or nxt <= sp:
                break
            l = read(base + nxt + 0x10, 8)
            chain.append("%X" % struct.unpack(">Q", l)[0] if l else "?")
            sp = nxt
        hist[t][" ".join(chain)] += 1
    time.sleep(ms)
for t in want:
    print("t%d" % t)
    for s, c in hist[t].most_common(10):
        print("  %5.1f%% %s" % (100.0 * c / n, s))
