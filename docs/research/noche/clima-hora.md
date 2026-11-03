# Hora y clima del invitado (opción D) — causa raíz y arreglo

Build **1.0.0.2976**, PE `0x6AB28F00`, base `0x140000000`; direcciones en RVA. Investigación de solo lectura
(2026-10-11): sin editar código, sin lanzar el juego y sin parar Ghidra. Etiquetas: **[V]** comprobado en capturas,
logs o bytes; **[E]** deducido del código (`[E-x]`: tramo legible de `.xtls`, solo leído); **[H]** hipótesis.
H = anfitrión, G = invitado (Shadow). Scripts propios en `%TEMP%\noche-clima\` (`ext.py`, `align.py`,
`ftscan12.py`, `window2.py`, `dis.py`, …). La alineación H/G se hace en el **eje del tick del servidor**
(cada captura convierte su tiempo con el `ServerTimeInfoAck` 2188 anterior).

## 0. Dictamen

1. **[V] El reloj del mundo no está congelado y G recibe una hora fresca al entrar.** En S16 G recibe
   `2144` con día 6 **13:59:53.952**, escala 12, ancla 194007, que es exactamente la base del mundo
   (13:37:28.728 @ 81905) más (194007−81905)·12 ms. El `clock gate` no mueve el reloj mundial en ninguna sesión
   (S15–S23: "world clock … -> … (sin MOVED)").
2. **[V] Después de entrar, G no vuelve a recibir la hora nunca.** No hay ningún 2144 posterior, ni 2816,
   en S15–S22. Los viajes rápidos de G dentro del campo son `2437→2643→2873→2445=0→2762=0`, sin hora.
3. **[V] Lo que se vio en S16 ("en H anochece y llueve, en G hay sol") es el clima, no el calendario.**
   Las sesiones duran ~10 min y H siempre arranca en ~13:37 (el guardián impide guardar): el calendario de H
   acaba hacia **15:31** en S16. No llega a anochecer. La lluvia de H empieza en el tick 245976 y la de G en el
   305985 (**+60,0 s**). Luego cada viaje rápido **del anfitrión** reinicia el clima de G.
4. **[E] La causa raíz es de arquitectura.** En el juego normal, cliente y servidor comparten proceso. Por eso
   basta con fijar la hora al entrar (2144), con los cambios explícitos (2816 `ToNear`) y con "parar el mundo
   mientras uno carga" (`LoadingComplete` con flag 1, en los dos lados a la vez). La opción D conserva ese
   contrato puntual, pero ningún mecanismo mantiene a G en la línea de tiempo del servidor de H:
   - G pierde su propia carga (S16: **3 min 31 s de juego**);
   - G no se entera de las re-anclas que H hace en sus cargas completas, cambios de campo o sueños;
   - el clima es una simulación por cliente, sin mensaje S→C. Su estado depende de cuándo se reinició, y a G
     se lo reinicia la ola de descarga que provoca cada viaje de H.
5. **[E] Arreglo propuesto: el anfitrión es la única autoridad del entorno.**
   - **Hora:** el servidor de H inyecta a la sesión 2 un **`SetFieldTimeAck` 2816 nativo** con la base y el ancla
     del reloj mundial. Lo hace justo **después de cada 2762** de G y **cada vez que se publica el reloj mundial**.
     No necesita ganchos nuevos en el juego.
   - **Clima:** se cierra la vía G→H (bloquear 2560 de G). Los reinicios que provoca H se arreglan en la ola de
     descarga, que es el pendiente nº 4. Una réplica del estado del clima queda condicionada a medir (§5.3).

## 1. Qué viaja, cuándo y a quién (pregunta 1)

| Mensaje | Dir. | Formato | Cuándo / a quién | Visto en S15–S22 |
|---|---|---|---|---|
| `MoveFieldCompleteAck` 2144 | S→C | 51 B: actor, pos, **FieldTime 31 B en +0x10**, u32 | Al terminar un **cambio de campo**, solo al actor que se mueve (`ToTarget`). La hora se toma en `0x2BC0A30` al **empezar** el viaje | G: 2 por sesión (aparcamiento día0 24:00 esc.0; campo compartido fresco) [V] |
| `LoadingCompleteAck` 2762 | S→C | 13 B: actor, **flag**, **now u64** | Fin de cada carga, `ToTarget` (`0x2BCAF00`) | G: 2 con flag 1 al entrar (aparcamiento y campo compartido) y flag 0 en cada viaje rápido [V] |
| `SetFieldTimeAck` 2816 | S→C | 35 B: u32 actor + FieldTime | Solo en cambios explícitos de hora (`0x2BC8970`, `ToNear`) | **0** [V] |
| `ServerTimeInfoAck` 2188 | S→C | 20 B: 0, tick u64, unix u64 | Cada 5 s a todas las sesiones; solo el reloj técnico (desfase `0x6D69438`, handler `0xBDF310`) | G: 100 en S16, crecientes [V] |
| `SetFieldTimeReq` 2125 | C→S | 36 B | Petición de dormir o esperar; el mod la bloquea para G (`session2.cpp:208-211`) | 0 [V] |
| `SyncWeatherStateReq` 2560 | C→S | 6 × f32 | Solo cuando cambia la clasificación local; escribe `field+0x178` (`0x20E6FE0`, **sin comprobar el dueño**) | H 9, G 11 en S16; **no se bloquea** [V] |

**[V] No existe ningún mensaje S→C de clima** en el CSV ni en las capturas.

## 2. Mecanismo en el código

- **Reloj visible (cliente).** `0xA384A0` corre en cada frame (desde `0x9406C0`) sobre el gestor
  `mgr = [[0x6D69190]+0x28]` (= mundo+0x1148). Solo trabaja si `+0x28 != 0` y `+0x2B == 0`, y lee
  `GetCurrentFieldTime(0x20E55A0)` del campo cliente:
  - hora fija del nivel;
  - si `field+0x17C == 0`, extrapolación del global `0x6A6D218` con `now = FrameClock + desfase(2188)`;
  - si `+0x29 != 0`, en su lugar un **calendario privado** en `+0x30..+0x4F` (escala `+0x46`).

  El resultado va al cielo (`world.vfF8` en `0xA386E5`) y a `UITimeOfDayEvent` [E].
- **2144 en el cliente.** `0xB87B90` busca el actor por la clave del paquete y llama a `.xtls 0x9CE7940`, que en
  `0x9CE7B03` hace `0xA38B00(mgr, ft, inmediato=1)`. De ahí pasa a `0x21A1CC0` y luego a `.xtls 0xE52E900`:
  escribe `TS+0x450` y **el global cliente en `0xE52E97E` sin comprobar el dueño** [E/E-x].
- **2762 en el cliente.** `0xB8BB00` llama a `.xtls 0x9CEAC50`, que hace:
  - `0x219D7F0(TS,…,flag del paquete, &now del paquete)` en `0x9CEAD20`;
  - si va bien, **activa el gestor**: `mov byte [rcx+0x28],1` en `0x9CEAD7B` [E-x].

  Con flag≠0, `0x219D7F0` hace `TS+0x468 = now`, copia `TS+0x450` al global y pone `field+0x17C = 0` [E].
- **Pausa de carga.** El cargador cliente `0x77AE70` hace `field+0x17C = 1` en `0x77AFE9` solo si
  `loader+0x162 != 0`. Ese mismo byte es el flag del 2445 [E]. Por eso el 2762 con flag 1 es el que
  **despausa** el campo.
- **Servidor.** `0x2BCAF00` toma `now = FrameClock`, llama a `0x219D7F0` con el flag (que el `clock gate`
  cambia a 0 para el usuario 2) y serializa el Ack con el **byte original**. Por eso G recibe 1 [E, V 2762=1].
- **2816 en el cliente.** `0xB88990` pasa a `.xtls 0xA41D560`, que llama sin condiciones a
  `0xA38B00([[0x6D69188]+0x30]+0x1148, &ft, 1)`. El `vf150` del `ClientTransformSync` es `0x359B50`, que es
  un `ret` [E/E-x].
- **Salida del calendario privado.** `0xA384A0` llama a `0xA38B00(…, inmediato=0)`, que en `0xA38BEE` pide la
  hora al servidor (`0xB7AFC0` → `.xtls 0xA3DD1E0`, con `R8 = ft`). Es la vía de 2125, y para G está bloqueada
  [E]. Quién activa `+0x29` no se ha localizado. El candidato es el evento de secuenciador
  `GamePlayEvent_SetTimeOfDay`, cuyo parser `0xC432B0` lee `TimeOfDayEventType`, `TimeSpeed`, `Hour`, `Minute`,
  `IsFixTime`… [V cadenas, H ejecutor].
- **Clima.** `0x9ADF20` (campo cliente) lee el estado de `[[0x6D69198]+0x38]+0x38`: lluvia en
  `0x3DC3A10` (+0x16C), nieve en `0x3DC3AC0` (+0x168), viento en `0x3DC3B70`. Calcula los flags y emite 2560
  si cambian. Los umbrales están a 0 en el fichero; por los datos rondan 0,45, 0,15 y 4,5 [E/V].
  **API nativa de forzado** (la del buff `ClientChangeWeatherBuffProcessor`, `0x931DC0`): `env=[[0x6D69190]+0xE0]`,
  `ctrl=env->vf168()`, `ctrl->vf8(claveClima)`; con `vf8(0)` se libera. Solo la usa ese buff [V barrido, E].

## 3. Por qué G se queda atrás (preguntas 2 y 3)

**A. Pérdida de la carga propia [V+E].** G aplica la base fresca (anclada en 194007) y su 2762 con flag 1 la
re-ancla en 211620. Pierde 17,6 s reales, que son **211 s de juego**: 3:31 en S16, ~20 s reales en S21/S22. Es
la semántica nativa de "el mundo se para mientras cargas", pero aquí solo se aplica en el lado de G.

**B. Sin seguimiento posterior [V].** Nadie le reenvía a G las publicaciones del reloj mundial:
- las cargas completas de H: su 2762 con flag 1 re-ancla el mundo;
- los cambios de campo de H: `0x2BC0A30` instala la hora de la tabla personal;
- los sueños de H: 2816 `ToNear`.

En S15–S23 H no hizo ninguna de estas cosas después de entrar G, así que **la deriva medible es solo A**.

**C. Clima [V].** En el eje del tick, S16:

| Evento (umbral) | H (tick) | G (tick) | Δ |
|---|---|---|---|
| [3] ≥ .45 y [4] ≥ .15 | 210349 | 270339 | **+59,99 s** |
| lluvia > 0, [3] .5751, [4] .5038 | 245976 | 305985 | **+60,01 s** |
| H viaja (2437 357046) → **reinicio de G** (lluvia 0,5→0, `WetnessReq` 0) | — | 357218 / 357250 | +0,17 s |
| H viaja (519895) → reinicio de G | — | 520163 | +0,27 s |
| tras los reinicios: [4] ≥ .15 | 495785 | 495824 | **+0,04 s** |

En S21 y S22 los cruces coinciden con 0,45 s y 0,65 s de diferencia.

- **[E]** El clima evoluciona con el reloj compartido (tick) y la posición. Su "fase" depende de cuándo se
  reinició. En S16 G arrancó un paso de 60 s detrás. Que el paso sea de 60 s es una hipótesis [H].
- **[V]** El reinicio de G llega a los ~170 ms de la ola que recibe G cuando H viaja: 416 `RemoveActorAck`,
  3243 `UnloadSyncSceneObjectAck` y 239 `ClearAllyGroupDataAck`, las mismas cifras que recibe H.
  Es el observador ligado a la vista de H (pendiente nº 4).
- **[V]** El primer viaje propio de G (tick 325737) **no** reinició su clima: no hay 2560 hasta el viaje de H.

**D. `clock gate` [V].** No congela nada en el servidor: la escala sigue en 12 y el usuario 2 no hace ninguna
publicación.

**E. Lo que no se puede excluir sin una sonda [H].** Un congelamiento local en G por cualquiera de estas causas:
- `+0x29` (calendario privado activado por una escena de H que se reproduce en G);
- `+0x2B`;
- `field+0x17C` que se queda en 1.

Solo eso haría literal la frase "G ve la hora de la carga". Las capturas no lo muestran: G no envía su hora.

**Pregunta 3, teletransporte de G [V/E].** En un viaje dentro del campo no hay 2144 y el flag es 0. Por tanto,
de forma nativa, no hay re-ancla, ni pausa, ni reinicio del calendario. El "reinicio" que se ve encaja con los
reinicios de clima que provocan los viajes de H. En S16, H viajó 31 s después del primer viaje de G y 17 s
antes del segundo.

## 4. Causa raíz

**[E]** El entorno de G (calendario y clima) se inicializa una vez y luego se extrapola o simula en local, sin
autoridad continua de H. Los dos únicos acoplamientos son:
- el tick de 2188, que alinea el "ahora" pero no la base del calendario;
- el reparto de la ola de descarga del observador, que reinicia el clima de G cuando viaja H.

## 5. Arreglo: entorno con autoridad del anfitrión

### 5.1 Hora (implementar)

- **T1.** En `take_frame` (`server.cpp:413`, en el hilo del servidor que envía), justo después de encolar un
  **2762 hacia la sesión 2 viva**:
  1. leer los 32 B del global servidor `0x6A6D238` dos veces y exigir que coincidan;
  2. serializar a 31 B (`<5IBHQ`, sin el relleno +0x15);
  3. encolar `00 0B FF 23 00 | u32 clave de personaje del usuario 2 | FT31` con `push_outgoing_extra`, igual que
     hace `horse_follow`.

  **El orden importa:** debe ir *después* del 2762, porque `0x9CEAC50` re-ancla con flag 1. Como el ancla está
  en el dominio del tick que G ya sigue con 2188, G queda en la misma línea que el servidor (error ≈ desfase de
  2188, menos de 1 s de juego).
- **T2.** En `watch_thread` (`clock_gate.cpp:107`): si el global cambia y el usuario 2 está dentro, se envía el
  mismo 2816. La cola tiene su propio cerrojo y el envío vale desde cualquier hilo. Si lo rechaza el presupuesto,
  se marca como pendiente y se reintenta. Cubre las cargas y los cambios de campo de H, y los sueños de H cuando
  el 2816 nativo no le llega a G por `ToNear`. Un duplicado no hace daño: es la misma hora.
- **No** cambiar el flag del 2762 a 0 [E]. Con flag 0, `field+0x17C`, que `0x77AFE9` puso a 1, no se despausa, y
  el calendario de G quedaría congelado *para siempre*.
- **Ganchos:** ninguno nuevo en el juego. Se mantienen los del `clock gate`, con los bytes verificados en la copia:
  - `0x219D7F0`: `48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 60 …`
  - `0x2BC0A30`: `48 89 5C 24 10 … 48 8D 6C 24 C9 48 81 EC C0 00 00 00 …`

  `take_frame` es el `vf9` propio de la sesión 2. El protocolo del mod no cambia (es un paquete nativo dentro del
  túnel), así que no hay que subir `kVersion`.
- **Riesgos:**
  - cada 2816 llama a `0xA39330` (cielo) y al evento de interfaz; con la misma hora no se nota;
  - tras una carga completa de H, G salta atrás lo mismo que H (semántica nativa);
  - si G está en calendario privado (+0x29), el empuje no se ve hasta que salga (§3E).

### 5.2 Clima (implementar ya)

- **W1.** Añadir 2560 a `never_from_guest` (`session2.cpp:209`). G deja de escribir los flags de clima del
  campo del anfitrión. Esos flags los consumen la IA y los horarios de NPC; los visuales no.
- **W2.** Los reinicios de clima en G al viajar H son un síntoma de la ola de descarga del observador. Se arreglan
  en el pendiente nº 4 (que G conserve su vista), no con un parche de clima.

### 5.3 Clima (condicionado a medir)

Si con la hora sincronizada, W1 y W2 sigue habiendo un desfase de fase (como los 60 s de S16), la única
aplicación nativa encontrada es `ctrl->vf8(clave)`. Eso exige saber qué clave corresponde al clima natural de
H [H]. No se recomienda hasta medir.

## 6. Verificación

- **En un PC (H y `cdmp_peer` como usuario 2):**
  - en la grabación del peer, cada 2762 va seguido de un 2816 con la FT igual a la que registra H para
    `0x6A6D238`;
  - tras una recarga completa de H aparece un 2816 nuevo con la base re-anclada;
  - una sonda de solo lectura del cliente H (`0x6A6D218`, `field+0x17C`, `mgr+0x28..+0x2B`) confirma la
    semántica nativa: pausa durante la carga completa y activación en el 2762.
- **Con dos PC:** una sonda de solo lectura en G cada 5 s y con cada 2144, 2762, 2816 y 2643. Registra:
  - `0x6A6D218` y `TS+0x450`;
  - `field+0x17C`;
  - `mgr+0x28..+0x50`;
  - `0x6D69438`;
  - el calendario calculado (con el `toRaw`/`fromRaw` del mod);
  - los 6 valores del clima a 1 Hz, en ambos PC.

  Guion de la prueba:
  1. H espera 90 s;
  2. entra G;
  3. 60 s juntos;
  4. viaja solo G;
  5. viaja solo H;
  6. captura de pantalla en los dos PC en el mismo tick.

  Criterios de éxito:
  - |cal_G − cal_H| ≤ 2 s de juego en todas las muestras después del primer empuje;
  - `field+0x17C = 0` fuera de las cargas y `+0x28 = 1` tras cada 2762;
  - 0 paquetes 2560 de G que lleguen al servidor.

  También se responde aquí al §3E.

## 7. Contraste con el informe externo

1. **"2144 fresco": de acuerdo [V].** Los mismos números (13:59:53.952, ancla 194007).
2. **"Contrato 1→0 frente a 2762 = 1": de acuerdo con los hechos.** Matizo:
   - en el juego normal la pérdida es simétrica por diseño;
   - su efecto es pequeño (3:31) y no explica el cielo de S16;
   - forzar 0 no "puede" pausar: **pausa con seguridad**, porque el byte del 2445 es `loader+0x162`, el mismo que
     activa la pausa en `0x77AFE9`.
3. **"Los viajes usan 0 y no llevan 2144": de acuerdo [V].**
4. **Selector y calendario privado: de acuerdo.** Añado:
   - la activación del gestor en el 2762 (`0x9CEAD7B`);
   - que la salida del modo privado pide 2125, bloqueado para G;
   - el candidato `SetTimeOfDay`.

   Tampoco localizo el escritor de `+0x29`.
5. **Clima cliente y 2560 sin comprobar el dueño: de acuerdo.** Discrepo en que sea "solo coincidencia temporal":
   - G recibe *la misma ola* de descarga (416/3243/239) y su clima y su humedad se reinician a ~170 ms;
   - añado la sincronía medida (0,04–0,65 s) y el desfase exacto de 60 s.
6. **Arreglo: comparto la autoridad H→G y la aplicación nativa.** Discrepo en la complejidad. No hace falta una
   petición G→H, ni barreras, ni generaciones: un 2816 encolado tras el 2762 en la misma cola FIFO del mismo
   hilo ya es la transacción. La fuente es el global servidor (base y ancla en el dominio del tick), sin llamar al
   getter desde un hilo que no sea del servidor.

   Sus cabeceras de 15 B para `0x219D7F0` y `0x2BC0A30` coinciden con las mías [V].

## 8. Lo que sigue sin saberse

- Si G entra alguna vez en calendario privado o en `+0x2B` por escenas de H [H].
- El ejecutor de `GamePlayEvent_SetTimeOfDay` [H].
- Qué objeto descargado reinicia el clima: volúmenes de escena, regiones o *triggers* [H].
- La regla de fase o paso del simulador de clima (60 s) y la clave del clima natural para `vf8` [H].
- El efecto visual exacto en G: hace falta la sonda de §6.

## 9. Implementado (sesión 25)

Build **1.0.0.2976**. Código: `src/mod/session2/clock_push.{h,cpp}` (T1, T2 y la sonda), la lógica pura en
`clock_push_logic.h` (tests en `tests/optd_clock_push_tests.cpp`), T1 en `take_frame` (`server.cpp`), T2 en el
`watch_thread` de `clock_gate.cpp`, W1 en `offer()` (`session2.cpp`). Ningún gancho nuevo en el juego, el flag del
2762 no se toca y el protocolo del mod no cambia (es un paquete nativo dentro del túnel): `kVersion` sigue igual.

### 9.1 Claves (`[session2]`, todas 0 por defecto)

| Clave | Dónde | Valores | Qué hace |
|---|---|---|---|
| `user2_clock_push` | anfitrión (necesita `create_session=1` y `clock_gate=1` instalado) | `0` \| `probe` \| `1` | **T1**: justo detrás de cada `LoadingCompleteAck` 2762 encolado para la sesión 2 viva, un `SetFieldTimeAck` 2816 nativo con el reloj del mundo del servidor (`0x6A6D238`, dos lecturas iguales). **T2**: el mismo 2816 cuando ese reloj cambia con el usuario 2 en el mundo (fase `Stable`: su 2762 escrito y ningún movimiento pedido después), asentado 900 ms (una muestra de 1 s sin otro cambio), como mucho uno cada 2 s, y el reintento si el de T1 no llegó a entrar (la cola lo rechazó: solo entra si está por debajo de la mitad de su presupuesto; o T1 salió antes, §9.7). Cada 2762 olvida lo que se le había dado al usuario 2. `probe`: las mismas decisiones y líneas, no se envía nada. |
| `block_guest_weather` | anfitrión (`create_session=1`) | `0` \| `1` | **W1**: el `SyncWeatherStateReq` 2560 del usuario 2 se descarta en `offer()` y se cuenta. El servidor no contesta nada a un 2560 (`0x29C5DF0` solo llama a `0x20E6FE0`), así que el cliente de G no espera respuesta. |
| `clock_probe` | anfitrión e invitado | `0` \| `1` | Solo lectura (lecturas protegidas, sin ganchos ni llamadas): una línea cada 5 s con los dos relojes, el gestor de hora y el desfase 2188. En G, además, cuántos 2816/2762 se entregaron a su cliente y el último 2816. |

Inis: `cdmp.p8-s7-host.ini` y `cdmp.p8-noche-local.ini` → `user2_clock_push=probe`, `block_guest_weather=1`,
`clock_probe=1`; `cdmp.p8-s7-shadow.ini`, `cdmp.p8-s7-shadow-fase2.ini` y el plan B `cdmp.p8-s12-shadow-seguro.ini` →
`clock_probe=1` (solo lectura). Para enviar de verdad: `user2_clock_push=1` en el anfitrión.

### 9.2 Formato del 2816, comprobado [V]

No hay ningún 2816 en las capturas S15–S22, así que el formato se comprobó en Ghidra (proyecto de `D:\RE`, solo lectura):

- **Escritor del servidor** `0x299AED0` (`SetFieldTimeAck_BroadcastToNear`): cabecera `00 0B FF`, `u16` de longitud en +3,
  `u32 [actor+0x60]` (la clave del personaje) y `FUN_1420CF330` = `FieldTime::Serialize`: `+0x00..+0x10` cinco `u32`,
  el byte de `+0x14`, el `u16` de `+0x16`, el `u64` de `+0x18`. El relleno `+0x15` no viaja: **31 B**.
- **Lector del cliente** `.xtls 0xA41D560`: `DeserializerMemory::Read` (la clave) y `FieldTime::Deserialize` `0x20CF3F0`
  (→ `.xtls 0xE2B2260`, los mismos campos en el mismo orden); exige consumir **exactamente** los 35 B; busca el actor
  (si lo encuentra, `vf150(0)` del TS si notifica) y llama **siempre** a `0xA38B00([[0x6D69188]+0x30]+0x1148, &ft, 1)`.
- **2762 en el cliente** `.xtls 0x9CEAC50`: `LoadingComplete_ApplyTime(…, flag del paquete, now)` y, si va bien,
  `mgr+0x28 = 1`. Por eso el 2816 va **detrás** del 2762 en la misma cola.
- **Qué clave espera el cliente [V]: da igual para la hora.** En `.xtls 0xA41D560` la clave solo sirve para
  `ActorManager::FindByKey`. Si encuentra el actor, se llama a `Actor_NotifyTransformSync` (`0x466060`, que solo mira el
  tipo de actor) y, si toca, al `vf150(0)` de su TS. En `pa::ClientTransformSyncActorComponent::vftable` (`0x55BF248`)
  ese hueco (+0x150, índice 42) es `0x359B50` = `ret 0`, es decir, nada. Además, se guarda el actor en el contexto del
  paquete (`*param_3`). `0xA38B00` se llama después, **fuera** del `if` del actor, y con el gestor global
  `[[0x6D69188]+0x30]+0x1148`, no con el actor. Así que la hora se aplica con cualquier clave, también con una que no
  exista. Es coherente con el servidor nativo: `0x299AED0` difunde con `ToNear` la clave del actor cuya hora cambió, y
  los demás clientes cercanos la reciben con una clave que no es la suya. El mod usa la clave del personaje del
  usuario 2 (su 2756) y, si aún no la conoce, el actor del 2762. En la práctica las dos coinciden (el 2762 va por
  `ToTarget` al personaje del usuario 2); el contador `2762 actor != user 2's character` lo vigila sin cambiar nada.

Paquete (40 B): `00 0B FF 23 00 | u32 clave | día, hora, min, s, ms (5×u32) | u8 | u16 escala | u64 ancla`. El test
`test_wire_byte_exact` lo compara byte a byte con la hora de S16 (día 6 13:59:53.952, ×12, ancla 194007, clave
`A0100303`) y comprueba que `troc::parse_wire` lo acepta como un paquete S→C entero. `test_decode_and_t0` reproduce la
igualdad de §0.1 (t0 de 13:37:28.728 @81905 = t0 de 13:59:53.952 @194007 = 566 465 868 ms) y la pérdida de la carga
propia de G (re-ancla en 211620: **211 356 ms** de juego, los 3:31 de S16).

### 9.3 Líneas del log

- Al instalar: `Session2 clock push ON|PROBE (user2_clock_push=…): …; world clock now …` y, con W1,
  `Session2 W1 ON (block_guest_weather=1): …`.
- T1, una por 2762 del usuario 2: `Session2 clock push [1|probe] T1: user 2's 2762 (actor 0x…, flag F, now N) queued ->
  2816 for 0x… QUEUED right behind it | WOULD queue (probe: nothing sent) | NOT queued (…; T2 tries again): world clock
  day D HH:MM:SS.mmm xS @ancla (t0 T ms) (its anchor X ms before the 2762's now)`. Sin 2816: `NO 2816: … unreadable …`,
  `… not a running calendar (…)` (p. ej. el día 0 24:00 ×0 del aparcamiento) o `… character key unknown`.
- T2: `Session2 clock push [..] T2: the world clock changed | user 2 has not been given the clock since its last 2762
  (T1 refused or returned early) while user 2 is in the world -> 2816 for 0x… QUEUED | WOULD queue | NOT queued: world
  clock …`.
- Máximo 128 líneas de T1/T2; luego, solo contadores en el informe de `session2`:
  `clock push [modo]: T1 2762s N -> 2816 queued Q would W refused R (unreadable clock, implausible, no key, 2762 actor !=
  user 2's character) | T2 queued … (unsettled samples …)` y `guest weather Req 2560 dropped N` (W1; la primera vez,
  también `Session2 W1: user 2's SyncWeatherStateReq 2560 dropped …`).
- Sonda: `ClockProbe [host|guest]: client clock exe+0x6A6D218 … (t0 …) | server clock exe+0x6A6D238 … (t0 …) | time
  manager +28 active A +29 private P +2A … +2B … | private clock … | +50 … | 2188 bias N ms` y, al final,
  `| user 2 in the world yes|no` (H) o `| handed to the client: 2816 N (last for 0x…: …), 2762 M` (G). Si no se puede
  leer nada (pantalla de título), solo una línea hasta que cambie.

**t0** = base − ancla × escala: la hora del calendario en el tick 0 del servidor. Dos relojes que marcan lo mismo en el
mismo tick tienen el mismo t0, sean cuales sean su base y su ancla. Es el número que se compara entre los dos PC
(modelo lineal: vale para dos copias de la misma base o dentro de un tramo de día, §7.2).

### 9.4 Comprobación en un PC (anfitrión + `cdmp_peer`)

1. `cdmp.p8-noche-local.ini` (T1/T2 en `probe`): tras cada 2762 del usuario 2 sale una línea `T1 … WOULD queue` cuya
   `world clock` coincide con la `server clock` de la `ClockProbe [host]` más cercana (y con la última línea
   `Session2 clock watch` del `clock gate`). En el informe, `T1 2762s` = `would`.
2. Con `user2_clock_push=1` y `cdmp_peer --mode replay … --record FICHERO`: en la grabación, **cada 2762 va seguido
   inmediatamente de un 2816 de 40 B** cuyos 31 B de FieldTime son los de `0x6A6D238` que registra la línea T1; en el
   informe, `queued` = `T1 2762s` y `refused` 0.
3. Una recarga completa del anfitrión (o un cambio de campo) con el usuario 2 dentro: una línea `T2 … the world clock
   changed … QUEUED` con la base re-anclada (la misma que imprime `Session2 clock watch`).
4. W1: el peer no envía 2560; basta con ver `Session2 W1 ON` y `guest weather Req 2560 dropped 0`.

### 9.5 Comprobación con dos PC (H anfitrión, G Shadow)

Guion de §6: H espera 90 s; entra G; 60 s juntos; viaja solo G; viaja solo H; captura de pantalla en los dos PC a la
vez. Primero con `user2_clock_push=probe` (los inis de la sesión 25) para medir el estado actual y después con `1`.

- **Criterio de hora:** en cada `ClockProbe [guest]` posterior al primer 2816, |t0 del *client clock* de G − t0 del
  *server clock* de H| ≤ **2000 ms** de juego. Con `probe` se espera la diferencia de la carga propia (~211 s en S16);
  con `1`, ≈ 0 (el error que queda es el del desfase 2188, < 1 s de juego). El `2188 bias` de G debe ser estable.
- `+28 active 1` tras cada 2762 y `+29 private 0` fuera de escenas (si `+29` se queda en 1, es el caso de §3E: el
  empuje no se ve hasta que salga del calendario privado).
- En G, `handed to the client: 2816` crece con cada 2762 (y con cada T2 de H).
- **W1:** `guest weather Req 2560 dropped` > 0 en H (G emitió 11 en S16) y ningún 2560 de G llega al servidor.

### 9.6 Lo que queda del clima

- **W2** no está hecho: los reinicios de clima de G cuando viaja H son la ola de descarga del observador (416/3243/239);
  van con el pendiente nº 4 del mundo alrededor del invitado (que G conserve su vista), no con un parche de clima.
- **Aplicación nativa del clima:** no se ha encontrado ninguna salvo `ClientChangeWeatherBuffProcessor` (`0x931DC0`,
  `ctrl->vf8(clave)`), que exige saber la clave del clima natural de H y no se recomienda (§5.3). Queda condicionada a
  medir, con la hora ya sincronizada, si sigue el desfase de fase (los 60 s de S16).
- W1 solo cierra la vía G→H (los flags del campo que leen la IA y los horarios); no cambia el cielo de G.

### 9.7 Riesgos

- El 2816 hace que el cliente de G llame a `0xA39330` (cielo) y al evento de interfaz; con la misma hora no se nota,
  pero tras una recarga completa de H, G salta atrás lo mismo que H (semántica nativa).
- T1 también sigue al 2762 del **aparcamiento** (la primera carga de G): G recibe la hora del mundo mientras su campo
  privado tiene hora fija; el getter del nivel manda ahí y el siguiente 2762 (campo compartido) la vuelve a dar.
- T2 lee la fase `Stable` y encola un instante después: si G empieza a cargar justo entre las dos cosas, un 2816 puede
  llegar al principio de su carga. Es la misma hora y el T1 de esa carga la vuelve a poner.
- Si G está en calendario privado (`+0x29`), el empuje no se verá hasta que salga (§3E).
- Si la cola de salida pasa de la mitad de su presupuesto justo en el 2762 (ráfaga de login), T1 no entra. Cada 2762
  re-ancla al invitado, así que T1 empieza **olvidando** lo que se le había dado (`PushWatch::forget`). Un rechazo
  (`failed`) o una salida temprana de T1 (sin clave, reloj ilegible o no plausible) lo dejan como «no dado». Entonces
  T2 le da el reloj en cuanto está asentado y han pasado 2 s desde el último intento, aunque sea el mismo valor que el
  último 2816 que sí entró. (En 8047e5c, `failed` no lo olvidaba y T2 se quedaba en «ya dado»: corregido, con tests.)
- Entre ese `forget` y el registro de lo que hace T1 hay unos microsegundos en los que T2 (una vez por segundo) podría
  encolar el mismo 2816. Saldría duplicado, con la misma hora: no hace daño.
