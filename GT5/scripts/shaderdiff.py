import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Why does a shader miss the disk cache? shaderdiff.py <minutes> [count]

Takes files written in the last <minutes>, finds the older file whose HLSL is closest, and prints the lines that
differ. If the same text comes back with small changes, something run-specific has got into the text.
"""
import difflib
import os
import struct
import sys
import time

D = (GT5_DIR + r"\scratch\rsx_shader_cache_v2")
minutes = float(sys.argv[1])
count = int(sys.argv[2]) if len(sys.argv) > 2 else 4
now = time.time()
HEADER = 32


def source(path):
    d = open(path, "rb").read()
    # header: magic, version, stage, source_length, blob_length, pad, hash
    fields = struct.unpack_from("<5I", d)
    for off in (24, 32, 40):
        n = fields[3]
        text = d[off:off + n]
        if text[:64].isascii() and b"\0" not in text[:64]:
            return text.decode("latin1")
    return ""


new, old = [], []
for e in os.scandir(D):
    if not e.name.endswith(".bin"):
        continue
    (new if now - e.stat().st_mtime < minutes * 60 else old).append(e)
print("new", len(new), "old", len(old))
olds = {}
for e in old:
    if e.name[0] == "P":
        olds[e.name] = source(e.path)
shown = 0
exact = 0
for e in sorted(new, key=lambda x: x.name):
    if e.name[0] != "P":
        continue
    s = source(e.path)
    same = [n for n, t in olds.items() if t == s]
    if same:
        exact += 1
        continue
    lines = s.splitlines()
    best, best_r = None, 0
    cands = sorted(olds.items(), key=lambda kv: abs(len(kv[1]) - len(s)))[:150]
    for n, t in cands:
        r = difflib.SequenceMatcher(None, lines, t.splitlines(), autojunk=False).quick_ratio()
        if r > best_r:
            best, best_r = n, r
    if shown < count and best:
        print("==", e.name, "closest", best, "ratio %.3f" % best_r, "lengths", len(s), len(olds[best]))
        for l in list(difflib.unified_diff(olds[best].splitlines(), lines, lineterm="", n=0))[:14]:
            print("  ", l[:150])
        shown += 1
print("identical text under another name:", exact)
