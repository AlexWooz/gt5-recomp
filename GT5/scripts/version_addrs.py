#!/usr/bin/env python3
"""Find, in this copy's EBOOT.elf, the few addresses the lift needs that differ between GT5 releases.

    python scripts/version_addrs.py [input/EBOOT.elf]

Writes analysis/version.json (read by relift.ps1 and setjmp_patch.py) and analysis/label_jumps.json
(the lifter's --label-jumps file). Nothing here is tied to one release's addresses; each value is
found by what the code looks like:

  * code end: the end of the executable sections, from the ELF section headers;
  * libc setjmp and longjmp: the only routines opening with addi r3,r3,15 / clrrdi r3,r3,4 and then
    storing (setjmp) or loading (longjmp) r1 at 0(r3);
  * the computed goto whose label table is filled at run time (the lifter cannot read it): found by
    its dispatch sequence, which has no absolute address in it, with its four case labels moved by
    the same distance and checked against the instructions expected there.

Exit status 1 when something was not found; the message says what.
"""
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ELF = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "input" / "EBOOT.elf"
OUT = ROOT / "analysis"

# Releases this was checked against: SHA-1 of the decrypted EBOOT.elf.
KNOWN = {
    "1535B865EC7DD58E7704B378B8621532ECF844E1": "BCES00569 (Europe) 01.00",
}

SETJMP = [0x3863000F, 0x786306E4, 0xF8230000]    # addi r3,r3,15; clrrdi r3,r3,4; std r1,0(r3)
LONGJMP = [0x3863000F, 0x786306E4, 0xE8230000]   # addi r3,r3,15; clrrdi r3,r3,4; ld r1,0(r3)

# The computed goto: ld r2,40(r1); lwz r9,76(r30); slwi r9,r9,2; extsw r9,r9; add r9,r9,r25;
# lwz r9,256(r9); mtctr r9; bctr. The bctr is the last word.
DISPATCH = [0xE8410028, 0x813E004C, 0x5529103A, 0x7D2907B4, 0x7D29CA14, 0x81290100, 0x7D2903A6, 0x4E800420]
# Where its case labels sit relative to the bctr in the Europe 01.00 build, and the first two
# instructions at each, which say the label really is there.
CASES = [
    (0x04, [0x813E002C, 0x7F64DB78]),
    (0x9C, [0x813C002C, 0x3929FFFC]),
    (0x250, [0x807E004C, 0x48000080]),
    (-0x2DC, [0x801E0050, 0x38600000]),
]


def load(path):
    d = path.read_bytes()
    if d[:4] != b"\x7fELF":
        sys.exit(f"{path} is not an ELF (decrypt EBOOT.BIN first)")
    e_phoff = struct.unpack(">Q", d[32:40])[0]
    e_shoff = struct.unpack(">Q", d[40:48])[0]
    e_phentsize, e_phnum, e_shentsize, e_shnum = struct.unpack(">HHHH", d[54:62])
    text = None
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        p_type, p_flags, p_offset, p_vaddr, _, p_filesz, _ = struct.unpack(">IIQQQQQ", d[o:o + 48])
        if p_type == 1 and p_flags & 1 and p_filesz:          # PT_LOAD, executable
            text = (p_offset, p_vaddr, p_filesz)
            break
    if not text:
        sys.exit("no executable segment")
    code_end = 0
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        _, _, flags, addr, _, size = struct.unpack(">IIQQQQ", d[o:o + 40])
        if flags & 4 and size:                                  # SHF_EXECINSTR
            code_end = max(code_end, addr + size)
    off, base, size = text
    end = min(code_end or base + size, base + size)
    n = (end - base) // 4
    words = struct.unpack(f">{n}I", d[off:off + n * 4])
    return d, base, code_end, words


def find(words, base, sig):
    hits = []
    first = sig[0]
    for i in range(len(words) - len(sig)):
        if words[i] == first and list(words[i:i + len(sig)]) == sig:
            hits.append(base + 4 * i)
    return hits


def main():
    d, base, code_end, words = load(ELF)
    sha1 = hashlib.sha1(d).hexdigest().upper()
    release = KNOWN.get(sha1, "not one this was tested with")
    print(f"EBOOT.elf SHA-1 {sha1}: {release}")
    print(f"code end 0x{code_end:X}")
    problems = []
    sj, lj = find(words, base, SETJMP), find(words, base, LONGJMP)
    if len(sj) != 1 or len(lj) != 1:
        problems.append(f"setjmp/longjmp: {len(sj)}/{len(lj)} matches, expected 1/1")
    else:
        print(f"setjmp 0x{sj[0]:08X}, longjmp 0x{lj[0]:08X}")
    label_jumps = {}
    disp = find(words, base, DISPATCH)
    if len(disp) != 1:
        problems.append(f"computed goto: {len(disp)} matches of its dispatch sequence, expected 1")
    else:
        bctr = disp[0] + 4 * (len(DISPATCH) - 1)
        cases = []
        for rel, expect in CASES:
            a = bctr + rel
            i = (a - base) // 4
            got = list(words[i:i + len(expect)]) if 0 <= i < len(words) else []
            if got != expect:
                problems.append(f"computed goto at 0x{bctr:08X}: no case label at 0x{a:08X}")
            cases.append(a)
        label_jumps[f"0x{bctr:08X}"] = [f"0x{c:08X}" for c in cases]
        print(f"computed goto 0x{bctr:08X} -> " + ", ".join(f"0x{c:08X}" for c in cases))
    OUT.mkdir(exist_ok=True)
    info = {"sha1": sha1, "release": release, "code_end": f"0x{code_end:X}",
            "setjmp": f"{sj[0]:08X}" if len(sj) == 1 else None,
            "longjmp": f"{lj[0]:08X}" if len(lj) == 1 else None,
            "label_jumps": label_jumps, "problems": problems}
    (OUT / "version.json").write_text(json.dumps(info, indent=2) + "\n")
    (OUT / "label_jumps.json").write_text(json.dumps(label_jumps, indent=2) + "\n")
    for p in problems:
        print("NOT FOUND:", p)
    if problems:
        print("The lift can still run, but this release needs those values found by hand; see README.md.")
        sys.exit(1)


if __name__ == "__main__":
    main()
