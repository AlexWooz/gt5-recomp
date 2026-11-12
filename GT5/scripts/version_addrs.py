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
    its dispatch sequence, which has no absolute address in it. Its case labels come from the table
    the game copies them from: the one run of words in the data segment that all point into the code
    right next to that dispatch. Game updates (2.xx) moved both, and changed the two field offsets in
    the dispatch, so those are wildcards.

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
    "40126D2D14B0A95B9601BB11E3A6E778186067AC": "BCUS98114 (North America) 01.00",
    "49A3662D4B541BB4E0C2E72D2602F89FEB710E71": "BCAS20108 (Asia) 01.00",
    "96DEA35B194AA80E993CAD9EBE42E96072F3A3D5": "BCAS20108 (Asia) update 2.01",
}

SETJMP = [0x3863000F, 0x786306E4, 0xF8230000]    # addi r3,r3,15; clrrdi r3,r3,4; std r1,0(r3)
LONGJMP = [0x3863000F, 0x786306E4, 0xE8230000]   # addi r3,r3,15; clrrdi r3,r3,4; ld r1,0(r3)

# The computed goto: ld r2,40(r1); lwz r9,D(r30); slwi r9,r9,2; extsw r9,r9; add r9,r9,r25;
# lwz r9,T(r9); mtctr r9; bctr. The bctr is the last word. D and T are field offsets
# (disc 01.00: 76 and 256; update 2.01: 72 and 252), so they are left out of the match.
DISPATCH = [0xE8410028, 0x813E004C, 0x5529103A, 0x7D2907B4, 0x7D29CA14, 0x81290100, 0x7D2903A6, 0x4E800420]
DISPATCH_MASK = [0xFFFFFFFF, 0xFFFF0000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF0000, 0xFFFFFFFF, 0xFFFFFFFF]
NEAR = 0x1000   # the case labels lie within this distance of the bctr (01.00: -0x2DC..+0x250)


def load(path):
    d = path.read_bytes()
    if d[:4] != b"\x7fELF":
        sys.exit(f"{path} is not an ELF (decrypt EBOOT.BIN first)")
    e_phoff = struct.unpack(">Q", d[32:40])[0]
    e_shoff = struct.unpack(">Q", d[40:48])[0]
    e_phentsize, e_phnum, e_shentsize, e_shnum = struct.unpack(">HHHH", d[54:62])
    text = None
    data = []
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        p_type, p_flags, p_offset, p_vaddr, _, p_filesz, _ = struct.unpack(">IIQQQQQ", d[o:o + 48])
        if p_type == 1 and p_flags & 1 and p_filesz and not text:   # PT_LOAD, executable
            text = (p_offset, p_vaddr, p_filesz)
        elif p_type == 1 and not p_flags & 1 and p_filesz:          # PT_LOAD, data
            data.append((p_offset, p_vaddr, p_filesz))
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
    return d, base, code_end, words, data


def find(words, base, sig, mask=None):
    mask = mask or [0xFFFFFFFF] * len(sig)
    hits = []
    first, m0 = sig[0], mask[0]
    for i in range(len(words) - len(sig)):
        if words[i] & m0 == first & m0 and all(words[i + k] & mask[k] == sig[k] & mask[k] for k in range(len(sig))):
            hits.append(base + 4 * i)
    return hits


def label_tables(d, data, lo, hi):
    """Runs of 32-bit words in the data segments that all point at instructions in [lo, hi)."""
    runs = []
    for off, _, size in data:
        n = size // 4
        w = struct.unpack(f">{n}I", d[off:off + n * 4])
        i = 0
        while i < n:
            if lo <= w[i] < hi and w[i] % 4 == 0:
                j = i
                while j < n and lo <= w[j] < hi and w[j] % 4 == 0:
                    j += 1
                if j - i >= 2:
                    runs.append(list(w[i:j]))
                i = j
            else:
                i += 1
    return runs


def main():
    d, base, code_end, words, data = load(ELF)
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
    disp = find(words, base, DISPATCH, DISPATCH_MASK)
    if len(disp) != 1:
        problems.append(f"computed goto: {len(disp)} matches of its dispatch sequence, expected 1")
    else:
        bctr = disp[0] + 4 * (len(DISPATCH) - 1)
        tables = label_tables(d, data, max(base, bctr - NEAR), min(code_end, bctr + NEAR))
        if len(tables) != 1:
            problems.append(f"computed goto at 0x{bctr:08X}: {len(tables)} label tables found, expected 1")
        else:
            cases = tables[0]
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
