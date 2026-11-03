// P8 title (src/mod/session2/slot_refresh.cpp): the guest's local refresh of
// the save manager's slot table. The planner (once per connection, retries
// while not ready or busy, give up, stop for the run after a fault or a code
// mismatch) and the table helpers. The game side (guest.cpp) needs the game.

#include <cstdio>
#include <cstring>
#include <vector>

#include "mod/session2/offsets_s2.h"
#include "mod/session2/slot_refresh.h"

using namespace cdmp::session2::slot_refresh;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

void test_once_per_connection() {
    Planner p;
    CHECK(!p.pending() && !p.due(0));
    CHECK(p.arm(0x10, 1000));
    CHECK(p.pending() && p.due(1000) && p.key() == 0x10);
    CHECK(!p.arm(0x10, 1001));  // the same connection: once
    CHECK(p.report(Attempt::Done, 1000) == Outcome::Finished);
    CHECK(!p.pending() && !p.due(5000) && p.done_count() == 1);
    CHECK(!p.arm(0x10, 6000));  // its second 2490 does nothing
    CHECK(p.arm(0x11, 7000));   // a new remote connection: again
    CHECK(p.due(7000));
    CHECK(p.report(Attempt::Done, 7000) == Outcome::Finished && p.done_count() == 2);
}

void test_busy_retries_then_done() {
    Planner p;
    CHECK(p.arm(1, 0, {100, 5}));
    CHECK(p.report(Attempt::Busy, 0) == Outcome::Retry);
    CHECK(p.pending() && !p.due(50) && p.due(100) && p.next_try_ms() == 100);
    CHECK(p.report(Attempt::NotReady, 100) == Outcome::Retry);
    CHECK(p.report(Attempt::Done, 200) == Outcome::Finished);
    CHECK(p.attempts() == 3 && !p.pending() && !p.disabled());
}

void test_gives_up() {
    Planner p;
    CHECK(p.arm(1, 0, {100, 3}));
    CHECK(p.report(Attempt::Busy, 0) == Outcome::Retry);
    CHECK(p.report(Attempt::Busy, 100) == Outcome::Retry);
    CHECK(p.report(Attempt::Busy, 200) == Outcome::GaveUp);
    CHECK(!p.pending() && !p.disabled() && p.done_count() == 0);
    CHECK(p.arm(2, 300));  // the next connection may try again
}

void test_fault_and_refused_stop_the_run() {
    for (const Attempt a : {Attempt::Fault, Attempt::Refused}) {
        Planner p;
        CHECK(p.arm(1, 0));
        CHECK(p.report(a, 0) == Outcome::Stopped);
        CHECK(p.disabled() && !p.pending() && !p.due(10000));
        CHECK(!p.arm(2, 100));  // never again in this run
        CHECK(p.report(Attempt::Done, 100) == Outcome::Stopped);
    }
}

void test_new_connection_replaces_pending_and_cancel() {
    Planner p;
    CHECK(p.arm(1, 0, {100, 50}));
    CHECK(p.report(Attempt::Busy, 0) == Outcome::Retry);
    CHECK(p.arm(2, 50));  // a new connection wins, with its own tries
    CHECK(p.key() == 2 && p.attempts() == 0 && p.due(50));
    p.cancel();
    CHECK(!p.pending() && !p.due(1000));
    CHECK(!p.arm(2, 1000));  // its key stays used
    CHECK(p.report(Attempt::Done, 1000) == Outcome::Finished && p.done_count() == 0);  // nothing pending: no-op
}

void test_zero_attempts_means_one() {
    Planner p;
    CHECK(p.arm(1, 0, {100, 0}));
    CHECK(p.report(Attempt::NotReady, 0) == Outcome::GaveUp);
}

std::vector<uint8_t> table(const std::vector<Entry>& es) {
    std::vector<uint8_t> raw(es.size() * kEntrySize, 0xCC);
    for (size_t i = 0; i < es.size(); ++i) {
        std::memcpy(raw.data() + i * kEntrySize, &es[i].slot, 4);
        std::memcpy(raw.data() + i * kEntrySize + 8, &es[i].time, 8);
    }
    return raw;
}

void test_table() {
    const auto raw = table({{1, 111}, {100, 222}, {201, 333}});
    auto es = parse_entries(raw, 3);
    CHECK(es.size() == 3 && es[0].slot == 1 && es[0].time == 111 && es[2].slot == 201 && es[2].time == 333);
    CHECK(describe(es) == "[1 @111, 100 @222, 201 @333]");
    CHECK(title_would_enable(es));
    CHECK(parse_entries(raw, 5).size() == 3);  // a short copy
    CHECK(parse_entries(raw, 0).empty() && describe({}) == "[]");
    // The title's predicate (0x9096890): slot < 3 or slot - 100 < 9.
    CHECK(!title_would_enable(parse_entries(table({{3, 1}, {99, 1}, {109, 1}, {200, 1}, {207, 1}}), 5)));
    CHECK(title_would_enable(parse_entries(table({{108, 1}}), 1)));
    CHECK(title_would_enable(parse_entries(table({{0, 1}}), 1)));
    CHECK(!title_would_enable({}));
}

void test_first_mismatch() {
    const uint8_t a[] = {1, 2, 3}, b[] = {1, 2, 3}, c[] = {1, 9, 3}, d[] = {1, 2};
    CHECK(first_mismatch(a, b) == -1);
    CHECK(first_mismatch(a, c) == 1);
    CHECK(first_mismatch(a, d) == 0);
}

void test_expected_sequence_shape() {
    using namespace cdmp::session2::off;
    // 0x2A22475..0x2A224F2 (+0xF5..+0x172 of SaveDataSlotRefreshReq): 0x7D bytes,
    // with the call to 0x1870C20 at 0x2A224CD.
    static_assert(sizeof kSlotRefreshReqSequenceBytes == 0x7D);
    const uint8_t* call = kSlotRefreshReqSequenceBytes + (0x2A224CD - kSlotRefreshReqSequence);
    int32_t rel = 0;
    std::memcpy(&rel, call + 1, 4);
    CHECK(call[0] == 0xE8 && 0x2A224CD + 5 + static_cast<int64_t>(rel) == static_cast<int64_t>(kSaveMgrRefreshSlots));
    // Both globals are [rip+...] = 0x6D69698.
    int32_t g1 = 0, g2 = 0;
    std::memcpy(&g1, kSlotRefreshReqSequenceBytes + 3, 4);
    std::memcpy(&g2, kSlotRefreshReqSequenceBytes + (0x2A224C2 - kSlotRefreshReqSequence) + 3, 4);
    CHECK(0x2A2247C + static_cast<int64_t>(g1) == static_cast<int64_t>(kServerActorLookupSlot));
    CHECK(0x2A224C9 + static_cast<int64_t>(g2) == static_cast<int64_t>(kServerActorLookupSlot));
    // The thunk: jmp .xtls 0xCBD92D0.
    int32_t j = 0;
    std::memcpy(&j, kSaveMgrRefreshSlotsHead + 1, 4);
    CHECK(kSaveMgrRefreshSlots + 5 + static_cast<int64_t>(j) == 0xCBD92D0);
    // The title's read: [rip+...] = 0x6D69190, then +0x88.
    int32_t w = 0;
    std::memcpy(&w, kTitlePlayReadsSaveMgrBytes + 3, 4);
    CHECK(kTitlePlayReadsSaveMgr + 7 + static_cast<int64_t>(w) == static_cast<int64_t>(kWorldSystemSlot));
    CHECK(kTitlePlayReadsSaveMgrBytes[10] == kWorldSystem_SaveManager);
}

}  // namespace

int main() {
    test_once_per_connection();
    test_busy_retries_then_done();
    test_gives_up();
    test_fault_and_refused_stop_the_run();
    test_new_connection_replaces_pending_and_cancel();
    test_zero_attempts_means_one();
    test_table();
    test_first_mismatch();
    test_expected_sequence_shape();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_slot_refresh: all passed\n");
    return 0;
}
