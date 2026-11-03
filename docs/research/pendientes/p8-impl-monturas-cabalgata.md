# P8 monturas: cabalgata. Muestras densas del caballo del invitado en el anfitrión y un mismo reloj para la pareja. Implementación

11 de octubre de 2026, rama `p8-mount-ride` (desde `opcion-d` `f93ca39`). Build del juego **1.0.0.2976**, base
`0x140000000`, direcciones en RVA. No se lanzó el juego ni se desplegó nada.

Diagnóstico de partida: el informe independiente de monturas (`mounts-20261011-work/INFORME.txt` §3, §5 «Cabalgata»,
«Orden temporal único» y «Tiempo de reproducción común», §7) y su auditoría de módulos (`modules/INFORME.txt` §1–§3).
Las transiciones (montar y desmontar: `mount_sync`, `ride_filter`) las trabaja otra rama y quedan fuera de aquí.

- **[V]** = comprobado (código, tests, documentos de la build).
- **[E]** = deducción fuerte, falta medirla en vivo.

## 1. El problema, en números de S13 [V]

| Medida | H cabalga, G observa | G cabalga, H observa |
|---|---|---|
| Move Ack del caballo en el observador | 248 | 10 |
| Intervalos del caballo p50 / p90 / máx | 26 / 28 / 54 ms | 346 / 707 / 956 ms |
| Retardo de interpolación | caballo hacia 200 ms (muestras densas) | caballo 900 ms (tope owned) y luego 626 ms; jinete 200 ms |

- `horse_follow` (anfitrión) ya fabrica, para el invitado, un 2697 del caballo por cada 2697 del jinete montado (836 en
  S13, sin fallos). La ruta inversa (`guest_horse_probe`) solo mide: 2570 Req candidatas construidas y ninguna
  enviada; 18 de las 28 Req propias del caballo tenían tick menor o igual que el último del jinete.
- `remote_interp` da a los `0xB` un retardo adaptativo y deja a los `0xA` con el del juego: jinete y caballo se
  muestran en instantes distintos aunque los dos tengan datos correctos.

## 2. Qué hay

| Clave `[session2]` | Dónde | Valores | Qué hace |
|---|---|---|---|
| `horse_follow_reverse` | solo anfitrión (`create_session=1`, `[sniffer] enabled=1`, `child_hold` activo) | `0` (defecto) / `probe` / `1` | Por cada 2697 del jinete del usuario 2 montado en SU caballo que llega al **cliente del anfitrión**, entrega antes otro 2697 del caballo, construido a partir del primero (§3) |
| `remote_interp_pair` | anfitrión **e** invitado (necesita `remote_interp` y `[sniffer] enabled=1`) | `0` (defecto) / `1` | El caballo que monta un jugador apunta al retardo del jinete mientras sus muestras sean densas; mismo acercamiento sin saltos; recorte al primer sample de un búfer lleno (§4) |

**Ficheros:**
- `src/mod/session2/horse_follow_logic.h`: `make_horse_ack_into` (el mismo constructor que `horse_follow`, sin memoria
  dinámica), `MonoTs` (último tick aceptado por actor), `Intervals` (p50/p90/máx), `PairCache`, `decide_reverse`.
- `src/mod/session2/horse_follow.{h,cpp}`: el observador del cliente (`client_recv`), la ruta inversa, las medidas
  H→G nuevas y las líneas del resumen.
- `src/mod/session2/remote_interp_logic.h`: `RidePairs`, `pair_target`, `clamp_to_buffer`, `first_sample_ts`,
  `update_step(…, pair_delay)`.
- `src/mod/session2/remote_interp.{h,cpp}`: la pareja en el gancho de actualización de posición y la línea
  `RemoteInterp summary pairs`.
- Fuera de esos módulos, lo mínimo: `src/mod/net/troc_sniffer.{h,cpp}` (una segunda ranura en el gancho
  `OnRecvPacket` que ya existe: `sniffer_set_recv_extra`), `config.{h,cpp}`, `mod.cpp` (línea `Config: P8 mount ride`
  y el parámetro de `remote_interp_start`), `session2.cpp` (configuración y resumen por minuto). `ride_filter` no
  se toca.
- `tests/optd_remote_interp_tests.cpp`: `test_reverse`, `test_reverse_stream`, `test_pairs`, `test_pair_ramp`.
- `.ini`: §6.

## 3. `horse_follow_reverse`: el caballo del invitado en el cliente del anfitrión

Es la «variante visual con Ack sólo en H» del informe (§7): **mejora de presentación**, no sincronización
autoritativa. El servidor sigue recibiendo solo las Req reales del invitado; su física, colisiones e IA del caballo
no cambian, y ningún otro observador recibe nada nuevo.

### 3.1 Dónde y cómo [V]

- **Punto:** el mid-hook que el sniffer ya tiene en `ClientNet::OnRecvPacket` (`0x35CA00`, prólogo verificado por
  `sniffer_install`). No hay gancho nuevo ni bytes nuevos. El hilo es el `NetworkThread` del cliente del anfitrión.
- **Entrega:** `sniffer_set_recv_extra` añade una segunda ranura, independiente de la de `ride_filter`. Para cada
  paquete S→C, después de la reescritura, el callback puede devolver **un** paquete extra. El gancho lo entrega en
  ese momento con la misma función, en el mismo hilo y antes del real. Es el camino del experimento 4c
  (`t_injecting`). El callback se llama siempre; la entrega, solo con la inyección del sniffer activa (tabla de
  opcodes comprobada y sin fallo previo de 4c). Si no lo está, cuenta como `refused`.
- **Captura y taps:** el paquete extra queda en la captura marcado como del mod: bit 0 de `pad8` en el registro del
  `.bin` (los lectores que lo ignoran ven el formato de siempre) y `[mod: derived, not the server's]` en el `.log`.
  No pasa por los taps del sniffer (libros de `host_scene_diag`, `observer_seq_copy`, `owner_fanout`) ni por el
  observador de co-op, y nunca se reescribe ni se vuelve a observar.
- **Excepción dentro de `OnRecvPacket` con el paquete del mod (regla I1):** es del juego, que puede tener tomado el
  cerrojo de su cola. El filtro propio de la entrega apaga la ranura, escribe una línea ERR (`Sniffer recv-extra`,
  `game_fault_continue`) y **deja pasar la excepción**, como `apply_fault` de `mount_sync`. No se traga.
- **Sin memoria del juego ni del mod:** copia del paquete bajo SEH a la pila (240 B), construcción en un búfer de
  pila de 512 B y contadores atómicos. No asigna memoria, no toma cerrojos y no escribe en el log.
- **El caballo:** `make_horse_ack_into` aplica la receta de `horse_follow` byte a byte (test: idéntico a
  `make_horse_ack`):
  - clave del caballo;
  - pos/quat = `Path.world` del jinete;
  - aux = 0;
  - `Path.local` = el mismo transform, escala 1;
  - padre −1;
  - `+0x94..+0xD4` = 0;
  - **ts y cola del jinete**.

### 3.2 Validación de la pareja [V código / E en vivo]

Hace falta todo lo siguiente:
1. El jinete es el **personaje principal del usuario 2**: `server_user2_player_key()`, su `MainPlayerDataStartAck`
   de esta época.
2. El padre del `Path` de la muestra es una clave `0xB` (`read_move`: `Shape::Ridden`) y la pose de mundo no es
   cero.
3. El actor del caballo en el servidor tiene como **dueño el `ServerUserActor` del usuario 2**: `[actor+0xA0]`
   leído con las lecturas SEH de `child_hold` (`hold_actor_owner`) y comparado con `server_user2`. El resultado se
   guarda 1 s por caballo, así que no se recorre la tabla de actores en cada Ack.
   - Ilegible (por ejemplo, sin `observer_hold` ni `child_settle_ms`) → `owner unreadable`: no se deriva nada.
   - Otro dueño → `horse not user 2's`.
   - La cuenta `its Reqs name the horse` indica además cuántas veces el usuario 2 ya mandó Req propias de ese
     caballo, que es la prueba de que lo controla. Solo se cuenta; no es condición.

Un caballo con dueño del usuario 2 bajo su personaje principal es su montura propia (tipo 5 en los 2005 de S13). No
se usan como condición ni `g_user2_keys` ni la clase de clave sola, que es la objeción de `modules/INFORME.txt` §2.

### 3.3 Orden temporal [V]

- **Una muestra por tick.** Se deriva solo si el ts del jinete es **mayor** que la última muestra del caballo que
  recibió el cliente, derivada o propia (`MonoTs`). Un tick igual cuenta como `same tick` y uno anterior como
  `older than the horse's last`. Nunca se adelanta ni se altera un timestamp.
- **Las muestras propias del caballo** se siguen entregando tal cual. Si su ts no es posterior a la última derivada
  (S13: 18 de 28), cuentan como `stale`. Las descarta el propio búfer del cliente: `setInterpolatePositionData`
  `0x11B1FD0` desecha `ts_nueva ≤ ts_última` (`movimiento-remoto-cliente.md`, «Insertar», paso 2; también el
  contador `older samples` de `RemoteInterp`). No se reescribe ningún paquete real, así que un `StartAction` del
  caballo (otro paquete) se sigue aplicando entero.
- **Sin realimentación:** las derivadas existen solo en el cliente del anfitrión. No pasan por `server_inject` ni
  por `take_frame` y no vuelven nunca al controlador.
- **Fin:** cuando las muestras del jinete dejan de nombrar al caballo (desmontar), deja de haber derivadas. Una
  época nueva olvida las parejas, los ticks y las comprobaciones (`horse_follow_on_epoch`).

### 3.4 Lo que no cubre

- El servidor sigue con las muestras escasas del caballo; las colisiones y la IA del caballo en el servidor no
  cambian.
- Las transiciones de montar y desmontar son de otra rama.
- Los `StartAction` del caballo no se derivan.

## 4. `remote_interp_pair`: un reloj para jinete y caballo

### 4.1 La pareja [V]

- **De dónde sale:** el mismo observador de `OnRecvPacket`, ahora en los dos PCs. Cada 2697 de un `0xA` registra en
  `RidePairs` jinete → padre del `Path`. Si va a pie o tiene otro padre, la pareja de ese jinete termina, y
  solo así. Un jinete quieto en la silla no manda Move durante un rato, así que el silencio no la termina:
  solo 60 s sin Ack la olvidan (un jinete que desaparece sin último Ack: despawn, salida, paquete perdido).
- **Lectura:** el gancho de `updateInterpolatePosition` (`0x11B249A`) la consulta solo para claves `0xB`. Si no hay
  ninguna pareja viva, no recorre la tabla.

### 4.2 El objetivo [V]

- **Si el caballo tiene jinete:** su objetivo es el retardo del jinete. Un `0xA` siempre lee el valor del juego (en
  la práctica 200 ms), y el objetivo se toma solo si **cada uno de sus 3 últimos huecos** cabe en ese retardo
  (`pair_target`; `rider's delay taken`).
- **Si no:** el caballo mantiene su objetivo adaptativo (`own target kept`). Sin muestras densas, leerlo a 200 ms
  vaciaría el búfer a saltos, que es peor.
- **El retardo se mueve igual que siempre:** con `approach()`, como mucho +0,5 / −0,25 ms por ms. No hay salto al
  montar ni al desmontar; al desmontar vuelve a su propio objetivo de la misma manera (test `test_pair_ramp`: de
  900 a 200, ≤ 4 ms por frame de 16 ms, y vuelta a 900 a ≤ 8 ms por frame).
- **Alcance:** solo cambia el caballo de la pareja. Todos los demás `0xB` siguen con su retardo adaptativo y no se
  toca el retardo global.

### 4.3 El recorte al búfer lleno [V documento + test; E en vivo]

- **La limitación del juego** (`movimiento-remoto-cliente.md`, «Insertar» y S4):
  - guarda **30 muestras** por búfer y descarta la más antigua;
  - si `now − D` queda antes de la primera, **no mueve el actor**.
- **El riesgo:** con el jinete del invitado cada ~12 ms (S13), 30 muestras cubren unos 360 ms. Un caballo que empieza
  a recibir muestras densas con D = 900 (tope owned) se quedaría quieto unos 2 s mientras baja el retardo, y luego
  daría un salto.
- **El recorte:** mientras hay pareja y el búfer está lleno (≥ 29), D no puede pedir un instante anterior a su primera
  muestra **ni coincidir con ella**: `D = min(D, now − ts[0] − margen)`, nunca por debajo del valor del juego ni
  subiendo (`clamp_to_buffer`). El juego no mueve el actor con `T ≤ ts[0]`, así que recortar a `now − ts[0]` exacto
  lo dejaría parado en ese frame. El margen es un frame (33 ms) o el mayor hueco reciente del caballo, si es mayor,
  con un máximo de 100 ms (`buffer_margin`); así una ráfaga que adelanta `ts[0]` sigue dentro. Se cuenta como
  `D cut to its full buffer's first sample`.
- **Por qué no se nota:** en ese momento el caballo estaba quieto en su pose más antigua, y el paso lo lleva a la
  pose siguiente, no a otro instante.
- **También en el otro sentido:** el problema existe en H→G (26 ms → ~780 ms de búfer frente a 900), así que el
  invitado también lo aprovecha.

## 5. Medidas para la próxima sesión

Línea del minuto en el anfitrión, `Session2 probe (minute N)`, generada por `horse_follow_describe`. Los intervalos
son ms entre muestras del caballo aceptadas, más nuevas que la anterior; un hueco de más de 3 s es una pausa y se
cuenta aparte.

| Pregunta | Dónde | Campos |
|---|---|---|
| Intervalos del caballo G→H (en H), todas y solo las propias | `HorseFollowReverse [...]` | `intervals of the horse samples (ms) all [n p50 p90 max] own only [...]` |
| Intervalos del caballo H→G (hacia G), todas y solo las propias | `HorseRide H->G (to user 2)` | `intervals (ms) all [...] own only [...]` |
| Derivadas frente a reales | las dos líneas | `derived N` / `its own N`, `handed`, `delivered by OnRecvPacket`, `refused`, `FAULTED` |
| Descartadas por viejas | las dos líneas | `stale`, `not newer than the last` (reales y derivadas) |
| Rechazos de la pareja | `HorseFollowReverse` | `not user 2's rider`, `owner unreadable (checks)`, `horse not user 2's`, `same tick`, `older than the horse's last`, `its Reqs name the horse` |
| D del jinete frente a D del caballo | `RemoteInterp summary pairs` (los dos PCs, cada 60 s) | `riding now [rider on horse: D rider, D horse, gaps]`, `began/ended`, `rider's delay taken / own target kept`, `D cut …`, `|D horse - D rider| per paired frame` |

- **En el `.bin` del anfitrión:** cada derivada aparece justo antes del Ack del jinete del que sale, con la clave
  del caballo, aux 0, padre −1 y el mismo ts.
- **Comparación con S13:**
  - el caballo de G en H debería pasar de p50 346 / p90 707 / máx 956 ms a la cadencia del jinete (p50 ≈ 12–26 ms);
  - `own only` conserva la cadencia antigua en el mismo log;
  - `|D horse - D rider|` debería concentrarse en `<=0` / `<=10` cuando la pareja ya convergió.

## 6. `.ini`

- `cdmp.p8-s7-host.ini` y `cdmp.p8-noche-local.ini`: `horse_follow_reverse=1` y `remote_interp_pair=1`.
- `cdmp.p8-s7-shadow.ini`, `cdmp.p8-s7-shadow-fase2.ini` y `cdmp.p8-s12-shadow-seguro.ini`: `remote_interp_pair=1`.
  En el invitado no hay ruta inversa.

Por qué `1` y no `probe`:
- **El paquete no es nuevo.** Es exactamente el que `horse_follow` ya entrega al cliente del invitado (S13: 836 sin
  fallo de construcción ni rechazo), ahora con el caballo del usuario 2.
- **El camino de entrega tampoco.** Es el del experimento 4c: misma función y mismo hilo, antes del paquete real.
  Un fallo lo apaga solo.
- **La regla de orden impide lo que el informe temía:** mezclar muestras atrasadas con derivadas. Ninguna derivada
  va al servidor.
- **La pareja de `remote_interp` es lógica pura con fallback:** si las muestras no son densas, se queda el objetivo
  de siempre.
- **La comparación queda en el mismo log:** las columnas `own only` y `own target kept` siguen midiendo el
  comportamiento anterior.
- **Volver atrás no requiere recompilar:** `horse_follow_reverse=0` / `remote_interp_pair=0` (o `probe` en el
  primero, que decide y mide sin entregar).

## 7. Riesgos y criterios de aborto

- **Re-entrada en `OnRecvPacket` (E):** el camino 4c no se había usado en opción D. Una excepción del juego con el
  paquete del mod no se traga: se apaga la ranura, se escribe ERR y la excepción sigue su curso (el juego cae ahí,
  con su informe). Si aparece `FAULTED > 0` o esa línea ERR, abortar el ensayo de cabalgata, guardar los logs y
  seguir con `horse_follow_reverse=0`.
- **Lecturas del servidor desde el hilo del cliente (E):** `hold_actor_owner` lee la tabla de actores sin cerrojo
  (SEH, como `attach_follow`) como mucho una vez por segundo y caballo. Si sale ilegible, no se deriva nada.
- **Doble productor (E):** las Req reales del caballo siguen llegando. Las anteriores a la última derivada se
  descartan en el búfer del cliente; las posteriores entran en orden. Si el contador `stale` crece sin parar o se
  ve al caballo retroceder, comprobar en el `.bin` el orden de llegada.
- **Divergencia de autoridad:** el servidor y los demás observadores no ven la mejora. Las colisiones del caballo
  del invitado en el servidor siguen con muestras escasas.
- **Recorte del búfer (E):** el paso que da al cortar D debería ser invisible (el caballo estaba quieto). Si se ve
  un salto al empezar a galopar, mirar `D cut …` frente a `rider's delay taken` y el vídeo.
- **Desajuste residual:** el jinete lee el retardo del juego (200) y el caballo llega a él con `approach()`. Durante
  la rampa (≤ 2,8 s si parte de 900; menos con el recorte) siguen en instantes distintos.

**Abortar si:**
- `FAULTED > 0`;
- crash o congelación en el `NetworkThread` del anfitrión;
- el caballo del invitado visible en H en una posición absurda (pose del `Path` de otro actor);
- `not user 2's rider` cuenta pares que sí lo son;
- `|D horse - D rider|` sigue alto con `rider's delay taken` alto.

## 8. Tests [V]

`tests/optd_remote_interp_tests.cpp` (`optd_remote_interp`); `.\scripts\build.ps1`: 45 de 45 (tras la fusión con `mount_sync`).
- `make_horse_ack_into` es idéntico byte a byte a `make_horse_ack` y rechaza búferes cortos y paquetes que no son de
  jinete.
- `decide_reverse`: todas las ramas (apagado, otro jinete, clave desconocida, sin mundo, dueño ilegible o ajeno,
  mismo tick, más viejo, probe, derivar).
- `MonoTs`: primero, más nuevo con hueco, igual, más viejo (nunca retrocede) y tabla llena. `Intervals`: cuantiles
  por cubeta y pausas. `PairCache`: recomprobación a 1 s.
- Modelo de la cabalgata G→H de S13: jinete cada 12 ms, caballo propio cada ~346 ms con la mayoría al mismo tick o
  antes.
  - Resultado: 460 derivadas, ninguna repetida, todas con el tick del jinete; las propias viejas cuentan como
    `stale`.
  - Intervalos `all` ≤ 15 ms (p50/p90/máx) frente a `own only` ≥ 300 ms.
- `RidePairs`: alta, sigue emparejado 10 s sin Ack (jinete quieto), se olvida solo tras 60 s, fin al ir a pie, cambio de caballo, tabla llena. `pair_target` denso o
  escaso.
- `update_step` con pareja: rampa monótona 900 → 200 sin saltos, vuelta a 900 al terminar, caballo escaso sin
  cambio, `0xA` nunca con retardo propio. `clamp_to_buffer` y `first_sample_ts` sobre memoria falsa.

`clamp_to_buffer` deja `T` estrictamente después de `ts[0]` (margen de frame o de hueco). No prueban el gancho en vivo, la marca del registro en el
`.bin`, el filtro de la excepción, la re-entrada en `OnRecvPacket` ni el comportamiento del motor.

## 9. Siguiente

- Sesión con el guion del informe (§8): G monta, camina, galopa y desmonta; luego H igual. Con vídeo de los dos PCs.
- Si `HorseFollowReverse` sale limpio, valorar la variante autoritativa: Req del caballo hacia el servidor con
  `actorSlot`, historial y puertas validados (informe §5, «Cabalgata»). Esto ya no sería presentación y necesita
  sus propias sondas.

## 10. Revisión independiente de `3aaf038` (rama `p8-mount-ride-2`)

| # | Hallazgo | Corrección |
|---|---|---|
| 1 MEDIO | La entrega del paquete extra tragaba con `EXCEPTION_EXECUTE_HANDLER` una excepción dentro de `OnRecvPacket` (regla I1: el juego puede tener un cerrojo de cola) | Filtro propio (`extra_fault`): `g_extra = nullptr`, cuenta `FAULTED`, ERR y `game_fault_continue` (la excepción sigue) |
| 3 BAJO-MEDIO | Recortar D a `now − ts[0]` exacto pone `T == ts[0]` y el juego no mueve con `T ≤ ts[0]` | `D ≤ now − ts[0] − margen`, margen = máx(33 ms, mayor hueco reciente) ≤ 100 ms |
| 4 BAJO | La pareja caducaba a 1,5 s: un jinete quieto en la silla devolvía al caballo a su tope de 900 ms | Fin solo por Ack explícito sin padre u otro padre; olvido tras 60 s de silencio |
| 5 BAJO | Las derivadas llegaban a los taps del anfitrión (`session2.cpp`, `owner_fanout`) y a la captura como tráfico del servidor | El registro lleva la marca del mod: ni taps ni co-op; `.bin` `pad8` bit 0 y `[mod: …]` en el `.log` |
| 6 BAJO | El camino extra ignoraba `g_injection_enabled` | La entrega lo exige (contador `refused`); el callback sigue corriendo para las parejas |

