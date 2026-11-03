# P8: la inyección del anfitrión sin trabajo de heap del juego en hilos del mod

10 de octubre de 2026, rama `opcion-d` (sobre `9a2266b`). Build del juego 1.0.0.2976, PE timestamp `0x6AB28F00`,
direcciones en **RVA** (base `0x140000000`). Continúa `p8-sesion6.md` §5 (el cierre del anfitrión
`exe+0x47EFE20`) y §7.2, puntos 1 a 4.

**[V]** = comprobado (volcado, log o instrucciones con capstone sobre el PE de solo lectura
`D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, Python `-I`). **[E]** = inferencia o hipótesis.

No se lanzó el juego, no se tocó `bin64` y no se hizo commit. No se engancha nada en `.xtls` ni en sus
thunks: el único gancho nuevo está en `.rsrc`, con sus 39 bytes comprobados.

## 0. Evidencia y herramientas

| Ref. | Archivo |
|---|---|
| HD | `C:\Emulador CD\backups\p8-sesion6B-host-wer\CrimsonDesert.exe.18568.dmp` y `CrimsonDesert.exe(1).18568.dmp` (WER, PID 18568) |
| HB | `C:\Emulador CD\backups\logs\cdmp_20261009-2345_p8-6B-host.log` |
| PE | `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` (estático) |

Herramientas del scratchpad (no se conservan): un lector de minidump propio (`ThreadList`, `ThreadNames`,
`MemoryList`, `Exception`, `ModuleList`, TEB → `ThreadLocalStoragePointer`) y un desensamblador sobre el PE
con los límites de `.pdata`.

Los volcados tienen las pilas, el `.data` del exe y los TLS, pero **no** el heap de mimalloc. Las páginas
`0x3DDC4…` y el objeto del heap global no se pueden leer en ellos.

## 1. Resumen

1. **La hipótesis se confirma en lo esencial.** [V] La `CRITICAL_SECTION` del heap global
   (`exe+0x6C169C8`, la que toma `0x47EFA78`) está **tomada por el hilo de red del mod** (T9600) con
   **`RecursionCount = 40`**. Son exactamente los 40 fallos del log: la Req 2556 que «el servidor no pudo
   tomar» y los 39 «2001 COULD NOT be handed». Cada fallo fue una **excepción dentro del asignador**, entre
   el `EnterCriticalSection` y el `LeaveCriticalSection` de `0x47EFA78`, tragada por nuestro `__except`.
   La cola (`0x35DF50`) no fue. [V] Ningún hilo del juego usa ese heap: en los dos volcados ningún hilo
   tiene `TLS+0x1FD ≠ 0`, y las 22443 llamadas a `0x47EFA78` del exe son la mitad «con candado» de esa
   misma elección. [E fuerte] El heap global comparte el `tld` (segmentos) del hilo que lo creó. Usado desde
   otro hilo, corrompe los segmentos que ese hilo usa sin candado. El resultado fue el fallo del hilo
   principal en `_mi_page_malloc`.
2. **La ruta nativa de un paquete C→S** es otra (§3). [V] El búfer lo asigna un hilo del juego con **su
   propio heap** (`0x47EF9B0`). El **NetworkThread** del cliente lo mete en Q0 con `0x35DF50`. El servidor no
   lo libera: lo devuelve por Q3. El NetworkThread lo libera.
3. **Lo implementado copia esa ruta.** El hilo de red del mod ya no llama a nada del juego. Valida la Req y la
   deja en una cola nuestra, ordenada y acotada. Un mid-hook en el **mismo punto del NetworkThread**
   (`0x35BAD4`, cada vuelta, ≤ 16 ms) asigna el búfer con el heap de ese hilo, lo rellena y lo empuja a Q0
   con `0x35DF50`, en orden. El búfer vuelve a ese mismo hilo por Q3 y el juego lo libera ahí (liberación
   local). La sesión 2 se construye en ServerRunThread con el heap de ese hilo. **El mod ya no llama nunca
   a `0x47EFA78`.**
4. **Excepciones**: se registran el código, la dirección (`exe+RVA`), el paso, el hilo, la época y la Req de
   las 8 primeras. Tras la primera, la inyección **se detiene** para siempre: sin reintentos, sin logout por
   inyección y con cuarentena en C4. Todo se registra con `ERR`.
5. **Clasificación**: un servidor ocupado o en pausa y una cola llena **retienen** la Req, que se vuelve a
   ofrecer en orden con un *backoff* de 20 ms. **Nunca** provocan el logout de C4. Solo lo provocan el enlace
   (roto, invitado ausente 15 s, conexión reiniciada), la pérdida de un paquete S→C o el **límite de
   retención**. El 2001 sale con ritmo: como mucho 1 intento cada 250 ms, 240 en total y una línea `ERR`
   cada 5 s con el recuento.
6. Build y tests: **21/21** (§8). Falta la prueba en el juego (§9).

## 2. El volcado del cierre (HD), dato nuevo [V]

| Dato | Valor (los dos volcados) |
|---|---|
| Excepción | T16880, `0xC0000005`, `exe+0x47EFE20`, lectura de `0x3DDC43FA000` |
| `_tls_index` (`exe+0x6C88C50`) | 0 |
| Heap global `[exe+0x6C0E668]` | `0x3DDC40C0000` (su memoria no está en el volcado) |
| `CRITICAL_SECTION exe+0x6C169C8` | `LockCount 0xFFFFFFFE` (tomada, sin esperas), **`RecursionCount 40`**, **`OwningThread 0x2580` = T9600** |
| T9600 | el hilo de red del mod. HB escribe `[T9600] Net: …` y `[T9600] Session2: …`, y la pila tiene marcos de `cdmp.asi`. Su heap propio `[TLS+0x90] = 0x31FC0000` existe porque `g_push` → `0x35FB00` ya asignaba bloques de cola con `0x47EF9B0` en ese hilo |
| HB | 1 × «the server could not take Req 2556» + 39 × «2001 COULD NOT be handed» = **40** |
| T16880 | `[TLS+0x90] = exe+0x6A44178` (`_mi_heap_main`), `tld exe+0x6A44D70`, `heap->thread_id = 0x26A000` = su TEB: es el hilo «principal» de mimalloc |
| `TLS+0x1FD` | 0 en los 101 hilos |
| `TLS+0x4460` (marcas de aparcado del servidor, §6) | 0 en los 101 hilos en el momento del cierre |
| NetworkThread del cliente | **T13456**, esperando en `0x35B9D8`: el retorno del `WaitForSingleObject(Q1, 16 ms)` del modo virtual (`0x35B9D2`). El punto `0x35BAD4` viene justo después |
| Hilos con `TLS+0x1EC = 1` (reino servidor) | T4572 y T34548 |

Conclusión [V]: las 40 excepciones ocurrieron dentro del heap global, con su candado tomado, y ese candado
quedó tomado para siempre. [E fuerte] El daño a los segmentos que comparte con el hilo principal es la causa
del cierre de T16880. Lo que no se puede comprobar con estos volcados es qué hilo creó el heap global
(`[heap+0xB20]` no está volcado). El primero que llamó a `0x47EFA78` fue, según el código del mod, el
`build_session` en ServerRunThread o la primera inyección. Pero el objeto `0x3DDC40C0000` está en el segmento
`0x3DDC4000000`, donde el hilo principal tenía la página dañada. El arreglo no depende de esa duda: el mod deja
de usar ese heap.

## 3. La ruta nativa C→S y su liberación [V]

```
hilo lógico del cliente (T16880)  ClientNet::PostPacket 0x35C670
    0x35C692..0x35C6BA: TLS+0x1FD ? 0x47EFA78 : 0x47EF9B0  (0x8030, 0x10)   -> su propio heap
    rellena +0x8000.. +0x8028 = pb; push SIN señal a [[ClientNet+0xE0]] (0x143AB10)
NetworkThread (T13456)           0x35B800, modo virtual
    0x35B9D2 espera Q1 [ClientNet+0x18] <= 16 ms
    0x35BAD4 mientras [[ClientNet+0xE0]]+0x58: FlushSendQueue 0x35BE90 -> sesión vf6
             NwVirtualSyncSession::Send 0x14380B0 -> 0x35DF50 push CON señal a [sesión+0xE0] = Q0
             (los bloques de 0x2000 de la cola: 0x35FB00, misma elección -> heap de ESTE hilo)
    0x35BB7B..0x35BBE6 vacía Q3 [ClientNet+0x28]: free([pb+0x8028]) con 0x47F02BC (o 0x47F03BC si 0x1FD)
ServerRunThread                  0x27733F0
    0x27734EA pop Q0 [ServerNet+0x20] (0x35E030, espera <= 1000 ms)
    0x27735D3 OnRecvPacket 0x2773C20
    0x27735E4 push del pb a Q3 [ServerNet+0x78]           <- el servidor NO libera el búfer
```

Puntos clave:

- `p8-sesion6.md` §5.4 decía que el bucle del servidor libera con `0x47F03BC/0x47F02BC`. Es al revés: esas
  llamadas del bucle (`0x277358C/0x2773593`) liberan el objeto de una *petición interna* (`pb+0x800E ≠ 0`,
  tipo 6). Los paquetes vuelven a Q3 y **los libera el NetworkThread**.
- `0x47EF9B0` [V] lee `_tls_index`. Si el TLS no está inicializado (`+0x98 == 0`), llama a `0x48953C4`, la
  inicialización bajo demanda. Luego pasa `[TLS+0x90]` (el heap del hilo) a `0x47EF648`
  (`mi_heap_malloc_aligned`). No toma candados.
- `0x47EFA78` [V] hace `EnterCriticalSection(exe+0x6C169C8)`. Crea perezosamente el heap con
  `0x47F19A8 → 0x47F19BC`, que es `mi_heap_new_in_arena`: copia `heap->tld = backing->tld`, `thread_id` va a
  `+0xB20` y lo enlaza en `tld->heaps` sin candado. Después llama a `0x47EF648` y hace
  `LeaveCriticalSection`.
- Las 22443 llamadas a `0x47EFA78` del exe forman pareja con otras tantas a `0x47EF9B0`, con la misma
  prueba de `TLS+0x1FD` [V, recuento sobre todo `.rsrc`]. Nada en el juego llama al heap global sin esa
  prueba.

## 4. Lo implementado

### 4.1 Hilos y responsabilidades

| Hilo | Antes | Ahora |
|---|---|---|
| Red del mod (T9600) | `0x47EFA78` + relleno + `0x35DF50` | valida y encola bytes (`inject_logic.h`); **nada del juego** |
| Espejo E4 | igual que el de red | solo encola |
| NetworkThread del cliente (hilo del juego) | — | `on_client_pump` en `0x35BAD4`: asigna (la elección nativa de `TLS+0x1FD`, en la práctica `0x47EF9B0`), rellena y empuja con `0x35DF50`, en orden; el juego libera el búfer en este mismo hilo (Q3) |
| ServerRunThread (hilo del juego) | `build_session` con `0x47EFA78` | `build_session` con la elección nativa (su propio heap) |

- El gancho comprueba los **39 bytes** de `0x35BAD4` (única en el archivo, `.rsrc`; el único salto a ellos cae
  en su primer byte, `0x35BAC6`). También comprueba los 92 bytes enteros de `0x47EF9B0` y las cabeceras de
  siempre. Si algo no cuadra, la opción D no arranca.
- El gancho solo actúa si `r15 == [exe+0x6D69170]` (el `ClientNet` actual) y hay `ServerNet`.
- El anfitrión y el invitado nunca corren en el mismo proceso (`mod.cpp`: con `guest=1`, `session2 = false`).
  Por eso el gancho de `guest.cpp` y este, ambos en `0x35BAD4`, no coinciden.

### 4.2 Orden, cota y medida

- Una sola cola FIFO (`g_q`, con mutex). Solo el NetworkThread la vacía, y saca los elementos uno a uno.
- **En vuelo** (búferes nuestros en Q0 que el servidor aún no vio, `pushed − seen` de la época): como mucho
  **512**. Por vuelta salen como mucho **256**. Si el servidor no consume, el resto espera en nuestra cola y
  no en la del juego.
- Cola: **65 536 Req / 32 MB**, unos 10 min de un invitado a ~100 Req/s, más una reserva de 8 para la
  secuencia de logout (2549/2001). Si está llena, `Full` → el hilo de red retiene la Req en `g_early`
  (512 como mucho) y la vuelve a ofrecer cada 20 ms. Pasado eso, termina la conversación por **límite de
  retención**, nunca por «enlace perdido» sin serlo.
- Medidas en `describe()`: `inject queue: waiting N (peak P) | handed by the NetworkThread H (thread T, turns
  K, wait mean A ms max M ms, batch max B, not yet taken max F) | held: not ready X full Y | stale S cleared
  C`.
- La barrera de C4 cuenta nuestra cola como «aún no entregado» (`r.early += pendiente`).
  `server_epoch_reached` cuenta lo encolado.

### 4.3 Excepciones (tarea 3)

- Filtro SEH sin asignaciones (`note_exception`). Guarda el código, la dirección, `exe+RVA` si está en el
  exe, los parámetros de la AV (lectura o escritura y la dirección), el hilo, el paso (`allocating`,
  `filling`, `pushing to Q0`, `building session 2`), la época y la Req. Las 8 primeras van en detalle;
  después solo se lleva la cuenta.
- La primera excepción pone `g_halted_epoch`. Desde ese momento `server_inject_ex` devuelve `Halted`, la cola
  se vacía, el NetworkThread no hace nada y **no se manda el 2001** (el heap o la cola pueden estar dañados).
  El hilo de red:
  - con `reconnect=1`: `QUARANTINED`; el anfitrión sigue sin nuevas entradas;
  - sin `reconnect`: túnel muerto, sin logout.
- Una excepción dentro de `0x35DF50` puede dejar tomado el candado de Q0 (`vf8`) y bloquear al servidor.
  No se puede deshacer; queda registrado. Con `0x47EF9B0` no hay candado propio que pueda quedarse tomado.

### 4.4 Clasificación de `server_inject_ex` (tarea 4)

| Resultado | Cuándo | Qué hace el hilo de red |
|---|---|---|
| `Queued` | en nuestra cola | nada más |
| `NotReady` | sin sesión 2 de la época o sin `ServerNet` | **retener** (orden) y reintentar a los 20 ms |
| `Full` | cola en su cota (el servidor no consume) | **retener** y reintentar |
| `Refused` | no es una Req de este servidor, o mide > `0x8000` | descartar y contar (`WRN`, 16 como mucho) |
| `Blocked` | guardado/trampas/dev, C2, ranura 3/206 | descartar y contar |
| `AfterLogout` | tras pedir el logout | descartar y contar |
| `Halted` | tras una excepción | parar (§4.3) |

`kill_tunnel` (el logout de C4 por `LinkLost`) solo queda para: enlace roto, invitado ausente 15 s, conexión
reiniciada, paquete S→C perdido, «goodbye antes de existir la sesión» y el límite de retención.

**2001**: `hand_logout` hace como mucho un intento cada 250 ms y 240 en total (60 s). La primera línea `ERR`
sale en el acto; después, una cada 5 s con «N failed attempt(s) since the last line». El 9 de octubre fueron
39 líneas en 2,8 s; el test reproduce ese caso y da ≤ 12 intentos y 1 línea. Tras una excepción, una sola
línea («NOT handed») y ningún intento.

## 5. Por qué no ServerRunThread

Se consideró asignar y empujar en ServerRunThread, que es el consumidor de Q0:

- Ese hilo espera en el `pop` hasta 1000 ms (`0x27734D0`, `r9d = 0x3E8`). Con el anfitrión quieto, una Req del
  invitado podría esperar 1 s, a menos que el hilo de red señalara el semáforo de Q0. Eso es tocar la
  sincronización del juego desde un hilo del mod.
- El juego nunca asigna búferes C→S en ese hilo. Q0 la llena el NetworkThread.
- El NetworkThread gira cada ≤ 16 ms por diseño, es quien empuja de forma nativa a Q0 con la misma función y
  es quien libera esos búferes. Copiarlo es la opción con menos cosas nuevas.

## 6. Pausa del anfitrión (ESC) [V estático / E en vivo]

- [V] Los hilos del servidor se registran al arrancar en `ServerNet+0xF8`, mediante `0x27751D0`, la primera
  llamada de `0x27733F0`. Cada entrada es `{u32 tid, u8* &TLS[+0x4460]}`; el número va en `+0x100` y el
  candado en `+0x98` (`ServerNet::~ 0x2771E70` libera `+0xF8`). Los bucles `0x27733F0` (ServerRunThread,
  tras sacar un elemento) y `0x27731C0` hacen `byte[2] = 1`, `while (byte[0]) { byte[1] = 1; Sleep(10); }`:
  es un **aparcamiento** de los hilos del servidor que alguien pide desde fuera.
- [E] Que el menú ESC use este aparcamiento no está probado. No se encontró el código que escribe `byte[0]`
  (no está en `.rsrc` con un patrón directo). En los volcados las marcas valen 0 en todos los hilos, pero
  eso fue después del menú. El usuario indica que el ESC pausa el juego.
- Lo que hace el código ahora, sea cual sea el mecanismo:
  - Si el servidor deja de consumir, como mucho 512 búferes quedan en Q0. El resto espera en nuestra cola
    (65 536) y después en `g_early` (512). **Ni el 2001 ni el logout se disparan por eso.**
  - Diagnóstico: `WRN Session2 inject: the server has not taken our buffers for X ms (epoch E: handed A,
    seen B, N more held in our queue; user 2 is NOT logged out for this) | server threads n: T4572 idle …
    T…(ServerRunThread) PARK-ASKED PARKED …`. Se escribe al pasar de 1 s y cada 5 s mientras dure. Al
    terminar: `INF … takes our buffers again after X ms`.
  - Si el NetworkThread se detiene: `WRN … the client NetworkThread has not turned for X ms`.
- Al volver del menú, la cola se vacía en orden: como mucho 256 por vuelta y 512 en vuelo.

## 7. Archivos

| Archivo | Cambio |
|---|---|
| `src/mod/session2/inject_logic.h` (nuevo) | lógica pura: `Outcome`/`Disposition`, `InjectQueue`, `may_hand`, `RetryPacer`, `StallWatch`, `ExceptionNote` |
| `src/mod/session2/server.cpp` | cola + gancho `0x35BAD4` (`on_client_pump`/`pump_body`/`push_request`), `game_alloc` con la elección nativa, `build_session` sin `0x47EFA78`, filtro SEH con notas, `inject` → `Outcome`, `hand_logout` con ritmo, `server_inject_tick` (excepciones, consumo, aparcamiento, NetworkThread, cola llena), `describe` |
| `src/mod/session2/server.h` | `server_inject_ex`, `server_inject_tick`, `server_inject_halted`, `server_inject_halt_reason`, `server_inject_pending`, `server_inject_describe` |
| `src/mod/session2/session2.cpp` | `inject_or_kill` → `offer` (retener, descartar o parar), `hold_req`, `drain_held` con *backoff*, `halt_tick` (cuarentena sin logout), llamadas por vuelta |
| `src/mod/session2/offsets_s2.h` | `kAllocThread` + bytes, `kTlsIndexSlot`, `kTls_UseGlobalHeap`, `kServerNet_Threads`…, `kNetLoopPumpAob` |
| `tests/optd_inject_tests.cpp`, `CMakeLists.txt` | test `optd_inject` |

No se tocaron `guest.cpp` ni `guest_s3.*`.

Nota para quien lleve el invitado [E]: `guest.cpp` también llama a `g_push` (`0x35DF50`, en
`guarded_push`, para devolver a Q3). Si eso corre en un hilo del mod y no en el NetworkThread, `0x35FB00`
puede asignar bloques de cola con el heap de ese hilo. Es menos grave, porque es el heap propio del hilo y no
el global, pero no sigue la regla «nada de heap del juego en hilos del mod».

## 8. Build y tests [V]

`.\scripts\build.ps1` (RelWithDebInfo): compila y **21/21 tests OK**, incluido el nuevo `optd_inject`
(0,06 s). Durante la compilación apareció un aviso en `guest.cpp(438)` (C4100, parámetro `net` sin usar).
Ese archivo es del otro agente.

`optd_inject` comprueba:

- la tabla de clasificación;
- el orden FIFO y las cotas de paquetes y bytes;
- la reserva para el logout, y que también está acotada;
- el descarte por época;
- `may_hand` (presupuesto, en vuelo, sin desbordamiento);
- el ritmo del 2001, incluido el caso del 9-10;
- `StallWatch` (entrada, repetición, salida, nunca sin trabajo);
- el texto de una AV;
- una **pausa simulada de 5 s del servidor** con el invitado a 100 Req/s: ningún `Stop`, nada perdido ni
  desordenado, en vuelo ≤ cota, un episodio de «no consume» y uno de «otra vez».

No prueba el gancho, el ABI de `0x47EF9B0`/`0x35DF50` ni el juego.

## 9. Cómo verificarlo en el juego (un PC: anfitrión + `cdmp_peer`)

1. `.\scripts\deploy.ps1` con el `cdmp.ini` habitual de anfitrión (opción D, `create_session=1`,
   `accept_tunnel=1`, `reconnect=1`). Para tener tráfico continuo, añade `mirror_host=1`: el usuario 2
   repite el movimiento del anfitrión y su Req pasa por la cola, también desde el hilo espejo. Arranca
   `cdmp_peer --mode replay --script docs\research\data\login_e3lc00act_112627.troc --linger 900000`
   (unos 15 min dentro). Muévete con el anfitrión todo el rato. Al final, una vuelta con `…bye…troc`
   comprueba el 2001.
2. Al instalar, el log debe decir:
   `Session2: the guest's Req go to the server from the client NetworkThread (mid-hook exe+0x35BAD4, every
   turn <= 16 ms): buffer from that thread's own heap (exe+0x47EF9B0), push exe+0x35DF50; queue 65536 Req /
   32 MB, at most 512 not yet taken by the server`.
   Si aparece `does not start with the expected bytes` o `cannot hook the client NetworkThread's turn`, la
   opción D no arranca (fallo seguro).
3. Durante la prueba, en la línea periódica `Session2: …`:
   - `in: handed N … exceptions 0 seen by the server ≈N`;
   - `inject queue: waiting 0..pocos | handed by the NetworkThread N (thread T, turns K …, wait mean ~0–16
     ms max …, not yet taken max ≤ 512)`. `T` **no** es el hilo de red del mod (`[T…] Net:`): es el
     NetworkThread del juego. `turns` crece unas 60 veces por segundo.
4. Abrir y cerrar el menú ESC varias veces, de 10 a 60 s cada vez:
   - si el servidor se detiene, sale `WRN Session2 inject: the server has not taken our buffers for … ms …
     | server threads …` (con `PARK-ASKED/PARKED` si es el aparcamiento) y, al salir, `INF … takes our
     buffers again after … ms`;
   - **no** debe salir `the Troc tunnel stops`, `C4: logout begin … trigger=link_lost` ni `2001 COULD NOT`;
   - `waiting` sube durante el menú y vuelve a 0 después.
5. Criterio de éxito:
   - ningún cierre del anfitrión;
   - ninguna línea `INJECT EXCEPTION`;
   - `exceptions 0`;
   - `handed` ≈ `seen` al final;
   - `TLS+0x1FD set` ausente en `describe` (el juego nunca la activa);
   - la desconexión normal del peer sigue dando `2001 queued for the server` → `logout complete`.
6. Si apareciera `ERR Session2 INJECT EXCEPTION #1: code 0x… at exe+0x… while <paso> (thread …, epoch …,
   Req …)`, la inyección se detiene: `the Troc tunnel STOPS … no logout by injection` y `QUARANTINED`. Esa
   línea dice qué falló y dónde. Guarda el volcado de WER si lo hay.

Necesita Shadow después (no ahora): repetir la sesión 6B con el invitado real y el ESC del anfitrión con el
invitado dentro del mundo (`p8-sesion6.md` §5.5 c).

## Corrección (2026-10-10, ver p8-crash-esc-1831598.md)

El guion que se recomienda arriba, `login_e3lc00act_112627.troc`, es anterior a D-48: no envía la
`StartActionReq` del primer ciclo (paso 4b). El personaje del usuario 2 entra sin acción, y el anfitrión
cae en `0x1831598` cuando una pieza adjunta consulta su jerarquía. **No lo provocó el ESC.**

Para estas pruebas hay que usar `login_e3lc00soak_child_112627.troc` (o `bye`, `bye_child`, `cut8`, `cut9`,
`e3`, `e3opt`) con `mirror_host=0`. Los guiones `act`, `actearly` y `actfast` quedan obsoletos.

## Correcciones tras la revisión de la noche (2026-10-10)

Detalle completo en `p8-revision-noche.md` §6. Cambia §4.3:

- Solo se atrapan los pasos propios:
  - `Alloc`: `0x47EF9B0`, el heap del hilo, sin candado propio;
  - `Fill`: nuestras escrituras al búfer;
  - `BuildSession`: nuestras escrituras a la sesión 2.

  Cada uno deja la nota y para la inyección, como antes.
- Dentro del código del juego, el filtro guarda la nota, pone `g_halted_epoch`, escribe una línea `ERR` sin
  asignar memoria (`log::emergency`) y **deja seguir** la excepción (`EXCEPTION_CONTINUE_SEARCH`): un cierre real
  en su sitio, nunca un servidor colgado más tarde con un candado tomado. Son estos pasos:
  - `Push`: `0x35DF50`, el candado de Q0;
  - `AllocGlobal`: `0x47EFA78`, bajo su `CRITICAL_SECTION`;
  - `BuildCtor`: `0x1436EE0`, que asigna en los dos heaps y lanza él mismo `0xA0000002` [V].
- Lo mismo para la devolución a Q2 (`guarded_push`) y para `ToNear::vf1` (relay_fix).
- `pump_body`:
  - vuelve a comprobar la parada en cada vuelta;
  - solo entrega con `server_accepts_guest()`;
  - cuenta lo que tiene en mano para la barrera de C4 (`g_in_hand`).
- `0x35BAD4` se instala con `p8_install_mid` (patch de 7 bytes).
- `g_phase.on_req` avanza tras un `push` con éxito, todavía bajo `g_q_mu`.
