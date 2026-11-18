<#
addon_common.ps1: what the add scripts (add_vulkan.ps1, add_fsr.ps1, add_dlss.ps1) share, and the one
progress format they all print. They dot-source it; setup.ps1 and update.ps1 dot-source it for
Invoke-AddOn, which runs an add script with its full text going to GT5\logs and only the progress lines
passed on.

Progress. Besides their full text (for the log), the add scripts print lines of this one form on standard
output, and nothing else that starts with GT5_ADD:

    GT5_ADD <phase> <percent> <words>

  phase    start   the script began; words: the part's name ("Vulkan", "AMD FSR 3", "NVIDIA DLSS")
           fetch   downloading
           check   checking what was downloaded
           build   building the part
           game    building the game with it (add_vulkan.ps1 without -Prepare)
           remove  taking it out
           done    finished: the part is in (after -Remove: out). Percent 100.
           skip    nothing was done and nothing is wrong: not for this PC (no NVIDIA card, say) or not wanted
           fail    stopped; words say why. Nothing half-built is left looking finished.
  percent  0 to 100 for the whole run, or - when not known
  words    a few plain English words a person can read (the rest of the line)

Exit code: 0 done, 1 fail, 2 skip. With -NoPause a script asks nothing and waits for nothing: it can be
started by a program (the launcher, setup, update, the game) with no window and no input.
#>

# ---- For the add scripts ---------------------------------------------------------------------------------

# One progress line (see above). $pct: a number 0-100, or -1 / $null for "not known".
function Send-Add([string]$phase, $pct, [string]$words) {
    $p = '-'
    if ($null -ne $pct -and "$pct" -ne '' -and [double]$pct -ge 0) { $p = [string][int][math]::Min(100, [math]::Floor([double]$pct)) }
    Write-Host "GT5_ADD $phase $p $words"
}

# The end of an add script: the last words for the log, the last progress line, the exit code. Without
# -NoPause (someone ran it by hand in a console) it waits for Enter so the window can be read.
function Stop-AddOn([int]$code, [string]$msg, [string]$words = '') {
    Write-Host ''
    if ($code -eq 1) { Write-Host $msg -ForegroundColor Red } elseif ($code -eq 2) { Write-Host $msg -ForegroundColor Yellow } else { Write-Host $msg -ForegroundColor Green }
    if ($code -eq 0) { Send-Add 'done' 100 $(if ($words) { $words } else { 'Done' }) }
    elseif ($code -eq 2) { Send-Add 'skip' -1 $(if ($words) { $words } else { 'Not needed here' }) }
    else { Send-Add 'fail' -1 $(if ($words) { $words } else { 'Stopped' }) }
    if (-not $script:AddNoPause) { Write-Host ''; Read-Host 'Press Enter to close' | Out-Null }
    exit $code
}

# Git for this script: trusted on drives that don't record file owners (exFAT, FAT32: "dubious ownership",
# #41) through git's environment config, never asks for a password or opens a sign-in window, and is the
# PC's own or else the port's portable copy in tools\git (get-git.ps1; nothing installed in Windows).
function Initialize-AddOn([string]$gt5, [bool]$noPause) {
    $script:AddNoPause = $noPause
    if ($env:GT5_GIT_SAFE -ne '1') {
        $gn = 0; if ("$env:GIT_CONFIG_COUNT" -match '^\d+$') { $gn = [int]$env:GIT_CONFIG_COUNT }
        Set-Item "env:GIT_CONFIG_KEY_$gn" 'safe.directory'; Set-Item "env:GIT_CONFIG_VALUE_$gn" '*'
        $env:GIT_CONFIG_COUNT = "$($gn + 1)"; $env:GT5_GIT_SAFE = '1'
    }
    $env:GIT_TERMINAL_PROMPT = '0'; $env:GCM_INTERACTIVE = 'never'
    $ownGit = Join-Path (Split-Path -Parent $gt5) 'tools\git\cmd'
    if (-not (Get-Command git -ErrorAction SilentlyContinue) -and (Test-Path -LiteralPath (Join-Path $ownGit 'git.exe'))) { $env:Path = "$ownGit;$env:Path" }
}

# A folder and everything in it, git's read-only files too. cmd's rmdir: Remove-Item took minutes for the
# thousands of small files of a source checkout on a slow drive.
function Remove-Tree([string]$path) {
    if (-not $path -or -not (Test-Path -LiteralPath $path)) { return }
    cmd /c "rmdir /s /q `"$path`"" 2>$null
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction SilentlyContinue }
}

# This install's game is running (its exe is in GT5\build).
function Test-GameRunning([string]$build) {
    return [bool](Get-Process gt5 -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -like "$build\*" })
}

# The compiler and tools build.cmd uses (Visual Studio's, or get-toolchain.ps1's portable ones), into this
# process's environment. False when build.cmd can't set them up; its own reason is then in the output.
function Import-CompilerEnv([string]$scripts) {
    $lines = cmd /c "set GT5_ENV_ONLY=1&& call `"$scripts\build.cmd`" >nul 2>nul && set"
    if ($LASTEXITCODE) {
        # Again with build.cmd's own words shown (which compiler it looked for, and where), for the log.
        cmd /c "set GT5_ENV_ONLY=1&& call `"$scripts\build.cmd`"" 2>&1 | ForEach-Object { Write-Host "  $_" }
        return $false
    }
    foreach ($l in $lines) { if ($l -match '^([^=]+)=(.*)$' -and $Matches[1] -ne 'GT5_ENV_ONLY') { Set-Item "env:$($Matches[1])" $Matches[2] } }
    return [bool]$env:CLANGCL
}

# git with what it says shown, except "warning: refs/tags/... is not a commit!" (said each time a release tag
# that is a tag object is named: it means nothing here).
function Invoke-GitQuiet {
    $out = & git @args 2>&1
    $code = $LASTEXITCODE
    foreach ($l in $out) { $t = "$l"; if ($t -and $t -notmatch 'is not a commit') { Write-Host $t } }
    $global:LASTEXITCODE = $code
}

# A git download, with progress from the size of the folder it fills ($expectMB: about how big it ends up)
# mapped onto $pct0..$pct1 of the whole run. git's own text goes to the log afterwards. Returns git's exit code.
function Invoke-Fetch([string[]]$gitArgs, [string]$dir, [double]$expectMB, [double]$pct0, [double]$pct1, [string]$words) {
    $tmp = [IO.Path]::GetTempFileName()
    $p = Start-Process -FilePath git -ArgumentList $gitArgs -NoNewWindow -PassThru -RedirectStandardError $tmp -RedirectStandardOutput "$tmp.out"
    $null = $p.Handle   # so ExitCode is kept after the exit (Windows PowerShell 5.1)
    $last = -1
    while (-not $p.WaitForExit(1000)) {
        $mb = 0.0
        try { $mb = ((Get-ChildItem -LiteralPath $dir -Recurse -Force -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum) / 1MB } catch {}
        $pct = $pct0 + ($pct1 - $pct0) * [math]::Min(0.95, $mb / [math]::Max(1, $expectMB))
        if ([int]$pct -ne $last) { $last = [int]$pct; Send-Add 'fetch' $pct $words }
    }
    $p.WaitForExit()
    $code = $p.ExitCode
    foreach ($f in "$tmp.out", $tmp) {
        foreach ($l in @(Get-Content -LiteralPath $f -ErrorAction SilentlyContinue)) { if ($l -and $l -notmatch 'is not a commit') { Write-Host $l } }
        Remove-Item -LiteralPath $f -Force -ErrorAction SilentlyContinue
    }
    Send-Add 'fetch' $pct1 $words
    return [int]$code
}

# A build whose output counts its steps ("[12/240] ..." from ninja): every line shown, the count mapped onto
# $pct0..$pct1 of the whole run as $phase progress lines. Returns the exit code.
function Invoke-Counted([scriptblock]$run, [string]$phase, [double]$pct0, [double]$pct1, [string]$words) {
    $st = @{ Last = -1 }
    & $run 2>&1 | ForEach-Object {
        $l = "$_"
        Write-Host $l
        if ($l -match '^\[(\d+)/(\d+)\]' -and [int]$Matches[2] -gt 0) {
            $pct = $pct0 + ($pct1 - $pct0) * [int]$Matches[1] / [int]$Matches[2]
            if ([int]$pct -ne $st.Last) { $st.Last = [int]$pct; Send-Add $phase $pct $words }
        }
    }
    return [int]$LASTEXITCODE
}

# An NVIDIA RTX graphics card (DLSS needs one; GTX cards can't run it). The same test as the launcher's and the
# installer's. GT5_INSTALL_NORTX=1 (testing, as in GT5-Installer.bat): as if there were none.
function Test-RtxCard {
    if ($env:GT5_INSTALL_NORTX) { return $false }
    return [bool](@(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue) | Where-Object { "$($_.Name)" -match 'NVIDIA' -and "$($_.Name)" -match '\bRTX\b' })
}

# ---- For setup.ps1 and update.ps1 ---------------------------------------------------------------------------

# Runs an add script with no window and nothing to answer. Its whole text goes to GT5\logs\<Log>.log; only
# its GT5_ADD progress lines are passed on (the installer's and the update window's progress). Returns the
# script's exit code (0 done, 1 failed, 2 skipped); it never stops the caller.
function Invoke-AddOn([string]$Script, [string[]]$ArgList = @(), [string]$Log, [string]$Logs) {
    if (-not (Test-Path -LiteralPath $Script)) { return 1 }
    $w = $null
    try {
        New-Item -ItemType Directory -Force $Logs | Out-Null
        $w = New-Object IO.StreamWriter((Join-Path $Logs "$Log.log"), $false, (New-Object Text.UTF8Encoding($false)))
        $w.AutoFlush = $true
        $w.WriteLine("$(Split-Path -Leaf $Script) $($ArgList -join ' '), $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
    } catch { $w = $null }
    & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $Script @ArgList -NoPause 2>&1 | ForEach-Object {
        $l = "$_"
        if ($w) { try { $w.WriteLine($l) } catch {} }
        if ($l.StartsWith('GT5_ADD ')) { Write-Host $l }
    }
    $code = [int]$LASTEXITCODE
    if ($w) { try { $w.WriteLine("(exit code $code)"); $w.Dispose() } catch {} }
    return $code
}
