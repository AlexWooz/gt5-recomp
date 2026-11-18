# Keeps the install off processors that calculate wrongly. Dot-sourced by setup.ps1, update.ps1 and
# GT5-Installer.bat; by hand:
#   powershell -ExecutionPolicy Bypass -File pc-check.ps1        (runs the check and prints the result)
#
# Why: the game is built on your PC by long calculations (Python tools that turn the game's code into
# C++, then the compiler). They assume every processor gives the right answer. On some PCs one or two
# logical processors don't: a tool then stops with an error that makes no sense, the compiler crashes,
# or, worse, a wrong result goes unnoticed into the game. Seen on a 14th generation Intel desktop
# processor, where two of 28 logical processors failed a deterministic job most of the time and the
# other 26 never did.
#
# What it does: GT5\scripts\pc_check.py runs one small deterministic job on every logical processor,
# eight at a time, four times each (a minute or two), and compares the results. If all agree,
# nothing changes. If some processors give errors or different results, this process and everything it
# starts (the code generation, the compiler) are confined to the processors that passed, and you are
# told once. The result is kept in .gt5\pc-check.json for 30 days, so updates don't repeat the check.
# Fixing the PC is the real cure (a BIOS update, the processor maker's default settings, drivers); the
# port just doesn't let it break the install.

function Use-GoodProcessors {
    # Returns a sentence for the player when processors were left out, else nothing.
    param([string]$Base, [string]$Py, [switch]$Again)
    $script = Join-Path $Base "GT5\scripts\pc_check.py"
    $n = [Environment]::ProcessorCount
    if (-not (Test-Path -LiteralPath $script) -or -not $Py -or -not (Test-Path -LiteralPath $Py)) { return }
    if ($n -lt 3 -or $n -gt 64) { return }      # nothing to choose from, or more than one processor group
    $rec = Join-Path $Base ".gt5\pc-check.json"
    $cpu = ""
    try { $cpu = "$((Get-CimInstance Win32_Processor -ErrorAction Stop | Select-Object -First 1).Name)".Trim() } catch { }
    $bad = $null; $old = @()
    if (Test-Path -LiteralPath $rec) {
        try {
            $r = Get-Content -LiteralPath $rec -Raw | ConvertFrom-Json
            if ($r.processor -eq $cpu -and [int]$r.logical -eq $n) {
                $old = @($r.bad | ForEach-Object { [int]$_ })
                if (-not $Again -and ((Get-Date) - [datetime]$r.when).TotalDays -lt 30) { $bad = $old }
            }
        } catch { }
    }
    if ($null -eq $bad) {
        Write-Host "            Checking that every processor of this PC calculates correctly (a minute or two)..."
        $tmp = Join-Path ([IO.Path]::GetTempPath()) ("gt5_pc_check_" + [guid]::NewGuid().ToString("N") + ".json")
        $rounds = if ($Again) { 6 } else { 4 }
        & $Py -I $script --rounds $rounds --json $tmp | Out-Null
        if (-not (Test-Path -LiteralPath $tmp)) { Write-Host "            (the check could not run here; going on without it)"; return }
        $new = @()
        try {
            $j = Get-Content -LiteralPath $tmp -Raw | ConvertFrom-Json
            $new = @($j.bad.PSObject.Properties | ForEach-Object { [int]$_.Name })
        } catch { }
        Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
        # a processor that failed once stays out: the faults come and go
        $bad = @(@($old) + @($new) | Sort-Object -Unique)
        try {
            New-Item -ItemType Directory -Force (Join-Path $Base ".gt5") | Out-Null
            $save = @{ when = (Get-Date).ToString("s"); processor = $cpu; logical = $n; bad = @($bad) } | ConvertTo-Json
            [IO.File]::WriteAllText($rec, $save)
        } catch { }
        if (-not $bad.Count) { Write-Host "            All $n processors gave the same answers." }
    }
    if (-not $bad.Count) { return }
    $good = $n - $bad.Count
    if ($good -lt 2) {
        return "Almost every processor of this PC gave wrong answers in a test calculation, so none can be left out. The install goes on, but expect it to fail: this PC needs fixing first (README.md, 'A processor that calculates wrongly')."
    }
    $mask = [long]0
    for ($i = 0; $i -lt $n; $i++) { if ($bad -notcontains $i) { $mask = $mask -bor ([long]1 -shl $i) } }
    try {
        $p = [Diagnostics.Process]::GetCurrentProcess()
        $cur = [long]$p.ProcessorAffinity
        if (($cur -band $mask) -ne 0) { $mask = $cur -band $mask }     # inside a limit somebody already set
        $p.ProcessorAffinity = [IntPtr]$mask
    } catch { return "Processors $($bad -join ', ') of this PC gave wrong answers in a test calculation, and the install could not be kept off them ($($_.Exception.Message))." }
    $which = if ($bad.Count -eq 1) { "Processor $($bad[0])" } else { "Processors $($bad -join ', ')" }
    return "$which of this PC's $n gave wrong answers in a test calculation. The install goes on using the other $good only. This is a fault of the PC (processor, BIOS settings or a driver), not of the game: see README.md, 'A processor that calculates wrongly'."
}

# Run by itself: the check, in full, and what the port would do about it.
if ($MyInvocation.InvocationName -ne '.') {
    $base = $PSScriptRoot
    $py = Join-Path $base "venv\Scripts\python.exe"
    if (-not (Test-Path -LiteralPath $py)) { $py = Join-Path $base "tools\python\python.exe" }
    if (-not (Test-Path -LiteralPath $py)) { Write-Host "Run setup.ps1 first: the check needs the port's Python."; exit 2 }
    & $py -I (Join-Path $base "GT5\scripts\pc_check.py") @args
    exit $LASTEXITCODE
}
