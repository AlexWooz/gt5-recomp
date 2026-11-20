<#
add_vulkan.ps1: the Vulkan renderer in this PC's gt5.exe. setup.ps1 (so the installer) and update.ps1 run it
with -Prepare before they build the game, so the game is built with both renderers from the start and the
launcher only switches between them. DirectX 12 stays the default and keeps working whatever happens here:
when this can't be done, the game is built with DirectX 12 alone and the launcher doesn't list Vulkan.

Vulkan needs two libraries the game leaves out otherwise: SDL2 (its window) and glslang (turns the game's
shaders into Vulkan's SPIR-V). This fetches their source code at a fixed release with git (about 60 MB),
checks each is exactly that release, builds them into GT5\deps\vulkan with the compiler the game is built
with (a few minutes), and turns Vulkan on in the game's build folder. GT5\CMakeLists.txt also turns it on by
itself in a new build folder when it finds them. build\SDL2.dll next to gt5.exe then says the exe has Vulkan.

  powershell -File GT5\scripts\add_vulkan.ps1 -Prepare           # the libraries, and Vulkan on for the next build
  powershell -File GT5\scripts\add_vulkan.ps1                    # the same, then gt5.exe rebuilt (only the runtime:
                                                                 #   about 10 to 20 minutes)
  powershell -File GT5\scripts\add_vulkan.ps1 -Remove            # gt5.exe rebuilt without it; setup and updates
                                                                 #   leave it out from then on (add it again to undo)
  powershell -File GT5\scripts\add_vulkan.ps1 -Remove -Prepare   # off for the next build only (setup's fallback
                                                                 #   when a build with Vulkan fails)
  -Sdl2Src / -GlslangSrc <folder>: use source code already on this PC (the same releases) instead of git.
  -NoPause: ask nothing, wait for nothing (for programs). Progress: GT5_ADD lines, see addon_common.ps1.
Exit code: 0 done, 1 failed (Vulkan is then off in the build folder, so the game still builds), 2 skipped.
#>
param(
    [switch]$Remove,
    [switch]$Prepare,
    [string]$Sdl2Src = '',
    [string]$GlslangSrc = '',
    [switch]$NoPause
)
# Continue: git and cmake write progress to stderr; each step's exit code says whether it worked.
$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'addon_common.ps1')
$gt5 = Split-Path -Parent $PSScriptRoot
Initialize-AddOn $gt5 ([bool]$NoPause)
$build = Join-Path $gt5 'build'
$cache = Join-Path $build 'CMakeCache.txt'
$deps = Join-Path $gt5 'deps\vulkan'
$prefix = Join-Path $deps 'prefix'
# -Remove (without -Prepare) records that this install shouldn't have Vulkan; -Prepare then leaves it out.
$offMark = Join-Path (Split-Path -Parent $gt5) '.gt5\vulkan-off.txt'

# The releases, by the commit git checks them against, and about how much each clone fills on disk. Skip: a
# folder of the release that isn't fetched (glslang's Test: 2,900 of its 3,100 files, used only by its own
# tests, which are off here; on a slow drive they took longer than all the rest).
$Pins = @(
    @{ Name = 'SDL2 2.32.10'; Dir = 'SDL2-2.32.10'; Url = 'https://github.com/libsdl-org/SDL.git'; Tag = 'release-2.32.10'
       Commit = '5d249570393f7a37e037abf22cd6012a4cc56a71'; MB = 85; Skip = '' },
    @{ Name = 'glslang 15.4.0'; Dir = 'glslang-15.4.0'; Url = 'https://github.com/KhronosGroup/glslang.git'; Tag = '15.4.0'
       Commit = '8a85691a0740d390761a1008b4696f57facd02c4'; MB = 9; Skip = '/Test/' }
)

# Vulkan on or off in the build folder's cache, for its next configure. The cache is edited and build.ninja
# deleted, so build.cmd configures again (keeping everything already compiled that doesn't change). No cache
# yet: nothing to do (a new build folder finds deps\vulkan by itself, see GT5\CMakeLists.txt).
function Get-CacheVulkan {
    if (-not (Test-Path -LiteralPath $cache)) { return $null }
    $m = Select-String -LiteralPath $cache -Pattern '^PS3RECOMP_RSX_VULKAN:BOOL=(\w+)' | Select-Object -First 1
    if (-not $m) { return $null }
    return ($m.Matches[0].Groups[1].Value -match '^(ON|1|TRUE|YES)$')
}
function Set-CacheVulkan([bool]$on) {
    if (-not (Test-Path -LiteralPath $cache)) { return }
    if ((Get-CacheVulkan) -eq $on) { return }
    $v = if ($on) { 'ON' } else { 'OFF' }
    $text = [IO.File]::ReadAllText($cache)
    if ($text -match '(?m)^PS3RECOMP_RSX_VULKAN:BOOL=') { $text = $text -replace '(?m)^PS3RECOMP_RSX_VULKAN:BOOL=\w+', "PS3RECOMP_RSX_VULKAN:BOOL=$v" }
    else { $text = $text.TrimEnd() + "`r`nPS3RECOMP_RSX_VULKAN:BOOL=$v`r`n" }
    [IO.File]::WriteAllText($cache, $text)
    Remove-Item -LiteralPath (Join-Path $build 'build.ninja') -Force -ErrorAction SilentlyContinue
    Write-Host "Vulkan is $v in the build folder: the next build configures again and builds the runtime with$(if (-not $on) { 'out' }) it."
}
# The libraries are all in deps\vulkan\prefix (the headers are copied last, so their being there means the
# rest is too; the other files are checked as well).
function Test-PrefixDone {
    return (Test-Path (Join-Path $prefix 'bin\SDL2.dll')) -and (Test-Path (Join-Path $prefix 'cmake\SDL2Config.cmake')) -and
           (Test-Path (Join-Path $prefix 'lib\cmake\glslang\glslang-config.cmake')) -and (Test-Path (Join-Path $prefix 'include\vulkan\vulkan.h'))
}
# A failure: Vulkan off for the next build, so the game still builds with DirectX 12.
function Fail([string]$msg, [string]$words) {
    Set-CacheVulkan $false
    Stop-AddOn 1 "$msg DirectX 12 still works." $words
}

Send-Add 'start' 0 'Vulkan'
if (Test-GameRunning $build) { Stop-AddOn 1 'The game is running: close it first, then run this again.' 'Game running' }

# ---- Off ------------------------------------------------------------------------------------------------
if ($Remove) {
    Set-CacheVulkan $false
    if ($Prepare) { Stop-AddOn 0 'Vulkan is off for the next build.' 'Vulkan off' }
    if (-not (Test-Path (Join-Path $build 'build.ninja')) -and -not (Test-Path $cache)) {
        Stop-AddOn 1 "There's no game build in $build yet. Run setup first (setup.bat in the install folder)." 'No game build'
    }
    New-Item -ItemType Directory -Force (Split-Path $offMark) | Out-Null
    Set-Content -LiteralPath $offMark -Value "Vulkan taken out with add_vulkan.ps1 -Remove, $(Get-Date -Format 'yyyy-MM-dd HH:mm'). Run add_vulkan.ps1 to add it again." -Encoding ascii
    Write-Host '== Building gt5.exe without Vulkan'
    $rc = Invoke-Counted { cmd /c "`"$PSScriptRoot\build.cmd`" gt5" } 'game' 5 99 'Building the game'
    if ($rc) { Stop-AddOn 1 'Building gt5.exe failed (see above).' 'Build failed' }
    Stop-AddOn 0 'gt5.exe is built without Vulkan again (DirectX 12 only). GT5\deps\vulkan can be deleted.' 'Vulkan removed'
}
if ($Prepare -and (Test-Path -LiteralPath $offMark)) {
    Set-CacheVulkan $false
    Stop-AddOn 2 "Vulkan was taken out of this install ($offMark): left out. Run add_vulkan.ps1 to add it again." 'Not wanted'
}
if (-not $Prepare -and -not (Test-Path (Join-Path $build 'build.ninja')) -and -not (Test-Path $cache)) {
    Stop-AddOn 1 "There's no game build in $build yet. Run setup first (setup.bat in the install folder)." 'No game build'
}
Remove-Item -LiteralPath $offMark -Force -ErrorAction SilentlyContinue

# Where each part of the run goes on the bar: -Prepare is the libraries alone; a whole add is mostly the game.
$Bar = if ($Prepare) { @{ F1 = 15; F2 = 30; S = 45; G = 95 } } else { @{ F1 = 8; F2 = 15; S = 25; G = 45 } }

# ---- The libraries, when they aren't all there ----------------------------------------------------------
if (-not (Test-PrefixDone)) {
    $needSdl = -not (Test-Path (Join-Path $prefix 'bin\SDL2.dll')) -or -not (Test-Path (Join-Path $prefix 'cmake\SDL2Config.cmake'))
    $needHeaders = -not (Test-Path (Join-Path $prefix 'include\vulkan\vulkan.h'))
    $needGls = -not (Test-Path (Join-Path $prefix 'lib\cmake\glslang\glslang-config.cmake'))
    if (-not (Import-CompilerEnv $PSScriptRoot)) { Fail "Couldn't set up the compiler (build.cmd's reason is above)." 'No compiler' }
    $cc = $env:CLANGCL
    $toolArgs = @([regex]::Matches([string]$env:TOOLARGS, '"([^"]*)"') | ForEach-Object { $_.Groups[1].Value })
    $jobs = $env:GT5_JOBS
    if (-not $jobs) {
        $ram = [int][math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 2GB)
        $jobs = [math]::Max(1, [math]::Min([int]$env:NUMBER_OF_PROCESSORS, $ram))
    }
    $cm = { param($p) $p -replace '\\', '/' }   # CMake wants forward slashes
    $common = @('-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_C_COMPILER=$cc", "-DCMAKE_CXX_COMPILER=$cc",
                "-DCMAKE_INSTALL_PREFIX=$(& $cm $prefix)", "-DCMAKE_PREFIX_PATH=$(& $cm $prefix)") + $toolArgs

    # The source code, for what isn't built yet.
    $src = @{}
    $i = 0
    foreach ($p in $Pins) {
        $i++
        $f0 = if ($i -eq 1) { 0 } else { $Bar.F1 }; $f1 = if ($i -eq 1) { $Bar.F1 } else { $Bar.F2 }
        if ($p.Dir -like 'SDL2*') { if (-not ($needSdl -or $needHeaders)) { continue } } elseif (-not $needGls) { continue }
        $given = if ($p.Dir -like 'SDL2*') { $Sdl2Src } else { $GlslangSrc }
        if ($given) {
            if (-not (Test-Path (Join-Path $given 'CMakeLists.txt'))) { Fail "$given has no CMakeLists.txt: not the $($p.Name) source code." 'Wrong source' }
            $src[$p.Dir] = $given; continue
        }
        $d = Join-Path $deps "src\$($p.Dir)"
        $have = if (Test-Path (Join-Path $d '.git')) { git -C $d rev-parse HEAD 2>$null } else { '' }
        # A checkout cut short (a stop, a full disk) has that commit but not all its files: fetched again.
        if ($have -eq $p.Commit -and ((Test-Path (Join-Path $d '.git\index.lock')) -or (git -C $d status --porcelain --untracked-files=no 2>$null))) { $have = '' }
        if ($have -ne $p.Commit) {
            Remove-Tree $d
            if (Test-Path $d) { Fail "$d couldn't be emptied for a new download (is a file in it open?)." 'Folder in use' }
            New-Item -ItemType Directory -Force (Split-Path $d) | Out-Null
            Write-Host "== Fetching $($p.Name) ($($p.Url))"
            if ($p.Skip) {
                # Nothing checked out until the paths are set; the skipped folder's files are never downloaded.
                $fm = $f0 + ($f1 - $f0) * 0.2
                $rc = Invoke-Fetch @('-c', 'advice.detachedHead=false', 'clone', '--quiet', '--depth', '1', '--branch', $p.Tag, '--filter=blob:none', '--no-checkout', $p.Url, "`"$d`"") $d 2 $f0 $fm 'Downloading'
                if (-not $rc) { Invoke-GitQuiet -C $d sparse-checkout set --no-cone '/*' "!$($p.Skip)"; $rc = $LASTEXITCODE }
                if (-not $rc) { $rc = Invoke-Fetch @('-C', "`"$d`"", '-c', 'advice.detachedHead=false', 'checkout', '--quiet', $p.Tag) $d $p.MB $fm $f1 'Downloading' }
            } else {
                $rc = Invoke-Fetch @('-c', 'advice.detachedHead=false', 'clone', '--quiet', '--depth', '1', '--branch', $p.Tag, $p.Url, "`"$d`"") $d $p.MB $f0 $f1 'Downloading'
            }
            if ($rc) {
                Remove-Tree $d
                Fail "Fetching $($p.Name) failed: check the internet connection and run this again." 'No internet'
            }
            Send-Add 'check' $f1 'Checking'
            $have = git -C $d rev-parse HEAD 2>$null
            if ($have -ne $p.Commit) {
                Remove-Tree $d
                Fail "$($p.Name) isn't the expected release (commit $have, expected $($p.Commit)), so it was deleted. Please report this." 'Unexpected download'
            }
        }
        $src[$p.Dir] = $d
    }
    $sdl = $src['SDL2-2.32.10']; $gls = $src['glslang-15.4.0']

    # SDL2 and glslang, into GT5\deps\vulkan\prefix.
    if ($needSdl) {
        $b = Join-Path $deps 'build\sdl2'
        # SDL_LIBC=ON: with the C runtime. Without it SDL2 builds against no C library at all, and newer
        # clang (Visual Studio 18's clang 22) turns its own string loops into strlen / wcslen calls that
        # then don't link. gt5.exe uses the same C runtime DLLs, so this adds nothing to install.
        Write-Host '== SDL2: configure'
        Send-Add 'build' $Bar.F2 'Building'
        $rc = Invoke-Counted { cmake -S $sdl -B $b @common -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_LIBC=ON } 'build' $Bar.F2 $Bar.F2 'Building'
        if (-not $rc) { Write-Host "== SDL2: build ($jobs jobs)"; $rc = Invoke-Counted { cmake --build $b --target install -j $jobs } 'build' $Bar.F2 $Bar.S 'Building' }
        if ($rc) { Fail 'Building SDL2 failed (see above).' 'Build failed' }
    }
    if ($needGls) {
        $b = Join-Path $deps 'build\glslang'
        Write-Host '== glslang: configure'
        Send-Add 'build' $Bar.S 'Building'
        $rc = Invoke-Counted { cmake -S $gls -B $b @common -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF `
            -DENABLE_GLSLANG_BINARIES=OFF -DGLSLANG_ENABLE_INSTALL=ON -DENABLE_HLSL=ON -DBUILD_SHARED_LIBS=OFF `
            -DBUILD_EXTERNAL=OFF -DENABLE_SPVREMAPPER=OFF } 'build' $Bar.S $Bar.S 'Building'
        if (-not $rc) { Write-Host "== glslang: build ($jobs jobs)"; $rc = Invoke-Counted { cmake --build $b --target install -j $jobs } 'build' $Bar.S $Bar.G 'Building' }
        if ($rc) { Fail 'Building glslang failed (see above).' 'Build failed' }
    }
    # The Vulkan headers: SDL2 carries the Khronos ones. Copied last: GT5's CMakeLists turns Vulkan on for a
    # new build folder when it finds them, and this script takes them as the sign that the rest is there.
    if ($needHeaders) {
        try { Copy-Item -Recurse -Force (Join-Path $sdl 'src\video\khronos\*') (Join-Path $prefix 'include') -ErrorAction Stop }
        catch { Fail "Copying the Vulkan headers failed: $($_.Exception.Message)" 'Build failed' }
    }
    if (-not (Test-PrefixDone)) { Fail "The libraries were built but $prefix is missing some of them. Please report this." 'Build failed' }
    # The source code and the libraries' build folders aren't needed any more.
    if (-not $Sdl2Src -and -not $GlslangSrc) { Remove-Tree (Join-Path $deps 'src') }
    Remove-Tree (Join-Path $deps 'build')
}

# ---- Vulkan on in the build folder; without -Prepare, gt5.exe rebuilt ---------------------------------
Set-CacheVulkan $true
if ($Prepare) { Stop-AddOn 0 'Vulkan is ready: the next build of the game has it.' 'Vulkan ready' }
Write-Host '== Building gt5.exe (the runtime only, a few minutes)'
Send-Add 'game' $Bar.G 'Building the game'
$rc = Invoke-Counted { cmd /c "`"$PSScriptRoot\build.cmd`" gt5" } 'game' $Bar.G 99 'Building the game'
if ($rc) { Fail 'Building gt5.exe with Vulkan failed (see above).' 'Build failed' }
if (-not (Test-Path (Join-Path $build 'SDL2.dll'))) { Fail 'The build finished but build\SDL2.dll is missing. Please report this.' 'Build failed' }
# The DLSS and FSR 3 plug-ins, when they are in, were built before the Vulkan headers were here, so without
# their Vulkan part: each is built again now (its add script, its full text in GT5\logs\add_*.log; its
# progress lines are not passed on, this run's progress is Vulkan's). DLSS's plug-in being there means its
# licence was accepted when it was added (update.ps1 goes by the same rule). A failure leaves that plug-in as
# it was (Direct3D 12 only) and Vulkan in; the next update builds it again.
function Invoke-PlugInAgain([string]$Script, [string[]]$ArgList, [string]$Log) {
    $logs = Join-Path $gt5 'logs'
    $w = $null
    try {
        New-Item -ItemType Directory -Force $logs | Out-Null
        $w = New-Object IO.StreamWriter((Join-Path $logs "$Log.log"), $false, (New-Object Text.UTF8Encoding($false)))
        $w.AutoFlush = $true
        $w.WriteLine("$Script $($ArgList -join ' ') (from add_vulkan.ps1), $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
    } catch { $w = $null }
    & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot $Script) @ArgList -NoPause 2>&1 | ForEach-Object {
        if ($w) { try { $w.WriteLine("$_") } catch {} }
    }
    $code = [int]$LASTEXITCODE
    if ($w) { try { $w.Close() } catch {} }
    return $code
}
foreach ($p in @(@{ Dll = 'gt5_upscale_dlss.dll'; Script = 'add_dlss.ps1'; Args = @('-Accept'); Log = 'add_dlss'; Name = 'NVIDIA DLSS' },
                 @{ Dll = 'gt5_upscale_fsr.dll';  Script = 'add_fsr.ps1';  Args = @();          Log = 'add_fsr';  Name = 'AMD FSR 3' })) {
    if (-not (Test-Path (Join-Path $build $p.Dll))) { continue }
    Write-Host "== $($p.Name): its plug-in built again with its Vulkan part"
    Send-Add 'game' 99 'Finishing up'
    $r = Invoke-PlugInAgain $p.Script $p.Args $p.Log
    if ($r -eq 1) { Write-Host "   $($p.Name): not built again this time (GT5\logs\$($p.Log).log says why); it works with Direct3D 12 as before." }
}
Stop-AddOn 0 'Vulkan is in. Pick Display > Renderer: Vulkan in the launcher (or set "GT5_RENDERER=vulkan" in settings.bat).' 'Vulkan ready'
