# Combate con retardo (ROADMAP, opción D, punto 5): presupuesto por tramo, causa raíz y arreglo

11 de octubre de 2026, rama `opcion-d` (HEAD `43fc817`). Build del juego **1.0.0.2976**, PE `0x6AB28F00`, base
`0x140000000`; direcciones en RVA. **Solo investigación:** no se tocó `src/`, no se lanzó el juego, no se paró el
servidor de Ghidra y no hay commits. El único fichero escrito en el repositorio es este.

- **[V]** comprobado: captura, log, código del mod o desensamblado/decompilado de esta build (citado al lado).
- **[E]** deducción fuerte o simulación sobre datos reales; falta medirlo en vivo.
- **[H]** hipótesis.

## Fuentes

| Id | Fichero | Qué |
|---|---|---|
| G11 / GL11 | `C:\Emulador CD\backups\p8-sesion11-shadow\cdmp_packets_20261010-191530.bin` / `cdmp_20261010-191512.log` | invitado S11 (el combate más largo: 177 golpes del invitado, t = 440–720 s) |
| H11 / HL11 | `…\p8-sesion11-host\cdmp_packets_20261010-201238.bin` / `cdmp_20261010-201225.log` | anfitrión S11 |
| G10 | `…\p8-sesion10-shadow\cdmp_packets_20261010-173057.bin` | invitado S10 (75 golpes, t = 440–640 s; su log de la fase 1 se perdió) |
| G14 / H14 | `…\p8-sesion14-shadow\cdmp_packets_20261011-011835.bin`, `…\p8-sesion14-host\cdmp_20261011-021131.log` | S14 (poco combate del invitado) |
| logs S10–S22 | `…\p8-sesion1x-host\cdmp_*.log`, `…\p8-sesion1x-shadow\cdmp_*.log` | colas, bucles, `FreezeWatch` |

Scripts (Python `-I`, capstone no hizo falta; lectores del formato `BinRecord` de `troc_sniffer.cpp`) en
`%TEMP%\noche-combate\`: `cap.py` (lector), `find_combat.py` (qué capturas tienen combate), `guest_combat.py`
(latencias en el invitado), `host_combat.py` (latencias en el anfitrión), `align.py` (dos relojes),
`sim_d.py` (retardo `D` de `remote_interp` con la regla de inserción del juego), `follow_cover.py` (cobertura de
`combat_follow`), `enemy_trace.py` (traza de un enemigo), `engage_start.py`, `dump.py`.

**Qué capturas tienen combate [V `find_combat.py`]:** S10 (450–630 s), S11 (440–720 s, el grueso), S14 (330–420 s,
sobre todo enemigos que golpean), S12/S13 algún golpe suelto; **S15–S22 no tienen combate** (son las de monturas,
viaje y NPC). El punto 5 del ROADMAP se apuntó tras la sesión 16 (`f93ca39`) sin más detalle del usuario; el último
análisis de combate escrito es `pendientes/p8-s9-combate-montura.md` (S9, antes de `combat_follow`). Todo lo que
sigue mide las sesiones **con** `combat_follow=1` en el anfitrión (activo desde S10).

---

## 0. Resumen

1. **Qué va con retardo, visto en el invitado (S11, p50) [V salvo donde se dice]:**

   | Lo que ve el invitado | Retraso | Un jugador (anfitrión) |
   |---|---|---|
   | Su propio ataque (animación) | 0 (local) | 0 |
   | Confirmación del golpe (`AttackTargetAck` 2922: impacto, número de daño) | **63,5 ms** p50, 91 p90 tras su `DoFrameEventReq` (S10: 61 / 75) | **4 ms** |
   | Animación de reacción del enemigo (`StartActionAck` 2991 del enemigo) | +32 ms tras el 2922 → **~95 ms** tras el golpe | ~20 ms |
   | **Cuerpo del enemigo** (retroceso, embestidas, esquivas) | **`D` de `remote_interp`: p50 228 ms, p75 417, p90 952 en los golpes del invitado (S10: 599 / 1091 / 1247); en todo el tiempo de enganche p75 921 ms (S10: 1255)** [E: simulación con la regla del juego] | 0 (simulado local) |
   | Golpes de los enemigos sobre el invitado | los decide el **cliente del anfitrión** contra su copia del invitado, que va **200 ms** por detrás (retardo de juego para `0xA`), y el invitado se entera ~27 ms después | 0 |

   En el anfitrión: el ataque del invitado se ve ~34 ms tarde (animación) y su cuerpo 200 ms detrás de la
   animación; la reacción de los enemigos a sus golpes es local (12,6 ms tras el 2922).

2. **Qué tramo domina:** la **presentación**, no la red. El viaje completo invitado → servidor del anfitrión →
   invitado es de **~64–67 ms** (de ellos ~30 ms de Steam y ~30 ms de colas nuestras). El cuerpo de los enemigos
   llega **0,2–1,3 s** tarde: entre 3 y 20 veces más. Segundo: los 200 ms con que el anfitrión ve al invitado
   (esquivas que no cuentan). Tercero: el transporte.

3. **Causa raíz [V mecanismo + E magnitud]:** los enemigos solo se simulan en el cliente del anfitrión; en el
   invitado son actores en **modo 2**, que solo interpolan un búfer de muestras de red con un retardo `D` adaptado
   a la cadencia (`remote_interp`). La IA manda ~1,5–2 muestras/s. `combat_follow` (S10) añadió un flujo
   sintético, pero **su forma de muestrear choca con el controlador de `D`**:
   - arranca cuando el enemigo ya ataca o recibe del invitado, cuando su `D` ya vale ~1,1 s por las muestras
     escasas de antes (el 2984 de cambio de objetivo solo lo adelanta 24 ms p50);
   - `remote_interp` baja `D` a 0,25 ms por ms: de 1,1 s a 0,2 s tarda ~4 s;
   - el umbral de «quieto» de **1 cm** y el **keepalive de 500 ms** producen huecos de 0,5–1,2 s entre muestras que
     el juego **sí** inserta (se movieron más de 0,1 mm), y esos huecos vuelven a subir `D` en mitad del combate;
   - el corte «sin muestra real en 3 s», el tope de 8 y los 25 m dejan **~38 % del tiempo de combate cercano**
     (≤ 10 m) sin flujo (S11).

   Resultado: `D` **bimodal**, ~0,2 s cuando el flujo va bien y 0,9–1,3 s cuando no.

4. **Arreglo propuesto, uno solo («flujo de combate», §6):** en el anfitrión, `combat_follow` v2 manda un flujo
   **denso y continuo** (30 Hz, sin umbral de 1 cm, keepalive de 100 ms, sin corte de 3 s) de **todo enemigo en
   combate cerca del usuario 2**, no solo de los que ya le pegan. En el invitado, `remote_interp` da a esos actores
   una **clase de combate** (`D` = máximo de los 3 últimos huecos de llegada + 80 ms, entre 120 y 400, y bajada a
   1 ms por ms). Sin ganchos nuevos en el juego: usa los 4 mid hooks de `remote_interp` (bytes verificados, §6.4)
   y el `take_frame` de `combat_follow`. Solo la mitad del invitado, sobre el flujo de hoy, ya baja `D` en los golpes
   de 228 → 157 ms p50 y de 952 → 380 ms p90 en S11 [E sim]; con el flujo denso, `D` ≈ 130–150 ms constante [E].

5. **Un PC frente a dos:** la mitad del anfitrión (cadencia, cobertura, ancho de banda) y el coste interno del
   túnel se miden en un PC con `cdmp_peer` (§7.1). Lo que ve el invitado (`D` aplicado, tirones, sensación) y la
   red real necesitan Shadow (§7.2).

---

## 1. El camino de un golpe, tramo a tramo (código)

```
INVITADO                                                       ANFITRIÓN
hilo de juego: PostPacket 0x35C670 ──► LockFreeQueue::Push 0x143AB10 (sin señal)
NetworkThread 0x35B800: WaitForSingleObject(Q1, 0x10) 0x35B9CA ── 16 ms si nadie llama ──┐
   mid hook 0x35BAD4 (guest_pump) ► FlushSendQueue ► NwVirtualSyncSession::Send ► guest_send│
   └► g_to_host (guest.cpp)                                                              │
hilo de red del mod (mod.cpp run_network: recv con 5 ms, Sleep(1)) ► TrocLink ► Steam ───┼──► hilo de red del mod
                                                                                         │    tunnel_on_packet ► offer ► inject ► g_q
                                                                                         │    NetworkThread del cliente del anfitrión,
                                                                                         │    mid hook 0x35BAD4 (on_client_pump) ► push_request ► Q0
                                                                                         │    ServerRunThread: RecvLoop 0x27733F0, Pop(Q0, 1000 ms)
                                                                                         │      ► OnRecvPacket 0x2773C20 ► Acks a las dos sesiones
                                                                                         │    sesión 2: session2_send ► take_frame ► g_outgoing
◄── Steam ◄── TrocLink ◄── hilo de red del mod: push_outgoing (cada vuelta, ≤ ~5 ms) ◄────┘
hilo de red del mod: guest_deliver ► g_from_host
NetworkThread (cada 16 ms) ► guest_pump ► deliver_all ► OnRecvPacket 0x35CA00 ► cola ► hilo principal 0x35D0C0 (vf2 del Ack)
   2697/2991 ► 0x48BC60 / 0x48AD40 ► setInterpolatePositionData 0x11B1FD0 (búfer mc+0x3D0)
   cada frame: updateInterpolatePositionData 0x11B2420 muestra el búfer en now − D
```

Hechos de código que mandan en el presupuesto:

- **[V decomp 0x35B800 + disasm]** El NetworkThread del cliente, con servidor local, espera en el semáforo de Q1
  (servidor → cliente) con **16 ms** de tope (`0x35B9CA  BA 10 00 00 00  mov edx,0x10`, `0x35B9D2 call
  [WaitForSingleObject]`), y el punto del bombeo (`0x35BAD4`, donde `guest_pump`/`on_client_pump` entregan y donde
  corre `FlushSendQueue`) solo se alcanza tras sacar algo de Q1 o tras el tope. Un semáforo con Q1 vacía salta el
  bombeo (`DVar5 = 0xFFFFFFFF → LAB_14035bdbf`).
- **[V decomp 0x35DF50 / 0x143AB10]** `Queue::Push` hace `ReleaseSemaphore`; `LockFreeQueue::Push`, que es lo que
  usa `PostPacket`, **no**. En el anfitrión el servidor llena Q1 constantemente y su NetworkThread gira ~660
  veces/s (HL11: `turns` 272800 → 276092 en 5 s). En el invitado nadie escribe Q1: **gira cada 16 ms** (GL11:
  29765 → 30075 en 5 s = 62/s). Las salidas del invitado esperan esa vuelta y las entradas también.
- **[V decomp 0x27733F0]** El servidor saca de Q0 con semáforo (tope de 1000 ms) y procesa en el acto. **No hay
  validación ni viaje extra para el usuario 2**: sus Req pasan por el mismo `OnRecvPacket` (sesión fija de
  `[ServerNet+0x18]`), y las Ack salen en el mismo manejador para las dos sesiones.
- **[V `troc_link.cpp`, `mod.cpp`, `steam_transport.cpp`]** El túnel no tiene temporizador de lotes: `TrocLink::tick`
  vacía el `TunnelSender` en cada vuelta del hilo de red del mod (media 4,7–5,9 ms). Steam va sin Nagle
  (`kSendUnreliableNoNagle`).
- **[V `movimiento-remoto-cliente.md` §1, decomp 0x11B1FD0]** `StartActionAck` aplica la acción (animación) al
  llegar; su posición, como la de `MoveActorAck`, entra en el búfer. `setInterpolatePositionData` descarta una
  muestra con `ts ≤` la última, **vacía el búfer** si el hueco pasa de 1000 ms (2500 con `remote_interp=1`) y **no
  inserta** una muestra que se movió ≤ 1e-4 m; la lectura del retardo (`0x11B200E`) va antes de esas pruebas.
- **[V `remote_interp_logic.h`]** Para `0xB` en modo 2: `D` objetivo = `clamp(max(3 últimos huecos de muestras
  insertadas) + 150, 200, 1800)`; sube 0,5 ms/ms y **baja 0,25 ms/ms**. Los `0xA` (personajes de jugador) leen el
  valor del juego, **200 ms** (`exe+0x6CB9548`; GL11 `game delay 200 ms`).

---

## 2. Medidas

### 2.1 Transporte y colas [V]

| Medida | S11 | Otras | Fuente |
|---|---|---|---|
| Golpe del invitado: su `DoFrameEventReq` 2380 → su 2922 (un reloj) | **p10 49,5 / p50 63,5 / p90 90,8**, n = 177 | S10: 50 / 61 / 75 (n 73); S14: p50 104 (n 5) | `guest_combat.py` |
| Lo mismo para el anfitrión (servidor en memoria) | **p50 4,0**, p90 12,1 (n 7) | — | H11 |
| D1 + D2: invitado → servidor → invitado (dos relojes; el desfase se cancela) | **67,2 ms** (medianas; D1 n 208, D2 n 18227) | D2 + D3 (relevo de jugadores en los dos sentidos) 70,5 ms; deriva del reloj 1,8 ms en toda la sesión | `align.py` |
| Salto local del anfitrión (su PostPacket → el servidor → su propio cliente) | D3 − D1 ≈ **3,4 ms** | — | `align.py` |
| RTT de la sesión del mod (hilo a hilo, incluye Steam) | 31–33 ms | S14: 30 | `Net: … rtt` |
| Ping de Steam (relé) | 23–24 ms | 24–25 | `Net: … ping` |
| Bucle del hilo de red del mod | media 4,7 ms (anfitrión), 5,6–5,9 (invitado), máx. < 31 | igual | `net thread: …` |
| Invitado: espera en `g_from_host` hasta que el NetworkThread entrega | **media 8,3 ms, máx. 33,5** | S14: 8,4 / 31,0 | `in queue: … wait mean` (µs) |
| Anfitrión: espera en la cola de inyección hasta Q0 | **media 7 ms, máx. 32** | S10, S12–S16: idéntico | `inject queue: … wait mean` (con `GetTickCount64`: granularidad de 15,6 ms, ver §8.3) |
| Congelones del hilo principal del invitado durante el combate | ninguno en 19:22–19:28 (el primer `FreezeWatch` WRN es a las 19:29:21) | S10 fase 1 sí los tuvo (`p8-s10.md` §2) | GL11 |

### 2.2 Lo que hace el anfitrión [V, H11]

- **Los golpes de los enemigos sobre el invitado los decide el cliente del anfitrión:** 5 de 5 S>C 2922
  (objetivo = `0xA0100378`, atacante `0xB…`) van precedidos de la `DoFrameEventReq` 2380 **de ese enemigo**
  mandada por el cliente del anfitrión, 5,4 ms antes (p50). La prueba de impacto usa la copia del invitado en el
  cliente del anfitrión, que se pinta con el retardo de juego de `0xA` (200 ms).
- **Reacción de la IA del anfitrión a un golpe del invitado:** su `StartActionReq` 2223 del objetivo sale 12,6 ms
  (p50) después del 2922 (n 43 de 172).
- **Muestras del personaje del invitado en el anfitrión:** 18 929, huecos p50 16 ms, p90 32, p99 181. El flujo es
  denso: los 200 ms no los pide la red, son el valor del juego.

### 2.3 Lo que llega al invitado de cada enemigo [V G11]

- 2922 → primera 2991 del objetivo: **p50 32 ms** (p10 16), la reacción que lanza la IA del anfitrión.
- 2922 → primera 2697 del objetivo: p50 242 ms.
- Enemigos enganchados (regla de `combat_follow`, vista desde el invitado): 111; 1350 muestras reales y 3134
  sintéticas (las sintéticas llevan `+0x80` vec3 a 0); huecos de llegada p50 81 ms, p90 772, p99 1625.

### 2.4 El retardo `D` de los enemigos en el invitado [E: simulación con la regla de inserción verificada]

`sim_d.py` reproduce `setInterpolatePositionData` (descarta `ts` viejas, vacía con > 2500 ms, no inserta lo que se
movió ≤ 1e-4 m, registra el hueco si ≤ 5000) y el controlador de `remote_interp` (objetivo, subida 0,5 y bajada
0,25 ms/ms en pasos de 16 ms) sobre las 2697/2991 recibidas de cada enemigo enganchado:

| `D` (ms) | S11 p10 / p25 / p50 / p75 / p90 | S10 p10 / p25 / p50 / p75 / p90 |
|---|---|---|
| durante el enganche (cada 64 ms) | 200 / 200 / 226 / **921** / **1288** | 200 / 200 / **1110** / 1255 / 1313 |
| en los golpes del invitado | 226 / 227 / 228 / **417** / **952** | 200 / 367 / **599** / **1091** / **1247** |
| en el primer paquete que engancha | 200 / 200 / 200 / 648 / 1195 | 200 / 200 / 200 / 1140 / 1170 |

El log no da `D` por enemigo (solo un histograma de todos los `0xB`, sobre todo NPC de ciudad), así que la
simulación es la mejor medida disponible. Es coherente con S9 (sin `combat_follow`, «el invitado ve al enemigo donde
estaba hace 1,27 s», p50) y con los `largest delays` a 1800 ms de GL11.

### 2.5 Por qué `D` no baja: una traza (S10, enemigo `0xB0101EF7`, `enemy_trace.py`) [V paquetes, E `D`]

```
458.1–474.1  solo 2991 reales cada 0,4–1,0 s (pelea con el anfitrión)    huecos 513..1052 → D 900..1200
464.148      HIT objetivo A0100001 (el anfitrión)                         D 1097
474.945      HIT objetivo A0100363 (EL INVITADO): primer paquete que lo engancha       D 1143
475.12–476.5 2697 sintéticas cada 42–85 ms                                D 1143 → 886 (baja a 0,25 ms/ms)
477.0–477.6  sintéticas «still» (no insertadas)                           D 749 → 609
477.9–483.4  sintéticas cada 155–240 ms (se mueve < 1 cm por tick)       D 529 → 366
483.2–486.1  keepalives que se movieron unos mm: huecos 476, 768, 1157, 779 → insertados
486.821      HIT del invitado sobre él                                    D 1307
```

Cuatro mecanismos a la vez: `D` alto antes de que empiece el flujo, bajada lenta, flujo ralo al moverse despacio
(umbral de 1 cm) y keepalives de 500 ms que el juego sí inserta (umbral de 0,1 mm) y que vuelven a subir `D` justo
cuando el invitado golpea.

### 2.6 Cobertura de `combat_follow` [V G11/G10/G14 `follow_cover.py`; V HL11]

Ranuras de 250 ms durante el enganche: ¿llegó alguna sintética en los 600 ms anteriores?

| Distancia invitado-enemigo | S11 sintética / solo real / nada | S10 | S14 |
|---|---|---|---|
| ≤ 10 m | **56 % / 6 % / 38 %** | 64 / 12 / 24 % | 94 / 2 / 5 % |
| 10–25 m | 70 / 2 / 28 % | 70 / 5 / 25 % | 52 / 2 / 46 % |
| > 25 m | 5 / 34 / 61 % | 7 / 43 / 50 % | 1 / 47 / 51 % |

HL11 `CombatFollow summary` a las 20:28:38 (acumulado): candidatos 27 842, de ellos `beyond 25 m` 10 568 (38 %),
`not simulated here` 3072 (11 %); decisiones `sent` 3801, `still` 6873, `no real sample lately` 3432, `clock still`
96; `our cadence` ≤ 80 ms 2092 (55 %), 320–640 ms 1041 (27 %: los keepalives); `engaged now` hasta 10 (tope 8);
`real ts − game clock` ≤ 20 ms en 2092 de 2104 (**el reloj de la `ts` sintética es el correcto**). El hilo da
800–1000 vueltas por minuto (60–75 ms por vuelta con `Sleep(60)`): `game::find_actor` lee con `read`/`read_ptr`,
que pasan por `VirtualQuery` (`safe_memory.cpp:19`) [V código; E el coste].

---

## 3. Presupuesto por tramo (S11, p50; [V] medido, [E] repartido con las medias de los logs)

| # | Tramo | Dónde | Tiempo |
|---|---|---|---|
| 1 | Hilo de juego del invitado → su NetworkThread (salida) | `PostPacket` → `LockFreeQueue` sin señal → vuelta de 16 ms | 0–16, **≈ 8** [E: 62 vueltas/s V] |
| 2 | `guest_send` → hilo de red del mod → `TrocLink` | `g_to_host`, vuelta de ~5,7 ms | ≈ 3 [E] |
| 3 | Steam ida | relé | ≈ 12–15 [V ping 24, rtt del mod 31–33] |
| 4 | Hilo de red del anfitrión → `g_q` | `tunnel_on_packet`/`offer`/`inject` | ≈ 0–1 [E] |
| 5 | `g_q` → Q0 | `on_client_pump` en `0x35BAD4` | **≈ 7** (máx. 32) [V] |
| 6 | Servidor: Q0 → manejador → Ack a las dos sesiones | `RecvLoop 0x27733F0` | ≈ 0–1 [V código; D3−D1 = 3,4] |
| 7 | `take_frame` → `g_outgoing` → túnel | `push_outgoing` cada vuelta | ≈ 2,5 [E: vuelta 4,7 V] |
| 8 | Steam vuelta | relé | ≈ 12–15 |
| 9 | `guest_deliver` → `g_from_host` → `OnRecvPacket` | `guest_pump`, vuelta de 16 ms | **≈ 8,3** (máx. 33,5) [V] |
| = | **Golpe → confirmación** | | **63,5 medido** (1–9 suman ≈ 55–62) |
| 10 | Reacción de la IA del anfitrión al 2922 (llega al invitado como 2991) | cliente del anfitrión | **+32** [V] (12,6 de IA [V]) |
| 11 | **Cuerpo del enemigo en pantalla del invitado** | búfer de interpolación, `now − D` | **228 / 599 ms p50; 952 / 1247 p90 (S11 / S10)** [E sim] |
| 12 | Pose del invitado que usa el anfitrión para los golpes enemigos | `0xA`, 200 ms | **200** [V] + aviso 27 ms [E] |

**Domina el tramo 11.** El transporte (1–9) vale ~64 ms, de ellos ~30 ms son Steam y ~30 ms colas nuestras: dos
vueltas de 16 ms del NetworkThread del invitado (8 + 8), 7 ms de la cola de inyección y ~5 ms de los dos bucles del
mod. El tramo 12 (200 ms) explica golpes recibidos «tras esquivar».

---

## 4. Lo que no es la causa [V]

- **El servidor no trata al usuario 2 de otra manera:** misma `OnRecvPacket`, sin rondas extra (`0x27733F0`).
- **El túnel no agrupa por tiempo:** se vacía en cada vuelta (≤ ~5 ms); Steam sin Nagle; el `backlog` es 0 durante el
  combate (`Troc tunnel: … backlog 0 B`); compresión activa (`in compressed batches 39022`).
- **No falta predicción en el propio personaje del invitado:** es local (modo 1). Lo único «predecible» que espera al
  anfitrión es la reacción de los enemigos (+32 ms): no compensa el riesgo.
- **La animación de los enemigos no se interpola:** `StartActionAck` se aplica al llegar; lo que va tarde es su
  posición. El desfase animación-cuerpo es `D − ~45 ms`.
- **Congelones:** en el combate de S11 no hubo (`FreezeWatch` solo después); en S10 sí, pero ya están en `p8-s10.md`.

## 5. Causa raíz

1. **[V]** Los enemigos los simula (IA, raíz de movimiento, prueba de impacto) solo el cliente del anfitrión. En el
   invitado son actores en modo 2 (`u32[actor+0x90] != 0`, `movimiento-remoto-cliente.md` §1.1), que no simulan
   nada: interpolan muestras de red con un retardo `D`.
2. **[V S9]** La IA del anfitrión manda ~1,5–2 muestras/s en combate; con eso, `D` adaptativo sube a 1,1–1,8 s (si no,
   el enemigo se queda quieto y salta).
3. **[V traza + E sim]** `combat_follow` añade muestras, pero su política de muestreo no encaja con el controlador
   de `D` (§2.5): arranca tarde respecto al `D` acumulado, `D` baja despacio (0,25 ms/ms), el umbral de 1 cm deja
   huecos de 155–240 ms al moverse despacio, los keepalives de 500 ms que se mueven milímetros se insertan y suben
   `D`, y el corte de 3 s, el tope de 8 y el radio de 25 m dejan ~38 % del combate cercano sin flujo (S11).
4. **Consecuencia:** el cuerpo de los enemigos va 0,2–1,3 s detrás de su animación en la pantalla del invitado.
   Es «el combate con retardo»: reacciones que se ven al momento y desplazamientos (embestidas, retrocesos,
   esquivas) que llegan tarde o patinan.
5. **[V]** Aparte, el anfitrión prueba los golpes enemigos contra el invitado de hace 200 ms (tramo 12). Es el
   comportamiento nativo del MMO (cada cliente ve a los demás jugadores a 200 ms), pero aquí afecta a todos los
   golpes que recibe el invitado.

---

## 6. Arreglo: «flujo de combate» (`combat_follow` v2 + clase de combate en `remote_interp`)

Una sola idea: **mientras un enemigo pelea cerca del invitado, su posición viaja como un flujo denso y continuo, y el
invitado la muestra con un retardo que sigue a ese flujo.** Las dos mitades se necesitan: la del invitado sola sobre
el flujo de hoy mejora la mediana pero no la cola (§6.3); la del anfitrión sola deja `D` bajando a 0,25 ms/ms.

Regla del proyecto: cambio de comportamiento detrás de claves apagadas por defecto, con `probe`.

### 6.1 Anfitrión: `combat_follow` v2 (`src/mod/session2/combat_follow{.cpp,_logic.h}`)

Clave nueva `[session2] combat_follow_v2 = 0 | probe | 1` (requiere `combat_follow=1`).

| Hoy (`combat_follow_logic.h`) | v2 | Por qué |
|---|---|---|
| Enganche: 2922 con el usuario 2 o 2991 con objetivo `+0x16D` = usuario 2 (`read_out`) | **Además «en combate cerca»:** cualquier 2991/2922 escrito a la sesión 2 en el que un `0xB` tenga de objetivo o víctima a **cualquier** personaje `0xA` (también el anfitrión), si en el tick está a ≤ 35 m del usuario 2 | El flujo empieza antes del primer golpe al invitado: en S10 `0xB0101EF7` peleaba con el anfitrión 11 s antes |
| `kTickMs = 60` con `Sleep(60)` (60–75 ms reales) | **33 ms con reloj de espera de alta resolución** (`CreateWaitableTimerExW(..., CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, ...)`) y programación sin deriva (`next += 33`) | Cadencia 30 Hz |
| `pose_changed`: > 1 cm o cuaternión | **Los umbrales del juego:** > 1e-4 m (`0x11B1FD0`) o el de rotación (1,19e-7) | Sin huecos de 155–240 ms al moverse despacio |
| Keepalive 500 ms (`kKeepaliveMs`) | **100 ms**, y con la **pose idéntica bit a bit** si no se movió | El juego no inserta una muestra que no se movió (no crea hueco), pero sí llega al invitado como cadencia (§6.2) |
| `Stale`: nada si no hubo muestra real en 3 s (`kRealStaleMs`) | **Quitar el corte.** Mantener la puerta de reloj (`ClockBad`, 250 ms) y la edad del ancla (30 s) | La pose sale de la memoria del cliente del anfitrión, que es la verdad; el ancla solo hace falta para el reloj, y ya se verificó (2092/2104 a ≤ 20 ms) |
| `kMaxFollowed = 8` | **12, los más cercanos primero** | `engaged now` llegó a 10 en S11 |
| `game::find_actor` (con `VirtualQuery` por lectura) | Variante con `fast_read` (SEH, sin `VirtualQuery`), como `guest_lifecycle`'s `probe_*` | El hilo tarda 0–15 ms por vuelta en leer |

**Hilos:** todo en el hilo propio de `combat_follow` (lecturas SEH `fast_read`, sin llamar al juego; la clave del
actor se comprueba antes y después, como hoy) y en `take_frame` (try-lock, atómicos). Los sintéticos salen por
`server_push_extra` (cola del mod, solo con la cola por debajo de la mitad), igual que hoy. No toca el heap del juego.

**Coste:** ≤ 12 enemigos × 30 Hz × 240 B ≈ 86 KB/s en crudo. Con `tunnel_compress=1` (activo en S11), las 2697
consecutivas de un mismo enemigo se parecen mucho: estimo ×4–6, unos 15–20 KB/s [E]. El límite de Steam es 1 MB/s.

### 6.2 Invitado: clase de combate en `remote_interp` (`src/mod/session2/remote_interp{.cpp,_logic.h}`)

Clave nueva `[session2] remote_interp_combat = 0 | probe | 1` (requiere `remote_interp=1`).

- **Cadencia de llegada por controlador.** En `observe_insert` (`remote_interp.cpp:160`, el hook de `set pos`
  `0x11B200E`, que corre **antes** de las pruebas de `ts` y de movimiento [V decomp]), por cada muestra **más nueva**
  (se haya movido o no) se guarda el hueco de llegada en el `Slot` (`arrival_gaps[4]`, atómicos, en ms de `ts`).
  Los huecos «movidos» de hoy (`record_gap`) siguen como están.
- **Cuándo es de combate** (en `update_step`, `remote_interp_logic.h:460`): actor `0xB` y (a) los 4 últimos huecos de
  llegada ≤ 150 ms (flujo denso), **o** (b) la clave está en la tabla de enganche del invitado (≤ 32 claves,
  atómica, escrita por el hilo de red del mod en `guest_deliver` con la misma regla que `cfl::read_out`, pero con
  el personaje propio; leída en el hook como `g_pairs.rider_of`).
- **Con combate:**
  - objetivo = `clamp(max(3 últimos huecos de llegada) + 80, 120, 400)`;
  - si solo vale (b) y el flujo aún no es denso, se usa el hueco movido limitado a 300 ms;
  - subida 0,5 ms/ms, **bajada 1,0 ms/ms** (el actor se reproduce como mucho a 2× durante la recuperación);
  - el suelo de 120 ms solo vale en esta clase (hoy el suelo es `max(200, el del juego)`).
- **Sin combate:** como hoy.
- **Contadores nuevos en el resumen de 60 s:** histograma de `D` de la clase de combate, «frames sin muestra por
  delante» (`now − D` posterior a la última `ts` del búfer: el actor se detendría) y número de controladores en la
  clase.
- **Hilos:** los inserts corren en el hilo que aplica los Ack y los updates en los hilos de tareas (M5,
  `p8-impl-interp-remota.md` §3.5). Todo es atómico relajado en la tabla fija del mod, sin asignaciones, como hoy.

### 6.3 Efecto esperado

| `D` en los golpes del invitado (ms) | p50 | p75 | p90 |
|---|---|---|---|
| S11 hoy | 228 | 417 | 952 |
| S11 solo invitado (b), flujo de hoy [E sim] | **157** | 160 | **380** |
| S10 hoy | 599 | 1091 | 1247 |
| S10 solo invitado (b), flujo de hoy [E sim] | **297** | 679 | 1088 |
| Con las dos mitades [E analítico] | ~130–150 | ~150 | ≤ 200 |

La simulación «solo invitado» es la rama (b) con los huecos movidos limitados a 300 ms, +80, suelo 150 y tope 400,
bajada 1 ms/ms (`sim_d.py`, política `combat`). La rama (a), flujo denso por llegada (política `dense`), apenas
cambia nada con el flujo de hoy, que tiene keepalives de 500 ms: S11 p90 952 → 656 y S10 p50 599 → 467. Por eso
la mitad del anfitrión es necesaria. La cola de S10 sigue alta solo con el invitado porque el flujo empieza tarde y
ralo.
El último renglón sale de una cadencia de 33 ms más ≤ 16 ms de cuantización en la entrega del invitado (máximo de 3
huecos ~50–65 ms, + 80).

### 6.4 Direcciones del juego que usa (todas ya instaladas por `remote_interp`; bytes verificados hoy con Ghidra)

| RVA | Bytes | Instrucción | Función |
|---|---|---|---|
| `0x11B200E` | `4C 63 15 33 75 B0 05` | `movsxd r10,[exe+0x6CB9548]` | `setInterpolatePositionData` `0x11B1FD0` +0x3E |
| `0x11B2730` | `4C 63 1D 11 6E B0 05` | `movsxd r11,[exe+0x6CB9548]` | `setInterpolateRotateData` `0x11B26E0` +0x50 |
| `0x11B249A` | `48 63 05 A7 70 B0 05` | `movsxd rax,[exe+0x6CB9548]` | `updateInterpolatePositionData` `0x11B2420` +0x7A |
| `0x11B2B62` | `48 63 05 DF 69 B0 05` | `movsxd rax,[exe+0x6CB9548]` | `updateInterpolateRotateData` `0x11B2AF0` +0x72 |

Todas están en `.rsrc`, dentro de `[0x1000, 0x51EB000)`. **No hay ganchos nuevos** ni nada en `.xtls`.

### 6.5 Riesgos y criterios de aborto

- **Menos colchón ante el jitter:** con `D` 120–150 y la cadencia de 33 ms caben 3–4 muestras. Si Steam se atasca
  más de ~100 ms, el enemigo se detiene un instante (no hay extrapolación). Aborto: «frames sin muestra por delante»
  > 5 % en la clase de combate → suelo de 200.
- **Recuperación a 2×** cuando `D` baja desde 1 s: visible menos de 1 s. Si molesta, bajada de 0,5 ms/ms.
- **Ancho de banda** (§6.1): vigilar `backlog` y `gate … CONGESTED` en `Session2: Troc tunnel`.
- **Sin el corte de 3 s,** un enemigo muerto o retirado sigue hasta su 2530/2516, como hoy; uno que el anfitrión deja
  de simular sale en `not simulated here`.
- **Enganche «en combate cerca» más amplio:** más enemigos seguidos (tope de 12 y los más cercanos primero).
- **Lecturas de otro hilo,** como hoy: SEH y comprobación de clave antes y después.

---

## 7. Qué se puede medir en un PC y qué necesita dos

### 7.1 En un PC (anfitrión + `cdmp_peer`)

1. **Tests puros** (`optd_s9` o un `optd_combat` nuevo):
   - pasar por la lógica nueva de `remote_interp` las secuencias reales de S10/S11, que se pueden sacar de las
     capturas como fixtures (por ejemplo, `0xB0101EF7`). Hay que comprobar que `D` ≤ 400 menos de 1 s después del
     enganche y que, con un flujo sintético de 33 ms, `D` ≤ 160;
   - en `combat_follow_logic`: el umbral de 1e-4 m, el keepalive de 100 ms, el enganche «cerca» y que no hay
     `Stale`.
2. **Herramienta fuera del juego:** `cdmp_peer --mode interp-sim --capture <invitado.bin>`, la versión en C++ de
   `sim_d.py` con la misma cabecera de lógica. Da los percentiles de `D` con la política de hoy y con la nueva sobre
   G10/G11/G14.
3. **Mitad del anfitrión en vivo:**
   - `cdmp_peer --mode replay --script docs\research\data\login_e3lc00soak_child_112627.troc --record …`;
   - el anfitrión pelea con un campamento junto al personaje del usuario 2 aparcado (la regla «en combate cerca»
     lo cubre aunque nadie pegue al usuario 2);
   - se espera `CombatFollow summary`: ~1800 vueltas/min, `our cadence` ≤ 40 ms en ≥ 90 %, `refused 0` y ningún
     `no real sample lately`;
   - en la grabación del peer: huecos de llegada de las 2697 por enemigo p90 ≤ 50 ms (`guest_combat.py` sobre el
     `.bin` del peer).
4. **Coste interno del túnel sin Steam:** añadir sellos QPC en µs. Hoy la espera de inyección se mide con
   `GetTickCount64`, a 15,6 ms. Los sellos van en `tunnel_on_packet` → `pump_body` (entregado a Q0) → el
   `take_frame` del Ack → `push_outgoing`. Con el peer en bucle local, la suma da el coste de nuestra tubería sin red;
   hoy deberían salir ≈ 7 ms de inyección y ≈ 2–5 de bucles.

### 7.2 Con Shadow (dos PC)

1. **`D` real en el invitado:** el resumen de `RemoteInterp` con la clase de combate (histograma y «frames sin muestra
   por delante») y `largest delays` de los enemigos enganchados ≤ 400 ms. Sobre la captura nueva, `sim_d.py` /
   `interp-sim` en los golpes: p90 ≤ 300.
2. **Red real:** jitter de Steam frente a `D` 120–150, `Net: … rtt`, `backlog` y ancho de banda con 12 enemigos.
3. **Vista y sensación:**
   - vídeo de las dos pantallas con marcas `Ctrl+F12`;
   - criterio: la embestida y el retroceso del enemigo van en fase con su animación en el invitado;
   - preguntar al usuario qué notaba exactamente como «retardo» (cuerpo de los enemigos, golpes que recibe tras
     esquivar o confirmación del golpe). Este informe ordena los tres por magnitud, pero no tiene sus palabras.
4. **Protocolo:** el mismo campamento y el anfitrión fuera de la vista (`p8-s10.md` §1.5), primero
   `remote_interp_combat=probe` y después `1`.

---

## 8. Otros tramos (fuera del arreglo), por ganancia

### 8.1 Pose del invitado en el anfitrión: 200 ms (tramo 12) [V]

El anfitrión decide los golpes enemigos sobre una copia del invitado que va 200 ms atrás. Su flujo es denso (huecos
p50 16 ms, p99 181). Con un retardo propio para el **personaje del otro jugador** (`0xA` = clave del usuario 2 en el
anfitrión, o del anfitrión en el invitado) de ~100 ms mientras sus huecos p90 < 40 ms, en los mismos 4 hooks:
- la ventana de esquiva se desplaza 100 ms en lugar de 200;
- riesgo: tirones en los picos (p99 181 ms).

Es el siguiente paso si en vivo se confirma que al invitado le pegan aunque esquive. El remedio de fondo sería que el invitado
controlara a sus enemigos (traspaso de control, el modelo del MMO: `OverriedOwnershipStateTypeyAck` 2168, manejador
`0xBB1640`, `p8-s9-combate-montura.md` C2). Es de alto riesgo y está sin estudiar.

### 8.2 Las vueltas de 16 ms del NetworkThread del invitado (tramos 1 y 9) [V]

- **Ganancia:** −8 ms a la ida y −8 ms a la vuelta (~25 % del RTT).
- **Cómo:** un mid hook en **`0x35B9CF`** (`48 8B 0F` `mov rcx,[rdi]` + `FF 15 B0 FA E8 04` `call
  [WaitForSingleObject]`; ninguna referencia entra en `0x35B9CF–0x35B9D7` [V xrefs]) que, en modo remoto, ponga
  `rdx = 1`. El NetworkThread del invitado giraría a ~1 kHz con espera bloqueante: CPU despreciable.
- **En el anfitrión:** el mismo hook con `rdx = 1` solo mientras `g_q` no esté vacía quitaría ~6 de los 7 ms de
  inyección.
- **No sirve soltar el semáforo de Q1** (`ReleaseSemaphore`): con Q1 vacía, el bucle salta el bombeo
  (`LAB_14035bdbf`).

### 8.3 Medidas mal resueltas [V código]

`pump_body` (`server.cpp:1306`) mide la espera de inyección con `GetTickCount64` (pasos de 15,6 ms): media válida,
máximo cuantizado. Hay que pasarla a µs (QPC), como `gin::DeliveryStats` del invitado.

### 8.4 Fuera del mod [H]

El usuario juega el invitado por streaming de Shadow, que añade su propia latencia de entrada y de imagen a todo lo
que ve el invitado. No se puede medir desde los logs.

---

## 9. Reproducir

```
cd %TEMP%\noche-combate
py -I find_combat.py <capturas…>
py -I guest_combat.py <G11> 440 720         # §2.1 [1], §2.3
py -I host_combat.py  <H11> A0100378          # §2.2
py -I align.py <G11> <H11> A0100378 A0100001  # §2.1 D1/D2/D3
py -I sim_d.py <G11> 440 720 ; py -I sim_d.py <G10> 440 640   # §2.4, §6.3 ("combat" = la mitad del invitado)
py -I follow_cover.py <G11|G10|G14> …        # §2.6
py -I enemy_trace.py <G10> B0101EF7 440 640  # §2.5
py -I engage_start.py <H11> <G11> A0100378 -66403   # 2984 frente al primer enganche
```

Decompilados usados (servidor de Ghidra): `0x35B800` (NetworkThread), `0x35C670` (`PostPacket`), `0x143AB10`,
`0x35DF50`, `0x2773390`/`0x27733F0` (hilo del servidor), `0x11B1FD0` (inserción); desensamblado de `0x35B9B0` y de
los 4 sitios de §6.4.

---

## 10. Implementado (12 de octubre de 2026; compilado y con tests, **sin probar en el juego**)

**Claves** (`[session2]`, `0 | probe | 1`, apagadas por defecto):

- `combat_follow_v2` (anfitrión, necesita `combat_follow=1`): §6.1 tal cual. v2 tiene **su propia tabla** (con las
  peleas), cerrojo, hilo (temporizador de 33 ms) y reloj (QPC). `probe`: v1 queda exactamente como antes (su hilo con
  `Sleep(60)`, su tabla, `GetTickCount64`, `game::find_actor` y `actor_world_position`) y v2 se decide al lado, se cuenta
  y no se manda. `1`: solo corre v2 (el hilo de v1 no arranca).
- `remote_interp_combat` (invitado, necesita `remote_interp=1`): §6.2. `probe`: la `D` de la clase corre en un
  controlador sombra (`d2_fix`), se cuenta y no se aplica.
- Inis: `cdmp.p8-s7-host.ini` y `cdmp.p8-noche-local.ini` con `combat_follow_v2=probe`; `cdmp.p8-s7-shadow.ini` y
  `-fase2` con `remote_interp_combat=probe`; plan B `cdmp.p8-s12-shadow-seguro.ini` con `0`. Después: **1 en los dos
  a la vez**. Con el flujo v1, la clase sola deja `D` ≈ 380 ms frente a huecos de 0,5–1 s (abajo).

**Ficheros:** `src/mod/session2/combat_follow{.cpp,.h,_logic.h}`, `remote_interp{.cpp,.h,_logic.h}`, `guest.cpp`
(una llamada en `deliver_body`), `src/mod/config.{h,cpp}`, `mod.cpp`, `session2.cpp`, `tests/optd_combat_tests.cpp`
(test nuevo `optd_combat`) y `CMakeLists.txt`. Sin ganchos nuevos: los 4 mid hooks de §6.4 y el `take_frame` de hoy.

**Tests** (`optd_combat`, 48/48 en total):

- Anfitrión: quién pelea (2922/2991 con cualquier `0xA`), la tabla (v1 no ve las peleas; desalojo), 5 mm por tick
  (v1 «still», v2 manda), 1e-4 rad de giro, el keepalive en el tercer tick con la pose idéntica bit a bit, sin
  `Stale` a los 10 s (sí `ClockBad` y `OldAnchor`), la programación sin deriva y los 12 más cercanos. Un enemigo
  lento: huecos máximos ≤ 100 ms con v2 y ≥ 500 con v1. Los huecos de llegada con cuatro hilos a la vez
  (compare-exchange: la `ts` más nueva gana y ningún hueco es 0).
- Invitado, con la traza real de S10 (`0xB0101EF7`, 147 paquetes, t 458–492 s, regla de inserción del juego):

  | `D` (ms) | 1.er enganche (474,945) +1 s | 2.º enganche (486,821) +1 s | golpes del invitado |
  |---|---|---|---|
  | hoy | 1012 | 1063 | 1152, 1307, 1263, 1247, 967, 563, 707 |
  | clase de combate (solo invitado, flujo v1) | **164** | **380** | 695, 1291, 1099, 1035, 316, 320, 380 |

  Los tres golpes justo después del 2.º enganche siguen altos: el flujo v1 se había quedado en keepalives de 0,5–1,2 s
  y `D` había vuelto a subir; eso lo arregla la mitad del anfitrión (empieza antes y no se vuelve ralo). En `probe`,
  la `D` aplicada es la de hoy fotograma a fotograma y la sombra es exactamente la de `1`.
- Flujo sintético de 33 ms (40 ms de latencia, entrega en pasos de 16 ms): `D` 1150 → 126 ms 1 s después del
  enganche, 120 mientras se mueve, 179 parado (keepalive de 99 ms) y ningún fotograma sin muestra por delante.

**Desviaciones del diseño:**

1. Las mascotas/caballos con dueño y el caballo emparejado (`remote_interp_pair`) **nunca** entran en la clase: siguen
   a su dueño/jinete (`0xA`, 200 ms); con muestras densas irían por delante. Por eso, con la clase activa, el dueño
   (`[actor+0xA0]`) se lee aunque `remote_interp_owned_cap_ms=0` (el tope no cambia).
2. «Fotogramas sin muestra por delante» se mide contra la última `ts` **llegada** (movida o no). Aparte, «past the
   buffer's last inserted sample»: cuenta también a los enemigos parados (sus keepalives no se insertan).
3. v2 usa 35 m para todos sus candidatos (también los enganchados con el usuario 2, que en v1 son 25 m).
4. El keepalive vence a partir de 84 ms (medio tick antes) para salir en el 3.er tick (99 ms) y no en el 4.º (132).
5. El reloj de v2 es QPC (con `GetTickCount64`, a 15,6 ms, el keepalive caería en 132 ms); v1 sigue con
   `GetTickCount64`. La tabla de v2 es aparte para que sus entradas de pelea nunca desalojen las de v1 (revisión del
   12-10: con una tabla compartida, `probe` cambiaba lo que manda v1).
6. Clase solo por (b) sin hueco movido todavía: se usan los de llegada (tope 300); sin ninguno, 200 ms.
7. `remote_interp_combat` en el anfitrión: aviso y apagado.

**Qué mirar en los logs:**

- Anfitrión: `Config: P8 session 25 combat stream`, `CombatFollow v2 (probe…)` y, cada minuto, en `CombatFollow
  summary`: `fighting a character N`, `with the fights near user 2`, `v2 timer: every 33 ms, high resolution yes, late
  ticks N`; en `CombatFollow v2 probe` (contadores acumulados): `ticks` sube ~1800 por minuto, `our cadence` ≤ 40 ms
  mientras se mueven (los keepalives de 99 ms caen en ≤ 160), `keepalives`, `past the 12 nearest`. Con `1`: `refused by
  the queue` ≈ 0 y, en `Session2: Troc tunnel`, `backlog` y `CONGESTED`.
- Invitado: `RemoteInterp combat class ON`, y cada minuto `RemoteInterp summary combat`: `own character 0xA…` (si
  sale 0, la 2756 no se vio y (b) no funciona), `engaging packets`, `class frames` (dense only / engaged only / both),
  `D per class frame` (y, en `probe`, `D in force in the same frames`: la comparación directa), `frames with no sample
  ahead` (%), y los controladores en la clase con sus 4 huecos de llegada.
- Aborto (§6.5): «frames with no sample ahead» > 5 % **con `combat_follow_v2=1` en el anfitrión** → suelo de 200
  (`kCombatFloor`). Con el flujo v1 (primera sesión, anfitrión en `probe`) ese contador sale alto por diseño (≈ 20 %
  en la traza de S10): ahí no es criterio.

**Riesgos abiertos:** nada se ha probado en el juego; el coste de leer hasta 32 actores cada 33 ms (se ve en
`ticks`/min y `late ticks`); la regla «en combate cerca» toma cualquier 2991 de un `0xB` con objetivo `0xA`, y puede
incluir acciones de NPC que no son de combate (acotado por 35 m y los 12 más cercanos); ancho de banda con 12
enemigos a 30 Hz; con `D` 120 el jitter de Steam > ~70 ms detiene al enemigo un instante (no hay extrapolación).
