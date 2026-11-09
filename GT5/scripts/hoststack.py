import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Host call stacks of a running gt5.exe, from outside: hoststack.py [--depth N] [--name text] [--all]

Suspends each thread for a moment and walks it with dbghelp, naming frames in gt5.exe with llvm-symbolizer.
By default prints only named threads; --all prints everything, --name keeps threads whose description
contains the text.
"""
import ctypes
import ctypes.wintypes as w
import subprocess
import sys


def opt(name, default=None):
    if name in sys.argv:
        i = sys.argv.index(name)
        v = sys.argv[i + 1]
        del sys.argv[i:i + 2]
        return v
    return default


depth = int(opt("--depth", "14"))
name_filter = opt("--name")
show_all = "--all" in sys.argv

k = ctypes.WinDLL("kernel32", use_last_error=True)
dbg = ctypes.WinDLL("dbghelp", use_last_error=True)

for fn, res in [(k.OpenProcess, w.HANDLE), (k.OpenThread, w.HANDLE), (k.CreateToolhelp32Snapshot, w.HANDLE)]:
    fn.restype = res
k.SuspendThread.argtypes = [w.HANDLE]
k.ResumeThread.argtypes = [w.HANDLE]
k.CloseHandle.argtypes = [w.HANDLE]
k.GetThreadContext.argtypes = [w.HANDLE, ctypes.c_void_p]
k.Thread32First.argtypes = k.Thread32Next.argtypes = [w.HANDLE, ctypes.c_void_p]
k.GetThreadDescription.argtypes = [w.HANDLE, ctypes.POINTER(ctypes.c_wchar_p)]
k.LocalFree.argtypes = [ctypes.c_void_p]


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", w.DWORD), ("cntUsage", w.DWORD), ("th32ThreadID", w.DWORD), ("th32OwnerProcessID", w.DWORD),
                ("tpBasePri", w.LONG), ("tpDeltaPri", w.LONG), ("dwFlags", w.DWORD)]


class ADDRESS64(ctypes.Structure):
    _fields_ = [("Offset", ctypes.c_uint64), ("Segment", ctypes.c_uint16), ("Mode", ctypes.c_int)]


class STACKFRAME64(ctypes.Structure):
    _fields_ = [("AddrPC", ADDRESS64), ("AddrReturn", ADDRESS64), ("AddrFrame", ADDRESS64), ("AddrStack", ADDRESS64),
                ("AddrBStore", ADDRESS64), ("FuncTableEntry", ctypes.c_void_p), ("Params", ctypes.c_uint64 * 4),
                ("Far", w.BOOL), ("Virtual", w.BOOL), ("Reserved", ctypes.c_uint64 * 3),
                ("KdHelp", ctypes.c_byte * 112)]


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [("SizeOfStruct", w.ULONG), ("TypeIndex", w.ULONG), ("Reserved", ctypes.c_uint64 * 2), ("Index", w.ULONG),
                ("Size", w.ULONG), ("ModBase", ctypes.c_uint64), ("Flags", w.ULONG), ("Value", ctypes.c_uint64),
                ("Address", ctypes.c_uint64), ("Register", w.ULONG), ("Scope", w.ULONG), ("Tag", w.ULONG),
                ("NameLen", w.ULONG), ("MaxNameLen", w.ULONG), ("Name", ctypes.c_char * 512)]


out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq gt5.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
pid = int(out.split('","')[1])
proc = k.OpenProcess(0x1F0FFF, False, pid)
dbg.SymSetOptions(0x2 | 0x10)
dbg.SymInitialize.argtypes = [w.HANDLE, ctypes.c_char_p, w.BOOL]
dbg.SymInitialize(proc, b(GT5_DIR + r"\build"), True)
dbg.SymFromAddr.argtypes = [w.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(SYMBOL_INFO)]
dbg.SymFromName.argtypes = [w.HANDLE, ctypes.c_char_p, ctypes.POINTER(SYMBOL_INFO)]
dbg.SymGetModuleBase64.restype = ctypes.c_uint64
dbg.SymGetModuleBase64.argtypes = [w.HANDLE, ctypes.c_uint64]
FTA = ctypes.WINFUNCTYPE(ctypes.c_void_p, w.HANDLE, ctypes.c_uint64)(("SymFunctionTableAccess64", dbg))
GMB = ctypes.WINFUNCTYPE(ctypes.c_uint64, w.HANDLE, ctypes.c_uint64)(("SymGetModuleBase64", dbg))
dbg.StackWalk64.argtypes = [w.DWORD, w.HANDLE, w.HANDLE, ctypes.POINTER(STACKFRAME64), ctypes.c_void_p, ctypes.c_void_p,
                            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]

SYMBOLIZER = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\llvm-symbolizer.exe"
EXE = (GT5_DIR + r"\build\gt5.exe")
exe_base = 0
lines = {}


def source(addr):
    """Function and line in gt5.exe, which dbghelp only names by the nearest public symbol."""
    if addr not in lines:
        r = subprocess.run([SYMBOLIZER, "--obj=" + EXE, "--no-inlines", hex(addr - exe_base + 0x140000000)], capture_output=True, text=True)
        o = r.stdout.split("\n")
        lines[addr] = "%s  %s" % (o[0], o[1].rsplit("\\", 1)[-1]) if len(o) > 1 and o[0] != "??" else None
    return lines[addr]


def sym(addr):
    if exe_base and dbg.SymGetModuleBase64(proc, addr) == exe_base:
        n = source(addr)
        if n:
            return n
    s = SYMBOL_INFO()
    s.SizeOfStruct = 88
    s.MaxNameLen = 511
    disp = ctypes.c_uint64()
    if dbg.SymFromAddr(proc, addr, ctypes.byref(disp), ctypes.byref(s)):
        return "%s+%X" % (s.Name.decode(errors="replace"), disp.value)
    return "%X" % addr


_s = SYMBOL_INFO()
_s.SizeOfStruct = 88
_s.MaxNameLen = 511
if dbg.SymFromName(proc, b"gt5!cellSpursAddWorkload", ctypes.byref(_s)):
    exe_base = _s.ModBase

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
    if (name_filter and name_filter not in title) or (not show_all and not name_filter and not title):
        k.CloseHandle(h)
        continue
    k.SuspendThread(h)
    pcs = []
    try:
        ctypes.memset(ctx_addr, 0, 1232)
        ctypes.c_uint32.from_address(ctx_addr + 0x30).value = 0x10001F  # CONTEXT_ALL for amd64
        if k.GetThreadContext(h, ctx_addr):
            rip = ctypes.c_uint64.from_address(ctx_addr + 0xF8).value
            rsp = ctypes.c_uint64.from_address(ctx_addr + 0x98).value
            rbp = ctypes.c_uint64.from_address(ctx_addr + 0xA0).value
            f = STACKFRAME64()
            f.AddrPC.Offset, f.AddrPC.Mode = rip, 3
            f.AddrStack.Offset, f.AddrStack.Mode = rsp, 3
            f.AddrFrame.Offset, f.AddrFrame.Mode = rbp, 3
            for _ in range(depth):
                if not dbg.StackWalk64(0x8664, proc, h, ctypes.byref(f), ctx_addr, None, FTA, GMB, None):
                    break
                if not f.AddrPC.Offset:
                    break
                pcs.append(f.AddrPC.Offset)
    finally:
        k.ResumeThread(h)
        k.CloseHandle(h)
    print("== %d %s" % (tid, title))
    for pc in pcs:
        print("   ", sym(pc)[:120])
