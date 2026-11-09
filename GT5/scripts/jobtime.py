"""Audio job timing from a run with SPU_ORDER set: jobtime.py <err log> [from sec] [to sec]

Per notify on the sound guard: time until the job's event (job), and from that event to the next
notify (PPU). Times are host microseconds, seconds count from the first logged line.
"""
import re
import sys

rows = []
for line in open(sys.argv[1], errors="replace"):
    m = re.search(r"\[spu-order\] t=(\d+) (notify|event)(.*)", line)
    if m:
        rows.append((int(m.group(1)), m.group(2), m.group(3)))
t0 = rows[0][0]
a = float(sys.argv[2]) if len(sys.argv) > 2 else 0
b = float(sys.argv[3]) if len(sys.argv) > 3 else 1e9
job = []
ppu = []
last_notify = None
last_event = None
per_block = []
for t, k, x in rows:
    s = (t - t0) / 1e6
    if s < a or s > b:
        continue
    if k == "notify" and "0x60040180" in x:
        if last_event is not None:
            ppu.append(t - last_event)
        last_notify = t
    elif k == "event" and "q=4" in x:
        if last_notify is not None:
            job.append(t - last_notify)
        last_event = t


def show(name, v):
    if not v:
        print(name, "none")
        return
    v = sorted(v)
    n = len(v)
    print("%-4s n=%d mean=%.0f p50=%d p90=%d p99=%d max=%d" % (name, n, sum(v) / n, v[n // 2], v[n * 9 // 10], v[n * 99 // 100], v[-1]))


show("job", job)
show("ppu", ppu)
span = b - a if b < 1e8 else (rows[-1][0] - t0) / 1e6
print("span %.1f s, jobs per second %.1f" % (span, len(job) / span))
