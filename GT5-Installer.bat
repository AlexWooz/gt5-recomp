<# : GT5-Installer.bat
@echo off
rem GT5 Recomp - installer. Double-click it. A window asks for your own copy of GT5 and the PS3
rem system software file, then installs everything else and builds the game, showing how far it has got.
rem Run it again any time to update, or to carry on if something stopped it: it skips what's already done.
setlocal
set "GT5_INSTALLER=%~f0"
set "GT5_INSTALLER_ROLE="
if not defined GT5_INSTALL_NOGUI (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "iex ([IO.File]::ReadAllText($env:GT5_INSTALLER))"
    if not errorlevel 1 goto :eof
)
rem No window: GT5_INSTALL_NOGUI=1 (testing), or this PC can't show one. Everything runs in this console.
title GT5 Recomp - installer
set "GT5_INSTALLER_ROLE=console"
powershell -NoProfile -ExecutionPolicy Bypass -Command "iex ([IO.File]::ReadAllText($env:GT5_INSTALLER))"
echo.
pause
goto :eof
: The rest is PowerShell. #>

# =====================================================================================================
# Gran Turismo 5 for PC installer
#
# What it does, in the install folder you choose (each step skips what is already done, so running it
# again carries on after a stop, and on an existing install it updates and rebuilds):
#   1. checks the PC: the compiler (a Visual Studio with the Clang tools is used as it is; otherwise the
#      compiler tools are fetched in step 3) and Git (the PC's own, else Git for Windows' portable MinGit in
#      the install folder's tools\git). Parts that come under another company's licence (Microsoft's C++
#      libraries in the compiler tools; NVIDIA DLSS on a PC with an NVIDIA RTX card) are named, with links to
#      their licences, in one line by the Install button: pressing Install is the player's acceptance, and
#      only the parts that line named are fetched under it (.gt5\installer-licences.txt records them).
#   2. downloads the port's code from GitHub (code only: nothing from the game), or updates it
#   3. fetches the port's own Python (python.org's 3.11.9, 17 MB) into tools\python and, without Visual
#      Studio, the compiler tools (about 1.5 GB) into tools\toolchain. Nothing is installed in Windows, and
#      Visual Studio is never installed or changed.
#   4. installs the PS3 system software from Sony's PS3UPDAT.PUP into GT5\dev_flash
#   5. copies your game (a disc folder or an .iso) into GT5\vfs
#   6. unlocks (decrypts) the game's EBOOT.BIN into GT5\input\EBOOT.elf
#   7. and 8. runs setup.ps1, which generates the game code and builds gt5.exe with both renderers
#      (DirectX 12 and Vulkan) and GT5Launcher.exe, then adds AMD FSR 3 and, when the line named it, NVIDIA DLSS
# Steps 4 and 6 need Sony's keys, which this project never publishes: GT5\scripts\ps3_unlock.py fetches
# them from RPCS3's public source, and RPCS3 itself is fetched as a fallback tool only when that fails.
#
# How this file is put together. The batch lines above run this PowerShell code (Windows PowerShell 5.1)
# in one of four roles, from GT5_INSTALLER_ROLE:
#   ""        just double-clicked: starts the "ui" role in a PowerShell with no console window
#             (CREATE_NO_WINDOW), so the command prompt closes at once; exit code 2 when no window can
#             be shown, and the batch part then runs the "console" role.
#   "ui"      the window (Show-Window). It starts the work as a child "engine" and reads its report.
#   "engine"  the work (Invoke-Install), reporting as lines on its standard output (Send).
#   "console" the same work, reporting to the console (GT5_INSTALL_NOGUI=1).
# The engine only reports through the Report functions (Step-Start, Step-End, Set-Now, Set-Pct, Say,
# Note, Stop-Install); the window never does the work itself. What the player accepted with the Install
# button reaches the engine as GT5_INSTALL_ACCEPTED (the ids of the parts the line named, "dlss,msvc").
#
# Test switches (environment variables):
#   GT5_INSTALL_NOGUI=1        no window: the whole install in the console
#   GT5_INSTALL_GAME / _FIRMWARE / _DEST   the three choices (with the window: the fields' first values)
#   GT5_INSTALL_NOBUILD=1      stop before the game code and the build (setup.ps1 -NoBuild)
#   GT5_INSTALL_LNKDIR=<dir>   where the desktop shortcut goes
#   GT5_INSTALL_NOVS=1         act as if there were no Visual Studio (the compiler tools and their licence)
#   GT5_INSTALL_NORTX=1        act as if there were no NVIDIA RTX card (no DLSS, no NVIDIA in the licence line)
#   GT5_INSTALL_NOAVX2=1       act as if the processor had no AVX2 (the first page says the PC can't run the game)
#   GT5_INSTALL_RAMGB=<n>      act as if the PC had n GB of memory
#   GT5_INSTALL_ACCEPTED=<ids> console: what was accepted, without the question ("none", or "dlss,msvc")
#   GT5_INSTALL_NOGIT=1        act as if there were no Git (the portable MinGit in tools\git)
#   GT5_INSTALL_RPCS3=1        unlock with RPCS3 even when ps3_unlock.py could do it
#   GT5_INSTALL_NOTAR=1, GT5_INSTALL_NO7Z=1   RPCS3 unpacking: skip tar / the PC's 7-Zip
#   GT5_INSTALL_UISCALE=<k>    draw the window at that scale (1.5 = 150 %)
#   GT5_INSTALL_AUTOGO=1       the window fills in the three choices and presses Install itself
#   GT5_INSTALL_SNAP=<dir>     the window saves a picture of itself at each stage (and stays off screen)
#   GT5_INSTALL_TESTCANCEL=<s> the window cancels <s> seconds into the work, without asking
#   GT5_INSTALL_AUTOCLOSE=1    the window closes itself on the last page
# =====================================================================================================
$ErrorActionPreference = "Continue"
$ProgressPreference = "SilentlyContinue"
$Role = "$env:GT5_INSTALLER_ROLE"
if ($env:GT5_INSTALL_NOGUI -and $Role -ne "engine") { $Role = "console" }

# ---- Just double-clicked: the window, without a console ------------------------------------------------
if ($Role -eq "") {
    try {
        Add-Type -AssemblyName System.Windows.Forms -ErrorAction Stop
        $psi = New-Object Diagnostics.ProcessStartInfo (Join-Path $PSHOME "powershell.exe")
        $psi.Arguments = '-NoProfile -ExecutionPolicy Bypass -STA -Command "iex ([IO.File]::ReadAllText($env:GT5_INSTALLER))"'
        $psi.UseShellExecute = $false
        $psi.CreateNoWindow = $true
        $psi.EnvironmentVariables["GT5_INSTALLER_ROLE"] = "ui"
        [void][Diagnostics.Process]::Start($psi)
        exit 0
    } catch { exit 2 }
}

# ---- Shared: settings, the log, the game / firmware / folder checks --------------------------------------
# GitHub and the other download sites need TLS 1.2, which Windows PowerShell 5.1 doesn't always offer.
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
$RepoUrl   = "https://github.com/MormonFTP-hub/gt5-recomp.git"
$SonyPage  = "https://www.playstation.com/en-us/support/hardware/ps3/system-software/"
$HelpPage  = "https://github.com/MormonFTP-hub/gt5-recomp/issues/new?template=2-problem.yml"
$Supported = @{ "BCES00569" = "Europe"; "BCUS98114" = "North America" }
# Other GT5 releases, by their title in any language: "Gran Turismo 5" / "GT5", GT + "racing car" + 5
# (Hong Kong and China, traditional and simplified), "Gran Turismo" in katakana + 5 (Japan), the
# Taiwanese name + 5, the Korean name + 5. Never a Prologue (English, katakana, Chinese, Korean).
# The \u escapes keep this file plain ASCII.
$Gt5Title  = '(?i)((?<![a-z])gran\s*turismo|(?<![a-z])GT|\u30B0\u30E9\u30F3\u30C4\u30FC\u30EA\u30B9\u30E2|\u8DD1[\u8ECA\u8F66]\u6D6A\u6F2B\u65C5|\uADF8\uB780\s*\uD22C\uB9AC\uC2A4\uBAA8)[\s\u00AE\u2122:\-]*([\u8CFD\u8D5B][\u8ECA\u8F66])?[\s\u00AE\u2122]*[5\uFF15](?!\d)'
$Prologue  = '(?i)prologue|\u30D7\u30ED\u30ED\u30FC\u30B0|\u5E8F\u7AE0|\uD504\uB864\uB85C\uADF8'
$OtherIds  = @("BCAS20108", "BCAS20218")   # Asian releases players have tried to install
$RegKey    = "HKCU:\Software\GT5-PC-installer"
$NeedGB    = 25
$NeedUpdateGB = 5   # an existing install: the build's changed files and the code generated again
$NeedRamGB = 8
$StepTitles = @("Getting ready", "Downloading the code", "Build tools",
                "PS3 system software", "Your game files", "Preparing the game",
                "Converting the game", "Building the game")
$Total = $StepTitles.Count
$Utf8 = New-Object Text.UTF8Encoding($false)
# Git for Windows' portable MinGit, used when the PC has no Git. The same pin is in get-git.ps1.
$GitPin = @{ Version = "2.56.1"; Size = 39849768; File = "MinGit-2.56.1-64-bit.zip"
             Url = "https://github.com/git-for-windows/git/releases/download/v2.56.1.windows.1/MinGit-2.56.1-64-bit.zip"
             Sha256 = "4A92850B6E917DA4FD010669B614B55DCCF5B0EC333A534CC4BE4700DFEFDAFD"; Signer = "CN=Johannes Schindelin" }

# Everything the install shows also goes to GT5-Installer.log next to the installer (else in %TEMP%), for
# reports. With the window, the window writes it; in the console, the engine does.
$LogPath = Join-Path $env:TEMP "GT5-Installer.log"
if ($env:GT5_INSTALLER) {
    $p = Join-Path (Split-Path $env:GT5_INSTALLER) "GT5-Installer.log"
    try { [IO.File]::AppendAllText($p, ""); $LogPath = $p } catch {}
}
function Open-LogWriter {
    try {
        $fs = New-Object IO.FileStream($LogPath, [IO.FileMode]::Append, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
        $w = New-Object IO.StreamWriter($fs, $Utf8)
        $w.AutoFlush = $true
        return $w
    } catch { return $null }
}

# PARAM.SFO: the game's ID card (title and ID like BCES00569).
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

# The PS3_GAME folder inside whatever was picked: the disc folder, PS3_GAME itself, or a folder a
# level or two above it.
function Find-Ps3Game([string]$root) {
    if (-not $root -or -not (Test-Path -LiteralPath $root -PathType Container)) { return $null }
    foreach ($x in @($root, (Join-Path $root "PS3_GAME"))) {
        if ((Test-Path -LiteralPath (Join-Path $x "PARAM.SFO")) -and (Test-Path -LiteralPath (Join-Path $x "USRDIR"))) { return $x }
    }
    $hit = Get-ChildItem -LiteralPath $root -Directory -Recurse -Depth 3 -Filter "PS3_GAME" -ErrorAction SilentlyContinue |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "PARAM.SFO") } | Select-Object -First 1
    if ($hit) { return $hit.FullName }
    return $null
}

# Mounts an .iso: @{ Root = "F:\"; Ours } (Ours: this mounted it, so this dismounts it). An image that is
# already mounted (by the player, or a run that was cut short) is used where it is: Windows mounts the same
# image again under another drive letter, and one dismount takes away only one of them.
function Mount-Iso([string]$iso) {
    try {
        $img = Get-DiskImage -ImagePath $iso -ErrorAction Stop
        if ($img.Attached) {
            $v = @($img | Get-Volume -ErrorAction SilentlyContinue | Where-Object { $_.DriveLetter }) | Select-Object -First 1
            if ($v) { return @{ Root = "$($v.DriveLetter):\"; Ours = $false } }
        }
        $img = Mount-DiskImage -ImagePath $iso -PassThru -ErrorAction Stop
        $letter = $null
        for ($i = 0; $i -lt 20 -and -not $letter; $i++) {
            Start-Sleep -Milliseconds 300
            try { $letter = ($img | Get-Volume -ErrorAction Stop).DriveLetter } catch {}
        }
        if ($letter) { return @{ Root = "${letter}:\"; Ours = $true } }
        Dismount-Iso $iso
    } catch {}
    return $null
}
# A dismount right after the mount can be refused while Windows still looks at the new drive (it was, here,
# and left the image mounted): try for a few seconds, until the image is no longer mounted.
function Dismount-Iso([string]$iso) {
    for ($i = 0; $i -lt 12; $i++) {
        try { Dismount-DiskImage -ImagePath $iso -ErrorAction Stop | Out-Null } catch {}
        $att = $false
        try { $att = [bool](Get-DiskImage -ImagePath $iso -ErrorAction Stop).Attached } catch {}
        if (-not $att) { return }
        Start-Sleep -Milliseconds 400
    }
}
# What the picked game is: @{ Ok; Text; Ps3Game; TitleId; Iso }
function Get-GameInfo([string]$pick) {
    if (-not $pick) { return @{ Ok = $false; Text = "" } }
    $iso = $null; $folder = $pick
    if ($pick -match '\.iso$') {
        if (-not (Test-Path -LiteralPath $pick)) { return @{ Ok = $false; Text = "That file isn't there." } }
        $iso = $pick; $m = Mount-Iso $pick
        if ($m) { $folder = $m.Root }
        if (-not $m) { return @{ Ok = $false; Text = "Couldn't open this .iso. Double-click it in Explorer, then choose its drive with Folder... instead." } }
    }
    $g = Find-Ps3Game $folder
    $info = @{ Ok = $false; Iso = $iso; Ps3Game = $g }
    if (-not $g) {
        $info.Text = "No PS3_GAME folder with PARAM.SFO in there. Pick the folder of your disc dump (the one with PS3_GAME in it)."
    } else {
        $sfo = Read-Sfo (Join-Path $g "PARAM.SFO")
        $id = if ($sfo) { [string]$sfo["TITLE_ID"] } else { "" }
        $title = if ($sfo) { [string]$sfo["TITLE"] } else { "" }
        $appVer = if ($sfo) { [string]$sfo["APP_VER"] } else { "" }
        $info.TitleId = $id
        if (-not (Test-Path -LiteralPath (Join-Path $g "USRDIR\EBOOT.BIN"))) {
            $info.Text = "Found $id, but PS3_GAME\USRDIR\EBOOT.BIN is missing: the dump isn't complete."
        } elseif ($Supported.ContainsKey($id)) {
            $info.Ok = $true; $info.Text = "Found: $title, $($Supported[$id]) ($id)"
        } elseif ($title -notmatch $Prologue -and ($title -match $Gt5Title -or $OtherIds -contains $id)) {
            # Other releases: the Asian BCAS20108 runs (v0.1.18); the rest are let through with a warning.
            $info.Ok = $true
            if ($id -eq "BCAS20108") { $info.Text = "Found: $title ($id), the Hong Kong / Asian release. It runs (tested with disc 01.00 and update 2.01)." }
            else { $info.Text = "Found: $title ($id). Not tried yet (Europe, North America and Hong Kong/Asia work): it may work; please report how it goes." }
        } else {
            $info.Text = "That's $title ($id), not Gran Turismo 5."
        }
        # A copy with a game update built in (APP_VER 02.xx, the disc says 01.00): its EBOOT.BIN is the
        # update's. 2.01 runs (v0.1.22) but also needs the update's data, which the installer doesn't copy.
        $v = 0.0
        if ($info.Ok -and [double]::TryParse($appVer, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$v) -and $v -ge 2) {
            $info.Text = "Found: $title ($id), game version $appVer (an update is built in). Update 2.01 is tested, others aren't yet. The update's data (USRDIR, PDIPFS) must also go in GT5\hdd0: README, Game updates."
        }
    }
    if ($iso -and $m.Ours) { Dismount-Iso $iso }
    return $info
}

function Test-Pup([string]$f) {
    if (-not $f) { return "" }
    if (-not (Test-Path -LiteralPath $f -PathType Leaf)) { return "That file isn't there." }
    try {
        $s = [IO.File]::OpenRead($f); $h = New-Object byte[] 5; [void]$s.Read($h, 0, 5); $len = $s.Length; $s.Close()
    } catch { return "Couldn't read that file." }
    if ([Text.Encoding]::ASCII.GetString($h) -ne "SCEUF" -or $len -lt 100MB) { return "That isn't a PS3 system software file (PS3UPDAT.PUP)." }
    return "ok"
}

# Markers in the install folder's .gt5 (git ignores it): an install was started here (so a stopped one can
# carry on in a folder that is no longer empty), and a copy that was started but hasn't finished.
function Get-Mark([string]$base, [string]$name) { return (Join-Path $base ".gt5\installer-$name.txt") }
function Set-Mark([string]$base, [string]$name) {
    $f = Get-Mark $base $name
    New-Item -ItemType Directory -Force (Split-Path $f) | Out-Null
    [IO.File]::WriteAllText($f, (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))
}
function Clear-Mark([string]$base, [string]$name) { Remove-Item -LiteralPath (Get-Mark $base $name) -Force -ErrorAction SilentlyContinue }
function Test-Mark([string]$base, [string]$name) { return (Test-Path -LiteralPath (Get-Mark $base $name)) }

function Get-DestState([string]$d) {
    if (-not $d) { return @{ Ok = $false; Text = "Choose a folder." } }
    try { $full = [IO.Path]::GetFullPath($d) } catch { return @{ Ok = $false; Text = "That isn't a valid folder path." } }
    $root = [IO.Path]::GetPathRoot($full)
    if (-not (Test-Path -LiteralPath $root)) { return @{ Ok = $false; Text = "Drive $root isn't there." } }
    $drive = New-Object IO.DriveInfo($root)
    $free = [math]::Round($drive.AvailableFreeSpace / 1GB)
    $fs = ""; try { $fs = "$($drive.DriveFormat)" } catch {}
    $existing = (Test-Path -LiteralPath (Join-Path $full "setup.ps1")) -and (Test-Path -LiteralPath (Join-Path $full "GT5"))
    $resume = (-not $existing) -and (Test-Mark $full "started")
    $st = @{ Ok = $true; Full = $full; Existing = $existing; Resume = $resume; Free = $free
             HasFw = (Test-Path -LiteralPath (Join-Path $full "GT5\dev_flash\sys")) -and -not (Test-Mark $full "firmware-partial")
             HasGame = (Test-Path -LiteralPath (Join-Path $full "GT5\vfs\PS3_GAME\USRDIR")) -and -not (Test-Mark $full "game-partial") }
    # The space still needed: all of it for a new folder; an install that has the game's data (GT.VOL, 12 GB) needs
    # room for the build; an update rebuilds what changed.
    $need = if ($existing -and -not (Test-Mark $full "started")) { $NeedUpdateGB }
            elseif (Test-Path -LiteralPath (Join-Path $full "GT5\vfs\PS3_GAME\USRDIR\GT.VOL")) { $NeedGB - 15 }
            else { $NeedGB }
    $new = -not $existing -and -not $resume
    if ($new -and (Test-Path -LiteralPath $full) -and (Get-ChildItem -LiteralPath $full -Force | Select-Object -First 1)) {
        $st.Ok = $false; $st.Text = "That folder isn't empty. Choose an empty or new folder."
    } elseif ($new -and $fs -match '^FAT') {
        # FAT32 holds no file over 4 GB, and the game's data is one 12 GB file.
        $st.Ok = $false; $st.Text = "Drive $root can't hold the game's data. Choose another drive."
    } elseif ($new -and $full -match '[^\x20-\x7E]') {
        # The tools and the game read folder names in the PC's code page: letters outside it break them.
        $st.Ok = $false; $st.Text = "Choose a folder whose path has only English letters and numbers."
    } elseif ($free -lt $need) {
        $st.Ok = $false; $st.Text = "Only $free GB free on $root; about $need GB is needed."
    } elseif ($existing -and (Test-Mark $full "started")) { $st.Text = "An unfinished install is here: it carries on where it stopped. $free GB free." }
    elseif ($existing) { $st.Text = "Already installed here: it will be updated and rebuilt. $free GB free." }
    elseif ($resume) { $st.Text = "An unfinished install is here: it carries on where it stopped. $free GB free." }
    else { $st.Text = "$free GB free." }
    return $st
}

# Can this PC run the game at all? Asked before anything long. "" when it can, else a few plain words. The game is built
# for processors with AVX2 (GT5\CMakeLists.txt: -mavx2 -mfma ...; one without stops at start), and the first build needs
# memory. Test switches: GT5_INSTALL_NOAVX2=1, GT5_INSTALL_RAMGB=<n>.
function Get-PcProblem {
    if (-not ("Gt5Pc.Cpu" -as [type])) {
        try {
            Add-Type -Namespace Gt5Pc -Name Cpu -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern bool IsProcessorFeaturePresent(uint feature);
[DllImport("kernel32.dll")] public static extern ulong GetEnabledXStateFeatures();
[DllImport("kernel32.dll")] public static extern bool GetPhysicallyInstalledSystemMemory(out ulong kb);
'@
        } catch { return "" }
    }
    $avx2 = $true; $how = "unknown"
    try {
        # No AVX state in use at all: no AVX, so no AVX2 (the processors this stops: before 2013's, and AMD's before
        # Excavator). Where this Windows knows the AVX entries (39 = AVX), AVX2 (40) is asked directly; an older
        # Windows answers no to both, and then the processor is given the benefit of the doubt.
        if (([Gt5Pc.Cpu]::GetEnabledXStateFeatures() -band 4) -eq 0) { $avx2 = $false; $how = "no AVX" }
        elseif ([Gt5Pc.Cpu]::IsProcessorFeaturePresent(39)) { $avx2 = [Gt5Pc.Cpu]::IsProcessorFeaturePresent(40); $how = "AVX2 $avx2" }
    } catch {}
    if ($env:GT5_INSTALL_NOAVX2) { $avx2 = $false; $how = "GT5_INSTALL_NOAVX2" }
    $script:PcFacts = "processor: $how"
    if (-not $avx2) { return "This PC's processor is too old for the game." }
    $gb = 0
    try { $kb = [uint64]0; if ([Gt5Pc.Cpu]::GetPhysicallyInstalledSystemMemory([ref]$kb)) { $gb = [int][math]::Round($kb / 1MB) } } catch {}
    if ($env:GT5_INSTALL_RAMGB) { $gb = [int]$env:GT5_INSTALL_RAMGB }
    $script:PcFacts += ", memory: $gb GB"
    if ($gb -gt 0 -and $gb -lt $NeedRamGB) { return "This PC has $gb GB of memory; the game needs at least $NeedRamGB GB." }
    return ""
}

# Lock files a stopped git leaves in .git ("index.lock exists"), which make the next git refuse to work.
# The window removes them right after it stopped the work; the engine removes only old ones.
function Remove-GitLocks([string]$base, [int]$olderThanMin = 0) {
    if (-not $base) { return }
    $cut = (Get-Date).AddMinutes(-$olderThanMin)
    foreach ($repo in @($base, (Join-Path $base "ps3recomp"))) {
        $g = Join-Path $repo ".git"
        if (-not (Test-Path -LiteralPath $g -PathType Container)) { continue }
        Get-ChildItem -LiteralPath $g -Filter "*.lock" -File -Recurse -Depth 5 -Force -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -notmatch '\\objects\\' -and $_.LastWriteTime -lt $cut } |
            ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue }
    }
}

# Desktop shortcut, with the game's own icon from your disc. To the launcher (settings, play, updates).
function New-Shortcut([string]$base) {
    $icon = Join-Path $base "tools\gt5.ico"
    $png = Join-Path $base "GT5\vfs\PS3_GAME\ICON0.PNG"
    if ((Test-Path -LiteralPath $png) -and -not (Test-Path -LiteralPath $icon)) {
        try {
            Add-Type -AssemblyName System.Drawing
            New-Item -ItemType Directory -Force (Split-Path $icon) | Out-Null
            $src = [Drawing.Image]::FromFile($png); $bmp = New-Object Drawing.Bitmap 256, 256
            $g = [Drawing.Graphics]::FromImage($bmp); $g.InterpolationMode = "HighQualityBicubic"; $g.Clear([Drawing.Color]::Transparent)
            $h = [int](256 * $src.Height / $src.Width); $g.DrawImage($src, 0, [int]((256 - $h) / 2), 256, $h)
            $ms = New-Object IO.MemoryStream; $bmp.Save($ms, [Drawing.Imaging.ImageFormat]::Png); $data = $ms.ToArray(); $src.Dispose()
            $o = New-Object IO.MemoryStream; $w = New-Object IO.BinaryWriter($o)
            $w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]1)
            $w.Write([byte]0); $w.Write([byte]0); $w.Write([byte]0); $w.Write([byte]0)
            $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$data.Length); $w.Write([uint32]22); $w.Write($data)
            [IO.File]::WriteAllBytes($icon, $o.ToArray())
        } catch {}
    }
    $where = if ($env:GT5_INSTALL_LNKDIR) { $env:GT5_INSTALL_LNKDIR } else { [Environment]::GetFolderPath("Desktop") }
    $lnkPath = Join-Path $where "Gran Turismo 5 (PC).lnk"
    $ws = New-Object -ComObject WScript.Shell
    $l = $ws.CreateShortcut($lnkPath)
    $launcher = Join-Path $base "GT5Launcher.exe"   # settings, play and updates (built with the game)
    if (Test-Path -LiteralPath $launcher) { $l.TargetPath = $launcher; $l.WorkingDirectory = $base; $l.WindowStyle = 1 }
    else { $l.TargetPath = Join-Path $base "GT5\play.bat"; $l.WorkingDirectory = Join-Path $base "GT5"; $l.WindowStyle = 7 }
    $l.Description = "Gran Turismo 5 for PC"
    if (Test-Path -LiteralPath $icon) { $l.IconLocation = "$icon,0" }
    $l.Save()
    return $lnkPath
}

# =====================================================================================================
# The engine: the work, as steps that report only through the functions in this first part.
# =====================================================================================================
$script:Pipe = ($Role -eq "engine")
$script:LogW = $null
$script:CurStep = 0
$script:LastNow = $null; $script:LastHint = $null
$script:LastPct = -2.0; $script:LastPctT = 0; $script:LastDecile = -1
$script:Mounted = $null

# One report line for the window: "@@gt5<TAB>kind<TAB>field...". Anything else it reads is a detail line.
function Send([string]$kind, [string[]]$fields) {
    $clean = @($fields | ForEach-Object { ("$_" -replace "[\t\r\n]+", " ") })
    [Console]::Out.WriteLine("@@gt5`t$kind`t" + ($clean -join "`t"))
}
function Write-LogLine([string]$t) { if ($script:LogW) { try { $script:LogW.WriteLine($t) } catch {} } }
# A line of detail (the full text: the window's Details, the console, the log).
function Say([string]$t, [string]$c = "Gray") {
    if ($script:Pipe) {
        if ($t.StartsWith("@@gt5")) { $t = " " + $t }
        [Console]::Out.WriteLine($t)
        return
    }
    if ($c -eq "Gray") {
        if ($t -match '\[ok\]') { $c = "Green" } elseif ($t -match '\[missing\]|Trying (once more|again)') { $c = "Yellow" }
        elseif ($t -match '^(setup|update|get-\w+) stopped|^update: ') { $c = "Red" }
    }
    Write-Host $t -ForegroundColor $c
    Write-LogLine $t
}
# A step starts; $hint says roughly how long it takes.
function Step-Start([int]$n, [string]$hint) {
    $script:CurStep = $n; $script:LastNow = $null; $script:LastPct = -2.0; $script:LastDecile = -1
    if ($script:Pipe) { Send "step" @("$n", $hint); return }
    Write-Host ""
    Write-Host "[$n/$Total] $($StepTitles[$n - 1])" -ForegroundColor Cyan
    Write-LogLine ""; Write-LogLine "[$n/$Total] $($StepTitles[$n - 1])  ($(Get-Date -Format HH:mm:ss))"
}
# The step ended: "done", or "skip" (nothing to do), with a few words for the list.
function Step-End([string]$state, [string]$text) {
    if ($script:Pipe) { Send "end" @("$script:CurStep", $state, $text); return }
    Say "  $text"
}
# What is happening now, in plain words (and, optionally, how long it takes).
function Set-Now([string]$t, [string]$hint = "") {
    if ($t -eq $script:LastNow -and $hint -eq $script:LastHint) { return }
    $script:LastNow = $t; $script:LastHint = $hint; $script:LastPct = -2.0; $script:LastDecile = -1
    if ($script:Pipe) { Send "now" @($t, $hint); return }
    Say "  $t..."
}
# The figure for the bar: 0 to 100, or -1 for none (the bar only moves to show it's alive). $sub: the
# figure in words ("3.1 of 17.2 GB").
function Set-Pct([double]$p, [string]$sub = "") {
    if ($p -gt 100) { $p = 100 }
    if ($script:Pipe) {
        $p = [math]::Round($p, 1)
        $tick = [Environment]::TickCount
        if ($p -eq $script:LastPct -and $sub -eq $script:LastSub) { return }
        if ($p -ge 0 -and $script:LastPct -ge 0 -and [int]$p -eq [int]$script:LastPct -and ($tick - $script:LastPctT) -lt 250) { return }
        $script:LastPct = $p; $script:LastPctT = $tick; $script:LastSub = $sub
        Send "pct" @($p.ToString([Globalization.CultureInfo]::InvariantCulture), $sub)
        return
    }
    if ($p -lt 0) { return }
    $dec = [int][math]::Floor($p / 10)
    if ($dec -ne $script:LastDecile) { $script:LastDecile = $dec; Say ("  {0,3}%  {1}" -f [int]$p, $sub) "DarkGray" }
}
# Worth knowing, but not a stop: a retry, a fallback, a warning.
function Note([string]$t) {
    if ($script:Pipe) { Send "note" @($t); return }
    Say "  $t" "Yellow"
}
function Exit-Engine([int]$code) {
    if ($script:Mounted) { Dismount-Iso $script:Mounted; $script:Mounted = $null }
    if ($script:LogW) { try { $script:LogW.Dispose() } catch {} }
    exit $code
}
function Stop-Install([string]$t) {
    if ($script:Pipe) { Send "stop" @($t) }
    else {
        Write-Host ""
        Write-Host "Stopped: $t" -ForegroundColor Red
        Write-Host "Fix that and run GT5-Installer.bat again: it skips everything that's already done."
        Write-Host "Stuck? Report it here and attach the log ($LogPath): $HelpPage"
        Write-LogLine ""; Write-LogLine "Stopped: $t"
    }
    Exit-Engine 1
}
function Set-Mounted([string]$iso) {
    $script:Mounted = $iso
    if ($script:Pipe) { if ($iso) { Send "mount" @($iso) } else { Send "unmount" @() } }
}
# The console has no Install button: the same line, and Enter is the acceptance. True when accepted.
function Ask-Licences($parts) {
    Write-Host ""
    Write-Host "  $(Get-LicenceLine $parts)"
    foreach ($p in $parts) { Write-Host "    $($p.Name): $($p.Url)" -ForegroundColor Cyan }
    Write-Host ""
    $a = Read-Host "  Press Enter to accept them and install (type no to stop)"
    Write-LogLine "$(Get-LicenceLine $parts) Answered '$a'."
    return ("$a".Trim() -notmatch '^(?i)n')
}

# ---- Running programs: their output read line by line, nothing left waiting for an answer ----------------
function Join-Args([string[]]$list) {
    $out = foreach ($s in $list) {
        $s = "$s"
        if ($s -eq "") { '""' }
        elseif ($s -notmatch '[\s"]') { $s }
        else { '"' + (($s -replace '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1') + '"' }
    }
    return ($out -join " ")
}
# Runs a program with standard output and errors read as lines (both reach Say unless $OnLine returns
# $true for a line it has turned into progress) and standard input closed, so a question nobody can see
# gets an empty answer instead of waiting forever. $OnTick runs about twice a second. Returns the exit code.
function Invoke-Tool {
    param([string]$Exe, [string[]]$ArgList = @(), [string]$Dir = "", [scriptblock]$OnLine = $null,
          [scriptblock]$OnTick = $null, [switch]$Quiet)
    $itPsi = New-Object Diagnostics.ProcessStartInfo $Exe
    $itPsi.Arguments = Join-Args $ArgList
    if ($Dir) { $itPsi.WorkingDirectory = $Dir }
    $itPsi.UseShellExecute = $false
    $itPsi.RedirectStandardInput = $true; $itPsi.RedirectStandardOutput = $true; $itPsi.RedirectStandardError = $true
    $itPsi.StandardOutputEncoding = $Utf8; $itPsi.StandardErrorEncoding = $Utf8
    try { $itProc = [Diagnostics.Process]::Start($itPsi) }
    catch { Say "  couldn't start $Exe ($($_.Exception.Message))" "Red"; return -1 }
    try { $itProc.StandardInput.Close() } catch {}
    $itReaders = @($itProc.StandardOutput, $itProc.StandardError)
    $itTasks = @($itReaders[0].ReadLineAsync(), $itReaders[1].ReadLineAsync())
    $itOpen = 2; $itTick = [Environment]::TickCount; $itExitT = 0
    while ($itOpen -gt 0) {
        $itGot = $false
        for ($itI = 0; $itI -lt 2; $itI++) {
            $itT = $itTasks[$itI]
            if ($null -eq $itT -or -not $itT.IsCompleted) { continue }
            $itGot = $true
            $itLine = $null
            try { $itLine = $itT.Result } catch {}
            if ($null -eq $itLine) { $itTasks[$itI] = $null; $itOpen--; continue }
            $itTasks[$itI] = $itReaders[$itI].ReadLineAsync()
            $itUsed = $false
            if ($OnLine) { $itUsed = [bool](& $OnLine $itLine | Select-Object -Last 1) }
            if (-not $itUsed -and -not $Quiet -and $itLine.Trim()) { Say $itLine }
        }
        if ($OnTick -and ([Environment]::TickCount - $itTick) -ge 500) { $itTick = [Environment]::TickCount; & $OnTick | Out-Null }
        if (-not $itGot) {
            # A program it started may keep the pipes open after it ended: stop reading 5 s after the exit.
            if ($itProc.HasExited) {
                if ($itExitT -eq 0) { $itExitT = [Environment]::TickCount }
                elseif (([Environment]::TickCount - $itExitT) -gt 5000) { break }
            }
            Start-Sleep -Milliseconds 40
        }
    }
    $itProc.WaitForExit()
    $itCode = $itProc.ExitCode
    $itProc.Dispose()
    return $itCode
}
function Invoke-Ps([string]$script, [string[]]$a = @(), [string]$Dir = "", [scriptblock]$OnLine = $null, [scriptblock]$OnTick = $null) {
    return (Invoke-Tool (Join-Path $PSHOME "powershell.exe") (@("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $script) + $a) -Dir $Dir -OnLine $OnLine -OnTick $OnTick)
}
# A download with its size shown as it grows. True when the file is there.
function Get-Download([string]$url, [string]$file, [long]$size, [string]$what) {
    Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue
    $mb = [math]::Round($size / 1MB)
    Set-Now "Downloading $what$(if ($size -gt 0) { " ($mb MB)" })"
    $dl = @{ File = $file; Size = $size; Mb = $mb }
    $tick = {
        if ($dl.Size -gt 0) {
            $have = 0; try { $have = (New-Object IO.FileInfo($dl.File)).Length } catch {}
            Set-Pct (100.0 * $have / $dl.Size) ("{0:N0} of {1:N0} MB" -f ($have / 1MB), $dl.Mb)
        }
    }
    if (Get-Command curl.exe -ErrorAction SilentlyContinue) {
        $code = Invoke-Tool "curl.exe" @("-sSL", "--fail", "--retry", "3", "-o", $file, $url) -OnTick $tick
        if ($code -ne 0) { Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue }
    } else {
        Set-Pct -1
        try { Invoke-WebRequest -Uri $url -OutFile $file -UseBasicParsing -ErrorAction Stop } catch { Say "  $($_.Exception.Message)" }
    }
    return (Test-Path -LiteralPath $file)
}
function Get-Text([string]$url) {
    try { return [string](Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 60 -ErrorAction Stop).Content } catch { return "" }
}

# ---- Tools on the PC -----------------------------------------------------------------------------------
function Update-Path {
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
    $p = "$env:ProgramFiles\Git\cmd"
    if ((Test-Path $p) -and ($env:Path -notlike "*$p*")) { $env:Path += ";$p" }
}
$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
function Find-VsClang {
    # Both clang-cl and vcvars64.bat (the build calls it): a copy an uninstall left behind can keep
    # clang-cl alone, and the build then stopped with "The system cannot find the path specified" (#35).
    if (-not (Test-Path $VsWhere)) { return $null }
    foreach ($p in @(& $VsWhere -sort -products * -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath)) {
        if ($p -and (Test-Path (Join-Path $p "VC\Tools\Llvm\x64\bin\clang-cl.exe")) -and
            (Test-Path (Join-Path $p "VC\Auxiliary\Build\vcvars64.bat"))) { return $p }
    }
    return $null
}
function Test-WinSdk {
    # The Windows SDK (kernel32.lib and the other Windows libraries the build links with), shared by every
    # Visual Studio on the PC: one installed without it stopped with "could not open 'kernel32.lib'" (#35).
    foreach ($r in @("${env:ProgramFiles(x86)}\Windows Kits\10\Lib", "$env:ProgramFiles\Windows Kits\10\Lib")) {
        foreach ($d in @(Get-ChildItem -LiteralPath $r -Directory -ErrorAction SilentlyContinue)) {
            if (Test-Path (Join-Path $d.FullName "um\x64\kernel32.lib")) { return $true }
        }
    }
    return $false
}
# The compiler tools without Visual Studio, where GT5\scripts\build.cmd looks for them: the folder
# get-toolchain.ps1 recorded in .gt5\toolchain-path.txt, else "toolchain" next to the install folder.
function Find-Toolchain([string]$base) {
    $cands = @()
    $rec = Join-Path $base ".gt5\toolchain-path.txt"
    if (Test-Path -LiteralPath $rec) { $cands += [Console]::OutputEncoding.GetString([IO.File]::ReadAllBytes($rec)).Trim() }
    $cands += Join-Path $base "tools\toolchain"
    $cands += Join-Path (Split-Path $base -Parent) "toolchain"
    foreach ($c in $cands) { if ($c -and (Test-Path -LiteralPath (Join-Path $c "gt5-toolchain.txt"))) { return $c } }
    return $null
}
# ---- The licence line by the Install button -------------------------------------------------------------
# Parts this install downloads that come under another company's licence, named in one line by the Install
# button with links to their licences: pressing Install accepts them (the console asks once instead).
#   msvc  Microsoft's C++ libraries (and the Windows SDK) in the compiler tools, when those are fetched: no
#         Visual Studio with the Clang tools and a Windows SDK, and no tools fetched before. The link is
#         get-toolchain.ps1's own (Microsoft's link to the Visual Studio 2022 Build Tools licence).
#   dlss  NVIDIA DLSS, which setup.ps1 adds after the build on a PC with an NVIDIA RTX card (the install
#         doesn't have it yet). The tag is the one of $Pin in GT5\scripts\add_dlss.ps1.
$MsLicence   = "https://go.microsoft.com/fwlink/?LinkId=2179911"
$DlssLicence = "https://github.com/NVIDIA/DLSS/blob/v310.9.16/LICENSE.txt"
# An NVIDIA RTX card (DLSS runs on no other): the test add_dlss.ps1 and the launcher make too.
function Test-RtxCard {
    if ($env:GT5_INSTALL_NORTX) { return $false }
    return [bool](@(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue) | Where-Object { "$($_.Name)" -match 'NVIDIA' -and "$($_.Name)" -match '\bRTX\b' })
}
# $vsOk: a Visual Studio with the Clang tools and the Windows SDK is on this PC (Find-VsClang + Test-WinSdk);
# $rtx: Test-RtxCard. Both are asked by the caller, once (they take a moment).
function Get-LicenceParts([string]$base, [bool]$vsOk, [bool]$rtx) {
    $parts = @()
    $haveDlss = $base -and (Test-Path -LiteralPath (Join-Path $base "GT5\build\gt5_upscale_dlss.dll"))
    if ($rtx -and -not $haveDlss) { $parts += @{ Id = "dlss"; Name = "NVIDIA DLSS"; Url = $DlssLicence } }
    $haveTc = $base -and (Find-Toolchain $base)
    if (-not $vsOk -and -not $haveTc) { $parts += @{ Id = "msvc"; Name = "Microsoft's C++ libraries"; Url = $MsLicence } }
    return , $parts
}
function Get-LicenceLine($parts) {
    if (-not $parts -or $parts.Count -eq 0) { return "" }
    return "By installing you accept the licences of the parts it downloads: " + (($parts | ForEach-Object { $_.Name }) -join ", ")
}
# .gt5\installer-licences.txt: one line per part whose licence the player accepted ("dlss <link> <date>").
# setup.ps1 and update.ps1 add NVIDIA DLSS (add_dlss.ps1 -Accept) only when it names dlss.
function Write-LicenceMark([string]$base, $parts) {
    if (-not $parts -or $parts.Count -eq 0) { return }
    $f = Join-Path $base ".gt5\installer-licences.txt"
    New-Item -ItemType Directory -Force (Split-Path $f) | Out-Null
    $keep = @(Get-Content -LiteralPath $f -ErrorAction SilentlyContinue | Where-Object { $l = $_; -not ($parts | Where-Object { $l -match "^$($_.Id)\b" }) })
    $new = @($parts | ForEach-Object { "$($_.Id) $($_.Url) accepted with the installer's Install button $(Get-Date -Format 'yyyy-MM-dd HH:mm')" })
    [IO.File]::WriteAllLines($f, [string[]]($keep + $new))
}
# Git for Windows' MinGit (the official portable build, checked by SHA-256 and signature) into $dir.
# Nothing is installed in Windows; setup.ps1, update.ps1 and the launcher's updates use it from there.
function Install-PortableGit([string]$dir) {
    $work = "$dir.tmp"
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $work | Out-Null
    $zip = Join-Path $work $GitPin.File
    if (-not (Get-Download $GitPin.Url $zip $GitPin.Size "Git $($GitPin.Version) (MinGit, Git for Windows' portable build)")) {
        Stop-Install "downloading Git (MinGit, from github.com/git-for-windows) failed. Check the internet connection and press Try again, or install Git yourself (https://git-scm.com)."
    }
    $h = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
    if ($h -ne $GitPin.Sha256) {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
        Stop-Install "the Git download isn't the expected file (SHA-256 $h). It was deleted. Press Try again, and report it if it happens again."
    }
    Set-Now "Setting up"; Set-Pct -1
    try {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [IO.Compression.ZipFile]::ExtractToDirectory($zip, (Join-Path $work "x"))
    } catch { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue; Stop-Install "unpacking Git failed ($($_.Exception.Message))." }
    $exe = Join-Path $work "x\cmd\git.exe"
    $sig = if (Test-Path -LiteralPath $exe) { Get-AuthenticodeSignature -LiteralPath $exe } else { $null }
    if (-not $sig -or $sig.Status -ne "Valid" -or "$($sig.SignerCertificate.Subject)" -notmatch [regex]::Escape($GitPin.Signer)) {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
        Stop-Install "the unpacked Git isn't signed by Git for Windows' maintainer. Nothing was kept; please report it."
    }
    Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
    Move-Item -LiteralPath (Join-Path $work "x") -Destination $dir
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}

# ---- Git progress ("Receiving objects:  45% (...)") into the bar -------------------------------------------
function Read-GitProgress([string]$l) {
    $final = $l -match 'done\.?\s*$'
    if ($l -match '^(remote: )?(Enumerating|Counting|Compressing) objects:') { Set-Pct -1 "GitHub is preparing it"; return (-not $final) }
    if ($l -match '^Receiving objects:\s+(\d+)%(?:.*?,\s*([\d.]+ [KMG]iB))?') {
        $sub = if ($Matches[2]) { "$($Matches[2])" } else { "" }
        Set-Pct (0.9 * [int]$Matches[1]) $sub; return (-not $final)
    }
    if ($l -match '^Resolving deltas:\s+(\d+)%') { Set-Pct (90 + 0.1 * [int]$Matches[1]) "checking it"; return (-not $final) }
    if ($l -match '^Updating files:\s+(\d+)%') { Set-Pct ([int]$Matches[1]) "unpacking"; return (-not $final) }
    if ($l -match '^ \* \[new (tag|branch)\]') { return $true }   # one line per version: only noise here
    return $false
}
# A new install: the code from GitHub into the folder. Not "git clone", which wants an empty folder: the
# folder can already hold tools\git (the Git this uses) or a download a stop cut short, and these steps
# carry on from either.
function Get-Code([string]$base) {
    $git = $script:GitExe
    Remove-GitLocks $base
    if (-not (Test-Path -LiteralPath (Join-Path $base ".git"))) {
        if ((Invoke-Tool $git @("-C", $base, "init", "--quiet")) -ne 0) { Stop-Install "Git couldn't start a repository in $base (see Details)." }
    }
    if ((Invoke-Tool $git @("-C", $base, "remote", "add", "origin", $RepoUrl) -Quiet) -ne 0) {
        [void](Invoke-Tool $git @("-C", $base, "remote", "set-url", "origin", $RepoUrl) -Quiet)
    }
    Set-Now "Downloading the code"
    $code = Invoke-Tool $git @("-C", $base, "fetch", "--progress", "--tags", "origin") -OnLine { param($l) Read-GitProgress $l }
    if ($code -ne 0) {
        # A Git whose own certificate file is damaged or missing ("error adding trust anchors from file",
        # #46) still reaches GitHub through Windows' certificate store. The setting stays in this install's
        # repository, so updates use it too; it is taken out again if that doesn't help either.
        Say "  Trying again with Windows' certificate store (http.sslBackend=schannel)."
        [void](Invoke-Tool $git @("-C", $base, "config", "http.sslBackend", "schannel") -Quiet)
        $code = Invoke-Tool $git @("-C", $base, "fetch", "--progress", "--tags", "origin") -OnLine { param($l) Read-GitProgress $l }
        if ($code -ne 0) { [void](Invoke-Tool $git @("-C", $base, "config", "--unset", "http.sslBackend") -Quiet) }
    }
    if ($code -ne 0) { Stop-Install "downloading the code from GitHub failed (is the internet connection up?)." }
    Set-Now "Unpacking the code"; Set-Pct -1
    $code = Invoke-Tool $git @("-C", $base, "checkout", "--progress", "-f", "-B", "main", "--track", "origin/main") -OnLine { param($l) Read-GitProgress $l }
    [void](Invoke-Tool $git @("-C", $base, "remote", "set-head", "origin", "main") -Quiet)
    if ($code -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $base "setup.ps1"))) { Stop-Install "unpacking the code from GitHub failed (see Details)." }
}

# =====================================================================================================
# Unlocking Sony's files. The game's EBOOT.BIN and Sony's firmware file are encrypted with Sony's keys,
# which this project never publishes. Everything that knows how is in this section, behind two functions:
#   Unlock-Firmware <PS3UPDAT.PUP> <dev_flash folder>   puts the system files from Sony's update file into
#                                                       that folder (sys\external, ... as on a PS3)
#   Unlock-Eboot <EBOOT.BIN> <EBOOT.elf>                writes the decrypted executable
# Each returns $true when its result is there and checked, $false when no way worked, and stops the
# install (Stop-Install) when the player's file itself is wrong. They report through Set-Now / Set-Pct /
# Say / Note only.
# First choice: GT5\scripts\ps3_unlock.py on the port's own Python (it fetches the key tables from RPCS3's
# public source at a pinned commit, checked by SHA-256). Fallback: RPCS3, downloaded only then and used
# only as a tool, its windows kept hidden.
# =====================================================================================================
$script:Base = $null; $script:Tools = $null; $script:Rpcs3 = $null

# ps3_unlock.py: @{ Code; Line } (Code 98: switched off by GT5_INSTALL_RPCS3, 99: not in this copy)
function Invoke-Ps3Unlock([string[]]$a, [string]$now, [string]$hint, [scriptblock]$tick = $null) {
    $u = @{ Code = 0; Line = "" }
    if ($env:GT5_INSTALL_RPCS3) { $u.Code = 98; return $u }
    $py = Join-Path $script:Tools "python\python.exe"
    $tool = Join-Path $script:Base "GT5\scripts\ps3_unlock.py"
    if (-not (Test-Path -LiteralPath $py) -or -not (Test-Path -LiteralPath $tool)) { $u.Code = 99; return $u }
    Set-Now $now $hint
    $u.Code = Invoke-Tool $py (@("-I", $tool) + $a + @("--keys-dir", (Join-Path $script:Tools "ps3_keys"))) -OnLine {
        param($l)
        if ($l -match '^PS3_UNLOCK_FAIL\s+\d+\s+\w+:\s*(.*)$') { $u.Line = $Matches[1].Trim() }
        $false
    } -OnTick $tick
    return $u
}
function Write-Fallback($u, [string]$what) {
    if ($u.Code -eq 98) { Say "  Unlocking $what with RPCS3 (test switch GT5_INSTALL_RPCS3)." }
    elseif ($u.Code -eq 99) { Say "  This copy of the code can't unlock $what by itself: RPCS3 does it instead." }
    else { Say "  Unlocking $what directly didn't work ($(if ($u.Line) { $u.Line } else { "code $($u.Code)" })): RPCS3 does it instead." }
}

function Unlock-Firmware([string]$pup, [string]$devFlash) {
    # ps3_unlock.py unpacks into <dev_flash>.part first: its file count is the figure (1075 for 4.92).
    $fw = @{ Part = "$devFlash.part"; T = 0 }
    $u = Invoke-Ps3Unlock @("firmware", $pup, $devFlash) "Unpacking Sony's system software file" "Under a minute on most PCs (it writes about 1,000 files)." {
        if (([Environment]::TickCount - $fw.T) -lt 1500) { return }
        $fw.T = [Environment]::TickCount
        $n = 0; try { $n = @([IO.Directory]::EnumerateFiles($fw.Part, "*", [IO.SearchOption]::AllDirectories)).Count } catch {}
        if ($n -gt 0) { Set-Pct ([math]::Min(99, 100.0 * $n / 1075)) "$n system files" }
    }
    if ($u.Code -eq 0 -and (Test-Path -LiteralPath (Join-Path $devFlash "sys\external"))) { return $true }
    if ($u.Code -eq 3) { Stop-Install "Sony's system software file can't be used: $($u.Line). Choose an undamaged PS3UPDAT.PUP (Get it from Sony) and press Try again." }
    Write-Fallback $u "the system software"
    $rp = Get-Rpcs3
    $flash = Join-Path (Split-Path $rp) "dev_flash"
    $okMark = Join-Path (Split-Path $rp) "gt5-firmware-installed.txt"
    if (-not (Test-Path -LiteralPath $okMark)) {
        # A half-finished install in RPCS3's folder would make it ask about "old firmware": start clean.
        Remove-Item -LiteralPath $flash -Recurse -Force -ErrorAction SilentlyContinue
        Set-Now "Installing the PS3 system software" "A few minutes."
        if (-not (Install-Rpcs3Firmware $rp $pup)) { return $false }
        [IO.File]::WriteAllText($okMark, (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))
    }
    Set-Now "Installing the PS3 system software"; Set-Pct -1
    $code = Invoke-Tool "robocopy.exe" @($flash, $devFlash, "/E", "/R:1", "/W:1", "/NFL", "/NDL", "/NJH", "/NJS", "/NP") -Quiet
    return ($code -lt 8 -and (Test-Path -LiteralPath (Join-Path $devFlash "sys\external")))
}

function Unlock-Eboot([string]$bin, [string]$elf) {
    $u = Invoke-Ps3Unlock @("eboot", $bin, $elf) "Unlocking EBOOT.BIN" "A few seconds."
    if ($u.Code -eq 0 -and (Test-ElfFile $elf)) { return $true }
    if ($u.Code -eq 3) { Stop-Install "unlocking EBOOT.BIN failed: $($u.Line). If your game is an encrypted disc image (a 'Redump' .iso), decrypt the image first or use a folder dump of the disc." }
    Write-Fallback $u "EBOOT.BIN"
    $rp = Get-Rpcs3
    $work = Join-Path (Split-Path (Split-Path $rp)) "decrypt"
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $work | Out-Null
    Copy-Item -LiteralPath $bin (Join-Path $work "EBOOT.BIN")
    Set-Now "Preparing the game" "Under a minute."
    $p = Start-Rpcs3 $rp @("--no-gui", "--decrypt", "`"$(Join-Path $work 'EBOOT.BIN')`"")
    if (-not $p.WaitForExit(180000)) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    Stop-Rpcs3Watch
    $out = Join-Path $work "EBOOT.elf"
    $ok = Test-ElfFile $out
    if ($ok) { Move-Item -LiteralPath $out $elf -Force }
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    return $ok
}
function Test-ElfFile([string]$f) {
    if (-not (Test-Path -LiteralPath $f)) { return $false }
    try { $s = [IO.File]::OpenRead($f); $b = New-Object byte[] 4; [void]$s.Read($b, 0, 4); $s.Close() } catch { return $false }
    return ($b[0] -eq 0x7F -and $b[1] -eq 0x45 -and $b[2] -eq 0x4C -and $b[3] -eq 0x46)
}

# RPCS3 into tools\rpcs3, once. It is unpacked into rpcs3.part and renamed when complete, so a folder that
# is there is a finished one.
function Get-Rpcs3 {
    if ($script:Rpcs3) { return $script:Rpcs3 }
    $dir = Join-Path $script:Tools "rpcs3"
    $exe = Join-Path $dir "rpcs3.exe"
    if (Test-Path -LiteralPath $exe) { $script:Rpcs3 = $exe; return $exe }
    Set-Now "Getting a helper tool"; Set-Pct -1
    try { $rel = Invoke-RestMethod "https://api.github.com/repos/RPCS3/rpcs3-binaries-win/releases/latest" -Headers @{ "User-Agent" = "gt5-pc-installer" } -TimeoutSec 60 }
    catch { Stop-Install "couldn't reach GitHub to download RPCS3 ($($_.Exception.Message))." }
    $asset = $rel.assets | Where-Object { $_.name -like "*win64*.7z" } | Select-Object -First 1
    if (-not $asset) { Stop-Install "couldn't find RPCS3's Windows download." }
    $sum = $rel.assets | Where-Object { $_.name -eq "$($asset.name).sha256" } | Select-Object -First 1
    $part = "$dir.part"
    Remove-Item -LiteralPath $part -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $part | Out-Null
    $arc = Join-Path $part $asset.name
    if (-not (Get-Download $asset.browser_download_url $arc ([long]$asset.size) "RPCS3 ($($asset.name))")) {
        Stop-Install "downloading RPCS3 failed. Check the internet connection and press Try again."
    }
    if ($sum) {
        $want = ((Get-Text $sum.browser_download_url) -split '\s+')[0]
        if ($want -and ((Get-FileHash -LiteralPath $arc -Algorithm SHA256).Hash -ne $want.ToUpper())) {
            Remove-Item -LiteralPath $part -Recurse -Force -ErrorAction SilentlyContinue
            Stop-Install "the RPCS3 download was damaged (checksum). Press Try again."
        }
    }
    Set-Now "Setting up a helper tool"; Set-Pct -1
    if (-not $env:GT5_INSTALL_NOTAR) { [void](Invoke-Tool "tar.exe" @("-xf", $arc, "-C", $part) -Quiet) }
    if (-not (Test-Path -LiteralPath (Join-Path $part "rpcs3.exe"))) {
        # Windows 10's tar can't read this archive (no LZMA): use 7-Zip, or 7-Zip's own small extractor.
        $sz = @("$env:ProgramFiles\7-Zip\7z.exe", "${env:ProgramFiles(x86)}\7-Zip\7z.exe") | Where-Object { (Test-Path $_) -and -not $env:GT5_INSTALL_NO7Z } | Select-Object -First 1
        if ($sz) { Say "  unpacking it with your 7-Zip" } else {
            $sz = Join-Path $script:Tools "7zr.exe"
            if (-not (Test-Path -LiteralPath $sz)) {
                Say "  getting 7-Zip's extractor (7zr.exe) from 7-zip.org to unpack it"
                [void](Get-Download "https://www.7-zip.org/a/7zr.exe" $sz 0 "7-Zip's extractor (7zr.exe)")
            }
        }
        [void](Invoke-Tool $sz @("x", "-y", "-o$part", $arc) -Quiet)
    }
    Remove-Item -LiteralPath $arc -ErrorAction SilentlyContinue
    if (-not (Test-Path -LiteralPath (Join-Path $part "rpcs3.exe"))) {
        Remove-Item -LiteralPath $part -Recurse -Force -ErrorAction SilentlyContinue
        Stop-Install "unpacking RPCS3 failed. Install 7-Zip from https://www.7-zip.org and run this again."
    }
    # No welcome box, no update check, no pop-ups: it's only used from here.
    New-Item -ItemType Directory -Force (Join-Path $part "GuiConfigs") | Out-Null
    $ini = "[main_window]`r`ninfoBoxEnabledWelcome=false`r`ninfoBoxEnabledInstallPUP=false`r`ninfoBoxEnabledInstallPKG=false`r`n[Meta]`r`ncheckUpdateStart=false`r`n"
    [IO.File]::WriteAllText((Join-Path $part "GuiConfigs\CurrentSettings.ini"), $ini)
    Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
    Move-Item -LiteralPath $part -Destination $dir
    $script:Rpcs3 = $exe
    return $exe
}

# RPCS3 started hidden, with a watcher that hides any window of it the moment it shows (it shows its
# question boxes, a progress box and, after a firmware install, an empty game window) and presses Yes in
# its "RPCS3 Firmware Installer" questions through UI Automation. RPCS3 has no way to install firmware
# without its window ("Cannot perform installation in no-gui mode!"), and a separate desktop doesn't work:
# UI Automation can't reach it and Qt ignores keys posted to an inactive window.
function Start-Rpcs3([string]$exe, [string[]]$a) {
    if (-not ("Gt5Inst.Rpcs3Watch" -as [type])) {
        Add-Type -ReferencedAssemblies UIAutomationClient, UIAutomationTypes, WindowsBase -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Automation;
namespace Gt5Inst {
public static class Rpcs3Watch {
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h, int cmd);
    static volatile bool stop = true;
    static int pid;
    static Thread thread;
    public static int Pressed;
    public static readonly List<string> Seen = new List<string>();
    public static void Start(int processId) {
        Stop();
        pid = processId; Pressed = 0; stop = false;
        lock (Seen) Seen.Clear();
        thread = new Thread(Loop); thread.IsBackground = true; thread.Start();
    }
    public static void Stop() { stop = true; if (thread != null) { thread.Join(2000); thread = null; } }
    static void Loop() {
        var yes = new AndCondition(new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.Button),
                                   new PropertyCondition(AutomationElement.NameProperty, "Yes"));
        var tried = new Dictionary<IntPtr, int>();
        while (!stop) {
            var found = new List<IntPtr>();
            EnumWindows(delegate(IntPtr h, IntPtr l) { int p; GetWindowThreadProcessId(h, out p); if (p == pid) found.Add(h); return true; }, IntPtr.Zero);
            foreach (var h in found) {
                if (!IsWindowVisible(h)) continue;
                ShowWindow(h, 0);
                var t = new StringBuilder(256); GetWindowTextW(h, t, 256);
                string title = t.ToString();
                lock (Seen) Seen.Add(title);
                int n; tried.TryGetValue(h, out n);
                if (title == "RPCS3 Firmware Installer" && n < 3) {
                    tried[h] = n + 1;
                    try {
                        var b = AutomationElement.FromHandle(h).FindFirst(TreeScope.Descendants, yes);
                        if (b != null) { ((InvokePattern)b.GetCurrentPattern(InvokePattern.Pattern)).Invoke(); tried[h] = 9; Interlocked.Increment(ref Pressed); }
                    } catch { }
                }
            }
            Thread.Sleep(40);
        }
    }
}
}
'@
    }
    $p = Start-Process -FilePath $exe -ArgumentList $a -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
    [Gt5Inst.Rpcs3Watch]::Start($p.Id)
    return $p
}
function Stop-Rpcs3Watch { if ("Gt5Inst.Rpcs3Watch" -as [type]) { [Gt5Inst.Rpcs3Watch]::Stop() } }

# RPCS3's firmware install: answer its questions (the file, and the version when one is installed), then
# wait until all the files are there (1075 for 4.92) and stay that way, then end it.
function Install-Rpcs3Firmware([string]$rp, [string]$pup) {
    $flash = Join-Path (Split-Path $rp) "dev_flash"
    $p = Start-Rpcs3 $rp @("--installfw", "`"$pup`"")
    $start = [Environment]::TickCount; $last = -1; $stable = 0; $why = ""
    while ($true) {
        Start-Sleep -Seconds 2
        $sec = ([Environment]::TickCount - $start) / 1000
        if ($p.HasExited) { $why = "RPCS3 closed before it finished"; break }
        $n = (Get-ChildItem -LiteralPath $flash -Recurse -File -ErrorAction SilentlyContinue | Measure-Object).Count
        if ($n -gt 0) { Set-Pct ([math]::Min(99, 100.0 * $n / 1075)) "$n system files" }
        if ($n -ge 1000 -and $n -eq $last) { $stable++ } else { $stable = 0 }
        $last = $n
        if ($stable -ge 4) { break }
        if ([Gt5Inst.Rpcs3Watch]::Pressed -eq 0 -and $sec -gt 120) { $why = "RPCS3 didn't ask to install it"; break }
        if ($n -eq 0 -and $sec -gt 600) { $why = "RPCS3 didn't start installing it"; break }
        if ($sec -gt 1800) { $why = "it took more than 30 minutes"; break }
    }
    Stop-Rpcs3Watch
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; [void]$p.WaitForExit(10000) }
    if ($why) {
        $seen = @(); try { $seen = @([Gt5Inst.Rpcs3Watch]::Seen | Select-Object -Unique) } catch {}
        Say "  RPCS3: $why ($last files; its windows: $($seen -join ', '))"
    }
    return ($last -ge 1000 -and -not $why)
}

# =====================================================================================================
# The install itself
# =====================================================================================================
function Invoke-Install {
    $pickGame = "$env:GT5_INSTALL_GAME".Trim('"', ' ')
    $pickFw = "$env:GT5_INSTALL_FIRMWARE".Trim('"', ' ')
    # Before anything long: a PC that can't run the game is told so (the window has already said it and offered no
    # Install; this is the console's way, and a safety net).
    $pcProblem = Get-PcProblem
    Say "This PC: $script:PcFacts"
    if ($pcProblem) { Stop-Install $pcProblem }
    $dest = Get-DestState "$env:GT5_INSTALL_DEST".Trim('"', ' ')
    if (-not $dest.Ok) { Stop-Install $dest.Text }
    $base = $dest.Full; $gt5 = Join-Path $base "GT5"; $tools = Join-Path $base "tools"
    $script:Base = $base; $script:Tools = $tools
    $game = Get-GameInfo $pickGame
    try {
        New-Item -Path $RegKey -Force | Out-Null
        Set-ItemProperty -Path $RegKey -Name Dest -Value $base
        if ($pickGame) { Set-ItemProperty -Path $RegKey -Name Game -Value $pickGame }
        if ($pickFw) { Set-ItemProperty -Path $RegKey -Name Firmware -Value $pickFw }
    } catch {}
    Say "Installing to $base"
    if ($game.Text) { Say "Game: $($game.Text)" }
    # The parts the Install button's line named (the window passes them), else, in the console, the same line
    # and one question.
    $vsClang = if ($env:GT5_INSTALL_NOVS) { $null } else { Find-VsClang }
    $useVs = [bool]($vsClang -and (Test-WinSdk))
    $accepted = @()
    if ($env:GT5_INSTALL_ACCEPTED) { $accepted = @("$env:GT5_INSTALL_ACCEPTED" -split '[,\s]+' | Where-Object { $_ -and $_ -ne "none" }) }
    elseif (-not $script:Pipe) {
        $parts = Get-LicenceParts $base $useVs (Test-RtxCard)
        if ($parts.Count -gt 0) {
            if (-not (Ask-Licences $parts)) { Stop-Install "the licences weren't accepted, so nothing was installed." }
            $accepted = @($parts | ForEach-Object { $_.Id })
        }
    }
    if (-not $dest.Existing) { New-Item -ItemType Directory -Force $base | Out-Null; Set-Mark $base "started" }
    $acceptedParts = @(@(@{ Id = "dlss"; Url = $DlssLicence }, @{ Id = "msvc"; Url = $MsLicence }) | Where-Object { $accepted -contains $_.Id })
    Write-LicenceMark $base $acceptedParts
    foreach ($p in $acceptedParts) { Say "  Licence accepted with the Install button: $($p.Id) ($($p.Url))" }
    # On a drive that doesn't record who owns a file (exFAT, FAT32, many USB and network drives) git refuses
    # to work in the install folder ("detected dubious ownership", #41). Trust it for the git this installer
    # and the scripts it starts run, through git's environment config; the player's git settings stay as they are.
    if ($env:GT5_GIT_SAFE -ne "1") {
        $gn = 0; if ("$env:GIT_CONFIG_COUNT" -match '^\d+$') { $gn = [int]$env:GIT_CONFIG_COUNT }
        Set-Item "env:GIT_CONFIG_KEY_$gn" "safe.directory"; Set-Item "env:GIT_CONFIG_VALUE_$gn" "*"
        $env:GIT_CONFIG_COUNT = "$($gn + 1)"; $env:GT5_GIT_SAFE = "1"
    }
    # Nothing may wait for an answer nobody can see: git never asks for a password, opens a sign-in window, or
    # asks "Unlink of file ... failed. Should I try again? (y/n)" (Git for Windows does, when a virus scanner
    # holds a file; GIT_ASK_YESNO names a program that answers no). Its automatic upkeep after a download
    # (maintenance, gc) waits for the next update instead: it ran on in the background, holding the output.
    $env:GIT_TERMINAL_PROMPT = "0"; $env:GCM_INTERACTIVE = "never"; $env:GIT_ASK_YESNO = "false"
    if ($env:GT5_GIT_QUIET -ne "1") {
        $gn = 0; if ("$env:GIT_CONFIG_COUNT" -match '^\d+$') { $gn = [int]$env:GIT_CONFIG_COUNT }
        Set-Item "env:GIT_CONFIG_KEY_$gn" "maintenance.auto"; Set-Item "env:GIT_CONFIG_VALUE_$gn" "false"
        Set-Item "env:GIT_CONFIG_KEY_$($gn + 1)" "gc.auto"; Set-Item "env:GIT_CONFIG_VALUE_$($gn + 1)" "0"
        $env:GIT_CONFIG_COUNT = "$($gn + 2)"; $env:GT5_GIT_QUIET = "1"
    }
    # Python's progress lines (the code generation) arrive as they are printed, not in blocks.
    $env:PYTHONUNBUFFERED = "1"
    # Another Python's settings (Anaconda, QGIS and others set these for everyone) would make the port's own Python
    # load that one's libraries.
    Remove-Item Env:PYTHONHOME, Env:PYTHONPATH -ErrorAction SilentlyContinue

    # ---- 1. This PC ----------------------------------------------------------------------------------
    Step-Start 1 "A few seconds."
    Update-Path
    # The compiler first. A Visual Studio with the Clang tools and the Windows SDK that is already here is used
    # as it is; this installer never installs or changes Visual Studio. GT5_INSTALL_NOVS=1 looks past one.
    # Otherwise the compiler tools are fetched in step 3, under the licence the Install button's line named.
    $needTc = $false; $tc = $null
    if ($useVs) { Say "  Compiler: the Visual Studio already on this PC ($vsClang)" }
    else {
        $tc = Find-Toolchain $base
        if ($tc) { Say "  Compiler: the compiler tools already in $tc" }
        else {
            $needTc = $true
            Say "  Compiler: no Visual Studio with the Clang tools here, so the compiler tools are fetched into $tools\toolchain in step 3 (about 1.5 GB; nothing is installed in Windows)"
            if ($accepted -notcontains "msvc") {
                # The line by the Install button didn't name them (this PC changed since the window looked):
                # back to the first page, where it now does.
                Stop-Install "choose Install again: the compiler tools have to be fetched, and the line by the Install button now names their licence."
            }
        }
    }
    $ownGit = Join-Path $tools "git\cmd"
    $pcGit = if ($env:GT5_INSTALL_NOGIT) { $null } else { Get-Command git -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1 }
    if ($pcGit) { $script:GitExe = $pcGit.Source; $gitText = "the PC's own" }
    else {
        if (-not (Test-Path -LiteralPath (Join-Path $ownGit "git.exe"))) { Install-PortableGit (Join-Path $tools "git") }
        $env:Path = "$ownGit;$env:Path"
        $script:GitExe = Join-Path $ownGit "git.exe"; $gitText = "the port's own (tools\git)"
    }
    $gv = "$(& $script:GitExe --version 2>$null)".Trim()
    if (-not $gv) { Stop-Install "Git doesn't run ($script:GitExe)." }
    Say "  Git: $gv, $gitText"
    Step-End "done" "Ready"

    # ---- 2. The port's code ----------------------------------------------------------------------------
    Step-Start 2 $(if ($dest.Existing) { "Up to a few minutes." } else { "About a minute." })
    if ($dest.Existing) {
        if (Test-Path -LiteralPath (Join-Path $base ".git")) {
            # Rerun = update: the newest version first, so every script used below is the newest.
            Set-Now "Checking for a newer version"; Set-Pct -1
            Remove-GitLocks $base 10
            $up = @{ Fail = ""; Label = "" }
            $code = Invoke-Ps (Join-Path $base "update.ps1") @("-NoBuild") -Dir $base -OnLine {
                param($l)
                if ($l -match '^update: (.*)$') { $up.Fail = $Matches[1] }
                elseif ($l -match '^Switching from .+ to (.+)$|^Reinstalling (.+)$|^Already on (.+?)\.?$') {
                    $up.Label = "$($Matches[1])$($Matches[2])$($Matches[3])"
                    if ($l -notmatch '^Already') { Set-Now "Updating to $($up.Label)" }
                }
                elseif ($l -match '^ps3recomp:') { Set-Now "Updating" }
                $false
            }
            if ($code -ne 0) {
                if ($up.Fail -match 'close the game') { Stop-Install "Gran Turismo 5 is running from this install folder: close it, then press Try again." }
                Note "Couldn't get the newest version: carrying on with the one already here."; Say "  Updating the code didn't work ($(if ($up.Fail) { $up.Fail } else { "code $code" })): carrying on with the version already here."
                Step-End "done" "Not updated"
            } else { Step-End "done" $(if ($up.Label) { "$($up.Label)" } else { "Up to date" }) }
        } else { Step-End "skip" "Already here" }
    } else {
        Get-Code $base
        Step-End "done" "Downloaded"
    }

    # ---- 3. Python and the compiler tools -------------------------------------------------------------------
    Step-Start 3 $(if ($needTc) { "10 to 30 minutes, once (about 1.5 GB to download)." } else { "A few seconds." })
    New-Item -ItemType Directory -Force $tools | Out-Null
    # The port's own Python (python.org's build, checked by hash and signature), for every tool from here on.
    # setup.ps1 makes its environment from it; GT5_OWN_PYTHON=1 also replaces an environment an earlier
    # install made from the PC's Python.
    $getPy = Join-Path $base "get-python.ps1"
    if (Test-Path -LiteralPath $getPy) {
        Set-Now "Downloading build tools"; Set-Pct -1
        # get-python.ps1 unpacks into tools\python.tmp: its file count shows it isn't stuck (minutes on a slow USB drive).
        $pyw = @{ Dir = (Join-Path $tools "python.tmp"); T = 0 }
        [void](Invoke-Ps $getPy @() -Dir $base -OnTick {
            if (([Environment]::TickCount - $pyw.T) -lt 1500) { return }
            $pyw.T = [Environment]::TickCount
            $n = 0; try { $n = @([IO.Directory]::EnumerateFiles($pyw.Dir, "*", [IO.SearchOption]::AllDirectories)).Count } catch {}
            if ($n -gt 1) { Set-Pct -1 "$n files unpacked" }
        })
        if (-not (Test-Path -LiteralPath (Join-Path $tools "python\python.exe"))) { Stop-Install "the port's own Python wasn't fetched (see Details; is the internet connection up?)." }
    } else { Say "  This copy of the code has no get-python.ps1 yet: setup.ps1 fetches Python." }
    $env:GT5_OWN_PYTHON = "1"
    # Processors that calculate wrongly are left out of everything this installer starts from here on
    # (pc-check.ps1 says why): the unlock tool, the code generation, the compiler.
    $pcCheck = Join-Path $base "pc-check.ps1"
    $ownPy = Join-Path $tools "python\python.exe"
    if ((Test-Path -LiteralPath $pcCheck) -and (Test-Path -LiteralPath $ownPy)) {
        Set-Now "Checking this PC"; Set-Pct -1
        . $pcCheck
        $pcNote = Use-GoodProcessors -Base $base -Py $ownPy
        if ($pcNote) { Say "  $pcNote" }
    }
    if ($needTc) {
        $tc = Find-Toolchain $base
        if (-not $tc) {
            $getTc = Join-Path $base "get-toolchain.ps1"
            if (-not (Test-Path -LiteralPath $getTc) -or ([IO.File]::ReadAllText($getTc) -notmatch '\[switch\]\$AcceptLicence')) {
                Stop-Install "this copy of the code is too old to fetch its compiler tools from the installer window. Run Update.bat in $base, then run the installer again."
            }
            Set-Now "Downloading build tools (about 1.5 GB)" "10 to 30 minutes, once."
            # -AcceptLicence: the player accepted Microsoft's licence with the Install button, whose line named it
            # (or, in the console, by answering its one question).
            $code = Invoke-Ps $getTc @("-Path", (Join-Path $tools "toolchain"), "-AcceptLicence") -Dir $base -OnLine {
                param($l)
                if ($l -match '^\s*#*\s*(\d{1,3}(?:\.\d)?)%\s*$') { Set-Pct ([double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture)); return $true }
                if ($l -match '^\s*#[#\s]*$|^\s*$') { return $true }
                if ($l -match 'downloading (.+?)\.\.\.$') { Set-Now "Downloading build tools" }
                elseif ($l -match 'unpacking the parts of LLVM') { Set-Now "Setting up build tools"; Set-Pct -1 }
                elseif ($l -match 'running portable-msvc\.py') { Set-Now "Downloading build tools from Microsoft" "5 to 15 minutes."; Set-Pct -1 }
                elseif ($l -match '^(.+?) \.\.\. (\d+)%$') { Set-Pct -1 "$($Matches[1]) $($Matches[2])%"; return $true }
                elseif ($l -match '^Unpacking msi files') { Set-Now "Setting up build tools"; Set-Pct -1 }
                $false
            }
            $tc = Find-Toolchain $base
            if (-not $tc) {
                Stop-Install "the compiler tools weren't fetched (see Details). Press Try again: what was downloaded is kept. If you would rather use Visual Studio, install Visual Studio 2022 with 'Desktop development with C++', 'C++ Clang tools for Windows' and a Windows 11 SDK yourself, then run this again."
            }
        }
        Say "  Compiler tools: ready in $tc"
    }
    # setup.ps1 and build.cmd pick Visual Studio first; tell them to use these tools in this run (the test
    # switch, or a Visual Studio that has no Windows SDK).
    if (-not $useVs) { $env:GT5_TOOLCHAIN = $tc }
    Step-End "done" "Ready"

    # ---- 4. PS3 system software ---------------------------------------------------------------------------
    $needFw = -not (Test-Path -LiteralPath (Join-Path $gt5 "dev_flash\sys\external")) -or (Test-Mark $base "firmware-partial")
    Step-Start 4 "A few seconds to a few minutes, once."
    if (-not $needFw) { Step-End "skip" "Done" } else {
        if ((Test-Pup $pickFw) -ne "ok") { Stop-Install "choose PS3UPDAT.PUP (Sony's PS3 system software file) in the installer window." }
        Set-Mark $base "firmware-partial"
        if (-not (Unlock-Firmware $pickFw (Join-Path $gt5 "dev_flash"))) { Stop-Install "installing the PS3 system software didn't finish (see Details). Press Try again." }
        Clear-Mark $base "firmware-partial"
        Step-End "done" "Done"
    }

    # ---- 5. The game files ------------------------------------------------------------------------------
    Step-Start 5 "A few minutes, once (about 17 GB)."
    $vfsGame = Join-Path $gt5 "vfs\PS3_GAME"
    if ($game.Ok) {
        $src = $null
        if ($game.Iso) {
            Set-Now "Opening your game"
            $m = Mount-Iso $game.Iso
            if (-not $m) { Stop-Install "couldn't open the .iso. Double-click it in Explorer and choose its drive as the game folder instead." }
            if ($m.Ours) { Set-Mounted $game.Iso }
            $src = Find-Ps3Game $m.Root
        } else { $src = $game.Ps3Game }
        $same = $false
        try { $same = ((Resolve-Path -LiteralPath $src).Path.TrimEnd('\') -eq [IO.Path]::GetFullPath($vfsGame).TrimEnd('\')) } catch {}
        if ($same) { Step-End "skip" "Done" } else {
            $bytes = [double](Get-ChildItem -LiteralPath $src -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
            $gb = [math]::Round($bytes / 1GB, 1)
            Set-Now "Copying your game ($gb GB)"
            Set-Mark $base "game-partial"
            # The bar follows what robocopy has read, so it doesn't matter how it writes the files.
            $cp = @{ Bytes = $bytes; Gb = $gb; Pid = 0 }
            $code = Invoke-Tool "robocopy.exe" @($src, $vfsGame, "/E", "/MT:8", "/R:1", "/W:1", "/NFL", "/NDL", "/NJH", "/NJS", "/NP") -Quiet -OnTick {
                if (-not $cp.Pid) { $cp.Pid = (Get-CimInstance Win32_Process -Filter "Name='robocopy.exe'" -ErrorAction SilentlyContinue | Where-Object { $_.ParentProcessId -eq $PID } | Select-Object -First 1).ProcessId }
                if ($cp.Pid -and $cp.Bytes -gt 0) {
                    $r = (Get-CimInstance Win32_Process -Filter "ProcessId=$($cp.Pid)" -ErrorAction SilentlyContinue).ReadTransferCount
                    if ($r) { Set-Pct ([math]::Min(99.5, 100.0 * $r / $cp.Bytes)) ("{0:N1} of {1:N1} GB" -f ($r / 1GB), $cp.Gb) }
                }
            }
            if ($script:Mounted) { Dismount-Iso $script:Mounted; Set-Mounted $null }
            if ($code -ge 8 -or $code -lt 0) { Say "  robocopy ended with code $code"; Stop-Install "copying the game files didn't finish. Is there enough free space?" }
            Clear-Mark $base "game-partial"
            Step-End "done" "Done"
        }
        if ($script:Mounted) { Dismount-Iso $script:Mounted; Set-Mounted $null }
    } elseif ((Test-Path -LiteralPath (Join-Path $vfsGame "USRDIR")) -and -not (Test-Mark $base "game-partial")) { Step-End "skip" "Done" }
    elseif (Test-Mark $base "game-partial") { Stop-Install "copying your game files didn't finish last time: choose your GT5 game (its folder or .iso) in the installer window so the copy can be completed." }
    else { Stop-Install "choose your GT5 game (its folder or .iso) in the installer window." }

    # ---- 6. Unlock EBOOT.BIN ------------------------------------------------------------------------------
    $elf = Join-Path $gt5 "input\EBOOT.elf"
    $needElf = -not (Test-Path -LiteralPath $elf) -or [bool]$game.Ok
    Step-Start 6 "Under a minute."
    if (-not $needElf) { Step-End "skip" "Done" } else {
        $bin = Join-Path $vfsGame "USRDIR\EBOOT.BIN"
        New-Item -ItemType Directory -Force (Split-Path $elf) | Out-Null
        if (Test-ElfFile $bin) {
            Copy-Item -LiteralPath $bin $elf -Force   # already a plain ELF
        } elseif (-not (Unlock-Eboot $bin $elf)) {
            Stop-Install "unlocking EBOOT.BIN failed. If your game is an encrypted disc image (a 'Redump' .iso), decrypt the image first or use a folder dump of the disc."
        }
        $sha = (Get-FileHash -LiteralPath $elf -Algorithm SHA1).Hash
        Say "  EBOOT.elf SHA-1 $sha"
        Step-End "done" "Done"
    }

    # ---- 7 and 8. Game code and build: setup.ps1 ----------------------------------------------------------
    Step-Start 7 "About 10 minutes."
    Set-Now "Checking everything is in place" "About 10 minutes."; Set-Pct -1
    Remove-GitLocks $base 10
    # Code generation: relift.ps1's phases ("== name") and how much of the time each takes.
    $sp = @{ Msg = ""; Missing = @(); Phase = ""; Built = $false; Gen = $false; Lost = @()
             W = @{ version_addrs = 1; ppu_loader = 4; find_functions = 8; filter = 1; mods = 1; ppu_lifter = 50; setjmp_patch = 2
                    spu_programs = 14; spu_jobs_find = 2; spu_jobs = 12; gen_hle_nids = 3; lift_stamp = 2 } }
    $sp.Order = @("version_addrs", "ppu_loader", "find_functions", "filter", "mods", "ppu_lifter", "setjmp_patch", "spu_programs", "spu_jobs_find", "spu_jobs", "gen_hle_nids", "lift_stamp")
    $setupArgs = @(); if ($env:GT5_INSTALL_NOBUILD) { $setupArgs += "-NoBuild" }
    $code = Invoke-Ps (Join-Path $base "setup.ps1") $setupArgs -Dir $base -OnLine {
        param($l)
        $t = $l.Trim()
        if ($t -match '^\[missing\]\s+(.*)$') { $sp.Missing += $Matches[1] }
        elseif ($t -match '^setup stopped: (.*)$') { $sp.Msg = $Matches[1] }
        elseif ($t -match '^downloading FFmpeg') { Set-Now "Downloading the video player (about 75 MB)" }
        # An optional part that didn't come: the install goes on, and the finish page says in a few words what is missing.
        elseif ($t -match '^\[note\]\s+FFmpeg') { $sp.Lost += "The game's movies won't play until the next update." }
        elseif ($t -match '^Generating the game code') { $sp.Gen = $true; Set-Now "Converting the game" "About 10 minutes."; Set-Pct 0 }
        elseif ($t -match '^Trying once more from the start') {
            # A retry is Details' business, not a note in the main window.
            Say "  The code generation stopped; it is being run once more from the start."
            Set-Now "Converting the game" "About 10 minutes."; Set-Pct 0
        }
        elseif ($sp.Gen -and $t -match '^== (\w+)$' -and $sp.W.ContainsKey($Matches[1])) {
            $sp.Phase = $Matches[1]; $done = 0
            foreach ($k in $sp.Order) { if ($k -eq $sp.Phase) { break }; $done += $sp.W[$k] }
            $sp.Done = $done; Set-Pct $done
        }
        elseif ($sp.Phase -eq "ppu_lifter" -and $t -match 'lifted (\d+)/(\d+) functions') {
            Set-Pct ($sp.Done + $sp.W.ppu_lifter * 0.55 * [double]$Matches[1] / [double]$Matches[2]) "$($Matches[1]) of $($Matches[2]) functions"; return $true
        }
        elseif ($sp.Phase -eq "ppu_lifter" -and $t -match 'emitting (\d+)/(\d+) functions') {
            Set-Pct ($sp.Done + $sp.W.ppu_lifter * (0.6 + 0.4 * [double]$Matches[1] / [double]$Matches[2])) "writing C++"
        }
        elseif ($t -match '^The game code is already generated and current') { $sp.End7 = $true; Step-End "skip" "Done" }
        elseif ($t -match '^Building gt5\.exe') {
            if (-not $sp.End7) { $sp.End7 = $true; Step-End "done" "Done" }
            Step-Start 8 "20 to 50 minutes the first time."
            Set-Now "Building the game" "20 to 50 minutes the first time."; Set-Pct -1
        }
        elseif ($t -match '^\[(\d+)/(\d+)\]') {
            if ([int]$Matches[2] -gt 0) { Set-Pct (100.0 * [int]$Matches[1] / [int]$Matches[2]) "file $($Matches[1]) of $($Matches[2])" }
        }
        elseif ($t -match '^The build stopped\. Trying again with (\d+)') {
            Say "  The compiler stopped on a file (it happens on busy PCs): carrying on with $($Matches[1]) jobs at a time."
            Set-Now "Building the game"
        }
        elseif ($t -match '^launcher: built') { Set-Now "Finishing up" }
        elseif ($t -match '^GT5_ADD (\w+) (\S+) ?(.*)$') {
            # The parts setup.ps1 adds (Vulkan before the build, AMD FSR 3 and NVIDIA DLSS after it): their
            # progress lines (GT5\scripts\addon_common.ps1), shown inside the step that is running.
            $ph = $Matches[1]; $pc = $Matches[2]; $wd = $Matches[3]
            if ($ph -eq "start") { $sp.AddPrev = $script:LastNow; $sp.AddHint = $script:LastHint; $sp.Part = $wd; Set-Now "Adding $wd" $sp.AddHint; Set-Pct 0 }
            elseif ($ph -eq "done" -or $ph -eq "skip" -or $ph -eq "fail") {
                Say "  $($sp.Part): $(if ($ph -eq 'done') { 'ready' } else { "not added ($wd)" })"
                if ($sp.AddPrev) { Set-Now $sp.AddPrev $sp.AddHint }
                Set-Pct -1
            }
            elseif ($pc -match '^\d+$') { Set-Pct ([double]$pc) }
            return $true
        }
        $false
    }
    if ($code -ne 0) {
        # setup.ps1's stop is a few plain words already; what led to it is in Details.
        $why = if ($sp.Msg) { $sp.Msg } elseif ($sp.Missing.Count) { "these are still missing: $($sp.Missing -join '; ')." } else { "the setup didn't finish (Details has the whole text)." }
        Stop-Install $why
    }
    foreach ($l in $sp.Lost) { if ($script:Pipe) { Send "lost" @($l) } else { Say "  $l" "Yellow" } }
    if ($env:GT5_INSTALL_NOBUILD) {
        if (-not $sp.End7) { Step-End "skip" "Not run (GT5_INSTALL_NOBUILD)" }
        Say "Stopped before building (GT5_INSTALL_NOBUILD)." "Yellow"
        Clear-Mark $base "started"
        if ($script:Pipe) { Send "done" @($base, "nobuild") }
        Exit-Engine 0
    }
    if (-not (Test-Path -LiteralPath (Join-Path $gt5 "build\gt5.exe"))) { Stop-Install "the build didn't produce GT5\build\gt5.exe (see Details)." }
    if ($script:CurStep -eq 8) { Step-End "done" "Done" }
    Clear-Mark $base "started"

    if ($script:Pipe) { Send "done" @($base, "built"); Exit-Engine 0 }
    $lnk = New-Shortcut $base
    Write-Host ""
    Write-Host "  Done! GT5 Recomp is installed." -ForegroundColor Green
    Write-Host "  Start it with 'Gran Turismo 5 (PC)' on your desktop: the launcher, with settings (resolution,"
    Write-Host "  full screen, wheel, language), Play and Updates. (Or $gt5\play.bat directly.)"
    Write-Host "  Keyboard: Z = Cross, X = Circle, arrows, Enter = Start. Pads work too. More in README.md."
    Write-Host "  To update later: the launcher's Updates page, or double-click Update.bat in $base."
    Write-LogLine "Done: installed in $base (shortcut $lnk)"
    Exit-Engine 0
}

function Invoke-Engine {
    # UTF-8 on this process's console: its report lines, and the PowerShell scripts it starts (they share
    # the console and write in its code page).
    try { [Console]::OutputEncoding = $Utf8 } catch {}
    # Below normal priority, and so is everything started from here (git, Python, the compiler: a child takes a below
    # normal parent's class): "You can keep using your PC" while the hour-long build runs.
    try { [Diagnostics.Process]::GetCurrentProcess().PriorityClass = [Diagnostics.ProcessPriorityClass]::BelowNormal } catch {}
    if (-not $script:Pipe) {
        $script:LogW = Open-LogWriter
        Write-LogLine ""; Write-LogLine "==== GT5-Installer $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), console ===="
        Write-Host ""
        Write-Host "  GT5 Recomp - installer" -ForegroundColor White
        Write-Host "  (log: $LogPath)" -ForegroundColor DarkGray
    }
    try { Invoke-Install }
    catch { Stop-Install "the installer itself hit an error: $($_.Exception.Message) (line $($_.InvocationInfo.ScriptLineNumber))." }
}

# =====================================================================================================
# The window. It shows the choices, then turns into the progress view, the licence page when needed, the
# result of a stop, and the finish page. The work runs in the engine process; the window reads its report
# lines on a timer and never blocks.
# =====================================================================================================
$script:U = @{}
function S([double]$v) { return [int][math]::Round($v * $script:U.K) }
function Write-UiLog([string]$t) { if ($script:U.Log) { try { $script:U.Log.WriteLine($t) } catch {} } }
function New-Ctl([string]$type, $parent, [double]$x, [double]$y, [double]$w, [double]$h, [string]$text = "") {
    $c = New-Object "Windows.Forms.$type"
    $c.SetBounds((S $x), (S $y), (S $w), (S $h))
    if ($text) { $c.Text = $text }
    $parent.Controls.Add($c)
    return $c
}
function New-Btn($parent, [string]$text, [double]$x, [double]$y, [double]$w) {
    $b = New-Ctl "Button" $parent $x $y $w 28 $text
    $b.FlatStyle = "System"
    return $b
}
function Set-DoubleBuffered($c) {
    try { [Windows.Forms.Control].GetProperty("DoubleBuffered", [Reflection.BindingFlags]"NonPublic,Instance").SetValue($c, $true, $null) } catch {}
}
function Format-Span([double]$sec) {
    $s = [int]$sec
    if ($s -ge 3600) { return ("{0}:{1:D2}:{2:D2}" -f [int][math]::Floor($s / 3600), [int]([math]::Floor($s / 60) % 60), ($s % 60)) }
    return ("{0}:{1:D2}" -f [int][math]::Floor($s / 60), ($s % 60))
}

function Show-Window {
    $U = $script:U
    Add-Type -AssemblyName System.Windows.Forms, System.Drawing
    try {
        Add-Type -Namespace Gt5Ui -Name Native -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
'@
        [void][Gt5Ui.Native]::SetProcessDPIAware()
    } catch {}
    [Windows.Forms.Application]::EnableVisualStyles()
    # The layout is in 100%-scaling pixels; S() scales it to the screen (fonts in points scale by themselves).
    $dpi = 96.0; try { $gr = [Drawing.Graphics]::FromHwnd([IntPtr]::Zero); $dpi = $gr.DpiX; $gr.Dispose() } catch {}
    $U.K = $dpi / 96.0; $fk = 1.0
    if ($env:GT5_INSTALL_UISCALE) { $want = [double]$env:GT5_INSTALL_UISCALE; $fk = $want / $U.K; $U.K = $want }   # testing
    $U.Log = Open-LogWriter
    Write-UiLog ""; Write-UiLog "==== GT5-Installer $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), window ===="
    $U.Band = [Drawing.Color]::FromArgb(24, 27, 36); $U.Accent = [Drawing.Color]::FromArgb(40, 116, 208); $U.Ok = [Drawing.Color]::FromArgb(20, 120, 50)
    $U.Bad = [Drawing.Color]::FromArgb(180, 30, 30); $U.Amber = [Drawing.Color]::FromArgb(150, 90, 0)
    $U.Dim = [Drawing.Color]::DimGray; $U.Wait = [Drawing.Color]::FromArgb(150, 150, 150)
    $U.Font = New-Object Drawing.Font("Segoe UI", (10 * $fk)); $U.Bold = New-Object Drawing.Font("Segoe UI Semibold", (10 * $fk))
    $U.Small = New-Object Drawing.Font("Segoe UI", (9 * $fk)); $U.NowFont = New-Object Drawing.Font("Segoe UI Semibold", (11 * $fk))
    $U.Big = New-Object Drawing.Font("Segoe UI Semibold", (16 * $fk)); $U.Mono = New-Object Drawing.Font("Consolas", (9 * $fk))
    $U.BrOk = New-Object Drawing.SolidBrush($U.Ok); $U.BrBad = New-Object Drawing.SolidBrush($U.Bad)
    $U.BrAmber = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(200, 130, 0)); $U.BrBar = New-Object Drawing.SolidBrush($U.Accent)
    $U.BrBarBg = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(228, 228, 228))
    $U.St = @(); $U.StNote = @(); for ($i = 0; $i -lt $Total; $i++) { $U.St += "wait"; $U.StNote += "" }
    $U.Lines = New-Object Collections.Generic.List[string]
    $U.SnapDir = $env:GT5_INSTALL_SNAP; $U.SnapN = 0; $U.SnapDue = 0; $U.SnapName = ""
    if ($U.SnapDir) { New-Item -ItemType Directory -Force $U.SnapDir | Out-Null }

    $f = New-Object Windows.Forms.Form
    $U.Form = $f
    $f.AutoScaleMode = "None"
    $f.Text = "GT5 Recomp - installer"; $f.ClientSize = New-Object Drawing.Size((S 760), (S 520))
    $f.FormBorderStyle = "FixedDialog"; $f.MaximizeBox = $false
    $f.Font = $U.Font; $f.BackColor = [Drawing.Color]::White
    try {
        $ib = New-Object Drawing.Bitmap 32, 32; $ig = [Drawing.Graphics]::FromImage($ib); $ig.Clear($U.Band)
        $ig.TextRenderingHint = "AntiAlias"; $ig.DrawString("GT", (New-Object Drawing.Font("Segoe UI", 13, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)), [Drawing.Brushes]::White, 3, 7)
        $f.Icon = [Drawing.Icon]::FromHandle($ib.GetHicon())
    } catch {}
    if ($U.SnapDir) { $f.StartPosition = "Manual"; $f.Location = New-Object Drawing.Point(-10000, -10000); $f.ShowInTaskbar = $false }
    else { $f.StartPosition = "CenterScreen" }

    $band = New-Ctl "Panel" $f 0 0 760 78; $band.BackColor = $U.Band
    $t1 = New-Ctl "Label" $band 22 6 700 40 "GT5 Recomp"; $t1.ForeColor = [Drawing.Color]::White
    $t1.Font = New-Object Drawing.Font("Segoe UI Semibold", (20 * $fk))
    $U.Sub = New-Ctl "Label" $band 25 48 720 24; $U.Sub.ForeColor = [Drawing.Color]::FromArgb(186, 192, 208); $U.Sub.AutoEllipsis = $true
    $U.Pages = @{}
    foreach ($n in "pick", "work", "fin") {
        $pg = New-Ctl "Panel" $f 0 78 760 442; $pg.Visible = $false; $U.Pages[$n] = $pg
    }
    Build-PickPage $U.Pages.pick
    Build-WorkPage $U.Pages.work
    Build-FinishPage $U.Pages.fin

    $U.Timer = New-Object Windows.Forms.Timer
    $U.Timer.Interval = 50
    $U.Timer.Add_Tick({ try { On-Tick } catch { Write-UiLog "window: $($_.Exception.Message) (line $($_.InvocationInfo.ScriptLineNumber))" } })
    $f.Add_Shown({
        try {
            Show-Page "pick"
            & $U.Check
            if (-not $U.SnapDir) { $f.Activate() }
            $U.Timer.Start()
        } catch { Write-UiLog "window: $($_.Exception.Message)" }
    })
    $f.Add_FormClosing({
        param($s, $e)
        try {
            if ($U.Proc -and -not $U.ClosingNow) {
                if (-not (Request-Cancel)) { $e.Cancel = $true; return }
            }
            if ($U.Page -eq "fin") { Set-ShortcutChoice }
            $U.Timer.Stop()
        } catch { Write-UiLog "window: $($_.Exception.Message)" }
    })
    [void]$f.ShowDialog()
    if ($U.Proc) { Stop-Engine }
    if ($U.Log) { try { $U.Log.Dispose() } catch {} }
}

function Show-Page([string]$name) {
    $U = $script:U
    foreach ($k in @($U.Pages.Keys)) { $U.Pages[$k].Visible = ($k -eq $name) }
    $U.Page = $name
    $U.Sub.Text = switch ($name) {
        "pick" { "Builds the PC version from your own copy of the game. Choose the three things below and press Install." }
        "work" { if ($U.Mode -eq "run") { "Installing. You can keep using your PC." } else { $U.Sub.Text } }
        "fin"  { "Finished." }
    }
    Request-Snap $name
}
function Request-Snap([string]$name) {
    if ($script:U.SnapDir) { $script:U.SnapDue = [Environment]::TickCount + 700; $script:U.SnapName = $name }
}
function Save-Snap([string]$name) {
    $U = $script:U
    if (-not $U.SnapDir) { return }
    try {
        $w = $U.Form.Width; $h = $U.Form.Height
        $bmp = New-Object Drawing.Bitmap($w, $h)
        $g = [Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
        [void][Gt5Ui.Native]::PrintWindow($U.Form.Handle, $hdc, 2)
        $g.ReleaseHdc($hdc); $g.Dispose()
        $U.SnapN++
        $bmp.Save((Join-Path $U.SnapDir ("{0:D2}-{1}.png" -f $U.SnapN, $name)), [Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
    } catch { Write-UiLog "snap: $($_.Exception.Message)" }
}

# ---- Page: the three choices ----------------------------------------------------------------------------
function Build-PickPage($pg) {
    $U = $script:U
    function New-Row([int]$y, [string]$title, [string]$hint) {
        $l = New-Ctl "Label" $pg 22 $y 716 21 $title; $l.Font = $U.Bold
        $h = New-Ctl "Label" $pg 22 ($y + 21) 716 21 $hint; $h.ForeColor = $U.Dim
        $tb = New-Ctl "TextBox" $pg 22 ($y + 46) 470 26
        $st = New-Ctl "Label" $pg 22 ($y + 76) 716 22; $st.AutoEllipsis = $true
        return @{ Box = $tb; Status = $st; Y = $y + 45 }
    }
    $rg = New-Row 14  "1.  Your Gran Turismo 5 game" "Your disc dump: its folder (the one with PS3_GAME in it) or an .iso file. Europe or US."
    $rf = New-Row 118 "2.  PS3 system software (PS3UPDAT.PUP)" "Sony's free PS3 update file. The game needs some PS3 system files from it."
    $rd = New-Row 222 "3.  Install to" "A new or empty folder. About $NeedGB GB of space."
    $U.Rg = $rg; $U.Rf = $rf; $U.Rd = $rd
    $bgF = New-Btn $pg "Folder..." 502 $rg.Y 110; $bgI = New-Btn $pg "ISO file..." 620 $rg.Y 118
    $bfB = New-Btn $pg "Browse..." 502 $rf.Y 110; $bfS = New-Btn $pg "Get it from Sony" 620 $rf.Y 118
    $bdB = New-Btn $pg "Browse..." 502 $rd.Y 110
    $note = New-Ctl "Label" $pg 22 326 716 40; $note.ForeColor = $U.Dim
    $note.Text = "Takes about an hour the first time. Everything goes into the install folder. Nothing from your game is uploaded or shared."
    # This PC can't run the game (no AVX2, too little memory): that line instead, and no Install. Asked once.
    $U.PcProblem = Get-PcProblem
    Write-UiLog "PC: $script:PcFacts$(if ($U.PcProblem) { " -> $($U.PcProblem)" })"
    if ($U.PcProblem) { $note.Text = $U.PcProblem; $note.ForeColor = $U.Bad; $note.Font = $U.Bold }
    # The licences of the parts it downloads, when there are any: one line, the names are links. Pressing
    # Install is the acceptance (Get-LicenceParts; set again by $U.Check, as it depends on the folder).
    $U.Lic = New-Ctl "LinkLabel" $pg 22 368 716 20; $U.Lic.ForeColor = $U.Dim; $U.Lic.Font = $U.Small
    $U.Lic.LinkBehavior = "HoverUnderline"
    $U.Lic.Add_LinkClicked({ param($s, $e) try { Start-Process ([string]$e.Link.LinkData) } catch {} })
    $U.LicParts = @()
    $U.VsOk = [bool]((-not $env:GT5_INSTALL_NOVS) -and (Find-VsClang) -and (Test-WinSdk))
    $U.Rtx = Test-RtxCard
    $U.Go = New-Btn $pg "Install" 520 392 110; $U.Go.Font = $U.Bold
    $cancel = New-Btn $pg "Cancel" 638 392 100
    $U.Form.AcceptButton = $U.Go

    # Saved choices from the last run, else a PS3UPDAT.PUP in Downloads, and the usual folder.
    $saved = @{}
    try { $k = Get-ItemProperty -Path $RegKey -ErrorAction Stop; foreach ($n in "Game", "Firmware", "Dest") { $saved[$n] = $k.$n } } catch {}
    $fw0 = $saved.Firmware
    if (-not $fw0) {
        $fw0 = Get-ChildItem (Join-Path $env:USERPROFILE "Downloads") -Filter "PS3UPDAT.PUP" -Recurse -Depth 2 -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
    }
    # A remembered game or firmware file that isn't there any more (deleted, or on a drive not plugged in) isn't filled
    # in: an install that already has them needs neither, and a red "isn't there" would only stop Install.
    if ($saved.Game -and -not (Test-Path -LiteralPath $saved.Game)) { Write-UiLog "The game remembered from last time isn't there now: $($saved.Game)"; $saved.Game = $null }
    if ($fw0 -and -not (Test-Path -LiteralPath $fw0)) { Write-UiLog "The firmware file remembered from last time isn't there now: $fw0"; $fw0 = $null }
    $rg.Box.Text = if ($env:GT5_INSTALL_GAME) { $env:GT5_INSTALL_GAME } else { $saved.Game }
    $rf.Box.Text = if ($env:GT5_INSTALL_FIRMWARE) { $env:GT5_INSTALL_FIRMWARE } else { $fw0 }
    $rd.Box.Text = if ($env:GT5_INSTALL_DEST) { $env:GT5_INSTALL_DEST } elseif ($saved.Dest) { $saved.Dest } else { "C:\Games\GT5-PC" }

    $U.Check = {
        $U = $script:U
        $d = Get-DestState $U.Rd.Box.Text.Trim('"', ' '); $U.Dest = $d
        $U.Rd.Status.Text = $d.Text; $U.Rd.Status.ForeColor = $(if ($d.Ok) { $U.Ok } else { $U.Bad })
        $gp = $U.Rg.Box.Text.Trim('"', ' ')
        # Already in the install folder: a path that isn't there now (the .iso was deleted after the first install)
        # counts as nothing chosen. The game and firmware are only needed when the folder lacks them.
        $gpGone = $gp -and -not (Test-Path -LiteralPath $gp) -and $d.Ok -and $d.HasGame
        if ($gpGone) { $gp = "" }
        if ($gp -ne $U.GamePick) { $U.GamePick = $gp; $U.Form.Cursor = "WaitCursor"; $U.Game = Get-GameInfo $gp; $U.Form.Cursor = "Default" }
        $g = $U.Game
        if ($g -and $g.Ok) { $U.Rg.Status.Text = $g.Text; $U.Rg.Status.ForeColor = $U.Ok }
        elseif ($g -and $g.Text) { $U.Rg.Status.Text = $g.Text; $U.Rg.Status.ForeColor = $U.Bad }
        elseif ($d.Ok -and $d.HasGame) { $U.Rg.Status.Text = "Not needed: the game is already in the install folder."; $U.Rg.Status.ForeColor = $U.Ok }
        else { $U.Rg.Status.Text = "" }
        $fp = $U.Rf.Box.Text.Trim('"', ' ')
        $fpGone = $fp -and -not (Test-Path -LiteralPath $fp) -and $d.Ok -and $d.HasFw
        if ($fpGone) { $fp = "" }
        $p = Test-Pup $fp
        $fwOk = ($p -eq "ok")
        if ($fwOk) { $U.Rf.Status.Text = "Found: PS3 system software"; $U.Rf.Status.ForeColor = $U.Ok }
        elseif ($p) { $U.Rf.Status.Text = $p; $U.Rf.Status.ForeColor = $U.Bad }
        elseif ($d.Ok -and $d.HasFw) { $U.Rf.Status.Text = "Not needed: already installed in the install folder."; $U.Rf.Status.ForeColor = $U.Ok }
        else { $U.Rf.Status.Text = "" }
        $gameReady = ($g -and $g.Ok) -or ((-not $gp) -and $d.Ok -and $d.HasGame)
        $fwReady = $fwOk -or ((-not $fp) -and $d.Ok -and $d.HasFw)
        $U.Go.Enabled = [bool]($d.Ok -and $gameReady -and $fwReady -and -not $U.PcProblem)
        $U.GameArg = $gp; $U.FwArg = $fp   # what Install passes on (a path that's gone and not needed: nothing)
        # The licence line: only the parts this install will download.
        $U.LicParts = Get-LicenceParts $(if ($d.Ok) { $d.Full } else { "" }) $U.VsOk $U.Rtx
        $line = Get-LicenceLine $U.LicParts
        if ($U.Lic.Text -ne $line) {
            $U.Lic.Text = $line; $U.Lic.Links.Clear()
            foreach ($p in $U.LicParts) { [void]$U.Lic.Links.Add($line.LastIndexOf($p.Name), $p.Name.Length, $p.Url) }
        }
        $U.Lic.Visible = [bool]$line
    }
    foreach ($r in @($rg, $rf, $rd)) { $r.Box.Add_Leave({ try { & $script:U.Check } catch {} }) }
    $bgF.Add_Click({ $dlg = New-Object Windows.Forms.FolderBrowserDialog; $dlg.Description = "Choose your GT5 disc dump (the folder with PS3_GAME in it)"
                     if ($dlg.ShowDialog($script:U.Form) -eq "OK") { $script:U.Rg.Box.Text = $dlg.SelectedPath; & $script:U.Check } })
    $bgI.Add_Click({ $dlg = New-Object Windows.Forms.OpenFileDialog; $dlg.Filter = "Disc image (*.iso)|*.iso"; $dlg.Title = "Choose your GT5 .iso"
                     if ($dlg.ShowDialog($script:U.Form) -eq "OK") { $script:U.Rg.Box.Text = $dlg.FileName; & $script:U.Check } })
    $bfB.Add_Click({ $dlg = New-Object Windows.Forms.OpenFileDialog; $dlg.Filter = "PS3 system software (PS3UPDAT.PUP)|*.PUP"; $dlg.Title = "Choose PS3UPDAT.PUP"
                     if ($dlg.ShowDialog($script:U.Form) -eq "OK") { $script:U.Rf.Box.Text = $dlg.FileName; & $script:U.Check } })
    $bfS.Add_Click({ Start-Process $SonyPage })
    $bdB.Add_Click({ $dlg = New-Object Windows.Forms.FolderBrowserDialog; $dlg.Description = "Choose where to install (a new folder is made inside if this one isn't empty)"
                     if ($dlg.ShowDialog($script:U.Form) -eq "OK") {
                         $p = $dlg.SelectedPath
                         if ((Get-ChildItem -LiteralPath $p -Force -ErrorAction SilentlyContinue | Select-Object -First 1) -and -not (Test-Path -LiteralPath (Join-Path $p "setup.ps1")) -and -not (Test-Mark $p "started")) { $p = Join-Path $p "GT5-PC" }
                         $script:U.Rd.Box.Text = $p; & $script:U.Check } })
    $U.Go.Add_Click({ try { Start-Install } catch { Write-UiLog "window: $($_.Exception.Message)" } })
    $cancel.Add_Click({ $script:U.Form.Close() })
}

function Start-Install {
    $U = $script:U
    & $U.Check
    if (-not $U.Go.Enabled) { return }
    $U.Choice = @{ Game = $U.GameArg; Firmware = $U.FwArg; Dest = $U.Rd.Box.Text.Trim('"', ' ') }
    # Pressing Install with the licence line shown accepts the parts it names.
    $U.Accepted = if ($U.LicParts.Count -gt 0) { (@($U.LicParts | ForEach-Object { $_.Id }) -join ",") } else { "none" }
    Write-UiLog "Install pressed. $(if ($U.Lic.Visible) { "Licence line shown: $($U.Lic.Text) ($((@($U.LicParts | ForEach-Object { $_.Url })) -join ', '))" } else { 'No licence line (nothing it downloads needs one).' })"
    $U.Base = $U.Dest.Full
    $U.T0 = Get-Date
    Show-Page "work"
    Start-Engine
}

# ---- Page: the work ---------------------------------------------------------------------------------------
function Build-WorkPage($pg) {
    $U = $script:U
    $U.Steps = New-Ctl "Panel" $pg 22 12 716 (26 * $Total + 4)
    Set-DoubleBuffered $U.Steps
    $U.Steps.Add_Paint({ param($s, $e) try { Draw-Steps $e.Graphics $s.ClientSize.Width } catch {} })
    $U.Details = New-Ctl "TextBox" $pg 22 12 716 226
    $U.Details.Multiline = $true; $U.Details.ReadOnly = $true; $U.Details.WordWrap = $false
    $U.Details.ScrollBars = "Both"; $U.Details.Font = $U.Mono; $U.Details.Visible = $false
    $U.Details.BackColor = [Drawing.Color]::FromArgb(248, 248, 248)
    $U.NowL = New-Ctl "Label" $pg 22 246 716 24; $U.NowL.Font = $U.NowFont; $U.NowL.AutoEllipsis = $true
    $U.Bar = New-Ctl "Panel" $pg 22 274 716 12
    Set-DoubleBuffered $U.Bar
    $U.Bar.Add_Paint({ param($s, $e) try { Draw-Bar $e.Graphics $s.ClientSize.Width $s.ClientSize.Height } catch {} })
    $U.SubL = New-Ctl "Label" $pg 22 290 520 20; $U.SubL.ForeColor = $U.Dim; $U.SubL.AutoEllipsis = $true
    $U.TimeL = New-Ctl "Label" $pg 542 290 196 20; $U.TimeL.ForeColor = $U.Dim; $U.TimeL.TextAlign = "TopRight"
    $U.HintL = New-Ctl "Label" $pg 22 310 716 20; $U.HintL.ForeColor = $U.Dim; $U.HintL.AutoEllipsis = $true
    $U.NoteL = New-Ctl "Label" $pg 22 334 716 54; $U.NoteL.ForeColor = $U.Amber
    $U.DetBtn = New-Btn $pg "Details" 22 392 120
    $U.DetBtn.Add_Click({ try { Show-Details (-not $script:U.Details.Visible) } catch {} })
    $U.Report = New-Ctl "LinkLabel" $pg 150 397 140 22 "Report a problem"; $U.Report.Visible = $false
    $U.Report.Add_LinkClicked({ Start-Process $HelpPage })
    $U.CopyBtn = New-Btn $pg "Copy details" 298 392 112
    $U.CopyBtn.Add_Click({ try { Copy-Details } catch {} })
    $U.LogBtn = New-Btn $pg "Open the log" 416 392 112
    $U.LogBtn.Add_Click({ try { Start-Process notepad.exe -ArgumentList "`"$LogPath`"" } catch {} })
    $U.RetryBtn = New-Btn $pg "Try again" 534 392 98; $U.RetryBtn.Font = $U.Bold
    $U.RetryBtn.Add_Click({ try { Restart-Install } catch { Write-UiLog "window: $($_.Exception.Message)" } })
    $U.CancelBtn = New-Btn $pg "Cancel" 638 392 100
    $U.CancelBtn.Add_Click({ try { if ($script:U.Proc) { [void](Request-Cancel) } else { $script:U.Form.Close() } } catch {} })
    Set-WorkMode "run"
}
# The buttons for: "run" (working), "fail" (stopped by a problem), "stop" (stopped by the player).
function Set-WorkMode([string]$mode) {
    $U = $script:U
    $U.Mode = $mode
    $U.Report.Visible = ($mode -eq "fail"); $U.CopyBtn.Visible = ($mode -eq "fail"); $U.LogBtn.Visible = ($mode -ne "run")
    $U.RetryBtn.Visible = ($mode -ne "run")
    $U.CancelBtn.Text = if ($mode -eq "run") { "Cancel" } else { "Close" }
    # Stopped: the message takes the room of the figures and times.
    $U.SubL.Visible = ($mode -eq "run"); $U.TimeL.Visible = ($mode -eq "run"); $U.HintL.Visible = ($mode -eq "run")
    if ($mode -eq "run") { $U.NoteL.SetBounds((S 22), (S 334), (S 716), (S 54)) }
    else { $U.NoteL.SetBounds((S 22), (S 292), (S 716), (S 96)) }
}
function Show-Details([bool]$on) {
    $U = $script:U
    $U.Details.Visible = $on; $U.Steps.Visible = -not $on
    $U.DetBtn.Text = if ($on) { "Hide details" } else { "Details" }
    if ($on) { $U.DetDirty = $true; Update-Details }
}
function Add-Detail([string]$l) {
    $U = $script:U
    $U.Lines.Add($l)
    if ($U.Lines.Count -gt 3000) { $U.Lines.RemoveRange(0, 500) }
    $U.DetDirty = $true
}
function Update-Details {
    $U = $script:U
    if (-not $U.Details.Visible -or -not $U.DetDirty) { return }
    $U.DetDirty = $false; $U.DetT = [Environment]::TickCount
    $U.Details.Text = [string]::Join("`r`n", $U.Lines.ToArray()) + "`r`n"   # ends on an empty line: no sideways scroll
    $U.Details.SelectionStart = $U.Details.TextLength; $U.Details.ScrollToCaret()
}
function Copy-Details {
    $U = $script:U
    $n = [math]::Min(300, $U.Lines.Count)
    $tail = if ($n -gt 0) { $U.Lines.GetRange($U.Lines.Count - $n, $n).ToArray() } else { @() }
    $txt = "GT5-Installer, $(Get-Date -Format 'yyyy-MM-dd HH:mm')`r`nStopped: $($U.StopText)`r`nThe whole log: $LogPath`r`n`r`n" + [string]::Join("`r`n", $tail)
    [Windows.Forms.Clipboard]::SetText($txt)
    $U.CopyBtn.Text = "Copied"
}
function Draw-Steps([Drawing.Graphics]$g, [int]$w) {
    $U = $script:U
    $g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $rowH = S 26; $d = [float](S 16); $x0 = [float](S 3)
    $flags = [Windows.Forms.TextFormatFlags]"Left, VerticalCenter, EndEllipsis, NoPrefix, SingleLine"
    $right = [Windows.Forms.TextFormatFlags]"Right, VerticalCenter, EndEllipsis, NoPrefix, SingleLine"
    for ($i = 0; $i -lt $Total; $i++) {
        $y = $i * $rowH
        $cy = [float]($y + ($rowH - $d) / 2)
        $r = New-Object Drawing.RectangleF($x0, $cy, $d, $d)
        $st = $U.St[$i]
        $pw = [float][math]::Max(1.5, (S 2))
        if ($st -eq "done" -or $st -eq "skip") {
            $g.FillEllipse($U.BrOk, $r)
            $pen = New-Object Drawing.Pen([Drawing.Color]::White, $pw)
            $g.DrawLines($pen, [Drawing.PointF[]]@((New-Object Drawing.PointF(($x0 + $d * 0.26), ($cy + $d * 0.52))),
                                                  (New-Object Drawing.PointF(($x0 + $d * 0.44), ($cy + $d * 0.70))),
                                                  (New-Object Drawing.PointF(($x0 + $d * 0.76), ($cy + $d * 0.32)))))
            $pen.Dispose()
        } elseif ($st -eq "run") {
            $pen = New-Object Drawing.Pen([Drawing.Color]::FromArgb(225, 225, 225), $pw); $g.DrawEllipse($pen, $r); $pen.Dispose()
            $pen = New-Object Drawing.Pen($U.Accent, $pw); $g.DrawArc($pen, $r, [float]$U.Spin, [float]110); $pen.Dispose()
        } elseif ($st -eq "fail" -or $st -eq "stop") {
            $g.FillEllipse($(if ($st -eq "fail") { $U.BrBad } else { $U.BrAmber }), $r)
            $pen = New-Object Drawing.Pen([Drawing.Color]::White, $pw)
            if ($st -eq "fail") {
                $g.DrawLine($pen, ($x0 + $d * 0.32), ($cy + $d * 0.32), ($x0 + $d * 0.68), ($cy + $d * 0.68))
                $g.DrawLine($pen, ($x0 + $d * 0.68), ($cy + $d * 0.32), ($x0 + $d * 0.32), ($cy + $d * 0.68))
            } else { $g.DrawLine($pen, ($x0 + $d * 0.3), ($cy + $d * 0.5), ($x0 + $d * 0.7), ($cy + $d * 0.5)) }
            $pen.Dispose()
        } else {
            $pen = New-Object Drawing.Pen([Drawing.Color]::FromArgb(200, 200, 200), [float][math]::Max(1, (S 1.5))); $g.DrawEllipse($pen, $r); $pen.Dispose()
        }
        $font = if ($st -eq "run") { $U.Bold } else { $U.Font }
        $col = if ($st -eq "wait") { $U.Wait } else { [Drawing.Color]::Black }
        $tx = S 28; $tw = S 300
        [Windows.Forms.TextRenderer]::DrawText($g, ("{0}.  {1}" -f ($i + 1), $StepTitles[$i]), $font, (New-Object Drawing.Rectangle($tx, $y, $tw, $rowH)), $col, $flags)
        if ($U.StNote[$i]) {
            $nc = if ($st -eq "fail") { $U.Bad } elseif ($st -eq "stop") { $U.Amber } else { $U.Dim }
            $nx = $tx + $tw; $nw = $w - $nx - (S 2)
            [Windows.Forms.TextRenderer]::DrawText($g, $U.StNote[$i], $U.Small, (New-Object Drawing.Rectangle($nx, $y, $nw, $rowH)), $nc, $right)
        }
    }
}
function Draw-Bar([Drawing.Graphics]$g, [int]$w, [int]$h) {
    $U = $script:U
    $g.FillRectangle($U.BrBarBg, 0, 0, $w, $h)
    if ($U.Shown -ge 0) {
        $fw = [int]($w * [math]::Min(100, $U.Shown) / 100)
        if ($fw -gt 0) { $g.FillRectangle($U.BrBar, 0, 0, $fw, $h) }
    } elseif ($U.Proc) {
        $bw = [int]($w / 5); $x = [int](($U.Anim % ($w + $bw)) - $bw)
        $g.FillRectangle($U.BrBar, $x, 0, $bw, $h)
    }
}

# ---- Page: finished ---------------------------------------------------------------------------------------
function Build-FinishPage($pg) {
    $U = $script:U
    $U.FinBig = New-Ctl "Label" $pg 22 18 716 40 "Done! GT5 Recomp is installed."; $U.FinBig.Font = $U.Big; $U.FinBig.ForeColor = $U.Ok
    $U.FinWhere = New-Ctl "Label" $pg 22 64 716 22; $U.FinWhere.AutoEllipsis = $true
    $tips = New-Ctl "Label" $pg 22 100 716 110
    $tips.Text = "Play opens the launcher: settings (resolution, full screen, wheel, language), Play and Updates.`r`n`r`nKeyboard: Z = Cross, X = Circle, arrows, Enter = Start. Pads work too. More in README.md.`r`nTo update later: the launcher's Updates page, or Update.bat in the install folder."
    $U.LnkTick = New-Ctl "CheckBox" $pg 22 230 716 26 "Put 'Gran Turismo 5 (PC)' on the desktop"
    $U.LnkTick.Checked = $true
    # An optional part that didn't come this time (the install went on without it): what that means, in a few words.
    $U.FinLost = New-Ctl "Label" $pg 22 270 716 44; $U.FinLost.ForeColor = $U.Dim
    $U.PlayBtn = New-Btn $pg "Play" 520 392 110; $U.PlayBtn.Font = $U.Bold
    $U.PlayBtn.Add_Click({ try { Start-Game } catch { Write-UiLog "window: $($_.Exception.Message)" } })
    $close = New-Btn $pg "Close" 638 392 100
    $close.Add_Click({ $script:U.Form.Close() })
}
function Show-Finish([string]$base, [string]$kind) {
    $U = $script:U
    $U.Base = $base
    $U.FinWhere.Text = "Installed in $base"
    $U.FinLost.Text = (@($U.Lost) -join "`r`n")
    if ($kind -eq "nobuild") {
        $U.FinBig.Text = "Stopped before building (GT5_INSTALL_NOBUILD)."; $U.FinBig.ForeColor = $U.Amber
        $U.LnkTick.Checked = $false
    }
    $U.PlayBtn.Enabled = (Test-Path -LiteralPath (Join-Path $base "GT5Launcher.exe")) -or (Test-Path -LiteralPath (Join-Path $base "GT5\build\gt5.exe"))
    Show-Page "fin"
    if (-not $U.SnapDir) { $U.Form.Activate() }
}
function Set-ShortcutChoice {
    $U = $script:U
    if ($U.LnkDone -or -not $U.Base) { return }
    $U.LnkDone = $true
    if ($U.LnkTick.Checked) { try { $l = New-Shortcut $U.Base; Write-UiLog "Shortcut: $l" } catch { Write-UiLog "Shortcut failed: $($_.Exception.Message)" } }
}
function Start-Game {
    $U = $script:U
    Set-ShortcutChoice
    $launcher = Join-Path $U.Base "GT5Launcher.exe"
    if (Test-Path -LiteralPath $launcher) { Start-Process -FilePath $launcher -WorkingDirectory $U.Base }
    else { Start-Process -FilePath (Join-Path $U.Base "GT5\play.bat") -WorkingDirectory (Join-Path $U.Base "GT5") }
    $U.Form.Close()
}

# ---- The engine, from the window --------------------------------------------------------------------------
function Start-Engine {
    $U = $script:U
    for ($i = 0; $i -lt $Total; $i++) { $U.St[$i] = "wait"; $U.StNote[$i] = "" }
    $U.Cur = 0; $U.Now = "Starting"; $U.Hint = ""; $U.Pct = -1; $U.Shown = -1; $U.SubText = ""; $U.NoteText = ""
    $U.StopText = ""; $U.Done = $null; $U.Cancelled = $false; $U.Mounted = $null; $U.EtaBase = $null; $U.CopyBtn.Text = "Copy details"; $U.Lost = @()
    $U.NoteL.Text = ""; $U.NoteL.ForeColor = $U.Amber
    $U.StepT0 = Get-Date; $U.RunT0 = Get-Date; $U.ExitSeen = 0; $U.LastSnapT = [Environment]::TickCount
    Set-WorkMode "run"
    $U.Sub.Text = "Installing. You can keep using your PC."
    $psi = New-Object Diagnostics.ProcessStartInfo (Join-Path $PSHOME "powershell.exe")
    $psi.Arguments = '-NoProfile -ExecutionPolicy Bypass -Command "iex ([IO.File]::ReadAllText($env:GT5_INSTALLER))"'
    $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = $Utf8; $psi.StandardErrorEncoding = $Utf8
    $psi.EnvironmentVariables["GT5_INSTALLER_ROLE"] = "engine"
    foreach ($kv in @(@("GT5_INSTALL_GAME", $U.Choice.Game), @("GT5_INSTALL_FIRMWARE", $U.Choice.Firmware), @("GT5_INSTALL_DEST", $U.Choice.Dest), @("GT5_INSTALL_ACCEPTED", $U.Accepted))) {
        if ($kv[1]) { $psi.EnvironmentVariables[$kv[0]] = $kv[1] } elseif ($psi.EnvironmentVariables.ContainsKey($kv[0])) { $psi.EnvironmentVariables.Remove($kv[0]) }
    }
    Write-UiLog ""; Write-UiLog "---- Install started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss'): game '$($U.Choice.Game)', firmware '$($U.Choice.Firmware)', folder '$($U.Choice.Dest)'"
    Add-Detail "Installing to $($U.Choice.Dest)"
    try { $U.Proc = [Diagnostics.Process]::Start($psi) }
    catch { $U.Proc = $null; Show-Failed "The installer couldn't start its work ($($_.Exception.Message))."; return }
    $U.Streams = @($U.Proc.StandardOutput, $U.Proc.StandardError)
    $U.Reads = @($U.Streams[0].ReadLineAsync(), $U.Streams[1].ReadLineAsync())
}
function Restart-Install {
    $U = $script:U
    # A stop about one of the three choices goes back to them; anything else carries on at once.
    if ($U.StopText -match '(?i)\bchoose\b') { Show-Page "pick"; & $U.Check; return }
    Add-Detail ""; Add-Detail "---- Trying again"
    & $U.Check
    if ($U.Dest -and $U.Dest.Ok) { $U.Base = $U.Dest.Full }
    elseif ($U.Dest) { Show-Failed $U.Dest.Text; return }
    Start-Engine
}
# Stops the engine and everything it started (git, Python, the compiler, RPCS3), then tidies what a cut
# could leave behind: a mounted .iso, git's lock files.
function Stop-Engine {
    $U = $script:U
    $p = $U.Proc
    if ($p -and -not $p.HasExited) {
        try {
            $k = New-Object Diagnostics.ProcessStartInfo "taskkill.exe", "/T /F /PID $($p.Id)"
            $k.UseShellExecute = $false; $k.CreateNoWindow = $true
            $kp = [Diagnostics.Process]::Start($k); [void]$kp.WaitForExit(15000)
        } catch {}
        try { [void]$p.WaitForExit(5000) } catch {}
    }
    $U.Proc = $null
    if ($U.Mounted) { Dismount-Iso $U.Mounted; $U.Mounted = $null }
    Remove-GitLocks $U.Base 0
}
function Request-Cancel {
    $U = $script:U
    if (-not $U.Proc) { return $true }
    $sure = $true
    if (-not $env:GT5_INSTALL_TESTCANCEL) {
        $sure = ([Windows.Forms.MessageBox]::Show($U.Form, "Stop the installation?`r`n`r`nWhat's done so far is kept: run the installer again (or press Try again) to carry on from here.", "GT5 Recomp - installer", "YesNo", "Question") -eq "Yes")
    }
    if (-not $sure -or -not $U.Proc) { return $false }
    $U.Cancelled = $true
    Write-UiLog "Stopped from the window ($(Get-Date -Format HH:mm:ss))."
    Stop-Engine
    if ($U.Cur -ge 1) { $U.St[$U.Cur - 1] = "stop"; $U.StNote[$U.Cur - 1] = "stopped" }
    $U.Now = "Stopped."; $U.Pct = -1; $U.Shown = -1; $U.SubText = ""; $U.Hint = ""
    $U.NoteL.ForeColor = $U.Amber
    $U.NoteL.Text = "Stopped. It's safe to run the installer again (or press Try again): it carries on where it stopped."
    Set-WorkMode "stop"
    $U.Sub.Text = "Stopped. Nothing is lost: it carries on from here next time."
    if ($U.Page -ne "work") { Show-Page "work" } else { Request-Snap "stopped" }
    return $true
}
function Show-Failed([string]$msg) {
    $U = $script:U
    if ($U.Cur -ge 1 -and $U.St[$U.Cur - 1] -eq "run") { $U.St[$U.Cur - 1] = "fail"; $U.StNote[$U.Cur - 1] = "stopped here" }
    $U.Now = "Stopped: something needs fixing."; $U.Pct = -1; $U.Shown = -1; $U.SubText = ""; $U.Hint = ""
    $m = $msg.Trim()
    if ($m.Length -gt 0) { $m = $m.Substring(0, 1).ToUpper() + $m.Substring(1) }
    $U.NoteL.ForeColor = $U.Bad
    if ($m -notmatch '(?i)again') { $m += " Then press Try again: what's done is kept." }
    $U.NoteL.Text = $m
    Set-WorkMode "fail"
    $U.Sub.Text = "Stopped. Nothing is lost: it carries on from here once that's fixed."
    if ($U.Page -ne "work") { Show-Page "work" } else { Request-Snap "failed" }
    if (-not $U.SnapDir) { $U.Form.Activate() }
}

function On-EngineLine([string]$l) {
    $U = $script:U
    if ($l.Length -gt 0 -and $l[0] -eq [char]0xFEFF) { $l = $l.Substring(1) }
    if (-not $l.StartsWith("@@gt5`t")) { Add-Detail $l; Write-UiLog $l; return }
    $f = $l.Split("`t")
    $now = Get-Date
    switch ($f[1]) {
        "step" {
            $n = [int]$f[2]
            if ($U.Cur -ge 1 -and $U.St[$U.Cur - 1] -eq "run") { $U.St[$U.Cur - 1] = "done"; $U.StNote[$U.Cur - 1] = Format-Span ($now - $U.StepT0).TotalSeconds }
            $U.Cur = $n; $U.St[$n - 1] = "run"; $U.StepT0 = $now
            $U.Now = $StepTitles[$n - 1]; $U.Hint = $f[3]; $U.Pct = -1; $U.SubText = ""; $U.EtaBase = $null
            $h = "[$n/$Total] $($StepTitles[$n - 1])"
            Add-Detail ""; Add-Detail $h; Write-UiLog ""; Write-UiLog "$h  ($(Get-Date -Format HH:mm:ss))"
            Request-Snap "step$n"
        }
        "end" {
            $n = [int]$f[2]
            $U.St[$n - 1] = $f[3]; $U.StNote[$n - 1] = $f[4]
            Add-Detail "  $($f[4])"; Write-UiLog "  $($f[4])"
        }
        "now" {
            $U.Now = $f[2]; if ($f.Count -gt 3 -and $f[3]) { $U.Hint = $f[3] }
            $U.SubText = ""; $U.EtaBase = $null
            Add-Detail "  $($f[2])..."; Write-UiLog "  $($f[2])..."
        }
        "pct" {
            $p = [double]::Parse($f[2], [Globalization.CultureInfo]::InvariantCulture)
            if ($p -ge 0 -and ($null -eq $U.EtaBase -or $p -lt $U.Pct)) { $U.EtaBase = @{ P = $p; T = $now } }
            $U.Pct = $p; $U.SubText = if ($f.Count -gt 3) { $f[3] } else { "" }
        }
        "note" { $U.NoteL.ForeColor = $U.Amber; $U.NoteL.Text = $f[2]; Add-Detail "  $($f[2])"; Write-UiLog "  Note: $($f[2])" }
        "stop" { $U.StopText = $f[2]; Add-Detail "Stopped: $($f[2])"; Write-UiLog ""; Write-UiLog "Stopped: $($f[2])" }
        "done" { $U.Done = @{ Base = $f[2]; Kind = $f[3] }; Write-UiLog "Done ($($f[3])): $($f[2])" }
        "lost" { $U.Lost = @($U.Lost) + $f[2]; Add-Detail "  $($f[2])"; Write-UiLog "  Left out: $($f[2])" }
        "mount" { $U.Mounted = $f[2] }
        "unmount" { $U.Mounted = $null }
    }
}
function On-EngineExit {
    $U = $script:U
    $code = 0; try { $code = $U.Proc.ExitCode } catch {}
    try { $U.Proc.Dispose() } catch {}
    $U.Proc = $null
    $U.Mounted = $null   # the engine dismounts it itself when it ends normally
    Write-UiLog "(the work ended with code $code, $(Get-Date -Format HH:mm:ss))"
    if ($U.Done) {
        if ($U.Cur -ge 1 -and $U.St[$U.Cur - 1] -eq "run") { $U.St[$U.Cur - 1] = "done" }
        for ($i = 0; $i -lt $Total; $i++) { if ($U.St[$i] -eq "wait") { $U.St[$i] = "skip"; $U.StNote[$i] = "not run" } }
        Show-Finish $U.Done.Base $U.Done.Kind
        return
    }
    $msg = if ($U.StopText) { $U.StopText } else { "The installer's work ended unexpectedly (code $code). Details says where." }
    Show-Failed $msg
}

function On-Tick {
    $U = $script:U
    $p = $U.Proc
    if ($p) {
        $n = 0
        for ($k = 0; $k -lt 2; $k++) {
            while ($n -lt 400) {
                $t = $U.Reads[$k]
                if ($null -eq $t -or -not $t.IsCompleted) { break }
                $line = $null; try { $line = $t.Result } catch {}
                if ($null -eq $line) { $U.Reads[$k] = $null; break }
                $U.Reads[$k] = $U.Streams[$k].ReadLineAsync()
                On-EngineLine $line
                $n++
            }
        }
        if ($U.Proc -and $U.Proc.HasExited) {
            if ($null -eq $U.Reads[0] -and $null -eq $U.Reads[1]) { On-EngineExit }
            elseif ($U.ExitSeen -eq 0) { $U.ExitSeen = [Environment]::TickCount }
            elseif (([Environment]::TickCount - $U.ExitSeen) -gt 3000) { On-EngineExit }
        }
    }
    # The moving parts: the running step's circle, the bar, the times.
    $U.Spin = ($U.Spin + 12) % 360
    $U.Anim += (S 5)
    if ($U.Pct -lt 0) { $U.Shown = -1 }
    elseif ($U.Shown -lt 0 -or $U.Pct -lt $U.Shown) { $U.Shown = $U.Pct }
    else { $U.Shown += ($U.Pct - $U.Shown) * 0.3 }
    if ($U.Page -eq "work") {
        if ($U.NowL.Text -ne $U.Now) { $U.NowL.Text = $U.Now }
        $sub = $U.SubText
        if ($U.Proc -and $U.EtaBase -and $U.Pct -gt $U.EtaBase.P + 1) {
            $el = ((Get-Date) - $U.EtaBase.T).TotalSeconds
            if ($el -gt 15) {
                $left = $el * (100 - $U.Pct) / ($U.Pct - $U.EtaBase.P)
                $eta = if ($left -lt 90) { "about a minute left" } elseif ($left -lt 3600) { "about $([int][math]::Round($left / 60)) min left" } else { "about $([math]::Round($left / 3600, 1)) h left" }
                $sub = if ($sub) { "$sub - $eta" } else { $eta }
            }
        }
        if ($U.Proc -and $U.Pct -ge 0) { $sub = if ($sub) { "$([int]$U.Pct)%  -  $sub" } else { "$([int]$U.Pct)%" } }
        if ($U.SubL.Text -ne $sub) { $U.SubL.Text = $sub }
        $hint = if ($U.Proc -and $U.Hint) { "How long: $($U.Hint.Substring(0, 1).ToLower())$($U.Hint.Substring(1))" } else { "" }
        if ($U.HintL.Text -ne $hint) { $U.HintL.Text = $hint }
        if ($U.Proc -and $U.RunT0) {
            $tt = "this step $(Format-Span ((Get-Date) - $U.StepT0).TotalSeconds), in all $(Format-Span ((Get-Date) - $U.RunT0).TotalSeconds)"
            if ($U.TimeL.Text -ne $tt) { $U.TimeL.Text = $tt }
            if ($U.Cur -ge 1 -and $U.St[$U.Cur - 1] -eq "run") { $U.StNote[$U.Cur - 1] = Format-Span ((Get-Date) - $U.StepT0).TotalSeconds }
        }
        if ($U.Steps.Visible) { $U.Steps.Invalidate() }
        $U.Bar.Invalidate()
        if ($U.Details.Visible -and $U.DetDirty -and ([Environment]::TickCount - $U.DetT) -gt 300) { Update-Details }
    }
    # Test hooks.
    if ($U.SnapDue -and [Environment]::TickCount -ge $U.SnapDue) {
        $name = $U.SnapName; $U.SnapDue = 0
        Save-Snap $name
        if ($name -eq "pick" -and $env:GT5_INSTALL_AUTOGO -and -not $U.AutoWent) { $U.AutoWent = $true; Start-Install }
        elseif ($name -eq "failed" -and -not $U.Details.Visible) { Show-Details $true; Update-Details; Request-Snap "failed-details" }
        elseif ($env:GT5_INSTALL_AUTOCLOSE -and ($name -eq "fin" -or (($name -eq "failed-details" -or $name -eq "stopped" -or $name -eq "work") -and -not $U.Proc -and $U.Mode -ne "run"))) { $U.Form.Close() }
    }
    if ($U.SnapDir -and $U.Proc -and $U.Page -eq "work" -and ([Environment]::TickCount - $U.LastSnapT) -gt 20000) { $U.LastSnapT = [Environment]::TickCount; Save-Snap "work-step$($U.Cur)" }
    if ($env:GT5_INSTALL_TESTCANCEL -and $U.Proc -and -not $U.TestCancelled -and ((Get-Date) - $U.RunT0).TotalSeconds -ge [double]$env:GT5_INSTALL_TESTCANCEL) {
        $U.TestCancelled = $true; [void](Request-Cancel)
    }
}

# =====================================================================================================
if ($Role -eq "lib") { return }   # testing: only the functions above, nothing runs
if ($Role -eq "ui") {
    try { Show-Window }
    catch {
        $m = "The installer window hit an error: $($_.Exception.Message) (line $($_.InvocationInfo.ScriptLineNumber))."
        try { [IO.File]::AppendAllText($LogPath, "`r`n$m`r`n") } catch {}
        try { [void][Windows.Forms.MessageBox]::Show("$m`r`n`r`nThe log is $LogPath. Running GT5-Installer.bat with GT5_INSTALL_NOGUI=1 works without the window.", "GT5 Recomp - installer") } catch {}
        if ($script:U.Proc) { Stop-Engine }
    }
    exit 0
}
Invoke-Engine
