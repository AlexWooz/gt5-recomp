# One-time setup, run from the folder this repository was cloned to:
#   powershell -ExecutionPolicy Bypass -File setup.ps1
# Fetches ps3recomp at the commit GT5\patches\ps3recomp.patch was made against and applies the patch
# (update.ps1 does that part, the same way it does for every later update), creates the Python
# environment for the lift tools, and downloads the FFmpeg LGPL build the game's movie player loads at
# run time. Safe to run again. To update later, run update.ps1 instead.
# Errors are checked through exit codes: with "Stop", Windows PowerShell turns a native command's
# redirected stderr into a terminating error before the check can run.
$ErrorActionPreference = "Continue"
$base = $PSScriptRoot
$rc = Join-Path $base "ps3recomp"

# 1. ps3recomp, patched for the version checked out here
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $base "update.ps1") -Here
if ($LASTEXITCODE -ne 0) { throw "setup stopped: see the message above" }

# 2. Python environment for the lift tools
$venv = Join-Path $base "venv"
if (-not (Test-Path (Join-Path $venv "Scripts\python.exe"))) {
    python -m venv $venv
    if ($LASTEXITCODE -ne 0) { throw "python -m venv failed (is Python 3.11+ installed?)" }
}
& (Join-Path $venv "Scripts\python.exe") -m pip install --quiet -r (Join-Path $rc "requirements.txt")
if ($LASTEXITCODE -ne 0) { throw "pip install failed" }

# 3. FFmpeg (LGPL shared build; GT5\CMakeLists.txt expects this folder name)
$ffname = "ffmpeg-n9.0-latest-win64-lgpl-shared-9.0"
$ffdir = Join-Path $base "ffmpeg"
if (-not (Test-Path (Join-Path $ffdir $ffname))) {
    New-Item -ItemType Directory -Force $ffdir | Out-Null
    $zip = Join-Path $ffdir "ffmpeg.zip"
    $url = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/$ffname.zip"
    try {
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing -ErrorAction Stop
        Expand-Archive -Path $zip -DestinationPath $ffdir -Force -ErrorAction Stop
        Remove-Item $zip
        Write-Output "FFmpeg downloaded"
    } catch {
        Write-Warning "Could not download $url. Get an FFmpeg 9.0 'win64-lgpl-shared' build and unpack it so that ffmpeg\$ffname\bin and \include exist."
    }
}

New-Item -ItemType Directory -Force (Join-Path $base "GT5\input"), (Join-Path $base "GT5\vfs") | Out-Null
Write-Output ""
Write-Output "Next: put your own game files in GT5\vfs and GT5\input and the firmware in GT5\dev_flash"
Write-Output "(see README.md), then run GT5\scripts\relift.ps1 and GT5\scripts\build.cmd gt5."
Write-Output "Later, update.ps1 updates to new versions (or switches to any other: update.ps1 -List)."
