import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Temporary: trace the walk of the script module list in func_00B835A4 (chunk 030)."""
path = (GT5_DIR + r"\generated\ppu_recomp_030.cpp")
raw = open(path, "rb").read()
PROBE = (b'        fprintf(stderr, "[modlist %s] rec=%08X name=%08X a=%08X b=%08X c=%08X next=%08X nm=%.30s\\n", "%s", '
         b'(unsigned)ctx->gpr[31], vm_read32(ctx->gpr[31]), vm_read32(ctx->gpr[31]+4), vm_read32(ctx->gpr[31]+8), '
         b'vm_read32(ctx->gpr[31]+12), vm_read32(ctx->gpr[31]+16), '
         b'vm_read32(ctx->gpr[31]) >= 0x10000 && vm_read32(ctx->gpr[31]) < 0x1000000 ? "?" : "?");\r\n')
for label, tag in ((b"loc_00B835E8:", b"1"), (b"loc_00B83614:", b"2")):
    i = raw.index(label)
    eol = raw.index(b"\n", i) + 1
    if b"[modlist" in raw[eol:eol + 40]:
        continue
    raw = raw[:eol] + PROBE.replace(b'"%s", ', b'"' + tag + b'", ') + raw[eol:]
open(path, "wb").write(raw)
print("patched")
