#pragma once
// Diagnostic for the horse-copy crash (docs/INFORME-CABALLO-CRASH.md and the
// outside review of it). Read-only mid-hooks at the entry of:
//   0x85FBA0  character-control (CC) update, every frame per character
//   0x8646D0  restores the actions from the load data after the character's
//             resources loaded; does nothing if byte [[CC+0x40]+0x30] == 0
//   0x2329D70 processes the pending actions (CC+0xA8 flag, CC+0x80 list)
// For the watched keys only (the copy 0xB010F002 and the sender's horse), it
// logs every change of the CC's action state, straight to the log file so the
// last lines survive a crash. Containment (optional): the copy's CC update
// (0x85FBA0 and the following 0x860D30) is skipped while its main layer has a
// chart but no current action. Build 1.0.0.2976.

#include <cstdint>
#include <string>

namespace cdmp::net {

// Call once (after the world exists). `contain` enables the skip.
bool horse_diag_install(bool contain);
// The sender's horse key, to watch it next to the copy (any thread).
void horse_diag_watch(uint32_t key);
// Writer thread, regularly: summary lines.
void horse_diag_tick();
// Option D, E3b: one more key contained like a copy (user 2's character on
// the host's client: C1b crashed in the movement update 0x88F832, which only
// 0x860D30 calls). 0 = none.
void horse_diag_also_key(uint32_t key);
// Option D: contain also every actor owned by this user key (actor +0x90). 0 = off.
void horse_diag_owner(uint32_t user_key);
// Counters, for logs that run without the co-op (option D).
std::string horse_diag_describe();

// P8 guest, [session2] guest_remote_update_guard (S34, Shadow's crash at
// exe+0x88F832): ONLY the containment's two hooks (0x85FBA0 and 0x860D30; not
// the restore/pending diagnostics, whose bytes the guest's own modules hook),
// for every actor controlled by a user other than the local one (+0x90 non-zero
// and != the local key, gad::contain_remote_update) while a layer has a chart
// and no action. The copies' diagnostics do not run in this mode. Call once,
// after the world exists (any thread but the game's); false = not installed.
// If the co-op's install already ran, its hooks stay and this returns false.
bool horse_diag_install_remote();
// The local user key (0 = not known yet: nothing is contained). Any thread.
void horse_diag_contain_remote(uint32_t local_user_key);
// Logs the first 32 contained keys once each, with their controller. One
// caller thread (never the game's): the guest's install thread, every second.
void horse_diag_remote_report();
// Atomics only: " | remote update guard ..." ("" when not installed).
std::string horse_diag_remote_describe();

}  // namespace cdmp::net
