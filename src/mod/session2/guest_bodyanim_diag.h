#pragma once
// P8 session 13, guest_seq_bodyanim_diag (docs/research/pendientes/p8-impl-secuenciador-bodyanim.md), GUEST: [session2]
//   guest_seq_bodyanim_diag = 0 | 1   READ-ONLY (nothing written to the game, no game function called).
// Needs guest_seq_route = probe | 1 (its G5 watch list: guest_seq_watch and every routed 2661's NPCs).
//
// Family (d) of p8-s12-npc-familias.md §5: the host's sequencer NPCs stand in the idle wait on the guest because their
// SeqBodyAnim graph (CC+0x70/+0x78) is never built there. This measures, for the watched NPCs only:
//   G5 (every 5 s, the diagnostics thread, on change; every 60 s anyway): CC = [[actor+0x68]+0x40], [CC+0x70],
//      [CC+0x78] (and their +0x15), u8 CC+0x23A, u8 CC+0xA8, per layer [CC+0x88/+0x90]: chart +0x110, hash +0x3C and
//      the current action's hash [[layer+0x10]+0x18]; with the hooks' counts for that actor. 'GuestBodyAnim [G5 body]'.
//   0xA79770 BodyAnim entry (mid hook, 7 bytes `mov rax,rsp ; mov [rax+0x18],rbx`, .rsrc): every call counted (all,
//      NPC keys, watched); for a watched actor one line: body, CC, CC+0x70/+0x78 before, desc, the clip name and hash
//      ([[desc+0x18]+0x18] -> {char*, len, hash}), desc floats +0x28/+0x2C/+0x30/+0x3C, p3/p4. Not installed when
//      s1_diag=1 (P8Probe owns that address: its 'BodyAnim 0xA79770' lines cover it).
//   0x2432F60 entry (mid hook, 5 bytes `mov [rsp+0x20],rbx`, .rsrc): counted by caller ([rsp] = 0xA79C24 / 0xA79FCA:
//      BodyAnim's layer 0 / 1, else another caller, not read); for a watched actor (rsi = body) one line: the graph,
//      both name refs (text + hash), xmm3 and p5..p8 (floats), the resource object [p9].
// Hooks: SEH reads, atomics, a bounded lock-free ring; no allocation, CRT, lock or formatting. Lines from the
// diagnostics thread; a summary every 60 s ('GuestBodyAnim summary': hook counts and the cohort by state).

#include <string>

namespace cdmp::session2 {

// From guest_start. enabled: guest_seq_bodyanim_diag; route_mode: guest_seq_route's raw value (needs probe | 1);
// s1_diag: P8Probe hooks 0xA79770 itself (then only 0x2432F60 here).
void guest_bodyanim_diag_start(bool enabled, const std::string& route_mode, bool s1_diag);
void guest_bodyanim_diag_tick() noexcept;  // the diagnostics thread, every 5 s
std::string guest_bodyanim_diag_describe();

}  // namespace cdmp::session2
