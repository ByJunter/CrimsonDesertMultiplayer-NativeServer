# S25: volver a entrar al mundo del anfitrión en el mismo arranque (diseño de la reentrada)

Investigación de solo lectura (2026-10-12, noche). No se tocó código, no se hizo ningún commit, no se instaló
nada y no se abrió el juego. Build 1.0.0.2976 (PE `0x6AB28F00`): toda RVA de este documento vale solo para ella.
Etiquetas: **[V]** visto en código, log, captura o Ghidra; **[E]** deducción; **[H]** hipótesis sin medir.
Los números de línea son los del árbol del 12-10 noche; otra sesión estaba editando `session2.cpp`, `mod.cpp` y
`user2_funnels.cpp` a la vez, así que pueden haberse movido: se cita siempre la función.

Fuentes:
- Invitado (Shadow): `cdmp_20261012-062315.log` y `cdmp_packets_20261012-062330.*` (la salida al título de las
  06:25 y el «Continuar» en local), más los otros logs del paso 2 de S25 para las decisiones de conexión.
- Anfitrión: `C:\Emulador CD\backups\p8-sesion25-host\cdmp_20261012-071438.log`.
- Reloj: anfitrión ≈ invitado + 1 h − 1,0 s (`invisibles.md`).
- Código: `guest.cpp`, los módulos `guest_*` y P8 del invitado, `session2.cpp`/`server.cpp`/`guest_lifecycle.*`
  del anfitrión, `common/troc_link.*`, `common/client_session.*`, `tools/peer_sim/`.
- Ghidra (servidor de `D:\RE\CrimsonDesert_2976`): `0x8CAFF0` `NwVirtualSyncSession::OnConnect`, `0x1437F80`.

## Resumen

1. **Hoy hay dos cerrojos, no uno.** El invitado se niega a una segunda conexión remota (`g_had_remote`), pero
   aunque no se negara, el anfitrión **no lo dejaría entrar**: tras la barrera C4 pasa a READY y desde ese momento
   no manda ningún «ready» (estado 1) y solo arranca la época siguiente con una conexión de **transporte** más
   nueva (`ClientSession::epoch()`, un `Hello` nuevo). En el mismo arranque del juego del invitado su
   `ClientSession` y su `TrocLink` siguen siendo los mismos, así que esa conexión nueva no llega nunca. En S25 el
   invitado vio el último «ready» 13,5 s antes de su conexión nueva: aunque se quitara `g_had_remote`, `decide()`
   habría dicho «el último ready es demasiado viejo».
2. **El juego no es el problema.** El cliente hace una conexión nueva limpia al volver al título (época nueva en
   `[session+0xB0]`, primer paquete 2319, objeto de sesión persistente), y el logout nativo del anfitrión (2003/2002
   reales) desmonta en el cliente el mundo del anfitrión: actores, X del personaje del anfitrión, clima en modo
   visor. La carga local de las 06:25 terminó bien (`not stuck`). [V]
3. **Lo que queda viejo es del mod**, y lo peligroso son las tablas indexadas por **clave de actor**: el
   personaje del anfitrión es `0xA0100001` en cada conexión remota (y también el personaje propio del invitado en
   local). Una tabla con un puntero de la conexión anterior bajo esa clave apuntaría a memoria liberada en la
   siguiente. §1 lista módulo por módulo qué se reinicia ya con la época del cliente y qué no. Tres restos
   rompen cosas aunque no haya punteros: la red de seguridad de la despedida queda muerta en la segunda conexión
   (`g_goodbye_answered` nunca vuelve a false), «hay campo» se lee de un contador del arranque (`out_3093`), y el
   2910 final del guardado propio solo sale una vez por arranque (`GuestHold::final_done_`).
4. **Diseño propuesto (§3): una conversación nueva dentro del mismo enlace**, no un transporte nuevo.
   - Dos estados de control nuevos: **13 `REJOIN`** (invitado → anfitrión) y **14 `EPOCH`** (anfitrión →
     invitado), y la marca de capacidades **0x08**.
   - El anfitrión, en READY, arranca la época siguiente sobre el mismo enlace cuando le llega un `REJOIN`
     (`GuestLifecycle::start_epoch_inband`). El orden del canal fiable separa las dos conversaciones igual que lo
     hacía el `Hello` nuevo.
   - El invitado pide la reentrada en cuanto el anfitrión contesta su despedida (2002), invalida el «ready» viejo,
     y su conexión siguiente va a Remote si el anfitrión anunció la época nueva.
   - Cada módulo del invitado se reinicia en su propio hilo al ver una época de cliente nueva (el patrón que ya usan
     varios). Se añaden los reinicios que faltan.
   - **Sin cambio de `kVersion`**: el formato de `protocol.cpp` no cambia; los estados nuevos van dentro de las
     tramas Troc y se negocian con la marca 0x08, como los estados 3-12.
   - Clave `[session2] rejoin = 0 | probe | 1` en los dos PC (por defecto 0).
5. **El guardado propio no necesita una segunda subida, y no debe haberla.** El anfitrión guarda el progreso del
   invitado en su ranura 204 (el 2910 final, «gen3» en S25), que sigue LIVE; un segundo login sin oferta nueva
   carga esa ranura. Volver a subir la ranura local del invitado **perdería** lo jugado en la primera sesión (la
   generación devuelta solo se aplica con el juego cerrado).
6. **Verificación:** el lado del anfitrión se prueba en un PC con `cdmp_peer` ampliado (`--rejoin N`: varias
   entradas sobre la misma `ClientSession`). El lado del invitado (el cliente real que vuelve a entrar) necesita
   Shadow. Las piezas puras van con pruebas unitarias.

## 0. Lo que pasó a las 06:25 (S25, sesión 06:23 del invitado)

| Invitado | Anfitrión | Qué | Fuente |
|---|---|---|---|
| 06:25:07.190 | — | El cliente manda su 2001; el mod lo retiene detrás del 2910 final (`guest_own_save=1`) | log invitado l. 2179-2181 [V] |
| 06:25:07.378 | 07:25:07.007 | `SAVE_RESULT gen3`; el anfitrión: `the guest STORED gen3`, `C4: logout begin epoch=2 user=0x90100748 trigger=bye`, `MountSync … cleared`, `WorldAnchor [clear]` | l. 2184 / l. 16139-16151 [V] |
| 06:25:08.083 | — | `86 held packet(s) released` (el 2001 sale) | l. 2188 [V] |
| — | 07:25:07.258 | La barrera pasa las cuatro comprobaciones (a sesión, b SAM: 103 claves, c campo `users 0x1 userActors 0x2`, d colas) | l. 16157-16160 [V] |
| 06:25:08.198 | — | `in LogoutUserToServerAck 2003` y `LogoutSessionToServerAck 2002 … index 4 vs own 4 -> equal`: **respuesta nativa del anfitrión** | l. 2189-2190 [V] |
| 06:25:08.3 | — | El cliente desmonta el mundo del anfitrión: `stage teardown … X GONE`, `G5 actor … NOT in this client`, `GuestReloadWait [trip 3 begins]` | l. 2206-2274 [V] |
| ≈06:25:08.8 | 07:25:07.769 | `C4: logout complete epoch=2` y **`reconnect epoch=3 ready (a NEW guest connection starts it)`** | l. 16166-16167 [V] |
| 06:25:10.9 | — | `WeatherSync … X back to its own weather … a gate closed` | l. 2374 [V] |
| 06:25:14 / :19 | — | Línea periódica: `connection REMOTE … host ready no` | l. 2407, 2472 [V] |
| **06:25:21.541** | — | `MountSync … cleared`; `ERR … the client reconnected; restart the game…`; **`connection (epoch 0x3) -> local: this game run already had a remote connection … first packet 2319 | tunnel up | last host ready 13469 ms ago`** | l. 2474-2476 [V] |
| 06:25:21.568 | — | `UiPause: P8 session over`; `WeatherSync … gate closed (this game does not play on the host now)` | l. 2477-2478 [V] |
| 06:25:37.4 | — | `GuestDiag: local user key 0x90100000 … was 0x90100748` (el usuario local) | l. 2516 [V] |
| 06:25:39 → 06:25:49 | — | 3093 local, 2549 local `not stuck`: partida propia, en solitario | `invisibles.md` §1 [V] |

Lo que dicen estos tiempos:
- **El anfitrión estuvo listo 12,8 s antes** de que llegara la conexión nueva del invitado. El hueco natural
  (volver al título y reconectar) basta; no hace falta retener el 2319. [V]
- **El último «ready» lo vio el invitado a las ≈06:25:08,07**, justo cuando el anfitrión empezó el logout: es lo
  que dice el código (`tunnel_tick_reconnect`: estado 1 solo en ACTIVE). [V]
- La época del cliente pasa de 0x1 a 0x3: sale de un contador global del proceso (`0x1437F80`, `lock inc`),
  único en el arranque, no consecutivo por sesión. Solo sirve como «ha cambiado». [V Ghidra]

## 1. Estado por conexión en el invitado

Hilos: **NT** = NetworkThread del cliente (`guest_send`, `guest_pump`); **MNT** = hilo de red del mod
(`guest_deliver`, `guest_set_tunnel_up`, `guest_take_outgoing`); **DT** = hilo de diagnóstico; **JP** = hilo
principal del juego (ganchos). Hoy **no existe ningún aviso de «conexión nueva» en el invitado**: el único reinicio
que se empuja es `mount_sync_reset` (desde `end_remote` y desde el cambio de época tras una rotura); el resto de
módulos, si se reinicia, lo hace comparando `guest_connection_epoch()` o leyendo `guest_remote_live()` en vivo.
[V, inventario del subagente del invitado, comprobado a mano en `guest.cpp`, `mount_sync.cpp`,
`guest_save_wire.h`, `remote_interp.cpp`]

### 1.1 Lo que hoy bloquea o rompe una segunda conexión remota

| # | Estado | Dónde | Efecto en una segunda conexión |
|---|---|---|---|
| B1 | `g_had_remote` (nunca vuelve a false) | `guest.cpp` `decide()`, `guest_save_guest_host_seen(!g_had_remote)`, `guest_local_after_remote()`, los sellos B1 | La conexión va a local. Es el cerrojo conocido |
| B2 | `g_host_ready_ms` sin «suelo» | `guest.cpp` `host_ready()`/`decide()` | Un «ready» de la conversación vieja (< 3 s) daría por listo a un anfitrión que está en RETIRING. Hoy no pasa porque el cliente tarda 13 s, pero no está garantizado |
| B3 | `g_goodbye_answered` (solo se pone a true) | `guest.cpp` `note_in`, `answer_goodbye`; `guest_goodbye_logic.h` `decide()` sale con `Wait` si `answered` | En la segunda conexión **la red de seguridad de la despedida queda muerta**: ni respuesta local en el título ni `EndProcess` con campo. Una segunda salida con el anfitrión caído dejaría el cliente colgado (el cuelgue del 09-10) |
| B4 | `g_goodbye_ms` (solo se pone a 0 en `AnswerLocally`) | `guest.cpp` `guest_pump` | Tras una respuesta nativa del anfitrión conserva la hora vieja |
| B5 | `g_n.out_3093` usado como bandera | `guest.cpp`: «hay campo» en `guest_pump` (`goodbye::State.field`) y `sent_3093` en `guest_save_guest_on_send` | En la segunda conexión vale «sí» antes de su 3093: una despedida desde el título se trataría como «con campo» (terminar el proceso en vez de contestar en local) |
| B6 | `GuestHold::final_done_` (por arranque) | `guest_save_wire.h` `GuestHold::on_send` | **La segunda despedida no manda el 2910 final**: lo jugado en la segunda conversación no se guarda al salir |
| B7 | `g_status` / `g_status_seen` (nunca se borran) | `guest_save_guest.cpp` (`final_ok`) | El 2910 final de la segunda conversación se decidiría con el estado de la primera |
| B8 | `g_may_upload = !g_had_remote` y `g_prep` leído una vez al arrancar | `guest_save_guest.cpp` | Ninguna oferta en la segunda: **es lo correcto** (§3.6), pero por accidente; hay que hacerlo explícito |
| B9 | `mount_sync` `g_local_key` (nunca se borra) | `mount_sync.cpp` (`publishable`, el evento Status, `seat_tick`) | Mientras se juega en local tras una remota, la clave propia sigue siendo la del anfitrión (`0xA0100749` en S25); en la segunda remota se reescribe con el 2756 nuevo |
| B10 | Anfitrión: solo arranca la época siguiente con un transporte nuevo | `GuestLifecycle::may_start`, `start_epoch` | Ver §2. Aunque el invitado aceptara, el anfitrión no |

### 1.2 `guest.cpp`

| Estado | Reinicio hoy | Para la reentrada | Hilo |
|---|---|---|---|
| `g_epoch`, `g_mode` | En cada época nueva (`guest_send`) | Igual | NT |
| `g_broken` | false al decidir Remote; true en `end_remote` | Igual | todos |
| `g_q3` | Se valida en cada `decide()` Remote | Igual | NT |
| `g_tunnel_up`, `g_host_dead` | Cada vuelta y cada control | Igual | MNT |
| `g_host_ready_ms` | Cada estado 1 | Añadir `g_ready_floor_ms` (B2) | MNT |
| `g_to_host` | No se vacía al decidir; se vacía en cada `take_all` | Vaciar al empezar una conversación nueva (no debería tener nada: tras el 2001 no se empuja) | NT/MNT |
| `g_mount_drain` (guarda un evento de montura rechazado) | **No** | Descartar lo pendiente al empezar conversación | NT/MNT |
| `g_from_host`, `g_in_stats` | `clear_from_host` al decidir Remote y en `end_remote` | Igual | MNT/NT |
| `g_ms_held*` | Por época (`ms_release`) | Igual | MNT |
| `g_goodbye_ms`, `g_goodbye_answered`, `g_last_in_ms` | **No** (B3, B4) | A cero al empezar conversación (en NT, en `guest_send` al decidir Remote) | NT/MNT |
| `g_goodbye_ending`, `g_local_ending` | `exchange(true)`: el proceso termina | Igual | NT/DT |
| `g_ll_*` (B1 «carga local atascada») | Se rearman con sellos nuevos; dependen de `g_had_remote` | Usar «hubo una conversación remota que terminó» en vez de `g_had_remote` | NT/DT |
| `g_refresh` (ranuras del título) | `arm(epoch)` una vez por época | Igual; `disabled_`/`g_refresh_code` por arranque tras un fallo: correcto | NT |
| `g_n` (contadores) | **No** | Contadores: por arranque (para el resumen). Añadir un `g_conv` por conversación con `out_3093`, `out_2001`, `in_2002` (B5) | NT/MNT |
| `g_rean`, `g_rean_epoch` | `g_rean.reset()` por época | Igual | MNT |

### 1.3 Los demás módulos del invitado

| Módulo | Estado por conexión | ¿Se reinicia hoy? | Para la reentrada |
|---|---|---|---|
| **guest_save_guest** | `g_hold` (`upload_`, `final_done_`, `retries_`), `g_held`/`g_held_goodbye`, `g_status*`, `g_tunnel_up`/`g_uploaded`/`g_want_upload`, `g_out`/`g_up_out`/`g_down` | Por **época de transporte** (`guest_save_guest_tunnel`) y `on_link_lost` al romperse; `final_done_`, `retries_`, `g_status*` nunca | `guest_save_guest_conversation_begin()`: `final_done_ = false`, retención a Off, `g_status_seen = false`, conservar `upload_ = Acked` (no hay subida nueva). Bajo `g_mu`, llamado desde NT al decidir Remote |
| **mount_sync** | `g_remote_key` (se borra en `reset`), `g_local_key` (**nunca**), `g_table`/`g_table_epoch` (por época), `g_out` (no se vacía), `g_peer_no_publish_ms`; travas por fallo `g_seat_off`, `g_apply_off`, `g_gate_off`, `g_gate_degraded` | Parcial (`mount_sync_reset`) | En `mount_sync_reset`: borrar también `g_local_key` y la cola `g_out`. Las cuatro travas son de **fallo** («no más llamadas en esta sesión»): dejarlas por arranque (conservador) y decirlo en el HUD/log |
| **ride_filter** | `g_local_key`, `g_remote_key` | Sí: `sync_connection` al cambiar de época; el anfitrión reenvía el estado 4 cada segundo | Nada |
| **weather_sync** (invitado) | `g_gate`/`g_gate_epoch`, `g_owned`, `g_latch` | Sí: la puerta se recalcula con `guest_remote_live()`; al cerrarse devuelve X a su clima (06:25:10 [V]) | Nada. `FaultLatch` (10 fallos) es por arranque: correcto |
| **remote_interp** | `g_table` (por mc+clave, se recicla a los 10 s), `g_engage` (8 s), `g_own_char` (cada 2756 remoto), `g_pairs` (60 s) | Por tiempo, sin época | Recomendado: vaciar `g_table`/`g_engage`/`g_pairs` y `g_own_char = 0` al empezar conversación (el `mc` es una dirección del juego: una dirección reutilizada con la misma clave heredaría el `d_fix` viejo) |
| **remote_motion** | `g_tracks` | Se reconstruye cada segundo recorriendo el mundo | Nada |
| **remote_rope** | `g_own_run`/`g_own_check_done`; travas `g_*_faulted` | No (por arranque) | Nada obligatorio (medida de la propia cuerda y travas por fallo) |
| **attach_follow** (registro) | `g_registry` | `attach_follow_on_epoch` solo lo llama el anfitrión; 15 s de caducidad | Llamar `attach_follow_on_epoch()` también al empezar conversación en el invitado |
| **guest_seq_route** | `g_own_key`/`g_own_epoch` (por época); `g_xreg` (punteros X, validados con `x_gone`); `g_sched_ledger` (anillo, sin época); `g_seq_res`, listas de vigilancia | Parcial | `g_sched_ledger`: vaciar al empezar conversación (son X de la conversación vieja bajo la clave `0xA0100001`, que se reutiliza). **`g_xreg` NO** (corregido al implementar): es la guarda del desmontaje de etapas; una etapa de un X viejo puede desmontarse más tarde y `x_gone()` la sigue reconociendo porque compara con el X vivo del personaje. El resto son topes de log |
| **guest_seq_play** | `g_table` (XTable por x+clave+**época**) | Sí (`StaleEpoch` limpia la fila) | Nada; `g_disabled`/`g_live_latched` son travas por fallo |
| **guest_sched_load** | Retenciones con dueño (época, gestor, contenedor); `g_eng` (libro de claves, intentos, tope 1024 cargas por arranque) | Retenciones sí (`DROPPED (another connection …)`, 06:25:14 [V]); el motor no | Nada obligatorio; el tope de 1024 cargas por **arranque** puede agotarse con varias conversaciones: medir en Shadow (`loads_total` en el resumen) |
| **guest_sched_bind** | `g_queue` (puntero X + clave dueña por NPC, 2 min) | No por época; cada pase revalida vtable y dueño | Vaciar `g_queue` al empezar conversación (mismo motivo que `g_xreg`) |
| **guest_x_veto** | `g_m`/`g_m_epoch`/`g_m_key` | Sí (por época) | Nada |
| **guest_x_interaction** | `g_x` (4 punteros X) | Nunca, pero `check_x` revalida en cada uso | Vaciar al empezar conversación (barato, quita la dependencia de la revalidación) |
| **guest_s3** | `g_guard_tab` (por clave, nunca se borra, tope 1024 claves por arranque) | No | Vaciar al empezar conversación (las claves 0xA/0xB se reutilizan entre servidores) |
| **guest_reload_wait** | `g_ages` (por clave, solo texto de log), sellos de viaje | Por viaje | Nada (solo log) |
| **guest_seq_restore, load_trip, clock_probe, p8_probes, guest_scene/stage/bodyanim/actor_diag** | Mapas por clave o sello de época | No | Solo diagnóstico: nada (el texto puede mezclar conversaciones; añadir la conversación a sus líneas) |
| **ui_pause, fast_travel** | `g_live` | Se recalculan con `guest_remote_live()` | Nada |
| **slot_refresh** | Planner por época | Sí | Nada |
| **TrocLink** del invitado (`mod.cpp`) | `epoch_`/`broken_`, capacidades por transporte | `reset()` si roto y no Remote | Nada en V1 (mismo transporte) |

### 1.4 Las funciones de reinicio que hay que añadir

Un único punto: `guest_conversation_begin(uint64_t client_epoch)` en `guest.cpp`, llamado en NT cuando `decide()`
devuelve Remote con `g_had_remote` ya puesto (la segunda conversación o posteriores). Sube un atómico
`g_conversation` (1 la primera remota, 2 la segunda…) y hace en el acto lo que es de NT; el resto lo hace cada
módulo en su hilo al ver el número nuevo (`guest_conversation_id()`), como ya hacen con la época:

| Función nueva | Hilo | Qué hace |
|---|---|---|
| (en `guest.cpp`) | NT | `g_goodbye_ms = 0`, `g_goodbye_answered = false`, `g_last_in_ms = 0`, `g_conv = {}` (B3-B5), descartar `g_mount_drain` pendiente |
| `guest_save_guest_conversation_begin()` | NT (bajo `g_mu`) | B6, B7; `upload_` se queda `Acked`; log `own save: conversation N plays the host's slot 204 (no new upload)` |
| `mount_sync_reset` ampliado | el que llama (ya es seguro entre hilos con `g_lock`) | `g_local_key = 0`, vaciar `g_out` |
| `remote_interp_on_conversation(n)` | sus hilos de gancho, perezoso | Vaciar `g_table`, `g_engage`, `g_pairs`; `g_own_char = 0` |
| `guest_seq_route_on_conversation(n)` | JP/DT, perezoso | Vaciar `g_sched_ledger` (no `g_xreg`: ver §1.3) |
| `guest_sched_bind_on_conversation(n)` | JP, perezoso | Vaciar `g_queue` |
| `guest_x_interaction_on_conversation(n)` | JP, perezoso | Vaciar `g_x` |
| `guest_s3_on_conversation(n)` | su hilo, perezoso | Vaciar `g_guard_tab` |
| `attach_follow_on_epoch()` (ya existe) | MNT, perezoso en `attach_registry_on_in` | Vaciar `g_registry` |

Las travas por fallo (`g_seat_off`, `g_apply_off`, `g_gate_off`, `g_gate_degraded`, `g_disabled` de varios
módulos, `FaultLatch`) **no** se reinician: son la reacción a un fallo medido y deben valer para todo el arranque.

### 1.5 El cliente del juego

- **Objeto de sesión:** `ClientNet+0x10` (`NwVirtualSyncSession`) es persistente; en cada conexión
  `OnConnect` (`0x8CAFF0`) escribe `[session+0xB0] = FUN_141437f80()`, un contador global con `lock inc`
  (`0x1437F80`), `+0xB8 = 0x7FFF…` y `+0xA8 = 0`. Por eso 0x1 → 0x3: la época solo sirve para «cambió», y nunca
  se repite en un arranque. [V Ghidra]
- **Q3** (`ClientNet+0x28`): persistente; `decide()` la vuelve a validar en cada decisión Remote. [V]
- **Restos del mundo del anfitrión:** el 2003/2002 nativo del anfitrión desmonta todo en el cliente. Tras la
  despedida de las 06:25: X del personaje del anfitrión destruido (`stage teardown … X GONE`, `G3 X … 0x0`), los
  actores del anfitrión fuera (`G5 actor … NOT in this client`), el clima devuelto (`back to its own weather`), y la
  carga siguiente (local) terminó en 10 s. [V]
- **Clave de usuario del cliente:** `0x90100748` (la que dio el anfitrión) → `0x90100000` (la del servidor local)
  al cargar; en una reentrada será otra del anfitrión. Los módulos que la usan (`GuestDiag`, `GuestS3`) la releen
  sola cada segundo. [V]
- **Lo que el código del juego no reinicia y el mod no ve:** no medido. La carga local de las 06:25 es la mejor
  señal de que el cliente queda limpio; la reentrada solo cambia el servidor al que habla.

## 2. El anfitrión: despedida, segundo login y lo que falta

### 2.1 Lo que hace hoy (con `reconnect=1`, activo en S25: `C4: reconnect on`)

Máquina `GuestLifecycle` (`guest_lifecycle.h`): ACTIVE → RETIRING → CLEAN → READY → ACTIVE (época + 1), o
QUARANTINED. [V]

- **Despedida** (`offer()` ve el 2001 → `begin_logout(Goodbye)`, `session2.cpp`):
  - captura las claves del usuario 2 (`capture_user2` → `DepartedSnap`), vacía `g_early`,
    `mount_sync_reset("user 2 logs out")`;
  - `server_logout_user2` → `hand_logout`: cierra un movimiento abierto (2549 en la ventana A), en `LinkLost` un
    2910 final, y entrega el 2001 nativo;
  - desde ahí `offer()` tira todo paquete del invitado (`g_after_goodbye`), `inject()` solo admite 2001/2549/2910
    finales y el espejo se para.
- **RETIRING:** lo que escribe el servidor sigue yendo al mismo enlace (por eso llegan 2003/2002); la barrera
  (sesión, SAM, campo, colas) corre cada 250 ms y debe pasar dos veces con 500 ms de separación antes de 10 s.
- **CLEAN → READY:** `make_ready(link.session_epoch())` guarda `ready_link_` y solo admite una conexión **de
  transporte** más nueva (`may_start`: `link_epoch > ready_link_`; `start_epoch`: `min_link_ = ready_link_ + 1`).
- **READY → ACTIVE:** `tunnel_tick_reconnect` (al ver otra `session_epoch`) o `tunnel_on_packet` (primer paquete de
  esa conexión) llaman `start_epoch(le)`:
  - `server_begin_epoch(next)`: `g_want_epoch`, descarta búferes de otras épocas, reinicia logout, FinalSave,
    `g_outgoing`, `g_user2_player`, espejo, fase de movimiento, Nak, y llama
    `horse_follow_on_epoch`, `attach_follow_on_epoch`, `combat_follow_on_epoch`, `identity_fix_new_epoch`,
    `observer_milestone_on_epoch`, `observer_hold_acks_clear(Epoch)`, `observer_scene_copy_on_epoch`,
    `observer_seq_copy_on_epoch`;
  - además limpia snap, base del campo, `g_user_seen`, `g_early`, `mount_sync_reset("a new guest connection")` y
    `g_link->reset()`;
  - la sesión 2 nueva la construye el hilo del servidor en `on_server_recv` con el primer paquete que recibe; la
    vieja no se libera nunca (máximo 32 por arranque).
- **Latido de control** (cada 1 s mientras `link.connected()`): estado 1 solo si ACTIVE, el enlace es «el nuestro»
  (`le >= min_link` y ligado a él o sin ligar) y `server_accepts_guest()`; estado 2 solo en QUARANTINED; **en
  RETIRING, CLEAN y READY no manda nada**. El estado 4 (clave del personaje del anfitrión) sale cada segundo en
  todos los estados.
- **Módulos con reinicio por época ya hecho:** world_anchor (`on_epoch`, y «leave» en el 2001), clock_push
  (`g_watch.reset()` al cambiar de época y sin usuario 2), mount_sync (los dos `reset` y el cambio de época del
  enlace), identity_fix (`new_epoch`), combat/horse/attach_follow, observer_* (milestone, hold_acks, scene_copy,
  seq_copy), child_hold (entradas marcadas con `server_epoch()`), reannounce (`g_rean_host.reset()`). Sin estado
  por usuario 2 o leído en vivo: clock_gate, user2_funnels (`refresh_keys` cada 100 ms), ride_filter, ui_pause,
  fast_travel, field_share, remote_rope/catch_seq (solo topes de líneas de log). [V, informe del subagente del
  anfitrión]
- **Guardado propio** (`guest_save_tunnel.cpp`/`guest_save_host.cpp`):
  - los estados 6-11 se atienden **antes** de la admisión C4, en cualquier conexión;
  - una conexión de transporte nueva → `on_new_connection()` (tira la subida en curso y el último estado);
  - una segunda `SAVE_OFFER` en el mismo arranque **funciona** (`guest_save_incoming_begin` → staged → se mueve a
    la ranura 204 en la siguiente carga de lobby);
  - **sin oferta nueva, el login siguiente del usuario 2 se remapea a la ranura 204 que sigue LIVE**, la que
    escribió el 2910 final de la sesión anterior (gen3 en S25). No se comprueba la identidad del invitado al
    cargar (`guest_save_host.cpp`, decisión en `guest_save_logic.h`), y no hace falta: es el mismo invitado;
  - desactivado o envenenado son «para todo el arranque».

### 2.2 Lo que falta en el anfitrión

| # | Falta | Efecto hoy | Arreglo (§3) |
|---|---|---|---|
| H1 | Una forma de arrancar la época siguiente **sin un `Hello` nuevo** | El invitado, en el mismo arranque, no puede volver a entrar nunca | `REJOIN` (13) + `start_epoch_inband` |
| H2 | Un aviso explícito de «época N empieza» al invitado | El invitado solo puede deducirlo del «ready», que es de 1 byte y sin época | `EPOCH` (14) con la época C4 |
| H3 | READY no dice nada al invitado (ni ready ni dead) | El invitado no distingue «el anfitrión limpia» de «el anfitrión no está» | `EPOCH` (14) con el estado C4 una vez por segundo para un invitado con 0x08 |
| H4 | `g_user2_player`, `g_user2_userkey`, `puppet_guard`/`horse_diag` `also_key` siguen con los valores del usuario 2 anterior durante RETIRING/READY | Nada mientras no haya usuario 2; se sobrescriben al llegar el 2646/2756 nuevos [V] | Ninguno obligatorio; anotarlo |
| H5 | clock_push `g_last_ack_actor` no se reinicia por época | Solo es el respaldo de clave de T2 | Reiniciarlo en el cambio de época (trivial) |
| H6 | weather_sync anfitrión: `sent_seq` estático por arranque | El invitado ve saltar la secuencia; es solo diagnóstico | Ninguno |

Nada de lo demás del anfitrión necesita cambiar: el C4 está probado con cuatro épocas en una partida (C4r,
2026-10-09) y en S25 con un invitado real hasta `reconnect epoch=3 ready`.

## 3. Diseño

### 3.1 Elección: conversación nueva en el mismo enlace (V1) frente a transporte nuevo (V2)

`p8-preparacion.md` G5 ya dejaba las dos puertas: «un mensaje de control "nueva conversación, época X"» o «forzar
que el invitado reconecte su `ClientSession`».

| | V1: conversación nueva dentro del enlace | V2: reconectar el transporte |
|---|---|---|
| Qué separa las conversaciones | El orden del canal fiable: todo lo que el invitado manda después de su `REJOIN`, y todo lo que el anfitrión manda después de su `EPOCH`, es de la época nueva | El `Hello` nuevo |
| Cambios en código común | Ninguno (`TrocLink::reset()` ya conserva las capacidades «para una época C4 en la misma conexión») | `ClientSession::disconnect()` tendría que **cambiar el nonce**: hoy un `Hello` con el mismo nonce estando conectado es «un duplicado» y no sube la época; si el `Bye` (no fiable) se pierde, el anfitrión no vería nunca la conexión nueva [V `client_session.cpp`] |
| Efectos laterales | Ninguno fuera de session2 | Se corta el canal del mod (posiciones, HUD), Steam renegocia, se vuelven a anunciar capacidades, el guardado propio ve «conexión nueva» y querría volver a subir la ranura local (pérdida de progreso, §3.6) |
| Riesgo de carrera | El `REJOIN` puede llegar en RETIRING: se ignora y el invitado lo repite cada segundo | El `Hello` puede llegar antes del READY: es la trampa `ready_link_` (`p8-preparacion.md`), la conexión queda por debajo y no entra nunca |
| Prueba en un PC | `cdmp_peer --rejoin` (una `ClientSession`, varias entradas) | Igual que C4r |

**Se elige V1.** La conversación es un concepto de session2, no del transporte, y V1 no toca nada compartido con
el cooperativo. El requisito de `salida.md` §4.3 («época y nonce de transporte nuevos») buscaba que no se herede
ningún paquete viejo; en V1 lo garantiza el orden del canal fiable (§3.4) y, en el invitado, la regla de que la
conexión del cliente solo se decide remota con un «ready» posterior al `EPOCH` nuevo.

### 3.2 Mensajes (opcode 1, slot −1, `u8 estado | …`, LE)

| Estado | Nombre | Dirección | Carga | Cuándo |
|---|---|---|---|---|
| 13 | `REJOIN` | invitado → anfitrión | `13 | u8 versión 1 | u8 modo (1 = real, 2 = probe) | u32 conversación del invitado (1 = la primera remota del arranque, 2 = la segunda…) | u32 última época C4 que el invitado vio en un `EPOCH` (0 = ninguna)` = 11 B | Cada 1 s, como mucho 30 veces, desde que el anfitrión contestó la despedida del invitado (§3.3 paso 3) hasta ver un `EPOCH` con época mayor |
| 14 | `EPOCH` | anfitrión → invitado | `14 | u8 versión 1 | u8 estado C4 (0 ACTIVE, 1 RETIRING, 2 CLEAN, 3 READY, 4 QUARANTINED) | u8 motivo (0 latido, 1 empieza por REJOIN, 2 empieza por conexión nueva, 3 probe: empezaría, 4 REJOIN ignorado) | u32 época C4 | u32 conversación del invitado que lo pidió (0 = ninguna)` = 12 B | Cada 1 s junto al latido, y **en el acto** tras `start_epoch*`, solo si el invitado anunció 0x08 en esta conexión |
| caps 0x08 | `kCapsRejoin` | invitado → anfitrión (opcode 2) | bit en el byte de capacidades | Con `rejoin = probe` o `1` |

- Los estados 0-12 están ocupados (0 = «no mandar nada» en el latido); 13-255 libres [V `troc_frame.h`,
  informe del anfitrión].
- **`kVersion` (protocol.h, 8) no cambia**: `protocol.cpp` no serializa nada nuevo; estas tramas viajan dentro de
  `RelKind::TrocFrame` como los estados 3-12, que tampoco lo subieron (`guest_save_wire.h:27`,
  `weather_sync_logic.h:244`).
- **Compatibilidad:** un anfitrión viejo no conoce el 13: `tunnel_on_packet` lo cuenta como «otro frame de opcode 1»
  y `offer()` lo tira (o, en READY, `admit()` lo descarta); el invitado nunca ve un 14 y cae a local con el aviso
  de hoy. Un invitado viejo no anuncia 0x08: el anfitrión no manda 14 y no recibe 13. Nada cambia para él.
- Las tramas se reconocen **solo en su forma exacta** (longitud fija, versión 1), como `ran::is_reannounce_frame`
  y `msl::is_frame`.

### 3.3 La secuencia en los dos PC

Partimos de la primera conversación remota en marcha (época C4 *E* en el anfitrión, conversación 1 en el
invitado, época de cliente *c1*).

1. **Primera conexión (sin cambios).** Al empezar la época *E* (o al primer latido tras anunciar 0x08) el
   anfitrión manda `EPOCH(ACTIVE, latido, E, 0)`. El invitado anota `host_rejoin_capable = true` y `last_epoch = E`.
2. **El invitado se despide** (2001; con `guest_own_save=1`, detrás del 2910 final). Sin cambios: RETIRING, barrera,
   2003/2002 del anfitrión, el cliente desmonta el mundo del anfitrión.
3. **El 2002 del anfitrión llega al invitado** (`note_in`): el mod pasa la conversación a `Over(clean)` y
   **invalida el «ready»** (`g_host_ready_ms = 0`, y un `g_ready_floor_ms` = ahora: solo cuenta un «ready» posterior).
   Con `rejoin=1` y `host_rejoin_capable`, empieza a mandar `REJOIN(conv = 2, last_epoch = E)` cada segundo
   (hilo de red del mod, `guest_take_outgoing`).
   - Sin 0x08 en el anfitrión (anfitrión viejo) o `rejoin=0`: no se manda nada y todo sigue como hoy.
4. **El anfitrión** recibe el `REJOIN` en `tunnel_on_packet`, **antes** de la admisión C4 (como los estados 6-11):
   - RETIRING o CLEAN: lo cuenta, contesta `EPOCH(estado, REJOIN ignorado, E, 2)` y espera (el invitado repite);
   - READY: `GuestLifecycle::start_epoch_inband(link_epoch)` + el mismo `start_epoch()` de `session2.cpp` que hoy
     (con `server_begin_epoch(E+1)`, los `*_on_epoch`, `mount_sync_reset`, `g_link->reset()`), y **después** del
     `reset` del enlace empuja `EPOCH(ACTIVE, empieza por REJOIN, E+1, 2)`. Log:
     `Session2 C4: epoch=E+1 begins IN-BAND on the same guest connection (link L, guest conversation 2)`;
   - ACTIVE sin conversación (ya empezó por un `REJOIN` anterior y el cliente aún no ha hablado): contesta el mismo
     `EPOCH(ACTIVE, empieza por REJOIN, E+1, 2)` otra vez (idempotente);
   - ACTIVE con conversación de la época E+1 o QUARANTINED: lo ignora y contesta su estado.
   - Con `rejoin=probe` en el anfitrión: no arranca nada; contesta `EPOCH(READY, probe: empezaría, E, 2)` y escribe
     la línea de lo que haría.
5. **El latido vuelve:** en ACTIVE con el enlace «nuestro» (en V1, `min_link_ = ready_link_`, el mismo enlace sirve)
   y `server_accepts_guest()` (cierto mientras la sesión 2 de la época nueva está pendiente de construir), el estado
   1 sale cada segundo, **siempre detrás del 14 en el canal**.
6. **El invitado ve el `EPOCH(E+1)`** con su conversación: `rejoin_state = Granted(E+1)`, deja de mandar
   `REJOIN`, y el siguiente «ready» (posterior a `g_ready_floor_ms`) vuelve a ser fresco. HUD: «El anfitrión te
   espera: pulsa Continuar».
7. **El cliente vuelve a conectar** (al llegar al título; en S25, 13 s después del 2002) con una época nueva *c2*:
   - `guest_send` ve el cambio de época: `end_remote(...)` como hoy para lo que quedara de *c1*, y
     **`guest_conversation_begin(c2)`** (§3.5) en vez de quedarse en local;
   - `decide()`: con `g_had_remote`, ahora es Remote **solo si** `rejoin=1`, `rejoin_state == Granted`, primer
     paquete 2319, túnel arriba, anfitrión no muerto y un «ready» fresco **posterior** al `EPOCH` concedido. Si falta
     algo, Local con el motivo exacto en el log y en el HUD (el aviso de hoy, `alone_after_remote`, con el motivo);
   - todo lo que entre del anfitrión antes de esta decisión no se encola (hoy ya es así: `deliver_body` solo encola
     en Remote y la decisión llama `clear_from_host()`).
8. **El primer Req del cliente** (2319) entra en el anfitrión con la época E+1 → `server_accepts_guest()` lo pone en
   la cola nativa → el hilo del servidor construye la sesión 2 de E+1 (el arreglo del C4r) → login normal (2490,
   título, el usuario pulsa Continuar → 3093). Con `guest_own_save=1`, el 3093 se remapea a la ranura 204 LIVE (§3.6).
9. **Segunda despedida o salida:** igual que la primera. El invitado puede repetir el ciclo hasta el tope del
   anfitrión (32 sesiones 2 por arranque).

### 3.4 Por qué no se cuela nada de la conversación anterior

- **Invitado → anfitrión:** tras su 2001 el invitado no manda paquetes del cliente al anfitrión (la conexión vieja
  queda `Over`; `guest_send` solo empuja con `g_mode == Remote` y sin `g_broken`). Todo lo que el anfitrión reciba
  tras el `REJOIN` es de la época nueva. Lo que llegue antes, en RETIRING/READY, lo descarta `offer()`
  (`g_after_goodbye`) o `admit()` como hoy.
- **Anfitrión → invitado:** en RETIRING el anfitrión solo empuja lo de la sesión vieja (los Acks de logout); en
  CLEAN/READY descarta lo que escriba el servidor (`g_out_discarded`). `start_epoch` hace `link.reset()` (vacía lo
  pendiente de enviar) y **después** empuja el 14 y el 1. El invitado no encola nada hasta decidir Remote, y decide
  solo con un «ready» posterior al 14: todo lo anterior queda fuera.
- **Búferes nativos:** `server_begin_epoch` descarta los búferes etiquetados con otra época y `take_frame` tira lo
  que escriban sesiones de épocas anteriores (`server.cpp`). No cambia.

### 3.5 Cambios en el invitado

**a. Máquina pura `rejoin_logic.h` (nueva, probada en `tests/`):**
- Estados: `Off`, `FirstConversation`, `Over(clean | broken)`, `Asking(n, intentos)`, `Granted(epoch)`,
  `Refused(motivo)`.
- Entradas: `on_epoch_frame(estado, motivo, epoch, conv)`, `on_goodbye_answered(by_host)`, `on_end_remote(why)`,
  `on_client_connection(first_op, ready_fresh_after_grant)`, `tick(now)` (reintentos, tope de 30 s).
- Salidas: «manda REJOIN ahora», «decisión Remote/Local con motivo», texto del HUD.
- Cuándo **no** se pide la reentrada (sin guardas nuevas sobre valores sin medir: son estados ya registrados en
  vivo):
  - la conversación terminó con la despedida contestada **por el anfitrión** (2002 real) o por pérdida del enlace
    seguida de `EPOCH(READY)` del anfitrión (logout `link_lost` limpio);
  - **no** si se contestó en local (`answer_goodbye`, sin campo) sin `EPOCH(READY)`, si el anfitrión mandó el
    estado 2 o `EPOCH(QUARANTINED)`, o si `end_without_host` terminó el proceso;
  - **no** si el cliente ya cargó una partida local tras la remota (un 3093 en una conexión local: el B1 de hoy lo
    marca). Para volver, cerrar el juego, como hoy.

**b. `guest.cpp`:**
- `decide()`: la rama `had` consulta la máquina (`Granted` y «ready» posterior al permiso); el texto de la línea
  dice qué faltó.
- `note_in()`: al 2002 del anfitrión, `rejoin.on_goodbye_answered(true)` y `g_ready_floor_ms = now`.
- `host_ready()`: además de fresco, `g_host_ready_ms > g_ready_floor_ms`.
- `guest_deliver()`: el estado 14 se consume (nunca llega al juego) y va a la máquina.
- `guest_take_outgoing()`: los `REJOIN` que toquen.
- `guest_send()`, cambio de época tras una remota: `end_remote()` como hoy y luego
  `guest_conversation_begin(epoch)` si la decisión es Remote.
- `mod.cpp`: la marca 0x08 en `set_announce_flags`, como las de guardado y clima.
- HUD (`hud.cpp`, el bloque `alone_after_remote`): con `rejoin=1`, el motivo concreto («el anfitrión no admite
  volver a entrar», «el anfitrión aún está limpiando», «cargaste una partida local») y, mientras está `Granted`,
  «El anfitrión te espera: pulsa Continuar».

**c. El reinicio por conexión (la parte grande):** ver §1.4. Regla: **cada módulo se reinicia en el hilo que
posee su estado**, al ver que `guest_connection_epoch()` (o un contador nuevo `guest_conversation_id()`, que solo
sube cuando una conexión se decide Remote) es distinto del que guardó. Es el patrón que ya usan `guest_seq_play`
(`XTable` con época), `reannounce_tick` (`g_rean_epoch`), `ms_release` (`g_ms_held_epoch`) y `pump_title_refresh`.
Nada de reiniciar desde otro hilo ni dentro de un gancho del juego.

### 3.6 Guardado propio en la reentrada

- **No subir otra vez la ranura local.** El progreso de la primera sesión está en la ranura 204 del anfitrión (el
  2910 final) y en `guest_progress\…\incoming\genN` del invitado, pero **no** en su ranura local hasta aplicar la
  generación con el juego cerrado. Subir la ranura local en la reentrada devolvería al invitado a como entró la
  primera vez.
- Hoy ya es así por casualidad: `guest_save_guest_host_seen(!g_had_remote)` no sube nada tras una remota. Con V1,
  `guest_save_guest_tunnel` tampoco ve conexión nueva (mismo `session_epoch`), así que `g_hold` sigue con la subida
  `Acked` de la primera conversación y el 3093 de la segunda no se retiene.
- Lo que hay que reiniciar por conversación en `guest_save_guest` (ver §1.4): **`GuestHold::final_done_`** (hoy
  por arranque: sin reiniciarlo, la segunda despedida no manda el 2910 final y **se pierde lo jugado en la segunda
  conversación**), la retención `Final`/`Login`, la cola de retenidos, `g_held_goodbye`, el estado del anfitrión visto (`g_status_seen`, para que el 2910 final de la
  segunda despedida dependa del `SAVE_STATUS` de la época nueva) y el `sent_3093` que hoy viene del contador **del
  arranque** (`g_n.out_3093 != 0`, `guest.cpp`): debe ser por conversación.
- En el anfitrión no cambia nada: el 3093 de la segunda conversación se remapea a la ranura 204 LIVE (gen3 en S25),
  y el 2910 final de la segunda despedida la vuelve a escribir.
- Si el invitado elige otra ranura en el título (otro 3093), `GuestHold` ya prepara la oferta de esa ranura: es el
  comportamiento de hoy y sí sube algo; anotarlo en las instrucciones («Continuar», no «Cargar»).

### 3.7 Fallos y avisos

| Fallo | Qué pasa | Aviso |
|---|---|---|
| Anfitrión viejo o `rejoin=0` en el anfitrión | Nunca llega un 14; la conexión siguiente es local (como hoy) | HUD de hoy + «el anfitrión no admite volver a entrar: cierra y abre el juego» |
| El `REJOIN` llega en RETIRING/CLEAN | El anfitrión contesta `EPOCH(estado, ignorado)`; el invitado repite cada 1 s | Nada (o «el anfitrión está terminando tu salida…») |
| La barrera tarda más que la vuelta del cliente al título | El 2319 llega sin `Granted`: Local | HUD con el motivo; el siguiente intento exige cerrar el juego (igual que hoy). Medir antes de añadir una espera (S25: 12,8 s de margen) |
| Cuarentena del anfitrión | `EPOCH(QUARANTINED)` o estado 2 | «El anfitrión no admite más entradas en esta partida» |
| `Granted` y el cliente no vuelve (cierra el juego) | Anfitrión ACTIVE sin conversación; al perder el enlace 15 s, `kill_tunnel` → época sin usuario → limpia sin 2001 → READY | Nada |
| Fallo en la segunda conversación (paquete rechazado, cola llena, fallo en `OnRecvPacket`) | `end_remote` como hoy | El de hoy |
| El cliente cargó en local tras la remota | No se pide reentrada | El de hoy |

Ninguna de estas ramas rechaza un valor sin medir: todas se apoyan en estados que ya se registran en vivo (2002,
2001, `reconnect … ready`, la decisión de conexión).

### 3.8 Clave de modo `[session2] rejoin = 0 | probe | 1` (por defecto 0)

- **0:** nada cambia (ni la marca 0x08, ni 13, ni 14). Es el comportamiento de hoy.
- **probe** (en los dos PC):
  - invitado: anuncia 0x08, manda `REJOIN` con modo 2 y registra lo que haría en la conexión siguiente
    (`would be REMOTE: granted epoch E+1, ready N ms after the grant`), pero la decisión sigue siendo la de hoy
    (local tras una remota);
  - anfitrión: contesta `EPOCH(…, probe: empezaría)` y escribe si habría arrancado la época, sin arrancarla.
  - Sirve para medir en una sesión con Shadow el hueco despedida → READY → reconexión del cliente sin arriesgar
    nada.
- **1:** el diseño completo. Requiere `reconnect=1` en el anfitrión (si no, se registra y queda en 0).

### 3.9 Riesgos

| Riesgo | Probabilidad | Mitigación |
|---|---|---|
| Un módulo del invitado conserva un puntero de la conversación anterior bajo una clave reutilizada (`0xA0100001`) → uso tras liberar | Media si se olvida uno | El inventario de §1.4 y una prueba que recorra todos los `*_describe` tras la reentrada; en probe, cada módulo registra qué habría reiniciado |
| El cliente real arrastra algo que `cdmp_peer` no tiene (estado del título, gestores de horarios, ranuras) | Media | La carga local de las 06:25 (mismo cliente, misma salida) terminó bien; la reentrada solo cambia el servidor. Primera prueba con Shadow en `probe` y luego en `1` |
| La sesión 9 dejó una carga local atascada en `0x0C` tras una sesión remota (causa [H]: tablas del mod con el X viejo) | Baja hoy (S25 no se atascó) | El reinicio por conversación quita justo esos restos; el B1 (`guest_local_after_remote=stuck`) sigue de red |
| El `REJOIN` empieza una época y el cliente se va a local por otra razón | Baja | El anfitrión queda ACTIVE sin conversación; un `REJOIN` posterior recibe el mismo `EPOCH`; si el enlace se pierde, C4 limpia sin 2001 |
| Dos conversaciones del mismo invitado con identidades distintas (C2, `characterNo F002`) | Baja | `identity_fix_new_epoch` ya reinicia; el 3093 se reescribe por paquete |
| Tope de 32 sesiones 2 por arranque del anfitrión | Muy baja | Cuarentena limpia; aviso |
| La pausa por ESC del anfitrión durante la reentrada | Baja | El servidor del anfitrión corre igual; `ui_pause` es por estado vivo |

Reglas del proyecto: sin ganchos nuevos en el juego (todo es lógica del mod en puntos que ya existen:
`guest_send`, `guest_pump`, `guest_deliver`, `tunnel_on_packet`, `tunnel_tick_reconnect`); nada en `DllMain`;
nada identificado entre PC por dirección (la conversación y la época C4 son números del mod); ningún rechazo nuevo
sobre un valor que no se haya medido en vivo.

## 4. Cómo verificarlo

### 4.1 En un PC (el anfitrión real contra `cdmp_peer`)

`cdmp_peer` hoy hace **un login por proceso**: cada ronda del C4r es un proceso nuevo, con nonce y `Hello` nuevos
(`tools/peer_sim/replay.cpp`, `opcion-d-pruebas.md` C4r). Para V1 hay que ampliarlo:

- **`--rejoin N`**: en la misma `ClientSession` y el mismo `TrocLink`, tras `script done` y el 2002, mandar
  `REJOIN(conv = k+1)` cada segundo hasta ver `EPOCH(E+1)` y luego un estado 1, y repetir el guion. Anunciar 0x08.
- Procedimiento (como C4r, `cdmp.c4r.ini` + `rejoin=1`, ranura 108, sin viajar):
  1. `cdmp_peer --mode replay --script login_e3lc00bye_112627.troc --var SAVE_SLOT=108 --rejoin 3 --record …`.
  2. Pasa si, en cada ronda: `epoch=E begins IN-BAND on the same guest connection`, las líneas de siempre de C4
     (`user 2 of epoch E exists`, `logout begin`, barrera a-d `pass`, `logout complete`, `reconnect … ready`), el
     segundo Kliff aparece y desaparece, y ningún `guest packets of another connection dropped` tras el 14.
  3. Con `--guest-save` y `--save-before-bye`: la segunda ronda no manda `SAVE_OFFER` y su 3093 se remapea a la
     204 (línea `the guest's slot is ready`), y el 2910 final de la segunda despedida la reescribe.
  4. Con el anfitrión en `rejoin=probe`: ninguna época nueva; solo la línea de «empezaría».
- **Lógica pura** (sin juego): `rejoin_logic.h` (todas las transiciones y la decisión), `GuestLifecycle::
  start_epoch_inband` en `tests/optd_c4_tests.cpp`, el formato exacto de 13 y 14 (y que un 13/14 de otra longitud no
  se toma).
- **El invitado en un PC, solo la fontanería:** el juego con `guest_mode=observe` en solitario, salir al título y
  Continuar: cada conexión nueva pasa por `guest_conversation_begin` en modo probe y cada módulo escribe qué
  reiniciaría (deberían ser ceros: en observe no hay datos del anfitrión). Comprueba que el reinicio no rompe una
  partida local. No prueba la reentrada.

### 4.2 Lo que necesita Shadow

La reentrada del **cliente real** del invitado: no caben dos copias del juego en un PC y `cdmp_peer` no hace de
servidor para un cliente.

1. Sesión A, los dos en `rejoin=probe`: el invitado entra, juega 1-2 min, sale al título y pulsa Continuar.
   Medir: la hora del 2002, la de `reconnect … ready`, la del `EPOCH` y la de la conexión nueva del cliente
   (`would be REMOTE …`). Se juega en local, como hoy.
2. Sesión B, los dos en `rejoin=1`: lo mismo. Pasa si:
   - el invitado: `connection (epoch 0x…) -> REMOTE: re-entry granted (host epoch 3)`, el 3093 con F002, en el mundo
     del anfitrión, HUD «Otro jugador: HP …» con `same_world`;
   - el anfitrión: `epoch=3 begins IN-BAND`, `user 2 of epoch 3 exists`, el personaje del invitado aparece;
   - **el progreso del invitado es el de la salida** (la ranura 204: mismo HP, inventario, posición que al salir);
   - los dos se ven y se mueven (las comprobaciones de `invisibles.md` §2: 2005 del otro, MoveActorAck, piezas con
     padre), y la segunda salida es limpia;
   - en el invitado, las líneas de reinicio de cada módulo y ningún puntero de la conversación 1 en los resúmenes.
3. Repetir una tercera entrada en el mismo arranque.

## 5. Lo que no está medido

- Que el cliente real acepte un segundo login **en otro servidor** sin restos de la primera sesión remota. Lo más
  parecido medido es la carga local de las 06:25, que funcionó.
- El hueco despedida → reconexión del cliente en otros casos (salir desde el mapa, desde un viaje, con el
  anfitrión en pausa). S25 da un solo dato: 13,3 s.
- Si alguna tabla del juego del lado del cliente guarda la clave de usuario del anfitrión (`0x90100748` en S25)
  más allá del logout. En S25, tras el logout, el cliente pasó a `0x90100000` local sin problemas.

## Implementado (2026-10-12/13, noche)

Lo que se construyó a partir de §3, sin abrir el juego ni instalar nada. `.\scripts\build.ps1` en verde (58 pruebas,
nueva `optd_rejoin` y `optd_c4` ampliada).

### Qué hay

- **Clave** `[session2] rejoin = 0 | probe | 1` (por defecto 0), en los dos PC. En el anfitrión necesita
  `reconnect=1` (si no, `WRN … rejoin='1' needs reconnect=1 … it stays off`); en el invitado necesita
  `guest_mode=remote`.
- **Protocolo:** estados de control 13 `REJOIN` y 14 `EPOCH`, marca de capacidades 0x08 (`troc_frame.h`). Sin cambio
  de `kVersion`. Formatos exactos en `src/mod/session2/rejoin_logic.h` (lógica pura: tramas, respuesta del anfitrión,
  máquina del invitado y su decisión).
- **Anfitrión** (`session2.cpp`): el `REJOIN` se atiende en `tunnel_on_packet` antes de la admisión C4; en READY,
  `GuestLifecycle::start_epoch_inband` + el `start_epoch()` de siempre (`server_begin_epoch`, los `*_on_epoch`,
  `mount_sync_reset`, `link.reset()`) y después el `EPOCH`. Latido: `EPOCH` (estado C4) cada segundo, delante del
  «ready», solo a un invitado que anunció 0x08. Huecos H4/H5 de §2.2: `server_begin_epoch` borra la clave de usuario
  del usuario 2 anterior y las guardas `puppet_guard`/`horse_diag`; clock_push borra `g_last_ack_actor` al cambiar de
  época.
- **Invitado** (`guest.cpp`): `decide()`, tras una conversación remota, solo va a Remote con la reentrada concedida,
  un «ready» posterior al permiso, primer paquete 2319 y el túnel arriba; si no, local con el motivo exacto en el log
  y en el HUD. B2: un «ready» de la conversación vieja nunca cuenta (contador de «ready» con suelo).
  `guest_conversation_begin()` (NetworkThread, al decidir Remote) reinicia B3-B9 y sube `guest_conversation_id()`.
- **Reinicios perezosos por conversación**, cada uno en el hilo dueño de su estado:

  | Módulo | Qué se reinicia | Hilo |
  |---|---|---|
  | guest.cpp | `g_goodbye_ms`, `g_goodbye_answered`, `g_last_in_ms`, «hay campo» por conversación (`g_conv.out_3093`), eventos de montura pendientes (`OutboundDrain::discard`) | NetworkThread |
  | guest_save_guest | `GuestHold::new_conversation()` (2910 final una vez **por conversación**), retenidos, `g_status_seen`; sin subida nueva | NetworkThread (bajo `g_mu`) |
  | mount_sync | `mount_sync_reset` borra además la clave propia del invitado (`g_local_key`) | cualquiera (atómicos) |
  | remote_interp | nueva generación de la tabla de slots (sin vaciarla: lock-free), `EngageTable`/`RidePairs::forget()`, `g_own_char` | hilo de red del mod, con el primer paquete del anfitrión |
  | guest_seq_route | `SchedLedger::clear()` (no `g_xreg`, ver §1.3) | hilo principal (`res_thread_ok`) |
  | guest_sched_bind | la cola de enlaces de NPC | hilo principal, bajo `QueueLock` |
  | guest_x_interaction | `XSlots::clear()` | el primero que lo use (atómico) |
  | guest_s3 | nueva generación de `GuardTable` (el tope de claves por arranque se mantiene) | su bucle |
  | attach_follow | el registro del invitado | hilo de red del mod |

  Las travas por fallo (`g_seat_off`, `g_apply_off`, `g_gate_off`, `g_gate_degraded`, los `g_disabled`, `FaultLatch`)
  siguen siendo por arranque.
- **HUD:** con `rejoin` activo, en lugar de «cierra el juego y ábrelo»: «Volviendo al mundo del anfitrión: esperando a
  que termine tu salida…», «El anfitrión te espera: pulsa Continuar para volver a su mundo.», o el motivo
  («Cargaste tu partida local…», «El anfitrión no admite volver a entrar…», «…no respondió…», «…no admite más
  entradas…»), seguido de `(motivo: …)` con la razón exacta de la última decisión.
- **`cdmp_peer --rejoin N [--rejoin-probe]`:** tras el guion, N rondas más en la misma `ClientSession` y el mismo
  túnel. Anuncia 0x08, manda `REJOIN` cada segundo, espera el `EPOCH` y un «ready» posterior y vuelve a jugar el guion.
  La subida del guardado propio (`--guest-save`) solo va en la primera ronda, como en el mod. Código de salida 4 si
  una ronda no se concede en 60 s.
- **Inis:** `rejoin=probe` en `cdmp.p8-s27-host-paso1.ini` y `cdmp.p8-s26-shadow.ini`; `rejoin=1` en
  `cdmp.p8-s25-host-activo.ini` (que tiene `reconnect=1`), `cdmp.p8-s26-shadow-paso2.ini` y `cdmp.c4r.ini` (para la
  prueba en un PC; sin `--rejoin`, `cdmp_peer` no anuncia 0x08 y C4r va como siempre).

### Líneas de log

Anfitrión:
- `Session2 rejoin ON (1): in C4's READY, the guest's REJOIN (control state 13) begins the next epoch on the same connection …`
- Mientras limpia (hasta 64 líneas): `Session2 rejoin [1]: the guest asks for conversation 2 (real; it last played epoch 1) | C4 RETIRING epoch 1 on link L -> not now: answer the state`; después: `… | C4 READY epoch 1 on link L -> start the next epoch in-band`.
- `Session2 C4: epoch=2 begins IN-BAND on the same guest connection (link L, guest conversation 2); a new session 2 is built for it`.
- Después, las de siempre: `user 2 of epoch 2 exists; host field baseline …`, `logout begin epoch=2 …`, la barrera, `reconnect epoch=3 ready`.
- En la línea periódica `Session2: …`: `| rejoin [1]: asked N (bad 0), started in-band K (this epoch for conversation C), answered again …, probe …, not now …, off … | EPOCH out …`.
- Con `rejoin=probe`: `… -> probe: would begin epoch 2 in-band (nothing started)`.

Invitado:
- `Session2 guest: rejoin ON (1): after a remote conversation (back to the title), this client asks the host …`
- La primera vez: `Session2 guest rejoin [1]: host EPOCH: C4 ACTIVE epoch 1 (heartbeat, for conversation 0) | …` (el anfitrión admite la reentrada).
- `Session2 guest rejoin [1]: remote conversation 1 is over (the host answered the client's goodbye 2002) | asking the host for conversation 2 (REJOIN every 1000 ms, at most 30)`.
- `Session2 guest rejoin [1]: REJOIN #1 for conversation 2 (last host epoch played 1)`.
- `Session2 guest rejoin [1]: host EPOCH: C4 ACTIVE epoch 2 (started in-band for the re-entry, for conversation 2) | asking the host -> granted: the client's next connection (back at the title) goes to the host once a 'ready' follows (conversation 2)`.
- `Session2 guest: connection (epoch 0x…) -> REMOTE: the host is ready | … | had remote yes | guest_mode remote | re-entry granted (host epoch 2 for conversation 2, N ms ago)`.
- `Session2 guest: remote conversation 2 begins (client connection epoch 0x…, host epoch 2): re-entry in the same game run | …`.
- Con `guest_own_save`: `Session2 guest save: remote conversation 2 (re-entry in the same game run): NO new upload …`.
- Las de los módulos que estén activos: `MountSync: the observer's transactions cleared (a new remote conversation (re-entry))`, `RemoteInterp: remote conversation 2 (re-entry): …`, `GuestSchedRoute: remote conversation 2 (re-entry): …`, `GuestSchedBind: …`, `GuestS3: …`.
- Si no se concede: `connection (epoch 0x…) -> local: <motivo>`, con uno de estos motivos:
  - `re-entry asked N time(s) for conversation 2; the host has not begun a new epoch (its C4 state …)`;
  - `re-entry granted (host epoch 2) but no 'ready' from the host after it yet`;
  - `… the host never said it can take the guest back (no EPOCH frame …)`;
  - `no re-entry in this run: <motivo>`.
- Con `rejoin=probe`: `-> local: rejoin=probe: the host would begin epoch 2 for conversation 2 (probe: nothing started, this connection stays local)`.
- En la línea periódica `Session2 guest: …`: `| rejoin [1]: <fase> | conversation N (asking …, asks …) | host C4 … epoch … (EPOCH frames …), granted epoch … | REJOIN sent … | ready count … (floor …)`.

### Prueba en un PC (anfitrión real + `cdmp_peer`)

Como C4r: ini `cdmp.c4r.ini` (ya con `rejoin=1`), ranura 108, quieto en un sitio tranquilo y **sin viajar**. Un
arranque del juego y **un solo** `cdmp_peer`:

```
.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00bye_112627.troc --var SAVE_SLOT=108 --rejoin 2 --record D:\cdmp-grabaciones\rejoin-1.bin
```

El guion **tiene que terminar con una despedida** (`logout_session`). Sin ella, el anfitrión sigue en ACTIVE y no
concede la reentrada: el peer acaba con `Rejoin: no next round …` y código 4.

Pasa si:
- en `cdmp_peer`:
  - `Rejoin: round 1 of 3 plays (conversation 1)` y `script done` con 2003 y 2002;
  - `Rejoin: round 1 over; asking the host for conversation 2 (real)`;
  - `Rejoin: host EPOCH: C4 ACTIVE epoch 2 (started in-band for the re-entry, conversation 2) | asking the host -> granted`;
  - `Rejoin: re-entry granted … -> round 2`;
  - lo mismo para la ronda 3, y al final `Rejoin: 3 of 3 round(s) played`, con código de salida 0;
- en el log del anfitrión, por cada ronda E:
  - `epoch=E begins IN-BAND on the same guest connection` (E = 2 y 3);
  - las líneas de C4r: `user 2 of epoch E exists; host field baseline …` (no `NOT TAKEN`), `logout begin epoch=E …
    trigger=bye`, las cuatro comprobaciones de la barrera en `pass`, `logout complete epoch=E` y `reconnect epoch=E+1
    ready`;
- no aparece ningún `guest packets of another connection dropped` después de un `begins IN-BAND`;
- el segundo Kliff aparece y desaparece en cada ronda, y tu juego sigue normal.

Variantes:
- **Probe:** el mismo comando con `--rejoin 1 --rejoin-probe`. El anfitrión escribe `probe: would begin epoch 2
  in-band (nothing started)` y no empieza ninguna época nueva. `cdmp_peer` acaba con `Rejoin: rejoin=probe: the host
  would begin …; stopping (probe)` y código 0.
- **Guardado propio:** `--guest-save <copia del laboratorio> --save-before-bye`, con un ini de anfitrión que tenga
  `guest_own_save=1`.
  - La primera ronda sube la partida (`SAVE_OFFER`); las siguientes **no**.
  - En cada ronda la carga se remapea a la ranura 204 del anfitrión (`LoadSlot … REMAP 204`).
  - Cada despedida trae su `SAVE_RESULT` (una generación más por ronda).

Para y mándame el log y la grabación si:
- sale `Session2 C4: QUARANTINED`;
- una ronda no se concede (`Rejoin: no next round`);
- aparece `Nak 1013` en una ronda posterior a la primera;
- tu campo cambia de dirección o de generación;
- el juego se cierra.

### Lo que necesita Shadow

La reentrada del cliente real. Antes, copiar a Shadow el `cdmp.asi` nuevo y su ini por la carpeta compartida.

1. **Sesión A (probe).** Anfitrión con `cdmp.p8-s27-host-paso1.ini` (`rejoin=probe`) e invitado con
   `cdmp.p8-s26-shadow.ini` (`rejoin=probe`). El invitado entra, juega 1-2 min, sale al título (ESC → salir) y pulsa
   Continuar.
   - Pasa si:
     - en el invitado salen `remote conversation 1 is over (the host answered the client's goodbye 2002) | asking …`
       y `host EPOCH … probe: would start`, y la conexión siguiente dice `-> local: rejoin=probe: the host would
       begin epoch 2 …`;
     - en el anfitrión sale `probe: would begin epoch 2 in-band (nothing started)`;
     - el invitado juega en solitario, como hoy.
   - Apuntar los tiempos: 2002 → `reconnect … ready` → `EPOCH` → conexión nueva del cliente.
2. **Sesión B (1).** Anfitrión con `cdmp.p8-s25-host-activo.ini` (`rejoin=1`, `reconnect=1`) e invitado con
   `cdmp.p8-s26-shadow-paso2.ini` (`rejoin=1`). Lo mismo que en A, y después una tercera entrada en el mismo arranque.
   - Pasa si:
     - en el invitado: `-> REMOTE: … re-entry granted (host epoch 2 …)`, `remote conversation 2 begins`, el 3093 con
       F002, y el HUD con «Otro jugador: HP …» y sin el aviso de solitario;
     - en el anfitrión: `epoch=2 begins IN-BAND` y `user 2 of epoch 2 exists`, y el personaje del invitado aparece;
     - el progreso del invitado es el de su salida (ranura 204: mismo HP, inventario y posición);
     - los dos se ven y se mueven (las comprobaciones de `invisibles.md` §2);
     - la segunda salida es limpia: el invitado recibe el `SAVE_RESULT` de la segunda despedida (el 2910 final ahora
       sale una vez por conversación).
   - Para y manda los logs si:
     - el invitado se queda en la pantalla de carga;
     - se cierra el juego del invitado o el del anfitrión;
     - sale `QUARANTINED`;
     - el invitado juega en local con `(motivo: …)` en el HUD;
     - el HUD dice que se ven, pero no se ven.

### Riesgos

- **El cliente real en un segundo login a otro servidor no se ha probado nunca.** `cdmp_peer` no reproduce el estado
  que arrastra el cliente. La señal más cercana es la carga local de las 06:25 de S25, que terminó bien.
- **Que algún módulo guarde estado de la conversación anterior y el inventario no lo haya visto.**
  - Los que guardan punteros bajo claves reutilizadas están cubiertos.
  - `g_xreg` se conserva a propósito: es la guarda del desmontaje de etapas.
  - Los módulos de solo diagnóstico pueden mezclar conversaciones en sus líneas.
- **El tope de 1024 cargas de horarios por arranque** (`guest_sched_load`) puede agotarse con varias conversaciones.
  Hay que vigilar `loads_total` en el resumen.
- **La carrera entre la barrera y el cliente.** Si el cliente reconecta antes de que el anfitrión conceda la reentrada
  (en S25 hubo 12,8 s de margen), esa conexión va a local con el motivo, y para volver hay que cerrar el juego. No se
  ha añadido ninguna espera.
- **Elegir otra ranura en el título** (Cargar en vez de Continuar) prepara la oferta de esa ranura, como hoy: sube una
  partida local sin el progreso de la conversación anterior. Por eso las instrucciones dicen «Continuar».
- **El tope de 32 sesiones 2 por arranque del anfitrión.** Al llegar a él hay una cuarentena limpia
  (`EPOCH(QUARANTINED)` y el aviso en el HUD).

### Resultado en un PC (2026-10-13)

Con HEAD `08f0250` instalado, anfitrión con `cdmp.c4r.ini` y ranura 108, `cdmp_peer … --rejoin 2` terminó con
`Rejoin: 3 of 3 round(s) played`. El anfitrión registró `epoch=2 begins IN-BAND …` y `epoch=3 begins IN-BAND …`, y
todas las barreras pasaron. **PASA.** Falta Shadow (el cliente real).

### Revisión independiente de `c4367f8` y arreglos

| # | Hallazgo | Arreglo | Prueba |
|---|---|---|---|
| 1 | Si la concesión llegaba **después** de decidir local la primera conexión tras la conversación, el HUD decía «El anfitrión te espera: pulsa Continuar» con la conexión local: Continuar cargaba la partida local y el anfitrión se quedaba con una época ACTIVE huérfana | Esa conexión local es terminal: `GuestRejoin::on_local_connection()` → `Refused(LocalConnection)`. Deja de pedir, una concesión tardía no cambia nada y el HUD dice «No se pudo volver… cierra el juego y ábrelo». En el anfitrión, una época empezada para una reentrada y sin usar en `kGrantUnusedMs` (60 s) se abandona (`rjl::grant_unused`; `WRN Session2 rejoin: epoch E was begun for guest conversation C and nothing of it came in 60 s: it is given up …`): logout sin usuario 2, la barrera la limpia («nothing reached») y vuelve a READY | `test_local_connection_terminal` |
| 2 | Se podía conceder una época que se seguía jugando: `played_epoch_` quedaba en 0 si no llegaba ningún `EPOCH` antes del 2319, y el latido seguía nombrando la conversación de la época | Invitado: mientras juega, `played_epoch_ = max(played_epoch_, epoch ACTIVE)`. Anfitrión: el latido nombra la conversación solo mientras nadie ha hablado (`rjl::heartbeat_conversation`) | `test_played_epoch_follows_host` |
| 3 | Elegir otra ranura en una reentrada subía la ranura local, sin el progreso de la conversación anterior: la 204 de la conversación 1 pasaba a `slot204.old` y se perdía después | En una conversación de reentrada, `GuestHold` no vuelve a ofrecer (`OnSend::slot_kept`): se mantiene la 204 viva, `WRN Session2 guest save: re-entry: the client's 3093 asks for slotN … NOT offered …` y aviso en el HUD (`Notice::ReentrySlotKept`, 12 s) | `test_guest_hold_reentry_slot` |
| 4 | Tramas de control viejas del invitado entraban en la época nueva: eventos de montura generados en local o en el título, `g_to_host` sin vaciar, y en el anfitrión una trama de control podía ligar la conversación | Con `rejoin` activo, los eventos de montura se descartan si no se juega en el anfitrión (`remote_live_for_keys`). En una conversación de reentrada, `g_to_host` se vacía salvo los `REJOIN`. En el anfitrión, con `rejoin` activo, una trama de control nunca liga la conversación: solo se acepta dentro de una ya ligada a esa conexión | revisión de código |
| 5 | El tope de 30 peticiones contaba las contestadas con «ahora no» (RETIRING/CLEAN) y las encoladas con el transporte caído | Solo el **silencio** rechaza: 30 peticiones seguidas sin ningún `EPOCH`. Con el transporte caído no se manda ni se cuenta | `test_silence_only` |
| 6 | `rejoin=0` no era idéntico | La primera conversación solo se cuenta (`rjl::reentry_conversation`): el guardado propio, el vaciado de la cola de monturas, `g_to_host` y los reinicios de despedida solo se hacen desde la conversación 2. La clave propia de mount_sync se borra en `mount_sync_forget_local_key()`, solo con `rejoin` activo. En el anfitrión, `server_begin_epoch(…, forget_user2_keys)` y el `g_last_ack_actor` de clock_push solo cambian con `rejoin` activo | `test_rejoin_off_identity` |
| 7 | El sondeo era ruidoso | En probe el HUD no cambia nunca; las respuestas «would begin» del anfitrión entran en el tope de líneas (64) | `test_rejoin_off_identity`, `test_guest_probe` |
| 8 | Una época ACTIVE empezada por una conexión de transporte nueva (sin conversación en banda) nunca se concedía | Si nadie ha hablado en ella, un `REJOIN` real la toma (`Repeat`): `… -> the free epoch E (begun on a new connection, nobody spoke yet) is now this conversation's`. Le aplica el abandono por falta de uso | `test_host_decide` |
| 9 | `cdmp_peer`: `source` nulo si el guion no se volvía a cargar en una ronda posterior | Se conserva el anterior hasta que carga el nuevo; si falla, código de salida 4 | revisión de código |

Líneas nuevas para los criterios:
- **Invitado:** `connection (epoch 0x…) -> local: … | rejoin [1]: no re-entry in this run (this connection is local)`.
  A partir de ahí, `no re-entry in this run: the client's first connection after the remote conversation went to this
  PC's own server …`.
- **Anfitrión:** `Session2 rejoin: epoch E was begun for guest conversation C and nothing of it came in 60 s: it is
  given up …`. La línea periódica añade `unused given up N`.
- **Invitado, en una reentrada en la que se elige otra ranura:** `Session2 guest save: re-entry: … NOT offered …`. El
  HUD muestra «Reentrada: se juega tu partida del anfitrión (la de tu última salida), no la ranura elegida».

Para la sesión B con Shadow, se añade un criterio de parada: el HUD dice «No se pudo volver al mundo del anfitrión…»
aunque el anfitrión estaba en READY antes de que el invitado llegara al título. Hay que mandar los dos logs, con las
horas de `reconnect … ready` y de la conexión del cliente.

## 6. S29 paso 2: la comprobación c sin baseline (2026-10-13)

**Lo que pasó [V].** En el paso 2 de la S29 la reentrada se rechazó: el HUD del invitado dijo «El anfitrión no admite
más entradas en esta partida (motivo: the host's C4 is quarantined)». Anfitrión: `…\backups\p8-sesion29-host\cdmp_20261013-130045.log`.

| Hora (anfitrión) | Línea / hecho |
|---|---|
| 13:00:51–13:09:57 | `Local player: no controlled actor (not in world)`; la sonda de cada minuto: `host character ? … field -` (el anfitrión en su pantalla de título; su usuario existe desde la 2490 de las 13:01:14, sin personaje) |
| 13:09:49.9 / 13:09:50.3 | Shadow se une; túnel conectado: C4 ACTIVE |
| 13:10:02.77 | 2490 del usuario 2 (`REMAP 204`) |
| 13:10:02.962 | `user 2 of epoch 1 exists; host field baseline NOT TAKEN` (≈12 muestras de 1 s, todas sin personaje del anfitrión) |
| 13:10:04.6 | el anfitrión carga su personaje (`LoadSlot … (3093 character)`) |
| 13:10:07.4 | sonda: campo **transitorio** `0x5B7A8D17700`, generación `0x81020000` |
| 13:10:46 | `Local player: in world`; desde 13:10:57 el campo estable es `0x5B7A8D17A10`, generación `0x81020001` |
| 13:19:26.956 | adiós del usuario 2; a 13:19:27.207 pasan a, b y d; c: `no baseline …` |
| 13:19:36.956 | `QUARANTINED: c field (no baseline …)`: no más entradas en esta ejecución |

En el adiós el campo estaba limpio (`user 2 gone`: `users 0x1 user actors 0x2`, el usuario que se fue fuera de la lista).

**La causa.** La baseline solo se tomaba con C4 ACTIVE, sin usuario 2 y con el campo del anfitrión legible por
`[user+0xD0]` → TS → campo. El invitado entró mientras el anfitrión estaba en el título: no había personaje. Tomarla
tarde no habría servido: a las 13:10:07 el campo era otro (`…17700`/`0x81020000`) que el del adiós (`…17A10`/`0x81020001`).
Y la comparación con la baseline también fallaba en falso si el anfitrión cambiaba de campo durante la sesión.

**La comprobación nueva (estructural; `evaluate_barrier`, `server_barrier_read`).** c pasa si, en el campo actual del
anfitrión:
1. es legible (`server_host_field`) y su dueño es el anfitrión (`owner == host key`);
2. la lista de usuarios, leída **una vez** (`scan_field_list`: un recuento, una copia), es legible; el usuario que se fue
   (`snap.user`, solo comparado) no está; sus entradas son tantas como `users`;
3. **ninguna entrada ajena:** cada `FieldUserEntry.user` es el usuario del anfitrión (`session 1 +0x90`,
   `server_host_user`). Es el mismo valor que la lista guarda: AddActor mete `[actor+0xA0]` del personaje, que es el
   usuario de su sesión (`field_share.cpp`, `user == user2`). Una entrada nula cuenta como ajena;
4. la suma de los actores por usuario es igual a `userActors` (el invariante del arreglo del caballo, 2026-10-09).

Una baseline del **mismo** campo añade su generación y `open` como información (`(CHANGED)` si cambian). Sin baseline,
la línea dice `no baseline (user 2 came in before the host was in its world): structural check`; con otro campo,
`the host changed field since the baseline 0x…: structural check`. La línea de entrada del usuario 2 ya no dice
`NOT TAKEN (the barrier will fail)` sino `not taken (user 2 came in before the host was in its world, S29 step 2): the
barrier's check c is structural only`.

**Por qué es seguro.** c existe para probar que el logout nativo sacó al usuario 2 del campo del anfitrión: su entrada y
sus actores. Las pruebas directas son: el que se fue no está en la lista; ninguna entrada es de otro usuario (más fuerte
que `users == baseline.users`, que pasaría si saliera el que se fue y entrara otra entrada); y el total coincide con la
suma de la lista (detecta actores del que se fue todavía contados). Lo que añadía la baseline (puntero y generación
iguales) no dice nada de la salida del usuario 2: si el anfitrión viajó, es suyo. Lo que ninguna de las dos ve (el
usuario 2 en otro campo) lo sigue cubriendo b (claves y lista de usuarios del SAM). Los fallos van en la dirección
segura: algo ilegible o ajeno → FAIL → cuarentena, como antes. En H2, la comprobación nueva habría pasado a las 13:19:27.2.

**Riesgo.** Si el servidor pusiera en el campo del anfitrión otra entrada legítima que no sea un usuario del mod, c
fallaría (cuarentena). En todos los logs de S29, sin usuario 2 el campo tiene `users 0x1`.

**No cambia:** `kControlHostReady` sigue mandándose aunque el anfitrión esté en el título. Retenerlo hasta que el
anfitrión esté en el mundo haría que `decide()` del invitado mandara su conexión a **local** (su propia partida); sería
una decisión de experiencia de uso con un aviso en el HUD («esperando a que el anfitrión entre al mundo»), no de
seguridad.

Tests: `optd_c4` (`test_field`: `scan_field_list`; `test_barrier`: sin baseline y campo limpio pasa; entrada ajena,
el que se fue en la lista, suma distinta, dueño distinto, recuento distinto y lista ilegible fallan; otro campo que el
de la baseline pasa; generación/`open` distintos pasan con `(CHANGED)`).

**Shadow:** una reentrada con el anfitrión en el título cuando Shadow se une, y otra en el orden normal. Esperado:
`logout complete epoch=…` con `c field pass (… | no baseline … structural check)` y `reconnect epoch=… ready`.
