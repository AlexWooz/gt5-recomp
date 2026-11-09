#!/usr/bin/env python3
"""Lift every captured raw SPU image and regenerate src/gen/spu_jobs.c.

GT5's job binaries and its Wws_Job policy module are raw code+data blobs, not the
ELFs build_spu_workloads.py discovers. Capture them first, then lift and rebuild:

    run.ps1 -Env @{SPU_DUMP_MISS="...\\analysis\\spu_miss"}
    python scripts/lift_spu_jobs.py

The dump name carries the fingerprint the runtime computed, which is the value to
register. Run again after each round: a job only dumps once it gets dispatched,
and earlier jobs have to work for later ones to be reached.

The Wws_Job policy module streams a second family of jobs into LS 0x4000 from the
EBOOT's own data. Those are found by scanning the EBOOT (wws_jobs below) and registered
as overlays, so they run compiled instead of through the interpreter.
"""
import concurrent.futures
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT.parent / "ps3recomp" / "tools"
DUMPS = ROOT / "analysis" / "spu_miss"
OUT = ROOT / "spu" / "jobs"
REG = ROOT / "src" / "gen" / "spu_jobs.c"

# Policy modules run at LS 0xA00 (spurs_policy.c); jobs load at LS 0 (spurs_job.c).
PM_BASE = {"75BFE68AE96D7577": 0xA00}  # Wws_Job, embedded in the EBOOT at 0x0177F200

# A Wws job: 16 bytes of `ila $r2` fingerprint, then {entry, text, data, bss}, then
# {0xC0DEC0DE, ?, link base, 5}. The DMA covers text - 0x20 bytes when there's no data.
EBOOT = ROOT / "input" / "EBOOT.elf"
WWS_DIR = ROOT / "analysis" / "spu_wws"
WWS_BASE = 0x4000
WWS_IMAGE0 = 100
WWS_SAMPLE = 64  # code bytes checked at first dispatch; pins the overlay to its load address

NAME_RE = re.compile(r"^spujob_([0-9A-Fa-f]{16})_(\d+)\.bin$")


def run(*args):
    r = subprocess.run([sys.executable, *map(str, args)], capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        print(r.stdout[-2000:], r.stderr[-2000:], sep="\n", file=sys.stderr)
        raise SystemExit(f"failed: {' '.join(map(str, args))}")
    return r.stdout


def lift(blob, fp):
    base = PM_BASE.get(fp, 0)
    outdir = OUT / f"j{fp[:8]}"
    outdir.mkdir(parents=True, exist_ok=True)
    funcs = OUT / f"j{fp[:8]}_funcs.json"
    out = run(TOOLS / "find_spu_functions.py", "--raw", "--base", hex(base), "--out", funcs, blob)
    m = re.search(r"Detected ([\d,]+) function", out)
    run(TOOLS / "spu_lifter.py", blob, "--base", hex(base), "--functions", funcs,
        "--symbol-prefix", f"gt5_j{fp[:8]}", "--output", outdir, "--source-name", "spu_recomp.c")
    return int(m.group(1).replace(",", "")) if m else 0


def wws_jobs():
    """Every Wws job in the EBOOT, as (ea, entry offset, code size, blob)."""
    d = EBOOT.read_bytes()
    phoff = struct.unpack(">Q", d[0x20:0x28])[0]
    phnum = struct.unpack(">H", d[0x38:0x3A])[0]
    jobs = []
    for i in range(phnum):
        kind, _, off, va, _, size, _, _ = struct.unpack(">IIQQQQQQ", d[phoff + i * 56:phoff + i * 56 + 56])
        if kind != 1:
            continue
        for o in range(off, off + size - 0x40, 16):
            w = struct.unpack(">12I", d[o:o + 0x30])
            if w[8] == 0xC0DEC0DE and all((x >> 25) == 0x21 and (x & 0x7F) == 2 for x in w[:4]):
                code = w[5] - 0x20
                jobs.append((va + o - off, w[4], code, d[o:o + code]))
    return jobs


def lift_wws(job):
    ea, entry, code, blob = job
    name = f"w{ea:08X}"
    WWS_DIR.mkdir(parents=True, exist_ok=True)
    path = WWS_DIR / f"{name}.bin"
    path.write_bytes(blob)
    outdir = OUT / name
    outdir.mkdir(parents=True, exist_ok=True)
    funcs = OUT / f"{name}_funcs.json"
    out = run(TOOLS / "find_spu_functions.py", "--raw", "--base", hex(WWS_BASE), "--entry", hex(WWS_BASE + entry),
              "--out", funcs, path)
    m = re.search(r"Detected ([\d,]+) function", out)
    run(TOOLS / "spu_lifter.py", path, "--base", hex(WWS_BASE), "--functions", funcs, "--code-end", hex(WWS_BASE + code),
        "--return-entries", "--merge-chunks", "--symbol-prefix", f"gt5_{name}", "--output", outdir, "--source-name", "spu_recomp.c")
    return int(m.group(1).replace(",", "")) if m else 0


def main():
    dumps = sorted(p for p in DUMPS.glob("spujob_*.bin") if NAME_RE.match(p.name))
    if not dumps:
        raise SystemExit(f"no spujob_*.bin in {DUMPS} -- run with SPU_DUMP_MISS first")
    fps = [NAME_RE.match(p.name).group(1).upper() for p in dumps]
    with concurrent.futures.ThreadPoolExecutor(8) as ex:
        counts = list(ex.map(lift, dumps, fps))
    for p, n in zip(dumps, counts):
        print(f"  {p.name}: {n} functions")

    jobs = wws_jobs()
    with concurrent.futures.ThreadPoolExecutor(8) as ex:
        wcounts = list(ex.map(lift_wws, jobs))
    print(f"  {len(jobs)} Wws jobs, {sum(wcounts)} functions, {sum(j[2] for j in jobs)} bytes of code")

    externs, regs = [], []
    for img, (p, fp) in enumerate(zip(dumps, fps), start=2):
        pre = f"gt5_j{fp[:8]}"
        entry = f"{pre}_spu_func_{PM_BASE.get(fp, 0):08X}"
        externs += [f"extern void {entry}(spu_context*);", f"extern void {pre}_spu_recomp_register(void);"]
        regs += [f"    /* {p.name} */",
                 f"    spu_begin_image({img});",
                 f"    {pre}_spu_recomp_register();",
                 f'    spu_workload_register_img(0x{fp}ULL, {entry}, {img}, "j{fp[:8]}");']
    for img, (ea, entry, code, blob) in enumerate(jobs, start=WWS_IMAGE0):
        pre = f"gt5_w{ea:08X}"
        sample = blob[0x30:0x30 + WWS_SAMPLE]
        externs += [f"extern void {pre}_spu_recomp_register(void);",
                    f"static const uint8_t {pre}_sample[{len(sample)}] = {{{', '.join(str(b) for b in sample)}}};"]
        regs += [f"    spu_begin_image({img});",
                 f"    {pre}_spu_recomp_register();",
                 f"    {{ spu_overlay_code_segment s = {{0x{WWS_BASE + 0x30:X}, {len(sample)}, {pre}_sample}};",
                 f"      spu_overlay_register_verified_region(0x{ea:08X}u, 0x{WWS_BASE:X}u, 0x{code:X}u, {img}, &s, 1); }}"]
    REG.parent.mkdir(parents=True, exist_ok=True)
    REG.write_text(TEMPLATE.format(externs="\n".join(externs), regs="\n".join(regs), n=len(dumps)), encoding="utf-8")
    print(f"{len(dumps)} images, {sum(counts)} SPU functions -> {REG.relative_to(ROOT)}")


TEMPLATE = """/* Raw SPU images (SPURS jobs + the Wws_Job policy module), GENERATED by
 * scripts/lift_spu_jobs.py. The runtime matches an image by content fingerprint;
 * without a match it logs `dispatch MISS`, and that job never runs. */
#include <stdint.h>
#include "spu_workload.h"
#include "spu_overlay_residency.h"

extern void spu_begin_image(int image_id);

{externs}

/* {n} images. Image ids 2.. leave room for build_spu_workloads.py's hashed ids. */
__attribute__((constructor)) static void gt5_spu_jobs_register(void)
{{
{regs}
    spu_begin_image(0);
}}
"""

if __name__ == "__main__":
    main()
