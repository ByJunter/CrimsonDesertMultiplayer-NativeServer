// P8 session 25, weather (docs/research/noche/clima-sincronizar.md §5, "Implementado"): the pure logic of
// weather_sync (src/mod/session2/weather_sync_logic.h): the field table and its block reads, the state-12 frame
// byte for byte, sanity, interpolation (tick and receive-time bases, accumulator extrapolation, Raw words), the
// precipitation command, the gates, the owned-X records, the selftest state machine, the modes and the lock-free
// double buffer (including two threads).

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

#include "common/troc_frame.h"
#include "mod/session2/weather_sync_logic.h"

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

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

wsy::Snapshot sample(uint32_t seq, uint64_t tick, float base) {
    wsy::Snapshot s;
    s.seq = seq;
    s.tick = tick;
    s.mask = wsy::kHasA | wsy::kHasB | wsy::kHasC | wsy::kHasX | wsy::kHasPos;
    s.pos[0] = 100.0f;
    s.pos[1] = 50.0f;
    s.pos[2] = -200.0f;
    for (size_t i = 0; i < wsy::kFieldCount; ++i) s.w[i] = wsy::as_word(base * 0.01f + static_cast<float>(i) * 0.001f);
    return s;
}

void test_field_table() {
    static_assert(wsy::kFieldCount == 61);
    CHECK(wsy::kIdxPrecip == 0 && wsy::kFields[wsy::kIdxPrecip].off == 0x12C);
    CHECK(wsy::kFields[wsy::kIdxRain].off == 0x16C && wsy::kFields[wsy::kIdxSnow].off == 0x168);
    CHECK(wsy::kFields[wsy::kIdxCloudBase].block == wsy::Block::B && wsy::kFields[wsy::kIdxCloudBase].off == 0xA8);
    CHECK(wsy::kFields[wsy::kIdxScrollX].kind == wsy::Kind::Accum && wsy::kFields[wsy::kIdxScrollZ].off == 0x128);
    // Every B word from +0x64 to +0xF0, in order, no gap; the colours and the seed are Raw.
    size_t b = 0;
    for (size_t i = 0; i < wsy::kFieldCount; ++i) {
        if (wsy::kFields[i].block != wsy::Block::B) continue;
        CHECK(wsy::kFields[i].off == 0x64 + 4 * b);
        ++b;
    }
    CHECK(b == 36);
    // Review 4: the seed and the angle are floats held from the newest sample; only the colours are Raw.
    CHECK(wsy::kFields[wsy::index_of(wsy::Block::B, 0xE8)].kind == wsy::Kind::Hold);
    CHECK(wsy::kFields[wsy::index_of(wsy::Block::A, 0x154)].kind == wsy::Kind::Hold);
    CHECK(wsy::kFields[wsy::index_of(wsy::Block::B, 0xEC)].kind == wsy::Kind::Raw);
    CHECK(wsy::kFields[wsy::index_of(wsy::Block::C, 0x1B4)].kind == wsy::Kind::Raw);
    CHECK(wsy::kFields[wsy::index_of(wsy::Block::C, 0x178)].kind == wsy::Kind::Unknown);
    // Review 6: B+0x78/7C/80 are of unknown type: logged, never written.
    for (uint16_t off : {uint16_t{0x78}, uint16_t{0x7C}, uint16_t{0x80}}) {
        const size_t i = wsy::index_of(wsy::Block::B, off);
        CHECK(wsy::kFields[i].kind == wsy::Kind::Unknown && !wsy::written(i, wsy::kKnownMask) &&
              !wsy::kept_for_release(i));
    }
    CHECK(wsy::written(wsy::index_of(wsy::Block::A, 0x154), wsy::kHasA));
    CHECK(wsy::written(wsy::index_of(wsy::Block::B, 0xEC), wsy::kHasB));
    CHECK(wsy::index_of(wsy::Block::A, 0x140) == wsy::kFieldCount);  // not in the schema
    // The rain/snow outputs are sent but never written; the precipitation command is.
    CHECK(!wsy::written(wsy::kIdxRain, wsy::kKnownMask) && !wsy::written(wsy::kIdxSnow, wsy::kKnownMask));
    CHECK(wsy::written(wsy::kIdxPrecip, wsy::kKnownMask));
    CHECK(!wsy::written(wsy::index_of(wsy::Block::C, 0x178), wsy::kKnownMask));
    CHECK(!wsy::written(wsy::kIdxPrecip, wsy::kHasB | wsy::kHasX));  // A absent
    CHECK(wsy::written(wsy::index_of(wsy::Block::B, 0xE8), wsy::kHasB));
}

void test_extract() {
    uint8_t a[wsy::kReadA.size], b[wsy::kReadB.size], c[wsy::kReadC.size], x[wsy::kReadX.size];
    for (size_t i = 0; i < sizeof a; ++i) a[i] = static_cast<uint8_t>(i);
    for (size_t i = 0; i < sizeof b; ++i) b[i] = static_cast<uint8_t>(0x80 + i);
    for (size_t i = 0; i < sizeof c; ++i) c[i] = static_cast<uint8_t>(0x40 + i);
    const float scroll[2] = {123.5f, -7.25f};
    std::memcpy(x, scroll, sizeof x);
    uint32_t w[wsy::kFieldCount];
    wsy::extract(a, b, c, x, w);
    // A+0x12C is the first 4 bytes of the A copy; A+0x16C is 0x40 further.
    CHECK(w[wsy::kIdxPrecip] == 0x03020100u);
    CHECK(w[wsy::kIdxRain] == 0x43424140u);
    // B+0x64 = first word of the B copy; B+0xF0 its last.
    CHECK(w[wsy::index_of(wsy::Block::B, 0x64)] == 0x83828180u);
    const size_t last_b = wsy::index_of(wsy::Block::B, 0xF0);
    uint32_t expect = 0;
    std::memcpy(&expect, b + (0xF0 - 0x64), 4);
    CHECK(w[last_b] == expect);
    CHECK(w[wsy::index_of(wsy::Block::C, 0x178)] == 0x43424140u);
    CHECK(near(wsy::as_float(w[wsy::kIdxScrollX]), 123.5f) && near(wsy::as_float(w[wsy::kIdxScrollZ]), -7.25f));
    // A missing block leaves zeros.
    wsy::extract(a, nullptr, c, x, w);
    CHECK(w[wsy::index_of(wsy::Block::B, 0x64)] == 0 && w[wsy::kIdxPrecip] == 0x03020100u);
}

void test_frame_bytes() {
    wsy::Snapshot s;
    s.seq = 0x01020304;
    s.tick = 0x1122334455667788ull;
    s.mask = wsy::kHasA | wsy::kHasB | wsy::kHasX | wsy::kHasPos;
    s.pos[0] = 1.0f;  // 0x3F800000
    s.pos[1] = -2.0f;  // 0xC0000000
    s.pos[2] = 0.5f;  // 0x3F000000
    for (size_t i = 0; i < wsy::kFieldCount; ++i) s.w[i] = 0xA0000000u + static_cast<uint32_t>(i);
    const auto f = wsy::frame(s);
    CHECK(f.size() == 277);
    // u16 opcode 1 | i8 -1 | u16 272
    CHECK(f[0] == 0x01 && f[1] == 0x00 && f[2] == 0xFF && f[3] == 0x10 && f[4] == 0x01);
    // u8 12 | u8 1 | u16 mask 0x001B | u32 seq | u64 tick
    CHECK(f[5] == 12 && f[6] == 1 && f[7] == 0x1B && f[8] == 0x00);
    CHECK(f[9] == 0x04 && f[10] == 0x03 && f[11] == 0x02 && f[12] == 0x01);
    CHECK(f[13] == 0x88 && f[14] == 0x77 && f[15] == 0x66 && f[16] == 0x55 && f[17] == 0x44 && f[18] == 0x33 &&
          f[19] == 0x22 && f[20] == 0x11);
    CHECK(f[21] == 0x00 && f[22] == 0x00 && f[23] == 0x80 && f[24] == 0x3F);  // pos x
    CHECK(f[25] == 0x00 && f[26] == 0x00 && f[27] == 0x00 && f[28] == 0xC0);  // pos y
    CHECK(f[33] == 0x00 && f[34] == 0x00 && f[35] == 0x00 && f[36] == 0xA0);  // word 0
    CHECK(f[276] == 0xA0 && f[273] == 60);                                    // word 60
    // The tunnel takes it as a whole packet of opcode 1 (troc_frame.h kControlOpcode) in ToGuestClient batches.
    const auto v = troc::parse(f);
    CHECK(v && v->opcode == troc::kControlOpcode && v->slot == -1 && v->payload.size() == wsy::kPayloadSize);
    CHECK(troc::parse_wire(f, troc::Dir::ToGuestClient).has_value());
    static_assert(wsy::kControlWeather != troc::kControlHostReady && wsy::kControlWeather != troc::kControlHostDead &&
                  wsy::kControlWeather != troc::kControlHostCharacter);
    static_assert(wsy::kControlOpcode == troc::kControlOpcode);
    // Caps 0x04: neither the compression flag nor phase B's own-save flag (0x02, main worktree).
    static_assert(wsy::kCapsWeatherSync != troc::kCapsCompressed && wsy::kCapsWeatherSync != 0x02);
    // Round trip.
    const auto d = wsy::decode(f);
    CHECK(d && d->seq == s.seq && d->tick == s.tick && d->mask == s.mask && near(d->pos[1], -2.0f));
    CHECK(d && std::memcmp(d->w, s.w, sizeof s.w) == 0);
    // Exactly this form only.
    auto bad = f;
    bad[5] = 5;  // another state
    CHECK(!wsy::is_frame(bad) && !wsy::decode(bad));
    bad = f;
    bad[6] = 2;  // another version: still the frame (taken out), not decoded
    CHECK(wsy::is_frame(bad) && !wsy::decode(bad));
    bad = f;
    bad[8] = 0x01;  // unknown mask bit 0x100
    CHECK(!wsy::decode(bad));
    std::vector<uint8_t> shorter(f.begin(), f.end() - 1);
    shorter[3] = 0x0F;
    CHECK(!wsy::is_frame(shorter));
    bad = f;
    bad[2] = 0x00;  // slot 0
    CHECK(!wsy::is_frame(bad));
}

void test_sanity() {
    wsy::Snapshot s = sample(1, 1000, 10.0f);
    CHECK(wsy::check(s).ok() && wsy::applicable(s));
    s.w[wsy::kIdxCloud] = wsy::as_word(std::numeric_limits<float>::quiet_NaN());
    CHECK(wsy::check(s).nonfinite == 1 && !wsy::applicable(s));
    s = sample(1, 1000, 10.0f);
    s.w[wsy::index_of(wsy::Block::B, 0xA8)] = wsy::as_word(3.0e8f);
    CHECK(wsy::check(s).huge == 1 && !wsy::check(s).ok());
    // A colour word that is a NaN as a float is fine (Raw is not checked), nor are words of unknown type.
    s = sample(1, 1000, 10.0f);
    s.w[wsy::index_of(wsy::Block::B, 0xEC)] = 0xFFFFFFFFu;
    s.w[wsy::index_of(wsy::Block::C, 0x178)] = 0xFFFFFFFFu;
    s.w[wsy::index_of(wsy::Block::B, 0x7C)] = 0xFFFFFFFFu;
    CHECK(wsy::check(s).ok());
    // Review 4: the Hold floats are checked.
    s.w[wsy::index_of(wsy::Block::A, 0x154)] = wsy::as_word(std::numeric_limits<float>::quiet_NaN());
    CHECK(!wsy::check(s).ok() && wsy::check(s).first_bad == static_cast<int>(wsy::index_of(wsy::Block::A, 0x154)));
    s = sample(1, 1000, 10.0f);
    s.w[wsy::index_of(wsy::Block::B, 0xE8)] = wsy::as_word(5.0e7f);
    CHECK(wsy::check(s).huge == 1 && std::strcmp(wsy::field_name(wsy::check(s).first_bad), "cloudSeed") == 0);
    // Review 8: +-FLT_MAX ("not set") passes and is counted apart.
    s = sample(1, 1000, 10.0f);
    s.w[wsy::kIdxCloudBase] = wsy::kFltMaxBits;
    s.w[wsy::index_of(wsy::Block::C, 0x1B0)] = wsy::kNegFltMaxBits;
    CHECK(wsy::check(s).ok() && wsy::check(s).unset == 2 && wsy::check(s).huge == 0 && wsy::applicable(s));
    CHECK(wsy::check(s).first_bad == -1 && std::strcmp(wsy::field_name(-1), "-") == 0);
    // Out of [0, 1] is counted, not fatal.
    s.w[wsy::kIdxPrecip] = wsy::as_word(1.5f);
    CHECK(wsy::check(s).outside_unit == 1 && wsy::check(s).ok());
    // A block that is absent is not looked at; A, B and X are needed to apply.
    s = sample(1, 1000, 10.0f);
    s.mask = wsy::kHasB | wsy::kHasX;
    s.w[wsy::kIdxCloud] = wsy::as_word(std::numeric_limits<float>::infinity());
    CHECK(wsy::check(s).ok() && !wsy::applicable(s));
    s.mask = wsy::kHasA | wsy::kHasB | wsy::kHasX;  // no C: still applicable
    s.w[wsy::kIdxCloud] = wsy::as_word(0.5f);
    CHECK(wsy::applicable(s));
    // A bad position.
    s.mask |= wsy::kHasPos;
    s.pos[0] = std::numeric_limits<float>::infinity();
    CHECK(!wsy::check(s).ok() && std::strcmp(wsy::field_name(wsy::check(s).first_bad), "position") == 0);
}

void test_blend() {
    wsy::Timed prev{sample(1, 10000, 0.0f), 50000};
    wsy::Timed cur{sample(2, 10200, 0.0f), 50210};
    // Lerp field: cloudiness 0.2 -> 0.4; Raw: the seed differs; Accum: scroll 100 -> 102.
    prev.s.w[wsy::kIdxCloud] = wsy::as_word(0.2f);
    cur.s.w[wsy::kIdxCloud] = wsy::as_word(0.4f);
    const size_t seed = wsy::index_of(wsy::Block::B, 0xE8);
    prev.s.w[seed] = 0x11111111u;
    cur.s.w[seed] = 0x22222222u;
    prev.s.w[wsy::kIdxScrollX] = wsy::as_word(100.0f);
    cur.s.w[wsy::kIdxScrollX] = wsy::as_word(102.0f);
    uint32_t out[wsy::kFieldCount];
    // now = cur's arrival + 200 ms: host now ~ 10400, render 10100 -> t = 0.5.
    wsy::Blend b = wsy::blend(&prev, cur, 50410, out);
    CHECK(b.blended && !b.extrapolated && near(b.t, 0.5f));
    CHECK(near(wsy::as_float(out[wsy::kIdxCloud]), 0.3f));
    CHECK(out[seed] == 0x22222222u);  // Raw: the newest
    CHECK(near(wsy::as_float(out[wsy::kIdxScrollX]), 101.0f));
    // Right at cur's arrival: render 9900 -> before prev: t = 0.
    b = wsy::blend(&prev, cur, 50210, out);
    CHECK(b.blended && near(b.t, 0.0f) && near(wsy::as_float(out[wsy::kIdxCloud]), 0.2f));
    // Late: 900 ms after cur arrived -> render 10800, 600 ms past cur: Lerp holds cur, Accum goes on (slope 10/s).
    b = wsy::blend(&prev, cur, 51110, out);
    CHECK(b.extrapolated && near(wsy::as_float(out[wsy::kIdxCloud]), 0.4f));
    CHECK(near(wsy::as_float(out[wsy::kIdxScrollX]), 108.0f, 1e-3f));
    // Very late: extrapolation capped at 1000 ms past cur.
    b = wsy::blend(&prev, cur, 60000, out);
    CHECK(near(wsy::as_float(out[wsy::kIdxScrollX]), 112.0f, 1e-3f));
    // No prev / same seq / gap too large / tick not growing and receive times not either: cur as it is.
    b = wsy::blend(nullptr, cur, 50410, out);
    CHECK(!b.blended && out[wsy::kIdxCloud] == cur.s.w[wsy::kIdxCloud]);
    wsy::Timed far_prev{sample(1, 7000, 0.0f), 47000};
    b = wsy::blend(&far_prev, cur, 50410, out);
    CHECK(!b.blended);
    // Ticks unreadable (0): receive times are the base (210 ms apart).
    wsy::Timed p0 = prev, c0 = cur;
    p0.s.tick = 0;
    c0.s.tick = 0;
    b = wsy::blend(&p0, c0, 50210 + 300 + 105, out);  // render = c.recv + 405 - 300 = prev.recv + 315 -> t = 1.5 -> 1
    CHECK(b.blended && b.extrapolated);
    b = wsy::blend(&p0, c0, 50210 + 195, out);  // render = prev.recv + 105 -> t = 0.5
    CHECK(b.blended && near(b.t, 0.5f) && near(wsy::as_float(out[wsy::kIdxCloud]), 0.3f));
    wsy::Timed backwards = c0;
    backwards.recv_ms = p0.recv_ms - 1;
    b = wsy::blend(&p0, backwards, 50410, out);
    CHECK(!b.blended);
    // A NaN in prev keeps cur's word for that field only.
    wsy::Timed pn = prev;
    pn.s.w[wsy::kIdxCloud] = wsy::as_word(std::numeric_limits<float>::quiet_NaN());
    b = wsy::blend(&pn, cur, 50410, out);
    CHECK(out[wsy::kIdxCloud] == cur.s.w[wsy::kIdxCloud] && near(wsy::as_float(out[wsy::kIdxScrollX]), 101.0f));
    // FLT_MAX ("not set") on either side is never interpolated: cur's word as it is.
    wsy::Timed pu = prev, cu = cur;
    pu.s.w[wsy::kIdxCloud] = wsy::kFltMaxBits;
    b = wsy::blend(&pu, cur, 50410, out);
    CHECK(out[wsy::kIdxCloud] == cur.s.w[wsy::kIdxCloud]);
    cu.s.w[wsy::kIdxCloud] = wsy::kFltMaxBits;
    b = wsy::blend(&prev, cu, 50410, out);
    CHECK(out[wsy::kIdxCloud] == wsy::kFltMaxBits);
    // Hold: the newest, never mixed (the wind angle 350 -> 10 must not sweep through 180).
    const size_t deg = wsy::index_of(wsy::Block::A, 0x154);
    wsy::Timed ph = prev, ch = cur;
    ph.s.w[deg] = wsy::as_word(350.0f);
    ch.s.w[deg] = wsy::as_word(10.0f);
    b = wsy::blend(&ph, ch, 50410, out);
    CHECK(b.blended && out[deg] == wsy::as_word(10.0f));
}

void test_precip_command() {
    uint32_t w[wsy::kFieldCount] = {};
    w[wsy::kIdxPrecip] = wsy::as_word(0.9f);  // the host's target: NOT what is written
    w[wsy::kIdxRain] = wsy::as_word(0.35f);
    w[wsy::kIdxSnow] = wsy::as_word(0.0f);
    CHECK(near(wsy::precip_command(w), 0.35f));
    CHECK(near(wsy::as_float(wsy::value_to_write(wsy::kIdxPrecip, w)), 0.35f));
    w[wsy::kIdxSnow] = wsy::as_word(0.6f);
    CHECK(near(wsy::precip_command(w), 0.6f));
    w[wsy::kIdxRain] = wsy::as_word(std::numeric_limits<float>::quiet_NaN());
    w[wsy::kIdxSnow] = wsy::as_word(3.0f);
    CHECK(near(wsy::precip_command(w), 1.0f));
    w[wsy::kIdxSnow] = wsy::as_word(-1.0f);
    CHECK(near(wsy::precip_command(w), 0.0f));
    // "Not set" is no rain (FLT_MAX must never become a downpour).
    w[wsy::kIdxRain] = wsy::kFltMaxBits;
    w[wsy::kIdxSnow] = wsy::as_word(0.2f);
    CHECK(near(wsy::precip_command(w), 0.2f));
    // Any other field is written as it is.
    w[wsy::kIdxCloud] = 0x3F000000u;
    CHECK(wsy::value_to_write(wsy::kIdxCloud, w) == 0x3F000000u);
}

void test_gates() {
    wsy::GateIn in;
    CHECK(wsy::decide_gate(in) == wsy::Gate::NotRemote);
    in.remote_live = true;
    CHECK(wsy::decide_gate(in) == wsy::Gate::NotInWorld);
    in.in_world = true;
    CHECK(wsy::decide_gate(in) == wsy::Gate::NoPosition);
    in.host_pos = true;
    CHECK(wsy::decide_gate(in) == wsy::Gate::NoPosition);  // ours too
    in.own_pos = true;
    // Review 5: radius 2000 m, margin 100 m: a closed gate opens inside 1900 m, an open one closes outside 2100 m.
    const auto margin_is = [](uint32_t r, double m) { return std::fabs(wsy::radius_margin_m(r) - m) < 1e-9; };
    CHECK(margin_is(2000, 100.0) && margin_is(100, 10.0) && margin_is(10, 5.0) && margin_is(400, 20.0));
    CHECK(margin_is(10000, 100.0) && margin_is(0, 0.0));
    in.own[0] = 1000.0f;
    in.host[0] = -899.0f;  // 1899 m
    CHECK(wsy::decide_gate(in) == wsy::Gate::Apply);
    in.host[0] = -950.0f;  // 1950 m: closed stays closed, open stays open
    CHECK(wsy::decide_gate(in) == wsy::Gate::TooFar);
    in.was_open = true;
    CHECK(wsy::decide_gate(in) == wsy::Gate::Apply);
    in.host[0] = -1099.0f;  // 2099 m
    CHECK(wsy::decide_gate(in) == wsy::Gate::Apply);
    in.host[0] = -1101.0f;  // 2101 m
    CHECK(wsy::decide_gate(in) == wsy::Gate::TooFar);
    in.was_open = false;
    in.radius_m = 5000;
    CHECK(wsy::decide_gate(in) == wsy::Gate::Apply);
    in.host[1] = std::numeric_limits<float>::infinity();
    CHECK(wsy::decide_gate(in) == wsy::Gate::TooFar);
    in.host[1] = 0.0f;
    in.cutscene = true;
    CHECK(wsy::decide_gate(in) == wsy::Gate::Cutscene);
    // Freshness: under 1 s since it arrived.
    CHECK(wsy::fresh(10999, 10000) && !wsy::fresh(11000, 10000) && !wsy::fresh(5000, 0));
    // S25: a sample stamped after the reader's clock read (between it and the mailbox read) is fresh, age 0.
    CHECK(wsy::fresh(9999, 10000) && wsy::fresh(10000, 10016) && wsy::age_ms(10000, 10016) == 0 &&
          wsy::age_ms(10016, 10000) == 16);
}

void test_world_watch() {
    wsy::WorldWatch w;
    w.feed(1000, true);
    w.feed(3999, true);
    CHECK(!w.ready());
    w.feed(4000, true);
    CHECK(w.ready());
    // One failed read (or a few within 2 s) never ends it.
    w.feed(4200, false);
    w.feed(6100, false);
    CHECK(w.ready());
    w.feed(6200, false);
    CHECK(!w.ready());
    // Back: 3 s of good reads again.
    w.feed(7000, true);
    w.feed(9999, true);
    CHECK(!w.ready());
    w.feed(10000, true);
    CHECK(w.ready());
    // A good read in between restarts the leave timer.
    w.feed(11000, false);
    w.feed(12500, true);
    w.feed(13000, false);
    w.feed(14900, false);
    CHECK(w.ready());
    w.feed(15000, false);
    CHECK(!w.ready());
}

void test_line_gate() {
    wsy::LineGate g(5000);
    CHECK(g.take(100));
    CHECK(!g.take(200) && !g.take(4000) && g.pending() == 2);
    CHECK(g.take(5100) && g.suppressed_and_reset() == 2 && g.pending() == 0);
    CHECK(!g.take(5200) && g.take(10100) && g.suppressed_and_reset() == 1);
}

void test_fault_latch() {
    wsy::FaultLatch f;
    CHECK(!f.blocked() && !f.on_frame(true) && !f.on_gate_epoch(0));
    f.on_gate_epoch(3);  // an edge before the fault is already seen: it never clears the fault
    f.on_fault();
    CHECK(f.blocked() && f.faults() == 1);
    CHECK(!f.on_gate_epoch(3));
    // Clean frames: a failed read restarts the count.
    for (uint32_t i = 0; i + 1 < wsy::FaultLatch::kCleanFrames; ++i) CHECK(!f.on_frame(true));
    CHECK(!f.on_frame(false));
    for (uint32_t i = 0; i + 1 < wsy::FaultLatch::kCleanFrames; ++i) CHECK(!f.on_frame(true));
    CHECK(f.on_frame(true) && !f.blocked());
    // A new open edge clears at once.
    f.on_fault();
    CHECK(f.blocked() && f.on_gate_epoch(4) && !f.blocked());
    // At most kMaxFaults: then it stays blocked for good.
    wsy::FaultLatch g;
    for (uint32_t i = 0; i < wsy::FaultLatch::kMaxFaults; ++i) {
        g.on_fault();
        g.on_gate_epoch(100 + i);
    }
    CHECK(g.exhausted() && g.blocked() && !g.on_gate_epoch(999));
    for (uint32_t i = 0; i < 2 * wsy::FaultLatch::kCleanFrames; ++i) CHECK(!g.on_frame(true));
    CHECK(g.blocked());
}

void test_restore_bits() {
    // Kept: every holder word that may be written or that 0x382A1A0 rewrites; never the accumulators, never the
    // words of unknown type.
    CHECK(wsy::kept_for_release(wsy::kIdxPrecip) && wsy::kept_for_release(wsy::kIdxRain));
    CHECK(wsy::kept_for_release(wsy::index_of(wsy::Block::B, 0xEC)) &&
          wsy::kept_for_release(wsy::index_of(wsy::Block::A, 0x154)));
    CHECK(!wsy::kept_for_release(wsy::kIdxScrollX) && !wsy::kept_for_release(wsy::index_of(wsy::Block::C, 0x178)));
    uint64_t kept = 0, written = 0;
    for (size_t i = 0; i < wsy::kFieldCount; ++i) {
        if (wsy::kept_for_release(i)) kept |= wsy::field_bit(i);
        if (wsy::written(i, wsy::kHasA | wsy::kHasB | wsy::kHasX)) written |= wsy::field_bit(i);
    }
    // Written A/B words go back; C was never written (absent from the samples); the outputs only if the flag was ours.
    const uint64_t r = wsy::restore_bits(kept, written, /*flag_was_ours=*/true);
    CHECK((r & wsy::field_bit(wsy::kIdxPrecip)) && (r & wsy::field_bit(wsy::kIdxRain)) &&
          (r & wsy::field_bit(wsy::kIdxCloudBase)));
    CHECK(!(r & wsy::field_bit(wsy::index_of(wsy::Block::C, 0x1B0))) && !(r & wsy::field_bit(wsy::kIdxScrollX)));
    const uint64_t r2 = wsy::restore_bits(kept, written, /*flag_was_ours=*/false);
    CHECK(!(r2 & wsy::field_bit(wsy::kIdxRain)) && (r2 & wsy::field_bit(wsy::kIdxPrecip)));
    CHECK(wsy::restore_bits(0, written, true) == 0);
}

void test_owned() {
    wsy::OwnedSet<2> s;
    CHECK(s.size() == 0 && !s.find(0x1000) && !s.add(0));
    wsy::Owned* a = s.add(0x1000);
    CHECK(a && a->x == 0x1000 && s.size() == 1);
    a->orig_flag = 0;
    a->has_orig_a = true;
    a->orig_a[0] = 7;
    CHECK(s.add(0x1000) == a && s.find(0x1000)->orig_a[0] == 7);
    CHECK(s.add(0x2000) && !s.add(0x3000) && s.size() == 2);  // full
    s.remove(0x1000);
    CHECK(!s.find(0x1000) && s.size() == 1);
    wsy::Owned* c = s.add(0x3000);
    CHECK(c && !c->has_orig_a && c->orig_a[0] == 0 && c->kept == 0 && !c->flag_ours);  // a fresh record
    // Review 3: the records of any other X go (handed to the callback first), the current one stays.
    wsy::OwnedSet<4> t;
    t.add(0x1000)->flag_ours = true;
    t.add(0x2000);
    t.add(0x3000);
    std::vector<uintptr_t> seen;
    CHECK(t.evict_others(0x2000, [&](wsy::Owned& o) { seen.push_back(o.x); }) == 2);
    CHECK(seen.size() == 2 && seen[0] == 0x1000 && seen[1] == 0x3000 && t.size() == 1 && t.find(0x2000));
    // A re-created X never fills the set: each new address evicts the old one.
    for (uintptr_t x = 0x10000; x < 0x10000 + 64 * 0x100; x += 0x100) {
        t.evict_others(x, [](wsy::Owned&) {});
        CHECK(t.add(x) != nullptr);
    }
    CHECK(t.size() == 1);
}

void test_selftest() {
    wsy::Selftest st(wsy::Selftest::Config{1000, 3000});
    CHECK(st.phase() == wsy::StPhase::WaitWorld);
    wsy::StStep s = st.step(0, false, true);
    CHECK(!s.write && !s.restore && st.phase() == wsy::StPhase::WaitWorld);
    s = st.step(100, true, true);
    CHECK(st.phase() == wsy::StPhase::Settle && !s.write);
    s = st.step(600, false, true);  // the world left: wait again
    CHECK(st.phase() == wsy::StPhase::WaitWorld);
    st.step(700, true, true);
    s = st.step(1600, true, true);
    CHECK(!s.write && st.phase() == wsy::StPhase::Settle);
    s = st.step(1700, true, true);
    CHECK(s.write && s.began && !s.restore && st.phase() == wsy::StPhase::Apply);
    s = st.step(2000, true, true);
    CHECK(s.write && !s.began);
    s = st.step(4699, true, true);
    CHECK(s.write);
    s = st.step(4700, true, true);
    CHECK(!s.write && s.restore && st.phase() == wsy::StPhase::Done);
    s = st.step(9000, true, true);
    CHECK(!s.write && !s.restore && st.phase() == wsy::StPhase::Done);  // once per game start
    CHECK(!st.fault(9000));

    // Another X while applying: aborted, restore asked once.
    wsy::Selftest b(wsy::Selftest::Config{0, 3000});
    b.step(0, true, true);
    CHECK(b.step(1, true, true).write);
    s = b.step(2, true, false);
    CHECK(s.restore && !s.write && b.phase() == wsy::StPhase::Aborted);
    CHECK(!b.step(3, true, true).restore);
    // The world leaving while applying.
    wsy::Selftest c(wsy::Selftest::Config{0, 3000});
    c.step(0, true, true);
    c.step(1, true, true);
    CHECK(c.step(2, false, true).restore && c.phase() == wsy::StPhase::Aborted);
    // A fault while applying.
    wsy::Selftest d(wsy::Selftest::Config{0, 3000});
    d.step(0, true, true);
    d.step(1, true, true);
    CHECK(d.fault(5) && d.phase() == wsy::StPhase::Aborted && !d.fault(6));
    CHECK(!d.step(7, true, true).write);
    // Defaults: 15 s of natural weather, then 30 s.
    CHECK(wsy::Selftest().config().settle_ms == 15000 && wsy::Selftest().config().duration_ms == 30000);
}

void test_modes() {
    CHECK(wsy::parse_mode("0").mode == wsy::Mode::Off && wsy::parse_mode("").known);
    CHECK(wsy::parse_mode("probe").mode == wsy::Mode::Probe);
    CHECK(wsy::parse_mode(" SelfTest ; P1").mode == wsy::Mode::Selftest);
    CHECK(wsy::parse_mode("1").mode == wsy::Mode::On && wsy::parse_mode("on").mode == wsy::Mode::On);
    const auto bad = wsy::parse_mode("2");
    CHECK(!bad.known && bad.mode == wsy::Mode::Off);
    CHECK(std::strcmp(wsy::mode_name(wsy::Mode::Selftest), "selftest") == 0);
}

void test_box() {
    wsy::Timed t{sample(7, 0x123456789ull, 3.0f), 0xABCDEF012ull};
    uint32_t w[wsy::kBoxWords];
    wsy::pack(t, w);
    wsy::Timed u;
    wsy::unpack(w, u);
    CHECK(u.s.seq == 7 && u.s.tick == 0x123456789ull && u.recv_ms == 0xABCDEF012ull && u.s.mask == t.s.mask);
    CHECK(near(u.s.pos[2], -200.0f) && std::memcmp(u.s.w, t.s.w, sizeof t.s.w) == 0);

    wsy::DoubleBuffer<wsy::kBoxWords> box;
    uint64_t seq = 0;
    CHECK(box.seq() == 0 && !box.read(w, seq));
    box.publish(w);
    uint32_t r[wsy::kBoxWords];
    CHECK(box.read(r, seq) && seq == 1 && std::memcmp(r, w, sizeof w) == 0);
    wsy::Timed t2 = t;
    t2.s.seq = 8;
    wsy::pack(t2, w);
    box.publish(w);
    CHECK(box.read(r, seq) && seq == 2 && r[0] == 8);

    // History: the two newest distinct samples.
    wsy::History h;
    h.push(t, 1);
    CHECK(h.has_cur && !h.has_prev && h.cur.s.seq == 7);
    h.push(t, 2);  // the same sample again: no prev
    CHECK(!h.has_prev);
    h.push(t2, 3);
    CHECK(h.has_prev && h.prev.s.seq == 7 && h.cur.s.seq == 8 && h.seen == 3);

    // Two threads: every copy a reader keeps is one whole publish (all words carry the same value).
    constexpr size_t N = 64;
    wsy::DoubleBuffer<N> b2;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> torn{0}, good{0};
    std::thread reader([&] {
        uint32_t c[N];
        uint64_t s = 0;
        while (!stop.load()) {
            if (!b2.read(c, s)) continue;
            bool same = true;
            for (size_t i = 1; i < N; ++i) same = same && c[i] == c[0];
            (same && c[0] == static_cast<uint32_t>(s) ? good : torn).fetch_add(1);
        }
    });
    uint32_t src[N];
    for (uint32_t k = 1; k <= 200000; ++k) {
        for (size_t i = 0; i < N; ++i) src[i] = k;
        b2.publish(src);
    }
    stop.store(true);
    reader.join();
    CHECK(torn.load() == 0);
    CHECK(good.load() > 0);
}

}  // namespace

int main() {
    test_field_table();
    test_extract();
    test_frame_bytes();
    test_sanity();
    test_blend();
    test_precip_command();
    test_gates();
    test_world_watch();
    test_line_gate();
    test_fault_latch();
    test_restore_bits();
    test_owned();
    test_selftest();
    test_modes();
    test_box();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("optd_weather_sync: all checks passed\n");
    return 0;
}
