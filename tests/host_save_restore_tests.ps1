# P8 S35 phase E, review M3: scripts/host_save_restore.ps1 against a FAKE save folder in %TEMP% (LOCALAPPDATA is
# redirected for this process and the script's child processes; the real save folder is never named). Checks: a dry
# run changes nothing; the refusals (slots 3 / 204 / 205 / 109, the game's _new / _backup leftovers, a tampered
# generation file, a tampered or incomplete manifest, a probe not-candidate, an unverified slot without
# -AllowUnverified, the game running - simulated by a harmless process named CrimsonDesert); a restore that verifies;
# and the put-back after a failure (an existing slot, and a new slot whose folder is removed again).
# Run by ctest (host_save_restore_script); exit 0 = all passed.

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$restore = Join-Path $repo 'scripts\host_save_restore.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) "cdmp_restore_test_$PID"
if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
$local = Join-Path $root 'Local'
$save = Join-Path $local 'Pearl Abyss\CD\save'
$exports = Join-Path $root 'exports'
$backups = Join-Path $root 'backups'
$acct = '1234567890'
$env:LOCALAPPDATA = $local
$env:CDMP_RESTORE_TEST_FAIL = ''
if (-not $save.StartsWith([IO.Path]::GetTempPath())) { throw "the fake save folder is not under %TEMP%: $save" }

$failures = 0
function Check([bool]$cond, [string]$what) {
    if (-not $cond) { Write-Host "FAIL: $what" -ForegroundColor Red; $script:failures++ }
}
function Sha([string]$p) { return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() }

# A file the game's header check accepts: "SAVE", version 1, header 0x80, raw = 3 x data, 0x80 + data bytes.
function New-SaveBytes([int]$data, [byte]$fill) {
    $b = New-Object byte[] (0x80 + $data)
    for ($i = 0x80; $i -lt $b.Length; $i++) { $b[$i] = $fill }
    [Text.Encoding]::ASCII.GetBytes('SAVE').CopyTo($b, 0)
    $b[4] = 1; $b[6] = 0x80
    [BitConverter]::GetBytes([uint32](3 * $data)).CopyTo($b, 0x12)
    [BitConverter]::GetBytes([uint32]$data).CopyTo($b, 0x16)
    return , $b
}
function Write-Bytes([string]$path, [byte[]]$bytes) {
    New-Item -ItemType Directory -Force -Path (Split-Path $path) | Out-Null
    [IO.File]::WriteAllBytes($path, $bytes)
}
function Write-Pair([string]$dir, [byte]$fill) {
    Write-Bytes (Join-Path $dir 'lobby.save') (New-SaveBytes 0x186 $fill)
    Write-Bytes (Join-Path $dir 'save.save') (New-SaveBytes 0x2000 ([byte]($fill + 1)))
}
# Names, sizes and hashes (and, with -Full, attributes and write times) of everything under $dir.
function Snapshot([string]$dir, [switch]$Full) {
    if (-not (Test-Path -LiteralPath $dir)) { return '(none)' }
    $lines = foreach ($i in Get-ChildItem -LiteralPath $dir -Recurse -Force | Sort-Object FullName) {
        $rel = $i.FullName.Substring($dir.Length)
        $extra = if ($Full) { " $($i.Attributes) $($i.LastWriteTimeUtc.Ticks)" } else { '' }
        # A folder's write time is not compared: NTFS stamps it late (p8-guardado-desviado.md, revision of S24).
        if ($i.PSIsContainer) { "$rel <dir> $($i.Attributes)" } else { "$rel $($i.Length) $(Sha $i.FullName)$extra" }
    }
    return ($lines -join "`n")
}

# A generation as the mod writes it (host_save_logic.h manifest_text); $probes: folder -> "verdict|reasons".
function Write-Generation([string]$name, [string[]]$slots, [hashtable]$probes, [int]$seed) {
    $g = Join-Path $exports $name
    $lines = @('cdmp host save generation (P8 S35 phase E)', 'format=1', 'game_build=1.0.0.2976',
        'pe_timestamp=0x6AB28F00', 'mod_build=test', "generation=$name", 'created=2026-10-14 19:02:32.125',
        'created_utc=2026-10-14T17:02:32Z', 'seq=1', 'run=test', 'run_start=2026-10-14 19:02:30.000', 'mode=probe',
        'source=sandbox|C:\fake|unknown||', "account=sandbox|$acct")
    $k = 0
    foreach ($s in $slots) {
        $dir = Join-Path $g "sandbox\$acct\$s"
        Write-Pair $dir ([byte]($seed + 2 * $k)); $k++
        foreach ($n in 'lobby.save', 'save.save') {
            $p = Join-Path $dir $n
            $lines += "file=sandbox|$acct\$s\$n|$((Get-Item $p).Length)|$(Sha $p)|$((Get-Item $p).LastWriteTimeUtc.ToFileTimeUtc())"
        }
        $lines += "slot=sandbox|$acct|$s|host"
    }
    foreach ($key in $probes.Keys) { $lines += "probe=sandbox|$acct|$key|$($probes[$key])" }
    $lines += 'state=complete'
    [IO.File]::WriteAllText((Join-Path $g 'manifest.txt'), (($lines -join "`n") + "`n"))
    return $g
}

function Run([string[]]$a, [switch]$NoSteamFlag) {
    $all = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $restore) + $a + @('-ExportDir', $exports, '-BackupDir', $backups)
    if (-not $NoSteamFlag) { $all += '-SteamRunningOk' }
    $out = & powershell.exe @all 2>&1 | Out-String
    $script:lastOut = $out
    return $LASTEXITCODE
}

# ---------------------------------------------------------------- the fake real folder and two generations
Write-Pair (Join-Path $save "$acct\slot0") 0x10
Write-Pair (Join-Path $save "$acct\slot1") 0x20
[IO.File]::WriteAllText((Join-Path $save "$acct\steam_autocloud.vdf"), 'vdf')
$g1 = Write-Generation '20261014-190232' @('slot0', 'slot1', 'slot2', 'slot3', 'slot204', 'slot205', 'slot109') `
    @{ 'slot0' = 'candidate|1 host save(s) ...'; 'slot1' = 'unknown|no journal'; 'slot2' = 'not-candidate|user 2 wrote it' } 0x40
$g2 = Write-Generation '20261014-200000' @('slot100') @{} 0x70

$before = Snapshot $save -Full

# ---------------------------------------------------------------- dry run
Check ((Run @('-Generation', '20261014-190232', '-Slot', '0')) -eq 0) "dry run exits 0: $lastOut"
Check ($lastOut -match 'Dry run: nothing was changed') 'dry run says so'
Check ((Snapshot $save -Full) -eq $before) 'dry run: the real folder unchanged'
Check (-not (Test-Path -LiteralPath $backups)) 'dry run: no backup made'
Check ((Run @('-Generation', '20261014-190232', '-Slot', '1')) -eq 0) 'dry run of an unknown slot only warns'

# ---------------------------------------------------------------- refusals: exit 1, nothing changed
function Refused([string[]]$a, [string]$pattern, [string]$what) {
    $code = Run $a
    Check ($code -eq 1) "$what refused (exit $code): $lastOut"
    Check ($lastOut -match $pattern) "${what}: reason '$pattern' in: $lastOut"
    Check ((Snapshot $save -Full) -eq $before) "${what}: the real folder unchanged"
}
Refused @('-Generation', '20261014-190232', '-Slot', '3', '-Confirm') 'CrossSave' 'slot 3'
Refused @('-Generation', '20261014-190232', '-Slot', '204', '-Confirm') 'guest pool' 'slot 204'
Refused @('-Generation', '20261014-190232', '-Slot', '205', '-Confirm') 'guest pool' 'slot 205'
Refused @('-Generation', '20261014-190232', '-Slot', '109', '-Confirm') 'outside the whitelist' 'slot 109'
Refused @('-Generation', '20261014-190232', '-Slot', '2', '-Confirm', '-AllowUnverified') 'not-candidate' 'probe not-candidate'
Refused @('-Generation', '20261014-190232', '-Slot', '1', '-Confirm') 'AllowUnverified' 'unknown slot without -AllowUnverified'
Refused @('-Generation', '20261014-200000', '-Slot', '100', '-Confirm') 'AllowUnverified' 'no probe verdict without -AllowUnverified'
Refused @('-Generation', '20261099-000000', '-Slot', '0') 'no manifest' 'a missing generation'
foreach ($left in 'slot0_new', 'slot0_backup', 'slot0_backup_213942844459_delete') {
    $p = Join-Path $save "$acct\$left"
    New-Item -ItemType Directory -Path $p | Out-Null
    $code = Run @('-Generation', '20261014-190232', '-Slot', '0', '-Confirm')
    Check ($code -eq 1 -and $lastOut -match 'leftovers') "leftover $left refused: $lastOut"
    Remove-Item -LiteralPath $p -Force
    Check ((Snapshot $save -Full) -eq $before) "leftover ${left}: the real folder unchanged"
}
# A generation file changed after the export.
$victim = Join-Path $g1 "sandbox\$acct\slot1\save.save"
$orig = [IO.File]::ReadAllBytes($victim)
$bad = [byte[]]$orig.Clone(); $bad[0x90] = $bad[0x90] -bxor 1
[IO.File]::WriteAllBytes($victim, $bad)
Refused @('-Generation', '20261014-190232', '-Slot', '0', '-Confirm') 'SHA-256 differs' 'a tampered generation file'
[IO.File]::WriteAllBytes($victim, $orig)
# The manifest changed: a size, the final state line, another build.
$mpath = Join-Path $g1 'manifest.txt'
$mtext = [IO.File]::ReadAllText($mpath)
$fileLine = @($mtext -split "`n" | Where-Object { $_ -like "file=sandbox|$acct\slot0\save.save|*" })[0]
$parts = $fileLine.Split('|'); $parts[2] = [string]([int64]$parts[2] + 1)
[IO.File]::WriteAllText($mpath, $mtext.Replace($fileLine, ($parts -join '|')))
Refused @('-Generation', '20261014-190232', '-Slot', '0', '-Confirm') 'size differs' 'a tampered manifest size'
[IO.File]::WriteAllText($mpath, $mtext.Replace("state=complete`n", ''))
Refused @('-Generation', '20261014-190232', '-Slot', '0', '-Confirm') 'not complete' 'an incomplete manifest'
[IO.File]::WriteAllText($mpath, $mtext.Replace('game_build=1.0.0.2976', 'game_build=1.0.0.3000'))
Refused @('-Generation', '20261014-190232', '-Slot', '0', '-Confirm') 'this script knows only' 'another game build'
[IO.File]::WriteAllText($mpath, $mtext)
# The game running (a harmless copy of ping.exe named CrimsonDesert.exe, stopped right after).
$fake = Join-Path $root 'CrimsonDesert.exe'
Copy-Item -LiteralPath (Join-Path $env:windir 'System32\PING.EXE') -Destination $fake
$proc = Start-Process -FilePath $fake -ArgumentList '-n', '60', '127.0.0.1' -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Milliseconds 300
    Refused @('-Generation', '20261014-190232', '-Slot', '0', '-Confirm') 'the game is running' 'the game running'
} finally {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    $proc.WaitForExit(5000) | Out-Null
}
# Steam running: refused without -SteamRunningOk (checked only when a real Steam is running; never simulated).
if (Get-Process steam -ErrorAction SilentlyContinue) {
    $code = Run @('-Generation', '20261014-190232', '-Slot', '0') -NoSteamFlag
    Check ($code -eq 1 -and $lastOut -match 'Steam is running') "Steam running refused: $lastOut"
}

# ---------------------------------------------------------------- a restore that verifies
$code = Run @('-Generation', '20261014-190232', '-Slot', '0', '-Confirm')
Check ($code -eq 0) "restore of slot 0 (candidate): $lastOut"
foreach ($n in 'lobby.save', 'save.save') {
    Check ((Sha (Join-Path $save "$acct\slot0\$n")) -eq (Sha (Join-Path $g1 "sandbox\$acct\slot0\$n"))) "slot0\$n restored"
    Check ((Sha (Join-Path $save "$acct\slot1\$n")) -ne (Sha (Join-Path $g1 "sandbox\$acct\slot1\$n"))) "slot1\$n untouched"
}
Check (@(Get-ChildItem -LiteralPath (Join-Path $save "$acct\slot0") -Force | Where-Object Name -like '*.cdmp_restore_tmp').Count -eq 0) 'no temporary file left'
$bk = @(Get-ChildItem -LiteralPath $backups -Directory)
Check ($bk.Count -eq 1) 'one backup folder'
if ($bk.Count -ge 1) {
    # The backup holds the real folder as it was (names, sizes, hashes).
    $was = ($before -split "`n" | ForEach-Object { ($_ -split ' ')[0..2] -join ' ' }) -join "`n"
    $bak = ((Snapshot (Join-Path $bk[0].FullName 'save')) -split "`n" | ForEach-Object { ($_ -split ' ')[0..2] -join ' ' }) -join "`n"
    Check ($bak -eq $was) "the backup is the real folder before the restore"
    Check (Test-Path -LiteralPath (Join-Path $bk[0].FullName 'restore.txt')) 'restore.txt written'
}

# ---------------------------------------------------------------- put back after a failure
$mid = Snapshot $save
$env:CDMP_RESTORE_TEST_FAIL = 'slot1'
$code = Run @('-Generation', '20261014-190232', '-Slot', '1', '-Confirm', '-AllowUnverified')
Check ($code -eq 1 -and $lastOut -match 'put back and checked against the backup') "failed restore of an existing slot is put back: $lastOut"
Check ((Snapshot $save) -eq $mid) 'existing slot: the real folder as before the failed restore'
$env:CDMP_RESTORE_TEST_FAIL = 'slot100'
$code = Run @('-Generation', '20261014-200000', '-Slot', '100', '-Confirm', '-AllowUnverified')
Check ($code -eq 1 -and $lastOut -match 'put back and checked against the backup') "failed restore of a new slot is put back: $lastOut"
Check (-not (Test-Path -LiteralPath (Join-Path $save "$acct\slot100"))) 'new slot: the folder it created is removed'
Check ((Snapshot $save) -eq $mid) 'new slot: the real folder as before'
$env:CDMP_RESTORE_TEST_FAIL = ''
# And without the injected failure the new slot is restored.
$code = Run @('-Generation', '20261014-200000', '-Slot', '100', '-Confirm', '-AllowUnverified')
Check ($code -eq 0) "restore of a new slot with -AllowUnverified: $lastOut"
Check ((Sha (Join-Path $save "$acct\slot100\save.save")) -eq (Sha (Join-Path $g2 "sandbox\$acct\slot100\save.save"))) 'slot100 restored'

Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
if ($failures -eq 0) { Write-Host 'All host_save_restore.ps1 tests passed'; exit 0 }
Write-Host "$failures failure(s)"
exit 1
