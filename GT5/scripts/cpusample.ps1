# Per-thread CPU over a window of a running gt5.exe: cpusample.ps1 -At 50 -Window 5 -Log <run .err file>
# Prints each busy host thread's share of one core, with its guest thread id where the log names it.
param([int]$At = 50, [int]$Window = 5, [string]$Log = "")
$gt = $null
for ($i = 0; $i -lt 40 -and -not $gt; $i++) { $gt = Get-Process gt5 -ErrorAction SilentlyContinue | Select-Object -First 1; if (-not $gt) { Start-Sleep -Milliseconds 500 } }
if (-not $gt) { "gt5 not running"; exit }
while (((Get-Date) - $gt.StartTime).TotalSeconds -lt $At) { Start-Sleep -Milliseconds 500 }
$gt.Refresh()
$a = @{}; foreach ($t in $gt.Threads) { try { $a[$t.Id] = $t.TotalProcessorTime.TotalSeconds } catch {} }
Start-Sleep -Seconds $Window
$gt.Refresh()
$map = @{}
if ($Log -and (Test-Path $Log)) {
    Select-String -Path $Log -Pattern "\[THREAD (\d+)\] host thread started.*hosttid=(\d+)" | ForEach-Object { $map[[int]$_.Matches[0].Groups[2].Value] = $_.Matches[0].Groups[1].Value }
}
$rows = foreach ($t in $gt.Threads) {
    try { $d = $t.TotalProcessorTime.TotalSeconds - [double]$a[$t.Id] } catch { continue }
    if ($d -gt 0.05) { [pscustomobject]@{ Host = $t.Id; Guest = $map[$t.Id]; Pct = [math]::Round(100 * $d / $Window) } }
}
$total = ($rows | Measure-Object Pct -Sum).Sum
"total $total% of one core across $(($rows | Measure-Object).Count) busy threads"
$rows | Sort-Object Pct -Descending | Select-Object -First 16 | ForEach-Object { "{0,4}%  host {1,6}  guest {2}" -f $_.Pct, $_.Host, $_.Guest }
