#pragma once
// S46 phases 1 and 3, T2 (docs/research/s25/s46-tenderos.md; the pure logic: hold_scene_order_logic.h), HOST:
// [session2] observer_hold_scene_order = 0 (default) | probe | 1 (phase 3: active). Needs observer_hold_acks=1.
// probe, READ ONLY: no packet is kept, changed or dropped. In take_frame (session 2's Send, the game's threads), for user 2's
// own 2661 of a trade scene (smithy / butcher) that binds NPCs observer_hold_acks holds now: '[would hold scene]' with
// the keys, then the delay until their 2005s went and every opcode with that scene id (2981, 2524, 2810) it would
// have held. Lines and expiry on the mod's network thread (tick).

// S46 phase 3: observer_hold_scene_order=1 is T2 ACTIVE: the keeping itself is observer_hold_acks' (its route and
// lock, hso::SceneHold); this module formats its lines (observer_hold_scene_order_note) and the summary.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "mod/session2/hold_scene_order_logic.h"

namespace cdmp::session2 {

// session2::install (host, create_session=1), after observer_hold_acks_configure. value: the key's text.
void observer_hold_scene_order_configure(const std::string& value, bool observer_hold_acks_on);
bool observer_hold_scene_order_active() noexcept;
// take_frame, after the 2005 re-parse (the payload as queued), only when the packet reached the queue.
void observer_hold_scene_order_on_out(uint16_t opcode, std::span<const uint8_t> payload, bool to_user2,
                                      bool pushed) noexcept;
// The mod's network thread (tunnel_tick): expiry, user 2 gone, the lines, a summary every 60 s.
void observer_hold_scene_order_tick() noexcept;
// T2 active: observer_hold_acks_tick hands its events (taken under its lock, given outside it). lost: its total.
void observer_hold_scene_order_note(std::vector<hso::Event>& events, uint64_t lost) noexcept;

}  // namespace cdmp::session2
