# P8: escalada, subidas a salientes y desmontar del otro jugador (implementación)

Build **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`; RVA). Parte de
[p8-escalada-desmontar.md](p8-escalada-desmontar.md) (diagnóstico) y del hook del clon de `main`
(1b610a4, [pointclimb-remoto.md](../pointclimb-remoto.md)). No se lanzó el juego ni se desplegó nada.

Notación: **[V]** = verificado (bytes del exe en disco con capstone, o tests); **[H]** = hipótesis
pendiente de la prueba en vivo.

## 1. Qué se hizo

Un módulo nuevo, `src/mod/session2/remote_motion.{h,cpp}`, con la lógica pura en
`remote_motion_logic.h` y tests en `tests/optd_remote_motion_tests.cpp` (29 tests en total, todos
pasan). Tres claves en `[session2]`, por defecto 0, para **los dos PCs** (anfitrión e invitado):

| Clave | Valores | Qué hace |
|---|---|---|
| `remote_climb` | `0` / `probe` / `1` | Hook inline en `PointClimb::Enter` (`0x691400`). Si el original falla y el actor es el personaje de un jugador remoto (modo de control 2 según la propia función del juego `0x88E8E0`, clave `0xA01…`), devuelve 1. `probe` solo cuenta |
| `wallup_send` | `0` / `1` | Escribe 1 en el byte de datos `exe+0x6D76D5C` (puerta del emisor de `UpdateWallUpDataReq` 2851). Lo vuelve a comprobar cada segundo |
| `dismount_probe` | `0` / `1` | Sonda de solo lectura, cada 30 ms, del personaje del otro jugador: episodios de montar/desmontar (y escalada/subida), de 1 s antes a 5 s después, en un log compacto por tramos |

Arranque: `mod.cpp` llama a `session2::remote_motion_start(...)` en las dos ramas (invitado, junto a
`ui_pause_start(..., true)`; anfitrión, con `session2`). Un hilo propio: build y bytes, espera a
"jugador local en el mundo" 3 s (como `ui_pause`/`user2_funnels`), instala, y luego bucle de 30 ms
(sonda) con tareas de 1 s (byte, búsqueda de candidatos, volcado de eventos) y resumen de 60 s. La
línea `Config: session2 ...` termina en `| P8 remote motion: remote_climb '1' wallup_send true
dismount_probe true`.

INIs con las tres claves a 1: `docs/research/ini/cdmp.p8-s7-host.ini`, `cdmp.p8-s7-shadow.ini`,
`cdmp.p8-noche-local.ini`.

Decisión: **no se reutilizó** `remote_puppet.cpp: point_climb_enter`. Está atado al clon (dueño
`0x9010F001`, horse guards, instalación con `[coop]`/espejo). Un módulo aparte en `session2/` es más
limpio y no cambia el comportamiento de `main`/coop. Si alguien activa a la vez el espejo de
desarrollo (`dev clone_hotkeys`) con session2 en el anfitrión, el segundo hook en `0x691400` ve
bytes distintos y se niega (línea de error); nunca hay dos hooks.

## 2. Bytes verificados [V]

Todas las comprobaciones se hacen contra el juego en ejecución antes de escribir nada. Si algo no
coincide: `RemoteMotion: ... REFUSED` y esa pieza queda apagada. El código está en `.rsrc`
(`[0x1000, 0x51EB000)`); nada en `.xtls`.

| Qué | RVA | Bytes | Uso |
|---|---|---|---|
| `PointClimb::Enter` | `0x691400` | `4C 8B DC 49 89 5B 10 49 89 6B 18 49 89 73 20 57 48 81 EC 80 00 00 00` (23 B, 6 instrucciones, ninguna relativa a RIP) | hook inline; el parche debe acabar en un límite `{7, 11, 15, 16, 23}` (safetyhook: 7 con trampolín cercano, 15 con el absoluto) |
| Modo de control | `0x88E8E0..0x88E9B4` | cuerpo entero (212 B) | se llama desde el detour; solo lee |
| Su tabla de saltos | `0x88E9B4` | 7 × u32 (`0x88E980` ×6, `0x88E97B` para el tipo 7) | |
| Su llamada `0x17F2A50` (¿control local?) | `0x17F2A50..0x17F2A9A` + tabla `0x17F2A9C` | cuerpo entero (74 B) + 7 × u32 | |
| Puerta del emisor 2851 | `0x87DD25..0x87DD5C` | `... 44 38 35 26 90 4F 06 0F 84 17 02 00 00 80 B9 F0 00 00 00 05 ...` (55 B) | el `cmp` de `0x87DD2F` se decodifica en vivo: `0x87DD2F + 7 + 0x064F9026 = 0x6D76D5C` |
| Su llamada | `0x87D6EF..0x87D715` | 38 B (`actor tipo 1`, `byte actor+0x96 != 0`, `call 0x87DD00`) | |

El byte `exe+0x6D76D5C`:
- está en la sección RW llamada `.rdata` (`0x6893000`, vsize `0x854000`, flags `0xC0000040`: lectura
  y escritura, sin ejecución), en su cola sin datos en disco (raw acaba en `0x6BEF800`), así que vale
  0 al arrancar;
- fuera de `[0x1000, 0x51EB000)` y fuera de `.xtls` (`0x75C9000…`);
- un único lector en las secciones de código del exe (`riprefs.py 0x6D76D5C`: solo `0x87DD2F`); nada
  lo escribe. `.xtls` está cifrado por Denuvo: no se puede descartar del todo un acceso calculado
  desde ahí.

Comprobaciones en vivo antes de escribir (en este orden): bytes de la puerta y de la llamada, destino
del `cmp` = `0x6D76D5C`, sección (`check_data_byte`: no código, no `.xtls`, escribible, no
ejecutable), página (`VirtualQuery`: `MEM_COMMIT`, `PAGE_READWRITE`/`WRITECOPY`, sin `GUARD`),
lectura previa (debe ser 0 o 1; se registra), escritura con SEH, relectura = 1.

## 3. Pieza 1: `remote_climb`

Detour `climb_detour(m, same)` en el hilo del juego:
1. Llama al original (`unsafe_call`). Si devuelve ≠ 0, cuenta y lo devuelve.
2. Si falla, lee con `fast_read` (SEH, sin `VirtualQuery`): `actor = [m+0x20]`, `CC = [m+0x18]`,
   `[CC+8] == actor`, `mc = [CC+0xC8]`, `[mc+0x68] == actor`, `key = u32 [actor+0x60]`.
3. Llama a `0x88E8E0(mc)` dentro de `__try` (solo lecturas; mismas que hace el juego).
4. `climb_decide` (lógica pura, testeada): devuelve 1 **solo** con `remote_climb=1`, modo **2** y
   `(key >> 20) == 0xA01`. Modo 1, modo 3, otro valor, otra clave o algo ilegible: el original (0).
5. Contadores atómicos por resultado y un evento en un anillo sin bloqueos; el hilo del mod los
   escribe al log (máximo 400 líneas, luego solo contadores).

`Enter` solo se llama al empezar cada acción del método 18 (cada "paso" de la escalada), no por
frame: el coste es despreciable.

## 4. Pieza 2: `wallup_send`

### 4.1 Qué más condiciona el envío [V estático]

Emisor `0x87DD00`, llamado solo desde `X::Update 0x87CBC0` (`0x87D710`), que a su vez se llama una
vez por frame desde el update del `CharacterControl` `0x8610A0` (`0x861B2A`):

1. Llamada (`0x87D6EF`): actor de tipo 1 (`[[actor+0x88]+1] == 1`) y `byte actor+0x96 != 0` (el
   personaje controlado localmente; el manejador de 2893 exige justo lo contrario, `== 0`).
2. `byte exe+0x6D76D5C != 0` (**el que escribimos**).
3. `byte X+0xF0 == 5` (método WallUp activo) y `X+0x120` (el `WallUpMovementMethod`) ≠ 0.
4. Copia los datos de `D = [CC+0xD8]`: `+0xEE0`, `+0xEE4`, `+0x1170` (32 B), `+0x1190`, `+0x11C0`
   (32 B, WallPassPath), `+0x11E0`, `+0x11E8`, `+0x1200` (id), `+0xF40…`, `+0x10DC…`, `+0x1100`.
5. Actor controlado de `WorldSystem` (`[[[exe+0x6D69190]+0x30]+0x50]`) y su `vf 0xC0(4, 0x10)`
   (`UserOrCharacterValidLoginedScopeAttacher`: "usuario con sesión iniciada").
6. `0xB60CF0`: serializa, `vf 0x168` del actor controlado ≠ `0xFF` y
   `byte [[exe+0x6D69170]+0x112] != 0` (`ClientNet` conectado); entonces `PostPacket` (`0x35C670`)
   con el opcode `0xB23` = 2851, sesión 4.

Es decir: con el byte a 1, el cliente envía 2851 **en cada frame** mientras su personaje está en el
método 5. Las otras puertas son las normales de cualquier Req (en sesión y conectado).

### 4.2 ¿Sale el 2851 antes o después del inicio de la subida? [V el orden en el frame; H el resto]

- En `0x8610A0` el orden por frame es: `X::Update` (`0x861B2A`: cambio de método + `Enter` en
  `0x87D00A` + emisor 2851 en `0x87D710`) y **después** el vaciado de `StartActionReq` (`0x861D79` →
  `0x48A530` → `0xB54B90`, opcode `0x8AF` = 2223).
- Por tanto, si la acción de subida ya es la actual cuando corre `X::Update`, el **primer 2851 sale
  en el mismo frame y antes que el 2223** de esa acción: en el observador, el 2893 llegaría antes
  que el `StartActionAck`. Si la acción se fija después de `X::Update`, el 2223 sale en el frame N y
  el primer 2851 en el N+1 (~16 ms después). [H] cuál de los dos casos ocurre.
- Las funciones por frame del `WallUpMovementMethod` del observador (`vf5 0x6A7090`, `vf10
  0x6A7C00`) vuelven a leer `D+0xEE0` y `D+0x1200` en cada frame (y `vf5` reescribe `D+0x1200`), así
  que [H] un 2893 que llega un frame tarde solo afectaría a ese frame. El riesgo del informe (§6) queda
  acotado a ≤ 1 frame; **se mide en la captura** (ver §6.3).
- El 2851 se envía en cada frame del método 5: aunque el primero se pierda o llegue tarde, los
  siguientes rellenan `D` del remoto durante toda la subida.

### 4.3 Comprobación periódica

Cada segundo el hilo del mod relee el byte. Si vale ≠ 1, lo registra (`WRN`, máximo 50 líneas) y
escribe 1 otra vez. El resumen de 60 s lleva el número de comprobaciones y de reinicios.

No se vuelve a poner a 0 al salir: el mod no se descarga dentro del juego, y con el byte a 1 en una
partida sin otro jugador el servidor interno reenvía el 2851 a nadie (`ToNearExceptOneControl`
excluye al que controla).

### 4.4 Contar 2851/2893

El sniffer existente no tiene un contador barato por opcode (solo un `PacketTap` único, que ya usa
`host_scene_diag`, y estadísticas "top 8" cada 10 s). **Se deja al análisis de capturas**: cada
paquete aparece con su nombre en `cdmp_packets_*.log` (`UpdateWallUpDataReq` C>S en el escalador,
`UpdateWallUpDataAck` S>C en el observador) y completo en el `.bin`. La sonda (pieza 3) además
registra en memoria del observador el `D` del remoto en cada subida (`wall D+0xEE0 … id … up …`).

## 5. Pieza 3: `dismount_probe`

Solo lectura, desde el hilo del mod (nunca en un hilo del juego, nunca con el heap del juego):
- **Candidatos** (cada segundo): recorre la tabla hash del `ClientActorManager` (misma estructura que
  `game/actor_lookup.cpp`, con `fast_read`, hasta 2¹⁸ cubos) y se queda con hasta 4 actores con clave
  `0xA01…`, `u32 actor+0x90 != 0` (usuario dueño) y distintos del actor controlado local. Una línea
  `RemoteMotion probe: tracking the other player's character 0x…` al empezar a seguir uno.
- **Muestra** (cada 30 ms, reloj QPC): método `u8 mc+0xF0`; su resultado de `Enter`
  `u8 [mc+0xF8+8·método]+0x44`; acción de las dos capas `u32 [[CC+0x88/+0x90]+0x10]+0x18`; vínculo
  de vehículo `u32 [[[actor+0x68]+0x48]+0x18]+0x10`; posición `TS+0xB4` + sector `TS+0xC0`·1000
  (`TS = [[actor+0x68]+0x1A0]`); datos de subida `D+0xEE0`, `D+0x1200`, `D+0x1170` (3 floats).
- **Búfer circular** de 64 muestras por actor (memoria del mod): da el "1 s antes".
- **Disparo**: cambio de método que entra o sale de 6 (RideOn), 7 (RideOff) o 10 (Vehicle), o cambio
  del vínculo → `ride`. También se disparan, etiquetados, 18 (PointClimb → `climb`) y 5 (WallUp →
  `wallup`): sirven para medir las piezas 1 y 2 con la misma herramienta.
- **Episodio**: desde 1 s antes hasta 5 s después del último disparo (un disparo dentro lo alarga;
  máximo 20 s). Se registra como **tramos**: muestras seguidas con el mismo estado (método, `Enter`,
  acciones, vínculo, datos de subida) se agrupan; de cada tramo, posición inicial y final, distancia
  y rango de y. Máximo 64 tramos y 300 episodios por sesión.

Nota: el "padre del Path" de los Move no está en una posición de memoria conocida del actor remoto;
la sonda usa el método y el vínculo, que el informe (§4) identifica como la causa. Del caballo no se
lee nada (en el observador el vínculo vale 0 [H], así que no hay clave de la que partir).

## 6. Qué mirar en el log (`cdmp.log`, los dos PCs)

### 6.1 Arranque

```
Config: session2 ... | P8 remote motion: remote_climb '1' wallup_send true dismount_probe true
RemoteMotion: waiting for the local player in the world before hooking (never at start-up or on the title screen)
RemoteMotion: the local player is in the world (N s): installing
RemoteMotion: remote_climb 1 ON (host|guest): inline hook at PointClimb::Enter exe+0x691400, patch 7 of the 23 verified bytes, inside .rsrc; ...
RemoteMotion: wallup_send ON: byte exe+0x6D76D5C (.rdata 0x6893000+0x854000, flags 0xC0000040, page protect 0x4) 0 -> 1 | ...
RemoteMotion: dismount_probe ON (host|guest): read only, every 30 ms, ...
RemoteMotion probe: tracking the other player's character 0xA01xxxxx
```

Fallos posibles: `RemoteMotion: ... REFUSED` (bytes, sección o página), `RemoteMotion: wallup_send
OFF`, `RemoteMotion: nothing installed`.

### 6.2 Escalada libre (pieza 1), en el cliente que **observa**

```
RemoteMotion climb [1] qpc ...: PointClimb::Enter(same 0|1) of actor 0x... key 0xA01002E9 control mode 2: FORCED to 1 (mode 2, player character)
```

- Esperado: una línea por paso de escalada del otro jugador y el actor remoto subiendo por la pared
  sin "teletransporte" al final.
- `failed in mode 1` con la clave **propia** sería un fallo del `Enter` del jugador local (no se toca).
- `would be forced (probe ...)` con `remote_climb=probe`.
- Con la sonda: `RemoteMotion probe #N: actor 0x… climb ...`, y el tramo con `method 18 PointClimb
  enter 1` y `moved X m, y a..b` creciendo. Sin la pieza 1 se vería `enter 0` y la y quieta.

### 6.3 Subida a saliente / cajas (pieza 2)

- En el **escalador**: `UpdateWallUpDataReq` en `cdmp_packets_*.log` (≈ 1 por frame durante la
  subida).
- En el **observador**: `UpdateWallUpDataAck` en `cdmp_packets_*.log`; su Δt frente al
  `StartActionAck b0044755` (o `a7d51c05` en cajas) responde al §4.2.
- En el log del observador (sonda): tramos `method 5 WallUp ... | wall D+0xEE0 n id 0x… up (x, y, z)`
  con `up` ≠ (0, 0, 0). Antes del arreglo era (0, 0, 0).
- `RemoteMotion: the game reset exe+0x6D76D5C ...` (WRN) si el juego lo devuelve a 0 (no se espera).

### 6.4 Desmontar (pieza 3), para elegir F3

```
RemoteMotion probe #N: actor 0xA01xxxxx ride at t0 (method 10 Vehicle -> 7 RideOff, link 0x0 -> 0x0) | S samples from -990 to +5010 ms, K segments
  [-990..-30 ms x33] method 10 Vehicle enter 1 | act 7b6aff57/7b6aff57 | link 0x0 | pos (...) -> (...) moved 4.20 m, y 618.03..618.03
  [+0..+900 ms x31] method 7 RideOff enter 0 | act 667e3b35/... | link 0x0 | pos (...) -> (...) moved 0.00 m, y 618.03..618.03
  ...
```

Cómo leerlo:
- `enter 0` en RideOff y la y quieta a la altura de la silla confirman el congelamiento (§4 del
  informe).
- Si después hay un tramo `method 0` (u otro) con `enter 1` y la y no baja, la acción siguiente no lo
  recupera: mirar qué método y qué acción tiene.
- `link` siempre 0 en el observador confirma que nunca recibe el vínculo (descarta F3b sin F3c).

### 6.5 Resumen cada 60 s

```
RemoteMotion summary: remote_climb 1 (hook on): Enter calls N, original ok N, forced N (last 0x...), would force 0 (...), failed mode 1 N / mode 3 N / other mode N / mode 2 not a player N (...) / unreadable N, events lost 0 | wallup_send on (byte 0 -> now 1, checks N, game resets 0, unreadable 0) | dismount_probe on: tracked 1 (0xA01...), samples N, misses N, triggers ride N climb N wallup N, episodes N (not logged 0), scans N
```

## 7. Riesgos y dudas

- **Pieza 1:**
  - Se fuerza solo en modo 2 con clave de jugador; en 1/3 nunca (vf1 `0x691DB0` usaría un handle de
    punto inválido). Probado en `main` con el clon (mismo modo 2); en el actor nativo es [H].
  - Llamar a `0x88E8E0` desde el detour: es del propio juego, solo lee, y el mismo hilo ya está
    dentro de código del juego. Está bajo `__try`.
  - Deslizamiento leve de la animación (ya visto en `main`).
- **Pieza 2:**
  - Denuvo podría leer o escribir el byte desde `.xtls` (no analizable); la comprobación periódica
    lo detectaría.
  - Orden 2893 frente a `StartActionAck` (§4.2): acotado a 1 frame [H]; medir en la captura.
  - "Desaparece" por ruta a cero está medido en el clon de `main`, no en el actor nativo
    (**media-alta**). Si el actor sigue desapareciendo con `up` ≠ 0 en la sonda, la causa es otra.
  - Más tráfico por el túnel: ~70–110 paquetes de 137 B por subida (despreciable).
- **Pieza 3:** solo lectura; carreras con el hilo del juego cubiertas por `fast_read` (SEH). Un tramo
  puede mezclar un frame a medio actualizar (es una sonda, no una decisión).
- **Fuera de alcance:** el arreglo F3 (caballo sintético, `RideOn` forzado, vínculo real, recarga) y la
  interpolación remota (`0x11B2xxx`), en la que trabaja otro agente.
- **No probado en vivo** (ni lanzado ni desplegado). Las direcciones y bytes son del exe en disco.
