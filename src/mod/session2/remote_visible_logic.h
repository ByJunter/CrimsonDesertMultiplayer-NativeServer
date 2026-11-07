#pragma once
// Build 3527: why the two players cannot see each other, and the decision that
// keeps them visible.
//
// When a character logs in, ClientMercenaryClanActorComponent::vf23 (exe+0x23143D0)
// looks for its id in the component's active list (+0xB8, empty unless the party
// has been changed) and otherwise hides it: exe+0x231DEE0 sets Status bit 0x100000,
// and Status::vf79 (exe+0x6E99D0) writes alpha 0 to [CC+0xE8]+0x100 for any non-zero
// flags. Only actor vf178 (exe+0x5B3440, "belongs to the local user") is exempt.
// A single player's character always passes that test; another user's never does,
// and nothing clears the bit afterwards. Measured on both clients: remote flags
// 0x100000, alpha 0, while position, action and login state are all correct.
//
// So: refuse exactly that request, for exactly another user's main character.
// Companions and everything else stay native. Pure decision, no game access.
//
// The clan's code runs for the server's actors too (the host's process has both). There the rule stays refuse_hide. On
// a client's copies, once the other player has said which character it controls (focus_follow_logic.h), that is the one
// that may not be hidden, whatever its class, and its main character left behind goes the native way.

#include <cstdint>

namespace cdmp::session2::rvis {

inline constexpr uint64_t kHiddenByClanBit = 0x100000;
inline constexpr uint32_t kPlayerCharacterClass = 0xA;

constexpr bool is_player_character(uint32_t key) noexcept { return (key >> 28) == kPlayerCharacterClass; }

// `bit` and `set` are Status' flag setter arguments. actor_user and local_user
// are the ClientUserActor pointers of the actor and of this client (0 = none yet).
constexpr bool refuse_hide(uint64_t bit, bool set, uint32_t key, uintptr_t actor_user, uintptr_t local_user) noexcept {
    return bit == kHiddenByClanBit && set && is_player_character(key) && actor_user != 0 && local_user != 0 &&
           actor_user != local_user;
}

// focus and main: what the other player said (0 = nothing yet). A main character other than the one it named (a new
// guest logging in with the last guest's words still stored) keeps the old rule.
constexpr bool refuse_client_hide(uint64_t bit, bool set, uint32_t key, uintptr_t actor_user, uintptr_t local_user,
                                  uint32_t focus, uint32_t main) noexcept {
    if (bit != kHiddenByClanBit || !set || actor_user == 0 || local_user == 0 || actor_user == local_user) return false;
    if (focus != 0 && key == focus) return true;
    if (!is_player_character(key)) return false;
    return !(focus != 0 && key == main);
}

}  // namespace cdmp::session2::rvis
