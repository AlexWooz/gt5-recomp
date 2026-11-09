import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Temporary: log script function and native calls by name. Undo with dbg_trace.py --clean.

func_00B7B954 is the native dispatcher (r3 = the callable), func_00B15B70 calls a script
function (r4 = the callable). A callable's name is the std::string at *(obj+0xC)+4, the
same read func_00B01DF4 does.
"""
import os
import shutil

GEN = (GT5_DIR + r"\generated")
ORIG = (GT5_DIR + r"\analysis\dbg_orig")
os.makedirs(ORIG, exist_ok=True)
DECL = b'#include <stdint.h>\nextern "C" void gt5_log_call(const char*, uint64_t, uint64_t);\n'
for chunk, fn, reg in (("030", "00B7B954", 3), ("029", "00B15B70", 4)):
    path = os.path.join(GEN, f"ppu_recomp_{chunk}.cpp")
    dst = os.path.join(ORIG, os.path.basename(path))
    if not os.path.exists(dst):
        shutil.copy2(path, dst)
    raw = open(path, "rb").read()
    head = f"void func_{fn}(ppu_context* ctx) {{".encode()
    assert raw.count(head) == 1
    raw = raw.replace(head, head + f'\n    gt5_log_call("{"nat" if reg == 3 else "fn"}", ctx->gpr[{reg}], ctx->thread_id);'.encode())
    open(path, "wb").write(DECL + raw)
print("patched")
