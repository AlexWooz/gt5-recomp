# Short muted run, saving the sound command area several times: audiocmd.ps1 -Tag ac -Blocks 1 [-At 45] [-Seconds 70]
param([string]$Tag = "cm", [string]$Blocks = "1", [int]$At = 45, [int]$Seconds = 70)
$root = "$(Split-Path $PSScriptRoot -Parent)"
$env:AUDIO_EVEN_BLOCKS = $Blocks; $env:AUDIO_RATE = "1"
$a = "-NoProfile -ExecutionPolicy Bypass -File `"$root\scripts\racerun.ps1`" -Tag $Tag -Seconds $Seconds -EndAfter 600"
Start-Process powershell -WindowStyle Hidden -ArgumentList $a | Out-Null
Start-Sleep $At
$o = "$root\logs\$Tag.cmd"
Remove-Item $o -ErrorAction SilentlyContinue
foreach ($i in 1..4) { "--- snap $i" | Add-Content $o -Encoding ascii; python "$root\scripts\gmem.py" "dump 60040000,49152" *>> $o }
"done"
