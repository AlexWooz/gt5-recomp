"""Which win32u and ntdll calls a prof.ps1 capture sits in, for its busiest renderer thread: kcalls.py <prof.txt> [--tid n]

win32u.dll is mostly thin kernel-call stubs (NtGdiDdDDI* is the D3D kernel interface), so naming the export names the
kernel call. Prints each thread's share of samples per call; a thread is picked by its nvwgf driver samples.
"""
import collections
import re
import struct
import sys


def exports(path):
    d = open(path, "rb").read()
    pe = struct.unpack("<I", d[0x3C:0x40])[0]
    nsec = struct.unpack("<H", d[pe + 6:pe + 8])[0]
    optsz = struct.unpack("<H", d[pe + 20:pe + 22])[0]
    opt = pe + 24
    exp_rva = struct.unpack("<I", d[opt + 112:opt + 116])[0]
    secs = []
    for i in range(nsec):
        s = pe + 24 + optsz + i * 40
        vsz, va, rsz, ptr = struct.unpack("<IIII", d[s + 8:s + 24])
        secs.append((va, max(vsz, rsz), ptr))

    def off(rva):
        for va, sz, ptr in secs:
            if va <= rva < va + sz:
                return rva - va + ptr
        raise ValueError(hex(rva))

    e = off(exp_rva)
    nfun, nnames, afun, aname, aord = struct.unpack("<IIIII", d[e + 20:e + 40])
    funcs = struct.unpack("<%dI" % nfun, d[off(afun):off(afun) + 4 * nfun])
    names = struct.unpack("<%dI" % nnames, d[off(aname):off(aname) + 4 * nnames])
    ords = struct.unpack("<%dH" % nnames, d[off(aord):off(aord) + 2 * nnames])
    out = []
    for n, o in zip(names, ords):
        p = off(n)
        out.append((funcs[o], d[p:d.index(b"\0", p)].decode()))
    out.sort()
    return out


def name(table, rva):
    lo, hi = 0, len(table)
    while lo < hi:
        m = (lo + hi) // 2
        if table[m][0] <= rva:
            lo = m + 1
        else:
            hi = m
    return table[lo - 1][1] if lo else "?"


tables = {"win32u.dll": exports(r"C:\Windows\System32\win32u.dll"), "ntdll.dll": exports(r"C:\Windows\System32\ntdll.dll")}
rows = collections.defaultdict(collections.Counter)
total = collections.Counter()
for line in open(sys.argv[1], errors="replace"):
    p = line.split()
    if line.startswith("#") or len(p) < 4:
        continue
    tid, mod, rva, cnt = p[0], p[1], p[2], int(p[3])
    total[tid] += cnt
    base = mod.split("\\")[-1].lower()
    if base in tables:
        rows[tid][base.split(".")[0] + ":" + name(tables[base], int(rva, 16))] += cnt
    else:
        rows[tid][base] += cnt
for tid, c in sorted(rows.items(), key=lambda kv: -kv[1].get("nvwgf2umx.dll", 0))[:2]:
    print("thread", tid, "samples", total[tid])
    for k, n in c.most_common(22):
        print("  %5.1f%%  %s" % (100.0 * n / total[tid], k[:70]))
