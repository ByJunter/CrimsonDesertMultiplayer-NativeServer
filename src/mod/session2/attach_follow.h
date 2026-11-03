#pragma once
// P8 session 8, pets: [session2] attach_follow = 0 (default) | probe | 1, HOST (needs create_session)
// (docs/research/pendientes/p8-s8-mascotas.md §4.3; docs/research/pendientes/p8-impl-mascotas.md; the
// pure logic: attach_follow_logic.h). A generalisation of horse_follow.h for a child that rides its
// owner's character by a StartAction with a Path parent (the pet on the shoulder):
//   - host's pet -> user 2 (take_frame, session 2's Send, the game's threads): with 1, every 3rd
//     MoveActorAck 2697 of the character to user 2 is followed by a synthetic 2697 of the attached child
//     (character pose x the Path local of its last StartAction); probe: nothing made, the Path local
//     hypothesis measured (error lines and histograms);
//   - user 2's pet -> host (server inject, the mod's network thread): measured only, in every mode (a
//     synthetic MoveActorReq 2556 would change the server's state; not sent by this version).
// Only a user's child: its owner, read by key in the server's actor manager (child_hold.h
// hold_actor_owner, needs child_hold's hooks installed), is the host (outgoing) or user 2 (incoming).
// Mod memory only (std::vector of the mod, never the game's heap); no game function is called. The
// trackers sit behind a try-lock (a busy one skips the packet and counts it: never blocks a game thread).
//
// The attached-actor registry (both PCs; enabled by remote_interp_start when
// remote_interp_owned_cap_ms > 0): the 0xB keys whose last StartAction named an 0xA parent, read by
// remote_interp's hooks (lock-free).

#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

// session2::install (host, create_session=1): the raw ini value. False: off (0, unknown: warned).
bool attach_follow_configure(const std::string& raw);
bool attach_follow_active();

// take_frame, for a packet to the LIVE session 2 that was queued: its whole packet (header + payload).
// `push` queues one more packet right after it (false: no room). nullptr = observe only (the packets
// observer_hold_acks queues itself, under its lock, after a 2005: they update the attachments, nothing is
// made there). A synthetic Move is never made while child_hold or observer_hold_acks withholds the child.
using AttachPushFn = bool (*)(std::span<const uint8_t> packet);
void attach_follow_on_out(uint16_t opcode, std::span<const uint8_t> packet, AttachPushFn push) noexcept;
// server inject, a Req of user 2 that was queued for the server (its payload). Also feeds the registry.
void attach_follow_on_guest_req(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
// A new guest epoch: the attachments and the registry are forgotten.
void attach_follow_on_epoch() noexcept;
// The mod's network thread: the queued lines.
void attach_follow_tick() noexcept;
std::string attach_follow_describe();  // atomics only ("" when off)

// The registry. enable: remote_interp_start. on_in: the guest's mod network thread, every S>C packet of
// the host's server (its payload). attached: remote_interp's hooks (any thread). count: its summary.
void attach_registry_enable(bool on);
void attach_registry_on_in(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
bool attach_registry_attached(uint32_t key, uint64_t now_ms) noexcept;
size_t attach_registry_count(uint64_t now_ms) noexcept;

}  // namespace cdmp::session2
