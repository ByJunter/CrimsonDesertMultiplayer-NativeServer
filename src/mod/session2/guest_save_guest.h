#pragma once
// P8 session 25, the guest's own save, PHASE B, the GUEST's side (docs/research/noche/progreso-invitado.md §3.2, §3.5,
// §3.6, "Implementado (fase B)"). The wire and the pure state machines: guest_save_wire.h.
//
// [session2] guest_own_save on the guest's PC: 0 (default) | probe | 1.
//   probe  at start-up its own worker thread picks the slot "Continue" would load (the newest save.save of 0-2 /
//          100-108 under save\<accountID>\), checks it as the game would (and refuses one with a slotN_new /
//          slotN_backup folder) and keeps it in memory; at the host's first control packet on a tunnel connection
//          it goes to the host (SAVE_OFFER + SAVE_CHUNKs, paced under 256 KB of tunnel backlog) ahead of every
//          packet of the client. The results
//          (SAVE_RESULT + chunks) are checked and stored by that worker in
//          %LOCALAPPDATA%\cdmp\guest_progress\<host SteamID64>\incoming\gen<n>\ (+ manifest.txt, the last 10 kept)
//          and acknowledged (SAVE_RESULT_ACK). The client's packets are never held, no Req is added.
//   1      the same, and the holds (gsw::GuestHold): the client's packets wait while the start offer is still being
//          prepared, its 3093 waits for the host's SAVE_ACK (30 s at most; then it falls back and tells the host),
//          a 3093 for another slot sends that slot's offer first, and its goodbye 2001 waits behind one
//          SaveToFileCurrentPlayerReq 2910 (slot 100, 'Co-op': a 3037 does not save now, guest_save_wire.h) for the
//          final save's SAVE_RESULT (15 s at most; at once when the server's 3016 says it failed) when the host says
//          the own save is
//          active.
// Never a write inside the game's save folder: tools/scripts/apply-guest-progress.ps1 applies a generation with
// the game closed.

#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

namespace cdmp::session2 {

// From guest_start (the worker thread), before the tunnel exists.
void guest_save_guest_configure(const std::string& raw);
// troc::kCapsGuestSave when probe or 1 (the tunnel's caps packet), else 0.
uint8_t guest_save_guest_caps();

// The mod's network thread, every loop after guest_set_tunnel_up: the tunnel up or not, and its session epoch (a new
// one is a new connection: the upload goes again).
void guest_save_guest_tunnel(bool up, uint32_t epoch);
// A control packet of the host arrived on this connection (any state): the upload goes out now if it has not, BEFORE
// guest.cpp marks the host ready (so before the client's first packet can go remote). may_upload: no remote
// connection yet in this run (after one, the client never plays on the host again until the game restarts).
void guest_save_guest_host_seen(bool may_upload);
// P8 re-entry (s25/reentrada.md §3.6): the client NetworkThread, when a connection is decided remote. conversation:
// 1 = the first remote conversation of the run, 2+ = a re-entry. A re-entry offers nothing new (the host plays its live
// guest slot, written by the previous conversation's final save); the holds, the final save (once per conversation)
// and the host's SAVE_STATUS seen start again.
void guest_save_guest_conversation_begin(uint32_t conversation);
// A save control frame of the host (states 7, 8, 9, 10): consumed. False: not one (it goes on as before).
bool guest_save_guest_on_control(const std::vector<uint8_t>& packet);
// A packet of the host's server: its SaveToFileCurrentPlayerAck 3016 is logged and ends a final hold that failed.
void guest_save_guest_on_host_packet(uint16_t opcode, std::span<const uint8_t> payload);
// Acknowledgements, an offer and, paced (while link_backlog + what is taken stays under 256 KB), the upload's chunks:
// taken BEFORE the client's own packets. upload_pending: chunks are left; the client's packets must wait behind them.
std::deque<std::vector<uint8_t>> guest_save_guest_take_outgoing(size_t link_backlog, bool& upload_pending);
// The tunnel refused that many packets (mod.cpp): logged (the link resets; the upload goes again then).
void guest_save_guest_push_refused(size_t refused);

// The client NetworkThread (guest_send, guest_pump).
struct GuestSaveSend {
    bool held = false;            // the packet was copied into the hold (it goes later, in order): do not queue it now
    std::vector<uint8_t> before;  // a packet to queue right now, before anything held (the final 2910)
};
// A client packet of a remote connection, before it is queued for the host. `sent_3093`: the client's 3093 already
// went to the host in this connection.
GuestSaveSend guest_save_guest_on_send(const std::vector<uint8_t>& packet, bool sent_3093);
struct GuestSaveRelease {
    std::vector<std::vector<uint8_t>> packets;  // in order
    bool goodbye = false;                       // a 2001 is among them (or was dropped with `broken`)
};
// The held packets once their hold ends (in order), or all of them dropped when the connection broke.
GuestSaveRelease guest_save_guest_release(bool broken);

std::string guest_save_guest_describe();

}  // namespace cdmp::session2
