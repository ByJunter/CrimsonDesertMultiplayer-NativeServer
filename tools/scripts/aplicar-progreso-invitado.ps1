# aplicar-progreso-invitado.ps1 - P8 sesion 25, fase B (docs/research/noche/progreso-invitado.md, "Implementado
# (fase B)"): aplica en el PC del INVITADO la ultima generacion de su progreso que el anfitrion le devolvio por el
# tunel (guardada por el mod en %LOCALAPPDATA%\cdmp\guest_progress\<SteamID64 anfitrion>\incoming\gen<n>\) a su
# ranura N de save\<cuenta>\slotN\, CON EL JUEGO CERRADO.
#
#   .\tools\scripts\aplicar-progreso-invitado.ps1                   # la ultima generacion verificada, a su ranura
#   .\tools\scripts\aplicar-progreso-invitado.ps1 -SoloComprobar    # solo comprueba y dice que haria
#   -Anfitrion <SteamID64>   carpeta del anfitrion en guest_progress (si hay varias)
#   -Generacion <n>          una generacion concreta (por defecto la mas nueva que se verifica y cabe en su ranura)
#   -Ranura <N>              la ranura destino (por defecto la del manifest.txt; 0-2 o 100-108; nunca 3 ni 206)
#   -Cuenta <id>             la carpeta de cuenta en save\ (por defecto la unica)
#   -CarpetaPartidas <ruta>  otra carpeta de partidas (por defecto %LOCALAPPDATA%\Pearl Abyss\CD\save); para probar
#                            en UN PC sobre una COPIA, nunca la real del anfitrion
#   -Progreso <ruta>         otra carpeta con gen<n>\ dentro (p. ej. la de cdmp_peer --save-results)
#
# Pasos: 1) se niega si CrimsonDesert.exe esta abierto; 2) verifica las generaciones (manifest, tamanos, SHA-256 y
# la cabecera como el juego); 3) la carpeta de cuenta; 4) la LINEA (sesion 25): una generacion solo se aplica a una
# ranura que tenga la partida de la que sale (su lineage_root) o una generacion de esa misma partida, o ninguna
# partida; las generaciones sin lineage_root (mod anterior al 12-10) nunca se aplican; 5) copia de seguridad de slotN
# en %LOCALAPPDATA%\cdmp\guest_backups\<fecha>_slotN\ (se guardan las 10 ultimas) y la verifica; 6) prepara slotN en
# una carpeta de preparacion del MISMO volumen, la verifica, cambia las carpetas con dos renombrados y verifica el
# resultado; si algo falla, deja la ranura como estaba. Nunca crea nombres *_new, *_backup ni *_delete dentro de
# save\<cuenta>\ (el juego los restaura o borra).

param(
    [string]$Anfitrion,
    [int]$Generacion = 0,
    [int]$Ranura = -1,
    [string]$Cuenta,
    [string]$CarpetaPartidas,
    [string]$Progreso,
    [switch]$SoloComprobar
)

$ErrorActionPreference = 'Stop'
$local = $env:LOCALAPPDATA

function Fallo([string]$msg) {
    Write-Host ""
    Write-Host "ERROR: $msg" -ForegroundColor Red
    Write-Host "No se ha cambiado nada en la carpeta de partidas." -ForegroundColor Red
    exit 1
}

function Leer-Manifest([string]$path) {
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

# La cabecera de 0x80 bytes como la comprueba el juego (0x2357DA0): "SAVE", version 1-2, tamano 0x80, flags solo el
# bit 1, raw != 0, 0 < data <= raw, y el fichero mide exactamente 0x80 + data.
function Cabecera-Valida([string]$path) {
    $b = [System.IO.File]::ReadAllBytes($path)
    if ($b.Length -lt 0x80) { return "mas corto que su cabecera" }
    if ([System.BitConverter]::ToUInt32($b, 0) -ne 0x45564153) { return "no empieza por SAVE" }
    $ver = [System.BitConverter]::ToUInt16($b, 4)
    if ($ver -lt 1 -or $ver -gt 2) { return "version $ver" }
    if ([System.BitConverter]::ToUInt16($b, 6) -ne 0x80) { return "tamano de cabecera distinto de 0x80" }
    if (([System.BitConverter]::ToUInt32($b, 0xC) -band 0xFFFFFFFD) -ne 0) { return "flags distintos del bit 1" }
    $raw = [System.BitConverter]::ToUInt32($b, 0x12)
    $data = [System.BitConverter]::ToUInt32($b, 0x16)
    if ($raw -eq 0) { return "tamano raw 0" }
    if ($data -eq 0 -or $data -gt $raw) { return "tamano de datos 0 o mayor que raw" }
    # El juego solo comprueba 0 < datos <= raw: un raw falsificado le haria reservar gigas al cargar.
    if ($raw -gt 64MB -or $raw -gt (255 * [uint64]$data)) { return "tamano raw mayor de 64 MB o de 255 veces los datos" }
    if ($b.Length -ne (0x80 + $data)) { return "el fichero no mide 0x80 + datos" }
    return $null
}

# Una generacion completa y correcta: manifest, los dos ficheros con su tamano, su SHA-256 y su cabecera.
function Verificar-Generacion([string]$dir) {
    $mf = Join-Path $dir 'manifest.txt'
    if (-not (Test-Path -LiteralPath $mf)) { return @{ ok = $false; why = "sin manifest.txt (incompleta)" } }
    $m = Leer-Manifest $mf
    foreach ($name in 'lobby.save', 'save.save') {
        $p = Join-Path $dir $name
        if (-not (Test-Path -LiteralPath $p)) { return @{ ok = $false; why = "falta $name" } }
        $f = $m.files[$name]
        if (-not $f) { return @{ ok = $false; why = "el manifest no nombra $name" } }
        $len = (Get-Item -LiteralPath $p).Length
        if ([string]$len -ne $f['size']) { return @{ ok = $false; why = "$name mide $len, el manifest dice $($f['size'])" } }
        if ((Sha256 $p) -ne $f['sha256'].ToLowerInvariant()) { return @{ ok = $false; why = "el SHA-256 de $name no es el del manifest" } }
        $c = Cabecera-Valida $p
        if ($c) { return @{ ok = $false; why = "${name}: $c" } }
    }
    return @{ ok = $true; manifest = $m }
}

function Ranura-Permitida([int]$n) { return ($n -ge 0 -and $n -le 2) -or ($n -ge 100 -and $n -le 108) }

# ------------------------------------------------------------------ 1. el juego cerrado

if (Get-Process -Name 'CrimsonDesert' -ErrorAction SilentlyContinue) {
    Fallo "Crimson Desert esta abierto. Cierralo del todo (tambien de la pantalla de titulo) y vuelve a ejecutar este script."
}

# ------------------------------------------------------------------ 2. la generacion

$progressRoot = Join-Path $local 'cdmp\guest_progress'
$hostDir = $null
if ($Progreso) {
    $incoming = $Progreso
} else {
    if (-not (Test-Path -LiteralPath $progressRoot)) { Fallo "no existe $progressRoot (el anfitrion aun no ha devuelto ninguna generacion)." }
    if ($Anfitrion) {
        $hostDir = Join-Path $progressRoot $Anfitrion
    } else {
        $hosts = @(Get-ChildItem -LiteralPath $progressRoot -Directory)
        if ($hosts.Count -ne 1) {
            Write-Host "Carpetas de anfitrion en ${progressRoot}:"
            $hosts | ForEach-Object { Write-Host "  $($_.Name)" }
            Fallo "hay $($hosts.Count) carpetas de anfitrion; elige una con -Anfitrion <SteamID64>."
        }
        $hostDir = $hosts[0].FullName
    }
    $incoming = Join-Path $hostDir 'incoming'
}
if (-not (Test-Path -LiteralPath $incoming)) { Fallo "no existe $incoming." }

# La mas nueva por la HORA de su manifest (time_unix del anfitrion), no por el numero: si el buzon del anfitrion se
# vacio, la numeracion pudo empezar de nuevo. A igual hora, el numero mayor.
function Hora-Generacion($dir) {
    $mf = Join-Path $dir.FullName 'manifest.txt'
    if (-not (Test-Path -LiteralPath $mf)) { return [uint64]0 }
    $m = Leer-Manifest $mf
    $h = [uint64]0
    if ($m['time_unix'] -and [uint64]::TryParse($m['time_unix'], [ref]$h)) { return $h }
    return [uint64]0
}
$gens = @(Get-ChildItem -LiteralPath $incoming -Directory | Where-Object { $_.Name -match '^gen[1-9][0-9]*$' } |
          Sort-Object @{ Expression = { Hora-Generacion $_ }; Descending = $true },
                      @{ Expression = { [uint64]$_.Name.Substring(3) }; Descending = $true })
if ($gens.Count -eq 0) { Fallo "no hay ninguna generacion en $incoming." }

# Todas se verifican: las lineas (abajo) se forman con las validas.
$validas = @()
foreach ($g in $gens) {
    $v = Verificar-Generacion $g.FullName
    if ($v.ok) {
        $validas += [pscustomobject]@{ dir = $g; manifest = $v.manifest }
    } else {
        Write-Host "  $($g.Name): NO valida ($($v.why))" -ForegroundColor Yellow
    }
}

# ------------------------------------------------------------------ 3. la carpeta de partidas

$saveRoot = if ($CarpetaPartidas) { $CarpetaPartidas } else { Join-Path $local 'Pearl Abyss\CD\save' }
if (-not (Test-Path -LiteralPath $saveRoot)) { Fallo "no existe la carpeta de partidas $saveRoot." }
if ($Cuenta) {
    $accountDir = Join-Path $saveRoot $Cuenta
} else {
    $accounts = @(Get-ChildItem -LiteralPath $saveRoot -Directory | Where-Object { $_.Name -match '^[0-9]+$' })
    if ($accounts.Count -ne 1) { Fallo "hay $($accounts.Count) carpetas de cuenta en $saveRoot; elige una con -Cuenta <id>." }
    $accountDir = $accounts[0].FullName
}
if (-not (Test-Path -LiteralPath $accountDir)) { Fallo "no existe $accountDir." }

# ------------------------------------------------------------------ 4. la linea (sesion 25)

# Cada generacion continua UNA partida: su linea, que el anfitrion nombra con lineage_root (el SHA-256 del save.save
# del que salio la primera sesion). Una generacion solo se aplica a una ranura que tenga una partida de su linea (la
# raiz, una generacion o la base de una generacion: lineage_base, el save.save que se subio en esa sesion), o que no
# tenga partida. Asi el progreso de una partida nunca pisa otra distinta (sesion 25: el progreso de la de pruebas
# llego marcado para las ranuras 0 y 1, que tenian otra partida mas avanzada).
function Hex-Valido($h) { return ($h -is [string]) -and ($h.ToLowerInvariant() -match '^[0-9a-f]{64}$') }
function Hora($c) {
    $h = [uint64]0
    if ($c.manifest['time_unix'] -and [uint64]::TryParse($c.manifest['time_unix'], [ref]$h)) { return $h }
    return [uint64]0
}
$miembros = @{}
foreach ($c in $validas) {
    $root = $c.manifest['lineage_root']
    if (-not (Hex-Valido $root)) { continue }
    $root = $root.ToLowerInvariant()
    if (-not $miembros.ContainsKey($root)) { $miembros[$root] = @{ $root = $true } }
    $miembros[$root][$c.manifest.files['save.save']['sha256'].ToLowerInvariant()] = $true
    if (Hex-Valido $c.manifest['lineage_base']) { $miembros[$root][$c.manifest['lineage_base'].ToLowerInvariant()] = $true }
}
# Lo ultimo aplicado (applied.txt) tambien es de su linea, aunque su generacion ya no este en incoming.
if ($hostDir -and (Test-Path -LiteralPath (Join-Path $hostDir 'applied.txt'))) {
    $ap = Leer-Manifest (Join-Path $hostDir 'applied.txt')
    if ((Hex-Valido $ap['lineage_root']) -and (Hex-Valido $ap['save_sha256'])) {
        $r = $ap['lineage_root'].ToLowerInvariant()
        if (-not $miembros.ContainsKey($r)) { $miembros[$r] = @{ $r = $true } }
        $miembros[$r][$ap['save_sha256'].ToLowerInvariant()] = $true
    }
}

# Por que no se puede aplicar $c a la ranura $n ($null = se puede).
function Motivo-Linea($c, [int]$n) {
    if ($n -eq 3) { return "la ranura 3 es la de CrossSave (guardarla la sube a Pearl Abyss): nunca se aplica" }
    if ($n -eq 206) { return "la ranura 206 se borra tras cargarla: nunca se aplica" }
    if (-not (Ranura-Permitida $n)) { return "la ranura $n no es 0-2 ni 100-108" }
    $root = $c.manifest['lineage_root']
    if (-not $root) {
        return "no dice de que partida sale (sin lineage_root: version del mod anterior al 12-10); nunca se aplica, borrala"
    }
    if (-not (Hex-Valido $root)) { return "su lineage_root no es un SHA-256" }
    $root = $root.ToLowerInvariant()
    $sv = Join-Path (Join-Path $accountDir "slot$n") 'save.save'
    if (-not (Test-Path -LiteralPath $sv)) { return $null }
    $actual = Sha256 $sv
    if (-not $miembros[$root].ContainsKey($actual)) {
        return ("la ranura $n tiene OTRA partida (save.save {0}...), que no es la de esta generacion ni una de sus " +
                "generaciones (linea {1}...)") -f $actual.Substring(0, 16), $root.Substring(0, 16)
    }
    # Nunca hacia atras dentro de la linea: si la ranura tiene una generacion de la linea, solo una mas nueva; si tiene
    # la base de unas generaciones, solo esas o mas nuevas (la hora es la del anfitrion, la de su manifest).
    if ($c.manifest.files['save.save']['sha256'].ToLowerInvariant() -eq $actual) { return "la ranura $n ya la tiene" }
    $hc = Hora $c
    $cb = if (Hex-Valido $c.manifest['lineage_base']) { $c.manifest['lineage_base'].ToLowerInvariant() } else { '' }
    $tmin = $null
    $desde = ''
    foreach ($o in $validas) {
        if ($o.manifest['lineage_root'] -ne $c.manifest['lineage_root']) { continue }
        if ($o.manifest.files['save.save']['sha256'].ToLowerInvariant() -eq $actual -and (Hora $o) -ge $hc) {
            return "la ranura $n ya tiene una generacion igual o mas nueva de esta partida ($($o.dir.Name))"
        }
        if ((Hex-Valido $o.manifest['lineage_base']) -and $o.manifest['lineage_base'].ToLowerInvariant() -eq $actual -and
            ($null -eq $tmin -or (Hora $o) -lt $tmin)) {
            $tmin = Hora $o
            $desde = $o.dir.Name
        }
    }
    # La ranura tiene la partida de la que salieron unas generaciones: las de antes de ellas son mas viejas.
    if ($null -ne $tmin -and $cb -ne $actual -and $hc -lt $tmin) {
        return "es mas vieja que la partida de la ranura $n (de ella sale $desde)"
    }
    return $null
}

$elegida = $null
foreach ($c in $validas) {
    if ($Generacion -gt 0 -and $c.dir.Name -ne "gen$Generacion") { continue }
    $n = if ($Ranura -ge 0) { $Ranura } elseif ($c.manifest['slot'] -match '^[0-9]+$') { [int]$c.manifest['slot'] } else { -1 }
    $motivo = Motivo-Linea $c $n
    if (-not $motivo) { $elegida = $c; $slot = $n; break }
    Write-Host "  $($c.dir.Name) (para la ranura $n): no se aplica: $motivo" -ForegroundColor Yellow
    if ($Generacion -gt 0) { Fallo "gen${Generacion}: $motivo." }
}
if (-not $elegida) {
    Fallo "ninguna generacion de $incoming se puede aplicar$(if ($Generacion -gt 0) { " (gen$Generacion no existe o no es valida)" })."
}
$manifest = $elegida.manifest
$genNum = [int]$elegida.dir.Name.Substring(3)
Write-Host "Generacion: $($elegida.dir.FullName) (gen$genNum, motivo $($manifest['reason_text']), recibida $($manifest['received_utc']), linea $($manifest['lineage_root'].Substring(0, 16))...)"

$target = Join-Path $accountDir "slot$slot"
foreach ($bad in "slot${slot}_new", "slot${slot}_backup") {
    if (Test-Path -LiteralPath (Join-Path $accountDir $bad)) {
        Fallo "existe $bad en ${accountDir}: un guardado se interrumpio. Carga esa partida una vez en solitario para que el juego la repare, cierralo y vuelve a probar."
    }
}
# Una ranura del juego solo tiene ficheros; con subcarpetas, la copia de seguridad y la preparacion (solo ficheros) no
# las guardarian y el cambio las quitaria: se niega.
if ((Test-Path -LiteralPath $target) -and @(Get-ChildItem -LiteralPath $target -Directory).Count -gt 0) {
    Fallo "$target tiene subcarpetas (no es una ranura normal del juego): no se toca."
}
Write-Host "Destino: $target"

# La preparacion, en el MISMO volumen que la carpeta de partidas (los cambios son renombrados).
$stagingRoot = Join-Path $local 'cdmp\staging'
if ([System.IO.Path]::GetPathRoot($stagingRoot) -ne [System.IO.Path]::GetPathRoot($accountDir)) {
    $stagingRoot = Join-Path (Split-Path $saveRoot -Parent) 'cdmp_guest_apply_staging'
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backupRoot = Join-Path $local 'cdmp\guest_backups'
$backup = Join-Path $backupRoot "${stamp}_slot$slot"
# Dos aplicaciones en el mismo segundo nunca comparten la copia (la segunda pisaria la primera).
while (Test-Path -LiteralPath $backup) {
    Start-Sleep -Seconds 1
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $backup = Join-Path $backupRoot "${stamp}_slot$slot"
}
$staged = Join-Path $stagingRoot "slot$slot"
$old = Join-Path $stagingRoot "slot${slot}_old_$stamp"

if ($SoloComprobar) {
    Write-Host ""
    Write-Host "SOLO COMPROBAR: la generacion gen$genNum es valida. Se haria:" -ForegroundColor Green
    Write-Host "  copia de seguridad: $target -> $backup"
    Write-Host "  preparacion:        $staged"
    Write-Host "  cambio:             $target -> $old ; $staged -> $target"
    exit 0
}

# ------------------------------------------------------------------ 5. copia de seguridad

New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
if (Test-Path -LiteralPath $target) {
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    foreach ($f in Get-ChildItem -LiteralPath $target -File) {
        Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $backup $f.Name)
        if ((Sha256 $f.FullName) -ne (Sha256 (Join-Path $backup $f.Name))) { Fallo "la copia de seguridad de $($f.Name) no coincide." }
    }
    Write-Host "Copia de seguridad: $backup ($(@(Get-ChildItem -LiteralPath $backup -File).Count) ficheros, verificada)"
} else {
    Write-Host "La ranura slot$slot no existia: no hay nada que copiar."
}
# Las 10 copias mas nuevas se quedan.
$copias = @(Get-ChildItem -LiteralPath $backupRoot -Directory | Where-Object { $_.Name -match '^\d{8}-\d{6}_slot\d+$' } | Sort-Object Name -Descending)
if ($copias.Count -gt 10) {
    foreach ($c in $copias[10..($copias.Count - 1)]) { Remove-Item -LiteralPath $c.FullName -Recurse -Force }
}

# ------------------------------------------------------------------ 6. preparacion y cambio

New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null
if (Test-Path -LiteralPath $staged) { Remove-Item -LiteralPath $staged -Recurse -Force }
New-Item -ItemType Directory -Force -Path $staged | Out-Null
if (Test-Path -LiteralPath $target) {
    # Los demas ficheros de la ranura (si los hay) se conservan; lobby.save y save.save son los de la generacion.
    foreach ($f in Get-ChildItem -LiteralPath $target -File) { Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $staged $f.Name) }
}
foreach ($name in 'lobby.save', 'save.save') {
    Copy-Item -LiteralPath (Join-Path $elegida.dir.FullName $name) -Destination (Join-Path $staged $name) -Force
    if ((Sha256 (Join-Path $staged $name)) -ne $manifest.files[$name]['sha256'].ToLowerInvariant()) {
        Remove-Item -LiteralPath $staged -Recurse -Force
        Fallo "la copia preparada de $name no coincide con el manifest."
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
            throw "el $name aplicado no coincide con el manifest"
        }
    }
} catch {
    $err = $_.Exception.Message
    # Vuelta atras: la ranura como estaba (la aplicada fallida queda aparte en la preparacion).
    try {
        if ($movedNew) {
            Move-Item -LiteralPath $target -Destination (Join-Path $stagingRoot "slot${slot}_fallida_$stamp")
        }
        if ($movedOld) { Move-Item -LiteralPath $old -Destination $target }
    } catch {
        Write-Host "ERROR GRAVE en la vuelta atras: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host "Restaura a mano $target desde la copia de seguridad $backup." -ForegroundColor Red
        exit 2
    }
    Write-Host ""
    Write-Host "ERROR al aplicar: $err" -ForegroundColor Red
    Write-Host "La ranura se ha devuelto a como estaba; la copia de seguridad sigue en $backup." -ForegroundColor Red
    exit 1
}
if ($movedOld) { Remove-Item -LiteralPath $old -Recurse -Force }

# El registro de lo aplicado (el mod lo manda como base_gen en su proxima oferta: el anfitrion no le devuelve nada
# mas viejo).
if ($hostDir) {
    $lines = @(
        "generation=$genNum",
        "slot=$slot",
        "applied_utc=$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))",
        "source=$($elegida.dir.FullName)",
        "backup=$backup",
        "lineage_root=$($manifest['lineage_root'])",
        "lobby_sha256=$($manifest.files['lobby.save']['sha256'])",
        "save_sha256=$($manifest.files['save.save']['sha256'])"
    )
    [System.IO.File]::WriteAllLines((Join-Path $hostDir 'applied.txt'), $lines)
}

Write-Host ""
Write-Host "HECHO: gen$genNum aplicada a $target (SHA-256 comprobado)." -ForegroundColor Green
Write-Host "Copia de seguridad de lo que habia: $backup"
Write-Host "Carga la partida en solitario para comprobarla. Al cerrar el juego, Steam Cloud subira esa ranura."
