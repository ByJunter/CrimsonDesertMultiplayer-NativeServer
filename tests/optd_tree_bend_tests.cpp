// P8 session 29, the tree bend (docs/research/s25/gancho-cuerda.md section 14): the pure logic of tree_bend
// (src/mod/session2/tree_bend_logic.h): the state-15 frame, the sender's rate limits and run ends, the out queue, the
// receiver's seq / age / connection handling and interpolation, the observer's decision and the self-consistency of the
// verified build-2976 bytes.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "mod/session2/tree_bend_logic.h"

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

bool near_f(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

constexpr uint32_t kHost = 0xA0100001;
constexpr uint32_t kGuest = 0xA0100336;

void set_scene(uint8_t* s, uint64_t obj, uint64_t level) {
    std::memcpy(s, &obj, 8);
    std::memcpy(s + 8, &level, 8);
}

tbl::Sample sample(uint32_t key, uint32_t seq, uint32_t run, float amount, uint32_t tick, tbl::Kind kind = tbl::Kind::Sample) {
    tbl::Sample s;
    s.kind = kind;
    s.key = key;
    s.seq = seq;
    s.run = run;
    set_scene(s.scene, 0x0000000000FC82ull, 0x1234);
    s.amount = amount;
    s.dx = 1.0f;
    s.dz = 0.0f;
    s.tick_ms = tick;
    return s;
}

tbl::Bend bend(uint64_t t_ms, float amount, uint32_t key = kHost) {
    tbl::Bend b;
    b.key = key;
    set_scene(b.scene, 0x0000000000FC82ull, 0x1234);
    b.amount = amount;
    b.dx = 0.6f;
    b.dz = 0.8f;
    b.t_ms = t_ms;
    return b;
}

void test_frame() {
    tbl::Sample s = sample(kGuest, 7, 3, -0.25f, 123456);
    s.slot = 0;
    s.flags = tbl::kFlagApplyFlag;
    s.dx = 0.6f;
    s.dz = -0.8f;
    const auto f = tbl::frame(s);
    CHECK(f.size() == tbl::kFrameSize);
    CHECK(f.size() == 57);
    CHECK(f[0] == 1 && f[1] == 0 && f[2] == 0xFF && f[3] == 52 && f[4] == 0);
    CHECK(f[5] == 15 && f[6] == tbl::kVersion);
    CHECK(tbl::is_frame(f));
    const auto d = tbl::decode(std::span<const uint8_t>(f).subspan(tbl::kHeader));
    CHECK(d.has_value());
    if (d) {
        CHECK(d->kind == tbl::Kind::Sample);
        CHECK(d->key == kGuest && d->seq == 7 && d->run == 3 && d->slot == 0 && d->flags == tbl::kFlagApplyFlag);
        CHECK(std::memcmp(d->scene, s.scene, 16) == 0);
        CHECK(d->amount == -0.25f && d->dx == 0.6f && d->dz == -0.8f && d->tick_ms == 123456);
    }
    // End frames carry the last values and decode as End.
    const auto fe = tbl::frame(sample(kGuest, 8, 3, 0.4f, 1, tbl::Kind::End));
    const auto de = tbl::decode(std::span<const uint8_t>(fe).subspan(tbl::kHeader));
    CHECK(de && de->kind == tbl::Kind::End);

    // Not ours: other states, sizes, slots, opcodes.
    auto g = f;
    g[5] = 5;  // mount_sync's state
    CHECK(!tbl::is_frame(g));
    g = f;
    g[2] = 0;
    CHECK(!tbl::is_frame(g));
    g = f;
    g.push_back(0);
    CHECK(!tbl::is_frame(g));
    g = f;
    g[0] = 2;
    CHECK(!tbl::is_frame(g));

    // Malformed payloads are refused.
    auto bad = [&](auto mutate) {
        auto p = tbl::encode(s);
        mutate(p);
        return !tbl::decode(p).has_value();
    };
    CHECK(bad([](auto& p) { p[1] = 2; }));                  // version
    CHECK(bad([](auto& p) { p[2] = 3; }));                  // kind
    CHECK(bad([](auto& p) { p[3] = 2; }));                  // slot
    CHECK(bad([](auto& p) { p[5] = 1; }));                  // reserved
    CHECK(bad([](auto& p) { tbl::put32(&p[8], 0xB0100723); }));  // not a player
    CHECK(bad([](auto& p) { tbl::put32(&p[12], 0); }));     // seq 0
    CHECK(bad([](auto& p) { tbl::put32(&p[16], 0); }));     // run 0
    CHECK(bad([](auto& p) { std::memset(&p[20], 0, 12); }));  // scene id not set (first three dwords)
    CHECK(bad([](auto& p) { tbl::putf(&p[36], 1.5f); }));   // amount out of range
    CHECK(bad([](auto& p) { tbl::putf(&p[36], NAN); }));    // not finite
    CHECK(bad([](auto& p) { tbl::putf(&p[40], INFINITY); }));
    CHECK(bad([](auto& p) {  // a Sample's null direction
        tbl::putf(&p[40], 0.0f);
        tbl::putf(&p[44], 0.0f);
    }));
    std::vector<uint8_t> shortp(tbl::kPayloadSize - 1, 0);
    CHECK(!tbl::decode(shortp));
    // The level id's last dword alone does not make a scene id (as the natives).
    {
        auto p = tbl::encode(s);
        std::memset(&p[20], 0, 16);
        p[35] = 1;
        CHECK(!tbl::decode(p));
    }
}

void test_sender() {
    tbl::Sender snd;
    // The first SetBend starts run 1 and is sent at once.
    auto r = snd.on_bend(bend(1000, 0.10f));
    CHECK(r.new_run && r.out && r.out->run == 1 && r.out->seq == 1 && r.out->kind == tbl::Kind::Sample);
    CHECK(r.out && near_f(r.out->dx, 0.6f) && near_f(r.out->dz, 0.8f));
    // Within 25 ms nothing goes, even a jump.
    r = snd.on_bend(bend(1016, 0.90f));
    CHECK(!r.new_run && !r.out);
    // A jump > 0.15 after 25 ms goes at once.
    r = snd.on_bend(bend(1026, 0.30f));
    CHECK(r.out && r.out->seq == 2 && near_f(r.out->amount, 0.30f));
    // A small change waits for the 50 ms period.
    r = snd.on_bend(bend(1060, 0.35f));
    CHECK(!r.out);
    r = snd.on_bend(bend(1076, 0.36f));
    CHECK(r.out && r.out->seq == 3);
    // Still (< 0.01): only the 250 ms keep-alive.
    r = snd.on_bend(bend(1200, 0.365f));
    CHECK(!r.out);
    r = snd.on_bend(bend(1330, 0.365f));
    CHECK(r.out && r.out->seq == 4);
    // At most 40 Hz whatever the input: 60 frames a second with big changes give <= 40 frames.
    int sent = 0;
    for (int i = 0; i < 60; ++i) {
        const auto x = snd.on_bend(bend(2000 + i * 16, (i % 2) ? 0.9f : -0.9f));
        if (x.out) ++sent;
    }
    CHECK(sent <= 40 && sent >= 20);
    // Clamped to [-1, 1].
    r = snd.on_bend(bend(4000, 3.0f));
    CHECK(r.out && r.out->amount == 1.0f);
    // No End before 150 ms of silence; then one End with the run's last values, once.
    tbl::Sender::Ended ends[4];
    CHECK(snd.tick(4100, ends, 4) == 0);
    CHECK(snd.tick(4150, ends, 4) == 1);
    CHECK(ends[0].end.kind == tbl::Kind::End && ends[0].key == kHost && ends[0].run == 1 && ends[0].end.amount == 1.0f);
    CHECK(snd.tick(5000, ends, 4) == 0);
    CHECK(!snd.active(kHost, 0));
    // The next SetBend starts run 2.
    r = snd.on_bend(bend(6000, 0.2f));
    CHECK(r.new_run && r.out && r.out->run == 2);
    // Another tree (scene id) while run 2 is active: its End in end_first (lower seq, queued first), a new run now,
    // and no second End from the tick (review L2).
    tbl::Bend other = bend(6100, 0.2f);
    set_scene(other.scene, 0x0000000000AB12ull, 0x1234);
    r = snd.on_bend(other);
    CHECK(r.new_run && r.out && r.out->run == 3);
    CHECK(r.end_first && r.end_first->run == 2 && r.end_first->end.kind == tbl::Kind::End);
    CHECK(r.end_first && r.out && r.end_first->end.seq < r.out->seq);
    CHECK(snd.tick(6101, ends, 4) == 0);
    // Run 3 ends by the tick; then another tree on the INACTIVE channel: no End again (it was already sent).
    CHECK(snd.tick(6300, ends, 4) == 1 && ends[0].run == 3);
    CHECK(ends[0].end.seq > 0 && ends[0].end.seq <= snd.seq());
    tbl::Bend third = bend(6400, 0.2f);
    set_scene(third.scene, 0x0000000000CD34ull, 0x1234);
    r = snd.on_bend(third);
    CHECK(r.new_run && r.out && r.out->run == 4 && !r.end_first);
    CHECK(snd.tick(6600, ends, 4) == 1 && ends[0].run == 4);
    // Refused: no scene id, null direction, not a player, not finite.
    tbl::Bend nb = bend(7000, 0.2f);
    std::memset(nb.scene, 0, 16);
    CHECK(snd.on_bend(nb).refused);
    nb = bend(7000, 0.2f);
    nb.dx = nb.dz = 0.0f;
    CHECK(snd.on_bend(nb).refused);
    nb = bend(7000, 0.2f, 0xB0100723);
    CHECK(snd.on_bend(nb).refused);
    nb = bend(7000, NAN);
    CHECK(snd.on_bend(nb).refused);
}

void test_out_queue() {
    tbl::OutQueue q;
    for (uint32_t i = 1; i <= 10; ++i) q.push(sample(kHost, i, 1, 0.1f, i));
    CHECK(q.size() == tbl::OutQueue::kMax);
    CHECK(q.dropped() == 2);
    auto s = q.pop();
    CHECK(s && s->seq == 3);  // the oldest two were dropped
    // An End is kept over Samples when full.
    tbl::OutQueue q2;
    q2.push(sample(kHost, 1, 1, 0.1f, 1, tbl::Kind::End));
    for (uint32_t i = 2; i <= 9; ++i) q2.push(sample(kHost, i, 2, 0.1f, i));
    auto first = q2.pop();
    CHECK(first && first->kind == tbl::Kind::End && first->seq == 1);
    q2.clear();
    CHECK(q2.size() == 0 && q2.discarded() == 7);
}

void test_receiver() {
    tbl::Receiver rx;
    // Nothing yet.
    CHECK(!rx.query(kHost, 0, 1000));
    CHECK(rx.on_sample(sample(kHost, 1, 1, 0.2f, 5000), 7, 1000) == tbl::Rx::NewRun);
    auto p = rx.query(kHost, 0, 1000);
    CHECK(p && near_f(p->amount, 0.2f) && p->run == 1 && p->seq == 1 && p->age_ms == 0);
    CHECK(p && near_f(p->dx, 1.0f) && near_f(p->dz, 0.0f));
    // The other key is not mixed in.
    CHECK(!rx.query(kGuest, 0, 1000));
    CHECK(!rx.query(kHost, 1, 1000));
    // An old or repeated seq is dropped.
    CHECK(rx.on_sample(sample(kHost, 1, 1, 0.9f, 5000), 7, 1010) == tbl::Rx::OldSeq);
    // The next sample, 50 ms later in the sender's clock.
    CHECK(rx.on_sample(sample(kHost, 2, 1, 0.6f, 5050), 7, 1050) == tbl::Rx::Accepted);
    // Rendered 100 ms behind the newest: right at receive time -> alpha 1 - 100/50 < 0 -> the previous value.
    p = rx.query(kHost, 0, 1050);
    CHECK(p && near_f(p->amount, 0.2f));
    // 75 ms later: half-way.
    p = rx.query(kHost, 0, 1125);
    CHECK(p && near_f(p->amount, 0.4f));
    // 100 ms later and after: the newest.
    p = rx.query(kHost, 0, 1150);
    CHECK(p && near_f(p->amount, 0.6f));
    CHECK(p && p->age_ms == 100);
    // Older than 500 ms: nothing (a lost End).
    CHECK(rx.query(kHost, 0, 1550));
    CHECK(!rx.query(kHost, 0, 1551));
    CHECK(!rx.query(kHost, 0, 1600));
    // End: nothing applied any more.
    CHECK(rx.on_sample(sample(kHost, 3, 1, 0.6f, 5100, tbl::Kind::End), 7, 1100) == tbl::Rx::Ended);
    CHECK(!rx.query(kHost, 0, 1101));
    // A new run after the End: its first sample alone (no interpolation with the old run).
    CHECK(rx.on_sample(sample(kHost, 4, 2, -0.3f, 6000), 7, 2000) == tbl::Rx::NewRun);
    p = rx.query(kHost, 0, 2000);
    CHECK(p && near_f(p->amount, -0.3f) && p->run == 2);
    // A new connection forgets everything first: an old seq is accepted there.
    CHECK(rx.on_sample(sample(kHost, 1, 1, 0.5f, 100), 8, 2010) == tbl::Rx::NewRun);
    CHECK(rx.epoch_resets() == 1);
    p = rx.query(kHost, 0, 2010);
    CHECK(p && near_f(p->amount, 0.5f));
    // The sender restarted on the same connection (seq back to 1): dropped within 2 s, accepted after.
    CHECK(rx.on_sample(sample(kHost, 2, 1, 0.5f, 150), 8, 2060) == tbl::Rx::Accepted);
    CHECK(rx.on_sample(sample(kHost, 1, 1, 0.1f, 10), 8, 3000) == tbl::Rx::OldSeq);
    CHECK(rx.on_sample(sample(kHost, 1, 1, 0.1f, 10), 8, 4100) == tbl::Rx::NewRun);
    // Two throwers at once.
    CHECK(rx.on_sample(sample(kGuest, 1, 1, 0.7f, 10), 8, 4100) == tbl::Rx::NewRun);
    CHECK(rx.query(kGuest, 0, 4100) && rx.query(kHost, 0, 4100));
    // Interpolated direction is normalized.
    tbl::Receiver r2;
    tbl::Sample a = sample(kGuest, 1, 1, 0.0f, 0);
    a.dx = 1.0f;
    a.dz = 0.0f;
    tbl::Sample b = sample(kGuest, 2, 1, 0.0f, 50);
    b.dx = 0.0f;
    b.dz = 1.0f;
    r2.on_sample(a, 1, 0);
    r2.on_sample(b, 1, 50);
    const auto q = r2.query(kGuest, 0, 125);  // half-way (rendered 100 ms behind)
    CHECK(q && near_f(q->dx * q->dx + q->dz * q->dz, 1.0f) && near_f(q->dx, q->dz));
    // Samples further apart than 1 s are not interpolated: the newest.
    tbl::Receiver r3;
    r3.on_sample(sample(kGuest, 1, 1, 0.0f, 0), 1, 0);
    r3.on_sample(sample(kGuest, 2, 1, 1.0f, 5000), 1, 400);
    const auto q3 = r3.query(kGuest, 0, 400);
    CHECK(q3 && near_f(q3->amount, 1.0f));
    // The table: four entries, an ended one reused, then full.
    tbl::Receiver r4;
    for (uint32_t i = 0; i < 4; ++i) CHECK(r4.on_sample(sample(0xA0000010 + i, 1, 1, 0.1f, 0), 1, 0) == tbl::Rx::NewRun);
    CHECK(r4.on_sample(sample(0xA0000020, 1, 1, 0.1f, 0), 1, 0) == tbl::Rx::TableFull);
    CHECK(r4.on_sample(sample(0xA0000011, 2, 1, 0.1f, 0, tbl::Kind::End), 1, 10) == tbl::Rx::Ended);
    CHECK(r4.on_sample(sample(0xA0000020, 1, 1, 0.1f, 0), 1, 20) == tbl::Rx::NewRun);
    CHECK(!r4.query(0xA0000011, 0, 20));
}

void test_decide() {
    tbl::Pose p;
    set_scene(p.scene, 0xFC82, 0x1234);
    tbl::SlotView v;
    CHECK(tbl::decide(false, v, p) == tbl::Outcome::NotClient);
    CHECK(tbl::decide(true, v, p) == tbl::Outcome::Unreadable);
    v.read_ok = true;
    v.kind = tbl::kSlotEmpty;
    CHECK(tbl::decide(true, v, p) == tbl::Outcome::SlotEmpty);
    v.kind = 0;
    v.state = 2;  // still in flight
    CHECK(tbl::decide(true, v, p) == tbl::Outcome::NotAttached);
    v.state = tbl::kStateAttached;
    set_scene(v.scene, 0xFC83, 0x1234);
    CHECK(tbl::decide(true, v, p) == tbl::Outcome::SceneMismatch);
    set_scene(v.scene, 0xFC82, 0x1234);
    CHECK(tbl::decide(true, v, p) == tbl::Outcome::WouldApply);
    for (int i = 0; i < static_cast<int>(tbl::Outcome::Count); ++i) {
        CHECK(std::strcmp(tbl::outcome_name(static_cast<tbl::Outcome>(i)), "?") != 0);
    }
    for (int i = 0; i < static_cast<int>(tbl::Rx::Count); ++i) {
        CHECK(std::strcmp(tbl::rx_name(static_cast<tbl::Rx>(i)), "?") != 0);
    }
    // S29 guards: off after a fault first, then the main thread, then this PC's own bend on the same tree.
    CHECK(tbl::apply_gate(true, true, false) == tbl::Outcome::Disabled);
    CHECK(tbl::apply_gate(false, false, false) == tbl::Outcome::SkippedNotMain);
    CHECK(tbl::apply_gate(false, true, true) == tbl::Outcome::SkippedOwnBend);
    CHECK(tbl::apply_gate(false, true, false) == tbl::Outcome::WouldApply);
    {
        uint8_t sc[16];
        set_scene(sc, 0xFC82, 0x1234);
        CHECK(tbl::own_bend_same_tree(0xFC82, 0x1234, 1000000, sc, 1100000));   // 100 ms after the own SetBend
        CHECK(!tbl::own_bend_same_tree(0xFC82, 0x1234, 1000000, sc, 1150000));  // 150 ms: the own run ended
        CHECK(!tbl::own_bend_same_tree(0xFC83, 0x1234, 1000000, sc, 1000001));  // another tree
        CHECK(!tbl::own_bend_same_tree(0xFC82, 0x1234, 0, sc, 10));             // never bent here
        CHECK(!tbl::own_bend_same_tree(0xFC82, 0x1234, 2000, sc, 1000));        // clock before (another thread's store)
    }
    // Review B-M1: the local player's inline bend blocks any application for 150 ms (after the other guards).
    CHECK(tbl::apply_gate(false, true, false, true) == tbl::Outcome::SkippedOwnInline);
    CHECK(tbl::apply_gate(false, true, true, true) == tbl::Outcome::SkippedOwnBend);
    CHECK(tbl::apply_gate(false, false, false, true) == tbl::Outcome::SkippedNotMain);
    CHECK(tbl::own_inline_recent(1000000, 1149999));
    CHECK(!tbl::own_inline_recent(1000000, 1150000));
    CHECK(!tbl::own_inline_recent(0, 10));
    // Review B-L1: the seqlock gives back what was recorded, and guard 2 reads it.
    {
        tbl::OwnBend ob;
        uint64_t lo = 1, hi = 1, us = 1;
        CHECK(ob.read(lo, hi, us) == tbl::OwnBend::Read::Ok && lo == 0 && hi == 0 && us == 0);
        uint8_t sc[16];
        set_scene(sc, 0xFC82, 0x1234);
        CHECK(!ob.same_tree_now(sc, 1000));
        CHECK(ob.record(0xFC82, 0x1234, 1000000));
        CHECK(ob.read(lo, hi, us) == tbl::OwnBend::Read::Ok && lo == 0xFC82 && hi == 0x1234 && us == 1000000);
        CHECK(ob.same_tree_now(sc, 1100000));
        CHECK(!ob.same_tree_now(sc, 1200000));
        set_scene(sc, 0xFC83, 0x1234);
        CHECK(!ob.same_tree_now(sc, 1100000));
    }
    CHECK(tbl::during_apply(1000000, 1100000));
    CHECK(!tbl::during_apply(1000000, 1200000));
    CHECK(!tbl::during_apply(0, 5));
    // Lines: on a change at once, else once a second.
    CHECK(tbl::apply_line_due(0xFF, 6, 0, 10));
    CHECK(!tbl::apply_line_due(6, 6, 10, 500000));
    CHECK(tbl::apply_line_due(6, 6, 10, 1000010));
    CHECK(tbl::apply_line_due(6, 7, 10, 20));
}

void test_key_table() {
    tbl::KeyTable<16> t;
    CHECK(t.get(0) == nullptr);
    CHECK(t.get(0xB0100723) == nullptr);  // not a player
    tbl::KeyStats* a = t.get(kHost);
    CHECK(a && a->key.load() == kHost);
    CHECK(t.get(kHost) == a);
    tbl::KeyStats* b = t.get(kGuest);
    CHECK(b && b != a);
    for (uint32_t i = 0; i < 20; ++i) (void)t.get(0xA0200000 + i);
    CHECK(t.full() > 0);
    CHECK(t.get(kHost) == a);
    CHECK(t.used() == 16);
    a->m3_calls.store(5);
    t.clear();
    CHECK(t.used() == 0 && t.full() == 0 && t.at(0).m3_calls.load() == 0);
    // Review L1: the switch gives a clean table; the old one keeps its entries until the next switch clears it.
    tbl::KeySwitch<16> ks;
    for (uint32_t i = 0; i < 20; ++i) (void)ks.active().get(0xA0300000 + i);
    CHECK(ks.active().full() > 0);
    tbl::KeyTable<16>& before = ks.active();
    ks.switch_tables();
    CHECK(&ks.active() != &before && ks.active().used() == 0 && ks.active().full() == 0 && ks.switches() == 1);
    CHECK(ks.active().get(kGuest) != nullptr);
    ks.switch_tables();
    CHECK(&ks.active() == &before && ks.active().used() == 0);  // cleared when it became active again
}

void test_tid_set() {
    tbl::TidSet<4> a;
    tbl::TidSet<8> b;
    a.add(0);
    CHECK(a.size() == 0);
    a.add(100);
    a.add(100);
    a.add(200);
    CHECK(a.size() == 2 && a.has(100) && a.has(200) && !a.has(300));
    a.add(300);
    a.add(400);
    a.add(500);
    CHECK(a.size() == 4 && a.over() == 1);
    b.add(200);
    b.add(999);
    CHECK(a.common(b) == 1 && b.common(a) == 1);
}

int32_t rel32(const uint8_t* p) {
    int32_t r = 0;
    std::memcpy(&r, p + 1, 4);
    return r;
}

void test_constants() {
    // The calls verified in Ghidra land on the hooked entries.
    CHECK(tbl::kApplyCallBytes[0] == 0xE8);
    CHECK(static_cast<int64_t>(tbl::kApplyCall) + 5 + rel32(tbl::kApplyCallBytes) == static_cast<int64_t>(tbl::kApply));
    CHECK(tbl::kSetBendCallBytes[0] == 0xE8);
    CHECK(static_cast<int64_t>(tbl::kSetBendCall) + 5 + rel32(tbl::kSetBendCallBytes) ==
          static_cast<int64_t>(tbl::kSetBend));
    CHECK(tbl::kTickJumpBytes[0] == 0xE9);
    CHECK(static_cast<int64_t>(tbl::kTickJump) + 5 + rel32(tbl::kTickJumpBytes) == static_cast<int64_t>(tbl::kTick));
    // The patches are whole verified instructions.
    CHECK(tbl::kVf2Patch == 5 && tbl::kVf2Head[0] == 0x48 && tbl::kVf2Head[4] == 0x08);
    CHECK(tbl::kApplyPatch == 7 && tbl::kApplyHead[0] == 0x48 && tbl::kApplyHead[2] == 0xC4 && tbl::kApplyHead[6] == 0x10);
    CHECK(tbl::kSetBendPatch == 5 && tbl::kSetBendHead[5] == 0x57);
    CHECK(tbl::kTickPatch[0] == 5);
    bool boundary = false;
    for (const uint32_t b : tbl::kTickBoundaries) boundary = boundary || b == tbl::kTickPatch[0];
    CHECK(boundary);
    static_assert(sizeof tbl::kVf2Head >= tbl::kVf2Patch);
    static_assert(sizeof tbl::kApplyHead >= tbl::kApplyPatch);
    static_assert(sizeof tbl::kSetBendHead >= tbl::kSetBendPatch);
    static_assert(tbl::kFrameSize == 57);
    static_assert(tbl::kControlTreeBend == 15);
    static_assert(tbl::slot_target(0x1000, 1) == 0x1000 + 0x20 + 0x58);
    static_assert(tbl::slot_state(0x1000, 1) == 0x1000 + 0x9BC + 0x1A0);
    static_assert(tbl::kState_Dir == tbl::kState_Amount + 8);
}

}  // namespace

int main() {
    test_frame();
    test_sender();
    test_out_queue();
    test_receiver();
    test_decide();
    test_key_table();
    test_tid_set();
    test_constants();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_tree_bend: all passed\n");
    return 0;
}
