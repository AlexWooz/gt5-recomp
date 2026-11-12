# Gives GT5\spu (the compiled SPU code) this install's normal permissions again when it has a closed
# list. The SPU step used to make it with Python's tempfile.mkdtemp, which on Python 3.12.4 and newer
# allows only SYSTEM, Administrators and the folder's owner. Made from an Administrator window, a normal
# window then can't read it, and the build stops with "GetFileAttributesEx(...spu/sources.cmake): Access
# is denied". build_run.ps1 and relift.ps1 run this first; it does nothing when the folder is fine.
$spu = Join-Path (Split-Path $PSScriptRoot -Parent) "spu"
if (-not (Test-Path -LiteralPath $spu)) { exit 0 }

function Test-Readable {
    try {
        $acl = Get-Acl -LiteralPath $spu -ErrorAction Stop
        Get-ChildItem -LiteralPath $spu -Recurse -Force -ErrorAction Stop | Out-Null
        return $(if ($acl.AreAccessRulesProtected) { "closed" } else { "ok" })
    } catch { return "denied" }
}

$state = Test-Readable
if ($state -eq "ok") { exit 0 }
if ($state -eq "closed") {
    # This window owns it, so it may reset the list itself.
    icacls "$spu" /reset /t /c /q | Out-Null
    if ((Test-Readable) -eq "ok") { Write-Host "Fixed the permissions on GT5\spu."; exit 0 }
}
Write-Host "GT5\spu was made from an Administrator window, so this window can't read it." -ForegroundColor Yellow
Write-Host "Windows will ask to let icacls give it this install's normal permissions back..." -ForegroundColor Yellow
try {
    $p = Start-Process -FilePath "icacls.exe" -ArgumentList "`"$spu`" /reset /t /c /q" -Verb RunAs -Wait -PassThru -WindowStyle Hidden -ErrorAction Stop
} catch {
    $p = $null
}
if ((Test-Readable) -eq "ok") { Write-Host "Fixed the permissions on GT5\spu."; exit 0 }
Write-Host "GT5\spu still can't be read. Open PowerShell as administrator and run:" -ForegroundColor Red
Write-Host "  icacls `"$spu`" /reset /t" -ForegroundColor Red
Write-Host "then run this again." -ForegroundColor Red
exit 1
