"""List GPUSyncArray jobs from gmem.py dumps: joblist.py <lists.bin> <lists ea> <jobs.bin> <jobs ea> <list ea> [--find ea]

Each entry is {u16 size, u16 valid, u32 job}. Prints index, job address and the segment each job writes (the
job's first pointer into the command area 0x61xxxxxx). --find names the job holding a given address.
"""
import struct
import sys

find = None
if "--find" in sys.argv:
    i = sys.argv.index("--find")
    find = int(sys.argv[i + 1], 16)
    del sys.argv[i:i + 2]
lists = open(sys.argv[1], "rb").read()
lists_ea = int(sys.argv[2], 16)
jobs = open(sys.argv[3], "rb").read()
jobs_ea = int(sys.argv[4], 16)
list_ea = int(sys.argv[5], 16)
off = list_ea - lists_ea
prev = None
idx = 0
entries = []
while off + 8 <= len(lists):
    size, valid, job = struct.unpack_from(">HHI", lists, off)
    if valid == 0:
        break
    entries.append((idx, size, valid, job))
    off += 8
    idx += 1
print("entries:", len(entries))
for idx, size, valid, job in entries:
    o = job - jobs_ea
    segs = []
    if 0 <= o < len(jobs) - 0x300:
        words = struct.unpack_from(">192I", jobs, o)
        segs = [w for w in words if 0x61000000 <= w < 0x61400000][:3]
    note = ""
    if find is not None and job <= find < job + size * 8:
        note = "  <== holds %08X" % find
    if find is None or note:
        print("%3d (0x%02X) size=%04X job=%08X segs=%s%s" % (idx, idx, size, job, " ".join("%08X" % s for s in segs), note))
