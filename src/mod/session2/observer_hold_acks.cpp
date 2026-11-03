#include "mod/session2/observer_hold_acks.h"

#include <windows.h>

#include <atomic>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#include "mod/log.h"
#include "mod/session2/npc_retirement_logic.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

std::atomic<bool> g_on{false};
std::atomic<bool> g_retirement_on{false};
std::recursive_mutex g_publication_mu;
size_t g_publications_in_flight = 0; // sólo bajo publication_mu
nret::Retired<> g_retired; // bajo g_mu
std::atomic<uint64_t> g_retired_filtered{0}, g_retired_packets{0}, g_retired_malformed{0};
std::mutex g_mu;  // leaf lock: under it only the outgoing queue's own lock is taken (its push)
oha::AckHold g_hold;                // under g_mu
std::atomic<size_t> g_keys{0};      // g_hold.keys(), read without the lock (the hot path's test)
std::atomic<uint64_t> g_scope_while_holding{0}, g_pushed_released{0}, g_pushed_spilled{0}, g_push_failed{0},
    g_begin_calls{0}, g_drop_calls{0}, g_released_calls{0}, g_ended_empty{0};

// S46 phase 3, T2 active (observer_hold_acks.h, hso::SceneHold): under g_mu, like g_hold.
std::atomic<bool> g_t2_on{false};
// Set once with g_t2_on (observer_hold_scene_order_configure): user 2's character key, and where the lines go. Plain
// function pointers so that the tests that link this file need no stub of the server / the line module.
std::atomic<uint32_t (*)()> g_t2_own_key{nullptr};
std::atomic<void (*)(std::vector<hso::Event>&, uint64_t)> g_t2_note{nullptr};
uint64_t t2_now_us() {  // T2's clock (its own: only SceneHold compares these times)
    LARGE_INTEGER f{}, c{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    if (f.QuadPart <= 0) return 0;
    return static_cast<uint64_t>(c.QuadPart / f.QuadPart * 1'000'000 + c.QuadPart % f.QuadPart * 1'000'000 / f.QuadPart);
}
std::atomic<size_t> g_t2_live{0};       // scenes kept + tombstones (the hot path's test, read without the lock)
hso::SceneHold g_scene;                  // under g_mu
std::vector<hso::Event> g_t2_events;     // under g_mu: handed to observer_hold_scene_order by tick
std::atomic<uint64_t> g_t2_events_lost{0};
constexpr size_t kT2MaxEvents = 256;
void t2_sync() { g_t2_live.store(g_scene.live(), std::memory_order_relaxed); }
void t2_keep(std::vector<hso::Event>& ev) {  // under g_mu
    for (hso::Event& e : ev) {
        if (g_t2_events.size() < kT2MaxEvents) {
            g_t2_events.push_back(std::move(e));
        } else {
            g_t2_events_lost.fetch_add(1, std::memory_order_relaxed);
        }
    }
    ev.clear();
}
// Under g_mu: observer_hold_acks' hold of `key` ended without its 2005: T2's scenes waiting for it are discarded.
void t2_key_gone(uint32_t key, oha::Discard why) {
    if (!g_t2_on.load(std::memory_order_relaxed) || !g_scene.scenes()) return;
    std::vector<hso::Event> ev;
    g_scene.key_gone(key, t2_now_us(), ev, static_cast<uint8_t>(why));
    t2_keep(ev);
    t2_sync();
}
void t2_clear() {  // under g_mu
    if (!g_t2_on.load(std::memory_order_relaxed) || !g_scene.live()) return;
    std::vector<hso::Event> ev;
    g_scene.clear(t2_now_us(), ev);
    t2_keep(ev);
    t2_sync();
}

enum Note : uint16_t { kNHeld, kNReleased, kNDiscarded, kNSpill, kNFull, kNScope, kNClear, kNoteCount };
p8p::EventRing<1024> g_notes;
std::atomic<uint64_t> g_notes_lost{0};
std::atomic<uint64_t> g_note_count[kNoteCount]{};
constexpr uint64_t kNoteCap[kNoteCount] = {32, 48, 48, 16, 16, 8, 16};

uint64_t now_ms() { return GetTickCount64(); }

void note(Note kind, std::initializer_list<uint64_t> v) {
    if (g_note_count[kind].fetch_add(1, std::memory_order_relaxed) >= kNoteCap[kind]) return;
    p8p::Event e;
    e.kind = kind;
    e.tid = GetCurrentThreadId();
    size_t i = 0;
    for (const uint64_t x : v) {
        if (i < 12) e.v[i++] = x;
    }
    if (!g_notes.push(e)) g_notes_lost.fetch_add(1, std::memory_order_relaxed);
}

void sync_size() { g_keys.store(g_hold.keys(), std::memory_order_relaxed); }

std::string note_text(const p8p::Event& e) {
    const uint64_t* v = e.v;
    switch (e.kind) {
        case kNHeld:
            return std::format("Session2 observer hold acks: NPC 0x{:08X} is withheld from user 2: its {} (and what "
                               "follows for it) waits for its 2005 instead of reaching user 2's client first (T{})",
                               v[0], v[1], e.tid);
        case kNReleased:
            return std::format("Session2 observer hold acks: NPC 0x{:08X}: its 2005 went to user 2, then its {} held "
                               "packet(s) in their order ({} .. {}), held {} ms (T{})",
                               v[0], v[1], v[3], v[4], v[2], e.tid);
        case kNDiscarded:
            return std::format("Session2 observer hold acks: NPC 0x{:08X}: {} held packet(s) discarded: {}", v[0], v[1],
                               oha::discard_name(static_cast<oha::Discard>(v[2])));
        case kNSpill:
            return std::format("Session2 observer hold acks: NPC 0x{:08X} went over a bound (32 packets / 32 KB per key, "
                               "4 MB in all): its {} packet(s) went to user 2 at once in order, the rest of its hold "
                               "passes natively",
                               v[0], v[1]);
        case kNFull:
            return std::format("Session2 observer hold acks: {} keys held: NPC 0x{:08X}'s packets pass natively", v[1],
                               v[0]);
        case kNScope:
            return "Session2 observer hold acks: a ScopeSenderAck 2042 bundle went to user 2 while NPC packets were "
                   "held: what is inside it is not seen here";
        case kNClear:
            return std::format("Session2 observer hold acks: {} held packet(s) of {} key(s) discarded: {}", v[0], v[1],
                               oha::discard_name(static_cast<oha::Discard>(v[2])));
        default:
            return std::format("Session2 observer hold acks: note {}?", e.kind);
    }
}

}  // namespace

std::unique_lock<std::recursive_mutex> observer_hold_acks_publication_guard() {
    std::unique_lock lock(g_publication_mu, std::defer_lock);
    if (g_retirement_on.load(std::memory_order_acquire)) lock.lock();
    return lock;
}
// Las publicaciones en vuelo de ESTE hilo (anidadas: NotifyAppear -> Send). Sólo cambia bajo publication_mu.
thread_local size_t t_own_publications = 0;
NpcPublicationScope::NpcPublicationScope() {
    auto guard = observer_hold_acks_publication_guard();
    active_ = guard.owns_lock();
    if (active_) { ++g_publications_in_flight; ++t_own_publications; }
}
NpcPublicationScope::~NpcPublicationScope() {
    if (active_) { std::lock_guard lock(g_publication_mu); --g_publications_in_flight; --t_own_publications; }
}
bool observer_hold_acks_publications_idle(bool own_thread_ok) noexcept {
    return g_publications_in_flight == (own_thread_ok ? t_own_publications : 0);
}
void observer_hold_acks_set_retirement(bool on) noexcept {
    std::lock_guard publication(g_publication_mu);
    std::lock_guard lock(g_mu);
    g_retired.clear();
    g_retirement_on.store(on, std::memory_order_release);
}
bool observer_hold_acks_retire(uint32_t epoch, uint64_t generation,
                               std::span<const uint32_t> keys) noexcept {
    if (!g_retirement_on.load() || epoch != server_epoch()) return false;
    auto publication = observer_hold_acks_publication_guard();
    std::lock_guard lock(g_mu);
    if (!g_retired.retire(epoch, generation, keys)) return false;
    for (const uint32_t key : keys) {
        g_retired_packets.fetch_add(g_hold.drop(key, oha::Discard::Dropped));
        t2_key_gone(key, oha::Discard::Dropped);
    }
    sync_size();
    return true;
}
void observer_hold_acks_actor_created(uint32_t epoch, uint64_t generation, uint32_t key) noexcept {
    if (!g_retirement_on.load() || epoch != server_epoch()) return;
    auto publication = observer_hold_acks_publication_guard();
    std::lock_guard lock(g_mu);
    g_retired.created(epoch, generation, key);
}
bool observer_hold_acks_retired(uint32_t key) noexcept {
    if (!g_retirement_on.load(std::memory_order_relaxed)) return false;
    std::lock_guard lock(g_mu);
    return g_retired.contains(key, server_epoch());
}
bool observer_hold_acks_filter_retired(std::vector<uint8_t>& packet) noexcept {
    if (!g_retirement_on.load(std::memory_order_relaxed)) return true;
    auto publication = observer_hold_acks_publication_guard();
    std::lock_guard lock(g_mu);
    const auto result = nret::filter_packet(packet, [](uint32_t key) {
        return g_retired.contains(key, server_epoch());
    });
    if (result == nret::Filter::Changed || result == nret::Filter::Drop) ++g_retired_filtered;
    if (result == nret::Filter::Malformed) ++g_retired_malformed;
    return result != nret::Filter::Drop;
}

void observer_hold_acks_configure(bool on) { g_on.store(on); }
bool observer_hold_acks_active() { return g_on.load(std::memory_order_relaxed) || g_retirement_on.load(std::memory_order_relaxed); }

oha::Begin observer_hold_acks_begin(uint32_t key, uint32_t epoch) noexcept {
    if (!g_on.load(std::memory_order_relaxed)) return oha::Begin::Full;
    auto publication = observer_hold_acks_publication_guard();
    g_begin_calls.fetch_add(1, std::memory_order_relaxed);
    try {
        oha::Begin b;
        size_t keys = 0;
        {
            std::lock_guard lock(g_mu);
            if (g_retired.contains(key, epoch)) return oha::Begin::Full;
            b = g_hold.begin(key, epoch, now_ms());
            keys = g_hold.keys();
            sync_size();
        }
        if (b == oha::Begin::Full) note(kNFull, {key, keys});
        return b;
    } catch (...) {
        return oha::Begin::Full;  // not held: its packets pass as without the key
    }
}

void observer_hold_acks_drop(uint32_t key) noexcept {
    if (!g_on.load(std::memory_order_relaxed) || !g_keys.load(std::memory_order_relaxed)) return;
    g_drop_calls.fetch_add(1, std::memory_order_relaxed);
    try {
        size_t n = 0;
        {
            std::lock_guard lock(g_mu);
            n = g_hold.drop(key, oha::Discard::Dropped);
            t2_key_gone(key, oha::Discard::Dropped);
            sync_size();
        }
        if (n) note(kNDiscarded, {key, n, static_cast<uint64_t>(oha::Discard::Dropped)});
    } catch (...) {
    }
}

void observer_hold_acks_released(std::span<const uint32_t> keys) noexcept {
    if (!g_on.load(std::memory_order_relaxed) || !g_keys.load(std::memory_order_relaxed)) return;
    g_released_calls.fetch_add(1, std::memory_order_relaxed);
    try {
        std::lock_guard lock(g_mu);
        g_hold.released(keys, now_ms());
    } catch (...) {
    }
}

void observer_hold_acks_clear(oha::Discard why) noexcept {
    if (!g_on.load(std::memory_order_relaxed)) return;
    try {
        size_t n = 0, k = 0;
        {
            std::lock_guard lock(g_mu);
            k = g_hold.keys();
            n = g_hold.clear(why);
            t2_clear();
            sync_size();
        }
        if (k) note(kNClear, {n, k, static_cast<uint64_t>(why)});
    } catch (...) {
    }
}

bool observer_hold_acks_holding(uint32_t key) noexcept {
    if (!g_on.load(std::memory_order_relaxed) || !g_keys.load(std::memory_order_relaxed)) return false;
    try {
        std::unique_lock lock(g_mu, std::try_to_lock);
        if (!lock.owns_lock()) return true;  // busy: as held (the caller skips, never waits)
        return g_hold.holding(key);
    } catch (...) {
        return true;
    }
}

AckRoute observer_hold_acks_route(uint16_t opcode, std::span<const uint8_t> payload, std::vector<uint8_t>& packet,
                                  AckPushFn push, AckRouteOut& out) noexcept {
    out = {};
    auto publication = observer_hold_acks_publication_guard();
    if (!observer_hold_acks_filter_retired(packet)) return AckRoute::Held;
    // La compactación2005 puede cambiar memoria/longitud: nunca reutilizar el span anterior.
    if (packet.size() >= 5 && opcode == 2005) payload = std::span<const uint8_t>(packet).subspan(5);
    const bool t2 = g_t2_on.load(std::memory_order_relaxed);
    if (!g_on.load(std::memory_order_relaxed) ||
        (!g_keys.load(std::memory_order_relaxed) &&
         // Review B7: with T2 on, every 2661 is looked at (counted, '[no hold]'), the rest only while T2 keeps something.
         !(t2 && (opcode == hso::kOpPrepare || g_t2_live.load(std::memory_order_relaxed))))) {
        return AckRoute::Pass;
    }
    try {
        if (opcode == oha::kOpScopeSender) {
            if (!g_keys.load(std::memory_order_relaxed)) return AckRoute::Pass;
            g_scope_while_holding.fetch_add(1, std::memory_order_relaxed);
            note(kNScope, {});
            return AckRoute::Pass;
        }
        // S46 phase 3, T2 active: user 2's own trade 2661 binding held keys, and every packet of a kept scene id, are
        // kept here, under this lock; a tombstoned id's packets are dropped. The rest goes on as before.
        if (t2 && opcode != oha::kOpAnnounce && hso::t2_op(opcode)) {
            const auto own_key = g_t2_own_key.load(std::memory_order_relaxed);
            const uint32_t own = own_key ? own_key() : 0;  // an atomic read (before the lock: no lock inside)
            std::vector<hso::Event> ev;
            std::lock_guard lock(g_mu);
            const hso::Take tk = g_scene.offer(
                opcode, payload, packet, t2_now_us(), own, [](uint32_t k) { return g_hold.withheld(k); },
                [] { return g_hold.next_seq(); }, ev);
            t2_keep(ev);
            t2_sync();
            if (tk != hso::Take::Pass) return AckRoute::Held;  // kept, or discarded with its scene: never queued here
            return AckRoute::Pass;  // none of T2's opcodes is one observer_hold_acks holds (held_key_of: nullopt)
        }
        if (!g_keys.load(std::memory_order_relaxed)) return AckRoute::Pass;
        if (opcode == oha::kOpAnnounce) {
            std::vector<uint32_t> keys;
            oha::announce_keys(payload, keys);
            if (keys.empty()) return AckRoute::Pass;
            std::vector<oha::Released> rel;
            std::lock_guard lock(g_mu);  // the 2005 and what follows it, with no other packet of these keys between
            size_t matched = 0;
            std::vector<oha::HeldPacket> after = g_hold.announce(keys, now_ms(), &rel, &matched);
            sync_size();
            // No hold for these keys: queued as usual. A hold that ended here, even with nothing
            // held, queues its 2005 under this lock (P8 review I5): once the key is no longer held
            // a packet of it offered by another thread passes at once, and pushed after the lock
            // it could reach the queue before this 2005.
            if (!matched) return AckRoute::Pass;
            if (after.empty()) g_ended_empty.fetch_add(1, std::memory_order_relaxed);
            // T2 active: the scenes whose last pending key this 2005 announces go right after it, merged with this
            // module's own released packets by their original sequence (one order: AckHold::next_seq).
            std::vector<hso::OutPacket> scene;
            if (t2 && g_scene.scenes()) {
                std::vector<hso::Event> ev;
                g_scene.announced(keys, t2_now_us(), scene, ev);
                t2_keep(ev);
                t2_sync();
            }
            const bool any_scene = !scene.empty();
            hso::merge_release(after, scene);
            out.self_pushed = push(packet);
            for (oha::HeldPacket& h : after) {
                ++out.extra_pushed;
                const bool ok = push(h.bytes);
                if (!ok) ++out.extra_failed;
                // key 0: a T2 scene packet (observer_hold_acks' own carry their actor key): shown to the observers
                // by take_frame after the route (only those the queue took).
                if (ok && h.key == 0 && any_scene) out.scene_released.push_back(std::move(h.bytes));
            }
            g_pushed_released.fetch_add(out.extra_pushed, std::memory_order_relaxed);
            if (out.extra_failed) g_push_failed.fetch_add(out.extra_failed, std::memory_order_relaxed);
            for (const oha::Released& r : rel) note(kNReleased, {r.key, r.count, r.wait_ms, r.first_op, r.last_op});
            return AckRoute::Pushed;
        }
        const auto key = oha::held_key_of(opcode, payload);  // an NPC's, or (character_hold) a character's
        if (!key) return AckRoute::Pass;
        std::vector<std::vector<uint8_t>> spill;
        std::lock_guard lock(g_mu);
        bool first = false;
        const oha::Offer o = g_hold.offer(opcode, *key, packet, now_ms(), spill, &first);
        if (o == oha::Offer::Pass) return AckRoute::Pass;
        if (o == oha::Offer::Held) {
            if (first) note(kNHeld, {*key, opcode});  // the first packet held for a key (a few keys get a line)
            return AckRoute::Held;
        }
        // Spill: what it held, then this one, in order, now (the behaviour without the key).
        for (size_t i = 0; i < spill.size(); ++i) {
            const bool ok = push(spill[i]);
            if (i + 1 == spill.size()) {
                out.self_pushed = ok;
            } else {
                ++out.extra_pushed;
                if (!ok) ++out.extra_failed;
            }
        }
        g_pushed_spilled.fetch_add(spill.size(), std::memory_order_relaxed);
        if (out.extra_failed) g_push_failed.fetch_add(out.extra_failed, std::memory_order_relaxed);
        note(kNSpill, {*key, spill.size()});
        return AckRoute::Pushed;
    } catch (...) {
        // Review B1: an allocation that failed AFTER the packet was moved in (oha's offer or T2's keep): it is gone,
        // never queue an empty packet nor let the observers read a moved-from payload.
        return packet.empty() ? AckRoute::Held : AckRoute::Pass;
    }
}

void observer_hold_acks_set_scene_order(bool on, uint32_t (*own_key)(),
                                        void (*note)(std::vector<hso::Event>&, uint64_t)) noexcept {
    g_t2_own_key.store(own_key);
    g_t2_note.store(note);
    g_t2_on.store(on);
}
bool observer_hold_acks_scene_order_on() noexcept { return g_t2_on.load(std::memory_order_relaxed); }
hso::HoldCounters observer_hold_acks_scene_counters(size_t& scenes, size_t& tombs, size_t& bytes) {
    std::lock_guard lock(g_mu);
    scenes = g_scene.scenes();
    tombs = g_scene.tombs();
    bytes = g_scene.bytes();
    return g_scene.counters();
}

void observer_hold_acks_tick() noexcept {
    if (!g_on.load(std::memory_order_relaxed)) return;
    try {
        const bool t2 = g_t2_on.load(std::memory_order_relaxed);
        if (g_keys.load(std::memory_order_relaxed) || (t2 && g_t2_live.load(std::memory_order_relaxed))) {
            // User 2 left or is leaving: nothing more is published to it (child_hold drops its entries too).
            uintptr_t user2 = 0;
            if (!server_user2(user2) || !user2 || server_logout_handed()) {
                observer_hold_acks_clear(oha::Discard::UserGone);
            } else {
                std::vector<oha::AckHold::Gone> gone;
                {
                    std::lock_guard lock(g_mu);
                    g_hold.expire(now_ms(), &gone);
                    for (const auto& g : gone) t2_key_gone(g.key, g.why);  // T2: their scenes go with them
                    if (t2 && g_scene.live()) {
                        std::vector<hso::Event> ev;
                        g_scene.tick(t2_now_us(), ev);
                        t2_keep(ev);
                        t2_sync();
                    }
                    sync_size();
                }
                for (const auto& g : gone) {
                    if (g.packets) note(kNDiscarded, {g.key, g.packets, static_cast<uint64_t>(g.why)});
                }
            }
        }
        // T2's lines, formatted by observer_hold_scene_order outside this lock.
        if (t2) {
            std::vector<hso::Event> ev;
            {
                std::lock_guard lock(g_mu);
                ev.swap(g_t2_events);
            }
            const auto note_fn = g_t2_note.load(std::memory_order_relaxed);
            if (!ev.empty() && note_fn) note_fn(ev, g_t2_events_lost.load());
        }
        p8p::Event e;
        while (g_notes.pop(e)) {
            if (e.kind < kNoteCount) log::info("{}", note_text(e));
        }
        static uint64_t lost_logged = 0;
        if (const uint64_t lost = g_notes_lost.load(); lost != lost_logged) {
            log::warn("Session2 observer hold acks: {} log line(s) lost (ring full)", lost - lost_logged);
            lost_logged = lost;
        }
    } catch (...) {
    }
}

oha::Counters observer_hold_acks_counters() {
    std::lock_guard lock(g_mu);
    return g_hold.counters();
}

size_t observer_hold_acks_keys() {
    std::lock_guard lock(g_mu);
    return g_hold.keys();
}

std::string observer_hold_acks_describe() {
    if (!g_on.load() && !g_retirement_on.load()) return {};
    oha::Counters c;
    size_t keys = 0, pkts = 0, bytes = 0;
    {
        std::lock_guard lock(g_mu);
        c = g_hold.counters();
        keys = g_hold.keys();
        pkts = g_hold.packets();
        bytes = g_hold.bytes();
    }
    std::string ops;
    for (size_t i = 0; i < oha::kOpCount; ++i) {
        if (c.held_by_op[i]) ops += std::format(" {} x{}", oha::kHeldOps[i], c.held_by_op[i]);
    }
    std::string disc;
    for (size_t i = 0; i < static_cast<size_t>(oha::Discard::Count); ++i) {
        if (c.discarded_pkts[i] || c.discarded_keys[i]) {
            disc += std::format(" {}: {} pkt / {} key;", oha::discard_name(static_cast<oha::Discard>(i)),
                                c.discarded_pkts[i], c.discarded_keys[i]);
        }
    }
    return std::format(
        "observer hold acks: keys withheld from user 2 {} (again {}, table full {}; now {}, peak {}) | packets held {} "
        "({}) | after their 2005: {} packet(s) of {} key(s) (wait mean {} ms, max {} ms; at most {} per key), 2005 "
        "with nothing held {} | discarded:{} | spilled {} key(s) / {} packet(s) (closed {}) | now {} packet(s), {} B "
        "(peak {} B) | queue refused {} | 2042 bundles while holding {} | calls: begin {}, drop {}, released {} | "
        "holds ended by a 2005 with nothing held (queued under the lock) {} | S33 retirada local: paquetes filtrados {}, Acks cancelados {}, 2005 malformados {}",
        c.begun, c.again, c.full, keys, c.keys_peak, c.held, ops.empty() ? std::string("none") : ops.substr(1),
        c.released_pkts, c.released_keys, c.released_keys ? c.wait_sum_ms / c.released_keys : 0, c.wait_max_ms,
        c.per_key_max, c.announced_empty, disc.empty() ? std::string(" none") : disc, c.spilled_keys, c.spilled_pkts,
        c.spilled_closed, pkts, bytes, c.bytes_peak, g_push_failed.load(), g_scope_while_holding.load(),
        g_begin_calls.load(), g_drop_calls.load(), g_released_calls.load(), g_ended_empty.load(),
        g_retired_filtered.load(), g_retired_packets.load(), g_retired_malformed.load());
}

}  // namespace cdmp::session2
