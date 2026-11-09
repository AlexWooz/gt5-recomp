# Frame rate on the first dialog with no dumps or input, under several settings, muted and behind other windows:
#   idlefps.ps1 -Seconds 40 -Configs @(@{}, @{ PS3_THROTTLE = "4" })
param([int]$Seconds = 40, [object[]]$Configs = @(@{}))
$root = "$(Split-Path $PSScriptRoot -Parent)"
for ($i = 0; $i -lt $Configs.Count; $i++) {
    $tag = "idle$i"
    $cfg = ($Configs[$i].GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join " "
    $e = @{ PS3_VERBOSE = "0"; LD_GPU_WORK = "1"; GCM_DRAIN_STAT = "1" }
    foreach ($k in $Configs[$i].Keys) { $e[$k] = $Configs[$i][$k] }
    & "$root\scripts\run.ps1" -Seconds $Seconds -Tag $tag -Env $e | Out-Null
    "== [$cfg]"
    Select-String -Path "$root\logs\$tag.err" -Pattern "\[fps\]" | Select-Object -Last 3 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(50, $_.Line.Length)) }
    Select-String -Path "$root\logs\$tag.err" -Pattern "\[drain\]" | Select-Object -Last 1 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(100, $_.Line.Length)) }
    Select-String -Path "$root\logs\$tag.err" -Pattern "\[present-parts\]" | Select-Object -Last 1 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(90, $_.Line.Length)) }
}
