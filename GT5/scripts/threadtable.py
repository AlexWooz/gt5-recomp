"""Replay a run's log to see who holds PPU thread slots: threadtable.py <err log>

Follows create, exit, return and join lines per thread id and prints what each id is at the end of the log.
"""
import collections
import re
import sys

state = {}
info = {}
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    m = re.search(r'sys_ppu_thread_create tid=(\d+) name="([^"]*)" entry=(\S+) arg=(\S+) stack=(\S+) prio=(\d+) flags=(\S+)', line)
    if m:
        t = int(m.group(1))
        state[t] = "running"
        info[t] = (m.group(2), m.group(6), m.group(7))
        continue
    m = re.search(r"\[THREAD (\d+)\] entry RETURNED|sys_ppu_thread_exit\(tid=(\d+)", line)
    if m:
        t = int(m.group(1) or m.group(2))
        if state.get(t) == "running":
            state[t] = "finished"
        continue
    m = re.search(r"ppu_thread_join\(tid=(\d+)\)", line)
    if m:
        t = int(m.group(1))
        state[t] = "joined"
c = collections.Counter()
for t in sorted(state):
    joinable = info.get(t, ("", "", ""))[2] not in ("0", "0x0")
    s = state[t]
    if s == "finished" and not joinable:
        s = "free"
    c[s] += 1
    if s in ("running", "finished"):
        print("%3d %-9s %-26s prio=%s flags=%s" % (t, s, info[t][0], info[t][1], info[t][2]))
print(dict(c))
