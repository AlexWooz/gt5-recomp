"""The made-ahead lists by program (rsx_shader_prep.inc): look at them, merge players' lists, make the one
that ships with the port.

A record names one pipeline by its two programs' keys (hashes of their own bytes) and every input of
their translation; it holds no game code or data. A player's game writes them to
GT5\\scratch\\shader_keys\\shader_keys.lst; the launcher's "Save logs for a report" writes the same records
as base64 text (gt5-shader-keys.txt), which this reads too.

  shaderkeys.py stats <list>...                    what is in them, by release and settings kind
  shaderkeys.py releases <list>...                 the releases in them (disc id + EBOOT size)
  shaderkeys.py merge -o <out.lst> <list>...       one list, each record once (first copy kept)
  shaderkeys.py ship [-o <out.lst>] <list>...      the shipped list (default share_docs/GT5/shaders/known_d3d12.lst):
                                                   merged, damaged and old ones dropped, sorted so a new version
                                                   of the list diffs small
  shaderkeys.py text -o <out.txt> <list>           as the launcher saves it (base64, a record a line)

merge / ship take --release ID[:EBOOTSIZE] (repeatable; records of no known release, from version-1 lists,
go with any release unless --strict) and --min-version N (default: the current record version, so a list
made before a change of the translation's inputs is left out; --min-version 1 keeps old records, which are
converted).

Versions. A record's version is the version of its set of translation inputs (LD_PREP_VERSION in
rsx_shader_prep.inc), not of the generator: the HLSL may change freely and records stay good, because the
game translates them again and uses the result only when a draw makes the same text. When the sibling work
on the cache key adds an input to the translation, LD_PREP_VERSION goes up; records of the old version are
either converted (when the new input has a value meaning "as before"; add the conversion here and in
ld_prep_add_list) or dropped (ship's --min-version). The game reads version-1 records (round 1's test
lists) as "made under any settings".

The game reads the shipped list from GT5\\shaders\\known_d3d12.lst and uses records of any release (their
programs are matched by content, so another release's records cost nothing when their programs never
come). Records made under other settings are skipped by kind (Shadows; render scale / upscaler; motion
vectors; the upscaler's resolve), only for records whose text depends on that kind; `stats` shows the mix.
"""
import argparse
import base64
import collections
import os
import struct
import sys

REC = 512
REC_V1 = 496
MAGIC = 0x31504B53        # "SKP1"
VERSION = 2
CHECK_AT = 504
FNV_OFF = 1469598103934665603
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1
KINDS = ("Shadows", "render scale / upscaler", "motion vectors", "upscaler resolve", "render scale (pipeline only)")
FLAGS = {1: "masked layout", 2: "packed", 4: "motion vectors", 8: "one-tap resolve", 16: "WPOS grid", 32: "WPOS scale",
         64: "branch bits"}


def fnv1a(data, h=FNV_OFF):
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & MASK64
    return h


def from_v1(b):
    """A version-1 record (496 bytes) as version 2: any settings, no release (as the game reads it)."""
    magic, ver, size = struct.unpack_from("<III", b, 0)
    if magic != MAGIC or ver != 1 or size != REC_V1 or struct.unpack_from("<Q", b, 488)[0] != fnv1a(b[:488]):
        return None
    r = bytearray(REC)
    r[:488] = b[:488]
    struct.pack_into("<II", r, 4, VERSION, REC)
    flags, = struct.unpack_from("<I", r, 12)
    struct.pack_into("<I", r, 12, flags & 0xFF)
    struct.pack_into("<I", r, 92, 0)          # cfg: any
    struct.pack_into("<Q", r, CHECK_AT, fnv1a(bytes(r[:CHECK_AT])))
    return bytes(r)


def valid(b):
    magic, ver, size = struct.unpack_from("<III", b, 0)
    if magic != MAGIC or ver != VERSION or size != REC:
        return False
    return struct.unpack_from("<Q", b, CHECK_AT)[0] == fnv1a(b[:CHECK_AT])


def read_records(path):
    """Records (version 2, converted from version 1 when need be) from a binary list or the launcher's
    base64 text; (records, damaged, version-1 count)."""
    raw = open(path, "rb").read()
    blobs = []
    text_like = raw[:1] != b"S" and all(32 <= c < 127 or c in (9, 10, 13) for c in raw[:4096])
    if text_like:
        for line in raw.decode("ascii", "replace").splitlines():
            line = line.strip()
            if not line:
                continue
            try:
                blobs.append(base64.b64decode(line, validate=True))
            except Exception:
                blobs.append(None)
    else:
        size = REC
        if len(raw) >= 12:
            ver, sz = struct.unpack_from("<II", raw, 4)
            if ver == 1 and sz == REC_V1:
                size = REC_V1
        for i in range(len(raw) // size):
            blobs.append(raw[i * size:(i + 1) * size])
    good, bad, v1 = [], 0, 0
    for b in blobs:
        if b is not None and len(b) == REC_V1:
            b = from_v1(b)
            if b is not None:
                v1 += 1
                good.append((b, 1))
                continue
        if b is None or len(b) != REC or not valid(b):
            bad += 1
        else:
            good.append((b, VERSION))
    return good, bad, v1


def rec_id(b):
    return fnv1a(b[:CHECK_AT]) or 1


def fields(b):
    flags, = struct.unpack_from("<I", b, 12)
    vp_key, fp_key = struct.unpack_from("<QQ", b, 16)
    vp_instrs, vp_start, fp_size = struct.unpack_from("<III", b, 32)
    cfg, = struct.unpack_from("<I", b, 92)
    fp_head, vp_head = struct.unpack_from("<II", b, 104)
    title = b[488:500].split(b"\0", 1)[0].decode("ascii", "replace")
    eboot, = struct.unpack_from("<I", b, 500)
    return dict(flags=flags, vp_key=vp_key, fp_key=fp_key, fp_size=fp_size, cfg=cfg, deps=(flags >> 8) & 0x1F,
                fp_head=fp_head, vp_head=vp_head, title=title, eboot=eboot)


def release_of(b):
    f = fields(b)
    return (f["title"], f["eboot"])


def release_name(rel):
    t, e = rel
    return "%s:%d" % (t, e) if t or e else "(none: version-1 list)"


def parse_releases(args):
    out = []
    for a in args or []:
        t, _, e = a.partition(":")
        out.append((t, int(e) if e else None))
    return out


def merge(paths, releases=None, strict=False, min_version=VERSION):
    out, seen, bad, old, other = [], set(), 0, 0, 0
    per_file = []
    for p in paths:
        recs, b, v1 = read_records(p)
        bad += b
        added = 0
        for r, ver in recs:
            if ver < min_version:
                old += 1
                continue
            if releases:
                t, e = release_of(r)
                if not t and not e:
                    if strict:
                        other += 1
                        continue
                elif not any(t == rt and (re is None or e == re) for rt, re in releases):
                    other += 1
                    continue
            i = rec_id(r)
            if i in seen:
                continue
            seen.add(i)
            out.append(r)
            added += 1
        per_file.append((p, len(recs), added, b, v1))
    return out, dict(damaged=bad, old=old, other_release=other), per_file


def cmd_stats(a):
    recs, drop, per = merge(a.lists, min_version=1)
    for p, n, added, b, v1 in per:
        print("%s: %d records (%d new to the merge, %d of version 1), %d damaged" % (p, n, added, v1, b))
    if not recs:
        print("no records")
        return
    f = [fields(r) for r in recs]
    vps = set(x["vp_key"] for x in f)
    fps = set(x["fp_key"] for x in f)
    pairs = collections.Counter((x["vp_key"], x["fp_key"]) for x in f)
    print("%d pipelines, %d vertex programs, %d fragment programs, %d program pairs (pipelines per pair: max %d, mean %.2f)" % (
        len(recs), len(vps), len(fps), len(pairs), max(pairs.values()), len(recs) / len(pairs)))
    rel = collections.Counter(release_of(r) for r in recs)
    print("releases: " + ", ".join("%s %d" % (release_name(k), v) for k, v in rel.most_common()))
    print("text depends on no setting: %d" % sum(1 for x in f if not x["deps"]))
    for k, name in enumerate(KINDS):
        dep = [x for x in f if x["deps"] & (1 << k)]
        if not dep:
            continue
        byte = k if k < 4 else 1   # the pipeline-only scale kind uses the scale byte
        vals = collections.Counter((x["cfg"] >> (8 * byte)) & 0xFF for x in dep)
        print("  depends on %s: %d (settings values: %s)" % (
            name, len(dep), ", ".join("%s %d" % ("any" if v == 0 else "%02X" % v, n) for v, n in vals.most_common())))
    fl = collections.Counter()
    for x in f:
        for bit, n in FLAGS.items():
            if x["flags"] & bit:
                fl[n] += 1
    print("  " + ", ".join("%s %d" % kv for kv in fl.most_common()))
    print("size %.1f MB (%d bytes a record)" % (len(recs) * REC / 1e6, REC))


def cmd_releases(a):
    recs, drop, per = merge(a.lists, min_version=1)
    rel = collections.Counter(release_of(r) for r in recs)
    for k, v in rel.most_common():
        print("%-28s %d" % (release_name(k), v))


def write_list(path, recs):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "wb") as o:
        for r in recs:
            o.write(r)
    os.replace(tmp, path)


def drop_words(d):
    return "%d damaged, %d of an older version, %d of other releases left out" % (d["damaged"], d["old"], d["other_release"])


def cmd_merge(a):
    recs, d, per = merge(a.lists, parse_releases(a.release), a.strict, a.min_version)
    write_list(a.out, recs)
    print("%s: %d records from %d lists (%s)" % (a.out, len(recs), len(a.lists), drop_words(d)))


def cmd_ship(a):
    recs, d, per = merge(a.lists, parse_releases(a.release), a.strict, a.min_version)
    recs.sort(key=lambda r: (struct.unpack_from("<Q", r, 24)[0], struct.unpack_from("<Q", r, 16)[0], rec_id(r)))
    out = a.out or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "share_docs", "GT5", "shaders", "known_d3d12.lst")
    write_list(out, recs)
    print("%s: %d records, %.1f MB (%s)" % (os.path.normpath(out), len(recs), len(recs) * REC / 1e6, drop_words(d)))


def cmd_text(a):
    recs, d, per = merge(a.lists, min_version=1)
    with open(a.out, "w", newline="\n") as o:
        for r in recs:
            o.write(base64.b64encode(r).decode("ascii") + "\n")
    print("%s: %d records" % (a.out, len(recs)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    s = sp.add_parser("stats"); s.add_argument("lists", nargs="+"); s.set_defaults(fn=cmd_stats)
    s = sp.add_parser("releases"); s.add_argument("lists", nargs="+"); s.set_defaults(fn=cmd_releases)
    for name, fn, need_out in (("merge", cmd_merge, True), ("ship", cmd_ship, False)):
        s = sp.add_parser(name)
        s.add_argument("-o", "--out", required=need_out)
        s.add_argument("--release", action="append", help="ID[:EBOOTSIZE], e.g. BCES00569:26029448 (repeatable)")
        s.add_argument("--strict", action="store_true", help="with --release: leave out records of no known release")
        s.add_argument("--min-version", type=int, default=VERSION)
        s.add_argument("lists", nargs="+")
        s.set_defaults(fn=fn)
    s = sp.add_parser("text"); s.add_argument("-o", "--out", required=True); s.add_argument("lists", nargs="+"); s.set_defaults(fn=cmd_text)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
