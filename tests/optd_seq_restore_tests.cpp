// P8 seq_actor_restore (docs/research/pendientes/p8-impl-seq-restore.md).
//
// SCOPE: offline logic only (guest_seq_restore_logic.h):
//  - the key's values;
//  - sar::chart_find, the replica of ActionGraph_Find 0x24314C0, on fake charts (sorted entries, empty,
//    incomplete triples, unreadable memory, a torn count);
//  - sar::plan: every verdict, in order, on the session-8 cases (the Freeswords of sequencer 0x1B0 and Damiane's
//    group: both layers seq_bodyanim, nothing in the charts nor in [CC+0x70/+0x78]), a layer that keeps its own
//    action, the wait missing from a chart (drop, then skip), the exit registers, another failing hash, an own
//    actor, probe vs ON;
//  - sar::KeyLog (bounded, no duplicates, several writers).
// They do NOT test the hook, the stack writes, the jump to 0x864B1A, the game's code nor its behaviour.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <thread>
#include <vector>

#include "mod/session2/guest_seq_restore_logic.h"

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

// Fake memory: 8-byte cells at aligned addresses (u32 reads take the low half of their cell).
struct Mem {
    std::map<uint64_t, uint64_t> cells;
    uint64_t next = 0x100000;
    uint64_t steps = 0;  // reads made
    void put64(uint64_t a, uint64_t v) { cells[a] = v; }
    void put32(uint64_t a, uint32_t v) { cells[a] = v; }
    bool r64(uint64_t a, uint64_t& v) const {
        ++const_cast<Mem*>(this)->steps;
        auto it = cells.find(a);
        if (it == cells.end()) return false;
        v = it->second;
        return true;
    }
    bool r32(uint64_t a, uint32_t& v) const {
        uint64_t q = 0;
        if (!r64(a, q)) return false;
        v = static_cast<uint32_t>(q);
        return true;
    }
    uint64_t alloc(uint64_t size) {
        const uint64_t a = next;
        next += (size + 0xFFF) & ~0xFFFull;
        return a;
    }
    // An action definition with its hash at +0x18.
    uint64_t action(uint32_t hash) {
        const uint64_t a = alloc(0x40);
        put32(a + sar::kAction_Hash, hash);
        return a;
    }
    // A chart: count at +0x20, array at +0x18 of {p0, p1, p2} sorted by [p2+0x18].
    uint64_t chart(std::vector<uint32_t> hashes, bool hole_at_first = false) {
        std::sort(hashes.begin(), hashes.end());
        const uint64_t c = alloc(0x40), arr = alloc(hashes.size() * sar::kEntry_Size + 8);
        put64(c + sar::kChart_Array, arr);
        put32(c + sar::kChart_Count, static_cast<uint32_t>(hashes.size()));
        for (size_t i = 0; i < hashes.size(); ++i) {
            const uint64_t e = arr + i * sar::kEntry_Size;
            put64(e, 0x7000 + i);
            put64(e + 8, (hole_at_first && i == 0) ? 0 : 0x8000 + i);
            put64(e + 16, action(hashes[i]));
        }
        return c;
    }
};

std::vector<uint32_t> many(uint32_t n, uint32_t seed) {
    std::vector<uint32_t> v;
    uint32_t x = seed;
    for (uint32_t i = 0; i < n; ++i) {
        x = x * 1664525u + 1013904223u;
        if (x != sar::kHashSeqBodyAnim && x != sar::kHashSeqBodyAnimLoop && x != sar::kHashWait) v.push_back(x);
    }
    return v;
}

constexpr uint32_t kFreesword = 0xB0100F36;  // session 8, sequencer 0x1B0
constexpr uint32_t kHostUser = 0x90100000;
constexpr uint32_t kLocalUser = 0x90100332;

void test_mode() {
    bool bad = false;
    CHECK(sar::parse_mode("0") == sar::Mode::Off);
    CHECK(sar::parse_mode("") == sar::Mode::Off);
    CHECK(sar::parse_mode("probe") == sar::Mode::Probe);
    CHECK(sar::parse_mode("1", &bad) == sar::Mode::On && !bad);
    CHECK(sar::parse_mode("yes", &bad) == sar::Mode::Off && bad);
    CHECK(sar::seq_family(0x837487D4) && sar::seq_family(0x35DD392F) && !sar::seq_family(sar::kHashWait));
    CHECK(sar::key_class_ok(0xB0100F36) && sar::key_class_ok(0xA0100001) && !sar::key_class_ok(0x90100000));
    CHECK(!sar::changes(sar::Verdict::Resolvable) && sar::changes(sar::Verdict::Substituted) &&
          sar::changes(sar::Verdict::Skipped));
}

void test_find() {
    Mem m;
    std::vector<uint32_t> hs = many(27000, 7);
    hs.push_back(sar::kHashWait);
    hs.push_back(0x968C694E);
    const uint64_t c = m.chart(hs);
    CHECK(sar::chart_find(m, c, sar::kHashWait) == sar::Lookup::Found);
    CHECK(sar::chart_find(m, c, 0x968C694E) == sar::Lookup::Found);
    CHECK(sar::chart_find(m, c, sar::kHashSeqBodyAnim) == sar::Lookup::NotFound);
    CHECK(sar::chart_find(m, c, 0) == sar::Lookup::NotFound);           // entries are complete: key 0 never matches
    CHECK(sar::chart_find(m, c, 0xFFFFFFFFu) == sar::Lookup::NotFound);
    // Every key of a small chart, its neighbours not.
    std::vector<uint32_t> small = {10, 20, 30, 40, 50};
    const uint64_t s = m.chart(small);
    for (uint32_t h : small) {
        CHECK(sar::chart_find(m, s, h) == sar::Lookup::Found);
        CHECK(sar::chart_find(m, s, h + 1) == sar::Lookup::NotFound);
    }
    CHECK(sar::chart_find(m, s, 5) == sar::Lookup::NotFound);
    // A binary search: ~log2(n) entries read.
    m.steps = 0;
    sar::chart_find(m, c, sar::kHashSeqBodyAnim);
    CHECK(m.steps < 4 * 20 + 4);
    // No chart, empty chart, unreadable chart, unreadable array.
    CHECK(sar::chart_find(m, 0, sar::kHashWait) == sar::Lookup::NoChart);
    const uint64_t empty = m.chart({});
    CHECK(sar::chart_find(m, empty, sar::kHashWait) == sar::Lookup::NotFound);
    CHECK(sar::chart_find(m, 0xDEAD0000, sar::kHashWait) == sar::Lookup::Unreadable);
    const uint64_t torn = m.alloc(0x40);
    m.put32(torn + sar::kChart_Count, 5);
    m.put64(torn + sar::kChart_Array, 0xBAD00000);
    CHECK(sar::chart_find(m, torn, 20) == sar::Lookup::Unreadable);
    // An entry with a null pointer: 0x24314C0 reads its key as 0 (it is never "found" for a real hash).
    const uint64_t holed = m.chart({10, 20, 30}, true);
    CHECK(sar::chart_find(m, holed, 10) == sar::Lookup::NotFound);
    CHECK(sar::chart_find(m, holed, 30) == sar::Lookup::Found);
    // A huge torn count: bounded steps, never a long loop.
    const uint64_t huge = m.alloc(0x40);
    m.put32(huge + sar::kChart_Count, 0x7FFFFFF0);
    m.put64(huge + sar::kChart_Array, m.cells.begin()->first);
    const sar::Lookup l = sar::chart_find(m, huge, 20);
    CHECK(l == sar::Lookup::Unreadable || l == sar::Lookup::NotFound);
}

struct Case {
    Mem m;
    uint64_t chart0 = 0, chart1 = 0;
    sar::Input in;
    Case(sar::Mode mode, bool wait0 = true, bool wait1 = true) {
        std::vector<uint32_t> h0 = many(2000, 1), h1 = many(400, 2);
        if (wait0) h0.push_back(sar::kHashWait);
        if (wait1) h1.push_back(sar::kHashWait);
        chart0 = m.chart(h0);
        chart1 = m.chart(h1);
        in.mode = mode;
        in.key = kFreesword;
        in.control = kHostUser;
        in.local = kLocalUser;
        in.chart[0] = chart0;
        in.chart[1] = chart1;
        in.hash[0] = sar::kHashSeqBodyAnim;
        in.hash[1] = sar::kHashSeqBodyAnimLoop;
        in.exit_regs_ok = true;
        in.frame_ok = true;
    }
};

void test_session8_case() {
    // Both layers seq_bodyanim, [CC+0x70/+0x78] null, both charts have the wait: both get it.
    Case c(sar::Mode::On);
    const sar::Plan p = sar::plan(c.m, c.in);
    CHECK(p.verdict == sar::Verdict::Substituted);
    CHECK(p.layer[0].fails && p.layer[1].fails);
    CHECK(p.layer[0].in_chart == sar::Lookup::NotFound && p.layer[0].in_fallback == sar::Lookup::NoChart);
    CHECK(p.layer[0].act == sar::Act::Substitute && p.layer[0].new_hash == sar::kHashWait);
    CHECK(p.layer[1].act == sar::Act::Substitute && p.layer[1].new_hash == sar::kHashWait);
    CHECK(p.write[0] && p.write[1] && !p.skip);
    CHECK(p.write_index[0] && p.write_index[1]);  // review 7: the old action's index word -> 0xFFFF
    static_assert(sar::kNoIndex == 0xFFFF, "the constructor's default (0x1EC994F)");
    const sar::LayerCounts lc = sar::layer_counts(p);
    CHECK(lc.substituted == 2 && lc.dropped == 0);
    // probe: the same decision, nothing written, no jump.
    Case q(sar::Mode::Probe);
    const sar::Plan pq = sar::plan(q.m, q.in);
    CHECK(pq.verdict == sar::Verdict::Substituted && !pq.write[0] && !pq.write[1] && !pq.skip);
    CHECK(!pq.write_index[0] && !pq.write_index[1]);
    CHECK(pq.layer[0].act == sar::Act::Substitute);
    // Review 2: an unexpected frame (rbp != rsp + 0x100): untouched, nothing read from the charts.
    Case f(sar::Mode::On);
    f.in.frame_ok = false;
    const sar::Plan pf = sar::plan(f.m, f.in);
    CHECK(pf.verdict == sar::Verdict::BadFrame && !pf.write[0] && !pf.write[1] && !pf.skip);
    // Review 4: the dynamic graph appears between the plan and the write: nothing is written.
    const uint64_t same[2] = {0, 0}, now_installed[2] = {0x5000, 0};
    CHECK(sar::still_valid(p, c.in, same));
    CHECK(!sar::still_valid(p, c.in, now_installed));
}

void test_wait_missing() {
    // Layer 1's chart has no wait: layer 0 gets it, layer 1 goes to 0 (0x863880 skips it).
    Case c(sar::Mode::On, true, false);
    sar::Plan p = sar::plan(c.m, c.in);
    CHECK(p.verdict == sar::Verdict::Substituted);
    CHECK(p.layer[0].act == sar::Act::Substitute && p.layer[1].act == sar::Act::Drop && p.layer[1].new_hash == 0);
    CHECK(p.write[0] && p.write[1] && !p.skip);
    CHECK(p.write_index[0] && !p.write_index[1]);  // a dropped layer is skipped by 0x863880: its word is unread
    sar::LayerCounts lc = sar::layer_counts(p);
    CHECK(lc.substituted == 1 && lc.dropped == 1);
    // Neither has it: both 0 -> the native restore-byte-0 exit.
    Case n(sar::Mode::On, false, false);
    p = sar::plan(n.m, n.in);
    CHECK(p.verdict == sar::Verdict::Skipped && p.skip && !p.write[0] && !p.write[1]);
    lc = sar::layer_counts(p);  // review 8: a skip is not counted as two dropped layers
    CHECK(lc.substituted == 0 && lc.dropped == 0);
    // ... only with the expected exit registers.
    n.in.exit_regs_ok = false;
    p = sar::plan(n.m, n.in);
    CHECK(p.verdict == sar::Verdict::CannotSkip && !p.skip && p.layer[0].act == sar::Act::Keep);
    // probe never jumps.
    n.in.exit_regs_ok = true;
    n.in.mode = sar::Mode::Probe;
    p = sar::plan(n.m, n.in);
    CHECK(p.verdict == sar::Verdict::Skipped && !p.skip);
}

void test_native_paths() {
    Case c(sar::Mode::On);
    // No seq hash at all: the fast path's verdict.
    sar::Input in = c.in;
    in.hash[0] = 0x968C694E;
    in.hash[1] = sar::kHashWait;
    CHECK(sar::plan(c.m, in).verdict == sar::Verdict::NoSeqHash);
    // Review 3: a seq hash on a layer without a chart (never written by 0x8646D0, but 0x863880 reads it always):
    // looked up in the fallback only; no chart for the wait -> 0; both 0 -> skip.
    in = c.in;
    in.chart[0] = in.chart[1] = 0;
    sar::Plan pn = sar::plan(c.m, in);
    CHECK(pn.verdict == sar::Verdict::Skipped);
    CHECK(pn.layer[0].in_chart == sar::Lookup::NoChart && pn.layer[0].wait == sar::Lookup::NoChart);
    // ... the constructor's 0 there (the real case): no seq hash.
    in.hash[0] = in.hash[1] = 0;
    CHECK(sar::plan(c.m, in).verdict == sar::Verdict::NoSeqHash);
    // The own actor (control = this client's user) or a user key: native.
    in = c.in;
    in.control = kLocalUser;
    CHECK(sar::plan(c.m, in).verdict == sar::Verdict::NotRemote);
    in = c.in;
    in.key = 0x90100000;
    CHECK(sar::plan(c.m, in).verdict == sar::Verdict::NotRemote);
    in = c.in;
    in.control = 0;
    CHECK(sar::plan(c.m, in).verdict == sar::Verdict::NotRemote);
    // Review 5: local user not known yet: native, counted apart.
    in = c.in;
    in.local = 0;
    CHECK(sar::plan(c.m, in).verdict == sar::Verdict::LocalUnknown);
    // The host's character (0xA...) in a scene: the same complement.
    in = c.in;
    in.key = 0xA0100001;
    CHECK(sar::plan(c.m, in).verdict == sar::Verdict::Substituted);
    // The scene is playing: BodyAnim's graph [CC+0x70/+0x78] has the hashes -> native.
    in = c.in;
    in.fallback[0] = c.m.chart({sar::kHashSeqBodyAnim, 77});
    in.fallback[1] = c.m.chart({sar::kHashSeqBodyAnimLoop});
    sar::Plan p = sar::plan(c.m, in);
    CHECK(p.verdict == sar::Verdict::Resolvable && !p.write[0] && !p.write[1] && !p.skip);
    CHECK(p.layer[0].in_fallback == sar::Lookup::Found);
    // Only layer 1 resolvable: layer 0 alone gets the wait, layer 1 kept.
    in.fallback[0] = 0;
    p = sar::plan(c.m, in);
    CHECK(p.verdict == sar::Verdict::Substituted && p.layer[0].act == sar::Act::Substitute &&
          p.layer[1].act == sar::Act::Keep && p.write[0] && !p.write[1]);
    // A layer chart that has the seq hash itself: native.
    in = c.in;
    in.chart[0] = c.m.chart({sar::kHashSeqBodyAnim, sar::kHashWait});
    in.hash[1] = 0;
    CHECK(sar::plan(c.m, in).verdict == sar::Verdict::Resolvable);
}

void test_mixed() {
    Case c(sar::Mode::On);
    // Layer 0 seq_bodyanim (fails), layer 1 a normal action its chart has: layer 0 -> wait, layer 1 kept.
    sar::Input in = c.in;
    in.chart[1] = c.m.chart({0xDA390291, sar::kHashWait});
    in.hash[1] = 0xDA390291;
    sar::Plan p = sar::plan(c.m, in);
    CHECK(p.verdict == sar::Verdict::Substituted && p.layer[1].act == sar::Act::Keep && !p.layer[1].fails);
    // Layer 1 fails with a hash that is NOT seq_bodyanim: everything untouched (the native error).
    in.hash[1] = 0x12345678;
    p = sar::plan(c.m, in);
    CHECK(p.verdict == sar::Verdict::OtherFailure && !p.write[0] && !p.write[1] && !p.skip);
    CHECK(p.layer[0].act == sar::Act::Keep);
    // Layer 0 seq (fails, no wait in its chart -> 0), layer 1 hash 0: both 0 -> skip.
    Case n(sar::Mode::On, false, true);
    in = n.in;
    in.hash[1] = 0;
    p = sar::plan(n.m, in);
    CHECK(p.verdict == sar::Verdict::Skipped && p.skip);
    // Layer 0 seq with wait, layer 1 has no chart but a seq hash: layer 0 -> wait, layer 1 -> 0.
    in = n.in;
    in.chart[0] = n.chart1;  // has the wait
    in.chart[1] = 0;
    p = sar::plan(n.m, in);
    CHECK(p.verdict == sar::Verdict::Substituted && p.write[0] && p.write[1] && p.layer[1].act == sar::Act::Drop);
    // ... and with the constructor's 0 on layer 1: layer 0 alone.
    in.hash[1] = 0;
    p = sar::plan(n.m, in);
    CHECK(p.verdict == sar::Verdict::Substituted && p.write[0] && !p.write[1]);
    // An unreadable chart: untouched.
    in = n.in;
    in.chart[1] = 0xDEAD0000;
    p = sar::plan(n.m, in);
    CHECK(p.verdict == sar::Verdict::Unreadable && !p.write[0] && !p.write[1] && !p.skip);
}

void test_shared_sites() {
    // Review 1: 0x8646FC (guest_actor_diag's "before" point) and 0x864B1D (its exit point).
    const uint8_t regs[] = {0x48, 0x8B, 0xF2, 0x4C, 0x8B, 0xE9};
    const uint64_t lo = 0x140000000, hi = lo + 0x7000000, site = lo + 0x8646FC;
    CHECK(sar::classify_shared(regs, regs, site, lo, hi) == sar::Shared::Original);
    // SafetyHook's jmp to a trampoline outside the image (+ the NOP filler).
    const uint64_t tramp = site + 5 + 0x7FFF0000ull;  // within rel32 reach, above the image
    const auto r = static_cast<uint32_t>(tramp - (site + 5));
    const uint8_t hooked[] = {0xE9, static_cast<uint8_t>(r), static_cast<uint8_t>(r >> 8), static_cast<uint8_t>(r >> 16),
                              static_cast<uint8_t>(r >> 24), 0x90};
    CHECK(sar::classify_shared(hooked, regs, site, lo, hi) == sar::Shared::ForeignHook);
    // A jmp into the image is not a mid hook: refused. So is any other change.
    const uint8_t inside[] = {0xE9, 0x10, 0x00, 0x00, 0x00, 0x90};
    CHECK(sar::classify_shared(inside, regs, site, lo, hi) == sar::Shared::Mismatch);
    const uint8_t other[] = {0x48, 0x8B, 0xF2, 0x4C, 0x8B, 0xE8};
    CHECK(sar::classify_shared(other, regs, site, lo, hi) == sar::Shared::Mismatch);
    const uint8_t shorter[] = {0x48, 0x8B, 0xF2};
    CHECK(sar::classify_shared(shorter, regs, site, lo, hi) == sar::Shared::Mismatch);
}

void test_keylog() {
    sar::KeyLog<4> k;
    CHECK(k.add(1) && !k.add(1) && k.add(2) && k.add(3) && k.add(4));
    CHECK(!k.add(5) && k.full() == 1 && k.size() == 4);
    CHECK(!k.add(0));
    CHECK(k.at(0) == 1 && k.at(3) == 4 && k.at(9) == 0);
    // Several writers: bounded, every remembered key is one that was added.
    sar::KeyLog<64> big;
    std::vector<std::thread> ts;
    for (uint32_t t = 0; t < 4; ++t) {
        ts.emplace_back([&big, t] {
            for (uint32_t i = 1; i <= 40; ++i) big.add(t * 1000 + i);
        });
    }
    for (auto& t : ts) t.join();
    CHECK(big.size() == 64);
    for (size_t i = 0; i < big.size(); ++i) {
        const uint32_t key = big.at(i);
        CHECK(key % 1000 >= 1 && key % 1000 <= 40 && key / 1000 < 4);
    }
}

}  // namespace

int main() {
    test_mode();
    test_find();
    test_session8_case();
    test_wait_missing();
    test_native_paths();
    test_mixed();
    test_shared_sites();
    test_keylog();
    if (g_failures) {
        std::printf("optd_seq_restore: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_seq_restore: all passed\n");
    return 0;
}
