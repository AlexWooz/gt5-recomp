<#
add_dlss.ps1: adds NVIDIA DLSS to this PC's game, once. The launcher runs it from Display > "Get NVIDIA DLSS".
The game works the same without it; with it, the anti-aliasing choices "DLSS" and "DLAA" can be picked.

DLSS is NVIDIA's software, not this project's, and none of it is in this project's files. This script:
  1. fetches NVIDIA's DLSS SDK at a fixed release from NVIDIA's own public repository with git
     (https://github.com/NVIDIA/DLSS, about 150 MB of it: the headers, one library, nvngx_dlss.dll) into
     GT5\deps\dlss, and checks it is exactly that release;
  2. shows NVIDIA's licence for it (GT5\deps\dlss\DLSS\LICENSE.txt) and asks you to accept it. No: what was
     fetched is deleted again and nothing changes;
  3. builds GT5\build\gt5_upscale_dlss.dll (this project's small plug-in, GT5\src\upscale\upscale_dlss.cpp)
     against the SDK with the compiler the game is built with, and puts NVIDIA's nvngx_dlss.dll beside it.
gt5.exe itself is not rebuilt: it looks for the plug-in when it starts.

  powershell -File GT5\scripts\add_dlss.ps1            # add DLSS
  powershell -File GT5\scripts\add_dlss.ps1 -Remove    # take it out again
  -Accept: the licence was already shown and accepted (the launcher's own window does that).
  -SdkDir <folder>: use a copy of the same SDK release already on this PC instead of git.
#>
param(
    [switch]$Remove,
    [switch]$Accept,
    [string]$SdkDir = '',
    [switch]$NoPause
)
$ErrorActionPreference = 'Continue'
if ($env:GT5_GIT_SAFE -ne '1') {
    $gn = 0; if ("$env:GIT_CONFIG_COUNT" -match '^\d+$') { $gn = [int]$env:GIT_CONFIG_COUNT }
    Set-Item "env:GIT_CONFIG_KEY_$gn" 'safe.directory'; Set-Item "env:GIT_CONFIG_VALUE_$gn" '*'
    $env:GIT_CONFIG_COUNT = "$($gn + 1)"; $env:GT5_GIT_SAFE = '1'
}
$gt5 = Split-Path -Parent $PSScriptRoot
# Git: the PC's own, else the port's own portable copy in tools\git (get-git.ps1; nothing installed in Windows).
$ownGit = Join-Path (Split-Path -Parent $gt5) 'tools\git\cmd'
if (-not (Get-Command git -ErrorAction SilentlyContinue) -and (Test-Path -LiteralPath (Join-Path $ownGit 'git.exe'))) { $env:Path = "$ownGit;$env:Path" }
$build = Join-Path $gt5 'build'
$deps = Join-Path $gt5 'deps\dlss'
$plug = Join-Path $build 'gt5_upscale_dlss.dll'
$ngx = Join-Path $build 'nvngx_dlss.dll'

# The release, by the commit git checks it against.
$Pin = @{ Name = 'NVIDIA DLSS SDK 310.9.16'; Url = 'https://github.com/NVIDIA/DLSS.git'; Tag = 'v310.9.16'
          Commit = '9981c6350dc58c0ce21f024fd65e31e851c31689' }

function Done([int]$code, [string]$msg) {
    Write-Host ''
    if ($code) { Write-Host $msg -ForegroundColor Red } else { Write-Host $msg -ForegroundColor Green }
    if (-not $NoPause) { Write-Host ''; Read-Host 'Press Enter to close' | Out-Null }
    exit $code
}

if (Get-Process gt5 -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -like "$build\*" }) {
    Done 1 'The game is running: close it first, then run this again.'
}
if ($Remove) {
    Remove-Item -Force $plug, $ngx, (Join-Path $build 'gt5_upscale_dlss.lib') -ErrorAction SilentlyContinue
    if (Test-Path $deps) { Remove-Item -Recurse -Force $deps -ErrorAction SilentlyContinue }
    Done 0 'DLSS is taken out: the game is as it was before it.'
}
if (-not (Test-Path (Join-Path $build 'gt5.exe'))) {
    Done 1 "There's no game build in $build yet. Run setup first (setup.bat in the install folder)."
}
$src = Join-Path $gt5 'src\upscale\upscale_dlss.cpp'
if (-not (Test-Path $src)) { Done 1 "This version of the game has no DLSS plug-in source ($src): update first." }
$nv = @(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue | Where-Object { $_.Name -match 'NVIDIA' })
if (-not $nv) {
    Done 1 'No NVIDIA graphics card was found in this PC. DLSS only runs on NVIDIA RTX cards; for other cards pick FSR in the game''s menu.'
}

# ---- NVIDIA's SDK ----------------------------------------------------------------------------------------
$sdk = if ($SdkDir) { $SdkDir } else { Join-Path $deps 'DLSS' }
if (-not $SdkDir) {
    $have = if (Test-Path (Join-Path $sdk '.git')) { git -C $sdk rev-parse HEAD } else { '' }
    if ($have -ne $Pin.Commit) {
        if (Test-Path $sdk) { Remove-Item -Recurse -Force $sdk -ErrorAction Stop }
        New-Item -ItemType Directory -Force $deps | Out-Null
        Write-Host "== Fetching $($Pin.Name) from NVIDIA ($($Pin.Url))"
        # only what is needed: the headers, the library the plug-in links, the DLSS DLL
        git -c advice.detachedHead=false clone --quiet --depth 1 --branch $Pin.Tag --filter=blob:none --sparse $Pin.Url $sdk
        if ($LASTEXITCODE) { Done 1 'Fetching the SDK failed: check the internet connection and run this again. Nothing was changed.' }
        git -C $sdk sparse-checkout set include lib/Windows_x86_64/x64 lib/Windows_x86_64/rel
        if ($LASTEXITCODE) { Remove-Item -Recurse -Force $sdk; Done 1 'Fetching the SDK failed part way: run this again. Nothing was changed.' }
        $have = git -C $sdk rev-parse HEAD
        if ($have -ne $Pin.Commit) {
            Remove-Item -Recurse -Force $sdk
            Done 1 "The SDK isn't the expected release (commit $have, expected $($Pin.Commit)), so it was deleted. Please report this."
        }
    }
}
$lib = Join-Path $sdk 'lib\Windows_x86_64\x64\nvsdk_ngx_d.lib'
$dll = Join-Path $sdk 'lib\Windows_x86_64\rel\nvngx_dlss.dll'
$inc = Join-Path $sdk 'include'
$lic = Join-Path $sdk 'LICENSE.txt'
foreach ($f in $lib, $dll, (Join-Path $inc 'nvsdk_ngx.h'), $lic) {
    if (-not (Test-Path $f)) { Done 1 "The SDK in $sdk is missing $f. Delete GT5\deps\dlss and run this again." }
}
# NVIDIA's DLL must be NVIDIA's: its signature is checked before it is put next to the game.
$sig = Get-AuthenticodeSignature $dll
if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'NVIDIA') {
    Done 1 "nvngx_dlss.dll in the SDK isn't signed by NVIDIA ($($sig.Status)). Nothing was changed. Please report this."
}

# ---- NVIDIA's licence ------------------------------------------------------------------------------------
if (-not $Accept) {
    Write-Host ''
    Write-Host 'NVIDIA DLSS is NVIDIA''s software and comes under NVIDIA''s licence, not this project''s:'
    Write-Host "  $lic"
    Write-Host 'Read it (it opens in Notepad now). It is between you and NVIDIA.'
    Start-Process notepad.exe -ArgumentList "`"$lic`""
    $a = Read-Host 'Type  accept  to accept NVIDIA''s licence and add DLSS, anything else to stop'
    if ($a.Trim().ToLower() -ne 'accept') {
        if (-not $SdkDir) { Remove-Item -Recurse -Force $deps -ErrorAction SilentlyContinue }
        Done 1 'Not accepted: what was fetched is deleted and nothing was changed.'
    }
}

# ---- The plug-in -----------------------------------------------------------------------------------------
$envLines = cmd /c "set GT5_ENV_ONLY=1&& call `"$PSScriptRoot\build.cmd`" >nul 2>nul && set"
if ($LASTEXITCODE) { Done 1 "Couldn't set up the compiler: run GT5\scripts\build.cmd gt5 to see why." }
foreach ($l in $envLines) { if ($l -match '^([^=]+)=(.*)$' -and $Matches[1] -ne 'GT5_ENV_ONLY') { Set-Item "env:$($Matches[1])" $Matches[2] } }
$cc = $env:CLANGCL
if (-not $cc) { Done 1 "Couldn't find clang-cl: run GT5\scripts\build.cmd gt5 to see why." }
Write-Host '== Building the DLSS plug-in (a few seconds)'
$tmp = Join-Path $deps 'build'
New-Item -ItemType Directory -Force $tmp | Out-Null
Push-Location $tmp
# gt5_upscale.h (the interface the game and the plug-in share) is the runtime's: ps3recomp\libs\video.
$iface = Join-Path (Split-Path $gt5 -Parent) 'ps3recomp\libs\video'
if (-not (Test-Path (Join-Path $iface 'gt5_upscale.h'))) { Done 1 "This version of the game has no upscaler interface ($iface\gt5_upscale.h): update first." }
& $cc /nologo /O2 /MD /EHsc /W3 /LD -fuse-ld=lld "/I$inc" "/I$iface" $src "/Fe:$(Join-Path $tmp 'gt5_upscale_dlss.dll')" `
    /link $lib advapi32.lib user32.lib shell32.lib version.lib ole32.lib
$rc = $LASTEXITCODE
Pop-Location
if ($rc -or -not (Test-Path (Join-Path $tmp 'gt5_upscale_dlss.dll'))) {
    Done 1 'Building the plug-in failed (see above). Nothing else was changed. Please report it with this window''s text.'
}
Copy-Item -Force (Join-Path $tmp 'gt5_upscale_dlss.dll') $plug -ErrorAction Stop
Copy-Item -Force $dll $ngx -ErrorAction Stop
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
Done 0 'DLSS is in. In the game press F1: Display > Anti-aliasing and upscaling now offers DLAA and DLSS.'
