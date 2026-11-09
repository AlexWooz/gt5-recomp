"""Draws per frame from a RSX_DRAW_CSV log, runs of equal counts collapsed: drawcounts.py <csv>"""
import collections
import csv
import sys

n = collections.Counter()
colour = collections.Counter()
for r in csv.DictReader(open(sys.argv[1])):
    f = int(r["frame"])
    n[f] += 1
    if r["cmask"] != "0x00000000":
        colour[f] += 1
last = None
start = None
for f in range(min(n), max(n) + 2):
    key = (n[f], colour[f])
    if key != last:
        if last is not None:
            print(f"frames {start}-{f - 1}: draws={last[0]} colour={last[1]}")
        start, last = f, key
