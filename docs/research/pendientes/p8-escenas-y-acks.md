# P8: las escenas propias del usuario 2 (ESC «ver objeto») y los paquetes por NPC de `observer_hold`

10 de octubre de 2026, rama `opcion-d` (sobre `42d42b7`, sin commit). Build del juego 1.0.0.2976; direcciones en
**RVA** (base `0x140000000`). Continúa `p8-sesion6.md` §3.4 (efecto secundario de `observer_hold`), §4 (ESC del
invitado) y §7.2 filas 5 y 6.

**[V]** = comprobado en capturas, logs, instrucciones (capstone 5.0.7 sobre el PE de solo lectura
`D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, Python `-I`), código, compilación o pruebas. **[E]** = inferencia o
validación pendiente en el juego.

No se lanzó el juego, no se tocó `bin64` (sus capturas solo se leyeron), no se hizo commit y no se tocó `guest.cpp`.
No se engancha ni se parchea `.xtls` ni sus thunks: **no hay ningún gancho nuevo**. La copia del 2810 usa el gancho
que `observer_stage` ya tenía en `0x2A4E4C0`, y la retención usa `take_frame` y los puntos de `child_hold`. Lo que se
llama del juego se llama en el hilo lógico del servidor (`T4572` en la sesión 6B), como en `observer_milestone`.
Ningún hilo del mod asigna ni libera en el heap del juego (`p8-heap-inyeccion.md`).

Dos claves nuevas en `[session2]`, **a 0 por defecto**:

| Clave | Lado | Qué hace |
|---|---|---|
| `observer_scene_copy=1` | anfitrión | §2: cuando el servidor cierra una escena **propia del usuario 2** con un 2810 dirigido a otro personaje, el emisor nativo se llama una vez más para el personaje del usuario 2 |
| `observer_hold_acks=1` | anfitrión | §3: los paquetes por NPC (2723, 2404…) de un NPC que `observer_hold` retiene para el usuario 2 esperan a su 2005 y salen justo detrás, en orden. Necesita `observer_hold=1` |

## 0. Evidencia

| Ref. | Archivo |
|---|---|
| GB | `C:\Emulador CD\backups\p8-sesion6B-shadow\cdmp.log` (invitado, tramo B) |
| GBC | `…\p8-sesion6B-shadow\cdmp_packets_20261009-224848.bin/.log` (cliente del invitado) |
| HB | `C:\Emulador CD\backups\logs\cdmp_20261009-2345_p8-6B-host.log` |
| HBC | `D:\SteamLibrary\…\bin64\cdmp_packets_20261009-234606.bin/.log` (cliente propio del anfitrión, solo lectura) |
| PE | `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` |

Herramientas: los scripts `pedis.py`/`xref.py` de `observador/scripts` (copiados al scratchpad de la sesión), un
barrido de rel32 y dos subcomandos nuevos de `tools/scripts/analyze_capture.py`: `acks` y `scenes` (§5).

## 1. Resumen

1. **ESC del invitado: lo único que falta es el 2810 [V].** De la escena `0x1A4` (la que abre `UseItemReq 2855`),
   el invitado recibe el 2981, el 2661, los dos 2111 y el 2524. El `DeleteSequencerAck` 2810 lo emite
   `0x2879440` hacia `[gestor+0x18]`, que es el Kliff del anfitrión, y le llega **solo** al cliente del anfitrión
   (HBC t=330.531). Es el único paquete de esa escena que no llega al invitado.
2. **Cómo se reconoce una escena del usuario 2 [V/E fuerte].** La arranca su propia petición (`UseItemReq 2855`,
   no 2223 ni 2547). Su 2661 va a su personaje (`A010031F`) con dueño de play data `A010031F` y un enlace
   `0x20000002→A010031F`. En el servidor, el secuenciador que borra `0x2879440` (rsi) tiene el id en `+0x30` y la
   clave de su dueño en `+0x98` (la lee `0x205D7C0`, a través de `0x2879753 → 0x20532B0`).
3. **Lo que espera el cliente del invitado [V].** Su manejador `0xC06330` lee `u32 actor | u64 secuenciador | u32 |
   u8` (17 bytes exactos). Aplica un efecto **al actor del paquete**: `vf150(0)` en su componente de transformación
   (`0xC0653C`). Después enruta al M de su propio personaje (`0x17FA6C0 → ClientField::vf48 0x9A5BE0`), que es donde
   cayó el 2661 de `0x1A4` (GB:2993–2994). Por eso la copia lleva **el personaje del usuario 2** como actor, igual que
   su 2661, su 2981 y su 2524.
4. **Por qué el emisor nativo y no duplicar en `take_frame` [V].** El 2810 nativo va a la sesión 1 y nunca pasa por
   `take_frame`, que solo es la Send de la sesión 2. Duplicarlo exigiría enganchar la salida del cliente propio del
   anfitrión y reescribir bytes. En cambio, el serializador `0x2A4E4C0` tiene un único llamador (`0x2879819`), solo
   lee sus argumentos y se puede llamar en el mismo hilo y en el mismo marco con un `ToTarget` del usuario 2. Es el
   patrón que ya se validó con el 2322 en la sesión 6B.
5. **El efecto secundario de `observer_hold` [V].** En GBC, los 465 NPC liberados tienen su 2723 **antes** que su 2005,
   y los 465 tienen también 2404 antes (1 374 paquetes). 431 claves siguen el patrón exacto
   `2404, 2404, 2404, 2723`; 10 tienen además `2586`. Como máximo hay 5 paquetes y 3 437 B por clave.
   `observer_hold_acks` los retiene por clave y los entrega en orden justo detrás del 2005.
6. **Build y tests [V]:** `.\scripts\build.ps1` compila sin avisos en los archivos tocados y pasan **23/23**. Incluye
   la prueba nueva `optd_scene_acks` y una prueba de integración con `child_hold` dentro de `optd_s3`.

## 2. Tarea 1: `observer_scene_copy`

### 2.1 Qué recibe cada cliente de la escena `0x1A4` [V]

`analyze_capture.py … scenes` (nuevo) y los `.log` de las capturas:

| Paquete | Invitado (GBC) | Cliente del anfitrión (HBC) | Destino en el servidor |
|---|---|---|---|
| 2981 `StageBranchStateAck` | t=153.653, actor `A010031F` | t=324.209, igual | buscador `exe+0x5B25418` (no ToTarget), HB 23:51:46 |
| 2661 `PrepareSequencerAck` | t=153.653, `A010031F`, dueño `A010031F` | — | `ToTarget` al usuario 2 (HB: `-> actor 0xA010031F … via ToTarget (session 0x3DDC40D00E0)`) |
| 2111 `UpdateStageChartEventAck` ×2 | t=153.961, actor `A0100001` | t=324.506, igual | llega a los dos |
| 2524 `StageOnBranchEventAck` | t=159.983, `A010031F` | t=330.531, igual | llega a los dos |
| **2810 `DeleteSequencerAck`** | **nunca** (`2810 in 0xC06330: hits 0`) | t=330.531, `01 00 10 A0 A4 01 …` (actor `A0100001`) | `ToTarget [gestor+0x18]` (HB 23:51:52.055 `corr=2810:A0100001:0x1A4:0x0`) |

Veredicto de `scenes A010031F` sobre GBC:
`sequencer 0x1A4: 2661 t=153.653 actor 0xA010031F | C>S 2173 x2, C>S 2436 x1, C>S 2591 x12, S>C 2111 x2, S>C 2524 x1,
S>C 2661 x1, S>C 2981 x1 -> NOT CLOSED`.

El 2111 cae en el M del invitado aunque lleve `A0100001`, porque el enrutado no depende del actor (B1,
`p8-observador-escenas.md` §14.2). **No hace falta copiar ni el 2111, ni el 2524, ni el 2981 [V]**: todos llegan.
Solo falta el 2810.

### 2.2 Cómo se identifica una escena del usuario 2

| Criterio | Evidencia | Uso |
|---|---|---|
| La petición que la arranca | `UseItemReq 2855` del invitado en GBC t=153.548, con su propio actor `1F 03 10 A0`. No es 2223 (solo salen 2223 de su personaje y de su acompañante) ni 2547 **[V]** | Informativo: no se engancha la petición |
| El 2661 que la prepara | A `A010031F`, play data `+0 = A010031F`, `+8 = 0x1A4`, enlace `0x20000002→A010031F` (GB:2992) **[V]** | **Libro** `osc::SceneBook`: cada 2661 que entra en la cola del usuario 2 (`take_frame`, tras el push real) |
| El secuenciador del servidor | En `0x2879440`, `rsi` es el secuenciador: `0x28794E4 mov rsi,[r15]` (sale del mapa del gestor); su id es `[rsi+0x30]` (`0x28797B0`, va a `r8`); `rsi` no se escribe entre `0x2879753` y `0x2879819` **[V]** | Coherencia: `[r8] == [rsi+0x30]` |
| Su dueño | `0x2879753 mov rcx,rsi ; call 0x20532B0` → `0x20532D5 mov rdi,rcx … 0x20532E6 call 0x205D7C0` → `0x205D83B mov r8d,[rsi+0x98] … call rbx` (`vf+0x28` del gestor de actores elegido por el reino, `0x205D7F8–0x205D811`). `0x205D7C0` tiene 17 llamadores en `.rsrc`, también del cliente (`0xA9E364`, `0xC08DB8`): es el «actor dueño» de la clase de secuenciador **[V]**. Que `+0x98` valga `A010031F` para `0x1A4` es **[E fuerte]**: es coherente con el 2524, que va a `A010031F` con el actor de `0x205D7C0` en `0x29330B0`. La línea `[decision]` lo imprime | **Condición:** `[rsi+0x98] ==` clave del personaje del usuario 2 |
| El destinatario nativo | `0x2879814 mov rcx,[r12+0x18]` (actor del gestor) y `ToTarget(rcx->vfE8())` en `0x28797BB–0x28797F4` **[V]** | **Condición:** `rcx == [r12+0x18]` y su clave ≠ la del usuario 2 (si fuera la suya, el nativo ya le llega) |

Se copia solo si **todo** cuadra (orden de comprobación en `observer_scene_copy_logic.h decide()`):
- la clave está activa;
- no es nuestra propia llamada;
- el retorno es `0x287981E`;
- el marco es coherente;
- hay un usuario 2 vivo y se conoce la clave de su personaje;
- el dueño `[rsi+0x98]` es el personaje del usuario 2;
- el 2810 nativo no le llega ya;
- no se ha pedido su logout;
- su 2661 llegó a la cola del usuario 2 en la misma época y en el mismo viaje, con actor y dueño iguales a su
  personaje;
- el hilo es del reino del servidor, tiene contexto de envío y no hay ningún agrupador abierto;
- el personaje del usuario 2 se lee bien: `[user+0xD0]`, su clave y `[actor+0xA0] == user`.

### 2.3 Qué hace el cliente del usuario 2 con el 2810 (`0xC06330`) [V]

| Paso | Instrucciones | Consecuencia para la copia |
|---|---|---|
| Formato | `0xC0635C–0xC0636D`: tamaño exacto; lectores `u32 [rbp+0x78]`, `u64 [rbp-0x58]`, `u32 [rbp-0x60]`, `u8 [rbp+0x70]`; `0xC064E6` exige que se consuma todo | El serializador nativo lo cumple por construcción |
| Listo | `0xC0641E–0xC06448`: si la clave no es de usuario, `0x8B53C0` devuelve «reintentar» mientras el actor no esté listo (`+0x5E != 0x10` o `+0xCF == 0`) | `A010031F` ya está listo: su 2661 se procesó (GB:2994) |
| Actor | `0xC06510 call 0x8A67E0`; si existe y `0x466060` acepta su tipo (`[[actor+0x88]+1]`) → `[[actor+0x68]+0x1A0]->vf150(0)` (`0xC0652C–0xC0653C`) | **El efecto va al actor del paquete.** Con `A0100001` se aplicaría a la marioneta del Kliff del anfitrión. Por eso la copia lleva el personaje del usuario 2 |
| Gestor | `0xC06563 call 0x17FA6C0` → `vf48` (`0xC0659B`) = `0x9A5BE0`, el M del dueño del campo en ese cliente (el propio usuario 2, por el 2174 reescrito) | El mismo M que el 2661 de `0x1A4`: `GuestStage [2661 route 0xC0612A] … manager 0x3F2EC323600 … of actor 0xA010031F` (GB:2993) |
| Borrado | `0xC065B5 call 0xA9D950(M, …, seq, &u32, u8)` | Lo hace el código nativo del cliente |

### 2.4 Por qué el emisor nativo [V]

- **`take_frame` no ve el 2810 nativo:** va a la sesión 1 (el cliente propio del anfitrión). Copiar sus bytes
  obligaría a enganchar la salida de la sesión 1 y a reescribir la clave del actor (paquete hecho a mano).
- **`0x2A4E4C0(actor, finder, &seq, &u32, &u8)` es un emisor puro:**
  - solo lee `[r8]` (`0x2A4E5C2`/`0x2A4E712`), `[r9]` (`0x2A4E5DD`/`0x2A4E73F`), `[5.º]` (`0x2A4E602`/`0x2A4E77B`) y
    `actor+0x60`;
  - reserva y envía el paquete en el hilo que lo llama, como en la llamada nativa.
- **Llamadores:** un único rel32, `0x2879819`, y ningún puntero absoluto (barrido de `.rsrc/.xtls/.text`). Sus
  primeros 5 bytes son el gancho de `observer_stage`.
- **Mismo hilo, mismo marco:** la copia se hace en ese gancho de entrada, justo antes de la llamada nativa, con los
  mismos valores de id, u32 y u8, que se copian a la pila del mod. El gancho guarda todos los registros y alinea la
  pila (`mid_hook.x86_64-windows.asm`), igual que para el 2322.
- **Reentrada:** la llamada interna vuelve a pasar por el gancho. `observer_stage` la registra como un 2810 más (con
  su `corr=`) y `observer_scene_copy` la reconoce con un `thread_local` y no decide nada.
- **Orden para el invitado:** el 2524 (por el buscador común) y después nuestro 2810, en el mismo procesamiento del
  2436. Es el orden que ve el anfitrión.

### 2.5 Implementación

| Archivo | Contenido |
|---|---|
| `src/mod/session2/observer_scene_copy_logic.h` | `osc::SceneBook` (2661/2810/2174 en la cola del usuario 2, por id, con época y viaje, acotado a 256), `CopyFacts`, `Why` y `decide()` |
| `src/mod/session2/observer_scene_copy.{h,cpp}` | Verificación de **11 regiones** de código (sitio `0x28797AD` de 113 B, cadena del dueño `0x287974C`/`0x20532D5`/`0x205D7DC`, cuerpo y opcode del serializador, constructor y `vf1/vf2` de `ToTarget`, búsqueda de agrupador y marca de reino), la entrada del serializador (prólogo o `jmp`) y las vtables. Decisión, emisión (`0x2A757A0` + `0x2A4E4C0`), confirmación en `take_frame` y líneas |
| `observer_stage.{h,cpp}` | El gancho del 2810 llama a `observer_scene_copy_on_delete`; el hilo verifica el código antes de poner los ganchos, apaga la copia si el gancho del 2810 no entra y vacía sus líneas |
| `server.cpp` | `take_frame` → `observer_scene_copy_on_out` tras el push real, solo hacia la sesión 2 viva; época y servidor sustituido → libro vacío |
| `session2.cpp`, `config.{h,cpp}` | Clave `observer_scene_copy`. Instala los ganchos de `observer_stage` en modo medida si `observer_stage_sync=0` |

Garantías:
- **Nunca las escenas del anfitrión [V código]:** el dueño `+0x98` debe ser el personaje del usuario 2 y su 2661
  debe haber llegado a la cola del usuario 2 (el 2661 de `0x1A5` va al anfitrión; en las pruebas da
  `NotUser2Scene`).
- **Nunca un segundo 2810 al anfitrión:** el nativo no se toca y la copia va solo a la sesión 2.
- **Nunca dos veces:** el 2810 que entra en la cola del usuario 2 cierra la entrada del libro, y un segundo borrado
  da `NotDelivered`.
- **Fallo:** si el código del juego falla dentro de la llamada (SEH), se registra `FAULT` y la copia se apaga para el
  resto de la partida.

### 2.6 Riesgos y límites [E]

- `0x2879440` sigue aplicando sus efectos de servidor sobre `[gestor+0x18]`, el personaje del anfitrión
  (`0x287985C` y siguientes: `[[actor+0xA0]+0xD8]->vfC0`, `0x2929080`, eventos `vf+0x740`), aunque la escena sea del
  usuario 2. No se ha estudiado si eso afecta al anfitrión ni si al usuario 2 le falta algún efecto de servidor
  equivalente. La copia solo da al cliente el cierre que espera.
- `[rsi+0x98]` como dueño es [E fuerte] (§2.2). Si no lo fuera, la línea `[decision]` diría
  `NOT COPIED: not user 2's scene (owner [seq+0x98] 0x…)` con el valor real. **No copiaría nada erróneo.**
- Una escena que sobreviva a un viaje del usuario 2 no se copia (`OtherTrip`), y con un agrupador abierto tampoco
  (`ScopeOpen`). Ambos casos quedan contados y registrados.

## 3. Tarea 2: `observer_hold_acks`

### 3.1 Medida [V]

`analyze_capture.py GBC acks` (nuevo; salida resumida):

```
2078 keys with a 2005
per-actor packets BEFORE their key's first 2005 (dropped by the client):
  StartSpawnActionAck            2723 before     465 (465 keys) | after 0
  ChangeAIStateAck               2404 before    1375 (466 keys) | after 5073
  DataDefinedRegenerateValueAck  2586 before      19 (10 keys)  | after 421
  StartActionAck                 2991 before       2 (2 keys)   | after 11773
  MoveActorAck                   2697 before       6 (6 keys)   | after 20009
  UpdateElementalAck             2642 before       9 (7 keys)   | after 16626
  ApplyBuffAck / ClearAllyGroupDataAck / SetMinStatRateAck / VaryDataDefinedStaticStatAck: 1 key (B0100320,
  el acompañante del propio invitado, no retenido)
-> keys with a 2723 BEFORE their 2005: 465 (the observer_hold side effect)
```

- **[V]** Para las 465 claves retenidas, los patrones son `2404,2404,2404,2723` ×431, `2404,2404,2723` ×18,
  `2404,2404,2586,2586,2723` ×9, `2404×4,2723` ×6 y `2404×3,2586,2723` ×1. Como máximo hay 5 paquetes y 3 437 B
  por clave.
- **[V]** El 2723 es siempre el último, pegado al 2005 (mediana −1 µs). Sale dentro de `StartActionReq::vf2`, antes
  de que `child_hold` libere.
- **[V]** El 2005 de `B0100425` empieza por `15 1A 06 25 04 10 B0`: registro de `0x1A15` B, tipo 6. El analizador
  de claves lo lee igual que `parse_2005`.

### 3.2 Diseño

- **Qué se retiene:** los opcodes por actor con la clave en `+0`, los que la captura muestra antes de un 2005 (`2723,
  2404, 2586, 2991, 2697, 2642, 2506, 2402, 2645, 2434`). Solo de las claves que `observer_hold` retiene **para la
  sesión 2 viva como observador**. El controlador nunca se retiene en `child_hold`, y lo que va al cliente del
  anfitrión no pasa por `take_frame`.
- **Cuándo empieza:** `child_hold::record()`, cuando la entrada NPC (clave, sesión 2) se añade o se refresca.
  `observer_hold_acks_begin` se llama **fuera** del candado de `child_hold`.
- **`take_frame`:** antes de encolar, `observer_hold_acks_route()`:
  - un opcode retenido de una clave en espera → se guarda en orden (`Held`, no se encola);
  - un 2005 que anuncia claves en espera → bajo el candado propio, se encola el 2005 y justo detrás los paquetes
    guardados de esas claves, en su orden original (una secuencia común a todas las claves) (`Pushed`);
  - lo demás → `Pass`, como antes.
- **Fin sin 2005:**
  - una retención soltada por `child_hold` (`npc_pass` → `Drop`: despawn, fuera de alcance, época, observador sin
    usuario) → `observer_hold_acks_drop` descarta sus paquetes;
  - una liberación cuyo 2005 no pasó por `take_frame` durante su `NotifyAppear` → 2 s más, y después se descarta;
  - cualquier retención de más de 40 s → se descarta;
  - usuario 2 ausente o saliendo (`tick` del hilo de red), nueva época o servidor sustituido → se vacía.
- **Cotas:** 1024 claves (= la tabla de `observer_hold`), 32 paquetes y 32 KB por clave, 4 MB en total. Si se pasa
  de una cota, esa clave **se derrama**: sus paquetes guardados y el actual salen en el acto, en orden (exactamente
  lo de antes, sin la clave), y el resto de su retención pasa de forma nativa.
- **Candados:** el de `observer_hold_acks` es una hoja (dentro solo se toma el de `g_outgoing.push`).
  `child_hold` lo llama sin su `g_mu` tomado. `take_frame` nunca llama a `child_hold`.
- **Medida (describe):**
  `observer hold acks: keys withheld from user 2 N (again, table full; now, peak) | packets held H (2404 xa 2723 xb …)
  | after their 2005: P packet(s) of K key(s) (wait mean/max, at most M per key), 2005 with nothing held E |
  discarded: <motivo>: pkt / key | spilled … | now …, bytes (peak) | queue refused | 2042 bundles while holding |
  calls: begin, drop, released`.
  Más unas pocas líneas por clave: `NPC 0x… is withheld from user 2: its 2404 … waits for its 2005`,
  `NPC 0x…: its 2005 went to user 2, then its 4 held packet(s) in their order (2404 .. 2723), held N ms`,
  `… discarded: …`.

### 3.3 Riesgos [E]

- **Estado más viejo detrás de un 2005 más nuevo.** El 2005 liberado se serializa al liberar (restauración 1, acción
  y posición actuales). Los 2404/2697/2991 guardados son anteriores y se aplican después. El estado final converge
  al último, pero el NPC puede repetir un instante de un estado viejo. En 6B, las claves retenidas solo tenían
  2404/2723/2586: no había 2697 ni 2991.
- **El 2723 en un remoto.** `p8-impl-s3.md` §1.3: `vf188(5)` devuelve AL 0 («éxito sin aplicar») en un actor de
  control remoto. Que el 2723 entregado tras el 2005 cambie algo visible es [E]. Lo medirán las sondas S3 del
  invitado (§6).
- **Ventana sin retener.** Entre la división de `NotifyAppear` (el 2005 al controlador) y `record()`, un paquete del
  NPC emitido en otro hilo del servidor pasaría de forma nativa (y se perdería como hoy).
- **Agrupadores 2042.** Un paquete por NPC dentro de un 2042 no se ve. Se cuenta («2042 bundles while holding»). En
  las sesiones 5 y 6 no llegó ningún 2042 a la sesión 2.

## 4. Archivos tocados

| Archivo | Cambio |
|---|---|
| `src/mod/session2/observer_scene_copy_logic.h`, `observer_scene_copy.{h,cpp}` | nuevos (§2.5) |
| `src/mod/session2/observer_hold_acks_logic.h`, `observer_hold_acks.{h,cpp}` | nuevos (§3.2) |
| `src/mod/session2/server.cpp` | `take_frame`: `to_live`, `observer_hold_acks_route` antes del push, `observer_scene_copy_on_out` tras el push; `push_outgoing`; época y servidor sustituido |
| `src/mod/session2/child_hold.cpp` | `record_npc_locked` devuelve si quedó en la tabla; `record()` avisa a `observer_hold_acks_begin` tras soltar el candado (`unique_lock`); `npc_pass` (Drop) → `observer_hold_acks_drop`; `release_npcs` → `observer_hold_acks_released`; `ack_session()` |
| `src/mod/session2/observer_stage.{h,cpp}` | Llamada desde el gancho del 2810, verificación, apagado si falta el gancho, vaciado de líneas y contadores |
| `src/mod/session2/session2.cpp`, `src/mod/config.{h,cpp}` | Claves, instalación, `tick` y describe |
| `CMakeLists.txt` | Fuentes nuevas; `observer_hold_acks.cpp` en `optd_child_hold` y `optd_s3`; prueba `optd_scene_acks` |
| `tests/optd_scene_acks_tests.cpp` | Nuevo |
| `tests/optd_s3_tests.cpp`, `tests/optd_child_hold_tests.cpp` | Stub de `server_live_session2`; integración `child_hold` ↔ `observer_hold_acks` |
| `tools/scripts/analyze_capture.py` | Subcomandos `acks` y `scenes` |
| `docs/research/ini/cdmp.p8-escenas-host.ini`, `cdmp.p8-escenas-local.ini` | Nuevos (§5, §6) |

## 5. Verificación en un PC (anfitrión + `cdmp_peer`)

`bin64\cdmp.ini` = `docs/research/ini/cdmp.p8-escenas-local.ini`. Es `cdmp.p8-2322-local.ini` más las dos claves,
sin `mirror_host`. **Guion:** `login_e3lc00soak_child_112627.troc`, el de D-48 con su primera acción. **No** uses
`…act…`, que está obsoleto (`p8-crash-esc-1831598.md`).

Al arrancar deben salir estas líneas:
- `Session2: observer_hold_acks ON: …`
- `Session2: observer_scene_copy wanted…`
- `ObserverSceneCopy ON (observer_scene_copy=1): … 11 code regions + serializer entry + vtables checked`
- `ObserverStage ON (…) … | its 2810 hook also hands each 2810 to observer_scene_copy …`

Si aparece `ObserverSceneCopy: REFUSED` u `OFF`, para y manda el log.

### 5.1 `observer_hold_acks`

```
.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00soak_child_112627.troc --var SAVE_SLOT=<ranura del anfitrión> --record D:\cdmp-grabaciones\acks-1.bin
python -I -B tools\scripts\analyze_capture.py D:\cdmp-grabaciones\acks-1.bin acks
```

- **Grabación:** `keys with a 2723 BEFORE their 2005: 0 (none: every spawn action follows its 2005)`. Los 2723 y
  2404 de esas claves pasan a la columna `after`.
- **Control:** la misma prueba con `observer_hold_acks=0`. Debe dar `> 0`, como la sesión 6B.
- **Log del anfitrión:**
  - `observer hold: held NPC …` (de `child_hold`);
  - `observer hold acks: NPC 0x… is withheld from user 2: its 2404 …`;
  - `… its 2005 went to user 2, then its N held packet(s) in their order (2404 .. 2723), held X ms`;
  - en el describe: `after their 2005: P packet(s) of K key(s)`, con K parecido a `observer hold … released`,
    `queue refused 0` y `spilled 0`.
- **Al final del guion (despedida):** `… held packet(s) of … key(s) discarded: user 2 gone or leaving` (si quedaba
  algo) y `now 0 packet(s)`.

### 5.2 `observer_scene_copy`

1. **Negativo (escena del anfitrión).** Con el peer dentro del campo del anfitrión, el anfitrión abre un objeto
   con ESC («ver objeto») y lo cierra.
   - Esperado: `ObserverSceneCopy [decision] … DeleteSequencerAck 2810 of sequencer 0x… (owner [seq+0x98]
     0xA0100001) natively to 0xA0100001 … NOT COPIED: not user 2's scene …`.
   - En la grabación del peer, ninguna línea de `scenes` para esa escena (su 2661 no le llega).
   - Así se comprueba también **[V en vivo]** que `+0x98` es el dueño en una escena del anfitrión.
2. **Positivo (escena del usuario 2), opcional [E].** Una copia **local** del guion `soak_child` con estas líneas
   antes de `wait 300000`. Son los bytes del invitado en 6B; la `UseItemReq` del anfitrión para el mismo objeto
   (HBC t=370.684) es idéntica salvo el actor, y el peer carga la misma partida:
   ```
   learn SCENE u64 2661 9  # PrepareSequencerAck: play data +8 (el id del secuenciador)
   send UseItemReq 2855 0 1f0310a0a5420f00000000007a450f00000005000100000000000000ffffffff000000000000
   put u32 0 PLAYER_KEY
   expect_learned SCENE 15000
   wait 300
   send UpdateSequencerGamePlayDataEventReq 2173 0 a4010000000000000000006001000f
   put u64 0 SCENE
   send UpdateSequencerGamePlayDataEventReq 2173 0 a4010000000000000100006001000f
   put u64 0 SCENE
   send UpdateStageChartEventReq 2591 0 a401000000000000000000000000000001
   put u64 0 SCENE
   send UpdateStageChartEventReq 2591 0 a401000000000000000000000100000001
   put u64 0 SCENE
   wait 6000
   send StageBranchStateReq 2436 0 a401000000000000000000000000
   put u64 0 SCENE
   expect 2810 15000  # DeleteSequencerAck: con observer_scene_copy=1
   ```
   - El analizador del peer acepta el fragmento (10 pasos; comprobado con `cdmp_peer`).
   - Usa el objeto `0x0F42A5` y lo consume en la copia en memoria del usuario 2. El guardián de guardado impide
     escribirlo.
   - **Esperado en el anfitrión:**
     - `ObserverSceneCopy [2661] 2661 of sequencer 0x… reached user 2's queue (actor 0xA01…, owner 0xA01…) … user 2's
       own scene: its 2810 is watched`;
     - después, `ObserverStage [2810 DeleteSequencerAck] … -> actor 0xA0100001 …` (el nativo);
     - `ObserverSceneCopy [decision] … COPY to user 2: … corr=2810:<usuario 2>:0x…:0x0`;
     - `ObserverStage [2810 …] -> actor <usuario 2> … via ToTarget` (la llamada interna);
     - `ObserverSceneCopy [2810] a 2810 of sequencer 0x… reached user 2's queue (OUR COPY) …: closed in the book`;
     - `ObserverSceneCopy [copied] … its 2810 IS in user 2's queue (x1)`.
   - **En la grabación:** `analyze_capture.py acks-2.bin scenes <clave del usuario 2>` →
     `CLOSED by 2810 (actor <usuario 2>)`.
   - Si el servidor no prepara la escena (`expect_learned SCENE` vence), el peer no tiene ese objeto en esa ranura:
     sirve igual el punto 1.
3. **Contadores (cada 60 s):** `ObserverSceneCopy (on) | 2810 decisions: … | copies N (in user 2's queue N, LOST 0,
   not seen 0, faults 0)`.

Lo que **no** puede probar un PC: que el cliente real del usuario 2 cierre la escena y recupere el control. Eso
queda para Shadow.

## 6. Con Shadow: qué mirar

Anfitrión: `cdmp.p8-escenas-host.ini` (= `cdmp.p8-2322-host-B.ini` + las dos claves). Shadow:
`cdmp.p8-2322-shadow.ini` sin cambios (trae `guest_stage_diag=1`, `guest_s3_diag=1`, `load_diag=1`). Mismo `cdmp.asi`
en los dos PC. Guardar los dos `cdmp.log` y las dos capturas.

**ESC «ver objeto» del invitado, dentro del campo del anfitrión:**

| Dónde | Confirma | Refuta |
|---|---|---|
| Anfitrión | `ObserverSceneCopy [decision] … sequencer 0x… (owner [seq+0x98] 0xA01…<usuario 2>) natively to 0xA0100001 … COPY to user 2 … corr=2810:<usuario 2>:0x…:0x0` y `[copied] … IS in user 2's queue (x1)` | `NOT COPIED: <motivo>`, `FAULT`, `NOT SEEN`, `LOST` |
| Invitado | `GuestStage [2810 in 0xC06330] … corr=2810:<usuario 2>:0x…:0x0` (el mismo `corr=`) y `[2810 route 0xC0659E] … manager 0x…` **igual** al del `[2661 route 0xC0612A]` de esa escena | `2810 in 0xC06330: hits 0` en el resumen |
| Juego | Al cerrar la vista del objeto, el invitado **recupera el control** del personaje | Sigue sin control |
| Captura del invitado | `analyze_capture.py … scenes <usuario 2>`: `CLOSED by 2810 (actor <usuario 2>)` | `NOT CLOSED` |

Además, el ESC del anfitrión con el invitado dentro debe dar `NOT COPIED: not user 2's scene`, y en la captura del
invitado no debe aparecer ningún 2810 de esa escena (no tenía su 2661).

**NPC (efecto de `observer_hold`):**

| Dónde | Confirma | Refuta |
|---|---|---|
| Captura del invitado | `analyze_capture.py … acks`: `keys with a 2723 BEFORE their 2005: 0`; `2404 before` ≈ solo claves no retenidas | 2723 antes de su 2005 |
| Invitado (`GuestS3`) | `2723 consumer` > 0 (era 0). Sus líneas `vf188(5)` (`AL 0 … WITHOUT applying` en remotos) o `StartAction … from 2723`. `null action 0`, `guard skips 0` sin cambio | `NULL ACTION`, `STUCK`, guardas que suben |
| Anfitrión | Describe `observer hold acks: … after their 2005: P packet(s) of K key(s)` con K ≈ `released` de `observer hold`; `spilled 0`; `queue refused 0`; `discarded` solo por drops | `spilled`/`queue refused` > 0 |
| Juego | Los NPC liberados por S3 aparecen en su estado de IA (no en el estado por defecto) | Sin cambio visible: queda medido, sin regresión |

Si algo sale en «Refuta», hay que apagar esa clave sola (cada una es independiente) y guardar el log.

## 7. Build y pruebas [V]

`.\scripts\build.ps1` (RelWithDebInfo): compila sin avisos en los archivos tocados y pasan **23/23 tests** (0,07 s
`optd_scene_acks`, 0,31 s `optd_s3`).

- **`optd_scene_acks` (nuevo, solo lógica, con paquetes reales de 6B):**
  - el libro sobre el 2661 de `0x1A4` (102 B) y el de `0x1A5`: viaje, época, reemplazo, cierre por el 2810, cota;
  - la decisión: cada motivo de rechazo en su orden; la escena del anfitrión nunca se copia; tras la copia, un
    segundo borrado da `NotDelivered`; el 2810 copiado solo difiere del nativo en la clave del actor;
  - el filtro de opcode y clave con los prefijos reales de `B0100425`;
  - el analizador de claves del 2005 sobre las dos fixtures de la sesión 4 (`B01002A0`; `B0100078–7B`) y la
    cabecera real de `B0100425`;
  - la retención: el patrón `2404×3, 2723` intercalado con otra clave, el orden común por secuencia, la primera
    marca por clave, el fin al llegar el 2005, `drop`, huérfanos (justo antes y justo después de 2 s), nueva
    retención tras liberar, caducidad a 40 s, época, vaciado, cotas por clave y total, y derrame en orden.
- **`optd_s3` (integración con `child_hold` y los dobles de prueba existentes):**
  - el Crow retenido para la sesión 2 → sus 2404×3 y su 2723 se guardan, y los de otra clave pasan;
  - la `StartActionReq` del Crow → `NotifyAppear([S2],[Crow])` produce su 2005 en `take_frame` y detrás salen los 4
    paquetes en orden; después pasan;
  - una retención soltada (fuera de alcance) los descarta;
  - un NPC que controla el usuario 2 (retenido para el anfitrión) no retiene nada;
  - la salida del usuario 2 vacía;
  - con la clave apagada no cambia nada.
- **No prueban:** el gancho, la ABI real de `0x2A757A0`/`0x2A4E4C0`, el hilo en vivo, `take_frame` en el juego ni
  el comportamiento del cliente.

## 8. Pendiente

- Confirmar en vivo `[rsi+0x98]` (§2.2): con el punto 1 de §5.2 en un PC y con Shadow.
- Los efectos de servidor de `0x2879440` sobre `[gestor+0x18]` en una escena del usuario 2 (§2.6).
- Si la entrega de los 2404/2723 tras el 2005 cambia algo visible en los NPC (§3.3): sondas S3 del invitado.

## Correcciones tras la revisión de la noche (2026-10-10)

Detalle completo en `p8-revision-noche.md` §6. `observer_hold_acks`:

1. Un 2005 que termina la retención de una clave **sin nada retenido** se encola en la ruta, bajo su candado
   (`Pushed`). Un paquete de esa clave que otro hilo ofrezca después ya no puede adelantarse.
2. Las claves que la división retiene al usuario 2 se registran **antes** de las llamadas nativas de la división
   (`pre_begin_acks`), no al final de `record()`. Una clave nueva que al final no queda en la tabla se suelta.
3. Una clave retenida otra vez (`Again`) cuenta sus 40 s desde la última retención.

Tests en `optd_scene_acks` y `optd_character_hold`. `observer_scene_copy`: una excepción dentro del emisor
`0x2A4E4C0` ya no se absorbe. Apaga la copia, escribe una línea `ERR` y sigue como cierre real. El constructor
`0x2A757A0` (sin candados [V]) sí se atrapa.
