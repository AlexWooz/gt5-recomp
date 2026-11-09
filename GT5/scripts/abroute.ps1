# Same route under several settings, muted and behind other windows: abroute.ps1 -Prefix ab -Seconds 300 -Configs @(@{A="1"}, @{B="1"})
# Prints when each run reached the race, then frame rate against draw count (fpscmp.py) for all of them.
param([string]$Prefix = "ab", [int]$Seconds = 300, [object[]]$Configs = @(@{}))
$root = "$(Split-Path $PSScriptRoot -Parent)"
$tags = @()
for ($i = 0; $i -lt $Configs.Count; $i++) {
    $tag = "$Prefix$i"
    $tags += $tag
    Remove-Item "$root\logs\$tag.drive" -ErrorAction SilentlyContinue
    & "$root\scripts\racerun.ps1" -Tag $tag -Seconds $Seconds -EndAfter 40 -Env $Configs[$i] | Out-Null
    $d = Get-Content "$root\logs\$tag.drive" -ErrorAction SilentlyContinue
    $cfg = ($Configs[$i].GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join " "
    "$tag [$cfg]: $(if ($d) { $d[-1] } else { 'no route log' })"
}
& python "$root\scripts\fpscmp.py" @tags
