#pragma once
// Option D, piece P8 (docs/research/opcion-d-p8.md): on the GUEST's PC, its
// game client talks to the HOST's game server through the Troc tunnel instead
// of to its own in-process server.
//
//   out: an inline hook at the entry of NwVirtualSyncSession::Send
//        (0x14380B0, the only client->server funnel) copies each frame to the
//        tunnel and hands the buffer back to Q3, as the server would;
//   in:  a mid-hook in the NetworkThread loop (0x35BAD4, every <= 16 ms) feeds
//        the host's Acks to ClientNet::OnRecvPacket, which copies them.
//
// The choice is made per connection: when the client connects (its 1001,
// a new epoch in [session+0xB0]) the tunnel must already be up, or that
// connection stays local (a normal single-player game). The client logs in
// as soon as the game starts, so the hooks go in from their own thread as
// soon as ClientNet exists, before the 1001; if the client already connected,
// it is too late until the game restarts.

#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

namespace cdmp::session2 {

// From the worker thread, as early as possible ([session2] guest=1).
// remote=false (guest_mode=observe, the default): the hooks go in and only
// count; every connection stays local (stage G0). remote=true: a connection
// goes to the host when the host says its session 2 is ready.
// In remote mode, when the client's first LoginUserToFieldServerReq 2490 of a
// remote connection leaves for the host, the guest rebuilds its own save
// manager's slot table with the game's refresh (what its local UserLogin
// would have done), so the title enables Play/Load (pendientes/p8-titulo.md).
// slot_refresh_test ([session2] guest_slot_refresh_test=1, observe only): the
// same refresh once right after install, to check it on one PC.
// actor_diag / layer_guard ([session2] guest_actor_diag / guest_layer_guard,
// off by default): guest_actor_diag.h.
// s3_diag / crash_dump / update_guard ([session2] guest_s3_diag /
// guest_crash_dump / guest_update_guard, off by default): guest_s3.h.
void guest_start(bool remote, bool slot_refresh_test = false, bool actor_diag = false, bool layer_guard = false,
                 bool s3_diag = false, bool crash_dump = false, bool update_guard = false);

// The network thread: whether the tunnel to the host is up right now (read
// when the client connects), the host's packets for the client, and the
// client's packets for the host.
void guest_set_tunnel_up(bool up);
void guest_deliver(std::vector<uint8_t>&& packet);
// Also runs child_reannounce's step (P8 session 9, reannounce_logic.h): new CreateOwnedActor failures of a
// user's child become a control packet to the host, queued before the batch is taken.
// link_backlog (P8 S25 phase B): the tunnel's own backlog now, to pace the own-save upload (guest_save_guest.h).
std::deque<std::vector<uint8_t>> guest_take_outgoing(size_t link_backlog = 0);
// [session2] child_reannounce on the guest (0 / probe / 1): from the worker thread at start-up. False: off.
bool guest_reannounce_configure(const std::string& raw, bool errors_hook_expected);
bool guest_active();  // hooks installed
bool guest_remote();  // the current connection goes to the host
// S25 (docs/research/s25/invisibles.md): this game run already played on the host and the current connection does
// not (back to the title and Continue: its own server, alone). The HUD says so.
bool guest_local_after_remote();
// ui_pause_block (ui_pause.h): the current connection plays on the host right
// now: remote, the tunnel up and the connection not stopped. Atomics only.
bool guest_remote_live() noexcept;
// The client connection's epoch ([session+0xB0], 0 before the first): an atomic
// load, safe from any thread (the P8 probes stamp their events with it).
uint64_t guest_connection_epoch() noexcept;
// P8 re-entry (rejoin_logic.h, docs/research/s25/reentrada.md §1.4): the remote conversations of this game run (0 =
// none yet, 1 = the first, 2+ = re-entries). An atomic load, any thread: a module with state of one remote
// conversation compares it on its own thread and starts again when it changes (never from another thread).
uint32_t guest_conversation_id() noexcept;
// [session2] rejoin on the guest: troc::kCapsRejoin for the tunnel's caps packet (0 when off).
uint8_t guest_rejoin_caps();
// What the HUD says about the re-entry while the client does not play on the host (Spanish; "" = nothing).
std::string guest_rejoin_hud();
std::string guest_describe();
// B1 (pendientes/p8-s9-cinematica.md §8; guest_local_after_remote=stuck): the diagnostics thread, every 5 s. Ends the
// game when a save loaded locally after a remote session in this run is seen stuck (gxv::LocalLoadWatch).
void guest_local_load_watch() noexcept;

}  // namespace cdmp::session2
