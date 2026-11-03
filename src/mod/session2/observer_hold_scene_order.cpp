#include "mod/session2/observer_hold_scene_order.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#include "mod/log.h"
#include "mod/session2/funcnpc_trade_policy.h"
#include "mod/session2/hold_scene_order_logic.h"
#include "mod/session2/observer_hold_acks.h"
#include "mod/session2/observer_hold_acks_logic.h"
#include "mod/session2/p8_probes.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

std::atomic<bool> g_on{false};
std::atomic<bool> g_active{false};          // S46 phase 3: observer_hold_scene_order=1 (T2 active)
std::atomic<uint64_t> g_active_lost{0};     // T2 active: events observer_hold_acks could not keep for the lines
std::mutex g_mu;                 // leaf: nothing else is taken under it (observer_hold_acks is asked before it)
hso::SceneOrder g_order;          // under g_mu
std::vector<hso::Event> g_lines;  // under g_mu: formatted on the network thread
std::atomic<uint64_t> g_count[static_cast<size_t>(hso::Kind::Count)] = {};
std::atomic<uint64_t> g_2661_own{0}, g_lost{0}, g_delay_max_us{0};
constexpr size_t kMaxPending = 256;

std::string keys_text(const std::vector<uint32_t>& k) {
    std::string s;
    for (const uint32_t x : k) s += std::format("{}0x{:08X}", s.empty() ? "" : " ", x);
    return s.empty() ? std::string("none") : s;
}
std::string ops_text(const hso::Event& e) {
    std::string s;
    for (const uint16_t op : e.ops) s += std::format("{}{}", s.empty() ? "" : " ", op);
    if (e.more_ops) s += std::format(" (+{} more)", e.more_ops);
    return s.empty() ? std::string("none") : s;
}
// S46 phase 3, T2 ACTIVE: what really happened (the packets were kept, released or discarded).
std::string active_line(const hso::Event& e, const std::string& scene) {
    switch (e.kind) {
        case hso::Kind::WouldHold:
            return std::format("HoldSceneOrder [HELD scene] user 2's own 2661 of {} binds NPC(s) observer_hold still "
                               "withholds from it: {} -> KEPT (with every later packet of this id) until their 2005",
                               scene, keys_text(e.keys));
        case hso::Kind::Released:
            return std::format("HoldSceneOrder [RELEASED scene] {}: the last of its NPCs' 2005s went {:.1f} ms after "
                               "its 2661: queued right behind it, in order: {}",
                               scene, static_cast<double>(e.delay_us) / 1000.0, ops_text(e));
        case hso::Kind::Cancelled:
            return std::format("HoldSceneOrder [DISCARDED scene] {}: its 2810 came while kept ({:.1f} ms; NPCs still "
                               "pending: {}): the scene, its 2810 and what it kept are dropped ({})",
                               scene, static_cast<double>(e.delay_us) / 1000.0, keys_text(e.keys), ops_text(e));
        case hso::Kind::Dropped:
            return std::format("HoldSceneOrder [DISCARDED scene] {}: {} after {:.1f} ms: dropped with {} (its later "
                               "packets too, up to its 2810)",
                               scene,
                               e.keys.empty() ? std::string("user 2's field changed")
                                              : std::format("its NPC {} was removed (2516)", keys_text(e.keys)),
                               static_cast<double>(e.delay_us) / 1000.0, ops_text(e));
        case hso::Kind::Expired:
            return std::format("HoldSceneOrder [DISCARDED scene] {}: 100 s without the 2005 of {}: dropped with {}",
                               scene, keys_text(e.keys), ops_text(e));
        case hso::Kind::Capacity:
            return std::format("HoldSceneOrder [DISCARDED scene] {}: the cap (32 scenes, 64 packets / 64 KB per scene, "
                               "1 MB in all): REFUSED and dropped with its 2661 and {}",
                               scene, ops_text(e));
        case hso::Kind::KeyGone:
            return std::format("HoldSceneOrder [DISCARDED scene] {}: observer_hold_acks' hold of {} ended without a "
                               "2005 ({}) after {:.1f} ms: dropped with {}",
                               scene, keys_text(e.keys),
                               e.reason == 0xFF ? std::string("user 2 gone or a new epoch")
                                                : std::string(oha::discard_name(static_cast<oha::Discard>(e.reason))),
                               static_cast<double>(e.delay_us) / 1000.0, ops_text(e));
        case hso::Kind::NoHold:
            return std::format("HoldSceneOrder [no hold] user 2's own 2661 of {}: none of its bound NPCs ({}) is "
                               "withheld now: it goes at once (the native order)",
                               scene, keys_text(e.keys));
        default:
            return "HoldSceneOrder: event ?";
    }
}

std::string line(const hso::Event& e) {
    const std::string_view name = u2trade::trade_name(e.hash);
    const std::string scene = std::format("scene 0x{:X} ({} 0x{:08X})", e.id, name.empty() ? "?" : name, e.hash);
    if (e.active) return active_line(e, scene);
    switch (e.kind) {
        case hso::Kind::WouldHold:
            return std::format("HoldSceneOrder [would hold scene] user 2's own 2661 of {} binds NPC(s) observer_hold "
                               "still withholds from it: {} -> T2 would keep it (and every later packet of this id) "
                               "behind their 2005 | probe: it went now",
                               scene, keys_text(e.keys));
        case hso::Kind::Released:
            return std::format("HoldSceneOrder [would hold scene] {}: the last of its NPCs' 2005s went {:.1f} ms after "
                               "its 2661: T2 would have released, in order, the opcodes {}",
                               scene, static_cast<double>(e.delay_us) / 1000.0, ops_text(e));
        case hso::Kind::Cancelled:
            return std::format("HoldSceneOrder [would hold scene] {}: its 2810 came while held ({:.1f} ms; NPCs still "
                               "pending: {}): T2 would have dropped it with {}",
                               scene, static_cast<double>(e.delay_us) / 1000.0, keys_text(e.keys), ops_text(e));
        case hso::Kind::Dropped:
            return std::format("HoldSceneOrder [would hold scene] {}: {} after {:.1f} ms: T2 would have released {} at "
                               "once (user 2's client rejects the scene, as today)",
                               scene,
                               e.keys.empty() ? std::string("user 2's field changed")
                                              : std::format("its NPC {} was removed", keys_text(e.keys)),
                               static_cast<double>(e.delay_us) / 1000.0, ops_text(e));
        case hso::Kind::Expired:
            return std::format("HoldSceneOrder [would hold scene] {}: 40 s without the 2005 of {}: T2 would have "
                               "released {} at once",
                               scene, keys_text(e.keys), ops_text(e));
        case hso::Kind::Capacity:
            return std::format("HoldSceneOrder [would hold scene] {}: too many scenes held: T2 would let it go at once "
                               "(keys {})",
                               scene, keys_text(e.keys));
        case hso::Kind::NoHold:
            return std::format("HoldSceneOrder [no hold] user 2's own 2661 of {}: none of its bound NPCs ({}) is "
                               "withheld now: it goes at once (the native order)",
                               scene, keys_text(e.keys));
        default:
            return "HoldSceneOrder: event ?";
    }
}

// Under g_mu.
void keep(std::vector<hso::Event>& ev) {
    for (hso::Event& e : ev) {
        g_count[static_cast<size_t>(e.kind)].fetch_add(1, std::memory_order_relaxed);
        if (e.kind == hso::Kind::Released) {
            uint64_t cur = g_delay_max_us.load(std::memory_order_relaxed);
            while (e.delay_us > cur && !g_delay_max_us.compare_exchange_weak(cur, e.delay_us)) {
            }
        }
        if (g_lines.size() < kMaxPending) {
            g_lines.push_back(std::move(e));
        } else {
            g_lost.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

uint32_t own_key_now() { return server_user2_player_key(); }

}  // namespace

void observer_hold_scene_order_configure(const std::string& value, bool observer_hold_acks_on) {
    const hso::Key k = hso::parse_key(value);
    if (k.bad) log::warn("HoldSceneOrder: observer_hold_scene_order={} is not 0, probe or 1: off", value);
    if (k.mode == hso::Mode::Off) return;
    if (!observer_hold_acks_on) {
        log::warn("HoldSceneOrder: observer_hold_scene_order needs observer_hold_acks=1 (its holds are the ones T2 "
                  "orders behind): off");
        return;
    }
    g_on.store(true);
    if (k.mode == hso::Mode::On) {
        g_active.store(true);
        observer_hold_acks_set_scene_order(true, &own_key_now, &observer_hold_scene_order_note);
        log::info("HoldSceneOrder: observer_hold_scene_order=1 (S46 phase 3, T2 ACTIVE): user 2's own 2661 of a trade "
                  "scene (smithy 0xA7132561, butcher 0x978345CA) binding NPCs observer_hold_acks holds is KEPT in its "
                  "route, with every later packet of that scene id (2981 / 2524 / 2111 / 2810), and queued right after "
                  "the 2005 of the last of them, in order; its 2810 discards it; any other end (the hold dropped / "
                  "expired, a 2516, a field change, 100 s, the cap: 32 scenes, 64 packets / 64 KB each, 1 MB) discards "
                  "it and later packets of its id. No game call, packets only");
        return;
    }
    log::info("HoldSceneOrder: observer_hold_scene_order=probe (READ ONLY): user 2's own 2661 of a trade scene (smithy "
              "0xA7132561, butcher 0x978345CA) binding NPCs observer_hold_acks holds -> '[would hold scene]' with the "
              "keys, the delay to their 2005s and the 2981 / 2524 / 2810 of that id it would have held; nothing kept");
}

bool observer_hold_scene_order_active() noexcept { return g_on.load(std::memory_order_relaxed); }

void observer_hold_scene_order_note(std::vector<hso::Event>& events, uint64_t lost) noexcept {
    try {
        g_active_lost.store(lost, std::memory_order_relaxed);
        std::lock_guard lock(g_mu);
        keep(events);
    } catch (...) {
    }
}

void observer_hold_scene_order_on_out(uint16_t opcode, std::span<const uint8_t> payload, bool to_user2,
                                      bool pushed) noexcept {
    // T2 active: the probe's watch is off (it would see none of the kept 2661s).
    if (!g_on.load(std::memory_order_relaxed) || g_active.load(std::memory_order_relaxed) || !to_user2 || !pushed) return;
    switch (opcode) {
        case hso::kOpPrepare:
        case hso::kOpAnnounce:
        case hso::kOpRemove:
        case hso::kOpBranch:
        case hso::kOpOnBranch:
        case hso::kOpDelete:
        case hso::kOpMoveField:
        case hso::kOpLoadingField:
        case hso::kOpBoundary2756:
            break;
        default:
            return;
    }
    try {
        const uint32_t own = server_user2_player_key();
        // The keys observer_hold_acks holds now, asked BEFORE this module's lock (each lock stays a leaf).
        std::vector<uint32_t> held;
        if (opcode == hso::kOpPrepare) {
            const hso::Prepare pr = hso::parse_prepare(payload);
            if (!pr.ok || !own || pr.owner != own || !u2trade::is_trade_hash(pr.hash)) return;
            g_2661_own.fetch_add(1, std::memory_order_relaxed);
            for (const uint32_t k : pr.bound) {
                if (observer_hold_acks_holding(k)) held.push_back(k);
            }
        }
        const auto holding = [&](uint32_t k) { return std::find(held.begin(), held.end(), k) != held.end(); };
        std::vector<hso::Event> ev;
        std::lock_guard lock(g_mu);
        g_order.feed(opcode, payload, p8_now_us(), own, holding, ev);
        keep(ev);
    } catch (...) {
    }
}

void observer_hold_scene_order_tick() noexcept {
    if (!g_on.load(std::memory_order_relaxed)) return;
    try {
        std::vector<hso::Event> out;
        static bool was_here = false;
        bool left = false;
        {
            uintptr_t user2 = 0;
            const bool gone = !server_user2(user2) || !user2;
            left = was_here && gone;  // live phase 1 (minor): a summary right when user 2 leaves
            was_here = !gone;
            std::lock_guard lock(g_mu);
            if (gone) {
                g_order.clear();
            } else {
                std::vector<hso::Event> ev;
                g_order.tick(p8_now_us(), ev);
                keep(ev);
            }
            out.swap(g_lines);
        }
        for (const hso::Event& e : out) {
            if (e.kind == hso::Kind::WouldHold || e.kind == hso::Kind::Released || e.kind == hso::Kind::NoHold) {
                log::info("{}", line(e));
            } else {
                log::warn("{}", line(e));
            }
        }
        static uint64_t last = 0;
        const uint64_t now = GetTickCount64();
        if ((now - last >= 60000 || left) && g_active.load(std::memory_order_relaxed)) {
            last = now;
            size_t scenes = 0, tombs = 0, bytes = 0;
            const hso::HoldCounters c = observer_hold_acks_scene_counters(scenes, tombs, bytes);
            const auto d = [&](hso::Kind k) { return c.discarded[static_cast<size_t>(k)]; };
            log::info("HoldSceneOrder summary (T2 ACTIVE): user 2's own trade 2661 {} | kept {} (released in order {}, "
                      "max delay {:.1f} ms; packets kept {}, released {}) | nothing held, went at once {} | DISCARDED: "
                      "by its 2810 {}, NPC removed / field changed {}, hold ended without its 2005 {}, 100 s {}, cap {} "
                      "(packets dropped {}; later packets of a discarded id dropped {}; a 2661 of a discarded id "
                      "decided again {}; 2810 not 17 B on a kept id {}) | now {} scene(s), {} tombstone(s), "
                      "{} B (peak {} scenes, {} B) | lines lost {} + {}",
                      c.own_2661, c.held, c.released, static_cast<double>(c.delay_max_us) / 1000.0, c.packets_held,
                      c.packets_released, c.nohold, d(hso::Kind::Cancelled), d(hso::Kind::Dropped),
                      d(hso::Kind::KeyGone), d(hso::Kind::Expired), d(hso::Kind::Capacity), c.packets_discarded,
                      c.tomb_dropped, c.tomb_reopened, c.odd_2810, scenes, tombs, bytes, c.peak_scenes, c.peak_bytes,
                      g_lost.load(),
                      g_active_lost.load());
        } else if (now - last >= 60000 || left) {
            last = now;
            log::info("HoldSceneOrder summary (probe): user 2's own trade 2661 {} | would hold {} (released {}, max delay "
                      "{:.1f} ms; cancelled by 2810 {}; dropped {}; expired {}; capacity {}) | nothing held {} | lines "
                      "lost {}",
                      g_2661_own.load(), g_count[0].load(), g_count[1].load(),
                      static_cast<double>(g_delay_max_us.load()) / 1000.0, g_count[2].load(), g_count[3].load(),
                      g_count[4].load(), g_count[5].load(), g_count[6].load(), g_lost.load());
        }
    } catch (...) {
    }
}

}  // namespace cdmp::session2
