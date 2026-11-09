import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Find a symbol across dumped scripts and show the symbols around it: adcgrep.py <word> [width]"""
import glob
import os
import re
import sys

word = sys.argv[1].encode()
width = int(sys.argv[2]) if len(sys.argv) > 2 else 700
for p in sorted(glob.glob((GT5_DIR + r"\analysis\adc\*"))):
    d = open(p, "rb").read()
    i = d.find(word)
    if i < 0:
        continue
    print("==", os.path.basename(p), "hits", d.count(word), "at", hex(i))
    chunk = d[max(0, i - width // 3):i + width]
    print(" ".join(m.group().decode() for m in re.finditer(rb"[\x20-\x7e]{3,}", chunk)))
