#pragma once
// Piece 4 of host-authoritative enemies (docs/research/pieza4-capturas.md §6,
// pieza4-botin-reaccion.md §5): our blows on the copy of one of the other
// player's enemies hurt the real enemy in THEIR world.
//
// Both sides run the same code:
// - Learner: our own blows on our own enemies teach what each of our attacks
//   (accessor action hash + frame) costs on a defence-1.0 enemy ("base"),
//   which flags its result has, and keep its AttackTargetAck /
//   ApplyActionRestrictionAck as templates for the other side's copy of us.
// - Sender: a DoFrameEventReq of our player whose targets include enemy
//   copies (our server ignores those silently, pieza4-capturas.md §1.3)
//   becomes one GuestHit event per copy.
// - Owner of the enemy: applies the blow with attribution to its own player
//   (VaryStatCheatReq; a lethal blow goes down to 1 HP and is finished by a
//   collision of our player, a normal kill with skill XP and loot),
//   and injects the AttackTargetAck of that attack from the other player's
//   copy (the AI notices it), with the stagger when that attack staggers
//   ([coop] guest_hit_stagger: restriction + hit buff, pieza4-tambaleo.md).

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/protocol.h"

namespace cdmp::net {

void guest_hits_set_enabled(bool on, bool stagger);

// Sniffer writer thread.
void guest_hits_observe_c2s(const uint8_t* packet, size_t size);
void guest_hits_observe_s2c(const uint8_t* packet, size_t size);

// Co-op (network thread, every loop): GuestHit events to send; also applies
// the other player's hits that are due and finishes lethal ones.
std::vector<proto::GameEvent> guest_hits_take_events();
// Co-op (network thread): a GuestHit from the other player, about one of OUR
// enemies. Applied `delay_ms` later (the playout delay of their copy), from
// guest_hits_take_events.
void guest_hits_on_event(const proto::GameEvent& ev, uint64_t delay_ms);

}  // namespace cdmp::net
