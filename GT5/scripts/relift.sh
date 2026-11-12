#!/usr/bin/env bash
# Linux version of relift.ps1: one command for a full lift (functions, PPU C++, setjmp patch, NID
# table, SPU images), made from your own GT5/input/EBOOT.elf. setup.sh runs it when the game code is
# missing or out of date; GT5/generated, GT5/spu and GT5/src/gen are rebuilt wholesale.
#   bash GT5/scripts/relift.sh
set -u
GT5="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BASE="$(dirname "$GT5")"
TK="$BASE/ps3recomp/tools"
PY="$BASE/venv/bin/python"
export PYTHONIOENCODING=utf-8
cd "$GT5" || exit 1

run() {
    local label="$1"; shift
    echo "== $label"
    "$PY" "$@" || { echo "relift: $label failed ($?)" >&2; exit 1; }
}

missing=()
[ -x "$PY" ] || missing+=("the Python environment (venv/bin/python): setup.sh makes it")
[ -f "$TK/ppu_lifter.py" ] || missing+=("ps3recomp with the patch (ps3recomp/tools): setup.sh fetches it")
[ -f "input/EBOOT.elf" ] || missing+=("GT5/input/EBOOT.elf, your decrypted game executable (README, Linux)")
if [ ${#missing[@]} -gt 0 ]; then
    echo "relift.sh can't start. Missing:" >&2
    for m in "${missing[@]}"; do echo "  - $m" >&2; done
    echo "Run setup.sh from the gt5-recomp folder: it lists everything still missing, and once it's all" >&2
    echo "in place it runs this lift and the build for you." >&2
    exit 1
fi
mkdir -p analysis

# The addresses that differ between GT5 releases (code end, setjmp/longjmp, a computed goto).
echo "== version_addrs"
"$PY" scripts/version_addrs.py input/EBOOT.elf || echo "relift: version_addrs.py did not find everything; see its NOT FOUND lines" >&2
if [ ! -f analysis/version.json ]; then
    echo "version_addrs.py couldn't read GT5/input/EBOOT.elf (see the message above). Is it the decrypted file?" >&2
    exit 1
fi
CODE_END="$("$PY" -c 'import json,sys; print(json.load(open("analysis/version.json"))["code_end"])')"

run ppu_loader     "$TK/ppu_loader.py" input/EBOOT.elf -o analysis/
run find_functions "$TK/find_functions.py" input/EBOOT.elf --seed-json analysis/EBOOT.functions.json --output analysis/functions.json
run filter         scripts/filter_functions.py input/EBOOT.elf analysis/functions.json analysis/functions_code.json

# Game-code mods in GT5/mods (MODDING.md): with no mod for this EBOOT, mods.py writes nothing.
run mods           scripts/mods.py
JOBS="$(nproc 2>/dev/null || echo 4)"; [ "$JOBS" -gt 16 ] && JOBS=16
lift=("$TK/ppu_lifter.py" input/EBOOT.elf --functions analysis/functions_code.json
      --hle-stubs analysis/EBOOT.imports.json --code-end "$CODE_END"
      --label-jumps analysis/label_jumps.json --jobs "$JOBS" --chunks 48 --output generated)
[ -f analysis/insn_patches.json ] && lift+=(--insn-patches analysis/insn_patches.json)
run ppu_lifter     "${lift[@]}"
run setjmp_patch   scripts/setjmp_patch.py

# The SPU code: the SPU programs and the SPURS jobs this copy's EBOOT carries.
run spu_programs   "$TK/build_spu_workloads.py" --input input/EBOOT.elf --lifted spu \
                   --out spu/spu_workloads.c --constructor --allow-partial
run spu_jobs_find  scripts/spu_jobs_from_eboot.py
if compgen -G "analysis/spu_miss/spujob_*.bin" > /dev/null; then
    run spu_jobs   scripts/lift_spu_jobs.py
fi

mkdir -p src/gen
run gen_hle_nids   "$TK/gen_hle_nids.py" --all --out src/gen/ppu_hle_nids.cpp
# Record what this lift was made from, so a later setup.sh re-lifts only when that changes.
run lift_stamp     scripts/lift_stamp.py --write
