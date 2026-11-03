# P8: interpolación remota por actor (F1) y caballo que sigue al jinete (F2). Implementación

10 de octubre de 2026, rama `p8-remote-interp` (desde `opcion-d` `a7d043c`). Build del juego **1.0.0.2976**, base
`0x140000000`, direcciones en RVA. Diseño y diagnóstico: `p8-movimiento-remoto.md` (§3 F1 y F2) y
`p8-npc-tiendas-animales-escenas.md` §4–5.1. No se lanzó el juego ni se desplegó nada.

- **[V]** = comprobado (capstone sobre el exe, tests).
- **[E]** = deducción fuerte, falta medirla en vivo.

## 1. Qué hay

| Clave `[session2]` | Dónde | Valores | Qué hace |
|---|---|---|---|
| `remote_interp` | anfitrión **e** invitado | `0` (defecto, nada instalado) / `probe` / `1` | 4 mid-hooks en las lecturas del retardo de interpolación (`exe+0x6CB9548`): cada actor `0xB…` remoto (modo 2) usa su propio retardo según su cadencia. Con `1`, además, umbral de vaciado del búfer 1000 → 2500 ms |
| `horse_follow` | solo anfitrión (`create_session=1`) | `0` / `probe` / `1` | Cada `MoveActorAck` 2697 de un jinete que va a la sesión del usuario 2 va seguido de un 2697 sintético de su caballo. El otro sentido solo se mide (§4.3) |

Con `probe` las dos piezas miden y no cambian nada. Con `0` no se instala nada.

**Ficheros:**
- `src/mod/session2/remote_interp.{h,cpp}` y `remote_interp_logic.h` (lógica pura).
- `src/mod/session2/horse_follow.{h,cpp}` y `horse_follow_logic.h` (lógica pura).
- `server.cpp`: dos llamadas (en `take_frame` y al encolar una Req del invitado) y `push_outgoing_extra`.
- `session2.cpp`: configuración y línea de cada minuto.
- `config.{h,cpp}`, `mod.cpp` (arranque y línea `Config`) y `CMakeLists.txt`.
- `tests/optd_remote_interp_tests.cpp` (test 26 de 29).
- `.ini`: `cdmp.p8-s7-host.ini` y `cdmp.p8-noche-local.ini` (las dos a `1`); `cdmp.p8-s7-shadow.ini`
  (`remote_interp=1`, `horse_follow=0`).

## 2. Bytes verificados [V]

Capstone sobre `CrimsonDesert.exe` (build 2976). Los seis sitios están en `.rsrc` y dentro de
`[0x1000, 0x51EB000)`; ninguno en `.xtls`. El mod los vuelve a comparar byte a byte con el juego en marcha
antes de tocar nada. Si no coinciden, escribe una línea `ERR … REFUSED` y la pieza queda apagada.

| Sitio | Función | Bytes | Instrucción | Contexto en ese punto |
|---|---|---|---|---|
| `0x11B200E` | `setInterpolatePositionData` `0x11B1FD0` | `4C 63 15 33 75 B0 05` | `movsxd r10,[rip→0x6CB9548]` | `rdi` = búfer (`mc+0x3D0`), `rbp` = muestra nueva (pos `+0`, ts `+0x98`), now = `rax+rbx` |
| `0x11B2730` | `setInterpolateRotateData` `0x11B26E0` | `4C 63 1D 11 6E B0 05` | `movsxd r11,[…]` | `rdi` = `mc+0x488`, now = `rax+rbx` |
| `0x11B249A` | `updateInterpolatePositionData` `0x11B2420` | `48 63 05 A7 70 B0 05` | `movsxd rax,[…]` | `rdi` = `mc+0x3D0`, `rcx` = now (`0x11B2496`) |
| `0x11B2B62` | `updateInterpolateRotateData` `0x11B2AF0` | `48 63 05 DF 69 B0 05` | `movsxd rax,[…]` | `rdi` = `mc+0x488`, `rcx` = now (`0x11B2B5E`) |
| `0x11B2073` | umbral de vaciado (posición) | `49 81 F8 E8 03 00 00` → `49 81 F8 C4 09 00 00` | `cmp r8,0x3E8` → `cmp r8,0x9C4` | seguido de `jbe 0x11B2148` |
| `0x11B2775` | umbral de vaciado (rotación) | ídem | ídem | seguido de `jbe 0x11B284D` |

- **Lectores de `0x6CB9548`:** `riprefs.py` encuentra exactamente esos cuatro y la escritura del valor inicial
  (`0xF7BFB`, `mov [..],0xC8`).
- **Llamadores:** `0x87D9C0` pasa `rcx = mc+0x3D0` al set de posición; `0x88E6A0` hace `lea rbx,[rcx+0x3D0]` y
  pasa `rdx = rbx` al update.
- **Parche de cada mid-hook:** una sola instrucción de 7 bytes. Ningún salto puede caer dentro, y
  `p8_install_mid` exige que safetyhook reubique exactamente esos 7 bytes (si cae en el salto absoluto de 14,
  rechaza).

## 3. `remote_interp`: decisiones

### 3.1 De búfer a actor (barato y validado)
`mc = rdi − 0x3D0` (posición) o `rdi − 0x488` (rotación). A partir de ahí:
1. `CC = [mc+0x60]` y se exige `[CC+0xC8] == mc`. Así se descarta cualquier otro uso de estas funciones de
   script.
2. `actor = [mc+0x68]`, `clave = u32[actor+0x60]`.

Son 4 lecturas `game::fast_read` (SEH, sin VirtualQuery) por llamada. Si algo no se lee, la instrucción
original se ejecuta tal cual (contadores `unresolved [...]`).

Como la clave sale del actor, **no hace falta** el plan B de «si los huecos son < 300 ms, el original»:
- Los `0xA…` (personajes de jugador) **siempre** leen el valor del juego.
- Los `0xB…` usan el suyo.
- El resto (`other`) lee el del juego.

### 3.2 «Modo 2» sin llamar al juego
El update solo se llama desde `0xAB0540` (camino del modo 2; `movimiento-remoto-cliente.md` §1). Cuando un
update de posición lee un búfer, su estado queda marcado `updated`. Solo los búferes marcados reciben un retardo
propio, también en los sets. Un NPC simulado localmente (modo 1/3) nunca pasa por el update y no se toca.

### 3.3 Retardo por controlador
El estado es por `mc`, compartido por posición y rotación:
- **Huecos:** solo en el set de posición. Se registra `ts_nueva − ts_última` del búfer si la muestra es nueva,
  se movió más de 1e-4 m (la misma prueba que el juego, `0x5D72C34`) y el hueco es ≤ 5 s. Así las muestras quietas
  y las pausas no cuentan como cadencia.
- **Objetivo:** `clamp(max(3 últimos huecos) + 150, lo, 1800)`, con `lo = max(200, valor del juego)`. Si se
  repite el experimento F0 subiendo la global, nunca se queda por debajo.
- **Aproximación:** se calcula en el update de posición (una vez por frame y actor). Sube como mucho 0,5 ms por ms
  (el actor se reproduce a ≥ 0,5×) y baja como mucho 0,25 ms por ms (≤ 1,25×). `dt` está topado a 100 ms, así
  que tras una ausencia larga no hay salto. `T = now − D` nunca retrocede.
- **El resto de lecturas:** el update de rotación y los dos sets usan el `D` vigente del controlador. En un
  vaciado, la muestra sintética queda en `now − D`, el instante que se está mostrando.
- **Sustitución:** el hook pone `D` en el registro destino (`r10`, `r11` o `rax`) y reanuda **después** de la
  instrucción (`ctx.rip = sitio + 7`), como `guest_s3.cpp` y `horse_diag.cpp`. Si no sustituye, se ejecuta la
  instrucción reubicada original.
- **Primer contacto:** `D` arranca en el valor del juego (200) y sube hasta ~1250 en unos 2,2 s.

### 3.4 Umbral 2500 ms
- Solo con `1`, y solo si los cuatro hooks entraron. El umbral solo, con `D` = 200, empeora los NPC: salto del
  83 % en vez de un tramo de 200 ms (`p8-movimiento-remoto.md` §1.3).
- Se verifican los dos sitios y se escriben con `safetyhook::trap_threads`. Si el segundo falla, se restaura el
  primero.
- Si el parche falla, el retardo por actor sigue activo y el log lo dice.

### 3.5 Tabla y concurrencia (M5)
**Estática [E fuerte]: los sitios NO corren en el mismo hilo.**
- **Updates:** `0x8C4D50` (thunk de tarea paralela) → `0x8B56D0` → `0x860D30` → `0x88EB70` → `0x88F4E0` →
  `0x87B760` → vf2 `0xAB0540` → `0x88E6A0`. El volcado de `caballo-coop.md` §9 muestra `0x8B56D0` en un
  `ParallelTaskWorker`. Los updates corren en los hilos de trabajo.
- **Sets:** los llama el Ack en el hilo principal (`0x35D0C0`).

**Por tanto, la tabla es concurrente:**
- 8192 ranuras fijas del mod: estática, nunca el heap del juego, sin asignaciones ni candados.
- Sondeo lineal de 32. Cada campo es un atómico relajado.
- Una ranura no se vacía nunca. Se reasigna a otro `mc` si lleva 10 s sin tocarse.
- Si cambia la clave tras el mismo `mc` (controlador liberado y reutilizado), el estado empieza de cero.
- Una vista parcial entre campos solo da a un frame un `D` ligeramente distinto.

Cada sitio guarda hasta 8 TID; el resumen los imprime y dice si son uno o varios.

### 3.6 Coste
Por llamada: 4 lecturas protegidas, un hash y unos atómicos. Llamadas ≈ 2 × (actores en modo 2) × fps, más los
sets (~1 Hz por NPC). En el invitado (≈1800 actores remotos vistos en la sesión 7, no todos activos a la vez)
puede rondar el 1 % de un núcleo **[E]**. El resumen da `calls` por sitio para medirlo.

## 4. `horse_follow`: decisiones

### 4.1 Lo que se hace (sentido anfitrión → invitado)
Se engancha en `take_frame`, el Send de la sesión 2: es la única interceptación de envíos por sesión del mod.
Después de encolar con éxito un 2697 de 235 B para la sesión 2 viva, se construye un 2697 sintético si se cumplen
estas condiciones:
- la clave es `0xA…`;
- `u64 +0x8C` es una clave `0xB…` de 32 bits;
- `+0x94` no es (0,0,0);
- la clave del caballo no es una de las que el usuario 2 controla. Estas claves salen de sus propias
  `MoveActorReq` encoladas, en un anillo de 64 que se vacía en cada época.

El sintético se construye con la receta de `caballo-coop.md` §4, la de los Move propios del caballo:
- `+0x00` = caballo;
- `+0x04` = `+0x94` del jinete y `+0x10` = `+0xA0`;
- `+0x20` = 0;
- `Path.local` = la misma pos y quat, escala 1;
- `+0x8C` = −1;
- `+0x94…+0xD4` = 0;
- `ts`, el u8 y la cola, los del jinete.

Va a la misma cola, justo detrás (`push_outgoing_extra`). **Solo memoria del mod:** un `std::vector` como el que
`take_frame` ya crea. No se asigna nada en el heap del juego ni se llama a ninguna función del juego. Por eso no
aplica la regla de `p8-heap-inyeccion.md`, que es para búferes del juego.

`push_outgoing_extra` solo encola si la cola está por debajo de la mitad de su presupuesto: un sintético nunca le
quita sitio a un paquete real, cuyo rechazo corta la conversación.

**Si el jinete va retenido** (ruta `Held`/`Pushed` de `observer_hold_acks`), no se fabrica nada.

### 4.2 Por qué la receta no necesita saber que es un caballo
`+0x94` es el Transform de mundo **del padre** (CAB: error 0,000 m frente a la posición del caballo). Si el padre
fuera un asiento o un barco, el sintético llevaría su propia posición: no lo mueve. Riesgo bajo, pero solo está
comprobado con caballos (§6).

### 4.3 Lo que NO se hace: el sentido invitado → anfitrión. Diseño que no se sostiene tal cual
- El Ack del jinete invitado hacia el cliente del anfitrión lo escribe el servidor por la **sesión 1**. Ese camino
  es nativo y ningún código del mod lo ve («session 1 never goes through any of our code», `server.h`).
- Para añadir un sintético habría que crear un búfer del juego y meterlo en la cola de recepción del cliente del
  anfitrión desde el hilo correcto. Eso no existe en el anfitrión (la ruta de inyección de `p8-heap-inyeccion.md`
  es C→S).
- **Alternativa no implementada:** fabricar, en el túnel, una `MoveActorReq` 2556 del caballo por cada Req del
  jinete invitado. El servidor la difundiría de forma nativa al anfitrión. Pero cambia el estado del servidor (la
  posición del caballo a 36 Hz) y duplica las Req de movimiento del invitado. No la fuerzo sin prueba.
- **Lo implementado en ese sentido es solo la sonda:** las Req 2556 del usuario 2 montado y su cadencia, y las
  Req propias de su caballo mientras va montado.
- En el anfitrión, el caballo del invitado sin jinete y su mascota **sí** mejoran con `remote_interp`. Con jinete,
  `remote_interp` también suaviza el caballo, pero **1,3 s por detrás** del jinete, que va a 200 ms. El
  desfase cambia de forma («a tirones» → «suave pero atrasado») y no desaparece.

## 5. Tests [V]
`.\scripts\build.ps1`: compila sin avisos nuevos; **29/29** tests (los 28 de antes y `optd_remote_interp`).

`optd_remote_interp` cubre:
- objetivo y aproximación: tasas, sin saltos, coma fija, ~2,2 s de 200 a 1300;
- observación de inserciones: vaciado a 1000/2500, muestras viejas, quietas, pausas;
- la tabla: re-clave, reasignación por antigüedad, llena, 4 hilos con 2000 controladores;
- la cadena búfer→actor y la última muestra sobre memoria falsa;
- la decisión por clase y modo;
- un **modelo del búfer del juego**: un NPC a 1 m/s con muestras cada 1,1 s. Hoy da picos de > 4 m/s y paradas a
  0; con la pieza, 1 m/s constante y `D` = 1250;
- el 2697 sintético byte a byte, los veredictos, el anillo de claves y las cadencias.

No prueban los hooks, el parche, la ABI ni el juego.

## 6. Riesgos
1. **Latencia de NPC y animales de ~1,3 s.** En combate el golpe se calcula contra una vista atrasada. F1-bis
   (extrapolar con la velocidad) no está hecho. Medir M6: si la IA en combate envía más a menudo, `D` baja sola.
2. **El umbral 2500 es global**, también para los `0xA`:
   - un jugador remoto quieto 1–2,5 s que arranca da un salto pequeño (fracción × lo poco que avanzó) en vez de
     recuperar en 200 ms;
   - un teletransporte con una pausa de 1–2,5 s se interpola en vez de aparecer de golpe.
3. **Saltar la instrucción con `ctx.rip`:** depende del stub de safetyhook v0.7 (el `ret` final usa el `rip` del
   contexto). Ya lo usan `guest_s3.cpp`, `horse_diag.cpp` y `remote_puppet.cpp`.
4. **Relojes por hilo:** `now` incluye el sesgo `[exe+0x6D69438]` salvo en hilos con `TLS+0x1EC ≠ 0` (reino
   servidor). Si un hilo cliente no lo tuviera, la antigüedad de las ranuras (10 s) podría mezclar relojes. Solo
   afecta a qué ranura se reasigna cuando la tabla está llena.
5. **`horse_follow`:**
   - padres `0xB` que no sean caballo (§4.2);
   - unos +9 KB/s de túnel mientras se cabalga;
   - durante la animación de montar (~2 s sin Move del jinete), el caballo vuelve a sus muestras de ~1 Hz, que
     `remote_interp` suaviza.
6. **Coste de los hooks:** ver §3.6.

## 7. Qué mirar en la próxima prueba

### 7.1 Líneas del log
Las dos PCs:
- `Config: session2 … | P8 movement: remote interp '1' horse follow '1'` (en el invitado `'1'`/`'0'`).
- `RemoteInterp: waiting for the local player in the world before hooking …` y después:

  `RemoteInterp ON (1, guest|host): 4 mid hooks at the delay reads exe+0x11B200E / 0x11B2730 / 0x11B249A /
  0x11B2B62 (7 verified bytes each, inside .rsrc) | 0xB actors in mode 2: … | emptying threshold 2500 ms
  (exe+0x11B2073 / 0x11B2775 patched)`

  Cualquier `ERR RemoteInterp: … REFUSED` = la pieza apagada.
- Cada 60 s, tres líneas:
  - `RemoteInterp summary [1] (…): hooks on | delay per actor (0xB) | threshold 2500 ms (patched) | game delay
    200 ms | threads (M5): … | set pos 0x11B200E: calls … resolved … (0xA …, 0xB …, other …) unresolved […] no
    slot … replaced … threads […] | …` (los cuatro sitios).
    - **M5:** los TID de cada sitio. Se espera `SEVERAL threads`: los updates en hilos de trabajo y los sets en
      el principal.
    - `replaced` > 0 en los cuatro sitios.
    - `unresolved` debería ser ~0.
  - `RemoteInterp summary inserts (set pos): | 0xA: buffer emptied … (with the game's 1000 ms: …) … | 0xB: buffer
    emptied N (with the game's 1000 ms: M) … gaps [<=100 …, <=1500 …]`.
    - En `0xB`, `N` debe ser **mucho menor que `M`**. Hoy, en la sesión 7, `M` ≈ 70–77 % de las muestras.
    - Los huecos, concentrados en `<=1500`.
  - `RemoteInterp summary delays: 0xB per frame (applied with 1) [<=200 …, <=1300 …] | controllers: table used …
    | controllers seen in the last minute … | largest delays: 0xB0… 1250 ms (gaps 1100/1086/1149), …`.
    - Los NPC que caminan deben estar en ~1200–1300.
- Con `remote_interp=probe`: lo mismo, con `(would be, with 1)` y `replaced 0`.

Solo en el anfitrión:
- `HorseFollow ON (1): …`.
- Cada minuto (con `probe=1`):

  `Session2 probe (minute N): HorseFollow [1]: to user 2: Move Acks … (… rider on an 0xB parent R …) | made X
  (push refused 0, build failed 0) | … | last pair 0xA… on 0xB… | rider Ack cadence [<=40 …] | its horse's own
  Acks H [<=1000/2000 …] || user 2's Move Reqs … (riding …) | rider Req cadence […] | its horse's own Reqs …`.

  Mientras el anfitrión cabalga: `X ≈ R`, cadencia del jinete ≤ 40 ms y la propia del caballo ~1 s.

### 7.2 Comprobación visual
1. **Invitado:** NPC caminando, aves, la mascota del anfitrión. Se mueven sin «paso y teletransporte», ~1,3 s
   atrasados.
2. **Anfitrión:** la mascota y el caballo sin jinete del invitado, suaves.
3. **El anfitrión cabalga 30 s:** en el invitado, jinete y caballo juntos.
4. **El invitado cabalga 30 s:** en el anfitrión, caballo suave pero detrás del jinete (§4.3). Anotar la
   separación. Si molesta, decidir sobre la alternativa de §4.3.
5. **A/B con `remote_interp=probe`** (mismo sitio): debe volver el síntoma, con `replaced 0`.
