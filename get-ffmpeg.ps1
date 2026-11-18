# Gets FFmpeg, which plays the game's movies, into the ffmpeg folder of this install. setup.ps1 and update.ps1 run it;
# nothing has to be done by hand.
#   powershell -ExecutionPolicy Bypass -File get-ffmpeg.ps1
#
# What it fetches: one fixed release of BtbN's FFmpeg builds for Windows (the LGPL build with its shared libraries,
# about 75 MB), a month-end build (those are kept for about two years; the daily ones only for weeks), checked against
# the SHA-256 below. It is unpacked under the folder name GT5\CMakeLists.txt looks for, so an install made with the
# rolling "latest" build before this keeps working as it is.
#
# FFmpeg is optional: without it the game runs and only its movies don't play. So a failed download never stops an
# install or an update: this says so (exit 1), the build goes on without movies, and the next update tries again.
# When it does arrive later, the build folder is told to configure again (build.ninja goes), so the next build
# switches the movie decoder on (only the runtime is rebuilt).
$ErrorActionPreference = "Continue"
$base = $PSScriptRoot
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
$Pin = @{ Version = "9.0.2"; MB = 75; Size = 76576818
          Url = "https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-10-31-12-59/ffmpeg-n9.0.2-39-g7adfbe40dc-win64-lgpl-shared-9.0.zip"
          Sha256 = "e907b01fa4783913151237e37df2a10fa97d99a1598a021bf59d4ae62ffed4c0" }
$Folder = "ffmpeg-n9.0-latest-win64-lgpl-shared-9.0"     # GT5\CMakeLists.txt, GT5_FFMPEG_DIR
$root = Join-Path $base "ffmpeg"
$dir = Join-Path $root $Folder
function Stop-Here([string]$m) { Write-Host "FFmpeg: not added this time: $m" -ForegroundColor Yellow; exit 1 }
function Test-There { return (Test-Path -LiteralPath (Join-Path $dir "bin\avcodec-63.dll")) -and (Test-Path -LiteralPath (Join-Path $dir "include\libavcodec\avcodec.h")) }

if (Test-There) { Write-Host "FFmpeg: already in $dir"; exit 0 }

$work = Join-Path $root "download.tmp"
Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $work | Out-Null
$zip = Join-Path $work "ffmpeg.zip"
# (setup.ps1's and the installer's progress read this line.)
Write-Host "downloading FFmpeg $($Pin.Version) (about $($Pin.MB) MB)..."
if (Get-Command curl.exe -ErrorAction SilentlyContinue) { & curl.exe -sSL --fail --retry 3 -o $zip $Pin.Url }
if (-not (Test-Path -LiteralPath $zip) -or (Get-Item -LiteralPath $zip).Length -ne $Pin.Size) {
    # curl.exe doesn't use the Windows proxy settings; Invoke-WebRequest does.
    Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
    try { $ProgressPreference = "SilentlyContinue"; Invoke-WebRequest -Uri $Pin.Url -OutFile $zip -UseBasicParsing -ErrorAction Stop } catch { }
}
if (-not (Test-Path -LiteralPath $zip)) { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue; Stop-Here "the download didn't finish (no internet connection?)." }
$h = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
if ($h -ne $Pin.Sha256) {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    Stop-Here "the download isn't the expected file (SHA-256 $h); it was deleted."
}
try { Expand-Archive -LiteralPath $zip -DestinationPath (Join-Path $work "x") -Force -ErrorAction Stop }
catch { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue; Stop-Here "unpacking failed ($($_.Exception.Message))." }
$got = Get-ChildItem -LiteralPath (Join-Path $work "x") -Directory | Select-Object -First 1
if (-not $got -or -not (Test-Path -LiteralPath (Join-Path $got.FullName "bin\avcodec-63.dll"))) {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    Stop-Here "the unpacked files aren't what was expected."
}
Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
Move-Item -LiteralPath $got.FullName -Destination $dir
Set-Content -LiteralPath (Join-Path $dir "gt5-pin.txt") -Value "$($Pin.Url)`r`nSHA-256 $($Pin.Sha256)" -Encoding ascii
Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
if (-not (Test-There)) { Stop-Here "the files didn't end up in $dir." }
# A build folder configured without FFmpeg configures again at the next build (GT5\scripts\build.cmd does that when
# build.ninja is missing), so the movie decoder is built in.
$cache = Join-Path $base "GT5\build\CMakeCache.txt"
if ((Test-Path -LiteralPath $cache) -and (Select-String -LiteralPath $cache -Pattern '^PS3RECOMP_FFMPEG_RUNTIME:BOOL=OFF' -Quiet)) {
    Remove-Item -LiteralPath (Join-Path $base "GT5\build\build.ninja") -Force -ErrorAction SilentlyContinue
}
Write-Host "FFmpeg $($Pin.Version): ready in $dir"
exit 0
