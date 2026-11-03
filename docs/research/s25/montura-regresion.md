# Montura del anfitrión vista en el invitado (S26): por qué volvió el jinete desplazado

12 de octubre de 2026, rama `opcion-d`. Build del juego **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`,
tamaño de imagen `0x173AB000`), direcciones en RVA. Mod de la sesión 26: `5F5497C1` en los dos PCs.
Las secciones 0 a 9 son la investigación: se hizo sin tocar `src/` y sin lanzar el juego. Lo implementado después
está en la sección 10, también sin lanzar el juego; la medida de un PC de §5.2 la hizo el coordinador. Scripts
desechables en `…\scratchpad\montura26\` (`rider_acks.py`: las 2991 de un jinete alrededor de cada commit, leídas del
`.bin`).

- **[V]** comprobado (línea del log, paquete de la captura o código del mod/safetyhook, citado al lado).
- **[E]** deducción fuerte que falta medir en vivo.
- **[H]** hipótesis.

## Fuentes y relojes

| Id | Fichero | Qué |
|---|---|---|
| H1 | `bin64\cdmp_20261012-193111.log` (copia en `scratchpad\montura26\`) | anfitrión, paso 1 (19:31–19:42) |
| G1 | `scratchpad\s26-shadow\cdmp_20261012-183300\cdmp_20261012-183300.log` + `cdmp_packets_20261012-183314.bin` | invitado, paso 1 |
| H2 | `bin64\cdmp_20261012-195346.log` + `cdmp_packets_20261012-195357.log` | anfitrión, paso 2 (19:53–20:23) |
| G2 | `scratchpad\s26-shadow\cdmp_packets_20261012-190442\cdmp_20261012-190423.log` + `cdmp_packets_20261012-190442.bin` | invitado, paso 2 |
| S25 | `scratchpad\s25-shadow\cdmp_20261012-054004.log` / `-060436.log` + capturas; `backups\p8-sesion25-host\cdmp_20261012-063736.log` | sesión 25, paso 1 (monturas bien) |
| S20–S22 | `scratchpad\s2{0,1,2}-shadow\` (log + `.bin` del invitado) | sesiones 20–22 |
| S23 | `backups\p8-sesion23-host\cdmp_20261011-214457.log` | solo el anfitrión (no hay logs de Shadow de la S23) |

- Reloj del invitado + 1 h ≈ reloj del anfitrión (+1,0–1,1 s): el `RideOn::Enter` local del anfitrión a las
  19:38:14,098 aparece en G1 a las 18:38:15,190.
- `capture t=` de los logs = segundos desde el primer paquete de la captura del mismo PC.
- Inis: el invitado de la S26 tiene **las mismas claves de montura que el kit de la S23** (`dist/cdmp-p8-shadow-s23.zip`
  frente a `cdmp.p8-s26-shadow.ini` de `a3b19d5`: solo añade `clock_probe`, `guest_own_save`, `remote_interp_combat`,
  `save_guard`, `weather_sync`). El anfitrión tiene `mount_sync=1 ride_dismount=native mount_seat=1 ride_hold_pos=probe
  horse_follow=1 horse_follow_reverse=1 remote_interp_pair=1` en S23, S25 y los dos pasos de la S26 (líneas `Config:`).
- El código de monturas no cambió desde la S23: `git log cc2d595..5f5497c -- mount_sync*` solo trae `b99cc37`, que
  es el propio arreglo de la S23 (el build de la S23 era `CC2D5953 + local changes`).

---

## 0. Resumen

1. **No es la interpolación.** La hipótesis «el caballo del anfitrión entra en la clase de combate del invitado o en el
   flujo de 33 ms del anfitrión» es **falsa** [V]:
   - el caballo `0xB0100003` no aparece nunca en la clase de combate (`RemoteInterp summary combat`, 24 resúmenes) y
     por código no puede entrar mientras está emparejado (`combat_class(..., excluded || pair_delay != 0, ...)`) ni
     siendo de un usuario (`owned`);
   - `combat_follow_v2` nunca lo envió (ninguna línea `CombatFollow` lo nombra);
   - su cadencia en el invitado (huecos de 29–32 ms) es la de `horse_follow`, igual que en la S25 con
     `combat_follow_v2=0` (25–31 ms);
   - el par jinete/caballo funcionó como en la S25: «D caballo = D jinete = 200 ms» en el 80 % (paso 1) y el 93 %
     (paso 2) de los fotogramas emparejados (S25: 93 % y 95 %).
2. **Lo que se vio es exactamente el defecto de antes de la S21: el jinete del anfitrión no se sentaba en el
   invitado.** Dos causas distintas, una por paso:
   - **Paso 1 — el anfitrión nunca publicó sus monturas [V].** En H1 el gancho del controlador
     `CommonVehicle::vf53 0x2228260` se **rechazó al instalar** («the hook would patch 15 bytes, 7 were verified;
     REFUSED») y con él la retirada `0x2228E40`. Sin ese gancho el anfitrión no manda `MountBegin`/`MountCommit`
     (`published Begin 0 Commit 0` toda la sesión, con 3 monturas suyas). En G1: `observer events [none]`,
     `seat [seated 0 …]`, `RideOn::Enter … NOT SEATED 1` y `ride_filter` reescribió las otras dos (15 acciones). Es el
     estado de antes de la S17/S21: jinete sin vínculo, desplazado, y con el parpadeo desde el origen en la primera.
   - **Paso 2, montura 2 — carrera en el observador [V].** El commit del anfitrión llegó al invitado 6 ms **después**
     que una acción propia del jinete que va justo antes de montar (capa 1 `0xCE35CA5D`, A0 = 0, objetivo = el
     caballo) y **antes** de que el hilo principal la despachara. `mount_sync` aplica el commit en el primer despacho
     tras su llegada, y fue el de esa acción: vinculó y sentó (vf54 9 → 4) y, 13 ms después, el manejador nativo de esa
     misma acción retiró el vínculo. Cuando llegó la acción de montar, «its Link here 0» → `FALLBACK` (reescrita como
     `ride_filter`) → montura entera sin asiento, con el jinete hasta 1,45 m fuera de la silla. La montura 1 del mismo
     paso salió bien (40,6 s sentado).
3. **Por qué se rechazó el gancho (paso 1).** En 37 logs de anfitrión distintos con `mount_sync`, el gancho de
   `0x2228260` se instaló 35 veces y se rechazó 2 (H1 y `backups\noche-1pc-E-cdmp_20261012-043104.log`). Las 2 veces
   `MountSync` instaló antes que `P8Probe` [V], es decir, `0x2228260` fue el primer gancho en línea del ejecutable.
   - safetyhook solo cae al salto de 14 bytes cuando no encuentra memoria a ±2 GB para el trampolín [V código].
   - Su búsqueda de memoria cercana (`allocate_nearby_memory`, v0.7.0) tiene dos huecos [V código]:
     - hacia arriba solo prueba el **inicio** de cada región libre, y el de la que sigue a la imagen (`0x1573AB000`) no
       está alineado a 64 KB;
     - hacia abajo se para tras el primer intento rechazado.
   - **Medido después (12-10, 21:01–21:04, en un PC):** 5 arranques seguidos con `load_diag=0` y `s1_diag=0` (sin
     `P8Probe`). En los 5, `MountSync` fue el primer gancho en línea del ejecutable (~5,5 s tras arrancar, el mismo
     momento que en H1) y se instaló. La predicción «fallará casi siempre» **no se cumple**.
   - Conclusión: es un fallo **raro** (2 de 37 + 0 de 5) que depende de cómo esté el espacio de direcciones en ese
     instante. La condición exacta **no está demostrada** (§5.3). El arreglo sigue siendo el de raíz para un fallo raro
     y aleatorio, y ahora, si vuelve a fallar, el log explica qué regiones se examinaron (§10).
4. **Arreglos de raíz (§7).**
   - (A) Búsqueda de memoria cercana correcta en safetyhook (copia parcheada de `allocator.cpp`, como ya se hace con
     `os.windows.cpp`): probar la primera dirección alineada **dentro** de cada región libre, en los dos sentidos.
     Además, `install_one` reintenta un punto que salió con el salto de 14 bytes, y un controlador sin `0x2228260` lo
     dice en voz alta (y al otro PC).
   - (B) Invariante del observador: **el vínculo tiene que existir en el despacho de cada acción propia del jinete que
     nombra a su caballo hasta su `RideOn::Enter`**. Si una acción anterior lo retiró de forma nativa, se vuelve a
     aplicar el commit (y el asiento) antes del manejador. Sin guardas nuevas que rechacen nada.
5. **Muestreo.** Las monturas del anfitrión vistas por el invitado con `mount_seat=1` son pocas: S23 1 (sin log de
   Shadow), S25 2 (bien), S26 paso 1 3 (sin controlador), S26 paso 2 2 (1 carrera). La carrera ya rozó en la S20 y la
   S21 (acción previa a 15 y 14 ms del commit, despachada antes por poco: §6.3).

---

## 1. Qué era «desplazado» y cómo se arregló (S16–S23)

- **S20 [V]:** el jinete remoto se veía fuera de la silla y parpadeaba al subir. El juego sienta al jinete con
  `ClientVehicle vf54 0xA45760` (tipo de enganche `[[CC+0x100]+8]` 4 en el hueso de la silla; `0x88EB70` deja de
  interpolarlo). El mod solo llamaba a vf53.
- **S21–S22:** una sola causa para los dos defectos: el jinete remoto no se sentaba (`p8-impl-monturas.md` §14). El
  parpadeo es el *warp* de `RideOn::Enter` desde el origen cuando entra sin vínculo/asiento.
- **S23 [V]:** `mount_seat=1` (vf54 tras el vf53 del observador, juez con `vf178 0x4FE260`), «¡Funciona perfecto!».
- **Cadena necesaria en el observador** (`mount_sync_logic.h`, `Table::plan`): el controlador engancha `0x2228260` y
  publica `MountBegin`/`MountCommit` por el túnel (estado de control 5) → el observador reproduce `0x2228260` en el
  primer despacho de una 2991 tras el commit (cualquier actor) y «como muy tarde en la 2991 propia del jinete, antes de
  su manejador» → `seat_rider` llama a vf54 (9 → 4) → la 2991 de montar entra en `RideOn::Enter` con vínculo (tipo 4).
  **Si falta el commit o el vínculo al llegar la acción de montar, se vuelve al comportamiento anterior**
  (`ride_filter` reescribe la acción, o `RideOn::Enter` sin vínculo con *warp*): eso es «desplazado como antes».

---

## 2. Las monturas del anfitrión en la S26 (pregunta 1)

### 2.1 Paso 1: el controlador sin gancho

H1, instalación [V]:

```
19:31:17.266 ERR MountSync: CommonVehicle::vf53 0x2228260 (MountBegin/MountCommit) (exe+0x2228260): the hook would patch 15 bytes, 7 were verified; REFUSED, nothing written
19:31:17.274 ERR MountSync: the CommonVehicle RideOff thunk 0x2228E40 (MountDetach) (exe+0x2228E40): the inline hook could not be prepared; REFUSED
19:31:17.278 INF P8Probe: Find 0x24314C0 hooked inline …
19:31:17.287 INF P8Probe ON (read only, host): … REFUSED: none
19:31:17.288 INF MountSync 1 (mount_sync, host): CommonVehicle::vf53 0x2228260 … REFUSED, the ClientVehicle vf53 thunk 0xA45740 … SKIPPED (needs 0x2228260), … RideOff thunk 0x2228E40 … REFUSED, ClientVehicle vf54 0xA45760 hooked, RideOn::Enter 0x681C80 hooked, …
```

| # | Anfitrión (H1) | Invitado (G1) |
|---|---|---|
| 1 | 19:38:14,098 `RideOn::Enter (local player)` A0 `0x8341B6AA`, nada publicado | 18:38:15,190 `RideOn::Enter 0xA0100001 (remote player) -> 0 | Link 0 | … attach type 9 | NOT SEATED: its warp starts at the world origin (the mount blink)` |
| 2 | 19:38:30,159 A0 `0x91605616`, nada publicado | `ride_filter` reescribe (sin `RideOn`, sin vínculo) |
| 3 | 19:39:08,081 A0 `0x91605616` → `RideOff` 19:41:26,520 | `ride_filter` reescribe |

- Resúmenes de H1 [V]: `controller: vf53 0` y `published Begin 0 Commit 0 Detach 0` desde el inicio hasta las 19:42
  (sí observó y sentó al invitado: `seated 1, released 1`).
- Resúmenes de G1 [V]: `observer events [none] | judges [none] | outcomes [none]`; `seat [seated 0, … RideOn::Enter of
  the remote rider NOT seated 1 (must be 0)]`; `RideFilter … mount 2 dismount 13 | rewritten 15 (… deferred to
  mount_sync 0 …)`; `game faults 0`.
- SeatWatch: nada que vigilar (nunca hubo asiento).

### 2.2 Paso 2: una bien, una con carrera

| # | Anfitrión (H2) | Invitado (G2) |
|---|---|---|
| 1 | 20:08:52,592 vf53 `client, PUBLISHED`, transición 1 (captura H 878,805); `RideOff` 20:09:33 | commit t=221,1710 → aplicado t=221,1786 en el despacho de **otro actor** → `MountSeat … attach type 9 -> 4 … SEATED` → 2991 de montar despachada t=221,1953 con `its Link here 0xB0100003/0` → `RideOn::Enter … attach type 4`. Liberado «47 ms after its RideOff::Enter here (40563 ms seated)». **Bien.** |
| 2 | 20:10:20,574 vf53 `PUBLISHED`, transición 2 (captura H 966,803); `RideOff` 20:10:38 | ver §2.3. **Sin asiento toda la montura.** |

### 2.3 La montura 2, paso a paso [V]

Captura del invitado (`rider_acks.py`) y G2:

```
t=309.168  llega 2991 0xA0100001 A0 0x00000000 A1 0xCE35CA5D parent -1 target 0xB0100003   (6,1 ms antes del commit)
t=309.1737 MountSync observer … MountBegin / MountCommit rider 0xA0100001 horse 0xB0100003 transition 2
t=309.1804 MountApply [1] … judge: apply -> APPLIED | Link 0 -> 0xB0100003/0 | occupant 0 -> 0xA0100001 count 0 -> 1
t=309.1805 MountSeat [1] … vf54 … *res 0 | attach type 9 -> 4, hierarchy parent 0 -> 0xB0100003 … SEATED
t=309.1805 MountSync rider action … 2991 A0 0x00000000 A1 0xCE35CA5D parent -1 target 0xB0100003 | ride kind none | its Link here 0xB0100003/0
           (su manejador nativo corre ahora)
t=309.184  llega 2991 A0 0x91605616 A1 0x26107D3C parent -1 target 0xB0100003 (montar)
t=309.1937 MountSync FALLBACK … 2991 A0 0x91605616 … ride kind mount | its Link here 0 | txn riding -> rewritten as ride_filter would (no link)
t=309.2352 MountSeat release … attach type 4 -> 9 with no dismount seen (63 ms seated) | Link 0 method 1
19:10:39.183 MountSync FALLBACK … A0 0x412C2C2B … ride kind dismount | its Link here 0 -> rewritten
```

- El vínculo estaba en el pre-despacho de la acción `0xCE35CA5D` y ya no estaba en el pre-despacho de la de montar,
  13 ms después [V]. `0x2228E40` está enganchado y no se llamó (ningún `MountDetach` del jinete), no hubo
  `RideOff::Enter` ni retirada del mod (`retired 0`) [V]. Lo retiró el propio juego al instalar esa acción [E]: es la
  retirada en línea de `vf89 → 0x397F40` («la primera acción no montada») descrita en `p8-s18-desmontar-nativo.md` §1.
- Geometría de G2 a las 19:10:37 [V]: `NOT linked here` 284 muestras, `|off the seat|` hasta 1,45 m a 6–10 m/s
  (tras la montura 1 solo había 9, las del desmonte).
- Resumen final de G2: `seat [seated 2, … released 2, … NOT seated 0]`, `fallback rewrites 2`, `game faults 0`.

### 2.4 Lado del anfitrión de la montura 2 (captura H2) [V]

```
966.743 C>S CurrentTargetChangeReq / InteractionPreemptionStepReq 2671 (objetivo 0xB0100003)
966.756 S>C InteractionPreemptionStepSuccessAck 2663
966.774 C>S InteractionDoStepReq 2720
966.775 C>S StartActionReq 2223 (A0 = 0: la acción de capa 1 de arriba)
966.788 S>C InteractionDoStepSuccessAck 2916
966.803 vf53 (MountBegin PUBLISHED) + C>S StartActionReq 2223 A0 0x91605616 (montar)
```

En la montura 1 la acción `A0 = 0` del anfitrión fue a las 878,565, **240 ms antes** del vf53 (y su objetivo era
`0xB01005E9`, no el caballo). En la 2 fueron **28 ms**: el servidor reenvió esa 2991 al invitado solo 6 ms antes del
control del túnel.

---

## 3. Interpolación: el caballo y el jinete (preguntas 1 y 2)

### 3.1 Pares (`remote_interp_pair=1`) [V]

| Log | Comienzos/fines | Fotogramas emparejados | Retardo del jinete / objetivo propio | `|D caballo − D jinete|` = 0 | 200–700 ms | Huecos del caballo montado |
|---|---|---|---|---|---|---|
| G1 (S26 p1) | 3 / 3 | 4218 | 3940 / 278 | 3369 (80 %) | 659 (16 %) | 29–32 ms (102/41/82 al frenar) |
| G2 (S26 p2) | 2 / 2 | 2592 | 2503 / 89 | 2403 (93 %) | 110 (4 %) | 29–31 ms |
| S25 `054004` | 3 / 3 | 2389 | 2281 / 108 | 2224 (93 %) | 131 (5 %) | 25–31 ms |
| S25 `060436` | 1 / 1 | 4744 | 4671 / 73 | 4523 (95 %) | 117 (2 %) | 28–93 ms (una vez 21/76/315) |

- La diferencia de G1 está en el primer minuto de cada par (249 de 957 fotogramas en 200–700 ms entre 18:37:45 y
  18:38:45): es la bajada sin saltos del caballo desde su tope de 900 ms a 200 ms (0,25 ms por ms, ~2,8 s), como se
  diseñó. Con el jinete sentado no se ve (va pegado al hueso de la silla); sin asiento añade un desfase los primeros
  ~3 s, pero no es la causa: la montura 1 del paso 2 tuvo el mismo par y se vio bien.
- El jinete (`0xA0100001`) siempre lee el retardo del juego (200 ms): las claves `0xA` no tienen retardo propio.

### 3.2 Clase de combate (`remote_interp_combat=1`, invitado) [V]

- `0xB0100003` no sale en ninguna lista «in the class now» de G1 (8 resúmenes, 32 971 fotogramas de clase) ni de G2
  (16 resúmenes, 73 299). Por código no puede: `update_step` pasa `combat->excluded || pair_delay != 0` y
  `on_delay_read` pone `cs.excluded = owned == kOwnedYes` (hay 2 controladores con dueño, `remote_interp_owned_cap_ms=900`).
- El jinete es `0xA`: `combat_class` devuelve «fuera» para todo lo que no sea `0xB`.

### 3.3 Flujo de 33 ms (`combat_follow_v2=1`, anfitrión) [V]

- Ningún `CombatFollow summary` de H1/H2 nombra `0xB0100003`; los «last» son `0xB010007C`, `0xB010153E`,
  `0xB0100002`, `0xB0101666`… (`0xB0100002` es otro actor con dueño del anfitrión, no el caballo de estas monturas).
- La cadencia de ~30 ms del caballo montado viene de `horse_follow=1` (una muestra sintética del caballo tras cada
  Move del jinete), activo también en S23 y S25.

**Conclusión:** los módulos nuevos de combate no tocaron al caballo ni al jinete del anfitrión.

---

## 4. Qué cambia frente a las sesiones buenas (pregunta 2)

| Sesión | Gancho vf53 del anfitrión | Monturas del anfitrión | Invitado: aplicado / sentado | Fallos |
|---|---|---|---|---|
| S23 | hooked (`P8Probe` antes) | 1 (`published Begin 1`) | sin log de Shadow; en vivo: funciona | — |
| S25 p1 | hooked | 2 | `APPLIED 1 / SEATED`, en los dos logs; liberado tras `RideOff` | 0 |
| **S26 p1** | **REFUSED** (`MountSync` antes que `P8Probe`) | 3 | **0 / 0**, `NOT seated 1`, `ride_filter` 15 | las 3 |
| **S26 p2** | hooked | 2 | 2 / 2; la 2.ª liberada a los 63 ms sin desmonte, `FALLBACK 2` | la 2.ª |

Además, en la S22 se aplicaron las 2 pero el asiento lo rechazó la guarda `+0x94` (el fallo de entonces); sirven para
la carrera (§6.3), no para el asiento.

No cambiaron: claves de montura de los inis, código de `mount_sync`, métricas de los pares, cadencia del caballo,
latencia de despacho del invitado (3–17 ms de llegada a despacho en S20–S26).

---

## 5. Causa raíz del paso 1: el primer gancho en línea del ejecutable falla (pregunta 3)

### 5.1 Qué pasó [V]

- `install_one` (`mount_sync.cpp`, ~l. 1625) crea el gancho con `InlineHook::create(…, StartDisabled)` y lo rechaza
  si `original_bytes().size() != pt.patch`. Para `0x2228260` esperaba 7 bytes (`mov rax,rsp; mov [rax+0x10],rbx`) y
  safetyhook quería 15 (`… mov [rax+0x20],r9b; mov [rax+0x18],r8`): eso es `ff_hook` (salto absoluto de 14 bytes).
- `InlineHook::setup` solo llama a `ff_hook` cuando `e9_hook` falla (`inline_hook.cpp` l. 181). Con estos bytes (ni
  instrucciones relativas ni indescifrables) `e9_hook` solo puede fallar en `allocator->allocate_near({target}, …)`:
  **no encontró memoria a ±2 GB de `0x142228260`**.
- `0x2228E40` es `jmp rel32`: `e9` falla igual y `ff_hook` no puede reubicar una instrucción relativa →
  «could not be prepared».
- No hay reintento: `install_thread` instala una vez. Sin `g_on_common` el controlador no publica nada en toda la sesión.

### 5.2 Cuándo pasa [V]

| Logs de anfitrión con `mount_sync` (backups + bin64, sin duplicados) | `MountSync` instala antes que `P8Probe` | vf53 |
|---|---|---|
| 35 | no (la línea de `P8Probe` va antes) | hooked |
| 2 (`193111` = S26 p1; `noche-1pc-E-…-043104`) | **sí** | **REFUSED** |
| 5 (`bin64\cdmp_20261012-210155/-210336/-210346/-210355/-210406.log`, en un PC, build `5F5497C1`, `load_diag=0`, `s1_diag=0`: sin `P8Probe`) | sí (el primero de todos) | hooked |

- En `noche-1pc-E`: vf53 rechazado a las 04:31:09,574, `P8Probe ON` a las ,576 y, después, la retirada `0x2228E40`
  sí se instaló. En H1, vf54 y el resto se instalaron tras los ganchos de `P8Probe`. En cuanto existe un bloque de
  trampolines cerca del ejecutable, todos los ganchos siguientes lo reutilizan.
- En el invitado nunca pasa: otros ganchos del ejecutable (`Session2 guest` en `0x14380B0`, `GuestSeqPlay`…) se
  instalan antes de cargar el mundo y `MountSync` ya encuentra un bloque.
- En el anfitrión, los dos hilos esperan a `WorldSystem` con `Sleep(500)` y arrancan seguidos en `mod.cpp`
  (`p8_probes_start` ~l. 987, `mount_sync_start` ~l. 1014). Desde la S25 hay más módulos arrancados entre los dos
  (`clock_probe`, `weather_sync`, `catch_seq`, `schedule_selfcheck`…), lo que hace más probable que `MountSync` gane
  [H].

### 5.3 Por qué la búsqueda falla en ese momento [V código, H en vivo]

`safetyhook` v0.7.0, `allocator.cpp`, `Allocator::allocate_nearby_memory` (sin parche en el proyecto):

- **Hacia arriba:** `for (p = desired; …; p += mbi.size)` solo llama a `VirtualAlloc(p, 64 KB)` en el **inicio** de
  cada región libre. `VirtualAlloc` con `MEM_RESERVE` redondea `p` hacia abajo a 64 KB. Si la región libre empieza
  sin alinear, ese redondeo pisa la reserva de abajo, el intento falla y se salta la región entera. La región que
  sigue a la imagen empieza en `0x140000000 + 0x173AB000 = 0x1573AB000`, **no alineada**: ese hueco, el más cercano,
  nunca se usa.
- **Hacia abajo:** salta de reserva en reserva con `AllocationBase`. Solo tiene éxito si encuentra un hueco libre
  dentro de los 2 GB bajo la imagen, y tras un intento fallido en una región libre (cuyo `AllocationBase` es 0) el
  bucle termina.
- **Un *mid hook* no depende de eso [V código]:** `MidHook::setup` reserva primero su stub con `allocate()` (sin
  dirección, `VirtualAlloc(nullptr)`), y el trampolín cercano busca primero en los bloques ya reservados. Si ese bloque
  cae a ±2 GB, el trampolín cabe ahí sin buscar. El primer punto de `P8Probe` es un *mid hook* (`owner 0x2BC8470`).
- **[H] (no demostrado; la medida de §5.2 lo debilita).** Que la zona baja estuviera llena hasta cerca de
  `0x140000000` y que solo sirviera el hueco de arriba, que la búsqueda no sabe usar. Un *mid hook* no tendría ese
  problema. Pero en los 5 arranques sin `P8Probe` el primer gancho en línea se instaló siempre, en el mismo momento
  de la carga: el mapa no es así siempre, o no lo es en ese instante.
- **[H] alternativa:** una carrera con otra reserva del proceso entre `VirtualQuery` y `VirtualAlloc` (una sola
  negativa hacia abajo bastaba para parar la búsqueda).
- **Qué se sabe [V]:** el fallo es raro (2 de 42 arranques de anfitrión con `mount_sync`), depende del espacio de
  direcciones de ese proceso en ese instante, y la búsqueda de la v0.7.0 no ve huecos que sí existen.
- **Qué cubre el arreglo (§7.1, §10):** las dos hipótesis. Si vuelve a fallar, el log dirá qué examinó la búsqueda y
  cómo estaba el mapa.

---

## 6. Causa raíz de la montura 2 del paso 2: el commit se aplica antes de una acción que lo deshace (pregunta 3)

### 6.1 El supuesto roto [V código]

`Table::plan` reclama el commit «en el primer despacho tras su Commit, de cualquier actor, y como muy tarde en la
StartActionAck propia del jinete, antes de su manejador». El diseño suponía que la 2991 propia siguiente al commit es
la de montar (S17: 2–56 ms). Hay más sitios con el mismo supuesto:

- `note_rider_parent` toma «parent −1 que nombra al caballo» como la propia acción de montar;
- la vigilancia del asiento (`on_dispatch`, `ack.target != g_seat.horse()`) solo espera una liberación «a pie» si la
  acción no nombra al caballo.

### 6.2 La realidad [V captura]

Antes de montar, el cliente que controla al jinete manda a menudo una o dos acciones propias de **capa 1** (A0 = 0)
con **objetivo = el caballo** y padre −1, y a veces la de locomoción a pie (A0 `0x26107D3C`) también con el caballo
de objetivo. Al instalarse en el observador, retiran el vínculo que el mod acaba de hacer [E: `vf89 → 0x397F40`].
Si el commit llega antes que ellas y se aplica en su despacho, la montura se queda sin vínculo.

### 6.3 Doce monturas del anfitrión en capturas del invitado (`rider_acks.py`) [V]

| Sesión / montura | Acción propia que nombra al caballo más cercana antes del commit | Llegada respecto al commit | Resultado |
|---|---|---|---|
| S20 #1 | ninguna | — | bien (reclamado en la de montar) |
| S20 #2 | A1 `0xADD6AAE1` | **−15,3 ms** | despachada antes del commit por poco: bien |
| S20 #3 | ninguna | — | bien |
| S20 #4 | A1 `0x1C683394` | −91,8 ms | bien |
| S21 #1 | ninguna | — | bien (vínculo) |
| S21 #2 | A1 `0xCE35CA5D` | **−14,1 ms** | despachada antes: bien (vínculo) |
| S22 #1 | A1 `0xF681407A`, A0 `0x25DC7FB9`/A1 `0xCE35CA5D`, A0 `0x26107D3C` | −922 a −778 ms | bien (vínculo) |
| S22 #2 | A1 `0x005D1301`, A0 `0x26107D3C`, A1 `0x0F2F5BBC` | −390 a −149 ms | bien (vínculo) |
| S25 `054022` | ninguna (la previa no nombra al caballo) | — | bien (sentado) |
| S25 `060451` | A1 `0x85898754`, `0xCE35CA5D`, `0xADD6AAE1` | −1487 a −943 ms | bien (sentado) |
| S26 p2 #1 | ninguna (previas con objetivo `0xB01005E9`) | — | bien (sentado) |
| **S26 p2 #2** | **A1 `0xCE35CA5D`** | **−6,1 ms**, despachada +6,4 ms **después** | **sin vínculo** |

- En S20–S22 el asiento no estaba (S20) o se rechazaba (S21–S22): ahí la tabla habla del vínculo.
- 3 de 12 monturas tuvieron esa acción ≤ 16 ms antes del commit. Como el invitado tarda 3–17 ms en despachar lo que
  llega, es una carrera real: dos veces se libró por poco (S20 #2, S21 #2) y en la S26 se perdió.
- Todas las acciones de montar tienen A0 ≠ 0 (`0x8341B6AA`, `0x99826678`, `0x0CA26187`, `0x875B47E6`, `0x91605616`,
  `0x872A6750`), A1 `0x26107D3C`, padre −1 y objetivo = el caballo. Pero A0 `0x26107D3C` con el caballo de objetivo
  también aparece antes de montar (S22, S25, S26 p1). Por eso no conviene decidir con listas de hashes.

---

## 7. Arreglos de raíz propuestos (pregunta 4)

### 7.1 Instalación de ganchos (paso 1)

1. **Raíz: `third_party/safetyhook-patch/allocator.cpp`**, copia de la v0.7.0 con la misma comprobación SHA-256 en
   `CMakeLists.txt` que ya protege `os.windows.cpp`. Solo cambia `allocate_nearby_memory`:
   - en cada región libre visitada (los dos sentidos), probar `a = align_up(max(base_libre, search_start), 64 KB)`
     si `a + size <= fin_libre` y `in_range(a)`;
   - hacia abajo, después de un intento fallido seguir desde la base de la región libre (`BaseAddress`), no desde
     `AllocationBase` (que en una región libre vale 0);
   - nada más: la API, los bloques y los rangos siguen igual.
2. **`install_one` (MountSync) y `p8_install_mid`:** si el gancho sale con más bytes de los verificados o «could not be
   prepared», destruirlo (no se escribió nada) y reintentar (implementado: 3 intentos, 250 ms entre ellos, §10.1). Anotar en el log el tipo de error
   de safetyhook (`InlineHook::Error::type`).
3. **Alarma, no guarda:**
   - con `mount_sync=1`, un controlador sin `0x2228260` deja un `ERR` en cada resumen y un aviso en el overlay;
   - además lo anuncia al otro PC por el túnel (un bit en el estado de control 5 o en el saludo), para que su log diga
     «el anfitrión no publica sus monturas: su jinete no se sentará aquí»;
   - esto no rechaza nada: solo hace visible lo que en la S26 pasó en silencio.
4. *(Opcional)* Un «arena» cercana: una reserva de 256 KB junto a la imagen hecha por el worker antes de arrancar los
   módulos, mantenida viva, para que ningún módulo dependa del orden. Con (1) deja de ser necesaria.

**Riesgos.**
- (1) toca una biblioteca de terceros, pero es un cambio pequeño y comprobable sin el juego (§8.1). La memoria nueva
  cae en la región que sigue a la imagen (`0x1573B0000…`), memoria normal reservada por el proceso.
- (2) no escribe nada hasta tener el gancho bueno.
- (3) es solo log y túnel.

### 7.2 Observador (paso 2): volver a vincular si el juego deshace el vínculo antes de `RideOn`

**Invariante nueva:** mientras la transacción del jinete está aplicada aquí (`Riding`) y su `RideOn::Enter` aún no
ha corrido, cada 2991 propia que nombra a su caballo (objetivo o padre = `t.horse`) debe encontrar el vínculo hecho.
Si no lo está y no hay Detach del controlador, se reaplica el commit (vf53) y el asiento (vf54) **antes de su
manejador**, como el primer intento.

- `mount_sync_logic.h`:
  - `Txn` gana `ride_on_entered`, marcado por el gancho de `RideOn::Enter` del jinete remoto (como
    `note_ride_off_enter`);
  - `Table::plan(key, now, rn, ack)` recibe padre y objetivo de la acción propia (`on_dispatch` ya los tiene leídos
    antes de planificar);
  - el caso nuevo: `t.phase == Riding && t.rider == key && !t.ride_on_entered && (ack.target == t.horse ||
    ack.parent == t.horse) && rn.known && rn.link_vehicle != t.horse && t.attempts < kMaxAttempts &&
    now - t.committed_ms < kApplyWaitMs` → `Applying`, `++attempts`, `Plan::Commit`;
  - `finish_commit` y `seat_rider` no cambian (el juez da `Apply` con vínculo 0 y ocupante 0, como la primera vez);
    `g_seat.seat` reinicia la vigilancia.
- `mount_sync.cpp`: un contador `relinks` y una línea `MountSync RELINK` en el resumen. La vigilancia del asiento
  clasifica la liberación «sin desmonte» como «retirada nativa antes de RideOn (relink)».
- *(Medir primero, solo log)* Contar cuántas veces el commit se reclama en una acción propia con A0 = 0. Si es
  frecuente, se puede aplazar el reclamo a la siguiente acción propia que nombre al caballo, para quitar el
  asiento-desasiento de ~13 ms. No hace falta para el arreglo.

**Riesgos.**
- Varias llamadas a vf53/vf54 sobre el mismo par en pocos ms. Cada una pasa por el juez y las postcondiciones de
  siempre (un fallo pone el par en cuarentena, como hoy), con un máximo de `kMaxAttempts` y solo hasta
  `kApplyWaitMs` tras el commit.
- Un montar cancelado por el controlador antes de `RideOn`: su Detach (vigilancia del vínculo) pasa la transacción a
  `DetachPending`, y ahí ya no se revincula. Mientras tanto, solo se revincula en acciones que nombran al caballo.
- Ninguna guarda nueva sobre valores sin medir: el caso nuevo solo reaplica lo que el juez ya permite.

### 7.3 Lo que no hay que tocar

- `remote_interp_combat`, `combat_follow_v2`, `remote_interp_pair` y `horse_follow`: los datos (§3) no muestran
  efecto sobre el caballo ni el jinete.
- La regla «un caballo emparejado nunca entra en la clase de combate» ya existe y se cumple.

---

## 8. Cómo verificarlo

### 8.1 En un PC, sin juego

- **Allocator** (`tests/optd_hook_tests.cpp` o un test nuevo enlazado con safetyhook):
  1. reservar (`MEM_RESERVE`, sin comprometer) los 2 GB bajo un `desired` de prueba;
  2. dejar encima una región libre que empiece en una dirección no alineada a 64 KB;
  3. con el `allocator.cpp` original, `Allocator::create()->allocate_near({desired}, 32)` debe dar
     `NO_MEMORY_IN_RANGE`;
  4. con el parcheado debe devolver `align_up(inicio, 64 KB)`;
  5. comprobar también que con un mapa normal sigue reservando a ±2 GB.
- **Lógica del observador** (`tests/optd_mount_sync_tests.cpp`), con los tiempos reales:
  - **S26 p2 #2:** Begin/Commit a 309,1737; `plan` en la 2991 propia A0 0 / objetivo caballo → `Commit` (intento 1);
    `finish_commit(Applied)`; en la de montar con `rn.link_vehicle = 0` → `Commit` otra vez (intento 2); tras
    `note_ride_on_enter` → nada más;
  - **S26 p2 #1:** reclamo en otro actor y vínculo presente en la de montar → sin revínculo;
  - **acción propia que no nombra al caballo** con vínculo 0 → sin revínculo (lo lleva la ruta «a pie»/sin evento de
    hoy);
  - **Detach recibido** → sin revínculo.

### 8.2 En un PC, con el juego (anfitrión + `cdmp_peer`)

1. **Hecho (12-10, 21:01–21:04, build `5F5497C1`):** 5 arranques con `load_diag=0` y `s1_diag=0`. Los 5 instalaron
   `0x2228260` como primer gancho en línea; la predicción [E] de entonces no se cumplió (§5.2). Con el build nuevo hay
   que repetirlo (criterios en §10.3).
2. **Hecho en el código (§10.1):** la línea `HookPrepare (… first hook prepared in this process)` con el mapa de
   ±2 GB alrededor de `0x140000000` en el primer gancho preparado de cada proceso, y otra tras cada intento rechazado.
3. La regresión de monturas de la S23: asientos nativos de NPC `9 -> 4`, `game faults 0`, autoprueba `vf178` ok.
4. **Revínculo:** con un PC no se ejercita del todo (`cdmp_peer` no publica `MountBegin`/`Commit` del usuario 2).
   Opción si se quiere:
   - añadir a `--mode replay` un `--mount-events` que mande los dos controles de estado 5 del personaje y el caballo
     del usuario 2 en un instante de la captura;
   - con una captura del invitado montando tras una acción de capa 1 (la de G2, 19:09:47), el anfitrión se convierte
     en el observador de la carrera.

### 8.3 Con Shadow

- **Anfitrión:** al menos 8 monturas variadas:
  - andando hasta el caballo y montando;
  - corriendo hacia él;
  - nada más atacar, con el arma fuera (S26: pelea antes de montar);
  - llamándolo con el silbido.
- **Log del anfitrión:**
  - `CommonVehicle::vf53 … hooked`;
  - `published Begin` igual al número de sus monturas.
- **Log del invitado:**
  - `observer events [begun N, committed N]` con N = número de monturas;
  - `seat [seated N, released N …]`, con cada liberación «after its RideOff::Enter» o «after the observer's
    retirement» (ninguna «no dismount seen»);
  - `RideOn::Enter of the remote rider NOT seated 0`;
  - `fallback rewrites 0`;
  - `relinks` mayor que 0 si la carrera ocurrió (y la montura se vio bien);
  - `NOT linked here` solo con las pocas muestras del desmonte.
- **Lo mismo en el otro sentido** (el anfitrión observa al invitado), que comparte código.

---

## 9. Lo que no se pudo medir

- El mapa de memoria del anfitrión en el instante del fallo: §5.3 queda en [H]. La próxima vez que falle, las líneas
  `HookPrepare` y la última búsqueda fallida del asignador (§10.1) lo dirán.
- Qué instrucción exacta retiró el vínculo en la montura 2 (§2.3): el gancho de `0x2228E40` no lo vio, así que fue la
  retirada en línea del manejador de acciones [E]. Habría que comprobarlo poniendo una sonda en `0x397F40` (o en
  `vf42` del jinete) en el observador.
- No hay logs de Shadow de la S23: la única montura del anfitrión de aquella sesión no tiene datos del invitado.

---

## 10. Implementado (12-10, rama `opcion-d`; commits `bf255bb` y el de la revisión, «P8 S26 mounts review: …»)

Sin desplegar y sin lanzar el juego. `.\scripts\build.ps1`: 56 de 56 pruebas.

### 10.1 Instalación de ganchos (A)

**Asignador de safetyhook.** `third_party/safetyhook-patch/allocator.cpp` (el diff es `allocator.cpp.patch`) se
compila en lugar del `src/allocator.cpp` de la v0.7.0. `CMakeLists.txt` comprueba antes el SHA-256 del original
(`dec1a65f…`), igual que con `os.windows.cpp`. Solo cambia `allocate_nearby_memory`:
- hacia abajo, un gránulo libre demasiado pequeño, o que `VirtualAlloc` rechaza, sigue con el gránulo de debajo (antes
  se paraba);
- hacia arriba, prueba los límites de 64 KB **dentro** de cada región libre, con hasta 16 rechazos por región (antes
  solo el inicio de la región);
- la ventana de ±2 GB, el orden (primero abajo) y los bloques son los mismos;
- si no encuentra nada, guarda los primeros 48 pasos de lo que examinó (`cdmp_near_last_failure`);
- (revisión de `bf255bb`, punto 3) solo toma un bloque si lo tiene **entero** al alcance de cada dirección deseada,
  principio y final. Vale para los bloques nuevos y para los huecos de bloques ya reservados. La v0.7.0 solo miraba el
  principio;
- (revisión, punto 4) hacia abajo no tiene más límite que la ventana: como mucho 32 768 gránulos; la revisión midió un
  caso peor de 32 770 pasos en 6,3 ms.

**Preparar ganchos con reintento.** `src/mod/game/hook_prepare.{h,cpp}` ofrece `prepare_inline` y `prepare_mid`,
siempre con `StartDisabled` (no se escribe nada del juego).
- Si un intento sale con el salto de 14 bytes o no se puede preparar, deja un WRN y lo repite hasta 3 veces, cada
  250 ms, en el hilo del propio módulo (nunca en `DllMain`). El WRN incluye el error de safetyhook, una sonda de la
  memoria cercana en ese momento y lo que examinó la última búsqueda fallida.
- Lo usan:
  - `MountSync` (`install_one`);
  - `P8Probe` (`p8_install_mid` y Find); con `p8_install_mid` también `RemoteInterp` y los demás que lo usan;
  - `CatchSeq`, `GuestFastTravel`, `GuestXVeto`, `OwnerFanout`;
  - `RemoteRope`, `RemoteMotion`, `UiPause`, `User2Funnels`, `WeatherSync`.
- Las líneas propias de rechazo de cada módulo no cambian: añaden el texto del error.

**Mapa del espacio de direcciones.** El primer gancho preparado del proceso deja:

```
HookPrepare (<módulo> first hook prepared in this process): address space around the exe 0x140000000..0x1573AB000 within +-2 GB: below: … nearest free 64 KB slot … | above (the image ends at 0x1573AB000, NOT on a 64 KB boundary): … | near searches failed so far N
```

Tras cada intento rechazado sale otra igual. Como mucho hay 4 por proceso. El WRN de un intento rechazado dice cuántos
bytes preparó safetyhook y cuáles, y que 14 o más es probablemente su salto de 14 bytes; ya no lo da siempre por
supuesto (revisión, punto 4).

**Medido con `bf255bb` desplegado** (el coordinador, en un PC): 5 de 5 arranques instalaron todos los ganchos con
parches de 5 bytes, 4 de ellos sin `P8Probe`. El mapa de `HookPrepare` dio:
- debajo de la imagen, ningún hueco libre de 64 KB en 2 GB;
- encima, el primero a +850…+1005 MB.

Con todo lo de abajo ocupado, el trampolín depende de los huecos de arriba, que la búsqueda de la v0.7.0 solo usaba si
empezaban alineados.

**Controlador sin `0x2228260`** (`mount_sync` ≠ 0):
- **Log:** ERR `MountSync: this PC's CommonVehicle::vf53 0x2228260 is NOT hooked: none of its own mounts is published…`
  al instalar, y ERR `MountSync: this PC still publishes NO mount …` en cada resumen.
- **Overlay:** en rojo en el panel, «Monturas: este PC no publica las suyas (gancho 0x2228260 sin instalar): el otro
  jugador te verá fuera de la silla».
- **Aviso al otro PC:** cada 15 s manda el estado de control 5 con un tipo nuevo, 4 (`MountStatus`, bandera
  `kFlagNoPublish` 0x10).
  - No cambia `kVersion`, ni la del protocolo ni la del estado 5: un build anterior lo rechaza como malformado y no
    hace nada más.
  - Ese PC deja una vez por conexión el ERR `MountSync: the other PC publishes NO mount (…): the other player's
    mounts will not be seated here …`.
  - Mientras sigan llegando avisos (hasta 45 s después del último), su panel muestra «Monturas: el otro PC no publica
    las suyas: su jinete no se sentará en tu pantalla».
- **Resumen de MountSync:** termina en `| P8 S26: relinks N (…) | this PC's mounts published: yes|NO (…) | the other
  PC's: …`.

### 10.2 Observador (B): el revínculo

- **`mount_sync_logic.h`:**
  - `RiderAction` y `relink_due`;
  - `Table::plan(key, now, rn, act)`, que devuelve `Work::relink`;
  - en `Txn`, `ride_on_entered`, `applied` y `relinks`;
  - `note_ride_on_enter`.
- **`mount_sync.cpp`:**
  - el gancho de `RideOn::Enter` marca al jinete remoto;
  - `on_dispatch` lee su vínculo en cada acción propia y pasa la acción al plan;
  - cada revínculo deja `MountSync RELINK [n] … vf53 + vf54 again before this action's handler (claim c, relink r of
    this mount)`; solo los 20 primeros, después solo se cuentan.
- **Clave `[session2] mount_relink = 0 | probe | 1`** (en los dos PCs, por defecto 0; revisión de `bf255bb`, punto 2):
  - `1` revincula. Solo actúa con `mount_sync=1`: con `mount_sync=probe` se queda en `probe`.
  - `probe` no llama a nada. Una vez por montura deja `MountSync RELINK (probe: would relink) …` y, a continuación,
    la línea `MountApply [probe] … commit … judge: <veredicto> -> probe: nothing called` con el par tal como lo
    encontraría (`Link`, ocupante, cuenta, C8).
  - Queda apagada, con un WRN `mount_relink=… is OFF`, si falta el gancho de `RideOn::Enter`, el de `RideOff::Enter`
    (sus condiciones de parada) o el del 2991 (revisión, punto 1).
  - Inis:
    - `mount_relink=probe` en las del paso 1 (`cdmp.p8-s26-host-paso1.ini` y `cdmp.p8-s26-shadow.ini`);
    - `mount_relink=1` en las del paso 2 (`cdmp.p8-s25-host-activo.ini` y `cdmp.p8-s26-shadow-paso2.ini`).
- **Un revínculo fallido se queda en su montura (revisión, punto 2):**
  - si fallan sus postcondiciones, esa transacción queda en cuarentena, pero no la pareja jinete-caballo para el
    resto de la conexión;
  - si falla su vf54, el asiento no se apaga para la sesión;
  - un fallo de la PRIMERA aplicación sigue como antes (cuarentena de la pareja, asiento apagado).
- **Condiciones (todas a la vez):**
  - la transacción se aplicó aquí y está en `Riding`;
  - es una acción propia del jinete **previa a montar**: padre −1 y su caballo como objetivo. Una acción ya montada
    (padre = el caballo) nunca cuenta (revisión, punto 1: solo lo que midió la S26);
  - `Link+0x10 == 0`, leído antes del manejador;
  - desde el commit no hubo `RideOn::Enter`, `RideOff::Enter`, Detach ni acción a pie;
  - han pasado menos de `kApplyWaitMs` (3 s) desde el Commit;
  - `attempts < kMaxAttempts` (20).
- **Quién decide:** el juez y las postcondiciones de siempre. `NotReady` cancela como antes; no hay guardas nuevas.
- **Pruebas en `tests/optd_mount_sync_tests.cpp`:**
  - `test_relink_s26_ride1`: reclamado en otro actor (t=221179) y vínculo presente en la acción de montar, así que no
    hay revínculo;
  - `test_relink_s26_ride2`: la acción previa (t=309180) reclama, el vínculo desaparece, la de montar (t=309193)
    revincula (intento 2, revínculo 1), y tras `RideOn::Enter` ya nunca;
  - `test_relink_bounds` y `test_status_event`;
  - `test_relink_review`: con `mount_relink` apagado nunca; `probe` avisa una vez por montura y no reclama nada; una
    acción con padre = el caballo no cuenta; un revínculo fallido no pone en cuarentena la pareja (y la montura
    siguiente se aplica), mientras que un primer apply fallido sí.
- **Pruebas en `tests/optd_hook_near_tests.cpp`:** los mapas L0–L4, reservados en el propio proceso de la prueba.
  - En L1 (región libre tras la imagen sin alinear) y L2 (hueco pequeño debajo), la lógica de la v0.7.0 no encuentra
    nada y la copia del proyecto da la dirección esperada.
  - En L3 (todo reservado) las dos fallan, y queda guardado lo examinado.
  - L4 (revisión, punto 3) es un objetivo 4 bytes después de un límite de 64 KB, con un único hueco libre que empieza
    en `T + 0x7FFFFFFC`. La v0.7.0 lo toma, pero el final del bloque cae fuera de los ±2 GB y los saltos del trampolín
    darían la vuelta. La copia del proyecto lo rechaza, y lo acepta para un objetivo 64 KB más arriba.

### 10.3 Cómo comprobarlo y criterios

**(1) En un PC** (anfitrión + `cdmp_peer`), con el build nuevo y un ini de anfitrión con `load_diag=0`, `s1_diag=0` y
`mount_sync=1`. Al menos 5 arranques hasta el mundo. Qué buscar con grep:

| Línea | Cuándo pasa | Cuándo falla |
|---|---|---|
| `MountSync 1 (mount_sync, host): CommonVehicle::vf53 0x2228260 (MountBegin/MountCommit) hooked` | aparece en todos los arranques | algún `REFUSED` |
| `HookPrepare (` … `first hook prepared in this process` | una por arranque (anotar `nearest free 64 KB slot` de abajo y de arriba) | — |
| `attempt 1 of 3` / `prepared on attempt` | si aparecen, el reintento actuó: pasa igual si termina en `hooked` (guardar la línea entera, con `the allocator's last failed near search`) | — |
| ERR `would patch` / `could not be prepared` | no aparece | aparece (el WRN anterior explica las regiones) |
| `MountSync: this PC's CommonVehicle::vf53 0x2228260 is NOT hooked` | no aparece | aparece |

Además, la regresión de la S23: asientos nativos de NPC `attach type 9 -> 4`, `game faults 0` y autoprueba
`vf178 … (ok)`.

**(2) Con Shadow** (anfitrión = PC del usuario, invitado = Shadow).
- **Qué se hace:** el anfitrión monta al menos 8 veces (andando hasta el caballo, corriendo hacia él, justo después
  de atacar con el arma fuera, y llamándolo con el silbido) y desmonta cada vez.
- **Anfitrión:** `CommonVehicle::vf53 … hooked`, y en el último `MountSync summary`, `published Begin N Commit N` con
  N = número de monturas.
- **`mount_relink` por paso:**
  - en el paso 1 (`probe`), cada `MountSync RELINK (probe: would relink)` es una montura que con `1` se habría
    revinculado. Su `MountApply [probe]` muestra el par y el juicio (`judge: apply` es lo esperado) y debe coincidir
    con un `FALLBACK` en esa montura;
  - en el paso 2 (`1`), esas mismas monturas deben salir sentadas.
- **Invitado, en el último `MountSync summary`:**
  - `observer events [begun N, committed N, …]` y `outcomes [APPLIED N …]`;
  - `seat [seated S, …]` con S ≥ N (un revínculo vuelve a sentar);
  - cada `MountSeat release` dice «after its RideOff::Enter here» o «after the observer's retirement». Una «with no
    dismount seen» solo vale si la sigue, en 50 ms como mucho, una `MountSync RELINK` de la misma transición;
  - `fallback rewrites 0` (ninguna línea `MountSync FALLBACK`);
  - `RideOn::Enter of the remote rider NOT seated 0`;
  - `P8 S26: mount_relink <modo>: relinks K, would relink (probe) P`, cada uno con su línea `MountSync RELINK`. Con
    `1` debe ser P = 0; con `probe`, K = 0;
  - ningún `MountSync: mount_relink=… is OFF` al instalar;
  - `this PC's mounts published: yes` y `the other PC's: no Status saying otherwise`;
  - `NOT linked here` solo con las pocas muestras del desmonte.
- **Pasa:** se cumple todo lo anterior y el usuario ve al anfitrión en la silla en las N monturas.
- **Falla:** cualquier `FALLBACK`, `NOT seated` > 0, una línea `MountSync: the other PC publishes NO mount`, o una
  montura desplazada a la vista.
- **También en el otro sentido:** el invitado monta y el anfitrión observa.

### 10.4 Riesgos que quedan

- La copia del asignador es código de terceros parcheado. Si cambia el `GIT_TAG`, la configuración se para (SHA)
  hasta rehacer la copia.
- La búsqueda nueva puede poner trampolines en sitios que antes no usaba, por ejemplo justo encima de la imagen
  (`0x1573B0000…`). Es memoria normal reservada por el proceso.
- **Revínculo:**
  - si el juego retira el vínculo en cada acción previa, vf53/vf54 se llaman varias veces seguidas sobre el mismo par
    (como mucho 20 reclamos en 3 s, solo en acciones previas a montar);
  - solo está probado en lógica pura, no en el juego. Por eso el paso 1 lo lleva en `probe`;
  - se llama a vf53 en un estado que ninguna sesión capturó: el vínculo ya retirado de forma nativa antes del RideOn.
    Si falla, solo se pierde esa montura.
- `MountSeat release … with no dismount seen` puede aparecer justo antes de un RELINK, porque el asiento se rehace.
- Un PC con un build anterior no entiende `MountStatus` (lo registra como malformado): los dos PCs deben llevar el
  mismo build.
- El fallo de instalación es raro y su causa exacta no está demostrada. El arreglo cubre las dos hipótesis, pero solo
  una nueva ocurrencia con este log lo confirmará.
