# Two copies on one PC (docs/research/dos-instancias.md): scripts/launch-2inst.ps1 -NoLaunch against FAKE folders in
# %TEMP% (LOCALAPPDATA is redirected for this process and the script's child processes; -GameDir and -BackupDir point
# at %TEMP% too; nothing is launched). Checks: the backup (real folder and guest folder, checked); the guest slot
# copied once and never over the guest's own progress; the settings files copied when absent; the inis installed; the
# real save folder unchanged; and the refusals (two accounts without -Account, an interrupted save of the slot, a slot
# without its pair, a guest folder that is a link, a bad -GuestSlot, a missing ini, no cdmp.asi).
# Run by ctest (launch_2inst_script); exit 0 = all passed.

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$launcher = Join-Path $repo 'scripts\launch-2inst.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) "cdmp_launch2_test_$PID"
if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
$local = Join-Path $root 'Local'
$cd = Join-Path $local 'Pearl Abyss\CD'
$save = Join-Path $cd 'save'
$guest = Join-Path $cd 'save_cdmp_guest'
$game = Join-Path $root 'bin64'
$backups = Join-Path $root 'backups'
$acct = '1234567890'
$env:LOCALAPPDATA = $local
if (-not $save.StartsWith([IO.Path]::GetTempPath())) { throw "the fake save folder is not under %TEMP%: $save" }

$failures = 0
function Check([bool]$cond, [string]$what) {
    if (-not $cond) { Write-Host "FAIL: $what" -ForegroundColor Red; $script:failures++ }
}
function Write-Text([string]$path, [string]$text) {
    New-Item -ItemType Directory -Force -Path (Split-Path $path) | Out-Null
    [IO.File]::WriteAllText($path, $text)
}
function Sha([string]$p) { return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash }
function Snapshot([string]$dir) {
    if (-not (Test-Path -LiteralPath $dir)) { return '(none)' }
    $lines = foreach ($i in Get-ChildItem -LiteralPath $dir -Recurse -Force | Sort-Object FullName) {
        $rel = $i.FullName.Substring($dir.Length)
        if ($i.PSIsContainer) { "$rel <dir>" } else { "$rel $($i.Length) $(Sha $i.FullName) $($i.LastWriteTimeUtc.Ticks)" }
    }
    return ($lines -join "`n")
}
# The launcher in its own process: its exit code and its output.
function Run([string[]]$extra) {
    $args2 = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $launcher, '-NoLaunch', '-GameDir', $game,
        '-BackupDir', $backups, '-HostIni', $script:hostIni, '-GuestIni', $script:guestIni) + $extra
    $ErrorActionPreference = 'Continue'  # a native stderr line must not stop this test (PowerShell 5.1)
    $out = & powershell.exe @args2 2>&1 | Out-String
    return @{ Code = $LASTEXITCODE; Out = $out }
}

# The fake game folder and the real save folder (the host's slots, the guest's test character 108, settings).
Write-Text (Join-Path $game 'CrimsonDesert.exe') 'not a game'
Write-Text (Join-Path $game 'cdmp.asi') 'not a mod'
$script:hostIni = Join-Path $root 'host.ini'
$script:guestIni = Join-Path $root 'guest.ini'
Write-Text $hostIni "[net]`r`nrole=host`r`n"
Write-Text $guestIni "[net]`r`nrole=guest`r`n"
Write-Text (Join-Path $save "$acct\slot0\lobby.save") 'host lobby0'
Write-Text (Join-Path $save "$acct\slot0\save.save") 'host save0'
Write-Text (Join-Path $save "$acct\slot108\lobby.save") 'lobby108'
Write-Text (Join-Path $save "$acct\slot108\save.save") 'save108'
Write-Text (Join-Path $save "$acct\steam_autocloud.vdf") 'cloud'
Write-Text (Join-Path $save 'user_engine_option_save.xml') '<options/>'
Write-Text (Join-Path $save 'initialize_input.save') 'input'
$before = Snapshot $save

# ---- first run: everything prepared
$r = Run @()
Check ($r.Code -eq 0) "first run exit code $($r.Code): $($r.Out)"
Check ((Get-Content -LiteralPath (Join-Path $guest "$acct\slot108\save.save") -Raw) -eq 'save108') 'guest slot108 copied'
Check (-not (Test-Path -LiteralPath (Join-Path $guest "$acct\slot0"))) 'only the chosen slot is copied'
Check (-not (Test-Path -LiteralPath (Join-Path $guest "$acct\steam_autocloud.vdf"))) 'no cloud file in the guest folder'
Check ((Get-Content -LiteralPath (Join-Path $guest 'user_engine_option_save.xml') -Raw) -eq '<options/>') 'settings copied'
Check (Test-Path -LiteralPath (Join-Path $guest 'initialize_input.save')) 'input settings copied'
Check ((Get-Content -LiteralPath (Join-Path $game 'cdmp.host.ini') -Raw) -match 'role=host') 'cdmp.host.ini installed'
Check ((Get-Content -LiteralPath (Join-Path $game 'cdmp.guest.ini') -Raw) -match 'role=guest') 'cdmp.guest.ini installed'
$b1 = @(Get-ChildItem -LiteralPath $backups -Directory | Where-Object { $_.Name -like 'saves-2inst-*' })
Check ($b1.Count -eq 1) "one backup folder ($($b1.Count))"
if ($b1.Count -ge 1) { Check ((Snapshot (Join-Path $b1[0].FullName 'save')) -eq $before) 'backup equals the real folder' }
Check ((Snapshot $save) -eq $before) 'real folder unchanged after the first run'
Check ($r.Out -match 'nothing launched') 'NoLaunch says so'

# ---- second run (another second): the guest's own progress is kept, the guest folder is backed up too
Start-Sleep -Seconds 1
Write-Text (Join-Path $guest "$acct\slot108\save.save") 'guest progress'
Write-Text (Join-Path $guest 'user_engine_option_save.xml') '<windowed/>'
$r = Run @()
Check ($r.Code -eq 0) "second run exit code $($r.Code): $($r.Out)"
Check ((Get-Content -LiteralPath (Join-Path $guest "$acct\slot108\save.save") -Raw) -eq 'guest progress') 'guest progress kept'
Check ((Get-Content -LiteralPath (Join-Path $guest 'user_engine_option_save.xml') -Raw) -eq '<windowed/>') 'guest settings kept'
Check ($r.Out -match 'kept as it is') 'kept is said'
$b2 = @(Get-ChildItem -LiteralPath $backups -Directory | Where-Object { $_.Name -like 'saves-2inst-*' } | Sort-Object Name)
Check ($b2.Count -eq 2) "two backup folders ($($b2.Count))"
if ($b2.Count -eq 2) {
    Check ((Get-Content -LiteralPath (Join-Path $b2[1].FullName "save_cdmp_guest\$acct\slot108\save.save") -Raw) -eq 'guest progress') 'guest folder backed up'
}
Check ((Snapshot $save) -eq $before) 'real folder unchanged after the second run'

# ---- refusals (each in a fresh guest folder, so the copy step runs)
Remove-Item -LiteralPath $guest -Recurse -Force
Write-Text (Join-Path $save "$acct\slot108_new\save.save") 'interrupted'
$r = Run @()
Check ($r.Code -ne 0 -and $r.Out -match 'was interrupted') "interrupted save refused: $($r.Out)"
Remove-Item -LiteralPath (Join-Path $save "$acct\slot108_new") -Recurse -Force
$r = Run @('-GuestSlot', 'slot101')
Check ($r.Code -ne 0 -and $r.Out -match 'has no lobby.save') "slot without its pair refused: $($r.Out)"
$r = Run @('-GuestSlot', '108')
Check ($r.Code -ne 0 -and $r.Out -match 'must look like slot108') "bad slot name refused: $($r.Out)"
New-Item -ItemType Directory -Force -Path (Join-Path $save '2222') | Out-Null
$r = Run @()
Check ($r.Code -ne 0 -and $r.Out -match 'account folders') "two accounts refused: $($r.Out)"
$r = Run @('-Account', $acct)
Check ($r.Code -eq 0) "two accounts with -Account: $($r.Out)"
Remove-Item -LiteralPath (Join-Path $save '2222') -Force
Remove-Item -LiteralPath $guest -Recurse -Force
New-Item -ItemType Junction -Path $guest -Target $save | Out-Null
$r = Run @()
Check ($r.Code -ne 0 -and $r.Out -match 'is a link') "guest folder as a link refused: $($r.Out)"
(Get-Item -LiteralPath $guest -Force).Delete()  # the junction only, never its target
$keep = $script:hostIni
$script:hostIni = Join-Path $root 'missing.ini'
$r = Run @()
Check ($r.Code -ne 0 -and $r.Out -match 'ini not found') "missing ini refused: $($r.Out)"
$script:hostIni = $keep
Remove-Item -LiteralPath (Join-Path $game 'cdmp.asi')
$r = Run @()
Check ($r.Code -ne 0 -and $r.Out -match 'cdmp.asi is not installed') "no mod refused: $($r.Out)"
Check ((Snapshot $save) -eq $before) 'real folder unchanged at the end'

Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
if ($failures -eq 0) { Write-Host 'All launch-2inst tests passed'; exit 0 }
Write-Host "$failures failure(s)"
exit 1
