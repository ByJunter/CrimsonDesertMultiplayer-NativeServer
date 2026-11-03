# Two copies of Crimson Desert with the mod on ONE PC: the host and the guest (docs/research/dos-instancias.md).
#
#   powershell -ExecutionPolicy Bypass -File .\scripts\launch-2inst.ps1                  # both copies, kit inis
#   powershell -ExecutionPolicy Bypass -File .\scripts\launch-2inst.ps1 -NoLaunch        # prepare only, print the plan
#   .\scripts\launch-2inst.ps1 -GuestSlot slot108 -HostIni <ini> -GuestIni <ini> -BackupDir 'C:\Emulador CD\backups'
#
# Steps (it stops at the first failure, before launching anything):
#  1. Refuses if any CrimsonDesert.exe is running, or (unless -NoLaunch) if Steam is not.
#  2. Backs up %LOCALAPPDATA%\Pearl Abyss\CD\save (and save_cdmp_guest, if any) to <BackupDir>\saves-2inst-<time>\
#     and checks the copy (same files, same sizes).
#  3. The guest's folder %LOCALAPPDATA%\Pearl Abyss\CD\save_cdmp_guest (the mod sends the guest-role game's whole
#     save\ there): copies the ONE slot -GuestSlot from save\<account>\ when the guest folder lacks it (never over an
#     existing one), and the game's settings files of save\ (user_engine_option_save.xml, initialize_input.save) when
#     absent, so the guest starts with the same settings and then keeps its own (windowed mode, resolution...).
#     Nothing is ever written to save\.
#  4. Copies -HostIni / -GuestIni to <GameDir>\cdmp.host.ini / cdmp.guest.ini.
#  5. Launches the host: CrimsonDesert.exe started DIRECTLY (not steam://) with SteamAppId / SteamGameId = 3321460,
#     the argument PlatformServiceType=Steam and CDMP_ROLE=host. A steam:// launch is started by Steam, so it would not
#     inherit CDMP_ROLE; launched like this the game does not relaunch itself through Steam (the way the second copy
#     was verified to run on 14-10; Steam must be open).
#  6. Waits until the host's mod log (cdmp.host.log, this PID) says the game is up (its title screen), then launches
#     the guest the same way with CDMP_ROLE=guest.
#  7. Prints both PIDs and the log files to read.
#
# -NoLaunch: steps 1-4 only (no Steam needed). -GameDir / -BackupDir / %LOCALAPPDATA% are parameters so that
# tests\launch_2inst_tests.ps1 runs it against folders in %TEMP%.

param(
    [string]$GameDir = $(if ($env:CDMP_GAME_DIR) { $env:CDMP_GAME_DIR } else { 'D:\SteamLibrary\steamapps\common\Crimson Desert\bin64' }),
    [string]$BackupDir = 'C:\Emulador CD\backups',
    [string]$GuestSlot = 'slot108',
    [string]$Account = '',
    [string]$HostIni = $(Join-Path (Split-Path $PSScriptRoot -Parent) 'docs\research\ini\cdmp.2inst-host.ini'),
    [string]$GuestIni = $(Join-Path (Split-Path $PSScriptRoot -Parent) 'docs\research\ini\cdmp.2inst-guest.ini'),
    [int]$HostWaitSec = 300,
    [int]$GuestDelaySec = 5,
    [switch]$NoLaunch
)
$ErrorActionPreference = 'Stop'
$kAppId = '3321460'

function Refuse([string]$why) {
    Write-Host "REFUSED: $why" -ForegroundColor Red
    exit 1
}

# Files and total bytes under $dir (for the backup's check).
function Get-TreeSummary([string]$dir) {
    $files = @(Get-ChildItem -LiteralPath $dir -Recurse -Force -File)
    $bytes = 0
    foreach ($f in $files) { $bytes += $f.Length }
    return @{ Count = $files.Count; Bytes = $bytes }
}

# ---------------------------------------------------------------- 1. checks
$exe = Join-Path $GameDir 'CrimsonDesert.exe'
if (-not (Test-Path -LiteralPath $exe)) { Refuse "CrimsonDesert.exe not found in $GameDir" }
if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'cdmp.asi'))) { Refuse "cdmp.asi is not installed in $GameDir (scripts\deploy.ps1)" }
$running = @(Get-Process -Name CrimsonDesert -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    Refuse ("CrimsonDesert is already running (PID {0}); close every copy first (Stop-Process at the title screen)" -f
        (($running | ForEach-Object { $_.Id }) -join ', '))
}
if (-not $NoLaunch -and -not (Get-Process -Name steam -ErrorAction SilentlyContinue)) {
    Refuse 'Steam is not running: open Steam first (both copies need it)'
}
foreach ($ini in $HostIni, $GuestIni) {
    if (-not (Test-Path -LiteralPath $ini)) { Refuse "ini not found: $ini" }
}
if ($GuestSlot -notmatch '^slot[0-9]+$') { Refuse "-GuestSlot must look like slot108 (got '$GuestSlot')" }
if (-not $env:LOCALAPPDATA) { Refuse '%LOCALAPPDATA% is not set' }
$cd = Join-Path $env:LOCALAPPDATA 'Pearl Abyss\CD'
$save = Join-Path $cd 'save'
$guestSave = Join-Path $cd 'save_cdmp_guest'
if (-not (Test-Path -LiteralPath $save)) { Refuse "the save folder $save does not exist" }
if ((Test-Path -LiteralPath $guestSave) -and ((Get-Item -LiteralPath $guestSave -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    Refuse "$guestSave is a link; the mod refuses it too (it could lead into the real save folder)"
}

# The account folder: -Account, or the only numeric folder of save\.
if ($Account) {
    $accountDir = Join-Path $save $Account
    if (-not (Test-Path -LiteralPath $accountDir)) { Refuse "no account folder $accountDir" }
} else {
    $accounts = @(Get-ChildItem -LiteralPath $save -Directory | Where-Object { $_.Name -match '^[0-9]+$' })
    if ($accounts.Count -ne 1) { Refuse "there are $($accounts.Count) account folders in $save; choose one with -Account <id>" }
    $Account = $accounts[0].Name
    $accountDir = $accounts[0].FullName
}

# ---------------------------------------------------------------- 2. backup
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $BackupDir "saves-2inst-$stamp"
for ($n = 2; Test-Path -LiteralPath $backup; $n++) { $backup = Join-Path $BackupDir "saves-2inst-${stamp}_$n" }
New-Item -ItemType Directory -Force -Path $backup | Out-Null
foreach ($pair in @(@($save, 'save'), @($guestSave, 'save_cdmp_guest'))) {
    if (-not (Test-Path -LiteralPath $pair[0])) { continue }
    $dst = Join-Path $backup $pair[1]
    Copy-Item -LiteralPath $pair[0] -Destination $dst -Recurse -Force
    $a = Get-TreeSummary $pair[0]
    $b = Get-TreeSummary $dst
    if ($a.Count -ne $b.Count -or $a.Bytes -ne $b.Bytes) {
        Refuse ("the backup of {0} does not match ({1} files / {2} bytes vs {3} / {4})" -f $pair[0], $a.Count, $a.Bytes, $b.Count, $b.Bytes)
    }
    Write-Host ("Backup: {0} -> {1} ({2} files, {3} bytes, checked)" -f $pair[0], $dst, $a.Count, $a.Bytes)
}

# ---------------------------------------------------------------- 3. the guest's folder
$srcSlot = Join-Path $accountDir $GuestSlot
$dstAccount = Join-Path $guestSave $Account
$dstSlot = Join-Path $dstAccount $GuestSlot
if (Test-Path -LiteralPath $dstSlot) {
    Write-Host "Guest save: $dstSlot already exists: kept as it is (the guest's own progress)"
} else {
    foreach ($leftover in @(Get-ChildItem -LiteralPath $accountDir -Directory | Where-Object { $_.Name -like "${GuestSlot}_*" })) {
        Refuse "$($leftover.FullName) exists: a save of $GuestSlot was interrupted; load it once alone first"
    }
    foreach ($name in 'lobby.save', 'save.save') {
        if (-not (Test-Path -LiteralPath (Join-Path $srcSlot $name))) { Refuse "$srcSlot has no $name" }
    }
    New-Item -ItemType Directory -Force -Path $dstAccount | Out-Null
    Copy-Item -LiteralPath $srcSlot -Destination $dstSlot -Recurse
    foreach ($f in Get-ChildItem -LiteralPath $srcSlot -File) {
        $c = Join-Path $dstSlot $f.Name
        if ((Get-FileHash -LiteralPath $f.FullName).Hash -ne (Get-FileHash -LiteralPath $c).Hash) { Refuse "the copy of $($f.FullName) does not match" }
    }
    Write-Host "Guest save: $srcSlot -> $dstSlot (copied, checked)"
}
foreach ($name in 'user_engine_option_save.xml', 'initialize_input.save') {
    $src = Join-Path $save $name
    $dst = Join-Path $guestSave $name
    if ((Test-Path -LiteralPath $src) -and -not (Test-Path -LiteralPath $dst)) {
        Copy-Item -LiteralPath $src -Destination $dst
        Write-Host "Guest settings: $name copied (from now on the guest's own)"
    }
}

# ---------------------------------------------------------------- 4. inis
Copy-Item -LiteralPath $HostIni -Destination (Join-Path $GameDir 'cdmp.host.ini') -Force
Copy-Item -LiteralPath $GuestIni -Destination (Join-Path $GameDir 'cdmp.guest.ini') -Force
Write-Host "Inis: $HostIni -> cdmp.host.ini; $GuestIni -> cdmp.guest.ini (in $GameDir)"

if ($NoLaunch) {
    Write-Host '-NoLaunch: prepared; nothing launched.'
    exit 0
}

# ---------------------------------------------------------------- 5-7. launch
function Start-Copy([string]$role) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.Arguments = 'PlatformServiceType=Steam'
    $psi.WorkingDirectory = $GameDir
    $psi.UseShellExecute = $false
    $psi.EnvironmentVariables['SteamAppId'] = $kAppId
    $psi.EnvironmentVariables['SteamGameId'] = $kAppId
    $psi.EnvironmentVariables['CDMP_ROLE'] = $role
    return [System.Diagnostics.Process]::Start($psi)
}

# The alias cdmp.<role>.log may still be an older run's: only this PID's header counts.
function Read-Log([string]$path) {
    try {
        $fs = New-Object IO.FileStream($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]'ReadWrite, Delete')
        try { return (New-Object IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Dispose() }
    } catch { return '' }
}

$hostLog = Join-Path $GameDir 'cdmp.host.log'
$guestLog = Join-Path $GameDir 'cdmp.guest.log'
$hostProc = Start-Copy 'host'
Write-Host "Host: PID $($hostProc.Id) (CDMP_ROLE=host); waiting for its title screen (at most $HostWaitSec s)..."
$deadline = (Get-Date).AddSeconds($HostWaitSec)
$up = $false
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 2
    if ($hostProc.HasExited) {
        $why = ("the host's process {0} ended (exit code {1}) before its title screen. If a CrimsonDesert started by " +
            "Steam is running now, the direct launch relaunched through Steam (no CDMP_ROLE there): close it and see " +
            "docs/research/dos-instancias.md")
        Refuse ($why -f $hostProc.Id, $hostProc.ExitCode)
    }
    $text = Read-Log $hostLog
    if ($text -match "\(PID $($hostProc.Id)\)" -and $text -match 'Late init: game is up') { $up = $true; break }
}
if (-not $up) { Refuse "the host's log $hostLog did not show 'Late init: game is up' for PID $($hostProc.Id) in $HostWaitSec s (the host keeps running)" }
Start-Sleep -Seconds $GuestDelaySec
$guestProc = Start-Copy 'guest'
Write-Host ''
Write-Host "Host:  PID $($hostProc.Id)  log $hostLog (alias of cdmp.host_<time>.log)  ini cdmp.host.ini"
Write-Host "Guest: PID $($guestProc.Id)  log $guestLog (alias of cdmp.guest_<time>.log)  ini cdmp.guest.ini"
Write-Host "Captures: cdmp_packets.host_<time>.* / cdmp_packets.guest_<time>.*; the game's short-lived second process, if any: cdmp.<role>.extra.log"
Write-Host "Guest saves: $guestSave  |  backup: $backup"
Write-Host 'Order: load the host''s save first; then Continue on the guest (its slot is the copied one).'
