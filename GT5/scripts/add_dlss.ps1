<#
add_dlss.ps1: NVIDIA DLSS for this PC's game, on a PC with an NVIDIA RTX card. setup.ps1 adds it after the
first build when the installer showed its licence by the Install button; update.ps1 builds its plug-in again
after each update when it is there; the launcher's Display page can add or remove it too (its Add button
carries the same licence line). The game works the same without it; with it, the anti-aliasing choices
"DLSS" and "DLAA" can be picked and, on an RTX 40 series card or newer, "NVIDIA DLSS Frame Generation" as
the frame generator.

DLSS is NVIDIA's software, not this project's, and none of it is in this project's files. This script:
  1. fetches NVIDIA's DLSS SDK at a fixed release from NVIDIA's own public repository with git
     (https://github.com/NVIDIA/DLSS: 60 MB to download, 210 MB on disk while it is there: 150 MB of
     files (the headers, NVIDIA's libraries, nvngx_dlss.dll, nvngx_dlssg.dll and one more DLL that comes
     with them) and git's packed copy) into GT5\deps\dlss, and checks it is exactly that release;
  2. NVIDIA's licence for it (GT5\deps\dlss\DLSS\LICENSE.txt): -Accept says the player accepted it where it
     was shown (the installer's Install button, the launcher's Add button). Run by hand without -Accept, it
     opens the licence and asks; with -NoPause and no -Accept it asks nothing and adds nothing;
  3. builds GT5\build\gt5_upscale_dlss.dll (this project's small plug-in, GT5\src\upscale\upscale_dlss.cpp)
     against the SDK with the compiler the game is built with, and puts NVIDIA's nvngx_dlss.dll (the
     upscaler, 56 MB) and nvngx_dlssg.dll (frame generation, 7 MB) beside it.
gt5.exe itself is not rebuilt: it looks for the plug-in when it starts.

  powershell -File GT5\scripts\add_dlss.ps1 -Accept    # add DLSS (or build its plug-in again)
  powershell -File GT5\scripts\add_dlss.ps1 -Remove    # take it out again
  -SdkDir <folder>: use a copy of the same SDK release already on this PC instead of git.
  -NoPause: ask nothing, wait for nothing (for programs). Progress: GT5_ADD lines, see addon_common.ps1.
Exit code: 0 done, 1 failed (an earlier copy, if any, is left as it was), 2 skipped (no NVIDIA RTX card,
or the licence not accepted).
#>
param(
    [switch]$Remove,
    [switch]$Accept,
    [string]$SdkDir = '',
    [switch]$NoPause
)
$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'addon_common.ps1')
$gt5 = Split-Path -Parent $PSScriptRoot
Initialize-AddOn $gt5 ([bool]$NoPause)
$build = Join-Path $gt5 'build'
$deps = Join-Path $gt5 'deps\dlss'
$plug = Join-Path $build 'gt5_upscale_dlss.dll'
$ngx = Join-Path $build 'nvngx_dlss.dll'
$ngxg = Join-Path $build 'nvngx_dlssg.dll'

# The release, by the commit git checks it against. The launcher reads Tag from these lines for its link to
# NVIDIA's licence (LICENSE.txt at this tag), and GT5-Installer.bat names the same tag in its own link: a new
# release here moves both.
$Pin = @{ Name = 'NVIDIA DLSS SDK 310.9.16'; Url = 'https://github.com/NVIDIA/DLSS.git'; Tag = 'v310.9.16'
          Commit = '9981c6350dc58c0ce21f024fd65e31e851c31689' }

Send-Add 'start' 0 'NVIDIA DLSS'
if (Test-GameRunning $build) { Stop-AddOn 1 'The game is running: close it first, then run this again.' 'Game running' }
if ($Remove) {
    Send-Add 'remove' 50 'Removing'
    Remove-Item -Force $plug, $ngx, $ngxg, (Join-Path $build 'gt5_upscale_dlss.lib') -ErrorAction SilentlyContinue
    Remove-Tree $deps
    Stop-AddOn 0 'DLSS is taken out: the game is as it was before it.' 'Removed'
}
if (-not (Test-Path (Join-Path $build 'gt5.exe'))) {
    Stop-AddOn 1 "There's no game build in $build yet. Run setup first (setup.bat in the install folder)." 'No game build'
}
$src = Join-Path $gt5 'src\upscale\upscale_dlss.cpp'
if (-not (Test-Path $src)) { Stop-AddOn 1 "This version of the game has no DLSS plug-in source ($src): update first." 'Update first' }
# gt5_upscale.h (the interface the game and the plug-in share) is the runtime's: ps3recomp\libs\video.
$iface = Join-Path (Split-Path $gt5 -Parent) 'ps3recomp\libs\video'
if (-not (Test-Path (Join-Path $iface 'gt5_upscale.h'))) { Stop-AddOn 1 "This version of the game has no upscaler interface ($iface\gt5_upscale.h): update first." 'Update first' }
if (-not (Test-RtxCard)) {
    Stop-AddOn 2 'No NVIDIA RTX graphics card was found in this PC. DLSS only runs on NVIDIA RTX cards; for other cards pick FSR in the game''s menu.' 'No NVIDIA RTX card'
}
if (-not $Accept -and $NoPause) {
    Stop-AddOn 2 'NVIDIA''s licence wasn''t accepted here (-Accept), so DLSS wasn''t added. Nothing was changed.' 'Licence not accepted'
}

# ---- NVIDIA's SDK ----------------------------------------------------------------------------------------
$sdk = if ($SdkDir) { $SdkDir } else { Join-Path $deps 'DLSS' }
$lib = Join-Path $sdk 'lib\Windows_x86_64\x64\nvsdk_ngx_d.lib'
$dll = Join-Path $sdk 'lib\Windows_x86_64\rel\nvngx_dlss.dll'
$dllg = Join-Path $sdk 'lib\Windows_x86_64\rel\nvngx_dlssg.dll'
$inc = Join-Path $sdk 'include'
$lic = Join-Path $sdk 'LICENSE.txt'
$need = @($lib, $dll, $dllg, (Join-Path $inc 'nvsdk_ngx.h'), (Join-Path $inc 'nvsdk_ngx_helpers_dlssg_d3d.h'), (Join-Path $inc 'nvsdk_ngx_helpers_dlssg_vk.h'), $lic)
function Test-SdkFiles { foreach ($f in $need) { if (-not (Test-Path $f)) { return $false } }; return $true }
if (-not $SdkDir) {
    $have = if (Test-Path (Join-Path $sdk '.git')) { git -C $sdk rev-parse HEAD 2>$null } else { '' }
    # Fetched again unless it is that release and whole (a fetch cut short leaves files out).
    if ($have -ne $Pin.Commit -or -not (Test-SdkFiles)) {
        Remove-Tree $sdk
        if (Test-Path $sdk) { Stop-AddOn 1 "$sdk couldn't be emptied for a new download (is a file in it open?). Nothing was changed." 'Folder in use' }
        New-Item -ItemType Directory -Force $deps | Out-Null
        Write-Host "== Fetching $($Pin.Name) from NVIDIA ($($Pin.Url)): 60 MB"
        # Only what is needed: the headers, the library the plug-in links, the DLSS DLLs.
        $rc = Invoke-Fetch @('-c', 'advice.detachedHead=false', 'clone', '--quiet', '--depth', '1', '--branch', $Pin.Tag, '--filter=blob:none', '--sparse', $Pin.Url, "`"$sdk`"") $sdk 4 0 10 'Downloading'
        if (-not $rc) { $rc = Invoke-Fetch @('-C', "`"$sdk`"", 'sparse-checkout', 'set', 'include', 'lib/Windows_x86_64/x64', 'lib/Windows_x86_64/rel') $sdk 210 10 60 'Downloading' }
        if ($rc) {
            Remove-Item -Recurse -Force $sdk -ErrorAction SilentlyContinue
            Stop-AddOn 1 'Fetching the SDK failed: check the internet connection and run this again. Nothing was changed.' 'No internet'
        }
        $have = git -C $sdk rev-parse HEAD 2>$null
        if ($have -ne $Pin.Commit -or -not (Test-SdkFiles)) {
            Remove-Item -Recurse -Force $sdk -ErrorAction SilentlyContinue
            Stop-AddOn 1 "The SDK isn't the expected release or isn't whole (commit $have, expected $($Pin.Commit)), so it was deleted. Please report this." 'Unexpected download'
        }
    }
}
Send-Add 'check' 60 'Checking'
foreach ($f in $need) { if (-not (Test-Path $f)) { Stop-AddOn 1 "The SDK in $sdk is missing $f." 'Download incomplete' } }
# NVIDIA's DLLs must be NVIDIA's: their signatures are checked before they are put next to the game.
foreach ($f in $dll, $dllg) {
    $sig = Get-AuthenticodeSignature $f
    if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'NVIDIA') {
        Stop-AddOn 1 "$(Split-Path -Leaf $f) in the SDK isn't signed by NVIDIA ($($sig.Status)). Nothing was changed. Please report this." 'Unexpected download'
    }
}

# ---- NVIDIA's licence: run by hand without -Accept, it is shown and asked here -------------------------------
if (-not $Accept) {
    Write-Host ''
    Write-Host 'NVIDIA DLSS is NVIDIA''s software and comes under NVIDIA''s licence, not this project''s:'
    Write-Host "  $lic"
    Write-Host 'Read it (it opens in Notepad now). It is between you and NVIDIA.'
    Start-Process notepad.exe -ArgumentList "`"$lic`""
    $a = Read-Host 'Type  accept  to accept NVIDIA''s licence and add DLSS, anything else to stop'
    if ("$a".Trim().ToLower() -ne 'accept') {
        if (-not $SdkDir) { Remove-Item -Recurse -Force $deps -ErrorAction SilentlyContinue }
        Stop-AddOn 2 'Not accepted: what was fetched is deleted and nothing was changed.' 'Licence not accepted'
    }
}

# ---- The plug-in -----------------------------------------------------------------------------------------
Write-Host '== Building the DLSS plug-in (a few seconds)'
Send-Add 'build' 70 'Building'
if (-not (Import-CompilerEnv $PSScriptRoot)) { Stop-AddOn 1 "Couldn't set up the compiler (build.cmd's reason is above). Nothing else was changed." 'No compiler' }
$cc = $env:CLANGCL
$tmp = Join-Path $deps 'build'
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $tmp | Out-Null
Push-Location $tmp
# With the Vulkan headers add_vulkan.ps1 leaves (setup and update run it before this), the plug-in also gets
# its Vulkan part (gt5_upscale_vk.h: NGX's Vulkan entry points, in the same nvsdk_ngx_d.lib); nothing of
# Vulkan is linked, the game hands its device over.
$vk = @()
$vkInc = Join-Path $gt5 'deps\vulkan\prefix\include'
if ((Test-Path (Join-Path $vkInc 'vulkan\vulkan.h')) -and (Test-Path (Join-Path $iface 'gt5_upscale_vk.h'))) {
    $vk = @('/DGT5_UPSCALE_WITH_VK', "/I$vkInc")
    Write-Host '   (with its Vulkan part)'
}
& $cc /nologo /O2 /MD /EHsc /W3 /LD -fuse-ld=lld -Wno-deprecated-declarations -Wno-missing-braces "/I$inc" "/I$iface" @vk $src "/Fe:$(Join-Path $tmp 'gt5_upscale_dlss.dll')" `
    /link $lib advapi32.lib user32.lib shell32.lib version.lib ole32.lib gdi32.lib
$rc = $LASTEXITCODE
Pop-Location
if ($rc -or -not (Test-Path (Join-Path $tmp 'gt5_upscale_dlss.dll'))) {
    Stop-AddOn 1 'Building the plug-in failed (see above). Nothing else was changed. Please report it with this text.' 'Build failed'
}
# NVIDIA's files first and the plug-in last: the plug-in being there is what says DLSS is in.
Send-Add 'build' 92 'Building'
try {
    Copy-Item -Force $dll $ngx -ErrorAction Stop
    Copy-Item -Force $dllg $ngxg -ErrorAction Stop
    Copy-Item -Force (Join-Path $tmp 'gt5_upscale_dlss.dll') $plug -ErrorAction Stop
} catch { Stop-AddOn 1 "Copying the files next to the game failed: $($_.Exception.Message)" 'Copy failed' }
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
Stop-AddOn 0 'DLSS is in. In the game press F1: Display > Anti-aliasing and upscaling now offers DLAA and DLSS, and "Frame generation: how" offers NVIDIA DLSS Frame Generation on a card that can run it.' 'NVIDIA DLSS ready'
