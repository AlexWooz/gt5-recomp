"""SPU state of a running gt5.exe, from outside: spuregs.py [--name "spurs pm"] [--reg a,b,c] [--ls hexaddr,hexlen]

Finds each host thread's spu_context by scanning its stack for a pointer whose `ls` field points at its own
ls_store, then prints image, pc, link register and the MFC staging registers.
"""
import ctypes
import ctypes.wintypes as w
import struct
import subprocess
import sys

LS_FIELD = 0x40800


def opt(name, default=None):
    if name in sys.argv:
        i = sys.argv.index(name)
        v = sys.argv[i + 1]
        del sys.argv[i:i + 2]
        return v
    return default


name = opt("--name", "spurs")
ls_dump = opt("--ls")
regs = [int(x) for x in opt("--reg", "3,11,14,15,16,18,55,61,63,70").split(",")]
k = ctypes.WinDLL("kernel32", use_last_error=True)
for fn in (k.OpenProcess, k.OpenThread, k.CreateToolhelp32Snapshot):
    fn.restype = w.HANDLE
k.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k.SuspendThread.argtypes = k.ResumeThread.argtypes = k.CloseHandle.argtypes = [w.HANDLE]
k.GetThreadContext.argtypes = [w.HANDLE, ctypes.c_void_p]
k.Thread32First.argtypes = k.Thread32Next.argtypes = [w.HANDLE, ctypes.c_void_p]
k.GetThreadDescription.argtypes = [w.HANDLE, ctypes.POINTER(ctypes.c_wchar_p)]
k.LocalFree.argtypes = [ctypes.c_void_p]


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", w.DWORD), ("cntUsage", w.DWORD), ("th32ThreadID", w.DWORD), ("th32OwnerProcessID", w.DWORD),
                ("tpBasePri", w.LONG), ("tpDeltaPri", w.LONG), ("dwFlags", w.DWORD)]


out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq gt5.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
pid = int(out.split('","')[1])
proc = k.OpenProcess(0x1F0FFF, False, pid)


def read(addr, n):
    buf = ctypes.create_string_buffer(n)
    got = ctypes.c_size_t()
    if not k.ReadProcessMemory(proc, addr, buf, n, ctypes.byref(got)):
        return buf.raw[:got.value]
    return buf.raw[:got.value]


snap = k.CreateToolhelp32Snapshot(4, 0)
te = THREADENTRY32()
te.dwSize = ctypes.sizeof(te)
tids = []
ok = k.Thread32First(snap, ctypes.byref(te))
while ok:
    if te.th32OwnerProcessID == pid:
        tids.append(te.th32ThreadID)
    ok = k.Thread32Next(snap, ctypes.byref(te))
k.CloseHandle(snap)

raw = ctypes.create_string_buffer(4096 + 16)
ctx_addr = (ctypes.addressof(raw) + 15) & ~15
for tid in tids:
    h = k.OpenThread(0x1FFFFF, False, tid)
    if not h:
        continue
    desc = ctypes.c_wchar_p()
    title = ""
    if k.GetThreadDescription(h, ctypes.byref(desc)) >= 0 and desc.value:
        title = desc.value
        k.LocalFree(desc)
    if name not in title:
        k.CloseHandle(h)
        continue
    k.SuspendThread(h)
    ctypes.memset(ctx_addr, 0, 1232)
    ctypes.c_uint32.from_address(ctx_addr + 0x30).value = 0x10001F
    rsp = 0
    if k.GetThreadContext(h, ctx_addr):
        rsp = ctypes.c_uint64.from_address(ctx_addr + 0x98).value
    stack = read(rsp, 0x4000) if rsp else b""
    seen = set()
    for i in range(0, len(stack) - 8, 8):
        p = struct.unpack_from("<Q", stack, i)[0]
        if p < 0x10000 or p >> 47 or p & 15 or p in seen:
            continue
        seen.add(p)
        field = read(p + LS_FIELD, 0x30)
        if len(field) == 0x30 and struct.unpack_from("<Q", field)[0] == p + 0x800:
            pc, status, stop, spu, group, img = struct.unpack_from("<IIIIIi", field, 8)
            gp = read(p, 0x800)
            r = lambda n: struct.unpack_from("<I", gp, n * 16)[0]
            print("%d %s ctx=%X img=%d pc=%05X lr=%05X sp=%05X status=%X spu=%X" % (tid, title, p, img, pc & 0x3FFFF, r(0) & 0x3FFFF, r(1), status, spu))
            print("   " + " ".join("r%d=%08X" % (n, r(n)) for n in regs))
            if ls_dump and status == 1:
                a, n = [int(x, 16) for x in ls_dump.split(",")]
                d = read(p + 0x800 + a, n)
                for o in range(0, len(d), 32):
                    print("   %05X: %s" % (a + o, " ".join("%08x" % x for x in struct.unpack_from("<8I", (d + bytes(32)), o))))
            break
    k.ResumeThread(h)
    k.CloseHandle(h)
