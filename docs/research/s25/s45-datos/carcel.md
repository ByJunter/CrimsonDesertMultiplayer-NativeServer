# S45 — La cinemática de la cárcel del anfitrión en la pantalla del invitado

Build 1.0.0.2976 (PE `0x6AB28F00`). Investigación de solo lectura. Datos: `C:\Emulador CD\backups\p8-2inst-s44-regresion\`
(`guest.log`, `host.log`, `cdmp_packets.guest_20261015-234655.*`, `cdmp_packets.host_20261015-234637.*`, inis). Código
de referencia: `C:\Emulador CD-2inst` (`426d2e3`). Scripts y salidas: `scratchpad\s45\carcel\` (abajo, §7).

Marcas: **[V]** visto en log, captura, datos del juego o código; **[E]** inferencia fuerte; **[H]** hipótesis por medir.

> Nota de identidad: en esta sesión el invitado es el personaje **`0xA0100067`** (usuario `0x90100066`), no
> `0xA0100066`/`0x90100065` (`GuestSeqForeign … this client 0xA0100067`, `GuestSeqRestore … local user 0x90100066`). El
> anfitrión sí es `0xA0100001` / `0x90100000`.

---

## 0. Resumen

1. **La escena que vio el invitado es `cd_seq_quest_crime_police_wakeup_prison_hernand_0100`** (recurso `0xE5B490F1`,
   secuenciador `0x30A`, etapa `0x170555C`, la misma clave que el `ReviveLoadingAck` de la cárcel). Es la escena personal
   del anfitrión al «despertar» en la celda tras ser abatido por los guardias. **Le llegó por la copia de escenas
   (`observer_seq_copy`)** [V]: no hubo paquete propio de arresto o cámara para el invitado, ni carga, ni `Revive*`, ni
   `Wanted*`, ni teletransporte (§1).
2. **El mod hizo lo previsto por S44** [V]: `ROUTED` a X, creada (`0xA9CA40` → 0), el enlace del dueño
   neutralizado (`written 1/1`, P1 `link 0`), control de secuenciador solo a los tres guardias, P3 y P4 a cero, y la
   copia del anfitrión sin alinear. **Pero la escena es una cinemática pura**: `Timeline.set_cutScene(True)`,
   `set_exclusive(True)`, cámara propia (`CameraComponent` + `CameraSelectorEventData` en la pista global), fundidos y
   `IsCameraJump(FadeOut/LetterBox)` [V en el `.paseq`/`.pastage`]. Al reproducirla el tick de X
   (`guest_seq_play`), la cámara del cliente del invitado pasó a la de la escena.
3. **Ninguno de los 28 manejadores de `guest_x_*` vio la cámara** [V]: para `0x30A` solo pasaron `UIStageIcon`
   (probe, no vetable) y `UIStageGuide` (vetado). Los contadores de las familias de cámara (`SequencerCamera`,
   `LetterBox`, `FadeInOut`, `CameraPreset`, `LookAt`…) siguen **a 0** en X hasta las 23:50:43. Es el riesgo **G-cam**
   de `p8-s9-cinematica.md` §7.6, ahora visto: la cámara de la línea de tiempo no pasa por las funciones de etapa.
4. **Personaje del invitado** [V]: no se movió ni se teletransportó. Ningún paquete del servidor iba a él, y P2 nunca
   dio control a `0xA0100067` en esta escena. **Pero** [E]: su movimiento se paró durante toda la cinemática (sin
   `MoveActorReq` propios de 23:50:28,8 a ~23:50:41,8) y entró en una acción en bucle (`0x4564B267`, cada 2,000 s), que
   **no aparece en ningún otro momento de la sesión**. Es compatible con que el modo cinemática le quitara la entrada,
   pero no está demostrado (§1.4).
5. **Nativo:** el servidor manda esa escena solo al dueño (`ObserverStage … via ToTarget`) [V]. Un tercer cliente no
   la crea, así que no hay cámara, ni modo cinemática, ni bloqueo de entrada. **Lo que la deja pasar es la cadena del
   mod** `observer_seq_copy` → `guest_seq_route` (a X) → `guest_seq_play` (tick de X). Los vetos solo cubren funciones
   de etapa, y S44 solo toca el enlace del dueño (§2).
6. **Arreglo de raíz propuesto** (§3): `guest_seq_foreign_present = 0 | probe | 1`. Una etapa de X con dueño ajeno (el
   mismo juicio que S44) **no aplica su presentación global en este cliente**: cámara de secuenciador, modo
   cinemática/exclusivo, fundidos/bandas y entrada local. Sus pistas de NPC siguen. El punto exacto del motor **no está
   localizado** (la lógica está en `.xtls`), así que el paso 1 es una sonda de solo lectura que lo encuentra. Como
   parche temporal, por lista: `guest_seq_route_deny=8CD771B1,E5B490F1`.

---

## 1. Cronología (23:48:30–23:52:30)

Relojes: invitado `pared = t + 23:47:10,782`, anfitrión `pared = t + 23:46:55,505` (de las parejas `qpc … capture t=`
del log). Las horas `~` son las del QPC, no las de escritura.

### 1.1 Antes del arresto

| Hora | Lado | Qué |
|---|---|---|
| 23:48:30–23:48:40 | inv. | Carga del invitado; recibe del anfitrión el tiempo (×4) y `reward_spear_base`, todo neutralizado |
| 23:49:10–23:49:32 | inv. | Llegan las escenas de la zona del anfitrión: `funcnpc_*` (12 tiendas), minijuegos, `quest_hernand_2400`, `boss_rematch_matthias`… Todas `ROUTED`, `[foreign] NEUTRALIZED` |
| 23:49:56,3 | — | El usuario pasa a la ventana del **anfitrión** (BgSpeed) |
| 23:50:07,7 / 09,4 | anf. | `ChangeWantedStateAck 2295` (solo al anfitrión; el invitado no lo recibe) |
| 23:50:11,47–11,52 | inv. | 2661 `cd_seq_quest_crime_police_blockroad_0100` (`0x307`), `_0200` (`0x308`) y `cd_seq_quest_crime_police_hernand_0500` (`0x309`): guardias que cortan calles. Neutralizados; P2 solo a NPC |
| 23:50:15–20 | anf. | Combate con los guardias (`SetCrimeTargetReq`, `WantedAddWitnessActor*`, caída de HP). El invitado recibe los `VaryHPAck` del anfitrión (difusión normal) |

### 1.2 El arresto y la cinemática

| Hora | Lado | Qué |
|---|---|---|
| 23:50:20,84 | anf. | `ReviveLoadingReq 2362` (tipo 3) → `ReviveLoadingAck 2993` `03 5C557001` (etapa `0x170555C`) → `LoadingFieldData 2643` en la cárcel (-10729,8; 622,3; -3835,9) |
| 23:50:20,99 | inv. | 19 × `2810` de las escenas viejas del anfitrión (copiados) y `2444` |
| 23:50:21,6–25,9 | inv. | `RemoteMotion divergence #2`: la copia del anfitrión va 69 m por detrás del canal y salta a la cárcel. Es la red, no la escena |
| 23:50:22,15 | inv. | `ReviveAck 2475` del anfitrión (tipo 3). Difusión del servidor; nada que ver con la escena |
| 23:50:22,15–25,53 | anf. | `ReviveAck`, `ChangeWantedStateAck`, `UpdateMissionListAck`, `RemoveItemToInventoryAck` ×2 (multa / máscara), `ClearWantedAck 2543`. **Solo al anfitrión** |
| **23:50:24,1** | — | El usuario pasa a la ventana del **invitado** (BgSpeed), donde sigue hasta 23:50:49,6 |
| **23:50:25,537** | inv. | **2661 `0x30A` `cd_seq_quest_crime_police_wakeup_prison_hernand_0100`**: `kind 4`, dueño `A0100001`, `u17 0001`, enlaces `20000002→A0100001/5`, `20000007→B0101050/5`, `20000004→B0101104/5`, `20000005→B0101105/5`, más su 2981 de estado 0. Anfitrión: `ObserverSeqCopy [copy] … SENT to user 2's queue` |
| 23:50:25,549 | inv. | `GuestSeqRoute … ROUTED to the actor's own manager X`; `GuestStage [2661 result] … created`; `GuestSeqForeign [foreign] … owner's binding NEUTRALIZED (type -> 0) … written 1/1` |
| 23:50:25,58–26,32 | inv. | El resto de escenas del anfitrión tras su carga: `seotda`, tiempo ×4, **`funcnpc_wantedfence`**, **`quest_progression_call_0100` (`0x404`)**, `armwrest`, `timeattack`, tiendas, `quest_hernand_2400`, `goblinbroker`. Todas neutralizadas |
| 23:50:26,03 | anf. | `PlayAllPreLoadSequencerAck 2322` del anfitrión. **No** se copió (el único 2322 copiado es el de las 23:49:10) |
| **23:50:27,666** | inv. | **P1 `0x602940`** de `0x30A`: `FOREIGN, owner's binding at link 0 (neutralized)`; actores: nodo `20000002` tipo 0 (CurrentControl) dueño **link 0**, y los guardias `20000007/04/05` tipo 2, link 5 |
| 23:50:27,691–692 | inv. | P2 vf21 → `0xB0101104` y `0xB0101105` (*NPC / gimmick only*); `GuestXVeto [interface] WOULD VETO UIStageIcon` (no vetable: corrió) y `VETOED UIStageGuide` |
| 23:50:28,55 | inv. | P1 de `0x404` (`quest_progression_call_0100`): también lleva cámara, pero se queda esperando a su NPC (`AITask`) |
| **~23:50:28,8** | inv. | **Se paran los `MoveActorReq` propios** (≈40/s hasta t=197; 3 en t=198; **ninguno** en t=199–208) |
| 23:50:29,29 | anf. | El anfitrión empieza su acción de cinemática `0xF9A278B5` (hasta 23:50:43,93). Su `LoadingCompleteReq` llega a las 23:50:30,22: **el invitado empezó la cinemática ~2 s antes que el propio anfitrión** |
| 23:50:30,64 → 31,33 | inv. | Acciones propias del invitado: `0x25DC7FB9 → 0xBCC6BD59 → 0xEA356409 → 0x4564B267` |
| **23:50:30,825** | ambos | **MARK 1** (Ctrl+F12) |
| 23:50:31,3–43,2 | inv. | `0x4564B267 → 0x4564B267` cada **2,000 s** (seis veces). Esta acción solo aparece aquí en toda la sesión |
| 23:50:32,55 / 38,17 | inv. | P2 vf21/vf22 de `0x30A`, solo a los guardias |
| **~23:50:41,8** | inv. | P2 vf22/vf21 de `0x30A` sobre `0x20000007` (fin de la línea de tiempo [E]); **vuelven los `MoveActorReq` propios** (10 en t=211, 39 en t=212) |
| 23:50:43,18 → 43,91 | inv. | `0x4564B267 → 0xB1A755E7 → 0x26107D3C` (la espera normal, `WAIT`) |
| 23:50:43,93 | inv. | 2524 `0x30A` (`WOULD route to X (probe)`, clase WORLD, se queda nativo) + 2810 `0x30A` (`ROUTED`): se borra de X. `UIStageGuide` vetado de nuevo |
| 23:50:55,4–57,7 | inv. | `cd_seq_memory_hernandgibbet_0001` (`0x469`, `u17 0306`, `set_cutScene(True)` en sus datos): 2,3 s de vida, solo NPC. El usuario no dijo nada |
| 23:51:02,0 | inv. | 2810 de `0x404`: `GuestXVeto [camera] WOULD VETO (probe) SequencerCamera` (el único de cámara en este tramo, al borrarse) |
| 23:51:01–23:52:30 | inv. | Revanchas de Matthias, tiendas y `quest_hernand_2400`, todas neutralizadas. Sin `P2 … this client's character` |

### 1.3 Qué hizo el mod con `0x30A` [V]

- `GuestSeqRoute`: 2981 y 2661 `ROUTED` a X `0x3A53A33F300`. 2810 `ROUTED`. 2524 `WOULD route (probe)`, que es lo
  previsto con `guest_seq_route_2524=probe`.
- `GuestSeqForeign [foreign]`: `NEUTRALIZED`, `written 1/1`, copia sin el bit `0x200`. P1 confirma `link 0` en el nodo
  CurrentControl. P2: 9 llamadas, todas `control to NPC / gimmick only` o `nothing resolved`. P3 `none`, P4 `none`.
- `GuestXVeto`, solo dos manejadores: `UIStageIcon` (WOULD, corre nativo) y `UIStageGuide` (VETOED).
  - Resumen de las 23:50:43: `SequencerCamera`, `LetterBox`, `FadeInOut`, `CameraPreset`, `LookAt`, `ForceLockOn`,
    `PostProcess` y `DeactiveSequencerCamera*` **no tienen ni una llamada** en X.
  - `InputBlock` tiene `X 0 / M 2` (las dos de M son de la entrada en el mundo, 23:48).
  - `limited 46 lost 0`: los contadores son exhaustivos aunque se recorten líneas.
- `GuestStage`: creada (`0xA9CA40` → 0x0), sin fallos.
- `ObserverSeqCopy` (anfitrión): `[copy] … SENT to user 2's queue 2661 (140 bytes) after the 2005 of its 4 bound
  actor(s)`, y el 2810 también.

### 1.4 Qué le pasó al invitado

- **Posición y teletransporte** [V]:
  - `Local: pos` continuo (-10668,7 → -10673,6 → -10673,9), sin saltos;
  - ningún `LoadingFieldData`, `ReviveLoading`, `Revive*` ni `2223`/`StartActionAck` del servidor para `0xA0100067`;
  - los únicos S>C con su clave son de gimmicks, diálogos, `ApplyBuff`/`UnapplyBuff` (`0x128E7`/`0xF4265`, que salen
    toda la sesión al moverse) y `DataDefinedRegenerate`.
- **El servidor no lo «arrestó»** [V]:
  - sin `Wanted*`, `ChangeWantedState`, `ClearWanted` ni `RemoveItemToInventory` para él;
  - sin C>S de arresto ni de objetos: los `Player.set_clearWanted(Faction_Celeste_Hernand)` y
    `Player.set_deleteItem_onEnter_0(Kliff_mask, All)` de la escena no produjeron tráfico desde el invitado.
- **Control** [E]:
  - movimiento cortado desde ~0,9 s después del P1 hasta el fin de la línea de tiempo (~23:50:41,8, no el 2810 de
    las 23:50:43,9);
  - en medio, la acción en bucle única `0x4564B267`. La cadena `0xBCC6BD59 → 0xEA356409` también aparece a las
    23:55:14,8 (marca 3) seguida de otras acciones: parece un prefijo común, no exclusivo de las cinemáticas;
  - esto encaja con «modo cinemática del cliente», pero también con que el usuario dejara de pulsar al ver la cámara
    cambiar. Sin una sonda de entrada o cámara no se puede separar. El nombre de las acciones no está en
    `names.json` (no son rutas de archivo).
- **Cámara**: la del jugador (se mostró toda la escena) [V en el informe de la prueba; el log no tiene sonda de
  cámara]. La ventana del invitado tenía el foco de 23:50:24,1 a 23:50:49,6, que cubre toda la vida de `0x30A`.
- **Interfaz**: `UIStageGuide` vetado, `UIStageIcon` corrió [V]. Si la cinemática ocultó el HUD o puso bandas, fue por
  otro camino [H].
- **La copia del anfitrión en el invitado**: S44 funcionó [V]. Llegó a la celda por la red (divergencia #2), no por la
  escena (P3 `none`, enlace a 0).

### 1.5 Por qué es esta escena y no otra [V/E]

- Es la única escena que empieza en esa ventana con cámara propia **activa desde el principio**. Su `.pastage` solo
  tiene `SCENE_0` y `Terminate`, sin interacción que abra la rama.
- Su `.paseq` (44 894 B, extraído a `data\`) contiene `CameraComponent`, `CameraSelectorEventData`,
  `CameraParameterPresetEventData`, `CameraEffectComponent`, `camera_1`/`camera_2`, `_hasSequencerCamera`,
  `_screenFadeInOut`, `Timeline.set_cutScene(True)` y `Timeline.set_exclusive(True)`. **No** tiene
  `SequencerControl_Input` (no hay nada «del jugador» que cortar por enlace).
- `quest_progression_call_0100` (`0x404`) también tiene cámara, pero espera a un NPC. Su único `SequencerCamera` salta
  al borrarse, a las 23:51:02.
- `memory_hernandgibbet` duró 2,3 s, 25 s después de la marca.

---

## 2. Nativo frente al mod

### 2.1 Qué haría el juego nativo [V salvo donde se indica]

- El servidor manda el 2661/2981/2524/2810 de `0x30A` **solo al dueño**: en el anfitrión,
  `ObserverStage [2661 PrepareSequencerAck] … -> actor 0xA0100001 user 0x90100000 via ToTarget (session …)`.
  - Un tercer cliente nunca crea la etapa: no hay cámara de secuenciador, ni modo cinemática o exclusivo, ni fundido.
  - Tampoco le llegan `Wanted*`, `ClearWanted`, `RemoveItem` ni `LoadingFieldData` del dueño.
- Lo que sí recibe un tercer cliente son las **difusiones**: `VaryHPAck`, `ReviveAck 2475` (llegó al invitado), la
  desaparición y reaparición del personaje en la celda (`RemoveActor`/`ActorLoadData`) y los guardias como NPC
  normales (`2005`).
- Por eso el motor no comprueba nunca **de quién** es la etapa al activar su cámara o su modo cinemática: no le hace
  falta [E]. Es el mismo caso que S44, que el nativo nunca produce: un cliente con la escena personal de otro.
  - La única noción nativa de «etapa de otro usuario» que se conoce es `stage+0x280`, que lee `Loading`
    (`p8-s9-cinematica.md` §7.2).

### 2.2 Qué pieza del mod la deja pasar

| Pieza | Papel | Evidencia |
|---|---|---|
| `observer_seq_copy=1` (anfitrión) | Copia el 2661 vivo `0x30A` (y su 2981/2810) al invitado | `ObserverSeqCopy [copy] … SENT` [V] |
| `guest_seq_route=1`, `guest_seq_route_deny=8CD771B1` | La lleva a X y la crea. La lista no tiene `E5B490F1` | `ROUTED`, `created` [V] |
| `guest_seq_play=1` (`live=0`) | Ejecuta `0xA953C0(X, Q, mode)` cada fotograma (4 412 ticks a las 23:50:43): **la línea de tiempo se reproduce entera**, con su pista global (selector de cámara, cinemática, fundidos) | resumen `GuestSeqPlay` [V]; que la cámara sale de ahí es [E] |
| `guest_x_stage_veto=1` (V1 Loading) | No interviene: no hubo `Loading` en X | `V1 … verdicts: none` [V] |
| `guest_x_camera_veto=probe` | **No ve la cámara**: 0 llamadas de la familia cámara. Con `1` tampoco habría cambiado nada | resumen por manejador [V] |
| `guest_x_ui_veto=1` | Vetó `UIStageGuide`. `UIStageIcon` no es vetable | [V] |
| `guest_x_ui_veto_allow=SetInteraction` | Irrelevante: `0x30A` no tiene `SetInteraction` | [V] |
| `guest_seq_foreign_player=1` (S44) | Cumplió: el dueño queda fuera (link 0, sin control ni alineación). **Por diseño no cubre** cámara, interfaz ni carga (S44 §8) | P1/P2/P3/P4 [V] |
| Los 11 P2 «control to this client's character» | **No tienen que ver**. Son escenas **propias** (no ajenas: `guest_seq_foreign.cpp:316` solo escribe líneas de las ajenas) y ocurren todas antes de las 23:49:43 (7+6 ya a las 23:48:43, durante la entrada en el mundo). No sube ninguno después. En `p8-2inst-s44-probe` son también 11 y del mismo tipo [E: mismas escenas de entrada] | resúmenes `GuestSeqForeign` [V] |

**Conclusión:** S44 corta lo que la escena hace **al actor del dueño**. Las vetos cortan lo que hacen **las funciones de
etapa**. Lo que queda sin cubrir es la **presentación global de la línea de tiempo** (cámara, modo cinemática o
exclusivo, fundido), que el motor aplica al cliente local sea cual sea el gestor que reproduce la etapa.

### 2.3 Cuánto afecta: otras escenas con la misma marca [V en datos, E en efecto]

Tabla `markers.tsv` (89 `.paseq` de escenas del anfitrión vistas en el inventario y en esta sesión).

| Grupo | n | Ejemplos | Riesgo en el invitado |
|---|---|---|---|
| Cámara propia + `set_cutScene(True)` | 24 | `crime_police_wakeup_prison_*`, `faction_*_loading`, `quest_hernand_1200/1300`, `quest_demeniss_0200`, `quest_splithorn_boss_0100`, `quest_propagation_information_0100`, `node_her_*_nature_call_0100`, minijuegos, `funcnpc_craftsmith/wantedfence` | Alto si la cinemática corre desde el principio (cárcel, carga); en tiendas y minijuegos solo en la rama que abre el dueño |
| Cámara propia, cinemática condicional | 4 | `quest_progression_call_0100` (`set_cutScene(catch)`), `quest_hernand_2400`, `quest_demeniss_south_2100/2200` | Cuando se cumple la condición |
| `set_cutScene(True)` sin cámara propia | 10 | `memory_*` (×3), `funcnpc_bank/clothes/general/pub/contribution`, `contents_wanted_her_0010`, `node_her_bluemontmanor_0300` | Modo cinemática o entrada [H] |
| Nada de eso | 51 | tiempo, fauna, `seotda`, `blockroad`, `hernand_0500`, `reward_spear_base` | Ninguno |

Una lista de denegación no escala: los recuerdos (`memory_*`) y las misiones con cámara pasarán igual.

---

## 3. Arreglo de raíz propuesto (coherente con S44)

**Principio** (el de S44, ampliado): en el cliente del invitado, una escena cuyo dueño es otro jugador no toma **la
cámara, la entrada, la interfaz ni el personaje** de este cliente. Sus NPC y gimmicks siguen animándose. La decisión es
por etapa, con el mismo juicio que S44 (`gsf::judge_bind`: dueño = personaje `0xA…` distinto del propio, y la etapa
está en X). No se usan nombres.

Clave nueva, solo invitado: **`guest_seq_foreign_present = 0 | probe | 1`** (por defecto `0`). Necesita
`guest_seq_route=1` y `guest_seq_play=1`.

### 3.1 Puntos verificados en los que se apoya [V]

- **El juicio por etapa ya existe**: el gancho mid de `0xC0612A` (`guest_seq_route`) y el anillo secuencia → recurso
  (`gsf::SeqResRing`).
  - P1 en la entrada de `0x602940` (verificada: `.pdata`, firma de 40 B) recibe la etapa en RCX: `stage+0x30` = clave,
    `[stage+0x48]+0x1B8` = nombre, `stage+0x98` = dueño (`0x205D7C0`).
  - Sirve para fijar «esta etapa es ajena» en el arranque.
- **El tick de X es nuestro**: `guest_seq_play` llama a `0xA953C0(X, Q, mode)` desde el gancho de bucle principal
  `0x8B6144` (verificado al instalar). Se puede poner un contexto de hilo «dentro del tick de X» alrededor de esa
  llamada, el mismo patrón que V0 (`p8-s9` §7.4).
- **Los 28 manejadores de etapa** ya están enganchados (`guest_x_veto`, `gxv::judge` por gestor) y se quedan como
  están.
- **Cámara del cliente**: el manejador `SequencerCamera` (`0x5EE250` → `.xtls 0x8F285C0`) usa el gestor de cámara
  `[0x6D691E0]+0x120` y escribe `[[0x6D691E0]]+0xDE8 = 0` (`p8-s9` §7.3, [V est]). Es el único ancla de cámara
  conocida.
- **Datos de la escena**: `CameraSelectorEventData` (vtable `0x558B4A8`), `ConditionData_ExistSequencerCamera`
  (`0x5958610`), `ConditionData_ExclusiveStagePlaying` (`0x594F728`) y `ConditionData_IsCameraJump` (`0x594B1B0`)
  existen en la RTTI.
  - Sus evaluadores son trampolines a `.xtls` (`0x22739A0` → `0xE8CB700`; `0x34CFC20` → `0x12DBBCA0`): la lógica no
    se puede leer en `.rsrc`.

### 3.2 Paso 1 — `probe` (solo lectura; localiza el punto)

**P5a, muestreo diferencial del estado de presentación del cliente**, en el gancho `0x8B6144` ya existente. Una vez
por fotograma, con SEH, copiar ventanas fijas y registrar solo los cambios:
- `[[0x6D691E0]]` (0x1000 B) y el objeto `[[0x6D691E0]+0x120]` (0x400 B), anotando el RVA de su vtable;
- el `+0xDE8` conocido;
- 0x200 B del componente de control del personaje local (el `CC` que ya lee `GuestBodyAnim` para los NPC).

Cada línea lleva las etapas de X que empezaron en los 2 s anteriores (del anillo de P1). Así queda medido [V] qué
campos cambian al empezar y al acabar `0x30A` (modo cámara, cámara activa, bandera de cinemática, bloqueo de entrada).
El coste es una comparación de unos 6 KB por fotograma, sin llamar al juego.

**P5b, ¿síncrono con el tick de X?** Un contador de los cambios de P5a vistos *justo después* de la llamada a
`0xA953C0(X…)`, comparado con los vistos en el resto del fotograma. Si el cambio se produce dentro del tick de X, el
escritor está en la reproducción de la línea de tiempo de X y un veto con contexto de hilo basta. Si llega diferido,
hace falta el veto en la etapa.

**P5c, acción y entrada del propio personaje**: una línea por cada cambio de acción de `0xA0100067` (las capas
L0/L1, como hace `GuestBodyAnim`), para separar «el usuario dejó de pulsar» de «el cliente le quitó la entrada».

**Después, en estático**: con el RVA de la vtable y los offsets que salgan de P5a, buscar en `.rsrc` las entradas que
escriben esos campos (son trampolines a `.xtls`; el punto de veto debe ser la entrada en `.rsrc`, nunca a mitad de
función).

### 3.3 Paso 2 — `1` (el arreglo), por orden de preferencia

| Variante | Qué hace | Cuándo | Estado |
|---|---|---|---|
| **F1, nativa** | En P1, para una etapa ajena de X, escribir `stage+0x280 = usuario del dueño` (`0x90100000`), la marca nativa de «etapa de otro usuario» | Si el estático muestra que cámara, cinemática o exclusivo comprueban `+0x280` como `Loading` | [H]: solo `Loading` la lee, que se sepa |
| **F2, veto en la entrada** | Gancho en la entrada en `.rsrc` de la función que activa la cámara o el modo cinemática de una etapa (la que salga de P5a/P5b), con `gxv::judge` (G = X → salida nativa leída entera, como V1) | Si P5b dice «síncrono» o el escritor recibe la etapa o el gestor | [H] hasta localizarla |
| **F3, bandera por instancia** | Si la etapa guarda una copia por instancia de «cinemática/exclusiva/tiene cámara» (P5a sobre la etapa en P1 frente a 0,5 s después), ponerla a 0 en las etapas ajenas de X, como S44 hace con el vector de enlaces | Si existe esa copia por instancia (no los datos compartidos del recurso, que también usa el invitado para sus propias escenas) | [H] |

En las tres:
- solo etapas de X con dueño ajeno;
- nunca M ni las escenas propias del invitado (su tienda, su revancha, su cárcel si le arrestan a él);
- nada de CRT ni llamadas al juego en los ganchos;
- la verificación de bytes y `.pdata` al instalar sigue el mismo patrón que S44: si algo no cuadra, `WRN` y la pieza
  queda apagada.

**Criterios de aborto** (además de los de S44 §5):
1. Los guardias, Matthias o los tenderos dejan de animarse (la presentación no debería afectar a las pistas de actor).
2. Una escena del anfitrión se queda colgada en X (por ejemplo, esperando un `IsCameraJump` o un
   `condition_timelineEnd` que nunca llega). Mirar los 2524/2810 que no se cierran.
3. Una cinemática **propia** del invitado deja de verse.
4. Cualquier cierre del juego o `crashpad`.

### 3.4 Parche temporal (no es de raíz)

`guest_seq_route_deny=8CD771B1,E5B490F1`:
- deniega la cinemática de la celda;
- sus tres guardias están en la cárcel, a 70 m, y no aportan nada al invitado;
- es una lista: no cubre los `memory_*` ni las misiones con cámara de §2.3.

### 3.5 Prueba (dos copias en un PC)

Repetir el arresto, que es reproducible: con el anfitrión, atacar a un NPC en Hernand hasta que lo abatan los
guardias, con la ventana del invitado delante. Marcar con Ctrl+F12 al empezar y al acabar la cinemática.

- **Paso 1 (`probe`)**:
  - P5a debe mostrar los campos que cambian a las ~2 s del 2661 `E5B490F1` y vuelven al acabar;
  - P5c, si la entrada del invitado se bloquea.
- **Paso 2 (`1`)**:
  - la pantalla del invitado sigue en su cámara;
  - el invitado puede moverse durante la cinemática del anfitrión;
  - los guardias de la cárcel siguen animados (si el invitado va a verlos);
  - repetir con un recuerdo (`memory_*`) y con una tienda abierta por el anfitrión.

---

## 4. Lo que no está demostrado

- **[V del usuario, no del log] Que la cámara del invitado cambiara.** No hay sonda de cámara ni captura de pantalla de
  esta prueba. Lo apoyan [E] que la escena es una cinemática pura, que el foco estaba en el invitado, que coinciden la
  marca y el movimiento cortado, y el precedente de la sesión 9 (fov 40 en el invitado).
- **[E] Que la cámara salga de la pista global de la línea de tiempo** (`CameraSelectorEventData` y `CameraComponent`)
  reproducida por el tick de X. Lo que sí está [V] es que no pasó por ningún manejador de etapa enganchado.
- **[H] Que al invitado se le bloqueara la entrada.** El movimiento se cortó justo durante la línea de tiempo y hubo una
  acción en bucle única, pero el usuario también pudo soltar las teclas. P5c lo separa.
- **[H] Qué es `0x4564B267`** (y `0xBCC6BD59`/`0xEA356409`): no hay diccionario de acciones.
- **[H] El punto exacto del motor** que aplica cámara, cinemática o exclusivo: la lógica está en `.xtls` (los
  evaluadores y la vtable de `CameraSelectorEventData` llevan a trampolines). Ni F1, ni F2, ni F3 están verificados
  todavía.
- **[H] Efectos secundarios de «exclusivo»**: si la etapa ajena se registró como exclusiva en el cliente del invitado,
  sus propias etapas con `!ExclusiveStagePlaying()` habrían esperado mientras tanto. No se ha medido.
- **[H] Las órdenes `Player.*` de la escena** (`set_clearWanted`, `set_deleteItem_onEnter_0`) en X: no hubo tráfico, pero
  no se sabe si se ejecutaron en local contra el jugador del invitado (con el nodo del dueño a 0 no deberían tener a
  quién aplicarse [E]).
- No se ha mirado con Shadow (red real).

---

## 5. Datos clave (para copiar)

- 2661 en el invitado: `t=194.756` (23:50:25,537), payload de 135 B, `04 01 00 10 A0 01 00 10 A0 0A 03 00 00 00 00 00 00
  01 00 5C 55 70 01 00 00 00 00 F1 90 B4 E5 …`, enlaces `02:H/5 07:N/5 04:N/5 05:N/5`.
- 2524 + 2810: `t=213.153` (23:50:43,935), `2810 … 0A 03 … 5C 55 70 01 00`.
- Anfitrión: `ReviveLoadingAck` `01 00 10 A0 03 5C 55 70 01` (`t=205.335`); `LoadingFieldData` con la posición
  `35 A7 27 C6 52 8D 1B 44 B1 BD 6F C5` (-10729,8; 622,2; -3835,9).
- Líneas del invitado: `guest.log` L22333–22337 (GuestStage), L23479 (Route), L23571 (`[foreign]`), L23596 (P1), L23661–23662
  (XVeto), L24779–24858 (2810/2524).

## 6. Respuestas cortas a las preguntas

1. **¿Por qué vía?** Por la escena copiada (`observer_seq_copy`) `0x30A` `crime_police_wakeup_prison_hernand_0100`.
   - No por un paquete propio de arresto o cámara: el invitado no recibió `2993`, `2643`, `2295`, `2543`, `2481` ni
     `2322` de esta carga.
   - No le arrestó el servidor.
   - **¿Lo movió?** No [V]. **¿Le quitó el control?** Probablemente sí, durante los ~13 s de la línea de tiempo [E],
     sin pasar por ningún `InputBlock` ni `SequencerControl` sobre él.
2. **¿Nativo?** El servidor solo se la manda al dueño (ToTarget). Un tercer cliente nunca la reproduce. La deja pasar la
   cadena copia → ruta a X → tick de X. Ni los vetos (solo funciones de etapa, 0 llamadas de cámara) ni S44 (solo el
   enlace del dueño) cubren la cámara de la línea de tiempo. Los 11 P2 son escenas propias de la entrada en el mundo,
   ajenas a esto.
3. **¿Arreglo?** `guest_seq_foreign_present`: una etapa ajena de X no aplica su presentación global en este cliente.
   Primero, una sonda diferencial de solo lectura (P5a/b/c) en el gancho de fotograma ya verificado `0x8B6144`; después,
   F1 (`stage+0x280`), F2 (veto en la entrada) o F3 (bandera por instancia), según lo que mida. Parche temporal:
   `deny=…,E5B490F1`.

## 7. Archivos de esta investigación (`scratchpad\s45\carcel\`)

- `scan.py` (extrae opcodes de un `.bin`), `decode.py` (cronología con nombres; `names_cache.json`), `strs.py`,
  `markers.py`, `extract.py` / `extract2.py` (copias del extractor de S44, solo lectura de los archivos del juego).
- `g_timeline.txt` y `h_timeline.txt` (2661/2981/2524/2810/2322/2475/2993/2643/2344/2295/2543/… con nombres),
  `g_window.txt` y `h_window.txt` (logs recortados), `g_jail_lines.txt`, `gcap_win.txt` y `hcap_win.txt` (capturas en
  texto recortadas), `hjail.txt`, `g_own_actions.txt` y `h_own_actions.txt` (acciones propias).
- `data\` (los `.paseq/.paseqc/.pastage` de la cárcel, el recuerdo, `progression_call` y `hernand_0500`), `data2\` (89
  `.paseq` para `markers.tsv`). Son datos del juego: no copiarlos al repositorio.
