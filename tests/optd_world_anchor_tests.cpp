// The world around user 2 (src/mod/session2/world_anchor_logic.h; docs/research/noche/mundo-invitado.md, section
// "Implementado").
//
// SCOPE: offline logic only. The option, the state machine of the collector's second anchor (set after a 2762 in a
// field the host owns, the leaves 2437 / 2283 / 2001 and the re-arm, the watchdog's verify / lost / busy / field
// reset, the clears and the fallback, epochs, probe's "would" path) and the probe's RemoveActorAck reader and removal
// statistics. They do NOT test the OnRecvPacket hook, the field's lock calls, the slot write against the running
// game nor the collector's behaviour (one-PC test, §6.2 of the document).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "mod/session2/world_anchor_logic.h"

using namespace cdmp::session2;
using wal::Act;
using wal::Anchor;
using wal::Decision;
using wal::Inputs;
using wal::Mode;
using wal::Result;
using wal::Slot;
using wal::Why;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

constexpr uint32_t kHostUser = 0x90100000;
constexpr uint32_t kUser2Key = 0xA0100001;  // user 2's main character
constexpr uint32_t kOtherKey = 0x90100000;  // what a native world observer of the host would write ([user+0x60])
constexpr uintptr_t kField = 0x2000'0000'1000;
constexpr uintptr_t kField2 = 0x2000'0000'9000;
constexpr uint32_t kGen = 0x01000007;

wal::FieldView field(uintptr_t f, Slot slot = {}, uint32_t gen = kGen, uint8_t open = 1, uint32_t owner = kHostUser) {
    wal::FieldView v;
    v.ok = true;
    v.field = f;
    v.gen = gen;
    v.open = open;
    v.owner = owner;
    v.slot = slot;
    return v;
}

// User 2 loaded (its first 2762) in the host's field, nothing set yet.
Inputs ready(uint64_t now, uint32_t loads = 1, Slot slot = {}) {
    Inputs in;
    in.now_ms = now;
    in.user2 = true;
    in.stable = true;
    in.loads = loads;
    in.key = kUser2Key;
    in.host_user = kHostUser;
    in.f2 = field(kField, slot);
    return in;
}
// The same, with the slot this side holds read back as `slot`.
Inputs held(uint64_t now, Slot slot, uint32_t loads = 1) {
    Inputs in = ready(now, loads, slot);
    in.owned = field(kField, slot);
    return in;
}

// Set the anchor at t = 1000 (mode 1); returns the anchor.
Anchor set_at_1000(Mode m = Mode::On) {
    Anchor a(m);
    const Decision d = a.step(ready(1000));
    a.applied(d, Result::Written, 1000);
    return a;
}

// ---------------------------------------------------------------- the option

void test_option() {
    CHECK(wal::parse_mode("").mode == Mode::Off);
    CHECK(wal::parse_mode("0").mode == Mode::Off);
    CHECK(wal::parse_mode("probe").mode == Mode::Probe);
    CHECK(wal::parse_mode("probe ; P8 sesion 25").mode == Mode::Probe);
    CHECK(wal::parse_mode("1").mode == Mode::On);
    CHECK(wal::parse_mode(" PROBE\r").mode == Mode::Probe);
    CHECK(!wal::parse_mode("2").known);
    CHECK(wal::parse_mode("2").mode == Mode::Off);
    CHECK(std::strcmp(wal::mode_name(Mode::Probe), "probe") == 0);
    CHECK(wal::leaves_field(2437) && wal::leaves_field(2283) && wal::leaves_field(2001));
    CHECK(!wal::leaves_field(2445) && !wal::leaves_field(2549) && !wal::leaves_field(0));
    // The build's numbers the cpp checks (documented in mundo-invitado.md).
    static_assert(wal::kFieldSlotOn == 0x2E4 && wal::kFieldSlotKey == 0x2E8 && wal::kTsField == 0x500);
    static_assert(wal::kLockSlotTry == 0x28 && wal::kLockSlotUnlock == 0x20);
    for (int i = 0; i < static_cast<int>(Why::Count); ++i) CHECK(std::strcmp(wal::why_name(static_cast<Why>(i)), "?") != 0);
}

// ---------------------------------------------------------------- when it is wanted

void test_off_does_nothing() {
    Anchor a(Mode::Off);
    CHECK(!a.due(0));
    const Decision d = a.step(ready(1000));
    CHECK(d.act == Act::None);
    CHECK(a.on_leave(2437, 1, 1000).act == Act::None);
    CHECK(!a.owned());
}

void test_wanted_conditions() {
    Anchor a(Mode::On);
    Inputs in = ready(1000);
    CHECK(a.wanted(in) == Why::None);
    {
        Inputs x = in;
        x.user2 = false;
        CHECK(a.wanted(x) == Why::NoUser2);
    }
    {
        Inputs x = in;
        x.logout = true;
        CHECK(a.wanted(x) == Why::Logout);
    }
    {
        Inputs x = in;
        x.stable = false;
        CHECK(a.wanted(x) == Why::NotStable);
    }
    {
        Inputs x = in;
        x.loads = 0;  // Stable without a 2762 cannot happen; still not armed
        CHECK(a.wanted(x) == Why::NotArmed);
    }
    {
        Inputs x = in;
        x.key = 0;  // unknown, or not its MainPlayerDataStartAck's
        CHECK(a.wanted(x) == Why::NoKey);
        x.key = 0xB0100704;  // an NPC key is never the anchor
        CHECK(a.wanted(x) == Why::NoKey);
    }
    {
        Inputs x = in;
        x.f2.ok = false;
        CHECK(a.wanted(x) == Why::FieldUnread);
    }
    {
        Inputs x = in;
        x.f2.open = 0;
        CHECK(a.wanted(x) == Why::FieldClosed);
    }
    {
        Inputs x = in;
        x.f2.owner = 0x90100001;  // user 2's own field
        CHECK(a.wanted(x) == Why::NotHostField);
        x.f2.owner = kHostUser;
        x.host_user = 0;  // the host's user unreadable
        CHECK(a.wanted(x) == Why::NotHostField);
    }
}

void test_waits_then_sets() {
    Anchor a(Mode::On);
    CHECK(a.due(0));
    // User 2 still loading: nothing, read again in 0.5 s.
    Inputs in = ready(1000);
    in.stable = false;
    Decision d = a.step(in);
    CHECK(d.act == Act::None && d.why == Why::NotStable);
    CHECK(!a.due(1499) && a.due(1500));
    // Its 2762: set {1, key} in its field.
    d = a.step(ready(1500));
    CHECK(d.act == Act::Set && d.why == Why::First);
    CHECK(d.field == kField && d.gen == kGen && d.key == kUser2Key);
    CHECK(d.found == Slot{});
    a.applied(d, Result::Written, 1500);
    CHECK(a.owned() && a.owned()->field == kField && a.owned()->key == kUser2Key);
    CHECK(a.ours(kUser2Key));
    // The watchdog reads it back 5 s later, not before.
    CHECK(!a.due(6499) && a.due(6500));
}

void test_set_stale_or_retry() {
    Anchor a(Mode::On);
    Decision d = a.step(ready(1000));
    CHECK(d.act == Act::Set);
    a.applied(d, Result::Stale, 1000);  // the field changed under the lock
    CHECK(!a.owned());
    CHECK(!a.due(1499) && a.due(1500));
    d = a.step(ready(1500));
    CHECK(d.act == Act::Set);
    a.applied(d, Result::Retry, 1500);  // the lock was busy: the next packet
    CHECK(!a.owned() && a.due(1500));
}

// ---------------------------------------------------------------- the watchdog

void test_verify_once() {
    Anchor a = set_at_1000();
    const Slot mine{1, kUser2Key};
    Decision d = a.step(held(6000, mine));
    CHECK(d.act == Act::None && d.verified && d.since_ms == 5000);
    d = a.step(held(11000, mine));
    CHECK(d.act == Act::None && !d.verified);
    CHECK(a.owned() && a.owned()->verified);
}

void test_lost_set_again() {
    Anchor a = set_at_1000();
    // Lost (e.g. a native observer turned on and off): set again, at most one per 5 s.
    Decision d = a.step(held(6000, Slot{0, 0}));
    CHECK(d.act == Act::Set && d.why == Why::Lost && d.found == Slot{});
    a.applied(d, Result::Written, 6000);
    CHECK(a.owned() && !a.owned()->verified && a.owned()->since_ms == 6000);
    // Lost again right away (the watchdog's next read is 5 s later anyway; a forced read before is rate-limited).
    Anchor b = set_at_1000();
    d = b.step(held(3000, Slot{0, 0}));  // not due yet, but step() is a read: rate-limited by the last write
    CHECK(d.act == Act::None && d.why == Why::Lost);
    CHECK(!b.due(5999) && b.due(6000));
    d = b.step(held(6000, Slot{0, 0}));
    CHECK(d.act == Act::Set && d.why == Why::Lost);
}

void test_busy_never_stolen() {
    // Held, then a foreign key in the slot: left alone, kept as ours (when it is freed: lost -> set again).
    Anchor a = set_at_1000();
    Decision d = a.step(held(6000, Slot{1, kOtherKey}));
    CHECK(d.act == Act::None && d.why == Why::Busy && d.found == (Slot{1, kOtherKey}));
    CHECK(a.owned());
    d = a.step(held(11000, Slot{0, 0}));
    CHECK(d.act == Act::Set && d.why == Why::Lost);
    // Not held: a foreign key there is never taken...
    Anchor b(Mode::On);
    d = b.step(ready(1000, 1, Slot{1, kOtherKey}));
    CHECK(d.act == Act::None && d.why == Why::Busy);
    // ... but one of our own (an earlier epoch's user 2) is.
    Anchor c = set_at_1000();
    c.on_epoch();
    Inputs gone = held(2000, Slot{1, kUser2Key});
    gone.user2 = false;
    d = c.step(gone);  // cleared, as the fallback
    c.applied(d, Result::Stale, 2000);  // say the clear found it changed: forgotten
    CHECK(!c.owned());
    Inputs next = ready(3000, 1, Slot{1, kUser2Key});
    next.key = 0xA0100002;  // the new user 2's character
    d = c.step(next);
    CHECK(d.act == Act::Set && d.key == 0xA0100002 && d.found == (Slot{1, kUser2Key}));
}

void test_field_reset_forgets() {
    Anchor a = set_at_1000();
    Inputs in = held(6000, Slot{0, 0});
    in.owned.gen = kGen + 0x100;  // the field went back to the pool and was reused
    Decision d = a.step(in);
    CHECK(d.act == Act::Forget && d.why == Why::FieldReset);
    CHECK(!a.owned() && a.due(6000));
    // Only closed (same generation: still in the pool, not handed out again): cleared, not left at {1, K}.
    Anchor b = set_at_1000();
    in = held(6000, Slot{1, kUser2Key});
    in.owned.open = 0;
    in.f2.open = 0;  // the same field, read for user 2's character
    d = b.step(in);
    CHECK(d.act == Act::Clear && d.why == Why::FieldClosed && d.field == kField && d.gen == kGen && !d.fallback);
    b.applied(d, Result::Written, 6000);
    CHECK(!b.owned());
    // Closed and user 2 gone: the fallback clear, same rule.
    Anchor b2 = set_at_1000();
    in = held(6000, Slot{1, kUser2Key});
    in.owned.open = 0;
    in.user2 = false;
    d = b2.step(in);
    CHECK(d.act == Act::Clear && d.fallback);
    Anchor c = set_at_1000();
    in = held(6000, Slot{1, kUser2Key});
    in.owned.ok = false;
    CHECK(c.step(in).act == Act::Forget);
}

// ---------------------------------------------------------------- the leaves and the re-arm

void test_leave_clears_and_rearms() {
    for (const uint16_t op : {uint16_t{2437}, uint16_t{2283}, uint16_t{2001}}) {
        Anchor a = set_at_1000();
        Decision d = a.on_leave(op, 1, 2000);
        CHECK(d.act == Act::Clear && d.why == Why::Leave && d.leave_op == op);
        CHECK(d.field == kField && d.key == kUser2Key && d.gen == kGen);
        a.applied(d, Result::Written, 2000);
        CHECK(!a.owned() && a.armed_after() == 1 && a.due(2000));
        // Still Stable with the same 2762 count (a 2283 does not move the phase): not armed.
        d = a.step(ready(2000, 1));
        CHECK(d.act == Act::None && d.why == Why::NotArmed);
        // Its next 2762: set again.
        d = a.step(ready(2500, 2));
        CHECK(d.act == Act::Set && d.why == Why::First);
    }
}

void test_leave_without_slot() {
    Anchor a(Mode::On);
    Decision d = a.on_leave(2437, 3, 1000);
    CHECK(d.act == Act::None && d.why == Why::Leave && d.leave_op == 2437);
    CHECK(a.armed_after() == 3);
    CHECK(a.step(ready(1000, 3)).why == Why::NotArmed);
    CHECK(a.step(ready(1500, 4)).act == Act::Set);
    // A leave never lowers the bar.
    Anchor b(Mode::On);
    b.on_leave(2437, 5, 1000);
    b.on_leave(2001, 2, 1000);
    CHECK(b.armed_after() == 5);
}

void test_leave_retry_then_clear() {
    Anchor a = set_at_1000();
    Decision d = a.on_leave(2437, 1, 2000);
    a.applied(d, Result::Retry, 2000);  // the lock stayed busy
    CHECK(a.owned() && a.due(2000));
    // The next packet: not armed any more -> cleared (an ordinary clear now).
    Inputs in = held(2001, Slot{1, kUser2Key});
    in.stable = false;  // its 2437 moved the phase
    d = a.step(in);
    CHECK(d.act == Act::Clear && d.why == Why::NotStable && !d.leave_op && !d.fallback);
    a.applied(d, Result::Written, 2001);
    CHECK(!a.owned());
}

// ---------------------------------------------------------------- the clears

void test_fallback_clear() {
    Anchor a = set_at_1000();
    Inputs in = held(6000, Slot{1, kUser2Key});
    in.user2 = false;  // the link went away without a 2001
    Decision d = a.step(in);
    CHECK(d.act == Act::Clear && d.why == Why::NoUser2 && d.fallback);
    CHECK(d.field == kField && d.key == kUser2Key && d.gen == kGen);
    a.applied(d, Result::Written, 6000);
    CHECK(!a.owned());
    // Nothing more while user 2 is gone.
    in = ready(6500);
    in.user2 = false;
    CHECK(a.step(in).act == Act::None);
}

void test_logout_clear_is_not_fallback() {
    Anchor a = set_at_1000();
    Inputs in = held(6000, Slot{1, kUser2Key});
    in.logout = true;
    const Decision d = a.step(in);
    CHECK(d.act == Act::Clear && d.why == Why::Logout && !d.fallback);
}

void test_not_ours_forgotten() {
    // Not wanted any more, and the slot no longer holds our key: not ours to clear.
    Anchor a = set_at_1000();
    Inputs in = held(6000, Slot{1, kOtherKey});
    in.user2 = false;
    Decision d = a.step(in);
    CHECK(d.act == Act::Forget && d.why == Why::NotOurs);
    CHECK(!a.owned());
    Anchor b = set_at_1000();
    in = held(6000, Slot{0, 0});
    in.stable = false;
    d = b.step(in);
    CHECK(d.act == Act::Forget && d.why == Why::NotOurs);
}

void test_moved_field() {
    // User 2 is now in another field the host owns (the host travelled; then user 2 too): clear, then set there.
    Anchor a = set_at_1000();
    Inputs in = held(6000, Slot{1, kUser2Key}, 2);
    in.f2 = field(kField2, Slot{}, kGen + 1);
    Decision d = a.step(in);
    CHECK(d.act == Act::Clear && d.why == Why::Moved && d.field == kField);
    a.applied(d, Result::Written, 6000);
    CHECK(!a.owned() && a.due(6000));
    in.owned = {};
    d = a.step(in);
    CHECK(d.act == Act::Set && d.field == kField2 && d.gen == kGen + 1);
    // The host travelled alone: user 2 stays in the old field, still owned by the host -> kept.
    Anchor b = set_at_1000();
    in = held(6000, Slot{1, kUser2Key});
    d = b.step(in);
    CHECK(d.act == Act::None && d.verified);
}

void test_clear_stale_forgets() {
    Anchor a = set_at_1000();
    Inputs in = held(6000, Slot{1, kUser2Key});
    in.user2 = false;
    const Decision d = a.step(in);
    CHECK(d.act == Act::Clear);
    a.applied(d, Result::Stale, 6000);  // under the lock it no longer held our key / the field changed
    CHECK(!a.owned());
}

// ---------------------------------------------------------------- epochs and the server

void test_epochs() {
    Anchor a(Mode::On);
    a.on_leave(2437, 3, 1000);
    CHECK(a.armed_after() == 3);
    a.on_epoch();  // a new guest: its 2762s from 0
    CHECK(a.armed_after() == 0 && a.due(0));
    CHECK(a.step(ready(1000, 1)).act == Act::Set);
    // The phase tracker restarted inside an epoch (a rebuilt session 2): the bar goes with it.
    Anchor b(Mode::On);
    b.on_leave(2437, 3, 1000);
    CHECK(b.step(ready(1000, 3)).why == Why::NotArmed);
    CHECK(b.step(ready(2000, 1)).act == Act::Set);
    // The server instance replaced: nothing of the old one is touched.
    Anchor c = set_at_1000();
    c.forget();
    CHECK(!c.owned() && c.armed_after() == 0);
}

// ---------------------------------------------------------------- probe

void test_probe_would() {
    Anchor a(Mode::Probe);
    CHECK(a.probe());
    Decision d = a.step(ready(1000));
    CHECK(d.act == Act::Set && d.why == Why::First);
    a.applied(d, Result::Written, 1000);  // "would": nothing was written
    CHECK(a.owned());
    // The slot reads 00/00000000 (nothing written): never "lost", never "forgotten".
    Inputs in = ready(6000);
    in.owned = {};  // probe does not need it
    d = a.step(in);
    CHECK(d.act == Act::None && !d.verified);
    // Leaves and clears as "would".
    d = a.on_leave(2283, 1, 7000);
    CHECK(d.act == Act::Clear && d.leave_op == 2283);
    a.applied(d, Result::Written, 7000);
    CHECK(!a.owned());
    d = a.step(ready(7500, 2));
    CHECK(d.act == Act::Set);
    a.applied(d, Result::Written, 7500);
    in = ready(12500, 2);
    in.user2 = false;
    d = a.step(in);
    CHECK(d.act == Act::Clear && d.fallback);
}

// ---------------------------------------------------------------- the probe's reader and statistics

std::vector<uint8_t> remove_ack(uint8_t reason, std::initializer_list<uint32_t> keys, uint8_t head = 4) {
    std::vector<uint8_t> p = {head, 0, 0, 0, 0, reason};
    for (uint32_t k : keys) {
        uint8_t b[4];
        std::memcpy(b, &k, 4);
        p.insert(p.end(), b, b + 4);
    }
    return p;
}

void test_read_remove() {
    // A real one of the S16 capture: 04 00 00 00 00 19 | 27 06 10 B0 (0xB0100627, reason 25).
    const uint8_t real[] = {0x04, 0x00, 0x00, 0x00, 0x00, 0x19, 0x27, 0x06, 0x10, 0xB0};
    wal::Removal r = wal::read_remove(real);
    CHECK(r.ok && r.reason == 25 && r.total == 1 && r.key(0) == 0xB0100627);
    CHECK(r.key(1) == 0);  // past the end: none
    auto two = remove_ack(19, {0xB0100704, 0xB0100705});
    r = wal::read_remove(two);
    CHECK(r.ok && r.reason == 19 && r.total == 2 && r.key(1) == 0xB0100705);
    auto many = remove_ack(25, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
    r = wal::read_remove(many);
    CHECK(r.ok && r.total == 10);  // every key, none dropped
    for (uint32_t i = 0; i < 10; ++i) CHECK(r.key(i) == i + 1);
    CHECK(!wal::read_remove(remove_ack(25, {0xB0100627}, 5)).ok);  // not the known head
    CHECK(!wal::read_remove(std::span<const uint8_t>(real, 9)).ok);
    std::vector<uint8_t> odd(real, real + 10);
    odd.push_back(0);
    CHECK(!wal::read_remove(odd).ok);
    CHECK(!wal::read_remove({}).ok);
}

void test_dist2d() {
    const float a[3] = {-10628.f, 50.f, -3826.f}, b[3] = {-10763.f, 900.f, -3941.f};
    const float d = wal::dist2d(a, b);  // the height is not counted
    CHECK(std::fabs(d - std::sqrt(135.f * 135.f + 115.f * 115.f)) < 0.01f);
}

void test_removal_stats() {
    wal::RemovalStats s;
    const float host[3] = {0, 0, 0}, user2[3] = {400, 0, 0};
    const float near_u2[3] = {350, 0, 0};    // 50 m from user 2, 350 m from the host: the fault
    const float near_both[3] = {100, 0, 0};  // 100 m from the host, 300 m from user 2
    const float near_u2_b[3] = {320, 0, 0};  // 80 / 320
    // Pass 1 (t 5000-5010), pass 2 (t 10000).
    s.add(5000, &near_u2, &host, &user2);
    s.add(5005, &near_both, &host, &user2);
    s.add(5010, nullptr, &host, &user2);  // its position was unknown
    s.add(10000, &near_u2_b, &host, &user2);
    s.add(10001, &near_u2, nullptr, &user2);  // the host's position unknown
    s.other_reason();
    wal::RemovalStats::Summary m = s.take();
    CHECK(m.removals == 5 && m.passes == 2 && m.unresolved == 2 && m.other_reason == 1);
    CHECK(m.near_user2 == 2 && m.near_far_host == 2);
    CHECK(std::fabs(m.near_host_min - 320.f) < 0.01f && std::fabs(m.near_host_max - 350.f) < 0.01f);
    CHECK(std::fabs(m.near_u2_max - 80.f) < 0.01f);
    CHECK(std::fabs(m.players_max - 400.f) < 0.01f);
    CHECK(std::fabs(m.all_host_med - 320.f) < 0.01f);  // {100, 320, 350}
    // take() resets; a pass that goes on past the minute's edge is not counted again.
    s.add(10500, &near_both, &host, &user2);
    m = s.take();
    CHECK(m.removals == 1 && m.passes == 0 && m.near_user2 == 0);
    s.add(20000, &near_both, &host, &user2);
    m = s.take();
    CHECK(m.removals == 1 && m.passes == 1);
    m = s.take();
    CHECK(m.removals == 0 && m.passes == 0 && m.near_user2 == 0 && m.near_host_med == 0.f);
}

}  // namespace

int main() {
    test_option();
    test_off_does_nothing();
    test_wanted_conditions();
    test_waits_then_sets();
    test_set_stale_or_retry();
    test_verify_once();
    test_lost_set_again();
    test_busy_never_stolen();
    test_field_reset_forgets();
    test_leave_clears_and_rearms();
    test_leave_without_slot();
    test_leave_retry_then_clear();
    test_fallback_clear();
    test_logout_clear_is_not_fallback();
    test_not_ours_forgotten();
    test_moved_field();
    test_clear_stale_forgets();
    test_epochs();
    test_probe_would();
    test_read_remove();
    test_dist2d();
    test_removal_stats();
    if (g_failures) {
        std::printf("optd_world_anchor: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_world_anchor: all passed\n");
    return 0;
}
