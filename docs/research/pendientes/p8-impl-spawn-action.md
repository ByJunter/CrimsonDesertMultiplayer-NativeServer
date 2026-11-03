# P8 · El 2723 de la cohorte de instantánea y `observer_load_defer`

11 de octubre de 2026, rama `p8-spawn-action` (sobre `opcion-d` `7bbb5b6`). Build del juego **1.0.0.2976**,
direcciones en **RVA** (base `0x140000000`). No se lanzó el juego ni se desplegó nada.

**[V]** = comprobado (capturas y logs de las sesiones 8 y 13, desensamblado del PE, código, pruebas). **[E]** =
interpretación. **[H]** = pendiente de prueba.

Parte de `p8-s13-npc-familias.md` (la ciudad partida en «instantánea» y «vivos»; los vivos reciben 2723 y la
instantánea casi nunca). Scripts de esta sesión: scratchpad `s14sa\` (`cap.py`, `a1.py`–`a10.py`, Python `-I`).

## 0. Resumen

1. **El 2723 no es la causa [V].** En el cliente del invitado un `StartSpawnActionAck` no hace nada:
   - su consumidor `0x48B120` sale en `0x48B199` si `vf188(5)` del actor es falso, antes de tocar el actor;
   - `vf188(5)` = «lo controla este cliente» (`byte [actor+0xC8] != 0`, `0x4FA620`), y los NPC del invitado los
     controla el anfitrión;
   - medido en la sesión 8 (sonda S3 del invitado): **2017 de 2017** 2723 con `vf188(5)` falso, 0 aplicados.

   El 2723 es solo una **marca** de la cohorte: lo reciben los NPC que aparecen con el invitado ya dentro.
2. **La copia pedida no se puede construir ni serviría [V]:**
   - el cliente del anfitrión no recibe 2723 de sus NPC: es su controlador, y el relevo es `ToNearExceptOneControl`.
     En toda la sesión 13 recibió **2** (y 0 de los 538 NPC de instantánea);
   - el payload lleva dos veces el reloj del servidor en ms (`+0x15F`, `+0x2CA`): repetirlo minutos después
     mandaría un arranque viejo;
   - de forma nativa, quien entra en un campo poblado tampoco lo recibe. Lo equivalente va en el 2005: la
     restauración 1 y la acción. `0x8646D0` la aplica, y la instantánea la llevaba (322 de 323 NPC con bloque CC).
3. **Lo que sí distingue a la instantánea [V]: se crea en el invitado durante su carga.**
   - El servidor anuncia el campo al usuario 2 dentro de `MoveFieldCompleteCore`, antes de su 2144: **531
     registros de NPC entre su 2549 y su 2144**, 78 más antes del 2762.
   - En la carga propia del anfitrión, el 2144 sale 12 ms después del 2549 y sus 464 NPC llegan después.
   - En el invitado, los 181 NPC de la instantánea con nodo de escena **no encontraron al dueño** (`0xA0100001`)
     al desempaquetarse (t 45,0–46,6). Los 483 vivos sí (t ≥ 103).
4. **Arreglo implementado: `[session2] observer_load_defer = 0 | probe | 1`** (anfitrión, encima de `observer_hold`).
   - Mientras el usuario 2 carga (desde su 2549 hasta que el servidor le escribe el 2762), un NPC del
     anfitrión que se le anuncie **se le retiene aunque tenga restauración 1**.
   - Se le anuncia de forma nativa (`NotifyAppear([sesión 2], [K])`, un 2005 fresco) en cuanto sale su 2762.
   - Así la instantánea sigue el camino de los vivos.
   - Tope de 60 s. `probe` solo cuenta.
   - `cdmp.p8-s7-host.ini` y `cdmp.p8-noche-local.ini` lo llevan a **1**.
5. **Build y pruebas [V]:** `.\scripts\build.ps1` compila sin avisos y pasan **42 de 42**. Incluye
   `test_hold_load_defer`, nuevo en `optd_s3`.

## 1. Qué hace el 2723 en el cliente [V, estático]

`troc_messages_2976.csv`: `StartSpawnActionAck,2723`, manejador `0xB57BC0`. Su pareja `StartActionAck,2991` tiene
el manejador `0xB57720`.

| Paso | 2723 `0xB57BC0` | 2991 `0xB57720` |
|---|---|---|
| Deserializa | `u32 clave`, 2 × bloque de 0x50 (`0x2442610`), acción (`0x2442BF0`) | `u32 clave` (rechaza ciertas claves con `0x8B53C0`), 2 × 0x50, **`u8 bandera`**, acción |
| Busca el actor | `0x8A67E0`; si no hay, nada | igual |
| Común | `0x466060(actor)` → `[[actor+0x68]+0x1A0]->vf150(0)` | igual |
| Por tipo `[[actor+0x88]+1]` | 3–6, 8, 9 → `0x48B120`; el 7 no | 3–6, 8, 9 → `0x48AD40` |

Payload: 724 B (2723) y 725 B (2991), por el byte de bandera.

**Consumidor `0x48B120` (2723):**
1. Llama a `vf188(5)` del actor (`0x48B191`). Si es falso: resultado 0 y sale (`0x48B19B`). No escribe nada.
2. Si es verdadero hace lo mismo que el 2991 con la bandera puesta:
   - convierte los bloques (`0x1EC9C00`);
   - aplica la acción (`0x863530`, `0x232B120`);
   - copia la acción a `[[actor+0x68]+0x1A0]+0x534…+0x550`;
   - arranca la reproducción **sin condiciones** (`0x87DB50`, `0x87D9C0`).

**Consumidor `0x48AD40` (2991):**
- Rechaza si `[[CC+0x60]+0x10] & 1` y la bandera es 0.
- Omite `0x87DB50`/`0x87D9C0` si `vf188(5)` y bandera 0.
- En un actor remoto (`vf188(5)` falso) aplica la acción y arranca la reproducción.

Ni el manejador ni `0x48B120` escriben `+0xCF`, `+0xC2` ni `+0x5E` del actor. No inicializan nada que la 2991 no
haga ya.

**Medido [V]** (sesión 8, `backups\p8-sesion8-shadow\cdmp.log`, sonda S3 con `0x48B120`/`0x48B197` y
`0x48AD40`):
- resumen final: `2723 consumer 2017 (vf188 true 0, FALSE 2017)`;
- `2991 consumer 49785 (discarded 0)`;
- `StartAction installed … from 2723 0/0/0, from 2991 49785/0/0`;
- líneas `2723 vf188(5) 0xB010041B ctrl 0x90100000: AL 0 (C8 0) -> returns success WITHOUT applying`.

En la sesión 13 la sonda estaba apagada. El controlador es el mismo en las dos cohortes (`0x90100000`, 537 de
538 y 13 351 de 13 352): las dos son remotas por igual.

## 2. Cuándo se manda, y qué recibe cada cliente [V]

- **Escritor y envío** (ya documentados en `opcion-d-notas/optD-mundo.md`):
  - el relevo `0x2A7A560` manda **2723** (escritor `0x29644A0`) en lugar de 2991 cuando la acción lleva la
    bandera `0x40` («de aparición», `+0x51`/`+0xC9`);
  - es la primera acción tras aparecer;
  - va `ToNearExceptOneControl`: a todos los cercanos **menos el controlador**.
- **Sesión 13** (`a4.py`; invitado = anfitrión − 147,84 s):

  | | 2723 S>C | 2223 C>S |
  |---|---:|---:|
  | Cliente del anfitrión | **2** | 81 539 |
  | Cliente del invitado | 2 589 (2 589 claves, todos de 724 B) | 806 |

  - Cada 2723 del invitado coincide con un 2223 del anfitrión (Δt 0,0 s en 2 570 de 2 570 vivos; `+4 == 0` en 2 114).
  - El 2723 sale pegado al 2005 (p50 +5 µs, máximo 32 ms): `observer_hold` libera al NPC justo después de su
    primera acción.
- **Instantánea en el anfitrión** (`a3.py`), 538 claves:

  | Momento del primer 2005 en el anfitrión | NPC |
  |---|---:|
  | t 40–50 (su propia carga del campo, 2549 en t 43,96) | 399 |
  | t 80–190 (antes de que entrara el invitado, ~t 192) | el resto |

  - Su primer 2005 en el anfitrión llevaba restauración **0** (211 + 83 + 5 + 4 de tipos 3, 6, 5, 4).
  - Su primera acción salió con el invitado aún fuera: **537 de 538 no tienen ningún 2723** en ninguna captura.
- **De forma nativa**, un jugador que entra en un campo ya poblado no recibe 2723 de los NPC que ya actuaron. Recibe
  su 2005 con el bloque CC: ruta `.app_xml`, `u64` sesión de acción y **byte de restauración**.
  - El cliente lo aplica en `0x8646D0` solo con `[[CC+0x40]+0x30] != 0`.
  - En un actor remoto el desplazamiento es 0 (`vf188(5)` en `0x864936` solo cambia el tiempo de reanudación).
  - Después vienen `0x863880`, `0x85ED00` y `0x2329D70`.
  - Los 2005 de instantánea del invitado tienen restauración 1 en 322 de 323 NPC con CC (los 217 de tipo 7 no
    tienen CC). Los vivos, en 2 585 de 2 587.
- **Payload del 2723 (724 B)** (`a5.py`/`a6.py` sobre los 2 589 del invitado):

  | Desplazamiento | Contenido |
  |---|---|
  | `+0` | clave |
  | `+4` | bloque A: `u32 0`, hash en `+8`, `0x3EAAAAAB` (1/3) en `+0x24`, `1.0` en `+0x30`, `0x57` en `+0x35` |
  | `+0x49` | bloque B, con la misma forma |
  | `+0x8A` en adelante | posición y rotación |
  | `+0x15F` y `+0x2CA` | `u32` que crece 999,9 por segundo de captura: **el reloj del servidor en ms** |

  - Repetirlo byte a byte arrancaría la acción con la posición y la hora de su aparición.
  - Uno fresco exige el escritor `0x29644A0` con el estado actual del servidor.
  - Las dos cosas son inútiles por §1.

## 3. Lo que diferencia de verdad a la instantánea [V salvo lo marcado]

1. **Orden nativo del servidor, propiedad no nativa.**

   | Viaje | 2549 → 2144 | NPC antes del 2144 | NPC entre 2144 y 2762 |
   |---|---|---:|---:|
   | Usuario 2, viaje 2 (invitado, `a7.py`) | 0,90 s | **531** | 78 |
   | Anfitrión, su propia carga | 12 ms | 1 | 464 |

   - En el anfitrión, `SceneDiag [trip] … trip 2 of user 2` lo dice: `first NPC 2005 +6.354 s` y `2144 +6.375 s`, y
     `NPC records: … 2549..2144 531, after 2144 79`.
   - El servidor anuncia el campo dentro de `MoveFieldCompleteCore` (`0x2BC7670`), antes de escribir el 2144. Es su
     código: un cliente del MMO que entraba en un campo recibía igual a los NPC del servidor durante la carga.
   - Lo nuevo de la opción D es que estos NPC tienen **controlador y dueño de escena de otro cliente**: el Kliff del
     anfitrión `0xA0100001`. En el invitado, ese Kliff se crea en la misma instantánea.
2. **El dueño no es válido al desempaquetarse (`a8.py` sobre las líneas `GuestStage`).**

   | | Con `res8 passes` | Dueño encontrado | t |
   |---|---:|---:|---|
   | Instantánea | 181 | **0** | 45,05–46,55 |
   | Vivos | 483 | 483 | 103–823 |

   - `0x8AE1A0` es un `UserOrCharacterValidLoginedScopeAttacher`: no da un actor sin `+0x5E & 0x10`.
   - El reintento `GuestSchedBind` arregló solo el enlace del secuenciador, 3–7 s después (177 BOUND).
   - **[H]** Cualquier otro paso que busque al dueño o al controlador en la creación pudo fallar igual sin reintento.
     Es la hipótesis (ii) de `p8-s13-npc-familias.md` §6.
3. **Los vivos llegan con todo listo.**
   - `observer_hold` retuvo a 2 586 NPC con restauración 0 y los soltó tras su primera acción (205 ms de media).
   - Ya con el invitado dentro, su vf13 encuentra al dueño (`+0x231 = 3`) y se enlaza de forma nativa.
4. **Veredicto.**
   - La causa **no** es la falta del 2723 [V].
   - Lo más probable es que la cohorte se cree durante la carga del invitado, antes de que el Kliff del anfitrión
     sea válido allí [E fuerte]:
     - explica la «mitad» (la instantánea es la mitad de la plaza, `p8-s13` §2);
     - explica la geografía (oeste y centro);
     - explica que fuera de la ciudad todo vaya bien (todo es vivo);
     - el único mecanismo que se ha visto fallar es el del dueño.
   - Qué paso concreto queda roto sin reintento sigue sin medir [H].
   - Por eso el arreglo **no repara pasos uno a uno**: hace que la instantánea siga el camino de los vivos.
   - La familia (d) del secuenciador está en las dos cohortes y es otro problema (`p8-s13` §5). Esto no la toca.

## 4. Diseño: `observer_load_defer`

Opciones consideradas, de menor a mayor coste:

| Opción | Por qué no / por qué sí |
|---|---|
| Libro de 2723 en el anfitrión y copia en el viaje del observador (lo pedido) | el anfitrión no los recibe (§2), el payload depende del reloj (§2), y en el invitado no hace nada (§1) |
| Retener los 2005 en el invitado hasta que el dueño sea válido | habría que partir 2005 de 4–5 registros y retener todos los paquetes por clave (2697, 2991, 2404…) durante ~15 s en el hilo de red del invitado; mucho código nuevo |
| Reanunciar (2516 + 2005) la instantánea tras el 2762 | `NotifyDisappear` (`0x2B00380`) tiene el cuerpo mutado en `.xtls` (`0x10650890`); habría que llamarlo desde el mod; doble trabajo en el cliente; parpadeo |
| **Retener en el anfitrión, con `observer_hold`, a los NPC anunciados al usuario 2 mientras carga, y anunciarlos tras su 2762** | reutiliza el reparto de `NotifyAppear`, la tabla de NPC (1 024), los pases y la liberación nativa probados en S13 (2 586 liberaciones, 0 caídas); solo cambian el predicado y una puerta en la liberación |

### 4.1 Qué hace (anfitrión)

- **Estado de carga del usuario 2.**
  - `server.cpp` ya sigue su fase de movimiento: Req en `server_inject`, Acks en `take_frame`, bajo `g_phase_mu`.
  - Cada cambio publica `child_hold_note_user2_loading(move_phase_loading(fase))`, un atómico.
  - «Carga» va desde el 2549 (`Completing`, también `WindowB` y `LoadingSent`) hasta que el servidor escribe el 2762
    (`Stable`). Desde la revisión M1 no cuentan 3093/2437/2174: un movimiento que no llega a su 2549 no retiene nada.
  - El 2549 se anota al entrar en la cola del servidor, antes de que este procese la instantánea. El 2762 se anota
    al escribirlo en la cola de la sesión 2, así que toda liberación posterior sale detrás de él.
- **Predicado.**
  - Con la clave a 1, el usuario 2 entre los destinatarios y cargando: un NPC (dueño 0) **controlado por el
    anfitrión** con restauración **1** es `hold` (`HoldVerdict::defer`).
  - `plan_split` lo retiene solo a la sesión 2. El anfitrión, su controlador, lo recibe en la misma llamada.
  - No se difieren:
    - los NPC que controla el usuario 2;
    - los de tipo 7 sin CC (gimmicks), que siguen como antes;
    - las llamadas en las que el usuario 2 no está.
- **Liberación** (`npc_release_step`).
  - Tras las comprobaciones de época, despawn, observador y rango, con `load_wait` (clave a 1 y usuario 2
    cargando) una entrada observada por el usuario 2 se queda (`user 2 is still loading`) durante menos de
    `kLoadDeferMaxMs = 60 s`.
  - Sin `load_wait` deciden las reglas de siempre: restauración 1 → `NotifyAppear([sesión 2], [claves])` en el
    hilo de siempre.
  - La puerta vale también para las retenciones propias de `observer_hold` (restauración 0) mientras el usuario 2
    carga: tampoco salen antes de su 2762.
  - Pases: el dirigido tras cada `StartActionReq`, y el presupuestado (256 entradas cada 100 ms, también tras
    `NotifyAppear`). ~540 NPC salen en ≤ 3 pases tras el 2762, en llamadas de ≤ 256 claves.
  - El serializador nativo parte en 2005 de 4–5 registros (≤ 32 KB), como hizo con la instantánea de 610 claves.
- **`observer_hold_acks`.**
  - Las claves diferidas **no** se registran, porque serían ~15 s de 2697/2991 por NPC por encima del tope de 20.
  - Sus paquetes llegan al invitado como sin retención y su cliente los descarta (clave desconocida, `0xB579F9`).
  - El 2005 tras el 2762 lleva el estado de entonces.
- **Tope.** Sin 2762 en 60 s (S13: 2549 → 2762 en 14,7 y 19,0 s), las reglas de siempre lo liberan con
  restauración 1, como el motor durante la carga. Se cuenta como `AT THE CAP`.
- **Salida del usuario 2:** lo suyo se borra con el resto de `observer_hold` (`drop_npc_user2_locked`).
- **`probe`:** nada se retiene. Se cuenta `would` y hay hasta 16 líneas `WOULD be withheld`. Las retenciones de
  `observer_hold` no esperan.

### 4.2 Archivos

| Archivo | Cambio |
|---|---|
| `src/mod/session2/child_hold.h` | `kLoadDeferMaxMs`, `HoldVerdict/HoldRecord/HoldEntry::defer`, `ObserverHoldUsers::{load_defer, user2_loading}`, `NpcReleaseReadings::load_wait`, `child_hold_set_load_defer`, `child_hold_note_user2_loading`, costuras de prueba |
| `src/mod/session2/child_hold.cpp` | predicado, registro (líneas propias), puerta en `npc_release_step`, pase (`load_wait`, sin línea «still held» por la espera), contadores, `describe`, sin registro en `observer_hold_acks` |
| `src/mod/session2/guest_lifecycle.h` | `move_phase_loading()` |
| `src/mod/session2/server.cpp` | publica la fase en sus tres puntos (`phase_reset`, `on_req`, `on_ack`) |
| `src/mod/config.{h,cpp}`, `src/mod/session2/session2.cpp` | clave `observer_load_defer` (`0 \| probe \| 1`; necesita `observer_hold=1` y `create_session=1`) |
| `tests/optd_s3_tests.cpp` | `test_hold_load_defer` (7 casos) |
| `docs/research/ini/cdmp.p8-s7-host.ini`, `cdmp.p8-noche-local.ini` | `observer_load_defer=1` |

No hay ganchos nuevos ni lecturas nuevas del juego. Las únicas llamadas al juego son las `NotifyAppear` de
`observer_hold`, en sus hilos. Nada toca `.xtls` ni el heap del juego desde un hilo del mod.

## 5. Pruebas [V]

`test_hold_load_defer`, con el mundo falso de `optd_s3`:
1. Con la clave a 1 y cargando, un NPC con restauración 1 va solo al anfitrión. Sus acciones y los 10 s de
   `observer_hold` no lo sueltan; el 2762 sí, con un `NotifyAppear` a la sesión 2. Se comprueban las líneas, y que
   después ya pasa de forma nativa.
2. El tope de 60 s: sale aunque siga cargando (`released_at_cap`).
3. Restauración 0 durante la carga: es la retención de `observer_hold`, no una diferida, y también espera al 2762.
4. `probe`: nada retenido; `would` contado; las retenciones de `observer_hold` no esperan.
5. Pasan de forma nativa: el usuario 2 sin cargar, el usuario 2 sin ser destinatario, un NPC que controla el
   usuario 2, y la clave a 0.
6. `observer_hold_acks`: la clave diferida no se registra, su 2697 pasa y su 2005 sale tras el 2762.
7. Si el usuario 2 se va, sus diferidas se borran.

Además, `move_phase_loading` para las 8 fases. Resultado de `.\scripts\build.ps1`: 42 de 42.

## 6. Qué mirar en la próxima sesión de dos PC

Necesita Shadow. El anfitrión usa `cdmp.p8-s7-host.ini` (con la clave a 1) y el invitado su ini de siempre.

**Anfitrión (`cdmp.log`):**
- `Session2: observer_load_defer ON: …` al arrancar.
- `observer load defer: user 2 starts a login / field load …` en cada viaje, y `… has its 2762 …` al cerrarlo.
- Hasta 16 líneas `observer load defer: NPC 0x… withheld from session …` y 16 `released NPC 0x… after N ms, user
  2's 2762 written (… key(s) in that NotifyAppear)`. Lo esperable es N ≈ la duración de la carga (15–20 s).
- En el `describe` del minuto: `observer load defer (on): … NPCs withheld while it loads N | released N (after its
  2762 N, AT THE CAP … 0 …) | dropped … | withheld now 0`.
  - Esperado en Hernand: ~320 retenidos en el viaje 2 y 0 al tope.
- `SceneDiag [trip] … trip 2 of user 2`:
  - `NPC records: … 2549..2144` debe bajar de 531 a solo gimmicks (tipo 7, ~200);
  - el primer 2005 de un NPC con bloque CC (tipos 3–6) debe ir **después** del 2762.

**Invitado:**
- `GuestStage`: ningún NPC con `res8 passes` sin `owner … found`. La cola de `GuestSchedBind retry` debe quedar en
  ~0 (en S13 tenía 190).
- La sonda S3 sigue apagada en las inis de Shadow. Si se quiere la contraprueba del §1, pon `guest_s3_diag=1` en el
  ini del invitado: `2723 consumer N (vf188 true 0, FALSE N)`.
- **Vídeo de la plaza de Hernand** al entrar: la mitad oeste y centro (escenas `0x20000014`, `0x20000188`,
  `0x20000187`) debe moverse como la este.
  - Ventana visual: tras la carga, los NPC aparecen 0,1–0,5 s después del fundido (es la liberación).
  - La familia (d) (la tropa de `0x13` y `0x1A4`) puede seguir quieta: es otro problema.

## 6 bis. Regresión de un PC del 11-10 (`backups\regresion-20261011-014008.log`) [V]

- Resultado: `NPCs withheld while it loads 293 | released 285 (after its 2762 285, AT THE CAP 0; wait mean 5702 ms,
  max 5813 ms) | dropped 8`.
- **Las 8 caídas son inocuas.** Las 8 son `dropped after 3282 ms: despawned`: criaturas de tipo 6 (`0xB0100210`–`213`,
  `223`, `22C`, `23B`, `23C`, acción `0x968C694E`).
  - Su 2005 llegó al cliente del anfitrión en t 60,48 / 74,23 de su captura (`cdmp_packets_20261011-014020.bin`,
    `a11.py`).
  - El servidor las dio de baja con **un mismo 2516** en t 91,706, el instante de la caída (≈ t 91,72 por el reloj
    de `SceneDiag`).
  - Sin actor no hay nada que mostrar, y el invitado nunca tuvo su 2005.
- **Endurecido igualmente** (rama `p8-spawn-action-2`):
  - mientras el usuario 2 carga, solo dejan caer una entrada la otra época, el despawn y el observador ido;
  - «fuera de rango» y «ilegible demasiado tiempo» esperan al 2762 y se deciden entonces con las reglas de siempre.
    Durante la carga los observadores de un sector pueden estar aún asentándose;
  - el `describe` separa `dropped N (despawned a, user 2 gone or new epoch b, OTHER c …)`. Solo `OTHER` puede dejar
    un NPC invisible;
  - caso 8 nuevo en `test_hold_load_defer`.

## 6 ter. Revisión independiente (rama `p8-spawn-action-3`)

| Hallazgo | Arreglo |
|---|---|
| **M1:** sin tope global, una fase atascada (2174/2549 sin 2762, un movimiento abortado, un 2762 que no pasa por `take_frame`) retendría todo NPC durante 60 s, una y otra vez, y congelaría las retenciones de restauración 0 | `move_phase_loading` solo vale de 2549 a 2762 (`Completing`/`WindowB`/`LoadingSent`). La carga anota su inicio y, pasados `kLoadDeferMaxMs` (60 s) desde él, se da por terminada (`user2_loading_now`): una línea WRN por carga (`… without its 2762 (a stuck move phase?) …`), `STUCK past 60000 ms N` en el `describe`; las retenidas se liberan y no se retiene nada más hasta la siguiente carga |
| **M2:** las copias de instantánea de `observer_seq_copy` cuyos actores (tipo 4/5: tenderos, NPC de charla o barrido) difiere `load_defer` quedan listas tras el 2762, es decir, **después** de la copia del 2322 del anfitrión (en S13 iban antes) | `Copier::set_milestone_hold`: con `observer_load_defer=1`, el 2322 del anfitrión espera mientras quede una copia de instantánea esperando a sus actores, hasta el 2762 del usuario 2 + 2 s (o 62 s desde el viaje sin 2762). Contadores y líneas: `ObserverSeqCopy 2322 hold … trips held N, ran out … N, snapshot copies sent AFTER the 2322 N`, y en cada copia `| AFTER the host's 2322 …` |
| **L1:** el aviso de fin de carga corría antes del `push` del 2762 | el fin de carga se avisa tras el `push` del 2762 (al salir de `take_frame`); el inicio, en el acto |
| **L2:** el tiempo límite de restauración 0 contaba desde la retención: lo retenido durante la carga vencía en el acto con el 2762 | cuenta desde `max(retención, fin de carga)` (`NpcReleaseReadings::timeout_age_ms`) |
| **L3:** `observer_hold_acks` `max_hold_ms` 40 s < las nuevas esperas | 100 s (> 60 s de la carga + 3 × 10 s) |
| **L5:** si el usuario de la sesión 2 cambia durante la carga, «observer gone» la soltaba como inocua aunque el usuario 2 siguiera | motivo propio `observer's user changed`; «gone» solo es inocuo si el usuario 2 ya no está, si no cuenta como `OTHER` |

Pruebas nuevas:
- `test_hold_load_defer` casos 9–11: carga atascada, tiempo límite tras la carga, y cambio de usuario.
- Fases de `move_phase_loading`.
- `test_milestone_hold` (5 casos) en `optd_seq_copy`.

Resultado de `.\scripts\build.ps1`: 42 de 42.

## 7. Criterios para abortar (volver a `observer_load_defer=0`)

- Cualquier `AT THE CAP` en una carga normal: la fase no llega a `Stable`, o el 2762 no se ve en `take_frame`.
- `OTHER` > 0 en el `describe` con el usuario 2 en la plaza: el NPC se queda invisible hasta que el usuario 2 salga
  de su sector y vuelva. `despawned` y `user 2 gone` son inocuos.
- NPC invisibles en el invitado tras la carga (presencia en el anfitrión sin 2005 en el invitado; lo da
  `honly.py` de S13).
- `Actor 생성에 실패` o errores de creación nuevos en el invitado, o un cierre en el invitado al liberar.
- Si la plaza sigue partida igual con la instantánea ya creada tras el 2762, la hipótesis del §3 queda refutada.
  En ese caso, volver a `0` y medir pares de la misma escena con las sondas del §6 de `p8-s13`.
