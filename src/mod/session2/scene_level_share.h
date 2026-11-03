#pragma once
// S43 (docs/research/s25/s43-puertas-y-r42.md; the pure logic: scene_level_share_logic.h), HOST only:
// [session2] scene_level_share = 0 (default: nothing installed, exactly as before) | probe | 1.
//
// A  mid hook at the owner gate 0x283044D inside 0x2830400 (the only place a SceneLevel entry becomes ready):
//    user 2's own 2598 / 2104 in the host's field (its Troc, subject = this actor = its main character, eax = its
//    user key) -> probe: '[would ready]'; 1: eax := [field+0x180] (one register), '[ready for user 2]'.
// B  inline hook at the entry of the SceneLevel delete 0x2829E60: who caused it (its return address: 0x29C3049 the
//    2834, 0x28363D4 the owner's load 0x2836110, 0x2876460 the field loader; any other: always the original), the
//    level's name and its hash. Per user (host, user 2) the type-0 levels its client has loaded, from the C->S packets
//    the server sees (2598 adds, 2834 type 0 removes, 2104 is the whole list; 2283 / 2362 / 2437 / 2001, user 2 gone,
//    out of the host's field or a new epoch empty the set). Mode 1 skips ONLY a 2834 delete of a level another user
//    still has loaded, with the name's hash proven equal to the packet's in this process; 0x2836110 and the loader are
//    observed only ('[would keep]'). Probe: everything measured, the original always runs.
// Needs user2_funnels' ExecuteTroc subject (any of its keys on) for A and for B's 2834 / owner-load callers, and
// user2_stages (probe or 1) for "user 2 is in the host's field" (without it user 2's set never keeps anything).
// In the hooks: guarded reads, atomics and a lock-free ring only; lines from its own thread (every second, a summary
// every 60 s).

#include <cstdint>
#include <string>

namespace cdmp::session2 {

// From session2's install (host, create_session=1), once. Starts its thread (it waits for the game's code and the
// world, verifies every site, then hooks). 0: nothing at all. guest: never anything (a line if the key is set).
void scene_level_share_start(const std::string& raw, bool guest);

// server.cpp's OnRecvPacket hook (ServerRunThread, before the packet is handled; a mid hook): the whole request
// (5-byte header). from_user2: one of session 2's buffers; from_host: the host's own session. Never throws, never
// blocks (a busy writer marks that user's set as untrusted).
void scene_level_share_server_ingress(uint16_t op, uintptr_t packet, uint16_t size, bool from_user2,
                                      bool from_host) noexcept;
// The server instance was replaced: both sets start again (server.cpp, the same thread).
void scene_level_share_on_server_replaced() noexcept;

}  // namespace cdmp::session2
