import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Read host variables of a running gt5.exe by symbol name: gvar.py s_fifo_getoff s_stop_ea:8 [--watch n gap]

Each name may carry a byte count after a colon (default 4). Needs the variable's object file built with /Z7. Prints
the address once and the value as hex dwords; --watch repeats it, to tell a frozen value from a moving one.
"""
import ctypes
import ctypes.wintypes as w
import struct
import subprocess
import sys
import time

EXE = (GT5_DIR + r"\build\gt5.exe")
k = ctypes.WinDLL("kernel32", use_last_error=True)
dbg = ctypes.WinDLL("dbghelp", use_last_error=True)
k.OpenProcess.restype = w.HANDLE
k.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
dbg.SymInitialize.argtypes = [w.HANDLE, ctypes.c_char_p, w.BOOL]
dbg.SymLoadModuleEx.restype = ctypes.c_uint64
dbg.SymLoadModuleEx.argtypes = [w.HANDLE, w.HANDLE, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64, w.DWORD, ctypes.c_void_p, w.DWORD]
dbg.SymFromName.argtypes = [w.HANDLE, ctypes.c_char_p, ctypes.c_void_p]


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [("SizeOfStruct", w.ULONG), ("TypeIndex", w.ULONG), ("Reserved", ctypes.c_uint64 * 2), ("Index", w.ULONG),
                ("Size", w.ULONG), ("ModBase", ctypes.c_uint64), ("Flags", w.ULONG), ("Value", ctypes.c_uint64),
                ("Address", ctypes.c_uint64), ("Register", w.ULONG), ("Scope", w.ULONG), ("Tag", w.ULONG),
                ("NameLen", w.ULONG), ("MaxNameLen", w.ULONG), ("Name", ctypes.c_char * 512)]


args = sys.argv[1:]
watch, gap = 1, 1.0
if "--watch" in args:
    i = args.index("--watch")
    watch, gap = int(args[i + 1]), float(args[i + 2])
    del args[i:i + 3]

out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq gt5.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
pid = int(out.split(",")[1].strip('"'))
proc = k.OpenProcess(0x0410, False, pid)
dbg.SymSetOptions(0x00000002 | 0x00000010)  # undname, deferred loads off
dbg.SymInitialize(proc, None, False)


def module_base():
    # The main module is the first one the snapshot lists.
    class ME(ctypes.Structure):
        _fields_ = [("dwSize", w.DWORD), ("id", w.DWORD), ("pid", w.DWORD), ("g", w.DWORD), ("p", w.DWORD),
                    ("base", ctypes.c_void_p), ("size", w.DWORD), ("h", w.HMODULE), ("name", ctypes.c_char * 256),
                    ("path", ctypes.c_char * 260)]
    k.CreateToolhelp32Snapshot.restype = w.HANDLE
    k.Module32First.argtypes = [w.HANDLE, ctypes.c_void_p]
    s = k.CreateToolhelp32Snapshot(0x8, pid)
    e = ME()
    e.dwSize = ctypes.sizeof(ME)
    k.Module32First(s, ctypes.byref(e))
    k.CloseHandle(ctypes.c_void_p(s))
    return e.base, e.size


base, size = module_base()
print("module base %X, symbols %X" % (base, dbg.SymLoadModuleEx(proc, None, EXE.encode(), None, base, size, None, 0)))
vars_ = []
for a in args:
    name, _, n = a.partition(":")
    s = SYMBOL_INFO()
    s.SizeOfStruct = 88
    s.MaxNameLen = 512
    if not dbg.SymFromName(proc, name.encode(), ctypes.byref(s)):
        print(name, "not found")
        continue
    vars_.append((name, s.Address, int(n or 4)))
    print("%-28s at %X" % (name, s.Address))
for t in range(watch):
    row = []
    for name, addr, n in vars_:
        buf = ctypes.create_string_buffer(n)
        got = ctypes.c_size_t()
        k.ReadProcessMemory(proc, ctypes.c_void_p(addr), buf, n, ctypes.byref(got))
        words = struct.unpack("<%dI" % (n // 4), buf.raw[:n // 4 * 4])
        row.append("%s=%s" % (name, ",".join("%X" % x for x in words)))
    print(" ".join(row))
    if t + 1 < watch:
        time.sleep(gap)
