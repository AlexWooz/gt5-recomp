"""Which ntdll calls a prof.ps1 capture sits in, per thread: ntwaits.py <prof.txt> [--threads N] [--log err]

Names come from ntdll's export table, so internal functions show as the nearest export before them.
"""
import bisect
import collections
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


def arg(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


ex = exports(r"C:\Windows\System32\ntdll.dll")
rvas = [e[0] for e in ex]


def name(rva):
    i = bisect.bisect_right(rvas, rva) - 1
    return ex[i][1] if i >= 0 else "?"


guest = {}
log = arg("--log", None)
if log:
    import re
    for line in open(log, encoding="utf8", errors="replace"):
        m = re.match(r"\[THREAD (\d+)\] host thread started.*hosttid=(\d+)", line)
        if m:
            guest[int(m.group(2))] = m.group(1)
cpu = {}
per = collections.defaultdict(collections.Counter)
tot = collections.Counter()
for line in open(sys.argv[1], encoding="utf8", errors="replace"):
    p = line.split()
    if line.startswith("#cpu"):
        cpu[int(p[1])] = int(p[2])
    elif len(p) >= 4:
        n = int(p[-1])
        lab = name(int(p[-2], 16)) if " ".join(p[1:-2]).lower() == "ntdll.dll" else "(other code)"
        per[int(p[0])][lab] += n
        tot[lab] += n
for t in sorted(cpu, key=lambda x: -cpu[x])[:int(arg("--threads", 12))]:
    all_n = sum(per[t].values()) or 1
    print("thread %d guest %s cpu %d%%: %s" % (t, guest.get(t, "-"), cpu[t],
          ", ".join("%s %.0f%%" % (k, 100.0 * v / all_n) for k, v in per[t].most_common(5))))
print("all:", ", ".join("%s %.0f%%" % (k, 100.0 * v / sum(tot.values())) for k, v in tot.most_common(10)))
