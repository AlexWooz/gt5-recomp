import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Temporary: dump each Adhoc script to analysis/adc/ as func_00B866DC loads it.

func_00B26FA8 is the stream read (r4 = buffer, returns the count). Undo with
dbg_trace.py --clean.
"""
import os
import re
import shutil

GEN = (GT5_DIR + r"\generated")
ORIG = (GT5_DIR + r"\analysis\dbg_orig")
DECL = b'#include <stdint.h>\nextern "C" void gt5_adc_path(uint64_t); extern "C" void gt5_dump_adc(uint64_t, uint64_t, uint64_t);\n'
os.makedirs(ORIG, exist_ok=True)


def patch(chunk, edit):
    path = os.path.join(GEN, f"ppu_recomp_{chunk}.cpp")
    dst = os.path.join(ORIG, os.path.basename(path))
    if not os.path.exists(dst):
        shutil.copy2(path, dst)
    raw = edit(open(path, "rb").read())
    open(path, "wb").write(DECL + raw)


def path_probe(raw):
    head = b"void func_00B866DC(ppu_context* ctx) {"
    assert raw.count(head) == 1
    return raw.replace(head, head + b"\n    gt5_adc_path(ctx->gpr[5]);")


def read_probe(raw):
    head = b"void func_00B26FA8(ppu_context* ctx) {"
    assert raw.count(head) == 1
    raw = raw.replace(head, head + b"\n    const uint64_t _adc_buf = ctx->gpr[4];")
    tail = rb"\{ g_trampoline_fn = \(void\(\*\)\(void\*\)\)func_00D39D9C; return; \}(\s*\}\s*void func_00B270AC)"
    raw, n = re.subn(tail, lambda m: b"gt5_dump_adc(_adc_buf, ctx->gpr[3], ctx->thread_id); { g_trampoline_fn = (void(*)(void*))func_00D39D9C; return; }" + m.group(1), raw)
    assert n == 1
    return raw


patch("030", path_probe)
patch("029", read_probe)
print("patched")
