# P8 · Estrategia B, paso 2: sonda `owner_fanout` en `ToTarget::vf1`

10 de octubre de 2026, rama `opcion-d` (sobre `281d30a`). Build **1.0.0.2976**, direcciones en **RVA** (base
`0x140000000`). Es el paso 2 de `p8-arquitectura-observador.md` §7: **solo cuenta**, no cambia ni envía nada. No se
lanzó el juego ni se desplegó.

**[V]** = comprobado (capstone sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, compilación, pruebas,
desensamblado del `cdmp.asi`). **[E]** = pendiente de la prueba en el juego.

| Clave `[session2]` | PC | Valores | Qué hace |
|---|---|---|---|
| `owner_fanout` | anfitrión (necesita `create_session=1`) | `0` (defecto) \| `probe` | Gancho en `ToTarget::vf1 0x2A757F0`. Mientras el usuario 2 observa el campo del anfitrión, cuenta por escritor y opcode los Ack que van a la sesión del anfitrión, es decir, a cuántos se **añadiría** la sesión 2 en el paso 3. `1` está **reservado** para el paso 3: se rechaza con un aviso y queda apagado |

Está puesta `owner_fanout=probe` en `docs/research/ini/cdmp.p8-s7-host.ini` y `cdmp.p8-noche-local.ini`.

## 1. Qué hace

1. **Gancho inline** en la entrada de `0x2A757F0` (patrón de `relay_fix`: `safetyhook`, creado deshabilitado y
   habilitado después, `unsafe_call` al original, sin el mutex del gancho alrededor del código del juego).
2. El *detour* lee su **dirección de retorno** (`_ReturnAddress()`: el `jmp` del parche no la toca), llama al original
   y después:
   - suma `calls` (todas las llamadas, también las de ToControlUser);
   - si no hay **viaje observador**, sale. Coste: un contador relajado y una carga atómica;
   - si lo hay, lee con SEH `[finder]` (vtable) y `[finder+0x10]` (la sesión que empuja `vf1`) y decide (`ofo::judge`):

| Veredicto | Condición |
|---|---|
| `other finder` | `[finder]` no es la vtable de ToTarget (`exe+0x5B25438`) ni la de ToControlUser (`exe+0x5B252F8`) |
| `empty finder` | `[finder+0x10] = 0` (el original no añade nada) |
| `no live user 2` | no hay sesión 2 viva o se pidió su logout |
| `stale trip` | la sesión 2 viva no es la del viaje (época o servidor nuevos) |
| `user 2's own session` | el Ack ya va al usuario 2 de forma nativa |
| `another session` | ni la del anfitrión ni la del usuario 2 |
| `array too long (unknown)` | el array tiene más de 64 sesiones y la 2 no está en las 64 primeras: no se sabe (no cuenta como «no está») |
| `session 2 already there` | la sesión 2 ya está en el array |
| `unreadable` | alguna lectura falló |
| **`the host's session`** | **el caso** (ToTarget): se cuenta por escritor (retorno → escritor → opcode) |
| `ToControlUser to the host` | lo mismo con un buscador ToControlUser (el anfitrión controla el actor). Va en un cubo aparte, por escritor, con su propia línea en el resumen |

3. **Familias** (§3.4 del informe): secuenciadores (2661, 2810, 2322, 2797, 2224, 2226, 2140, 2675, 2222, 2918,
   2416), horarios (2126, 2958, 2909, 2499, 2127, 2839) y facción (2195, 2130) forman la **lista**: es lo que el
   paso 3 añadiría (`WOULD ADD`). Aparte van los dudosos (2264, 2165, 2724), los excluidos (interfaz de misiones e
   inventario, respuestas al que pidió, IA del controlador) y el resto de escritores.
4. **Comparación** (resumen de 60 s), por opcode:
   - `hook`: el recuento del gancho;
   - `host client`: lo que recibió el cliente del anfitrión durante los viajes (tap del sniffer);
   - `A queued`: las copias de `observer_seq_copy` que entraron en la cola del usuario 2 (`server_push_extra`), solo
     para 2661/2810/2322.

## 2. Bytes y punto [V]

- `0x2A757F0`–`0x2A7584E` (`ret`), **95 bytes**, dentro de `.rsrc`. Se comparan enteros antes de enganchar
  (`kVf1Bytes`, los mismos que ya verifican `observer_milestone` y `observer_scene_copy`).
- vtable de ToTarget `exe+0x5B25438`: ranuras 0/1/2 = `0x2A75740` / `0x2A757F0` / `0x2A75850` (se comprueban).
- **Única otra vtable con `0x2A757F0`**: `exe+0x5B252F8` (ToControlUser, ranura 1). Barrido de todos los punteros
  a `0x142A757F0` fuera de `.xtls`/`.text`/`.trace`: solo esas dos.
- Parche: `safetyhook` reubica 5 bytes (`mov [rsp+8],rbx`) o, con salto absoluto, 15 (`… push rdi; sub rsp,0x20;
  cmp qword [rcx+0x10],0`). Ninguna es relativa a RIP y ninguna rama del cuerpo cae en ese tramo (`je 0x2A75844`,
  `ja 0x2A75833`). Se rechaza cualquier otro tamaño.
- **Convivencia:** `observer_milestone` y `observer_scene_copy` verifican los 95 bytes desde el hilo de
  `observer_stage`, que puede correr después del gancho. Su `code_at` lee ahora con
  `owner_fanout_read_unpatched`. Es una lectura protegida que pone los bytes originales en el tramo parcheado **solo
  si** ese tramo contiene exactamente **nuestro** salto (`ofo::patch_is_ours`):
  - la forma `E9 rel32` hacia el `FF 25 00000000` + dirección del *detour*, dentro del trampolín de safetyhook;
  - o la forma `FF 25 00000000` + *detour* en `vf1+6`;
  - en las dos, el resto relleno con `0x90`.

  Así ven el código del juego y no el `jmp`. Cualquier otra cosa (una escritura ajena, un parche a medias o bytes
  corruptos) se devuelve tal cual y su comprobación falla. Si `enable()` falla, se olvida el parche. El 2322 que emite `observer_milestone_send` pasa por el gancho con destino la sesión 2
  (`user 2's own session`). `relay_fix` engancha otras funciones (`0x2A759B0`, `0x2A75880`).

## 3. Tabla de escritores [V]

- `src/mod/session2/owner_fanout_writers.h` **se genera** con
  `docs/research/pendientes/observador/scripts/ofo_writers.py` a partir de
  `C:\Emulador CD\build\research\appearance\broadcast_scan.txt` y del PE (solo lectura).
- Para cada escritor alcanzado por un ToTarget o un ToControlUser, el retorno de su `finder->vf1`: la primera
  `call qword [reg+8]` después del `lea reg,[rax+0x2B0]` del array del TLS.
- **ToControlUser [V]:** el barrido solo ve los dos `lea` de su vtable: `0x2979A5B` (→ 2339, truco de GM) y su
  constructor `.xtls 0x104EBDD0`. El constructor:
  - pone en `+0x10` la sesión del **controlador** del actor (`[actor+0x90]` → `FindUser 0x2A82590` → `vfE8`);
  - se llama desde el stub `0x2A76060`, que tiene 9 llamadores.

  Los escritores que siguen a esos llamadores son 2979, 3018, 2954, 2652, 3097 y 2184. **Ninguno** de los listados,
  dudosos o excluidos del §3.4 (2165/2264/2126/2195/2297/2218/2167/2909/2948/2203…) sale por ToControlUser; lo
  comprueba una prueba. Aun así, si alguno llegara a la sesión del anfitrión, se contaría aparte, por destino.
- Resultado: **345 escritores** (7 con ToControlUser), 0 sin llamada reconocible y retornos únicos. Todos en
  `.rsrc`; todos los de la lista y los dudosos del §3.4 están en la tabla. Ejemplos:

| Opcode | Escritor | Retorno de `vf1` |
|---|---|---|
| 2661 | `0x2A4DEF0` | `0x2A4DF57` |
| 2810 | `0x2A4E4C0` | `0x2A4E51E` |
| 2322 | `0x2999D80` | `0x2999DD6` |
| 2126 | `0x2A53EB0` | `0x2A53F09` |
| 2195 | `0x2A52BC0` | `0x2A52C1E` |
| 2264 | `0x2A56BC0` | `0x2A56C1E` |

- Un retorno que no está en la tabla se cuenta aparte, en una tabla fija de 64 huecos (`unknown return`, con su RVA
  en el log). Indica un escritor que el barrido no vio.

## 4. El viaje observador

- **Regla:** la de la opción A, sin depender de su estado (que vive bajo su candado). En `take_frame`, el 2174 que se
  empuja a la sesión 2 viva abre un viaje observador si el dueño que escribió el **servidor** es un usuario (`0x9…`)
  distinto del usuario 2. Guarda la sesión 2 y el dueño. Cualquier otro 2174 lo cierra.
- **También termina** si:
  - cambia la sesión 2 viva (nueva época o servidor nuevo): veredicto `stale trip`;
  - se pide el logout del usuario 2;
  - el usuario 2 desaparece (lo comprueba el *tick* de red).
- Se acumulan el número de viajes y los segundos observados.

## 5. Coste en el gancho [V desensamblado]

- **Sin viaje:** la llamada al original, un incremento relajado y una carga.
- **Con viaje:** `fast_read` (SEH) de la vtable y del destino. Si es ToTarget con destino:
  - `server_live_session2()` (dos lecturas SEH) y `server_logout_requested()`;
  - el array del TLS, como mucho 64 entradas;
  - una búsqueda binaria en la tabla constante y un incremento atómico.
- **Sin** asignaciones, candados, log ni llamadas al juego. Comprobado en el `cdmp.asi`: `after_vf1` solo llama a
  `fast_read`, a los tres accesores de `server.cpp`, a `array_has`, a `rva_of` y a `Counters::count_host`.
- Las líneas las escribe el hilo de red (`owner_fanout_tick`):
  - `[trip]` en cada 2174;
  - el resumen cada 60 s.

## 6. Líneas del log

- `OwnerFanout ON (probe, owner_fanout=probe): inline hook at ToTarget::vf1 exe+0x2A757F0 (95 bytes verified, 5
  patched; …); 345 writers …`.
- `OwnerFanout [trip] user 2's MoveFieldAck #N: owner 0x90100000 … -> OBSERVER trip in the host's field: counting …`.
- `OwnerFanout summary (probe) | ToTarget::vf1 calls … | while user 2 observes …: … | the host's session N, user 2's
  own …, … | WOULD ADD user 2: N (sequencers a, schedules b, faction c) | not listed: doubtful …, excluded …, other
  writers …, unknown return …`.
- `OwnerFanout top writers to the host's session while user 2 observes (total, +60 s): | 2264 UpdateStageStateAck
  [doubtful (missions)] 327 (+12) | …` (12 primeros).
- `OwnerFanout returns not in the writer table …` (solo si los hay).
- `OwnerFanout compare while observing (…): | 2661 hook 27 / host client 27 / A queued 45 MATCH | 2810 … | 2322 … |
  2126 hook 23 / host client 23 MATCH | …`.
- **Avisos:**
  - `owner_fanout=1 is reserved for step 3 …: off`;
  - `REFUSED` si fallan los bytes, la sección, la vtable o el tamaño del parche;
  - sin sniffer, la columna `host client` sale como `-`.

## 7. Qué medir

### 7.1 Un PC (anfitrión + `cdmp_peer`, `cdmp.p8-noche-local.ini`)

No hace falta Shadow. Guion del peer: el de la noche (`login_e3lc00soak_child_112627.troc`).

1. **Instalación:** sale `OwnerFanout ON … 95 bytes verified, 5 patched` (o 15). Y **no** sale `ObserverMilestone:
   REFUSED` ni el `REFUSED` de `observer_scene_copy`: es la prueba de convivencia del §2.
2. **Viaje:** `[trip] … OBSERVER trip` al entrar el peer en el campo del anfitrión, y `not counting` si viaja a su
   campo.
3. **Recuentos:**
   - `ToTarget::vf1 calls` sube siempre;
   - `while user 2 observes` sube solo durante el viaje;
   - `the host's session` > 0 si el anfitrión anda por zonas con escenas.
4. **Comparación:**
   - `2661` y `2810`: `hook` = `host client` (**MATCH**, holgura 1);
   - `A queued 2661` ≥ `hook 2661` (A incluye la instantánea).

   Con el peer, el anfitrión prepara secuenciadores al moverse por la ciudad: basta con pasear por la plaza o la taberna.
5. **Cobertura de la tabla:** `unknown return` = 0. Si sale alguno, anotar su RVA: es un escritor que el barrido no
   vio.
6. **Coste:** sin tirones nuevos en el anfitrión. Los avisos `Session2 host tunnel: one tick took …` no cambian.
   `calls` da el ritmo de llamadas a `vf1` (aún sin medir).
7. **Veredictos raros**, que deben quedar en 0 o casi 0: `session 2 already there`, `array too long`, `unreadable` y `stale trip`
   (salvo en una reconexión).

### 7.2 Dos PCs (anfitrión `cdmp.p8-s7-host.ini`; Shadow con su ini de siempre, sin cambios)

Lo mismo que en un PC, y además, en la ventana en la que el invitado está en el campo del anfitrión:

1. **Contrastar con el inventario de la sesión 8** (`p8-arquitectura-observador.md` §2.1), que en ~724 s dio:
   - 2165 ×633, 2264 ×327, 2126 ×23, 2909 ×2 (lo del anfitrión que no llega al invitado);
   - 2661 ×27, 2810 ×22.

   El gancho debe dar los mismos órdenes de magnitud por minuto observado. Los excluidos (2218 ×59, 2297 ×58, 2167 ×7,
   2203…) salen en `excluded`, no en `WOULD ADD`.
2. **`WOULD ADD`** (secuenciadores + horarios + facción) por minuto: es lo que el paso 3 metería de más en la cola
   del usuario 2.
   - Comparar con el presupuesto de la cola (`queue refused` de `observer_seq_copy` = 0);
   - y con el resto del tráfico del túnel.
3. **Familias de horario y facción:** 2195, 2130, 2958, 2499 y 2127 en vivo durante la estancia. El inventario los
   veía en la entrada (350 × 2195), no en la estancia. Si aparecen, se confirma que el paso 3 los cubre sin libro.
4. **`2126` frente a `host client`:** puede salir `DIFFER (hook less)`, porque 2126 también tiene un sitio ToNear
   (`0x28182C5`) y el cliente del anfitrión recibe los dos. Lo mismo con 2724. No es un error del gancho.
5. **`2322`:** el gancho solo lo cuenta cuando el propio anfitrión carga (su 2322 en vivo). `A queued 2322` es el
   2322 que A copia del libro, uno por viaje. No tienen por qué coincidir.

**Criterio para pasar al paso 3:**
- MATCH en 2661/2810 en las dos pruebas;
- `unknown return` = 0 (o explicado);
- sin coste visible;
- `WOULD ADD` acotado (del orden de lo que A ya copia más los horarios y la facción).

## 8. Archivos

| Archivo | Contenido |
|---|---|
| `src/mod/session2/owner_fanout_logic.h` (nuevo) | Lógica pura: modo, familias, búsqueda en la tabla, regla del viaje, veredicto, contadores, top, comparación |
| `src/mod/session2/owner_fanout_writers.h` (nuevo, generado) | 345 filas `{retorno, escritor, opcode, buscadores, nombre}` ordenadas |
| `src/mod/session2/owner_fanout.{h,cpp}` (nuevos) | Verificación, gancho, viaje, tap, *tick* y resumen |
| `server.{h,cpp}` | `server_host_session()`; una línea en `take_frame` (2174) y otra en `server_push_extra` |
| `session2.cpp` | Clave, instalación antes de `observer_stage_install`, `sniffer_fanout`, *tick* y describe |
| `observer_milestone.cpp`, `observer_scene_copy.cpp` | Su `code_at` lee con `owner_fanout_read_unpatched` |
| `config.{h,cpp}`, `CMakeLists.txt` | Clave, fuente y prueba `optd_owner_fanout` |
| `tests/optd_owner_fanout_tests.cpp` (nuevo) | Modo (1 reservado), tabla (ordenada, `.rsrc`, escritores del §3.4), familias, regla del viaje, cada veredicto, contadores (también con 4 hilos), top y comparación |
| `docs/research/pendientes/observador/scripts/ofo_writers.py` (nuevo) | Generador de la tabla |
| `docs/research/ini/cdmp.p8-s7-host.ini`, `cdmp.p8-noche-local.ini` | `owner_fanout=probe` |

No se tocan `observer_seq_copy`, `guest_seq_play` ni `child_hold`. Build: `.\scripts\build.ps1`, **35/35** (las 34
anteriores más `optd_owner_fanout`).

## 9. Límites y riesgos

1. **A incluye la instantánea:** `A queued` no es comparable uno a uno con el gancho, que solo ve lo que se emite en
   vivo. La comparación fina es gancho frente a cliente del anfitrión.
2. **Bordes del viaje:** un Ack emitido justo antes del 2174 puede llegar al cliente del anfitrión ya dentro del
   viaje, o al revés. Por eso la holgura es de 1.
3. **Pérdidas del sniffer** (`# DROPPED`): bajan la columna `host client`.
4. **Escritores fuera del barrido:** se ven como `unknown return` con su RVA. La tabla se regenera con el script.
5. **ToControlUser** pasa por el mismo `vf1`: se distingue por la vtable y su destino al anfitrión va en un cubo aparte (`ToControlUser to the host`), fuera de `WOULD ADD`.
6. **El gancho queda puesto toda la partida**, como `relay_fix`. Con `owner_fanout=0` no se instala.
