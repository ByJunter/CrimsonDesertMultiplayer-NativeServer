#pragma once
// P8 session 28: the gimmicks docked to a player character - phases probe and 1 ("local drive") of
// docs/research/s25/gimmicks-dueno.md section 3 (implemented: section 6). Build 1.0.0.2976; docked_gimmicks_logic.h
// has the decisions, the verified bytes and why each request is withheld.
//
// [session2] docked_gimmicks = 0 (default) | probe | 1, on BOTH PCs:
//   GUEST, packets only (no hook): every 2005 the host's server sends is classified on its way into the client
//     (guest.cpp's delivery, before ClientNet::OnRecvPacket); a gimmick record docked to THIS client's own
//     character (2756) whose controller is not this client's user (its own character's record +0x12) gets that user
//     at +0x12 (1) or is only counted (probe). Those keys are "local-only": the client's 3082 / 2758 / 2609 / 2774 /
//     3089 / 2779 / 2826 / 2618 for them are WITHHELD in guest_send (1; probe: counted as "would withhold"). Records docked
//     to the other player's character, to NPCs or not docked: never changed. The server is untouched (the host stays
//     the controller): phase 2 (server authority) is NOT here.
//   HOST: nothing is rewritten (the field owner already controls its own character's docked gimmicks; user 2's are
//     never taken). From the sniffer's capture ([sniffer] enabled=1): the same classification of every 2005 its
//     client receives (counts only).
//   Probes (probe and 1, log only), once the world is up, at verified ENTRIES (5-byte jumps, every byte checked,
//   REFUSED and nothing written otherwise):
//     both PCs: the client's gimmick event requests 0x8E59D0 (vf +0x740) and 0x8E5F60: per gimmick key, calls,
//       refusals (eErrNoActorNotExist: not this client's to control), event hashes, classed by the player parent the
//       key's 2005 named (since S29: the component's +0x2B4, used in S28, is not the parent); for each player-docked
//       2005 record, ~1.5 s later, the client actor's +0x90 against the value the record left there (+0x2B4 only
//       logged);
//     host: the docking spawns 0x28AD170 / 0x28ADE40 (the docked-to actor, its controller) and, inside them, the
//       controller the server writes at 0x2792A7B (mid hook, read only); accepted / rejected per requester at the
//       server's 0x2AC1270 (3082) and 0x2AC0790 (2758); at the server's receive (server.cpp) the 2221 unspawn
//       requests with the age of the OTHER user's last request for that gimmick (who still uses it).
// Lines 'DockedGimmicks ...' (at most 400 per run) and a 'DockedGimmicks summary' every 60 s.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cdmp::session2 {

// mod.cpp's worker, both roles. Nothing for 0.
void docked_gimmicks_start(const std::string& raw, bool guest);
// [session2] docked_gimmicks_other = 0 | probe | 1 (P8 S29; gimmicks-dueno.md section 8): the GUEST's mirror rule for
// the "Game Mechanic" records docked to the OTHER player's character (the host's grappling device: rewritten to this
// client's user with 1, its listed requests withheld; probe counts). Call after docked_gimmicks_start (needs it on);
// the host logs that it is ignored. Nothing for 0.
void docked_gimmicks_other_start(const std::string& raw, bool guest);
// [session2] gimmick_event_dedup = 0 | probe | 1 (gimmick_event_dedup_logic.h; gimmicks-dueno.md section 7), both
// roles, the same packet points and thread as docked_gimmicks: the guest's 2779 for gimmicks the other user controls
// (decided / withheld in 1); the host only logs (2779 by origin, duplicated 2250). Nothing for 0.
void gimmick_event_dedup_start(const std::string& raw, bool guest);

// GUEST (guest.cpp). A new remote conversation (re-entry) or the connection's end: the own character, its user and
// the local-only keys start again (the client's NetworkThread or the end of the connection).
void docked_gimmicks_guest_reset(const char* why) noexcept;
// A packet of the host's server about to be handed to the client (NetworkThread, guest_pump): its 2756 gives the own
// character, a 2516 retires local-only keys, a 2005 is classified and, in mode 1, rewritten in place (same size).
// All three on this one thread, in the order the client gets them.
void docked_gimmicks_guest_deliver(std::vector<uint8_t>& packet) noexcept;
// A request of the client about to go to the host (guest_send): true = withhold it (mode 1, a state request of a
// local-only gimmick). Probe: counted, false.
bool docked_gimmicks_guest_withhold(const uint8_t* packet, size_t size) noexcept;

// HOST. The sniffer's writer thread (session2.cpp's tap fan-out): one whole packet (header + payload).
void docked_gimmicks_sniffed(const uint8_t* packet, size_t size, uint64_t t_us, bool server_to_client) noexcept;
// HOST. The server's controller write 0x2792A7B (ICreateServerActorDesc::vf3: mov [r14+0x90],eax) is ONE mid hook,
// owned by this file and installed once for whichever key needs it first: docked_gimmicks' server probe or
// user2_stages' user2_npc_control (U-e, which its callback asks first: user2_stages_npc_control may set eax). Verified
// bytes (the write, the block's head 0x2792A11, the field-owner read 0x2792A70) or nothing is written. Any thread
// (a lock inside); true when live. `who` names the caller in the log.
bool docked_gimmicks_ctl_write_for_user2(const char* who) noexcept;

// HOST, inside ServerNet::OnRecvPacket's mid hook (server.cpp, ServerRunThread): one request about to be handled.
// Cheap; returns at once unless the probe is on.
void docked_gimmicks_server_ingress(uint16_t op, uintptr_t packet, uint16_t size, bool from_user2) noexcept;

}  // namespace cdmp::session2
