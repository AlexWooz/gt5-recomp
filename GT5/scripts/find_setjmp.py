"""Locate the guest libc setjmp/longjmp and their call sites by instruction shape.

setjmp saves r13-r31 to [r3+d] and returns 0; longjmp loads them back and returns
r4. Neither has a symbol in a retail ELF, so the register runs are the signature.

  python find_setjmp.py input/EBOOT.elf [code_end]
"""
import collections
import struct
import sys

d = open(sys.argv[1], "rb").read()
code_end = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x111c684
BASE, OFF = 0x10000, 0
n = (code_end - BASE) // 4
words = struct.unpack(f">{n}I", d[OFF:OFF + n * 4])


def fields(w):
    return w >> 26, (w >> 21) & 31, (w >> 16) & 31


def scan(want_op):
    found = []
    i = 0
    while i < n - 64:
        regs = set()
        last = None
        for j in range(i, i + 64):
            op, rt, ra = fields(words[j])
            if op == want_op and ra == 3 and 13 <= rt <= 31:
                regs.add(rt)
                last = j
        if len(regs) >= 17:
            found.append((BASE + i * 4, BASE + last * 4, len(regs)))
            i = last + 1
        else:
            i += 1
    return found


def callers(target):
    out = []
    for i, w in enumerate(words):
        if w >> 26 == 18 and w & 3 == 1:  # bl
            li = w & 0x03FFFFFC
            if li & 0x02000000:
                li -= 0x04000000
            if BASE + i * 4 + li == target:
                out.append(BASE + i * 4)
    return out


def func_start(addr):
    """Walk back to the previous blr/b/bctr -- good enough for small leaf routines."""
    i = (addr - BASE) // 4
    while i > 0:
        w = words[i - 1]
        if w == 0x4E800020 or w == 0x4E800420 or w >> 26 == 18 and w & 1 == 0:
            return BASE + i * 4
        i -= 1
    return addr


def dump(addr, count):
    for k in range(count):
        a = addr + k * 4
        print(f"    {a:08X}: {words[(a - BASE) // 4]:08X}")


for label, op in (("setjmp (std run)", 62), ("longjmp (ld run)", 58)):
    hits = scan(op)
    print(f"== {label}: {len(hits)} candidate(s)")
    for first, last, regs in hits:
        start = func_start(first)
        cs = callers(start)
        print(f"  start=0x{start:08X} run=0x{first:08X}-0x{last:08X} regs={regs} bl-callers={len(cs)}")
        print("    callers:", " ".join(f"0x{c:08X}" for c in cs[:12]))
        dump(start, 6)
        print("    ...")
        dump(last + 4, 10)
