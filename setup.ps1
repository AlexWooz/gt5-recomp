# One-time setup, run from the folder this repository was cloned to:
#   powershell -ExecutionPolicy Bypass -File setup.ps1
# Fetches ps3recomp at the commit GT5\patches\ps3recomp.patch was made against and applies the patch,
# creates the Python environment for the lift tools, and downloads the FFmpeg LGPL build the game's
# movie player loads at run time. Safe to run again.
# Errors are checked through exit codes: with "Stop", Windows PowerShell turns a native command's
# redirected stderr into a terminating error before the check can run.
$ErrorActionPreference = "Continue"
$base = $PSScriptRoot
$commit = "4e8f3ca87a3e1d87bb83b8b7e9348fe2685cd514"
$patch = Join-Path $base "GT5\patches\ps3recomp.patch"

# 1. ps3recomp, patched
$rc = Join-Path $base "ps3recomp"
if (-not (Test-Path (Join-Path $rc ".git"))) {
    # No line-ending conversion: the patch matches the files as stored upstream.
    git clone --config core.autocrlf=false https://github.com/sp00nznet/ps3recomp.git $rc
    if ($LASTEXITCODE -ne 0) { throw "git clone failed" }
    git -C $rc checkout --quiet $commit
    if ($LASTEXITCODE -ne 0) { throw "checkout of $commit failed" }
}
git -C $rc apply --check $patch 2>$null
if ($LASTEXITCODE -eq 0) {
    git -C $rc apply $patch
    Write-Output "ps3recomp patched"
} else {
    git -C $rc apply --reverse --check $patch 2>$null
    if ($LASTEXITCODE -eq 0) { Write-Output "ps3recomp already patched" }
    else { throw "the patch does not apply: ps3recomp must be at $commit with no other changes" }
}

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
