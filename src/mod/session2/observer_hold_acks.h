#pragma once
// P8 S3, HOST: [session2] observer_hold_acks=1 (default 0; needs observer_hold=1)
// (docs/research/pendientes/p8-escenas-y-acks.md §3; the pure logic: observer_hold_acks_logic.h).
//
// observer_hold withholds an NPC's 2005 from user 2 until its controller gave it its first action,
// but the server keeps relaying that NPC's per-actor packets (StartSpawnActionAck 2723,
// ChangeAIStateAck 2404, ...) to user 2 meanwhile: they reach its client BEFORE the NPC exists
// there and are dropped (session 6B: 465 x 2723 with "2723 consumer 0", ~430 NPCs x 3 x 2404).
// With this key, in take_frame (session 2's Send, the game's threads):
//   - a held opcode (oha::kHeldOps, actor key at payload +0) for a key observer_hold withholds from
//     user 2's session is kept, in order, instead of being queued;
//   - the 2005 that announces the key (observer_hold's native re-announce, or the engine's own) is
//     queued, then right after it the key's kept packets in their original order;
//   - a drop of the hold (despawn, out of range, epoch, user 2 leaving) discards them (the client
//     would drop them: the NPC is not there); a release whose 2005 did not show up during its
//     NotifyAppear waits 2 s more; any hold ends after 40 s;
//   - bounded (1024 keys, 32 packets / 32 KB per key, 4 MB in all): over a bound the key's kept
//     packets go out at once in order and its hold passes natively (the behaviour without the key).
// Only for user 2's session as the OBSERVER: the controller of an NPC is never held (child_hold.h),
// and nothing addressed to the host's own client ever passes here. With character_hold=1 the host's
// main character withheld from user 2 is one more key (its 0xA key; the same opcodes, bounds and drops);
// needs observer_hold=1 or character_hold=1.
// Measures in describe; lines with the held/released/discarded keys (a few of each).

#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "mod/session2/hold_scene_order_logic.h"
#include "mod/session2/observer_hold_acks_logic.h"

namespace cdmp::session2 {

// S33: barrera sólo con npc_activation=1. Orden: publicación -> holds/Acks ->
// cola. Recursiva: NotifyAppear llama Send en el mismo hilo. Nunca se mantiene
// un mutex de tabla al llamar funciones nativas del juego.
std::unique_lock<std::recursive_mutex> observer_hold_acks_publication_guard();
// Contador de publicaciones en vuelo: NO mantiene un cerrojo al llamar al juego.
// El retiro consulta idle bajo publication_guard y se pospone si hay un emisor activo.
class NpcPublicationScope {
public:
    NpcPublicationScope();
    ~NpcPublicationScope();
    NpcPublicationScope(const NpcPublicationScope&) = delete;
    NpcPublicationScope& operator=(const NpcPublicationScope&) = delete;
    bool active() const noexcept { return active_; }
private:
    bool active_ = false;
};
// Requiere publication_guard. own_thread_ok: las publicaciones del hilo llamador no cuentan (retirada de un hijo
// tardío desde su propia publicación); las de otros hilos siempre aplazan la retirada.
bool observer_hold_acks_publications_idle(bool own_thread_ok = false) noexcept;

void observer_hold_acks_set_retirement(bool on) noexcept;
bool observer_hold_acks_retire(uint32_t epoch, uint64_t generation,
                               std::span<const uint32_t> keys) noexcept;
void observer_hold_acks_actor_created(uint32_t epoch, uint64_t generation, uint32_t key) noexcept;
bool observer_hold_acks_retired(uint32_t key) noexcept;
// False: sólo claves retiradas. true: conservar (posiblemente2005 compactado).
bool observer_hold_acks_filter_retired(std::vector<uint8_t>& packet) noexcept;

void observer_hold_acks_configure(bool on);
bool observer_hold_acks_active();

// child_hold (observer_hold), any server thread, no lock of child_hold held:
//   begin     an NPC entry withheld from user 2's live session is about to be added or refreshed
//             (P8 review I5: called from the split BEFORE its native NotifyAppear calls, so no
//             packet of the key can pass meanwhile); returns what the hold table did (Full also
//             when the key is off: nothing held)
//   drop      such an entry was dropped (no 2005 will come)
//   released  a release NotifyAppear to user 2's session returned (keys of that call)
oha::Begin observer_hold_acks_begin(uint32_t key, uint32_t epoch) noexcept;
void observer_hold_acks_drop(uint32_t key) noexcept;
void observer_hold_acks_released(std::span<const uint32_t> keys) noexcept;
// A new guest epoch / the server instance replaced / user 2 gone: everything held goes.
void observer_hold_acks_clear(oha::Discard why) noexcept;
// Is `key` held now (its packets wait for its 2005)? A try-lock: busy counts as held (true). Never call
// it under this module's own lock (the push function of observer_hold_acks_route). attach_follow.
bool observer_hold_acks_holding(uint32_t key) noexcept;

// take_frame, for a packet to the LIVE session 2 (after parsing; before it is queued).
enum class AckRoute : uint8_t { Pass, Held, Pushed };
struct AckRouteOut {
    bool self_pushed = false;  // Pushed: the packet itself (a 2005, or the last of a spill) reached the queue
    uint32_t extra_pushed = 0; // Pushed: packets queued after/with it (released or spilled)
    uint32_t extra_failed = 0; // ... of which the queue refused
    // S46 phase 3, T2 active: the scene packets this 2005 released and the queue took (whole packets, in order), for
    // take_frame to show them to the observers that keep state on user 2's 2661 / 2981 / 2810 (observer_scene_copy,
    // observer_seq_copy, shop_probe) AFTER the route, outside its locks.
    std::vector<std::vector<uint8_t>> scene_released;
};
using AckPushFn = bool (*)(std::span<const uint8_t> packet);
// Pass: queue it as usual. Held: kept (packet moved from: do not use it). Pushed: queued here with
// what had to follow it, in order (a 2005 and its held packets; or, over a bound, the key's held
// packets then this one: packet may be moved from). After Held/Pushed the caller only counts.
AckRoute observer_hold_acks_route(uint16_t opcode, std::span<const uint8_t> payload, std::vector<uint8_t>& packet,
                                  AckPushFn push, AckRouteOut& out) noexcept;

// S46 phase 3, T2 ACTIVE (observer_hold_scene_order=1; hold_scene_order_logic.h hso::SceneHold): user 2's own trade
// 2661 binding keys held here, and every later packet of its scene id, are kept in this route, under this module's
// lock, and queued right after the 2005 that announces the last of those keys (with this module's own packets, in one
// original order). Its 2810 discards it; any other end discards it and drops later packets of its id. Set once, at
// install (session2), before any packet.
// own_key: user 2's character key (server_user2_player_key); note: where its events go (observer_hold_scene_order_note,
// called by tick outside this module's lock).
void observer_hold_acks_set_scene_order(bool on, uint32_t (*own_key)(),
                                        void (*note)(std::vector<hso::Event>&, uint64_t)) noexcept;
bool observer_hold_acks_scene_order_on() noexcept;
// Its counters and what it keeps now (the summary). Its event lines are handed to observer_hold_scene_order by tick.
hso::HoldCounters observer_hold_acks_scene_counters(size_t& scenes, size_t& tombs, size_t& bytes);

// The mod's network thread, every turn: expiry, user 2 gone, the lines.
void observer_hold_acks_tick() noexcept;
std::string observer_hold_acks_describe();
// A copy of the counters and the keys held now (describe, tests).
oha::Counters observer_hold_acks_counters();
size_t observer_hold_acks_keys();

}  // namespace cdmp::session2
