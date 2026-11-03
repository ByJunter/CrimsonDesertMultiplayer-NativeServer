# S45 — Escenas ajenas y cliente local: datos, semántica nativa y punto de raíz

Build 1.0.0.2976 (PE `0x6AB28F00`, base `0x140000000`), direcciones en RVA. Solo lectura: no se compiló, no se
desplegó y no se abrió el juego. Herramientas: capstone sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`,
decompilación Ghidra (`cdmp-query.ps1`, caché en `exports\decomp\`), logs y capturas de
`C:\Emulador CD\backups\p8-2inst-s44-{probe,activo,activo2,regresion,control-matthias}`. Scripts y salidas en
`scratchpad\s45\nativo\`:
- `scf.py` → `scf.txt`: cada `StageChart_Function_*` → ranura del procesador → manejador cliente / común / servidor;
- `slotscan.py` → `slotscan.txt`: qué toca cada manejador (resolución de actor, `GetLocalPlayer`, globales del cliente);
- `tev.py` → `tev.txt`: inicio y fin (ranuras 99 y 101) de los eventos de línea de tiempo;
- `r280.txt`, `r280ctx.txt`: lectores y escritores de `stage+0x280`;
- `findcall.py`, `learef.py`, `near.py`, `dc.sh` (decompilación con reintento por el cerrojo del proyecto).

Integra lo del informe de la cárcel (`s45\carcel.md`).

Marcas: **[V]** leído en instrucciones, decompilación, log o captura; **[E]** inferencia fuerte; **[H]** hipótesis que
hay que medir.

---

## 0. Resumen

1. **Los 11 + 11 «control to this client's character» de P2 son escenas PROPIAS del invitado, de la entrada en el
   mundo. No son la tienda [V].**
   - P2 solo escribe líneas de las escenas ajenas (`foreign = own && owner && owner != own`); en cambio cuenta todas.
   - Las 11 + 11 aparecen en los cinco logs, siempre en la ventana de carga, antes de la primera escena ajena
     (`on foreign scenes 0`) y nunca vuelven a crecer.
   - Coinciden con la misma serie nativa de 10 `UpdateSequencerGamePlayDataEventReq` 2173 propios (ids
     `0x60000000..09`) que **también hace el anfitrión** en su propia entrada.
   - En las escenas ajenas, el nodo `CurrentControl` **nunca** se resolvió al jugador local:
     - antes de S44 iba a la copia del anfitrión (rama de NPC);
     - con S44 = 1, a nada (`link 0`).
2. **La hipótesis del encargo es falsa [V].** El «jugador» de cámara, interfaz, tienda, entrada y carga no se resuelve
   por `CurrentControl` → `GetLocalPlayer`/`vf178`. Esos efectos no resuelven ningún actor: escriben directamente en los
   **servicios globales del cliente** colgados de `[0x6D691E0]` (ClientWorld):
   - `+0x10` = estado de carga P;
   - `+0x20` = interfaz;
   - `+0x118` y `+0x120` = cámara y reproductor de líneas de tiempo;
   - `+0x30` = gestor de actores y usuario locales.

   Para el motor, toda etapa que existe en el cliente es del jugador local, porque el servidor solo la manda a su
   dueño. **No hay ninguna comprobación nativa general de que «esta escena es del jugador local»** (§2.4).
3. **`stage+0x280` no es la marca que buscamos [V].**
   - Es la **clave de un personaje** (`actor+0x60`), y la escribe `0x205F7F0` con un registro de guardado pendiente.
   - Su semántica es «etapa ligada al personaje C del usuario dueño».
   - La leen unos 10 sitios: carga, posproceso, autoenlace, una condición de etapa… Ninguno es de cámara, interfaz,
     entrada ni línea de tiempo.
   - Ponerle la clave del anfitrión en X es **peligroso**: el tick de X (`0x600220`) compara `+0x280` con el personaje
     controlado por el **usuario dueño** y, si coinciden, **lanza la carga diferida** (`0x631020`) en el cliente del
     invitado (§2.4).
4. **Las vías por las que una escena ajena llega al cliente local son tres [V]:**
   - **(A) Funciones de etapa con ranura en el procesador** (`ClientSequencerStage_StageChartProcessor`, vtable
     `0x558C080`). Las de presentación son justo las ranuras que en el procesador común (`0x590DF78`) y en el del
     servidor (`0x5B1FE10`) son `ret`: una clasificación **nativa**, sin listas (§2.1).
   - **(B) Eventos y pistas de la línea de tiempo** (`.paseq`): cámara (`CameraComponent`, `CameraSelectorEventData`,
     `SequencerPlayerCameraPreset`), bandas, posproceso, `StartLoadingSequencer` (escribe `P+0x1E0`), diálogo y
     subtítulos. Los reproduce el reproductor global `[0x6D691E0]+0x120`, con el id `stage+0x298`. **No pasan por
     ningún manejador de `guest_x_*`.** Ahí cayeron la cámara de la tienda y la de la cárcel (§1.2, §1.3).
   - **(C) Funciones de etapa «de diálogo»** con código propio (no por ranura): `SetQuestDialogAICategory`,
     `TimelineEvent`, `BlockInteraction` y `SetInteraction`.

   El control de secuenciador (vf21/vf22) **ya está cerrado** por S44 [V].
5. **Punto de raíz propuesto** (§3): **dos fronteras**, no una. No hay un único punto nativo.
   - **R-A**, para la vía (A): **un punto**. En el inicio de cada etapa de X (`0x602940`, la entrada de P1, ya
     verificada), el procesador de la etapa pasa a una vtable «solo mundo» del mod. Es copia de `0x558C080` con las
     ranuras de presentación sustituidas por las del procesador común (`ret`) y se construye en tiempo de ejecución a
     partir de las tres vtables nativas. Retira los 23 ganchos de `guest_x_ui_veto` y `guest_x_camera_veto` y V1.
     **Tiene escritura** (el puntero de vtable de un objeto del juego); riesgos en §3.1.
   - **R-B**, para la vía (B): **el despacho genérico de eventos de la línea de tiempo**. Son 4 llamadas de inicio
     (`call [vt+0x318]`) y 2 de fin (`+0x328`) en `.rsrc`: `0x377FC03/0x377FC2F/0x377FC3C`, `0x3780474/0x37804A0`,
     `0x3782243/0x378226C`, `0x3780F2E`, y de fin `0x3780F05`, `0x377A96B`. Ahí se saltan los eventos de presentación
     (clase de evento clasificada al instalar) de las líneas de tiempo de etapas de X.
     - **Falta medir [H]** cómo llegar del contexto (`rdx`) a la etapa o a su `+0x298`, y si la cámara de pista
       (`CameraComponent`) depende de un evento o la consume el sistema de cámara directamente.
     - Primero, sonda de solo lectura (§4).
6. **La vía (C) no la cubre ninguna de las dos.** Se queda con los vetos actuales (`TimelineEvent` vetable,
   `SetInteraction` permitido porque es del mundo); los otros dos son *thunks* a `.xtls`.

---

## 1. Datos

### 1.1 Los 11 casos de P2 [V]

| Sesión | Cuándo se cuentan | vf21 / vf22 «this client» | Escenas ajenas en ese momento | 2173 propios `0x6000000N` (`N` 0–9) |
|---|---|---|---|---|
| probe | entre 22:47:11 y 22:48:11 (carga) | 11 / 11 | 0 | t 94,3–147,4 |
| activo | 22:57–22:59 | 11 / 11 | 0 | t 82,8–118,2 |
| activo2 | 23:25–23:27 | 11 / 11 | 0 | t 213,3–248,7 |
| regresion | 23:47–23:49 | 11 / 11 | 0 | t 63,9–117,0 (actor `0xA0100067`) |
| control-matthias | 23:07–23:08 | 11 / 11 | 0 | t 163,4–198,7 |

- **Código:** `guest_seq_foreign.cpp:312-316`. `g_p2[]` cuenta todas las llamadas, y la línea solo se escribe si
  `foreign` u `OtherPlayer`. **No hay ni una línea P2 con «this client's character»** en ningún log, así que ninguna de
  las 11 es de una escena ajena.
- **No hay ningún 2661 del invitado** en esas ventanas (0 en probe; en activo, uno a t 202). Las etapas se crean en el
  propio cliente: es la secuencia de entrada en el mundo.
- **El anfitrión hace la misma serie** de 2173 (`01 00 10 A0`, ids `0x60000000..06`, t 55,8–86,4 en probe). Es nativo.
- **P1 en probe** cuenta `own scene x1` en la carga, así que son una o pocas etapas propias con 11 eventos
  SequencerControl. El nombre no queda en el log (P1 no escribe las propias).

### 1.2 La tienda del anfitrión (`cd_seq_funcnpc_smithy`, `0xA7132561`) [V salvo lo marcado]

`ShopProbe [2671]` en el host.log marca cada interacción del anfitrión:

| Sesión | 2671 del anfitrión (interacción `0xF4321` = tienda) | Escena en X del invitado |
|---|---|---|
| probe | 22:50:54,997 → NPC `0xB0100500` | `0x29A` smithy; el nodo `0x20000008` → `B0100500` es el herrero |
| activo | 23:01:47,590 → `0xB010086D` | smithy |
| activo2 | 23:30:08,915 → `0xB0100DEF` | smithy |

Probe a las 22:50:55–22:51:05, en X:

1. **Ramas de la escena:** 2981 de los estados 1, 2, 7 y 0 enrutados a X; «state 2 into X» a las 22:50:56,104.
2. **P2:**
   - `CurrentControl` (`0x20000002`) → `0xA0100001` (la copia del anfitrión, rama de NPC);
   - `0x20000008` → herrero;
   - **nunca** el invitado.
3. **Vetos:** `InputBlock` ×5 y `UIControl` ×4 VETADOS; `UIStageGuide` vetado.
4. **Corren de forma nativa** en el invitado:
   - `SetInteraction` add/remove (permitido);
   - `ChangePhasePanelTag` ×2, que va a la interfaz local `[0x6D691E0]+0x20`, `0x5E9750` [V];
   - `BlockInteraction` on/off y `SetQuestDialogAICategory` (solo probe, no vetables).
5. **Cámara: cero llamadas** a las funciones de etapa de cámara en toda la sesión (resumen final: «handlers 28/28
   hooked», ninguna de la familia de cámara con llamadas). Pasa lo mismo en activo y activo2.
6. **La línea de tiempo del herrero sí lleva cámara propia** [V, strings del `.paseq`/`.paseqc`]:
   - en el `.paseq`: `CameraComponent`, `CameraFollowProcessor`, `CameraLookAtProcessor`,
     `CameraSetFollowTargetEventData`, `_cameraPresetName` / `PlayerCameraPresetSelectionType` (evento
     `SequencerPlayerCameraPreset`), `_screenFadeInOut`, `camera_1`;
   - en el `.paseqc`: `_hasSequencerCamera`, `_hasSequencerCamera_Jump`, `Timeline.set_cutScene(True)` ×5,
     `Timeline.set_exclusive(True)` ×5 y `OnSequencerSetStoreMainMenu`.
7. **[E fuerte] La «escena de tienda vacía»** es la cámara de la línea de tiempo de X (vía B), más los paneles que no
   se vetan (`ChangePhasePanelTag`). La tienda en sí (`NpcStoreBuyView` por `UIControl`) estaba vetada, y por eso sale
   «vacía, sin poder comprar».

### 1.3 La cárcel (de `carcel.md`, comprobado en `regresion\guest.log`) [V]

- **La escena:** `cd_seq_quest_crime_police_wakeup_prison_hernand_0100` (`0xE5B490F1`, seq `0x30A`). Antes llegan
  `cd_seq_quest_crime_police*` `0x307/0x308/0x309` (23:50:11–13).
- **S44 funcionó:** `[foreign] NEUTRALIZED`, P1 `link 0`, y P2 solo da control a los guardias.
- **Lo que llegó al invitado:** cámara y modo cinemática por la línea de tiempo (vía B). Ningún manejador de cámara de
  etapa se llamó para `0x30A`.
- **Las dos únicas `SequencerCamera` de X en toda la regresión** son de `cd_seq_quest_progression` (`0x404`, 23:51:02, y
  `0x4ED`, 23:54:31), en probe.

### 1.4 Cómo se resolvió el «actor local» [V]

| Vía | ¿Se usó para el invitado? |
|---|---|
| Enlace a `0xA0100066`/`0xA0100067` en un 2661 ajeno | **No.** Ningún enlace al invitado en 1 635 escenas (S44) ni en estas sesiones |
| `0x205E520` tipo 0, por datos del nodo | No visto. Con S44 = 1, el nodo `CurrentControl` a tipo 0 no dio actor en P2. `0x205E520` resuelve el `0x205D7C0` (dueño `stage+0x98`), que no es el invitado |
| `CurrentControl` → `GetLocalPlayer`/`vf178` | No existe esa vía. `vf178` solo **elige la rama** para un actor ya resuelto por enlace |
| **Servicios globales del cliente** (`[0x6D691E0]+…`) | **Sí.** Es lo que afecta al invitado: cámara, interfaz, carga y entrada |

---

## 2. Semántica nativa

### 2.1 Vía A — funciones de etapa con ranura en el procesador [V]

- **El trampolín** `Execute` (vf8) de cada `StageChart_Function_*` hace `mov rax,[rdx] ; mov r8,rcx ;
  mov rcx,[rax+0x58] ; mov rax,[rcx] ; jmp [rax+off]`:
  - `[ctx]` = la etapa;
  - `stage+0x58` = **su procesador** (uno por etapa; en los logs, procesadores distintos por etapa);
  - `proc+8` = G (gestor), `proc+0x10` = la etapa.
- **La vtable del procesador se lee en cada llamada** (`0x2368240`, `0x236A120`, etc.). Ningún manejador resuelve
  quién es «el jugador».
- **La tabla de `scf.txt`** compara cliente, común y servidor:
  - **Solo cliente** (común = `ret`, servidor = `ret`): `ActivateMiniGame`/`Deactivate`/`ExecuteMiniGameEvent`
    (`+0x128..0x138`), `Craft` `+0x140`, **`InputBlock` `+0x150`**, **`UIControl` `+0x158`**, `ChangePhasePanelTag`
    `+0x160`, `UIShowMinimap` `+0x168`, `UIFindPath` `+0x170`, `SetCustomMesh` `+0x198`, `DisableCharacterCollision`
    `+0x1A0/+0x1A8`, `SetPreviewTarget` `+0x1B0`, `DeactiveSequencerCamera` `+0x1B8/+0x1C0`, **`SequencerCamera`
    `+0x1C8`**, `DeactivateSequencerCameraFadeIn` `+0x1D0/+0x1D8`, **`CameraPreset` `+0x1E0/+0x1E8`**, `ForceLockOn`
    `+0x1F0`, `UIStageIcon` `+0x1F8`, **`LetterBox` `+0x208`**, **`FadeInOut` `+0x210`**, `LookAt` `+0x220/+0x228`,
    `UIStageGuide` `+0x238`, `UIStageBackgroundGuide` `+0x240`, `UIHint` `+0x248/+0x250`, `PostProcess` `+0x260` y
    `MaterialParameter` `+0x268`.
  - **Cliente y servidor:** `HireMercenary` `+0x200`, **`Loading` `+0x218`** (cliente `0x5F2A00`; servidor *thunk*),
    `Reward` `+0x258` y `VarySharpness` `+0x288`.
  - **Comunes:** `SetPhase` `+0x230` y `SetHologramActor` `+0x298/+0x2A0`.
  - Las funciones de etapa del mundo que importan al servidor (`CreateItem`, `PushReward`, `EvadeTrigger`,
    `CompleteStage`, `SetWanted`, `Sell`…) son `ret` en el cliente.
- **Qué tocan** (`slotscan.txt`):
  - casi todas las de solo cliente leen `[0x6D691E0]`;
  - `Craft`, `ActivateMiniGame` (un nivel por debajo) y `Loading` llaman a `GetLocalPlayer 0x8B44E0`;
  - **resuelven actores por enlace** (`0x205DC30` / `0x205E520`) solo `LookAt` (`0x5F3600/0x5F3810`) y
    `UIShowMinimap` (`0x205E520`).

  En una etapa ajena, con S44, esos actores son NPC o nada.
- **Conclusión [V]:** la diferencia entre la vtable del cliente y la común/servidor es la frontera nativa de
  «presentación para este cliente». `Loading` hay que añadirla aparte, porque también la implementa el servidor.

### 2.2 Vía B — la línea de tiempo [V salvo lo marcado]

- **Dónde vive:** la línea de tiempo de una etapa está en el reproductor global **`[0x6D691E0]+0x120`** con el id
  `stage+0x298`. `0x1434DC130(mgr, id)` la busca, y la usan:
  - `0xA9D860`, el inicio, antes de `0x602940`;
  - el tick del gestor `0xAA3E70`, que le pide `+0xDC` («reproduciendo») antes de `0x5DB630` y `0x600220`.

  **Es global del cliente:** no tiene noción de dueño.
- **Inicio (ranura 99) y fin (ranura 101) de los eventos** (`tev.txt`):

  | Evento | Inicio | Qué toca |
  |---|---|---|
  | `SequencerPlayerCameraPreset` | `0xB27880` | ranura 100 `0xB27DE0` → `[0x6D691E0]+0x118` (cámara) |
  | `CameraSelectorEventData` (vtable `0x558B4A8`) | `0x34CFC20` (*thunk* → `.xtls 0x12DBBCA0`) | 100 `0x34CFDE0`, 101 *thunk* `.xtls`. Es la cámara de la cárcel y de la tienda [E] |
  | `SequencerCustomEventLetterBoxControl` | `0xC38F30` / `0xC38F40` → `0xC39050` | bandas |
  | `SequencerCustomEventPostProcess` | `0xC3A440` (y 98 `0xC3A410`) | `call 0x3793EA0` → global `+0x130` |
  | **`SequencerGamePlayEventStartLoadingSequencer`** | **`0xC45740`** | `mov rax,[0x6D691E0] ; rdx=[rax+0x10] ; [rdx+0x1E0] = ev+0x38`: **una marca en el estado de carga P del cliente local**. Ningún veto la cubre |
  | `SequencerAIDialog` | `0xC4D6B0` | diálogo |
  | `SequencerControl_Input`/`_AI`/`_Off`, `ChangeFocusActor`, `SubtitleGroup` | `0xC3A840` → `0xC35790` → `.xtls 0xA534680` → etapa vf21 | control (cerrado por S44) |

- **Despacho [V en instrucciones, E en su generalidad]:** el motor de líneas de tiempo llama a esas ranuras con
  `rcx` = el evento y `rdx` = el contexto desde:
  - `0x377FC03`, `0x377FC2F` y `0x377FC3C` (+0x320) en la función `0x377F93D`;
  - `0x3780474` y `0x37804A0` en `0x37801A0`;
  - `0x3782243` y `0x378226C` en `0x37821A5`;
  - `0x3780F2E` / `0x3780F05` en `0x3780E49`;
  - `0x377A96B` (fin) en `0x377A950`.

  Son ranuras virtuales (sin llamadores directos), con `.pdata`, en `.rsrc`. **Que todas las líneas de tiempo de
  secuenciador pasen por ahí, y solo ellas, es [H].**
- **Que la cámara de pista (`CameraComponent`) se aplique por un evento o la lea el sistema de cámara** cuando la
  línea de tiempo está activa es **[H]**; el código está en `.xtls`.

### 2.3 Control (vf21/vf22) y la rama local [V]

- `0x601470`: `0x205D7C0` (dueño `stage+0x98`) → `desc+0`, y destinos con `0x205DC30`.
- **La rama local** exige a la vez:
  - `[proc+0x38] == [proc+0x1F8]`;
  - `actor->vf178()`, es decir, el usuario del actor es el usuario local (`0x4FE260`);
  - `desc->vf2A8()`.

  Solo entonces usa el control de entrada `[proc+0x200]` (`0x5FA1D0`).
- **Sin actores resueltos** va a la cola de pendientes `0x6075B0`. **No hay ninguna vuelta al jugador local.**
- Con S44 = 1, el nodo del jugador de una escena ajena no resuelve nada. **La familia de control está cerrada** (P2 de
  la regresión: solo guardias y NPC).

### 2.4 ¿Hay una comprobación nativa de «esta escena es del jugador local»? [V]

- **Sobre `stage+0x98`:** sus 20 llamadores resuelven el actor dueño. Algunos leen su usuario (`actor+0xA0`) para
  comprobar que está conectado (`vfC0(4, 0x10)`, `0x206AE30`), pero **ninguno compara al dueño con el jugador local**
  (`near.py`).
- **`stage+0x28C`** (`0x20519F0`: `stage+0x30 == [stage+0x28]+0x98`) es «la etapa principal de su gestor»: relativo al
  gestor, no al cliente.
- **`stage+0x280`** (F1 del coordinador):
  - **Escritor:** solo `0x205F7F0` (`0x205FA1F`), que hace `stage+0x280 = param_3+0x60` (clave de un actor) cuando el
    registro de guardado pendiente apunta a esta etapa y el personaje de la información de etapa (`+0x1E0`, `+0x80`)
    no es uno de los dos por defecto.
  - **Semántica [E]:** «etapa ligada a un personaje jugable concreto C».
  - **Lectores:**

    | Función | Qué hace con `+0x280` |
    |---|---|
    | `Loading 0x5F2A00` (`0x5F2CA9`) | si ≠ 0 y ≠ la clave del jugador local (de `GetLocalPlayer` en `0x5F2A52`), no carga ahora: `proc+0x29B = 1`, diferida |
    | `0x600220` (llamada desde el tick de `0xAA3E70`, `0x6006C8`) | si `proc+0x29B` y `+0x280 ==` la clave del personaje controlado por el **usuario dueño** de la etapa (`[[owner+0xA0]+0xD8]+0x60`, contexto de `0x5DB630`), **lanza la carga diferida** `0x631020` |
    | `PostProcess 0x5F4440` | si ≠ 0, intensidad 0 |
    | `0x5EC510` (procesador `+0x328`, filtra actores por distancia al jugador local) | si ≠ 0, no hace nada |
    | `0x205FCC0` | si ≠ 0, sale |
    | `0x5DF430` | solo actúa si ≠ 0 |
    | `0x5E2110`, `0x5F0780` (`+0xD8`), `0x5FC280` (`+0x2C0`), `0x2045BC0` | no autoenlazan el actor cuya clave es `+0x280` |
    | `0x2043670` (evaluador de condiciones común, caso `0xC`) | condición de etapa «`+0x280 == 0`» (nombre sin identificar, H) |

  - **Veredicto F1:** es relativa al **dueño**, no al cliente. No la lee ningún manejador de cámara, interfaz,
    entrada ni línea de tiempo.
  - **Poner `+0x280` = clave del anfitrión en X:**
    - desactiva el posproceso y `0x5EC510`;
    - cambia la condición `0xC` (rama de etapa distinta, H);
    - y, por `0x600220`, **dispararía en el invitado la carga diferida** cuando el anfitrión controle a ese personaje.

    **Descartada.**
- **F3, bandera por instancia:** las candidatas son los bytes de estado de la etapa:
  - `+0x28B` (iniciada), `+0x28C` (principal), `+0x28D`, `+0x28E` (estado 2/3/5/6), `+0x441`;
  - las de la instancia de línea de tiempo (`+0xDC`).

  Ninguna corta la presentación sin parar también los NPC: `+0x28B = 0` o `+0x441` saltan la etapa entera en el tick
  (`0xAA3E70`) [V]. **No se encontró una bandera de «sin cámara» ni de «sin presentación».**
- **Conclusión:** el motor da por hecho que toda etapa es del jugador local. **La frontera hay que ponerla nosotros**,
  usando la clasificación nativa de §2.1 y §2.2.

---

## 3. Puntos de raíz propuestos

**La regla.** Una etapa es **ajena** si `[stage+0x28]` (G) ≠ M, es decir, es una etapa de X; es el mismo juicio que
`gsr`, S44 y los vetos actuales. En el cliente de una etapa ajena solo corre lo que el motor común y el servidor también
ejecutan. Lo que solo existe para presentar al jugador local no corre.

### 3.1 R-A — Procesador «solo mundo» (vía A): **1 punto**

| | |
|---|---|
| Punto | Entrada de **`0x602940`** (RUNTIME_FUNCTION `0x602940–0x6030C8`, primeros bytes `4C 89 4C 24 20 48 89 54`). Es la sonda P1 de S44, **ya verificada e instalada** (firma de 40 B única, parche de 5 B, sin saltos a `0x602941–44`; `revision.md` §5). Único llamador: `0xA9D91A` (`0xA9D860`, inicio de la escena) |
| Qué hace | Si `[rcx+0x28]` es X (no M) y `[rcx+0x58]` tiene la vtable `0x558C080`: escribe en `[proc]` la dirección de **una vtable propia** (`[-1]` = el COL original y 105 ranuras copiadas de `0x558C080`). En cada ranura donde cliente ≠ `ret`, común = `ret` y servidor = `ret`, pone la del procesador común (`0x359B50`, `ret`); también en `+0x218` (`Loading`) |
| Qué se queda nativo | `LookAt` y `UIShowMinimap` ¿también fuera? (los dos resuelven actores, pero escriben en globales del cliente: van fuera). Se pueden excluir con prudencia `SetCustomMesh`, `DisableCharacterCollision` y `MaterialParameter`: tocan actores de NPC, son del mundo y dejarlas nativas es seguro. Mejor decidirlo con la sonda S-A |
| La tabla | Se calcula **al instalar** con las tres vtables nativas: sin nombres. Se comprueban las firmas de las tres vtables |
| Cubre | interfaz (`UIControl`/tienda, guías, minimapa, pistas, iconos, paneles), entrada (`InputBlock`), cámara de etapa (`SequencerCamera`/`CameraPreset`/`Deactive*`/`ForceLockOn`/`LookAt`), pantalla (`LetterBox`/`FadeInOut`/`PostProcess`), carga de campo de etapa (`Loading`), minijuego, `Craft` y `SetPreviewTarget` |
| No cubre | la vía B (línea de tiempo) y la vía C (funciones de diálogo con código propio) |
| Efectos colaterales | las «End» quedan también en `ret`, simétricas. Las ranuras internas (`0x00..0x30`, `0x2A8..0x340`: tick, condiciones, enlace) quedan idénticas |
| Riesgos | (1) **Escribe** el puntero de vtable de un objeto del juego. La regla «nunca parches de vtable» de `CLAUDE.md` es para el *overlay*, pero el espíritu pide **acuerdo del usuario**. (2) La tabla tiene que vivir mientras exista el procesador: no hay que descargar el módulo, o hay que restaurar los vptr al cerrar. (3) Hay que comprobar que ninguna comprobación de tipo de `.xtls` compara el vptr del procesador con `0x558C080` (H; mirarlo con la sonda S-A). (4) Las funciones de etapa que corran **antes** de `0x602940` (en la carga, `0xA90E80` → `.xtls`) no quedarían cubiertas (H; S-A lo cuenta) |
| Riesgo global | Medio: una sola escritura por etapa, en su inicio, con todo verificado y reversible (se puede restaurar el vptr original) |

**Variante sin escritura (R-A′)**, por si el usuario veta el cambio de vptr: mantener los ganchos por manejador, pero
generados desde la misma tabla nativa. Así se pasa de los 28 actuales a todas las ranuras de solo cliente, unas 35, cada
una con `create_mid`/entrada verificada. Es más superficie y es justo lo que el encargo quiere evitar.

### 3.2 R-B — Despacho de eventos de línea de tiempo (vía B): **6 puntos mid**, pendientes de sonda

| | |
|---|---|
| Puntos | `0x377FC03` (`FF 90 18 03 00 00`), `0x377FC2F` (`41 FF 91 18 03 00 00`), `0x377FC3C` (`41 FF 91 20 03 00 00`), `0x3780474`/`0x37804A0`, `0x3782243`/`0x378226C`, `0x3780F2E` (inicio), `0x3780F05` y `0x377A96B` (fin). Todos dentro de RUNTIME_FUNCTION de `.rsrc`. **Sin verificar** el tramo de instrucciones completas sin entradas: por ejemplo, `0x377FBFA` es destino de salto, así que el *mid* tiene que empezar justo ahí o en la propia `call` |
| Qué hace | Si el evento (`rcx`) es de una clase de «presentación» y su línea de tiempo pertenece a una etapa de X, salta la llamada (como si el evento no estuviera) |
| Clasificación de la clase, al instalar | ranura 99 o 101 que acaba en una función que lee `[0x6D691E0]` y no resuelve actor (`PlayerCameraPreset`, `CameraSelector`, `LetterBoxControl`, `PostProcess`, `StartLoadingSequencer`, `AIDialog`, `Subtitle`…), **frente a** las que van a la etapa o al actor (`Control_*`, `ChangeFocusActor`, gimmicks, efectos de golpe…). Se puede generar desde `tev.txt` con un criterio de código, no de nombre |
| [H] crítico | (1) Cómo saber de qué etapa es la línea de tiempo desde `rdx`. Pistas: `0x34CFDE0` recorre `[[ctx+0x58]+0x10]` → `+0x98` (cadena de nodos padre) → raíz `[+0x1E0]`, y `[[ctx+0x68]+0x68]`; hay que comparar con `stage+0x298` de las etapas de X, que se apuntan en P1. (2) Si la cámara de pista se apaga al saltar `CameraSelectorEventData` y `PlayerCameraPreset`, o necesita un corte en el sistema de cámara. (3) Que estos sitios no sirvan también a otras líneas de tiempo del juego (gráficos de acción) en caliente: el coste de la comprobación tiene que ser una carga atómica |
| Riesgo | Medio-alto hasta medirlo: es un camino caliente y genérico |

**Alternativa a R-B** si la sonda no encuentra la etapa desde el contexto: un gancho por cada inicio de evento de
presentación (≈8 entradas de `.rsrc`, más los *thunks* de `.xtls` de 5 B, como ya hace `guest_x_veto`). El contexto de
etapa se tomaría igual que en vf21: `0xC35790` → `.xtls` recibe la etapa, a medir.

### 3.3 Vía C (funciones de diálogo)

- Siguen los ganchos de `guest_x_ui_veto` para `TimelineEvent` (vetable), `SetInteraction` (permitido a propósito:
  marcas de interacción de los NPC, que son del mundo), `BlockInteraction` y `SetQuestDialogAICategory` (*thunks* a
  `.xtls`, solo probe).
- No son ranuras del procesador, así que R-A no las toca. Sus efectos en el invitado son menores (bloqueo de
  interacción y categoría de diálogo de la IA) [E].

### 3.4 Qué piezas del mod quedarían redundantes

| Pieza | Con R-A | Con R-A + R-B |
|---|---|---|
| `guest_x_ui_veto`: `InputBlock`, `UIControl`, `ChangePhasePanelTag`, `UIShowMinimap`, `UIFindPath`, `UIStageIcon`, `UIStageGuide`, `UIStageBackgroundGuide`, `UIHint`/`End` | **redundante** | redundante |
| `guest_x_ui_veto`: `TimelineEvent`, `SetInteraction`, `BlockInteraction`, `SetQuestDialogAICategory` | sigue | sigue |
| `guest_x_camera_veto` (13 de cámara y pantalla) | **redundante** | redundante |
| `guest_x_stage_veto` V1 (`0x5F2A3A`) | **redundante** (`Loading` en `ret`) | redundante |
| V0 (`0x778AB0`, red de seguridad) | se queda como red | se queda |
| `guest_seq_route_deny=8CD771B1` | redundante si su recarga va solo por `Loading` (S9 §7.2 [E]). Si además usa `StartLoadingSequencer` (`P+0x1E0`), hace falta R-B | **redundante** |
| `guest_seq_foreign_player` (S44, enlace del dueño) | **se queda**: cierra control, alineación y `0x205DC30` | se queda |
| `guest_x_interaction_stage` | aparte (interacción con NPC); no se toca | — |

---

## 4. Sondas de solo lectura, antes de escribir nada

| # | Punto | Lee | Para qué |
|---|---|---|---|
| S-A | P1 `0x602940` (ya instalada) | `[stage+0x58]`, su vptr (`== 0x558C080`), `[proc+0x10] == stage`, `[proc+8] == [stage+0x28]`, `stage+0x298`; la tabla «solo mundo» calculada (se escribe una vez) | Que todas las etapas de X tienen el procesador esperado, y guardar el `+0x298` para S-B |
| S-A2 | los 28 manejadores ya enganchados en probe | contar llamadas de X **antes** del P1 de su etapa (comparando con el instante de P1) | ¿Corre alguna función de etapa antes de `0x602940`? (riesgo 4 de R-A) |
| S-B | los 6 sitios de despacho (§3.2), *mid* de solo lectura | vptr del evento (clase), `rdx`, y las cadenas `[[rdx+0x58]+0x10]`, `[rdx+0x68]` y la raíz; contar por clase y por «línea de tiempo de X» (si `+0x298` coincide) | Encontrar el vínculo contexto → etapa; ver que pasan `CameraSelector`, `PlayerCameraPreset`, `LetterBox` y `StartLoadingSequencer` de la tienda y de la cárcel; medir el coste (llamadas por segundo) |
| S-C | entrada de `0xC45740` (`StartLoadingSequencer`, 0x13 B: hay que verificar su `.pdata` y su firma) | `ev+0x38` y P+0x1E0 antes de la escritura | Si `0x8CD771B1` usa este camino además de `Loading` |
| S-D | estado de cámara en el invitado (`[0x6D691E0]+0x118/+0x120`) | cuál es la cámara activa durante la tienda o la cárcel | Confirmar que la cámara viene de la línea de tiempo de X (E de §1.2) |

Todo con lecturas SEH, atómicos y anillo; las líneas las escribe el hilo de diagnóstico. Nada por fotograma salvo S-B,
que tiene que ir con muestreo.

---

## 5. Tabla de verdad (invitado; «hoy» = S44 = 1 y vetos actuales)

| Caso | Escena (ejemplo) | Vía que toca el cliente local | Hoy | Con R-A | Con R-A + R-B | Efectos de mundo que se conservan |
|---|---|---|---|---|---|---|
| Tienda del anfitrión | `cd_seq_funcnpc_smithy` `0xA7132561` | B (cámara de pista, `PlayerCameraPreset`, fundido); A (`UIControl`/`InputBlock` vetados, `ChangePhasePanelTag` nativo); C (`BlockInteraction`, `SetQuestDialogAICategory`) | **se ve una escena de tienda vacía** (cámara) [V/E] | se quitan paneles e interfaz; **la cámara sigue** | no salta nada | el herrero (nodo `0x20000008`) y sus gestos; `SetInteraction` |
| Cárcel | `..._wakeup_prison_hernand_0100` `0xE5B490F1` | B (`CameraSelector`, cinemática, fundidos); A (`UIStageIcon` probe) | **cinemática completa** [V] | igual que hoy, salvo el icono | sin cinemática | guardias |
| Recuerdo (Matthias, memoria) | `cd_seq_memory_hernandgib` `0xF2A314F0` (6/3), `boss_rematch_matthias` `0x9586FFA4` (2/2) | A (`UIStageGuide`, `BlockInteraction`); B (cámara/`Talk`) | guías vetadas; `BlockInteraction` nativo; cámara según `.paseq` (H) | guías y cámara de etapa fuera | fuera | Matthias, gimmick |
| Carga de facción | `0x8CD771B1` | A (`Loading`); quizá B (`StartLoadingSequencer`, H) | denegada por lista (`guest_seq_route_deny`) | `Loading` en `ret`: se puede quitar la lista si S-C da 0 | se puede quitar la lista | — (la escena entera es de carga) |
| Tiempo | `cd_seq_contents_weather_*` | A (`UIStageGuide`, vetado) | sin efecto visible | igual, sin gancho | igual | clima: corre por SetPhase/tiempo, común [E] |
| Fauna | `cd_seq_spawn_auto_*` (10/1) | A (`UIStageGuide`) | sin efecto | igual | igual | animales (`SpawnCharacter`, Pivot) |
| Escena del mundo con NPC | `funcnpc_*` en reposo, `minigame_*_base` | A (`SetInteraction`, guías) | marcas de interacción nativas | igual (`SetInteraction` es vía C, se queda) | igual | NPC animados, marcas |
| Escena **propia** del invitado | su tienda (`0x143`), `use_sealedartifact` (`0x1A3`), entrada (11 controles) | todas, **a propósito** | nativas (en M) | **sin cambio**: R-A solo toca etapas con G ≠ M | sin cambio | todo |

---

## 6. No demostrado

- **Cámara de la línea de tiempo:** si la de pista (`CameraComponent`) se aplica por un evento despachado en los sitios
  de §2.2, o si la lee el sistema de cámara mientras la línea de tiempo está activa (`.xtls`). Es lo que decide si R-B
  basta.
- **Contexto de los eventos:** cómo llevar `rdx` del despacho a la etapa (o a `stage+0x298`).
- **Funciones de etapa antes del inicio:** si alguna corre antes de `0x602940` (cubre la sonda S-A2).
- **`.xtls` y el procesador:** si algo en `.xtls` compara el vptr del procesador o usa `typeid`/`dynamic_cast` sobre él.
- **Lo que queda sin identificar:** la condición `0xC` de `0x2043670`, el papel de `0x5DF430` y el de la ranura `+0x328`
  `0x5EC510` (distancia al jugador local, con la guarda `+0x280 == 0`).
- **El camino de `0x8CD771B1`:** si usa solo `Loading` o también `StartLoadingSequencer`.
