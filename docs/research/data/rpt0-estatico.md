# RPT-0 — Static audit of the Sentry error-reporting path

Static, read-only disassembly of `CrimsonDesert.exe` (build 1.0.0.2976, PE TS `0x6AB28F00`,
image base `0x140000000`). All RVAs are `VA - 0x140000000`. Bytes/disassembly quoted from the
on-disk image (capstone 5). The game was never run and nothing was written into the game folder.
Helper scripts: `<scratch>\rpt0\` (`pe.py`, `d.py`, `xref.py`).

Convention tags: **[E]** proven by the quoted bytes; **[S]** inference from them.

## Section map (Denuvo renamed; trust flags, not names)
`.rsrc` RVA `0x1000` flags `0x60000020` = EXEC|READ|CODE → **holds the Sentry glue code**.
`.xtls` RVA `0x75C9000`..`0x16F0F800` flags `0xE0000020` = Denuvo-protected (readable, never hook).
All functions below (`0x1315CD0`, `0x1317D40`, `0x1321F90`, `0x2356270`, `0x13003D0`, `0x1317BB0`,
`0x1319xxx`, `0x1320E00`, `0x33117xx`) resolve to `.rsrc` (verified with `pe.py sec()`), **not `.xtls`**.
The singleton accessor `sub_1318E70` is a `.rsrc` thunk `jmp 0x14B686EB0` **into `.xtls`** (body protected).

---

## Q1 — The "reporter active" byte: address, readers, writers, re-enable

**[E] It is NOT a fixed-RVA static.** It is a 1-byte field of a runtime singleton object `M`
(the per-session reporter). Every site reaches it through a **qword pointer stored at `M+0x1988`**:
`sub_1318E70` returns `M` in `rax`, then `mov rcx,[rax+0x1988]` yields the byte pointer; the byte is
at offset 0 of the pointee. The companion config byte is `M+0x1990` (see Q3).

Writer **=1** (unique): `0x131623B  c6 07 01  mov byte [rdi],1`, with `rdi` = arg1 of the setup
function `sub_1315CD0` = the active-byte pointer (caller `0x13192CB  mov rcx,[r14+0x1988]`). Executed
**only if `sentry_init` returned 0**: `0x131622C  test eax,eax` / `0x131622E  jne 0x1317B37`.

Writers **=0** (shutdown): 
- `0x131C298  c6 00 00  mov byte [rax],0` (`rax=[rcx+0x1988]` @`0x131C287`) just before
  `0x131C29B call sentry_close`. 
- `0x13250AA  c6 03 00  mov byte [rbx],0` (`rbx=[rcx]` @`0x132509A`) before `0x13250AD call sentry_close`.

Readers (`cmp byte [reg],0`, reg = the +0x1988 pointer):
- `0x1317A60  48 8b 8d f0 0a 00 00` then `80 39 00` — inside `sub_1315CD0` (re-check after init).
- `0x1317D60  80 39 00` — inside reporter `sub_1317D40` (arg1 = `[M+0x1988]`).
- `0x1323482  80 3b 00 0f 84 b5 00 00 00` — inside minidump path (`rbx=[r12+0x1988]` @`0x132347A`).
- `0x131C293`, `0x13250A5` — the two shutdown paths (read before clearing).
- `0x1317BBD`/`0x1317BDF` — global-attach helper `sub_1317BB0` (arg1 = ptr to the byte).
- crash dispatcher `~0x1323843` (`cmp byte [mem]` gating `sentry_handle_exception`).

**[E] Same byte at `0x1317D40` and `0x1323482`?** Yes — both dereference the identical pointer
`[M+0x1988]` (`0x235642E/0x33119D0 mov rcx,[rax+0x1988]` feed `0x1317D40`; `0x132347A mov rbx,[r12+0x1988]`
feeds `0x1323482`), and the shutdown writer clears that same `[M+0x1988]` pointee (`0x131C287/298`).

**[S] Re-enable?** The value `1` is written at exactly one site (`0x131623B`), inside `sub_1315CD0`,
which has a single direct caller (`0x13192D2`). No other `mov byte [*],1` targets this field. So the
reporter can only be (re)armed by re-running the init routine; shutdown disarms it. No stray writer found.

---

## Q2 — `sub_1317D40` and `sub_2356270`: section, heads, ABI, callers

**Section:** both `.rsrc` (EXEC/READ/CODE) — safe to hook, **not `.xtls`**.

First 32 bytes (safetyhook head check):
- `0x1317D40`: `48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 80 00 00 00 4D 8B F0 48 8B FA 48 8B D9`
  (`mov [rsp+10],rbx; push rbp/rsi/rdi/r12/r13/r14/r15; sub rsp,0x80; mov r14,r8; mov rdi,rdx; mov rbx,rcx`).
- `0x2356270`: `48 89 5C 24 08 48 89 74 24 10 55 57 41 54 41 56 41 57 48 8D 6C 24 A0 48 81 EC 60 01 00 00 4C 8B`
  (`mov [rsp+8],rbx; mov [rsp+10],rsi; push rbp/rdi/r12/r14/r15; lea rbp,[rsp-0x60]; sub rsp,0x160; mov r14,rdx`).

**`sub_1317D40` ABI from the call site `0x2356434`** (`e8 07 19 fc fe`): 
`rcx = [M+0x1988]` (ptr to active byte), `rdx = r14` (the fail reason / message text, arg2 of `0x2356270`),
`r8 = &vector` (lea `rsp+0x30`). Inside: `0x1317DD1 mov rsi,[r14]` / `0x1317DD4 mov eax,[r14+8]` →
the arg is a `{ptr data; u32 count}` vector of wide-string handles; each element `[rsi]→[.]` is passed to
`sentry_scope_attach_filew`. **Returns:** effectively void — epilogue `0x1317EF0` restores and `ret` with
no value established; on the success path it only logs via `sub_35D8D0` if the event UUID is nil
(`0x1317EA6 sentry_uuid_is_nil`; log string `0x57110C0` = `"[Pentry] sendMessageEvent 실패"`). 
**Caller use of return:** ignored — `0x2356439 inc byte [0x6D9CCE8]` directly after; second caller
`0x33119D7` falls into `0x33119F5 call sub_394DB0`. Neither reads `rax`.

**Direct callers of `sub_1317D40`** (`xref.py callers`): `0x2356434` (save-fail reporter) and
`0x33119D7` (a second "sendMessageEvent" route in `.rsrc` at `~0x33117xx`; builds a path vector,
pulls the extra log path from `sub_13003D0` @`0x331185F`, re-fetches `M` @`0x33119C3`,
`mov rcx,[rax+0x1988]` @`0x33119D0`). **Both in `.rsrc`.**

**Direct callers of `sub_2356270`**: `0xEF51913`, `0xEF519B8`, `0xEF51A63`, `0xEF51B07` — **all in `.xtls`**
(the Savedata Load/Valid/Save/Delete fail vtable stubs). The save-fail reporter is only invoked from
Denuvo-protected code.

---

## Q3 — `0x1315CD0..0x1316300`: sentry_* imports, attachments, init gating

Imports resolved from the exe IAT (`.rdata` `0x51EC940..0x51ECA28`, 30 `sentry_*` symbols). Calls inside setup:
- `0x1315E7A sentry_options_new`
- `0x1315E91 sentry_options_set_dsn` (rdx=`0x5711280`; DSN host is a `pentry-relay` Pearl Abyss endpoint — **secret not reproduced**)
- `0x1315E9F sentry_options_set_auto_session_tracking(1)` ; `0x1315EAD sentry_options_set_symbolize_stacktraces(1)`
- `0x1315ECB sentry_options_set_database_pathw` (wide `"%#/DumpCache"` @`0x5711258`)
- `0x131605C sentry_options_set_handler_pathw` (wide `"%#/crashpad_handler.exe"` @`0x57111F0`),
  **guarded** by `0x131602D GetFileAttributesW` + `0x1316038 cmp eax,-1` / `0x1316040 test al,0x10` (file must
  exist and not be a dir), else error string `"[CRASH] crashpad… 찾을 수 없습니다"` @`0x5711220`.
- `0x1316074 sentry_options_set_on_crash` (callback `sub_1317F10` @rdx `0x1317F10`)
- `0x1316179 sentry_options_set_backend(opts, 0)` — taken only when arg2 is 0 (see gating).
- `0x131618E sentry_options_set_environment` (`"blackspace_release"` @`0x57112E0`)
- `0x131621D sentry_options_set_release` (format `"%#@%#"` @`0x57112F4`)
- `0x1316226 sentry_init`
- post-init tags: `0x131624C set_tag("BuildKey", "BlackSpace_CD_Cert_ReleaseSteam_x64_cd_alpha_2026_09_22_13_37_39")`
  (`0x57112C8`/`0x5709770`); `0x1316267 set_tag("CodeRev","5129333")` (`0x57112D8`/`0x55CD0F0`);
  `0x1316287 set_tag("Publisher", …)` (`0x5711078`).

**[E] Negative evidence — NOT imported, hence never called:** `sentry_options_add_attachment(w)`,
`set_before_send`, `set_require_user_consent`, `user_consent_*`, `set_max_breadcrumbs`,
`set_auto_session_tracking` is present but `set_transport`/`set_screenshot`/`attach_screenshot`/
`set_debug`/`set_shutdown_timeout`/`set_handler_path` (narrow) are **absent**. **No attachment is added at
init**, and **there is no user-consent gate anywhere in the exe**. A `temp\dump_screenshot.png` path does
not exist as a sentry attachment; all file attachment happens later via `sentry_scope_attach_filew`
(per-event, `0x1317E24`) and `sentry_attach_filew` (global scope, `0x1317BE7`).

**[E] Condition that skips `sentry_init` entirely:** none inside `sub_1315CD0`. The only branch is
`0x1315ED1 test r12b,r12b` / `0x1315ED4 je 0x1316174`, where `r12b = dl` = arg2 = `[M+0x1990]`
(caller `0x13192C3 movzx edx,byte [r14+0x1990]`). `dl!=0` → configure crashpad handler + on_crash;
`dl==0` → `set_backend(NULL)`. **Both paths rejoin at `0x1316184` and call `sentry_init`.** So `M+0x1990`
toggles the crashpad/minidump backend, not whether telemetry initializes. [S] Where `M+0x1990` is set was
not located statically (likely a config/launch field); `sub_1315CD0` has one caller (`0x13192D2`).

---

## Q4 — `sentry_scope_attach_filew` return, and the path list

**[E] `0x1317E24` return value is ignored.** After `sentry_scope_attach_filew` (`0x1317E24`) the code does
`0x1317E2A nop` then releases the local wstring (`lock xadd [rbx+0x10]` refcount @`0x1317E3C`); `rax` is
never tested. Same for the global variant `0x1317BE7` (`0x1317BED nop`).

**[E] The iterated list** is the `r8` arg of `sub_1317D40`: struct `{ void* data @+0x0; u32 count @+0x8 }`
(read at `0x1317DD1`/`0x1317DD4`). Each slot is a pointer to a wide-string handle; `sub_44FD30`
(`0x1317E0E`) resolves it to a `PCWSTR` passed to the attach import.

**[E] Where it comes from in `0x2356270`:** vector at `[rsp+0x30]` (`data @rsp+0x30`, `count @rsp+0x38`).
It is filled with the failed slot's `*.save` files: root built by `sub_64C240` (`0x2356322`, fmt `"%#/%#"`
@`0x5578E3C` from `H+0x20` root and slot name `S+0x28`), enumerated by `sub_1328100` (`0x235634D`,
mask wide `"*.save"` @`0x57DB820`). Then an **optional extra path** from `sub_13003D0` (`0x2356362`) is
appended via `strncpy_s` (`0x23563B3`) + `sub_44B9E0` push (`0x23563F4`) at `0x2356406`. Fail reason text
region `0x5983908` = `"[INFO] Savedata Save Fail Event"`. **Confirmed:** the slot's `*.save` files plus one
optional extra. (Prior §4.2 stands.)

**[E] What `sub_13003D0` returns:** it walks a collection `[rcx]` (count `[rcx+8]`), calls virtual
`[vtbl+0x30]` on each entry to get its string (`0x13003FB`), and `strstr`s it against `".json"` @`0x570D2C4`
(`0x1300410`). It returns (`0x1300482 mov rax,rsi`) the **first entry whose path does NOT contain `.json`** —
i.e. the active **plaintext log-file path** among the registered log sinks; if every sink is `.json` it builds
a fallback via `sub_35D8D0` and returns null. Nearby string `0x570D168` = `"log file …없습니다"`
("log file does not exist"). **[S] The extra attachment is the current text log file.**

---

## Q5 — Every file-attach / capture / minidump call site

All resolved via IAT xref (`xref.py iat`). Call sites (all `.rsrc`):
- `sentry_scope_attach_filew` — **1 site** `0x1317E24` (inside reporter `sub_1317D40`, per-event local scope).
- `sentry_attach_filew` — **1 site** `0x1317BE7` (inside helper `sub_1317BB0`, attaches to the **global** scope;
  head `0x1317BB0 48 89 5C 24 10 57 48 83 EC 20 48 8B F9 80 39 00`, guards on the active byte at `0x1317BBD`).
  `sub_1317BB0` has **7 callers**, all in the crash dump-collection cluster: `0x1320F5E, 0x13213EC, 0x1321419,
  0x1322306, 0x132233C, 0x1322D00, 0x1322D7E` (enclosing `sub_1320E00`, reached from on_crash `sub_1317F10`
  @`0x1317F2B` and from the minidump path). Paths are computed wide-strings (not static literals).
- `sentry_capture_event_with_scope` — **1 site** `0x1317E91` (inside `sub_1317D40`). Event is a
  `sentry_value_new_message_event(0,"Message",reason)` built at `0x1317DC8` (logger `"Message"` @`0x558D010`);
  tag `"Project"="BlackSpace"` set at `0x1317DB6` (`0x57110B8`/`0x5711098`).
- `sentry_capture_minidump` — **1 site** `0x1323493` (minidump path `sub_1321F90`); UUID checked
  `0x13234A5 sentry_uuid_is_nil`, then `0x1323501 sentry_flush(0x1388)`. Active-byte gate at `0x1323482`.
  `sub_1321F90` is called from `0x1323864`, right after `0x132385C sentry_handle_exception` (crash dispatcher,
  `cmp byte` gate `~0x1323843`; progress log `0x5711D78 "[Crash] 덤프 보내는 중……"`).
- `sentry_handle_exception` — **1 site** `0x132385C`.
- `sentry_capture_event` (non-scope) — **not imported, no site.**

Routes that can attach files: (a) save-fail `sub_2356270 → sub_1317D40` (per-event `.save` + log);
(b) second `sendMessageEvent` route `0x33117xx → sub_1317D40` (per-event list + log from `sub_13003D0`);
(c) crash collection `sub_1320E00 → sub_1317BB0` ×7 (global scope, feeding the minidump at crash time).

---

## Q6 — Teardown / consent / level imports

- `sentry_close` — imported; **2 sites** `0x131C29B` and `0x13250AD` (both clear the active byte first; Q1).
- `sentry_flush` — imported; **1 site** `0x1323501` (after a non-nil minidump UUID).
- `sentry_shutdown`, `sentry_user_consent_give/revoke/reset`, `sentry_set_level`,
  `sentry_options_set_require_user_consent`, `sentry_options_set_before_send` — **not in the IAT → absent.**
- `sentry_set_user` — imported; **2 sites** `0x1319343`, `0x13193A1` (inside `sub_1318ED0`, which also builds
  `sentry_value_new_user`); contents not identified here (prior §4.1 unchanged — not asserted to be the SteamID).

---

## Summary of proven vs inferred
**Proven [E]:** the active flag is `*(*(M+0x1988))`, set=1 only at `0x131623B` after `sentry_init==0`,
cleared=0 at `0x131C298`/`0x13250AA` before `sentry_close`; `sub_1317D40`/`sub_2356270` are in `.rsrc`
with the heads above; `0x1317D40(rcx=flagptr, rdx=reason, r8=wstring-vector)` returns void and callers
ignore it; init adds **no** attachments and has **no consent gate**; attachments flow only through
`scope_attach_filew`(1 site) + `attach_filew`(1 site, 7 callers); single minidump/flush/handle_exception
sites; `sub_13003D0` returns the non-`.json` (text) log path. **Inferred [S]:** `M+0x1990` origin (backend
toggle) and the computed crash-cluster filenames were not pinned statically; `sub_1318E70` body is in `.xtls`.
