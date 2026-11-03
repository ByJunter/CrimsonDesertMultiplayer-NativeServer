# S43: el `CONTROL ABORT` de motivo 42 (prueba S42 probe, 15-10, build 2976)

Datos: `C:\Emulador CD\backups\p8-2inst-s42-probe\` (host.log, guest.log y las dos capturas). Código:
worktree `C:\Emulador CD-enemigos`, rama `opcion-d-enemigos` (`f2c960d`). Scripts en
`scratchpad\s43\` (`r42.py`, `r42pos.py`, `slots.py`, `hl.py`). No se ha compilado ni cambiado nada.

Leyenda: [V] verificado en log, captura o código; [E] evidencia fuerte; [H] hipótesis.

## Resumen

- **[V] Ninguno de los 19 logouts contados era un bandido ni un NPC muerto.** Eran NPC de horario cercanos
  (32–91 m del invitado) a los que **la IA del propio cliente del invitado** dio de baja: cada `2516` de motivo 42
  va precedido, 17–64 ms antes, de una `StartAIFunctionReq 2291` del invitado para esa misma clave.
- **[V] Los bandidos salieron con motivo 25, nunca con 42.** Los dos muertos (`2530`) salieron en el mismo instante
  de su muerte.
- **[V] El disparo fue un falso positivo.** Lo provocó un grupo nativo de 24 NPC (familia `0xB0100D9F`, recurso
  `0x3537D7CE`) que se despidió de golpe 9–16 s después de nacer. Ese grupo **no se repuso**, así que no hubo bucle.
- **[V] El aborto tuvo un coste real.** No se rearma nunca. Tras el rearmado de U-1 a las 17:14:01, los NPC nacieron
  otra vez con el anfitrión de controlador y volvió el bucle de la S31: 164 bajas de motivo 42 en 17:14, pedidas por
  el cliente del anfitrión con `logout`.
- **El arreglo propuesto: contar solo las bajas de motivo 42 que se repiten en el mismo hueco de stage.** Un hueco es
  el par (datos del horario, nodo). Esa repetición es la definición del bucle: el NPC nace, se va con 42 y la stage lo
  repone en el mismo punto. Aplicado a los logs:
  - hoy da 0 repeticiones, así que no habría abortado;
  - en el bucle de la S34 local da 68–180 por minuto, así que aborta igual que antes.
- **Filtrar por muerte (2147 HP=0 o 2530) no habría cambiado nada.** Ningún NPC contado murió, y los muertos salen
  con motivo 25.

## 1. Qué claves contaron y qué significa el motivo 42

### 1.1 Las 19 (y las 14 del último minuto) [V]

La guarda (`user2_stages.cpp:1472-1488`, `ctl_on_logout`) suma 1 a `g_ctl_r42[1]` en cada baja del servidor
(sonda O) con motivo 42 de una clave marcada `kKeyAssigned` en `g_ctl_keys`. No cuenta las bajas propias del mod
(`local_call`). El aborto se dispara con más de 10 en la ventana de unos 60 s (`npc_control_tick`, `:6287-6291`;
`ctl_abort`, `user2_stages_logic.h:980`).

Las líneas `NpcActivation [baja] ... asignado=1 ... dato=42` de host.log con qpc anterior al aborto
(`qpc 148827.317453`) son exactamente 19. Coinciden con «reason 42 19 (last minute 14)».

| # | Hora de la baja (qpc ≈) | Clave | Recurso / nodo | Vida desde su 2005 | Línea host.log |
|---|---|---|---|---|---|
| 1 | 17:09:29 (148718.57) | `0xB01008A0` (raíz, 2 hijos) | `0x59D5C885` / 0 | 28,5 s | 42925 |
| 2 | 17:09:32 (148721.80) | `0xB01008A2` | `0x59D5C885` / 8 | 31,8 s | 43070 |
| 3 | 17:09:52 (148741.82) | `0xB01008A1` | `0x59D5C885` / 4 | 51,8 s | 45778 |
| 4 | 17:09:54 (148744.14) | `0xB0100A25` | `0xDFA491AB` / 0 | 12,1 s | 46053 |
| 5 | 17:09:55 (148744.31) | `0xB0100A16` | `0xDFA491AB` / 8 | 12,8 s | 46303 |
| 6–15 | 17:11:14 (148823.54–148824.33) | `0xB0100DB5`, `D9F` (raíz), `DB4`, `DB0`, `DB2`, `DA6`, `DA0`, `DB3`, `DAA`, `DA7` | `0x3537D7CE` / 88, 0, 84, 68, 76, 28, 4, 80, 44, 32 | 9,0–9,8 s | 57049-57057, 57153 |
| 16 | 17:11:17 (148826.93) | `0xB0100DA8` | `0x3537D7CE` / 36 | 12,4 s | 57327 |
| 17 | 17:11:17 (148827.04) | `0xB0100D87` | `0x50BC12F0` / 44 | 15,5 s | 57328 |
| 18 | 17:11:17 (148827.11) | `0xB0100D86` | `0x50BC12F0` / 28 | 15,6 s | 57355 |
| 19 | 17:11:17 (148827.21) | `0xB0100D81` | `0x50BC12F0` / 16 | 16,3 s | 57356 |

- Las 14 del último minuto son las filas 6–19. Las 5 primeras quedan fuera de la ventana: 148744 < 148767.
- Después del aborto salieron otras 18 asignadas con motivo 42 (17:11:18–17:11:21, líneas 57445–57764):
  - el resto de la familia `0x3537D7CE` (24 de 24, ninguno repuesto);
  - y más del recurso `0x50BC12F0`.
- `0xB010092D` (17:09:31, motivo 42) era un viajero de User2Road (`[viajero asignado]`, línea 41286), no una clave de
  U-e, y **no** contó. Por eso hay 38 bajas de motivo 42 en la captura del invitado y 37 en `NpcActivation`.
- Todas nacieron como asignaciones de U-e. Ejemplos:
  - `User2Stages [control] ... NPC 0xB0100D87 ... controller 0x90100000 -> 0x90100065 ... schedule handle data
    0x50BC12F0 node 0x2C` (línea 54670);
  - `0xB01008A0` en la línea 39657;
  - `0xB0100D9F` en la línea 55209.

### 1.2 No eran los bandidos [V]

- Bandidos del combate de las 17:09:10, según `EnemyDamageProbe [inmunidad]`, líneas 41787–46301:
  `0xB01008E9`, `EE`, `E4`, `CE`, `CF`, `EC`, `0xB0100882` y `0xB010096A`. Todos son del recurso `0x022E5458` (o
  viajeros) y ninguno aparece en la tabla.
- Muertes: en la ventana solo hay dos `NotifyHandleDeadAck 2530`, las dos con asesino `0xA0100066` (el invitado) y
  motivo 8:
  - `0xB01008EE`, captura del anfitrión t=4583.403 (≈17:09:22.1);
  - `0xB01008CE`, t=4598.521 (≈17:09:37.1).
- Las dos salieron del servidor **con motivo 25** en ese mismo instante:
  - `NpcProbe [anfitrion baja] ... clave 0xB01008EE ... motivo 25` a las 17:09:22.046 (línea 42608);
  - `... 0xB01008CE ... motivo 25` a las 17:09:37.071 (línea 43449).
- El resto de bandidos salió entre las 17:09:57 y las 17:10:02 **con motivo 25**, al alejarse el invitado. Ejemplos:
  - `0xB01008E9` y `0xB01008E4` en las líneas 46404 y 46419;
  - `User2Road [viajero: baja] 0xB0100882 ... motivo=25 ... d(user 2)=163.9 m ... rama=distancia` (línea 46462).
- Ninguna clave de la tabla recibió daño (`EnemyDamageProbe`) ni un 2530.

### 1.3 Qué es el motivo 42 nativamente

- **[V] El servidor lo emite cuando la IA del cliente que controla al NPC se lo pide.** Lo hace `ServerAiActorComponent`
  vf69 (`0x2AC6C10`), con retorno `0x2AC6EA9`. El resumen de las 17:11:42 (host.log 60880) lo confirma: «Logout (O,
  the server's) by reason ... [reason 42 other from exe+0x2AC6EA9] x47». Son todas las bajas 42 de la sesión hasta
  ese momento (4+5+6+32) y no hay otro llamador.
  - Lo documentan `u0-resultados-s28.md:1017-1020`, `s33-npc-causas-y-diseno.md:86` y
    `user2_stages_logic.h:664-674`.
  - En cambio, el 25 es la pasada del recolector, que también retira los cadáveres. El 19 es la descarga al cargar el
    dueño (`reason_name`, `user2_stages_logic.h:385-393`).
- **[V] En los 38 casos, la petición la hizo el cliente del invitado.** En `cdmp_packets.guest_*.log`, cada `2516` S>C
  de motivo `0x2A` va precedido, 17–64 ms antes, de una `2291` C>S del invitado para esa clave (`r42.py`). Hay dos
  funciones distintas, y la disposición de la 2291 es `+0 actor | +4 grafo | +8 paquete | +0xC función | +0x10 evento`
  (`optD-combate.md:491`):

  | Bajas | Grafo | Paquete | Función | Evento | Qué es |
  |---|---|---|---|---|---|
  | 6 (17:09:29–17:09:55; incluye `0xB010092D`) | `A17A8F3E` | `1256388E` | `40BC1B1E` | `513FC4FF` | `logout` / `onstateenterfunction` (hashlittle(…, 0xC5EDE), comprobado) |
  | 32 (17:11:14–17:11:21) | `A8B491EC` | `BA7DA7C7` | `CC8BF58C` | `DD03ECC1` | evento = hashlittle(`onupdate`) [V]; nombre de la función **desconocido** [H] |

- **[V] Distancia NPC–invitado en el momento de la baja: 32–91 m en los 38 casos** (`r42pos.py`, con las últimas
  2556/2697 de cada uno). En la S31 la distancia era de 3,3–4,1 km.
- **[V] Repuesto:** `0x3537D7CE` nació una sola vez (24 claves a las 17:11:05) y no volvió a nacer. `0x59D5C885`
  nació una vez (3 claves).
- **[E] Es comportamiento nativo.** En capturas antiguas, el cliente del anfitrión controlaba NPC junto a su propio
  personaje (antes de que existiera U-e):
  - la función `CC8BF58C` aparece seguida siempre de un 42: sesión 14, ×23; sesión 25, ×67 + ×7 + ×6;
  - en la sesión 25 (`p8-sesion25-host/cdmp_packets_20261012-063747.log`) hay una ráfaga de 28 bajas de motivo 42 en
    unos 27 s (t 770,9–798,2), de NPC a 33–78 m del anfitrión. Esa ráfaga también habría disparado la guarda;
  - la S31 ya contaba el `logout` nativo: de 1 a 15 por sesión, a 23–117 m.
- **[H] Qué es `CC8BF58C`.** Por el patrón, parece el fin del horario de un grupo o una retirada programada: el grupo
  entero se va en menos de 1 s y no se repone. No se ha encontrado el nombre.
- Lo que hizo saltar el aborto: `0x3537D7CE` (10 en el mismo segundo y 14 en la ventana) más `0x50BC12F0`. Fue un
  único despido nativo de un grupo grande, a la vista del invitado, sin ningún bucle.

## 2. Origen de la guarda y efectos de su disparo

- **[V] Origen.** `git log -S "r42_last_minute > 10"` y `-S "reason-42"` en el worktree dan un único commit:
  `ae14ab6`, del 14-10-2026 a las 08:51 («P8 U-e: user2_npc_control and user2_drain_at_host_prepare (S31's guard
  loop and handover gap)»). Llegó con U-e:
  - lógica en `user2_stages_logic.h:960-993`;
  - prueba en `tests/optd_user2_stages_tests.cpp:664-668` (10 → None, 11 → Reason42).
- **[V] Motivo.** El bucle de la S31 (`u0-resultados-s28.md:1008-1032`; criterio en `:1076`):
  1. los NPC de las stages del usuario 2 nacían con el anfitrión de controlador;
  2. su cliente, a 3–4 km, pedía `logout` unos 5,2 s después;
  3. el servidor los daba de baja con motivo 42 y la stage los reponía en su puesto unos 0,42 s más tarde, con otra
     clave (828 de 1206).

  U-e da esos NPC al invitado. La guarda debía detectar que **«the guest's AI despawns them too»**
  (`user2_stages_logic.h:965`), es decir, que el bucle siguiera con el invitado de controlador. Pero cuenta cualquier
  baja de motivo 42, no la repetición.
- **[V] Efectos** (`user2_stages.cpp:6319-6358`):
  - `g_ctl_aborted.store(true)` (`:6347`), y nada vuelve a ponerlo a `false`: solo se escribe en `:3013` y `:6347`, y
    las dos veces con `true`. **U-e queda cortado para el resto del proceso**, también tras los vaciados y rearmados
    de U-1;
  - las raíces nuevas reciben `Ctl::Aborted` (`user2_stages_logic.h:715`; `user2_stages.cpp:2124`) y nacen con el
    anfitrión;
  - los miembros de familias ya admitidas también se paran (`npc_family_logic.h:525` `member_stopped`;
    `user2_stages.cpp:1973`);
  - los NPC **ya asignados se quedan** con el usuario 2, como dice el texto «the NPCs already given to user 2 stay». La
    limpieza con motivo 25 solo se hace con el aborto `Unaccepted` (`:6349`);
  - los vaciados de U-1 siguen.
- **[V] Lo que pasó después en esta sesión:**
  - veredictos `[aborted]`: 95 a las 17:11:42, 219, 314 y 664 a las 17:14:43 (resúmenes U-e);
  - 17:13:50, `[DRAINED now] its load`; 17:14:01, `[ARMED]` (host.log 71241 y 71814);
  - en 17:14 nacieron 954 NPC con `controlador 0x90100000` y 76 con `0x90100065`;
  - en 17:14 hubo **164 bajas de motivo 42**. En la captura del anfitrión hay 163 `2291 logout` (`A17A8F3E/1256388E/
    40BC1B1E`) entre t=4860 y 4920 (≈17:13:55–17:14:55), con `primera-Req-us` distinto de 0: **el bucle de la S31 del
    lado del anfitrión**;
  - lo cortó `[drain at host prepare]` a las 17:14:37 (línea 77370), cuando el anfitrión se acercó.

## 3. Arreglo de raíz propuesto (no implementado)

### 3.1 Por qué no basta con filtrar por muerte

- [V] Ninguna de las 19 claves recibió `2530` ni daño. Los muertos salen con motivo 25 (§1.2), así que un filtro «no
  contar si murió» **no habría evitado este aborto**.
- [E] Nativamente, el motivo 42 tras una muerte solo aparece en la montura de un jinete muerto
  (`pendiente-jinetes-animales.md:238, 256`). Se puede añadir como defensa, pero no es la causa.
- La causa de raíz es que la guarda mide «bajas de IA», que es comportamiento nativo y normal, en lugar de lo que la
  justificó: **la reposición en bucle del mismo puesto**.

### 3.2 Criterio: repetición del mismo hueco de stage

- **Hueco:** `(datos del horario, nodo)` del descriptor. Es el mismo valor que ya se lee para la línea `[control]`:
  `[desc+0x4E8] -> u32 +0` (datos) y `+4` (nodo), en `user2_stages.cpp:2331-2336`.
  - [V] Coincide con el `resource/node` de las líneas G1. Por ejemplo, `0xB0100D87`: `[control]` da
    `0x50BC12F0 node 0x2C` y G1 da `resource 0x50BC12F0 node 0x0000002C`.
  - [V] Las 495 líneas `[control]` de hoy tienen datos distintos de 0.
- **Regla:** una baja 42 de una clave rastreada cuenta como «bucle» solo si su hueco ya tuvo otra baja 42 de una
  clave rastreada en los últimos 60 s. El aborto `Reason42` pasa a «más de 10 bajas de bucle en un minuto». El
  recuento bruto se queda para el log.
- **Validación retrospectiva** (`slots.py`, hueco tomado de G1 o `NpcActivation [nacimiento]`):
  - hoy, en 17:09 hubo 6 bajas 42 y **0 repeticiones**; en 17:11, 32 y **0 repeticiones** [V];
  - en el bucle de la S34 local (`p8-s34npc-local/cdmp_20261014-154248.log`), de 15:57 a 16:02 hubo
    78/180/128/122/100/87 bajas 42 por minuto y **68/180/128/122/100/48 repeticiones** [V]. Abortaría en segundos;
  - en 17:14 de hoy no se puede comprobar, porque las líneas G1 se acabaron a las 17:12:14 por el presupuesto de log.
  - Las altas que se reponen tras un motivo 25 (por ejemplo, `0x50BC12F0` nodo 28 nació 6 veces) no cuentan, porque
    solo cuentan pares 42→42.
- **Hueco desconocido** (handle nulo o datos 0): conviene **fallar cerrado** y contarlo como bucle, igual que hoy. Hoy
  no ocurrió ni una vez.
- **[H] Opción:** incluir la clave de la stage (`stage_of_data`) en el hueco, para no mezclar dos instancias de los
  mismos datos en lugares distintos (en `[create]` se ven pares así, por ejemplo `0xA0F3AE60` dos veces). Para llegar
  a más de 10 repeticiones por minuto con esa mezcla harían falta muchas coincidencias, así que no es imprescindible.

### 3.3 Dónde cambiarlo (archivo:línea en `C:\Emulador CD-enemigos`, `f2c960d`)

1. **`src/mod/session2/user2_stages_logic.h`**
   - `:853-857`, `KeyTable::Slot`: añadir `std::atomic<uint32_t> slot{0}`.
   - `:809`, `put(...)`: añadir el parámetro `uint32_t slot_id = 0` y guardarlo. En la reutilización de una clave
     (`take_over`, `:863-871`) se sobrescribe con el valor nuevo.
   - Junto a `:834`, añadir `uint32_t slot_of(uint32_t key) const` (como `flags()`).
   - Después de `KeyTable` (~`:905`), añadir un `R42SlotTable<N>` sin bloqueos:
     - direccionamiento abierto sobre `slot_id` (nunca 0), con `std::atomic<uint64_t> last_us`;
     - `bool repeat(uint32_t slot, uint64_t now, uint64_t window)` devuelve true si hubo otra baja en ese hueco hace
       `window` µs o menos, y anota `now`;
     - con la tabla llena, devuelve true (fallar cerrado) y suma un contador de desbordamiento;
     - `kR42LoopWindowUs = 60'000'000` y `inline uint32_t r42_slot_id(uint32_t data, uint32_t node)` (hash; 0 =
       desconocido).
   - `:971-976`, `CtlAbortIn`: `r42_last_minute` pasa a `r42_loop_last_minute`, y se añade `r42_raw_last_minute`
     solo para el log.
   - `:980`: `if (a.r42_loop_last_minute > 10) return CtlAbort::Reason42;`.
   - `:965` y `:990`, comentario y texto: «more than 10 reason-42 logouts of the SAME stage slots in a minute (S31's
     respawn loop)».
2. **`src/mod/session2/user2_stages.cpp`**
   - `:314`: añadir `g_ctl_r42_loop[2]` (y `g_ctl_r42_noslot[2]`) y la tabla `u2s::R42SlotTable<4096> g_r42_slots`.
   - `:1482-1487`, `ctl_on_logout`: con `r42 && !(before & kKeyR42)`:
     - mantener `g_ctl_r42[i]++`;
     - leer `const uint32_t s = g_ctl_keys.slot_of(key)`;
     - si `!s || g_r42_slots.repeat(s, now, kR42LoopWindowUs)`, entonces `g_ctl_r42_loop[i]++` (y `noslot` si
       `!s`).
     - Se ejecuta en el hilo del servidor (sonda O): solo atómicos, como ahora.
   - `:2298` y `:2331-2336`: leer `h`/`data`/`node` **antes** del `put` (las lecturas son las mismas `rdp`/`rdv`
     protegidas) y pasar `u2s::r42_slot_id(data, node)` a `g_ctl_keys.put(key, kind, now, fam_unit, slot)`.
   - `:2055`, `put` de miembros de familia: leer `rdp(desc + 0x4E8)` y sus dos `u32` del descriptor del miembro (está
     en ese ámbito) y pasar el hueco.
   - `:963` y `:6265-6291`: crear un segundo anillo para `g_ctl_r42_loop[1]` y conservar el bruto.
   - `:6330`: `in.r42_loop_last_minute = g_ctl_r42_loop_minute; in.r42_raw_last_minute = g_ctl_r42_minute;`.
   - `:6350-6358`, línea `[CONTROL ABORT]`, y `:6795-6807`, resumen U-e: mostrar el bruto, el de bucle y los sin hueco.
   - Opcional: un WRN `[control r42 burst]`, limitado a uno por minuto, cuando el bruto pase de 10 sin bucle, con las
     claves, sus huecos y la distancia al personaje del usuario 2. Sirve de diagnóstico y no aborta.
3. **`src/mod/config.h:510`**: actualizar el comentario de `s2_user2_npc_control`.
4. **Documentación:** el criterio en `docs/research/s25/u0-resultados-s28.md:1076` y `s36-fase-b.md:222`.

No toca el protocolo (no hay que subir `kVersion`) ni los inis.

### 3.4 Pruebas que añadir (`tests/optd_user2_stages_tests.cpp`)

En `test_key_table()` (`:560`):
- `put` con hueco → `slot_of` lo devuelve; `mark` lo conserva; una clave reutilizada por `take_over` lleva el hueco
  nuevo; una clave no rastreada → 0.

Prueba nueva, `test_r42_slot_loop()`:
- la primera baja en un hueco → false; la segunda a 5,8 s → true; otra a más de 60 s de la anterior → false;
- tabla llena → true y el contador de desbordamiento sube.

En `test_ctl_stats_and_abort()` (`:613`, ajustar `:664-668` al campo nuevo):
- **Caso de hoy:**
  - 24 huecos distintos (`0x3537D7CE`, nodos 0..92) en menos de 1 s;
  - 8 de `0x50BC12F0` (nodos 8–44);
  - 5 previos (`0x59D5C885` 0/4/8 y `0xDFA491AB` 0/8);
  - resultado: bucle 0, bruto ≥ 14, `ctl_abort == None`.
- **Bucle S31/S34:** un mismo hueco repuesto cada 5,8 s con clave nueva y baja 42 en cada vida → 10 repeticiones en
  menos de 60 s da `None` y 11 da `Reason42`.
- **Mezcla:** 20 huecos distintos más 11 repeticiones repartidas en 3 huecos → `Reason42`.
- **Hueco desconocido:** 11 bajas 42 con hueco 0 → `Reason42` (falla cerrado, como hoy).
- **Orden:** con `bad_2005` también presente, sigue devolviendo `Bad2005`.

## 4. Pendiente / [H]

- El nombre de la función `CC8BF58C` (grafo `A8B491EC`) y si el grupo `cd_…` de `0x3537D7CE` es una comitiva que
  termina su horario. Se podría resolver con las tablas de IA del juego.
- Si los 6 `logout` de las 17:09 (a 32–50 m y con 12–52 s de vida) son el `logout` nativo cercano (1–15 por sesión
  según la S31) [E], o si tuvo algo que ver el combate cercano [H]. Con el criterio nuevo da igual: no se repitieron.
