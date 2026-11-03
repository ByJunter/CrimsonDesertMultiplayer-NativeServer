#pragma once
// P8 session 13, C1 (docs/research/pendientes/p8-impl-tenderos-c1.md, p8-s12-tenderos.md §5.1), GUEST only:
// [session2] guest_x_interaction_stage = 0 | probe | 1 (default 0). Needs guest_seq_route=1 (without it no X exists).
//
// The interaction prompt's check of a dynamic entry, 0x2126920(entry, J), looks the entry's stage id [entry+0x20] up
// in the local player's own stage manager M (ClientField::vf48, 0x2126A1C) with 0x1F6C190 (0x2126A6A). The entries
// the host character's manager X added to the shopkeepers (SetInteraction, allowed by guest_x_ui_veto_allow) name a
// stage of X: refused, no prompt, no 2671 (session 12).
//
// Three mid hooks inside 0x2126920 (isl::kPointA / R / F, bytes verified, .rsrc):
//   A 0x2126A1F  RAX = M. When [rsp+0x48] is a ClientField, this connection plays on the host, and the id is not a
//                live stage of M (read-only replica of 0x1F6C190, isl::find_stage) but is one of a known X (the X
//                guest_x_veto noted: a routed 2981, a handler run with G = X; checked like guest_seq_play's X:
//                vtable, not M, not the own character, its character's manager is X): '1' RAX := X (the next two
//                instructions copy it to rdi and [rbp+0x8E8]: the lookup, its lock [X+8] and the dead check
//                0x1F6FC40 all use X); 'probe' decides and counts only.
//   R 0x2126AAB  read only: the native lookup's result (RSI) and the manager it used ([rbp+0x8E8]), for a call A
//                followed (thread_local, same entry and rsp).
//   F 0x2126D11  read only: the function's result BL (0 refused, 1 / 2 accepted) for that call.
// Thread: whichever runs 0x2126920 (the client's interaction UI: the main thread). Hooks: SEH reads, atomics, a
// bounded lock-free ring; no allocation, no lock, no log I/O, no game call. Lines 'GuestXInter [...]' (per stage id:
// the first 3 of each verdict; rate-limited) from the diagnostics thread every 5 s, a summary every 60 s.

#include <cstddef>
#include <cstdint>
#include <string>

namespace cdmp::session2 {

// From guest_start. mode = guest_x_interaction_stage's raw value; route_mode = guest_seq_route's (refused unless 1).
void guest_x_interaction_start(const std::string& mode, const std::string& route_mode);
void guest_x_interaction_tick() noexcept;  // the diagnostics thread, every 5 s
// guest_x_veto: an X (another character's ClientSequencerStageManager) this client just used, with the character
// key it belongs to (0 if unknown). Any game thread; atomics only; a no-op while the module is off.
void guest_x_interaction_note_x(uintptr_t x, uint32_t key) noexcept;
// S46 phase 2 (shop_trade_probe's R0 on the guest): the X managers noted in this conversation (at most cap; 0 while the
// module is off). Any thread; atomics only.
size_t guest_x_interaction_known_x(uintptr_t* xs, size_t cap) noexcept;

}  // namespace cdmp::session2
