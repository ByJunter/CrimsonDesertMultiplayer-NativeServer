# P8: rendimiento del túnel — vueltas lentas del hilo de red del anfitrión y el 2144 que llega tarde

10 de octubre de 2026, rama `opcion-d`, base `e6d045d`. Build del juego 1.0.0.2976.

**[V]** = comprobado (log, captura, medida en este PC, código o prueba unitaria). **[E]** = inferencia o hipótesis
pendiente de comprobar en vivo.

No se lanzó el juego, no se tocó `bin64` y no se hizo commit. No se engancha `.xtls`. Los hilos del mod siguen sin
tocar el heap del juego (`p8-heap-inyeccion.md`): lo nuevo solo **lee** memoria del juego (con SEH) y solo asigna
memoria propia (CRT). Los cambios de comportamiento van detrás de claves; ver §4.3 sobre la única excepción
(`c4_safe_reads`), que no cambia la semántica.

## 0. Evidencia

| Ref. | Archivo |
|---|---|
| L1 | `C:\Emulador CD\backups\logs\cdmp_20261009-2326_p8-2322-local.log` (anfitrión + `cdmp_peer`, 3 épocas) |
| HB | `C:\Emulador CD\backups\logs\cdmp_20261009-2345_p8-6B-host.log` (anfitrión, sesión 6B con Shadow) |
| L2 | `C:\Emulador CD\backups\logs\cdmp_20261010-0113_esc-ok-host.log` |
| L3 | `C:\Emulador CD\backups\logs\cdmp_20261010-0129_acks-host.log` |
| GB | `C:\Emulador CD\backups\p8-sesion6B-shadow\cdmp.log` (invitado, 6B) |
| GBC | `C:\Emulador CD\backups\p8-sesion6B-shadow\cdmp_packets_20261009-224848.bin/.log` (cliente del invitado, 6B) |
| GAC | `C:\Emulador CD\backups\p8-sesion6A-shadow\cdmp_packets_20261009-224011.bin/.log` (cliente del invitado, 6A) |

## 1. Resumen

1. **Problema 1 (vueltas de 250–920 ms en «host tunnel») [V causa medida, E fuerte en el juego].** Coinciden con
   dos momentos del túnel: la primera captura de las claves del usuario 2 («`C4: user 2 of epoch N exists`») y la
   barrera C4 de cada salida (cada 250 ms). Las dos hacen cientos o miles de lecturas de memoria del juego con
   `game::safe_read`, que llama primero a `VirtualQuery`. `VirtualQuery` recorre la tabla de páginas **de toda la
   región** para calcular su tamaño. En este PC cuesta 52 µs en una región de 64 MB, 0,47 ms en 512 MB y 5,9 ms en
   2 GB, mientras que una lectura con SEH cuesta 0,01–0,03 µs. **Arreglo:** esas lecturas pasan a ser SEH directas
   (`game::fast_read`, la misma que ya usa `child_hold` en los hilos del juego), con los mismos valores y los mismos
   fallos. La clave `c4_safe_reads=1` vuelve a las de antes. Además, el tick del túnel queda **medido por partes**:
   cualquier tick de más de 50 ms sale en el log con el tiempo de cada parte.
2. **Problema 2 (el 2144 llega unos 7,6 s tarde) [V].** El servidor escribe la ráfaga del campo
   (6363 × `SyncSceneObjectAck` 2665 + 120 × `ActorLoadDataAck` 2005, 7,4 MB) **antes** del 2144, dentro de su
   propio `MoveFieldComplete`, 27 ms antes de encolar el 2144. Ese orden es el nativo y es el mismo en 6A y en 6B.
   **No se reordena** (§3.2): no hay ninguna prueba de que el cliente admita un 2144 antes que los objetos de su
   campo. Por eso **no hay `tunnel_priority`**. Lo que se acorta es la ráfaga: **compresión de los lotes del
   túnel** (LZ propio, sin dependencias), con los mismos paquetes y en el mismo orden. Medido sobre GBC [V]:
   7 453 055 B → 786 696 B (×9,5), 7,4 ms de CPU. A 1 MB/s son **7,12 s → 0,75 s**. Clave
   `[session2] tunnel_compress=1` en **los dos PC** (por defecto 0). El anfitrión solo comprime si el invitado dijo
   en esa conexión que sabe descomprimir.

## 2. Problema 1: vueltas lentas en «host tunnel»

### 2.1 Qué se ve [V]

| Log | Vuelta lenta | Lo que pasa justo antes |
|---|---|---|
| L1 | 23:28:00.501, 334,7 ms | 23:28:00.171 `C4: user 2 of epoch 1 exists` (primera captura de sus 102 claves) |
| L1 | 23:28:58.982, 401,6 ms | 23:28:58.581 `… epoch 2 exists` |
| L1 | 23:29:25.640, 264,1 ms | barrera de la época 2: `b sam FAIL (6 of 102 keys still resolve …)` |
| L1 | 23:29:47.238, 306,9 ms | 23:29:46.936 `… epoch 3 exists` |
| L1 | 23:30:14.517, 549,2 ms y 23:30:15.038, 521,2 ms | barrera de la época 3 (`b sam FAIL (101 of 102 …; user key … still in the user list)`): 102 claves más la lista de usuarios del SAM, entera |
| L2 | 01:15:28.996, 252,4 ms | 01:15:28.749 `… epoch 1 exists`. Después, mientras el usuario 2 está dentro, el máximo de cada ventana de 5 s es de 40–85 ms «host tunnel» (la captura de 1 Hz) |
| L3 | máx. 103,4 ms en la ventana del `exists`; 160,8 ms en la de la salida | igual |

La vuelta de 711,7 ms de HB (23:52:38) es de un binario anterior, con el bucle de reintentos del 2001 sin ritmo.
`hand_logout` ya lo limita (`p8-heap-inyeccion.md`). No es esta causa.

### 2.2 Qué hace el tick en esos momentos [V código]

- `tunnel_tick_reconnect`, cada `kSampleMs = 1 s` mientras el usuario 2 existe, llama a `capture_user2()` →
  `server_capture_user2` → `capture_departed`, que hace:
  - `user_actor_keys`: unas 3 + 2 × (hijos) lecturas;
  - por cada clave nueva, `sam_key_resolves`: unas 8–9 lecturas del mapa del SAM;
  - `sam_user_listed` hasta encontrarla: 1 lectura por cada actor de la lista de usuarios del SAM (hasta 0x4000).

  La primera vez son todas nuevas: más de mil lecturas. Las siguientes son unas 200 por segundo (los hijos).
- En RETIRING, cada `kBarrierMs = 250 ms`, `barrier_tick` → `server_barrier_read`:
  - 102 claves × ~9 lecturas;
  - la lista de usuarios **entera** (el usuario ya no está, así que se recorre hasta el final);
  - el campo y el diagnóstico de movimiento.
- Todas eran `game::read` / `game::read_ptr` / `game::safe_read` → `VirtualQuery` + copia con SEH.

### 2.3 Lo que cuesta `VirtualQuery` [V medido en este PC]

Programa de prueba en el scratchpad. Reserva una región, la confirma en trozos de 64 KB (como un heap), toca cada
página y mide 200 llamadas:

| Región confirmada | `VirtualQuery` | Lectura con SEH |
|---|---|---|
| 4 MB | 3,40 µs | 0,010 µs |
| 64 MB | 51,56 µs | 0,009 µs |
| 512 MB | 467,34 µs | 0,020 µs |
| 2 GB | 5 930 µs | 0,022 µs |

`optd_c4` lo repite en cada build sobre 256 MB: `0.032 us SEH only, 222.0 us with VirtualQuery`. El coste crece
con la región porque `VirtualQuery` calcula `RegionSize` recorriendo las entradas de página contiguas con los
mismos atributos.

**[E fuerte]** Los segmentos y arenas de mimalloc del juego son regiones de cientos de MB. A 0,2–0,5 ms por lectura,
las ~1 000–2 000 lecturas de una captura o de una barrera son 250–900 ms, y las ~200 por segundo de la captura
periódica son los 40–85 ms de L2. La medida por partes de §2.4 lo confirmará en vivo (§6.1).

### 2.4 Arreglo

1. **Lecturas sin `VirtualQuery`** (`guest_lifecycle.{h,cpp}`: `probe_read`, `probe<T>`, `probe_ptr`).
   - Todas las sondas de C4 (`sam_find_actor`, `sam_user_listed`, `read_field`, `field_lists_user`,
     `field_listed_actors`, `read_move_diag`, `user_actor_keys`, `capture_departed`) y las lecturas de
     `server_capture_user2`, `server_host_field` y `server_barrier_read` leen con `game::fast_read`.
   - Es SEH puro, los mismos límites de dirección y la misma comprobación de puntero plausible: los mismos valores
     y los mismos `nullopt`. Un puntero viejo se captura igual.
   - Lo único que se pierde es la comprobación previa de `PAGE_GUARD`. Estas lecturas son solo de objetos del heap
     (SAM, actores, campo), nunca de pilas, la misma política que `child_hold` aplica al mismo mapa en los hilos
     del juego (`safe_memory.h`).
   - `[session2] c4_safe_reads=1` vuelve a `safe_read`. Sirve de control (§6.1) y de salida si algo raro
     apareciera.
   - `child_hold` llama a `sam_find_actor` de este módulo y también sale ganando. No se tocó su archivo.
2. **El tick del túnel, medido por partes** (`common/tick_profile.h`, `session2.cpp`). Las partes son:
   `child hold`, `hold acks`, `inject watch`, `link state`, `user 2 sample/capture`, `logout (2549/2001)`,
   `held guest Req`, `outgoing to the link`, `C4 barrier`, `control` y `link flush`.
   - Un tick de más de 50 ms se registra (200 líneas como máximo):
     `WRN Session2 host tunnel: one tick took 412.3 ms: user 2 sample/capture 405.1 ms, …`.
   - Si la barrera pesó, la línea añade `| barrier reads: a session X ms, b 102 keys Y ms, b user list Z ms, c field W ms`.
     Si pesó el envío, añade `| link backlog N B`.
   - El informe de 5 s lleva `| host tunnel ticks N, max X ms (parte), above 50 ms K (T in all)`.
3. **Nada se saca del hilo de red.** Con lecturas SEH, una barrera son ~1 000 lecturas × 0,03 µs ≈ 30 µs más la
   lista (≤ 16 K × 0,03 µs ≈ 0,5 ms) **[E]**. Moverla a otro hilo añadiría sincronización sin ganar nada medible.
4. **La semántica no cambia.** Mismas funciones, mismo orden, mismos resultados. `optd_c4` ejecuta todas las
   pruebas de sondas con los dos tipos de lectura.

## 3. Problema 2: el 2144 llega tarde detrás de la ráfaga

### 3.1 Qué pasó [V]

- **GBC, viaje 2:** el `MoveFieldCompleteReq` 2549 sale en #1449 (t = 54,389 s) y el 2144 llega en #9118
  (t = 62,095 s): 7,7 s. Entre los dos llegan, de servidor a cliente:
  - 6363 × `SyncSceneObjectAck` 2665 (615 B cada uno, casi iguales: cambian un id y pocos campos);
  - 120 × `ActorLoadDataAck` 2005;
  - unos pocos más (2697, 2642, 2991, 2404, 2277).

  Son 6500 paquetes y 7 432 865 B en total, a un ritmo constante de ~1577 paquetes/s ≈ 1 MB/s. Mientras tanto el
  cliente ya los procesa: manda 1083 × `RegionInOutReq` 3067.
- **HB:**
  - el 2549 se entrega al servidor a las 23:50:06.551 y el 2144 se encola a las 06.578, 27 ms después;
  - a las 23:50:11, `backlog 2945825 B`, `reliable pending 549`, `gate … CONGESTED`, `send rate 1024 KB/s`;
  - a las 23:50:16, `out 1015.1 KB/s`.

  El túnel es FIFO: lo que se encoló antes del 2144 tuvo que cruzar antes.
- **GAC (6A):** lo mismo. 6295 × 2665 + 125 × 2005 entre el 2549 (t = 54,993 s) y el 2144 (t = 62,815 s).
- Tras la ráfaga, 2277 `SetMoveFieldAck`, 2144, 2586, 3007, …, 3066 y nuestro 2322 llegan juntos: el 2322 llega
  55 ms después de su 2144 (GB `GuestLoad [in 2322] … +55.279 ms after its 2144`).

### 3.2 ¿Se puede adelantar el 2144? El orden que necesita el cliente

- **El orden es del servidor, no del túnel [V].** El servidor escribe los 2665 y los 2005 del campo **dentro** del
  manejo del 2549, antes del 2277 y del 2144. Pasa en los dos viajes de 6A y de 6B, y HB lo encola todo en esos
  27 ms. El cliente nativo (bucle de memoria del anfitrión) nunca ve otro orden.
- **El cliente consume la ráfaga en la carga [V]:** sus 1083 3067 salen mientras llegan los 2665. El 2144 abre la
  ventana B. Luego el 2322 hace que la carga pase de `0x11` a `0x12` y termine en el 2445 (GB `GuestLoad`).
- **Adelantar 2144/3066/2322 [E, sin prueba de que sea seguro]** dejaría al cliente cerrar la carga (`0x11→0x12`,
  `LoadingCompleteReq` 2445) antes de conocer los objetos de escena de su campo. El servidor daría al usuario 2 por
  cargado con objetos aún sin aplicar. Ningún cliente real ha visto nunca un 2665 después de su 2144, y el
  manejador no se ha estudiado.
- El 2322 tiene que seguir a su 2144 y a su 3066 (`p8-2322-observador.md`, veredicto `one 2322, after its 2144`).
  Por eso tampoco se puede adelantar solo.
- Los paquetes pequeños de control no esperan por otra cosa: en cuanto pasa la ráfaga van en el mismo lote.

**Decisión:** no se reordena nada y **no se implementa `tunnel_priority`**. Una clave que cambia el orden sin
prueba sería una trampa para la próxima sesión. Se reduce la latencia de la ráfaga.

### 3.3 Solución: lotes del túnel comprimidos

Medido con la herramienta nueva `cdmp_peer --mode tunnel-measure`. Toma los paquetes de servidor a cliente de una
captura, los pasa por un `TunnelSender` como el del anfitrión, los decodifica con un `TunnelReceiver` y comprueba
que salen los mismos y en el mismo orden [V]:

| Tramo | Paquetes | Bytes en crudo | Comprimidos | CPU (compr. / descompr.) | A 1 MB/s |
|---|---|---|---|---|---|
| GBC viaje 2 (54,38–62,096 s) | 6558 | 7 453 055 | 786 696 (×9,5) | 7,4 ms en 7 vueltas / 3,2 ms | 7,12 s → **0,75 s** |
| GBC viaje 1 (26,38–26,87 s) | 255 | 421 241 | 31 091 (×13,6) | 0,5 / 0,4 ms | 0,40 s → 0,03 s |
| GBC entera | 110 783 | 54 271 011 | 7 719 309 (×7,1) | 65,9 ms en 50 vueltas / 35,7 ms | 51,97 s → 7,36 s |
| GAC entera | 77 699 | 35 708 201 | 5 172 203 (×6,9) | 43,4 ms / 22,4 ms | 34,20 s → 4,93 s |

En todas: `back to the same packets in order: yes`.

**Diseño:**

- **`common/lz_block.{h,cpp}`:** LZ77 del estilo de LZ4, propio y sin dependencias.
  - Tabla hash de 16 K posiciones y ventana de 64 KB.
  - El decodificador nunca escribe más allá de `raw_size` ni lee fuera de la entrada, y falla si no sale
    exactamente `raw_size`.
- **Formato** (`troc_frame.h`): un lote comprimido es `u8 dir|0x80 | u32 tamaño en crudo | datos LZ` dentro del
  mismo mensaje `TrocFrame`.
  - Dentro va el lote normal (`encode_batch`). Su `dir` interno debe coincidir con el externo.
  - Límite de 256 KB en crudo.
  - Si comprimir no ahorra, el lote va en crudo. `decode_message` acepta las dos formas.
- **`TunnelSender::set_compress`:** comprime cada lote (≤ 64 KB) al construirlo.
  - Como mucho 1 MB en crudo por `flush()` (≈ 1,3 ms medidos); el resto, en la vuelta siguiente.
  - Contadores: bytes en crudo, bytes en el cable y lotes comprimidos.
- **Acuerdo por conexión** (`TrocLink`):
  - El extremo que recibe la bajada (el mod del invitado con la clave, o `cdmp_peer`) manda un paquete de
    capacidades (opcode 2, `u8 flags`, bit 0 = sabe descomprimir) **justo antes de su primer paquete** en cada
    conexión. Nunca va solo: un enlace que no ha dicho nada sigue sin haber dicho nada para la regla de
    reconexión de `TrocLink::tick`. Con el anuncio suelto, `optd_replay` falló; así se detectó.
  - El otro `TrocLink` lo saca antes de que llegue a su dueño.
  - El anfitrión comprime solo si `set_compress(true)` **y** las capacidades llegaron en la conexión **actual**.
    Una conexión nueva vuelve a crudo hasta el siguiente anuncio. Un `reset()` de época C4 en la misma conexión
    las conserva.
- **Compatibilidad:**
  - Un invitado sin la clave (o con un binario anterior) nunca anuncia, y el anfitrión manda en crudo.
  - Un invitado nuevo frente a un anfitrión anterior: su opcode 2 llega a `offer()`, que lo descarta como «no es
    una Req del servidor» (contado). Igualmente, los dos PC deben llevar el mismo `cdmp.asi`.
- **Velocidad de Steam:** `send rate 1024 KB/s` es el mínimo configurado (`[steam] send_rate_min_kb=1024`).
  Subirlo (por ejemplo, 2048) es otra palanca, pero depende de la subida real del anfitrión y del relé **[E]**.
  La compresión no depende de eso.
- **Esperado en una sesión como 6B [E]:** el 2144 de la entrada al campo, de ~7,6 s a ~0,8 s más el RTT. La
  carga del juego en sí (0x11 → 2445) no cambia.

## 4. Cambios

| Archivo | Qué |
|---|---|
| `src/common/lz_block.{h,cpp}` | Nuevo: el códec |
| `src/common/troc_frame.{h,cpp}` | `kCapsOpcode`/`kCapsCompressed`; `compress_batch`, `decode_message`; `TunnelSender::set_compress`, presupuesto por `flush()` y contadores; `TunnelReceiver` acepta las dos formas |
| `src/common/troc_link.{h,cpp}` | `set_compress`, `set_announce_caps`, `peer_decodes_compressed`, `compressing`; el anuncio va con el primer paquete de cada conexión; `describe()` con la compresión |
| `src/common/tick_profile.h` | Nuevo: tiempo por partes de un tick |
| `src/mod/session2/session2.cpp` | Medida por partes de `tunnel_tick`, línea de tick lento, ventana en `describe()`, tiempos de lectura de la barrera; `set_probe_safe_reads` en `install` |
| `src/mod/session2/guest_lifecycle.{h,cpp}` | `probe_read`/`probe`/`probe_ptr` (SEH por defecto, `VirtualQuery` con la clave); las sondas los usan; `BarrierReadings::us_*` |
| `src/mod/session2/server.cpp` | `server_capture_user2`, `server_host_field` y `server_barrier_read` con `probe*`, con tiempos por lectura |
| `src/mod/config.{h,cpp}` | `s2_tunnel_compress` (`tunnel_compress`), `s2_c4_safe_reads` (`c4_safe_reads`), en un bloque aparte |
| `src/mod/mod.cpp` | Enlace del anfitrión: `set_compress(tunnel_compress)`; enlace del invitado: `set_announce_caps(tunnel_compress)` |
| `tools/peer_sim/{main,replay}.cpp`, `replay.h` | `--raw-tunnel` (por defecto anuncia) y `--mode tunnel-measure --capture F [--from S] [--to S]` |
| `tests/optd_tests.cpp` | Códec, lotes comprimidos, acuerdo por conexión, ráfaga sobre Steam simulado, `TickProfile` |
| `tests/optd_c4_tests.cpp` | Sondas con los dos tipos de lectura, memoria ilegible, coste en 256 MB |
| `CMakeLists.txt` | `lz_block.cpp` en `cdmp_common` |
| `docs/research/ini/cdmp.p8-tunel-local.ini` | Nuevo (§6) |

### 4.1 Claves (`[session2]`)

| Clave | Dónde | Por defecto | Qué |
|---|---|---|---|
| `tunnel_compress` | **anfitrión e invitado** | 0 | Anfitrión: comprime cuando el invitado lo anuncia. Invitado: lo anuncia |
| `c4_safe_reads` | anfitrión | 0 | 1 = lecturas de C4 con `VirtualQuery`, como antes (control) |

### 4.2 Líneas nuevas del log

- `Session2: Troc tunnel open … ; tunnel_compress=1: batches compressed once the guest says it decodes them`
  (anfitrión) y `Session2 guest: Troc tunnel open … ; tunnel_compress=1: the host may send compressed batches`
  (invitado).
- En el informe de 5 s:
  - `Session2: Troc tunnel: … | compression ON (the other end decodes it): N batches compressed, out X KB as Y KB (xR)`
    en el anfitrión;
  - `| in compressed batches N` en el invitado y en `cdmp_peer`.
- `WRN Session2 host tunnel: one tick took … ms: <partes>` y `| host tunnel ticks …` (§2.4).
- `Session2: c4_safe_reads=1: …` solo si la clave está a 1.

### 4.3 Por qué `c4_safe_reads` va a 0 por defecto

La regla del proyecto es: cambio de comportamiento detrás de una clave apagada. Aquí no cambia ningún
comportamiento observable. Son las mismas lecturas, los mismos valores y las mismas decisiones de la barrera;
solo cambia el coste. Con 0 por defecto se cumple el objetivo (sin vueltas > 100 ms), y la clave queda para el
control y como salida.

## 5. Pruebas [V]

`.\scripts\build.ps1`: `100% tests passed, 0 tests failed out of 24`.

- **`optd`:**
  - códec: ida y vuelta (vacío, 1–39 B, 100 KB de ceros y aleatorios, repeticiones de periodo 1–70 000);
    tamaño exacto en los dos sentidos; truncados que siempre fallan; 4000 entradas corrompidas o basura, sin
    fallo y sin pasar de `raw_size`; desplazamiento 0 o anterior al inicio;
  - lotes comprimidos: forma, dirección interna, tamaño, truncados, lote que no se comprime, emisor con parte en
    crudo y parte comprimida, presupuesto por `flush()` (≥ 3 vueltas para 3,6 MB), mismo orden;
  - acuerdo `TrocLink`: nada sin anuncio; el anuncio va con el primer paquete; el dueño nunca ve el opcode 2;
    sin la clave del anfitrión, en crudo; una conexión nueva pide un anuncio nuevo;
  - ráfaga de campo sobre el Steam simulado a 1 MB/s (4000 paquetes del tipo de 6B y luego el 2144):
    `raw 2805 ms, compressed 605 ms`, mismo orden y el 2144 después de todo lo anterior;
  - `TickProfile`.
- **`optd_c4`:** todas las sondas con SEH y con `VirtualQuery`; página reservada sin confirmar ilegible por los dos
  caminos (SAM con contenedor ilegible = `nullopt`, no «ausente»); puntero implausible; coste en 256 MB:
  `0.032 us SEH only, 222.0 us with VirtualQuery`.
- **`optd_replay`:** `cdmp_peer` anuncia y el anfitrión de prueba lo filtra: `All option D replay tests passed`.
- **`cdmp_peer --mode tunnel-measure`** sobre GBC y GAC: §3.3.

## 6. Verificación en un PC (anfitrión + `cdmp_peer`)

`bin64\cdmp.ini` = `docs/research/ini/cdmp.p8-tunel-local.ini` (= `cdmp.p8-escenas-local.ini` + `tunnel_compress=1`
+ `c4_safe_reads=0`). Desplegar `cdmp.asi` de este build.

**Guiones:**
- `login_e3lc00soak_child_112627.troc` (entrada, primera acción y estancia larga);
- `login_e3lc00bye_112627.troc` (entrada y despedida: salida, barrera y nueva época).

**No** uses los `…act…`, que están obsoletos (`p8-crash-esc-1831598.md`).

```
.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00soak_child_112627.troc --var SAVE_SLOT=<ranura del anfitrión> --record D:\cdmp-grabaciones\tunel-soak.bin
.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00bye_112627.troc --var SAVE_SLOT=<ranura del anfitrión> --record D:\cdmp-grabaciones\tunel-bye-1.bin
```

Para un ciclo de entrada, salida y época: `bye` dos o tres veces seguidas (reconnect=1: cada una es una época nueva).
Después, `soak_child` y cerrarlo con Ctrl+C para la salida por enlace perdido (15 s).

### 6.1 Problema 1 (hilo de red)

- **Esperado:**
  - ninguna `WRN Net: one loop took … (mostly host tunnel)`;
  - ninguna `Session2 host tunnel: one tick took` por encima de 100 ms;
  - en cada informe, `Net: … net thread: … max X ms` con X < 100 y `host tunnel ticks …, max X ms (…), above 50 ms 0`;
  - en cada `C4: user 2 of epoch N exists` y en cada barrera (`C4: barrier epoch=…`), ninguna línea de tick lento.
    Si sale alguna, dice qué parte fue (`user 2 sample/capture`, `C4 barrier` con `barrier reads: …`, `link flush`…).
- **Control** (confirma la causa en vivo): la misma prueba con `c4_safe_reads=1`. Deben volver las vueltas de
  250–550 ms, ahora con
  `Session2 host tunnel: one tick took … user 2 sample/capture …` en el `exists` y `… C4 barrier … | barrier reads:
  … b user list …` en la salida. Si con `c4_safe_reads=1` la parte lenta fuera otra, §2.3 queda refutado: mandar
  el log.

### 6.2 Problema 2 (compresión)

En un PC el enlace es UDP en bucle local, sin el límite de 1 MB/s de Steam. Aquí se verifica que **funciona y no
cambia nada**; la ganancia de tiempo es la de §3.3 y §5.

- **Anfitrión** (informe de 5 s, tras la entrada):
  `Session2: Troc tunnel: connected | … | compression ON (the other end decodes it): N batches compressed, out X KB as Y KB (xR)`,
  con R ≈ 7–10.
- **`cdmp_peer`:** su línea `Troc tunnel: … | in compressed batches N` con N > 0; el guion termina igual
  (`script done at step …`).
- **Mismo contenido:** la grabación de `cdmp_peer` con compresión y otra con `tunnel_compress=0` (o con
  `--raw-tunnel`) dan el mismo resumen por opcode, salvo lo que el servidor varía de suyo:
  `python -I -B tools\scripts\analyze_capture.py D:\cdmp-grabaciones\tunel-soak.bin trips` (2144 → 3066 → 2322 →
  2445 → 2762 como siempre). Con `--raw-tunnel`, el anfitrión debe decir
  `compression off (the other end did not say it decodes it)`.
- **Fallo:** cualquier `rejected` > 0 en el túnel (`in … packets, N rejected`), `BROKEN` o un guion que falle donde
  antes pasaba. Para y manda el log.

### 6.3 Con Shadow (pendiente: hace falta Shadow PC)

`tunnel_compress=1` en los dos `cdmp.ini` (anfitrión e invitado) y la misma entrada al campo de 6B.

- **Esperado:** `GuestLoad [in 2144] … +X ms after the 2549` con X ≈ 1 s en lugar de 7,7 s.
- **En HB:** `compression ON … (x7–10)` y un `backlog` mucho menor durante la ráfaga.

## 7. Riesgos y pendientes

- **[E] `PAGE_GUARD`:** las lecturas SEH no comprueban la página antes. Un puntero basura que apunte justo a la
  página de guarda de una pila la consumiría. Solo se siguen punteros a objetos del heap (SAM, actores, campo),
  igual que `child_hold` en los hilos del juego. `c4_safe_reads=1` lo evita si hiciera falta.
- **[E]** La compresión añade ≤ ~1,3 ms por vuelta del hilo de red del anfitrión durante una ráfaga (presupuesto de
  1 MB por `flush`) y ~3 ms por cada 7 MB en el invitado.
- Si la subida del anfitrión fuera menor que 1 MB/s, la ráfaga comprimida sigue siendo ×7–10 menor. Subir
  `send_rate_min_kb` es otra prueba, aparte.
- La reordenación queda descartada mientras no se estudie el manejador del 2665 en el cliente y se pruebe un 2665
  posterior a su 2144. Esa sería la única base para un futuro `tunnel_priority`.

## Correcciones tras la revisión de la noche (2026-10-10)

Detalle completo en `p8-revision-noche.md` §6.

- **`PAGE_GUARD` (riesgo de §7):** resuelto. El filtro de `fast_read` (y de `safe_read`) vuelve a armar la guarda
  de la página tocada (`VirtualProtect(…, Protect | PAGE_GUARD)`) y la lectura falla como ilegible: la página
  queda como estaba. Solo se atrapan AV, `IN_PAGE_ERROR` y guarda; lo demás sigue. Test en `optd_c4`.
- **`c4_safe_reads` sigue a 0 por defecto, y ahora con más razón:** con el filtro nuevo, las dos lecturas dan lo
  mismo para cada estado de página. Solo cambia el coste (0,035 us frente a 234 us por lectura en 256 MB, este
  build). Por tanto no es un cambio de comportamiento que la regla obligue a apagar (§4.3). El arranque lo dice en
  una línea, y `describe` cuenta las guardas tocadas.
- **M8 (anotado, sin cambio):** `decode_message` reserva hasta 256 KB (`out.assign`) antes de validar un mensaje
  comprimido. Con entrada remota maliciosa son 256 KB y una descompresión O(n) por mensaje, acotado por el ritmo
  del transporte. Aceptable.
- **M7:** `TrocLink` solo intercepta el opcode 2 (*caps*) en el extremo que recibe lotes `ToHostServer`.
