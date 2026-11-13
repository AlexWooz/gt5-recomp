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
    "12EF249F9B95591E08251AC85F103B1451DA23BF": "BCUS98114 (North America) update 2.17",
    "306F86C62B9F03041C903BE96AC59E5C3F130452": "BCES00569 (Europe) update 2.17",
}

SETJMP = [0x3863000F, 0x786306E4, 0xF8230000]    # addi r3,r3,15; clrrdi r3,r3,4; std r1,0(r3)
LONGJMP = [0x3863000F, 0x786306E4, 0xE8230000]   # addi r3,r3,15; clrrdi r3,r3,4; ld r1,0(r3)

# The computed goto: lwz rA,D(rB); slwi rA,rA,2; extsw rA,rA; add rA,rA,rT; lwz rA,T(rA);
# mtctr rA; bctr. The bctr is the last word. D and T are field offsets (disc 01.00: 76 and 256;
# updates 2.01 and 2.17: 72 and 252) and the registers vary between releases too (01.00 and 2.01
# read D off r30 after an ld r2,40(r1); 2.17 reads it off r31 with no TOC reload), so only the
# opcodes are matched.
DISPATCH = [0x80000000, 0x5400103A, 0x7C0007B4, 0x7C000214, 0x80000000, 0x7C0903A6, 0x4E800420]
DISPATCH_MASK = [0xFC000000, 0xFC00FFFF, 0xFC00FFFE, 0xFC0007FE, 0xFC000000, 0xFC1FFFFF, 0xFFFFFFFF]
NEAR = 0x1000   # the case labels lie within this distance of the bctr (01.00: -0x2DC..+0x250)
STORE_SCAN = 0x400   # how far before the dispatch to look for code that stores the labels (2.17)


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


def stored_labels(words, base, disp, lo, hi):
    """The case labels when the game stores them with code instead of copying a data table (2.17).

    The dispatch reads its label from T(rA), with rA = the table's base plus 4 * the case number. The
    function fills that table on its first call: lis rX,hi / addi rX,rX,lo, then stw rX,T+4k(rBase).
    Returns the labels in case order, or None when the stores aren't there or don't form 0, 1, 2, ..."""
    t = words[(disp - base) // 4 + 4] & 0xFFFF            # T of lwz rA,T(rA)
    end = (disp - base) // 4
    start = max(0, end - STORE_SCAN // 4)
    regs, slots = {}, {}
    for i in range(start, end):
        x = words[i]
        op, rd, ra, imm = x >> 26, (x >> 21) & 31, (x >> 16) & 31, x & 0xFFFF
        if op == 15 and ra == 0:                            # lis rd,imm
            regs[rd] = imm << 16
        elif op == 14 and ra != 0 and ra in regs:           # addi rd,ra,imm (ra=0 is li)
            regs[rd] = (regs[ra] + (imm - 0x10000 if imm & 0x8000 else imm)) & 0xFFFFFFFF
        elif op == 36 and rd in regs and imm >= t and (imm - t) % 4 == 0:   # stw rd,imm(ra)
            v = regs[rd]
            if lo <= v < hi and v % 4 == 0:
                slots[(imm - t) // 4] = v
        elif op not in (15, 14, 36) and rd in regs and op not in (11, 10, 16, 18):
            regs.pop(rd, None)                              # rd overwritten by something else
    if len(slots) < 2 or sorted(slots) != list(range(len(slots))):
        return None
    return [slots[k] for k in range(len(slots))]


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
        lo, hi = max(base, bctr - NEAR), min(code_end, bctr + NEAR)
        tables = label_tables(d, data, lo, hi)
        if not tables:
            stored = stored_labels(words, base, disp[0], lo, hi)
            if stored:
                tables = [stored]
        if len(tables) != 1:
            problems.append(f"computed goto at 0x{bctr:08X}: {len(tables)} label tables found, expected 1")
        else:
            # The lifter makes one switch case per target, so each address once (2.17's table
            # sends two cases to the same label).
            cases = list(dict.fromkeys(tables[0]))
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
