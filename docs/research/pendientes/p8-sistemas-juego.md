# P8: cómo se comportarán los sistemas de juego para el usuario 2 (antes de probarlos)

10 de octubre de 2026, rama `opcion-d`. Build del juego 1.0.0.2976; direcciones en **RVA** (base `0x140000000`).
Continúa `p8-sesion6.md` (el invitado ya entra en el mundo) y reúne lo que ya sabíamos de la etapa de copias
(`opcion-d-notas/optD-combate.md`, `optD-mundo.md`) con lo nuevo de esta lectura.

**[V]** = comprobado en instrucciones (capstone sobre el PE de solo lectura `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`,
SHA1 `D73CE808…`, idéntico al de `bin64`; Python `-I`), en capturas o en logs. **[E]** = inferencia o hipótesis
pendiente de prueba. **[S]** = suposición razonada sin lectura directa.

Solo análisis: no se tocó código, no se lanzó el juego, no se escribió en `bin64` (las capturas solo se leyeron), no se
abrió el proyecto de Ghidra de `D:\RE` y no se hizo commit. No se propone ningún gancho en `.xtls` ni en sus thunks.

## 0. Evidencia y método

| Ref. | Qué |
|---|---|
| PE | `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` |
| CSV | `docs/research/data/troc_messages_2976.csv` (manejadores `vf2` de cada Req) |
| BS | `C:\Emulador CD\build\research\appearance\broadcast_scan.txt` (buscador de destinatarios de cada escritor) |
| GBC | `C:\Emulador CD\backups\p8-sesion6B-shadow\cdmp_packets_20261009-224848.bin` (cliente del invitado, sesión 6B) |
| HBC | `D:\SteamLibrary\…\bin64\cdmp_packets_20261009-234606.bin` (cliente propio del anfitrión, sesión 6B) |
| Previos | `optD-combate.md`, `optD-mundo.md`, `opcion-d-p9.md`, `opcion-d-e3-riesgos.md`, `opcion-d-campo-compartido.md`, `p8-escenas-y-acks.md`, `p8-pausa-esc.md`, `pendiente-reaccion-muerte-invitado.md` |

Herramientas (en el scratchpad de la sesión; no se conservan): `hscan.py` (resume un manejador de Req: sigue el thunk a
`.xtls` si lo hay, sigue el registro que carga `[ctx]`, lista componentes, llamadas virtuales, búsquedas en la SAM,
accesos a `+0x90`/`+0x180`), `vt.py` (vtables por RTTI), `comprange.py` (rango de código de cada
`Server*ActorComponent`) y `capstat.py`/`cap2..6.py` (sobre `tools/scripts/analyze_capture.py`).

## 1. Resumen

1. **El modelo del servidor es por usuario, salvo tres embudos.** [V] Todo manejador de Req recibe un contexto cuyo
   primer campo `[ctx]` es **el personaje de la sesión que envía** (no hay "jugador global"). Ejemplos leídos:
   `MoveActorReq 0x2977E16 mov rdi,[r15]` → `vf170` (`0x17EA4E0` = clave del usuario dueño, `[actor+0xA0]+0x60`) contra
   `actor+0x90`; `SetQuestDialogReq` (`.xtls 0x104C7AF6 mov r14,[rbx]`, exige tipo 1); `EquipItem` (`.xtls 0x108860DC
   mov rdi,[rdx]` → `[[rdi+0x68]+0x38]`); igual en `UnequipItem`, `SellItemToItemStore`, `LearnSkill`, `ResetSkills`,
   `AcquireKnowledge`, `RequestMoveItemInternal`, `PushTargetToInventory`, `ClearWanted`… Los Nak 1013 van por
   `vfE8` del mismo personaje (su sesión). Por eso inventario, equipo, tienda, diálogo de misión, conocimiento,
   habilidades, mercenarios, caballo y "buscado" son **del usuario 2 de forma nativa**.
2. **Los tres embudos de dueño único [V]** (todo lo demás en este informe cuelga de ellos):
   - **`ServerField::vf40` = `0x2B00C70`**: busca el usuario `[field+0x180]` (dueño) y devuelve
     `[[[[user+0xD0]+0x68]+0x138]+0x18]` (gestor de stages/misiones del **personaje del dueño**). Lo usan
     `MissionTriggerInOutReq 2112` (`0x2C4A36C`), `UpdateTraceMissionReq 2372` (`0x2C4A0AA`), la sincronización de
     stages `0x2BC7885` y el revivir (`0x2947F76`).
   - **`ServerField::vf48` = `0x2B00CE0`**: igual, pero devuelve el gestor de **secuenciadores** del dueño
     (`+0xA8`, el de `0x2BC8470`). Lo usan **todas** las Req de secuenciador/escena: `StageBranchStateReq 2436`
     (`0x2A5C6C8`), `StageBranchStateDelayEndReq 2105`, `UpdateStageChartEventReq 2591` (`0x2C4A8F9`),
     `SequencerStageFailReq 2138` (`.xtls 0x1089F281`), `SubTimelinePauseReq 2406`/`ResumeReq 2968`,
     `StageQuitReq 2325`, `ReloadStageStartReq 2283`, `RegisterGlobalStageSequencerReq 3046`, `UpdateStageListReq 2454`,
     `UpdateSequencerGamePlayDataEventReq 2173` (todas pasan por `0x17FA6C0` = `TransformSync::vf140` → campo).
   - **`[field+0x180]` directo**: el evento `DeadActor 0x62` (`0x29432DE call 0x2AFFF50` → usuario dueño →
     `[user+0xD0]`), el **destinatario del botín al morir** (`0x2B83D24 call 0x20DEE60` → `.xtls 0xE2BF850`, lee
     `[r9+0x180]`), `SetFieldTimeReq` (`0x2BC89E0 cmp [rcx+0x180],eax ; jne` → solo el dueño), el revivir
     (`0x2947F0E`), los spawns de pool (`0x27EED63`, `0x27EA44B`) y el 2322 (`0x2BC8470`, ya conocido).
   Con `share_field=1` el dueño del campo compartido es siempre el anfitrión (`0x90100000`).
3. **Combate.** Golpear funciona de forma nativa con las estadísticas del usuario 2 (el servidor solo exige que el
   atacante sea de la sesión) [V previo]. La IA del anfitrión **ve y fija** al usuario 2: en HBC, 36 NPC distintos
   mandaron `CurrentTargetChangeReq` con objetivo `0xA010031F`, todos con motivo `0x18` (AIFunction_SetCurrentTarget)
   [V]. El riesgo grande sigue siendo el **bloqueo de rango**: un objetivo remoto (`+0xC8 = 0` en el cliente del
   anfitrión) no entra en la lista `C+0x58` y la IA no hace ataques normales contra él [V con la marioneta, E con el
   usuario 2 real]. La XP va al que mata (el usuario 2) [V]; **el botín y el `DeadActor` (contadores de misión,
   campamentos) van al anfitrión** [V].
4. **NPC y misiones.** Hablar y aceptar misiones son por usuario (componente `ServerQuestDialogActorComponent`,
   `+0xF8`; en 6B, 539 `SetQuestDialogReq` → 539 `SetQuestDialogAck`, 0 Nak) [V]. Pero **la maquinaria de misión del
   mundo es del anfitrión**: los disparadores de misión, el seguimiento, las ramas de escena, las recompensas de stage
   y los contadores por muerte van a los gestores del dueño [V ruta, E efecto]. Esperado: las misiones del usuario 2
   no avanzan, o avanzan las del anfitrión.
5. **Economía.** Tienda, inventario, equipo y dinero son por usuario y se escriben en los componentes del usuario 2
   en el servidor del anfitrión [V ruta]. Excepciones: el botín de las muertes (al anfitrión) [V] y los posibles
   gestores globales que pisa la carga de la partida del invitado (precios, eventos globales) [S, P9 §3].
6. **Persistencia.** Todo el progreso del usuario 2 vive en sus componentes del servidor del anfitrión, cargados de
   una ranura del disco del anfitrión. Hoy el guardián rechaza toda escritura y el progreso se pierde al salir. El
   diseño para devolverlo a la partida del invitado está en `opcion-d-p9.md` §5; aquí se añaden los problemas de
   coherencia del campo compartido (§5).
7. **Pausas.** Con `ui_pause_block` los menús ya no congelan el mundo. Quedan con congelación o efecto global:
   cargas del anfitrión, la barrera del autoguardado, las cinemáticas y **`SubTimelinePauseReq` del invitado, que
   cae en el gestor de secuenciadores del anfitrión** [V ruta] (§7).

---

## 2. El patrón de los manejadores (cómo se decide "quién")

### 2.1 Personaje de la sesión [V]

| Pieza | Evidencia |
|---|---|
| `vf2(this, &res, ctx)`; `ctx+0x18` = paquete, `ctx+0x10` = tamaño, `[ctx]` = **personaje** de la sesión | `0x2977E41`, `0x2978216 mov rdi,[r15]` |
| `vf160` del personaje = `[actor+0xA0]` (usuario dueño); `vf170` = `&([actor+0xA0]+0x60)` (clave del usuario) | `ServerNormalInGameActor` vt `0x5B1DE20`: `[0x160]=0x50E6B0`, `[0x170]=0x17EA4E0` |
| Comprobación de dueño típica: `*vf170() == target+0x90` | `0x2978265..0x2978274` (Move), `DoFrameEventReq`, `CurrentTargetChangeReq`, `ChangeAIStateReq`, `RegionInOutReq`, `AiControlChangeCheatReq` (`0x2C414F8..0x2C41500`) |
| Lógica fuera del manejador: `logic(this, ctx, &req…)` con `mov rdi,[rdx]` | `0x2C449A0` (Equip) y 20 más (tabla §2.3) |
| `SetQuestDialogReq` exige tipo 1 y `[actor+0x96] != 0` | `.xtls 0x104C7AF9..0x104C7B0E` |

### 2.2 Los gestores del campo [V]

| Función | Qué devuelve | Quién la usa |
|---|---|---|
| `0x17FA6C0(actor)` | `[[actor+0x68]+0x1A0]->vf140()` = `FieldAttacherForActor` del campo actual | toda Req de escena, misión, spawns |
| `ServerField::vf40 0x2B00C70` | `FindUser([field+0x180])` → `[[[[u+0xD0]+0x68]+0x138]+0x18]` | `MissionTriggerInOut`, `UpdateTraceMission`, `0x2BC7885`, revivir |
| `ServerField::vf48 0x2B00CE0` | igual con `+0xA8` (secuenciadores) | las 11 Req de escena de §1.2 |
| `ServerField::vf50 0x2B00D50` | `[field+0x228]` (del campo, sin dueño) | — |
| `0x2AFFF50(field)` | usuario dueño → su personaje `[u+0xD0]` | `DeadActor 0x62` (`0x29432DE`) |
| `0x20DEE60` → `.xtls 0xE2BF850(field)` | usuario dueño `[field+0x180]` | botín al morir (`0x2B83D24`) |

**Consecuencia general [E fuerte]:** en el campo compartido, cualquier acción del usuario 2 que pase por estas
funciones actúa **sobre el estado del anfitrión**, y lo que el juego "entrega al jugador" por esa vía llega al
anfitrión. Nada de esto falla con error: no hay Nak, simplemente se aplica al otro.

### 2.3 Clasificación de Req por sistema [V ruta salvo que se diga]

| Req (op) | Manejador → lógica | Sobre qué actúa | Clase |
|---|---|---|---|
| `DoFrameEventReq` 2380 | `0x296D130` | atacante = de la sesión (`+0x90`), objetivos libres | **por usuario** |
| `CurrentTargetChangeReq` 2984 / `AggroRelationListReq` 3006 / `ChangeBattleStateReq` 2257 | `0x2980740` / `0x2980AD0` / `0x29814A0` | actor controlado por la sesión | por controlador (NPC = anfitrión) |
| `CreateProjectileFromFrameEventReq` 2387 / `OnHitProjectileReq` 2999 | `0x2A3E000` / `0x2A3A1C0` | dueño del proyectil = sesión | por usuario |
| `AttackByCollisionReq` 2815 | `0x2C3D7D0` (`mov rdi,[rdx]`) | sesión | por usuario |
| `DieByRunOutFatalReq` 2321 | `0x29947E0` | `+0x90` del muerto = sesión | por controlador |
| `ReviveLoadingReq` 2362 | `.xtls 0x1048FFC0` → `0x2947E00` | Status de la sesión, **pero** lee dueño y `+0x138` del dueño | **mixto** |
| `ReviveReq` 2988 | `0x298C070` → `0x1794000` | personaje de la sesión | por usuario |
| `UpdateSequencerGamePlayDataEventReq` 2173 (al morir) | `0x2A5C090` → vf48 → `0x2891CE0` | **gestor del dueño** | dueño único |
| `InteractionPreemptionStepReq` 2671 / `InteractionDoStepReq` 2720 / `InteractionResetReq` 2136 | `0x29E8FA0`→`0x2B36860` / `0x2C44080` / `0x2C44240` | sesión + objetivo por clave | por usuario (bloqueo por objetivo) |
| `SetQuestDialogReq` 2857 / `QuestDialogEndReq` 2342 / `…InterrogationAnswerCompleteReq` 2554 / `ClearQuestDialogReq` 2151 | `+0xF8` de la sesión | `ServerQuestDialogActorComponent` | **por usuario** |
| `RewardAfterRelationalDialogReq` 2415 / `DonateToFactionReq` 3017 | `+0x140` → `0x286C120`/`0x286DA80` | `ServerFriendlyActorComponent` | por usuario |
| `MissionTriggerInOutReq` 2112 / `UpdateTraceMissionReq` 2372 | `0x2C4A310` / `0x2C4A050` → **vf40** | gestor de misiones **del dueño** | **dueño único** |
| `StageBranchState*` 2436/2105, `UpdateStageChartEventReq` 2591, `SequencerStageFailReq` 2138, `SubTimeline*` 2406/2968, `StageQuitReq` 2325, `ReloadStageStartReq` 2283, `RegisterGlobalStageSequencerReq` 3046, `UpdateStageListReq` 2454 | → **vf48** | secuenciadores **del dueño** | **dueño único** |
| `AcquireKnowledgeReq` 2964, `StartKnowledgeLearnDelayReq` 2599, `AddKnowledgeLearnStackReq` 2522 | `0x2C46F80` (`mov rsi,[rdx]`) … | `ServerKnowledgeActorComponent` | por usuario |
| `StartDiscover…` 2154 / `CompleteDiscover…` 2688 / `SetTimeWrapCoolTimeReq` 2669 / `PushAlertHistoryReq` 2469 | `+0x168` | `ServerContentsMiscActorComponent` | por usuario |
| `BuyStockFromStoreReq` 2789 / `ReorderItemFromStoreReq` 2308 / `BuyRestorationGimmickReq` 3077 | `+0xD0` → `0x2BFFB00`/`0x2C002D0`/`0x2C086A0` | `ServerStoreActorComponent` (rango `0x2BFD840–0x2BFF130`) | **por usuario** |
| `SellItemToItemStoreReq` 2366 | `0x2C44F40` (`mov rdi,[rdx]`) → `0x2C04880` | sesión | por usuario |
| `UseItemReq` 2855 / `UseItemByItemInfoReq` 2269 / `RemoveItemToInventoryReq` 2630 / `RequestMoveItemInternalReq` 2585 / `MoveItem*` | inventario `0x2B4xxxx–0x2B6xxxx` | `ServerInventoryActorComponent` | por usuario |
| `EquipItemReq` 2157 / `UnequipItemReq` 2120 / sockets / tinte / reparar | `+0x38` EquipSlot, `+0xB8`, `+0xD8` | sesión | por usuario |
| `ExtractEnchantedInventoryItemReq` 2373 / `PushMiniGameRewardReq` 2595 | `+0xB8` → `0x2B699D0`/`0x2B68900` | inventario | por usuario |
| `LootingDropFromDeadBodyReq` 3052 | `0x2A116B0` → `0x2B81410(cuerpo)`; `RemoveLootableAck` ToTarget | quien saquea | por usuario [E en el reparto] |
| `PushTargetToInventoryReq` 2198 | `0x2C46AB0` (`mov rsi,[rdx]`) + objetivo por clave | quien recoge | por usuario |
| `LearnSkillReq` 2870 / `ResetSkillsIfPossibleReq` 2956 | `0x2C4BDD0` / `0x2C47C10` | `ServerSkillActorComponent` | por usuario |
| `CallHyosiByQuickSlotReq` 2919 / `CallSpecialVehicleByQuickSlotReq` 2289 / `FireAndPush*` 2214/2590 / `FrameEventCallMercenaryReq` 2166 | `+0x110` → `0x2BA0090`… | `ServerMercenaryClanActorComponent` | por usuario |
| `WantedAddCrimeRecordReq` 2422 / `ChangeWantedStateReq` 2983 / `ClearWantedReq` 2837 | `0x2C4D8F0` / `0x2C4D560` / `0x2C4DC10` | `ServerWantedActorComponent` | por usuario |
| `SetFieldTimeReq` 2125 | `.xtls 0x1049D430` → `0x2BC8970` | **solo si el que pide es el dueño** (`0x2BC89E0`) | dueño único (se ignora al invitado) |
| `SyncWeatherStateReq` 2560 | `0x29C5DF0` → campo (`0x2BC4280`) → `0x20E6FE0` | campo | del campo [E] |
| `ActivateSpawningPoolReq` 2424 / `AutoSpawnRoadReq` 2550 | `0x2C41AF0` / `0x2C41E50` → spawns del campo `0x27EE3E0`/`0x27E8C90` (leen `[field+0x180]`) | campo; controlador de lo creado = dueño | dueño único [E fuerte] |
| `SaveAutoReq` 3037 / `SaveToFileCurrentPlayerReq` 2910 | `0x28FC5F0` / `0x28FD6E0` | personaje de la sesión, **ranura del gestor global** | mixto (P9 §5.1) |

---

## 3. Combate

### 3.1 El usuario 2 golpea NPC y enemigos del anfitrión

| Paso | Qué pasa | Base |
|---|---|---|
| Detección del golpe | En el cliente del invitado, con la posición de los enemigos que le llegan del anfitrión (interpolados 200 ms) | [V previo, `optD-mundo` §2.3] |
| `DoFrameEventReq` 2380 | El servidor exige que el atacante sea de la sesión (`+0x90`) y que su acción actual coincida con el accesor; los objetivos pueden ser de otro dueño | [V, `optD-combate` §1.3, §2.2] |
| Movimiento del golpeado | Solo se aplica el `MoveData` de objetivos del **mismo dueño** (`0x2BC5420`); el golpe del invitado no recoloca al NPC en el servidor | [V regla, E efecto] |
| Daño | Fórmula del servidor `0x209A680` con las estadísticas y el arma **del usuario 2** | [V] |
| Acks | `PostProcessAttackResult_*` 2922/2509, `VaryHPAck` 2147, `OnPostBuffDamagedAck` 2681, `ApplyActionRestrictionAck` 2162, buffs: **ToNear** → llegan a los dos | [V, BS] |
| Reacción del NPC | La anima el cliente que lo controla (el anfitrión) y vuelve al invitado por `StartActionAck`/`ChangeAIStateAck`, relevos `ExceptOneControl` que esta build trae vaciados: hace falta **`relay_fix=1`** (está en el ini de la sesión 7) | [V en vivo A2] |
| Números de daño | Solo si el atacante es el jugador local (`OPBD → 0x632EC0`): el invitado ve los suyos | [V] |
| Aggro | En el cliente del anfitrión, el buff de daño llama a `AddAggro(fuente)` (`.xtls 0xE181C10`); exige que el enemigo sea de control local, y lo es | [V ruta, E en vivo] |
| Muerte del enemigo | `KillActor 0x61` al jugador acreditado (el usuario 2) → `AddSubLevelExperienceAck` 2381 **ToTarget** al usuario 2 | [V; capturas: kill de otro usuario dio XP 9/9] |
| Contadores de misión y campamento | `DeadActor 0x62` al **personaje del dueño del campo** (`0x2AFFF50`) | **[V]** |
| Botín | Destinatario = **usuario dueño del campo** (`0x2B83D24` → `.xtls 0xE2BF850`); `LootDropCharacterRewardListAck` 2897 ToNear | **[V]** |
| Buff de asesino 20001 | Al asesino si es jugador (el usuario 2) | [V previo] |

**Lo que necesita recibir el cliente del invitado:** los Acks ToNear del golpe (llegan por la sesión 2 si el NPC está
en su AOI), los relevos de animación del NPC (`relay_fix`), su `AddSubLevelExperienceAck` y el `RemoveActorAck` del
cadáver. Todo es nativo.

### 3.2 La IA del anfitrión frente al usuario 2

- **Lo ve y lo fija [V]:** en HBC, 36 `CurrentTargetChangeReq` de 36 NPC distintos con objetivo `0xA010031F`, todos
  motivo `0x18` (`AIFunction_SetCurrentTarget`), más 5 `StartAIFunctionReq` y 209 `StartActionReq` que contienen su
  clave. No eran enemigos en combate (no hubo pelea en 6B), así que esto prueba la percepción, no el ataque.
- **Bloqueo de rango [V marioneta, E usuario 2]:** `AICondition_CheckAIBattleType` (`.xtls 0xC5DC280`, desde la vf1
  `0x22E0A30`) devuelve "esperar" si el atacante no está en la lista de rango `C+0x58` del objetivo, que solo se
  llena para objetivos controlados por el cliente local. En el cliente del anfitrión, el Kliff del usuario 2 es
  remoto (`+0x90 = 0x9010031E ≠ local` → `+0xC8 = 0`). Con la marioneta: 0 ataques normales, 10,9 ataques en
  carrera por minuto, frente a 16,8–18,1 contra el jugador local. **Esperado: los enemigos rodean al usuario 2 y le
  atacan poco, salvo que el anfitrión esté cerca y se lleve el aggro.**
- **No hay transferencia de control nativa utilizable [V parcial]:** `OverriedOwnershipStateTypeyAck` 2168 lo escribe
  `0x2A9B670` (componente de gimmick), no de NPC. `ChangeAiControlAck` 2508 sale solo de `0x2C41360`
  (`AiControlChangeCheatReq` 2442), que pide permiso de trucos tipo 3 y que el que pide **ya sea** el controlador
  (`0x2C414E4..0x2C41500`).
- **Primera adquisición:** con la marioneta, 11 de 14 primeras adquisiciones fueron al jugador local aunque la
  marioneta estuviera más cerca [V captura]. Esperado igual con el usuario 2.
- **Distancia [E fuerte]:** el controlador de todo lo que se crea en el campo compartido es el dueño (2005 de 6B:
  1 685 de 1 693 con control `0x90100000`; los 8 restantes son gimmicks y el propio Kliff del usuario 2) [V
  recuento]. Los spawns de pool leen `[field+0x180]` (`0x27EED63`) [V]. Un NPC que ningún cliente carga queda
  vivo pero quieto en el servidor (sin IA) [V previo, 3G §1.1]. Si el usuario 2 se aleja más de ~200 m del
  anfitrión, los NPC que lo rodean no tendrán IA.

### 3.3 El usuario 2 recibe golpes

| Paso | Qué pasa | Base |
|---|---|---|
| Detección | En el cliente del anfitrión (controla al enemigo), contra la posición interpolada del usuario 2: puede recibir golpes que en su pantalla esquivó | [E] |
| `DoFrameEventReq` del enemigo con objetivo el usuario 2 | El servidor no exige mismo dueño para los objetivos; el 17 % de los golpes reales llegan sin lista y el servidor barre él mismo (`0x1565970`) | [V previo] |
| Invulnerabilidad / esquiva | Se mira en el servidor (`0x209CB30`) con los buffs del usuario 2, que manda su propio cliente | [V ruta] |
| Reacción | `AAR` 2162 + buff de golpe (mismo id) → el cliente del invitado reacciona en ~19 ms y manda su `StartActionReq` | [V captura, 192/193 golpes reaccionan en el jugador real] |
| Vida | `VaryHPAck` ToNear con fuente = enemigo | [V] |

### 3.4 Muerte y reaparición del usuario 2

Secuencia nativa (capturas de un jugador [V]): `NotifyHandleDeadAck` 2530 (ToNear) → acción de muerte →
`UpdateSequencerGamePlayDataEventReq(0x198, 0)` 2173 → ~10 s → `ReviveLoadingReq(2)` 2362 → `ReviveLoadingAck` 2993
(ToTarget), `DisbandMercenaryAck`, `LoadingFieldDataAck` (punto de reaparición) → `GameLoadingStartReq` 2437 →
`ReviveReq` 2988 → `VaryHP` + `NotifyHandleReviveAck` 3053 + `ReviveAck` 2475 (ToNear) → `LoadingCompleteReq 00`.

Para el usuario 2:
- **2173 cae en el gestor de secuenciadores del anfitrión** (vf48) [V ruta]. Efecto desconocido: anota la muerte en
  los datos de juego del secuenciador del anfitrión [E].
- **`ReviveLoading` lee el dueño del campo y su gestor `+0x138`** (`0x2947F0E`, `0x2947F76`) para elegir el punto de
  reaparición [V lectura, E uso]. Puede elegir un punto según el progreso del anfitrión.
- **Es un viaje de carga**: `GameLoadingStartReq` pone `FM+0x58` y arma `MoveFieldOnceTimer` [V previo]. Pasa por
  `AcquireField` (H1 de `share_field`): si el punto está en el mismo nivel que el campo abierto del anfitrión, vuelve
  al campo compartido; si no, **el usuario 2 recibe un campo propio** y es su dueño [V lógica de H1]. En los dos casos
  vuelve a necesitar el 2322 de `observer_milestone` y la retención de NPC (`observer_hold`). Es el "cambio de campo"
  del criterio 6 de S2, que no se ha probado [V `p8-sesion6.md` §1].
- Si el anfitrión está en carga o en otro viaje en ese momento, se mezclan dos viajes [E].

---

## 4. Interacción con NPC, diálogos, misiones y escenas

### 4.1 Hablar

| Paso | Clase | Lo que recibe el invitado | Riesgo |
|---|---|---|---|
| `InteractionPreemptionStepReq` 2671 → `0x2B36860` del objetivo; `…SuccessAck` 2663 / `…FailAck` 3000 **ToTarget** | por usuario; bloqueo por NPC | su Ack | Si el anfitrión está hablando con el mismo NPC, el invitado recibe el Fail [E, comportamiento MMO] |
| `InteractionDoStepReq` 2720 → `0x2C44080`; `InteractionDoStepSuccessAck` 2916 ToTarget | por usuario | su Ack | — |
| `SetQuestDialogReq` 2857 / `SetQuestDialogAck` 2948 ToTarget | por usuario | ya funciona: 539/539 en 6B [V] | — |
| `ShowAIDialogAck` 2450 (frases de IA) | `ToSessionList` (`0x2ACA761`) | a la lista de sesiones del NPC [V finder] | Puede no incluir al invitado [E] |
| El diálogo en sí es un **secuenciador**: `PrepareSequencerAck` 2661 ToTarget al usuario 2; `StageBranchStateAck` 2981, `UpdateStageChartEventAck` 2111, `StageOnBranchEventAck` 2524, `SetAvoidByCutSceneAck` 2561 **ToNear**; `DeleteSequencerAck` 2810 **ToTarget al actor del gestor** | escena del usuario 2 **en el gestor del dueño** | 2661, 2981, 2111, 2524 sí; el 2810 solo con `observer_scene_copy=1` | Es el caso "ver un objeto" de la sesión 6 (`p8-escenas-y-acks.md`). La copia del 2810 es general (dueño del secuenciador `[rsi+0x98]` = usuario 2), así que debería cubrir también los diálogos [E: probar] |
| Elecciones del diálogo (`StageBranchStateReq` 2436, `UpdateStageChartEventReq` 2591) | **gestor del dueño** | 2981/2111 ToNear | Funcionó para `0x1A4` (llegaron 2981 y 2111) [V]. Con ramas que miran `Player.` (el personaje local del gestor) se evaluarán **con el Kliff del anfitrión** [E fuerte, BO §1.3] |

### 4.2 Misiones

| Pieza | Dónde vive | Esperado para el usuario 2 |
|---|---|---|
| Lista de misiones, misiones aceptadas, indicadores | `ServerQuestActorComponent` de su personaje; `UpdateQuestListAck` 2808 / `UpdateMissionListAck` 3009 / `UpdateQuestGaugeListAck` 2635 **ToTarget** (`0x290FF30`) | Ve **su** lista (la de su partida). En 6B: `UpdateQuestListAck` ×1, `UpdateMissionListAck` ×60 [V] |
| Aceptar o entregar en un diálogo | `QuestDialogEndReq` 2342 (`+0xF8` propio) | Se apunta en sus componentes [E] |
| Zonas de misión (`MissionTriggerInOutReq` 2112) | evento `0x11`/`0x12` (`0x2C4A3E0`/`0x2C4A428`) al gestor `+0x138` **del dueño** (`0x2C4A420 call 0x1804360`) [V] | Al entrar el invitado en una zona, **avanza o dispara la misión del anfitrión** si coincide; la suya no se entera |
| Seguimiento (`UpdateTraceMissionReq` 2372) | vf40 del dueño [V] | Cambia el seguimiento del anfitrión o no hace nada |
| Stages de misión (NPC, gimmicks, ramas, `Reward`, `PushReward`, `CompleteStage`) | secuenciadores y stages del dueño | Las recompensas y finales de stage van al anfitrión [E fuerte] |
| Contadores por muerte, campamentos (`UpdateQuestGaugeAck` 3088, `CompleteQuestGaugeAck` 2738) | `DeadActor` al dueño [V] | Las muertes del invitado suben los contadores **del anfitrión** |
| Desafíos de maestría y logros por `KillActor` | jugador acreditado | Del usuario 2 en el servidor del anfitrión [E]. Los logros de Steam son del cliente (`PlatformAchievementManager_Steam`): el invitado podría desbloquearlos en su cuenta [E] |
| Incoherencia de mundo | P9 §3: la partida del invitado espera stages, NPC y gimmicks **de su mundo** | Misiones del invitado bloqueadas en el campo del anfitrión [S] |

**Conclusión:** en el campo compartido las misiones son, en la práctica, **del anfitrión**. El usuario 2 puede
ayudar (sus muertes y zonas cuentan para el anfitrión), pero su propio progreso de misión no avanza de forma fiable.
La alternativa natural es que el usuario 2 haga sus misiones en **su propio campo** (cuando viaja a otro nivel es el
dueño, §6.2).

### 4.3 Escenas y cinemáticas que dispara el usuario 2

- Abrir una escena propia (objeto, diálogo, `UseItemReq` 2855): el servidor la crea en el gestor del dueño y manda
  el 2661 al usuario 2. Falta el 2810 (cubierto por `observer_scene_copy`) [V].
- `SetAvoidByCutSceneAck` 2561 (`u8 4 | u32 user | u64 stage | u8 flag | u32 user`) es ToNear y lleva el usuario:
  el anfitrión la recibe también [V finder]. Si su cliente la aplica sin mirar el usuario, **podría entrar en modo
  "evitar por cinemática"** mientras el invitado ve la suya [E].
- `SubTimelinePauseReq` 2406 del invitado pausa una subtimeline **en el gestor del anfitrión**, y su Ack 2892 es
  ToNear [V]. Si el id coincide con una secuencia del anfitrión, se la pausa [E].
- `SequencerStageFailAck` 2253 va con `ToStageSendTargetFinder` (`0x2893D20`) [V]: a los participantes del stage del
  dueño. El invitado puede no recibir el fallo de su propia secuencia [E].
- Las escenas de NPC del campo (S1) siguen pendientes de la decisión de `p8-s1-escenas-npc.md`.

---

## 5. Economía y persistencia

### 5.1 Tienda, inventario, dinero, equipo, recogida

| Sistema | Clase | Lo que recibe el invitado | Riesgo |
|---|---|---|---|
| Datos de tienda | `StoreDataListAck` 2146 ToTarget (`0x28F50CC` al entrar; `0x2BFF206`/`0x2BFF8B7` al cambiar) | En 6B recibió 675 [V] | — |
| Comprar / vender | `+0xD0` (tienda) / `0x2C44F40`; inventario `AddItemToInventoryAck` 2962 / `RemoveItemToInventoryAck` 2481 / `ItemChangedInventoryDataListAck` 2200 **ToTarget** | sus Acks | El stock de la tienda podría ser por usuario (cada uno ve su stock) [E] |
| Dinero | Objeto del inventario [S, como en BDO] | `ItemChanged…` | — |
| Precios globales | `TradeItemPriceRate`, `GlobalGameEventSaveData` (cargadores cerca de `0x18B6xxx`/`0x18CDxxx`), `GlobalGameEventVaryTradeItemPriceAck` 3075 | — | La carga de la partida del invitado podría pisar precios y eventos globales del anfitrión [S, P9 §3, sin leer] |
| Equipo | `+0x38` del personaje de la sesión [V]; el anfitrión lo ve por `OtherEquipItemAck` 2757 ToNearExceptOneSelf [V previo] | `EquipItemAck` 3098 | `characterNo` repetido: `identity_fix` (F002) ya lo trata [V previo] |
| Botín al matar | **Destinatario = dueño del campo** [V] | `LootDropCharacterRewardListAck` 2897 ToNear (lo ve) | Lo que mata el invitado lo suelta "para" el anfitrión. Si solo el destinatario puede recogerlo, el invitado no podrá [E] |
| Saquear cadáver | `LootingDropFromDeadBodyReq` 3052 de quien saquea; `RemoveLootableAck` 2822 ToTarget | su Ack | Si el cadáver tiene recogida exclusiva del destinatario, falla [E] |
| Recoger del suelo, plantas, animales | `PushTargetToInventoryReq` 2198 (sesión + objetivo) | `AddItemToInventoryAck` | El objeto es del mundo: si el anfitrión lo recoge antes, el invitado falla [E, normal] |
| `itemNo` repetidos | Dos partidas del mismo juego pueden repetir números de instancia | — | Cualquier tabla del servidor indexada por `itemNo` (caché, mercado) puede mezclar objetos [S, `optD-mundo` §7.8] |

### 5.2 Dónde vive el progreso del usuario 2 y qué se guardaría

- **Dónde vive [V/E]:** en los componentes del personaje y del usuario 2 dentro del servidor del anfitrión
  (inventario, equipo, misiones, conocimiento, habilidades, SubLevel/XP, mercenarios, ContentsMisc, Wanted,
  Friendly). Se cargan con `0x186F570` desde la ranura que pide su `LoginCharacterToFieldServerReq` (u32 en `+9`),
  **leída del disco del anfitrión** (`p8-titulo.md` §3; `opcion-d-login.md`).
- **Qué intenta guardar el juego:**
  - el temporizador periódico 2011 pide `SaveGameDataAutoTimer` 2077 **también para el usuario 2**, sin mirar quién es
    (`opcion-d-e3-riesgos.md` R2) [E-x];
  - la ranura la elige el **gestor global** (`0x1871850`, rotación 0-2), no el usuario: escribiría en una ranura de
    autoguardado del anfitrión (P9 §5.1) [E];
  - el *commit save* corre dentro del hilo del servidor con la barrera TLS `+0x4460`: congela un momento a todos
    (E3 R4) [E].
- **Hoy:** el guardián (`save_hooks.cpp`) bloquea toda escritura de fichero en la carpeta de partidas. El juego
  muestra "Failed to save data" [V]. Pero **la memoria del almacén** de esa ranura (`fileObj`) se queda con los datos
  del usuario 2 hasta el siguiente guardado del anfitrión (P9 §5.1) [E]. Por eso nunca hay que levantar el
  guardián con un guardado pendiente.
- **Qué se serializa con el campo compartido [S, lo más delicado]:** los datos del campo
  (`_fieldGimmickSaveDataList`, `FieldNPCSaveData`, `LevelGimmickSceneObject*`, niebla) se guardan en componentes del
  usuario y se aplican al campo del que es dueño. Si al guardar se toman del campo **actual**, el usuario 2 se
  llevaría a su partida el estado de gimmicks del anfitrión. Sin leer.

### 5.3 Qué hace falta para guardar el progreso del usuario 2 en su propia partida

Base: `opcion-d-p9.md` §2.2 y §5.2. Lo mínimo:

1. **Ranura propia `Ng`** (propuesta: 207) para el usuario 2. Ganchos en **entradas `.rsrc`** con prólogo normal:
   `0x186F570` (cargar) y `0x186E450` (guardar). Si el actor es del usuario 2, `r8d = Ng`. Cubre el autoguardado,
   `SaveToFileCurrentPlayerReq` y cualquier otro llamador.
2. **Redirigir, no bloquear**, la ruta `…\save\<id>\slot<Ng>\` en los hooks de ficheros a una carpeta de intercambio
   (`%LOCALAPPDATA%\cdmp\guest\slot<Ng>\`). El juego cifra con su propio código.
3. **Subida al conectar** (antes de la 1001): el invitado manda `save.save` y `lobby.save` de la ranura elegida; el
   anfitrión los deja en la carpeta de intercambio (P9 §4).
4. **Bajada al guardar:** tras un guardado con resultado 0, el mod lee los dos ficheros y los manda al invitado, que
   los guarda **fuera** de su carpeta de partidas y los aplica con el juego cerrado y con copia de seguridad.
5. **Listas negras:** ranuras 3 (CrossSave: subiría por HTTP a Pearl Abyss, `0x15AC100`) y 206.
6. **Bloquear "Nueva partida"** del invitado en el servidor del anfitrión.
7. **Nuevo por el campo compartido:** decidir qué estado de mundo se guarda en la partida del invitado. Opciones:
   (a) guardar solo cuando el usuario 2 está en **su** campo (es dueño); (b) leer los serializadores de datos de
   campo antes de permitir guardar en el campo compartido. Recomendación: (a) hasta leer (b).
8. **Gestores globales:** leer los cargadores `vf(0x50)` de eventos globales y precios (P9 §3) antes de cargar una
   partida distinta, para que la carga del invitado no pise los del anfitrión.

Hasta entonces: guardián activo, progreso del invitado descartado al salir, y copia de la carpeta de partidas del
anfitrión antes de cada prueba.

---

## 6. Monturas, compañeros, viajes, habilidades, oficios (clasificación rápida)

### 6.1 Tabla

| Sistema | Clase | Evidencia | Riesgo principal |
|---|---|---|---|
| Caballo y mercenarios | **Por usuario**: hijos de su contenedor (`ServerChildOnlyInGameActor`, `SAM::CreateUser 0x2A83310`); su IA la lleva **el cliente del invitado** | GBC: `0xB0100320` (hijo del usuario 2) con 77 `ChangeAIStateReq`, 147 `StartActionReq`, 53 `DoFrameEventReq` y 45 `UpdateAIDestinationReq` del invitado [V] | En el anfitrión es un remoto de modo 2: clase de cierre "montura sin acción" (`0x925F0E`), contenida por `child_hold`/`character_hold` [V previo]. Montar = interacción; `RideOn/RideOff::Enter` fallan sin enlace de jinete en remotos → el anfitrión puede ver al invitado montado raro o congelado al desmontar [V previo, `optD-mundo` §2.4/§2.6] |
| Llamar montura | `CallVehicleMercenaryAck` 2955 **ToTarget**; `AITeleportReq` → `MoveForceAck` | [V BS] | — |
| Viaje rápido / cambio de nivel | Viaje de carga (`MoveFieldAck` 2174 ToTarget). H1 de `share_field` **solo comparte si el destino es la misma entrada de nivel** que el campo abierto del anfitrión; si no, campo propio | `field_share.h` [V] | Fuera del campo del anfitrión, el usuario 2 juega solo en su campo (es el dueño: escenas, misiones y hora le funcionan, pero no ve al anfitrión). Al volver, otro viaje con 2322 |
| Teletransporte dentro del campo | `DoTeleportAck` 2987 ToTarget; `MoveForceAck` ToNear | [V BS] | — |
| Hora del día | `SetFieldTimeReq` solo del dueño (`0x2BC89E0`) | [V] | Esperar o dormir no funciona para el invitado (se ignora sin Nak); la hora es la del anfitrión |
| Clima | `SyncWeatherStateReq` → campo | [V ruta] | El cliente del invitado podría empujar su clima al campo del anfitrión [E] |
| Habilidades | `LearnSkillReq`, `ResetSkills…`: por usuario; puntos por `AddSubLevelExperienceAck` ToTarget | [V] | — |
| Conocimiento y descubrimientos | Por usuario (`AcquireKnowledge`, `+0x168`) | [V] | El estado "descubierto" del objeto del mundo puede ser del campo [E] |
| Oficios (cocina, forja, minijuegos) | Los Req de resultado son por usuario (`PushMiniGameRewardReq`, `ExtractEnchanted…`, sockets, tinte, reparar); empiezan con interacción con un gimmick del mundo | [V ruta] | Gimmick ocupado por el otro [E] |
| Buscado / crimen | Por usuario (`ServerWantedActorComponent`) | [V] | Los testigos son NPC del anfitrión [E] |
| Fiesta / grupo | `JoinPartyAck` por compañero; sin datos de grupo entre jugadores | [V previo] | Sin grupo, no hay reparto de XP ni de botín [E] |

### 6.2 El campo propio del usuario 2 como modo alternativo

Cuando el usuario 2 viaja a un nivel distinto del del anfitrión, recibe un campo propio y **él es el dueño** [V H1 y
B3]. Ahí los tres embudos trabajan para él: sus escenas, sus misiones, su hora, su botín y el control de los NPC
(controlador = dueño = su cliente). Es "un jugador" servido por el anfitrión. Es útil como modo seguro para que el
invitado progrese, pero no es cooperativo.

---

## 7. Pausas y modales

Base: `p8-pausa-esc.md`. Con `ui_pause_block=1`, ESC, mapa, inventario y ajustes ya no congelan el mundo mientras
hay un invitado [V un PC]. Lo que todavía congela o tiene efecto global:

| Estado | Efecto | Base |
|---|---|---|
| Cargas del anfitrión (viaje, muerte, cambio de campo) | Su cliente no simula: todos los NPC quietos para el invitado | [V mecanismo] |
| Barrera del autoguardado (2077, TLS `+0x4460`) | Para todos los hilos del servidor mientras guarda (con el guardián, falla rápido) | [E] |
| Cinemáticas (`GamePause`, secuenciador) | Su pausa propia sigue | [E] |
| `SubTimelinePauseReq` 2406 del invitado | **Cae en el gestor del anfitrión** (§4.3) | [V ruta] |
| `SetAvoidByCutSceneAck` 2561 ToNear | Puede poner al anfitrión en modo cinemática | [E] |
| ESC del invitado sin gancho en su lado | Su mundo y su reloj se congelan **localmente**; el anfitrión lo ve "correr en el sitio" | [V mecanismo] |
| Mapa del mundo con el mundo en marcha | El streaming puede seguir a la cámara del mapa y descargar el entorno | [E, riesgo principal de T1] |
| Minijuegos, modo foto, QTE | No pausan con el gancho | [E] |
| Tirones de más de 2 s | El reloj del servidor se queda atrás | [V previo] |
| Esperar o dormir del invitado | Ignorado (solo dueño) | [V] |

Nada de lo anterior **necesita** un mundo congelado para funcionar; solo pierden su "tiempo gratis".

---

## 8. Plan de pruebas priorizado (con Shadow)

**Requisitos comunes:** kit de la sesión 7 (`cdmp.p8-s7-host.ini` / `cdmp.p8-s7-shadow.ini`: `relay_fix`,
`share_field`, `observer_*`, `child_hold`, `character_hold`, `ui_pause_block`, `save_guard`), **sniffer con `record=1`
en los dos PC** (es la fuente principal), copia de la carpeta de partidas del anfitrión antes de empezar, y una
partida del anfitrión **desechable** para las pruebas de misiones. Hacerlas **después** de los puntos de la sesión 7
(S1, ESC, salir y volver), porque reutilizan su base.

Análisis tras cada prueba: `analyze_capture.py … summary/trips/scenes` en GBC y HBC, y un recuento por opcode de
los Acks que recibe la sesión 2 (los de §3–§5 son ToTarget o ToNear: hay que verlos en GBC).

| # | Prueba | Qué hacer | Esperado | Qué mirar / sondas |
|---|---|---|---|---|
| **G1** | Combate básico | Junto al anfitrión, el invitado ataca a un enemigo débil (bandido o animal hostil) hasta matarlo; el anfitrión no ataca | Golpes registrados (`VaryHPAck` en los dos); números de daño en el invitado; el enemigo fija al invitado (`CurrentTargetChangeReq` motivo `0x1D`/`0x18` hacia `0xA01…` en HBC); **pocos o ningún ataque normal** al invitado; al morir: `AddSubLevelExperienceAck` a la sesión 2, `LootDropCharacterRewardListAck` con destinatario el anfitrión, `UpdateQuestGaugeAck` (si es campamento) al anfitrión | HBC: `DoFrameEventReq` del enemigo con objetivo el invitado (cuántos por minuto); GBC: 2381, 2897, 2530. Ventaja: el anfitrión puede apuntar en voz alta qué ve |
| **G2** | Botín y recogida | Tras G1, el invitado intenta recoger el botín y saquear el cadáver; luego recoge una planta | Botín: puede fallar (recogida del anfitrión); planta: funciona | GBC: 3052→2822, 2198→2962, 3005 (`AutoPickUpItemToTargetFailAck`), 2255 |
| **G3** | Tienda | El invitado compra y vende un objeto barato | `StoreDataListAck`, `AddItemToInventoryAck`/`RemoveItemToInventoryAck` y cambio de dinero en el invitado; el anfitrión sin cambios | GBC 2146/2962/2481/2200; inventario del anfitrión igual |
| **G4** | Hablar sin misión | El invitado habla con un NPC sin misión y sale del diálogo | El diálogo abre y cierra; **sin "ver un objeto" ni personaje inmóvil** (`observer_scene_copy` copia el 2810) | Log del anfitrión: `ObserverStage [2810…]` y la línea de copia; GBC: `scenes A01…` cerrada |
| **G5** | Montura | El invitado llama a su caballo, monta, cabalga junto al anfitrión y desmonta | Funciona en el invitado; el anfitrión ve el caballo sin cierre; al desmontar puede verlo congelado | `child_hold`/`companion_probe` en el log del anfitrión; captura de cierres |
| **G6** | Recibir golpes, morir y reaparecer | El invitado se deja matar por un enemigo (o cae de altura) | Reacciones a golpes en el invitado; muerte; reaparición = **viaje**: o vuelve al campo compartido (2322 de `observer_milestone`, `LoadingCompleteReq`) o queda en un campo propio. Riesgo de carga atascada | Log: `ObserverMilestone [decision]`, `[owner 0x2BC8470]`; GBC `trips` (2174 … 2762); 2173 en GBC |
| **G7** | Distancia | El invitado se aleja >250 m del anfitrión en el mismo campo | NPC sin IA (quietos) alrededor del invitado; nuevos spawns sin animación | GBC: 2005 con control `0x90100000` y ningún 2223/2556 relevado para ellos |
| **G8** | Viaje rápido | El invitado viaja a otra región y vuelve | Campo propio (dueño él) y vuelta al compartido con su 2322 | Log de `field_share` (H1), `trips` |
| **G9** | Misiones (partida desechable) | El invitado acepta una misión secundaria; entra en una zona de misión **del anfitrión** | Su lista se actualiza; la zona dispara o avanza la misión **del anfitrión** | GBC 2808/3009; HBC 3009/3088/2346; anotar el diario de misiones de los dos. **No guardar** |
| **G10** | Esperar/dormir | El invitado intenta esperar o dormir | Ignorado sin error | HBC sin `SetFieldTimeAck` |

**Sondas opcionales (solo lectura, mid-hooks en `.rsrc`, nunca en `.xtls`)**, por si las capturas no bastan:

| Punto | Qué leería |
|---|---|
| `0x2B83D29` (tras `call 0x20DEE60`) | `[rbp+0x48]` = usuario destinatario del botín |
| `0x29432E3` (tras `call 0x2AFFF50`) | personaje que recibe `DeadActor` |
| `0x2C4A36F` (tras `vf40`) | `rax` = gestor de misiones elegido para la zona y la clave de la sesión |
| `0x2A5C6CB` (tras `vf48`) | gestor de secuenciadores elegido para un `StageBranchStateReq` del usuario 2 |
| `0x2BC89E0` | dueño del campo frente al que pide la hora |

---

## 9. Tabla de riesgos

| Id | Riesgo | Prob. | Impacto | Evidencia | Mitigación / siguiente paso |
|---|---|---|---|---|---|
| R1 | Los enemigos rodean al invitado sin ataques normales | alta | alto (combate sin gracia) | [V marioneta], [E usuario 2] | Medir en G1. Opciones sin `.xtls`: que el invitado controle los NPC de su zona (diseño nuevo, sin camino nativo encontrado) o una sonda/ajuste en `.rsrc` antes de la condición (por estudiar) |
| R2 | El botín de lo que mata el invitado va al anfitrión | alta | medio | **[V]** `0xE2BF850` | Aceptar en cooperativo o redirigir el destinatario en una entrada `.rsrc` (estudiar `0x2B83960`) |
| R3 | Las muertes y zonas del invitado avanzan misiones y campamentos del anfitrión | alta | medio/alto (estado del anfitrión) | **[V]** `0x2AFFF50`, vf40 | Aceptable si el anfitrión guarda; peligroso para la partida del invitado. Probar G9 con partida desechable |
| R4 | Las misiones del invitado no avanzan en el campo compartido | alta | medio | [V ruta], [E efecto] | Hacer misiones del invitado en su campo propio (§6.2) |
| R5 | Diálogos y escenas del invitado se quedan abiertos | media | alto (invitado bloqueado) | [V] 2810; `observer_scene_copy` hecho, sin probar en vivo | G4 |
| R6 | `SubTimelinePause` y `SetAvoidByCutScene` del invitado afectan al anfitrión | media | medio | [V ruta/finder] | Observar en G4; filtrar en el anfitrión si molesta |
| R7 | Reaparición del invitado atascada o en un campo propio | media | alto | [V] viaje de carga; criterio 6 sin probar | G6 después de salir/volver de la sesión 7 |
| R8 | NPC sin IA lejos del anfitrión | alta | medio | [V recuento], [E fuerte] | G7; aceptar una distancia máxima o dar control al invitado (diseño) |
| R9 | El autoguardado del usuario 2 deja datos suyos en la ranura en memoria del anfitrión | media | alto si se levanta el guardián | [E] P9 §5.1 | No levantar el guardián; implementar P9 (`Ng`) |
| R10 | La carga de la partida del invitado pisa precios o eventos globales del anfitrión | baja/media | medio | [S] | Leer cargadores `vf(0x50)` (P9 §3) |
| R11 | `itemNo` repetidos mezclan objetos en tablas del servidor | baja | medio | [S] | Vigilar en G3; extender `identity_fix` si aparece |
| R12 | Montura del invitado en el anfitrión: cierre o congelación al desmontar | media | alto (cierre) | [V previo] | `child_hold`/`character_hold`; G5 |
| R13 | El invitado recibe golpes que esquivó en su pantalla | media | bajo | [E] latencia | Ninguna (MMO) |
| R14 | Esperar o dormir del invitado no hace nada | segura | bajo | **[V]** `0x2BC89E0` | Documentar |
| R15 | Logros de Steam del invitado por `KillActor` en otro mundo | baja | bajo | [E] | Aceptado |
| R16 | Sin `relay_fix`, el invitado no recibe las animaciones ni las reacciones de los NPC (relevos `ExceptOneControl` vaciados en esta build) | segura con `relay_fix=0` | alto | [V A2] | Mantener `relay_fix=1` en los ini |

## 10. Pendientes estáticos cortos

1. Leer quién escribe la lista de rango `C+0x58` (está en `.xtls`; solo lectura) y si existe un camino `.rsrc`
   anterior a `0x22E0A30` donde se pueda decidir el estado "esperar".
2. Leer en `0x2B81410`/`0x2B85870` si la recogida del botín exige ser el destinatario.
3. Leer `0x2891CE0` (2173 al morir) para saber qué escribe en el gestor del anfitrión.
4. Leer la rama de `0x2947E00` que elige el punto de reaparición con el gestor `+0x138` del dueño.
5. Identificar las clases de los componentes `+0x138` y `+0xA8` (por su constructor) para nombrarlos con RTTI.
6. P9 §3: cargadores de datos globales.

---

## 11. Lecturas estáticas cerradas y diseño de embudos

10 de octubre de 2026 (segunda lectura). Cierra la lista del §10. Mismo método y mismas reglas: capstone sobre el PE
de solo lectura `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` (Python `-I`), sin Ghidra, sin tocar
`D:\RE\CrimsonDesert_2976\project`, sin lanzar el juego, sin commit. Ningún gancho propuesto cae dentro de `.xtls`: cuando una
función de `.rsrc` empieza por un `jmp` a `.xtls` (como `0x22E0A30` de F5), el punto propuesto es ese stub de `.rsrc` o la
llamada que lo precede. Scripts del scratchpad de la sesión (no se conservan): `d.py` (desensamblar con pdata), `xr.py` /
`callscan.py` (referencias), `qref.py` (punteros en vtables), `vfs.py`, `ctype.py`, `chain.py` (cadenas
`[x+0x68]→[+disp]→[+0x18]`), `own180.py` / `after.py` (búsquedas de dueño), `jt.py` (tablas de salto).

### 11.1 Bloqueo de rango: quién llena `C+0x58` y por qué excluye al usuario 2

**Estructura del componente de objetivo `C = [[A+0x68]+0x90]`** [V]:

| Campo | Qué es |
|---|---|
| `C+0x18+16t` | lista de **espera** por tipo de orden `t` (elementos de 8 B: `u32` clave, `u8` marca en `+4`; n en `+8`, capacidad en `+0xC`) |
| `C+0x58+16t` | lista de **rango** (mismo formato; crece con `0x3D4130`) |
| `C+0x98+16t` | atacantes (lo de antes, `0x221D030`; estructura aparte, no alimenta el rango) |
| `C+0x140` / `+0x148` | lista de **permisos de rango** (`u32` claves de atacantes) |

**Escritores** [V]. Todos son stubs `jmp` de 5 B en `.rsrc` hacia `.xtls`:

| Stub `.rsrc` → `.xtls` | Qué hace | Llamador único |
|---|---|---|
| `0x221CBD0` → `0xE69FF80` | `InsertAttacker(C, clave, t, marca)`: si la clave tiene permiso (`C+0x140`) va a `C+0x58+16t`; si no, a `C+0x18+16t` | `0x2217290` (SetTarget) en `0x2217B5A` (antes, `0x2217A35` llama a AddAggro `0x221D470`) |
| **`0x2220850` → `0xE6B74D0`** | **`GrantRankPermit(C, &clave)`**: añade el permiso y pasa la clave de `+0x18` a `+0x58` | **`0x221E110` en `0x221E2BB`** |
| `0x2220370` → `0xE6ADAC0` | quita el permiso y baja la clave de `+0x58` a `+0x18` (`0x221B9C0` → `.xtls 0xE6926B0`) | `0x221EF50` (`0x221F2DA`, condicionado por `vf188(5)` del objetivo en `0x221F29F`), `0x221F360` (`0x221F764`, por `vf188(5)` del propio E) |
| `0x221CE50` → `0xE6A2830` | mueve una clave entre tipos al cambiar el tipo de orden (desde `0x221ADD0`) | — |

**La condición que excluye al usuario 2** [V, comprobado de nuevo en esta lectura]. En `0x221E110(C_E, {X objetivo}, …)`,
alcanzado por AddAggro `0x221D470` → `0x221D750` → `0x221D8A0` (`0x221DC1D`/`0x221DC65`):

```
0x221E27C  mov dl,5
0x221E27E  mov rcx,rbx                 ; rbx = X (el objetivo)
0x221E281  call [rax+0x188]            ; vf188(5) de X = "lo controla este cliente"   (FF 90 88 01 00 00)
0x221E287  movzx ebx,al
0x221E2A8  test bl,bl
0x221E2AA  je 0x221E2C2                ; X remoto -> 0x43DF50: la clave va a un vector temporal que solo se libera
0x221E2AC  mov rax,[r14+8] ; rcx=[[rax+0x68]+0x90]   ; C_X
0x221E2BB  call 0x2220850              ; GrantRankPermit(C_X, &claveE)
```

En el cliente del anfitrión el Kliff del usuario 2 tiene `+0x90 ≠ usuario local` → `0x4FA530` le pone `+0xC8 = 0` →
`vf188(5)` falso → **ningún enemigo recibe permiso de rango sobre él**; se quedan en su `C+0x18` (espera) y
`AICondition_CheckAIBattleType` (`.xtls 0xC5DC280`) devuelve 2. Esto explica la sonda F0 (en G, `+0x18` lleno y `+0x58`
vacío). Antes del `vf188` hay comprobaciones que el usuario 2 también debe pasar (no gimmick `0x221E165`, bytes de estado
`0x221E188`/`0x221E1FC`, bit `&4` de su IA `0x221E1B6`, `0x17FA100` `0x221E209`, `0x22141E0` `0x221E22A`, `vf158`
`0x221E24A`) [E: no verificables sin el juego].

**¿Camino nativo?** **No** [V + E]:
- Una clave solo entra en `+0x58` con permiso, y el único productor de permisos (`0x2220850`) está detrás del `vf188(5)` del
  objetivo, venga de donde venga la llamada. La rama remota no manda ningún Req.
- En el servidor no hay IA de NPC (la lleva el cliente controlador); ningún Req/Ack escribe `C+0x58`, `C+0x140` ni `C+0x164`.
  `ChangeAiControlAck` 2508 y `OverriedOwnershipStateTypeyAck` 2168 cambian el control de E, no el de G (ya descartados).
- La única vía "nativa" sería que el **cliente del invitado controle a los enemigos que le atacan** (controlador = su
  usuario), y eso no existe sin traspaso de control (§3.2).

**Puntos de intervención (todos en `.rsrc`), por preferencia:**

1. **Parchear la llamada de `0x221E281`** (`FF 90 88 01 00 00` → `E8 rel32` a un thunk + `90`): el thunk llama al
   `vf188` original y devuelve 1 si `dl == 5` y `rcx` es el personaje del usuario 2. Igual en **`0x221F29F`** para que
   la baja por `0x221EF50` también quite el permiso. Variante: mid-hook en `0x221E29A` (`48 8B 47 08 48 8D 55 48`) que
   ponga `bl = 1` si `[r14+8]` es el usuario 2. Efecto: rango **nativo** para el usuario 2, que leen la condición de IA,
   `BattleRank`, `0x170D900`, los huecos `0x1EB2D10` y `0x221A940`. Riesgo bajo-medio (dos llamadas, el motor usa sus
   propios vectores y cupos). Primero contar (fase A) si se llega a `0x221E281` con X = usuario 2.
2. Llamar desde el mod a `0x2220850(C_G, &claveE)` para cada E en `C_G+0x18` (y `0x2220370` al irse): más frágil
   (bloqueos reentrantes del actor dentro de SetTarget, permisos huérfanos).
3. **F5 actual** (`aggro_test.cpp`, entrada de `0x22E0A30`): solo arregla `CheckAIBattleType`; `BattleRank`, `0x170D900` y
   `0x1EB2D10` siguen viendo el rango vacío. Queda como reserva.

### 11.2 Botín y recogida: no exigen ser el destinatario

**Al morir, `0x2B83960`** (componente `ServerLootDropActorComponent`, índice `0x19` = `comps+0xC8`, §11.5) [V]:
- `byte [this+0x6C] != 0` (`0x2B83C73`/`0x2B83CDA`): cadáver con saqueo; **no se calcula destinatario** (salta a
  `0x2B83F6D`).
- Si no: `0x2B83D24 call 0x20DEE60` → usuario dueño del campo → su `+0xD8` o, si no vale, `+0xD0` (personaje) →
  `0x1F3E020(tabla, muerto, personaje del dueño, …)` + `0x2B85500` arman la lista de botín **con las condiciones del
  dueño**. Los objetos se crean en el suelo con `0x2AB2090(…, [rsp+0x58] = &attacher vacío)`: el attacher se construye en
  `0x2B8412C..0x2B8414E` con actor `0` (`r13 = 0`). **Los objetos del suelo no llevan dueño.**

**Saquear el cadáver, `LootingDropFromDeadBodyReq` 3052** → `0x2A116B0` → cuerpo por clave (`0x2A827B0`) →
`0x2B81410([[cuerpo+0x68]+0xC8], &res, saqueador = [ctx])` [V]:
- Si el saqueador es tipo 4/5/6 (mercenario, montura) se usa su dueño (`[[r15+0x68]+0x118]+0x18`); si no, él mismo.
- `0x2B85870` vuelve a calcular la lista **para el saqueador** (`0x1F3E020(tabla, muerto, saqueador)` en `0x2B859F9`) y
  `0x2B829A0` la transfiere. En `0x2B81410`, `0x2B85870` y `0x2B829A0` **no hay ninguna comparación** con una clave de
  dueño o destinatario (`+0x90`, `vf160`/`vf170`, `[field+0x180]`); los `cmp` que hay son de datos estáticos (tipo de
  objeto `0x2B83035..0x2B83047`, `0x2B8300E` contra el tamaño de una tabla) [V primer nivel; E en llamadas más hondas].
- Recoger del suelo, `PushTargetToInventoryReq` 2198 → `0x2C46AB0` → `0x2B46EC0(inventario del que pide, …)`: tampoco
  compara dueño (solo tipo gimmick 7 y banderas del objeto) [V primer nivel].

**Conclusión:** el usuario 2 **puede** saquear y recoger. El dueño del campo solo decide las **condiciones** de la lista
que cae al suelo al morir (p. ej. objetos de misión del anfitrión); el saqueo de cadáver usa las del usuario 2. La
predicción "G2: el botín puede fallar" del §8 pasa a "debería funcionar". Riesgo R2 baja a **bajo**.

### 11.3 `UpdateSequencerGamePlayDataEventReq` 2173 al morir

Formato (15 B) [V captura `tl_080253`, `0x2A5C0F2..0x2A5C207`]: `u64 clave de secuenciador | u32 evento | u8 b1 | u8 b2 |
u8 b3`. Muerte del anfitrión: `98 01 00 00 01 00 10 A0 | 00 00 00 60 | 00 | 00 | 0F` → secuenciador
`0xA0100001_00000198` (la clave lleva **la clave del actor** en la mitad alta), evento `0x60000000`; a los 10 s, evento
`0x60000001`.

Manejador `0x2A5C090` [V]: `field = 0x17FA6C0([ctx])`, `mgr = field->vf48()` (`0x2A5C280`), y
`0x2891CE0(mgr, &res, clave, evento, b1, b2, b3)`. **El personaje que pide no se pasa.** `0x2891CE0` [V]:
- `evento == 0` → error `0x6CF76BC` (Nak 1013 al que pide), no escribe nada.
- `0x1F6C190(mgr, &s, clave)` busca el secuenciador **por clave** en el gestor del dueño. **Si no está → resultado 0, no
  hace nada** (`0x2891DA1`).
- Si está: resuelve el evento (`>= 0x80000000` índice en `[seq+0x58]+0x18`; si no, `0x255A8D0`), arma
  `{fuente = [mgr+0x18], dato}` y llama a `seq->vfA8` (si `b1 != 0`) o `seq->vfB0` (si `b1 == 0`).

**Efecto para el usuario 2** [V ruta, E efecto]: su 2173 lleva la clave `0xA010031F_00000198`. Solo actúa si **su**
secuenciador de muerte está registrado en el gestor del anfitrión (lo estará si lo creó una Req suya por vf48, p. ej.
`RegisterGlobalStageSequencerReq` 3046). En ese caso dispara el evento sobre **su** secuenciador, no sobre los del
anfitrión (la clave incluye su actor: no hay colisión). Si no está, no hace nada. **No corrompe ni avanza la escena del
anfitrión**; lo único raro es que la "fuente" del evento sea `[mgr+0x18]` del gestor del anfitrión [E: su personaje], que
importa solo si el gráfico de esa secuencia mira la fuente.

### 11.4 Punto de reaparición: mixto (posición propia, filtros del dueño)

`ReviveLoadingReq(2)` → `0x2947E00(…, tipo = 2)` [V]:
1. Al entrar, para todos los tipos: `0x2947F0E mov r8d,[rbx+0x180]` → `FindUser` → **`rdi = [[[dueño+0xD0]+0x68]+0xA8]+0x18`
   (gestor de secuenciadores del dueño)** y `0x2947F76 call vf40` → **gestor de misiones del dueño** (`[rsp+0xF0]`).
   `0x17EE020(muerto)` → `[[muerto+0xA0]+0xD0]` = el personaje del **propio** usuario que muere (`[rsp+0x238]`).
2. Tipo 2 = índice 1 de la tabla `0x2949898` → **`0x2948331`**:
   - `0x294843B call 0x1F759C0(gestor de secuenciadores del DUEÑO, &q)`: si una stage activa **del anfitrión** define
     punto de reaparición, se usa ese (`0x294846C test bl,bl`) [V ruta].
   - Si no: posición actual del **que muere** (`[[r15+0x68]+0x1A0]`, `0x4F8220`) → `0x2B03A60(field, &out,
     personaje propio, &params)`, que **vuelve a llamar a vf40** (`0x2B03AA2`) y, si la región tiene una misión que
     condiciona sus puntos (`+0x188` de los datos estáticos), mira **en la tabla hash del gestor de misiones del dueño**
     (`[r15+0x68]`/`+0x78`/`+0x80`, estado `byte +0x2C == 2`, `0x2B03BFE`) qué lista de puntos usar; luego elige el punto
     **más cercano a la posición del que muere** (`0x2B03C93..0x2B03CEA`).
   - Lo aplica con `0x2BC4950([[pj+0x68]+0x1A0], muerto, &pos)` y disuelve los mercenarios de su `+0x110` (`0x2B98600`).

**Conclusión:** el punto es **el más cercano al usuario 2**, pero las anulaciones por stage y los filtros por misión
salen del **progreso del anfitrión** [V ruta]. En la mayoría de sitios (sin stage ni misión que condicione) dará un punto
cercano y correcto. El tipo 2 no lee el `[field+0x180]` para nada más; el viaje posterior sigue siendo el de §3.4.

### 11.5 Componentes `+0x138` y `+0xA8`, y su equivalente por usuario

La tabla de componentes es `[actor+0x68]`, y `0x206A7A0(actor, &res, índice, &comp)` guarda `comp` en
`[actor+0x68] + índice·8` (`0x206A838..0x206A85B`) [V]. Los componentes de jugador los crea
**`ServerActorManager::vf38` = `0x2A7F320`** (vtable `0x5B25988`, ranura `0x38`), rama `r8b == 1` (`0x2A7F33A`) [V]:

| Desplazamiento | Índice | Clase (RTTI) | `+0x18` (creado por `vf18` base `0x1ABE250` con `this->vf140()`) |
|---|---|---|---|
| **`+0x138`** | `0x27` (`0x2A7FDAE`) | **`ServerQuestActorComponent`** (vt `0x5B1F8F8`; base `CommonQuestActorComponent`) | **`ServerQuestManager`** (vt `0x5B1FA48`; `vf140 0x290DCC0`, `0x290DD48`) |
| **`+0xA8`** | `0x15` (`0x2A7FBF5`) | **`ServerSequencerActorComponent`** (vt `0x5B2D7C0`; base `CommonSequencerActorComponent`) | **`ServerSequencerStageManager`** (vt `0x5B17A58`; `vf140` → `.xtls 0x10782330` → ctor `0x2878450`) |

Otros índices leídos en la misma función (sirven de comprobación con lo ya conocido): `0x17` Inventory `+0xB8`, `0x1A`
Store `+0xD0`, `0x1B` RepairItem `+0xD8`, `0x1F` QuestDialog `+0xF8`, `0x22` MercenaryClan `+0x110`, `0x23` Mercenary
`+0x118`, `0x24` Wanted `+0x120`, `0x28` Friendly `+0x140`, `0x29` Journal `+0x148`, `0x2A` Knowledge `+0x150`, `0x19`
LootDrop `+0xC8`.

**¿Hay uno por usuario?** **Sí** [V estructura + V captura]: los dos se crean en la rama de jugador de `0x2A7F320`, la
misma que crea `ServerQuestDialogActorComponent +0xF8`, que el usuario 2 ya usa con éxito (539/539 `SetQuestDialogAck` en
6B). Por tanto el personaje del usuario 2 tiene su propio `ServerQuestManager` y su propio `ServerSequencerStageManager`,
hoy sin usar en el campo compartido. Un embudo "por usuario" solo tiene que devolver los del personaje que pide.

### 11.6 Cargadores globales (P9 §3)

**No cerrado en esta lectura** [S]. La lectura de los cargadores `vf(0x50)` (eventos globales `GlobalGameEventSaveData`,
precios `TradeItemPriceRate`, `FactionNode*`, `NPCSchedule*`/`NPCScheduleStageManagerSaveData`,
`FactionSpawnStageManagerSaveData`, hora `TransformSync+0x5D0` → `exe+0x6A6D238`) se lanzó en paralelo y no terminó a
tiempo para este informe. Sigue valiendo lo de P9 §3: con campos separados no bloquea; antes de cargar una partida
**distinta** para el usuario 2 hay que leer, para cada cargador, si escribe en un global (RVA e instrucción) y si lo
condiciona "es dueño del campo". Lo único ya verificado es la hora: `0x2BC0A30` solo si es dueño y `0x219D7F0` sin mirar
dueño (P9 §3). Herramientas: `C:\Emulador CD\build\research\optd\e3\globscan.py`, `comps.txt`, `compload_glob.txt`.

### 11.7 Diseño: hacer por usuario los tres embudos

**Inventario de consumidores** [V]:

| Embudo | Consumidores |
|---|---|
| `vf40 0x2B00C70` (`ServerQuestManager` del dueño) | 15 llamadas directas en `.rsrc` (`0x28069A0`×2, `0x2808970`, `0x280EC30`, `0x2818AE0`×2, `0x2879440`, `0x287C200`, `0x2880BC0`, `0x2896C60`, `0x28CE7F0`, `0x2947E00` revivir, `0x2B03A60` punto de reaparición, `0x2BC12A0`, `0x2BC7670`) + 1 en `.xtls` (`0x102CB550`) + la ranura `0x5B29AA0` de la vtable `ServerField` (`MissionTriggerInOutReq`, `UpdateTraceMissionReq`). **No hay copias en línea** que lean `+0x138` del dueño |
| `vf48 0x2B00CE0` (`ServerSequencerStageManager` del dueño) | ranura `0x5B29AA8` (0 llamadas directas): las 11 Req de escena del §1. **Además, 13 copias en línea (11 funciones)** del mismo cálculo (`FindUser 0x2A82590([field+0x180])` → `+0xD0` → `+0x68` → `+0xA8` → `+0x18`): `0x2791CD0` (`ICreateServerActorDesc::vf18`, crear actores atados a stages), `0x27EA160`/`0x27EE3E0` (spawns de pool), `0x28F9CC0` y `0x2947E00`×2 (revivir), `0x2911300`, `0x292D1F0`, `0x2AB97D0`, `0x2B01D20` (`PreemptActionPointReq` 2382), `0x2BB4020`×2 (`ServerSequencerPlayDataActorComponent::vf30`), `0x2BC7670` (2322). `0x2911300` además exige dueño == usuario del actor (`0x29113CB`) |
| `[field+0x180]` directo | `FindUser 0x2A82590`: 15 llamadas en `.rsrc` + 2 en `.xtls`, **las 15 de `.rsrc` usan la clave del dueño** (las 2 de `.xtls`, sin leer); `0x2AFFF50` (dueño → personaje, vía `0x2A829D0`): **53** llamadores (entre ellos `DeadActor 0x29432DE`); `0x20DEE60` (stub → `.xtls 0xE2BF850`): 17 llamadores (botín `0x2B83D24`); comparaciones sueltas (`SetFieldTime 0x2BC89E0`, 2322 `0x2BC8470`, viaje `0x2BC0A78`) |

**Pieza común: el "sujeto" de la Req.** Todas las Req y temporizadores pasan por `ExecuteTroc 0x2774B20(int* res,
Troc*, TrocParams*)` (`48 89 4C 24 08 53 48 83 EC 20`), con `[params]` = personaje de la sesión. Un gancho de entrada
y salida ahí puede guardar en una variable del hilo del servidor "sujeto = personaje del usuario 2" mientras dura la
Req, solo si `u32 [pj+0x90]` es la clave del usuario 2 y su campo es el compartido. Fuera de ese ámbito todo sigue igual.
Las muertes que causa el usuario 2 (`DoFrameEventReq` → daño → muerte) ocurren **dentro** de su Req, así que el sujeto
también vale para `DeadActor` y botín [E fuerte: la muerte se procesa de forma síncrona en el manejador del golpe].

**Opciones, de la más nativa a la menos, con riesgos:**

| Rango | Embudo | Cómo | Dónde | Riesgo |
|---|---|---|---|---|
| **1** | **vf40** | Gancho de entrada en `0x2B00C70` (`53 48 83 EC 40` + `44 8B 81 80 01 00 00`): si hay sujeto, devolver `[[[sujeto+0x68]+0x138]+0x18]`; si no, el original | `.rsrc`, un solo punto (cubre la vtable y las 15 llamadas directas) | **Bajo.** No hay copias en línea. Efecto: las zonas de misión, el seguimiento y el filtro de reaparición usan las misiones **del usuario 2**. Coherencia de mundo P9 §3: sus misiones pueden pedir stages de su mundo [S] |
| **2** | `[field+0x180]` para **DeadActor** y **botín** | Gancho de entrada en `0x2AFFF50` (`48 89 5C 24 08 …`) filtrado por dirección de retorno `0x29432E3`, y en el stub `0x20DEE60` filtrado por `0x2B83D29`: con sujeto, devolver el usuario/personaje del sujeto | `.rsrc`, dos puntos con filtro | **Bajo-medio.** Contadores de misión y campamento por muertes pasan al usuario 2; las condiciones del botín de suelo también. Los demás 52 + 16 llamadores no cambian |
| **3** | **vf48** | Gancho de entrada en `0x2B00CE0` (mismos bytes que vf40) **más** las 13 copias en línea. Para las copias, lo más corto es un gancho de entrada en `FindUser 0x2A82590` que, con sujeto y `clave == [field+0x180]` del campo del sujeto, la cambie por la del usuario 2 | `.rsrc`, dos puntos | **Medio-alto.** (a) Hay que redirigir **todo** a la vez: si una escena se crea en un gestor y se busca en otro, se pierde (lo que hoy tapan `observer_scene_copy`/`observer_milestone`, que leen el gestor del anfitrión, tendría que revisarse). (b) `FindUser` bajo sujeto también cambia los spawns de pool y el `ICreateServerActorDesc` lanzados por Req del usuario 2: sus NPC quedarían atados a stages del usuario 2. (c) Ganancia: escenas, diálogos, `SubTimelinePause`, `SetAvoidByCutScene` y 2810 serían del usuario 2 de forma nativa (R5, R6) |
| **4** | Los tres | **Cambio temporal de dueño**: `[field+0x180] = clave del usuario 2` durante su `ExecuteTroc` y restaurar al salir | `.rsrc`, un punto | **Alto.** Además de lo anterior: `SetFieldTime` aceptaría al usuario 2, los spawns lanzados en su Req tendrían **controlador** = usuario 2 (su cliente pasaría a simular esos NPC), la lógica de 2322 y viajes (`0x2BC8470`, `0x2BC0A78`) vería otro dueño. Útil solo como experimento corto |
| — | Los tres | Campo propio del usuario 2 (§6.2) | nativo | Sin riesgo, pero no es cooperativo |

**Recomendación:** empezar por **1** (vf40) y **2** (DeadActor + botín), que son puntos únicos, con filtro y sin
copias en línea, y dejar **3** (vf48) para cuando se haya probado G4/G6 con la ruta actual de observadores. Todo con
interruptor en el ini y una sonda de solo lectura previa que cuente, por Req del usuario 2, cuántas veces se pasa por
`0x2B00C70`, `0x2B00CE0`, `0x2A82590`, `0x2AFFF50 (ret 0x29432E3)` y `0x20DEE60 (ret 0x2B83D29)`.

### 11.8 Cambios a los riesgos del §9

| Id | Cambio |
|---|---|
| R1 | Causa exacta: `vf188(5)` del objetivo en `0x221E281`. Corrección preferida: parche de esa llamada (y `0x221F29F`) en vez de F5 |
| R2 | El botín **no** es exclusivo: baja a "bajo". El dueño solo fija las condiciones de la lista de suelo al morir |
| R3 | Se puede pasar al usuario 2 con la opción 2 del §11.7 |
| R7 | El punto de reaparición es el más cercano al usuario 2, con filtros de stage y misión del anfitrión |
| R10 | Acotado a tres vías (ver §11.9) |
| Nuevo R17 | Si se hace vf48 por usuario sin las 13 copias en línea, escenas perdidas (creadas en un gestor, buscadas en otro) |

### 11.9 Cargadores globales (P9 §3): cerrado con una lectura estática posterior

**Resumen [V]:** ningún cargador `vf(0x50)` escribe eventos globales, precios, facciones ni horarios de PNJ en estado de todo el proceso; todo va a objetos del componente o del campo. Aun así, una partida distinta del usuario 2 afectaría al anfitrión por tres vías:

| Vía | Evidencia | Riesgo | Punto mínimo (solo `.rsrc`) |
|---|---|---|---|
| Hora del mundo, global en `0x6A6D238` | `0x2BC0A30` la escribe en `0x2BC0C50` si el viajero es el dueño del campo (`cmp [field+0x180]` en `0x2BC0A78`) [V]. `0x219D7F0` (llamada en `0x2BCB0CA`) no filtra [E, de E3] | Alto (R1, pero con otra hora) | Entrada de `0x2BC0A30`, o el `jne` de `0x2BC0A7E`, para el usuario 2. Además, el `call 0x219D7F0` en `0x2BCB0CA` o la alternativa ya prevista `LoadingCompleteReq 00` |
| Envíos de eventos y precios: 0xC03 (`VaryTradeItemPriceAck`) y 0x82D | Salen de `0x29014D0` → `0x29BD5E0` / `0x29BD000` con `ToWorldSendTargetFinder` (`vt 0x5B25318`), cuya vf8 en `.xtls` recorre **todos** los usuarios de `0x6D696A8` → `+0x1F8` [V]. En el servidor no hay mezcla: cada campo tiene su `ServerGlobalGamePlayEventManager` en `field+0x88` [V] | Medio: el cliente del anfitrión recibiría precios y eventos del campo del invitado [E] | Filtrar en la entrada de `0x29BD5E0` / `0x29BD000` o en las vf 0x10/0x18/0x20 de `vt 0x5B1F278`, u omitir el `call 0x29014D0` en `0x28C0712` para el usuario 2 |
| Global transitorio `0x6C8CD98` (versión de `CharacterStatusSaveData`, escrita en `0x1791B98`) | Se lee justo después, en `.xtls 0x10427572` [V] | Bajo: solo con dos cargas a la vez | Ya lo impide la guarda de E3 §1.1 |

**Otros cargadores:**
- Las facciones (`ServerFaction`, `[comp+0x18]`) y los horarios de PNJ (`ServerSequencer` → `ServerNPCScheduleSpawnManger`) son por usuario [V en la creación].
- EquipSlot, GameEventHandler y Wanted están ofuscados en `.xtls` y no se leyeron [E].
