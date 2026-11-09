import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Temporary call trace of lifted functions: prints r3/r4 after every call inside them.

  dbg_trace.py 0001285C 00D397F8 ...   insert the probes (entry also shows strings and
                                       the first words of the r3 object)
  dbg_trace.py --clean                 put the patched chunks back

Originals go in analysis/dbg_orig/ so --clean needs no re-lift.
"""
import glob
import os
import re
import shutil
import sys

GEN = (GT5_DIR + r"\generated")
ORIG = (GT5_DIR + r"\analysis\dbg_orig")
CALL = re.compile(rb"^(\s*)ctx->lr = (0x[0-9A-F]+); (func_[0-9A-F]+|ps3_indirect_call)\(ctx\); DRAIN_TRAMPOLINE\(ctx\);(\s*)$")
DECL = b'#include <stdint.h>\nextern "C" const char* gt5_dbgstr(uint64_t); extern "C" void gt5_dbgobj(const char*, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);'

if sys.argv[1:] == ["--clean"]:
    for p in glob.glob(ORIG + r"\*.cpp"):
        shutil.copy(p, os.path.join(GEN, os.path.basename(p)))  # fresh mtime, so ninja rebuilds it
        os.remove(p)
    print("restored")
    sys.exit()

want = {a.upper().rjust(8, "0") for a in sys.argv[1:]}
os.makedirs(ORIG, exist_ok=True)
head = re.compile(rb"^void (?:gt5_orig_)?func_([0-9A-F]{8})\(ppu_context\* ctx\) \{")
for path in sorted(glob.glob(GEN + r"\ppu_recomp_0*.cpp")):
    lines = open(path, "rb").read().split(b"\n")
    out, cur, hits = [], None, 0
    for l in lines:
        m = head.match(l)
        if m:
            cur = m.group(1).decode() if m.group(1).decode() in want else None
            out.append(l)
            if cur:
                out.append(b'    gt5_dbgobj("%s", ctx->gpr[3], ctx->gpr[4], ctx->gpr[5], ctx->lr, ctx->thread_id);' % cur.encode())
                out.append(b'    fprintf(stderr, "    s6=[%%s] s7=[%%s] s8=[%%s]\\n", gt5_dbgstr(ctx->gpr[6]), gt5_dbgstr(ctx->gpr[7]), gt5_dbgstr(ctx->gpr[8]));')
                hits += 1
            continue
        if cur and l.startswith(b"}"):
            cur = None
        out.append(l)
        if cur:
            c = CALL.match(l)
            if c:
                out.append(b'    fprintf(stderr, "[tr %s] %s %s -> r3=%%llX r4=%%llX\\n", (unsigned long long)ctx->gpr[3], (unsigned long long)ctx->gpr[4]);'
                           % (cur.encode(), c.group(2), c.group(3)))
    if hits:
        out.insert(0, DECL)
        dst = os.path.join(ORIG, os.path.basename(path))
        if not os.path.exists(dst):
            shutil.copy2(path, dst)
        open(path, "wb").write(b"\n".join(out))
        print("patched", os.path.basename(path), hits)
