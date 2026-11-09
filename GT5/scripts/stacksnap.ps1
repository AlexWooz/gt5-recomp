# Short muted run with snapshots of every guest thread's stack: stacksnap.ps1 -Tag ss -At 35 -Seconds 60 -Blocks 1
param([string]$Tag = "ss", [int]$At = 35, [int]$Seconds = 60, [string]$Blocks = "1")
$root = "$(Split-Path $PSScriptRoot -Parent)"
$env:AUDIO_EVEN_BLOCKS = $Blocks; $env:AUDIO_RATE = "1"
$a = "-NoProfile -ExecutionPolicy Bypass -File `"$root\scripts\racerun.ps1`" -Tag $Tag -Seconds $Seconds -EndAfter 600"
Start-Process powershell -WindowStyle Hidden -ArgumentList $a | Out-Null
Start-Sleep $At
$out = "$root\logs\$Tag.stacks"
Remove-Item $out -ErrorAction SilentlyContinue
foreach ($i in 1..3) { "--- snap $i" | Add-Content $out -Encoding ascii; python "$root\scripts\gstack.py" --depth 10 *>> $out; Start-Sleep 2 }
"done"
