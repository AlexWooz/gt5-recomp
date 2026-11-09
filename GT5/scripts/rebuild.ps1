# Rebuild gt5.exe, optionally re-lifting first: rebuild.ps1 [-Lift] [-Jobs 10]
param([switch]$Lift, [int]$Jobs = 10)
$root = "$(Split-Path $PSScriptRoot -Parent)"
$ErrorActionPreference = "Continue"
if ($Lift) {
    $t = Get-Date
    & "$root\scripts\relift.ps1" *> "$root\analysis\relift.log"
    "relift: $([int]((Get-Date) - $t).TotalSeconds) s"
    Select-String -Path "$root\analysis\relift.log" -Pattern "failed|Traceback" | Select-Object -First 10 | ForEach-Object { $_.Line }
}
$t = Get-Date
$env:GT5_JOBS = "$Jobs"
cmd /c "$root\scripts\build.cmd gt5 > $root\analysis\build_gt5.log 2>&1"
$built = $LASTEXITCODE
"build exit $built in $([int]((Get-Date) - $t).TotalSeconds) s"
Select-String -Path "$root\analysis\build_gt5.log" -Pattern "error:|FAILED|LNK\d+" | Select-Object -First 15 | ForEach-Object { $_.Line }
# A freshly linked exe can stall on first launch, so run it once now: silent, behind other windows.
if ($built -eq 0) {
    $env:PS3_MUTE = "1"; $env:PS3_WINDOW_BEHIND = "1"
    $p = Start-Process "$root\build\gt5.exe" -ArgumentList "--help" -PassThru -NoNewWindow -RedirectStandardOutput "$root\logs\h.out" -RedirectStandardError "$root\logs\h.err"
    Start-Sleep -Seconds 4
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -Confirm:$false }
}
