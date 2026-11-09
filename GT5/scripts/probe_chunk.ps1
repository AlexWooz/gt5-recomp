# Compile the largest lifted chunk alone and report its time and peak memory, to size -j.
$root = "$(Split-Path $PSScriptRoot -Parent)"
$big = Get-ChildItem "$root\generated\ppu_recomp_0*.cpp" | Sort-Object Length -Descending | Select-Object -First 1
$mb = [math]::Round($big.Length / 1MB, 1)
"largest chunk: $($big.Name) $mb MB"
$obj = "CMakeFiles/gt5.dir/generated/$($big.Name).obj"
$t0 = Get-Date
$p = Start-Process "$root\scripts\build.cmd" -ArgumentList $obj -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput "$root\analysis\probe.log" -RedirectStandardError "$root\analysis\probe.err"
$h = $p.Handle  # keeps ExitCode readable after exit
$peak = 0
while (-not $p.HasExited) {
    Get-Process clang-cl, clang -ErrorAction SilentlyContinue | ForEach-Object {
        try { if ($_.PeakWorkingSet64 -gt $peak) { $peak = $_.PeakWorkingSet64 } } catch {}
    }
    Start-Sleep -Milliseconds 500
}
"exit $($p.ExitCode), $([int]((Get-Date) - $t0).TotalSeconds) s, peak $([math]::Round($peak / 1GB, 2)) GB"
$o = Get-Item "$root\build\$obj" -ErrorAction SilentlyContinue
if ($o) { "object $([math]::Round($o.Length / 1MB, 1)) MB" }
Get-Content "$root\analysis\probe.log" -Tail 12
Get-Content "$root\analysis\probe.err" -Tail 12
