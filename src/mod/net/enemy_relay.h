#pragma once
// Piece 3 of host-authoritative enemies (docs/research/enemigos-del-anfitrion.md):
// the guest sees copies of the HOST's enemies. Stage 3a (dev test): Ctrl+F6
// sends the nearest enemy, still, with its last action; the receiver loads a
// network-driven copy of it (key 0xB01F0000 + net id, control user = the
// sender's copy user, so control mode 2) and gives it that action right
// away (a network-created actor needs one: INFORME-CABALLO-CRASH.md §11).
// Ctrl+Shift+F6 removes the copies.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "common/protocol.h"

namespace cdmp::net {

inline constexpr uint32_t kEnemyCopyBase = 0xB01F0000;  // + net id (1..0xEFFF)
inline bool is_enemy_copy_key(uint32_t key) { return (key & 0xFFFF0000u) == kEnemyCopyBase; }

// ---- sending side (sniffer writer thread: everything the client sees)
void enemy_observe_s2c(const uint8_t* packet, size_t size);
void enemy_observe_c2s(const uint8_t* packet, size_t size);

// Character variant (characterinfo key, load +0x18) of one of our world's
// enemies, 0 if unknown; and its copy key on the other side, 0 if none.
uint32_t enemy_variant(uint32_t key);
uint32_t enemy_copy_of(uint32_t key);
// The reverse: our enemy behind one of the copies we announced, 0 if none.
uint32_t enemy_original_of(uint32_t copy);

// ---- dev hotkeys (own thread); `stop` is a HANDLE
void enemy_test_loop(void* stop);

// Co-op (network thread): reliable messages to send.
// EnemySpawn: u32 copy key | u32 load size | load | n x StartActionReq (725 B each,
// oldest first; n may be 0).
// EnemyRemove: n x u32 copy key.
// EnemyState: u16 opcode | payload (VaryHPAck, NotifyHandleDeadAck; keys translated).
std::optional<std::pair<proto::RelKind, std::vector<uint8_t>>> enemy_take_outgoing();
// Co-op (network thread, every loop): events about the enemies we copied to
// the other player (their actions and, at 15 Hz, their positions).
std::vector<proto::GameEvent> enemy_host_tick();

// ---- receiving side (network thread)
// Copies of the other player's enemies are loaded only once their crash
// guards are installed (mod.cpp).
void enemy_set_receive_enabled(bool on);
void enemy_on_spawn(const std::vector<uint8_t>& bytes);
void enemy_on_remove(const std::vector<uint8_t>& bytes);
// Network thread: the other side changed (new partner, restart, link lost):
// our copies sent there are forgotten (to be sent again) and the copies of
// theirs here are removed.
void enemy_reset_partner();
// `delay_ms`: the playout delay of the copies' actions.
void enemy_on_state(const std::vector<uint8_t>& bytes, uint64_t delay_ms);
// Network thread, every loop: logs where new copies ended up.
void enemy_tick();
// Network thread: an EnemyAct / EnemyPos event, with the clock shift and
// playout delay co-op uses for the other player's own events.
void enemy_on_event(const proto::GameEvent& ev, int64_t ts_shift_ms, uint64_t delay_ms);

}  // namespace cdmp::net
