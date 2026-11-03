#pragma once
// Option D (docs/OPCION-D-EMULADOR.md): the host's in-process game server
// serves the guest's client as its second user. This module switches the
// pieces on in order, each behind its own [session2] key:
//
//   P6 save guard (first, always when anything writes)
//   E0 probe (read only)  ->  E0b finder log (read only)
//   P1+P3 session 2 asleep (E1)  ->  P2 tunnel into the server (E2+)  ->  P5 relay fix
//
// Rules: never together with [coop] (the copies model); nothing that writes
// to the game runs without the save guard.

#include <cstdint>
#include <string>
#include <vector>

#include "common/save_guard.h"
#include "common/troc_link.h"

namespace cdmp::session2 {

struct Options {
    bool probe = true;
    // Block refuses every save write; Trace (lab, session 24) refuses none and logs them;
    // Redirect (session 24) sends them all to a sandbox copy of the save folder.
    saveguard::Mode save_guard = saveguard::Mode::Block;
    bool block_save_trocs = false;
    bool finder_log = false;
    bool create_session = false;
    bool accept_tunnel = false;
    bool relay_fix = false;
    uint64_t account = 1;
    bool mirror_host = false;
    uint32_t mirror_delay_ms = 1000;
    bool share_field = false;  // E3b, needs create_session
    bool clock_gate = true;    // P-RELOJ, with create_session: user 2 never publishes the world clock
    bool identity_probe = false;  // C2-0, with create_session: read-only log of user 2's characterNo at login
    // C4 (guest_lifecycle.h): after user 2's logout and a clean barrier, a new
    // guest connection starts a new epoch with a session 2 of its own. 0 = as
    // before C4's second part: the tunnel stays dead after a logout.
    bool reconnect = false;
    // Companions step A (pendientes/companeros.md §A), with create_session: read-only log of owned-actor
    // creation, of the restore of remote actors and of the 0x232D5C0 null on the host's client.
    bool companion_probe = false;
    // D-53 (child_hold.h), with create_session: user 2's children reach the other users only once
    // the server accepted their first action. If it cannot be installed, option D goes on without it.
    bool child_hold = false;
    // C2 (identity_fix.h), with create_session: user 2's characterNo becomes 0x7FFFFFFFFFFFF002 in its
    // lobby and Status. If it is wanted and cannot be installed, user 2's logins are refused.
    bool identity_fix = false;
    // S3 (child_hold.h, pendientes/p8-impl-s3.md), with create_session: an NPC whose controller is a user and
    // whose restore byte is 0 reaches only its controller until its first action; the other users then get a
    // fresh native 2005. Off by default; with 0 nothing changes.
    bool observer_hold = false;
};

// From the late-init thread, once the game is up. Returns false if option D
// stays off (the reason is in the log).
bool install(const Options& opt);
bool installed();

// P8 S27 (rope_probe.h), both roles: the sniffer's tap fan-out also feeds rope_probe (sets the tap; idempotent).
void sniffer_tap_rope_probe();
// P8 S28 (docked_gimmicks.h), host: the sniffer's tap fan-out also feeds docked_gimmicks (sets the tap; idempotent).
void sniffer_tap_docked_gimmicks();
// P8 S34 (user2_funnels.h, quest_policy), host: the sniffer's tap fan-out also feeds its probe (sets the tap).
void sniffer_tap_quest_policy();

// One line for the periodic log.
std::string describe();

// The host's end of the Troc tunnel, driven by the network thread
// (mod.cpp): open a TrocLink (out ToGuestClient, in ToHostServer) once
// tunnel_wanted(), hand it the guest's packets with tunnel_on_packet and call
// tunnel_tick every loop. link_epoch = the network session's connection
// (ClientSession::epoch) the packet came on: with reconnect=1, only the
// current guest epoch's connection reaches the server.
bool tunnel_wanted();
void tunnel_on_packet(std::vector<uint8_t>&& packet, uint32_t link_epoch, uint32_t now_ms);
void tunnel_tick(troc::TrocLink& link, uint32_t now_ms);

}  // namespace cdmp::session2
