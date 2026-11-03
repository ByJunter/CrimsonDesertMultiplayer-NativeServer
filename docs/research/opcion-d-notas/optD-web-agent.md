# Option D web research: BDO server emulators, BlackSpace net, SP->MP analogues
Researched 2026-10-05. Confidence tags: [H] high, [M] medium, [L] low.
Access caveat: RaGEZONE and elitepvpers (the main BDO emulator forums) return HTTP 403 to the fetcher,
and web.archive.org is blocked. Facts from those threads come only from search-engine snippets, so they
are tagged [M]/[L]. No public, buildable BDO emulator source turned up on GitHub (searched via the API).

## A) Black Desert Online private-server emulators and protocol

### Projects found
- RaGEZONE "Black Desert online SERVER Emulator Source" (thread 1001211, around 2015-16, early C# attempts).
  Early attempts lacked working crypto and could only replay sniffed packets. [M]
  https://forum.ragezone.com/threads/black-desert-online-server-emulator-source.1001211/
- RaGEZONE "Black Desert Emulator" (thread 1085129). [M] https://forum.ragezone.com/threads/black-desert-emulator.1085129/
- RaGEZONE "Black Desert Online (Java, Emulator, MongoDB)" (thread 1142876, around 2017-18), the most complete one:
  separate **login server** (login.jar; the source was removed but the jar can be decompiled) and **game server**,
  MongoDB for dynamic data (accounts, characters), static game data in `data\sqlite3`,
  account `accessLevel` 1-4 (tester..admin). [M]
  https://forum.ragezone.com/threads/black-desert-online-java-emulator-mongodb.1142876/
  - Snippets: "server-side packet data is in packets.xml, client side all packet data hardcoded in the exe";
    handlers are classes named like `SMEnterPlayerCharacterToField.class`. [M]
- RaGEZONE "OpCode and Func dump from kr2100 (OF) and 2179 (Remastered)" (thread 1152798). [M]
  https://forum.ragezone.com/threads/opcode-and-func-dump-from-kr2100-of-and-2179-remastered.1152798/
- RaGEZONE "EU/NA Client 656 - need some help" (thread 1145296); private servers target old clients
  (versions 519, 656, 661, 819). [M] https://forum.ragezone.com/threads/eu-na-client-656-need-some-help.1145296/
- GitHub `Yurdead/819-packets-xml-`: opcode XML for client 819 (the Nova/Hashashin era), GPL-3.
  Holds `loginpackets.xml` and `packets.xml`. [H for content]
  https://github.com/Yurdead/819-packets-xml- ; also `Yurdead/BDO_661` (client 661) https://github.com/Yurdead/BDO_661
- GitHub `MaelHan/BDO519Reborn` ("Open Source Emulator", client 519): **empty repo**. [H] https://github.com/MaelHan/BDO519Reborn
- GitHub `Yurdead/DesertCore`: PHP/MongoDB web CMS for emulated servers, with no server code. [H] https://github.com/Yurdead/DesertCore
- GitHub `emudevs/SimpleBDOServerConfig`: config tool; the server root has `database.bat`, `loginserver.bat`, `gameserver.bat`. [M]
  https://github.com/emudevs/SimpleBDOServerConfig
- GitHub `iblazys/bdohook`: internal packet sniffer DLL (no source) for "GamezBD 972", with 972 opcodes.
  The author says "packet structures changed majorly in the remastered client". [H] https://github.com/iblazys/bdohook
- GitHub `ychwu/bdo-toolkit` (2026, Python): passive parser of live BDO TCP traffic. [H] https://github.com/ychwu/bdo-toolkit
- Legal context: Pearl Abyss/Kakao started acting against private servers and added a "Client Authentication System"
  on 2018-11-07. [H] https://mmos.com/news/black-desert-online-private-servers

### Architecture and naming (from opcode XMLs: client 819)
- The names show a 2-tier login: **AuthenticServer** (account login) followed by **FieldServer** (game world).
  "World" is a list of servers/channels. [H for names]
  - Login (loginpackets.xml): `CMHeartbeat 0x03E9`, `CMGetCreateUserInformationToAuthenticServer 0x14E1`,
    `CMLoginUserToAuthenticServer 0x13C7`, `CMRegisterNickNameToAuthenticServer 0x0F8C`;
    `SMSetFrameworkInformation 0x03EB`, `SMNak 0x03F3`, `SMLoginUserToAuthenticServer 0x12C6`,
    `SMGetWorldInformations 0x115C` (server/channel list), `SMGetContentServiceInfo 0x1920`.
  - Field/game (packets.xml): `CMLoginUserToFieldServer 0x14A7` -> `SMLoginUserToFieldServer 0x0FC8`;
    `CMCreateCharacterToField 0x1321`; `CMEnterPlayerCharacterToField 0x0E36` -> `SMEnterPlayerCharacterToField 0x0F69`,
    `SMLoadField 0x0ED0`, `SMLoadFieldComplete 0x130B`, `SMEnterPlayerCharacterToFieldComplete 0x1759`;
    client `CMSetReadyToPlay 0x16D1`; `CMExitFieldToCharacterSelection 0x123A`; `CMBeginDelayedLogout 0x176B`.
  - Spawn/despawn: `SMAddPlayers 0x12E1`, `SMAddMonsters 0x0F6A`, `SMAddNpcs 0x1407`, `SMAddVehicles 0x0BBE`,
    `SMAddDeadBodys 0x0E7A`, `SMRemoveActor 0x0E8E`, `SMDieNonPlayer 0x12FF`.
  - Movement/action: `CMMovePlayer 0x11FB` -> `SMMovePlayer 0x0DDD`, `CMDoAction 0x0E12`, `CMStartAction 0x0D1A`
    -> `SMStartAction 0x175F`, `SMActionRotate 0x12F1`, `SMStartCharacterNoUseNaviMoving 0x1052`,
    `SMRideOnVehicle 0x0E26`, `CMCheckActionVerifyValueT 0x131C` (server-side action verification).
  - World state: `SMSetGameTime 0x1704`, `SMInitializeWeatherData`, `SMChangeWeathers 0x1373`,
    `SMUpdateWeatherManager 0x17DB`.
  - Character state is pushed field by field after enter: `SMSetCharacterStats/Levels/Speeds/...`,
    `SMGetAllEquipSlot`, `SMAddItemToInventory`, `SMProgressingQuestList`, `SMActiveSkillList`, and others.
  - The XML has client-packet **connection states**: `CONNECTED`, `AUTHED`, `AUTHED_GG` (GameGuard), `ENTERED`.
    For example, `CMLoginUserToFieldServer` is valid only in CONNECTED and `CMCreateCharacterToField` only in AUTHED. [H]
  - Naming convention: `CM*` = client message, `SM*` = server message (not SMSG/CMSG). Opcodes are **reshuffled per client build**. [H]
  - Lesson for CD: if Crimson Desert's in-process "server" kept BDO's message model, look in the CD exe for strings or
    RTTI like `SMAddPlayers`, `CMMovePlayer`, `EnterPlayerCharacterToField`, `FieldServer`, `AuthenticServer`. [L, speculative]
- Actors: the BDO client UI Lua uses `getSelfPlayer()`, `ActorKeyRaw`, `getActorKeyRaw()`, and `openByActorKeyRaw(...)`.
  The server assigns each spawned actor a numeric **actor key** that the client uses for references.
  Decompiled Lua: https://github.com/BelovedTomboyishGirl/Black-Desert-Lua-Script-Decompiled ,
  https://github.com/gamepois0n/x64Decompiled/blob/master/panel_window_camp.lua [M]

### Packet framing and crypto
- Current live framing (2026, bdo-toolkit `_protocol.py`/`_framing.py`): `u16le length (incl. header) | u8 flag/unknown | u16le opcode | payload`,
  so the header is 5 bytes and messages are at most 0xFFFF. TCP, with several messages per segment or one message
  across segments. Default server ports are **8884, 8885, 8889**. [H for the toolkit's observation]
  https://raw.githubusercontent.com/ychwu/bdo-toolkit/main/src/bdo_toolkit/_protocol.py
- Older clients (ragezone snippets): bytes 0..1 size, 2 = "maybe encrypted flag", 5-6 = packet id. One post says
  the opcode is at byte 12 (so the header layout changed across patches). There are about 450 packets in older builds;
  972 opcodes in build 972. [M/L]
- Crypto (old clients, ragezone snippets): the **key is generated server-side** and sent on connect in opcode
  **0x03EB (`SMSetFrameworkInformation`)**, the "hello" packet. One post describes the crypto as "AES keys and Rijndael 128-bit".
  Another says "BDO uses an xor chain; you can't decrypt a single packet from within the chain" (stateful stream).
  `CMLoginUserToAuthenticServer` carries a block of `u16 len=0x100 | char[0x100] key | u16 real_len` (a 256-byte
  RSA-ish/AES key blob). The encrypted flag is in the header. [M/L]
  https://forum.ragezone.com/threads/black-desert-emulator.1085129/ ,
  https://forum.ragezone.com/threads/black-desert-online-server-emulator-source.1001211/page-10
- **ICE (Thin-ICE level 0, 8 rounds, key `51 F3 0F 11 04 24 6A 00`) is the PAZ *file-archive* cipher, not the
  network cipher**. It is combined with a custom LZ (marker 0x6E/0x6F). [H]
  https://github.com/iDevelopThings/bdo-data-extractor/blob/main/FORMATS.md
  Crimson Desert also ships PAZ/PAMT archives, so the file layer shares heritage with BDO. [H]
  https://github.com/faisalkindi/CrimsonDesert-UltimateModsManager
- No public source documents session or actor tables beyond "Mongo collections for accounts and characters". [L]

### Client/server shared code
- No public evidence of a shared client/server source tree for BDO. The ragezone snippet says the client hardcodes
  packet structs while the server uses packets.xml, so the two sides are separate implementations. [L]
- Pearl Abyss bought **Nettention** (maker of the **ProudNet** server and network engine) in July 2017 for 100% of its shares.
  The CEO at the IPO said it was bought to "strengthen the server management". Whether ProudNet or something in-house
  (for example "Troc") underlies BDO/CD is **not public**. [H for the acquisition, L for any link to Troc]
  https://www.invenglobal.com/articles/2831/kyung-in-jung-ceo-of-pearl-abyss-a-company-capable-of-both-development-and-service ,
  https://www.cbinsights.com/company/nettention , ProudNet docs: https://docs.nettention.com/proudnet.eng
- No web hits for "Troc" or "TrocNet" in relation to Pearl Abyss, BDO, or CD. The name is unknown publicly. [H for absence]

## B) BlackSpace Engine and Crimson Desert's online origins
- Development started in early 2018 (codename "Project CD"). It was conceived as a **prequel to BDO** and led by
  Jason Jung (ex-RYL Online). [H] https://en.wikipedia.org/wiki/Crimson_Desert
- G-Star (Nov 2019): announced as an **"action MMORPG"** that "features both online multiplayer and single player
  content", with a beta planned for 2020. [H]
  https://www.rpgsite.net/news/9180-pearl-abyss-reveals-new-mmorpgs-with-dokev-and-crimson-desert ,
  https://rpgamer.com/2019/11/pearl-abyss-announces-new-mmorpgs/
- The Game Awards (Dec 2020): rebranded as an "open-world action adventure that combines elements of narrative-driven
  single-player games with **online multiplayer functionality**", targeting winter 2021. [H]
  https://www.gematsu.com/2020/12/crimson-desert-launches-in-winter-2021-gameplay-reveal-trailer
- Later it became single-player only. In Feb 2026 PR lead Will Powers answered "No" to multiplayer or co-op.
  Release was 2026-03-19. [H] https://insider-gaming.com/crimson-desert-mmo-single-player-multiplayer/
- So an MMO-style client/server split (the in-process server) fits the 2018-2020 online design surviving into the
  shipped single-player build. This is an inference. [M]
- BlackSpace Engine: an in-house engine first shown to press at GDC 2025, covering path tracing, streaming, and simulation.
  Pearl Abyss says it also eases MMORPG creation. No public talk covers its networking or server layer. [H for absence]
  https://crimsondesert.pearlabyss.com/en-us/News/Notice/Detail?_boardNo=40 ,
  https://massivelyop.com/2021/11/19/pearl-abyss-discusses-the-advantages-of-creating-its-own-graphics-engine-for-dokev-and-crimson-desert/
- Existing CD co-op mod: blizz3010/CrimsonDesertCoop ("host's world shared with a second player via Steam P2P").
  It is unlicensed; see `docs/ANALISIS-CrimsonDesertCoop.md`. https://github.com/blizz3010/CrimsonDesertCoop

## C) Analogous SP->MP projects and lessons

### Skyrim Together Reborn (TiltedEvolution): dedicated server and per-actor ownership [H]
https://github.com/tiltedphoques/TiltedEvolution (Code/client, Code/server, Code/common, Code/encoding)
- The server is an ECS (entt). `AssignCharacterRequest` asks the server to own an actor. The server allocates a
  **server entity id** and stores **GameId = {BaseId, ModId}** (a load-order-independent form id).
  **Actors are never identified by address.**
- Each entity has an `OwnerComponent{owner, OwnershipEpoch}`. The server **rejects updates from non-owners**, and
  the epoch drops stale claims. `TransferToNextOwner` hands actors to another in-range player when the owner leaves;
  players who declined go on an InvalidOwners list.
- Spawn: on `OnCharacterSpawned` the server serialises a `CharacterSpawnRequest` (appearance, inventory, actor values,
  animation replay) to players in range. Cell changes drive spawn and despawn messages.
- Movement: the client's `ClientReferencesMoveRequest` goes to the server, which batches every 20 ms and sends
  `ServerReferencesMoveRequest` to non-owners. The client interpolates remote actors **300 ms in the past**.
- Client side: owned actors get `LocalComponent`, others get `RemoteComponent`. A remote player is a **real engine actor
  created as a custom NPC**: `TESNPC::Create(appearanceBuffer)` then `Actor::Create(npc)`, waiting for 3D before use.
  Animations are replayed (`AddActionsForReplay`) rather than running local AI.
  https://raw.githubusercontent.com/tiltedphoques/TiltedEvolution/master/Code/server/Services/CharacterService.cpp ,
  https://raw.githubusercontent.com/tiltedphoques/TiltedEvolution/master/Code/client/Services/Generic/CharacterService.cpp
- Quests: the **party leader's world is authoritative**. Only the leader should talk to quest NPCs, others progress
  by mirroring, and the leader should not change between sessions. The server does not own a world save; each client
  keeps its own save. [M] https://wabbanode.com/help/skyrim-together/understanding-parties-in-skyrim-together-reborn
- The same team built HogWarp (Hogwarts Legacy, Unreal), which spawns appearance-copied characters. [M]
  https://github.com/hogwarts-mp/mod , https://80.lv/articles/skyrim-together-team-presented-hogwarts-legacy-multiplayer-mod-prototype/

### Elden Ring Seamless Co-op (LukeYui) [M]
- Extends the game's **own** phantom/session netcode (guests are phantoms in the host's world) instead of a new protocol.
  It removes session-ending triggers (boss kill, death, grace) and uses Steam P2P lobbies instead of FromSoft matchmaking.
  It writes **separate save files** (`.co2` instead of `.sl2`) to protect vanilla saves and keep guests' progression synced.
  It supports 4 players and was ported to DS3 and DS2 in 2025-26.
  https://www.pcgamer.com/is-elden-ring-co-op-multiplayer , https://www.nexusmods.com/eldenring/mods/510 ,
  https://www.techspot.com/community/topics/a-modder-is-finally-bringing-seamless-co-op-to-dark-souls-ii-over-a-decade-later.297868/
- Lesson: reusing the native net layer gives replication of enemies, damage, and animation "for free". Here the native
  layer already existed for 2+ players, unlike CD (unknown).

### FiveM / OneSync (GTA V): the closest "reuse native netcode with a custom server" analogue [H]
- Keeps GTA Online's native entity sync (sync trees and nodes, per-entity owner client) but routes through a custom
  server. OneSync makes the **server hold the entity state**, creates entities server-side, culls by scope
  (424-unit focus zone), **migrates or disowns** ownership when the owner leaves scope, and adds routing buckets (instances).
  https://docs.fivem.net/docs/scripting-reference/onesync/
- Known pain point: ownership-migration desync ("invalid uniqifier"). Network IDs need a uniqueness or generation counter.
  https://forum.cfx.re/t/onesync-severe-vehicle-entity-desync-with-repeated-ownership-migration-and-invalid-uniqifier-reproducible-on-clean-server/5422910

### KenshiCoop (nhoral) [H for README/wiki claims]
- Host authoritative for the world, each client authoritative for **its own squad**. ENet/UDP plus optional Steam P2P.
  **All game mutation runs on the main thread.** Two players is the design target.
- **Saves are unified**: any save by either player becomes one shared save streamed to both. Desyncs are expected.
  https://github.com/nhoral/KenshiCoop , https://github.com/nhoral/KenshiCoop/wiki

### Outer Wilds: Quantum Space Buddies [H/M]
- Unity plus Mirror with the **host as server**. It needed a custom **authority system** (the host can be far away)
  and **identifies world objects by scene hierarchy**. The NPC AI (ghostbirds) was **rewritten for N players**.
  The loop and ship log are shared. https://github.com/misternebula/quantum-space-buddies ,
  https://outerwildsmods.com/mods/quantumspacebuddies/

### Fallout 76: an official SP engine turned online [H/M]
- BattleCry/BGS Austin retrofitted Fallout 4 with **id's Quake netcode** as a prototype, then hired MMO veterans
  (Ultima Online, SWG, SWTOR). The engine was so player-centric they nicknamed the player "Atlas" ("the world on his
  shoulders"). https://www.pcgamesn.com/fallout-76/fallout-76-making-of , https://en.wikipedia.org/wiki/Fallout_76

### BotW multiplayer (Cemu) and CyberMP [M]
- BotW reads player pose from emulator RAM, relays it via its own server, and **repurposes an NPC as the remote Link**.
  https://wccftech.com/breath-of-the-wild-multiplayer-mod-kind-of-works-and-nintendo-isnt-happy-about-it/amp/
- CyberMP (Cyberpunk 2077) uses its own dedicated server with 30-40 stable players. No public architecture docs.
  https://www.pcgamesn.com/cyberpunk-2077/multiplayer-cybermp-mod

### Cross-project lessons for "second client on CD's in-process server"
1. **Identity:** use stable content ids (Skyrim GameId, Kenshi/QSB hierarchy, BDO actor keys issued by the server)
   plus a server-issued id with a generation or epoch counter. Never use addresses. (Matches CLAUDE.md.)
2. **Authority:** per-actor owner with an epoch (Skyrim, FiveM), or host world plus client-owned avatar (Kenshi,
   Seamless, QSB). Quests and world flags have a single authority, the host or party leader.
3. **Remote players** are always real engine actors (custom NPC or spawned character) driven by replayed
   animation and interpolation (around 300 ms buffer), not by local AI.
4. **Saves:** keep the host's save authoritative. Guests use a separate save slot (Seamless `.co2`) or receive a
   shared save (Kenshi).
5. **Engines built around one player** need their AI and systems that assume a single player rewritten
   (QSB ghostbirds, FO76 "Atlas"). The same "player singleton" problem will apply even if CD's server can
   accept a second connection.
6. **Reusing native netcode** (Seamless, FiveM) is the highest-leverage path when the native layer already supports
   N peers. BDO's message model (`EnterPlayerCharacterToField`, `SMAddPlayers` with actor keys, `CMMovePlayer`,
   connection states CONNECTED/AUTHED/ENTERED) is the template to look for inside CD's in-process client and server
   boundary. [L until verified in the binary]
