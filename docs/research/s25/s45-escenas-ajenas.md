# S45 — Escenas de otro jugador sobre el cliente local: causa, fronteras propuestas y sondas

Documento del 16-10, **validado en vivo con dos copias en un PC** (pasos 1–3, §11). Build del juego 1.0.0.2976 (PE `0x6AB28F00`, base `0x140000000`); todas las direcciones
son RVA de esa build. Rama `opcion-d-escenas`. Encargo: `s45-ENCARGO-escenas-ajenas-cliente-local.md`.

Estado:
- **Hecho:** investigación de solo lectura (informes copiados en `s45-datos/`), las **sondas de solo lectura**
  (`guest_seq_foreign_present_probe=1`) con las correcciones B1–B4 de su revisión, y **R-A implementado** detrás de la
  clave nueva `guest_seq_foreign_present=0|probe|1` (por defecto 0), con tests y build limpio (§3.1, §10).
- **Paso 1 en vivo (probe) limpio** (16-10, `s45-datos/vivo1.md`): R-A en probe cumple todos los criterios y el vínculo
  evento → etapa de la vía B quedó demostrado.
- **R-B implementado** detrás de `guest_seq_foreign_timeline=0|probe|1` (§3.2), y S-D arreglada.
- **Pasos 2 y 3 en vivo limpios** (16-10, §11): con R-A = 1 y R-B = 1 la tienda del anfitrión ya no sale vacía en el
  invitado, la cinemática de la cárcel tampoco, y el invitado conserva sus controles y su cámara.
- **Revisión de R-B** (`revision-rb.md`) corregida: P2-1 (M también solo con R-B), P3-1 (la etapa sigue con ese id; la
  coincidencia más reciente), P3-2 (clase del evento), P3-3 (los fines se leen y toleran el inicio saltado).
- **Falta:** la prueba entre dos PC con Shadow (red real, túnel por Steam, cargas a distinta velocidad).

Marcas: **[V]** leído en instrucciones, decompilación, log o captura; **[E]** inferencia fuerte; **[H]** hipótesis por
medir.

---

## 1. Resumen de la causa

Fuentes: `s45-datos/nativo.md` (semántica nativa, vías, puntos de raíz) y `s45-datos/carcel.md` (la cinemática de la
cárcel). Lo esencial:

1. **Los 11 + 11 «control to this client's character» de P2 no son la tienda [V].** Son escenas *propias* del
   invitado, de su entrada en el mundo: P2 cuenta todas las llamadas, pero solo escribe líneas de las ajenas, y esas
   11 salen siempre en la ventana de carga, antes de la primera escena ajena. El anfitrión hace la misma serie nativa.
2. **La hipótesis del encargo es falsa [V].** El «jugador» de cámara, interfaz, tienda, entrada y carga **no** se
   resuelve por el nodo `CurrentControl` → `GetLocalPlayer`. Esos efectos no resuelven ningún actor: escriben en los
   servicios globales del cliente colgados de `[0x6D691E0]`:
   - `+0x10` estado de carga P;
   - `+0x20` interfaz;
   - `+0x118` cámara y `+0x120` reproductor de líneas de tiempo;
   - `+0x30` gestor de actores y usuario locales.

   Para el motor, toda etapa que existe en el cliente es del jugador local (el servidor solo se la manda a su dueño).
   **No hay ninguna comprobación nativa general** de «esta escena es del jugador local»; `stage+0x280` no lo es (es
   relativa al dueño y ponerla dispararía la carga diferida en el invitado).
3. **S44 cumplió lo suyo [V]:** la copia del anfitrión ya no se mueve. Lo que queda llega por otras vías:
   - **tienda del anfitrión** (`cd_seq_funcnpc_smithy` `0xA7132561`): la cámara de la línea de tiempo de X (vía B)
     más paneles no vetados (`ChangePhasePanelTag`); la tienda en sí (`UIControl`) está vetada, por eso sale «vacía»
     [E fuerte];
   - **cárcel** (`cd_seq_quest_crime_police_wakeup_prison_hernand_0100` `0xE5B490F1`, seq `0x30A`): cinemática pura
     (`set_cutScene(True)`, `set_exclusive(True)`, `CameraSelectorEventData`). **Ninguno** de los 28 manejadores de
     `guest_x_*` vio cámara [V]; el movimiento del invitado se cortó ~13 s [E, no separado de «el usuario soltó las
     teclas»].

## 2. Las tres vías [V salvo lo marcado]

| Vía | Qué es | Ejemplos | Qué la cubre hoy |
|---|---|---|---|
| **A** | Funciones de etapa con ranura en el procesador (`ClientSequencerStage_StageChartProcessor`, vtable `0x558C080`). Las de presentación son las ranuras que en el común (`0x590DF78`) y el servidor (`0x5B1FE10`) son `ret` | `InputBlock`, `UIControl`, `SequencerCamera`, `CameraPreset`, `LetterBox`, `FadeInOut`, `Loading`… | 28 ganchos de `guest_x_ui_veto` / `guest_x_camera_veto` (unos vetan, otros solo cuentan) y V1 |
| **B** | Eventos y pistas de la línea de tiempo (`.paseq`), reproducidos por el reproductor global `[0x6D691E0]+0x120` con el id `stage+0x298` | `CameraSelectorEventData`, `SequencerPlayerCameraPreset`, bandas, posproceso, `StartLoadingSequencer`, `AIDialog` | **nada** |
| **C** | Funciones de etapa «de diálogo» con código propio | `SetQuestDialogAICategory`, `TimelineEvent`, `BlockInteraction`, `SetInteraction` | los vetos actuales (`SetInteraction` permitido a propósito) |

## 3. Fronteras (R-A y R-B implementadas)

**La regla.** En el cliente del invitado, de una etapa **ajena** (de X: dueño = personaje de otro jugador) solo corre lo
que también ejecutan el motor común y el servidor. Lo que solo existe para presentar al jugador local, no.

### 3.1 R-A — procesador «solo mundo» (vía A), 1 punto: IMPLEMENTADO

Clave **`guest_seq_foreign_present = 0 | probe | 1`** (`[session2]`, solo invitado; por defecto `0` = como hoy, dos
cargas atómicas en P1). Código en `guest_seq_present.{h,cpp}` (`verify_ra`, `ra_p1`), lógica pura y decisiones en
`guest_seq_present_logic.h` (`kRaExpect`, `ra_plan`, `ra_build`, `ra_judge`), tests en `tests/optd_seq_present_tests.cpp`.
Las pruebas de código (scripts y salida) están en `s45-datos/ra/` (`ra-verificacion-2976.txt`).

#### 3.1.1 Qué hace

- **Dónde:** la entrada de `0x602940` (P1 de S44, ya verificada). `guest_seq_foreign` instala P1 también para esta clave
  (`guest_seq_present_needs_p1`) y le pasa la etapa y su recurso (el del 2661).
- **Cuándo actúa:** la etapa es **ajena** (dueño `[stage+0x98]` = personaje `0xA…` que no es el propio ni el de M: el
  mismo juicio de S44/S45), **no es de M** (revisión P1-1: M tiene que conocerse y `[stage+0x28] != M`), su gestor es el
  X que `guest_seq_play` conoce para ese dueño (`guest_seq_play_knows_x`), y su procesador `[stage+0x58]` tiene la
  vtable de cliente `0x558C080`.
  - **probe:** una línea `GuestSeqPresent [R-A WOULD CUT presentation]` con la etapa (secuenciador y nombre), el recurso,
    el dueño, el procesador y las ranuras que cortaría. **No escribe nada.**
  - **1:** cambia el puntero de vtable de **ese** procesador (solo esa instancia, 8 bytes) por la tabla del mod, con un
    único `lock cmpxchg` (`_InterlockedCompareExchange64`) de `exe+0x558C080` a la tabla. Línea
    `[R-A CUT presentation]`.
  - Si una comprobación falla, línea `[R-A NOT cut]` con el motivo y **no se escribe**.
  - **Restauración (revisión P2-2):** si una etapa que nunca debe llevar la tabla (no ajena, o de M, o con M desconocido)
    la lleva (etapa reciclada o juicio anterior erróneo), se le devuelve `0x558C080` con el mismo `cmpxchg` y sale un WRN
    `[R-A RESTORED]` (o `[R-A RESTORE FAILED]`). Es criterio de aborto.
- **La tabla** se construye **una vez** al instalar, a partir de las tres vtables nativas leídas en memoria:
  - `[-1]` = el mismo puntero al COL que la nativa (`exe+0x5DB7A20`): `typeid`/`dynamic_cast` siguen funcionando;
  - 105 ranuras copiadas de `0x558C080`, salvo las 25 que encienden presentación (§3.1.2), que toman el valor de la
    **común** `0x590DF78`;
  - vive en una **página propia** (`VirtualAlloc`), que pasa a solo lectura en cuanto se rellena y, una vez armada,
    **nunca se libera** (sobrevive a cualquier procesador que apunte a ella, incluso si el módulo se descargara, cosa que
    en el juego no ocurre). Si la verificación falla después de reservarla, se libera (revisión P3-4): aún no apunta
    nadie a ella.

#### 3.1.2 Decisión por ranura (build 2976) [V salvo lo marcado]

Rango: solo `+0x108..+0x2A0`, las ranuras que alcanzan los trampolines `StageChart_Function_*` (`tramp.py`: 52
trampolines, todos con la misma cabecera de 16 bytes `mov rax,[rdx] ; mov r8,rcx ; mov rcx,[rax+0x58] ; mov rax,[rcx] ;
jmp [rax+off]`, uno por ranura). Las ranuras fuera del rango (tick, condiciones, enlaces del propio procesador) quedan
**todas** nativas.

**Convención de retorno.** El trampolín es un salto de cola: lo que devuelve la ranura es lo que devuelve `Execute`
(vf8/vf9 de la función de etapa). `Execute` es `void`: en la clase base `pa::StageChart_Function` (vtable `0x5984EE0`)
vf8 y vf9 son `0x359B50` = `ret 0` (`C2 00 00`), el cuerpo vacío **void** plegado por el compilador. El cuerpo vacío que
devuelve `bool` es otro, `0x359BA0` = `xor al,al ; ret`, y aparece en otras ranuras de las mismas vtables. Por tanto,
en las ranuras de `ret 0` del común no hay valor de retorno que imitar: poner el mismo `0x359B50` es equivalente [V
código, E en la deducción del tipo].

**Criterio (revisión P2-1):** se corta solo lo que **enciende** presentación en este cliente. Lo que la **apaga**, y lo
que lleva la dirección en sus datos, se queda nativo. Así un apagado nunca se pierde, venga de donde venga su encendido
(una etapa de mundo de X, el hueco antes de P1, una etapa rechazada).

| Ranuras | Qué son | Decisión | Evidencia |
|---|---|---|---|
| 21 solo de cliente: 16 sin pareja de apagado, `+0x128 +0x138 +0x140 +0x150 +0x158 +0x160 +0x168 +0x170 +0x198 +0x1B0 +0x1F0 +0x1F8 +0x238 +0x240 +0x260 +0x268` y los encendidos de los pares `+0x1C0 +0x1E0 +0x208 +0x220 +0x248` (ver la tabla de pares) | minijuego, `Craft`, `InputBlock`, `UIControl` (tienda), paneles, minimapa, ruta, malla, vista previa, `ForceLockOn`, icono, guías, posproceso, material; cámara de nuevo encendida, preajuste de cámara, bandas, `LookAt`, pista | **`ret` del común** `0x359B50` | común = servidor = `ret`; cada manejador toca un servicio del cliente `[0x6D691E0]+…` (`+0x20` interfaz, `+0x28`, `+0x30`, `+0x118` cámara, `+0x120` reproductor), `GetLocalPlayer` o un *thunk* opaco a `.xtls` (`use.py`). `InputBlock`, `UIControl`, `UIShowMinimap` y `UIStageGuide` llevan su nivel o su estado en los datos, pero `guest_x_ui_veto=1` ya veta **todas** sus llamadas de X (en los dos sentidos), así que cortarlas no parte ningún par que hoy pase |
| `+0x1A0` / `+0x1A8` `DisableCharacterCollision` (y su fin) | colisión de los actores que la función enlaza | **nativas** (mundo) | su único efecto: `0x2049B90` (enlaces) → `0x5037B0`, bit 4 de `[[[actor+0x68]+0x40]+0xE0]+0x4C` (colisión del actor); ningún servicio del cliente, ni `GetLocalPlayer`. Con S44 el enlace al dueño ya está neutralizado y en 1 635 escenas ninguna enlazó al invitado: solo toca NPC [E] |
| 9 de apagado o con la dirección en los datos: `+0x130 +0x1B8 +0x1C8 +0x1D0 +0x1D8 +0x1E8 +0x210 +0x228 +0x250` | ver la tabla de pares | **nativas** | ver la tabla de pares |
| `+0x200` `HireMercenary` (cliente y servidor) | contratar mercenario | **`ret`** | el servidor (`0x293AEB0`) hace la contratación real para el dueño; la parte de cliente (`0x5F4BC0`) resuelve enlaces y usa el gestor de actores local `[cw]+0x30` (`0x8AE1A0`): en una etapa ajena actuaría sobre el cliente del invitado [E] |
| `+0x218` `Loading` (cliente y servidor) | carga del campo | **`ret`** | es exactamente lo que fuerza hoy V1 (`guest_x_stage_veto=1`, en vivo desde S9 sin problemas): V1 salta tras la compuerta `0x204F730` a la salida. La compuerta solo lee (bandera de datos, estado del dueño y una comprobación con *holder* que se libera) y no escribe en la etapa ni en el procesador [V], así que no llamarla es equivalente. Con `ret`, `proc+0x29B` nunca se pone y `0x600220` nunca lanza la carga diferida |
| `+0x258` `Reward` (cliente y servidor) | recompensa | **`ret`** | el servidor (`0x293BC60`) da la recompensa al dueño; el cliente (`0x5F4280`) es un *thunk* a `.xtls` (presentación de la recompensa [E]). El motor nativo nunca la ejecuta en el cliente de otro jugador |
| `+0x288` `VarySharpness` (el común tiene función propia) | — | **la del común** `0x204DDD0` | el manejador de cliente `0x5F4EC0` llama primero a `0x204DDD0` con los mismos argumentos (`0x5F4EF0`) y después hace su trabajo de cliente (enlaces y el servicio `[cw]+0x28`). `0x204DDD0` es una comprobación del usuario del dueño (`0x206AE30`) que devuelve `bool` y libera su *holder*: tomarla es «lo que ejecuta el común», sin el trabajo de cliente |
| `+0x230` `SetPhase`, `+0x298/+0x2A0` `SetHologramActor` | comunes | nativas | iguales en las tres |
| solo de servidor (`CreateItem`, `PushReward`, `CompleteStage`, `SetWanted`, `Sell`…) | — | nativas | ya son `ret` en el cliente |

Total: **24 a `ret`** + **1 a la del común** = **25 ranuras cambiadas**; nativas: 2 de mundo y 9 de apagado o con la
dirección en los datos.

**Tabla de pares [V en el desensamblado salvo lo marcado]** (vf8 = inicio de la función de etapa, vf9 = su fin):

| Par | Encendido (se corta) | Apagado o dudoso (nativo) | Evidencia |
|---|---|---|---|
| `DeactiveSequencerCamera` | vf9 `+0x1C0` `0x5EE230`: `0x5EA5B0(proc, dl = 1, r8 = 0)`, la cámara de secuencia **otra vez encendida** | vf8 `+0x1B8` `0x5EE210`: `0x5EA5B0(proc, dl = 0, r8 = 0)`, **apagada** | `0x5EA5B0` es el «poner cámara de secuencia» común (`movzx r12d,dl`, `test r12b,r12b`); también lo llama `.xtls 0x8F285CE` (el de `SequencerCamera`) |
| `SequencerCamera` (solo vf8) | — | `+0x1C8` `0x5EE250` → `.xtls 0x8F285C0`: `dl = [data+0x28]`, `r8 = [data+0x29]` → `0x5EA5B0`: **la dirección va en los datos** (enciende o apaga) | no se puede partir por ranura → nativa. Se apaga también con `+0x1B8` (nativa) |
| `DeactivateSequencerCameraFadeIn` | — | vf8 `+0x1D0` `0x5EE0C0`: pone `proc+0x27F = 1`, `[[cw]]+0xDE8 = 1` (la cámara del jugador; `SequencerCamera` la pone a 0) y `+0xF9 = 1` en los objetos de su línea de tiempo: **apaga**. vf9 `+0x1D8` `0x5EE160` → `.xtls 0x8F257B0`: opaco | apagado → nativo; su fin, opaco → nativo por prudencia [E] |
| `CameraPreset` | vf8 `+0x1E0` `0x5EDD30`: aplica el preajuste (enlaces y `[cw]+0x118`) | vf9 `+0x1E8` `0x5EE1F0`: `0x5EB200(proc, dl = [data+0x20], r8 = [[data+0x28]])`, que es también la ranura propia del procesador `+0x310` y la llama `0x5FA530`: **restaura** [E] | — |
| `LookAt` | vf8 `+0x220` `0x5F3600` | vf9 `+0x228` `0x5F3810` | inicio / fin de la función de etapa [E por la convención vf8/vf9] |
| `UIHint` | vf8 `+0x248` → `.xtls 0x8F44B10` | vf9 `+0x250` → `.xtls 0x8F4A140` | ídem [E] |
| `ActivateMiniGame` / `DeactivateMiniGame` | `+0x128` (y `ExecuteMiniGameEvent` `+0x138`) | `+0x130` | por nombre: la desactivación es el apagado [E] |
| `FadeInOut` (una sola ranura) | — | `+0x210` → `.xtls 0x8F4E0D0`: lee `[data+0x20]` y `[data+0x24]` (alfa de origen y destino): **la dirección va en los datos** | nativa |
| `LetterBox` (una sola ranura) | `+0x208` `0x5F2970`: `0x5F9CD0(proc, dl = 1, xmm2 = [data+0x20])`, **siempre enciende** | el apagado no es una función de etapa: `0x5F9CD0` con `dl = 0` desde la ranura propia del procesador `+0x80` (`0x5EE93B`) o desde el evento de la línea de tiempo (`.xtls 0xA549E6F`), los dos nativos | se corta: su apagado nunca pasa por una ranura cortada |

**La vía C** (`SetInteraction`, permitida a propósito, `BlockInteraction`, `SetQuestDialogAICategory`, `TimelineEvent`)
**no son ranuras del procesador** (sus `Execute` saltan a código propio, `scf.txt`): R-A no las toca y siguen con
`guest_x_ui_veto` / `_allow`.

**Robustez frente a otra build:** las 36 decisiones están fijadas con el RVA del manejador de cliente, el tipo de ranura
y su trampolín. Al instalar se comprueban: base y `TimeDateStamp`, los tres COL, que la clasificación de las tres vtables
coincide **exactamente** con las 36 decisiones (ninguna ranura distinta sin decisión, ninguna decisión sobre una ranura
igual), los 36 trampolines byte a byte, la ranura 0 y los cuatro *thunks* de vida útil (§3.1.3). Si algo no coincide, R-A
no se arma (WRN) y la clave actúa como 0.

#### 3.1.3 El procesador es de la etapa (por instancia) y no hace falta restaurarlo [V estático]

- **Creación:** solo dos sitios del ejecutable cargan la dirección `0x558C080` (`vtref.py`, búsqueda de `lea` RIP-relativo
  en `.rsrc`, `.xtls`, `.text` y `.xtext`, más punteros absolutos de 8 bytes en todo el archivo): el constructor del
  procesador `0x8F08E30` y su destructor `0x8F0E3D0`. **Ningún otro código compara un vptr con `0x558C080`.**
  - Al constructor solo se llega por el *thunk* `0x5D94F0`, y a este solo lo llama `0x8F5EE0A`, dentro del constructor de
    `ClientSequencerStage` `0x8F5EBD0` (que escribe las vtables de etapa `0x558BD80` / `0x558BFB0`).
  - Ese constructor reserva **0x2F8 bytes nuevos** con el asignador del juego, construye el procesador con la etapa
    (`r8 = rdi` = la etapa) y lo guarda en `[stage+0x58]` (`0x8F5EE30`; si había otro, lo borra antes).
  - Es decir: **un procesador nuevo por cada etapa**. En tiempo de ejecución se comprueba además `[proc+0x10] ==` la etapa
    y `[proc+8] == [stage+0x28]` antes de escribir.
- **Ninguna otra referencia (revisión P2-3 y P3-3, `rva32.py` y el `tdref.py` del revisor):** en todo el archivo no hay
  RVA de 32 bits de `0x558C080`. Las del COL `0x5DB7A20` y del TypeDescriptor `0x6AACB00` están solo en los datos RTTI
  de `.xdata` (`0x5DB7A34`; `0x5DB6E40` y `0x5DB7A2C`). Tampoco hay ningún `lea` del TypeDescriptor en código: no hay
  `dynamic_cast` ni `typeid` sobre esta clase.
  - **Salvedad:** `.xtls` construye constantes ofuscadas (por ejemplo `mov edx,imm ; sub edx,[rip+…]` en `0x8F5ED9E`), y
    una comparación con la vtable calculada así no se ve en una búsqueda estática. La probabilidad es baja, pero solo se
    descarta en vivo con R-A = 1.
- **Destrucción:** el destructor base de la etapa (`0x20516B0` → `0xE0617E0`) hace `call [[stage+0x58]]` con 1 (el
  destructor con borrado, ranura 0) y pone `[stage+0x58] = 0`.
  - La ranura 0 de la tabla del mod es la nativa (`0x5D9750` → `0x8F094A0` → `0x5D97B0` → `0x8F0E3D0`), y ese destructor
    **reescribe él mismo `0x558C080`** en el vptr antes de liberar.
  - Por eso **no hace falta restaurar**: cada procesador muere con su etapa, por el camino nativo, y la tabla (página que
    nunca se libera) existe mientras haya alguno que apunte a ella.

#### 3.1.4 Hilo y concurrencia

- **P1 corre en el hilo principal [V estático + comprobación en vivo]:** `0x602940` tiene un único llamador, `0xA9D860`
  (`0xA9D91A`), al que solo llaman `0xA97300` (la actualización del gestor, desde `0xA953C0`, desde `0x8B6150`, el
  fotograma del bucle principal) y `0x9F8FCE0` (estado `0x14` de `.xtls`, solo M) (`p8-s8-escenas.md` §3.3,
  `p8-xtls-lectura.md`). Las etapas de X empiezan cuando `guest_seq_play` llama a `0xA953C0(X, …)` desde su gancho
  `0x8B6144`, en ese mismo hilo.
  - R-A **lo comprueba en cada etapa**: escribe solo si `GetCurrentThreadId()` es el hilo del gancho de fotograma de
    `guest_seq_play` (`guest_seq_play_main_tid()`). Si no lo es, o aún no se conoce, no escribe (`P1 not on
    guest_seq_play's frame thread`).
- **Nadie más usa ese procesador en ese momento:** P1 es el inicio de la etapa, y sus funciones de etapa se ejecutan desde
  el tick del gestor (`0xAA3E70`), en el mismo hilo y después.
  - Aunque otro hilo leyera el vptr a la vez, el cambio es **una sola escritura alineada de 8 bytes con `lock cmpxchg`**:
    se ve el puntero viejo o el nuevo, y las dos tablas son completas y válidas. Las ranuras que no son de presentación
    son idénticas en ambas.
  - El `cmpxchg` es la «lectura y comparación previa»: si el vptr ya no es `0x558C080`, no se escribe nada.
- **Nunca en M (revisión P1-1):** «propia» depende de la clave de M, que solo se actualiza en el gancho de fotograma y
  puede valer 0 o ser vieja. Por eso R-A exige además M conocido (`M not seen yet`), que la etapa no sea de M (`the stage
  is M's (G = M)`) y que su gestor sea el X que `guest_seq_play` conoce para ese dueño en esta conexión (`G is not
  guest_seq_play's X of the owner`). Con el veto por gestor, una etapa del invitado no se corta aunque el juicio de
  dueño fallara.

#### 3.1.5 Cobertura: el hueco antes de P1

- Una función de etapa que corra **antes** de P1 encuentra aún la vtable nativa.
- S-A2 ya cuenta, por manejador de `guest_x_veto`, las llamadas de etapas de X **antes** de que P1 las viera, y cuántas
  son de etapa ajena.
- El resumen de R-A (`GuestSeqPresent R-A summary`) suma las de los manejadores de interfaz y cámara cuyas ranuras R-A
  corta: «before their P1 N (R-A's gap)». Con R-A = 1, la cifra «after it» de etapas ajenas debería quedarse en 0 (salvo
  las rechazadas): es la prueba en vivo de que R-A corta.
- Para la revisión P2-1, el mismo resumen da además, en probe:
  - por manejador de ranura cortada, sus llamadas desde etapas de X **no ajenas** («the same handlers from X's NOT foreign
    stages»): activaciones que R-A nunca corta;
  - por manejador de ranura que se queda nativa por apagar (o por llevar la dirección en los datos), sus llamadas antes
    y después de P1 y desde etapas no ajenas.

  Como los apagados ya son nativos, un encendido que venga de esas fuentes siempre encuentra su apagado.
- Necesita la sonda (`guest_seq_foreign_present_probe=1`), que está en los dos inis.
- Otros huecos:
  - etapas ajenas antes de conocer la clave propia (nada es ajeno hasta entonces, igual que S44);
  - etapas que empiecen antes de que R-A se arme (el hilo de instalación espera a `WorldSystem`; las de X no empiezan sin el
    tick de `guest_seq_play`).

#### 3.1.6 Redundancias (no se quitan)

Con R-A = 1, en las etapas **ajenas** que pasaron por P1 y se cortaron, estos vetos ya no se alcanzan (el trampolín salta
al `ret` antes de llegar al manejador enganchado):
- `guest_x_ui_veto`: `InputBlock`, `UIControl`, `ChangePhasePanelTag`, `UIShowMinimap`, `UIFindPath`, `UIStageIcon`,
  `UIStageGuide`, `UIStageBackgroundGuide` y `UIHint` (su fin `+0x250` se queda nativo, así que su gancho sigue);
- `guest_x_camera_veto`, solo los de ranuras cortadas: el fin de `DeactiveSequencerCamera` (`+0x1C0`), `CameraPreset`,
  `ForceLockOn`, `LetterBox`, `LookAt` y `PostProcess`. Los de `DeactiveSequencerCamera`, `SequencerCamera`,
  `DeactivateSequencerCameraFadeIn` ×2, el fin de `CameraPreset`, `FadeInOut` y el fin de `LookAt` siguen viendo las
  llamadas (ranuras nativas por P2-1);
- `guest_x_stage_veto` V1 (`0x5F2A3A`, dentro de `Loading`).

**Siguen siendo necesarios** para:
- las etapas de X que no son ajenas (dueño NPC o sin dueño: «mundo»);
- las llamadas antes de P1;
- las etapas que R-A rechace;
- el caso R-A = 0 / probe.

La vía C sigue entera en `guest_x_ui_veto`. Por eso no se retira nada; con las medidas en vivo se decidirá.

#### 3.1.7 Riesgos, explicados para el usuario

- **Qué se escribe:** cada objeto de C++ del juego empieza con un puntero a su «tabla de funciones» (vtable). R-A
  cambia ese puntero, **solo** en el procesador de cada escena del **otro** jugador, por una copia de la tabla en la que
  las funciones de «mostrar algo a este jugador» (cámara, interfaz, entrada, carga) son la función vacía que ya usa el
  propio juego en su motor común y en el servidor. No se toca código del juego, ni la tabla original, ni ningún otro
  objeto.
- **Qué podría ir mal:**
  1. **Algo del juego (en `.xtls`, cifrado) comprueba el tipo del procesador** comparando el puntero con `0x558C080`. No
     hay ninguna otra referencia a esa dirección en todo el ejecutable (§3.1.3) y RTTI sigue funcionando, así que es
     improbable [E]. Síntoma: una escena ajena que se queda colgada o un cierre.
  2. **Una escena del anfitrión espera algo que hacía una función cortada**, por ejemplo que se cierre una interfaz que
     ya no se abre. Las esperas van por las condiciones del procesador, que no se tocan, y `UIControl`/`InputBlock` ya se
     vetan hoy sin ese problema [V en vivo]. Pero para `Reward`, `HireMercenary` y los minijuegos no hay experiencia [H].
     Síntoma: la copia del anfitrión o los NPC de la escena se quedan quietos en el invitado.
  3. **Un caso que el juicio de «ajena» clasifique mal** (la escena propia del invitado tomada por ajena). El juicio es
     el de S44, validado; R-A además escribe la línea con el dueño. Síntoma: el invitado no ve su propia tienda o cámara.
  4. **Pares de encendido y apagado (revisión P2-1).** Si R-A cortara un apagado cuyo encendido vino de algo que no corta
     (una etapa de mundo de X, el hueco antes de P1 o una etapa rechazada), el estado del cliente se quedaría pegado:
     pantalla negra, bandas o cámara de secuencia. Por eso solo se cortan los encendidos; los apagados, y las ranuras que
     llevan la dirección en sus datos (`SequencerCamera`, `FadeInOut`), son nativos.
     - Queda un caso [H]: un encendido cortado cuyo apagado nativo encuentre el estado ya apagado. Es lo normal en el
       juego (apagar lo apagado), no lo contrario.
     - Coste: de una escena ajena siguen pasando su `SequencerCamera` y sus fundidos (como hoy, que `guest_x_camera_veto`
       está en probe).
  5. **El hueco antes de P1** (§3.1.5): deja pasar lo que pasa hoy, en los dos sentidos.
- **Qué no puede pasar por diseño:**
  - escribir en un procesador que no tenga exactamente `0x558C080`;
  - escribir en uno que no sea de esa etapa;
  - escribir desde otro hilo;
  - escribir en una etapa de M, o con M desconocido, o en un gestor que no sea el X conocido del dueño;
  - cortar un apagado;
  - dejar un puntero a memoria liberada (la tabla no se libera nunca);
  - escribir con una tabla no verificada contra este ejecutable.
- **Crash reporting:** un cierre del juego se envía a Pearl Abyss (crashpad); por eso el orden es probe primero y 1
  después, con copia de las partidas.

#### 3.1.8 Criterios de aborto (→ `guest_seq_foreign_present=probe`, o 0)

- cierre o crashpad;
- una escena del anfitrión que se queda colgada en el invitado (la copia del anfitrión o sus NPC quietos);
- el invitado sin control o sin interfaz después de una escena ajena;
- **pantalla negra, bandas o cámara de secuencia pegadas** tras una escena ajena (revisión P2-1);
- cualquier `[R-A WRITE FAILED]`, `[R-A RESTORED]` o `[R-A RESTORE FAILED]` (revisión P2-2);
- un `[R-A CUT]` cuyo `owner` sea el del propio invitado o el de M, o con `G = M`;
- en la línea de instalación, `NOT armed` (con la clave a 1 no actúa: hay que mirar el motivo antes de seguir).

#### 3.1.9 Plan de prueba de R-A

1. **Paso 1 = el del §6 con `cdmp.2inst-s45-probe-guest.ini`** (ya lleva `guest_seq_foreign_present=probe`). Mirar:
   - `GuestSeqPresent R-A guest_seq_foreign_present=probe ARMED` (24 a `ret`, 1 a la del común, 2 de mundo, 9 apagados
     o con dirección en los datos, 80 copiadas);
   - una línea `[R-A WOULD CUT presentation]` por cada escena ajena (tienda, cárcel, Matthias…), con el recurso correcto;
   - ningún `[R-A NOT cut]` con motivo distinto de `not foreign`, en particular ninguno «thread», «[proc+0x10]», «other
     vtable», «G = M» o «not guest_seq_play's X» (un «M not seen yet» solo al principio);
   - en el resumen de R-A, el hueco «before their P1» y las llamadas desde etapas no ajenas de X (P2-1).
2. **Paso 2 = el mismo recorrido con `cdmp.2inst-s45-activo-guest.ini`** (R-A = 1). Esperado:
   - la tienda del anfitrión ya no abre paneles ni bloquea la entrada del invitado (la cámara de la línea de tiempo puede
     seguir: es la vía B, R-B);
   - la cárcel igual que hoy salvo el icono (su cámara es vía B);
   - el resumen de `GuestXVeto` deja de contar llamadas de X en etapas ajenas, y «after it» de R-A se queda en 0;
   - la copia del anfitrión y los NPC de la escena siguen animados;
   - las escenas propias del invitado no aparecen en ninguna línea R-A.
3. Si el paso 2 es limpio, medir R-B (§3.2) con las líneas S-B/S-D de esas mismas sesiones.

### 3.2 R-B — los inicios de cámara de la línea de tiempo (vía B): IMPLEMENTADO

Clave **`guest_seq_foreign_timeline = 0 | probe | 1`** (`[session2]`, solo invitado; por defecto `0` = como hoy). Código
en `guest_seq_present.cpp` (`rb_event`, `verify_rb`), decisión pura en `guest_seq_present_logic.h` (`rb_judge`), tests en
`tests/optd_seq_present_tests.cpp` (`test_rb_judge`).

#### 3.2.1 Lo que midió el paso 1 en vivo (`s45-datos/vivo1.md`, datos en `C:\Emulador CD\backups\p8-2inst-s45-probe\`)

- **R-A en probe, limpio:**
  - `ARMED` con 25 cambiadas, 2 de mundo, 9 nativas y 80 copiadas;
  - 58 `[R-A WOULD CUT]`, todos con dueño el anfitrión, G = X conocido, la vtable de cliente y el hilo de fotograma;
  - G = M: 0; `NOT cut`, `RESTORED` y `WRITE FAILED`: 0;
  - S-A2: 0 llamadas antes de P1 y 0 desde etapas no ajenas de X.
- **El vínculo evento → etapa está demostrado [V]:** `[ctx+8]` **es** el objeto de línea de tiempo (vtable
  `exe+0x5C75F30`, profundidad 0) y su `+0x1D8` coincide con el `stage+0x298` que anotó P1 (tienda 245 = `0x2E4`, cárcel
  318 = `0x33A`).
- **Llamadores [V]:** todos los inicios vienen de `exe+0x36574CD` o `exe+0x36575D6`, el retorno de los dos
  `call [rax+0x318]` (ranura 99) del despachador (fragmento con `.pdata` `0x36573B5–0x3657600`). No son los sitios
  `0x377FC03…` que proponía `nativo.md`.
- **La cámara de la tienda y la de la cárcel van por la vía B:**
  - tienda: tres `SequencerPlayerCameraPreset`, uno por rama;
  - cárcel: un `CameraSelectorEventData` 22 ms después de P1, con la línea de tiempo arrancando, y un
    `SequencerPlayerCameraPreset` a los 8 s;
  - ningún manejador de cámara de etapa (vía A).
- **Las demás clases con sonda** (bandas, posproceso, diálogo de IA, `StartLoadingSequencer` de etapa ajena) tuvieron
  **0** llamadas en escenas ajenas.
- **S-D no se instaló:** su verificación comparaba los bytes originales de `0x5E4E6E`, donde `guest_x_ui_veto=1` ya tiene
  su gancho. Arreglado: `0x5E4E6E` sale de la lista; el desplazamiento que lee S-D (`G+0x230`) lo sigue verificando su
  escritor `0xA98D3E`.

#### 3.2.2 Qué hace

- **Dónde:** los dos ganchos S-B que ya existían: `0x34CFC20` (`CameraSelectorEventData`, *thunk* hoja de 5 B
  `jmp .xtls`, sin `.pdata`) y `0xB27880` (`SequencerPlayerCameraPreset`, entrada con `.pdata` `0xB27880–0xB27DDE`). Con
  esta clave se instalan aunque la sonda esté a 0 (solo esos dos).
- **Solo la ranura 99 (el inicio):**
  - cada entrada es la ranura 99 de su vtable y **ninguna otra** ranura de esa vtable;
  - no hay ningún `call`/`jmp` directo a ellas;
  - además, el juicio exige que la dirección de retorno sea uno de los dos sitios de la ranura 99 del despachador
    (`0x36574CD`, `0x36575D6`).
- **Juicio** (el de R-A más el enlace medido):
  1. llamador = `0x36574CD` o `0x36575D6`, y el evento es de la clase enganchada (`[rcx]` = su vtable; revisión P3-2);
  2. `[rdx+8]` legible y con vptr `exe+0x5C75F30`;
  3. su id `+0x1D8` es una etapa que anotó P1 (si hay varias, la más reciente; revisión P3-1);
  4. esa etapa sigue viva: relee su secuenciador, su dueño y su `+0x298 ==` el id (revisión P3-1);
  5. ajena;
  6. M conocido y G ≠ M;
  7. G = el X que `guest_seq_play` conoce para ese dueño;
  8. hilo de fotograma.
- **probe:** línea `GuestSeqPresent [R-B WOULD SKIP]`. **1:** la entrada vuelve al llamador sin ejecutar nada, línea
  `[R-B SKIPPED]`. Si una comprobación falla en una etapa ajena: `[R-B NOT skipped]` con el motivo. Resumen
  `GuestSeqPresent R-B summary` (por clase y veredicto, con `SKIPPED` y `WOULD SKIP`) cada 60 s.
- **Se arma** solo si los dos ganchos están puestos y se verifican: los bytes de los dos sitios de llamada y del retorno
  del despachador (`mov al,1 ; … ; ret` en `0x3657600`), el código del id `+0x1D8` (`0x34DC175`, `0x34DBB03`), las dos
  primeras ranuras de la vtable `0x5C75F30` (`0x34C0B60`, `0x34BD680`; esta clase no tiene RTTI) y que las ranuras 97 y
  98 de las dos clases son `0x359B50`.

#### 3.2.3 Demostración del retorno temprano [V]

1. **El mecanismo (safetyhook 0.x, `mid_hook.x86_64-windows.asm`):**
   - El gancho entra con un `jmp` (sin `call`): en el punto, `[rsp]` es la dirección de retorno del llamador.
   - El *stub* apila `rip` (el trampolín), `trampoline_rsp` (que apunta a ese `rip`) y el `rsp` original, que corrige a
     `[rsp+384] + 16` = el `rsp` del punto (`ctx.rsp`).
   - A la salida restaura los registros y hace `lea rsp,[rsp+8] ; pop rsp ; ret`. Es decir, `rsp = ctx.trampoline_rsp` y
     `rip = [ctx.trampoline_rsp]`. `context.hpp` lo documenta así: «use trampoline_rsp … make sure the top of the stack
     is the rip you want to resume at».
   - Con `ctx.trampoline_rsp = ctx.rsp`, la salida queda en `rsp = ctx.rsp` y `ret` saca la dirección de retorno del
     llamador. Es **exactamente un `ret`** en la entrada: `rsp` vuelve a +8 y los demás registros (`rax` incluido) salen
     como entraron.
   - Equivale a `ret 0` `0x359B50`, que no toca `rax`. Esas son las ranuras 97 y 98 de las dos clases: el cuerpo vacío
     `void` de la base.
2. **`0x34CFC20` (*thunk* sin marco):** es un `jmp rel32` de 5 B, sin prólogo ni `.pdata`. En su primer byte la pila es la
   del llamador más la dirección de retorno, así que el `ret` en la entrada es el de una función vacía.
3. **`0xB27880` (entrada con `.pdata`):** en el desplazamiento 0 no se ha ejecutado nada del prólogo (el primer
   `mov [rsp+0x10],rbx` escribe en el *home space* del llamador). Volver ahí es volver de una función que no hizo nada,
   y el estado de desenrollado es el del llamador.
4. **Los llamadores ignoran el valor de retorno:**
   - `0x36574C7 call [rax+0x318]` → `0x36574CD jmp 0x36575EC`: el epílogo recarga `r15/r12/r13/r14` de la pila y
     `0x3657600 mov al,1 ; add rsp,0x38 ; pop rsi ; pop rbp ; ret`. El `rax` del evento nunca se lee; el despachador
     devuelve su propio `al = 1`.
   - `0x36575D0 call [rax+0x318]` → `0x36575D6 mov rcx,[rsi+0xA8] ; … ; mov rax,[rcx] ; call [rax+0x328]` (el fin, ranura
     101): `rax` se sobrescribe antes de usarse.
5. **El fin sin inicio [V] (revisión de R-B, P3-3):** el motor ya llama a la ranura 101 sin la 99 (`0x365740D` y
   `0x36574DA` → `0x36575D6`), y los dos fines se leen (no son opacos) y toleran un inicio que no corrió:
   - **`PlayerCameraPreset`**, fin `0xB27E40` → `.xtls 0xA24AAC0`: recalcula el id por nombre (`0xAAEF80`) y llama a
     `vf 0x48` del `PlayerCameraComponentScript` (`0x11B9830`), que solo restablece la ranura del modo si
     `[this+ranura] == id` (`cmp [rcx],r8d ; jne`). Sin inicio no hay nada con ese id: no hace nada.
   - **`CameraSelector`**, fin `0x34CFF80` → `.xtls 0x12DBDED0`: pone el peso de mezcla a 0 y llama a
     `0x34E5D70(mgr, cámara, 0, …)`, que recorre `[mgr+0x110]/[mgr+0x118]` y quita **solo** las entradas con esa cámara.
     Sin inicio no encuentra nada: no hay doble liberación ni puntero nulo. Si la pila queda vacía ejecuta la rama «vuelta
     a la cámara de juego» (`0x34E5E88`), igual que en nativo al cerrar una escena (P3-4: como mucho, un pequeño
     reinicio de cámara al acabar la escena ajena; en el paso 3 el usuario no vio nada raro).

#### 3.2.4 Qué se queda nativo

- La ranura 101 (fin) de las dos clases (`0x34CFF80`, `0xB27E40`), la 100 (`0x34CFDE0`, `0xB27DE0`) y la 103.
- Las demás clases con sonda: `LetterBoxControl`, `PostProcess`, `SequencerAIDialog`, `StartLoadingSequencer`. Tuvieron
  0 llamadas en escenas ajenas: sin datos no se cortan.
- **Pendientes, sin sonda:**
  - `CameraSetFollowTargetEventData` (en el `.paseq` del herrero);
  - `CameraParameterPresetEventData` y `CameraEffectComponent` (cárcel);
  - `SubtitleGroup`, `ChangeFocusActor`;
  - el modo cinemática/exclusivo (`set_cutScene`/`set_exclusive`) y la pista `CameraComponent`.

  Si S-D muestra que la cámara cambia sin un `CameraSelector`/`PlayerCameraPreset` de esa etapa, R-B no basta y habrá
  que sondear estas.
- El despachador `0x36573B5` no se engancha: es un fragmento a mitad de función, compartido por todas las líneas de tiempo.

#### 3.2.5 Criterios de aborto (→ `guest_seq_foreign_timeline=probe`, o 0)

- **cámara del invitado pegada** (en el preajuste o el encuadre de la escena del anfitrión) **o pantalla negra o bandas**
  tras una escena ajena;
- cierre o crashpad;
- la escena del anfitrión que no avanza en el invitado (su copia o sus NPC quietos);
- un `[R-B SKIPPED]` de una escena propia del invitado o con `G = M`.

#### 3.2.6 Plan de los pasos 2 y 3

- **Paso 2** (`cdmp.2inst-s45-activo-guest.ini`: R-A = 1, R-B = probe, S-D ya instalada). Recorrido de §6: tienda,
  cárcel, Matthias, paseo y, si se puede, la tienda del invitado, con la ventana del invitado **con el foco** durante la
  cárcel e intentando moverse. Mirar:
  - R-A (§3.1.8/§3.1.9): paneles e interfaz fuera, sin nada pegado;
  - `[R-B WOULD SKIP]` en los `CameraSelector` / `PlayerCameraPreset` de la tienda y la cárcel, y ningún `NOT skipped`
    salvo de etapas no ajenas;
  - **S-D:** si `camera entry` cambia justo después del `WOULD SKIP` de esa etapa (la cámara va por esos eventos: R-B
    basta) o sin ninguno (va por la pista: R-B no basta);
  - `M+0x230` / `X+0x230`.
- **Paso 3** (`cdmp.2inst-s45-activo2-guest.ini`: R-B = 1), solo si el paso 2 sale limpio y S-D muestra que la cámara va
  por los eventos. Esperado:
  - en la tienda y la cárcel el invitado conserva su cámara;
  - `[R-B SKIPPED]` en esos eventos;
  - la escena del anfitrión sigue en su pantalla.

  Criterios de aborto de §3.2.5. Copia de las partidas antes de cada paso.

### 3.3 Vía C

Se queda con los vetos actuales; R-A y R-B no la tocan.

## 4. Las sondas (implementadas, solo lectura)

Clave nueva **`guest_seq_foreign_present_probe = 0 | 1`** (`[session2]`, solo invitado; por defecto 0 = ni ganchos
nuevos ni coste). Módulo `src/mod/session2/guest_seq_present.{h,cpp}`, lógica pura en `guest_seq_present_logic.h`,
tests en `tests/optd_seq_present_tests.cpp`. Juicio de etapa **ajena** igual que S44: dueño `[stage+0x98]` personaje
`0xA…` distinto del propio y del de M; nada es ajeno mientras la clave propia sea 0.

Verificación común antes de instalar cada pieza: base `0x140000000` y `TimeDateStamp` `0x6AB28F00`; `.pdata`; firma
(16–40 B, **única** en `.rsrc`); para cada clase de evento, su vtable (COL en `[vt-8]`) y que su ranura 99 apunte a la
entrada; `p8_install_mid` (solo `.rsrc`, bytes reubicados = los verificados). Un escaneo de todo el exe no encontró
ningún salto rel8/rel32 ni puntero absoluto a los bytes parcheados (salvo a su primer byte)
(`s45-datos/sondas/verificacion-sondas-2976.txt`). Si algo falla, esa sonda no se instala y sale un WRN.

| Sonda | Punto (RVA) | Tipo | Verificación | Qué registra |
|---|---|---|---|---|
| **S-A** | P1, entrada de `0x602940` (de `guest_seq_foreign`, que ahora lo instala también para esta clave) | ya existente | la de S44; COL de las tres vtables del procesador; bytes de `0xA9D8F6` (`+0x298` → `0x34DC130`), `0x34DC175`, `0x34DBB03` (`T+0x1D8`) | toda etapa que empieza va a una tabla (secuenciador, id de línea de tiempo, dueño, juicio). Para las **ajenas**, una línea: `[stage+0x58]`, su vtable y si es la de cliente / común / servidor / otra, `[proc+8]` frente a `[stage+0x28]`, `[proc+0x10]` frente a la etapa, `stage+0x298`, si G = M. Tabla R-A una vez |
| **S-A2** | los manejadores de `guest_x_veto` (con G = X) | ya existentes | — | por manejador, llamadas **antes** y después del P1 de su etapa (y cuántas de etapa ajena); una línea por cada «antes» |
| **S-B** | `0x34CFC20` `CameraSelectorEventData` | *thunk* de 5 B (`jmp .xtls`), sin `.pdata` (hoja) | firma 16 B única; vtable `0x558B4A8` / COL `0x5DB7610`, ranura 99 | por evento: clase, raíz de `[rdx+8]` (profundidad, vtable), `raíz+0x1D8`, `+0x1D1`, `+0xDC`, si la raíz es la línea de tiempo de una etapa de la tabla y de quién; `ctx+0x5C`; `ev+0x38`; coste |
| **S-B** | `0xB27880` `SequencerPlayerCameraPreset` | entrada con `.pdata` (`0xB27880–0xB27DDE`), 5 B | firma 40 B; vtable `0x55F71B8` | ídem |
| **S-B** | `0xC38F30` bandas (`LetterBoxControl`) | entrada de un *stub* hoja, 9 B (`movzx` + `jmp`) | firma 16 B; vtable `0x562E0B0` | ídem |
| **S-B** | `0xC3A440` posproceso | entrada con `.pdata` (`0xC3A440–0xC3A510`), 6 B | firma 40 B; vtable `0x562E420` | ídem |
| **S-B** | `0xC4D6B0` `SequencerAIDialog` | entrada con `.pdata` (`0xC4D6B0–0xC4E14D`), 7 B | firma 40 B; vtable `0x5629BF0` | ídem |
| **S-C** | `0xC45747` dentro de `StartLoadingSequencer` `0xC45740` | *mid* de 8 B (`mov rdx,[rax+0x10]` + `movzx eax,[rcx+0x38]`), dentro de una hoja de 0x16 B, tras su carga RIP-relativa (que no se toca) | firma 24 B en `0xC45747`, 7 B de cabecera en `0xC45740`, sin `.pdata` que la cubra, vtable `0x5630920`, nadie salta dentro | lo de S-B más el byte que escribe en `P+0x1E0` y el que había; si `rax` es `[0x6D691E0]` |
| **S-D** | gancho de fotograma `0x8B6144` de `guest_seq_play` | ya existente | bytes de `0xB27DEE` (`+0x118`/`+0x40`/`-0x28`), `0xA98D3E` (`G+0x230`), `0x5E4E6E`, `0xA9D8F6` | cada fotograma, solo **cambios**: entrada actual de la cámara `[[cw+0x118]+0x40]` y la vtable de su objeto, cuentas de las listas del reproductor (contadas, sin línea propia), `[[cw]]+0xDE8`, nivel de bloqueo de entrada `M+0x230` y `X+0x230` de la etapa ajena viva más reciente, y si su línea de tiempo está en el reproductor y su `+0xDC` |

Notas:
- **Entrada del jugador local:** el único campo legible identificado es el nivel de bloqueo por etapas `G+0x230`
  (`0xA98AD0`, al que llama `InputBlock`). El bloqueo que pueda poner el modo cinemática/exclusivo de la línea de tiempo
  **no tiene campo identificado** (está en `.xtls`); S-D no lo ve directamente.
- **S-A2 depende de `guest_x_veto`:** sin `guest_x_ui_veto` ni `guest_x_camera_veto` no hay datos (el resumen lo dice).
- **S-D depende de `guest_seq_play`:** sin su gancho no hay fotogramas (el resumen lo dice).
- **Coste:** cada gancho mide su propio tiempo con QPC (media, máximo, total) y el resumen lo da por clase; S-D también.
- **Presupuesto:** anillo de 512 sin bloqueo, límites por tipo (5 s), 3000 líneas, resumen cada 60 s.

Correcciones de la revisión de las sondas (`revision.md`, todas de severidad baja):
- **B1:** cada línea S-B/S-C lleva `caller exe+0x…`, el RVA de la dirección de retorno (`[rsp]` en el punto). El resumen
  cuenta hasta cuatro llamadores por clase. Así se separa la ranura 67 de `LetterBoxControl` (`0xC38F30` está también
  ahí) de su ranura 99.
- **B2:** `guest_x_veto` pasa el procesador, y S-A2 lee `proc+0x10` dentro, después de comprobar la clave: con la sonda a
  0 no se lee nada.
- **B3:** S-D recorre el reproductor (`find_timeline`) solo cuando cambia la etapa ajena viva o cada 16 fotogramas; entre
  medias conserva el último resultado. El resumen cuenta los recorridos.
- **B4:** el hilo de instalación espera a que `guest_seq_foreign` haya **terminado** de instalar (hasta 11 min, más que su
  propia espera de `WorldSystem`). El WRN «settled WITHOUT P1» solo sale si P1 de verdad no se instaló; si aún no ha
  terminado, sale otro WRN que lo dice.

## 5. Inis

- Invitado, paso 1: `docs/research/ini/cdmp.2inst-s45-probe-guest.ini` = `cdmp.2inst-s44-activo-guest.ini` +
  `guest_seq_foreign_present_probe=1` + **`guest_seq_foreign_present=probe`** (R-A solo mide; cabecera de comentario).
- Invitado, paso 2: **`docs/research/ini/cdmp.2inst-s45-activo-guest.ini`** = el de probe + `guest_seq_foreign_present=1`
  (comprobado con `diff`: solo cambian la cabecera y esa línea).
- Invitado, paso 2 (actualizado): `cdmp.2inst-s45-activo-guest.ini` lleva además `guest_seq_foreign_timeline=probe`
  (R-A activo, R-B midiendo, S-D funcionando).
- Invitado, paso 3: **`cdmp.2inst-s45-activo2-guest.ini`** = el del paso 2 con `guest_seq_foreign_timeline=1`.
- Anfitrión: `cdmp.2inst-s43-activo-host.ini` sin cambios (no lleva ninguna clave de S45).
- Comprobación fuera del juego con `cdmp_host.exe` (§10): la línea `Config: P8 X veto` sale
  `'1' 'probe' '1' 'SetInteraction' 'stuck'`, y `Config: S44/S45 foreign scenes (guest)` sale
  `'1' 1 1 guest_seq_foreign_present 'probe'` (paso 1) o `'1'` (paso 2).

## 6. Plan de prueba en vivo (dos copias en un PC; probe)

Antes: copia de las partidas, partidas del invitado separadas (`CLAUDE.md`), los dos procesos reiniciados, ventana del
invitado delante en cada caso. Marcar con Ctrl+F12 al empezar y al acabar.

1. **Tienda:** el anfitrión habla con el vendedor de armas (`cd_seq_funcnpc_smithy`) y compra.
2. **Cárcel:** el anfitrión ataca a NPC en la ciudad hasta que lo arrestan (`..._wakeup_prison_hernand_0100`).
3. **Recuerdo de Matthias** (`cd_seq_memory_hernandgibbet`, `boss_rematch_matthias`).
4. **Paseo** por la ciudad con NPC del mundo animados (escenas `funcnpc_*` en reposo, minijuegos, tiempo, fauna).
5. Si se puede: el invitado habla con el mismo vendedor (su escena **propia**; no debe salir como ajena).

Criterios de aborto: cierre o crashpad, tirones visibles (mirar el coste en el resumen), cualquier WRN de instalación
inesperado → `guest_seq_foreign_present_probe=0` y `guest_seq_foreign_present=0`.

Paso 2 (R-A = 1, `cdmp.2inst-s45-activo-guest.ini`): el mismo recorrido, con lo esperado y los criterios de aborto de
§3.1.8 y §3.1.9.

## 7. Qué mirar en el log del invitado

1. **Instalación:** `GuestSeqPresent guest_seq_foreign_present_probe=1 ... S-B/S-C hooked: ...` (las seis) y la línea
   `GuestSeqPresent R-A table` (32 / 3 / 1 y 33 fuera de rango: la clasificación cruda; las decisiones, en §3.1.2). Cualquier `NOT installed` dice por qué.
2. **S-A:** `[S-A P1 FOREIGN stage]` de la tienda y la cárcel: ¿vtable = `ClientSequencerStage_StageChartProcessor`?,
   ¿`[proc+8] ==`, `[proc+0x10] == the stage`?, ¿id de línea de tiempo distinto de `-1`? Una vtable «other» o un
   procesador nulo en P1 cambian R-A.
3. **S-B:** en la tienda y la cárcel, líneas `[S-B CameraSelectorEventData] timeline of a FOREIGN stage` con el
   secuenciador de esa escena. Si solo salen `timeline of no stage P1 saw`, el vínculo raíz → `+0x1D8` no es el
   supuesto: mirar la vtable de la raíz y el id. El resumen da cuántas por clase y su coste.
4. **S-C:** ¿algún `StartLoadingSequencer`? Con `0x8CD771B1` denegada por lista no debería; si sale uno de etapa
   ajena, R-B tiene que cubrirlo (la carga de facción iría también por ahí).
5. **S-D:** al empezar la escena ajena, ¿cambia la entrada de la cámara (`camera entry`, su vtable) o `+0xDE8`? ¿Antes,
   a la vez o sin ningún S-B? ¿Sube `X+0x230` (bloqueo por `InputBlock` de X, que hoy se veta) o `M+0x230`? ¿La línea
   de tiempo de la etapa ajena está en el reproductor y con `+0xDC` = 1 mientras se ve la cinemática?
6. **S-A2:** `[S-A2 BEFORE P1]`: cualquier función de etapa de X antes de su P1 es un hueco de R-A.
7. **R-A:**
   - instalación: `GuestSeqPresent R-A guest_seq_foreign_present=… ARMED` (24 a `ret`, 1 a la del común, 2 de mundo, 9
     de apagado o con la dirección en los datos, 80 copiadas). Un `R-A … NOT armed` dice por qué;
   - por escena ajena: `[R-A WOULD CUT presentation]` (probe) o `[R-A CUT presentation]` (1), o `[R-A NOT cut]` con el
     motivo; un WRN `[R-A RESTORED]` es criterio de aborto;
   - cada 60 s, `GuestSeqPresent R-A summary`: veredictos, escrituras, restauraciones, fallos, el hueco antes de P1 y las
     llamadas de los manejadores desde etapas no ajenas de X (P2-1).

## 8. Qué no está demostrado

- ~~[H] Que la raíz de `[ctx+8]` sea el objeto de línea de tiempo del reproductor~~: **[V] en el paso 1** (§3.2.1).
- [H] Que la cámara de pista se aplique por eventos (S-B) y no la lea directamente el sistema de cámara (S-D lo separa
  en el paso 2). Si no, R-B no basta.
- ~~[E] Que el fin de `CameraSelector` tolere un inicio saltado~~: **[V]**, los dos fines se leen y lo toleran (§3.2.3,
  punto 5).
- [H] Las clases de evento sin sonda (§3.2.4).
- [H] Que el procesador exista ya en la entrada de `0x602940` (S-A cuenta los nulos; si no existe, R-A no actúa en esa
  etapa: `no processor`).
- [E] Que nada en `.xtls` compare el vptr del procesador. Estático: ninguna otra referencia a `0x558C080` en todo el
  ejecutable; `.xtls` está cifrado en parte, así que no se puede descartar del todo. Lo medirá R-A = 1 en vivo.
- [E] Que `Execute` (vf8/vf9) sea `void`. Se deduce del plegado del compilador: base = `ret 0`, y el vacío `bool` es otra
  función. No hay firma de C++.
- [H] Que cortar `Reward`, `HireMercenary` y los minijuegos de una etapa ajena no deje esa escena esperando algo en el
  invitado. Para `UIControl`/`InputBlock`/`Loading` sí hay experiencia, porque hoy se vetan.
- [E] La dirección de los pares sin cuerpo legible (`+0x1D8`, `UIHint`, `LookAt`, `ActivateMiniGame`): por la
  convención vf8 = inicio y vf9 = fin, y por nombre. Ante la duda, lo dudoso se queda nativo.
- [H] Que la tabla del mod no deje ningún encendido sin su apagado nativo: lo mide el resumen de R-A en probe (P2-1).
- [E] Que `DisableCharacterCollision` solo toque NPC. El invitado nunca se enlazó en una escena ajena (S44), pero no es
  imposible.
- [H] El tamaño del hueco antes de P1 (lo mide S-A2 con la sonda) y que P1 corra siempre en el hilo del fotograma. Si no,
  R-A no escribe y lo dice: `NotMainThread`.
- [H] Qué es `[[cw]]+0xDE8` (dato de `p8-s9` §7.3, estimado) y qué es la entrada `+0x40` del gestor de cámara (se
  infiere de `0xB27DE0`).
- [H] El bloqueo de entrada del modo cinemática: sin campo identificado.
- [H] Que estas cinco clases sean todas las de presentación que importan; las demás (`SubtitleGroup`,
  `ChangeFocusActor`, control) pasan por `0xC35790` → `.xtls` y no tienen sonda aquí.
- No se ha probado en el juego, ni con Shadow.

## 9. Archivos

- `s45-datos/nativo.md`, `s45-datos/carcel.md`: los dos informes de la investigación (copia del scratchpad).
- `s45-datos/nativo/`, `s45-datos/carcel/`: sus scripts y salidas pequeñas (sin capturas ni datos del juego).
- `s45-datos/sondas/`: los scripts de la verificación de las sondas (`sigs.py`, `inbr.py`, `slots.py`, `rtti.py`, …) y
  su salida `verificacion-sondas-2976.txt`.
- `s45-datos/ra/`: los scripts de R-A y su salida `ra-verificacion-2976.txt`:
  - `slots.py`: las tres vtables;
  - `use.py`: qué toca cada manejador;
  - `tramp.py`: los trampolines;
  - `vtref.py`: las referencias a las vtables;
  - `callers.py`: quién construye y destruye;
  - `cmpvec.py`: los vectores de los tests frente al exe;
  - `rva32.py`: referencias por RVA de 32 bits a la vtable, al COL y al TypeDescriptor (revisión P2-3).
- `s45-datos/vivo1.md`: el análisis del paso 1 en vivo.

## 10. Build y comprobación fuera del juego

**Correcciones de la revisión de R-A** (`revision-ra.md`: P1-1, P2-1, P2-2 y P3): el binario se compila en limpio desde
el commit que las contiene, y `cdmp_host` se repite con los dos inis en carpetas nuevas. El SHA-256 y la salida están en
el informe de esa entrega (el commit no puede contener su propio hash). Lo que sigue es la entrega anterior (`266df9a`).

- **Build limpio** (carpeta `build\` borrada) con `.\scripts\build.ps1` desde el commit `266df9a`: 81/81 tests en verde,
  incluido `optd_seq_present`, con los tests nuevos de R-A (decisiones a partir de las tres vtables del exe, rechazos,
  tabla, trampolín y juicio) y de B1/B3.
- `cdmp.asi`: SHA-256 `6863C0DBA8352306829E6D81F79FB2CA707297A33CFD4AD04CDB55C7753DAC29` (12 037 120 bytes; el log dice
  `build 266DF9AA`). **No desplegado.**
- **`cdmp_host.exe`, 4 s, `CDMP_ROLE=guest`:**
  - En `C:\Emulador CD\build\s45a\`, el asi más `cdmp.2inst-s45-activo-guest.ini` como `cdmp.guest.ini`. Salen
    `Config: P8 X veto: guest_x_stage_veto '1' guest_x_camera_veto 'probe' guest_x_ui_veto '1' guest_x_ui_veto_allow
    'SetInteraction' guest_local_after_remote 'stuck'` y `Config: S44/S45 foreign scenes (guest): guest_seq_foreign_player
    '1' guest_seq_foreign_probe 1 guest_seq_foreign_present_probe 1 guest_seq_foreign_present '1'`. Sin ERR ni WRN, y
    descarga limpia.
  - En `s45b\`, con `cdmp.2inst-s45-probe-guest.ini`: la misma línea con `guest_seq_foreign_present 'probe'`.
  - Fuera del juego no hay `WorldSystem`, así que la instalación (R-A incluida) no llega a correr: lo esperado.

## 11. Resultados en vivo (dos copias del juego en un PC, 16-10)

Build del mod de los pasos 2 y 3: `D3DEBF9D` (antes de las correcciones de la revisión de R-B, que no cambian el
comportamiento en estos inis). Anfitrión con `cdmp.2inst-s43-activo-host.ini`.

| Paso | Configuración | Datos (`C:\Emulador CD\backups\`) | Resultado |
|---|---|---|---|
| 1 | sondas + R-A probe | `p8-2inst-s45-probe\` | R-A `ARMED` 25/2/9/80; 58 `WOULD CUT`, todos válidos; G = M 0; `NOT cut` / `RESTORED` 0; S-A2 0 antes de P1. Vínculo evento → etapa demostrado. S-D no se instaló (arreglado). Detalle: `s45-datos/vivo1.md` |
| 2 | R-A = 1, R-B = probe (`cdmp.2inst-s45-activo-guest.ini`) | `p8-2inst-s45-paso2\` | R-A: `CUT x34`, `vptr written 34`, 0 fallos, 0 `RESTORED`, manejadores después de P1: 0. R-B: 5 eventos `WOULD SKIP`: 3 `SequencerPlayerCameraPreset` de la tienda (`cd_seq_funcnpc_smithy`, etapa `0x293`) y, de la cárcel (`cd_seq_quest_crime_police…`, `0x301`), 1 `CameraSelectorEventData` + 1 `SequencerPlayerCameraPreset`. 0 `NOT skipped`. 0 ERR |
| 3 | R-A = 1, R-B = 1 (`cdmp.2inst-s45-activo2-guest.ini`) | `p8-2inst-s45-paso3\` | R-A: `CUT x55`, 0 fallos, 0 `RESTORED`, después de P1 0. R-B: 12 eventos `SKIPPED` (10 `SequencerPlayerCameraPreset` de la tienda, etapa `0x2A1`; de la cárcel, `0x2FB`, 1 `CameraSelectorEventData` + 1 `SequencerPlayerCameraPreset`); 0 `NOT skipped`. 0 ERR |

Notas de recuento:
- El grep de `[R-B WOULD SKIP]` / `[R-B SKIPPED]` da 6 y 13 líneas: la línea `ARMED` de instalación también contiene el
  texto. Los eventos reales son 5 y 12 (el resumen de las 08:22:21 da `SKIPPED 12`).
- Los 6 `CameraSelectorEventData` «not foreign» de cada paso son de las escenas propias de carga del invitado
  (`abyssoneloading`): bien separados.

**Lo que vio el usuario:**
- Paso 2: nada raro observado, salvo lo de las cámaras. Durante las escenas del anfitrión el invitado conserva los controles
  (lo que corta R-A: `InputBlock`, `UIControl`, paneles). La cámara de la escena sí llegaba: R-B estaba en probe.
- Paso 3: «todo funcionó bien con respecto a las escenas». En el invitado no salen ni la tienda vacía ni la cinemática de
  la cárcel.

**S-D (primera vez instalada) y coste:**
- Paso 2: 5 509 fotogramas, media 1,0 µs, máximo 57,4 µs, total 5,97 ms. Paso 3: 9 410 fotogramas, media 1,0 µs, máximo
  9,3 µs, total 9,55 ms. Recorridos del reproductor: 229 y 382 (B3).
- Los inicios de cámara: 2,6–2,9 µs de media, máximo 3,8 µs, 7 + 11 llamadas en el paso 3.
- **La entrada de cámara que lee S-D (`[[cw+0x118]+0x40]`) no cambió nunca, ni en el paso 2 (cámara de la escena visible)
  ni en el 3** (solo la primera muestra). Por tanto ese campo no es el que lleva la cámara de las escenas [E]; S-D no
  sirve para separar «cámara por eventos» de «cámara por pista». El resultado del paso 3 (sin cámara ajena en pantalla)
  sí muestra que saltar esos dos inicios basta en la tienda y la cárcel [V en vivo].
- `+0xDE8` cambió 3 veces y `M+0x230` (bloqueo de entrada del invitado) 3 veces en cada paso, todas en la carga inicial
  del invitado; `X+0x230` cambió 4 y 8 veces (escenas del anfitrión en X, sin efecto en el invitado).
- `FreezeWatch` y los `Game error` del juego del paso 3 son del cierre (`Terminate`) y de mensajes internos del juego
  (gimmicks, proyectiles), como en sesiones anteriores.

**Lo que queda:**
- La prueba entre dos PC con Shadow (red real, túnel por Steam, cargas a distinta velocidad).
- Las clases de evento sin sonda (§3.2.4) y el modo cinemática: en estas escenas no hicieron falta.
- P3-4 (posible reinicio de cámara al acabar una escena ajena): no se vio.
- Un efecto nuevo, que no es de S45 hasta que se demuestre: objetos del escenario rotos por el anfitrión que no
  reaparecen (`PENDIENTES.md`).
