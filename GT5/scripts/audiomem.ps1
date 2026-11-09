# Short muted run, dumping the sound library's state: audiomem.ps1 -Tag am -At 35 -Seconds 62 -Blocks 1
param([string]$Tag = "am", [int]$At = 35, [int]$Seconds = 62, [string]$Blocks = "1")
$root = "$(Split-Path $PSScriptRoot -Parent)"
$env:AUDIO_EVEN_BLOCKS = $Blocks; $env:AUDIO_RATE = "1"
$a = "-NoProfile -ExecutionPolicy Bypass -File `"$root\scripts\racerun.ps1`" -Tag $Tag -Seconds $Seconds -EndAfter 600"
Start-Process powershell -WindowStyle Hidden -ArgumentList $a | Out-Null
Start-Sleep $At
python "$root\scripts\gmem.py" "g=*186E990" "s=*186E860" "root=*s+A8" "dump g+0,512" "dump s+0,256" "dump root+0,128" "job=*g+70" "dump job+4280,16" "dump g+1F0,16" "dump g+2200,16" *> "$root\logs\$Tag.mem"
"done"
