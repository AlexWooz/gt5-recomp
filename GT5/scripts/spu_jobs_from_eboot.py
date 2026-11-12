#!/usr/bin/env python3
"""Find GT5's SPU job images in this copy's EBOOT, for lift_spu_jobs.py to compile.

GT5 hands most of its SPU work (physics, sound, graphics set-up) to SPURS as raw job images
stored inside EBOOT.elf. Each one is found here by its hashes (no game data is stored in this
script) and written to analysis/spu_miss under the name the runtime would have dumped it with,
the fingerprint the runtime registers it by. lift_spu_jobs.py then lifts them to C, and the build
compiles them in. A job that isn't compiled in can't run: the runtime drops it, and whatever
waits on it stalls (a black screen after the logos, for one in the game updates).

The list is the European release (BCES00569, disc 01.00), which the other discs share, plus the
versions the game updates (2.xx) changed. A copy needs one version of each job; the summary says
how many it has.

    python scripts/spu_jobs_from_eboot.py [input/EBOOT.elf]
"""
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "analysis" / "spu_miss"
PREFIX = 64   # bytes hashed at each candidate offset
STEP = 16     # SPU images are at least quadword aligned

# (runtime fingerprint, size, SHA-1 of the first PREFIX bytes, SHA-1 of the whole image)
JOBS = [
    ("1B6BDAC375C5438D", 13840, "1dcbe5569e3ff030af74a4ee4a6bc68dc34b5916", "6428d06556207eb480f5f6f53d2b4637bc47a959"),
    ("48AF2BC79A72CB50", 25232, "d059dbdc8f7bc5bb07edd875d17cd28d34d172a4", "a33efe337060b6984fbe0d5c4b8ffcd83be6ae4f"),
    ("49C1D6C47E80AF43", 24720, "a5ca7ca714dfd3d42f6d2791c52e0aabe91d9fe5", "bab6bc8bd1c240db96c648e01ab549958fb5998e"),
    ("5DA195169669A5FF", 72464, "912583737f277c4b255face079ae6ce444b5d295", "724e11c2b86807bc4e7858f39a86dc010271c2f5"),
    ("650EE1A07AEBB151", 22032, "9a05ed1196c433d422230bbe2c81492c5d014ce8", "600792fb187ca897f81879dea87b6fbec6aa8163"),
    ("75BFE68AE96D7577", 12032, "3e6a0b4551e424a7315f2dca03bb8a202ff47486", "3cad156b9d0d985e4788140db9728da0bf8cd734"),
    ("80435D36E623FAA0", 19488, "7928e1212b01f0881edfa032020cca4068960679", "8c77e02b4b9bbb84e492f37ac2c332ab1985d485"),
    ("82D0BE145EC28AEE", 17424, "49b0dff67aa20024f04415f899cb6c82b3ad8dc1", "2e1a55c099adba1ce3f2ac1bb8dd7e815bec546b"),
    ("9804ABA84566F431", 20624, "7b875f4adf8082ec22cc4cf0ed3b4a23430da201", "ec458520f805b66025656eda6431d0fdff7bc144"),
    ("ABD49FC58F53F368", 9488, "8ebad749fab419a62a8db9bd90d817e3a4a758b4", "1ba431ca767a4016eab4c06218a0bcca0e60bf30"),
    ("D58063CD2A0236B5", 53408, "078fd4721b87c3870605f474cbab013c6ae0154e", "e934703d61aca2e7f025503e3326dd8d57c21f01"),
    ("EB79FDFCD7112702", 22288, "7276512751ce9dfae150210e4029aa03592499de", "38b144965d2b3a89bc29cc8a6e1970ea26424120"),
    ("F523F26F69764538", 64912, "f767a6c12afd282eb9a9997df10760a79138ba7e", "eb871a28c52e2f5d430fc93145ca3e51291e2fae"),
    ("FDFFAAC1BCD5352E", 34704, "6282b2e494736583faf042793e20804657c901b0", "61d01493b972923284dad53ece6c80516b0af794"),
    ("FE031AF67ED048DE", 46928, "6872f373fae3b3578750a555b2dbf54d3ad2a5da", "65c1baa23a23c6a15eed04ae15137270fcc4d86c"),
]

# Versions changed by game updates: (fingerprint, size, prefix SHA-1, whole SHA-1, the job above it replaces).
UPDATED = [
    ("C2A031BC164B4A45", 21952, "c7f26544a13fbe51db2e9f71e56d8c08ea06f0e3", "7ea77243d007ad477175d54ceae94d25297741dc", "650EE1A07AEBB151"),  # 2.01
    ("E3FDDA10963A6130", 22544, "2a759a61e4fa122c5344b18265352c9ac4836e40", "fe409a4e6c7f4e72c21021aaadd47cd0d7a0eefb", "EB79FDFCD7112702"),  # 2.01
]


def main():
    eboot = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "input" / "EBOOT.elf"
    data = eboot.read_bytes()
    by_prefix = {}
    for job in JOBS + UPDATED:
        by_prefix.setdefault(job[2], []).append(job[:4])
    found = {}
    sha1 = hashlib.sha1
    for off in range(0, len(data) - PREFIX, STEP):
        cands = by_prefix.get(sha1(data[off:off + PREFIX]).hexdigest())
        if not cands:
            continue
        for fp, size, _, full in cands:
            if fp not in found and sha1(data[off:off + size]).hexdigest() == full:
                found[fp] = (off, data[off:off + size])
    OUT.mkdir(parents=True, exist_ok=True)
    for fp, (off, blob) in sorted(found.items()):
        (OUT / f"spujob_{fp}_{len(blob)}.bin").write_bytes(blob)
    have = set(found) | {old for fp, *_, old in UPDATED if fp in found}
    missing = [fp for fp, *_ in JOBS if fp not in have]
    print(f"SPU jobs found in {eboot.name}: {len(JOBS) - len(missing)} of {len(JOBS)}")
    for fp in missing:
        print(f"  not found: {fp} (or a version of it this script doesn't know yet; the game stalls when it needs it)")


if __name__ == "__main__":
    main()
