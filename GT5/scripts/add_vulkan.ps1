<#
add_vulkan.ps1: puts the Vulkan renderer into this PC's gt5.exe, once. The launcher runs it when you pick
Display > Renderer: Vulkan. DirectX 12 stays the default and keeps working whatever happens here.

Vulkan needs two libraries the normal build leaves out: SDL2 (its window) and glslang (turns the game's
shaders into Vulkan's SPIR-V). This fetches their source code at a fixed release with git (about 60 MB),
checks each is exactly that release, builds them into GT5\deps\vulkan with the compiler the game is built
with, and rebuilds gt5.exe with Vulkan in. The game's generated code is not rebuilt, only the runtime:
about 10 to 20 minutes. After that, build\SDL2.dll next to gt5.exe says the exe has Vulkan.

  powershell -File GT5\scripts\add_vulkan.ps1            # add Vulkan
  powershell -File GT5\scripts\add_vulkan.ps1 -Remove    # build gt5.exe without it again
  -Sdl2Src / -GlslangSrc <folder>: use source code already on this PC (the same releases) instead of git.
#>
param(
    [switch]$Remove,
    [string]$Sdl2Src = '',
    [string]$GlslangSrc = '',
    [switch]$NoPause
)
# Continue: git and cmake write progress to stderr; each step's exit code says whether it worked.
$ErrorActionPreference = 'Continue'
# A drive that doesn't record file owners (exFAT, FAT32) makes git refuse the folders it clones here
# ("dubious ownership", #41): trust them for this script's git, through git's environment config.
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
$deps = Join-Path $gt5 'deps\vulkan'
$prefix = Join-Path $deps 'prefix'
$cm = { param($p) $p -replace '\\', '/' }   # CMake wants forward slashes

# The releases, by the commit git checks them against.
$Pins = @(
    @{ Name = 'SDL2 2.32.10'; Dir = 'SDL2-2.32.10'; Url = 'https://github.com/libsdl-org/SDL.git'; Tag = 'release-2.32.10'
       Commit = '5d249570393f7a37e037abf22cd6012a4cc56a71' },
    @{ Name = 'glslang 15.4.0'; Dir = 'glslang-15.4.0'; Url = 'https://github.com/KhronosGroup/glslang.git'; Tag = '15.4.0'
       Commit = '8a85691a0740d390761a1008b4696f57facd02c4' }
)

function Done([int]$code, [string]$msg) {
    Write-Host ''
    if ($code) { Write-Host $msg -ForegroundColor Red } else { Write-Host $msg -ForegroundColor Green }
    if (-not $NoPause) { Write-Host ''; Read-Host 'Press Enter to close' | Out-Null }
    exit $code
}
function Step([string]$what, [scriptblock]$do) {
    Write-Host "== $what"
    $global:LASTEXITCODE = 0
    & $do
    if ($LASTEXITCODE) { Done 1 "$what failed (see above). Nothing else was changed: DirectX 12 still works. Please report it with this window's text." }
}

if (Get-Process gt5 -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -like "$build\*" }) {
    Done 1 'The game is running: close it first, then run this again.'
}
if (-not (Test-Path (Join-Path $build 'build.ninja'))) {
    Done 1 "There's no game build in $build yet. Run setup first (setup.bat in the install folder)."
}

# The compiler and tools build.cmd uses (Visual Studio's, or get-toolchain.ps1's portable ones).
$envLines = cmd /c "set GT5_ENV_ONLY=1&& call `"$PSScriptRoot\build.cmd`" >nul 2>nul && set"
if ($LASTEXITCODE) { Done 1 "Couldn't set up the compiler: run GT5\scripts\build.cmd gt5 to see why." }
foreach ($l in $envLines) { if ($l -match '^([^=]+)=(.*)$' -and $Matches[1] -ne 'GT5_ENV_ONLY') { Set-Item "env:$($Matches[1])" $Matches[2] } }
$cc = $env:CLANGCL
if (-not $cc) { Done 1 "Couldn't find clang-cl: run GT5\scripts\build.cmd gt5 to see why." }
$toolArgs = @([regex]::Matches([string]$env:TOOLARGS, '"([^"]*)"') | ForEach-Object { $_.Groups[1].Value })

if ($Remove) {
    Step 'Turning Vulkan off in the build' { cmake -S $gt5 -B $build -DPS3RECOMP_RSX_VULKAN=OFF }
    Step 'Building gt5.exe' { cmd /c "`"$PSScriptRoot\build.cmd`" gt5" }
    Done 0 'gt5.exe is built without Vulkan again (DirectX 12 only). GT5\deps\vulkan can be deleted.'
}

$jobs = $env:GT5_JOBS
if (-not $jobs) {
    $ram = [int][math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 2GB)
    $jobs = [math]::Max(1, [math]::Min([int]$env:NUMBER_OF_PROCESSORS, $ram))
}
$common = @('-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_C_COMPILER=$cc", "-DCMAKE_CXX_COMPILER=$cc",
            "-DCMAKE_INSTALL_PREFIX=$(& $cm $prefix)", "-DCMAKE_PREFIX_PATH=$(& $cm $prefix)") + $toolArgs

# ---- The source code, for what isn't built yet ------------------------------------------------------------
$needSdl = -not (Test-Path (Join-Path $prefix 'bin\SDL2.dll')) -or -not (Test-Path (Join-Path $prefix 'cmake\SDL2Config.cmake'))
$needHeaders = -not (Test-Path (Join-Path $prefix 'include\vulkan\vulkan.h'))
$needGls = -not (Test-Path (Join-Path $prefix 'lib\cmake\glslang\glslang-config.cmake'))
$src = @{}
foreach ($p in $Pins) {
    if ($p.Dir -like 'SDL2*') { if (-not ($needSdl -or $needHeaders)) { continue } } elseif (-not $needGls) { continue }
    $given = if ($p.Dir -like 'SDL2*') { $Sdl2Src } else { $GlslangSrc }
    if ($given) {
        if (-not (Test-Path (Join-Path $given 'CMakeLists.txt'))) { Done 1 "$given has no CMakeLists.txt: not the $($p.Name) source code." }
        $src[$p.Dir] = $given; continue
    }
    $d = Join-Path $deps "src\$($p.Dir)"
    $have = if (Test-Path (Join-Path $d '.git')) { git -C $d rev-parse HEAD } else { '' }
    if ($have -ne $p.Commit) {
        if (Test-Path $d) { Remove-Item -Recurse -Force $d -ErrorAction Stop }
        New-Item -ItemType Directory -Force (Split-Path $d) | Out-Null
        Write-Host "== Fetching $($p.Name) ($($p.Url))"
        git -c advice.detachedHead=false clone --quiet --depth 1 --branch $p.Tag $p.Url $d
        if ($LASTEXITCODE) { Done 1 "Fetching $($p.Name) failed: check the internet connection and run this again. DirectX 12 still works." }
        $have = git -C $d rev-parse HEAD
        if ($have -ne $p.Commit) {
            Remove-Item -Recurse -Force $d
            Done 1 "$($p.Name) isn't the expected release (commit $have, expected $($p.Commit)), so it was deleted. Please report this."
        }
    }
    $src[$p.Dir] = $d
}
$sdl = $src['SDL2-2.32.10']; $gls = $src['glslang-15.4.0']

# ---- SDL2 and glslang, into GT5\deps\vulkan\prefix ------------------------------------------------------
if ($needSdl) {
    $b = Join-Path $deps 'build\sdl2'
    # SDL_LIBC=ON: with the C runtime. Without it SDL2 builds against no C library at all, and newer
    # clang (Visual Studio 18's clang 22) turns its own string loops into strlen / wcslen calls that
    # then don't link. gt5.exe uses the same C runtime DLLs, so this adds nothing to install.
    Step 'SDL2: configure' { cmake -S $sdl -B $b @common -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_LIBC=ON }
    Step "SDL2: build ($jobs jobs)" { cmake --build $b --target install -j $jobs }
}
if ($needGls) {
    $b = Join-Path $deps 'build\glslang'
    Step 'glslang: configure' { cmake -S $gls -B $b @common -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF `
        -DENABLE_GLSLANG_BINARIES=OFF -DGLSLANG_ENABLE_INSTALL=ON -DENABLE_HLSL=ON -DBUILD_SHARED_LIBS=OFF `
        -DBUILD_EXTERNAL=OFF -DENABLE_SPVREMAPPER=OFF }
    Step "glslang: build ($jobs jobs)" { cmake --build $b --target install -j $jobs }
}
# The Vulkan headers: SDL2 carries the Khronos ones. Copied last, as GT5's CMakeLists turns Vulkan on for a
# new build folder when it finds them.
if ($needHeaders) {
    Copy-Item -Recurse -Force (Join-Path $sdl 'src\video\khronos\*') (Join-Path $prefix 'include') -ErrorAction Stop
}

# ---- gt5.exe with Vulkan in ------------------------------------------------------------------------------
Step 'Turning Vulkan on in the build' { cmake -S $gt5 -B $build -DPS3RECOMP_RSX_VULKAN=ON }
Step 'Building gt5.exe (the runtime only, a few minutes)' { cmd /c "`"$PSScriptRoot\build.cmd`" gt5" }
if (-not (Test-Path (Join-Path $build 'SDL2.dll'))) { Done 1 'The build finished but build\SDL2.dll is missing. Please report this window''s text.' }
# The source code and the libraries' build folders aren't needed any more.
if (-not $Sdl2Src -and -not $GlslangSrc) { Remove-Item -Recurse -Force (Join-Path $deps 'src'), (Join-Path $deps 'build') -ErrorAction SilentlyContinue }
else { Remove-Item -Recurse -Force (Join-Path $deps 'build') -ErrorAction SilentlyContinue }
Done 0 'Vulkan is in. Pick Display > Renderer: Vulkan in the launcher (or set "GT5_RENDERER=vulkan" in settings.bat).'
