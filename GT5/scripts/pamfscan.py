"""Walk a dumped PAMF section stream (DMUX_DUMP): pamfscan.py <file> [--every n]

Per n MB: video and audio PES bytes so far, the last PTS of each, and how far video leads audio.
"""
import sys

path = sys.argv[1]
every = int(sys.argv[sys.argv.index("--every") + 1]) if "--every" in sys.argv else 1
d = open(path, "rb").read()
print("size %.1f MB, first bytes %s" % (len(d) / 1e6, d[:16].hex()))
PACK = 2048
kinds = {}
vb = ab = 0
vpts = apts = None
apts0 = vpts0 = None
next_print = 0
for o in range(0, len(d) - PACK + 1, PACK):
    p = d[o:o + PACK]
    if p[:4] != b"\0\0\1\xba":
        kinds["notpack"] = kinds.get("notpack", 0) + 1
        continue
    q = 14 + (p[13] & 7)
    if p[q:q + 4] == b"\0\0\1\xbb":
        q += 6 + (p[q + 4] << 8 | p[q + 5])
        if p[q:q + 4] == b"\0\0\1\xbf":
            q += 6 + (p[q + 4] << 8 | p[q + 5])
    if p[q:q + 3] != b"\0\0\1":
        kinds["noPES"] = kinds.get("noPES", 0) + 1
        continue
    sid = p[q + 3]
    plen = (p[q + 4] << 8 | p[q + 5]) + 6
    k = "%02X" % sid
    if sid == 0xBD:
        k += "/%02X" % p[q + 9 + p[q + 8]]
    kinds[k] = kinds.get(k, 0) + 1
    flags = p[q + 7]
    pts = None
    if flags & 0x80:
        t = p[q + 9:q + 14]
        pts = ((t[0] & 0x0E) << 29) | (t[1] << 22) | ((t[2] & 0xFE) << 14) | (t[3] << 7) | (t[4] >> 1)
    size = plen - 9 - p[q + 8]
    if (sid & 0xF0) == 0xE0:
        vb += size
        if pts is not None:
            vpts = pts
            vpts0 = vpts0 if vpts0 is not None else pts
    elif sid == 0xBD and p[q + 9 + p[q + 8]] == 0:
        ab += size
        if pts is not None:
            apts = pts
            apts0 = apts0 if apts0 is not None else pts
    if o >= next_print and vpts is not None and apts is not None:
        print("%6.1f MB  video %7.2f MB pts %8.3f s  audio %6.2f MB pts %8.3f s  lead %6.3f s" % (
            o / 1e6, vb / 1e6, vpts / 90000, ab / 1e6, apts / 90000, (vpts - apts) / 90000))
        next_print += every * 1000000
print(sorted(kinds.items()))
print("audio frames so far about %d (2744 bytes each)" % (ab / 2744))
