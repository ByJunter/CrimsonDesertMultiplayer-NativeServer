// S46 phase 1, T2 in probe (docs/research/s25/s46-tenderos.md): the pure logic of observer_hold_scene_order
// (hold_scene_order_logic.h).
//
// SCOPE: offline logic only, on packets built with the real layouts (2661 91 + 11 n, 2981 21 + 8 n, 2524 18 + 11 n,
// 2810 17, 2005 records, 2516, 2174 / 2643 / 2756 boundaries):
//  - which 2661 would be held (user 2's own, a trade hash, a bound key held), and which not;
//  - the keys read from batched 2005s, the release with its delay and the scene's opcodes in order;
//  - cancellation by 2810, a removed NPC (2516), a field boundary, the 40 s expiry, the capacity; the key.
// They do NOT test take_frame, observer_hold_acks nor the game.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

#include "mod/session2/hold_scene_order_logic.h"
#include "mod/session2/observer_hold_acks_logic.h"

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

constexpr uint32_t kOwn = 0xA0100066;   // user 2's character (p2b)
constexpr uint32_t kHost = 0xA0100001;  // the host's
constexpr uint32_t kRenee = 0xB01002EB, kRhett = 0xB01002EC, kOther = 0xB0100010;
constexpr uint32_t kButcher = 0x978345CA, kSmithy = 0xA7132561, kGrocery = 0x0E965A02;

void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) { std::memcpy(&v[at], &x, 4); }
void put64(std::vector<uint8_t>& v, size_t at, uint64_t x) { std::memcpy(&v[at], &x, 8); }

struct Bind {
    uint32_t actor;
    uint8_t type;
};
std::vector<uint8_t> p2661(uint32_t owner, uint64_t id, uint32_t hash, std::vector<Bind> binds) {
    std::vector<uint8_t> p(91 + 11 * binds.size(), 0);
    p[0] = 4;
    put32(p, 1, owner);
    put32(p, 5, owner);
    put64(p, 9, id);
    put32(p, 27, hash);
    for (size_t i = 0; i < binds.size(); ++i) {
        put32(p, 91 + 11 * i, static_cast<uint32_t>(i + 1));
        put32(p, 91 + 11 * i + 4, binds[i].actor);
        p[91 + 11 * i + 8] = binds[i].type;
    }
    return p;
}
std::vector<uint8_t> p2981(uint64_t id, size_t n = 0) {
    std::vector<uint8_t> p(21 + 8 * n, 0);
    p[0] = 4;
    put32(p, 1, kOwn);
    put64(p, 5, id);
    return p;
}
std::vector<uint8_t> p2524(uint64_t id, size_t n = 1) {
    std::vector<uint8_t> p(18 + 11 * n, 0);
    p[0] = 4;
    put32(p, 1, kOwn);
    put64(p, 5, id);
    return p;
}
std::vector<uint8_t> p2810(uint64_t id) {
    std::vector<uint8_t> p(17, 0);
    put32(p, 0, kOwn);
    put64(p, 4, id);
    p[16] = 0x0E;
    return p;
}
// A 2005 with a user record (type 0) first, then one record per key (type 1), as NotifyAppear batches them.
std::vector<uint8_t> p2005(std::vector<uint32_t> keys) {
    std::vector<uint8_t> p;
    const auto rec = [&](uint8_t type, uint32_t key) {
        const uint16_t len = 9;
        p.push_back(static_cast<uint8_t>(len));
        p.push_back(0);
        p.push_back(type);
        for (int b = 0; b < 4; ++b) p.push_back(static_cast<uint8_t>(key >> (8 * b)));
        for (int b = 0; b < 4; ++b) p.push_back(0);
    };
    rec(0, 0x90100066);
    for (const uint32_t k : keys) rec(1, k);
    return p;
}
std::vector<uint8_t> p2516(std::vector<uint32_t> keys) {
    std::vector<uint8_t> p(6 + 4 * keys.size(), 0);
    p[0] = 4;
    p[5] = 0x1E;
    for (size_t i = 0; i < keys.size(); ++i) put32(p, 6 + 4 * i, keys[i]);
    return p;
}

struct Run {
    hso::SceneOrder o;
    std::set<uint32_t> held;
    std::vector<hso::Event> ev;
    explicit Run(hso::Limits l = {}) : o(l) {}
    void feed(uint16_t op, const std::vector<uint8_t>& p, uint64_t t) {
        o.feed(op, p, t, kOwn, [&](uint32_t k) { return held.count(k) != 0; }, ev);
    }
};

void test_key() {
    CHECK(hso::parse_key("").mode == hso::Mode::Off);
    CHECK(hso::parse_key("0").mode == hso::Mode::Off);
    CHECK(hso::parse_key("probe").mode == hso::Mode::Probe && !hso::parse_key("probe").asked_on);
    CHECK(hso::parse_key("1").mode == hso::Mode::On && hso::parse_key("1").asked_on);  // S46 phase 3: T2 active
    CHECK(hso::parse_key("on").bad && hso::parse_key("on").mode == hso::Mode::Off);
}

void test_layouts() {
    const auto p = p2661(kOwn, 0x142, kButcher, {{kRenee, 4}, {kOther, 2}, {kRhett, 5}, {kRenee, 5}});
    const hso::Prepare pr = hso::parse_prepare(p);
    CHECK(pr.ok && pr.owner == kOwn && pr.id == 0x142 && pr.hash == kButcher);
    CHECK(pr.bound.size() == 2 && pr.bound[0] == kRenee && pr.bound[1] == kRhett);  // types 4/5, unique, in order
    auto bad = p;
    bad.pop_back();
    CHECK(!hso::parse_prepare(bad).ok);
    CHECK(hso::scene_id(2981, p2981(0x142)) == 0x142);
    CHECK(hso::scene_id(2981, p2981(0x142, 3)) == 0x142);
    const auto full = p2981(0x142);
    const auto truncated = std::vector<uint8_t>(full.begin(), full.begin() + 17);
    CHECK(!hso::scene_id(2981, truncated));  // a 17-byte prefix is not a 2981
    CHECK(hso::scene_id(2524, p2524(0x143)) == 0x143);
    CHECK(!hso::scene_id(2524, p2981(0x143)));
    CHECK(hso::scene_id(2810, p2810(0x2DE)) == 0x2DE);
    CHECK(!hso::scene_id(2661, p));
    std::vector<uint32_t> keys;
    hso::announce_keys(p2005({kRenee, kRhett}), keys);
    CHECK(keys.size() == 2 && keys[0] == kRenee && keys[1] == kRhett);  // the user record is skipped
    keys.clear();
    hso::removed_keys(p2516({kRhett}), keys);
    CHECK(keys.size() == 1 && keys[0] == kRhett);
    // Boundaries: 2174 17 + 48 n, 2643 20 + 48 n with append 0, 2756.
    std::vector<uint8_t> b2174(17 + 48, 0);
    put32(b2174, 1, kOwn);
    CHECK(hso::field_boundary(2174, b2174, kOwn));
    CHECK(!hso::field_boundary(2174, b2174, kHost));
    std::vector<uint8_t> b2643(20, 0);
    put32(b2643, 1, kOwn);
    CHECK(hso::field_boundary(2643, b2643, kOwn));
    b2643[19] = 1;
    CHECK(!hso::field_boundary(2643, b2643, kOwn));  // an append: not a new field
    std::vector<uint8_t> b2756(8, 0);
    put32(b2756, 0, kOwn);
    CHECK(hso::field_boundary(2756, b2756, kOwn));
    CHECK(!hso::field_boundary(2756, b2756, 0));
}

void test_p2b_far() {
    // p2b far: user 2's butcher 0x142 binds Renee (type 4) whose 2005 observer_hold holds ~344 ms; then the smithy
    // 0x143 binds Rhett (~375 ms). Both 2661s went at once (0x3B331F28 on the guest). T2 would have held them.
    Run r;
    r.held = {kRenee, kRhett};
    const uint64_t t0 = 1'000'000;
    r.feed(2661, p2661(kOwn, 0x142, kButcher, {{kRenee, 4}}), t0);
    r.feed(2661, p2661(kOwn, 0x143, kSmithy, {{kRhett, 5}}), t0 + 1000);
    CHECK(r.ev.size() == 2 && r.ev[0].kind == hso::Kind::WouldHold && r.ev[0].keys.size() == 1 &&
          r.ev[0].keys[0] == kRenee && r.ev[1].id == 0x143);
    CHECK(r.o.size() == 2);
    r.feed(2981, p2981(0x142), t0 + 5'000);
    r.feed(2524, p2524(0x142), t0 + 5'100);
    r.feed(2981, p2981(0x999), t0 + 5'200);  // another scene: not counted
    r.feed(2981, p2981(0x143), t0 + 6'000);
    // A batched 2005 announcing another NPC and Renee: 0x142 released after 344 ms with its opcodes in order.
    r.ev.clear();
    r.feed(2005, p2005({kOther, kRenee}), t0 + 344'000);
    CHECK(r.ev.size() == 1 && r.ev[0].kind == hso::Kind::Released && r.ev[0].id == 0x142);
    CHECK(r.ev[0].delay_us == 344'000);
    CHECK((r.ev[0].ops == std::vector<uint16_t>{2661, 2981, 2524}));
    CHECK(r.o.size() == 1);
    r.ev.clear();
    r.feed(2005, p2005({kRhett}), t0 + 1000 + 375'000);
    CHECK(r.ev.size() == 1 && r.ev[0].kind == hso::Kind::Released && r.ev[0].delay_us == 375'000 &&
          (r.ev[0].ops == std::vector<uint16_t>{2661, 2981}));
    CHECK(r.o.size() == 0);
}

void test_not_held() {
    Run r;
    r.held = {kRenee};
    // The host's own scene (owner = the host): not T2's.
    r.feed(2661, p2661(kHost, 0x2A2, kButcher, {{kRenee, 4}}), 1);
    // Not a trade scene.
    r.feed(2661, p2661(kOwn, 0x150, kGrocery, {{kRenee, 4}}), 2);
    CHECK(r.ev.empty() && r.o.size() == 0);
    // User 2's own trade scene with nothing held: it goes now (NoHold, nothing watched).
    r.feed(2661, p2661(kOwn, 0x151, kSmithy, {{kRhett, 5}, {kOther, 2}}), 3);
    CHECK(r.ev.size() == 1 && r.ev[0].kind == hso::Kind::NoHold && r.ev[0].keys.size() == 1 && r.o.size() == 0);
    // Types other than 4/5 are not waited for.
    r.ev.clear();
    r.held = {kOther};
    r.feed(2661, p2661(kOwn, 0x152, kSmithy, {{kOther, 2}}), 4);
    CHECK(r.ev.size() == 1 && r.ev[0].kind == hso::Kind::NoHold);
    // Without user 2's key nothing is T2's.
    hso::SceneOrder o;
    std::vector<hso::Event> ev;
    o.feed(2661, p2661(kOwn, 0x153, kSmithy, {{kRenee, 4}}), 5, 0, [](uint32_t) { return true; }, ev);
    CHECK(ev.empty());
}

void test_endings() {
    // 2810 while held: cancelled with what it held.
    Run r;
    r.held = {kRenee, kRhett};
    r.feed(2661, p2661(kOwn, 0x142, kButcher, {{kRenee, 4}, {kRhett, 5}}), 0);
    r.feed(2005, p2005({kRenee}), 100);  // one of two: still held
    CHECK(r.o.size() == 1);
    r.feed(2981, p2981(0x142), 200);
    r.ev.clear();
    r.feed(2810, p2810(0x142), 300);
    CHECK(r.ev.size() == 1 && r.ev[0].kind == hso::Kind::Cancelled && r.ev[0].keys.size() == 1 &&
          r.ev[0].keys[0] == kRhett && (r.ev[0].ops == std::vector<uint16_t>{2661, 2981, 2810}) && r.o.size() == 0);
    // A pending NPC removed (2516): dropped, with the key.
    r.ev.clear();
    r.feed(2661, p2661(kOwn, 0x143, kSmithy, {{kRhett, 5}}), 1000);
    r.feed(2516, p2516({kOther, kRhett}), 2000);
    CHECK(r.ev.size() == 2 && r.ev[1].kind == hso::Kind::Dropped && r.ev[1].keys.size() == 1 &&
          r.ev[1].keys[0] == kRhett && r.o.size() == 0);
    // A field boundary of user 2: every watch dropped, no key.
    r.ev.clear();
    r.feed(2661, p2661(kOwn, 0x144, kSmithy, {{kRhett, 5}}), 3000);
    std::vector<uint8_t> b(17, 0);
    put32(b, 1, kOwn);
    r.feed(2174, b, 4000);
    CHECK(r.ev.size() == 2 && r.ev[1].kind == hso::Kind::Dropped && r.ev[1].keys.empty() && r.o.size() == 0);
    // 40 s without the 2005: expired.
    r.ev.clear();
    r.feed(2661, p2661(kOwn, 0x145, kSmithy, {{kRhett, 5}}), 10'000);
    r.o.tick(10'000 + 39'999'999, r.ev);
    CHECK(r.o.size() == 1);
    r.o.tick(10'000 + 40'000'000, r.ev);
    CHECK(r.ev.size() == 2 && r.ev[1].kind == hso::Kind::Expired && r.o.size() == 0);
    // The same id again while held: one watch.
    r.ev.clear();
    r.feed(2661, p2661(kOwn, 0x146, kSmithy, {{kRhett, 5}}), 1);
    r.feed(2661, p2661(kOwn, 0x146, kSmithy, {{kRhett, 5}}), 2);
    CHECK(r.ev.size() == 1 && r.o.size() == 1);
    r.o.clear();
    CHECK(r.o.size() == 0);
}

void test_bounds() {
    hso::Limits l;
    l.max_scenes = 1;
    l.max_ops = 2;
    Run r(l);
    r.held = {kRenee, kRhett};
    r.feed(2661, p2661(kOwn, 0x142, kButcher, {{kRenee, 4}}), 0);
    r.feed(2661, p2661(kOwn, 0x143, kSmithy, {{kRhett, 5}}), 1);
    CHECK(r.ev.size() == 2 && r.ev[1].kind == hso::Kind::Capacity && r.o.size() == 1);
    r.feed(2981, p2981(0x142), 2);
    r.feed(2981, p2981(0x142), 3);
    r.feed(2524, p2524(0x142), 4);
    r.ev.clear();
    r.feed(2005, p2005({kRenee}), 5);
    CHECK(r.ev.size() == 1 && r.ev[0].ops.size() == 2 && r.ev[0].more_ops == 2);
    for (int i = 0; i < static_cast<int>(hso::Kind::Count); ++i) {
        CHECK(std::strlen(hso::kind_name(static_cast<hso::Kind>(i))) > 0);
    }
}

// ---------------------------------------------------------------- S46 phase 3: T2 ACTIVE (hso::SceneHold)

// A whole packet as take_frame has it: u16 opcode | payload (the header's other bytes do not matter to SceneHold).
std::vector<uint8_t> wire(uint16_t op, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> w(2);
    w[0] = static_cast<uint8_t>(op);
    w[1] = static_cast<uint8_t>(op >> 8);
    w.insert(w.end(), payload.begin(), payload.end());
    return w;
}
std::vector<uint8_t> p2111(uint64_t id) {
    std::vector<uint8_t> p(22, 0);
    p[0] = 4;
    put32(p, 1, kOwn);
    put64(p, 5, id);
    return p;
}
uint16_t op_of(const std::vector<uint8_t>& w) { return static_cast<uint16_t>(w[0] | w[1] << 8); }

// observer_hold_acks' table and T2 side by side, as the route uses them (one lock, one sequence).
struct Active {
    oha::AckHold acks;
    hso::SceneHold t2;
    std::vector<hso::Event> ev;
    std::vector<std::vector<uint8_t>> queue;  // what reached user 2's queue, in order
    uint64_t now = 1'000'000;
    explicit Active(hso::HoldLimits l = {}) : t2(l) {}
    // A packet to user 2 through the route: oha's per-actor ops, T2's ops, 2005s; returns what happened to it.
    hso::Take send(uint16_t op, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> packet = wire(op, payload);
        const std::span<const uint8_t> pl(payload);
        if (op == hso::kOpAnnounce) {
            std::vector<uint32_t> keys;
            oha::announce_keys(pl, keys);
            size_t matched = 0;
            std::vector<oha::HeldPacket> after = acks.announce(keys, now / 1000, nullptr, &matched);
            if (matched) {
                std::vector<hso::OutPacket> scene;
                t2.announced(keys, now, scene, ev);
                hso::merge_release(after, scene);
            }
            queue.push_back(packet);
            for (oha::HeldPacket& h : after) queue.push_back(std::move(h.bytes));
            return hso::Take::Pass;
        }
        if (hso::t2_op(op)) {
            const hso::Take tk = t2.offer(op, pl, packet, now, kOwn, [&](uint32_t k) { return acks.withheld(k); },
                                          [&] { return acks.next_seq(); }, ev);
            if (tk == hso::Take::Pass) queue.push_back(packet);
            return tk;
        }
        if (const auto key = oha::held_key_of(op, pl)) {
            std::vector<std::vector<uint8_t>> spill;
            const oha::Offer o = acks.offer(op, *key, packet, now / 1000, spill);
            if (o == oha::Offer::Held) return hso::Take::Held;
            if (o == oha::Offer::Spill) {  // its held packets then this one, at once (the route's Pushed)
                for (auto& s : spill) queue.push_back(std::move(s));
                return hso::Take::Pass;
            }
        }
        queue.push_back(packet);
        return hso::Take::Pass;
    }
    std::vector<uint16_t> ops() const {
        std::vector<uint16_t> o;
        for (const auto& w : queue) o.push_back(op_of(w));
        return o;
    }
    size_t count(hso::Kind k) const {
        size_t n = 0;
        for (const auto& e : ev) n += e.kind == k;
        return n;
    }
};
std::vector<uint8_t> actor_op(uint32_t key) {  // a per-actor S>C packet oha holds (u32 key @0)
    std::vector<uint8_t> p(12, 0);
    put32(p, 0, key);
    return p;
}

void test_t2_hold_release_in_order() {
    // p2b far, active: Renee withheld; user 2's butcher 0x142 and what follows it wait for her 2005, then go right
    // after it, merged with observer_hold_acks' own held packets by their original order.
    Active a;
    CHECK(a.acks.begin(kRenee, 1, 0) == oha::Begin::Started);
    CHECK(a.send(2723, actor_op(kRenee)) == hso::Take::Held);                                   // oha's (seq 1)
    CHECK(a.send(2661, p2661(kOwn, 0x142, kButcher, {{kRenee, 4}})) == hso::Take::Held);        // T2 (seq 2)
    CHECK(a.send(2404, actor_op(kRenee)) == hso::Take::Held);                                   // oha's (seq 3)
    CHECK(a.send(2981, p2981(0x142)) == hso::Take::Held);                                       // T2 (seq 4)
    CHECK(a.send(2111, p2111(0x142)) == hso::Take::Held);                                       // T2 (seq 5)
    CHECK(a.send(2524, p2524(0x142)) == hso::Take::Held);                                       // T2 (seq 6)
    CHECK(a.send(2981, p2981(0x999)) == hso::Take::Pass);                                       // another id
    CHECK(a.t2.scenes() == 1 && a.t2.kept(0x142) && a.count(hso::Kind::WouldHold) == 1);
    CHECK((a.ops() == std::vector<uint16_t>{2981}));
    a.now += 344'000;
    a.send(2005, p2005({kOther, kRenee}));  // batched: another NPC first
    CHECK((a.ops() == std::vector<uint16_t>{2981, 2005, 2723, 2661, 2404, 2981, 2111, 2524}));
    CHECK(a.t2.scenes() == 0 && a.count(hso::Kind::Released) == 1);
    const hso::Event& r = a.ev.back();
    CHECK(r.active && r.kind == hso::Kind::Released && r.id == 0x142 && r.delay_us == 344'000);
    CHECK((r.ops == std::vector<uint16_t>{2661, 2981, 2111, 2524}));
    CHECK(a.t2.counters().released == 1 && a.t2.counters().packets_released == 4);
    // After the release the id is free: its later packets pass as usual.
    CHECK(a.send(2981, p2981(0x142)) == hso::Take::Pass);
    CHECK(a.send(2810, p2810(0x142)) == hso::Take::Pass);
}

void test_t2_two_keys_and_compacted_2005() {
    // A scene binding two held NPCs goes with the 2005 of the LAST of them; a 2005 of one only keeps it.
    Active a;
    a.acks.begin(kRenee, 1, 0);
    a.acks.begin(kRhett, 1, 0);
    CHECK(a.send(2661, p2661(kOwn, 0x143, kSmithy, {{kRenee, 4}, {kRhett, 5}})) == hso::Take::Held);
    a.send(2005, p2005({kRhett}));
    CHECK(a.t2.scenes() == 1 && (a.ops() == std::vector<uint16_t>{2005}));
    a.send(2981, p2981(0x143));
    a.send(2005, p2005({kRenee}));
    CHECK(a.t2.scenes() == 0 && (a.ops() == std::vector<uint16_t>{2005, 2005, 2661, 2981}));
    // A 2005 whose records were compacted to the user record only (a retired key filtered out) releases nothing.
    a.queue.clear();
    a.acks.begin(kRhett, 1, 0);
    a.send(2661, p2661(kOwn, 0x144, kSmithy, {{kRhett, 5}}));
    a.send(2005, p2005({}));
    CHECK(a.t2.scenes() == 1 && (a.ops() == std::vector<uint16_t>{2005}));
}

void test_t2_not_kept() {
    Active a;
    a.acks.begin(kRenee, 1, 0);
    // The host's own scene, a non-trade scene, a bound key not held: all pass at once.
    CHECK(a.send(2661, p2661(kHost, 0x2A2, kButcher, {{kRenee, 4}})) == hso::Take::Pass);
    CHECK(a.send(2661, p2661(kOwn, 0x150, kGrocery, {{kRenee, 4}})) == hso::Take::Pass);
    CHECK(a.send(2661, p2661(kOwn, 0x151, kSmithy, {{kRhett, 5}})) == hso::Take::Pass);
    CHECK(a.send(2661, p2661(kOwn, 0x152, kSmithy, {{kRenee, 2}})) == hso::Take::Pass);  // type 2: not waited for
    CHECK(a.t2.scenes() == 0 && a.count(hso::Kind::NoHold) == 2 && a.queue.size() == 4);
    // A spilled key is not held (its packets pass natively): not waited for either.
    hso::SceneHold t2;
    std::vector<hso::Event> ev;
    std::vector<uint8_t> pk = wire(2661, p2661(kOwn, 0x153, kSmithy, {{kRenee, 4}}));
    const auto pl = p2661(kOwn, 0x153, kSmithy, {{kRenee, 4}});
    CHECK(t2.offer(2661, pl, pk, 0, 0, [](uint32_t) { return true; }, [] { return uint64_t{1}; }, ev) ==
          hso::Take::Pass);  // without user 2's key nothing is T2's
}

void test_t2_2810_discards() {
    // A 2810 of a kept scene: the scene, the 2810 and what it kept are dropped; no tombstone (the id is over).
    Active a;
    a.acks.begin(kRhett, 1, 0);
    a.send(2661, p2661(kOwn, 0x143, kSmithy, {{kRhett, 5}}));
    a.send(2981, p2981(0x143));
    CHECK(a.send(2810, p2810(0x143)) == hso::Take::Discarded);
    CHECK(a.t2.scenes() == 0 && a.t2.tombs() == 0 && a.count(hso::Kind::Cancelled) == 1 && a.queue.empty());
    CHECK((a.ev.back().ops == std::vector<uint16_t>{2661, 2981, 2810}));
    a.send(2005, p2005({kRhett}));
    CHECK((a.ops() == std::vector<uint16_t>{2005}));  // nothing of the scene follows its 2005
    CHECK(a.send(2981, p2981(0x143)) == hso::Take::Pass);
}

void test_t2_cap() {
    // Scenes cap: the 2661 over it is REFUSED and dropped with its packets (tombstoned until its 2810).
    hso::HoldLimits l;
    l.max_scenes = 1;
    l.max_packets = 3;
    Active a(l);
    a.acks.begin(kRenee, 1, 0);
    a.acks.begin(kRhett, 1, 0);
    CHECK(a.send(2661, p2661(kOwn, 0x142, kButcher, {{kRenee, 4}})) == hso::Take::Held);
    CHECK(a.send(2661, p2661(kOwn, 0x143, kSmithy, {{kRhett, 5}})) == hso::Take::Discarded);
    CHECK(a.count(hso::Kind::Capacity) == 1 && a.t2.tombs() == 1 && a.t2.scenes() == 1);
    CHECK(a.send(2981, p2981(0x143)) == hso::Take::Discarded);   // its later packets too
    CHECK(a.send(2810, p2810(0x143)) == hso::Take::Discarded);   // its 2810 ends the tombstone
    CHECK(a.t2.tombs() == 0 && a.send(2981, p2981(0x143)) == hso::Take::Pass);
    // Packets per scene: the one over the cap discards the scene with everything it kept.
    CHECK(a.send(2981, p2981(0x142)) == hso::Take::Held);
    CHECK(a.send(2524, p2524(0x142)) == hso::Take::Held);       // 3 kept: 2661, 2981, 2524
    CHECK(a.send(2111, p2111(0x142)) == hso::Take::Discarded);  // the 4th: refused
    CHECK(a.t2.scenes() == 0 && a.count(hso::Kind::Capacity) == 2 && a.t2.tombs() == 1);
    a.send(2005, p2005({kRenee}));
    CHECK((a.ops() == std::vector<uint16_t>{2981, 2005}));  // nothing of 0x142 is released
    CHECK(a.t2.counters().packets_discarded == 5 && a.t2.counters().tomb_dropped == 2);
    // Bytes in all.
    hso::HoldLimits lb;
    lb.max_bytes_total = 200;
    Active b(lb);
    b.acks.begin(kRenee, 1, 0);
    CHECK(b.send(2661, p2661(kOwn, 0x160, kButcher, {{kRenee, 4}})) == hso::Take::Held);  // 2 + 102 B
    CHECK(b.send(2661, p2661(kOwn, 0x161, kButcher, {{kRenee, 4}})) == hso::Take::Discarded);
}

void test_t2_other_ends() {
    // The hold of a pending key ends without its 2005 (oha drop / expiry / retirement): discarded, tombstoned.
    Active a;
    a.acks.begin(kRenee, 1, 0);
    a.send(2661, p2661(kOwn, 0x142, kButcher, {{kRenee, 4}}));
    a.acks.drop(kRenee);
    a.t2.key_gone(kRenee, a.now, a.ev);
    CHECK(a.t2.scenes() == 0 && a.t2.tombs() == 1 && a.count(hso::Kind::KeyGone) == 1);
    CHECK(a.ev.back().keys.size() == 1 && a.ev.back().keys[0] == kRenee);
    CHECK(a.send(2524, p2524(0x142)) == hso::Take::Discarded);
    // A 2516 of a pending key: discarded (the 2516 itself passes).
    a.acks.begin(kRhett, 1, 0);
    a.send(2661, p2661(kOwn, 0x143, kSmithy, {{kRhett, 5}}));
    CHECK(a.send(2516, p2516({kOther, kRhett})) == hso::Take::Pass);
    CHECK(a.t2.scenes() == 0 && a.count(hso::Kind::Dropped) == 1 && a.t2.tombs() == 2);
    CHECK(a.ev.back().keys.size() == 1 && a.ev.back().keys[0] == kRhett);  // the 2516 names the NPC
    // A field boundary of user 2: everything goes, the tombstones too (the boundary passes).
    a.acks.begin(kOther, 1, 0);
    a.send(2661, p2661(kOwn, 0x144, kSmithy, {{kOther, 4}}));
    std::vector<uint8_t> b(17, 0);
    put32(b, 1, kOwn);
    CHECK(a.send(2174, b) == hso::Take::Pass);
    CHECK(a.t2.scenes() == 0 && a.t2.tombs() == 0 && a.count(hso::Kind::Dropped) == 2);
    CHECK(a.ev.back().keys.empty());  // review B3: a field change, not a 2516 (no key named)
    // 100 s without the 2005: expired and tombstoned; the tombstone ages out after 120 s.
    a.acks.begin(kRenee, 1, 0);
    a.send(2661, p2661(kOwn, 0x145, kSmithy, {{kRenee, 4}}));
    a.t2.tick(a.now + 99'999'999, a.ev);
    CHECK(a.t2.scenes() == 1);
    a.t2.tick(a.now + 100'000'000, a.ev);
    CHECK(a.t2.scenes() == 0 && a.t2.tombs() == 1 && a.count(hso::Kind::Expired) == 1);
    a.t2.tick(a.now + 220'000'000, a.ev);
    CHECK(a.t2.tombs() == 0);
    // User 2 gone: cleared.
    a.send(2661, p2661(kOwn, 0x146, kSmithy, {{kRenee, 4}}));
    a.t2.clear(a.now, a.ev);
    CHECK(a.t2.live() == 0 && a.t2.bytes() == 0);
    for (int i = 0; i < static_cast<int>(hso::Kind::Count); ++i) {
        CHECK(std::strlen(hso::kind_name(static_cast<hso::Kind>(i))) > 0);
    }
    CHECK(hso::scene_id_t2(2111, p2111(0x115)) == 0x115 && !hso::scene_id_t2(2111, p2981(0x115)));
    CHECK(!hso::scene_id(2111, p2111(0x115)));  // the probe keeps its own list
}

// S46 phase 3 review (revision-f3.md, its sim.cpp cases 1 to 4) and the fixes M1, B2, B4.
void test_t2_review() {
    // Case 1 (M1): an Orphan key (its NotifyAppear returned, the 2005 not seen: e.g. inside a 2042) is NOT pending: the
    // 2661 passes ([no hold]); before the fix it was kept and then discarded 2 s later when the Orphan expired.
    {
        Active a;
        a.acks.begin(kRhett, 1, 0);
        const uint32_t k = kRhett;
        a.acks.released(std::span<const uint32_t>(&k, 1), 0);
        CHECK(!a.acks.withheld(kRhett) && a.acks.holding(kRhett));
        CHECK(a.send(2661, p2661(kOwn, 0x143, kSmithy, {{kRhett, 4}})) == hso::Take::Pass);
        CHECK(a.t2.scenes() == 0 && a.count(hso::Kind::NoHold) == 1);
        std::vector<oha::AckHold::Gone> gone;
        a.acks.expire(2001, &gone);
        for (const auto& g : gone) a.t2.key_gone(g.key, a.now, a.ev, static_cast<uint8_t>(g.why));
        CHECK(gone.size() == 1 && a.count(hso::Kind::KeyGone) == 0 && (a.ops() == std::vector<uint16_t>{2661}));
    }
    // Case 2 (M1): a Spilled key (over 32 packets, its 2005 still to come) IS pending: the 2661 is kept and goes right
    // after the 2005 (before the fix it passed at once: the original 0x3B331F28).
    {
        Active a;
        a.acks.begin(kRhett, 1, 0);
        hso::Take last = hso::Take::Held;
        for (int i = 0; i < 33 && last == hso::Take::Held; ++i) last = a.send(2404, actor_op(kRhett));
        CHECK(last == hso::Take::Pass && !a.acks.holding(kRhett) && a.acks.withheld(kRhett));  // spilled
        const size_t spilled = a.queue.size();
        CHECK(spilled == 33);
        CHECK(a.send(2661, p2661(kOwn, 0x143, kSmithy, {{kRhett, 4}})) == hso::Take::Held);
        a.send(2005, p2005({kRhett}));
        CHECK(a.queue.size() == spilled + 2 && op_of(a.queue[spilled]) == 2005 && op_of(a.queue[spilled + 1]) == 2661);
        CHECK(a.count(hso::Kind::Released) == 1);
    }
    // Case 3 (B2): a 2810 of 18 B with the kept scene's id discards it (before: it passed and the 2005 released a scene
    // the server had deleted).
    {
        Active a;
        a.acks.begin(kRhett, 1, 0);
        a.send(2661, p2661(kOwn, 0x143, kSmithy, {{kRhett, 4}}));
        auto d = p2810(0x143);
        d.push_back(0);
        CHECK(a.send(2810, d) == hso::Take::Discarded && a.t2.scenes() == 0 && a.t2.counters().odd_2810 == 1);
        a.send(2005, p2005({kRhett}));
        CHECK((a.ops() == std::vector<uint16_t>{2005}));
        std::vector<uint8_t> shortd(11, 0);
        CHECK(!hso::scene_id_t2(2810, shortd) && hso::scene_id_t2(2810, d) == 0x143);
        CHECK(!hso::scene_id(2810, d));  // the probe keeps its 17 B rule
    }
    // Case 4 (B4): after a KeyGone tombstone, a 2661 of the same id whose NPC is on the client now passes (decided
    // again); one whose NPC is withheld again is kept again.
    {
        Active a;
        a.acks.begin(kRhett, 1, 0);
        a.send(2661, p2661(kOwn, 0x143, kSmithy, {{kRhett, 4}}));
        a.acks.drop(kRhett);
        a.t2.key_gone(kRhett, a.now, a.ev, static_cast<uint8_t>(oha::Discard::Dropped));
        CHECK(a.t2.tombs() == 1 && a.ev.back().kind == hso::Kind::KeyGone &&
              a.ev.back().reason == static_cast<uint8_t>(oha::Discard::Dropped));
        a.acks.begin(kRhett, 1, 6);
        a.send(2005, p2005({kRhett}));  // the NPC comes back and its 2005 goes
        CHECK(a.send(2661, p2661(kOwn, 0x143, kSmithy, {{kRhett, 4}})) == hso::Take::Pass);
        CHECK(a.t2.tombs() == 0 && a.t2.counters().tomb_reopened == 1);
        a.send(2661, p2661(kOwn, 0x144, kSmithy, {{kRenee, 4}}));  // nothing withheld: passes
        a.acks.begin(kRenee, 1, 7);
        a.t2.key_gone(kRenee, a.now, a.ev);  // no scene waits for it: nothing
        CHECK(a.t2.tombs() == 0);
        // Withheld again: kept again under the same id.
        Active b;
        b.acks.begin(kRhett, 1, 0);
        b.send(2661, p2661(kOwn, 0x150, kSmithy, {{kRhett, 4}}));
        b.t2.key_gone(kRhett, b.now, b.ev);
        CHECK(b.t2.tombs() == 1 && b.ev.back().reason == 0xFF);
        CHECK(b.send(2661, p2661(kOwn, 0x150, kSmithy, {{kRhett, 4}})) == hso::Take::Held && b.t2.tombs() == 0);
    }
}

}  // namespace

int main() {
    test_key();
    test_layouts();
    test_p2b_far();
    test_not_held();
    test_endings();
    test_bounds();
    test_t2_hold_release_in_order();
    test_t2_two_keys_and_compacted_2005();
    test_t2_not_kept();
    test_t2_2810_discards();
    test_t2_cap();
    test_t2_other_ends();
    test_t2_review();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_hold_scene_order: all passed\n");
    return 0;
}
