# Short muted run that looks at the audio threads while it plays: audiolive.ps1 -Tag al [-At 35] [-Seconds 60] [-Env @{}]
param([string]$Tag = "al", [int]$At = 35, [int]$Seconds = 60, [string]$Mem = "", [string]$EnvText = "")
$root = "$(Split-Path $PSScriptRoot -Parent)"
$a = "-NoProfile -ExecutionPolicy Bypass -File `"$root\scripts\racerun.ps1`" -Tag $Tag -Seconds $Seconds -EndAfter 600"
Start-Process powershell -WindowStyle Hidden -ArgumentList $a | Out-Null
Start-Sleep $At
$out = "$root\logs\$Tag.live"
python "$root\scripts\gstack.py" --tid 18,28 --depth 8 *> $out
python "$root\scripts\gsample.py" --tid 18,28 --n 800 --ms 3 --depth 6 *>> $out
Get-Content $out | ForEach-Object { $_.ToString().Substring(0, [Math]::Min(140, $_.ToString().Length)) }
