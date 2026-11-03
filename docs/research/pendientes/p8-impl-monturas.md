# P8 monturas: compromiso nativo jinete–caballo (etapas 1 y 2, implementación)

11 de octubre de 2026, desde `opcion-d` `f93ca39`, rama `p8-mount-commit`. Build del juego **1.0.0.2976** (PE
`0x6AB28F00`, base `0x140000000`; todas las direcciones son RVA). Investigación de partida (solo lectura, externa):
`<codex-workspace>\mounts-20261011-work\` (`INFORME.txt` §5, §6, §8;
`PUNTOS-VERIFICADOS.txt`; `native\INFORME.txt`; `modules\INFORME.txt`; `ghidra-results\2228260.c`, `e6f3880.c`,
`e6fb160.c`, `9dc90a0.c`, `e6f2e60.c`, `b59c20.c`, `a45760.c`). No se lanzó el juego ni se desplegó nada. No se
tocaron `horse_follow*`, `guest_horse_probe*` ni `remote_interp*` (otro agente: muestras de cabalgata y retardo de
pareja).

- **[V]** = comprobado (bytes del PE, código, tests).
- **[E]** = deducción fuerte, falta medirla en vivo.
- **[H]** = hipótesis / pendiente de la próxima sesión.

## 0. Qué hay

| Clave `[session2]` | Dónde | Valores (defecto `0`) | Qué hace |
|---|---|---|---|
| `mount_sync` | anfitrión **e** invitado (simétrico) | `0` / `probe` / `1` | Etapa 1 (sondas) con `probe` y `1`. Etapa 2 (retención, llamada nativa en el observador, aplazar `ride_filter`) solo con `1` |

**Qué está vivo y qué es sonda:**

| Parte | `probe` | `1` |
|---|---|---|
| Ganchos de observación (tabla §2) | sí | sí |
| Publicar `MountBegin` / `MountCommit` / `MountDetach` del personaje **propio** por el túnel | sí | sí |
| Observador: recibir los eventos, juzgar el estado nativo del par, apuntar **con los argumentos exactos** qué llamaría | sí | sí |
| Observador: retener el flujo observado desde un `Begin` hasta su `Commit` (≤ 500 ms) | no | sí |
| Observador: llamar a `0x2228260` y luego a `0x2228E40` | **no** | sí |
| `ride_filter`: aplazar la reescritura de esa transacción validada | no | sí |
| Reescritura de respaldo en el despacho si falta el vínculo | no | sí (solo con `ride_filter=1`) |

**Inis** (`docs/research/ini/`): `mount_sync=probe` en `cdmp.p8-s7-host.ini`, `cdmp.p8-noche-local.ini`,
`cdmp.p8-s7-shadow.ini`, `cdmp.p8-s7-shadow-fase2.ini` y `cdmp.p8-s12-shadow-seguro.ini`. Por qué no `1` todavía:
§6. **Desde la sesión 17 (§10):** `mount_sync=1` en los dos inis del anfitrión, `probe` en los tres del invitado. **Desde la sesión 18 (§11):** `ride_dismount=native` en los dos del anfitrión y `rewrite` en los
tres del invitado. **Fase B (§12):** `mount_sync=1` + `ride_dismount=native` también en `cdmp.p8-s7-shadow.ini` y
`-fase2`; `cdmp.p8-s12-shadow-seguro.ini` (plan B) sigue en `probe` / `rewrite`.

**Ficheros:**
- Nuevos: `src/mod/session2/mount_sync{.h,.cpp,_logic.h}`, `tests/optd_mount_sync_tests.cpp`.
- Cambiados: `ride_filter{.h,.cpp,_logic.h}` (aplazamiento), `guest.cpp` (eventos de control, retención, envío de
  eventos propios), `session2.cpp` (eventos de control, retención de las Req del usuario 2, orden de salida),
  `config.{h,cpp}`, `mod.cpp`, `common/troc_frame.h` (comentario del estado 5), `CMakeLists.txt`, los cinco inis.

**Tests [V]:** `.\scripts\build.ps1` compila sin avisos nuevos y pasan **45/45**; `optd_mount_sync` es el nuevo
(formato del evento, transacciones del observador, tiempos, idempotencia, cuarentena, nonce, juicio previo,
postcondiciones, respaldo).

## 1. Diseño

### 1.1 Lo que falta en el observador [V, informe externo §1–§2]

- `RideOn::Enter` `0x681C80` lee `Vehicle=[[actor+0x68]+0x48]`, `Link=[Vehicle+0x18]`, `u32 Link+0x10` (vehículo
  actual) y sale con `+0x44 = 0` si falta. `RideOff::Enter` `0x682C20` igual.
- Lo escribe `CommonVehicle::vf53` `0x2228260` solo en el cliente que monta: ocupante en `Vehicle+0x48+4·asiento`
  (`0x2228552`), `+0xC8`++, `0x222AFD0→.xtls 0xE6FB160` escribe `Link+0x10/+0x14`, y limpia la reserva
  `Link+0x08/+0x0C` que dejó `0xE6F3880` (interacción 2663/2916).
- El observador recibe la acción (2991) pero nunca el compromiso: anfitrión que ve al invitado → `ride_filter`
  sustituye `91605616` por locomoción; invitado que ve al anfitrión → `875B47E6`/`C2FEBDE4` pasan y fallan con
  `link=0, Enter=0`.

### 1.2 Transacción

Cada cliente es **controlador** de su jinete+caballo y **observador** del par del otro.

**Controlador** (gancho de `0x2228260`, marcado como llamada cliente por el thunk `0xA45740`):
1. A la **entrada**, si el jinete es el personaje propio (`mount_sync_set_local_key`: anfitrión = su usuario `+0xD0`;
   invitado = su 2756) y el vehículo es `0xB…`: número de transición nuevo y **`MountBegin`** al anillo de salida.
2. Llama al original.
3. A la **salida**: `*resultado`, `Link+0x10/+0x14`, ocupante, cuenta → **`MountCommit`** con `ok` solo si
   `*res == 0` y el vínculo/ocupante quedaron (`controller_commit_ok`).
4. Una retirada nativa `0x2228E40` de ese mismo par en el hilo del cliente → **`MountDetach`** (con su 4.º
   argumento y su bandera).

**Formato** (`mount_sync_logic.h`, paquete de control del túnel, opcode 1, slot −1, 44 B; estado **5**):
`u8 5 | u8 versión 1 | u8 tipo (1 Begin, 2 Commit, 3 Detach) | u8 asiento | u8 quinto | u8 flags | u16 0 |
u32 jinete | u32 caballo | u32 transición | u32 resultado | u32 nonce | u32 llamador | u32 aux | u64 tick_ms`.
El `nonce` es aleatorio por proceso: un controlador reiniciado no se confunde con el anterior. Un par antiguo lo
ignora (invitado) o lo tira como "no es Req" (anfitrión).

**Orden causal [E]:** el `Begin` se encola a la entrada de vf53, antes de cualquier Req que emitan sus llamados.
- Invitado controlador: `guest_send` (NetworkThread) vacía el anillo de eventos **antes** de empujar cada paquete
  del cliente a `g_to_host` (y `guest_take_outgoing` también, por si el cliente no envía nada).
  - Revisión de `c2eed25` (MED): con dos hilos vaciando, un `pop` y su `push` separados podían adelantar un
    `Commit` a su `Begin`, o una Req a su `Begin`, y un `push` rechazado perdía el evento. Ahora
    `msl::OutboundDrain` hace de cada `pop`+`push` un solo paso bajo un candado del mod: `guest_send` lo toma
    bloqueando y mete **su propio paquete dentro del mismo paso**; el hilo de red del mod solo lo intenta
    (`try_lock`, cuando el cliente está quieto). Un evento rechazado se guarda y se ofrece primero la vez
    siguiente; mientras esté pendiente no sale el paquete del cliente (la cola está llena: la conexión termina
    como con cualquier paquete rechazado). Los rechazos se cuentan y se avisan (`WRN … mount_sync: the queue to
    the host refused a mount event`). Test: `test_outbound_drain_order` (dos hilos, 20 000 eventos, orden y
    "cada paquete después de los eventos anteriores"; rechazo y reintento; candado ocupado).
- Anfitrión controlador: `push_outgoing` toma **primero** la cola de salida del servidor y **después** vacía el
  anillo: una 2991 de la Req de montar se encoló después de esa Req, que salió después de la entrada de vf53; su
  `Begin` ya está en el anillo y sale antes.

**Observador** (`msl::Table`, una transacción por jinete; fases `begun → committed → applying → riding →
detach pending → detaching → detached`, más `cancelled` y `QUARANTINED`):
1. `Begin` del jugador remoto conocido (anfitrión: `server_user2_player_key`; invitado: estado 4 del anfitrión) →
   `begun`. Con `1`, el flujo observado se **retiene** (invitado: todos los paquetes del anfitrión en
   `guest_deliver`, en orden; anfitrión: las Req del usuario 2 en `g_early` antes del servidor) hasta el `Commit`
   o 500 ms. Topes: 2048 paquetes / 4 MB (invitado), 256 Req (anfitrión; el propio tope de `g_early` es 512).
2. `Commit` ok → `committed` (se suelta la retención). `Commit` fallido → `cancelled`.
3. En el **despacho de la siguiente 2991 del jinete** (gancho `0xB57720`, hilo principal del cliente; tras 1 s,
   en cualquier 2991): se resuelven de nuevo ambos actores por clave y se lee el par; `judge_commit`:
   - `already applied` (vínculo y ocupante ya son los del par: **idempotente**, no se vuelve a ocupar);
   - no listo (actor/Vehicle/Link ausente): se reintenta en la siguiente acción, máx. 3;
   - rechazos: asiento ocupado por otro, jinete enlazado a otro vehículo, controladores `actor+0x90` distintos,
     `C8 == 1` en el jinete, rama de acción del caballo (`C8 != 0`, asiento 0, quinto ≠ 1);
   - `apply`: con `1` llama `0x2228260(RCX=Vehicle del caballo, RDX=&res, R8=jinete, R9b=asiento,
     [rsp+0x28]=quinto)` bajo SEH (una excepción del juego **no** se traga: apaga las llamadas y sigue la
     búsqueda; `game_fault_continue`), y comprueba `verify_commit`: `*res == 0`, `Link+0x10/+0x14 = caballo/asiento`,
     ocupante = jinete, cuenta +1 (o igual si ya estaba), reserva limpia, `actor+0x90` y `C8` intactos. **Nunca
     escribe esos campos.** Fallo → `QUARANTINED` (ese par ya no se aplica en la conexión).
4. Luego sigue el manejador original: `RideOn::Enter` encuentra el vínculo.
5. `Detach` de esa transacción (nunca de una anterior: número de transición y caballo) → `detach pending`. Se
   aplica **una sola vez** en un despacho, después de que este cliente ejecutara `RideOff::Enter` del jinete (el
   desmontaje sigue necesitando el vínculo) o a los 3 s: `0x2228E40(&res, caballo, &clave, p4, bandera)` con los
   valores del controlador, y `verify_detach`.
6. Conexión nueva (época) → la tabla se vacía.

**`ride_filter` [V, código]:** con `ride_filter=1` decide primero sobre una copia; si `mount_sync=1` tiene una
transacción `begun/committed/riding/…` de ese jinete, la deja pasar (`DEFERRED to mount_sync`). En el despacho,
tras el intento, si la acción es de montar/desmontar y el jinete **no** tiene vínculo a **su** caballo, se reescribe
allí mismo como habría hecho `ride_filter` (`FALLBACK`). Sin transacción, `ride_filter` actúa como siempre.

### 1.3 Por qué el despacho de 2991 es un contexto nativo seguro [E]

- `ClientNet::OnRecvPacket` → cola → hilo principal `0x35D0C0` → `vf2` de cada Ack (`movimiento-remoto-cliente.md`).
  `0xB57720` es el `vf2` de 2991; a su entrada no ha tomado nada (deserializa después).
- El compromiso nativo del controlador ocurre en el mismo tipo de contexto: los Acks de interacción 2663/2916
  (`0xBB4130`/`0xBB4AC0`) también son `vf2` en ese despacho, y el 2938 `RideOffByServerAck` (`0xB59C20`) llama a
  `0x2228E40` para **cualquier** actor desde ahí. Es la prueba nativa de que la retirada en el despacho existe.
- `0x2228260` toma sus propios candados (`cdmp_Actor_Lock` del caballo y del jinete) y elige reino por
  `TLS+0x1EC` (cliente en el hilo principal).
- La alternativa `0x8B6144` (gancho de fotograma de `guest_seq_play`) solo existe en el invitado; `0x35CD990`
  (`ui_pause`) depende de `ui_pause_block`. El despacho existe en los dos PCs y está pegado a la acción que
  necesita el vínculo.

## 2. Puntos y bytes [V: PE de referencia y el instalado, mismo SHA-256 `57da440d…`]

Todos en `.rsrc`, dentro de `[0x1000, 0x51EB000)`; nada en `.xtls` (sus cuerpos solo se leyeron). Escaneo
`E8/E9/Jcc32` de las secciones ejecutables y `.xtls`, `rel8` alrededor: sin saltos reales hacia dentro de ninguna
ventana (el único candidato, `0xBB4B3F→0xBB4AC1`, son bytes del `mov` de `0xBB4B3D`; igual que el informe). El
mod comprueba en el proceso los bytes (16, o 5 en los thunks) y que safetyhook mueva **exactamente** las
instrucciones verificadas; si no, `REFUSED` y no se escribe nada.

| RVA | Parche | Bytes | Instrucciones | Uso |
|---|---|---|---|---|
| `0x2228260` | 7 | `48 8B C4 48 89 58 10` | `mov rax,rsp; mov [rax+0x10],rbx` | `MountBegin`/`MountCommit`; también la llamada del observador pasa por aquí |
| `0xA45740` | 5 | `E9 5B 39 38 09` | `jmp 0x9DC90A0` (thunk cliente vf53) | marca la llamada cliente y su sitio de llamada (vtable) |
| `0x2228E40` | 5 | `E9 1B A0 4C 0C` | `jmp 0xE6F2E60` (thunk RideOff) | `MountDetach`; la retirada del observador pasa por aquí |
| `0xA45760` | 5 | `48 89 5C 24 08` | `mov [rsp+8],rbx` | ClientVehicle vf54: ciclo de vida (solo observación) |
| `0x681C80` | 7 | `48 8B C4 48 89 58 10` | RideOn::Enter | resultado y vínculo (`MountApply`) |
| `0x682C20` | 7 | `48 8B C4 48 89 58 10` | RideOff::Enter | resultado; habilita la retirada |
| `0xBB4130` | 5 | `48 89 5C 24 08` | 2663 vf2 | `MountReserve` (`Link+0x08/+0x0C` antes/después) |
| `0xBB4AC0` | 5 | `48 89 5C 24 08` | 2916 vf2 | `MountReserve` |
| `0xB57720` | 5 | `48 89 5C 24 08` | 2991 vf2 (nuevo, verificado aquí) | punto de aplicación + registro de acciones del jinete |

ABI usada [V, `2228260.asm` y `9dc90a0.asm`]: `0x2228260(RCX=Vehicle, RDX=int* res, R8=Actor* jinete, R9b=asiento,
quinto byte en [rsp entrada+0x28])`, devuelve `RDX`; el thunk lee `[rsp+0x60]` tras `push rbx; sub rsp,0x30` = mismo
`[entrada+0x28]`. `0x2228E40(int* res, Actor* caballo, u32* clave jinete, u64 p4, u8 bandera)` [V,
`e6f2e60.c`, `b59c20.c`]. Los desvíos con aridad dudosa (vf54, `Enter`, manejadores) reenvían todos los registros y
dos huecos de pila del llamador.

## 3. Hilos

| Qué | Hilo | Qué hace allí |
|---|---|---|
| Desvíos de `0x2228260`, `0xA45740`, `0x2228E40`, `0xA45760`, `Enter`, 2663/2916 | los del juego (cliente; en el anfitrión vf53/RideOff también el del servidor) | lecturas SEH, anillo sin bloqueo; ni E/S, ni formato, ni heap del juego, ni candado |
| Desvío `0xB57720` | hilo principal del cliente | `TryAcquire` del candado del mod para reclamar trabajo (nunca durante una llamada al juego); llamada nativa fuera del candado; reescritura de respaldo bajo SEH |
| Eventos del túnel, retención | hilo de red del mod (y NetworkThread para `guest_send` y la decisión de `ride_filter` del anfitrión, solo `TryAcquireShared`) | memoria del mod |
| Formato de líneas, caducidad, resumen | hilo propio de `mount_sync` (100 ms) | nunca llama al juego |

## 4. Qué buscar en el log

- Arranque: `Config: P8 mounts: mount_sync 'probe'` y `MountSync PROBE (mount_sync, host|guest): CommonVehicle::vf53
  0x2228260 … hooked, …` (cada punto `hooked` o `REFUSED`).
- Controlador al montar: `MountReserve` (2663, 2916; `Link res` debe pasar a caballo/asiento), `MountBegin … caller
  exe+0x… (vtable call, client thunk) | client, PUBLISHED | transition N`, `MountCommit … *result 0x00000000 …
  postconditions OK`, `RideOn::Enter … (local player) -> 1`.
- Observador: `MountSync observer … MountBegin … -> begun`, `… MountCommit … -> committed`, `commit args
  0x2228260(RCX vc=…, R8 rider=…, R9b seat=…, [rsp+0x28] fifth=…)`, `MountApply [probe] … judge: apply -> probe:
  nothing called`, `rider action … 2991 A0 0x… | its Link here 0 | txn riding`, `RideOn::Enter … (remote player)
  -> 0`.
- Al desmontar: `RideOff::Enter`, `MountDetach … 0x2228E40 … PUBLISHED` en el controlador, y en el observador
  `-> detach pending`, `detach args …`, `MountApply … detach`.
- Resumen cada 60 s: `MountSync summary [...]` (ganchos, claves, cuentas por paso, juicios, resultados, retenciones,
  aplazamientos, respaldos, fallos del juego).

## 5. Guion de la próxima sesión de dos PCs (~5 min, `mount_sync=probe` en los dos)

Grabar vídeo en ambos PCs con un marcador visible (p. ej. abrir y cerrar el mapa) entre pasos. Terreno llano, sin
combate, viaje rápido ni cinemáticas.

1. 20 s quietos los dos.
2. **Anfitrión**: llama al caballo, monta por un lado, espera 3 s, camina 10 s, gira, galopa 10 s, frena, desmonta.
   Repite montando por el otro lado.
3. **Invitado**: la misma secuencia con su caballo.
4. Uno monta y se aleja; el otro entra en alcance cuando ya va montado (entrada tardía); cruzan juntos un límite de
   sector; desmontan.
5. Copiar `cdmp.log` de los dos (y los `cdmp_packets_*`).

**Qué tiene que salir para pasar a `1` (preguntas [H] que cierra la sesión):**
- El `Begin` del controlador **precede** a la Req 2223 de montar (comparar el `qpc` de `MountBegin` con la Req en
  `cdmp_packets`) y, en el observador, el `MountBegin` llega antes que la 2991 de montar (`rider action` después de
  `-> begun`). Si no, la retención no cubre esa acción (con `1` cae al respaldo).
- `caller exe+0x…`: quién llama a vf53 (¿el 2916/DoStep, la acción de montar, `RideOn::Enter`?) y el **quinto
  byte** real; `client without the thunk` debe ser 0.
- En el observador: `judge: apply` con `C8 rider 0 horse 0 | same controller yes`; si sale `would start a horse
  action` o `controller mismatch`, `1` no aplicaría (se queda en respaldo).
- `MountDetach` en el controlador al desmontar (si no aparece, la retirada nativa va por otra ruta y el observador
  conservaría un vínculo viejo: ver §7).
- vf54 (`0xA45760`): cuándo corre respecto a vf53/RideOff (¿efectos cliente que también haya que reproducir?).
- En el anfitrión, `vf53 … OTHER thread (server realm?)`: si el servidor también compromete.

**Primera prueba con `1` (después, no en la misma sesión):** solo el PC que observa con `mount_sync=1` y el otro en
`probe` (los eventos salen igual); una montura por lado; mirar `MountApply [1] … APPLIED`, `post: ok`,
`RideOn::Enter … (remote player) -> 1`, y al desmontar `MountApply … detach … APPLIED`.

## 6. Por qué los inis quedan en `probe` [E]

La llamada en el observador es posible y está implementada con todas las guardas, pero varios datos de los que
depende siguen siendo [H] en el informe y no se pueden cerrar sin la sonda:
1. **Orden**: no está medido si vf53 corre antes o después de emitir la Req 2223 de montar. Si corre después, la
   acción de montar llega al observador sin `Begin` y la llamada solo serviría para la siguiente acción.
2. **Quinto argumento**: solo se conoce su efecto en una rama (`param_5 != 1` lanza una acción del caballo); su
   valor real sale de la sonda.
3. **Efectos en un caballo remoto**: con asiento 0 vf53 llama sin condición a `0x221C200(comps+0x90)`,
   `0x17F58A0(caballo)`, `CC->vf260(0)` y `0x2228070` (IA/navegación/acciones). En el controlador el caballo es
   local; en el observador es remoto (modo 2). No hay prueba estática de que sean inocuos; las postcondiciones solo
   miran vínculo, ocupante, cuenta, controladores y `C8`.
4. **Ruta de retirada**: no está medido que el desmontaje del controlador pase por `0x2228E40` en el hilo del
   cliente (puede ser vf54 u otra).
5. **2663/2916**: vf53 no lee el componente Interaction ni exige la reserva (la limpia) [V, `2228260.c`], pero
   `0xE6E9FB0` (`.xtls`, valida el asiento) no está descompilado: si exigiera la reserva, el observador vería
   `*result != 0` → `FAILED` y cuarentena, sin daño fuera del par.
Con la sesión en `probe` se ven (1)–(4) y el log dice, transacción a transacción, qué habría llamado y con qué.

## 7. Riesgos y límites

- **Vínculo viejo** si el controlador no publica `MountDetach` (retirada por otra ruta): el observador sigue en
  `riding`; volver a montar el mismo caballo es `already applied` (bien); otro caballo es `rider linked elsewhere`
  → rechazado y respaldo. El respaldo exige vínculo **a su caballo**, así un vínculo viejo nunca deja entrar en
  RideOn/RideOff con otro caballo.
- **Retención**: con `1` retrasa el flujo observado como mucho 500 ms si el `Commit` no llega (normalmente llegan
  juntos en el mismo lote). En el anfitrión retrasa las Req del usuario 2 al servidor, no a su cliente.
- **Entrada tardía**: no implementada (etapa siguiente). Quien entra cuando el otro ya va montado no recibe
  transacción: sin cambio respecto a hoy.
- **Sin pose durante la animación de montar**: el vínculo permite `Enter=1`, pero no crea las muestras de
  trayectoria que el controlador deja de enviar (informe §5): etapa siguiente, con el agente de cabalgata.
- `mount_sync=1` con `ride_filter=0`: no hay respaldo (la acción sin vínculo entra en RideOn como hoy sin filtro).
- Un fallo de postcondiciones pone el par en cuarentena para esa conexión; una excepción dentro del juego apaga
  todas las llamadas nativas del módulo y se deja pasar (el crash es real, donde ocurrió).

## 8. Criterios de aborto (sesión con `1`)

Volver a `mount_sync=0` (o `probe`) y conservar los logs ante cualquiera de:
- `MountApply … FAILED postconditions` o `QUARANTINED`;
- controlador cambiado (`same controller NO`, o `actor+0x90` distinto tras la llamada) o cámara/entrada del
  observador reaccionando al otro jugador;
- ocupante duplicado, vínculo a otro caballo/transición, o un desmontaje que retira una montura nueva;
- Req del observador sobre actores ajenos (en `cdmp_packets` del observador: 2223/2556 con la clave del otro
  jinete o de su caballo);
- `RideOn::Enter … (remote player) -> 0` persistente o capas que dejan de avanzar tras `APPLIED`;
- `GAME FAULTS passed on` / `MountSync: exception …`, `REFUSED` en un punto, o colas que crecen (retenciones sin
  liberar, `ring full`).

## 9. Recomendación

1. Próxima sesión de dos PCs con `mount_sync=probe` en los dos (guion §5). No cambia el comportamiento.
2. Si (1)–(4) de §6 salen bien: `mount_sync=1` primero solo en un observador, una montura por lado.
3. Después: retirada por otra ruta si `MountDetach` no aparece, muestras de pose durante montar y reloj de pareja
   (con el agente de cabalgata), y entrada tardía (instantánea `riding` con cursor de eventos).

> **Sesión 17 hecha** (§10): (1), (2), (3) y (5) salen bien; (4) no: el desmontaje de un jugador no pasa por
> `0x2228E40`. Se añade la vigilancia del vínculo en el controlador y la sesión 18 pone `mount_sync=1` **solo en el
> anfitrión**.

## 10. Sesión 17 (11 oct., dos PCs, `mount_sync=probe` en los dos) y decisión para la sesión 18

Rama `p8-mount-s17` desde `opcion-d` `f68cd88`. No se lanzó el juego. Datos:
- invitado (Shadow): `backups\p8-sesion17-shadow\cdmp_20261011-140408.log` + `cdmp_packets_20261011-140425.{bin,log}`;
- anfitrión: `backups\p8-sesion17-host\cdmp_20261011-150156.log` + `cdmp_packets_20261011-150206.{bin,log}`.

El `capture t=` de cada línea `MountSync` es el `t` de la captura del mismo PC, así que los tiempos de las tablas
son exactos dentro de un PC. Entre PCs se alinea por paquetes (reloj del invitado ≈ anfitrión − 1 h). El vídeo
(`video-host.mp4`) no se pudo decodificar: no hay ffmpeg. Claves: anfitrión `A0100001`/`B0100003` (usuario
`0x90100000`), invitado `A0100361`/`B0100363` (`0x90100360`). Las 4 marcas Ctrl+F12 del invitado y las 3 del
anfitrión separan los tramos del guion.

### 10.1 Transiciones: lo que dicen las sondas

**Ganchos [V]:** los nueve `hooked` en los dos PCs. Sin `REFUSED`, `game faults 0`, `ring full 0` y
`client without the thunk 0`.

**Montar, en el controlador** (6 montajes: 4 del anfitrión y 2 del invitado):

| Controlador / transición | 2663 → 2916 | 2916 → vf53 | vf53 → Req 2223 de montar | Acción (`+0x08` de la 2223) | Antes de vf53 | Después |
|---|---|---|---|---|---|---|
| H 1 | 760 ms | 2,2 ms | **0,40 ms** | `91605616` | Link cur 0, res `B0100003/0`; asiento libre, reserva = jinete; C8 jinete 2, caballo 1 | `*res 0`, Link `B0100003/0`, res limpia, ocupante = jinete, cuenta 1 |
| H 2 | 493 ms | 1,8 ms | 0,38 ms | `91605616` | igual | igual |
| H 3 | 672 ms | 1,8 ms | 0,41 ms | `875B47E6` | igual | igual |
| H 4 | 31 ms | 1,9 ms | 0,40 ms | `91605616` | igual | igual |
| G 1 | 65 ms | 7 ms | **1,03 ms** | `C2FEBDE4` | igual (C8 2/1) | igual |
| G 2 | 626 ms | 13 ms | 0,57 ms | `875B47E6` | igual | igual |

En los seis montajes:
- **asiento (R9b) 0, quinto byte 0**;
- llamador `exe+0xEE609BE` (`.xtls`, por la vtable y el thunk cliente `0xA45740`), seguido de vf54 desde
  `exe+0xEE609E1` y de `RideOn::Enter (local player) -> 1`;
- `postconditions OK`, `PUBLISHED`.

En el anfitrión, el **reino servidor** (otro hilo, `T31656`, sin thunk) hace su propio vf53 del mismo par
5–6 ms después del cliente. Lo hace para su jugador y también para el del invitado (`A0100361`, 56 ms después del
`Begin` que llegó por el túnel): `C8 1/1`, `*res 0`. Es decir, el servidor compromete por su cuenta al procesar la
acción, y lo que le falta al observador es solo el vínculo de su **cliente**.

**Montar, en el observador** (eventos del otro PC; los 6 llegaron, `Begin` y `Commit` juntos):

| Observador | `Begin` → primera 2991 del jinete | Juicio | Args que habría usado | `RideOn::Enter` del remoto |
|---|---|---|---|---|
| G ve H 1–4 | 18,5 / 2,1 / 15,5 / 13,0 ms | `apply` ×4 | `0x2228260(vc, &res, jinete, R9b 0, [rsp+0x28] 0)` | 1–2: no entra (`ride_filter` reescribe `91605616`); 3 (`875B47E6`): **-> 0, Link 0**, quieto 2,9 s (RemoteMotion #1); 4: reescrita |
| H ve G 1–2 | 56,1 / 6,5 ms | `apply` ×2 | ídem | **-> 0, Link 0** (`C2FEBDE4` y `875B47E6` no están en la lista de `ride_filter`) |

En los seis juicios: `C8 rider 0 horse 0`, `same controller yes`, actores, Vehicle y Link presentes, asiento
libre. Con `1` se habría llamado a vf53 en el despacho de la 2991 de montar, **antes** de su manejador. Así,
`RideOn::Enter` habría encontrado el vínculo.

**Respuestas a §6:**
1. **Orden [V].**
   - vf53 corre **antes** de la Req 2223 de montar: 0,38–1,03 ms en los 6 casos.
   - El `Begin` llega al observador **antes** que la 2991 de montar: 2–56 ms.
   - La retención no hace falta en la práctica (`Begin` y `Commit` llegan en el mismo lote), pero no estorba.
2. **Quinto argumento [V]:** 0 en los 6 montajes de jugador y en todas las llamadas de NPC. El asiento es siempre 0.
3. **Efectos con asiento 0 sobre un caballo remoto [E fuerte, casi V].**
   - El cliente ya llama **por sí mismo** a vf53 con asiento 0 y quinto 0 sobre parejas **remotas** de NPC:
     el invitado, 11 llamadas desde `exe+0xA469F0`, NPC con `C8 0/0` y controlador `0x90100000` (el anfitrión);
     `*res 0` y sin fallos.
   - En el cuerpo (`2228260.c`), los efectos de asiento 0 (`0x221C200`, `0x17F58A0`, `CC->vf260(0)`,
     `0x2228070`) se ejecutan **sin condición** cuando `param_4 == 0`. Es decir, el juego ya los ejecuta en
     clientes sobre caballos remotos.
   - La rama que lanza una acción del caballo (`horse->vf188(5)`, `C8` del caballo) no se toma con `C8 0`.
   - Diferencia que queda: esas parejas ya tenían el vínculo (re-compromiso). El primer compromiso escribe además
     el ocupante, `+0xC8`++ y `WriteRideLink`.
4. **Retirada [V]: NO.**
   - Para un jugador, `0x2228E40` **no se llama nunca**: 0 llamadas en 2 desmontajes del invitado y 4 del
     anfitrión, ni en el hilo cliente ni en el reino servidor.
   - `RideOff::Enter (local player) -> 1` sí ocurre, y en el montaje siguiente el vínculo ya es 0
     (`cur 0 res <caballo>/0` tras el 2663). La ruta que lo limpia no pasa por el thunk; probablemente `.xtls`
     llama directamente a `0xE6F2E60` [H].
   - Los NPC sí usan el thunk: el servidor con bandera 7 y el cliente con **bandera 12, p4 0** desde
     `exe+0xA46C21`, 3–25 ms después.
   - Consecuencia para `1`: el observador nunca recibiría `MountDetach` y su vínculo quedaría colgado.
     **Corregido** (§10.3).
5. **Reserva 2663 / `0xE6E9FB0` [E].** En los re-compromisos de NPC del cliente, `0xE6E9FB0` da `*res 0` con
   `res 0x00000000/-1` y reserva 0, es decir, sin la reserva del 2663. Si en un primer compromiso la exigiera, el
   observador vería `*result != 0`: `FAILED` y cuarentena, sin daño.

**vf54 [V]:** corre en cada vf53, justo después y desde el mismo llamador `.xtls`; el vínculo queda igual. En el
anfitrión, los vf54 de NPC a veces corren en el reino servidor. No hay efecto cliente que reproducir.

**Qué habría hecho `1` [V, líneas `commit args`/`MountApply [probe]`]:**
- 6 compromisos `apply` con los argumentos de arriba.
- 0 retiradas.
- Las transacciones se quedan en `riding` hasta el siguiente `Begin`. Con `1`, eso deja el vínculo del observador
  vivo tras desmontar.

### 10.2 Cabalgata: por qué el jinete se separa del caballo al galope

**Muestras [V].**
- **G→H** (anfitrión), `HorseFollowReverse`:
  - derivadas 5.957, entregadas 5.957, `FAULTED 0`;
  - intervalos del caballo: all p50 **15** / p90 20 ms; own only p50 1.000 / p90 2.000;
  - propias rancias 131 de 156; mismo tick 141.
- **H→G** (invitado), `HorseRide`:
  - derivadas 3.604;
  - intervalos all p50 **30** / p90 40 ms; own only p50 2.000;
  - propias rancias 103 de 134.
- **Reloj de pareja** (`RemoteInterp summary pairs`): `|D horse − D rider| ≤ 0` en el **95,6 %** de los frames
  emparejados en el anfitrión (4.272/4.470) y en el **86 %** en el invitado (6.390/7.393). El resto es la rampa
  900 → 200 ms de los primeros ~3 s de cada monta. Recortes `D cut` 3.

**Geometría de las muestras [V]** (script sobre las capturas; 9.732 Move montadas de los dos controladores y sus
Ack en los observadores):
- La posición del jinete (`+0x04`) es **exactamente** `Path.world` (`+0x94`, el caballo) ⊕ `Path.local` (`+0x4C`):
  el desplazamiento en el marco del caballo coincide con `Path.local` al milímetro.
- Por velocidad del caballo, el asiento en el marco del caballo:

  | Velocidad | <1 m/s | 1–3 | 3–6 | 6–10 | ≥10 |
  |---|---|---|---|---|---|
  | Asiento (H) | (+0,00, 1,51, 0,55) | (−0,01, 1,56, 0,58) | (+0,13, 1,52, 0,54) | (+0,15, 1,58, 0,50) | (+0,09, 1,62, 0,50) |
  | sd | 0,10 | 0,09 | 0,12 | 0,16 | 0,15 m |

  El invitado sale igual. Es el balanceo del jinete en la silla, no un desfase.
- En los observadores, el caballo tiene muestras **en los mismos ticks** que el jinete: 6.105 de 6.120 (H) y
  3.730 de 3.748 (G). Las derivadas son idénticas a `Path.world`; las propias, ≤ 0,26 m salvo una (9,2 m, a mitad de la 2.ª monta del invitado, t=432,5 s de su captura), con el mismo tick que una derivada (el búfer del cliente descarta la que llega segunda).
- Respuesta a la pregunta: **no** son conjuntos de muestras ni ticks distintos (eso era S13); **sí** son actores
  independientes. Mientras cabalga, el jinete remoto está en **método 0 con Link 0** en todos los segmentos de
  RemoteMotion #1 (`f79c7424/...`). Es un personaje suelto con la animación de montar, colocado en su raíz
  interpolada.

**Causa [E/H].** Con datos coherentes y el mismo retardo, las raíces de jinete y caballo deberían coincidir en el
asiento ±0,15 m. Lo que queda, por orden de probabilidad:
- **(a) Fase de animación [E].** Sin vínculo, el jinete no va pegado al hueso de la silla de **este** cliente: lleva
  el balanceo de la silla del controlador (en su `Path.local`), mientras que el caballo del observador galopa con
  su **propia** fase de animación.
  - Al paso la amplitud es mínima; al galope, el balanceo (±0,1–0,15 m de `Path.local`) y el cabeceo de la silla
    van desfasados.
  - Esto encaja con "bien al paso, despegado al galope".
- **(b) Colocación de un personaje suelto [H]:** colisión de la cápsula del jinete con el caballo o ajuste al suelo
  del método 0.
- **(c) Rampas de D [V, acotado]:** 4–14 % de los frames, solo al empezar cada monta.

(a) y (b) solo se resuelven con el **vínculo nativo**. `RideOn::Enter` consulta el transform del asiento
(`0x681F80→0xA453F0`) y prepara la corrección (`0x877F90`), y el método de cabalgar trabaja sobre el vínculo.
Está por medir [H] si el método de cabalgar remoto (RideOn/RideOff tienen `vf2 0xAB0540`, que interpola) coloca
al jinete en la silla en cada frame. Por eso se añade la medida de §10.3: separa (a), (b) y (c) en la sesión 18.

### 10.3 Decisión y cambios (sesión 18)

**`mount_sync=1` solo en el anfitrión; el invitado sigue en `probe`.**
- Las sondas cierran (1), (2), (3) y (5), y sus 6 juicios son `apply`.
- El vídeo y la queja vienen del anfitrión (el jinete del invitado visto desde H).
- El invitado en `probe` deja en la **misma sesión** la comparación sin vínculo (H visto desde G) y publica igual
  sus eventos, incluido el `Detach` nuevo.
- Fase B, si la A sale limpia: `mount_sync=1` también en el invitado (línea en sus tres inis).

**Cambios** (`mount_sync{.cpp,_logic.h,.h}`, tests). La llamada de compromiso **no cambia**: los argumentos, el orden
y las precondiciones medidos coinciden con lo implementado.
1. **Vigilancia del vínculo en el controlador** (probe y `1`).
   - Un `Commit` ok arma el par (`msl::arm_pack`, atómico de 64 bits). En cada despacho de 2991 (hilo principal,
     a lo sumo cada 30 ms) se lee el `Link+0x10` del personaje propio. Si ya no es ese caballo, se desarma una
     sola vez (compare-exchange) y se publica un **`MountDetach` observado**: flags `ok|observed` (`0x09`),
     bandera 12, p4 0 (la retirada nativa del cliente para un jinete remoto, §10.1 punto 4).
   - Se publica en cuanto lo ve si el jinete pasó por su `RideOff::Enter` después del `Commit` (los 6 desmontajes de
     S17). Si no (muerte, caída, teletransporte), solo cuando el vínculo lleva 1 s ido (`kCtlGoneMs`), para no
     anunciar un borrado pasajero a mitad de la monta.
   - Si el siguiente `Begin` llega antes (remonta rápida), el `Detach` sale justo antes, en la entrada de vf53.
   - El gancho de `0x2228E40` usa el mismo desarme, así que nunca salen dos.
   - Línea `MountSync MountDetach (observed) … N ms after its last RideOff::Enter`.
2. **Observador** (revisado tras la revisión independiente, §10.7):
   - El `Detach` observado pasa a `detach pending` y se retira en el primer despacho en que las 2991 del jinete
     van a pie aquí (padre −1 y sin nombrar a su caballo), o tras su `RideOff::Enter`, o como mucho a 1 s.
     La retirada es `0x2228E40(&res, caballo, &clave, 0, 12)` y luego `verify_detach`. Si la acción a pie llega
     en el mismo despacho, se retira **antes** de su manejador.
   - Un `Detach` nativo (no observado) sigue esperando su `RideOff::Enter` o 3 s.
   - Si su propio `RideOff` nativo ya limpió el vínculo: `already applied`.
   - **Respaldo sin evento:** si nunca llega un `Detach` y las 2991 del jinete van a pie durante **6 s** seguidos
     mientras su transacción está en `riding`, se retira igual, una vez, con bandera 12. No exige
     `RideOff::Enter`: en S17 el anfitrión no ejecutó ninguno para el invitado. La acción de montar (padre −1
     pero con el caballo como objetivo) no cuenta, y una acción montada en medio anula el candidato. Línea
     `detach args … WITHOUT the controller's Detach` y contador `retirements without the controller's Detach`.
3. **Geometría de la monta en los observadores** (probe y `1`, solo lectura).
   - Mientras la transacción está en `riding` y las 2991 del jinete dicen "montado", a lo sumo cada 30 ms se
     leen los `TransformSync` del jinete remoto y de su caballo (posición `+0xB4` + sector, cuaternión `+0xA4`).
   - Se calcula el desplazamiento en el marco del caballo y se compara con el asiento que envía el controlador
     `(+0,05, 1,56, 0,55)`. Va por velocidad del caballo y separado por **"linked here" / "NOT linked here"**.
   - Sale en el resumen de cada 60 s. Mide la raíz, no los huesos:
     - si la raíz cuadra (≤ 0,25–0,5 m) y aun así se ve despegado, la causa es la fase de animación (a);
     - si la raíz no cuadra, es (b) o (c).

**Tests [V]:** `.\scripts\build.ps1` compila sin avisos nuevos y pasan 45/45. En `optd_mount_sync` hay 3 nuevos:
- `test_ctl_link_watch`: empaquetado, `ctl_link`, evento observado por el cable y la transacción 3 de S17 de
  principio a fin;
- `test_no_event_detach`: antes y después de `RideOff::Enter`, la anulación por acción montada, una sola vez y
  el `Detach` tardío como `Dup`;
- `test_ride_geometry`: asiento con y sin giro de 90°, 1,5 m detrás al galope, lejos y cuaternión malo.

**Inis.** `cdmp.p8-s7-host.ini` y `cdmp.p8-noche-local.ini`: `mount_sync=1`. `cdmp.p8-s7-shadow.ini`,
`-fase2` y `cdmp.p8-s12-shadow-seguro.ini`: `mount_sync=probe` (con la nota de la fase B). Sin cambios en
`horse_follow_reverse=1` ni en `remote_interp_pair=1`.

### 10.4 Qué mirar en la sesión 18 (mismo guion que S17)

- **Anfitrión** (observa al invitado con `1`):
  - `MountApply [1] … commit … -> APPLIED | post: ok`, `Link 0 -> 0xB0100xxx/0`, `count 0 -> 1`;
  - después, `RideOn::Enter … (remote player) -> 1`, y en `ride_filter`, `deferred to mount_sync`;
  - al desmontar: `RideOff::Enter … (remote player) -> 1` si entra (en S17 no entró: el desmontaje llegó como
    acciones a pie), `observer … MountDetach … flags 0x9 -> detach pending`,
    `detach args 0x2228E40(… p4=0x0, flag=12)` y `MountApply [1] … detach -> APPLIED` (o `already applied`), a lo
    sumo 1 s después;
  - nunca `commit` después de la acción de montar: la primera línea `MountApply [1] … commit` precede a
    `RideOn::Enter (remote player)`;
  - `MountSync: the observer's transactions cleared (…)` al desconectar o al salir el usuario 2.
- **Invitado** (controlador):
  - `MountDetach (observed) … N ms after its last RideOff::Enter`, una por desmontaje;
  - en el resumen, `observed Detach published` = número de desmontajes y `armed now no` al final.
- **Geometría** (los dos resúmenes): comparar `linked here` en el anfitrión con `NOT linked here` en el invitado,
  por velocidad. Éxito: `|off the seat|` mayoritariamente ≤ 0,5 m en `6-10` y `>=10` m/s, y el vídeo del
  anfitrión con el jinete en la silla al galope.

### 10.5 Criterios de aborto de la sesión 18

Volver a `mount_sync=probe` en el anfitrión (conservando logs y capturas) ante cualquiera de los de §8 o:
- `MountApply … FAILED` / `QUARANTINED`, `GAME FAULTS passed on`, `MountSync: exception …` o un cuelgue al montar;
- `RideOn::Enter … (remote player) -> 0` con `Link` presente, o capas del jinete paradas tras `APPLIED`;
- cámara, entrada o control del anfitrión reaccionando a la monta del invitado (`same controller` o
  `actor+0x90` cambiados, Req 2223/2556 del anfitrión con la clave del jinete o del caballo del invitado en su
  `cdmp_packets`);
- tras desmontar: el jinete del invitado pegado al caballo o arrastrado, `detach … FAILED`, o ningún
  `MountDetach (observed)` en el invitado (la vigilancia no ve el vínculo: `unreadable` alto);
- la geometría `linked here` **peor** que la `NOT linked here` del otro PC a la misma velocidad.

### 10.6 Riesgos

- **Primer compromiso en un observador [E]:** el cuerpo es el mismo que el juego ejecuta en re-compromisos de NPC
  remotos, pero no hay un primer compromiso de jugador remoto medido. Un `*res != 0` deja el par en cuarentena:
  vuelve al comportamiento de S17.
- **Retirada con bandera 12 en un jugador [E]:** es la bandera nativa del cliente para jinetes remotos (NPC). Si
  el `RideOff` nativo del observador ya limpia el vínculo, no se llama nada (`already applied`).
- **Retardo de la vigilancia:** ≤ 30 ms de despachos (cada 2991 del mundo; S17: ~100 por segundo) más la red. El
  observador retira en cuanto el jinete va a pie aquí, o como mucho 1 s después del evento.
- **Respaldo sin evento:** podría retirar a un jinete que siga montado si sus 2991 dicen "a pie" (sin nombrar al
  caballo) durante 6 s. En S17, montado, todas las 2991 llevan al caballo como padre. Se cuenta y se anota.
- **Compromiso aplicado en el despacho de otro actor:** desde §10.7 (5) se aplica en el primer despacho tras el
  `Commit` (S17: ~10 ms), antes de la acción de montar del jinete, nunca después.
- **Geometría:** lecturas del `TransformSync` en el despacho (no en el frame que las produce). Es ruido de un
  frame (≤ 0,2 m a 10 m/s) y se ve igual en los dos PCs.
- **No cubre:** pose del jinete durante la animación de montar (el controlador no envía muestras entonces),
  entrada tardía y desmontar en marcha. Siguen como en §7.

### 10.7 Revisión independiente de `76c4a07` (rama `p8-mount-s18`)

"GO" condicionado para `mount_sync=1` en el anfitrión; todo corregido aquí.

| # | Hallazgo | Corrección |
|---|---|---|
| 1 ALTO | `0xE6F2E60` hace `mov rax,[rdx+0x68]; mov rcx,[rax+0x48]; mov rax,[rcx]` sin comprobar. Un caballo a medio crear o destruir = AV dentro del juego, que se deja pasar (crash real). El 2938 nativo comprueba `vc` y `vc+0xEB` antes | `msl::detach_decide`: solo se llama con jinete y caballo encontrados, los dos Vehicle, el Link del jinete legible, `vc+0xEB != 0` y `C8` del caballo 0. Si no, `Refused`. Si el vínculo ya no es de ese caballo, `already applied`. Vale para la retirada normal y para la de §3 |
| 2 MEDIO | vf53 desreferencia sin comprobar: jinete comps+0x40 (CC, +0xA8, `[+0x30]+0x1B` y el puntero de `CC+0x88+8·i`), comps+0x50; caballo comps+0x1A0, +0x90, +0x40, `actor+0x88` (+1) y, condicionalmente, comps+0xB0 | `vf53_deps_missing` (lecturas SEH) y `PairState::deps_missing` (bits `kDep*`). Cualquiera que falte da `NoVehicle`: no listo, se reintenta |
| 3 MEDIO | Un `Begin` nuevo vaciaba la transacción aunque el vínculo hecho aquí siguiera vivo. Con otro caballo: `RiderLinkedElsewhere`, cancelación, el vínculo viejo se queda y `RideOn` monta al jinete en el caballo **viejo** | `Txn::linked_here` (el caballo al que **este** cliente enlazó al jinete) se conserva entre transacciones. `judge_commit(…, linked_here)` da `RetireFirst`: se retira primero ese vínculo (con las guardas de 1, bandera 12) y se vuelve a juzgar. Un vínculo que no hizo este cliente sigue siendo `RiderLinkedElsewhere` |
| 4 MEDIO | En S17 el anfitrión no vio ningún `RideOff::Enter` del remoto: el desmontaje llegó como acciones a pie (padre −1). Con el vínculo, el `Detach` observado esperaba 3 s (el jinete andaba 3 s enlazado) y el respaldo de 6 s exigía `RideOff::Enter`, así que no saltaba nunca | `Detach` observado: se retira en el primer despacho con el jinete a pie aquí (o tras `RideOff::Enter`, o a 1 s), antes del manejador de esa acción. El respaldo ya no exige `RideOff::Enter`. "A pie" = padre −1 sin el caballo como objetivo (la acción de montar sí lo nombra) |
| 5 MEDIO-BAJO | Reclamo con `TryAcquire` y aplazamiento con `TryAcquireShared`: con contención, o con `NotReady`, la acción de montar pasaba sin vínculo y vf53 se aplicaba después en cualquier 2991 (enlazado sin `RideOn`) | Candados bloqueantes: exclusivo en el despacho y compartido en `defers` y `holding_now`. El candado nunca se tiene durante una llamada al juego. Se aplica en el primer despacho tras el `Commit`, de cualquier actor, y como muy tarde en la 2991 del propio jinete: un `NotReady` allí cancela (respaldo `ride_filter`). Fuera `kAnyDispatchMs`; `kMaxAttempts` 20 en despachos ajenos |
| 6 BAJO | Una transacción podía quedarse en `Applying` para siempre si un manejador de fuera capturaba el fallo; un `Failed` podía dejar medio vínculo y perder el `Detach` posterior; un `Detach` durante `Applying` se perdía | `expire`: reclamo de más de 2 s → `QUARANTINED` (ya no aplaza). `Detach` en `Applying` se encola y se planifica al terminar. `Detach` tras un compromiso fallido (o en cuarentena por él): un intento de retirada, nunca dos (`detach_tried`) |
| 7 BAJO | La tabla solo se vaciaba con el siguiente evento de la época nueva | `mount_sync_reset(why)` desde `start_epoch` y `begin_logout` (anfitrión) y desde `end_remote` (invitado) |
| 8 cosmético | `c_rel_host` nunca subía; `plan()` no publicaba la fase | `drain_held` cuenta las Req soltadas tras una retención; el despacho publica la fase tras planificar |

**Tests [V]:** `.\scripts\build.ps1`: 45/45. En `optd_mount_sync`:
- nuevos: `test_detach_guard` y `test_review_lifecycle` (`RetireFirst`, `linked_here` solo con `1`, reclamo atascado, `Detach` durante `Applying`, `Detach` tras un fallo una sola vez);
- `test_judge` comprueba cada bit `kDep*` y `RetireFirst` frente a `RiderLinkedElsewhere`;
- `test_no_event_detach` cubre el desmontaje como acciones a pie sin `RideOff::Enter`, el `Detach` observado retirado en el mismo despacho de la acción a pie y la espera de 1 s frente a 3 s;
- `test_table_*` adaptados al reclamo en el primer despacho.

Inis sin cambios respecto a §10.3.

## 11. Sesión 18: desmontar (`ride_dismount`)

Rama `p8-dismount` desde `opcion-d` `7ed013c`. No se lanzó el juego. Base: `p8-s18-desmontar.md` (forense) y
`p8-s18-desmontar-nativo.md` §5–§7 (estática). Build **1.0.0.2976**, RVA.

### 11.1 Causa [V código]

- `0x1EC9C00` monta cada entrada de 0x78 B de una acción recibida (2991/2723). En `0x1EC9C26` hace
  `or r8b,2; or [rcx+0x50],r8b`: **toda acción de red lleva el bit 1 de `+0x50`**. El bit 3 de `+0x51` sale del
  byte `+0x42` del bloque de red.
- `0x863880` (ClientCC vf44) llama al thunk `0x2329A90` (`E9 AB EF B2 0C` → `.xtls 0xEE58A40`) en `0x8639C8` con
  `RCX = CC` y `RDX = [rsp+0x20]`, las 2 entradas (bucle `0x8638B4`, paso 0x78, `ebp = 2`). Retorno: `exe+0x8639CD`.
  - **Revisión de `0e8605c` (8) [V, barrido E8/E9]:** ese retorno no distingue a quién llama a `0x863880`. Hay tres
    llamadores: `0x863830` (la ruta de 2991/2723), `0x864AF9` (`RestoreActionsFromLoadData`) y `0x9FEFF7`. "Ruta de
    red" en este documento significa esos tres. Las demás condiciones del desvío (jinete remoto atascado) son las
    que limitan.
  - Llamadores del thunk: `0x8639C8`, `0xA7A10E`, `0x232D4C1` y `.xtls 0xEE681B3`. Ningún E8/E9 apunta a
    `0x2329A91..94`.
- `0xEE58A40` copia las dos entradas a su pila (`0xEE58A7A`, `0x1EC9A80` campo a campo: sin punteros con cuenta de
  referencias) y solo trabaja sobre su copia. Toma 2 argumentos (sobrescribe `r8`/`r9` antes de leerlos).
  - `dil` = `+0x51 & 8` y `bl` = `+0x50 & 2` de cualquiera de las dos entradas (`0xEE58B2B`, `0xEE58B3F`).
  - Con `dil || bl` (siempre en una 2991) descarta **en silencio** si tiene `def+0xA & 0x40` alguna de estas:
    la pendiente `[CC+0x80]` capa 0 (`0xEE58C0E`) o capa 1 (`0xEE58C3D`), o la actual `[[CC+0x90]+0x10]`
    (`0xEE58C64`) o `[[CC+0x88]+0x10]` (`0xEE58C87`).
  - Sin esos bits va por `0xEE58B83` (la rama del chart local del controlador): copia a `[CC+0x80]`, `CC+0xA8 = 1`.
- La copia remota de un jugador (`actor+0x96 == 0` [E]) nunca ejecuta su chart (`0x85FBA0` → `0x232D2A0` →
  `0x232D5C0` → `0x2329A90` en `0x232D4C1`). En cuanto una acción `0x40` es la actual (`C9356CEB`, `412C2C2B`;
  `667E3B35` no la lleva), todas las 2991 siguientes se descartan: el jinete se queda en RideOff (método 7).

### 11.2 Qué hay: `[session2] ride_dismount` (anfitrión **e** invitado; por defecto `rewrite`)

| Valor | `ride_filter` (acciones de desmontar del jugador remoto) | Retirada del vínculo (`mount_sync=1`) |
|---|---|---|
| `rewrite` | todas reescritas (S17), también con transacción | S18: en el primer despacho a pie tras el `Detach` (≤ 1 s), bandera 12 |
| `partial` (arreglo A) | solo las `0x40` (`C9356CEB`, `412C2C2B` + las aprendidas); `667E3B35` pasa (o se aplaza a `mount_sync`) | nativa (abajo) |
| `native` (arreglo B) | aplazadas a `mount_sync` con transacción válida (S18) **y** el desvío de la puerta enganchado; si no, reescritas | nativa (abajo) |

**Retirada nativa** (`msl::DetachPolicy::Native`, nativo §5):
- El `Detach` del controlador ya no dispara nada. En cada despacho se lee el jinete remoto (`Link+0x10`, `mc+0xF0`):
  - si su vínculo ya no es ese caballo → `already applied`, sin llamar (lo hizo `vf89` → `0x397F40` al entrar en
    RideOff);
  - si sigue ≥ **2 s** después del `Detach` y el método **no** es 6/7 → **respaldo** `0x2228E40(&res, caballo,
    &clave, p4 0, bandera 0)` con las guardas de §10.7 (1) y `verify_detach`;
  - con el método en 6/7, o sin poder leer al jinete: espera (nunca una llamada a ciegas).
- El respaldo de 6 s sin evento sigue, con la misma condición de método y bandera 0.
- **Compromiso nuevo:** `Judge::StuckInRideOff` si `mc+0xF0 == 7` y `[mc+0xF8+7·8]+0xD4 ≠ 0`. Es "no listo": se
  reintenta y, en la acción de montar del propio jinete, se cancela (respaldo de `ride_filter`).

**Arreglo B: desvío del thunk `0x2329A90`** (solo `mount_sync=1` + `ride_dismount=native`):
- Parche de 5 bytes `E9 AB EF B2 0C`, el mismo tipo que `0xA45740`/`0x2228E40`. `install_one` exige que safetyhook
  mueva exactamente esos 5 bytes. El `jmp rel32` reubicado en el trampolín ya funciona en `0x2228E40`: la llamada del
  observador en S18 dio `APPLIED`.
  - Antes de instalar se verifican además, solo leyendo `.rsrc`, el sitio de llamada `0x8639C0`
    (`48 8D 54 24 20 49 8B CF E8 C3 60 AC 01 45 89 26`) y la marca de red `0x1EC9C26` (`41 80 C8 02 44 08 41 50`).
  - Con cualquier diferencia: `REFUSED` y nada escrito.
  - `.xtls` no se toca ni se lee en el proceso; sus bytes se verificaron en el PE instalado.
- **El desvío solo actúa si se cumple todo** (`msl::gate_decide`, lecturas SEH):
  1. retorno `exe+0x8639CD`;
  2. `[CC+8]` es el jugador remoto conocido (no el local), con `[[actor+0x68]+0x40] == CC` y `C8 == 0`;
  3. las entradas son legibles y la **entrada 0 tiene hash** (`0xEE58B83` copia las dos a la pendiente: nunca una
     entrada 0 vacía; revisión (2));
  4. llevan los bits de red y la puerta las descartaría (`0x40` actual o pendiente);
  5. el jinete está **atascado** (`msl::gate_stuck`, revisión (1)): hay una acción `0x40` **actual** y o bien
     `mc+0xF0 == 7` o bien `actor+0x96 == 0`, la copia que nunca ejecuta su chart. Así entra también un `0x40` actual
     fuera del método 7 (`RideOff::Enter` rechazado, o aún en el método 4). Con solo un `0x40` pendiente hace falta el
     método 7;
  6. `CC+0xA8 == 0` (nada pendiente en este fotograma), **o** una petición completa con las dos entradas con hash:
     la segunda acción de red del mismo fotograma tras un desbloqueo sustituye a la anterior (revisión (6));
  7. ninguna entrada nueva tiene `0x40` (no deja entrar otro RideOff);
  8. ninguna entrada nueva tiene `+0x52 & 1`.
- **Qué hace:** copia las 0xF0 B a un búfer de su propio marco, quita **solo** el bit 1 de `+0x50` y el bit 3 de
  `+0x51` de cada entrada y llama al original con `(CC, copia)`. Cualquier otra llamada pasa intacta, incluido el chart
  de todos los actores, que sale tras comparar el retorno.
- La comparación del retorno va antes de nada, sin marca del vigilante (revisión (9)). Solo la ruta de red entra en
  `gate_network`, que es la que lleva `FreezeMark`.
- Una excepción dentro de `0xEE58A40` con la copia **no se traga**: el arreglo B se apaga (`g_gate_off`), se escribe
  una línea `ERR` y se deja pasar (`game_fault_continue`).
  - El texto dice que la excepción pasa a los manejadores del juego. Si un marco exterior la maneja (un `throw`/`catch`
    de C++), no es un crash; solo apaga el arreglo B (revisión (5)).
  - Desde ese momento `ride_filter` vuelve a reescribir (`mount_sync_gate_ready()`).
- **Degradado (revisión (1)):** tras **8 acciones seguidas** del jinete que la puerta descarta sin desbloqueo, sale
  la línea `gate [fix B] … DEGRADED`.
  - `ride_filter` vuelve a reescribir los desmontajes (como en S17).
  - El desvío sigue intentando sacar al jinete.
  - Un desbloqueo, o un jinete que deja de estar atascado, pone la racha a 0.

**Sondas** (con `mount_sync` en `probe` o `1`, cualquier `ride_dismount`), en el despacho de cada 2991 del jinete
remoto (antes y después de su manejador):
- `MountSync gate probe`: método, hash y `def+0xA` de la acción actual de cada capa, `0x40` pendiente, `CC+0xA8`,
  `+0xD4` de RideOff, `actor+0x96` del remoto **y del local**, y el estado después.
  - Hay una línea cada vez que cambia esa vista y otra por cada 2991 que llega en el método 7 con `0x40`:
    `REJECTED`, `taken` o `UNLOCKED by fix B`. Tope de 200 líneas.
- `MountSync gate [fix B]` (solo `native`): cada llamada de red del jinete que la puerta descartaría, con su
  veredicto. Tope de 200 líneas.
- `MountSync gate … LEARNED`: cada hash con `def+0xA & 0x40` visto en el `RideOff::Enter` del jugador local o en la
  acción actual del remoto. Pasa a la lista de `partial` (8 huecos).
- **Autocomprobación** (anfitrión, toque del sniffer): cada `StartActionReq` 2223 **propia** (C>S) con la clave del
  jinete remoto o de su caballo se cuenta y da `WRN MountSync self-check …`. Tiene que ser 0.
  - Desde la revisión (4), **la primera apaga el arreglo B** (`OFF: the self-check saw a 2223 for the rider`), y
    `ride_filter` vuelve a reescribir. Desde §12.8 (2) lo **degrada** en vez de apagarlo (`DEGRADED by the self-check`).
- Resumen de cada 60 s: `P8 S18 ride_dismount … gate detour … would drop … rejected/taken/UNLOCKED … native
  retirement already done … fallback … self-check …`. En `RideFilter summary`: `ride_dismount`, las acciones `0x40`,
  los hashes aprendidos y `passed`.

**Ficheros:**
- `mount_sync{.cpp,.h,_logic.h}`, `ride_filter{.cpp,.h,_logic.h}`;
- `config.{h,cpp}` y `mod.cpp` (clave y orden: `ride_dismount_configure` antes de `ride_filter` y `mount_sync`);
- `session2.cpp` (toque del sniffer para la autocomprobación);
- los cinco inis y `tests/optd_mount_sync_tests.cpp`.

`remote_motion` no se tocó: las lecturas de la sonda van en el despacho de `mount_sync`, que ya está en el hilo
principal y ve el antes y el después del manejador.

**Tests [V]:** `.\scripts\build.ps1` compila sin avisos nuevos y pasan **45/45**. Nuevos en `optd_mount_sync`:
- `test_dismount_route`: `parse_dismount`, la tabla de `route` y `wants_mount_sync`, `GateHashes`
  (fijos/aprendidos/lleno) y `gate_dismount` (`667E3B35` no);
- `test_gate_decide`: cada condición de `gate_decide`, `gate_would_drop`, y que `gate_clear_bits` toca solo 2 bits por
  entrada;
- `test_judge_stuck`: `StuckInRideOff`, que se reintenta y que `AlreadyApplied` va antes;
- `test_native_detach_policy`: no al recibir el `Detach`, nunca a ciegas, nunca en 6/7, `already applied` si el
  vínculo ya no está, respaldo a los 2 s con bandera 0, `Detach` no observado, sin evento a los 6 s, la política
  sobrevive a `reset`, y S18 sin cambios.

**Inis:**
- `cdmp.p8-s7-host.ini` y `cdmp.p8-noche-local.ini`: `mount_sync=1`, `ride_dismount=native`. El comentario explica
  cómo retroceder: primero `partial`, luego `rewrite`.
- `cdmp.p8-s7-shadow.ini`, `-fase2` y `cdmp.p8-s12-shadow-seguro.ini`: `ride_dismount=rewrite`. El invitado sigue en
  `mount_sync=probe`, sin vínculo, así que RideOff no puede entrar bien allí.

### 11.3 Qué mirar en la sesión 19 (anfitrión con `native`; mismo guion de S17, desmontajes del invitado)

- Arranque: `Config: P8 mounts: mount_sync '1' ride_dismount 'native'`; `MountSync … ride_dismount=native: fix B`; en
  la línea de ganchos `MountSync 1 (mount_sync, host): …` aparece `the CC request gate thunk 0x2329A90 (jmp 0xEE58A40; fix B) hooked`.
- Al desmontar el invitado, en este orden:
  1. `RideFilter … dismount … -> DEFERRED`;
  2. `MountSync RideOff::Enter … (remote player) -> 1` con `Link 0xB…`;
  3. `gate probe … the gate's view of the rider changed` con **`def+0xA` de `C9356CEB` con `0x40`** (cierra la [E]
     de nativo §6.1) y `actor+0x96 remote 0` frente a `local ≠ 0`;
  4. la siguiente 2991 a pie: `MountSync gate [fix B] … -> UNLOCKED` y `gate probe … UNLOCKED by fix B`, con
     `after: method ≠ 7`;
  5. retirada: `MountApply [1] … detach … already applied` (nativa); el respaldo con `flag=0` solo si el vínculo
     sigue.
- Éxito: tras `RideOff::Enter -> 1`, `mc+0xF0` sale de 7 y la capa 0 cambia a la acción de a pie en ≤ duración de
  RideOff + 200 ms, con la animación completa en el vídeo del anfitrión.
- La montura siguiente: `RideOn::Enter (remote player) -> 1` (en S18 no entró). En el resumen: `UNLOCKED` = número de
  desmontajes, `rejected` ≈ 0 y `self-check … rider 0 horse 0`.

### 11.4 Criterios de aborto (volver a `partial` y después a `rewrite`, conservando logs y capturas)

- Cualquier `WRN MountSync self-check` o `self-check … rider N` con N > 0: el observador pidió acciones del jinete del
  invitado (2223 en el `cdmp_packets` del anfitrión con su clave).
- Un fallo o cuelgue cerca de `0x2329A90` / `0xEE58A40`: `MountSync: exception …` con `0xEE58A40`, `gate detour …
  (OFF after a fault)` o `GAME FAULTS passed on`.
- El jinete sigue atascado en el método 7: `gate probe … REJECTED` repetido, `UNLOCKED 0` con `rejected > 0`, o
  veredictos `Busy` / `Flag52` / `NewDismount` persistentes.
- Acciones del anfitrión afectadas: su personaje o su caballo dejan de cambiar de acción, saltan animaciones, o hay
  un `gate [fix B]` con una clave que no es la del invitado (no debería existir).
- Además, los de §8 y §10.5.

### 11.5 Riesgos

- **[H] Otros lectores de los bits.**
  - Barrido de bytes `test byte [r+0x50],2` / `[r+0x51],8` (disp8) en `.rsrc`, `.text`, `.xtext` y `.xtls`: 4 y 1
    sitios en `.rsrc` (`0xA672C7`, `0x439B066`, `0x4812F6D`, `0x48138D6`; `0x863BAC`).
  - `0x863BAC` está en `0x863A10` (CC, entradas): con el mismo hash ya actual, omite la actualización si
    `+0x50 & 0x80` y `+0x51 & 8`. Quitar el bit 3 en la copia puede cambiar ese caso, el de una acción repetida.
  - El código mutado de `.xtls` lee por `rbp`, así que el barrido no lo ve. La autocomprobación de 2223 cubre el
    efecto peligroso: que el observador pida la acción al servidor.
- **[E] Rama `0xEE58B83`:** copia **las dos** entradas a la pendiente aunque una venga vacía; la rama de red solo
  copia las que tienen hash.
  - Se exige hash en la entrada 0.
  - Con `CC+0xA8 == 0` no hay nada pendiente que se pierda.
  - Con `CC+0xA8 ≠ 0` solo entra una petición completa.
  - Queda el caso de una entrada 1 vacía con la pendiente ya tomada: copia un vacío en la capa 1, como hace el chart
    del controlador.
- **Dismount repetido sin vínculo** tras RideOff: el respaldo del despacho (`fallback_rewrite`) lo reescribe como en
  S17 (bloque `F79C7424` con el caballo de padre) y el desvío lo deja salir. Se vería un instante montado.
- **`partial` sin `mount_sync=1`:** `667E3B35` pasa sin vínculo (método 4, AnimationAll). No está medido sin
  vínculo; por eso el invitado usa `rewrite`.
- **`partial` reescribe al bloque montado `F79C7424`** (el probado en S17), no al de a pie: tras la primera parte de
  la animación puede verse un instante la pose de montar antes de la acción a pie.
- El desvío corre en **todas** las transiciones del chart de todos los actores. El camino rápido es una comparación
  del retorno, antes de la marca del vigilante (`g_mark[9]`, solo en la ruta de red).
- **`RestoreActionsFromLoadData` (`0x864AF9`)** comparte el retorno. Si restaura acciones de un jinete remoto atascado
  con los bits de red, el desvío también las desbloquea. Es el mismo efecto que se busca [E].

### 11.6 Revisión independiente de `0e8605c` (rama `p8-dismount-2`)

"GO" para S19 con estas correcciones:

| # | Hallazgo | Corrección |
|---|---|---|
| 1 MEDIO | Con un `0x40` actual y `mc+0xF0 ≠ 7` (`Enter` rechazado, o aún el método 4) daba `NotStuck`, y la puerta descartaba todas las 2991 para siempre | `gate_stuck`: `0x40` actual con el método 7 **o** `actor+0x96 == 0`. Además `GateStreak`: tras 8 descartes seguidos sin desbloqueo, `DEGRADED` y `ride_filter` reescribe |
| 2 MEDIO-BAJO | `0xEE58B83` copia las dos entradas aunque una no tenga hash | Veredicto `NoHash0`: sin hash en la entrada 0 no se desbloquea |
| 3 MEDIO-BAJO | En la política nativa, un jinete ilegible dejaba la transacción en `DetachPending` (o `Riding` a pie) para siempre: candado en cada 2991 y `defers()` seguía aplazando | Tras 15 s ilegible (`kNativeUnreadableMs`) pasa a `Detached` **sin llamar a nada** (`native_lost`, salida `Refused`). Conserva `linked_here` para un `RetireFirst`. Línea `detach args … FORGOTTEN` y contador `forgotten` en el resumen |
| 4 BAJO | La autocomprobación solo detectaba | La primera 2223 con la clave del jinete o del caballo pone `g_gate_off` (por qué: 2) |
| 5 BAJO | Una excepción manejada por un marco exterior se anunciaba como crash real | Texto nuevo: "passed on to the game's handlers … a crash only if nothing handles it" |
| 6 BAJO | `Busy` perdía la 2.ª acción de red del mismo fotograma tras un desbloqueo | Con `CC+0xA8 ≠ 0` se desbloquea si las dos entradas tienen hash |
| 8 doc | `network_call` agrupa los tres llamadores de `0x863880` | §11.1 corregido, verificado con un barrido E8/E9 |
| 9 rendimiento | `FreezeMark` antes de comparar el retorno | La comparación va primero; la marca solo en `gate_network` |

**Tests [V]:** `.\scripts\build.ps1`: 45/45.
- `test_gate_decide` amplía los casos: `NoHash0`, `Busy` frente a una petición completa, `0x40` actual fuera del
  método 7 con `actor+0x96` a 0 / 1 / desconocido, y un pendiente solo fuera de RideOff.
- `test_gate_streak` (nuevo): racha, reinicios, otros actores sin efecto, un solo aviso.
- `test_native_detach_policy` añade el olvido a los 15 s, con el reloj que se reinicia al volver a leerse, en
  `DetachPending` y en `Riding` a pie.

**Qué mirar además en S19:**
- `DEGRADED` no debe salir.
- `forgotten` debe ser 0.
- El resumen debe decir `gate detour hooked` sin `OFF`.
- Abortar también si sale `NoHash0` o `Busy` de forma persistente en las líneas `gate [fix B]`.

## 12. Invitado en modo 1 (fase B: el invitado observa al jinete del anfitrión)

Rama `p8-mount-guest` desde `opcion-d` `dea8e1a`. No se lanzó el juego ni se desplegó nada. Build **1.0.0.2976**, RVA.
Punto de partida: en S19 el anfitrión, con `mount_sync=1` + `ride_dismount=native`, vio montar y desmontar al jinete del
invitado con el vínculo nativo y el arreglo B (`UNLOCKED 5`, `native retirement already done 5`, 0 fallos). El código
es el mismo en los dos PCs; aquí se revisa todo lo que en el invitado va por otro camino.

### 12.1 Auditoría: el camino del invitado frente al del anfitrión

| Punto | Anfitrión (validado S19) | Invitado | Veredicto |
|---|---|---|---|
| Recepción de eventos | `on_guest_packet` (hilo de red del mod) | `guest_deliver` (hilo de red del mod) | Igual. El anfitrión publica en `push_outgoing` el anillo **antes** que los paquetes del servidor: un `Begin` precede siempre a la 2991 de montar [V S17/S18: `Begin` y `Commit` en el mismo lote, 0,4–24 ms antes de que `ride_filter` vea la acción] |
| Identidad del jinete remoto | `server_user2_player_key` | estado de control 4 del anfitrión = `[usuario anfitrión+0xD0]+0x60`, cada 1 s | Solo el personaje principal del anfitrión (`0xA0100001`). `on_event` da `NotRemote` a cualquier otra clave; `gate_view`, el despacho y `Enter` comparan con la misma clave y además `!= local`. Los vf53 de parejas de NPC (`0xB…/0xB…`) solo se anotan: nunca se publican ni se aplican |
| Retención | Req del usuario 2 en `g_early` | **todos** los paquetes del anfitrión en `guest_deliver`, en orden, ≤ 500 ms, 2048 / 4 MB | Se mantiene. En S17–S19 el invitado estaba en `probe`, donde no se retiene nunca (`holds guest 0` no prueba nada); la prueba es que los 5 `Begin` del anfitrión llegaron **en el mismo lote** que su `Commit` (mismo `qpc` en las líneas `observer`), así que con `1` la retención habría durado 0. Solo una carrera de ~60 µs en el anfitrión (la duración de vf53) los puede separar, y entonces dura un tick del túnel. `g_last_in_ms` se marca antes (host muerto / despedida intactos); la cola de entrada admite 1 M / 64 MB; `load_trip`, `guest_reload_wait` y 2322/2762 solo verían ≤ 500 ms de retraso durante una retención; el vigilante de cuelgues no ve nada (ningún hilo del juego espera). Retener solo los paquetes del jinete se descartó: reordena el flujo (sus Move/2991 frente a los 2005/2516 de su caballo, 2322…) a cambio de nada |
| Compromiso nativo | despacho 2991 `0xB57720` | igual | El invitado despacha ~160× más 2991 (S19: 75 036 frente a 460): el compromiso se reclama casi siempre en la 2991 de un NPC, 2–56 ms antes de la acción de montar (permitido desde la revisión (5)). Mismo contexto nativo |
| Puerta (arreglo B) | `actor+0x96` del remoto 0 | **[V S19 invitado]** `gate probe … actor+0x96 remote 0 local 1` para `0xA0100001` | `gate_stuck` vale igual. Ningún módulo del invitado parchea `0x2329A90`, `0x8639C0..CF` ni `0x1EC9C26` (`guest_seq_restore` en `0x864AEA` y P8Probe en `0x86392A` están fuera) |
| `ride_filter` | `OnRecvPacket` (NetworkThread), justo antes de la cola del hilo principal | `deliver_body`, **antes** de la cola de entrada del cliente | El aplazamiento se decide 10–35 ms antes del despacho (S17/S18), más si el hilo principal se para (tirón, carga). Ver 12.3 (2) |
| Autocomprobación 2223 | toque del sniffer | **no había** | Añadida (12.3 (3)) |
| Reinicios | `start_epoch`, `begin_logout` | `end_remote`; época del cliente | La tabla se vacía, pero la clave del anfitrión quedaba (12.3 (4)) |
| Otros módulos | — | `horse_follow` H→G (muestras derivadas del caballo del anfitrión), `remote_interp_pair`, `guest_seq_route`, `guest_fast_travel`, `guest_reload_wait` | Las muestras H→G son el espejo de `HorseFollowReverse` G→H, que en S19 funcionó con el vínculo (`linked here` ≤ 0,5 m casi siempre). `remote_interp_pair` es simétrico. `guest_seq_route` solo enruta 2661/2810/2322. `guest_fast_travel` veta el viaje propio. Una recarga del invitado destruye la copia (y su vínculo): el desmontaje siguiente llega sin vínculo y el despacho lo reescribe (S17) |

### 12.2 Lo que habría hecho el invitado (sondas S17–S19)

Logs: `backups\p8-sesion17-shadow\cdmp_20261011-140408.log`, `p8-sesion18-shadow\cdmp_20261011-151520.log`,
`p8-sesion19-shadow\cdmp_20261011-165224.log`.

| Sesión / transición del anfitrión | `Commit` → `commit args` (hilo principal) | `ride_filter` (en `deliver_body`) | Juicio y argumentos |
|---|---|---|---|
| S17 1 | 522,2165 → 522,2486 s (32 ms) | `91605616` a 522,221 s (5 ms tras el `Commit`): con `1`, `DEFERRED` | `apply`, intento 1 |
| S17 2 | 15 ms | `91605616` 0,4 ms tras el `Commit` | `apply` |
| S17 3 | 30 ms | `875B47E6` no está en la lista: pasa. En probe `RideOn::Enter (remote player) -> 0, Link 0` | `apply`: con `1` el vínculo ya estaría y `Enter` daría 1 |
| S17 4 | 27 ms | — | `apply` |
| S18 1 | 449,0799 → 449,1144 s (35 ms) | `872A6750` a 449,104 s (24 ms tras el `Commit`) | `apply` |
| S19 | el anfitrión no montó (`observer events [none]`) | — | — |

- Los 5 juicios: `C8 rider 0 horse 0`, `same controller yes`, `0x2228260(RCX vc, RDX &res, R8 rider, R9b 0, [rsp+0x28] 0)`,
  intento 1. Coinciden con lo que el anfitrión aplicó en S18/S19.
- Desmontajes del anfitrión vistos en el invitado (S17, resumen de `RideFilter`): `667E3B35/7B6AFF57`, `C9356CEB/7B6AFF57`
  (0x40), `F79C7424/7B6AFF57`, `00000000/7B6AFF57` y `412C2C2B/7B6AFF57` (0x40). Las mismas familias que el invitado en
  S19: con `native` se aplazan, RideOff entra con vínculo y el arreglo B saca a la copia en la siguiente 2991 a pie.
- El `Detach` observado del anfitrión llega con `flags 0x9`, bandera 12 (S18: 485,36 s). Con la política nativa no dispara
  nada por sí mismo.
- En las tres sesiones: 0 `REFUSED`, 0 fallos del juego, `self-check … 0`. La retención no se puede medir en `probe`;
  `Begin` y `Commit` llegaron siempre juntos.

Ningún dato dice que algo saldría mal en la ruta normal. La auditoría encontró dos problemas de **temporización**
propios del camino más largo del invitado y dos huecos de seguridad (12.3).

### 12.3 Cambios

1. **Respaldo nativo solo tras una acción a pie** (`msl::Table::native_due`, los dos PCs). El `Detach` es un paquete de
   control y entra en la tabla al momento; la 2991 de desmontar puede seguir en la cola de entrada del cliente (en el
   invitado pasa por `g_from_host` → NetworkThread → cola del hilo principal; con el hilo principal parado, por un tirón o una carga,
   el atasco se despacha luego en orden). Antes, a los 2 s del `Detach` el primer despacho del atasco (el de un NPC)
   retiraba el vínculo **antes** de `RideOff::Enter` de esa acción (`Enter 0`, el defecto de S9). Ahora el respaldo exige
   además `on_foot_ms`: una acción a pie del jinete ya despachada aquí. Va después del desmontaje en el flujo, así que
   cuando llega, `RideOff` ya corrió (vínculo ido: `already applied`) o no va a correr (vínculo sobrante: respaldo). En
   S19 el respaldo no saltó nunca (`fallback 0`), así que el anfitrión validado no cambia.
2. **Reescritura en el despacho si el arreglo B se perdió** (`msl::fallback_rewrite(…, gate_lost)`,
   `msl::dismount_gate_lost`). `ride_filter` aplaza un desmontaje con `native` si `mount_sync_gate_ready()`; si entre esa
   decisión y el despacho el arreglo B se apaga o pasa a `DEGRADED`, la copia entraría en RideOff sin salida (S18). Ahora
   el despacho la reescribe como haría `ride_filter` desde ese momento (línea `FALLBACK … fix B not ready any more`); el
   vínculo lo quita el respaldo de (1).
3. **Autocomprobación 2223 en el invitado** (`mount_sync_guest_outgoing`, en `guest_send`). Cada paquete del cliente para
   el anfitrión: una `StartActionReq` 2223 con la clave del jinete del anfitrión o de su caballo (`msl::self_check_hit`)
   se cuenta, da `WRN MountSync self-check … tried to send … WITHHELD` y degrada el arreglo B (12.8 (2)). Con `1` además **no se
   envía** (se da por enviada: la conexión sigue). En `probe` solo se cuenta. Resumen: `… (must be 0; withheld N)`.
4. **La clave del anfitrión solo mientras se juega en él** (`guest.cpp`, `mount_sync_reset`). Tras `end_remote` el invitado
   puede cargar una partida en su propio servidor, donde **su** personaje puede tener la misma clave `0xA0100001` (el
   primer usuario de cualquier servidor). Con la clave vieja, el despacho trataba al jugador local como "jinete remoto" y
   el respaldo podía reescribir sus propias 2991 de montar/desmontar (las del arreglo B y las llamadas las paraban sus `C8`
   1/2, de milagro). Ahora `end_remote` borra la clave, y los eventos y el estado de control 4 solo se aceptan en modo
   remoto.

Tests [V]: `.\scripts\build.ps1` sin avisos nuevos, **45/45**. En `optd_mount_sync`: `test_native_detach_policy` exige la
acción a pie; `test_guest_observer` (nuevo) cubre la clave del anfitrión (`NotRemote` con 0 o con otra clave), el atasco
del hilo principal (5 s sin despachos y luego el atasco en orden: nada hasta `RideOff`, luego `Gone`), el desmontaje
reescrito y el respaldo tras la acción a pie, la tabla de `fallback_rewrite` / `dismount_gate_lost` y `self_check_hit`.

### 12.4 Inis

- `cdmp.p8-s7-shadow.ini` y `cdmp.p8-s7-shadow-fase2.ini`: `mount_sync=1`, `ride_dismount=native`. El comentario da el
  retroceso: 1) `ride_dismount=partial`, 2) `rewrite`, 3) `mount_sync=probe`; apagar con `mount_sync=0`.
- `cdmp.p8-s12-shadow-seguro.ini`: sigue en `probe` / `rewrite` (plan B).
- Anfitrión sin cambios (`1` / `native`). Mismo `cdmp.asi` en los dos PCs: los cambios (1) y (2) también corren en el
  anfitrión.

### 12.5 Qué mirar en el log del invitado (sesión 20)

Guion: el de S17 (§5), con montajes y desmontajes **del anfitrión** (al paso, al galope, por los dos lados), uno con el
invitado entrando en alcance tarde.

- Arranque: `Config: P8 mounts: mount_sync '1' ride_dismount 'native'`; `MountSync: mount_sync=1 on the guest … fix B`;
  `MountSync 1 (mount_sync, guest): …` con los nueve puntos y `the CC request gate thunk 0x2329A90 … hooked`.
- Resumen: `keys local 0xA01003xx remote 0xA0100001` (nunca `remote 0x00000000` mientras se juega en el anfitrión).
- **Montar (anfitrión)**, en este orden:
  1. `MountSync observer … MountBegin rider 0xA0100001 horse 0xB0100003 … -> begun` y `… MountCommit … -> committed`;
  2. `RideFilter [1] … 0xA0100001 mount … -> DEFERRED to mount_sync` (o la acción de montar sin clasificar, p. ej.
     `875B47E6`);
  3. `MountSync commit args 0x2228260(… R9b seat=0, [rsp+0x28] fifth=0)` y `MountApply [1] … commit … judge: apply ->
     APPLIED | post: ok | … Link 0 -> 0xB0100003/0 | … count 0 -> 1 | … same controller yes`;
  4. `MountSync RideOn::Enter … 0xA0100001 (remote player) -> 1 | Link 0xB0100003/0`.
- **Desmontar (anfitrión)**: `RideFilter … dismount … -> DEFERRED`; `RideOff::Enter … (remote player) -> 1` con
  `Link 0xB…`; `gate probe … def+0xA 0x4…` de `C9356CEB`/`412C2C2B`; en la siguiente 2991 a pie `gate [fix B] … ->
  UNLOCKED` y `gate probe … UNLOCKED by fix B`; `observer … MountDetach … flags 0x9 -> detach pending`; `detach args … the
  native retirement already ran here` y `MountApply [1] … detach … already applied`.
- Resumen de 60 s: `observer events [begun N, committed N, detach pending N]`, `judges [apply N]`, `outcomes [APPLIED N,
  already applied N]`, `post failures 0`, `holds guest 0` (o soltadas), `cancelled 0`, `fallback rewrites 0`, `UNLOCKED`
  = desmontajes, `rejected 0`, `fallback 0`, `forgotten 0`, `gate detour hooked` sin `OFF`/`DEGRADED`,
  `self-check … rider 0 horse 0 (must be 0; withheld 0)`, `game faults 0`; geometría `linked here` con `|off the seat|`
  ≤ 0,5 m en su mayoría a 6–10 y ≥ 10 m/s.
- Ninguna retirada antes de `RideOff::Enter`: ninguna línea `native FALLBACK` en un desmontaje con
  `RideOff::Enter (remote player) -> 1`.
- En el anfitrión, por cada desmontaje suyo: `MountDetach (observed)`; nada nuevo en su papel de observador.

### 12.6 Criterios de aborto (invitado)

Retroceder (en el invitado: `partial`, luego `rewrite`, luego `mount_sync=probe`; conservar los logs y las capturas de
los dos PCs) ante cualquiera de los de §8, §10.5 y §11.4 vistos en el log del invitado, y además:
- cualquier `WRN MountSync self-check` (retenida o no): el cliente del invitado intentó pedir acciones del jinete o del
  caballo del anfitrión;
- `MountApply … FAILED` / `QUARANTINED`, `MountSync: exception …`, `GAME FAULTS passed on`, `(OFF after …)`,
  `DEGRADED`, `REFUSED` en un punto, o un cuelgue al montar o desmontar el anfitrión;
- `RideOn::Enter … 0xA0100001 (remote player) -> 0` con `Link` presente, o la copia del anfitrión parada en el método 7
  (`gate probe … REJECTED` repetido, `UNLOCKED 0` con `rejected > 0`);
- cámara, control o personaje del **invitado** reaccionando a la monta del anfitrión (`same controller NO`, `actor+0x90`
  cambiado);
- en el **anfitrión**: su personaje o su caballo cambian de acción sin que él lo haga, o su `cdmp_packets` muestra una 2223
  del usuario 2 con `0xA0100001`/`0xB0100003`;
- `transaction(s) cancelled (the guest's held stream reached its bound)` o `cancelled` que crece sin desmontajes;
- líneas `FALLBACK … fix B not ready any more`: no son un aborto en sí (la copia termina de pie como en S17), pero piden
  pasar a `partial`.

### 12.7 Riesgos

- **Primer compromiso en el invitado [E]:** mismo cuerpo que el anfitrión aplicó en S18/S19 (`APPLIED`, `post: ok`).
- **Vínculo puesto 2–56 ms antes de la acción de montar** (reclamo en la 2991 de un NPC). Una acción a pie del jinete en
  medio pasa por `vf89` → `0x397F40`, que solo retira si la acción **anterior** tenía `0x40`/`0x80` [V nativo §1.1]; antes
  de montar no la tiene.
- **`kMaxAttempts` 20 en despachos ajenos** son ~0,2 s en el invitado (≈ 100 despachos/s): un `NotReady` pasajero
  (caballo recién creado, `deps_missing`) cancela antes que en el anfitrión. Sale el respaldo de S17 para esa montura,
  sin daño. En los datos, los 5 juicios fueron `apply` al primer intento.
- **Respaldo nativo más tardío** (12.3 (1), los dos PCs): un jinete que se queda quieto tras una retirada sin `RideOff`
  conserva el vínculo hasta su siguiente acción a pie (o como mucho 30 s, 12.8 (3)). Sus acciones dicen "montado"
  mientras tanto, así que no se ve.
- **2223 retenida:** el cliente del invitado no recibe respuesta a una Req sobre un actor que no controla. No debería
  existir (S19 anfitrión: 0).
- **Retención de todo el flujo:** no observada; si apareciera (`holds guest > 0`), retrasa como mucho 500 ms y está
  acotada.
- Sin cubrir, como en §7: entrada tardía (el invitado que llega con el anfitrión ya montado ve S17 hasta el siguiente
  montaje) y la pose durante la animación de montar.

### 12.8 Revisión independiente de `458cf49` (rama `p8-mount-guest-2`)

"GO" para S20 como prueba vigilada, con estas correcciones:

| # | Hallazgo | Corrección |
|---|---|---|
| 1 MEDIO | `end_remote` solo pone `g_broken`; `g_mode` sigue `Remote` hasta la siguiente conexión del cliente. Con el túnel vivo (cola llena, fallo en `OnRecvPacket`, Q3, memoria, caída y vuelta del túnel) un estado de control 4 tardío volvía a escribir `0xA0100001` en la clave; en el título el segundo `end_remote` no hace nada (ya roto) y la tabla no se reiniciaba otra vez; una partida local cuyo personaje sea `0xA0100001` (con `g_local_key` viejo) pasaba la guarda `!= local` | `guest.cpp`: eventos de montura, `mount_sync_set_remote_key` y `ride_filter_guest_control` solo con `Remote` **y** sin `g_broken` (`remote_live_for_keys`). `mount_sync.cpp`: en el invitado `remote_key()` devuelve 0 salvo con `guest_remote_live()` (remoto, túnel arriba, no roto): sin clave no hay jinete remoto para el despacho, el desvío ni `Enter`. Un cambio de época en `guest_send` repite `mount_sync_reset` aunque la conexión ya estuviera rota |
| 2 MEDIO-BAJO | Apagar el arreglo B (autocomprobación o fallo) con la copia ya en RideOff la dejaba atascada para siempre: el desvío lo dejaba pasar todo | La autocomprobación (`why 2`) ya no apaga: **degrada** (`g_gate_degraded`; `ride_filter` reescribe los desmontajes nuevos, así que no entra otro RideOff) y el desvío sigue sacando a un jinete ya atascado. Solo un fallo dentro de `0xEE58A40` (`why 1`) lo apaga del todo. Resumen: `DEGRADED by the self-check …` |
| 3 BAJO | `on_foot_ms` no se borraba con el `Detach` y no había tope: una marca a pie de antes del `Detach` anulaba la guarda de 12.3 (1) | Con la política nativa, `take_detach` pone `on_foot_ms = 0` (el `Detach` sale 16–31 ms después del `RideOff::Enter` del controlador y sus acciones a pie ~1 s después: nunca se pierde una de verdad más que hasta la siguiente). Tope `kNativeFallbackCapMs` = 30 s: un vínculo sobrante con el método fuera de 6/7 se retira aunque no haya acción a pie |
| 4 menor | La 2223 retenida no se contaba en G4 de `guest_scene_diag` | Se cuenta antes de vaciar el paquete |
| 6 doc | §12.1 citaba `holds guest 0` como prueba (en `probe` no se retiene nunca) | Corregido: la prueba es que `Begin` y `Commit` llegaron en el mismo lote |

Tests [V]: `.\scripts\build.ps1` 45/45. `test_guest_observer` añade la marca a pie anterior al `Detach` (no cuenta), el
tope de 30 s (nunca en 6/7) y que la política S18 no cambia.

**Qué mirar además en S20 (invitado):** el resumen con `keys … remote 0xA0100001` mientras se juega en el anfitrión y
`remote 0x00000000` tras cualquier `the connection to the host's server stops`; `DEGRADED by the self-check` no debe
salir (es un aborto, §12.6).

## 13. Sesión 21: sentar al jinete remoto (`mount_seat`) y la pose al montar (`ride_hold_pos`)

Rama `opcion-d` tras `43ec349`. No se lanzó el juego ni se desplegó nada. Build **1.0.0.2976**, RVA. Punto de partida:
en S20, con `mount_sync=1` en los dos PCs, cada observador reproduce la montura del otro (vf53 `0x2228260` y el vínculo),
pero quedan dos defectos visibles: el jinete remoto **se separa de la silla** (hasta 1,4 m) y **parpadea al montar**.
Causas por análisis estático (capstone sobre el PE instalado).

### 13.1 Defecto 1: el jinete se sale de la silla

- **[V]** El juego solo sienta a un jinete con `ClientVehicle` vf54 `0xA45760`, y siempre justo después de vf53 con los
  mismos argumentos: el controlador (`.xtls 0xEE609B8` / `0xEE609DB`) y el recompromiso de parejas remotas de NPC
  `0xA467E0` (vf53 en `0xA469EA`, vf54 en `0xA46A08`; en S20 salen como `MountSync vf54 … caller exe+0xA46A0E | client`).
- **[V] Convención de vf54** (`0xA469F7..0xA46A08`): `mov rax,[rsi]; movzx r9d,r14b; mov r8,rbx; lea rdx,[rbp+0x7F];
  mov rcx,rsi; call [rax+0x1B0]`. RCX vc, RDX `&res`, R8 jinete, R9 = el asiento extendido con ceros. A diferencia de
  vf53 (`mov byte [rsp+0x20],0` en `0xA469D7`, el quinto byte), vf54 **no tiene argumento de pila**: su cuerpo
  (`0xA45760..0xA45A7C`) guarda RBX/RBP/RSI en la zona de inicio y no lee nada por encima. Devuelve `&res` (el llamador
  hace `mov eax,[rax]` en `0xA46A0E`).
- **[V]** vf54 lee `[[jinete+0x88]+1]` sin comprobar (tabla `0xA45A80`): 1, 3..6, 8, 9 van al asiento (`0xA457C4`); 7 y
  el resto dejan un código de error en `*res`. Luego `0x2228A70(vc, &tmp)` (sale si no es 0) y `0xA462B0(vc, jinete,
  asiento)`, que construye la información de enganche de **tipo 4** (`mov byte [rbp-0x69],4`, `0xA4636A`) y, para un
  caballo de tipo `[[caballo+0x88]+1]` 1, 3..6, 8 o 9 (tabla `0xA4660C`; el 7 va a otra rama, `0xA46491`), llama a
  `0xA651E0([CC+0x100], caballo, &info)` con CC = `[[jinete+0x68]+0x40]`.
- **[V]** `0xA651E0` engancha al jinete al hueso del asiento: tipo `[[CC+0x100]+8]` = 4 y el padre de jerarquía
  `[[[actor+0x68]+0x1B8]+0x1C]` = clave del caballo **[E: el desplazamiento del padre; se comprueba en S21]**.
- **[V]** La actualización de movimiento por fotograma `0x88EB70` llama a `0x87BA80`, que da al actor por enganchado
  con el tipo `[[CC+0x100]+8]` en {2, 4..8}; con tipo 4 y un vehículo de tipo != 7 (`0x88EE7B`) **no** llama a la
  interpolación ni a la aplicación de red (`0x88F4E0` / `0x891180`): el jinete sigue el hueso de la silla de este
  cliente.
- **Causa:** el observador del mod solo llamaba a vf53. El vínculo quedaba bien, pero el tipo de enganche seguía en 0 y
  el jinete se colocaba con sus propias muestras de red, que llegan por otro camino y con otro retraso que las del
  caballo: hasta 1,4 m de separación.
- **Liberación [E]:** nativa, `0x2228E40` (RideOff, la que el mod ya llama al retirar, bandera 12) → `.xtls 0xE6F2E60` →
  vf42 del vc del jinete `0xA45CA0` (un salto a `.xtls`) → `0xA65960(CC+0x100, …)` cuando `[CC+0x228] != 0`. Que
  `CC+0x228` sea la marca que pone `0xA651E0` es **[H]**: el log de S21 la enseña antes y después de vf54.

### 13.2 Defecto 2: parpadeo al montar

- **[V/E]** En el observador, `RideOn::Enter` `0x681C80` llama en `0x681DBC` a `0x88E520(mc)` (cuerpo en `.xtls`), que
  vacía el búfer de interpolación del jinete remoto y pone el byte `[mc+0x3D0]` = 1 (se borra al llegar una muestra
  nueva, `0x87DB2A`). Durante los 1–2 s de la animación de montar el controlador no manda ningún Move.
- **[V]** Cada fotograma, la actualización de modo 2 `0x88E6A0(RCX mc, XMM1 dt, R8 out)` hace `cmp byte [rcx+0x3D0],1;
  je 0x88E8B6` (`0x88E6D3` / `0x88E6D6`) y vuelve **sin escribir** `out`, una estructura de pila que su llamador puso a
  cero (`0x88F000`). El jinete se aplica en (0,0,0) del sector (0,0) uno o más fotogramas: el parpadeo.
- **[V]** Formato de `out`: `float pos[3]` en +0x0/+0x4/+0x8 (dentro del sector) e `int16` del sector en +0xC/+0xE
  (`0x88E82A` / `0x88E82E`; escala 1000.0 en `0x5D736F4`). La propia función lee la pose actual con `0x4F8220` sobre
  `TS = [[actor+0x68]+0x1A0]`, cuyo camino rápido copia exactamente `TS+0xB4` (8 B), `TS+0xBC` (4 B), `TS+0xC0` (u16) y
  `TS+0xC2` (u16): los mismos 16 bytes. El actor de `mc` es `[[mc+0x60]+8]`. Único llamador directo: `0xAB05CD` (un
  método de movimiento por vtable; RCX = `[CC+0xC8]`), que no usa el valor de vuelta.

### 13.3 Qué hay (anfitrión **e** invitado; las dos por defecto `0`)

**`[session2] mount_seat = 0 | probe | 1`** (solo con `mount_sync=1`; con `probe` en `mount_sync` queda en 0 y lo dice el
arranque).

1. En `do_commit`, si el resultado de vf53 es `APPLIED` (o `already applied`), se lee al jinete (`read_seat`: CC, mc,
   `[CC+0x100]+8`, padre, `CC+0x228`, tipos de jinete y caballo, todo con lecturas protegidas) y se juzga
   (`msl::seat_judge`): **aplicar** solo si el `Link` es de este caballo y asiento 0, el jinete es de tipo 1, el caballo
   de un tipo que `0xA462B0` sienta (nunca 7), la estructura de enganche se lee y su tipo es 0; tipo 4 bajo este
   caballo = `already seated`; cualquier otro caso `REFUSED (motivo)`.
2. La llamada: `guarded_vf54(vc, &res, rider, 0)` por el **original** del gancho de vf54 (`g_h_vf54`, así `vf54_detour`
   no ve la llamada propia), con `t_applying` y el mismo patrón SEH que vf53 (`apply_fault`: la excepción sigue al
   juego y todas las llamadas nativas se apagan). Sin el gancho de vf54 no se llama.
3. Comprobación (`msl::seat_verify`, solo lectura): `*res == 0`, tipo 4 y padre = caballo. Un `FAILED` apaga
   `mount_seat` para el resto de la sesión (nunca un segundo intento); si el jinete quedó en tipo 4, se vigila igual.
4. **Liberación**, en el despacho 2991 del hilo principal (≤ cada 30 ms): se espera desde la retirada del observador
   (`MountApply … detach` aplicada o ya hecha), desde `RideOff::Enter` del jinete remoto aquí o desde su primera acción a
   pie que no nombra al caballo (una acción montada anula esta última). Línea `MountSeat release … attach type 4 -> 0 N
   ms after …`. Si sigue en 4 a los 1500 ms y el `Link` sigue nombrando al caballo: **una** retirada
   `0x2228E40(&res, caballo, &clave, 0, 12)` por `retire_native` (con `detach_decide`), nunca en RideOn/RideOff (6/7)
   salvo pasados 6 s. Si el `Link` ya no está (o tras esa retirada sigue en 4): una línea `ERR MountSeat STUCK SEATED`,
   contada; **no** se llama a `0xA65960` en esta versión. Una clave que deja de ser la del jinete remoto (o pasa a ser
   la local) deja de vigilarse.
5. Log: `MountSeat [1] … 0xA0100001 on 0xB0100003 seat 0 (vf53 APPLIED, transition N): vf54 0xA45760(vc=…,&res,
   rider=…,0) -> *res 0x00000000 | attach type 0 -> 4, hierarchy parent 0x… -> 0xB0100003, CC+0x228 0 -> 1 | rider type
   1 horse type 5 | N us | SEATED` (o `FAILED (motivo)`, `already seated`, `REFUSED (motivo)`, `probe: would call vf54
   …`). Resumen de 60 s: `P8 S21 mount_seat 1: seat [seated N, already N, refused N, failed N, released N, retired N,
   stuck N]`.
6. **Geometría**: los cubos pasan a `seated here` (tipo 4) / `linked here` (vínculo, no sentado) / `NOT linked here`, y
   un contador `frozen` (sentado en dos lecturas seguidas, el jinete se movió < 1 cm y su caballo > 0,5 m).

**`[session2] ride_hold_pos = 0 | probe | 1`** (independiente de `mount_seat`; se instala con `mount_sync`; `1` solo
escribe con `mount_sync=1`, si no queda en `probe`).

- Gancho inline en la **entrada** de `0x88E6A0`: 7 bytes parcheados (`mov rax,rsp; mov [rax+0x10],rbx`) de 19
  verificados (`48 8B C4 48 89 58 10 48 89 70 18 57 48 81 EC 90 00 00 00`); ningún salto cae en +1..+6. Si no
  coinciden: una línea `REFUSED` y nada escrito.
- El desvío lee `[rcx+0x3D0]` con `fast_read`, llama al original y, si el byte era 1 y el actor es **el jinete remoto**
  (nunca la clave local), en `1` rellena `out` con `TS+0xB4..+0xC3` (16 bytes) solo si todo se lee, la posición es finita
  y no es la pose vacía (sector 0/0 con la posición cerca de 0). En `probe` solo cuenta. Las vueltas tempranas de
  **otros** actores `0xA` solo se cuentan (nunca se escribe). Sin cerrojos, sin reservar memoria, sin log (corre en los
  ParallelTaskWorker para cada actor remoto, cada fotograma).
- Resumen: `ride_hold_pos 1 (hooked): hold [remote early returns N, filled N, unreadable N, other 0xA early returns N]`.

Con `mount_seat=1` el jinete sentado ya no pasa por `0x88F4E0` (13.1), así que `ride_hold_pos` cubre sobre todo el caso
sin asiento (`REFUSED`, `probe`, o antes de la confirmación).

Tests [V]: `.\scripts\build.ps1` sin avisos nuevos, **45/45**. En `optd_mount_sync`: `test_seat_judge` (cada rama),
`test_seat_verify`, `test_seat_release` (liberación nativa, plazo → una retirada → atascado, vínculo ido → atascado,
espera en 6/7 con tope, acción montada que anula la espera a pie, jinete ilegible), la división de la geometría y
`frozen`, y `test_hold_pos` (`hold_who`, `hold_pose_ok`, `hold_decide`).

### 13.4 Inis

- Anfitrión `cdmp.p8-s7-host.ini` y `cdmp.p8-noche-local.ini`, invitado `cdmp.p8-s7-shadow.ini` y
  `cdmp.p8-s7-shadow-fase2.ini`: `mount_seat=1`, `ride_hold_pos=1`. Retroceso: `probe` y luego `0` en cada una.
- Plan B `cdmp.p8-s12-shadow-seguro.ini`: `mount_seat=0`, `ride_hold_pos=probe` (allí `mount_sync=probe`).

### 13.5 Qué mirar en la sesión 21 (guion de S17/S20: montar y desmontar los dos, al paso y al galope)

- Arranque: `Config: P8 mounts: … mount_seat '1' ride_hold_pos '1'`; `MountSync: mount_seat=1: vf54 0xA45760 seats …
  | ride_hold_pos=1: …`; en la línea de ganchos `the mode-2 movement update 0x88E6A0 (ride_hold_pos) hooked (1)`.
- **Montar** (en el observador), tras `MountApply [1] … commit … APPLIED`: `MountSeat [1] … SEATED` con `*res
  0x00000000`, `attach type 0 -> 4`, `hierarchy parent … -> 0xB…` (la clave del caballo) y `CC+0x228` (anotar el
  cambio: confirma o descarta la [H] de 13.1). Visualmente: el jinete pegado a la silla también al galope.
- **Desmontar:** `MountSeat release … attach type 4 -> 0 N ms after its RideOff::Enter here` (o `the observer's
  retirement here`), con N pequeño. Ningún `STUCK SEATED`, ningún `still seated … 0x2228E40 … once` (si sale y queda
  `released` justo después, la liberación nativa no corre: apuntarlo).
- Resumen: `seat [seated N, already 0, refused 0, failed 0, released N, retired 0, stuck 0]` con `seated` = `released` =
  monturas; geometría `seated here` con `|off the seat|` en el primer cubo a todas las velocidades, `linked here` casi
  vacío, `frozen 0`.
- `hold [remote early returns N, filled N, unreadable 0, …]`: `filled` = vueltas tempranas al montar; sin parpadeo en la
  captura. `other 0xA early returns` solo informa.

### 13.6 Criterios de aborto (`mount_seat` → `probe` → `0`; `ride_hold_pos` → `probe` → `0`; conservar logs y capturas)

Además de los de §8, §10.5, §11.4 y §12.6:
- `MountSeat … FAILED` (vf54 con `*res != 0`, o tipo distinto de 4 / padre que no es el caballo después);
- `frozen` > 0 en el resumen, o el jinete visiblemente clavado en el aire mientras el caballo anda;
- `ERR MountSeat STUCK SEATED` tras un desmontaje (o `retired` > 0 sin `released` después);
- la cámara, el control o el personaje **local** reaccionando cuando el otro monta (vf54 toca más que el jinete:
  `0xA48E50` con `r8b 1` sobre un global en `0xA4581F`);
- cualquier `WRN` del self-check;
- cualquier fallo del juego (`MountSync: exception …`, `GAME FAULTS passed on`, `native calls OFF`), o `REFUSED` en la
  instalación de `0x88E6A0`;
- `hold … unreadable` que crece, o un parpadeo nuevo con `filled` > 0 (la pose escrita no es la buena).

### 13.7 Riesgos y dudas

- **Momento del asiento:** el mod llama a vf54 en el despacho, antes del `RideOn::Enter` de la acción de montar (como
  el controlador, que también hace vf53 + vf54 en su compromiso; su orden frente a su propio RideOn no se midió [E]).
  Si la animación de montar espera tipo 0, puede verse un salto a la silla al empezar: apuntarlo con captura.
- **`CC+0x228` [H]** y el padre de jerarquía `comps+0x1B8`/`+0x1C` [E]: si el padre no es el que dice 13.1, la
  comprobación da `FAILED (hierarchy parent not the horse)` y `mount_seat` se apaga solo; el jinete queda vigilado.
- **vf54 tiene efectos fuera del jinete** (`0xA48E50` sobre un global, `0xA45AC0` sobre el vc del jinete con la clave
  del caballo): no analizados; de ahí el criterio de la cámara local.
- **La retirada de respaldo** (1,5 s) usa la misma llamada guardada que las demás retiradas (bandera 12) y nunca en
  RideOn/RideOff antes de 6 s; con la política nativa (`ride_dismount=native`) el vínculo ya suele haberse ido, y
  entonces solo se informa (`STUCK SEATED`).
- **`ride_hold_pos`** corre en hilos del juego para cada actor remoto: el coste es una lectura protegida de un byte por
  llamada; la parte cara solo con el byte a 1. La cadena "estructura puesta a cero por `0x88F000`" va por vtable y no se
  siguió entera aquí **[E]**.

## 14. Sesión 22: el tipo 9 es "sin enganche" (S21 no sentó a nadie) y la causa real del parpadeo

**Resultado de S21 (11 oct., dos PCs).** Pequeña mejora, pero los jinetes siguen desplazados en la silla y siguen
parpadeando al montar, en los dos PCs. `game faults 0`.
- `MountSeat … REFUSED (attach type already set (not by this horse)) | attach type 9, hierarchy parent 0x00000000,
  CC+0x228 1 | rider type 1 horse type 5`, en **todas** las monturas (anfitrión 2, invitado 2): vf54 no se llamó nunca.
- `hold [remote early returns 359, filled 358]` en el anfitrión y 78/78 en el invitado, y aun así la sonda RemoteMotion
  ve el origen en cada montura: `[+0..+1542 ms] method 6 RideOn | pos (-496.56, 37.17, -168.64) -> (-10762.97, …)` (un
  4,6 % de la posición real: una mezcla que sale del origen) y `[+1572 ms] method 0 | pos (0.00, 0.00, 0.00)`.
  `divergence_probe` "largest 11477.41 m", dos picos por montura y ninguno al desmontar.

### 14.1 El tipo de enganche 9 [V Ghidra + capstone]

- `0x9E990E0` (`.xtls`, por el thunk `0xA65170` = `E9 6B 3F 43 09`) reinicia la información de enganche y empieza con
  `mov byte [rcx],9`; además deja `+0x1C`/`+0x44` en la identidad (`0x6A658E0`: escala 1, cuaternión 0,0,0,1,
  posición 0,0,0).
- El constructor de CC `0x85CB00` crea el objeto de enganche en `CC+0x100` con esa información: **todo personaje empieza
  en 9**. `0xA65960` y `0xA6A120` (el desenganche) vuelven a escribir 9; `0xA651E0` (`0xA65386 cmp r8b,9`) y
  `0xA69140` no ponen padre de jerarquía con 9.
- Valores conocidos: 2 agarre, 3 seguir, 4 asiento de vehículo (`0xA462B0`), 6/7 gimmick o bolsa, 8 enganche de acción,
  **9 ninguno**. El 0 no lo escribe nadie. `0x87BA80` da "anclado" para 2 y 4–8 (3 con padre); 9 cae en el `default`.
- `CC+0x228` = 1 lo escribe la vf19 de CC (`0x85E2B0` → `0x984D830`) como primera escritura: "componente iniciado". La
  vf42 de liberación solo desengancha con 1. Era [H] en 13.7 y era otra cosa.
- vf54 sobre un 9: `0xA462B0` → `0xA651E0` (que primero llama a `0xA65960(attach, 0.0, 1)`, sin efecto con padre 0)
  copia la información: el tipo pasa a 4, se cuelga del hueso de la silla y `0x15ECA10(hier, caballo, 4)`. Sobrescribe;
  no se anida ni se niega. El controlador (`.xtls 0xEE60840`) y el re-compromiso de NPC (`0xA467E0`) llaman a vf53 +
  vf54 sin mirar el tipo (los dos parten de 9).
- La liberación nativa (vf42 → `0xA65960` → `0xA6A120(caballo)`) deja 9 y padre 0: no hay que restaurar nada.
  **Trampa:** si el caballo no tiene CC, solo limpia el padre y el jinete se queda en 4.

### 14.2 El parpadeo [V código; E la rama exacta]

- **Ventana 1 (inicio de RideOn, 0–1,5 s):** `RideOn::Enter` `0x681C80` arma el "warp" desde `[[CC+0x100]+0x24..0x4B]`
  (`0x682174`/`0x682180` → `0x877F90(mc+0x1A8, inicio, …)`, `.xtls 0x988EA70`). Sin asiento esa información es la
  identidad = **el origen del mundo**. Cada frame la vf6 de RideOn `0x6822C0` avanza el warp (`0x878250`) y lo coloca
  con `0x2D28990([CC+0x130], …)`: no pasa por `0x88E6A0` ni por `0x891180`, así que `ride_hold_pos` no podía verlo.
  Con vf54 primero, `0xA651E0` escribe en `+0x24` la transformación actual del jinete (`0x26D3940`).
- **Ventana 2 (al entrar el método 0, ~1 frame):** `RideOn::Exit` `0x6829C0` (`.xtls 0x9145C70`) pone {escala, giro,
  posición 0,0,0} con `0xFEC2DD0`; con padre (el hueso) es local a la silla, sin padre es el origen.
- **La premisa de S21 era falsa:** la salida de `0x88E6A0` no está a cero; `UpdateByMode` la rellena antes con la pose
  actual (`0x88FD56..0x88FD66`). Por eso `filled 358` no cambió nada. `0x11B2420` con 0 muestras devuelve la actual.
- Saltarse el reinicio del búfer (`0x681DBC`) o vigilar la aplicación (`0x891180`/`0x891EC0`) no arregla ninguna de las
  dos ventanas: las dos escrituras al origen van por otro camino.
- **Una sola causa para los dos defectos:** el jinete remoto no se sentaba.

### 14.3 Cambios (sesión 22)

- `msl::kAttachNone = 9`. `seat_judge` solo da `Apply` con tipo 9, padre de jerarquía legible y 0, `CC+0x228 == 1`,
  byte `rider+0x94 == 0` (si no, vf54 pediría una acción local, `0xA45AC0` por la vf178) y caballo con CC; razones
  nuevas `ParentSet`, `NotInit`, `LocalCtl`, `HorseNoCC`. `seat_verify` registra el tipo de la jerarquía
  (`hier+0x18`) pero no lo juzga (offset sin lectura en vivo todavía). La clave local desconocida (0) también se
  rechaza.
- Línea `MountSeat` con tipo de jerarquía antes → después, `rider+0x94` y si el caballo tiene CC. Toda llamada a vf54
  (la del jugador local y las de NPC incluidas) registra `attach type antes -> después` (se espera `9 -> 4`).
- `RideOn::Enter … | attach type N` y, si es el jinete remoto sin sentar, `NOT SEATED: its warp starts at the world
  origin (the mount blink)`; contador `RideOn::Enter of the remote rider NOT seated` en el resumen (debe ser 0).
- `ride_hold_pos=probe` en las inis (no hace falta; la vigilancia se queda contando).
- **Comprobado en un PC (S22):** el re-compromiso nativo de los NPC jinetes registra `MountSync vf54 … attach type 9 -> 4`
  (`cdmp_20261011-205223.log`): el 9 es el punto de partida real de un asiento nativo.

### 14.4 Qué mirar en la sesión 22

- `MountSeat [1] … vf54 0xA45760(…) -> *res 0x00000000 | attach type 9 -> 4, hierarchy parent 0x00000000 -> 0xB…`
  `… | hierarchy type 9 -> 4, rider+0x94 0, horse CC yes | SEATED`, en cada montura y en los dos PCs.
- `RideOn::Enter … (remote player) … attach type 4`, nunca `NOT SEATED`; resumen `NOT seated 0`.
- Al desmontar: `MountSeat release … attach type 4 -> 9 … after its RideOff::Enter here` (o la retirada del
  observador); `stuck 0`.
- Geometría: el cubo `seated here` con ≥95 % ≤0,25 m a todas las velocidades; `frozen 0`.
- Sonda RemoteMotion: ninguna muestra cerca del origen; `divergence_probe … largest` de pocos metros.
- **Aborto:** `FAILED`, `FAULT`, `stuck` > 0, `frozen` > 0, el jinete pegado al caballo tras bajar, la cámara o el
  control local reaccionando, o cualquier `WRN` del self-check → `mount_seat=0` en ese PC.

## 15. Sesión 22 (dos PCs): otra barrera, `rider+0x94`, y lo que de verdad decide (sesión 23)

**Resultado S22.** "Sigue exactamente igual". `MountSeat … REFUSED (rider+0x94 != 0 (locally controlled)) | attach type
9, … | hierarchy type 9 -> 9, rider+0x94 1, horse CC yes` en las 4 monturas (2 por PC) y `RideOn::Enter … attach type 9
| NOT SEATED: its warp starts at the world origin (the mount blink)`: la causa de §14 se confirma, pero la guarda
`rider+0x94 == 0` (añadida sin medir el valor en un jugador remoto, como avisó la revisión) volvió a impedir el asiento.

**Lo que decide [V Ghidra + capstone]:**
- vf54 acaba con `if (rider->vf178()) 0xA45AC0(rider vc, clave del caballo, 1)`: una petición de acción lanzada sobre los
  componentes del jugador **local**. vf42 (desmontar, `.xtls 0x9DCF13B`) hace lo mismo con 0.
- Toda clase de actor de juego del cliente (ClientActor `0x55860F0`, ClientNormalInGameActor `0x55857B0`,
  ClientChildOnlyInGameActor `0x5585C40`) tiene vf178 = `0x4FE260`: `[+0xA0] == [[WorldSystem+0x30]+0x58] && [+0x94]`.
  `0x8A67D0` (solo `+0x94`) es de la cuenta y del lado servidor, nunca del personaje en el cliente.
- El personaje del otro jugador lee `+0x94 = 1`, pero su `+0xA0` es el usuario 2: vf178 = falso y la petición no sale.
- **Medido en vivo (S23, un PC):** `local-user self-test (0xA0100001): vf178 exe+0x4FE260, +0xA0 IS the local user,
  +0x94 1 -> local user (ok)`. La misma lectura sobre el remoto da "no local".
- Lo demás de vf54 para un jinete remoto ya corre de forma nativa con los NPC jinetes en el observador (`9 -> 4`): el
  `LaunchGlobalAction` del caballo exige C8 del caballo != 0; `0x21A8F50` ("caballo del jugador local") exige C8 = 2 del
  jinete; `UpdateRidingInfo` va al modelo de UI propio del jinete. Nada lee cámara ni entrada.

**Cambios (sesión 23):**
- `msl::seat_local_user` reproduce `0x4FE260` (con `0x8A67D0`: solo `+0x94`; ranura desconocida o usuario local sin
  leer: cuenta como local). `LocalCtl` solo si dice "local"; nueva `Controlled` (C8 del jinete 2 o C8 del caballo != 0).
- Veto en la entrada de `0xA45AC0` (14 bytes verificados, 2 llamadores, frío): nunca para el personaje del otro jugador;
  contador `its riding action requests vetoed` (esperado 0).
- Línea `MountSeat` con la ranura vf178, si `+0xA0` es el usuario local y C8 de jinete y caballo; autoprueba
  `local-user self-test` en el resumen (debe decir `local user (ok)`).

**Qué mirar en la sesión 23:** lo de §14.4 (`SEATED`, `attach type 9 -> 4`, `NOT seated 0`, liberación `4 -> 9`,
geometría `seated here`), más `vetoed 0`, `local-user self-test … (ok)` en los dos PCs y que ni el jugador local ni sus
acompañantes reaccionen cuando el otro monta o desmonta.
