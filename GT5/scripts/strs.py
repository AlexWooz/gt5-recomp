"""Print strings from the ELF that match regexes, with their guest address (rodata maps file off + 0x10000)."""
import re
import sys

d = open(sys.argv[1], "rb").read()
BASE = 0x10000
strs = list(re.finditer(rb"[\x20-\x7e]{3,200}", d))
for pat in sys.argv[2:]:
    rx = re.compile(pat.encode())
    print(f"== {pat}")
    n = 0
    for m in strs:
        if rx.search(m.group(0)):
            print(f"  0x{m.start() + BASE:08X} {m.group(0).decode('latin1')}")
            n += 1
            if n > 60:
                print("  ...")
                break
