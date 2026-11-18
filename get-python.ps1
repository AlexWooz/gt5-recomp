# Gets the Python the port's tools run on, into tools\python of this folder. setup.ps1 and
# GT5-Installer.bat run it; nothing has to be installed by hand.
#   powershell -ExecutionPolicy Bypass -File get-python.ps1
#
# Why its own copy: the tools that turn the game's code into C++ are Python programs, and they are tested
# on one Python version. A PC's own Python can be older, newer, a pre-release or broken (one install
# stopped with an error from inside Python itself), and installing one changes the PC. This copy is
# python.org's own build of that one version, in this folder only: nothing is installed in Windows, no
# administrator rights, PATH is left alone; delete tools\python to remove it.
#
# What it fetches: the "python" package the Python Software Foundation publishes on nuget.org (17 MB,
# 43 MB unpacked: the full Python with pip and venv). The download is checked against the SHA-256
# below, and python.exe and python311.dll must carry the Python Software Foundation's signature.
$ErrorActionPreference = "Continue"
$base = $PSScriptRoot
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
$Pin = @{ Version = "3.11.9"; MB = 17
          Url = "https://www.nuget.org/api/v2/package/python/3.11.9"
          Sha256 = "9283876d58c017e0e846f95b490da3bca0fc0a6ee1134b2870677cfb7eec3c67" }
$dir = Join-Path $base "tools\python"
$exe = Join-Path $dir "python.exe"
function Stop-Here([string]$m) { Write-Host "get-python stopped: $m" -ForegroundColor Red; exit 1 }
function Test-Signed([string]$p) {
    $s = Get-AuthenticodeSignature -LiteralPath $p
    return ($s.Status -eq "Valid" -and "$($s.SignerCertificate.Subject)" -match "O=Python Software Foundation")
}
function Get-Ver([string]$p) { return "$(& $p -I -c "import sys; print('%d.%d.%d' % sys.version_info[:3])" 2>$null)".Trim() }

if ((Test-Path -LiteralPath $exe) -and (Get-Ver $exe) -eq $Pin.Version) { Write-Host "Python $($Pin.Version): already in $dir"; exit 0 }

$work = Join-Path $base "tools\python.tmp"
Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $work | Out-Null
$zip = Join-Path $work "python.zip"     # a .nupkg is a zip
Write-Host "Python $($Pin.Version): downloading python.org's build ($($Pin.MB) MB) into $dir..."
if (Get-Command curl.exe -ErrorAction SilentlyContinue) { & curl.exe -sSL --fail --retry 3 -o $zip $Pin.Url }
if (-not (Test-Path -LiteralPath $zip)) {
    try { $ProgressPreference = "SilentlyContinue"; Invoke-WebRequest -Uri $Pin.Url -OutFile $zip -UseBasicParsing -ErrorAction Stop } catch { }
}
if (-not (Test-Path -LiteralPath $zip)) { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue; Stop-Here "the download failed. Check the internet connection and run this again." }
$h = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
if ($h -ne $Pin.Sha256) {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    Stop-Here "the download isn't the expected file (SHA-256 $h). It was deleted. Run this again, and report it if it happens again."
}
try { Expand-Archive -LiteralPath $zip -DestinationPath (Join-Path $work "x") -Force -ErrorAction Stop }
catch { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue; Stop-Here "unpacking failed ($($_.Exception.Message))." }
$got = Join-Path $work "x\tools"
if (-not (Test-Path (Join-Path $got "python.exe")) -or -not (Test-Signed (Join-Path $got "python.exe")) -or -not (Test-Signed (Join-Path $got "python311.dll"))) {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    Stop-Here "the unpacked Python isn't signed by the Python Software Foundation. Nothing was kept; please report it."
}
Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
Move-Item -LiteralPath $got -Destination $dir
Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
if ((Get-Ver $exe) -ne $Pin.Version) { Stop-Here "the Python in $dir doesn't run. Delete that folder and run this again." }
Write-Host "Python $($Pin.Version): ready in $dir"
exit 0
