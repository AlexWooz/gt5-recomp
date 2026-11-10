#!/usr/bin/env python3
"""Fingerprint of everything the generated code is made from, so an update knows when to re-lift.

    python scripts/lift_stamp.py           prints the current fingerprint
    python scripts/lift_stamp.py --write   records it as the fingerprint of the last lift (relift.ps1 does)
    python scripts/lift_stamp.py --check   exit 0 when the last lift is still current, 1 when a re-lift is due

The fingerprint covers the lift tools in ps3recomp/tools, the GT5 lift scripts, the game-code mods and
this copy's EBOOT.elf. Runtime-only changes (graphics, sound, input) leave it alone: those need a
rebuild, not a re-lift.
"""
import hashlib
import sys
from pathlib import Path

GT5 = Path(__file__).resolve().parent.parent
BASE = GT5.parent
STAMP = GT5 / "analysis" / "lift_stamp.txt"
SCRIPTS = ["relift.ps1", "filter_functions.py", "setjmp_patch.py", "version_addrs.py", "mods.py",
           "lift_spu_jobs.py"]


def inputs():
    files = sorted((BASE / "ps3recomp" / "tools").glob("*.py"))
    files += [GT5 / "scripts" / s for s in SCRIPTS]
    files += sorted((GT5 / "mods").glob("*/mod.json")) if (GT5 / "mods").is_dir() else []
    files += [GT5 / "input" / "EBOOT.elf"]
    return files


def fingerprint():
    h = hashlib.sha1()
    for f in inputs():
        h.update(str(f.relative_to(BASE)).replace("\\", "/").encode())
        h.update(b"\0")
        # Line endings don't change what the tools do.
        h.update(f.read_bytes().replace(b"\r\n", b"\n") if f.exists() else b"<missing>")
        h.update(b"\0")
    return h.hexdigest()


def main():
    fp = fingerprint()
    if "--write" in sys.argv:
        STAMP.parent.mkdir(exist_ok=True)
        STAMP.write_text(fp + "\n")
        print(f"lift stamp written: {fp[:12]}")
    elif "--check" in sys.argv:
        last = STAMP.read_text().strip() if STAMP.exists() else ""
        if last == fp:
            print("lift is current")
            sys.exit(0)
        print("re-lift needed" + ("" if last else " (no record of a lift)"))
        sys.exit(1)
    else:
        print(fp)


if __name__ == "__main__":
    main()
