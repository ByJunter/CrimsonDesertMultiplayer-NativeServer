#pragma once
// The game reports its internal failures through one function (exe+0x35D8D0,
// ~2900 callers) with a printf-style message (often Korean, UTF-8), an error
// code and a severity, but writes them nowhere. A read-only mid-hook copies
// them so cdmp.log shows why the game rejected something we injected
// (caballo-coop.md §8.3). Build 1.0.0.2976.

#include <cstddef>
#include <cstdint>

namespace cdmp::game {

// Call once after the game is up. Safe to call more than once.
bool game_errors_install();
// Writer thread, regularly: logs what was collected (deduplicated, capped).
void game_errors_drain();
// P8 session 8 pets (pendientes/p8-s8-mascotas.md §1.1): how many times CreateOwnedActor's failure
// "Actor 생성에 실패했습니다" was reported from exe+0x8BDF7C (counted in the hook, never capped), and
// the last value of it that looks like (owner user 0x9... << 32 | actor 0xB...), 0 if none.
uint64_t game_errors_actor_create_failures(uint64_t* last_value = nullptr);
// P8 session 9 (session2 child_reannounce): the failures' values from `cursor` on, in order (0 for one whose
// value had no owner/actor form), at most `max`; the cursor moves past them. Only the last 15 are read: older
// ones are skipped and counted in *lost. Lock-free (the hook writes, one reader).
size_t game_errors_actor_create_failures_since(uint64_t& cursor, uint64_t* values, size_t max,
                                               uint64_t* lost = nullptr);
// The hook is in (game_errors_install succeeded; it needs the sniffer, mod.cpp late_init).
bool game_errors_installed();

}  // namespace cdmp::game
