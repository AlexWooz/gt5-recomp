# One full test: run gt5.exe, follow its screen to a race with screendrive.py, sample it with prof.ps1.
#   racerun.ps1 -Tag p2 [-Seconds 900] [-EndAfter 120] [-Env @{...}]
# Frames land in %LOCALAPPDATA%\Temp\gt5_frames_<Tag>, samples in logs\prof_<Tag>_car.txt and _race.txt, the route in logs\<Tag>.drive.
# -Capture car|race|both writes logs\<Tag>.csv (RSX_DRAW_CSV) and analysis\surf_<Tag> (LD_SURF_DUMP) at that point.
param([string]$Tag = "rr", [int]$Seconds = 900, [int]$EndAfter = 120, [hashtable]$Env = @{}, [string]$Capture = "", [int]$SkipMovie = 50)
$root = "$(Split-Path $PSScriptRoot -Parent)"
$s = "$root\scripts"
Set-Content -Path "$env:LOCALAPPDATA\Temp\gt5_pad.txt" -Value "" -Encoding ascii -NoNewline
$frames = "$env:LOCALAPPDATA\Temp\gt5_frames_$Tag"   # C:, because a 6.6 MB dump on the USB drive stalls the RSX thread
$e = @{ PAD_FILE = "$env:LOCALAPPDATA\Temp\gt5_pad.txt"; PS3_VERBOSE = "0"
        LD_FRAME_DUMP = $frames; LD_FRAME_DUMP_MS = "1000"; LD_GPU_WORK = "1"; SPU_DMA_REPEAT_SECS = "100000" }
New-Item -ItemType Directory -Force $frames | Out-Null
$extra = ""
if ($Capture) {
    $gate = "$root\logs\$Tag.gate"
    Remove-Item $gate -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force "$root\analysis\surf_$Tag", "$root\analysis\ps_$Tag" | Out-Null
    $e["RSX_DRAW_CSV"] = "$root\logs\$Tag.csv"; $e["RSX_DRAW_CSV_GATE"] = $gate
    $e["LD_SURF_DUMP"] = "$root\analysis\surf_$Tag"; $e["LD_SURF_DUMP_GATE"] = $gate
    $e["LD_FRAME_BURST"] = $gate
    $e["LD_PS_DUMP"] = "$root\analysis\ps_$Tag"
    $extra = "--gate `"$gate`" --capture $Capture"
}
if ($env:FS_HIDE) { $e["FS_HIDE"] = $env:FS_HIDE }
foreach ($k in $Env.Keys) { $e[$k] = $Env[$k] }
$drive = Start-Process python -WindowStyle Hidden -PassThru -ArgumentList "`"$s\screendrive.py`" --dir `"$frames`" --pad `"$env:LOCALAPPDATA\Temp\gt5_pad.txt`" --race --log `"$root\logs\$Tag.drive`" --prof `"$root\logs\prof_$Tag`" --end-after $EndAfter --timeout $Seconds --skip-movie $SkipMovie $extra"
& "$s\run.ps1" -Seconds $Seconds -Tag $Tag -Env $e
if (-not $drive.HasExited) { Stop-Process -Id $drive.Id -Force -Confirm:$false }
Get-Content "$root\logs\$Tag.drive" -ErrorAction SilentlyContinue
