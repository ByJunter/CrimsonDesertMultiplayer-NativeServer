# S34: contención (paso 2b) y relevo de NPC (paso 1b)

Análisis de solo lectura. Build **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`). Código: worktree
`C:\Emulador CD-npc-s33-fix`, rama `opcion-d-npc-s33-fix`, HEAD `d835806`. Horas en **reloj del anfitrión** salvo
que se diga otra cosa. Para alinear los dos PC se usa `qpc` (µs), no el reloj de pared: offset invitado−anfitrión
= mínimo de (alta en el túnel del invitado − `[2005 encolado al invitado]` del anfitrión) sobre las mismas claves
(2b: −68241,540 s, 371 pares; 1b: −68241,565 s, 230 pares). Ese mínimo incluye la latencia mínima de ida (unos
12 ms con un RTT de 23–25 ms), así que las latencias de abajo son el **exceso** sobre ese mínimo. Los relojes de
pared de los dos PC difieren unos 0,75 s, además de la hora de diferencia.

Scripts (Python 3.14 con `-I -B`): `build/research/s34/scripts/` (`p2b.py`, `p2b_more.py`, `fam.py`, `p1b.py`,
`p1b_scene.py`, `swap.py`, `life.py`).

## 0. Resumen

1. **Paso 2b: las retiradas fueron todas injustas, pero no por llegar tarde.** De las 110 unidades `[sin progreso]`,
   **109 ya habían aplicado una acción** (`[acción OnServer] aplicada=1`) antes del plazo, con una mediana de 1,3 s
   desde el 2005. La única restante es de después del cierre del invitado. Causa: un **fallo del criterio de
   progreso**. Las 359 líneas `[acción OnServer]` dicen `emisor=0x00000000 … aplicada=1 progreso=0`: el emisor se
   lee mal, así que `applied_to()` nunca acredita progreso y **todas** las unidades del invitado vencerían a los 5 s.
   Antes del cierre se retiraron 60 unidades (unos 65 actores); el invitado las vio desaparecer con motivo 25. Los
   nodos que fallaron dos veces se vetaron (5 nodos, 70 vetos): esos NPC ya no vuelven.
2. **El túnel no es el cuello de botella.** En la ráfaga del armado (199 NPC en 0,5 s), el exceso de latencia del
   2005 fue de 51 ms de mediana y 96 ms como máximo. Steam marcaba `queue 0 ms`, `pending 0 B` y `backlog 0 B`. Lo
   que tarda es **el arranque en el cliente del invitado** (desde el 2005 hasta la acción aplicada: mediana 1,3 s,
   p99 2,9 s; en 1b, máximo 3,3 s). Hay además un caso determinista de unos 10 s: **una raíz con hijos tipo 6
   controlados por el anfitrión** (5 de 5 en 2b) no arranca hasta que le llegan los hijos. El observer hold los
   retiene 10 s. Este mismo bloqueo fue el que disparó el aborto antiguo en 1b (`0xB01003AD`).
3. **Paso 1b, salida del anfitrión: 21,2 s con la ciudad vacía**. Se reparten en 10,8 s de histéresis de armado,
   10,05 s esperando el 2043 encolado con un periodo entero, 0,14 s de creación de stages y unos 0,8 s de 2005. Al
   llegar, los 174 NPC nacieron con el **anfitrión de controlador**, porque U-e había abortado a las 18:52:36. Entraron
   en el bucle de motivo 42: viven unos 2,5 s de mediana y se reponen, por eso parecía que «solo volvían algunos».
4. **Paso 1b, llegada del anfitrión: el relevo funcionó**. Hubo un `[DRAIN held]`, el vaciado en la preparación del
   anfitrión desde `exe+0x289604E` con 0 Trocs del invitado en curso, 0 duplicados y 0 restos. El primer NPC del
   anfitrión llegó 0,17 s después de la última baja de user 2, y el grueso (223) 1,5 s después. Solo hay un WRN
   espurio de carrera (§3).

## 1. Paso 2b (`npc_activation=1`): ¿fallaban de verdad las unidades retiradas?

Armado `[ARMED]` 19:06:38.633 (qpc 69348.054779). El primer 2005 encolado fue a +9,0 s y el cierre del invitado a
+32,4 s (`exe+0x1831598`, actor `0xB0100578`: tipo 7, controlado por el anfitrión, sin padre, fuera de la
contención; lo estudia otro agente).

> **Corrección S35 (AUDITORIA.md §1 y §6, DMP `180710_6364_1`):** el actor cuya capa se lee en el cierre C es
> **`0xB01004BF`** (R14 actor, RDX su CharacterControl, CC+8 vuelve al actor; acción y reproducción cero, restore 0).
> `0xB0100578` sólo es el dueño del `ClientHierarchyActorComponent` (RSI, vtable `0x5578BB8`) que consulta por
> jerarquía a su padre `0xB01004BF` (`[hier+0x1C]`). La línea GuestS3 que llamaba a RSI «CharacterControl de
> B0100578» estaba equivocada; desde S35 la captura nombra ese componente y la clave de su padre
> (`guest_s3.cpp`, `describe_cc`). Ver `docs/research/s25/s35-fase-a.md`.

### 1.1 El progreso nunca se registra (bug)

- Host 2b: 359 de 359 `[acción OnServer]` con `emisor=0x00000000 resultado_interno=0 rama=0 aplicada=1 progreso=0`.
  En 1b (`probe`), lo mismo: 223 de 223. Por eso el plan seco de 1b da el motivo 0 («vencido») y **nunca** el 7
  («ya hubo progreso»).
- `npc_action_probe.cpp:41-42`: `user=ptr(session+off::kSession_User)`, `sender=read<uint32_t>(user+0x60)`. Ahí se
  supone que el cuarto argumento de `0x2A78CA0` es una `NwVirtualAsyncSession`. Según el decompilado
  (`D:/RE/CrimsonDesert_2976/exports/decomp/0x2a78ca0.c:41-44`), `param_4` es un objeto con vtable, y la clave del
  controlador se obtiene por su `vf+0x170`. No es `[+0x90]+0x60`, y la lectura da 0.
- `npc_activation_logic.h:106-108` `applied_to()` exige `sender == a.controller`. Lo llaman `Table::progress`
  (`:456-461`) y `user2_stages_npc_action_result` (`user2_stages.cpp:3219`). Así, `applied_us` sigue a 0 y
  `no_progress()` (`:87-92`) se cumple a los 5 s para todo actor asignado. La comprobación de la línea 1913 de
  `user2_stages.cpp`, que cancelaría la retirada si hubo progreso, tampoco actúa.
- La comprobación del emisor es **redundante**: `0x2A78CA0` sale por `LAB_142a79096` si `*vf170(param_4) !=
  actor+0x90` antes de llegar a la rama `+0x281` y a OnServer. H1b (`0x2A79060`) solo se alcanza después de esa
  comprobación nativa.

### 1.2 Las 110 `[sin progreso]` frente a lo que ocurrió de verdad

| Clase (respecto a su `[sin progreso]`) | Unidades |
|---|---:|
| Acción aplicada **antes** del plazo, Req del anfitrión antes y primera Req del invitado antes | **109** |
| El invitado nunca recibió el 2005 (`0xB0100592`, encolado a +46,7 s, después del cierre) | 1 |

Ejemplo `0xB0100391`: 2005 → recibido por el invitado +0,04 s → primera Req en el invitado +1,53 → en el anfitrión
+1,67 → acción aplicada +1,71 → `[sin progreso]` +5,04 → retirada. En el invitado: `baja motivo 25`.

Antes del cierre (qpc < 69380,44): 71 diagnósticos, **60 unidades retiradas** (líneas raíz `asignado=1`) y 65
líneas de miembro. El invitado registró 60 `baja motivo 25` de claves retiradas. Son NPC que funcionaban y
desaparecieron a la vista. La stage los repone con otra clave (69 nodos nacieron más de una vez), se vuelven a
retirar y, al segundo fallo, `nodo bloqueado` (5) y vetos (70 en el resumen de 19:07:43). Esos puestos quedan
vacíos para esa generación. El ritmo de unas 2 retiradas por segundo (un intento cada 250 ms o más, a la salida de
un Troc del invitado) es lo único que impidió que en 32 s se retirara todo.

### 1.3 Latencia del túnel durante la ráfaga

| Encolado (respecto al armado) | n | Exceso de la mediana | p90 | Máx |
|---|---:|---:|---:|---:|
| +8…+10 s (ráfaga) | 199 | 51 ms | 90 ms | 96 ms |
| +14…+34 s | 172 | 5–17 ms | 6–29 ms | 30 ms |

Las líneas `Net`/`Troc tunnel` del anfitrión entre 19:06:37 y 19:07:12 dan `queue 0 ms`, `pending 0 B`,
`backlog 0–277 B` y un tope de unos 190 KB/s de salida. En 1b la ráfaga de 180 NPC dio un exceso de mediana de
67 ms y 113 ms como máximo. **El plazo de 5 s desde el encolado equivale en la práctica a 5 s desde la recepción
(±0,1 s).**

### 1.4 Distribución de arranque (para elegir el criterio)

| Desde el 2005 encolado hasta… | n | p10 | p50 | p90 | p99 | máx | > 5 s |
|---|---:|---:|---:|---:|---:|---:|---:|
| 2b: primera Req del invitado (exceso) | 365 | 0,17 | 1,20 | 2,63 | 2,68 | 10,32 | 2 |
| 2b: primera Req en el anfitrión | 365 | 0,23 | 1,24 | 2,73 | 2,86 | 10,36 | 2 |
| 2b: acción aplicada (OnServer) | 359 | 0,24 | 1,29 | 2,80 | 2,88 | 10,38 | 2 |
| 1b: acción aplicada | 223 | 0,35 | 2,31 | 3,07 | 3,34 | 3,34 | 0 |

- Entre la primera Req y la acción aplicada pasan de 30 a 70 ms: la primera Req del controlador ya es una buena
  señal de vida.
- En el invitado, las primeras Req de la ráfaga salen en dos olas: a +10,0…+10,7 s (unos 150) y a +11,6 s (46).
  Es decir, de 1,0 a 2,7 s después de recibirlas: el cliente reparte el arranque del cerebro en varios fotogramas.
- **Los dos casos de más de 5 s son el bloqueo familiar** (`0xB010048E`, `0xB01004BE`):
  - la raíz es tipo 3, de user 2, con 2 hijos tipo 6 cuyo controlador es el **anfitrión** `0x90100000`;
  - el anfitrión no recibe la raíz (retenida, `withheld from the host`), así que su cliente nunca arranca los hijos;
  - el observer hold retiene a los hijos hacia la sesión 2 (`still held from session 0x5A4D20C00E0 … released with
    restore 0 at 10000 ms`) y su 2005 sale 10,095 s después del de la raíz;
  - el invitado manda la primera Req de la raíz **0,21 s después** de recibir los hijos (1134,474 → 1134,680 y
    1136,969 → 1137,186 en qpc del invitado).

  `fam.py`: hijos tipo 6 con controlador anfitrión, 5 de 5 retrasados entre 10,09 y 10,10 s. Hijos tipo 3 y tipo 7
  de user 2: menos de 1 ms. **[V]** para estos dos; **[E]** como regla general. En S33 esto había quedado como
  hipótesis (el pastor).
- 1b, el disparador del `[CONTROL ABORT]` de 18:52:36 (`unaccepted after 5 s 1`) es `0xB01003AD`: tipo 3, con su
  hijo tipo 6 `0xB01003AE` controlado por el anfitrión. El hijo llegó al invitado 20 s más tarde, ya como huérfano
  (`Session2 family order: ORPHAN queued for user 2`). Es el mismo bloqueo.

**Conclusión.** El plazo de 5 s **no** fue la causa: sin el bug, ninguna unidad normal lo habría superado (máximo
3,34 s en 582 arranques). Pero el margen es escaso (unos 1,7 s sobre el máximo con ráfagas de unos 200) y las
familias con tipo 6 lo superan siempre. El criterio propuesto está en el §4.

## 2. Paso 1b: la salida del anfitrión de Hernand

| Hora (anfitrión) | Hecho | Fuente |
|---|---|---|
| 18:55:51.646 | Carga del anfitrión (L): su gestor descarga sus 56 stages de Hernand (motivo 19, `0x287D4C0`) | host `[delete]/[close] (host) reason 19` |
| 18:55:51.65–.77 | Invitado: 222 NPC de Hernand con baja de motivo 19 y 2126 ×55. **Hernand se queda vacío** (solo quedan NPC sin stage de horario) | alineado `swap.py`, `p1b_scene.py` |
| ~18:55:52.47 | Primera muestra a ≥ 10 celdas (61) | se deduce del `[would ARM]` 10 s después |
| 18:55:58.645 | El gestor del anfitrión crea 23 stages en su destino | `[create] (host)` |
| 18:56:02.473 | `[would ARM]` + `[ARM requested]`, a 61 celdas | host |
| 18:56:02.477 | `[ARMED]` a la salida de la Req 2556: cadena 2043 **muerta**, se encola un 2043 con `EnqueueTimer delay 100` (unos 10 s) | host |
| 18:56:12.529 | Llega el 2043 al gestor de user 2 (estado 2, reprograma) | `[timer]` |
| 18:56:12.660–.684 | `[lists built]`, evaluación y `[create]` de **55 stages (user 2)** | host |
| 18:56:12.88–13.71 | Invitado: 174 NPC tipo 3 (2005), **todos con controlador del anfitrión** (`0x90100000`) | `swap.py` |
| 18:56:15 → 18:56:44 | Bucle: 483 altas tipo 3 y 306 bajas de motivo 42; vida mediana 2,5 s (p10 2,2, p90 2,9); entre 236 y 259 vivos | `life.py`, `p1b_scene.py` |
| 18:56:44.0 | 2001 del invitado (`goodbye while ARMED`), `DRAINED now` en su logout | host |

**Desglose del hueco de 21,2 s** (de la última baja de Hernand al primer 2005 de user 2):

| Componente | s |
|---|---:|
| Histéresis de armado (≥ 10 celdas durante 10 s; muestreo de 1 s) | 10,8 |
| Espera del 2043 encolado en el armado (`delay 100`, un periodo) | 10,05 |
| Evaluación y creación de 55 stages | 0,14 |
| 2045 → primer 2005 recibido | 0,2 (el grueso, 1,0) |

En 2b ocurre lo mismo con otra forma. La cadena estaba «viva» y no se encoló nada, así que el primer tick llegó
7,7 s después del armado (`[ARMED]` 69348,055 → 2043 69355,745 → `[create]` 69356,092 → primer 2005 69356,948).

**Por qué solo volvieron algunos.** En `probe` el viejo vigilante de U-e sigue activo. A las 18:52:36 hubo un
`[CONTROL ABORT]` por `0xB01003AD`, la familia bloqueada del §1.4, y desde entonces `user2_npc_control` deja de
asignar. Los 55 stages nuevos nacen con el anfitrión de controlador; su IA corre en el cliente del anfitrión, a unos
4 km, y pide `logout` (motivo 42, el bucle de la S31). Cada NPC aparece, vive unos 2,5 s, desaparece y la stage lo
repone con otra clave. Aunque el número de vivos ronda los 250, en pantalla parecen pocos y con huecos. No hay
ninguna limitación de ritmo en la creación de stages: las 55 se crean en 24 ms y el grueso de 2005 llega en 0,8 s.

**Qué lo haría rápido y completo** (detalle en el §4):

- armar sin la histéresis cuando la distancia salta de ≤ 7 a ≥ 10 celdas en una sola muestra (un viaje rápido),
  más un 2043 **de un solo disparo** al armar (`+0xB = 0`, `delay 1` = 100 ms). Así el hueco pasaría de 21 s a
  unos 1–2 s, como en la llegada;
- con U-e a 1 y los dos arreglos del §1 (progreso y familias tipo 6), los NPC repuestos nacen controlados por el
  invitado y no entran en el bucle;
- la opción estructural es no vaciar las stages de user 2 cuando llega el anfitrión (U-f2 ya evita duplicados:
  `schedules not created (alive in the other's …)`). Daría cero huecos en ambos sentidos, pero cambia el contrato
  de U-1.

## 3. Paso 1b: la llegada del anfitrión (relevo)

- La carga del anfitrión empieza hacia las 18:54:48.2. A las 18:54:53.419 hay `[would DRAIN]` a 1 celda, seguido de
  `[DRAIN held] … the host's load started 5.2 s ago` (la revisión A2 funciona).
- A las 18:54:57.372, `[drain at host prepare]` en `0x287EF60` (gestor del anfitrión en estado 1), llamado desde
  `exe+0x289604E`, con `user 2's Trocs in flight 0` y 52 stages de user 2. A las 18:54:57.589, `DRAINED now`
  (estado 2 → 1).
- Desde las 18:54:57.736, 40 `[create] (host)`, todos con `alive in the other player's … : no`. Hay 0 `YES` en todo
  el log, `not created (U-f2) 0/0`, `drains held 1 (expired 0)`, y ni `[control leftover]` ni `[control cleanup]`
  después.
- En el invitado (qpc alineado):

  | qpc del anfitrión | Evento en el invitado |
  |---|---|
  | 68647,001–68647,029 | 209 bajas de motivo 19 (NPC de las stages de user 2) |
  | 68647,190 | primer 2335 del anfitrión |
  | 68647,201 | primer 2005 |
  | 68647,723–68648,488 | grueso de 223 NPC |

  **Hueco visible: 0,17 s hasta el primer NPC y 1,5 s hasta el grueso.** No hay duplicados y no queda nada de
  user 2.
- Único detalle: a las 18:54:57.423 hay un WRN `[DRAIN requested] stages left in its manager after a drain:
  schedules 52`. Su qpc 68646,845 cae **dentro** del vaciado en curso, entre la decisión (68646,794) y `DRAINED`
  (68647,011). El hilo del módulo vio `!armed` con las stages aún sin descargar. No produjo un segundo `DRAINED`.
  Es una carrera inocua, pero es ruido y pide un vaciado innecesario (`user2_stages.cpp:2299-2305`; debería
  ignorarse mientras `g_p_draining`).

## 4. Arreglos por orden de impacto

| # | Arreglo | Dónde | Cómo verificarlo |
|---|---|---|---|
| 1 | **Acreditar el progreso.** H1b/rama `+0x281` dentro de `0x2A78CA0` solo se alcanzan si el controlador coincide (decompilado `:41-44`), así que basta con no comparar el emisor: `applied_to(a, birth, normal_applied)`, o bien emisor = `[actor+0x90]` leído a la entrada. Registrar una vez el RVA de la vtable de `param_4` para saber qué es. Sin esto, `npc_activation=1` retira a todos los NPC del invitado y veta sus nodos. | `npc_action_probe.cpp:41-42,55`; `npc_activation_logic.h:106-108,456-461`; `user2_stages.cpp:3209-3227` | Test puro en `optd_npc_activation`: acción aplicada con emisor 0 ⇒ progreso. Sin juego: volver a pasar 1b/2b con el mismo criterio (aquí, 582 de 582 progresarían). Shadow: `progreso=1` en casi todas las `[acción OnServer]`, `[sin progreso]` ≈ 0 salvo familias tipo 6, ninguna `baja motivo 25` en el invitado de NPC que se movían |
| 2 | **Familias con hijos tipo 6 del anfitrión** (bloqueo de 10 s; causa del aborto de 1b y del pastor de S33). Preferido: que U-e asigne a user 2 los hijos tipo 6 cuya raíz (`+0xAC`) es una clave asignada en este armado (hoy se rechaza por `Ctl::Type`/`HasParent`). Primero en modo prueba, con `[would control child]`. Alternativa: que el observer hold no retenga hacia user 2 al hijo de una raíz controlada por user 2. Es más arriesgada: el invitado recibiría actores del anfitrión sin acción, la clase del cierre en `0x1831598`, que `guest_remote_update_guard` no cubre. | `user2_stages_logic.h:709-728` (`ctl_verdict`); `user2_stages.cpp:1136-1196` (`npc_control_impl`); alternativa en `child_hold.cpp:548-640` (`npc_release_step`) | Un PC: descriptor, sitio y `+0xAC` de los hijos en `[would control child]`. Shadow: 2005 de los hijos a menos de 1 ms del de la raíz (`fam.py`), primera Req de esas raíces en menos de 3 s, 0 `still held from session <sesión 2>` para hijos de raíces asignadas |
| 3 | **Re-armado rápido.** (a) En `u2s::step`, si la muestra anterior era ≤ `off_cells` y la actual ≥ `on_cells` (salto de viaje rápido), armar sin esperar 10 s, o con 1–2 s. (b) En `arm_locked`, encolar siempre además un 2043 de un solo disparo (`p[11]=0`, `delay 1` = 100 ms), sea cual sea el estado de la cadena. Así la evaluación es inmediata y no nace otra cadena (el tick en estado 2 con `r8b=0` evalúa y no reprograma, §8.1). | `user2_stages_logic.h:220-245` (`step`), `:559-569` (`timer_2043_payload`); `user2_stages.cpp:1476-1486` (`queue_chain_2043`), `:1553-1559` (`arm_locked`) | Un PC con `cdmp_peer` (el anfitrión viaja lejos del peer): `[ARMED]` − baja de stages del anfitrión < 2 s y `[create] (user 2)` − `[ARMED]` < 0,5 s. En el resumen, el ritmo de 2043 sin cadenas nuevas (`count_chains`, `[host cadence]`). Shadow: hueco en el invitado < 2 s (`swap.py`) |
| 4 | **Criterio de contención.** Retirar solo si: (i) no hay acción aplicada **ni Req del controlador para esa clave** 8 s después del encolado (en vez de 5; máximo medido 3,34 s); (ii) el invitado está vivo (alguna Req de user 2 en los últimos 2 s; si no, no se retira nada); (iii) ningún miembro de la familia sigue retenido hacia user 2. Mientras exista el bloqueo, el plazo cuenta desde el último 2005 de la familia. | `npc_activation_logic.h:24` (`kNoProgressUs`), `:87-92` (`no_progress`); `user2_stages.cpp:3576-3588` (`first_req_us` ya existe), `:1796-1820` | Tests puros nuevos (Req ⇒ no se retira; invitado callado ⇒ no se retira). Shadow: en una sesión de 5 min, `[sin progreso]` solo en unidades sin ninguna Req y 0 `nodo bloqueado` con un invitado activo |
| 5 | **Estructural: conservar las stages de user 2 mientras el anfitrión está de visita.** No vaciar en la preparación y dejar que U-f2 impida que el anfitrión las duplique. Cero huecos en ambos sentidos; el anfitrión vería NPC conducidos por el invitado. Cambia la regla de U-1 (vaciar a ≤ 7 celdas) y necesita decisión de diseño (misiones y escenas del anfitrión). | `user2_stages_logic.h` `prep_drain_verdict` y `hold_distance_drain`; U-f2 | Primero en `probe` («would keep»). Shadow: 0 huecos y 0 `YES (a duplicate)` |
| 6 | Ruido: ignorar la comprobación `stages left … after a drain` mientras `g_p_draining`, o durante 1 s después de un `DRAINED`. | `user2_stages.cpp:2299-2305` | Log de llegada sin el WRN |

Prioridad práctica: el 1 es imprescindible para cualquier paso 2. El 2 elimina la única causa medida de arranques
de más de 5 s y el disparador del aborto antiguo. El 3 resuelve el hueco de 21 s del §2, que con el 1 y el 2 se
vería como una ciudad vacía y no como un bucle. Sin el 1, no hay que repetir el paso 2 con Shadow.
