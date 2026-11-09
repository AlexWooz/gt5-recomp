import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Print the shader of a capture's biggest scene draw: drawshader.py <tag> [--surf 0x1980000] [--rank 0] [--match text]

Needs racerun.ps1 -Capture. With --match, only draws whose texture list contains the text count.
"""
import collections
import csv
import os
import re
import sys

ROOT = GT5_DIR


def arg(n, d):
    return sys.argv[sys.argv.index(n) + 1] if n in sys.argv else d


tag = sys.argv[1]
rows = [r for r in csv.DictReader(open(os.path.join(ROOT, "logs", tag + ".csv"))) if r.get("frame") and r["frame"].isdigit()]
fr = collections.Counter(r["frame"] for r in rows)
want = arg("--frame", max(fr, key=lambda k: fr[k]))
surf = arg("--surf", "0x1980000").lower()
match = arg("--match", "")
keys = collections.Counter()
sample = {}
for r in rows:
    if r["frame"] == want and r["surf"].lower() == surf and match in (r.get("tex") or ""):
        m = re.search(r"K=([0-9A-F]{16})", r["tex"])
        if m:
            keys[m.group(1)] += int(r["verts"])
            sample.setdefault(m.group(1), r)
print("frame", want, "shaders", len(keys))
k = keys.most_common()[int(arg("--rank", 0))][0]
print("key", k, "verts", keys[k])
print(sample[k]["tex"].split(" K=")[0])
t = open(os.path.join(ROOT, "analysis", "ps_" + tag, "ps_" + k + ".hlsl"), errors="replace").read()
print(t[t.index(" main(PSInput"):])
