# Set up and build GT5 Recomp with one command. Run it from the gt5-recomp folder:
#   powershell -ExecutionPolicy Bypass -File setup.ps1
# Run it as often as you like. It does whatever is still to do: fetches and patches ps3recomp, makes
# the Python environment, downloads FFmpeg, then checks your game files and says exactly what's
# missing or wrong. When everything is in place it generates the game code and builds gt5.exe (the
# first time takes 30 to 60 minutes), and you can play with GT5\play.bat. -NoBuild stops after the
# checks. To update later, use update.ps1.
param([switch]$NoBuild)
# Errors are checked through exit codes: with "Stop", Windows PowerShell turns a native command's
# redirected stderr into a terminating error before the check can run.
$ErrorActionPreference = "Continue"
$base = $PSScriptRoot
$gt5 = Join-Path $base "GT5"
$rc = Join-Path $base "ps3recomp"
$venv = Join-Path $base "venv"
$py = Join-Path $venv "Scripts\python.exe"
$script:todo = 0

function Ok([string]$m) { Write-Host "  [ok]      $m" -ForegroundColor Green }
function Todo([string]$m, [string]$fix) {
    Write-Host "  [missing] $m" -ForegroundColor Yellow
    if ($fix) { Write-Host "            $fix" }
    $script:todo++
}
function Stop-Setup([string]$m) { Write-Host ""; Write-Host "setup stopped: $m" -ForegroundColor Red; exit 1 }

if (-not (Test-Path (Join-Path $gt5 "scripts\relift.ps1"))) {
    Stop-Setup "run this from the gt5-recomp folder (the one with setup.ps1, README.md and the GT5 folder)."
}
Write-Host "== GT5 Recomp setup in $base"
Write-Host ""
Write-Host "Tools:"

# ---- Git ------------------------------------------------------------------------------------------------
if (Get-Command git -ErrorAction SilentlyContinue) { Ok "Git" }
else { Todo "Git" "Install Git for Windows (https://git-scm.com), then run setup.ps1 again."; Stop-Setup "Git is needed to fetch ps3recomp." }

# ---- Python 3.11 or newer (the py launcher first: plain "python" can be the Microsoft Store stub) --------
$pyCmd = $null
foreach ($cand in @(@("py", "-3.11"), @("py", "-3.12"), @("py", "-3.13"), @("py", "-3"), @("python"), @("python3"))) {
    if (-not (Get-Command $cand[0] -ErrorAction SilentlyContinue)) { continue }
    $rest = @($cand | Select-Object -Skip 1)
    $v = & $cand[0] @rest -c "import sys; print('%d.%d' % sys.version_info[:2])" 2>$null
    if ($LASTEXITCODE -eq 0 -and "$v" -match '^3\.(\d+)$' -and [int]$Matches[1] -ge 11) { $pyCmd = $cand; break }
}
if (Test-Path $py) { Ok "Python environment (venv)" }
elseif ($pyCmd) {
    $rest = @($pyCmd | Select-Object -Skip 1)
    & $pyCmd[0] @rest -m venv $venv
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $py)) { Stop-Setup "making the Python environment failed (see the message above)." }
    Ok "Python environment (venv), made with $($pyCmd -join ' ')"
} else {
    Todo "Python 3.11 or newer" "Install it from https://www.python.org/downloads/ and tick 'Add python.exe to PATH' in the installer, then run setup.ps1 again."
    Stop-Setup "Python is needed for the tools."
}

# ---- Visual Studio with the Clang tools (what GT5\scripts\build.cmd needs) -------------------------------
$vsOk = Test-Path "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe"
if (-not $vsOk) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath
        $vsOk = $vsPath -and (Test-Path (Join-Path $vsPath "VC\Tools\Llvm\x64\bin\clang-cl.exe"))
    }
}
if ($vsOk) { Ok "Visual Studio with the C++ Clang tools" }
else { Todo "Visual Studio 2022 with 'Desktop development with C++' and 'C++ Clang tools for Windows'" "Install the free Community edition (https://visualstudio.microsoft.com), choose that workload and tick the Clang tools under Individual components." }

# ---- ps3recomp, patched for this version (the same step update.ps1 uses) --------------------------------
$out = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $base "update.ps1") -Here 2>&1
if ($LASTEXITCODE -ne 0) { $out | ForEach-Object { Write-Host "            $_" }; Stop-Setup "fetching or patching ps3recomp failed (see above)." }
Ok "ps3recomp, patched"
& $py -m pip install --quiet --disable-pip-version-check -r (Join-Path $rc "requirements.txt")
if ($LASTEXITCODE -ne 0) { Stop-Setup "installing the Python packages failed (see above)." }
Ok "Python packages"

# ---- FFmpeg (LGPL shared build; GT5\CMakeLists.txt expects this folder name) -----------------------------
$ffname = "ffmpeg-n9.0-latest-win64-lgpl-shared-9.0"
$ffdir = Join-Path $base "ffmpeg"
if (-not (Test-Path (Join-Path $ffdir "$ffname\bin"))) {
    Write-Host "            downloading FFmpeg (about 100 MB)..."
    New-Item -ItemType Directory -Force $ffdir | Out-Null
    $zip = Join-Path $ffdir "ffmpeg.zip"
    $url = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/$ffname.zip"
    try {
        $ProgressPreference = "SilentlyContinue"
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing -ErrorAction Stop
        Expand-Archive -Path $zip -DestinationPath $ffdir -Force -ErrorAction Stop
        Remove-Item $zip
    } catch { }
}
if (Test-Path (Join-Path $ffdir "$ffname\bin")) { Ok "FFmpeg (plays the game's movies)" }
else { Todo "FFmpeg" "Download $ffname.zip from https://github.com/BtbN/FFmpeg-Builds/releases and unpack it into the ffmpeg folder, so that ffmpeg\$ffname\bin exists." }

# ---- Your game files ------------------------------------------------------------------------------------
Write-Host ""
Write-Host "Your files (see Setup in README.md for where each comes from):"
foreach ($d in "input", "vfs") { New-Item -ItemType Directory -Force (Join-Path $gt5 $d) | Out-Null }

$elf = Join-Path $gt5 "input\EBOOT.elf"
if (-not (Test-Path $elf)) {
    if (Test-Path (Join-Path $gt5 "input\EBOOT.BIN")) {
        Todo "GT5\input\EBOOT.elf" "You have EBOOT.BIN there, but the tools need the DECRYPTED file: in RPCS3, Utilities > Decrypt PS3 Binaries, pick EBOOT.BIN, and put the result in GT5\input named EBOOT.elf."
    } else {
        Todo "GT5\input\EBOOT.elf (your decrypted game executable)" "In RPCS3, Utilities > Decrypt PS3 Binaries, pick your disc's PS3_GAME\USRDIR\EBOOT.BIN, and put the result in GT5\input named EBOOT.elf."
    }
} else {
    $fs = [IO.File]::OpenRead($elf); $magic = New-Object byte[] 4; [void]$fs.Read($magic, 0, 4); $fs.Close()
    $sha1 = (Get-FileHash $elf -Algorithm SHA1).Hash
    if (($magic -join ",") -eq "83,67,69,0") {
        Todo "a decrypted GT5\input\EBOOT.elf" "This file is still encrypted (it's EBOOT.BIN renamed). Decrypt it in RPCS3: Utilities > Decrypt PS3 Binaries."
    } elseif (($magic -join ",") -ne "127,69,76,70") {
        Todo "a valid GT5\input\EBOOT.elf" "This file isn't a PS3 executable. Decrypt your disc's PS3_GAME\USRDIR\EBOOT.BIN in RPCS3 and use the result."
    } elseif ($sha1 -eq "1535B865EC7DD58E7704B378B8621532ECF844E1") {
        Ok "GT5\input\EBOOT.elf: Europe BCES00569, version 01.00 (the tested one)"
    } elseif ($sha1 -eq "40126D2D14B0A95B9601BB11E3A6E778186067AC") {
        Ok "GT5\input\EBOOT.elf: North America BCUS98114 (players have raced on it)"
    } else {
        Ok "GT5\input\EBOOT.elf (SHA-1 $sha1)"
        Write-Host "            Not one tried yet (Europe and US 01.00 are). Another disc version may work: please report how it goes. One from a game update (2.xx) won't." -ForegroundColor Yellow
    }
}

$game = Join-Path $gt5 "vfs\PS3_GAME"
if (Test-Path (Join-Path $game "USRDIR")) {
    if (Test-Path (Join-Path $game "PARAM.SFO")) { Ok "GT5\vfs\PS3_GAME (your disc's game data)" }
    else { Todo "GT5\vfs\PS3_GAME\PARAM.SFO" "Copy the whole PS3_GAME folder from your disc, including PARAM.SFO." }
} elseif (Test-Path (Join-Path $game "PS3_GAME\USRDIR")) {
    Todo "GT5\vfs\PS3_GAME at the right level" "It's one folder too deep: move the contents of GT5\vfs\PS3_GAME\PS3_GAME up into GT5\vfs\PS3_GAME."
} else {
    $nested = Get-ChildItem (Join-Path $gt5 "vfs") -Directory -ErrorAction SilentlyContinue |
              Where-Object { Test-Path (Join-Path $_.FullName "PS3_GAME\USRDIR") } | Select-Object -First 1
    if ($nested) { Todo "GT5\vfs\PS3_GAME" "Found it inside GT5\vfs\$($nested.Name): move that PS3_GAME folder up so it's GT5\vfs\PS3_GAME." }
    else { Todo "GT5\vfs\PS3_GAME (your disc's game data, about 17 GB)" "Copy the PS3_GAME folder from your disc dump to GT5\vfs, so that GT5\vfs\PS3_GAME\USRDIR exists." }
}

$flash = Join-Path $gt5 "dev_flash"
if (Test-Path (Join-Path $flash "sys")) { Ok "GT5\dev_flash (PS3 system software)" }
elseif (Test-Path (Join-Path $flash "dev_flash\sys")) { Todo "GT5\dev_flash at the right level" "It's one folder too deep: move the contents of GT5\dev_flash\dev_flash up into GT5\dev_flash." }
else { Todo "GT5\dev_flash (PS3 system software)" "In RPCS3, File > Install Firmware with Sony's PS3UPDAT.PUP, then copy RPCS3's dev_flash folder to GT5\dev_flash (it contains sys, vsh, data...)." }

Write-Host ""
if ($script:todo -gt 0) {
    Write-Host "$($script:todo) thing(s) still to do (marked [missing] above). Sort them out, then run setup.ps1 again." -ForegroundColor Yellow
    exit 1
}
if ($NoBuild) { Write-Host "Everything is in place. Run setup.ps1 without -NoBuild to build the game." -ForegroundColor Green; exit 0 }

# ---- Generate the game code (only when it's missing or out of date), then build --------------------------
Push-Location $gt5
& $py "scripts\lift_stamp.py" --check | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "Generating the game code from your EBOOT.elf (about 10 minutes)..."
    & powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\relift.ps1"
    if ($LASTEXITCODE -ne 0) { Pop-Location; Stop-Setup "generating the game code failed (see above). Please report it with that output: https://github.com/MormonFTP-hub/gt5-recomp/issues/new?template=2-problem.yml" }
} else {
    Write-Host "The game code is already generated and current."
}
Write-Host "Building gt5.exe (the first build takes 20 to 50 minutes)..."
cmd /c "scripts\build.cmd gt5"
$built = $LASTEXITCODE
Pop-Location
if ($built -ne 0) { Stop-Setup "the build failed (see above). Please report it with that output: https://github.com/MormonFTP-hub/gt5-recomp/issues/new?template=2-problem.yml" }
Write-Host ""
Write-Host "Done! Start GT5Launcher.exe in this folder (settings, Play, Updates), or GT5\play.bat directly." -ForegroundColor Green
Write-Host "Later, the launcher's Updates page or a double-click on Update.bat gets new versions." -ForegroundColor Green
