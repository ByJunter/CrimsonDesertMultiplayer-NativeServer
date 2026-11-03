// P8 S1/S2 read-only probes (src/mod/session2/p8_probe_logic.h and the wait-list
// scan in guest_actor_diag_logic.h; docs/research/pendientes/p8-impl-s1s2.md).
//
// SCOPE: offline logic only. These tests run the probes' pure functions on fake
// memory and synthetic inputs: the lock-free event ring, the rate / per-key
// limits, change-only logging, the wait-list counting with unreadable slots,
// the replay of ActionGraph_Find 0x24314C0 (as its instructions read), the
// RestartLayerAction path and the 0xA97D60 predicate attribution. They do NOT
// test the game's behaviour, the hooks, the byte checks against the running
// game nor whether the offsets are right for build 2976 (that is static
// analysis, p8-impl-s1s2.md §2, and the live session).

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <thread>
#include <vector>

#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/p8_probe_logic.h"

using namespace cdmp::session2;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

// ---------------------------------------------------------------- ring, limits, gates

void test_ring_basic() {
    p8p::EventRing<4> r;
    p8p::Event e;
    CHECK(!r.pop(e));  // empty
    for (uint64_t i = 0; i < 4; ++i) {
        p8p::Event in;
        in.kind = static_cast<uint16_t>(i);
        in.v[11] = i * 3;
        CHECK(r.push(in));
    }
    p8p::Event extra;
    CHECK(!r.push(extra));  // full: refused and counted, never blocking
    CHECK(r.dropped() == 1);
    for (uint64_t i = 0; i < 4; ++i) {
        CHECK(r.pop(e));
        CHECK(e.kind == i && e.v[11] == i * 3);  // FIFO, the whole event copied
    }
    CHECK(!r.pop(e));
    // Space again after draining (the sequence numbers wrap correctly).
    for (int round = 0; round < 10; ++round) {
        p8p::Event in;
        in.v[0] = static_cast<uint64_t>(round);
        CHECK(r.push(in));
        CHECK(r.pop(e) && e.v[0] == static_cast<uint64_t>(round));
    }
    CHECK(r.dropped() == 1);
}

void test_ring_concurrent() {
    // Four "game threads" push while one drain pops: every event is either
    // received exactly once or counted as dropped.
    static p8p::EventRing<256> r;
    constexpr int kThreads = 4, kEach = 20000;
    std::atomic<bool> done{false};
    std::vector<uint32_t> seen(kThreads * kEach, 0);
    std::thread consumer([&] {
        p8p::Event e;
        for (;;) {
            const bool finished = done.load();
            while (r.pop(e)) ++seen[e.v[0]];
            if (finished) break;
        }
    });
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([t] {
            for (int i = 0; i < kEach; ++i) {
                p8p::Event e;
                e.v[0] = static_cast<uint64_t>(t * kEach + i);
                r.push(e);
            }
        });
    }
    for (auto& p : producers) p.join();
    done = true;
    consumer.join();
    uint64_t received = 0;
    bool duplicate = false;
    for (uint32_t s : seen) {
        received += s;
        duplicate |= s > 1;
    }
    CHECK(!duplicate);
    CHECK(received + r.dropped() == static_cast<uint64_t>(kThreads) * kEach);
}

void test_window_limiter() {
    p8p::WindowLimiter lim(3, 5'000'000);
    CHECK(lim.take(1) && lim.take(2) && lim.take(4'999'999));
    CHECK(!lim.take(4'999'999) && lim.refused() == 1);
    // The next window starts again.
    CHECK(lim.take(5'000'000) && lim.take(5'000'001) && lim.take(9'000'000));
    CHECK(!lim.take(9'999'999) && lim.refused() == 2);
    // A window far later (a quiet minute) too.
    CHECK(lim.take(60'000'000));
    p8p::WindowLimiter none(0, 1000);
    CHECK(!none.take(1) && none.refused() == 1);
}

void test_key_counter() {
    p8p::KeyCounter<8> k;
    CHECK(k.bump(0) == 0);  // 0 is never a key
    CHECK(k.bump(0xB0100078) == 1 && k.bump(0xB0100078) == 2 && k.bump(0xB0100078) == 3);
    CHECK(k.count(0xB0100078) == 3 && k.count(0xB0100079) == 0);
    // The per-key budget: the first two events of each key.
    CHECK(p8p::within_key_budget(1, 2) && p8p::within_key_budget(2, 2) && !p8p::within_key_budget(3, 2));
    CHECK(!p8p::within_key_budget(0, 2));  // untracked (full table): over budget
    // Bounded (I4): N/2 keys, then closed for good (no insertion tried, a bounded lookup only).
    for (uint32_t i = 1; i < 4; ++i) CHECK(k.bump(0xB0100000 + i) == 1);
    CHECK(k.size() == 4 && !k.closed());
    CHECK(k.bump(0xB0100004) == 0 && k.full() == 1 && k.closed());  // full: says 0, counted
    CHECK(k.bump(0xA0100001) == 0 && k.full() == 2);
    CHECK(k.bump(0xB0100003) == 2);  // known keys still count
    CHECK(k.size() == 4);
    // The production size, overfilled: it closes at N/2 and stays bounded.
    static p8p::KeyCounter<8192> prod;
    size_t tracked = 0;
    for (uint32_t key = 1; key <= 10000; ++key) tracked += prod.bump(0xB0000000u + key * 7919u) == 1;
    CHECK(prod.closed() && tracked == prod.size() && prod.size() <= p8p::KeyCounter<8192>::kMaxKeys);
    CHECK(prod.bump(0xB0000000u + 7919u) == 2);
    // Concurrent bumps of the same keys lose nothing.
    static p8p::KeyCounter<1024> big;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([] {
            for (uint32_t r = 0; r < 100; ++r) {
                for (uint32_t key = 1; key <= 50; ++key) big.bump(0xB0100000 + key);
            }
        });
    }
    for (auto& t : threads) t.join();
    bool all = big.size() == 50;
    for (uint32_t key = 1; key <= 50; ++key) all &= big.count(0xB0100000 + key) == 400;
    CHECK(all);
}

void test_change_gate() {
    p8p::ChangeGate g;
    CHECK(g.changed(0));   // the first offer always logs, even a 0 signature
    CHECK(!g.changed(0));  // the same state: quiet
    CHECK(!g.changed(0));
    CHECK(g.changed(p8p::mix(0, 0x11)));
    CHECK(!g.changed(p8p::mix(0, 0x11)));
    CHECK(g.changed(0));  // back to the old state: a change again
    // mix separates nearby inputs (M pointer + small fields).
    CHECK(p8p::mix(p8p::mix(0, 0x1000), 1) != p8p::mix(p8p::mix(0, 0x1000), 2));
    CHECK(p8p::mix(p8p::mix(0, 1), 2) != p8p::mix(p8p::mix(0, 2), 1));
}

void test_qpc() {
    CHECK(p8p::qpc_to_us(10'000'000, 10'000'000) == 1'000'000);
    CHECK(p8p::qpc_to_us(15, 10) == 1'500'000);
    CHECK(p8p::qpc_to_us(123, 0) == 0);
    // A month of uptime at 10 MHz: no overflow (ticks * 1e6 would overflow).
    const uint64_t month = 10'000'000ull * 3600 * 24 * 31;
    CHECK(p8p::qpc_to_us(month, 10'000'000) == 1'000'000ull * 3600 * 24 * 31);
}

// ---------------------------------------------------------------- the wait list (A2)

gad::WaitRead slot(uint32_t key, bool found, uint8_t c2 = 0, uint8_t cf = 0) {
    gad::WaitRead r;
    r.key_ok = true;
    r.e.key = key;
    r.e.found = found;
    r.e.c2 = c2;
    r.e.cf = cf;
    return r;
}

gad::WaitRead unreadable() { return gad::WaitRead{}; }

void test_wait_scan() {
    using V = gad::WaitVerdict;
    // Counting: every slot read is classified, unreadable ones included.
    const gad::WaitRead mixed[] = {slot(0xB0100002, false), unreadable(), slot(0xA010022B, true, 1, 1),
                                   slot(0xB0100078, true, 1, 0), slot(0xB0100079, true, 0, 1)};
    gad::WaitScan s = gad::scan_wait_list(mixed, 5);
    CHECK(s.total == 5 && s.inspected == 5 && s.unreadable == 1 && s.not_found == 1 && s.ready == 1 &&
          s.not_ready == 2);
    // Replay: slot 0 (missing) goes, the last (#4, not ready) moves to the front and blocks.
    CHECK(s.verdict == V::Blocks && s.index == 4 && s.key == 0xB0100079);
    // An unreadable slot on the replay's path: undetermined, never guessed.
    const gad::WaitRead path_unreadable[] = {slot(0xB0100002, false), slot(0xB0100078, true, 1, 0), unreadable()};
    s = gad::scan_wait_list(path_unreadable, 3);
    CHECK(s.verdict == V::Undetermined && s.index == 2 && s.unreadable == 1);
    // An unreadable slot the replay never reaches does not hide the blocker.
    const gad::WaitRead off_path[] = {slot(0xB0100078, true, 1, 0), unreadable()};
    s = gad::scan_wait_list(off_path, 2);
    CHECK(s.verdict == V::Blocks && s.index == 0 && s.key == 0xB0100078 && s.unreadable == 1);
    // Fewer slots read than the count (the cap): the swap-remove brings an
    // uninspected slot to the front -> undetermined, with its index.
    const gad::WaitRead capped[] = {slot(0xB0100002, false), slot(0xB0100078, true, 1, 0)};
    s = gad::scan_wait_list(capped, 173);
    CHECK(s.total == 173 && s.inspected == 2 && s.verdict == V::Undetermined && s.index == 172);
    // ... but a front that blocks is still certain.
    const gad::WaitRead capped_front[] = {slot(0xB0100078, true, 1, 0)};
    s = gad::scan_wait_list(capped_front, 173);
    CHECK(s.verdict == V::Blocks && s.index == 0);
    // Everything ready or missing: the list empties.
    const gad::WaitRead clear[] = {slot(1, false), slot(2, true, 1, 1), slot(3, true, 2, 9)};
    s = gad::scan_wait_list(clear, 3);
    CHECK(s.verdict == V::Empties && s.index == -1 && s.ready == 2 && s.not_found == 1);
    CHECK(gad::scan_wait_list({}, 0).verdict == V::Empties);
    // A count no list has: no replay, said so.
    s = gad::scan_wait_list(capped, 0x7FFFFFFF);
    CHECK(s.garbage_count && s.verdict == V::Undetermined);
    // With every slot readable the verdict agrees with the original replay (wait_blocker).
    std::mt19937 rng(2976);
    bool agree = true;
    for (int round = 0; round < 500; ++round) {
        const uint32_t n = rng() % 12;
        std::vector<gad::WaitRead> reads;
        std::vector<gad::WaitEntry> entries;
        for (uint32_t i = 0; i < n; ++i) {
            const gad::WaitRead r = slot(0xB0100000 + i, rng() % 4 != 0, static_cast<uint8_t>(rng() % 2),
                                         static_cast<uint8_t>(rng() % 2));
            reads.push_back(r);
            entries.push_back(r.e);
        }
        const int expect = gad::wait_blocker(entries);
        const gad::WaitScan got = gad::scan_wait_list(reads, n);
        agree &= expect < 0 ? got.verdict == V::Empties : got.verdict == V::Blocks && got.index == expect;
    }
    CHECK(agree);
}

// ---------------------------------------------------------------- ActionGraph_Find's replay (S1)

struct FakeMem {
    std::map<uintptr_t, uint64_t> q;
    std::map<uintptr_t, uint32_t> d;
    bool u32(uintptr_t a, uint32_t& v) const {
        if (auto it = d.find(a); it != d.end()) {
            v = it->second;
            return true;
        }
        return false;
    }
    bool ptr(uintptr_t a, uintptr_t& v) const {
        if (auto it = q.find(a); it != q.end()) {
            v = static_cast<uintptr_t>(it->second);
            return true;
        }
        return false;
    }
};

using L = p8p::GraphLayout;
constexpr uintptr_t kGraph = 0x100000, kTable = 0x200000, kThirds = 0x300000;

// A graph whose table holds `hashes` (sorted ascending, as 0x24315A0 builds it).
void make_graph(FakeMem& m, const std::vector<uint32_t>& hashes) {
    m = FakeMem{};
    m.d[kGraph + L::kCount] = static_cast<uint32_t>(hashes.size());
    m.q[kGraph + L::kTable] = kTable;
    for (size_t i = 0; i < hashes.size(); ++i) {
        const uintptr_t e = kTable + i * L::kTripleSize;
        const uintptr_t third = kThirds + i * 0x100;
        m.q[e] = 0x500000 + i;
        m.q[e + 8] = 0x600000 + i;
        m.q[e + 0x10] = third;
        m.d[third + L::kHashInThird] = hashes[i];
    }
}

// The native semantics, written independently: exact hash match in a complete triple.
int reference_find(const std::vector<uint32_t>& hashes, uint32_t h) {
    for (size_t i = 0; i < hashes.size(); ++i) {
        if (hashes[i] == h) return static_cast<int>(i);
    }
    return -1;
}

void test_graph_find() {
    FakeMem m;
    const std::vector<uint32_t> table = {0x0000BEEF, 0x26107D3C, 0x349EFD61, p8p::kHashSeqBodyAnimLoop,
                                         p8p::kHashSeqBodyAnim, 0xF2EE2678};
    std::vector<uint32_t> sorted = table;
    std::sort(sorted.begin(), sorted.end());
    make_graph(m, sorted);
    for (size_t i = 0; i < sorted.size(); ++i) {
        const p8p::FindResult r = p8p::graph_find(m, kGraph, sorted[i]);
        CHECK(r.read_ok && r.found && r.index == static_cast<int32_t>(i) && r.count == sorted.size());
        CHECK(r.triple[0] == 0x500000 + i && r.triple[1] == 0x600000 + i && r.triple[2] == kThirds + i * 0x100);
        CHECK(r.iterations <= 4);  // log2(6)+1
    }
    // A hash that is not there: not found, every read fine (the case of S1's 15 actors).
    p8p::FindResult r = p8p::graph_find(m, kGraph, 0x12345678);
    CHECK(r.read_ok && !r.found && r.index == -1);
    // Empty graph, null graph, a count above 0x7FFFFFFF (the native "js").
    make_graph(m, {});
    r = p8p::graph_find(m, kGraph, p8p::kHashSeqBodyAnim);
    CHECK(r.read_ok && !r.found && r.count == 0);
    CHECK(!p8p::graph_find(m, 0, 1).read_ok);
    make_graph(m, sorted);
    m.d[kGraph + L::kCount] = 0x80000001u;
    r = p8p::graph_find(m, kGraph, p8p::kHashSeqBodyAnim);
    CHECK(r.read_ok && !r.found);
    // A garbage count with a readable table: the loop bound stops it, never "found".
    m.d[kGraph + L::kCount] = 0x7FFFFFFF;
    r = p8p::graph_find(m, kGraph, 0xFFFFFFFF, 8);
    CHECK(!r.found);
    // An unreadable table slot: read_ok false.
    make_graph(m, sorted);
    m.q.erase(kTable + 2 * L::kTripleSize);
    r = p8p::graph_find(m, kGraph, sorted[2]);
    CHECK(!r.read_ok && !r.found);
    // An incomplete triple (third pointer 0) at the hash's place: the native
    // compares 0 and moves right, so its own hash is not found there.
    make_graph(m, sorted);
    m.q[kTable + 3 * L::kTripleSize + 0x10] = 0;
    r = p8p::graph_find(m, kGraph, sorted[3]);
    CHECK(r.read_ok && !r.found);
    // Hash 0 matches an entry whose triple is incomplete (k = 0 >= 0): the copy happens.
    make_graph(m, {0x10, 0x20});
    m.q[kTable + 8] = 0;
    r = p8p::graph_find(m, kGraph, 0);
    CHECK(r.read_ok && r.found);
    // Random sorted tables against the independent reference.
    std::mt19937 rng(57);
    bool agree = true;
    for (int round = 0; round < 300; ++round) {
        std::vector<uint32_t> hs;
        const size_t n = rng() % 40;
        while (hs.size() < n) {
            const uint32_t h = rng() | 1u;
            if (std::find(hs.begin(), hs.end(), h) == hs.end()) hs.push_back(h);
        }
        std::sort(hs.begin(), hs.end());
        make_graph(m, hs);
        for (int probe = 0; probe < 10; ++probe) {
            const uint32_t h = (probe % 2 && !hs.empty()) ? hs[rng() % hs.size()] : (rng() | 1u);
            const p8p::FindResult got = p8p::graph_find(m, kGraph, h);
            const int want = reference_find(hs, h);
            agree &= got.read_ok && got.found == (want >= 0) && (want < 0 || got.index == want);
        }
    }
    CHECK(agree);
}

void test_restart_path() {
    using P = p8p::RestartPath;
    p8p::FindResult hit, miss, bad;
    hit.read_ok = hit.found = true;
    miss.read_ok = true;
    CHECK(p8p::classify_restart(0, true, hit, true, hit) == P::HashZero);
    CHECK(p8p::classify_restart(1, true, hit, true, miss) == P::Primary);
    CHECK(p8p::classify_restart(1, true, miss, true, hit) == P::Fallback);
    CHECK(p8p::classify_restart(1, false, miss, true, hit) == P::Fallback);  // no layer chart (je 0x863918)
    CHECK(p8p::classify_restart(1, true, miss, true, miss) == P::FallbackMissing);
    // S1's 15 actors: chart present, hash absent, fallback CC+0x70/+0x78 null.
    CHECK(p8p::classify_restart(p8p::kHashSeqBodyAnim, true, miss, false, miss) == P::NoGraph);
    CHECK(p8p::classify_restart(1, true, bad, true, hit) == P::Unreadable);
    CHECK(p8p::classify_restart(1, true, miss, true, bad) == P::Unreadable);
    for (P path : {P::HashZero, P::Primary, P::Fallback, P::FallbackMissing, P::NoGraph, P::Unreadable}) {
        CHECK(std::strlen(p8p::restart_path_name(path)) > 3);
    }
    const uintptr_t full[3] = {1, 2, 3}, part[3] = {1, 0, 3};
    CHECK(p8p::triple_complete(full) && !p8p::triple_complete(part));
    CHECK(p8p::s1_hash(0x837487D4) && p8p::s1_hash(0x35DD392F) && !p8p::s1_hash(0x26107D3C));
}

// ---------------------------------------------------------------- 0xA97D60's attribution (S2)

p8p::List1Entry entry(uint8_t f28c, uint8_t mode, uint8_t st61, float lo = 0, float hi = 0) {
    p8p::List1Entry e{};
    e.readable = true;
    e.flag28c = f28c;
    e.mode60 = mode;
    e.state61 = st61;
    for (int i = 0; i < 3; ++i) {
        e.box[i] = lo;
        e.box[3 + i] = hi;
    }
    return e;
}

p8p::LoadSnapshot ready_snapshot() {
    p8p::LoadSnapshot s;
    s.m_ok = true;
    s.b231 = 2;
    s.pos[0] = s.pos[1] = s.pos[2] = 50.0f;
    return s;
}

void test_attribution() {
    using B = p8p::Blocker;
    p8p::LoadSnapshot s = ready_snapshot();
    // AL = 1: nothing blocked, whatever the reads say.
    s.b231 = 0;
    CHECK(p8p::attribute_a97d60(s, true).blocker == B::None);
    // The order of 0xA97D60: +0x231, +0x174, the timer, then list 1.
    CHECK(p8p::attribute_a97d60(s, false).blocker == B::Byte231);
    s.b231 = 2;
    s.i174 = 1;
    CHECK(p8p::attribute_a97d60(s, false).blocker == B::Busy174);
    s.i174 = 0;
    s.b2d4 = 1;
    s.f2d0 = -1.0f;
    s.threshold = 0.0f;
    CHECK(p8p::attribute_a97d60(s, false).blocker == B::Timer2D0);
    s.f2d0 = 0.5f;  // timer past the threshold: not a blocker
    CHECK(p8p::attribute_a97d60(s, false).blocker == B::Undetermined);
    s.b2d4 = 0;
    s.f2d0 = -1.0f;  // timer off: the float does not matter
    CHECK(p8p::attribute_a97d60(s, false).blocker == B::Undetermined);
    // List 1: an entry whose +0x28C is 0 blocks before 0x1AC2830 runs.
    const p8p::List1Entry flag0[] = {entry(0, 2, 0)};
    s.list1 = flag0;
    s.list1_count = 1;
    p8p::Attribution a = p8p::attribute_a97d60(s, false);
    CHECK(a.blocker == B::List1Flag28C && a.index == 0 && a.unknown_before == 0);
    // +0x61 == 5 and "mode 0/1 inside the box" block whatever 0x1AC2830 says; the
    // entries before it are undetermined (their 0x1AC2830 may have blocked first).
    const p8p::List1Entry later[] = {entry(1, 2, 0), entry(1, 2, 0), entry(1, 7, 5)};
    s.list1 = later;
    s.list1_count = 3;
    a = p8p::attribute_a97d60(s, false);
    CHECK(a.blocker == B::List1State61 && a.index == 2 && a.unknown_before == 2);
    const p8p::List1Entry box[] = {entry(1, 1, 0, 0.0f, 100.0f)};
    s.list1 = box;
    s.list1_count = 1;
    CHECK(p8p::attribute_a97d60(s, false).blocker == B::List1InBox);
    const p8p::List1Entry box_mode2[] = {entry(1, 2, 0, 0.0f, 100.0f)};  // mode 2: the box does not apply
    s.list1 = box_mode2;
    CHECK(p8p::attribute_a97d60(s, false).blocker == B::Undetermined);
    const p8p::List1Entry outside[] = {entry(1, 0, 0, 60.0f, 100.0f)};  // mode 0 but outside the box
    s.list1 = outside;
    CHECK(p8p::attribute_a97d60(s, false).blocker == B::Undetermined);
    // An unreadable entry on the way: said so, with its index.
    p8p::List1Entry hole[] = {entry(1, 2, 0), entry(0, 0, 0)};
    hole[1].readable = false;
    s.list1 = hole;
    s.list1_count = 2;
    a = p8p::attribute_a97d60(s, false);
    CHECK(a.blocker == B::Unreadable && a.index == 1);
    // Fewer entries read than the count: flagged.
    const p8p::List1Entry two[] = {entry(1, 2, 0), entry(1, 2, 0)};
    s.list1 = two;
    s.list1_count = 40;
    a = p8p::attribute_a97d60(s, false);
    CHECK(a.blocker == B::Undetermined && a.list1_truncated && a.unknown_before == 2);
    // M unreadable.
    s.m_ok = false;
    CHECK(p8p::attribute_a97d60(s, false).blocker == B::Unreadable);
    for (B b : {B::None, B::Byte231, B::Busy174, B::Timer2D0, B::List1Flag28C, B::List1State61, B::List1InBox,
                B::Unreadable, B::Undetermined}) {
        CHECK(std::strlen(p8p::blocker_name(b)) > 3);
    }
    // in_box is inclusive on both ends, as 0xA97D60's <= compares.
    const float b6[6] = {0, 0, 0, 1, 1, 1};
    const float edge[3] = {1, 0, 1}, out[3] = {1.0001f, 0, 0};
    CHECK(p8p::in_box(b6, edge) && !p8p::in_box(b6, out));
}

}  // namespace

int main() {
    test_ring_basic();
    test_ring_concurrent();
    test_window_limiter();
    test_key_counter();
    test_change_gate();
    test_qpc();
    test_wait_scan();
    test_graph_find();
    test_restart_path();
    test_attribution();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("optd P8 probe tests (offline logic only): all passed\n");
    return 0;
}
