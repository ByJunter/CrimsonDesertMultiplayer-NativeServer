#pragma once
// P8 session 29, the other player's tree bend (docs/research/s25/gancho-cuerda.md section 14; the pure logic and the
// wire format: tree_bend_logic.h). Build 1.0.0.2976.
//
// [session2] tree_bend = 0 (default) | probe | 1, on BOTH PCs (symmetric: each client is the THROWER of its own
// character and the OBSERVER of the other player's):
//   M1  mid hook at the entry of RemoteCatchFunctions vf2 0x117F7E0 (5 B): who runs it, slot, f6, thread (log only);
//   M2  mid hook at the entry of the bend's apply 0x1190FD0 (7 B): r8b counts; the thread's context for M3;
//   M3  mid hook at the entry of TreeComponent::SetBend 0x349EA10 (5 B): the thrower's amount / direction / tree, sent
//       (probe and 1) as tunnel control state 15 (<= 20 Hz per thrower, End after 150 ms without SetBend);
//   M4  inline hook at the entry of the RemoteCatch tick 0x59C0D0 (5 B): for a REMOTE player character with a live
//       sample, after the original: slot 0 checked (client component, attached, the same scene id) and logged as
//       "would apply" (probe) or applied (1: FUN_1405b1600 -> FUN_140491ac0 -> SetBend, the natives 0x1190FD0 uses).
// Lines 'TreeBend ...' and a 'TreeBend summary' every 60 s.
//
// Threads: the hooks run on the game's threads (guarded reads, relaxed atomics, a bounded lock-free ring, a try-lock
// on the receiver's table); the module's thread installs once the world is up (every byte checked, REFUSED and nothing
// written otherwise), drains the ring every 10 ms, runs the sender and logs; the network threads move the frames.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cdmp::session2 {

// mod.cpp's worker, both roles. Nothing with "0".
void tree_bend_start(const std::string& raw, bool guest);
bool tree_bend_active();  // probe or 1

// A tunnel frame from the other PC (host: tunnel_on_packet, inside the bound conversation; guest: guest_deliver, only
// while playing on the host). True when it was ours (taken out of the stream). `epoch`: its connection.
bool tree_bend_on_frame(std::span<const uint8_t> frame, uint64_t epoch) noexcept;

// This client's own bend frames, oldest first (the network threads). False: none.
bool tree_bend_pop_outbound(std::vector<uint8_t>& frame) noexcept;
// No conversation to send them on: dropped (counted).
void tree_bend_discard_outbound() noexcept;
// A popped frame the queue to the host refused (guest): counted in the summary (only the newest bend matters, the next
// Sample or End replaces it).
void tree_bend_note_unsent() noexcept;

// S29 (gancho-cuerda.md section 14.7): rope_probe's entry hook on 0x8A30C0 (its only hook there; never hooked twice)
// tells tree_bend about every PLAYER character's call: its control byte, whether the tree-pull timer ran and whether
// the inline SetBend copy wrote (+0x638 >= 0, rope_probe's "applied"). For a REMOTE player that is a fight (it bends a
// tree natively here) and it is counted per key. Game threads, atomics only.
void tree_bend_note_pull(uint32_t key, uint8_t ctl, bool running, bool inline_bend) noexcept;

}  // namespace cdmp::session2
