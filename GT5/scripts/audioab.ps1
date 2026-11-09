# Short runs comparing audio settings: audioab.ps1 -Prefix ab -Seconds 50 -Count 3 -Env @{AUDIO_EXTRA_LEAD="0"}
# Prints the silent % of each 5 s of mixed audio. Runs stay muted and behind other windows.
param([string]$Prefix = "ab", [int]$Seconds = 50, [int]$Count = 3, [hashtable]$Env = @{})
$root = "$(Split-Path $PSScriptRoot -Parent)"
$out = "$root\logs\audioab_$Prefix.txt"
Set-Content $out "" -Encoding ascii
for ($i = 1; $i -le $Count; $i++) {
    $tag = "$Prefix$i"
    $e = @{ AUDIO_RATE = "1" }
    foreach ($k in $Env.Keys) { $e[$k] = $Env[$k] }
    & "$root\scripts\racerun.ps1" -Tag $tag -Seconds $Seconds -EndAfter 600 -Env $e *> "$root\logs\$tag.run"
    $s = Select-String -Path "$root\logs\$tag.err" -Pattern "silent ([0-9.]+)%" | ForEach-Object { $_.Matches[0].Groups[1].Value }
    "$tag : $($s -join ' ')" | Add-Content $out -Encoding ascii
    $last = Get-Content "$root\logs\$tag.drive" -Tail 1 -ErrorAction SilentlyContinue
    "   route: $last" | Add-Content $out -Encoding ascii
}
"finished" | Add-Content $out -Encoding ascii
