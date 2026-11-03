#pragma once
// Option D, piece P6 (docs/OPCION-D-EMULADOR.md §4.6): while [session2] is
// on, nothing may change the host's saves. Inline hooks at the ENTRY of the
// KernelBase file functions (the game's imports and the CRT both end there)
// fail every write, delete, move, copy or attribute change under
// %LOCALAPPDATA%\Pearl Abyss\CD\save with ERROR_ACCESS_DENIED. Reads pass:
// the server loads the host's slot for user 2 in the first experiments.
// The policy is common/save_guard.h (unit-tested).
//
// save_guard=trace (session 24, lab only): the same hooks, plus
// FindFirstFile(Ex)W and GetFileAttributes(Ex)W, refuse nothing and log
// every operation under the folder after the original call
// ("Session2 save trace #N ..."), to design the redirected save from the
// game's real sequence. The saves are NOT protected in that mode.
//
// save_guard=redirect (session 24): at install the whole save folder is
// copied to %LOCALAPPDATA%\Pearl Abyss\CD\cdmp_save_sandbox (the previous
// sandbox becomes cdmp_save_sandbox_prev), then the same hooks plus the
// queries rewrite every path under the real folder to the sandbox and call
// the original with it (common/save_guard.h, Redirector): the game saves,
// loads and lists in the sandbox and the real folder is never written. If
// the copy fails the install falls back to block.

#include <cstdint>
#include <string>

#include "common/save_guard.h"

namespace cdmp::session2 {

// Installs the hooks in `mode` (Block: the guard refuses; Trace: it logs;
// Redirect: everything goes to the sandbox, or Block if it cannot be made).
// False if the save folder cannot be found, a required hook could not be
// created (then nothing is installed, and the caller must not go on with
// option D), or the mode is Off.
bool save_hooks_install(saveguard::Mode mode = saveguard::Mode::Block);

// CDMP_ROLE=guest (mod/role_logic.h, docs/research/dos-instancias.md), from the worker's start, in every guest-role
// process: the same hooks in redirect mode, but every path under %LOCALAPPDATA%\Pearl Abyss\CD\save goes to
// ...\CD\save_cdmp_guest (same sub-folders), a PERSISTENT folder: no copy of the real folder, no rotation, no
// host_save. Created empty (and said loudly) if missing; refused if it is a link. False = not redirected (logged):
// the caller must not let the game run on. After it, save_hooks_install refuses (a guest never runs the host's
// stages that write).
bool save_hooks_install_guest_role();
bool save_hooks_guest_role();
bool save_hooks_active();       // Block mode on: writes are refused
bool save_hooks_tracing();      // Trace mode on: nothing refused, operations logged
bool save_hooks_redirecting();  // Redirect mode on: everything goes to the sandbox

// What was refused since install (for the log).
uint64_t save_hooks_blocked();
uint64_t save_hooks_reads();
// Trace mode: "N lines logged (cap C), M not logged".
std::string save_hooks_trace_describe();
// Redirect mode: "to '<sandbox>', writes W, reads R, queries Q" (operations
// that had a path under the real folder).
std::string save_hooks_redirect_describe();

// P8 session 25 (guest_save_host.h), redirect mode only.
// The sandbox root as the hooks write it (empty unless redirecting).
std::wstring save_hooks_sandbox_dir();
// Calls failed because a path under the folder could not be rewritten (the
// line's "FAILED n"; 0 = healthy).
uint64_t save_hooks_redirect_failed();
// Successful reads (CreateFile*, read access) of `<folder>\lobby.save` and
// `<folder>\save.save` the game made through the redirect, `folder` given as
// its sandbox path (any spelling). One folder per run: false if one is set.
bool save_hooks_watch_folder(const std::wstring& sandbox_folder);
uint64_t save_hooks_watch_reads(bool save_file);

}  // namespace cdmp::session2
