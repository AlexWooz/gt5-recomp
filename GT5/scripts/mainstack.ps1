# Short muted run, sampling the main thread: mainstack.ps1 -Tag ms -Blocks 1 [-At 45] [-Seconds 65]
param([string]$Tag = "ms", [string]$Blocks = "1", [int]$At = 45, [int]$Seconds = 65)
$root = "$(Split-Path $PSScriptRoot -Parent)"
$env:AUDIO_EVEN_BLOCKS = $Blocks; $env:AUDIO_RATE = "1"
$a = "-NoProfile -ExecutionPolicy Bypass -File `"$root\scripts\racerun.ps1`" -Tag $Tag -Seconds $Seconds -EndAfter 600"
Start-Process powershell -WindowStyle Hidden -ArgumentList $a | Out-Null
Start-Sleep $At
python "$root\scripts\gsample.py" --tid 0 --n 400 --ms 5 --depth 30 *> "$root\logs\$Tag.main"
"done"
