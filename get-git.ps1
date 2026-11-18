# Gets Git for the port's scripts when the PC has none, into tools\git of this folder. setup.ps1 runs it;
# GT5-Installer.bat does the same before the code is downloaded. Nothing has to be installed by hand.
#   powershell -ExecutionPolicy Bypass -File get-git.ps1
#
# What it fetches: MinGit, the official portable build of Git for Windows (github.com/git-for-windows, about
# 40 MB, 90 MB unpacked). It isn't installed: nothing goes in Windows, no administrator rights, PATH is left
# alone. setup.ps1, update.ps1 (and so the launcher's Updates page), add_vulkan.ps1 and add_dlss.ps1 use
# tools\git\cmd\git.exe when the PC has no Git of its own; delete tools\git to remove it.
# The download is checked against the SHA-256 below, and git.exe must carry the signature of Git for
# Windows' maintainer. The same pin is in GT5-Installer.bat ($GitPin).
$ErrorActionPreference = "Continue"
$base = $PSScriptRoot
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
$Pin = @{ Version = "2.56.1"; MB = 38
          Url = "https://github.com/git-for-windows/git/releases/download/v2.56.1.windows.1/MinGit-2.56.1-64-bit.zip"
          Sha256 = "4A92850B6E917DA4FD010669B614B55DCCF5B0EC333A534CC4BE4700DFEFDAFD"
          Signer = "CN=Johannes Schindelin" }
$dir = Join-Path $base "tools\git"
$exe = Join-Path $dir "cmd\git.exe"
function Stop-Here([string]$m) { Write-Host "get-git stopped: $m" -ForegroundColor Red; exit 1 }
function Get-Ver([string]$p) { return "$(& $p --version 2>$null)".Trim() }

if ((Test-Path -LiteralPath $exe) -and (Get-Ver $exe) -match [regex]::Escape($Pin.Version)) { Write-Host "Git $($Pin.Version): already in $dir"; exit 0 }

$work = Join-Path $base "tools\git.tmp"
Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $work | Out-Null
$zip = Join-Path $work "mingit.zip"
Write-Host "Git $($Pin.Version): downloading Git for Windows' MinGit ($($Pin.MB) MB) into $dir..."
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
try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, (Join-Path $work "x"))
} catch { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue; Stop-Here "unpacking failed ($($_.Exception.Message))." }
$got = Join-Path $work "x\cmd\git.exe"
$sig = if (Test-Path -LiteralPath $got) { Get-AuthenticodeSignature -LiteralPath $got } else { $null }
if (-not $sig -or $sig.Status -ne "Valid" -or "$($sig.SignerCertificate.Subject)" -notmatch [regex]::Escape($Pin.Signer)) {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    Stop-Here "the unpacked git.exe isn't signed by Git for Windows' maintainer. Nothing was kept; please report it."
}
Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
Move-Item -LiteralPath (Join-Path $work "x") -Destination $dir
Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
if (-not (Get-Ver $exe)) { Stop-Here "the Git in $dir doesn't run. Delete that folder and run this again." }
Write-Host "Git $($Pin.Version): ready in $dir ($(Get-Ver $exe))"
exit 0
