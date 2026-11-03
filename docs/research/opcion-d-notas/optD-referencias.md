# Opción D ("segunda sesión" / emulador) — referencias externas

Fecha: 2026-10-05. Build objetivo: CrimsonDesert.exe 1.0.0.2976 (PE 0x6AB28F00, parche público
2.03.02, Steam build 25477059). Investigación de solo lectura: nada descargado ni ejecutado,
ningún fichero del repo modificado.

Confianza: **[A]** alta (fuente primaria / verificado en disco en nuestra build), **[M]** media
(fuente secundaria o resumen automático de una página), **[B]** baja (inferencia).
Lo que sale de páginas web resumidas por WebFetch es como mucho [M] hasta abrir la fuente.

Notas internas relacionadas (no repetidas aquí): `docs/research/segunda-sesion.md`,
`troc-bdo-y-runtime.md`, `troc-sintesis.md`, `troc-dispatch.md`, `troc-rtti.md`,
`broadcast-servidor-movimiento.md`.

---

## 0. Lo más importante (resumen)

1. **No existe ningún proyecto público que haga lo que pretende la opción D en CD** (dos
   clientes reales contra el servidor en proceso). Todo lo público en CD es "cada uno en su
   mundo + relevo" (blizz3010, el post de Reddit de u/No-Context2224 del 2026-10-05) o
   herramientas de un solo jugador. [A para el repo; M para el post]
2. **La mejor referencia técnica nueva es World Builder / cdmodkit (Moon-yungg, MIT)**: ya
   **inyecta mensajes Troc directamente en el servidor** llamando a `execute` (slot 2 del
   handler estático) **en el tick del servidor**, crea `ServerNormalInGameActor` con
   `TrocTrSpawnCharacterCheatReq`, filtra la creación de actores en `ServerField::vtable[17]`
   (por "spawn reason", 41 valores), bloquea guardados por hook de imports y probó un
   `TrocTrReloadStageStartReq` directo que **falla** porque el servidor reinicia al jugador a
   (0,1000,0) si el cliente no completa la recarga. Build 1.0.0.2976. [M, fuente primaria
   leída vía resumen; RVAs a re-verificar en disco]
3. **Para el *sender* de una Req, el servidor exige un `ServerChildOnlyInGameActor` válido**
   (World Builder lo toma del primer campo de `MoveActorReq`/`EchoMoveSessionIDReq`). Es la
   misma idea que "el usuario 2 necesita su propio ServerUserActor + personaje" de
   `segunda-sesion.md`. [M]
4. **BDO**: no hay emulador abierto/legal que pase de "entrar al mundo y andar". El valor
   está en la **secuencia de entrada al mundo** (login → server list → character → enter field
   → load field complete → AddPlayers/AddMonsters… → SetReadyToPlay) y en **altas/bajas por
   área de interés con motivo** (`ERemoveActorType` incl. `OutOfRange`, `Logout`,
   `QuitPlayer`). Los emuladores filtrados (BDOEmu/OgreFest, "server files" oficiales) están
   **descartados**. Ver `bdo-emuladores-monstruos.md`. [A]
5. **Pearl Abyss (CEO Heo Jin-young, junta de accionistas 2026)**: el multijugador "requiere
   revisar estructuras de servidor, balance, estabilidad por plataforma y sistemas de
   operación" — es decir, ni siquiera el dueño lo considera "activar un modo". CD se anunció
   en 2019 como precuela MMO de BDO y se rehízo como un jugador. [M]
6. **Referencias de patrones**: Genshin/Grasscutter (un servidor por mundo, `peer_id`,
   autoridad por entidad, el invitado *abandona* su mundo, entrada tardía tras
   `EnterSceneDone`), SPICA/NDC 2021 (servidor delega actores en un cliente "host" y los
   reasigna). Ver `genshin-coop-referencia.md`. Analogías de otros juegos: §5 (agente web).

---

## 1. Documentos del repo (resumen de lo útil para D)

### 1.1 `bdo-emuladores-monstruos.md` (2026-10-03) [A]
- Convención de nombres: BDO cliente `eTrXxxReq`/`TrXxxAck(s)`/`TrXxxNak`; emuladores `CMXxx`/`SMXxx`;
  CD `TrocTrXxxReq/Ack/Nak`. Heartbeat BDO `0x3E9`, CD `HeartbeatReq` 1008.
- Registro de mensajes del cliente BDO: entradas de 0x110 bytes `{vtable*, u16 opcode, u16 flags[6],
  wchar name[81], char name[88]}`, parser = primer método vtable, forma `process(ActorRoot*, u8*, u16)`.
  CD guarda `{u32 X, u32 opcode, prototipo*}`.
- **Opcodes barajados por versión** (661 vs 819 vs CD 2976): resolver siempre por nombre/RTTI.
- Entrada al mundo BDO: `CMEnterPlayerCharacterToField` → `SMEnterPlayerCharacterToField` →
  `SMLoadField` → `SMLoadFieldComplete` → `SMAddPlayers` → … → `CMSetReadyToPlay`.
  CD: `LoginUserToFieldServerReq`, `LoginCharacterToFieldServerReq` 3093, `MoveFieldAck` 2174,
  `MoveFieldCompleteReq` 2549, `MoveFieldCompleteAck` 2144, ráfaga `ActorLoadDataAck` 2005,
  `LoadingCompleteReq`, `LoginCharacterToGameReq`.
- `ERemoveActorType` (Illuminate): −2 None, −1 Collect, 0 Logout, 2 DespawnDeadBody, 3 DespawnMonster,
  4 Teleport, 5 DespawnEscort, 7 DespawnTent, 9 OutOfRange, 10 SealServant, 11 DespawnSummon,
  14 DespawnTamedServant, 15 QuitPlayer.
- BDO: IA de monstruos en servidor. CD: IA en el **controlador** de cada NPC (cliente) —
  `ChangeAiControlAck` 2508 y `OverriedOwnershipStateTypeyAck` 2168 son las pistas para saber a
  quién asigna el servidor cada NPC con dos usuarios (riesgo B10).
- Para D: con un 2.º `ServerUserActor` el diff de observadores (`0x2AF8AF0`, aparecer `0x2B000C0`,
  desaparecer `0x2B00380`) debería mandar altas/bajas al invitado **sin código nuestro**; pero
  `ToNearExceptOneControl` está **vaciado** → no llega el movimiento (B1).
- Fuentes BDO usadas: Illuminate (C++, sin licencia, https://github.com/CoolManBob/Illuminate),
  Yurdead/BDO_661 `packets.xml` (GPL-3.0, ~1519 nombres; procede del emulador Java filtrado → solo
  nombres), Yurdead/819-packets-xml-, ychwu/bdo-toolkit (MIT, cabecera `u16 len, u8 flags, u16 opcode`),
  hilos de RaGEZONE archivados (2014-2018), NDC 2021 SPICA.

### 1.2 `bdo-otra-ia.md` [M]
- BlackSpace se construyó sobre el Black Desert Engine (GDC 2025); IA por árboles XML (>4.400
  variantes), el servidor de BDO valida acciones con navegación por vóxeles (GDC 2018).
- Propuesta de diseño: ids de red propios, un simulador por enemigo, todos los jugadores objetivos
  válidos de la IA, AOI por jugador con instantánea al entrar, cada impacto aceptado una vez.
- `SMSetNpcActionForClientSpawn` + "acción inicial" en datos de spawn ↔ "un actor creado por red
  necesita su acción inicial" (nuestro crash del caballo).

### 1.3 `genshin-coop-referencia.md` [A para Grasscutter, inferido para el oficial]
- Un servidor por mundo (el del host); `peer_id` 1 = host, 2.. = invitados; autoridad por entidad
  (`authority_peer_id`), traspaso con estado de IA (`EntityAuthorityChangeNotify`).
- **El invitado sale de su mundo** (Grasscutter `World.addPlayer` da de baja su escena) y el host
  re-entra (`HostFromSingleToMp`); si el host se va, el mundo se disuelve. Equivalente directo de D:
  el cliente del invitado deja de usar su propio servidor.
- Visibilidad = unión de posiciones de todos los jugadores (`checkGroups`), motivos
  MEET/BORN/DIE/REMOVE; **entrada tardía solo tras `EnterSceneDoneReq`** (barrera de carga);
  el servidor ignora movimientos mientras el cliente carga.
- Daño: el servidor se fía del cliente atacante; HP/muerte/botín los emite el servidor.

### 1.4 `cheat-tables-versions.md` y `orcax-equiphide.md` (estructuras para D) [A en disco]
- WorldSystem global RVA `0x6D69190`; `+0x30` ClientActorManager, `+0x58` ClientUserActor, `+0xD8`
  CCOIA controlado. ClientActorManager global `0x6DDA800`.
- **Cadena servidor (LiveTransmog, MIT)**: PlayerStatic `0x6D69680` → `+0x18`
  `pa::NwVirtualAsyncSession` (vtable `0x057260C0`) → `+0xA0` (o `+0x90` según
  helm_audio_filter.cpp con self-heal por RTTI; ojo, discrepancia) `pa::ServerUserActor`
  (vtable `0x05B257A8`) → `+0xD0` `pa::ServerChildOnlyInGameActor` (vtable `0x05B1E2B0`).
  bbfox (1.x): `[Play_Base_Cur]+0x08 WindowsRWLock → +0x158 NwVirtualAsyncSession → +0x90
  ServerUserActor → +0xD0/+0xD8/+0xE0 ServerCCOIA` (personajes 1/2/3).
- Stats viven en el actor **Server**Child (`ServerStatusActorComponent` vtable `0x05B203F0`);
  `ServerTransformSyncActorComponent` vtable `0x05B2CF60`.
- `CCOIA+0x60` dword identidad: byte +0x60 id de sesión, byte +0x63 0xA0 Kliff / 0xB0 resto [M].
- Denuvo: código en claro en `.rsrc` (R-X); algunas funciones trasplantadas/mutadas en `.xtls`
  (no hookear). El servidor de CD tiene partes en `.xtls` (ver `segunda-sesion.md`).

### 1.5 `cd-companion-justskip.md` [A]
- Nada de red. Útil: posición estática `0x6D77858/0x6D77860`, movimiento por delta de física
  `0x4282EAF` (rbx=física, r13=pos local, xmm6=delta), cámara `0x6C8CF30`. Para D no aplica salvo
  como herramienta de diagnóstico (teletransporte).

### 1.6 `ANALISIS-CrimsonDesertCoop.md` (blizz3010) [A]
- Sin licencia; host autoritativo por **secuestro de un compañero NPC** (Oongka/Yann/Naira) y
  Steam P2P; nunca conecta (handshake antes del callback `Connected`), hooks inline a mitad de
  función, ids de enemigos = punteros. **No toca el servidor ni Troc.** Solo útil como lista de
  enlaces. No copiar código.

---

## 2. Carpeta `reference/` — qué es cada uno y qué sabe de red/servidor

| Carpeta | Qué es | Licencia / build | Red / Troc / servidor / actores |
|---|---|---|---|
| `CrimsonDesertCoop` (blizz3010, `c0a8938` 2026-05-22) | ASI co-op 0.2.3, Steam P2P, secuestro de compañero | sin licencia; 1.00.0x-1.01.03 | Solo cliente. "Spawn" del jugador 2 = secuestrar compañero (`+0xD0..+0x108` del actor, IA "desactivada" poniendo a 0 `+0x48`). Menciona `pa::ServerUserActor`, `pa::ServerChildOnlyInGameActor` y "ServerActor pointer (Tuuuup! CT)" en `game_structures.h` (líneas ~20-35, 217, 789-790: `rbx = ServerCCOIA, [rbx+0x68]+0x1B0 = controller`, v1.01.02). Nada sobre Troc. |
| `CrimsonDesertTelemetry` (ZappendusterFX, `3284601` 2026-09-29) | Telemetría jugador/cámara/luces; lector .NET externo + ASI | MIT; **exactamente nuestra build** (`definitions/build-25477059.json`) | Nada de red. **`docs/WORLD_BUILDER_RESEARCH.md`** revisa World Builder: RVAs en 2976 — setWorldTransform `0x26D4B40`, setEnable `0x26D55E0`, WorldGlobal `0x6D69190`, movement tick `0x42820E0`, ResolveActorCore/Inner `0x2827A40`/`0x2827D50`, ResolveRemovalLoop `0x2780450`, ResolveActorCtor `0x28F23F0`, ResolveUuidLookup `0x1851010`, GameHash `0x1364780`, gimmick prepare `0x278F490`; `ResolveSoServerCreate` **sin match en disco**. "ServerField tick (vtable slot 9) processes server jobs and gimmicks." "hiding an interactive spawn removes its server actor". |
| `JustSkip` (wealdly, GPL-3.0) | Acelerador por QPC | — | Nada. Su "combat flag" es un ejemplo inventado. |
| `cd-companion` (leandrodiogenes, sin licencia) | Overlay MapGenie + teletransporte | 1.09-era | Proceso externo pymem; nada de servidor. |
| `cdtools` (tkhquang CrimsonDesertTools, MIT, `a529ca1` 2.03.00) | EquipHide / LiveTransmog / CDCore | MIT | `anchors.hpp`: **`VisualEquipChange` "called from the network handler for `TrocTrAddVisualEquipItemAck`"** (rcx=`ClientFrameEventActorComponent*`, dx slot, r8w item, r9 itemData) → ejemplo de cómo un Ack Troc acaba en una función cliente. LiveTransmog `aob_resolver.hpp` ~2349 y `helm_audio_filter.cpp` ~100-130/440-540: cadena PlayerStatic→NwVirtualAsyncSession→ServerUserActor→ServerCCOIA con **self-heal por RTTI** (`.?AVServerUserActor@pa@@`), y `ServerCCOIA+0x68 → +0x40 = pa::ServerCharacterControlActorComponent → +0x40 → +0x38 CharacterAssets` (vector de std::string de 0x40). |
| `dmk` (DetourModKit, MIT) | Librería de hooks/scan | — | Nada de CD; útil por sintaxis de patrones y self-heal RTTI. |
| `orcax` (Orcax player-status-modifier, sin licencia, v1.01.00) | Mid-hooks de stats/posición | — | `researches/damage_research.md`: función de daño (`rcx` root stats objetivo, `[rsp+0x28]` ctx origen → atacante). Nada de red. |

Conclusión: **ningún proyecto de `reference/` habla con el servidor por Troc** salvo la mención de
cdtools al Ack de equipo. El que sí lo hace es World Builder (no clonado; ver §3).

---

## 3. World Builder / cdmodkit — la referencia externa más útil para D

Repo: https://github.com/Moon-yungg/crimson-desert-world-builder (MIT; commits hasta 2026-10-04;
fuente v0.93-0.96+). Nexus: https://www.nexusmods.com/crimsondesert/mods/3546. Telemetry lo revisó en
`ee1f05a3…` (ver §2). Notas: `notes/FORMATS.md` (en alemán/inglés), `notes/GIMMICK_SPAWN.md`,
`HTTP_API.md`, `asi/cdmodkit/cdmodkit.cpp`. **Leído solo por resúmenes de WebFetch → [M]**;
confirmar abriendo los ficheros antes de usar cualquier RVA.

**Verificación cruzada con nuestra tabla `docs/research/data/troc_messages_2976.csv` [A]:**
| Mensaje | id | handler slot2 (nuestra tabla) | instancia estática | World Builder dice |
|---|---|---|---|---|
| SpawnCharacterCheatReq | 2115 | `0x29BE8C0` | `0x6A71E80` | execute `0x29BE8C0` ✔ (la "instancia `0x6A7E240`" del resumen es un error) |
| GameLoadingStartReq | 2437 | `0x29C59D0` | `0x6A72FF0` | `0x29C59D0` ✔ |
| ReloadStageStartReq | 2283 | `0x2A5F580` | `0x6A7E240` | "handler `0x6A7E240`" = instancia estática ✔ |
| AiControlChangeCheatReq | 2442 | `0x29BFB80` | `0x6A72270` | — |
| EchoMoveSessionIDReq | 2624 | `0x2978900` | `0x6A6FCC0` | fuente del remitente |
| LoginUserToFieldServerReq | 2490 | `0x2A21060` | `0x6A7A460` | (no lo usan) |
| LoginCharacterToFieldServerReq | 3093 | `0x298BA10` | `0x6A70E30` | (no lo usan) |
Todos en `.rsrc` (no `.xtls`). Así que el mecanismo "llamar slot 2 con un paquete construido" de
World Builder es el mismo registro que ya tenemos mapeado.

### 3.1 Inyección de Troc en el servidor (build 1.0.0.2976) [M → A para las RVAs de la tabla]
- Los handlers de Req son **instancias estáticas** (RTTI) en `.data`. World Builder construye el
  paquete y llama `execute(handler, int* result, packet)` = **slot 2 de la vtable**, **en el hilo
  del tick del servidor** ("RunOnServerTick"), no por la red.
- Paquete: cabecera de 5 bytes, `u16` longitud del payload en +3 (= total − 5); "every payload byte
  must be read or the handler rejects it".
- `TrocTrSpawnCharacterCheatReq`: execute RVA `0x29BE8C0`; payload `{u32 characterKey, u32 unused,
  float3 pos mundo, u8 spawnType}`; characterKey = fila de `characterinfo.staticinfo`
  (p. ej. 30191 = Animal_Domestic_Bear_Domestic_30191). El actor aparece **5-7 s después**
  (asíncrono) como `pa::ServerNormalInGameActor`.
- **Remitente**: el campo +0 del paquete debe ser un `ServerChildOnlyInGameActor` válido; lo
  capturan hookeando el slot 2 de `MoveActorReq` / `EchoMoveSessionIDReq` y validan por vtable
  (una carga de partida lo reemplaza). `HeartbeatReq` no sirve (es un timer del servidor, su 3.er
  argumento no es paquete).
- `TrocTrAiControlChangeCheatReq`: id de actor de 4 bytes; `AIFunction_TerminateAi` escribe flags en
  `+0x28` del objeto IA (`actor+0x68 → +0x190`).
- Otros mensajes que usan: `TrocTrSaveAutoReq`, `TrocTrSaveToFileCurrentPlayerReq`,
  `TrocTrSaveGameDataAutoTimer`, `TrocTrGamePlaySaveDataRepeatTimer` (10 s),
  `TrocTrFlushPendingSaveDataOnceTimer`, `TrocTrGameLoadingStartReq` (execute `0x29C59D0`),
  `TrocTrReloadStageStartReq`.
- **Relevancia para D**: es exactamente nuestro mecanismo de "Req del invitado → servidor del host"
  (`segunda-sesion.md` §4.1), probado en vivo por un tercero en nuestra build. Diferencia: ellos
  usan el remitente del host; D necesita un `ServerChildOnlyInGameActor` **del invitado**.

### 3.2 Creación de actores en el servidor [M]
- `ICreateServerActorDesc` (ctor `0x278EA70`), spawn reason en desc `+0xA`; variantes
  `CreateServerActorDesc_NPCSchedule`, `CreateServerActorDesc_InstantGimmick` (0xC8 B); builder de
  descriptores `0x2C3ED50`.
- `ServerField::vtable[17]` ("field create", `0x2AF9AE0` en 2976; `0x2AF9A70` en GIMMICK_SPAWN, build
  2949) recibe `{desc** data; u32 count}`; lista vacía → hash("eErrNoActorSpawnFail").
  `ServerField` RTTI `0x5B292B8` (2949). Núcleo de creación `0x2827CE0` (wrapper `0x28279D0`);
  fábrica `0x2A82B80` (0x100 B); ctor base `ServerActor` `0x28F2380` (2949).
- **Spawn reasons (41)**: Level 0, Sequencer 1, AutoSpawn 2-11 y 38-39, NPCSchedule 12,
  SceneCollectSpawn 31, EquipDockingGimmick 46… → ya nos da el filtro de "suprimir spawns del
  invitado" (prueba T7) y es como su "play mode" aísla el mundo (Calphade: 105 Level, 105
  NPCSchedule, 26 pájaros → ~150 actores aislado).
- Gimmicks: UUID de 8 B `{id, constante por sesión}` que `ServerSyncSceneObjectManager` no conozca;
  generan `0x7E000000+n`; si falta/duplicado → `eErrNoInvalidSceneObjectUUID` (`0x282E630`). Cliente:
  `ClientGimmickActorComponent::sceneObjectCreateAndAddToLevel()` `0x8CE350` y
  `ClientSyncSceneObjectManager` localiza por UUID. **Patrón servidor-crea → cliente replica por
  UUID**, útil para entender `ActorLoadDataAck`.
- Retirada: bucle del tick del servidor `0x27803E0` (lista `{word, actor}`), escribe motivo global
  `0x6CDB94C` en `actor+0x98`, estado `+0x5C`, llama vtable[16] y [34] y luego
  `ServerSyncSceneObjectManager` slots 2/3. (Nota: `+0x98` "motivo" — comparar con nuestro
  `RemoveActorAck`/ERemoveActorType.)
- Riesgo que ellos documentan: **el juego puede autoguardar gimmicks creados → duplicados al
  recargar**. Para D: cualquier actor del invitado creado en el servidor del host puede acabar en
  el guardado del host.

### 3.3 Play mode, guardados y carga de escena [M]
- Bloqueo de guardado: hook de imports (`CreateFileW/A/2`, `DeleteFileW`, `MoveFileExW`, `CopyFileW`,
  `RemoveDirectoryW`, `CreateDirectoryW`, `SetFileAttributesW/A`) → `ERROR_ACCESS_DENIED` bajo
  `%LOCALAPPDATA%\Pearl Abyss\CD\save`; y no ejecutan los timers de autoguardado. **Patrón directo
  para proteger el guardado del host mientras hay un invitado.**
- Carga directa con `TrocTrReloadStageStartReq` (payload 53 B: `u32×2, u32, float3 pos, float4 quat,
  float3 scale, u8 flag`) inyectado dentro de `GameLoadingStartReq::execute`: destino en
  `ServerTransformSyncActorComponent+0x550`; **a los ~10 s el servidor devuelve al jugador a
  (0,1000,0) y lo mata**, "server-side reload completion that was never initiated on the client".
  Lección clave para D: **servidor y cliente tienen un apretón de manos de carga de escena**; si el
  cliente del invitado no completa `MoveField…Complete`/`LoadingComplete`, el servidor lo
  "rescata". (`0x6A7E240` = instancia estática de `ReloadStageStartReq`, comprobado en nuestra tabla.)
- Usan el viaje rápido nativo (`ClientSequencerStageManager`, `0xA9A860`) como vía fiable.
- Guardados: `%LOCALAPPDATA%\Pearl Abyss\CD\save\<steamid>\slotN\save.save`.

---

## 4. Contexto público de CD (web)

- Wikipedia: "Project CD" en informes 2019, anunciado en G-Star 2019; "originally conceived as a
  prequel to Black Desert Online … gradually reimagined as a single-player experience"; motor =
  versión mejorada del motor de BDO, **BlackSpace Engine**; lanzamiento 2026-03-19; ~7 años.
  https://en.wikipedia.org/wiki/Crimson_Desert [M]
- Mein-MMO (2026-03-12): anunciado como MMORPG, pensado "in MMO-sized thinking"; sistemas heredados de
  BDO (caballos, mascotas de botín, oficios, vivienda). Sin datos de servidor.
  https://mein-mmo.de/en/crimson-desert-wie-viel-mmo-black-desert-steckt-drin,1555904 [M]
- CEO Heo Jin-young (junta de accionistas): "Multiplayer is not just a matter of adding a mode. It is
  a major task that requires reviewing server structures…". https://mp1st.com/news/crimson-desert-multiplayer-not-ruled-out-by-pearl-abyss-still-undecided-on-mod-support
  y https://www.kitguru.net/gaming/matthew-wilson/pearl-abyss-still-exploring-mod-support-and-multiplayer-for-crimson-desert/ [M]
- **Post de Reddit 2026-10-05** de u/No-Context2224 "I'm building a co-op multiplayer mod for Crimson
  Desert" (https://www.reddit.com/r/CrimsonDesert/comments/1wx3hxy/; reddit bloqueado para WebFetch):
  Steam sin abrir puertos, se ve moverse/escalar/planear/atacar/equipo, daño compartido
  experimental, **cada uno en su mundo**, enemigos no sincronizados, montura flota, interacciones
  locales. Cubierto por Insider Gaming, Wccftech y Tech4Gamers el mismo día.
  https://insider-gaming.com/crimson-desert-multiplayer-mod-is-being-tested/ ,
  https://wccftech.com/crimson-desert-modder-pearl-abyss-co-op-multiplayer/ ,
  https://tech4gamers.com/modders-crimson-desert-co-op/ [M]. **Coincide punto por punto con nuestro
  mod (opción A/relevo): probablemente es el propio usuario; confirmar.** En cualquier caso, no es
  la opción D.
- Denuvo: añadido el 2026-03-12, bypass por hipervisor en ~24 h (irrelevante para nosotros salvo
  que el exe no cambia de RVAs en disco). https://www.gamereactor.eu/crimson-desert-confirmed-to-use-denuvo-at-launch-1689653/ [M]
- GDC 2025: BlackSpace mostrado a puerta cerrada; solo gráficos/física, nada de red.
  https://www.mynewsdesk.com/uk/swipe-right/pressreleases/pearl-abyss-showcases-blackspace-engine-at-gdc-2025-3376979 [M]
- NattKh "CRIMSON DESERT SAVE EDITOR AND GAME MODS" (MPL-2.0): guardado `.save` cifrado, varias
  ranuras; "SpawnEdit" toca contadores/cooldowns de 6+ tablas de spawn. Sin formato publicado en el
  README. https://github.com/NattKh/CRIMSON-DESERT-SAVE-EDITOR-AND-GAME-MODS [M]
- Búsquedas sin resultado: "TrocTr", "ServerUserActor", "NwVirtualAsyncSession" en la web abierta
  (fuera de GitHub de CDTools/World Builder no hay nada público). No hay ninguna charla de Pearl
  Abyss sobre el servidor de BDO/CD.

---

## 5. BDO emuladores, BlackSpace y proyectos análogos (agente web)

(se completa abajo)
