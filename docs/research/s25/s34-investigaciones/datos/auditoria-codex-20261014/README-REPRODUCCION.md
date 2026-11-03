# Reproducción de los scripts propios de la auditoría S34

Este documento inventaría los **33 scripts propios** de `a1–a5`, `revision-consolidacion` y `consolidacion`. Se preparó leyendo únicamente sus fuentes y nombres de auxiliares; no se volvieron a ejecutar las auditorías, no se consultaron informes ajenos y no se modificó ningún script original. Los comandos siguientes son instrucciones para una reproducción futura. **`a5-guardado/finalize_report.py` queda excluido**: edita un informe, no reproduce cifras.

## 1. Entorno, alcance y límites

- Paquete publicado: `C:\Emulador CD-npc-s33-fix\docs\research\s25\s34-investigaciones\datos\auditoria-codex-20261014`.
- Raíz histórica incrustada en algunos scripts: `%TEMP%\codex-s34-auditoria-20261014`. No hace falta que sobreviva esa carpeta temporal: el ejemplo distingue `SourceRoot` publicado de `OriginalRoot` histórico.
- Python observado: **3.14.6, Windows x64**. Intérprete existente: `C:\Emulador CD\build\research\venv\Scripts\python.exe`.
- Única biblioteca Python externa usada por estos scripts: **Capstone**; versión instalada comprobada: **5.0.9**. Los demás imports son de la biblioteca estándar. No requieren `pefile`.
- Ejecutar Python siempre con **`-I -B`**. No hace falta compilar el proyecto, arrancar juego/peer/host, cargar DLL, usar depurador ni leer partidas.
- Código fijado: `d835806c46da1cc3ec9ad3e5367dc0d1feb95db9`. La instantánea debe contener `src/` y `tests/` bajo `codigo-d835806/`; los scripts que la usan leen fuentes concretas, no el HEAD de trabajo.
- EXE esperado: build 1.0.0.2976, timestamp PE **`0x6AB28F00`**, ImageBase **`0x140000000`**. Usar la copia autorizada de esa build. No sustituirla por una actualización y comparar las cifras como si fueran las mismas fuentes.
- Varios scripts cargan el EXE o capturas completas en memoria; usar Python de 64 bits y espacio suficiente para entradas y resultados. No se ha establecido un mínimo universal de RAM ni compatibilidad de estos scripts con otras versiones de Python/Capstone.
- Las rutas de datos primarios están fijadas en el código. Cambiar el directorio de trabajo o copiar las carpetas **no las actualiza automáticamente**.

Comprobación de entorno, sin ejecutar una auditoría:

```powershell
$Py = 'C:\Emulador CD\build\research\venv\Scripts\python.exe'
& $Py -I -B -c "import sys, importlib.metadata; print(sys.version); print(importlib.metadata.version('capstone'))"
```

Si se reconstruye el entorno en otra máquina, instalar Capstone 5.0.9 en un venv propio compatible; esa instalación no se realizó al preparar este documento. No instalar paquetes ni actualizar el entorno original para reproducirlo.

## 2. Datos externos necesarios

`L = C:\Emulador CD\backups\p8-sesion34`. Las rutas de esta tabla son relativas a `L` y se deben conservar literalmente, o cambiar de manera explícita **en las copias** de los scripts.

| Ensayo | Log anfitrión | Log invitado | Captura anfitrión | Captura invitado |
|---|---|---|---|---|
| A | `cdmp_20261014-180308.log` | `shadow\cdmp_20261014-170438.log` | `cdmp_packets_20261014-180319.bin` | `shadow\cdmp_packets_20261014-170455.bin` |
| B | `paso1b\cdmp_20261014-184959.log` | `shadow-p1b-p2b\cdmp_20261014-175039.log` | `paso1b\cdmp_packets_20261014-185010.bin` | `shadow-p1b-p2b\cdmp_packets_20261014-175057.bin` |
| C | `paso2b\cdmp_20261014-190232.log` | `shadow-p1b-p2b\cdmp_20261014-180502.log` | `paso2b\cdmp_packets_20261014-190242.bin` | `shadow-p1b-p2b\cdmp_packets_20261014-180517.bin` |

Además:

- `L\shadow\cdmp_crash_20261014-170638_1704_1.dmp` (A).
- `L\shadow-p1b-p2b\cdmp_crash_20261014-180710_6364_1.dmp` (C).
- `D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe` — únicamente lectura del EXE; ningún otro archivo de `bin64` es necesario.
- `codigo-d835806\src\mod\session2\guest_save_host.cpp` para a5; `npc_action_probe.cpp`, `host_scene_diag.cpp` y `docked_gimmicks_logic.h` para `a4/pe-detalle.py`; `npc_action_probe.cpp` para `consolidacion/verificar_transversal.py`.
- Solo el manifiesto documental de `consolidacion/inventario_fuentes.py` consulta `C:\Emulador CD-npc-s33-fix\docs\research\s25\s34-investigaciones\*.md`. Lee bytes para hashes; excluye exactamente el nombre `AUDITORIA.md` sin distinguir mayúsculas. No interpreta dictámenes. Para repetir ese manifiesto, mantener una copia de las fuentes documentales originales del momento de auditoría (HEAD documental observado `f79545c0f18f933ca0f9e185f60455b1bcc38501`) y apuntar `REPORTS` allí en la copia del script. El glob puede producir un manifiesto vacío si falta la carpeta: no confundir eso con una reproducción completa. No requiere leer ni copiar los informes de auditoría a1–a5.

No hacen falta los scripts del equipo autor de las investigaciones. Estos lectores decodifican directamente logs, `.bin`, `.dmp` y PE. Tampoco hacen falta partidas reales, credenciales, Steam, Cloud ni red para recalcular las cifras.

## 3. Preparar una copia de trabajo sin cambiar originales

Los bloques de esta sección **no se ejecutaron** al crear el README. Trabajar en una raíz nueva y estable; no usar `$env:TEMP`, cuyo valor puede variar entre procesos sandbox. La copia de trabajo debe tener esta estructura:

```text
<RunRoot>/
  a1-cierres/                  scripts propios
  a2-familias/                 scripts propios
  a3-viajeros/                 scripts propios
  a4-contencion/               scripts propios
  a5-guardado/                 scripts propios, sin finalize_report.py
  revision-consolidacion/      scripts propios
  consolidacion/               scripts propios
  codigo-d835806/
    src/
    tests/
```

Portabilidad real por grupo:

| Grupo | Salidas/auxiliares internos | Qué hay que reubicar |
|---|---|---|
| a1, a2, a3 | Usan `__file__` para directorio de script y auxiliares. | Se pueden mover juntos; las rutas externas de logs/capturas/EXE permanecen fijas. |
| a4 | `OUT` absoluto bajo la raíz original; `pe-detalle.py` también fija `codigo-d835806`. | Reemplazar el prefijo de raíz original en **copias** de los seis scripts. Regenerar los JSON, que contienen rutas. |
| a5 | `OUT`/`p`, fuente C++ y `exec(open(...))` absolutos. | Reemplazar el prefijo interno en los siete scripts; conservar los scripts base junto a sus consumidores. |
| revisión | `ROOT` absoluto bajo `revision-consolidacion`. | Reemplazar el prefijo interno en ambas copias. |
| consolidación | `BASE/OUT` por `__file__`; snapshot por defecto hermano de `BASE`. | Mantener estructura; `verificar_transversal.py --codigo <ruta>` permite cambiar explícitamente la instantánea. Los datos externos y `REPORTS` siguen fijos. |

Ejemplo de preparación desde el paquete publicado: crea otra raíz, copia los `.py`, conserva los cuatro documentos propios que necesita la revisión final y reubica **solo el prefijo interno histórico** en las copias de scripts. No modifica originales ni ejecuta auditorías. `SourceRoot` es la ubicación actual del paquete; `OriginalRoot` es el literal TEMP que aparece en el código. Reemplazar solo `SourceRoot` no corregiría esos literales. No es un reubicador universal de rutas.

```powershell
$SourceRoot = 'C:\Emulador CD-npc-s33-fix\docs\research\s25\s34-investigaciones\datos\auditoria-codex-20261014'
$OriginalRoot = '%TEMP%\codex-s34-auditoria-20261014'
$RunRoot = '%TEMP%\codex-s34-reproduccion-20261014'
$AuditDirs = @('a1-cierres','a2-familias','a3-viajeros','a4-contencion','a5-guardado','revision-consolidacion','consolidacion')
if (Test-Path -LiteralPath $RunRoot) { throw 'Elegir una raíz nueva para no sobrescribir resultados.' }
New-Item -ItemType Directory -Path $RunRoot | Out-Null
$Utf8 = New-Object System.Text.UTF8Encoding($false)
foreach ($dir in $AuditDirs) {
    $dest = Join-Path $RunRoot $dir
    New-Item -ItemType Directory -Path $dest | Out-Null
    foreach ($src in Get-ChildItem -LiteralPath (Join-Path $SourceRoot $dir) -Filter '*.py' -File) {
        if ($src.Name -eq 'finalize_report.py') { continue }
        $text = [System.IO.File]::ReadAllText($src.FullName)
        $text = $text.Replace($OriginalRoot, $RunRoot)
        [System.IO.File]::WriteAllText((Join-Path $dest $src.Name), $text, $Utf8)
    }
}
$ReviewedDocs = @('consolidacion\AUDITORIA.md','consolidacion\seccion-9.md','a4-contencion\INFORME-A4.md','a5-guardado\AUDITORIA-5.md')
foreach ($relative in $ReviewedDocs) {
    Copy-Item -LiteralPath (Join-Path $SourceRoot $relative) -Destination (Join-Path $RunRoot $relative) -ErrorAction Stop
}
# Si el paquete conserva la instantánea, copiarla; de lo contrario usar git archive abajo.
$PublishedCode = Join-Path $SourceRoot 'codigo-d835806'
if (Test-Path -LiteralPath $PublishedCode) {
    Copy-Item -LiteralPath $PublishedCode -Destination (Join-Path $RunRoot 'codigo-d835806') -Recurse
} else {
    Write-Output 'Falta materializar codigo-d835806 mediante git archive antes de ejecutar sus consumidores.'
}
```

Revisar antes de ejecutar: `rg -n -F $OriginalRoot $RunRoot -g '*.py'` no debería encontrar dependencias internas antiguas. La ausencia de coincidencias no valida rutas externas, archivos faltantes ni equivalencia de datos. Si se cambia la ubicación de `L`, del EXE o de los documentos fuente, adaptar explícitamente sus constantes en las copias; mantener un registro de esas sustituciones. Evitar aliases/junctions hacia originales para “arreglar” salidas.

Si no se conservó `codigo-d835806`, se puede reconstruir sin checkout ni modificación de ramas usando `git archive` desde un repositorio que contenga el commit:

```powershell
$Repo = 'C:\Emulador CD-npc-s33-fix'
$Archive = Join-Path $RunRoot 'codigo-d835806.zip'
$CodeDir = Join-Path $RunRoot 'codigo-d835806'
if (Test-Path -LiteralPath $CodeDir) { throw 'La instantánea ya existe; verificarla en lugar de sobrescribirla.' }
& git -C $Repo archive --format=zip "--output=$Archive" d835806c46da1cc3ec9ad3e5367dc0d1feb95db9 src tests
if ($LASTEXITCODE -ne 0) { throw 'Falló git archive; no continuar.' }
Expand-Archive -LiteralPath $Archive -DestinationPath $CodeDir
```

Usar **una** vía: copiar la instantánea conservada o extraerla. No hace falta ejecutar ambas. No sustituirla por archivos actuales del worktree. `git archive` requiere Git y acceso de lectura al objeto; los scripts de análisis no requieren Git después de preparar el snapshot.

## 4. Inventario de los 33 scripts

Las entradas externas se refieren a §2. El auxiliar de revisión final también necesita cuatro documentos propios de la entrega, indicados abajo. “stdout” significa salida de consola, no un archivo creado por el script. Todos carecen de argumentos de línea de comandos salvo `consolidacion/verificar_transversal.py`, que ofrece `--codigo`.

### a1-cierres — 3

| Script | Entradas / dependencia | Salidas y finalidad |
|---|---|---|
| `audit_captures.py` | Seis capturas y tres logs host; biblioteca estándar. | `capturas-independientes.json` + stdout: altas/familias, restore, acciones, huérfanos, pérdidas, latencias2223 y contadores timeout. |
| `audit_dumps.py` | EXE y ambos `.dmp`; biblioteca estándar. | `dumps-independientes.json` + stdout: excepción, registros, memoria y desenrollado mediante pdata propio. |
| `audit_static.py` | EXE; importa dinámicamente el `audit_dumps.py` hermano para su clase PE; Capstone. | `estatico-independiente.json` + stdout: desensamblado, límites, firmas y referencias. El `main` del lector de dumps está protegido: importar no requiere generar primero su JSON. |

Los tres se pueden ejecutar independientemente, conservando juntos sus archivos fuente.

### a2-familias — 8

| Script | Entradas / dependencia | Salidas y finalidad |
|---|---|---|
| `censo_independiente.py` | Tres capturas guest y tres logs host; biblioteca estándar. | `A-censo.json`, `B-censo.json`, `C-censo.json` + stdout. Las rutas de logs guest se incluyen como metadatos, sin leerlos en este paso. |
| `log_independiente.py` | Tres logs guest y tres censos JSON; obtiene constantes con `runpy` del censo hermano. | `censo-log-vs-bin.json` + stdout: contraste log/captura. **No regenera los censos**: su productor solo procesa bajo `__main__`. |
| `resumen_evidencia.py` | Tres censos JSON. | Solo stdout: familias/miembros/offsets/tiempos/timeout/nacimientos seleccionados. |
| `latencia2223.py` | Tres censos JSON. | Solo stdout: separación de nacimientos y primera2223 desde último miembro de familias tipo6. |
| `estatico_independiente.py` | EXE; Capstone. | `estatico.json`, `estatico-disasm.txt` + stdout: PE/pdata/firmas/ramas/operandos. |
| `rtti_independiente.py` | Ejecuta `estatico_independiente.py` mediante `runpy`; Capstone indirecto. | `rtti.json` + stdout y regenera ambos resultados estáticos. No lee `estatico.json` como entrada. |
| `resumen_estatico.py` | `estatico-disasm.txt`. | Solo stdout: selección de funciones/instrucciones. |
| `contraejemplos_diseno.py` | Datos sintéticos embebidos; biblioteca estándar. | `contraejemplos_diseno.json` + stdout; modelos/assertions de diseño, no datos del juego ejecutándose. |

Primero censo, luego sus tres consumidores. Rama estática: generador o RTTI → resumen. RTTI vuelve a ejecutar el generador. No requiere los snapshots auxiliares `d835806-*.cpp/.h` de esa carpeta; quedan fuera de esta reproducción.

### a3-viajeros — 4

| Script | Entradas / dependencia | Salidas y finalidad |
|---|---|---|
| `auditar_viajeros.py` | Cuatro capturas H/G de B/C; biblioteca estándar. | `recuentos.json`, `cohortes-evidencias.json` + stdout: viajeros, movimientos, peticiones carretera, bajas/cohortes/latencias. |
| `auditar_pe.py` | EXE; Capstone. | `estatico.json` + stdout: PE/funciones/parche/firma/escrituras seleccionadas. |
| `comprobar_extra.py` | Ejecuta los dos anteriores mediante `runpy`; también logs host B/C. | `comprobaciones-extra.json` + stdout; regenera las tres salidas base. Capstone indirecto. |
| `comprobar_ventana.py` | Ejecuta `auditar_viajeros.py` mediante `runpy`. | `posiciones-tras-alta.json` + stdout; regenera resultados de viajeros. |

Los consumidores usan variables resultantes de `runpy`, no leen los JSON base. No ejecutarlos simultáneamente: comparten salidas. El `ghidra-0x28981b0.c` conservado es apoyo de revisión, **ninguno de estos scripts lo abre**.

### a4-contencion — 6

| Script | Entradas / dependencia | Salidas y finalidad |
|---|---|---|
| `recuento.py` | Seis logs H/G de A/B/C; biblioteca estándar. | `recuentos.json` + stdout: emisor/aplicación/latencias, alineación QPC, censura y relevo. |
| `capturas.py` | `recuentos.json`; capturas guest B/C; logs guest desde rutas del JSON. | `capturas.json` + stdout: integridad, altas/bajas y ventanas de llegada/salida/reaparición. |
| `detalle.py` | `recuentos.json` y log guest B desde el JSON. | Solo stdout: ventanas, aplicaciones no observadas y anchuras de stages. |
| `familias-tardios.py` | `recuentos.json`; logs host desde sus rutas. | `familias-tardios.json` + stdout: hijos tipo6 y acciones tardías. |
| `pe.py` | EXE; Capstone. | `pe.json` + stdout: PE/pdata/límites/firmas/candidatos de ramas. |
| `pe-detalle.py` | `pe.json`, EXE según `file` del JSON, snapshot C++ fijado; Capstone. | `pe-detalle.json` + stdout: valida límites de fuentes de ramas y firmas completas declaradas en código. |

Primero recuento → tres consumidores. Separadamente pe → pe-detalle. `detalle.txt` y `pe.txt` son capturas de stdout históricas, no entradas de otros scripts.

### a5-guardado — 7

| Script | Entradas / dependencia | Salidas y finalidad |
|---|---|---|
| `audit_logs.py` | Tres logs host; biblioteca estándar. | `logs.json`, `log-A/B/C-extract.txt` + stdout: llamadas, redirects, correlación por TID y hashes. |
| `audit_log_coverage.py` | Ejecuta por ruta absoluta `audit_logs.py`; relee logs. | Amplía/sobrescribe `logs.json` con cobertura/último resumen; regenera extractos; stdout. No ejecutar luego el base si se quiere conservar la versión ampliada. |
| `summarize_sequence.py` | `logs.json`. | Solo stdout: aperturas/renombrados con líneas originales. |
| `audit_pe.py` | EXE, `guest_save_host.cpp` del snapshot; Capstone. | `pe-check.json` + stdout: secciones/pdata/bytes/firmas completas/llamadas directas. |
| `audit_extra.py` | Ejecuta por ruta absoluta `audit_pe.py`; Capstone indirecto. | `extra-bytes.json` + stdout; regenera `pe-check.json`. |
| `audit_branches.py` | Ejecuta por ruta absoluta `audit_pe.py`; Capstone indirecto. | `branch-check.json` + stdout; regenera `pe-check.json`. |
| `audit_models.py` | Datos sintéticos embebidos; biblioteca estándar. | `counterexamples.json` + stdout: predicado de cabecera, pareja mezclada y recovery. No ejecuta código del juego ni valida partidas reales. |

Ejecutar coverage → resumen; pe/extra/branches secuencialmente. `log-summary.txt` fue una captura de stdout de coverage, no una entrada. Decompilados `decomp-*.txt` son apoyo manual de lectura y no los abren estos siete scripts.

### revision-consolidacion — 3

| Script | Entradas / dependencia | Salidas y finalidad |
|---|---|---|
| `verificar_primarias.py` | Dump C y EXE; biblioteca estándar. | `verificaciones-primarias.json` + stdout: cadena de memoria seleccionada y bytes primarios. |
| `verificar_thunk.py` | EXE; Capstone. | `thunk-logout.json` + stdout: firma, pdata/vecinos y candidatos de ramas al bloque, con alcance explícito del scan. |
| `verificar_revision_final.py` | Captura guest B, lectura de 64 B en offset 146811672; cuatro documentos propios por rutas relativas bajo la raíz absoluta. Biblioteca estándar. | `verificacion-final.json` + stdout: registro CDPK/actor seleccionado y SHA-256 de los documentos revisados. |

`verificar_primarias.py` y `verificar_thunk.py` son independientes y no leen informes. `verificar_revision_final.py` sí lee los bytes de estos cuatro documentos **para hashes**, sin interpretarlos:

- `consolidacion/AUDITORIA.md`
- `consolidacion/seccion-9.md`
- `a4-contencion/INFORME-A4.md`
- `a5-guardado/AUDITORIA-5.md`

Para ejecutar ese auxiliar, colocar las copias idénticas entregadas en esas rutas de `RunRoot`; no sustituirlas por otros borradores ni por el informe publicado en otra ubicación. La preparación de §3 copia expresamente estos cuatro documentos desde el paquete publicado; conservarlos sin cambios. Si se preparó la copia manualmente, colocarlos antes de ejecutar este auxiliar. El script no contiene hashes esperados: calcula los actuales; comparar con el `verificacion-final.json` conservado y el manifiesto de entrega.

El JSON final ya se regeneró después de las correcciones y el SHA-256 final de `consolidacion/AUDITORIA.md` es `ba04c53f37e51758c19e2926b106d378db64aa14e7a39a4407282dedb8e15db0`. No es el hash de un borrador. `REVISIÓN.md` conserva observaciones de un borrador que se corrigieron en el principal; `REVISIÓN-FINAL.md` cierra la revisión. Esos dos textos no son entradas de este auxiliar.

### consolidacion — 2

| Script | Entradas / dependencia | Salidas y finalidad |
|---|---|---|
| `inventario_fuentes.py` | Seis capturas; glob documental descrito en §2; biblioteca estándar. | `inventario-fuentes.json` + stdout: hashes, integridad CDPK, huecos/truncados y manifiesto documental. |
| `verificar_transversal.py` | Tres logs host, EXE, `npc_action_probe.cpp`; Capstone. `--codigo` opcional. | `verificacion-transversal.json` + stdout: recuentos de sonda, PE/pdata, prefijos de 32 B y firmas exactas del código señalado. |

Independientes; no leen los dictámenes de a1–a5. El prefijo de 32 B del transversal no sustituye las comprobaciones de firmas completas de a4/a5.

## 5. Comandos de reproducción, en orden seguro

Ejecutar **después** de preparar la copia y verificar sus rutas; no lanzar grupos con dependencias simultáneamente. Estos comandos enumeran los 32 lectores/modelos originales; el auxiliar 33 de revisión final figura aparte porque requiere documentos propios conservados. Algunos vuelven a generar resultados base. Para ahorrar trabajo se pueden omitir solo las llamadas redundantes identificadas en §4; no cambiar su orden a ciegas.

```powershell
$Py = 'C:\Emulador CD\build\research\venv\Scripts\python.exe'
$RunRoot = '%TEMP%\codex-s34-reproduccion-20261014'
function Invoke-Audit([string]$RelativePath) {
    & $Py -I -B (Join-Path $RunRoot $RelativePath)
    if ($LASTEXITCODE -ne 0) { throw "Falló $RelativePath; revisar entradas antes de continuar." }
}
Invoke-Audit 'a1-cierres\audit_captures.py'
Invoke-Audit 'a1-cierres\audit_dumps.py'
Invoke-Audit 'a1-cierres\audit_static.py'
Invoke-Audit 'a2-familias\censo_independiente.py'
Invoke-Audit 'a2-familias\log_independiente.py'
Invoke-Audit 'a2-familias\resumen_evidencia.py'
Invoke-Audit 'a2-familias\latencia2223.py'
Invoke-Audit 'a2-familias\estatico_independiente.py'
Invoke-Audit 'a2-familias\rtti_independiente.py'
Invoke-Audit 'a2-familias\resumen_estatico.py'
Invoke-Audit 'a2-familias\contraejemplos_diseno.py'
Invoke-Audit 'a3-viajeros\auditar_viajeros.py'
Invoke-Audit 'a3-viajeros\auditar_pe.py'
Invoke-Audit 'a3-viajeros\comprobar_extra.py'
Invoke-Audit 'a3-viajeros\comprobar_ventana.py'
Invoke-Audit 'a4-contencion\recuento.py'
Invoke-Audit 'a4-contencion\capturas.py'
Invoke-Audit 'a4-contencion\detalle.py'
Invoke-Audit 'a4-contencion\familias-tardios.py'
Invoke-Audit 'a4-contencion\pe.py'
Invoke-Audit 'a4-contencion\pe-detalle.py'
Invoke-Audit 'a5-guardado\audit_logs.py'
Invoke-Audit 'a5-guardado\audit_log_coverage.py'
Invoke-Audit 'a5-guardado\summarize_sequence.py'
Invoke-Audit 'a5-guardado\audit_pe.py'
Invoke-Audit 'a5-guardado\audit_extra.py'
Invoke-Audit 'a5-guardado\audit_branches.py'
Invoke-Audit 'a5-guardado\audit_models.py'
Invoke-Audit 'revision-consolidacion\verificar_primarias.py'
Invoke-Audit 'revision-consolidacion\verificar_thunk.py'
Invoke-Audit 'consolidacion\inventario_fuentes.py'
& $Py -I -B (Join-Path $RunRoot 'consolidacion\verificar_transversal.py') --codigo (Join-Path $RunRoot 'codigo-d835806')
if ($LASTEXITCODE -ne 0) { throw 'Falló verificar_transversal.py.' }
```

Una vez colocados los cuatro documentos propios indicados en §4, ejecutar el auxiliar 33:

```powershell
Invoke-Audit 'revision-consolidacion\verificar_revision_final.py'
```

Para conservar una salida que solo se imprime, ejecutarla por separado y redirigir a un archivo **nuevo de la copia**; por ejemplo:

```powershell
& $Py -I -B (Join-Path $RunRoot 'a2-familias\latencia2223.py') | Set-Content -LiteralPath (Join-Path $RunRoot 'a2-familias\stdout-latencia2223.txt') -Encoding UTF8
if ($LASTEXITCODE -ne 0) { throw 'Falló latencia2223.py.' }
```

Comparar campos numéricos, offsets, firmas y hashes de **entradas**; las rutas absolutas de los JSON cambiarán al reubicar. No exigir igualdad byte a byte de todo JSON con rutas nuevas. Un comando con código0 tampoco sustituye verificar cobertura, población y archivos esperados.

## 6. Auxiliares de lectura estática y Ghidra

No son necesarios para ejecutar los scripts numéricos, pero deben conservarse si se quiere repetir también el contraste manual de decompilados y referencias:

- a3: `ghidra-0x28981b0.c`.
- a5: `decomp-1329EB0.txt`, `decomp-186E450.txt`, `decomp-186F570.txt`, `decomp-187C490.txt`, `decomp-20688A0.txt`, `decomp-2358A20.txt`, `decomp-2358EF0.txt`, `decomp-235A800.txt`; lectura adicional original `D:\RE\CrimsonDesert_2976\exports\decomp\0x1871930.c`.

Recrear consultas Ghidra necesita la instalación/proyecto analizado de **esa build**, `D:\RE\CrimsonDesert_2976\scripts\cdmp-query.ps1`, su `_env.ps1`, scripts Java auxiliares y servidor/headless configurado. No basta copiar únicamente el wrapper. No se ejecutan scripts de análisis de los autores del estudio para obtener cifras; este wrapper se usa exclusivamente como interfaz de consulta estática de Ghidra.

Ejemplo **opcional y no ejecutado aquí**; VA absoluta = `0x140000000 + RVA`, no concatenar prefijos sin comprobarla:

```powershell
& powershell -NoProfile -ExecutionPolicy Bypass -File 'D:\RE\CrimsonDesert_2976\scripts\cdmp-query.ps1' decomp 0x14186E450 |
    Set-Content -LiteralPath (Join-Path $RunRoot 'a5-guardado\decomp-186E450.txt') -Encoding UTF8
```

El wrapper puede escribir su export decompilado y usar temporales aunque la consulta del programa sea de lectura; verificar esa configuración si la reproducción exige que todo artefacto permanezca en la copia. Ejecutar consultas **secuencialmente**: el modo headless bloquea el proyecto y los intentos simultáneos pueden devolver `LockException`. Buscar `ERROR`/`no devolvió resultado` en las salidas; no confiar solo en `$LASTEXITCODE`, porque el wrapper puede devolver un texto de error con exit0. La ruta TEMP del sandbox con llaves produjo fallos del wrapper en la sesión original; se resolvió con consulta de lectura escalada autorizada, no modificando políticas globales. No asumir que otra máquina tendrá el mismo permiso, proyecto o servidor.

## 7. Qué no se reproduce automáticamente

- La secuencia documentada reproduce parsers/modelos y resultados estáticos; no convierte hipótesis en pruebas de comportamiento en vivo.
- El A archivado acaba antes de los autoguardados host18:21/18:36. Reejecutar a5 no recupera ese tramo ni prueba un inventario histórico de partidas.
- Los modelos de a2/a5 son contraejemplos sintéticos, no registros del juego.
- El desenrollado de a1 usa su lector propio; conservar las advertencias/cobertura del resultado, sin equipararlo a una sesión nativa de depurador.
- La igualdad de un prefijo/firma en el EXE de disco no acredita código vivo ni destinos indirectos no examinados.
- Los decompilados dependen de la base Ghidra y sus símbolos/análisis; solo los bytes PE y lectores propios se recalculan con el EXE sin ese proyecto.
- Los dictámenes y la consolidación editorial no los genera ningún script de esta lista. No ejecutar `finalize_report.py` ni copiar resultados sobre informes para “reproducir” una conclusión.
