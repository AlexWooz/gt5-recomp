# Short muted run, dumping the sound job lists: audiojob.ps1 -Tag aj -Blocks 1 [-At 38] [-Seconds 62]
param([string]$Tag = "aj", [string]$Blocks = "1", [int]$At = 38, [int]$Seconds = 62)
$root = "$(Split-Path $PSScriptRoot -Parent)"
$env:AUDIO_EVEN_BLOCKS = $Blocks; $env:AUDIO_RATE = "1"
$a = "-NoProfile -ExecutionPolicy Bypass -File `"$root\scripts\racerun.ps1`" -Tag $Tag -Seconds $Seconds -EndAfter 600"
Start-Process powershell -WindowStyle Hidden -ArgumentList $a | Out-Null
Start-Sleep $At
$o = "$root\logs\$Tag.job"
Remove-Item $o -ErrorAction SilentlyContinue
foreach ($i in 1..3) {
  "--- snap $i" | Add-Content $o -Encoding ascii
  python "$root\scripts\gmem.py" "dump 1933410,128" "dump 6FFA0000,64" "dump 6FF9E000,64" "dump 60040000,1024" "dump 60044280,32" "dump 60046000,1024" "dump 6004A280,32" "dump 58000000,64" *>> $o
}
"done"
