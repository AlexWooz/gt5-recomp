# Start the game muted and behind other windows for driving by hand with gp.py: liveplay.ps1 -Tag lp -Seconds 1800
# Frames land in analysis\frames_<Tag>; only the newest few .ppm dumps are kept. Saves go to the normal hdd0.
param([string]$Tag = "lp", [int]$Seconds = 1800, [hashtable]$Env = @{})
$root = "$(Split-Path $PSScriptRoot -Parent)"
$frames = "$env:LOCALAPPDATA\Temp\gt5_frames_$Tag"   # C:, because a 6.6 MB dump on the USB drive stalls the RSX thread
New-Item -ItemType Directory -Force $frames | Out-Null
Set-Content -Path "$env:LOCALAPPDATA\Temp\gt5_pad.txt" -Value "" -Encoding ascii -NoNewline
$e = @{ PAD_FILE = "$env:LOCALAPPDATA\Temp\gt5_pad.txt"; PS3_VERBOSE = "0"; LD_FRAME_DUMP = $frames; LD_FRAME_DUMP_MS = "700"; LD_GPU_WORK = "1"; WATCHDOG_AT = "" }
foreach ($k in $Env.Keys) { $e[$k] = $Env[$k] }
$keep = Start-Process python -WindowStyle Hidden -PassThru -ArgumentList "`"$root\scripts\framekeep.py`" `"$frames`" $Seconds"
& "$root\scripts\run.ps1" -Seconds $Seconds -Tag $Tag -Env $e
if (-not $keep.HasExited) { Stop-Process -Id $keep.Id -Force -Confirm:$false }
