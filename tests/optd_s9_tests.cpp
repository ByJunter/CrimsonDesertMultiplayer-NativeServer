// P8 session 9 (docs/research/pendientes/p8-s9-combate-montura.md; p8-impl-s9-movimiento.md), offline logic only:
//  - ride_filter_logic.h (D1): the StartActionAck layout on real packets of the session 9 host capture, the
//    classification (mount / dismount / none), the in-place rewrite (byte-identical to the native on-foot and
//    riding Acks of the same capture, nothing else touched), probe / off / local / unknown keys, the window
//    watch and the pair table;
//  - reannounce_logic.h (P1): the control packet, the failure value, the attempts table (gap, cap, full) and the
//    host's backoff; child_settle_logic.h's re-announce wait;
//  - combat_follow_logic.h (C1): who is engaged (2922 / 2991 / 2697), the table (expiry, order, cap), the
//    decisions (anchor, parent, clock, still, keepalive), the ts on the enemy's clock, and the synthetic 2697
//    byte for byte against a real enemy Ack of the session 9 guest capture;
//  - horse_follow_logic.h (M1): the horse's synthetic 2556 from its rider's Req.
// Not the hooks, the threads, the game's behaviour.

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "mod/game/seq_ring.h"
#include "mod/session2/child_settle_logic.h"
#include "mod/session2/combat_follow_logic.h"
#include "mod/session2/horse_follow_logic.h"
#include "mod/session2/reannounce_logic.h"
#include "mod/session2/ride_filter_logic.h"

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

std::vector<uint8_t> unhex(const char* s) {
    std::vector<uint8_t> v;
    const size_t n = std::strlen(s);
    for (size_t i = 0; i + 1 < n; i += 2) {
        auto nib = [](char c) { return static_cast<uint8_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10); };
        v.push_back(static_cast<uint8_t>(nib(s[i]) << 4 | nib(s[i + 1])));
    }
    return v;
}

// H2 (session 9 host capture, cdmp_packets_20261010-163017.bin): user 2's character 0xA0100423 and its horse
// 0xB0100425, StartActionAck 2991 as the host's client received them (header + 725 bytes).
// t 2535.247 A0 26107D3C A1 00000000 (on foot, the horse as target)
const char* kFootAck =
    "af0bffd502230410a0000000003c7d10260000ffffffffffff00ffffffffffff00ffffffffffff0000898808bd0000000000000000d7a3703f575700"
    "00000003000000000000000000000000000000000000000000000000ffffffffffff00ffffffffffff00ffffffffffff0002000080bf000000000000"
    "00000000803f575700000000030000000000000000000000000000006ccc26c6151f1a4490c270c500000000f8f359bf00000000fc48063f00000000"
    "0000000000000000000000000000000000000000000000000000803f0000803f0000803f0000803f6ccc26c6151f1a4490c270c500000000f8f359bf"
    "00000000fc48063f0000803f0000803f0000803f82696c3f977f443db90b053f0000000073ff3c4000000000ffffffffffffffff0000000000000000"
    "000000000000000000000000000000000000803f0000803f0000803f0000803f000000000000000000000000000000000000000000000000000a190a"
    "000000000000e3b7d15e250410b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0a190a00000000000000";
// t 2535.818 A0 91605616 A1 26107D3C (mount: RideOn on the host's client, Enter 0, frozen 2.3 s)
const char* kMountAck =
    "af0bffd502230410a000000000165660910000ffffffffffff00ffffffffffff00ffffffffffff0000abaaaa3e00000000000000000000803f575700"
    "000000030000010000000000000000000000000000003c7d10260000ffffffffffff00ffffffffffff00ffffffffffff0001898808bd000000000000"
    "00000000803f575700000000030000000000000000000000000000006ac826c6a4201a449aba70c5000000001b7159bf000000007c1c073f00000000"
    "0000000000000000000000000000000000000000000000000000803f0000803f0000803f0000803f6ac826c6a4201a449aba70c5000000001b7159bf"
    "000000007c1c073f0000803f0000803f0000803ff9aee23f00000000fe6d5f3f000000000000000000000000ffffffffffffffff0000000000000000"
    "000000000000000000000000000000000000803f0000803f0000803f0000803f000000000000000000000000000000000000000000000000003e1b0a"
    "00000000000089bb1c0a250410b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3e1b0a00000000000000";
// t 2540.633 A0 F79C7424 A1 00000000 (riding, the horse as parent; method 0, Enter 1)
const char* kRidingAck =
    "af0bffd502230410a00000000024749cf70000ffffffffffff00ffffffffffff00ffffffffffff0000898808bd0000000000000000d7a3703f575700"
    "00000003000000000000000000000000000000000000000000000000ffffffffffff00ffffffffffff00ffffffffffff0002000080bf000000000000"
    "00000000803f5757000000000300000000000000000000000000000078ce26c6c9831a449a9a70c5024e713b20eb623f1645d6bbdafbec3e00000000"
    "0000000000000000000000000000000000000000000000000000803f0000803f0000803f0000803f44a26cbc00f4c23f09f8173f2dc6f53b00a65dba"
    "60487f3922fe7f3f0000803f0000803f0000803f000000000000000000000000000000000000000000000000250410b00000000073d026c64f221a44"
    "5a9570c5000000006306633f000000004c9bec3e0000803f0000803f0000803fb774b4bfb774b43d1416853f00000000a60937be00000000001b2e0a"
    "000000000000b935469f0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "1b2e0a00000000000000";
// t 2546.041 A0 C9356CEB A1 7B6AFF57 (dismount: RideOff on the host's client, Enter 0, stuck 83.6 s)
const char* kDismountAck =
    "af0bffd502230410a000000000eb6c35c90000ffffffffffff00ffffffffffff00ffffffffffff0000cecccc3d00000000000000000000803f575700"
    "0000000300000000000000000000000000000000000057ff6a7b0000ffffffffffff00ffffffffffff00ffffffffffff0001898808bd000000000000"
    "00000000803f57570000000003000000000000000000000000000000a3d626c639851a441cdd70c5e7330e3cdbbd52bf00b7c4b8e952113f00000000"
    "0000000000000000000000000000000000000000000000000000803f0000803f0000803f0000803f447790bd0048c83f7c05133fc0149e3bfe116a3b"
    "f671ecbb1dfd7f3f0000803f0000803f0000803f000000000000000000000000000000000000000000000000250410b00000000099d426c615211a44"
    "bbd870c5000000006e4453bf000000005f93103f0000803f0000803f0000803fb0d86f3f8e48cdbecc3db63e00000000e2c5aebe000000000032430a"
    "000000000000d2da277e0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "32430a00000000000000";

// G1 (session 9 guest capture, cdmp_packets_20261010-151331.bin) t 1108.066: the host's enemy 0xB0101A7F, MoveActorAck
// 2697 as user 2's client received it (header + 235 bytes).
const char* kEnemyMoveAck =
    "890affeb007f1a10b0981027c627340b441e5583c500000000ab4cfa3e00000000ea525f3f00000000000000000000000000000000000000000000"
    "0000000000000000803f0000803f0000803f0000803f981027c627340b441e5583c500000000ab4cfa3e00000000ea525f3f0000803f0000803f00"
    "00803f909c133e93c096bcf0c175be000000005fd3f1bd00000000ffffffffffffffff0000000000000000000000000000000000000000000000000000"
    "803f0000803f0000803f0000803f00000000000000000000000000000000000000000000000000e0860c00000000000000000000000000000059a68f"
    "3e";

constexpr uint32_t kGuestChar = 0xA0100423, kGuestHorse = 0xB0100425, kHostChar = 0xA0100001;
constexpr size_t kH = rfl::kHeader;

bool same_except(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, size_t from, size_t to) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (i >= from && i < to) continue;
        if (a[i] != b[i]) return false;
    }
    return true;
}
bool block_eq(const std::vector<uint8_t>& a, size_t at_a, const std::vector<uint8_t>& b, size_t at_b) {
    return std::memcmp(a.data() + at_a, b.data() + at_b, rfl::kBlock) == 0;
}

// ---------------------------------------------------------------- D1 ride_filter

void test_ride_read_classify() {
    const auto foot = unhex(kFootAck), mount = unhex(kMountAck), riding = unhex(kRidingAck), dis = unhex(kDismountAck);
    CHECK(foot.size() == kH + rfl::kAckSize && mount.size() == foot.size() && riding.size() == foot.size() &&
          dis.size() == foot.size());
    const rfl::Ack f = rfl::read_ack(foot.data() + kH, rfl::kAckSize);
    CHECK(f.ok && f.key == kGuestChar && f.a0 == rfl::kFootHash && f.a1 == 0 && f.parent == ~0ull &&
          f.target == kGuestHorse);
    const rfl::Ack m = rfl::read_ack(mount.data() + kH, rfl::kAckSize);
    CHECK(m.ok && m.a0 == 0x91605616u && m.a1 == rfl::kFootHash && m.parent == ~0ull && m.target == kGuestHorse);
    const rfl::Ack r = rfl::read_ack(riding.data() + kH, rfl::kAckSize);
    CHECK(r.ok && r.a0 == rfl::kRidingHash && r.a1 == 0 && r.parent == kGuestHorse && r.target == 0);
    const rfl::Ack d = rfl::read_ack(dis.data() + kH, rfl::kAckSize);
    CHECK(d.ok && d.a0 == 0xC9356CEBu && d.a1 == rfl::kDismountA1 && d.parent == kGuestHorse);
    CHECK(rfl::classify(f) == rfl::Kind::None && rfl::classify(r) == rfl::Kind::None);
    CHECK(rfl::classify(m) == rfl::Kind::Mount && rfl::classify(d) == rfl::Kind::Dismount);
    // The host's mount hash too.
    rfl::Ack h = m;
    h.a0 = 0x872A6750u;
    CHECK(rfl::classify(h) == rfl::Kind::Mount);
    // The dismount layer without an 0xB parent (already down) is not a dismount; nor an NPC's.
    rfl::Ack x = d;
    x.parent = ~0ull;
    CHECK(rfl::classify(x) == rfl::Kind::None);
    x = d;
    x.parent = 0xA0100001;
    CHECK(rfl::classify(x) == rfl::Kind::None);
    x = d;
    x.key = 0xB0101A7F;
    CHECK(rfl::classify(x) == rfl::Kind::None);
    // The native blocks of the logic are those of the capture.
    CHECK(std::memcmp(foot.data() + kH + rfl::kA0, rfl::kBlockFoot.data(), rfl::kBlock) == 0);
    CHECK(std::memcmp(foot.data() + kH + rfl::kA1, rfl::kBlockEmpty.data(), rfl::kBlock) == 0);
    CHECK(std::memcmp(riding.data() + kH + rfl::kA0, rfl::kBlockRiding.data(), rfl::kBlock) == 0);
    CHECK(std::memcmp(riding.data() + kH + rfl::kA1, rfl::kBlockEmpty.data(), rfl::kBlock) == 0);
    // Wrong sizes.
    CHECK(!rfl::read_ack(foot.data() + kH, 724).ok && !rfl::read_ack(nullptr, 725).ok);
}

void test_ride_rewrite() {
    const auto foot = unhex(kFootAck), riding = unhex(kRidingAck);
    // Mount, on: A0 = the native on-foot block, A1 = empty, everything else as received.
    {
        const auto orig = unhex(kMountAck);
        auto p = orig;
        const rfl::Outcome o = rfl::filter_packet(rfl::Mode::On, p.data(), p.size(), kGuestChar, kHostChar);
        CHECK(o.start_action && o.remote && o.kind == rfl::Kind::Mount && o.rewritten && o.ack.a0 == 0x91605616u);
        CHECK(block_eq(p, kH + rfl::kA0, foot, kH + rfl::kA0) && block_eq(p, kH + rfl::kA1, foot, kH + rfl::kA1));
        CHECK(same_except(p, orig, kH + rfl::kA0, kH + rfl::kA1 + rfl::kBlock));
        const rfl::Ack a = rfl::read_ack(p.data() + kH, rfl::kAckSize);
        CHECK(a.a0 == rfl::kFootHash && a.a1 == 0 && a.target == kGuestHorse && a.parent == ~0ull);
        CHECK(rfl::classify(a) == rfl::Kind::None);  // a second pass changes nothing
    }
    // Dismount, on: A0 = the native riding block, A1 = empty; parent, position, ts untouched.
    {
        const auto orig = unhex(kDismountAck);
        auto p = orig;
        const rfl::Outcome o = rfl::filter_packet(rfl::Mode::On, p.data(), p.size(), kGuestChar, 0);
        CHECK(o.remote && o.kind == rfl::Kind::Dismount && o.rewritten);
        CHECK(block_eq(p, kH + rfl::kA0, riding, kH + rfl::kA0) && block_eq(p, kH + rfl::kA1, riding, kH + rfl::kA1));
        CHECK(same_except(p, orig, kH + rfl::kA0, kH + rfl::kA1 + rfl::kBlock));
        CHECK(rfl::read_ack(p.data() + kH, rfl::kAckSize).parent == kGuestHorse);
    }
    // Probe and off: never written.
    for (const rfl::Mode mode : {rfl::Mode::Probe, rfl::Mode::Off}) {
        const auto orig = unhex(kMountAck);
        auto p = orig;
        const rfl::Outcome o = rfl::filter_packet(mode, p.data(), p.size(), kGuestChar, kHostChar);
        CHECK(!o.rewritten && p == orig);
        CHECK(mode == rfl::Mode::Off ? !o.start_action : (o.kind == rfl::Kind::Mount));
    }
    // Only exactly the known remote character (review 2026-10-10): any other 0xA (a third character, the client's
    // own) and an unknown remote key leave it alone.
    {
        auto p = unhex(kMountAck);
        const auto orig = p;
        CHECK(!rfl::filter_packet(rfl::Mode::On, p.data(), p.size(), 0, kHostChar).remote && p == orig);  // unknown
        CHECK(!rfl::filter_packet(rfl::Mode::On, p.data(), p.size(), 0xA0100999, 0).remote && p == orig);
        CHECK(!rfl::filter_packet(rfl::Mode::On, p.data(), p.size(), kGuestChar, kGuestChar).remote && p == orig);
        CHECK(!rfl::filter_packet(rfl::Mode::On, p.data(), p.size(), 0, 0).remote && p == orig);
    }
    // The host's control state 4: u8 4 | u32 key (0xA only).
    {
        const uint8_t c[5] = {4, 0x01, 0x00, 0x10, 0xA0};
        CHECK(rfl::host_character_from_control(c, 5) == kHostChar);
        const uint8_t wrong_state[5] = {1, 0x01, 0x00, 0x10, 0xA0};
        const uint8_t npc[5] = {4, 0x02, 0x00, 0x10, 0xB0};
        CHECK(rfl::host_character_from_control(wrong_state, 5) == 0 && rfl::host_character_from_control(npc, 5) == 0);
        CHECK(rfl::host_character_from_control(c, 4) == 0 && rfl::host_character_from_control(nullptr, 5) == 0);
    }
    // Not a StartActionAck / a wrong length in the header.
    {
        auto p = unhex(kMountAck);
        p[0] = 0x8A;
        CHECK(!rfl::filter_packet(rfl::Mode::On, p.data(), p.size(), kGuestChar, 0).start_action);
        p = unhex(kMountAck);
        p[3] = 0xD4;
        CHECK(!rfl::filter_packet(rfl::Mode::On, p.data(), p.size(), kGuestChar, 0).start_action);
        CHECK(!rfl::filter_packet(rfl::Mode::On, p.data(), p.size() - 1, kGuestChar, 0).start_action);
    }
    CHECK(rfl::is_remote_player(kGuestChar, kGuestChar, 0) && !rfl::is_remote_player(kHostChar, kGuestChar, 0));
    CHECK(rfl::is_remote_player(kHostChar, kHostChar, kGuestChar) && !rfl::is_remote_player(kHostChar, 0, kGuestChar));
    CHECK(!rfl::is_remote_player(0xB0100002, 0xB0100002, 0) && !rfl::is_remote_player(kHostChar, kHostChar, kHostChar));
}

void test_ride_probe_tables() {
    const auto foot = unhex(kFootAck), mount = unhex(kMountAck), riding = unhex(kRidingAck), dis = unhex(kDismountAck);
    rfl::ParentWatch w;
    rfl::PairTable t;
    const rfl::Ack f = rfl::read_ack(foot.data() + kH, rfl::kAckSize);
    const rfl::Ack r = rfl::read_ack(riding.data() + kH, rfl::kAckSize);
    const rfl::Ack d = rfl::read_ack(dis.data() + kH, rfl::kAckSize);
    rfl::Ack plain = f;
    plain.target = 0;
    // First sight: no change noted; a target 0xB is in the window.
    rfl::WindowNote n = w.note(plain, 1000);
    CHECK(!n.in_window && !n.after_change);
    n = w.note(f, 1100);
    CHECK(n.in_window && !n.after_change);
    // Mounted: the parent changes -> within 3 s everything is recorded.
    n = w.note(r, 2000);
    CHECK(n.in_window && n.after_change);
    n = w.note(d, 2500);
    CHECK(n.in_window && n.after_change);
    rfl::Ack down = plain;  // on the ground again: another change
    n = w.note(down, 3000);
    CHECK(n.in_window && n.after_change);
    n = w.note(down, 6001);
    CHECK(!n.in_window && !n.after_change);
    // Pairs with flags; the same pair counts.
    t.add(d.a0, d.a1, rfl::pair_flags(d, true, rfl::Kind::Dismount));
    t.add(d.a0, d.a1, rfl::pair_flags(d, true, rfl::Kind::Dismount));
    t.add(f.a0, f.a1, rfl::pair_flags(f, false, rfl::Kind::None));
    CHECK(t.used() == 2 && t.slot[0].n.load() == 2 && t.slot[0].a0.load() == 0xC9356CEBu &&
          t.slot[0].flags.load() == (1 | 4 | (2 << 4)) && t.slot[1].flags.load() == 2);
    for (uint32_t i = 0; i < 70; ++i) t.add(i, i, 0);
    CHECK(t.used() == rfl::PairTable::kSlots && t.overflow.load() == 70 - (rfl::PairTable::kSlots - 2));
    (void)mount;
}

// ---------------------------------------------------------------- P1 child_reannounce

void test_reannounce() {
    const auto p = ran::encode(0xB0100002, 0x90100000, 2);
    CHECK(p[0] == ran::kControlReannounce && p.size() == ran::kPayloadSize);
    const auto r = ran::decode(p);
    CHECK(r && r->key == 0xB0100002 && r->owner == 0x90100000 && r->attempt == 2);
    auto bad = p;
    bad[0] = 1;
    CHECK(!ran::decode(bad));
    bad = p;
    bad[9] = 0;
    CHECK(!ran::decode(bad));
    bad = p;
    bad[9] = 4;
    CHECK(!ran::decode(bad));
    bad = ran::encode(0xA0100001, 0x90100000, 1);
    CHECK(!ran::decode(bad));
    CHECK(!ran::decode(std::span<const uint8_t>(p.data(), 9)));
    // Session 9's value.
    const auto f = ran::split_failure(0x90100000B0100002ull);
    CHECK(f && f->key == 0xB0100002 && f->owner == 0x90100000);
    CHECK(!ran::split_failure(0) && !ran::split_failure(0xB0100002ull) && !ran::split_failure(0x90100000A0100001ull));
    // Attempts: 1, then too soon, then 2 and 3, then exhausted.
    ran::Attempts<2> a;
    uint8_t n = 0;
    CHECK(a.next(0xB0100002, 1000, n) == ran::Verdict::Ask && n == 1);
    CHECK(a.next(0xB0100002, 1500, n) == ran::Verdict::TooSoon && n == 0);
    CHECK(a.next(0xB0100002, 2000, n) == ran::Verdict::Ask && n == 2);
    CHECK(a.next(0xB0100002, 9000, n) == ran::Verdict::Ask && n == 3);
    CHECK(a.next(0xB0100002, 20000, n) == ran::Verdict::Exhausted);
    CHECK(a.next(0xA0100001, 20000, n) == ran::Verdict::NotAChild);
    CHECK(a.next(0xB0100003, 20000, n) == ran::Verdict::Ask && n == 1);
    CHECK(a.next(0xB0100004, 20000, n) == ran::Verdict::Full);
    CHECK(a.count(0xB0100002) == 3);
    a.reset();
    CHECK(a.count(0xB0100002) == 0 && a.next(0xB0100002, 0, n) == ran::Verdict::Ask && n == 1);
    // The host: peek only (nothing recorded until child_settle confirmed a child of the host), then commit; the
    // guest's attempt number when it is ahead.
    ran::Attempts<4> h;
    CHECK(h.accept_peek(0xB0100002, 3, 1000, n) == ran::Verdict::Ask && n == 3);
    CHECK(h.count(0xB0100002) == 0 && h.total() == 0);
    CHECK(h.accept_peek(0xB0100009, 1, 1000, n) == ran::Verdict::Ask && n == 1);  // refused later: never recorded
    CHECK(h.commit(0xB0100002, 1000) && h.count(0xB0100002) == 1 && h.total() == 1 && h.count(0xB0100009) == 0);
    CHECK(h.accept_peek(0xB0100002, 1, 1500, n) == ran::Verdict::TooSoon);
    CHECK(h.accept_peek(0xB0100002, 1, 2000, n) == ran::Verdict::Ask && n == 2);
    // The per-epoch cap over all keys.
    ran::Attempts<64> cap;
    for (uint32_t i = 0; i < ran::kMaxPerEpoch; ++i) {
        CHECK(cap.next(0xB0200000 + i, 1000, n) == ran::Verdict::Ask);
    }
    CHECK(cap.next(0xB0300000, 1000, n) == ran::Verdict::EpochCap && cap.total() == ran::kMaxPerEpoch);
    cap.reset();
    CHECK(cap.next(0xB0300000, 1000, n) == ran::Verdict::Ask);
    // Only the exact frame is taken out of the guest's stream.
    {
        std::vector<uint8_t> frame = {1, 0, 0xFF, 10, 0};
        frame.insert(frame.end(), p.begin(), p.end());
        CHECK(ran::is_reannounce_frame(frame));
        auto g = frame;
        g[2] = 0;  // slot 0
        CHECK(!ran::is_reannounce_frame(g));
        g = frame;
        g[3] = 11;  // another length
        CHECK(!ran::is_reannounce_frame(g));
        g = frame;
        g[5] = 1;  // another state
        CHECK(!ran::is_reannounce_frame(g));
        g = frame;
        g.push_back(0);
        CHECK(!ran::is_reannounce_frame(g));
        g = frame;
        g[0] = 0xEA;  // a Troc opcode (1002)
        g[1] = 0x03;
        CHECK(!ran::is_reannounce_frame(g));
    }
    CHECK(ran::backoff_ms(1) == 2000 && ran::backoff_ms(2) == 4000 && ran::backoff_ms(3) == 8000);
    // child_settle's wait for a re-announce: kept until not_before, then released as settled.
    cst::Readings rd;
    rd.actor = uintptr_t{0x7000};
    rd.actor_then = 0x7000;
    rd.owner = uintptr_t{0xA000};
    rd.owner_then = 0xA000;
    rd.observer_valid = true;
    rd.observes = true;
    rd.main_last_ms = 1000;
    rd.since_ms = 5000;
    rd.not_before_ms = 7000;
    rd.now_ms = 6999;
    const char* why = "";
    cst::Kind kind = cst::Kind::None;
    CHECK(cst::release_step(rd, 4000, why, &kind) == cst::Step::Keep && std::strcmp(why, "re-announce backoff") == 0);
    rd.now_ms = 7000;
    CHECK(cst::release_step(rd, 4000, why, &kind) == cst::Step::Release && kind == cst::Kind::Settled);
    rd.observes = false;  // out of range meanwhile: dropped (the engine announces it again)
    CHECK(cst::release_step(rd, 4000, why, &kind) == cst::Step::Drop);
}

// ---------------------------------------------------------------- C1 combat_follow

std::vector<uint8_t> attack(uint32_t target, uint32_t attacker) {
    std::vector<uint8_t> v(199, 0);
    std::memcpy(v.data(), &target, 4);
    std::memcpy(v.data() + 4, &attacker, 4);
    return v;
}

void test_combat_read() {
    constexpr uint32_t u2 = 0xA0100377, enemy = 0xB0101A7E;
    CHECK(cfl::read_out(cfl::kOpAttackTargetAck, attack(enemy, u2), u2).engage == enemy);
    CHECK(cfl::read_out(cfl::kOpAttackTargetAck, attack(u2, enemy), u2).engage == enemy);
    CHECK(cfl::read_out(cfl::kOpAttackTargetAck, attack(enemy, 0xA0100001), u2).engage == 0);
    CHECK(cfl::read_out(cfl::kOpAttackTargetAck, attack(enemy, u2), 0).engage == 0);
    // A StartActionAck of an enemy aimed at user 2: engaged and a sample.
    std::vector<uint8_t> sa(cfl::kStartAck, 0);
    std::memcpy(sa.data(), &enemy, 4);
    std::memcpy(sa.data() + cfl::kStartTarget, &u2, 4);
    const uint64_t ts = 821963, none = ~0ull;
    std::memcpy(sa.data() + cfl::kStartTs, &ts, 8);
    std::memcpy(sa.data() + cfl::kStartParent, &none, 8);
    cfl::Seen s = cfl::read_out(cfl::kOpStartActionAck, sa, u2);
    CHECK(s.engage == enemy && s.sample == enemy && s.ts == ts && !s.parent);
    const uint32_t other = 0xA0100001;
    std::memcpy(sa.data() + cfl::kStartTarget, &other, 4);
    s = cfl::read_out(cfl::kOpStartActionAck, sa, u2);
    CHECK(s.engage == 0 && s.sample == enemy);
    // The real enemy Move of the guest capture: a sample, no parent.
    const auto mv = unhex(kEnemyMoveAck);
    s = cfl::read_out(cfl::kOpMoveActorAck, std::span<const uint8_t>(mv).subspan(cfl::kHeader), u2);
    CHECK(s.sample == 0xB0101A7F && s.ts == 0x0C86E0 && !s.parent);
    // A character's Move is not followed.
    auto ch = mv;
    const uint32_t ck = 0xA0100001;
    std::memcpy(ch.data() + cfl::kHeader, &ck, 4);
    CHECK(cfl::read_out(cfl::kOpMoveActorAck, std::span<const uint8_t>(ch).subspan(cfl::kHeader), u2).sample == 0);
    // Liveness: NotifyHandleDeadAck (G1 673.333: 0xB0101A7E killed by 0xA0100377) and RemoveActorAck (G1 76.828).
    const auto dead = unhex("7e1a10b0770310a0080001000000000000000a");
    CHECK(dead.size() == 19 && cfl::read_out(cfl::kOpNotifyHandleDeadAck, dead, u2).gone == 0xB0101A7E);
    CHECK(cfl::read_out(cfl::kOpNotifyHandleDeadAck, dead, 0).gone == 0xB0101A7E);  // user 2 unknown: still
    const auto removed = unhex("0400000000159b0210b0");
    CHECK(cfl::read_out(cfl::kOpRemoveActorAck, removed, u2).gone == 0xB010029B);
    CHECK(cfl::read_out(cfl::kOpRemoveActorAck, std::span<const uint8_t>(removed).first(9), u2).gone == 0);
    const auto player_dead = unhex("770310a07e1a10b0080001000000000000000a");
    CHECK(cfl::read_out(cfl::kOpNotifyHandleDeadAck, player_dead, u2).gone == 0);  // a character: not an enemy
}

void test_combat_table_and_decide() {
    cfl::Table<4> t;
    // Engaged keys: the most recent first, at most kMaxFollowed, gone after kEngageMs.
    for (uint32_t i = 0; i < 4; ++i) t.engage(0xB0000010 + i, 1000 + i);
    t.engage(0xB0000099, 1100);  // full: replaces the oldest (…10)
    CHECK(!t.find(0xB0000010) && t.find(0xB0000099));
    std::array<cfl::Enemy, cfl::kMaxFollowed> out{};
    CHECK(t.followed(1200, out) == 4 && out[0].key == 0xB0000099 && out[1].key == 0xB0000013);
    CHECK(t.followed(1003 + cfl::kEngageMs + 1, out) == 1 && out[0].key == 0xB0000099);
    CHECK(t.engaged(1200) == 4);
    // Samples only for engaged keys.
    constexpr uint64_t G0 = 820970;  // the host client's game clock: a real ts is ~10 ms behind it
    t.sample(0xB0000077, 500, 510, false, 1200);
    CHECK(!t.find(0xB0000077));
    cfl::Enemy* e = t.find(0xB0000099);
    CHECK(e && !e->has_anchor);
    const float pos[3] = {-10692.15f, 556.81f, -4202.64f}, q[4] = {0, 0.489f, 0, 0.872f};
    uint64_t ts = 0;
    CHECK(cfl::decide(*e, G0, 1200, pos, q, ts) == cfl::Skip::NoAnchor && ts == 0);
    t.sample(0xB0000099, 820960, G0, false, 1200);
    e = t.find(0xB0000099);
    CHECK(e->has_anchor && e->anchor_ts == 820960 && e->anchor_game == G0 && e->max_real_ts == 820960 &&
          e->offset_ms == -10 && !e->clock_bad);
    CHECK(cfl::decide(*e, G0, 1200, pos, q, ts) == cfl::Skip::Clock);  // the clock did not move
    CHECK(cfl::decide(*e, G0 + 60, 1260, pos, q, ts) == cfl::Skip::None && ts == 821020);
    t.sent(0xB0000099, ts, G0 + 60, 1260, pos, q);
    e = t.find(0xB0000099);
    // Same pose 60 ms later: still; after the keepalive: sent again; moved: sent.
    CHECK(cfl::decide(*e, G0 + 120, 1320, pos, q, ts) == cfl::Skip::Still);
    CHECK(cfl::decide(*e, G0 + 560, 1760, pos, q, ts) == cfl::Skip::None && ts == 821520);
    const float moved[3] = {pos[0] + 0.05f, pos[1], pos[2]};
    CHECK(cfl::decide(*e, G0 + 120, 1320, moved, q, ts) == cfl::Skip::None && ts == 821080);
    // Velocity from the last sent pose over the game clock.
    float v[3] = {};
    cfl::velocity(*e, moved, G0 + 110, v);
    CHECK(std::fabs(v[0] - 1.0f) < 3e-2f && v[1] == 0 && std::fabs(v[2]) < 1e-3f);
    cfl::velocity(*e, moved, G0 + 1000, v);  // too long ago: 0
    CHECK(v[0] == 0 && v[1] == 0 && v[2] == 0);
    // A real sample with an older ts than ours: the next synthetic is still above what was sent.
    t.sample(0xB0000099, 821000, G0 + 50, false, 1300);
    e = t.find(0xB0000099);
    CHECK(cfl::synth_ts(*e, G0 + 60) == 0);  // 821010 <= 821020 already sent
    CHECK(cfl::synth_ts(*e, G0 + 160) == 821110);
    // A real ts that went to user 2 without an anchor (clock unread): synthetics stay above it too.
    t.sample(0xB0000099, 821900, 0, false, 1400);
    e = t.find(0xB0000099);
    CHECK(e->max_real_ts == 821900 && e->anchor_ts == 821000);
    CHECK(cfl::synth_ts(*e, G0 + 300) == 0 && cfl::synth_ts(*e, G0 + 1000) == 821950);
    // A real ts far from the game clock read here: not followed until a sample agrees again.
    t.sample(0xB0000099, 900000, G0 + 1000, false, 1500);
    e = t.find(0xB0000099);
    CHECK(e->clock_bad && e->anchor_ts == 821000 && e->max_real_ts == 900000);
    CHECK(cfl::decide(*e, G0 + 1060, 1560, moved, q, ts) == cfl::Skip::ClockBad);
    t.sample(0xB0000099, 900050, 900060, false, 1600);  // the clock agrees again (|offset| 10 ms)
    e = t.find(0xB0000099);
    CHECK(!e->clock_bad && e->anchor_ts == 900050 && e->anchor_game == 900060);
    CHECK(cfl::decide(*e, 900120, 1660, moved, q, ts) == cfl::Skip::None && ts == 900110);
    // No real sample for kRealStaleMs: stale (not moving on the host's client).
    CHECK(cfl::decide(*e, 900120, 1600 + cfl::kRealStaleMs + 1, moved, q, ts) == cfl::Skip::Stale);
    // A parent: not followed. An old anchor: not trusted.
    t.sample(0xB0000099, 900100, 900110, true, 1700);
    CHECK(cfl::decide(*t.find(0xB0000099), 900200, 1800, moved, q, ts) == cfl::Skip::Parent);
    t.sample(0xB0000099, 900200, 900210, false, 1800);
    CHECK(cfl::decide(*t.find(0xB0000099), 900210 + cfl::kAnchorMaxAgeMs + 1, 1900, moved, q, ts) ==
          cfl::Skip::OldAnchor);
    // Died / removed: forgotten; engaged again later it has no anchor until it moves.
    CHECK(t.remove(0xB0000099) && !t.find(0xB0000099) && !t.remove(0xB0000099));
    t.engage(0xB0000099, 2000);
    CHECK(t.find(0xB0000099) && !t.find(0xB0000099)->has_anchor);
    t.reset();
    CHECK(!t.find(0xB0000099) && t.engaged(1200) == 0);
    // Distance, sanity.
    const float a[3] = {0, 0, 0}, b[3] = {3, 4, 0};
    CHECK(std::fabs(cfl::distance(a, b) - 5.0f) < 1e-6f);
    const float bad_q[4] = {0, 0, 0, 0.5f};
    CHECK(cfl::sane_pose(pos, q) && !cfl::sane_pose(pos, bad_q));
}

void test_combat_packet() {
    // The real enemy Ack rebuilt from its pose, velocity and ts: byte for byte but the angular vec3 (+0x80, 0 here).
    const auto real = unhex(kEnemyMoveAck);
    CHECK(real.size() == cfl::kPacketSize);
    const uint8_t* p = real.data() + cfl::kHeader;
    float pos[3], quat[4], vel[3];
    uint64_t ts = 0;
    std::memcpy(pos, p + 0x04, 12);
    std::memcpy(quat, p + 0x10, 16);
    std::memcpy(vel, p + 0x74, 12);
    std::memcpy(&ts, p + 0xD5, 8);
    const auto made = cfl::make_move_ack(0xB0101A7F, pos, quat, vel, ts);
    std::vector<uint8_t> mv(made.begin(), made.end());
    // The speed (+0xE7) is |velocity| (0.2806 in the capture); the last bit of the game's own sqrt may differ.
    float speed_made = 0, speed_real = 0;
    std::memcpy(&speed_made, mv.data() + cfl::kHeader + 0xE7, 4);
    std::memcpy(&speed_real, p + 0xE7, 4);
    CHECK(std::fabs(speed_made - speed_real) < 1e-5f && std::fabs(speed_real - 0.2806f) < 1e-3f);
    std::memcpy(mv.data() + cfl::kHeader + 0xE7, &speed_real, 4);
    CHECK(same_except(mv, real, cfl::kHeader + 0x80, cfl::kHeader + 0x8C));
    for (size_t i = 0x80; i < 0x8C; ++i) CHECK(mv[cfl::kHeader + i] == 0);
    // And it reads back as a sample of that enemy with no parent.
    const cfl::Seen s = cfl::read_out(cfl::kOpMoveActorAck, std::span<const uint8_t>(mv).subspan(cfl::kHeader), 1);
    CHECK(s.sample == 0xB0101A7F && s.ts == ts && !s.parent);
}

// ---------------------------------------------------------------- M1 guest_horse_probe

// ---------------------------------------------------------------- game_errors' failure ring (seq_ring.h)

void test_seq_ring() {
    cdmp::game::SeqRing<4> r;
    uint64_t cur = 0, out[8] = {}, lost = 0;
    CHECK(r.read(cur, out, 8, &lost) == 0 && cur == 0 && lost == 0);
    r.push(11);
    r.push(12);
    CHECK(r.count() == 2 && r.read(cur, out, 8, &lost) == 2 && out[0] == 11 && out[1] == 12 && cur == 2 && !lost);
    // More than N - 1 behind: only the last N - 1 are read, the rest counted lost.
    for (uint64_t v = 13; v <= 18; ++v) r.push(v);  // indexes 2..7
    CHECK(r.read(cur, out, 8, &lost) == 3 && out[0] == 16 && out[1] == 17 && out[2] == 18 && lost == 3 && cur == 8);
    // `max` respected; the cursor resumes.
    r.push(19);
    r.push(20);
    CHECK(r.read(cur, out, 1, &lost) == 1 && out[0] == 19 && cur == 9);
    CHECK(r.read(cur, out, 8, &lost) == 1 && out[0] == 20 && cur == 10);
    // A cursor ahead of the count (a reset) comes back to it.
    uint64_t ahead = 99;
    CHECK(r.read(ahead, out, 8, &lost) == 0 && ahead == 10);
    // Concurrent writers: every value read once, in index order, none torn.
    cdmp::game::SeqRing<64> big;
    std::vector<std::thread> w;
    for (int t = 0; t < 4; ++t) {
        w.emplace_back([&big, t] {
            for (uint64_t i = 0; i < 10; ++i) big.push(0x100 * static_cast<uint64_t>(t + 1) + i);
        });
    }
    for (auto& th : w) th.join();
    uint64_t c2 = 0, got[64] = {};
    const size_t n = big.read(c2, got, 64, &lost);
    CHECK(n == 40 && lost == 0 && big.count() == 40);
    uint64_t sum = 0;
    for (size_t i = 0; i < n; ++i) sum += got[i];
    CHECK(sum == (0x100 + 0x200 + 0x300 + 0x400) * 10 + 4 * 45);
}

void test_horse_req() {
    std::vector<uint8_t> rider(hfl::kReqSize, 0x11);
    const uint32_t key = kGuestChar, horse = kGuestHorse;
    const uint64_t parent = horse, ts = 734011;
    const float world[3] = {-10669.6f, 616.14f, -3850.8f}, wq[4] = {0, 0.6f, 0, 0.8f};
    std::memcpy(rider.data(), &key, 4);
    std::memcpy(rider.data() + hfl::kParent, &parent, 8);
    std::memcpy(rider.data() + hfl::kWorldPos, world, 12);
    std::memcpy(rider.data() + hfl::kWorldQuat, wq, 16);
    std::memcpy(rider.data() + hfl::kTs, &ts, 8);
    std::vector<uint8_t> out;
    CHECK(hfl::make_horse_req(rider, out) && out.size() == hfl::kReqSize);
    const hfl::Move m = hfl::read_move(out, hfl::kReqSize);
    CHECK(m.shape == hfl::Shape::OnFoot && m.key == horse && m.ts == ts);
    CHECK(std::memcmp(out.data() + hfl::kPos, world, 12) == 0 && std::memcmp(out.data() + hfl::kQuat, wq, 16) == 0);
    CHECK(std::memcmp(out.data() + hfl::kLocalPos, world, 12) == 0);
    for (size_t i = hfl::kWorldPos; i < hfl::kWorldEnd; ++i) CHECK(out[i] == 0);
    for (size_t i = hfl::kReqHistory; i < hfl::kReqSize; ++i) CHECK(out[i] == 0);
    CHECK(out[0xDE] == 0x11 && out[0xE9] == 0x11);  // the rider's tail kept
    // Not a rider: nothing.
    std::vector<uint8_t> foot = rider;
    std::memset(foot.data() + hfl::kParent, 0xFF, 8);
    CHECK(!hfl::make_horse_req(foot, out));
    CHECK(!hfl::make_horse_req(std::span<const uint8_t>(rider.data(), 513), out));
    hfl::ErrHistMm h;
    h.add(30);
    h.add(150);
    h.add(5000);
    CHECK(h.n[0].load() == 1 && h.n[2].load() == 1 && h.n[6].load() == 1);
}

}  // namespace

int main() {
    test_ride_read_classify();
    test_ride_rewrite();
    test_ride_probe_tables();
    test_reannounce();
    test_combat_read();
    test_combat_table_and_decide();
    test_combat_packet();
    test_horse_req();
    test_seq_ring();
    if (g_failures) {
        std::printf("optd_s9: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_s9: all passed\n");
    return 0;
}
