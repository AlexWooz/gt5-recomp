# Run gt5.exe for a fixed time and keep its output. -Window opens the D3D12 window;
# otherwise it runs headless. -Env sets extra variables, e.g. -Env @{PS3_ARGV="boot_from=x"}.
param([int]$Seconds = 60, [switch]$Window, [hashtable]$Env = @{}, [string]$Tag = "run")
$root = "$(Split-Path $PSScriptRoot -Parent)"
New-Item -ItemType Directory -Force "$root\hdd0", "$root\hdd1", "$root\logs" | Out-Null
$env:PS3_VFS_ROOT = "$root\vfs"
# Test runs use their own copy of the saves and caches, so the real save (play.bat) stays untouched.
$env:PS3_HDD0_ROOT = "$root\test_save\hdd0"
$env:PS3_GAMEDATA_ROOT = "$root\test_save\gamedata"
$env:PS3_HDD1_ROOT = "$root\test_save\hdd1"
$env:PS3_DEV_FLASH = "$(Split-Path $PSScriptRoot -Parent)\dev_flash"
$env:RSX_LIVE_DRAW = "1"
$env:PS3_TITLE = "Gran Turismo 5"
# GT5's SPU jobs fill self-JUMP holes in the FIFO, slowly during loads. Real RSX just waits.
$env:GCM_PARK_GRACE_MS = "600000"
$env:SPU_DMA_REPEAT_SECS = "100000"
$env:SPURS_EXTRA_SPUS = "2"
# Test runs stay silent on the speakers; audiolevel.py checks what would have played.
$env:PS3_MUTE = "1"
$env:AUDIO_WAV = "$root\logs\audio_last.f32"
if (-not $Window) { $env:PS3_HEADLESS = "1"; $env:PS3_WINDOW_BEHIND = "1" }
foreach ($k in $Env.Keys) { Set-Item "env:$k" $Env[$k] }
$out = "$root\logs\$Tag.out"; $err = "$root\logs\$Tag.err"
$t0 = Get-Date
$p = Start-Process "$root\build\gt5.exe" -ArgumentList "`"$root\input\EBOOT.elf`"" -WorkingDirectory $root `
    -PassThru -NoNewWindow -RedirectStandardOutput $out -RedirectStandardError $err
$h = $p.Handle  # keeps ExitCode readable after exit
$peak = 0
while (-not $p.HasExited -and ((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
    try { $p.Refresh(); if ($p.PeakWorkingSet64 -gt $peak) { $peak = $p.PeakWorkingSet64 } } catch {}
    Start-Sleep -Milliseconds 500
}
$alive = -not $p.HasExited
if ($alive) { Stop-Process -Id $p.Id -Force -Confirm:$false; "still running after $Seconds s, stopped it" }
else { "exited by itself after $([int]((Get-Date) - $t0).TotalSeconds) s, code $($p.ExitCode) (0x$('{0:X8}' -f $p.ExitCode))" }
"peak RAM $([math]::Round($peak / 1GB, 2)) GB"
"stdout $([math]::Round((Get-Item $out).Length / 1MB, 2)) MB, stderr $([math]::Round((Get-Item $err).Length / 1MB, 2)) MB"
