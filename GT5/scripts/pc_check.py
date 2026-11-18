"""Does every processor of this PC give the same answer?

The port is built on the player's PC: Python tools turn the game's code into C++, then a compiler turns
that into gt5.exe. Both are long calculations, and both assume the processor calculates correctly. Some
do not: a processor (or one of its cores) that has become unstable gives a wrong answer now and then,
which shows up as a tool stopping with an error that makes no sense, a compiler crash, or a game that
misbehaves. Seen on Intel's 13th and 14th generation desktop processors in particular.

This check runs one of the lift's own jobs (a deterministic program: same input, same output, always)
many times, each run pinned to one logical processor, a few processors at a time (the pattern in which
the faults were seen: runs alone passed, eight side by side failed). A run that stops with an error or
writes a different result is a wrong calculation, and the report says which processors gave them.

    python pc_check.py [--rounds 6] [--together 8] [--cpus 0,1,2] [--json FILE]

Exit code 0: every run agreed. 1: some did not (the table says where). 2: the check could not run.
Standard library only; Windows and Linux. Nothing outside the temporary folder is written.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent

# The child: pin itself to one logical processor, then run the job in this same interpreter.
CHILD = r"""
import os, sys, runpy
cpu = int(sys.argv[1])
if os.name == "nt":
    import ctypes
    k = ctypes.windll.kernel32
    k.GetCurrentProcess.restype = ctypes.c_void_p
    k.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    if not k.SetProcessAffinityMask(k.GetCurrentProcess(), 1 << cpu):
        sys.exit(97)
else:
    os.sched_setaffinity(0, {cpu})
sys.argv = sys.argv[2:]
runpy.run_path(sys.argv[0], run_name="__main__")
"""

# With no lift job on disk yet (a fresh install), a stand-in of the same kind of work: a pure-Python
# loop over objects with attribute reads, comparisons, list building and hashing.
SYNTHETIC = r"""
import hashlib, sys
class Ins:
    def __init__(self, addr, op, a, b):
        self.addr = addr; self.op = op; self.a = a; self.b = b
x = 0x2545F491
ins = []
for i in range(60000):
    x = (x * 1103515245 + 12345) & 0xFFFFFFFF
    ins.append(Ins(i * 4, x >> 26, (x >> 13) & 127, x & 127))
h = hashlib.sha256()
bounds = [(s, s + 2400) for s in range(0, 240000, 2000)]
for rnd in range(6):
    for (s, e) in bounds:
        fn = [i for i in ins if s <= i.addr < e]
        acc = 0
        for idx, i in enumerate(fn):
            if i.op & 1:
                acc = (acc * 31 + i.a * i.b + idx) & 0xFFFFFFFFFFFF
            elif i.a < i.b:
                acc ^= i.addr << (i.op & 7)
        h.update(acc.to_bytes(8, "little"))
open(sys.argv[sys.argv.index("--output") + 1] + "/out.txt", "w").write(h.hexdigest())
"""


def find_job():
    """One of the lift's SPU jobs with the function list the lift made for it, or None."""
    # next to this script (GT5\scripts), or, for a copy of the script run from elsewhere, the GT5 folder
    # it was started in
    gt5 = next((g for g in (HERE.parent, Path.cwd())
                if (g.parent / "ps3recomp" / "tools" / "spu_lifter.py").is_file()), None)
    if gt5 is None:
        return None
    lifter = gt5.parent / "ps3recomp" / "tools" / "spu_lifter.py"
    best = None
    for dump in sorted((gt5 / "analysis" / "spu_miss").glob("spujob_*.bin")):
        fp = dump.name.split("_")[1][:8].upper()
        funcs = gt5 / "spu" / "jobs" / f"j{fp}_funcs.json"
        if funcs.is_file() and (best is None or dump.stat().st_size > best[0].stat().st_size):
            best = (dump, funcs, fp)
    if not best:
        return None
    dump, funcs, fp = best
    return [str(lifter), str(dump), "--base", "0x0", "--functions", str(funcs),
            "--symbol-prefix", f"gt5_j{fp}", "--source-name", "spu_recomp.c"], dump.name


def digest(folder):
    h = hashlib.sha256()
    for p in sorted(Path(folder).rglob("*")):
        if p.is_file():
            h.update(p.name.encode()); h.update(p.read_bytes())
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--rounds", type=int, default=6, help="runs on each processor (default 6)")
    ap.add_argument("--together", type=int, default=8, help="processors busy at the same time (default 8)")
    ap.add_argument("--cpus", default="", help="only these logical processors, e.g. 0,1,2")
    ap.add_argument("--json", default="", help="also write the result here")
    ap.add_argument("--synthetic", action="store_true", help="use the stand-in job even when a lift job is there")
    a = ap.parse_args()

    n = os.cpu_count() or 1
    if n > 64 and os.name == "nt":
        n = 64          # one processor group; enough to say whether this PC calculates correctly
    cpus = [int(c) for c in a.cpus.split(",") if c.strip()] if a.cpus else list(range(n))
    job = None if a.synthetic else find_job()
    tmp = Path(tempfile.mkdtemp(prefix="gt5_pc_check_"))
    child = tmp / "child.py"; child.write_text(CHILD)
    if job:
        argv, what = job
    else:
        syn = tmp / "job.py"; syn.write_text(SYNTHETIC)
        argv, what = [str(syn)], "a stand-in job (no lift job on disk yet)"
    name = ""
    try:
        if os.name == "nt":
            name = subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-CimInstance Win32_Processor).Name"],
                                  capture_output=True, text=True, timeout=30).stdout.strip()
        else:
            name = next((l.split(":", 1)[1].strip() for l in open("/proc/cpuinfo") if l.startswith("model name")), "")
    except Exception:
        pass
    print(f"Processor: {name or 'unknown'}, {n} logical processors; Python {sys.version.split()[0]}")
    print(f"Job: {what}; {a.rounds} runs on each of {len(cpus)} processors, {a.together} at a time")

    runs = {c: [] for c in cpus}        # cpu -> list of (exit code, digest or "", last error line)
    t0 = time.time()
    seq = 0
    for rnd in range(a.rounds):
        # a different neighbourhood each round, so a processor is tried next to different others
        order = cpus[rnd % len(cpus):] + cpus[:rnd % len(cpus)]
        for g in range(0, len(order), a.together):
            group = order[g:g + a.together]
            procs = []
            for c in group:
                seq += 1
                out = tmp / f"o{seq}"; out.mkdir()
                p = subprocess.Popen([sys.executable, "-I", str(child), str(c), *argv, "--output", str(out)],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
                procs.append((c, p, out))
            for c, p, out in procs:
                try:
                    _, err = p.communicate(timeout=600)
                except subprocess.TimeoutExpired:
                    p.kill(); _, err = p.communicate()
                last = next((l for l in reversed((err or "").splitlines()) if l.strip()), "")
                runs[c].append((p.returncode, digest(out) if p.returncode == 0 else "", last.strip()[:160]))
                shutil.rmtree(out, ignore_errors=True)
        print(f"  round {rnd + 1} of {a.rounds} done ({time.time() - t0:.0f} s)", flush=True)
    shutil.rmtree(tmp, ignore_errors=True)

    if all(code == 97 for r in runs.values() for code, _, _ in r):
        print("The check could not pin its runs to processors on this system.")
        return 2
    counts = {}
    for r in runs.values():
        for code, d, _ in r:
            if code == 0:
                counts[d] = counts.get(d, 0) + 1
    if not counts:
        print("No run finished. First error: " + next((e for r in runs.values() for _, _, e in r if e), "(none)"))
        return 1
    good = max(counts, key=counts.get)
    bad = {}
    for c, r in runs.items():
        errs = [e or f"exit code {code}" for code, d, e in r if code != 0]
        diff = sum(1 for code, d, _ in r if code == 0 and d != good)
        if errs or diff:
            bad[c] = (len(errs), diff, errs[0] if errs else "a different result, with no error")
    total = sum(len(r) for r in runs.values())
    print()
    if not bad:
        print(f"RESULT: all {total} runs gave the same answer. No wrong calculation was seen.")
    else:
        wrong = sum(e + d for e, d, _ in bad.values())
        print(f"RESULT: {wrong} of {total} runs went wrong, on {len(bad)} of {len(cpus)} logical processors:")
        for c in sorted(bad):
            e, d, first = bad[c]
            print(f"  processor {c:2d}: {e} stopped with an error, {d} wrote a different result   e.g. {first}")
        print("The same program with the same input must always give the same answer, so this is the")
        print("processor (or memory) calculating wrongly, not the game's tools.")
    if a.json:
        json.dump({"processor": name, "logical": n, "job": what, "rounds": a.rounds, "together": a.together,
                   "total": total, "bad": {str(c): list(v) for c, v in bad.items()}}, open(a.json, "w"), indent=1)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
