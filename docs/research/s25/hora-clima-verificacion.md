# Hora y clima en la sesión 25 (P8): verificación con los logs

Build **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`). Análisis de solo lectura del 2026-10-12: no se ha
tocado el código, no se ha lanzado el juego y no se ha hecho ningún commit. H = anfitrión (PC del usuario),
G = invitado (Shadow, usuario 2).

**Fuentes**
- **G, paso 1** (`clock_probe=1`, `weather_sync=probe`): `cdmp_20261012-054004.log` y `-060436.log` (dos entradas).
- **G, paso 2** (`weather_sync=1`): `-061831`, `-062315`, `-062751` y `-062954` (cuatro entradas cortas).
- **H, paso 1** (`user2_clock_push=1`, `block_guest_weather=1`, `clock_probe=1`, `weather_sync=probe`):
  `C:\Emulador CD\backups\p8-sesion25-host\cdmp_20261012-063736.log`.
- **H, paso 2** (lo mismo, con `weather_sync=1`): `bin64\cdmp_20261012-071438.log`.
- **Capturas de paquetes de G**, solo los 2144, 2445, 2762 y 2816.

**Alineación**
- Reloj de pared: hora de H = hora de G + 1 h − 1,011 s.
- Las comparaciones de clima se hacen en el eje del tick del servidor. El valor de H se interpola linealmente
  entre sus dos líneas de 5 s que rodean el tick de G.

## 0. Veredicto

| Pieza | Veredicto | En una línea |
|---|---|---|
| Hora, T1 (2816 detrás de cada 2762) | **Funciona** | 12 de 12 empujes encolados y entregados en el mismo ms que su 2762. El t0 del reloj cliente de G es **idéntico** al t0 del reloj servidor de H (Δ = 0 ms) en las 6 entradas, sin deriva en 22,5 min. |
| Hora, error residual | **Bien** | Solo queda el error del tick que G deduce del 2188: mediana 50–120 ms de retraso (≈ 0,6–1,4 s de juego). Hay picos de 25–27 s, pero solo mientras el bucle principal de G estaba congelado, y se corrigen con el siguiente 2188. |
| Hora, T2 (el reloj del mundo cambia con G dentro) | **No probado** | El reloj de H no cambió nunca con G dentro. T2 = 0 en las dos pruebas. |
| W1 (bloquear el 2560 de G) | **Funciona** | 17 + 9 paquetes 2560 de G descartados. |
| Clima nativo (paso 1, `probe`) | **Parcial** | Primera entrada: casi igual que H (3 % de muestras fuera de umbral). Segunda entrada: el 53 % fuera de umbral durante unos 3,5 min (Δnubosidad hasta 0,15, Δviento hasta 4). La posición de las nubes es siempre distinta (600–6 150 unidades). No llovió en ningún momento. |
| Clima en modo visor (paso 2, `weather_sync=1`) | **Funciona en lo probado** | Modo visor activo en las 4 entradas: unos 64 s y unos 3 240 frames en total, sin fallos, sin desalojos y sin muestras malas. Diferencias con H: nubosidad ≤ 0,0005, viento ≤ 0,013, A8 ≤ 0,0002; B y C idénticos; nubes ≈ 0,3 s por detrás. **Sin probar:** lluvia, nieve, humedad, puerta de radio y viajes de H con el modo visor activo. |
| Errores y efectos secundarios | **Sin errores** | Ninguna línea ERR ni FAILED de estos módulos. Hay dos fallos solo de log, una carrera latente y una holgura: H sigue «viendo» al usuario 2 en el mundo después de que este se despida (§3). |

## 1. Hora

### 1.1 Qué hizo el anfitrión

- Paso 1: `T1 2762s 4 -> 2816 queued 4 would 0 refused 0 (unreadable clock 0, implausible 0, no key 0, 2762 actor != user 2's character 0) | T2 queued 0 … (unsettled samples 0, no key 0)`.
- Paso 2: lo mismo con **8/8**.
- Hubo dos T1 por entrada: el aparcamiento y el campo compartido. G no hizo ningún viaje rápido propio en
  ninguna de las dos pruebas.
- **Reloj del mundo de H:**
  - paso 1: `day 6 13:37:37.260 x12 @81807` (t0 566 475 576 ms) durante toda la prueba;
  - paso 2: `day 6 13:37:38.688 x12 @94096` (t0 566 329 536 ms).
- Las únicas publicaciones del reloj del mundo fueron las del arranque de H, antes de que entrara G (`clock watch`:
  4 publicaciones, 2 congeladas). La re-ancla de su propia carga le costó 3 min 26 s y 3 min 31 s de juego,
  respectivamente; es la semántica nativa.
- El `clock gate` informa `user 2 travels 4/8, loads 4/7 (flag cut), world clock moved during them 0`.
- H hizo **un viaje rápido** en el paso 1 (2437 → 2643 → 2762 con flag 0, a las 06:53:30 de H). Ese viaje no
  publica el reloj, así que no hubo T2. Es lo esperado.

### 1.2 Reloj de G frente a H

| Log de G | Paso | 2816 entregados | t0 cliente G | t0 servidor H | Δt0 | Pérdida evitada por T1* |
|---|---|---|---|---|---|---|
| 054004 | 1 | 2 | 566 475 576 | 566 475 576 | **0** | 20 074 ms × 12 = **4 min 01 s** |
| 060436 | 1 | 2 | 566 475 576 | 566 475 576 | **0** | 17 916 ms × 12 = 3 min 35 s |
| 061831 | 2 | 2 | 566 329 536 | 566 329 536 | **0** | 18 361 ms × 12 = 3 min 40 s |
| 062315 | 2 | 2 | 566 329 536 | 566 329 536 | **0** | 18 334 ms × 12 = 3 min 40 s |
| 062751 | 2 | 2 | 566 329 536 | 566 329 536 | **0** | 18 213 ms × 12 = 3 min 39 s |
| 062954 | 2 | 2 | 566 329 536 | 566 329 536 | **0** | 17 992 ms × 12 = 3 min 36 s |

\* La pérdida es lo que el 2762 con flag 1 re-ancla, desde el ancla del 2144 hasta el `now` del 2762 (mecanismo de
`clima-hora.md` §3A). Por ejemplo, en 061831 el 2144 trae `14:22:18.636 @317425` y el 2762 trae `now 335786`. El
2816 llega **en el mismo milisegundo** que el 2762 (capturas: 49,040/49,040 y 71,503/71,503, y lo mismo en las 12
parejas), así que la sonda de 5 s nunca ve la re-ancla intermedia.

**Entre empujes**
- El reloj cliente de G no cambia: base `@81807` o `@94096` en todas las líneas.
- El 2144 del campo compartido trae una hora nueva (`14:05:46.080 @222542`, `19:00:28.560 @1696082`, …) con el
  **mismo t0**.
- `+28 active 1` después de cada 2762 y `+29 private 0`, `+2A 0`, `+2B 0` siempre: G no entró nunca en el calendario
  privado.
- **Deriva medida: ninguna.** En 054004, G estuvo 22,5 min (271 muestras) con t0 = t0 de H.
- Durante el paso 1 la hora del juego avanzó de unas 14:05 a unas 20:35: G vio el atardecer a la vez que H. Eso
  encaja con lo observado en vivo.

### 1.3 Error que queda: el «ahora» de G (2188)

La hora visible de G es t0 + (FrameClock + desfase 2188) × 12. El t0 es idéntico, así que la diferencia con H es
el error del tick que estima G. Se mide así: el `tick` de las líneas `WeatherSync` de G, menos el tick de H en el
mismo instante de pared.

| Log de G | Muestras | Mediana | p05 | Peor | En juego (mediana / p05) |
|---|---|---|---|---|---|
| 054004 | 278 | −50 ms | −333 ms | −25,2 s (06:04:26, al salir) | −0,6 s / −4 s |
| 060436 | 104 | −97 ms | −1,79 s | −27,0 s (06:09:07) | −1,2 s / −21 s |
| 061831 | 24 | −103 ms | −250 ms | −271 ms | −1,2 s / −3 s |
| 062315* | 16 | −99 ms | −445 ms | −445 ms | −1,2 s / −5 s |
| 062751 | 15 | −119 ms | −971 ms | −971 ms | −1,4 s / −12 s |
| 062954 | 17 | −120 ms | −293 ms | −293 ms | −1,4 s / −3,5 s |

\* Solo mientras jugaba en H. Después de despedirse, G cargó su propia partida (§3).

- **Picos.** Los −25 s y −27 s coinciden con congelaciones del bucle principal de G:
  - FreezeWatch #2 de 060436, de 06:08:40 a unas 06:09:05 (`UiPause: NO FRAMES for 10 s … game clock +0 ms`);
  - el cierre de 054004.
  
  Mientras dura la congelación no se pinta nada, y el siguiente 2188 (cada 5 s) corrige el desfase de golpe (+25 018
  ms a las 06:09:11). Al final de 060436 el desfase sube 1,5–2 s cada 5 s mientras G tiene el menú abierto para
  salir. Es nativo, no del mod.
- **Desfase 2188 de G.** Paso 1: 132 395–140 636 ms y 1 599 668–1 640 566 ms (con el salto de la congelación). En las
  entradas del paso 2 se mueve menos de ±0,7 s. El de H está entre −40 y 0 ms.
- La incertidumbre de la alineación de pared es de unos ±50 ms. Por eso una mediana de unos −100 ms es, en la
  práctica, la latencia del 2188.

### 1.4 Cargas y viajes

- **Cargas de entrada de G** (aparcamiento y campo compartido). T1 respondió a las 12. Al aparcamiento le da la hora
  del mundo, aunque ahí manda la hora fija del nivel (`day 0 24:00 x0` en el 2144). El segundo T1 la vuelve a poner.
- **Viajes de G:** no hubo, así que T1 con un 2762 de flag 0 no se probó.
- **Viaje rápido de H** (paso 1): sin publicación y sin salto, como se esperaba.
- **Carga completa, cambio de campo o sueño de H con G dentro:** no hubo. **T2 está sin probar.**
- **G vuelve a su propia partida** (062315, 06:25:21): su reloj pasa a su calendario (`day 83 19:23:59`). Los 2816 de
  H no llegaron a esa partida: en la captura, los 2762 de la partida local no llevan 2816 detrás.

### 1.5 W1

- `guest weather Req 2560 dropped`: **17** en el paso 1 y **9** en el paso 2.
- La primera vez sale la línea `Session2 W1: user 2's SyncWeatherStateReq 2560 dropped …`.
- Ningún 2560 de G llegó al servidor de H.

## 2. Clima

### 2.1 Paso 1 (`probe`): el clima nativo de G frente al de H

**Lo que coincide.** Los dos lados dan `same, vtable ok, viewer X+0x31 = 0`, `sanity 0` y `X+0x88` de 57–61.

**Diferencias** (G − H en el mismo tick, con G en el mundo):

| Entrada | Muestras | Nubosidad: \|Δ\| media / máx (> 0,02) | Viento: \|Δ\| media / máx (> 0,3) | cloudBaseDensity A8: \|Δ\| media / máx | Desplazamiento de nubes X+0x124 |
|---|---|---|---|---|---|
| 054004 | 269 | 0,006 / 0,249 (3 %) | 0,06 / 2,0 (3 %) | 0,004 / 0,150 | −633 a −656 (siempre) |
| 060436 | 91 | 0,054 / 0,149 (**53 %**) | 1,44 / 4,0 (**54 %**) | 0,012 / 0,150 | −5 740 a −6 150 (siempre) |

- **054004.** G solo se separa de H en dos momentos:
  - los primeros ~25 s después de entrar: valores por defecto 0,501 / 2,0 / 0,40 frente a 0,75 / 4,0 / 0,25 de H;
  - una muestra durante el viaje de H (06:53:30 de H). Ese viaje **reinició los dos climas a la vez**: H y G pasan
    a `X+0x88 57` y 0,501 / 2,0 / 0,4002 en la misma muestra de 5 s. Es W2, alineado de forma nativa.
  
  El resto del tiempo coinciden a ±0,004.
- **060436.** De 06:06:54 a 06:10:35, G sube a nubosidad 0,65 y viento 6,0 mientras H baja a 0,50 y 2,0. La
  diferencia máxima es +0,149 en nubosidad y +4,0 en viento. Después vuelven a coincidir (0,80 / 4,0).
- **Precipitación, lluvia, nieve y humedad:** 0 en los dos lados durante las dos pruebas enteras (máx. de
  nubosidad 0,80 y de viento 8,0). **No llovió.** Por eso se veía «bien»: tiempo seco y nubosidad parecida. Aun
  así, la posición de las nubes nunca fue la misma.

### 2.2 Paso 2 (`weather_sync=1`): modo visor

**Anfitrión**
- `to the guest: sent 1287 (guest caps 0x07; …, refused 0, samples not sane 0)` y `tunnel backlog 0`.
- Ritmo real: unos 4,4–4,8 Hz.

**Invitado**, en cada entrada:
1. `the tunnel also announces caps 0x04`;
2. gancho instalado y puerta abierta (`gate OPEN (apply)`);
3. `VIEWER MODE (X+0x31 0 -> 1)` con 55 palabras guardadas y `X+0x88 = 61`.

| Log de G | Gancho y puerta | Distancia a H | Modo visor (muestra, edad) | Fin | Frames aplicados |
|---|---|---|---|---|---|
| 061831 | 06:20:12.27 | 0 m | 06:20:22.00 (#1598, 0 ms) | 06:20:59.67, puerta cerrada al salir al título (2001 a las 06:20:55.95): `55 holder words put back, X+0x88 = 57` | 2302 |
| 062315 | 06:24:50.25 | 7 m | 06:25:00.40 (#2916, 15 ms) | 06:25:10.92, igual (salida al título) | 495 |
| 062751 | 06:29:24.69 | 13 m | 06:29:34.39 (#4211, 16 ms) | cierre del juego (2001 a las 06:29:46.8, «Terminate») todavía en modo visor | ≥ 298 |
| 062954 | 06:31:43.59 | 13 m | 06:31:53.60 (#4864, 16 ms) | cierre del juego (06:31:57.7) todavía en modo visor | ≥ 144 |

En total, unos 64 s con el modo visor activo. Contadores en todas las entradas: `faulted no (faults 0)`,
`evicted X 0`, `re-asserted 0`, `bad sample 0`, `samples in … (bad 0, insane 0)`, `foreign 0`, `busy 0` y
`X not the weather system 0`.

**Convergencia** (G − H interpolado en el mismo tick, líneas con `X+0x31 = 1`):

| Log de G | Muestras | Δnubosidad máx | Δviento máx | ΔA8 máx | Δdesplazamiento de nubes |
|---|---|---|---|---|---|
| 061831 | 7 | 0,0000 | 0,013 | 0,0000 | −0,9 a −1,5 |
| 062315 | 2 | 0,0000 | 0,000 | 0,0000 | −1,8 a −2,2 |
| 062751 | 2 | 0,0005 | 0,005 | 0,0001 | −0,3 a −0,6 |
| 062954 | 1 | 0,0005 | 0,000 | 0,0002 | −1,1 |

- El bloque B (sol, niebla de altura, capas D4..E0, altitud de nubes) y el C coinciden carácter por carácter en las
  líneas del mismo tick. Por ejemplo, G a las 06:20:44 frente a H con tick 359073.
- El desplazamiento que queda en las nubes son los 300 ms de retraso de la interpolación más unos 100 ms del tick de
  G: de 1 a 2 m de recorrido. No se nota.
- **Justo antes del modo visor**, en las mismas entradas, G tenía su propio clima: Δnubosidad de −0,15, Δviento de
  −0,2 a −5,0, ΔA8 de +0,07 a +0,15 y Δdesplazamiento de −1 700 a −4 800. El modo visor elimina exactamente esas
  diferencias.
- Precipitación y lluvia: 0 en los dos lados (`precipitation command 0.000`). **La lluvia en dos PC sigue sin
  probarse.** Solo está validada en un PC (P1, selftest).

### 2.3 Cargas, puerta de radio, desalojos y fallos

- **Entrada.** El gancho y la puerta se abren en el 2144 del campo compartido. `PlayerReader` da «en el mundo»
  ~9,5 s antes de que el cliente termine de cargar: 2445 en t = 71,42 s y 2762 en t = 71,50 s. Pero H no manda
  muestras hasta que el usuario 2 está en fase `Stable` (su 2762). Así que G cuenta unos 10 s de frames «stale»
  (613 / 625 / 611 / 629) con su propio clima, y el modo visor entra 0,3 s después del 2762. Eso queda **bajo la
  pantalla de carga**; probablemente no se ve.
- **Aparcamiento.** H mandó unas 19 muestras mientras G estaba aparcado. La puerta de G estaba cerrada (`not in the
  world`), así que G no escribió nada.
- **Salida al título.** La puerta se cierra y se libera en ese mismo frame. En la línea siguiente G muestra
  `X+0x31 = 0` y vuelve a sus valores (0,501 / 2,0 / 0,4002). El mezclador vuelve a mandar, porque `X+0x88 = 57`.
- **Cierre del juego en modo visor.** El proceso termina sin errores (FreezeWatch normal de la salida). No queda nada
  persistente.
- **X sobrevive a todas las cargas.** Solo hay un `X CHANGED` por ejecución (0 → X al arrancar) en los 8 logs. Eso
  incluye el viaje de H del paso 1 y la vuelta de G a su partida (062315).
- **Puerta de radio** (`weather_sync_radius_m = 2000`): distancias de 0 a 13 m al abrir y de 3 a 6 m al cerrar.
  Nunca se acercó al límite: **sin probar**.
- **Viaje de H con G en modo visor:** no ocurrió en el paso 2. **Sin probar.**

## 3. Avisos, errores y efectos secundarios

- **Errores y avisos de estos módulos:** ninguno (`FAILED`, `REFUSED`, `NOT sent`, `dropped:`, `worker stopped`:
  0 en los 8 logs).
- **ERR de G:** solo los de despedida y reconexión de la sesión 2.
- **H:** un único ERR, el cierre del túnel de la época 1.
- **FreezeWatch:** en G, todos son al salir o los dos del paso 1 (06:08:40, sin relación: `weather_sync` era de
  solo lectura y T1 solo actúa en el 2762). En H, uno al cerrar.

**A. Log del gancho: «0 bytes patched» (solo log)**
- `install_hook` (`weather_sync.cpp`, ~573–592) guarda `const auto& orig = h->original_bytes()`, hace
  `g_hook = std::move(*h)` y **después** escribe `orig.size()`. Lo que imprime es el tamaño del vector ya movido: 0.
  Pasa también en los dos selftest P1 de la noche.
- La comprobación de los 5/6/7/8/12/18 bytes se hizo **antes** del `move`; si no, habría salido `REFUSED`. El
  parche, por tanto, es correcto.
- **Arreglo:** guardar `orig.size()` en una variable local antes del `move` y escribir esa variable.

**B. Edad negativa en la línea de 5 s, y la misma carrera latente en el gancho**
- La línea de 5 s de G muestra `age 18446744073709551584 ms` y `…601 ms` (06:20:39, 06:27:15 y 06:27:20).
- Causa: `now` se lee al principio de la vuelta (`weather_sync.cpp:857`) y el buzón se lee después (`:945`). Si
  entra una muestra entre las dos lecturas, `now − recv_ms` desborda (`:997`).
- El gancho tiene la misma ventana: `mirror_pre` lee `now` (`:288`) antes de leer el buzón, y `wsy::fresh()`
  (`weather_sync_logic.h:375`) devuelve **falso** si `recv_ms > now`. Eso sería un frame «stale», con liberación y
  nueva aplicación (otra línea `VIEWER MODE`). No se vio nunca (`released 0` mientras aplicaba), pero puede pasar.
- **Arreglo:**
  - en `fresh()`, tratar `recv_ms > now_ms` como edad 0;
  - leer `now` después del buzón;
  - imprimir la edad con signo o recortada a 0;
  - añadir un test con `recv = now + 16`.

**C. El anfitrión cree que el usuario 2 sigue en el mundo después de despedirse (efecto real, inofensivo de momento)**
- `server_user2_in_world()` = `live_session2() && phase Stable` (`server.cpp:1239`). No tiene en cuenta la
  despedida.
- La `ClockProbe [host]` dice `user 2 in the world yes` en estos intervalos de H:
  - de 07:20:23 a 07:23:38, aunque G se despidió a las 07:20:55 y cerró el juego a las 07:21:12;
  - de 07:24:58 a 07:28:13, aunque G se despidió a las 07:25:09.
  
  Solo pasa a `no` cuando llega la siguiente conexión.
- **Medido:** en 062315, H mandó **734 muestras de clima** (contador 370 → 1104; G las recibe, #3025–#3674) mientras
  G ya jugaba **su propia partida**. G las ignoró (`gate: this game does not play on the host now`). Son unos
  1,3 KB/s sin efecto visible.
- **Riesgo:** T2 usa la misma señal (`clock_push_tick`) y podría encolar un 2816 para un usuario 2 que ya se ha ido.
  En esta prueba no pasó, porque el reloj no cambió. No se comprobó si G entrega a su cliente paquetes de H después
  de pasar a «local»: en la captura, la partida local no recibe ningún 2816.
- **Arreglo:**
  - `weather_sync_host_tick` y `clock_push_tick` deben exigir también `!server_logout_handed()`, como ya hacen
    `child_hold` y `observer_hold_acks`;
  - T2 debe exigir además el enlace conectado;
  - mejor aún, que `server_user2_in_world()` sea falso desde la despedida (2001 → 2002/2003) o con el túnel caído.
    Antes hay que revisar a sus otros usuarios.

**D. Detalles menores**
- El `X CHANGED` del arranque (0 → X) sale como **WRN**. No es un cambio real.
- La puerta «en el mundo» de G se abre durante la carga de G (en el 2144), no después del 2762. Hoy no tiene efecto,
  porque H no manda nada mientras el usuario 2 carga. Con el fallo C, en cambio, G podría escribir el clima de H
  durante sus propias cargas, aunque no tendría consecuencias.

**Efecto en otros sistemas: ninguno observado**
- El 2816 no provocó congelaciones, cambios de `+29`/`+2B` ni errores del juego.
- El modo visor detiene los 4 simuladores de G, el de la hora incluido, e ignora sus capas `GlobalEffect`. Es
  intencionado.
- Las muestras de los dos lados son sanas (`sanity 0`).

## 4. Qué hacer

1. **Arreglos de código** (por orden; ninguno toca el protocolo):
   - **C:** que el envío del clima y T2 no se hagan después de la despedida del usuario 2;
   - **B:** `fresh()` y la edad con signo, con su test;
   - **A:** el número de bytes del log del gancho.
2. **Pruebas que faltan con Shadow** (mismos inis del paso 2):
   - **T2:** con G dentro, H duerme o espera, carga una partida o cambia de campo. Debe salir `T2 … the world clock
     changed … QUEUED`, y en G la `ClockProbe` debe dar el t0 nuevo de H con Δ = 0.
   - **Viaje rápido de G** (2762 con flag 0): un T1 más y t0 igual.
   - **Lluvia o nieve** con modo visor: comparar precipitation / rain(16C) / snow(168) / humidity, y hacer capturas a
     la vez en los dos PC.
   - **Viaje rápido de H con G en modo visor:** G debe seguir el reinicio de H en ≤ 0,5 s.
   - **Radio:** G a más de 2,1 km cierra la puerta y vuelve a su clima; a menos de 1,9 km la vuelve a abrir.
   - **Corte del enlace:** `back to its own weather (… 1 s old or more)` en ≤ 1 s.
   - **Una sesión larga (≥ 15 min) en modo 1**, con capturas al atardecer en los dos PC.
3. **Opcional:** que la `ClockProbe [guest]` escriba también el tick estimado (FrameClock + 2188) y la edad del último
   2188. Así el error residual de §1.3 se mediría sin alinear relojes de pared entre los dos PC.

## Apéndice: método

- Scripts de Perl propios, en el scratchpad de la sesión (`vf\`), no en el repositorio:
  - `cp.pl`: resume las `ClockProbe`;
  - `wx.pl`: las líneas `WeatherSync` → TSV;
  - `ijoin.pl`: interpola H en el tick de G y saca la estadística;
  - `tickerr.pl`: error del tick de G frente al de H en el mismo instante de pared.
- Umbrales de «fuera de umbral»: nubosidad 0,02, viento 0,3, A8 0,01 y desplazamiento de nubes 20.
- Las capturas de paquetes solo se filtraron con `grep` (2144 / 2437 / 2445 / 2643 / 2762 / 2816).
