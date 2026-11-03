#pragma once
// P8 S1 option A, the GUEST's half (docs/research/pendientes/p8-impl-opcion-a.md): [session2]
// guest_seq_route = 0 | probe | 1 (guest=1). guest_seq_watch = <hex actor keys> (G5, optional).
//
// Four points, right after the handler's ClientField::vf48 returned the manager (RAX):
//   0xC0612A  2661 PrepareSequencerAck (BL = kind; [R12] = the resolved actor, [R12+0x18] the raw packet)
//   0xC07749  2981 StageBranchStateAck ([R15] = the resolved actor, [R15+0x18] the raw packet)
//   0xC0659E  2810 DeleteSequencerAck ([RBP-8] = the actor 0x8A67E0 found, byte [RBP] = found,
//             u32 [RBP+0x78] = the packet's actor key, u64 [RBP-0x58] = the sequencer)
//   0xB87A67  2322 PlayAllPreLoadSequencerAck's write [RAX+0x231] = 2 (R14 = the raw packet; the actor the
//             handler resolved at 0xB879DD: [RBP-0x10] when byte [RBP-8])
// When the packet names a character other than M's own and that character has its own
// ClientSequencerStageManager X ([[[A+0x68]+0xA8]+0x18], vtable checked), RAX := X (gsr::decide).
// Thread and lifetime: the actor is always the one the handler itself just resolved with the game's own
// locked lookup (0x8A67E0 -> 0x8B3C00 on the client actor manager, its lock at +0x10/+0x18), in this same
// handler call on this same thread, and the handler goes on using it; X is read from that actor's
// components. No table walk of ours runs in a routing hook (game::find_actor-like reads only on the diagnostics
// thread, for G5); guest_sched_bind's point (0xC0B626, guest_sched_bind.h) walks the client actor table to COLLECT
// keys and takes every actor again with the game's own locked lookup before calling anything on it. The lookup covers every client actor: in session 7 the 2981 handler resolved the host's
// Kliff 0xA0100001 through it (GL "addressed actor ... (0xA0100001)").
// With the routing ON, another character's 2661 / 2322 that cannot be routed is refused (gsr::refuse):
// the 2661's kind becomes 0 (the handler's native "not 4" path) and the 2322's write goes to a sink of the
// mod. 2981 / 2810 are not refused: in M they only miss an id (the native foreign 2981s do so today).
// The first three are guest_stage_diag's points and the fourth is P8Probe's (load_diag): when that
// module hooks the address, ITS callback calls guest_seq_route_hook first (one hook per address);
// otherwise this module hooks it itself, with the same verified bytes. Callbacks: SEH reads, atomics,
// a bounded lock-free ring; no allocation, no CRT, no game call, no lock.
//
// The delivery gate (guest.cpp deliver_all, the NetworkThread, before OnRecvPacket): a 2661 / 2810 /
// 2322 naming another character is dropped unless the routing of that opcode is ON and hooked (it
// would land in M), and any 2661 / 2810 / 2322 naming a character is dropped while this connection's
// own character (its 2756) is not known yet (none arrives that early: session 7, both logins). Always
// active on the guest, whatever the key: natively no foreign one arrives. The 2981 is NOT gated: the
// host's 2981s reach this client natively (near finder; 129 in session 7, into M, where they only miss
// the id) and a copied one is the same kind of packet; gating it would change that validated path.
//
// Lines 'GuestSeqRoute [...]' and G5 'GuestSeqRoute [G5 ...]' (the watched actors and the X of each
// routed character, on change) every 5 s from the guest's diagnostics thread; a summary every 60 s.
// G5 (guest_seq_play, p8-xtls-lectura.md §2): per actor also comp+0x3D8 / +0x320 / +0x2E0 and actor+0xAC
// (with the parent's +0x3D8); per X also +0x1D0, the live scenes +0x28 and the engage queue +0x190; with
// guest_seq_play on, the X line repeats every 30 s unchanged.

#include <safetyhook.hpp>

#include <cstdint>
#include <span>
#include <string>

#include "mod/session2/guest_seq_route_logic.h"

namespace cdmp::session2 {

// guest_sched_route = 0 | probe | 1 (guest=1; p8-impl-horarios-x.md): four more points of this module's own, for the
// NPC schedule stages (no other module hooks them): 0xC0B5C1 2335 Create, 0xC0BA4E 2126 Delete, 0xC0C761 2958 Change,
// 0xC0CC91 2909 Deactivate, each right after its handler's vf48. The same decision as the sequencers' (another
// character's packet -> its X, with X+0x148 present), and a ledger scene key -> manager so that the delete / changes
// of a key go where its 2335 went. Never gated, never refused: these packets arrive natively (S11: 204/204 2335
// identical H/G). G5 also reads comp+0x318 / +0x3D9 and the bound stage's +0x178 / +0x30 / +0x28 of the watched NPCs,
// and a census of X+0x148 / M+0x148 (stages, active, bound nodes) every 5 s ('GuestSchedRoute [census]', on change).
//
// From guest_start (guest=1). Starts the install thread (it waits for guest_stage_diag and P8Probe
// to install theirs, so that one address never gets two hooks). mode: the raw ini value.
// want_2524 (guest_seq_play probe/1 with guest_seq_play_live=1, p8-impl-escenas-play.md §3): a fifth point,
// 0xC08178 in the 2524 StageOnBranchEventAck handler (its own hook: no other module uses it); routed only
// with guest_seq_play=1 (guest_seq_play_routes_2524), decided only otherwise; never gated (it arrives
// natively). Every ROUTED / WOULD-route packet but the 2524 also feeds guest_seq_play's table of X.
// deny (guest_seq_route_deny, p8-impl-escenas-play.md §15): hex resources of field-reloading cutscenes; a 2661
// with one is refused (created nowhere, as before option A) and its 2981 / 2524 stay native (M, no effect).
// sched_mode: guest_sched_route's raw ini value (the install thread also starts for it alone).
// bind_mode: guest_sched_bind's raw ini value (guest_sched_bind.h; off unless guest_sched_route=1).
// route_2524 / allow_2524 (session 13, p8-impl-secuenciador-bodyanim.md): guest_seq_route_2524 = 0 | probe | 1 and
// guest_seq_route_2524_allow (hex 2661 resources). 0 keeps the 2524 tied to want_2524 as above. probe installs the 2524
// point (and its inner check) by itself, classifies every 2524 by its scene (gsr::classify_2524: WORLD = binds an NPC of
// type 3 present here, its name read from X and not cd_seq_funcnpc_*, not denied, on the allow list when it has one)
// and routes nothing; 1 routes only the WORLD ones (with guest_seq_route=1, an X guest_seq_play ticks, pre_2524 and the
// inner check), whatever guest_seq_play_live says.
void guest_seq_route_start(const std::string& mode, const std::string& watch, bool want_2524, const std::string& deny,
                           const std::string& sched_mode, const std::string& bind_mode, const std::string& route_2524,
                           const std::string& allow_2524);
bool guest_seq_route_wanted();
// One of the four points (its own hook, or guest_stage_diag's / P8Probe's at the same address).
void guest_seq_route_hook(gsr::Point point, safetyhook::Context& ctx) noexcept;
// One of the four schedule points (always this module's own hook).
void guest_sched_route_hook(gsr::SchedPoint point, safetyhook::Context& ctx) noexcept;
// deliver_all, before OnRecvPacket: true = do not deliver this packet. Learns the own character
// from MainPlayerDataStartAck 2756.
bool guest_seq_route_gate(std::span<const uint8_t> packet) noexcept;
// A 2661/2810/2322 payload naming a character other than this client's own (known from its
// MainPlayerDataStartAck): an observer_seq_copy copy (load_trip leaves such a 2322 out of its trips).
bool guest_seq_route_other_character(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
void guest_seq_route_tick() noexcept;  // the diagnostics thread, every 5 s
std::string guest_seq_route_describe();
// This connection's own character (its MainPlayerDataStartAck 2756), 0 while unknown. Atomics only (guest_x_veto).
uint32_t guest_seq_route_own_key() noexcept;
// Session 13 (guest_bodyanim_diag): the G5 watch list (the ini's guest_seq_watch and every routed 2661's NPCs, up to
// 64, append-only), lock-free for any thread; and the client actor table's actor of a key (SEH reads, 0 if absent).
size_t guest_seq_route_watch_count() noexcept;
uint32_t guest_seq_route_watch_key(size_t i) noexcept;
int guest_seq_route_watch_index(uint32_t key) noexcept;  // -1: not watched
uintptr_t guest_seq_route_find_actor(uint32_t key) noexcept;

}  // namespace cdmp::session2
