#pragma once
// [session2] user2_world_anchor = 0 (default) | probe | 1, HOST only (needs create_session=1).
// docs/research/noche/mundo-invitado.md (the cause, and its section "Implementado"); the rules: world_anchor_logic.h.
//
// The server's actor collector keeps an NPC alive only near the field owner's character (the host) or near the actor
// of the field's native "world observer" slot ([field+0x2E4] on, [field+0x2E8] key), free in normal play. With 1, that
// slot holds user 2's main character while user 2 stands in a field the host owns: what surrounds user 2 no longer
// dies once it is ~250 m from the host.
//   - The slot is written directly (the two fields the native 2245 handler writes, 0x29C2915), on ServerRunThread at
//     the OnRecvPacket hook that already exists, under the field's own lock taken with its TryLock (never waits).
//     No new hook, no .xtls, no injected packet.
//   - set after the server wrote user 2's LoadingCompleteAck 2762 and its character is in a field the host owns;
//     cleared at user 2's 2437 / 2283 / 2001 (before the server handles it, unless the field's lock stays busy for
//     5 ms: then at the following packets, counted) and when user 2 is gone without one (fallback); read back every
//     5 s (set again if lost, never over a foreign key).
//   - It runs only when ServerRunThread receives a packet (the host's or user 2's): the mod has no other periodic
//     point on that thread (its loop pops Q0 with a 1 s timeout and calls OnRecvPacket per packet; the server's
//     timers run on another thread). The host client's own Req keep it turning in play (<= ~5 s apart, measured).
//   - probe: the same decisions as "would" lines, the slot only read.
// Read-only measure in both probe and 1: the collector's reason-25 RemoveActorAck 2516 that user 2 is sent, with the
// actor's distance to the host and to user 2 (positions from the host client's actor manager), once a minute.
// Lines: 'WorldAnchor [set|verify|clear|fallback clear|lost|busy|forget|probe ...]' and 'WorldAnchor [minute]'.

#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

// install(), before the tunnel opens: the raw ini value. False = off.
bool world_anchor_configure(const std::string& raw);
bool world_anchor_active();
// Active with 1 (not probe): option U's U-1 needs it (user2_stages.h). Any thread.
bool world_anchor_on();

// ServerRunThread, at the OnRecvPacket hook (server.cpp on_server_recv), before the packet is handled: its opcode
// (0 if unread) and whether it is one of user 2's (a buffer of session 2). Every packet, the host session's too.
// Never throws.
void world_anchor_server_tick(uint16_t opcode, bool from_user2) noexcept;
// server.cpp, on ServerRunThread: the server instance was replaced (its fields are not ours to touch).
void world_anchor_on_server_replaced() noexcept;
// take_frame (the server's threads): a packet the server wrote for the live session 2 (only 2516 is read).
void world_anchor_on_user2_out(uint16_t opcode, std::span<const uint8_t> payload) noexcept;

// Network thread (tunnel_tick): the minute's summary line.
void world_anchor_tick();
// For Session2's describe(); empty when off.
std::string world_anchor_describe();

}  // namespace cdmp::session2
