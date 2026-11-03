// P8 guest, the host's field actors (src/mod/session2/guest_actor_diag_logic.h,
// docs/research/pendientes/p8-actores-anfitrion.md §3, §4.1(i), §4.4): the
// pure logic of the guest's actor diagnostic and layer guard. The hooks
// themselves (guest_actor_diag.cpp) need the game.

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <thread>
#include <vector>

#include "mod/session2/guest_actor_diag_logic.h"

using namespace cdmp::session2::gad;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

constexpr uint32_t kLocal = 0x9010022A;  // the guest's user (P8 session 1)
constexpr uint32_t kHost = 0x90100000;   // the host's user: controls its field's NPCs

void test_remote_predicate() {
    // The host's NPCs, its Kliff and companion: not controlled here.
    CHECK(remote_controlled(kHost, kLocal));
    // Unowned/uncontrolled (+0x90 = 0) is not the local user either.
    CHECK(remote_controlled(0, kLocal));
    // The guest's own character and children: never touched.
    CHECK(!remote_controlled(kLocal, kLocal));
    // Local user unknown: nothing is remote (no guard, no probe lines).
    CHECK(!remote_controlled(kHost, 0));
    CHECK(!remote_controlled(0, 0));
    static_assert(remote_controlled(kHost, kLocal) && !remote_controlled(kLocal, kLocal));
}

// S34 guest_remote_update_guard (horse_diag_install_remote): who is contained and when.
void test_remote_update_guard() {
    // Shadow S34: NPC 0xB01004A5 controlled by the host, on the guest 0x9010029A.
    constexpr uint32_t kShadow = 0x9010029A;
    CHECK(remote_user_controlled(kHost, kShadow));
    CHECK(!remote_user_controlled(kShadow, kShadow));  // the local player and its children: never
    CHECK(!remote_user_controlled(0, kShadow));        // +0x90 = 0 (nobody): left alone, unlike remote_controlled
    CHECK(remote_controlled(0, kShadow));
    CHECK(!remote_user_controlled(kHost, 0));          // local key unknown: nothing
    CHECK(!remote_user_controlled(0, 0));

    // A layer: chart +0x110, current action +0x10, playback +0x18.
    constexpr uintptr_t kP = 0x36328B8D940;
    CHECK(layer_lacks_action(kP, 0, 0));    // the S34 state: chart, neither current nor playback
    CHECK(layer_lacks_action(kP, 0, kP));   // chart, playback, no current (0x232D5FF / 0x88F832)
    CHECK(layer_lacks_action(kP, kP, 0));   // chart, current, no playback (0x1F63FE6)
    CHECK(!layer_lacks_action(kP, kP, kP)); // has its action
    CHECK(!layer_lacks_action(0, 0, 0));    // no chart: an empty layer is not this state
    CHECK(!layer_lacks_action(0, kP, 0));

    // The decision: another user's actor AND a layer without action.
    CHECK(contain_remote_update(kHost, kShadow, true, false));
    CHECK(contain_remote_update(kHost, kShadow, false, true));  // layer 1 only (the enemy copy of 2026-10-04)
    CHECK(contain_remote_update(kHost, kShadow, true, true));
    CHECK(!contain_remote_update(kHost, kShadow, false, false));  // it has its action: never skipped
    CHECK(!contain_remote_update(kShadow, kShadow, true, true));  // local: never, even without action
    CHECK(!contain_remote_update(0, kShadow, true, true));
    CHECK(!contain_remote_update(kHost, 0, true, true));
    CHECK(contain_remote_update(0x901002BF, kShadow, true, false));  // any other user, not only the host
    static_assert(contain_remote_update(kHost, kLocal, true, false) && !contain_remote_update(kLocal, kLocal, true, true));

    // The first keys, in order, each once; full after N; a 0 key is refused.
    FirstPairs<4> first;
    std::vector<std::pair<uint32_t, uint32_t>> got;
    auto sink = [&](uint32_t k, uint32_t v, size_t) { got.emplace_back(k, v); };
    first.drain(sink);
    CHECK(got.empty());
    CHECK(!first.add(0, kHost));
    CHECK(first.add(0xB01004A5, kHost));
    CHECK(first.add(0xB01004A4, kShadow));
    first.drain(sink);
    CHECK(got.size() == 2 && got[0] == std::make_pair(0xB01004A5u, kHost) && got[1] == std::make_pair(0xB01004A4u, kShadow));
    first.drain(sink);
    CHECK(got.size() == 2);  // nothing new: nothing logged twice
    CHECK(first.add(3, kHost) && first.add(4, kHost));
    CHECK(!first.add(5, kHost));  // full
    first.drain(sink);
    CHECK(got.size() == 4 && got[3].first == 4 && first.drained() == 4);

    // Many threads: every kept key exactly once.
    FirstPairs<32> many;
    std::vector<std::thread> ts;
    for (uint32_t t = 0; t < 4; ++t) {
        ts.emplace_back([&many, t] {
            for (uint32_t i = 1; i <= 20; ++i) many.add(t * 100 + i, t);
        });
    }
    for (auto& t : ts) t.join();
    std::map<uint32_t, uint32_t> seen;
    many.drain([&](uint32_t k, uint32_t v, size_t) { ++seen[k]; CHECK(v == k / 100); });
    CHECK(seen.size() == 32);
    for (const auto& [k, n] : seen) CHECK(n == 1);
}

Section sec(const char* name, uint32_t va, uint32_t size) {
    Section s;
    std::memcpy(s.name, name, std::min<size_t>(std::strlen(name), 8));
    s.va = va;
    s.size = size;
    return s;
}

void test_sections() {
    // Build 2976's layout (the read-only copy's PE header).
    const std::vector<Section> secs = {sec(".rsrc", 0x1000, 0x51EA000), sec(".xdata", 0x51EB000, 0x16A8000),
                                       sec(".rdata", 0x6893000, 0x854000), sec(".xtls", 0x75C9000, 0xF9466E2),
                                       sec(".text", 0x16F10000, 0x1C3986)};
    for (const uint32_t at : {0x4FD491u, 0x8646FCu, 0x864B1Du, 0x1F63F70u, 0x2329700u}) {
        CHECK(in_real_code(secs, at, 16));
        CHECK(std::string_view(section_of(secs, at)->name) == ".rsrc");
    }
    // Denuvo's .xtls and the other sections: refused.
    CHECK(!in_real_code(secs, 0x984D830, 5));   // ClientCharacterControlActorComponent::vf19 (.xtls)
    CHECK(!in_real_code(secs, 0x16F10010, 5));  // .text
    CHECK(!in_real_code(secs, 0x6CF6C50, 4));   // a global in .rdata
    // Straddling the end of .rsrc, outside every section, or empty: refused.
    CHECK(!in_real_code(secs, 0x51EA000 + 0x1000 - 3, 5));
    CHECK(in_real_code(secs, 0x51EA000 + 0x1000 - 5, 5));
    CHECK(!in_real_code(secs, 0x500, 5));
    CHECK(!in_real_code(secs, 0x4FD491, 0));
    CHECK(section_of(secs, 0x500) == nullptr);
    // A section that is merely named like it (no exact match) is not .rsrc.
    const std::vector<Section> odd = {sec(".rsrc2", 0x1000, 0x1000)};
    CHECK(!in_real_code(odd, 0x1100, 5));
}

void test_line_budget() {
    LineBudget b(3);
    CHECK(b.take() && b.take() && b.take());
    CHECK(!b.take() && !b.take());
    CHECK(b.asked() == 5 && b.suppressed() == 2 && b.cap() == 3);
    LineBudget none(0);
    CHECK(!none.take() && none.suppressed() == 1);
}

void test_key_set() {
    KeySet<8> s;
    using I = KeySet<8>::Insert;
    CHECK(s.insert(0xB0100415) == I::Added);
    CHECK(s.insert(0xB0100415) == I::Present);
    CHECK(s.contains(0xB0100415) && !s.contains(0xB0100416));
    CHECK(s.insert(0) == I::Present && !s.contains(0));  // 0 is never a key
    for (uint32_t k = 1; k < 8; ++k) CHECK(s.insert(0xB0100000 + k) == I::Added);
    CHECK(s.size() == 8);
    CHECK(s.insert(0xA0100001) == I::Full);
    CHECK(s.insert(0xB0100003) == I::Present);  // still found when full
    CHECK(!s.contains(0xA0100001));

    // Several game threads at once: every key counted once.
    KeySet<4096> big;
    std::vector<std::thread> threads;
    std::atomic<int> added{0};
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            for (uint32_t k = 1; k <= 1000; ++k) {
                if (big.insert(0xB0100000 + k) == KeySet<4096>::Insert::Added) added.fetch_add(1);
            }
        });
    }
    for (auto& t : threads) t.join();
    CHECK(added.load() == 1000 && big.size() == 1000);
}

WaitEntry entry(uint32_t key, bool found, uint8_t c2 = 0, uint8_t cf = 0) {
    WaitEntry e;
    e.key = key;
    e.found = found;
    e.c2 = c2;
    e.cf = cf;
    return e;
}

void test_wait_blocker() {
    CHECK(entry_ready(entry(1, true, 1, 1)));
    CHECK(!entry_ready(entry(1, true, 1, 0)));  // +0xCF never set: a failed loginCharacter
    CHECK(!entry_ready(entry(1, true, 0, 1)));
    CHECK(!entry_ready(entry(1, false, 1, 1)));
    // Empty list: nothing to wait for.
    CHECK(wait_blocker({}) == -1);
    // Missing (B0100002, never created) and ready keys go away: the list empties.
    const WaitEntry clear[] = {entry(0xB0100002, false), entry(0xA010022B, true, 1, 1), entry(0xB0100003, false)};
    CHECK(wait_blocker(clear) == -1);
    // The front is checked first.
    const WaitEntry front[] = {entry(0xB0100010, true, 1, 0), entry(0xB0100011, true, 1, 0)};
    CHECK(wait_blocker(front) == 0);
    // Swap-remove: after the front goes, the LAST key moves to the front, so
    // the key the state waits for is the last one, not the second.
    const WaitEntry swap[] = {entry(0xB0100002, false), entry(0xB0100011, true, 1, 0), entry(0xB0100012, true, 1, 0)};
    CHECK(wait_blocker(swap) == 2);
    // ... and once that one is ready, the order goes on from the back.
    const WaitEntry swap2[] = {entry(0xB0100002, false), entry(0xB0100011, true, 1, 0), entry(0xB0100012, true, 1, 1)};
    CHECK(wait_blocker(swap2) == 1);
    // One key, never ready: the eternal tunnel of S1.
    const WaitEntry stuck[] = {entry(0xB0100099, true, 1, 0)};
    CHECK(wait_blocker(stuck) == 0);
}

// Fake game memory for ClientActorContainer::Find.
struct FakeMem {
    std::map<uintptr_t, uint64_t> q;  // 8-byte cells, also read as u32 (little endian, low half)
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

void test_container_find() {
    using L = ContainerLayout;
    constexpr uintptr_t C = 0x10000, kArray = 0x20000, kEntries = 0x30000, kEntry = 0x40000;
    constexpr uint32_t kBuckets = 7;
    constexpr uint32_t key = 0xB0100415;
    const uintptr_t bucket = kArray + (key % kBuckets) * L::kBucketSize;
    FakeMem m;
    m.d[C + L::kUsed] = 1;
    m.d[C + L::kBuckets] = kBuckets;
    m.q[C + L::kBucketArray] = kArray;
    m.q[C + L::kEntries] = kEntries;
    // The bucket: another key first (same bucket), then ours at index 5.
    m.d[bucket] = 2;
    m.d[bucket + 8] = key + kBuckets;
    m.d[bucket + 0xC] = 4;
    m.d[bucket + 0x10] = key;
    m.d[bucket + 0x14] = 5;
    m.q[kEntries + 8 * 5] = kEntry;
    m.d[kEntry + L::kEntryKey] = key;
    m.q[kEntry + L::kEntryActor] = 0x7FF612345678;
    CHECK(container_find(m, C, key) == 0x7FF612345678);
    // A key that is not there: none (Find gives null, the state drops the key).
    CHECK(container_find(m, C, key + 1) == 0);
    // The entry names another key: skipped, as 0x4642E7 does.
    m.d[kEntry + L::kEntryKey] = key ^ 1;
    CHECK(container_find(m, C, key) == 0);
    m.d[kEntry + L::kEntryKey] = key;
    // An unreadable entry pointer, an empty or unused container, garbage counts: none, never a crash.
    m.q.erase(kEntries + 8 * 5);
    CHECK(container_find(m, C, key) == 0);
    m.q[kEntries + 8 * 5] = kEntry;
    m.d[bucket] = 40;  // > 31 pairs fit in 0x100 bytes
    CHECK(container_find(m, C, key) == 0);
    m.d[bucket] = 2;
    m.d[C + L::kUsed] = 0;
    CHECK(container_find(m, C, key) == 0);
    m.d[C + L::kUsed] = 1;
    m.d[C + L::kBuckets] = 0;
    CHECK(container_find(m, C, key) == 0);
    m.d[C + L::kBuckets] = kBuckets;
    CHECK(container_find(m, 0, key) == 0);
    CHECK(container_find(m, C, key) == 0x7FF612345678);
}

// The walk of session 5's wait list (193 keys): the header is read once
// (container_view) and every key gives what container_find gives (p8-hilo-red.md).
struct CountingMem {
    const FakeMem& m;
    mutable size_t reads = 0;
    mutable size_t header_reads = 0;
    uintptr_t c = 0;
    bool u32(uintptr_t a, uint32_t& v) const {
        ++reads;
        if (a >= c && a < c + 0x100) ++header_reads;
        return m.u32(a, v);
    }
    bool ptr(uintptr_t a, uintptr_t& v) const {
        ++reads;
        if (a >= c && a < c + 0x100) ++header_reads;
        return m.ptr(a, v);
    }
};

void test_container_view() {
    using L = ContainerLayout;
    constexpr uintptr_t C = 0x10000, kArray = 0x20000, kEntries = 0x30000, kEntryBase = 0x100000;
    constexpr uint32_t kBuckets = 61, kKeys = 193;
    FakeMem m;
    m.d[C + L::kUsed] = kKeys;
    m.d[C + L::kBuckets] = kBuckets;
    m.q[C + L::kBucketArray] = kArray;
    m.q[C + L::kEntries] = kEntries;
    std::vector<uint32_t> keys;
    for (uint32_t i = 0; i < kKeys; ++i) {
        const uint32_t key = 0xB0100000 + 3 * i;
        keys.push_back(key);
        const uintptr_t bucket = kArray + (key % kBuckets) * L::kBucketSize;
        const uint32_t n = m.d[bucket]++;
        m.d[bucket + 8 + 8 * n] = key;
        m.d[bucket + 0xC + 8 * n] = i;
        const uintptr_t entry = kEntryBase + 0x20 * i;
        m.q[kEntries + 8 * i] = entry;
        m.d[entry + L::kEntryKey] = key;
        // Every third actor never finished loading: its entry has no actor pointer.
        if (i % 3) m.q[entry + L::kEntryActor] = 0x7FF600000000 + i;
    }
    CountingMem per_key{m};
    per_key.c = C;
    CountingMem once{m};
    once.c = C;
    const ContainerView view = container_view(once, C);
    CHECK(view.ok && view.buckets == kBuckets && view.array == kArray && view.entries == kEntries);
    const size_t header_after_view = once.header_reads;
    size_t found = 0;
    for (const uint32_t key : keys) {
        const uintptr_t a = container_find(per_key, C, key);
        CHECK(container_find_in(once, view, key) == a);
        if (a) ++found;
    }
    CHECK(found == kKeys - (kKeys + 2) / 3);
    CHECK(container_find_in(once, view, 0xB0100001) == 0);  // not in the container
    // The header: 4 reads once, against 4 per key before.
    CHECK(header_after_view == 4);
    CHECK(once.header_reads == header_after_view);
    CHECK(per_key.header_reads == 4 * kKeys);
    // An unusable header gives a view that finds nothing (as Find gives null), never a read beyond it.
    FakeMem empty = m;
    empty.d[C + L::kUsed] = 0;
    CHECK(!container_view(empty, C).ok);
    CHECK(container_find_in(empty, container_view(empty, C), keys[1]) == 0);
    FakeMem no_entries = m;
    no_entries.q.erase(C + L::kEntries);
    CHECK(!container_view(no_entries, C).ok);
    CHECK(!container_view(m, 0).ok);
    CHECK(container_find_in(m, ContainerView{}, keys[1]) == 0);
}

}  // namespace

int main() {
    test_remote_predicate();
    test_remote_update_guard();
    test_sections();
    test_line_budget();
    test_key_set();
    test_wait_blocker();
    test_container_find();
    test_container_view();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("optd guest actor tests: all passed\n");
    return 0;
}
