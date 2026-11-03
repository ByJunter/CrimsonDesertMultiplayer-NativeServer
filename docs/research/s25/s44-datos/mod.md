# S44 §3.3 — Mapa del mod actual: escenas del anfitrión en el invitado

Build 1.0.0.2976. Worktree `C:\Emulador CD-escenas`, rama `opcion-d-escenas` (`4ca7f44`). Solo lectura.
Rutas de código relativas a `src/mod/session2/` salvo que se diga otra cosa; docs: `P/` = `docs/research/pendientes/`,
`S/` = `docs/research/s25/`, `I/` = `docs/research/ini/`.
Etiquetas: **[V]** leído en código o en un log citado; **[E]** inferencia fuerte; **[H]** hipótesis.

---

## 0. Resumen

1. **Por qué le llegan al invitado las escenas del anfitrión [V].** De forma nativa, el servidor envía el 2661, el 2810
   y el 2322 de un secuenciador solo al personaje dueño (ToTarget: `observer_seq_copy_logic.h:6-11`, `:146-150`). El
   invitado los recibe únicamente porque el mod del **anfitrión** los copia: `observer_seq_copy=1` lee la captura S>C
   del propio cliente del anfitrión y los inyecta en la cola del usuario 2. Los 2981 y 2524 sí le llegan de forma nativa
   (buscador por cercanía), pero sin el 2661 no tienen instancia en ningún gestor y no hacen nada.
   → Conclusión: **«como lo haría el juego nativo si el anfitrión fuera otro jugador» = el invitado no vería ninguna
   escena del anfitrión, tampoco las del mundo.** La opción A existe para romper eso. La distinción entre mundo y
   personal no la hace el servidor: tiene que salir de los datos de la escena.
2. **En el invitado todo es por gestor, no por escena [V].**
   - `guest_seq_route` decide por paquete: un actor que es un personaje distinto del de M va a X. El único criterio
     por escena es la lista `deny`.
   - `guest_seq_play` hace tick de **todo** X y pone `+0x231 = 3`, lo que reproduce de golpe todo lo precargado.
   - `guest_x_ui_veto` veta por manejador cuando G = X.

   El único sitio donde ya existe una clasificación por escena es `classify_2524` (`guest_seq_route_logic.h:1120`):
   WORLD / Function (`cd_seq_funcnpc_*`) / NoNpc («host's own / personal») / …. Pero solo se aplica al 2524 y está en
   `probe` en todos los kits.
3. **El punto natural de una política por escena en el invitado** es el gancho del 2661 en `0xC0612A`
   (`guest_seq_route_hook`, `guest_seq_route.cpp:1271`). Allí ya se leen:
   - el recurso;
   - los enlaces (rol `node`, actor y tipo, 11 bytes por enlace);
   - y existe el camino «rechazar» (`gsr::refuse`: kind := 0, no se crea en ningún gestor).

   El nombre de la escena **no** se puede leer todavía en `0xC0612A`: la instancia aún no existe. Hoy se lee en X con
   `peek_instance`, solo al llegar el 2524.
4. **El punto natural en el anfitrión** es `osq::Copier::add_pending` (`observer_seq_copy_logic.h:848-855`), donde ya
   se aplica `observer_seq_copy_only`. Allí el anfitrión tiene el 2661 completo: dueño, enlaces con rol y tipo, y
   recurso.
5. **Tests:** hay tests de la lógica pura para `decide`, `refuse`, `gate`, `DenyList`, `classify_2524`,
   `is_function_scene`, `OnlyFilter`, el veto y la interacción C1 (§5.3). Ningún test lleva un 2661 de una escena
   personal real: no está el de `0x9586FFA4`.

---

## 1. Piezas del INVITADO que tocan escenas del anfitrión (2661 / 2981 / 2810 / 2322 / 2524)

### 1.1 Valores en los kits actuales [V]

| Clave | `I/cdmp.2inst-guest.ini` | `I/cdmp.2inst-s43-activo-guest.ini` | `I/cdmp.2inst-s43-pose-guest.ini` (A/B) | `I/cdmp.p8-s38-shadow.ini` | Por defecto (`config.cpp`) |
|---|---|---|---|---|---|
| `guest_stage_diag` | 1 | 1 | 1 | 1 | 0 (:196) |
| `guest_seq_route` | 1 | 1 | 1 | 1 | "0" (:265) |
| `guest_seq_watch` | 9 claves de la sesión 9 | = | = | = | "" |
| `guest_seq_route_deny` | 8CD771B1 | 8CD771B1 | **8CD771B1,9586FFA4** | 8CD771B1 | "8CD771B1" (:267) |
| `guest_seq_route_2524` | probe | probe | probe | probe | "0" (:276) |
| `guest_seq_route_2524_allow` | (vacío) | (vacío) | (vacío) | (vacío) | "" |
| `guest_seq_play` | 1 | 1 | 1 | 1 | "0" (:273) |
| `guest_seq_play_live` | 0 | 0 | 0 | 0 | 0 |
| `guest_seq_bodyanim_diag` | 1 | 1 | 1 | 1 | 0 (:278) |
| `guest_sched_route` | 1 | 1 | 1 | 1 | "0" (:269) |
| `guest_sched_bind` | 1 | 1 | 1 | 1 | "0" (:271) |
| `guest_x_stage_veto` | 1 | 1 | 1 | 1 | "0" (:280) |
| `guest_x_camera_veto` | probe | probe | probe | probe | "probe" (:281) |
| `guest_x_ui_veto` | 1 | 1 | 1 | 1 | "0" (:282) |
| `guest_x_ui_veto_allow` | SetInteraction | = | = | = | "SetInteraction" |
| `guest_x_interaction_stage` | probe | probe | probe | probe | "0" (:289) |
| `guest_schedule_load` | 1 | 1 | 1 | 1 | "0" |
| `seq_actor_restore` | 1 | 1 | 1 | 1 | "0" (:329) |
| `guest_scene_diag` | 1 | 1 | 1 | 1 | — |

Lo que se lanza y en qué orden: `guest.cpp:1557-1589`. Primero `guest_stage_diag_start` y después, en un bloque,
`guest_seq_play_start`, `guest_seq_route_start`, `guest_bodyanim_diag_start`, `guest_x_veto_start` y
`guest_x_interaction_start`. La compuerta de entrega es `guest.cpp:1135` → `guest_seq_route_gate`.

### 1.2 `guest_seq_route` (enrutado al gestor X)

**Archivos:**
- `guest_seq_route.h` (contrato, :1-102);
- `guest_seq_route.cpp` (ganchos `:990-1050`, decisión `:1271-1500`, compuerta `:1623-1660`);
- `guest_seq_route_logic.h` (lógica pura).

**Ganchos (mid hooks, justo después de `ClientField::vf48`, RAX = M)** — tabla `points()` en
`guest_seq_route.cpp:1028-1048`, RVA en `:44-99`:

| Punto | RVA | Manejador | Qué lee |
|---|---|---|---|
| P2661 | `0xC0612A` (5 B) | 2661 PrepareSequencerAck `0xC05BB0` | BL = kind, `[R12]` = actor resuelto, `[R12+0x18]` = paquete crudo: actor @1, secuencia @9, etapa @19, **recurso @27**, enlaces desde el byte 91, de 11 bytes cada uno: `u32 node(rol) \| u32 actor \| u8 type \| 2×u8` (`observer_seq_copy_logic.h:98-99`) |
| P2981 | `0xC07749` (9 B) | 2981 `0xC073A0` | `[R15]` actor; secuencia @5 y estado @13 |
| P2810 | `0xC0659E` (8 B) | 2810 `0xC06330` | `[RBP-8]` actor y `[RBP-0x58]` secuencia |
| P2322 | `0xB87A67` (7 B) | 2322 `0xB87890`, la escritura `[RAX+0x231]=2` | R14 paquete y `[RBP-0x10]` actor |
| P2524 | `0xC08178` (9 B) | 2524 `0xC07D00` → `0xA9E480` | `[R15]` actor; secuencia @5 y rama @13 |
| interno 2524 | `0xA9E55B` (6 B) | dentro de `0xA9E480`, con el cerrojo `[G+8]` | instancia hallada; `rdi := 0` si la escena no está cargada (`:237-257`) |
| teardown | `0x43B0B6` | `0x43AE50` | redirige `vf12` de una etapa de horario cuyo X ya no existe (`:966`, `:1111`) |
| P2335/2126/2958/2909 | `0xC0B5C1` / `0xC0BA4E` / `0xC0C761` / `0xC0CC91` | horario (ver `guest_sched_route`, §1.6) | |

Si `guest_stage_diag=1` o `load_diag=1`, los puntos P2661/P2981/P2810 son de `guest_stage_diag` y P2322 es de
P8Probe. Sus callbacks llaman primero a `guest_seq_route_hook` (`guest_seq_route.h:24-26`).

**Decisión (`gsr::decide`, `guest_seq_route_logic.h:372-399`), en orden:**
1. off / sin gancho;
2. kind ≠ 4;
3. sin M o sin actor;
4. clave del paquete ≠ actor;
5. **no es personaje 0xA → nativo**;
6. **el propio personaje de M → nativo** (las escenas propias del invitado, incluida su tienda U2);
7. sin X, X no es `ClientSequencerStageManager`, X no es suyo o X == M;
8. (horario) sin contenedor `+0x148`;
9. **`denied`** (recurso en `deny`);
10. (2524 con `_2524`) no es WORLD;
11. (2524) X no tiene tick / no está listo;
12. si no se ha cortado antes → `Route` (RAX := X) o `WouldRoute` en probe.

**Rechazo (`gsr::refuse`, `:437-453`):** con el modo a 1, el 2661/2322 de otro personaje que no se puede llevar a X no
toca M.
- **2661:** `ctx.rbx = 0`, que lleva a la ruta nativa «kind ≠ 4» del manejador: no se crea nada.
- **2322:** su escritura va a `g_sink`.
- Un 2661 `Denied` también se rechaza.
- 2981 y 2810 nunca se rechazan: en M solo buscan un id que M no tiene.

Código: `guest_seq_route.cpp:1482-1491`.

**Compuerta de entrega (`guest_seq_route_gate`, `guest_seq_route.cpp:1623`; `gsr::gate`, `logic.h:419-431`),** en
el NetworkThread, antes de `OnRecvPacket`:
- un 2661/2810/2322 que nombra a otro personaje se descarta salvo que el enrutado esté a 1 y el gancho puesto;
- cualquiera que nombre a un personaje se descarta mientras no se conoce el propio personaje (su 2756);
- 2981 y 2524 nunca pasan por la compuerta: llegan de forma nativa.

**Efectos laterales del Route:**
- `guest_seq_play_note_x(X, …)` (`:1450-1452`): alimenta la tabla de X que `guest_seq_play` usa para el tick.
  Todo 2661/2981/2810/2322 enrutado cuenta, el 2524 no. El 2322 rearma el 3.
- `guest_x_context_routed_2981` (`:1478`): contexto X para el V0 del veto.
- `g_seq_res.note(seq, recurso, denied, binds)` (`:1426-1437`): un anillo con el recurso y las claves enlazadas de
  cada 2661, para juzgar el 2981 y el 2524 posteriores (`logic.h:250-297`).

**`guest_seq_route_deny`** (`config.h:442-444`, `logic.h:188-248`; parseo en `DenyList`):
- Si el recurso del 2661 está en la lista: `Denied` → rechazado, no se crea en ningún gestor.
- Sus 2981 y 2524 heredan `denied` por `g_seq_res` y no se enrutan: van a M, donde no tienen efecto.
- El comentario de `logic.h:190-195` es la razón histórica: «No flag in the 2661 tells a field-reloading cutscene
  from a shop or table scene» (sesión 9, comparando los 86 bytes de play data) [V para esos bytes]. **Los enlaces y
  sus roles no se compararon allí.**

**`guest_seq_route_2524` / `_allow`** (`config.h:458-463`; `logic.h:1040-1165`; `cpp:1361-1416`):
- Modo `gsr::mode_2524`: 0 = legacy (el 2524 solo va a X con `guest_seq_play=1` y `_live=1`); probe = clasifica y no
  enruta; 1 = enruta solo WORLD.
- `classify_2524(Scene2524Facts)`, en orden:
  1. `UnknownSeq` (no se vio el 2661);
  2. `Denied`;
  3. **`NoNpc` = «binds no NPC: the host's own / personal scene (0x164-0x167 bind only 0xA0100001)»**;
  4. `NoNpcHere`;
  5. `NoType3` (ningún NPC presente de tipo `[[a+0x88]+1]==3`);
  6. `NameUnknown` (falla cerrado);
  7. **`Function` = `cd_seq_funcnpc_*`**;
  8. `NotAllowed`;
  9. `World`.
- El nombre se lee en X: `peek_instance` + `read_scene_name`, en `[[obj+0x48]+0x1B8]` (`logic.h:1160-1170`).
- Los contadores `g_2524_class` aparecen en el resumen (`summary_2524`, `:760`).
- **Ojo:** es la única clasificación por escena que existe. La revancha de Matthias tendría NPC de tipo 3 (Matthias
  `0x20000006`, Game Mechanic `0x20000004`). Lo más probable es que salga **WORLD** [E], así que este criterio no
  separa lo personal de lo del mundo cuando lo personal también enlaza NPC.

**`guest_seq_watch`:** lista G5 (estado de NPC vigilados y de X) que se escribe cada 5 s desde el hilo de diagnóstico
(`g5()`, `:730`). Las claves de la sesión 9 están obsoletas y la lista se llena (64) (`S/s43-datos/pose-informe.md:213-217`).
`watch()` solo admite claves 0xB (`:488`), así que **el Kliff remoto no se puede vigilar**.

### 1.3 `guest_seq_play` y `guest_seq_play_live`

**Archivos:** `guest_seq_play.h:1-52`, `guest_seq_play.cpp` (RVA `:49-109`), `guest_seq_play_logic.h` (`gsp::plan`).
- **Gancho:** mid hook en `0x8B6144`, dentro de `0x8B6060` (bucle principal, una vez por fotograma), justo antes del
  tick nativo de M `0xA953C0(M, Q, mode)`.
- **Qué hace**, para cada X de la tabla (se llena **solo** por Route de `guest_seq_route`), si se cumplen identidad,
  campo y M a 3:
  - `X+0x231 = 3` una vez por cada 2322 enrutado;
  - después `0xA953C0(X, Q, mode)` (tick).
  - Con `_live` también `0xAA3E70(X, dt)`, con un gancho de salto en `0xAA4B27` → `0xAA4CB2` y la compuerta de lectura
    `X+0x2A4/+0x288/+0x2A8/+0x2D4`.
- **Decide por X, no por escena:** el 3 reproduce todo lo elegible de `+0x1B0` / `+0x1C0` («una por llamada»,
  `logic.h:13-15`). La llamada nativa `0x9F8FCE0` (la que pone M en 3) **solo** reproduce al momento lo que cumple
  «owner's missions / type 5 / AABB» (`logic.h:11-13`). Escribir el 3 a mano se salta ese filtro nativo: es el riesgo
  R12 de `P/p8-impl-escenas-play.md:301`. **Es una pista nativa para el §3.2 del encargo** [E].
- **Kits:** `guest_seq_play=1` y `_live=0`. `_live` no ha funcionado nunca: compuerta retenida al 100 % (S9), cierre en
  `0xA9E63B` (S10) y la imagen empeoró (S11).
- **Exige** `guest_seq_route=1` (`gsp::effective_mode`, `logic.h:56-60`).

### 1.4 `guest_stage_diag` (solo lectura)

`guest_stage_diag.h:1-59`; RVA en `guest_stage_diag.cpp:40-106`:
- entrada 2661 `0xC05BB0`, enrutado `0xC0612A` y resultado de `0xA9CA40` en `0xC061C1`;
- `0xA9CE87` (actor enlazado que falta) y `0xA9CB87/98`, `0xA9CC0E`;
- 2981 `0xC073A0` / `0xC07749`; 2810 `0xC06330` / `0xC0659E`; 2322 `0xB87A32`;
- lado NPC `0x9ED290` / `0x9ED401` / `0x9ED453` / `0x9ED48C`;
- escrituras de `M+0x231`: `0x77AA8B` / `0x77B0CE`.

No decide nada. Es la **dueña de los ganchos compartidos** con el enrutado (P2661/P2981/P2810). Imprime el token
`corr=` que permite emparejar con el anfitrión. Interesa para S44 porque **`0xC061C1` es un punto posterior a la
creación**: allí ya existe la instancia en X y se podría leer su nombre o stageinfo (§5).

### 1.5 `guest_x_stage_veto`, `guest_x_camera_veto`, `guest_x_ui_veto` y `guest_x_ui_veto_allow`

Archivos: `guest_x_veto.h:1-73`, `guest_x_veto.cpp`, `guest_x_veto_logic.h`.
- **V1** `0x5F2A3A` (manejador Loading `0x5F2A00`, tras su compuerta `0x204F730`): si G = X de otro personaje, RAX := 0
  y no carga. Es la defensa de raíz contra la recarga de campo de la sesión 9.
- **V0** `0x778AB0`: solo cuenta (`v0_effective`).
- Contexto: se pone en `0xC07749` y se limpia en `0xC07776`.
- **`gxv::judge`** (`logic.h:94-107`), **por gestor**:
  - M propio, su dueño vivo o el propio personaje → `OwnManager`, nunca se veta;
  - X de la tabla de `guest_seq_play` u otro personaje 0xA → `Veto`.
- **`guest_x_ui_veto`:** un mid hook por manejador (`Fn`, `logic.h:172-202`):
  - diálogo: SetQuestDialogAICategory, TimelineEvent, BlockInteraction, SetInteraction;
  - interfaz: InputBlock, UIControl, UI*;
  - cámara: SequencerCamera, LetterBox, FadeInOut, LookAt, CameraPreset, ForceLockOn, PostProcess…
- Con 1 solo se vetan los que tienen una salida nativa leída entera: **TimelineEvent, SetInteraction, InputBlock,
  UIControl, UIShowMinimap, UIStageGuide**. El resto (BlockInteraction, todas las cámaras, Fade, LetterBox…) queda
  en probe (`fn_mode`, `:262-269`).
- **`guest_x_ui_veto_allow=SetInteraction`:** SetInteraction corre de forma nativa para X (alta y baja de las
  interacciones dinámicas de los NPC: estado del mundo y tenderos). Por eso el invitado puede hablar con Renee, Delkin
  y los tenderos.
- El gancho lee el **nombre de la etapa** `[[[proc+0x10]+0x48]+0x1B8]` (`guest_x_veto.cpp:217-221`, `:478-481`): así
  salió `'cd_seq_boss_rematch_matt'` en la pose S43.
- **Límite clave para S44 [V]:** el veto **no cubre** lo que la escena hace al **cuerpo** del actor jugador (acciones,
  BodyAnim, pose). No hay ningún manejador de pista de animación en la lista. Por eso la pose congelada pasa con
  `guest_x_ui_veto=1` (`S/s43-datos/pose-informe.md:144-146`).

### 1.6 `guest_sched_route` y `guest_sched_bind`

- **`guest_sched_route`** (`guest_seq_route.h:53-59`, `cpp:1502-1620`). Cuatro ganchos: 2335 `0xC0B5C1`, 2126
  `0xC0BA4E`, 2958 `0xC0C761` y 2909 `0xC0CC91`.
  - Lleva las escenas de **horario** de NPC del anfitrión a X, donde la `vf13 0x9ED290` del NPC las busca.
  - Usa el libro `SchedLedger` para que el borrado y los cambios sigan a su 2335.
  - Rechaza los duplicados y los no creadores sin clave.
  - No trata secuenciadores (2661), pero comparte X. Las escenas de horario no tienen rol de jugador: el riesgo de pose
    no aplica aquí [E].
- **`guest_sched_bind`** (`guest_sched_bind.h:1-58`, punto `0xC0B626` tras `0xA9FEE0`).
  - Enlaza con la nativa `0x9F2FA0` a los NPC del anfitrión que la `vf13` dejó sin enlazar.
  - Cola de reintentos desde el gancho de fotograma `0x8B6144` de `guest_seq_play`.
  - Depende de `guest_sched_route=1`. Es ajeno a la clasificación de secuenciadores, pero **comparte el gancho de
    fotograma de `guest_seq_play`**: si se tocara la pieza de reproducción, hay que mantener ese gancho.

### 1.7 `guest_x_interaction_stage` (C1, tenderos)

`guest_x_interaction.h:1-36`, `guest_x_interaction_logic.h:95-190`. Ganchos A `0x2126A1F` (RAX = M), R `0x2126AAB`
y F `0x2126D11`, dentro de `0x2126920` (comprobación del aviso de interacción de una entrada dinámica).
- Si el id de etapa `[entry+0x20]` no está en M pero sí en un X conocido, con 1 hace RAX := X; en probe solo cuenta.
- Los X conocidos se los pasa `guest_x_veto` (`guest_x_interaction_note_x`, `guest_x_veto.cpp:452`, `:809`).
- Kits: **probe**. Con 1 (sesión 14) el invitado se quedó atascado en la interacción: «C1 en 1 es incompatible con
  cualquier diseño de tiendas» (`S/tiendas-u2.md:221-236`).
- Relación con S44: las entradas de los tenderos que ve el invitado vienen de escenas del anfitrión enrutadas a X
  (SetInteraction permitido). **Si una política dejara de crear en X las escenas `cd_seq_funcnpc_*` del anfitrión,
  desaparecerían esas entradas** [E] (ver §4).

### 1.8 BodyAnim: `guest_seq_bodyanim_diag` y la parte BodyAnim de `guest_seq_route_2524`

- `guest_bodyanim_diag.h:1-31`. Solo lectura: mid hooks en BodyAnim `0xA79770` (si no lo tiene ya P8Probe con
  `s1_diag=1`) y en `0x2432F60`, más G5 de `CC+0x70/+0x78` de los NPC vigilados.
- Requiere `guest_seq_route=probe|1`. No decide nada.
- La reproducción BodyAnim de verdad dependería de que el 2524 de una escena WORLD fuera a X (`guest_seq_route_2524=1`),
  pero en los kits está en **probe**: hoy **no se enruta ningún 2524** y la escena de X no construye BodyAnim por ese
  camino (`S/s43-datos/pose-informe.md`, §2.2).
- Las 10 llamadas BodyAnim sobre actores no NPC de cada sesión de la pose no se pueden atribuir: la vigilancia solo
  acepta claves 0xB.

### 1.9 Otras piezas del invitado que tocan escenas del anfitrión

- **`seq_actor_restore`** (`guest_seq_restore.h:1-28`; mid hook `0x86490F` en `RestoreActionsFromLoadData 0x8646D0`):
  un NPC anunciado en `seq_bodyanim` sin chart recibe la espera `0x26107D3C` en lugar de fallar el login. Así el 2661
  copiado encuentra al actor (`0xA9CE81`).
  - Solo toca actores remotos 0xA/0xB.
  - Interacción: si el Kliff del anfitrión llega con una acción `seq_bodyanim` de una escena personal, este módulo le
    pone la espera [E]. No es la causa de la pose: `restore OK` en S43.
- **`load_trip`** (`load_trip.cpp:71`, `:78`): deja fuera de sus viajes el 2322 de otro personaje
  (`guest_seq_route_other_character`).
- **`guest_scene_diag`** (G1-G4): solo lectura.
- **`guest_schedule_load`:** carga los ficheros de horario que nombra un 2005/2335/2874 del anfitrión. Es de horario y
  no afecta a secuenciadores.

---

## 2. Historia: por qué existe cada pieza, qué aporta hoy y qué riesgos ya estaban escritos

| Pieza | Introducida | Problema que resolvía | Qué se perdería al apagarla |
|---|---|---|---|
| `guest_stage_diag` | `5288876` 09-10, `P/p8-observador-escenas.md` §13 y §17 | Medir a qué gestor van los acks | Nada visible: diagnóstico y dueña de los ganchos compartidos |
| `guest_seq_route` (+`_watch`) | `fc49a0d` 10-10 (sesión 8), `P/p8-impl-opcion-a.md` §8 | Las copias caían en M; los NPC buscan en X | **Todas** las escenas del anfitrión en el invitado. Vuelven los NPC de secuenciador quietos y desaparecen las entradas SetInteraction de los tenderos |
| `guest_seq_route_deny` | `f1f2991` 10-10 (sesión 9), `P/p8-impl-escenas-play.md` §15.4 (:771-806) | La cinemática de liberar el campamento (`0x8CD771B1`) recargó el campo del invitado → título | La protección por lista. El propio §15 dice que es una lista y no la causa (:801-803). Hoy V1 (`guest_x_stage_veto`) cubre la recarga de raíz |
| `guest_seq_play` | `60b226a` 10-10 (sesión 9), `P/p8-impl-escenas-play.md` §0-§2 | Sesión 8: X sin tick, atascado en 2, 0 de 71 NPC con acción | **El beneficio principal:** en S9 todos los NPC de secuenciador pasan a `+0xCF 1` en ≤5 s (Rhett, Turnali, Tina, Dahlia, Grover, posadero, niños, Damiane, Freeswords). Resultado: mucho mejor (`P/p8-s9-npc.md:32-35`). Al apagarla vuelven los NPC quietos o en A-pose y los tenderos sin pose |
| `guest_seq_play_live` | `60b226a`, §3, §13 y §15.1 | La vida de las escenas: ramas y BodyAnim | Nada: está a 0 y nunca ha funcionado |
| `guest_seq_route_2524` (+`_allow`), `guest_seq_bodyanim_diag` | `5f30a52` 11-10 (sesión 13), `P/p8-impl-secuenciador-bodyanim.md` §0-§1 | NPC de la familia (d) de pie en espera: el 2524 iba a M | Nada visible (probe y solo lectura). Su `classify_2524` es la semilla de la clasificación |
| `guest_sched_route` / `_bind` | `8b773f8`, `9a80e58` y `51f9599` 10-10 (sesiones 11 y 12) | NPC de horario sin escena (X vacío) o sin enlazar | NPC de horario (criadas, niños, fauna) en su sitio |
| `guest_x_stage_veto` / `_camera_veto` | `6e67d0c` 10-10 (sesión 9), `P/p8-s9-cinematica.md` §8 | Etapa Loading de X → recarga → título | La defensa contra la recarga (V1). Las cámaras solo cuentan |
| `guest_x_ui_veto` | `16a4116` 10-10 (sesión 10), `P/p8-s10.md` §4 | El menú de diálogo del anfitrión con un tendero aparecía en el invitado (`P/p8-s10.md:28`) | Diálogos, bloqueo de entrada y guías del anfitrión en la pantalla del invitado |
| `guest_x_ui_veto_allow` | `f5cff86` 10-10 (sesión 11), `P/p8-impl-veto-interaccion.md:12-20` | Vetar SetInteraction dejaba al invitado sin hablar con Renee y Delkin | Con ella apagada (lista vacía), el invitado pierde las interacciones dinámicas que añaden las escenas de X |
| `guest_x_interaction_stage` | `9185382` 11-10 (sesión 13), `P/p8-impl-tenderos-c1.md` | El aviso del tendero solo busca en M (922/922 fallan) | Nada: está en probe (con 1 se atascó en la sesión 14) |
| `seq_actor_restore` | `9682d04` 10-10, `P/p8-impl-seq-restore.md` | Login fallido de NPC en `seq_bodyanim` (mesa de pulsos `0x1B0`, `0x13`) | Vuelven los fallos de login y los 2661 rechazados |
| `observer_seq_copy` (anfitrión) | `fc49a0d` 10-10, `P/p8-impl-opcion-a.md` §1-§7 | El invitado recibía 0 de 33 2661 del anfitrión | Ver §3 |

### 2.1 Riesgos que ya estaban escritos (con cita)

1. **El Kliff del anfitrión como «jugador» (rol `0x20000002`) de la escena en el invitado:**
   - `P/p8-impl-opcion-a.md:322` (§11.2);
   - `P/p8-s8-escenas.md:309-311` («X a 3 reproduce de golpe lo precargado (hasta 22 escenas), con el Kliff del
     anfitrión (`0x20000002/3`) como jugador»);
   - `P/p8-impl-escenas-play.md:295` (R1).
2. **El 3 a mano se salta el filtro nativo** (misiones del dueño / tipo 5 / AABB): `P/p8-impl-escenas-play.md:301`
   (R12). El 2322 copiado reproduce todo lo precargado: `P/p8-impl-opcion-a.md:324`.
3. **Cámara, UI y control del invitado:**
   - `0xAA3E70` actúa sobre el jugador local (R6, `P/p8-impl-escenas-play.md:300`);
   - menú de diálogo (`P/p8-s10.md:28`);
   - recarga de campo (`P/p8-s9-cinematica.md:13-24`).
4. **Pose congelada (revancha de Matthias `0x9586FFA4`):** `S/s43-datos/pose-informe.md:28-39`, `:144-146` y
   `:186-187`. La A/B del 15-10 (`S/s43-resultado-vivo.md:39-47`, commit `7c43a57`):
   - el 2661 llegó 15 veces y las 15 se denegaron;
   - el usuario no vio la pose congelada;
   - queda validada la **correlación**, pero el **mecanismo no está medido**: las variantes C y D y la sonda
     `[G5 body]` del Kliff remoto no se ejecutaron.
5. **Tienda del anfitrión:**
   - el invitado ve la escena sin menú cuando habla el anfitrión (`P/p8-impl-tenderos-s2.md:31-33`);
   - C1 a 1 lo atasca (`S/tiendas-u2.md:229-236`);
   - vetar SetInteraction le quita el diálogo (`P/p8-s10.md:325-331`).
6. **Minijuegos, recompensas y misiones en X:** prefijos `cd_minigame_`, `cd_seq_reward_` y `cd_seq_quest_` vistos en
   X (`S/s43-datos/pose-informe.md:233-237`).
7. **El 2810 aplica `vf150(0)` a la copia del Kliff del anfitrión** (`P/p8-impl-opcion-a.md:326`).
8. **Entrada tardía:** la escena empieza desde el principio (`P/p8-impl-opcion-a.md:331`).
9. **El invitado conducía escenas del anfitrión** (2888 a ~90 Hz, `P/p8-s9-npc.md:48`). Ya está filtrado en el
   anfitrión (C>S por id).
10. **2524 durante la carga:** cierre del juego (`P/p8-impl-escenas-play.md:817-843`).
11. **Propuesta ya escrita en la pose S43** (`S/s43-datos/pose-informe.md:230-245`): R2 = una clave
    `guest_seq_route_personal` que deja fuera de X las escenas que enlazan al Kliff del anfitrión como jugador y no
    hacen falta para el mundo, y mantiene `cd_seq_funcnpc_*` y `contents_weather`. Es la idea de S44, pero aún
    definida por nombres.

---

## 3. Lado del ANFITRIÓN: qué se le envía al invitado y dónde se podría decidir

### 3.1 Por qué le llega al invitado el 2661 de una escena del anfitrión [V]

- Servidor nativo: el 2661 lo serializa `0x2A4DEF0` y lo envía **ToTarget al personaje dueño**
  (`observer_stage.h:38-43`, `observer_seq_copy_logic.h:6-11`). En la sesión 7 el invitado recibió 0 de 33.
- **`observer_seq_copy=1`** (`observer_seq_copy.cpp:593-660`, `observer_seq_copy_logic.h`):
  - el sniffer lee el S>C **del propio cliente del anfitrión**;
  - `Ledger::on_host_packet` guarda los bytes del último 2661 y 2981 de cada secuencia; el 2810 la cierra;
  - el `MoveFieldAck` del anfitrión vacía el libro.
- En cada **viaje observador** del usuario 2 (su 2174 a un campo cuyo dueño no es él), el `Copier`:
  - convierte la instantánea en copias pendientes;
  - cada copia espera a que **todos** sus actores de tipo 4/5 se hayan anunciado al usuario 2 (2005) y a que se abra
    la compuerta (su 2322 o 30 s);
  - después envía el último 2981 y el 2661 byte a byte.
- En vivo: todo 2661 nuevo del anfitrión durante el viaje se convierte en copia (`on_host_prepared`, `:644-647`). Se
  copian también el 2981 que no llegó de forma nativa y el 2810.
- Además, **el 2322 del anfitrión una vez por viaje** (`observer_seq_copy_2322=1`). En el invitado va a X (+0x231 = 2)
  y `guest_seq_play` lo pone a 3: así se reproduce todo lo precargado.
- Filtro C>S: los 2767/2591/2436/2138/2173/2276/2105/2888 del usuario 2 sobre secuencias del anfitrión se descartan
  (`filter_req`, `:806`). El invitado nunca conduce en el servidor una escena del anfitrión.
- **`observer_stage_sync=1`:** solo mide (ganchos en los serializadores `0x2A4DEF0`, `0x2A4E4C0`, `0x2A4F2D0`,
  `0x2999D80` y la entrada `0x2BC84E2`). No envía nada: el modo 2 o superior se rechaza.
- **`observer_scene_copy=1`:** emite un 2810 extra **solo para escenas propias del usuario 2** (ESC «ver objeto»).
  Nunca toca las del anfitrión (`observer_scene_copy.h:1-25`).
- **`observer_sched_copy=1`:** copia las escenas de horario (2335/2958/2909). Son de NPC y no llevan rol de jugador.
- Kits del anfitrión (`2inst-host`, `s43-activo-host`, `s43-probe-host`, `p8-s38-host`): `observer_stage_sync=1`,
  `observer_scene_copy=1`, `observer_seq_copy=1`, `_only=` (vacío), `_2322=1`, `_2100=0`, `observer_sched_copy=1`,
  `observer_milestone=1`, `_send=1`, `shop_probe=1`, `funcnpc_twin=probe`, `user2_stages=1`, `user2_npc_control=1`.
  `scene_level_share` vale 1 en s43-activo y probe en s43-probe. `user2_shop_route` y `user2_stages_elements` no
  aparecen (valen 0 y `schedule`).

### 3.2 Dónde se podría decidir en el anfitrión no copiar las escenas personales

**Punto exacto:** `osq::Copier::add_pending` (`observer_seq_copy_logic.h:848-880`), que ya aplica
`only_.matches(r.seq, bound)` → `filtered_`. Lo llaman la instantánea y `on_host_prepared` (en vivo). El `SeqRec` tiene:
- el 2661 completo;
- `actor` y `owner`;
- `bindings` con `node` (el **rol**, p. ej. `0x20000002`), `actor` y `type`;
- `required`.

El recurso está en el byte 27 del payload. Una política `observer_seq_copy_personal = 0|probe|1` encajaría aquí con
coste mínimo:
- es lógica pura y testeable con los paquetes capturados;
- se juzga en el hilo del sniffer, bajo `g_mu`.

**Ventajas:**
- La escena personal no llega nunca al invitado: no se crea en X, no entra en la tabla de `guest_seq_play`, el 3 no la
  reproduce y su 2981/2524 nativo cae en M sin efecto.
- Es lo más parecido a lo nativo: el servidor real nunca se la manda a un tercero.
- No toca ningún gancho del invitado.
- Cuando el invitado es un PC remoto (Shadow), ahorra red.

**Inconvenientes y consecuencias:**
- El anfitrión **no tiene** el nombre de la escena en ese hilo: solo bytes. El nombre está en la memoria del cliente del
  anfitrión, en su M (`[[inst+0x48]+0x1B8]`), pero leerlo desde el hilo del sniffer sería una lectura cruzada de
  hilos (SEH, sin el cerrojo de M) [E]. **Si la clase se basa solo en datos del 2661** (roles, tipos, recurso,
  dueño), el anfitrión es el sitio más limpio. Si necesita stageinfo o flags del recurso, el invitado (tras crear en
  X) o una tabla aprendida son más fáciles.
- Requiere actualizar a la vez el ini del anfitrión y el del invitado. Hoy la decisión de enrutado está toda en el
  invitado.
- Con `observer_seq_copy_2322=1`, el 2322 copiado sigue poniendo X a 2 y `guest_seq_play` lo pone a 3: lo precargado
  que **sí** se copió se reproduce igual. No cambia.
- Los NPC que enlaza una escena personal (Matthias `0x20000006`, el Game Mechanic `0x20000004`) no tendrán esa escena
  en el invitado. Es lo nativo para un tercero [E], pero hay que comprobar que no se quedan en A-pose:
  `seq_actor_restore` cubre el login [E].
- Una escena «de tienda» del anfitrión (`cd_seq_funcnpc_*` enlazada a A0100001) **no** se debe filtrar sin más. Las
  entradas SetInteraction que ve el invitado salen de ahí (§4).

**Servidor de verdad (código del juego, en el proceso del anfitrión):** el serializador `0x2A4DEF0` (hook de
`observer_stage`, RCX actor, R8 play data, R9 enlaces) y el secuenciador del servidor (`rsi` de `0x2879440`, dueño en
`[rsi+0x98]`). Allí podría leerse la info de la etapa del servidor. Pero ese hook está en modo medida y no hay que
cambiar lo que el servidor envía al anfitrión. El copiador del mod ya es el punto de decisión.

---

## 4. Interacción con la rama de tenderos (`opcion-d-tenderos`)

Rama `opcion-d-tenderos` en `a294fac`, con merge-base `ef5a6b5`. Hay dos commits de código:
- `24456e4`: cuarentena de la gemela y parser del 3000.
- `026386f`: `guest_shop_order`.

`git merge-tree --write-tree opcion-d-2inst opcion-d-tenderos` termina limpio, sin conflictos textuales. La rama de
escenas todavía no tiene código propio.

### 4.1 Qué toca la rama de tenderos (líneas en `opcion-d-tenderos`)

- **Invitado: `guest_shop_order = 0|probe|1`** (por defecto 0; `config.cpp:291`, `config.h:491`).
  - `deliver_all` se reestructura en una lambda `deliver` (`guest.cpp:1127-1182`).
    - `guest_shop_order_feed` (`:1166`) se ejecuta **antes** de `guest_seq_route_gate` (`:1136`).
    - `guest_shop_order_poll` (`:1177`) se ejecuta al final del lote.
  - Alcance (`guest_shop_order_logic.h:380-383`): 2661 **propios** del invitado (actor @1 = dueño @5 = clave
    propia) con un hash de comercio @27 (`A7132561` herrero, `978345CA` carnicería).
  - Retiene el 2661 hasta que llegan los 2005 de sus enlaces de tipo 4/5 (`:414-424`).
  - Retiene sus 2981 y 2524 en una FIFO (`:493-509`).
  - El 2524 espera a que la escena esté cargada en **M**. Para eso usa `gsr::Scene2524` de
    `guest_seq_route_logic.h` (`guest_shop_order.cpp:11`, `:94`) e `isl::find_stage`.
- **Anfitrión:**
  - `funcnpc_twin` en cuarentena (`funcnpc_twin_logic.h:26`: `1` pasa a ser `probe`).
  - Lista positiva de comercio en U-f (`funcnpc_trade_policy.h:12-17`, `SkipNonTrade`).
  - En U2 (`user2_shops.cpp`): origen lejos/gemela, el 2743 cierra la ventana, parser del 3000 y endurecimiento W9.
  - En U-1: descarga asentada y `ArmRequest`/`DrainRequest`.
  - `funcnpc_twin_logic.h:133-145` **duplica el formato del 2661**: 91 B más 11 B por enlace.
- **No toca** `guest_seq_route*`, `guest_seq_play*`, `guest_x_veto*`, `guest_x_interaction*`, `observer_seq_copy*`
  ni `observer_stage*`. Kit vigente del invitado (`cdmp.s41p2-p2-guest.ini`): `guest_seq_route=1`,
  `deny=8CD771B1`, `guest_seq_play=1`, `_live=0`, `guest_x_stage_veto=1`, `guest_x_ui_veto=1`,
  `allow=SetInteraction`, `guest_x_interaction_stage=probe`, `guest_seq_route_2524=probe`. Kit del anfitrión:
  `funcnpc_twin=0`, `user2_shop_route=1`, `observer_seq_copy=1`.

### 4.2 Restricciones para el diseño de escenas personales

1. **Las escenas propias del invitado no se tocan.** Las tiendas lejanas (U2 caso A) viven en su M: el 2661 tiene
   actor = dueño = clave propia. La clasificación tiene que mirar **primero el dueño**: si es la clave propia, la
   escena es propia, se entrega a M de forma nativa y no se veta ni se retiene.
   - `gsr::decide` ya devuelve `OwnCharacter` en el paso 6.
   - `gsr::gate` ya entrega lo propio.
2. **[V] El rol `0x20000002` NO distingue una escena «personal».** Las escenas de tienda enlazan `0x20000002` al
   personaje **dueño**, con tipo 5:
   - anfitrión `293`: `0x20000002→A0100001`, `0x20000008→B0100570` (`s41p2-datos/capturas/extractos-log.txt:12`);
   - invitado `142`/`143`: `0x20000002→A0100066`.

   Lo mismo pasa con la ESC «ver objeto» (`0x20000002→A010031F`). Una regla «enlaza al anfitrión como jugador ⇒
   personal» clasificaría como personales las tiendas del anfitrión.

   Tampoco distinguen la tienda del anfitrión de la del invitado:
   - el hash @27;
   - la stageinfo (`41934310` en las de herrero);
   - el NPC enlazado;
   - su controlador (`90100000`).

   **Consecuencia para S44: el criterio nativo tiene que ser otro dato** (§3.2 del encargo: flags o categoría del
   recurso o del stageinfo, el filtro nativo «owner's missions / type 5 / AABB» de `0x9F8FCE0`, cámara o control). Si
   no, la clase «tienda» debería quedar como hoy hasta que la defina la pieza de tenderos.
3. **La escena de tienda del anfitrión en X no hace de tienda para el invitado** [V]:
   - con C1 en probe, las entradas que SetInteraction añade desde X fallan en `0x2126920`;
   - en P1 el NPC de `293` siguió sin binding `+318` (`local_res8` inválido en `0x9ED401`);
   - en la S14, con C1=1, el invitado se quedó atascado.

   Ninguna pieza de tenderos depende de que ese 2661 se enrute a X. La «visibilidad actual» de los tenderos sale de los
   2005 (`observer_hold`, `docked_gimmicks`), así que no hay que tocar 2005, 2516 ni su orden.
   - `tiendas-u2.md` §4.3.5 **prefiere** que, en el diseño cercano (gemela en M), X no aporte entradas: C1 en 0/probe
     y, opcionalmente, vetar en X los SetInteraction de funcnpc.
   - Por eso la clase **«tienda del anfitrión»** necesita su propia política (`como hoy` | `ruta sin SetInteraction` |
     `no crear`) separada de «personal».
   - El efecto sobre la pose del tendero si deja de reproducirse está **sin medir**.
4. **Orden de entrega:**
   - Cualquier compuerta o clasificación nueva en `deliver_all` va **dentro de `deliver`**, después de `gso`, o deja
     pasar lo propio.
   - No hay que montar una segunda cola de retención en paralelo. Si hiciera falta una cola para los 2661 del
     anfitrión (el mismo rechazo `0xA9CE87` puede darse en X), lo correcto es generalizar `gso` y tener una sola
     entrega con la misma FIFO.
   - No hay que reescribir 2174/2643/2756/2810/2516 antes de `gso`: los usa como fronteras.
   - **Diseño recomendado:** la política en el gancho P2661 (`0xC0612A`), no en `deliver_all`. Así no choca con la
     lambda (riesgo de fusión **alto** en `guest.cpp:1124-1182`).
5. **Acoplamiento de compilación:** `guest_shop_order.cpp` usa `gsr::Scene2524`
   (`inst/stage/data/branches/count/capacity/branch/entry`). No se deben renombrar ni cambiar esos campos. Si se crea
   `scene_class_logic.h` con un parser del 2661, conviene que `funcnpc_twin_logic.h` y `osq::parse_prepare` acaben
   usándolo, pero sin romperlos ahora.
6. **En el anfitrión:**
   - U2 depende de que R1 de `observer_seq_copy` filtre solo los Req del invitado sobre escenas **del anfitrión**.
   - Si la política se pone en `Copier::add_pending`, no debe copiar ni filtrar las escenas propias del invitado: hoy
     no entran en el libro, porque el libro es del S>C del cliente del anfitrión.
   - No se deben tocar los ganchos de ShopProbe P `0x288AAEF` ni S `0x2126A1F`.
7. **Qué no se puede romper:**
   - el 2661 propio se entrega una sola vez;
   - un 2810 de una escena entregada siempre pasa;
   - C1 nunca en 1 junto a U2;
   - `guest_x_ui_veto_allow=SetInteraction` se mantiene mientras no exista otra política para tiendas;
   - `guest_x_ui_veto` no puede extenderse a M.

### 4.3 Conflictos de fusión previsibles

| Archivo | Riesgo | Motivo |
|---|---|---|
| `guest.cpp` `deliver_all` `:1124-1182` | Alto | Reestructurado en una lambda |
| `guest_diag_worker.cpp:42-44` | Medio | Línea nueva junto a `guest_seq_route_tick` |
| `config.cpp:288-300`, `config.h:487-494` | Medio | Claves nuevas junto a `guest_x_interaction_stage` |
| `session2.cpp:1192-1196`, `:1345-1350`; `server.cpp:642` | Medio / bajo | |
| `CMakeLists.txt` | Bajo | Archivos y tests nuevos |
| `guest_seq_route*`, `guest_seq_play*`, `guest_x_veto*`, `observer_seq_copy*` | Ninguno textual | Solo acoplamiento semántico (puntos 2, 5 y 6) |

Tests nuevos o cambiados en tenderos:
- `optd_guest_shop_order_tests.cpp`, que incluye los dos 2661 reales de P2 (`142`/`143`, tipo 5): sirven de fixture de
  «propia».
- `optd_funcnpc_twin_tests.cpp`.
- `optd_shop_troc_tests.cpp`.
- Cambios en `optd_user2_shops_tests.cpp`.

---

## 5. Dónde encajaría una clasificación «personal / mundo / tienda» con política `0|probe|1`, y tests

### 5.1 Clasificación (lógica pura)

- **Archivo nuevo** (recomendado): `scene_class_logic.h`, espacio `cdmp::session2::scls`. Lo comparten:
  - el invitado (`guest_seq_route`);
  - el anfitrión (`observer_seq_copy`);
  - y mañana tenderos (U2 / `funcnpc_twin`).
- Entrada: un `Prepare` ya parseado (`osq::parse_prepare`, `observer_seq_copy_logic.h:218-232`) más hechos opcionales
  del cliente: tipo `[[a+0x88]+1]` de los actores, nombre o stageinfo si se lee tras crear, y `own_key` / `owner`.
- Salida, por ejemplo: `enum class SceneClass { OwnPlayer, HostPersonal, HostShop, World, Unknown }` y el motivo.
- **Reutilizar `classify_2524`** (`guest_seq_route_logic.h:1081-1130`): su cascada (`NoNpc` = personal,
  `Function` = tienda, `World`) es la semilla. Hay que generalizarla al 2661 y sustituir el criterio de nombre
  (`is_function_scene`) por el dato nativo que encuentre el §3.2 del encargo.
- **Ojo:** `classify_2524` llama WORLD a cualquier escena con un NPC de tipo 3, y Matthias en una revancha lo sería [E].
  Hace falta el rol `0x20000002` y algún dato más.
- Datos del 2661 que ya se leen y sirven [V]:
  - **rol** (`node`) de cada enlace (`0x20000002` = jugador; `0x20000004` / `0x20000006` = otros roles). **No basta
    por sí solo**: las escenas de tienda y la ESC «ver objeto» también enlazan `0x20000002` al dueño (§4.2.2);
  - **tipo** del enlace (4/5 = requeridos);
  - **dueño** (play data +0) frente al **actor** (+1). `HostSeen::NotOwn` ya nunca se copia;
  - recurso @27 y etapa @19;
  - número de enlaces y cuántos son NPC 0xB.

  El kind del play data (`01 00`, `ff ff ff 7f 01`, `0d`…) es igual en la cinemática y en las escenas de tienda o mesa
  (`logic.h:190-192`). No sirve para distinguir.

### 5.2 Política (dónde actuar)

| Sitio | Clave propuesta | Qué haría con 1 | Qué haría en probe | Riesgo |
|---|---|---|---|---|
| **Invitado, P2661 `0xC0612A`** (`guest_seq_route_hook`, `guest_seq_route.cpp:1271-1500`) | `guest_seq_route_personal = 0\|probe\|1` | `HostPersonal` → nuevo `Verdict::Personal`, tratado como `Denied` en `gsr::decide` (después de `AlreadyX`, junto a `denied`, `logic.h:385`) y `gsr::refuse` (`:443`): el 2661 no se crea; su 2981/2524 hereda la marca por `g_seq_res.note(..., denied)` | Una línea `WOULD personal` y contadores por clase, como `g_2524_class` | Mínimo: reutiliza el camino `Denied`, ya validado en vivo (A/B S43, 15/15). `0` = como hoy |
| Invitado, variante «sin el rol de jugador» | `=role` (otro valor) | Reescribir en el paquete crudo el actor del enlace `0x20000002` antes de `0xA9CA40`. **No recomendado**: el tipo 4/5 lo hace requerido (`0xA9CE81` la descarta) y no se sabe qué hace la escena sin jugador [H] | — | Alto, sin medir |
| Invitado, tras crear `0xC061C1` (`guest_stage_diag`) | — | Leer el nombre o stageinfo de la instancia nueva en X y eliminarla con el camino nativo de 2810 | — | Más invasivo: crea y destruye. Solo si el dato nativo está en el stageinfo y no en el 2661 |
| **Anfitrión, `Copier::add_pending`** (`observer_seq_copy_logic.h:848`) | `observer_seq_copy_personal = 0\|probe\|1` | No copiar `HostPersonal` (contador `filtered_personal_`) | Línea y contador `would filter` | Bajo. Necesita la clase con datos del 2661. Las instantáneas existentes no cambian |
| `guest_seq_play` | — | **No.** Hace tick de X entero; no puede filtrar por escena | — | — |
| `guest_x_ui_veto` | — | **No** para la pose: no tiene ningún manejador de animación. Puede quedar como segunda capa (por escena en vez de por gestor) | — | — |

**Recomendación técnica [E]:**
- Clasificar con lógica pura común.
- Aplicar la política primero en el invitado, en P2661: es un solo sitio, el camino de rechazo ya existe y se valida con
  dos copias y un solo ini.
- Dejar el punto del anfitrión como segunda fase. Ahorra red con Shadow y es «más nativo».
- `guest_seq_route_deny` se queda como lista de **emergencia**, ya no como solución. La clase `Denied` se reporta
  aparte.
- La clase `HostShop` se mantiene como hoy (Route + SetInteraction permitido) hasta que la pieza de tenderos la use.

### 5.3 Tests existentes que cubren estas piezas [V]

| Test | Cubre |
|---|---|
| `tests/optd_seq_copy_tests.cpp` (1683 líneas) | `test_guest_route` (:456): `gsr::decide` (39 usos), `gsr::refuse` (15), `gsr::gate` (10), `prepare_ids` / `DenyList` / `SeqResources` con el 2661 real de `0x213` / `0x8CD771B1` (:509-524). También el `Ledger` y el `Copier` del anfitrión (`test_ledger`, `test_trip_snapshot_and_live`, `test_filter` con `OnlyFilter`, :136-455) y el horario y el enlace (`test_guest_sched_route`, `test_guest_sched_bind`, `test_guest_sched_kinds_and_teardown`, :1232-1683) |
| `tests/optd_seq_bodyanim_tests.cpp` (305) | `classify_2524` (12), `mode_2524` (16), `is_function_scene` (8), `decide` con `need_world` (12) |
| `tests/optd_seq_play_tests.cpp` (453) | `gsp::plan` y `effective_mode` (116 usos `gsp::`) |
| `tests/optd_x_veto_tests.cpp` (474) | `gxv::judge`, `fn_mode`, `parse_allow` / `fn_act` (SetInteraction) |
| `tests/optd_shop_c1_tests.cpp` (496) | `gxi::decide` / `check_x` (C1), `shop_probe_logic`, `stage_lookup_logic` |
| `tests/optd_guest_stage_tests.cpp` (331) | `guest_stage_diag_logic` (`classify_route`) y `observer_stage_logic` (`required_actors`) |
| `tests/optd_seq_restore_tests.cpp` (385) | `seq_actor_restore` |
| `tests/optd_scene_acks_tests.cpp` | `observer_scene_copy` (2661 real del ESC «ver objeto», `0x20000002→A010031F`, :57) |
| `tests/optd_rejoin_tests.cpp` | partes de `gsr` en la reconexión |
| `tests/optd_user2_shops_tests.cpp` | `user2_shops_logic` (U2-a y U2-b) |

**Huecos:**
- Ningún test tiene el 2661 de `0x9586FFA4`, ni de `cd_seq_funcnpc_*` del anfitrión, ni de una escena de mundo con NPC
  (Grover `0x1AB`, Dahlia `0x1AD`) para una tabla de verdad.
- Están en los logs y capturas de `backups\p8-2inst-s43-activo` y `p8-2inst-pose-anfitrion` (`S/s43-datos/pose-informe.md:75`, `:90`).
- La tabla de verdad del diseño debería ir en un `tests/optd_scene_class_tests.cpp` nuevo con esos bytes.
