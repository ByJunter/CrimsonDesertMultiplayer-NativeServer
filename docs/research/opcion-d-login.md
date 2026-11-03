# Opción D: el guion de login de un cliente (capturas 112627 y 105149)

Build **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`). Direcciones en RVA (`exe+…`).
Análisis de solo lectura: capturas `captures/cdmp_packets_20261003-112627.bin` (formato CDPK v2,
paquetes completos) y `-105149.bin` (CDPK v1, paquetes cortados a 1.024 B), su log
`cdmp_20261003-105149.log`, la tabla `docs/research/data/troc_messages_2976.csv` y el exe en disco
(capstone con `build/research/cddis.py`). El juego no se ejecutó.

Leyenda: **[V]** visto en captura, **[E]** leído en el desensamblado, **[S]** suposición.

Scripts y salidas en `C:\Emulador CD\build\research\optd\` (ver apéndice B). Guion legible por
máquina: `docs/research/data/login_script_112627.json`.

---

## Resumen

1. **El login no acaba en `GameLoadingStartReq`: hay dos ciclos de carga completos** [V].
   - Ciclo 1: `LoginCharacterToFieldServerReq` → `MoveFieldAck` **tipo 3** a una escena de
     aparcamiento en (0, 1000, 0) → `MoveFieldCompleteReq` → `LoadingCompleteReq` →
     `LoginCharacterToGameReq`.
   - Ciclo 2: `GameLoadingStartReq` → `MoveFieldAck` **tipo 1** a la posición guardada →
     `MoveFieldCompleteReq` → **la ráfaga grande** (42.000 Ack en 112627, 76 `ActorLoadDataAck`) →
     `LoadingCompleteReq` → juego.
2. **Las Req de login son casi todas constantes** [V]. Entre las dos capturas solo cambian:
   - la clave RSA de `ConnectCryptogramToServerReq`, que se genera en cada arranque;
   - la **ranura de guardado** de `LoginCharacterToFieldServerReq` (u32 en el offset 9: **1** en
     112627, **0** en 105149). Queda confirmado que ese u32 es la ranura [V + E].
   - `LoginUserToFieldServerReq`, `MoveFieldCompleteReq`, `LoadingCompleteReq`,
     `LoginCharacterToGameReq` y `GameLoadingStartReq` son idénticas byte a byte.
3. **Para el usuario 2 hay que reescribir muy poco:**
   - la **cuenta u64** de `LoginUserToFieldServerReq` (el anfitrión usa 0);
   - la **clave del personaje** (`0xA0100001` en el anfitrión) en las pocas Req opcionales que la
     llevan (`RegionInOutReq`, `OnDemandEquipDockingGimmickSpawnReq`…).
   - Las Req imprescindibles del guion **no llevan ninguna clave de actor**: el servidor deduce el
     actor de la sesión y del byte `slot` de la cabecera [E].
4. **Tres detalles del servidor que el inyector tiene que respetar** [E] (`OnRecvPacket`
   `0x2773C20`):
   - **el canal de la trama:** tiene que ser igual al byte `Troc+0x14`. Vale **2** para
     `LoginUserToFieldServerReq` y **0** para el resto del guion;
   - **sin usuario en la sesión solo pasan las Req con bit 0 en `flags_16`.** De las del guion,
     solo `ConnectCryptogramToServerReq` y `LoginUserToFieldServerReq` (`0xFF`);
   - **la cola de diferidas indexa por la clave del usuario o, si no hay usuario, por `*(u32*)
     (sesión+0x88)`.** La sesión 2 necesita su propio id.
5. **Varias cosas que daban por hechas `segunda-sesion.md` §1.1 y `OPCION-D-EMULADOR.md` §2.3 no
   son exactas** (ver §9.1). Por ejemplo, `ConnectCryptogramToServerAck` no va todo a ceros, las dos
   capturas no son idénticas, y la ráfaga de `ActorLoadDataAck` llega con el **segundo**
   `MoveFieldCompleteReq`.
6. **Guion mínimo:**
   - **E2:** 2 Req;
   - **E3:** 8 Req obligatorias (más 8 tipos opcionales).
   - El detalle está en §8 y en el JSON.

---

## 1. Fases del login

Hitos tomados de las dos capturas. t = segundos desde la trama `1001` [V].

| Fase | Empieza con | t 112627 | t 105149 | C>S | S>C (112627) |
|---|---|---|---|---|---|
| F0 conexión | `1001` (lo consume el cliente) | 0,000 | 0,000 | 2 | 2 |
| F1 lobby | `LoginUserToFieldServerReq` | 0,059 | 0,061 | 1 | 4 (105149: 22, con `ServerTimeInfoAck` cada 5 s) |
| F2 carga del personaje | `LoginCharacterToFieldServerReq` | 3,437 | 90,454 | 1 | 724 (63 tipos) |
| F3 el cliente carga | `MoveFieldAck` #1 (tipo 3) | 4,277 | 92,179 | **0** | 4 |
| F4 escena de aparcamiento (0, 1000, 0) | `MoveFieldCompleteReq` #1 | 15,017 | 103,562 | 162 | 118 |
| F5 | `LoadingCompleteReq` #1 | 33,256 | 121,517 | 14 | 3 |
| F6 | `GameLoadingStartReq` | 37,318 | 125,564 | 5 | 30 |
| F7 carga de la zona real | `MoveFieldCompleteReq` #2 | 42,811 | 131,082 | 1.747 (105149: 3.810) | 42.118 (105149: 13.513) |
| F8 juego | `LoadingCompleteReq` #2 | 54,909 | 143,165 | ~60/s | — |

- En 105149 el usuario estuvo 90 s en la pantalla de título antes de elegir partida. En 112627,
  solo 3,4 s.
- El log del mod (105149) confirma la escena de F4:
  - `Local player: loading (actor parked at 0,1000,0)` desde 10:53:41;
  - `in world` a las 10:54:17, que coincide con `LoadingCompleteReq` #2.

---

## 2. Todas las Req C→S del login

La lista completa, en orden y con el payload entero, está en
`build/research/optd/reqs_112627.tsv` y `reqs_105149.tsv`. Cubre todas las C→S desde la trama 1001
hasta 10 s después de `LoadingCompleteReq` #2; la columna `fase` dice a qué fase pertenece cada una.

- **F0-F6** (186 Req): la tabla del apéndice A, en orden y con las rachas agrupadas.
- **F7** (1.747 Req) y **F8**: la tabla por tipo de §2.2. Casi todas son simulación de NPC y
  gimmicks del propio cliente.

La columna "dif." es `flag_1c` (1 = diferida por la cola `ServerNet+0x38`).

### 2.1 Las Req de login y carga (las únicas imprescindibles)

| # | t 112627 | Mensaje | op | slot | len | dif. | payload (hex completo) | ¿= 105149? |
|---|---|---|---|---|---|---|---|---|
| 0 | 0,000 | `1001` (no es Troc) | 1001 | — | 202 copiados | — | `01 "127.0.0.1" 00 0f …` + punteros del heap | no (punteros) |
| 1 | 0,045 | `ConnectCryptogramToServerReq` | 2319 | -1 | 286 | 1 | `04` \| `04000000 5400 4500 4d00 5000 0000` ("TEMP") \| `00` \| `3082010a0282010100…0203010001` (en el JSON) | no: la clave RSA cambia en 255 de 286 bytes |
| 2 | 0,059 | `LoginUserToFieldServerReq` | 2490 | -1 | 19 | 1 | `00000000000000000000000000000000000000` | **sí** |
| 3 | 3,437 | `LoginCharacterToFieldServerReq` | 3093 | -1 | 19 | 1 | `04 ffffffffffffff7f 01000000 d4ab5441 4242` | 1 byte distinto: offset 9, la ranura (1 frente a 0) |
| 4 | 15,017 | `MoveFieldCompleteReq` | 2549 | 0 | 1 | 1 | `00` | **sí** |
| 5 | 15,034 | `SceneLevelLoadCompleteSyncSceneObjectListReq` ×9 | 2598 | 0 | 8 | 1 | `03000000 ec635e2a` … | **sí** (las 9) |
| 6 | 16,814 | `OnDemandEquipDockingGimmickSpawnReq` | 2249 | 0 | 17 | 1 | `04 010010a0 00000100 02000f00 14001700` | **sí** |
| 7 | 33,256 | `LoadingCompleteReq` | 2445 | 0 | 1 | 1 | `01` | **sí** |
| 8 | 33,272 | `LoginCharacterToGameReq` | 2601 | 0 | 0 | 1 | (vacío) | **sí** |
| 9 | 37,318 | `GameLoadingStartReq` | 2437 | 0 | 0 | 1 | (vacío) | **sí** |
| 10 | 37,381 | `OnDemandEquipDockingGimmickSpawnReq` | 2249 | 0 | 17 | 1 | igual que #6 | **sí** |
| 11 | 42,811 | `SyncStageCandidatePostionReq` | 2100 | 0 | 1.549 | 1 | `04` + posiciones f32 | no (posiciones) |
| 12 | 42,811 | `MoveFieldCompleteReq` | 2549 | 0 | 1 | 1 | `00` | **sí** |
| 13 | 42,829 | `RegionInOutReq` ×5 (personaje) | 3067 | 0 | 8 | 1 | `010010a0 0100 01 00`, regiones 1, 4, 13, 10000, 10011 | depende de la zona |
| 14 | 42,832 | `SceneLevelLoadCompleteSyncSceneObjectListReq` ×65 | 2598 | 0 | 8 | 1 | `01000000 <hash>` | 40 de 65 (depende de la zona) |
| 15 | 42,879 | `OnDemandEquipDockingGimmickSpawnReq` | 2249 | 0 | 17 | 1 | igual que #6 | **sí** |
| 16 | 54,909 | `LoadingCompleteReq` | 2445 | 0 | 1 | 1 | `01` | **sí** |
| 17 | 54,934 | `LoadMercenaryStatReq` | 2094 | 0 | 0 | 1 | (vacío) | **sí** |
| 18 | 54,934 | `GameOptionDataReq` | 2604 | 0 | 1 | 1 | `00` | **sí** |
| 19 | 54,934 | `UpdateStageChartEventReq` ×12 | 2591 | 0 | 17 | 1 | `4d010000 …01` | 4 de 12 |
| 20 | 54,979 | `RefreshUIFilterGroupDataReq` | 2683 | 0 | 0 | 1 | (vacío) | **sí** |
| 21 | 61,941 | `EchoMoveSessionIDReq` | 2624 | 0 | 12 | 0 | `040010b0 01000000 00000000` | **sí** (es la respuesta a `ChangeMoveSessionIDAck`) |

- **El byte `slot` de la cabecera** [V]:
  - vale **-1** (el propio usuario) hasta `LoginCharacterToFieldServerReq`;
  - vale **0** (el personaje 0 del usuario) desde `MoveFieldCompleteReq` #1.
- **El orden de los tiempos es fijo** [V]:
  - el cliente nunca manda nada entre `MoveFieldAck` y su `MoveFieldCompleteReq` (11 s en F3);
  - en F7, el `MoveFieldCompleteReq` llega 5,5 s después del `MoveFieldAck` #2.

### 2.2 El resto de Req del login: simulación del propio cliente

Recuento por tipo en F4 y F7 de 112627 (entre paréntesis, 105149). "Var." indica qué cambia entre
las dos capturas: comparé la Req n-ésima de cada tipo en orden, con `varoff.py` en F4-F6 y
`cmp_phase.py` en F7.

| Tipo | F4 | F7 | Var. | Qué es |
|---|---|---|---|---|
| `GimmickExecuteFrameEventReq` 3082 | 67 | 231 (1.135) | clave de gimmick `0xB01000xx` en +0; tiempos y hashes en +12..30, +113, +126, +136, +176 | gimmicks del personaje (los "Game Mechanic" del equipo) y del mundo |
| `UpdateGimmickReq` 2758 | 27 | 49 (179) | lo mismo | ídem |
| `GimmickBranchStateReq` 2609 | 8 | 27 (147) | +0 clave | ídem |
| `UpdateGimmickBodyMotionTypeReq` 2774 | 6 | 8 | +0 clave | ídem |
| `GamePlayTriggerInOutReq` 3068 | 17 | 18 | +0..3, +32..59, +76..83, +104..123 | disparadores; uno por segundo en F4 |
| `StartActionReq` 2223 + `ProcessAfterBeginFrameEventReq` 2189 | 7 + 7 | 185 + 185 | +36.., +105.., +142.., +254.., +714 (tiempos y posiciones) | acciones del personaje (F4) y de los NPC (F7) |
| `UpdateSequencerGamePlayDataEventReq` 2173 | 4 (+1 en F5) | 9 | **ninguno**; contador en +8 (00…04) | secuenciador de la escena de aparcamiento |
| `ChangeDrowningStateReq` 2739 | 2 | 1 | ninguno | `010010a0 00 01/00` |
| `ChangeAIStateReq` 2734 | 2 | 202 (313) | ninguno en F4 | IA (en F7, de los NPC: `0xB01000xx`) |
| `MoveActorReq` 2556 | 2 | 121 (200) | tiempo y posición | |
| `SyncWeatherStateReq` 2560 | 1 | 0 (1) | +8, +16..20 | tiempo atmosférico |
| `RegionInOutReq` 3067 | 0 | 419 (1.057) | clave y región | regiones de cada actor que entra en la zona |
| `SetQuestDialogReq` 2857 | 0 | 98 (25) | clave del mercenario o NPC | 1:1 con `SetQuestDialogAck` |
| `StartAIFunctionReq`, `UpdateAIDestinationReq`, `CurrentTargetChangeReq`, `DoFrameEventReq` | 0 | 41, 17, 2, 8 | — | IA de los NPC que simula el cliente |
| `ActivateSpawningPoolReq` 2424, `AutoSpawnRoadReq` 2550, `TerrainRegionVoxelSpawnReq` 2327, `SceneLevelOnDemandGimmickSpawnReq` 2928 | 0 | 1, 13, 1, 7 | posición y hashes | **spawns que pide el cliente** al cargar la zona |
| `SyncElementalGoalStatReq` 2286, `SyncStageCandidatePostionReq` 2100 | 0 | 32, 4 | — | periódicos |

**Constantes y variables, resumen** [V]:

| Constantes byte a byte | Variables |
|---|---|
| `LoginUserToFieldServerReq`, `MoveFieldCompleteReq` (×2), `LoadingCompleteReq` (×2), `LoginCharacterToGameReq`, `GameLoadingStartReq`, `LoadMercenaryStatReq`, `GameOptionDataReq`, `RefreshUIFilterGroupDataReq`, `SceneLevelLoadComplete…` de F4 (×9), `OnDemandEquipDockingGimmickSpawnReq`, `ChangeDrowningStateReq`, `UpdateSequencerGamePlayDataEventReq`, `GimmickBranchStateReq` (salvo la clave), `EchoMoveSessionIDReq` | **Clave RSA** (2319); **ranura** (3093 +9); **claves de actor** `0xA0100001` (personaje), `0xB01000xx` (gimmicks, NPC, mercenarios) en casi todas las de F4-F8; **tiempos y contadores** (`StartActionReq`, `Gimmick*`, `GamePlayTriggerInOutReq`); **posiciones** (`MoveActorReq`, `SyncStageCandidatePostionReq`, `SyncWeatherStateReq`); **hashes de escena y regiones** (`SceneLevelLoadComplete…` y `RegionInOutReq` de F7: dependen de dónde se cargue) |

---

## 3. Qué responde el servidor a cada Req

Ventana: desde la Req hasta la siguiente Req clave (`resp.py`) y, en detalle, `window.py` [V].

| Req | S→C en respuesta (112627) | Tiempo |
|---|---|---|
| `1001` | `ConnectToServerAck` 1002 (lo **fabrica el cliente**) | 0 ms |
| `ConnectCryptogramToServerReq` | `ConnectCryptogramToServerAck` 2840 (325 B) | 3,4 ms |
| `LoginUserToFieldServerReq` | `ServerTimeInfoAck` (+6 ms); después, a la vez a +184 ms: `MainUserDataStartAck` (`0x90100000`), `CharacterLobbyDataAck` ("Kliff", `characterNo` = `0x7FFFFFFFFFFFFFFF`) y `MainUserDataEndAck` | 184 ms |
| `LoginCharacterToFieldServerReq` | `ClearCharacterLobbyDataForSinglePlayAck` (+7 ms); ~720 Ack de estado (442 `StoreDataListAck`, 80 `UpdateStageListAck`, 36 `MercenaryEquipmentListAck`, 23 `ApplyBuffAck`, 21 `MercenaryDataListAck`…); **`MainPlayerDataStartAck`** (+317 ms, da la clave `0xA0100001`); `PrivateActorMoveAck`, `SetMoveFieldAck`(1), `MainPlayerDataEndAck` (+337 ms); **`SetMoveFieldAck`(1) + `MoveFieldAck` tipo 3** (+840 ms) | 0,84 s |
| `MoveFieldCompleteReq` #1 | en 3 ms: `VaryExpandedNoSaveInventorySlotAck`, 9 `StoreItemSellDataListAck`, buffs, `VaryHPAck`, 49 `StoreDataListAck`, **`ActorLoadDataAck` ×2** (personaje `0xA0100001`, 12.803 B, y "Game Mechanic" `0xB0100065`, 18.973 B), `SyncSceneObjectAck`, `SetMoveFieldAck`(0), **`MoveFieldCompleteAck`**, `MoveFieldCompletePostAck`, `OpenPartyAck`, facciones, `PlayAllPreLoadSequencerAck` | 3 ms |
| `SceneLevelLoadComplete…` ×9 (F4) | `SyncSceneObjectAck` | — |
| `OnDemandEquipDockingGimmickSpawnReq` (F4) | `SyncSceneObjectAck` ×12 + **`ActorLoadDataAck` ×6** ("Game Mechanic" `0xB0100066`…`6B`: el equipo acoplado) | 11 ms |
| `LoadingCompleteReq` #1 | `LoadingCompleteAck` (`010010a0 01 <u32> 00000000`) | 2,6 ms |
| `LoginCharacterToGameReq` | nada propio | — |
| `GameLoadingStartReq` | 7 `UnloadSyncSceneObjectAck`, 10 `UnapplyBuffAck`, `SpawnOrUnspawnHousingGimmickAck`, `UpdateFieldGimmickSaveData*`, `SetMoveFieldAck`(1), **`MoveFieldAck` tipo 1** con la posición guardada | 21 ms |
| `MoveFieldCompleteReq` #2 | en 2 ms: buffs, **`ActorLoadDataAck` ×2** (personaje 12.807 B y "Game Mechanic" `0xB010006C`), `SyncSceneObjectAck`, `SetMoveFieldAck`(0), **`MoveFieldCompleteAck`**. Después: 189 `EraseNotNoticeChallengeMissionAck`, `UpdateFieldGimmickSaveDescListAck`, misiones, `NotifyQuestSaveDataAck`, **`ChangeGameLevelStateAck`** (2.431 B) y `MoveFieldCompletePostAck` (+72 ms). Luego los mercenarios: `RefreshMercenaryDataAck` + `JoinPartyAck` + `ActorLoadDataAck` de `0xB0100002`/`3`/`4` (+88 a +101 ms). Por último, 35.827 `UpdateStageStateAck`, 4.977 `SyncSceneObjectAck`, 334 `CreateFactionSpawnStageAck` y **72 `ActorLoadDataAck` de NPC y gimmicks** (de 43,2 a 54 s) | 2 ms a 12 s |
| `LoadingCompleteReq` #2 | `LoadingCompleteAck` | 11 ms |
| `GameOptionDataReq` | `GameOptionDataAck` + 12 `UpdateStageChartEventAck` | 2 ms |
| `AITeleportReq` (mercenario, F8) | `MoveForceAck` + **`ChangeMoveSessionIDAck`** (`040010b0 00001090 01000000 00000000`) → el cliente responde `EchoMoveSessionIDReq` | 5 ms + 13 ms |

**`ActorLoadDataAck` durante el login** [V]:

| | F4 | F7 | F8 (10 s) |
|---|---|---|---|
| 112627 | 8 (1 personaje + 7 "Game Mechanic") | 76 (personaje, 3 mercenarios, 33 "Game Mechanic", 19 "Bleed Bandit", animales, carro…) | 2 |
| 105149 | 8 | 165 | 35 |

**Claves** [V]:

| Rango | Qué es | Cuándo aparece |
|---|---|---|
| `0x90100000` | usuario | `MainUserDataStartAck` y todo lo que va al usuario |
| `0xA0100001` | personaje | desde `MainPlayerDataStartAck` |
| `0xB0100002`-`04` | mercenarios o compañeros | `JoinPartyAck` en F7 |
| `0xB0100065`-`6B` | "Game Mechanic" de la escena de aparcamiento | F4 |
| `0xB010006C` en adelante | gimmicks y NPC de la zona | F7 |

- Solo aparecen **una** clave `0x9010…` y **una** `0xA010…`.
- Valores sueltos como `0x9010FFFF` o `0xA010FFFF` en F2 son datos, no claves.

**Formato de `ActorLoadDataAck`** [V]:
- **Actores del usuario** (`+0 = 0x09`):
  - `+3` clave del usuario dueño;
  - `+0x0E` clave del actor;
  - `+0x12` usuario;
  - `+0x16` índice (0 = personaje, 1-3 = mercenarios);
  - `+0x17` u64 `characterNo`;
  - `+0x1F` usuario.
- **NPC y gimmicks:**
  - `+3` clave;
  - `+0x14` **siempre `0x90100000`** (en las 1.198 cargas de NPC de la captura);
  - `+0x20` wstring con el nombre.
- **Para P10:** ese `+0x14` podría ser el usuario que controla el NPC o el destinatario. Con un solo
  usuario no se puede distinguir [S]. Comprobarlo en E3: ¿qué valor trae una carga de NPC enviada a
  la sesión 2?

---

## 4. Formato de los campos

### 4.1 `ConnectCryptogramToServerReq` 2319 (vf2 `0x2A21670`)

| Offset | Tipo | Valor | Fuente |
|---|---|---|---|
| 0 | u8 | `04` = tipo. Solo con 4 se procesa | [E] `cmp al,4` en `0x2A219B9` |
| 1 | wstring | u32 len (≤ 0x100) + chars + u16 0: `"TEMP"` | [E] + [V] |
| 15 | u8 | `00` | [E] |
| 16 | u8[] | el resto: DER `SubjectPublicKeyInfo`/RSA-2048 (270 B, `30 82 01 0a … 02 03 01 00 01`) | [V] |

- El servidor procesa la clave en `0x2C492A0(sesión, &bytes)` [E]:
  - la decodifica (`0x4063250`) y la guarda en `sesión+0xA0`;
  - genera 32 B de clave y 12 B de IV aleatorios (`0x4059080`);
  - los cifra con la RSA del cliente (`0x4063340`);
  - manda el Ack a la sesión que preguntó (`0x2A1F680`, `ToTargetSendTargetFinder`);
  - **solo instala la clave** en la crypto de la sesión (`[sesión+0x98]->vf(0x38)`) si
    `sesión->vf(0x70)()` devuelve falso.
- En la sesión del anfitrión el cifrado sigue apagado [V]. La sesión 2 tiene la misma vtable, así
  que debería ocurrir lo mismo [S].
- Si el DER no es válido, devuelve error y no hay Ack. Por eso hay que mandar una clave válida, y la
  de 112627 sirve.
- **Ack 2840** (325 B) [V]: `u8 4` + 68 B a cero + **256 B cifrados** con la clave del cliente
  (cambian en cada arranque). **No es "todo ceros"**.

### 4.2 `LoginUserToFieldServerReq` 2490 (vf2 `0x2A21060` → `0x2C48BE0`)

| Offset | Tipo | 112627 | Significado |
|---|---|---|---|
| 0 | u64 | 0 | **cuenta**. `0x2A838E0(SAM, &cuenta)` busca un usuario existente y, si lo hay, va a la rama de relogin `0x2C17AB0` [E] |
| 8 | u32 | 0 | se copia a la estructura de login `+0x10` y `UserLogin` `0x2C17520` lo guarda en el componente de usuario `+0x84` [E] |
| 12 | u8 | 0 | estructura de login `+0x14` (no sé qué es) [E] |
| 13 | wstring | u32 len = 0 + u16 0 | nombre (≤ 1.500). `0x2C187A0` lo copia al componente de usuario `+0x88` cuando el estado de la sesión no es 2 [E] |

- El servidor monta la estructura `{sesión, cuenta, u32, u8, wchar[0x5DD]}` y se la pasa a
  `UserLogin` [E].
- **Canal 2** [E]: `flags_14 = 0xFF0002`, y `OnRecvPacket` compara `Troc+0x14` con el canal de la
  trama.

### 4.3 `LoginCharacterToFieldServerReq` 3093 (vf2 `0x298BA10` → `0x2C3E030`)

| Offset | Tipo | 112627 / 105149 | Significado |
|---|---|---|---|
| 0 | u8 | 4 / 4 | se lee y **no se usa** (no se pasa a `0x2C3E030`) [E] |
| 1 | u64 | `0x7FFFFFFFFFFFFFFF` | `characterNo`, el mismo que trae `CharacterLobbyDataAck` @5 [V] |
| 9 | u32 | **1 / 0** | **ranura de guardado** [V + E] |
| 13 | u32 | `0x4154ABD4` en las dos | se escribe en `[[personaje+0x68]+0xB8]+0x124` [E]. Significado desconocido; constante [V] |
| 17 | wstring **sin prefijo** | 1 wchar `0x4242` en las dos | el resto del payload / 2. `0x2135420` lo guarda en el mismo componente [E] |

Cómo usa el servidor esos campos [E]:
- `0x2C3E030` recorre los hijos del usuario (`[[usuario+0x68]+0x18]`) buscando uno con ese
  `characterNo` (en `[[[hijo+0x68]+0x20]+0x70]`).
- **Si no lo encuentra y `byte [exe+0x6CEEDE8] == 1`** (modo un jugador), llama a
  `0x186F570(GestorGuardado, &res, ranura = u32@9, usuario)`.
- La ranura se usa otra vez en `0x2940EE0(…, ranura)`.
- `MainPlayerDataStartAck` (25 B) trae `u32 personaje | u32 personaje | u8 0 | u64 characterNo |
  u32 1 | u32 0` [V]. Su `u32 1` **no** es la ranura: vale 1 en las dos capturas.

### 4.4 Carga: `MoveFieldCompleteReq` 2549, `LoadingCompleteReq` 2445, `LoginCharacterToGameReq` 2601, `GameLoadingStartReq` 2437

| Mensaje | Payload | vf2 | Notas |
|---|---|---|---|
| `MoveFieldCompleteReq` | `u8 0` | `0x29BF120`: **virtualizado por Denuvo** (salta a `0x150499470`) | no se puede decodificar en estático; el valor es constante [V] |
| `LoadingCompleteReq` | `u8 1` | `0x29C61A0`: **virtualizado** | constante |
| `LoginCharacterToGameReq` | vacío | `0x298D2B0`: **virtualizado** | sin Ack propio |
| `GameLoadingStartReq` | vacío | `0x29C59D0`: normal | exige `payload_len == 0`. Llama a `0x2BC9200` sobre el componente `[actor+0x68]+0x1A0`; si devuelve un id ≠ 0, puede mandar un `0x3F5` con el id y llama a `0x2068340(actor, 0xB, id)` [E]. Provoca el `MoveFieldAck` tipo 1 [V] |

**Ack de la carga** [V]:

| Ack | Contenido |
|---|---|
| `MoveFieldAck` 2174 (65 B) | `u8 4`, `u32 personaje`, **`u32 tipo`** (3 = aparcamiento, 1 = normal), `u32 usuario`, `u32 1`, `u32 1`, `u32 personaje`, `f32×3 posición`, `f32×4 cuaternión`, `f32×3 escala`. El #1 es idéntico en las dos capturas (pos (0, 1000, 0)); el #2 lleva la posición guardada: (-10329,8; 627,8; -3442,5) en 112627 y (-10285,9; 649,6; -2823,2) en 105149 |
| `MoveFieldCompleteAck` 2144 (51 B) | `u32 personaje`, `f32×3 posición`, 5 u32 (40/1/7/4/0x218 en 112627; ¿índices de sector o región? [S]), …, **`u32` clave del "Game Mechanic" cargado con el personaje** en @47 |
| `SetMoveFieldAck` 2277 | `u32 personaje`, `u8` (1 = en traslado, 0 = terminado) |
| `LoadingCompleteAck` 2762 (13 B) | `u32 personaje`, `u8 1`, `u32` creciente (≈ ms del reloj del servidor [S]), `u32 0` |

**`SceneLevelLoadCompleteSyncSceneObjectListReq` 2598** (vf2 `0x29C1900`) [E + V]:
- `u32 tipo` + `u32 hash` del nivel de escena;
- el tipo coincide con el del `MoveFieldAck` en curso (3 en F4, 1 en F7);
- los 9 niveles de F4 vuelven a aparecer, con tipo 1, entre los 65 de F7.

### 4.5 Otras Req de la carga que pide el cliente

| Mensaje | Formato | Nota |
|---|---|---|
| `RegionInOutReq` 3067 (vf2 `0x2979070`) | `u32 clave`, `u16 región`, `u8 1` = entra, `u8 0` [E + V] | 419 en F7: una por actor y región |
| `OnDemandEquipDockingGimmickSpawnReq` 2249 (vf2 `0x2970790`) | `u8 4`, `u32 clave del personaje`, resto constante `00 00 01 00 02 00 0f 00 14 00 17 00` (¿lista u16 de huecos de equipo 1, 2, 15, 20, 23? [S]) | genera los "Game Mechanic" del equipo (6 `ActorLoadDataAck` en F4) |
| `SyncStageCandidatePostionReq` 2100 (1.549 B) | `u8 4` + posiciones f32 | cada ~3 s; no es parte del apretón de manos [V] |
| `ActivateSpawningPoolReq` 2424 (55 B), `AutoSpawnRoadReq`, `TerrainRegionVoxelSpawnReq`, `SceneLevelOnDemandGimmickSpawnReq`, `MeshGroupAutoSpawnReq` | hashes y posiciones | **spawns que pide el cliente**. Con dos clientes en la misma zona podrían duplicar spawns: el invitado sintético **no** debe mandarlos |
| `EchoMoveSessionIDReq` 2624 (vf2 `0x2978900`) | `u32 clave`, `u32 id`, `u32 0` | responde a `ChangeMoveSessionIDAck` (`u32 actor`, `u32 usuario que controla`, `u32 id`, `u32 0`). Necesario más adelante para el movimiento (E4) [S] |
| `LoadMercenaryStatReq`, `RefreshUIFilterGroupDataReq` | vacíos | justo después de `LoadingCompleteAck` #2 |
| `GameOptionDataReq` | `u8 0` | → `GameOptionDataAck` |

---

## 5. Lo que comprueba el servidor al recibir (`OnRecvPacket` `0x2773C20`) [E]

1. **Canal:** `Troc+0x14 == canal` de la trama (`0x2773D49`); si no coincide, el paquete se
   descarta. Canales del guion (byte bajo de `flags_14` en el CSV):
   - **`LoginUserToFieldServerReq` = 2**;
   - todos los demás = 0.
2. **Sesión con usuario** (`sesión+0x90 != 0`):
   - si `slot >= 0`, el actor es `usuario->vf(0xF8)(slot)`, y si es nulo el paquete se descarta;
   - si `slot < 0`, el actor es el propio usuario.
   - Así, el `slot 0` de las Req de carga se resuelve **dentro del usuario de la sesión**: para la
     sesión 2 apunta solo al personaje del usuario 2, **sin tocar ningún byte**.
3. **Sesión sin usuario:** solo pasan las Req con `flags_16 == 0x100` (y `sesión->vf(0x30)`) o con
   **bit 0** en `flags_16`.
   - En la tabla: `0xFF` para `ConnectCryptogramToServerReq`, `LoginUserToFieldServerReq` y los
     temporizadores;
   - `0x04` para `LoginCharacterToFieldServerReq`, `CreateCharacterReq`, `NewGameStartReq`…;
   - `0x10` para el resto.
   - Por eso **`LoginCharacterToFieldServerReq` solo se puede mandar después de que
     `LoginUserToFieldServerReq` haya creado y enlazado el usuario**: hay que esperar a
     `MainUserDataEndAck`.
4. **Diferidas** (`Troc+0x1C == 1`, todas las del guion): se copian a un `TrBufferPoolValue` con:
   - `+0x50` = clave del usuario (`[usuario+0x60]`) o 0;
   - `+0x54` = `*(u32*)[sesión+0x88]`;
   - `+0x58` = `[usuario+0x58]` o el id de la sesión;
   - y van a la cola `ServerNet+0x38`.
   - **La sesión 2 necesita un `+0x88` propio y distinto del de la sesión 1.** Si no, las diferidas
     sin usuario (la de login) podrían mezclarse con las de la sesión 1 [S].
5. **Tamaño:** `payload_len == size-5` y `≤ 0x7FC1` (salvo el flag `TrocParams+0x38`).

---

## 6. Qué hay que reescribir para el usuario 2

| Req | ¿Se reescribe? | Campo | Origen del valor nuevo |
|---|---|---|---|
| `ConnectCryptogramToServerReq` | no | — | bytes de 112627 tal cual (DER válido) |
| `LoginUserToFieldServerReq` | **sí** | u64 @0 cuenta | **≠ 0** (p. ej. 1), por prudencia. Corrección (`data/b2-login-pasos.md`): con 0 tampoco hay relogin; el valor peligroso es **todo unos**, el único que coincide con el usuario del anfitrión (`0x2C17B88`). El mod y `cdmp_peer` lo rechazan |
| `LoginCharacterToFieldServerReq` | **sí, por plantilla** | u64 @1 `characterNo`, u32 @9 ranura | `characterNo` de `CharacterLobbyDataAck` @5 recibido en la sesión 2. Ranura: en E3 la del anfitrión (la leemos de su propio `LoginCharacterToFieldServerReq` con el sniffer o de la configuración) |
| `MoveFieldCompleteReq`, `LoadingCompleteReq`, `LoginCharacterToGameReq`, `GameLoadingStartReq`, `LoadMercenaryStatReq`, `GameOptionDataReq`, `RefreshUIFilterGroupDataReq` | no | — | constantes; el actor sale de la sesión y del slot |
| `SceneLevelLoadComplete…` (F4) | no | — | constantes |
| `SceneLevelLoadComplete…` (F7), `RegionInOutReq` | depende de la zona | hash o región, y la clave en `RegionInOutReq` | si el usuario 2 aparece donde el anfitrión, copiar en vivo las del cliente del anfitrión y cambiar la clave |
| `OnDemandEquipDockingGimmickSpawnReq` | **sí** | u32 @1 | `PLAYER_KEY` del usuario 2 (`MainPlayerDataStartAck` @0) |
| `EchoMoveSessionIDReq` | se genera | @0 actor, @4 id | del `ChangeMoveSessionIDAck` dirigido al usuario 2 |
| Gimmicks, IA, spawns, `StartActionReq`/`MoveActorReq` de **NPC** de la escena | **no se mandan** | claves `0xB01000xx` que asigna el servidor, tiempos y posiciones | simulación del cliente; un invitado sintético no la necesita para cargar |
| **Primera `StartActionReq` del personaje** (corrección D-48) | **sí, obligatoria** | u32 @0 `PLAYER_KEY` | justo tras el primer `MoveFieldCompleteAck` (en 112627, 10 ms después). Sin ella el servidor deja a 0 el byte de restauración de la acción y el cliente del anfitrión crea al personaje del usuario 2 **sin acción** y se cierra (C1–C1f, `opcion-d-accion-usuario2.md`). Antes esta tabla la daba por innecesaria |

**Claves que asigna el servidor al usuario 2** [S]:
- No se pueden predecir: `0x2A845E0` está virtualizado. Lo lógico sería `0x9010000x` y
  `0xA010000x`, pero **hay que leerlas de los Ack**:
  - `MainUserDataStartAck` @0 → `USER_KEY`;
  - `MainPlayerDataStartAck` @0 → `PLAYER_KEY`;
  - `MoveFieldAck` @1 y @0x15 → el mismo `PLAYER_KEY`.
- Los mercenarios del usuario 2 llegarán con `JoinPartyAck` y `ActorLoadDataAck` (`+0=09`,
  `+0x16` índice 1-3).

---

## 7. Cómo se comporta el cliente (para P8)

- El cliente del invitado (P8) seguirá solo este mismo guion si recibe los mismos Ack. No hay nada
  que reescribir en sus Req, salvo `LoginUserToFieldServerReq`. Tiene que ser una cuenta ≠ 0 o
  conviene que el anfitrión la reescriba al vuelo.
- Ojo: el cliente del invitado mandará además toda la simulación de §2.2 con las claves que le dé el
  servidor del anfitrión (las suyas). Eso es correcto, pero en P10 hay que decidir qué hacer con las
  Req de IA y spawns de NPC que no son suyos.

---

## 8. Guion mínimo

Formato: paso | mensaje (op en 2976) | slot | canal | payload o plantilla | esperar antes del
siguiente paso. Los mismos datos, con los hex completos, están en
`docs/research/data/login_script_112627.json`.

### E2: solo login del usuario

| # | Mensaje | slot | canal | Payload | Esperar |
|---|---|---|---|---|---|
| 1 | `ConnectCryptogramToServerReq` 2319 | -1 | 0 | 286 B de 112627 (en el JSON) | `ConnectCryptogramToServerAck` 2840 |
| 2 | `LoginUserToFieldServerReq` 2490 | -1 | **2** | `{ACCOUNT:u64=1}` + `00000000 00 00000000 0000` (19 B) | `MainUserDataEndAck` 2716. Guardar `USER_KEY` (`MainUserDataStartAck` @0) y `CHARACTER_NO` (`CharacterLobbyDataAck` @5) |

Éxito de E2: llegan a la sesión 2 `ServerTimeInfoAck`, `MainUserDataStartAck` con una clave
`0x9010xxxx` ≠ `0x90100000`, `CharacterLobbyDataAck` y `MainUserDataEndAck`. Además, el SAM pasa a
tener 2 `ServerUserActor`.

### E3: personaje y carga de zona

| # | Mensaje | slot | canal | Payload | Esperar |
|---|---|---|---|---|---|
| 3 | `LoginCharacterToFieldServerReq` 3093 | -1 | 0 | `04 {CHARACTER_NO:u64} {SAVE_SLOT:u32} d4ab5441 4242` | `MoveFieldAck` 2174 con tipo @5 = 3. Antes llegan `MainPlayerDataStartAck`, del que se guarda `PLAYER_KEY`, y `MainPlayerDataEndAck` |
| 4 | `MoveFieldCompleteReq` 2549 | 0 | 0 | `00` | `MoveFieldCompleteAck` 2144 |
| 5 | (opcional) `SceneLevelLoadComplete…` 2598 ×9 | 0 | 0 | constantes F4 | — |
| 6 | `LoadingCompleteReq` 2445 | 0 | 0 | `01` | `LoadingCompleteAck` 2762 |
| 7 | `LoginCharacterToGameReq` 2601 | 0 | 0 | vacío | 200 ms (no tiene Ack) |
| 8 | `GameLoadingStartReq` 2437 | 0 | 0 | vacío | `MoveFieldAck` 2174 con tipo @5 = 1 (posición @25) |
| 9 | `MoveFieldCompleteReq` 2549 | 0 | 0 | `00` | `MoveFieldCompleteAck`. Después llegan `ChangeGameLevelStateAck` y `MoveFieldCompletePostAck`; conviene esperar a que acabe la ráfaga (1-2 s sin S→C nuevos para la sesión 2) |
| 10 | (opcional) `RegionInOutReq` ×5 | 0 | 0 | `{PLAYER_KEY} <región u16> 01 00` | — |
| 11 | (opcional) `SceneLevelLoadComplete…` ×N (tipo 1) | 0 | 0 | los de la zona (copiar en vivo los del anfitrión) | — |
| 12 | (opcional) `OnDemandEquipDockingGimmickSpawnReq` 2249 | 0 | 0 | `04 {PLAYER_KEY} 00000100 02000f00 14001700` | — |
| 13 | `LoadingCompleteReq` 2445 | 0 | 0 | `01` | `LoadingCompleteAck` |
| 14-16 | (opcional) `LoadMercenaryStatReq` 2094, `GameOptionDataReq` 2604 (`00`), `RefreshUIFilterGroupDataReq` 2683 | 0 | 0 | — | `GameOptionDataAck` |
| reactivo | `EchoMoveSessionIDReq` 2624 | 0 | 0 | `{actor} {id} 00000000` | cuando llegue `ChangeMoveSessionIDAck` con @4 = `USER_KEY` |

Éxito de E3:
- el cliente del anfitrión recibe por `ToNear` un `ActorLoadDataAck` (`+0=09`) con
  `+0x0E = PLAYER_KEY` del usuario 2;
- el usuario 2 recibe su propia ráfaga de zona.

---

## 9. Riesgos y preguntas abiertas

### 9.1 Correcciones a la documentación anterior [V]

| Dónde | Decía | Lo que muestran las capturas |
|---|---|---|
| `segunda-sesion.md` §1.1 | "capturas idénticas" | difieren en la **ranura** (1 frente a 0), en la clave RSA y en todo lo que depende de la posición |
| `segunda-sesion.md` §1.1, `OPCION-D-EMULADOR.md` §2.3 | `ConnectCryptogramToServerAck` "todo ceros" | lleva 256 B cifrados con la RSA del cliente |
| `OPCION-D-EMULADOR.md` §2.3 | "15,0 `MoveFieldCompleteReq` → ráfaga de `ActorLoadDataAck`" | ese es el **primero** (escena de aparcamiento: 2 + 6 cargas). La ráfaga real va tras el **segundo**, a los 42,8 s |
| `OPCION-D-EMULADOR.md` §2.3 | la secuencia acababa en "33,3 `LoadingCompleteReq`, `LoginCharacterToGameReq`, `GameLoadingStartReq`" | después hay un segundo ciclo `MoveFieldAck` → `MoveFieldCompleteReq` → `LoadingCompleteReq` (54,9 s) |
| `OPCION-D-EMULADOR.md` §4.4 | no menciona el canal | `LoginUserToFieldServerReq` tiene que ir por el **canal 2** |

### 9.2 Riesgos

1. **Guardado del anfitrión:** E3 con la misma ranura hace que `0x186F570` lea la partida del
   anfitrión para el usuario 2 (clon).
   - P6 (bloqueo de escritura bajo `...\CD\save`) tiene que estar activo antes del paso 3.
   - La guarda de concurrencia `[gestor+0x60]` puede rechazar una segunda carga si la primera no ha
     terminado [E]. Aquí no aplica, porque la del anfitrión acabó hace rato.
2. **Handlers virtualizados:** `MoveFieldCompleteReq`, `LoadingCompleteReq` y
   `LoginCharacterToGameReq` no se pueden leer. No sé si el servidor exige algo entre medias (por
   ejemplo, los `SceneLevelLoadComplete…`) ni si tiene un temporizador de "rescate" si la carga no
   termina (`OPCION-D-EMULADOR.md` §3.6) [S].
   - Mitigación: empezar mandando también los pasos opcionales con los valores de la captura.
3. **Spawns duplicados:** si el guion mandase las Req de spawn del cliente (§2.2) en la zona del
   anfitrión, el servidor podría crear NPC duplicados. El guion no las incluye.
4. **Volumen:** la carga de zona manda 13.000-42.000 Ack a la sesión 2 en ~12 s (sobre todo
   `UpdateStageStateAck` y `SyncSceneObjectAck`). En E3 con un solo PC basta con contarlos y
   tirarlos. Para el túnel (P7) es el pico de tráfico.
5. **Claves `characterNo`/`itemNo` repetidas** con la misma ranura (§4.9 del documento principal):
   pueden chocar en tablas indexadas por `characterNo` (`0x7FFF…`) [S].
6. **Sin `ConnectCryptogram`:** no está comprobado que `LoginUser` funcione sin ese paso. El guion
   lo incluye siempre.
7. **`sesión+0x88`:** si no es único, el login diferido puede ir a la cola equivocada (§5.4).
8. **`flags_16 = 0x04`** en `LoginCharacterToFieldServerReq`: además de exigir usuario, el código
   tiene una rama `== 0x100` con `sesión->vf(0x30)` que no he seguido. Es posible que el estado de
   la sesión intervenga [S].

### 9.3 Preguntas para E2 y E3

- ¿Qué claves asigna el SAM al segundo usuario y a su personaje?
- ¿Va el `ActorLoadDataAck` del personaje 2 al cliente del anfitrión **sin** P5? `ToNear` en
  `MoveFieldComplete` usa otro buscador, no los vaciados.
- ¿Qué valor lleva `+0x14` en las cargas de NPC enviadas a la sesión 2 (P10)?
- ¿Hace falta responder a `ChangeMoveSessionIDAck` para que el servidor acepte después
  `MoveActorReq` del usuario 2 (E4)?

---

## Apéndice A: Req C→S de F0-F6 en 112627, en orden

Rachas consecutivas del mismo tipo agrupadas (`n`). "= 105149" cuenta cuántas de la racha coinciden
byte a byte con la Req de la misma posición en 105149. Generado por `optd/md_reqs.py`.

| Fase | t (s) | Mensaje | slot | len | dif. | n | = 105149 | payload (primer elemento) |
|---|---|---|---|---|---|---|---|---|
| F0 | 0,000 | 1001 1001 | -1 | 9006 | ? | 1 | 0/1 | `013132372e302e302e31000f00000000000000201ff7d180…` |
| F0 | 0,045 | ConnectCryptogramToServerReq 2319 | -1 | 286 | 1 | 1 | 0/1 | `0404000000540045004d0050000000003082010a02820101…` |
| F1 | 0,059 | LoginUserToFieldServerReq 2490 | -1 | 19 | 1 | 1 | 1/1 | `00000000000000000000000000000000000000` |
| F2 | 3,437 | LoginCharacterToFieldServerReq 3093 | -1 | 19 | 1 | 1 | 0/1 | `04ffffffffffffff7f01000000d4ab54414242` |
| F4 | 15,017 | MoveFieldCompleteReq 2549 | 0 | 1 | 1 | 1 | 1/1 | `00` |
| F4 | 15,033 | StartActionReq 2223 | 0 | 725 | 0 | 1 | 0/1 | `010010a0000000003c7d10260000ffffffffffff00ffffff…` |
| F4 | 15,033 | ProcessAfterBeginFrameEventReq 2189 | 0 | 4 | 0 | 1 | 1/1 | `010010a0` |
| F4 | 15,034 | SceneLevelLoadCompleteSyncSceneObjectListReq 2598 | 0 | 8 | 1 | 9 | 9/9 | `03000000ec635e2a` |
| F4 | 16,798 | StartActionReq 2223 | 0 | 725 | 0 | 1 | 0/1 | `010010a000000000000000000000ffffffffffff00ffffff…` |
| F4 | 16,798 | ProcessAfterBeginFrameEventReq 2189 | 0 | 4 | 0 | 1 | 1/1 | `010010a0` |
| F4 | 16,798 | GimmickBranchStateReq 2609 | 0 | 8 | 1 | 1 | 1/1 | `650010b089746c86` |
| F4 | 16,798 | GimmickExecuteFrameEventReq 3082 | 0 | 238 | 1 | 8 | 0/8 | `650010b0e9068b12000000000000000000000000650010b0…` |
| F4 | 16,798 | UpdateGimmickReq 2758 | 0 | 236 | 1 | 1 | 0/1 | `650010b0e9068b12000000000000000000000000650010b0…` |
| F4 | 16,800 | ChangeDrowningStateReq 2739 | 0 | 6 | 1 | 1 | 1/1 | `010010a00001` |
| F4 | 16,814 | OnDemandEquipDockingGimmickSpawnReq 2249 | 0 | 17 | 1 | 1 | 1/1 | `04010010a00000010002000f0014001700` |
| F4 | 16,890 | ChangeAIStateReq 2734 | 0 | 672 | 0 | 2 | 2/2 | `010010a00100010001000100010101000101010101000100…` |
| F4 | 16,890 | StartActionReq 2223 | 0 | 725 | 0 | 1 | 0/1 | `010010a03c7d1026d48774830000ffffffffffff00ffffff…` |
| F4 | 16,890 | ProcessAfterBeginFrameEventReq 2189 | 0 | 4 | 0 | 1 | 1/1 | `010010a0` |
| F4 | 16,919 | ChangeDrowningStateReq 2739 | 0 | 6 | 1 | 1 | 1/1 | `010010a00000` |
| F4 | 17,051-17,093 | bloques de los gimmicks `0xB0100066`-`6B` (y otra vez `69`, `6A`): `GimmickBranchStateReq` + 1-10 `GimmickExecuteFrameEventReq` + 1-3 `UpdateGimmickReq` cada uno; 6 `UpdateGimmickBodyMotionTypeReq` | 0 | 5-238 | 1 | ≈80 | solo 5 `GimmickBranchStateReq` y 1 `UpdateGimmickBodyMotionTypeReq` | `660010b0d0140b15`… |
| F4 | 17,071 | UpdateSequencerGamePlayDataEventReq 2173 | 0 | 15 | 1 | 1 | 1/1 | `01000000010010a00000006000000f` |
| F4 | 17,072 | StartActionReq + ProcessAfterBeginFrameEventReq | 0 | 725/4 | 0 | 2 | 1/2 | `010010a0d4877483d4877483…` |
| F4 | 17,091 | GamePlayTriggerInOutReq 3068 | 0 | 161 | 1 | 1 | 0/1 | `af3046386413000000000000000000000100100000000000…` |
| F4 | 17,092 | UpdateSequencerGamePlayDataEventReq 2173 | 0 | 15 | 1 | 1 | 1/1 | `01000000010010a00100006000000f` |
| F4 | 17,093 | StartActionReq + ProcessAfterBeginFrameEventReq | 0 | 725/4 | 0 | 2 | 1/2 | `010010a0d4877483d4877483…` |
| F4 | 17,112-17,113 | GamePlayTriggerInOutReq + UpdateGimmickReq (`0xB0100069`) | 0 | 161/236 | 1 | 2 | 0/2 | `af304638…` / `690010b03119395e…` |
| F4 | 18,387 | UpdateSequencerGamePlayDataEventReq 2173 + StartActionReq + ProcessAfterBeginFrameEventReq | 0 | 15/725/4 | 1/0/0 | 3 | 2/3 | `01000000010010a00200006000000f` |
| F4 | 18,645 | UpdateSequencerGamePlayDataEventReq 2173 + StartActionReq + ProcessAfterBeginFrameEventReq | 0 | 15/725/4 | 1/0/0 | 3 | 2/3 | `01000000010010a00300006000000f` |
| F4 | 18,1-32,3 | cada ~1 s: `GamePlayTriggerInOutReq` + `UpdateGimmickReq` (`0xB0100069`) | 0 | 161/236 | 1 | 2×15 | 0 | `af304638…` / `690010b0e29fb15c…` |
| F4 | 30,945 | SyncWeatherStateReq 2560 | 0 | 24 | 1 | 1 | 0/1 | `0000000000000000d9562341ffe1cc3e9b771c3d2c413840` |
| F4 | 30,945 | MoveActorReq 2556 (personaje y `0xB0100069`) | 0 | 514 | 0 | 2 | 0/2 | `010010a0000000009a097a44…` |
| F4 | 31,303 | RotateActorReq 2828 (`0xB0100069`) (no está en 105149) | 0 | 176 | 0 | 1 | 0/1 | `690010b066c5e9beeae50b3f…` |
| F5 | 33,256 | LoadingCompleteReq 2445 | 0 | 1 | 1 | 1 | 1/1 | `01` |
| F5 | 33,272 | LoginCharacterToGameReq 2601 | 0 | 0 | 1 | 1 | 1/1 | (vacío) |
| F5 | 33,287 | UpdateSequencerGamePlayDataEventReq 2173 | 0 | 15 | 1 | 1 | 1/1 | `01000000010010a00400006000000f` |
| F5 | 33,288 | StartActionReq + ProcessAfterBeginFrameEventReq + UpdateGimmickReq | 0 | 725/4/236 | 0/0/1 | 3 | 1/3 | `010010a0d4877483…` |
| F5 | 33,3-36,4 | cada ~1 s: `GamePlayTriggerInOutReq` + `UpdateGimmickReq` | 0 | 161/236 | 1 | 2×4 | 0 | `af304638…` |
| F6 | 37,318 | GameLoadingStartReq 2437 | 0 | 0 | 1 | 1 | 1/1 | (vacío) |
| F6 | 37,359 | GamePlayTriggerInOutReq 3068 | 0 | 161 | 1 | 2 | 0/2 | `af304638…` |
| F6 | 37,381 | OnDemandEquipDockingGimmickSpawnReq 2249 | 0 | 17 | 1 | 1 | 1/1 | `04010010a00000010002000f0014001700` |
| F6 | 42,811 | SyncStageCandidatePostionReq 2100 | 0 | 1549 | 1 | 1 | 0/1 | `04556821c6e8f51c44042f57c5000922c60080164400245a…` |

La tabla sin condensar (115 filas) está en `build/research/optd/md_reqs_112627.md`.

## Apéndice B: scripts (`C:\Emulador CD\build\research\optd\`)

| Script | Qué hace |
|---|---|
| `cap.py` | lector CDPK v1/v2 (solo los primeros N segundos) y tabla de nombres del CSV |
| `timeline.py`, `tl2.py`, `window.py` | líneas de tiempo C>S y S>C (completas, condensadas o por ventana) |
| `marks.py` | hitos del login y primera aparición de cada tipo C>S (`marks_*.txt`) |
| `phases.py` | fases F0-F8, recuentos por tipo, y `reqs_<cap>.tsv` con **todas** las Req del login |
| `cmp_login.py`, `cmp_acks.py` | diff byte a byte de las Req y los Ack clave entre las dos capturas |
| `cmp_phase.py`, `varoff.py` | Req iguales por fase y tipo; offsets que cambian |
| `resp.py` | Ack recibidos entre Req clave |
| `actorload.py`, `ald_detail.py`, `ald_names.py`, `ald14.py` | `ActorLoadDataAck`: claves, nombres y campos |
| `md_reqs.py` | tabla del apéndice A |
| `make_script.py` | genera `docs/research/data/login_script_112627.json` |
| `d_*.asm` | desensamblados usados: `0x298BA10`, `0x2C3E030`, `0x2A21060`, `0x2C48BE0`, `0x2C17520`, `0x2A21670`, `0x2C492A0`, `0x29C59D0`, `0x2773C20`, etc. |
