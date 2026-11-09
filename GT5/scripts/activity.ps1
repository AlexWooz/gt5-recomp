# Short muted run, sampling guest thread stacks: activity.ps1 -Tag ac -At 35 -Seconds 75 -Blocks 1 [-Tids 15,16,...]
param([string]$Tag = "ac", [int]$At = 35, [int]$Seconds = 75, [string]$Blocks = "1", [string]$Tids = "15,16,18,19,20,21,22,23,24,25,26,27,28,29,30")
$root = "$(Split-Path $PSScriptRoot -Parent)"
$env:AUDIO_EVEN_BLOCKS = $Blocks; $env:AUDIO_RATE = "1"
$a = "-NoProfile -ExecutionPolicy Bypass -File `"$root\scripts\racerun.ps1`" -Tag $Tag -Seconds $Seconds -EndAfter 600"
Start-Process powershell -WindowStyle Hidden -ArgumentList $a | Out-Null
Start-Sleep $At
python "$root\scripts\gsample.py" --tid $Tids --n 1200 --ms 3 --depth 5 *> "$root\logs\$Tag.activity"
"done"
