// P8 session 8 pets (docs/research/pendientes/p8-s8-mascotas.md; p8-impl-mascotas.md):
//  - child_settle_ms (child_hold.cpp, child_settle_logic.h): its pure rules (the announcement log, the
//    split's need, the release steps) and, through child_hold.h's test seams (a fake NotifyAppear, SAM,
//    clock, realm and thread id), the hooks end to end: the host's pet in the same NotifyAppear as the
//    host's character (session 8's 2005) is withheld from user 2 and re-announced natively 1500 ms after
//    the character; a character announced just before; announced long ago (passes); off (unchanged);
//    the owner's own session; type filter; drops (out of range, user 2 leaving); D-53's release waiting
//    for the same settle; a character still withheld by character_hold; observer_hold_acks keeping the
//    pet's per-actor packets until its own 2005;
//  - remote_interp_logic.h: the cap for owned actors and the gap rule while attached;
//  - attach_follow_logic.h: the StartAction layout, the quaternion maths, the tracker (attach, measures,
//    the 1:3 Makes, detach by action / own Move / dismount hash / staleness, not a user's child, a Path
//    local that is not an offset), the synthetic 2697 and 2556 byte by byte, the registry.
// The hooks themselves need the game.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "mod/session2/attach_follow_logic.h"
#include "mod/session2/child_hold.h"
#include "mod/session2/child_settle_logic.h"
#include "mod/session2/guest_lifecycle.h"
#include "mod/session2/observer_hold_acks.h"
#include "mod/session2/remote_interp_logic.h"

namespace cdmp::session2 {
// The real ones are in server.cpp (need the game).
uintptr_t t_host = 0, t_user2 = 0;
std::vector<std::pair<uintptr_t, uintptr_t>> t_sessions;  // session -> its user
bool t_leaving = false;
uint32_t t_epoch = 1;
uintptr_t t_live_s2 = 0;
bool server_users(uintptr_t& host, uintptr_t& user2) {
    host = t_host;
    user2 = t_user2;
    return t_host && t_user2;
}
bool server_host_user(uintptr_t& host) {
    host = t_host;
    return t_host != 0;
}
bool server_user2(uintptr_t& user2) {
    user2 = t_user2;
    return t_user2 != 0;
}
// P8 U-e (the 6c12c82 crash review): child_hold.cpp asks user 2's key and user2_stages (stubs).
uint32_t server_user2_user_key() { return 0; }
bool user2_stages_wants_start_action() noexcept { return false; }
void user2_stages_start_action_done(uint32_t, int32_t) noexcept {}
void user2_stages_npc_family_observed(uint32_t, uint32_t, uint32_t, uint8_t) noexcept {}
void user2_stages_npc_late_family(uint32_t, uint32_t, uint32_t) noexcept {}
uint64_t user2_stages_npc_birth_token(uint32_t) noexcept { return 100; }
bool server_prune_retired_outgoing() noexcept { return true; }
bool server_session_user(uintptr_t session, uintptr_t& user) {
    for (const auto& [s, u] : t_sessions) {
        if (s == session) {
            user = u;
            return true;
        }
    }
    return false;
}
uint32_t server_thread_id() { return 0; }
uint32_t server_epoch() { return t_epoch; }
bool server_logout_handed() { return t_leaving; }
uintptr_t server_live_session2() { return t_live_s2; }
}  // namespace cdmp::session2

using namespace cdmp::session2;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

template <class T>
void put(uint8_t* base, uint32_t off, T v) {
    std::memcpy(base + off, &v, sizeof v);
}
uintptr_t addr(const void* p) { return reinterpret_cast<uintptr_t>(p); }

struct Blob {
    std::vector<uint8_t> b;
    explicit Blob(size_t n) : b(n, 0) {}
    uint8_t* p() { return b.data(); }
    uintptr_t a() { return addr(b.data()); }
};

constexpr uintptr_t kUserVt = 0x1405B257A8;
constexpr uint32_t kHostKey = 0x90100000, kUser2Key = 0x90100332;
constexpr uint32_t kHostChar = 0xA0100001, kUser2Char = 0xA0100333;
constexpr uint32_t kHostPet = 0xB0100002, kUser2Pet = 0xB0100334, kGimmick = 0xB010006C;

struct FakeUser : Blob {
    explicit FakeUser(uint32_t key, uintptr_t vt = kUserVt) : Blob(0x220) {
        put(p(), 0, vt);
        put(p(), hold::kUserKey, key);
    }
    void set_main(uintptr_t actor) { put(p(), hold::kUserCharacter, actor); }
};

struct FakeSector : Blob {
    std::vector<uint8_t> obs = std::vector<uint8_t>(hold::kObserverStride * 8, 0);
    FakeSector() : Blob(0x100) { set({}); }
    void set(std::vector<uintptr_t> users) {
        for (size_t i = 0; i < users.size(); ++i) put(obs.data(), static_cast<uint32_t>(i * hold::kObserverStride), users[i]);
        put(p(), hold::kSectorObservers, addr(obs.data()));
        put(p(), hold::kSectorObserverCount, static_cast<uint32_t>(users.size()));
    }
};

struct FakeActor : Blob {
    Blob comps{0x200}, cc{0x100}, restore{0x40}, ts{0x600}, info{0x10};
    FakeActor(uint32_t key, uintptr_t owner, uint32_t controller, uint8_t type, uint8_t restore_byte) : Blob(0x200) {
        put(p(), hold::kActorKey, key);
        put(p(), hold::kActorComponents, comps.a());
        put(p(), hold::kActorInfo, info.a());
        put(p(), hold::kActorOwner, owner);
        put(p(), hold::kActorController, controller);
        put(info.p(), hold::kInfoType, type);
        put(comps.p(), hold::kComponentsCc, cc.a());
        put(comps.p(), hold::kComponentsTs, ts.a());
        put(cc.p(), hold::kCcActor, a());
        put(cc.p(), hold::kCcRestore, restore.a());
        put(restore.p(), hold::kRestoreByte, restore_byte);
        put(ts.p(), hold::kTsActor, a());
    }
    void set_restore(uint8_t v) { put(restore.p(), hold::kRestoreByte, v); }
    void set_owner(uintptr_t v) { put(p(), hold::kActorOwner, v); }
    void set_sector(FakeSector* s) { put(ts.p(), hold::kTsSector, s ? s->a() : uintptr_t{0}); }
};

struct FakeSam : Blob {
    static constexpr uint32_t kBuckets = 4;
    Blob buckets{kBuckets * c4::kSamBucketBytes};
    std::vector<uintptr_t> holders = std::vector<uintptr_t>(64, 0);
    std::vector<std::unique_ptr<Blob>> holder_blobs;
    uint32_t size = 0;
    FakeSam() : Blob(0x100) {
        put(p(), c4::kSamMapBuckets, kBuckets);
        put(p(), c4::kSamMapBucketBase, buckets.a());
        put(p(), c4::kSamMapHolders, addr(holders.data()));
    }
    uint32_t add(FakeActor& actor) {
        uint32_t key = 0;
        std::memcpy(&key, actor.p() + hold::kActorKey, 4);
        const uint32_t idx = key & c4::kSamKeyIndexMask;
        uint8_t* bucket = buckets.p() + (idx % kBuckets) * c4::kSamBucketBytes;
        uint32_t n = 0;
        std::memcpy(&n, bucket, 4);
        const auto slot = static_cast<uint32_t>(holder_blobs.size());
        put(bucket, 8 + 8 * n, idx);
        put(bucket, 0xC + 8 * n, slot);
        put(bucket, 0, n + 1);
        auto h = std::make_unique<Blob>(0x10);
        put(h->p(), 4, idx);
        put(h->p(), 8, actor.a());
        holders[slot] = h->a();
        holder_blobs.push_back(std::move(h));
        put(p(), c4::kSamMapSize, ++size);
        return slot;
    }
};

struct NotifyCall {
    std::vector<uintptr_t> sessions;
    std::vector<uint32_t> keys;
};
std::vector<NotifyCall> g_native;
uint64_t g_now = 1000;
uint32_t g_tid = 7;

void fake_notify(uintptr_t, const GameVec* s, const GameVec* k) {
    NotifyCall c;
    const auto* sd = static_cast<const uintptr_t*>(s->data);
    const auto* kd = static_cast<const uint32_t*>(k->data);
    c.sessions.assign(sd, sd + s->count);
    c.keys.assign(kd, kd + k->count);
    g_native.push_back(std::move(c));
}
uint64_t fake_now() { return g_now; }
uint32_t fake_tid() { return g_tid; }

struct FakeReq {
    Blob wrapper{0x40}, raw{0x40};
    explicit FakeReq(uint32_t key, uint16_t len = 0x2D5) {
        put(raw.p(), 0, uint16_t{2223});
        put(raw.p(), hold::kReqRawLen, len);
        put(raw.p(), hold::kReqRawPayload, key);
        put(wrapper.p(), hold::kReqWrapperSize, static_cast<uint16_t>(len + 5));
        put(wrapper.p(), hold::kReqWrapperRaw, raw.a());
    }
};

constexpr uintptr_t kS1 = 0x51000, kS2 = 0x52000;  // the host's session, user 2's

// Session 8: the host (character + pet, both acting: restore 1) and user 2 (character + pet) in one
// sector. Holds: D-53 (child_hold), character_hold, child_settle 1500 ms.
struct World {
    FakeUser host{kHostKey}, user2{kUser2Key};
    FakeSam sam;
    FakeSector sector;
    FakeActor char1{kHostChar, 0, kHostKey, 1, 1};
    FakeActor char2{kUser2Char, 0, kUser2Key, 1, 1};
    FakeActor pet1{kHostPet, 0, kHostKey, 6, 1};
    FakeActor pet2{kUser2Pet, 0, kUser2Key, 6, 1};
    FakeActor gimmick{kGimmick, 0, 0, 3, 1};
    hold_test::Seams seams;
    explicit World(uint32_t settle_ms = 1500, bool characters = true) {
        char1.set_owner(host.a());
        char2.set_owner(user2.a());
        pet1.set_owner(host.a());
        pet2.set_owner(user2.a());
        host.set_main(char1.a());
        user2.set_main(char2.a());
        for (FakeActor* a : {&char1, &char2, &pet1, &pet2, &gimmick}) {
            sam.add(*a);
            a->set_sector(&sector);
        }
        sector.set({host.a(), user2.a()});
        t_host = host.a();
        t_user2 = user2.a();
        t_sessions = {{kS1, host.a()}, {kS2, user2.a()}};
        t_leaving = false;
        t_epoch = 1;
        seams.notify = fake_notify;
        seams.sam = sam.a();
        seams.base = 0x140000000;
        seams.realm = true;
        seams.now_ms = fake_now;
        seams.thread_id = fake_tid;
        g_now = 1000;
        g_tid = 7;
        g_native.clear();
        hold_test::reset(&seams, true, false, characters);
        hold_test::set_settle_ms(settle_ms);
    }
    ~World() { hold_test::reset(nullptr, true, false, false); }
    void appear(std::vector<uint32_t> keys, std::vector<uintptr_t> sessions = {kS1, kS2}) {
        const GameVec s{sessions.data(), static_cast<uint32_t>(sessions.size()), static_cast<uint32_t>(sessions.size())};
        const GameVec k{keys.data(), static_cast<uint32_t>(keys.size()), static_cast<uint32_t>(keys.size())};
        hold_test::notify(0, &s, &k);
    }
    void action(uint32_t key = 0xB0100555) {
        FakeReq r(key);
        hold_test::start_action(r.wrapper.a());
    }
};

bool native_has(size_t i, std::vector<uintptr_t> sessions, std::vector<uint32_t> keys) {
    return i < g_native.size() && g_native[i].sessions == sessions && g_native[i].keys == keys;
}
bool contains(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

// ---------------------------------------------------------------- child_settle: pure rules

void test_settle_pure() {
    CHECK(cst::settle_type(6) && cst::settle_type(5) && cst::settle_type(4));
    CHECK(!cst::settle_type(1) && !cst::settle_type(3) && !cst::settle_type(7));
    // The split's need.
    CHECK(!cst::settle_needed(0, true, 0, true, 100));
    CHECK(cst::settle_needed(1500, true, 0, false, 100));
    CHECK(cst::settle_needed(1500, false, 0, true, 100));
    CHECK(cst::settle_needed(1500, false, 1000, false, 2499));
    CHECK(!cst::settle_needed(1500, false, 1000, false, 2500));
    CHECK(!cst::settle_needed(1500, false, 0, false, 2500));
    // Never announced there: waited for only while that user sees the character (it is coming).
    CHECK(cst::settle_needed(1500, false, 0, false, 2500, true));
    CHECK(!cst::settle_needed(1500, false, 0, false, 2500, false));
    CHECK(!cst::settle_needed(1500, false, 0, false, 2500, std::nullopt));
    CHECK(!cst::settle_needed(1500, false, 100, false, 2500, true));  // announced long ago: in range is irrelevant
    // The log: newest wins, per (key, session).
    cst::AnnounceLog<4> log;
    CHECK(log.last(kHostChar, kS2) == 0);
    log.note(kHostChar, kS2, 100);
    log.note(kHostChar, kS1, 200);
    log.note(kHostChar, kS2, 300);
    CHECK(log.last(kHostChar, kS2) == 300 && log.last(kHostChar, kS1) == 200 && log.last(kUser2Char, kS2) == 0);
    for (uint32_t i = 0; i < 8; ++i) log.note(0xA0200000 + i, kS1, 400 + i);  // overwrites round robin
    CHECK(log.last(0xA0200007, kS1) == 407);
    log.clear();
    CHECK(log.last(0xA0200007, kS1) == 0);
    // The release steps.
    cst::Readings r;
    r.actor = uintptr_t{0x7000};
    r.actor_then = 0x7000;
    r.owner = uintptr_t{0xA000};
    r.owner_then = 0xA000;
    r.observer_valid = true;
    r.observes = true;
    r.since_ms = 1000;
    r.main_last_ms = 1000;
    const char* why = "";
    cst::Kind kind = cst::Kind::None;
    r.now_ms = 2499;
    CHECK(cst::release_step(r, 1500, why, &kind) == cst::Step::Keep && std::strcmp(why, "settling") == 0);
    r.now_ms = 2500;
    CHECK(cst::release_step(r, 1500, why, &kind) == cst::Step::Release && kind == cst::Kind::Settled);
    // Due from the character's announcement, not from the hold.
    r.since_ms = 2000;
    CHECK(cst::release_step(r, 1500, why, &kind) == cst::Step::Release);
    r.main_last_ms = 0;  // unknown: from the hold
    CHECK(cst::release_step(r, 1500, why, &kind) == cst::Step::Keep);
    r.since_ms = 1000;
    r.main_last_ms = 1000;
    // Still withheld by character_hold: kept, then natively after the max wait.
    r.main_held = true;
    CHECK(cst::release_step(r, 1500, why, &kind) == cst::Step::Keep);
    r.now_ms = 1000 + cst::max_wait_ms(1500);
    CHECK(cst::release_step(r, 1500, why, &kind) == cst::Step::Release && kind == cst::Kind::MaxWait);
    r.main_held = false;
    r.now_ms = 2600;
    // Drops: the observer cannot see it.
    cst::Readings d = r;
    d.observes = false;
    CHECK(cst::release_step(d, 1500, why, &kind) == cst::Step::Drop && std::strcmp(why, "observer out of range") == 0);
    d = r;
    d.actor = uintptr_t{0};
    CHECK(cst::release_step(d, 1500, why, &kind) == cst::Step::Drop);
    d = r;
    d.observer_valid = false;
    CHECK(cst::release_step(d, 1500, why, &kind) == cst::Step::Drop);
    d = r;
    d.epoch_same = false;
    CHECK(cst::release_step(d, 1500, why, &kind) == cst::Step::Drop);
    // In range, another actor / owner: natively.
    d = r;
    d.actor = uintptr_t{0x7100};
    CHECK(cst::release_step(d, 1500, why, &kind) == cst::Step::Release && kind == cst::Kind::Native);
    d = r;
    d.owner = uintptr_t{0xB000};
    CHECK(cst::release_step(d, 1500, why, &kind) == cst::Step::Release && kind == cst::Kind::Native);
    // Unreadable: kept, then announced natively (never a silent drop; the engine's lookup decides).
    d = r;
    d.observes = std::nullopt;
    CHECK(cst::release_step(d, 1500, why, &kind) == cst::Step::Keep);
    d.now_ms = 1000 + cst::max_wait_ms(1500);
    CHECK(cst::release_step(d, 1500, why, &kind) == cst::Step::Release && kind == cst::Kind::MaxWait);
    d = r;
    d.actor = std::nullopt;
    CHECK(cst::release_step(d, 1500, why, &kind) == cst::Step::Keep);
    d.now_ms = 1000 + cst::max_wait_ms(1500);
    CHECK(cst::release_step(d, 1500, why, &kind) == cst::Step::Release && kind == cst::Kind::MaxWait);
    d = r;
    d.owner = std::nullopt;
    d.observes = std::nullopt;
    d.now_ms = 1000 + cst::max_wait_ms(1500);
    CHECK(cst::release_step(d, 1500, why, &kind) == cst::Step::Release && kind == cst::Kind::MaxWait);
    // The character in range but never announced there: kept (even past the settle), natively at the max.
    d = r;
    d.main_last_ms = 0;
    d.main_in_range = true;
    d.now_ms = 9000;
    CHECK(cst::release_step(d, 1500, why, &kind) == cst::Step::Keep &&
          std::strcmp(why, "its character is in range but not announced there yet") == 0);
    d.now_ms = 1000 + cst::max_wait_ms(1500);
    CHECK(cst::release_step(d, 1500, why, &kind) == cst::Step::Release && kind == cst::Kind::MaxWait);
    d.main_in_range = false;  // not coming: from the hold
    d.now_ms = 2500;
    CHECK(cst::release_step(d, 1500, why, &kind) == cst::Step::Release && kind == cst::Kind::Settled);
}

// ---------------------------------------------------------------- child_settle: hooks end to end

// Session 8's 2005: user 2 enters the host's sectors; its session alone receives the host's character and
// pet (and a gimmick) in one NotifyAppear. The pet must not follow its character in that 2005.
void test_settle_same_call_user2_only() {
    World w;
    w.appear({kGimmick, kHostChar, kHostPet}, {kS2});
    CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kGimmick, kHostChar}));
    CHECK(hold_test::settle_size() == 1);
    HoldEntry e;
    CHECK(hold_test::settle_entry(0, e) && e.key == kHostPet && e.observer == kS2 && !e.observer_host &&
          e.owner_who == OwnerWho::Host && e.main_key == kHostChar && e.settle && e.type == 6);
    hold_test::network_tick();
    std::string lines = hold_test::lines();
    CHECK(contains(lines, "child settle: held 0xB0100002") && contains(lines, "restore 1") &&
          contains(lines, "goes in the same NotifyAppear"));
    // Before the settle: StartActionReqs of anything release nothing.
    g_native.clear();
    g_now = 2499;
    w.action();
    CHECK(g_native.empty() && hold_test::settle_size() == 1);
    // The receiving thread's fallback (another thread than the last NotifyAppear's) does not release yet.
    g_now = 2600;
    g_tid = 9;
    hold_test::server_tick();
    CHECK(g_native.empty() && hold_test::settle_size() == 1);
    // 1500 ms after its character: one native NotifyAppear to user 2 only, the pet alone (a fresh 2005).
    g_tid = 7;
    w.action();
    CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kHostPet}) && hold_test::settle_size() == 0);
    const auto c = hold_test::settle_counters();
    CHECK(c.held == 1 && c.released == 1 && c.settled == 1 && c.dropped == 0 && c.deferred == 1);
    hold_test::network_tick();
    lines = hold_test::lines();
    CHECK(contains(lines, "child settle: released 0xB0100002") && contains(lines, "after 1600 ms") &&
          contains(lines, "settled after its character") && !contains(lines, "WRN"));
}

// A quiet world: no StartActionReq, no NotifyAppear after the hold. The receiving thread's fallback
// releases on the last NotifyAppear thread at once (as D-53's fallback), and, with notify_thread_rule=0,
// on another thread once the due release has waited hold::kDeferWarnMs; with 1, never there.
void test_settle_fallback() {
    {
        World w;
        w.appear({kHostChar, kHostPet}, {kS2});
        g_native.clear();
        g_now = 2600;  // g_tid 7: the thread of the last NotifyAppear
        hold_test::server_tick();
        CHECK(native_has(0, {kS2}, {kHostPet}) && hold_test::settle_size() == 0);
    }
    {
        World w;
        w.appear({kHostChar, kHostPet}, {kS2});
        g_native.clear();
        g_tid = 9;  // the receiving thread
        g_now = 2600;
        hold_test::server_tick();  // due, deferred: its wait starts
        g_now = 2600 + hold::kDeferWarnMs - 300;  // the fallback runs at most every 250 ms
        hold_test::server_tick();
        CHECK(g_native.empty() && hold_test::settle_size() == 1);
        g_now = 2600 + hold::kDeferWarnMs;
        hold_test::server_tick();
        CHECK(native_has(0, {kS2}, {kHostPet}) && hold_test::settle_size() == 0 &&
              hold_test::settle_counters().fallback == 1);
    }
    {
        World w;
        hold_test::set_thread_rule(true);  // reset() puts it back to 0
        w.appear({kHostChar, kHostPet}, {kS2});  // thread 7 calls NotifyAppear natively: a native thread
        g_native.clear();
        g_tid = 9;
        g_now = 2600;
        hold_test::server_tick();
        g_now = 9000;
        hold_test::server_tick();
        CHECK(g_native.empty() && hold_test::settle_size() == 1);
        g_tid = 7;  // the native thread's own fallback may
        g_now = 9300;
        hold_test::server_tick();
        CHECK(native_has(0, {kS2}, {kHostPet}) && hold_test::settle_size() == 0);
    }
}

// A split re-holds the child for the same observer while the release's NotifyAppear runs: that entry goes
// after the release (the 2005 just sent is its own), never a second NotifyAppear.
uint32_t g_race_key = 0;
uintptr_t g_race_session = 0;
void fake_notify_race(uintptr_t flag, const GameVec* s, const GameVec* k) {
    fake_notify(flag, s, k);
    const auto* sd = static_cast<const uintptr_t*>(s->data);
    const auto* kd = static_cast<const uint32_t*>(k->data);
    if (s->count == 1 && k->count == 1 && sd[0] == g_race_session && kd[0] == g_race_key) {
        HoldEntry e;
        e.key = g_race_key;
        e.observer = g_race_session;
        e.settle = true;
        e.epoch = 1;
        e.since_ms = g_now;
        hold_test::add_settle_entry(e);
    }
}

void test_settle_release_race() {
    World w;
    w.appear({kHostChar, kHostPet}, {kS2});
    w.seams.notify = fake_notify_race;
    g_race_key = kHostPet;
    g_race_session = kS2;
    g_native.clear();
    g_now = 2500;
    w.action();
    CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kHostPet}));
    CHECK(hold_test::settle_size() == 0 && hold_test::settle_counters().raced == 1);
    g_now = 9000;
    w.action();
    CHECK(g_native.size() == 1);
    g_race_key = 0;
}

// The pet's NotifyAppear comes before its character's (its sector first): waited for when that user sees
// the character; not when the character is out of its range (the horse left far away).
void test_settle_child_first() {
    {
        World w;
        w.appear({kHostPet}, {kS2});
        CHECK(g_native.empty() && hold_test::settle_size() == 1);
        g_now = 5000;
        w.action();
        CHECK(g_native.empty() && hold_test::settle_size() == 1);  // its character never announced there yet
        w.appear({kHostChar}, {kS2});
        CHECK(native_has(0, {kS2}, {kHostChar}) && hold_test::settle_size() == 1);
        g_native.clear();
        g_now = 6499;
        w.action();
        CHECK(g_native.empty());
        g_now = 6500;
        w.action();
        CHECK(native_has(0, {kS2}, {kHostPet}) && hold_test::settle_size() == 0);
    }
    {
        World w;
        FakeSector far;
        far.set({w.host.a()});
        w.char1.set_sector(&far);  // user 2 does not see the host's character
        w.appear({kHostPet}, {kS2});
        CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kHostPet}) && hold_test::settle_size() == 0);
    }
}

void test_settle_same_call_both() {
    World w;
    // Both receive: the host gets its pet at once (its own 2005), user 2 only the character.
    w.appear({kHostChar, kHostPet});
    CHECK(g_native.size() == 2 && native_has(0, {kS1, kS2}, {kHostChar}) && native_has(1, {kS1}, {kHostPet}));
    CHECK(hold_test::settle_size() == 1);
    // The tail of a native NotifyAppear releases too (no StartActionReq at all).
    g_native.clear();
    g_now = 2500;
    w.appear({0xB0100777});
    CHECK(g_native.size() == 2 && native_has(0, {kS1, kS2}, {0xB0100777}) && native_has(1, {kS2}, {kHostPet}));
    CHECK(hold_test::settle_size() == 0);
}

void test_settle_recent_and_old() {
    {
        World w;
        w.appear({kHostChar}, {kS2});  // the character alone first
        g_now = 1300;
        g_native.clear();
        w.appear({kHostPet}, {kS2});  // the pet 300 ms later: withheld
        CHECK(g_native.empty() && hold_test::settle_size() == 1);
        HoldEntry e;
        CHECK(hold_test::settle_entry(0, e) && e.since_ms == 1300);
        g_now = 2499;
        w.action();
        CHECK(g_native.empty());
        g_now = 2500;  // 1500 ms after the character (not after the pet's hold)
        w.action();
        CHECK(native_has(0, {kS2}, {kHostPet}) && hold_test::settle_size() == 0);
    }
    {
        World w;
        w.appear({kHostChar}, {kS2});
        g_now = 2500;  // settled long enough: the pet passes natively, nothing withheld
        g_native.clear();
        w.appear({kHostPet}, {kS2});
        CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kHostPet}) && hold_test::settle_size() == 0);
    }
}

void test_settle_off_owner_type() {
    {
        World w(0);
        w.appear({kHostChar, kHostPet}, {kS2});
        CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kHostChar, kHostPet}) && hold_test::settle_size() == 0);
    }
    {
        // Its owner's own session only (the host's own login): never withheld.
        World w;
        w.appear({kHostChar, kHostPet}, {kS1});
        CHECK(g_native.size() == 1 && native_has(0, {kS1}, {kHostChar, kHostPet}) && hold_test::settle_size() == 0);
    }
    {
        // A child of another type (not 4/5/6): unchanged.
        World w;
        FakeActor other(0xB0100009, w.host.a(), kHostKey, 1, 1);
        w.sam.add(other);
        other.set_sector(&w.sector);
        w.appear({kHostChar, 0xB0100009}, {kS2});
        CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kHostChar, 0xB0100009}) && hold_test::settle_size() == 0);
    }
    {
        // An NPC (no owner) right after a character: unchanged.
        World w;
        w.appear({kHostChar, kGimmick}, {kS2});
        CHECK(g_native.size() == 1 && hold_test::settle_size() == 0);
    }
}

void test_settle_drops() {
    {
        World w;
        w.appear({kHostChar, kHostPet}, {kS2});
        CHECK(hold_test::settle_size() == 1);
        w.sector.set({w.host.a()});  // user 2 left the pet's sectors
        g_native.clear();
        g_now = 2600;
        w.action();
        CHECK(g_native.empty() && hold_test::settle_size() == 0 && hold_test::settle_counters().dropped == 1);
        hold_test::network_tick();
        CHECK(contains(hold_test::lines(), "observer out of range"));
    }
    {
        World w;
        w.appear({kHostChar, kHostPet}, {kS2});
        t_leaving = true;  // user 2's logout was handed: its entries go, never published
        hold_test::network_tick();
        CHECK(hold_test::settle_size() == 0 && hold_test::settle_counters().cleared == 1);
        CHECK(contains(hold_test::lines(), "child settle: user 2 is leaving"));
    }
    {
        // The engine announces the pet to user 2 itself later (passes: not recent any more): entry forgotten.
        World w;
        w.appear({kHostChar, kHostPet}, {kS2});
        g_now = 9000;
        g_native.clear();
        hold_test::set_settle_ms(1500);
        w.appear({kHostPet}, {kS2});
        CHECK(native_has(0, {kS2}, {kHostPet}) && hold_test::settle_size() == 0 &&
              hold_test::settle_counters().forgotten == 1);
    }
}

// User 2's pet at restore 0 (D-53 holds it from the host): its first action lands 200 ms after user 2's
// character reached the host: D-53's release waits until the character settled there.
void test_settle_d53_waits() {
    World w;
    w.pet2.set_restore(0);
    w.appear({kUser2Char, kUser2Pet});  // both sessions: the character to both, the pet held by D-53 from S1
    CHECK(native_has(0, {kS1, kS2}, {kUser2Char}) && native_has(1, {kS2}, {kUser2Pet}));
    CHECK(hold_test::child_size() == 1 && hold_test::settle_size() == 0);
    w.pet2.set_restore(1);
    g_native.clear();
    g_now = 1200;
    w.action(kUser2Pet);
    CHECK(g_native.empty() && hold_test::child_size() == 1 && hold_test::settle_counters().d53_waited == 1);
    g_now = 2500;
    w.action(kUser2Pet);
    CHECK(native_has(0, {kS1}, {kUser2Pet}) && hold_test::child_size() == 0);
    hold_test::network_tick();
    CHECK(contains(hold_test::lines(), "D-53's release of 0xB0100334"));
}

// The host's character itself withheld by character_hold (restore 0, a reload): its pet waits for it and
// then 1500 ms more.
void test_settle_behind_character_hold() {
    World w;
    w.char1.set_restore(0);
    w.appear({kHostChar, kHostPet}, {kS2});
    CHECK(g_native.empty() && hold_test::char_size() == 1 && hold_test::settle_size() == 1);
    g_now = 3000;
    w.action();
    CHECK(g_native.empty() && hold_test::settle_size() == 1);  // the character is still withheld
    w.char1.set_restore(1);
    g_now = 4000;
    w.action(kHostChar);  // character_hold releases the character now (noted as announced)
    CHECK(native_has(0, {kS2}, {kHostChar}) && hold_test::char_size() == 0 && hold_test::settle_size() == 1);
    g_native.clear();
    g_now = 5499;
    w.action();
    CHECK(g_native.empty());
    g_now = 5500;
    w.action();
    CHECK(native_has(0, {kS2}, {kHostPet}) && hold_test::settle_size() == 0);
}

// observer_hold_acks: the pet's 2697/2991/2404 to user 2 wait for its own 2005.
std::vector<std::vector<uint8_t>> g_out;
bool test_push(std::span<const uint8_t> p) {
    g_out.emplace_back(p.begin(), p.end());
    return true;
}
std::vector<uint8_t> wire(uint16_t op, std::vector<uint8_t> payload) {
    std::vector<uint8_t> p(5 + payload.size());
    put(p.data(), 0, op);
    p[2] = 0xFF;
    put(p.data(), 3, static_cast<uint16_t>(payload.size()));
    std::copy(payload.begin(), payload.end(), p.begin() + 5);
    return p;
}
std::vector<uint8_t> actor_packet(uint16_t op, uint32_t key, uint8_t marker) {
    std::vector<uint8_t> payload(12, 0);
    put(payload.data(), 0, key);
    payload[4] = marker;
    return wire(op, payload);
}
std::vector<uint8_t> announce(const std::vector<uint32_t>& keys) {
    std::vector<uint8_t> payload;
    for (const uint32_t k : keys) {
        const auto at = static_cast<uint32_t>(payload.size());
        payload.resize(at + 2 + 9, 0);
        put(payload.data(), at, uint16_t{9});
        payload[at + 2] = 1;
        put(payload.data(), at + 3, k);
    }
    return wire(2005, payload);
}
void take(std::vector<uint8_t> pkt) {
    const uint16_t op = static_cast<uint16_t>(pkt[0] | pkt[1] << 8);
    AckRouteOut o;
    const std::vector<uint8_t> copy = pkt;
    const AckRoute r = observer_hold_acks_route(op, std::span<const uint8_t>(copy).subspan(5), pkt, &test_push, o);
    if (r == AckRoute::Pass) g_out.push_back(std::move(pkt));
}
void fake_notify_2005(uintptr_t flag, const GameVec* s, const GameVec* k) {
    fake_notify(flag, s, k);
    const auto* sd = static_cast<const uintptr_t*>(s->data);
    const auto* kd = static_cast<const uint32_t*>(k->data);
    for (uint32_t i = 0; i < s->count; ++i) {
        if (sd[i] == kS2) take(announce(std::vector<uint32_t>(kd, kd + k->count)));
    }
}
uint16_t op_of(const std::vector<uint8_t>& p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t key_of(const std::vector<uint8_t>& p) {
    uint32_t k = 0;
    std::memcpy(&k, p.data() + (op_of(p) == 2005 ? 8 : 5), 4);
    return k;
}

void test_settle_acks() {
    t_live_s2 = kS2;
    observer_hold_acks_configure(true);
    observer_hold_acks_clear(oha::Discard::Epoch);
    {
        World w;
        w.seams.notify = fake_notify_2005;
        g_out.clear();
        w.appear({kHostChar, kHostPet}, {kS2});
        CHECK(g_out.size() == 1 && op_of(g_out[0]) == 2005 && key_of(g_out[0]) == kHostChar);
        CHECK(observer_hold_acks_keys() == 1);
        // The pet's packets meanwhile (session 8: 2404 / 2991 / 2697): kept.
        take(actor_packet(2404, kHostPet, 1));
        take(actor_packet(2991, kHostPet, 2));
        take(actor_packet(2697, kHostPet, 3));
        take(actor_packet(2697, kHostChar, 4));  // the character's own: passes
        CHECK(g_out.size() == 2 && key_of(g_out[1]) == kHostChar);
        g_now = 2500;
        w.action();
        // Its own 2005, then its three packets in order.
        CHECK(g_out.size() == 6);
        if (g_out.size() == 6) {
            CHECK(op_of(g_out[2]) == 2005 && key_of(g_out[2]) == kHostPet);
            CHECK(op_of(g_out[3]) == 2404 && g_out[3][9] == 1);
            CHECK(op_of(g_out[4]) == 2991 && g_out[4][9] == 2);
            CHECK(op_of(g_out[5]) == 2697 && g_out[5][9] == 3);
        }
        CHECK(observer_hold_acks_keys() == 0);
    }
    {
        // A drop discards what was kept (no 2005 will come).
        observer_hold_acks_clear(oha::Discard::Epoch);
        World w;
        w.seams.notify = fake_notify_2005;
        g_out.clear();
        w.appear({kHostChar, kHostPet}, {kS2});
        take(actor_packet(2991, kHostPet, 2));
        w.sector.set({w.host.a()});
        g_now = 2600;
        w.action();
        CHECK(observer_hold_acks_keys() == 0 && g_out.size() == 1);
    }
    observer_hold_acks_configure(false);
    t_live_s2 = 0;
}

// ---------------------------------------------------------------- remote_interp: owned actors

void test_owned_cap() {
    using namespace rip;
    const std::array<uint32_t, 3> shoulder = {4170, 4180, 4175};
    CHECK(target_delay(shoulder, 200) == 1800);
    CHECK(target_delay(shoulder, 200, 900) == 900);
    CHECK(target_delay({600, 500, 0}, 200, 900) == 750);
    CHECK(target_delay({0, 0, 0}, 200, 900) == 200);
    CHECK(target_delay(shoulder, 1000, 900) == 1000);  // never below the game's own value
    CHECK(delay_cap(kOwnedYes, 900) == 900 && delay_cap(kOwnedNo, 900) == kMaxDelay &&
          delay_cap(kOwnedUnknown, 900) == kMaxDelay && delay_cap(kOwnedYes, 0) == kMaxDelay);
    CHECK(delay_cap(kOwnedYes, 50) == kMinDelay && delay_cap(kOwnedYes, 5000) == kMaxDelay);
    CHECK(skip_gap_attached(kOwnedYes, 900, true) && !skip_gap_attached(kOwnedYes, 900, false) &&
          !skip_gap_attached(kOwnedNo, 900, true) && !skip_gap_attached(kOwnedYes, 0, true));
    // Owned = a pointer to a USER (its +0x60 a 0x9 key), as the client's 0x8BDA10 / 0x8BDC07 read it.
    CHECK(classify_owner(true, 0x7FF600001000ull, true, kHostKey) == kOwnedYes);
    CHECK(classify_owner(true, 0x7FF600001000ull, true, 0xB0100005) == kOwnedNo);  // not a user
    CHECK(classify_owner(true, 0, false, 0) == kOwnedNo);
    CHECK(classify_owner(true, 0x1234, false, 0) == kOwnedNo);
    CHECK(classify_owner(false, 0, false, 0) == kOwnedUnknown);
    CHECK(classify_owner(true, 0x7FF600001000ull, false, 0) == kOwnedUnknown);
    // "No" is asked again every second; Unknown always; Yes stays.
    CHECK(owned_recheck_due(kOwnedUnknown, 5000, 5000) && !owned_recheck_due(kOwnedNo, 5000, 5999) &&
          owned_recheck_due(kOwnedNo, 5000, 6000) && !owned_recheck_due(kOwnedYes, 0, 1000000));
    // The slot: owned survives updates, reset by another actor behind the controller.
    static SlotTable<64> table;
    Slot* s = table.get(0x1000, kUser2Pet, 10, true).slot;
    CHECK(s && s->owned.load() == kOwnedUnknown);
    s->owned.store(kOwnedYes);
    for (const uint32_t g : shoulder) s->record_gap(g);
    // From 200 the owned pet's delay climbs to 900 and stays there (session 8: 1138-1800).
    uint64_t now = 100;
    for (int i = 0; i < 400; ++i) update_step(*s, KeyClass::Npc, now += 17, 200, delay_cap(s->owned.load(), 900));
    CHECK(from_fix(s->d_fix.load()) == 900);
    CHECK(table.get(0x1000, kUser2Pet, now, false).slot->owned.load() == kOwnedYes);
    CHECK(table.get(0x1000, 0xB0109999, now, false).slot->owned.load() == kOwnedUnknown);
}

// ---------------------------------------------------------------- attach_follow_logic

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
bool near3(afl::Vec3 a, afl::Vec3 b, float eps = 1e-3f) { return afl::dist(a, b) <= eps; }

afl::Quat yaw(float deg) {
    const float r = deg * 3.14159265f / 180.0f;
    return {0, std::sin(r / 2), 0, std::cos(r / 2)};
}

void test_math() {
    using namespace afl;
    const Pose k{{10, 20, 30}, yaw(90)};
    const Pose local{{0.2f, 1.58f, 0}, yaw(0)};
    const Pose w = compose(k, local);
    // Yaw 90 about +y maps +x to -z.
    CHECK(near3(w.p, {10, 21.58f, 29.8f}));
    const Pose back = relative(k, w);
    CHECK(near3(back.p, local.p) && near(std::fabs(back.q.w), 1.0f));
    CHECK(near(qlen(mul(yaw(30), yaw(60))), 1.0f) && near(mul(yaw(30), yaw(60)).w, yaw(90).w));
    CHECK(sane_local(local) && !sane_local({{100, 0, 0}, {}}) && !sane_world({{0, 0, 0}, {0, 0, 0, 0}}));
    CHECK(char_parent(0xA0100001ull) && !char_parent(0xB0100003ull) && !char_parent(kNoParent) &&
          !char_parent(0x1A0100001ull));
}

// A StartAction payload (725 bytes) in Ack or Req layout.
std::vector<uint8_t> action_payload(bool ack, uint32_t key, uint64_t parent, const afl::Pose& world,
                                    const afl::Pose& local, uint64_t ts, uint32_t hash = 0xBC77AF45) {
    std::vector<uint8_t> p(afl::kActionSize, 0);
    const afl::ActionLayout& l = ack ? afl::kAck : afl::kReq;
    put(p.data(), 0, key);
    put(p.data(), afl::kHash0, hash);
    put(p.data(), static_cast<uint32_t>(l.parent), parent);
    put(p.data(), static_cast<uint32_t>(l.ts), ts);
    put(p.data(), static_cast<uint32_t>(l.target), static_cast<uint32_t>(parent));
    std::memcpy(p.data() + l.pos, &world.p, 12);
    std::memcpy(p.data() + l.quat, &world.q, 16);
    std::memcpy(p.data() + l.local_pos, &local.p, 12);
    std::memcpy(p.data() + l.local_quat, &local.q, 16);
    return p;
}

std::vector<uint8_t> move_payload(size_t size, uint32_t key, uint64_t parent, const afl::Pose& pose, uint64_t ts) {
    std::vector<uint8_t> p(size, 0);
    put(p.data(), 0, key);
    std::memcpy(p.data() + afl::kMPos, &pose.p, 12);
    std::memcpy(p.data() + afl::kMQuat, &pose.q, 16);
    put(p.data(), static_cast<uint32_t>(afl::kMParent), parent);
    put(p.data(), static_cast<uint32_t>(afl::kMTs), ts);
    for (size_t i = afl::kMTs + 8; i < size; ++i) p[i] = static_cast<uint8_t>(i);  // a recognisable tail
    return p;
}

void test_layout_and_read() {
    using namespace afl;
    CHECK(kAck.world_pos == 0x11F && kReq.world_pos == 0x11E && kAck.local_quat == 0xE3 && kReq.ts == 0x15F);
    const Pose world{{1, 2, 3}, yaw(10)}, local{{0.1f, 1.5f, 0}, yaw(5)};
    for (const bool ack : {true, false}) {
        const auto p = action_payload(ack, kHostPet, kHostChar, world, local, 777);
        const Action a = read_action(p, ack);
        CHECK(a.ok && a.key == kHostPet && a.parent == kHostChar && a.ts == 777 && a.hash0 == 0xBC77AF45 &&
              a.target == kHostChar && near3(a.world.p, world.p) && near3(a.local.p, local.p) &&
              near(a.local.q.y, local.q.y));
    }
    CHECK(!read_action(std::vector<uint8_t>(724), true).ok);
    // The hash sits at +0x08 (session 8, Req and Ack); the Req repeats it at +0x04: either ends the ride.
    static_assert(kHash0 == 0x08 && kHash1 == 0x04);
    auto down = action_payload(false, kHostPet, kHostChar, world, local, 1, 0);
    put(down.data(), 0x04, kDismountHash);
    CHECK(dismount(read_action(down, false)));
    CHECK(!dismount(read_action(action_payload(true, kHostPet, kHostChar, world, local, 1), true)));
    const auto m = move_payload(kMoveAckSize, kHostChar, kNoParent, world, 555);
    const Move mv = read_move(m, kMoveAckSize);
    CHECK(mv.ok && mv.key == kHostChar && mv.parent == kNoParent && mv.ts == 555 && near3(mv.pose.p, world.p));
    CHECK(!read_move(m, kMoveReqSize).ok);
}

void test_tracker() {
    using namespace afl;
    Tracker<8> t;
    std::array<Make, 8> out{};
    const Pose k0{{100, 50, 200}, yaw(0)};
    const Pose local{{0.15f, 1.58f, -0.05f}, yaw(0)};
    // The character moves (noted even with nothing attached).
    CHECK(t.on_char_move(read_move(move_payload(kMoveAckSize, kHostChar, kNoParent, k0, 1000), kMoveAckSize), 10, 3,
                         out) == 0);
    // Climbing onto the shoulder: its world pose = K x local (the hypothesis holds exactly here).
    Measure m;
    const Pose on = compose(k0, local);
    CHECK(t.on_action(read_action(action_payload(true, kHostPet, kHostChar, on, local, 1010), true), 20, true, &m) ==
          Ev::Attached);
    CHECK(m.have_k && m.same_local >= 0 && m.same_local < 1e-3f && m.dt_ms == 10 && m.next_local < 0);
    CHECK(near3(m.rel.p, local.p));
    CHECK(t.attached(kHostPet) && t.size() == 1);
    // The character walks: one child Move every 3 of its own, at K x local.
    size_t made = 0;
    Pose k = k0;
    for (int i = 1; i <= 9; ++i) {
        k.p.x += 0.1f;
        k.q = yaw(static_cast<float>(i * 10));
        const size_t n =
            t.on_char_move(read_move(move_payload(kMoveAckSize, kHostChar, kNoParent, k, 1010 + i * 17), kMoveAckSize),
                           30 + i, 3, out);
        if (n) {
            CHECK(n == 1 && out[0].child == kHostPet && near3(out[0].pose.p, compose(k, local).p));
        }
        made += n;
    }
    CHECK(made == 3);
    // The next shoulder action: the previous local applied to K now is what 1 would show.
    const Pose on2 = compose(k, local);
    CHECK(t.on_action(read_action(action_payload(true, kHostPet, kHostChar, on2, local, 4200), true), 4200, true, &m) ==
          Ev::Refreshed);
    CHECK(m.next_local >= 0 && m.next_local < 1e-3f && m.next_rel >= 0);
    // Another character's Moves make nothing for it.
    CHECK(t.on_char_move(read_move(move_payload(kMoveAckSize, kUser2Char, kNoParent, k0, 5000), kMoveAckSize), 4300,
                         3, out) == 0);
    // Its own Move with parent -1 (it jumped down): detached.
    CHECK(t.on_child_move(kHostPet, kNoParent) && !t.attached(kHostPet));
    CHECK(!t.on_child_move(kHostPet, kNoParent));
    // Detach by an action with no parent, and by the dismount hash.
    CHECK(t.on_action(read_action(action_payload(true, kHostPet, kHostChar, on, local, 5000), true), 5000, true) ==
          Ev::Attached);
    CHECK(t.on_action(read_action(action_payload(true, kHostPet, kNoParent, on, local, 5100), true), 5100, true) ==
          Ev::Detached);
    CHECK(t.on_action(read_action(action_payload(true, kHostPet, kHostChar, on, local, 5200), true), 5200, true) ==
          Ev::Attached);
    CHECK(t.on_action(read_action(action_payload(true, kHostPet, kHostChar, on, local, 5300, kDismountHash), true), 5300,
                      true) == Ev::Detached);
    // Not a user's child (a grabbed prey with the same parent): never followed.
    CHECK(t.on_action(read_action(action_payload(true, 0xB0105555, kHostChar, on, local, 5400), true), 5400, false) ==
          Ev::NotOwned);
    CHECK(!t.attached(0xB0105555));
    // A Path local that is the world position (not an offset): kept, but nothing is made from it.
    CHECK(t.on_action(read_action(action_payload(true, kHostPet, kHostChar, on, on, 5500), true), 5500, true) ==
          Ev::BadLocal);
    size_t total = 0;
    for (int i = 0; i < 6; ++i) {
        total += t.on_char_move(read_move(move_payload(kMoveAckSize, kHostChar, kNoParent, k0, 5600 + i), kMoveAckSize),
                                5600 + i, 3, out);
    }
    CHECK(total == 0);
    // Staleness: 15 s without an attached action.
    CHECK(t.on_action(read_action(action_payload(true, kHostPet, kHostChar, on, local, 6000), true), 6000, true) ==
          Ev::Refreshed);
    size_t expired = 0;
    t.on_char_move(read_move(move_payload(kMoveAckSize, kHostChar, kNoParent, k0, 7000), kMoveAckSize),
                   6000 + kStaleMs + 1, 3, out, &expired);
    CHECK(expired == 1 && !t.attached(kHostPet));
    // Something else: ignored.
    CHECK(t.on_action(read_action(action_payload(true, kHostChar, kNoParent, on, local, 1), true), 1, true) ==
          Ev::Ignored);
}

void test_synthetic_moves() {
    using namespace afl;
    const Pose k{{100, 50, 200}, yaw(30)};
    const Pose pet = compose(k, {{0.15f, 1.58f, 0}, yaw(0)});
    auto parent = move_payload(kMoveAckSize, kHostChar, kNoParent, k, 0x1234567);
    std::vector<uint8_t> packet(kHeader);
    put(packet.data(), 0, kOpMoveActorAck);
    packet[2] = 0xFF;
    put(packet.data(), 3, static_cast<uint16_t>(kMoveAckSize));
    packet.insert(packet.end(), parent.begin(), parent.end());
    std::vector<uint8_t> out;
    CHECK(make_child_ack(packet, kHostPet, pet, out));
    CHECK(out.size() == packet.size() && std::memcmp(out.data(), packet.data(), kHeader) == 0);
    const std::span<const uint8_t> body(out.data() + kHeader, kMoveAckSize);
    const Move mv = read_move(body, kMoveAckSize);
    CHECK(mv.ok && mv.key == kHostPet && mv.parent == kNoParent && mv.ts == 0x1234567 && near3(mv.pose.p, pet.p));
    float scale[3] = {};
    std::memcpy(scale, body.data() + kMLocalScale, 12);
    CHECK(scale[0] == 1.0f && scale[1] == 1.0f && scale[2] == 1.0f);
    CHECK(std::memcmp(body.data() + kMLocalPos, &pet.p, 12) == 0);
    bool world_zero = true;
    for (size_t i = kMWorldPos; i < kMWorldEnd; ++i) world_zero = world_zero && body[i] == 0;
    CHECK(world_zero && body[kMAux] == 0);
    CHECK(std::memcmp(body.data() + kMTs, parent.data() + kMTs, kMoveAckSize - kMTs) == 0);  // ts and tail
    // Refused: not an Ack, a wrong size, not a child key, a bad pose.
    CHECK(!make_child_ack(std::span<const uint8_t>(packet).first(kHeader + 10), kHostPet, pet, out));
    CHECK(!make_child_ack(packet, kHostChar, pet, out));
    CHECK(!make_child_ack(packet, kHostPet, {{0, 0, 0}, {0, 0, 0, 0}}, out));
    // The Req (built, never sent): history zeroed.
    const auto req = move_payload(kMoveReqSize, kUser2Char, kNoParent, k, 99);
    CHECK(make_child_req(req, kUser2Pet, pet, out) && out.size() == kMoveReqSize);
    const Move rm = read_move(out, kMoveReqSize);
    CHECK(rm.key == kUser2Pet && rm.parent == kNoParent && rm.ts == 99);
    bool hist_zero = true;
    for (size_t i = kMReqHistory; i < kMoveReqSize; ++i) hist_zero = hist_zero && out[i] == 0;
    CHECK(hist_zero && out[kMTs + 8] == req[kMTs + 8]);
}

void test_registry() {
    using namespace afl;
    const Pose p{{1, 2, 3}, yaw(0)};
    RegEvent e = registry_event(kOpStartActionAck, action_payload(true, kUser2Pet, kUser2Char, p, p, 1));
    CHECK(e.what == Reg::Attach && e.key == kUser2Pet);
    e = registry_event(kOpStartActionReq, action_payload(false, kUser2Pet, kUser2Char, p, p, 1));
    CHECK(e.what == Reg::Attach && e.key == kUser2Pet);
    e = registry_event(kOpStartActionAck, action_payload(true, kUser2Pet, kNoParent, p, p, 1));
    CHECK(e.what == Reg::Detach);
    e = registry_event(kOpStartActionAck, action_payload(true, kUser2Pet, kUser2Char, p, p, 1, kDismountHash));
    CHECK(e.what == Reg::Detach);
    e = registry_event(kOpMoveActorAck, move_payload(kMoveAckSize, kUser2Pet, kNoParent, p, 1));
    CHECK(e.what == Reg::Detach && e.key == kUser2Pet);
    e = registry_event(kOpMoveActorReq, move_payload(kMoveReqSize, kUser2Char, kNoParent, p, 1));
    CHECK(e.what == Reg::None);
    e = registry_event(kOpStartActionAck, action_payload(true, kUser2Char, kNoParent, p, p, 1));
    CHECK(e.what == Reg::None);
    // Deferred detaches (the tracker was busy): each key once, applied by the next holder.
    PendingDetach<2> pd;
    CHECK(pd.empty() && pd.push(kUser2Pet) && pd.push(kUser2Pet) && pd.push(kHostPet) && !pd.push(0xB0100009));
    Tracker<8> t;
    CHECK(t.on_action(read_action(action_payload(true, kHostPet, kHostChar, p, {{0.1f, 1.5f, 0}, yaw(0)}, 1), true), 1,
                      true) == Ev::Attached);
    std::vector<uint32_t> drained;
    CHECK(pd.drain([&](uint32_t k) {
        drained.push_back(k);
        (void)t.drop(k);
    }) == 2);
    CHECK(drained.size() == 2 && !t.attached(kHostPet) && pd.empty() && !t.drop(kHostPet));
    Registry<4> r;
    r.attach(kUser2Pet, 1000);
    CHECK(r.attached(kUser2Pet, 1000) && r.attached(kUser2Pet, 1000 + kStaleMs - 1) &&
          !r.attached(kUser2Pet, 1000 + kStaleMs) && r.count(1000) == 1);
    r.detach(kUser2Pet);
    CHECK(!r.attached(kUser2Pet, 1001) && r.count(1001) == 0);
    for (uint32_t i = 0; i < 6; ++i) r.attach(0xB0200000 + i, 2000);  // full: round robin
    CHECK(r.attached(0xB0200005, 2000) && r.count(2000) == 4);
    r.clear();
    CHECK(r.count(2000) == 0);
}

}  // namespace

// P8 session 9, P1 (child_reannounce; pendientes/p8-impl-s9-movimiento.md §2): user 2's client could not create
// the host's pet; the host's child_settle_reannounce puts it in the settle table for session 2 and the usual
// release re-announces it natively after the backoff, never before; only the host's own children.
void test_settle_reannounce() {
    World w(4000);
    t_live_s2 = kS2;
    w.appear({kHostChar}, {kS2});  // the host's character announced to user 2 at 1000
    g_now = 6000;
    g_native.clear();
    CHECK(child_settle_reannounce(kHostPet, 2000) == Reannounce::Queued);
    CHECK(hold_test::settle_size() == 1);
    HoldEntry e;
    CHECK(hold_test::settle_entry(0, e) && e.key == kHostPet && e.observer == kS2 && e.settle &&
          e.owner_who == OwnerWho::Host && e.main_key == kHostChar && e.not_before_ms == 8000 && e.since_ms == 6000);
    CHECK(child_settle_reannounce(kHostPet, 2000) == Reannounce::AlreadyHeld && hold_test::settle_size() == 1);
    g_now = 7999;
    w.action();
    CHECK(g_native.empty() && hold_test::settle_size() == 1);
    g_now = 8000;
    w.action();
    CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kHostPet}) && hold_test::settle_size() == 0);
    // dry_run (the host's probe, and its check before the attempt is recorded): the verdict, nothing queued.
    CHECK(child_settle_reannounce(kHostPet, 2000, true) == Reannounce::Queued && hold_test::settle_size() == 0);
    CHECK(child_settle_reannounce(kUser2Pet, 2000, true) == Reannounce::NotHosts && g_native.size() == 1);
    // Refused: user 2's own pet, not a settle candidate, no session 2, child_settle off.
    CHECK(child_settle_reannounce(kUser2Pet, 2000) == Reannounce::NotHosts);
    CHECK(child_settle_reannounce(kGimmick, 2000) == Reannounce::NotCandidate);
    t_live_s2 = 0;
    CHECK(child_settle_reannounce(kHostPet, 2000) == Reannounce::NoUser2);
    t_live_s2 = kS2;
    hold_test::set_settle_ms(0);
    CHECK(child_settle_reannounce(kHostPet, 2000) == Reannounce::NotActive);
    CHECK(hold_test::settle_size() == 0 && g_native.size() == 1);
    t_live_s2 = 0;
}

int main() {
    test_settle_reannounce();
    test_settle_pure();
    test_settle_same_call_user2_only();
    test_settle_same_call_both();
    test_settle_fallback();
    test_settle_release_race();
    test_settle_child_first();
    test_settle_recent_and_old();
    test_settle_off_owner_type();
    test_settle_drops();
    test_settle_d53_waits();
    test_settle_behind_character_hold();
    test_settle_acks();
    test_owned_cap();
    test_math();
    test_layout_and_read();
    test_tracker();
    test_synthetic_moves();
    test_registry();
    if (g_failures) {
        std::printf("optd_pets: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_pets: all passed\n");
    return 0;
}
