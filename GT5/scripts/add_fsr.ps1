<#
add_fsr.ps1: AMD FSR 3 frame generation for this PC's game. setup.ps1 (so the installer) adds it after the
first build and update.ps1 builds its plug-in again after each update; the launcher's Display page can add
or remove it too. The game works the same without it; with it, "AMD FSR 3" can be picked as the frame
generator. It runs on any graphics card that can run the game with Direct3D 12 (AMD, NVIDIA, Intel), and
with Vulkan on any card whose Vulkan driver has what AMD's frame generator needs.

FSR is AMD's software, not this project's, and none of it is in this project's files. This script:
  1. fetches the parts of AMD's FSR SDK it needs at a fixed release from AMD's own public repository with
     git (https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK: 37 MB to download, 103 MB on disk:
     a few headers, the licence, and AMD's loader and frame generation DLLs for Direct3D 12
     (amd_fidelityfx_loader_dx12.dll, amd_fidelityfx_framegeneration_dx12.dll) and for Vulkan (the same
     names ending _vk.dll)) into GT5\deps\fsr, and checks it is exactly that release (kept there, so later
     runs fetch nothing);
  2. checks that AMD's four DLLs are the ones this script was written for (SHA-256) and are signed by AMD;
  3. builds GT5\build\gt5_upscale_fsr.dll (this project's small plug-in, GT5\src\upscale\upscale_fsr.cpp)
     against AMD's headers with the compiler the game is built with (with its Vulkan part when
     add_vulkan.ps1 has put the Vulkan headers in GT5\deps\vulkan\prefix), and puts AMD's four DLLs and
     AMD's licence text beside it (67 MB next to the game).
AMD gives these files under the MIT licence (the licence file names them); there is nothing to accept, the
script says where the text is. gt5.exe itself is not rebuilt: it looks for the plug-in when it starts.

  powershell -File GT5\scripts\add_fsr.ps1            # add AMD FSR 3 (or build its plug-in again)
  powershell -File GT5\scripts\add_fsr.ps1 -Remove    # take it out again
  -SdkDir <folder>: use a copy of the same SDK release already on this PC (its top folder, the one that
                    holds Kits\ and docs\) instead of git.
  -NoPause: ask nothing, wait for nothing (for programs). Progress: GT5_ADD lines, see addon_common.ps1.
Exit code: 0 done, 1 failed (an earlier copy, if any, is left as it was).
#>
param(
    [switch]$Remove,
    [string]$SdkDir = '',
    [switch]$NoPause
)
$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'addon_common.ps1')
$gt5 = Split-Path -Parent $PSScriptRoot
Initialize-AddOn $gt5 ([bool]$NoPause)
$build = Join-Path $gt5 'build'
$deps = Join-Path $gt5 'deps\fsr'
$plug = Join-Path $build 'gt5_upscale_fsr.dll'
$licOut = Join-Path $build 'amd_fidelityfx_license.md'

# The release, by the commit git checks it against, and AMD's two DLLs of that release by their SHA-256.
$Pin = @{ Name = 'AMD FSR SDK 2.4.0'; Url = 'https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git'; Tag = 'v2.4.0'
          Commit = '7539ae8b6568ad29ed78a753404351daf3b5cca7' }
$Dlls = @(
    @{ Name = 'amd_fidelityfx_loader_dx12.dll';          Sha = 'E2191D3C0828BE45EB8AFFD4E3EC0378F2293ADC345B772A7170EDCF3B021ECB' },
    @{ Name = 'amd_fidelityfx_framegeneration_dx12.dll'; Sha = 'E6843252F58B0341A0D80F6A20BC0D840643EE95BC48C22CAC08745344577BB6' },
    @{ Name = 'amd_fidelityfx_loader_vk.dll';            Sha = '38A56A01FB3565C0757D9F5C1E4E69DE4B65E5F2E5769B323B200DC1F142BAB9' },
    @{ Name = 'amd_fidelityfx_framegeneration_vk.dll';   Sha = 'D002DABFF8C5E71EAA715FC99438CD4DC81230262A7897B16BC402EFEC3498B4' }
)
# What is taken out of AMD's repository (git sparse-checkout patterns, from its top folder).
$Paths = @(
    '/Kits/FidelityFX/api/include/',
    '/Kits/FidelityFX/framegeneration/include/',
    '/Kits/FidelityFX/signedbin/amd_fidelityfx_loader_dx12.dll',
    '/Kits/FidelityFX/signedbin/amd_fidelityfx_framegeneration_dx12.dll',
    '/Kits/FidelityFX/signedbin/amd_fidelityfx_loader_vk.dll',
    '/Kits/FidelityFX/signedbin/amd_fidelityfx_framegeneration_vk.dll',
    '/docs/license.md'
)

Send-Add 'start' 0 'AMD FSR 3'
if (Test-GameRunning $build) { Stop-AddOn 1 'The game is running: close it first, then run this again.' 'Game running' }
if ($Remove) {
    Send-Add 'remove' 50 'Removing'
    Remove-Item -Force $plug, $licOut, (Join-Path $build 'gt5_upscale_fsr.lib') -ErrorAction SilentlyContinue
    foreach ($d in $Dlls) { Remove-Item -Force (Join-Path $build $d.Name) -ErrorAction SilentlyContinue }
    Remove-Tree $deps
    Stop-AddOn 0 'AMD FSR 3 is taken out: the game is as it was before it.' 'Removed'
}
if (-not (Test-Path (Join-Path $build 'gt5.exe'))) {
    Stop-AddOn 1 "There's no game build in $build yet. Run setup first (setup.bat in the install folder)." 'No game build'
}
$src = Join-Path $gt5 'src\upscale\upscale_fsr.cpp'
if (-not (Test-Path $src)) { Stop-AddOn 1 "This version of the game has no FSR plug-in source ($src): update first." 'Update first' }
# gt5_upscale.h (the interface the game and the plug-in share) is the runtime's: ps3recomp\libs\video.
$iface = Join-Path (Split-Path $gt5 -Parent) 'ps3recomp\libs\video'
if (-not (Test-Path (Join-Path $iface 'gt5_upscale.h'))) { Stop-AddOn 1 "This version of the game has no upscaler interface ($iface\gt5_upscale.h): update first." 'Update first' }

# ---- AMD's SDK -------------------------------------------------------------------------------------------
$sdk = if ($SdkDir) { $SdkDir } else { Join-Path $deps 'FidelityFX-SDK' }
$kit = Join-Path $sdk 'Kits\FidelityFX'
$incApi = Join-Path $kit 'api\include'
$incFg = Join-Path $kit 'framegeneration\include'
$bin = Join-Path $kit 'signedbin'
$lic = Join-Path $sdk 'docs\license.md'
$need = @((Join-Path $incApi 'ffx_api.h'), (Join-Path $incApi 'dx12\ffx_api_dx12.h'), (Join-Path $incApi 'vk\ffx_api_vk.h'), (Join-Path $incFg 'ffx_framegeneration.h'), $lic) + @($Dlls | ForEach-Object { Join-Path $bin $_.Name })
function Test-SdkFiles { foreach ($f in $need) { if (-not (Test-Path $f)) { return $false } }; return $true }
if (-not $SdkDir) {
    $have = if (Test-Path (Join-Path $sdk '.git')) { git -C $sdk rev-parse HEAD 2>$null } else { '' }
    # Fetched again unless it is that release and whole (a fetch cut short leaves files out).
    if ($have -ne $Pin.Commit -or -not (Test-SdkFiles)) {
        Remove-Tree $sdk
        if (Test-Path $sdk) { Stop-AddOn 1 "$sdk couldn't be emptied for a new download (is a file in it open?). Nothing was changed." 'Folder in use' }
        New-Item -ItemType Directory -Force $deps | Out-Null
        Write-Host "== Fetching $($Pin.Name) from AMD ($($Pin.Url)): 20 MB"
        # Only what is needed: nothing is checked out until the list of paths is set.
        $rc = Invoke-Fetch @('-c', 'advice.detachedHead=false', 'clone', '--quiet', '--depth', '1', '--branch', $Pin.Tag, '--filter=blob:none', '--no-checkout', $Pin.Url, "`"$sdk`"") $sdk 3 0 10 'Downloading'
        if (-not $rc) { Invoke-GitQuiet -C $sdk sparse-checkout set --no-cone @Paths; $rc = $LASTEXITCODE }
        if (-not $rc) { $rc = Invoke-Fetch @('-C', "`"$sdk`"", '-c', 'advice.detachedHead=false', 'checkout', '--quiet', $Pin.Tag) $sdk 49 10 60 'Downloading' }
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
# AMD's DLLs must be AMD's, and the ones of this release: hash and signature are checked before they are
# put next to the game.
foreach ($d in $Dlls) {
    $f = Join-Path $bin $d.Name
    $sha = (Get-FileHash $f -Algorithm SHA256).Hash
    if ($sha -ne $d.Sha) {
        Stop-AddOn 1 "$($d.Name) in the SDK isn't the file of $($Pin.Name) (SHA-256 $sha). Nothing was changed. Please report this." 'Unexpected download'
    }
    $sig = Get-AuthenticodeSignature $f
    if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'Advanced Micro Devices') {
        Stop-AddOn 1 "$($d.Name) in the SDK isn't signed by AMD ($($sig.Status)). Nothing was changed. Please report this." 'Unexpected download'
    }
}

# ---- AMD's licence ---------------------------------------------------------------------------------------
Write-Host ''
Write-Host 'AMD FSR is AMD''s software. AMD gives these files under the MIT licence; its text, with the list of files'
Write-Host 'it covers (the headers and the two DLLs among them), is here and is copied next to the game:'
Write-Host "  $lic"

# ---- The plug-in -----------------------------------------------------------------------------------------
Write-Host '== Building the FSR plug-in (a few seconds)'
Send-Add 'build' 70 'Building'
if (-not (Import-CompilerEnv $PSScriptRoot)) { Stop-AddOn 1 "Couldn't set up the compiler (build.cmd's reason is above). Nothing else was changed." 'No compiler' }
$cc = $env:CLANGCL
$tmp = Join-Path $gt5 'deps\fsr_build'
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $tmp | Out-Null
Push-Location $tmp
# Nothing of AMD's is linked: the plug-in loads AMD's loader DLL by name when the game starts.
# With the Vulkan headers add_vulkan.ps1 leaves (setup and update run it before this), the plug-in also gets
# its Vulkan part (gt5_upscale_vk.h); nothing of Vulkan is linked either, the game hands its device over.
$vk = @()
$vkInc = Join-Path $gt5 'deps\vulkan\prefix\include'
if ((Test-Path (Join-Path $vkInc 'vulkan\vulkan.h')) -and (Test-Path (Join-Path $iface 'gt5_upscale_vk.h'))) {
    $vk = @('/DGT5_UPSCALE_WITH_VK', "/I$vkInc")
    Write-Host '   (with its Vulkan part)'
}
& $cc /nologo /O2 /MD /EHsc /W3 /LD -fuse-ld=lld -Wno-unknown-pragmas -Wno-deprecated-declarations -Wno-missing-braces "/I$incApi" "/I$incFg" "/I$iface" @vk $src "/Fe:$(Join-Path $tmp 'gt5_upscale_fsr.dll')" `
    /link wintrust.lib crypt32.lib advapi32.lib user32.lib
$rc = $LASTEXITCODE
Pop-Location
if ($rc -or -not (Test-Path (Join-Path $tmp 'gt5_upscale_fsr.dll'))) {
    Stop-AddOn 1 'Building the plug-in failed (see above). Nothing else was changed. Please report it with this text.' 'Build failed'
}
# AMD's files first and the plug-in last: the plug-in being there is what says FSR is in.
Send-Add 'build' 92 'Building'
try {
    foreach ($d in $Dlls) { Copy-Item -Force (Join-Path $bin $d.Name) (Join-Path $build $d.Name) -ErrorAction Stop }
    Copy-Item -Force $lic $licOut -ErrorAction Stop
    Copy-Item -Force (Join-Path $tmp 'gt5_upscale_fsr.dll') $plug -ErrorAction Stop
} catch { Stop-AddOn 1 "Copying the files next to the game failed: $($_.Exception.Message)" 'Copy failed' }
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
Stop-AddOn 0 'AMD FSR 3 is in. In the game press F1: Display > "Frame generation: how" now offers AMD FSR 3.' 'AMD FSR 3 ready'
