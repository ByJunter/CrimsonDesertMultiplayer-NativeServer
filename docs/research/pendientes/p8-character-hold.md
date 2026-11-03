# P8: retención del personaje sin acción (`character_hold=1`) y regla de hilos de `NotifyAppear` (2026-10-10)

Implementa la opción B de [p8-crash-esc-1831598.md](p8-crash-esc-1831598.md) §5.2 y cierra el caso 3 de
[p8-auditoria-hilos.md](p8-auditoria-hilos.md) §4. Sin lanzar el juego, sin tocar `bin64` y sin commit.

Marcas: **[V]** verificado (código, binario estático, prueba unitaria o log citado); **[E]** deducción.

## 0. Resumen

- **Clave nueva** `[session2] character_hold`, **0 por defecto**. Con 0 no cambia nada de la retención de
  personajes: un `0xA…` ni se mira (`HoldWhy::NotChildKey`, como antes) [V prueba `test_off_unchanged`].
- **Con 1:** el personaje **principal** de un usuario (clave `0xA…`, dueño `[actor+0xA0]` = usuario validado
  y `[[actor+0xA0]+0xD0] == actor`) que `NotifyAppear` anuncia con **byte de restauración 0** a una sesión que
  **no es la de su dueño** queda retenido para esa sesión. Su dueño lo recibe en el acto, en el mismo
  `NotifyAppear`: **nunca se retiene para el dueño ni se retrasa su propio login** [V pruebas].
- **Liberación:** el mismo re-anuncio nativo de D-53/`observer_hold` (`NotifyAppear([observador],[clave])`,
  un 2005 nuevo) en cuanto el servidor acepta su primera acción (byte 1).
- **Tope:** si sigue a 0 tras **15 s**, se anuncia **con byte 0** (lo que haría el motor sin la clave) y sale
  una línea `WRN`. Nunca se suelta en silencio mientras el observador lo ve.
- **Limpieza:** igual que D-53/`observer_hold`: época, salida o marcha del usuario 2, despawn, observador sin
  usuario, observador fuera de alcance (cambio de campo), tabla acotada (16) con hueco reservado en el reparto.
- **`observer_hold_acks`:** si el retenido es el personaje del **anfitrión** y el observador es el usuario 2, sus
  paquetes por actor (2697, 2991, 2723, 2404…) esperan a su 2005, como los de un NPC.
- **Regla de hilos (las tres retenciones):** el código solo llama a `NotifyAppear` en un hilo que **el propio
  juego** ya usó para llamarla. Ese hilo se reconoce en el desvío por la dirección de retorno de una de sus
  dos llamadas nativas. Tras una `StartActionReq` en otro hilo, la liberación se aplaza a la cola del siguiente
  `NotifyAppear` nativo.
- **Pruebas:** nuevo `optd_character_hold`, con 103 comprobaciones. Lo detecta una mutación de la regla de
  hilos: 6 fallos. `optd_child_hold`, `optd_s3` y `optd_scene_acks` siguen en verde.
  - `.\scripts\build.ps1` completo: `Build OK` y `100% tests passed, 0 tests failed out of 24`.

## 1. El predicado [V código / E offsets]

`child_hold.cpp`, `character_verdict_with` (lo llama `verdict_with` solo con `characters=true`):

| Paso | Lectura | Si falla |
|---|---|---|
| clase | `key >> 28 == 0xA` | `NotChildKey` (no se mira) |
| actor | SAM por clave (dos intentos, sin candado, como D-53) | `Unreadable` / `NotFound` |
| dueño | `[actor+0xA0]` ≠ 0 | `NoOwner` |
| usuario | `owner_who_with` de D-53: el puntero exacto del anfitrión, el del usuario 2, o cualquier `ServerUserActor` (vtable) que no lleve la clave del anfitrión | `NotUser` (nativo) |
| principal | `[[owner]+0xD0] == actor` | `NotMain` (nativo, contado) |
| CC y byte | `[[[actor+0x68]+0x40]+0x40]+0x30`, con `[CC+8] == actor` | `NoCc` / `Restored` |

- **`[pj+0xA0]` = el `ServerUserActor` del personaje** [V doc]: `opcion-d-campo-compartido.md` («el
  `ServerUserActor` que viaja (`[pj+0xA0]`)»), `opcion-d-p9.md` (`[r9+0xA0] == usuario2`, personaje del
  usuario 2) y `orcax-equiphide.md` §3.6.
- **`[usuario+0xD0]` = su personaje controlado** [E, dos fuentes]:
  - `0x10859ED0` (autoguardado, `.xtls`, `opcion-d-e3-riesgos.md` §3.2) usa `[[pj+0xA0]+0xD0] == pj` como
    «personaje principal de su usuario» [E-x];
  - la cadena de LiveTransmog: `NwSession+0xA0 → ServerUserActor +0xD0 → ServerChildOnlyInGameActor`.
  - Si el offset fuera otro, no se retiene nada (`NotMain`, contado en `describe`: «0xA keys not a user's main
    character N»). Es un fallo seguro.
- **Las piezas adjuntas sin CC** (equipo, como `0xB01002DA`) **no se retienen**: `NoCc`, y pasan en el mismo
  2005 [V prueba `test_pieces_and_children`].
  - Con el padre ausente, `0x15ECA10` pasa `rdx = 0` y `0x439AB0` salta a `0x439BB8`, que no lee la acción
    (crash §5.2 B) [V código del juego, E conducta].
- **Los hijos (D-53) no cambian.** Con `child_hold=0` y `character_hold=1`, los hijos pasan (`ChildrenOff`)
  y solo se retiene el personaje [V prueba].

## 2. Reparto, tabla, liberación y limpieza [V código + pruebas]

- **Reparto** (`plan_split`, el de D-53):
  1. el resto de las claves, a todos y en su orden;
  2. el dueño recibe su personaje en un 2005 propio;
  3. cada otra sesión receptora queda anotada como `(personaje, observador)`.
- **Tabla propia** `CharHoldTable`, de 16 entradas. Con dos usuarios hay como mucho dos: el personaje de cada
  uno retenido para el otro.
  - El hueco se **reserva bajo el candado en el propio reparto**, como el de los NPC.
  - Si no cabe, el personaje se anuncia de forma nativa (con byte 0, `WRN … table full`). **Nunca se retiene
    sin entrada** [V prueba `test_room_reserved`].
  - Un duplicado conserva el primer instante, para que la espera total quede acotada. Si detrás de la clave
    hay ya otro actor, el instante se reinicia.
- **Pasada** (`char_pass`): copia la tabla bajo el candado, decide fuera con lecturas SEH por clave
  (`read_char_release`) y aplica bajo el candado solo a las entradas que siguen iguales (`stamp`). Corre:
  - tras cada `StartActionReq` (`0x296CAA0`, vf2);
  - en la cola de cada `NotifyAppear` nativo, como mucho cada 50 ms. Así avanza sin ninguna Req; el tope
    vence aquí;
  - en el respaldo del hilo receptor, cada 250 ms: **solo suelta y anota**; una liberación debida queda
    aplazada.

`char_release_step`:

| Lectura | Decisión |
|---|---|
| otra época | soltar |
| actor ilegible | esperar; a los 45 s, soltar |
| actor 0 (despawn o logout) | soltar |
| observador sin usuario válido | soltar |
| observador fuera de alcance (cambio de campo o de sectores) | soltar; el motor lo vuelve a anunciar al volver y se retiene otra vez si sigue a 0 |
| otro actor tras la clave, u otro dueño, **con el observador en alcance** | **anuncio nativo** (`WRN`) |
| ilegible 45 s con el observador en alcance | **anuncio nativo** (`WRN`) |
| byte 1 y en alcance | **liberar** (`restore 1`) |
| byte 0 y edad ≥ 15 s, en alcance | **anuncio con byte 0** (`WRN … TIMEOUT, announced with restore 0`) |
| byte 0 antes del tope | esperar; a los 3 s, una línea `CharHold wait` |

- **Salida del usuario 2** (`2001` entregado o usuario ausente): el tick de red del mod vacía todas las
  entradas de personaje. Con dos usuarios, todas son suyas: o su personaje, o el del anfitrión retenido
  para él [V prueba].
- **El motor lo anuncia por sí mismo** (`NotifyAppear` sin repartir a esa sesión, ya con byte 1): la entrada
  se olvida (`forget_announced`) [V prueba].

## 3. `observer_hold_acks` con un personaje [V código + prueba]

- Al registrar una entrada de personaje cuyo observador es la sesión 2 viva, se llama a
  `observer_hold_acks_begin(clave)`.
  - Al liberarla: `observer_hold_acks_released`.
  - Al soltarla: `observer_hold_acks_drop`.
  - Es exactamente lo que ya se hace con los NPC.
- La ruta (`observer_hold_acks_route`, en `take_frame`) toma ahora la clave con `oha::held_key_of`, que acepta
  `0xB…` y `0xA…`. `npc_key_of` no cambia, y su prueba «a character key: not held» sigue valiendo.
  - Una clave `0xA…` que nadie ha empezado a retener pasa como antes (`offer` → `Pass`).
- **Solo cubre al personaje del anfitrión retenido para el usuario 2.** Lo que va al cliente del anfitrión no
  pasa por `take_frame`, así que los 2723/2991 del personaje del usuario 2 que el relevo (P5) lleve al
  anfitrión antes de su 2005 los descarta su cliente por clave desconocida. Es lo mismo que ya pasa con los
  NPC [E].
- **Necesita** `observer_hold_acks=1`. `session2.cpp` lo admite ahora con `observer_hold=1` **o**
  `character_hold=1`.
- **Riesgo [E]:** los `MoveActorAck` 2697 retenidos se reenvían tras un 2005 que ya trae la posición actual.
  Un personaje que se mueve durante la espera repetiría movimientos viejos. A partir de 32 paquetes o 32 KB
  la clave se «derrama»: salen todos de golpe y la retención pasa a ser nativa. En la práctica solo se
  retiene al anfitrión si su byte vuelve a 0 (muerte o recarga, riesgo R3), y su cliente real actúa en
  milisegundos.

## 4. La regla de hilos (caso 3 de `p8-auditoria-hilos.md`)

**Antes:**
- `sweep_children(…, on_handler_thread=true)` llamaba a `NotifyAppear` en **cualquier** hilo del reino
  servidor, tras `StartActionReq`.
- `npc_pass`/`npc_periodic` también liberaban ahí.
- El respaldo exigía `GetCurrentThreadId() == g_appear_tid`, el último que llamó.

**Ahora:**
- `g_native_tids`: hasta 8 hilos. Se llena en `notify_detour` **solo** si `_ReturnAddress()` es
  `exe+0x2AF90F0` o `exe+0x2AF92D6`, es decir, el retorno de las dos llamadas nativas.
- `native_appear_thread()` exige estar en esa lista, además del reino servidor (`TLS+0x1EC`). Lo cumplen
  `sweep_children`, `char_pass` y las pasadas de NPC tras `StartActionReq`.
- Si no se cumple, la liberación espera a la **cola del siguiente `NotifyAppear` nativo**:
  - los hijos, como mucho cada 50 ms (nuevo);
  - los personajes, como mucho cada 50 ms;
  - los NPC, la pasada presupuestada de siempre cada 100 ms.

**Evidencia:**
- **[V estático]** `NotifyAppear 0x2B000C0` solo se llama desde `0x2AF90EB` y `0x2AF92D1`.
  - Búsqueda de `E8`/`E9` rel32 y de punteros absolutos en todas las secciones, más la de desplazamientos
    rel32 dirigidos a `0x2B000C0` en las secciones ejecutables (`xref.py`, `riprel.py`).
  - El único acierto más, `0x2AEA938`, cae en mitad de una instrucción.
  - El mod solo la llama por el trampolín, que el desvío no ve.
  - La instalación ya comprueba los bytes de las dos llamadas (`E8 D0 6F 00 00`, `E8 EA 6D 00 00`).
- **[V log]** El hilo receptor (`OnRecvPacket`) **no** es el de la lógica (T29232; diario C2, D-53r).
- **[E]** No se sabe en estático en qué hilo corre el manejador de `StartActionReq`. D-53r vio «sweeps
  refused 4» con la regla del último llamador, así que hay más de un hilo de servidor.
- **En vivo** lo dirá:
  - la línea `Session2 child hold: thread N calls NotifyAppear natively (from exe+0x2AF90EB|0x2AF92D1)`;
  - en `describe`: `NotifyAppear threads N (other callers 0, slots full 0); releases deferred to one:
    children A, NPC passes B`, y en el bloque de personajes `deferred to a NotifyAppear thread C`.
  - `other callers` debe ser 0.
- **Coste [E]:** una liberación tras una `StartActionReq` en un hilo no nativo se retrasa hasta el siguiente
  `NotifyAppear` del juego. En la sesión D-53 hubo 912 llamadas, del orden de decenas por segundo con
  movimiento.

## 5. Los dos sentidos [V pruebas, E en vivo]

| Caso | Quién recibe al instante | Retenido para | Liberación |
|---|---|---|---|
| Personaje del usuario 2 con byte 0 entra en el campo del anfitrión (E1.T2, una llamada a ambas sesiones) | sesión 2 (dueño) | sesión del anfitrión | su primera `StartActionReq` aceptada → 2005 nativo a la sesión del anfitrión; o 15 s → byte 0 (`WRN`) |
| Personaje del anfitrión con byte 0 (tras recarga o muerte) visto por el usuario 2, también en la llamada del usuario que se mueve (solo sesión 2) | sesión del anfitrión | sesión 2 | igual; y, con `observer_hold_acks=1`, sus paquetes van tras su 2005 |
| Login propio del usuario 2 (su campo, solo su sesión) | sesión 2 | nadie | — |
| Byte 1 (D-48: paso 4b o cliente real) | todos | nadie | — |

## 6. Configuración y archivos tocados

- `src/mod/config.h`:
  - línea 203: comentario de `s2_observer_hold_acks` («Needs observer_hold=1 or character_hold=1»);
  - **líneas 205–208**: `bool s2_character_hold = false;` con su comentario.
- `src/mod/config.cpp` **línea 192**:
  `c.s2_character_hold = read_uint(ini_path, L"session2", L"character_hold", 0) != 0;`
- `src/mod/session2/session2.cpp`:
  - 617–627: lectura de la clave, aviso sin `create_session` y `child_hold_install(child, observer,
    character)`, con su error;
  - 655–657: `observer_hold_acks` con `observer_hold` **o** `character_hold`.
- `src/mod/session2/child_hold.{h,cpp}`: el predicado, la tabla, la pasada, las líneas, `describe`, la regla de
  hilos y las costuras de prueba (`Seams::thread_id`, `reset(…, characters)`, `char_*`, `fill_char`).
- `src/mod/session2/observer_hold_acks_logic.h` (`held_key_of`), `observer_hold_acks.cpp` (una línea) y
  `observer_hold_acks.h` (comentario).
- `tests/optd_character_hold_tests.cpp` (nuevo) y `CMakeLists.txt` (destino `optd_character_hold`).

## 7. Pruebas (resultados reales, 2026-10-10)

```
cdmp_tests_optd_character_hold.exe  -> optd_character_hold: all tests passed
cdmp_tests_optd_child_hold.exe      -> optd_child_hold: all tests passed
cdmp_tests_optd_s3.exe              -> optd_s3 tests passed
cdmp_tests_optd_scene_acks.exe      -> optd_scene_acks: all checks passed
cmake --build … --target cdmp_mod   -> cdmp.asi, sin avisos
.\scripts\build.ps1                 -> Build OK; 100% tests passed, 0 tests failed out of 24
```

Un primer `build.ps1` falló por causas ajenas a este cambio:
- `C1041` (PDB ocupado por otra compilación simultánea);
- `replay.cpp`: `now_us` ambiguo, del trabajo en paralelo sobre `troc_*`.

El segundo pasó entero.

`optd_character_hold` cubre:
- el predicado: host, usuario 2, apagado, `NotMain`, `NoOwner`, `NotUser`, `NoCc`, `NotFound`, ilegible,
  anfitrión no leído, `Restored`, hijos;
- las reglas puras de liberación, una por fila de §2;
- de punta a punta con las costuras:
  - el personaje del usuario 2 retenido para el anfitrión: línea `held`, `CharHold wait` una sola vez a los
    3 s, liberación a la sesión del anfitrión tras su Req a los 5,1 s;
  - el del anfitrión retenido para el usuario 2, también en la llamada «solo sesión 2»;
  - nunca para el dueño;
  - el tope: anuncio con byte 0 en la cola de `NotifyAppear` y línea `WRN`;
  - las salidas: fuera de alcance, despawn, época, marcha del usuario 2 en los dos sentidos, y el respaldo
    que aplaza;
  - el anuncio nativo cuando hay otro actor tras la clave;
  - el olvido cuando el motor lo anuncia él mismo;
  - piezas sin CC y la mascota;
  - la reserva con la tabla llena;
- la regla de hilos para personajes, hijos (D-53) y NPC (`observer_hold`);
- `observer_hold_acks`: 2697 y 2991 del personaje del anfitrión, tras su 2005 y en orden; el soltado
  descarta; nada para el anfitrión como observador; `held_key_of`.

**Mutación:** `native_appear_thread()` siempre verdadero → 6 fallos, los de la regla de hilos (luego
revertida).

## 8. Verificación en un PC (anfitrión + `cdmp_peer`)

Despliegue y juego, cuando se pueda (no se hizo aquí). Ini de anfitrión de opción D (`create_session=1`,
`accept_tunnel=1`, `share_field=1`, `child_hold=1`), más:

```ini
[session2]
character_hold=1
mirror_host=0   ; el espejo inyecta Req del anfitrión con la clave del usuario 2: podría dar el byte 1 antes y confundir
```

### 8.1 `login_e3lc00act_112627.troc`, el guion OBSOLETO, sin 4b

`cdmp_peer --mode replay --script docs\research\data\login_e3lc00act_112627.troc --linger 900000`

Es exactamente el caso que tiró al anfitrión en `0x1831598`: el personaje del usuario 2 entra en tu campo
con byte 0, y su única `StartActionReq` es el paso 14, unos 5,1 s después (p8-crash-esc-1831598.md §2.1).

**Qué debe salir en el log, en orden:**
1. Al instalar:
   - `Session2 character hold ON: a user's main character …`;
   - `Session2 child hold: the holds call NotifyAppear only on a thread the game itself used …`;
   - y, en cuanto el juego lo llame, `… thread N calls NotifyAppear natively (from exe+0x2AF90EB)`.
2. En E1.T2 (`users 2 user actors 3`):
   - `Session2 character hold: held character 0xA01002xx (type 1, owner user 2 0x901002xx, restore 0) from
     session 0x… (host 0x90100000); its owner got it at once …`;
   - y, de D-53, `child hold: held 0xB01002xx` (su mascota).
   - **No debe salir** `HorseDiag RESTORE … USER 2 … restoreByte=0` en ese momento: el cliente del anfitrión
     no crea al personaje.
3. A los 3 s: `Session2 character hold: CharHold wait 0xA01002xx (owner user 2) still held from session 0x…
   (host) after 3xxx ms (restore 0)`.
4. Tras el paso 14, si el servidor acepta la Req:
   - `Session2 character hold: released character 0xA01002xx (type 1, owner user 2) to session 0x… (host
     0x90100000) after ~5000 ms, restore 1 (restore 1; after StartActionReq)`. Si el manejador no corre en
     un hilo nativo de `NotifyAppear`, termina en `…; after NotifyAppear)`;
   - y después `HorseDiag RESTORE … USER 2 … restoreByte=1`, con capas con acción.
5. **Criterio:**
   - ningún cierre del anfitrión;
   - ninguna línea `WRN … TIMEOUT`;
   - en `describe`: `character hold: held 1 … released 1 (restore 1 1, TIMEOUT with restore 0 0 …)`;
   - `other callers 0`.
6. Su mascota (D-53) sigue retenida, porque el guion `act` no le da acción, hasta que el usuario 2 se va:
   `user 2 is gone`.

**Si sale `WRN Session2 character hold: released character … TIMEOUT, announced with restore 0`** a los
15 s:
- el servidor **no aceptó** el paso 14 en el campo compartido. Es el candidato #9
  (`eErrEnterToStageNotCompleted`) de `opcion-d-accion-usuario2.md` §2.4;
- el anfitrión recibe entonces al personaje con byte 0, el estado de antes, y **puede volver a caer** en
  `0x1831598`. La línea `WRN` justo antes lo identifica [E].
- Ese resultado no es un fallo de la retención (el requisito es «nunca soltarlo en silencio»). Es una medida:
  - pide la red C de §5.2 (`0x8646D0`) para los personajes que nunca reciben acción;
  - o un tope mayor, cambiando `hold::kCharTimeoutMs`.

### 8.2 `login_e3lc00soak_child_112627.troc`, con 4b

**Ojo:** con este guion el personaje **no se retiene**.
- Su paso 4b le da la primera acción en el aparcamiento.
- El byte persiste entre campos [V: C1g `restoreByte=1`; `opcion-d-accion-usuario2.md` §1.1], así que al
  entrar en tu campo el veredicto es `Restored`.

**Qué debe salir:**
- **ninguna** línea `held character`;
- `HorseDiag RESTORE … USER 2 … restoreByte=1` al entrar;
- de D-53: `child hold: held 0xB01…` y, tras el paso 9b, `child hold: released 0xB01… after … ms, restore 1
  (after StartActionReq)`, o `(after NotifyAppear)` si se aplazó;
- 5 minutos después, la despedida: `user 2 is leaving …`, y ninguna entrada que quede.

**Qué comprueba:**
- que `character_hold=1` no estorba al camino D-48;
- la regla de hilos con D-53. Si el manejador de `StartActionReq` no es un hilo nativo, el hijo sale «after
  NotifyAppear» y `describe` cuenta `releases deferred to one: children 1`.

**La liberación del personaje tras su primera acción** la comprueba el §8.1: allí el paso 14 **es** su
primera acción.

### 8.3 Lo que no se puede con un PC

- **El personaje del anfitrión con byte 0 retenido para el usuario 2.** El anfitrión real siempre tiene byte
  1 y su cliente actúa al instante. Queda cubierto por las pruebas y, en vivo, por P8 con Shadow si aparece
  un caso R3 (muerte o recarga del anfitrión con el invitado dentro).
- **`observer_hold_acks` con personaje**, por la misma razón.

## 9. Límites y riesgos

- **Otros `0xA…` de un usuario** que no sean su principal (personajes intercambiables) no se retienen
  (`NotMain`).
- **Con el tope cumplido**, el personaje llega con byte 0: el estado de antes, contenido solo por D-52/D-44/D-46
  en el cliente del anfitrión, y no por los 10 llamadores de `0x1831560`.
- **Mientras dura la retención**, el usuario 2 está en el campo del anfitrión sin que el cliente del anfitrión
  lo vea.
  - Sus acciones y movimientos que el relevo lleve al anfitrión se descartan por clave desconocida [E].
  - Con un cliente real (P8) esto no ocurre: llega con byte 1.
- **El registro de usuario** (`0x9…`) y las piezas sin CC sí llegan antes que el personaje [V prueba piezas].
- **`D-53` cambia un poco**, por la regla de hilos: una liberación tras `StartActionReq` en un hilo que no sea
  nativo espera a la cola del siguiente `NotifyAppear`. Antes salía en ese mismo hilo, sin pruebas de que el
  juego llamase allí a `NotifyAppear`.

## Correcciones tras la revisión de la noche (2026-10-10)

Detalle completo en `p8-revision-noche.md` §6. Cambian §9 («D-53 cambia un poco») y la regla de hilos:

- La regla de hilos va tras **`[session2] notify_thread_rule`, 0 por defecto**. Con 0, D-53, `observer_hold` y
  `character_hold` liberan en el hilo del manejador de `StartActionReq`, como antes de `737a0d7`. El respaldo del
  hilo receptor solo libera hijos en el último hilo que llamó a `NotifyAppear`, y no hay barrido de hijos en la
  cola de `NotifyAppear`. Con 1, la regla de la noche.
- Con las dos opciones: `WRN Session2 child hold: a due release has waited N ms for a thread that may call
  NotifyAppear …` cuando una liberación debida espera más de 2 s, y una línea cuando por fin corre. `describe`
  añade la espera más larga. Así, el «nunca soltar en silencio» de §8.1 vuelve a tener un tope visible.
- **[V]** El llamador `0x2AF8AF0` (ni sus llamadores `0x2AF8090`/`0x2AF86E0`) no toca el contexto de envío ni las
  funciones de agrupación: no abre un 2042 alrededor del `NotifyAppear` nativo.
- Los inis de la sesión 7 (`cdmp.p8-s7-host.ini`, `cdmp.p8-noche-local.ini`) llevan `notify_thread_rule=1`.
