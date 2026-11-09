"""Summarise a run's stderr: triage.py logs/<tag>.err [--tail N]

Drops the per-frame and register-dump noise, and counts each repeated message
once with its numbers masked, in first-seen order.
"""
import collections
import re
import sys

path = sys.argv[1]
tail = int(sys.argv[sys.argv.index("--tail") + 1]) if "--tail" in sys.argv else 25
noise = re.compile(r"^\s+(r\d+\s*:|rawstk|ARG\[)|\[live-draw\] (frame|flip)|^\[boot-dbg\]")
lines = [l.rstrip("\n") for l in open(path, encoding="utf-8", errors="replace") if not noise.search(l)]
key = lambda l: re.sub(r"0x[0-9A-Fa-f]+|\b[0-9A-F]{8}\b|\b\d+\b", "#", l)[:90]
seen = collections.OrderedDict()
for l in lines:
    k = key(l)
    if k not in seen:
        seen[k] = [0, l]
    seen[k][0] += 1
print(f"{len(lines)} lines, {len(seen)} distinct")
for n, l in seen.values():
    if "[GSTACK" in l or "[exit-chain]" in l:
        continue
    print(f"{n:6d} {l[:200]}")
print(f"--- last {tail}")
for l in lines[-tail:]:
    print(l[:260])
