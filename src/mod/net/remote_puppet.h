#pragma once
// The RECEIVING side of a remote player: a copy of another player's
// character loaded into this game (MMO code path: the client believes the
// server sent another player) and driven by that player's game events.
//
// Spawn injects the sender's two ActorLoadDataAck packets (user-owned actor
// + body) with fresh keys of ours. Events (local_capture.h) become the Acks a
// server would have relayed (MoveActorAck, StartActionAck, RotateActorAck,
// UpdateWallUpDataAck), queued and injected on the client's NetworkThread
// from inside the ClientNet::OnRecvPacket hook (troc_sniffer.cpp).
//
// Feed events from ONE thread only (the sniffer's writer thread for the
// mirror, the network thread for co-op).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common/protocol.h"

namespace cdmp::net {

// Any thread. Queue the load of a copy of the sender; false (with a log line)
// if a puppet already exists.
bool puppet_spawn(const proto::SpawnInfo& info);
// Any thread. Queue its removal (with fade-out); false if there is none.
bool puppet_remove();
bool puppet_spawned();
// True once after the sender dismounted: without a real link to a horse the
// copy stays stuck in the dismount pose (live tests), so co-op reloads it.
bool puppet_take_reload_request();
// Ask co-op to reload the copy where the sender is (any thread).
void puppet_request_reload();
// A copy of one of the sender's enemies (enemy_relay.h): its load, keys
// already translated, moved by the puppet offset, plus its recent actions
// (n x 725-byte StartActionReq, oldest first) queued right behind it. Network thread.
bool puppet_spawn_enemy(uint32_t copy_key, std::vector<uint8_t> load, std::vector<uint8_t> action_req);
// Moves / animates such a copy (network thread): a still sample where the
// enemy stands (built on the other player's last Move) or one of its
// StartActionReq / RotateActorReq, as the Acks a server would relay.
void puppet_enemy_move(uint32_t copy_key, uint64_t ts, const proto::Vec3& pos, const proto::Quat& rot,
                       int64_t ts_shift_ms, uint64_t delay_ms);
void puppet_enemy_action(uint16_t req_op, const std::vector<uint8_t>& req, int64_t ts_shift_ms,
                         uint64_t delay_ms);
// Removes one actor this side loaded (RemoveActorAck). Any thread.
void puppet_remove_actor(uint32_t key);

// Dev tests: queue one server->client packet for the client, now (any thread).
void puppet_inject_s2c(uint16_t op, const std::vector<uint8_t>& payload, uint64_t delay_ms = 0);
// Several packets, in this order and due together, so the client normally
// gets them in one OnRecvPacket pass like a real server batch.
void puppet_inject_s2c_batch(const std::vector<std::pair<uint16_t, std::vector<uint8_t>>>& packets);
// The game dropped the copy by itself (our loading screen, area change):
// forget it without sending a removal, so it can be loaded again.
void puppet_forget();
// Where the puppet is drawn right now (read from game memory); nullopt if
// it is not spawned or not loaded yet.
std::optional<proto::Vec3> puppet_position();

// The other player as a pin on our map (world map, and the minimap when
// near): the client half of the game's own pin markers, never told to our
// server, so never saved. `owner` is our own player key; nullopt removes the
// pin; `replace` removes the previous one first (needed to move it). Any thread.
void puppet_map_pin(uint32_t owner, const std::optional<proto::Vec3>& pos, bool replace = false);
// Shape (0-13) and colour (0-14) of that pin; out-of-range values are ignored.
void puppet_map_pin_style(uint8_t shape, uint8_t colour);

// Replays one event on the puppet.
//   ts_shift_ms: added to the event's timestamps to express them on THIS
//                game's clock, plus the playout delay;
//   delay_ms:    how long to hold the event before injecting it.
// Mirror: both are the mirror delay (same clock). Co-op: shift = estimated
// clock offset + delay.
void puppet_event(const proto::GameEvent& ev, int64_t ts_shift_ms, uint64_t delay_ms);

// Event thread, regularly: logs.
void puppet_tick();

// NetworkThread, inside the OnRecvPacket hook: next packet that is due.
// Never blocks (gives up if the queue is busy).
bool puppet_take_injection(std::vector<uint8_t>& out);

// Lets the puppet follow its network position during free wall climbs
// (hooks PointClimb::Enter; only the puppet is affected). Call once.
bool puppet_install_climb_fix();

// Crash guards for network-created copies whose action layers have a chart
// but nothing playing yet (INFORME-CABALLO-CRASH.md). Call once; idempotent.
bool puppet_install_layer_guards();
// Option D, E3b: one more actor key the 0x2329700 skip protects (user 2's
// character, which the host's client creates from the server's ActorLoadData
// before any action reaches it: C1 crashed at exe+0x1F63FE6). 0 = none.
void puppet_guard_also_key(uint32_t key);
// Option D: also every actor OWNED by this user (its character and children:
// pet, horse...), on the host's client (actor +0x90). A remote actor arrives
// with a chart and no action until its controller's first action is relayed;
// unlike the host's own actors nobody can start one locally (companion probe,
// 2026-10-09: user 2's child crashed 0x232D5C0 73 ms after its restore). 0 = off.
void puppet_guard_owner(uint32_t user_key);
// P8, the guest's client (pendientes/p8-actores-anfitrion.md §4.1(i)): also
// every actor NOT controlled locally, i.e. whose actor +0x90 is not this local
// user key (the host's NPCs, its character and children: they get their actions
// by network, seconds late). Locally controlled actors (+0x90 == the key) are
// never touched. 0 = off. `observer` (optional, may be null) is called on the
// game thread each time such an actor's layer update is skipped, with its key
// and its +0x90; it must be cheap and must not throw.
using PuppetGuardObserver = void (*)(uint32_t key, uint32_t control) noexcept;
void puppet_guard_not_controlled_by(uint32_t local_user_key, PuppetGuardObserver observer = nullptr);
// The guards' counters, for logs that run without the co-op (option D).
std::string puppet_guard_describe();
// S3 (p8-raiz-invitado.md §5.2): why the last 0x2329700 skip on THIS thread
// happened (call it from the observer): bits 1 = no current action (+0x10),
// 2 = no playback (+0x18), shifted by 2 for layer 1; and its text.
uint8_t puppet_guard_last_reason();
std::string puppet_guard_reason_text(uint8_t reason);

// Sideways offset of the puppet (world X, metres); 0 = the sender's exact path.
void puppet_set_offset(float metres);

// Load and drive the sender's horse too, with the mount/dismount fixes
// ([coop] horse=1; experimental, off by default). Call before
// puppet_install_climb_fix.
void puppet_set_horse_enabled(bool on);
// Apply the sender's equipment changes ([coop] equip=1; experimental).
void puppet_set_equip_enabled(bool on);

}  // namespace cdmp::net
