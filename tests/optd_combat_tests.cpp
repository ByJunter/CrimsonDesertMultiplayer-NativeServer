// P8 session 25, the combat stream (docs/research/noche/combate-retardo.md §6, §7.1), offline logic only:
//  - combat_follow_logic.h, combat_follow_v2 (the host): who fights (2922 / 2991 with ANY character), the table's
//    fight entries (v1's view unchanged, eviction), the decisions with the game's thresholds (1e-4 m, 1.19e-7 rad),
//    the 100 ms keepalive that repeats the pose bit for bit, no cutoff without real samples, the probe's shadow
//    stream, the drift-free schedule and the nearest-first choice; a slow enemy sampled by v1 and by v2;
//  - remote_interp_logic.h, the combat class (the guest): the arrival gaps, dense / engaged, the class's target and
//    its 1 ms/ms descent, probe's shadow controller, the exclusions (owned, paired), the engagement table (one
//    writer, readers on other threads);
//  - real traces: the session 10 guest's enemy 0xB0101EF7 (report §2.5, cdmp_packets_20261010-173057.bin, t 458-492 s)
//    replayed through the game's insert rule and the controller, today vs the class: D <= 400 within 1 s of every
//    engagement; probe changes nothing and its shadow is exactly what 1 does. A synthetic 33 ms stream: D <= 160.
// Not the hooks, the threads, the timer, the network or the game.

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "mod/session2/combat_follow_logic.h"
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

std::vector<uint8_t> attack(uint32_t target, uint32_t attacker) {
    std::vector<uint8_t> v(199, 0);
    std::memcpy(v.data(), &target, 4);
    std::memcpy(v.data() + 4, &attacker, 4);
    return v;
}
std::vector<uint8_t> start_action(uint32_t key, uint32_t target, uint64_t ts) {
    std::vector<uint8_t> v(cfl::kStartAck, 0);
    const uint64_t none = ~0ull;
    std::memcpy(v.data(), &key, 4);
    std::memcpy(v.data() + cfl::kStartTarget, &target, 4);
    std::memcpy(v.data() + cfl::kStartTs, &ts, 8);
    std::memcpy(v.data() + cfl::kStartParent, &none, 8);
    return v;
}

// ================================================================ the host: combat_follow_v2

void test_fight_read() {
    constexpr uint32_t u2 = 0xA0100377, host = 0xA0100001, enemy = 0xB0101EF7, other = 0xB01020AA;
    // The enemy hits the host: not engaged with user 2, but fighting a character.
    cfl::Seen s = cfl::read_out(cfl::kOpAttackTargetAck, attack(host, enemy), u2);
    CHECK(s.engage == 0 && s.fight == enemy);
    // User 2 hits it: both.
    s = cfl::read_out(cfl::kOpAttackTargetAck, attack(enemy, u2), u2);
    CHECK(s.engage == enemy && s.fight == enemy);
    s = cfl::read_out(cfl::kOpAttackTargetAck, attack(enemy, host), u2);
    CHECK(s.engage == 0 && s.fight == enemy);
    // An enemy hitting another 0xB (a companion, S10 474.945): not a fight with a character.
    CHECK(cfl::read_out(cfl::kOpAttackTargetAck, attack(other, enemy), u2).fight == 0);
    // A 2991 aimed at the host: a fight and a sample; at nobody or at an 0xB: a sample only.
    s = cfl::read_out(cfl::kOpStartActionAck, start_action(enemy, host, 700000), u2);
    CHECK(s.engage == 0 && s.fight == enemy && s.sample == enemy && s.ts == 700000);
    CHECK(cfl::read_out(cfl::kOpStartActionAck, start_action(enemy, 0, 700000), u2).fight == 0);
    CHECK(cfl::read_out(cfl::kOpStartActionAck, start_action(enemy, other, 700000), u2).fight == 0);
    // User 2 unknown: nothing (no session 2 to stream to).
    CHECK(cfl::read_out(cfl::kOpAttackTargetAck, attack(host, enemy), 0).fight == 0);
}

void test_fight_table() {
    cfl::Table<4> t;
    t.fight(0xB0000001, 1000);  // fighting the host only
    t.engage(0xB0000002, 1000);
    // v1 sees only the engaged one; v2 both.
    std::array<cfl::Enemy, cfl::kMaxFollowed> l1{};
    std::array<cfl::Enemy, 4> l2{};
    CHECK(t.followed(1100, l1) == 1 && l1[0].key == 0xB0000002 && t.engaged(1100) == 1);
    CHECK(t.followed_v2(1100, l2) == 2 && t.engaged_v2_count(1100) == 2);
    // A fight keeps an engaged entry alive for v2 only.
    t.fight(0xB0000002, 1000 + cfl::kEngageMs);
    CHECK(t.followed(1001 + cfl::kEngageMs, l1) == 0 && t.followed_v2(1001 + cfl::kEngageMs, l2) == 1);
    // Full: the least recent of max(engaged, fight) is replaced (…01 fought at 1000; …02 at 1000 + 8000).
    t.engage(0xB0000003, 2000);
    t.fight(0xB0000004, 2100);
    t.fight(0xB0000005, 2200);
    CHECK(!t.find(0xB0000001) && t.find(0xB0000002) && t.find(0xB0000005));
    // Without fights the eviction is session 9's (least recently engaged).
    cfl::Table<2> u;
    u.engage(0xB0000011, 100);
    u.engage(0xB0000012, 200);
    u.engage(0xB0000013, 300);
    CHECK(!u.find(0xB0000011) && u.find(0xB0000012) && u.find(0xB0000013));
}

constexpr uint64_t G0 = 820970;  // the host client's game clock

cfl::Enemy anchored(cfl::Table<4>& t, uint32_t key, uint64_t now_ms) {
    t.fight(key, now_ms);
    t.sample(key, G0 - 10, G0, false, now_ms);
    return *t.find(key);
}

void test_decide_v2() {
    cfl::Table<4> t;
    constexpr uint32_t k = 0xB0000099;
    cfl::Enemy e = anchored(t, k, 1000);
    const float p0[3] = {-10674.29f, 556.51f, -4178.59f}, q0[4] = {0, 0.489f, 0, 0.872f};
    uint64_t ts = 0;
    bool ka = true;
    CHECK(cfl::decide(e, G0 + 33, 1033, p0, q0, ts, cfl::kPolicyV2, &ka) == cfl::Skip::None && !ka && ts == G0 + 23);
    t.sent(k, ts, G0 + 33, 1033, p0, q0);
    e = *t.find(k);
    // 5 mm in a tick: v1 calls it still (1 cm), v2 sends (the game inserts anything over 1e-4 m).
    const float p1[3] = {p0[0] + 0.005f, p0[1], p0[2]};
    CHECK(cfl::decide(e, G0 + 66, 1066, p1, q0, ts) == cfl::Skip::Still);
    CHECK(cfl::decide(e, G0 + 66, 1066, p1, q0, ts, cfl::kPolicyV2, &ka) == cfl::Skip::None && !ka);
    // Under 1e-4 m: still for v2 too (the game would not insert it).
    const float p2[3] = {p0[0] + 0.00005f, p0[1], p0[2]};
    CHECK(cfl::decide(e, G0 + 66, 1066, p2, q0, ts, cfl::kPolicyV2) == cfl::Skip::Still);
    // A turn of 1e-4 rad in place: v2 sends, v1 does not (0.99999 on the cosine).
    const double h = 0.5e-4, c = std::cos(h), sn = std::sin(h);
    const float q1[4] = {static_cast<float>(q0[0] * c + q0[3] * 0), static_cast<float>(q0[1] * c + q0[3] * sn),
                         q0[2], static_cast<float>(q0[3] * c - q0[1] * sn)};
    CHECK(cfl::pose_changed_v2(e, p0, q1) && !cfl::pose_changed(e, p0, q1));
    CHECK(!cfl::pose_changed_v2(e, p0, q0));
    // The keepalive: due at the 3rd tick (99 ms, half a tick early), not the 2nd; flagged, so the caller repeats the
    // last pose sent bit for bit.
    CHECK(cfl::decide(e, G0 + 99, 1099, p2, q0, ts, cfl::kPolicyV2, &ka) == cfl::Skip::Still);
    CHECK(cfl::decide(e, G0 + 132, 1132, p2, q0, ts, cfl::kPolicyV2, &ka) == cfl::Skip::None && ka && ts == G0 + 122);
    CHECK(cfl::decide(e, G0 + 115, 1116, p2, q0, ts, cfl::kPolicyV2, &ka) == cfl::Skip::Still);  // 83 ms: not yet
    CHECK(cfl::decide(e, G0 + 120, 1117, p2, q0, ts, cfl::kPolicyV2, &ka) == cfl::Skip::None && ka);  // 84 ms
    // v1's keepalive stays 500 ms.
    CHECK(cfl::decide(e, G0 + 132, 1132, p0, q0, ts) == cfl::Skip::Still);
    CHECK(cfl::decide(e, G0 + 540, 1533, p0, q0, ts) == cfl::Skip::None);
    // No real sample for 10 s: v1 stops (Stale), v2 goes on; the clock gate and the anchor's age still hold.
    CHECK(cfl::decide(e, G0 + 10000, 11033, p1, q0, ts) == cfl::Skip::Stale);
    CHECK(cfl::decide(e, G0 + 10000, 11033, p1, q0, ts, cfl::kPolicyV2) == cfl::Skip::None);
    CHECK(cfl::decide(e, G0 + cfl::kAnchorMaxAgeMs + 1, 40000, p1, q0, ts, cfl::kPolicyV2) == cfl::Skip::OldAnchor);
    t.sample(k, G0 + 900, G0 + 200, false, 1200);  // its ts 700 ms off the clock read here
    CHECK(cfl::decide(*t.find(k), G0 + 300, 1300, p1, q0, ts, cfl::kPolicyV2) == cfl::Skip::ClockBad);
}

void test_schedule_and_choice() {
    bool late = true;
    CHECK(cfl::next_deadline(1000, 1005, 33, &late) == 1033 && !late);
    CHECK(cfl::next_deadline(1000, 1032, 33, &late) == 1033 && !late);
    CHECK(cfl::next_deadline(1000, 1033, 33, &late) == 1066 && late);  // a whole period behind: from now
    CHECK(cfl::next_deadline(1000, 1500, 33, &late) == 1533 && late);
    // Drift-free: 30 ticks served 2 ms late each still end at 1000 + 30 * 33.
    uint64_t due = 1000;
    for (int i = 0; i < 30; ++i) due = cfl::next_deadline(due, due + 2, 33);
    CHECK(due == 1000 + 30 * 33);
    std::array<cfl::Near, 6> c{};
    const float d[5] = {30.0f, 5.0f, 12.0f, 5.0f, 34.9f};
    for (size_t i = 0; i < 5; ++i) c[i] = cfl::Near{i, d[i]};
    CHECK(cfl::nearest_first(c, 5, 3) == 3 && c[0].index == 1 && c[1].index == 3 && c[2].index == 2);
    CHECK(cfl::nearest_first(c, 2, 12) == 2);
}

// A slow enemy (0.15 m/s: 5 mm per 33 ms) for 2 s, then standing 1.5 s: v2 every 33 ms vs v1 every 66 ms. The gaps
// between the ts sent: v2 never above 100 ms (the guest's arrivals stay dense); v1 leaves 500 ms holes.
void test_slow_enemy() {
    for (int v2 = 0; v2 < 2; ++v2) {
        cfl::Table<4> t;
        constexpr uint32_t k = 0xB0000077;
        anchored(t, k, 1000);
        const cfl::Policy& pol = v2 ? cfl::kPolicyV2 : cfl::kPolicyV1;
        const uint64_t period = v2 ? cfl::kTickV2Ms : 66;
        uint64_t last_ts = 0, max_gap = 0;
        int keepalives = 0, same_pose = 0;
        for (uint64_t ms = 1033; ms <= 4500; ms += period) {
            const uint64_t g = G0 + (ms - 1000);
            const float x = -10674.0f + 0.15f * static_cast<float>(std::min<uint64_t>(ms, 3000) - 1000) / 1000.0f;
            float pos[3] = {x, 556.5f, -4178.6f};
            const float q[4] = {0, 0.489f, 0, 0.872f};
            const cfl::Enemy e = *t.find(k);
            uint64_t ts = 0;
            bool ka = false;
            if (cfl::decide(e, g, ms, pos, q, ts, pol, &ka) != cfl::Skip::None) continue;
            if (ka && pol.keepalive_same_pose) {
                ++keepalives;
                same_pose += std::memcmp(e.pos, pos, sizeof pos) != 0 ? 1 : 0;  // the read moved < 1e-4 m
                std::memcpy(pos, e.pos, sizeof pos);
            }
            if (last_ts) max_gap = std::max(max_gap, ts - last_ts);
            last_ts = ts;
            t.sent(k, ts, g, ms, pos, q);
        }
        if (v2) {
            CHECK(max_gap <= 100 && keepalives >= 12);
            CHECK(same_pose == 0);  // standing: the read pose is the last one anyway
        } else {
            CHECK(max_gap >= 500);
        }
    }
}

// ================================================================ the guest: the combat class

void test_arrivals() {
    rip::Slot s;
    s.reinit(0xB0101EF7, 0);
    CHECK(s.note_arrival(1000) && s.arrivals() == (std::array<uint32_t, 4>{0, 0, 0, 0}));
    CHECK(!s.note_arrival(1000) && !s.note_arrival(990));  // repeated / older: no arrival
    for (uint64_t ts : {1033, 1066, 1100, 1133, 1250}) CHECK(s.note_arrival(ts));
    CHECK(s.arrivals() == (std::array<uint32_t, 4>{117, 33, 34, 33}));  // newest first
    CHECK(s.arr_last.load() == 1250);
    CHECK(rip::dense_arrivals({117, 33, 34, 33}) && !rip::dense_arrivals({151, 33, 34, 33}) &&
          !rip::dense_arrivals({33, 33, 33, 0}));
    s.note_arrival(1250 + 100000);  // a long absence: capped
    CHECK(s.arrivals()[0] == rip::kMaxArrivalGap);
    s.reinit(0xB0101EF8, 5);
    CHECK(s.arr_last.load() == 0 && s.arrivals()[1] == 0 && s.d2_fix.load() == 0 && s.combat_at.load() == 0);
    // Four threads inserting interleaved ts at once (the hook is not proven single-threaded): the newest ts wins,
    // no gap of 0 (an older ts never overwrites a newer one), every accepted ts was newer than the one it replaced.
    rip::Slot c;
    c.reinit(0xB0101EF9, 0);
    std::atomic<uint64_t> accepted{0};
    std::vector<std::thread> w;
    for (uint64_t id = 0; id < 4; ++id) {
        w.emplace_back([&c, &accepted, id] {
            for (uint64_t t = 1; t <= 20000; ++t) accepted.fetch_add(c.note_arrival(t * 4 + id) ? 1 : 0);
        });
    }
    for (auto& th : w) th.join();
    CHECK(c.arr_last.load() == 20000 * 4 + 3 && accepted.load() >= 20000 && accepted.load() <= 80000);
    for (const uint32_t g : c.arrivals()) CHECK(g >= 1 && g <= 7);
}

void test_class_and_target() {
    const std::array<uint32_t, 4> dense = {33, 50, 34, 33}, sparse = {1014, 42, 84, 84}, none = {0, 0, 0, 0};
    // Who is in the class.
    CHECK(rip::combat_class(KeyClass::Npc, false, false, dense).in);
    CHECK(rip::combat_class(KeyClass::Npc, false, true, sparse).in && !rip::combat_class(KeyClass::Npc, false, true,
                                                                                          sparse).dense);
    CHECK(!rip::combat_class(KeyClass::Npc, false, false, sparse).in);
    CHECK(!rip::combat_class(KeyClass::Npc, true, true, dense).in);        // owned / paired
    CHECK(!rip::combat_class(KeyClass::Character, false, true, dense).in);  // a player's character
    // Its target: dense -> max(last 3 arrivals) + 80 within [120, 400]; engaged only -> the moved gaps capped at 300.
    CHECK(rip::combat_target(dense, {0, 0, 0}, true) == 130);
    CHECK(rip::combat_target({33, 33, 33, 33}, {0, 0, 0}, true) == rip::kCombatFloor);
    CHECK(rip::combat_target({99, 100, 99, 99}, {0, 0, 0}, true) == 180);
    CHECK(rip::combat_target(sparse, {203, 20, 993}, false) == 380);
    CHECK(rip::combat_target(sparse, {40, 20, 50}, false) == 130);
    CHECK(rip::combat_target(sparse, {0, 0, 0}, false) == 380);  // no moved gap yet: its arrivals, capped
    CHECK(rip::combat_target(none, {0, 0, 0}, false) == rip::kMinDelay);
    // The descent: 1 ms per ms in the class, 0.25 outside; the rise 0.5 in both.
    CHECK(rip::approach(rip::to_fix(1143), 380, 16, rip::kCombatDownFix) == rip::to_fix(1127));
    CHECK(rip::approach(rip::to_fix(1143), 380, 16) == rip::to_fix(1139));
    CHECK(rip::approach(rip::to_fix(130), 180, 16, rip::kCombatDownFix) == rip::to_fix(138));
    CHECK(!rip::no_sample_ahead(1000, 120, 0) && !rip::no_sample_ahead(1000, 120, 880) &&
          rip::no_sample_ahead(1000, 120, 879));
}

void test_update_step_modes() {
    // One controller fed the same frames in each mode: off (today), probe, 1.
    rip::Slot off, probe, on;
    for (rip::Slot* s : {&off, &probe, &on}) {
        s->reinit(0xB0101EF7, 0);
        s->record_gap(1014);  // today's target: 1164
        for (uint64_t ts = 1000; ts <= 1132; ts += 33) s->note_arrival(ts);  // dense
    }
    for (uint64_t now = 0; now <= 3000; now += 16) {
        rip::update_step(off, KeyClass::Npc, now, 200);
        rip::CombatStep cp;
        cp.mode = Mode::Probe;
        rip::update_step(probe, KeyClass::Npc, now, 200, rip::kMaxDelay, 0, nullptr, &cp);
        rip::CombatStep c1;
        c1.mode = Mode::On;
        rip::update_step(on, KeyClass::Npc, now, 200, rip::kMaxDelay, 0, nullptr, &c1);
        CHECK(cp.cls.in && cp.cls.dense && c1.cls.in && cp.target == 113 + 7 && c1.target == cp.target);
        CHECK(probe.d_fix.load() == off.d_fix.load());   // probe changes nothing
        CHECK(probe.d2_fix.load() == on.d_fix.load());   // its shadow is what 1 does
        CHECK(cp.today == rip::from_fix(off.d_fix.load()) && cp.would == c1.would && c1.today == c1.would);
        CHECK(probe.combat_at.load() == (now ? now : 1));
    }
    CHECK(rip::from_fix(on.d_fix.load()) == 120 && rip::from_fix(off.d_fix.load()) == 1164);
    // Excluded (owned) or paired: as today, never in the class.
    rip::Slot pet;
    pet.reinit(0xB0100425, 0);
    for (uint64_t ts = 1000; ts <= 1132; ts += 33) pet.note_arrival(ts);
    rip::CombatStep c;
    c.mode = Mode::On;
    c.excluded = true;
    rip::update_step(pet, KeyClass::Npc, 16, 200, rip::kMaxDelay, 0, nullptr, &c);
    CHECK(!c.cls.in && pet.combat_at.load() == 0);
    c.excluded = false;
    rip::update_step(pet, KeyClass::Npc, 32, 200, rip::kMaxDelay, /*pair_delay=*/200, nullptr, &c);
    CHECK(!c.cls.in);
    // A player's character: marked, no delay of its own, no class.
    rip::Slot ch;
    ch.reinit(0xA0100363, 0);
    c = rip::CombatStep{};
    c.mode = Mode::On;
    c.engaged = true;
    rip::update_step(ch, KeyClass::Character, 16, 200, rip::kMaxDelay, 0, nullptr, &c);
    CHECK(ch.d_fix.load() == 0 && !c.cls.in && ch.updated.load() == 1);
}

void test_engage_table() {
    rip::EngageTable t;
    CHECK(!t.engaged(0xB0101EF7, 1000));
    t.note(0xB0101EF7, 1000);
    CHECK(t.engaged(0xB0101EF7, 1000) && t.engaged(0xB0101EF7, 1000 + rip::kEngageMs - 1));
    CHECK(!t.engaged(0xB0101EF7, 1000 + rip::kEngageMs) && !t.engaged(0xB0000001, 1500));
    t.note(0xB0101EF7, 5000);  // renewed in place
    CHECK(t.engaged(0xB0101EF7, 12000) && t.live(12000) == 1);
    // Full: an expired entry is reused first, then the oldest (counted).
    rip::EngageTable f;
    for (uint32_t i = 0; i < rip::kEngageSlots; ++i) f.note(0xB0000100 + i, 100 + i);
    CHECK(f.live(200) == rip::kEngageSlots && f.evicted.load() == 0);
    f.note(0xB0000999, 300);
    CHECK(f.evicted.load() == 1 && !f.engaged(0xB0000100, 300) && f.engaged(0xB0000999, 300) &&
          f.engaged(0xB0000101, 300));
    f.note(0xB0000998, 100 + 1 + rip::kEngageMs);  // …101 expired by now: reused without an eviction
    CHECK(f.evicted.load() == 1 && f.engaged(0xB0000998, 101 + rip::kEngageMs));
    // One writer, three readers: a reader never sees a key that was never noted.
    rip::EngageTable c;
    std::atomic<bool> stop{false};
    std::atomic<int> bad{0};
    std::vector<std::thread> readers;
    for (int r = 0; r < 3; ++r) {
        readers.emplace_back([&] {
            while (!stop.load()) {
                for (uint32_t k = 0; k < 64; ++k) {
                    if (c.engaged(0xC0000000 + k, 50000)) bad.fetch_add(1);
                    (void)c.engaged(0xB0000000 + k, 50000);
                }
            }
        });
    }
    for (uint64_t i = 0; i < 20000; ++i) c.note(0xB0000000 + static_cast<uint32_t>(i % 64), 40000 + i / 4);
    stop.store(true);
    for (auto& th : readers) th.join();
    CHECK(bad.load() == 0 && c.live(46000) == rip::kEngageSlots);
}

// ================================================================ real trace: S10, enemy 0xB0101EF7

// The session 10 guest capture (cdmp_packets_20261010-173057.bin), every 2697 / 2991 / 2922 of 0xB0101EF7 from
// t 458.0 s (enemy_trace.py's clock): {t ms after 458.0 (arrival), kind, ts, pos, a, b}. M = MoveActorAck 2697 (a = 1:
// combat_follow's synthetic), S = StartActionAck 2991 (a = its target +0x16D), H = AttackTargetAck 2922 (a = target,
// b = attacker). The guest's character is 0xA0100363; the host's 0xA0100001.
struct Ev {
    uint32_t t;
    char kind;
    uint64_t ts;
    float x, y, z;
    uint32_t a, b;
};
constexpr Ev kS10[] = {
    {88, 'S', 685264, -10698.25f, 556.93f, -4202.25f, 0xA0100001u, 0},
    {618, 'S', 685777, -10697.88f, 556.95f, -4201.46f, 0xA0100001u, 0},
    {650, 'S', 685821, -10697.78f, 556.97f, -4201.29f, 0xA0100001u, 0},
    {1488, 'M', 686622, -10695.07f, 557.25f, -4197.79f, 0, 0},
    {1504, 'M', 686641, -10695.00f, 557.26f, -4197.71f, 0, 0},
    {1954, 'S', 687120, -10692.91f, 557.27f, -4195.82f, 0xA0100001u, 0},
    {2388, 'S', 687544, -10690.84f, 557.25f, -4194.49f, 0xA0100001u, 0},
    {3288, 'S', 688459, -10686.41f, 556.75f, -4190.20f, 0xA0100001u, 0},
    {3949, 'S', 689107, -10683.64f, 556.58f, -4187.19f, 0xA0100001u, 0},
    {4576, 'S', 689743, -10681.17f, 556.98f, -4184.20f, 0xA0100001u, 0},
    {5517, 'S', 690690, -10679.51f, 557.01f, -4182.09f, 0xA0100001u, 0},
    {6148, 'H', 0, 0, 0, 0, 0xA0100001u, 0xB0101EF7u},
    {6567, 'M', 691707, -10678.45f, 557.00f, -4180.30f, 1, 0},
    {6777, 'S', 691938, -10678.44f, 557.02f, -4180.10f, 0xA0100001u, 0},
    {7148, 'S', 692307, -10678.36f, 557.02f, -4179.90f, 0xA0100001u, 0},
    {7164, 'S', 692327, -10678.35f, 557.02f, -4179.89f, 0xA0100001u, 0},
    {8134, 'S', 693296, -10677.12f, 556.78f, -4180.65f, 0xA0100001u, 0},
    {8166, 'S', 693316, -10677.07f, 556.77f, -4180.68f, 0xA0100001u, 0},
    {9163, 'M', 694334, -10674.69f, 556.48f, -4182.35f, 0, 0},
    {9502, 'S', 694657, -10674.26f, 556.38f, -4183.07f, 0xA0100001u, 0},
    {10437, 'S', 695590, -10671.06f, 556.00f, -4183.26f, 0xA0100001u, 0},
    {11016, 'S', 696166, -10669.65f, 555.90f, -4182.23f, 0xA0100001u, 0},
    {12078, 'M', 697218, -10667.70f, 555.81f, -4179.68f, 0, 0},
    {12496, 'S', 697630, -10667.17f, 555.70f, -4178.52f, 0xA0100001u, 0},
    {12883, 'S', 698037, -10666.94f, 555.87f, -4177.65f, 0xA0100001u, 0},
    {13881, 'M', 699048, -10666.81f, 555.85f, -4174.18f, 0, 0},
    {13881, 'S', 699048, -10666.81f, 555.85f, -4174.18f, 0xA0100001u, 0},
    {14898, 'M', 700065, -10670.13f, 556.08f, -4174.80f, 0, 0},
    {15106, 'S', 700268, -10670.61f, 556.10f, -4174.83f, 0xA0100001u, 0},
    {15122, 'S', 700288, -10671.24f, 556.20f, -4174.79f, 0xA0100001u, 0},
    {16124, 'S', 701281, -10674.64f, 556.66f, -4177.81f, 0xA0100001u, 0},
    {16945, 'H', 0, 0, 0, 0, 0xA0100363u, 0xB0101EF7u},
    {16945, 'H', 0, 0, 0, 0, 0xA0100001u, 0xB0101EF7u},
    {16945, 'H', 0, 0, 0, 0, 0xB01020AAu, 0xB0101EF7u},
    {17124, 'M', 702295, -10674.29f, 556.51f, -4178.59f, 1, 0},
    {17189, 'M', 702337, -10674.25f, 556.50f, -4178.67f, 1, 0},
    {17253, 'M', 702421, -10674.21f, 556.48f, -4178.76f, 1, 0},
    {17351, 'M', 702505, -10674.16f, 556.47f, -4178.88f, 1, 0},
    {17351, 'M', 702484, -10674.18f, 556.47f, -4178.85f, 1, 0},
    {17367, 'S', 702527, -10674.16f, 556.47f, -4178.90f, 0xA0100001u, 0},
    {17383, 'H', 0, 0, 0, 0, 0xB0101EF7u, 0xA0100363u},
    {17415, 'S', 702569, -10674.22f, 556.49f, -4178.89f, 0xA0100001u, 0},
    {17495, 'M', 702635, -10674.75f, 556.58f, -4178.48f, 1, 0},
    {17576, 'M', 702720, -10675.44f, 556.64f, -4177.99f, 1, 0},
    {17658, 'M', 702804, -10675.87f, 556.85f, -4177.70f, 1, 0},
    {17738, 'M', 702866, -10676.18f, 556.94f, -4177.48f, 1, 0},
    {17818, 'M', 702950, -10676.38f, 556.95f, -4177.35f, 1, 0},
    {17851, 'S', 703013, -10676.41f, 556.95f, -4177.32f, 0xA0100001u, 0},
    {17899, 'M', 703055, -10676.53f, 556.96f, -4177.23f, 1, 0},
    {17995, 'M', 703138, -10676.74f, 556.96f, -4177.07f, 1, 0},
    {18076, 'M', 703221, -10676.84f, 556.97f, -4176.99f, 1, 0},
    {18220, 'M', 703364, -10677.12f, 557.09f, -4176.78f, 1, 0},
    {18461, 'M', 703610, -10677.30f, 557.20f, -4176.64f, 1, 0},
    {19010, 'M', 704168, -10677.30f, 557.20f, -4176.64f, 1, 0},
    {19571, 'M', 704703, -10677.30f, 557.20f, -4176.64f, 1, 0},
    {19844, 'S', 705004, -10677.30f, 557.20f, -4176.64f, 0xA0100001u, 0},
    {19893, 'M', 705025, -10677.38f, 557.24f, -4176.58f, 1, 0},
    {20055, 'M', 705184, -10677.67f, 557.30f, -4176.36f, 1, 0},
    {20264, 'M', 705421, -10677.89f, 557.30f, -4176.19f, 1, 0},
    {20424, 'M', 705579, -10678.45f, 557.37f, -4175.76f, 1, 0},
    {20633, 'S', 705794, -10678.69f, 557.40f, -4175.52f, 0xA0100001u, 0},
    {20665, 'M', 705813, -10678.94f, 557.46f, -4175.31f, 1, 0},
    {20826, 'S', 705990, -10679.13f, 557.54f, -4175.17f, 0xA0100001u, 0},
    {20859, 'S', 706011, -10679.20f, 557.58f, -4175.12f, 0xA0100001u, 0},
    {20891, 'M', 706050, -10679.20f, 557.58f, -4175.12f, 1, 0},
    {21052, 'M', 706207, -10679.15f, 557.56f, -4175.09f, 1, 0},
    {21294, 'M', 706442, -10679.02f, 557.51f, -4175.00f, 1, 0},
    {21456, 'M', 706597, -10678.83f, 557.46f, -4174.81f, 1, 0},
    {21601, 'M', 706752, -10678.63f, 557.43f, -4174.56f, 1, 0},
    {21616, 'S', 706771, -10678.69f, 557.44f, -4174.64f, 0xA0100001u, 0},
    {21809, 'S', 706968, -10678.34f, 557.47f, -4174.20f, 0xA0100001u, 0},
    {21857, 'M', 706988, -10678.34f, 557.47f, -4174.20f, 1, 0},
    {22003, 'M', 707144, -10677.96f, 557.44f, -4173.83f, 1, 0},
    {22228, 'M', 707376, -10677.44f, 557.25f, -4174.01f, 1, 0},
    {22389, 'M', 707531, -10676.99f, 557.25f, -4173.87f, 1, 0},
    {22631, 'M', 707767, -10676.44f, 557.19f, -4173.59f, 1, 0},
    {22776, 'M', 707926, -10675.86f, 557.11f, -4173.29f, 1, 0},
    {22953, 'M', 708086, -10675.42f, 557.14f, -4173.05f, 1, 0},
    {22953, 'M', 708106, -10675.42f, 557.14f, -4173.05f, 0, 0},
    {23017, 'M', 708165, -10675.32f, 557.13f, -4173.00f, 1, 0},
    {23178, 'M', 708321, -10674.78f, 557.03f, -4172.71f, 1, 0},
    {23178, 'S', 708341, -10674.78f, 557.03f, -4172.71f, 0xA0100001u, 0},
    {23419, 'M', 708556, -10674.25f, 557.11f, -4172.41f, 1, 0},
    {23580, 'M', 708714, -10673.73f, 557.12f, -4172.11f, 1, 0},
    {23805, 'M', 708949, -10673.28f, 557.07f, -4171.77f, 1, 0},
    {23966, 'M', 709107, -10672.80f, 556.94f, -4171.60f, 1, 0},
    {23966, 'S', 709126, -10672.89f, 556.94f, -4171.58f, 0xA0100001u, 0},
    {24206, 'M', 709344, -10672.29f, 556.88f, -4171.89f, 1, 0},
    {24271, 'S', 709441, -10672.29f, 556.88f, -4171.89f, 0xA0100001u, 0},
    {24351, 'M', 709500, -10672.29f, 556.87f, -4172.03f, 1, 0},
    {24560, 'S', 709716, -10672.28f, 556.82f, -4172.26f, 0xA0100001u, 0},
    {24576, 'M', 709735, -10672.28f, 556.82f, -4172.26f, 1, 0},
    {24736, 'M', 709893, -10672.28f, 556.80f, -4172.37f, 1, 0},
    {24945, 'S', 710112, -10672.28f, 556.80f, -4172.37f, 0xA0100001u, 0},
    {25203, 'M', 710369, -10672.28f, 556.80f, -4172.39f, 1, 0},
    {25380, 'M', 710529, -10672.28f, 556.80f, -4172.40f, 1, 0},
    {26120, 'M', 711297, -10672.28f, 556.79f, -4172.51f, 1, 0},
    {27294, 'M', 712454, -10672.28f, 556.80f, -4172.62f, 1, 0},
    {28065, 'S', 713233, -10672.29f, 556.80f, -4172.39f, 0xA0100001u, 0},
    {28821, 'H', 0, 0, 0, 0, 0xB0101EF7u, 0xA0100363u},
    {28837, 'S', 713989, -10671.99f, 556.81f, -4171.88f, 0xA0100001u, 0},
    {28853, 'M', 714009, -10671.96f, 556.81f, -4171.82f, 1, 0},
    {29014, 'H', 0, 0, 0, 0, 0xB0101EF7u, 0xA0100363u},
    {29062, 'S', 714210, -10671.82f, 556.77f, -4171.56f, 0xA0100001u, 0},
    {29078, 'H', 0, 0, 0, 0, 0xB0101EF7u, 0xA0100363u},
    {29078, 'M', 714250, -10671.82f, 556.77f, -4171.56f, 1, 0},
    {29239, 'M', 714409, -10671.82f, 556.77f, -4171.56f, 1, 0},
    {29544, 'M', 714696, -10671.82f, 556.77f, -4171.56f, 0, 0},
    {29624, 'S', 714791, -10671.82f, 556.77f, -4171.56f, 0xA0100001u, 0},
    {29640, 'S', 714811, -10671.82f, 556.77f, -4171.56f, 0xA0100001u, 0},
    {29802, 'M', 714946, -10671.82f, 556.77f, -4171.56f, 1, 0},
    {29882, 'M', 715043, -10671.84f, 556.78f, -4171.64f, 1, 0},
    {30043, 'M', 715197, -10671.87f, 556.77f, -4171.85f, 1, 0},
    {30203, 'S', 715369, -10672.00f, 556.67f, -4172.72f, 0xA0100001u, 0},
    {30268, 'M', 715427, -10672.00f, 556.67f, -4172.72f, 1, 0},
    {30316, 'M', 715466, -10672.00f, 556.67f, -4172.72f, 0, 0},
    {30413, 'S', 715562, -10672.16f, 556.58f, -4173.70f, 0xA0100001u, 0},
    {30429, 'M', 715581, -10672.16f, 556.58f, -4173.70f, 1, 0},
    {30461, 'H', 0, 0, 0, 0, 0xB0101EF7u, 0xA0100363u},
    {30477, 'S', 715638, -10671.62f, 556.42f, -4173.84f, 0xA0100001u, 0},
    {30509, 'M', 715658, -10671.29f, 556.29f, -4174.03f, 1, 0},
    {30589, 'M', 715736, -10670.71f, 556.16f, -4174.48f, 1, 0},
    {30670, 'M', 715813, -10670.24f, 556.09f, -4174.83f, 1, 0},
    {30734, 'M', 715909, -10669.87f, 556.05f, -4175.11f, 1, 0},
    {30830, 'M', 715986, -10669.63f, 556.08f, -4175.30f, 1, 0},
    {30847, 'S', 716024, -10669.55f, 556.09f, -4175.38f, 0xA0100001u, 0},
    {30895, 'M', 716063, -10669.55f, 556.09f, -4175.38f, 1, 0},
    {30975, 'M', 716141, -10669.08f, 556.12f, -4175.50f, 1, 0},
    {31200, 'M', 716371, -10668.55f, 555.87f, -4175.63f, 1, 0},
    {31361, 'M', 716524, -10668.21f, 555.82f, -4175.72f, 1, 0},
    {31618, 'M', 716756, -10668.15f, 555.82f, -4175.74f, 1, 0},
    {31972, 'M', 717106, -10668.15f, 555.82f, -4175.74f, 1, 0},
    {32117, 'H', 0, 0, 0, 0, 0xB0101EF7u, 0xA0100363u},
    {32117, 'S', 717297, -10668.13f, 555.82f, -4175.75f, 0xA0100363u, 0},
    {32149, 'M', 717316, -10668.13f, 555.82f, -4175.75f, 1, 0},
    {32696, 'M', 717854, -10668.12f, 555.82f, -4175.75f, 1, 0},
    {32696, 'S', 717874, -10668.12f, 555.82f, -4175.75f, 0xA0100363u, 0},
    {32953, 'M', 718084, -10667.74f, 555.86f, -4175.85f, 1, 0},
    {32953, 'H', 0, 0, 0, 0, 0xB0101EF7u, 0xA0100363u},
    {33097, 'M', 718259, -10667.57f, 555.88f, -4175.90f, 1, 0},
    {33322, 'M', 718487, -10667.09f, 555.90f, -4176.02f, 1, 0},
    {33483, 'M', 718641, -10666.34f, 555.62f, -4176.21f, 1, 0},
    {33483, 'S', 718641, -10666.42f, 555.64f, -4176.19f, 0xA0100363u, 0},
    {33708, 'M', 718870, -10665.63f, 555.51f, -4176.35f, 1, 0},
    {33870, 'S', 719024, -10665.63f, 555.51f, -4176.35f, 0xA0100363u, 0},
    {33870, 'M', 719024, -10665.63f, 555.51f, -4176.35f, 1, 0},
    {33870, 'S', 719043, -10665.63f, 555.51f, -4176.35f, 0xA0100363u, 0}
};
constexpr uint32_t kS10Enemy = 0xB0101EF7, kS10Guest = 0xA0100363;

// The guest replayed: the set-position hook (arrivals, the moved gaps) and the game's insert rule (newer only, emptied
// past 2500 ms, not inserted when moved <= 1e-4 m), a frame every 16 ms (update position), the engagement table fed
// by the 2922 / 2991 through combat_follow's own rule. Returns D per frame (the delay in force) and the frames' times.
struct Replay {
    std::vector<uint64_t> t;   // frame times (trace ms)
    std::vector<uint32_t> d;   // the delay in force
    std::vector<uint32_t> d2;  // probe: the shadow's
    std::vector<uint64_t> engage_starts;  // trace ms of the packets that started an engagement
    uint64_t class_frames = 0, starved = 0;
};

Replay replay(Mode mode) {
    Replay out;
    rip::Slot slot;
    slot.reinit(kS10Enemy, 0);
    rip::EngageTable eng;
    // The game clock: the fastest sample arrives 30 ms after its ts.
    int64_t off = INT64_MIN;
    for (const Ev& e : kS10) {
        if (e.kind != 'H') off = std::max(off, static_cast<int64_t>(e.ts) - static_cast<int64_t>(e.t));
    }
    off += 30;
    auto clock = [&](uint64_t t) { return static_cast<uint64_t>(static_cast<int64_t>(t) + off); };
    bool any = false;
    uint64_t last_ts = 0;
    float last_pos[3] = {};
    size_t i = 0;
    const uint64_t end = kS10[std::size(kS10) - 1].t + 2000;
    for (uint64_t ft = 0; ft <= end; ft += 16) {
        for (; i < std::size(kS10) && kS10[i].t <= ft; ++i) {
            const Ev& e = kS10[i];
            const uint64_t mod_now = e.t + 1;  // the engagement table's clock
            if (e.kind == 'H' || e.kind == 'S') {
                const auto pl = e.kind == 'H' ? attack(e.a, e.b) : start_action(kS10Enemy, e.a, e.ts);
                const uint16_t op = e.kind == 'H' ? cfl::kOpAttackTargetAck : cfl::kOpStartActionAck;
                if (const uint32_t k = cfl::read_out(op, pl, kS10Guest).engage) {
                    if (k == kS10Enemy && !eng.engaged(k, mod_now)) out.engage_starts.push_back(e.t);
                    eng.note(k, mod_now);
                }
            }
            if (e.kind == 'H') continue;
            const float pos[3] = {e.x, e.y, e.z};
            const uint64_t now = clock(e.t);
            // The hook, before the game's tests.
            if (mode != Mode::Off) slot.note_arrival(e.ts);
            rip::SetIn in;
            in.count = any ? 1 : 0;
            in.ts_last = last_ts;
            in.ts_new = e.ts;
            in.moved = !any || rip::moved(pos, last_pos);
            in.threshold = rip::kPatchedThreshold;
            const rip::SetOut o = rip::observe_set(in);
            if (o.record) slot.record_gap(static_cast<uint32_t>(o.gap));
            // The game.
            if (any && e.ts <= last_ts) continue;
            if (any && o.reset) last_ts = now - rip::from_fix(slot.d_fix.load());  // emptied: the shown pose at now - D
            if (any && !rip::moved(pos, last_pos)) continue;
            any = true;
            last_ts = e.ts;
            std::memcpy(last_pos, pos, sizeof pos);
        }
        rip::CombatStep cs;
        cs.mode = mode;
        cs.engaged = eng.engaged(kS10Enemy, ft + 1);
        rip::update_step(slot, KeyClass::Npc, clock(ft), 200, rip::kMaxDelay, 0, nullptr,
                         mode != Mode::Off ? &cs : nullptr);
        out.t.push_back(ft);
        out.d.push_back(rip::from_fix(slot.d_fix.load()));
        out.d2.push_back(mode == Mode::Probe ? cs.would : 0);
        if (cs.cls.in) {
            ++out.class_frames;
            out.starved += rip::no_sample_ahead(clock(ft), cs.would, slot.arr_last.load()) ? 1 : 0;
        }
    }
    return out;
}

uint32_t d_at(const Replay& r, uint64_t t) {
    for (size_t i = 0; i < r.t.size(); ++i) {
        if (r.t[i] >= t) return r.d[i];
    }
    return r.d.back();
}

void test_s10_trace() {
    static_assert(std::size(kS10) == 147);
    const Replay today = replay(Mode::Off), probe = replay(Mode::Probe), on = replay(Mode::On);
    // The engagements as the guest sees them (its 2922s and the 2991s aimed at it): the first at 474.945 (t 16945).
    CHECK(on.engage_starts.size() >= 2 && on.engage_starts[0] == 16945);
    // Today's controller reproduces enemy_trace.py's numbers: D ~1.1 s at the first engagement, still ~1 s later.
    CHECK(d_at(today, 16945) > 1000 && d_at(today, 16945 + 1000) > 900);
    // The class: D <= 400 within 1 s of every engagement (report §7.1).
    for (const uint64_t t0 : on.engage_starts) CHECK(d_at(on, t0 + 1000) <= 400);
    // And at every blow of the guest on it (the 2922 with the guest as attacker).
    for (const Ev& e : kS10) {
        if (e.kind == 'H' && e.b == kS10Guest) CHECK(d_at(on, e.t) <= 400 || d_at(on, e.t) < d_at(today, e.t));
    }
    // probe: the delay in force is today's frame by frame, and its shadow is exactly what 1 applies.
    bool same = probe.d.size() == today.d.size() && probe.d2.size() == on.d.size();
    for (size_t i = 0; same && i < probe.d.size(); ++i) same = probe.d[i] == today.d[i] && probe.d2[i] == on.d[i];
    CHECK(same);
    CHECK(on.class_frames > 0 && on.class_frames == probe.class_frames);
}

// A synthetic 33 ms stream (combat_follow_v2): the enemy had sparse samples (D ~1.15 s), then fights the guest and
// streams at 33 ms with 40 ms of latency and the guest's 16 ms delivery steps; then stands with 99 ms keepalives of
// the same pose. D <= 400 within 1 s of the engagement, <= 160 while it moves, <= 180 while it stands; with D at
// its target no frame is left without a sample ahead.
void test_synthetic_stream() {
    rip::Slot slot;
    slot.reinit(0xB0101EF7, 0);
    rip::EngageTable eng;
    struct S {
        uint64_t ts, arrive;
        float x;
    };
    std::vector<S> samples;
    for (uint64_t ts = 100000; ts < 110000; ts += 1000) samples.push_back({ts, ts + 60, (ts - 100000) / 1000.0f});
    constexpr uint64_t kStream = 110000, kStand = 113000, kEnd = 115000;
    uint64_t stand_tick = 0;
    size_t keepalives = 0;
    for (uint64_t k = 0, ts = kStream; ts < kEnd; ++k, ts += 33) {
        const uint64_t arrive = (ts + 40 + 15) / 16 * 16;  // the next 16 ms turn of the guest's NetworkThread
        const float x = 10.0f + 3.0f * static_cast<float>(std::min(ts, kStand) - kStream) / 1000.0f;
        if (ts >= kStand) {  // standing: every 3rd tick (99 ms) a keepalive of the same pose
            if (!stand_tick) stand_tick = k;
            if ((k - stand_tick) % 3) continue;
            ++keepalives;
        }
        samples.push_back({ts, arrive, x});
    }
    CHECK(keepalives >= 19);
    size_t i = 0;
    bool any = false;
    uint64_t last_ts = 0;
    float last_x = 0;
    uint32_t d_engage_1s = 0, max_moving = 0, max_standing = 0;
    uint64_t starved_converged = 0;
    for (uint64_t now = 100000; now <= kEnd + 500; now += 16) {
        for (; i < samples.size() && samples[i].arrive <= now; ++i) {
            const S& s = samples[i];
            slot.note_arrival(s.ts);
            const bool moved = !any || std::fabs(s.x - last_x) > 1e-4f;
            rip::SetIn in;
            in.count = any ? 1 : 0;
            in.ts_last = last_ts;
            in.ts_new = s.ts;
            in.moved = moved;
            in.threshold = rip::kPatchedThreshold;
            if (const rip::SetOut o = rip::observe_set(in); o.record) slot.record_gap(static_cast<uint32_t>(o.gap));
            if ((!any || s.ts > last_ts) && moved) {
                any = true;
                last_ts = s.ts;
                last_x = s.x;
            }
        }
        if (now == kStream + 48) eng.note(0xB0101EF7, now);  // the first blow, as the stream starts
        rip::CombatStep cs;
        cs.mode = Mode::On;
        cs.engaged = eng.engaged(0xB0101EF7, now);
        rip::update_step(slot, KeyClass::Npc, now, 200, rip::kMaxDelay, 0, nullptr, &cs);
        const uint32_t d = rip::from_fix(slot.d_fix.load());
        if (now == 109984) CHECK(d > 1100);  // sparse before: today's controller
        if (now >= kStream + 48 + 1000 && !d_engage_1s) d_engage_1s = d;
        if (now >= kStream + 1600 && now < kStand + 100) {
            max_moving = std::max(max_moving, d);
            starved_converged += rip::no_sample_ahead(now, d, slot.arr_last.load()) ? 1 : 0;
        }
        if (now >= kStand + 400 && now <= kEnd) max_standing = std::max(max_standing, d);
    }
    CHECK(d_engage_1s > 0 && d_engage_1s <= 400);
    CHECK(max_moving > 0 && max_moving <= 160);
    CHECK(max_standing > 0 && max_standing <= 180);
    CHECK(starved_converged == 0);
}

}  // namespace

int main() {
    test_fight_read();
    test_fight_table();
    test_decide_v2();
    test_schedule_and_choice();
    test_slow_enemy();
    test_arrivals();
    test_class_and_target();
    test_update_step_modes();
    test_engage_table();
    test_s10_trace();
    test_synthetic_stream();
    if (g_failures) {
        std::printf("optd_combat: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("optd_combat: all passed\n");
    return 0;
}
