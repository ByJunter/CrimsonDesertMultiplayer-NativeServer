# P8 S35 phase E (docs/research/s25/s35-fase-e-guardado.md): copies chosen slots of an exported host save generation
# (host_save=export|probe, <bin64>\cdmp_save_exports\<generation>\) into the REAL save folder, by hand, offline.
#
#   .\scripts\host_save_restore.ps1 -Generation 20261014-190232 -Slot 0            # dry run: checks and plan only
#   .\scripts\host_save_restore.ps1 -Generation 20261014-190232 -Slot 0 -Confirm   # really restores slot 0
#
# It refuses when: the game is running; Steam is running (unless -SteamRunningOk: Steam Cloud could upload or bring
# back a rival copy); the manifest is not complete, is for another game build, or any file of the generation does not
# match its size and SHA-256; a slot is not in the whitelist 0-2 / 100-108 (3 = CrossSave and 200-207 = the guest
# pool, 204 the guest's own slot, are always refused); a slot's lobby.save + save.save pair is missing or its header is
# not valid; probe classified the slot as not-candidate; the real account folder is missing; or the real folder holds
# the game's own leftovers of that slot (slotN_new / slotN_backup*), which its native recovery would use.
# A slot that probe did not classify as candidate (unknown, or exported with host_save=export) needs -AllowUnverified
# to be restored (the dry run only warns).
# Before changing anything the whole real save folder is copied to backups\host_save_restore-<time>\save and verified.
# After the copy every restored file is read back; on any mismatch the slot is put back from that backup, the put-back
# files are checked against the backup and a slot folder this script created is removed again.
# Tests (tests/host_save_restore_tests.ps1) run it against %TEMP% with LOCALAPPDATA redirected; CDMP_RESTORE_TEST_FAIL
# = "slotN" makes the copy of that slot fail after its first file (to test the put-back). Nothing else reads it.
#
# A verified file is NOT proof of who wrote it nor of a coherent world (audit S34 section 5): restoring is the user's
# decision. Never run it while the game or Steam may write the save folder.

param(
    [Parameter(Mandatory = $true)][string]$Generation,
    [Parameter(Mandatory = $true)][int[]]$Slot,
    [string]$Source = 'sandbox',
    [string]$Account = '',
    [string]$ExportDir = $(if ($env:CDMP_GAME_DIR) { Join-Path $env:CDMP_GAME_DIR 'cdmp_save_exports' } else { 'D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\cdmp_save_exports' }),
    [string]$BackupDir = $(Join-Path (Split-Path $PSScriptRoot -Parent) 'backups'),
    [switch]$Confirm,
    [switch]$AllowUnverified,
    [switch]$SteamRunningOk
)
$ErrorActionPreference = 'Stop'

$kHeader = 'cdmp host save generation (P8 S35 phase E)'
$kBuild = '1.0.0.2976'
$kPe = '0x6AB28F00'
$saveRoot = Join-Path $env:LOCALAPPDATA 'Pearl Abyss\CD\save'

function Refuse([string]$why) {
    Write-Host "REFUSED: $why" -ForegroundColor Red
    exit 1
}

function Test-Whitelisted([int]$n) { return ($n -ge 0 -and $n -le 2) -or ($n -ge 100 -and $n -le 108) }

function Get-Sha([string]$path) { return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }

# The game's own header check (guest_save_logic.h check_header): "SAVE", version 1-2, header 0x80, flags only bit 1,
# raw size > 0, 0 < data <= raw, raw <= 64 MB and <= 255 x data, file <= 16 MB and = 0x80 + data.
function Test-SaveHeader([string]$path) {
    $len = (Get-Item -LiteralPath $path).Length
    if ($len -lt 0x80) { return 'shorter than its 0x80-byte header' }
    if ($len -gt 16MB) { return 'file larger than 16 MB' }
    $fs = [System.IO.File]::OpenRead($path)
    try {
        $h = New-Object byte[] 0x80
        if ($fs.Read($h, 0, 0x80) -ne 0x80) { return 'header unreadable' }
    } finally { $fs.Dispose() }
    if ([System.BitConverter]::ToUInt32($h, 0) -ne 0x45564153) { return 'magic is not SAVE' }
    $ver = [System.BitConverter]::ToUInt16($h, 4)
    if ($ver -lt 1 -or $ver -gt 2) { return 'version not 1 or 2' }
    if ([System.BitConverter]::ToUInt16($h, 6) -ne 0x80) { return 'header size not 0x80' }
    if (([System.BitConverter]::ToUInt32($h, 0xC) -band (-bnot 2)) -ne 0) { return 'flags other than bit 1' }
    $raw = [System.BitConverter]::ToUInt32($h, 0x12)
    $data = [System.BitConverter]::ToUInt32($h, 0x16)
    if ($raw -eq 0) { return 'raw size 0' }
    if ($data -eq 0 -or $data -gt $raw) { return 'data size 0 or above the raw size' }
    if ([int64]$raw -gt 64MB -or [int64]$raw -gt 255 * [int64]$data) { return 'raw size above 64 MB or 255 times the data' }
    if ($len -ne (0x80 + [int64]$data)) { return 'file size is not 0x80 + data size' }
    return $null
}

# ---------------------------------------------------------------- the processes
if (Get-Process CrimsonDesert -ErrorAction SilentlyContinue) { Refuse 'the game is running: close it first.' }
if ((Get-Process steam -ErrorAction SilentlyContinue) -and -not $SteamRunningOk) {
    Refuse 'Steam is running: exit Steam completely first (Steam Cloud syncs the save folder), or pass -SteamRunningOk knowingly.'
}

# ---------------------------------------------------------------- the generation and its manifest
if (Test-Path -LiteralPath $ExportDir -PathType Container) {
    $ExportDir = (Resolve-Path -LiteralPath $ExportDir).Path
} elseif (-not (Test-Path -LiteralPath $Generation -PathType Container)) {
    Refuse "the export folder '$ExportDir' does not exist (pass -ExportDir, or the generation's full path)."
}
if ($Generation -notmatch '^\d{8}-\d{6}(-[1-9]\d{0,3})?$' -and -not (Test-Path -LiteralPath $Generation -PathType Container)) {
    Refuse "'$Generation' is not a generation name (yyyymmdd-hhmmss[-N]) nor a folder."
}
$gen = if (Test-Path -LiteralPath $Generation -PathType Container) { (Resolve-Path -LiteralPath $Generation).Path } else { Join-Path $ExportDir $Generation }
$genName = Split-Path $gen -Leaf
if ($genName -notmatch '^\d{8}-\d{6}(-[1-9]\d{0,3})?$') { Refuse "'$gen' is not a generation folder (a .partial export is never complete)." }
$manifestPath = Join-Path $gen 'manifest.txt'
if (-not (Test-Path -LiteralPath $manifestPath)) { Refuse "no manifest.txt in '$gen'." }
$lines = @(Get-Content -LiteralPath $manifestPath -Encoding UTF8 | Where-Object { $_ -ne '' })
if ($lines.Count -lt 2 -or $lines[0] -ne $kHeader) { Refuse 'the manifest header is not a cdmp host save generation.' }
if ($lines[$lines.Count - 1] -ne 'state=complete') { Refuse 'the manifest is not complete (no final state=complete).' }

$meta = @{}
$files = New-Object System.Collections.Generic.List[object]
$accounts = New-Object System.Collections.Generic.List[string]
$probes = @{}
foreach ($l in $lines[1..($lines.Count - 1)]) {
    $eq = $l.IndexOf('=')
    if ($eq -lt 1) { Refuse "malformed manifest line '$l'." }
    $key = $l.Substring(0, $eq); $v = $l.Substring($eq + 1)
    switch ($key) {
        'file' {
            $p = $v.Split('|')
            if ($p.Count -ne 5 -or $p[3] -notmatch '^[0-9a-f]{64}$') { Refuse "malformed file line '$l'." }
            $files.Add([pscustomobject]@{ Source = $p[0]; Path = $p[1]; Size = [int64]$p[2]; Sha = $p[3]; MTime = [int64]$p[4] })
        }
        'account' { $p = $v.Split('|'); if ($p.Count -eq 2 -and $p[0] -eq $Source) { $accounts.Add($p[1]) } }
        'probe' { $p = $v.Split('|'); if ($p.Count -eq 5 -and $p[0] -eq $Source) { $probes["$($p[1])\$($p[2])"] = $p } }
        default { if (-not $meta.ContainsKey($key)) { $meta[$key] = $v } }
    }
}
if ($meta['game_build'] -ne $kBuild -or $meta['pe_timestamp'] -ne $kPe) {
    Refuse "the generation is for build $($meta['game_build']) / $($meta['pe_timestamp']); this script knows only $kBuild / $kPe."
}
if ($meta['generation'] -ne $genName) { Refuse "the manifest names generation '$($meta['generation'])', the folder is '$genName'." }

# Every file of the generation, not only the slots asked for.
Write-Host "Verifying $($files.Count) files of generation $genName ..."
foreach ($f in $files) {
    $p = if ($f.Source -eq 'journal') { Join-Path $gen ('journals\' + $f.Path) } else { Join-Path $gen ($f.Source + '\' + $f.Path) }
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { Refuse "missing in the generation: $p" }
    if ((Get-Item -LiteralPath $p).Length -ne $f.Size) { Refuse "size differs from the manifest: $p" }
    if ((Get-Sha $p) -ne $f.Sha) { Refuse "SHA-256 differs from the manifest: $p" }
}
$srcRoot = Join-Path $gen $Source
if (-not (Test-Path -LiteralPath $srcRoot -PathType Container)) {
    Refuse "the generation has no copied '$Source' folder (it may be 'identical to' another generation: restore from that one)."
}
$listed = @{}
foreach ($f in $files) { if ($f.Source -eq $Source) { $listed[$f.Path.ToLowerInvariant()] = $true } }
foreach ($item in Get-ChildItem -LiteralPath $srcRoot -Recurse -File -Force) {
    $rel = $item.FullName.Substring($srcRoot.Length + 1).ToLowerInvariant()
    if (-not $listed.ContainsKey($rel)) { Refuse "a file not in the manifest: $($item.FullName)" }
}

# ---------------------------------------------------------------- the account and the slots
if ($Account -eq '') {
    if ($accounts.Count -ne 1) { Refuse "the '$Source' tree has $($accounts.Count) account folders: pass -Account." }
    $Account = $accounts[0]
} elseif (-not $accounts.Contains($Account)) { Refuse "account '$Account' is not in the '$Source' tree." }
$realAccount = Join-Path $saveRoot $Account
if (-not (Test-Path -LiteralPath $realAccount -PathType Container)) {
    Refuse "the real account folder '$realAccount' does not exist (another Steam account or PC?)."
}

$plan = @()
foreach ($n in ($Slot | Sort-Object -Unique)) {
    if ($n -eq 3) { Refuse 'slot 3 is CrossSave: never restored.' }
    if ($n -ge 200 -and $n -le 207) { Refuse "slot $n is in the guest pool 200-207 (204 = the guest's own slot): never restored as the host's." }
    if (-not (Test-Whitelisted $n)) { Refuse "slot $n is outside the whitelist 0-2 / 100-108." }
    $folder = "slot$n"
    $prefix = ("$Account\$folder\").ToLowerInvariant()
    $slotFiles = @($files | Where-Object { $_.Source -eq $Source -and $_.Path.ToLowerInvariant().StartsWith($prefix) })
    $names = @($slotFiles | ForEach-Object { $_.Path.Substring($prefix.Length).ToLowerInvariant() })
    if (-not ($names -contains 'lobby.save') -or -not ($names -contains 'save.save')) {
        Refuse "$folder of '$Source' has no lobby.save + save.save pair in this generation."
    }
    if (@($slotFiles | Where-Object { $_.Path.Substring($prefix.Length).Contains('\') }).Count -gt 0) {
        Refuse "$folder of '$Source' has sub-folders: not a slot this script knows."
    }
    foreach ($kind in 'lobby.save', 'save.save') {
        $bad = Test-SaveHeader (Join-Path $srcRoot "$Account\$folder\$kind")
        if ($bad) { Refuse "$folder\$kind header: $bad" }
    }
    $probe = $probes["$Account\$folder"]
    if ($probe -and $probe[3] -eq 'not-candidate') { Refuse "probe classified $folder as not-candidate: $($probe[4])" }
    $verdict = if ($probe) { "$($probe[3]): $($probe[4])" } else { 'no probe verdict (exported with host_save=export)' }
    $left = @(Get-ChildItem -LiteralPath $realAccount -Directory -Force | Where-Object { $_.Name -like "${folder}_*" })
    if ($left.Count -gt 0) {
        Refuse "the real folder holds the game's leftovers of ${folder}: $(($left | ForEach-Object Name) -join ', ') (its native recovery would use them)."
    }
    $plan += [pscustomobject]@{ Slot = $n; Folder = $folder; Files = $slotFiles; Verdict = $verdict }
}

# ---------------------------------------------------------------- the plan
Write-Host ''
Write-Host "Generation $genName ($Source), made $($meta['created']) by run $($meta['run']), mode $($meta['mode'])"
Write-Host "Real folder: $realAccount"
foreach ($s in $plan) {
    $exists = Test-Path -LiteralPath (Join-Path $realAccount $s.Folder)
    Write-Host ("  {0}: {1} file(s) -> {2} ({3})" -f $s.Folder, $s.Files.Count, (Join-Path $realAccount $s.Folder), $(if ($exists) { 'replaces the current files' } else { 'new slot' }))
    Write-Host "      verdict: $($s.Verdict)"
    if ($s.Verdict -notlike 'candidate*') {
        Write-Warning "$($s.Folder) is not a probe candidate: authorship and world coherence are NOT verified (needs -AllowUnverified)."
        if ($Confirm -and -not $AllowUnverified) { Refuse "$($s.Folder) is not a probe candidate: pass -AllowUnverified to restore it anyway." }
    }
}
if (-not $Confirm) {
    Write-Host ''
    Write-Host 'Dry run: nothing was changed. Repeat with -Confirm to restore.' -ForegroundColor Yellow
    exit 0
}

# ---------------------------------------------------------------- backup of the real folder, verified
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $BackupDir "host_save_restore-$stamp"
for ($i = 2; (Test-Path -LiteralPath $backup) -and $i -lt 100; $i++) { $backup = Join-Path $BackupDir "host_save_restore-$stamp-$i" }
if (Test-Path -LiteralPath $backup) { Refuse "'$backup' already exists." }
New-Item -ItemType Directory -Force -Path $backup | Out-Null
$backupSave = Join-Path $backup 'save'
Copy-Item -LiteralPath $saveRoot -Destination $backupSave -Recurse -Force
foreach ($item in Get-ChildItem -LiteralPath $saveRoot -Recurse -File -Force) {
    $rel = $item.FullName.Substring($saveRoot.Length + 1)
    $copy = Join-Path $backupSave $rel
    if (-not (Test-Path -LiteralPath $copy -PathType Leaf) -or (Get-Sha $copy) -ne (Get-Sha $item.FullName)) {
        Refuse "the backup of '$($item.FullName)' does not verify: nothing restored (backup left in '$backup')."
    }
}
Write-Host "Backup of the real save folder: $backupSave (verified)"
$log = New-Object System.Collections.Generic.List[string]
$log.Add("host_save_restore ${stamp}: generation $gen, source $Source, account $Account, slots $($Slot -join ',')")

# ---------------------------------------------------------------- the copy, slot by slot
# Puts the slot back as the backup has it, checks every put-back file against the backup, and removes the slot folder
# when this script created it. Throws if the result does not match the backup.
function Restore-FromBackup($s, [bool]$created) {
    $dst = Join-Path $realAccount $s.Folder
    $saved = Join-Path $backupSave "$Account\$($s.Folder)"
    foreach ($f in $s.Files) {
        $name = Split-Path $f.Path -Leaf
        $target = Join-Path $dst $name
        $old = Join-Path $saved $name
        $tmp = "$target.cdmp_restore_tmp"
        if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force }
        if (Test-Path -LiteralPath $old) {
            Copy-Item -LiteralPath $old -Destination $target -Force
            if ((Get-Sha $target) -ne (Get-Sha $old)) { throw "'$target' does not match the backup after the put-back" }
        } elseif (Test-Path -LiteralPath $target) {
            Remove-Item -LiteralPath $target -Force  # it did not exist before
        }
    }
    if ($created -and (Test-Path -LiteralPath $dst)) {
        if (@(Get-ChildItem -LiteralPath $dst -Force).Count -eq 0) { Remove-Item -LiteralPath $dst -Force }
        else { throw "'$dst' (created by this script) is not empty after the put-back" }
    }
}

$failed = $false
foreach ($s in $plan) {
    $dst = Join-Path $realAccount $s.Folder
    $created = -not (Test-Path -LiteralPath $dst)
    try {
        if ($created) { New-Item -ItemType Directory -Path $dst | Out-Null }
        $copied = 0
        foreach ($f in $s.Files) {
            if ($copied -eq 1 -and $env:CDMP_RESTORE_TEST_FAIL -eq $s.Folder) { throw 'failure injected by the test' }
            $copied++
            $from = Join-Path $srcRoot $f.Path
            $target = Join-Path $dst (Split-Path $f.Path -Leaf)
            $tmp = "$target.cdmp_restore_tmp"
            Copy-Item -LiteralPath $from -Destination $tmp -Force
            if ((Get-Sha $tmp) -ne $f.Sha) { throw "the copy '$tmp' does not verify" }
            Move-Item -LiteralPath $tmp -Destination $target -Force
            (Get-Item -LiteralPath $target).LastWriteTimeUtc = [DateTime]::FromFileTimeUtc($f.MTime)
        }
        foreach ($f in $s.Files) {
            $target = Join-Path $dst (Split-Path $f.Path -Leaf)
            if ((Get-Item -LiteralPath $target).Length -ne $f.Size -or (Get-Sha $target) -ne $f.Sha) {
                throw "'$target' does not read back as the manifest says"
            }
        }
        Write-Host "  $($s.Folder): restored and verified" -ForegroundColor Green
        $log.Add("  $($s.Folder): restored and verified ($($s.Files.Count) files)")
    } catch {
        $failed = $true
        Write-Host "  $($s.Folder): FAILED ($($_.Exception.Message)); putting it back from the backup" -ForegroundColor Red
        $log.Add("  $($s.Folder): FAILED ($($_.Exception.Message)); put back from the backup")
        try {
            Restore-FromBackup $s $created
            Write-Host "  $($s.Folder): put back and checked against the backup"
        } catch {
            Write-Host "  put back FAILED too ($($_.Exception.Message)): copy '$backupSave' over '$saveRoot' by hand." -ForegroundColor Red
        }
    }
}
$log | Set-Content -LiteralPath (Join-Path $backup 'restore.txt') -Encoding UTF8
if ($failed) {
    Write-Host "Some slots were NOT restored; the real folder's previous state is in '$backupSave'." -ForegroundColor Red
    exit 1
}
Write-Host "Done. To undo: close the game and Steam, then copy '$backupSave' back over '$saveRoot'."
