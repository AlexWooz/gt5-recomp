# Drive a fresh gt5.exe through PAD_FILE, with generous waits, to Arcade's pre-race screen
# and, with -Race, on into the race with the accelerator held:
#   autodrive.ps1 [-Scale 1.0] [-Race]   Start run.ps1 with PAD_FILE=logs\pad.txt first.
param([double]$Scale = 1.0, [switch]$Race)
$root = "$(Split-Path $PSScriptRoot -Parent)"
$steps = @(
    @(28, "0x4000", 6),   # first dialog: OK
    @(18, "0x0020", 3),   # top menu: Right to Arcade
    @(6,  "0x4000", 4),   # Arcade
    @(20, "0x4000", 4),   # Single Race
    @(8,  "0x4000", 4),   # difficulty
    @(15, "0x4000", 4),   # track
    @(35, "0x4000", 4),   # car
    @(50, "0x4000", 4),   # car again, the showroom ignores the first while it loads
    @(55, "0x4000", 3)
)
if ($Race) {
    $steps += ,@(135, "0x4000", 6)   # pre-race: start flag
    $steps += ,@(30, "0x4000", 6)    # in case the first came early
    $steps += ,@(60, "0x4000", 4000) # accelerator
}
foreach ($s in $steps) {
    Start-Sleep -Seconds ([int]($s[0] * $Scale))
    Set-Content -Path "$env:LOCALAPPDATA\Temp\gt5_pad.txt" -Value "$($s[1]) $($s[2])" -Encoding ascii -NoNewline
    "$(Get-Date -Format HH:mm:ss) pressed $($s[1])"
}
