#include "mod/session2/user2_shops.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <span>
#include <string>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"
#include "mod/session2/server.h"
#include "mod/session2/shop_probe.h"
#include "mod/session2/stage_lookup_logic.h"
#include "mod/session2/user2_funnels.h"
#include "mod/session2/user2_shops_logic.h"
#include "mod/session2/user2_stages.h"

namespace cdmp::session2 {
namespace {

namespace p = p8p;
namespace off = game::build_2976;
using u2sh::RouteOut;

uintptr_t g_base = 0;
std::atomic<bool> g_started{false}, g_sites_checked{false}, g_aborted{false};
std::atomic<uint8_t> g_mode{0};  // u2sh::Mode
std::atomic<uint8_t> g_abort_why{0};
std::atomic<bool> g_site_ok[u2sh::kSiteCount] = {};
// Review B1: the sites without which the shop cannot work (the branch, the game-play events, the trigger volumes).
constexpr uint16_t kCriticalOps[] = {2436, 2173, 2767};

// Route decisions per site (the last row: U2-b's server check), and the vf48 of those Trocs that are not the site's.
constexpr size_t kRows = u2sh::kSiteCount + 1;
std::atomic<uint64_t> g_out[kRows][static_cast<size_t>(RouteOut::Count)] = {};
u2s::CountTable<64> g_other_vf48;  // key: opcode << 32 | return RVA | bit 48: in a routed (mode 1) Troc
isl::IdBook<128> g_nested_seen;    // review M2: the first nested vf48 of each (opcode, return) is a WRN line
// User 2's shop scenes prepared by its own manager (shop_probe's P), and the other sequencers it prepared.
isl::IdBook<128> g_u2_shops;
std::atomic<uint64_t> g_u2_prepared{0}, g_u2_prepared_failed{0}, g_u2_not_funcnpc{0}, g_u2_name_unknown{0};
// Review M6 (log only): the host's shop scenes by resource hash (the last id of each), and user 2's shop scenes whose
// resource is also alive in the host's manager.
constexpr size_t kHashSlots = 64;
std::atomic<uint32_t> g_host_hash[kHashSlots] = {};
std::atomic<uint64_t> g_host_hash_id[kHashSlots] = {};
std::atomic<uint64_t> g_dup_resource{0};
// The shop interaction: the 2916 of user 2's own scene that opened the window (this arm), what followed it, the wait.
std::atomic<uint64_t> g_open_us{0}, g_followed_us{0}, g_hold_start_us{0}, g_last_srv_mine_u2_us{0};
std::atomic<uint32_t> g_open_npc{0};
std::atomic<bool> g_judged{true}, g_hold_logged{false}, g_waiting{false}, g_near_logged{false};
std::atomic<uint64_t> g_opens{0}, g_not_own_2916{0}, g_follows{0}, g_stucks{0}, g_held_s{0}, g_would_hold_s{0},
    g_too_near{0}, g_resets{0};
std::atomic<uint64_t> g_last_routed_us{0}, g_last_srv_routed_us{0}, g_last_host_srv_routed_us{0};
std::atomic<uint64_t> g_3000{0}, g_nak{0}, g_nak_scene{0}, g_host_refused{0};
std::atomic<uint64_t> g_deletes_mine{0};  // 2054 deferred deletes of user 2's scenes (routed or would)
std::atomic<uint64_t> g_t1_breach{0};      // S46 phase 3: shop scenes of user 2's manager off T1's list (abort)
std::atomic<uint64_t> g_t1_offlist_outside{0};  // review M2: the same, prepared outside a T1 pass (counted only)

enum Kind : uint16_t { kERoute, kESrv, kEPrep, kEOpen, kEOut, kEAbort, kEHold, kENested, kEReset, kKindCount };
p::EventRing<512> g_ring;
p::WindowLimiter g_lim[kKindCount] = {{24, 5'000'000}, {24, 5'000'000}, {32, 5'000'000}, {16, 5'000'000},
                                       {16, 5'000'000}, {8, 5'000'000},  {8, 5'000'000},  {32, 5'000'000},
                                       {8, 5'000'000}};
isl::IdBook<512> g_book;  // the first K lines per (kind, opcode, decision)
std::atomic<uint64_t> g_limited{0}, g_lost{0}, g_lines{0};
constexpr uint64_t kMaxLines = 3000;

u2sh::Mode mode_now() noexcept { return static_cast<u2sh::Mode>(g_mode.load(std::memory_order_relaxed)); }
DWORD WINAPI shops_thread(LPVOID);

// Guarded reads (game::fast_read: an access violation or in-page error returns false).
struct Rd {
    template <class T>
    bool rd(uintptr_t a, T& v) const noexcept {
        return a >= 0x10000 && game::fast_read(a, &v, sizeof v);
    }
};

void push(Kind k, p::Event& e, bool limited = true) noexcept {
    e.kind = k;
    e.qpc_us = p8_now_us();
    e.tid = GetCurrentThreadId();
    if (limited && !g_lim[k].take(e.qpc_us)) {
        g_limited.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!g_ring.push(e)) g_lost.fetch_add(1, std::memory_order_relaxed);
}

void clear_window() noexcept {
    g_open_us.store(0, std::memory_order_relaxed);
    g_followed_us.store(0, std::memory_order_relaxed);
    g_hold_start_us.store(0, std::memory_order_relaxed);
    g_judged.store(true, std::memory_order_release);
    g_waiting.store(false, std::memory_order_relaxed);
    g_hold_logged.store(false, std::memory_order_relaxed);
    g_near_logged.store(false, std::memory_order_relaxed);
}

void abort_now(u2sh::Abort why, uint64_t a = 0, uint64_t b = 0) noexcept {
    if (mode_now() != u2sh::Mode::On || g_aborted.exchange(true)) return;
    g_abort_why.store(static_cast<uint8_t>(why));
    clear_window();  // review A1: an abort ends the wait at once
    p::Event e;
    e.v[0] = static_cast<uint64_t>(why);
    e.v[1] = a;
    e.v[2] = b;
    push(kEAbort, e, false);  // one per run: never limited
}

bool critical_sites_ok() noexcept {
    for (uint16_t op : kCriticalOps) {
        const size_t i = u2sh::site_index(op);
        if (i >= u2sh::kSiteCount || !g_site_ok[i].load(std::memory_order_acquire)) return false;
    }
    return true;
}

// One decision for (row, id) between user 2's manager and the original answer. Review M4: only with U-1 armed and
// user 2's goodbye not asked.
RouteOut decide(size_t row, uint64_t id, uintptr_t field, uintptr_t original, uintptr_t& mgr2_out) noexcept {
    u2sh::RouteIn in;
    in.mode = mode_now();
    in.aborted = g_aborted.load(std::memory_order_relaxed);
    in.host_field = user2_stages_host_field(field);
    in.armed = in.host_field && user2_stages_armed();
    in.logout = in.armed && server_logout_requested();
    // Looked up even when not armed, so the counts say whose the id is (the host's scenes: "not in user 2's").
    const uintptr_t mgr2 = in.host_field ? user2_stages_user2_manager() : 0;
    in.mgr2_ok = mgr2 != 0;
    in.same_mgr = mgr2 && mgr2 == original;
    if (in.mgr2_ok && !in.same_mgr) {
        const Rd m;
        in.in_mgr2 = isl::find_stage(m, mgr2, id);
        if (in.in_mgr2.found) in.in_orig = isl::find_stage(m, original, id);
    }
    const RouteOut o = u2sh::decide_route(in);
    g_out[row][static_cast<size_t>(o)].fetch_add(1, std::memory_order_relaxed);
    mgr2_out = mgr2;
    return o;
}

// Review B2: the FIRST Req of its own scene after the 2916 is the follow-up (later ones never move it).
void note_followed(uint64_t now) noexcept {
    if (!g_open_us.load(std::memory_order_relaxed)) return;
    uint64_t expected = 0;
    g_followed_us.compare_exchange_strong(expected, now, std::memory_order_relaxed);
}

size_t hash_slot(uint32_t hash) noexcept { return (hash * 0x9E3779B9u >> 26) & (kHashSlots - 1); }

}  // namespace

void user2_shops_start(const std::string& route_mode, bool shop_probe) {
    const u2s::ModeKey k = u2s::parse_mode(route_mode);
    if (k.bad) log::warn("User2Shops: user2_shop_route={} is not 0, probe or 1: off", route_mode);
    if (k.mode == u2sh::Mode::Off) return;
    if (!shop_probe) {
        log::warn("User2Shops: user2_shop_route={} needs shop_probe=1 (its points P 0x288AAEF and S 0x2126A1F): off",
                  route_mode);
        return;
    }
    g_mode.store(static_cast<uint8_t>(k.mode));
    if (g_started.exchange(true)) return;
    CloseHandle(CreateThread(nullptr, 0, shops_thread, nullptr, 0, nullptr));
}

bool user2_shops_wanted() noexcept { return g_mode.load(std::memory_order_relaxed) != 0; }

bool user2_shops_may_prepare() noexcept {
    // Review B1: both halves and the critical call sites in; review M1: probe prepares too (and only logs the route).
    return user2_shops_wanted() && !g_aborted.load(std::memory_order_relaxed) &&
           g_sites_checked.load(std::memory_order_acquire) && critical_sites_ok() && user2_funnels_vf48_live() &&
           shop_probe_route_points_live();
}

bool user2_shops_route_live() noexcept { return mode_now() == u2sh::Mode::On && user2_shops_may_prepare(); }

bool user2_shops_waiting() noexcept { return g_waiting.load(std::memory_order_relaxed); }

uintptr_t user2_shops_vf48(uint16_t opcode, uint64_t seq_id, uint32_t ret_rva, uintptr_t field, uintptr_t original,
                           bool mine_troc, bool& mine) noexcept {
    mine = false;
    if (!user2_shops_wanted()) return 0;
    const size_t i = u2sh::site_index(opcode);
    if (i >= u2sh::kSiteCount) return 0;
    if (ret_rva != u2sh::kSites[i].ret_rva) {
        // The other vf48 of a Troc of user 2's own scene (nested: the branch 0x2891FB0 -> .xtls 0x102D2B70, the chart
        // 0x2936060...): the original (the host's manager) is kept; counted, and the first of each (opcode, return) is
        // a WRN line (review M2).
        if (mine_troc) {
            const bool routed = mode_now() == u2sh::Mode::On;
            g_other_vf48.add(static_cast<uint64_t>(opcode) << 32 | ret_rva | (routed ? 1ull << 48 : 0));
            if (g_nested_seen.take(static_cast<uint64_t>(opcode) << 32 | ret_rva, 1)) {
                p::Event e;
                e.v[0] = opcode;
                e.v[1] = ret_rva;
                e.v[2] = original;
                e.v[3] = routed;
                e.v[4] = field;
                push(kENested, e);
            }
        }
        return 0;
    }
    if (!g_site_ok[i].load(std::memory_order_acquire)) return 0;
    uintptr_t mgr2 = 0;
    const RouteOut o = decide(i, seq_id, field, original, mgr2);
    const uint64_t now = p8_now_us();
    if (o == RouteOut::InBoth) abort_now(u2sh::Abort::InBoth, seq_id, opcode);
    const bool deferred_delete = opcode == u2sh::kOpDeleteTimer;
    if (o == RouteOut::Routed || o == RouteOut::Would) {
        mine = true;
        g_last_routed_us.store(now, std::memory_order_relaxed);
        if (deferred_delete) {
            g_deletes_mine.fetch_add(1, std::memory_order_relaxed);
        } else {
            note_followed(now);  // a deferred delete is not the shop being driven
        }
    }
    // Lines: the first 4 per (opcode, decision), every one of a booked shop scene and every deferred delete (2054) of
    // one of user 2's scenes (up to the limiter).
    if (o != RouteOut::NotHostField &&
        ((deferred_delete && mine) || g_u2_shops.count_of(seq_id) ||
         g_book.take(static_cast<uint64_t>(opcode) << 8 | static_cast<uint8_t>(o), 4))) {
        p::Event e;
        e.v[0] = opcode;
        e.v[1] = seq_id;
        e.v[2] = static_cast<uint64_t>(o);
        e.v[3] = original;
        e.v[4] = mgr2;
        e.v[5] = ret_rva;
        push(kERoute, e);
    }
    return o == RouteOut::Routed ? mgr2 : 0;
}

uintptr_t user2_shops_server_check(uintptr_t field, uint64_t entry_stage_id, uintptr_t original,
                                   bool host_requester, bool user2_requester) noexcept {
    if (!user2_shops_wanted()) return 0;
    const uint64_t id = entry_stage_id;
    if ((id & 0x1FFFFFFF) == 0) return 0;
    uintptr_t mgr2 = 0;
    const RouteOut o = decide(u2sh::kSiteCount, id, field, original, mgr2);
    const uint64_t now = p8_now_us();
    if (o == RouteOut::InBoth) abort_now(u2sh::Abort::InBoth, id, 2671);
    if (o == RouteOut::Routed) {
        g_last_srv_routed_us.store(now, std::memory_order_relaxed);
        if (host_requester) g_last_host_srv_routed_us.store(now, std::memory_order_relaxed);
    }
    // Review B3: user 2 asked about an entry of its OWN scene (routed, or would in probe): its next 2916 is its shop.
    if (user2_requester && (o == RouteOut::Routed || o == RouteOut::Would)) {
        g_last_srv_mine_u2_us.store(now, std::memory_order_relaxed);
    }
    if (o != RouteOut::NotHostField && o != RouteOut::NotInUser2 &&
        g_book.take(0x2126A1Full << 16 | static_cast<uint64_t>(o) << 8 | (host_requester ? 1 : 0), 16)) {
        p::Event e;
        e.v[0] = id;
        e.v[1] = static_cast<uint64_t>(o);
        e.v[2] = original;
        e.v[3] = mgr2;
        e.v[4] = host_requester ? 1 : user2_requester ? 2 : 0;
        push(kESrv, e);
    }
    return o == RouteOut::Routed ? mgr2 : 0;
}

void user2_shops_host_2671(uint32_t result) noexcept {
    if (!user2_shops_wanted() || !result) return;
    if (u2sh::within(g_last_host_srv_routed_us.load(std::memory_order_relaxed), p8_now_us(), 1'000'000)) {
        g_host_refused.fetch_add(1, std::memory_order_relaxed);
        abort_now(u2sh::Abort::HostRefused, result, 0);
    }
}

void user2_shops_prepared(uintptr_t mgr, uint64_t seq_id, bool funcnpc, bool name_known, uint32_t hash,
                          uint32_t result) noexcept {
    if (!user2_shops_wanted() || !mgr) return;
    // Review M6: the host's shop scenes by resource hash (log only).
    if (funcnpc && !result && hash && mgr == user2_stages_host_manager()) {
        const size_t s = hash_slot(hash);
        g_host_hash_id[s].store(seq_id, std::memory_order_relaxed);
        g_host_hash[s].store(hash, std::memory_order_release);
        return;
    }
    if (mgr != user2_stages_user2_manager()) return;
    g_u2_prepared.fetch_add(1, std::memory_order_relaxed);
    if (result) g_u2_prepared_failed.fetch_add(1, std::memory_order_relaxed);
    if (!name_known) g_u2_name_unknown.fetch_add(1, std::memory_order_relaxed);
    uint64_t dup_host_id = 0;
    if (funcnpc && !result) {
        g_u2_shops.take(seq_id, 0);  // booked (count 1)
        const size_t s = hash_slot(hash);
        if (hash && g_host_hash[s].load(std::memory_order_acquire) == hash) {
            const uint64_t hid = g_host_hash_id[s].load(std::memory_order_relaxed);
            const uintptr_t hm = user2_stages_host_manager();
            const Rd m;
            if (hid && hm && isl::find_stage(m, hm, hid).found) {
                dup_host_id = hid;
                g_dup_resource.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    // S46 phase 3, T1's abort: with T1 active, a shop scene of user 2's manager whose playData hash is off the list,
    // prepared INSIDE an element T1 let through (review M2: the thread's mark; one prepared outside U-f's pass - a
    // tick between U-1's abort and its drain, an evaluation not armed - is only counted, never blamed on T1).
    const bool t1_off_list = u2sh::t1_breach(user2_stages_trade_list_active(), true, funcnpc, hash, result);
    if (t1_off_list && !user2_stages_in_trade_prepare()) g_t1_offlist_outside.fetch_add(1, std::memory_order_relaxed);
    if (t1_off_list && user2_stages_in_trade_prepare()) {
        user2_stages_trade_list_breach(seq_id, hash);  // T1 latched off: no more shop scenes for the run
        g_t1_breach.fetch_add(1, std::memory_order_relaxed);
        abort_now(u2sh::Abort::NotTradeList, seq_id, hash);  // and the route to probe (mode 1), one ERR line
    }
    // Review B5: only a name READ and known not to be a shop's, with user 2 armed in the host's field, is U-f broken.
    if (name_known && !funcnpc && user2_stages_armed() && user2_stages_user2_in_host_field()) {
        g_u2_not_funcnpc.fetch_add(1, std::memory_order_relaxed);
        abort_now(u2sh::Abort::NotFuncnpc, seq_id, 0);
    }
    p::Event e;
    e.v[0] = seq_id;
    e.v[1] = funcnpc ? 1 : name_known ? 0 : 2;
    e.v[2] = result;
    e.v[3] = mgr;
    e.v[4] = hash;
    e.v[5] = dup_host_id;
    push(kEPrep, e);
}

void user2_shops_on_out(uint16_t opcode, std::span<const uint8_t> payload, bool to_user2, bool pushed) noexcept {
    if (!user2_shops_wanted() || !to_user2) return;
    const uint64_t now = p8_now_us();
    if (opcode == u2sh::kOpDoStepOk || opcode == u2sh::kOpPreemptionFail) {
        const u2sh::StepAck a = u2sh::parse_step_ack(payload, opcode);  // S46: the 3000 has its own layout
        if (!a.ok || a.interaction != u2sh::kShopInteraction) return;
        bool own = false;
        if (opcode == u2sh::kOpDoStepOk) {
            if (!pushed) return;
            // Review B3: a shop of user 2's OWN scene only (a U2-b check of its own entry within 5 s before).
            own = u2sh::within(g_last_srv_mine_u2_us.load(std::memory_order_relaxed), now, u2sh::kFollowUs);
            if (own) {
                g_open_us.store(now, std::memory_order_relaxed);
                g_followed_us.store(0, std::memory_order_relaxed);
                g_open_npc.store(a.npc, std::memory_order_relaxed);
                g_judged.store(false, std::memory_order_release);
                g_opens.fetch_add(1, std::memory_order_relaxed);
            } else {
                g_not_own_2916.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            g_3000.fetch_add(1, std::memory_order_relaxed);
            if (u2sh::within(g_last_srv_routed_us.load(std::memory_order_relaxed), now, 2'000'000)) {
                abort_now(u2sh::Abort::Fail3000, a.npc, a.interaction);
            }
        }
        p::Event e;
        e.v[0] = opcode;
        e.v[1] = a.npc;
        e.v[2] = a.requester;
        e.v[3] = pushed;
        e.v[4] = own;
        push(kEOut, e);
    } else if (opcode == u2sh::kOpNak) {
        g_nak.fetch_add(1, std::memory_order_relaxed);
        // Review B4: only a Nak of a scene Req (its opcode at +7) right after a route is the criterion.
        const uint16_t req = u2sh::nak_req_opcode(payload);
        const bool scene = req && u2sh::site_of(req);
        if (scene) g_nak_scene.fetch_add(1, std::memory_order_relaxed);
        const bool after_route = scene && u2sh::within(g_last_routed_us.load(std::memory_order_relaxed), now, 1'000'000);
        if (after_route) abort_now(u2sh::Abort::Nak, req, payload.size());
        p::Event e;
        e.v[0] = opcode;
        e.v[1] = req;
        e.v[2] = payload.size();
        e.v[3] = after_route;
        for (size_t k = 0; k < payload.size() && k < 24; ++k) e.v[4 + k / 8] |= static_cast<uint64_t>(payload[k]) << (8 * (k % 8));
        push(kEOut, e);
    }
}

bool user2_shops_hold_drain(uint64_t now_us, uint32_t dist) noexcept {
    if (!user2_shops_wanted()) return false;
    u2sh::HoldIn in;
    in.open_us = g_open_us.load(std::memory_order_relaxed);
    in.hold_start_us = g_hold_start_us.load(std::memory_order_relaxed);
    in.now_us = now_us;
    in.dist = dist;
    const u2sh::Hold h = u2sh::hold_decision(in);
    const bool live = mode_now() == u2sh::Mode::On && !g_aborted.load(std::memory_order_relaxed);
    if (h != u2sh::Hold::Wait) {
        g_waiting.store(false, std::memory_order_relaxed);
        if (h == u2sh::Hold::TooNear && !g_near_logged.exchange(true)) {
            g_too_near.fetch_add(1, std::memory_order_relaxed);
            p::Event e;
            e.v[0] = 2;
            e.v[1] = g_open_npc.load(std::memory_order_relaxed);
            e.v[3] = dist;
            push(kEHold, e);
        }
        return false;
    }
    if (!in.hold_start_us) {
        uint64_t expected = 0;
        g_hold_start_us.compare_exchange_strong(expected, in.open_us, std::memory_order_relaxed);
    }
    (live ? g_held_s : g_would_hold_s).fetch_add(1, std::memory_order_relaxed);
    g_waiting.store(live, std::memory_order_relaxed);
    if (!g_hold_logged.exchange(true)) {
        p::Event e;
        e.v[0] = live;
        e.v[1] = g_open_npc.load(std::memory_order_relaxed);
        e.v[2] = (now_us - g_hold_start_us.load(std::memory_order_relaxed)) / 1000;
        e.v[3] = dist;
        push(kEHold, e);
    }
    return live;
}

void user2_shops_reset(const char* why) noexcept {
    if (!user2_shops_wanted()) return;
    const bool had = g_open_us.load(std::memory_order_relaxed) || g_hold_start_us.load(std::memory_order_relaxed);
    clear_window();
    g_resets.fetch_add(1, std::memory_order_relaxed);
    if (!had) return;
    p::Event e;
    for (size_t i = 0; why && why[i] && i < 32; ++i) e.v[i / 8] |= static_cast<uint64_t>(static_cast<uint8_t>(why[i])) << (8 * (i % 8));
    push(kEReset, e);
}

// ---------------------------------------------------------------- the module's thread

namespace {

std::string text_of(const uint64_t* q, size_t n) {
    std::string s;
    for (size_t i = 0; i < n * 8; ++i) {
        const auto c = static_cast<unsigned char>(q[i / 8] >> (8 * (i % 8)));
        if (!c) break;
        s += (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
    }
    return s;
}

std::string line_of(const p::Event& e) {
    const uint64_t* v = e.v;
    const std::string when = p8_when(e.qpc_us);
    switch (e.kind) {
        case kERoute: {
            const auto o = static_cast<RouteOut>(v[2]);
            const u2sh::Site* s = u2sh::site_of(static_cast<uint16_t>(v[0]));
            return std::format("User2Shops [{}] {} T{} | {} {} sequencer 0x{:X}{} | the original answer 0x{:X}, user "
                               "2's manager 0x{:X} | {}",
                               v[0] == u2sh::kOpDeleteTimer ? "deferred delete" : "route", when, e.tid, v[0],
                               s ? s->name : "?", v[1],
                               g_u2_shops.count_of(v[1]) ? " (a shop scene of user 2's own manager)" : "", v[3], v[4],
                               u2sh::route_out_name(o));
        }
        case kESrv:
            return std::format("User2Shops [server check] {} T{} | 0x2126920 on the server for {}: entry stage id 0x{:X} | "
                               "vf48 gave 0x{:X}, user 2's manager 0x{:X} | {}{}",
                               when, e.tid, v[4] == 1 ? "the HOST" : v[4] == 2 ? "user 2" : "another", v[0], v[2], v[3],
                               u2sh::route_out_name(static_cast<RouteOut>(v[1])),
                               static_cast<RouteOut>(v[1]) == RouteOut::Routed ? " (RAX := user 2's manager)" : "");
        case kEPrep:
            return std::format("User2Shops [prepared] {} T{} | user 2's manager 0x{:X} prepared sequencer 0x{:X} (resource "
                               "hash 0x{:08X}): {} | result 0x{:X}{}",
                               when, e.tid, v[3], v[0], static_cast<uint32_t>(v[4]),
                               v[1] == 1   ? "a cd_seq_funcnpc_* (its own shop scene: booked)"
                               : v[1] == 2 ? "its name could not be read (not judged)"
                                           : "NOT a shop scene",
                               v[2],
                               v[5] ? std::format(" | the host's manager has this resource alive too (its sequencer "
                                                  "0x{:X}): two shop scenes on one shopkeeper (log only, M6)",
                                                  v[5])
                                    : std::string());
        case kEOpen:
            return std::format("User2Shops [shop {}] {} T{} | the 2916 of the shop interaction to user 2 (NPC 0x{:08X}) "
                               "{} | {}",
                               v[0] == 1 ? "followed" : "NOT followed", when, e.tid, static_cast<uint32_t>(v[1]),
                               v[0] == 1 ? std::format("was followed by a Req of its own scene {:.1f} s later",
                                                       static_cast<double>(v[2]) / 1e6)
                                         : std::string("got no Req of user 2's own scene within 5 s"),
                               v[0] == 1 ? "its own scene drives the shop"
                               : v[3]    ? "mode 1: STUCK (S14's signature)"
                                         : "probe: expected (its Reqs are not routed)");
        case kEOut:
            if (v[0] == u2sh::kOpNak) {
                std::string hex;
                for (size_t i = 0; i < v[2] && i < 24; ++i) {
                    hex += std::format("{}{:02X}", i ? " " : "", static_cast<unsigned>((v[4 + i / 8] >> (8 * (i % 8))) & 0xFF));
                }
                return std::format("User2Shops [to user 2] {} T{} | Nak 1013 ({} bytes: {}) | the Req opcode at +7: {}{}",
                                   when, e.tid, v[2], hex, v[1],
                                   v[1] && u2sh::site_of(static_cast<uint16_t>(v[1])) ? " (a scene Req)" : "",
                                   v[3] ? " right after a routed scene Req" : "");
            }
            return std::format("User2Shops [to user 2] {} T{} | {} of the shop interaction 0x{:08X}: NPC 0x{:08X}, "
                               "requester 0x{:08X} | {}{}",
                               when, e.tid,
                               v[0] == u2sh::kOpDoStepOk ? "2916 InteractionDoStepSuccessAck"
                                                         : "3000 InteractionPreemptionStepFailAck",
                               u2sh::kShopInteraction, static_cast<uint32_t>(v[1]), static_cast<uint32_t>(v[2]),
                               v[3] ? "queued" : "NOT queued",
                               v[0] != u2sh::kOpDoStepOk ? std::string()
                               : v[4] ? std::string(" | a shop of its OWN scene: the window opens")
                                      : std::string(" | not after a check of its own entry: not its own scene (ignored)"));
        case kEAbort:
            return std::format("User2Shops [ABORT] {} T{} | {} (0x{:X}, 0x{:X}) | user2_shop_route behaves as probe for the "
                               "rest of this run (U-f no longer lets shop scenes through)",
                               when, e.tid, u2sh::abort_name(static_cast<u2sh::Abort>(v[0])), v[1], v[2]);
        case kEHold:
            if (v[0] == 2) {
                return std::format("User2Shops [drain NOT held] {} T{} | user 2 is in a shop (NPC 0x{:08X}) but the players "
                                   "are {} cells apart (<= {}): U-1's drain runs now",
                                   when, e.tid, static_cast<uint32_t>(v[1]),
                                   v[3] == UINT32_MAX ? std::string("?") : std::format("{}", v[3]), u2sh::kHoldMaxNearCells);
            }
            return std::format("User2Shops [drain {}] {} T{} | user 2 is in a shop (NPC 0x{:08X}, the wait's 2916 {} ms ago, "
                               "{} cells apart): U-1's distance drain {} for at most {} s after that 2916, while > {} cells, "
                               "once in this arm",
                               v[0] ? "held" : "would wait", when, e.tid, static_cast<uint32_t>(v[1]), v[2], v[3],
                               v[0] ? "waits" : "would wait (probe)", u2sh::kHoldUs / 1'000'000, u2sh::kHoldMaxNearCells);
        case kENested:
            return std::format("User2Shops [nested vf48] {} T{} | inside {} Troc {} of user 2's own scene, a vf48 returning to "
                               "exe+0x{:X} got the host's manager 0x{:X} (the original kept; first of this opcode and "
                               "return) | review M2: step B passes only without these",
                               when, e.tid, v[3] ? "the ROUTED" : "a would-be", v[0], static_cast<uint32_t>(v[1]), v[2]);
        case kEReset:
            return std::format("User2Shops [window reset] {} T{} | {}: the shop window and the drain wait start over", when,
                               e.tid, text_of(v, 4));
        default:
            return std::format("User2Shops: event {}?", e.kind);
    }
}

void judge(uint64_t now) {
    if (g_judged.load(std::memory_order_acquire)) return;
    const uint64_t open = g_open_us.load(std::memory_order_relaxed);
    const uint64_t fol = g_followed_us.load(std::memory_order_relaxed);
    const u2sh::Judge j = u2sh::judge_open(open, fol, now);
    if (j == u2sh::Judge::Pending) return;
    g_judged.store(true, std::memory_order_release);
    p::Event e;
    e.v[1] = g_open_npc.load(std::memory_order_relaxed);
    if (j == u2sh::Judge::Followed) {
        g_follows.fetch_add(1, std::memory_order_relaxed);
        e.v[0] = 1;
        e.v[2] = fol - open;
    } else {
        g_stucks.fetch_add(1, std::memory_order_relaxed);
        // The window only opens for its own scene (B3): with the route live, an unfollowed shop is the S14 stuck.
        const bool counts = user2_shops_route_live();
        e.v[3] = counts;
        if (counts) abort_now(u2sh::Abort::Stuck, e.v[1], 0);
    }
    push(kEOpen, e, false);
}

std::string outs_text(size_t row) {
    std::string s;
    for (size_t o = 0; o < static_cast<size_t>(RouteOut::Count); ++o) {
        if (const uint64_t n = g_out[row][o].load()) {
            s += std::format(" {} {};", u2sh::route_out_name(static_cast<RouteOut>(o)), n);
        }
    }
    return s.empty() ? std::string(" none") : s;
}

void summary() {
    std::string sites;
    for (size_t i = 0; i < u2sh::kSiteCount; ++i) {
        uint64_t n = 0;
        for (size_t o = 0; o < static_cast<size_t>(RouteOut::Count); ++o) n += g_out[i][o].load();
        if (n) sites += std::format(" [{}:{}]", u2sh::kSites[i].op, outs_text(i));
    }
    u2s::CountTable<64>::Row rows[64];
    const size_t n = g_other_vf48.rows(rows, 64);
    std::sort(rows, rows + n, [](const auto& a, const auto& b) { return a.count > b.count; });
    std::string other;
    for (size_t i = 0; i < n && i < 16; ++i) {
        other += std::format(" [{} exe+0x{:X}{}] x{};", (rows[i].key >> 32) & 0xFFFF, static_cast<uint32_t>(rows[i].key),
                             rows[i].key >> 48 ? " routed Troc" : " would-be Troc", rows[i].count);
    }
    log::info("User2Shops summary (user2_shop_route={}{}; may prepare {}, route live {}): scene Reqs by handler:{} | "
              "server checks (U2-b):{} | nested vf48 of user 2's scene Trocs (the host's manager kept):{} | user 2's "
              "manager prepared {} (failed {}; shop scenes booked {}; NOT shop scenes {}; name unreadable {}; shop "
              "resources also alive in the host's manager {}) | shop 2916 to user 2: its own {} (followed {}, not "
              "followed {}), not its own {} | deferred deletes (2054) of its scenes {} | S46 T1: shop scenes off the trade list {} (outside a T1 pass, counted only: {}) | drain held {} s (would {} s; too near, drained at once {}; window resets "
              "{}) | to user 2: 3000 {} Nak {} (of scene Reqs {}) | host's 2671 refused on a routed check {} | limited {} "
              "lost {}",
              u2s::mode_name(mode_now()),
              g_aborted.load() ? std::format(", ABORTED: {}", u2sh::abort_name(static_cast<u2sh::Abort>(g_abort_why.load())))
                               : std::string(),
              user2_shops_may_prepare() ? "yes" : "NO", user2_shops_route_live() ? "yes" : "no",
              sites.empty() ? std::string(" none") : sites, outs_text(u2sh::kSiteCount),
              other.empty() ? std::string(" none") : other, g_u2_prepared.load(), g_u2_prepared_failed.load(),
              g_u2_shops.used(), g_u2_not_funcnpc.load(), g_u2_name_unknown.load(), g_dup_resource.load(),
              g_opens.load(), g_follows.load(), g_stucks.load(), g_not_own_2916.load(), g_deletes_mine.load(), g_t1_breach.load(), g_t1_offlist_outside.load(),
              g_held_s.load(),
              g_would_hold_s.load(), g_too_near.load(), g_resets.load(), g_3000.load(), g_nak.load(),
              g_nak_scene.load(), g_host_refused.load(), g_limited.load(), g_lost.load());
}

void tick_once(uint64_t tick) noexcept {
    try {
        judge(p8_now_us());
        p::Event e;
        while (g_ring.pop(e)) {
            if (e.kind >= kKindCount || g_lines.fetch_add(1) >= kMaxLines) continue;
            if (e.kind == kEAbort) {
                log::error("{}", line_of(e));
            } else if ((e.kind == kEOpen && e.v[3]) || (e.kind == kEOpen && e.v[0] != 1) ||
                       (e.kind == kESrv && static_cast<RouteOut>(e.v[1]) == RouteOut::InBoth) ||
                       (e.kind == kEPrep && (e.v[1] == 0 || e.v[5])) || (e.kind == kENested && e.v[3]) ||
                       (e.kind == kEHold && e.v[0] == 2)) {
                log::warn("{}", line_of(e));
            } else {
                log::info("{}", line_of(e));
            }
        }
        if (tick % 60 == 0) summary();
    } catch (...) {
    }
}

DWORD WINAPI shops_thread(LPVOID) {
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    // The sites' `call [rax+0x48]` (3 bytes before each return), once the game's code is in place.
    for (int i = 0; i < 1200; ++i) {
        uintptr_t world = 0;
        if (game::safe_read(g_base + off::kWorldSystemSlot, &world, sizeof world) && world) break;
        Sleep(500);
    }
    std::string bad;
    size_t ok = 0;
    for (size_t i = 0; i < u2sh::kSiteCount; ++i) {
        uint8_t b[3] = {};
        const bool good = game::safe_read(g_base + u2sh::kSites[i].ret_rva - 3, b, sizeof b) &&
                          std::memcmp(b, u2sh::kCallVf48, sizeof b) == 0;
        g_site_ok[i].store(good, std::memory_order_release);
        if (good) {
            ++ok;
        } else {
            bad += std::format(" {} (exe+0x{:X});", u2sh::kSites[i].op, u2sh::kSites[i].ret_rva - 3);
        }
    }
    g_sites_checked.store(true, std::memory_order_release);
    log::info("User2Shops (user2_shop_route={}): U2-a at the vf48 call of {} of {} scene handlers{}{} | U2-b at "
              "shop_probe's 0x2126A1F | the drain wait (120 s after a shop 2916 of its own scene, > {} cells, once per "
              "arm) | user2_funnels' vf48 / subject hooks and shop_probe's S / P right now: {} / {} (checked again on "
              "every use) | {}",
              u2s::mode_name(mode_now()), ok, u2sh::kSiteCount,
              bad.empty() ? std::string() : " (NOT build 2976's call, left out:" + bad + ")",
              critical_sites_ok() ? "" : " | a CRITICAL site (2436 / 2173 / 2767) is out: no shop scene is prepared",
              u2sh::kHoldMaxNearCells, user2_funnels_vf48_live() ? "live" : "not yet live",
              shop_probe_route_points_live() ? "in" : "NOT in",
              mode_now() == u2sh::Mode::On ? "mode 1: user 2's own shop scenes get their Reqs"
                                            : "probe: shop scenes may be prepared, the route is only logged ('would')");
    for (uint64_t tick = 1;; ++tick) {
        Sleep(1000);
        tick_once(tick);
    }
}

}  // namespace

}  // namespace cdmp::session2
