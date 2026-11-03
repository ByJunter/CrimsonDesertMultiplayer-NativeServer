# S44 §3.2 — Semántica nativa de las escenas del anfitrión en el cliente del invitado

Build 1.0.0.2976, base `0x140000000`, direcciones en RVA. Solo lectura: no se compiló, no se desplegó y no se abrió el
juego. Herramientas: capstone 5.0.7 sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` (scripts `pe.py`, `win.py`,
`scanw.py`, `callers.py`), decompilación Ghidra (`dc.sh` → `dc\0x*.c`), las 98 capturas de `C:\Emulador CD\backups`
(`dump2661.py`, `names.py` → `res2661.json`, `resnames.json`) y los recursos del juego extraídos de los PAZ con el
extractor del proyecto (`extract.py` → `data\`). Todo en `scratchpad\s44\nativo\`.

Marcas: **[V]** = leído en instrucciones o en la decompilación, contrastado; **[E]** = inferencia fuerte; **[H]** =
hipótesis que hay que medir.

---

## 0. Resumen

1. **El manejador del 2661 no mira de quién es la escena [V].** `0xC05BB0` resuelve el actor del paquete, toma el
   gestor del **dueño del campo** (`0x17FA6C0` → `ClientField::vf48 = 0x9A5BE0`) y crea la escena ahí con `0xA9CA40`.
   No hay ninguna comprobación de «dueño ≠ personaje local». En el juego nativo esto no hace falta, porque el servidor
   solo manda el 2661 al dueño (ToTarget). La tarea de carga guarda como «dueño» al **jugador local**:
   `0xA90AF0` → `task+0x80 = [ActorMgr+0x50]`, el mismo campo que lee `GetLocalPlayer 0x8B44E0`.
2. **`0x2000000N` es un número de nodo; el rol de jugador lo da el recurso [V].** Cada nodo de actor del `.paseq` tiene
   una clase de datos: `ActorTrackNodeData_CurrentControl`, base, `SpawnCharacter` o `PivotActor`. Esa clase escribe
   `nodo+0xAC` (el «tipo de nodo»):
   - `0` = **CurrentControl**, el jugador (`0xC7FB2E`);
   - `1` = actor fijo (`0xC7F6DA`);
   - `2` = SpawnCharacter (`0xC88577`);
   - `4` = PivotActor (`0xC8014E`).

   En las capturas, el dueño del 2661 está enlazado como mucho una vez por escena, siempre con el tipo de enlace 5.
3. **El motor da por hecho que el actor del nodo CurrentControl es el jugador local [V].** Cuando no lo es (el Kliff del
   anfitrión en el invitado), ese actor va por el camino de los NPC:
   - **Alineación al empezar la escena:** `0x602940` → `0x6030D0`. A los actores de nodos de tipo 0, 2 o 4 que **no**
     cumplen `ClientActor::vf178` (`0x4FE260`: el usuario del actor es el usuario local y `+0x94`), y que están lejos
     del punto de inicio de la sección, el juego les fija la transformación con `TransformSync::vf1A0(pos, rot, 4, 5, 0)`.
   - **Control de secuenciador:** `SequencerControl_*` → vf21 `0x601470` y vf22 `0x601C10` de la etapa. Si el actor
     cumple vf178, se usa el objeto de control del jugador local (`[stage+0x58]+0x200`, modo input). Si no, se llama a
     `0x9F8980(TransformSync, 1)` y se aplica el control por actor de `.xtls`.
   - Cada orden de etapa y cada evento de línea de tiempo dirigidos al nodo resuelven su actor con **`0x205DC30`**: las
     entradas `stage+0x120` de tipo 5, buscadas por clave.
4. **Bytes 17 y 18 del 2661 [V]:**
   - son `playdata+0x10` y `playdata+0x11`, que `0x20519F0` copia a `stage+0x60` y `stage+0x61`;
   - **`+0x61` (byte 18) elige la tabla estática que describe la escena** (`0x2054340`, llamada desde `0xA90E80`):
     0 = `stageinfo`, 1 = `sequencerspawninfo`, 2 = un actor (la revancha), 3 = `itemuseinfo`,
     4 = `GlobalStageSequencerInfo`;
   - **`+0x60` (byte 17) es la categoría de disparo:** 0 o 1 = de posición (caja `+0xD8..+0xEC`), 2 = ligada a un
     actor (la compuerta de `Loading` `0x204F730`), 6 = uso de objeto (`0x1F6FC40`), 8 = contexto de misión
     (`0x1AC2830`, `0x1F709A0`), 10 = `AutoSpawnOwnerData` (servidor `0x287DFE0`).
   - Separan la revancha (2/2), el uso de objetos y memorias (6/3) y la fauna (10/1 y 1/1). **No separan lo personal
     de las escenas de etapa 1/0:** tiendas, minijuegos, misiones, recompensas, tiempo y la cinemática de carga.
5. **El filtro `0x9F8FCE0` / `0xA97D60` no distingue mundo y personal [V].** Decide qué escenas tienen que estar listas
   o reproducirse ya **para el jugador local**:
   - escenas del contexto de misión cargado del dueño del campo (`0x1AC2830`);
   - escenas con `+0x61 == 5`;
   - escenas con `+0x60 ∈ {0, 1}` y la posición local dentro de su caja.

   Lo demás espera, pero `0xA97300` acaba reproduciéndolo en la fase 3. Es una compuerta de carga y de prioridad.
6. **Causa de la pose [E fuerte; el último paso está en `.xtls`, H].** `cd_seq_boss_rematch_matthias` liga al jugador
   (`0x20000002`) a un nodo cuyo actor es el Kliff remoto. El `.paseq` tiene eventos `SequencerControl_Input`, `_AI` y
   `_Off`, `SequencerPlayerCameraPreset` y `Sequencer_Talk_TwoTarget`. En el invitado, el Kliff remoto recibe la
   alineación y el control de secuenciador **como si fuera un NPC**, porque `vf178` es falso. Mientras tanto, los 2991
   del anfitrión siguen llegando. Que el control de `.xtls` congele la pose y anule las acciones de red no está medido:
   §5 propone la sonda.
7. **Arreglo de raíz recomendado: opción (a).** En las escenas de X, quitar el enlace **activo** del nodo de jugador
   cuando su actor no es el local: el tipo de enlace pasa de 5 a 0. Así `0x205DC30` no lo resuelve nunca, y ninguna
   orden ni evento llega a la copia del anfitrión. NPC, gimmicks y tiendas siguen igual. Detalle y riesgos en §6.

---

## 1. Pregunta 1 — Qué hace el cliente con un 2661

### 1.1 El manejador `0xC05BB0` (RUNTIME_FUNCTION `0xC05BB0–0xC0632F`) [V]

| Paso | Dónde | Qué hace |
|---|---|---|
| Formato | `0xC05BDB…` | `u8 kind` → `[rbp+0xF0]`; `u32 actor` → `[rbp+0xF8]`; los datos de reproducción (`thunk 0x14F604F90`, `.xtls`) → `[rbp+0x00]`, una estructura de 0x68 B con estos valores iniciales: `+0x10/+0x11 = 0B 07`, `+0x4C = 0x7FFFFFFF`, `+0x50 = 0xFFFF`, `+0x5C = −1`, `+0x64 = 0x0D`; `(len−91)/11` enlaces → vector `[rbp−0x80]` (puntero) / `[rbp−0x78]` (número), en entradas de 12 B `{u32 nodo, u32 actor, u8 tipo, u8, u8}`, inicializadas a `{−1, 0, 7, 0}` |
| Listo | `0xC05D49` | si la clave del actor es de usuario y el actor no está listo → «reintentar» |
| Actor | `0xC0608D` `0x8A67E0`; `0xC060A3` `0x466060` | si el tipo del actor es 1 o 3–9: `[[actor+0x68]+0x1A0]->vf150(0)`. En el cliente, `ClientTransformSyncActorComponent` vf42 = `0x359B50` = `ret`: **no hace nada**. Después, `[r12] = actor` |
| Gestor | `0xC060EC` → `0x17FA6C0`; `0xC06127 call [rax+0x48]` | `ClientField::vf48 = 0x9A5BE0`: el gestor del personaje del usuario dueño del campo (M). **No depende del dueño de la escena** |
| Alta | `0xC0612A cmp bl,4 ; je 0xC06179` | solo `kind == 4`: `0xC06188 call 0xA9CA40(M, &res, &playdata=[rbp], &bindings=[rbp−0x80])`. Con otro valor, solo una línea de error. `0xC061C1`: el manejador devuelve 0 en los dos casos |

**Si el dueño no es el personaje local [V]:** el código nativo no lo comprueba en ningún sitio. La escena va a M, el
gestor del dueño del campo, con los enlaces tal cual. El mod cambia RAX en `0xC0612A` (`guest_seq_route`) para que vaya
a X.

### 1.2 `0xA9CA40` — creación (RUNTIME_FUNCTION `0xA9CA40–0xA9D47E`) [V]

1. Reserva 0xC0 B y llama a `0xA90AF0(task, playdata, ?, gestor)`:
   - `task+8..+0x77` = copia de los datos de reproducción;
   - `task+0x78` = gestor;
   - `task+0x80` = **el jugador local** (`[[0x6D691E0]+0x30]+0x50`, con la misma comprobación vfC0(4, 0x10) que
     `GetLocalPlayer 0x8B44E0`). Si es nulo, la carga falla en `0xA90E80` (`DAT_146CE5118`);
   - `task+0xBC = 1`.
2. Por cada enlace (bucle de `0xA9CB60` a `0xA9CDEE`), con entradas de 0x30 B en `task+0xA8` (número `+0xB0`):
   - **tipo 4 o 5:** `0x8AF170(ActorMgr, &att, clave)` crea un
     `ContentsCharacterValidLoginingOrLoginedScopeAttacher<CommonActor>` (referencia al actor). Si el actor no está:
     `*res = eErrNoInvalidActorKey`, la tarea se destruye (`vf0(1)`) y **se descarta la escena entera**. Con clave 0, el
     resultado es «no encontrado»;
   - **otro tipo:** la entrada se guarda sin actor.
3. Encola la carga asíncrona `AsyncLoadSequencerCustomFileDataForPrepare` con tres retrollamadas:
   - `0xAA6790 → 0xA90E80`:
     - información estática con `0x2054340(+0x61, +0x18)` (§2.1);
     - el `.paseq`, por la ruta de `[info]+0x1B0` («`%#/%#`»);
     - la etapa con `thunk 0x14DE16970` (en `.xtls`), con `(gestor, playdata, recurso, info)` → `task+0x90`;
   - `0xAA67A0 → 0xA912C0`: animaciones y recursos de la escena;
   - `0xAA67B0 → 0xA91D30`.
4. Suma uno a `gestor+0x17C`, calcula el hash del id (`playdata+8`) y lo inserta en el mapa `gestor+0x170`. Si ya
   existía: `DAT_146CF76A4`.

### 1.3 La etapa y sus enlaces [V]

- **Inicialización de la etapa** (`0x20519F0`, ranura 55 de `pa::ClientSequencerStage`):
  - `+0x98` = `pd+0` (dueño);
  - `+0x30` = `pd+8` (id);
  - **`+0x60` = `pd+0x10`**, **`+0x61` = `pd+0x11`**;
  - `+0x94` = `pd+0x40`;
  - `+0x48` = información de etapa.
- **Lista de actores** `stage+0x120` (número `+0x128`), entradas de 0x28 B:
  - `+0` = puntero al nodo (`nodo+0x30`/`+0x34` = ids, `nodo+0xAC` = tipo de nodo);
  - `+8` = clave del actor;
  - `+0xC` = tipo de enlace.

  Así la leen `0x602940`, `0x205DC30` y `0x205F7F0`.
- **Tipos de enlace:**
  - `0x205B4D0` (`0x205B5B0`) convierte el 4 en 5: el 4 es «pendiente»;
  - `0x2053D50` registra en el gestor los de tipo 4 (`vf88(stage+0x30, clave)`);
  - `0x205DC30` solo produce actor con el 5 y no cuenta el 0.

  **En las 1 520 recepciones el tipo es siempre 5** (también en el inventario).
- **Tipo de nodo `+0xAC` (clase de datos del nodo, vf85):**

  | Clase | vf85 | `nodo+0xAC` |
  |---|---|---|
  | `ActorTrackNodeData_CurrentControl` (`0x55FF090`) | `0xC7FB20` | **0** |
  | `ActorTrackNodeData` (base) | `0xC7F5A0` | 1 (`0xC7F6DA`, condicional) |
  | `ActorTrackNodeData_SpawnCharacter` | `0xC884D0` | 2 |
  | `ActorTrackNodeData_PivotActor` | `0xC80140` | 4 |
  | `ActorTrackNodeData_ConnectActor` | `0xC80740` | lo que deje la base, más `+0x100` |

### 1.4 Qué se hace con el actor de cada enlace [V salvo lo marcado]

| Momento | Función | Nodo de tipo 0 (jugador) | Otros |
|---|---|---|---|
| Creación | `0xA9CA40` | referencia al actor (tipo 4/5) | igual |
| Inicio (`0xA9D860` → **`0x602940`**) | `0x602BFF–0x602CB3` | `FindActor` por clave (`0x8AE940`). Si el componente de datos de secuenciador del actor tiene `+0x2E0 ==` esta etapa → `0x9EE710` (desenlaza). **Con tipo 5:** `Status_SetFlagBit(Status, 0x200, 0 = quitar, mezcla)`, es decir, quita el bit `0x200` de `Status+0x390`, que es un bit de ocultación (`copias-grupo-invisibles.md`) | tipos 1 y 2: lista `ToBird` y `0x9CB1CB0`; `+0x61 == 3`: el gimmick `+0x94` (tipo 7) → `0x8E59D0` |
| Inicio (`0x602940` → **`0x6030D0`**) | nodos de pista de actor (`vf298 == 0xF`) de la sección inicial con `+0xAC ∉ {1, 3}` | **`0x205DC30`** resuelve los actores. Si un actor no cumple `vf178` (no es el jugador local), su componente de datos no está en esta etapa, cumple `0x505D60` y está más lejos que el umbral del punto de inicio, entonces `[[a+0x68]+0x1A0]->vf1A0(pos, rot, 4, 5, 0)`: **lo coloca en la marca de la escena** | igual, para Spawn y Pivot |
| Órdenes de etapa y eventos | `0x205DC30` (llamado desde `0x5EC510`, `0x5F3600` LookAt, `0x5F3810`, `0x601470`, `0x601C10`, `0x6030D0`, `0x2047510`, `0x2049C70`, `0x204AA50`, `0x205EFB0`, `0x205FCC0`, `0x2060380`, `0x2062DA0`, `0x2064270`, `0x20659A0`, `0x207DDB0` y `.xtls 0xE0314C0`) | los ids de destino se comparan con `nodo+0x30/+0x34`. Con tipo de enlace 5, se busca el actor por clave (`[ActorMgr]->vf28`). El id especial `0x1FFFFFFF` → `0x205EFB0`: el nodo que corresponde al contexto de misión del **dueño del campo** | igual |
| Control de secuenciador | `SequencerControl_Input` (vtable `0x562A2D0`): ranura 99 `0xC3A840` → `.xtls 0xA534680` → **etapa vf21 `0x601470`** (inicio); ranura 101 `0xC3A850` → `.xtls 0xA535010` → **vf22 `0x601C10`** (fin) | `0x601470`: `0x205D7C0` (actor dueño `stage+0x98`); si `stage+0x28B == 0`, sale. Destinos con `0x205DC30`. Por cada actor: si **`actor->vf178()`** (jugador local) y `desc->vf2A8()` → el objeto de control `[stage+0x58]+0x200` (`0x5FA1D0`). **Si no**, `0x9F8980(TransformSync, 1)`, y después el control por actor de `.xtls` (`thunk 0x14F098530`, H) | igual (NPC) |

`ClientActor::vf178 = 0x4FE260` [V]: `[[0x6D691E0]+0x30]+0x58 == [actor+0xA0] && byte [actor+0x94]`, es decir, «el
usuario de este actor es el usuario local». En el Kliff del anfitrión, en el invitado, es falso.

**Respuesta [V/E]:** el «rol de jugador» no se aplica por el número `0x20000002`, sino por la clase del nodo
(`CurrentControl`, `+0xAC = 0`). El código nativo **supone** que el actor de ese nodo es el jugador local. Con un
jugador remoto:
- la cámara, la interfaz y la entrada no se le aplican a él: esas funciones actúan sobre globales del cliente local y
  ya las vetan `guest_x_*`;
- pero sí se le aplican la **alineación** (`0x6030D0`), el **control de secuenciador de NPC** (`0x601470`, rama de
  `vf178` falso) y todo lo que resuelve `0x205DC30`.

---

## 2. Pregunta 2 — Qué dato nativo distingue una escena personal

### 2.1 Bytes 17 y 18 del 2661 = `stage+0x60` / `stage+0x61` [V]

**`+0x61` (byte 18): la fuente de la información estática.** `0x2054340(b, st)`, con `st = pd+0x18` (bytes 19–26),
llamada desde `0xA90E80`:

| b | Tabla (global y quién la escribe) | Clave | En las capturas |
|---|---|---|---|
| 0 | `stageinfo` (`0x6D6C1F8`, `0x2352F70`) | `st` bajo | tiempo, tiendas, minijuegos, misiones, nodos, recompensas, cinemática de carga |
| 1 | `sequencerspawninfo` (`0x6D782E8`, `0x236F4E0`) | `st>>32`, índice `st` bajo × 0x38 | `spawn_docu` y `spawn_auto` de fauna |
| 2 | ninguna: un actor (`[ActorMgr]->vf28(st>>32)` → `0x222D890`) | NPC (`st = NPC<<32 \| 0x135`) | solo `cd_seq_boss_rematch_matthias` |
| 3 | `itemuseinfo` (`0x6D6F6C8`, `0x2423550`) | `st` bajo | `memory_*`, `use_sealedartifact` |
| 4 | `GlobalStageSequencerInfo` (`0x6D6AA28`, `0x1AD7DC0`) | `st>>32` | — |
| 5 | `0x15531D0` / `0x9D35E40` | `st` bajo | — |

- `+0x61 == 3` en `0x602940` (`0x602E9C`): el gimmick `+0x94` (tipo 7) → `0x8E59D0`.
- `+0x61 == 5` en `0xA97D60` (`0xA97E61`): «hace falta ya».
- `+0x61 == 1` en `0x20532B0`.

**`+0x60` (byte 17): la categoría de disparo.**

| a | Dónde | Significado |
|---|---|---|
| 0 o 1 | `0xA97D60`/`0x9F8FCE0`: caja `+0xD8..+0xEC` frente a la posición; `0x1AC2830`: `+0x70 ==` contexto de tipo 1 | escena de **posición** (de campo) |
| 2 | `0x204F730` (compuerta de `Loading`): `+0x68` = `{NPC>>32, tipo>>16}`; el NPC tiene que existir y pasar `0x204F650` | **ligada a un actor** (revancha) |
| 3 | `0xA95940` (`0xA95A6D`): lista `+0x1D0` | — |
| 4 | `0x1F70E50`, `0x1F74500` | excluida de cierta cuenta |
| 6 | `0x1F6FC40`: `+0x68` como clave en una tabla | **uso de objeto** |
| 8 | `0x1AC2830`, `0x1F709A0`, servidor `0x287ABB0` | **contexto de misión** (compara `+0x68` alto) |
| 10 | servidor `0x287DFE0` (`"AutoSpawnOwnerData"`) | **aparición automática** |

- Quién la escribe en el servidor: **no localizado** (los lectores del servidor son `0x287ABB0` y `0x287DFE0`).
- **Conclusión [V + datos]:** los dos bytes describen **qué creó la escena y cómo se dispara**, no a quién afecta. La
  revancha (2/2) y el uso de objetos (6/3) son personales por construcción. Pero 1/0 mezcla el ambiente con escenas que
  se vuelven personales en cuanto el dueño interactúa. Un clasificador basado solo en estos dos bytes no basta.

### 2.2 El filtro `0x9F8FCE0` (`.xtls`) y su gemelo `0xA97D60` (`.rsrc`) [V]

`0xA97D60` (lo llama la máquina de carga `0x7797F0`, en `0x779E72`) devuelve «aún no listo» si alguna escena cargada de
`+0x1B0` cumple una de estas condiciones:
- `0x1AC2830(fieldOwner->vf40(), stage)`;
- `stage+0x61 == 5`;
- `stage+0x60 ∈ {0, 1}` y la posición del jugador local está dentro de la caja.

Además, exige que las escenas de `+0x1C0` que pasan `0x1AC2830` estén listas (`0x5DB510`). `0x9F8FCE0` (§1.1 de
`p8-xtls-lectura.md`) reproduce de golpe lo que cumple esas mismas condiciones.

`0x1AC2830` [V]: compara la etapa con el componente `q = [[c+0x68]+0x138]+0x18` del **personaje dueño del campo**,
campos `+0x186` (tipo; `0xB` = ninguno) y `+0x188` (id). Lo escribe `0x1AC2320`, al que llaman:
- la función de etapa `Loading` `0x5F2A00`;
- la vf7 de recarga `0xA99AD0`;
- `ApplyFieldTime 0xA38B00`;
- `0x49D5F0`, `0x632110`, `0xBF78D0` y `0x1AC4BB0`.

[E] Es el «contexto de nivel o misión cargado».

- **No distingue mundo y personal:** es una prioridad de carga. Todo lo de `+0x1C0` se reproduce después en la fase 3
  de `0xA97300`, con freno (`p8-xtls-lectura.md` §1.3 [V]).
- Sus entradas son del **cliente local**: el dueño del campo y la posición local. En el invitado darían el contexto del
  invitado, no el del anfitrión.
- **Saltarlo con `guest_seq_play` no es la causa de la pose:** en el anfitrión, la misma escena también se reproduce
  al final.

### 2.3 Lo que sí marca que una escena «toma» al jugador [V estático; la clasificación previa es H]

En el recurso hay dos sitios:
- **Etapa** (`.pastage`): las órdenes `Player.*` y las de la línea de tiempo.
  - Revancha: `Player.set_ui_bossRematch`, `Player.condition_uiInput`, `Player.set_move_field`,
    `Timeline.set_exclusive(True)`.
  - Herrero: `Player.set_inputBlock`, `Player.set_enterStore`, `Player.set_ui_buttonList_*`,
    `Timeline.set_exclusive/set_cutScene(True)`, en las ramas `CONTACT`/`MENU`.
  - Pulsos: `Player.set_miniGame_play` en `WAIT_SCENE`.
- **Línea de tiempo** (`.paseq` / `.paseqc`): la pista del nodo `CurrentControl` con eventos `SequencerControl_Input`,
  `_AI` y `_Off`, `GameData_TimelineEvent_Control_Input/Off`, cámara (`SequencerPlayerCameraPreset`) y diálogo
  (`Sequencer_Talk_TwoTarget`).
  - **Los tienen tanto las escenas personales como las de tienda.** En las tiendas solo actúan en las ramas que abre la
    interacción del dueño.
  - En el invitado, esas ramas llegan como 2981 copiados, y eso cuadra con el inventario: estados 1–3, 7 y 14.

**Conclusión:** no hay ningún dato del 2661 ni de la información estática que diga «personal» [V]. El dato nativo que
decide si una escena **actúa sobre un jugador** es el nodo `CurrentControl` (`+0xAC == 0`) y lo que su pista u órdenes
hacen sobre él. El motor resuelve todo eso en un único punto, `0x205DC30`, y en el inicio (`0x602940` / `0x6030D0`). Por
eso el diseño de raíz no clasifica escenas: **neutraliza el nodo de jugador cuando su actor no es el jugador local**.
Es justo el caso que el juego nativo nunca produce.

- En el juego hay una noción nativa de «etapa de otro usuario», pero parcial: `stage+0x280`. La escribe `0x205F7F0`
  (`= param_3+0x60`) y la lee `Loading` («si `+0x280` ≠ 0 y ≠ usuario local, no hay carga»,
  `p8-s9-cinematica.md` §7.2).
  - Solo se rellena en un caso (registro de guardado pendiente).
  - No sirve como clasificador general [V].

---

## 3. Pregunta 3 — Por qué la copia del anfitrión se queda congelada

**Cadena verificada [V]:**
1. El 2661 `0x464` (s43-activo, 20:08:54.507), recurso `0x9586FFA4`, a/b = 2/2, con los enlaces
   `0x20000004→B0101214` (gimmick), `0x20000002→A0100001` y `0x20000006→B0101313` (Matthias), todos de tipo 5. El mod
   lo crea en X. El 2981 de estado 0 se procesa a la vez.
2. X está en 3 y su tick reproduce la escena: `0xA9D860` → `0x602940`.
   - Para el nodo del jugador (`+0xAC = 0` si es `CurrentControl` [E: la clase sale del recurso; la sonda P1 lo
     confirma]), se quita el bit `0x200` del Status del Kliff remoto (no es una pose).
   - `0x6030D0`: el Kliff remoto **no** cumple vf178. Si está más lejos que el umbral de la marca de la sección, se le
     fija la transformación con `TransformSync vf1A0(…, 4, 5, 0)`.
3. A los 0,5 s se ejecutan órdenes de la etapa en X (`GuestXVeto`: `BlockInteraction on`, `UIStageGuide`): la línea de
   tiempo está corriendo.
4. La línea de tiempo de la revancha tiene eventos `SequencerControl_Input/_AI/_Off`, `PlayerCameraPreset` y
   `Talk_TwoTarget` (strings del `.paseq` en `data\`).
   - `SequencerControl_*` llama a vf21 `0x601470`. El actor del jugador se resuelve por `0x205DC30`: es el Kliff remoto
     (tipo 5, clave `A0100001`).
   - Como vf178 es falso, va por la rama de NPC: `0x9F8980(TransformSync, 1)` y el control por actor de `.xtls`.
   - En el anfitrión, el mismo evento cae en la rama del jugador local (control con entrada), y por eso allí no se
     congela.

**El último paso [H, sin medir]:** que el control de secuenciador «de NPC» de `.xtls` (`0x14F098530`, `0x14F09BBC0`)
deje al actor en manos de la línea de tiempo. Las capas de animación quedarían con la acción de la escena y los 2991
(esperas `ECF9B7D2`/`26107D3C`) no reemplazarían esa acción. Los 2697 sí se aplicarían a la posición. Coincide con el
síntoma: la posición está bien y la pose fija. Que no haya `_Off` en X se explica porque las ramas posteriores no
llegan: solo hay 2981 de estado 0 en las dos sesiones.

No se descarta: la pose podría venir de la alineación (`vf1A0` con modo 4/5), o de un evento de cuerpo o acción
(`GameCharacterPlaySequencerEventData`, `BodyAnimation`) de la pista del jugador. Todos resuelven el actor por el mismo
`0x205DC30`, así que el arreglo §6(a) los cubre igual.

Descartado [V]: `vf150(0)` del manejador del 2661 es un `ret` en el cliente.

---

## 4. Los demás paquetes de una escena en X [V]

- **2981** `0xC073A0` → `0xA9E100`; **2524** `0xC07D00` → `0xA9E480`; **2810** `0xC06330` → `0xA9D950`.
  - Buscan la escena por **id** (`0x1F6C190`). No miran los enlaces para encontrarla.
  - Las órdenes de rama que ejecutan resuelven los actores con `0x205DC30`.
  - El 2524 engancha actores a la escena viva (`.xtls 0x8F80300`, [H] por qué camino).
- Con el enlace del jugador neutralizado (§6a), las órdenes `Player.*` y los eventos de la pista del jugador no tienen
  destino. Las de los NPC, gimmicks y la etapa siguen igual.

---

## 5. Sondas propuestas (solo lectura; entradas de función con `.pdata` en `.rsrc`)

| # | Punto | Lee | Para qué |
|---|---|---|---|
| P1 | entrada de **`0x602940`** (`0x602940–0x6030C8`, primeros bytes `4C 89 4C 24 20 48 89 54`) | RCX = etapa: `+0x28` (gestor, ¿es X?), `+0x30`, `+0x60/+0x61`, `+0x98`, `[+0x48]+0x1B8` (nombre); por cada entrada de `+0x120` (número `+0x128`, paso 0x28): `[e]+0x30`, `[e]+0xAC`, `e+8`, `e+0xC`; y `vf178` del actor (réplica de `0x4FE260`: `[[0x6D691E0]+0x30]+0x58 == [a+0xA0]`) | Comprobar que en **todas** las escenas el dueño enlazado está en un nodo de tipo 0, y cuántas escenas de X tienen un nodo de tipo 0 con un actor que no es local |
| P2 | entradas de **`0x601470`** (vf21) y **`0x601C10`** (vf22) | RCX = etapa (`+0x28`, `+0x30`); R8 = descripción: `[R8+8]` → `+0x30` (ids de destino), `+0x38` (número) | Ver, a las 20:08:54, si X aplica control a la clave `A0100001`, y su fin |
| P3 | entrada de **`0x6030D0`** (contar) y su llamada `vf1A0` (punto mid que habría que verificar) | etapa y actor | Alineaciones de la copia del anfitrión |
| P4 | `GuestBodyAnim` con `guest_seq_watch=A0100001` (`pose-informe.md` §4.2) | `CC+0x70/+0x78`, capas | Ver si la pose viene de un gráfico de BodyAnim |

En los ganchos, solo lecturas protegidas, atómicos y anillos. Las líneas las escribe el hilo de diagnóstico.

---

## 6. Pregunta 4 — Opciones de raíz en el cliente del invitado

### (a) RECOMENDADA — Neutralizar el enlace del jugador cuando su actor no es el local

Se aplica a las escenas de X: tipo de enlace 5 → **0**. Es el valor que `0x205DC30` ignora y que no se promueve (solo
el 4 pasa a 5, en `0x205B4D0`).

> **Corrección tras la revisión independiente (S44, H-1) [V capstone]:** el 0 **no es inerte**. Es el estado nativo
> «nodo sin enlazar»: `0x5EB310` (la orden que crea un actor de cliente, `CreateClientActorDesc_Sequencer`) sigue
> adelante con los tipos {0,3,6,7} (`0x5EB41F`) y `0x205E520` tiene una rama propia para el 0 (`0x205E6AF`). Además el
> dueño no siempre está en el nodo de jugador (tiempo y fauna en `0x20000001`, `memory`/`quest` en `0x20000003`).
> Ningún otro valor es inerte en todos los lectores (1/2 hacen que `0x20532B0` tome su camino ligado al actor). La
> implementación mantiene el 0, pero solo en nodos que P1 ha visto que son `CurrentControl` (mapa aprendido) y con la
> sonda P4 en `0x5EB310`. Detalle y tabla de lectores en `../s44-escenas-del-anfitrion.md` §2.2-§2.3. La frase
> «todos los lectores de `+0xC` aceptan valores distintos de 4/5» de los riesgos de abajo es cierta, pero incompleta.

- **Variante a1, punto `0xC0612A`.** Es el gancho mid que ya existe en `guest_seq_route` y está verificado.
  - En ese punto: RAX = gestor, BL = kind, `[rbp−0x80]` = puntero a las entradas, `[rbp−0x78]` = número, y `[rbp+0]` =
    datos de reproducción (`+0` dueño). Las entradas son memoria propia del manejador, sin compartir.
  - Regla: si el destino es X (veredicto `Route`), la entrada con `actor == pd+0` (el dueño, que no es el personaje
    propio) pasa a tipo 0.
  - El dato es «el dueño enlazado», no la clase del nodo. Se apoya en los datos: el dueño aparece como mucho una vez
    por 2661 y siempre en una ranura baja (`0x20000001/2/3`) [V]. P1 tiene que confirmar que ese nodo es siempre de
    tipo 0.
- **Variante a2, punto en la entrada de `0x602940`.** Usa el dato nativo (`nodo+0xAC == 0` y vf178 falso), con la etapa
  en X.
  - Problema: la lista `+0x120` está protegida por `[stage+0x20]`, y `0x602940` la copia bajo ese cerrojo. Escribirla
    desde la entrada, sin cerrojo, es más arriesgado.
  - Además, otros caminos podrían leer el tipo antes de que empiece la escena (el 2524 hacia X, que hoy está en
    `probe`).
  - **Preferible a1, con P1 como validación previa.**

**Efecto [V en `.rsrc`]:**
- `0xA9CA40` no toma referencia al actor del anfitrión;
- `0x205DC30` no lo devuelve para ninguna orden ni evento de la pista del jugador;
- `0x6030D0` no lo alinea;
- `0x602940` no toca su bit `0x200`;
- `0x205F7F0`, `0x2065F40` y `0x20521E0` lo ignoran (exigen tipo 5 y nodo de tipo 1, 2 o 3).

NPC, gimmicks, tiendas, el tiempo y la etapa siguen igual. **Las tiendas** conservan `Shopnpc` y `DummyItem`; las
órdenes `Player.*` del anfitrión quedan sin destino en el invitado. Para la pieza de tenderos es justo lo que se busca:
la escena propia del invitado sigue en M, con el invitado como jugador, de forma nativa.

**2981/2810/2524 posteriores:**
- se aplican por id, igual;
- las condiciones `Player.condition_*` evaluadas en X dejan de cumplirse sobre la copia. Eso reduce, no aumenta, las
  peticiones C>S de un segundo conductor (B4 de `p8-observador-escenas.md`).

**Riesgos:**
- **[H]** El código `.xtls` que crea la etapa (`0x14DE16970`) o los eventos podría tratar mal un tipo 0, que el servidor
  nativo nunca manda. En `.rsrc`, todos los lectores de `+0xC` aceptan valores distintos de 4/5 (§1.3).
- Hay que probarlo primero en `probe`: decidir y contar, sin escribir.
- La escena de carga `0x8CD771B1` y las órdenes de cámara, interfaz o carga actúan sobre globales locales, no sobre el
  actor: (a) no las cubre. Las siguen cubriendo los vetos de función existentes (`Loading` V1, cámara e interfaz).

**Clave propuesta:** `guest_seq_foreign_player=0|probe|1`.

### (b) No crear en X las escenas personales por construcción

- **Punto:** `0xC0612A`, con `BL := 0` (el camino nativo de `kind ≠ 4`, como `gsr::refuse`). El dato nativo es
  `pd+0x11 ∈ {2, 3}`: revancha y uso de objetos.
- **No cubre:** tiendas, minijuegos, recompensas, misiones ni la cinemática de carga (todas 1/0).
- **Efecto secundario:** los NPC enlazados a esas escenas (Matthias, gimmick) se quedan sin instancia en X. Su vf13 no
  los enlaza y vuelven a su estado por defecto, como antes de la opción A. Los 2981/2524/2810 posteriores no encuentran
  el id y no hacen nada (`p8-s9-cinematica.md` §9, fila 9 [V]).
- **Uso:** complemento de (a), no sustituto.

### (c) Crear la escena sin aplicar sus efectos al actor remoto (por efecto)

- **Puntos:** entradas de vf21 `0x601470` y vf22 `0x601C10` (control), `0x6030D0` (alineación), y los eventos de cuerpo
  y acción. Todos son entradas `.rsrc` con `.pdata`.
- Es un juego del topo: hay muchas funciones que resuelven destinos (§1.4).
- Un gancho en `0x205DC30` (entrada `0x205DC30–0x205DF2E`) sería un único punto, pero exige filtrar su salida al
  volver. Es más complejo y más frágil que (a), que corta en el origen.

### (d) Descartadas

- Reescribir el enlace del jugador con el personaje del invitado: el invitado sería el «jugador» de las escenas del
  anfitrión (cámara, control). Ya estaba desaconsejado [V].
- Usar `stage+0x280` («etapa de otro usuario») como clasificador: solo se rellena en un caso.
- El filtro `0x9F8FCE0`: no clasifica (§2.2).

### Orden sugerido

1. P1 y P2 en una sesión de dos copias junto a Matthias, para confirmar el mecanismo de §3 y el tipo 0 del dueño.
2. (a1) en `probe`, y después en `1`.
3. (b) solo si la revancha sigue causando algo (por ejemplo, Matthias en otra pose).

---

## 7. No demostrado

- El efecto exacto del control «de NPC» de `.xtls` sobre la copia (§3, H) y si la pose viene de él o de la alineación.
- Quién escribe los bytes 17 y 18 en el servidor (solo se localizaron lectores: `0x287ABB0` y `0x287DFE0`).
- Que la etapa creada en `.xtls` conserve el tipo de enlace 0 (lo dirá P1 con a1 en `probe`).
- La estructura completa del `.paseq`: no se analizó entero. Las afirmaciones sobre sus eventos salen de las strings,
  no de las pistas.
