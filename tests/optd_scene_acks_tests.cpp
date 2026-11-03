// P8: user 2's own scenes and the observer_hold side effect (docs/research/pendientes/p8-escenas-y-acks.md).
//
// SCOPE: offline logic only.
//  - observer_scene_copy_logic.h: the book of the 2661/2810 that reached user 2's queue (trips, epochs,
//    replacement, closing, bound) on the session-6B packets (the ESC "view item" sequencer 0x1A4 of user 2,
//    the host's 0x1A5), and the copy decision, reason by reason.
//  - observer_hold_acks_logic.h: the hold of a withheld NPC's per-actor packets until its 2005 (order within
//    and across keys, the session-6B pattern 2404 x3 + 2723, drops, orphans, expiry, epochs, bounds and the
//    spill in order), the opcode/key filter and the 2005 key parser on the session-4 fixtures.
// They do NOT test the hooks, the emitter's ABI, take_frame, the threads nor the game's behaviour.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "mod/session2/observer_hold_acks_logic.h"
#include "mod/session2/observer_scene_copy_logic.h"

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

std::vector<uint8_t> hex(const char* s) {
    std::vector<uint8_t> out;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; s[i] && s[i + 1]; i += 2) out.push_back(static_cast<uint8_t>(nib(s[i]) << 4 | nib(s[i + 1])));
    return out;
}

std::vector<uint8_t> load(const char* name) {
    const std::string path = std::string(CDMP_SOURCE_DIR) + "/tests/data/" + name;
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Session 6B, user 2's capture (cdmp_packets_20261009-224848.bin) #107250, t=153.653: the 2661 of user 2's ESC
// item scene, sequencer 0x1A4, actor and owner A010031F, 1 binding 0x20000002 -> A010031F (102 bytes).
const char* k2661UserScene =
    "041f0310a01f0310a0a40100000000000006035f670f00000000002e38ae7000000000f97e27c6b50a1b44e2b674c5cd841f4000000000"
    "1f0310a01f0310a00000000000ffffff7f01007a450f007a450f00a5420f000000000000020000201f0310a0050000";
// The host's own client (cdmp_packets_20261009-234606.bin) #288949: the host's item scene 0x1A5 (A0100001).
const char* k2661HostScene =
    "04010010a0010010a0a50100000000000006035f670f00000000002e38ae70000000000c5227c6e4211a4429ac74c51933893f00000000"
    "010010a0010010a00000000000ffffff7f02007a450f007a450f00a5420f00000000000002000020010010a0050000";
// #257976: the 2810 of 0x1A4 the server sent to the HOST's client (A0100001) - the one user 2 never got.
const char* k2810ToHost = "010010a0a4010000000000000000000000";
// What the copy serializes for user 2: the same id, u32 and u8, user 2's character.
const char* k2810ToUser2 = "1f0310a0a4010000000000000000000000";

constexpr uint32_t kUser2 = 0xA010031F, kHost = 0xA0100001;

// ---------------------------------------------------------------- the book

void test_book() {
    osc::SceneBook b(4);
    const auto p = hex(k2661UserScene);
    CHECK(p.size() == 102);
    osc::Delivered d;
    CHECK(b.on_out(osc::kOpPrepare, p, 100, 1, &d) == osc::SceneBook::Seen::Prepared);
    CHECK(d.seq == 0x1A4 && d.actor == kUser2 && d.owner == kUser2 && d.epoch == 1 && d.trip == 0 && d.t_us == 100);
    CHECK(b.live() == 1 && b.find(0x1A4) && !b.find(0x1A5));
    // The same id again: replaced, not duplicated.
    CHECK(b.on_out(osc::kOpPrepare, p, 200, 1) == osc::SceneBook::Seen::Replaced && b.live() == 1);
    // A trip: what came before keeps its trip.
    CHECK(b.on_out(osc::kOpMoveField, {}, 300, 1) == osc::SceneBook::Seen::Trip && b.trip() == 1);
    CHECK(b.find(0x1A4)->trip == 0);
    // Its 2810 reaching user 2 (our copy, or the native one when user 2 owns the manager) closes it.
    const auto del = hex(k2810ToUser2);
    CHECK(del.size() == osc::kDeletePayload);
    CHECK(b.on_out(osc::kOpDelete, del, 400, 1, &d) == osc::SceneBook::Seen::Closed && d.seq == 0x1A4);
    CHECK(b.live() == 0 && b.closed() == 1);
    CHECK(b.on_out(osc::kOpDelete, del, 401, 1) == osc::SceneBook::Seen::ClosedUnknown && b.closed_unknown() == 1);
    // Wrong sizes are ignored.
    CHECK(b.on_out(osc::kOpPrepare, std::vector<uint8_t>(90, 0), 500, 1) == osc::SceneBook::Seen::Other);
    CHECK(b.on_out(osc::kOpDelete, std::vector<uint8_t>(16, 0), 500, 1) == osc::SceneBook::Seen::Other);
    CHECK(b.on_out(2111, del, 500, 1) == osc::SceneBook::Seen::Other);
    // Bounded: the oldest goes.
    for (uint64_t s = 1; s <= 5; ++s) {
        auto q = p;
        std::memcpy(q.data() + 9, &s, 8);
        b.on_out(osc::kOpPrepare, q, 600 + s, 1);
    }
    CHECK(b.live() == 4 && b.evicted() == 1 && !b.find(1) && b.find(5));
    // Another epoch: the book starts again.
    CHECK(b.on_out(osc::kOpMoveField, {}, 700, 2) == osc::SceneBook::Seen::Trip);
    CHECK(b.epoch() == 2 && b.trip() == 1 && b.live() == 0);
    b.on_epoch(3);
    CHECK(b.epoch() == 3 && b.trip() == 0);
}

// ---------------------------------------------------------------- the decision

osc::CopyFacts session6b_facts(const osc::SceneBook& b) {
    // 0x2879440 deleting user 2's 0x1A4 in the host's manager (HB 23:51:52.055, T4572).
    osc::CopyFacts f;
    f.on = true;
    f.from_delete = true;
    f.consistent = true;
    f.seq = 0x1A4;
    f.owner_key = kUser2;
    f.recipient_key = kHost;
    f.user2_key = kUser2;
    f.live_session = true;
    f.epoch = 1;
    f.delivered = b.find(0x1A4);
    f.book_epoch = b.epoch();
    f.book_trip = b.trip();
    f.realm = true;
    f.send_ctx = true;
    f.actor_ok = true;
    return f;
}

void test_decide() {
    osc::SceneBook b;
    b.on_out(osc::kOpMoveField, {}, 1, 1);  // trip 1: user 2 enters the host's field
    b.on_out(osc::kOpPrepare, hex(k2661UserScene), 2, 1);
    osc::CopyFacts f = session6b_facts(b);
    CHECK(osc::decide(f) == osc::Why::Copy);

    auto with = [&](auto change) {
        osc::CopyFacts g = session6b_facts(b);
        change(g);
        return osc::decide(g);
    };
    CHECK(with([](osc::CopyFacts& g) { g.on = false; }) == osc::Why::Off);
    CHECK(with([](osc::CopyFacts& g) { g.reentry = true; }) == osc::Why::Reentry);
    CHECK(with([](osc::CopyFacts& g) { g.from_delete = false; }) == osc::Why::NotDeleteSite);
    CHECK(with([](osc::CopyFacts& g) { g.consistent = false; }) == osc::Why::Inconsistent);
    CHECK(with([](osc::CopyFacts& g) { g.live_session = false; }) == osc::Why::NoUser2);
    CHECK(with([](osc::CopyFacts& g) { g.user2_key = 0; }) == osc::Why::NoUser2);
    // The host's own scene (0x1A5, owner A0100001): never copied.
    CHECK(with([](osc::CopyFacts& g) {
              g.seq = 0x1A5;
              g.owner_key = kHost;
          }) == osc::Why::NotUser2Scene);
    // User 2 owns the manager (its own field): the native 2810 already goes to it.
    CHECK(with([](osc::CopyFacts& g) { g.recipient_key = kUser2; }) == osc::Why::AlreadyToUser2);
    CHECK(with([](osc::CopyFacts& g) { g.logout = true; }) == osc::Why::LogoutAsked);
    CHECK(with([](osc::CopyFacts& g) { g.delivered.reset(); }) == osc::Why::NotDelivered);
    CHECK(with([](osc::CopyFacts& g) { g.epoch = 2; }) == osc::Why::OtherEpoch);
    CHECK(with([](osc::CopyFacts& g) { g.book_trip = 2; }) == osc::Why::OtherTrip);
    CHECK(with([](osc::CopyFacts& g) { g.delivered->owner = kHost; }) == osc::Why::OwnerMismatch);
    CHECK(with([](osc::CopyFacts& g) { g.delivered->actor = kHost; }) == osc::Why::OwnerMismatch);
    CHECK(with([](osc::CopyFacts& g) { g.realm = false; }) == osc::Why::NoRealm);
    CHECK(with([](osc::CopyFacts& g) { g.send_ctx = false; }) == osc::Why::NoSendCtx);
    CHECK(with([](osc::CopyFacts& g) { g.scope_open = true; }) == osc::Why::ScopeOpen);
    CHECK(with([](osc::CopyFacts& g) { g.actor_ok = false; }) == osc::Why::NoActor);
    for (int i = 0; i < static_cast<int>(osc::Why::Count); ++i) {
        CHECK(std::string(osc::why_name(static_cast<osc::Why>(i))) != "?");
    }

    // The whole 6B sequence as user 2's queue sees it: the host's 0x1A5 never reaches it (ToTarget the host),
    // 0x1A4's 2661 does; its native 2810 goes to the host (not to this book); the copy reaches user 2 once, and
    // a second native 2810 of the same id (cannot happen, but) finds nothing to copy.
    osc::SceneBook q;
    q.on_out(osc::kOpMoveField, {}, 1, 1);
    q.on_out(osc::kOpPrepare, hex(k2661UserScene), 2, 1);
    osc::CopyFacts host = session6b_facts(q);
    host.seq = 0x1A5;
    host.owner_key = kHost;
    host.delivered = q.find(0x1A5);
    CHECK(osc::decide(host) == osc::Why::NotUser2Scene && !host.delivered);
    CHECK(osc::decide(session6b_facts(q)) == osc::Why::Copy);
    q.on_out(osc::kOpDelete, hex(k2810ToUser2), 3, 1);  // our copy in user 2's queue
    CHECK(osc::decide(session6b_facts(q)) == osc::Why::NotDelivered);
    // The 2810 the host got carries the host's key; the copy carries user 2's, the rest identical.
    const auto a = hex(k2810ToHost), c = hex(k2810ToUser2);
    CHECK(osc::le32(a.data()) == kHost && osc::le32(c.data()) == kUser2);
    CHECK(std::equal(a.begin() + 4, a.end(), c.begin() + 4));
    // The host's own 2661 parses as the host's (it never reaches user 2's book).
    osc::SceneBook h;
    osc::Delivered d;
    h.on_out(osc::kOpPrepare, hex(k2661HostScene), 1, 1, &d);
    CHECK(d.seq == 0x1A5 && d.actor == kHost && d.owner == kHost);
}

// ---------------------------------------------------------------- observer_hold_acks

std::vector<uint8_t> wire(uint16_t op, std::vector<uint8_t> payload) {
    std::vector<uint8_t> p(5 + payload.size());
    p[0] = static_cast<uint8_t>(op);
    p[1] = static_cast<uint8_t>(op >> 8);
    p[2] = 0xFF;
    p[3] = static_cast<uint8_t>(payload.size());
    p[4] = static_cast<uint8_t>(payload.size() >> 8);
    std::copy(payload.begin(), payload.end(), p.begin() + 5);
    return p;
}

std::vector<uint8_t> npc(uint16_t op, uint32_t key, uint8_t marker, size_t size = 16) {
    std::vector<uint8_t> payload(size, 0);
    std::memcpy(payload.data(), &key, 4);
    payload[4] = marker;
    return wire(op, payload);
}

void test_filter_and_parser() {
    // Session 6B, the held NPC B0100425: the payload prefixes of its 2404 (#10348) and 2723 (#10386).
    const auto ai = hex("250410b001000100010001000100010001010101");
    const auto spawn = hex("250410b0000000004e698c960000ffffffffffff");
    CHECK(oha::npc_key_of(2404, ai) == std::optional<uint32_t>(0xB0100425u));
    CHECK(oha::npc_key_of(2723, spawn) == std::optional<uint32_t>(0xB0100425u));
    for (const uint16_t op : oha::kHeldOps) CHECK(oha::op_index(op) >= 0 && oha::npc_key_of(op, ai));
    CHECK(!oha::npc_key_of(2005, ai) && !oha::npc_key_of(2111, ai) && !oha::npc_key_of(2661, ai));
    CHECK(!oha::npc_key_of(2404, hex("1f0310a0")));  // a character key: not held
    CHECK(!oha::npc_key_of(2404, hex("2504")));      // too short
    // The 2005 key parser on the session-4 fixtures (guest_s3_logic.h parse_2005 finds the same keys).
    std::vector<uint32_t> keys;
    const auto crow = load("p8s4_2005_12334.bin");
    CHECK(crow.size() == 6637 && oha::announce_keys(crow, keys) == 1 && keys == std::vector<uint32_t>{0xB01002A0});
    keys.clear();
    const auto four = load("p8s4_2005_1191.bin");
    CHECK(four.size() == 28532 && oha::announce_keys(four, keys) == 4);
    CHECK(keys == (std::vector<uint32_t>{0xB0100078, 0xB0100079, 0xB010007A, 0xB010007B}));
    // Session 6B: B0100425's own 2005 starts "15 1a 06 25 04 10 b0": record of 0x1A15 bytes, type 6.
    std::vector<uint8_t> big(2 + 0x1A15, 0);
    std::memcpy(big.data(), hex("151a06250410b0").data(), 7);
    keys.clear();
    CHECK(oha::announce_keys(big, keys) == 1 && keys == std::vector<uint32_t>{0xB0100425});
    keys.clear();
    big.pop_back();  // a record that does not fit: nothing
    CHECK(oha::announce_keys(big, keys) == 0);
    // The user record (type 0) is skipped.
    std::vector<uint8_t> user = hex("0900001e0310900000000000");
    user.resize(2 + 9);
    keys.clear();
    CHECK(oha::announce_keys(user, keys) == 0);
}

void test_hold_order() {
    oha::AckHold h;
    std::vector<std::vector<uint8_t>> spill;
    constexpr uint32_t a = 0xB0100425, b = 0xB0100426;
    CHECK(h.begin(a, 1, 1000) == oha::Begin::Started && h.begin(b, 1, 1000) == oha::Begin::Started);
    // Not held: other keys, and a held key's opcodes outside the list never reach offer (filter).
    auto other = npc(2404, 0xB0100999, 0);
    CHECK(h.offer(2404, 0xB0100999, other, 1001, spill) == oha::Offer::Pass && !other.empty());
    // Session 6B's pattern for a, interleaved with b's.
    const uint16_t ops[] = {2404, 2404, 2404, 2723};
    uint8_t m = 0;
    for (const uint16_t op : ops) {
        auto pa = npc(op, a, ++m);
        bool first = false;
        CHECK(h.offer(op, a, pa, 1010 + m, spill, &first) == oha::Offer::Held && pa.empty());
        CHECK(first == (m == 1));
        auto pb = npc(2586, b, static_cast<uint8_t>(100 + m));
        CHECK(h.offer(2586, b, pb, 1010 + m, spill) == oha::Offer::Held);
    }
    CHECK(h.keys() == 2 && h.packets() == 8 && h.holding(a) && spill.empty());
    // One 2005 announcing both: all their packets, in the original order across keys.
    std::vector<oha::Released> rel;
    const uint32_t both[] = {b, a, 0xB0100999};
    const auto out = h.announce(both, 1200, &rel);
    CHECK(out.size() == 8);
    for (size_t i = 0; i + 1 < out.size(); ++i) CHECK(out[i].seq < out[i + 1].seq);
    for (size_t i = 0; i < out.size(); ++i) {
        CHECK(out[i].key == (i % 2 == 0 ? a : b));
        CHECK(out[i].bytes[9] == (i % 2 == 0 ? i / 2 + 1 : 100 + i / 2 + 1));
    }
    CHECK(out[6].opcode == 2723 && out[0].opcode == 2404);
    CHECK(rel.size() == 2 && h.keys() == 0 && h.bytes() == 0);
    const oha::Counters& c = h.counters();
    CHECK(c.held == 8 && c.released_pkts == 8 && c.released_keys == 2 && c.per_key_max == 4);
    CHECK(c.held_by_op[static_cast<size_t>(oha::op_index(2404))] == 3 &&
          c.held_by_op[static_cast<size_t>(oha::op_index(2723))] == 1 &&
          c.held_by_op[static_cast<size_t>(oha::op_index(2586))] == 4);
    CHECK(c.wait_max_ms == 1200 - 1011);
    // After its 2005 the key passes.
    auto later = npc(2404, a, 50);
    CHECK(h.offer(2404, a, later, 1300, spill) == oha::Offer::Pass && !later.empty());
    // A 2005 for a key held with nothing yet: the hold ends, nothing to push.
    h.begin(a, 1, 1400);
    CHECK(h.announce(std::vector<uint32_t>{a}, 1401).empty() && h.counters().announced_empty == 1 && h.keys() == 0);
    // P8 review I5: `matched` says a hold ended even with nothing held (the route then queues the 2005 under
    // its lock); keys without a hold do not count.
    h.begin(a, 1, 1500);
    size_t matched = 99;
    CHECK(h.announce(std::vector<uint32_t>{a, 0xB0100999}, 1501, nullptr, &matched).empty() && matched == 1);
    CHECK(h.announce(std::vector<uint32_t>{a}, 1502, nullptr, &matched).empty() && matched == 0);
}

// P8 review I5: a key withheld again and again expires 40 s after its LAST begin, not its first: what a
// later 2005 releases is not lost early.
void test_hold_again_refreshes_expiry() {
    oha::AckHold h;
    std::vector<std::vector<uint8_t>> spill;
    constexpr uint32_t k = 0xB0100777;
    CHECK(h.begin(k, 1, 0) == oha::Begin::Started);
    CHECK(h.begin(k, 1, 30000) == oha::Begin::Again);
    auto p = npc(2404, k, 1);
    CHECK(h.offer(2404, k, p, 35000, spill) == oha::Offer::Held);
    CHECK(h.expire(h.limits().max_hold_ms) == 0 && h.keys() == 1);  // 40 s after the first begin: kept
    CHECK(h.announce(std::vector<uint32_t>{k}, 45000).size() == 1);  // its 2005 still releases it
    CHECK(h.begin(k, 1, 50000) == oha::Begin::Started);
    CHECK(h.begin(k, 1, 60000) == oha::Begin::Again);
    CHECK(h.expire(60000 + h.limits().max_hold_ms - 1) == 0 && h.expire(60000 + h.limits().max_hold_ms) == 1);
}

void test_hold_drop_orphan_expire_epoch() {
    oha::AckHold h;
    std::vector<std::vector<uint8_t>> spill;
    constexpr uint32_t a = 0xB0100001, b = 0xB0100002, c = 0xB0100003;
    h.begin(a, 1, 0);
    auto p = npc(2723, a, 1);
    h.offer(2723, a, p, 1, spill);
    // A dropped hold: its packets go, nothing is returned by a later 2005.
    CHECK(h.drop(a) == 1 && h.keys() == 0 && h.bytes() == 0);
    CHECK(h.counters().discarded_pkts[static_cast<size_t>(oha::Discard::Dropped)] == 1);
    CHECK(h.announce(std::vector<uint32_t>{a}, 2).empty());
    // Released but its 2005 never seen: orphan_ms later its packets go; a 2005 before that still flushes.
    h.begin(b, 1, 0);
    h.begin(c, 1, 0);
    auto pb = npc(2404, b, 1), pc = npc(2404, c, 1);
    h.offer(2404, b, pb, 10, spill);
    h.offer(2404, c, pc, 10, spill);
    h.released(std::vector<uint32_t>{b, c}, 100);
    CHECK(h.announce(std::vector<uint32_t>{c}, 150).size() == 1);
    std::vector<oha::AckHold::Gone> gone;
    CHECK(h.expire(100 + h.limits().orphan_ms - 1, &gone) == 0 && h.keys() == 1);
    CHECK(h.expire(100 + h.limits().orphan_ms, &gone) == 1 && gone.size() == 1 && gone[0].key == b &&
          gone[0].why == oha::Discard::Orphan && gone[0].packets == 1);
    // Held again (withheld again) after a release: holds again.
    h.begin(b, 1, 5000);
    auto again = npc(2404, b, 2);
    CHECK(h.offer(2404, b, again, 5001, spill) == oha::Offer::Held);
    h.released(std::vector<uint32_t>{b}, 5002);
    CHECK(h.begin(b, 1, 5003) == oha::Begin::Again && h.expire(5002 + h.limits().orphan_ms) == 0);
    // Expiry: a hold older than max_hold_ms, counted from its LAST begin (P8 review I5: 5003, not 5000).
    gone.clear();
    CHECK(h.expire(5000 + h.limits().max_hold_ms, &gone) == 0 && h.keys() == 1);
    CHECK(h.expire(5003 + h.limits().max_hold_ms, &gone) == 1 && gone[0].why == oha::Discard::Expired);
    // Another epoch: the old hold's packets go, a fresh one starts.
    h.begin(a, 1, 6000);
    auto pa = npc(2404, a, 3);
    h.offer(2404, a, pa, 6001, spill);
    CHECK(h.begin(a, 2, 6002) == oha::Begin::Started && h.packets() == 0);
    CHECK(h.counters().discarded_pkts[static_cast<size_t>(oha::Discard::Epoch)] == 1);
    // Everything (user 2 gone).
    auto pa2 = npc(2404, a, 4);
    h.offer(2404, a, pa2, 6003, spill);
    CHECK(h.clear(oha::Discard::UserGone) == 1 && h.keys() == 0 && h.bytes() == 0);
}

void test_hold_bounds() {
    oha::Limits l;
    l.max_keys = 2;
    l.max_packets_per_key = 3;
    l.max_bytes_per_key = 1000;
    l.max_bytes_total = 100;
    oha::AckHold h(l);
    std::vector<std::vector<uint8_t>> spill;
    CHECK(h.begin(1u | 0xB0000000, 1, 0) == oha::Begin::Started);
    CHECK(h.begin(2u | 0xB0000000, 1, 0) == oha::Begin::Started);
    CHECK(h.begin(3u | 0xB0000000, 1, 0) == oha::Begin::Full && h.counters().full == 1);
    const uint32_t k = 0xB0000001;
    // Per key: the 4th packet spills the key in order (its 3 then this one), then it passes.
    for (uint8_t i = 1; i <= 3; ++i) {
        auto p = npc(2404, k, i, 8);
        CHECK(h.offer(2404, k, p, i, spill) == oha::Offer::Held);
    }
    auto p4 = npc(2404, k, 4, 8);
    CHECK(h.offer(2404, k, p4, 4, spill) == oha::Offer::Spill && spill.size() == 4);
    for (size_t i = 0; i < spill.size(); ++i) CHECK(spill[i][9] == i + 1);
    CHECK(!h.holding(k) && h.bytes() == 0 && h.counters().spilled_keys == 1 && h.counters().spilled_pkts == 4);
    auto p5 = npc(2404, k, 5, 8);
    CHECK(h.offer(2404, k, p5, 5, spill) == oha::Offer::Pass);
    // Its 2005 closes the spilled hold (nothing to push).
    CHECK(h.announce(std::vector<uint32_t>{k}, 6).empty() && h.counters().spilled_closed == 1);
    // In all: the total bound spills too.
    spill.clear();
    const uint32_t k2 = 0xB0000002;
    auto big = npc(2723, k2, 1, 60);  // 65 bytes
    CHECK(h.offer(2723, k2, big, 7, spill) == oha::Offer::Held);
    auto big2 = npc(2723, k2, 2, 60);
    CHECK(h.offer(2723, k2, big2, 8, spill) == oha::Offer::Spill && spill.size() == 2 && h.bytes() == 0);
}

}  // namespace

int main() {
    test_book();
    test_decide();
    test_filter_and_parser();
    test_hold_order();
    test_hold_drop_orphan_expire_epoch();
    test_hold_bounds();
    test_hold_again_refreshes_expiry();
    if (g_failures) {
        std::printf("%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("optd_scene_acks: all checks passed\n");
    return 0;
}
