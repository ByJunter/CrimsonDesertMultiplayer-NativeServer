#pragma once
// P8 session 15, GUEST: [session2] guest_reload_wait_fix = 0 (default) | probe | 1
// (docs/research/pendientes/p8-s15-viaje-tunel.md §8; the pure logic: guest_reload_wait_logic.h).
//
// Needs guest_actor_diag=1: it runs inside that module's checked mid hook at 0x7798D0 (the field-change state
// machine 0x7797F0 reads [P+0x15C], RDI = P, on the game's own thread), every pass, before the state handler.
//
//   probe and 1:
//     - the load-machine trace: one line per change of [P+0x15C] inside a trip (and one every 5 s while a state
//       lasts), with P+0x160..0x163, P+0x290/0x291, the wait list's count, the guest character's +0xC2/+0xCF/
//       +0xC3 and its own stage manager's +0x231 (bounded);
//     - when a trip begins (the client's 2283 or [P+0x15C] leaving 0x18, whichever comes first): the wait list's
//       keys with their actor, +0x5E and readiness (the "not ready before the trip" set);
//     - in 0x12: the front key (the one 0x12 waits for) with its +0x5E/+0xC2/+0xCF, controller, parent +0xAC and
//       children +0xB0 (each looked up), how long it has been not ready (guest_actor_diag's 5 s walk) and what
//       mode 1 does with it (grw::judge), at +2/+10/+30/+60 s and every 60 s.
//   1: also removes that front key from the wait list exactly as 0x12 removes a ready one (0x779EF7..0x779F0B)
//      under the lock 0x12 takes ([M+0x10] vf1/vf2), when grw::judge says Drop: a reload in the same field
//      (2643 and no 2174 in this trip), a world actor 0xB... controlled by another user, waiting for its login
//      (+0x5E 8), not ready, the same actor that was already not ready in the wait list when the trip began,
//      after 2 s in 0x12, at most 128 per trip (a warning past 16). Never the guest's character, never a key that
//      became not ready during the trip.
// Every read is SEH-guarded; the code of 0x12, the removal block, vf1D8 0x4FE240 and Find 0x464290 are checked
// against their bytes before anything is written. Lines are formatted on the guest's diagnostics thread.

#include <cstdint>
#include <string>

namespace cdmp::session2 {

// guest_start (worker thread), before guest_actor_diag_start. False: off (0, unknown value or no diag).
bool guest_reload_wait_configure(const std::string& raw, bool actor_diag);
bool guest_reload_wait_active();  // atomics only

// guest_actor_diag's hook at 0x7798D0 (game thread), every pass of the P it follows: [P+0x15C] now and the
// value seen at the previous pass (-1 = first).
void guest_reload_wait_on_pass(uintptr_t p, uint8_t state, int previous) noexcept;
// guest_deliver (the mod's network thread): a host packet for the client (2174 / 2643 / 3066 are noted).
void guest_reload_wait_on_in(uint16_t opcode) noexcept;
// guest_send (the client NetworkThread): a client Req sent to the host (its 2283 begins a trip if none runs).
void guest_reload_wait_on_out(uint16_t opcode) noexcept;
// guest_actor_diag's 5 s walk of the wait list (diagnostics thread): each key found, ready or not.
void guest_reload_wait_note_entry(uint32_t key, bool ready) noexcept;
// The guest's diagnostics thread, every 5 s: the lines.
void guest_reload_wait_tick() noexcept;
std::string guest_reload_wait_describe();  // atomics only

}  // namespace cdmp::session2
