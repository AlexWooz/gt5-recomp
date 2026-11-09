import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Temporary: log each C++ throw (func_00D20138 = __cxa_throw). Undo with dbg_trace.py --clean."""
import os
import shutil

path = (GT5_DIR + r"\generated\ppu_recomp_035.cpp")
dst = (GT5_DIR + r"\analysis\dbg_orig\ppu_recomp_035.cpp")
os.makedirs(os.path.dirname(dst), exist_ok=True)
if not os.path.exists(dst):
    shutil.copy2(path, dst)
raw = open(path, "rb").read()
head = b"void func_00D20138(ppu_context* ctx) {"
assert raw.count(head) == 1
raw = raw.replace(head, head + b'\n    gt5_log_throw(ctx->gpr[3], ctx->gpr[4], ctx->thread_id); ppu_dump_guest_stack(ctx, "throw");')
decl = (b'#include <stdint.h>\nextern "C" void gt5_log_throw(uint64_t, uint64_t, uint64_t);\n'
        b'struct ppu_context; extern "C" void ppu_dump_guest_stack(ppu_context*, const char*);\n')
open(path, "wb").write(decl + raw)
print("patched")
