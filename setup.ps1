# Set up and build GT5 Recomp with one command. Run it from the gt5-recomp folder:
#   powershell -ExecutionPolicy Bypass -File setup.ps1
# Run it as often as you like. It does whatever is still to do: fetches and patches ps3recomp, makes
# the Python environment, downloads FFmpeg (optional: the movies), then checks your game files and says exactly what's
# missing or wrong. When everything is in place it generates the game code and builds gt5.exe with both
# renderers, DirectX 12 and Vulkan (the first time takes 30 to 60 minutes), then adds AMD FSR 3 and, on a PC
# with an NVIDIA RTX card whose installer showed NVIDIA's licence, NVIDIA DLSS. Then play with
# GT5Launcher.exe or GT5\play.bat. -NoBuild stops after the checks. To update later, use update.ps1.
param([switch]$NoBuild)
# Errors are checked through exit codes: with "Stop", Windows PowerShell turns a native command's
# redirected stderr into a terminating error before the check can run.
$ErrorActionPreference = "Continue"
# Another Python's settings (Anaconda, QGIS and others set these for everyone) would make the port's own Python load
# that one's libraries: not for this run or anything it starts.
Remove-Item Env:PYTHONHOME, Env:PYTHONPATH -ErrorAction SilentlyContinue
# On a drive that doesn't record who owns a file (exFAT, FAT32, many USB and network drives) git refuses
# to work in the install folder ("detected dubious ownership", #41). Trust it for git run from here and
# what this starts, through git's environment config; the player's own git settings stay as they are.
if ($env:GT5_GIT_SAFE -ne "1") {
    $gn = 0; if ("$env:GIT_CONFIG_COUNT" -match '^\d+$') { $gn = [int]$env:GIT_CONFIG_COUNT }
    Set-Item "env:GIT_CONFIG_KEY_$gn" "safe.directory"; Set-Item "env:GIT_CONFIG_VALUE_$gn" "*"
    $env:GIT_CONFIG_COUNT = "$($gn + 1)"; $env:GT5_GIT_SAFE = "1"
}
$base = $PSScriptRoot
# Git: the PC's own, else the port's own portable copy in tools\git (get-git.ps1; nothing installed in Windows).
$ownGit = Join-Path $base "tools\git\cmd"
if (-not (Get-Command git -ErrorAction SilentlyContinue) -and (Test-Path -LiteralPath (Join-Path $ownGit "git.exe"))) { $env:Path = "$ownGit;$env:Path" }
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
# PARAM.SFO's values by key (APP_VER, TITLE_ID, ...), or $null when it isn't one.
function Read-Sfo([string]$path) {
    try { $b = [IO.File]::ReadAllBytes($path) } catch { return $null }
    if ($b.Length -lt 20 -or $b[1] -ne 0x50 -or $b[2] -ne 0x53 -or $b[3] -ne 0x46) { return $null }
    $keyT = [BitConverter]::ToUInt32($b, 8); $dataT = [BitConverter]::ToUInt32($b, 12)
    $n = [BitConverter]::ToUInt32($b, 16); $r = @{}
    for ($i = 0; $i -lt $n; $i++) {
        $e = 20 + $i * 16
        if ($e + 16 -gt $b.Length) { break }
        $ko = [BitConverter]::ToUInt16($b, $e); $fmt = [BitConverter]::ToUInt16($b, $e + 2)
        $len = [BitConverter]::ToUInt32($b, $e + 4); $do = [BitConverter]::ToUInt32($b, $e + 12)
        $k = ""; $p = $keyT + $ko
        while ($p -lt $b.Length -and $b[$p] -ne 0) { $k += [char]$b[$p]; $p++ }
        if ($dataT + $do + $len -gt $b.Length) { continue }
        if ($fmt -eq 0x0404) { $r[$k] = [BitConverter]::ToUInt32($b, $dataT + $do) }
        else { $r[$k] = [Text.Encoding]::UTF8.GetString($b, $dataT + $do, $len).TrimEnd([char]0) }
    }
    return $r
}

if (-not (Test-Path (Join-Path $gt5 "scripts\relift.ps1"))) {
    Stop-Setup "run this from the gt5-recomp folder (the one with setup.ps1, README.md and the GT5 folder)."
}
Write-Host "== GT5 Recomp setup in $base"
Write-Host ""

# ---- Can this PC run the game? Before anything long (GT5-Installer.bat asks the same on its first page) ----------
# The game is built for processors with AVX2 (GT5\CMakeLists.txt: -mavx2 -mfma ...): one without it stops the moment
# the game starts, after an hour of building. The first build also needs memory.
function Get-PcProblem {
    try {
        if (-not ("Gt5Pc.Cpu" -as [type])) {
            Add-Type -Namespace Gt5Pc -Name Cpu -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern bool IsProcessorFeaturePresent(uint feature);
[DllImport("kernel32.dll")] public static extern ulong GetEnabledXStateFeatures();
[DllImport("kernel32.dll")] public static extern bool GetPhysicallyInstalledSystemMemory(out ulong kb);
'@
        }
        $avx2 = $true
        # No AVX state in use: no AVX, so no AVX2. A Windows that knows the AVX entries (39) answers AVX2 (40) directly;
        # an older one answers no to both, and the processor is given the benefit of the doubt.
        if (([Gt5Pc.Cpu]::GetEnabledXStateFeatures() -band 4) -eq 0) { $avx2 = $false }
        elseif ([Gt5Pc.Cpu]::IsProcessorFeaturePresent(39)) { $avx2 = [Gt5Pc.Cpu]::IsProcessorFeaturePresent(40) }
        if ($env:GT5_INSTALL_NOAVX2) { $avx2 = $false }
        if (-not $avx2) { return "this PC's processor is too old for the game (it has no AVX2)." }
        $kb = [uint64]0; $gb = 0
        if ([Gt5Pc.Cpu]::GetPhysicallyInstalledSystemMemory([ref]$kb)) { $gb = [int][math]::Round($kb / 1MB) }
        if ($env:GT5_INSTALL_RAMGB) { $gb = [int]$env:GT5_INSTALL_RAMGB }
        if ($gb -gt 0 -and $gb -lt 8) { return "this PC has $gb GB of memory; the game needs at least 8 GB." }
    } catch { }
    return ""
}
$pcProblem = Get-PcProblem
if ($pcProblem) { Stop-Setup $pcProblem }
Write-Host "Tools:"

# ---- Git ------------------------------------------------------------------------------------------------
if (-not (Get-Command git -ErrorAction SilentlyContinue) -and (Test-Path (Join-Path $base "get-git.ps1"))) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $base "get-git.ps1") | ForEach-Object { Write-Host "            $_" }
    if (Test-Path -LiteralPath (Join-Path $ownGit "git.exe")) { $env:Path = "$ownGit;$env:Path" }
}
if (Get-Command git -ErrorAction SilentlyContinue) { Ok "Git" }
else { Todo "Git" "get-git.ps1 couldn't fetch the port's own Git (see above; is the internet connection up?). Run setup.ps1 again, or install Git for Windows (https://git-scm.com)."; Stop-Setup "Git is needed to fetch ps3recomp." }

# ---- Python: the port's own copy (get-python.ps1, in tools\python) ------------------------------------------
# The tools run in an environment (venv) made from python.org's Python 3.11.9 that get-python.ps1 puts in
# this folder: the version they are tested on, whatever Python the PC has or hasn't. An environment that
# already exists is kept as it is (an earlier setup made it from the PC's Python), unless GT5_OWN_PYTHON=1
# (GT5-Installer.bat sets it) asks for it to be made again from the port's own.
$ownPy = Join-Path $base "tools\python\python.exe"
function Get-PyVer([string]$p) { return "$(& $p -c "import sys; print('%d.%d.%d%s' % (sys.version_info[:3] + ('' if sys.version_info.releaselevel == 'final' else ' ' + sys.version_info.releaselevel,)))" 2>$null)".Trim() }
function Test-VenvIsOwn {
    $cfg = Join-Path $venv "pyvenv.cfg"
    if (-not (Test-Path -LiteralPath $cfg)) { return $false }
    $h = (Get-Content -LiteralPath $cfg | Where-Object { $_ -match '^\s*home\s*=' } | Select-Object -First 1) -replace '^\s*home\s*=\s*', ''
    return ("$h".Trim().TrimEnd('\') -ieq (Split-Path $ownPy -Parent))
}
if (-not (Test-Path $py) -or ($env:GT5_OWN_PYTHON -eq "1" -and -not (Test-VenvIsOwn))) {
    if (-not (Test-Path $ownPy)) { & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $base "get-python.ps1") | ForEach-Object { Write-Host "            $_" } }
    if ((Test-Path $ownPy) -and (Test-Path $py) -and -not (Test-VenvIsOwn)) {
        Write-Host "            Making the Python environment again from the port's own Python (it was made from the PC's: Python $(Get-PyVer $py))."
        Remove-Item -LiteralPath $venv -Recurse -Force -ErrorAction SilentlyContinue
    }
}
if (Test-Path $py) { $pyVer = Get-PyVer $py; Ok "Python environment (venv, Python $pyVer)" }
else {
    # The port's own Python, else (the download failed) one already on the PC: 3.11 or newer, final releases only.
    $pyCmd = $null
    if (Test-Path $ownPy) { $pyCmd = @($ownPy) }
    else {
        foreach ($cand in @(@("py", "-3.11"), @("py", "-3.12"), @("py", "-3.13"), @("py", "-3"), @("python"), @("python3"))) {
            if (-not (Get-Command $cand[0] -ErrorAction SilentlyContinue)) { continue }
            $rest = @($cand | Select-Object -Skip 1)
            $v = & $cand[0] @rest -c "import sys; print('%d.%d' % sys.version_info[:2] if sys.version_info.releaselevel == 'final' else 'pre-release')" 2>$null
            if ($LASTEXITCODE -eq 0 -and "$v" -match '^3\.(\d+)$' -and [int]$Matches[1] -ge 11) { $pyCmd = $cand; break }
        }
    }
    if (-not $pyCmd) {
        Todo "Python" "get-python.ps1 couldn't fetch the port's own Python (see above; is the internet connection up?). Run setup.ps1 again, or install Python 3.11 from https://www.python.org/downloads/ and tick 'Add python.exe to PATH'."
        Stop-Setup "Python is needed for the tools."
    }
    $rest = @($pyCmd | Select-Object -Skip 1)
    & $pyCmd[0] @rest -m venv $venv
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $py)) { Stop-Setup "making the Python environment failed (see the message above)." }
    $pyVer = Get-PyVer $py
    Ok "Python environment (venv, Python $pyVer$(if (Test-VenvIsOwn) { ', the port''s own' } else { ', from the PC''s ' + ($pyCmd -join ' ') }))"
}

# ---- Visual Studio with the Clang tools (what GT5\scripts\build.cmd needs), or get-toolchain.ps1's tools ----
# A usable Visual Studio has both clang-cl and vcvars64.bat (build.cmd calls it): a folder an uninstall left
# behind can keep clang-cl alone, and then the build stopped with "The system cannot find the path specified" (#35).
function Test-VsFolder([string]$p) {
    $p -and (Test-Path (Join-Path $p "VC\Tools\Llvm\x64\bin\clang-cl.exe")) -and (Test-Path (Join-Path $p "VC\Auxiliary\Build\vcvars64.bat"))
}
$vsOk = Test-VsFolder "$env:ProgramFiles\Microsoft Visual Studio\2022\Community"
if (-not $vsOk) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        foreach ($vsPath in @(& $vswhere -sort -products * -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath)) {
            if (Test-VsFolder $vsPath) { $vsOk = $true; break }
        }
    }
}
# The same tools without Visual Studio, where build.cmd looks: the folder get-toolchain.ps1 recorded, else
# "toolchain" next to this folder.
function Find-Toolchain {
    $rec = Join-Path $base ".gt5\toolchain-path.txt"
    $cands = @($env:GT5_TOOLCHAIN)
    if (Test-Path $rec) { $cands += [Console]::OutputEncoding.GetString([IO.File]::ReadAllBytes($rec)).Trim() }
    $cands += Join-Path (Split-Path $base -Parent) "toolchain"
    foreach ($c in $cands) { if ($c -and (Test-Path -LiteralPath (Join-Path $c "gt5-toolchain.txt"))) { return $c } }
}
# The Windows SDK (kernel32.lib and the other Windows libraries), shared by every Visual Studio on the PC: one
# installed without it stopped the build with "could not open 'kernel32.lib'" (#35). get-toolchain.ps1's tools
# bring their own.
$sdkOk = $false
foreach ($r in @("${env:ProgramFiles(x86)}\Windows Kits\10\Lib", "$env:ProgramFiles\Windows Kits\10\Lib")) {
    foreach ($d in @(Get-ChildItem -LiteralPath $r -Directory -ErrorAction SilentlyContinue)) {
        if (Test-Path (Join-Path $d.FullName "um\x64\kernel32.lib")) { $sdkOk = $true }
    }
}
if ($vsOk -and -not $sdkOk -and -not (Find-Toolchain)) {
    Todo "Windows SDK" "Your Visual Studio has the Clang tools but no Windows SDK (kernel32.lib and the other Windows libraries the build needs). In the Visual Studio Installer choose Modify, tick 'Windows 11 SDK' under Individual components, then run setup.ps1 again. (GT5-Installer.bat fetches its own compiler tools instead in that case.)"
}
elseif ($vsOk -and $sdkOk) { Ok "Visual Studio with the C++ Clang tools" }
else {
    $tc = Find-Toolchain
    if (-not $tc) {
        Write-Host "            Visual Studio with the C++ Clang tools isn't installed. get-toolchain.ps1 can get the same" -ForegroundColor Yellow
        Write-Host "            compiler tools without it: about 1.5 GB in a folder you choose, on any drive (C: stays free)." -ForegroundColor Yellow
        $a = Read-Host "            Run get-toolchain.ps1 now? (Y/N)"
        if ("$a".Trim() -match '^[Yy]') { & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $base "get-toolchain.ps1") }
        $tc = Find-Toolchain
    }
    if ($tc) { Ok "Compiler tools from get-toolchain.ps1 (no Visual Studio needed), in $tc" }
    else { Todo "Visual Studio 2022 with 'Desktop development with C++' and 'C++ Clang tools for Windows'" "Install the free Community edition (https://visualstudio.microsoft.com), choose that workload and tick the Clang tools under Individual components. Not much space on C:? Run get-toolchain.ps1 instead (README.md: No Visual Studio)." }
}

# ---- ps3recomp, patched for this version (the same step update.ps1 uses) --------------------------------
$out = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $base "update.ps1") -Here 2>&1
if ($LASTEXITCODE -ne 0) { $out | ForEach-Object { Write-Host "            $_" }; Stop-Setup "the game's tools couldn't be fetched (is the internet connection up?)." }
Ok "ps3recomp, patched"
# The packages at the versions the tools are tested with (requirements.txt gives only lower bounds, so each install
# got whatever was newest that day). A later version of one of these comes with an update that names it here.
$pins = @("capstone==5.0.9", "construct==2.10.70", "pycryptodome==4.2.0", "tabulate==0.10.0", "tqdm==4.70.1")
& $py -m pip install --quiet --disable-pip-version-check -r (Join-Path $rc "requirements.txt") @pins
if ($LASTEXITCODE -ne 0) { Stop-Setup "the Python packages couldn't be installed (is the internet connection up?)." }
Ok "Python packages"

# ---- FFmpeg (plays the game's movies; optional) ------------------------------------------------------------
# get-ffmpeg.ps1 fetches a fixed release, checked by its SHA-256. Without it the game builds and runs, and only its
# movies don't play: a failed download is a note, never a stop, and the next update tries again.
$ffname = "ffmpeg-n9.0-latest-win64-lgpl-shared-9.0"
$ffdir = Join-Path $base "ffmpeg"
$getFf = Join-Path $base "get-ffmpeg.ps1"
if ((Test-Path -LiteralPath $getFf) -and -not (Test-Path (Join-Path $ffdir "$ffname\bin\avcodec-63.dll"))) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $getFf | ForEach-Object { Write-Host "            $_" }
}
if (Test-Path (Join-Path $ffdir "$ffname\bin\avcodec-63.dll")) { Ok "FFmpeg (plays the game's movies)" }
else { Write-Host "  [note]    FFmpeg: not added this time, so the game's movies won't play. The next update tries again." -ForegroundColor Yellow }

# ---- Your game files ------------------------------------------------------------------------------------
Write-Host ""
Write-Host "Your files (see Setup in README.md for where each comes from):"
foreach ($d in "input", "vfs") { New-Item -ItemType Directory -Force (Join-Path $gt5 $d) | Out-Null }

$elf = Join-Path $gt5 "input\EBOOT.elf"
$elfDisc = $false   # the disc's own version 01.00 executable (no update)
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
        Ok "GT5\input\EBOOT.elf: Europe BCES00569, version 01.00 (the tested one)"; $elfDisc = $true
    } elseif ($sha1 -eq "40126D2D14B0A95B9601BB11E3A6E778186067AC") {
        Ok "GT5\input\EBOOT.elf: North America BCUS98114, version 01.00 (players have raced on it)"; $elfDisc = $true
    } elseif ($sha1 -eq "49A3662D4B541BB4E0C2E72D2602F89FEB710E71") {
        Ok "GT5\input\EBOOT.elf: Hong Kong / Asia BCAS20108, version 01.00 (tested)"; $elfDisc = $true
    } elseif ($sha1 -eq "96DEA35B194AA80E993CAD9EBE42E96072F3A3D5") {
        Ok "GT5\input\EBOOT.elf: Hong Kong / Asia BCAS20108, update 2.01 (tested; needs the update's data in GT5\hdd0, README: Game updates)"
    } elseif ($sha1 -eq "12EF249F9B95591E08251AC85F103B1451DA23BF") {
        Ok "GT5\input\EBOOT.elf: North America BCUS98114, update 2.17 (tested; needs the update's data in GT5\hdd0, README: Game updates)"
    } elseif ($sha1 -eq "266EE27115B2AAC6318AD0122DAE56EDE94B797F") {
        Ok "GT5\input\EBOOT.elf: North America BCUS98114, update 2.11 with the GT5 Master Mod 1.9.2 (a player raced on it; needs 2.11's and the mod's data in GT5\hdd0, README: Game updates)"
    } elseif ($sha1 -eq "306F86C62B9F03041C903BE96AC59E5C3F130452") {
        Ok "GT5\input\EBOOT.elf: Europe BCES00569, update 2.17 (tested; needs the update's data in GT5\hdd0, README: Game updates)"
    } else {
        Ok "GT5\input\EBOOT.elf (SHA-1 $sha1)"
        Write-Host "            Not one tried yet. Another disc version may well work: please report how it goes. One from a game update (2.xx) also needs the update's data in GT5\hdd0 (README: Game updates)." -ForegroundColor Yellow
    }
}

# A game update's data in GT5\hdd0 needs the update's executable too. GT5 reads updated files from the
# hard disk whatever its own version, so the disc's 01.00 executable runs the update's newer scripts,
# which call code it doesn't have: the game stops before the main menu with a black screen (13 Nov 2026,
# US 2.11 with the Master Mod). The update's PARAM.SFO (APP_VER) or EBOOT.BIN in GT5\hdd0\game\<title ID>
# says an update is installed; the disc version writes neither there.
# scripts\update_exe.ps1 finds the update's EBOOT.BIN (GT5\hdd0, or RPCS3's dev_hdd0), unlocks it with
# RPCS3 and swaps it in, keeping the disc's as EBOOT.disc-01.00.elf; the game code is then generated for it.
if ($elfDisc) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $gt5 "scripts\update_exe.ps1") -Gt5 $gt5
    if ($LASTEXITCODE -eq 2) { Todo "GT5\input\EBOOT.elf from the game update in GT5\hdd0" "See the lines just above." }
    elseif ((Get-FileHash $elf -Algorithm SHA1).Hash -ne $sha1) { Ok "GT5\input\EBOOT.elf: switched to the installed update's executable" }
    $global:LASTEXITCODE = 0
}

$game = Join-Path $gt5 "vfs\PS3_GAME"
if (Test-Path (Join-Path $game "USRDIR")) {
    if (Test-Path (Join-Path $game "PARAM.SFO")) {
        # Joins a GT.VOL that a FAT32 dump split into pieces, and checks it's there at all.
        & (Join-Path $gt5 "scripts\fix_game_files.ps1") -Gt5 $gt5
        if ($LASTEXITCODE -eq 2) {
            Todo "GT5\vfs\PS3_GAME\USRDIR\GT.VOL (the game's data, about 12 GB)" "Copy it from PS3_GAME\USRDIR in your disc dump (or its pieces GT.VOL.66600, .66601, ...: they're joined automatically)."
        } else { Ok "GT5\vfs\PS3_GAME (your disc's game data)" }
        $global:LASTEXITCODE = 0
    }
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
# Processors that calculate wrongly are left out of everything from here on (pc-check.ps1 says why).
$pcCheck = Join-Path $base "pc-check.ps1"
if (Test-Path -LiteralPath $pcCheck) {
    . $pcCheck
    $pcNote = Use-GoodProcessors -Base $base -Py $py
    if ($pcNote) { Write-Host $pcNote -ForegroundColor Yellow }
}
Push-Location $gt5
& $py "scripts\lift_stamp.py" --check | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "Generating the game code from your EBOOT.elf (about 10 minutes)..."
    & powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\relift.ps1"
    if ($LASTEXITCODE -ne 0) {
        # Seen on a player's PC: a step stopped with an error from inside Python itself that no line of the
        # tool can raise: two of its processors calculated wrongly. So the check is made again, now with
        # one of the lift's own jobs, any processor that fails is left out, and the lift starts again.
        if (Get-Command Use-GoodProcessors -ErrorAction SilentlyContinue) {
            $pcNote = Use-GoodProcessors -Base $base -Py $py -Again
            if ($pcNote) { Write-Host $pcNote -ForegroundColor Yellow }
        }
        Write-Host "Generating the game code stopped (see above). Trying once more from the start..." -ForegroundColor Yellow
        & powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\relift.ps1"
    }
    if ($LASTEXITCODE -ne 0) { Write-Host "Generating the game code failed twice (Python $pyVer)."; Pop-Location; Stop-Setup "converting the game's code didn't finish." }
} else {
    Write-Host "The game code is already generated and current."
}
Write-Host "Building gt5.exe (the first build takes 20 to 50 minutes)..."
# The parts the game is built with or gets next to it are set up here, not on first use: Vulkan (the second
# renderer) before the build, so this one build has both renderers; AMD FSR 3 and, on a PC with an NVIDIA RTX
# card, NVIDIA DLSS after it. Their scripts (GT5\scripts\add_*.ps1) write their whole text to GT5\logs and pass
# on only their progress lines (GT5_ADD ...). None of them can stop setup: without one, the launcher simply
# doesn't offer it, and the next update tries again.
$addCommon = Join-Path $gt5 "scripts\addon_common.ps1"
$addOns = Test-Path -LiteralPath $addCommon
if ($addOns) { . $addCommon }
$logs = Join-Path $gt5 "logs"
$cacheFile = Join-Path $gt5 "build\CMakeCache.txt"
function Test-VulkanInBuild { return (Test-Path -LiteralPath $cacheFile) -and [bool](Select-String -LiteralPath $cacheFile -Pattern '^PS3RECOMP_RSX_VULKAN:BOOL=ON' -Quiet) }
if ($addOns) {
    $r = Invoke-AddOn (Join-Path $gt5 "scripts\add_vulkan.ps1") @("-Prepare") "add_vulkan" $logs
    if ($r -eq 0) { Write-Host "  Vulkan: built in" } elseif ($r -eq 1) { Write-Host "  Vulkan: left out this time (DirectX 12 only; GT5\logs\add_vulkan.log says why). The next update tries again." }
}
cmd /c "scripts\build.cmd gt5"
$built = $LASTEXITCODE
if ($built -ne 0 -and $addOns -and (Test-VulkanInBuild)) {
    # The game must build even when Vulkan doesn't (a newer compiler that its code doesn't suit, say): once more
    # without it, which rebuilds only the runtime. The next update tries Vulkan again.
    Write-Host "The build stopped. Building once more without Vulkan..."
    $null = Invoke-AddOn (Join-Path $gt5 "scripts\add_vulkan.ps1") @("-Remove", "-Prepare") "add_vulkan-off" $logs
    cmd /c "scripts\build.cmd gt5"
    $built = $LASTEXITCODE
    if ($built -eq 0) { Write-Host "  Vulkan: left out this time (the build with it stopped; GT5\logs says why). The next update tries again." }
}
Pop-Location
if ($built -ne 0) { Stop-Setup "the build didn't finish." }
# The launcher (settings, Play, updates) is built after every good build (GT5\scripts\build_run.ps1). Without it the
# shortcut would start the game in a console window: once more, else the setup isn't done.
$launcherExe = Join-Path $base "GT5Launcher.exe"
$buildLauncher = Join-Path $base "launcher\build-launcher.ps1"
if (-not (Test-Path -LiteralPath $launcherExe) -and (Test-Path -LiteralPath $buildLauncher)) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $buildLauncher
    if (-not (Test-Path -LiteralPath $launcherExe)) { Stop-Setup "the launcher couldn't be built." }
}
if ($addOns) {
    $r = Invoke-AddOn (Join-Path $gt5 "scripts\add_fsr.ps1") @() "add_fsr" $logs
    if ($r -eq 0) { Write-Host "  AMD FSR 3: added" } elseif ($r -eq 1) { Write-Host "  AMD FSR 3: not added this time (GT5\logs\add_fsr.log says why). The next update tries again." }
    # NVIDIA's licence: accepted with the installer's Install button, which showed it (GT5-Installer.bat writes
    # .gt5\installer-licences.txt), or when DLSS was added before. A setup.ps1 run by hand without either
    # leaves DLSS out without a word: the launcher's Display page can add it, with the same licence line.
    $licFile = Join-Path $base ".gt5\installer-licences.txt"
    $dlssShown = (Test-Path -LiteralPath $licFile) -and [bool](Select-String -LiteralPath $licFile -Pattern '^dlss\b' -Quiet)
    $dlssThere = Test-Path -LiteralPath (Join-Path $gt5 "build\gt5_upscale_dlss.dll")
    if (($dlssShown -or $dlssThere) -and (Test-RtxCard)) {
        $r = Invoke-AddOn (Join-Path $gt5 "scripts\add_dlss.ps1") @("-Accept") "add_dlss" $logs
        if ($r -eq 0) { Write-Host "  NVIDIA DLSS: added" } elseif ($r -eq 1) { Write-Host "  NVIDIA DLSS: not added this time (GT5\logs\add_dlss.log says why). The next update tries again." }
    }
}
# The shaders this PC's game has drawn before (a setup over an earlier install keeps them), made with this build
# (gt5.exe --prepare-shaders). On a first install there are none yet and it ends at once. Never a stop.
$gt5exe = Join-Path $gt5 "build\gt5.exe"
if (Test-Path -LiteralPath $gt5exe) {
    $null = Start-Process -FilePath $gt5exe -ArgumentList "--prepare-shaders" -WorkingDirectory $gt5 -NoNewWindow -Wait -PassThru -RedirectStandardError (Join-Path $logs "prepare.log")
}
Write-Host ""
Write-Host "Done! Start GT5Launcher.exe in this folder (settings, Play, Updates), or GT5\play.bat directly." -ForegroundColor Green
Write-Host "Later, the launcher's Updates page or a double-click on Update.bat gets new versions." -ForegroundColor Green
