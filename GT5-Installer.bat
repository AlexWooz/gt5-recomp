<# : GT5-Installer.bat
@echo off
rem Gran Turismo 5 for PC - installer. Double-click it. It asks for your own copy of GT5 and the PS3
rem system software file, then installs everything else and builds the game. Run it again any time
rem to update, or to carry on if something stopped it: it skips what's already done.
title Gran Turismo 5 for PC - installer
set "GT5_INSTALLER=%~f0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "iex ([IO.File]::ReadAllText($env:GT5_INSTALLER))"
echo.
pause
goto :eof
: The rest is PowerShell. #>

# =====================================================================================================
# Gran Turismo 5 for PC installer
#
# What it does, in the install folder you choose:
#   1. installs Git, Python and Microsoft's C++ build tools (with Clang) if they're missing (winget)
#   2. downloads the port's code from GitHub (code only: nothing from the game)
#   3. downloads RPCS3, used only as a tool: it holds the keys that unlock your EBOOT.BIN and Sony's
#      firmware file, which can't be published here. The game itself never runs in RPCS3.
#   4. installs the PS3 system software from PS3UPDAT.PUP (for the system files the game uses)
#   5. copies your game (a disc folder or an .iso) into GT5\vfs
#   6. unlocks (decrypts) the game's EBOOT.BIN into GT5\input\EBOOT.elf
#   7. runs setup.ps1, which generates the game code and builds gt5.exe, then puts a shortcut on the
#      desktop
# =====================================================================================================
$ErrorActionPreference = "Continue"
$ProgressPreference = "SilentlyContinue"
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
$Total     = 7

function Say([string]$t, [string]$c = "Gray") { Write-Host $t -ForegroundColor $c }
function Step([int]$n, [string]$t) { Write-Host ""; Write-Host "[$n/$Total] $t" -ForegroundColor Cyan }
function Stop-Install([string]$t) {
    Write-Host ""
    Write-Host "Stopped: $t" -ForegroundColor Red
    Write-Host "Fix that and run GT5-Installer.bat again: it skips everything that's already done."
    Write-Host "Stuck? Report it here and attach the log ($script:LogPath): $HelpPage"
    exit 1
}

# Everything this window shows also goes to GT5-Installer.log next to the installer, for reports.
$script:LogPath = Join-Path $env:TEMP "GT5-Installer.log"
if ($env:GT5_INSTALLER) {
    $p = Join-Path (Split-Path $env:GT5_INSTALLER) "GT5-Installer.log"
    try { [IO.File]::AppendAllText($p, ""); $script:LogPath = $p } catch {}
}
try { Start-Transcript -Path $script:LogPath -Append | Out-Null } catch {}

# ---- Game, firmware and folder checks (used by the window and again before installing) -------------

# PARAM.SFO: the game's ID card (title and ID like BCES00569).
function Read-Sfo([string]$path) {
    try { $b = [IO.File]::ReadAllBytes($path) } catch { return $null }
    if ($b.Length -lt 20 -or $b[1] -ne 0x50 -or $b[2] -ne 0x53 -or $b[3] -ne 0x46) { return $null }
    $keyT = [BitConverter]::ToUInt32($b, 8); $dataT = [BitConverter]::ToUInt32($b, 12)
    $n = [BitConverter]::ToUInt32($b, 16); $r = @{}
    for ($i = 0; $i -lt $n; $i++) {
        $e = 20 + $i * 16
        $ko = [BitConverter]::ToUInt16($b, $e); $fmt = [BitConverter]::ToUInt16($b, $e + 2)
        $len = [BitConverter]::ToUInt32($b, $e + 4); $do = [BitConverter]::ToUInt32($b, $e + 12)
        $k = ""; $p = $keyT + $ko
        while ($p -lt $b.Length -and $b[$p] -ne 0) { $k += [char]$b[$p]; $p++ }
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

function Mount-Iso([string]$iso) {
    try {
        $img = Mount-DiskImage -ImagePath $iso -PassThru -ErrorAction Stop
        Start-Sleep -Milliseconds 800
        $letter = ($img | Get-Volume -ErrorAction Stop).DriveLetter
        if ($letter) { return "${letter}:\" }
    } catch {}
    return $null
}
function Dismount-Iso([string]$iso) { try { Dismount-DiskImage -ImagePath $iso -ErrorAction Stop | Out-Null } catch {} }

# What the picked game is: @{ Ok; Text; Ps3Game; TitleId; Iso }
function Get-GameInfo([string]$pick) {
    if (-not $pick) { return @{ Ok = $false; Text = "" } }
    $iso = $null; $folder = $pick
    if ($pick -match '\.iso$') {
        if (-not (Test-Path -LiteralPath $pick)) { return @{ Ok = $false; Text = "That file isn't there." } }
        $iso = $pick; $folder = Mount-Iso $pick
        if (-not $folder) { return @{ Ok = $false; Text = "Couldn't open this .iso. Double-click it in Explorer, then choose its drive with Folder... instead." } }
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
            # Other releases (Japan, Asia) haven't been tried: let them through, with a warning.
            $info.Ok = $true
            $info.Text = "Found: $title ($id). Not tried yet (Europe and North America work): it may work; please report how it goes."
        } else {
            $info.Text = "That's $title ($id), not Gran Turismo 5."
        }
        # A copy with a game update built in (APP_VER 02.xx, the disc says 01.00): its EBOOT.BIN is the
        # update's, which the port can't run yet. Let it through, but say so.
        $v = 0.0
        if ($info.Ok -and [double]::TryParse($appVer, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$v) -and $v -ge 2) {
            $info.Text = "Found: $title ($id), game version $appVer (an update is built in). The port needs the disc's own version 01.00; update versions (2.xx) don't work yet."
        }
    }
    if ($iso) { Dismount-Iso $iso }
    return $info
}

function Test-Pup([string]$f) {
    if (-not $f -or -not (Test-Path -LiteralPath $f -PathType Leaf)) { return "" }
    try {
        $s = [IO.File]::OpenRead($f); $h = New-Object byte[] 5; [void]$s.Read($h, 0, 5); $len = $s.Length; $s.Close()
    } catch { return "Couldn't read that file." }
    if ([Text.Encoding]::ASCII.GetString($h) -ne "SCEUF" -or $len -lt 100MB) { return "That isn't a PS3 system software file (PS3UPDAT.PUP)." }
    return "ok"
}

function Get-DestState([string]$d) {
    if (-not $d) { return @{ Ok = $false; Text = "Choose a folder." } }
    try { $full = [IO.Path]::GetFullPath($d) } catch { return @{ Ok = $false; Text = "That isn't a valid folder path." } }
    $root = [IO.Path]::GetPathRoot($full)
    if (-not (Test-Path -LiteralPath $root)) { return @{ Ok = $false; Text = "Drive $root isn't there." } }
    $free = [math]::Round((New-Object IO.DriveInfo($root)).AvailableFreeSpace / 1GB)
    $existing = (Test-Path -LiteralPath (Join-Path $full "setup.ps1")) -and (Test-Path -LiteralPath (Join-Path $full "GT5"))
    $st = @{ Ok = $true; Full = $full; Existing = $existing; Free = $free
             HasFw = (Test-Path -LiteralPath (Join-Path $full "GT5\dev_flash\sys"))
             HasGame = (Test-Path -LiteralPath (Join-Path $full "GT5\vfs\PS3_GAME\USRDIR")) }
    if ($existing) { $st.Text = "Already installed here: it will be updated and rebuilt. $free GB free." }
    elseif ((Test-Path -LiteralPath $full) -and (Get-ChildItem -LiteralPath $full -Force | Select-Object -First 1)) {
        $st.Ok = $false; $st.Text = "That folder isn't empty. Choose an empty or new folder."
    } elseif ($free -lt $NeedGB) {
        $st.Ok = $false; $st.Text = "Only $free GB free on $root; about $NeedGB GB is needed."
    } else { $st.Text = "$free GB free." }
    return $st
}

# ---- The window -------------------------------------------------------------------------------------
function Show-Installer($game0, $fw0, $dest0) {
    Add-Type -AssemblyName System.Windows.Forms, System.Drawing
    try { Add-Type -Namespace Gt5 -Name Dpi -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();'; [void][Gt5.Dpi]::SetProcessDPIAware() } catch {}
    [Windows.Forms.Application]::EnableVisualStyles()
    # The layout is in 100%-scaling pixels; S() scales it to the screen (fonts in points scale by themselves).
    $dpi = 96.0; try { $gr = [Drawing.Graphics]::FromHwnd([IntPtr]::Zero); $dpi = $gr.DpiX; $gr.Dispose() } catch {}
    $script:k = $dpi / 96.0; $fk = 1.0
    if ($env:GT5_INSTALL_UISCALE) { $want = [double]$env:GT5_INSTALL_UISCALE; $fk = $want / $script:k; $script:k = $want }   # testing
    function S([double]$v) { return [int][math]::Round($v * $script:k) }
    $red = [Drawing.Color]::FromArgb(176, 20, 30); $ok = [Drawing.Color]::FromArgb(20, 120, 50); $bad = [Drawing.Color]::FromArgb(180, 30, 30)
    $font = New-Object Drawing.Font("Segoe UI", (10 * $fk)); $bold = New-Object Drawing.Font("Segoe UI Semibold", (10 * $fk))

    $f = New-Object Windows.Forms.Form
    $f.AutoScaleMode = "None"
    $f.Text = "Gran Turismo 5 for PC - installer"; $f.ClientSize = New-Object Drawing.Size((S 760), (S 520))
    $f.FormBorderStyle = "FixedDialog"; $f.MaximizeBox = $false; $f.StartPosition = "CenterScreen"
    $f.Font = $font; $f.BackColor = [Drawing.Color]::White

    $band = New-Object Windows.Forms.Panel; $band.SetBounds(0, 0, (S 760), (S 78)); $band.BackColor = $red
    $t1 = New-Object Windows.Forms.Label; $t1.Text = "GRAN TURISMO 5  for PC"; $t1.ForeColor = [Drawing.Color]::White
    $t1.Font = New-Object Drawing.Font("Segoe UI Semibold", (20 * $fk)); $t1.AutoSize = $true; $t1.Location = New-Object Drawing.Point((S 22), (S 6))
    $t2 = New-Object Windows.Forms.Label; $t2.Text = "Builds the PC version from your own copy of the game. Point it at two things and press Install."
    $t2.ForeColor = [Drawing.Color]::FromArgb(255, 225, 225); $t2.AutoSize = $true; $t2.Location = New-Object Drawing.Point((S 25), (S 48))
    $band.Controls.AddRange(@($t1, $t2)); $f.Controls.Add($band)

    function New-Row([int]$y, [string]$title, [string]$hint) {
        $l = New-Object Windows.Forms.Label; $l.Text = $title; $l.Font = $bold; $l.AutoSize = $true; $l.Location = New-Object Drawing.Point((S 22), (S $y))
        $h = New-Object Windows.Forms.Label; $h.Text = $hint; $h.ForeColor = [Drawing.Color]::DimGray; $h.AutoSize = $true; $h.Location = New-Object Drawing.Point((S 22), (S ($y + 21)))
        $tb = New-Object Windows.Forms.TextBox; $tb.SetBounds((S 22), (S ($y + 46)), (S 470), (S 26))
        $st = New-Object Windows.Forms.Label; $st.SetBounds((S 22), (S ($y + 76)), (S 720), (S 22)); $st.AutoEllipsis = $true
        $f.Controls.AddRange(@($l, $h, $tb, $st))
        return @{ Box = $tb; Status = $st; Y = $y + 45 }
    }
    function New-Button([string]$text, [int]$x, [int]$y, [int]$w) {
        $b = New-Object Windows.Forms.Button; $b.Text = $text; $b.SetBounds((S $x), (S $y), (S $w), (S 28)); $b.FlatStyle = "System"; $f.Controls.Add($b); return $b
    }

    $rg = New-Row 92  "1.  Your Gran Turismo 5 game" "Your disc dump: its folder (the one with PS3_GAME in it) or an .iso file. Europe or US."
    $rf = New-Row 196 "2.  PS3 system software (PS3UPDAT.PUP)" "Sony's free PS3 update file. The game needs some PS3 system files from it."
    $rd = New-Row 300 "3.  Install to" "A new or empty folder. About $NeedGB GB of space."
    $bgF = New-Button "Folder..." 502 $rg.Y 110; $bgI = New-Button "ISO file..." 620 $rg.Y 118
    $bfB = New-Button "Browse..." 502 $rf.Y 110; $bfS = New-Button "Get it from Sony" 620 $rf.Y 118
    $bdB = New-Button "Browse..." 502 $rd.Y 110

    $note = New-Object Windows.Forms.Label; $note.SetBounds((S 22), (S 412), (S 716), (S 44)); $note.ForeColor = [Drawing.Color]::DimGray
    $note.Text = "Takes about an hour the first time. Anything missing (Git, Python, Microsoft's C++ build tools) is installed for you, and Windows may ask for permission. Nothing from the game is ever uploaded or shared."
    $f.Controls.Add($note)
    $go = New-Button "Install" 520 470 110; $go.Font = $bold; $cancel = New-Button "Cancel" 638 470 100
    $f.AcceptButton = $go; $f.CancelButton = $cancel

    $rg.Box.Text = $game0; $rf.Box.Text = $fw0; $rd.Box.Text = $dest0
    $script:ui = @{ Game = $null; Dest = $null; FwOk = $false }
    $check = {
        $d = Get-DestState $rd.Box.Text; $script:ui.Dest = $d
        $rd.Status.Text = $d.Text; $rd.Status.ForeColor = $(if ($d.Ok) { $ok } else { $bad })
        $gp = $rg.Box.Text.Trim('"', ' ')
        if ($gp -ne $script:ui.GamePick) { $script:ui.GamePick = $gp; $script:ui.Game = Get-GameInfo $gp }
        $g = $script:ui.Game
        if ($g -and $g.Ok) { $rg.Status.Text = $g.Text; $rg.Status.ForeColor = $ok }
        elseif ($g -and $g.Text) { $rg.Status.Text = $g.Text; $rg.Status.ForeColor = $bad }
        elseif ($d.Ok -and $d.HasGame) { $rg.Status.Text = "Not needed: the game is already in the install folder."; $rg.Status.ForeColor = $ok }
        else { $rg.Status.Text = "" }
        $p = Test-Pup $rf.Box.Text.Trim('"', ' ')
        $script:ui.FwOk = ($p -eq "ok")
        if ($p -eq "ok") { $rf.Status.Text = "Found: PS3 system software"; $rf.Status.ForeColor = $ok }
        elseif ($p) { $rf.Status.Text = $p; $rf.Status.ForeColor = $bad }
        elseif ($d.Ok -and $d.HasFw) { $rf.Status.Text = "Not needed: already installed in the install folder."; $rf.Status.ForeColor = $ok }
        else { $rf.Status.Text = "" }
        $gameReady = ($g -and $g.Ok) -or ((-not $rg.Box.Text) -and $d.Ok -and $d.HasGame)
        $fwReady = $script:ui.FwOk -or ((-not $rf.Box.Text) -and $d.Ok -and $d.HasFw)
        $go.Enabled = [bool]($d.Ok -and $gameReady -and $fwReady)
    }
    foreach ($r in @($rg, $rf, $rd)) { $r.Box.Add_Leave($check) }
    $bgF.Add_Click({ $dlg = New-Object Windows.Forms.FolderBrowserDialog; $dlg.Description = "Choose your GT5 disc dump (the folder with PS3_GAME in it)"
                     if ($dlg.ShowDialog($f) -eq "OK") { $rg.Box.Text = $dlg.SelectedPath; & $check } })
    $bgI.Add_Click({ $dlg = New-Object Windows.Forms.OpenFileDialog; $dlg.Filter = "Disc image (*.iso)|*.iso"; $dlg.Title = "Choose your GT5 .iso"
                     if ($dlg.ShowDialog($f) -eq "OK") { $rg.Box.Text = $dlg.FileName; & $check } })
    $bfB.Add_Click({ $dlg = New-Object Windows.Forms.OpenFileDialog; $dlg.Filter = "PS3 system software (PS3UPDAT.PUP)|*.PUP"; $dlg.Title = "Choose PS3UPDAT.PUP"
                     if ($dlg.ShowDialog($f) -eq "OK") { $rf.Box.Text = $dlg.FileName; & $check } })
    $bfS.Add_Click({ Start-Process $SonyPage })
    $bdB.Add_Click({ $dlg = New-Object Windows.Forms.FolderBrowserDialog; $dlg.Description = "Choose where to install (a new folder is made inside if this one isn't empty)"
                     if ($dlg.ShowDialog($f) -eq "OK") {
                         $p = $dlg.SelectedPath
                         if ((Get-ChildItem -LiteralPath $p -Force -ErrorAction SilentlyContinue | Select-Object -First 1) -and -not (Test-Path -LiteralPath (Join-Path $p "setup.ps1"))) { $p = Join-Path $p "GT5-PC" }
                         $rd.Box.Text = $p; & $check } })
    $go.Add_Click({ & $check; if ($go.Enabled) { $f.DialogResult = "OK"; $f.Close() } })
    $f.Add_Shown({ & $check; $f.Activate() })
    if ($f.ShowDialog() -ne "OK") { return $null }
    return @{ Game = $script:ui.Game; GamePick = $rg.Box.Text.Trim('"', ' '); Firmware = $rf.Box.Text.Trim('"', ' '); Dest = $script:ui.Dest }
}

# ---- Tools ---------------------------------------------------------------------------------------
function Update-Path {
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
    foreach ($p in @("$env:ProgramFiles\Git\cmd", "$env:LOCALAPPDATA\Programs\Python\Launcher", "$env:windir\py.exe")) {
        if ((Test-Path $p) -and ($env:Path -notlike "*$p*")) { $env:Path += ";$p" }
    }
}
function Install-WithWinget([string]$id, [string]$name, [string[]]$extra) {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Stop-Install "$name is missing, and this PC doesn't have winget to install it. Install 'App Installer' from the Microsoft Store (it adds winget), or install $name yourself (README, Requirements)."
    }
    Say "  installing $name (Windows may ask for permission)..." "Yellow"
    & winget install --id $id -e --source winget --accept-package-agreements --accept-source-agreements @extra
    Update-Path
}
function Find-Python {
    foreach ($c in @(@("py", "-3.11"), @("py", "-3.12"), @("py", "-3.13"), @("py", "-3"), @("python"))) {
        $exe = $c[0]; $a = @($c | Select-Object -Skip 1)
        if (-not (Get-Command $exe -ErrorAction SilentlyContinue)) { continue }
        $v = & $exe @a -c "import sys; print(1 if sys.version_info >= (3, 11) else 0)" 2>$null
        if ($v -eq "1") { return $true }
    }
    return $false
}
$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$VsParts = @("Microsoft.VisualStudio.Workload.VCTools", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
             "Microsoft.VisualStudio.Component.VC.Llvm.Clang", "Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset",
             "Microsoft.VisualStudio.Component.VC.CMake.Project")
function Find-VsClang {
    if (-not (Test-Path $VsWhere)) { return $null }
    $p = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath
    if ($p -and (Test-Path (Join-Path $p "VC\Tools\Llvm\x64\bin\clang-cl.exe"))) { return $p }
    return $null
}

# ---- RPCS3, as an unlocking tool ---------------------------------------------------------------------
function Get-Rpcs3([string]$dir) {
    $exe = Join-Path $dir "rpcs3.exe"
    if (Test-Path $exe) { return $exe }
    New-Item -ItemType Directory -Force $dir | Out-Null
    Say "  finding the newest RPCS3 build on GitHub..."
    try { $rel = Invoke-RestMethod "https://api.github.com/repos/RPCS3/rpcs3-binaries-win/releases/latest" -Headers @{ "User-Agent" = "gt5-pc-installer" } }
    catch { Stop-Install "couldn't reach GitHub to download RPCS3 ($($_.Exception.Message))." }
    $asset = $rel.assets | Where-Object { $_.name -like "*win64*.7z" } | Select-Object -First 1
    $sum = $rel.assets | Where-Object { $_.name -eq "$($asset.name).sha256" } | Select-Object -First 1
    if (-not $asset) { Stop-Install "couldn't find RPCS3's Windows download." }
    $arc = Join-Path $dir $asset.name
    Say "  downloading $($asset.name) ($([math]::Round($asset.size / 1MB)) MB)..."
    curl.exe -sSL -o $arc $asset.browser_download_url
    if ($sum) {
        $want = ((curl.exe -sSL $sum.browser_download_url) -split '\s+')[0]
        if ($want -and ((Get-FileHash $arc -Algorithm SHA256).Hash -ne $want.ToUpper())) { Remove-Item $arc; Stop-Install "the RPCS3 download was damaged (checksum). Try again." }
    }
    if (-not $env:GT5_INSTALL_NOTAR) { tar -xf $arc -C $dir 2>&1 | Out-Null }
    if (-not (Test-Path $exe)) {
        # Windows 10's tar can't read this archive (no LZMA): use 7-Zip, or 7-Zip's own small extractor.
        $sz = @("$env:ProgramFiles\7-Zip\7z.exe", "${env:ProgramFiles(x86)}\7-Zip\7z.exe") | Where-Object { (Test-Path $_) -and -not $env:GT5_INSTALL_NO7Z } | Select-Object -First 1
        if ($sz) { Say "  unpacking it with your 7-Zip..." } else {
            Say "  getting 7-Zip's extractor (7zr.exe) from 7-zip.org to unpack it..."
            $sz = Join-Path $dir "7zr.exe"
            curl.exe -sSL -o $sz "https://www.7-zip.org/a/7zr.exe"
        }
        & $sz x -y "-o$dir" $arc | Out-Null
    }
    Remove-Item $arc -ErrorAction SilentlyContinue
    if (-not (Test-Path $exe)) { Stop-Install "unpacking RPCS3 failed. Install 7-Zip from https://www.7-zip.org and run this again." }
    # No welcome box or pop-ups: it's only used from here.
    New-Item -ItemType Directory -Force (Join-Path $dir "GuiConfigs") | Out-Null
    $ini = "[main_window]`r`ninfoBoxEnabledWelcome=false`r`ninfoBoxEnabledInstallPUP=false`r`ninfoBoxEnabledInstallPKG=false`r`n"
    [IO.File]::WriteAllText((Join-Path $dir "GuiConfigs\CurrentSettings.ini"), $ini)
    return $exe
}

# RPCS3 installs firmware only through its window and asks two questions first; answer Yes to them
# (the file and the version), then wait until all the files are there.
function Install-Firmware([string]$rpcs3, [string]$pup) {
    Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
    $A = [Windows.Automation.AutomationElement]
    $flash = Join-Path (Split-Path $rpcs3) "dev_flash"
    $p = Start-Process -FilePath $rpcs3 -ArgumentList @("--installfw", "`"$pup`"") -PassThru -WindowStyle Minimized
    $cond = New-Object Windows.Automation.PropertyCondition($A::ProcessIdProperty, $p.Id)
    $yes = New-Object Windows.Automation.AndCondition(
        (New-Object Windows.Automation.PropertyCondition($A::ControlTypeProperty, [Windows.Automation.ControlType]::Button)),
        (New-Object Windows.Automation.PropertyCondition($A::NameProperty, "Yes")))
    $named = New-Object Windows.Automation.PropertyCondition($A::NameProperty, "RPCS3 Firmware Installer")
    $start = Get-Date; $last = -1; $stable = 0
    while (((Get-Date) - $start).TotalMinutes -lt 30) {
        if ($p.HasExited) { break }
        try {
            foreach ($w in $A::RootElement.FindAll([Windows.Automation.TreeScope]::Children, $cond)) {
                foreach ($d in @($w) + @($w.FindAll([Windows.Automation.TreeScope]::Descendants, $named))) {
                    if ($d.Current.Name -ne "RPCS3 Firmware Installer") { continue }
                    $b = $d.FindFirst([Windows.Automation.TreeScope]::Descendants, $yes)
                    if ($b) { $b.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke() }
                }
            }
        } catch {}
        Start-Sleep -Seconds 2
        $n = (Get-ChildItem $flash -Recurse -File -ErrorAction SilentlyContinue | Measure-Object).Count
        if ($n -ge 1000 -and $n -eq $last) { $stable++ } else { $stable = 0 }
        if ($n -ne $last) { Write-Host "`r  $n system files installed...   " -NoNewline }
        $last = $n
        if ($stable -ge 5) { break }
    }
    Write-Host ""
    if (-not $p.HasExited) { [void]$p.CloseMainWindow(); Start-Sleep -Seconds 3; if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
    return ($last -ge 1000)
}

# ---- Desktop shortcut, with the game's own icon from your disc ----------------------------------------
function New-Shortcut([string]$base) {
    $icon = Join-Path $base "tools\gt5.ico"
    $png = Join-Path $base "GT5\vfs\PS3_GAME\ICON0.PNG"
    if ((Test-Path $png) -and -not (Test-Path $icon)) {
        try {
            Add-Type -AssemblyName System.Drawing
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
    if (Test-Path $launcher) { $l.TargetPath = $launcher; $l.WorkingDirectory = $base; $l.WindowStyle = 1 }
    else { $l.TargetPath = Join-Path $base "GT5\play.bat"; $l.WorkingDirectory = Join-Path $base "GT5"; $l.WindowStyle = 7 }
    $l.Description = "Gran Turismo 5 for PC"
    if (Test-Path $icon) { $l.IconLocation = "$icon,0" }
    $l.Save()
    return $lnkPath
}

# =====================================================================================================
Write-Host ""
Write-Host "  GRAN TURISMO 5 for PC - installer" -ForegroundColor White
if (-not $env:GT5_INSTALL_NOGUI) { Write-Host "  Its window is open (it may be behind this one)." -ForegroundColor DarkGray }

$saved = @{}
try { $k = Get-ItemProperty -Path $RegKey -ErrorAction Stop; foreach ($n in "Game", "Firmware", "Dest") { $saved[$n] = $k.$n } } catch {}
$fw0 = $saved.Firmware
if (-not $fw0) {
    $fw0 = Get-ChildItem (Join-Path $env:USERPROFILE "Downloads") -Filter "PS3UPDAT.PUP" -Recurse -Depth 2 -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}
$dest0 = if ($saved.Dest) { $saved.Dest } else { "C:\Games\GT5-PC" }

if ($env:GT5_INSTALL_NOGUI) {
    # For testing without the window: GT5_INSTALL_GAME, GT5_INSTALL_FIRMWARE, GT5_INSTALL_DEST
    # (GT5_INSTALL_NOBUILD stops before the build, GT5_INSTALL_LNKDIR puts the shortcut elsewhere).
    $pickGame = $env:GT5_INSTALL_GAME
    $c = @{ Game = (Get-GameInfo $pickGame); GamePick = $pickGame; Firmware = $env:GT5_INSTALL_FIRMWARE; Dest = (Get-DestState $env:GT5_INSTALL_DEST) }
} else {
    $c = Show-Installer $saved.Game $fw0 $dest0
    if (-not $c) { Say "Cancelled: nothing was changed."; exit 0 }
}
$dest = $c.Dest; $base = $dest.Full; $gt5 = Join-Path $base "GT5"
if (-not $dest.Ok) { Stop-Install $dest.Text }
try {
    New-Item -Path $RegKey -Force | Out-Null
    Set-ItemProperty -Path $RegKey -Name Dest -Value $base
    if ($c.GamePick) { Set-ItemProperty -Path $RegKey -Name Game -Value $c.GamePick }
    if ($c.Firmware) { Set-ItemProperty -Path $RegKey -Name Firmware -Value $c.Firmware }
} catch {}
Say "Installing to $base" "White"

# ---- 1. Tools --------------------------------------------------------------------------------------
Step 1 "Tools: Git, Python, Microsoft C++ build tools"
Update-Path
if (Get-Command git -ErrorAction SilentlyContinue) { Say "  Git: ok" } else {
    Install-WithWinget "Git.Git" "Git" @("--silent")
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Stop-Install "Git didn't install. Install it from https://git-scm.com and run this again." }
}
if (Find-Python) { Say "  Python: ok" } else {
    Install-WithWinget "Python.Python.3.11" "Python 3.11" @("--silent")
    if (-not (Find-Python)) { Stop-Install "Python didn't install. Install Python 3.11 from https://www.python.org (tick 'Add python.exe to PATH') and run this again." }
}
if (Find-VsClang) { Say "  C++ build tools with Clang: ok" } else {
    $adds = ($VsParts | ForEach-Object { "--add $_" }) -join " "
    $any = if (Test-Path $VsWhere) { & $VsWhere -latest -products * -property installationPath } else { $null }
    if ($any) {
        # Visual Studio is there without the Clang tools: add them to it.
        Say "  adding the Clang tools to your Visual Studio (Windows may ask for permission; 5-15 minutes)..." "Yellow"
        $vsSetup = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\setup.exe"
        Start-Process -FilePath $vsSetup -ArgumentList "modify --installPath `"$any`" $adds --includeRecommended --passive --norestart" -Wait -Verb RunAs
    } else {
        Say "  Microsoft's C++ build tools are about 7 GB and take 10-20 minutes." "Yellow"
        Install-WithWinget "Microsoft.VisualStudio.2022.BuildTools" "Microsoft C++ Build Tools" @("--override", "--wait --passive --norestart $adds --includeRecommended")
    }
    if (-not (Find-VsClang)) { Stop-Install "the C++ build tools with Clang aren't installed. In the Visual Studio Installer, add 'Desktop development with C++' and 'C++ Clang tools for Windows', then run this again." }
}

# ---- 2. The port's code -------------------------------------------------------------------------------
Step 2 "The port's code (from GitHub)"
if ($dest.Existing) {
    Say "  already here"
} else {
    New-Item -ItemType Directory -Force (Split-Path $base) | Out-Null
    git clone --quiet $RepoUrl $base
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path (Join-Path $base "setup.ps1"))) { Stop-Install "downloading the code from GitHub failed (is the internet connection up?)." }
    Say "  downloaded"
}
$tools = Join-Path $base "tools"
New-Item -ItemType Directory -Force $tools | Out-Null

# ---- 3. RPCS3 (only to unlock files) --------------------------------------------------------------
$needFw = -not (Test-Path (Join-Path $gt5 "dev_flash\sys\external"))
$needElf = -not (Test-Path (Join-Path $gt5 "input\EBOOT.elf")) -or [bool]($c.Game -and $c.Game.Ok)
Step 3 "RPCS3, used only to unlock your game's EBOOT and Sony's firmware file"
$rpcs3 = $null
if ($needFw -or $needElf) { $rpcs3 = Get-Rpcs3 (Join-Path $tools "rpcs3"); Say "  ready" } else { Say "  not needed" }

# ---- 4. PS3 system software ---------------------------------------------------------------------------
Step 4 "PS3 system software"
if (-not $needFw) { Say "  already installed" } else {
    if ((Test-Pup $c.Firmware) -ne "ok") { Stop-Install "choose PS3UPDAT.PUP (Sony's PS3 system software file) in the installer window." }
    Say "  installing it with RPCS3 (a minimized RPCS3 window opens and closes; a few minutes)..."
    if (-not (Install-Firmware $rpcs3 $c.Firmware)) { Stop-Install "installing the PS3 system software didn't finish. Run the installer again." }
    robocopy (Join-Path $tools "rpcs3\dev_flash") (Join-Path $gt5 "dev_flash") /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8 -or -not (Test-Path (Join-Path $gt5 "dev_flash\sys\external"))) { Stop-Install "copying the system files into GT5\dev_flash failed." }
    Say "  installed"
}

# ---- 5. The game files ----------------------------------------------------------------------------
Step 5 "Your game files"
$vfsGame = Join-Path $gt5 "vfs\PS3_GAME"
if ($c.Game -and $c.Game.Ok) {
    $src = $null
    if ($c.Game.Iso) {
        $drive = Mount-Iso $c.Game.Iso
        if (-not $drive) { Stop-Install "couldn't open the .iso. Double-click it in Explorer and choose its drive as the game folder instead." }
        $src = Find-Ps3Game $drive
    } else { $src = $c.Game.Ps3Game }
    $same = $false
    try { $same = ((Resolve-Path -LiteralPath $src).Path.TrimEnd('\') -eq [IO.Path]::GetFullPath($vfsGame).TrimEnd('\')) } catch {}
    if ($same) { Say "  already in place" } else {
        $gb = [math]::Round(((Get-ChildItem -LiteralPath $src -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum) / 1GB, 1)
        Say "  copying $gb GB from $src (a few minutes)..."
        robocopy $src $vfsGame /E /MT:8 /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
        $rc = $LASTEXITCODE
        if ($c.Game.Iso) { Dismount-Iso $c.Game.Iso }
        if ($rc -ge 8) { Stop-Install "copying the game files failed (robocopy code $rc). Is there enough space?" }
        Say "  copied"
    }
} elseif (Test-Path (Join-Path $vfsGame "USRDIR")) { Say "  already in place" }
else { Stop-Install "choose your GT5 game (its folder or .iso) in the installer window." }

# ---- 6. Unlock EBOOT.BIN -----------------------------------------------------------------------------
Step 6 "Unlocking the game's executable (EBOOT.BIN -> EBOOT.elf)"
$elf = Join-Path $gt5 "input\EBOOT.elf"
$bin = Join-Path $vfsGame "USRDIR\EBOOT.BIN"
if (-not $needElf) { Say "  already done" } else {
    New-Item -ItemType Directory -Force (Split-Path $elf) | Out-Null
    $work = Join-Path $tools "decrypt"
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $work | Out-Null
    Copy-Item -LiteralPath $bin (Join-Path $work "EBOOT.BIN")
    $head = [IO.File]::ReadAllBytes((Join-Path $work "EBOOT.BIN"))[0..3]
    if ($head[0] -eq 0x7F -and $head[1] -eq 0x45) {
        Move-Item (Join-Path $work "EBOOT.BIN") $elf -Force   # already a plain ELF
    } else {
        $p = Start-Process -FilePath $rpcs3 -ArgumentList @("--no-gui", "--decrypt", "`"$(Join-Path $work 'EBOOT.BIN')`"") -PassThru -WindowStyle Hidden
        if (-not $p.WaitForExit(300000)) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
        $out = Join-Path $work "EBOOT.elf"
        $okElf = (Test-Path $out) -and ([IO.File]::ReadAllBytes($out)[0] -eq 0x7F)
        if (-not $okElf) { Stop-Install "unlocking EBOOT.BIN failed. If your game is an encrypted disc image (a 'Redump' .iso), decrypt the image first or use a folder dump of the disc." }
        Move-Item $out $elf -Force
    }
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
    $sha = (Get-FileHash $elf -Algorithm SHA1).Hash
    Say "  done (EBOOT.elf SHA-1 $sha)"
}

# ---- 7. Build -------------------------------------------------------------------------------------
Step 7 "Building the game (about 10 minutes of code generation, then 20-50 minutes of compiling)"
Push-Location $base
if ($dest.Existing -and (Test-Path (Join-Path $base ".git"))) {
    Say "  getting the newest version first..."
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $base "update.ps1") -NoBuild | Out-Host
}
$setupArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $base "setup.ps1"))
if ($env:GT5_INSTALL_NOBUILD) { $setupArgs += "-NoBuild" }
& powershell @setupArgs | Out-Host     # through Out-Host so the log gets setup's output too
$built = $LASTEXITCODE
Pop-Location
if ($built -ne 0) { Stop-Install "setup stopped (see its message above)." }
if ($env:GT5_INSTALL_NOBUILD) { Say "Stopped before building (GT5_INSTALL_NOBUILD)." "Yellow"; exit 0 }
if (-not (Test-Path (Join-Path $gt5 "build\gt5.exe"))) { Stop-Install "the build didn't produce GT5\build\gt5.exe (see the messages above)." }

$lnk = New-Shortcut $base
Write-Host ""
Write-Host "  Done! Gran Turismo 5 is installed." -ForegroundColor Green
Write-Host "  Start it with 'Gran Turismo 5 (PC)' on your desktop: the launcher, with settings (resolution,"
Write-Host "  full screen, wheel, language), Play and Updates. (Or $gt5\play.bat directly.)"
Write-Host "  Keyboard: Z = Cross, X = Circle, arrows, Enter = Start. Pads work too. More in README.md."
Write-Host "  To update later: the launcher's Updates page, or double-click Update.bat in $base."
if (-not $env:GT5_INSTALL_NOGUI) {
    $a = Read-Host "  Start it now? (Y/N)"
    if ($a -match '^[Yy]') { Start-Process -FilePath $lnk }
}
