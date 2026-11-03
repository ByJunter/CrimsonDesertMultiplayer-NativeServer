# Evidencia reproducible S42

El dictamen vigente está en [s42-enemigos-puertas.md](../s42-enemigos-puertas.md). Son tres análisis primarios independientes y una revisión del código. Los informes de agentes conservan su evolución: un «pendiente» inicial puede quedar resuelto en un apartado posterior; gobierna el cierre y el documento principal. No confundir evidencia observada con reparación aprobada.

- `capturas/`: censo, cronología, offsets, payloads, alineación Shadow y hashes de originales. `manifiesto.json` conserva las rutas originales de producción de estos artefactos bajo `build/research/s42`; los archivos aquí son copias byte a byte.
- `estatico/`: RVA/sección/pdata/firmas, desensamblados y decompilados de inmunidad y serialización2147. El PE usado tiene SHA256 `57da440d72f4db974f25fef047cf84c4dadd999a88cb2a3c5af4c9bd67fde1e7`.
- `puertas/`: lector propio, recuento estricto por UUID, cronología candidata, hook verificado y Ghidra. La candidata no está identificada visualmente con certeza.
- `revision/`: revisión independiente con hallazgos corregidos y límite de aprobación.
- `compilacion/`: resultado final de `scripts/build.ps1` y hashes del binario/objetos. Los ensamblados completos quedan en `build/research/s42/compilacion/` para no versionar megabytes de código de soporte.
- `ini-check.json`: todas las claves de los INI base conservadas; sólo tres claves añadidas por INI. El nombre activo no cambia el alcance observacional.

## Reproducir

Ejecutar desde `C:\Emulador CD-enemigos` con los originales de `C:\Emulador CD\backups` presentes. No hace falta el juego abierto; no se le llama. Python: `C:\Emulador CD\build\research\venv\Scripts\python.exe -I -B`. Los scripts de capturas/estático mantienen rutas de salida absolutas del worktree; los de puertas usan su propia carpeta. Prepararlos en `build/research/s42` evita escribir resultados junto a documentación versionada:

```powershell
$areasS42 = @{'capturas'='enemigos-capturas'; 'estatico'='enemigos-estatico'; 'puertas'='puertas'}
foreach ($areaS42 in $areasS42.Keys) {
    $destS42 = Join-Path 'build/research/s42' $areasS42[$areaS42]
    New-Item -ItemType Directory -Force -Path $destS42 | Out-Null
    Get-ChildItem -LiteralPath "docs/research/s25/s42-datos/$areaS42" -Filter '*.py' |
        Copy-Item -Destination $destS42
}
Copy-Item -LiteralPath 'docs/research/s25/s42-datos/capturas/informe.md' -Destination 'build/research/s42/enemigos-capturas/informe.md'
$pyS42 = 'C:\Emulador CD\build\research\venv\Scripts\python.exe'
& $pyS42 -I -B build/research/s42/enemigos-capturas/analizar.py
& $pyS42 -I -B build/research/s42/enemigos-capturas/finalizar.py
& $pyS42 -I -B build/research/s42/enemigos-capturas/archivar.py
& $pyS42 -I -B build/research/s42/enemigos-capturas/alineacion.py
& $pyS42 -I -B build/research/s42/enemigos-estatico/inspeccionar.py
& $pyS42 -I -B build/research/s42/puertas/parse_capturas.py
& $pyS42 -I -B build/research/s42/puertas/resumir_candidata.py
& $pyS42 -I -B build/research/s42/puertas/verifica_hook.py
```

`analizar` genera cuatro JSON grandes fuera de Git; `finalizar` calcula `cohorte` y tabla; `archivar` conserva evidencia mínima; `alineacion` coteja payloads2922. No se versionan capturas originales. En puertas el resultado decisivo es `evidencia-resumen.json`, que vuelve a leer los `.bin` con longitud estricta; no sustituirlo por un mero conteo de subcadenas. `inspeccionar` usa el lector propio existente `cdpe.py` y lee únicamente el exe en disco.

Para reproducir la compilación: `.\scripts\build.ps1`. Los tests puros llaman helpers de producción y devuelven error sin depender de assert/NDEBUG. No ejecutan los detours en el juego ni acreditan apertura o IA. El manifiesto `archivos-sha256.json` identifica las copias archivadas (excluye su propio hash).
