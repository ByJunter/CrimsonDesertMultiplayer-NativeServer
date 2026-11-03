#pragma once
// P8 seq_actor_restore (docs/research/pendientes/p8-impl-seq-restore.md), GUEST: [session2]
//   seq_actor_restore = 0 | probe | 1
//
// An NPC the host announces (2005) in the middle of a scene action (seq_bodyanim_action 0x837487D4 /
// seq_bodyanim_action_loop 0x35DD392F) cannot restore it on the guest (the dynamic graph [CC+0x70/+0x78] only
// exists while the scene plays): eErrNoInvalidActionChart, failed login, actor destroyed, and the copied 2661
// of its sequencer is then rejected by 0xA9CE81 (session 8: the arm-wrestling table 0x1B0, Damiane's 0x13).
//
// One mid hook at 0x86490F (mov rbx,[r13+8] ; lea rdx,[rbx+8], 8 bytes, .rsrc) inside
// RestoreActionsFromLoadData 0x8646D0, after both layers' entries are on its stack and before 0x863880. For a
// remote actor (key 0xA.../0xB..., +0x90 not this client's user) whose seq_bodyanim hash no chart has (the
// same lookup 0x863880 makes, replicated read-only), that entry gets the idle wait 0x26107D3C when the layer's
// chart has it, else hash 0; if both end at 0 the call continues at 0x864B1A with result 0 (the native path of
// restore byte 0). probe: the same decision, counted and logged, nothing written. Any other failure is left
// to the game. In the hook: SEH reads, at most two dword writes to the function's own stack entries, no CRT,
// no allocation, no lock, no formatting; lines from the guest's diagnostics thread (every 5 s, summary 60 s).

#include <string>

namespace cdmp::session2 {

void guest_seq_restore_start(const std::string& mode);  // guest_start (guest=1); "0" installs nothing
bool guest_seq_restore_wanted();
void guest_seq_restore_tick() noexcept;  // the diagnostics thread, every 5 s: lines, a summary every 60 s
std::string guest_seq_restore_describe();

}  // namespace cdmp::session2
