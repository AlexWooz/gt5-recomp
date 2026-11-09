"""Condense the last WATCHDOG sample of a log: wdsum.py <err log> [width]"""
import re
import sys

width = int(sys.argv[2]) if len(sys.argv) > 2 else 260
lines = [l.rstrip() for l in open(sys.argv[1], encoding="utf8", errors="replace") if l.startswith("[WATCHDOG]")]
start = max(i for i, l in enumerate(lines) if " sample;" in l)
threads = {}
for l in lines[start:]:
    m = re.search(r"tid (\d+) (?:BOOT )?(?:rip|ret) rva=0x[0-9A-F]+(?:\s+func_00([0-9A-F]+)\+0x[0-9A-F]+)?", l)
    if not m:
        print(l[:120])
        continue
    frames = threads.setdefault(m.group(1), [])
    if m.group(2) and (not frames or frames[-1] != m.group(2)):
        frames.append(m.group(2))
for tid, frames in threads.items():
    print(f"host {tid}: {' '.join(frames)[:width]}")
