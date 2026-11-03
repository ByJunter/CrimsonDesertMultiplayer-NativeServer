#pragma once
// Option D, host side (docs/OPCION-D-EMULADOR.md §4.1-§4.5): the pieces that
// touch the host's in-process game server.
//
//   P1  session 2: a second NwVirtualAsyncSession, built like the game's own
//       (D0 §6) on ServerRunThread, with a private copy of its vtable whose
//       Send (vf9) is ours: session 1 never goes through any of our code.
//   P2  the guest's Req: a packet buffer laid out like ClientNet::PostPacket's,
//       tagged, pushed to the server's input queue (D0 §4, "option B"); a
//       mid-hook at the entry of ServerNet::OnRecvPacket swaps the session
//       argument for session 2 when it sees the tag.
//   P3  what the server writes to session 2: copied out by our vf9, the
//       buffer handed back as the client would (D0 §3.2).
//   P5  the emptied "near, except one" recipient finders: while user 2 is
//       logged in, they collect ToNear's sessions minus the one whose user
//       controls the actor (D0 §7.3, actor+0x90).
//   E0  probe: logs the session, user, queue and key fields (read only).
//   E0b finder log: how many sessions each emptied finder collects.
//   E4  mirror: the host player's MoveActorReq/StartActionReq/RotateActorReq,
//       copied to session 2 with user 2's character key, mirror_delay_ms
//       later: user 2 walks the host's path natively (one-PC test of P5).

#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

#include "mod/session2/guest_lifecycle.h"
#include "mod/session2/inject_logic.h"

namespace cdmp::session2 {

struct ServerOptions {
    bool probe = true;
    bool finder_log = false;
    bool create_session = false;
    bool accept_tunnel = false;
    bool relay_fix = false;
    // E4 on one PC: user 2 repeats the host player's movement this much later.
    bool mirror_host = false;
    uint32_t mirror_delay_ms = 1000;
    // Written into every LoginUserToFieldServerReq for session 2: never 0, the
    // host's own account (the server would take it for a relogin of the host).
    uint64_t account = 1;
};

// Checks the build and every function's first bytes, then installs what the
// options ask for. False = nothing was installed.
bool server_install(const ServerOptions& opt);

// E0: one read-only snapshot in the log.
void server_probe_log(const char* when);

// P1: session 2 exists (built on the server thread after install).
bool server_session_ready();
// C4: session 2 of the current epoch is ready, or a new epoch's is about to be
// built by the server thread from the guest's first Req (server_inject).
bool server_accepts_guest();
// User 2 is logged in (session 2 has a ServerUserActor).
bool server_user2_present();
// The host's user (session 1) and user 2 (session 2), both validated (vtable
// and back pointer). Any thread; false if either is missing.
bool server_users(uintptr_t& host, uintptr_t& user2);
// Each on its own (the clock gate must not depend on both at once): the
// host's user of the current server, and user 2 of a live session 2.
bool server_host_user(uintptr_t& host);
bool server_user2(uintptr_t& user2);
// The user of any session, validated the same way (ServerUserActor vtable and
// its back pointer +0x1F8 == session). Any thread, read only.
bool server_session_user(uintptr_t session, uintptr_t& user);
// ServerRunThread's id (the thread session 2 was built on; 0 = not yet).
uint32_t server_thread_id();
// P8 S2 (observer_milestone.h), any thread, read only: the live session 2 of
// the current epoch (0 = none), user 2's user key (its MainUserDataStartAck)
// and character key (its MainPlayerDataStartAck of this epoch; 0 = unknown),
// and whether THIS thread is inside take_frame (session 2's Send).
uintptr_t server_live_session2();
// P8 B step 2 (owner_fanout.h), any thread: session 1 (the host's) of the server session 2 was built in
// (0 = none yet). Only meaningful together with a live session 2.
uintptr_t server_host_session();
uint32_t server_user2_user_key();
uint32_t server_user2_player_key();
bool server_in_take_frame();
// P8 session 25 (clock_push.h), any thread, read only: user 2 is in the world of the live session 2 (its move
// phase is Stable: the server wrote its 2762 and it has asked for no other move since) and its goodbye was not asked.
bool server_user2_in_world();
// user2_world_anchor (world_anchor.h), any thread: a copy of user 2's move phase in this epoch (its phase and how
// many of each tracked Req / Ack, e.g. the LoadingCompleteAck 2762 the server wrote for it).
MovePhaseTracker server_user2_phase();
// P8 S1 option A (observer_seq_copy.h), any thread: a packet the mod makes for the live session 2 (the
// sequencer copies), queued only while the outgoing queue is under half its budget. False: not queued.
bool server_push_extra(std::span<const uint8_t> packet);

// P2: a guest Req for the server, from the network thread (or the E4 mirror
// thread). Nothing of the game runs here: the packet is checked and queued
// (inject_logic.h); the host's NetworkThread hands it to the server in order
// (allocation, buffer and Q0 push on that game thread, as the native
// forwarder does). The outcome says what the caller does with it
// (inj::disposition): Hold (not ready / queue full: offer it again, never a
// logout), Drop (policy), Stop (an exception inside the game's code in this
// epoch: injection is over, no retry, no logout by injection).
inj::Outcome server_inject_ex(std::span<const uint8_t> packet);
// server_inject_ex(...) == Queued.
bool server_inject(std::span<const uint8_t> packet);
// Network thread, every loop turn: logs the exceptions the NetworkThread
// caught (each one, loudly), the server not taking our buffers (with its
// threads' park flags), the NetworkThread not turning, the queue at its bound.
void server_inject_tick(uint64_t now_ms);
// An exception inside the game's code stopped the injection of the current epoch.
bool server_inject_halted();
std::string server_inject_halt_reason();
// Our queue (not yet handed to the server).
size_t server_inject_pending();
std::string server_inject_describe();

// C4 (docs/research/pendientes/salida.md §4): user 2's native goodbye. Asks
// for one LogoutSessionToServerReq 2001 (D1 07 FF) for session 2, the message
// the game's own client sends when it leaves (0x8CAD10); the server then runs
// its own logout (LeaveField, RemoveActor, user and session) with its locks
// and in its order. From the moment it is asked for, nothing else of user 2
// reaches the server (server_inject refuses it). It is handed at most once per
// session 2 (the native logout is not proven idempotent), as soon as user 2
// exists: if the login is still running, server_logout_tick hands it when the
// user appears (up to 60 s). `why` goes to the log. Network thread.
// final_save (P8 S25 phase B, §3.6: a lost link with user 2's own save active): one SaveAutoReq 3037 for user 2
// goes first, and the 2001 waits for the export of its slot (15 s at most).
void server_logout_user2(const std::string& why, bool final_save = false);
// Call every network-loop turn: hands a pending goodbye once user 2 exists.
void server_logout_tick();
// The goodbye was asked for (by user 2 or because its link was lost).
bool server_logout_requested();
// The installed build knows LogoutSessionToServerReq (C4 can work).
bool server_logout_supported();
// A Req the tunnel never feeds to the server (saving, deleting saves,
// cheats, dev tools): server_inject refuses it.
bool server_opcode_blocked(uint16_t opcode);
// A Req the host's server has a handler for (any other packet is not for it).
bool server_opcode_known(uint16_t opcode);

// P3: the packets the server wrote to session 2 since the last call.
std::deque<std::vector<uint8_t>> server_take_outgoing();
// S33: purgar sólo claves retiradas bajo la barrera de publicación.
bool server_prune_retired_outgoing() noexcept;
// True if an outgoing packet was lost (the conversation is broken).
bool server_outgoing_lost();
size_t server_outgoing_bytes();

// C4, reconnect (guest_lifecycle.h). The guest epoch is the mod's: session 2
// of epoch E is built for E alone, tags every packet buffer it gets with E,
// and stays alive (never freed, never reused) after its user's logout.
inline constexpr uint32_t kMaxGuestEpochs = 32;
// The epoch whose session 2 is wanted now (1 until the first reconnection).
uint32_t server_epoch();
// Network thread, after a clean barrier: epoch+1. Resets the per-epoch state
// (logout asked/handed, outgoing queue and its loss, user 2's character key,
// the mirror) and asks the server thread for a NEW session 2. Until it is
// built there is no live session 2. False if `epoch` is not the next one.
// forget_user2_keys (P8 re-entry, [session2] rejoin on): also forget the previous user 2's user key and the host
// client's guards for its keys (set again by the new user 2's 2646 / 2756). False: as before.
bool server_begin_epoch(uint32_t epoch, bool forget_user2_keys = false);
// The session 2 built for `epoch` (0 = none).
uintptr_t server_epoch_session(uint32_t epoch);
// The 2001 of the current session 2 went to the server.
bool server_logout_handed();
// A packet buffer of `epoch` was handed to the server.
bool server_epoch_reached(uint32_t epoch);
// Read only, while user 2 is valid (before its 2001 goes): adds its keys to snap.
bool server_capture_user2(DepartedSnap& snap);
// Read only: the host's field now (through the host's user and main character).
bool server_host_field(FieldSnap& out);
// Read only: what the barrier needs for `epoch` (all but the early queue).
void server_barrier_read(uint32_t epoch, const DepartedSnap& snap, BarrierReadings& r);
std::string server_epoch_describe();

std::string server_describe();
// The finder hook counters (atomics only: any thread); empty when the hooks are off.
std::string server_finder_describe();

}  // namespace cdmp::session2
