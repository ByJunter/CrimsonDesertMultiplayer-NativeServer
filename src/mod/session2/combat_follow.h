#pragma once
// P8 session 9, C1: [session2] combat_follow = 0 (default) | probe | 1, HOST only (create_session=1)
// (docs/research/pendientes/p8-s9-combate-montura.md §1.1, §6; the implementation and what to look for in the
// log: docs/research/pendientes/p8-impl-s9-movimiento.md §3; the pure logic: combat_follow_logic.h).
//
// The host client's enemies in combat with user 2 get synthetic MoveActorAck 2697 to user 2 at ~16 Hz, read
// from the host client's memory by a thread of the mod, so the guest's remote_interp sees a high cadence and
// its delay drops from 1.1-1.8 s to ~200-300 ms. probe: candidates, reads and the would-be cadence counted,
// nothing sent.
//   take_frame (the game's threads): combat_follow_on_out, a try-lock and a few fast reads of the game clock;
//     never waits, never allocates, never logs.
//   its own thread: SEH reads of the host client's actors, server_push_extra (the mod's memory and queue only,
//     never the game's heap, nothing of the game called), its minute summary.
//
// P8 session 25, [session2] combat_follow_v2 = 0 (default) | probe | 1, needs combat_follow (the combat stream,
// docs/research/noche/combate-retardo.md §6.1; combat_follow_logic.h): also the enemies fighting any character near
// user 2, at 30 Hz on a high-resolution waitable timer, with the game's own movement thresholds, a 100 ms keepalive
// of the same pose, no cutoff without real samples, the 12 nearest, actors found with fast_read. probe: v1 sends
// as before, v2 is decided beside it and counted ('CombatFollow v2 probe' line).

#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

// session2::install (host, create_session=1): the raw ini values; starts the thread. False: off.
bool combat_follow_start(const std::string& raw, const std::string& raw_v2 = "0");
bool combat_follow_active();
// take_frame, for a packet queued for the LIVE session 2 (route Pass): its opcode and payload.
void combat_follow_on_out(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
// A new guest epoch: the engaged enemies are forgotten.
void combat_follow_on_epoch() noexcept;
std::string combat_follow_describe();  // atomics only ("" when off)

}  // namespace cdmp::session2
