# Muted movie run, then read guest memory from outside at set times: moviemem.ps1 -Tag mm -At 90,120 -Seconds 130 -Args2 @("x=*628C06C0","dump x,32")
param([string]$Tag = "mm", [int[]]$At = @(90), [int]$Seconds = 130, [string[]]$Args2 = @(), [string]$Trace = "cellDmuxSetStream")
$root = "$(Split-Path $PSScriptRoot -Parent)"
$env:HLE_NAME_TRACE = $Trace
$env:VDEC_DUMP = "$root\analysis\yuv_$Tag"
New-Item -ItemType Directory -Force $env:VDEC_DUMP | Out-Null
$a = "-NoProfile -ExecutionPolicy Bypass -File `"$root\scripts\racerun.ps1`" -Tag $Tag -Seconds $Seconds -EndAfter 900 -SkipMovie 9999"
$run = Start-Process powershell -WindowStyle Hidden -PassThru -ArgumentList $a
$t0 = Get-Date
$out = "$root\logs\$Tag.mem"
Remove-Item $out -ErrorAction SilentlyContinue
foreach ($t in $At) {
    while (((Get-Date) - $t0).TotalSeconds -lt $t) { Start-Sleep 1 }
    "--- t=$t" | Add-Content $out -Encoding ascii
    & python "$root\scripts\gmem.py" @Args2 *>> $out
}
$run.WaitForExit()
"done"
