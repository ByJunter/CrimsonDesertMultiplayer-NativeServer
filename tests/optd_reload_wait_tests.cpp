// P8 session 15, guest_reload_wait_fix (src/mod/session2/guest_reload_wait_logic.h,
// docs/research/pendientes/p8-s15-viaje-tunel.md §8): the pure logic. The hook, the lock and the game's
// behaviour need the game.

#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/guest_reload_wait_logic.h"

using namespace cdmp::session2;
using namespace cdmp::session2::grw;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

constexpr uint32_t kGuestUser = 0x9010046A;  // S15: the guest's user
constexpr uint32_t kHostUser = 0x90100000;   // the host's user: controls its field's NPCs
constexpr uint32_t kGuestKliff = 0xA010046B;
constexpr uint32_t kA3 = 0xB01000A3, k12D = 0xB010012D;  // the two S15 blockers (+5E 8, C2 0, CF 0)

void test_trip_clock() {
    TripClock t;
    // The title -> 0x18 is not a trip; 0x18 -> 0x01 (the 2283) begins one.
    auto c = t.on_state(-1, 0x18, 10);
    CHECK(!c.began && !t.in_trip());
    c = t.on_state(0x18, 0x01, 100);
    CHECK(c.began && t.in_trip() && t.trip() == 1 && t.start_us() == 100);
    t.on_state(0x01, 0x04, 110);
    t.on_state(0x04, 0x11, 200);
    CHECK(!t.wait_since_us());
    c = t.on_state(0x11, 0x12, 300);
    CHECK(c.entered_wait && t.wait_since_us() == 300 && !c.began);
    c = t.on_state(0x12, 0x13, 400);
    CHECK(c.left_wait && !t.wait_since_us());
    c = t.on_state(0x17, 0x18, 500);
    CHECK(c.ended && !t.in_trip());
    // The same value again is no change.
    c = t.on_state(0x18, 0x18, 600);
    CHECK(!c.began && !c.ended && t.trip() == 1);
    c = t.on_state(0x18, 0x04, 700);
    CHECK(c.began && t.trip() == 2 && t.start_us() == 700);
}

void test_trip_clock_2283() {
    using O = TripClock::Origin;
    // The 2283 seen while still in 0x18: the trip begins there; the exit from 0x18 is the same trip.
    TripClock t;
    t.on_state(-1, 0x18, 10);
    CHECK(t.on_2283(100) && t.in_trip() && t.trip() == 1 && t.start_us() == 100 && t.origin() == O::Req2283);
    auto c = t.on_state(0x18, 0x01, 150);
    CHECK(!c.began && t.trip() == 1 && t.start_us() == 100);
    // A second 2283 inside the trip changes nothing.
    CHECK(!t.on_2283(200) && t.trip() == 1);
    c = t.on_state(0x17, 0x18, 900);
    CHECK(c.ended && !t.in_trip());
    // The exit from 0x18 first (the usual order): the 2283 right after is the same trip.
    c = t.on_state(0x18, 0x01, 1'000);
    CHECK(c.began && t.trip() == 2 && t.origin() == O::StateExit);
    CHECK(!t.on_2283(1'010) && t.trip() == 2);
    t.on_state(0x01, 0x18, 2'000);
    // A 2283 whose load never starts: no end while in 0x18, and the real exit much later is a new trip.
    CHECK(t.on_2283(3'000) && t.trip() == 3);
    c = t.on_state(0x18, 0x18, 4'000);
    CHECK(!c.ended && t.in_trip());
    c = t.on_state(0x18, 0x01, 3'000 + kStaleBeginUs + 1);
    CHECK(c.began && t.trip() == 4 && t.origin() == O::StateExit && t.start_us() == 3'000 + kStaleBeginUs + 1);
    // A 2283 after a missed 0x18 exit (the mod saw the machine first mid-load): begins at once.
    TripClock late;
    late.on_state(-1, 0x09, 10);
    CHECK(!late.in_trip() && late.on_2283(20) && late.in_trip());
    c = late.on_state(0x09, 0x0A, 30);
    CHECK(!c.began && !c.ended);
    c = late.on_state(0x17, 0x18, 40);
    CHECK(c.ended && !late.in_trip());
}

void test_reload_trip() {
    // S15: 2283 at t=174.099 (trip start), 2643 at 176.121, no 2174 since the entry at 45.457.
    CHECK(is_reload_trip(174'099'000, 176'121'000, 45'457'000));
    // An entry: its 2174 after the trip began.
    CHECK(!is_reload_trip(45'000'000, 45'500'000, 45'457'000));
    CHECK(!is_reload_trip(45'000'000, 0, 45'457'000));
    // A 2643 of an older trip is not this trip's.
    CHECK(!is_reload_trip(200'000'000, 176'121'000, 45'457'000));
    // Not in a trip at all.
    CHECK(!is_reload_trip(0, 176'121'000, 0));
}

// P8 S27 (docs/research/s25/reaparicion.md): the guest's log-only warning. Trip 4 of the session (the revive): 0x10 ->
// 0x11 at t=1207.718 with own M+0x231 1, still there 40 s later; trip 3 (the fast travel with our 2322): 0x11 with
// M+0x231 2, left in the same pass.
void test_waiting_own_2322() {
    const uint64_t start = 1'205'690'570, last_2643 = 1'206'834'000, last_2174 = 58'590'000;
    const bool reload = is_reload_trip(start, last_2643, last_2174);
    CHECK(reload);
    CHECK(!waiting_own_2322(0x11, reload, 1, 5'006'000));   // the first [still] line (5 s): not yet
    CHECK(waiting_own_2322(0x11, reload, 1, 10'018'000));   // the second (10 s): warned
    CHECK(waiting_own_2322(0x11, reload, 0, 40'070'000));
    CHECK(!waiting_own_2322(0x11, reload, 2, 40'070'000));  // its 2322 came (trip 3)
    CHECK(!waiting_own_2322(0x11, reload, 3, 40'070'000));
    CHECK(!waiting_own_2322(0x11, reload, 0xFF, 40'070'000));  // unreadable: nothing said
    CHECK(!waiting_own_2322(0x11, false, 1, 40'070'000));      // an entry (2174): observer_milestone's path
    CHECK(!waiting_own_2322(0x12, reload, 1, 40'070'000));     // 0x12 is the wait list's (its own lines)
    CHECK(!waiting_own_2322(0x10, reload, 1, 40'070'000));
}

Front blocker(uint32_t key, uintptr_t actor) {
    Front f;
    f.key = key;
    f.actor = actor;
    f.state = 0x0008;
    f.c2 = 0;
    f.cf = 0;
    f.control = kHostUser;
    f.vf_native = true;
    return f;
}

Context reload_ctx() {
    Context c;
    c.reload = true;
    c.in_wait_us = 3'000'000;
    c.grace_us = 2'000'000;
    c.local_user = kGuestUser;
    c.own_key = kGuestKliff;
    c.dropped = 0;
    c.cap = 16;
    return c;
}

void test_judge() {
    Snapshot<64> snap;
    snap.begin(174'099'000);
    snap.add({k12D, 0x0008, false, 0x5000});
    snap.add({kA3, 0x0008, false, 0x6000});
    snap.add({0xB0100100, 0x0010, true, 0x7000});
    CHECK(snap.size() == 3 && snap.not_ready() == 2);
    const Context c = reload_ctx();
    // The S15 blocker: every condition holds.
    CHECK(judge(blocker(k12D, 0x5000), snap.find(k12D), c) == Verdict::Drop);
    CHECK(judge(blocker(kA3, 0x6000), snap.find(kA3), c) == Verdict::Drop);
    // Nothing to do: empty list, missing actor, ready actor (0x12 removes those natively).
    CHECK(judge(Front{}, nullptr, c) == Verdict::NoFront);
    CHECK(judge(blocker(k12D, 0), snap.find(k12D), c) == Verdict::Missing);
    Front ready = blocker(k12D, 0x5000);
    ready.c2 = 1;
    ready.cf = 1;
    CHECK(judge(ready, snap.find(k12D), c) == Verdict::Ready);
    // Half ready is not ready (vf1D8: +0xC2 && +0xCF).
    Front half = blocker(k12D, 0x5000);
    half.c2 = 1;
    CHECK(judge(half, snap.find(k12D), c) == Verdict::Drop);
    // Not a reload (an entry with its 2174): never.
    Context entry = c;
    entry.reload = false;
    CHECK(judge(blocker(k12D, 0x5000), snap.find(k12D), entry) == Verdict::NotReload);
    // The guest's own character, any character, any user key: never.
    Front own = blocker(kGuestKliff, 0x8000);
    CHECK(judge(own, nullptr, c) == Verdict::OwnCharacter);
    Front host_kliff = blocker(0xA0100001, 0x9000);
    CHECK(judge(host_kliff, nullptr, c) == Verdict::NotWorldActor);
    // The guest's user key unknown, an actor this guest controls, an uncontrolled one.
    Context no_local = c;
    no_local.local_user = 0;
    CHECK(judge(blocker(k12D, 0x5000), snap.find(k12D), no_local) == Verdict::LocalUnknown);
    Front mine = blocker(k12D, 0x5000);
    mine.control = kGuestUser;
    CHECK(judge(mine, snap.find(k12D), c) == Verdict::ControlLocal);
    Front nobody = blocker(k12D, 0x5000);
    nobody.control = 0;
    CHECK(judge(nobody, snap.find(k12D), c) == Verdict::ControlNotUser);
    // Logged in (+0x5E 0x10) but CF 0 (a scene it waits for): native business.
    Front logged = blocker(k12D, 0x5000);
    logged.state = 0x0010;
    CHECK(judge(logged, snap.find(k12D), c) == Verdict::LoggedIn);
    // A subclass whose readiness is not 0x4FE240's.
    Front other_vf = blocker(k12D, 0x5000);
    other_vf.vf_native = false;
    CHECK(judge(other_vf, snap.find(k12D), c) == Verdict::VfOther);
    // A key that became not ready during the trip: not in the snapshot, or ready in it.
    CHECK(judge(blocker(0xB01007C6, 0xA000), snap.find(0xB01007C6), c) == Verdict::NotInSnapshot);
    CHECK(judge(blocker(0xB0100100, 0x7000), snap.find(0xB0100100), c) == Verdict::WasReady);
    // Same key, another actor (re-announced during the trip).
    CHECK(judge(blocker(k12D, 0x5100), snap.find(k12D), c) == Verdict::OtherInstance);
    // The native first: 2 s in 0x12.
    Context early = c;
    early.in_wait_us = 1'999'999;
    CHECK(judge(blocker(k12D, 0x5000), snap.find(k12D), early) == Verdict::Grace);
    // The per-trip cap.
    Context capped = c;
    capped.dropped = 16;
    CHECK(judge(blocker(k12D, 0x5000), snap.find(k12D), capped) == Verdict::CapReached);
    for (int i = 0; i < static_cast<int>(Verdict::kCount); ++i) {
        CHECK(verdict_text(static_cast<Verdict>(i))[0] != '?');
    }
}

void test_snapshot_bounds() {
    Snapshot<4> s;
    s.begin(1);
    for (uint32_t i = 0; i < 6; ++i) s.add({0xB0000000 + i, 8, false, 0x1000 + i});
    CHECK(s.size() == 4 && s.overflow() == 2);
    CHECK(s.find(0xB0000003) && !s.find(0xB0000004));
    CHECK(s.at(2).key == 0xB0000002);
    s.begin(2);
    CHECK(s.size() == 0 && !s.find(0xB0000000) && s.taken_us() == 2);
}

// The wait list in fake memory: C+0x78 -> keys, C+0x80 count.
struct ListMem {
    std::map<uintptr_t, uint32_t> d;
    std::map<uintptr_t, uintptr_t> q;
    bool fail_writes = false;
    bool u32(uintptr_t a, uint32_t& v) const {
        auto it = d.find(a);
        if (it == d.end()) return false;
        v = it->second;
        return true;
    }
    bool ptr(uintptr_t a, uintptr_t& v) const {
        auto it = q.find(a);
        if (it == q.end()) return false;
        v = it->second;
        return true;
    }
    bool put32(uintptr_t a, uint32_t v) {
        if (fail_writes || !d.count(a)) return false;
        d[a] = v;
        return true;
    }
};

constexpr uintptr_t C = 0x10000, K = 0x20000;

ListMem list_of(const std::vector<uint32_t>& keys) {
    ListMem m;
    m.q[C + WaitListLayout::kKeys] = K;
    m.d[C + WaitListLayout::kCount] = static_cast<uint32_t>(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) m.d[K + 4 * i] = keys[i];
    if (keys.empty()) m.d[K] = 0;
    return m;
}

std::vector<uint32_t> keys_of(const ListMem& m) {
    std::vector<uint32_t> out;
    const uint32_t n = m.d.at(C + WaitListLayout::kCount);
    for (uint32_t i = 0; i < n; ++i) out.push_back(m.d.at(K + 4 * i));
    return out;
}

void test_swap_remove() {
    // 0x779EF7..0x779F0B: the last key moves into slot 0, the count goes down by one.
    ListMem m = list_of({0xB1, 0xB2, 0xB3, 0xB4});
    CHECK(swap_remove_front(m, C, 0xB1));
    CHECK((keys_of(m) == std::vector<uint32_t>{0xB4, 0xB2, 0xB3}));
    // The last key alone: only the count (n - 1 == 0 skips the move, as 0x779F00 je).
    ListMem one = list_of({0xB9});
    CHECK(swap_remove_front(one, C, 0xB9));
    CHECK(one.d.at(C + WaitListLayout::kCount) == 0 && one.d.at(K) == 0xB9);
    // Refused without writing: empty list, another front key, a failed read, a failed write, a garbage count.
    ListMem empty = list_of({});
    CHECK(!swap_remove_front(empty, C, 0xB1));
    ListMem other = list_of({0xB1, 0xB2});
    CHECK(!swap_remove_front(other, C, 0xB2));
    CHECK((keys_of(other) == std::vector<uint32_t>{0xB1, 0xB2}));
    ListMem torn = list_of({0xB1, 0xB2, 0xB3});
    torn.d.erase(K + 8);
    CHECK(!swap_remove_front(torn, C, 0xB1));
    CHECK(torn.d.at(C + WaitListLayout::kCount) == 3 && torn.d.at(K) == 0xB1);
    ListMem ro = list_of({0xB1, 0xB2});
    ro.fail_writes = true;
    CHECK(!swap_remove_front(ro, C, 0xB1));
    ListMem garbage = list_of({0xB1});
    garbage.d[C + WaitListLayout::kCount] = 0x7FFFFFFF;
    CHECK(!swap_remove_front(garbage, C, 0xB1));
    CHECK(!swap_remove_front(m, 0, 0xB4));
}

// The S15 list at 0x12 (217 keys: 2 blockers from before the trip at #100 and #173, the rest missing or ready),
// replayed with 0x12 (gad::scan_wait_list) and the drop: the state reaches 0x13 after exactly 2 drops.
void test_s15_replay() {
    std::vector<uint32_t> keys;
    std::map<uint32_t, gad::WaitEntry> actors;
    for (uint32_t i = 0; i < 217; ++i) {
        const uint32_t k = i == 100 ? kA3 : i == 173 ? k12D : 0xB0100500 + i;
        keys.push_back(k);
        gad::WaitEntry e;
        e.key = k;
        e.found = i % 4 != 0;  // 53 missing in the S15 line; here about a quarter
        e.state = 0x10;
        e.c2 = e.cf = 1;
        if (k == kA3 || k == k12D) {
            e.found = true;
            e.state = 0x08;
            e.c2 = e.cf = 0;
        }
        actors[k] = e;
    }
    ListMem m = list_of(keys);
    Snapshot<1024> snap;
    snap.begin(1);
    snap.add({kA3, 8, false, 0xA3});
    snap.add({k12D, 8, false, 0x12D});
    const Context c = reload_ctx();
    uint32_t drops = 0, frames = 0;
    for (; frames < 10; ++frames) {
        // Our pass (the hook, before the handler): the front key only.
        std::vector<uint32_t> now = keys_of(m);
        if (!now.empty()) {
            const gad::WaitEntry& e = actors[now[0]];
            Front f = blocker(now[0], e.found ? (now[0] == kA3 ? 0xA3 : now[0] == k12D ? 0x12D : 0x1) : 0);
            f.state = e.state;
            f.c2 = e.c2;
            f.cf = e.cf;
            Context cc = c;
            cc.dropped = drops;
            if (judge(f, snap.find(now[0]), cc) == Verdict::Drop) {
                CHECK(swap_remove_front(m, C, now[0]));
                ++drops;
            }
        }
        // The native 0x12: removes missing / ready front keys, stops at the first existing not-ready one.
        now = keys_of(m);
        std::vector<gad::WaitRead> list;
        for (uint32_t k : now) list.push_back({true, actors[k]});
        const gad::WaitScan s = gad::scan_wait_list(list, static_cast<uint32_t>(list.size()));
        if (s.verdict == gad::WaitVerdict::Empties) break;
        CHECK(s.verdict == gad::WaitVerdict::Blocks);
        CHECK(s.key == kA3 || s.key == k12D);
        // Apply the native removals in fake memory (same swap-remove) up to the blocker.
        while (true) {
            std::vector<uint32_t> cur = keys_of(m);
            const gad::WaitEntry& e = actors[cur[0]];
            if (e.found && !gad::entry_ready(e)) break;
            CHECK(swap_remove_front(m, C, cur[0]));
        }
        // The S15 prediction: the first blocker is 0xB010012D.
        if (frames == 0) CHECK(keys_of(m)[0] == k12D);
    }
    CHECK(drops == 2);
    CHECK(frames <= 3);
}

// S15 had 75 orphans in one session: with the cap at 128, 75 pre-existing blockers in one reload all go.
void test_many_blockers() {
    std::vector<uint32_t> keys;
    Snapshot<1024> snap;
    snap.begin(1);
    for (uint32_t i = 0; i < 75; ++i) {
        keys.push_back(0xB0100000 + i);
        snap.add({0xB0100000 + i, 8, false, 0x1000 + i});
    }
    ListMem m = list_of(keys);
    Context c = reload_ctx();
    c.cap = 128;
    uint32_t drops = 0;
    for (int pass = 0; pass < 200 && !keys_of(m).empty(); ++pass) {
        const uint32_t k = keys_of(m)[0];
        c.dropped = drops;
        if (judge(blocker(k, 0x1000 + (k - 0xB0100000)), snap.find(k), c) != Verdict::Drop) break;
        CHECK(swap_remove_front(m, C, k));
        ++drops;
    }
    CHECK(drops == 75 && keys_of(m).empty());
    // The old cap of 16 would have stopped at the 17th.
    c.dropped = 16;
    c.cap = 16;
    CHECK(judge(blocker(0xB0100000, 0x1000), snap.find(0xB0100000), c) == Verdict::CapReached);
}

void test_report_clock() {
    ReportClock r;
    CHECK(!r.due(1'999'999));
    CHECK(r.due(2'000'000));
    CHECK(!r.due(2'500'000));
    CHECK(r.due(10'000'000));
    CHECK(r.due(30'000'000));
    CHECK(r.due(60'000'000));
    CHECK(!r.due(119'000'000));
    CHECK(r.due(120'000'000));
    CHECK(r.due(180'000'000));
    r.reset();
    CHECK(r.due(2'000'000));
}

void test_age_table() {
    AgeTable<16> t;
    t.note(kA3, false, 1000);
    t.note(kA3, false, 6000);  // the first time is kept
    CHECK(t.since(kA3) == 1000);
    t.note(k12D, true, 1000);  // ready and never seen not ready: nothing kept
    CHECK(t.since(k12D) == 0);
    t.note(kA3, true, 7000);  // ready: forgotten
    CHECK(t.since(kA3) == 0);
    t.note(kA3, false, 9000);
    CHECK(t.since(kA3) == 9000);
    CHECK(t.since(0) == 0);
    for (uint32_t i = 1; i <= 20; ++i) t.note(0xB0000000 + i, false, i);
    CHECK(t.full() >= 4);
}

}  // namespace

int main() {
    test_trip_clock();
    test_trip_clock_2283();
    test_reload_trip();
    test_waiting_own_2322();
    test_judge();
    test_snapshot_bounds();
    test_swap_remove();
    test_s15_replay();
    test_many_blockers();
    test_report_clock();
    test_age_table();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("optd reload wait tests: all passed\n");
    return 0;
}
