// S46 phases 1 and 2 (docs/research/s25/s46-tenderos.md): the pure logic of shop_trade_probe (shop_trade_probe_logic.h)
// and the eErrNo table (eerrno_names.h).
//
// SCOPE: offline logic only:
//  - R0's verdict (noop / swap / sentinel) and [would orphan] on synthetic lists and on the p2b reconstruction (the
//    twin 0x2DE erased after the host's 0x2A2 takes the host's entry); the native erase / append replayed;
//  - phase 2: R0's plan (shop_interaction_own_delete) on synthetic lists, across / within managers, the shop key only,
//    no own entry -> native (review F1: no sentinel), its swap applied to the list, the p2b reconstruction with R0
//    applied, the 0x30-byte block swap, cross_of (F6), the WindowsRWLock vtables (F5), and the PROPERTY over random
//    interleavings of entries / exits with two managers: R0 never leaves more leftover entries than native, and none;
//  - U2-c's decision (phase 2: routed only for user 2's own Troc, armed, with the NPC's shop entry in mgr2), P1's
//    verdict, the managers' "current" stage, the callers of 0x21F2780;
//  - the ledger (orphan after two checks, NPC gone, live != expected);
//  - the eErrNo value -> name table (zeros and duplicates left out) and the generated names;
//  - the in-place ring the hooks fill (with R0's fields) and the user2_shop_trade_route / shop_interaction_own_delete
//    keys.
// They do NOT test the hooks, the game's code nor the server.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <string_view>
#include <vector>

#include "mod/session2/eerrno_names.h"
#include "mod/session2/shop_trade_probe_logic.h"

using namespace cdmp::session2;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

constexpr uint32_t kShop = stp::kShopKey;
constexpr uint16_t kShopIdx = 77;  // any: the key is known
stp::Entry shop(uint64_t stage) { return stp::Entry{kShopIdx, true, kShop, stage}; }
stp::Entry talk(uint64_t stage) { return stp::Entry{12, true, 0x000000AA, stage}; }

void test_r0_synthetic() {
    // Only its own entry: noop.
    std::vector<stp::Entry> v = {talk(0), shop(0x2A2)};
    stp::R0Out r = stp::decide_r0(v.data(), 2, 2, kShop, kShopIdx, 0x2A2);
    CHECK(r.verdict == stp::R0::Noop && r.first == 1 && r.own == 1 && !r.orphan && r.complete);
    // Another stage's comes first: swap, and the native erase would take the other one (orphan).
    v = {shop(0x2A2), talk(0), shop(0x2DE)};
    r = stp::decide_r0(v.data(), 3, 3, kShop, kShopIdx, 0x2DE);
    CHECK(r.verdict == stp::R0::Swap && r.first == 0 && r.own == 2 && r.orphan && r.victim == 0x2A2);
    // No entry of this stage, another's with the key: sentinel and orphan.
    v = {shop(0x2DE)};
    r = stp::decide_r0(v.data(), 1, 1, kShop, kShopIdx, 0x2A2);
    CHECK(r.verdict == stp::R0::Sentinel && r.first == 0 && r.own == -1 && r.orphan && r.victim == 0x2DE);
    // No entry with the key at all: sentinel, nothing taken.
    v = {talk(0x10)};
    r = stp::decide_r0(v.data(), 1, 1, kShop, kShopIdx, 0x2A2);
    CHECK(r.verdict == stp::R0::Sentinel && r.first == -1 && !r.orphan);
    // Empty.
    r = stp::decide_r0(v.data(), 0, 0, kShop, kShopIdx, 0x2A2);
    CHECK(r.verdict == stp::R0::Sentinel && !r.orphan && r.complete);
    // A key not loaded: the index decides (the same row of the same table).
    v = {stp::Entry{kShopIdx, false, 0, 0x2A2}, stp::Entry{5, false, 0, 0x2DE}};
    r = stp::decide_r0(v.data(), 2, 2, kShop, kShopIdx, 0x2A2);
    CHECK(r.verdict == stp::R0::Noop && r.own == 0);
    r = stp::decide_r0(v.data(), 2, 2, kShop, 5, 0x2DE);
    CHECK(r.verdict == stp::R0::Noop && r.own == 1);
    // The vector longer than what was read and neither found: incomplete.
    v = {talk(1), talk(2)};
    r = stp::decide_r0(v.data(), 2, 40, kShop, kShopIdx, 0x2A2);
    CHECK(!r.complete);
    // Found within what was read: complete even if longer.
    v = {shop(0x2A2), talk(2)};
    r = stp::decide_r0(v.data(), 2, 40, kShop, kShopIdx, 0x2A2);
    CHECK(r.complete && r.verdict == stp::R0::Noop);
    for (int i = 0; i < static_cast<int>(stp::R0::Count); ++i) {
        CHECK(std::strlen(stp::r0_name(static_cast<stp::R0>(i))) > 0);
    }
}

// p2b (tenderos.md §2.3 b): the guest's 0x143, the host's 0x2A2, the drain erases 0x143, the twin 0x2DE, the twin's
// unload erases the host's entry. Replayed with the native operations, R0's verdict at each erase.
void test_r0_p2b() {
    std::vector<stp::Entry> v;
    stp::native_append(v, shop(0x143));
    stp::native_append(v, shop(0x2A2));
    // 3. The drain erases 0x143: its own is the first -> noop; [E2A2] remains.
    stp::R0Out r = stp::decide_r0(v.data(), static_cast<uint32_t>(v.size()), static_cast<uint32_t>(v.size()), kShop,
                                  kShopIdx, 0x143);
    CHECK(r.verdict == stp::R0::Noop && !r.orphan);
    CHECK(stp::native_erase(v, kShop, kShopIdx) == 0);
    CHECK(v.size() == 1 && v[0].stage == 0x2A2);
    // 4. The twin: [E2A2, E2DE].
    stp::native_append(v, shop(0x2DE));
    // 5. The twin's erase: its own is second -> swap; the native erase takes the HOST's.
    r = stp::decide_r0(v.data(), 2, 2, kShop, kShopIdx, 0x2DE);
    CHECK(r.verdict == stp::R0::Swap && r.orphan && r.victim == 0x2A2 && r.first == 0 && r.own == 1);
    CHECK(stp::native_erase(v, kShop, kShopIdx) == 0);
    CHECK(v.size() == 1 && v[0].stage == 0x2DE);  // 6. the orphan every 2671 takes (the five 3000s)
    // Later, the host's own exit finds no entry of 0x2A2: sentinel, and the native erase would take 0x2DE's.
    r = stp::decide_r0(v.data(), 1, 1, kShop, kShopIdx, 0x2A2);
    CHECK(r.verdict == stp::R0::Sentinel && r.orphan && r.victim == 0x2DE);
    // With R0 applied at step 5 (swap first, then the native erase): the host's entry stays.
    std::vector<stp::Entry> w = {shop(0x2A2), shop(0x2DE)};
    std::swap(w[0], w[1]);
    CHECK(stp::native_erase(w, kShop, kShopIdx) == 0);
    CHECK(w.size() == 1 && w[0].stage == 0x2A2);
    // The native erase moves the LAST entry into the hole (F1: no order kept).
    std::vector<stp::Entry> x = {talk(1), shop(0x10), talk(2), talk(3)};
    CHECK(stp::native_erase(x, kShop, kShopIdx) == 1);
    CHECK(x.size() == 3 && x[1].stage == 3 && x[2].stage == 2);
    CHECK(stp::native_erase(x, kShop, kShopIdx) == -1 && x.size() == 3);
}

// ---------------------------------------------------------------- phase 2: R0 active

using W = stp::Where;
constexpr W H = W::Host, U = W::User2;
// Each entry's manager by stage id (Host = side A, User2 = side B); a missing id reads "neither".
std::vector<W> wheres(const std::vector<stp::Entry>& v, const std::map<uint64_t, W>& m) {
    std::vector<W> w;
    for (const stp::Entry& e : v) {
        auto it = m.find(e.stage);
        w.push_back(it == m.end() ? W::Neither : it->second);
    }
    return w;
}
stp::R0Plan plan(const std::vector<stp::Entry>& v, uint64_t own, W own_w, const std::map<uint64_t, W>& m,
                 uint32_t total = 0) {
    const std::vector<W> w = wheres(v, m);
    const uint32_t n = static_cast<uint32_t>(v.size());
    return stp::plan_r0(v.data(), n, total ? total : n, kShop, kShopIdx, own, own_w, w.data());
}

void test_r0_plan() {
    const std::map<uint64_t, W> m = {{0x2A2, H}, {0x2A3, H}, {0x142, U}, {0x2DE, U}, {0x10, H}};
    // One scene per NPC: noop (its own first) or nothing to take.
    stp::R0Plan p = plan({talk(0x10), shop(0x2A2)}, 0x2A2, H, m);
    CHECK(p.act == stp::R0Act::Noop && p.why == stp::R0Why::OwnFirst && p.first == 1 && p.own == 1);
    p = plan({talk(0x10)}, 0x2A2, H, m);
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::NoMatch && p.first == -1);
    p = plan({}, 0x2A2, H, m);
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::NoMatch);
    // Across managers, own second: swap.
    p = plan({shop(0x2A2), talk(0x10), shop(0x2DE)}, 0x2DE, U, m);
    CHECK(p.act == stp::R0Act::Swap && p.why == stp::R0Why::Cross && p.first == 0 && p.own == 2 && p.cross == 0);
    // Across managers, no own entry: NATIVE (review F1: the entry an earlier exit left behind is the one to go).
    p = plan({shop(0x2DE)}, 0x2A2, H, m);
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::NoOwn && p.own == -1 && p.cross == 0);
    // Only the shop key (review F2): the same overlap on a talk key is native.
    {
        const std::vector<stp::Entry> t = {talk(0x2A2), talk(0x2DE)};
        const std::vector<W> w = wheres(t, m);
        p = stp::plan_r0(t.data(), 2, 2, 0x000000AA, 12, 0x2DE, U, w.data());
        CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::NotShopKey && p.cross == 0);
        p = stp::plan_r0(t.data(), 2, 2, 0x000000AA, 12, 0x2DE, U, w.data(), 0x000000AA);  // another act key: swap
        CHECK(p.act == stp::R0Act::Swap);
    }
    // The other manager's entry is not the first: still the plan (an entry of the other manager exists).
    p = plan({shop(0x2A3), shop(0x142), shop(0x2A2)}, 0x2A2, H, m);
    CHECK(p.act == stp::R0Act::Swap && p.first == 0 && p.own == 2 && p.cross == 1);
    // Overlap inside ONE manager: native (R0 never touches it).
    p = plan({shop(0x2A3), shop(0x2A2)}, 0x2A2, H, m);
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::SameManager);
    p = plan({shop(0x2A3)}, 0x2A2, H, m);
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::NoOwn);
    // An entry of a stage in neither manager (gone, or a third manager) is not "the other manager".
    p = plan({shop(0x999), shop(0x2A2)}, 0x2A2, H, m);
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::SameManager);
    // This stage's manager not proven: native; the counter's cross (F6) still sees the other manager's entry.
    p = plan({shop(0x2A2), shop(0x2DE)}, 0x2DE, W::Neither, m);
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::OwnUnknown && p.cross == 0);
    p = plan({shop(0x2A2), shop(0x2DE)}, 0x2DE, W::Both, m);
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::OwnUnknown);
    p = plan({shop(0x2A2), shop(0x2DE)}, 0x2DE, W::Unknown, m);
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::OwnUnknown);
    // Another stage's entry in both managers, or unreadable: native.
    std::map<uint64_t, W> amb = m;
    amb[0x2A2] = W::Both;
    p = plan({shop(0x2A2), shop(0x2DE)}, 0x2DE, U, amb);
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::Ambiguous);
    amb[0x2A2] = W::Unknown;
    p = plan({shop(0x2A2), shop(0x2DE)}, 0x2DE, U, amb);
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::Ambiguous);
    // The vector longer than what was read: native (a swap or a sentinel needs every entry).
    p = plan({shop(0x2A2), shop(0x2DE)}, 0x2DE, U, m, 40);
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::Incomplete);
    p = plan({talk(1)}, 0x2DE, U, m, 40);  // nothing found in what was read, more not read: not "no match"
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::Incomplete);
    // An entry whose key is not loaded: native (the erase compares keys).
    std::vector<stp::Entry> v = {shop(0x2A2), stp::Entry{9, false, 0, 0x10}, shop(0x2DE)};
    p = plan(v, 0x2DE, U, m);
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::KeyUnknown);
    // ... but the own-first case stays noop whatever the rest (nothing to change).
    v = {shop(0x2DE), stp::Entry{9, false, 0, 0x10}};
    p = plan(v, 0x2DE, U, m);
    CHECK(p.act == stp::R0Act::Noop);
    // Entries of other keys never count (a talk entry of the other manager).
    p = plan({talk(0x142), shop(0x2A3), shop(0x2A2)}, 0x2A2, H, m);
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::SameManager);
    // A second entry of this very stage before its own is its own (the first own one is taken): noop.
    p = plan({shop(0x2DE), shop(0x2A2), shop(0x2DE)}, 0x2DE, U, m);
    CHECK(p.act == stp::R0Act::Noop);
    // cross_of (F6): computed whatever stops the plan; a third manager's (neither) entry is never "another manager's".
    {
        const std::vector<stp::Entry> t = {shop(0x999), shop(0x2A3), talk(0x142), shop(0x142)};
        const std::vector<W> w = wheres(t, m);
        CHECK(stp::cross_of(t.data(), 4, kShop, kShopIdx, 0x2A2, H, w.data()) == 3);
        CHECK(stp::cross_of(t.data(), 4, kShop, kShopIdx, 0x2A2, W::Neither, w.data()) == 1);
        CHECK(stp::cross_of(t.data(), 4, kShop, kShopIdx, 0x142, U, w.data()) == 1);
        CHECK(stp::cross_of(t.data(), 2, kShop, kShopIdx, 0x2A2, H, w.data()) == -1);
    }
    // The lock's vtables (F5): the 11 pa::WindowsRWLock of build 2976.
    CHECK(stp::is_rw_lock_vtable(0x557CED0) && stp::is_rw_lock_vtable(0x5908A10) && !stp::is_rw_lock_vtable(0) &&
          !stp::is_rw_lock_vtable(0x5B17A58));
    for (int i = 0; i < static_cast<int>(stp::R0Act::Count); ++i) {
        CHECK(std::strlen(stp::r0_act_name(static_cast<stp::R0Act>(i))) > 0);
    }
    for (int i = 0; i < static_cast<int>(stp::R0Why::Count); ++i) {
        CHECK(std::strlen(stp::r0_why_name(static_cast<stp::R0Why>(i))) > 0);
    }
    // The guest side reads the same plan with A = X (Host) and B = M (User2): X's shop entry first, M's own second.
    p = plan({shop(0x2A2), shop(0x142)}, 0x142, U, m);
    CHECK(p.act == stp::R0Act::Swap);
    CHECK(std::string_view(stp::where_name(W::Host, true)) == "in X");
    CHECK(std::string_view(stp::where_name(W::User2, true)) == "in M");
    CHECK(std::string_view(stp::where_name(W::Host, false)) == "in mgrH");
}

// The plan applied to the list (what the vector holds after the exit) and the swap of two raw 0x30-byte blocks.
void test_r0_apply() {
    const std::map<uint64_t, W> m = {{0x2A2, H}, {0x2DE, U}, {0x142, U}, {0x2A3, H}};
    // Swap: the other manager's entry stays, this stage's own goes; nothing else moves but the native move-last.
    std::vector<stp::Entry> v = {shop(0x2A2), talk(1), shop(0x2DE), talk(2)};
    stp::R0Plan p = plan(v, 0x2DE, U, m);
    CHECK(p.act == stp::R0Act::Swap);
    CHECK(stp::apply_r0(v, p, kShop, kShopIdx) == 0);
    CHECK(v.size() == 3 && v[0].stage == 2 && v[1].stage == 1 && v[2].stage == 0x2A2);
    // No own entry: the native erase takes the first (review F1).
    v = {shop(0x2DE), talk(1)};
    p = plan(v, 0x2A2, H, m);
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::NoOwn);
    CHECK(stp::apply_r0(v, p, kShop, kShopIdx) == 0 && v.size() == 1 && v[0].stage == 1);
    // Native / noop: the native erase.
    v = {shop(0x2A3), shop(0x2A2)};
    p = plan(v, 0x2A2, H, m);
    CHECK(p.act == stp::R0Act::Native);
    CHECK(stp::apply_r0(v, p, kShop, kShopIdx) == 0 && v.size() == 1 && v[0].stage == 0x2A2);
    // A plan with indices out of range never swaps (the erase alone).
    v = {shop(0x2A2), shop(0x2DE)};
    stp::R0Plan bad;
    bad.act = stp::R0Act::Swap;
    bad.first = 0;
    bad.own = 7;
    CHECK(stp::apply_r0(v, bad, kShop, kShopIdx) == 0 && v.size() == 1 && v[0].stage == 0x2DE);
    // The raw swap: two blocks of six u64 exchanged whole (string handles +0x00 / +0x18, counted pointer +0x08, the
    // index +0x10, the stage id +0x20, +0x28..+0x2D), the rest of the vector untouched.
    uint64_t vec[3][6];
    for (int e = 0; e < 3; ++e) {
        for (int k = 0; k < 6; ++k) vec[e][k] = 0x1000ull * static_cast<uint64_t>(e + 1) + static_cast<uint64_t>(k);
    }
    stp::swap_blocks(vec[0], vec[2]);
    for (int k = 0; k < 6; ++k) {
        CHECK(vec[0][k] == 0x3000ull + static_cast<uint64_t>(k));
        CHECK(vec[2][k] == 0x1000ull + static_cast<uint64_t>(k));
        CHECK(vec[1][k] == 0x2000ull + static_cast<uint64_t>(k));
    }
    // Every handle still appears exactly once (no copy, no loss).
    std::set<uint64_t> all;
    for (auto& e : vec) {
        for (uint64_t x : e) all.insert(x);
    }
    CHECK(all.size() == 18);
    static_assert(stp::kEntrySize == 6 * 8 && stp::kEntry_Stage / 8 == 4, "the entry is six u64, the stage id the 5th");
}

// p2b with R0 applied at every erase: [E143, E2A2] -> 0x143's exit (own first: noop) -> [E2A2] -> the twin
// [E2A2, E2DE] -> the twin's exit: swap -> [E2A2] -> the host's 0x2A2 exit: noop -> []. Zero orphans, the host's entry
// alive while its scene is.
void test_r0_p2b_active() {
    std::map<uint64_t, W> m = {{0x143, U}, {0x2A2, H}};
    std::vector<stp::Entry> v;
    stp::native_append(v, shop(0x143));
    stp::native_append(v, shop(0x2A2));
    stp::R0Plan p = plan(v, 0x143, U, m);
    CHECK(p.act == stp::R0Act::Noop);
    stp::apply_r0(v, p, kShop, kShopIdx);
    CHECK(v.size() == 1 && v[0].stage == 0x2A2);
    m.erase(0x143);
    m[0x2DE] = U;  // the twin, in mgr2
    stp::native_append(v, shop(0x2DE));
    p = plan(v, 0x2DE, U, m);
    CHECK(p.act == stp::R0Act::Swap && p.first == 0 && p.own == 1);
    stp::apply_r0(v, p, kShop, kShopIdx);
    CHECK(v.size() == 1 && v[0].stage == 0x2A2);  // the host's entry stays (the five 3000s of p2b do not happen)
    m.erase(0x2DE);
    // No orphan: every remaining entry names a live stage.
    for (const stp::Entry& e : v) CHECK(m.count(e.stage) == 1);
    p = plan(v, 0x2A2, H, m);
    CHECK(p.act == stp::R0Act::Noop);
    stp::apply_r0(v, p, kShop, kShopIdx);
    CHECK(v.empty());
    // The other order of the relay: the host's scene ends first while the twin's entry is ahead of it.
    m = {{0x2A2, H}, {0x2DE, U}};
    v = {shop(0x2DE), shop(0x2A2)};
    p = plan(v, 0x2A2, H, m);
    CHECK(p.act == stp::R0Act::Swap);
    stp::apply_r0(v, p, kShop, kShopIdx);
    CHECK(v.size() == 1 && v[0].stage == 0x2DE);
    // The review's counterexample (F1): [S1, S2] of mgrH; S2 exits (one manager: native, takes S1's) -> [S2]; U of mgr2
    // adds -> [S2, U]; S1 exits with no own entry: native (a sentinel would keep S2's for good) -> [U]; U exits -> [].
    m = {{0x101, H}, {0x102, H}, {0x201, U}};
    v = {shop(0x101), shop(0x102)};
    p = plan(v, 0x102, H, m);
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::SameManager);
    stp::apply_r0(v, p, kShop, kShopIdx);
    stp::native_append(v, shop(0x201));
    p = plan(v, 0x101, H, m);
    CHECK(p.act == stp::R0Act::Native && p.why == stp::R0Why::NoOwn);
    stp::apply_r0(v, p, kShop, kShopIdx);
    CHECK(v.size() == 1 && v[0].stage == 0x201);
    p = plan(v, 0x201, U, m);
    stp::apply_r0(v, p, kShop, kShopIdx);
    CHECK(v.empty());
    m = {{0x2A2, H}, {0x2DE, U}};
    // The ledger with the plan applied expects what R0 left.
    stp::Ledger l;
    std::vector<stp::Entry> before = {shop(0x2A2), shop(0x2DE)};
    p = plan(before, 0x2DE, U, m);
    l.apply(0xB01002EC, 1, 2, before, false, shop(0x2DE), &p);
    CHECK(l.find(0xB01002EC)->expected.size() == 1 && l.find(0xB01002EC)->expected[0].stage == 0x2A2);
    l.apply(0xB01002EC, 1, 2, before, false, shop(0x2DE));  // not applied: the native erase
    CHECK(l.find(0xB01002EC)->expected.size() == 1 && l.find(0xB01002EC)->expected[0].stage == 0x2DE);
}

// The property (review F1, as its sim.cpp): random interleavings of entries (add) and exits of 1-3 stages of mgrH and
// 1-2 of mgr2 on one NPC, each stage adding once and exiting once. Native and R0 (swap only) replayed side by side.
// R0 never leaves more leftover entries than native, and leaves none; and it never has more steps whose first entry
// belongs to a stage that already exited (a 2671 then takes it: a 3000). With the stage dead at its exit too.
struct SeqStage {
    uint64_t id;
    W mgr;
    bool added = false, exited = false, alive = true;
};
struct SeqResult {
    int leftover[2] = {0, 0};  // runs ending with entries: [native, R0]
    int stale[2] = {0, 0};     // steps whose first entry is an exited stage's
    int swaps = 0;
};
SeqResult run_sequences(int runs, bool die_at_exit) {
    SeqResult res;
    std::mt19937 rng(1);
    for (int it = 0; it < runs; ++it) {
        const int nh = 1 + static_cast<int>(rng() % 3), nu = 1 + static_cast<int>(rng() % 2);
        for (int r0 = 0; r0 < 2; ++r0) {
            std::mt19937 r2(static_cast<uint32_t>(it));
            std::vector<SeqStage> st;
            for (int i = 0; i < nh; ++i) st.push_back({0x100u + static_cast<uint64_t>(i), H});
            for (int i = 0; i < nu; ++i) st.push_back({0x200u + static_cast<uint64_t>(i), U});
            std::vector<int> ev;
            for (int i = 0; i < static_cast<int>(st.size()); ++i) {
                ev.push_back(i);
                ev.push_back(i);
            }
            std::shuffle(ev.begin(), ev.end(), r2);
            std::vector<stp::Entry> v;
            for (const int e : ev) {
                SeqStage& s0 = st[static_cast<size_t>(e)];
                if (!s0.added) {
                    s0.added = true;
                    stp::native_append(v, shop(s0.id));
                } else {
                    if (r0) {
                        std::vector<W> w;
                        for (const stp::Entry& x : v) {
                            W m0 = W::Neither;
                            for (const SeqStage& t : st) {
                                if (t.id == x.stage && t.alive) m0 = t.mgr;
                            }
                            w.push_back(m0);
                        }
                        const W own = s0.alive ? s0.mgr : W::Neither;
                        const stp::R0Plan p = stp::plan_r0(v.data(), static_cast<uint32_t>(v.size()),
                                                           static_cast<uint32_t>(v.size()), kShop, kShopIdx, s0.id,
                                                           own, w.data());
                        if (p.act == stp::R0Act::Swap) ++res.swaps;
                        stp::apply_r0(v, p, kShop, kShopIdx);
                    } else {
                        stp::native_erase(v, kShop, kShopIdx);
                    }
                    s0.exited = true;
                    if (die_at_exit) s0.alive = false;
                }
                if (!v.empty()) {
                    for (const SeqStage& t : st) {
                        if (t.id == v[0].stage && t.exited) ++res.stale[r0];
                    }
                }
            }
            if (!v.empty()) ++res.leftover[r0];
        }
    }
    return res;
}

void test_r0_sequences() {
    for (int die = 0; die < 2; ++die) {
        const SeqResult r = run_sequences(20000, die != 0);
        std::printf("optd_shop_trade: R0 sequences (20000 runs, stage %s at its exit): leftover native %d, R0 %d | "
                    "steps with an exited stage's entry first: native %d, R0 %d | swaps %d\n",
                    die ? "dead" : "alive", r.leftover[0], r.leftover[1], r.stale[0], r.stale[1], r.swaps);
        CHECK(r.leftover[1] <= r.leftover[0]);
        CHECK(r.leftover[1] == 0);
        CHECK(r.stale[1] <= r.stale[0]);
        CHECK(die || r.swaps > 0);  // the property is not vacuous: R0 acts in these interleavings
    }
}

void test_u2c_and_p1() {
    stp::U2cIn in;
    in.server = true;
    in.user2 = true;
    in.subject = true;
    in.host_field = true;
    in.mgr2_ok = true;
    in.host = 0x1000;
    in.user2_mgr = 0x2000;
    in.got = 0x1000;
    in.armed = true;
    in.mgr2_state = 2;
    in.npc_read = true;
    in.npc_in_mgr2 = true;
    CHECK(stp::decide_u2c(in) == stp::U2c::WouldRoute);
    // mgr2's current stage is only logged (the check itself compares it with the NPC's stage).
    in.mgr2_current = 0;
    CHECK(stp::decide_u2c(in) == stp::U2c::WouldRoute);
    in.npc_in_mgr2 = false;
    CHECK(stp::decide_u2c(in) == stp::U2c::NpcNotInMgr2);
    in.npc_in_mgr2 = true;
    in.npc_read = false;
    CHECK(stp::decide_u2c(in) == stp::U2c::NpcUnreadable);
    in.npc_read = true;
    // Never without U-1 armed, the native state 2, or with user 2 leaving.
    in.armed = false;
    CHECK(stp::decide_u2c(in) == stp::U2c::NotArmed);
    in.armed = true;
    in.mgr2_state = 1;
    CHECK(stp::decide_u2c(in) == stp::U2c::NotArmed);
    in.mgr2_state = 2;
    in.logout = true;
    CHECK(stp::decide_u2c(in) == stp::U2c::NotArmed);
    in.logout = false;
    in.got = 0x3000;
    CHECK(stp::decide_u2c(in) == stp::U2c::OtherManager);
    in.got = 0x2000;
    CHECK(stp::decide_u2c(in) == stp::U2c::AlreadyUser2);
    in.got = 0x1000;
    in.mgr2_ok = false;
    CHECK(stp::decide_u2c(in) == stp::U2c::NoUser2Mgr);
    in.mgr2_ok = true;
    in.host_field = false;
    CHECK(stp::decide_u2c(in) == stp::U2c::NotHostField);
    in.host_field = true;
    in.subject = false;
    CHECK(stp::decide_u2c(in) == stp::U2c::NotSubject);
    in.subject = true;
    // Never for the host's request.
    in.user2 = false;
    CHECK(stp::decide_u2c(in) == stp::U2c::NotUser2);
    in.server = false;
    CHECK(stp::decide_u2c(in) == stp::U2c::NotServer);
    for (int i = 0; i < static_cast<int>(stp::U2c::Count); ++i) {
        CHECK(std::strlen(stp::u2c_name(static_cast<stp::U2c>(i))) > 0);
    }
    // The NPC's shop entries against the managers (Host = mgrH, User2 = mgr2).
    std::map<uint64_t, stp::Where> alive = {{0x2A2, stp::Where::Host}, {0x142, stp::Where::User2}};
    const auto al = [&](uint64_t id) {
        auto it = alive.find(id);
        return it == alive.end() ? stp::Where::Neither : it->second;
    };
    bool rd = false, m2 = false, mh = false;
    std::vector<stp::Entry> v = {talk(0x142), shop(0x142)};
    stp::u2c_npc(v.data(), 2, 2, al, rd, m2, mh);
    CHECK(rd && m2 && !mh);
    v = {shop(0x2A2)};
    stp::u2c_npc(v.data(), 1, 1, al, rd, m2, mh);
    CHECK(rd && !m2 && mh);
    v = {talk(0x142), shop(0)};  // a talk entry of mgr2 and a static shop entry: no scene of mgr2 links the shop
    stp::u2c_npc(v.data(), 2, 2, al, rd, m2, mh);
    CHECK(rd && !m2 && !mh);
    v = {shop(0x2A2), shop(0x142)};  // both: NOT routed (review F3: the NPC's current stage may be the host's)
    stp::u2c_npc(v.data(), 2, 2, al, rd, m2, mh);
    CHECK(rd && m2 && mh);
    {
        stp::U2cIn both = in;
        both.server = true;
        both.user2 = true;
        both.npc_in_mgr2 = true;
        both.npc_in_mgrh = true;
        CHECK(stp::decide_u2c(both) == stp::U2c::NpcAlsoInMgrH);
        both.npc_in_mgrh = false;
        CHECK(stp::decide_u2c(both) == stp::U2c::WouldRoute);
    }
    stp::u2c_npc(v.data(), 2, 40, al, rd, m2, mh);  // longer than what was read: not decided
    CHECK(!rd && !m2 && !mh);
    v = {stp::Entry{kShopIdx, false, 0, 0x142}};  // key not loaded: not counted
    stp::u2c_npc(v.data(), 1, 1, al, rd, m2, mh);
    CHECK(rd && !m2);
    // P1: 0x21F2C45 cmp [rsp+0x28],rbx: equal passes (also 0 == 0, as the game: no current and no link never reach
    // here, P1b refuses first).
    CHECK(stp::p1_verdict(0x2A2, 0x2A2) == stp::P1::Pass);
    CHECK(stp::p1_verdict(0x2A2, 0x142) == stp::P1::Mismatch);
    CHECK(stp::p1_verdict(0x2A2, 0) == stp::P1::NoLink);
    // The current stage: [mgr+0xA0] when its low 29 bits are set, else [mgr+0x90].
    CHECK(stp::current_of(0x2A2, 0x99) == 0x2A2);
    CHECK(stp::current_of(0, 0x99) == 0x99);
    CHECK(stp::current_of(0x20000000, 0x99) == 0x99);
    CHECK(stp::mgr_is(0x1000, 0x1000, 0x2000) == stp::MgrIs::Host);
    CHECK(stp::mgr_is(0x2000, 0x1000, 0x2000) == stp::MgrIs::User2);
    CHECK(stp::mgr_is(0x3000, 0x1000, 0x2000) == stp::MgrIs::Other);
    CHECK(stp::mgr_is(0, 0, 0) == stp::MgrIs::Other);
    // The callers (call rel32 + 5).
    const stp::Caller* buy = stp::caller_of(0x2BFFDC5 + 5);
    CHECK(buy && buy->server && std::string_view(buy->what).starts_with("2789"));
    const stp::Caller* sell = stp::caller_of(0x2C04B00 + 5);
    CHECK(sell && std::string_view(sell->what).starts_with("2366"));
    const stp::Caller* ui = stp::caller_of(0xA1D9DC + 5);
    CHECK(ui && !ui->server);
    CHECK(!stp::caller_of(0x2BFFDC5));
    CHECK(stp::where(true, true, true, false) == stp::Where::Host);
    CHECK(stp::where(true, false, true, true) == stp::Where::User2);
    CHECK(stp::where(true, true, true, true) == stp::Where::Both);
    CHECK(stp::where(true, false, true, false) == stp::Where::Neither);
    CHECK(stp::where(false, false, true, false) == stp::Where::Unknown);
}

void test_keys() {
    CHECK(stp::parse_route("").mode == stp::RouteMode::Off);
    CHECK(stp::parse_route("0").mode == stp::RouteMode::Off);
    CHECK(stp::parse_route("probe").mode == stp::RouteMode::Probe && !stp::parse_route("probe").bad);
    const stp::RouteKey one = stp::parse_route("1");
    CHECK(one.mode == stp::RouteMode::On && !one.bad);  // phase 2: applied
    const stp::RouteKey bad = stp::parse_route("yes");
    CHECK(bad.mode == stp::RouteMode::Off && bad.bad);
    CHECK(std::string_view(stp::mode_name(stp::RouteMode::Off)) == "0");
    CHECK(std::string_view(stp::mode_name(stp::RouteMode::Probe)) == "probe");
    CHECK(std::string_view(stp::mode_name(stp::RouteMode::On)) == "1");
}

void test_ledger() {
    stp::Ledger l;
    const uint32_t npc = 0xB01002EC;
    std::vector<stp::Entry> before = {shop(0x2A2)};
    l.apply(npc, 0x5000, 0x6000, before, true, shop(0x2DE));
    CHECK(l.size() == 1 && l.find(npc) && l.find(npc)->expected.size() == 2);
    std::map<uint64_t, stp::Where> alive = {{0x2A2, stp::Where::Host}, {0x2DE, stp::Where::User2}};
    std::vector<stp::Entry> live = {shop(0x2A2), shop(0x2DE)};
    bool present = true;
    std::vector<uint64_t> reported;
    const auto read = [&](const stp::LedgerNpc&, std::vector<stp::Entry>& out) {
        out = live;
        return present;
    };
    const auto al = [&](uint64_t s) {
        auto it = alive.find(s);
        return it == alive.end() ? stp::Where::Neither : it->second;
    };
    const auto rep = [&](const stp::LedgerNpc&, const stp::Entry& e, stp::Where) { reported.push_back(e.stage); };
    CHECK(l.check(read, al, rep) == 0);
    CHECK(reported.empty());
    // The twin's erase takes the host's entry (as the hook saw it): expected [E2DE]; then the twin's stage is deleted.
    l.apply(npc, 0x5000, 0x6000, live, false, shop(0x2DE));
    CHECK(l.find(npc)->expected.size() == 1 && l.find(npc)->expected[0].stage == 0x2DE);
    live = {shop(0x2DE)};
    alive.erase(0x2DE);
    CHECK(l.check(read, al, rep) == 0);
    CHECK(reported.empty());  // one check: not yet
    CHECK(l.check(read, al, rep) == 0);
    CHECK(reported.size() == 1 && reported[0] == 0x2DE && l.orphans() == 1);
    CHECK(l.check(read, al, rep) == 0);
    CHECK(reported.size() == 1);  // once
    // A stage that comes back (or an unknown reading) resets the count; unknown does not count as gone.
    alive[0x2DE] = stp::Where::Unknown;
    live = {shop(0x2DE), shop(0x300)};
    alive[0x300] = stp::Where::Unknown;
    CHECK(l.check(read, al, rep) == 1);  // live != expected (an append the hooks did not see)
    CHECK(l.check(read, al, rep) == 1);
    CHECK(reported.size() == 1);
    // Static entries (stage 0) are never orphans.
    live = {shop(0)};
    alive.clear();
    l.check(read, al, rep);
    l.check(read, al, rep);
    CHECK(reported.size() == 1);
    // The NPC gone: dropped.
    present = false;
    l.check(read, al, rep);
    CHECK(l.size() == 0 && l.gone() == 1);
    // The table is bounded.
    for (uint32_t i = 0; i < stp::kLedgerNpcs + 5; ++i) l.apply(0xB0000000 + i, 1, 2, {}, true, shop(1));
    CHECK(l.size() == stp::kLedgerNpcs && l.full() == 5);
    CHECK(stp::has_shop({talk(1), shop(2)}));
    CHECK(!stp::has_shop({talk(1), stp::Entry{kShopIdx, false, 0, 2}}));
}

void test_errors() {
    // The generated table: names, unique variables, the ones the probes print.
    static_assert(eerr::kNameCount == 1346, "the generated table of build 2976");
    std::set<uint32_t> rvas;
    std::map<std::string_view, uint32_t> by_name;
    for (const eerr::Name& n : eerr::kNames) {
        CHECK(std::string_view(n.name).starts_with("eErrNo"));
        CHECK(n.rva >= 0x6C00000 && n.rva < 0x6E00000);
        rvas.insert(n.rva);
        by_name[n.name] = n.rva;
    }
    CHECK(rvas.size() == eerr::kNameCount);
    CHECK(by_name["eErrNoInvalidStageInfo"] == 0x6CF7260);
    CHECK(by_name["eErrNoAlreadyPreemptedActor"] == 0x6CF7808);
    CHECK(by_name["eErrNoAlreadyPreemptingActor"] == 0x6CF780C);
    CHECK(by_name["eErrNoInvalidInteractionInfo"] == 0x6CF6CB0);
    CHECK(by_name["eErrNoInvalidDynamicInteractionData"] == 0x6CF6CB4);
    CHECK(by_name["eErrNoInvalidStageChartInteractionCondition"] == 0x6CF6D48);
    CHECK(by_name["eErrNoInvalidInteractionDistance"] == 0x6CF7B80);
    CHECK(by_name["eErrNoSequencerNotInteractableState"] == 0x6CF7E90);
    // value -> name: zeros and duplicated values left out.
    stp::ErrTable t;
    std::vector<uint32_t> values = {0x58C8345D, 0, 0x11, 0x22, 0x11, 0x33};
    t.build(values);
    CHECK(t.find(0x58C8345D) == 0);
    CHECK(t.find(0x22) == 3);
    CHECK(t.find(0x33) == 5);
    CHECK(t.find(0x11) == -1 && t.dup == 2);  // ambiguous
    CHECK(t.find(0) == -1 && t.zero == 1);
    CHECK(t.find(0x44) == -1);
    t.build(std::vector<uint32_t>{});
    CHECK(t.find(1) == -1 && t.rows.empty());
}

void test_ring() {
    stp::FillRing<stp::SiEvent, 4> r;
    for (uint32_t k = 0; k < 4; ++k) {
        CHECK(r.push([&](stp::SiEvent& e) {
            e.npc = 0xB0000000 + k;
            e.n = 3;
            e.read = 2;
            e.es[0] = 0x2A2;
            e.ek[0] = kShop;
            e.ei[0] = kShopIdx;
            e.ef[0] = 1;
            e.es[1] = 0x2DE;
            e.ek[1] = 0;
            e.ei[1] = 9;
            e.ef[1] = 0;
            e.guest = 1;
            e.r0_act = static_cast<uint8_t>(stp::R0Act::Swap);
            e.r0_why = static_cast<uint8_t>(stp::R0Why::Cross);
            e.r0_done = 1;
            e.first = 0;
            e.own = 1;
            e.lock_vt = 0x557CED0;
            e.cross = 0;
            e.ef[0] = static_cast<uint8_t>(1 | static_cast<uint8_t>(stp::Where::Host) << 1);
            e.ef[1] = static_cast<uint8_t>(0 | static_cast<uint8_t>(stp::Where::User2) << 1);
        }));
    }
    CHECK(!r.push([](stp::SiEvent&) {}) && r.dropped() == 1);
    stp::SiEvent out{};
    CHECK(r.pop(out) && out.npc == 0xB0000000 && out.read == 2);
    const std::vector<stp::Entry> l = out.list();
    CHECK(l.size() == 2 && l[0].stage == 0x2A2 && l[0].key_known && l[0].key == kShop && !l[1].key_known &&
          l[1].idx == 9);
    CHECK(out.guest == 1 && out.r0_done == 1 && out.where_of(0) == stp::Where::Host &&
          out.where_of(1) == stp::Where::User2);
    const stp::R0Plan pl = out.plan();
    CHECK(pl.act == stp::R0Act::Swap && pl.why == stp::R0Why::Cross && pl.first == 0 && pl.own == 1 && pl.cross == 0);
    for (int i = 0; i < 3; ++i) CHECK(r.pop(out));
    CHECK(!r.pop(out));
    CHECK(r.push([](stp::SiEvent& e) { e.read = 1000; }));  // a bad count never overflows the copy
    CHECK(r.pop(out) && out.read == stp::kMaxScan);
}

}  // namespace

// S46 live phase 1 (p8-2inst-s46-f1, vivo1.md), findings A and B.
void test_live_f1() {
    // B: the host's smithy (NPC 0xB010053D, scene 0x298 of mgrH). Its 2671 (17:13:16) took the entry of 0x298; the
    // 2720 then took the scene's entries down: the 2789 (17:13:37) met an EMPTY vector, and the game compared the NPC's
    // linked stage 0x298 (P1). The same for user 2 far, with its own scene of mgr2 (0x142 here):
    stp::U2cIn in;
    in.server = in.user2 = in.subject = in.host_field = in.mgr2_ok = in.armed = true;
    in.host = 0x1000;
    in.user2_mgr = 0x2000;
    in.got = 0x1000;
    in.mgr2_state = 2;
    // The dialogue: no entry at all (read whole: 0 of 0).
    std::map<uint64_t, stp::Where> alive = {{0x298, stp::Where::Host}, {0x142, stp::Where::User2}};
    const auto al = [&](uint64_t id) {
        auto it = alive.find(id);
        return it == alive.end() ? stp::Where::Neither : it->second;
    };
    stp::u2c_npc(nullptr, 0, 0, al, in.npc_read, in.npc_in_mgr2, in.npc_in_mgrh);
    CHECK(in.npc_read && !in.npc_in_mgr2 && !in.npc_in_mgrh);
    CHECK(stp::decide_u2c(in) == stp::U2c::NpcNotInMgr2);  // the phase 2 rule: never routed during the dialogue
    // With the stage user 2's accepted 2671 used: routed when it is mgr2's only.
    in.rec = true;
    in.rec_stage = 0x142;
    in.rec_where = al(0x142);
    CHECK(stp::decide_u2c(in) == stp::U2c::WouldRoute);
    in.rec_where = al(0x298);  // the host's scene (F3): kept
    CHECK(stp::decide_u2c(in) == stp::U2c::StageInMgrH);
    in.rec_where = stp::Where::Both;
    CHECK(stp::decide_u2c(in) == stp::U2c::StageInMgrH);
    in.rec_where = stp::Where::Neither;
    CHECK(stp::decide_u2c(in) == stp::U2c::StageNotLive);
    in.rec_where = stp::Where::Unknown;
    CHECK(stp::decide_u2c(in) == stp::U2c::StageNotLive);
    // F3 stays first: a live shop entry of mgrH on the NPC never routes, whatever the record.
    in.rec_where = stp::Where::User2;
    in.npc_in_mgrh = true;
    CHECK(stp::decide_u2c(in) == stp::U2c::NpcAlsoInMgrH);
    in.npc_in_mgrh = false;
    in.npc_read = false;
    CHECK(stp::decide_u2c(in) == stp::U2c::NpcUnreadable);
    in.npc_read = true;
    in.armed = false;
    CHECK(stp::decide_u2c(in) == stp::U2c::NotArmed);  // the record never opens the near case
    in.armed = true;
    // Without a record the entries decide, as in phase 2.
    in.rec = false;
    in.npc_in_mgr2 = true;
    CHECK(stp::decide_u2c(in) == stp::U2c::WouldRoute);

    // The book of accepted 2671s: per NPC, the last one; too old (> 120 s) or another NPC: none.
    stp::AcceptedBook<8> book;
    stp::Accepted a;
    CHECK(!book.get(0xB010053D, 10, a));
    book.put({0xB010053D, 0x298, 1, 1'000'000});
    CHECK(book.get(0xB010053D, 2'000'000, a) && a.stage == 0x298 && a.who == 1);
    CHECK(!book.get(0xB010053D, 1'000'000 + stp::kAcceptedMaxUs + 1, a));
    CHECK(book.get(0xB010053D, 1'000'000 + stp::kAcceptedMaxUs, a));
    static_assert(stp::kAcceptedMaxUs == 600'000'000, "review ab B2: 10 minutes");
    CHECK(!book.get(0xB010053E, 2'000'000, a));
    CHECK(!book.get(0, 2'000'000, a));
    CHECK(!book.get(0xB010053D, 999'999, a));  // a clock behind the record: none
    book.put({0xB010053D, 0x142, 2, 3'000'000});  // user 2's later 2671 on the same NPC replaces it
    CHECK(book.get(0xB010053D, 3'000'001, a) && a.stage == 0x142 && a.who == 2);
    // Every slot keeps its own NPC: 8 NPCs that land on 8 slots or overwrite each other, never a wrong answer.
    for (uint32_t k = 0; k < 64; ++k) book.put({0xB0100000 + k, 0x300 + k, 2, 5'000'000});
    for (uint32_t k = 0; k < 64; ++k) {
        stp::Accepted b;
        if (book.get(0xB0100000 + k, 5'000'001, b)) CHECK(b.npc == 0xB0100000 + k && b.stage == 0x300 + k);
    }

    // Review ab M1: "mgr2 only" must be PROVEN: a failed read of mgrH is never "not in mgrH".
    CHECK(stp::where_proven(true, false, true, true) == stp::Where::User2);
    CHECK(stp::where_proven(false, false, true, true) == stp::Where::Unknown);  // mgrH unreadable
    CHECK(stp::where_proven(true, false, false, false) == stp::Where::Unknown);  // mgr2 unreadable
    CHECK(stp::where_proven(true, true, true, false) == stp::Where::Host);
    CHECK(stp::where_proven(true, true, false, false) == stp::Where::Host);  // found in mgrH: F3 whatever mgr2 reads
    CHECK(stp::where_proven(true, true, true, true) == stp::Where::Both);
    CHECK(stp::where_proven(true, false, true, false) == stp::Where::Neither);
    CHECK(stp::where(false, false, true, true) == stp::Where::User2);  // the old rule (R0, the lines) is unchanged
    {
        stp::U2cIn r = in;
        r.npc_read = true;
        r.npc_in_mgr2 = r.npc_in_mgrh = false;
        r.rec = true;
        r.rec_stage = 0x142;
        r.rec_where = stp::where_proven(false, false, true, true);  // the record path with mgrH unreadable
        CHECK(stp::decide_u2c(r) == stp::U2c::StageNotLive);
        // The entries path: a shop entry whose managers are not proven makes the read undecided (NpcUnreadable).
        const auto unk = [](uint64_t id) { return id == 0x142 ? stp::Where::Unknown : stp::Where::User2; };
        std::vector<stp::Entry> ve = {shop(0x143), shop(0x142)};
        bool rd2 = true, a2 = true, b2 = true;
        stp::u2c_npc(ve.data(), 2, 2, unk, rd2, a2, b2);
        CHECK(!rd2 && !a2 && !b2);
        r.rec = false;
        r.npc_read = rd2;
        r.npc_in_mgr2 = a2;
        CHECK(stp::decide_u2c(r) == stp::U2c::NpcUnreadable);
    }
    // Review ab B2: the record ends with the dialogue (P2a appends that stage's shop entry again, or a 2743), 10 min max.
    {
        stp::AcceptedBook<8> bk;
        stp::Accepted x;
        bk.put({0xB010053D, 0x142, 2, 1'000'000});
        CHECK(bk.get(0xB010053D, 1'000'000 + 300'000'000, x));  // a 5-minute dialogue keeps it (was 120 s)
        CHECK(!bk.get(0xB010053D, 1'000'000 + stp::kAcceptedMaxUs + 1, x));
        CHECK(!bk.close(0xB010053D, 0x143));  // another stage's entry appended: not this dialogue
        CHECK(bk.get(0xB010053D, 2'000'000, x));
        CHECK(bk.close(0xB010053D, 0x142));  // its stage's entry back: closed
        CHECK(!bk.get(0xB010053D, 2'000'000, x) && !bk.close(0xB010053D, 0x142));
        bk.put({0xB010053D, 0x142, 2, 3'000'000});
        CHECK(bk.close(0xB010053D) && !bk.get(0xB010053D, 3'000'001, x));  // a 2743 of the NPC: any stage
        CHECK(!bk.close(0xB010053E) && !bk.close(0));
    }

    // A: P1 takes the manager P1b sealed in the same frame (rsp), never [rsp+0x38] (live: 0xFFFDFFF6C4541564, a
    // position written by 0x4F8220 over it).
    stp::P1Seal s{0x14F000, 0x53A9C1E4D00};
    CHECK(stp::p1_manager(s, 0x14F000) == 0x53A9C1E4D00);
    CHECK(stp::p1_manager(s, 0x14F010) == 0);  // another frame (a nested or a later call): unknown
    CHECK(stp::p1_manager(stp::P1Seal{}, 0) == 0);
}

int main() {
    test_r0_synthetic();
    test_r0_p2b();
    test_r0_plan();
    test_r0_apply();
    test_r0_p2b_active();
    test_r0_sequences();
    test_u2c_and_p1();
    test_live_f1();
    test_keys();
    test_ledger();
    test_errors();
    test_ring();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_shop_trade: all passed\n");
    return 0;
}
