# Short muted run, logging changes to the sound library's records: audiowatch.ps1 -Tag aw -Blocks 1 [-Wait 10] [-Sec 25] [-Seconds 50]
param([string]$Tag = "aw", [string]$Blocks = "1", [int]$Wait = 10, [int]$Sec = 25, [int]$Seconds = 50)
$root = "$(Split-Path $PSScriptRoot -Parent)"
$env:AUDIO_EVEN_BLOCKS = $Blocks; $env:AUDIO_RATE = "1"
$a = "-NoProfile -ExecutionPolicy Bypass -File `"$root\scripts\racerun.ps1`" -Tag $Tag -Seconds $Seconds -EndAfter 600"
Start-Process powershell -WindowStyle Hidden -ArgumentList $a | Out-Null
python "$root\scripts\gwatch.py" --wait $Wait --sec $Sec --out "$root\logs\$Tag.watch" 1933410,200 19332A8,100 6FFB0000,1B00 6FFC5000,900 6FFAE000,900 *> "$root\logs\$Tag.watchlog"
Get-Content "$root\logs\$Tag.watchlog" | Select-Object -First 3
