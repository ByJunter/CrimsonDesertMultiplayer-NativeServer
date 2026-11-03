# P8 · S1: escenas de NPC en el invitado (nodo de escena, filtro del servidor y diseño de raíz)

10 de octubre de 2026, rama `opcion-d` (HEAD `7ccc885` al escribir). Build del juego **1.0.0.2976**; direcciones
en **RVA** (base `0x140000000`).

**[V]** = comprobado con instrucciones (capstone sobre la copia de solo lectura
`D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, Python `-I`), con decompilación de Ghidra **en una copia
propia del proyecto** (`-readOnly -noanalysis`, en el scratchpad de la sesión; borrada al terminar), con la
captura o con el log citados. **[E]** = inferencia o hipótesis pendiente de medir.

Solo análisis: no se tocó código, no se lanzó el juego, no se escribió en `bin64` (las capturas del cliente del
anfitrión solo se leyeron), no se modificó `D:\RE\CrimsonDesert_2976\project` y no se hizo commit. Ningún punto
propuesto está en `.xtls` ni en un *thunk* hacia ella.

## 0. Evidencia

| Ref. | Archivo |
|---|---|
| GB | `C:\Emulador CD\backups\p8-sesion6B-shadow\cdmp.log` (invitado, tramo B; líneas `GuestStage`, `GuestDiag`, `P8Probe`) |
| GBC | `…\p8-sesion6B-shadow\cdmp_packets_20261009-224848.bin` (cliente real del usuario 2) |
| HB | `C:\Emulador CD\backups\logs\cdmp_20261009-2345_p8-6B-host.log` |
| HBC | `D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\cdmp_packets_20261009-234606.bin` (cliente propio del anfitrión, solo lectura) |
| Decomp | `…\Codex\2026-10-03\…\dictamen-2322-work\decomp\0x9ed290.c`, `0xa9ca40.c`; exportaciones nuevas de esta sesión (no conservadas) de `0x1EB88C0`, `0x1869D70`, `0x186A810`, `0x1861F00`, `0x20853D0`, `0x9F2FA0`, `0x9F3230`, `0x91CD90`, `0x43B4B0`, `0xA90AF0`, `0xA9FEE0`, `0xC0CE50`, `0x2A78CA0`, `0x2A7A560`, `0x2BC4F70`, `0x2BC51E0`, `0x2BC4D50`, `0x2AC66D0`, `0x296CAA0` |

Numeración de capturas: `#n` = registro CDPK desde 1; `t` = segundos desde el primer registro (formato de
`src/common/capture_file.h`). Las claves de actor coinciden en HBC y GBC en esta sesión (mismo servidor).

## 1. Resumen

1. **`local_res8` no lo escribe el 2661 [V].** Es el nodo de escena que el **componente de IA del propio NPC**
   lee de **su 2005**: `ClientAiActorComponent::vf11` (`0x206DBD0`) llama en `0x206DE66` a `0x1EB88C0` con
   `rcx = [ai+0x90]`, que lee `u8, u32 recurso, u32 nodo, u64 clave de escena` y resuelve `(recurso, nodo)` con
   `0x1869D70`. Esa resolución **solo busca** el recurso en el contenedor de ficheros cargados
   `pa::NPCScheduleManager` (`[exe+0x6C8CDA0]+8`, `ThreadSafeRefCountedContainer<u32, NPCScheduleData_File>`,
   creado en `0x1861F00`); `0x186A810` no carga nada. En el invitado el fichero no está cargado y el *handle*
   queda inválido: `local_res8 = 0xFFFFFFFF00000000` en los 235 NPC.
2. **Lo que carga ese fichero en el cliente es la creación de la escena, que solo recibe el dueño [V/E].**
   - De los 235 NPC que salen en `0x9ED401`, **225** llevan en su 2005 un id de recurso que en HBC solo aparece
     en un `CreateNPCScheduleStageAck` 2335 (**199 NPC**, escenas de horario `0x2000xxxx`) o en un
     `PrepareSequencerAck` 2661 (**26 NPC**, secuenciadores). Los otros 10 no se identificaron [V].
   - El invitado recibió **0 × 2335** y un solo 2661 (el suyo, `0x1A4`) [V].
   - Que el 2335 o el 2661 provoquen la carga es **[E fuerte]**. Apoyos: el 2661 construye una
     `pa::SequencerLoadingTask` (`0xA9CA40 → 0xA90AF0`) y el 2335 una `pa::ClientNPCScheduleStage` en
     `gestor+0x148` (`0xA9FEE0`), y el id del fichero viaja en el paquete (2661 `payload+27`, 2335 `payload+9`).
     El contenedor es por recuento de referencias [V por RTTI].
3. **El «filtro del servidor» no es de visibilidad ni de dueño de escena: es una bandera por acción [V].**
   `cdmp_ServerStartActionLogic 0x2A78CA0`, en `0x2A78D0D`, hace `cmp byte [r15+0x281],0`. Si la bandera está
   puesta, aplica la acción con `0x232FF90` (*thunk* a `.xtls`) y **vuelve sin difundir**. `B+0x281` es el último
   byte del bloque B (`0x2442DBE`, en `0x2442BF0`), es decir, el byte **`0x2D3`** del `StartActionReq` 2223.
   - Mientras el usuario 2 estuvo en el campo (t ≥ 240 en HBC), el cliente del anfitrión solo mandó por esos NPC
     **504 × 2223, todos con `0x2D3 = 1`** y hash `0x35DD392F` (`seq_bodyanim_action_loop_xxx`).
   - Los **11 160** `StartActionReq` de NPC que sí se reenviaron tienen `0x2D3 = 0`. Es el único byte que separa
     los dos grupos.
   - Los 2734/2556 de esos NPC son todos de t < 240, antes de que existiera el usuario 2. Por eso tampoco hay
     2404/2697.
4. **Diseño nativo [E fuerte]:** una escena se ejecuta **localmente en cada cliente que la ve**. El servidor no
   reenvía sus acciones de BodyAnimation, y en un jugador el único que la ve es el dueño. El invitado necesita la
   **misma escena viva en X** (el gestor del Kliff del anfitrión en su proceso, que existe:
   `its own manager 0x3F2F8F9E380 (ClientSequencerStageManager)`, GB 2981 route). Reenviar acciones
   `seq_bodyanim` no basta: el invitado no tiene el gráfico dinámico (`CC+0x70/+0x78`), que es justo lo que falla
   al restaurar.
5. **Recomendación (§4):**
   - **(A)** Duplicar en el anfitrión, a nivel de transporte, la familia de paquetes de escena del dueño hacia la
     sesión 2 (bytes exactos, orden nativo), más una instantánea en la entrada del observador **antes** de la
     ráfaga de 2005.
   - En el invitado, enrutar esos Ack al gestor del **actor dueño de la escena** (X) en lugar del dueño del
     campo.
   - Retener o reordenar los 2005 de NPC hasta que el fichero esté cargado.
   - Filtrar en el anfitrión los C>S de escena del invitado.
   - Para los 15 en A-pose hace falta además el complemento de restauración de `p8-actores-anfitrion.md` §4.1 (ii).
   - (B), con el 2174 nativo, queda como alternativa de más riesgo.

## 2. Pregunta 1: quién escribe el nodo del NPC y qué exige el enlace

### 2.1 Cadena en el cliente [V]

```
2005 (registro del NPC)
  └─ ClientAiActorComponent::vf11 0x206DBD0
       0x206DE5C mov rcx,[rdi+0x90]      ; X1 = datos de escena de la IA
       0x206DE66 call 0x1EB88C0          ; lector
          0x1EB88D8  u8   -> X1+0x61
          u32 recurso, u32 nodo          ; [rbp+0x50]/[rbp+0x54]
          0x1EB8940  call 0x1869D70(&h, (nodo<<32)|recurso)
                     -> [X1+8] = H = {u64 clave, u32 índice, ptr fichero}   (copia, 0x1EB894x)
          u64 -> X1[0]                   ; clave de escena (secuenciador o 0x2000xxxx)
          …(u8 ×5, u32, lista, FieldTime, …)
0x1869D70: 0x186A810([0x6C8CDA0]+8, id)  -> fichero cargado o NULL (también NULL si +0x15 = «descargando»)
           búsqueda binaria de (clave>>34) en fichero+0xD0 (entradas de 12 B) -> índice < fichero+0x58
NPC vf13 0x9ED290 (ClientSequencerPlayDataActorComponent, vtable 0x55BEE78, ranura 13):
  0x9ED3A2..0x9ED3B5  H = [[[npc+0x68]+0x58]+0x90]+8 ; [npc+0x68]+0x58 = componente de IA
  0x9ED3CC..0x9ED3F4  si H válido: local_res8 = [[H.fichero+0x50] + índice*0x140]   (primer u64 de la entrada)
  0x9ED401            sale si low32 == 0 o high32 == -1
  0x9ED42B            0x8AE1A0(comp+0x2D8) -> actor dueño A (personaje)
  0x9ED45B..          X = [[[A+0x68]+0xA8]+0x18]; instancia = [X+0x148]->vf8(&comp+0x2E0)
  0x9ED497            0x43B4B0(instancia, &local_res8, npc)
```

- **[V]** `0x186A810` solo recorre la tabla hash del contenedor (`param_1+0x20/+0x30/+0x38`). No hay rama de
  carga.
- **[V]** El contenedor es `pa::NPCScheduleManager` (`0x1861F00`): guarda `DAT_146c8cda0 = this` y crea en `+8`
  un `ThreadSafeRefCountedContainer<unsigned_int, pa::NPCScheduleData_File, …>` con nombre `"NPCSchedule"`.
- **[V] Otros escritores de H**, todos con `0x20853D0(ai, handle, &clave, lista)`, que escribe `[ai+0x90][0] = clave`
  y copia el *handle* a `[[ai+0x90]+8]`:
  - `ClientAiActorComponent` vf107 `0x91CD90` (vtable `0x55B87A0`): desenlaza con `0x9F3230`, fija con
    `0x20853D0` y enlaza con `0x9F2FA0`.
  - El manejador de `InitializeNPCScheduleAck` 2874, `0xC0CE50`: vuelve a resolver con `0x1869D70` en `0xC0D1EE`
    y, si es válido, llama a `0x9F3230` (`0xC0D216`), `0x20853D0` (`0xC0D235`) y `0x9F2FA0` (`0xC0D24D`).
  - Su emisor en el servidor, `0x2A55870`, solo lo llama `.xtls 0x10248FD2`.
  - 0 × 2874 en HBC y GBC.
- **[V]** `0x9F2FA0` es un segundo camino de enlace, el mismo cuerpo que la vf13 pero con la clave de escena como
  parámetro. Sus llamadores son `0x91CE05` (IA vf107) y `0xC0D24D` (2874).
- **[V] Qué hace el enlace** (`0x43B4B0`):
  1. Recorre las entradas de la instancia (`+0x138`, número en `+0x140`, paso `0x168`).
  2. Compara cada una con `0x1869D70(local_res8)` por fichero (`+0x10`), clave alta (`+4`) e índice (`+8`).
  3. Si `+0x28 == 0`, escribe allí la clave del NPC.
  4. Si la instancia tiene `+0x178` y el NPC está listo (`+0xC2 && +0xCF`), construye una petición de acción y
     llama a la IA `vf(0x190)`.
- **[V] `X+0x148` es el contenedor de escenas del gestor.** `0xA9FEE0` (2335) mete ahí la
  `pa::ClientNPCScheduleStage` (vector `+0x18`). Que el secuenciador del 2661 pase de `+0x170` a `+0x148` al
  terminar su `SequencerLoadingTask` es **[E]**.

### 2.2 Qué lleva el 2005: los nodos ya vienen en el registro del NPC [V]

En el 2005 de cada NPC aparece `u32 recurso | u32 nodo | u64 clave de escena`. El nodo es el mismo que el 2661
del dueño asocia a ese NPC.

| NPC (sesión 6) | Nombre / variante (2005) | Clave de escena | Recurso | Nodo (2005) | Nodo en el 2661 del anfitrión (HB:223/225) |
|---|---|---|---|---|---|
| `B010006D` | Damiane / 4 | `0x13` | `0xC4E5A00C` | `0x2000000C` | `0x2000000C->0xB010006D` |
| `B0100071` | Matthias / 50901 | `0x13` | `0xC4E5A00C` | `0x20000005` | `0x20000005->0xB0100071` |
| `B0100076` | Stranger / 3101 | `0x13` | `0xC4E5A00C` | `0x2000000D` | `0x2000000D->0xB0100076` |
| `B0100077` | Stranger / 3053 | `0x19` | `0xBD40FBCF` | `0x20000003` | `0x20000003->0xB0100077` |
| `B0100078` | Stranger / 3052 | `0x19` | `0xBD40FBCF` | `0x20000004` | `0x20000004->0xB0100078` |
| `B0100083` (control enlazado en el anfitrión) | — | `0x20000059` | `0xDA8B15D0` | `0` | sin 2661: escena de horario (2335 `#2391`) |

- **[V]** El registro de `B010006D` es igual en HBC (`#1571`) y en GBC (`#1467`), salvo 46 bytes variables en
  `0x1B89–0x2097`. El bloque `0x2242` (`0c a0 e5 c4 | 0c 00 00 20 | 13 00…`) es idéntico. **El invitado tiene el
  dato; le falta el fichero.**
- **[V]** En el 2661 `0x13` del anfitrión (`#1607`), `payload+27 = 0xC4E5A00C`, el mismo recurso. En el 2335 de
  `0x20000059` (`#2391`), `payload+9 = 0xDA8B15D0` y `payload+14` es la clave `0x20000059`.
- **[V] Clase de los 235 NPC** que salen en `local_res8` (GB, `GuestStage [NPC res8 0x9ED401]`), cruzados con los
  recursos que nombran los 2335/2661 de HBC:

| Origen de su recurso | NPC | Claves de escena más frecuentes |
|---|---:|---|
| 2335 (horario) | 199 | `0x2000018B` ×54, `0x20000014` ×54, `0x2000018A` ×38, `0x20000074` ×20, `0x2000001B` ×14 |
| 2661 (secuenciador) | 26 | `0x13` ×7, `0x16C` ×3, `0x19` ×2, `0x19D` ×2, `0x19F` ×2, otros ×1 |
| sin identificar | 10 | — (¿2195 `0x4000xxxx`, o preparados antes de la captura?) **[E]** |

### 2.3 Traza en el anfitrión para Damiane y para un NPC de horario [V/E]

| HBC | t | Paquete |
|---|---:|---|
| `#1188` / `#1195` | 43.817 / 43.826 | 2549 / 2144 (entrada del anfitrión en su campo de mundo) |
| `#1447…` | 44.113 | ráfaga de `CreateFactionSpawnStageAck` 2195 (`A0100001 | id | u64 0x4000xxxx`) |
| `#1571` | 44.132 | 2005 con **Damiane** `B010006D` (recurso `0xC4E5A00C`, nodo `0x2000000C`, escena `0x13`) |
| `#1606` / `#1607` | 44.214 | 2981 `0x13` / 2661 `0x13` (8 enlaces, recurso `0xC4E5A00C` en `+27`) |
| `#1609` | 44.214 | 2335 de `0x20000014` |
| `#2391` | 44.230 | 2335 de `0x20000059` (recurso `0xDA8B15D0`) |
| `#38259` | 44.516 | 2005 de `B0100083` (recurso `0xDA8B15D0`, escena `0x20000059`) |
| HB:368/369 | 44.532 | `P8Probe [binding 0x9ED431] … B0100083 … comp+0x2E0 0x20000059` y `[instance 0x9ED47F]` |
| `#38451` | 44.564 | 2322 |
| HB:1367 | 52.584 | `P8Probe [BodyAnim 0xA79770] … actor 0xB0100077` (8,4 s después de su 2005 `#1668`/2661 `#1671`) |

- **[V] Escena de horario:** el 2335 llega 286 ms antes que el 2005 y el enlace por la vf13 se produce 16 ms
  después del 2005. Es el camino «natural».
- **[E] Secuenciadores (Damiane):** en el anfitrión el 2005 llega **82 ms antes** que el 2661, y la vf13 se ejecuta
  unos 15 ms después del 2005 (medido en el invitado: GB, 2005 t=54.523, vf13 t=54.538). Lo más probable es que
  la vf13 del anfitrión también falle en `local_res8`. El enlace llegaría después por la **IA del controlador**
  (vf107 `0x91CD90` → `0x20853D0` → `0x9F2FA0`), que vuelve a fijar el *handle* cuando el fichero ya está
  cargado. Así se explica el BodyAnimation de `B0100077` 8 s más tarde. Las líneas de enlace de HB se cortaron en
  200 (presupuesto de la sonda) y no incluyen a estos NPC: **hay que medirlo** (§5, sonda H2).
- **[V]** En el invitado la vf13 se llama **una sola vez por NPC** (`NPC vf13 0x9ED290: hits 2077`, 235 con
  clave = `NPC res8 … hits 235`). Sin «cerebro» de IA (`0x9245B0` sale si `[[ai+0x48]+8] == 0`) no hay segundo
  intento: ni por vf107 ni por un 2404 (que, además, no llega a estos NPC).

### 2.4 Qué necesita el enlace, en orden [V salvo indicación]

1. **El fichero `NPCScheduleData_File` cargado** en `[0x6C8CDA0]+8` **cuando se lee el 2005** (o un camino que
   vuelva a fijar H después: vf107 con IA, o un 2874).
2. **El actor `comp+0x2D8`** (Kliff del anfitrión `A0100001`) como personaje en el cliente (`0x8AE1A0`). Existe en
   el invitado [V: GB 2981 route, «addressed actor 0x3F2F8F81180 (0xA0100001), its own manager 0x3F2F8F9E380»].
3. **La instancia `comp+0x2E0` en `X+0x148`**:
   - para horarios, creada por 2335 (`0xA9FEE0`, que también exige su búsqueda de recurso, `0x1862A80`, `local_58`
     no nulo);
   - para secuenciadores, por 2661 (`0xA9CA40`, que exige que **todos** los actores enlazados existan; si no,
     `0xA9CE81` la descarta);
   - **en X**, no en M.
4. **Entradas de enlace** de la instancia con el mismo fichero y nodo (las del 2661) y `+0x28 == 0`.
5. Para que la escena «actúe»: `instancia+0x178` y el NPC con `+0xC2 && +0xCF` (login terminado), **o** el
   evento `GameBodyAnimationEventData` de la carta de escena (`0xAE0A50 → 0xADED90 → 0xA79770`, que instala
   `CC+0x70/+0x78`; `p8-raiz-invitado.md` §3.3).

**Consecuencia para los 15 en A-pose [E fuerte]:**
- Su restauración (`0x8646D0`) corre unos 0,9 s después del 2005 (GB: `B010006D` vf13 t=54.538, `Restart
  0x86392A` t=55.452).
- Si falla, el NPC no llega a `+0xCF`, y entonces el punto 5 tampoco le da acción.
- Hace falta, además de la escena, que la restauración de un hash `seq_bodyanim_*` en un actor remoto no sea un
  error (`p8-actores-anfitrion.md` §4.1 (ii)) o que la carta se instale antes del login.

## 3. Pregunta 2: el «filtro» del servidor

### 3.1 Dónde está [V]

```
StartActionReq::vf2 0x296CAA0
  0x296CC64 call 0x2442BF0 (Deserialize_StartActionB)      ; 0x2442DBE: último u8 del bloque B -> B+0x281
  0x296CF02 call 0x2A78CA0 (ServerStartActionLogic)
     1. vf160/vf170: el usuario de la sesión == actor+0x90 (controlador), si no Err_NotControlledBySession
     2. 0x2A78D0D cmp byte [r15+0x281],0 ; r15 = B
        jne -> 0x2A78D1A call 0x232FF90 (jmp .xtls 0xEE67EE0) ; *ret = 0 ; return   <-- SIN DIFUSIÓN
     3. TS+0x4F8 < 0 y comps+0x1A8… (si no, DAT_146cf78d4)
     4. rango de posición
     5. 0x2A79032 call 0x2A7A8C0 (ServerStartActionOnServer) -> si 0 y su bandera de salida == 0 ->
        0x2A7907D call 0x2A797C0 (Broadcast) -> 0x2A7A560
```

- **[V] Correspondencia en el cable:** Req = clave (4) + A (`0x04–0x8D`) + B (`0x8E–0x2D3`) + u8 (`0x2D4`); el
  orden de lectura de `0x296CAA0` y `0x2442BF0` lo confirma. El último byte de B, `0x2D3`, es `B+0x281`.
- **[V] Captura (HBC, t ≥ 240, `StartActionReq` de 725 B de claves `0xB…`):**
  - **504** con `seq_bodyanim`, todos con `0x2D3 = 0x01`;
  - **11 160** sin él, todos con `0x2D3 = 0x00`;
  - ningún otro byte de B separa los dos grupos (barrido byte a byte).
- **[V] Recuento por clave (GBC frente a HBC):**
  - de los 26 NPC de secuenciador, **17** no reciben nada (`6D, 71–78, 7B, 7E, 7F, 80, 82, 1F5, 1F6, 1F9`), y en
    la ventana del usuario 2 el anfitrión solo manda 2223 con la bandera;
  - los 9 que sí reciben 2991 (`79, 7A, 7C, 1F7, 1F8, 1FA, 1FB, 1FC, 720`) tienen acciones de su propio gráfico;
  - los **199** de horario **reciben todos** relevos.
- **[V]** En la ventana no hay `ChangeAIStateReq` 2734 ni `MoveActorReq` 2556 de esos 17. Los 86 × 2734 y 17 × 2556
  son de t < 240, antes de la entrada del usuario 2. Por eso «0 × 2404/2697» no es otro filtro.
- **[V] Lo que no actúa en esto:**
  - `0x2A7A560` (difusión): solo exige `TS+0x4F8 < 0` y `def+0x10A & 1 == 0`, que no dependen de la acción.
  - `ServerAiActorComponent::vf68 0x2AC66D0` (2404): sin puertas.
  - Los destinatarios (`relay_fix` → `ToNear 0x2A75860 → 0x2BC51E0 → 0x2BC4F70`/`0x2BC4D50`, observadores del
    sector `TS+0x540`): son los mismos para todas las acciones de una clave, y la misma clave recibe unas y no
    otras (p. ej. `B0100079`: 12 × 2991 de su propio gráfico).
- **[V]** Ningún `Game error` del servidor en HB por esto: para NPC el error no genera Nak (`0x2A7C300` solo
  para tipo 1).

### 3.2 Qué significa [E fuerte]

- La bandera la pone el **cliente que controla** en las acciones de la escena (BodyAnimation). El servidor las
  aplica por su lado (`0x232FF90`) y **decide no reenviarlas**: el diseño supone que cada cliente que ve la escena
  la reproduce él mismo con su gestor de escenas.
- **No es** un filtro de interés ni un «propiedad de un *stage*» por actor. **Sí está ligado** al mismo desacople
  que S2/ESC: las escenas del gestor del dueño del campo solo existen en el cliente del dueño.
- **El arreglo no es quitar la bandera.** Con un relevo `seq_bodyanim` el invitado tampoco instalaría la acción
  (falta el gráfico dinámico, `RestartLayerAction … NOT FOUND (no fallback graph)`, GB `0x86392A`), y además
  rompería la semántica de autoridad del servidor.
- **Clase B de `p8-sesion6.md` §3.3 aclarada [V]:** `B010007B` y `B010007F` restauran bien pero sus únicas acciones
  posteriores llevan la bandera: se quedan «en bucle» en la acción del 2005.

## 4. Pregunta 3: opciones de raíz, por orden

Hechos de partida [V]:
- El cliente enruta todo Ack de escena al gestor del **dueño del campo** (`0x17FA6C0 → 0x9F8560 → ClientField::vf48
  0x9A5BE0`; `p8-observador-escenas.md` §14.1).
- El 2174 reescrito hace dueño al usuario 2, así que el destino es M.
- Los NPC buscan en **X** (`comp+0x2D8 = A0100001`).
- El servidor emite la familia de escena (2195, 2335, 2165, 2264, 2981, 2661, 2524, 2111, 2810, …) **solo al dueño**
  (`ToTarget` del actor del gestor, `0x2886FC3–0x2887035` para 2264; `0x28797BB–0x2879819` para 2810).
- 2661 solo sale de `0x287C200` y 2981 solo de `.xtls 0x103FB3FD`: ninguno se puede emitir de forma nativa para el
  usuario 2.

### A. Recomendada: «coobservador» en el transporte del anfitrión + enrutado por dueño de escena en el invitado

Es la forma practicable de (i) con la mitad servidor de (iii).

**Anfitrión (todo en `.rsrc`, hilo del servidor):**
1. **Duplicación en vivo.** Mientras el usuario 2 esté en el campo del anfitrión (lo sabe ya `ObserverMilestone`),
   cada paquete de la familia de escena que sale hacia la **sesión 1** se copia byte a byte a la cola de la
   sesión 2 (`g_outgoing`, `take_frame`). El orden relativo se conserva porque se copia en el mismo punto.
   - Punto: el envío de la sesión 1. El mod ya valida `NwVirtualAsyncSession::Send` (`kSessionSend`) y sustituye
     la vtable de la sesión 2.
   - Lista de opcodes cerrada: 2195/2130, 2335/2126/2411/2958/2499/2127/2165/2839/2909, 2264, 2981/2661/2810,
     2524/2111/2226/2140/2253/2224/2510/2222/2736/2266/2101, 2874.
   - Excluidos 2724 (misiones del dueño) y 2322 (S2, aparte).
2. **Instantánea en la entrada.** Un libro (el `obs::Ledger` de `observer_stage`, ampliado) guarda el último
   paquete de creación de cada escena viva (2195, 2335, 2661 con su 2981 previo) y el último estado (2981 por
   secuenciador; 2264 por escena, **[E]** si hace falta). Los borrados (2126/2130/2810/2411/2266) lo vacían.
   - En la **entrada del observador**, el libro se encola **antes** de la ráfaga de 2005 de NPC.
   - **[V]** En GBC, el 2549 del viaje 2 está en t=54.389 (`#1449`) y el primer 2005 en t=54.475 (`#1450`, 86 ms
     después), todo antes del 2144 (t=62.095). Por eso el punto tiene que ser el **inicio** de
     `MoveFieldCompleteCore 0x2BC7670` para un viajero que no es dueño, y no `0x2BC84E2` (final).
3. **Autoridad.** En la inyección de Req del invitado, descartar (y contar) `2767, 2591, 2436, 2173, 2547, 2283,
   2138, 3020, 2100` cuyo u64 de escena esté en el libro del dueño. Las del invitado (p. ej. `0x1A4`, ESC) siguen.
   - **[V]** Los `StartActionReq` del invitado para NPC ya los rechaza el servidor en la primera puerta de
     `0x2A78CA0` (controlador ≠ usuario de la sesión), sin Nak.

**Invitado (`.rsrc`):**
4. **Enrutado por el dueño de la escena.** Tras la llamada `vf48` de cada manejador de la familia (2661 `0xC06120`,
   2981 `0xC07746`, 2810 `0xC0659B`, 2264 `0xC0E236`, 2524 `0xC08175`, 2111 en `0xC08970`, 2335 en `0xC0AFC0`,
   2195 en `0xC0A870`, …), si el actor del paquete (`payload+1`) es un **personaje distinto** del dueño del campo y
   tiene `ClientSequencerStageManager`, se sustituye el gestor por **el suyo** (X).
   - Es un gancho intermedio sobre `rax` después de `vf48`, por manejador. Hay que localizar el punto exacto de
     2335/2195/2111.
   - **No** se toca `0x9A5BE0`: lo usan los estados de carga (`0x779E43`, `0x77AA6C`, `0x77AE9A`, `0x77D03C`), que
     deben seguir viendo M (S2 aceptado).
5. **Orden en el invitado.** Las escenas tienen que existir y el fichero tiene que estar en el contenedor
   **antes** de que se lea el 2005 del NPC (§2.3: no hay segundo intento sin IA). Dos maneras:
   - en `guest_deliver`, retener los 2005 de la entrada hasta que los ids de fichero del libro estén en
     `[0x6C8CDA0]+8` (réplica de solo lectura de la búsqueda de `0x186A810`, como ya se hace con
     `ClientActorContainer::Find`), con tiempo límite;
   - o, cuando no se pueda garantizar el orden, entregar después un **2874** por NPC con los mismos
     `(recurso, nodo, clave)` de su 2005, que es el camino nativo de reenlace (`0xC0CE50`). Esto último es contenido
     sintético: necesita visto bueno.
6. **Complemento para los 15:** la restauración de un hash `seq_bodyanim_*` en un actor remoto no debe dar error
   (`p8-actores-anfitrion.md` §4.1 (ii)), para que el login termine y la escena le dé la acción.

**Efectos y riesgos [E]:**
- **Conducción:** con escenas en X, ¿manda el cliente del invitado 2767/2591/2436? En el dueño las manda el cliente
  dueño (HBC: 865 × 2767, 152 × 2591, 63 × 2436). Hay que medirlo; el filtro 3 cubre la autoridad.
- **Efectos locales:** el 2661 `0x13` enlaza también al Kliff del anfitrión (`0x20000003->0xA0100001`). Hay que
  vigilar cámara, entrada e interfaz del invitado al enlazar un personaje remoto (dictamen §7.2 punto 7).
- **Tráfico:** 36 293 × 2264 en unos 6 minutos (HBC). El enlace del túnel ya va justo durante la carga
  (`p8-sesion6.md` §2.5). Quizá baste con 2264 de las escenas que tengan NPC enlazados cerca.
- **Entrada tardía:** el 2661 copiado reinicia la secuencia desde su preparación; el estado actual de rama llega en
  el último 2981. Las cinemáticas largas pueden verse «desde el principio» [E].
- **Colisión de ids:** M y X son objetos distintos, así que no se mezclan con las del usuario 2 [V: dos gestores].
- **2174 intacto y S2 sin cambios.**

**Qué medir primero:** §5, sondas G1–G3 y H1–H3.

### B. Dueño real en el 2174 (ii) + duplicación del anfitrión

- **Qué cambia:** con el 2174 nativo, `0x9A5BE0(Q)` devuelve **X** y todos los Ack de escena caen en X sin el
  enrutado 4. La duplicación 1–3 sigue haciendo falta, porque el servidor solo emite al dueño.
- **Lo que exige** (`p8-cierre-campo.md` §1):
  - que el usuario `0x90100000` (`+0x5E & 4`) y su personaje existan en el cliente **antes del estado `0x0A`**.
    Hoy llegan en la ráfaga de 2005 posterior al 2549 [V: `p8-actores-anfitrion.md` §1.1, 2549 85,989 → 2005 con
    `U(90100000)` 86,640].
  - Hay que adelantarlos (un `[U][A0100001]` antes del 2174), y eso no es nativo.
  - `.xtls 0x940B170` busca en X el nodo de destino `P+0x200`, creado en el estado 4 con el contexto del jugador
    local (en M): **[E]** probable fallo de búsqueda, con efecto desconocido.
  - Los estados `0x77AA8B`/`0x77B0CE` escribirían `X+0x231`, y el 2322 del usuario 2 caería en X.
- **Ventaja:** encaja con el modelo del servidor (un gestor por dueño de campo). El ESC `0x1A4` y sus 2111/2810
  irían a X, donde el servidor también los guarda.
- **Riesgos:** las escenas y misiones propias del invitado se mezclan con las del anfitrión. Hay cierre si el orden
  falla (`0x940B223`). La reconexión, la salida y el cambio de campo dependen de mantener ese orden.
- **Qué medir primero:** con Shadow, solo lectura: en qué estado está P cuando llega el 2005 con `U(90100000)` y si
  `0x9A5BE0(Q)` con el dueño nativo devolvería X (réplica de lectura de `0x8AE390`).

### C. Réplica desde el cliente del anfitrión (i) «pura»

- **Qué es:** los mismos bytes, tomados del `OnRecvPacket` del cliente del anfitrión (sniffer) y llevados al
  invitado por el túnel.
- **Problemas:** no controla el orden respecto a la ráfaga de 2005 del servidor (otra cola, otro hilo), y duplica
  lo que A hace en el origen. Solo como plan B si el envío de la sesión 1 no se puede enganchar con seguridad.

### D. Descartada: que el servidor reenvíe las acciones con bandera

- **Cómo sería:** saltar `0x2A78D0D` para observadores.
- **Por qué no:**
  - el invitado no tiene el gráfico dinámico, así que la instalación del hash fallaría como la restauración [V GB
    `0x86392A`];
  - el servidor ya aplica la acción por `.xtls` y no tenemos los argumentos del difusor para ese caso;
  - cambia una decisión nativa sin arreglar el enlace.

## 5. Pregunta 4: plan para una sesión de Shadow

### 5.1 Antes de Shadow (un PC, anfitrión y `cdmp_peer`; lo del servidor no necesita Shadow)

| # | Sonda (solo lectura, `.rsrc`) | Qué decide |
|---|---|---|
| H1 | Gancho intermedio en `0x2A78D0D` (antes del `cmp`). Ahí, `r14` = 1.er argumento (CC del servidor), `rbx = [r14+8]` = actor (`0x2A78CCB`) y `r15` = bloque B (`0x2A78D06`). Registrar la clave del actor (`kServerActor_Key`), `[r15+0x281]`, el controlador `[rbx+0x90]` y un contador por clave; una línea por clave nueva y un resumen cada 60 s | Confirma en vivo §3 (bandera → sin difusión) y da la lista de NPC «de escena» del dueño |
| H2 | Entradas de `0x9F2FA0` y `0x20853D0` en el **cliente del anfitrión**, con la dirección de retorno (`0x91CE05`/`0x91CDDA` = IA vf107; `0xC0D24D`/`0xC0D235` = 2874) y la vf13 (`0x9ED401`, `local_res8`) para las claves de los 2661 | Saber si Damiane y su grupo se enlazan por la IA (hipótesis §2.3) o por la vf13 |
| H3 | Libro ampliado (`observer_stage_sync=1`): 2195/2335/2264/2165/2981/2661/2810/2126/2130/2411 hacia la sesión 1, con `corr=`, recurso y clave de escena. En la entrada del observador: `PLAN, NOT SENT` con el orden y la **posición** respecto al primer 2005 encolado a la sesión 2 | Dónde insertar la instantánea (inicio de `0x2BC7670` o antes) y cuánto ocupa |

### 5.2 Sesión con Shadow, tramo 1: solo medida (unos 5 minutos en el campo del anfitrión)

| # | Invitado (solo lectura) | Criterio |
|---|---|---|
| G1 | Tras `0x206DE6B` (vuelta de `0x1EB88C0`): clave del NPC, `(recurso, nodo, clave de escena)` leídos de `[[ai+0x90]]` y `[[ai+0x90]+8]` (`clave`, `índice`, `fichero`) | `fichero = 0`, `índice = -1` en los 225, con el recurso del 2005 → confirma §1.1 |
| G2 | Cada 5 s, réplica de lectura de la tabla de `[[0x6C8CDA0]+8]` (`+0x20` cubos, `+0x30` tabla, `+0x38` entradas, `+0x15` descargando): ¿están `0xC4E5A00C`, `0x5FB728ED`, `0xAC00B3BB`…? En el anfitrión, lo mismo | Ausentes en el invitado y presentes en el anfitrión → confirma §1.2 |
| G3 | X (`[[[A0100001+0x68]+0xA8]+0x18]`): `+0x148` (n en `+0x20` del vector `+0x18`), `+0x174`, `+0x231` | Línea base de X antes de cualquier réplica |
| G4 | Contar C>S del invitado: 2767/2591/2436/2173/2223 de claves `0xB…` | Línea base de autoridad (hoy: 0/12/1/12; 2223 solo propios) |

### 5.3 Tramo 2: experimento mínimo (clave nueva, apagada por defecto, una sola escena de horario)

1. **Elegir la escena** por **recurso**, que es estable entre sesiones **[E]**. Las claves `0x2000xxxx` y las de
   actor cambian. Por ejemplo, el recurso de la escena con más NPC (`0x5FB728ED` o `0xAC00B3BB`, 54 NPC cada una en
   la sesión 6).
2. **Anfitrión:**
   - en la entrada del observador (inicio de `0x2BC7670`, viajero = usuario 2, no dueño), encolar a la sesión 2
     **solo el 2335** de esa escena;
   - durante la estancia, duplicar sus 2264/2165/2126;
   - filtrar las Req de escena del invitado con ese id.
3. **Invitado:** enrutado 4 **solo** para 2335/2264/2165/2126 con actor `A0100001`.
4. **Éxito:**
   - G1: `fichero ≠ 0` para esos NPC;
   - `[NPC res8] passes`, `[NPC owner 0x9ED453] … searches manager X` y `[NPC instance 0x9ED48C] FOUND`;
   - en X, `+0x148` = 1 más;
   - G4 sin Req nuevas de esa escena;
   - sin cierres;
   - a la vista, esos NPC hacen su rutina, no un bucle.
5. **Aborto:** cualquier C>S de escena con ese id desde el invitado, un efecto de cámara o de control en el
   invitado, o un error de `0xA9FEE0` (`local_58 == 0`).

### 5.4 Tramo 3 (solo si el 2 sale bien): un secuenciador

- 2981 + 2661 de `0x13` (recurso `0xC4E5A00C`: Damiane, Matthias, tres Wandering Freesword, Barden Middler,
  Stranger y el Kliff del anfitrión en `0x20000003`).
- **Antes** de su 2005 (retención en `guest_deliver` hasta G2 = presente), con el complemento 6 activado para esos
  NPC.
- **Éxito:**
  - `BodyAnim 0xA79770` en el invitado para esas claves;
  - `CC+0x70/+0x78 ≠ 0`;
  - la restauración ya no da `CHART FAILED`, o se omite como «sin restauración»;
  - a la vista, no hay A-pose.
- **Vigilar:** el Kliff del anfitrión enlazado en el invitado (cámara o entrada) y que el invitado **no** mande
  2767/2436/2591 de `0x13`.

### 5.5 Qué no hacer en esa sesión

- Retirar la reescritura del 2174 (opción B) a la vez que A.
- Reenviar acciones con bandera.
- Forzar `local_res8`, H, `X+0x148` o `M+0x231`.
- Copiar 2661 al M del invitado sin el enrutado 4 (B1 de `p8-observador-escenas.md`).

## 6. Correcciones a informes anteriores

| Afirmación | Corrección |
|---|---|
| `p8-sesion6.md` §3.2: «el servidor no les reenvía nada… filtro de visibilidad de escena personal» y §3.4 / §8 «el nodo lo pone el 2661 [E fuerte]» | **[V]** No es visibilidad: es la bandera por acción `B+0x281` (`0x2A78D0D`). Los 0 × 2404/2697 se deben a que no hubo Req en la ventana. El nodo lo trae el **2005 del NPC** (IA, `0x1EB88C0`); el 2661/2335 solo hacen falta para que el fichero esté cargado y para la instancia en X |
| `p8-observador-escenas.md` §14.4: «`local_res8`, el nodo de escena del propio NPC (`[[npc+0x68]+0x58]+0x90…`)» | **[V]** `[npc+0x68]+0x58` es el **componente de IA**; `+0x90` son sus datos de escena (`[0]` clave, `[8]` *handle* al `NPCScheduleData_File`). `comp+0x2E0` es la clave de escena (secuenciador **o** escena de horario `0x2000xxxx`) |
| `p8-observador-escenas.md` §14.3 [E]: «no está cartografiado cómo se llena `[gestor+0x148]`» | **[V]** para horarios: `0xA9FEE0` inserta la `ClientNPCScheduleStage` en `+0x148`. Para el 2661 sigue siendo [E] (paso desde `+0x170` al terminar la `SequencerLoadingTask`) |
| `p8-sesion6.md` §3.2: «el cliente del anfitrión sí manda por ellos 2–232 StartActionReq, 1 MoveActorReq y 4–5 ChangeAIStateReq» | **[V]** Son totales de toda la captura. Con el usuario 2 presente solo hay 2223, y con bandera |
| `p8-sesion6.md` §3.4, clase C | **[V]** 199 de los 235 son NPC de **escenas de horario** (2335) que sí reciben relevos. Les falta la escena, no las acciones |

## 7. Qué sigue sin demostrar

- **[E]** Que 2335/2661 sean lo que **carga** el `NPCScheduleData_File` en el cliente, y no otra cosa que les
  acompañe. Lo decide G2.
- **[E]** Cómo se enlaza Damiane en el anfitrión si su 2005 llega antes que el 2661 (IA vf107). Lo decide H2.
- **[E]** Que el cliente del invitado no conduzca las escenas de X (C>S). Lo decide G4 en el tramo 2.
- **[E]** Qué son los 10 NPC sin recurso identificado (¿2195?).
- **[E]** Quién pone la bandera `0x2D3` en el cliente (probablemente al construir la Req de una acción
  BodyAnimation): no es necesario para el diseño.
- **[E]** El orden exacto entre el inicio de `0x2BC7670` y el `NotifyAppear` de los NPC al observador. Lo decide H3.

## 8. Reproducibilidad

- **Scripts** (scratchpad de esta sesión, no se conservan): lector CDPK (`observador/scripts/cap.py`); comparación
  de opcodes; búsqueda de claves y recursos en los payloads; partición de los 2005 en registros (`u16 len |
  registro`); cruce «235 NPC × recursos de 2335/2661»; barrido byte a byte de los 2223 con y sin bandera; capstone
  (`observador/scripts/pedis.py`).
- **Ghidra:** copia propia del proyecto en el scratchpad, `analyzeHeadless … -process CrimsonDesert.exe
  -noanalysis -readOnly` con un script de exportación de solo lectura. La copia se borró al terminar. El proyecto
  original no se abrió.

## 9. Implementado para la medida (tramo 1: H1–H3 y G1–G4)

10 de octubre de 2026, rama de trabajo sobre `opcion-d` `6f3e992`. **Solo medida**: ninguna sonda escribe en el juego,
llama a una función del juego, envía, retiene ni reordena un paquete, ni cambia una bandera o un valor de retorno. Dos
claves nuevas en `[session2]`, las dos a 0 por defecto:

| Clave | PC | Qué enciende | Requisitos |
|---|---|---|---|
| `s1_scene_diag=1` | anfitrión | H1, H2, H3 (`src/mod/session2/host_scene_diag.*`) | `create_session=1`. Instala `observer_stage` en modo medida si `observer_stage_sync=0` (usa su hilo). El libro de escenas sale de la captura del cliente propio: `[sniffer] enabled=1` |
| `guest_scene_diag=1` | invitado (`guest=1`) | G1, G2, G3, G4 (`src/mod/session2/guest_scene_diag.*`) | ninguno más |

Lógica pura (sin acceso al juego) en `src/mod/session2/scene_diag_logic.h`; los puntos G1 compartidos en
`scene_points.h`; tests en `tests/optd_scene_diag_tests.cpp` (datos en `tests/data/p8s6_*.bin`). Inis:
`docs/research/ini/cdmp.p8-s1-host.ini` y `cdmp.p8-s1-shadow.ini` (= `cdmp.p8-2322-host-B.ini` /
`cdmp.p8-2322-shadow.ini` con la clave nueva a 1; en esos dos la clave queda comentada).

### 9.1 Verificación de los puntos [V]

Cada punto se comprobó sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` (capstone, Python `-I`) con los scripts
de `observador/scripts/` (`verify2.py`, `regw.py`, `genbytes.py`, `rorder.py`, `isins.py`, y `pedis.py` con
`func_whole`, que une los trozos encadenados de `.pdata`: `0x206DBD0` está partida en tres):

- el parche entero dentro de `.rsrc`, instrucciones completas desde el inicio de la función (todos sus trozos);
- ninguna rama de la función y ningún `rel32` real (`E8`/`E9`/`0F8x` de `.rsrc`, cada origen redesensamblado; los
  falsos positivos de bytes sueltos se descartan, p. ej. `0x38F2A6F → 0x2A79064` no es instrucción) cae dentro;
- ningún operando relativo a RIP en los bytes reubicados;
- los registros que lee el callback no se escriben entre su definición y el punto (`regw.py`), y no hay ramas que
  entren saltándose la definición;
- ninguno es un punto de `P8Probe`, `GuestStage` u `ObserverStage` ni está dentro de los bytes que esos verifican.

En el juego, antes de enganchar: bytes esperados y bytes de «contexto» (el código del que depende la lectura de
registros) comparados con la build; luego `p8_install_mid` (los bytes reubicados deben ser exactamente los
verificados; si no, `REFUSED` y nada se escribe). Un punto rechazado no impide los demás.

### 9.2 Tabla de ganchos

| Punto | Sonda | Parche (bytes verificados) | Registros / lecturas | Contexto comprobado |
|---|---|---|---|---|
| `0x2A78D0D` | H1 bandera (`ServerStartActionLogic 0x2A78CA0`, único llamador `0x296CF02`) | 8: `41 80 BF 81 02 00 00 00` (`cmp byte [r15+0x281],0`) | `rbx` actor (clave `+0x60`, controlador `+0x90`, usuario `[+0xA0]+0x60`); `r15` = B (`[B+0x281]` = byte `0x2D3`); `[r12]` = las dos capas de acción (`+0`/`+4` y `+0x50`/`+0x54`, los hashes); `r13` = sesión | `0x2A78CB0` (marco), `0x2A78CBF` (`r13`,`r12`,`rsi`,`r14`), `0x2A78CCB` (`rbx`), `0x2A78D06` (`r15`), `0x2A78D15` (`je; call 0x232FF90; …; jmp` = aplicar sin difundir) |
| `0x2A79060` | H1 difusión | 7: `80 BD F0 00 00 00 00` (`cmp byte [rbp+0xF0],0`) | `[rbp+0xF0]` (bandera de salida de `ServerStartActionOnServer`; 0 → `0x2A7907D call 0x2A797C0`), actor `[r14+8]` | `0x2A78FF8` (`lea rax,[rbp+0xF0]`), `0x2A7906E` (la llamada de difusión), marco y argumentos |
| `0x1EB8946` | G1/H2 lectura interna (`0x1EB88C0`, único llamador `0x206DE66`) | 7: `49 8B 56 08 48 8B 08` (tras `call 0x1869D70; nop`) | `[rbp+0x50]` recurso, `[rbp+0x54]` nodo, `rax` → resultado de `0x1869D70` (`u64`, índice `+8`, fichero `+0x10`), `r14` = X1 | `0x1EB88CB`, `0x1EB88D5`, `0x1EB88F8`, `0x1EB8938` |
| `0x206DE6B` | G1/H2 (vuelta de `0x1EB88C0` en vf11 `0x206DBD0`) | 8: `84 C0 0F 84 CB 00 00 00` (`test al,al; je rel32`, reubicado) | `rdi` = componente de IA; actor `[rdi+8]`; X1 `[rdi+0x90]`: clave `[X1]`, `u8 [X1+0x61]`, handle `[X1+8]`; componente de reproducción `[[actor+0x68]+0xB0]` si su vtable es `0x55BEE78` (`+0x2D8`, `+0x2E0`) | `0x206DBE2` (`mov rdi,rcx`), `0x206DE5C` (la llamada) |
| `0x9ED401` | H2, solo anfitrión (vf13 `local_res8`; en el invitado es punto de `GuestStage`) | 10: `83 7D 30 00 0F 84 30 02 00 00` | `r14` componente, `[rbp+0x30]` | `0x9ED2A8`, `0x9ED2B5` |
| `0x20853D0` | H2 entrada (re-fijar el handle de la IA) | 5: `40 53 55 56 57` | `rcx` IA, `rdx` handle, `[r8]` clave, `[rsp]` retorno (`0x91CDDF` = vf107 de la IA, `0xC0D23A` = 2874) | `0x91CDDA`, `0xC0D235` (las dos llamadas) |
| `0x9F2FA0` | H2 entrada (segundo camino de enlace) | 5: `48 89 5C 24 10` | `rcx` componente, `[rdx]` clave, `[rsp]` retorno (`0xC0D252` = 2874; vf107 llega por `jmp` desde `0x91CE05`) | `0x9F2FAE`, `0x91CE05`, `0xC0D24D` |
| `0x9F30CB` | H2 `local_res8` del segundo camino | 10: `83 7D 20 00 0F 84 45 01 00 00` | `rdi` componente, `[rbp+0x20]`, `[rsi]` clave | `0x9F2FAE`, `0x9F30A5` (de dónde sale `[rbp+0x20]`) |
| `0x9F312D` | H2 instancia en X+0x148 | 7: `48 8B 4D B8 48 85 C9` (el `je rel32` siguiente se comprueba, no se parchea) | `[rbp-0x48]` instancia, `[rbp-0x20]` actor dueño → X | `0x9F3118` (búsqueda en `+0x148`), `0x9F313A` (`call 0x43B4B0`) |

Bytes completos (16 por punto) en `host_scene_diag.cpp` y `scene_points.h`. Sin gancho: G2, G3 (lecturas desde el
hilo de diagnóstico), G4 (`guest_send`), la entrega en el invitado (`deliver_all`), H3 (`take_frame`, `server_inject`,
el gancho de entrada `0x2BC84E2` de `observer_stage` y un *tap* del hilo escritor del sniffer).

**Formatos de cable usados** [V por orden de lectura del manejador y por las capturas]: 2335 `0xC0AFC0` (`u8 | u32 actor
| u32 | u32 recurso @9 | u8 | u64 clave @14 | …`); 2126 `0xC0B840` (`u32 actor | u64 clave`); 2411 `0xC0DB40` (`u32
actor | u32`, **[E]** comparado con los 32 bits bajos de la clave); 2874 `0xC0CE50` (`u32 actor | u32 NPC | u32 recurso
| u32 nodo | u64 clave | 10 × 4 u8`, 64 B); 2661 (recurso en `payload+27`); 2005 (`n × (u16 len | registro)`, registro
`u8 | u32 clave | …`); 2223 (`u32 clave | 2 × 69 B de capas | B | u8`, hashes en `+4/+8/+73/+77`, bandera en `0x2D3`);
C>S de escena: 2767 `u64 escena | u32 | u8 | u32 actor @13 | u8`, 2591/2436/2138 `u64 escena @0`, 2173 actor `@4`, 3020
actor `@0`; 2547/2283 **[E]** (no aparecen en las capturas: se supone el `u64` en `@0`).

### 9.3 Coste

- **H1** (`0x2A78D0D`, `0x2A79060`): cada `StartActionReq` que trata la lógica del servidor (unas 35/s de NPC en la
  sesión 6). Por llamada: el `MidHook` (guardar contexto), ~8 lecturas con SEH y dos sumas atómicas en tablas sin
  candado (`CountTable`, 8192 claves × 6 columnas y 1024 hashes). Solo la primera vez de cada clave y las 2 primeras
  acciones con bandera de cada NPC pasan al anillo.
- **G1/H2**: una vez por bloque de IA leído (≈ 1 por NPC en cada carga de zona, ~235); vf13 y el segundo camino, las 3–4
  primeras llamadas de cada NPC con línea; el resto solo se cuenta.
- **H3**: `take_frame` (hilo del servidor que envía a la sesión 2): dos atómicos por paquete; en los 2005, un recorrido
  sin asignación de sus registros (≤ 256) y, si hay NPC, una copia del paquete (montón del mod, no el del juego) a una
  cola acotada (512 paquetes / 8 MB; lo que no cabe se cuenta). El sniffer: un `switch` por paquete en su hilo
  escritor. `server_inject`: un contador.
- **Invitado**: G4 y la entrega, un `switch` y como mucho un registro fijo por paquete en el NetworkThread del cliente.
  G2/G3 cada 5 s en el hilo de diagnóstico: ≤ 64 búsquedas acotadas (≤ 31 pares por cubo, ≤ 40 pasos de búsqueda
  binaria) y ≤ 8 inventarios de X.
- **Formato**: anfitrión, el hilo de `observer_stage` cada 1 s; invitado, `guest_diag_worker` cada 5 s. Nunca en un
  hilo del juego ni en el de red. Anillos de 8192 (anfitrión) / 4096 (invitado), límites por ventana de 5 s (512; 4096
  para «to session 2»; 1024 en el invitado) y tope de líneas por tipo (2500 / 3000): todo descarte se cuenta en las
  líneas `SceneDiag counters` / `GuestScene [G1 summary]`.

### 9.4 Líneas esperadas

Anfitrión (`cdmp.log`, prefijo `SceneDiag`):

```
SceneDiag ON (s1_scene_diag, host, READ ONLY): hooked: H1 flag 0x2A78D0D H1 relay 0x2A79060 G1 inner 0x1EB8946 G1 0x206DE6B vf13 res8 0x9ED401 AI rebind 0x20853D0 rebind 0x9F2FA0 rebind res8 0x9F30CB rebind instance 0x9F312D | REFUSED: none | ...
SceneDiag [session 1 (sniffer)] qpc … capture t=44.214 | 2661 PrepareSequencerAck actor 0xA0100001 owner 0xA0100001 sequencer 0x13 resource 0xC4E5A00C bindings 8 | corr=2661:A0100001:0x13:0x8 | live sequencers 1
SceneDiag [session 1 (sniffer)] … | 2335 CreateNPCScheduleStageAck actor 0xA0100001 scene 0x20000059 resource 0xDA8B15D0 (+5 0xF505F +13 0) | corr=2335:A0100001:0x20000059:0xDA8B15D0 | live schedules 2
SceneDiag [G1 AI block 0x206DE6B] … | NPC 0xB010006D (#1) | corr=2005:B010006D:0x13:0xC4E5A00C | AI block: resource 0xC4E5A00C node 0x2000000C scene key 0x13 … -> INVALID: FILE NOT LOADED …
SceneDiag [vf13 res8 0x9ED401] … | NPC 0xB010006D (#1) … | local_res8 0xFFFFFFFF00000000 EXITS (no stage node) | +0.015 s after its 2005; its scene 0x13 = a sequencer (2661) of the host's ledger, created +0.082 s relative to its 2005
SceneDiag [AI rebind 0x20853D0] … | NPC 0xB010006D (#1) from the AI's vf107 (0x91CDDF) | … -> VALID (the stage node resolves) | … | +8.4 s after its 2005 …
SceneDiag [rebind instance 0x9F312D] … | NPC 0xB010006D … instance in X+0x148: 0x… FOUND: 0x43B4B0 binds it now …
SceneDiag [H1 flag 0x2A78D0D] … | flagged #1: actor 0xB0100077 … action hash 0x35DD392F … B+0x281 (wire 0x2D3) = 1 -> applied by 0x232FF90 and NOT relayed (kept local) | from session 0x… | user 2 logged in
SceneDiag [trip] … | trip 2 of user 2: 2174 t=0 | 2549 +5.445 s (x1) | entry 0x2BC84E2 … | 2144 +13.151 s | 2322 … | first NPC 2005 +5.531 s (packet #… ) -> between 2549 and 2144: the START of MoveFieldCompleteCore 0x2BC7670 … | NPC records: … | … the record carries the host's block N … | PLAN, NOT SENT: a snapshot of the host's ledger (field #k: 199 x 2335, 26 x 2981+2661) would go before packet #…
SceneDiag [H1 summary] / [H2 summary] / [H3 summary] / SceneDiag counters   (cada 60 s)
SceneDiag [G2 host] … | resource 0xC4E5A00C in the host client's NPCScheduleManager …: file loaded and node found …
```

Invitado (prefijo `GuestScene`):

```
GuestScene ON (guest_scene_diag, READ ONLY): hooked: G1 inner 0x1EB8946 G1 0x206DE6B | REFUSED: none | ...
GuestScene [G1 AI block 0x206DE6B] … | NPC 0xB010006D (#1) | corr=2005:B010006D:0x13:0xC4E5A00C | … -> INVALID: FILE NOT LOADED (NPCScheduleData_File absent) | this read came +0.015 s after its 2005 was handed to the game | its scene on this client: NO 2335/2661 of it reached this client | …
GuestScene [G2] … | resource 0xC4E5A00C (7 NPC(s), node 0x2000000C) in NPCScheduleManager's container 0x… : FILE NOT LOADED …
GuestScene [G3 X] … | character 0xA0100001: actor 0x… | X = its stage manager 0x… (ClientSequencerStageManager) | X+0x148 0x… holds 0 schedule stage(s) | X+0x174 … | X+0x231 … (first)
GuestScene [G4 C>S] … FIRST 2591 of this client for scene 0x1A4 (payload 17 bytes) | from now on only counted
GuestScene [G1 summary] / [G2 summary] / [G4 summary]   (cada 60 s)
```

Los `corr=` siguen el formato de `ObserverStage`/`GuestStage` (`corr=op:actor:seq:valor`): 2661 `valor = vínculos`,
2810 el `u32`, 2335 `seq = clave de escena, valor = recurso`, 2126 `valor 0`, 2411 `seq 0, valor = id`, 2874 `seq =
clave, valor = NPC`, y para el bloque de IA de un NPC `corr=2005:NPC:clave:recurso` (el mismo en H2 y en G1: un `grep`
del token encuentra el mismo NPC en los dos logs).

### 9.5 Cómo leer los resultados para decidir el tramo 2 de la opción A (§5.3)

1. **H1 confirma el filtro (§3).** En `[H1 summary]`, con el usuario 2 conectado (columna `/in`), los NPC de escena
   deben tener `F > 0` y `B = 0` en esas acciones, y los hashes con bandera deben ser `0x35DD392F`/`0x837487D4`. Las
   acciones sin bandera de esas mismas claves deben dar `B > 0`. Si es así, la opción D (§4.D) sigue descartada y el
   tramo 2 no toca la bandera. Si aparece `F` con `B > 0` para la misma clave, el filtro no es solo `0x2D3` y hay que
   revisar §3 antes de seguir. El recuento del cliente propio (`the host client's own 2223 (sniffer)`) debe coincidir
   en orden de magnitud con `flagged`.
2. **H2 dice cómo se enlaza Damiane en el anfitrión (§2.3, [E] → [V]).**
   - Si `vf13 res8 … EXITS` y, más tarde, `AI rebind 0x20853D0 from the AI's vf107` con handle `VALID`, seguido de
     `rebind res8 … passes` e `instance … FOUND`, la hipótesis queda confirmada: en el anfitrión el enlace llega por la
     IA. En el invitado no hay segundo intento. Por tanto, el tramo 2 **tiene que** cargar el fichero **antes** del
     2005 del NPC (retención en `guest_deliver`) o entregar después un 2874 (contenido sintético, necesita visto bueno).
   - Si `vf13 res8 … passes` en el anfitrión (el 2661 llegó antes que el 2005), basta con el orden.
   - Para los NPC de horario se espera `passes` unos 16 ms después del 2005: el camino natural.
3. **H3 fija dónde iría la instantánea.** La línea `[trip]` da la posición del primer 2005 de NPC:
   - antes del 2549 → encolar en el 2174;
   - entre 2549 y 2144 → inicio de `MoveFieldCompleteCore 0x2BC7670` (lo esperado según GBC);
   - después del 2144 → bastaría el gancho de entrada `0x2BC84E2`.

   El libro (`field #k: N x 2335, M x 2981+2661`) da el tamaño de la instantánea. `the record carries the host's block`
   confirma en vivo que el 2005 que recibe el usuario 2 trae el mismo `(recurso, nodo, clave)` que leyó el anfitrión
   («al invitado le falta el fichero, no el dato»).
4. **G1 + G2 deciden §1.1–§1.2.**
   - G1 debe decir `FILE NOT LOADED` en unos 225 NPC (no `node not found`).
   - G2 en el invitado debe dar `FILE NOT LOADED` para los recursos de esos NPC (`0xC4E5A00C`, `0xAC00B3BB`,
     `0x5FB728ED`, …), y `[G2 host]` debe dar `found` para los mismos en el anfitrión.
   - Si el invitado sí tuviera el fichero (`node not found`), la causa sería otra y el tramo 2 no sirve como está.
5. **G3 es la línea base de X.** El gestor del Kliff del anfitrión existe en el invitado. `X+0x148` tiene `n`
   escenas de horario, previsiblemente 0 de las del anfitrión. Éxito del tramo 2: `n + 1`, y G2 pasa a `found`.
6. **G4 es la línea base de autoridad.** Recuentos por opcode y por id de escena. En GBC (sesión 6): 2591 ×12 y 2436
   ×1, todos de la escena propia `0x1A4`; 2173 ×12, de su propio personaje; 2767 ×0. En el tramo 2 es criterio de
   aborto **cualquier** C>S con el id de la escena elegida.
7. **Elección de la escena del tramo 2:** el recurso con más NPC según `[G2 summary]`/G1 que el anfitrión tenga como
   2335 vivo en `[H3 summary]` (estable entre sesiones **[E]**). Se pasa al tramo 2 solo si 1–4 salen como se espera.

### 9.6 Límites conocidos de la medida

- G1 empareja las dos mitades por hilo y por X1. Si el lector `0x1EB88C0` sale antes (lectura fallida), la línea dice
  «resource/node from the handle only».
- Que vf11 solo se llame al leer el 2005 es **[E]**: el bloque de IA se lee en `ClientAiActorComponent::vf11`, y el 2005
  es lo único que se ha visto llamarla.
- H1 cuenta las acciones que llegan a la lógica del servidor. Las Req del invitado para NPC se rechazan antes, en la
  primera puerta. «Usuario 2 presente» se muestrea una vez por segundo (`server_user2_present`).
- H3 data el 2549 cuando entra en la cola de inyección, no cuando lo procesa el servidor. Las copias de 2005 se
  limitan a 512 / 8 MB por segundo.
- El libro del anfitrión se alimenta de la captura del cliente propio. Si el sniffer pierde paquetes (`# DROPPED` en
  `cdmp_packets_*.log`), el libro queda incompleto. Cada 2174 del anfitrión empieza un campo nuevo.
- G3 busca X para `0xA0100001` (el primer personaje del servidor del anfitrión en todas las sesiones hasta hoy) y para
  los personajes `0xA…` que aparecen como dueño de escena o como destinatario de un paquete de escena. Si el Kliff del
  anfitrión tuviera otra clave y no apareciera, G3 no lo vería **[E]**.
- 2547/2283 (C>S) y 2411 tienen formato supuesto **[E]**: solo afectan a la columna «id» de G4 y a la baja de horarios
  del libro.

### 9.7 Build y tests [V]

`.\scripts\build.ps1` (RelWithDebInfo) en el árbol de trabajo: compila sin avisos en los archivos nuevos o tocados;
**23/23 tests OK**. Incluye `optd_scene_diag` (0,05 s), que cubre:

- los analizadores de 2335 (`#1609`, `#2391`, `#9734`), 2661 (`#1607`), 2005 (`#1571` Damiane, `#38259` B0100083,
  `#785` con tres registros), 2223 (`#27836` con bandera, `#141350` sin ella) y C>S 2767/2591/2436/2138/3020/2173, con
  registros reales de la sesión 6;
- 2874/2126/2411 construidos según el orden de lectura (no hay ninguno en las capturas);
- la búsqueda del bloque de IA (Damiane en `+0x2241`, B0100083 dos veces en `+0x13A9`/`+0x179A`);
- la réplica de búsqueda de `NPCScheduleManager` sobre memoria simulada (encontrado, nodo `>> 2`, fichero ausente o
  descargándose, índice fuera de rango, cubos y nodos basura acotados);
- la tabla de recuentos sin candado (límites y cuatro hilos);
- el libro de escenas, el seguimiento de viajes con los tiempos de GBC, la cronología por NPC, el anillo y los límites
  por ventana y por NPC.

No prueban los ganchos, el ABI ni el comportamiento del juego: eso es la sesión con Shadow (§5.2, tramo 1).
