# P8 S2: el 2322 nativo para el usuario 2 como observador

9 de octubre de 2026, rama `opcion-d`, HEAD `6dd8032` más estos cambios **sin commit**. Build del juego
1.0.0.2976; direcciones en **RVA** (base `0x140000000`). Encargo:
[encargo-2322-observador.md](encargo-2322-observador.md); dictamen:
[dictamen-2322-observador.md](dictamen-2322-observador.md).

**[V]** = comprobado en código, instrucciones (capstone sobre el PE de solo lectura
`D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, SHA-256 `57DA440D…E1E7`, Python `-I`), capturas, logs
conservados, compilación o pruebas. **[E]** = inferencia o validación pendiente.

**[V]** No se lanzó el juego, no se copió nada a `bin64` (solo se **leyó** la captura HC ya existente,
como en el dictamen), no se hizo commit, no se usó Ghidra (bastó capstone y las decompilaciones ya
guardadas en `dictamen-2322-work\decomp`), y no se engancha ni parchea `.xtls` ni sus thunks: la emisión
**no añade ningún gancho**; usa el de entrada `0x2BC84E2` que ya instalaba `observer_stage`.

**S2 no está resuelto en vivo**: falta la sesión de Shadow (§9). Esta entrega es la corrección
candidata, compilada y probada en lógica, con su medida.

## 1. Qué se implementó

| Pieza | Lado | Qué hace |
|---|---|---|
| `observer_milestone_logic.h` | anfitrión | Libro de **viajes** del usuario 2 (uno por 2174) y la **decisión**: pura, sin juego, probada |
| `observer_milestone.{h,cpp}` | anfitrión | Verifica el código del juego, lee el marco nativo y el TLS en el gancho de entrada, decide y, en modo envío, llama a `0x2A757A0` + `0x2999D80` **una vez por viaje** en el hilo lógico; líneas `ObserverMilestone` |
| `server.cpp` | anfitrión | `take_frame` informa de 2174/2144/3066/2322/2762/2042 **después** de `g_outgoing.push` con su resultado real; `inject` de 2549/2445; época, servidor sustituido y logout cancelan |
| `observer_stage.cpp` | anfitrión | Verifica el código del hito **antes** de poner sus ganchos; su gancho `0x2BC84E2` llama a `observer_milestone_on_entry` (sale enseguida si está apagado); drena sus líneas |
| `load_trip_logic.h`, `load_trip.{h,cpp}` | invitado | Línea por paquete de carga (2174, 2144, 2322, 2445, 2762) y **resumen por viaje con veredicto**, correlado con los estados de carga; en el hilo de diagnóstico |
| `analyze_capture.py trips` | herramienta | Viajes de cualquier captura CDPK (Shadow, cliente del anfitrión o grabación de `cdmp_peer`) |
| `tests/optd_observer_milestone_tests.cpp` | pruebas | Orden, duplicados, invalidación, reconexión, fallo de cola, destinatario incorrecto, interruptor apagado y simulación; viajes del invitado con la sesión 5 |
| `docs/research/ini/cdmp.p8-2322-*.ini` | configuración | Anfitrión A y B, Shadow (A=B) y un PC |

## 2. Verificación de los puntos de integración

### 2.1 Las referencias del encargo, una por una

| Referencia | Comprobación | Estado |
|---|---|---|
| Emisor `0x2999D80` | `(rcx actor, rdx buscador)`. Lee el bloque TLS 0 (`gs:[0x58]→[0]`) `+0x250` = contexto de envío; `[ctx+0x2B8]=0`; `finder->vf1(finder, ctx+0x2B0)`; si la cuenta es 0, **retorna sin emitir**; después `0x2961CA0` (agrupador abierto). Escribe `0x912` en `0x2999E77`; carga `actor+0x60`. Un único llamador directo: `0x287F1D4` (barrido rel32 de `.rsrc/.text/.xtls`) | **[V]** |
| Construcción `0x287F194–0x287F1C7` | vtable base `exe+0x5B253F8`, byte `[rsp+0x58]=0`, vtable derivada `exe+0x5B25438`, `[rsp+0x60]=r15` y luego `r14->vfE8()`; `0x287F1D4` llama al emisor; `0x287F1DA` **solo** vuelve a escribir la vtable base (no hay llamada a destructor) | **[V]** |
| Constructor nativo `0x2A757A0(this, actor)` | Hace exactamente lo mismo: base, `+8=0`, derivada, `+0x10=0` y, si hay actor, `+0x10 = actor->vfE8()`; **46 llamadores** rel32. Es el que usa la implementación | **[V]** |
| Vtable `exe+0x5B25438` | RTTI `.?AVToTargetSendTargetFinder@pa@@`; ranuras `0x2A75740` (destructor: restaura la base y libera solo con `flag&1`), `0x2A757F0` (añade `[this+0x10]` a la lista si no es nulo), `0x2A75850` (igualdad por `+0x10`). Base `.?AVISendTargetFinder@pa@@`, ranura 0 `0x2A756B0` | **[V]** |
| `finder+8 = 0` | Es la etiqueta de tipo que compara `0x2961CA0` (`0x2961CFE–0x2961D02`) antes de la igualdad `vf2` | **[V]** |
| Sesión en `finder+0x10` vía `vfE8()` | La pone el constructor nativo. Antes de llamar al emisor se exige que sea **la sesión 2 viva de la época** | **[V]** código |
| Punto `0x2BC84E2` | Dentro de `MoveFieldCompleteCore 0x2BC7670`. Solo se llega por caída desde `0x2BC84DD` (`call 0x2841D00`); `0x2BC84D9` viene del `jne` de `0x2BC8473` (no dueño) o de la llamada nativa `0x2BC84D4` (dueño). Ningún salto llega a `0x2BC84D9..0x2BC84E2` desde otro sitio | **[V]** |
| Camino de éxito | `0x2BC8276 call 0x2B9B330 → je 0x2BC82FC` y `0x2BC8315 call 0x283E5A0 → je 0x2BC839F`; los fallos escriben el error en `[rbp+0xB8]` y no pasan por `0x2BC84E2` | **[V]** |

### 2.2 Orden nativo 2144 → 3066 → 2322 [V]

- **2144** `MoveFieldCompleteAck`: serializador `0x299A180` (`mov word [r14],0x860` en `0x299A2B3`), llamado en
  `0x2BC7433` dentro de `0x2BC6BB0`, con `ToTarget(rsi->vfE8())`, **antes** de que `0x2BC7475` llame a
  `MoveFieldCompleteCore`. Mismo hilo. (Otro llamador: `.xtls 0xDA4206C`, `jmp`.)
- **3066** `MoveFieldCompletePostAck`: `0x299A610` (`0xBFA` en `0x299A737`), llamado en `0x2BC80F0`, **dentro**
  del Core, antes de `0x2BC84E2` en todo camino que llega allí.
- **2322**: `0x2BC84D4 → 0x287EF60 → 0x287F1D4 → 0x2999D80`, solo para el dueño.
- Capturas (nuevo subcomando `analyze_capture.py … trips`):

| Captura | Viaje | Secuencia |
|---|---|---|
| HC (cliente del anfitrión) | 1 | 2144 #789 → 3066 #790 → 2322 #794 (10 µs) → 2445 #1141 → 2762 #1142 |
| HC | 2 | 2144 #1199 → 3066 #1391 → 2322 #38370 (**1,357 s**: temporizador de `0x287EF60`) → 2445 → 2762 |
| GC (Shadow, sesión 5) | 1 (campo propio) | 2144 #797 → 3066 #798 → 2322 #802 (18 µs) → 2445 #1143 → 2762 #1144 |
| GC | 2 (campo del anfitrión) | 2174 #1190 → 2549 #1451 → 2144 #9114 → 3066 #9132 → **ningún 2322**, ni 2445 ni 2762 |

- **[V]** En GC no llegó **ningún** `ScopeSenderAck 2042` en toda la sesión (`delivered 2042 0`) y el
  anfitrión nunca tuvo un marco ilegible para la sesión 2 (`out: 38898 packets, 0 unreadable`): para la
  sesión 2, cada envío fue un paquete suelto.

### 2.3 ABI, hilo, envío y vida de los objetos

- **[V] Envío directo síncrono.** Con un único destinatario y sin agrupador abierto, `0x2999D80` vacía el
  agrupador «actual» (`0x2962220`, `+0x2A8`), y llama a `session->vf48` (Send). La Send de la sesión 2 es
  `session2_send → take_frame` del mod: el 2322 pasa por `take_frame` **durante la llamada y en el mismo
  hilo**. El 2144 y el 3066 de esa entrada siguen la misma ruta (`0x299A180/0x299A610` comparten el epílogo:
  `vf70`, `[+0x98]->vf10/vf28`, `vf48`). Con más de un destinatario el emisor usaría `ServerNet::SendPacket`;
  `ToTarget` da 0 o 1.
- **[V] Agrupadores.** `0x2961CA0` busca en `ctx+0x298` (cuenta `+0x2A0`, modo `+0x2AC`) uno con la misma
  etiqueta y `vf2`. Si hubiera uno abierto, el 2322 se uniría a un 2042. El gancho lee `+0x2AC`, `+0x2A0` y
  `+0x2A8` y **rechaza** si hay modo agrupado con agrupadores abiertos (un 2042 además no lo vería el libro).
- **[V] El gancho.** Mid hook de safetyhook (`observer_stage`, bytes verificados). Su stub guarda todos los
  registros generales, `RFLAGS` y `XMM0–15`, alinea la pila a 16 y reserva 0x30 antes del callback (stub
  desensamblado). `0x2BC84E2` está **justo después de una llamada** (`0x2841D00`): por la ABI x64 solo
  pueden estar vivos los no volátiles, que el callee preserva; las mitades altas YMM son volátiles. Llamar a
  código del juego desde ahí es ABI-correcto.
- **[V] Hilo.** Es el hilo que ejecuta `MoveFieldCompleteCore`, el mismo en el que el dueño llama a
  `0x287EF60` y, por tanto, a `0x2999D80`. El byte de reino `TLS+0x1EC` se **exige** pero no basta:
  además el 2144 de este viaje y el 3066 que lo siguió deben haber salido por `take_frame` **en este mismo
  hilo** (prueba de que es este manejador y de que el contexto de envío es directo). Nunca desde el hilo de
  Steam, el trabajador de diagnóstico ni `take_frame` (un contador por hilo lo rechaza).
- **[V] El buscador.** 0x20 B en la pila del mod, a cero; los usa hasta `+0x10`. Tras la llamada se escribe
  la vtable base, como `0x287F1DA`. Nada que liberar.
- **[V] Punteros.** `R13` (campo) y `R14` (viajero) son los del marco nativo en curso: viven durante toda la
  llamada. **No se guarda ningún puntero para después**: no hay trabajo diferido.
- **[E]** Si el código del juego fallara dentro de la llamada, el `__except` del mod lo absorbe, registra
  `FAULT` y apaga la emisión para el resto de la partida. Es la misma política que las otras llamadas
  protegidas del proyecto; un fallo así dejaría estado del juego a medias.

## 3. Diseño

### 3.1 La transacción

Identidad: **`E<época>.T<viaje>`**, más la sesión 2 viva, el usuario y su clave, el actor (clave del
`MainPlayerDataStartAck` de esa época + puntero + `[user+0xD0]`), el campo (puntero, `+0x2C8` y dueño) y el
hilo. Un **viaje** empieza con cada 2174 que entra en la cola de la sesión 2 viva de la época; la época de
conexión y la clave del actor no bastarían para distinguir viajes sucesivos.

| Evento (origen) | Efecto |
|---|---|
| 2174 a la sesión 2 viva (`take_frame`, tras el push) | Cierra el viaje anterior (línea `trip end`) y abre otro con el **dueño que escribió el servidor** (antes de la reescritura de la copia). Si el push falló: viaje envenenado |
| 2144 (push real) | Último 2144 del viaje: hilo, secuencia, hora. Push fallido: envenenado |
| 3066 en el **mismo hilo** que ese 2144 (push real) | Prueba de que este Core envió directo. Push fallido: envenenado |
| 2322 nativo a la sesión 2 | El viaje ya no recibirá el nuestro; si llega **después** del nuestro: «DUPLICATE MILESTONE» |
| 2549 / 2445 (inject) y 2762 (`take_frame`) | Salida de la carga vista desde el anfitrión |
| 2042 a la sesión 2 | Contado y avisado: su contenido no se ve |
| Nueva época / servidor sustituido | Se cancela lo pendiente; nada pasa a la nueva |
| Logout pedido | Viaje sin decidir: cancelado (incluido el 2549 → 2144 que la secuencia de salida inyecta) |

### 3.2 La decisión (una por viaje, definitiva)

En la **primera entrada de observador** del usuario 2 en el viaje (`0x2BC84E2`, usuario ≠ dueño). Las del
anfitrión solo se cuentan; si el usuario 2 es el dueño (su campo inicial), la ruta nativa envía su 2322
y el libro la ve. Motivos de rechazo, en orden (`oms::Why`):

| Motivo | Qué protege |
|---|---|
| no 2174 de esta época | reconexión, servidor sustituido |
| ya decidido / logout | duplicados, cancelación |
| reentrada desde `take_frame` | recursión |
| viaje envenenado | 2174/2144/3066 perdidos en la cola |
| 2322 nativo ya en cola | coexistencia con la ruta nativa |
| sin 2144 / 2144 de otro hilo / sin 3066 tras él en este hilo | orden y vínculo con el resultado real de la cola |
| sin reino (`TLS+0x1EC`) / sin contexto (`TLS+0x250`) / agrupador abierto | hilo y ámbito de envío |
| clave del personaje desconocida o distinta / no es el personaje principal | invalidación del actor, reutilización de direcciones |
| la sesión del usuario (`+0x1F8`) no es la sesión 2 viva | destinatario, reconexión |
| `TS+0x500` (campo del field mover) ≠ campo / dueño ≠ el del 2174 o ≠ anfitrión / no es el campo actual del anfitrión | destrucción o cambio de campo |
| un fallo anterior | apagado tras `FAULT` |

Se decide **una vez**: cualquier rechazo deja el viaje sin 2322 del mod y con su motivo. Sin reintentos
ni temporizadores. Los seguimientos a +10/+60/+300 s solo informan.

### 3.3 La emisión (solo `observer_milestone_send=1`)

1. Fuera del candado: `0x2A757A0(&finder, viajero)`.
2. Se exige vtable `ToTarget`, `+8 == 0` y `+0x10 ==` la sesión 2 viva; si no: «NOT CALLED».
3. `0x2999D80(viajero, &finder)`; durante la llamada `take_frame` ve **nuestro** 2322 (mismo hilo, marcado) y
   el libro guarda el resultado real del push y la sesión.
4. Vtable base en el buscador. Línea `called`: **solicitado** (decisión), **llamado** (retorno),
   **encolado** (push visto) o «returned but NO 2322 reached take_frame» (sin destinatario) o «LOST».
   La **aplicación** es del invitado (§7.2).

## 4. El significado del protocolo

- **[V] No se omite preparación propia del invitado.** Lo que la rama de no dueño salta es del **dueño**:
  `0x2910DE0` recibe el gestor de escenas de `0x2B00C70(field)`, buscado por la clave del dueño
  `[field+0x180]`; `0x287EF60` recibe `rbx = [[[[att+8]+0xD0]+0x68]+0xA8]+0x18` del usuario dueño
  (`0x2BC8483 call 0x2A82590(…, ownerKey)`). Ninguna de las dos toca el gestor del personaje del usuario
  2. El 2264 que prepararía M en el cliente sale por usuario al actor de **ese** gestor
  (`0x2886FC3–0x2887035`): tampoco sería del invitado.
- **[V]** El 2322 nativo para el dueño sale en la misma función justo después del 2144 y del 3066 cuando el
  gestor ya está listo (HC viaje 1: 10 µs), o más tarde por el temporizador (HC viaje 2: 1,357 s, tras
  sus 2661). **[E]** En la entrada de observador no se envía ningún 2661 al invitado, así que no hay nada
  que esperar en el gestor del dueño; emitir en la entrada reproduce el caso «listo».
- **[E] Lo que garantiza el código y lo que debe comprobar el invitado.** El anfitrión garantiza: viaje,
  entrada completada, observador, actor/sesión/campo vigentes, 2144 y 3066 en cola, hilo y contexto,
  un solo 2322 a la sesión 2. **No puede** saber el estado de M en Shadow: `+0x231`, `+0x174`, `+0x2D4/0x2D0`,
  las listas y, sobre todo, el resultado de `0xA97D60`. Eso lo miden en el invitado `P8Probe [loading
  0x779E48]` y `[loading 0x779E77]`. Listas vacías no equivalen a un gestor vacío ni `+0x174=0` a ausencia
  de instancias (dictamen §3.2).
- **[V] Se conserva:** la reescritura de 2174, el dueño real del campo en el servidor, S3 (`observer_hold`),
  ambos predicados de `0x11` (`0xA97D60` intacto) y los estados posteriores.
- **[V] No se hace:** llamar a `0x287EF60` o `0x2910DE0`, crear temporizadores o efectos del dueño, escribir
  `M+0x231`, saltar `0x11`, vaciar listas, forzar resultados, cambiar el dueño, usar `ToNear`, esperas
  fijas, reintentos, construir el paquete a mano, replicar 2661/2981/2264, tocar S1.

## 5. Garantías

| Garantía | Cómo | Estado |
|---|---|---|
| Hilo lógico, TLS y ámbito válidos | gancho en el Core; reino; contexto `+0x250`; sin agrupador abierto; 2144+3066 del viaje por `take_frame` en este hilo | **[V]** código y estático; **[E]** en vivo |
| Después del 2144 y en orden | 2144 (`0x2BC7433`) y 3066 (`0x2BC80F0`) en cola **antes** del gancho, con push real; el 2322 entra en la misma cola FIFO, del mismo hilo, después | **[V]** |
| Destinatario exclusivo | `ToTarget` nativo del viajero; sesión = sesión 2 viva, comprobada antes y vista en `take_frame` | **[V]** código |
| Una vez por viaje | decisión única, definitiva; 2322 nativo previo lo impide; posterior se avisa | **[V]** pruebas |
| Ciclo de vida | nueva época, servidor sustituido, logout, nuevo 2174: cancelan; nada diferido; nada guardado | **[V]** pruebas |
| Sin efectos de dueño | solo constructor + emisor (que no prepara escenas ni temporizadores, dictamen §4) | **[V]** estático |

## 6. Configuración, activación y reversión

**Decisión sobre la clave [V].** `observer_stage_sync` es de medida de escenas, su `=2` ya significaba «envío
rechazado» y su envío teórico era otro (la instantánea 2661/2981). Reutilizarla para el 2322 lo habría
vuelto ambiguo. Se añaden dos claves **booleanas** nuevas en `[session2]` y `observer_stage_sync=2` sigue
rechazado, ahora con el texto «This key NEVER sends anything, not even the 2322».

| Clave | Por defecto | Efecto |
|---|---|---|
| `observer_milestone=1` | 0 | Sigue viajes y **decide y registra** (`WOULD EMIT` / motivo). No llama a nada del juego |
| `observer_milestone_send=1` | 0 | Además **emite**. Implica la anterior |

Ambas necesitan `create_session=1`; si `observer_stage_sync=0`, instalan los ganchos de `observer_stage`
en modo medida. Si cualquier byte verificado difiere: `ObserverMilestone: REFUSED…` y queda apagado.

| Archivo | Uso |
|---|---|
| `docs/research/ini/cdmp.p8-2322-host-A.ini` | Anfitrión, tramo A: `observer_milestone=1`, `observer_milestone_send=0` |
| `docs/research/ini/cdmp.p8-2322-host-B.ini` | Anfitrión, tramo B: **única diferencia** `observer_milestone_send=1` |
| `docs/research/ini/cdmp.p8-2322-shadow.ini` | Shadow, **el mismo en A y B** (la emisión es del anfitrión) |
| `docs/research/ini/cdmp.p8-2322-local.ini` | Un PC con `cdmp_peer` (§10) |

Constantes en A y B: `observer_hold=1` (S3), `child_hold=1`, `reconnect=1`, sondas, `recv_buffer_kb=16384`,
`recv_buffer_messages=16384` (hilo de red), capturas en los dos PC.

- **Desactivar:** `observer_milestone_send=0` (vuelve a A) o ambas a 0 (nada nuevo se instala).
- **Revertir:** no hay estado persistente; reconstruir desde `6dd8032` deja el binario anterior.

**Binario [V]:** `build\bin\RelWithDebInfo\cdmp.asi`, 5 003 264 B, SHA-256
`68A475859E008BFD4CB7D7DC9F1FF095E82E2BF01CE65D75136A80ADD664C476`; el log dice build `6dd80326 + local
changes`. El mismo archivo en el anfitrión y en Shadow, en los dos tramos.

## 7. Diagnóstico: la cadena completa

### 7.1 Anfitrión

| Paso | Línea |
|---|---|
| instalación | `ObserverMilestone ON (SEND\|DRY RUN …): … 13 code regions + emitter entry + vtables checked` |
| 2174 → viaje nuevo | `ObserverMilestone [2174] … trip E1.T2 begins: MoveFieldAck 2174 to session 2 queued \| field owner written by the server 0x90100000` |
| 2144 encolado | `[2144] … trip E1.T2: MoveFieldCompleteAck 2144 #1 to session 2 queued (seq N)` |
| 3066 en el mismo hilo | `[3066] … after the trip's 2144 (seq N) on the same thread …` |
| decisión | `[decision] T<hilo> \| trip E1.T2 \| observer entry of user 0x90100333 actor 0xA0100334 into field … owned by 0x90100000 \| EMIT NOW … / WOULD EMIT … / NO 2322 FROM US: <motivo> \| epoch \| session: live …, the traveller's … \| thread: server realm, send context, scope mode/open/current \| mode` |
| 2322 encolado | `[our 2322] … OUR 2322 reached take_frame on this thread (actor 0xA0100334, the live session 2) -> queued \| corr=2322:A0100334:0x0:0x0` |
| resultado de la llamada | `[called] … 0x2999D80 returned; its 2322 IS in user 2's queue (x1) (ToTarget session 0x…) \| call N ms` |
| salida normal | `[in] … LoadingCompleteReq 2445 #1 handed to the server (X s after our 2322)` y `[2762] … the load's normal exit seen from the host` |
| cierre | `[trip end] … trip E1.T2 ends: EMITTED: 2322 in user 2's queue \| 2174 -> 2144 … \| 2144 -> our 2322 … \| our 2322 -> 2445 … \| 2445 -> 2762 …` |
| otros | `[native 2322]`, `[owner entry]`, `[logout]`, `[epoch]`, `[2042]`, `[follow-up] +10/+60/+300 s` |
| contadores (60 s) | `ObserverMilestone (SEND) \| entries … \| trips … \| decisions …: emit … / would emit … \| calls …: enqueued, LOST, not seen, failed \| native 2322 (after ours) \| duplicates avoided \| cancelled \| 2042 bundles \| refusals: …` |

Además: `ObserverStage [2322 PlayAllPreLoadSequencerAck] … via ToTarget (session …) | corr=2322:A0100334…`
(su gancho en `0x2999D80` ve nuestra llamada), `ObserverStage [entry 0x2BC84E2] … NOT the owner … | THE
2322 ALONE … see 'ObserverMilestone [decision]'` y `P8Probe [owner 0x2BC8470] … NOT the owner`.

### 7.2 Invitado (Shadow, `load_diag=1` y `guest_actor_diag=1`)

| Paso | Línea |
|---|---|
| recepción | `GuestLoad [in 2322] … \| trip N \| PlayAllPreLoadSequencerAck #1 actor 0xA0100334 \| +X ms after its 2144 \| corr=2322:A0100334:0x0:0x0` |
| manejador nativo | `P8Probe [2322 entry 0xB87890]`, `GuestStage [2322 field 0xB87A32]` (mismo `corr=`) |
| escritura sobre M | `P8Probe [2322 write 0xB87A67]`: M, byte anterior y si es el M de `0x779E48` |
| ambos predicados | `P8Probe [loading 0x779E48] … M+0x231 2 (needs >= 2) …` y `[loading 0x779E77]`: resultado de `0xA97D60` y primera dependencia falsa |
| transiciones | `GuestDiag loading transition: [P+0x15C] 0x11 -> 0x12 … EXIT FROM 0x11` y siguientes |
| salida | `GuestLoad [out 2445]`, `GuestLoad [in 2762]` |
| veredicto | `GuestLoad trip N summary … \| VERDICT: one 2322, after its 2144 \| 0x11 -> 0x12 (+… after the 2322) \| 2445 and 2762: normal load completion` (o `NO 2322`, `DUPLICATE`, `ORDER`, `0x11 -> 0x18: NOT the normal exit`, `still in 0x11`) |

Correlación entre PCs: el `corr=2322:<actor>` y el 2144 de cada viaje (como en §8 de
[p8-observador-escenas.md](p8-observador-escenas.md)); los tiempos de la captura con
`analyze_capture.py <captura> trips`. Las líneas del invitado salen del hilo de diagnóstico (5 s), nunca del
de red; los estados llegan hasta 5 s tarde, así que el resumen espera 6 s.

## 8. Pruebas y compilación [V]

- `.\scripts\build.ps1` (RelWithDebInfo): compila; **20/20 pruebas**. Único aviso: el preexistente de
  `guest.cpp` (`net` sin usar).
- `optd_observer_milestone`:
  - **interruptor**: apagado no decide nada; simulación decide sin marcar llamada;
  - **orden**: entrada antes del 2144, 2144 de otro hilo, 3066 ausente o de otro hilo, segundo 2144 sin su 3066;
  - **duplicados**: entrada repetida, 2322 nativo antes y después del nuestro, entradas de dueño y del anfitrión;
  - **invalidación**: 15 casos (clave reutilizada, personaje, field mover, dueño, campo del anfitrión, reino,
    contexto, agrupador, reentrada, logout, sesión…), nuevo 2174 antes de la entrada, logout con la 2549
    de la secuencia de salida, 2174 sin dueño legible;
  - **reconexión**: nueva época, sesión antigua, viaje nuevo de la época nueva;
  - **fallo de cola**: 2174/2144/3066/nuestro 2322 perdidos, «no visto», seguimientos solo informativos;
  - **destinatario**: sesión del buscador distinta, nuestro 2322 hacia otra sesión, 2322 de otro hilo, fallo;
  - **invitado**: los dos viajes de la sesión 5 (normal y `NO 2322 … 0x11 -> 0x18`), orden y duplicado,
    sin estados.
- **[E] Lo que las pruebas no cubren:** el gancho, la ABI real de `0x2A757A0/0x2999D80`, el hilo en vivo,
  que `TS+0x500` ya sea el campo nuevo en `0x2BC84E2` y el comportamiento del cliente. Comprobado aparte
  en el binario: `dumpbin /disasm` del `cdmp.asi` muestra `rcx=&finder, rdx=viajero` para el constructor
  y `rcx=viajero, rdx=&finder` para el emisor, y las comprobaciones de vtable, `+8` y `+0x10` antes de la llamada.

## 9. Sesión con Shadow en dos tramos

Mismo `cdmp.asi` (§6) en los dos PC y tramos. `crashpad_handler.exe` fuera (K2). Guardar todo con la etiqueta
del tramo: anfitrión `bin64\cdmp.log` y `bin64\cdmp_packets_*.bin` (su cliente propio); Shadow `cdmp.log` y
`cdmp_packets_*.bin` a la carpeta compartida `cdmp-shadow\logs\`.

**Tramo A (medida):** anfitrión `cdmp.p8-2322-host-A.ini`, Shadow `cdmp.p8-2322-shadow.ini`.
1. Anfitrión en su mundo, quieto. Shadow entra: viaje 1 (campo propio, `[owner entry]` + `[native 2322]`),
   viaje 2 (campo del anfitrión).
2. Observar unos 2 minutos de carga (límite de observación: no se envía ni fuerza nada). Después cerrar
   Shadow; un cierre durante la carga puede repetir la excepción conocida de salida en `0x2045A6A`
   ([p8-hilo-red.md](p8-hilo-red.md) §4), ajena a esta entrega: anotarla, no atribuirla al 2322.
3. Esperado: `[decision] … WOULD EMIT` en el viaje 2 (o el motivo exacto) y en Shadow `VERDICT: NO 2322 … the
   load waits in 0x11`, como en la sesión 5.

**Tramo B (experimento S2):** anfitrión `cdmp.p8-2322-host-B.ini`; Shadow **sin cambios**. Mismos pasos,
añadiendo si se puede: salir y volver a entrar, una reconexión (cerrar Shadow y entrar otra vez) y un
cambio de campo.

**Criterios de aceptación** (el dictamen §8, con sus líneas):

| # | Confirma | Refuta |
|---|---|---|
| 1 | anfitrión: un `[decision] … EMIT NOW` y un `[called] … IS in user 2's queue (x1)` por viaje de observador, después de su `[2144]` y `[3066]`; invitado: `GuestLoad [in 2322] … #1 … +X ms after its 2144`; `trips` de la captura: `one 2322, … after its 2144, after its 3066` | dos 2322 en un viaje, 2322 antes del 2144, `[native 2322] … DUPLICATE`, `not seen`/`LOST` |
| 2 | `P8Probe [2322 write 0xB87A67]` `1→2` sobre **el mismo M** que `[loading 0x779E48]` | otro M, o ninguna escritura |
| 3 | `[loading 0x779E77]` verdadero con las condiciones reales | falso: registrar la dependencia; **no** forzarla |
| 4 | `EXIT FROM 0x11` hacia **0x12**, estados siguientes completos, `[out 2445]`/`[in 2762]` y en el anfitrión `[2762] … normal exit` | `0x11 -> 0x18`, quedarse en `0x11` u otro estado |
| 5 | anfitrión sin regresión (`Net:` sano, ningún `ERR` nuevo), S3 igual que en A (`GuestS3`, `observer hold`) | cierres, `Net: one loop took`, S3 peor. S1 se cuenta aparte |
| 6 | salir/volver, reconectar, cambiar de campo: un hito por viaje nuevo, `[epoch]`/`[logout]` con `CANCELLED` cuando toque, ningún `[our 2322]` de un viaje viejo | emisión fuera de su viaje, 2322 tras logout o en otra época |

Si se desplaza el bloqueo (otro predicado o estado), se anota el nuevo y se conservan las comprobaciones
nativas. Una sola entrada buena acredita el mecanismo en ese caso, no su robustez completa.

## 10. Prueba en un PC con `cdmp_peer` (solo el lado del anfitrión)

Sin cliente real del usuario 2 no hay M ni `0x11`: verifica **emisión única, destinatario (sesión 2), orden
tras 2144/3066 y cancelación**. `cdmp.p8-2322-local.ini` como `bin64\cdmp.ini`, ranura 108, quieto y sin viajar
(como C4r). Un arranque del juego y espera a `ObserverMilestone ON (SEND)` y `Session2 C4: reconnect on`.

1. **Una entrada normal con despedida:**
   ```
   .\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00bye_112627.troc --var SAVE_SLOT=108 --record D:\cdmp-grabaciones\ms-1.bin
   python -I -B tools\scripts\analyze_capture.py D:\cdmp-grabaciones\ms-1.bin trips
   ```
   Esperado: en el viaje compartido, `[decision] … EMIT NOW`, `[our 2322] … the live session 2 -> queued`,
   `[called] … IS in user 2's queue (x1)`; en `trips`, `one 2322, … after its 2144, after its 3066`. Tras la
   despedida, `[logout]` y `[trip end]`.
2. **Reconexión:** tras `reconnect epoch=2 ready`, el mismo comando (`ms-2.bin`): `[epoch] guest epoch 2
   begins …` y un solo 2322 en el viaje compartido de la época 2 (`E2.T…`).
3. **Cancelación en carga:** `login_e3lc00cut8_112627.troc` (`cut8.bin`): corta tras el 2174 compartido, sin
   2549. A los 15 s (`link_lost`) debe salir `[logout] … CANCELLED (no 2322 will be sent for it)`. Si la
   secuencia de salida cierra el traslado (2549 inyectado → 2144 → entrada), su decisión debe ser `NO 2322
   FROM US: user 2's logout was asked for`. En `cut8.bin`, ningún 2322 en el viaje compartido.
4. **Control apagado:** `observer_milestone_send=0`: `WOULD EMIT` y ningún 2322 en la grabación.

Es además la **primera validación en vivo** de las condiciones del anfitrión que la lógica no puede probar
(`TS+0x500` ya en el campo nuevo, el hilo, `TLS+0x250`, agrupadores, la sesión del `ToTarget`). Si la
decisión del viaje compartido da cualquier otro motivo, o aparece `REFUSED`, `NOT CALLED` o `FAULT`, hay
que parar y mandar el log antes de ir a Shadow.

## 11. Riesgos y pendientes

- **[E]** Que `TS+0x500` sea ya el campo nuevo en `0x2BC84E2`; si no, el tramo A dará `field mover points to
  another field` y no se emitirá en B (la comprobación la exige el dictamen).
- **[E]** Que el cliente aplique el 2322 y salga a `0x12`: depende de `0xA97D60` en Shadow (§4).
- **[E]** Que no haya agrupadores abiertos en ese punto (la sesión 5 no tuvo ningún 2042).
- **[E]** Un fallo dentro del juego absorbido por SEH (§2.3).
- **[V]** S1 no cambia: los 15 NPC sin `BodyAnimation` deberían seguir fallando y se cuentan aparte.

## 12. Archivos tocados

| Archivo | Cambio |
|---|---|
| `src/mod/session2/observer_milestone_logic.h`, `observer_milestone.{h,cpp}` | nuevos |
| `src/mod/session2/load_trip_logic.h`, `load_trip.{h,cpp}` | nuevos |
| `src/mod/session2/server.{h,cpp}` | `take_frame` (profundidad por hilo, dueño original del 2174, aviso tras el push), `inject`, época, servidor sustituido, logout; accesores `server_live_session2`, `server_user2_user_key`, `server_user2_player_key`, `server_in_take_frame` |
| `src/mod/session2/observer_stage.{h,cpp}` | verificación antes de los ganchos, llamada desde el gancho de entrada, drenaje, textos de `=2` y de la línea de entrada |
| `src/mod/session2/session2.cpp`, `src/mod/config.{h,cpp}` | claves y cableado |
| `src/mod/session2/guest.cpp`, `guest_diag_worker.cpp`, `guest_actor_diag.cpp` | entradas de `load_trip` |
| `CMakeLists.txt`, `tests/optd_observer_milestone_tests.cpp` | fuentes y prueba |
| `tools/scripts/analyze_capture.py` | subcomando `trips` |
| `docs/research/ini/cdmp.p8-2322-*.ini`, este documento | nuevos |

## 13. Resumen operativo

- **Implementado:** el 2322 nativo para el usuario 2 observador, tras una clave explícita y apagada por
  defecto (`observer_milestone_send=1`; `observer_milestone=1` solo decide). Se emite una vez por viaje con
  el constructor `ToTarget` y el emisor del juego, en el hilo lógico y dentro del mismo
  `MoveFieldCompleteCore`. La decisión está ligada al push real del 2144 y del 3066 de ese viaje.
  Diagnóstico de la cadena completa en los dos PC.
- **Verificado:** puntos y bytes contra el PE; orden 2144→3066→2322 en código y capturas; ABI del gancho y
  de las llamadas en el binario; 20/20 pruebas.
- **Anfitrión:** tramo A con `cdmp.p8-2322-host-A.ini` y tramo B con `cdmp.p8-2322-host-B.ini`; mismo `cdmp.asi`
  (`68A47585…C476`); guardar `cdmp.log` y la captura del cliente propio.
- **Shadow:** `cdmp.p8-2322-shadow.ini` en los dos tramos; guardar `cdmp.log` y la captura.
- **Lo confirma:** en B, por viaje de observador, un `[called] … IS in user 2's queue (x1)` tras su 2144, la
  escritura `1→2` sobre el M esperado, `0xA97D60` verdadero, `EXIT FROM 0x11` a **0x12** y `VERDICT: … 2445 and
  2762: normal load completion`, sin regresiones y con cancelación correcta al salir/reconectar.
- **Lo refuta:** el 2322 llega y se aplica pero la carga sigue en `0x11` o pasa a `0x18`, `0xA97D60` falso,
  otro M, duplicados, o emisiones fuera de su viaje.
