# Short muted run with audioprobe.py alongside: audioprobe.ps1 -Tag ap -Blocks 1 [-Wait 10] [-Sec 30] [-Seconds 55]
param([string]$Tag = "ap", [string]$Blocks = "1", [int]$Wait = 10, [int]$Sec = 30, [int]$Seconds = 55)
$root = "$(Split-Path $PSScriptRoot -Parent)"
$env:AUDIO_EVEN_BLOCKS = $Blocks; $env:AUDIO_RATE = "1"
$a = "-NoProfile -ExecutionPolicy Bypass -File `"$root\scripts\racerun.ps1`" -Tag $Tag -Seconds $Seconds -EndAfter 600"
Start-Process powershell -WindowStyle Hidden -ArgumentList $a | Out-Null
python "$root\scripts\audioprobe.py" --wait $Wait --sec $Sec --out "$root\logs\$Tag.probe" *> "$root\logs\$Tag.probelog"
"done"
