#pragma once
// P8 session 25, the guest's own save, PHASE B, the host's side of the tunnel (docs/research/noche/progreso-invitado.md
// §3.2, §3.5-§3.8, "Implementado (fase B)"). The wire and the pure state machines: guest_save_wire.h.
//
//   upload    the guest's SAVE_OFFER + SAVE_CHUNKs (states 6, 7) are reassembled on the network thread, in the
//             tunnel's order (so before the guest's 2490 and 3093 that follow them), checked (SHA-256, the game's
//             header check) and written into guest_save_incoming_dir(); guest_save_incoming_ready() stages them.
//             SAVE_ACK (state 8) answers. A mailbox generation newer than the guest's base_gen is staged instead.
//   download  each export of user 2's slot (guest_save_host's result handler, server thread: only queued) goes to the
//             guest as SAVE_RESULT + SAVE_CHUNKs (states 9, 7), one chunk at a time while the tunnel's backlog is
//             under 256 KB; the guest's SAVE_RESULT_ACK (11) marks it delivered (<mailbox>\delivered.txt). Only the
//             newest undelivered generation is kept; a new connection sends it again from the start.
//   status    SAVE_STATUS (10) every 5 s and on every change, to a guest that announced kCapsGuestSave.
//   policy    guest_save_req_policy: §3.7 (2910 for slot 100-108 and 3037 allowed in own-save mode, a 2910 for slot 3
//             or malformed and 2711 / 2178 never).
//   final     guest_save_final_save_wanted: a lost link with the own save active gets one SaveToFileCurrentPlayerReq 2910
//             (gsw::make_final_save_req: 3037 never saves now, guest_save_wire.h) before
//             user 2's native 2001 (server.cpp hand_logout, gsw::FinalSave).
// Network thread only, except where noted.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "mod/session2/guest_save_wire.h"

namespace cdmp::troc {
class TrocLink;
}

namespace cdmp::session2 {

// Both roles, from the network loop (mod.cpp), any thread: this end's SteamID64 and the other end's (0 = unknown /
// UDP). The host checks an offer's SteamID64 against the peer; the guest names its progress folder after the host.
void guest_save_set_net_ids(uint64_t own, uint64_t peer);
uint64_t guest_save_own_net_id();
uint64_t guest_save_peer_net_id();

// After guest_save_install (late-init thread): the result handler and the line. Nothing when the mode is off.
void guest_save_tunnel_install();
// A save control frame from the guest (states 6, 7, 8, 11; also a stray 9 / 10, dropped): consumed. False = not one
// of these (it goes on as before). Before the C4 admission: an upload never binds a conversation.
bool guest_save_tunnel_on_frame(const std::vector<uint8_t>& packet, uint32_t now_ms);
// Every tunnel_tick, before the link is flushed.
void guest_save_tunnel_tick(troc::TrocLink& link, uint32_t now_ms);
// §3.7 for one guest Req (any thread; atomics only).
gsw::ReqPolicy guest_save_req_policy(uint16_t opcode, std::span<const uint8_t> payload);
// A guest Req the policy let through (a 2910 named gsw::kFinalSaveName: the next export is the guest's final save).
void guest_save_tunnel_note_req(uint16_t opcode, std::span<const uint8_t> payload);
// A link loss now should save user 2 first (mode 1, its own slot live and both files verified). Any thread.
bool guest_save_final_save_wanted();
// server.cpp queued the host's own final 2910 (the next export's reason is "link lost").
void guest_save_tunnel_note_final_injected();
std::string guest_save_tunnel_describe();

}  // namespace cdmp::session2
