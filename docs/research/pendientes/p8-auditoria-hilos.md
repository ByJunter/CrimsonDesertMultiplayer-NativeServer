# P8: auditoría de hilos — qué hilo hace cada trabajo del juego que hace el mod

10 de octubre de 2026, rama `opcion-d` (sobre `c64b13a`). Build del juego 1.0.0.2976, PE timestamp `0x6AB28F00`,
direcciones en **RVA** (base `0x140000000`). Continúa `p8-heap-inyeccion.md` (el cierre del anfitrión por el heap
global `0x47EFA78` usado desde el hilo de red del mod).

**[V]** = comprobado (código del mod + referencias directas en el PE de solo lectura
`D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, capstone, Python `-I`, o documento con volcado).
**[E]** = inferencia.

No se lanzó el juego, no se tocó `bin64` y no se hizo commit. No se añadió ningún gancho. El único cambio de gancho es
que el invitado comprueba ahora los **39 bytes** de `0x35BAD4` (`kNetLoopPumpAob`, los mismos que el anfitrión; están
en `.rsrc` y son únicos en el archivo [V]) en vez de 14.

## 1. Regla y veredicto

**Regla** (de `p8-heap-inyeccion.md` §3): ningún hilo del mod llama a código del juego que asigne, libere, empuje a
colas del juego, tome candados del juego o cambie estructuras del juego. Ese trabajo se hace dentro de un gancho, en
el hilo del juego que lo hace de forma nativa. Como mínimo, se hace en un hilo del juego con su propio heap
(`0x47EF9B0`) y nunca con el heap global (`0x47EFA78`).

**Veredicto:**

1. **Invitado** (`guest.cpp` y `guest_*`) [V]: **ninguna infracción**. El aviso del agente anterior no se cumple en el
   código actual. `g_push` (`0x35DF50`) y `OnRecvPacket` (`0x35CA00`) solo se llaman desde el NetworkThread del
   cliente, a través de dos rutas:
   - la bomba `0x35BAD4`;
   - el gancho de `Send` `0x14380B0`, al que solo se llega desde `FlushSendQueue`, que a su vez solo se llama desde
     ese bucle.

   `guest_deliver` corre en el hilo de red del mod y solo encola. No llama a nada del juego. Sí se reforzó y se midió
   (§5): FIFO con dos cotas, como mucho 4096 por vuelta, medidas de espera y una comprobación de hilo en tiempo de
   ejecución.
2. **Resto del repo** [V]: ningún hilo del mod hace trabajo de heap, de cola o de candado del juego. El único
   `0x47EFA78` que queda (`server.cpp:223`) corre en hilos del juego y solo si `TLS+0x1FD ≠ 0`, cosa que no se vio
   nunca. Hay cinco casos límite fuera del invitado (§4).
3. Lo que más se acerca a «hilo del mod» son las **escrituras de código** al instalar ganchos desde hilos del mod. No
   tocan el heap. Una de ellas no congela los hilos (§4, punto 1).

## 2. Hechos nativos usados (referencias directas en el PE) [V]

Se escanearon `call/jmp rel32` en `.rsrc` y `.xtls`. Las funciones contenedoras se sacaron de `.pdata`.

| Función | Llamadores directos | Contenedor | Hilo |
|---|---|---|---|
| `ClientNet::OnRecvPacket 0x35CA00` | `0x35BB53` | bucle `0x35B800` | NetworkThread |
| | `0x35C0ED` | `FlushSendQueue 0x35BE90` | NetworkThread |
| | `0x35C951` | `0x35C890` (llamada en `0x35BD04`, dentro de `0x35B800`) | NetworkThread |
| | `.xtls 0x86E55AA` | `0x86E5500` (por `jmp` desde `0x35C490`, llamada en `0x35BD1A`, `0x35BFE3`, `0x35C009` y `0x35C02C`) | NetworkThread |
| `FlushSendQueue 0x35BE90` | `0x35BB05`, `0x35BC99` | `0x35B800` | NetworkThread |
| `NwVirtualSyncSession::Send 0x14380B0` | ninguno directo (vf6 de la vtable `0x5726248`, llamada en `FlushSendQueue 0x35BFC7`, `opcion-d-p8.md` §1.2) | — | NetworkThread [V para la ruta conocida, E para «ninguna otra llamada indirecta»] |
| `0x35B800` | `0x35B79A` | el procedimiento del hilo `0x35B6E0` | NetworkThread |
| `0x1870C20` (refrescar ranuras) | `0x18712BA`, `0x278318A`, `0x2A224CD`, `0x2C176DA` | `DeleteSlot 0x1871040`, `0x2782DF0` (NG+), `SaveDataSlotRefreshReq 0x2A22380`, `Server::UserLogin 0x2C17520` | **servidor**, ningún llamador es del cliente |
| `0x35DF50` (push de cola) | 141 coincidencias de la cabecera; en las colas de red: Q0 desde el NetworkThread (`0x14380CA`), Q3 desde ServerRunThread (`0x27735E4`), Q2 desde el NetworkThread (`0x35BB60`) | — | varios hilos del juego |

Bytes de los sitios del invitado, comprobados en el PE:

| Sitio | Bytes | Sección | Apariciones |
|---|---|---|---|
| `0x35BAD4` | 39 | `.rsrc` | 1 |
| `0x14380B0` | 16 | `.rsrc` | 1 |
| `0x35DF50` (cabecera) | 21 | `.rsrc` | — |

La cabecera de `0x35CA00` cuadra con `kClientOnRecvPacketHead`.

## 3. Tabla por llamada

Columnas:

- **Nativo**: si es el hilo que hace esa operación en el juego.
- **Puerta**: la opción del `cdmp.ini` que lo activa. Todas valen 0 por defecto, salvo el sniffer y el overlay.

### 3.1 Invitado (`src/mod/session2/guest*.cpp`, `load_trip.cpp`, `report_guard.cpp`)

| Archivo:línea | Llamada o escritura | Qué hace | Hilo (prueba) | Nativo | Puerta |
|---|---|---|---|---|---|
| `guest.cpp:720` → `:197` | `0x35DF50` push de `pb` a Q3 `[ClientNet+0x28]` | push de cola; puede asignar bloques con `0x35FB00` y el heap **de ese hilo** | `guest_send` = desvío inline de `0x14380B0`, solo desde `FlushSendQueue` ⇒ NetworkThread [V] (§2) | **hilo del juego con heap propio**. El NetworkThread empuja de forma nativa a Q0 y Q2 con esta función. A Q3 empuja ServerRunThread. El propio NetworkThread vacía Q3 en la misma vuelta (`0x35BB7B`–`0x35BBE6`) [V] | `guest=1` y conexión REMOTA |
| `guest.cpp:672` | original de `0x14380B0` (`unsafe_call`) | push a Q0 local | dentro de su propio desvío ⇒ NetworkThread [V] | sí | `guest=1` (observe o conexión local) |
| `guest.cpp:757` → `:206` | `ClientNet::OnRecvPacket 0x35CA00` con paquetes del anfitrión | copia el paquete (`0x1439060` + memcpy) con el heap del hilo y lo encola para el hilo lógico | `guest_pump` = mid-hook de `0x35BAD4` en `0x35B800` ⇒ NetworkThread [V] | **sí** (`0x35BB53`) [V] | REMOTA |
| `guest.cpp:794` → `:206` | `0x35CA00` con el 2003 y el 2002 locales (`answer_goodbye`) | igual | `guest_pump` ⇒ NetworkThread [V] | sí [V] | REMOTA, despedida sin respuesta y sin campo |
| `guest.cpp:516/529` → `:389/:399` | `WindowsLock<3>` vf1 y vf2 del gestor de guardado | candado del juego (`EnterCriticalSection`) | `guest_pump` → `pump_title_refresh` ⇒ NetworkThread [V] | **no nativo**: el original corre en un hilo del servidor (§2). Es un hilo del juego y sigue el mismo protocolo de candado y byte `+0x60` que `0x2A22475` (`p8-titulo.md`) | REMOTA tras el 2490, o `guest_slot_refresh_test=1` |
| `guest.cpp:526` → `:410` | `0x1870C20` (`jmp .xtls 0xCBD92D0`) | rehace la tabla de ranuras: **asigna y libera** | NetworkThread [V] | **no nativo** (servidor) [V]; heap propio del NetworkThread. mimalloc acepta liberar desde otro hilo [E, `p8-titulo.md` §«Hilo/heap»] | igual |
| `guest.cpp:832` | `TerminateProcess` | fin del proceso | NetworkThread | n/a | REMOTA, despedida con campo y sin anfitrión |
| `guest.cpp:1060…` (`guest_deliver`) | — | analiza, registra y **encola** (`gin::InboundQueue`, heap del CRT del mod) | hilo de red del mod (`mod.cpp:357`, `TrocLink` dentro de `run_network`) [V] | n/a: **ninguna llamada al juego**. Solo `safe_read` de `exe+0x6A65564` en `note_in` | `guest=1` y `[net]` |
| `guest.cpp:957/958` | instalación de ganchos (`create_mid`, `create_inline`) | escribe código; safetyhook congela los hilos | `install_thread` del invitado (hilo del mod) | n/a (instalación) | `guest=1` |
| `guest_s3.cpp:348` | original de `StartAction 0x863530` | lo que haga el original | dentro de su propio desvío ⇒ hilo del juego [V] | sí | `guest_s3_diag` |
| `guest_s3.cpp:278` | `ctx.rip` → `ret`: se salta `0x232D5C0` | cambia el flujo del juego (guarda) | dentro del mid-hook ⇒ hilo de actualización del juego [E] | sí | `guest_update_guard` |
| `guest_s3.cpp:207–344` | `0x232D5C0`, `0x85FBA0`, `0x48B120/0x48B197`, `0x48AD40` | lectura y anillo propio | ganchos ⇒ hilos del juego | sí, solo lectura | `guest_s3_diag` |
| `guest_s3.cpp` (captura) | UEF propio, `MiniDumpWriteDump` en `dump_thread`, `TerminateProcess` | — | hilo que falla, hilo del mod | n/a, no es código del juego | `guest_crash_dump` |
| `guest_actor_diag.cpp:673/711` | mid-hooks `0x4FD2E0…`, `0x8646FC/0x864B1D`, `0x7798D0` | solo lectura y anillo | hilos del juego | sí | `guest_actor_diag` |
| `guest_actor_diag.cpp:726` → `remote_puppet.cpp:524–657` | `0x1F63F70` (`[rsp+0x28]=0`, `rip`→`ret`), `0x2329700` (`rip`→`ret`) | cambia el flujo (guarda) | dentro de los mid-hooks ⇒ hilo de actualización [E] | sí | `guest_layer_guard` |
| `guest_actor_diag.cpp:499` y siguientes, `guest_stage_diag.cpp`, `load_trip.cpp` | `safe_read` del WorldSystem, el gestor, el estado P y la lista de espera | **solo lectura**, sin candado | hilos del mod (instalación cada 1 s; `guest_diag_worker` cada 5 s) | n/a; puede leer un dato viejo [E] | `guest_actor_diag`, `guest_stage_diag`, `load_diag` |
| `guest_stage_diag.cpp:926` | mid-hooks del P8 fase 2 | solo lectura | hilos del juego | sí | `guest_stage_diag` |
| `report_guard.cpp:44` | desvío de `0x1317D40` | **no** llama al original | hilo que informa del error | n/a | siempre con `guest=1` |
| `guest_lifecycle.cpp` (lo usa `server.cpp` desde el hilo de red del anfitrión) | `read`, `read_ptr`, `safe_read` del SAM y del campo | **solo lectura** [V: sin escrituras ni llamadas] | hilo de red del mod (anfitrión) | n/a | `reconnect` y C4 |

### 3.2 Anfitrión (`session2/` sin `guest*`)

Fuente: auditoría de solo lectura de este mismo trabajo. Se repasaron a mano `server.cpp:223/263/482` y
`p8-heap-inyeccion.md`. Los archivos son del otro agente: aquí **no se cambió nada**.

| Archivo:línea | Llamada | Hilo | Nativo | Puerta |
|---|---|---|---|---|
| `server.cpp:312` → `:223/225` | `0x47EF9B0` (`0x47EFA78` solo si `TLS+0x1FD`) para el objeto de sesión | ServerRunThread (mid-hook `0x2773C20`) [V] | hilo del juego, heap propio; el constructor nativo (`ServerModule::Init`) corre en un hilo desconocido [E] | `create_session` |
| `server.cpp:315` | constructor `0x1436EE0` | igual | igual | `create_session` |
| `server.cpp:1048` → `:223/225`, `:1070` | asignar el búfer y empujarlo a Q0 con `0x35DF50` | NetworkThread (mid-hook `0x35BAD4`, `r15 == ClientNet`) [V] | **sí** [V] | `accept_tunnel` |
| `server.cpp:482` → `:263` | `0x35DF50` push a **Q2** (`ServerNet+0x80`) en `session2_send` (vf9 sustituida) | el hilo del servidor que llama a `SendPacket` [V] | **hilo del juego no nativo**: de forma nativa empuja a Q2 el NetworkThread (`0x35BB60`). Mismo caso que Q3 en el invitado | `create_session` |
| `server.cpp:661/671` | `ToNear::vf1 0x2A75860` y los originales de `0x2A759B0/0x2A75880` | dentro de su desvío, hilo del servidor [E] | sí | `relay_fix` / `finder_log` |
| `child_hold.cpp:773/1192/1255` | `NotifyAppear 0x2B000C0` (emite 2005: asigna y envía) | desvío de `NotifyAppear` o de `StartActionReq vf2 0x296CAA0`, con reino servidor comprobado [V puerta, E hilo] | probable [E]; con `on_handler_thread=true` puede ser otro hilo del servidor | `child_hold` / `observer_hold` |
| `observer_milestone.cpp:251/260` | `ToTarget 0x2A757A0` + emisor `0x2999D80` | mid-hook `0x2BC84E2` en `MoveFieldCompleteCore`, rechazado sin `TLS+0x1EC` [V puerta] | sí [E] | `observer_milestone_send` |
| `field_share.cpp:57/253/255/271` | candado del campo, `TryAddRef 0x14343A0`, liberación, `vec_push 0x40B5B0` | desvíos de `0x2B07790` y `0x2BC04F0` [E] | sí | `share_field` |
| `clock_gate.cpp:224/227` | `0x20E55A0` + escritura de TS | desvío de `0x2BC0A30` [E] | sí | `clock_gate` con `create_session` |
| `identity_fix.cpp:132/166` | escritura atómica de 8 bytes (lobby, `Status+0x70`) | desvíos de `0x2B7F4D0` y `0x20688A0`, con reino servidor comprobado [V puerta] | sí | `identity_fix` |
| `identity_probe`, `companion_probe`, `p8_probes` | originales dentro de sus desvíos y mid-hooks de lectura | hilos del juego | sí | sus opciones |
| `session2.cpp`, `server.cpp:1155…` (hilo de red y espejo) | `server_inject_ex` → **cola propia** `g_q`; `hand_logout`; `child_hold_network_tick` | hilo de red del mod [V] | n/a: **ninguna llamada al juego**; solo lecturas protegidas | — |

### 3.3 `mod/net`, `mod/game`, `render`, `mod.cpp`

| Archivo:línea | Llamada | Hilo | Nativo | ¿En la opción D? |
|---|---|---|---|---|
| `troc_sniffer.cpp:147` | `PostPacket 0x35C670` (asigna 0x8030 y empuja) para inyectar C→S | dentro del mid-hook de entrada de `0x35C670`: el hilo que ya está enviando [V] | sí | el gancho sí (sniffer=1 por defecto); la inyección solo con `[dev] combat_tests` (desactivada en el invitado P8) |
| `troc_sniffer.cpp:213` | `OnRecvPacket 0x35CA00` para inyectar S→C | dentro del mid-hook de entrada de `0x35CA00` ⇒ NetworkThread [V, §2] | sí | solo con `[dev] clone_hotkeys` (desactivada en el invitado P8) |
| `remote_puppet.cpp:524–657`, `horse_diag.cpp:180/192` | `rip`→`ret` y parámetros de salida | dentro de los mid-hooks ⇒ hilos de actualización [E] | sí | guardas de capa y `horse_diag` (`session2.cpp:541/548`) |
| `aggro_test`, `puppet_minimap`, `local_capture`, `enemy_relay`, `player_hits`, `guest_hits`, `piece4_test`, `attack_probe` | originales dentro de sus desvíos o colas propias | hilos del juego o del mod (solo encolan) | sí / n/a | no (solo co-op o dev) |
| `game_errors.cpp:130–137` | **escribe 4 bytes de código** en `exe+0x35D8D1` sin congelar hilos | `late_init` (hilo del mod) | **no** (código, no heap) | sí, pero solo por la ruta de respaldo |
| `dx12_hook.cpp` | D3D12/DXGI (no es el exe) | hilo de `Present` | sí | overlay |
| `actor_lookup.cpp` y lectores de jugador y cámara | solo lectura, sin candado | hilos del mod | n/a | sí (`puppet_tick`) |

El mod enlaza el CRT estático (`CMakeLists.txt:11`). Sus `new` y `vector` van a su propio heap, nunca al mimalloc del
juego.

## 4. Casos fuera del invitado (para quien lleve esos archivos; aquí no se tocaron)

1. **`game_errors.cpp:130–137`**: es la ruta de respaldo cuando el mid-hook directo de `0x35D8D0` falla. El hilo
   `late_init` reescribe el `jmp rel32` con `VirtualProtect` + `memcpy`, **sin congelar** los hilos del juego. Si un
   hilo ejecuta ese `jmp` mientras se escribe, puede leer 4 bytes a medias.
   Arreglo: hacer la escritura dentro de `safetyhook::execute_while_frozen`, o quitar la ruta de respaldo
   («no enganchado»).
2. **`server.cpp:482` → `:263`, push a Q2 desde el hilo del servidor**: de forma nativa empuja a Q2 el NetworkThread
   (`0x35BB60`). El hilo actual es del juego y usa su propio heap, así que la regla del heap se cumple; solo cambia
   quién empuja.
   Para copiar la ruta nativa exacta: dejar `rq+0x30` en una lista propia y que `pump_body` (`0x35BAD4`) lo empuje a
   Q2.
3. **`child_hold.cpp:1192` vía `:1428`**: `NotifyAppear` se llama desde el desvío de `StartActionReq` en cualquier
   hilo del reino servidor, no necesariamente en el último que llamó a `NotifyAppear` [E].
   Para ser estrictos: exigir `GetCurrentThreadId() == g_appear_tid`, o aplazar la liberación al siguiente
   `notify_detour`.
   **Hecho (2026-10-10, [p8-character-hold.md](p8-character-hold.md) §4):** solo se llama en un hilo que el
   juego usó para `NotifyAppear` (retorno `0x2AF90F0`/`0x2AF92D6`); si no, se aplaza a la cola del siguiente
   `NotifyAppear` nativo. Vale para hijos, NPC y personajes.
4. **`troc_sniffer.cpp:193/230`**: ocurre en el hilo nativo, pero la inyección C→S usa el índice de sesión
   (`ctx.r8`) del `PostPacket` real siguiente. La S→C puede entrar dentro de `FlushSendQueue` (llamadores
   `0x35C0ED`/`0x86E55AA`) y no solo desde `0x35BB53`. Ninguna de las dos está activa en la opción D.
   Si se vuelve a usar: filtrar por `ctx.r8` y por la dirección de retorno `0x35BB58`.
5. **Instalación de ganchos desde hilos del mod con el juego en marcha**: `stage_thread` (`observer_stage.cpp:634`) y
   el `probe_thread` del P8 (`p8_probes.cpp:1062/1100`). safetyhook congela los hilos, así que no es trabajo de heap.
   Aun así conviene instalarlos antes de abrir el túnel o fuera de una carga.

Ninguno de estos explica el cierre del 9 de octubre: ese fue `0x47EFA78` en el hilo de red del mod, y ya está
corregido en `c64b13a`.

## 5. Cambios en el invitado

Archivos: `src/mod/session2/guest_inbound_logic.h` (nuevo, lógica pura), `src/mod/session2/guest.cpp`,
`tests/optd_guest_inbound_tests.cpp` (nuevo) y `CMakeLists.txt` (test `optd_guest_inbound`).

**Lo que ya era correcto y se mantiene:**

- `guest_deliver` (hilo de red del mod) solo encola.
- `guest_pump` (`0x35BAD4`, NetworkThread) es el único que llama a `OnRecvPacket`.
- `guest_send` devuelve el búfer a Q3 en el mismo hilo.

**Lo nuevo:**

1. **Cola de entrada `gin::InboundQueue`**:
   - FIFO con dos cotas: 64 MB, igual que antes, y **1 048 576 paquetes**, que es nuevo.
   - Cada paquete lleva la hora en que entró.
   - Si está llena: `end_remote("too many packets of the host waiting")`, igual que antes.
   - `end_remote` y la decisión «remota» la vacían bajo su mutex.
2. **Como mucho 4096 paquetes por vuelta del NetworkThread**, en orden, sin el mutex tomado mientras corre el código
   del juego. Lo que no cabe espera a la vuelta siguiente (≤ 16 ms).
   - En juego normal no se alcanza: una carga de zona son ~3500 paquetes/s, unos 60 por vuelta.
   - Solo actúa tras un parón del NetworkThread, para que una vuelta no retenga los envíos ni las liberaciones del
     cliente.
   - **Es el único cambio de comportamiento posible**, y solo con atascos de más de 4096 paquetes. Antes se entregaba
     todo en la misma vuelta.
3. **Comprobación de hilo (`gin::ThreadOwner`)**, que mide y nunca rechaza:
   - El primer hilo que gira en `0x35BAD4` queda como «dueño».
   - Una línea `INF` única: `Session2 guest: the client NetworkThread is thread T (mid-hook exe+0x35BAD4 …): only it
     hands the host's packets to ClientNet::OnRecvPacket … the mod's network thread only queues them`.
   - Si `Send` (`0x14380B0`) o la bomba corren en otro hilo, sale un `WRN` único y se suman al contador.
4. **Medidas** en la línea de estado del invitado (`guest_describe`, cada minuto y en la línea `Session2` de la red):
   `in queue: waiting N (K KB, peak P) full F cleared C | handed by the NetworkThread T… (turns K, batches B, batch
   max M, capped X, wait mean A ms max W ms) | mod network thread T… only queues | Send calls S | Send/pump off the
   NetworkThread 0`.
5. El invitado comprueba los 39 bytes de `0x35BAD4`, en vez de 14, antes de enganchar.
6. Se quitó el aviso C4100 de `try_refresh` (`net` sin usar).

**No cambia:**

- la decisión local o remota;
- la época;
- la despedida (`guest_goodbye_logic.h`);
- el refresco del título;
- el orden;
- qué hilo hace cada llamada al juego.

## 6. Build y tests [V]

`.\scripts\build.ps1` (RelWithDebInfo): compila sin avisos en los archivos tocados, **22/22 tests OK**. Incluye el
nuevo `optd_guest_inbound` (0,04 s), que comprueba:

- orden y `take` por tandas;
- las dos cotas, el paquete mayor que la cota y `clear`;
- las medidas (tandas, tope, espera, reloj hacia atrás);
- el dueño del hilo, con dos hilos reales;
- una carga de zona simulada: 42 000 paquetes a 3,5 por ms y una vuelta cada 16 ms → nunca topado, en orden,
  espera ≤ 16 ms;
- un parón simulado de 5 s: 15 000 paquetes → 4 vueltas (4096 × 3 + 2712), en orden, nada perdido.

No prueba los ganchos, el ABI ni los hilos reales del juego.

## 7. Cómo verificarlo

### 7.1 Un PC: `docs/research/ini/cdmp.p8-guest-local.ini`

Configuración: `guest=1`, `guest_mode=observe`, `[net] enabled=0` y todos los diagnósticos del invitado.

**Qué sí ejercita:**

- La instalación con los 39 bytes de `0x35BAD4`:
  - si fallaran: `the client's network functions are not the expected ones` → P8 apagado (fallo seguro);
  - si van bien: `Session2 guest (P8): hooked NwVirtualSyncSession::Send … observe`.
- **La comprobación de hilo**:
  - Cuando el cliente conecta con su servidor local, el NetworkThread empieza a girar y sale una vez
    `the client NetworkThread is thread T…`.
  - En modo observe, todos los paquetes locales pasan por `guest_send`. En la línea `Session2 guest (minute N)` deben
    verse `turns` creciendo (unas 60/s o más: cada paquete local despierta el bucle), `Send calls` creciendo y **`Send/pump off the NetworkThread 0`**.
  - Esa es la prueba en vivo de que `0x14380B0` corre solo en el NetworkThread.
  - Ningún `WRN … NOT the client NetworkThread`.
- Las guardas y sondas del invitado (`guest_layer_guard`, `guest_update_guard`, S3, `load_diag`, `s1_diag`,
  `guest_stage_diag`), que no cambiaron.
- Opcional: añadir `guest_slot_refresh_test=1` para ejercitar `0x1870C20` en el NetworkThread
  (`title slots refreshed locally … DIAGNOSTIC`), y comprobar que el login local posterior no dice «busy».

**Qué no puede ejercitar:** sin red y en modo observe no hay conexión remota. Por tanto:

- no hay `guest_deliver`;
- la cola de entrada queda a 0 (`waiting 0`, `batches 0`, `mod network thread T0`);
- no hay entrega a `OnRecvPacket`, ni push a Q3, ni respuesta local a la despedida, ni tope por vuelta.

Esas rutas solo las cubren el test offline y la prueba con Shadow.

### 7.2 Con Shadow (anfitrión + invitado real, `guest_mode=remote`)

1. Repetir la sesión 6B: el invitado entra al mundo del anfitrión, se mueve unos minutos y cambia de zona al menos
   una vez.
2. En el log del invitado:
   - `the client NetworkThread is thread T…`, y que `T` **no** sea el hilo de `[T…] Net:`;
   - en las líneas de estado:
     - `handed by the NetworkThread T…` con el mismo `T`;
     - `batches` y `delivered` crecen;
     - `wait mean` de pocos ms y `max` del orden de una vuelta (≤ 16–50 ms) fuera de parones;
     - `capped 0` en juego normal; algún `capped` solo tras un parón largo, sin `BROKEN`;
     - `full 0`;
     - `Send/pump off the NetworkThread 0`.
3. Pausa (ESC) del invitado durante 10–60 s con el anfitrión moviéndose:
   - `waiting` sube durante la pausa y vuelve a 0 al salir;
   - si no hubo parón del NetworkThread, `capped` sigue a 0;
   - ningún `connection to the host's server stops`.
4. Salida con Alt+F4 y vuelta al título. La despedida sigue igual que en `d9c542e`.

Criterio de éxito: ningún cierre, `deliver_errors 0`, `Send/pump off the NetworkThread 0` y `full 0`.

## Correcciones tras la revisión de la noche (2026-10-10)

Detalle completo en `p8-revision-noche.md` §6. La regla «`NotifyAppear` solo en hilos nativos» de las retenciones (caso 3 de §4) va tras
`[session2] notify_thread_rule` (0 por defecto = hilos de antes de la regla). Hay un `WRN` cuando una liberación
debida espera más de 2 s a un hilo que pueda llamar a `NotifyAppear`.
