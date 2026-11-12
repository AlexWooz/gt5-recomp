# Runs the build for build.cmd (inside its Visual Studio environment) and recovers from a crashed or
# damaged compile, which heavy parallel builds do now and then, especially on some 13th/14th-gen Intel
# CPUs: a compiler crash leaves no object and is simply redone; a damaged object (the link then fails
# with undefined "llvm." symbols) is deleted so it gets compiled again. Each retry halves the jobs.
#   build_run.ps1 -Jobs <n> [targets...]      (the whole output also goes to build\last_build.log)
param([int]$Jobs = 8, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Targets)
$build = Join-Path (Split-Path $PSScriptRoot -Parent) "build"
$log = Join-Path $build "last_build.log"

function Invoke-Build([int]$j) {
    $t = if ($Targets) { " --target " + ($Targets -join " ") } else { "" }
    cmd /c "cmake --build `"$build`" -j $j$t 2>&1" | Tee-Object -FilePath $log | Out-Host
    return $LASTEXITCODE
}

# Objects the linker found calling "llvm.*" (compiler-internal names): written damaged, so delete them.
function Remove-DamagedObjects {
    $bad = @{}; $inLlvm = $false
    foreach ($l in (Get-Content -LiteralPath $log -ErrorAction SilentlyContinue)) {
        if ($l -match 'error: undefined symbol: (\S+)') { $inLlvm = $Matches[1].StartsWith("llvm.") }
        elseif ($inLlvm -and $l -match '>>>\s+(\S+\.obj):') { $bad[$Matches[1]] = $true }
    }
    foreach ($o in $bad.Keys) {
        $p = Join-Path $build $o
        if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force; Write-Host "  removed damaged $o (it will be compiled again)" }
    }
}

# A GT5\spu from an older version may have a closed permission list ("Access is denied" from ninja).
& (Join-Path $PSScriptRoot "fix_spu_access.ps1")
if ($LASTEXITCODE -ne 0) { exit 1 }
# The game's data: joins a GT.VOL split by a FAT32 dump, and says when it's missing (the build itself
# doesn't need it, so this never stops the build).
& (Join-Path $PSScriptRoot "fix_game_files.ps1")
$global:LASTEXITCODE = 0
# After a good build: the launcher (GT5Launcher.exe in the install folder), rebuilt with every update.
function Exit-Built {
    $lb = Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) "launcher\build-launcher.ps1"
    if ((-not $Targets -or $Targets -contains "gt5") -and (Test-Path $lb)) { & $lb }
    exit 0
}
Write-Host "Building with $Jobs parallel jobs (set GT5_JOBS to change)."
if ((Invoke-Build $Jobs) -eq 0) { Exit-Built }
# Up to four retries: the linker names only a few files per error, so damaged objects surface one at a time.
foreach ($try in 1..4) {
    $Jobs = [math]::Max(1, [int][math]::Floor($Jobs / 2))
    Write-Host ""
    Write-Host "The build stopped. Trying again with $Jobs parallel jobs; it carries on where it stopped..." -ForegroundColor Yellow
    Remove-DamagedObjects
    if ((Invoke-Build $Jobs) -eq 0) { Exit-Built }
}
exit 1
