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
# The addresses that differ between GT5 releases (code end, setjmp/longjmp, a computed goto),
# found in this copy's EBOOT by version_addrs.py. Europe 01.00: 0x111c684, 00D00D74/00D00E98.
Write-Output "== version_addrs"
& $py "scripts\version_addrs.py" "input\EBOOT.elf"
if ($LASTEXITCODE -ne 0) { Write-Warning "version_addrs.py did not find everything; see its NOT FOUND lines" }
$ver = Get-Content "analysis\version.json" -Raw | ConvertFrom-Json
$codeEnd = $ver.code_end
Run "ppu_loader"     @("$tk\ppu_loader.py", "input\EBOOT.elf", "-o", "analysis\")
Run "find_functions" @("$tk\find_functions.py", "input\EBOOT.elf", "--seed-json", "analysis\EBOOT.functions.json", "--output", "analysis\functions.json")
Run "filter"         @("scripts\filter_functions.py", "input\EBOOT.elf", "analysis\functions.json", "analysis\functions_code.json")

Run "ppu_lifter"     @("$tk\ppu_lifter.py", "input\EBOOT.elf", "--functions", "analysis\functions_code.json",
                       "--hle-stubs", "analysis\EBOOT.imports.json", "--code-end", $codeEnd,
                       "--label-jumps", "analysis\label_jumps.json",
                       "--jobs", "16", "--chunks", "48", "--output", "generated")
Run "setjmp_patch"   @("scripts\setjmp_patch.py")

New-Item -ItemType Directory -Force src\gen | Out-Null
Run "gen_hle_nids"   @("$tk\gen_hle_nids.py", "--all", "--out", "src\gen\ppu_hle_nids.cpp")
