# A race test with a CPU profile of its middle: raceprof.ps1 -Tag x [-Env @{...}] [-ProfSeconds 25]
# Starts racerun.ps1 hidden (muted, behind windows), waits until screendrive says "accelerate", lets the race
# settle, samples every thread with prof.ps1 into logs\prof_<Tag>.txt, then waits for the run to end.
param([string]$Tag = "rp", [hashtable]$Env = @{}, [int]$ProfSeconds = 25, [int]$Settle = 20)
$root = "$(Split-Path $PSScriptRoot -Parent)"
Remove-Item "$root\logs\$Tag.drive" -ErrorAction SilentlyContinue
# Screenshots stop once the race is reached, so the measured frame rate is the game's own.
$stop = "$root\logs\$Tag.nodump"
Remove-Item $stop -ErrorAction SilentlyContinue
$Env["LD_FRAME_DUMP_STOP"] = $stop
$pairs = ($Env.GetEnumerator() | ForEach-Object { "$($_.Key)='$($_.Value)'" }) -join ";"
$cmd = "& '$root\scripts\racerun.ps1' -Tag $Tag -Seconds 640 -EndAfter $($Settle + $ProfSeconds + 30) -Env @{$pairs}"
$run = Start-Process powershell -WindowStyle Hidden -PassThru -ArgumentList "-NoProfile -ExecutionPolicy Bypass -Command `"$cmd`""
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt 560) {
    Start-Sleep 3
    $d = Get-Content "$root\logs\$Tag.drive" -ErrorAction SilentlyContinue
    if ($d -match "accelerate") { break }
    if ($run.HasExited) { break }
}
if (-not ($d -match "accelerate")) { "no race reached"; return }
Set-Content -Path $stop -Value "race" -Encoding ascii
Start-Sleep $Settle
# -ProfSeconds 0: no sampling (it suspends threads and costs frames), just a clean measurement.
if ($ProfSeconds -gt 0) { & "$root\scripts\prof.ps1" -Seconds $ProfSeconds -Hz 60 -Out "$root\logs\prof_$Tag.txt" | Out-Null }
$run.WaitForExit(240000) | Out-Null
"race at $([int]((Get-Date) - $t0).TotalSeconds) s, profile in logs\prof_$Tag.txt"
