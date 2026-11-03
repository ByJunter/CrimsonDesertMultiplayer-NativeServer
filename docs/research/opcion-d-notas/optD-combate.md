# Option D ("second session" / host server serves the guest client) — combat & multi-user notes

Source: 17 docs in `docs/research/` (read in full, 2026-10-05). Build **1.0.0.2976**, PE `0x6AB28F00`,
base `0x140000000`. All addresses are exe RVAs unless written `.xtls 0x…` (Denuvo-virtualised/mutated
section; stubs in `.rsrc` jump there). Nothing here was re-verified by me; confidence is as stated by the doc.

Confidence tags: **V** = read in disassembly/static data; **C** = measured in packet captures;
**L** = confirmed live in-game; **I** = inferred / untested.

Doc abbreviations:
EA `enemigos-del-anfitrion.md` · CC `combate-coop.md` · P4D `pieza4-dano-servidor.md` ·
P4C `pieza4-capturas.md` · P4T `pieza4-tambaleo.md` · P4B `pieza4-botin-reaccion.md` ·
3G `pendiente-3g-suprimir-enemigos-invitado.md` · AG `pendiente-aggro-y-respuesta-golpes.md` ·
CO `copia-objetivo-sin-ataques.md` · RW `pendiente-recompensas-progreso.md` · BO `pendiente-jefes.md` ·
ME `companeros-mercenarios.md` · IV `ids-enemigos-verificacion.md` · IO `ids-enemigos-otra-ia.md` ·
DG `pendiente-reaccion-muerte-invitado.md` · RI `pendiente-jinetes-animales.md` · PR `pendiente-proyectiles.md`.

Docs referenced by these but NOT in my set (read them for Option D): `segunda-sesion.md` (THE design doc
for the second session: SAM::CreateUser, E0-E6 steps, B1/B3), `broadcast-servidor-movimiento.md`
(ExceptOneControl relays), `mirror-ack-formats.md` (Req→Ack recipes, control-mode gate 5),
`movimiento-remoto-cliente.md` (control mode rules), `caballo-coop.md`, `INFORME-CABALLO-CRASH.md`,
`copias-grupo-invisibles.md`, `bdo-emuladores-monstruos.md`, `genshin-coop-referencia.md`.

---

## 0. TL;DR for Option D

1. The engine **is** an MMO server+client with recipient classes `ToNear`, `ToNearExceptOneSelf`,
   `ToTarget`, `ToWorld`, `ExceptOneControl` (CC §2.1, V). With one session the writers find an empty
   recipient list and exit before serialising (e.g. `0x2969BC0` exits at `0x2969C20`) (CC §3 A1, V).
   A second real session should make these flow natively. `SendPacket 0x2773870` routes to N sessions (CC §4.3 d, V).
2. **Authority model = "controller authority"** (CC §2.1, V+C): every actor has `actor+0x90` = user key of the
   client that runs its AI; server handlers accept movement/action/frame-event Reqs only from the session
   whose user == `actor+0x90`. AI, target choice, aggro lists, hit detection, reactions all run on the
   **controlling client**; server does HP, buffs, damage formula, death, loot, XP, quests.
3. In D, H's client keeps controlling all world NPCs; G's client would see them as remote (mode 2,
   `+0xC8 = 0`). G's own attacks (DoFrameEventReq with G as attacker) would pass server checks natively
   because G's session controls G's actor. Damage would use G's stats (only path to exact damage:
   P4D §2.b, §3 M5).
4. **Known blocker that D does NOT automatically fix:** H's enemy AI does not perform normal attacks
   on a target whose `+0xC8 == 0` (remote) because the "attack rank" list `C+0x58` of the target is only
   filled for client-controlled targets; `AICondition_CheckAIBattleType` (`.xtls 0xC5DC280`) returns
   "wait" (CO §7, V + live probe). G's real player on H's client is remote ⇒ enemies will circle G unless
   the MMO had another mechanism (unknown) or the F5 hook (`0x22E0A30`) is used.
5. Things that are per-user and will need decisions: XP/KillActor credit goes to ONE player (the
   credited killer), loot recipient = "user of the field", boss stages are per-user client-driven
   sequencer stages with `Player.` conditions, quest gauges, party/ally group data (RW, P4B, BO).
6. Cheat Reqs (`KillCheatReq`, `VaryStatCheatReq`) **pass the permission check** on retail (L) — useful
   as tooling, but deaths by cheat give no XP/KillActor and log `DevCheat` in telemetry for player deaths.

---

## 1. Multi-user design evidence

### 1.1 Key layout (C, V; my derivation of nibbles is I)
| Key | What | Source |
|---|---|---|
| `0x90100000` | local **user** key (owner/controller of everything on H) | EA §1.1 (1,235/1,235 NPC records `rec+0x12`) |
| `0xA0100001` | local **player** actor (type 1) | all |
| `0xA0101740` / user `0x9010173F` | player/user after a relogin (capture 183530): counters keep rising | P4C header, IV §2.2 table, EA R8 |
| `0xB01xxxxx` | world actors (NPC/gimmick/mount), **new key on every load**, counter ~8/s, reached `0x32E2` in 27 min | EA §1.2, R8; IV §0.5 (C) |
| `0xB0100002/3/4` | user's children: mercenary (type 4) slot 1, horse (type 5) slot 2, pet (type 6) slot 3 | ME §1.2 (C) |
| Mod-only: user `0x9010F001`, puppet `0xA010F001`, `0xB010F001-F003`, enemy copies `0xB01F0000+n` | — | EA §1.2, §6 |
- Top nibble pattern: `9` user, `A` player, `B` world actor. `key & 0x30000000 == 0x10000000` = "user key"
  (client `CreateProjectileAck` skips loaded-owner check for these) (PR §1.4, V).
- **Bit 30 (`0x40000000`)** of a key: CTC emitter skips (`.xtls 0x9DAA0F0`), `IsTargetable` rejects,
  projectile Req not sent (`0x78B720`), DoFrameEventReq target keys must have bit30=0 (ME §3.2, CC §1.4, PR §1.4; V).
  Probably "client-only/local actor" (I).
- **Never identify across PCs by key or address**; stable spawn identity is in the load record T0 block (§6).

### 1.2 Actor/record fields relevant to ownership (V unless noted)
- `ActorLoadDataAck` (2005) record header (offsets in record; payload +2): `+0x00` u8 record type
  (1 player,3 person,4 named char/mercenary,5 mount/vehicle,6 animal,7 gimmick), `+0x01` u32 key,
  **`+0x05` u32 owner (user of child container)**, `+0x09` u8 slot index in child container (`0xFF` = none),
  `+0x0A` 8×FF, **`+0x12` u32 control user = `actor+0x90`** (decides control mode), `+0x16` characterinfo key,
  `+0x1A` faction key, `+0x1E` u32 n + UTF-16 name (EA §1.1; IV §2.1 uses +2 origin: `+0x14` owner, `+0x18` charinfo, `+0x1C` faction).
- Owner 0 → factory `0x4631C0` (no "logined" user required); owner = user → child container slot (EA §1.2).
- `0x4FA530` computes `byte actor+0xC8` (control state): 3 gimmick; **2** = owner `[A+0xA0]` is local
  `ClientUserActor` and `byte [A+0x94]` (local controlled character); **1** = simulated by this client;
  **0 forced if `u32 [A+0x90]` ≠ local user**. `vf188(k)` `0x4FA620` reads it: `vf188(5)` = "this client
  controls it", `vf188(2)` = "local player" (CO §7.2, V+L probe). `0x88E8E0` gives control mode 2 when
  `actor+0x90 != 0` and foreign (EA §1.2). Changing `+0xC8` on a remote actor breaks Move/Rotate Ack
  application ("gate 5", `mirror-ack-formats.md` §5) (CO §7.2).
- Other actor fields: `+0x60` key; `+0x68` component table; `[actor+0x88]+1` actor type; `+0x94`;
  `+0x96` "is local character" (370 readers); `+0x5E` u16 state (`0x10` logged in, `0x20/0x40` logging out,
  8 = waiting for group, 1 = user actor); `+0xCF` login done; `+0xC2` flag needed by IsTargetable rule 4;
  `+0xA0` owning user actor (used by cheat permission); `+0x18` lock (ME §3.3, 3G §1.2, EA §7, RI).
- `ClientUserActor` vf178 `0x8A67D0`, `ClientActor` vf178 `0x4FE260` ("local user"/`+0x94`) (AG §8).
  Server `vf178` = "controlled by user" (ME §3.3, citing segunda-sesion §2.4).
- Mercenaries/horse/pet are **server child actors of the user**: `SAM::CreateUser 0x2A83310` creates a
  `ServerChildOnlyInGameActor` (vtable `0x5B1E2B0`, 0xD8 B) per child slot in its `ChildContainer`
  (ME §1.3, V). Preamble before each load: `VaryHP`, buffs, `RefreshMercenaryDataAck`, `JoinPartyAck`… .
- Client installer of **user**-actor components (clan, `SelfFaction`, `SelfFriendly`…): `0x8AB190` (ME §9).
- `ChangeAiControlAck` client `0xB681F0` changes AI graph only if `actor+0x90` = local user (ME §9).
  `OverriedOwnershipStateTypeyAck` (2168) exists — candidate for transferring NPC ownership between users (CC §2.1, unexplored).

### 1.3 Server checks "who sent this" (all V)
- `DoFrameEventReq` 2380 server `0x296D130`: attacker exists and **controlled by the session**
  (`vf160`/`vf170` == `actor+0x90`), else error `0x6CF7A04` (CC §1.4).
- `CurrentTargetChangeReq` 2984 server `0x2980740`: session user == `actor+0x90`; target unknown → stored
  without position, no failure (ME §3.2).
- `AttackByCollisionReq` 2815 (`0x2A3D430`→`0x2C3D7D0`): session must have a character
  (`[actor+0x88]+1 != 0`); victim must exist, owner (`+0x90`) == session owner, same field; attacker optional, same field (P4D §2.c).
- `CreateProjectileFromFrameEventReq` 2387 server `0x2A3E000`: owner looked up in `[0x6D69698]`
  (`0x2A82370`); unknown → silent; `0x2BDBCE0` requires `owner+0x90 == session` + accessor valid (PR §1.4).
- `OnHitProjectileReq` 2999 `0x2A3A1C0`: projectile must exist on server and belong to session owner (P4D §2.e).
- `MoveActorReq` handler requires owner; server accepted MoveActorReq for 3,393 NPCs (CC §2.1, C).
- `VaryStatByAccInputReq` 2181 `0x2972720`: **no owner check, no stat whitelist** (CC §1.4, P4D §2.d).
- `VaryStatCheatReq`/`KillCheatReq`: no owner check on target, only cheat permission (P4B §2.1).
- `EchoActionSessionIDReq` 2350 `0x296E1A0`: decrements `CC+0xC0` only if session matches, else nothing, no Nak (P4D §4.1).
- Unknown keys are generally **silently ignored** (no Nak 1013): 74 hits on copy keys → zero server response (P4C §1.3, C).

### 1.4 Recipient classes / who receives what (CC §2.1; V via `senders.py`)
| Ack (op) | Writer | Caller | Recipients | Client handler |
|---|---|---|---|---|
| AttackTargetAck 2922 | `0x2965BF0` | `0x28B0F30` | ToNear | `0xB59280` |
| AttackerAck 2509 | `0x29657D0` | `0x28B0150` | ToNear | `0xB58F50` |
| AttackTarget_SkillAck 2846 | `0x2966010` | `0x28B21D0` | ToNear | `0xB595E0` |
| VaryHPAck 2147 | `0x2991440` | `.xtls 0x10431D40` | ToNear | `0xB75020` |
| NotifyHandleDeadAck 2530 | `0x29927E0` | `.xtls 0x10433BD0` | ToNear | `0xB764A0` |
| NotifyHandleFatalAck 2899 | `0x2992D10` | `0x29444F0` | ToNear | `0xB76900` |
| OnPostBuffDamagedAck 2681 | `0x2989490` | `.xtls 0x1043CEB0` | ToNear | `0xB70890` |
| ApplyActionRestrictionAck 2162 | `0x2967810` | `.xtls 0x10502340` | ToNear | `0xB5AEE0` |
| VarySharpnessAck 2532 | `0x2A02040` | `0x2AE03B0` | ToNear | `0xBD2D00` |
| Apply/UnapplyBuffAck 2506/3007 | `0x2A622B0`/`0x2A626D0` | various | ToNear | `0xC13D70`/`0xC14140` |
| CreateProjectileAck 2250 | `0x2A389A0` | `.xtls 0x1074D9E0` | ToNear | `0xBF3D50` |
| OnHitEventSuccessAck 2650 | `0x296C590` | `0x2A7C780` | ToNear | `0xB5F430` |
| AttackGameEventAck 2553 / **AiEventAck 2218** | `0x2A571B0`/`0x297C710` | `.xtls` | **ToTarget** (only the AI controller) | |
| DeadActorHideAck 2229 | `0x29B36A0` | `0x2C37340` | ToWorld | `0xB9E770` |
| ChangeDockingSocketByFrameEventAck 2747 | `0x2969BC0` (exits at `0x2969C20` if no recipients) | `0x27DDE40` (`ServerFrameEventChangeDockingSocket::vf1`, sole caller) | **ToNearExceptOneSelf** | `0xB5D2E0` |
| OtherEquipItemAck 2757 / OtherUnequipItemAck 3073 | `0x29ED1F0`/`0x29ED760` | `0x2AD5CC0`/`0x2ADA5A0` | ToNearExceptOneSelf | `0xBBF8D0`/`0xBBFE90` |
| AddVisualEquipItemAck 2265 | `0x2A01110` | `0x28D8EE0` | ToNear | `0xBD2050` |
| LootDropCharacterRewardListAck 2897 | `0x29F2CD0` | `0x2B84E20` | ToNear | (P4B §1.2) |
| StartActionAck 2991, ChangeAIStateAck 2404 | — | — | **ExceptOneControl** relays of the controller's Reqs; never seen in single-player captures | CC §1.2 |
- In the MMO, when A hit monster M, B (nearby) got ATA, AA, VaryHP, OPBD, AAR, buffs, Dead (ToNear),
  A's animation via StartActionAck, and M's reaction as StartAction/Move/ChangeAIState **relayed from the
  client controlling M** (CC §2.1, V).
- `ChangeBattleStateReq` 2257 is not relayed to anyone; server stores byte `comp+0x164` and runs `0x22218F0` (`0x29814A0`) (CC §1.1, §7).
- Server writer inner structure: ServerActor `+0x60` = key (writer `0x2969BC0` reads `[rsi+0x60]` at `0x2969D98`) (CC annex, V).
- Wire header of an injected S→C packet: `u16 opcode | u8 0xFF | u16 len` (CC §3 A1).

### 1.5 Transport (CC §4.2, V)
- `ClientNet::PostPacket(ClientNet* [exe+0x6D69170], int* res, u32 session=4, const u8* pkt, u16 len,
  u8 channel, u16 flags=0x10)` = `0x35C670`; copies to own 0x8030 buffer, queues at `ClientNet+0xE0`;
  NetworkThread sends via **`NwVirtualSyncSession::Send` `0x14380B0` → `ServerNet::OnRecvPacket` `0x2773C20`**.
  Slot 0 for all captured Reqs; channel must equal `Troc+0x14` of that opcode; **encryption off, no sequence number**.
- `ClientNet::OnRecvPacket` `0x35CA00` (callers `0x35BB53`, `0x35C0ED`, `0x35C951`, `0x86E55AA`; return
  value unused; early-exit `0x35CA3C→0x35CCCC`); one call = one Troc message (3G §1.3).
- `ChangePlayerbleCharacterReq` uses `PostPacket(…, 4, pkt, len, channel 0, flags 0x10)` with the emitter
  actor's slot `actor->vf(0x168)` (ME §2.1).
- For D: the in-process link is a *virtual sync session* object; a second client needs a second session
  object on `ServerNet` (see segunda-sesion.md). CC §4.3(d) rated D "low feasibility short-term (session
  creation and login in mutated code)", "the only option with faithful combat".

### 1.6 Area of interest / spawning (C)
- Server despawns by distance with reason 25, peak at **200-225 m** (966/2,000 reason-25 removals) (3G §0.4).
  H's server has everything within ~200 m of H loaded (3G §3.1).
- Live NPC counts (EA §1.3): open field 50-75; camp fight 55-59 (4-7 fighting); city 324-331.
- NPC load rate 1.4/s avg, peaks 270/s on zone load; records 6.6-11 KB (median 7.7 KB) (EA §0, §1.3).
- **Spawns are requested by the client** (`AutoSpawnRoadReq` 2550, `ActivateSpawningPoolReq` 2424,
  `SceneLevelOnDemandGimmickSpawnReq` 2928, `MeshGroupAutoSpawnReq` 2809, `TerrainRegionVoxelSpawnReq` 2327;
  deactivations 2119/2259/2690) (EA §4.1). Pools/gimmicks have saved state (`SyncFieldGimmickSavedByGimmickStateAck`) — blocking them is high risk (EA §4.2 A).
- Client never confirms a load; first thing is `RegionInOutReq` 3067 (8 B: `u32 actor | u16 region | u8 in/out | u8`)
  at 0.27 s in 2,469/2,605 loads; **server never re-sends a load of a live key**; NPC never loaded by any client
  stays alive/frozen on the server, no AI, no attacks (3G §1.1). ⇒ In D, which client "owns"/controls an NPC
  matters: an NPC not controlled by any client is inert.
- Leader loads before member 617/617; member without parent destroyed (`eErrNoParentsNotExist`); leader
  missing a child stays state 8 forever (3G §1.4, `copias-grupo-invisibles.md` §2.2).
- `RemoveActorAck` 2516 = `u8 04 | u32 X | u8 reason | n×u32 key`; client handler `0xB62C70` skips unknown
  keys (`0xB62FD4`), user actors (`+0x5E==1`) and local player (unless reason 7); also removes the linked
  mount (`vf(0x160)`) with reason 6; local logout `0x2068340 → .xtls 0xE0D9AF0` (no network) (EA §3.2, 3G §1.2).
  Reasons: 19 loading screen, 25 distance/time despawn, 21 gimmick unspawn, 57/67 pool deactivation,
  65 gimmick body motion, 28/80 guards, 42 mount after rider death, 7 special (EA §3.2, RI §2.3).

---

## 2. Server-side combat pipeline

### 2.1 Hit detection vs damage (CC §0, P4D §1; V)
- **Client detects the hit, server computes damage.** Client sends `DoFrameEventReq` (2380) with attacker,
  current action's frame event accessor and target list; server responds in ~5-9 ms.
- 17 % of real hits on the player come from a DoFrameEventReq **with empty target list** → server sweeps
  itself (context type 0 → `0x1565970`) (DG §1.3, C; P4D §1.1 I). In D, server would natively find G.
- Call chain:
```
DoFrameEventReq::vf2 0x296D130 (deserialise, owner check, ascending targets…)
 └ 0x2A896E0 (accessor type 0) validate current action; move same-owner targets (0x2BC5420)
    └ frame-event system [[realm+0x48]]->vf(0x20) (virtual; factory 0x28F0210; type0 Attack, 1 AttackThrow,
      2 AttackCatch, 7 Projectile, 26 DetachDockingGimmick, 27 ChangeDockingSocket, 50 AttachDockingGimmick)
       └ ServerFrameEventAttack::vf1 0x27D8550 → .xtls 0x101A3F00
          ├ ServerAttackActorComponent ([actor+0x68]+0x80, vt 0x5B1D0C0) vf40 0x28AF220 → .xtls 0x10321260
          │   ├ ctx type 3 (client targets) → 0x28AF2B0: AttackInfo = 0x1EA94D0([realm+0x60], key=[fe+8])
          │   │   → 0x1564440 (volume/range, per-target results)
          │   └ ctx type 0 → 0x1565970 (server sweeps) [I]
          └ 0x17F3E20 → .xtls 0xC89F080 apply per target: vf44 0x28AF5D0 → 0x1566F10 (apply AttackInfo buffs)
               → vf72 0x28B0F30 ATA (ToNear) → vf71 0x28B0150 AA (ToNear)
type 1 accessor (projectiles) → [actor+0x68]+0x88 → 0x2BE0D90
```
- **Damage is a buff:** `ServerDamageBuffProcessor` (vt `0x5B2EBA0`) vf7 `0x2ACC840` → `.xtls 0x105EC040` →
  `0x209A220` (CommonDamageBuffProcessor: dead → "OnFreezedDeadBodyBreak"; `0x209CB30` immune/invulnerable)
  → `0x209BDC0` → **`0x209A680` formula** (~1,400 instr, NOT virtualised, shared client/server) → knockback
  (TransformSync `+0x1A0`) → `0x17AE080(stats, HP, now, −dmg, ATTACKER, …, reason, type)` → `0x17AC040`.
  Then Status vf51 `0x2944120` → `.xtls 0x1043CEB0` → OPBD writer `0x2989490`; post `0x209A090` (P4D §1.1).
- Client `ClientDamageBuffProcessor` vf7 = stub `0x35EA90` (client never computes damage); its vf2 `0x931420`
  → `.xtls 0x9AA6590` → `0x209A090` → `.xtls 0xE181C10` only does `AddAggro(source)` (P4D §3, AG §1.1).
- Formula inputs (P4D §1.3): stats via `0x17AD670(stats, idx, now)`, indices from
  `[[exe+0x6D69528 client / 0x6D69530 server]+8]+0x10`; **attacker attack stat `+0xA6`** (weapon via EquipSlot
  `+0x38` `0x20C5190` if `ctx+0x30` item, else attacker stats `0x209A848`-`0x209A8CF`); target defences
  `+0xCC/+0xCE` → StatusInfo `+0x1C/+0x1A`, `+0xA8,+0xAE,+0xA4,+0xB4/+0xB6`; HP `+0xA0`, HP max `+0xA2`;
  BuffInfo at `ctx+0x10` (`+0x120` dmg category, `+0x115` fixed dmg, `+0xB8/+0xD8` per-level tables `0x2404690`,
  `+0x121` death reason); level `ctx+0x20`. Fixed point ×1000, rates ×10⁶, MulDiv `0x910E90`, min 1 HP.
  Crits ×2 ~4 % (C), helpers `0x209D0F0`, `0x17B06F0`, `0x17B46B0`, `0x17FC3D0`, `0x15A1BD0` unopened.
- Actor components `[actor+0x68]+off` (P4D §1.1): `+0x20` Status (stats `+0x18`; dead `+0x273`; death time
  `+0x268`), `+0x38` EquipSlot, `+0x40` CharacterControl (layers `+0x88+slot·8`), `+0x48` Vehicle, `+0x58`
  GameEventHandler, `+0x80` Attack, `+0x88` Projectile, `+0x90` Target, `+0xF0` Skill (vf47 `0x2BEE940`
  apply buff + ApplyBuffAck), `+0xB0` Sequencer?, `+0xC8` LootDrop, `+0x110` clan, `+0x130` cheat-permission
  component (owner user actor's), `+0x1A0` TransformSync.
- Data tables (P4D §1.5): AttackInfo (`0010`, loader `0xA4B3F0`, manager `0x1EA96E0`/`0x1EA94D0`; key = `[fe+8]`
  = ATA `+0x24`), ProjectileInfo (`0010`), BuffInfo/SkillInfo/StatusInfo/CharacterInfo/ItemInfo/TribeInfo (`0008`),
  action chart (variants `chart+0x238`).

### 2.2 DoFrameEventReq validation (CC §1.4, P4D §1.2; V)
- n ≤ 30; keys ≠ 0, bit30 = 0, **strictly ascending**; position ≠ (0,0,0); accessor type ≠ 3.
- Current server action of attacker `[[CC+0x88+slot·8]+0x18]` exists (err `0x6CF6C60`); hash == accessor
  hash (silent drop `0x2A8986C`); u16 == `[[x+0x38]+0x5E]` = action **variant** (silent `0x2A8987A`);
  index < #frame events (`0x6CF73D4`, accepted if `CC+0xC0` pending echoes); **index ≥ next allowed index
  `layer+0x44`**, then index+1 (`0x6CF6C9C`; multi-hit windows for types ≤4); attacker alive or dead < 1 s
  (`Status+0x273`, `+0x268+1000`, err `0x6CF6C68`).
- Weighted alternatives chosen by LCG `0x343FD/0x269EC3` seeded with attacker's `[[[CC+0x38]]+8]+0xE8`.
- **Targets with the same owner as the attacker get their MoveData applied to server TransformSync
  (`0x2BC5420`, no Ack)** — the controlling client is authoritative over where its NPC was when hit. In D,
  G's hits on H-owned NPCs would NOT move them (different owner) (I).
- ⇒ A Req from a client whose server-side actor isn't running the same action is silently dropped. In D,
  G must send its own StartActionReq/ProcessAfterBeginFrameEventReq (2189) first so the server state matches.
- `FrameEventAccessor` (12 B wire, type 0): `u8 type, u8 slot, u32 action hash, u16 variant id, u32 fe index`;
  in memory `+0x00 vtable 0x557C480, +0x08 type, +0x10 slot, +0x14 hash, +0x18 u16, +0x1C index` (CC annex).
  (De)serialiser `0x1F78F20`/`0x1F78DE0` **virtualised**. Type 1 (projectile) accessor is **24 B**:
  `u8 1 | u32 owner | u32 proj index | u32 pkey | u8 | u32 a | u8 b | u32 shooter | u8` (PR §0.1, C 369/369).
- DoFrameEventReq layout: `+0x000 u8 04` tag; `+0x001` attacker MoveData (222 B, ts u64 at `+0x0D6`);
  `+0x0DF` 10×28 B history; `+0x1F7` accessor; `+0x203` u16 variant tag (0-6, 7 empty) + data; targets at
  `len − n·665`, each 665 B (`0x1F78430`): `+0x000` key, `+0x004` MoveData (218 B), `+0x0DE` u32,
  `+0x0E2` vec3 impact, `+0x0EE/+0x0FA` vec3, `+0x106` bone name (CC §1.4).

### 2.3 HP, death, fatal (P4D §1.4, DG §3.1, P4B §8; V + L)
- `0x17AE080(stats, stat, now, delta, attacker, b1, b2, reason, type, b3, out)`: HP → `0x17AC040`; else
  `0x17AE4B0` (generic, no attacker).
- `0x17AC040`: applies via `0x17B3230` (→ `.xtls 0xC7E0860` → `0x17B3E10` → `.xtls 0xC7E7960`: clamp to
  stat min `[stat+0x28]` / max `[+0x18]+[+0x20]`, BO §1.4). On HP ≤ 0:
  - damage record (`vf53` `0x2953C60` → `.xtls 0x1046DA60` = `[[gs:0x58]+0x250]+0x2E0`, per-thread)
    **`+0x4F == 0` → "fatal"** (`+0x272 = 1`, `vf(0x268)(0x13)` = NotifyHandleFatalAck) (`0x17AC3AE`-`0x17AC3F2`);
  - **`+0x4F != 0` → dead** (`+0x268 = now`, `vf(0x150)`, `+0x272 = 0x100` ⇒ `+0x273 = 1`) (`0x17AC3F7`-`0x17AC47B`).
  - sends VaryHPAck with **source = `attacker+0x60`** (Status vf100 `0x29418B0` → `.xtls 0x10431D40`).
  - record: `+0x08` killer key, `+0x4D/+0x4E` reason/type, `+0x18` time, `+0x52` died, `+0x53` fatal.
- Close: Status vf43 `0x2941B60` → `.xtls 0x10433BD0` → NotifyHandleDeadAck (dead, killer, reason) →
  `0x1797850` → `.xtls 0xC76CF30` post-death → vf96 `0x2941D60` (only if killer) → `0x29431A0` events; vf97 `0x2942350` loot+telemetry.
- Pattern "open record vf52 `0x29544F0` → `0x17AE080` → close vf54 `0x2954810`" = what KillCheatReq does.
- Fatal on enemies: client sends `DieByRunOutFatalReq` (2321, 4 B key, server `0x29947E0`) 5.0 s later →
  dead, reason 15 (L, P4B §8). `HandleFatal 0x17979F0`; type-1 player → global action `[0x6A6C650]/[0x6A6C64C]`
  via `0x207AA10` (`0x1797BB0`) — never captured on a player; may stay "alive at 0" (DG §3.2, I).
- Reason codes (NotifyHandleDeadAck `+0x08`, names table `0x57C7BD0`): 0 SpawnAsDead, 6 Drowning, 7 Falling,
  **8 BuffDamage (normal kill)**, 11 Fire, 14 KillSelf, **15 Fatal**, **16 DevCheat**, 18 Pressed; `0x12` KillJammedTarget (RW §2.6).
- `NotifyHandleDeadAck` 19 B: `u32 dead | u32 killer | u8 reason | u8 | u64 action-session | u8` (DG §9).
- Player death/revive flow (DG §3.3, C): Dead → client death action 17-25 ms → `UpdateSequencerGamePlayDataEventReq(0x198,0)`
  → ~10 s `ReviveLoadingReq(2)` (2362, `0x298C490`) → `ReviveLoadingAck` (2993), `DisbandMercenaryAck`,
  `LoadingFieldDataAck` (respawn point) → `ReviveReq` (2988; `u32 key, u8 type<9, u8,u8,u8`; server `0x298C070`
  → `0x1794000`) → VaryHP full + `NotifyHandleReviveAck` (3053) + `ReviveAck` (2475) → `LoadingCompleteAck`.

### 2.4 Kill credit, XP, quests (RW §2; V + C)
- `0x29431A0` emits `PlayerDead 0x6B` (to dead player), `DeadActor 0x62` (`0x29432F2`, to group/scenario owner
  via `0x28FB8B0`→`0x2AFFF50`, independent of killer), **`KillActor 0x61`** (`0x2943B8B`, emitted `0x2943CA1`
  via `0x1804360`) only to the **credited actor, which must be a player** and ≠ dead.
- Credit resolution `0x29433EF`-`0x2943771`: (1) if dead's CharacterInfo byte `+0x157` → lookup by
  **`[killer+0x90]` (the user!)**; (2) with attack data (r8b ≠ 0, record `+0x31`): killer if player, else
  attack owner `arg6+8`; (3) without attack data: key at `[[dead+0x68]+0x20]+0x250` (last attacker?, unknown);
  (4) else only reason `0xB` (fire) retries. Enum `GameEventType` at `0x6A6A240`.
- XP = sub-level 521 `SkillPoint_Kliff` (522 Oongka, 523 Damian) on KillActor: handler `0x2C14650` →
  `0x2C12EC0` → `.xtls 0x107CCA50` (registered by `.xtls 0x107C73D0` via `0x1ABB4D0` for events 0x61, 0x3E,
  0x3D, 0x1E, 0x21, 0x07, 0x02, 0x31, 0x2D) → `0x2C0FC00` → `.xtls 0x107CBD10` → writer `0x2A66410`
  `AddSubLevelExperienceAck` 2381 (27 B `u32 player | i64 max | u8 0 | u32 sublevel | u64 amount | u16 0`).
  Amounts by tier 10/15/25/140/1000 (Tier_1/2/3/Elite/Boss). `AddExperienceAck` never used.
- Measured: normal kill reason 8 → XP 172/172 humanoids; KillCheat (16) / fatal (15) → 0/9; collision
  remate (reason 8, player owner) → XP 2/2 (C).
- **Kill by another real user's player** (`0xA0101740`, a different session/relogin) → XP 9/9 (RW §1 table, C).
- Camp liberation gauge `UpdateQuestGaugeAck` 3088 (`u32 player | u32 gauge | u16 | u32 +1 | 6×0`) +1 per member
  death **regardless of killer** (NPC allies too); completion → `CompleteQuestGaugeAck` 2738, `CompleteChallengeAck`,
  `UpdateMissionListAck`, `AddItemToInventoryAck`, contribution 601 +100. Likely hangs off DeadActor (I).
- Mastery challenges (`Challenge_SealedArtifact_Mastery_*`), some missions, Steam achievements (client-side
  `PlatformAchievementManager_Steam` vt `0x57D6B28`) hang off KillActor (I).
- Writers: UpdateQuestGauge `0x2A43C90`, CompleteQuestGauge `0x2A44190`, UpdateMissionList `0x2A41E20`,
  CompleteChallenge `0x2A476A0`, SyncChallenge `0x2A47230`, AddItemToInventory `0x29EDBE0`.
- **D implication:** a kill credits exactly one player (whoever is credited). No party sharing was observed
  (no party combat in captures). Killer = user 2 ⇒ H gets no XP/KillActor unless party sharing exists (P4D §2.b, I).

### 2.5 Loot (P4B §1; V + C)
```
vf43 → 0x1797850 → .xtls 0xC76CF30 → Status vf97 0x2942350(status, killer, now, reason, a5)
  Status+0x270 = reason, +0x271 = arg5 → 0x2942730: actor type ∈ {1,3,4,5,6,8,9} (not 7), reason != 0,
  global byte 0x6D06E98 != 0 → 0x2AE6210 ([actor+0x68]+0x38): requires [actor+0xA0] == 0; drops equipment
  → LootDrop comp [actor+0x68]+0xC8: 0x2B85E40 (mode 3 if reason==3 else 2) → 0x2B83960:
  list CharacterInfo+0x280 (count +0x288), 12-B entries {key, mask, …}; need (mask & mode)
  → roll 0x1F3E020, 0x2B85500, 0x2B84650; **recipient = user of the field** → 0x2B84E20 → writer 0x29F2CD0 (ToNear)
```
- Killer, damage contribution, damage record: **not read** by loot. Only variant (mask 2 = on death; mask 1
  probably = on corpse search) matters. 72/72 mask-2 deaths dropped, 0/142 without (C).
- Corpse looting: `InteractionDoStepReq` → `LootDropCharacterRewardListAck` + `RemoveLootableAck(dead, A0100001)` 10-40 s later.
  Pickup via DoFrameEventReq → `AddItemToInventoryAck`; item must exist on that server.
- `LootDropCharacterRewardListAck` 2897 = `u8 4 | u32 dead | n × 37 B {u8 type, u32 item key, u32, u64 qty, u32 0x19, …}`;
  loot includes quest items (`Quest_Paper_Kwe_Baris`) (RW §1.1).
- Other callers of `0x29F2CD0`: `LootingDropFromDeadBodyReq`, `ProcessLootingDeadDropOnceTimer`, `ServerFrameEventDrop`,
  StealItem, `MiniGameResultReq`, `RewardAfterRelationalDialogReq`.
- **D implication:** "user of the field" — with two users in one field, whose? Unknown (I). Killer buff 20001
  `Skill_Active_Killer` goes to the killer only if it's a player.
- Death batch order (C): ATA > [AAR] > VaryHP(0) > [OPBD] > NotifyHandleDead > ApplyBuff@P/VaryDataDefinedStaticStat@P
  > UnapplyBuff@T ×4-8 > LootDrop@P > ClearAllyGroupDataAck@T (2402) > ElementalMaterialFuelAck@T (2614) > AttackerAck@P;
  also `ClosePartyAck` (2172); `RemoveActorAck` of corpse 15-114 s later (median 30) (P4C §4.1, EA §3.1).

### 2.6 Aggro / targeting (AG, ME, CO; V + C)
- **No numeric threat table.** Aggro list per enemy in its Target component `[[E+0x68]+0x90]`, lists at
  `+0x120/+0x130` (20-B entries `{u32 key, 3×u16 relation, …}`), TopAggro = first entry of first non-empty list.
  `AddAggro 0x221D470(comp, &entries, u8, mode)` requires `actor->vf188(5)` (client-controlled), mutual unless
  mode 9; insert `0x221D750`. Callers: `0x2217A35` (SetCurrentTarget), `.xtls 0xE181C10` (dmg buff),
  `0x20A24F0` (CommonAggroBuffProcessor, provoke), `0xA26D00`, `0x221B250` (NearAggro), `.xtls 0xE6C3290` (AG §1.1).
- Client syncs list to server: `ClientTargetActorComponent` (vt `0x55C4F28`) slot 44 `0xA27C00` →
  `AggroRelationListReq` 3006 (`u8 04 | u32 actor | u32 counter | n×{u32 hash, u32 0xF4240, u32 0xF4240, u32 key}`,
  membership only) or `ChangeAggroRelationValidTickReq` (`u32 actor | u32 | u64 expiry`) when empty.
- `SetCurrentTarget 0x2217290(comp, res*, target, u64* seq, u8 a5, u8 skip_seq [rsp+0x30], u8* reason [rsp+0x38])`:
  rejects if !skip_seq and `*seq < comp+0x170`; stores `comp+0x170` seq, `comp+0x178` target, `comp+0x100` time;
  AddAggro(target); slot 51 (+0x198) & **slot 53 `0xA28270` → `.xtls 0x9DAA0F0`** → emits `CurrentTargetChangeReq`
  (op `0xBA8`, thunk `0xB657D0` → `.xtls 0xA3A4D30`) only if actor+0x90 == local user, target bit30=0,
  `byte [[exe+0x6D69190]+0xD0]+0xCAB == 0` (ME §3.2). Target looked up in realm manager `[exe+0x6D694D8]`
  client / `+0x6D694E0` server (ctx+0x10 → vf 0x28); not found → (0,0,0).
- CTC/CTCAck format 18 B: `u32 actor | u32 target | u64 seq | u8 force | u8 reason`. Client handler of
  `CurrentTargetChangedAck` 3040 `0xB68520` → SetCurrentTarget without IsTargetable; ignored if
  `[[[actor+0x68]+0x40]+0x60]+0x10 & 2` and force=0 (ME §4). Seq advances ~1.6-1.9/s; forced target lasts < 1 s
  to 24 s (AG §0.4).
- CTC reasons (AG §1.3): 0x02/0x04/0x09 forced clears (0x09 = AI reset `0x208B450`), 0x0C, 0x13 maintenance,
  **0x14 `.xtls 0x89715B0` retarget to attacker — only for player victims (vf178)**, 0x15/0x28/0x2D/0x30,
  0x17 CC vf82 `0x232B120`, **0x18 AIFunction_SetCurrentTarget** (`0x17221A0` via `.xtls 0xEC76A00`, selector
  `0x207C8F0`: TopAggro, CurrentTarget, EventFromActor, PriorityDetectTarget, SavedTarget, BattleLoopPartner…),
  0x19/0x1A, **0x1B Clear**, **0x1D NearAggro** (`0x22EDDD0`→`.xtls 0xEC7E8C0`→`0x17231B0`, nearest 3D;
  moves entry to front via `0x221B250`), 0x1E RandomAggro (`0x1725730`), 0x1F/0x21/0x22, 0x20/0x24/0x26/0x27.
- `IsTargetable 0x22141E0(comp, target, mode)` 57 callers; rejects on: Status `+0x390|+0x394` bit22; key bit30;
  Status `+0x340`; `ClientTransformSync::vf(0x160)` `0xA02B30` requires `[me+0xC2]` and `[obj+0xC2]`;
  `TS+0x3DC` ("moved by network") on self or target; gimmick flags; modes 2/4/5 `[[Status+0x3F8]+0x19]`;
  local character (`+0x96`) in a flagged action; Status `+0x276`; `+0x398`; `(+0x390|+0x394) >= 0x80`; mode 3
  → `0x1800B90` → `.xtls 0xC955C20` rejects if `obj->vf178()`; **no distance rule, no `+0x273` dead check**
  (ME §3.3, AG §1.2, DG §3.4). Relation uses **local player's** faction table (`0xA35B90`, slot 52,
  `[[local_player+0x68]+0x188]+0x18`) (ME §3.4, I).
- Detection: `CommonDetectActorComponent` (vt `0x5910000`; `0x20B3F40` gather, `0x20B00A0` descriptor);
  client vt `0x55BC3E8` (ME §9). Insertion point into aggro/detection not found (ME §3.4).
- Measured with the puppet made targetable: enemies split 67-90 % P / 10-33 % G'; first acquisitions (0x18)
  prefer P even if G' closer (11/14); P↔G' switches are 0x1D to nearest (25/27) (AG §2.1, C).
- In the **real 2-player session (183530)** H's world never registered the friend fighting: 0 DFE by/at the puppet,
  0 CTC to it (P4C §3, C).

### 2.7 Attack-coordination ("battle rank") — the remote-target blocker (CO §2.2, §7; V + L probe)
- Target comp of X `C = [[X+0x68]+0x90]`: `+0x18+16i` 4 key lists; **`+0x58+16t` attackers of type t sorted by
  distance (rank)**; `+0x98+16t` attackers by battle-order type; `+0x120/+0x130` aggro; `+0x164` "in battle"
  byte (non-controlled actor; written by server via ChangeBattleStateReq); `+0x170/+0x178` seq/target.
- Register attacker: `0x221A820` → `.xtls 0xE6881E0` → `0x221D030(compX, add, key, u16, type)` writes `+0x98`
  (type = `[[myAI]+0x50]+0x40`), callers `0x221D8A0, 0x221EF50, 0x221F360, 0x2220170, 0x221B250`.
- Rank `+0x58` writer not found (obfuscated `.xtls`); `0x221A940` only reorders (from `0x1EB2BD0` =
  `.xtls 0xDB76EC0`). Readers: `0x221D200`, `0x1EB2D10` (slots around target, false if not in list),
  `0x170D900` (IsTargetLastRank), `.xtls 0xED78A50`/`0xED7E720` (BattleRank/Count), **`.xtls 0xC5DC280`
  `AICondition_CheckAIBattleType`** (vf1 `0x22E0A30`, vtable `0x58C2BD8`, first bytes `E9 3B B4 8A 0C` →
  `.xtls 0xEB8BE70` → `0x1712510` → `.xtls 0xC5DC280`): not in target's rank list → state 2 ("wait") ⇒ no normal attacks.
- Live probe (2026-10-05): local player P (`+0xC8`=2) rank lists == attacker lists; puppet G (`+0xC8`=0)
  attacker lists full, **rank 0/0/0/0 in 87/87 reads**, aggro 0/0, `C+0x164` 0. Result: 0 normal attacks
  vs G, 10.9/min run-attacks; P 16.8-18.1/min. `IsInBattle 0x17EC130`/`0x221BB60`; battle start `0x22218F0`
  → `0x1EB29A0` (`.xtls 0xDB75040`) never fires for G (CO §7.1-7.4).
- **D relevance (I, mine):** a second real user's player on H's client is still a remote actor (`+0x90`≠local
  user ⇒ `+0xC8`=0), so unless the server/other client fills rank or the MMO path differs, H-controlled enemies
  will not melee G normally. Mitigation proposed: F5 inline hook at `0x22E0A30` forcing state 0/1 for G
  (CO §7.5). Alternative D design: let G's client control the NPCs near G (ownership transfer) — unexplored
  (`OverriedOwnershipStateTypeyAck` 2168).

### 2.8 Reaction to a hit (client of the target's controller; P4B §3, P4T, DG §2; V + C + L)
- ATA client `0xB59280` → deserialiser `0x24431B0` (→ `0xDE45F50`) → `0x451020(ClientAttack of target)`:
  needs attacker in client manager `[exe+0x6D69190]+0x30` (`0x8AE940`) else nothing; `0x45C8A0` (impulse if flags&0x10,
  physical push `0x503E80` if flags&4; returns at `0x45CCF3` if cable `+0x82` ≠ 0); if cable `+0x81` == 0 →
  `0x45B700` impact VFX/SFX ("remote attacker" path; never 0 in 2,216 real ATAs); `0x45D3E0`; AI event
  `0x2083D90` (skipped if `+0x10`==1); `0x45D800` → `.xtls 0x89715B0`.
- ATA wire (199/211 B): `+0x00` target, `+0x04` attacker, `+0x08` flags (F7 clean, E3 no-dmg frame, 84 blocked,
  87 dodge?), `+0x10` u32 (7/3/4), `+0x20` u32, `+0x24` AttackInfo hash, u64 ids `+0x2D/+0x41/+0x49/+0x51`,
  push f32 `+0x35/+0x39/+0x3D`, levels `+0x5D/+0x5E/+0x5F/+0x64`, `+0x69` impact point (= DFE entry `+0x0E2`),
  `+0x83/+0x8F` attacker pos, `+0x75/+0xAB` dirs, `+0x81/+0x82` "attacker already rendered" bytes, `+0x9B` attacker quat,
  accessor at end (P4B §3.1, AG §4.1).
- **Stagger comes from AAR (2162, 77 B) + the paired hit buff ApplyBuffAck (same instance id)**, not from ATA.
  AAR handler `0xB5AEE0` → `0x48B400` (CC comp `+0x118`) → `0x182CDA0` (`.xtls 0xCA8FD40`) writes restriction
  in `[actor+0xB0]`, launches global action `0xE69AB342` prio 6 (`0x207AA10`); buff processor
  `CommonDamagedActionRestrictionBuffProcessor` (vt `0x55B8BA8`, vf2 `.xtls 0xE1943D0`, `0x209E760`) re-applies
  with duration and sends AI events 0x63/0x64. Buffs 1104 CC_PushLight `79156D1B`, 1105 `67AE8750`, 1106 `F931B81A`,
  1107 `E55D252F`, 1109 `58B15AA0`, 1112 `5A6EBE95`, 1120 `FFCFA077` (P4T).
- Live result: injected ATA **with a real attacker** spoils the stagger (R1b/R1d/R1e fail → `A73D6EC4`);
  AAR+buff without ATA works with any source (R1f/R1c) (P4T §8-9, L). In D this is moot if hits are native.
- Damage numbers only shown if attacker == local player (OPBD → `0x632EC0` → `.xtls 0x901D160`) (AG §0.8, V).
- AA: `+0x9E/+0x9F` = 1 in real ones; `0x45C690` hitstop skipped if `+0x9F`≠0; `.xtls 0xA38F720/0xA796DC0/0xF5D7A90`;
  combo hit flags `.xtls 0x9871E20` (`0x86BC10`); attacker AI event `0x2083AB0` (AG §8).

### 2.9 Projectiles (PR; V + C)
- Client fires: `ClientFrameEventProjectile` (vt `0x5585540`) vf1 `0x7F8AB0` → `.xtls 0x970DB60` → `0x79E3E0`
  → desc vf4 `0x78B720`: creates locally `0x796B40` (→ ProjectileComp vf42 `0x793660` → `.xtls 0x9447820`),
  skip send if key bit30, else `0xBF31B0` sends 2387, then vf45 `0x7A51E0`.
- `CreateProjectileAck` client `0xBF3D50`: owner must be loaded (`+0x5E==0x10`, `+0xCF≠0`) unless user key;
  **does not check owner's control mode** → in D, G's arrows would appear on H via native ToNear.
- Impact `0x81B8F0`: sends `OnHitProjectileReq`/`OnAirFinishProjectileReq` only if `byte [proj+0xE8]==0`
  ("this client reports it", I). Server `0x2BD7710` ServerProjectileActorComponent vf42 (vt `0x5B2D468`).
- pkey (ProjectileInfo key) determines damage: `F653A168` player arrow 28, `E912512D` 17, `719C2056` bandit
  archer, `84637121` musket, `F2EE2678` collision (7 HP with player owner).
- The puppet never fired projectiles locally (0 Req with owner = puppet, 109 shot actions) — hypothesis: no
  ammo/inventory on a foreign player actor (I). In D G's real client fires with its own inventory.

### 2.10 Bosses & scripted stages (BO; V + C)
- A boss fight is a **per-user sequencer stage driven by the client**: `.pastage` + `.paseq`; conditions
  reference `Player.` (local player): `CheckEnterTrigger`, `CheckAggroToPlayer`, `set_keepAggroPlayer("Player")`
  (102/125), `Player.CheckEnterTrigger(Trigger_01)_MatchCount<1` → Fail if **H** leaves (111/125).
- Messages all carry the user/player key: `StageBranchStateReq` 2436 (server `0x2A5C4F0`) / `Ack` 2981 (`0xC073A0`),
  `UpdateStageChartEventReq` 2591 (`0x2A5CC70`) / `Ack` 2111 (`0xC08970`), `StageOnBranchEventAck` 2524 (`0xC07D00`),
  `PrepareSequencerAck` 2661, `DeleteSequencerAck` 2810, `SetAvoidByCutSceneAck` 2561 (`u8 4 | u32 user | u64 stage | u8 flag | u32 user`;
  handler `0xC09000`), `UpdateStageStateAck` 2264 (~190/s). Server and client processors:
  `ServerSequencerStage_StageChartProcessor` / `ClientSequencerStage_StageChartProcessor`; 66 `StageChart_Function_*`
  incl. `KeepAggro`, `SetAggro`, `SetBattleTarget`, `ForceLockOn`, `CompleteStage`, `Reward`, `PushReward`, `BattleEnd`.
- 125 boss-bar stages: 34 bossencounter (no noDie/barrier), 24 `cd_boss_*` (noDie 23), 59 quest (noDie 47,
  level loading 14). `set_loading_stage`/rematch/boss rush change field (`MoveFieldAck` 2174).
- Stat floor `[stat+0x28]` respected by all HP paths; `SetMinStatRateAck` 2645 (`0xB77060`) candidate for noDie.
- Cheat damage (`VaryStatCheatReq`) bypasses immune check `0x209CB30` (I); KillCheat could kill outside script.
- D implications: G would not be in H's stage; rewards/CompleteStage go to H's user; aggro fixed to "Player" (H);
  stage fails if H leaves trigger. Double boss if G's own world runs the same stage.

### 2.11 Mounts/riders (RI; V + C)
- Rider↔mount link travels in the load record (`ClientVehicleActorComponent` vt `0x55B6108`, comp `0x48`,
  vf11 `0x2227280` → `.xtls 0xE6E3810`, 167-B block: rider `L+0x10` = mount key, `L+0x14` seat; mount
  `V+0x48[seat]` = rider, `V+0xC8` count). `RideOffByServerAck` 2938 (`u32 vehicle | u32 rider | u8 seat | u8 0 | u8 flag 8`)
  on rider death (handler `0xB59C20` → RideOff `0x2228E40`). AI dismount = client-authoritative, no Ack.
- Vehicle branch crash `0x925F0E` (from `0x925CE0` in `0x85FBA0`) for type-5 copies without action.

---

## 3. Cheat / dev Requests (server) and effects

Permission gate (all cheat handlers, 14 uses): `[[actor+0xA0]+0x68]+0x130 → vf(0x140)(type)`; failure →
Nak `0x3F5` (1013). `ServerContentsMiscActorComponent::vf40` = `mov al,1; ret` at `0x3600C0` (I that it's the
+0x130 one). **Passed live** for KillCheatReq (type 2) and VaryStatCheatReq (type 3), on enemies and on own
player (P4B §2.1, §8; DG §0.3; L). Injected via `sniffer_inject_c2s` (PostPacket hook).

| Req (op) | Server | Format | Effect | Status |
|---|---|---|---|---|
| **KillCheatReq** 2279 | `0x29BF730` → `0x2C40C00` | 8 B `u32 victim, u32 option` | open record vf52, `0x17AE080(HP, −HP, attacker = session actor, reason 0x10, type 0xA)`, close vf54; `+0x4F = (option==0)` (`0x2C40EBA`-`0x2C40ECB`) → immediate death | **L**: reason 16, loot per variant, buff 20001; **no XP, no KillActor**; no-op on dead |
| **VaryStatCheatReq** 2703 | `0x29BF3C0` → `0x2C3FBF0` | 15 B `u32 target | u32 stat (HP 0x000F4240) | u8 mode | i32 value | u8 percent | u8 f5` | mode 0 add (×1000, `0x2C3FF32`-`0x2C40819`, attacker = session actor, reason 0x10, type 0xA), 1 set (`0x17AED80`, +`0x17AE080` with f5), 2 `0x17AFB10`; `+0x4F=0` (`0x2C3FF67`) → at 0 HP "fatal" | **L**: VaryHPAck source = H's player; at 0 → NotifyHandleFatalAck → client DieByRunOutFatal 5 s → reason 15; no XP |
| VaryStatByAccInputReq 2181 | `0x2972720` → `0x17AE4B0` | 16 B `u32 key, u32 stat, i64 delta` | generic stat path, source 0, no owner check; HP 0 → `OnBattle_HandleKnockOut` (`0x1798BF0` vf103, event id `0x6A6C658`) only | **C**: 53 enemies at 0 HP, 0 died, 3 "zombies" |
| AttackByCollisionReq 2815 | `0x2A3D430` → `0x2C3D7D0` → `0x2BDA2F0` → `0x2BE11A0` | 44 B `u32 victim | u32 attacker | u32 0 | vec3 point | vec3 dir | u32 pkey | u32 0` | creates collision projectile of attacker (`0x21E3EF0`/`.xtls 0x23161C0` def lookup; unknown → `0x6CF744C`, no Nak) → client simulates → DFE type 1 → full native result | **L/C**: `0xF2EE2678` = 7 HP (player owner) / 1 HP (gimmick); full credit (reason 8, XP, 20001, loot). AttackInfo key → silent |
| ApplySkillFromCollideObjectReq 2806 | `0x296E6A0` → `0x2BF59C0` | `u32×5 + u8` | applies SkillInfo level buffs, source = the actor itself, reason 0x13 | V |
| Change/UnapplySkillFromCollideObjectReq 2330/2260 | `0x296F230` → `0x2BF5D80` | — | variants | V |
| ForceApplySkillForDebugReq 2531 | `0x2A647B0` | — | **empty** | V |
| CharacterPresetSpawnNPCReq 2440 | `0x298F490` | — | empty | V |
| SpawnCharacterCheatReq 2115 | `0x2C3ED50` (perm `0x2C3EF46`) | — | spawns a character | V, untested |
| SpawnCharacterForActionTestReq 3058 | `0x2C3F4D0` (perm `0x2C3F65C`) | — | spawns a character | V, untested |
| ClearKillerActorDataReq 2448 | `0x298CEC0` → `0x17933A0` | — | clears Status `+0x254/+0x258/+0x260/+0x275` | V |
| KillJammedTargetReq 2392 | `0x29D0380` → `0x28E5250` | — | kills player's "jammed target" (`[[comp+0x168]…+0xA0]+0xD8`), attacker = target, reason 0x12 | V |
| DieByRunOutFatalReq 2321 | `0x29947E0` | 4 B key | fatal → dead, reason 15 | L |
| SetIgnoreDamageByObserverReq 2798 / AttackResultBackgroundOnlyReq 2577 | `0x29D1E70` / `0x2973030` | — | not opened | — |
| SpawnItemToGroundByCheatReq 2433 | `0x2A03090` → `0x2C445D0` | u32,u64,u16,vec3,2 structs (`0x141FA90`/`0x141FB70`),u32,struct `0x12AD8D0`,u8,u64,u8 | item spawn; perm type 0 | V, format undecoded, risky |
| CreateItemFromTrItemValueCheatReq 2583 | `0x2A02EB0` → `0x2C443F0` | full TrItemValue | item creation | V, risky |
| AddExperienceByGmReq 2853 | `0x2994CB0` → `0x2C3EBC0` | — | character XP (unused system) | V |
| VarySubLevelExpByResourceReq 2441 | `0x2A68D60` → `.xtls 0x104E1600` → `0x2C111C0` | 4 B | spends a resource | V |
| CompleteQuestByCheatReq 2627 / StageChartForceBranchDevReq 2476 | `0x2A48930` / `0x2A5D330` | — | skip quest/stage — **do not use** | V |
| SetBossRematchLevelReq 2760 | `0x2A608D0` | — | — | V |
- Telemetry: vf97 `0x2942350` (`0x29425E6`-`0x29426E1`) builds `CharacterEventFunnelFormat` with `dead_reason`
  **for player deaths only**; reason 16 = "DevCheat". No "cheat used" flag seen in save/profile (RW §2.6, not exhaustive).

---

## 4. Packet layouts quick reference (offsets in payload)
- VaryHPAck 2147 (92 B): `+0x00` key, `+0x04` stat (`0x000F4240`=HP), `+0x08` i64 HP×1000, `+0x10` i64, `+0x18` i64 HPmax×1000,
  `+0x30` u64 event/attack id (shared with ApplyBuff/OPBD of the same hit), **`+0x40` u32 per-actor sequence**,
  `+0x4C/+0x50` FFFFFFFF on lethal, `+0x54` u32 source, `+0x58` u32 0x100. Client apply `0xB75020` → `0x62FA00` → `.xtls 0x17AD2D0` (virtualised) (EA §3.1).
- OnPostBuffDamagedAck 2681 (53 B): `+0x00` target, `+0x04` attacker, `+0x08` source, `+0x0C` u32 dmg×1000 (overkill not clipped), `+0x2C` u64 attack id, `+0x34` (CC §1.2, AG §4.2).
- AAR 2162 (77 B): `+0x00` target, `+0x04` quat, `+0x14` u64 relative delay (0; -1 none), `+0x1C` u64 1 (→ `0x8B4790`
  local action session → EchoActionSessionIDReq), `+0x24` u32 instance id `0x800000NN` (per-actor counter shared with buffs),
  `+0x28` f32 = ATA `+0x60`, `+0x34` restriction key, `+0x38` attack hash, `+0x44` slot type, `+0x45/+0x46` levels,
  `+0x47`, `+0x48` 1 (launch action), `+0x49` u32 source (optional) (P4T §1.1, P4B §3.3).
- ApplyBuffAck 2506 (77 B): `+0x00` holder, `+0x04` buff, `+0x0C` level, `+0x14` instance id, `+0x18` source, `+0x1C` holder,
  `+0x20` 0x27, `+0x29` vec3 attacker pos, `+0x35..+0x38` AAR levels/1, `+0x39`/`+0x41` u64 server game time ms, `+0x49` buff (P4T §2.1).
- NotifyHandleDeadAck 2530 (19 B) e.g. `D80610B0 010010A0 08 00 01 00000000 000000 0A`.
- CurrentTargetChange(d) 2984/3040 (18 B): see §2.6.
- StartActionReq 2223 (725 B): `+0x00` actor, `+0x04` action hash, `+0x8C/+0x116` parent, `+0x16C` current target, `+0xAA` u32 (sometimes key).
  StartActionAck 2991 = `K' + req[0x04:0x8E] + req[0x2D4] + req[0x8E:0x2D4]` (ts Ack `+0x160`) (EA §3.3).
- ChangeAIStateReq 2734 (672 B) → Ack 2404 = `K' + req[4:]` (requires u32 `+0x26C` != 0).
- MoveActorReq 2556 (514 B) / MoveActorAck 2697 (235 B synthesised layout in EA §3.4). RotateActorReq 2828 / Ack 2687.
- ChangeBattleStateReq 2257: `u32 actor | u8 state` (5 B); state ≠0 is the reliable "engaged" signal (133/135 hostile humans vs 1/26,654 animals) (RI §1.3).
- StartAIFunctionReq: `+0 actor | +4 graph (0xA17A8F3E) | +8 package | +0xC function | +0x10 event | +0x14 index`, names = `hashlittle(lowercase, 0xC5EDE)` (CO §1.4).
- RideOffByServerAck 2938; RegionInOutReq 3067; OnDemandEquipDockingGimmickSpawnReq 2249 (`04 | u32 NPC | u16 0`).
- AddSubLevelExperienceAck 2381; UpdateQuestGaugeAck 3088; CompleteQuestGaugeAck 2738; LootDrop 2897.
- Other opcodes: ReviveReq 2988, ReviveAck 2475, ReviveLoadingReq 2362, ReviveLoadingAck 2993, NotifyHandleReviveAck 3053,
  EchoActionSessionIDReq 2350, ProcessAfterBeginFrameEventReq 2189, AggroRelationListReq 3006, UpdateAIDestinationReq 2405,
  StartAIFunctionReq 2291, CreateProjectileFromFrameEventReq 2387, OnHitProjectileReq 2999, RemoveProjectileReq 2399,
  RemoveProjectileAck 2751, AddAttackImpulseCountReq 2705, CatchActionResultAck 2156, ReleaseCatchResultAck 2512,
  UpdateElementalAck 2642, OnDemandEquipDockingGimmickUnspawn 2451, UpdateFactionRelationDataAck 2597,
  CreateFactionSpawnStageAck 2195, FrameEventCallMercenaryReq 2166 (server `0x2A32440` → `0x2BA9950`),
  CallVehicleMercenaryAck 2955, ChangePlayerbleCharacterReq 2369 (server `0x2A4A180` → `0x29181B0`) / Ack 2473
  (client `0xBFDFB0`), ChangeMainMercenaryAck 3062 (`0xBE8060` → `0x9E38B0`), SelectMercenarySpawnReq 3069 (`0xBE21B0`),
  CallHyosiByQuickSlotReq 2919 (`0xBE2820`), RequestQuickTimeEventResultReq 2236 (`0x298C790`),
  NotifySummonCharacterAck 2965 (`0xC10780`), MoveFieldAck 2174 (`0xB87360`), SetMinStatRateAck 2645.

---

## 5. What breaks/changes with a second real user (synthesis)
| Area | Today's evidence | With D | Conf |
|---|---|---|---|
| G's damage to H's enemies | Only estimable (tables ~83 % ±1 HP) | Native with G's stats/weapon/crits (only exact path) | P4D §2.b, §3 (I) |
| Kill credit/XP/KillActor | One credited player; another user's player kill got XP 9/9 | Killer = G ⇒ G's credit **in H's server** (G's progression must live there, i.e. G's save loaded into H's server = "B3 hook" in segunda-sesion) | RW §1, P4D §2.b |
| Loot | recipient = field user; looting needs item on that server | G's loot/inventory live on H's server; H's world | P4B §1.2/1.4, P4D §2.b |
| G's save | — | Without a save-loading hook the 2nd character is a clone of H's save/stats | P4D §2.b (from segunda-sesion) |
| Stamina/buffs/sharpness | per server | Duplicated between G's own server and H's if G keeps both | P4D §2.b |
| Enemy AI vs G | AI on H's client; remote targets get no rank ⇒ circling; first acquisition prefers local P | Same (G remote on H's client) unless F5 hook or ownership transfer | CO §7 (V+L probe), AG §2 |
| Aggro on hit | Retarget-to-attacker 0x14 only for player victims; enemies acquire via AddAggro from damage buff (client) | Native ATA/buffs reach H's client via ToNear ⇒ damage buff AddAggro(G) on H's client should work | AG §1 (I) |
| Hit position | Same-owner targets' MoveData applied | G's hits on H-owned NPCs don't move them | P4D §1.2 (I) |
| Bosses/stages | per-user, client-driven, `Player.` | G not in H's stage; rewards to H only; stage fails if H leaves | BO §1.3, §2 |
| Camp gauge | per killer-independent | likely per user who owns the quest gauge (who receives `UpdateQuestGaugeAck`? it carries the player key) | RW §2.4 (I) |
| Party | Join/Open/ClosePartyAck, ClearAllyGroupData exist; no multi-player party data | Party/ally group semantics unknown | ME §1.1, EA §3.1 |
| Spawns | client requests spawns; server keeps NPCs no client controls frozen | G's client would also request spawns in H's world (AOI around G) | EA §4.1, 3G §1.1 (I) |
| Projectiles | puppet can't fire; client creates locally; Ack ToNear | native | PR §4.2 |
| Mercenaries | server children of user (`ServerChildOnlyInGameActor`) | G's user would need its own child container | ME §1.3 |

## 6. Stable cross-PC identity (if D keeps any mirroring)
- `+0x18` (record-relative +2 origin) = characterinfo key (5,332/5,332), `+0x1C` = faction key (IV §0, V).
- T0 = first Transform; located by unique signature `0C 00 | 8×00 | FF FF FF FF` at T0−18 (5,332/5,332).
  `T0+42` schedule hash `hashlittle(name,0xC5EDE)` + `T0+46` element id (stable for place schedules);
  `T0+50` u16 per-session serial; `T0+390` factionnodespawninfo key + `T0+394` u16 slot (variant re-rolled
  every session!); `T0+1043` `.paseq` hash + `T0+1047` `0x20000000|id`; 17 % no identity (IV §2.1, §4).
- Hostility: faction table `+0x22` relation group; `factionrelationgroup` 0x4243 Graymane … 0x4247 Monster_Common;
  55/135 factions hostile (3G §2.1).

## 7. Open questions, dead ends, risks
Dead ends / disproved:
- Substitute attacker (H's player DoFrameEventReq with G's accessor) — rejected: silent drops, H loses hits,
  uses H's stats (P4D §2.a).
- Calling `0x209A680` from the mod (M4) — rejected: crash risk, crash reports go to Pearl Abyss (P4D §3).
- VaryStatByAcc to 0 HP → zombies (P4D §2.d). VaryStatCheat to 0 → fatal + 5 s delay + reason 15 (P4B §8).
- AttackByCollision with AttackInfo key `0x81048142` → silent (needs ProjectileInfo key) (P4D §2.c).
- Injected ATA+AAR(+buff) with valid attacker → no stagger (`A73D6EC4`) (P4T §7-9, L).
- F1/F2 for the "copy not attacked" problem disproved by probe (CO §7.5).
- `factionnodespawninfo` "guid" is a quaternion; NattKh parser misaligned (65 vs 66 B) (IV §0.3, §3.1).
- Blocking spawn Reqs (pools have saved state, Nak 1013 risk) (EA §4.2).
- Placing enemies in the guest with "same twin" matching: only 3/94 spawn at exact same point between sessions for non-schedule (EA §4.2 D).
Open:
- Who fills target rank list `C+0x58` (obfuscated `.xtls`); does the MMO path handle remote players? (CO §7.3)
- `[[dead+0x68]+0x20]+0x250` (credit without attack data) (RW §7.1).
- Camp gauge via DeadActor? (RW T2). Quest credit with reason 0x10 (X5).
- `OverriedOwnershipStateTypeyAck` 2168 semantics (ownership transfer) — never examined.
- Fatal state on a player (DG §3.2). Revive type ≠ 2 / in-place revive.
- `byte [proj+0xE8]` origin; why puppet doesn't fire (PR §8).
- Selector used by AI graphs for 0x18 first acquisition (`PriorityDetectTarget`/`CheckAggroToPlayer`?) (AG §7).
- AAR `+0x1C` → `0xB55290` effect on local player (AG §4.1).
- Whether the cheat-permission component at `+0x130` really is `ServerContentsMiscActorComponent` (P4D §2.e).
- noDie = stat floor? (`SetMinStatRateAck`) (BO Q2). Boss record type 3/4/5 (BO Q1).
Risks:
- Denuvo-virtualised/mutated code in the key places for D: session creation/login (CC §4.3 d), post-death
  `.xtls 0xC76CF30`, damage record `.xtls 0x1046DA60`, vf43 `.xtls 0x10433BD0`, VaryHP writer caller
  `.xtls 0x10431D40`, FrameEventAccessor (de)serialisers `0x1F78F20/0x1F78DE0`, stat apply `.xtls 0x17AD2D0`/
  `.xtls 0xC7E7960`, AI condition `.xtls 0xC5DC280`, CTC emitter `.xtls 0x9DAA0F0`, rank writer, XP handlers.
  Hook only function entries (`safetyhook`); never mid-function inline hooks; `.xtls` should not be hooked (CO §5).
- Every game-memory read must be SEH/VirtualQuery protected; crashes are reported to Pearl Abyss (sentry/crashpad) (CLAUDE.md; P4D §3 M4).
- Telemetry `CharacterEventFunnelFormat` on player deaths (`DevCheat` for reason 16); unknown endpoint (RW §2.6, §7.6).
- Back up `%LOCALAPPDATA%\Pearl Abyss\CD\save` before any test that writes game memory or server state.
- Key counters: `0xB01` ~8/s ⇒ mod's `0xB010F00x` collide after ~2 h; `0xA01` rises on relogin (EA R8).
