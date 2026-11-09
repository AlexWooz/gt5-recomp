# Muted run with movies on, snapshots of every guest thread's stack and guest memory: moviesnap.ps1 -Tag ms -At 45 -Seconds 80 -Trace "cellDmux,cellVdec" -Mem @("dump 628C0000,64")
param([string]$Tag = "ms", [int]$At = 45, [int]$Seconds = 80, [string]$Trace = "cellDmux,cellVdec,cellAdec,cellSail", [string[]]$Mem = @(), [int]$Snaps = 2)
$root = "$(Split-Path $PSScriptRoot -Parent)"
$env:HLE_NAME_TRACE = $Trace
$a = "-NoProfile -ExecutionPolicy Bypass -File `"$root\scripts\racerun.ps1`" -Tag $Tag -Seconds $Seconds -EndAfter 900 -SkipMovie 9999"
$run = Start-Process powershell -WindowStyle Hidden -PassThru -ArgumentList $a
Start-Sleep $At
$out = "$root\logs\$Tag.stacks"
Remove-Item $out -ErrorAction SilentlyContinue
foreach ($i in 1..$Snaps) {
    "--- snap $i" | Add-Content $out -Encoding ascii
    & python "$root\scripts\gstack.py" --depth 12 *>> $out
    if ($Mem.Count) { & python "$root\scripts\gmem.py" @Mem *>> $out }
}
$run.WaitForExit()
"done"
