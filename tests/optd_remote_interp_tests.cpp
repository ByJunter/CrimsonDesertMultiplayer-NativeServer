// P8 remote movement: the per-actor interpolation delay and the horse that follows its rider
// (src/mod/session2/remote_interp_logic.h, horse_follow_logic.h; docs/research/pendientes/p8-impl-interp-remota.md).
//
// SCOPE: offline logic only. The delay target and its approach (no jump, the rates), the insert
// observation (resets at 1000 / 2500, older samples, cadence), the lock-free per-controller table
// (lookup, another actor behind a reused controller, stale reclaim, full, four threads at once), the
// buffer -> actor chain and the last sample on fake memory, the decision per class, a model of the
// game's interpolation buffer (0x11B1FD0 / 0x11B2420) driven by an NPC sending every 1.1 s, and the
// horse's synthetic MoveActorAck byte by byte. P8 mount ride (pendientes/p8-impl-monturas-cabalgata.md): the
// reverse direction's decisions, its monotonic per-horse stream and interval quantiles (S13's G->H ride modelled),
// the rider -> horse pairs, the pair's target and its no-jump ramp, and the full-buffer cut. They do NOT test the
// hooks, the threshold patch, the ABI (registers, ctx.rip), the byte checks against the running game, the
// re-entrant OnRecvPacket delivery nor the game's behaviour.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <thread>
#include <vector>

#include "mod/session2/horse_follow_logic.h"
#include "mod/session2/remote_interp_logic.h"

using namespace cdmp::session2;
using rip::KeyClass;
using rip::Mode;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

// ---------------------------------------------------------------- the delay

void test_keys_and_target() {
    CHECK(rip::key_class(0xA0100001) == KeyClass::Character);
    CHECK(rip::key_class(0xB010014D) == KeyClass::Npc);
    CHECK(rip::key_class(0x90100000) == KeyClass::Other);
    CHECK(rip::key_class(0) == KeyClass::Other);
    CHECK(rip::sane_game_delay(200) == 200 && rip::sane_game_delay(-5) == 200 && rip::sane_game_delay(70000) == 200);
    // No gap yet: the floor. NPC at 1.1 s: 1250. Dense: the floor. Huge: 1800.
    CHECK(rip::target_delay({0, 0, 0}, 200) == 200);
    CHECK(rip::target_delay({1086, 1100, 1149}, 200) == 1299);
    CHECK(rip::target_delay({28, 30, 29}, 200) == 200);
    CHECK(rip::target_delay({3000, 10, 10}, 200) == 1800);
    // The floor follows a raised game value (an F0 experiment) and never undercuts it.
    CHECK(rip::target_delay({28, 30, 29}, 1300) == 1300);
    CHECK(rip::target_delay({1100, 0, 0}, 1300) == 1300);
    CHECK(rip::target_delay({1100, 0, 0}, 2500) == 2500);
}

void test_approach() {
    // Up at 0.5 ms/ms, down at 0.25 ms/ms, never past the target, a long absence capped at 100 ms.
    uint32_t d = rip::to_fix(200);
    d = rip::approach(d, 1300, 16);
    CHECK(d == rip::to_fix(208));
    d = rip::approach(d, 1300, 100000);
    CHECK(d == rip::to_fix(258));
    d = rip::to_fix(1290);
    CHECK(rip::approach(d, 1300, 100) == rip::to_fix(1300));
    d = rip::to_fix(1300);
    CHECK(rip::approach(d, 200, 16) == rip::to_fix(1296));
    CHECK(rip::approach(rip::to_fix(201), 200, 16) == rip::to_fix(200));
    CHECK(rip::approach(rip::to_fix(500), 500, 16) == rip::to_fix(500));
    // 7 ms frames: the 1/16 ms fixed point keeps the 3.5 ms / 1.75 ms steps.
    CHECK(rip::approach(rip::to_fix(200), 1300, 7) == rip::to_fix(200) + 7 * 8);
    CHECK(rip::approach(rip::to_fix(1300), 200, 7) == rip::to_fix(1300) - 7 * 4);
    // From 200 to 1299 at 60 fps: ~2.2 s, every step <= 0.5 * dt.
    d = rip::to_fix(200);
    int frames = 0;
    while (d < rip::to_fix(1299) && frames < 1000) {
        const uint32_t next = rip::approach(d, 1299, 16);
        CHECK(next - d <= 8 * rip::kFix);
        d = next;
        ++frames;
    }
    CHECK(frames > 130 && frames < 140);
}

void test_observe_set() {
    rip::SetIn in;
    CHECK(!rip::observe_set(in).reset);  // empty buffer: nothing to compare
    in.count = 3;
    in.ts_last = 10000;
    in.ts_new = 10000;
    CHECK(rip::observe_set(in).older);
    in.ts_new = 11100;  // an NPC's 1.1 s
    rip::SetOut o = rip::observe_set(in);
    CHECK(o.gap == 1100 && o.reset && o.reset_1000 && o.record);
    in.threshold = rip::kPatchedThreshold;
    o = rip::observe_set(in);
    CHECK(!o.reset && o.reset_1000 && o.record);
    in.ts_new = 13000;  // 3 s: emptied even at 2500; still a recorded gap (<= 5 s)
    o = rip::observe_set(in);
    CHECK(o.reset && o.record && o.gap == 3000);
    in.ts_new = 16000;  // 6 s: a pause, not the cadence
    o = rip::observe_set(in);
    CHECK(o.reset && !o.record);
    in.ts_new = 10030;
    in.moved = false;  // still: the game drops it (after the reset test); not a cadence
    o = rip::observe_set(in);
    CHECK(!o.reset && !o.record && o.gap == 30);
}

// ---------------------------------------------------------------- the table

void test_table() {
    rip::SlotTable<64> t;
    auto a = t.get(0x1000, 0xB0000001, 100, true);
    CHECK(a.slot && a.created);
    CHECK(t.get(0x1000, 0xB0000001, 120, false).slot == a.slot);
    CHECK(!t.get(0x2000, 0xB0000002, 120, false).slot);  // not made without create
    a.slot->record_gap(1100);
    a.slot->d_fix.store(rip::to_fix(900));
    // Another actor behind the same controller address: a fresh state.
    auto b = t.get(0x1000, 0xB0000009, 130, true);
    CHECK(b.slot == a.slot && b.slot->d_fix.load() == 0 && b.slot->gaps()[0] == 0 && t.rekeyed() == 1);
    // Fill the probe window of one hash: 33 controllers that collide cannot all fit; a stale one is reclaimed.
    rip::SlotTable<32> small;
    for (uintptr_t i = 1; i <= 32; ++i) CHECK(small.get(i * 0x100, 0xB0000000 + static_cast<uint32_t>(i), 0, true).slot);
    CHECK(!small.get(0x9900, 0xB0009999, 5000, true).slot && small.full() == 1);  // none stale yet
    const auto r = small.get(0x9900, 0xB0009999, 0 + rip::kStaleSlotMs + 1, true);
    CHECK(r.slot && r.created && small.reclaimed() == 1);
    // Four threads, 2000 controllers, at once: every one found again, no duplicates beyond races.
    static rip::SlotTable<8192> big;  // ~0.5 MB: not on the stack
    std::vector<std::thread> th;
    for (int k = 0; k < 4; ++k) {
        th.emplace_back([] {
            for (int rep = 0; rep < 20; ++rep) {
                for (uintptr_t i = 1; i <= 2000; ++i) {
                    auto f = big.get(0x10000000 + i * 0x5A0, 0xB0100000 + static_cast<uint32_t>(i), 1000, true);
                    if (f.slot) f.slot->record_gap(static_cast<uint32_t>(i));
                }
            }
        });
    }
    for (auto& x : th) x.join();
    size_t found = 0;
    for (uintptr_t i = 1; i <= 2000; ++i) {
        auto f = big.get(0x10000000 + i * 0x5A0, 0xB0100000 + static_cast<uint32_t>(i), 1001, false);
        if (f.slot && f.slot->key.load() == 0xB0100000 + i) ++found;
    }
    CHECK(found == 2000);
    CHECK(big.full() == 0 && big.used() >= 2000 && big.used() <= 2000 + 64);
    CHECK(big.live(1000) >= 2000);
}

// ---------------------------------------------------------------- fake memory

struct FakeMem {
    std::map<uintptr_t, uint8_t> b;
    void put(uintptr_t a, const void* p, size_t n) {
        for (size_t i = 0; i < n; ++i) b[a + i] = static_cast<const uint8_t*>(p)[i];
    }
    void put32(uintptr_t a, uint32_t v) { put(a, &v, 4); }
    void put64(uintptr_t a, uint64_t v) { put(a, &v, 8); }
    bool get(uintptr_t a, void* out, size_t n) const {
        for (size_t i = 0; i < n; ++i) {
            auto it = b.find(a + i);
            if (it == b.end()) return false;
            static_cast<uint8_t*>(out)[i] = it->second;
        }
        return true;
    }
    bool u32(uintptr_t a, uint32_t& v) const { return get(a, &v, 4); }
    bool u64(uintptr_t a, uint64_t& v) const { return get(a, &v, 8); }
    bool vec3(uintptr_t a, float* v) const { return get(a, v, 12); }
    bool ptr(uintptr_t a, uintptr_t& v) const { return get(a, &v, 8); }
};

void test_resolve() {
    FakeMem m;
    const uintptr_t mc = 0x50000, cc = 0x60000, actor = 0x70000, arr = 0x80000;
    m.put64(mc + rip::kMcCc, cc);
    m.put64(cc + rip::kCcMc, mc);
    m.put64(mc + rip::kMcActor, actor);
    m.put32(actor + rip::kActorKey, 0xB010014D);
    rip::ResolveFail why{};
    auto r = rip::resolve(m, mc + rip::kPosBuffer, rip::kPosBuffer, &why);
    CHECK(r.ok && r.mc == mc && r.actor == actor && r.key == 0xB010014D && why == rip::ResolveFail::None);
    r = rip::resolve(m, mc + rip::kRotBuffer, rip::kRotBuffer, &why);
    CHECK(r.ok && r.mc == mc);
    // The rotation buffer taken as a position one: another "mc" whose CC does not point back.
    r = rip::resolve(m, mc + rip::kRotBuffer, rip::kPosBuffer, &why);
    CHECK(!r.ok && why == rip::ResolveFail::NoCc);
    m.put64(mc + rip::kRotBuffer - rip::kPosBuffer + rip::kMcCc, cc);
    r = rip::resolve(m, mc + rip::kRotBuffer, rip::kPosBuffer, &why);
    CHECK(!r.ok && why == rip::ResolveFail::NotItsMc);
    // The last sample.
    const uintptr_t buf = mc + rip::kPosBuffer;
    m.put32(buf + rip::kBufCount, 0);
    auto ls = rip::last_sample(m, buf, true);
    CHECK(ls.ok && ls.count == 0);
    m.put32(buf + rip::kBufCount, 2);
    m.put64(buf + rip::kBufArray, arr);
    const float p[3] = {1.5f, 2.0f, -3.0f};
    m.put(arr + rip::kSampleSize, p, 12);
    m.put64(arr + rip::kSampleSize + rip::kSampleTs, 123456);
    ls = rip::last_sample(m, buf, true);
    CHECK(ls.ok && ls.count == 2 && ls.ts == 123456 && ls.pos[0] == 1.5f && ls.pos[2] == -3.0f);
    m.put32(buf + rip::kBufCount, 999);  // garbage count
    CHECK(!rip::last_sample(m, buf, true).ok);
    const float q[3] = {1.5f, 2.0f, -3.00001f};
    CHECK(!rip::moved(p, q));
    const float s[3] = {1.5f, 2.0f, -3.001f};
    CHECK(rip::moved(p, s));
}

void test_decide() {
    rip::DelayIn in;
    in.mode = Mode::On;
    in.applying = true;
    in.cls = KeyClass::Npc;
    in.have_slot = true;
    in.updated = true;
    in.d_fix = rip::to_fix(1250);
    in.game_delay = 200;
    auto o = rip::decide_delay(in);
    CHECK(o.replaced && o.value == 1250 && o.would == 1250);
    in.mode = Mode::Probe;
    o = rip::decide_delay(in);
    CHECK(!o.replaced && o.value == 200 && o.would == 1250);
    in.mode = Mode::On;
    in.applying = false;  // the hooks are not all in
    CHECK(!rip::decide_delay(in).replaced);
    in.applying = true;
    in.cls = KeyClass::Character;  // a player's character: never
    CHECK(!rip::decide_delay(in).replaced && rip::decide_delay(in).value == 200);
    in.cls = KeyClass::Npc;
    in.updated = false;  // no mode-2 update reads this buffer (a locally simulated NPC)
    CHECK(!rip::decide_delay(in).replaced);
    in.updated = true;
    in.d_fix = 0;
    CHECK(!rip::decide_delay(in).replaced);
    in.d_fix = rip::to_fix(200);  // equal to the game's: nothing to replace
    CHECK(!rip::decide_delay(in).replaced);
    // update_step: a character's slot is marked but keeps no delay; an NPC's moves towards its target.
    rip::Slot s;
    s.reinit(0xA0100001, 0);
    rip::update_step(s, KeyClass::Character, 1000, 200);
    CHECK(s.updated.load() == 1 && s.d_fix.load() == 0);
    s.reinit(0xB010014D, 0);
    s.record_gap(1100);
    rip::update_step(s, KeyClass::Npc, 1000, 200);
    CHECK(s.d_fix.load() == rip::to_fix(200));  // first step: no dt yet
    rip::update_step(s, KeyClass::Npc, 1016, 200);
    CHECK(s.d_fix.load() == rip::to_fix(208));
}

void test_measures() {
    rip::Histogram<7> h(rip::kGapBounds);
    h.add(28);
    h.add(1100);
    h.add(99999);
    CHECK(h.n[0].load() == 1 && h.n[4].load() == 1 && h.n[7].load() == 1 && h.total() == 3);
    rip::TidSet<2> t;
    t.add(10);
    t.add(10);
    t.add(11);
    t.add(12);
    CHECK(t.count() == 2 && t.overflow.load() == 1);
}

// ---------------------------------------------------------------- a model of the game's buffer

// 0x11B1FD0 / 0x11B2420 reduced to positions on a line: insert with the reset at `thr`, read at
// now - D with the lerp and no extrapolation (movimiento-remoto-cliente.md §2).
struct BufferModel {
    struct S {
        double x;
        uint64_t ts;
    };
    std::vector<S> s;
    double shown = 0;
    void insert(double x, uint64_t ts, uint64_t now, uint32_t d, uint32_t thr) {
        if (!s.empty()) {
            if (ts <= s.back().ts) return;
            if (ts - s.back().ts > thr) {
                s.clear();
                s.push_back({shown, now - d});
            }
            if (std::fabs(x - s.back().x) <= 1e-4) return;
        }
        s.push_back({x, ts});
        while (s.size() > 30) s.erase(s.begin());
    }
    double read(uint64_t now, uint32_t d) {
        if (s.empty()) return shown;
        const uint64_t t = now - d;
        if (t <= s.front().ts) return shown;
        for (size_t i = 1; i < s.size(); ++i) {
            if (s[i - 1].ts <= t && t <= s[i].ts) {
                const double f = double(t - s[i - 1].ts) / double(s[i].ts - s[i - 1].ts);
                return shown = s[i - 1].x + (s[i].x - s[i - 1].x) * f;
            }
        }
        return shown = s.back().x;
    }
};

// An NPC walking 1 m/s whose controller sends every 1100 ms; 60 fps on the observer; the
// sample arrives 16 ms after its ts. Returns the max and min speed shown over the last 20 s.
void run_npc(bool ours, double& vmax, double& vmin, uint32_t& final_d) {
    BufferModel b;
    rip::Slot slot;
    slot.reinit(0xB010014D, 0);
    const uint32_t thr = ours ? rip::kPatchedThreshold : rip::kOrigThreshold;
    vmax = 0;
    vmin = 1e9;
    double prev = 0;
    uint64_t next_send = 1100;
    for (uint64_t now = 0; now < 40000; now += 16) {
        // Arrival.
        if (now >= next_send + 16) {
            const uint64_t ts = next_send;
            uint32_t d = rip::kMinDelay;
            if (ours) {
                rip::SetIn in;
                in.count = static_cast<uint32_t>(b.s.size());
                in.ts_last = b.s.empty() ? 0 : b.s.back().ts;
                in.ts_new = ts;
                in.threshold = thr;
                const auto o = rip::observe_set(in);
                if (o.record) slot.record_gap(static_cast<uint32_t>(o.gap));
                rip::DelayIn di{Mode::On, true, KeyClass::Npc, true, slot.updated.load() != 0, slot.d_fix.load(), 200};
                d = rip::decide_delay(di).value;
            }
            b.insert(ts / 1000.0, ts, now, d, thr);
            next_send += 1100;
        }
        // The frame.
        uint32_t d = rip::kMinDelay;
        if (ours) {
            rip::update_step(slot, KeyClass::Npc, now, 200);
            rip::DelayIn di{Mode::On, true, KeyClass::Npc, true, true, slot.d_fix.load(), 200};
            d = rip::decide_delay(di).value;
        }
        const double x = b.read(now, d);
        if (now >= 20000) {
            const double v = (x - prev) / 0.016;
            vmax = std::max(vmax, v);
            vmin = std::min(vmin, v);
        }
        prev = x;
    }
    final_d = rip::from_fix(slot.d_fix.load());
}

void test_buffer_model() {
    double vmax = 0, vmin = 0;
    uint32_t d = 0;
    run_npc(false, vmax, vmin, d);
    // The game today: ~1 m in ~200 ms (5 m/s), then still.
    CHECK(vmax > 4.0 && vmin < 0.01);
    run_npc(true, vmax, vmin, d);
    // Ours: a steady 1 m/s (the delay converged to max gap + 150).
    CHECK(d == 1250);
    CHECK(vmax < 1.1 && vmin > 0.9);
}

// ---------------------------------------------------------------- the horse

std::vector<uint8_t> rider_ack(uint32_t rider, uint64_t parent, uint64_t ts) {
    std::vector<uint8_t> p(hfl::kHeader + hfl::kAckSize, 0);
    p[0] = static_cast<uint8_t>(hfl::kOpMoveActorAck & 0xFF);
    p[1] = static_cast<uint8_t>(hfl::kOpMoveActorAck >> 8);
    p[2] = 0;
    p[3] = static_cast<uint8_t>(hfl::kAckSize & 0xFF);
    p[4] = static_cast<uint8_t>(hfl::kAckSize >> 8);
    uint8_t* x = p.data() + hfl::kHeader;
    std::memcpy(x + hfl::kKey, &rider, 4);
    const float rpos[3] = {10.0f, 2.6f, 20.0f};
    std::memcpy(x + hfl::kPos, rpos, 12);
    const uint32_t aux = 0xB0100777;
    std::memcpy(x + hfl::kAux, &aux, 4);
    const float local[3] = {0.0f, 1.6f, 0.57f};
    std::memcpy(x + hfl::kLocalPos, local, 12);
    const float vel[3] = {3.0f, 0.0f, 0.0f};
    std::memcpy(x + 0x74, vel, 12);
    std::memcpy(x + hfl::kParent, &parent, 8);
    const float hpos[3] = {10.0f, 1.0f, 19.5f};
    const float hq[4] = {0.0f, 0.518f, 0.0f, 0.855f};
    std::memcpy(x + hfl::kWorldPos, hpos, 12);
    std::memcpy(x + hfl::kWorldQuat, hq, 16);
    const float wscale[3] = {1.0f, 1.0f, 1.0f};
    std::memcpy(x + 0xB0, wscale, 12);
    std::memcpy(x + hfl::kTs, &ts, 8);
    x[0xDE] = 1;
    const float tail[3] = {0.25f, 0.5f, 7.5f};
    std::memcpy(x + 0xDF, tail, 12);
    return p;
}

void test_horse() {
    const uint32_t rider = 0xA0100001, horse = 0xB0100003;
    auto pkt = rider_ack(rider, horse, 555555);
    const std::span<const uint8_t> pay(pkt.data() + hfl::kHeader, hfl::kAckSize);
    hfl::Move m = hfl::read_move(pay, hfl::kAckSize);
    CHECK(m.shape == hfl::Shape::Ridden && m.key == rider && m.parent == horse && m.ts == 555555 && !m.world_zero);
    CHECK(hfl::read_move(pay.first(200), hfl::kAckSize).shape == hfl::Shape::NotMove);
    std::vector<uint8_t> h;
    CHECK(hfl::make_horse_ack(pkt, h));
    CHECK(h.size() == pkt.size());
    CHECK(std::memcmp(h.data(), pkt.data(), hfl::kHeader) == 0);  // same opcode, slot, length 235
    const uint8_t* y = h.data() + hfl::kHeader;
    const uint8_t* x = pkt.data() + hfl::kHeader;
    uint32_t k = 0;
    std::memcpy(&k, y, 4);
    CHECK(k == horse);
    CHECK(std::memcmp(y + hfl::kPos, x + hfl::kWorldPos, 12) == 0);
    CHECK(std::memcmp(y + hfl::kQuat, x + hfl::kWorldQuat, 16) == 0);
    uint32_t aux = 1;
    std::memcpy(&aux, y + hfl::kAux, 4);
    CHECK(aux == 0);
    CHECK(std::memcmp(y + hfl::kLocalPos, x + hfl::kWorldPos, 12) == 0);
    CHECK(std::memcmp(y + hfl::kLocalQuat, x + hfl::kWorldQuat, 16) == 0);
    float sc[3] = {};
    std::memcpy(sc, y + hfl::kLocalScale, 12);
    CHECK(sc[0] == 1.0f && sc[1] == 1.0f && sc[2] == 1.0f);
    CHECK(std::memcmp(y + 0x74, x + 0x74, 0x8C - 0x74) == 0);  // velocity and the vec3 after it: the rider's
    uint64_t par = 0;
    std::memcpy(&par, y + hfl::kParent, 8);
    CHECK(par == ~0ull);
    for (size_t i = hfl::kWorldPos; i < hfl::kWorldEnd; ++i) CHECK(y[i] == 0);
    CHECK(std::memcmp(y + hfl::kWorldEnd, x + hfl::kWorldEnd, hfl::kAckSize - hfl::kWorldEnd) == 0);  // u8, ts, tail
    // The horse's Ack is a horse on foot: the follower would never chain on it.
    CHECK(hfl::read_move(std::span<const uint8_t>(h.data() + hfl::kHeader, hfl::kAckSize), hfl::kAckSize).shape ==
          hfl::Shape::OnFoot);
    // Not a rider: on foot, an NPC with a parent, a non-0xB parent, a 64-bit parent, no world transform.
    CHECK(!hfl::make_horse_ack(rider_ack(rider, ~0ull, 1), h));
    CHECK(hfl::read_move(std::span<const uint8_t>(rider_ack(0xB01001F2, horse, 1)).subspan(5), hfl::kAckSize).shape ==
          hfl::Shape::OtherParent);
    CHECK(!hfl::make_horse_ack(rider_ack(rider, 0xA0100002, 1), h));
    CHECK(!hfl::make_horse_ack(rider_ack(rider, 0x1B0100003ull, 1), h));
    auto zero = rider_ack(rider, horse, 1);
    std::memset(zero.data() + hfl::kHeader + hfl::kWorldPos, 0, 12);
    CHECK(!hfl::make_horse_ack(zero, h));
    CHECK(hfl::read_move(std::span<const uint8_t>(zero).subspan(5), hfl::kAckSize).world_zero);
    auto wrong = rider_ack(rider, horse, 1);
    wrong[0] = 0;  // another opcode
    CHECK(!hfl::make_horse_ack(wrong, h));
    // The verdicts.
    CHECK(hfl::decide(Mode::Off, m, false) == hfl::Verdict::Skip);
    CHECK(hfl::decide(Mode::Probe, m, false) == hfl::Verdict::Probe);
    CHECK(hfl::decide(Mode::On, m, false) == hfl::Verdict::Make);
    CHECK(hfl::decide(Mode::On, m, true) == hfl::Verdict::OwnHorse);
    m.world_zero = true;
    CHECK(hfl::decide(Mode::On, m, false) == hfl::Verdict::NoWorld);
    // The key ring and the cadences.
    hfl::KeyRing<4> ring;
    for (uint32_t i = 1; i <= 6; ++i) ring.add(0xB0000000 + i);
    CHECK(!ring.contains(0xB0000001) && !ring.contains(0xB0000002) && ring.contains(0xB0000006) &&
          ring.contains(0xB0000003));
    ring.add(0xB0000006);
    CHECK(ring.next.load() == 6);
    ring.clear();
    CHECK(!ring.contains(0xB0000006));
    hfl::Cadence<4> c;
    CHECK(c.gap(rider, 1000) == 0 && c.gap(rider, 1028) == 28 && c.gap(rider, 1000) == 0 && c.last(rider) == 1000);
    CHECK(c.last(horse) == 0);
    CHECK(hfl::while_ridden(1000, 3500) && !hfl::while_ridden(1000, 5000) && !hfl::while_ridden(0, 10));
    hfl::CadenceHist hc;
    hc.add(28);
    hc.add(1015);
    hc.add(99999);
    CHECK(hc.n[1].load() == 1 && hc.n[7].load() == 1 && hc.n[8].load() == 1);
}

// ---------------------------------------------------------------- P8 mount ride: the reverse direction

void test_reverse() {
    const uint32_t rider = 0xA0100318, horse = 0xB010031A;  // S13: G's rider and horse
    // The buffer builder gives exactly make_horse_ack's bytes, and refuses a short buffer.
    auto pkt = rider_ack(rider, horse, 707758);
    std::vector<uint8_t> h;
    CHECK(hfl::make_horse_ack(pkt, h));
    uint8_t buf[512] = {};
    CHECK(hfl::make_horse_ack_into(pkt, buf, sizeof buf));
    CHECK(std::memcmp(buf, h.data(), h.size()) == 0);
    uint8_t small[239] = {};
    CHECK(!hfl::make_horse_ack_into(pkt, small, sizeof small));
    CHECK(!hfl::make_horse_ack_into(rider_ack(rider, ~0ull, 1), buf, sizeof buf));
    CHECK(!hfl::make_horse_ack_into(pkt, nullptr, 512));

    // The decision.
    const hfl::Move m = hfl::read_move(std::span<const uint8_t>(pkt).subspan(hfl::kHeader), hfl::kAckSize);
    using hfl::PairCheck;
    using hfl::Rev;
    CHECK(hfl::decide_reverse(Mode::Off, m, rider, PairCheck::Ok, 0) == Rev::Skip);
    CHECK(hfl::decide_reverse(Mode::On, m, rider, PairCheck::Ok, 0) == Rev::Derive);
    CHECK(hfl::decide_reverse(Mode::Probe, m, rider, PairCheck::Ok, 0) == Rev::Probe);
    CHECK(hfl::decide_reverse(Mode::On, m, 0, PairCheck::Ok, 0) == Rev::NotUser2);           // key not known yet
    CHECK(hfl::decide_reverse(Mode::On, m, 0xA0100001, PairCheck::Ok, 0) == Rev::NotUser2);  // the host's own rider
    CHECK(hfl::decide_reverse(Mode::On, m, rider, PairCheck::Unknown, 0) == Rev::Unverified);
    CHECK(hfl::decide_reverse(Mode::On, m, rider, PairCheck::NotOwned, 0) == Rev::NotOwned);
    CHECK(hfl::decide_reverse(Mode::On, m, rider, PairCheck::Ok, 707758) == Rev::Same);
    CHECK(hfl::decide_reverse(Mode::On, m, rider, PairCheck::Ok, 707759) == Rev::Older);
    CHECK(hfl::decide_reverse(Mode::On, m, rider, PairCheck::Ok, 707757) == Rev::Derive);
    hfl::Move foot = m;
    foot.shape = hfl::Shape::OnFoot;
    CHECK(hfl::decide_reverse(Mode::On, foot, rider, PairCheck::Ok, 0) == Rev::Skip);
    hfl::Move zero = m;
    zero.world_zero = true;
    CHECK(hfl::decide_reverse(Mode::On, zero, rider, PairCheck::Ok, 0) == Rev::NoWorld);

    // The monotonic stream.
    hfl::MonoTs<2> s;
    CHECK(s.note(horse, 1000).seq == hfl::Seq::First && s.last(horse) == 1000);
    auto n = s.note(horse, 1012);
    CHECK(n.seq == hfl::Seq::Newer && n.gap == 12);
    CHECK(s.note(horse, 1012).seq == hfl::Seq::Same && s.note(horse, 990).seq == hfl::Seq::Older);
    CHECK(s.last(horse) == 1012);  // an older one never moves it back
    CHECK(s.note(0xB0000001, 5).seq == hfl::Seq::First && s.note(0xB0000002, 5).seq == hfl::Seq::First);
    CHECK(s.last(horse) == 0);  // a full table took its slot round-robin
    s.clear();
    CHECK(s.last(0xB0000001) == 0 && s.last(0) == 0);

    // The intervals and their quantiles.
    hfl::Intervals iv;
    CHECK(iv.quantile(0.5) == 0);
    for (int i = 0; i < 90; ++i) iv.add(12);
    for (int i = 0; i < 9; ++i) iv.add(26);
    iv.add(956);
    iv.add(0);       // ignored
    iv.add(120000);  // a pause
    CHECK(iv.count() == 100 && iv.pauses.load() == 1 && iv.max.load() == 956);
    CHECK(iv.quantile(0.5) == 15 && iv.quantile(0.9) == 15 && iv.quantile(0.99) == 30 && iv.quantile(1.0) == 956);
    hfl::Intervals one;
    one.add(7);
    CHECK(one.quantile(0.5) == 7);  // never above the max seen

    // The pair cache: rechecked after kPairRecheckMs.
    hfl::PairCache<2> pc;
    CHECK(!pc.get(horse, 0));
    pc.put(horse, PairCheck::Ok, 5000);
    CHECK(pc.get(horse, 5999) == PairCheck::Ok && !pc.get(horse, 6000) && !pc.get(horse, 4000));
    pc.put(horse, PairCheck::NotOwned, 6000);
    CHECK(pc.get(horse, 6001) == PairCheck::NotOwned);
    pc.clear();
    CHECK(!pc.get(horse, 6001));
}

// S13's G->H ride, modelled: the rider's Acks every 12 ms, the horse's own every ~346 ms with 18 of 28 at or before
// the rider's last ts. With the reverse direction the horse's stream gets one sample per rider tick, never an older
// one: its intervals are the rider's.
void test_reverse_stream() {
    const uint32_t rider = 0xA0100318, horse = 0xB010031A;
    hfl::MonoTs<8> all, real;
    hfl::Intervals iv_all, iv_real;
    uint64_t derived = 0, stale_real = 0, same = 0;
    uint64_t t = 568906;
    uint64_t next_own = t + 200;
    int own_i = 0;
    for (int i = 0; i < 460; ++i, t += 12) {
        auto p = rider_ack(rider, horse, t);
        const hfl::Move m = hfl::read_move(std::span<const uint8_t>(p).subspan(hfl::kHeader), hfl::kAckSize);
        const hfl::Rev v = hfl::decide_reverse(Mode::On, m, rider, hfl::PairCheck::Ok, all.last(horse));
        if (v == hfl::Rev::Same) ++same;
        if (v == hfl::Rev::Derive) {
            uint8_t out[256];
            CHECK(hfl::make_horse_ack_into(p, out, sizeof out));
            uint64_t ts = 0;
            std::memcpy(&ts, out + hfl::kHeader + hfl::kTs, 8);
            CHECK(ts == t);  // the rider's tick, never moved
            const auto a = all.note(horse, ts);
            CHECK(a.seq == hfl::Seq::Newer || a.seq == hfl::Seq::First);
            if (a.seq == hfl::Seq::Newer) iv_all.add(a.gap);
            ++derived;
        }
        // The horse's own Ack, after the rider's of the same moment: one in three a little newer, the rest at or
        // before the rider's last tick (S13: 18 of 28).
        if (t >= next_own) {
            const uint64_t own_ts = (own_i % 3 == 0) ? t + 3 : t - (own_i % 2 ? 0 : 7);
            ++own_i;
            next_own += 346;
            const auto a = all.note(horse, own_ts);
            if (a.seq == hfl::Seq::Newer) iv_all.add(a.gap);
            if (a.seq == hfl::Seq::Same || a.seq == hfl::Seq::Older) ++stale_real;
            const auto r = real.note(horse, own_ts);
            if (r.seq == hfl::Seq::Newer) iv_real.add(r.gap);
        }
    }
    CHECK(derived == 460 && same == 0);  // one per rider tick (the horse's newer own ones are 3 ms ahead only)
    CHECK(stale_real > 0 && stale_real < static_cast<uint64_t>(own_i));  // its older ones: the client's buffer drops them
    CHECK(iv_all.quantile(0.5) <= 15 && iv_all.quantile(0.9) <= 15 && iv_all.max.load() <= 15);
    CHECK(iv_real.quantile(0.5) >= 300);  // its own cadence alone: the S13 symptom
}

// ---------------------------------------------------------------- P8 mount ride: one clock for the pair

void test_pairs() {
    rip::RidePairs p;
    const uint32_t rider = 0xA0100318, horse = 0xB010031A, other = 0xB0100003;
    CHECK(p.rider_of(horse, 1000) == 0);
    p.note(rider, horse, 1000);
    CHECK(p.rider_of(horse, 1000) == rider && p.rider_of(horse, 1000 + rip::kPairFreshMs - 1) == rider);
    CHECK(p.rider_of(horse, 1000 + 10000) == rider);  // a rider still in the saddle (no Move for 10 s): still paired
    static_assert(rip::kPairFreshMs >= 30000);
    CHECK(p.rider_of(horse, 1000 + rip::kPairFreshMs) == 0);  // only a long silence (a rider gone) forgets it
    CHECK(p.rider_of(other, 1000) == 0 && p.began.load() == 1);
    p.note(rider, horse, 1100);
    CHECK(p.began.load() == 1);  // the same pair goes on
    p.note(rider, 0, 1200);      // dismounted (on foot)
    CHECK(p.rider_of(horse, 1200) == 0 && p.ended.load() == 1);
    p.note(rider, other, 1300);  // another horse
    CHECK(p.rider_of(other, 1300) == rider && p.rider_of(horse, 1300) == 0 && p.began.load() == 2);
    p.note(0, horse, 1300);      // no rider key: nothing
    CHECK(p.rider_of(horse, 1300) == 0);
    p.note(0xA0100001, 0, 1400);  // a rider never seen on a horse: nothing to end
    size_t live = 0;
    p.for_each_live(1400, [&](uint32_t r, uint32_t h) {
        ++live;
        CHECK(r == rider && h == other);
    });
    CHECK(live == 1);
    // A full table: 16 more riders still find a slot (round-robin), and the newest is there.
    for (uint32_t i = 0; i < 20; ++i) p.note(0xA0200000 + i, 0xB0200000 + i, 2000);
    CHECK(p.rider_of(0xB0200013, 2000) == 0xA0200013);

    // The pair's target: the rider's only with three known gaps within it.
    bool dense = false;
    CHECK(rip::pair_target({12, 14, 26}, 900, 200, &dense) == 200 && dense);
    CHECK(rip::pair_target({12, 346, 26}, 900, 200, &dense) == 900 && !dense);
    CHECK(rip::pair_target({12, 0, 0}, 200, 200, &dense) == 200 && !dense);
    CHECK(rip::pair_target({200, 200, 200}, 350, 200, &dense) == 200 && dense);
    CHECK(rip::pair_target({12, 14, 26}, 900, 0, &dense) == 900 && !dense);
}

// The horse at the owned cap (900) when its rider mounts; derived samples every 12 ms. Paired: it reaches the rider's
// 200 without a jump (each frame's change within the approach's rates) and stays; on dismount (pair over, its own
// samples ~1 s) it goes back up the same way. Not paired: its own target only.
void test_pair_ramp() {
    rip::Slot s;
    s.reinit(0xB010031A, 0);
    s.d_fix.store(rip::to_fix(900));
    s.updated.store(1);
    uint64_t now = 100000;
    s.d_at.store(now);
    for (uint32_t g : {1100u, 1080u, 1150u}) s.record_gap(g);
    uint32_t prev = rip::from_fix(s.d_fix.load());
    uint32_t max_down = 0, max_up = 0;
    const uint32_t cap = rip::delay_cap(rip::kOwnedYes, 900);
    // Riding: samples every 12 ms for 6 s; a frame every 16 ms.
    for (int f = 0; f < 375; ++f) {
        now += 16;
        s.record_gap(12);
        bool dense = false;
        rip::update_step(s, KeyClass::Npc, now, 200, cap, /*pair_delay=*/200, &dense);
        const uint32_t d = rip::from_fix(s.d_fix.load());
        CHECK(d <= prev);  // monotonic down
        max_down = std::max(max_down, prev - d);
        prev = d;
    }
    CHECK(prev == 200);
    CHECK(max_down <= 5);  // <= 0.25 ms per ms over a 16 ms frame (+ rounding): no jump
    // Dismount: the pair is over (pair_delay 0), its own sparse samples come back.
    for (uint32_t g : {1100u, 1080u, 1150u}) s.record_gap(g);
    for (int f = 0; f < 200; ++f) {
        now += 16;
        rip::update_step(s, KeyClass::Npc, now, 200, cap, 0);
        const uint32_t d = rip::from_fix(s.d_fix.load());
        CHECK(d >= prev);  // monotonic up
        max_up = std::max(max_up, d - prev);
        prev = d;
    }
    CHECK(prev == 900 && max_up <= 9);  // <= 0.5 ms per ms over 16 ms (+ rounding), up to the owned cap

    // Paired but sparse (no reverse samples): its own target, never the rider's (the buffer would run dry).
    rip::Slot t;
    t.reinit(0xB010031A, 0);
    t.d_fix.store(rip::to_fix(900));
    t.updated.store(1);
    t.d_at.store(now);
    for (uint32_t g : {346u, 707u, 956u}) t.record_gap(g);
    for (int f = 0; f < 200; ++f) {
        now += 16;
        bool dense = true;
        rip::update_step(t, KeyClass::Npc, now, 200, cap, 200, &dense);
        CHECK(!dense);
    }
    CHECK(rip::from_fix(t.d_fix.load()) == 900);
    // The full buffer: 30 samples every 12 ms span ~350 ms; a delay of 810 would freeze the horse, cut to the first
    // sample's age; a delay already inside it, a buffer not yet full, or an age under the floor never raise it.
    bool cut = false;
    // Review of 3aaf038: the cut leaves a margin, so T = now - D lands strictly after ts[0] (the game does not move
    // the actor at T <= ts[0]).
    CHECK(rip::clamp_to_buffer(810, 30, 100000, 100360, 200, &cut) == 360 - rip::kBufMarginMs && cut);
    CHECK(100360 - rip::clamp_to_buffer(810, 30, 100000, 100360, 200) > 100000);
    CHECK(rip::clamp_to_buffer(810, 30, 100000, 100360, 200, &cut, 60) == 300 && cut);
    CHECK(rip::clamp_to_buffer(360, 30, 100000, 100360, 200, &cut) == 360 - rip::kBufMarginMs && cut);  // D == age
    CHECK(rip::clamp_to_buffer(300, 30, 100000, 100360, 200, &cut) == 300 && !cut);
    CHECK(rip::clamp_to_buffer(810, 20, 100000, 100360, 200, &cut) == 810 && !cut);
    CHECK(rip::clamp_to_buffer(810, 30, 100000, 100150, 200, &cut) == 200 && cut);
    CHECK(rip::clamp_to_buffer(180, 30, 100000, 100150, 200, &cut) == 180 && !cut);
    CHECK(rip::clamp_to_buffer(810, 30, 100000, 100020, 200, &cut) == 200 && cut);  // age under the margin
    CHECK(rip::buffer_margin({12, 14, 26}) == rip::kBufMarginMs && rip::buffer_margin({60, 12, 12}) == 60 &&
          rip::buffer_margin({500, 12, 0}) == rip::kBufMarginMaxMs);
    CHECK(rip::clamp_to_buffer(810, 30, 0, 100150, 200, &cut) == 810 && !cut);
    CHECK(rip::clamp_to_buffer(810, 30, 100500, 100150, 200, &cut) == 810 && !cut);  // a first sample "ahead"
    FakeMem fm;
    const uintptr_t buf = 0x90000 + rip::kPosBuffer, arr = 0xA0000;
    uint32_t count = 7;
    uint64_t ts0 = 7;
    fm.put32(buf + rip::kBufCount, 0);
    CHECK(rip::first_sample_ts(fm, buf, count, ts0) && count == 0 && ts0 == 0);
    fm.put32(buf + rip::kBufCount, 30);
    fm.put64(buf + rip::kBufArray, arr);
    fm.put64(arr + rip::kSampleTs, 100000);
    CHECK(rip::first_sample_ts(fm, buf, count, ts0) && count == 30 && ts0 == 100000);
    fm.put32(buf + rip::kBufCount, 999);
    CHECK(!rip::first_sample_ts(fm, buf, count, ts0));
    // A character (0xA) never gets a delay of its own, paired or not.
    rip::Slot c;
    c.reinit(0xA0100318, 0);
    rip::update_step(c, KeyClass::Character, now, 200, rip::kMaxDelay, 200);
    CHECK(c.d_fix.load() == 0 && c.updated.load() == 1);
}

}  // namespace

int main() {
    test_keys_and_target();
    test_approach();
    test_observe_set();
    test_table();
    test_resolve();
    test_decide();
    test_measures();
    test_buffer_model();
    test_horse();
    test_reverse();
    test_reverse_stream();
    test_pairs();
    test_pair_ramp();
    if (g_failures) {
        std::printf("optd_remote_interp: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("optd_remote_interp: all passed\n");
    return 0;
}
