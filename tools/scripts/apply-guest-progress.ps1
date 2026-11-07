# apply-guest-progress.ps1 - on the GUEST's PC, applies the newest generation of its progress that the host sent back
# through the tunnel (kept by the mod in %LOCALAPPDATA%\cdmp\guest_progress\<host SteamID64>\incoming\gen<n>\) to its
# slot N in save\<account>\slotN\, WITH THE GAME CLOSED. Background: docs/research/noche/progreso-invitado.md.
#
#   .\apply-guest-progress.ps1              # the newest verified generation, to its slot
#   .\apply-guest-progress.ps1 -CheckOnly   # only checks and says what it would do
#   -HostId <SteamID64>    the host's folder in guest_progress (if there are several)
#   -Generation <n>        a specific generation (default: the newest that verifies and fits its slot)
#   -Slot <N>              the target slot (default: the one in manifest.txt; 0-2 or 100-108; never 3 or 206)
#   -Account <id>          the account folder in save\ (default: the only one)
#   -SavesFolder <path>    another saves folder (default: %LOCALAPPDATA%\Pearl Abyss\CD\save); to test on ONE PC on a
#                          COPY, never the host's real one
#   -Progress <path>       another folder with gen<n>\ inside (for example the one from cdmp_peer --save-results)
#
# Steps: 1) refuses if CrimsonDesert.exe is open; 2) verifies the generations (manifest, sizes, SHA-256 and the header,
# as the game does); 3) the account folder; 4) the LINE: a generation is applied only to a slot that holds the save it
# comes from (its lineage_root) or a generation of that same save, or no save at all; generations without lineage_root
# (mods older than 12-10) are never applied; 5) backs up slotN to %LOCALAPPDATA%\cdmp\guest_backups\<date>_slotN\ (the
# newest 10 are kept) and verifies it; 6) prepares slotN in a staging folder on the SAME volume, verifies it, swaps the
# folders with two renames and verifies the result; if anything fails, the slot is left as it was. It never creates
# *_new, *_backup or *_delete names inside save\<account>\ (the game restores or deletes those).

param(
    [string]$HostId,
    [int]$Generation = 0,
    [int]$Slot = -1,
    [string]$Account,
    [string]$SavesFolder,
    [string]$Progress,
    [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'
$local = $env:LOCALAPPDATA

function Fail([string]$msg) {
    Write-Host ""
    Write-Host "ERROR: $msg" -ForegroundColor Red
    Write-Host "Nothing was changed in the saves folder." -ForegroundColor Red
    exit 1
}

function Read-Manifest([string]$path) {
    $m = @{ files = @{} }
    foreach ($line in [System.IO.File]::ReadAllLines($path)) {
        $eq = $line.IndexOf('=')
        if ($eq -le 0) { continue }
        $key = $line.Substring(0, $eq)
        $value = $line.Substring($eq + 1).Trim()
        if ($key -eq 'file') {
            $parts = @($value.Split(' '))
            $f = @{}
            for ($i = 1; $i -lt $parts.Count; $i++) {
                $e = $parts[$i].IndexOf('=')
                if ($e -gt 0) { $f[$parts[$i].Substring(0, $e)] = $parts[$i].Substring($e + 1) }
            }
            $m.files[$parts[0]] = $f
        } else {
            $m[$key] = $value
        }
    }
    return $m
}

function Sha256([string]$path) { return (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant() }

# The 0x80-byte header as the game checks it (0x2357DA0): "SAVE", version 1-2, size 0x80, flags only bit 1, raw != 0,
# 0 < data <= raw, and the file measures exactly 0x80 + data.
function Test-SaveHeader([string]$path) {
    $b = [System.IO.File]::ReadAllBytes($path)
    if ($b.Length -lt 0x80) { return "shorter than its header" }
    if ([System.BitConverter]::ToUInt32($b, 0) -ne 0x45564153) { return "does not start with SAVE" }
    $ver = [System.BitConverter]::ToUInt16($b, 4)
    if ($ver -lt 1 -or $ver -gt 2) { return "version $ver" }
    if ([System.BitConverter]::ToUInt16($b, 6) -ne 0x80) { return "header size is not 0x80" }
    if (([System.BitConverter]::ToUInt32($b, 0xC) -band 0xFFFFFFFD) -ne 0) { return "flags other than bit 1" }
    $raw = [System.BitConverter]::ToUInt32($b, 0x12)
    $data = [System.BitConverter]::ToUInt32($b, 0x16)
    if ($raw -eq 0) { return "raw size 0" }
    if ($data -eq 0 -or $data -gt $raw) { return "data size 0 or larger than raw" }
    # The game only checks 0 < data <= raw: a forged raw would make it reserve gigabytes on load.
    if ($raw -gt 64MB -or $raw -gt (255 * [uint64]$data)) { return "raw size over 64 MB or over 255 times the data" }
    if ($b.Length -ne (0x80 + $data)) { return "the file does not measure 0x80 + data" }
    return $null
}

# A complete and correct generation: manifest, both files with their size, their SHA-256 and their header.
function Test-Generation([string]$dir) {
    $mf = Join-Path $dir 'manifest.txt'
    if (-not (Test-Path -LiteralPath $mf)) { return @{ ok = $false; why = "no manifest.txt (incomplete)" } }
    $m = Read-Manifest $mf
    foreach ($name in 'lobby.save', 'save.save') {
        $p = Join-Path $dir $name
        if (-not (Test-Path -LiteralPath $p)) { return @{ ok = $false; why = "$name is missing" } }
        $f = $m.files[$name]
        if (-not $f) { return @{ ok = $false; why = "the manifest does not name $name" } }
        $len = (Get-Item -LiteralPath $p).Length
        if ([string]$len -ne $f['size']) { return @{ ok = $false; why = "$name measures $len, the manifest says $($f['size'])" } }
        if ((Sha256 $p) -ne $f['sha256'].ToLowerInvariant()) { return @{ ok = $false; why = "the SHA-256 of $name is not the manifest's" } }
        $c = Test-SaveHeader $p
        if ($c) { return @{ ok = $false; why = "${name}: $c" } }
    }
    return @{ ok = $true; manifest = $m }
}

function Test-SlotAllowed([int]$n) { return ($n -ge 0 -and $n -le 2) -or ($n -ge 100 -and $n -le 108) }

# ------------------------------------------------------------------ 1. the game closed

if (Get-Process -Name 'CrimsonDesert' -ErrorAction SilentlyContinue) {
    Fail "Crimson Desert is open. Close it completely (the title screen too) and run this script again."
}

# ------------------------------------------------------------------ 2. the generation

$progressRoot = Join-Path $local 'cdmp\guest_progress'
$hostDir = $null
if ($Progress) {
    $incoming = $Progress
} else {
    if (-not (Test-Path -LiteralPath $progressRoot)) { Fail "$progressRoot does not exist (the host has not sent back any generation yet)." }
    if ($HostId) {
        $hostDir = Join-Path $progressRoot $HostId
    } else {
        $hosts = @(Get-ChildItem -LiteralPath $progressRoot -Directory)
        if ($hosts.Count -ne 1) {
            Write-Host "Host folders in ${progressRoot}:"
            $hosts | ForEach-Object { Write-Host "  $($_.Name)" }
            Fail "there are $($hosts.Count) host folders; choose one with -HostId <SteamID64>."
        }
        $hostDir = $hosts[0].FullName
    }
    $incoming = Join-Path $hostDir 'incoming'
}
if (-not (Test-Path -LiteralPath $incoming)) { Fail "$incoming does not exist." }

# The newest by the TIME in its manifest (the host's time_unix), not by number: if the host's mailbox was emptied, the
# numbering may have started over. At equal time, the higher number.
function Get-GenerationTime($dir) {
    $mf = Join-Path $dir.FullName 'manifest.txt'
    if (-not (Test-Path -LiteralPath $mf)) { return [uint64]0 }
    $m = Read-Manifest $mf
    $h = [uint64]0
    if ($m['time_unix'] -and [uint64]::TryParse($m['time_unix'], [ref]$h)) { return $h }
    return [uint64]0
}
$gens = @(Get-ChildItem -LiteralPath $incoming -Directory | Where-Object { $_.Name -match '^gen[1-9][0-9]*$' } |
          Sort-Object @{ Expression = { Get-GenerationTime $_ }; Descending = $true },
                      @{ Expression = { [uint64]$_.Name.Substring(3) }; Descending = $true })
if ($gens.Count -eq 0) { Fail "there is no generation in $incoming." }

# All are verified: the lines (below) are built from the valid ones.
$valid = @()
foreach ($g in $gens) {
    $v = Test-Generation $g.FullName
    if ($v.ok) {
        $valid += [pscustomobject]@{ dir = $g; manifest = $v.manifest }
    } else {
        Write-Host "  $($g.Name): NOT valid ($($v.why))" -ForegroundColor Yellow
    }
}

# ------------------------------------------------------------------ 3. the saves folder

$saveRoot = if ($SavesFolder) { $SavesFolder } else { Join-Path $local 'Pearl Abyss\CD\save' }
if (-not (Test-Path -LiteralPath $saveRoot)) { Fail "the saves folder $saveRoot does not exist." }
if ($Account) {
    $accountDir = Join-Path $saveRoot $Account
} else {
    $accounts = @(Get-ChildItem -LiteralPath $saveRoot -Directory | Where-Object { $_.Name -match '^[0-9]+$' })
    if ($accounts.Count -ne 1) { Fail "there are $($accounts.Count) account folders in $saveRoot; choose one with -Account <id>." }
    $accountDir = $accounts[0].FullName
}
if (-not (Test-Path -LiteralPath $accountDir)) { Fail "$accountDir does not exist." }

# ------------------------------------------------------------------ 4. the line

# Each generation continues ONE save: its line, which the host names with lineage_root (the SHA-256 of the save.save
# the first session came from). A generation is applied only to a slot holding a save of its line (the root, a
# generation, or the base of a generation: lineage_base, the save.save uploaded in that session) or holding no save.
# So the progress of one save never overwrites a different one.
function Test-Hex($h) { return ($h -is [string]) -and ($h.ToLowerInvariant() -match '^[0-9a-f]{64}$') }
function Get-Time($c) {
    $h = [uint64]0
    if ($c.manifest['time_unix'] -and [uint64]::TryParse($c.manifest['time_unix'], [ref]$h)) { return $h }
    return [uint64]0
}
$members = @{}
foreach ($c in $valid) {
    $root = $c.manifest['lineage_root']
    if (-not (Test-Hex $root)) { continue }
    $root = $root.ToLowerInvariant()
    if (-not $members.ContainsKey($root)) { $members[$root] = @{ $root = $true } }
    $members[$root][$c.manifest.files['save.save']['sha256'].ToLowerInvariant()] = $true
    if (Test-Hex $c.manifest['lineage_base']) { $members[$root][$c.manifest['lineage_base'].ToLowerInvariant()] = $true }
}
# The last one applied (applied.txt) is of its line too, even if its generation is no longer in incoming.
if ($hostDir -and (Test-Path -LiteralPath (Join-Path $hostDir 'applied.txt'))) {
    $ap = Read-Manifest (Join-Path $hostDir 'applied.txt')
    if ((Test-Hex $ap['lineage_root']) -and (Test-Hex $ap['save_sha256'])) {
        $r = $ap['lineage_root'].ToLowerInvariant()
        if (-not $members.ContainsKey($r)) { $members[$r] = @{ $r = $true } }
        $members[$r][$ap['save_sha256'].ToLowerInvariant()] = $true
    }
}

# Why $c cannot be applied to slot $n ($null = it can).
function Get-LineProblem($c, [int]$n) {
    if ($n -eq 3) { return "slot 3 is the CrossSave one (saving it uploads to Pearl Abyss): never applied" }
    if ($n -eq 206) { return "slot 206 is deleted after loading: never applied" }
    if (-not (Test-SlotAllowed $n)) { return "slot $n is not 0-2 or 100-108" }
    $root = $c.manifest['lineage_root']
    if (-not $root) {
        return "it does not say which save it comes from (no lineage_root: a mod older than 12-10); never applied, delete it"
    }
    if (-not (Test-Hex $root)) { return "its lineage_root is not a SHA-256" }
    $root = $root.ToLowerInvariant()
    $sv = Join-Path (Join-Path $accountDir "slot$n") 'save.save'
    if (-not (Test-Path -LiteralPath $sv)) { return $null }
    $current = Sha256 $sv
    if (-not $members[$root].ContainsKey($current)) {
        return ("slot $n holds ANOTHER save (save.save {0}...), which is neither this generation's nor one of its " +
                "generations (line {1}...)") -f $current.Substring(0, 16), $root.Substring(0, 16)
    }
    # Never backwards within the line: if the slot holds a generation of the line, only a newer one; if it holds the
    # base of some generations, only those or newer (the time is the host's, from its manifest).
    if ($c.manifest.files['save.save']['sha256'].ToLowerInvariant() -eq $current) { return "slot $n already has it" }
    $hc = Get-Time $c
    $cb = if (Test-Hex $c.manifest['lineage_base']) { $c.manifest['lineage_base'].ToLowerInvariant() } else { '' }
    $tmin = $null
    $fromGen = ''
    foreach ($o in $valid) {
        if ($o.manifest['lineage_root'] -ne $c.manifest['lineage_root']) { continue }
        if ($o.manifest.files['save.save']['sha256'].ToLowerInvariant() -eq $current -and (Get-Time $o) -ge $hc) {
            return "slot $n already has an equal or newer generation of this save ($($o.dir.Name))"
        }
        if ((Test-Hex $o.manifest['lineage_base']) -and $o.manifest['lineage_base'].ToLowerInvariant() -eq $current -and
            ($null -eq $tmin -or (Get-Time $o) -lt $tmin)) {
            $tmin = Get-Time $o
            $fromGen = $o.dir.Name
        }
    }
    # The slot holds the save some generations came from: those from before them are older.
    if ($null -ne $tmin -and $cb -ne $current -and $hc -lt $tmin) {
        return "it is older than the save in slot $n ($fromGen comes from that one)"
    }
    return $null
}

$chosen = $null
foreach ($c in $valid) {
    if ($Generation -gt 0 -and $c.dir.Name -ne "gen$Generation") { continue }
    $n = if ($Slot -ge 0) { $Slot } elseif ($c.manifest['slot'] -match '^[0-9]+$') { [int]$c.manifest['slot'] } else { -1 }
    $problem = Get-LineProblem $c $n
    if (-not $problem) { $chosen = $c; $targetSlot = $n; break }
    Write-Host "  $($c.dir.Name) (for slot $n): not applied: $problem" -ForegroundColor Yellow
    if ($Generation -gt 0) { Fail "gen${Generation}: $problem." }
}
if (-not $chosen) {
    Fail "no generation in $incoming can be applied$(if ($Generation -gt 0) { " (gen$Generation does not exist or is not valid)" })."
}
$manifest = $chosen.manifest
$genNum = [int]$chosen.dir.Name.Substring(3)
Write-Host "Generation: $($chosen.dir.FullName) (gen$genNum, reason $($manifest['reason_text']), received $($manifest['received_utc']), line $($manifest['lineage_root'].Substring(0, 16))...)"

$target = Join-Path $accountDir "slot$targetSlot"
foreach ($bad in "slot${targetSlot}_new", "slot${targetSlot}_backup") {
    if (Test-Path -LiteralPath (Join-Path $accountDir $bad)) {
        Fail "$bad exists in ${accountDir}: a save was interrupted. Load that save once on your own so the game repairs it, close it and try again."
    }
}
# A game slot only has files; with subfolders, the backup and the staging (files only) would not keep them and the swap
# would remove them: refuse.
if ((Test-Path -LiteralPath $target) -and @(Get-ChildItem -LiteralPath $target -Directory).Count -gt 0) {
    Fail "$target has subfolders (it is not a normal game slot): not touched."
}
Write-Host "Target: $target"

# The staging, on the SAME volume as the saves folder (the swaps are renames).
$stagingRoot = Join-Path $local 'cdmp\staging'
if ([System.IO.Path]::GetPathRoot($stagingRoot) -ne [System.IO.Path]::GetPathRoot($accountDir)) {
    $stagingRoot = Join-Path (Split-Path $saveRoot -Parent) 'cdmp_guest_apply_staging'
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backupRoot = Join-Path $local 'cdmp\guest_backups'
$backup = Join-Path $backupRoot "${stamp}_slot$targetSlot"
# Two applies in the same second never share a backup (the second would overwrite the first).
while (Test-Path -LiteralPath $backup) {
    Start-Sleep -Seconds 1
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $backup = Join-Path $backupRoot "${stamp}_slot$targetSlot"
}
$staged = Join-Path $stagingRoot "slot$targetSlot"
$old = Join-Path $stagingRoot "slot${targetSlot}_old_$stamp"

if ($CheckOnly) {
    Write-Host ""
    Write-Host "CHECK ONLY: generation gen$genNum is valid. It would do:" -ForegroundColor Green
    Write-Host "  backup:  $target -> $backup"
    Write-Host "  staging: $staged"
    Write-Host "  swap:    $target -> $old ; $staged -> $target"
    exit 0
}

# ------------------------------------------------------------------ 5. backup

New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
if (Test-Path -LiteralPath $target) {
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    foreach ($f in Get-ChildItem -LiteralPath $target -File) {
        Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $backup $f.Name)
        if ((Sha256 $f.FullName) -ne (Sha256 (Join-Path $backup $f.Name))) { Fail "the backup of $($f.Name) does not match." }
    }
    Write-Host "Backup: $backup ($(@(Get-ChildItem -LiteralPath $backup -File).Count) files, verified)"
} else {
    Write-Host "Slot slot$targetSlot did not exist: nothing to back up."
}
# The 10 newest backups stay.
$copies = @(Get-ChildItem -LiteralPath $backupRoot -Directory | Where-Object { $_.Name -match '^\d{8}-\d{6}_slot\d+$' } | Sort-Object Name -Descending)
if ($copies.Count -gt 10) {
    foreach ($c in $copies[10..($copies.Count - 1)]) { Remove-Item -LiteralPath $c.FullName -Recurse -Force }
}

# ------------------------------------------------------------------ 6. staging and swap

New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null
if (Test-Path -LiteralPath $staged) { Remove-Item -LiteralPath $staged -Recurse -Force }
New-Item -ItemType Directory -Force -Path $staged | Out-Null
if (Test-Path -LiteralPath $target) {
    # The slot's other files (if any) are kept; lobby.save and save.save are the generation's.
    foreach ($f in Get-ChildItem -LiteralPath $target -File) { Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $staged $f.Name) }
}
foreach ($name in 'lobby.save', 'save.save') {
    Copy-Item -LiteralPath (Join-Path $chosen.dir.FullName $name) -Destination (Join-Path $staged $name) -Force
    if ((Sha256 (Join-Path $staged $name)) -ne $manifest.files[$name]['sha256'].ToLowerInvariant()) {
        Remove-Item -LiteralPath $staged -Recurse -Force
        Fail "the staged copy of $name does not match the manifest."
    }
}

$movedOld = $false
$movedNew = $false
try {
    if (Test-Path -LiteralPath $target) {
        Move-Item -LiteralPath $target -Destination $old
        $movedOld = $true
    }
    Move-Item -LiteralPath $staged -Destination $target
    $movedNew = $true
    foreach ($name in 'lobby.save', 'save.save') {
        if ((Sha256 (Join-Path $target $name)) -ne $manifest.files[$name]['sha256'].ToLowerInvariant()) {
            throw "the applied $name does not match the manifest"
        }
    }
} catch {
    $err = $_.Exception.Message
    # Roll back: the slot as it was (the failed one stays apart in the staging).
    try {
        if ($movedNew) {
            Move-Item -LiteralPath $target -Destination (Join-Path $stagingRoot "slot${targetSlot}_failed_$stamp")
        }
        if ($movedOld) { Move-Item -LiteralPath $old -Destination $target }
    } catch {
        Write-Host "SERIOUS ERROR during the rollback: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host "Restore $target by hand from the backup $backup." -ForegroundColor Red
        exit 2
    }
    Write-Host ""
    Write-Host "ERROR while applying: $err" -ForegroundColor Red
    Write-Host "The slot was put back as it was; the backup is still in $backup." -ForegroundColor Red
    exit 1
}
if ($movedOld) { Remove-Item -LiteralPath $old -Recurse -Force }

# The record of what was applied (the mod sends it as base_gen in its next offer: the host returns nothing older).
if ($hostDir) {
    $lines = @(
        "generation=$genNum",
        "slot=$targetSlot",
        "applied_utc=$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))",
        "source=$($chosen.dir.FullName)",
        "backup=$backup",
        "lineage_root=$($manifest['lineage_root'])",
        "lobby_sha256=$($manifest.files['lobby.save']['sha256'])",
        "save_sha256=$($manifest.files['save.save']['sha256'])"
    )
    [System.IO.File]::WriteAllLines((Join-Path $hostDir 'applied.txt'), $lines)
}

Write-Host ""
Write-Host "DONE: gen$genNum applied to $target (SHA-256 checked)." -ForegroundColor Green
Write-Host "Backup of what was there: $backup"
Write-Host "Load the save on your own to check it. When you close the game, Steam Cloud will upload that slot."
