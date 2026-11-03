# P8 · Escenas de horario del anfitrión hacia el invitado (`observer_sched_copy`)

10 de octubre de 2026, rama `opcion-d` (sobre `8b25afb`). Build del juego **1.0.0.2976**. Solo **anfitrión**. No se
lanzó el juego ni se desplegó nada. Sin ganchos nuevos: todo va por el camino de `observer_seq_copy` (captura del
cliente propio, `take_frame`, `push_outgoing_extra`).

**[V]** = comprobado (capturas de la sesión 11, código, pruebas). **[E]** = interpretación o pendiente de la prueba.

Base: `p8-impl-horarios.md` §5 (el diseño), `p8-impl-opcion-a.md` (el libro y los viajes de los 2661), el análisis
independiente de la sesión 11 (`npc-raiz-s11-20261010-work\INFORME.txt` §4 y `schedules\informe.txt`).

La otra mitad (que el invitado lleve estos paquetes al gestor del personaje del anfitrión, X, y no a M) la hace otro
agente en el invitado. Este documento solo cubre que **lleguen**.

## 0. Resumen

- **Qué faltaba [V, sesión 11, ventanas equivalentes anfitrión/invitado]:**

  | Mensaje | Fase 1 H / G | Fase 2 H / G | Envío |
  |---|---|---|---|
  | 2335 CreateNPCScheduleStageAck | 204 / 204 | 35 / 35 | ToNear |
  | 2126 DeleteNPCScheduleStageAck | 189 / 67 | 30 / 0 | ToTarget al dueño (y un sitio ToNear) |
  | 2958 ChangeNPCScheduleMiseensceneAck | 6 / 0 | 0 / 0 | ToTarget |
  | 2909 DeactivateMiseensceneScheduleEvadeObstacleAck | 4 / 0 | 0 / 0 | ToTarget |

  Antes de la entrada del invitado, 42/46 escenas vivas del anfitrión nunca le llegan; de los ids que nombran sus
  NPC faltan 17/18 exactos (401/336 actores).
- **Implementado** `[session2] observer_sched_copy = 0 | 1` (por defecto 0), encima de `observer_seq_copy`:
  1. **Libro** de las escenas de horario vivas del cliente del anfitrión, por clave de escena: el último 2335 que
     crea (tipo 1/4) **y, detrás, sus 2958/2909** (y un 2335 que no crea). Sale con su 2126, con el 2411 que la cierra
     y con el 2174 propio del anfitrión.
  2. **Instantánea** en el viaje de observador del usuario 2: cada escena del libro va byte a byte (2335 y luego sus
     2958/2909) en cuanto el usuario 2 **terminó de cargar** el campo (su 2549, o su 2144) y el actor del 2335 se le
     anunció (2005): **justo detrás del 2005 del Kliff del anfitrión, antes de los 2005 de NPC posteriores**, porque
     un NPC se enlaza a su escena una sola vez, al desempaquetar su 2005 (`p8-impl-horarios-x.md` §4.1). **No** usa
     la puerta de los 2661 (2322 / 30 s). Una escena cuyo 2335 le llegó antes de forma nativa no se repite (sus
     2958/2909 sí van); un 2335 nativo que llega **después** de nuestra copia se cuenta y se escribe.
  3. **En vivo:** un 2335 que crea y que el usuario 2 no recibió de forma nativa pasa a pendiente y se copia igual;
     el 2126/2958/2909 de una escena que el usuario 2 tiene (nativa o copiada) se copia, salvo que el mismo payload
     le acabe de llegar de forma nativa.

  **Revisión (10-10, rama `p8-sched-snapshot-2`):** puerta propia (arriba), 2335 en vivo copiados, seguimientos
  de un duplicado nativo, y una cola rechazada que ya no pierde seguimientos (§2.2, §2.3).
- **Dónde:** en `observer_seq_copy` (hilo escritor del *sniffer* + `take_frame`), no en `owner_fanout` (§3).
- **Pruebas:** 5 funciones en `optd_seq_copy` (40/40 suites pasan).
- **Inis:** `observer_sched_copy=1` en `cdmp.p8-s7-host.ini` y `cdmp.p8-noche-local.ini` (la regresión de un PC usa
  el mismo camino: `observer_seq_copy=1` y `[sniffer] enabled=1`).

## 1. Formatos [V salvo 2411]

Payload (tras `u16 opcode | i8 | u16 len`):

| Op | Formato | Tamaños vistos |
|---|---|---|
| 2335 | `u8 tipo | u32 actor @1 | u32 @5 | u32 recurso @9 | u8 @13 | u64 clave @14 | …` | 94–302 B |
| 2126 | `u32 actor | u64 clave @4` | 12 B |
| 2958 / 2909 | `u8 tipo | u32 actor @1 | u32 recurso @5 | u64 clave @9 | n × u32` | 21 / 25 / 29 B |
| 2411 | `u32 actor | u32 id` (id = 32 bits bajos de la clave) **[E]**, sin captura | 8 B |

- La clave de 2958/2909 en `@9` se comprobó contra los 2335 de H1: el 2958 de `t373.59` nombra `0x20000181` con el
  recurso `0x8EFF20F5`, el mismo que el 2335 de esa clave (`t50.47`); igual para `0x2000016E`/`0xB9F63F22` y
  `0x2000017A`/`0xFBD2E9FA`.
- Todos los 2335 de la sesión 11 son de tipo 4 y actor `A0100001` (el Kliff del anfitrión). `0xA9FEE0` solo crea
  instancia con tipo 1/4; con otro tipo busca una existente: por eso un 2335 de otro tipo se guarda como
  «seguimiento» y no sustituye a la creación.
- 2958 llega justo después de su 2335 (H1 `t878.2659` 2335 de `0x200003BA`, `t878.2661` su 2958): el invitado
  recibe el 2335 (ToNear) pero no el 2958 (ToTarget).

## 2. Diseño

Lógica pura en `src/mod/session2/observer_seq_copy_logic.h` (sección *schedule stages*): `SchedLedgerT<N>`,
`SchedTripT<N,H,R>`, `parse_sched`. Módulo: `observer_seq_copy.cpp`.

### 2.1 Libro (`SchedLedger`, 256 escenas)

| Entrada | Efecto |
|---|---|
| 2335 tipo 1/4, clave nueva | `Created`: guarda el paquete entero (≤ 512 B) |
| 2335 tipo 1/4, clave viva | `Recreated`: lo sustituye, sus seguimientos se van, pasa al final del orden |
| 2958 / 2909 / 2335 de otro tipo | `Followed`: detrás de su 2335, en orden (6 como máximo, ≤ 160 B cada uno; al pasar, se va el más antiguo y la entrada queda marcada `follow_lost`) |
| 2126 | `Deleted` |
| 2411 | `Closed` si hay una clave viva con esos 32 bits bajos **y el mismo actor**; si el actor no coincide no se borra y se cuenta (`closed by 2411 … other actor`): así se ve si el formato [E] está mal |
| 2174 propio del anfitrión | `NewField`: se vacía (como el libro de 2661) |
| un servidor nuevo | se vacía (`on_server_replaced`) |

Un 2335 de más de 512 B no se guarda (y se va el anterior de su clave: ya no es el estado del anfitrión). Libro
lleno: `Full` (línea en el acto y en el resumen).

### 2.2 Viaje (`SchedTrip`)

- En el 2174 del usuario 2 (`take_frame`, el dueño tal como lo escribió el servidor; la misma regla que los 2661),
  si es un viaje de observador, las claves del libro pasan a **pendientes** en el orden del libro. Se excluyen las
  escenas cuyo actor es el personaje del usuario 2 (`server_user2_player_key`).
- **Puerta propia (revisión, alta):** el usuario 2 **terminó de cargar** el campo. La abre la primera de:
  1. su **2549** `MoveFieldCompleteReq` al pasar por `offer` (`observer_seq_copy_filter_req`, hilo de red; nunca
     se descarta);
  2. su **2144** al entrar en su cola (`take_frame`).

  Además, por escena: el **actor del 2335** (el Kliff del anfitrión, `A0100001` en S11) ya se anunció al usuario 2
  con un 2005 en este viaje. La liberación corre en el `on_out` de cada 2005 (`kOpAnnounce` → `release_locked`): las
  copias salen **justo detrás del 2005 que anuncia al Kliff**, antes de los 2005 de NPC que vengan después.
- **Por qué no el 2144 solo ni la puerta de los 2661 [V, S11]:** un NPC se enlaza a su escena una sola vez, cuando
  se desempaqueta su 2005 (vf13 `0x9ED290`), sin reintento nativo (`p8-impl-horarios-x.md` §4.1–4.2). En el
  invitado los 2005 del campo llegan **antes** de su 2144:

  | Captura | 2549 | primer 2005 de NPC | 2144 | registros de NPC antes del 2144 |
  |---|---|---|---|---|
  | G1 | `t46.07` | `t46.12` | `t46.94` | 545 de 653 |
  | G2 | `t67.29` | `t67.38` | `t68.20` | 536 de 646 |

  (`inventory\actor_load_records.csv` y `lifecycle.json` del informe independiente). Con el 2144 como puerta la
  instantánea saldría detrás de ~540 NPC; con el 2549 sale en cuanto se anuncia el Kliff. El servidor solo manda
  los 2005 del campo después de procesar el 2549, así que la puerta ya está abierta cuando llega el primero.
- La línea `[sched copy]` dice cuántos **registros de NPC (0xB) de 2005** se habían encolado ya al usuario 2 cuando
  salió cada escena (`NPC 2005 records already queued`); `[sched gate]` lo dice al abrirse. Es la medida de que el
  orden se cumple: debe ser pequeño (los del primer 2005 del campo, si el Kliff va en él).
- Durante el viaje, `take_frame` anota lo que le llega al usuario 2 **de forma nativa**:
  - 2335 que crea, de una escena aún **no copiada y pendiente** → el nativo la sustituye:
    - mismos bytes que el libro (`DuplicateSame`): no se repite el 2335, pero **sus 2958/2909 del libro sí van**
      (son ToTarget: nunca llegan nativos), y los que lleguen después van detrás, en orden;
    - otros bytes (`DuplicateOther`): se descarta nuestra copia entera;
  - 2335 que crea, de una escena que **ya le copiamos** → `NativeAfterCopy`: se cuenta y se escribe una línea
    `[sched native] … NATIVE AFTER COPY` por clave; no se mezcla (el invitado rechaza un segundo *create* de una
    clave que ya tiene en X: lo implementa el otro agente);
  - 2335 que crea, de cualquier otra escena → «el usuario 2 la tiene (nativa)», con el hash del payload;
  - 2126 → ya no la tiene, y el hash queda en un anillo;
  - 2958 / 2909 / 2335 que no crea → hash en el anillo;
  - 2411 → solo se cuenta (su 2126 tiene que llegar igual).
- **Liberación** (`release_locked`, antes que las copias de 2661), por clave pendiente en orden:
  1. si ya no está en el libro → *vanished*;
  2. si el libro tiene una creación más nueva que la que salió → se empieza de nuevo con la nueva;
  3. si su 2335 no ha salido: actor sin anunciar → espera; si no, se envía el 2335 (`push_copy` →
     `server_push_extra`). Rechazado → se queda pendiente con todo lo que va detrás;
  4. sus seguimientos, desde el siguiente que no salió (numeración `follow_total` del libro). Si la cola rechaza
     uno, la clave se queda pendiente **desde ese** (`follow_wait`): no se pierde ninguno ni sale ninguno dos veces.
     Si mientras tanto el libro tuvo que tirar los más antiguos, se dice (`follow_lost`).
- Sin orden por actores NPC: el 2335 no enlaza NPC por clave (su enlace lo hace el NPC al leer su bloque de IA).

### 2.3 En vivo

El hilo del *sniffer* ve el paquete del cliente del anfitrión **después** de que el libro lo tomó:

| Paquete del anfitrión | Usuario 2 | Resultado |
|---|---|---|
| 2335 que crea | le llegó nativo | contado (`Native2335`) |
| 2335 que crea | la escena está pendiente | `KeptInPending` (sale la creación más nueva) |
| 2335 que crea | la tiene solo por nuestra copia | `Requeued2335`: **pendiente otra vez**, salen los bytes nuevos |
| 2335 que crea | no le llegó | `Queued2335`: **pendiente**, con la misma puerta que la instantánea |
| 2126/2958/2909 con el mismo payload que le acaba de llegar nativo (anillo, ≤ 10 s, se consume una vez) | — | `NativeAlready`: no se repite |
| 2958/2909 | la escena está pendiente (también tras su 2335, esperando seguimientos) | `KeptInPending`: sale con ella, en orden |
| 2126 | pendiente y su 2335 aún no salió (y no tenía otra copia) | `CancelledPending` |
| 2126 | pendiente, pero el usuario 2 ya tiene un 2335 de ella | **copiado** (y deja de estar pendiente) |
| 2126/2958/2909 | tiene la escena (nativa o copiada) | **copiado** (el 2126 la quita de «tiene») |
| cualquiera | no la tiene | `NotUser2`: nada |

Tabla de pendientes llena (256): `PendingFull`, contado y escrito. Un 2335 que el libro no pudo guardar (lleno o
demasiado grande) no se pone en pendientes.

**Por qué el orden basta para deduplicar [V por construcción, como el 2981 de `observer_seq_copy`]:** el servidor
escribe a las dos sesiones en la misma emisión; la de la sesión 2 pasa por `take_frame` dentro de esa emisión, y el
cliente del anfitrión la lee después (red del cliente → `OnRecvPacket` → hilo escritor del *sniffer*). El anillo se
consume una vez por coincidencia: dos eventos reales iguales se tratan como dos (INFORME §7.7).

## 3. Dónde vive la copia en vivo: `observer_seq_copy`, no `owner_fanout`

| | `observer_seq_copy` (elegido) | `owner_fanout` (ToTarget::vf1 `0x2A757F0`) |
|---|---|---|
| Qué haría | copiar los bytes que recibió el cliente del anfitrión | añadir la sesión 2 al destinatario del Ack en el servidor (paso 3, reservado) |
| Hilo | hilo escritor del *sniffer* (del mod) y `take_frame` | hilos del servidor, **dentro** del gancho |
| Memoria del juego | ninguna: copia en la cola propia del mod | el array TLS de destinatarios del juego (montón del juego) |
| Estado actual | camino probado con 2661/2810/2981/2322 (sesiones 7–11) | solo cuenta (`probe`); `1` se rechaza |
| Deduplicar nativos | sí: ve lo nativo en `take_frame` y lo del anfitrión en el *sniffer* | no hace falta para ToTarget, pero el 2126 tiene también un sitio ToNear: habría que distinguirlos |
| Instantánea previa | necesita un libro igualmente | no la resuelve |

- La instantánea necesita el libro de todos modos, y ese libro sale de la captura del cliente: el mismo hilo ya
  conoce el 2126/2958/2909 en vivo. Con `owner_fanout` serían dos mecanismos para una misma familia.
- El gancho de `owner_fanout` corre en los hilos del servidor con un `finder` del juego; cambiar sus destinatarios es
  modificar el reparto del servidor (riesgo de cierre que llega a Pearl Abyss). Copiar bytes en la cola propia no
  toca nada del juego.
- `owner_fanout` sigue en `probe` y sirve de **comprobación cruzada**: su resumen compara ahora, para 2126/2958/2909,
  lo que el servidor manda a la sesión del anfitrión mientras el usuario 2 observa (gancho), lo que recibe el cliente
  del anfitrión (*sniffer*) y lo que A pone en la cola del usuario 2 (`A queued`; `kCopiedByA` en
  `owner_fanout_logic.h`). `A queued` de 2126 debe ser `hook − nativos` aproximadamente (los nativos no se copian).

## 4. Hilos y cerrojos

- Todo el estado (`g_sched_ledger`, `g_sched_trip`) está bajo `g_mu`, el cerrojo hoja de `observer_seq_copy`; bajo
  él solo se toma el de la cola de salida (`server_push_extra`). No hay cerrojos nuevos.
- **Hilo escritor del *sniffer*** (del mod): libro, en vivo, liberación.
- **`take_frame`** (hilos del servidor): nativos al usuario 2, el 2174 (instantánea), cada 2005 (cuenta de registros
  de NPC y **liberación detrás del 2005**), el 2144 (puerta) y los seguimientos de un duplicado nativo. Trabajo
  acotado: como mucho 1024 comparaciones por paquete; la instantánea ordena ≤ 256 claves en la pila (2 KB). Empujar
  desde `take_frame` ya lo hacen las copias de 2661 al llegar un 2005.
- **Hilo de red**: el 2549 del usuario 2 (`observer_seq_copy_filter_req`, que nunca lo descarta) abre la puerta;
  `observer_seq_copy_tick` reintenta la liberación (cola que rechazó), líneas y resumen.
- **Sin reservas de memoria** en estas rutas: libro (≈ 390 KB), tabla de «tiene» (1024), anillo (64) y pendientes
  (256) son estáticos. Nada del montón del juego; ninguna llamada al juego; ninguna lectura de su memoria.

## 5. Puertas

| Puerta | De dónde |
|---|---|
| `observer_sched_copy=1` y `observer_seq_copy` en `probe` o `1` (y este en `create_session=1` + `[sniffer] enabled=1`) | `session2::install`; si falta, aviso y apagado |
| Viaje de **observador** (2174 del usuario 2 con un dueño que no es él) | `take_frame` |
| El usuario 2 terminó de cargar: su 2549 (`offer`) o su 2144 (`take_frame`), lo primero | copias de 2335 (instantánea y en vivo) |
| Actor del 2335 anunciado al usuario 2 (2005) en este viaje | copias de 2335 |
| Época del viaje igual a la actual y sin *logout* pedido | `push_copy` (como los 2661) |
| Cola de salida por debajo de la mitad de su presupuesto | `server_push_extra` |

Los 2126/2958/2909 en vivo de una escena que el usuario 2 ya tiene no esperan ninguna puerta (van detrás de su
2335). La puerta de los 2661 (2322 propio o 30 s tras el 2144) solo rige para los 2661.

`probe` de `observer_seq_copy` deja esto también en `probe`: decide y escribe `WOULD SEND`, no envía nada.

## 6. Líneas del log (anfitrión)

| Línea | Cuándo |
|---|---|
| `ObserverSchedCopy ON …` | al instalar (o el aviso de que falta `observer_seq_copy`) |
| `ObserverSeqCopy [sched ledger] … 2335 kind 4 of stage 0x… : created \| ledger live N` | cada paquete del libro (tope 400 líneas; `FULL`/`TOO BIG` siempre) |
| `ObserverSeqCopy [sched trip] user 2's observer trip: N of the ledger's M live schedule stage(s) wait …` | cada viaje de observador |
| `ObserverSeqCopy [sched gate] user 2 finished loading the field (its 2549 … / its 2144 …) \| NPC 2005 records already queued to user 2: K` | una vez por viaje |
| `ObserverSeqCopy [sched copy] stage 0x… (snapshot / live 2335 of the host, …): SENT … its 2335 + k follow-up(s) … \| NPC 2005 records already queued to user 2: K` | cada escena enviada; también `more follow-up(s)` (reanudación), `reached user 2 natively first … NOT repeated` (duplicado, con sus seguimientos), `left the ledger` (*vanished*), `REFUSED`, y las notas `follow-ups the ledger dropped` / `the queue REFUSED a follow-up` |
| `ObserverSeqCopy [sched native] user 2 got the 2335 of stage 0x… natively: … / NATIVE AFTER COPY …` | un 2335 nativo de una escena pendiente o ya copiada |
| `ObserverSeqCopy [sched live] the host's 2126 of stage 0x…: SENT / not repeated / not a stage user 2 has / CANCELLED / PENDING …` | cada decisión en vivo (salvo los 2335 nativos, que solo se cuentan) |
| `ObserverSchedCopy summary` | cada 60 s, con el resumen de `observer_seq_copy` |

El resumen trae: **tamaño del libro** (vivas de 256, creadas, seguimientos, borradas, cerradas, llenas, demasiado
grandes, campos), **instantáneas** (viajes, claves, del usuario 2 excluidas), la **puerta** (cerrada / abierta por el
2549 / por el 2144) y los **registros de NPC encolados** en el viaje, pendientes (la primera, tabla llena),
**enviadas** (2335 + seguimientos, seguimientos rechazados y reanudados), **duplicados suprimidos** (mismos / otros
bytes), **NATIVE AFTER COPY**, *vanished*, canceladas, rechazadas, **en vivo por opcode** (2126/2958/2909/2335 que no
crea: copiadas, ya nativas, no del usuario 2, con una copia pendiente, rechazadas), 2335 del anfitrión en el viaje
(nativos / **no nativos, copiados** / creados otra vez sobre nuestra copia), lo nativo al usuario 2 por opcode, y la
tabla de «tiene». La línea de estado (`describe`) añade `sched copy: ledger …, sent …, live 2126/2958/2909, dup …`.

## 7. Qué mirar en la prueba

En el **anfitrión**:

1. `ObserverSchedCopy ON` al arrancar.
2. Antes de que entre el invitado, `[sched ledger]` con decenas de `created` (sesión 11: 42–46 vivas).
3. En la entrada: `[sched trip] … N of the ledger's M` con N ≈ M.
4. `[sched gate] … (its 2549 …) | NPC 2005 records already queued to user 2: 0` (o casi).
5. `[sched copy] … SENT` por cada una con **`NPC 2005 records already queued` pequeño** (los del 2005 que llevaba al
   Kliff, si alguno): es el criterio de orden. Si es de cientos, el Kliff se anunció tarde: mirar en qué 2005 va.
6. Pocos `[sched native]`; `NATIVE AFTER COPY` debería ser 0 (en S11 ninguna escena previa a la entrada se
   reenvió de forma nativa).
7. En vivo: `[sched live] the host's 2126 … SENT` y `… not repeated` (sesión 11 fase 1: unos 122 copiados y 67
   nativos); 2958/2909 `SENT`; `2335 … PENDING` raros (2335 creados lejos del invitado).
8. Resumen: `full 0`, `too big 0`, `refused 0`, `closed by 2411 … other actor 0` (si no es 0, el formato [E] del
   2411 está mal: revisar).
9. `OwnerFanout compare`: 2126/2958/2909 `A queued` cerca de `hook` menos los nativos.

En el **invitado** (con `guest_schedule_load=1` y el enrutado del otro agente):

- `GuestSchedLoad [load]` de los recursos de los 2335 copiados antes de entregarlos;
- G3: X+148 con instancias y los NPC de horario (los 17/18 ids que faltaban) encontrando su escena **en su vf13**
  (el 2335 antes que su 2005);
- tras los 2126 copiados, instancias que **bajan** (no crecen sin parar);
- si llega un 2335 nativo de una clave ya creada por copia, el rechazo del lado del invitado.

En un PC (`cdmp.p8-noche-local.ini` + `cdmp_peer --mode replay`): el *peer* graba lo que recibe; los 2335 del
libro deben aparecer justo detrás del 2005 del Kliff del anfitrión y antes de la mayoría de 2005 de NPC.

## 8. Criterios de aborto

Poner `observer_sched_copy=0` (lo demás de `observer_seq_copy` sigue igual) si:

- el invitado se cierra o se congela al entrar en el campo del anfitrión o al recibir la instantánea (ahora llega
  **durante la carga del campo**, antes de su 2144: es el punto más delicado);
- en el invitado las instancias de horario crecen sin bajar tras los 2126 (duplicados o huérfanas);
- `NATIVE AFTER COPY` aparece y el invitado no lo rechaza (instancias dobles);
- aparecen C>S del invitado sobre escenas `0x2000…` del anfitrión que cambian su estado (G4 de `guest_scene_diag`);
- `refused` > 0 de forma sostenida (la cola de salida va llena: la instantánea compite con los paquetes reales);
- `closed by 2411 … other actor` crece (formato del 2411 equivocado: el libro conservaría escenas cerradas).

Para medir sin enviar: `observer_seq_copy=probe` (deja las dos cosas en `probe`).

## 9. Riesgos

| Id | Riesgo | Mitigación |
|---|---|---|
| S1 | **El Kliff del anfitrión se anuncia tarde** (detrás de muchos 2005 de NPC): sus escenas salen detrás de él y esos NPC ya pasaron su vf13 sin escena (no hay reintento nativo, `p8-impl-horarios-x.md` §4.1) | La línea `[sched copy]` mide los registros de NPC ya encolados. Si son muchos, la opción siguiente es dar el Kliff por anunciado cuando el usuario 2 ya lo tiene de un viaje anterior, o mirar en qué 2005 va; no se adelanta el 2335 a su actor (su manejador resuelve el actor) |
| S1b | **2335 durante la carga del campo** (tras el 2549, antes del 2144). De forma nativa el anfitrión los recibe tras su 2144; el invitado procesa ya en ese tramo los 2005 del campo **[E]** | El 2549 es el punto nativo desde el que el servidor manda el contenido del campo; si el invitado falla aquí, cambiar la puerta a solo el 2144 (una línea en `observer_seq_copy_filter_req`) |
| S2 | Duplicado: un 2335 nativo que `take_frame` no ve (p. ej. dentro de un `ScopeSenderAck` 2042 si el servidor agrupa) | Se compara por clave, no solo por bytes; `NATIVE AFTER COPY` y «mismos / otros bytes» en el resumen. Si aparecen instancias duplicadas, mirar 2042 |
| S3 | Orden *sniffer* / `take_frame` no garantizado en algún camino: un 2126/2958 nativo visto después del del anfitrión se copiaría además | El mismo supuesto que el 2981 de los 2661, ya probado; un 2126 repetido sobre una escena ya borrada no encuentra nada en el cliente [E] |
| S4 | El 2411 tiene formato [E] | Solo borra si coinciden id **y** actor; si no, cuenta |
| S5 | 2958 no idempotente: un cambio aplicado dos veces | El anillo de nativos (10 s, se consume una vez); un seguimiento rechazado se reanuda desde él, sin repetir los anteriores |
| S6 | El estado de la escena depende de más que 2335 + 2958/2909 (2499/2127/2839, misiones del dueño: INFORME §2) | Ninguno de esos opcodes apareció en la sesión 11; si aparecen, se añaden al libro igual que 2958 |
| S7 | Seguimientos perdidos (más de 6 por escena, también mientras una clave espera tras un rechazo) | `follow_lost` en la línea y en el resumen |
| S8 | Un 2335 creado otra vez sobre nuestra copia (`Requeued2335`) llega al invitado como segundo *create* de la clave | Lo rechaza el invitado (otro agente); contado en el resumen |
| S9 | Sin el enrutado del invitado, los 2335 copiados crean escenas en M (como los nativos) | Los 2126 en vivo llegan; el enrutado a X es del otro agente |

## 10. Pruebas (`tests/optd_seq_copy_tests.cpp`)

- `test_sched_parsers`: 2126/2958/2909 reales de H1 (12/21/25/29 B), 2335 sintetizado con su formato, tamaños malos,
  2411, `sched_op`, `sched_creates`, índices.
- `test_sched_ledger` (`SchedLedgerT<4>`): crear, orden, seguimientos (también de un 2335 que no crea), recrear
  (reemplaza y vacía seguimientos), 2126, 2411 con actor distinto (no borra) y con el mismo, lleno, demasiado grande,
  seguimientos por encima del tope (se va el más antiguo), malformado, el 2174 propio, `clear_all`.
- `test_sched_snapshot_and_live`: viaje que no es de observador, instantánea en orden sin las escenas del usuario 2,
  usuario 2 cargando (nada sale aunque el Kliff esté anunciado), puerta por el 2549 (una vez), envío byte a byte
  con su 2958 y el número de registros de NPC, 2126 en vivo (una vez), `NativeAfterCopy`, 2958 nativo suprimido y
  otro copiado, dos eventos iguales (uno nativo), 2909, caducidad del anillo, 2126 nativo, escena que el usuario 2
  no tiene, **2335 en vivo no nativo → pendiente → copiado con su 2958**, 2335 nativo y su 2958, **recreado sobre
  nuestra copia → copiado otra vez**, 2335 que no crea, fin del viaje.
- `test_sched_native_first`: 2335 nativo antes de la copia con los mismos bytes (no se repite; **sus seguimientos
  y uno posterior sí, en orden**), luego `NativeAfterCopy` de otra; con otros bytes (se descarta la copia).
- `test_sched_pending_refusal_bounds`: 2126 que cancela una pendiente y 2958 que viaja con ella, campo nuevo antes de
  la puerta (*vanished*), 2335 rechazado (se queda), **seguimiento rechazado: la clave sigue pendiente desde él, uno
  nuevo se pone detrás, nada se pierde ni se repite**, seguimientos que el libro tiró mientras esperaba
  (`follow_lost`), recreación mientras esperaba, nativos fuera de un viaje, tabla de «tiene» llena, anillo lleno,
  tabla de pendientes llena, y la escala de la sesión 11 (46 escenas que salen justo detrás del 2005 del Kliff, con 2
  registros de NPC antes).
- `optd_owner_fanout`: `copied_by_a` y el orden de la comparación con 2126/2958/2909.

`.\scripts\build.ps1`: 40/40 suites.
