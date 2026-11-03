# P8 sesión 9: montar/desmontar, mascota, combate y caballo del invitado (implementación)

10 de octubre de 2026, desde `opcion-d` `a26bc4a`. Build del juego **1.0.0.2976** (PE `0x6AB28F00`, base
`0x140000000`; RVA). Diagnóstico: [p8-s9-combate-montura.md](p8-s9-combate-montura.md) (§4, §2, §1 y §6). No se
lanzó el juego ni se desplegó nada. No se tocaron `guest_seq_play*`, `guest_seq_route*` ni `observer_seq_copy*`.

- **[V]** = comprobado (capturas de la sesión 9 byte a byte, tests).
- **[E]** = deducción fuerte, falta medirla en vivo.
- **[H]** = hipótesis.

## 0. Qué hay

| Clave `[session2]` | Dónde | Valores (defecto `0`) | Qué hace |
|---|---|---|---|
| `ride_filter` | anfitrión **e** invitado | `0` / `probe` / `1` | D1: la `StartActionAck` de montar o desmontar del **otro** jugador se convierte, antes de que la vea el cliente, en la acción nativa de a pie / montado. Nunca entra en `RideOn`/`RideOff` |
| `child_reannounce` | anfitrión **e** invitado | `0` / `probe` / `1` | P1: si el cliente del invitado no puede crear un hijo del anfitrión, lo pide otra vez por el túnel; el anfitrión lo re-anuncia con `child_settle` |
| `combat_follow` | anfitrión | `0` / `probe` / `1` | C1: los enemigos que pelean con el usuario 2 le mandan `MoveActorAck` sintéticos a ~16 Hz leídos de la memoria del cliente del anfitrión |
| `guest_horse_probe` | anfitrión | `0` / `probe` | M1: mide al jinete invitado frente a su caballo y construye (sin enviarla nunca) la 2556 sintética del caballo |

Además `child_settle_ms` pasa de 1500 a **4000** en los inis.

**Inis** (`docs/research/ini/`):
- `cdmp.p8-s7-host.ini`: `child_settle_ms=4000`, `ride_filter=1`, `child_reannounce=1`, `combat_follow=1`,
  `guest_horse_probe=probe`.
- `cdmp.p8-noche-local.ini` (regresión en un PC): lo mismo, salvo `ride_filter=probe`.
- `cdmp.p8-s7-shadow.ini` y `cdmp.p8-s7-shadow-fase2.ini`: `ride_filter=1`, `child_reannounce=1`.
- Tras la revisión (§7), `ride_filter` pasa a `1` en el ini del anfitrión y en los dos del invitado.

**Ficheros:**
- Nuevos: `src/mod/session2/ride_filter{.h,.cpp,_logic.h}`, `combat_follow{.h,.cpp,_logic.h}`,
  `reannounce_logic.h`, `tests/optd_s9_tests.cpp`.
- Cambiados: `net/troc_sniffer.{h,cpp}` (rewrite opcional en `OnRecvPacket`), `game/game_errors.{h,cpp}` (anillo de
  los fallos de creación), `session2/child_hold.{h,cpp}` y `child_settle_logic.h` (re-anuncio con espera),
  `guest.{h,cpp}`, `guest_diag_worker.cpp`, `server.cpp` (`take_frame`, época), `session2.cpp` (configuración,
  paquete de control, líneas), `horse_follow{.h,.cpp,_logic.h}`, `config.{h,cpp}`, `mod.cpp`, `CMakeLists.txt`,
  `tests/optd_pets_tests.cpp`.

**Tests [V]:** `.\scripts\build.ps1` compila sin avisos nuevos.
- Con el arreglo de la revisión (§7), **39/39** sobre `opcion-d` `7ea5dba`, que ya traía 38.
- `optd_s9` es el test nuevo; `optd_pets` gana el re-anuncio de punta a punta.

## 1. D1 `ride_filter`

### 1.1 Formato y hashes [V, capturas de la sesión 9]

`StartActionAck` 2991, 725 B: `+0x000` clave | `+0x004` bloque A[0] (69 B) | `+0x049` A[1] (69 B) | `+0x08E` u8 |
`+0x08F` bloque B. El hash de la acción va en **`+0x04` de cada bloque** (payload `+0x08` y `+0x4D`); 0 = esa capa
no cambia. Padre u64 en `+0x117`, objetivo u32 en `+0x16D`.

Secuencias reales (script `ride.py` de un solo uso sobre H2 y G2):

| Quién | Ack | A0 / A1 | Padre | Método en el que observa |
|---|---|---|---|---|
| invitado (H2 2535.818) | montar | `91605616` / `26107D3C` | −1, objetivo caballo | 6 `RideOn`, Enter 0 (§4.1) |
| anfitrión (G2 2176.567) | montar | `872A6750` / `26107D3C` | −1, objetivo caballo | 6 `RideOn`, Enter 0 (log del invitado de la prueba 2) |
| invitado (H2 2545.780) | desmontar 1 | `667E3B35` / `7B6AFF57` | caballo | 4, Enter 1 |
| invitado (H2 2546.041) | desmontar 2 | `C9356CEB` / `7B6AFF57` | caballo | 7 `RideOff`, Enter 0, bloqueado 83,6 s |
| anfitrión (G2 2197.321) | desmontar | `412C2C2B` / `7B6AFF57` | caballo | 7 `RideOff`, Enter 0 |

**Novedad [V]:** el `cdmp.log` del invitado de la prueba 2 ya está en `backups\p8-sesion9-shadow\…153446\` (se
copió después del diagnóstico). Confirma la simetría que §4.3 daba como [E]: el personaje del anfitrión se queda en
`RideOff enter 0` con `412c2c2b/7b6aff57` desde 15:42:40 y **solo sale ~30 s después** con una acción de
secuenciador (`method 13`, `837487d4/35dd392f`, la que lleva la bandera `0x2D3`), es decir, cuando el anfitrión
habla con alguien. Hasta entonces, `update NO` y la divergencia de 2–7 m.

### 1.2 La sustitución

Solo para **exactamente** el personaje remoto conocido (§7, punto 4):
- **invitado:** el del anfitrión. Su clave la manda el anfitrión una vez por segundo en el estado de control 4 del
  túnel (`u8 4 | u32 clave`, leída de `[usuario del anfitrión+0xD0]+0x60`). Además, nunca el suyo propio (de su
  `MainPlayerDataStartAck` 2756). Una conexión nueva del cliente olvida las dos claves. Sin la del anfitrión no se
  toca nada.
- **anfitrión:** solo el del usuario 2 (`server_user2_player_key()`).

| Caso | Regla | A0 nuevo | A1 nuevo |
|---|---|---|---|
| montar | A0 ∈ {`0x91605616`, `0x872A6750`} | bloque nativo `26107D3C` (a pie) | bloque vacío |
| desmontar | A1 == `0x7B6AFF57` **y** padre `0xB…` | bloque nativo `F79C7424` (montado) | bloque vacío |

- Se escriben los **69 bytes enteros** de cada bloque, copiados de Acks reales de la misma captura: `26107D3C` es la
  forma de 48 de 51 (H2) y 29 de 31 (G2); `F79C7424`, la de H2 2540.633 y 3 de 4 en G2; el bloque vacío, 208 de 208.
  El resto del paquete (posición, padre, objetivo, `ts`, u8, bloque B) no cambia.
- **Resultado [V test]:** el Ack de montar queda byte a byte como el nativo de H2 2535.247 (`26107D3C`/vacío con el
  caballo de objetivo), que el cliente aplicó en método 0/4 con Enter 1. El de desmontar, como el nativo de montado de
  H2 2540.633 (`F79C7424`/vacío con el caballo de padre), aplicado en método 0 con Enter 1.
- **Efecto esperado [E]:** el actor nunca entra en el método 6/7. La altura la ponen los Move (con padre, la silla;
  después, el suelo). La siguiente acción con padre −1 (`E21887A3/94B491AB` del invitado, `DBCB5F4B/34D1B3AE` del
  anfitrión) llega con el actor en método 0.
- **Coste:** no se ven las animaciones de montar ni de desmontar.
- **Riesgo [H]:** que la acción de aterrizaje (`E21887A3`, `DBCB5F4B`) también sea de la familia de `RideOff`. La sonda
  lo dirá (§1.4) y `dismount_probe` también (método de cada tramo).

### 1.3 Dónde

- **Invitado:** `guest_deliver` (hilo de red del mod), sobre el `std::vector` del paquete, antes de encolarlo para el
  cliente. La captura del invitado (`cdmp_packets_*.bin`) guarda lo que recibe el cliente, es decir, **ya
  sustituido** con `1`.
- **Anfitrión:** los Ack del usuario 2 llegan al cliente del anfitrión por la **sesión 1**, que es nativa; el único
  sitio donde pasan por código del mod es el mid-hook del sniffer en `ClientNet::OnRecvPacket` (`0x35CA00`, el
  mismo que captura). Se añadió `net::sniffer_set_recv_rewrite`: tras la copia de la captura (que guarda lo que mandó
  el servidor) y antes de que el juego lea el paquete, una función del mod puede cambiarlo **en sitio**.
  - En ese hook no se asigna, no se bloquea y no se escribe en el log: lectura y escritura del búfer del paquete
    bajo SEH, sin `memcpy` (bucle de bytes `volatile`), contadores atómicos y un anillo sin candados.
  - **Necesita `[sniffer] enabled=1`.** Sin él, la línea `RideFilter: the host side needs the sniffer's …` y el
    lado del anfitrión queda apagado.

### 1.4 `probe`: completar la lista

No se cambia nada. Se cuentan las sustituciones que se harían y se apunta cada `StartActionAck` del personaje remoto
que nombra una clave `0xB` (objetivo o padre) o llega en los 3 s siguientes a un cambio de su padre. Cada par
`A0/A1` sale con banderas: `P` (padre `0xB`), `T` (objetivo `0xB`), `after` (tras un cambio de padre) y la clase.

**¿Está completa la lista?** No se puede afirmar:
- la de montar viene de **dos** montajes (uno por personaje);
- desmontar se decide por la capa (`7B6AFF57` con padre caballo), que cubre las tres variantes vistas;
- no hay ninguna medida de desmontar en carrera, en combate o al caerse, ni de montar en marcha.

Aun así, el coordinador decidió `1` para la sesión 10 (§7, punto 12): las sustituciones son las nativas byte a byte, y
las variantes que no estén en la lista siguen como hoy. Con `1` los pares se siguen apuntando igual que en `probe`.

### 1.5 Líneas
- `RideFilter ON (probe|1, guest: in guest_deliver | host: in ClientNet::OnRecvPacket): …`
- Anfitrión: `RideFilter: host side attached to the sniffer's OnRecvPacket hook (user 2's character only)`.
- Hasta 48 líneas: `RideFilter [probe]: qpc … | 0xA0100001 mount|dismount|window A0 0x… A1 0x… | parent … | target
  … | within 3 s of a parent change -> would be rewritten (probe)` (con `1`: `-> REWRITTEN`).
- Cada minuto: `RideFilter summary [probe] (guest|host): StartActionAcks … (remote character …, its key unknown …) |
  mount … dismount … | rewritten … | … | pairs […]`.

## 2. P1 `child_settle_ms=4000` y `child_reannounce`

### 2.1 4000 ms
Los casos buenos de las sesiones 8 y 9 se crearon a +2,77, +3,1 y +4,31 s; el fallo de la sesión 9 fue a +1,5 s. Con
4000 la mascota del anfitrión aparece en el invitado ~4 s después que su personaje.

### 2.2 Red de seguridad
1. **Invitado:** el hook de errores (`game_errors.cpp`) ya contaba los fallos desde `exe+0x8BDF7C` y guardaba el
   último valor. Ahora guarda **todos** en un anillo de 16 (sin candados). El hilo de red del mod, en cada vuelta
   (`guest_take_outgoing`), lee los nuevos:
   - valor `0x9…_B…` (dueño arriba, actor abajo);
   - como mucho **3 intentos por clave** y conexión, y nunca dos en menos de 1 s (la misma línea repetida);
   - con `1`, encola en el túnel un **paquete de control propio** (opcode 1, no es un mensaje Troc): `u8 3 | u32
     clave | u32 dueño | u8 intento`.
2. **Anfitrión:** `tunnel_on_packet` saca el opcode 1 **antes** de cualquier retención u oferta al servidor (un
   anfitrión antiguo lo descarta como «no es una Req del servidor»: es compatible).
   - Lleva su propia cuenta por clave y época (como mucho 3).
   - Con `1`, `child_settle_reannounce(K, espera)`: solo si `K` es candidato de `child_settle` (hijo de usuario, tipo
     4/5/6, personaje principal conocido) y **su dueño es el anfitrión**.
   - Mete una entrada en la tabla de `child_settle` para la sesión 2 con `not_before = ahora + 2/4/8 s` (intento
     1/2/3). La liberación es la de siempre: `NotifyAppear([sesión 2], [K])` nativo, en los hilos de `child_settle`
     (tras `StartActionReq`, cola de `NotifyAppear`, respaldo). Las reglas de soltar (fuera de alcance, despawn,
     usuario 2 ido) siguen igual.
   - Con `probe` solo escribe lo que haría.
- **Por qué es seguro [E, `c1-actor-rechazado.md` §3]:** el fallo no deja un actor a medias, así que el 2005 nuevo no
  duplica nada. Si el motor anuncia él mismo la clave, la entrada se olvida (`forget_announced`).
- **Requisito:** `child_settle_ms > 0` en el anfitrión (si no, el aviso `WARNING: child_settle_ms=0 …` y nada se
  re-anuncia).

### 2.3 Líneas
- Invitado: `Session2 guest: child_reannounce ON (1): …`, hasta 32 líneas `Session2 guest reannounce [1]:
  CreateOwnedActor failed for 0x90100000B0100002 … -> ask: attempt 1 of 3 asked of the host (sent; it waits 2000
  ms)`, y en la línea de estado `| child reannounce [1]: failures … [ask …] | asked … | last 0x…`.
- Anfitrión: `Session2 child reannounce ON (1): …`, `Session2 child reannounce [1]: user 2's client could not create
  0xB0100002 (owner user 0x90100000, its attempt 1) -> attempt 1: queued (native NotifyAppear to session 2 not before
  2000 ms)`, la liberación normal `child settle: released 0xB0100002 …` y, cada minuto, `Session2 child reannounce
  [1]: asked by user 2 …`. En `child hold …`: `| re-announces the guest asked for queued N`.

## 3. C1 `combat_follow`

### 3.1 Quién [E]
Enemigo «enganchado» = clave `0xB` que, en lo que el servidor escribe a la sesión 2 (`take_frame`):
- es objetivo o atacante de un `PostProcessAttackResult_AttackTargetAck` 2922 (`+0x00` objetivo, `+0x04` atacante)
  junto al personaje del usuario 2; o
- tiene una `StartActionAck` cuyo objetivo (`+0x16D`) es el personaje del usuario 2.

Sigue enganchado 8 s después del último paquete así. Como mucho 8 a la vez (los más recientes). Es barato
(atómicos y un *try-lock* en `take_frame`) y se verifica en la línea de cada minuto.

### 3.2 Cómo
Un hilo del mod, cada 60 ms (~16,7 Hz):
1. lee en el **cliente del anfitrión** (lecturas SEH, sin llamar al juego): el actor por clave, su byte de control
   `[actor+0xC8]` (1/2 = lo simula este cliente; 0 = remoto, p. ej. un enemigo que controle el usuario 2), la
   TransformSync (`+0xA4` quat, `+0xB4` pos, `+0xC0` sector) y la posición del personaje del usuario 2;
2. descarta: sin actor, ilegible, no simulado aquí, a más de 25 m, con padre, retenido por `observer_hold_acks`;
3. fabrica un `MoveActorAck` 2697 de 235 B con el formato de los del propio enemigo y lo encola con
   `server_push_extra` (memoria y cola del mod; nunca el heap del juego; solo con la cola por debajo de la mitad).

**El 2697 [V test, byte a byte]:** reconstruido desde la pose, la velocidad y la `ts` de un Ack real del enemigo
`0xB0101A7F` (G1 1108,066), sale idéntico salvo `+0x80` (vec3 angular, que se pone a 0). Velocidad = diferencia con el
último enviado (si hace ≤ 500 ms; si no, 0) y `+0xE7` = su módulo.

**`ts` [E]:** la del reloj del cliente del anfitrión, que es el que llevan las muestras reales del enemigo. Se ancla en
su última muestra real hacia el usuario 2: en `take_frame` se guardan su `ts` y el reloj del juego en ese momento
(`[exe+0x6C8CD68] + [exe+0x6C8CD70]/1000 + [exe+0x6D69438]`, el mismo que usa `divergence_probe`). La sintética es
`ts_ancla + (reloj ahora − reloj_ancla)`: solo cuentan las diferencias, así que el sesgo se cancela. Es estrictamente
creciente y solo sale si el reloj avanza (con el juego en pausa, no sale nada). Sin muestra real desde que se
enganchó, o con un ancla de más de 30 s, no sale nada. Quieto: una muestra cada 0,5 s.

**Comprobación en vivo:** la línea de cada minuto trae el histograma de `ts real − reloj en take_frame`. Debería
estar casi todo en ≤ 20–50 ms. Si no, el reloj elegido no es el de las `ts` y hay que revisarlo antes de dejar `1`.

**Por qué `1` en el ini del anfitrión:** no veo un riesgo evidente en una prueba en un PC.
- Si `[actor+0xC8]` no significa lo que dice el análisis estático, no sale nada (sale en `not simulated here`).
- La `ts` no puede ir hacia atrás.
- Los sintéticos usan la misma cola que `horse_follow`.

**Riesgos:**
- ~25 KB/s por cada 5 enemigos;
- un Move real con `ts` menor que el último sintético se descarta del búfer del invitado (la acción vale igual);
- un enemigo muerto sigue enganchado hasta 8 s (se manda la posición del cadáver que ve el anfitrión) **[H]**;
- solo sentido anfitrión → invitado.

### 3.3 Líneas
- `CombatFollow ON (1): the host client's enemies engaged with user 2 …`
- Cada minuto: `CombatFollow summary [1]: engaging packets … | engaged now … | real samples of engaged enemies … (cadence
  […]) | ticks …, candidates … (gone …, unreadable …, not simulated here …, beyond 25 m …, withheld …) | control
  byte 0/1/2/other … | decisions [sent …, no anchor …, anchor too old …, clock still …, has a parent …, still …] |
  made … (refused by the queue …) | our cadence [<=20 …, <=40 …, <=80 …] | … | real ts - game clock at take_frame
  (ms) [<=20 …] …`.
  - Con `1`, «our cadence» en ≤ 80 ms.
  - En el invitado, `RemoteInterp summary delays` de esos enemigos en 200–300 ms.

## 4. M1 `guest_horse_probe` (solo medida)

### 4.1 Qué mide
En `server.cpp inject` (hilo de red), para cada `MoveActorReq` 2556 del usuario 2:
- **cadencia:** la de las Req del jinete montado (la de `horse_follow`, ~36 Hz en la sesión 9) frente a las Req
  propias del caballo mientras lo montan (~1 Hz, 19 frente a 575);
- **2556 sintética:** se construye para cada Req del jinete (`make_horse_req`) y se cuenta. **No se envía nunca**;
- **la hipótesis:** la posición de la Req propia del caballo frente al Path de mundo (`+0x94`) de la última Req del
  jinete con `ts` a ≤ 100 ms (histograma en mm). En el sentido contrario era 0,000 m;
- cuántas Req propias del caballo llevan una `ts` menor que la del jinete (se descartarían en el búfer del anfitrión
  si saliera la sintética).

Funciona con `horse_follow` apagado. Sus números van en `HorseFollow …` (o solo `GuestHorseProbe …`) de cada minuto.
`guest_horse_probe=1` no existe: avisa y se queda en `probe`.

### 4.2 Diseño de la 2556 sintética (no implementado) [E]

| Paso | Qué |
|---|---|
| Dónde | `server.cpp inject`, justo después de encolar una `MoveActorReq` del usuario 2 con padre = una clave `0xB` que el usuario 2 controla (su caballo: `g_user2_keys`) |
| Qué | `make_horse_req`: clave = caballo; pos/quat = `+0x94/+0xA0` del jinete; `+0x20` = 0; Path.local = la misma pose con escala 1; padre −1; `+0x94…+0xD4` = 0; `ts` y cola (`+0xDE`) del jinete; historial (`+0xEA`, 10 × 28 B) a cero |
| Cómo | por la misma cola de inyección (`server_inject_ex`), en orden, con el mismo canal; como mucho 1 de cada 2 (~18 Hz) |
| Qué hace el servidor | la valida (dueño = usuario 2), actualiza la posición del caballo y la difunde de forma nativa (`ToNearExceptOneControl`) al cliente del anfitrión |

**Riesgos que hay que medir antes:**
- cambia el estado del servidor (la posición del caballo a ~18 Hz, que es la real);
- el historial a cero: no se sabe qué hace el servidor con él;
- la `ts` del jinete frente a las Req propias del caballo (la sonda cuenta las que irían por detrás);
- duplica tráfico de inyección mientras se cabalga (~9 KB/s).

Si la sonda da error ≤ 50 mm y pocas Req propias por detrás, se puede probar con una clave `guest_horse_follow=1`
aparte.

## 5. Reglas cumplidas
- **`.xtls`:** ni se parchea ni se engancha. No hay hooks nuevos: D1 usa el mid-hook del sniffer que ya existía en
  `ClientNet::OnRecvPacket` (cuyos bytes de cabecera ya comprueba `sniffer_install`); el resto es red y memoria
  del mod.
- **Hilos del mod:** no usan el heap del juego. Las lecturas del juego son SEH (`fast_read`) o
  `VirtualQuery`+SEH (`find_actor`).
- **En hooks** (`OnRecvPacket`, `take_frame`, el hook de errores): nada de asignaciones del juego, candados que
  bloqueen ni log síncrono. Solo *try-locks*, atómicos y anillos.
- **Lógica pura** en `ride_filter_logic.h`, `reannounce_logic.h`, `combat_follow_logic.h` y `horse_follow_logic.h`
  (`make_horse_req`), con tests en `optd_s9` sobre paquetes reales de las capturas de la sesión 9.

## 6. Qué mirar en la sesión 10
1. **Copiar siempre el `cdmp.log` del invitado.**
2. **D1 (`probe`):**
   - en los dos PCs, las líneas `RideFilter [probe]` de cada montar y desmontar: ¿todas las de montar y desmontar
     caen en `mount`/`dismount`?
   - ¿qué pares salen con `after` justo después? Probar a montar en marcha, desmontar corriendo y caer del caballo.
   - Si la lista está completa, `ride_filter=1` y comprobar con `dismount_probe` que ya no hay `method 6/7 … enter 0`.
3. **P1:** HL `child settle: … released 0xB0100002 … after ~4000 ms` y GL sin `Actor 생성에 실패`. Si falla, GL
   `Session2 guest reannounce [1]: … ask` y HL `child reannounce … queued` y su liberación 2 s después.
4. **C1:**
   - HL `CombatFollow summary`: `engaged now` > 0 en combate, `control byte` con 1/2, `made` creciendo, `our cadence`
     ≤ 80 ms, `real ts - game clock` ≤ 50 ms;
   - GL: `largest delays` de esos enemigos ≤ 300 ms;
   - a ojo: el enemigo ya no «patina» 2–5 m después del golpe.
5. **M1:** `GuestHorseProbe`: error en mm (≤ 50 esperado), cadencia del jinete ~36 Hz frente a ~1 Hz del caballo.

## 7. Correcciones tras la revisión (sobre `78e4eca`, en `opcion-d` `7ea5dba`)

| # | Hallazgo | ¿Cierto? | Arreglo |
|---|---|---|---|
| 1 | El anfitrión saca cualquier opcode 1 del túnel | Sí | Solo la trama exacta: opcode 1, slot −1, longitud 10 y estado 3 (`ran::is_reannounce_frame`). Ningún mensaje Troc usa el opcode 1: el más bajo de la tabla de la build es 1002 (`troc_messages_2976.csv`). Cualquier otra trama con opcode 1 sigue el camino normal y `offer()` la descarta (no es una Req del servidor), contada |
| 2 | El anfitrión apunta el intento antes de saber si la clave es un hijo suyo | Sí | `accept_peek` decide sin apuntar. `child_settle_reannounce` comprueba (con `dry_run` en `probe`) y solo si es un hijo del anfitrión se hace `commit`. Nuevo tope de 16 re-anuncios por época, sumando todas las claves (`EpochCap`); el invitado aplica el mismo tope por conexión |
| 3 | En el invitado, sin sniffer no hay hook de errores | Sí | `child_reannounce` se niega a arrancar con `[sniffer] enabled=0`. Si el hook no llega a instalarse, avisa una vez y la línea de estado dice `(NO game errors hook)` |
| 4 | Invitado: «cualquier `0xA` distinto del mío» | Sí | Igualdad exacta con la clave del personaje del anfitrión (estado de control 4, §1.2). Un anfitrión antiguo no lo manda: entonces no se toca nada |
| 5 | La clave propia no se olvida al cambiar de conexión | Sí | Las dos claves se borran cuando cambia la época de la conexión del cliente. Se hace en el hilo de red del mod, el mismo que las aprende, así que no hay carrera |
| 6 | El rewrite también actuaría sobre paquetes inyectados por el mod | Sí (en teoría) | Se omite con `t_injecting` (experimento 4c: las herramientas de desarrollo, nunca el servidor) |
| 7 | La clave del actor no se vuelve a comprobar al leer la pose | Sí | Se compara `[actor+0x60]` con la clave antes y después de las lecturas, en la misma pasada SEH. Si no coincide, se descarta y se cuenta (`key changed under the read`) |
| 8 | `combat_follow_on_epoch` toma el candado SRW bloqueando | Sí | Ahora solo pone una bandera; el tick del hilo del mod vacía la tabla |
| 9 | La `ts` sintética puede quedar por debajo de una real ya enviada | Sí | Se guarda por enemigo la `ts` real más alta que fue al usuario 2, y la sintética queda siempre por encima de ella y de la última enviada. Puerta de reloj: si una muestra real se aleja más de 250 ms del reloj leído aquí, el enemigo no se sigue (`ts/clock disagree`) hasta que una muestra vuelva a coincidir; el ancla nunca se toma de una muestra así |
| 10 | Falta comprobar que el enemigo sigue vivo | Sí | `NotifyHandleDeadAck` 2530 (`+0x00` el muerto) y `RemoveActorAck` 2516 (10 B, clave en `+0x06`; 8342 de 8342 en G1) lo quitan de la tabla. Sin ninguna muestra real en 3 s (`no real sample lately`), no se fabrica nada |
| 11 | El índice del anillo de fallos no salía del `fetch_add`; el lector podía leer una ranura que se estaba reescribiendo | Sí | `SeqRing` (`game/seq_ring.h`): el índice sale del `fetch_add` del escritor, cada ranura lleva su secuencia (se borra, se escribe el valor y se pone `i+1`) y el lector nunca va más de N−1 por detrás. Una ranura a medio escribir se deja para la siguiente lectura; una ya reescrita se cuenta como perdida |
| 12 | Inis | — | `combat_follow=1` y `child_reannounce=1` en `cdmp.p8-s7-host.ini` y `cdmp.p8-noche-local.ini`. `ride_filter=1` en `cdmp.p8-s7-host.ini`, `cdmp.p8-s7-shadow.ini` y `cdmp.p8-s7-shadow-fase2.ini` (este último también recibe `child_reannounce=1`). `cdmp.p8-noche-local.ini` se queda en `ride_filter=probe`: el coordinador no lo nombró |

**Tests nuevos [V]:**
- **`optd_s9`:**
  - la regla de igualdad y el estado de control 4;
  - `peek`/`commit`, el tope por época y la trama exacta;
  - en `combat_follow`, la `ts` por encima de la real más alta, la puerta de reloj, `Stale`, quitar al muerto y la
    lectura de 2530/2516 con bytes reales de G1;
  - `SeqRing`: ventana, `max`, cursor y cuatro escritores concurrentes.
- **`optd_pets`:** `dry_run`.

**Líneas nuevas:**
- `RideFilter summary`: `guest keys: host's 0x…, own 0x…, connection resets N`.
- `CombatFollow summary`: `beyond 250 ms: not followed`, `key changed under the read`, `died/removed`, `epoch resets`,
  y las decisiones `ts/clock disagree` y `no real sample lately`.
- `Session2 child reannounce`: `other opcode-1 frames passed on`, `epoch cap`, `children of the host: probe …,
  queued …`.
