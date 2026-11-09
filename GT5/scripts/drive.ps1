# Drive a running gt5.exe by hand: send a press through PAD_FILE, wait, show the newest frame.
#   drive.ps1 -Press 0x4000 [-Polls 5] [-Wait 6] [-Dir analysis\frames_live]
# Masks: Cross 0x4000, Circle 0x2000, Square 0x8000, Triangle 0x1000, Start 0x0008,
#        Up 0x0010, Right 0x0020, Down 0x0040, Left 0x0080.
param([string]$Press = "", [int]$Polls = 5, [int]$Wait = 6, [string]$Dir = "analysis\frames_live")
$root = "$(Split-Path $PSScriptRoot -Parent)"
$py = "$(Split-Path (Split-Path $PSScriptRoot -Parent) -Parent)\venv\Scripts\python.exe"
if ($Press) { Set-Content -Path "$env:LOCALAPPDATA\Temp\gt5_pad.txt" -Value "$Press $Polls" -Encoding ascii -NoNewline }
if ($Wait -gt 0) { Start-Sleep -Seconds $Wait }
if (-not (Get-Process gt5 -ErrorAction SilentlyContinue)) { "gt5 is not running" }
& $py -I "$root\scripts\ppm2png.py" "$root\$Dir" 2>&1 | Out-Null
$n = Get-ChildItem "$root\$Dir\*.png" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
if ($n) { "newest: $($n.FullName)  $([int]($n.Length / 1024))K  $($n.LastWriteTime.ToString('HH:mm:ss'))" }
Select-String -Path "$root\logs\live.err" -Pattern "^\[fps\]" -ErrorAction SilentlyContinue | Select-Object -Last 1 | ForEach-Object { $_.Line }
Select-String -Path "$root\logs\live.err" -Pattern "^\[throw\]|^\[exit-chain\] code|identical transfers|device removed" -ErrorAction SilentlyContinue | Select-Object -First 3 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(110, $_.Line.Length)) }
