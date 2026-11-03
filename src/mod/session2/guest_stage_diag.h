#pragma once
// P8 phase 2, the GUEST's side of "user 2 as an observer of the field's
// stages/sequencers" (docs/research/pendientes/p8-observador-escenas.md, Fase
// 2). READ-ONLY probe, [session2] guest_stage_diag=1 (default 0), on the guest's
// PC only (guest=1). It answers, from the client that receives them, where every
// stage/sequencer Ack lands and why the NPCs' sequencer binding does or does
// not happen:
//
//   0xC05BB0  2661 PrepareSequencerAck handler entry: the raw packet (kind,
//             addressed actor, owner, sequencer id, bindings and their types,
//             FNV-1a of the payload) and the corr= token the host prints
//   0xC0612A  after ClientField::vf48 (RAX = the manager it routes to): that
//             manager's character (+0x18), u8 +0x231, loading tasks (+0x174), the
//             field (+0x180 owner), the addressed actor and ITS own manager
//             [[[A+0x68]+0xA8]+0x18], and the verdict (gsd::classify_route)
//   0xC061C1  0xA9CA40's result ([RBP-0x50]): created / missing bound actor /
//             duplicate id, and the manager's instance count after
//   0xA9CE87  inside 0xA9CA40: which bound actor was missing (R14 binding)
//   0xC073A0 / 0xC07749   2981 StageBranchStateAck entry / routing (RAX = manager)
//   0xC06330 / 0xC0659E   2810 DeleteSequencerAck entry / routing
//   0xB87A32  2322 PlayAllPreLoadSequencerAck: the field context it uses (the
//             write itself, 0xB87A67, is load_diag's P8Probe point)
//   0x9ED290  ClientSequencerPlayDataActorComponent::vf13 entry (the NPC side):
//             comp+0x2D8 (sequencer owner actor), comp+0x2E0, the NPC's key
//   0x9ED401  its local_res8 check (the NPC's stage node; 0 or hi -1 = exit)
//   0x9ED453  the owner actor found: its manager X and whether X is the manager
//             the stage handlers route to
//   0x9ED48C  the instance found in X (the binding happens)
//   0x77AA8B / 0x77B0CE   the loading state machine writes M+0x231 = 0 / 1
//
// Every point: patch inside .rsrc [0x1000, 0x51EB000), whole instructions, no
// branch into it, no RIP-relative operand, its bytes and the code its register
// reading relies on compared with build 1.0.0.2976 first; then p8_install_mid
// (relocated bytes exactly the verified ones, else REFUSED). None of them is a
// P8Probe point or inside the bytes P8Probe verifies. Callbacks: SEH-guarded
// loads into a bounded lock-free ring, per-point and per-NPC limits, no
// allocation, no CRT, no game call. Lines ("GuestStage ...") are formatted by
// guest_stage_diag_tick(), on the guest's diagnostics thread
// (guest_diag_worker.h), never on the network thread.

#include <cstdint>
#include <string>

namespace cdmp::session2 {

// From guest_start (guest=1). Starts a one-shot install thread (waits for the
// game's code like the P8 probes). No-op when `on` is false.
void guest_stage_diag_start(bool on);
bool guest_stage_diag_wanted();
// P8 S1 option A (guest_seq_route.h): the install thread installed (or refused) every point, and whether
// one of them is hooked at `rva` (false until settled). With guest_seq_route on, the three routing
// points (0xC0612A, 0xC07749, 0xC0659E) call guest_seq_route_hook first: RAX may change there.
bool guest_stage_diag_settled();
bool guest_stage_diag_point_hooked(uintptr_t rva);
// The guest's diagnostics thread, every 5 s: drains the ring and writes the lines.
void guest_stage_diag_tick() noexcept;
std::string guest_stage_diag_describe();  // atomics only; empty when off

}  // namespace cdmp::session2
