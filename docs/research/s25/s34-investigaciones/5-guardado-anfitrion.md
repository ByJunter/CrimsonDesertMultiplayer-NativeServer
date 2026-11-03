# S34: el progreso del anfitrión no se guarda (caja de partidas) y diseño para guardarlo de verdad

14 de octubre de 2026. Worktree `C:\Emulador CD-npc-s33-fix`, rama `opcion-d-npc-s33-fix`, HEAD `d835806`. Build del
juego **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`). **Solo lectura:** no se tocó código, no se compiló, no se
desplegó, no se lanzó el juego; de las carpetas de partidas solo se listaron nombres, tamaños y fechas.

Marcas: **[V]** visto (disco, log); **[E]** leído en código del mod o en el exe (documentos previos); **[S]** diseño o
inferencia; **[H]** a medir en vivo.

---

## 0. Respuestas cortas

1. **Lo que ve el usuario es lo esperado del modo actual [V].** Con `save_guard=redirect` (sesión 24) *todo* lo que
   el juego del anfitrión escribe bajo `...\CD\save` va a `...\CD\cdmp_save_sandbox`. La caja se **rehace desde la
   carpeta real en cada arranque** (la anterior pasa a `_prev` y la `_prev` anterior se borra). La carpeta real no se
   escribe nunca, así que cada sesión empieza desde la misma partida real y lo que el anfitrión guardó se pierde dos
   arranques después. El progreso del invitado sí se conserva porque sale de la caja hacia su PC (ranura 204 →
   buzón → `SAVE_RESULT`).
2. **No es que se cargue una caja vieja.** La caja es una copia recién hecha de la real (lecturas y consultas también se
   desvían, pero a esa copia). Lo viejo es la carpeta real: `slot1\save.save` tiene fecha de escritura **3-10-2026
   15:58** (la carpeta se recreó el 9-10 a las 15:35, seguramente una restauración). Cada sesión con el mod carga ese
   `slot1` [V: `LoadSlot ... = the host | slot 1 -> pass 1` en los tres logs de hoy].
3. **Por qué se introdujo:** P6 (`save_guard=1`, bloqueo) protegía las partidas del anfitrión de que el servidor
   **serializara al usuario 2 en una ranura del anfitrión**. El temporizador 2011/2077 pide autoguardado también para
   el usuario 2, y la ranura la elige el gestor global (rotación 0-2), no el usuario. El bloqueo provocaba el bucle
   "Failed to save data" cada 10 s; la sesión 24 lo cambió por el **desvío a una caja**. Además, la caja protege el
   progreso real del usuario durante los experimentos (es una seguridad de pruebas).
4. **El riesgo original ya está cubierto en origen [V/E].** Con `guest_own_save=1` (sesión 25), el gancho de
   `SavePlayerToSlot 0x186E450` manda todo guardado del usuario 2 a la ranura 204 o lo rechaza (falla cerrado), y deja
   pasar siempre los del anfitrión (logs de hoy: autoguardados del anfitrión a `slot0` y `slot2`; el 2910 del usuario
   2 se reasigna a la 204). La caja sigue siendo necesaria para la ranura 204 y como prueba, pero ya no hace falta para
   evitar que el usuario 2 escriba en las ranuras del anfitrión.
5. **Propuesta:** `[session2] host_save = sandbox | probe | real` (por defecto `sandbox`, el comportamiento de hoy).
   Con `real`, el juego sigue viendo solo la caja (nada cambia en el desvío ya probado), y **tras cada guardado
   correcto y verificado del anfitrión** el mod publica esa ranura en la carpeta real con la misma secuencia de
   renombrados que usa el juego, tras una copia de seguridad. Nunca publica la 204 ni ninguna ranura que haya escrito
   otro usuario. Detalles en §5.

---

## 1. Estado actual en disco y en los logs [V]

Listado de solo lectura (`%LOCALAPPDATA%\Pearl Abyss\CD`), cuenta `<id-cuenta>`:

| Carpeta | Contenido (`<cuenta>\`) | Fechas |
|---|---|---|
| `save` (real) | `slot1` (lobby 506 B, save 1 944 571 B), `slot108` (518 / 736 731 B, plantilla Nexus), `steam_autocloud.vdf` | `slot1\*.save` escritos el **03-10 15:58** (carpeta creada el 09-10 15:35); `slot108` 08-10 22:36 |
| `cdmp_save_sandbox` (sesión de las 19:02) | `slot1`, `slot108` (iguales que en la real) + **`slot204`** (512 / 1 896 074 B) | `slot204` 14-10 19:07:31 (el 2910 del usuario 2) |
| `cdmp_save_sandbox_prev` (sesión de las 18:49) | lo mismo + `slot204` (512 / 1 896 445 B) | `slot204` 14-10 18:56:41 |
| `cdmp_guest_save_staging\<id-cuenta>` | vacía | 14-10 18:51 |

Ninguna de las tres carpetas tiene `slot0`/`slot2`: **no queda ningún guardado del anfitrión posterior al 3 de
octubre**.

Logs del anfitrión de hoy (`bin64\cdmp_20261014-*.log`, `save_guard=redirect`, `guest_own_save=1`):

| Sesión | Guardados del anfitrión | Destino | Qué queda |
|---|---|---|---|
| 18:03 | autoguardado 18:21:16 (`slot 0 options 0x4 -> pass 0 (the host)`), autoguardado 18:36:16 (`slot 2`) | caja: `Session2 save redirect #31-#62 ... slot0_new/slot2_new ...` | esa caja pasó a `_prev` a las 18:49 y **se borró** al arrancar a las 19:02 |
| 18:49 | ninguno (solo `FlushPendingSaveData` de salida, `pending -1`, sin E/S) | — | `_prev` actual (solo `slot204`) |
| 19:02 | ninguno | — | caja actual (solo `slot204`) |

En las tres sesiones el usuario 2 cargó su propia partida (`REMAP 204`, `lobby-ok save-ok`) y su 2910 final fue a la
204. Su `FlushPendingSaveData` de salida no tenía nada pendiente (`pending slot +0x5C before -1`) [V].

---

## 2. Por qué existe el desvío (historia)

| Paso | Dónde | Motivo |
|---|---|---|
| P6, `save_guard=1` (bloqueo) | `src/common/save_guard.h` (cabecera), `docs/OPCION-D-EMULADOR.md` §4.6 | "A second ServerUserActor could end up serialised into the host's save slot": el autoguardado 2011→2077 incluye al usuario 2, la ranura sale de `0x1871850` (rotación global 0-2) → escribiría al personaje del invitado en un autoguardado del anfitrión (`opcion-d-p9.md` §5.1, riesgo P9-2; `p8-sistemas-juego.md` §5.2, R9). También la ranura 3 (CrossSave: subiría por HTTP a Pearl Abyss, P9-3) |
| Efecto secundario | `p8-sistemas-juego.md` §5.2 | el juego recibe `ACCESS_DENIED`, reintenta cada 10 s y muestra "Failed to save data"; la memoria del almacén (`fileObj`) de esa ranura se queda con datos del usuario 2 hasta el siguiente guardado del anfitrión |
| S24, `save_guard=trace` | `save_hooks.cpp` | solo registrar la secuencia real del juego (traza del 11-10) |
| S24, `save_guard=redirect` | `docs/research/pendientes/p8-guardado-desviado.md` | no rechazar sino **desviar toda la carpeta** a una copia: sin avisos y con la carpeta real intacta. §5 de ese documento ya lo decía: "El progreso del anfitrión en una sesión P8 se queda en la caja" |
| S25, `guest_own_save` | `guest_save_host.{h,cpp}`, `noche/progreso-invitado.md` | la ranura propia del invitado (204) vive en la caja; **exige `redirect` sano** (si no, se apaga con `forced OFF`) |

Así que el desvío protege de tres cosas: (a) que el usuario 2 entre en las ranuras del anfitrión (hoy ya lo impide
`guest_own_save=1`), (b) CrossSave (lo impide el remapeo/rechazo, que actúa antes de la rama de la ranura 3), y (c) la
seguridad de las pruebas: un fallo del mod no estropea la partida real.

---

## 3. Qué desvía exactamente [E]

**No engancha funciones del juego: desvía API de ficheros de KernelBase.** `save_hooks.cpp` (`save_hooks_install`,
línea 964) crea ganchos inline (safetyhook) en `CreateFileW/A`, `CreateFile2`, `DeleteFileW/A`, `MoveFileExW`,
`MoveFileWithProgressW`, `CopyFileW/ExW`, `CopyFile2`, `SetFileAttributesW/A`, `RemoveDirectoryW`,
`CreateDirectoryW/A/ExW`, `ReplaceFileW` y las consultas `FindFirstFileW/ExW`, `GetFileAttributesW/ExW`. Cualquier
ruta (de cualquier hilo y cualquier función) que resuelta caiga bajo `save` se reescribe a la misma ruta bajo la caja
(`saveguard::Redirector`, `redirect_path`). Escrituras, lecturas y consultas. La caja se prepara en `prepare_sandbox`
(línea 574) antes de crear ningún gancho: `cdmp_save_sandbox` → `_prev`, copia completa de `save`.

Las funciones del juego que nombra la pregunta las engancha **`guest_save_host.cpp`**, no el desvío:

| RVA | Función | Gancho | Qué hace con el anfitrión |
|---|---|---|---|
| `0x186F570` | `LoadSlot(mgr, &res, slot, actor)` | entrada (`load_detour`) | pasa siempre (salvo si pide la 204) |
| `0x186E450` | `SavePlayerToSlot(mgr, &res, slot, pj, &opts)` | entrada (`save_detour`, línea 870) | pasa siempre (salvo la 204); el usuario 2 → 204 o rechazo |
| `0x1871930` | `FlushPendingSaveData(mgr, &res, pj, counter, &opts, &slot)` | entrada (`flush_detour`, línea 938) | solo sonda |
| `0x20688A0` | `LoadComponentsFromFile` (gancho compartido con C2) | entrada/salida | registro y comprobación de la 204 |

Llamadores de `0x186E450` (los dos únicos [E]): autoguardado `.xtls 0xCBD86CE` (vuelta `exe+0xCBD86D3`) y 2910
`.xtls 0x103BE99A` (vuelta `exe+0x103BE99F`). La escritura en disco de un guardado ocurre **dentro** de esa llamada,
en el hilo del servidor: dos *commits* seguidos (lobby y save) con `slotN_backup`, `slotN_new`, `CreateFileW
slotN_new\<f>.save`, `MoveFileExW slotN_new → slotN`, `slotN_backup → slotN_backup_<marca>_delete` y borrado [V traza del
11-10 y log de hoy 18:21:16.821-.929]. El `FlushPendingSaveData` del temporizador 5 s después no hizo E/S [V].

---

## 4. Qué hay en la partida del anfitrión y qué podría traer el usuario 2

`0x186E450` serializa **dos actores del anfitrión**: su `ServerUserActor` (`[pj+0xA0]` → `lobby.save`) y su personaje
(`pj` → `save.save`), recorriendo el vector de componentes de cada uno (`vision-a3-guardado-mundo.md` §1.2, §2.1). No
recorre otros usuarios. El mundo va dentro de componentes mixtos del personaje (ContentsMisc: `FieldSaveData`,
gimmicks, NPC, vivienda, eventos; Quest; Sequencer; TransformSync con la tabla de hora; Store; Faction…).

| Vía de contaminación | Estado con `guest_own_save=1` | Medida en el diseño |
|---|---|---|
| Guardado del usuario 2 en una ranura del anfitrión (0-2 por rotación, 100-108 por 2910) | **cerrada** [V/E]: remapeo a 204 o rechazo antes del original; sin identificar → rechazo | además, solo se publica lo escrito dentro de una llamada **del anfitrión** (§5.3) |
| `fileObj` de una ranura del anfitrión con datos del usuario 2 (P9 §5.1) | cerrada: el original nunca corre para el usuario 2 sobre esas ranuras | ídem; el guardado del anfitrión reescribe los dos ficheros |
| `FlushPendingSaveData` de la salida del usuario 2: confirma `+0x5C` (ranura del anfitrión) y `PostSave` pone en la tabla del gestor el resumen del usuario 2 | tabla en memoria, no fichero [E]; hoy `pending -1` [V] | escrituras en ranuras del anfitrión fuera de una llamada del anfitrión → ranura "manchada", no se publica hasta otro guardado limpio |
| Inventario, equipo, habilidades, conocimiento, dinero del usuario 2 | en sus propios componentes (otro actor) | no viajan; comprobación funcional en §6 |
| Estado del mundo cambiado por las acciones del invitado en el campo compartido (enemigos, cofres, gimmicks, misiones con `quest_policy=1`) | **es progreso del anfitrión por diseño** (regla del proyecto del 13-10, `politica-misiones.md`: el progreso de misiones y del mundo sigue al del anfitrión) | se guarda; documentarlo. Con `kill_loot_per_user=1` el botín de lo que mata el invitado es suyo, no del anfitrión |
| Globales que pisa la **carga** de la partida del invitado: precios (`TradeItemPriceRate`), eventos globales (`GlobalGameEventSaveData`), `_currentRestoreCodeVersion`, hora (TransformSync `+0x5D0`) | **[S] abierto** (`progreso-invitado.md` §3.10, `vision-a3` §2.2): si la carga del usuario 2 los escribe en objetos compartidos, el siguiente guardado del anfitrión los persistiría. La hora la contiene `clock_gate` | medirlo antes de confiar (§8, M2); no lo resuelve ninguna capa de ficheros |
| Estados transitorios creados por el mod en el mundo del anfitrión (NPC controlados por el usuario 2 y devueltos, U-e; contención de NPC S33; caballos/monturas) | [H] | copias de seguridad para volver atrás; observar en la prueba larga |
| Identidad: `characterNo` F002 del usuario 2 (C2), claves de actor | solo en el actor del usuario 2 [E] | — |

---

## 5. Diseño para jugar de verdad

### 5.1 Clave del ini

```
[session2]
; ANFITRION. sandbox (por defecto) | probe | real
;   sandbox: como hasta ahora; nada llega a la carpeta real (modo de pruebas).
;   probe:   como sandbox, y una linea por cada guardado del anfitrion que se publicaria.
;   real:    tras cada guardado correcto del anfitrion, esa ranura se publica en la carpeta real (con copia).
; Necesita save_guard=redirect sano y guest_own_save=1 instalado; si no, se queda en sandbox (una linea).
host_save=sandbox
; Opcional: cuantas copias de ranura sustituida se guardan en cdmp_host_backups (por defecto 20; las de sesion, 10 fijas).
host_save_keep=20
```

Se lee en `config.cpp` junto a `save_guard` (línea 162) y `guest_own_save` (línea 331); no cambia el protocolo
(`kVersion` igual). El invitado no lo usa (en él no se instala el guardián). Los ini de prueba del kit se quedan en
`sandbox`; un ini "de jugar" lleva `real`.

### 5.2 Principio

- El juego **sigue viendo solo la caja** toda la sesión: el desvío de la sesión 24, el `slot204` de la 25 y sus
  comprobaciones no cambian. No se propone un desvío selectivo (ranuras del anfitrión a la real, 204 a la caja):
  partiría la vista del juego en dos carpetas (`FindFirstFileW save\<id>\*` vería una y `slot204` otra), cambiaría la
  capa más probada, y un fallo de clasificación escribiría directamente en la real (falla abierto).
- La carpeta real la escribe **solo el mod**, solo con ranuras que acaba de escribir un guardado del anfitrión
  verificado, y por las mismas operaciones atómicas (renombrados en el mismo volumen) que usa el juego.
- Falla cerrado: ante cualquier duda la ranura no se publica y el progreso queda en la caja (como hoy).

### 5.3 Qué se publica (puerta de procedencia)

Una ranura N se publica solo si se cumplen todas:

1. `host_save=real` activo (requisitos de §5.1 cumplidos en la instalación).
2. La llamada es `SavePlayerToSlot 0x186E450` con `owner == Owner::Host` (`owner_of_user`, hechos positivos), acción
   `Pass`, y devolvió `0`.
3. N está en la lista blanca: **0, 1, 2, 100-108**. Nunca 3 (CrossSave), 200-207 (200/202/203/204 de invitados,
   201/205-207 de memoria), 206.
4. **Durante esa misma llamada y en ese hilo**, el desvío vio: `CreateFile*` de escritura a
   `<caja>\<cuenta>\slotN_new\lobby.save` **y** a `...\slotN_new\save.save`, y el `MoveFileExW slotN_new → slotN`
   correcto posterior a la escritura de `save.save` (la secuencia de la traza #29-#72).
5. Las cabeceras de los dos ficheros pasan `gsv::check_header` (la misma validación que el juego) y su tamaño cuadra.
6. La ranura no está "manchada": si el desvío ve una escritura a la familia `slotN*` de una ranura del anfitrión fuera
   de una llamada del anfitrión (otro usuario, `FlushPendingSaveData` de la salida del usuario 2, hilo desconocido),
   N queda manchada hasta que una llamada del anfitrión reescriba sus dos ficheros (lo que la limpia).

El `FlushPendingSaveData` del anfitrión también abre la ventana (si alguna vez hiciera E/S de una ranura del anfitrión
con los dos ficheros, se publica; hoy no hace E/S [V]). El del usuario 2 nunca publica, solo mancha.

### 5.4 Cómo se publica

En dos tiempos, para no alargar la barrera del 2077 (todos los hilos del servidor parados):

**En el hilo del servidor, al volver del original** (dentro de `save_detour`): copiar `<caja>\<cuenta>\slotN\
{lobby,save}.save` a `...\CD\cdmp_host_commit\<seq>-slotN\` (fuera de `save` y de la caja: los ganchos no la tocan; ~2
MB, pocos ms) y encolar `{seq, N, motivo, hora}`. Nada más.

**En un hilo propio del mod** (cola FIFO; si llegan dos de la misma N, solo la más nueva):

1. Leer los dos ficheros de `cdmp_host_commit`, `check_header`, SHA-256 (BCrypt, como `guest_save`).
2. Si es la **primera publicación de la ejecución**: copia completa de la carpeta real a
   `...\CD\cdmp_host_backups\session-<AAAAMMDD-HHMMSS>\` (`copy_tree` de `save_hooks.cpp`, fechas incluidas); se
   guardan las 10 últimas (constante). Si falla: no se publica nada en esta ejecución.
3. En la carpeta real, con los ganchos **saltados** (ver §5.5, `t_raw`): `save\<cuenta>\slotN_new\` (borrarla si
   existe), escribir los dos ficheros desde memoria con `CreateFileW(CREATE_NEW)` + `WriteFile` + `FlushFileBuffers`,
   releerlos y comparar SHA-256.
4. Si existe `slotN`: `MoveFileExW slotN → slotN_backup` (como el juego). Luego `MoveFileExW slotN_new → slotN`.
5. `MoveFileExW slotN_backup → ...\CD\cdmp_host_backups\<AAAAMMDD-HHMMSS>-<seq>-slotN` (sale de `save`: es la copia
   de la ranura sustituida, mismo volumen). Podar a las últimas `host_save_keep` (20).
6. Borrar `cdmp_host_commit\<seq>-slotN`. Línea `COMMITTED`.

Por qué esta secuencia: todos los estados intermedios en `save\<cuenta>` (`slotN_new`, `slotN_backup`) son los mismos
que deja un guardado nativo interrumpido, y el juego ya los repara al cargar (`0x235A800`) o al arrancar (`vf7`
`0x2358EF0`). Nunca se deja un nombre `*_delete` ni carpetas propias dentro de `save\<cuenta>` (`vf7` crearía una
ranura por cada carpeta que no conoce).

Si falla un paso: deshacer lo posible (si falta `slotN` y existe `slotN_backup`, devolverlo), línea `ERR`, y
**apagar las publicaciones para el resto de la ejecución** (el juego sigue guardando en la caja, como hoy); la línea
periódica lo dice. Lo que quede en `cdmp_host_commit` de una ejecución anterior (juego cerrado con `Stop-Process`
antes de publicar) se mueve al arrancar a `cdmp_host_backups\orphan-<seq>-slotN` (recuperable a mano) y se registra;
no se aplica solo (el mapa de ranuras `S` ya se construyó contra la real antes de que el mod se instale).

Opcional, fase 2: `user_engine_option_save.xml` (opciones del anfitrión, un solo usuario) por el mismo camino cuando
el desvío vea una escritura suya.

### 5.5 Cambios de código (dónde)

| Fichero | Cambio |
|---|---|
| `src/mod/session2/host_save_logic.h` (nuevo, solo cabecera, como `guest_save_logic.h`) | `enum class HostSave { Sandbox, Probe, Real }`, `parse_host_save`, `host_save_name`; `host_slot_allowed(N)` (0-2, 100-108); `parse_slot_path(sandbox_path, sandbox_root, account)` → `{N, Family::Slot|New|Backup|Delete, File::Lobby|Save|Other}`; `SlotWrites` (lo visto en una llamada) y `ready_to_publish(SlotWrites, N)`; la máscara de ranuras manchadas; nombres de copias y plan de poda; el plan de pasos de §5.4 (para probarlo sin ficheros) |
| `tests/optd_host_save_tests.cpp` (nuevo) + `CMakeLists.txt` (`cdmp_tests_optd_host_save`, junto a `cdmp_tests_optd_guest_save`, línea ~427) | lo anterior: rutas de la traza (minúsculas, `\\?\`), lista blanca, secuencias completas e incompletas, manchas, poda |
| `src/mod/session2/save_hooks.{h,cpp}` | (1) `thread_local bool t_raw` + RAII `RawScope`: con él puesto, **cada** detour llama al original con los argumentos tal cual (antes de `redirecting()`/`refuse_*`); lo usa solo el hilo de publicación. (2) Registro por hilo: `save_hooks_record_begin()` / `save_hooks_record_end(SlotWrites&)`; en `redirected()` (línea 364), tras un original correcto de escritura con ruta en la caja, anotar en el registro activo (o, sin registro, manchar la ranura del anfitrión si `parse_slot_path` dice que lo es). Sin reservas de memoria en el camino caliente (`maybe_save` ya filtra). (3) `save_hooks_account_dir()` y `save_hooks_save_dir()` para el publicador. (4) En `save_hooks_redirect_describe()`: `host slot writes outside a host save N (slots …)` |
| `tests/optd_hook_redirect_tests.cpp` | ampliar: con `t_raw` una escritura bajo la `save` falsa llega a la real; el registro ve la secuencia de la traza para `slot0` y no la ve para una escritura fuera de registro (mancha) |
| `src/mod/session2/host_save.{h,cpp}` (nuevo) | `host_save_install(HostSave, keep…)` (comprueba requisitos, limpia `cdmp_host_commit` huérfanos, arranca el hilo), `host_save_after_host_call(slot, SlotWrites, code, caller)` (instantánea + cola, en el hilo del servidor), el hilo de publicación (§5.4), `host_save_describe()` para la línea periódica |
| `src/mod/session2/guest_save_host.cpp`, `save_detour` (línea 870) | si `owner == Owner::Host` y `d.action == Pass` y `host_save != Sandbox`: `save_hooks_record_begin()` antes de `g_h_save.unsafe_call` (línea 909) y, a la vuelta, `record_end` + `host_save_after_host_call(d.slot, writes, code, caller)`. Igual en `flush_detour` (línea 938) para el anfitrión; para el usuario 2/sin identificar, registro solo para manchar |
| `src/mod/session2/session2.cpp`, `install()` (bloque de `guest_own_save`, líneas 922-941) | tras `guest_save_install`: si `host_save != sandbox`, exigir `save_hooks_redirecting()` y `guest_save_mode() == gsv::Mode::On` (no `probe` para `real`: en `probe` los guardados del usuario 2 pasan a ranuras del anfitrión en la caja); si no, `Session2 host save: host_save=real needs ...; it stays sandbox` |
| `src/mod/config.{h,cpp}`, `session2.h` (`Options`), `mod.cpp` (línea 877 la línea `Config:`, línea 1112 `session2_opt`) | `s2_host_save`, `s2_host_save_keep`; mostrarlos en la línea de configuración |
| `docs/research/ini/` (kit) y `p8-guardado-desviado.md` §5 | `host_save=probe` en los ini de anfitrión del siguiente kit; un ini "jugar" con `real`; actualizar "Límites conocidos" |

### 5.6 Líneas de log

- Instalación: `Session2 host save: host_save=real | the host's own saves (slots 0-2, 100-108; owner verified; both
  files written in its call) are published to '<save>' after each save; backups in '<...\cdmp_host_backups>' (keep
  20 slots, 10 sessions); user 2 never (slot 204 stays in the sandbox)`.
- Cada guardado del anfitrión: `Session2 host save: slotN from autosave (exe+0xCBD86D3) queued #seq (lobby a B, save b
  B)`; en `probe`: `... WOULD publish slotN ...`.
- Publicación: `Session2 host save: COMMITTED #seq slotN to '<real>' (lobby a B sha256 …, save b B sha256 …; previous
  slotN kept as '<backup>'; session backup '<...>')`.
- Rechazos: `NOT published slotN: <motivo>` (manchada, falta un fichero, cabecera, resultado ≠ 0, ranura no
  permitida).
- Periódica: `host save real: committed N (last slotN hh:mm:ss), skipped M, tainted {…}, failed F`.

---

## 6. Verificación en un PC (`cdmp_peer`)

Base: la prueba L2+L3 de `noche/progreso-invitado.md` ("Prueba en UN PC"): el anfitrión juega, el usuario 2 es
`cdmp_peer` en replay con una partida de otra cuenta (copia de trabajo de la plantilla de Nexus en
`guest_own_save_debug_dir`, o la subida del peer de la fase B).

**Antes (juego cerrado):**
1. Copia de `%LOCALAPPDATA%\Pearl Abyss\CD\save` en `backups\` (CLAUDE.md).
2. Inventario de la carpeta real:
   `Get-ChildItem -Recurse "$env:LOCALAPPDATA\Pearl Abyss\CD\save" -File | ForEach-Object { [pscustomobject]@{ Path=$_.FullName; Len=$_.Length; Write=$_.LastWriteTime; Sha=(Get-FileHash $_.FullName).Hash } }`
3. Cabecera de cada `.save` (los primeros 0x80 B son legibles): magic `SAVE`, versión, `+0x12` tamaño en claro, `+0x16`
   tamaño de datos, nonce `+0x1A`. Anotar la del `save.save` del anfitrión y la de la partida del invitado.
4. Tres marcas que distingan al anfitrión de la partida del invitado, vistas en el juego: nivel/región/tiempo de juego
   en el menú "Continuar", dinero y arma equipada, una misión activa del diario.

**Sesión 1, `host_save=probe`** (M1, sin riesgo): entrar, unir el peer, que el anfitrión guarde (2910 desde ESC, ranura
100-108; o esperar un autoguardado, unos 15 min), despedir al peer (su 2910 final a la 204).
- Esperado: `WOULD publish slotN` para cada guardado del anfitrión con los dos ficheros vistos; **ninguno** para la
  204; `tainted {}` vacío; la carpeta real **idéntica** al inventario de antes.

**Sesión 2, `host_save=real`**, lo mismo.
- En el log: `COMMITTED` por cada guardado del anfitrión; ningún `ERR`, ningún `NOT published` salvo los esperados.
- Carpeta real después: solo cambian las ranuras publicadas (`slotN` nueva o con SHA nuevo, igual al SHA del log y al
  de la caja en ese momento); `slot108` y lo no publicado, idénticos; **no hay `slot204`** ni `*_new`, `*_backup`,
  `*_delete`; `cdmp_host_backups\session-…\` es igual al inventario de antes; `cdmp_host_backups\…-slotN` es la ranura
  sustituida.
- Separación por bytes: los SHA de la ranura publicada ≠ los de `slot204` de la caja y los de
  `%LOCALAPPDATA%\cdmp\guest_saves\…\gen<n>`; la cabecera del `save.save` publicado tiene un `+0x12` del orden del
  del anfitrión, no del invitado (comprobación débil: tamaños parecidos).
- El invitado sigue igual: `EXPORTED gen<n>` y `slot204` solo en la caja.

**Después: carga en un jugador** (mod con `create_session=0`, o sin mod; con la copia de `backups\` hecha):
"Continuar" ofrece la ranura publicada más reciente; el resumen del menú, el dinero, el equipo, el inventario y el
diario son los del anfitrión con lo hecho en la sesión 2. Nada de la partida del invitado (su nivel, sus objetos, su
posición). Los cambios del mundo hechos por el invitado (enemigos muertos, cofres, pasos de misión con
`quest_policy=1`) **sí** deben estar: es lo acordado.

**Negativos (sesión 3, opcional):** `guest_own_save=probe` con `host_save=real` → arranca en `sandbox` con su línea;
borrar a mano un fichero de `cdmp_host_commit` en medio (o simular fallo de escritura) → `ERR`, publicaciones
apagadas, la real sin `*_new` sueltos.

---

## 7. Riesgos

| Riesgo | Grav. | Mitigación |
|---|---|---|
| Un fallo del publicador estropea la partida real | alta | copia de sesión antes de la primera escritura, copia de cada ranura sustituida, secuencia idéntica a la del juego (recuperable por el propio juego), verificación por SHA tras escribir, apagado a la primera anomalía, `sandbox` por defecto |
| Globales pisados por la carga de la partida del invitado (precios, eventos, `restoreCodeVersion`) persistidos en la partida del anfitrión | media, [S] | medirlo (M2) antes de usar `real` de forma habitual |
| Estados raros del mundo dejados por contenciones del mod (NPC del usuario 2 devueltos, S33, monturas) persistidos | media, [H] | copias; jugar en `real` solo con las etapas ya estables; revisar tras la primera sesión larga |
| Steam Auto-Cloud sube al cerrar lo publicado (también una partida mala) | media | las copias están en `cdmp_host_backups` (local, fuera de la nube); restaurar con el juego cerrado |
| Pérdida del último guardado si se cierra el juego antes de publicar | baja | la publicación tarda < 1 s; los huérfanos se conservan en `cdmp_host_backups\orphan-…` |
| Parón extra dentro de la barrera del 2077 | baja | en el hilo del servidor solo la copia a `cdmp_host_commit` (~2 MB); el resto en el hilo propio |
| Borrar partidas desde el menú del anfitrión no llega a la real | baja | documentado; solo se publican escrituras de guardado |
| Opciones del anfitrión (`user_engine_option_save.xml`) siguen perdiéndose | baja | fase 2 |
| Ranura que el gestor no conocía al arrancar (p. ej. `slot0` nueva) | baja | se ve en el siguiente arranque (`vf7` lista la real antes de que el mod se instale) |

---

## 8. Qué medir antes

- **M1 (necesario, barato):** una sesión con `host_save=probe` (§6): que el registro por llamada vea la secuencia de los
  dos ficheros tanto en el autoguardado (`options 0x4`) como en el 2910 del anfitrión, y que con el usuario 2 dentro y
  en su salida no aparezcan escrituras de ranuras del anfitrión fuera de una llamada suya. La traza de las 18:21
  (#31-#46) ya lo apunta para el autoguardado [V].
- **M2 (recomendado antes de jugar en `real` a diario):** si la carga de la partida del invitado cambia globales del
  mundo del anfitrión. Comparar en el registro de paquetes del cliente del anfitrión (sniffer) los precios y eventos
  (`StoreDataListAck` 2146, `GlobalGameEventVaryTradeItemPriceAck` 3075 y los Ack de eventos) y la hora, antes y después
  del 3093 del usuario 2 con partida propia; y en el juego, un precio de tienda y la hora vistos por el anfitrión. Si
  cambian, el siguiente guardado del anfitrión los persistiría y hay que contenerlo antes (esto ocurre también hoy
  dentro de la sesión, solo que no se guarda).
- No hace falta descifrar partidas: la procedencia (§5.3) y la carga en un jugador (§6) bastan para lo de los ficheros.

---

## 9. Lo perdido hasta hoy

En las tres carpetas no hay ninguna ranura del anfitrión más nueva que `slot1` (3-10). Los autoguardados del 14-10 a
las 18:21 (`slot0`) y 18:36 (`slot2`) estaban en la caja de la sesión de las 18:03, que se borró al arrancar a las
19:02. Las sesiones anteriores con `redirect` corrieron la misma suerte. Solo podría quedar algo en copias manuales
(`C:\Emulador CD\backups\`, no revisado aquí). Mientras no exista `host_save=real`, para quedarse una sesión hay que
copiar a mano la ranura del anfitrión de `cdmp_save_sandbox\<cuenta>\` a `save\<cuenta>\` **con el juego cerrado y
antes del siguiente arranque**, sin copiar `slot204` (`p8-guardado-desviado.md` §5).
