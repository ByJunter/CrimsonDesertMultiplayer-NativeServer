# P8: el hilo de red del invitado se atasca en la carga del campo (sesión 5)

Análisis y cambios del 9 de octubre de 2026 sobre la rama `opcion-d` (HEAD `4fda435`). El juego es la build
**1.0.0.2976** (base `0x140000000`) y todas las direcciones son **RVA**.

**[V]** = comprobado con log, medición, desensamblado o test, con la evidencia citada.
**[E]** = inferencia o hipótesis todavía no probada en el juego.

No se lanzó el juego, no se tocó `bin64` y no se hizo commit. `.\scripts\build.ps1` compila y pasa 17/17 tests
(§7).

Evidencia:
- invitado (Shadow, hora local = anfitrión − 1 h): `C:\Emulador CD\backups\p8-sesion5-shadow\cdmp.log`;
- anfitrión: `C:\Emulador CD\backups\logs\cdmp_20261009-2038_p8g-host.log`.

## 1. Causa: el recorrido de la lista de espera tardaba 2,5–8 s dentro del hilo de red **[V]**

El hilo de red del invitado (T1860) hace varias cosas en el mismo bucle: bombea Steam, entrega el túnel Troc y
escribe el informe de cada 5 s. Dentro de ese informe (`mod.cpp`, `run_network`) llamaba a:
- `guest_actor_diag_tick()`, con las líneas de restauración y el recorrido de la lista de espera;
- `guest_s3_tick()`;
- `guest_describe()`.

En el log, cada informe escribe `Net:` y `Local:`, y a continuación la línea `GuestDiag loading:`. Esa línea solo
sale al terminar el recorrido de los 193 keys. El tiempo entre `Local:` y `GuestDiag loading:` es, por tanto, lo
que duró el recorrido:

| hora invitado | estado | lista | resultado de Find | recorrido |
|---|---|---|---|---|
| 19:42:00–19:42:40 | 0x04–0x0C | 0 | — | 0,004–0,011 s |
| 19:42:48 | 0x0E | 193 | 80 listos, 113 no | **2,46 s** |
| 19:42:53 | 0x0E | 193 | 193 listos | **2,89 s** |
| 19:43:00 | 0x10 | 193 | 192 listos | **4,33 s** |
| 19:43:04 / 19:43:10 | 0x11 | 193 | 192 listos | **4,0 / 4,5 s** |
| 19:43:16 | 0x11 | 193 | 192 listos | **5,39 s** |
| 19:43:21 → 19:47:03 | 0x11 | 193 | 192 listos | **5,6–8,2 s** en cada informe |
| 19:47:20 → | 0x18 | 193 | 192–193 no encontrados | 0,6–0,76 s |

Como `last_report = now` se fija antes del recorrido, el siguiente informe salía en cuanto este terminaba. El
espaciado de 5,0 → 5,4 → 5,9 → 6,3 → 6,5–8 s **es** la duración del recorrido. Durante ese tiempo el hilo no leía
Steam, no contestaba pings y no entregaba el túnel: estaba fuera del bucle unos 6 s de cada 6 s.

Así se encadenan los hechos:
1. A las 19:43:10,954 empieza un recorrido de 5,39 s, que supera el `kTimeoutMs` de 5000 ms de `ClientSession`.
2. A las 20:43:16,748 (hora del anfitrión) este escribe `Session: peer timed out after 5000 ms` **[V]**.
3. El anfitrión es `Responder`: en `Connecting` ya no envía nada, así que el invitado deja de recibir (`in 0.0
   KB/s`) y también caduca (19:43:34).
4. Cada reconexión vuelve a caducar a los 5 s, porque el invitado vuelve a pasar unos 6 s recorriendo la lista.
5. El bucle termina cuando el estado sale de 0x11 (19:47:10) y el recorrido baja a 0,6 s.
6. El ping del relé de Steam se queda en 27 ms todo el tiempo: Steam no tenía ningún problema; el que no leía era
   nuestro hilo.

El anfitrión no tuvo nunca este problema: sus informes salen cada 5,00 s durante toda la sesión **[V]**.

### 1.1 Por qué cada lectura costaba unos 1–2 ms: `VirtualQuery` **[V]**

Todas las lecturas pasaban por `peek()` → `game::safe_read()`, que hace un `VirtualQuery` y luego una copia con SEH.
Por cada clave se hacían unas 20 lecturas:
- la clave;
- la cabecera del contenedor (4 lecturas, **repetidas para cada clave**);
- el cubo y los pares;
- la entrada y el actor;
- `+0x5E`, `+0xC2`, `+0xCF` y `+0x90`.

Medición en este PC (`vq_bench.py`, Python + ctypes): `VirtualQuery` en una región confirmada y **tocada** (paginada
en memoria) cuesta en proporción a lo que queda de región desde la dirección consultada, porque Windows tiene que
calcular `RegionSize`:

| región | VirtualQuery al principio | cerca del final |
|---|---|---|
| 64 KB / 2 GB sin tocar | 0,8–2,2 µs | 0,9 µs |
| 64 MB tocados | 107 µs | 0,8 µs |
| 512 MB tocados | 925 µs | 0,8 µs |
| 1 GB tocado | **2897 µs** | 0,9 µs |

Los montones del juego son regiones de varios GB y van creciendo durante la carga. Eso cuadra con lo observado:
- 193 claves × ~20 lecturas × 1–2 ms dan 4–8 s;
- con las claves «no encontradas» hay unas 8 lecturas por clave y salen 0,6 s;
- la deriva va de 4 a 8 s a medida que crece el montón.

La contención con la carga del juego (bloqueo del espacio de direcciones mientras el juego reserva y libera
memoria) puede sumar algo más **[E]**, pero no hace falta para explicar las cifras.

### 1.2 Lo que **no** era la causa (descartado con el log) **[V]**

- **Formatear las líneas.** Las líneas `GuestDiag restore` de un mismo informe salen cada 0,2 ms, unos 80 ms por
  cada 400 líneas.
- **El drenaje de GuestS3.** Su resumen sale en el mismo milisegundo que la línea anterior.
- **`guest_describe`.** Solo lee atómicos.
- **La contención del mutex del log con P8Probe (T15520).** Sus ráfagas sueltan el mutex línea a línea y no hay
  huecos de segundos fuera del recorrido. Aun así, `log::write` hacía `FlushFileBuffers` **por línea dentro del
  mutex**, y es un coste innecesario para todos los hilos (§2.4).

## 2. Cambios

### 2.1 Hilo propio para los diagnósticos del invitado

**Archivos:** `src/common/kick_worker.h` y `src/mod/session2/guest_diag_worker.{h,cpp}`.

`guest_start` arranca un hilo con prioridad `THREAD_PRIORITY_BELOW_NORMAL`. Cada 5 s ejecuta:
1. `guest_actor_diag_tick()`: las líneas de restauración y la lista de espera;
2. `guest_s3_tick()`: el historial S3, el resumen y los registros de cuelgue;
3. la línea `Session2 guest: hooks …`.

Los **formatos de línea no cambian**. La única diferencia es que `Session2 guest: …` termina con ` | diag worker:
runs N (timed M), last X ms, max Y ms`.

Cómo se reparte el trabajo entre los dos hilos:
- **Hilo de red:** en su informe solo llama a `guest_diag_kick()`, que hace un `SetEvent` y nunca espera ni toma un
  lock. Si el hilo de red se atasca, el de diagnóstico sigue funcionando solo cada 5,5 s. Dos pasadas nunca quedan
  a menos de 2 s.
- **Hilo de diagnóstico:** las pasadas de más de 1 s escriben `GuestDiag worker: one run took … (actor diag …, S3 …,
  status line …)`.
- **Traspaso entre hilos:** sin locks. Los anillos ya eran MPMC sin locks (`p8p::EventRing`, `s3::Ring`) y siguen
  teniendo un solo consumidor, que ahora es este hilo.
- **Se queda en el hilo de red:** `guest_s3_on_host_packet`, que analiza los 2005 sin formatear ni leer memoria del
  juego. Moverlo cambiaría el orden del historial S3 (Rx2005 antes que restore), así que se mantiene.
- **También se queda:** `s2_guest_link->describe()`, porque `TrocLink` no es seguro entre hilos.

### 2.2 El recorrido, sin `VirtualQuery` y con la cabecera leída una vez

**Archivos:** `guest_actor_diag.cpp` y `guest_actor_diag_logic.h`.

- `peek()` usa ahora `game::fast_read`: SEH sin `VirtualQuery`, el mismo criterio que ya seguían los hooks calientes
  y que permite `CLAUDE.md` («VirtualQuery/SEH»). Solo se leen objetos del montón, nunca pilas, así que no se
  consume ninguna página de guarda. Esto abarata también los hooks de restauración (`on_restore` y
  `on_restore_exit`), que corren en el hilo de carga del juego.
- `gad::container_view()` lee una sola vez `used`, `buckets`, el array de cubos y el de entradas.
  `gad::container_find_in(view, key)` hace la búsqueda; `container_find()` sigue existiendo y se apoya en las dos.
- Coste estimado del recorrido: 193 × ~15 lecturas SEH, del orden de microsegundos **[E]** (falta medirlo en el juego;
  la línea `diag worker … last X ms` lo dirá).

### 2.3 Salud del hilo de red, en los dos papeles

**Archivos:** `src/common/loop_health.h` y `mod.cpp`.

Cada vuelta del bucle se divide en fases, medidas en µs: `steam poll`, `receive`, `session`, `host tunnel`, `guest
tunnel`, `coop`, `state/hud` y `5 s report`. Cada llamada a `transport->recv` cuenta como un bombeo de recepción, y
se mide el hueco desde el final de un bombeo hasta el principio del siguiente.

- La línea `Net:` (anfitrión e invitado) termina ahora con:
  `| net thread: N loops, mean X ms, max Y ms (fase), longest without a receive Z ms, slow (>250 ms) K (T in all)`.
- Una vuelta de más de 250 ms escribe `WRN Net: one loop took X ms (> 250 ms; mostly <fase>): steam poll …, receive …
  (n datagrams), … | no receive for Z ms`. Hay un tope de 200 líneas; a partir de ahí se siguen contando en la
  línea de 5 s.

### 2.4 Log sin `FlushFileBuffers` por línea

**Archivo:** `log.cpp`.

`WriteFile` deja la línea en la caché del sistema, así que un cuelgue del juego no pierde nada. Solo se perdería con
un cuelgue del sistema operativo. El vaciado a disco se hace al cerrar el log. Hasta ahora, cada línea de cualquier
hilo esperaba al disco con el mutex del log tomado.

### 2.5 Recepción de Steam

**Archivos:** `steam_transport.*`, `steam_config.h`, `config.*` y `mod.cpp`.

- **Búfer de recepción** **[V en el cliente de Steam de este PC; E en Shadow]**:
  - `RecvBufferSize` y `RecvBufferMessages` limitan lo que Steam guarda recibido y aún sin leer. Por defecto son
    1 MB y 1000 mensajes, y **los paquetes que pasan de ahí se descartan**.
  - A los 400–900 KB/s de la carga del campo, ese límite equivale a ~1 s sin leer. Ahora se fijan en 16 MB y 16384
    mensajes (≈ 18 s a 900 KB/s), como valores globales y antes de abrir la sesión.
  - Se configuran con las claves nuevas `[steam] recv_buffer_kb` y `recv_buffer_messages`; 0 deja el valor de Steam.
  - Cada valor se busca **por nombre**: primero el id del SDK (47, 48) y, si no coincide, se recorren los ids 1–512.
    Después se fija, se vuelve a leer y se registra (`Steam: RecvBufferSize = … (asked …)`). Si el cliente no lo
    conoce, se avisa y no se toca.
  - `steamclient64.dll` de este PC contiene las cadenas `RecvBufferSize` y `RecvBufferMessages` **[V]**.
- `drain_incoming` pide 64 mensajes por llamada en lugar de 16. Así, el atraso que se acumula tras una vuelta lenta
  se vacía con menos llamadas.

### 2.6 Captura del cuelgue

**Archivo:** `guest_s3.cpp`. Ver §4.

## 3. ¿Algo de la ruta de recepción puede bloquear los dos sentidos?

| Pieza | Conclusión |
|---|---|
| Hilo de red atascado | **Era la causa** **[V]** (§1). Corregido en §2.1 y §2.2. |
| `ClientSession` | Sin bloqueo **[V]**. Con 5 s de silencio, el anfitrión (`Responder`) pasa a `Connecting` y deja de enviar, y el invitado caduca 5 s después. Así cualquier atasco de más de 5 s en un lado corta los dos sentidos. Es el diseño, no un fallo: el tiempo límite es correcto con un hilo sano. |
| Búfer de recepción de Steam | **Agravante** **[E]**. Con 1 MB / 1000 mensajes, Steam descartaba lo que llegaba mientras el hilo no leía, pings y acks incluidos. Corregido en §2.5. Que SNP (Steam Networking Protocol) tratara esos descartes como pérdida y bajara su ritmo es otra hipótesis. |
| `SendGate` | Sin bloqueo **[V]** (código y log). El «est 12→37 KB» del invitado no era la cola de Steam, que marcaba `pending 0 B` en esas mismas líneas. Era la estimación tras la ráfaga de acks y pongs: tras cada atasco, el hilo procesaba de golpe hasta 512 datagramas atrasados en la misma vuelta. La estimación se rehace cada 20 ms con la lectura real y se drena con el tiempo. Además, solo frena el canal fiable; los pings, acks y pongs no pasan por ella. |
| `ReliableChannel` | Sin bloqueo **[V]**. Reenvía con backoff de hasta 2 s. Si hay congestión deja de enviar y vuelve a intentarlo en la vuelta siguiente; no espera nada que solo pueda procesar el hilo atascado. Los acks se procesan en `on_datagram`, que está en el mismo hilo, así que con el hilo sano no hay ciclo. |
| `recv`: 512 datagramas por vuelta, `inbox_` de 1024 | Sin bloqueo **[V]**. `inbox_` nunca supera el tamaño de un lote, porque solo se llena cuando está vacío. 512 por vuelta de ~5 ms dan para más de 100 000 mensajes por segundo. |
| Conexión de Steam | Siguió viva todo el tiempo (ping de 27 ms) **[V]**. El servicio de Steam corre en su propio hilo y confirma a nivel de transporte aunque nosotros no leamos. |

## 4. Cuelgue al salir con Alt+F4 en `exe+0x2045A6A`

Análisis estático de solo lectura con capstone sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`. Los scripts
están en `%TEMP%\claude\scr2\`.

### 4.1 Cronología (log del invitado) **[V]**

| hora (invitado) | evento |
|---|---|
| 19:47:08,793 | Error del juego «종료 이벤트 발생» («ha ocurrido el evento de salida»): la salida con Alt+F4. |
| 19:47:10,658 | El cliente envía `LogoutSessionToServerReq` 2001; el mod contesta 2002/2003 en local porque el túnel está roto. |
| 19:47:10,650 | `[P+0x15C]` pasa de 0x11 a 0x18 tras 250,3 s en 0x11. |
| 19:47:10,758 | AV de lectura de `0x0` en `exe+0x2045A6A`, hilo principal T3720, a primera oportunidad. El manejador vectorizado lo registró. |
| desde 19:47:17 | El contador de `presents` se queda congelado en 21960: el hilo principal no vuelve a dibujar. |
| tras 19:47:35 | El proceso desaparece entre esta línea y la siguiente, esperada a las 19:47:39. |

Es decir, la excepción **no se manejó**. El hilo principal se quedó en el filtro de nivel superior de otro (crashpad)
unos 25 s mientras se hacía su volcado, y después el proceso terminó **[E fuerte]**.

### 4.2 La función **[V]**

`0x2045A20`–`0x2045B1E` (0xFE bytes). Su unwind lleva `UHANDLER` (limpieza C++, sin `__except`). Por lo que hace,
sería algo como `StageChartProcessor::CheckBranchForField(this, entry) -> bool`:

```
02045A3E call 1F6C540        ; FieldAttacher del actor de [this+8]: [rsp+28]=campo, [rsp+30]=válido
02045A44 cmp byte [rsp+30],0 / je 2045ADB
02045A57 call [rax+40]       ; campo->vf40 (ClientField: 0x9A5B70) -> 0
02045A5A mov rdi,rax
02045A5D lea rdx,[rax+8] / xor eax,eax / test rdi,rdi / cmove rdx,rax
02045A6A mov rdx,[rdx]       ; <- FALLO: lee [0], sin comprobar el nulo
```

- `ClientField::vf40` (0x9A5B70) busca el usuario dueño del campo, con la clave `[campo+0x180]` = **0x90100333 (el
  usuario local)**, mediante `0x8AE390` → `ClientActorContainer::Find` (0x464290).
- Devuelve 0 si el usuario no está o si su `+0x5E` no tiene el bit 0x04.
- Los registros cuadran con esta ruta: `r8` = la clave sin máscara, `rcx` = el attacher vacío en la pila de
  0x9A5B70 (`rsp−0x30`), y `rdi`/`rdx`/`rax` = 0.

Llamadores:
- Ruta A: `0x2044390`, procesador de estados de rama del secuenciador.
- Ruta B: `ClientSequencerStage_StageChartProcessor` vf1 `0x5DA1C0` → `0x2042A60` → `0x2045B20` (`jmp` al cuerpo
  movido a `.xtls` en 0xE014020) → `0x2045A20`.

Con `rbx = 0` (entrada nula), la más probable es la ruta B **[E]**.

### 4.3 Causa probable **[E]**

Al salir durante la carga 0x11, la desconexión ya había quitado al usuario local del contenedor, o le había
borrado su bit 0x04. Mientras tanto, el secuenciador seguía procesando escenarios del campo del anfitrión, que
estaba a medio cargar. La lectura de usuario devuelve nulo y el juego lo desreferencia sin comprobarlo.

Es un fallo del propio juego en una salida que no estaba prevista. El túnel roto solo explica por qué la carga no
terminó nunca.

Contención posible (no implementada; requeriría un hook de comportamiento, ver §6): un mid hook en `0x2045A5D` que, si
`rax == 0`, salte a `0x2045ADB`. Ese es el camino de «falso» que ya usa el `je` de `0x2045A44`.

### 4.4 ¿Intervienen nuestros hooks? **No** **[V]**

Se tomaron las 471 constantes hexadecimales ≥ 0x10000 de `src/` (un superconjunto de todos los puntos de hook), se
asignó cada una a su función con `.pdata` y se cruzaron con:
- los llamados por `0x2045A20`, `0x9A5B70` y `0x2B00C70` hasta profundidad 3;
- todos los llamadores.

Ningún hook está en ese grafo. Las únicas coincidencias son lecturas o comentarios: `0x464290` (solo se relee,
nunca se llama), `0x1F6C540`/`0x1AC2830` (comentarios de P8Probe) y `0x14343A0` (llamada de field_share en el
anfitrión).

Las guardas de capas y de update solo tocan capas y la acción actual de personajes remotos. Aquí el nulo sale de
buscar el *usuario* dueño del campo, así que un update omitido no puede producirlo.

### 4.5 Por qué no hubo `.dmp` («crash events 1 (recorded 1) … dumps 0») **[V] + [E]**

- El manejador vectorizado solo **registra** a primera oportunidad. Solo captura en el sitio conocido `0x232D5FF`,
  y este no lo era. La captura con volcado de los cuelgues no manejados depende del filtro de nivel superior.
- Nuestro filtro se instaló a las 19:41:00,905, con «previous top-level filter none». Después, el juego inicializa
  Sentry: `cdmp_SentrySetup` (0x1315CD0) busca `crashpad_handler.exe`, `set_on_crash` y `sentry_init` (0x1316226).
- El backend crashpad pone **su propio** filtro de nivel superior y no encadena al anterior. **[E]**: es como
  funciona sentry-native con crashpad; `sentry.dll` no se ha inspeccionado.
- El exe solo llama a `SetUnhandledExceptionFilter` desde el código de fail-fast de la CRT **[V]**.
- Resultado: el cuelgue no manejado nunca llegó a nuestro filtro.

**Cambio** (`guest_s3.cpp`; no traga ninguna excepción):

- **Filtro de nuevo delante.** Cada segundo, el hilo de instalación de S3 vuelve a poner nuestro filtro delante
  (`rearm_crash_filter`).
  - Si otro lo había sustituido, se guarda ese otro como el siguiente de la cadena. La primera vez (y hasta 8) se
    escribe: `GuestS3 crash capture: the top-level exception filter had been replaced by <módulo>+0x… …; ours is in
    front again and chains to it`. Esa línea confirmará en la próxima sesión quién lo había sustituido.
  - Nuestro filtro captura (log + minidump) y **después llama al de crashpad**, así que el informe del juego sale
    igual que antes.
  - Una protección por hilo (`t_in_uef`) evita entrar dos veces y los bucles, si un filtro encadena de vuelta al
    nuestro.
- **Volcado desde el propio hilo.** Si el hilo de volcado ya no existe (`ExitProcess` termina los hilos antes que
  las DLL), el hilo que falla escribe el minidump él mismo. La línea lo indica: «written from the crashing thread:
  the dump thread is gone».
- **Detalle de la línea de arranque.** Ahora nombra el filtro anterior por módulo y desplazamiento.

**Aviso (Sentry):** con `crashpad_handler.exe` presente en Shadow, este volcado probablemente se subió (o está
pendiente) a `pentry-relay.pearlabyss.io`. El K1 (`0x1317D40`) no cubre los cuelgues nativos. Conviene revisar el
`DumpCache` del invitado y valorar K2 (sacar `crashpad_handler.exe` mientras haya pruebas) **[E]**.

## 5. Pruebas nuevas (`.\scripts\build.ps1`: 17/17) **[V]**

- `tests/tests.cpp`:
  - **`test_loop_health`:**
    - 1000 vueltas tranquilas (media de 5,2 ms, hueco máximo de 0,2 ms, ninguna lenta);
    - el informe de 5,39 s de la sesión 5, que se devuelve como lento, con la fase `5 s report`, 17 datagramas, «no
      receive for 5388,1 ms» y el texto exacto;
    - un hueco que cruza vueltas y que se ve en la ventana;
    - un atasco en curso, que aparece en el informe;
    - marcas fuera de vuelta, que se ignoran.
  - **`test_kick_worker`:**
    - 100 `kick()` mientras el trabajo dura 300 ms, cada uno por debajo de 20 ms y plegados en una sola pasada más;
    - pasadas temporizadas sin ningún kick;
    - `min_gap_ms`;
    - `stop()`;
    - un `kick()` antes de `start()`.
  - **`test_steam_config_lookup`:**
    - el id esperado se acepta con una sola consulta;
    - un id distinto se encuentra por nombre;
    - un nombre desconocido da -1, tras recorrer 512 ids;
    - un prefijo no basta para coincidir.
- `tests/optd_guest_actor_tests.cpp`, `test_container_view`:
  - 193 claves (un tercio sin actor), con el mismo resultado por clave que `container_find`;
  - la cabecera se lee 4 veces en total, frente a 4 × 193 antes;
  - las cabeceras inutilizables dan una vista que no encuentra nada.

## 6. Pendiente

1. **Prueba en vivo con Shadow (P8).** La misma entrada al campo. Hay que comprobar:
   - la línea `Net: … net thread: …` en los dos lados: el máximo debería quedar en decenas de ms y «longest without a
     receive» por debajo de 250 ms;
   - que no aparezcan líneas `Net: one loop took`;
   - `diag worker … last X ms`;
   - `Steam: RecvBufferSize = …` y `RecvBufferMessages = …` en el arranque;
   - si el filtro de nivel superior se sustituye, `GuestS3 crash capture: … replaced by …`.
2. **`PlayerReader` en el hilo de red** **[E]**. Hace 20–40 `safe_read` (con `VirtualQuery`) por muestra, 20 veces
   por segundo, en la fase `state/hud`. Con montones de varios GB, cada muestra puede costar decenas de ms. El nuevo
   indicador lo dirá. Si se confirma, hay que pasarlo a `fast_read` o sacarlo del hilo.
3. **`comp_read_control`** (`companion_probe.cpp`). También usa `safe_read`, dentro del hook de restauración en el
   hilo de carga del juego: varios `VirtualQuery` por restauración, que pueden ralentizar la carga del invitado.
   No se ha cambiado porque lo comparte la sonda de compañeros del anfitrión.
4. **Contención del cuelgue de salida** en `0x2045A5D` (§4.3). Sería un hook de comportamiento, no de observación,
   así que necesita decisión explícita. Antes conviene confirmar con el volcado que la ruta es la de §4.2.
5. **Si Steam descartaba paquetes con el búfer lleno** (§3). Se puede medir con un atasco provocado. Una opción es
   una clave de pruebas que duerma 3 s en el hilo de red, y comparar `received` y los reenvíos con y sin el búfer
   grande.
