# S35 fase A: evidencia de progreso, contención con revalidación, degradación y publicación segura

Fecha: 14-10-2026. Rama `opcion-d-s35`, sobre `1d99ed0` (auditoría S34). Build del juego **1.0.0.2976**, timestamp
**0x6AB28F00**, base **0x140000000**: todo RVA de este documento vale sólo para esa build. **No se ha ejecutado el
juego, ni el peer, ni Shadow para esta entrega**: sólo `scripts/build.ps1` con sus pruebas (66/66).

Implementa los puntos 1 y 2 del orden de la auditoría ([AUDITORIA.md](s34-investigaciones/AUDITORIA.md) §8) y los
«Cambios obligatorios» de §1 (cierres) y §4 (contención), con las condiciones de §6. Los dictámenes de detalle son
[INFORME-A1](s34-investigaciones/datos/auditoria-codex-20261014/a1-cierres/INFORME-A1.md) y
[INFORME-A4](s34-investigaciones/datos/auditoria-codex-20261014/a4-contencion/INFORME-A4.md). La fase siguiente
(familias, viajeros, rearmado) trabajará en los mismos archivos; aquí no se cambia autoridad de familias ni viajeros.

Todo lo nuevo va detrás de claves `[session2]` `0 | probe | 1`; **`0` es exactamente S34** (el comportamiento previo
sigue disponible, incluido el error del emisor para poder reproducirlo).

## 1. Qué cambia y por qué

### 1.1 Evidencia de progreso (`npc_action_evidence`) — AUDITORÍA §4, I-A4 «Contrato de acción»

**El error.** `npc_action_probe.cpp` leía el cuarto argumento de `0x2A78CA0` como sesión (`param4+0x90 → +0x60`).
No lo es: `0x2A78CF2` llama a `vf+0x170(param4, scratch)` y `0x2A78CF8/0x2A78CFE` comparan la clave devuelta con
`actor+0x90` (`0x2a78ca0.c:39-44`; llamador `0x296caa0.c:211-213`). Resultado: emisor 0 en 843/843 líneas S34,
`progreso=0` siempre, y la contención retiró NPC con acción aplicada (C: 110 diagnósticos, 109 con aplicación previa;
`B0100391`: Req +1,671541 s, aplicación +1,705458 s, retirada +5,056492 s).

**El criterio H1b (`npc_action_evidence=1`).** Una llamada cuenta como progreso de la unidad sólo si
(`npcact::classify`, `npc_activation_logic.h`):

- el Pending TLS de **esa** llamada llegó a H1b `0x2A79060` (después de `ServerStartActionOnServer` con éxito);
  la pila TLS es anidada (`npcact::PendingCall`): H1b sólo marca la llamada más interna y sólo con su componente y
  clave; una reentrada no acredita a la exterior ni hereda la marca;
- el token de nacimiento tomado **antes** de la llamada es el de la unidad (nunca otra encarnación de la clave);
- `actor+0x90` leído antes y después vale el controlador registrado de la unidad y no cambió. Es el **controlador
  esperado** (la comparación nativa ya lo exigió para llegar a H1b), **no un emisor observado**: el emisor real queda
  `emisor=no_medido`. El mod **nunca llama a vf170**;
- la unidad no está dada de baja ni en retirada local.

No cuentan: un retorno 0 sin H1b (`retorno0`), un resultado distinto de 0 (`rechazada`, se cuenta aparte con la
última hora de rechazo), otro controlador, otro nacimiento, lecturas ilegibles (`ilegible`).

**Rama `payload+0x281`** (`0x2a78ca0.c:45-49`, sólo tras la comprobación nativa del controlador): su propio contrato,
acción ficticia del secuenciador con resultado 0 y sin OnServer. Se guarda aparte (`activated_us`) como
**activación**, no movimiento ni misión. Con `npc_action_evidence=1` evita la retirada por «sin activación»; nada más.

**Estados separados** en el registro de la unidad (`npcact::Actor`): publicado = `queued_us` (2005 encolado al
invitado); entregado = **desconocido** en el anfitrión (no se inventa); Req autenticada = `first_req_us` (sólo las
Req de IA que llegan por la sesión del usuario 2: 2223/2734/2556/2405/2291/2828); aplicada = `applied_us` (H1b);
activación = `activated_us` (rama 281); estado restaurable = `restore_antes/después` en la línea; retirada =
`local_retired`/`removed`. Cada evidencia nueva sube `version` (para la revalidación al ejecutar, §1.2).

`probe` calcula las dos (S34 y H1b), registra la H1b y cuenta cuándo deciden distinto; decide la S34.

### 1.2 Contención (`npc_containment_guard`) — AUDITORÍA §4 «Cambios obligatorios», I-A4 puntos 1-8

Con `npc_containment_guard=1` (`npcact::containment_due`, `postpone`, `recheck_root`, `recheck_member`):

- **No se retira por silencio, carga o cola del cliente.** Si el invitado está cargando (fase de movimiento de
  child_hold, acotada a `hold::kLoadDeferMaxMs`) o no ha enviado **ninguna** Req de IA real en `npc_guest_silence_ms`
  (los temporizadores 2043/2045/2046 no cuentan), la decisión se **pospone** y el reloj de la unidad vuelve a empezar:
  cuando el invitado vuelve a hablar, la unidad tiene el plazo entero. Una posposición **no** consume rechazos ni
  presupuesto del nodo, ni abandona la unidad.
- **Una Req suspende, pero no para siempre.** Si hubo Req del invitado para la unidad, la retirada espera hasta
  `npc_no_progress_ms + npc_req_grace_ms` desde el inicio del reloj; después se retira aunque siga pidiendo (una
  operación rechazada no queda protegida indefinidamente). La gracia no reinicia el reloj.
- **Revalidación al EJECUTAR** (`user2_stages.cpp`, el bloque que era la línea ~1913): tras ModLock → barrera de Trocs,
  otra vez bajo `ActivationLock`, se compara la versión de la raíz elegida y la de cada miembro del plan (la versión vive
  en el relleno de `npcact::Member`, que sigue en 24 B) y se vuelve a evaluar `containment_due` con un contexto fresco.
  Una Req, aplicación, activación o vínculo nuevos desde la elección, o un silencio/carga, **cancelan** esa ejecución
  (`[retirada cancelada]`), sueltan la reserva y la unidad se vuelve a mirar; no es un rechazo. El orden de cerrojos no
  cambia y `child_hold_retire_family` sigue fuera de `ActivationLock`.
- Plazos **experimentales** y configurables (`npc_no_progress_ms`, por defecto 5000 = S34; los inis del paso 2 usan
  8000). No son leyes del motor: A tenía 22 aplicaciones nativas > 5 s (5,08–5,63 s).

Con `0` todo es S34: `containment_due` con la política por defecto es exactamente `no_progress` (prueba
`s35_guard_off_is_s34`), y una aplicación entre elegir y ejecutar sigue siendo un rechazo (motivo 4). `probe` decide
como S34 y registra `[contención pospuesta] … probe=1` cuando el guard habría pospuesto.

**Ventana residual (no cerrada):** entre la revalidación y los `Logout` hay una llamada a
`child_hold_retire_family`; una evidencia que llegue por otro hilo del servidor en ese intervalo ya no cancela la
retirada (no se puede retener `ActivationLock` sobre child_hold). Cada miembro sigue revalidando nacimiento y baja
antes de su `Logout` (`may_cleanup_member`), como en S33.

### 1.3 Degradación (`npc_degradation`) — AUDITORÍA §4 último párrafo, I-A4 punto 7

Antes, `containment_ready() == false` (una sonda no verificada, funciones de limpieza ausentes, fallo nativo) devolvía
el vigilante global de 5 s de U-e (`in.accept_tracked = child_hold_active() && !containment_ready()`), que podía
abortar U-e y retirar todo lo no aceptado por una sola unidad. Con `npc_degradation=1` **y** `npc_activation=1`
(`npcact::legacy_watchdog_armed`, `assignments_paused`):

- el aborto «Unaccepted» de 5 s **no se rearma**;
- U-e **deja de asignar NPC nuevos** al usuario 2 mientras la contención no pueda actuar (fail-closed: sin
  contención, nada nuevo bajo su control); los ya asignados siguen con su controlador y no se retira nada en masa;
- una línea `WRN … [contención degradada]` en cada transición, con qué pieza falta; contadores en `[resumen S35]`.

Se distingue fallo de infraestructura (esto) de «NPC sin activación» (la contención por unidad). Los demás abortos de
U-e (2005 con otro controlador, cliente del anfitrión pidiendo, motivo 42, restos tras drenar) no cambian. Con
`npc_activation` 0/probe el vigilante antiguo sigue siendo la única red, como antes.

### 1.4 Publicación segura (`publication_gate`) — AUDITORÍA §1 «Cambios obligatorios», §6 «Problemas adicionales» 3

**Invariante de admisión, no un `Timeout→Keep` aislado.** Ninguna salida de observer_hold / observer_hold_family
anuncia a un observador **que no lo controla** un actor cuyo estado no esté demostrado completo
(`pubg::admit`, `publication_gate_logic.h`). **Una sola regla para el controlador** (revisión 9): quien controla el
actor lo recibe siempre, sea cual sea su estado o el de su padre. Un actor que **ningún usuario** controla (el del
servidor, controlador 0, o un usuario desconocido: `CtrlNotUser`) sigue la regla nativa de `prepare`: su propio estado
no se juzga, su dependencia sí; y como padre cuenta como admisible (revisión 4).

**Tres estados de CC** (`pubg::read_cc`): `Unknown` (no se pudo leer: componentes ilegibles, ranura implausible, `+8`
que no vuelve al actor, sin estado de restore) **nunca** es `Absent` (componentes leídos y ranura CC exactamente 0), y
`Present` lleva el byte restore. Antes `child_hold.cpp:62-69` devolvía `nullopt` para las dos cosas y `:146-149`
llamaba NoCc (nativo) a todas.

| Salida | Antes | Con `publication_gate=1` |
|---|---|---|
| `npc_release_step` Timeout | restore 0 al observador | retenido (`Restore0`) |
| Native «unreadable for too long» (`:590`) | anuncio nativo | admisión: ilegible o restore 0 → retenido |
| Native «another actor behind the key» (`:595`) | anuncio nativo | se lee el actor NUEVO; incompleto → retenido |
| Native «owner changed, child_hold off» (`:606`) | anuncio nativo | admisión |
| Restore1 / ByControl | publicado | se vuelve a leer (TOCTOU) |
| Familia MaxAge / Orphan / OrphanTimeout / AfterParent (`family_release_step:485-514`) | publicado sin mirar restore | admisión del miembro y de su padre |
| WithParent (miembros detrás de un padre liberado) | publicado | padres primero; hijo de un padre rechazado → `ParentInBatch` |
| «no room: released anyway» (`blocked_how=2`) | publicado | admisión |
| Tabla NPC / familia llena en `prepare` | nativo con restore 0 | retenido **sin entrada** (invisible para ese observador hasta que el motor lo anuncie otra vez) |
| NPC con CC `Unknown` en `prepare` | nativo (NoCc) | retenido como un NPC restore 0 |
| NPC con CC `Absent` | nativo | nativo (sin cambio) |

**Dependencias.** La presencia del padre no demuestra que su capa sea utilizable: para cada liberación se lee el
padre (el `+0xAC` que nombra el 2005 o el de la jerarquía) y se exige que no esté retenido para ese observador, que no
se haya rechazado en el mismo lote y que su propio estado sea admisible (`pubg::parent_dep`; un nivel). Un padre que ya
no existe es la ruta nativa de huérfano del cliente y se publica como antes. Es el caso del cierre C: la pieza
`B0100578` consultaba por jerarquía a `B01004BF`, publicado con restore 0. Un Ack en cola no se usa como prueba de nada.

**Retención acotada y recuperación.** Una liberación rechazada vuelve a su tabla con `unsafe_since_ms` y se decide de
nuevo como mucho cada `pubg::kRetryMs` (1 s). La recuperación normal es el propio estado del controlador (restore 1,
el observador pasa a ser el controlador, el padre llega). Pasado `publication_retain_max_ms` desde el primer rechazo, o
si la tabla no tiene sitio, la entrada se **suelta sin publicar** (línea `WRN`, contadores; `observer_hold_acks`
descarta lo guardado para ella). Nunca se publica por expirar. Es la degradación temporal que la auditoría acepta:
animales sin acción restaurable o piezas de familias no resolubles pueden quedar **invisibles para el invitado**; se ve
en los contadores, no en silencio.

Las guardas existentes siguen como defensa en profundidad (`guest_remote_update_guard`, WithheldUser2 hacia el
anfitrión). No se retira ningún NPC de misión por esta puerta: sólo se decide qué se anuncia.

`probe` evalúa todas las salidas, cuenta y registra `WOULD RETAIN` / `WOULD be withheld`, y publica exactamente como
antes. **Lo que no cubre** está en §6.

### 1.5 Diagnóstico GuestS3 — AUDITORÍA §1 tabla y §6 primer cruce

El actor del segundo cierre de Shadow (`exe+0x1831598`, DMP `180710_6364_1`) es **`B01004BF`** (R14 actor, RDX su CC);
`B0100578` es el dueño del `ClientHierarchyActorComponent` (RSI, vtable `0x5578BB8`) que lo consulta por jerarquía.
`guest_s3.cpp` `describe_cc` reconoce ahora esa vtable y escribe el componente, su actor y la clave del padre
`[+0x1C]`, en lugar de llamarlo «CharacterControl de B0100578». El texto de la investigación 4 lleva la corrección.

## 2. Claves `[session2]` nuevas

Valores literales, sin comentarios en la misma línea (los modos aceptan `0`, `probe`, `1`).

| Clave | Por defecto | `probe` | `1` |
|---|---|---|---|
| `npc_action_evidence` | `0` (emisor S34) | H1b registrado, decide S34 | decide H1b (§1.1) |
| `npc_containment_guard` | `0` | registra lo que pospondría | pospone por silencio/carga, gracia por Req, revalida al ejecutar (§1.2) |
| `npc_degradation` | `0` | cuenta los segundos en que se habría evitado el aborto | no rearma el aborto de 5 s; pausa asignaciones (§1.3) |
| `publication_gate` | `0` | evalúa y registra todas las salidas | retiene lo incompleto (§1.4). Necesita `observer_hold=1` y `create_session=1` |
| `npc_no_progress_ms` | `5000` | — | plazo desde el 2005 al invitado (cota 1000..120000) |
| `npc_req_grace_ms` | `8000` | — | gracia tras una Req (0 = sin gracia; cota 500..120000) |
| `npc_guest_silence_ms` | `2000` | — | silencio del invitado que pospone (0 = sin regla; cota 500..60000) |
| `npc_postpone_max_ms` | `60000` | — | tope total de la posposición por silencio/carga, desde el plazo vencido; agotado se retira con WRN (0 = sin tope, no recomendado; cota 5000..600000) |
| `publication_retain_max_ms` | `60000` | — | retención máxima antes de soltar sin publicar (cota 5000..600000) |

Las tres primeras sólo actúan con `user2_stages` y `npc_activation` distintos de 0 (como S33); en el invitado no
hacen nada. Inis: [`docs/research/ini/cdmp.p8-s35-*.ini`](../ini/).

## 3. Líneas que hay que mirar (anfitrión salvo la última)

- Arranque: `NpcActivation S35: npc_action_evidence=… npc_containment_guard=… npc_degradation=… | npc_no_progress_ms=…`
  y `Session2: publication_gate ON|PROBE: …`.
- Cada unidad (dos líneas como mucho): `NpcActivation [acción OnServer] … controlador_actor_entrada=0x… controlador_actor_salida=0x…
  emisor=no_medido emisor_s34=0x… resultado_interno=… rama=… aplicada=… evidencia=<aplicada|rama281|rechazada|retorno0|otro_controlador|otro_nacimiento|retirada|ilegible|no_calculada>
  decide=<s34|s34(probe h1b)|h1b> progreso=…`.
- `NpcActivation [contención pospuesta] … motivo=<req|silencio|carga> posposiciones=N … última_req_invitado_hace_us=… probe=<0|1>`
  (la primera vez por unidad).
- `NpcActivation [retirada cancelada] … motivo=<progreso|evidencia nueva|posponer> …`.
- Cada 60 s, junto al `[resumen]` de S33: `NpcActivation [resumen S35] evidencia=… (aplicada=… rama281=… rechazada=…
  retorno0=… otro_controlador=… otro_nacimiento=… retirada=… ilegible=…; S34 y H1b distintas=…) | guard=… plazo_ms=…
  gracia_req_ms=… silencio_ms=… pospuestas req=… silencio=… carga=… (probe: pospondría …) | al ejecutar: válidas=…
  ya_no_está=… progreso=… evidencia_nueva=… posponer=… | degradación=… asignaciones_pausadas=… red_antigua_no_rearmada=…`.
- `WRN NpcActivation [contención degradada] la contención no puede actuar (…)` / `… fin: …`.
- `Session2 publication gate: RETAINED, not published 0x… (type N) to user 2's session 0x…: <motivo> (parent 0x…;
  release '<salida>', held N ms; CC slot 0x…)`, o `WOULD RETAIN (probe, published as before)`. Motivos:
  `restore 0 (observer is not its controller)`, `CC unreadable (not proven absent)`, `actor unreadable`,
  `controller unreadable`, `its parent is withheld, unsafe or unreadable`, `its parent was refused in this release`.
- `WRN Session2 publication gate: DROPPED WITHOUT PUBLISHING 0x… … for N ms (publication_retain_max_ms reached | its
  table had no room to keep it)`.
- `Session2 publication gate: observer hold|family table full: 0x… withheld WITHOUT an entry …` / `WOULD be withheld …`.
- `Session2 publication gate: NPC 0x… (type N, controller 0x…): its CC could not be read (slot 0x…; not proven absent): …`.
- Cada minuto, dentro de `Session2 probe (minute N): child hold …`: `| publication gate (on|probe): releases evaluated …
  published … gone … refused … (restore0 …, cc_unknown …, actor_unknown …, controller_unknown …, dependency …,
  parent_in_batch …) -> would … retained … dropped unpublished … (expired …, no room …; longest retention … ms, max … ms)
  | native exits: full table withheld without entry … (would …) | CC unreadable NPC held … (would …)`.
- Revisión: `WRN NpcActivation [posposición agotada] … posposiciones=N pospuesta_us=…`; en `[acción OnServer]`
  `salida_discrepante=<0|1>`; en `[resumen S35]` `| salida_discrepante=… posposiciones_agotadas=… posposición_máx_ms=…
  tope_posposición_ms=… degradada_sin_progreso=… (máx …)`; `WRN NpcActivation [contención degradada] N unidad(es)
  asignada(s) … sin progreso …` (cada minuto mientras dure); `NpcActivation [entradas]: … pila TLS muerta limpiada=…`;
  en `publication gate` del minuto `| merged with a fresh hold … | members dropped with an unpublished parent … |
  UNCHECKED NotifyAppear (went out unchanged): actor manager unreadable …, recipient unreadable …, error … | probe
  shadow: recorded … (table full …) would recover … (slowest … ms) would drop … gone … pending …`; y
  `WRN Session2 publication gate: a NotifyAppear of N key(s) went out UNCHECKED (…)`.
- Invitado (sólo si cierra): `GuestS3 crash capture: <reg> 0x… is the ClientHierarchyActorComponent (vtable
  exe+0x5578BB8) of actor 0x… key 0x…, NOT a CharacterControl: its hierarchy parent is key 0x…`.

## 4. Criterios de aborto (parar, desinstalar con `deploy.ps1 -Remove`, devolver logs)

- Cualquier cierre del juego (invitado o anfitrión). Con `publication_gate=1` un cierre en `0x88F832` o `0x1831598`
  invalida la hipótesis de cobertura: guardar el dump y el log.
- Paso 2: cualquier 2005 a Shadow con restore 0 de un actor que Shadow no controla (contar en la captura del invitado
  con `a1-cierres/audit_captures.py` sobre una copia). Debe ser 0.
- Paso 2: una `retirada local` de una unidad que antes tuvo `evidencia=aplicada` o `rama281`.
- Paso 2: `[CONTROL ABORT] … no accepted StartActionReq 5 s` con `npc_activation=1` y `npc_degradation=1`.
- Paso 2: `asignaciones_pausadas` > 0 sin una `[contención degradada]` que lo explique.
- `DROPPED WITHOUT PUBLISHING` o `would no entry` frecuentes (orientativo: más de 20 en 10 min), o
  `CC unreadable NPC … would` con tipos que se ven en pantalla (gimmicks): la puerta dejaría invisibles cosas que no
  debe; no pasar al paso 2 hasta refinar el lector.
- Paso 1: `evidencia=aplicada` en 0 mientras el invitado manda Req (`[primera petición]`): H1b no está midiendo; no
  pasar al paso 2. `otro_controlador`, `ilegible` o `salida_discrepante` frecuentes: revisar antes de seguir.
- Paso 1 (revisión 4), en `publication gate (probe)`: no pasar al paso 2 si `dependency` o `cc_unknown` son una parte
  apreciable de `refused` (orientativo: más del 10 % de `releases evaluated`) o si `probe shadow … would drop` es
  alto frente a `would recover`: la puerta dejaría invisibles muchos actores. Mirar las líneas `WOULD RETAIN` de esos
  motivos (tipo, ranura CC) antes de decidir.
- Cualquier `UNCHECKED NotifyAppear` en el paso 2: esa llamada salió sin puerta (puede haber llevado restore 0).
- `posposiciones_agotadas` > 0 con el invitado jugando con normalidad: el silencio no es real (revisar
  `npc_guest_silence_ms`); `degradada_sin_progreso` > 0 durante minutos: parar (nadie retira esas unidades).
- Progreso de misiones en la partida propia del invitado, o una acción del invitado que no acredite la misión del
  anfitrión (quest_policy sigue en probe en estos inis).

## 5. Plan de prueba

### 5.1 Un PC con `cdmp_peer` (primero)

El peer no ejecuta IA ni manda Req de IA: valida instrumentación, estados, colas y que nada global se dispare; no
valida activación ni movimiento.

1. Juego cerrado. `.\scripts\build.ps1`, `.\scripts\deploy.ps1`. Copia de `%LOCALAPPDATA%\Pearl Abyss\CD\save` en
   `backups\` (U-1/U-e escriben en la memoria del juego).
2. Guion del peer como en [u0-resultados-s28.md](u0-resultados-s28.md) §8 (`make_u1_idle_script.py` → `build\e2e\u1_idle.troc`).
3. **Paso 1:** `bin64\cdmp.ini` = `docs/research/ini/cdmp.p8-s35-local-paso1.ini` (`npc_activation=1` como en S34,
   claves S35 en probe). Lanzar el juego, cargar Hernand, arrancar
   `.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script build\e2e\u1_idle.troc --var ACCOUNT=1 --var SAVE_SLOT=108 --start-after 3000 --linger 30000 --seconds 1200`,
   alejarse para armar U-1 y esperar 5 min. Esperado: las retiradas locales de S34 (el peer no conduce), casi
   ninguna línea `[acción OnServer]` (el peer no pide acciones de NPC; si aparece alguna, `decide=s34(probe h1b)`),
   `[contención pospuesta] … motivo=silencio … probe=1` por unidad,
   `[resumen S35]` con `pospondría silencio` > 0, `publication gate (probe)` con `would` contado, sin cierre.
4. **Paso 2:** reiniciar juego y peer con `cdmp.p8-s35-local-paso2.ini`. Esperado: **ninguna** `retirada local` por
   falta de activación (el peer está en silencio: `[contención pospuesta] … motivo=silencio`), ningún `[CONTROL ABORT]`
   de 5 s, `publication gate (on)` con `retained`/`dropped unpublished` acotados y sin cierre. Volver junto al peer
   (≤ 7 celdas): el drenaje y la limpieza de U-1/U-e siguen como antes.
5. Leer `bin64\cdmp.log`; anotar los contadores de `[resumen S35]` y del `publication gate` del último minuto.

### 5.2 Shadow (después del 5.1)

Anfitrión en el PC del usuario, Shadow invitado ([PRUEBA-DOS-PC.md](../../PRUEBA-DOS-PC.md)). Mismo binario en los dos.

1. **Paso 1 (mide):** anfitrión `cdmp.p8-s35-host-paso1.ini` (`npc_activation=probe`, claves S35 en probe); Shadow
   `cdmp.p8-s35-shadow.ini`. Repetir la ruta de S34 (Hernand, perros/cabras, cría de ciervo, carros), con el anfitrión
   cerca y lejos. Devolver por la carpeta compartida el log, `cdmp_packets_*.bin` y dumps de los dos lados.
   Criterio para pasar al paso 2: `evidencia=aplicada` > 0 en unidades con Req del invitado; `S34 y H1b distintas`
   ≈ número de unidades aplicadas (la S34 nunca contaba); `CC unreadable … would` sólo en casos explicables; en la
   captura de Shadow, cada 2005 con restore 0 hacia Shadow aparece como `WOULD RETAIN` en el log del anfitrión.
2. **Paso 2 (actúa):** anfitrión `cdmp.p8-s35-host-paso2.ini` (`npc_activation=1`, claves S35 a 1,
   `npc_no_progress_ms=8000`); Shadow igual. Misma ruta y además: viaje rápido del anfitrión, salida y vuelta del
   invitado. Éxito: cero restore 0 hacia Shadow de actores que no controla; cero retiradas de unidades con aplicación
   previa; posposiciones sólo con silencio/carga reales; retenciones que se resuelven solas (restore 1) en su mayoría;
   ningún cierre. Anotar qué quedó invisible (`DROPPED WITHOUT PUBLISHING`) y si se nota en pantalla.

## 6. Lo que NO se hizo (y por qué), riesgos abiertos

- **Emisor real no medido.** Capturar el resultado de vf170 exigiría un mid hook nuevo dentro de `0x2A78CA0`
  (`0x2A78CF8`) sin medida en vivo; la evidencia H1b + `actor+0x90` basta para atribuir. Queda `emisor=no_medido`.
- **Salidas no cubiertas por la puerta:** `character_hold` (Timeout/Native de personajes, `:697-716`), `child_settle`
  y D-53 (hijos de usuario); en `prepare`: controlador NPC ilegible o actor no encontrado en el SAM (sigue nativo), NPC
  con controlador que no es usuario conocido (`CtrlNotUser`, nativo como sin el mod), familia con dueño mixto
  (`partial`), vínculo ilegible (`unlinked`) y sesión receptora ilegible (llamada sin cambios). Se cuentan en los
  contadores existentes; requieren decidir su política con medida.
- **La dependencia es de un nivel** y por el vínculo serializado/jerárquico: otras relaciones (lectores de otra capa)
  no están enumeradas (AUDITORÍA §1, «no hay prueba de punto común»).
- **`Unknown` puede ser frecuente** en tipos sin CC real con algo en la ranura `+0x40` (el test S3 antiguo simulaba
  un gimmick así). Con `publication_gate=1` esos actores quedarían invisibles para el invitado hasta 60 s y luego sin
  publicar. Por eso el paso 1 es probe y registra el valor de la ranura.
- **Retenidos sin entrada** (tabla llena): no hay reanuncio propio; vuelven cuando el motor los anuncie otra vez.
- **Ventana de ejecución** entre la revalidación y los `Logout` (§1.2).
- **8 s / 2 s / 8 s** son puntos de partida; la cola tardía de A (> 5 s) y las ráfagas grandes no se han medido con
  la evidencia corregida.
- **Degradación:** las unidades ya asignadas sin progreso durante una contención no lista sólo se cuentan y se avisan
  (revisión 3); una retirada acotada por unidad que no dependa de las piezas que fallan no está diseñada.
- Familias (reserva del subárbol, marcos anidados, `parent_birth`), viajeros y rearmado: fase siguiente.

## 7. Verificación de esta entrega

`.\scripts\build.ps1`: **66/66** pruebas (65 previas + `optd_publication_gate`). Nuevas pruebas puras en
`tests/optd_npc_activation_tests.cpp` (fixtures A/B/C de S34, rama 281, reentrada TLS, Req entre selección y
ejecución, gracia acotada, silencio/carga, versiones de miembros, degradación, equivalencia con S34 en `0`) y
`tests/optd_publication_gate_tests.cpp` (tres estados de CC, matriz de admisión, dependencias, retención); de extremo a
extremo por las costuras de child_hold en `tests/optd_s3_tests.cpp` (Timeout retenido/recuperado/expirado/probe,
Native con otro actor, CC desconocido frente a ausente, tabla llena, familia AfterParent con restore 0 — el caso
`B01003AE` —, WithParent tras un padre rechazado, pieza de jerarquía con padre restore 0, padre desaparecido).

## 8. Revisión independiente (14-10): «GO con correcciones»

La revisión confirmó el ABI de `0x2A78CA0` (param4 no es la sesión; vf+0x170 y la comparación con `actor+0x90`), que
con las claves a `0` el comportamiento es el de S34, que `probe` no cambia nada y que se conserva el orden de cerrojos.
Hallazgos y qué se hizo (sigue sin ejecutarse el juego; `build.ps1` 66/66):

| # | Hallazgo | Hecho |
|---|---|---|
| 1 (M) | `classify` exigía `ctl_exit` y su igualdad ANTES de mirar H1b: una aplicación con la lectura de salida a 0 o cambiada salía `ilegible`/`otro_controlador` → falsa retirada | H1b + `ctl_entry` = controlador de la unidad ⇒ `aplicada` siempre; la discrepancia se cuenta (`salida_discrepante`) y va en la línea. Pruebas `s35_h1b_exit_read_fails` |
| 2 (M) | Posposición por silencio sin tope, con el reloj reiniciándose | `npc_postpone_max_ms` (60 s por defecto) contado desde el plazo vencido original (reiniciar el reloj no lo alarga); agotado ⇒ se retira con `WRN [posposición agotada]`; `posposición_máx_ms` en el resumen. `s35_postpone_is_bounded` |
| 3 (M) | Con la contención degradada las unidades ya asignadas quedaban sin dueño, sin límite | Se cuentan cada segundo (`degradada_sin_progreso`, máximo) y `WRN` cada minuto mientras dure. **No** se retiran a ciegas: la pieza que falta puede ser la evidencia o la limpieza; la retirada acotada independiente de esas piezas queda pendiente de diseño (§6) |
| 4 (M) | La puerta dejaría invisibles piezas cuyo padre tiene CC `Unknown`, incluso padres `CtrlNotUser` que ella misma deja nativos | Un padre (y un actor) sin usuario controlador sigue la regla nativa de `prepare`. En probe, registro sombra: lo que se habría retenido se sigue en pases posteriores y se cuenta si se habría recuperado dentro de `publication_retain_max_ms` o se habría soltado. `dependency`/`cc_unknown`/sombra añadidos a §4 |
| 5 (M) | Pruebas: lecturas del detour sin probar (AUDITORÍA §6.5), cancelación, degradación, casos de la auditoría | Lecturas de `action_detour` extraídas a plantillas con política de lectura (`read_action_key/entry/exit`, `make_call`) y probadas con un marco simulado (`s35_detour_reads_on_a_frame`). La ejecución de `npc_activation_cleanup` es ahora `execution_decision` + `cancel_execution` (puras, `s35_execution_decisions`). Ack viejo > `max_hold_ms` (`old_ack_beyond_max_hold`), cadena rechazada profunda, época nueva con entrada retenida, pérdidas (despawn), reanuncio durante la retención y padre del servidor en `optd_s3`. **No factible con las costuras actuales:** `npc_activation_cleanup` y `npc_control_impl` de extremo a extremo (user2_stages.cpp no tiene costuras de prueba y llama a SAM::FindActor/Logout) y `DropNoRoom` de extremo a extremo (la liberación libera su propio hueco; sólo otra hebra reservándolo lo provoca): cubiertos en puro |
| 6 (B) | Devolver una entrada podía chocar con una retención fresca de la misma clave | `HoldTableN::find`; si existe, sólo se fusionan `unsafe_since_ms` (mínimo) y `unsafe_retry_ms` (máximo); los campos frescos se conservan; contado como `merged`, no como retención |
| 7 (B) | Soltar un padre sin publicar no arrastraba a sus miembros | Los padres soltados (expirados, sin sitio o desaparecidos) arrastran a sus miembros de la tabla de familia (`members dropped with an unpublished parent`) |
| 8 (B) | El límite de retención sólo se miraba al rechazar otra vez | Se mira en cada pase (NPC y familia); `longest retention` se mide en cada entrada retenida. Prueba del reanuncio |
| 9 (B) | `admit` incoherente con el controlador | Una regla: el controlador siempre recibe su actor (§1.4 corregido) |
| 10 (B) | Orden padre/hijo sin vínculo serializado | Dos fases: veredicto propio y luego propagación «padre rechazado en este lote» por el vínculo leído hasta punto fijo; la prueba de cadena profunda falla sin la segunda fase (comprobado mutándola) |
| 11 (B) | `prepare()` falso (SAM ilegible, sesión ilegible, excepción) publica todo nativo y `forget()` borra lo retenido | Contado y registrado (`UNCHECKED NotifyAppear`, WRN acotado). Se mantiene `forget()`: el motor acaba de anunciar esas claves él mismo, conservar su retención sólo provocaría un segundo anuncio |
| 12 (B) | Registro: progreso con guard=0 contado como `ya_no_está`; `[retirada cancelada]` imprimía `node.data`; contadores por llamada | El motivo real se cuenta (`execution_decision.why`); la línea lleva la edad de la última Req; `S34 y H1b distintas` y `asignaciones_pausadas` cuentan unidades (nacimientos que U-e habría asignado); aviso si `npc_containment_guard=1` sin `npc_action_evidence=1` |
| 13 (B) | `containment_context` bajo `ActivationLock` | Se calcula antes de tomar el cerrojo en la reserva y en la ejecución |
| 14 (B, previo) | `t_pending` colgando si una excepción SEH cruza el detour (/EHsc no ejecuta destructores) | El original se llama dentro de `__try/__finally` (restaura la pila TLS en el desenrollado sin tragarse la excepción) y, además, al entrar se descarta un `t_pending` que no esté por encima del marco actual (`pila TLS muerta limpiada`) |
| 15 | Ventana residual tras `child_hold_retire_family` | Documentada (§1.2): una evidencia que llegue por otro hilo entre la revalidación y los `Logout` ya no cancela; cada miembro revalida nacimiento y baja antes de su `Logout` |

Clave nueva: `npc_postpone_max_ms` (inis regenerados con `60000`).
