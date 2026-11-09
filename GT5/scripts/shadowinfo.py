"""What draws into the shadow map in a capture: shadowinfo.py <csv> [frame]"""
import collections
import csv
import sys

rows = [r for r in csv.DictReader(open(sys.argv[1])) if r.get("frame") and r["frame"].isdigit()]
frames = collections.Counter(r["frame"] for r in rows)
want = sys.argv[2] if len(sys.argv) > 2 else max(frames, key=lambda k: frames[k])
c = collections.Counter()
for r in rows:
    if r["frame"] != want or not r.get("zeta_off"):
        continue
    try:
        if int(r["zeta_off"], 16) != 0x5380000:
            continue
    except ValueError:
        continue
    c[(r["surf"], r["vpx"], r["vpy"], r["vpw"], r["vph"], r["clipw"], r["cliph"], r["zeta_pitch"], r["dwrite"], r["dfunc"], r["cmask"])] += int(r["verts"])
print("frame", want)
for k, v in sorted(c.items(), key=lambda x: -x[1])[:12]:
    print(v, k)
