# RPT-0, containment K2 (docs/research/opcion-d-rpt0.md §4): moves the game's
# crashpad_handler.exe out of bin64 while option D's block B tests run, and
# puts it back afterwards. Without it sentry_init fails and the game sends no
# error report, crash dump or session (the game checks the file exists and
# goes on without Sentry: 0x131602D, 0x131622E -> 0x1317B37).
#
#   .\scripts\crashpad.ps1            # status
#   .\scripts\crashpad.ps1 -Off       # move it to the backup folder
#   .\scripts\crashpad.ps1 -On        # put it back (checks the SHA-256)
#
# Steam's "verify integrity of game files" also puts it back.

param(
    [string]$GameDir = $(if ($env:CDMP_GAME_DIR) { $env:CDMP_GAME_DIR } else { 'D:\SteamLibrary\steamapps\common\Crimson Desert\bin64' }),
    [string]$BackupDir = 'C:\Emulador CD\backups\crashpad',
    [switch]$Off,
    [switch]$On
)
$ErrorActionPreference = 'Stop'
if ($Off -and $On) { throw 'Use -Off or -On, not both.' }
$name = 'crashpad_handler.exe'
$live = Join-Path $GameDir $name
$kept = Join-Path $BackupDir $name
$hashFile = Join-Path $BackupDir "$name.sha256"
$cache = Join-Path $env:LOCALAPPDATA 'Pearl Abyss\DumpCache'

function Show-Cache {
    if (-not (Test-Path $cache)) { Write-Host "DumpCache: absent"; return }
    $reports = @(Get-ChildItem (Join-Path $cache 'reports') -File -ErrorAction SilentlyContinue)
    $attach = @(Get-ChildItem (Join-Path $cache 'attachments') -Recurse -File -ErrorAction SilentlyContinue)
    $env = @(Get-ChildItem $cache -Recurse -Filter '*.envelope' -File -ErrorAction SilentlyContinue)
    $state = if ($reports.Count + $attach.Count + $env.Count) { 'PENDING REPORTS: do not turn crashpad back on before looking at them' } else { 'nothing pending' }
    Write-Host ("DumpCache: {0} crash reports, {1} attachments, {2} envelopes ({3})" -f $reports.Count, $attach.Count, $env.Count, $state)
}

if ((Get-Process CrimsonDesert, crashpad_handler -ErrorAction SilentlyContinue) -and ($Off -or $On)) {
    throw 'The game (or its crashpad_handler) is running; close it first.'
}

if ($Off) {
    if (-not (Test-Path $live)) {
        if (Test-Path $kept) { Write-Host "Already off: $name is in $BackupDir" } else { throw "$name is neither in the game folder nor in $BackupDir" }
    } else {
        New-Item -ItemType Directory -Force $BackupDir | Out-Null
        $hash = (Get-FileHash $live -Algorithm SHA256).Hash
        if (Test-Path $kept) {
            if ((Get-FileHash $kept -Algorithm SHA256).Hash -ne $hash) { throw "$kept exists and differs from the game's copy; look at it first" }
            Remove-Item $live
        } else {
            Move-Item $live $kept
        }
        Set-Content -Encoding ascii $hashFile $hash
        Write-Host "OFF: $name moved to $BackupDir (SHA-256 $hash). The game sends no error reports now."
    }
} elseif ($On) {
    if (Test-Path $live) {
        Write-Host "Already on: $name is in the game folder"
    } else {
        if (-not (Test-Path $kept)) { throw "$kept not found; use Steam's 'verify integrity of game files'" }
        $hash = (Get-FileHash $kept -Algorithm SHA256).Hash
        if ((Test-Path $hashFile) -and ((Get-Content $hashFile).Trim() -ne $hash)) { throw "$kept does not match the hash saved when it was moved" }
        Copy-Item $kept $live
        Write-Host "ON: $name is back in the game folder (SHA-256 $hash)"
    }
} else {
    $where = if (Test-Path $live) { 'ON (in the game folder: reports can be sent)' } elseif (Test-Path $kept) { "OFF (kept in $BackupDir)" } else { 'MISSING (neither in the game folder nor in the backup)' }
    Write-Host "crashpad_handler.exe: $where"
}
Show-Cache
