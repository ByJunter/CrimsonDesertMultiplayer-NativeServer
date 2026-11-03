# Ghidra: proyecto persistente de CrimsonDesert.exe (build 1.0.0.2976)

Proyecto de Ghidra con el ejecutable ya analizado, más herramientas para consultarlo en segundos
(decompilar, referencias, cadenas, vtables, importaciones) sin volver a analizar. Sustituye al
análisis con Python y capstone función a función. Encargo: `docs/PROMPT-GHIDRA.md`.
Trabajo hecho la noche del 8 al 9 de octubre de 2026.

> **Si el juego se actualiza, este proyecto deja de valer.** Todas las RVA, nombres y
> exportaciones son de la build 1.0.0.2976 (PE timestamp `0x6AB28F00`). Con un exe nuevo hay que
> repetir el proceso (unos 45 min en total, ver «Rehacerlo desde cero»).

## Resumen

| | |
|---|---|
| Ejecutable analizado | copia en `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, 384 521 624 bytes |
| SHA-256 | `57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7` (también en `bin\SHA256.txt`) |
| Versión | FileVersion 1.0.0.2976 (versión del juego 2.03.02), PE timestamp `0x6AB28F00`, base `0x140000000`, x86-64 |
| Ghidra | 12.1.4 (`ghidra_12.1.4_PUBLIC_20260921.zip`), en `D:\Tools\ghidra_12.1.4_PUBLIC` |
| JDK | Eclipse Temurin 21.0.12.1+1 (ZIP portable), en `D:\Tools\jdk-21.0.12.1+1` |
| Proyecto | `D:\RE\CrimsonDesert_2976\project\CrimsonDesert_2976.gpr`, programa `/CrimsonDesert.exe` |
| Duración del análisis | **39 min 56 s** (análisis automático) + 2 min 24 s (RTTI) + unos 30 s (nombres y cadenas) |
| Resultado | 221 113 funciones, 17,1 millones de instrucciones, 88 354 cadenas, 15 931 clases RTTI (15 165 vftables), 957 importaciones |
| Nombres del equipo | 7572 símbolos `cdmp_` (2865 nombres de función), contados en el estado vivo el 9-10 (`exports\cdmp_symbols.csv`); las 15 direcciones pedidas, nombradas |
| Consultas | servidor local: 0,003–0,25 s (medido el 9-10, sección 12); sin servidor (headless): 4–7 s (12 s `strings`) |
| MCP | `scripts\cdmp_mcp.py`: stdio, solo lectura, servidor fijado en 127.0.0.1, sin dependencias nuevas, **sin autenticación** (secciones 10 y 12) |
| Revisión | `REVISION.md` (NO CONECTAR): bloqueantes corregidos y prueba operativa hecha el 9-10 (sección 12) |
| Tamaño | proyecto 3,76 GB; copias de seguridad 6,0 GB; exportaciones 26 MB; Ghidra + JDK 1,9 GB |

Nada de esto toca la carpeta del juego: solo se leyó (y copió) el exe, con el juego cerrado.

## 1. Instalación

Descargado solo de las fuentes oficiales, cada archivo en una carpeta vacía propia
(`D:\Tools\_dl\ghidra\`, `D:\Tools\_dl\jdk\`), y comprobado el SHA-256 antes de descomprimir:

| Archivo | Origen | Tamaño | SHA-256 publicado = calculado |
|---|---|---|---|
| `ghidra_12.1.4_PUBLIC_20260921.zip` | github.com/NationalSecurityAgency/ghidra/releases (última estable, 21-09-2026) | 569 649 598 B | `ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db` ✔ |
| `OpenJDK21U-jdk_x64_windows_hotspot_21.0.12.1_1.zip` | api.adoptium.net (última Temurin 21 para Windows x64) | 205 073 461 B | `f9d6e191ab098c0d416e7d588a24420a8621cd2f4720dab2459b8b7b2d2d8b4e` ✔ |

- **Decisión:** el JDK va en ZIP portable y no en MSI, para no tocar el registro ni el `PATH` del
  sistema. Las herramientas fijan `JAVA_HOME` solo para su propio proceso (`scripts\_env.ps1`).
- **Nota:** el enlace oficial de adoptium.net descarga el binario de
  `github.com/adoptium/temurin21-binaries`, que es donde Adoptium aloja sus versiones. El checksum
  viene de su API y de su `.sha256.txt`.
- **Memoria (22 GB):** en Ghidra 12.1.4 `MAXMEM` ya no está en `support\launch.properties` sino en
  los `.bat`:
  - `support\analyzeHeadless.bat`: `set MAXMEM_DEFAULT=2G` → `22G`;
  - `ghidraRun.bat`: `set MAXMEM_DEFAULT=` → `22G`.
- **JDK:** en `support\launch.properties`, `JAVA_HOME_OVERRIDE=D:/Tools/jdk-21.0.12.1+1`.
- Los originales quedan junto a cada archivo como `*.orig`.
- **Ojo:** aun con `JAVA_HOME_OVERRIDE`, `launch.bat` necesita encontrar `java` en el `PATH` o
  en `JAVA_HOME` antes de leer ese fichero. Por eso todo se lanza a través de `_env.ps1`.
  - Para la interfaz gráfica: `scripts\ghidra-gui.ps1`.
- Las consultas, con servidor y sin él, bajan el heap a 12 GB (`GHIDRA_HEADLESS_MAXMEM`) salvo que
  ya esté definida. Antes del 9-10, las consultas sin servidor heredaban los 22 GB del análisis.

## 2. Carpetas

```
D:\RE\CrimsonDesert_2976\
  bin\        CrimsonDesert.exe (copia) y SHA256.txt
  project\    CrimsonDesert_2976.gpr + .rep  (el proyecto; no está en la carpeta compartida ni sincronizado)
  scripts\    scripts de Ghidra (Java) y de PowerShell; scripts\data\ = nombres conocidos
  exports\    exportaciones (paso 8), informe de nombres y exports\decomp\<rva>.c
  logs\       import, preanalysis, analysis, rtti, names, strings, export, server
  backup\     project_tras_analisis: estado justo DESPUÉS del análisis automático (23:53),
                antes de RTTI, nombres y cadenas. No es el estado previo al análisis.
              project_tras_rtti_y_nombres: tras RTTI y nombres, antes de las cadenas UTF-8/16
                y de la reparación de los 3 nombres.
```

**Huella fuera de `D:\RE` y `D:\Tools`** (inventario del 9-10):
- `%APPDATA%\ghidra\` (2,9 MB): preferencias, `application.log` y los bundles de scripts
  compilados de Ghidra.
- `%LOCALAPPDATA%\ghidra\` (30,6 MB): caché de Ghidra.
- `%TEMP%\ghidra\`: vacío.
- `%TEMP%\cdmp_h_*.cmd`, `cdmp_req_*.txt` y `cdmp_out_*.txt`: temporales de cada consulta
  headless; se borran al terminar. Borré a mano un `.cmd` que había quedado de un proceso matado.
  Los demás `%TEMP%\cdmp_*` (build, deploy, optd…) son de la otra sesión.
- El proyecto de prueba `msvcp140.dll` (16 MB) del scratchpad de esta conversación
  (`<scratch>\fbda5b42-…\scratchpad\testproj\`).
- **Reglas de cortafuegos:** la revisión encontró dos reglas «Query User» de entrada (TCP y UDP,
  perfil privado) para `D:\tools\jdk-21.0.12.1+1\bin\java.exe`. No hacen falta: el servidor solo
  escucha en 127.0.0.1. Dijiste que las borras tú; el 9-10, `Get-NetFirewallApplicationFilter` ya
  no devolvía ninguna para esa ruta.
- **Nada en el sistema:** ni `JAVA_HOME` ni `PATH` globales, ni servicios, ni tareas programadas,
  ni claves Run (comprobado por la revisión).

Hay una copia de `scripts\` (sin el CSV derivado `known_names_apply.csv`) en
`docs/research/ghidra/scripts/`, para que quede en el repositorio.

## 3. Importación y tabla de secciones

Comando:

```
analyzeHeadless D:\RE\CrimsonDesert_2976\project CrimsonDesert_2976 \
  -import ...\bin\CrimsonDesert.exe -noanalysis
```

- Cargador: «Portable Executable (PE)»; lenguaje `x86:LE:64:default:windows`.
- La importación tardó 42 s y Ghidra comprobó que la base de imagen es `0x140000000`.
- El cargador ya crea **192 711 funciones** a partir de `.pdata` (en Ghidra, la sección `.trace`):
  172 316 en `.rsrc` y 20 395 en `.xtls`.

Secciones. Denuvo cambió los nombres; la columna de la derecha dice qué es cada una de verdad:

| Sección | RVA | Tamaño | PE | En Ghidra | Qué es realmente |
|---|---|---|---|---|---|
| `.rsrc` | `0x1000` | `0x51EA000` | R-X | R-X | **El código del juego** (el `.text` real) |
| `.xdata` | `0x51EB000` | `0x16A8000` | R-- | R-- | El `.rdata` real: IAT al principio (`0x51EB000`), vtables, COL de RTTI, cadenas constantes |
| `.rdata` | `0x6893000` | `0x854000` | RW- | RW- | El `.data` real: globales y type descriptors de RTTI (`.?AV…`) |
| `.00cfg` | `0x70E7000` | `0x2CD000` | R-- | R-- | |
| `.tls$` | `0x73B4000` | `0x3000` | R-- | R-- | |
| `.tls` | `0x73B7000` | `0x30000` | R-- | R-- | **Recursos PE** (el directorio de recursos apunta aquí, a `0x73B7000`) |
| `.xtext` | `0x73E7000` | `0x1E2000` | R-- | R-- | Denuvo, sin código ejecutable |
| `.xtls` | `0x75C9000` | `0xF9466E2` | **RWX** | **RW-** | Código virtualizado por Denuvo (punto de entrada y TLS callbacks 0, 1, 2 y 5) |
| `.text` | `0x16F10000` | `0x1C3986` | R-- | R-- | Denuvo, sin código ejecutable (tabla de TLS callbacks en `0x16F10000`) |
| `.shared` | `0x170D4000` | `0x248` | RW- | RW- | |
| `.impdata` | `0x170D5000` | `0x8DDC` | R-- | R-- | Exportaciones (Wwise `AK::…`) |
| `.trace` | `0x170DE000` | `0x2CC250` | R-- | R-- | `.pdata` (RUNTIME_FUNCTION) |

Las mismas columnas están en `exports\sections.csv` (`perm_pe` sale de la tabla de secciones del
propio PE).

**`.rsrc` no se interpreta como recursos.** El cargador de Ghidra busca los recursos por el
directorio de datos del PE, no por el nombre de la sección, y ese directorio apunta al bloque
`.tls`. Por eso `.rsrc` queda como un bloque R-X normal y se desensambla entero: 200 707 funciones
y 17 124 735 instrucciones al final. No hizo falta tocar nada para eso.

## 4. Exclusión de `.xtls` (y cómo revertirla)

Pre-script `scripts\Cdmp_PreAnalysis.java`, ejecutado antes del análisis:

1. Quita el permiso de ejecución al bloque `.xtls` (`RWX` → `RW-`). El permiso original queda
   en Program Information, en `cdmp.xtls.execute.original = true`.
2. Activa la opción de programa **Disassembler › «Restrict Disassembly to Executable Memory» =
   true**. Con eso, ningún flujo (`call` o `jmp` desde `.rsrc`, entradas de `.pdata`, punto de
   entrada, TLS callbacks…) se desensambla fuera de memoria ejecutable.
3. Comprueba que `.rsrc` sigue siendo ejecutable.

Comprobaciones:
- **Antes del análisis** (`Cdmp_TestRestrict.java`, en solo lectura):
  - `0x1317D40` se desensambla (4609 instrucciones siguiendo flujos);
  - el `jmp 0x14B686EB0` del accesor `0x1318E70` no entra en `.xtls`;
  - en `.xtls` quedan 0 instrucciones.
- **Después del análisis:** 0 instrucciones en `.xtls`, `.text` y `.xtext`. Las 20 405 entradas de
  `.pdata` en `.xtls` siguen como funciones sin cuerpo; así, las llamadas a código virtualizado
  muestran un nombre (`FUN_14xxxxxxx` o `cdmp_…`). En `0x1318E70` Ghidra ve un thunk hacia
  `.xtls`.

**Revertir**: `scripts\Cdmp_RevertXtls.java` vuelve a poner X en `.xtls` (lo lee de la tabla de
secciones del PE) y quita la restricción. **Solo restaura esos dos ajustes**: no deshace el
análisis, ni RTTI, ni los nombres, ni las cadenas.

```
analyzeHeadless D:\RE\CrimsonDesert_2976\project CrimsonDesert_2976 -process CrimsonDesert.exe \
  -noanalysis -scriptPath D:\RE\CrimsonDesert_2976\scripts -postScript Cdmp_RevertXtls.java
```

Para volver al estado anterior al análisis hay que **reimportar** (42 s) y repetir lo que haga
falta. `backup\project_tras_analisis` es el estado justo *después* del análisis automático, antes de
RTTI, nombres y cadenas.

**Error mío, ya corregido:** el pre-script se ejecutó dos veces (la segunda dentro del análisis) y
sobrescribió `cdmp.xtls.execute.original` con `false`. El valor ya está corregido
(`Cdmp_SetXtlsOrig.java`), el pre-script no lo vuelve a escribir y la reversión ya no depende de
él.

## 5. Análisis completo

`scripts\run_analysis.ps1` lanza `analyzeHeadless … -process CrimsonDesert.exe -preScript
Cdmp_PreAnalysis.java -postScript Cdmp_Inspect.java -max-cpu 10`:
- `-max-cpu 10` es la mitad de los 20 núcleos;
- va con prioridad baja (`start /LOW`, clase Idle);
- toda la salida va a `logs\analysis.log`, con hora de inicio y de fin.

- Inicio: 2026-10-08 23:13:39 · Fin: 23:53:35 · **Duración: 39 min 56 s**, código de salida 0, guardado correcto.
- Memoria: el proceso Java llegó a unos 12,5 GB.
  - Con los demás programas abiertos, la RAM libre bajó a 4,6 GB y la memoria comprometida llegó a
    36,2 de 37,7 GB.
  - No falló, porque el archivo de paginación lo gestiona Windows y puede crecer.
  - Aviso: con el juego abierto a la vez, un análisis así paginaría mucho.

**Analizadores activos** (los de por defecto; RTTI y cadenas ya lo son): ASCII Strings, Apply Data
Archives, Call Convention ID, Call-Fixup Installer, Create Address Tables, Data Reference,
Decompiler Switch Analysis, Demangler Microsoft, Disassemble Entry Points, Embedded Media, External
Entry References, Function ID, Function Start Search, Non-Returning Functions (Discovered y Known),
PDB Universal (no hay PDB: no hace nada), Reference, Scalar Operand References, Shared Return
Calls, Stack, Subroutine References, Windows x86 PE Exception Handling, Windows x86 PE RTTI
Analyzer, Windows x86 TEB Analyzer, WindowsResourceReference, x86 Constant Reference Analyzer.

**Desactivados:**
- Aggressive Instruction Finder y Decompiler Parameter ID, como pedía el encargo. En Ghidra 12.1.4
  ya vienen apagados por defecto; el pre-script los fija igualmente.
- Por defecto también están apagados, y los dejé así: Condense Filler Bytes, PDB MSDIA, Variadic
  Function Signature Override y WindowsPE x86 Propagate External Parameters.
- Consecuencia de no usar Decompiler Parameter ID: la firma guardada de muchas funciones es
  `undefined f()`, aunque el decompilador deduce los parámetros al decompilar. Si se quieren firmas
  guardadas, se puede pasar más tarde solo ese analizador, que es lento.

Tiempos por analizador (del log):
- los más largos: Create Address Tables - One Time 810 s, Stack 375 s, x86 Constant Reference
  349 s, Disassemble Entry Points 235 s, Reference 128 s, Scalar Operand References 106 s y
  Decompiler Switch Analysis - One Time 105 s;
- total de analizadores, 2376 s.

El log tiene 44 WARN y 3 ERROR, todos inofensivos:
- 42 son `Decompiling …: Unable to read bytes at ram:20ccccccc`, por saltos a direcciones fuera de
  la imagen;
- los ERROR son 3 `Could not create Data` del demangler, en direcciones que ya son código.

**Si se interrumpe:** Ghidra en modo headless solo guarda al final, así que un análisis cortado no
deja nada a medias: el proyecto queda como en el último guardado. Para reanudar basta con volver a
ejecutar `scripts\run_analysis.ps1`. El pre-script es idempotente.

### 5b. RTTI: el analizador no encontraba nada por los nombres de sección

En el análisis automático, «Windows x86 PE RTTI Analyzer» tardó 0,002 s y no encontró ninguna
clase. El motivo, visto con `javap` sobre `MicrosoftCodeAnalyzer.jar`: el analizador busca bloques
**por nombre**.
- Busca los type descriptors (`.?AV…`) en bloques `.data*`; aquí están en el bloque `.rdata`.
- Busca los Complete Object Locators en `.rdata`, `.data` o `.text`; aquí están en `.xdata`.
- Si existe un bloque `.text` (o `.nep`), solo acepta entradas de vtable dentro de él. Aquí `.text`
  es de Denuvo y no tiene código, así que no valdría ninguna vtable.
- Además, al terminar deja `Program Information › RTTI Found = false` y ya no se repite.

Lo resuelve `scripts\Cdmp_RttiFix.java`:
1. renombra temporalmente `.text` → `.text_dnv`, `.rdata` → `.data` y `.xdata` → `.rdata`;
2. quita «RTTI Found»;
3. llama a `RttiAnalyzer.added()`;
4. deshace los nombres en orden inverso, también si falla.

No toca permisos. Sin bloque `.text`, el analizador acepta entradas que apunten a cualquier
función, de `.rsrc` o de `.xtls`.

Resultado (2 min 24 s): **15 931 type descriptors y 15 165 vftables** con su nombre de clase
(p. ej. `pa::NwVirtualAsyncSession::vftable` en `0x57260C0`). En el exe hay 15 204 cadenas `.?AV`
(más las `.?AU` de las struct), así que no se perdió casi ninguna. Los `ERROR Failed to disassemble
at 1468b…` del log son entradas falsas de vtable que apuntan a datos. La restricción impidió
desensamblarlas, que es lo correcto.

### 5c. Cadenas UTF-8 (coreano) y UTF-16

«ASCII Strings» solo encuentra ASCII: tras el análisis había 77 794 cadenas, casi ninguna en
coreano. `scripts\Cdmp_DefineStrings.java` define en `.xdata` y `.rdata`, solo sobre bytes que
estén sin definir:
- **9847 cadenas UTF-8 con hangul**, de las que 9101 tienen referencias desde el código;
- **713 UTF-16**, de las que 209 tienen referencias.

Las heurísticas están en la cabecera del script. Se probó primero en seco (`dry`). Total final:
**88 354 cadenas**, de las que 60 815 tienen referencias. Ejemplo: `.\strings.ps1 저장`
(«guardar») devuelve 30 cadenas con las funciones que las usan.

## 6. Nombres conocidos (`cdmp_`)

### Fuentes
- `scripts\data\known_names_repo.csv` (`rva,kind,name,comment,source`, 4172 filas). Lo sacó un
  subagente leyendo el worktree `opcion-d` (solo lectura):
  - `src/mod/game/offsets.h`, `src/mod/session2/offsets_s2.h` y las constantes comentadas de
    `src/mod/{net,session2,game}`;
  - los 78 `.md` de `docs/research/`;
  - los datos de `docs/research/data/` (`rpt0-estatico.md`, `vision-a7-rvas-fuente.csv`,
    `vision-firmas-2976.json` y los demás `.json`).
  - Por tipo: 1812 func, 1165 code, 434 string, 422 vtable y 339 data.
  - Cada fila lleva su fuente `ruta:línea`, y en el comentario los otros nombres y fuentes.
- **Decisión:** se aplican 4139 filas (`known_names_apply.csv`). Quité las 33 `*_AobTwin_*`: que
  coincidan los 40 primeros bytes no prueba que sea la misma función, y ese nombre engañaría.
  - Quedan otras ~180 de poco valor (`*_dataref_*`, `*_Caller_*`, `*_xtlsref_*`) que se
    reconocen por el nombre.
- `docs/research/data/troc_messages_2976.csv` (1115 mensajes), aplicado así:
  - vtable → `cdmp_troc_<Nombre>_vtable`;
  - instancia estática → `cdmp_troc_<Nombre>_inst`;
  - manejador (slot 2), si es único (1097) → `cdmp_troc_<Nombre>_Handler`;
  - compartidos → `cdmp_troc_SharedHandler_<rva>` (6), `cdmp_troc_OnReceive_<rva>` (3) y
    `cdmp_troc_Register_<rva>` (37), con la lista de mensajes en el comentario;
  - todos llevan un comentario con el opcode, el kind, la dirección y el dominio.

### Reglas del script (`scripts\Cdmp_ApplyNames.java`)
- Todo lleva el prefijo `cdmp_`.
- `Clase::metodo` se escribe `cdmp_Clase__metodo`, porque `::` en Ghidra es espacio de nombres.
  El nombre original queda en el comentario.
- **Función:** si la función tiene un nombre por defecto, se renombra. Si ya tenía uno (de una
  exportación, del demangler o de otra fila), se deja y `cdmp_…` queda como etiqueta secundaria.
- **Dirección que no es inicio de función,** `code` o datos: etiqueta, más un comentario «pre» o
  «plate».
- **Sin función:** solo se crea una si hay código ejecutable y no cae dentro de otra función.
  Nunca en `.xtls`.
- **Idempotente:** se puede volver a ejecutar sin duplicar nada.

### Resultado
Informe fila a fila en `exports\cdmp_names_report.csv`. Es el de la primera pasada: los 3 nombres
reparados siguen figurando ahí como `funcion_renombrada`.
- **Recuento vivo** (9-10, `exports\cdmp_symbols.csv`): **7572 símbolos `cdmp_`, de los que 2865
  son nombres de función**.
- `names.log` contó 7573 y 2869 antes de la reparación.
- La diferencia de 1 (de 2866 a 2865) es el thunk `0x41E1DE0`: heredaba el nombre mezclado y, tras
  la reparación, hereda `AK::TempAlloc::DumpTempAllocsToFile`.

| Estado | Filas |
|---|---|
| funcion_renombrada | 2737 (3 se devolvieron después a su nombre demanglado, ver abajo) |
| etiqueta_en_funcion_con_nombre (ya tenía nombre; cdmp_ como etiqueta) | 175 |
| etiqueta_codigo | 1017 |
| etiqueta_vtable / etiqueta_datos / etiqueta_cadena | 422 / 339 / 434 |
| etiqueta_troc_vtable / etiqueta_troc_instancia | 1115 / 1115 |
| **No encajaron** (se dejó etiqueta y comentario) | |
| x_code_sin_instruccion: `code` donde no hay instrucción | 148 (144 en `.xtls`, 4 en `.rsrc`) |
| x_func_en_xtls: «función» en `.xtls` sin entrada de `.pdata` | 7 |
| x_func_no_es_inicio: la dirección está a mitad de otra función | 3 |

Los 3 casos de «no es inicio»:
- `0xA170B0` BuffList_FindById, dentro de `0xA16F60`+0x150;
- `0xC06CC0` AquireItemNodeIdAck_Handler, dentro de `0xC06C60`+0x60;
- `0x143B8B0`, dentro de TrocRegistry::Insert `0x143B810`+0xA0.

Las 15 pedidas, todas `funcion_renombrada` salvo la vtable:

| RVA | Nombre en Ghidra |
|---|---|
| `0x35CA00` | `cdmp_ClientNet__OnRecvPacket` |
| `0x35C670` | `cdmp_ClientNet__PostPacket` |
| `0x2773C20` | `cdmp_ServerNet__OnRecvPacket` |
| `0x1437740` | `cdmp_NwVirtualAsyncSession__Send` (vtable `0x57260C0`: `pa::NwVirtualAsyncSession::vftable` + `cdmp_NwVirtualAsyncSession__vftable`) |
| `0x14380B0` | `cdmp_NwVirtualSyncSession__Send` |
| `0x2A759B0` | `cdmp_ToNearExceptOneControl__vf1` |
| `0x1317D40` | `cdmp_SentryReporter` |
| `0x2356270` | `cdmp_SaveFailReport` |
| `0x1315CD0` | `cdmp_SentrySetup` |
| `0x9E89C0` | `cdmp_MinimapIconDecide` |
| `0x349EA10` | `cdmp_TreeComponent__SetBend` (solo la respalda `docs/PROMPT-GHIDRA.md`; marcada «sin verificar») |
| `0x2217290` | `cdmp_TargetComponent__SetCurrentTarget` |
| `0x1318E70` | `cdmp_GetSentryReporterSingleton` (thunk a `.xtls`) |
| `0x44FD30` | `cdmp_Utf8ToUtf16` |

**Corrección aplicada:** la primera pasada renombró 3 stubs que ya tenían un nombre demanglado con
espacio de nombres. Son funciones compartidas por plegado de código idéntico, y quedaban mezclas
como `AK::TempAlloc::cdmp_ClientCharacterControlActorComponent__vf73`.
- `Cdmp_FixNamespacedNames.java` les devolvió el nombre original (`AK::TempAlloc::DumpTempAllocsToFile`
  `0x359B50`, `AK::WriteBytesCount::Reserve` `0x359BA0` y `AK::WriteBytesCount::Bytes` `0x3600B0`).
- El nombre `cdmp_` quedó como etiqueta.
- El script de nombres ya respeta esos casos.

**Conflictos y dudas del repositorio** (detalle en el comentario de cada símbolo):
- **`0x35C490`:** es `ClientNet::DisconnectLocal`. `FlushSendQueue` es `0x35BE90`, según
  `opcion-d-p8.md`. El comentario de `src/mod/session2/guest.cpp:214` parece decir lo contrario.
- **`0x6D69528` / `0x6D69530`:** `offsets.h` las da como raíz de iconos del minimapa, y
  `optD-combate.md` y `pieza4-dano-servidor.md` como raíz de los índices de stats (cliente y
  servidor). Se llaman `g_StaticDataRoot_Client/Server` y conservan los dos usos.
- **Se quedaron fuera:**
  - RVAs de otras builds: World Builder 2949 y cheat tables 1.0x;
  - falsos positivos que los propios documentos señalan;
  - offsets de fichero y hashes que parecen RVAs.

## 7. Herramientas de consulta

En `D:\RE\CrimsonDesert_2976\scripts\` (copia en `docs/research/ghidra/scripts/`). Se ejecutan
desde PowerShell, p. ej. `D:\RE\CrimsonDesert_2976\scripts\decomp.ps1 0x35CA00`. Admiten una RVA
(`0x35CA00`), una VA (`0x14035CA00`) o un nombre (`ClientNet::PostPacket`, `cdmp_SentryReporter`,
`SentryReporter`).

| Script | Qué hace |
|---|---|
| `decomp.ps1 <rva\|nombre>` | Pseudocódigo C de la función que contiene la dirección. Lo imprime y lo guarda en `exports\decomp\<rva>.c` |
| `xrefs.ps1 <rva\|nombre>` | Referencias a esa dirección (llamadas, lecturas y escrituras), con la función y la instrucción de origen |
| `strings.ps1 <texto>` | Cadenas que contienen el texto (sin distinguir mayúsculas; también coreano) y quién las usa |
| `vtable.ps1 <rva>` | Métodos de la tabla virtual que empieza ahí |
| `class.ps1 <nombre RTTI>` | Vftables de las clases cuyo nombre coincide (exacto o, si no hay, que lo contenga) |
| `imports.ps1 <nombre>` | Dónde se llama a una importación |
| `func.ps1 <rva\|nombre>` | Entrada, tamaño, firma, nombres, llamadores y llamados |
| `disasm.ps1 <rva> [n]` | n instrucciones (1–400, 40 por defecto) con bytes, etiquetas y referencias (añadida para la comprobación). Si no hay instrucción que empiece en esa dirección, lo avisa y sigue desde la siguiente |
| `symbols.ps1 <texto>` | Símbolos con nombre (no `FUN_`/`DAT_`) que contienen el texto: nombres `cdmp_`, clases, etiquetas (añadida con el MCP) |
| `server-start.ps1` / `server-stop.ps1` | Levanta o para el servidor local de consultas |
| `run_analysis.ps1` | Repite el análisis completo (solo si hace falta) |
| `ghidra-gui.ps1` | Abre la interfaz gráfica con el JDK correcto |

Todas usan `Cdmp_Query.java` sobre el proyecto ya analizado, con `-process -noanalysis -readOnly`:
no reanalizan ni guardan nada en el programa.

**Cómo viajan los argumentos (desde el 9-10).** Cada argumento de PowerShell es un **campo**. Un
nombre con espacios va entre comillas: `.\func.ps1 "std::foo bar"`.
- **Con servidor:** los campos van por el socket, separados por TAB, en una línea UTF-8.
- **Sin servidor:** se escriben en un fichero de petición en `%TEMP%`, que lee Java. A la línea de
  `cmd` de `analyzeHeadless` solo llegan rutas y opciones fijas, que pasan el filtro
  `^[A-Za-z0-9_:\\./=+\-]+$` (sin espacios, comillas, `%`, `&`, `|`, `<`, `>`, `^`, `!`…).
- **Nunca se interpreta texto libre como sintaxis.** Por ejemplo, `.\xrefs.ps1 'a&calc %PATH% "x"'`
  llega intacto a Java como un nombre, que no existe.

**Límites (en Java, mientras procesa; y antes, en PowerShell o Python):**

| Límite | Valor |
|---|---|
| Línea de petición | 4096 bytes, UTF-8 válido |
| Cada campo | de 1 a 512 caracteres, sin caracteres de control ni espacios al principio o al final |
| Campos por comando | `ping`/`shutdown` 0; `disasm` 1–2; el resto exactamente 1. Lo que sobra se rechaza |
| Instrucciones de `disasm` | entero de 1 a 400; se rechazan negativos, 0, 401, decimales y números enormes |
| Referencias de `xrefs` | 2000 como máximo (se para ahí y lo dice) |
| `strings` / `symbols` | 200 coincidencias; 30 referencias por cadena; al menos 2 caracteres |
| `vtable` / `class` | 1024 entradas; 40 vftables |
| `imports` | 50 importaciones; 500 usos |
| `func` | 500 llamadores y 500 llamados |
| Respuesta completa | 200 000 caracteres: al superarlos, Java deja de trabajar y marca el corte |
| Decompilación | 60 s como máximo por función |
| Lectura del socket | el servidor espera la línea 30 s; los clientes leen 2 MB como máximo |

**Escrituras de las herramientas** (lista completa):
- `exports\decomp\<rva>.c`, que se sobrescribe. El nombre sale de la RVA numérica, nunca del texto
  pedido.
- `logs\server.log`, en modo append.
- `%TEMP%\cdmp_h_<guid>.cmd`, `cdmp_req_<guid>.txt` y `cdmp_out_<guid>.txt`, que se borran al
  terminar.
- El log del MCP, si se define `CDMP_MCP_LOG`.
- Lo que Ghidra escribe al abrir cualquier proyecto: `~index.dat`/`~index.bak` del proyecto (63 y
  119 bytes), `%APPDATA%\ghidra` (preferencias, `application.log`, scripts compilados) y
  `%LOCALAPPDATA%\ghidra` (caché).
- La base de datos del programa (`db.*.gbf`) **no** se toca: sigue con la fecha 00:07:42 tras todas
  las sesiones de prueba.

**Tiempos medidos** (9-10, con las correcciones; detalle en la sección 12):
- **Sin servidor:** cada consulta lanza Ghidra headless y abre el proyecto: 4–7 s, y 12 s para
  `strings`, que crea la lista de cadenas.
- **Con servidor** (`server-start.ps1`):
  - tarda unos 12 s en abrir el proyecto una sola vez;
  - deja el programa y el decompilador abiertos en `127.0.0.1:18490`;
  - cada consulta tarda: decomp 0,06–0,16 s, xrefs 0,004–0,02 s, strings 0,01–0,02 s, class
    0,04–0,05 s, imports 0,003 s.
  - Mientras está levantado, el proyecto queda bloqueado para otros procesos de Ghidra: la
    interfaz gráfica, `run_analysis.ps1` o una consulta headless. Las herramientas lo detectan
    solas y usan el servidor; para el resto, ejecuta antes `server-stop.ps1`.
- **Identidad y parada:**
  - `server-start.ps1` da a cada arranque un id de instancia aleatorio (32 hex) y solo da por
    bueno un servidor que lo devuelva en `ping`, junto con su PID de Java y el SHA-256 del exe.
  - `server-stop.ps1` pide `shutdown`, espera a que **ese PID** termine (60 s) y comprueba que no
    queda ningún proceso con el id de la instancia. Solo fuerza el cierre de ese `java.exe`
    concreto, y solo si no sale solo.
- El servidor no queda levantado: lo paré al terminar.

Ejemplos reales (salida recortada):

_[Listado de decompilado del juego retirado de la versión pública (26 líneas); la RVA y la conclusión están en el texto.]_

_[Listado de decompilado del juego retirado de la versión pública (18 líneas); la RVA y la conclusión están en el texto.]_

_[Listado de decompilado del juego retirado de la versión pública (18 líneas); la RVA y la conclusión están en el texto.]_

```
> .\xrefs.ps1 0x1317D40
Referencias a 0x1317D40 (.rsrc) cdmp_SentryReporter
  0x1715B82C   DATA               (sin función)            <- su entrada de .pdata
  0x33119D7    UNCONDITIONAL_CALL FUN_143311810 [0x3311810]+0x1C7   CALL 0x141317d40
  0x2356434    UNCONDITIONAL_CALL cdmp_SaveFailReport [0x2356270]+0x1C4   CALL 0x141317d40

> .\imports.ps1 sentry_capture_event_with_scope
SENTRY.DLL::sentry_capture_event_with_scope
  puntero/thunk 0x51EC978 (.xdata)
    0x1317E91    READ           cdmp_SentryReporter [0x1317D40]+0x151   CALL qword ptr [0x1451ec978]

> .\vtable.ps1 0x57260C0
vtable 0x57260C0 pa::NwVirtualAsyncSession::vftable
  [  0] +0x000  0x1437580    .rsrc  cdmp_NwVirtualAsyncSession__dtor
  [  1] +0x008  0x3600C0     .rsrc  cdmp_SharedStub_ReturnTrue
  [  2] +0x010  0x35EA90     .rsrc  cdmp_SharedStub_WriteResultCode
  [  3] +0x018  0x1437600    .rsrc  cdmp_NwVirtualAsyncSession__Close
  ...
  [  9] +0x048  0x1437740    .rsrc  cdmp_NwVirtualAsyncSession__Send
  ... (15 entradas)

> .\strings.ps1 저장
0x5576B60  "저장된 하우징 결과물을 불러오시겠습니까?"
      <- 0x3DCC32  FUN_1403dcbf0 [0x3DCBF0]+0x42
...
```

Comandos por debajo (sirven para otros scripts):
- **Sin servidor:** escribir la petición (`<comando>` TAB `<arg1>` [TAB `<arg2>`], UTF-8) en un
  fichero y ejecutar:

  ```
  analyzeHeadless <proyecto> CrimsonDesert_2976 -process CrimsonDesert.exe -noanalysis -readOnly
    -scriptPath ...\scripts -postScript Cdmp_Query.java "req=<fichero>" "out=<fichero>"
  ```

  Es lo que hace `cdmp-query.ps1` con `Invoke-CdmpHeadless`.
- **Con servidor:** abrir un socket TCP a `127.0.0.1:18490`, enviar una línea con los campos
  separados por TAB, terminada en `\n`, y leer hasta que el servidor cierre.
- `ping` devuelve `pong` TAB programa TAB sha256 TAB pid TAB instancia; `shutdown` devuelve `bye`
  TAB pid.

Los argumentos del script no pueden empezar por `-`, porque `analyzeHeadless` los tomaría por
opciones, y van entre comillas, porque `cmd` parte por `=` fuera de ellas.

## 8. Exportaciones únicas (`D:\RE\CrimsonDesert_2976\exports\`)

Generadas por `Cdmp_Export.java` en 23 s (solo lectura):

| Fichero | Contenido | Filas |
|---|---|---|
| `functions.csv` | `rva,size,name,block,thunk,source` | 221 113 (200 707 en `.rsrc`, 20 405 en `.xtls`, 1 en `.xdata`*) |
| `strings.csv` | `rva,block,type,length,nrefs,value,refs` (refs `rva@función`, máx. 20) | 88 354 |
| `rtti_classes.csv` | `class,type_descriptor_rva,vtable_rva,nslots,slots` | 15 931 clases, 15 165 vftables |
| `sections.csv` | `name,start_rva,end_rva,size,perm_ghidra,perm_pe,initialized` | 14 |
| `imports.csv` | `library,name,ptr_rva,uses,callers` (máx. 30 llamadores) | 957 |
| `cdmp_names_report.csv` | informe del paso 6 | 7512 |
| `decomp\<rva>.c` | lo que vaya guardando `decomp.ps1` | |

\* La «función» de `.xdata` es la exportación `pa::BaseLock::RTTI_Complete_Object_Locator`
(`0x5E30A78`). El cargador la creó a partir de la tabla de exportaciones y es un dato, no código.

Se pueden filtrar con `Select-String` o `Import-Csv` sin abrir Ghidra.

## 9. Comprobación contra lo verificado a mano

| Lo que comprobó el equipo | Ghidra | ¿Cuadra? |
|---|---|---|
| `0x1317D40` empieza por `mov [rsp+10],rbx; push rbp/rsi/rdi/r12/r13/r14/r15` | `48 89 5c 24 10 MOV qword ptr [RSP + 0x10],RBX`, después `PUSH RBP`, `RSI`, `RDI`, `R12`, `R13`, `R14`, `R15`, `SUB RSP,0x80` | Sí |
| en `0x1317D60` hace `cmp byte [rcx],0` y salta a la salida | `0x1317D60 80 39 00 CMP byte ptr [RCX],0x0`; `0x1317D63 0f 84 … JZ 0x141317ef0` (epílogo). En el decompilado: `if (*param_1 == '\0') return;` | Sí |
| `0x2356420` llama a `0x1318E70` | `0x2356420 e8 4b 2a fc fe CALL 0x141318e70` (`cdmp_GetSentryReporterSingleton`) | Sí |
| carga `rcx` de `[rax+0x1988]` | `0x235642D 48 8b 88 88 19 00 00 MOV RCX,qword ptr [RAX + 0x1988]` | Sí |
| en `0x2356434` llama a `0x1317D40` | `0x2356434 e8 07 19 fc fe CALL 0x141317d40` (`cdmp_SentryReporter`) | Sí |
| `0x44FD30` recibe un `char*` UTF-8 y lo convierte a UTF-16 | El decompilador da `undefined ** cdmp_Utf8ToUtf16(char *param_1)`: si `param_1` es nulo o vacío devuelve una cadena vacía global; si no, mide la cadena byte a byte (bucle en `0x44FDB3`), la convierte en un búfer `wchar_t local_1028[2052]` (llamada en `0x44FDC2`) y la copia con `wcsncpy_s` a un objeto nuevo | Sí |

Ninguna discrepancia. Además, los dos llamadores de `0x1317D40` que da Ghidra (`0x2356434` y
`0x33119D7`) son los mismos que anotó RPT-0 (`data/rpt0-estatico.md`). Y la única llamada a
`sentry_capture_event_with_scope` está en `0x1317E91`, como dice `vision-a7-transversal.md`.

## 10. MCP (paso 9): capa propia, de solo lectura

Elegiste la opción A: una capa pequeña sobre el servidor de consultas, sin instalar ni descargar
nada. Es `D:\RE\CrimsonDesert_2976\scripts\cdmp_mcp.py` (copia en `docs/research/ghidra/scripts/`).

### Cómo funciona

```
Claude Code ──stdio (JSON-RPC, MCP)──► cdmp_mcp.py ──TCP 127.0.0.1:18490──► Ghidra headless -readOnly
 (lanza el proceso hijo)                (Python, solo                         (Cdmp_Query.java "serve")
                                         biblioteca estándar)
```

- **Sin dependencias nuevas.** Usa el Python 3.14 ya instalado
  (`%LOCALAPPDATA%\Python\pythoncore-3.14-64\python.exe`), solo con su biblioteca
  estándar (`json`, `socket`, `subprocess`, `ctypes` para comprobar PIDs), y se ejecuta con `-I`.
- **Red.**
  - El MCP no abre ningún puerto: habla con Claude Code por stdio y se conecta como cliente al
    literal `127.0.0.1`.
  - Lo único que escucha es el servidor de Ghidra, que hace `bind` al literal **`127.0.0.1`**
    (`InetAddress.getByAddress(127.0.0.1)`; antes era `getLoopbackAddress()`, que podía elegir
    `::1`).
  - Dirección real comprobada el 9-10 con el servidor levantado: `Get-NetTCPConnection` da
    `LocalAddress 127.0.0.1, LocalPort 18490`, y el dueño es el mismo `java` (pid 11888) que
    respondió al `ping`. El log de Java dice `escuchando en 127.0.0.1/127.0.0.1:18490`.
  - Solo el puerto se puede cambiar (`CDMP_PORT`, 1024–65535); la dirección no.
- **Sin autenticación.** Cualquier proceso local que pueda abrir `127.0.0.1:18490` puede hacer
  consultas y también enviar `shutdown` al servidor. Las consultas solo leen el proyecto y
  `shutdown` solo lo para; no hay ninguna operación de escritura ni de ejecución que alcanzar. En
  este PC, de un solo usuario, se acepta así. Si algún día hiciera falta, el siguiente paso sería un
  token por instancia.
- **Solo lectura.**
  - Todas las herramientas son consultas.
  - El servidor de Ghidra se abre con `-noanalysis -readOnly`, así que no puede guardar cambios en
    el programa. Comprobado: tras todas las sesiones del 9-10, la base de datos del programa
    (`db.9.gbf`) sigue con la fecha 00:07:42.
  - El MCP no tiene ninguna herramienta para renombrar, comentar, importar, analizar ni ejecutar
    scripts arbitrarios. Su `subprocess` solo lanza `server-start.ps1` y `server-stop.ps1`, con
    rutas fijas y, como mucho, `-Instance <32 hex>`.
  - Escrituras incidentales: ver la lista completa de la sección 7 (el `.c` de cada decompilación,
    `server.log`, temporales e índices y cachés de Ghidra).
- **No toca la carpeta del juego** ni ejecuta el juego.
- **Validación:**
  - Cada mensaje JSON-RPC tiene que ser un objeto con `jsonrpc: "2.0"`, `method` de texto, `id`
    entero o texto y `params` objeto.
  - Los argumentos se comprueban contra el esquema de cada herramienta: tipo, longitud de 1 a 512
    (2 como mínimo para buscar texto), sin caracteres de control. Si sobra alguna propiedad, se
    rechaza.
  - `count` tiene que ser un entero de verdad (no `bool`, `float` ni texto) de 1 a 400.
  - Nada se recorta en silencio: lo que no cumple se rechaza con `isError: true`.
- **Salidas grandes:** lee 2 MB del socket como máximo; luego recorta a 60 000 caracteres
  (`CDMP_MCP_MAXCHARS`) con un aviso. Java ya ha cortado antes, en 200 000 caracteres.
- **Propiedad y cierre:**
  - `ghidra_server_start` genera un id de instancia aleatorio (`os.urandom`).
  - Solo considera suyo el servidor que responde a `ping` con ese id, y apunta su PID de Java.
  - El bucle principal va en `try/finally`. Al terminar (fin de stdin, excepción o entrada
    errónea), si tiene un servidor propio, lo para con `server-stop.ps1 -Instance <id>`:
    `shutdown`, espera a que ese PID termine y comprueba que no queda ningún proceso de la
    instancia. Además verifica el PID por su cuenta (`OpenProcess`/`WaitForSingleObject`).
  - Un servidor que ya estaba levantado cuando empezó la sesión no es suyo y no lo para al salir.

### Herramientas que ve la IA

| Herramienta | Equivale a |
|---|---|
| `ghidra_decompile(target)` | `decomp.ps1` |
| `ghidra_disassemble(target, count)` | `disasm.ps1` |
| `ghidra_xrefs(target)` | `xrefs.ps1` |
| `ghidra_function(target)` | `func.ps1` |
| `ghidra_strings(text)` | `strings.ps1` |
| `ghidra_symbols(text)` | `symbols.ps1` (nuevo: busca nombres `cdmp_`, clases, etiquetas) |
| `ghidra_vtable(target)` | `vtable.ps1` |
| `ghidra_class(name)` | `class.ps1` |
| `ghidra_imports(name)` | `imports.ps1` |
| `ghidra_server_status` / `ghidra_server_start` / `ghidra_server_stop` | estado, arrancar y parar el servidor de Ghidra |

Todas las de consulta van marcadas `readOnlyHint: true`. `target` acepta una RVA, una VA o un
nombre.

### Arrancar y parar

El MCP en sí no hay que arrancarlo a mano: Claude Code lo lanza al abrir la sesión y lo cierra al
salir. Lo que hay que levantar es el **servidor de Ghidra** (≈12 s, hasta 12 GB de memoria), de una
de estas dos formas:
- **Desde Claude Code**, pidiéndole a la IA que use `ghidra_server_start`. Si lo levantó el MCP,
  **el MCP lo para solo al cerrarse la sesión**; también se puede parar antes con
  `ghidra_server_stop`.
- **A mano** (sirve también para los `.ps1`): `D:\RE\CrimsonDesert_2976\scripts\server-start.ps1`.
  Este servidor no lo para el MCP al salir: se para con `server-stop.ps1` (o con
  `ghidra_server_stop`).

Mientras el servidor está levantado, el proyecto no se puede abrir en la interfaz de Ghidra ni
repetir el análisis. Las consultas sin servidor tampoco hacen falta: los `.ps1` usan el servidor
solos.

Si el servidor no está levantado, las herramientas de consulta devuelven un error que lo dice. No
lo arrancan por su cuenta, para no ocupar 12 GB de memoria sin que se pida (por ejemplo, si estás
jugando).

### Conectarlo a Claude Code

No lo he registrado yo: es configuración persistente de tu Claude Code y te toca decidirlo. Desde
una terminal:

```bash
claude mcp add --scope user cdmp-ghidra -- "%LOCALAPPDATA%\Python\pythoncore-3.14-64\python.exe" -I "D:\RE\CrimsonDesert_2976\scripts\cdmp_mcp.py"
```

- `--scope user` lo deja disponible en todos tus proyectos (`~/.claude.json`).
- Con `--scope local` queda solo en esta carpeta.
- No uses `--scope project`: crearía un `.mcp.json` en el repositorio, y las rutas `D:\RE\…` solo
  existen en tu PC.
- Compruébalo con `claude mcp list`: debe salir `cdmp-ghidra … ✓ Connected`. Dentro de una sesión,
  `/mcp` lo muestra con sus 12 herramientas. La pestaña Code de la app de escritorio usa la misma
  configuración.
- Para quitarlo: `claude mcp remove cdmp-ghidra --scope user`.
- **Log opcional:** añade `-e CDMP_MCP_LOG=D:\RE\CrimsonDesert_2976\logs\mcp.log` antes del `--`.
  Apunta cada llamada (herramienta, argumentos, resultado y tiempo).
- Si actualizas Python y cambia la ruta de `pythoncore-3.14-64`, vuelve a registrarlo con la ruta
  nueva (`python -c "import sys; print(sys.executable)"`).

Ejemplos de uso en una sesión: «decompila `ClientNet::OnRecvPacket`», «¿quién llama a `0x1317D40`?»,
«busca la cadena 저장 y dime qué funciones la usan», «métodos de la clase NwVirtualSyncSession».

### Prueba hecha

La primera prueba fue del 9-10 a las 02:40. La prueba operativa completa, tras las correcciones de
la revisión, está en la sección 12.

## 11. Avisos

- **Otra build del juego = rehacer.** Con un exe nuevo, ninguna RVA, nombre ni exportación vale.
  Repite el proceso con el exe nuevo en una carpeta nueva (p. ej. `D:\RE\CrimsonDesert_<build>`).
- **No ejecutes dos procesos de Ghidra sobre el proyecto a la vez** (la interfaz, el servidor, un
  análisis): Ghidra bloquea el proyecto.
- **Las firmas guardadas** son casi todas `undefined f()`, porque Decompiler Parameter ID está
  apagado. Los nombres de parámetros y tipos son los del decompilador.
- **Referencias falsas:** algunas instrucciones con `[RBP+x]` tienen referencias falsas a globales
  (p. ej. `0x2356419 → 0x6A65940`), por el analizador de constantes. Fíate del operando, no de la
  flecha.
- **`.xtls` no se analiza:** el código virtualizado sigue siendo opaco. Las funciones que saltan ahí
  (como `0x1318E70`) se ven como thunks o como llamadas a funciones vacías.
- **Memoria:** el análisis completo puede subir el heap hasta 22 GB. Con el juego abierto, mejor no
  repetirlo.
- **Sentry:** el análisis es estático y nunca ejecuta el juego, así que no hay riesgo de enviar
  nada a Pearl Abyss.

## 12. Revisión independiente: correcciones y prueba operativa (9-10)

`REVISION.md` (veredicto **NO CONECTAR**) pedía tres correcciones antes de conectar, más una
prueba operativa con el servidor vivo. Las tres están hechas y la prueba, hecha.

### 12.1 Bloqueantes corregidos

| Bloqueante de la revisión | Corrección | Comprobado (9-10) |
|---|---|---|
| **1. Límites eludibles.** `target="0x1317D40 401", count=1` acababa en `disasm 0x1317D40 401 1` y Java tomaba 401; el recorte a 60 000 caracteres se hacía después de construir toda la respuesta | <ul><li>Protocolo **estructurado**: campos separados por TAB y número fijo de campos por comando; lo que sobra se rechaza.</li><li>Validación en **Python** (esquema estricto: tipos, rangos, sin propiedades extra) y otra vez en **Java** (longitud, caracteres de control, espacios al principio o al final, `count` con regex `[0-9]{1,3}` y rango 1–400).</li><li>Límites **durante el procesamiento**: salida acotada que corta el trabajo al pasar de 200 000 caracteres; `xrefs` con iterador y tope de 2000; topes en strings, imports, vtable y func.</li><li>Lectura acotada: línea de 4096 bytes; respuesta de 2 MB en el cliente.</li></ul> | <ul><li>Rechazados: 401, −1, 0, `"5"`, `true`, 5.5, 10¹², propiedad extra, TAB o salto de línea en `target`, 600 caracteres, argumento que falta, dirección fuera de memoria, `text` de 1 carácter.</li><li>`"0x1317D40 401"` llega a Java como un único nombre y da «no encuentro».</li><li>Por el socket: línea de 5000 bytes, UTF-8 inválido, comando desconocido, campo sobrante, vacío o con espacio inicial: todo rechazado.</li><li>`xrefs 0x6D69190` (unas 2800 referencias), pedido directamente por el socket: Java deja de trabajar al llegar al tope de salida (200 084 caracteres, 1984 referencias, 0,30 s) y lo indica con «respuesta cortada». El tope de 2000 referencias corta antes cuando las líneas son cortas.</li><li>`disasm` de 400 instrucciones responde en 0,03 s.</li></ul> |
| **2. Cierre y propiedad del proceso.** JSON `null` → `AttributeError` y sin `shutdown`; el arranque aceptaba cualquier respuesta al ping; nada comprobaba que Java terminara | <ul><li>Cada mensaje se valida como objeto JSON-RPC 2.0.</li><li>`try/finally` en el bucle principal.</li><li>**Id de instancia** aleatorio por arranque, devuelto por `ping` junto con el **PID de Java** y el SHA-256 del exe.</li><li>Arranque fallido: se matan solo los procesos de esa instancia.</li><li>Parada: `shutdown`, espera a que **ese PID** termine (60 s), comprueba que no queda ningún proceso de la instancia y solo fuerza ese `java.exe` si no sale.</li></ul> | <ul><li>`null`, `[]`, `42`, JSON roto, `id` booleano, `jsonrpc` 1.0, `params` lista y método desconocido devuelven el error JSON-RPC correspondiente, y el MCP sigue funcionando.</li><li>Tres sesiones (parada explícita, fin de stdin, y entrada errónea seguida de fin de stdin) terminan con «Java pid … terminado; procesos de la instancia que quedan: 0» y **ningún `java.exe` vivo**.</li><li>Un cliente que murió a mitad de una prueba (fallo del script de prueba) también dejó el servidor parado: pid 37932 terminado, 0 procesos.</li></ul> |
| **3. Interpolación en `cmd`** (`_env.ps1` / `cdmp-query.ps1`, ruta sin servidor) | <ul><li>Las consultas **ya no pasan por la línea de `cmd`**: van en un fichero de petición UTF-8 (`req=`), que lee Java.</li><li>A `cmd` solo llegan rutas y opciones fijas que pasan `^[A-Za-z0-9_:\\./=+\-]+$`; si no, se lanza una excepción.</li><li>Todas van entre comillas, lo que es seguro porque el filtro impide `"` y `%`.</li><li>`run_analysis.ps1` y el servidor usan la misma función.</li></ul> | `.\xrefs.ps1 'a&calc %PATH% "x"'` sin servidor llega a Java como un nombre literal («no encuentro…»): no se ejecutó nada ni se expandió `%PATH%`. |
| **Red y documentación** | <ul><li>`bind` al literal `127.0.0.1`.</li><li>Documentado que no hay autenticación (sección 10).</li><li>Reglas de cortafuegos: las borras tú (sección 2).</li></ul> | `Get-NetTCPConnection`: `127.0.0.1:18490`, dueño `java` (el mismo PID que el `ping`). |

Otras correcciones que pedía la revisión:
- **Memoria headless:** las consultas sin servidor ya usan 12 GB y no 22 GB.
- **Copias de seguridad y reversión** (sección 4): `project_tras_analisis` es el estado posterior
  al análisis, y la reversión de `.xtls` solo restaura permisos y opción.
- **Huella fuera de `D:`** (sección 2): preferencias y cachés de Ghidra, temporales y el proyecto de
  prueba.
- **Exportaciones regeneradas** a las 03:25 desde el estado vivo:
  - `functions.csv` ya muestra los nombres reparados (`0x359B50`
    `AK::TempAlloc::DumpTempAllocsToFile`, `0x359BA0`, `0x3600B0` y el thunk `0x41E1DE0`), sin
    ningún `::cdmp_`;
  - nuevo `cdmp_symbols.csv` con el recuento vivo: 7572 símbolos y 2865 nombres de función.
- **`disasm`:** avisa cuando la dirección pedida no es inicio de instrucción.

### 12.2 Prueba operativa (servidor vivo, proyecto real)

Hecha a las 03:23–03:27 con un cliente MCP por stdio, igual que Claude Code, más los `.ps1` y el
socket directo.

**Arranque y parada**
- **Antes:** ningún `java.exe` y ningún listener en el puerto 18490.
- **Arranque:** `ghidra_server_start` 13,1–13,5 s, de los que unos 12 s son abrir el proyecto.
- **Parada:** `ghidra_server_stop` 2,0 s. Por fin de stdin, 2,2–2,3 s.
- **Después de cada sesión:** `Get-Process java` vacío y sin listener en 18490.

**Las cinco consultas** (frío / caliente, tiempo total visto por el cliente MCP):

| Consulta | 1.ª | 2.ª |
|---|---|---|
| `decomp 0x35CA00` | 0,157 s | 0,062 s |
| `xrefs 0x1317D40` | 0,004 s | 0,022 s |
| `strings sentry` | 0,019 s | 0,011 s |
| `class NwVirtualAsyncSession` | 0,044 s | 0,049 s |
| `imports sentry_capture_event_with_scope` | 0,004 s | 0,003 s |

**Las nueve comprobaciones de la tabla 3.1 de la revisión**, ahora con consultas vivas a Ghidra:

| # | Dirección | Ghidra vivo (9-10) | ¿Coincide con el capstone de la revisión? |
|---|---|---|---|
| 1 | `0x1317D40` | Prólogo `48 89 5c 24 10` + `PUSH RBP/RSI/RDI/R12/R13/R14/R15`, `SUB RSP,0x80`; `0x1317D60 CMP byte ptr [RCX],0x0`; `0x1317D63 JZ 0x1317EF0`; epílogo con `RET` en `0x1317F0A`. Decompilado: `void cdmp_SentryReporter(char*,…)`, con `if (*param_1 == '\0') return;` | Sí, byte a byte |
| 2 | `0x2356420–39` | `CALL 0x1318E70`; `0x2356425 LEA R8,[RSP+0x30]`; `0x235642A MOV RDX,R14`; `0x235642D MOV RCX,[RAX+0x1988]`; `0x2356434 CALL 0x1317D40`; `0x2356439 INC byte ptr [0x6D9CCE8]` | Sí |
| 3 | `0x1316226–3B` | `CALL [0x51EC950]` (`sentry_init`); `0x131622C TEST EAX,EAX`; `0x131622E JNZ 0x1317B37`; `0x1316234 MOV RDI,[RBP+0xAF0]`; `0x131623B MOV byte ptr [RDI],0x1` | Sí, ahora también la secuencia posterior |
| 4 | `0x131602D` | `CALL [0x51EB418]` (`GetFileAttributesW`); `CMP EAX,-1` (vía `EBX=0xFFFFFFFF`); `TEST AL,0x10`; `0x131605C CALL [0x51EC990]` (`sentry_options_set_handler_pathw`) | Sí, ahora también el orden posterior |
| 5 | `0x44FD30` | Bucle `0x44FDB3 CMP byte ptr [RCX+R8],0`; `0x44FDC2 CALL 0x1420F60`, con `R8` = longitud; el destino hace `0x1420F77 MOV EBX,R8D`. **El decompilado sigue mostrando `FUN_141420f60(local_1028,param_1)` con 2 argumentos**: el tercero (R8, la longitud) se omite porque la firma de `0x1420F60` está `undefined` | Conversión: sí. **El defecto del tercer argumento omitido sigue**; no se corrige porque habría que escribir la firma en el proyecto |
| 6 | `0x2774076–87` | `0x277407E MOV RAX,[R15+0x88]`; `0x2774085 MOV EAX,[RAX]`; `0x2774087 MOV [R14+0x54],EAX`. Decompilado: `uVar7 = *(undefined4 *)param_3[0x11]; … + 0x54) = uVar7` | Sí: doble indirección `*(u32*)[sesión+0x88]` |
| 7 | `0x143A580` | **Decompilado nuevo** (`exports\decomp\0x143a580.c`): `cdmp_DeferredQueue_Dispatch(longlong *param_1, undefined4 *param_2, undefined8 *param_3, uint param_4)`; `if (param_4 != 0) {` elige la tabla `+0x38`/`+0x40` según `(param_4 >> 0x1c & 3)`, indexa `[tabla + (param_4 & 0xfffff) * 0x10]` **sin comprobar límites**, y si la subcola es nula devuelve `cdmp_eErrNo_DeferredQueuePush`; si no, `LockFreeQueue::Push` }, y después, siempre, el push a la FIFO común (`param_1[6]`) y `ReleaseSemaphore`. Desensamblado: `0x143A5A1 TEST R9D,R9D`; `0x143A5A4 JZ 0x143A609`; `0x143A5CC AND ECX,0xFFFFF`; `0x143A5D2 ADD RCX,RCX`; `0x143A5D8 MOV RCX,[RAX+RCX*8]` | Sí. Como dice la revisión, `0x143A5A4` es el salto y la cola común empieza en `0x143A609` |
| 8 | `0x27748D0` | Dentro de `cdmp_ServerNet__ProcessAsyncRequest` (`0x2774730`, tamaño 0x3ED): `0x27748C6 MOV RCX,[RSI+0x88]`; `0x27748CD MOV EAX,[RDX+0x54]`; `0x27748D0 CMP [RCX],EAX`; `0x27748D2 JNZ 0x2774A80` | Sí |
| 9 | `0x2773C20` / `0x35CA00` / `0x35C670` | Funciones `cdmp_ServerNet__OnRecvPacket` (0x80D), `cdmp_ClientNet__OnRecvPacket` (0x2ED) y `cdmp_ClientNet__PostPacket` (0x215). Prólogos `48 89 5c 24 10 4c 89 4c 24 20 …`, `48 89 5c 24 08 48 89 74 24 10 48 89 7c 24 …` y `48 89 5c 24 10 4c 89 4c 24 20 55 56 …`, iguales que los arrays `k…Head` de `offsets.h` | Sí |

Los nombres semánticos (`cdmp_…`) siguen siendo anotaciones del equipo, no símbolos del
fabricante.

### 12.3 Lo que queda pendiente o se acepta

- **Sin autenticación:** aceptado (ver sección 10).
- **Tercer argumento omitido en `0x44FD30` → `0x1420F60`:** es una limitación de las firmas
  `undefined`. Se corregiría poniendo la firma de `0x1420F60` en el proyecto, que es una escritura,
  y no la he hecho.
- **`resolve` toma el primer símbolo que coincide:** un nombre ambiguo puede llevar a otro
  destino. Ante la duda, usa la RVA.
- **`decomp`** decompila la función que *contiene* la dirección, que no tiene por qué empezar ahí:
  la cabecera lo indica con «pedido …, dentro de la función».
- **`vtable`** se recorre de forma heurística: el final que muestra no acredita el tamaño real.
- **Prueba de agotamiento:** no se intentó; los límites lo cortan por diseño.

### 12.4 Desmantelar (si algún día hace falta)

1. `server-stop.ps1`. Si estaba registrado, `claude mcp remove cdmp-ghidra --scope user`.
2. Las reglas de cortafuegos de Java, si quedara alguna: `Get-NetFirewallApplicationFilter |
   ? Program -like '*jdk-21.0.12.1*'`, y bórralas tú.
3. Borrar `D:\RE\CrimsonDesert_2976\` (proyecto, copias, exportaciones; hazlo solo si no quieres
   conservar las copias de seguridad) y `D:\Tools\ghidra_12.1.4_PUBLIC`, `D:\Tools\jdk-21.0.12.1+1`
   y `D:\Tools\_dl`.
4. Borrar `%APPDATA%\ghidra`, `%LOCALAPPDATA%\ghidra` y `%TEMP%\ghidra`.
5. En el repositorio solo hay `docs/research/ghidra/`.
6. No hay nada más en el sistema: ni variables globales, ni servicios, ni tareas.

### 12.5 Segunda revisión y ajustes de la sesión principal (9-10, 04:00)

Segunda revisión independiente (solo lectura): **conectar con condiciones**. Los tres bloqueantes
están corregidos; no hay ejecución remota de código, ni escrituras en el proyecto, ni acceso desde
la red. Ajustes hechos por la sesión principal en `server-stop.ps1` y `_env.ps1` (copias idénticas
en el repositorio y en `D:\RE`):

- **F2:** la parada forzada exige que el id de instancia que devuelve el `ping` tenga el formato
  propio (32 hex) y que la línea de comandos del `java.exe` contenga ese id **y** `Cdmp_Query`.
- **F1 (parcial):** `server-stop.ps1 -Instance <id>` para también un servidor propio **colgado** (que
  no responde al `ping`): para los procesos que llevan ese id en la línea de comandos.
- **F5:** anclas `\z` en vez de `$` en el filtro de argumentos y en el formato del id.
- Pendiente (solo afecta al cliente MCP, que no se ha registrado): F1 en Java (plazo de escritura) y
  F3 (registrar la instancia antes de esperar el arranque en `cdmp_mcp.py`). Mientras tanto, la
  sesión principal usa los `.ps1` y comprueba `Get-Process java` al parar.

## Rehacerlo desde cero (con otro exe)

1. Copiar el exe a `bin\`, calcular su SHA-256 y comprobar el timestamp.
2. Importar: `analyzeHeadless <proyecto> <nombre> -import bin\CrimsonDesert.exe -noanalysis` (≈1 min).
3. Revisar la tabla de secciones: los nombres de Denuvo pueden cambiar. Ajustar
   `Cdmp_PreAnalysis.java` y `Cdmp_RttiFix.java` si cambian.
4. `scripts\run_analysis.ps1` (≈40 min).
5. `Cdmp_RttiFix.java` (≈2,5 min), `Cdmp_DefineStrings.java` (≈10 s) y, con RVAs **de esa build**,
   `Cdmp_ApplyNames.java`.
6. `Cdmp_Export.java` (≈25 s).

Todos con `-process CrimsonDesert.exe -noanalysis -scriptPath scripts -postScript <script>`, y
`-readOnly` para los que no modifican.
