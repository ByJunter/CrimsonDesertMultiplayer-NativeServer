#pragma once
// S46 phases 1 and 2 (docs/research/s25/s46-tenderos.md; research: s46-datos/nativo.md, tenderos.md,
// revision-diseno.md): the pure logic of shop_trade_probe ([session2] shop_trade_probe = 0 | 1, host, read only;
// user2_shop_trade_route = 0 | probe | 1, host; shop_interaction_own_delete = 0 | probe | 1, host and guest). No game
// access, no allocation in what the hooks use: the tests run it. Build 1.0.0.2976.
//
//   R0 (phase 1: verdict only; phase 2: plan_r0, applied with shop_interaction_own_delete=1 only across managers):
//      the native erase of a dynamic interaction entry (.xtls 0xE404A10, called
//      from 0x2369360's exit path, 0x2369867) takes the FIRST entry of the NPC whose key is the function's and moves
//      the last entry into its hole: it never looks at the stage id. The stage whose SetInteraction ends may then take
//      ANOTHER stage's entry and leave its own behind (the p2b twin: the host's 3000s). R0's verdict per exit:
//        noop      the first entry with the key is this stage's own
//        swap      its own entry exists but another stage's comes first (R0 would swap the two 0x30-byte blocks)
//        sentinel  no entry of this stage (R0 would give the key a sentinel so that nothing is erased)
//      orphan: the native erase takes an entry of ANOTHER stage (swap, or sentinel with a match).
//   U2-c: at 0x21F28A1, the return of 0x21F2780's field.vf48 (the shop check of 2789/2366/...), the manager user 2's
//      request is checked against (phase 2, user2_shop_trade_route=1: mgr2 for user 2's own, armed, linked NPC).
//   The ledger: the live entries of the shop NPCs, and whether each one's stage is still alive in a manager.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace cdmp::session2::stp {

// ---------------------------------------------------------------- the keys

// user2_shop_trade_route (U2-c) and shop_interaction_own_delete (R0): 0 | probe | 1 (phase 2: "1" applies); anything
// else: off (logged).
enum class RouteMode : uint8_t { Off, Probe, On };
struct RouteKey {
    RouteMode mode = RouteMode::Off;
    bool bad = false;
};
inline RouteKey parse_route(std::string_view v) {
    RouteKey k;
    if (v.empty() || v == "0") return k;
    if (v == "probe") {
        k.mode = RouteMode::Probe;
        return k;
    }
    if (v == "1") {
        k.mode = RouteMode::On;
        return k;
    }
    k.bad = true;
    return k;
}
inline const char* mode_name(RouteMode m) {
    switch (m) {
        case RouteMode::Probe: return "probe";
        case RouteMode::On: return "1";
        default: return "0";
    }
}

// ---------------------------------------------------------------- the dynamic interaction entries [V-x]

// ServerInteractionActorComponent = [[actor+0x68]+0x98]: vector of 0x30-byte entries at +0x88, u32 count at +0x90.
// Entry: +0x10 u16 InteractionInfo index, +0x20 u64 stage id (& 0x1FFFFFFF; 0 = static).
inline constexpr uint32_t kComp_Vec = 0x88, kComp_Count = 0x90, kEntrySize = 0x30, kEntry_Idx = 0x10,
                          kEntry_Stage = 0x20;
inline constexpr uint64_t kStageMask = 0x1FFFFFFF;
inline constexpr uint32_t kShopKey = 0x000F4321;  // the shop interaction (S11, S13 smithy, S14 butcher)
inline constexpr uint32_t kMaxScan = 32;          // entries a hook reads at most (a shop NPC has a handful)
// The InteractionInfo table (0x87E65F0, the thunk 0x3888B0): [exe+0x6D69AB8] -> tbl; u32 count [tbl+8]; the
// pointers [tbl+0x58] (8 bytes per u16 index; null = not loaded yet: the game loads it lazily); key = u32 [info].
inline constexpr uint32_t kInfoTableSlot = 0x6D69AB8, kTbl_Count = 0x8, kTbl_Array = 0x58;

// No default member initializers: a hook's stack array of these is NOT zeroed (a zeroing of that size may become a
// CRT memset call, which may use AVX and clobber the upper halves of the game's YMM registers inside a mid hook).
// Value-initialize it (Entry{}) everywhere else.
struct Entry {
    uint16_t idx;
    bool key_known;  // the InteractionInfo was loaded: key is its u32 [info]
    uint32_t key;
    uint64_t stage;
};

// Does the entry match the erase's key? The native erase compares keys ([info] of each entry's index); when an
// entry's info is not loaded the index is compared instead (the same row of the same table).
inline bool key_match(const Entry& e, uint32_t key, uint16_t idx) { return e.key_known ? e.key == key : e.idx == idx; }

enum class R0 : uint8_t { Noop, Swap, Sentinel, Count };
inline const char* r0_name(R0 v) {
    switch (v) {
        case R0::Noop: return "would noop";
        case R0::Swap: return "would swap";
        case R0::Sentinel: return "would sentinel";
        default: return "?";
    }
}
struct R0Out {
    R0 verdict = R0::Sentinel;
    int first = -1;           // i_first: the first entry with the key (what the native erase takes), -1 none
    int own = -1;             // i_own: the first entry with the key AND this stage's id, -1 none
    bool orphan = false;      // the native erase takes ANOTHER stage's entry
    uint64_t victim = 0;      // that entry's stage id (orphan only)
    bool complete = true;     // false: the vector had more entries than were read and neither was found
};
// n: the entries read (e[0..n)); total: the vector's real count.
inline R0Out decide_r0(const Entry* e, uint32_t n, uint32_t total, uint32_t key, uint16_t idx, uint64_t own_stage) {
    R0Out r;
    for (uint32_t i = 0; i < n; ++i) {
        if (!key_match(e[i], key, idx)) continue;
        if (r.first < 0) r.first = static_cast<int>(i);
        if (e[i].stage == own_stage) {
            r.own = static_cast<int>(i);
            break;
        }
    }
    if (r.own < 0 && total > n) r.complete = false;
    if (r.own >= 0) {
        r.verdict = r.own == r.first ? R0::Noop : R0::Swap;
    } else {
        r.verdict = R0::Sentinel;
    }
    if (r.first >= 0 && r.first != r.own) {
        r.orphan = true;
        r.victim = e[r.first].stage;
    }
    return r;
}

// ---------------------------------------------------------------- whose stage

// Which manager has a stage id now (isl::find_stage on the host's and user 2's ServerSequencerStageManager).
enum class Where : uint8_t { Unknown, Host, User2, Both, Neither, Count };
inline Where where(bool h_ok, bool h_found, bool u_ok, bool u_found) {
    if (h_found && u_found) return Where::Both;
    if (h_found) return Where::Host;
    if (u_found) return Where::User2;
    return h_ok && u_ok ? Where::Neither : Where::Unknown;
}
// U2-c (review ab M1): User2 only when mgrH was READ and has not the stage, and mgr2 was read and has it; Host / Both
// only when found in mgrH; Neither only with both read; anything a failed read leaves open is Unknown (never routed).
inline Where where_proven(bool h_ok, bool h_found, bool u_ok, bool u_found) {
    if (h_ok && h_found) return u_ok && u_found ? Where::Both : Where::Host;
    if (!h_ok || !u_ok) return Where::Unknown;
    return u_found ? Where::User2 : Where::Neither;
}
inline const char* where_name(Where w) {
    switch (w) {
        case Where::Host: return "in mgrH";
        case Where::User2: return "in mgr2";
        case Where::Both: return "in BOTH";
        case Where::Neither: return "in neither";
        default: return "unknown (a read failed or a manager is not known)";
    }
}
// The same on either side: host A = mgrH, B = mgr2; guest A = X (the host character's copy), B = M (its own).
inline const char* where_name(Where w, bool guest) {
    if (!guest) return where_name(w);
    switch (w) {
        case Where::Host: return "in X";
        case Where::User2: return "in M";
        case Where::Both: return "in BOTH X and M";
        case Where::Neither: return "in neither X nor M";
        default: return "unknown (a read failed or M / X not known)";
    }
}

// The manager a pointer is (P1, U2-c).
enum class MgrIs : uint8_t { Other, Host, User2 };
inline MgrIs mgr_is(uintptr_t m, uintptr_t host, uintptr_t user2) {
    if (m && m == user2) return MgrIs::User2;
    if (m && m == host) return MgrIs::Host;
    return MgrIs::Other;
}
inline const char* mgr_name(MgrIs m) {
    switch (m) {
        case MgrIs::Host: return "mgrH";
        case MgrIs::User2: return "mgr2";
        default: return "another manager";
    }
}

// A manager's "current" stage as 0x21F2780 reads it: [mgr+0xA0] when its low 29 bits are not 0, else [mgr+0x90].
inline constexpr uint32_t kMgr_Current = 0xA0, kMgr_Current2 = 0x90;
inline uint64_t current_of(uint64_t a0, uint64_t m90) { return (a0 & kStageMask) ? a0 : m90; }

// ---------------------------------------------------------------- R0 active (phase 2): shop_interaction_own_delete
//
// At P2 (0x2369862, the actor's exclusive lock held: 0x2369853 -> 0x2369879), right before the native erase of the key:
//   i_own == i_first          nothing (the native erase takes this stage's own entry)
//   i_own exists, != i_first   the two 0x30-byte blocks are swapped (a move, as the native erase's own move of the last
//                              entry into the hole): the native erase then takes this stage's own entry
//   no i_own                   NATIVE (review F1 of phase 2): no own entry usually means an earlier exit took it; the
//                              entry that exit left behind is the one to go now, and the native erase takes the first.
//                              A sentinel (erase nothing) left it orphaned for good (729 leftovers in 20 000 random
//                              interleavings against 0; tests: test_r0_sequences).
// ONLY for the shop key 0x000F4321 (review F2: the ledger watches the shop NPCs), and ONLY when a key-matching entry
// belongs to a stage of the OTHER manager of this side (host: mgrH / mgr2; guest: X / M) than this stage's: with one
// scene per NPC (or overlaps inside one manager) R0 never touches anything (native).
// Where: Host = side A (host mgrH, guest X), User2 = side B (host mgr2, guest M).
enum class R0Act : uint8_t { Native, Noop, Swap, Count };
enum class R0Why : uint8_t {
    NoMatch,           // no entry with the key: the native erase takes nothing
    OwnFirst,          // the first entry with the key is this stage's own: noop
    NoOwn,             // this stage has no entry with the key: native (F1; the erase takes the first, as natively)
    Incomplete,        // the vector is longer than what was read: native
    KeyUnknown,        // an entry's InteractionInfo is not loaded (the erase compares keys): native
    OwnUnknown,        // this stage's manager is not proven (not found, in both, a read failed): native
    Ambiguous,         // a key-matching entry of another stage is in both managers or could not be looked up: native
    SameManager,       // no key-matching entry of the OTHER manager: native (an overlap inside one manager is native)
    NotShopKey,        // the erase's key is not the shop's: native (F2)
    Cross,             // acts: swap
    Count
};
inline const char* r0_act_name(R0Act a) {
    switch (a) {
        case R0Act::Native: return "native";
        case R0Act::Noop: return "noop";
        case R0Act::Swap: return "swap";
        default: return "?";
    }
}
inline const char* r0_why_name(R0Why w) {
    switch (w) {
        case R0Why::NoMatch: return "no entry with the key";
        case R0Why::OwnFirst: return "its own entry is the first";
        case R0Why::NoOwn: return "no own entry (the native erase takes the first)";
        case R0Why::Incomplete: return "the vector is longer than what was read";
        case R0Why::KeyUnknown: return "an entry's key is not loaded";
        case R0Why::OwnUnknown: return "this stage's manager is not proven";
        case R0Why::Ambiguous: return "another stage's entry is in both managers or unreadable";
        case R0Why::SameManager: return "no entry of the other manager (one manager: native)";
        case R0Why::NotShopKey: return "not the shop key (R0 acts on 0x000F4321 only)";
        case R0Why::Cross: return "an entry of the other manager";
        default: return "?";
    }
}
struct R0Plan {
    R0Act act = R0Act::Native;
    R0Why why = R0Why::NoMatch;
    int first = -1, own = -1;
    int cross = -1;  // the first key-matching entry of another stage proven in a manager this stage is not proven in
};
// Review F5: the lock object [actor+8] is a pa::WindowsRWLock<...> (RTTI, build 2976: 11 instances sharing vf18
// 0x1371AD0 = AcquireSRWLockShared with dl != 0 / exclusive, recursive, with dl = 0). Their vtables (RVA):
inline constexpr uint32_t kRwLockVtables[11] = {0x557CED0, 0x5585728, 0x5586C58, 0x571A228, 0x5725DD0, 0x57A30B8,
                                                0x57B6D88, 0x57D8E38, 0x58F9590, 0x58F9880, 0x5908A10};
inline bool is_rw_lock_vtable(uint32_t rva) {
    for (uint32_t v : kRwLockVtables) {
        if (v == rva) return true;
    }
    return false;
}
inline bool proven(Where w) { return w == Where::Host || w == Where::User2; }
// Review F6: computed on its own, whatever stops the plan: the first key-matching entry of ANOTHER stage whose manager
// is proven and is not this stage's (own_where may itself be unproven: the H1 case, a stage already out of its manager
// when its function exits). -1: none.
inline int cross_of(const Entry* e, uint32_t n, uint32_t key, uint16_t idx, uint64_t own_stage, Where own_where,
                    const Where* where) {
    for (uint32_t i = 0; i < n; ++i) {
        if (!key_match(e[i], key, idx) || e[i].stage == own_stage) continue;
        if (proven(where[i]) && where[i] != own_where) return static_cast<int>(i);
    }
    return -1;
}
// e[0..n): the entries read (total: the vector's count); key / idx: the erase's; own_stage: this stage's id; own_where:
// its manager; where[i]: each entry's (read for the key-matching ones; the others are not looked at). act_key: the only
// key R0 acts on (F2).
inline R0Plan plan_r0(const Entry* e, uint32_t n, uint32_t total, uint32_t key, uint16_t idx, uint64_t own_stage,
                      Where own_where, const Where* where, uint32_t act_key = kShopKey) {
    R0Plan p;
    const R0Out r = decide_r0(e, n, total, key, idx, own_stage);
    p.first = r.first;
    p.own = r.own;
    p.cross = cross_of(e, n, key, idx, own_stage, own_where, where);
    if (r.first < 0 && n == total) return p;  // NoMatch: nothing to take
    if (r.verdict == R0::Noop) {
        p.act = R0Act::Noop;
        p.why = R0Why::OwnFirst;
        return p;
    }
    if (n != total) {
        p.why = R0Why::Incomplete;
        return p;
    }
    if (r.own < 0) {
        p.why = R0Why::NoOwn;
        return p;
    }
    for (uint32_t i = 0; i < n; ++i) {
        if (!e[i].key_known) {
            p.why = R0Why::KeyUnknown;
            return p;
        }
    }
    if (!proven(own_where)) {
        p.why = R0Why::OwnUnknown;
        return p;
    }
    bool other = false;
    for (uint32_t i = 0; i < n; ++i) {
        if (static_cast<int>(i) == r.own || !key_match(e[i], key, idx)) continue;
        if (e[i].stage == own_stage) continue;  // a second entry of this very stage
        if (where[i] == Where::Both || where[i] == Where::Unknown) {
            p.why = R0Why::Ambiguous;
            return p;
        }
        if (proven(where[i]) && where[i] != own_where) other = true;
    }
    if (!other) {
        p.why = R0Why::SameManager;
        return p;
    }
    if (key != act_key) {
        p.why = R0Why::NotShopKey;
        return p;
    }
    p.act = R0Act::Swap;
    p.why = R0Why::Cross;
    return p;
}

// The native vector operations, replayed (the ledger's expectation and the tests' reconstruction of p2b).
// 0xE404A10: erase the first entry with the key, the last one moved into its hole. Returns the erased index or -1.
template <class Vec>
int native_erase(Vec& v, uint32_t key, uint16_t idx) {
    for (size_t i = 0; i < v.size(); ++i) {
        if (!key_match(v[i], key, idx)) continue;
        if (i + 1 < v.size()) v[i] = v.back();
        v.pop_back();
        return static_cast<int>(i);
    }
    return -1;
}
// 0xE402CC0: push_back (the condition may remove it again right away: the ledger reads the live vector anyway).
template <class Vec>
void native_append(Vec& v, const Entry& e) {
    v.push_back(e);
}
// The exit with R0's plan applied (the ledger's expectation, the tests): swap then the native erase; native / noop:
// the native erase.
template <class Vec>
int apply_r0(Vec& v, const R0Plan& p, uint32_t key, uint16_t idx) {
    if (p.act == R0Act::Swap && p.first >= 0 && p.own >= 0 && static_cast<size_t>(p.first) < v.size() &&
        static_cast<size_t>(p.own) < v.size()) {
        std::swap(v[static_cast<size_t>(p.first)], v[static_cast<size_t>(p.own)]);
    }
    return native_erase(v, key, idx);
}
// Swap two 0x30-byte blocks as six u64 (the hook's write, replayed on plain memory in the tests). The entry holds two
// 8-byte string handles (+0x00, +0x18), an 8-byte counted pointer (+0x08) and plain values: the native erase moves
// them by plain copy (de404a10.txt), so a swap keeps every handle owned exactly once.
inline void swap_blocks(uint64_t* a, uint64_t* b) {
    for (int k = 0; k < static_cast<int>(kEntrySize / 8); ++k) {
        const uint64_t t = a[k];
        a[k] = b[k];
        b[k] = t;
    }
}

// ---------------------------------------------------------------- a ring for bigger events (the hooks')

// D. Vyukov's bounded MPMC queue, as p8p::EventRing, for any T, filled IN PLACE: push(fill) claims a cell and calls
// fill(T&) on it (the hook writes the fields it has; no big local copy, no memset / memcpy call on a game thread),
// pop(T&) copies it out with T::copy (field by field). push fails when full (counted), never blocks.
template <class T, size_t N>
class FillRing {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two");

public:
    FillRing() {
        for (size_t i = 0; i < N; ++i) cells_[i].seq.store(i, std::memory_order_relaxed);
    }
    FillRing(const FillRing&) = delete;
    FillRing& operator=(const FillRing&) = delete;

    template <class Fill>
    bool push(Fill&& fill) {
        size_t pos = enq_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& c = cells_[pos & (N - 1)];
            const size_t seq = c.seq.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (dif == 0) {
                if (enq_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    fill(c.e);
                    c.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (dif < 0) {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return false;
            } else {
                pos = enq_.load(std::memory_order_relaxed);
            }
        }
    }
    bool pop(T& out) {
        size_t pos = deq_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& c = cells_[pos & (N - 1)];
            const size_t seq = c.seq.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            if (dif == 0) {
                if (deq_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    T::copy(out, c.e);
                    c.seq.store(pos + N, std::memory_order_release);
                    return true;
                }
            } else if (dif < 0) {
                return false;
            } else {
                pos = deq_.load(std::memory_order_relaxed);
            }
        }
    }
    uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

private:
    struct Cell {
        std::atomic<size_t> seq{0};
        T e;
    };
    Cell cells_[N];
    std::atomic<size_t> enq_{0}, deq_{0};
    std::atomic<uint64_t> dropped_{0};
};

// One SetInteraction entry (P2a) or exit (P2) of the server (host) or of the guest's client, with the NPC's list
// BEFORE it. Phase 2: R0's plan (r0_act / r0_why), what was done (r0_done: 0 nothing asked, 1 applied, 2 a write
// failed: native), and each entry's manager in ef bits 1-3 (Where; Unknown when it was not looked up).
struct SiEvent {
    uint64_t qpc_us;
    uint32_t tid;
    uint8_t add, server, client, r0, orphan, complete, where, comp_ok;
    uint8_t guest, r0_mode, r0_act, r0_why, r0_done;
    int8_t first, own, cross;
    uint32_t lock_vt;  // the RVA of [[actor+8]]'s vtable (review F5)
    uint32_t npc, key, owner, n, read;
    uint16_t idx;
    uint64_t stage, victim;
    uintptr_t comp, actor;
    uint64_t es[kMaxScan];
    uint32_t ek[kMaxScan];
    uint16_t ei[kMaxScan];
    uint8_t ef[kMaxScan];  // bit 0: key known; bits 1-3: Where
    static void copy(SiEvent& d, const SiEvent& s) {
        d.qpc_us = s.qpc_us;
        d.tid = s.tid;
        d.add = s.add;
        d.server = s.server;
        d.client = s.client;
        d.r0 = s.r0;
        d.orphan = s.orphan;
        d.complete = s.complete;
        d.where = s.where;
        d.comp_ok = s.comp_ok;
        d.guest = s.guest;
        d.r0_mode = s.r0_mode;
        d.r0_act = s.r0_act;
        d.r0_why = s.r0_why;
        d.r0_done = s.r0_done;
        d.first = s.first;
        d.own = s.own;
        d.lock_vt = s.lock_vt;
        d.cross = s.cross;
        d.npc = s.npc;
        d.key = s.key;
        d.owner = s.owner;
        d.n = s.n;
        d.read = s.read > kMaxScan ? kMaxScan : s.read;
        d.idx = s.idx;
        d.stage = s.stage;
        d.victim = s.victim;
        d.comp = s.comp;
        d.actor = s.actor;
        volatile uint64_t* des = d.es;
        volatile uint32_t* dek = d.ek;
        volatile uint16_t* dei = d.ei;
        volatile uint8_t* def = d.ef;
        for (uint32_t i = 0; i < d.read; ++i) {
            des[i] = s.es[i];
            dek[i] = s.ek[i];
            dei[i] = s.ei[i];
            def[i] = s.ef[i];
        }
    }
    // The list it carries, as entries.
    std::vector<Entry> list() const {
        std::vector<Entry> v;
        for (uint32_t i = 0; i < read && i < kMaxScan; ++i) v.push_back(Entry{ei[i], (ef[i] & 1) != 0, ek[i], es[i]});
        return v;
    }
    Where where_of(uint32_t i) const {
        const uint8_t w = static_cast<uint8_t>((ef[i] >> 1) & 7);
        return w < static_cast<uint8_t>(Where::Count) ? static_cast<Where>(w) : Where::Unknown;
    }
    R0Plan plan() const {
        R0Plan p;
        p.act = r0_act < static_cast<uint8_t>(R0Act::Count) ? static_cast<R0Act>(r0_act) : R0Act::Native;
        p.why = r0_why < static_cast<uint8_t>(R0Why::Count) ? static_cast<R0Why>(r0_why) : R0Why::NoMatch;
        p.first = first;
        p.own = own;
        p.cross = cross;
        return p;
    }
};

// ---------------------------------------------------------------- P1: the stage check of 0x21F2780

enum class P1 : uint8_t { Pass, Mismatch, NoLink, Count };
inline P1 p1_verdict(uint64_t current, uint64_t linked) {
    if (current == linked) return P1::Pass;  // 0x21F2C45 cmp [rsp+0x28],rbx ; je
    return linked ? P1::Mismatch : P1::NoLink;
}
inline const char* p1_name(P1 v) {
    switch (v) {
        case P1::Pass: return "PASS (the NPC's stage is the manager's current one)";
        case P1::Mismatch: return "FAIL eErrNoInvalidStageInfo (the NPC's stage is not the manager's current one)";
        case P1::NoLink: return "FAIL eErrNoInvalidStageInfo (the NPC is linked to no stage)";
        default: return "?";
    }
}

// The callers of 0x21F2780 (build 2976, callers of the PE: call rel32, return = call + 5).
struct Caller {
    uint32_t ret;
    const char* what;
    bool server;
};
inline constexpr Caller kCallers[] = {
    {0x2BFFDCA, "2789 BuyStockFromStoreReq (0x2BFFB00)", true},
    {0x2C04B05, "2366 SellItemToItemStoreReq (0x2C04880)", true},
    {0x2C004A5, "store op 0x2C002D0", true},
    {0x2C064BF, "store op 0x2C06340", true},
    {0x2C08291, "store op 0x2C080A0", true},
    {0x2C0AC65, "store op 0x2C0AA50", true},
    {0x2B47465, "store op 0x2B47250", true},
    {0x21F743B, "0x21F7400", true},
    {0xA1D9E1, "client UI check 0xA1D880", false},
    {0xA1EBCA, "client UI check 0xA1EA80", false},
    {0xA1F6AC, "client UI check 0xA1F550", false},
    {0xA1FD3D, "client UI check 0xA1FBC0", false},
};
inline const Caller* caller_of(uint32_t ret_rva) {
    for (const Caller& c : kCallers) {
        if (c.ret == ret_rva) return &c;
    }
    return nullptr;
}

// ---------------------------------------------------------------- U2-c: the manager of user 2's trade check

// At 0x21F28A1 (rax = the manager field.vf48 gave 0x21F2780; only its lock [rax+8] and its +0xA0 / +0x90 are read from
// it, between 0x21F28A1 and 0x21F2914: f21f2780.asm). Phase 2: user2_shop_trade_route=1 makes rax := mgr2 for Route.
struct U2cIn {
    bool server = false;      // the manager vf48 gave is a ServerSequencerStageManager
    bool user2 = false;       // the store component's actor is user 2's (shp::who: its user, else its character)
    bool subject = false;     // ... and the Troc running on this thread is user 2's, whose subject IS that actor (F9)
    bool host_field = false;  // the field whose vf48 was called is the host's (user2_stages_host_field)
    bool mgr2_ok = false;     // user 2's manager is known (vtable checked)
    uintptr_t got = 0, host = 0, user2_mgr = 0;
    bool armed = false;       // U-1 armed (user2_stages_armed) ...
    uint8_t mgr2_state = 0;   // ... and [mgr2+0x6A0] == 2 (the native "armed" state)
    bool logout = false;      // user 2's goodbye was asked (server_logout_requested)
    bool npc_read = false;    // the NPC's interaction entries could be read
    bool npc_in_mgr2 = false; // one of its shop entries (key 0x000F4321) names a stage alive in mgr2 and not in mgrH
    bool npc_in_mgrh = false; // (logged) one names a stage alive in mgrH
    uint64_t mgr2_current = 0;        // (logged) [mgr2+0xA0] / [+0x90]
    bool mgr2_current_found = false;  // (logged) that stage is alive in mgr2
    // S46 live phase 1, finding B: during the shop dialogue the NPC has NO entries (the 2720 takes its scene's
    // SetInteraction F42CF / F4321 / F42F6 down until the shop closes; the 2789 comes in between with the vector empty).
    // So U2-c decides by the stage whose 0x000F4321 entry user 2's own 2671 on this NPC used when it PASSED vf170 (P6;
    // review ab B1: not "accepted": 0x211E2A0 / 0x2125660 can still refuse it afterwards, then no dialogue opens and no
    // 2789 comes; the 2671's entry copy [rbp-0x41]; AcceptedBook). rec: such a record (user 2's, not closed, <=
    // kAcceptedMaxUs); rec_where: that stage in mgrH / mgr2 now (where_proven).
    bool rec = false;
    uint64_t rec_stage = 0;
    Where rec_where = Where::Unknown;
};
inline constexpr uint8_t kMgrStateArmed = 2;
enum class U2c : uint8_t {
    NotServer,       // the host client's own UI check (a client manager): never routed
    NotUser2,        // the host's (or an unknown) request: kept
    NotSubject,      // the store component is user 2's but this thread's Troc is not (or the subject hook is not in)
    NotHostField,    // not the host's field: kept
    NoUser2Mgr,      // user 2's manager is not known: kept
    AlreadyUser2,    // vf48 already gave mgr2: nothing to do
    OtherManager,    // vf48 gave neither mgrH nor mgr2: kept
    NotArmed,        // U-1 not armed, [mgr2+0x6A0] != 2, or user 2 leaving: kept (until the near lane allows it)
    NpcUnreadable,   // the NPC's entries could not be read: kept
    NpcNotInMgr2,    // no live scene of mgr2 links this NPC (its shop entries): kept
    NpcAlsoInMgrH,   // a live scene of mgrH links it too: the NPC's current stage may be the host's (review F3): kept
    WouldRoute,      // rax := mgr2 (probe: counted; 1: applied)
    StageInMgrH,     // user 2's 2671 (passed vf170) used a stage alive in mgrH (the host's scene, F3): kept
    StageNotLive,    // that stage is alive in neither (gone, or a lookup failed): kept
    Count
};
inline U2c decide_u2c(const U2cIn& in) {
    if (!in.server) return U2c::NotServer;
    if (!in.user2) return U2c::NotUser2;
    if (!in.subject) return U2c::NotSubject;
    if (!in.host_field) return U2c::NotHostField;
    if (!in.mgr2_ok || !in.user2_mgr) return U2c::NoUser2Mgr;
    if (in.got == in.user2_mgr) return U2c::AlreadyUser2;
    if (!in.got || in.got != in.host) return U2c::OtherManager;
    if (!in.armed || in.mgr2_state != kMgrStateArmed || in.logout) return U2c::NotArmed;
    if (!in.npc_read) return U2c::NpcUnreadable;
    // F3 kept: a live shop entry of mgrH on the NPC (the host's scene links it) never routes.
    if (in.npc_in_mgrh) return U2c::NpcAlsoInMgrH;
    // Finding B: the stage of user 2's 2671 that passed vf170 first (the entries are gone during the dialogue). It routes only
    // when that stage is alive in mgr2 and NOT in mgrH (F3).
    if (in.rec) {
        if (in.rec_where == Where::User2) return U2c::WouldRoute;
        if (in.rec_where == Where::Host || in.rec_where == Where::Both) return U2c::StageInMgrH;
        return U2c::StageNotLive;
    }
    if (!in.npc_in_mgr2) return U2c::NpcNotInMgr2;
    return U2c::WouldRoute;
}
inline const char* u2c_name(U2c v) {
    switch (v) {
        case U2c::NotServer: return "not the server's check (the host client's UI): kept";
        case U2c::NotUser2: return "not user 2's request: kept";
        case U2c::NotSubject: return "user 2's store component but not user 2's Troc on this thread: kept";
        case U2c::NotHostField: return "not the host's field: kept";
        case U2c::NoUser2Mgr: return "user 2's manager not known: kept";
        case U2c::AlreadyUser2: return "vf48 already gave mgr2";
        case U2c::OtherManager: return "vf48 gave neither mgrH nor mgr2: kept";
        case U2c::NotArmed: return "mgr2 not armed (U-1, +0x6A0 != 2) or user 2 leaving: kept";
        case U2c::NpcUnreadable: return "the NPC's entries could not be read: kept";
        case U2c::NpcNotInMgr2: return "no live scene of mgr2 links this NPC: kept";
        case U2c::NpcAlsoInMgrH: return "a live scene of mgrH links this NPC too: kept";
        case U2c::WouldRoute: return "route to mgr2";
        case U2c::StageInMgrH: return "user 2's 2671 (passed vf170) used a stage alive in mgrH: kept";
        case U2c::StageNotLive: return "user 2's 2671 (passed vf170) used a stage not proven alive in mgr2 only: kept";
        default: return "?";
    }
}
// S46 live phase 1, finding B: per NPC, the stage whose shop entry (0x000F4321) the last 2671 that PASSED vf170 used
// (P6 0x2B3708C: the 2671's entry copy at [rbp-0x61], its +0x20 = [rbp-0x41]), with who asked and when. Written
// by P6 on the server's threads, read by U2-c on them: a tiny spin lock per slot (only these stores and loads under
// it: no game call), the reader only TRIES it (busy: no record, U2-c keeps the manager). Direct-mapped by the NPC key:
// a colliding NPC overwrites the slot (no record: kept). Never across PCs: keys and stage ids of this server only.
// Review ab B2: the record ends with the dialogue (close: P2a appends that stage's 0x000F4321 entry again on the NPC,
// or a 2743 InteractionResetAck of the NPC to user 2), at most 10 minutes (a long shop dialogue keeps it).
inline constexpr uint64_t kAcceptedMaxUs = 600'000'000;
struct Accepted {
    uint32_t npc = 0;
    uint64_t stage = 0;
    uint8_t who = 0;  // shp::Who
    uint64_t t_us = 0;
};
template <size_t N = 64>
class AcceptedBook {
public:
    void put(const Accepted& a) noexcept {
        Slot& s = s_[slot(a.npc)];
        for (int spin = 0; s.lock.exchange(true, std::memory_order_acquire); ++spin) {
            if (spin > 4096) return;  // never: the holder only stores four values
        }
        s.v = a;
        s.lock.store(false, std::memory_order_release);
    }
    // The dialogue is over: the record of `npc` goes (stage != 0: only if it names that stage). Returns whether one went.
    bool close(uint32_t npc, uint64_t stage = 0) noexcept {
        if (!npc) return false;
        Slot& s = s_[slot(npc)];
        for (int spin = 0; s.lock.exchange(true, std::memory_order_acquire); ++spin) {
            if (spin > 4096) return false;
        }
        const bool hit = s.v.npc == npc && s.v.t_us && (!stage || s.v.stage == stage);
        if (hit) s.v = Accepted{};
        s.lock.store(false, std::memory_order_release);
        return hit;
    }
    // The record of `npc` if it is younger than max_us at now_us (false: none, too old, or the slot busy).
    bool get(uint32_t npc, uint64_t now_us, Accepted& out, uint64_t max_us = kAcceptedMaxUs) const noexcept {
        if (!npc) return false;
        const Slot& s = s_[slot(npc)];
        if (s.lock.exchange(true, std::memory_order_acquire)) return false;
        const Accepted v = s.v;
        s.lock.store(false, std::memory_order_release);
        if (v.npc != npc || !v.t_us || now_us < v.t_us || now_us - v.t_us > max_us) return false;
        out = v;
        return true;
    }

private:
    struct Slot {
        mutable std::atomic<bool> lock{false};
        Accepted v;
    };
    static size_t slot(uint32_t npc) noexcept { return (npc * 0x9E3779B9u >> 16) % N; }
    Slot s_[N];
};

// S46 live phase 1, finding A: P1 (0x21F2C45) cannot read the manager at [rsp+0x38] (0x21F2A56 / 0x21F2AC5 lea rdx,
// [rsp+0x30] ; call 0x4F8220 write a position over it). P1b (0x21F2917, the same call of 0x21F2780: rbx = the manager,
// unchanged from 0x21F28A1 mov rbx,rax, after U2-c) seals it with its rsp (the frame: rsp moves only in balanced calls
// between the two points); P1 takes it only when its rsp is the same.
struct P1Seal {
    uintptr_t rsp = 0, mgr = 0;
};
inline uintptr_t p1_manager(const P1Seal& s, uintptr_t rsp) noexcept { return s.rsp && s.rsp == rsp ? s.mgr : 0; }

// The NPC's shop entries (key 0x000F4321, a stage id) against the two managers: in_mgr2 = one is alive in mgr2 only;
// in_mgrh = one is alive in mgrH (then U2-c keeps mgrH: NpcAlsoInMgrH).
// alive(stage) -> Where (Host = mgrH, User2 = mgr2), from where_proven. Review ab M1: a shop entry whose managers could
// not be read (Unknown) makes the whole read not decided (read = false: NpcUnreadable, kept).
template <class Alive>
void u2c_npc(const Entry* e, uint32_t n, uint32_t total, Alive&& alive, bool& read, bool& in_mgr2, bool& in_mgrh) {
    read = n == total;
    in_mgr2 = in_mgrh = false;
    if (!read) return;
    for (uint32_t i = 0; i < n; ++i) {
        if (!e[i].key_known || e[i].key != kShopKey || !(e[i].stage & kStageMask)) continue;
        const Where w = alive(e[i].stage);
        if (w == Where::Unknown) {
            read = false;
            in_mgr2 = in_mgrh = false;
            return;
        }
        if (w == Where::User2) in_mgr2 = true;
        if (w == Where::Host || w == Where::Both) in_mgrh = true;
    }
}

// ---------------------------------------------------------------- the ledger (the module's thread)

// The shop NPCs whose entries the server changed (P2 / P2a), and every second: is each live entry's stage alive in
// mgrH or mgr2? An entry whose stage is in neither for kOrphanChecks checks in a row is an orphan ([orphan], once).
inline constexpr uint32_t kOrphanChecks = 2;
inline constexpr size_t kLedgerNpcs = 64;

struct LedgerNpc {
    uint32_t key = 0;        // the NPC's actor key (never its address across PCs; the address is this process's own)
    uintptr_t actor = 0, comp = 0;
    std::vector<Entry> expected;  // the native operations replayed on the last list a hook read
    struct Watch {
        uint64_t stage = 0;
        uint32_t gone = 0;
        bool reported = false;
    };
    std::vector<Watch> watch;
};

class Ledger {
public:
    // A server SetInteraction exit / entry on a shop NPC: the list the hook read BEFORE the operation and the
    // operation. add: an entry {key, idx, stage} is appended; else the native erase of `key`, with R0's plan when R0
    // applied it (phase 2: a swap first, or a sentinel that erases nothing).
    void apply(uint32_t npc_key, uintptr_t actor, uintptr_t comp, const std::vector<Entry>& before, bool add,
               const Entry& op, const R0Plan* applied = nullptr) {
        LedgerNpc* n = find(npc_key);
        if (!n) {
            if (npcs_.size() >= kLedgerNpcs) {
                ++full_;
                return;
            }
            npcs_.push_back({});
            n = &npcs_.back();
            n->key = npc_key;
        }
        n->actor = actor;
        n->comp = comp;
        n->expected = before;
        if (add) {
            native_append(n->expected, op);
        } else if (applied) {
            apply_r0(n->expected, *applied, op.key, op.idx);
        } else {
            native_erase(n->expected, op.key, op.idx);
        }
    }
    // One check. read_live(npc, out) -> false: the NPC is gone (its entry is dropped); alive(stage) -> Where.
    // report(npc, stage, where) is called once per orphan. Returns the number of mismatches between the live list and
    // the expected one (a native change the hooks did not see, or a torn read).
    template <class ReadLive, class Alive, class Report>
    uint32_t check(ReadLive&& read_live, Alive&& alive, Report&& report) {
        uint32_t mismatch = 0;
        for (size_t i = 0; i < npcs_.size();) {
            LedgerNpc& n = npcs_[i];
            std::vector<Entry> live;
            if (!read_live(n, live)) {
                npcs_.erase(npcs_.begin() + static_cast<std::ptrdiff_t>(i));
                ++gone_;
                continue;
            }
            if (!same(live, n.expected)) ++mismatch;
            std::vector<LedgerNpc::Watch> next;
            for (const Entry& e : live) {
                if (!(e.stage & kStageMask)) continue;  // static
                LedgerNpc::Watch w;
                for (const LedgerNpc::Watch& o : n.watch) {
                    if (o.stage == e.stage) w = o;
                }
                w.stage = e.stage;
                const Where wh = alive(e.stage);
                if (wh == Where::Neither) {
                    ++w.gone;
                    if (w.gone >= kOrphanChecks && !w.reported) {
                        w.reported = true;
                        ++orphans_;
                        report(n, e, wh);
                    }
                } else if (wh != Where::Unknown) {
                    w.gone = 0;
                }
                next.push_back(w);
            }
            n.watch = std::move(next);
            ++i;
        }
        return mismatch;
    }
    LedgerNpc* find(uint32_t key) {
        for (LedgerNpc& n : npcs_) {
            if (n.key == key) return &n;
        }
        return nullptr;
    }
    size_t size() const { return npcs_.size(); }
    template <class F>
    void each_key(F&& f) const {
        for (const LedgerNpc& n : npcs_) f(n.key);
    }
    uint64_t orphans() const { return orphans_; }
    uint64_t gone() const { return gone_; }
    uint64_t full() const { return full_; }
    static bool same(const std::vector<Entry>& a, const std::vector<Entry>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i].idx != b[i].idx || a[i].stage != b[i].stage) return false;
        }
        return true;
    }

private:
    std::vector<LedgerNpc> npcs_;
    uint64_t orphans_ = 0, gone_ = 0, full_ = 0;
};

// Is this list (or the operation) a shop NPC's? An entry with the shop key.
inline bool has_shop(const std::vector<Entry>& v) {
    for (const Entry& e : v) {
        if (e.key_known && e.key == kShopKey) return true;
    }
    return false;
}

// ---------------------------------------------------------------- P3: eErrNo value -> name

// Built once from the game's variables (eerrno_names.h): value -> index of the name; 0 and duplicated values are
// left out (a duplicated value names nothing: ambiguous).
struct ErrTable {
    struct Row {
        uint32_t value;
        uint32_t name;  // index into the names
    };
    std::vector<Row> rows;  // sorted by value
    size_t zero = 0, dup = 0;
    template <class Values>
    void build(const Values& values) {  // values[i] = the u32 of name i (0 = unreadable / not filled)
        rows.clear();
        zero = dup = 0;
        for (uint32_t i = 0; i < values.size(); ++i) {
            if (!values[i]) {
                ++zero;
                continue;
            }
            rows.push_back({values[i], i});
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.value < b.value; });
        std::vector<Row> out;
        for (size_t i = 0; i < rows.size();) {
            size_t j = i;
            while (j < rows.size() && rows[j].value == rows[i].value) ++j;
            if (j - i == 1) {
                out.push_back(rows[i]);
            } else {
                dup += j - i;
            }
            i = j;
        }
        rows = std::move(out);
    }
    // The name index of a value, or -1.
    int find(uint32_t v) const {
        size_t lo = 0, hi = rows.size();
        while (lo < hi) {
            const size_t mid = (lo + hi) / 2;
            if (rows[mid].value < v) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo < rows.size() && rows[lo].value == v ? static_cast<int>(rows[lo].name) : -1;
    }
};

}  // namespace cdmp::session2::stp
