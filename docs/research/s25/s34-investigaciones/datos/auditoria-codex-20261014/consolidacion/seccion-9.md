## 9. Evidencia propia y reproducción

Los cinco dictámenes independientes forman parte de esta auditoría; contienen tablas ampliadas, límites de cada lectura y más offsets que la síntesis. Se conservan junto con nuestros lectores, resultados y exports nuevos en [datos/auditoria-codex-20261014](datos/auditoria-codex-20261014/README-REPRODUCCION.md). Las capturas, volcados y ejecutable originales no se duplican ni se modifican.

| Área y cifra | Lector propio | Salida de referencia |
|---|---|---|
| Cierres: pila/actor/capa | `a1-cierres/audit_dumps.py` | `dumps-independientes.json`; desenrollado pdata/UNWIND_INFO, límites explícitos fuera del PE |
| Ocho entregas restore0 y familias recibidas | `a1-cierres/audit_captures.py` | `capturas-independientes.json`, offsets BIN absolutos |
| Cierres: lectores/bytes/mid | `a1-cierres/audit_static.py` | `estatico-independiente.json` |
| Familias completas/partidas/pendientes | `a2-familias/censo_independiente.py`, `log_independiente.py` | `A/B/C-censo.json`, `censo-log-vs-bin.json` |
| Constructores, RTTI y latencias familiares | `estatico_independiente.py`, `rtti_independiente.py`, `latencia2223.py` dentro de `a2-familias` | `estatico.json`, `rtti.json`; salida de latencias y dictamen2 |
| Casos que refutan el diseño literal | `a2-familias/contraejemplos_diseno.py` | `contraejemplos_diseno.json`; modelos de especificación, no incidentes en vivo |
| Viajeros327,0/82 y73/76, supervivientes46 | `a3-viajeros/auditar_viajeros.py`, `comprobar_ventana.py` | `recuentos.json`, `cohortes-evidencias.json`, `posiciones-tras-alta.json` |
| Productor adicional y límites de procedencia | `a3-viajeros/auditar_pe.py`, `comprobar_extra.py` | `estatico.json`, `comprobaciones-extra.json`, `ghidra-0x28981b0.c` |
|843 emisores0, retiradas, latencias y21,402565s | `a4-contencion/recuento.py`, `capturas.py`, `detalle.py`, `familias-tardios.py` | `recuentos.json`, `capturas.json`, `detalle.txt`, `familias-tardios.json` |
| Contención: PE/ramas/firmas | `a4-contencion/pe.py`, `pe-detalle.py` | `pe.json`, `pe-detalle.json` |
| Guardado: cobertura, llamadas y30 operaciones | `a5-guardado/audit_log_coverage.py` (usa `audit_logs.py`), `summarize_sequence.py` | `logs.json`, `log-summary.txt`, extractos A/B/C |
| Guardado: bytes, firmas completas y globales | `a5-guardado/audit_pe.py`, `audit_extra.py`, `audit_branches.py` | `pe-check.json`, `extra-bytes.json`, `branch-check.json` |
| Guardado: límites de cabecera y recuperación | `a5-guardado/audit_models.py` | `counterexamples.json`; modelo propio de las condiciones propuestas |
| Segunda revisión: actor consultante y thunk Logout | `revision-consolidacion/verificar_primarias.py`, `verificar_thunk.py` | `verificaciones-primarias.json`, `thunk-logout.json`, revisiones escritas |
| Contraste del coordinador, integridad y hashes | `consolidacion/verificar_transversal.py`, `inventario_fuentes.py` | `verificacion-transversal.json`, `inventario-fuentes.json` |

Ejemplo de ejecución de un lector autónomo, desde una **copia de trabajo** de la evidencia para no sobreescribir los resultados publicados:

```powershell
& 'C:\Emulador CD\build\research\venv\Scripts\python.exe' -I -B '<copia-de-evidencia>\a1-cierres\audit_captures.py'
```

No todos los scripts son portátiles sin preparar rutas: algunos conservan el directorio temporal original y otros requieren la instantánea `codigo-d835806`. El [README de reproducción](datos/auditoria-codex-20261014/README-REPRODUCCION.md) especifica estructura, dependencias, orden y comandos. `-I -B` evita importar desde el entorno ordinario y escribir bytecode; los scripts que usan otros lectores los cargan explícitamente. Usan Python y, para desensamblado, Capstone del venv existente; no descargan herramientas ni ejecutan código del juego.

Para Ghidra se usó la VA calculada como `0x140000000 + RVA`, no concatenación textual. Ejemplo: RVA `0x28981B0` → VA `0x1428981B0`. Los exports originales se leyeron y los nuevos se conservaron. La recuperación de una pila depende de memoria realmente incluida en el minidump; no se sustituyeron huecos por valores inventados.

El inventario de fuentes fija hashes SHA-256 de los informes originales y las seis capturas. El manifiesto de entrega fija los archivos propios publicados. Las conclusiones se limitan a d835806 y build2976; aplicar estos resultados a otro commit/build exige contrastar diferencias y repetir los contratos afectados. Ninguno de los dictámenes aprueba activar a la vez todos los cambios propuestos.