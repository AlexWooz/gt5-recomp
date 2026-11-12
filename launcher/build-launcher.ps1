# Builds GT5Launcher.exe (the settings / play / update window) in the install folder, with the C# compiler
# that comes with Windows (.NET Framework 4): nothing is downloaded. build.cmd runs this after every game
# build, so the launcher is rebuilt with each update; it can also be run by itself:
#   powershell -ExecutionPolicy Bypass -File launcher\build-launcher.ps1 [-Shortcut]
# -Shortcut also puts "Gran Turismo 5 (PC)" on the desktop. An existing shortcut of that name that starts
# this install's GT5\play.bat is switched to the launcher.
param([switch]$Shortcut)
$ErrorActionPreference = "Continue"
$root = Split-Path $PSScriptRoot -Parent
$src = Join-Path $PSScriptRoot "GT5Launcher.cs"
$exe = Join-Path $root "GT5Launcher.exe"
$csc = @("$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe", "$env:WINDIR\Microsoft.NET\Framework\v4.0.30319\csc.exe") |
       Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $csc) { Write-Host "launcher: .NET Framework 4 isn't installed, so the launcher can't be built (GT5\play.bat still works)." -ForegroundColor Yellow; exit 0 }

# The icon: the game's own, made from your disc's ICON0.PNG (the installer makes the same one).
$icon = Join-Path $root "tools\gt5.ico"
$png = Join-Path $root "GT5\vfs\PS3_GAME\ICON0.PNG"
if (-not (Test-Path $png)) { $png = Join-Path $root "vfs\PS3_GAME\ICON0.PNG" }   # a bare GT5 folder
if ((Test-Path $png) -and -not (Test-Path $icon)) {
    try {
        New-Item -ItemType Directory -Force (Split-Path $icon) | Out-Null
        Add-Type -AssemblyName System.Drawing
        $img = [Drawing.Image]::FromFile($png); $bmp = New-Object Drawing.Bitmap 256, 256
        $g = [Drawing.Graphics]::FromImage($bmp); $g.InterpolationMode = "HighQualityBicubic"; $g.Clear([Drawing.Color]::Transparent)
        $h = [int](256 * $img.Height / $img.Width); $g.DrawImage($img, 0, [int]((256 - $h) / 2), 256, $h)
        $ms = New-Object IO.MemoryStream; $bmp.Save($ms, [Drawing.Imaging.ImageFormat]::Png); $data = $ms.ToArray(); $img.Dispose()
        $o = New-Object IO.MemoryStream; $w = New-Object IO.BinaryWriter($o)
        $w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]1)
        $w.Write([byte]0); $w.Write([byte]0); $w.Write([byte]0); $w.Write([byte]0)
        $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$data.Length); $w.Write([uint32]22); $w.Write($data)
        [IO.File]::WriteAllBytes($icon, $o.ToArray())
    } catch {}
}

$tmp = Join-Path $env:TEMP ("GT5Launcher-" + [guid]::NewGuid().ToString("N") + ".exe")
$cscArgs = @("/nologo", "/target:winexe", "/optimize+", "/codepage:65001", "/out:$tmp",
             "/r:System.dll", "/r:System.Drawing.dll", "/r:System.Windows.Forms.dll", "/r:System.Web.Extensions.dll")
if (Test-Path $icon) { $cscArgs += "/win32icon:$icon" }
$out = & $csc @cscArgs $src 2>&1
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $tmp)) {
    Write-Host "launcher: building it failed (GT5\play.bat still works):" -ForegroundColor Yellow
    $out | ForEach-Object { Write-Host "  $_" }
    exit 0
}
try {
    Move-Item -LiteralPath $tmp -Destination $exe -Force -ErrorAction Stop
    Write-Host "launcher: built GT5Launcher.exe"
} catch {
    Remove-Item -LiteralPath $tmp -ErrorAction SilentlyContinue
    Write-Host "launcher: GT5Launcher.exe is open, so it wasn't replaced; close it and run update.ps1 -Force or this script again." -ForegroundColor Yellow
    exit 0
}

# The desktop shortcut.
try {
    $desk = [Environment]::GetFolderPath("Desktop")
    $lnkPath = Join-Path $desk "Gran Turismo 5 (PC).lnk"
    $ws = New-Object -ComObject WScript.Shell
    $make = $Shortcut
    if (Test-Path $lnkPath) {
        $old = $ws.CreateShortcut($lnkPath)
        $make = $make -or ($old.TargetPath -ieq (Join-Path $root "GT5\play.bat"))
    }
    if ($make) {
        $l = $ws.CreateShortcut($lnkPath)
        $l.TargetPath = $exe; $l.WorkingDirectory = $root; $l.WindowStyle = 1
        $l.Description = "Gran Turismo 5 for PC: settings, play, updates"
        if (Test-Path $icon) { $l.IconLocation = "$icon,0" }
        $l.Save()
        Write-Host "launcher: the desktop shortcut 'Gran Turismo 5 (PC)' opens the launcher"
    }
} catch {}
exit 0
