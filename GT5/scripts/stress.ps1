# Run the route to a race several times and report which runs got there.
#   stress.ps1 -Prefix s -Count 5 [-Seconds 260]
# A run that hasn't reached the race by -Seconds counts as stuck; its logs stay as logs\<Prefix><n>.*
param([string]$Prefix = "s", [int]$Count = 5, [int]$Seconds = 260)
$root = "$(Split-Path $PSScriptRoot -Parent)"
$res = "$root\logs\stress_$Prefix.txt"
Set-Content $res ""
for ($i = 1; $i -le $Count; $i++) {
    $tag = "$Prefix$i"
    & "$root\scripts\racerun.ps1" -Tag $tag -Seconds $Seconds -EndAfter 15 -Env @{ WATCHDOG_AT = "100,200" } | Out-Null
    $drive = Get-Content "$root\logs\$tag.drive" -ErrorAction SilentlyContinue
    $ok = ($drive | Select-String "done, stopping") -ne $null
    $last = if ($drive) { $drive[-1] } else { "" }
    Add-Content $res "$tag $(if ($ok) { 'ok' } else { 'STUCK' }) last: $last"
}
Get-Content $res
