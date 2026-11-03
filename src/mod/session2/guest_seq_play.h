#pragma once
// P8 guest_seq_play (docs/research/pendientes/p8-impl-escenas-play.md), GUEST: [session2]
//   guest_seq_play      = 0 | probe | 1   tick of the host character's manager X + its 2 -> 3 step
//   guest_seq_play_live = 0 | 1           also 0xAA3E70(X, dt) (live scenes) and the 2524 routed to X.
//                                         After the live scenes 0xAA3E70 works on the LOCAL player (0x8B4570 ->
//                                         0xA96070 / 0xA9A1F0 / 0xA9A410) and always ends in 0xAA5040 (may write
//                                         1.0f at [[World+0xA8]+0x208]) and 0xAA5400 (_chapterTransition, 0x63F200)
//                                         (p8-impl-escenas-play.md §13). So for X: a second mid hook at 0xAA4B27
//                                         resumes OUR call at 0xAA4CB2 (over all of that; thread_local flag + r13 =
//                                         X), and a read-only gate (X+0x2A4/+0x288/+0x2A8/+0x2D4 == 0, +0x2CC <= 0)
//                                         holds the call back otherwise; no skip hook, no live call (§13.3).
// Needs guest_seq_route=1 (it fills the table of X; without it nothing is ever ticked).
//
// One mid hook at 0x8B6144 (movzx r8d, byte [r8+0x50], 5 bytes, .rsrc), inside 0x8B6060 (the main loop's
// 0xAD277F, once per frame), right before the native 0x8B6150 call 0xA953C0(M, Q, mode): RAX = M, R8 =
// [rdi+0x80] (mode = byte [R8+0x50]), Q = [RBP-0x41] (the field, held by the native FieldAttacher of
// 0x8B60C6), XMM6 = the frame's dt (0x8B608F). For each X of gsp::XTable (gsp::plan, every frame):
//   slot:     routed on this connection and in this frame's field Q (else the slot is cleared);
//   identity: [X] = pa::ClientSequencerStageManager, [X+0x18]'s key = the routed character, that actor's own
//             manager = X, the client actor table still gives that actor for the key, X != M, [X+8] != 0 and
//             [[X+8]] = [[M+8]] (SEH reads);
//   frame:    mode != 2, M+0x231 == 3, Q a ClientField whose vf40 is 0x9A5B70 and (ON) vf40(Q) != 0;
//   then:     X+0x231 = 3 once per routed 2322 (X at 2, +0x174 0, something in +0x1B0/+0x1C0),
//             0xA953C0(X, Q, mode), and with _live 0xAA3E70(X, dt) when X is at 3 and byte [0x6CC0218] == 0.
// The calls run on the game's main thread, at a point where the game makes the same call for M, under SEH
// filters that switch the module off, write one line and pass the exception on (P8 review I1: the game's
// code may hold a lock; never swallowed). No allocation, no CRT, no lock of the mod, no formatting in the
// hook: counters and a bounded lock-free ring; lines from the guest's diagnostics thread every 5 s.

#include <cstdint>
#include <string>

namespace cdmp::session2 {

// From guest_start (guest=1). mode, route_mode: the raw ini values of guest_seq_play and guest_seq_route.
// guest_seq_play=1 is refused (off, one ERR line) unless guest_seq_route=1.
void guest_seq_play_start(const std::string& mode, bool live, const std::string& route_mode);
bool guest_seq_play_wanted();
// The thread of the frame hook 0x8B6144 (the game's main thread: it ticks M and, with guest_seq_play, X), set on its
// first call; 0 until then. Any thread (S45 R-A writes only from P1 on this thread).
uint32_t guest_seq_play_main_tid() noexcept;
// guest_seq_play is ON (not probe) with _live = 1: guest_seq_route really routes the 2524 (probe otherwise).
bool guest_seq_play_routes_2524();
// guest_seq_play is ON (not probe) and not switched off after a fault: it ticks the X it knows (the 2524 under
// guest_seq_route_2524=1 is routed only then).
bool guest_seq_play_ticks();
// guest_seq_route's hooks: a packet was ROUTED (Verdict::Route only) into X of character `key`, handled in
// `field` (the ClientField the handler's vf48 ran on) on this connection; milestone = a 2322 (re-arms the 3).
void guest_seq_play_note_x(uintptr_t x, uint32_t key, uintptr_t field, bool milestone) noexcept;
// X is the current X of `key` in the table on this connection (the 2524 is routed only to such an X).
bool guest_seq_play_knows_x(uintptr_t x, uint32_t key) noexcept;
void guest_seq_play_tick() noexcept;  // the diagnostics thread, every 5 s: lines, a summary every 60 s
std::string guest_seq_play_describe();

}  // namespace cdmp::session2
