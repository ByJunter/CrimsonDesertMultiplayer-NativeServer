#include "mod/session2/housing_share.h"

#include <windows.h>

#include <atomic>
#include <format>
#include <map>
#include <mutex>
#include <vector>

#include "mod/log.h"
#include "mod/session2/housing_share_logic.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

enum class Mode : uint8_t { Off, Probe, On };
std::atomic<Mode> g_mode{Mode::Off};
// Live changes of the host's housing forwarded to user 2: only with housing_share=live. 2026-11-06 (3576, two copies):
// the host entering the housing design mode got UpdateWaitingCustomHousingGimmickSyncDataListAck (2500, the sync ids of
// the gimmicks its OWN client spawned for the mode); forwarded to user 2, the guest's client crashed 72 ms later
// (exe+0x3D7FB6, a reference release through a garbage pointer). The login set is safe (every join that day).
std::atomic<bool> g_live{false};
std::vector<uint16_t> g_live_ops;  // housing_share_live_ops; written once by housing_share_configure, before any packet
// housing_share_edits=1: the host's construction-mode edits (its client's 2408, hsh::scene_req_to_ack) reach user 2.
std::atomic<bool> g_edits{false};
std::vector<std::vector<uint8_t>> g_edit_parts;  // the 2733 parts of the edit being received (g_mx)
uint32_t g_edit_stage = 0;
uint64_t g_edit_at = 0;
std::atomic<uint64_t> g_edits_seen{0}, g_edits_queued{0}, g_edit_parts_bad{0};
// housing_share_guest_spawn=0: user 2's own spawn of the housing gimmicks is dropped (hsh::kGuestSpawnReqs).
std::atomic<bool> g_block_guest_spawn{true};
// housing_share_rekey (default 0; 1 = the old behaviour): with 1 the host's housing packets get user 2's character key,
// so the guest's client takes the house as ITS OWN. 2026-11-07 (3576, two copies): owning the host's house, the guest's client runs the owner's side of
// it -- it spawns the housing gimmicks itself (crash exe+0x462BDA on approach) and follows the host into the housing
// mode (its own UpdateHousingModeEnableReq, then the crash exe+0x4632EA, sync id 0x80) -- for objects only the host's
// client created (a list, 35245,2733, crashed the same way). 0 keeps the host's key: the house is another player's.
hsh::RekeySet g_rekey{false, {}};  // written once by housing_share_configure, before any packet
hsh::DropGimmicks g_drop_gimmicks = hsh::DropGimmicks::None;  // housing_share_drop_gimmicks; written once, before any packet
std::atomic<uint64_t> g_gimmick_records_dropped{0};

std::atomic<uint64_t> g_guest_spawn_blocked{0};
std::mutex g_mx;
std::map<uint16_t, hsh::Slot> g_host;     // opcode -> the host client's latest packets of it
std::map<uint16_t, uint64_t> g_given_at;  // opcode -> when user 2 last got the host's packets of it (ms)
std::atomic<uint32_t> g_host_key{0}, g_user2_key{0};
std::atomic<uint64_t> g_recorded{0}, g_dropped{0}, g_queued{0}, g_queue_failed{0}, g_forwarded{0}, g_no_host{0},
    g_keys_rewritten{0};
std::atomic<int> g_lines{0};
constexpr uint64_t kGiveOnceMs = 3000;

bool may_log() { return g_lines.fetch_add(1, std::memory_order_relaxed) < 120; }
bool on() { return g_mode.load(std::memory_order_relaxed) == Mode::On; }

// What user 2 gets of one of the host's packets: re-keyed if housing_share_rekey says so, and for a 2733 without its
// gimmick records if housing_share_drop_gimmicks says so (the waiting ids from the host's latest 2500 of that stage).
// g_mx held.
std::vector<uint8_t> for_user2(uint16_t op, std::span<const uint8_t> p, uint32_t host, uint32_t user2, size_t* keys) {
    if (keys) *keys = 0;
    std::vector<uint8_t> v = g_rekey.has(op) && host && user2 && host != user2 ? hsh::rekey(p, host, user2, keys)
                                                                               : std::vector<uint8_t>(p.begin(), p.end());
    if (op != hsh::kSceneListAck || g_drop_gimmicks == hsh::DropGimmicks::None) return v;
    std::vector<uint32_t> waiting;
    if (g_drop_gimmicks == hsh::DropGimmicks::Waiting) {
        const auto stage = hsh::stage_of_list_ack(p);
        const auto it = g_host.find(hsh::kWaitingSyncList);
        if (it != g_host.end() && !it->second.packets.empty() && stage) {
            uint32_t s = 0;
            auto ids = hsh::waiting_ids(it->second.packets.back(), &s);
            if (s == *stage) waiting = std::move(ids);
        }
        if (waiting.empty()) waiting.push_back(0xFFFFFFFEu);  // none known: drop nothing rather than everything
    }
    size_t n = 0;
    v = hsh::drop_gimmick_records(v, waiting, &n);
    g_gimmick_records_dropped.fetch_add(n, std::memory_order_relaxed);
    return v;
}

// The host's packets of `op`, re-keyed for user 2, queued (or, in probe, only counted). g_mx held.
size_t give(uint16_t op, uint32_t user2, const char* why) {
    const auto it = g_host.find(op);
    if (it == g_host.end() || it->second.packets.empty()) {
        g_no_host.fetch_add(1, std::memory_order_relaxed);
        if (may_log()) log::info("HousingShare: user 2's {} ({}): the host has none of it; nothing given", op, why);
        return 0;
    }
    const uint32_t host = g_host_key.load();
    size_t n = 0, keys = 0;
    for (const auto& p : it->second.packets) {
        size_t k = 0;
        const auto v = for_user2(op, p, host, user2, &k);
        keys += k;
        if (!on()) {
            ++n;
            continue;
        }
        if (server_push_extra(v)) {
            ++n;
            g_queued.fetch_add(1, std::memory_order_relaxed);
        } else {
            g_queue_failed.fetch_add(1, std::memory_order_relaxed);
        }
    }
    g_keys_rewritten.fetch_add(keys, std::memory_order_relaxed);
    if (may_log()) {
        log::info("HousingShare: {} the host's {} ({} packet(s), {} B first, owner 0x{:08X} -> 0x{:08X}, {} key(s)) to user 2 "
                  "({})",
                  on() ? "QUEUED" : "would queue", op, n, it->second.packets.front().size(), host, user2, keys, why);
    }
    return n;
}

// One part of the host client's SyncHousingPlacedSceneObjectListReq: kept until the last part, then the whole list
// replaces that stage in the recorded login set (a later join gets it) and goes to user 2 if it is in the world.
void on_scene_list_req(std::span<const uint8_t> p) {
    const uint32_t host = g_host_key.load();
    if (!host) {
        g_edit_parts_bad.fetch_add(1, std::memory_order_relaxed);
        if (may_log()) log::info("HousingShare: the host edited a housing stage ({} B) before its login set: not used", p.size());
        return;
    }
    const auto ack = hsh::scene_req_to_ack(p, host);
    const auto stage = ack ? hsh::stage_of_list_ack(*ack) : std::nullopt;
    if (!ack || !stage) {
        g_edit_parts_bad.fetch_add(1, std::memory_order_relaxed);
        if (may_log()) log::info("HousingShare: a 2408 of {} B is not a whole list part: not used", p.size());
        return;
    }
    const uint8_t flags = (*ack)[hsh::kHeader];
    const uint64_t now = GetTickCount64();
    std::vector<std::vector<uint8_t>> parts;  // the whole list, once its last part is in
    {
        std::lock_guard lk(g_mx);
        if (g_edit_parts.empty() || g_edit_stage != *stage || now - g_edit_at > hsh::kBurstMs ||
            g_edit_parts.size() >= 16) {
            g_edit_parts.clear();
            g_edit_stage = *stage;
        }
        g_edit_at = now;
        g_edit_parts.push_back(*ack);
        if (!hsh::is_last_part(flags)) return;
        hsh::replace_stage(g_host[hsh::kSceneListAck], *stage, g_edit_parts);
        parts.swap(g_edit_parts);
        for (auto& part : parts) part = for_user2(hsh::kSceneListAck, part, host, g_user2_key.load(), nullptr);
    }
    g_edits_seen.fetch_add(1, std::memory_order_relaxed);
    size_t bytes = 0;
    for (const auto& part : parts) bytes += part.size();
    // Queued outside g_mx (server_push_extra takes the queue's own locks).
    const uint32_t user2 = g_user2_key.load();
    const bool to_user2 = on() && user2 && server_user2_in_world();
    size_t queued = 0;
    if (to_user2) {
        for (const auto& part : parts) {
            if (server_push_extra(part)) ++queued;
        }
        g_edits_queued.fetch_add(queued, std::memory_order_relaxed);
    }
    if (may_log()) {
        log::info("HousingShare: the host edited housing stage 0x{:X} (construction mode: {} part(s), {} B as 2733): {} "
                  "| kept for user 2's next join",
                  *stage, parts.size(), bytes,
                  !on()       ? std::string("probe, not queued")
                  : !to_user2 ? std::string("user 2 not in the world")
                              : std::format("{} of {} part(s) QUEUED to user 2 (0x{:08X})", queued, parts.size(), user2));
    }
}

}  // namespace

void housing_share_configure(const std::string& mode, const std::string& live_ops, bool edits, bool guest_spawn,
                             const std::string& rekey, const std::string& drop_gimmicks) {
    g_drop_gimmicks = hsh::parse_drop_gimmicks(drop_gimmicks);
    g_edits.store(edits);
    size_t bad_rekey = 0;
    g_rekey = hsh::parse_rekey(rekey, &bad_rekey);
    if (bad_rekey) log::warn("HousingShare: housing_share_rekey '{}': {} entr(ies) not a housing opcode: ignored", rekey, bad_rekey);
    g_block_guest_spawn.store(!guest_spawn);
    if (mode == "1" || mode == "live") {
        g_mode.store(Mode::On);
        g_live.store(mode == "live");
        size_t bad = 0;
        g_live_ops = hsh::parse_live_ops(live_ops, &bad);
        if (bad) {
            log::warn("HousingShare: housing_share_live_ops '{}': {} entr{} not a live housing change (or 2500): ignored",
                      live_ops, bad, bad == 1 ? "y is" : "ies are");
        }
    } else if (mode == "probe") {
        g_mode.store(Mode::Probe);
    } else {
        if (!mode.empty() && mode != "0") {
            log::warn("HousingShare: housing_share '{}' is not 0 | probe | 1 | live; off", mode);
        }
        return;
    }
    log::info("HousingShare {} (housing_share={}): user 2 gets the host's houses and their furniture instead of its own "
              "(the host client's housing Acks); user 2's own login set is dropped{}{}{}{}{}",
              on() ? "ON" : "PROBE", mode,
              !g_live.load()         ? std::string(" | live changes NOT forwarded (housing_share=live for that)")
              : g_live_ops.empty()   ? std::string(" | live changes forwarded too, all but 2500 (UNSAFE: untested)")
                                     : std::format(" | live changes forwarded too, only {} (UNSAFE: under test)", live_ops),
              g_edits.load() ? " | construction-mode edits sent too (housing_share_edits=1: the host client's 2408 as 2733)"
                             : " | construction-mode edits NOT sent (housing_share_edits=1 for that)",
              g_block_guest_spawn.load() ? " | user 2's own housing-gimmick spawn requests DROPPED (housing_share_guest_spawn=0)"
                                         : " | user 2 spawns the housing gimmicks itself (housing_share_guest_spawn=1)",
              g_rekey.all          ? std::string(" | re-keyed to user 2's character (it owns the house)")
              : g_rekey.ops.empty() ? std::string(" | the host's key kept (housing_share_rekey=0: the house is the host's)")
                                    : std::format(" | re-keyed to user 2's character only: {} (housing_share_rekey)", rekey),
              g_drop_gimmicks == hsh::DropGimmicks::None ? ""
              : g_drop_gimmicks == hsh::DropGimmicks::All
                  ? " | the 2733's gimmick records (category 4) left out for user 2 (housing_share_drop_gimmicks=all)"
                  : " | the 2733's records of the host's waiting gimmicks left out for user 2 (housing_share_drop_gimmicks=waiting)");
}

bool housing_share_wanted() { return g_mode.load(std::memory_order_relaxed) != Mode::Off; }

void housing_share_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept {
    if (!housing_share_wanted() || !packet || size < hsh::kHeader) return;
    try {
        const std::span<const uint8_t> p(packet, size);
        const uint16_t op = hsh::op_of(p);
        if (!server_to_client) {
            if (op == hsh::kSceneListReq && g_edits.load(std::memory_order_relaxed)) on_scene_list_req(p);
            return;
        }
        if (!hsh::is_housing(op)) return;
        if (const auto k = hsh::owner_of(p)) g_host_key.store(*k);
        const uint64_t now = GetTickCount64();
        std::lock_guard lk(g_mx);
        hsh::record(g_host[op], p, now);
        g_recorded.fetch_add(1, std::memory_order_relaxed);
        // A live change of the host's housing: user 2 gets it too, if it already has the host's set.
        if (hsh::is_update(op) && !hsh::forward_live(g_live.load(std::memory_order_relaxed), op, g_live_ops)) {
            if (may_log()) {
                log::info("HousingShare: the host's housing changed ({}, {} B): NOT forwarded to user 2 ({})", op, size,
                          !g_live.load()                    ? "housing_share=1 gives the login set only"
                          : op == hsh::kWaitingSyncList     ? "2500 is never forwarded: it crashed the guest"
                                                            : "not in housing_share_live_ops");
            }
        } else if (hsh::is_update(op) && g_user2_key.load() && server_user2_in_world()) {
            const uint32_t host = g_host_key.load(), user2 = g_user2_key.load();
            size_t k = 0;
            const auto v = g_rekey.has(op) && host && host != user2 ? hsh::rekey(p, host, user2, &k)
                                                                    : std::vector<uint8_t>(p.begin(), p.end());
            const bool ok = on() && server_push_extra(v);
            if (ok) g_forwarded.fetch_add(1, std::memory_order_relaxed);
            if (may_log()) {
                log::info("HousingShare: the host's housing changed ({}, {} B): {} to user 2", op, size,
                          on() ? (ok ? "QUEUED" : "NOT queued (queue full)") : "would queue");
            }
        }
    } catch (...) {
    }
}

bool housing_share_blocks_request(uint16_t opcode) noexcept {
    if (!on() || !g_block_guest_spawn.load(std::memory_order_relaxed) || !hsh::is_guest_spawn_req(opcode)) return false;
    const uint64_t n = g_guest_spawn_blocked.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 6 && may_log()) {
        log::info("HousingShare: user 2's own housing-gimmick spawn request {} DROPPED (#{}; housing_share_guest_spawn=0: "
                  "only the host's client spawns them)",
                  opcode, n);
    }
    return true;
}

bool housing_share_on_out(uint16_t opcode, std::span<const uint8_t> packet) noexcept {
    if (!housing_share_wanted() || !hsh::is_login(opcode)) return false;
    try {
        const auto k = hsh::owner_of(packet);
        if (k) g_user2_key.store(*k);
        const uint32_t user2 = k ? *k : g_user2_key.load();
        const uint64_t now = GetTickCount64();
        std::lock_guard lk(g_mx);
        auto& at = g_given_at[opcode];
        if (!at || now - at > kGiveOnceMs) {
            at = now;
            give(opcode, user2, "its own set from the server, dropped");
        }
        g_dropped.fetch_add(1, std::memory_order_relaxed);
        return on();  // probe: user 2's own packet still goes
    } catch (...) {
        return false;
    }
}

std::string housing_share_describe() {
    return std::format("HousingShare (housing_share={}): host packets recorded {} | user 2's own dropped {} | the host's "
                       "queued {} (failed {}) | live changes forwarded {} | edits {} (parts queued {}, bad {}){}{} | none "
                       "from the host {} | keys rewritten {} | host 0x{:08X} user 2 0x{:08X}",
                       on() ? (g_live.load() ? "live" : "1") : "probe", g_recorded.load(), g_dropped.load(), g_queued.load(), g_queue_failed.load(),
                       g_forwarded.load(), g_edits_seen.load(), g_edits_queued.load(), g_edit_parts_bad.load(),
                       g_edits.load() ? "" : " [housing_share_edits=0]",
                       g_block_guest_spawn.load() ? std::format(" | user 2's spawn requests dropped {}", g_guest_spawn_blocked.load())
                                                  : std::string(),
                       g_no_host.load(), g_keys_rewritten.load(),
                       g_host_key.load(), g_user2_key.load());
}

}  // namespace cdmp::session2
