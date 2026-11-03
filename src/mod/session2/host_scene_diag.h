#pragma once
// P8 S1, NPC scenes: the HOST's measurement probes H1-H3
// (docs/research/pendientes/p8-s1-escenas-npc.md §5.1 and "Implementado para la
// medida"). [session2] s1_scene_diag=1 (default 0), needs create_session=1;
// READ ONLY: nothing is written to the game, no game function is called,
// nothing is sent or held.
//
//   H1  0x2A78D0D  ServerStartActionLogic's flag check `cmp byte [r15+0x281],0`
//                  (rbx = the actor, r15 = block B, r12 -> the two action
//                  layers, r13 = the session): per NPC key and per hash, how
//                  many StartActionReq the server applies WITHOUT relaying (flag
//                  set: 0x232FF90 + return) and how many go on, with user 2
//                  logged in or not.
//      0x2A79060  the same function after ServerStartActionOnServer: [rbp+0xF0]
//                  == 0 -> 0x2A7907D calls the broadcaster 0x2A797C0; else kept.
//   H2  on the host's own client (who binds a scene NPC, and when):
//      0x1EB8946 + 0x206DE6B  the NPC's AI block read from its 2005 (resource,
//                  node, scene key, the handle 0x1869D70 gave) = G1 on the host
//      0x9ED401    vf13's local_res8 (the first binding attempt)
//      0x20853D0   entry: the AI's scene handle re-set (return address: the
//                  AI's vf107 0x91CDDF or the 2874 handler 0xC0D23A)
//      0x9F2FA0    entry, and inside it 0x9F30CB (local_res8) and 0x9F312D (the
//                  instance found in X+0x148: the binding 0x43B4B0 follows)
//   H3  no hook of its own: take_frame (what is queued to session 2: 2174, NPC
//      2005, 2144, 2322), the injection (the guest's 2549), observer_stage's
//      entry hook 0x2BC84E2 and the host client's S>C capture (the sniffer's
//      writer thread: 2335/2126/2411/2661/2810/2874/2174 to session 1 = the
//      live scene ledger of the host's field; and its C>S 2223 flag byte).
//
// Hooks: installed by observer_stage's thread (its code checks first; this key
// installs observer_stage in measure mode when observer_stage_sync=0), each
// patch inside .rsrc, whole instructions, no branch into it, its bytes and the
// code its register reading relies on compared with build 1.0.0.2976
// (p8_install_mid). Callbacks: SEH-guarded reads into a bounded lock-free ring
// and lock-free count tables; no allocation, no CRT, no game call. Lines
// ("SceneDiag [...]") are formatted by observer_stage's thread once a second;
// never on a game thread, never on the network thread.

#include <cstdint>
#include <span>
#include <string>

#include "mod/session2/guest_actor_diag_logic.h"

namespace cdmp::session2 {

// session2::install (create_session=1), before observer_stage_install.
void host_scene_diag_configure(bool on);
bool host_scene_diag_wanted();
// s1_scene_diag itself: observer_stage installs SceneDiag (with H1b). Without it user2_stages installs H1b alone
// for the S33 probes (npc_action_probe), and observer_stage is not forced on for them.
bool host_scene_diag_configured();
// observer_stage's install thread, after its own hooks.
void host_scene_diag_install(uintptr_t base, std::span<const gad::Section> secs);
// observer_stage's thread: once a second (lines, ledger, trips, the 60 s summary).
void host_scene_diag_drain();
std::string host_scene_diag_describe();  // atomics only; empty when off

// Taps (fixed records only; at once when off).
// take_frame, after the push (a game thread): a packet queued to session `self`.
void host_scene_diag_on_out(uint16_t opcode, const uint8_t* packet, size_t size, uintptr_t self, bool pushed) noexcept;
// server_inject, after the push (the mod's network thread): a Req of the guest.
void host_scene_diag_on_in(uint16_t opcode, std::span<const uint8_t> packet) noexcept;
// observer_stage's entry hook 0x2BC84E2 (the server's logic thread).
void host_scene_diag_on_entry(uintptr_t field, uintptr_t actor) noexcept;
// The sniffer's writer thread: one whole packet of the host's own client.
void host_scene_diag_sniffed(const uint8_t* packet, size_t size, uint64_t t_us, bool server_to_client) noexcept;

}  // namespace cdmp::session2
