# S34 — Familias divididas por U-e: censo, vínculo nativo y diseño de `npc_family_control=1`

Fecha: 14-10-2026. Investigación **de solo lectura**: no se ha editado código, compilado, desplegado ni abierto el juego. Worktree `C:\Emulador CD-npc-s33-fix` (rama `opcion-d-npc-s33-fix`, HEAD `d835806`). Juego **1.0.0.2976**, PE `0x6AB28F00`, base `0x140000000`; todas las direcciones son **RVA de esta build**.

Etiquetas: **[V]** visto en log/captura; **[E]** leído en el exe/decompilado o en el código; **[H]** hipótesis pendiente de medir.

Fuentes (reloj del invitado = anfitrión − 1 h):

| Ensayo | Anfitrión | Invitado (Shadow) | Claves |
|---|---|---|---|
| A (paso 1) | `p8-sesion34\cdmp_20261014-180308.log` | `shadow\cdmp_20261014-170438.log` + `cdmp_packets_20261014-170455.bin` | user 2 `0x9010029A`; `npc_activation=probe`; **CONTROL ABORT 18:06:31.870** |
| B (paso 1b) | `paso1b\cdmp_20261014-184959.log` | `shadow-p1b-p2b\cdmp_20261014-175039.log` + `…175057.bin` | user 2 `0x901001D4`; probe; **CONTROL ABORT 18:52:36.112** |
| C (paso 2b) | `paso2b\cdmp_20261014-190232.log` | `shadow-p1b-p2b\cdmp_20261014-180502.log` + `…180517.bin` | user 2 `0x90100249`; `npc_activation=1`, `npc_family_control=probe`; **cierre del invitado `exe+0x1831598` 18:07:10.241** |

Scripts del censo (Python aislado `-I -B`, sólo lectura): `build/research/s34/scripts/` — `fam_guest.py` (líneas `NpcProbe [invitado …]`), `names2005.py` (registros 2005 de las capturas: tipo `r[0]`, clave `+1`, controlador `+0x12`, nombre UTF-16 `+30/+34`, padre/hijos en la cola del registro, con `cap.py`/`nm_cap.py` de `build/research/s33-npc-20261014/b/dependencias`), `census.py`.

---

## 0. Resumen

1. **[V] Los tres ensayos de S34 reproducen el mismo mecanismo que el pastor de S33.** Una raíz tipo 3 asignada a user 2 con hijos **tipo 6** controlados por el anfitrión:
   - el anfitrión retiene a esos hijos para user 2 (observador, restore 0) y sólo los suelta al plazo de **10 s con restore 0**;
   - **la raíz no emite su primera Req hasta que sus hijos existen en el cliente del invitado**: 4/4 casos medibles empiezan **+0,16…0,22 s después de llegar el último hijo** y 10,2…10,3 s después de su propia alta;
   - en A y B esa espera supera los 5 s del vigilante y **dispara el aborto global de U-e**: raíz `B01003CC` «Boy» + hijo tipo 6 `B01003CD` en A, y raíz `B01003AD` «Hernandian Guard» + «Greyhound» `B01003AE` en B;
   - después, el hijo se publica al invitado **como huérfano**, 14–15 s tras la baja de su raíz.
2. **[V] Lo que llega con restore 0 cierra el invitado.**
   - A: `0x88F832` sobre el «Greyhound» `B01004A5`, tipo 6, controlado por el anfitrión, hijo de un guardia de user 2. Llegó 10,08 s tarde y sin acción.
   - C: los 4 hijos tipo 6 (cabras de dos «Stranger») llegaron +10,04/10,09 s con restore 0 y `RemoteUpdateGuard` los contuvo los 4. Después hubo un cierre en `0x1831598`: `rsi` es el CharacterControl de `B0100578` (tipo 7 «Game Mechanic», raíz, anfitrión, sin acción) y `r13 = B01004BF`, una de esas cabras.
3. **[E] El exe enlaza padre e hijo a nivel de descriptor antes de crear los actores.**
   - Al construir el hijo: `[desc_hijo+0x2F0] = desc_padre` y un `push_back` del envoltorio `CreateChildServerActorDesc_*` al vector `desc_padre+0x2F8`. Ocurre en todos los constructores de envoltorios encontrados.
   - El padre pasa por `0x2792A7B` **antes** que sus hijos, en el mismo hilo y en el mismo Troc [V].
   - En ese momento el hijo aún tiene `+0xAC = 0`: 0 de 878 nacimientos lo traían [V]. La clave del padre no está disponible, **pero el puntero a su descriptor sí**.
4. **[E] Tipo 7 / vtable `0x5B04C68` es `pa::CreateServerActorDesc_InstantGimmick`.**
   - RTTI `.?AVCreateServerActorDesc_InstantGimmick@pa@@` (`0x6BDA838`), TypeDescriptor `0x6BDA828`, 8 slots.
   - vf3 `0x27A4A40` llama primero a `0x2791CD0`.
   - El constructor está en `.xtls 0x100F8790` y llama al constructor base `0x278EA70`, que pone `+0x2F0 = 0` y vacía `+0x2F8`.
   - En S34 son los «Game Mechanic» hijos de Peddler, Deer Porter, Apothecary, Stranger y Wandering Freesword: accesorios, no IA.
5. **Diseño `npc_family_control=1`.** Usa el mismo `create_mid 0x2792A7B` y una tabla por hilo y Troc: `desc → actor` de las raíces asignadas.
   - Se asignan a user 2 los hijos con envoltorio `CreateChildServerActorDesc_Simple` y descriptor interior `NPCSchedule`, tipos 3 y 6, con el vínculo demostrado (`+0x2F0`, pertenencia a `+0x2F8` y clave del padre revalidada). Son los hijos de horario de la misma stage que el invitado ya tiene ligada.
   - Los gimmicks tipo 7 se quedan con el anfitrión, marcados como *host-kept*: nunca se publican a user 2 con CC y restore 0.
   - Las raíces cuyo subárbol contiene envoltorios de montura, vehículo, jinete, docking, guardado o captura no se asignan: la unidad entera queda coherente en el anfitrión.
   - La contención retira y juzga **por unidad (raíz)**.
6. **[V/E] Fallo lateral crítico de `npc_activation=1`.** En C, las **359/359** acciones `OnServer` tienen `aplicada=1 progreso=0 emisor=0x00000000`; también ocurre en A (261/261) y B (223/223).
   - El emisor se lee mal en `npc_action_probe.cpp:41` (`[[session+0x90]+0x60]`) y `applied_to` exige `sender == controller`.
   - Resultado: el progreso nunca cuenta y la contención retiró NPC que ya actuaban. Hay 99 retiradas de unidad en C, entre ellas `B0100428`, con la acción aplicada a 2,8 s y retirada a 11,5 s.
   - El nativo `0x2A78CA0` comprueba `controlador == usuario de la sesión` antes de OnServer (`0x2a78ca0.c:39-44`). Por eso alcanzar H1b ya demuestra que el emisor es el controlador. **Hay que corregirlo antes de cualquier prueba con familias.** Detalle en §5.

---

## 1. Censo S34: actores nacidos en las stages armadas de user 2

### 1.1 Lo que U-e vio en `0x2792A7B` bajo el sujeto de user 2 (anfitrión, último resumen)

| Ensayo | asignados (tipo 3, NPCSchedule) | «not type 3» (NPCSchedule, otro tipo) | «another descriptor» | abortados | sin temporizador |
|---|---:|---:|---|---:|---:|
| A | 264 | 9 | 9 × `0x5B04C68` tipo 7 | 27 | 88 |
| B | 228 | 6 | 6 × `0x5B04C68` tipo 7 | 1143 | 1027 |
| C | 386 | 11 | 16 × `0x5B04C68` tipo 7 | 0 | 177 |

**[E]** `ctl_verdict` (`user2_stages_logic.h:710-726`) evalúa `OtherDesc` y `Type` en este orden:
- `OtherDesc` se decide **antes** de comprobar dueño y controlador.
- `Type` se decide **después** de la rama del dueño del campo, `+0xA0`, `+0x40` y el controlador previo. Por eso los 9/6/11 «not type 3» cumplen ya todas las condiciones salvo el tipo: son hijos de horario de la propia stage.

«abortados» cuenta los nacimientos tras el aborto global. No hay línea por clave de los rechazos, así que el resto se identifica por la familia (§1.2).

`assigned 2005s with restore 0 at the host's client` = **0** en los tres ensayos [V]: la protección de la raíz hacia el anfitrión funciona.

### 1.2 Familias cuya raíz es de user 2, según el invitado (2005 recibidos)

| Ensayo | raíces tipo 3 de user 2 | con hijos | enteras en user 2 | **partidas** | hijos tipo 3 (user 2) | hijos tipo 6 (anfitrión) | hijos tipo 7 (anfitrión) | nunca recibidos |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| A | 220 | 36 | 33 | 3 (+1 retirada antes) | 44 | 2 (+1 `B01003CD`) | 1 | 1 |
| B | 202 | 24 | 22 | 2 | 26 | 1 | 1 | 0 |
| C | 304 | 49 | 41 | 8 | 57 | 4 (+1 `B0100577`) | 6 (uno a profundidad 2) | 1 |

Ningún hijo tipo 4 (personaje con nombre) ni tipo 5 (montura) cuelga de una raíz de user 2 [V]. Los tipo 4 y 7 controlados por user 2 son **suyos** y no de la stage: compañeros «Damiane» y «Oongka» y 25 «Game Mechanic» de su equipo, con dueño o controlador nativo.

Los «not type 3» y «another descriptor» que no aparecen como hijos (A 6+8, B 5+5, C 6+10) son raíces tipo 6 o tipo 7 de stages de user 2, o hijos de raíces no rastreadas. No hay línea por clave, así que no se identifican una a una. `B0100578`, el tipo 7 del cierre de C, nació entre los nacimientos 1271 y 1276 del mismo hilo `T8632` [H: probablemente en el mismo Troc 2045].

### 1.3 Qué son, quién los controla y qué llegó con restore 0

| Ensayo | Raíz (user 2) | Hijo | Tipo / controlador | Llega al invitado | Primera Req de la raíz | Resultado |
|---|---|---|---|---|---|---|
| A | `B01003CC` Boy | `B01003CD` (tipo 6) | 6 / anfitrión | retenido («waits for its parent»), encolado +20,1 s | ninguna | **raíz sin primera acción a 5,74 s → CONTROL ABORT**; baja 25 de la raíz; hijo huérfano encolado tras el cierre del invitado |
| A | `B010049D` Girl | `B010049E` Beagle | 6 / anfitrión | +10,08 s, restore 0 | +10,24 s (0,16 s tras el hijo) | perro sin acción |
| A | `B01004A4` Hernandian Guard | `B01004A5` Greyhound | 6 / anfitrión | +10,08 s, restore 0 | +10,25 s (0,18 s tras el hijo) | **cierre `0x88F832` sobre `B01004A5`** (17:06:38.476) |
| A | `B01003EA` Stranger | `B01003EB` Game Mechanic | 7 / anfitrión | con la raíz (+0,03 s) | +2,92 s | normal |
| B | `B01003AD` Hernandian Guard | `B01003AE` Greyhound | 6 / anfitrión | retenido; +20 s (huérfano) | ninguna | **CONTROL ABORT** (raíz baja 25 a 5,2 s); hijo publicado 14,9 s después de la baja |
| B | `B0100329` Peddler | `B010032A` Game Mechanic | 7 / anfitrión | con la raíz | +3,14 s | normal |
| C | `B010048E` Stranger | `B010048F` Goat, `B0100490` Baby Goat | 6 / anfitrión | +10,04 s, restore 0 | **+10,25 s (0,21 s tras los hijos)** | ambos contenidos por `RemoteUpdateGuard` (sin acción) |
| C | `B01004BE` Stranger | `B01004BF` Goat, `B01004C0` Baby Goat | 6 / anfitrión | +10,09 s, restore 0 | **+10,31 s (0,22 s tras los hijos)** | contenidos por `RemoteUpdateGuard`; `r13=B01004BF` en el cierre `0x1831598` 1,7 s después |
| C | `B0100576` Stranger | `B0100577` (tipo 6) | 6 / anfitrión | no llegó antes del cierre | — | — |
| C | Deer Porter ×2, Peddler ×3, Apothecary | 6 × Game Mechanic (`B010042A`, `B0100471`, `B0100475` a profundidad 2, `B010049F`, `B01004C6`, `B0100573`) | 7 / anfitrión | con la raíz | +0,23…2,54 s | normal; el **cliente anfitrión** emite 2556 para ellos en un punto fijo del puesto, p. ej. `(-10634.986,613.5,-3856.007)` |

Referencia del mismo ensayo [V]:

| Ensayo | Familias enteras en user 2, primera Req (mediana / máx.) | Raíces sin hijos, primera Req (mediana / p95) |
|---|---|---|
| A | 2,90 / 5,00 s | 3,19 / 5,05 s |
| B | 2,11 / 3,13 s | 2,10 / 3,10 s |
| C | 1,23 / 2,64 s | 1,07 / 2,59 s |

**Conclusiones [V]:**
- Los hijos **tipo 6** de una raíz de user 2 son animales de la propia actividad del NPC: perros de guardias y niños, cabras y ovejas de pastores. En S33 fueron 14 «Sheep» del Herder `B01004C8`. Siempre los controla el anfitrión y llegan al invitado con restore 0 a los 10 s, o como huérfanos tras la baja de la raíz.
- Con 4/4 casos, la raíz **espera a sus hijos**: no arranca hasta tenerlos en el cliente que la controla. La hipótesis [H] de S33 sobre el pastor queda respaldada.
- Los hijos **tipo 7** llegan junto con la raíz (sin retención; `HoldWhy::NoCc`, `child_hold.cpp:147-149`) y no la retrasan.

### 1.4 Tipo 7 / vtable `0x5B04C68`

| Dato | Valor [E] |
|---|---|
| Clase | `pa::CreateServerActorDesc_InstantGimmick` (derivada de `pa::ICreateServerActorDesc`, vtable `0x5B04BE8`) |
| RTTI | nombre `0x6BDA838`, TypeDescriptor `0x6BDA828` (`exports/rtti_classes.csv`) |
| Slots | `0x27A49E0` (thunk → `.xtls 0x100F99F0`), `0x2792FD0` (= base), `0x35EA90`, **vf3 `0x27A4A40`**, `0x27A5CC0`, `0x27A59A0`, `0x27A5C70`, `0x27A4650` |
| vf3 | `0x27A4A40`: llama primero a `cdmp_ICreateServerActorDesc__vf3 0x2791CD0` (aquí está `0x2792A7B`) y luego configura el gimmick con sus campos `+0x4A0…+0x500` |
| Constructor | `.xtls 0x100F8790` (único LEA de la vtable, `0x100F87A3`, índice `build/research/xref_index.pkl`): `call 0x278EA70` (constructor base) y después `mov [rbx],0x145B04C68`, `byte [rbx+0x4E8]=8`, campos `+0x4EC…+0x588` |
| Consecuencia | En InstantGimmick **`+0x4E8` no es el handle de horario**: el código actual sólo lo lee con vtable NPCSchedule (`user2_stages.cpp:1171,1213`). Correcto. |
| En la creación | Rama `type == 7` de `0x2791CD0`: tras escribir el controlador llama a `cdmp_CreateFromDesc_SetOwnershipState 0x27A1430` y a vf7. El estado de propiedad depende del controlador: es un motivo para no asignarlos sin medirlo (§4). |
| Docs | `noche/control-npc-lejos.md:429`: «Tipo 7 (gimmicks, objetos) … no son IA» |

---

## 2. Cómo enlaza el exe padre e hijos al crearlos

### 2.1 Descriptores (antes de crear actores) [E]

- Base `ICreateServerActorDesc`, constructor `0x278EA70` (`0x278ea70.c:132-136`): `+0x2F0 = 0` (descriptor padre), `+0x2F8 / +0x300 / +0x304` = vector `{T* data; u32 count; u32 cap}` de hijos, todo a cero.
- Nodo de horario: `0x281CEE0` construye el descriptor `NPCSchedule` (`+0x4E8` = puntero al nodo). Al final (`0x281cee0.c:394-396`) llama a:
  - `0x281D970` (sub-nodos de otro nodo);
  - **`0x281DBD0`** (`CreateChildServerActorDesc`, llamada en `0x281D929`). Recorre las entradas de la stage `+0x138` (stride `0x168`) sin actor (`+0x28 == 0`) cuyo nodo está en la lista de hijos del nodo padre (`node+0x140` pares `(recurso, índice)`, cantidad `+0x148`). Para cada una:
    - descriptor del hijo con `0x281CEE0` anidado;
    - envoltorio de 0x10 B `{vtable, inner}` con `CreateChildServerActorDesc::vftable 0x5B04E40` → **`_Simple 0x5B04E80`**;
    - **`[inner+0x2F0] = desc_padre`** (`0x281dbd0.c:105`);
    - **`push_back(desc_padre+0x2F8, envoltorio)`** (`FUN_14043DB70`, `0x281dbd0.c:121`).
- Sub-nodos del líder del grupo nativo: `0x2820D80` hace lo mismo (`0x2820d80.c:110,123`).
- Otros constructores de envoltorios, con **el mismo par de escrituras** (`[inner+0x2F0]=padre`, `push_back(padre+0x2F8)`), comprobados en el desensamblado:

| Envoltorio | vtable | Constructor / escritura de `+0x2F0` |
|---|---|---|
| `_Simple` | `0x5B04E80` | `0x281DBD0`, `0x2820D80`, `0x2799C30` (×4), `0x278BBB0` (invocación) |
| `_CatchTarget` | `0x5B04E20` | `0x2799C30` |
| `_AutoSpawnGimmick` | `0x5B04DA0` | `0x27A0DB0`, `0x27A1317` |
| `_CombinationGimmick` | `0x5B04D60` | `0x279D910`, `0x279FAD0` |
| `_SavedAttached` | `0x5B04DC0` | `0x279FCA0`, `0x27A013D` |
| `_LinkedVehicle` | `0x5B04D80` | `0x2797510`, `0x27979AC` |
| `_Docking` | `0x5B04DE0` | `0x288CFD0` (xref `0x288D17D`) |
| `_Rider` / `_NPCScheduleGroupActionMember` | `0x5B04E60` / `0x5B04E00` | sin xrefs estáticos en `.rsrc` |

Estos constructores sólo se referencian desde tablas `.xtls` (`0x17200xxx`). El descriptor del hijo, con su vínculo, **existe completo antes** de que `ServerField::vf17_CreateActors 0x2AF9AE0` (cuerpo `.xtls 0x10649310`) cree el lote.

### 2.2 Creación y orden [V/E]

- `0x2AF9AE0` recibe la lista de descriptores raíz y los crea en lote. Para cada actor: vf3 del descriptor interior (`NPCSchedule 0x27A6160` / `InstantGimmick 0x27A4A40`) → `0x2791CD0` → selector `0x2792A11…0x2792A7B` (`+0x90` existente → `desc+0x40` → `actor+0xA0` → `field+0x180`) → **`0x2792A7B mov [r14+0x90],eax`**.
  - En el mid: `rsi` = descriptor **interior**. Por eso los hijos tipo 6 dan `Type` y no `OtherDesc`: su vtable es NPCSchedule.
  - `r14` = actor nuevo; `r13` = campo.
- **Orden [V]:** el padre pasa por `0x2792A7B` antes que sus hijos, en el mismo hilo (`T8632` en C, `T11104` en A, `T6828` en B) y con claves consecutivas (padre N, hijos N+1…). Ejemplos:
  - `B0100428` nace en qpc …4141 µs, el hijo `B0100429` en …4234 µs y el 2005 de la raíz se encola en …4567 µs;
  - `B01004BE` nace en …8705 µs y sus hijos `BF`/`C0` están en la tabla en …9488/9727 µs.
- El lote se crea entero y **después** se publica (NotifyAppear). En la publicación, `prepare_family` ya lee el vínculo (`[NpcActivation familia]` emitido en la publicación, con `padre≠0`).
- **En el mid del hijo el vínculo de actor todavía no existe [V]:** de 878 líneas `[nacimiento]` (264+228+386), **0** traen `padre≠0`, aunque 130 de esas claves aparecen después con padre en `[familia]` (44+26+60). `+0xAC` y el componente de jerarquía (`child_hold_actor_parent`, `child_hold.cpp:4129`) se escriben después de vf3 (post-proceso por envoltorio, p. ej. `_Simple` vf2 `0x27A7110` → `.xtls 0x1010EEC0`).

### 2.3 Dónde se conoce el padre en el mid del hijo

| Fuente | ¿Disponible en `0x2792A7B` del hijo? | Uso |
|---|---|---|
| `[r14+0xAC]`, jerarquía `+0x1C` | **No** (0 de 878) [V] | sólo en la publicación |
| `[rsi+0x2F0]` (descriptor padre) | **Sí** [E] (escrito al construir el envoltorio, antes del lote) | vínculo primario |
| `[[rsi+0x2F0]+0x2F8]` vector, `[[v+i*8]+8] == rsi` | **Sí** [E] | prueba de pertenencia (máximo 64) |
| Actor padre | **No en el descriptor** (no se encontró un campo `desc → actor`); sí en **nuestro** registro hecho en el mid del padre (`rsi_padre → r14_padre`) | clave del padre, controlador ya escrito |

---

## 3. Diseño de `npc_family_control=1`

### 3.1 Contrato

| Valor | Anfitrión | Invitado |
|---|---|---|
| `0` | como hoy | — |
| `probe` | como hoy **más** la prueba del vínculo en cada nacimiento (§3.9), sin escrituras | observa (como hoy) |
| `1` | asigna los miembros de horario de una raíz asignada (§3.4); *host-kept* para el resto (§3.5); raíces con subárbol excluido no se asignan (§3.3) | igual que `probe` (`npc_probe_client`) |

Requisitos de `1`: `user2_stages=1`, `user2_npc_control=1`, hook `0x2792A7B` instalado y **`npc_activation=1` con el emisor corregido (§5)**. Si falta alguno: WRN y apagado, como hoy en `user2_stages.cpp:3176-3200`, que ahora rechaza `1` (`parse_mode(family,false)`).

Unidad = **raíz U-e (`Ctl::Assign`) + el subárbol de su descriptor creado en el mismo Troc**. El mundo y las misiones siguen siendo del anfitrión.

### 3.2 Punto de enganche: ninguno nuevo

`docked_gimmicks.cpp:396-403` `ctl_write_mid` → `user2_stages_npc_control(rax, rsi, r13, r14)` (`user2_stages.cpp:3336-3340`) → `npc_control_impl` (`:1136-1189`) + `npc_observe_birth` (`:1195-1230`). Todo se decide ahí: sólo registros (`eax`), lecturas protegidas (`rdv`/`game::fast_read`), atómicos y POD por hilo. Sin CRT ni cerrojos nuevos.

### 3.3 Registro por Troc y decisión de la raíz

1. `TrocMark` (`user2_stages.cpp:452-466`): añadir `uint64_t id[16]`, asignado en `user2_stages_troc_begin` (`:3405`) desde un contador global por cada profundidad.
2. Nuevo POD `thread_local` (≈1,6 KB por hilo):

   ```cpp
   struct FamRec { uintptr_t desc, actor; uint32_t key; uint8_t kind /*Root,Member,HostKept*/; };
   struct FamFrame { uint64_t troc = 0; uint8_t n = 0; bool full = false; FamRec r[64]; };
   thread_local FamFrame t_fam;
   ```

   - Si `t_fam.troc != t_troc.id[depth]`, el marco se reinicia en la primera escritura.
   - Ningún puntero sobrevive al Troc. Un Troc anidado oculta el marco exterior: sus hijos no se enlazan y quedan como hoy (contador `vínculo perdido`).
3. En `npc_control_impl`, cuando `ctl_verdict == Assign` y `npc_family_control=1`, **antes de escribir `eax`**:
   - **Pre-examen del subárbol** de `rsi`: vector `+0x2F8` (cantidad `+0x300` ≤ 64), profundidad ≤ 3, como mucho 64 envoltorios en total. Para cada uno se leen su vtable y la del descriptor interior.
   - Veredicto `FamRoot`:
     - `Ok`: vacío, o sólo `_Simple`+`NPCSchedule` y/o envoltorios de gimmick (`_Simple`/`_AutoSpawnGimmick`/`_CombinationGimmick` con interior `InstantGimmick`).
     - `Excluded`: hay `_LinkedVehicle`, `_Rider`, `_Docking`, `_SavedAttached`, `_CatchTarget`, `_NPCScheduleGroupActionMember` o una vtable desconocida.
     - `Unreadable`: lectura fallida o cantidad > 64.
     - `Full`: el marco ya tiene 64 registros.
   - **Con `Excluded`, `Unreadable` o `Full` la raíz no se asigna** en modo 1, así que toda la unidad sigue con el anfitrión. Es coherente, aunque esa unidad conserve el riesgo del bucle de S33.
   - Contadores por motivo; en `probe` sólo se cuenta y se registra.
4. Si la raíz se asigna: `t_fam.r[n++] = {rsi, r14, key, Root}`.

### 3.4 Miembros asignados (tipos 3 y 6 de horario)

En `npc_control_impl`, para todo nacimiento con `subject_user2 && own_mgr_troc && op ∈ {2043,2045,2046}` (los mismos que hoy llegan a `ctl_verdict`):

1. `pd = [rsi+0x2F0]`. Si es 0 es una raíz: se sigue el camino actual.
2. Buscar `rec` con `rec.desc == pd` en `t_fam` (Troc actual).
3. **Pertenencia:** en `[pd+0x2F8]` (≤ 64) hay un `w` con `[w+8] == rsi`. Se obtiene `vt_w = [w]`.
4. **Padre vivo y el mismo:** `[rec.actor+0x60] == rec.key` y, si `rec.kind != HostKept`, `[rec.actor+0x90] == key2`.
5. `family_verdict(FamIn)`, una función pura nueva en `user2_stages_logic.h` junto a `ctl_verdict`, con enum `Fam {AssignMember, HostKept, NoLink, NotInParent, ParentChanged, ParentNotAssigned, OwnerOrDescUser, HasController, NotOwnerBranch, NotHostField, TypeNotAllowed, KeyMismatch, Aborted}`.
   - **`AssignMember`** si se cumple todo lo siguiente:
     - `rec.kind ∈ {Root, Member}`;
     - `vt_w == 0x5B04E80` (`_Simple`) y vtable interior `== 0x5B04CB0` (NPCSchedule);
     - tipo `∈ {3, 6}`;
     - `old_ctl == 0`, `desc+0x40 == 0` y `actor+0xA0 == 0`;
     - campo del anfitrión, `eax == field+0x180` y `key2` coincide con la sesión;
     - sin aborto.
   - **`HostKept`** si `rec.kind == HostKept`, o si el padre está asignado pero el hijo no cumple lo anterior (p. ej. `InstantGimmick` tipo 7, u otro tipo).
   - En los demás casos, `NoLink`/motivo: no se escribe nada (como hoy).
6. Modo 1 y `AssignMember`:
   - `g_ctl_keys.put(key, kKeyAssigned | kKeyFamily)`; si no cabe, no se asigna y pasa a `HostKept`;
   - `eax := key2`;
   - `t_fam.r[n++] = {rsi, r14, key, Member}`.
   - Los hijos tipo 3 que ya obtienen `Assign` por su propio veredicto (los 44/26/57 actuales) se registran también como `Member` y reciben `kKeyFamily` si su vínculo se demuestra.
7. `HostKept`: no se escribe nada; `g_ctl_keys.put(key, kKeyHostKept)` (rastreado, **no asignado**); `t_fam.r[n++] = {rsi, r14, key, HostKept}`. Sus nietos heredan `HostKept`.
8. Nuevos bits de `KeyTable` (`user2_stages_logic.h:751-762`): `kKeyFamily = 512`, `kKeyHostKept = 1024`. Nuevas constantes: `kDesc_Parent=0x2F0`, `kDesc_Children=0x2F8`, `kDesc_ChildCount=0x300`, `kWrap_Inner=8`, las vtables de envoltorios de §2.1 y `kVtDescInstantGimmick=0x5B04C68`.

### 3.5 Por qué esta frontera (seguro para user 2 frente a anfitrión)

| Clase | ¿A user 2? | Motivo |
|---|---|---|
| `_Simple` + `NPCSchedule`, tipo 3 | **Sí** | Mismo contrato que la raíz: el invitado tiene la stage ligada (U-1). En S34, 127 hijos tipo 3 ya iban así sin incidencia (familias enteras: mediana 1,2-2,9 s). |
| `_Simple` + `NPCSchedule`, tipo 6 | **Sí** (nuevo) | Mismo descriptor y nodo de la misma stage. La raíz los necesita en su cliente para arrancar (§1.3); dejarlos con el anfitrión produce la espera de 10 s, el aborto o la retirada y los cierres. |
| `InstantGimmick` tipo 7 hijo de raíz asignada | **No (host-kept)** | No bloquea a la raíz (8/8 arrancan en 0,23-3,1 s), llega sin retención (NoCc) y ningún cierre se atribuye a estos hijos. `SetOwnershipState` (`0x27A1430`) y vf7 dependen del controlador y no está medido que el cliente invitado los gestione. Pasaría a otra etapa (`npc_family_gimmicks`) tras la sonda de §3.9. |
| Tipos 4/5, `_LinkedVehicle`, `_Rider`, `_Docking`, `_CatchTarget`, `_SavedAttached` | **No**, y la raíz tampoco | Monturas, vehículos, jinetes, docking del jugador, captura o estado guardado: contratos propios (`mount_sync`, `docked_gimmicks`, guardado del campo del anfitrión). Unidad entera con el anfitrión. |
| Raíces tipo 6 de stages de user 2 (sin padre) | **No** en `=1` | Mismo contrato que los miembros tipo 6, pero sin evidencia de IA tipo 6 con el invitado como controlador. Se activa después de medir los miembros (§3.10). Mientras tanto quedan *host-kept* (§3.6). |

### 3.6 Publicación (`child_hold`)

- **Miembros de user 2.** Para la sesión de user 2 son de su controlador: no se retienen. Viajan en la misma llamada NotifyAppear que la raíz (`prepare_family`, `child_hold.cpp:1700-1760`, `FamilyWhy::ParentInCall`). Para el anfitrión rige la regla actual `WithheldUser2` (`child_hold.cpp:617-622`): **nunca con restore 0**; espera restore 1, un cambio de controlador o una baja. Un tipo 6 puede seguir con restore 0 aunque actúe (`opcion-d-diario.md:692`). Entonces el anfitrión no lo ve mientras está lejos, y el drenaje en la preparación lo retira al acercarse (§3.8). Contador nuevo: «miembros retenidos al anfitrión > 30 s».
- **Host-kept.** Nueva regla en `npc_release_step` (`child_hold.cpp:603-630`), para el observador user 2 (`!e.observer_host`) y `user2_stages_npc_host_kept(key)` (lectura sin cerrojo de `g_ctl_keys.flags`):
  - con restore 0, al plazo → `Keep` con «retenido de user 2: nacido de su stage, lo controla el anfitrión, restore 0» (`NpcRelease::WithheldHostKept`), nunca `Timeout`;
  - `stale` → `Drop`, nunca la publicación nativa `Native` (`:589-593`);
  - sin CC (`HoldWhy::NoCc`, `:147-149`) → publicación nativa como hoy: los lectores de los cierres (`0x88F832`, `0x1831598`, `0x232D5FF`) parten de un CharacterControl.

  Así ningún actor *host-kept* con CC llega al invitado sin acción. `B0100578` tenía CC (captura GuestS3).
- No cambia la regla actual para los NPC del anfitrión ajenos a user 2 (ambientales con restore 0 liberados a los 10 s; `RemoteUpdateGuard` sigue en el invitado).

### 3.7 Contención `npc_activation=1` por unidad

Archivos `npc_activation_logic.h` y `user2_stages.cpp:1796-1960`:

1. `npcact::Actor` (`:50-59`): añadir `bool member, host_kept` y `uint32_t wrapper`.
   - `npc_observe_birth` (`user2_stages.cpp:1195`) recibe el resultado del vínculo: `a->parent = rec.key` **en el nacimiento**, sin esperar a la publicación.
   - `candidate` incluye a los miembros y a los *host-kept* (con su controlador real).
2. `no_progress` (`:85-92`): un `member` o `host_kept` **nunca vence por sí solo** (`false` salvo `orphaned`/`local_retired`). Con eso `next_due` (`:236-247`) sólo devuelve raíces.
   - Hoy un hijo tipo 3 vence solo: en C hay 21 de 110 «sin progreso» que son miembros con el padre ya actuando, p. ej. `B0100429`.
   - Un miembro sin acción con la raíz progresando sólo genera una línea de diagnóstico, una vez y a los 10 s.
3. El progreso de la unidad es el de la raíz, **con el emisor corregido (§5)**.
4. `family_into` (`:393-414`) ya recorre por `parent`. Con el padre fijado en el nacimiento, la familia está completa antes de la publicación e incluye a los *host-kept*. La retirada (`user2_stages.cpp:1878-1957`) ya va de hijos a padre y llama a `cleanup_one(sam, key, a.controller, err, a.parent)` con el controlador **de cada miembro**: el del anfitrión en los *host-kept*. Revalida clave, nacimiento, controlador y padre.
5. `abandon` (`:427-439`) marca a todos los miembros; `kKeyAbandoned` para los asignados.
6. Huérfanos: un miembro publicado tras la lápida de su raíz se retira en la publicación (`retired_for_publication`, `child_hold.cpp:1668-1690`), sin cambios.
7. El veto de nodo sigue en el nodo de la raíz: sus sub-nodos se construyen desde él (`0x281DBD0`/`0x2820D80`), de modo que no renacen sin la raíz.

### 3.8 Vigilante antiguo, drenaje y limpiezas

- `ctl_stats_add` (`user2_stages_logic.h:883-904`): `kKeyFamily` **no cuenta como `unaccepted`** (contador aparte `family`). Así ningún miembro puede disparar `CtlAbort::Unaccepted`: la unidad la juzga su raíz.
  - Sigue contando para `Bad2005`, `HostReq`, `r42` y `left_after_drain`.
  - Un 2005 de un miembro que nombre a otro controlador, o una Req del cliente anfitrión para un miembro, sigue abortando.
- `ctl_cleanup` (`user2_stages.cpp:1677-1740`): recoger primero las claves `kKeyFamily` y después las raíces (dos pasadas), para retirar a los hijos antes que al padre.
  - Cubre las tres causas existentes: su Troc de carga/2001 (`:3476`), su logout (`:1982`) y tras drenaje (`:3500`).
  - Las `kKeyHostKept` **no** entran en esas pasadas: `cleanup_one` exige el controlador `key2`.
- **Drenaje en la preparación del anfitrión** (`prep_drain`, `user2_stages.cpp:876-958` → `drain_now(mgr2)`): borra las stages de user 2. Los miembros de horario son entradas de esas stages (sub-nodos `+0x138`) y se van con la baja nativa (motivo 19, `exe+0x281821F`, 196 en C).
  - A los 5 s `ctl_cleanup(kCleanAfterDrain)` da de baja cualquier asignado que quede, **miembros incluidos**.
  - `LeftAfterDrain` los vigila.
  - Nueva pasada sólo tras el drenaje: los *host-kept* registrados en `g_activation`, nacidos antes del drenaje, aún en SAM, con controlador sin cambios y padre dado de baja → `cleanup_one(sam, key, a.controller, …)`. Contador `host-kept tras drenaje` (en `probe` sólo se cuenta).
- La barrera `g_p_draining` (CAS) entre el drenaje y la contención no cambia.

### 3.9 Sonda previa obligatoria (`npc_family_control=probe`)

Hoy `probe` sólo registra el padre en la publicación. Hay que añadir una línea por nacimiento bajo el sujeto de user 2 en su Troc, con límite de frecuencia y sin escrituras:

```
NpcFamily [vínculo] key=… tipo=… padre=0x… (o 0) +0x2F0=sí/no registro=raíz/miembro/host-kept/ninguno
  en_vector=sí/no envoltorio=exe+0x5B04E80 interior=exe+0x5B04CB0 veredicto=asignaría/host-kept/<motivo> op=2045
```

Más un resumen por tipo × envoltorio × veredicto y las raíces excluidas por subárbol con el motivo. Criterios para activar `1`:
- 100 % de los hijos `_Simple`/NPCSchedule de raíces asignadas con vínculo demostrado (`+0x2F0` ≠ 0, en el vector, padre registrado);
- 0 vínculos ambiguos;
- envoltorio conocido para todos los tipo 7.

Es medible **en un solo PC con `cdmp_peer`**, porque los descriptores y nacimientos son del servidor.

### 3.10 Invariantes y pruebas

**Invariantes:**
1. Un miembro sólo se asigna si comparte hilo y Troc con su padre, `+0x2F0` coincide con el descriptor registrado, está en el vector `+0x2F8` del padre, la clave del actor padre se ha revalidado, el padre está asignado y se cumplen todas las condiciones de U-e salvo el tipo (3 o 6) y la vtable interior NPCSchedule bajo `_Simple`.
2. Nunca se asigna un miembro sin raíz asignada en ese Troc, y nunca una raíz con subárbol excluido, ilegible o sin sitio en el marco.
3. Un miembro nunca es `unaccepted` ni vence solo. La unidad se juzga y se retira por la raíz, de hijos a padre, revalidando cada pieza.
4. Ningún actor *host-kept* con CC y restore 0 se publica a user 2 (ni `Timeout` ni `Native` por estancamiento).
5. Ningún miembro de user 2 con restore 0 se publica al anfitrión (`WithheldUser2`, hoy «assigned 2005s with restore 0 at the host's client 0»).
6. A los 5 s de un drenaje no queda vivo ningún asignado, raíz o miembro.
7. No se usan punteros ni direcciones entre PC ni entre Troc. En el mid no hay CRT, copias agregadas ni cerrojos nuevos.

**Pruebas puras:**
- `optd_user2_stages`:
  - matriz de `family_verdict`;
  - marco por Troc (anidado, desbordamiento, una dirección de descriptor reutilizada en otro Troc o refrescada por el mid de la nueva raíz);
  - lectura del vector en memoria sintética (límites, cantidad > 64, interior distinto, lectura fallida);
  - pre-examen del subárbol;
  - `ctl_stats` con `kKeyFamily`;
  - orden de `ctl_cleanup`;
  - `probe` nunca escribe `eax`.
- `optd_npc_activation`: un miembro nunca vence solo; la raíz vencida retira a los miembros y a los *host-kept* con sus controladores; el progreso de la raíz protege la unidad; el abandono marca a los miembros; huérfano en la publicación; **progreso con emisor 0 y acción aplicada (§5)**.
- `optd_child_hold`: `npc_release_step` para *host-kept* (Keep al plazo, Drop por estancamiento, nunca Native); `WithheldUser2` y `NoCc` sin cambios.

**Prueba en vivo (Shadow, Hernand, anfitrión lejos):** `npc_family_control=1` con `npc_activation=1` y el emisor corregido. Éxito si:
- las familias Stranger+cabras, Guard+Greyhound y Girl+Beagle nacen con los miembros en user 2 y llegan en la misma llamada que la raíz;
- primera Req de la raíz < 5 s (se esperan ~1-3 s, como las familias enteras de §1.3);
- 0 `RemoteUpdateGuard` sobre miembros y 0 liberaciones `TIMEOUT restore 0` a user 2 de actores nacidos en sus stages;
- «assigned 2005s with restore 0 at the host's client» = 0;
- sin `CONTROL ABORT`, sin huérfanos tras una retirada y sin cierres.

Después se mide la IA de los tipo 6 (perros y cabras siguen a su raíz) antes de ampliar a raíces tipo 6 o a gimmicks.

---

## 4. Riesgos y cómo los excluye el diseño

| Riesgo | Exclusión |
|---|---|
| **Monturas** (tipo 5; `_LinkedVehicle`/`_Rider`) | Tipo fuera de {3, 6}. Un subárbol con esos envoltorios deja la raíz con el anfitrión. En S34 no hay hijos tipo 5 bajo raíces de user 2 [V]. |
| **Mascotas y compañeros** (tipo 4: Damiane, Oongka) | Tienen dueño `+0xA0` y/o `desc+0x40` (nativos), excluidos por `family_verdict`. El tipo 4 está excluido. Nunca son hijos de horario de una stage. |
| **Hijos de un usuario y docking del jugador** | `actor+0xA0 ≠ 0` o `desc+0x40 ≠ 0` → no; `_Docking` excluido. `docked_gimmicks` sigue siendo dueño del mid y de `t_dock`. |
| **Gimmicks** (tipo 7, `SetOwnershipState`, estado guardado) | *Host-kept*. `_SavedAttached` excluye la raíz (estado del campo del anfitrión). Nunca se publican a user 2 con CC y restore 0. Su asignación queda para otra etapa medida. |
| **NPC de misión** (tipo 4 con nombre; misiones del anfitrión) | El tipo 4 está excluido. `quest_policy` no cambia: el progreso sigue en el anfitrión. Los miembros heredan sólo la autoridad de IA de una raíz que U-e ya asignaba. |
| **Combate** (perros de guardias) | `combat_follow` actúa sobre actividad reciente (`combat_follow.cpp:239-249`). Asimetría de aggro posible (C3 de `control-npc-lejos.md`). Se mide en la prueba en vivo. |
| **Familia partida residual** (miembro no elegible bajo raíz asignada) | El pre-examen lo evita para los envoltorios conocidos. Si aun así ocurre (tipo inesperado bajo `_Simple`), el miembro queda *host-kept*, la unidad se retira por su raíz y se cuenta «familia partida». |
| **Reutilización de dirección de descriptor** | Ámbito del Troc, pertenencia en `+0x2F8`, clave del actor padre revalidada, y el mid de la nueva raíz refresca el registro antes que sus hijos. |
| **Coste en el mid** | Sólo bajo el sujeto de user 2, armado y en su Troc. ≤ 64 lecturas de vector por raíz (unas 400 raíces por sesión). |
| **El anfitrión no ve los miembros con restore 0** | Aceptado mientras está lejos; el drenaje en la preparación los retira al acercarse. Contador nuevo. |
| **Lecturas protegidas** | Todo con `rdv`/`game::fast_read` (SEH). Ningún puntero recibido por red. |

---

## 5. Hallazgo lateral crítico: la contención no ve el progreso

**[V]** Líneas `NpcActivation [acción OnServer]`:

| Ensayo | Acciones | `aplicada=1` | `progreso=1` | `emisor` |
|---|---:|---:|---:|---|
| A | 261 | 261 | 0 | `0x00000000` en todas |
| B | 223 | 223 | 0 | `0x00000000` en todas |
| C | 359 | 359 | 0 | `0x00000000` en todas |

**[E] Causa:**
- `npc_action_probe.cpp:41` lee `sender = [[session+off::kSession_User(0x90)]+0x60]`, pero el cuarto argumento de `0x2A78CA0` no es ese objeto. El nativo obtiene la clave con `(*session->vf+0x170)()` (`0x2a78ca0.c:39-44`).
- `npcact::applied_to` (`npc_activation_logic.h:103-105`) exige `sender == a.controller`, y `Table::progress` (`user2_stages.cpp:3219`) nunca marca `applied_us`.

**Efecto en C [V]:** 110 «sin progreso» y 208 líneas de retirada local, de las que 99 son resúmenes de unidad. Entre ellas está `B0100428` (Deer Porter): acción aplicada a 2,80 s y retirada a 11,47 s. Además, 21 miembros vencieron con el padre actuando.

**Corrección propuesta (pequeña, antes de cualquier prueba de familias):**
- En la rama normal, H1b sólo se alcanza tras `ServerStartActionOnServer`, que el nativo condiciona a `controlador == usuario de la sesión`. Por tanto `normal_applied` basta si `[actor+0x90]`, leído antes de la llamada, es `a.controller`.
- Hay que guardar ese controlador en `Pending` (`npc_action_probe.cpp:40-49`) y pasarlo en lugar del emisor.
- El emisor puede seguir registrándose como diagnóstico. Prueba nueva en `optd_npc_activation`.

---

## 6. Pendiente y límites

- [H] Que `B0100578` naciera en el Troc de user 2: no hay línea por clave de los veredictos `OtherDesc`. La sonda de §3.9 lo resolverá.
- [H] Qué ruta crea exactamente cada tipo 7 hijo (envoltorio): la sonda registrará la vtable del envoltorio. Sólo se ha comprobado estáticamente que todos los envoltorios escriben `+0x2F0` y `+0x2F8`.
- [H] IA de un tipo 6 con el invitado como controlador: sin medir; los criterios están en §3.10.
- No se ha medido la convivencia con `user2_road_control` (viajeros 2550): su autoridad sigue fuera de este diseño.
