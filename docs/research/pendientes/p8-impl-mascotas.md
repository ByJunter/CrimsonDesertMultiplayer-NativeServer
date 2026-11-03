# P8: mascotas. Implementación de `child_settle_ms`, `remote_interp_owned_cap_ms` y `attach_follow`

10 de octubre de 2026, desde `opcion-d` `4cfbea3`. Build del juego **1.0.0.2976**, base `0x140000000`, direcciones
en RVA. Diagnóstico: `p8-s8-mascotas.md` (§1, §2 y §4). No se lanzó el juego ni se desplegó nada.

- **[V]** = comprobado (capstone sobre el exe, tests).
- **[E]** = deducción fuerte, falta medirla en vivo.
- **[H]** = hipótesis.

## 0. Qué hay

| Clave `[session2]` | Dónde | Valores (defecto) | Qué hace |
|---|---|---|---|
| `child_settle_ms` | anfitrión (`create_session=1`) | ms (`0`: nada) | Un hijo de usuario no se anuncia a otro usuario en el mismo `NotifyAppear` que el personaje de su dueño (ni menos de N ms después): se retiene y se re-anuncia de forma nativa N ms después del personaje |
| `remote_interp_owned_cap_ms` | anfitrión **e** invitado (necesita `remote_interp`) | ms (`0`: como antes) | Tope del retardo de interpolación de los actores con dueño (mascota, caballo, compañero) y sin contar sus huecos mientras van enganchados a un personaje |
| `attach_follow` | anfitrión (`create_session=1`) | `0` / `probe` / `1` (`0`) | Generaliza `horse_follow`: la mascota en el hombro sigue a su personaje con Move sintéticos (solo anfitrión → invitado y solo con `1`); en `probe` mide la hipótesis del Path local |

Además, en el invitado, un contador del error de creación `exe+0x8BDF7C` en la línea de cada minuto.

Inis (`docs/research/ini/`):
- `cdmp.p8-s7-host.ini` y `cdmp.p8-noche-local.ini`: `child_settle_ms=1500`, `remote_interp_owned_cap_ms=900`,
  `attach_follow=probe`.
- `cdmp.p8-s7-shadow.ini`: `remote_interp_owned_cap_ms=900`, `attach_follow=probe` (en el invitado no instala nada).

## 1. `child_settle_ms` (síntoma 2: la mascota del anfitrión nunca existe en el invitado)

### 1.1 El fallo [V, `p8-s8-mascotas.md` §1]
- La mascota del anfitrión `0xB0100002` (tipo 6, byte de restauración 1) llega al invitado en el **mismo** 2005 que
  `0xA0100001`, justo detrás.
- `CreateOwnedActor 0x8AF580` falla: `0x8BDC3E call 0x8AF580` → `0x8BDC44 cmp byte [rbp-0x48],0` → rama
  `0x8BDC4A` (cadena del error) → `0x8BDF77 call 0x35D8D0` (vuelve a `0x8BDF7C`, el `from` del log). Verificado
  otra vez con capstone.
- Causa probable: condición **c5** de `data/c1-actor-rechazado.md` §1.4 (`[pj+0x5E] & 0x10`: el personaje
  principal del dueño todavía cargando).
- Nadie la vuelve a anunciar: D-53 solo retiene con byte 0, y `character_hold` no la toca.

### 1.2 Qué hace
En el mismo desvío de `NotifyAppear 0x2B000C0` de `child_hold.cpp` (sin hooks nuevos):

1. **Candidato** (`settle_candidate`, lecturas SEH por clave en el `ServerActorManager`):
   - clave `0xB…`;
   - dueño `[actor+0xA0]` un usuario validado (la misma regla que D-53: el puntero del anfitrión, el del usuario 2,
     o, a prueba de fallos, otro `ServerUserActor`);
   - tipo `[[actor+0x88]+1]` ∈ {4, 5, 6};
   - **cualquier byte de restauración**, siempre que D-53 no lo retenga ya (veredicto `Restored`, o `ChildrenOff`
     con `child_hold=0`);
   - el personaje principal del dueño `[dueño+0xD0]`, con clave `0xA…` y cuyo propio `+0xA0` es ese dueño.
2. **Se retiene de una sesión destinataria que no es la del dueño** si:
   - el personaje principal va en **esta misma llamada**; o
   - se anunció a **esa** sesión hace menos de N ms; o
   - `character_hold` todavía lo retiene de esa sesión (se mira su tabla bajo el candado del reparto).
3. **El reparto es el de D-53** (`plan_split`): el resto de claves a todos, en su orden; el hijo al dueño en un 2005
   propio y en el acto; la pareja (hijo, observador) en una tabla propia de 32 (`SettleHoldTable`), con el sitio
   **reservado** en el reparto (nunca se retiene sin entrada; si no cabe, va nativa y se avisa).
4. **`observer_hold_acks`**: si el observador es la sesión 2 viva, la clave se registra **antes** de las llamadas
   nativas del reparto (como I5). Sus 2404/2991/2697 esperan a su propio 2005 y salen detrás, en orden. Si la
   retención se suelta, se descartan.
5. **Registro de anuncios de personajes** (`cst::AnnounceLog`, 64 ranuras, sin candados): `(clave 0xA, sesión) →
   momento`. Lo escriben el desvío (las claves `0xA` que de verdad salieron a cada sesión: todas en una llamada sin
   reparto; las de `pass` y las de cada grupo en una con reparto) y las liberaciones de `character_hold`.

### 1.3 Liberación: cuándo, en qué hilo y con qué función
- **Función:** la misma que D-53 y `character_hold`: `NotifyAppear([observador], [hijo])` por el trampolín (el
  2005 se serializa de nuevo, ahora con byte 1). Es la única función del juego que se llama.
- **Cuándo** (`cst::release_step`): N ms después del **último anuncio del personaje a ese observador** (si no se
  conoce, desde la retención). Nunca mientras `character_hold` siga reteniendo al personaje.
- **Hilos** (los de `char_pass`, con `notify_thread_rule` igual que hoy):
  - tras `StartActionReq::vf2` (`after_start_action`), en el hilo del manejador;
  - en la cola de cada `NotifyAppear` nativo (como mucho cada 50 ms);
  - el respaldo del hilo receptor (`child_hold_server_tick`, cada 250 ms), con la regla del respaldo de D-53: solo
    en el último hilo que llamó a `NotifyAppear` (regla 0) o en uno nativo (regla 1). Con `notify_thread_rule=0`,
    además, una liberación debida que ya esperó `hold::kDeferWarnMs` (2 s) sin ningún `StartActionReq` ni
    `NotifyAppear` (un mundo quieto) la hace el propio respaldo (§9, punto 6).
  - En la práctica los dos primeros se dan sin parar mientras alguien se mueve: los NPC del anfitrión y las
    mascotas mandan `StartActionReq` continuamente.
- **D-53** (hijo con byte 0): su liberación (byte 1) espera la misma regla (`d53_waits_settle`), para que un hijo
  que actúa justo tras aparecer su personaje no vuelva a caer en el mismo 2005.
- **Personaje aún no anunciado** (el hijo llega antes, p. ej. su sector entra primero): si el usuario observador ve
  el sector del personaje, el hijo lo espera (y N ms más); si no lo ve (el caballo dejado lejos), no espera.
- **Se suelta sin publicar** solo si es seguro que el observador no puede verlo ahora: otra época, despawn (el
  actor ya no está en el gestor), sesión sin usuario o con otro, fuera de alcance (el motor lo vuelve a anunciar al
  volver), usuario 2 saliendo o ido. Si el motor anuncia el hijo él mismo a esa sesión, la entrada se olvida.
- **Nativo en vez de soltar:** otro actor u otro dueño tras la clave (en alcance), y **cualquier lectura que no se
  aclara en `N + 20 s`** (`cst::max_wait_ms`; `NotifyAppear` busca la clave él mismo: un actor que ya no existe
  simplemente no se serializa). Línea `WRN`.
- **Carrera reanuncio/re-retención:** tras cada liberación se vuelve a tomar el candado y se quita cualquier
  entrada del mismo (hijo, observador) grabada mientras corría esa llamada nativa: su 2005 es el recién enviado.

### 1.4 Justificación de 1500 ms [V]
Se crea a +89 ms (cliente del anfitrión), +414 ms (login del propio invitado) y +3 s (prueba C del caballo); falla a
+0 ms en 7 de 7 sesiones.

### 1.5 Líneas
- `Session2 child settle ON (child_settle_ms=1500): …`
- `Session2 child settle: held 0xB0100002 (type 6, owner host 0x90100000, restore 1) from session 0x… (user 2
  0x…): its owner's character 0xA0100001 goes in the same NotifyAppear; released 1500 ms after …`
- `Session2 child settle: released 0xB0100002 … after 15xx ms, settled (after StartActionReq|after NotifyAppear;
  settled after its character)`
- `Session2 child settle: D-53's release of 0x… waits: …`
- En `child hold …` de cada minuto: `| child settle (1500 ms): held … released … (settled …, max wait …) | dropped
  … | D-53 releases that waited … | still held … | character announcements noted …`.

## 2. `remote_interp_owned_cap_ms` (síntoma 1a: la mascota del invitado va 1,1–1,8 s detrás)

- **Dueño en el cliente [V]:** `0x8BDA00 mov rax,[rbp-0x70]; mov rax,[rax+0xA0]; test; mov eax,[rax+0x60]` y
  `0x8BDBF7` igual (capstone): `[actor+0xA0]` es el dueño y `[dueño+0x60]` su clave.
- En el hook, por controlador (`Slot::owned`, se reinicia si cambia la clave tras el mismo `mc`): «con dueño»
  = `[actor+0xA0]` es un puntero **y** `[[actor+0xA0]+0x60]` es una clave de usuario (clase `0x9`), como la compara
  el propio cliente y como `owner_who` en el servidor. «Sí» se queda; «no» se vuelve a leer cada segundo (un
  dueño puesto después de la primera muestra); «no se pudo leer», en cada llamada (`rip::classify_owner`,
  `rip::owned_recheck_due`).
- **Tope:** `target = clamp(max(3 huecos) + 150, lo, C)` con `C = remote_interp_owned_cap_ms` (acotado a
  [200, 1800]) solo para los actores con dueño; el resto sigue con 1800. Nunca por debajo de `lo` = max(200, valor
  del juego).
- **Huecos mientras va enganchado:** no se registran si el actor tiene dueño y el **registro de enganches**
  (`attach_follow.h`) dice que su última `StartAction` tiene padre `0xA…`. El búfer del cliente no dice si hay
  padre (el Path se guarda en un sitio que no conocemos), así que se usa la red:
  - **invitado:** `guest_deliver` (hilo de red del mod) mira cada 2991/2697 que llega del anfitrión;
  - **anfitrión:** `server.cpp inject` mira cada 2223/2556 del usuario 2 (su mascota llega al cliente del
    anfitrión por la sesión 1, nativa).
  - Engancha: `StartAction` de una clave `0xB` con padre `0xA` (y sin el hash de bajar). Suelta: `StartAction`
    con otro padre o sin padre, o un Move de la clave con padre −1. Caduca a los 15 s.
- **Lo que se espera** (`sim2.py` de `p8-s8-mascotas.md` §4.2): `D` p50/p90 = 900/900 ms, frente a 1330/1800.
- **Resumen de cada minuto:** `| owned actors (cap 900 ms): controllers N, gaps not recorded while attached to a
  parent M (attached now K), owner unreadable U` y `(owned)` junto a la clave en `largest delays`.
- **Riesgo:** caballo sin jinete y mercenarios también tienen dueño y bajan a ≤ 900 (más micro-paradas, ~+2,6
  puntos según la simulación).

## 3. `attach_follow` (síntoma 1b: «flotar» en el hombro)

### 3.1 Formatos [V, `mirror-ack-formats.md` §1–§2]
El bloque B de `StartAction` empieza por el MoveData (= MoveActor `+0x04…+0xDD`) en Req `+0x8E` / Ack `+0x8F`: un
offset `X` de Move es Req `X+0x8A` / Ack `X+0x8B`.

| Campo | Move | Req 2223 | Ack 2991 |
|---|---|---|---|
| pos de mundo | `+0x04` | `+0x8E` | `+0x8F` |
| quat | `+0x10` | `+0x9A` | `+0x9B` |
| Path local (pos 12, quat 16) | `+0x4C` | `+0xD6` | **`+0xD7`** |
| padre u64 | `+0x8C` | **`+0x116`** | **`+0x117`** |
| Path mundo | `+0x94` | `+0x11E` | `+0x11F` |
| ts | `+0xD5` | `+0x15F` | **`+0x160`** |
| objetivo u32 | — | `+0x16C` | `+0x16D` |

Hash de la acción **[V, captura del anfitrión de la sesión 8]**: u32 en `+0x08` en Req y Ack (la Req lo repite en
`+0x04`). Ver §9 para lo que lleva cada uno. Bajar del hombro: `0x8276C0C5`.

### 3.2 Qué hace
- **Enganche** (por mascota, tabla de 8 bajo un *try-lock*: si está ocupada, se cuenta y se salta el paquete;
  nunca bloquea un hilo del juego):
  - una `StartAction` de una clave `0xB` con padre `0xA…`;
  - y el hijo es **de un usuario**: su dueño, leído por clave en el `ServerActorManager` (`hold_actor_owner`),
    es el anfitrión (sentido de salida) o el usuario 2 (sentido de entrada). Esto descarta las presas agarradas y
    otros enganches. Si no se puede leer (p. ej. sin los hooks de `child_hold`), no se sigue nada.
- **Fin:** otra `StartAction` con otro padre, sin padre o con el hash de bajar; un Move del hijo con padre −1; 15 s
  sin `StartAction` enganchada; nueva época.
- **Move sintético** (con `1`, solo anfitrión → invitado): por cada **3** Move del personaje (~20 Hz) que salen a
  la sesión 2 en `take_frame`, uno del hijo con
  - `pos = K.pos + R(K.quat)·local.pos`, `quat = K.quat ⊗ local.quat` (`local` = Path local de su última
    `StartAction`);
  - y la receta de `horse_follow`: `+0x20 = 0`, Path local = la pos/quat de mundo con escala 1, padre −1,
    `+0x94…+0xD4 = 0`, ts y cola los del personaje.
  - Va detrás, en la misma cola, con `push_outgoing_extra` (solo con la cola por debajo de la mitad).
  - Solo si el Path local parece un desplazamiento (cuaternión unitario y |pos| ≤ 5 m). Si no, la mascota cuenta
    como `Path local not an offset` y no se fabrica nada.
- **Sentido invitado → anfitrión: solo medida, en todos los modos.** Se construye la Req 2556 que haría falta
  (historial a cero) para saber que se podría, pero **no se envía**:
  - cambiaría la posición de la mascota en el servidor a ~20 Hz;
  - no hay ninguna medida de lo que haría el servidor con una Req que el cliente del invitado no mandó (su
    historial, su ts frente a las Req reales de la mascota);
  - no he podido demostrar que sea seguro.

### 3.3 `probe`: validar la hipótesis del Path local [H]
Para cada `StartAction` enganchada (24 líneas por sentido y cuatro histogramas en mm: ≤50, ≤100, ≤200, ≤500,
≤1000, ≤2000, más):
- **`parent x local`**: `|K + R(K)·local − pos de la acción|`, con `K` el último Move del personaje (la línea da
  también `ts acción − ts K`). Es la hipótesis.
- **`Path world x local`**: la otra lectura posible (el Path de mundo `+0x11F` como base).
- **`previous local applied later`**: el local de la acción anterior aplicado al `K` de ahora frente a la
  posición de la nueva. Es lo que vería el invitado con `1`; el criterio de `p8-s8-mascotas.md` §4.3 es < 0,2 m.
- **`previous relative pose applied later`**: lo mismo con la pose relativa medida (`K⁻¹ ⊗ mascota`) en vez del
  Path local. Si la hipótesis falla pero esta sale bien, se puede fabricar con la pose relativa sin depender de
  `+0xD7`.

Las líneas dicen el Path local en mm, su módulo, `w` del cuaternión y la posición de la acción en el marco del
personaje.

### 3.4 Lo que se ve en `describe` (cada minuto con `probe=1`)
`AttachFollow [probe]: host's -> user 2: StartActions … (attached …, still attached …, Path local not an offset …,
detached …, not a user's child …; owner unreadable …) | parent Moves … -> child Moves … would be made … || user 2's
-> host (measured only): … || registry: attached …`.

## 4. Contador del error de creación (invitado)
- En el hook de errores del juego (`game_errors.cpp`, `on_error`), sin candado: si el `from` es `exe+0x8BDF7C`,
  se cuenta y se guarda el último valor con forma `0x9…_B…` (dueño arriba, actor abajo).
- La línea normal del error se deduplica a las 10.
- Sale en `Session2 guest (minute N): … | owned actor creation failed N (last 0x90100000B0100002 …)`; nada si es 0.

## 5. Ficheros
- **Nuevos:** `src/mod/session2/child_settle_logic.h`, `attach_follow_logic.h`, `attach_follow.{h,cpp}`,
  `tests/optd_pets_tests.cpp`.
- **Cambiados:**
  - `child_hold.{h,cpp}`: settle, `settle_candidate`, `hold_actor_owner`, D-53 espera, costuras de prueba;
  - `remote_interp{.h,.cpp,_logic.h}`: tope por dueño y huecos enganchados;
  - `server.cpp`: tres llamadas a `attach_follow`;
  - `session2.cpp`: configuración, *tick* y `describe`;
  - `guest.cpp`: alimentación del registro y el contador;
  - `game_errors.{h,cpp}`;
  - `config.{h,cpp}`, `mod.cpp`, `CMakeLists.txt`;
  - los tres inis.
- **No se tocan** `log.cpp`, `hud.cpp`, `remote_motion*`, `guest_seq_route*` ni `observer_seq_copy*`.

## 6. Pruebas [V]
`.\scripts\build.ps1`: compila sin avisos; **34/34** tests tras la revisión (sobre `opcion-d` `281d30a`, que ya
trae 33; `optd_pets` es el nuevo). `optd_pets` cubre (lo añadido en la revisión, en §9):
- **`child_settle`, reglas puras:** tipos, necesidad, registro de anuncios, pasos de liberación (asentado, tope de
  espera, soltar, nativo, ilegible).
- **`child_settle` de punta a punta**, con las costuras de `child_hold`:
  - el 2005 de la sesión 8 (`[gimmick][A0100001][B0100002]` solo al usuario 2): sale sin la mascota, y la mascota
    sale sola 1500 ms después del personaje (`StartActionReq` y cola de `NotifyAppear`; el respaldo no libera);
  - los dos destinatarios (el anfitrión recibe su mascota en el acto);
  - personaje anunciado 300 ms antes (se retiene) o 1500 ms antes (pasa);
  - apagado, la propia sesión del dueño, otro tipo, un NPC: sin cambios;
  - fuera de alcance, usuario 2 saliendo, el motor la anuncia él mismo;
  - la espera de D-53;
  - detrás de `character_hold`;
  - `observer_hold_acks`: los 2404/2991/2697 de la mascota salen tras su 2005, en orden; si se suelta, se
    descartan.
- **`remote_interp`:** tope, `delay_cap`, regla de huecos, `Slot::owned` y su reinicio; una mascota con huecos de
  hombro converge a 900.
- **`attach_follow`:** el formato de `StartAction` (Req y Ack); cuaterniones; el rastreador:
  - enganche y medidas exactas cuando la hipótesis se cumple;
  - 1 Move de cada 3;
  - fin por Move propio, por acción sin padre, por hash de bajar o por caducidad;
  - no es de un usuario;
  - Path local que no es un desplazamiento.
- **Sintéticos:** el 2697 y la 2556 byte a byte.
- **El registro.**

No prueban los hooks en el juego, la ABI ni lo que hace el cliente con los sintéticos.

## 7. Qué mirar en la próxima sesión
1. **GL:** ninguna línea `Actor 생성에 실패 … B0100002`, y sin `owned actor creation failed` en la línea de cada
   minuto.
   - **HL:** `child settle: held 0xB0100002 … goes in the same NotifyAppear` y `released 0xB0100002 … after
     ~1500 ms, settled`.
   - **GC:** un 2005 `[U][B0100002]` ≥ 1,5 s después del de `A0100001`, con sus 2697/2991 detrás.
   - Si el fallo sigue, mirar si sale a +1,5 s (el invitado todavía cargando: subir `child_settle_ms`) o por otra
     condición de `0x8AF580`.
2. **HL:** `largest delays: 0xB0100334 (owned) ≤ 900`. Con la mascota en el hombro, `gaps not recorded while
   attached` > 0.
3. **HL, `AttachFollow`:**
   - histogramas `parent x local` y `previous local applied later`, casi todo en ≤ 200 mm: hipótesis válida y
     `attach_follow=1` en la siguiente;
   - si no, mirar `Path world x local` y `previous relative pose applied later`, y las 24 líneas con los valores;
   - mismas medidas en el sentido del invitado (`user 2's -> host`), para decidir si vale la pena la Req
     sintética.
4. **Visual:**
   - la mascota del anfitrión aparece en el invitado (~1,5 s después que él);
   - la del invitado en el anfitrión, ≤ 0,9 s detrás.
   - En el hombro sigue «flotando» en el anfitrión: esta versión no arregla ese sentido.

## 8. Riesgos y dudas
- **La mascota del anfitrión aparece ~1,5 s más tarde** en el invitado.
  - Si el invitado tarda más de 1,5 s en tener su personaje listo (`[pj+0x5E] & 0x10`), el fallo se repite y se
    ve en GL y en el contador.
  - Red de seguridad posible, no hecha: que el invitado pida «re-anuncia K» por el túnel al ver el error.
- **Retención con varios destinatarios:** se retiene de todas las sesiones que no son del dueño aunque solo una
  la necesite. Con dos usuarios es la misma.
- **`observer_hold_acks`** solo existe para la sesión 2 como observador. En el otro sentido, los paquetes de la
  mascota del invitado llegan al anfitrión por la sesión 1 antes que su 2005 (como hoy con D-53).
- **`hold_actor_owner`** lee el mapa del `ServerActorManager` sin su candado (dos intentos, lecturas SEH, como
  D-53), desde `take_frame` y desde el hilo de red. Sin los hooks de `child_hold` no sigue nada.
- **`attach_follow=1`** depende de la hipótesis `+0xD7` = desplazamiento respecto al personaje. La captura de la
  sesión 8 la confirma fuera de línea (§9.2), pero falta verlo en vivo: se queda en `probe`.
- **El registro de enganches del invitado:** los Move sintéticos de `attach_follow=1` (padre −1) sueltan el
  enganche en el registro del invitado. No importa: con ellos los huecos ya son pequeños.

## 9. Correcciones tras la revisión (10 de octubre, sobre `opcion-d` `281d30a`)

### 9.1 Los diez puntos

| # | Hallazgo | ¿Cierto? | Arreglo |
|---|---|---|---|
| 1 | Si no se lee el dueño en una `StartAction` nueva con padre, se queda el enganche viejo | Sí | Se quita el enganche que tuviera ese hijo (o se aplaza la baja si el rastreador está ocupado) |
| 2 | Con el *try-lock* ocupado se pierden altas y bajas: pegado hasta 15 s | Sí (bajas) | Candado acotado (64 intentos con pausa, nunca espera de verdad) y **bajas aplazadas** sin candados (`afl::PendingDetach`, 16 claves), aplicadas primero por el siguiente que tome el rastreador. Una alta perdida la repite la siguiente acción (~4 s) |
| 3 | El 2697 sintético se salta `observer_hold_acks` y `child_settle`/D-53 | Sí | No se fabrica mientras `child_hold` (D-53 o `child_settle`) retiene al hijo de la sesión 2 o `observer_hold_acks` guarda sus paquetes (`child_hold_withheld`, `observer_hold_acks_holding`; con el candado ocupado cuenta como retenido) |
| 4 | Un hijo anunciado antes de que su personaje se anuncie nunca a esa sesión sale en el acto | Sí | Si el usuario observador ve el sector del personaje (va a llegar), el hijo lo espera y N ms más; si no lo ve, no espera |
| 5 | Lecturas ilegibles → soltar sin publicar tras la espera máxima | Sí | Solo se suelta con despawn seguro, sesión que ya no es, fuera de alcance u otra época. Cualquier ilegible pasado `N + 20 s` se anuncia de forma nativa |
| 6 | Sin `StartActionReq` ni `NotifyAppear`, un hijo debido no sale nunca | Sí, en un mundo quieto | El respaldo libera como el de D-53 (último hilo de `NotifyAppear` / hilo nativo). Con `notify_thread_rule=0`, además, lo debido que ya esperó 2 s. **Riesgo:** ese último recurso llama a `NotifyAppear` desde el hilo receptor (`OnRecvPacket`), justo lo que la regla 1 evita. Con regla 1 no se hace |
| 7 | La liberación fuera del candado puede chocar con una re-retención: doble `NotifyAppear` | Sí (ventana de la llamada nativa) | Tras cada liberación se quita bajo el candado cualquier entrada nueva del mismo (hijo, observador). El mismo patrón existe en D-53 y `character_hold` (sin tocar) |
| 8 | «Sin dueño» guardado para siempre | Sí | «No» se relee cada segundo; «ilegible», siempre |
| 9 | Dueño = cualquier puntero | Sí | Exige `[[actor+0xA0]+0x60]` de clase `0x9` |
| 10 | `attach_follow` solo ve los paquetes de la ruta Pass | Sí | El `push` de `observer_hold_acks` (2005 + retenidos, derrames) también lo alimenta, en modo de solo observar: no fabrica bajo ese candado |

Tests nuevos en `optd_pets`:
- **Respaldo:** el último hilo de `NotifyAppear`, último recurso a los 2 s con regla 0, nunca con regla 1 fuera de
  un hilo nativo.
- **Carrera de re-retención.**
- **Hijo antes que su personaje:** en alcance espera; lejos sale.
- **Reglas puras:** ilegible → nativo; personaje en alcance sin anunciar.
- **Dueño:** clasificación y relectura.
- **Hash y bajas:** el hash en `+0x08` y en `+0x04`; bajas aplazadas.

### 9.2 Preguntas, con la captura del anfitrión de la sesión 8 (HC)
Script de un solo uso sobre `cdmp_packets_20261010-103235.bin` (no se conserva). Mascotas `B0100002` (Req C>S) y
`B0100334` (Ack S>C); personajes `A0100001` y `A0100333`.

**Hashes [V], u32 en `+0x08`** (no en `+0x04`, como se suponía; la Req lo repite en `+0x04`). Padre que lleva cada uno:

| Hash | Qué es | Padre |
|---|---|---|
| `0xA497102C` | subir | −1 |
| `0xBC77AF45` | subirse al hombro | el personaje, con el Path local **todavía en coordenadas de mundo** (22 de 22) |
| `0x94D333D6` | quieta en el hombro, cada ~4,17 s | el personaje, Path local = desplazamiento (83 de 83) |
| `0x8276C0C5` | bajar | **el personaje todavía**, desplazamiento en 27 de 28 |

Consecuencias:
- Bajar solo se detecta por su hash. Con el `+0x04` de la primera versión no se habría visto: corregido.
- En la acción de subirse no se fabrica nada (`Path local not an offset`). El seguimiento empieza con la primera
  `0x94D333D6`.

**¿Manda un hijo enganchado algún 2697 con padre −1?** Casi nunca, y solo al bajar:
- `B0100334`: 0 Move mientras va enganchado.
- `B0100002`: 1, a 478,768 s, en el mismo milisegundo que su `StartAction` con padre −1 (la de después de bajar).

Es decir, un Move con padre −1 marca el final. Soltar el enganche con él es correcto, no es ruido.

**¿Tiene sentido `aux +0x20…+0x23`?**
- En los Move de las mascotas es siempre 0: 378/378 de `B0100002` y 244/244 de `B0100334`.
- En los del personaje del anfitrión vale 0 en 3373 de 3386; en los otros 13 lleva una clave `0xB0101043`/
  `0xB0101044` (¿objeto de interacción?).

Poner 0 en el sintético es lo que manda la propia mascota.

**¿Ordena el cliente los Move de un actor por `ts`?**
- No los reordena: el set de posición **descarta** una muestra con `ts ≤` la última del búfer
  (`setInterpolatePositionData`, `rip::observe_set` → `older`). La acción en sí se aplica igual.
- En HC llegan fuera de orden pocas:
  - el personaje del invitado, 44 de 8120;
  - el Req del anfitrión, 6 de 3386;
  - las mascotas, 0.
- **Para el sintético:** lleva el `ts` del Move del personaje (mismo reloj de cliente que las acciones de la
  mascota). Si una acción real llega con un `ts` menor que el último sintético, su posición se descarta del búfer
  (la acción vale). Es inocuo: el sintético ya la tiene en el hombro.

**La hipótesis del Path local, fuera de línea [V captura, E en vivo]:**
- 133 acciones enganchadas de `B0100334`, contra el último Move de `A0100333`.
- `|K + R(K)·local − pos|` ≤ 5 cm en 99, ≤ 20 cm en 109 de las 110 con desplazamiento. Las 23 restantes llevan el
  local en mundo (22 `0xBC77AF45` y 1 de bajar) y la pieza las rechaza.
- `Path mundo × local`: ≤ 5 cm en 111.
- La pose relativa de una acción aplicada al `K` de la siguiente: ≤ 20 cm en 116 de 133 (el movimiento propio de la
  mascota en el hombro).
- `attach_follow=1` debería servir. Se mantiene `probe` hasta verlo en vivo.
