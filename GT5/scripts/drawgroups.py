"""One captured frame's draws grouped by pipeline: drawgroups.py <csv> [--frame N] [--surf 0x1980000] [--top K]

Shows, per pso: draw count, vertices, target and the texture units bound by the largest draw.
"""
import collections
import csv
import sys


def arg(n, d):
    return sys.argv[sys.argv.index(n) + 1] if n in sys.argv else d


rows = list(csv.DictReader(open(sys.argv[1])))
frames = collections.Counter(r["frame"] for r in rows)
print("frames:", " ".join(f"{k}:{v}" for k, v in sorted(frames.items(), key=lambda x: int(x[0]))[:12]))
want = arg("--frame", None) or max(frames, key=lambda k: frames[k])
surf = arg("--surf", None)
g = collections.defaultdict(list)
for r in rows:
    if r["frame"] == want and (not surf or r["surf"].lower() == surf.lower()):
        g[r["pso_key"]].append(r)
print("frame", want)
order = sorted(g.items(), key=lambda kv: -sum(int(r["verts"]) for r in kv[1]))
for key, rs in order[:int(arg("--top", 12))]:
    big = max(rs, key=lambda r: int(r["verts"]))
    print(f"{key} n={len(rs)} v={sum(int(r['verts']) for r in rs)} surf={big['surf']} blend={big['blend']} first={big['draw']}")
    print("   ", big["tex"].split(" at=")[0][:230])
