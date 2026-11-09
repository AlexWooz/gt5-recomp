import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Print a dumped script's symbol strings: adcstr.py <name part> [start] [length]"""
import glob
import os
import re
import sys

base = (GT5_DIR + r"\analysis\adc")
start = int(sys.argv[2]) if len(sys.argv) > 2 else 0
length = int(sys.argv[3]) if len(sys.argv) > 3 else 6000
for p in glob.glob(os.path.join(base, f"*{sys.argv[1]}*")):
    d = open(p, "rb").read()
    seen = []
    known = set()
    for m in re.finditer(rb"[\x20-\x7e]{3,}", d):
        x = m.group().decode()
        if x not in known:
            known.add(x)
            seen.append(x)
    text = " ".join(seen)
    print("==", os.path.basename(p), len(d), "text", len(text))
    print(text[start:start + length])
