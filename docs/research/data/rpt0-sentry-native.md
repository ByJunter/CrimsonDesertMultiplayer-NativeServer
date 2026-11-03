# rpt0 — sentry-native 0.12.5 (crashpad backend, Windows): attachments, persistence, consent, close

Source: github.com/getsentry/sentry-native tag `0.12.5` (raw files fetched 2026-10-08), crashpad submodule
`getsentry/crashpad@d4694e6e94f9a63a4ed4443263a83bb78d93510f` (the commit pinned by the tag).
Path abbreviations: `core`=src/sentry_core.c, `scope`=src/sentry_scope.c, `att`=src/sentry_attachment.c,
`env`=src/sentry_envelope.c, `db`=src/sentry_database.c, `opt`=src/sentry_options.c, `sess`=src/sentry_session.c,
`whttp`=src/transports/sentry_transport_winhttp.c, `cp`=src/backends/sentry_backend_crashpad.cpp,
`pathw`=src/path/sentry_path_windows.c, `sync`=src/sentry_sync.c, `h`=include/sentry.h.
Crashpad: `cpcli`=client/crashpad_client_win.cc, `cpup`=handler/crash_report_upload_thread.cc,
`cpexc`=handler/win/crash_report_exception_handler.cc, `cpmain`=handler/handler_main.cc, `cpdbw`=client/crash_report_database_win.cc.
Labels: **[code]** = proven by quoted code; **[inf]** = inference.

## 1. Attachments (local scopes): when the file is read

- **[code] Attach time stores a path only, nothing is read.** `sentry_scope_attach_filew_n` → `sentry__attachments_add_path(&scope->attachments, sentry__path_from_wstr_n(path, path_len), ATTACHMENT, NULL)` (scope:641-646).
  `sentry__attachment_from_path` only does `attachment->path = path;` (att:71-85). It does not open or stat the file.
- **[code] Return value.** It returns the new `sentry_attachment_t*`. It returns NULL only if `path` is NULL or allocation fails (att:74-81, pathw:206-209).
  A nonexistent file still returns a non-NULL pointer. A duplicate path returns the *existing* entry and frees the new one (att:168-172).
  `scope` is not NULL-checked, so passing `scope=NULL` dereferences NULL (scope:644).
- `sentry_attach_filew` (global) does the same, plus `backend->add_attachment_func` (core:1708-1721, 1792-1795). For crashpad this calls `client->AddAttachment(path)` (cp:780-801). See section 3.
- **[code] Read time = capture time, synchronously on the calling thread, while the envelope is built.**
  - `sentry_capture_event_with_scope` → `sentry__capture_event` → `sentry__prepare_event` (core:530-536, 551-568).
  - That merges attachments and calls `sentry__envelope_add_attachments(envelope, all_attachments)` (core:664-711).
  - For each file: `sentry__envelope_add_from_path` → `sentry__path_read_to_buffer(path,&len)` (env:628-643). Windows uses `_wfopen(path_w, L"rb")` and fread of the whole file (pathw:641-693).
  - The header doc agrees: "Attachments are read lazily at the time of `sentry_capture_event`, `sentry_capture_event_with_scope`, or at the time of a hard crash" (h:40-41).
- **[code] Content is copied into the envelope.** `item->payload = buf` (env:137). The bytes live in memory; the transport worker later only serializes the envelope (whttp:199-200 → transport.c:229-232). The worker never touches the original file.
- **[code] Missing or unreadable file:** `read_to_buffer` returns NULL → `SENTRY_WARNF("failed to read envelope item from ...")` and returns NULL (env:637-640). `sentry__envelope_add_attachment` then returns NULL (env:584-586).
  `add_attachments` ignores the result and continues (env:612-615). **The event is still sent without that attachment.**
- **[code] Size cap:** `MAX_READ_TO_BUFFER = 134217728` (128 MiB). Larger files return NULL and are skipped (pathw:23, 663-666). A 0-byte file gives an empty item (pathw:652-661).
- **[code] Ownership of the local scope:** the header says "Takes ownership of `scope`" (h:1764). `sentry__prepare_event` calls `sentry__scope_free(local_scope)` right after merging (core:665-671), before `before_send`. The caller must not free or reuse it.
  - Edge: if the SDK is not initialized (or already closed), `SENTRY_WITH_OPTIONS` does not run. Only `sentry_value_decref(event)` happens (core:605-607). **The local scope is leaked, not freed.**
  - A local scope only merges breadcrumbs plus its own fields (`SENTRY_SCOPE_BREADCRUMBS`, core:667). The global scope's attachments are appended too (core:679-681).
- [code] Only global attachments (`sentry_attach_file*` / `sentry_options_add_attachment*`) reach crashpad. Local-scope attachments never go to crashpad (scope:641-646 has no backend call).

## 2. Offline persistence / `<uuid>.run`

- **[code] No retry or offline cache for HTTP sends.**
  - The WinHTTP task sends once. On `WinHttpSendRequest` failure it only logs `"WinHttpSendRequest failed..."` (whttp:278-342).
  - The task's cleanup is `sentry_envelope_free` (whttp:364-365), so the envelope is dropped.
  - Same when the DSN is invalid: `if (!dsn || !dsn->is_valid) return NULL;` → nothing is sent (transport.c:225-227, whttp:199-204).
- **[code] Only envelopes still *queued* at `sentry_close()` are written to disk.**
  - After `sentry__transport_shutdown(..., shutdown_timeout)` (default 2000 ms, sentry_options.h:14), `sentry__transport_dump_queue(options->transport, options->run)` runs (core:352-360).
  - That calls `sentry__winhttp_dump_task` → `sentry__run_write_envelope` → `<db>/<uuid>.run/<event-id>.envelope` (whttp:368-382, db:102-139).
  - Attachments are inside the envelope, so they are persisted with it.
- [code] The crash handler also dumps the queue to the run folder (cp:387).
- [inf] If the process dies or exits without `sentry_close`, queued in-memory envelopes are lost; nothing writes them earlier.
- **[code] Next start:** `sentry__process_old_runs` (core:287; db:184-296) runs on each old, unlocked `*.run` directory.
  - It resends every `*.envelope` (db:281-283) and turns `session.json` into a session update (`abnormal`, or `crashed` if `last_crash` is newer) (db:243-271).
  - It then **deletes each file regardless of outcome** (`sentry__path_remove(file)` db:286; `sentry__path_remove_all(run_dir)` db:290).
  - So a re-sent envelope gets one more attempt. If that send fails too (or consent is missing → discarded, core:477-481), it is gone.
  - [code] The `<db>/external` directory is pruned of files older than 1 h (db:203-216).
- **[code] `<uuid>.run` contents:**
  - `session.json`: written on session start (sess:224 → db:153-175). Removed on session end (sess:249 → db:177-182).
  - `__sentry-event`: msgpack of the scope (no breadcrumbs). Rewritten on every scope change on Windows (cp:252-284; core/scope flush via scope.h:122-124) and once more at crash time (cp:286-309).
  - `__sentry-breadcrumb1` / `__sentry-breadcrumb2`: msgpack breadcrumbs in alternating ring files (cp:643-679).
  - `<uuid>/<name>`: copies of *byte* attachments for crashpad (cp:756-797).
  - Dumped `*.envelope` files.
  - The three `__sentry-*` files are touched at startup and registered as crashpad attachments (cp:498-514). sentry-native itself never sends them; on the next start they are simply deleted (db:286).
  - With crashpad, `sentry_close` does **not** delete the run folder, because `can_capture_after_shutdown = true` (core:361-365, cp:853). It is cleaned on the next start.
- **[code] No offline-caching option exists in 0.12.5.** `grep cache_keep|offline` over include/sentry.h and src/ finds nothing. Only `sentry_options_set_database_path` mentions "cached" data (h:1470-1500).
  - The default DB path is `.sentry-native` relative to the working directory (opt:21).
  - [inf] `sentry_options_set_cache_keep` belongs to later releases, not 0.12.5.

## 3. Crashpad backend (Windows)

- **[code] What the handler stores and uploads on a crash:**
  - The minidump.
  - Every path in its attachment list. That list is:
    - init-time `options->attachments` file paths (cp:492-496);
    - plus `__sentry-event`, `__sentry-breadcrumb1`, `__sentry-breadcrumb2` (cp:511-514);
    - plus runtime `sentry_attach_file*` paths through `AddAttachment` (cp:799-800, cpexc:198-206).
  - Optional screenshot (cp:516-521, cpexc:144-156).
- **[code] Files are read at crash time, by the handler process:** `file_reader.Open(attachment)` → `CopyFileContent` into the new report (cpexc:124-142).
  - If a file cannot be opened: `"couldn't be opened, skipping"`, and the report continues without it (cpexc:126-130).
  - Upload is a multipart POST: every attachment as a file part plus `upload_file_minidump` (cpup:314-322).
  - The sentry-side handler first writes the scope to `__sentry-event`, the crash marker, and a `crashed` session envelope into the run dir (cp:364-383).
- **[code] `SetUploadsEnabled` is forced on every start.**
  - `crashpad_backend_user_consent_changed` does `SetUploadsEnabled(!sentry__should_skip_upload())` (cp:148-156).
  - `sentry_init` always calls it after the backend starts (core:268-270).
  - Without `require_user_consent`, `should_skip_upload()` is false (core:93-103). So it writes **uploads=enabled** to `settings.dat` on every init.
  - **A hand-edited `settings.dat` is overwritten at the next `sentry_init`.**
  - [inf] An edit made *after* init in the same session would be honored by the running handler until the next init, because the handler reads it per report (cpup:202-213).
- **[code] Database layout on Windows:** `reports/` holds `<uuid>.dmp` (cpdbw:44,49), plus a `metadata` index file (cpdbw:45,941) and `settings.dat` (cpdbw:47).
  - `attachments/<uuid>/` holds the per-report attachments (crash_report_database.cc:32, 494-498).
  - There is no `pending/` or `completed/` folder on Windows. State lives in `metadata`.
- **[code] Retries:**
  - The handler runs with periodic tasks enabled. sentry passes only `"--no-rate-limit"` (cp:540), and `periodic_tasks` defaults to true (cpmain:771, 1091).
  - So the upload thread scans pending reports at start (`Start(0.0)`, cpup:125-127) and every 15 min (`kRetryWorkIntervalSeconds = 15*60`, cpup:58,103).
  - A failed HTTP POST returns `kRetry` (cpup:364-365). It is retried with exponential backoff up to `kRetryAttempts = 5`, then skipped (cpup:64, 259-270).
  - [inf] Pending reports from earlier sessions are uploaded the next time the game starts the handler.
- **[code] Reports found while uploads are disabled are not kept for later.** `SkipReportUpload(..., kUploadsDisabled)` (cpup:204-212). [inf] This marks them completed, so they are never sent, even if uploads are re-enabled.
- **[code] Pruning:**
  - sentry runs `CleanDatabase(2 days)` plus prune by size (8 MB) OR age (2 days) at each init (cp:739-753, core:288-290).
  - The handler also runs a prune thread (cpmain:1175-1180).
- **[code] `last_crash`:**
  - The file `<db>/last_crash` contains an ISO-8601 timestamp text (`sentry__usec_time_to_iso8601`), written by the crash handler (db:298-324, cp:368).
  - Only its existence is used: `g_last_crash = sentry__has_crash_marker(options)` (core:245) → `sentry_get_crashed_last_run()` (core:1628-1632). `sentry_clear_crashed_last_run()` deletes it (core:1634-1644, db:340-355).
  - The session-crash heuristic instead uses crashpad *completed* report creation times (cp:721-737, db:256-269).

## 4. Consent

- **[code]** Sending is skipped only if `require_user_consent && user_consent != GIVEN` (core:98-100). `sentry__capture_envelope` discards in that case (core:477-481).
- **[code] Revoke at runtime without `require_user_consent`:**
  - `set_user_consent` stores the value, calls `user_consent_changed_func`, and writes the file (core:405-432).
  - For crashpad that re-evaluates `!should_skip_upload()`, which is still **true** → `SetUploadsEnabled(true)` (cp:155).
  - **Revoke therefore blocks nothing: neither sentry HTTP sends nor crashpad uploads.**
  - `require_user_consent` is only set via options before init (opt:455-458). The header says the getter reflects the init configuration (h:1729-1737).
- **[code] Persistence:** file `<db>/user-consent`, containing `"1\n"` (given) or `"0\n"` (revoked). It is deleted on reset (core:416-427).
  - It is read **only in `sentry_init`** via `load_user_consent` (core:72-91, 207). The first char `'1'`/`'0'` is parsed; anything else means UNKNOWN.
  - [inf] Writing `0` into that file before launch blocks sending (core events and crashpad, via cp:155 at core:268-270) **only if the game set `require_user_consent`**. Otherwise it has no effect.

## 5. `sentry_close()` at runtime (crashpad, Windows)

- **[code] It does not uninstall the crash handler.** The header says it "does not uninstall any crash handler ... which will still process crashes after `sentry_close()`, except ... `crashpad` on Linux or ... `inproc`" (h:1661-1663).
  - `crashpad_backend_shutdown` only deletes the `CrashpadClient` and DB objects (cp:621-641).
  - `~CrashpadClient() {}` is empty on Windows (cpcli:661). `SetUnhandledExceptionFilter(&UnhandledExceptionHandler)` stays installed (cpcli:744).
  - The first-chance handler pointer stays set to `sentry__crashpad_handler` (cpcli:68,186; cp:586-587). Its `SENTRY_WITH_OPTIONS` body becomes a no-op and it returns `false`, so crashpad still dumps (cp:331-431).
  - [inf] The crashpad_handler process keeps running and keeps uploading per `settings.dat` (left at enabled by the last init).
- **[code] What close does:**
  - Flushes and shuts down logs (core:332-336).
  - Ends the session, which **sends** an `exited` session envelope (core:346, sess:280-290).
  - Shuts down the transport with `shutdown_timeout`, dumps the remaining queue to disk, frees the options, sets `g_options=NULL`, and cleans the scope (core:347-377).
- **[code] Concurrent calls:**
  - Options are refcounted: `SENTRY_WITH_OPTIONS` takes `sentry__options_getref` under the lock (core.h:133-135, core:46-55). A thread already inside an API call keeps the options alive until it finishes.
  - Calls made after `g_options=NULL` are no-ops; `should_skip_upload` returns `skip=true` with no options (core:96-102).
  - [inf] There is a race window. A thread holding a ref can submit an envelope to the already shut-down bgworker; `bgworker_submit` has no running check (sync:422-452), so it is never sent. It can also write breadcrumb or `__sentry-event` files after the crashpad client was deleted; those paths stay valid until the options are freed (cp:681-690).
  - No use-after-free was observed in this code path, but the API is documented as "to be called only once" (core:338-340). `sentry_init` itself calls `sentry_close()` first (core:171).

## 6. Auto session tracking

- **[code]** It is on by default (opt:49). It needs a release: `sentry__session_new` returns NULL if there is no release (sess:60-63).
- **[code] At start:** `sentry_start_session` only **writes** `session.json` to the run dir (sess:214-229). Nothing is sent at start.
- **[code] Session payload:** JSON with `init` (first update only), `sid`, `status`, `did` (user id/email/username), `errors`, `started`, `duration`, `attrs{release, environment}` (sess:98-142, 304-318).
- **[code] When it is sent:**
  - Attached to the first captured event with `init=true` (core:578-592).
  - At the end, on `sentry_close` (`exited`, core:346).
  - On a crash it is written to disk as `crashed` (cp:370-383) and sent at the next start.
  - Unclosed sessions are sent at the next start as `abnormal` or `crashed` (db:243-279).

## 7. Disabling without code changes

- **[code] Environment variables read** (only in `sentry_options_new`, as defaults the app's own setters override): `SENTRY_DSN`, `SENTRY_DEBUG`, `SENTRY_RELEASE`, `SENTRY_ENVIRONMENT` (opt:23-41).
  - `http_proxy` / `https_proxy` are read only if the app set no `options->proxy`, by both WinHTTP (whttp:104-125; must start with `http://`) and the crashpad handler URL (cp:551-556).
  - `TZ` is read only in the AIX branch (utils.c:487-494), not on Windows.
  - [inf] `SENTRY_DSN=""` only helps if the game never calls `sentry_options_set_dsn`.
  - [inf] `https_proxy=http://127.0.0.1:9` in the game's environment would make both transports fail, assuming the game sets no proxy. Result: events dropped; crash reports retried, then skipped, still sitting on disk.
- **[code] Hard failure of `sentry_init`** (no events, no sessions, no crashpad) happens if any of these fail:
  - The DB directory cannot be created or is not writable (core:181-185).
  - The run folder or lock cannot be created (core:201-205).
  - The crashpad handler exe is missing or invalid; default is `crashpad_handler.exe` next to the game exe unless `handler_path` is set (cp:439-475 → core:236-239 `goto fail`).
- **[code] File tricks:**
  - `user-consent` only matters with `require_user_consent` (section 4).
  - `settings.dat` is overwritten at every init (section 3).
  - No other kill-switch file or env var exists in this source.
