# Checks the game's data in GT5\vfs\PS3_GAME before a build or a run, and puts split files back together.
# A PS3 dump made to a FAT32 USB drive splits every file over 4 GB into pieces named <file>.66600,
# <file>.66601, ... GT5's GT.VOL (about 12 GB, all the game's data) is one of them. The game only opens
# GT.VOL itself, so with the pieces it finds nothing and shows a black screen (#12).
# setup.ps1 and build_run.ps1 run this. Exit codes: 0 fine (or nothing to check), 2 GT.VOL missing.
param([string]$Gt5 = "")
if (-not $Gt5) { $Gt5 = Split-Path $PSScriptRoot -Parent }
$game = Join-Path $Gt5 "vfs\PS3_GAME"
$usr = Join-Path $game "USRDIR"
if (-not (Test-Path -LiteralPath $usr)) { exit 0 }   # no game copied yet: setup says so itself

foreach ($first in @(Get-ChildItem -LiteralPath $game -Recurse -File -Filter "*.66600" -ErrorAction SilentlyContinue)) {
    $whole = $first.FullName.Substring(0, $first.FullName.Length - ".66600".Length)
    $parts = @()
    for ($i = 0; $i -lt 100; $i++) {
        $p = Get-Item -LiteralPath ("{0}.666{1:D2}" -f $whole, $i) -ErrorAction SilentlyContinue
        if (-not $p) { break }
        $parts += $p
    }
    $total = ($parts | Measure-Object Length -Sum).Sum
    $name = $whole.Substring($game.Length + 1)
    if ((Test-Path -LiteralPath $whole) -and (Get-Item -LiteralPath $whole).Length -eq $total) {
        Write-Host "  $name is already whole; removing its $($parts.Count) pieces."
        $parts | Remove-Item -Force -ErrorAction SilentlyContinue
        continue
    }
    $drive = [IO.Path]::GetPathRoot($whole)
    $free = (New-Object IO.DriveInfo $drive).AvailableFreeSpace
    if ($free -lt $total + 256MB) {
        Write-Host "  $name is in $($parts.Count) pieces (a dump made to a FAT32 drive), and joining them needs" -ForegroundColor Yellow
        Write-Host "  $([math]::Ceiling($total / 1GB)) GB free on $drive. Free some space and run this again." -ForegroundColor Yellow
        continue
    }
    Write-Host "  $name is in $($parts.Count) pieces (a dump made to a FAT32 drive): joining them ($([math]::Round($total / 1GB, 1)) GB, a few minutes)..."
    $tmp = "$whole.joining"
    try {
        $out = [IO.File]::Open($tmp, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try {
            foreach ($p in $parts) {
                $in = [IO.File]::OpenRead($p.FullName)
                try { $in.CopyTo($out, 8MB) } finally { $in.Dispose() }
            }
        } finally { $out.Dispose() }
        if ((Get-Item -LiteralPath $tmp).Length -ne $total) { throw "the joined file has the wrong size" }
        if (Test-Path -LiteralPath $whole) { Remove-Item -LiteralPath $whole -Force }
        Move-Item -LiteralPath $tmp -Destination $whole
        $parts | Remove-Item -Force -ErrorAction SilentlyContinue
        Write-Host "  joined: $name" -ForegroundColor Green
    } catch {
        Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
        Write-Host "  couldn't join $name ($($_.Exception.Message)). Join the pieces by hand or copy the whole file from your dump." -ForegroundColor Yellow
    }
}

if (-not (Test-Path -LiteralPath (Join-Path $usr "GT.VOL"))) {
    Write-Host ""
    Write-Host "  GT5\vfs\PS3_GAME\USRDIR\GT.VOL is missing. It's the game's data (about 12 GB): without it the game" -ForegroundColor Yellow
    Write-Host "  starts to a black screen. Copy it from PS3_GAME\USRDIR in your disc dump into that folder." -ForegroundColor Yellow
    Write-Host "  (If your dump has GT.VOL.66600, GT.VOL.66601, ... copy those: they're joined automatically.)" -ForegroundColor Yellow
    exit 2
}
exit 0
