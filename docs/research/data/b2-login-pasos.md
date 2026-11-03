# B2, steps 2319 → 2490: what the server does for session 2 (no user, key 0)

Build 1.0.0.2976, base `0x140000000`, all addresses are RVAs. Static only (capstone; new script
`%TEMP%\claude\b2b\idwr.py`, helpers in `rpt0\` and `b2\`). **[E]** = read in the disassembly,
**[E-x]** = read in mutated-but-readable `.xtls`, **[S]** = inference. `.xtls` VM entries are
named and not analysed.

## 0. Short answers

1. **Q1.** With key 0 the Req goes through the common FIFO. A worker pops it and the executor runs
   the handler with `TrocParams = {actor 0, session = item+0x48}`. There is **no** "one login",
   global-login or quota check on this path. The only gates are `*(s+0x88) == item+0x54` (0 == 0)
   and, inside the login, the save-manager busy byte (transient, §2.4).
2. **Q2.** With `account = 1` the new-user branch is guaranteed **even if** the VM lookup returned
   the host's user. Relogin compares account against `component+0x78`, and `UserLogin` writes −1
   there for every field login. So 1 ≠ −1 and relogin fails without side effects, and the handler
   then falls through to `CreateUser` (§2.1).
   - **The dangerous value is `0xFFFFFFFFFFFFFFFF`, not 0.** With −1 the relogin would match the
     host, rebind its user to session 2 and close session 1.
   - User key: `0x9010_0000 | idx`. `idx` is the next free index of the SAM key FIFO, so it is
     **not** necessarily `0x90100001`.
   - The user is attached through `user+0x1F8 = session` and `session+0x90` (via `0x1437400`).
     Nothing writes `*(s+0x88)` after login, and no global pointer is written.
3. **Q3.** All four Acks up to 2716 use `ToTargetSendTargetFinder` on session 2, so they reach
   **only** our vf9 Send. Nothing is addressed to session 1 (§3).
4. **Q4.** Every shutdown wait found is bounded or self-signalled. The candidate hang is outside
   these paths [S]. Before leaving: stop feeding, optionally log user 2 out natively, and never
   free session 2 (§4).

## 1. Deferred Req with key 0 and no user (Q1) [E]

Pop and dispatch:

| RVA | Code | Meaning |
|---|---|---|
| `0x27732F6` | `call 0x143A6F0` | deferred worker pops `ServerNet+0x38` |
| `0x143A98E` | `test edi,edi ; je 143AADE` | key 0 → return the value directly (no per-key subqueue) |
| `0x2773319` | `call 0x2774730(…, item)` | executor |

The executor (`r15 = item`, `r13 = [item+0x60]` Troc):

| RVA | Code | Meaning |
|---|---|---|
| `0x2774770` | `mov rsi,[rdx+0x48]` | **session = item+0x48** |
| `0x277479F` | `cmp [rdx+0x50],0 ; je 27748BD` | user key 0 → no-user path |
| `0x27748BD` | `test rsi,rsi ; je 27749CB` | — |
| `0x27748C6`-`0x27748D2` | `mov rcx,[rsi+0x88] ; mov eax,[rdx+0x54] ; cmp [rcx],eax ; jne 2774A80` | **only drop**: id changed since the push (0 vs 0 passes) |
| `0x27748D8`-`0x27748DF` | `cmp [rsi+0x90],rbx(=0) ; je 27749CB` | still no user → dispatch without actor |
| `0x27749D1`-`0x27749FD` | `[rbp-0x19] = rbx (0)`, `[rbp-0x11] = rsi`, `+0x10 = word [item+0x68]`, `+0x18 = [item+0x70]` | TrocParams `{actor 0, session}` |
| `0x2774A08` | `call 0x2774B20` → `troc->vf2(&res, &params)` (`0x2774B41`) | handler |
| `0x2774A11`-`0x2774A3C` | `res != 0` and `!session->vf(0x30)` → `session->vf(0x18)` Close | only on a handler error |

Notes:
- If the session already had a user when the item ran, `0x27748E5` looks the user up and passes
  it as the actor. The same handler still runs (`0x2C48FE5`, existing-actor branch).
- **Session 1 never produces key-0 items while it has a user.** Its Req carry the user key (b2
  §1, `0x2774066`). The key-0 lane is effectively session 2's.
- **The key-0 lane is not serialised.** Any of the deferred workers can run a key-0 item.
  - 2319 and 2490 must stay ordered by the script: it already waits for 2840.
  - 2490 runs **concurrently with the host's deferred Req** on other workers [S risk, §5].
- Gates checked and found absent:
  - **Quota:** none (D0 §1.3).
  - **Global "already logged in" or "single user" flag:** the only globals the login reads are
    `[0x6CEEDE8]` (single-player mode, read only, `0x2C1762F`), `[0x6D69698]` (SAM) and
    `[0x6D696A0]` (save manager).
  - **Global writes:** a scan for stores to `[rip+…]` in `0x2C48BE0`, `0x2C17520` and `0x186F570`
    gives **none**. The stats panel `0x1439540()+8` `+0xA8/+0xAC` is only `lock inc/dec`
    (`0x2C178DD`, `0x2C17923`, `0x2C17933`).

## 2. 2490 handler → login (Q2)

`0x2A21060` (vf2):
- `0x2A2109A`: size check.
- `0x2A210B5`: `Troc+0x21`.
- `0x2A2110E`-`0x2A2124E`: parses u64 account, u32, u8 and a wstring ≤ 0x5DC terminated by `u16 0`.
- `0x2A21298`: `call 0x2C48BE0(troc, params, &acc, &u32, &u8, &wstr)`.
- `0x2A2129D`: `*res = 0` always, so the handler result never closes the session.

### 2.1 Account lookup and relogin [E] (the lookup itself is VM)

```
2C48C0B  [rbp-0x60] = params+8 (session) ; [rbp-0x58] = account ; [rbp-0x50] u32 ; [rbp-0x4c] u8 ; name @-0x4a
2C48C6B  0x2A838E0(SAM, &out, &account)       -> jmp .xtls 0x10534880 (VM entry: not readable)
2C48C71  found? -> 2C48C90 0x2C17AB0(component=[[user+0x68]+8], &res, &loginstruct)
2C48C96  cmp res,0 ; je 2C4926E               relogin OK -> return.  relogin FAILED -> fall through to new user
```

Relogin `0x2C17AB0`: it compares everything first and has **no side effect before the compares**
(lock only):

| RVA | Comparison |
|---|---|
| `0x2C17AFE` | token string `+0xBD0` (empty here → skipped) |
| `0x2C17B3E` | `+0x14` vs `comp+0x80` |
| `0x2C17B60` | name vs `comp+0x88` |
| `0x2C17B7E` | `+0x10` vs `comp+0x84` |
| **`0x2C17B88`** | **`+8` (account) vs `comp+0x78`** |
| `0x2C17B8E` | mismatch → `*res=[0x6CDB948]`, unlock, return (`0x2C17E5A`) |

On a match, `0x2C17BFC` sets `user+0x1F8 = new session` and the old session is closed: the host is
stolen and kicked.

`comp+0x78` of a field-login user is **−1** [E]:
- `UserLogin 0x2C1759D` does `mov [rsi+0x78], r14(-1)` whenever `[user+0x5E] != 2` (a fresh user is
  state 1, `0x2C17808`).
- The other writers in `0x2C10000`-`0x2C20000` are `0x2C16F38` and `0x2C17128` (−1), and
  `0x2C1729E`. That last one is a counter, but only in the auth path `0x2C16F60`, called from
  `0x2C489BC`, which this game does not use.
- The auth path itself treats −1 as "no account" (`0x2C48619` `cmp [acc],-1` skips the lookup).

Consequences:
- **`account=1` (the mod, `server_inject`) always ends in `CreateUser`.** `account=0` would too:
  the claim in `opcion-d-login.md` §4.2/§6 that "0 = relogin of the host" is wrong.
- **Never write `0xFFFFFFFFFFFFFFFF`.** It would match the host (u32 0, u8 0 and empty name are
  equal as well) and kick it.

### 2.2 New user and attachment to the session [E] unless marked

```
2C48CB7..2C48D45  ToTarget(params+8 = session 2) -> 0x2A1E9D0 ServerTimeInfoAck (writes 0x88C @0x2A1EAF2)
2C48D81  rbx = params+0 (actor) ; r13 = params+8 (session) ; actor==0 -> 2C48DA0 SAM::CreateUser 0x2A83000
2C48DFE  CreateUser failed -> 2C4906A Nak 0x3F5 (error [0x6CF6B48]) via 0x2773630 (.xtls) to r13
2C48E1B  rsi = 0x1437400(session, user)   (.xtls 0xB8A6C70, mutated) [S: swaps session+0x90, returns the old user]
2C48E26  old user != 0 -> kick it (+0x98/+0x9C/+0x5C, vf 0x80, vf 0x110)  — session 2 has none
2C48F5A  rbx = [user+0x1F8] ; [user+0x1F8] = session 2         <- back-pointer
2C48F7B  strncpy_s(user+0x190, 0x65, session+0x18)              <- session name: the mod's "2\0" is fine
2C48FD1  old session != this and != 0 -> old->vf(0x18) Close     — new user: +0x1F8 was 0 (D0 §1.3)
2C4911F  UserLogin 0x2C17520([[user+0x68]+8], &res, &loginstruct)
2C49126  res != 0 -> Nak 0x3F5 (u16 req opcode @7, error @3) to [params+8] via 0x2773630
```

- `CreateUser 0x2A83000`:
  - fails only when the key FIFO is empty (D0 §1.3);
  - list `SAM+0xB8` (`0x2A831D4`) and map `SAM+0x98` (`0x2A83222`) are written **under the SAM lock
    `SAM+0x68`** (`0x2A83192` … `0x2A83248`);
  - `++SAM+0xA4` per actor.
- **User key** [E arithmetic + V]:
  - `0x2A83603`-`0x2A83621`: `high = (([user+0x60] & 0xFFF00000) + 0x100000) >> 20 → user->vf(0x20)(high)`;
  - then `0x2A837E0(SAM, key)` registers the deferred subqueue (table 1, since class bits ≠ 0).
  - The popped value is `0x80000000 | idx` (D0 §1.2) with class 1. That gives
    **`0x9010_0000 | idx`** (the host got `idx = 0` → `0x90100000`).
  - `idx` is the FIFO head when 2490 runs: after the host's 101 keys and every NPC/gimmick key
    handed out, so expect something like `0x9010_0xxx`, not `…0001` [S]. A recycled index carries
    its stored generation [S, the pop is VM].
  - The script learns it from `MainUserDataStartAck @0`, which is correct.
- **`*(s+0x88)`:** no writer in the network or login code [E].
  - `idwr.py` scans every `mov rX,[rY+0x88]` followed by a dword store through `rX`. There are no
    hits in `0x1434000`-`0x143C000`, `0x2770000`-`0x2778000`, `0x2A1xxxx` or
    `0x2C1xxxx`-`0x2C4xxxx`.
  - After login, OnRecvPacket keys the session's deferred Req by the user (`[user+0x60]`/`+0x58`),
    and the executor's user branch never reads `s+0x88`. **Writing 0 once in `build_session` is
    enough.**

### 2.3 `UserLogin 0x2C17520` [E]

| RVA | What it does |
|---|---|
| `0x2C17585` | state ≠ 2 → name to `comp+0x88` (`0x2C187A0`); `comp+0x78 = comp+0x18 = -1`; `comp+0x70 = 0` |
| `0x2C175AC` | `comp+0x84 = u32` |
| `0x2C1762F` | **single player (`[0x6CEEDE8] != 0`)**: `comp+0x6C = -1`; `comp+0x20` = "CrimesonDesert"; `comp+0x18 = 1` |
| `0x2C17687`-`0x2C176C8` | save manager `= [[0x6D696A0]+0x30]`; **busy byte `+0x60`: if already 1 → `*res = [0x6CF7CA4]` and return** |
| `0x2C176DA` | `0x1870C20(mgr)`, then the byte is cleared (`0x2C176F1`) |
| `0x2C17703`-`0x2C17738` | unless the command-line flags `SafeLaunchDummyTest` / `@@@NO_SVC_COMMAND_141` are set |
| `0x2C17740`-`0x2C17783` | picks the **most recent save** among slots 0-2 and 100-108 (`[e+8]` = time) |
| `0x2C17881` | **`0x186F570(mgr, &res, slot, user2)`**: the same busy byte (`0x186F6BF`); `0x20688A0(user2, …, path, slot)` loads that save **into user 2** (`0x186F78A`); slot 206 would be deleted (`0x186F850`), but 206 cannot be picked here |
| `0x2C17808` | user state must be 1, else error `[0x6CDB930]` |
| `0x2C178F3`-`0x2C17942` | state 1 → 2 → 4 |
| `0x2C179C9` | `0x2A1EE40` **MainUserDataStartAck** (writes `0xA56` @`0x2A1EF4F`) to ToTarget(`[user+0x1F8]`) |
| `0x2C179E5` | if `user+0x1F8 != 0`: `0x2B7FC70([[user+0x68]+0x10])` → `.xtls 0x1069C980` [E-x]: builds the list (`0x28FDDB0`), then `0x2A1BD40` **CharacterLobbyDataAck** (`0xB35` @`0x2A1BE8C`) with ToTarget(`user->vf(0xE8)()` = the user's session) (`0x1069C9E9`-`0x1069CA1E`) |
| `0x2C17A08` | `0x2A1F280` **MainUserDataEndAck** (`0xA9C` @`0x2A1F377`) to ToTarget(`[user+0x1F8]`) |
| `0x2C17A12` | `*res = 0` |

So **E2 already loads a save into user 2** (the 184 ms gap in 112627). The host's saves are
read-only; P6 blocks writes. What `0x20688A0` does globally was not analysed [S].

### 2.4 Failure modes that give no 2716 [E]

| Cause | Result |
|---|---|
| Save-manager byte `+0x60` held (another load or save in flight, e.g. a host autosave) | `[0x6CF7CA4]` → **Nak 1013** to session 2 |
| Load failure in `0x186F570` | error → Nak 1013 |
| Key FIFO empty | Nak 1013 (`0x2C4906A`) |

- Nak 1013 is 17 B: `u8 0xFF`, error u32 @3, req opcode u16 @7.
- Whether the Nak is sent depends on two config lookups (`0x1427C10`, type 2/1) [S].
- After a UserLogin failure, user 2 **stays created and attached** (state 1).
- A re-sent 2490 goes through the existing-actor branch `0x2C48FE5` and runs UserLogin again [E
  for the branch, S that it then succeeds].

## 3. Where each Ack goes (Q3)

| Ack | Writer | Finder and target | Path |
|---|---|---|---|
| 2840 ConnectCryptogramToServerAck | `0x2A1F680` | ToTarget(TrocParams+8 = s2) | b2-ack2840 §2 [E] |
| 2188 ServerTimeInfoAck | `0x2A1E9D0` (from `0x2C48D45`) | ToTarget(`[params+8]`, `0x2C48CB7`-`0x2C48CD8`) = s2 | [E] |
| 2646 MainUserDataStartAck | `0x2A1EE40` (from `0x2C179C9`) | ToTarget(`[user2+0x1F8]` = s2, set at `0x2C48F61`) | [E] |
| 2869 CharacterLobbyDataAck | `0x2A1BD40` (from `.xtls 0x1069CA1E`) | ToTarget(`user2->vf(0xE8)()`) = s2 | [E-x] |
| 2716 MainUserDataEndAck | `0x2A1F280` (from `0x2C17A08`) | ToTarget(`[user2+0x1F8]`) = s2 | [E] |
| 1013 Nak (errors only) | `0x2773630` (`.xtls`) | session argument = `[params+8]` / r13 = s2 | [S] |

- Every writer has the same shape as `0x2A1F680`:
  - `finder->vf1` (`0x2A1EE96`);
  - the ScopeSender check `0x2961CA0`, which compares finder+0x10, so it is per session;
  - `0x2771B30` allocates;
  - count 1 → `session->vf(0x48)` Send (`0x2A1F170`); count > 1 → `SendPacket 0x2773870`.
- **Nothing in 2319 → 2716 reaches session 1 or the host's client.**
- The old-user kick (`0x2C48E26`) and the old-session Close (`0x2C48FD1`) cannot fire for a fresh
  session and a fresh user.
- **After 2716 (new, harmless):**
  - the periodic `ServerTimeInfoAck` from `.xtls 0x10855376` uses **ToWorldSendTargetFinder**
    (vt `0x5B25318` @`0x1085530B`) [E-x];
  - with two users it goes to both sessions through `SendPacket`, so session 2 starts receiving it
    every ~5 s;
  - the host gets nothing it did not get before.

## 4. Shutdown with a half-logged user 2 (Q4)

Exit chain [E]: `ServerModule` shutdown `0x27C1260` (from `0x27BE780`).

| RVA | Step | Bound |
|---|---|---|
| `0x27C1298`-`0x27C12B3` | **`SetEvent([ServerNet+0])`** (KernelEvent vt `0x57216D0`: vf2 `0x1414F90` SetEvent, vf3 `0x1414FA0` Wait) | — |
| `0x27C12C0`-`0x27C12D7` | `while (!Wait(1000))` on the same event | ends at once (self-signalled) [E, assuming `[0x6D69680] == module+0x300`]; the timer `ShutDownServerOnceTimer` 2038 vf2 `0x2C327F0` also sets it |
| `0x27C12E0` | **SAM shutdown `0x2A84160`** | — |
| `0x2772400` | `ServerNet::Stop` ×2 | b2 §7 |
| `0x27C1310`-`0x27C1329` | `while [SN+0x108] Sleep(1000)` | Stop clears `+0x108` at its end (`0x27725DC`) [E] |

SAM shutdown `0x2A84160` (also reached from `LogoutAllActorOnceTimer` 2063, `0x2C34EE3`):
- **loop A**, ≤ 300 × `Sleep(10)` (`0x2A841A2`, `0x2A8444F`): for each user in `SAM+0xB8`, lock it,
  set `+0x98 = [0x6CDBA90]`, `+0x9C = 5`, call `vf(0x80)` (kick), then `vf(0x110)`;
- **loop B**, ≤ 300 × `Sleep(10)` (`0x2A844E0`-`0x2A84504`): waits for `SAM+0x9C == 0`
  (`0x2A845A4`).
- **Both are bounded, about 3 s each** [E].

So, statically:
- No unbounded wait depends on session 2 or user 2.
- `Stop` is the longest: up to about 2 min per phase if a server thread does not exit.
- User 2 with no character is kicked by loop A like any user: `vf(0x80)` → logout, which sends
  Acks to `user2+0x1F8` = our Send. That still runs **before** Stop, so Q2 is alive.
- The real hang must be elsewhere: client title loader, `.xtls`, or a mod lock [S]. To localise
  it, the mod should log entry and exit of `0x27C1260`, `0x2A84160` and `0x2772400` (or take
  thread stacks).

**What the mod should do before the host leaves the world:**
1. Stop injecting and wait for the outgoing burst to drain.
2. Optionally, a native logout of user 2:
   - inject `LogoutSessionToServerReq` 2001 for session 2;
   - its vf2 `.xtls 0x1089A360` [E-x] calls `0x2068340(actor = user2, &res, 4, [0x6CDB938])`, and
     with no actor it calls `session->vf(0x18)` Close;
   - it is an internal Troc like 2319 (`flags_14 0xFF0000`, deferred), so routing it needs the
     same treatment as 2319 [S].
   - If you do not do this, SAM shutdown kicks user 2 anyway (bounded).
3. Set `g_session2 = 0`.
   - **Never free session 2 and never call its vf0.** The user's `+0x1F8` and the kick path still
     point at it until the SAM is gone.
   - `session2_send` must stay valid as long as the DLL is loaded.

## 5. Risks and fixes for B2, most likely first

1. **Save-manager busy byte, or the save load in UserLogin** (§2.3, §2.4).
   - A host autosave or load in flight, or a `0x186F570` failure, gives a Nak 1013 instead of
     2716.
   - **Fix:**
     - make the runner treat Nak 1013 on session 2 as a failure and log its error u32 @3;
     - re-send 2490 once after 1-2 s (the existing-actor branch re-runs UserLogin);
     - do not start B2 during an autosave.
   - Also check FieldTime and other world state before and after 2716 (the mod's probe already
     logs FieldTime), because `0x20688A0` is unread [S].
2. **Concurrency.** The key-0 item runs on any deferred worker while the host plays.
   - SAM lists are locked [E]. UserLogin and the save load race with the host's Req only through
     the save manager (byte-guarded) [S].
   - **Fix:** keep 2319 and 2490 strictly ordered (the script does: `expect 2840` before 2490);
     never send two key-0 Req at once.
3. **Account value.** 1 is correct, and so is 0. **Forbid `0xFFFFFFFFFFFFFFFF`** in config
   (`config.cpp:155`): it would match `comp+0x78 == -1` of the host, and the relogin would kick the
   host (§2.1).
   - Also fix `opcion-d-login.md` §4.2/§6: "0 = relogin" is false.
4. **User key expectations.** Do not assume `0x90100001`. Read `MainUserDataStartAck @0`, as the
   script does. `server_user2_present()` checks `+0x90`, the vtable and `+0x1F8`, which is right.
5. **Expect 2188 periodically on session 2 after login** (ToWorld). The outgoing queue (32 MB)
   must keep draining.
6. **Shutdown** (§4).
   - Stop feeding before the host leaves; optionally inject 2001 for session 2.
   - Do not free session 2.
   - Add entry/exit logs on `0x27C1260`, `0x2A84160` and `0x2772400` to locate the hang.
   - No static path makes the server wait on a half-logged user 2.
