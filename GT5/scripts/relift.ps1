# One command for a full lift: functions, PPU C++, setjmp patch, NID table, SPU images.
# Run from anywhere; generated/ and spu/ are rebuilt wholesale.
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$tk   = Join-Path $root "..\ps3recomp\tools"
$py   = Join-Path $root "..\venv\Scripts\python.exe"
$env:PYTHONIOENCODING = "utf-8"
Set-Location $root

function Run($label, [string[]]$a) {
    Write-Output "== $label"
    & $py @a
    if ($LASTEXITCODE -ne 0) { throw "$label failed ($LASTEXITCODE)" }
}

$ErrorActionPreference = "Continue"
# Everything the lift needs, checked first so a missing piece gives one clear message instead of a
# cascade of errors. setup.ps1 makes the first two; the last is the player's own file.
$missing = @()
if (-not (Test-Path $py)) { $missing += "the Python environment (venv\Scripts\python.exe): setup.ps1 makes it" }
if (-not (Test-Path (Join-Path $tk "ppu_lifter.py"))) { $missing += "ps3recomp with the patch (ps3recomp\tools): setup.ps1 fetches it" }
if (-not (Test-Path "input\EBOOT.elf")) { $missing += "GT5\input\EBOOT.elf, your decrypted game executable (README, Setup)" }
if ($missing) {
    Write-Host "relift.ps1 can't start. Missing:" -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host "Run setup.ps1 from the gt5-recomp folder (not from GT5\scripts): it lists everything still missing,"
    Write-Host "and once it's all in place it runs this lift and the build for you."
    exit 1
}
New-Item -ItemType Directory -Force "analysis" | Out-Null

# The addresses that differ between GT5 releases (code end, setjmp/longjmp, a computed goto),
# found in this copy's EBOOT by version_addrs.py. Europe 01.00: 0x111c684, 00D00D74/00D00E98.
Write-Output "== version_addrs"
& $py "scripts\version_addrs.py" "input\EBOOT.elf"
if ($LASTEXITCODE -ne 0) { Write-Warning "version_addrs.py did not find everything; see its NOT FOUND lines" }
if (-not (Test-Path "analysis\version.json")) {
    Write-Host "version_addrs.py couldn't read GT5\input\EBOOT.elf (see the message above). Is it the decrypted file?" -ForegroundColor Red
    exit 1
}
$ver = Get-Content "analysis\version.json" -Raw | ConvertFrom-Json
$codeEnd = $ver.code_end
Run "ppu_loader"     @("$tk\ppu_loader.py", "input\EBOOT.elf", "-o", "analysis\")
Run "find_functions" @("$tk\find_functions.py", "input\EBOOT.elf", "--seed-json", "analysis\EBOOT.functions.json", "--output", "analysis\functions.json")
Run "filter"         @("scripts\filter_functions.py", "input\EBOOT.elf", "analysis\functions.json", "analysis\functions_code.json")

# Game-code mods in GT5\mods (see MODDING.md): their instruction patches go to the lifter, each
# behind a start-up switch. With no mod for this EBOOT, mods.py writes nothing and the lift is plain.
Run "mods"           @("scripts\mods.py")
$liftArgs = @("$tk\ppu_lifter.py", "input\EBOOT.elf", "--functions", "analysis\functions_code.json",
              "--hle-stubs", "analysis\EBOOT.imports.json", "--code-end", $codeEnd,
              "--label-jumps", "analysis\label_jumps.json",
              "--jobs", "16", "--chunks", "48", "--output", "generated")
if (Test-Path "analysis\insn_patches.json") { $liftArgs += @("--insn-patches", "analysis\insn_patches.json") }
Run "ppu_lifter"     $liftArgs
Run "setjmp_patch"   @("scripts\setjmp_patch.py")

# The SPU code (the PS3's co-processors: physics, sound, graphics set-up), compiled like the main code:
# the SPU programs and the SPURS jobs this copy's EBOOT carries. An SPU program not found runs in the
# SPU interpreter, many times slower; a SPURS job not found can't run at all, and the game stalls when
# it needs it (spu_jobs_from_eboot.py names any it misses).
# A GT5\spu from an older version may have a closed permission list: fix_spu_access.ps1 puts it right.
& (Join-Path $PSScriptRoot "fix_spu_access.ps1")
if ($LASTEXITCODE -ne 0) { exit 1 }
Run "spu_programs"   @("$tk\build_spu_workloads.py", "--input", "input\EBOOT.elf", "--lifted", "spu",
                       "--out", "spu\spu_workloads.c", "--constructor", "--allow-partial")
Run "spu_jobs_find"  @("scripts\spu_jobs_from_eboot.py")
if (Get-ChildItem "analysis\spu_miss\spujob_*.bin" -ErrorAction SilentlyContinue) {
    Run "spu_jobs"   @("scripts\lift_spu_jobs.py")
}

New-Item -ItemType Directory -Force src\gen | Out-Null
Run "gen_hle_nids"   @("$tk\gen_hle_nids.py", "--all", "--out", "src\gen\ppu_hle_nids.cpp")
# Record what this lift was made from, so update.ps1 re-lifts only when that changes.
Run "lift_stamp"     @("scripts\lift_stamp.py", "--write")
