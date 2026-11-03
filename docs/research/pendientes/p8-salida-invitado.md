# P8: salida del invitado sin anfitrión y fin del proceso tras un cierre

Rama `opcion-d`, desde `9a2266b`. Build del juego 1.0.0.2976, base `0x140000000`; las direcciones son RVA.
No se ejecutó el juego ni se tocó `bin64`. Análisis estático de solo lectura con capstone sobre
`D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` (scripts en el scratchpad de la sesión: `pe_dis.py`,
`xref.py`). **[V]** verificado (log, volcado, bytes, prueba); **[E]** inferencia.

Responde a `p8-sesion6.md` §6.4 y §7.2 #7 / §7.3 «Salida del invitado sin anfitrión».

## 0. Resumen

1. **Despedida del invitado (2001) sin anfitrión:**
   - **Sin campo del anfitrión** (el título; el cliente no ha mandado su 3093): igual que antes, el mod
     responde en local con 2003 + 2002. Eso arregló el cuelgue del título (sesión 1).
   - **Con campo del anfitrión** (el 3093 salió hacia el anfitrión): el mod **ya no responde en local**.
     Si el anfitrión no puede contestar, escribe una línea y **termina el proceso**
     (`TerminateProcess`, código de salida `0x2001`). Nunca se llega al desmontaje del campo sin su
     usuario dueño.
2. **Captura de cierres (`guest_crash_dump=1`):** sin `crashpad_handler.exe` junto al juego (K2), tras
   escribir historial y volcado de una **violación de acceso no manejada**, el filtro termina el proceso
   con el código de la excepción, en vez de encadenar al filtro de la CRT, que la pasa al informe de
   errores de Windows (proceso vivo 5 min 40 s en la sesión 6).
3. **Corrección de un [E] anterior** (`p8-sesion6.md` §6.3, `p8-hilo-red.md` §4.5): el filtro que
   sustituía al nuestro, `exe+0x4896E00`, **no es crashpad**. Es el `__scrt_unhandled_exception_filter`
   de la CRT de MSVC **[V]** (bytes, §3.1).
4. Build y pruebas: `.\scripts\build.ps1`, **20/20** (§5).

## 1. Evidencia: cuatro salidas reales

| Caso | Campo del anfitrión | Anfitrión | Qué pasó | Fuente |
|---|---|---|---|---|
| Sesión 1 (14:46–14:47), título | no (`out 3093 0`) | perdido (túnel caído 14:46:25) | Sin respuesta a la 2001, el cliente se quedó en la pantalla de carga hasta Alt+F4. Motivo de `answer_goodbye` (`9333be3`) | `backups/logs/shadow_20261009-1440_p8c_full.log` **[V]** |
| Sesión 5 (19:47), carga `0x11` | sí (`out 3093 1`) | perdido (reconexiones) | Alt+F4 → 2001 → respuesta local → AV en `0x2045A6A` 100 ms después | `backups/p8-sesion5-shadow/cdmp.log`, `p8-hilo-red.md` §4.1 **[V]** |
| Sesión 6A (22:44), carga `0x11` | sí (`out 3093 1`) | **vivo** | Alt+F4 (22:44:11.306) → 2001 (13.531) → el servidor real manda `RemoveActorAck`, `ClearAllyGroupDataAck`, `RefreshMercenaryDataAck` y **5 285 `UnloadSyncSceneObjectAck`** (t 213.298 → 213.666) → 2003/2002 (13.938) → `Terminate 시작` (14.190). **Sin cierre** | `backups/p8-sesion6A-shadow/cdmp.log` y `cdmp_packets_20261009-224011.log` **[V]** |
| Sesión 6B (22:52), en el mundo | sí (`out 3093 1`) | perdido (22:52:44.333) | Alt+F4 (55.539) → 2001 (57.518) → respuesta local 2003/2002 (57.534) → AV en `0x2045A6A` (57.556), `r8 = 0x9010031E` (el usuario 2 propio) → volcado (58.412) → proceso vivo hasta 22:58:37 | `backups/p8-sesion6B-shadow/cdmp.log` y el `.dmp` **[V]** |

- **[V]** El 3093 distingue exactamente los casos: 0 en el único caso que la respuesta local arregla, 1
  en los dos que cierra.
- **[V]** En los cuatro casos la 2001 llega 2 s después del evento de salida (`종료 이벤트 발생`, «ha
  ocurrido el evento de salida»). Tras la 2002, el juego ejecuta `Terminate 시작` («empieza Terminate»)
  y el proceso termina (6A y sesión 1, 14:31:49).
- **[V]** (`p8-sesion6.md` §6.3) La respuesta local borra el usuario local, que el 2174 reescrito hizo
  dueño del campo. 22 ms después, el procesador de escenas (`ClientField::vf40 0x9A5B70` → nulo →
  `0x2045A6A`) lo busca.
- **[E]** El orden nativo vacía el campo antes de borrar el usuario. La respuesta local no puede
  reproducirlo: no conoce los objetos y no puede inventar los Ack.

## 2. Decisión para la despedida

### 2.1 Regla (`guest_goodbye_logic.h`, `goodbye::decide`)

Con una 2001 pendiente y sin 2002 recibida:

| Campo del anfitrión | Condición | Acción |
|---|---|---|
| no | enlace roto, o 5 s desde la 2001 | `AnswerLocally`: 2003 + 2002 locales, **sin cambios** respecto a `9333be3` |
| sí | enlace roto (`g_broken`) | `EndProcess` en el acto |
| sí | ≥ 5 s desde la 2001 **y** ≥ 5 s sin ningún paquete del servidor del anfitrión | `EndProcess` |
| sí | ≥ 30 s desde la 2001, aunque sigan llegando paquetes | `EndProcess` |
| sí | en otro caso | `Wait`: se siguen entregando los paquetes del anfitrión (su descarga del campo) |

- «Campo del anfitrión» = `g_n.out_3093 != 0`: el cliente mandó al anfitrión su
  `LoginCharacterToFieldServerReq` 3093 en esta conexión remota (solo hay una por partida).
  - Es conservador: un 3093 que el anfitrión rechazara cuenta igual. El coste en ese caso es cerrar en vez
    de volver al título.
- La espera con campo tiene en cuenta el último paquete del servidor del anfitrión (`g_last_in_ms`, en
  `guest_deliver`, sin contar los de control del túnel). En 6A la descarga duró 0,4 s en local. Por el
  túnel puede tardar más y no debe cortarse a los 5 s si sigue llegando.
- `EndProcess` (`end_without_host`, `guest.cpp`):
  - escribe **una línea ERR** con el motivo, el tiempo desde la 2001, el puntero Q del estado de carga
    (`[[[exe+0x6D69190]+0x60]+0xC0]`), `[P+0x15C]` y la antigüedad del último paquete;
  - después llama a `TerminateProcess(GetCurrentProcess(), 0x2001)` desde el NetworkThread (el gancho de
    `guest_pump`);
  - si `TerminateProcess` fallara (no esperado), lo registra y el cliente sigue esperando su 2002: el
    cuelgue antiguo, nunca el cierre.

### 2.2 Por qué esta y no otra

- **No responder y nada más:** el cliente se queda en la pantalla de carga (sesión 1) y el usuario tiene
  que matar el proceso. Es seguro, pero peor que terminar.
- **Vaciar primero el campo con Ack inventados:** descartado. Exigiría fabricar `RemoveActorAck` y
  `UnloadSyncSceneObjectAck` de objetos que el mod no conoce, y la regla es no inventar paquetes.
  - Tampoco se encontró un camino **nativo local** de descarga en el cliente. No se buscó más: la salida
    ya iba a cerrar el juego.
- **Volver al título por el camino nativo de «servidor desconectado»:** no está localizado (`p8-sesion6.md`
  §6.4). Además, esta partida ya no puede volver a conectarse al anfitrión (`g_had_remote`: «restart the
  game to play on the host again»), así que el título no sirve de mucho.
- **Contención en `0x2045A5D`:** sería un gancho de comportamiento en un camino del juego. Solo tapa este
  nulo; otros lectores del dueño del campo podrían fallar después. Necesita una decisión explícita.
- **Terminar:** es lo que el juego hace de todos modos tras la 2002 (`Terminate 시작`, §1).
  - **[E]** No se pierde partida. En modo remoto, el guardado lo hace el servidor del anfitrión: el
    servidor local del invitado no ejecuta `UserLogin` (`p8-titulo.md`).
  - **[V]** Las líneas del log ya están en la caché del sistema (`log.cpp`: `WriteFile` por línea) y
    sobreviven a `TerminateProcess`.
  - **[V]** El registrador de paquetes vacía su búfer cada 10 s (`troc_sniffer.cpp`, `stats`). Se pueden
    perder sus últimos segundos.

### 2.3 Casos límite

- **«Volver al título» desde el menú, con el anfitrión perdido y un campo cargado:** también produce una
  2001. Ahora el juego **se cierra** en vez de ir al título. Es intencionado: ese título no puede volver
  a conectarse al anfitrión en esta partida.
- **Anfitrión vivo:** su 2002 llega y `g_goodbye_answered` se pone a `true` en `guest_deliver` (`note_in`).
  Nunca se termina (6A: `Wait` en toda la descarga, probado en §5).
- **Pérdida del anfitrión sin salida del usuario:** no cambia nada. El cliente sigue en el mundo congelado
  hasta que el usuario sale; entonces se aplica la tabla.
- **Modo observe/local:** `guest_pump` vuelve antes si la conexión no es remota. Esta lógica no corre.

## 3. Captura de cierres: terminar en vez de encadenar

### 3.1 Qué filtro había detrás del nuestro **[V]**

- La línea de 6B: `the top-level exception filter had been replaced by CrimsonDesert.exe+0x4896E00`.
- `0x4896E00` (`.rsrc`):
  - compara `[rec] == 0xE06D7363` (excepción C++), `NumberParameters == 4` y la firma
    `0x19930520`–`0x19930522` / `0x1994000`;
  - si coincide, `0x4896E49` llama a `0x4898E60` y `0x4898E66` (excepción y contexto actuales) y a
    `0x4898EB4` (`terminate`);
  - **si no, `xor eax,eax; ret` = `EXCEPTION_CONTINUE_SEARCH`**.
- `0x4896DF0`: `lea rcx,[0x4896E00]; jmp [0x51EB140]`. La entrada de la IAT `0x51EB140` es
  `KERNEL32!SetUnhandledExceptionFilter`. Es `__scrt_set_unhandled_exception_filter`; su único llamador
  está en `.xtls` (`0x1680D76E`, solo lectura).
- **[E]** Los nombres de la CRT se deducen del patrón, que es el de `__scrt_unhandled_exception_filter`.
- Así, una AV pasaba por nuestro filtro (captura), luego por el de la CRT (`CONTINUE_SEARCH`), y después
  Windows la trataba con su informe de errores. **[E]** Según la configuración de WER, puede mostrar un
  diálogo o tardar en recoger; el proceso sigue vivo mientras tanto. En 6B fueron 5 min 40 s; en la
  sesión 5, unos 25 s.
- Sin `crashpad_handler.exe`, `sentry_init` falla (`opcion-d-rpt0.md`): ningún informe del juego
  interviene.

### 3.2 Cambio (`guest_s3.cpp`, `guest_s3_logic.h`)

- `s3::uef_end(code, crash_dump_on, crashpad_present)` devuelve `Terminate` solo si se cumplen las tres:
  - `guest_crash_dump=1` (nuestro filtro solo existe entonces; se usa la clave, `g_crash`);
  - **no** hay `crashpad_handler.exe` junto al exe del juego (`GetModuleFileNameW(nullptr)` y
    `GetFileAttributesW`, comprobado en el arranque y otra vez en el momento del cierre);
  - el código es `0xC0000005`.
- En `crash_uef`, tras `capture()`:
  - si la decisión es `Terminate`, `end_after_capture` espera (como mucho 35 s) a que termine la captura
    de **otro** hilo, si había una;
  - escribe **una línea ERR** con el código, la dirección y el filtro al que no se encadena;
  - llama a `TerminateProcess(GetCurrentProcess(), código)`.
- Si la captura ya se hizo antes para esa dirección (por el manejador vectorizado, en `0x232D5FF`), se
  termina igual: el volcado ya existe.
- Cualquier otro código (excepción C++, desbordamiento de pila, fail-fast, punto de ruptura), o con
  `crashpad_handler.exe` presente: **se encadena como antes**.
- Ninguna excepción se traga en silencio. Antes de terminar siempre hay una línea, y el código de salida
  es el de la excepción.
- `CrashGate::busy()` es nuevo (solo lectura del indicador).
- Línea nueva en el arranque: `GuestS3 crash capture: crashpad_handler.exe is NOT next to the game (K2)
  -> an unhandled access violation ENDS the process right after the capture …` (o `is next to the game
  -> … is chained …`).

### 3.3 Riesgos

- Una AV no manejada termina el proceso de todos modos. Lo único que se cambia es **quién** lo termina y
  **cuándo**: enseguida, sin WER.
- Los informes de WER (`CrashDumps`, `Application Error`) de esa AV dejan de existir en el invitado. Nuestro
  `.dmp` y el log sustituyen al volcado de WER.

## 4. Código

| Archivo | Qué |
|---|---|
| `src/mod/session2/guest_goodbye_logic.h` (nuevo) | `goodbye::decide`, `end_reason`, tiempos (`kAnswerMs` 5 s, `kSilentMs` 5 s, `kFieldCapMs` 30 s) |
| `src/mod/session2/guest.cpp` | `g_last_in_ms`, `g_goodbye_ending` y `kGoodbyeExitCode` (l. 83–90); `end_without_host` (l. 738); `guest_pump` usa `goodbye::decide` (l. 772–796); `guest_deliver` sella `g_last_in_ms` (l. 976). `answer_goodbye` no cambia |
| `src/mod/session2/guest_s3_logic.h` | `CrashGate::busy()` (l. 323); `UefEnd`/`uef_end` (l. 330–346) |
| `src/mod/session2/guest_s3.cpp` | `crashpad_present`, `end_after_capture`, `crash_uef` (l. 1086–1143); comentario de `rearm_crash_filter`; línea del arranque (l. 1218) |
| `src/mod/session2/guest_s3.h` | documentación de `guest_crash_dump` |
| `tests/optd_s3_tests.cpp` | `test_uef_end`, `test_goodbye` y `busy()` en `test_crash_gate` |

- Sin cambios de configuración (`config.*`, `mod.cpp`) ni en `CMakeLists.txt`.
- Sin ganchos nuevos: ningún byte del juego cambia.

## 5. Pruebas **[V]**

`.\scripts\build.ps1` (RelWithDebInfo): compila; **20/20** pruebas (`optd_s3 tests passed`). El único aviso,
C4100 `net` en `try_refresh` (`guest.cpp`), ya existía.

- `test_goodbye`, sobre las cronologías reales:
  - nada pendiente o ya respondido → `Wait`;
  - título (sesión 1): roto → respuesta local al instante; callado → a los 5 s, no antes; los paquetes no
    cuentan;
  - 6B (campo, roto) y sesión 5 → `EndProcess` al instante, nunca respuesta local;
  - 6A (campo, anfitrión vivo, descarga de 0,4 s y 2002) → siempre `Wait`;
  - anfitrión lento: descargando a los 10 s → `Wait`; 5 s callado → `EndProcess`; sin nada tras la 2001,
    a los 5 s exactos; tope de 30 s;
  - paquete sellado después de leer el reloj (otro hilo) → nunca «callado» por desbordamiento;
  - textos del motivo.
- `test_uef_end`:
  - AV con captura y sin crashpad → `Terminate`;
  - con crashpad, con la captura desactivada, o con `0xE06D7363`, `0xC00000FD`, `0x80000003`,
    `0xC0000409` → `Chain`.
- `test_crash_gate`: `busy()` durante `Go` y libre tras `leave`, `Repeat` y `Spent`.
- No prueban los ganchos ni el comportamiento del juego.

## 6. Cómo verificarlo

### 6.1 En un PC

- **Invitado local** (`docs/research/ini/cdmp.p8-guest-local.ini` como `bin64\cdmp.ini`; modo observe,
  `guest_crash_dump=1`, `crashpad_handler.exe` apartado):
  1. Arrancar, entrar en la partida y salir con Alt+F4.
  2. En `cdmp.log`, tras `GuestS3 crash capture ON …`, debe aparecer `GuestS3 crash capture:
     crashpad_handler.exe is NOT next to the game (K2) -> an unhandled access violation ENDS the process
     right after the capture …`.
  3. La salida debe ser normal: `Terminate 시작` del juego y ninguna línea `the game ENDS NOW` ni
     `answered locally`. En observe no corre la lógica de despedida.
  4. Con `crashpad_handler.exe` en su sitio, la línea dice `is next to the game … is chained …`.
     Para no tocar K2, basta con no hacerlo.
- **Anfitrión con `cdmp_peer`** (`cdmp.p8-local.ini`): no ejercita nada de esto. Este cambio solo está en
  el invitado real y no toca los archivos del anfitrión.
- En un PC no se puede provocar la AV no manejada ni la despedida remota sin anfitrión: no hay clave de
  prueba para ello (§7).

### 6.2 En la próxima sesión con Shadow

Kit con esta build en los dos PC; en Shadow, `cdmp.p8-2322-shadow.ini` o `cdmp.p8-shadow-full.ini`
(`guest_crash_dump=1`) y `crashpad_handler.exe` apartado.

1. **Salida normal con el anfitrión vivo** (repite 6A):
   - el invitado, en el mundo o cargando, sale con Alt+F4;
   - esperado: `out 2001`, `in 2003`, `in 2002 … equal`, `Terminate 시작`;
   - **ninguna** línea `ENDS NOW` ni `answered locally`, y ningún `.dmp`.
2. **Anfitrión perdido con el invitado en el mundo** (repite 6B):
   - cerrar el juego del anfitrión (o su red) con el invitado en el mundo;
   - esperar a `the tunnel to the host went down`;
   - en el invitado, Alt+F4;
   - esperado: `out LogoutSessionToServerReq 2001` y, en la misma vuelta de red, `ERR … the client said
     goodbye (2001) N ms ago with a field of the host … the connection to the host is broken … the game
     ENDS NOW (TerminateProcess, exit code 0x2001) | field Q 0x… loading state 0x…`;
   - el proceso desaparece en menos de 1 s;
   - **no** deben aparecer `answered locally`, la AV en `0x2045A6A` ni un `.dmp`.
3. **Anfitrión perdido en el título del invitado** (repite la sesión 1, no regresión):
   - el invitado conecta y se queda en el título, sin «Jugar»;
   - perder el anfitrión y salir;
   - esperado: `answered locally with 2003 …` y `… 2002 …`, y el juego termina normal (`Terminate 시작`).
4. **Si hubiera otra AV no manejada en el invitado:**
   - esperado: `GuestS3 crash capture (unhandled, top-level filter) …`, el historial, `minidump … written` y
     después `GuestS3 crash capture: unhandled exception 0xC0000005 at …: the process ENDS NOW
     (TerminateProcess …)`;
   - el proceso desaparece en pocos segundos (antes: 25 s – 5 min 40 s);
   - sin `Application Error` de WER para ese PID.

## 7. Pendiente

- **Clave de prueba opcional para un PC** (no implementada: tocaría `config.*`, `mod.cpp` y
  `guest_start`). Por ejemplo, `guest_crash_test=1`, que provoque una AV no manejada en un hilo del mod a
  los N s, para comprobar §3 sin Shadow.
- **Camino nativo de «servidor desconectado → título»** en el cliente. Solo hace falta si se quiere
  volver al título sin reiniciar el juego, y eso también exige reconectar con el anfitrión en la misma
  partida (hoy, `g_had_remote`).
- **Volcado del registrador de paquetes antes de terminar**, si hacen falta sus últimos segundos.
  Necesitaría exponer un `flush` en `troc_sniffer`.

## Correcciones tras la revisión de la noche (2026-10-10)

Detalle completo en `p8-revision-noche.md` §6.

- **`[session2] guest_goodbye_end`**, **1 por defecto**: terminar el proceso tras el adiós con un campo del
  anfitrión cargado.
  - Es una excepción razonada a la regla de claves apagadas: con 0 se vuelve al cierre conocido de
    `exe+0x2045A6A`, que se envía a Pearl Abyss.
  - 0 = nunca `TerminateProcess` ni 2003/2002 local: el cliente sigue esperando su 2002, con una línea `WRN`, y lo
    que llegue del anfitrión se sigue entregando.
- **`crash_uef` ya no asigna memoria tras un cierre:**
  - `crashpad_present()` se calcula una vez al instalar;
  - la línea «ENDS NOW» se escribe con `log::emergency`.
- El gancho del NetworkThread del invitado (`0x35BAD4`) se instala con `p8_install_mid`.
