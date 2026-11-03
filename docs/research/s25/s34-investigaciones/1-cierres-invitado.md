# S34: los dos cierres del invitado (`0x88F832` y `0x1831598`), causa raíz y arreglo

Fecha: 14-10-2026. Investigación de solo lectura: no se ha editado código ni compilado, desplegado o lanzado nada.
Juego **1.0.0.2976**, timestamp PE **0x6AB28F00**, base **0x140000000**. Todas las direcciones son RVA.
Árbol: `C:\Emulador CD-npc-s33-fix` (rama `opcion-d-npc-s33-fix`, HEAD `d835806`).

Etiquetas: **[V]** visto en volcado, log o captura; **[E]** leído en el código o el decompilado, o deducción fuerte;
**[H]** hipótesis pendiente de medir.

Herramientas: scripts en el scratchpad de la sesión (`…\scratchpad\s34-crash-guest\`: `stk.py` desenrolla la pila
con `.pdata` y recupera los registros no volátiles de cada marco; `mem3.py`/`mem4.py` leen la memoria incluida en el
volcado; `split.py` busca familias partidas en las capturas; `layerscan.py`), apoyados en `crash_ue\md.py`/`walk.py`.
El Python del entorno de investigación se ejecutó con `-I -B`, junto con `tools/scripts/analyze_capture.py orphans` y
`cdmp-query.ps1` (`func`, `xrefs`, `vtable`). `cdb` no está instalado. Los volcados de `GuestS3` incluyen la memoria
alrededor de cada registro (unos 0x400 B), y eso basta para leer el actor, el CC y la capa del fallo 2.

---

## 0. Respuesta corta

- **Los dos cierres son la misma clase de fallo.** Una **familia partida entre controladores** por U-e se deshace con
  el *timeout* de 10 s de `observer_hold`, que manda al invitado, con restore 0, un hijo cuyo controlador es el anfitrión.
  1. U-e (`user2_npc_control=1`) cambia, en `0x2792A7B`, el controlador **solo del líder de tipo 3** de una stage del
     usuario 2.
  2. Sus sub-nodos (animales de tipo 6), construidos en la misma 2045 por `0x2820D80`/`0x281DBD0` como
     `CreateChildServerActorDesc_Simple`, nacen por la rama del dueño del campo y quedan con **controlador anfitrión**.
     En los logs aparecen como veredicto `not type 3`.
  3. `observer_hold` manda los hijos enseguida al cliente del anfitrión (su controlador), pero le retiene al padre
     (`WithheldUser2`). El cliente del anfitrión los recibe **huérfanos** y los destruye (`0x8B7D78`). Nadie les dará nunca
     una acción.
  4. El cliente del invitado no deja entrar al líder en el juego (`+0x5E = 8`, grupo de login pendiente) hasta que
     tiene a sus hijos, así que el líder no actúa.
  5. A los 10 s, `npc_release_step` → `Timeout` → **«released with restore 0»** al invitado. El invitado crea los hijos
     con una capa con *chart* y sin acción actual, y cualquier lector nativo cae:
     - **fallo 1:** el propio `PostUpdate` del hijo (`0x88F832`);
     - **fallo 2:** la jerarquía de **otro** actor, una pieza de tipo 7 enganchada al hijo, en `0x1831598`.
- **El arreglo de raíz no es una guarda en el invitado.**
  - El estado nulo lo leen al menos 5 funciones distintas: solo `0x1831560` tiene 10 llamadores, y hay unos 400 accesos
    en línea a `CC+0x88+8·i`. Ninguna entrada única las domina.
  - El arreglo es **«una unidad, un controlador» al nacer**: los sub-nodos heredan la asignación U-e de su líder
    (enlace nativo `desc+0x2F0` → descriptor del líder).
  - Además, **«nunca restore 0 a quien no controla»**: se quita la liberación por *timeout* con restore 0 hacia el
    usuario 2, de forma simétrica a `WithheldUser2`.
  - Como red queda la contención de `npc_activation=1`, que retira la unidad.
  - Las guardas del invitado se quedan como defensa en profundidad.

---

## a. Cada cierre: pila, invariante violado y estado del actor

### a.1 Fallo 1, `exe+0x88F832` (Shadow, 17:06:38.476; anfitrión 18:06:38)

**[V] Pila** (`cdmp_crash_20261014-170638_1704_1.dmp`, hilo 9020, desenrollado con `.pdata`):

| # | Retorno | Función |
|---|---|---|
| 0 | `0x88F832` (fallo) | `0x88F4E0` `MovementController_UpdateByMode`: lee `[[r14+0x10]]` y luego `+0x38` con `rcx = 0`. El vector `[ctx+0x10]` contiene las acciones actuales de las capas (`0x88f4e0.c:187-252`) |
| 1 | `0x88EFA7` | `0x88EB70` `MovementController_FrameUpdate` |
| 2 | `0x860EBC` | `0x860D30` `CharacterControl::PostUpdate` |
| 3 | `0x8B5773` | `0x8B56D0` `ActorUpdate_ParallelTask` (llama a `0x85FBA0` y `0x860D30`) |
| 4 | `0x13FB680` | `0x13FB350` `ParallelTaskWorker::vf3` |
| 5 | `0x1414EBF` | `0x1414DE0` `WorkerThread_Loop` → `ucrtbase` |

- **Es un hilo de trabajo del motor, no el bucle principal.** No hay ningún marco de `cdmp.asi`.
- No es un llamador de `FUN_141831560`: es el lector `0x88F4E0`, cuyo único camino es `0x860D30`.
- **[V] Actor** (memoria del volcado, `rbx` del marco 2 = `0x3637A470C00`):
  - `+0x60` = **`0xB01004A5`**;
  - `+0x90` = **`0x90100000`** (el anfitrión);
  - `+0xAC` = **`0xB01004A4`**.
  - El CC, `0x36365305C80` (vtable `0x55B5460`), apunta a ese actor.
- **[V] Estado según el log del invitado** (`shadow\cdmp_20261014-170438.log`, líneas 5684, 5781-5783):
  - `layer 0 has a chart and neither a current action (+0x10) nor a playback (+0x18)`;
  - tipo 6, controlador anfitrión, padre `0xB01004A4`, que controla el invitado;
  - el 2005 llegó a las 38.163, y la guarda de capas (`0x2329700`) y `Puppet` actuaron a las 38.466/38.468.
  - Esta build **no tenía** `guest_remote_update_guard`, por eso el `PostUpdate` no se saltó.
- **Invariante nativo violado:** «una capa con *chart* tiene acción actual».
  - Lo garantiza `RestoreActionsFromLoadData` `0x8646D0` solo cuando el byte de restauración `[[CC+0x40]+0x30]` vale 1
    (`0x8646d0.c:74`: con 0 vuelve sin `RestartLayerAction`).
  - En el juego nativo, que es de un jugador, un cliente que no controla un actor **nunca** lo recibe con restore 0.

### a.2 Fallo 2, `exe+0x1831598` (Shadow, 18:07:10.241; anfitrión 19:07:10)

**[V] Pila** (`cdmp_crash_20261014-180710_6364_1.dmp`, hilo 5104, **bucle principal**):

| # | Retorno | Función |
|---|---|---|
| 0 | `0x1831598` (fallo) | `0x1831560`: `this = [CC+0x40]` del padre; capa `[CC+0x88+8·[[CC+0x30]+0x1B]]`, acción `+0x10` = 0, lectura de `+0x10B` |
| 1 | `0x439B85` | `0x439AB0` `ClientHierarchyActorComponent` vf+0x150 (llamada `0x439B80`) |
| 2 | `0x15ECF73` | `0x15ECA10`: busca el padre por la clave `[hier+0x1C]` y llama a vf150. `rsi` = `0xB0100578`, la clave propia |
| 3 | `0x99A3454` | `.xtls` (por el *thunk* `0x8F7260`) |
| 4 | `0x8F7990` | `0x8F7780` |
| 5 | `0x8DA224` | `0x8D8940` `ClientGimmickActorComponent` vf+0xC0 |
| 6 | `0x4FE095` | `0x4FDDF0`, despachador vf+0xC0 de los componentes (llama a `ActorLogin_BuildGroup`); `r15` = el actor de `0xB0100578` |
| 7–18 | `0x98CB108`, `0x140F1C8`, `0x141145D`, `0x13F885D`, `0x13F50E1`, `0x140496F`, `0x6173E9`, `0x3F412E6`, `0x14C8803A`, `0x3F44B28`, `0x86D2ADC`, `0x4895C3E` | Bucle principal y arranque |

**El llamador es `0x439B80`.** Es la misma cadena que el cierre del ESC (`p8-crash-esc-1831598.md` §1.3).

**[V] Actores** (memoria del volcado):

| Objeto | Dirección | Contenido |
|---|---|---|
| Actor que falla (`r14`) | `0x3F3DB100F00` | `+0x60` = **`0xB01004BF`**, `+0x90` = **`0x90100000`**, `+0xAC` = **`0xB01004BE`** |
| Su CC (`rdx`) | `0x3F3DB25B180` | vtable `0x55B5460`; `+0x8` → ese actor; `+0x88` capa 0 = `0x3F3D536D480`; `+0x1E8/9` = 0 |
| Capa 0 | `0x3F3D536D480` | `+0x10` (acción) = **0**, `+0x18` (reproducción) = **0**, `+0x110` (*chart*) = `0x3F3CECB0780`, que **no es nulo**; `+0x3C` hash `0x866C7489` |
| Capa 1 | | Sin *chart* |
| `this` (`rbx`) = `[CC+0x40]` | `0x3F398A6B6E0` | `[+0]` = el actor; `+0x30` (restore) = **0** |
| `rsi` | `0x3F3DA914540` | `ClientHierarchyActorComponent` (vtable `0x5578BB8`) de `0xB0100578`, el actor `0x3F3DA87A680`: tipo 7, `+0x90` anfitrión, `+0xAC` = 0; `[hier+0x1C]` = **`0xB01004BF`** |

**Corrección al log.**
- La línea `GuestS3 … rsi … looks like a CharacterControl: actor … key 0xB0100578 … L0 chart 0 cur 0 play 0` se equivoca:
  `rsi` es el componente de jerarquía del **hijo** que consulta, no un CC, y sus «capas» son basura.
- El actor sin acción es **`0xB01004BF`, con *chart***, el mismo estado que en el fallo 1.
- `guest_remote_update_guard` **sí lo estaba reteniendo** (`RemoteUpdateGuard: holding actor 0xB01004BF`, línea 6959).
  El nulo se leyó desde la actualización de componentes de **otro** actor, la pieza `0xB0100578`, que llegó a las
  18:07:10.030, 211 ms antes del cierre.
- No es un caso «sin *chart*»: la guarda cubre solo `0x85FBA0`/`0x860D30` del propio actor.

**[V] Estado de `0xB01004BF`** (log del invitado `shadow-p1b-p2b\cdmp_20261014-180502.log`):
- 6931: llega a las 18:07:08.345, tipo 6, controlador `0x90100000`, padre `0xB01004BE`, que es del invitado;
- 6944: presente, IA y *stage* sí, `actor5E 16`;
- 6959: retenido por la guarda (capa con *chart* y sin acción);
- 7055-7058: el cierre.

**Invariante violado:** el mismo que en el fallo 1. El lector `0x1831560` no comprueba nada: para el motor, una capa
siempre tiene acción.

---

## b. Por qué el anfitrión publica esos actores en ese estado

### b.1 Qué son los de tipo 6 y 7 y quién los crea

- **[E] Los hijos de tipo 6** son **sub-nodos de un líder** de horario.
  - `0x2819E00`, en modo 3, crea el líder (`0x281CEE0`, retorno `0x281A4B5`). Después, `0x2820D80` construye sus
    sub-nodos (retorno `0x28210B5`; los índices son `+0xB0`+1…).
  - Cada sub-nodo es un `CreateServerActorDesc_NPCSchedule` (`0x5B04CB0`) envuelto en
    **`pa::CreateChildServerActorDesc_Simple`**:
    - **`[desc interno + 0x2F0] = descriptor del líder`**;
    - el líder guarda la lista de hijos en `+0x2F8` (`0x2820d80.c:96-121`; igual en `0x281DBD0`, `0x281dbd0.c:102-121`).
- **[V] Ejemplos en los logs:**
  - stage `0x20000169`, recurso `0x79AF6B66`:
    - nodo 916, retorno `0x281A4B5` → líder `0xB01004A4`;
    - nodo 920, retorno `0x28210B5` → hijo `0xB01004A5` (`180308.log` 9399-9400);
  - recurso `0x8F88BCF7`: nodo 0 → `0xB01004BE`; nodos 4 y 8 → `0xB01004BF`/`C0` (`190232.log` 14012-14014).
- **Recursos distintos afectados hoy:** `0xBB0F9260`, `0xD2D6C125`, `0x79AF6B66`, `0x8F88BCF7` y `0x1295A470`. No es
  un NPC concreto. Que sean animales que acompañan a un NPC es [H]: por ejemplo, el «niño y gato» de
  `Pruebas visuales`.
- **[E] El controlador al nacer** (`0x2791cd0.c:625-671`): `desc+0x40`, después el dueño `actor+0xA0` y por último el
  **dueño del campo `field+0x180`**.
  - No hay herencia del padre. En el juego nativo no la necesita, porque todo el campo tiene el mismo controlador.
- **[E] U-e** (`user2_stages_logic.h:709-725`) solo asigna `type == 3`. El sub-nodo pasa el filtro de descriptor y cae en
  **`Ctl::Type` («not type 3»)**: 9 en la prueba 1, 6 en el paso 1b y 11 en el 2b (resúmenes de U-e de los tres logs del
  anfitrión). Se queda con el anfitrión. **Es la familia partida.**
- **[E] Los de tipo 7** son `pa::CreateServerActorDesc_InstantGimmick` (vtable `0x5B04C68`, consultado con
  `cdmp-query vtable`): el veredicto «another descriptor», ×9, ×6 y ×16.
  - `0xB0100578` nace 11,8 s después, con `+0xAC` = 0 y padre de jerarquía `0xB01004BF`.

### b.2 La cadena, con evidencia (ejemplo del fallo 2; el 1 es igual)

| Paso | Evidencia |
|---|---|
| 1. U-e asigna el líder `0xB01004BE` al usuario 2; los hijos `BF`/`C0` nacen con controlador anfitrión | `190232.log` 14026-14029 (`[familia] … controlador=0x90100000 … tipo=6`) y 14116 (`[control]` del líder) |
| 2. `observer_hold`: el líder va al invitado y se retiene al anfitrión; los hijos van al anfitrión (su controlador) y se retienen al invitado | 14842-14844 (`still held … released with restore 0 at 10000 ms`); 16616 (`withheld from the host … never released to the host with restore 0`) |
| 3. **El cliente del anfitrión recibe los hijos huérfanos** (su padre aún no está) y los destruye | Captura del anfitrión, `orphans`: `B01004BF`/`C0` con el padre +10,377 s; también `B010048F`/`90` y `B0100577`. Prueba 1: `B01004A5`, `B010049E` y `B01003CD`. `anfitrion estado … presentes actor false` y `primera-Req-us 0` en toda su vida (17401-17403) |
| 4. **Espera familiar en el invitado** [V, ya no [H]]: el líder sigue con `+0x5E = 8` hasta que tiene a sus hijos | Ver los cuatro casos debajo de la tabla |
| 5. Se rompe el bloqueo con el *timeout* de 10 s y restore 0 | `npc_release_step`, `child_hold.cpp:621-638` → `NpcRelease::Timeout`. Contador `after the timeout with restore 0`: 0 → 1 → 3 → 5 → 6 en el paso 2b, y 2 en la prueba 1 |
| 6. El anfitrión no arregla nada al llegar el líder: su copia se queda con `actor5E 8` y `hijos-mascara 0/2` | `190232.log`, `anfitrion estado` de `B01004BE` a las 19:07:09.476 |

En el paso 4 se ve en cuatro familias. En cada una, el líder hace su primera Req unos 10,24–10,31 s después de su
2005 y entre 160 y 217 ms después de que lleguen sus hijos:
- `B01004A4`, prueba 1;
- `B010049D`, prueba 1;
- `B010048E`, paso 2b;
- `B01004BE`, paso 2b: `actor5E 8` (líneas 5786 y 6940) y `16` en 6944.

`B01003CC`, que nunca recibió a su hijo, no actuó y fue retirado a los 5,7 s.

### b.3 ¿Los moverá alguna vez su controlador, el cliente del anfitrión?

**No.** No es por la distancia: su copia ya no existe, porque la destruyó como huérfana. Nadie vuelve a anunciárselos,
porque el servidor cree que ya los entregó. [V]
- `primera-Req-us 0` y `presencia-us 0` hasta su baja, tras 18–31 s de vida.
- Ese mismo cliente, lejos, sí mueve familias suyas que recibe **completas**:
  - `B01004F6`/`F7`/`F8` hacen su primera Req a 137 ms y `B01004FA`–`FD` a 159 ms (`180308.log`);
  - los de tipo 6 `B0100391`–`97` se liberan con restore 1 a los 172 ms.

---

## c. El arreglo de raíz

### c.(iii) Una sola guarda en el invitado: **no existe un punto que lo domine todo** [V/E]

Lectores conocidos de la acción o la reproducción nulas:

| Lector | Desde dónde |
|---|---|
| `0x232D5FF` | `0x232D5C0` ← `0x232D2A0` ← `0x85FC94`/`0x860110` (dentro de `0x85FBA0`) |
| `0x88F832` | `0x88F4E0` ← `0x88EB70` ← `0x860D30` |
| `0x1F63FE6` | `0x1F63F70` ← `0x2329700` |
| `0x17F328F` | `0x17F3240` ← `0x630BF0` ← 3095 `0xB74AB0` (un manejador de red) |
| **`0x1831598`** | **`0x1831560`**, 10 llamadores: `0x439B80` (jerarquía), `0x70E29F`, `0x944CB5`, `0x9E916B` (icono del minimapa), `0x124136B`, `0x17031D1`, `0x17133B3`, `0x17135BC`, `0x1713CB6`, `0x22E52BD` (`cdmp-query xrefs`) |

- Todos leen la capa **en línea**: no hay ningún *accessor* compartido.
- Un recuento heurístico de `mov r64,[r+r*8+0x88]` da **395 sitios en `.rsrc` y 22 en `.xtls`**.
- **El único punto común es el que produce el estado**, `0x8646D0` con el byte 0. Esa variante (c′) la rechazó la
  revisión de `companeros.md` §B.2: el lock se suelta antes de `863880 → 85ED00 → 2329D70`, inventa un estado inicial y
  no debe tapar una acción auténtica.
- En la clase A no llegará nunca ninguna acción auténtica: un hijo con una acción fabricada quedaría quieto para
  siempre.
- Un *mid-hook* en `0x1831594` (`p8-crash-esc` §5.3) cubre los 10 llamadores de `0x1831560` y nada más.
- **Conclusión:** sirve como defensa en profundidad, no es un arreglo.

### c.(i) Quitar la liberación por *timeout* con restore 0 hacia el usuario 2, sola

- **Sin cierre, pero no es raíz.** Los hijos quedan retenidos (invisibles) para siempre, el líder no forma su grupo de
  login (`+0x5E = 8`) y no actúa.
  - Con `npc_activation=1`, la contención retira la unidad a los 5 s (motivo 25, dos intentos por nodo y después veto):
    la familia desaparece del invitado.
  - En `probe`/`0` vuelve el aborto global antiguo (fallo A de S33).
- La clase B (abajo) quedaría invisible hasta que el anfitrión le dé una acción aceptada.
- **Es necesaria como invariante**, pero no basta sola.

### c.(ii) Autoridad coherente por unidad: **la raíz** [E; falta confirmar en `probe`]

- Si la unidad entera (líder y sub-nodos) tiene el mismo controlador:
  - el cliente del invitado la recibe completa enseguida (es su controlador: `its controller got it now`);
  - el grupo de login se forma, la IA arranca a unos 0,2 s, como en las familias completas del anfitrión
    (137–200 ms) y en los 261/359 líderes U-e de hoy;
  - el anfitrión recibe la familia con restore 1, **sin huérfanos**, porque `observer_hold` + `WithheldUser2` + la
    retención familiar ya hacen eso.
- **[H] Lo que falta medir:** que el cliente del invitado dé su primera acción a los sub-nodos de tipo 6. Es el mismo
  código que ya lo hace en el anfitrión para sus familias.
- Si no lo hace, esos hijos quedan retenidos al anfitrión (`WithheldUser2`, nunca con 0). El líder del anfitrión se
  quedaría en `+0x5E = 8`, invisible allí, pero sin cierre en ningún lado.

### c.(iv) Recomendación: (ii) + (i) generalizada, con la contención como red

**R1. Herencia familiar en U-e al nacer.** Mismo *mid-hook* de `0x2792A7B`, sin enganche nuevo: `npc_control_impl`
(`user2_stages.cpp:1136`) y `ctl_verdict` (`user2_stages_logic.h:709`).
- Cuando el veredicto es `Assign` (el líder), se apunta `(hilo, Troc/opcode en curso, puntero desc = rsi, clave,
  generación)` en una tabla pequeña de ese hilo, válida solo durante ese Troc.
- Veredicto nuevo **`AssignFamily`**, con las mismas condiciones que `Assign` salvo `Type`:
  - 2043/2045/2046 del usuario 2, `own_mgr_troc`, armado y no abortado;
  - `old_ctl == 0`, `desc+0x40 == 0`, `actor+0xA0 == 0`, campo del anfitrión y `eax == field+0x180`;
  - **y `[desc+0x2F0]`** (lectura SEH) es igual al desc de un líder asignado en **este mismo Troc y hilo**.
  - Entonces `eax := key2`. Vale para cualquier tipo con descriptor de horario; los de tipo 7 se miden primero.
  - El puntero solo se usa como identidad local dentro de una llamada nativa: nunca entre PC.
- **[E] Por qué `desc+0x2F0` y no `+0xAC`.** El vínculo `+0xAC` del hijo aún no existe en `0x2792A7B`: la línea
  `[familia]` sale al publicar, con `changed`. `+0x2F0` lo escriben `0x2820D80`/`0x281DBD0` al crear el descriptor,
  antes de crear los actores, porque los `[nodo observado]` preceden a los `[nacimiento]`.
- Las claves se marcan `kKeyAssigned` + un indicador nuevo de **miembro**: forman parte de la unidad de `npc_activation`
  (retirada conjunta) y no cuentan como `unaccepted` para el aborto antiguo, igual que `kKeyAbandoned`.
- **Despliegue en dos fases:** primero `npc_family_control=probe`, con líneas «would inherit (leader 0x…, child 0x…,
  type, +0x2F0 match)»; después `1`. Es exactamente el papel que se le había dado a esa clave, «no autorizado» en S33.

**R2. «Nunca restore 0 a quien no controla».** En `npc_release_step` (`child_hold.cpp:628-638`), el `Timeout` hacia la
sesión del usuario 2 pasa a `Keep` con un tipo nuevo, p. ej. `WithheldNoAction`, igual que `WithheldUser2`
(`:632`).
- Sigue cayendo por las mismas vías: otra época, *despawn*, observador fuera de rango.
- Con un contador y unas pocas líneas WRN.
- **Excepción opcional [H]**, solo tras medirla: liberar si `observer_hold_acks` ya tiene retenido un 2723 o un 2991 de
  esa clave, que se encolan justo detrás del 2005. Es la nota «ringneck» del 09-10; hoy no aparece en ningún caso.
- Invariante nuevo en el anfitrión: **huérfanos encolados a la sesión controladora = 0**. Hoy hay 3, 5 y 1, por
  `orphans` en las capturas del anfitrión.

**R3. Red de seguridad.**
- Si R1 se salta algún sub-nodo, R2 lo retiene y `npc_activation=1` retira la unidad: sin cierre y sin bucle global.
- `guest_remote_update_guard` (`d835806`), la guarda de capas de `GuestDiag` y `guest_update_guard` se quedan como
  defensa en profundidad.
- Corregir la atribución de `GuestS3`: tomar el CC de `rdx`/`[[r14+0x68]+0x40]` y no de `rsi`.

**Qué sustituye:**
- la liberación por *timeout* con restore 0 hacia el usuario 2;
- la regla de U-e «solo tipo 3» para los sub-nodos;
- `guest_remote_update_guard` como arreglo de `0x88F832`, que pasa a ser solo red.

**Efectos esperados:**
- las familias de las stages del usuario 2 aparecen enteras y actúan en unos 0,2 s, y no a los 10 s;
- el anfitrión las ve con restore 1 y su grupo de login completo (hoy, la copia del líder se queda en `+0x5E 8`);
- la clase B queda invisible en el invitado hasta que actúe;
- coste: una lectura SEH más por nacimiento bajo los temporizadores armados.

**Riesgos que mide `probe`:**
- que el desc de `0x2792A7B` sea el interno (lo indica el recuento `not type 3`);
- que el líder nazca antes que sus hijos dentro del Troc;
- que el cliente del invitado actúe sobre los de tipo 6.

---

## d. Exposición: actores que llegaron al invitado con restore 0 y un controlador distinto del usuario 2

| Vía | Prueba 1 (cierre 1) | Paso 1b (sin cierre) | Paso 2b (cierre 2) |
|---|---|---|---|
| **P1** `observer_hold` `Timeout` con restore 0 (contador del anfitrión) | **2**: `B010049E`, `B01004A5` (clase A) | 0 | **6**: `B01001F6` (clase B); `B0100490`, `B010048F`, `B01004C0`, `B01004BF` (clase A); el 6.º, después del cierre: `B0100577` [E, por la hora de su huérfano +10 s] |
| P2 `observer_hold_family`, huérfano entregado de forma nativa | 0 | 1: `B01003AE` (tipo 6, anfitrión, +20,07 s). El cliente lo destruye: sin capas, sin cierre | 2 de tipo 7 (`B0100200`, `B01001FB`), sin acciones |
| P3 tipo 7 / InstantGimmick (sin byte de restore) con controlador anfitrión y padre del usuario 2, al instante | 1 (`B01003EB`) | 1 (`B010032A`) | 6; además `B0100578`, que llegó por su padre de jerarquía `B01004BF` |
| `table full → native`, `without entry`, `natively instead of dropped`; *timeout* de `character_hold` | 0 | 0 | 0 |

- **Corroboración en el invitado:**
  - contador `restore … byte 0`: 3 a las 18:07:08.167 en el paso 2b, más 2 a las 08.345; 0 en el paso 1b;
  - líneas de guarda: prueba 1, `GuestDiag guard` + `Puppet` de `B01004A5`; paso 2b, `RemoteUpdateGuard` de
    `B01001F6`, `B010048F`, `B0100490`, `B01004BF` y `B01004C0`.
- **Capturas del invitado**, familias partidas (`split.py`: hijo con controlador anfitrión y padre del invitado):
  - prueba 1: `B010049E` y `B01004A5`, ambos con +10,08 s, más un tipo 7;
  - paso 2b: `B010048F`/`90` (+10,05 s) y `B01004BF`/`C0` (+10,09 s), más 6 de tipo 7;
  - paso 1b: `B01003AE` (+20,07 s), más un tipo 7.
- **Total de hoy con riesgo de cierre:** 8 liberaciones con restore 0, **7 de la clase A y 1 de la clase B**. 2 de ellas
  cerraron el juego.
- **Clase B:** el anfitrión controla el actor y lo tiene, pero no consigue que se acepte una acción.
  - Caso de hoy: `B01001F6`, miembro de un viajero.
  - Su cliente solo envía una 2291 a los 178 ms y nunca una acción aceptada (`190232.log` 2830-2950 y 5681/6826).

---

## Fuentes principales

- **Volcados y logs del invitado:**
  - `backups\p8-sesion34\shadow\` (`cdmp_20261014-170438.log`: 2810-2811, 5684, 5772, 5781-5783);
  - `backups\p8-sesion34\shadow-p1b-p2b\` (`cdmp_20261014-180502.log`: 5768-5786, 6931-6959, 7055-7059).
- **Logs del anfitrión:**
  - `cdmp_20261014-180308.log`: 9102-9107, 9395-9424, 9554, 14094-14105, 15221-15222;
  - `paso2b\cdmp_20261014-190232.log`: 13891-13892, 14012-14116, 14842-14844, 16616-16706, 17009, 17401-17438;
  - `paso1b\cdmp_20261014-184959.log`: 9543-9853, 12121, 13284.
- **Decompilados:** `0x2791cd0.c`, `0x2820d80.c`, `0x281dbd0.c`, `0x1831560.c`, `0x8646d0.c`, `0x88f4e0.c`.
- **Código:** `child_hold.cpp:548-648`, `child_hold.h:44-72` y `:256-266`, `user2_stages_logic.h:663-725`,
  `user2_stages.cpp:1136-1230` y `:3240-3268`, `observer_hold_acks.h`.
