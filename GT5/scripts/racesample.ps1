# Start a race test, wait for the race, then sample guest threads: racesample.ps1 -Tag x -Tids 18,28 [-Wait 15]
param([string]$Tag = "rs", [string]$Tids = "18,28", [int]$Wait = 15, [int]$N = 1500)
$root = "$(Split-Path $PSScriptRoot -Parent)"
Remove-Item "$root\logs\$Tag.drive" -ErrorAction SilentlyContinue
Start-Process powershell -WindowStyle Hidden -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$root\scripts\racerun.ps1`" -Tag $Tag -Seconds 500 -EndAfter 120" | Out-Null
for ($i = 0; $i -lt 90; $i++) {
    Start-Sleep 5
    $d = Get-Content "$root\logs\$Tag.drive" -ErrorAction SilentlyContinue
    if ($d -match "accelerate") { break }
}
Start-Sleep $Wait
python "$root\scripts\gsample.py" --tid $Tids --n $N --ms 3 --depth 6 *> "$root\logs\$Tag.sample"
"done after $($i * 5) s"
