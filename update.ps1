# Update this GT5 Recomp install to the newest version, or switch to any other version (older too).
# Run it in PowerShell from the folder you cloned:
#   powershell -ExecutionPolicy Bypass -File update.ps1                     newest release
#   powershell -ExecutionPolicy Bypass -File update.ps1 -List               list the versions
#   powershell -ExecutionPolicy Bypass -File update.ps1 -Version v0.1.0     switch to that version
#   powershell -ExecutionPolicy Bypass -File update.ps1 -Dev                newest changes not yet released
# -NoBuild switches without building (build later with GT5\scripts\build.cmd gt5); -Force re-lifts and
# rebuilds even when nothing changed. Easiest: the launcher's Updates page, or double-click Update.bat.
# (-Relaunch <exe>: the launcher's Update button; this opens it again when done.)
#
# Kept: your saves (GT5\hdd0), your game files, the shader cache, and edits you made to files such as
# play.bat (set aside during the switch and put back). The previous gt5.exe is kept as
# GT5\build\gt5.previous.exe. The slow re-lift only runs when a version changes the lift tools;
# otherwise only the changed code is rebuilt.
param([string]$Version, [switch]$List, [switch]$Dev, [switch]$NoBuild, [switch]$Force, [switch]$Here, [string]$Relaunch)
# Errors are checked through exit codes: with "Stop", Windows PowerShell turns git's progress messages
# on stderr into errors.
$ErrorActionPreference = "Continue"
# On a drive that doesn't record who owns a file (exFAT, FAT32, many USB and network drives) git refuses
# to work in the install folder ("detected dubious ownership", #41). Trust it for git run from here and
# what this starts, through git's environment config; the player's own git settings stay as they are.
if ($env:GT5_GIT_SAFE -ne "1") {
    $gn = 0; if ("$env:GIT_CONFIG_COUNT" -match '^\d+$') { $gn = [int]$env:GIT_CONFIG_COUNT }
    Set-Item "env:GIT_CONFIG_KEY_$gn" "safe.directory"; Set-Item "env:GIT_CONFIG_VALUE_$gn" "*"
    $env:GIT_CONFIG_COUNT = "$($gn + 1)"; $env:GT5_GIT_SAFE = "1"
}
$base = $PSScriptRoot
# Git: the PC's own, else the port's own portable copy in tools\git (get-git.ps1; nothing installed in Windows).
$ownGit = Join-Path $base "tools\git\cmd"
if (-not (Get-Command git -ErrorAction SilentlyContinue) -and (Test-Path -LiteralPath (Join-Path $ownGit "git.exe"))) { $env:Path = "$ownGit;$env:Path" }
$gt5 = Join-Path $base "GT5"
$rc = Join-Path $base "ps3recomp"
$state = Join-Path $base ".gt5"
$backups = Join-Path $base "backups"
$py = Join-Path $base "venv\Scripts\python.exe"
$stamp = Get-Date -Format "yyyy-MM-dd_HHmm"
$fallbackBase = "4e8f3ca87a3e1d87bb83b8b7e9348fe2685cd514"   # versions from before ps3recomp-base.txt existed

# Started from the launcher: open it again at the end; after a failure, wait so the message can be read.
function Finish([int]$code) {
    if ($Relaunch) {
        if ($code -ne 0) { Write-Host ""; Read-Host "Press Enter to close this window" | Out-Null }
        if (Test-Path -LiteralPath $Relaunch) { Start-Process -FilePath $Relaunch }
    }
    exit $code
}
function Fail([string]$m) { Write-Host "update: $m" -ForegroundColor Red; Finish 1 }
function Warn([string]$m) { Write-Host $m -ForegroundColor Yellow }
function Describe {
    if (-not (Test-Path (Join-Path $base ".git"))) { return "a downloaded copy (not a git clone)" }
    $t = git -C $base describe --tags --exact-match HEAD 2>$null
    if ($t) { return $t }
    $d = git -C $base describe --tags 2>$null
    if ($d) { return "development build $d" }
    return "development build $(git -C $base rev-parse --short HEAD)"
}
function Read-Base([string]$file) {
    if (Test-Path $file) { return (Get-Content $file -Raw).Trim() }
    return $fallbackBase
}

# -Here (setup.ps1's step) also works in a ZIP download; switching versions needs a git clone.
if (-not $Here -and -not (Test-Path (Join-Path $base ".git"))) {
    Fail "this folder isn't a git clone. Install with: git clone https://github.com/MormonFTP-hub/gt5-recomp (see README.md)"
}
if (Test-Path (Join-Path $base ".git\gt5-publish-repo")) { Fail "this is the publishing copy of the repository, not an install" }
if (-not (Test-Path (Join-Path $gt5 "scripts\relift.ps1"))) { Fail "GT5\scripts is missing: this isn't a full checkout" }
# Only this install's game matters (another copy elsewhere can keep running).
$mine = Get-Process gt5 -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($base, [StringComparison]::OrdinalIgnoreCase) }
if ($mine) { Fail "close the game first" }

# ---- List the versions ------------------------------------------------------------------------------
if ($List) {
    git -C $base fetch --quiet --tags origin
    $cur = Describe
    $latest = git -C $base tag --list "v*" --sort=-v:refname | Select-Object -First 1
    Write-Host "Installed: $cur"
    Write-Host ""
    $rows = git -C $base for-each-ref "refs/tags/v*" --sort=-version:refname --format="%(refname:short)|%(creatordate:short)|%(subject)"
    if (-not $rows) { Write-Host "No releases yet: -Dev gets the newest changes." }
    foreach ($r in $rows) {
        $p = $r -split '\|', 3
        $mark = ""
        if ($p[0] -eq $latest) { $mark += "  [newest]" }
        if ($p[0] -eq $cur) { $mark += "  <- installed" }
        Write-Host ("{0,-9} {1}  {2}{3}" -f $p[0], $p[1], $p[2], $mark)
    }
    exit 0
}

$from = Describe
New-Item -ItemType Directory -Force $state | Out-Null

# The patch now in ps3recomp, saved before the switch replaces GT5\patches with the new version's.
$patch = Join-Path $gt5 "patches\ps3recomp.patch"
$baseFile = Join-Path $gt5 "patches\ps3recomp-base.txt"
$oldPatch = Join-Path $state "applied.patch"
$oldBase = Read-Base (Join-Path $state "applied-base.txt")
if (-not (Test-Path $oldPatch) -and (Test-Path (Join-Path $rc ".git")) -and (git -C $rc status --porcelain)) {
    # Installed before update.ps1 existed (maybe followed by a `git pull`, so this folder's patch can
    # already be newer than ps3recomp's): find which version's patch ps3recomp carries.
    $cand = Join-Path $state "pre-update.patch"
    foreach ($c in (git -C $base log --format=%H -n 40 -- GT5/patches/ps3recomp.patch)) {
        cmd /c "git -C `"$base`" show ${c}:GT5/patches/ps3recomp.patch > `"$cand`""
        git -C $rc apply --whitespace=nowarn --reverse --check $cand 2>$null
        if ($LASTEXITCODE -eq 0) {
            $oldPatch = $cand
            $b = git -C $base show "${c}:GT5/patches/ps3recomp-base.txt" 2>$null
            if ($LASTEXITCODE -eq 0 -and $b) { $oldBase = "$b".Trim() } else { $oldBase = $fallbackBase }
            break
        }
    }
}

# ---- Switch this folder to the chosen version ---------------------------------------------------------
$label = $from
if (-not $Here) {
    git -C $base fetch --quiet --tags origin
    if ($LASTEXITCODE -ne 0) { Fail "couldn't reach GitHub" }
    if ($Dev) {
        $target = "origin/main"; $label = "the newest development build"
    } elseif ($Version) {
        if ($Version -notmatch '^v') { $Version = "v$Version" }
        git -C $base rev-parse -q --verify "refs/tags/$Version" | Out-Null
        if ($LASTEXITCODE -ne 0) { Fail "there's no version $Version (see -List)" }
        $target = $Version; $label = $Version
    } else {
        $target = git -C $base tag --list "v*" --sort=-v:refname | Select-Object -First 1
        if ($target) { $label = $target } else { $target = "origin/main"; $label = "the newest development build" }
    }
    $targetCommit = git -C $base rev-parse "$target^{commit}"
    if ($targetCommit -eq (git -C $base rev-parse HEAD) -and -not $Force -and (Test-Path (Join-Path $state "applied.patch"))) {
        Write-Host "Already on $label."
        Finish 0
    }
    if ($targetCommit -eq (git -C $base rev-parse HEAD)) { Write-Host "Reinstalling $label" }
    else { Write-Host "Switching from $from to $label" }

    # Edits the player made to tracked files (play.bat, say) are set aside and put back after.
    $dirty = git -C $base status --porcelain --untracked-files=no
    $stashed = $false
    if ($dirty) {
        git -C $base stash push --quiet -m "update.ps1 $stamp"
        if ($LASTEXITCODE -eq 0) { $stashed = $true }
    }
    if ($Dev) {
        git -C $base checkout --quiet main
        if ($LASTEXITCODE -eq 0) { git -C $base merge --quiet --ff-only origin/main }
    } else {
        git -C $base checkout --quiet --detach $targetCommit
    }
    if ($LASTEXITCODE -ne 0) {
        if ($stashed) { git -C $base stash pop --quiet }
        Fail "couldn't switch to $label"
    }
    if ($stashed) {
        $files = ($dirty | ForEach-Object { $_.Substring(3) }) -join ", "
        $null = git -C $base stash pop --quiet 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Your edits to $files are back in place."
        } else {
            # The edits clash with this version's changes to the same lines: this version's files win,
            # and the edits are saved so nothing is lost.
            New-Item -ItemType Directory -Force $backups | Out-Null
            $mine = Join-Path $backups "my-edits-$stamp.patch"
            git -C $base reset --quiet --hard HEAD
            cmd /c "git -C `"$base`" stash show -p stash@{0} > `"$mine`""
            git -C $base stash drop --quiet
            Warn "Your edits to $files clash with this version's own changes there. This version's files are in place; your edits are saved in backups\$(Split-Path $mine -Leaf) to copy back by hand."
        }
    }
}

# ---- ps3recomp: this version's base commit plus this version's patch -----------------------------------
$newBase = Read-Base $baseFile
if (-not (Test-Path (Join-Path $rc ".git"))) {
    # No line-ending conversion: the patch matches the files as stored upstream.
    git clone --quiet --config core.autocrlf=false https://github.com/sp00nznet/ps3recomp.git $rc
    if ($LASTEXITCODE -ne 0) { Fail "couldn't clone ps3recomp" }
}
$same = (Test-Path $oldPatch) -and (Test-Path $patch) -and
        ((Get-FileHash $oldPatch).Hash -eq (Get-FileHash $patch).Hash) -and ($oldBase -eq $newBase) -and
        ((git -C $rc rev-parse HEAD) -eq $newBase)
if ($same) {
    Write-Host "ps3recomp: this version's patch is already in place"
} else {
    # 0. Every file either patch touches, with its content hash and timestamp now. Taking the old patch out
    #    and putting the new one in rewrites all of them, shared headers included, so the build saw them
    #    all as changed and recompiled every file of the game's generated code (20 to 50 minutes) even
    #    when an update changed a few runtime files. Below, the ones whose content comes out the same get
    #    their old timestamps back.
    $kept = @{}
    foreach ($pf in @($oldPatch, $patch)) {
        if (-not (Test-Path $pf)) { continue }
        foreach ($m in (Select-String -LiteralPath $pf -Pattern '^diff --git a/(\S+) b/')) {
            $rel = $m.Matches[0].Groups[1].Value
            $full = Join-Path $rc $rel
            if ($kept.ContainsKey($rel) -or -not (Test-Path -LiteralPath $full -PathType Leaf)) { continue }
            $kept[$rel] = @((Get-FileHash -LiteralPath $full -Algorithm SHA1).Hash, (Get-Item -LiteralPath $full).LastWriteTimeUtc)
        }
    }
    # 1. Take the old patch out, keeping anything else changed in ps3recomp.
    $changes = git -C $rc status --porcelain
    if ($changes -and (Test-Path $oldPatch)) {
        git -C $rc apply --whitespace=nowarn --reverse --check $oldPatch 2>$null
        if ($LASTEXITCODE -eq 0) {
            git -C $rc apply --whitespace=nowarn --reverse $oldPatch
            $changes = git -C $rc status --porcelain
        }
    }
    if ($changes) {
        New-Item -ItemType Directory -Force $backups | Out-Null
        $own = Join-Path $backups "ps3recomp-changes-$stamp.patch"
        git -C $rc add -A
        cmd /c "git -C `"$rc`" diff --cached --binary > `"$own`""
        git -C $rc reset --quiet
        Warn "ps3recomp had changes besides the old patch (your own edits?). They're saved in backups\$(Split-Path $own -Leaf)."
    }
    # 2. The ps3recomp commit this version's patch was made against.
    git -C $rc cat-file -e "$newBase^{commit}" 2>$null
    if ($LASTEXITCODE -ne 0) { git -C $rc fetch --quiet origin $newBase }
    git -C $rc checkout --quiet --force $newBase
    if ($LASTEXITCODE -ne 0) { Fail "couldn't check out ps3recomp $newBase" }
    git -C $rc clean -fdq
    # 3. This version's patch.
    git -C $rc apply --whitespace=nowarn --check $patch
    if ($LASTEXITCODE -ne 0) { Fail "this version's patch doesn't apply to ps3recomp $newBase. Please report it." }
    git -C $rc apply --whitespace=nowarn $patch
    if ($LASTEXITCODE -ne 0) { Fail "applying the patch failed" }
    # 4. Files whose content is what it was before get their timestamps back (step 0), so only what this
    #    version really changed is recompiled.
    $unchanged = 0
    foreach ($rel in $kept.Keys) {
        $full = Join-Path $rc $rel
        if ((Test-Path -LiteralPath $full -PathType Leaf) -and
            (Get-FileHash -LiteralPath $full -Algorithm SHA1).Hash -eq $kept[$rel][0]) {
            try { (Get-Item -LiteralPath $full).LastWriteTimeUtc = $kept[$rel][1]; $unchanged++ } catch { }
        }
    }
    Write-Host "ps3recomp: $($kept.Count - $unchanged) patched file(s) changed in this version, $unchanged unchanged"
    Copy-Item $patch (Join-Path $state "applied.patch") -Force
    Set-Content -Path (Join-Path $state "applied-base.txt") -Value $newBase -Encoding ascii
    Remove-Item (Join-Path $state "pre-update.patch") -ErrorAction SilentlyContinue
    Write-Host "ps3recomp: patched for $label"
}
Set-Content -Path (Join-Path $state "version.txt") -Value (Describe) -Encoding ascii
if (Test-Path $py) {
    & $py -m pip install --quiet --disable-pip-version-check -r (Join-Path $rc "requirements.txt")
}

if ($Here -or $NoBuild) {
    if (-not $Here) { Write-Host "Switched to $label. Build with GT5\scripts\build.cmd gt5 (or run update.ps1 -Force) when you're ready." }
    exit 0
}
if (-not (Test-Path (Join-Path $gt5 "input\EBOOT.elf")) -or -not (Test-Path $py)) {
    Write-Host "Switched to $label. Setup isn't finished (no GT5\input\EBOOT.elf or no venv), so nothing was built: see Setup in README.md."
    exit 0
}

# ---- A game update installed in GT5\hdd0 needs its executable in GT5\input (scripts\update_exe.ps1) -------
if (Test-Path (Join-Path $gt5 "scripts\update_exe.ps1")) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $gt5 "scripts\update_exe.ps1") -Gt5 $gt5
    if ($LASTEXITCODE -eq 2) { Warn "The game update in GT5\hdd0 still needs its executable (see above): until then the game stops before the main menu." }
}

# ---- Re-lift only when this version changes what the generated code is made from, then build --------------
# Processors that calculate wrongly are left out of everything from here on (pc-check.ps1 says why).
$pcCheck = Join-Path $base "pc-check.ps1"
if (Test-Path -LiteralPath $pcCheck) {
    . $pcCheck
    $pcNote = Use-GoodProcessors -Base $base -Py $py
    if ($pcNote) { Write-Host $pcNote -ForegroundColor Yellow }
}
Push-Location $gt5
& $py "scripts\lift_stamp.py" --check
if ($LASTEXITCODE -ne 0 -or $Force) {
    Write-Host "Re-lifting (about 10 minutes): this version changes the lift tools, mods or your EBOOT."
    & powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\relift.ps1"
    if ($LASTEXITCODE -ne 0) {
        # A step that stops inside Python itself: the processors are checked again with one of the lift's
        # own jobs, any that calculates wrongly is left out, and the lift starts again.
        if (Get-Command Use-GoodProcessors -ErrorAction SilentlyContinue) {
            $pcNote = Use-GoodProcessors -Base $base -Py $py -Again
            if ($pcNote) { Write-Host $pcNote -ForegroundColor Yellow }
        }
        Write-Host "The re-lift stopped (see above). Trying once more from the start..." -ForegroundColor Yellow
        & powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\relift.ps1"
    }
    if ($LASTEXITCODE -ne 0) { Pop-Location; Fail "the re-lift failed twice (see the messages above)" }
} else {
    # The runtime's HLE function list can change with any version.
    & $py "..\ps3recomp\tools\gen_hle_nids.py" --all --out "src\gen\ppu_hle_nids.cpp"
    if ($LASTEXITCODE -ne 0) { Pop-Location; Fail "gen_hle_nids.py failed" }
}
if (Test-Path "build\gt5.exe") { Copy-Item "build\gt5.exe" "build\gt5.previous.exe" -Force }
Write-Host "Building (only what changed)..."
cmd /c "scripts\build.cmd gt5"
$built = $LASTEXITCODE
Pop-Location
if ($built -ne 0) { Fail "the build failed (see the messages above). The previous game is in GT5\build\gt5.previous.exe." }
Write-Host ""
Write-Host "Done: $from -> $label. Saves, game files, settings and the shader cache were kept." -ForegroundColor Green
Write-Host "The previous build is GT5\build\gt5.previous.exe; to go back, run update.ps1 -Version <version> (see -List)."
if ($Relaunch) { Write-Host "Opening the launcher again..."; Start-Sleep -Seconds 2 }
Finish 0
