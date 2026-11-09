# Whole movie with no input, muted and behind other windows: moviefull.ps1 -Tag mf -Seconds 460
param([string]$Tag = "mf", [int]$Seconds = 460, [hashtable]$Env = @{})
$root = "$(Split-Path $PSScriptRoot -Parent)"
$frames = "$root\analysis\frames_$Tag"
New-Item -ItemType Directory -Force $frames | Out-Null
Set-Content -Path "$env:LOCALAPPDATA\Temp\gt5_pad.txt" -Value "" -Encoding ascii -NoNewline
$e = @{ PAD_FILE = "$env:LOCALAPPDATA\Temp\gt5_pad.txt"; PS3_VERBOSE = "0"; LD_FRAME_DUMP = $frames; LD_FRAME_DUMP_MS = "20000"; LD_GPU_WORK = "1"; SPU_DMA_REPEAT_SECS = "100000" }
foreach ($k in $Env.Keys) { $e[$k] = $Env[$k] }
& "$root\scripts\run.ps1" -Seconds $Seconds -Tag $Tag -Env $e
