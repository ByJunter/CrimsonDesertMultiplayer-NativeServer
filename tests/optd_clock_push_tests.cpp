// P8 session 25, hour and weather (docs/research/noche/clima-hora.md §5): the pure logic of clock_push
// (src/mod/session2/clock_push_logic.h): the native SetFieldTimeAck 2816 byte for byte, the FieldTime
// arithmetic against S16's numbers, the T2 state machine and W1's predicate.

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#include "common/troc_frame.h"
#include "mod/session2/clock_push_logic.h"

using namespace cdmp::session2;
using namespace cdmp;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

using Mem = std::array<uint8_t, ckp::kFieldTimeMem>;

// A FieldTime in memory layout; the padding byte +0x15 is set to `pad` to prove it never reaches the wire.
Mem field_time(uint32_t day, uint32_t hour, uint32_t minute, uint32_t second, uint32_t ms, uint16_t scale,
               uint64_t anchor, uint8_t b14 = 6, uint8_t pad = 0xEE) {
    Mem m{};
    const uint32_t v[5] = {day, hour, minute, second, ms};
    std::memcpy(m.data(), v, sizeof v);
    m[0x14] = b14;
    m[0x15] = pad;
    std::memcpy(m.data() + 0x16, &scale, 2);
    std::memcpy(m.data() + 0x18, &anchor, 8);
    return m;
}

// S16 (clima-hora.md §0, INFORME.txt §4): the host's world base and the fresh 2144 user 2 got.
const Mem kHostWorld = field_time(6, 13, 37, 28, 728, 12, 81905);
const Mem kGuest2144 = field_time(6, 13, 59, 53, 952, 12, 194007);

void test_wire_byte_exact() {
    // 00 0B FF 23 00 | key A0100303 | day 6, 13, 59, 53, 952 | u8 6 | scale 12 | anchor 194007 (0x2F5D7)
    const std::array<uint8_t, 40> expected = {
        0x00, 0x0B, 0xFF, 0x23, 0x00,                          // header: opcode 2816, slot -1, payload 35
        0x03, 0x03, 0x10, 0xA0,                                // user 2's character key
        0x06, 0x00, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x00,        // day, hour
        0x3B, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x00,        // minute, second
        0xB8, 0x03, 0x00, 0x00,                                // millisecond
        0x06,                                                  // the u8 at +0x14 (no padding byte after it)
        0x0C, 0x00,                                            // scale
        0xD7, 0xF5, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};       // anchor
    const auto p = ckp::make_set_field_time_ack(0xA0100303u, kGuest2144.data());
    CHECK(p.size() == ckp::kAckPacket && ckp::kAckPacket == 40 && ckp::kAckPayload == 35);
    CHECK(std::memcmp(p.data(), expected.data(), expected.size()) == 0);
    for (size_t i = 0; i < p.size(); ++i) {
        if (p[i] != expected[i]) std::printf("  byte %zu: got %02X want %02X\n", i, p[i], expected[i]);
    }
    // The padding never travels: another pad byte, the same packet.
    const Mem other_pad = field_time(6, 13, 59, 53, 952, 12, 194007, 6, 0x11);
    CHECK(ckp::make_set_field_time_ack(0xA0100303u, other_pad.data()) == p);

    // The tunnel takes it as one whole S->C packet (troc_frame.h), opcode 2816, slot -1, payload 35.
    const auto v = troc::parse_wire(std::span<const uint8_t>(p), troc::Dir::ToGuestClient);
    CHECK(v.has_value());
    if (v) CHECK(v->opcode == 2816 && v->slot == -1 && v->payload.size() == 35);
    // troc::make builds the same bytes from the payload.
    const std::vector<uint8_t> made = troc::make(2816, -1, std::span<const uint8_t>(p).subspan(ckp::kHeader));
    CHECK(made.size() == p.size() && std::memcmp(made.data(), p.data(), p.size()) == 0);
}

void test_wire_layout_and_parse() {
    const auto w = ckp::to_wire(kGuest2144.data());
    CHECK(w.size() == 31);
    uint16_t scale = 0;
    uint64_t anchor = 0;
    std::memcpy(&scale, w.data() + 0x15, 2);   // scale right after the u8 (no padding)
    std::memcpy(&anchor, w.data() + 0x17, 8);
    CHECK(scale == 12 && anchor == 194007 && w[0x14] == 6);

    const auto p = ckp::make_set_field_time_ack(0xA0100303u, kGuest2144.data());
    uint32_t key = 0;
    Mem back{};
    CHECK(ckp::parse_set_field_time_ack(p, key, back.data()));
    CHECK(key == 0xA0100303u);
    Mem expect = kGuest2144;
    expect[0x15] = 0;  // the padding comes back as 0
    CHECK(back == expect);
    // Anything else is refused.
    CHECK(!ckp::parse_set_field_time_ack(std::span<const uint8_t>(p).first(39), key, back.data()));
    auto bad = p;
    bad[0] = 0x01;  // another opcode
    CHECK(!ckp::parse_set_field_time_ack(bad, key, back.data()));
    bad = p;
    bad[3] = 0x22;  // another length
    CHECK(!ckp::parse_set_field_time_ack(bad, key, back.data()));
    bad = p;
    bad[2] = 0x00;  // not slot -1
    CHECK(!ckp::parse_set_field_time_ack(bad, key, back.data()));
}

void test_decode_and_t0() {
    const ckp::FieldTime g = ckp::decode(kGuest2144.data());
    CHECK(g.day == 6 && g.hour == 13 && g.minute == 59 && g.second == 53 && g.ms == 952 && g.b14 == 6 &&
          g.scale == 12 && g.anchor == 194007);
    CHECK(ckp::base_game_ms(g) == 568793952);
    // S16's determining equality (INFORME.txt §4): the 2144 user 2 got is the host's world clock, re-based.
    const ckp::FieldTime h = ckp::decode(kHostWorld.data());
    CHECK(ckp::t0_game_ms(h) == ckp::t0_game_ms(g));
    CHECK(ckp::t0_game_ms(g) == 566465868);
    // ... and user 2's own 2762 (flag 1, now 211620) re-anchored that base: 211.356 game s lost (3:31).
    const ckp::FieldTime reanchored = ckp::decode(field_time(6, 13, 59, 53, 952, 12, 211620).data());
    CHECK(ckp::t0_game_ms(h) - ckp::t0_game_ms(reanchored) == 211356);
}

void test_plausible() {
    CHECK(ckp::plausible(ckp::decode(kHostWorld.data())));
    // The parking field's fixed time that froze the world (C1e-C4b): day 0, 24:00, scale 0.
    CHECK(!ckp::plausible(ckp::decode(field_time(0, 24, 0, 0, 0, 0, 0).data())));
    CHECK(!ckp::plausible(ckp::decode(field_time(6, 13, 0, 0, 0, 0, 81905).data())));   // scale 0
    CHECK(!ckp::plausible(ckp::decode(field_time(6, 13, 0, 0, 0, 12, 0).data())));      // no anchor
    CHECK(!ckp::plausible(ckp::decode(field_time(6, 13, 60, 0, 0, 12, 1).data())));
    CHECK(!ckp::plausible(ckp::decode(field_time(6, 13, 0, 60, 0, 12, 1).data())));
    CHECK(!ckp::plausible(ckp::decode(field_time(6, 13, 0, 0, 1000, 12, 1).data())));
    CHECK(!ckp::plausible(ckp::decode(field_time(6, 13, 0, 0, 0, 1001, 1).data())));
    CHECK(!ckp::plausible(ckp::decode(Mem{}.data())));                                   // a zeroed global
    CHECK(ckp::plausible(ckp::decode(field_time(0, 0, 0, 0, 0, 1, 1).data())));
    CHECK(ckp::plausible(ckp::decode(field_time(99, 23, 59, 59, 999, 1000, 1).data())));
}

void test_loading_ack() {
    // u32 actor | u8 flag | u64 now (S16: user 2's 2762 = 1, now 211620).
    std::array<uint8_t, 13> pl{};
    const uint32_t actor = 0xA0100303u;
    const uint64_t now = 211620;
    std::memcpy(pl.data(), &actor, 4);
    pl[4] = 1;
    std::memcpy(pl.data() + 5, &now, 8);
    const auto a = ckp::parse_loading_ack(pl);
    CHECK(a.has_value());
    if (a) CHECK(a->actor == actor && a->flag == 1 && a->now == now);
    CHECK(!ckp::parse_loading_ack(std::span<const uint8_t>(pl).first(12)).has_value());
    CHECK(ckp::push_key(0xA0100304u, actor) == 0xA0100304u);
    CHECK(ckp::push_key(0, actor) == actor);
    CHECK(ckp::push_key(0, 0) == 0);
}

void test_weather_block() {
    CHECK(ckp::guest_req_blocked_by_env(2560, true));
    CHECK(!ckp::guest_req_blocked_by_env(2560, false));
    CHECK(!ckp::guest_req_blocked_by_env(2125, true));   // SetFieldTimeReq: never_from_guest's, not this key's
    CHECK(!ckp::guest_req_blocked_by_env(2961, true));   // CreateProjectileByWeatherReq: combat, not the field's flags
    CHECK(!ckp::guest_req_blocked_by_env(2556, true));
}

using V = ckp::PushWatch::Verdict;

void test_watch_basic() {
    ckp::PushWatch w;  // settle 900 ms, min gap 2000 ms
    const Mem a = kHostWorld;
    // Before user 2 is in the world: nothing, whatever the clock does.
    CHECK(w.sample(a.data(), false, 1000) == V::NotInWorld);
    CHECK(w.sample(a.data(), false, 5000) == V::NotInWorld);
    // T1 gave it at its 2762: the same clock is never pushed again.
    w.sent(a.data(), 6000);
    CHECK(w.has_sent());
    CHECK(w.sample(a.data(), true, 7000) == V::Same);
    CHECK(w.sample(a.data(), true, 60000) == V::Same);
    // The host reloads: a new base/anchor. One sample to settle, then a push.
    const Mem b = field_time(6, 14, 2, 0, 0, 12, 300000);
    CHECK(w.sample(b.data(), true, 61000) == V::Unsettled);
    CHECK(w.sample(b.data(), true, 61500) == V::Unsettled);
    CHECK(w.sample(b.data(), true, 62000) == V::Push);
    w.sent(b.data(), 62000);
    CHECK(w.sample(b.data(), true, 63000) == V::Same);
}

void test_watch_burst_and_load() {
    ckp::PushWatch w;
    w.sent(kHostWorld.data(), 0);
    // A burst of publishes (every 500 ms): never settled, never pushed.
    for (uint64_t t = 1000; t < 6000; t += 500) {
        const Mem m = field_time(6, 14, 0, static_cast<uint32_t>(t / 500 % 60), 0, 12, 100000 + t);
        CHECK(w.sample(m.data(), true, t) == V::Unsettled);
    }
    // A publish during user 2's load (phase not Stable): held back.
    const Mem c = field_time(6, 15, 0, 0, 0, 12, 400000);
    CHECK(w.sample(c.data(), false, 10000) == V::NotInWorld);
    CHECK(w.sample(c.data(), false, 12000) == V::NotInWorld);
    // Its 2762: T1 gives it the current clock; T2 then has nothing to add.
    w.sent(c.data(), 13000);
    CHECK(w.sample(c.data(), true, 14000) == V::Same);
    // Had T1 not given it (load ended, clock already settled): T2 at once.
    ckp::PushWatch w2;
    CHECK(w2.sample(c.data(), false, 10000) == V::NotInWorld);
    CHECK(w2.sample(c.data(), true, 13000) == V::Push);
}

void test_watch_refused_and_reset() {
    ckp::PushWatch w;
    const Mem a = kHostWorld;
    CHECK(w.sample(a.data(), true, 0) == V::Unsettled);  // the first sample starts the settle time
    CHECK(w.sample(a.data(), true, 1000) == V::Push);
    // The queue refused it: retried, not before min_gap_ms.
    w.failed(1000);
    CHECK(!w.has_sent());
    CHECK(w.sample(a.data(), true, 2000) == V::TooSoon);
    CHECK(w.sample(a.data(), true, 2999) == V::TooSoon);
    CHECK(w.sample(a.data(), true, 3000) == V::Push);
    w.sent(a.data(), 3000);
    CHECK(w.sample(a.data(), true, 4000) == V::Same);
    // A new epoch / user 2 gone: given nothing; the settled clock goes again at once.
    w.reset();
    CHECK(!w.has_sent());
    CHECK(w.sample(a.data(), true, 5000) == V::Push);
    // An implausible clock (the frozen parking time) is never pushed.
    const Mem frozen = field_time(0, 24, 0, 0, 0, 0, 0);
    CHECK(w.sample(frozen.data(), true, 6000) == V::Unsettled);
    CHECK(w.sample(frozen.data(), true, 7000) == V::Implausible);
    CHECK(w.sample(frozen.data(), true, 9000) == V::Implausible);
    // Custom timings.
    ckp::PushWatch fast(ckp::PushWatch::Config{0, 0});
    CHECK(fast.sample(a.data(), true, 0) == V::Push);
    fast.failed(0);
    CHECK(fast.sample(a.data(), true, 0) == V::Push);
    CHECK(std::strcmp(ckp::verdict_name(V::Push), "push") == 0);
}

// Review of 8047e5c: a refused T1 must be retried even when the clock equals the last successful push (the 2762
// re-anchored the guest), and T1's early returns (no key, unreadable, implausible) must leave "not given".
void test_watch_refused_same_clock() {
    const Mem a = kHostWorld;
    ckp::PushWatch w;
    w.sent(a.data(), 0);     // an earlier 2762 got its 2816
    w.failed(1000);          // the next 2762's 2816: the queue refused it
    CHECK(!w.has_sent());
    CHECK(w.sample(a.data(), true, 2000) == V::TooSoon);
    CHECK(w.sample(a.data(), true, 3000) == V::Push);   // same clock as before: retried all the same
    w.sent(a.data(), 3000);
    CHECK(w.sample(a.data(), true, 4000) == V::Same);
}

void test_watch_t1_early_return() {
    const Mem a = kHostWorld;
    // Control: without forget(), a later sample of the same clock is "already given".
    ckp::PushWatch control;
    control.sent(a.data(), 0);
    CHECK(control.sample(a.data(), true, 60000) == V::Same);
    // T1 at a new 2762: forget() first, then an early return (no key / unreadable / implausible): nothing recorded.
    ckp::PushWatch w;
    w.sent(a.data(), 0);
    w.forget();
    CHECK(!w.has_sent());
    CHECK(w.sample(a.data(), false, 59000) == V::NotInWorld);  // still loading: held back
    CHECK(w.sample(a.data(), true, 60000) == V::Push);         // in the world: T2 gives it
    // A T1 that did push right after its forget(): nothing more for T2.
    ckp::PushWatch ok;
    ok.sent(a.data(), 0);
    ok.forget();
    ok.sent(a.data(), 60000);
    CHECK(ok.sample(a.data(), true, 61000) == V::Same);
    // T1's read counts as a sample: a clock first seen by T1 is settled from then on.
    ckp::PushWatch fresh;
    const Mem b = field_time(6, 15, 0, 0, 0, 12, 500000);
    fresh.sent(b.data(), 10000);
    fresh.failed(10000);
    CHECK(fresh.sample(b.data(), true, 12000) == V::Push);
}

}  // namespace

int main() {
    test_wire_byte_exact();
    test_wire_layout_and_parse();
    test_decode_and_t0();
    test_plausible();
    test_loading_ack();
    test_weather_block();
    test_watch_basic();
    test_watch_burst_and_load();
    test_watch_refused_and_reset();
    test_watch_refused_same_clock();
    test_watch_t1_early_return();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_clock_push: all tests passed\n");
    return 0;
}
