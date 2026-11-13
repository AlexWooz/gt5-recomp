# Gets the compiler tools that build GT5 Recomp WITHOUT Visual Studio, all in one folder you choose, on
# any drive, so nothing big has to go on C:. Run it from the gt5-recomp folder:
#   powershell -ExecutionPolicy Bypass -File get-toolchain.ps1 [-Path <folder>]
# Without -Path it asks, suggesting a "toolchain" folder next to the gt5-recomp folder. GT5\scripts\build.cmd
# (and so setup.ps1) uses these tools when it finds no Visual Studio with the Clang tools; Visual Studio,
# when it's there, stays the first choice. No administrator rights needed, nothing is installed in Windows:
# to remove the tools, delete the folder.
#
# What goes in the folder (about 1.5 GB once done; it downloads about 1.5 GB and needs 3 GB free while it
# works):
#   llvm   LLVM 19.1.5: clang-cl, lld-link (the official release, github.com/llvm/llvm-project; only
#          the parts the build uses are unpacked)
#   cmake  CMake 3.31.6 (the official zip, github.com/Kitware/CMake)
#   ninja  Ninja 1.12.1 (the official zip, github.com/ninja-build/ninja)
#   msvc   Microsoft's C/C++ libraries (MSVC 14.44) and the Windows SDK (10.0.26100), fetched straight
#          from Microsoft by mmozeiko's portable-msvc.py, and only after YOU accept Microsoft's licence
# Every file this script downloads itself is checked against the SHA-256 written below; portable-msvc.py
# checks each Microsoft file against the SHA-256 in Microsoft's own catalog. Run it again to finish or
# repair: the parts that are done are kept.
param([string]$Path = "")
$ErrorActionPreference = "Continue"
$base = $PSScriptRoot
# GitHub and Microsoft need TLS 1.2, which Windows PowerShell 5.1 doesn't always offer by default.
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

# ---- Pinned downloads -------------------------------------------------------------------------------------
$Pins = @{
    llvm  = @{ Name = "LLVM 19.1.5"; MB = 806; File = "clang+llvm-19.1.5-x86_64-pc-windows-msvc.tar.xz"
               Url = "https://github.com/llvm/llvm-project/releases/download/llvmorg-19.1.5/clang%2Bllvm-19.1.5-x86_64-pc-windows-msvc.tar.xz"
               Sha256 = "467d1a73ca938f47734af3baac2e78c5e730285469096ee088bb5c9590cabd70" }
    cmake = @{ Name = "CMake 3.31.6"; MB = 44; File = "cmake-3.31.6-windows-x86_64.zip"
               Url = "https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-windows-x86_64.zip"
               Sha256 = "d163cd3ab4959b0a53fa8988f2ddbd2e6c501658201e6a154386bad9dbe4f836" }
    ninja = @{ Name = "Ninja 1.12.1"; MB = 1; File = "ninja-win.zip"
               Url = "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip"
               Sha256 = "f550fec705b6d6ff58f2db3c374c2277a37691678d6aba463adcbb129108467a" }
    # The gist's revision of 2026-09-09.
    pmsvc = @{ Name = "portable-msvc.py"; MB = 1; File = "portable-msvc.py"
               Url = "https://gist.githubusercontent.com/mmozeiko/7f3162ec2988e81e56d5c4e22cde9977/raw/49b58850aa5e5c1e65f090d4651ee76607883e11/portable-msvc.py"
               Sha256 = "1791cccca594ff083ce8d3e0a139e6156d2c312e24332a909e608010f0230a51" }
}
# The MSVC and SDK versions GT5 Recomp is built and tested with. The default (Visual Studio 2026's newest)
# libraries are made for a newer Clang than 19.
$MsvcArgs = @("--vs", "2022", "--msvc-version", "14.44", "--sdk-version", "26100", "--host", "x64", "--target", "x64")
# Where portable-msvc.py's licence link comes from (the Build Tools entry of Microsoft's VS 2022 catalog).
$Channel = "https://aka.ms/vs/17/release/channel"
$LicenceFallback = "https://go.microsoft.com/fwlink/?LinkId=2179911"

function Ok([string]$m) { Write-Host "  [ok]      $m" -ForegroundColor Green }
function Stop-Here([string]$m) { Write-Host ""; Write-Host "get-toolchain stopped: $m" -ForegroundColor Red; exit 1 }
function Get-SizeMB([string]$p) {
    $s = (Get-ChildItem -LiteralPath $p -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
    return [int][math]::Round($s / 1MB)
}

if (-not (Test-Path (Join-Path $base "GT5\scripts\build.cmd"))) {
    Stop-Here "run this from the gt5-recomp folder (the one with setup.ps1, README.md and the GT5 folder)."
}
Write-Host "== Compiler tools for GT5 Recomp without Visual Studio"
Write-Host ""
Write-Host "It puts these in one folder (about 1.5 GB once done, nothing anywhere else):"
Write-Host "  LLVM 19.1.5 (clang-cl, lld-link)      806 MB download   from github.com/llvm/llvm-project"
Write-Host "  CMake 3.31.6 and Ninja 1.12.1          45 MB download   from their GitHub releases"
Write-Host "  Microsoft's C/C++ libraries (MSVC     650 MB download   from Microsoft, by mmozeiko's portable-msvc.py"
Write-Host "  14.44) and Windows SDK 10.0.26100                       (a GitHub gist), after you accept the licence"
Write-Host "It needs about 3 GB free while it works; each download is deleted once it's unpacked."
Write-Host ""

# ---- The folder ---------------------------------------------------------------------------------------------
$default = Join-Path (Split-Path $base -Parent) "toolchain"
if (-not $Path) {
    $free = [math]::Floor((New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($default))).AvailableFreeSpace / 1GB)
    Write-Host "Suggested folder: $default ($free GB free on that drive)"
    $a = Read-Host "Press Enter to use it, or type another folder (on another drive, for example)"
    $Path = if ("$a".Trim()) { "$a".Trim().Trim('"') } else { $default }
}
$Path = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
if ([IO.Path]::GetPathRoot($Path) -eq $Path) { $Path = Join-Path $Path "toolchain" }   # a whole drive: a folder on it
New-Item -ItemType Directory -Force $Path -ErrorAction SilentlyContinue | Out-Null
if (-not (Test-Path -LiteralPath $Path)) { Stop-Here "can't make the folder $Path." }
$dl = Join-Path $Path "downloads"
$free = [math]::Round((New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($Path))).AvailableFreeSpace / 1GB, 1)
Write-Host "Folder: $Path ($free GB free)"
if (-not (Test-Path (Join-Path $Path "gt5-toolchain.txt")) -and $free -lt 3) {
    Stop-Here "that drive has $free GB free and this needs about 3 GB while it works. Free some space, or run it again and choose a folder on another drive."
}

# ---- Python (runs portable-msvc.py and unpacks LLVM): setup.ps1's venv, else Python 3.9 or newer --------------
$py = @(Join-Path $base "venv\Scripts\python.exe")
if (-not (Test-Path $py[0])) {
    $py = $null
    foreach ($cand in @(@("py", "-3"), @("python"), @("python3"))) {
        if (-not (Get-Command $cand[0] -ErrorAction SilentlyContinue)) { continue }
        $rest = @($cand | Select-Object -Skip 1)
        $v = & $cand[0] @rest -c "import sys; print('%d.%d' % sys.version_info[:2])" 2>$null
        if ($LASTEXITCODE -eq 0 -and "$v" -match '^3\.(\d+)$' -and [int]$Matches[1] -ge 9) { $py = $cand; break }
    }
    if (-not $py) { Stop-Here "Python 3.9 or newer is needed. Install it from https://www.python.org/downloads/ (tick 'Add python.exe to PATH'), then run this again." }
}
$pyExe = $py[0]; $pyArgs = @($py | Select-Object -Skip 1) + "-I"

# ---- Microsoft's licence: asked first, so nothing is downloaded if you say no -----------------------------------
$msvcDone = Test-Path (Join-Path $Path "msvc\setup_x64.bat")
if (-not $msvcDone) {
    $licence = $LicenceFallback
    try {
        $ProgressPreference = "SilentlyContinue"
        $ch = Invoke-RestMethod -Uri $Channel -UseBasicParsing -ErrorAction Stop
        $bt = $ch.channelItems | Where-Object { $_.id -eq "Microsoft.VisualStudio.Product.BuildTools" } | Select-Object -First 1
        $l = ($bt.localizedResources | Where-Object { $_.language -eq "en-us" } | Select-Object -First 1).license
        if ($l) { $licence = $l }
    } catch { }
    Write-Host ""
    Write-Host "Microsoft's C/C++ libraries and the Windows SDK are Microsoft's, under the licence of the"
    Write-Host "Visual Studio 2022 Build Tools:"
    Write-Host ""
    Write-Host "    $licence" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Please open that link and read it. Only you can accept it; this script never does it for you."
    $a = Read-Host "Type yes to accept Microsoft's licence and go on (anything else stops here)"
    if ("$a".Trim() -ne "yes") {
        Write-Host ""
        Write-Host "Stopped; nothing was downloaded. Without Microsoft's libraries the game can't be built here." -ForegroundColor Yellow
        Write-Host "The other way is Visual Studio (README.md, Requirements)." -ForegroundColor Yellow
        exit 1
    }
}

# ---- Downloads, each checked against its SHA-256 -----------------------------------------------------------------
function Get-Pinned([string]$key) {
    $p = $Pins[$key]
    New-Item -ItemType Directory -Force $dl | Out-Null
    $file = Join-Path $dl $p.File
    if ((Test-Path -LiteralPath $file) -and (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -eq $p.Sha256) { return $file }
    Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue
    Write-Host "            downloading $($p.Name) ($($p.MB) MB)..."
    if (Get-Command curl.exe -ErrorAction SilentlyContinue) {
        & curl.exe -L --fail --retry 3 --progress-bar -o $file $p.Url
    } else {
        try {
            $ProgressPreference = "SilentlyContinue"
            Invoke-WebRequest -Uri $p.Url -OutFile $file -UseBasicParsing -ErrorAction Stop
        } catch { }
    }
    if (-not (Test-Path -LiteralPath $file)) { Stop-Here "downloading $($p.Name) failed. Check the internet connection and run get-toolchain.ps1 again." }
    $h = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
    if ($h -ne $p.Sha256) {
        Remove-Item -LiteralPath $file -Force
        Stop-Here "the $($p.Name) download isn't the expected file (SHA-256 $h). It was deleted. Run get-toolchain.ps1 again, and report it if it happens again."
    }
    return $file
}
# Unpacks into <name>.part and renames it when complete, so a folder that's there is a finished one.
function New-Part([string]$name) {
    foreach ($d in "$name.part", $name) { Remove-Item -LiteralPath (Join-Path $Path $d) -Recurse -Force -ErrorAction SilentlyContinue }
    return (Join-Path $Path "$name.part")
}
Write-Host ""

# ---- LLVM: clang-cl, lld-link, llvm-lib, clang-scan-deps, the runtime DLLs they use and clang's own headers --------
if (-not (Test-Path (Join-Path $Path "llvm\bin\clang-cl.exe"))) {
    $tar = Get-Pinned "llvm"
    $part = New-Part "llvm"
    Write-Host "            unpacking the parts of LLVM the build uses (a minute or two)..."
    $helper = Join-Path $dl "unpack_llvm.py"
    Set-Content -LiteralPath $helper -Encoding ASCII -Value @'
import sys, tarfile
src, dest = sys.argv[1], sys.argv[2]
tools = {"clang-cl.exe", "lld-link.exe", "llvm-lib.exe", "clang-scan-deps.exe"}
dlls = ("msvcp140", "vcruntime140", "concrt140", "ucrtbase", "api-ms-win-")
def wanted(parts):
    if len(parts) == 2 and parts[0] == "bin":
        n = parts[1].lower()
        return n in tools or (n.endswith(".dll") and n.startswith(dlls))
    return len(parts) >= 3 and parts[0] == "lib" and parts[1] == "clang"
with tarfile.open(src, "r:xz") as tf:
    for m in tf:
        parts = m.name.split("/")[1:]          # without the archive's top folder
        if not (m.isfile() or m.isdir()) or not parts or not wanted(parts):
            continue
        m.name = "/".join(parts)
        if hasattr(tarfile, "data_filter"):
            tf.extract(m, dest, filter="data")
        else:
            tf.extract(m, dest)
'@
    & $pyExe @pyArgs $helper $tar $part
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path (Join-Path $part "bin\clang-cl.exe")) -or -not (Test-Path (Join-Path $part "lib\clang"))) {
        Stop-Here "unpacking LLVM failed (see above). Run get-toolchain.ps1 again."
    }
    Rename-Item -LiteralPath $part "llvm"
    Remove-Item -LiteralPath $tar, $helper -Force
}
Ok "LLVM 19.1.5 (clang-cl, lld-link)"

# ---- CMake and Ninja ----------------------------------------------------------------------------------------------
if (-not (Test-Path (Join-Path $Path "cmake\bin\cmake.exe"))) {
    $zip = Get-Pinned "cmake"
    $part = New-Part "cmake"
    Expand-Archive -LiteralPath $zip -DestinationPath $part -Force
    $inner = Join-Path $part "cmake-3.31.6-windows-x86_64"
    if (-not (Test-Path (Join-Path $inner "bin\cmake.exe"))) { Stop-Here "unpacking CMake failed. Run get-toolchain.ps1 again." }
    Move-Item -LiteralPath $inner (Join-Path $Path "cmake")
    Remove-Item -LiteralPath $part -Recurse -Force
    Remove-Item -LiteralPath $zip -Force
}
Ok "CMake 3.31.6"
if (-not (Test-Path (Join-Path $Path "ninja\ninja.exe"))) {
    $zip = Get-Pinned "ninja"
    $part = New-Part "ninja"
    Expand-Archive -LiteralPath $zip -DestinationPath $part -Force
    if (-not (Test-Path (Join-Path $part "ninja.exe"))) { Stop-Here "unpacking Ninja failed. Run get-toolchain.ps1 again." }
    Rename-Item -LiteralPath $part "ninja"
    Remove-Item -LiteralPath $zip -Force
}
Ok "Ninja 1.12.1"

# ---- Microsoft's libraries and the Windows SDK, with portable-msvc.py ------------------------------------------------
if (-not $msvcDone) {
    $script = Get-Pinned "pmsvc"
    Remove-Item -LiteralPath (Join-Path $Path "msvc") -Recurse -Force -ErrorAction SilentlyContinue   # an unfinished earlier try
    Write-Host "            running portable-msvc.py $($MsvcArgs -join ' ') --accept-license"
    Write-Host "            (--accept-license because you typed yes above; it fetches from Microsoft, 5-15 minutes)"
    Push-Location -LiteralPath $Path
    & $pyExe @pyArgs $script @MsvcArgs --accept-license
    $code = $LASTEXITCODE
    Pop-Location
    if ($code -ne 0 -or -not (Test-Path (Join-Path $Path "msvc\setup_x64.bat"))) {
        Stop-Here "portable-msvc.py didn't finish (see above). Run get-toolchain.ps1 again: what's downloaded is kept."
    }
}
$msvcDir = Get-ChildItem -LiteralPath (Join-Path $Path "msvc\VC\Tools\MSVC") -Directory -ErrorAction SilentlyContinue | Select-Object -Last 1
$kits = Join-Path $Path "msvc\Windows Kits\10"
$sdkDir = Get-ChildItem -LiteralPath (Join-Path $kits "Lib") -Directory -ErrorAction SilentlyContinue | Select-Object -Last 1
$missing = @()
if (-not $msvcDir -or -not (Test-Path (Join-Path $msvcDir.FullName "lib\x64\msvcrt.lib"))) { $missing += "MSVC libraries" }
if (-not $sdkDir -or -not (Test-Path (Join-Path $sdkDir.FullName "um\x64\kernel32.lib"))) { $missing += "Windows SDK libraries" }
elseif (-not (Test-Path (Join-Path $kits "bin\$($sdkDir.Name)\x64\rc.exe")) -or -not (Test-Path (Join-Path $kits "bin\$($sdkDir.Name)\x64\mt.exe"))) { $missing += "the SDK's rc.exe and mt.exe" }
if ($missing) {
    Remove-Item -LiteralPath (Join-Path $Path "msvc\setup_x64.bat") -Force -ErrorAction SilentlyContinue
    Stop-Here "the msvc folder has no $($missing -join ', '). Run get-toolchain.ps1 again to fetch it anew."
}
Ok "MSVC $($msvcDir.Name) and Windows SDK $($sdkDir.Name)"

# ---- Done: the record build.cmd looks for, and where the folder is -----------------------------------------------
Set-Content -LiteralPath (Join-Path $Path "gt5-toolchain.txt") -Encoding ASCII -Value @(
    "Compiler tools for GT5 Recomp, made by get-toolchain.ps1 on $(Get-Date -Format yyyy-MM-dd).",
    "LLVM 19.1.5, CMake 3.31.6, Ninja 1.12.1, MSVC $($msvcDir.Name), Windows SDK $($sdkDir.Name).",
    "GT5\scripts\build.cmd uses them when there's no Visual Studio with the Clang tools. Delete this folder to remove them.")
# build.cmd reads this with set /p, which takes the console's code page.
New-Item -ItemType Directory -Force (Join-Path $base ".gt5") | Out-Null
[IO.File]::WriteAllBytes((Join-Path $base ".gt5\toolchain-path.txt"), [Console]::OutputEncoding.GetBytes($Path))
Remove-Item -LiteralPath $dl -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
$total = 0
foreach ($d in "llvm", "cmake", "ninja", "msvc") {
    $mb = Get-SizeMB (Join-Path $Path $d); $total += $mb
    Write-Host ("  {0,-6} {1,6} MB" -f $d, $mb)
}
Write-Host ("  total  {0,6} MB in {1}" -f $total, $Path)
Write-Host ""
Write-Host "Done. Run setup.ps1 again (or GT5\scripts\build.cmd): it builds with these tools when there's no Visual Studio." -ForegroundColor Green
Write-Host "To remove them later, delete $Path." -ForegroundColor Green
