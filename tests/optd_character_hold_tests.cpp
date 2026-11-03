// character_hold (docs/research/pendientes/p8-character-hold.md; child_hold.h):
//  - the predicate for a user's main character (key class 0xA, owner a
//    validated user, [owner+0xD0] == the actor, restore byte 0), its pure
//    release rules (restore 1, timeout with restore 0, drops only when the
//    observer cannot see it, native announce instead of a drop in range);
//  - through child_hold.h's test seams (a fake NotifyAppear, SAM, clock, realm
//    and thread id), the hooks end to end: the split (the owner gets it at
//    once, never held for the owner), record(), the pass after StartActionReq
//    and at the tail of NotifyAppear, the timeout, the drops (out of range,
//    despawn, epoch, user 2 leaving), the room reserved in the split, the
//    engine's own later announce, the pieces without CC, and the lines;
//  - the thread rule (p8-auditoria-hilos.md §4 case 3) for the three holds,
//    with notify_thread_rule=1: a release after a StartActionReq on a thread
//    that never called NotifyAppear natively waits for the tail of the next
//    native NotifyAppear; with 0 (the default, P8 review I3) the handler's
//    thread releases as before the rule; a due release waiting > 2 s gives a
//    WRN line (DeferWatch);
//  - observer_hold_acks' windows (P8 review I5): a key is held from before the
//    split's native calls; a 2005 that ends a hold with nothing held is
//    queued under the lock;
//  - observer_hold_acks with a character: the host's character withheld from
//    user 2 keeps its per-actor packets until its 2005.
// The hooks themselves need the game.

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "mod/session2/child_hold.h"
#include "mod/session2/guest_lifecycle.h"
#include "mod/session2/observer_hold_acks.h"

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
constexpr uint32_t kHostKey = 0x90100000, kUser2Key = 0x9010052B;
constexpr uint32_t kHostChar = 0xA0100001, kUser2Char = 0xA010052C;

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
    void no_cc() { put(cc.p(), hold::kCcActor, uintptr_t{0x1234}); }
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
    void replace(uint32_t slot, FakeActor& actor) { put(holder_blobs[slot]->p(), 8, actor.a()); }
    void remove(uint32_t key) {
        const uint32_t idx = key & c4::kSamKeyIndexMask;
        uint8_t* bucket = buckets.p() + (idx % kBuckets) * c4::kSamBucketBytes;
        uint32_t n = 0;
        std::memcpy(&n, bucket, 4);
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t e = 0;
            std::memcpy(&e, bucket + 8 + 8 * i, 4);
            if (e == idx) put(bucket, 8 + 8 * i, 0xFFFFFFFFu);
        }
    }
};

// ---------------------------------------------------------------- the predicate

void test_predicate() {
    FakeUser host(kHostKey), user2(kUser2Key), not_user(0x90100777, 0x1234);
    const uintptr_t h = host.a(), u2 = user2.a();
    FakeSam sam;
    FakeActor kliff1(kHostChar, h, kHostKey, 1, 0);      // the host's character after a reload (restore 0)
    FakeActor kliff2(kUser2Char, u2, kUser2Key, 1, 0);   // user 2's, the obsolete script (no 4b)
    FakeActor other_char(0xA0100600, u2, kUser2Key, 1, 0);  // another 0xA actor of user 2 (not its main one)
    FakeActor nobody(0xA0100601, 0, 0, 1, 0);
    FakeActor stranger(0xA0100602, not_user.a(), 0, 1, 0);
    FakeActor no_cc(0xA0100603, u2, kUser2Key, 1, 0);
    no_cc.no_cc();
    FakeUser user3(0x90100603);
    user3.set_main(no_cc.a());  // so only the CC rule applies to it
    no_cc.set_owner(user3.a());
    host.set_main(kliff1.a());
    user2.set_main(kliff2.a());
    for (FakeActor* a : {&kliff1, &kliff2, &other_char, &nobody, &stranger, &no_cc}) sam.add(*a);
    HoldRules on;
    on.characters = true;
    HoldVerdict v = hold_verdict_rules(sam.a(), kUser2Char, u2, h, kHostKey, kUserVt, on);
    CHECK(v.hold && v.character && !v.npc && v.why == HoldWhy::Hold && v.owner == u2 && v.owner_who == OwnerWho::Guest &&
          v.actor == kliff2.a() && v.type == 1);
    v = hold_verdict_rules(sam.a(), kHostChar, u2, h, kHostKey, kUserVt, on);
    CHECK(v.hold && v.character && v.owner == h && v.owner_who == OwnerWho::Host);
    on.fast = true;  // the same with observer_hold's SEH-only reads
    CHECK(hold_verdict_rules(sam.a(), kHostChar, u2, h, kHostKey, kUserVt, on).hold);
    on.fast = false;
    // Off (default): a character key is not looked at, exactly as before.
    CHECK(hold_verdict(sam.a(), kUser2Char, u2, h, kHostKey, kUserVt).why == HoldWhy::NotChildKey);
    HoldRules off;
    CHECK(hold_verdict_rules(sam.a(), kUser2Char, u2, h, kHostKey, kUserVt, off).why == HoldWhy::NotChildKey);
    // Not its user's main character, nobody's, not a user's, no CC: left native.
    CHECK(hold_verdict_rules(sam.a(), 0xA0100600, u2, h, kHostKey, kUserVt, on).why == HoldWhy::NotMain);
    CHECK(hold_verdict_rules(sam.a(), 0xA0100601, u2, h, kHostKey, kUserVt, on).why == HoldWhy::NoOwner);
    CHECK(hold_verdict_rules(sam.a(), 0xA0100602, u2, h, kHostKey, kUserVt, on).why == HoldWhy::NotUser);
    CHECK(hold_verdict_rules(sam.a(), 0xA0100603, u2, h, kHostKey, kUserVt, on).why == HoldWhy::NoCc);
    CHECK(hold_verdict_rules(sam.a(), 0xA0100999, u2, h, kHostKey, kUserVt, on).why == HoldWhy::NotFound);
    CHECK(hold_verdict_rules(0, kUser2Char, u2, h, kHostKey, kUserVt, on).why == HoldWhy::Unreadable);
    // The host's pointer not read now: its character is left native.
    CHECK(hold_verdict_rules(sam.a(), kHostChar, u2, 0, kHostKey, kUserVt, on).why == HoldWhy::NotUser);
    // Its first action accepted (0x2328894): not held any more.
    kliff2.set_restore(1);
    CHECK(hold_verdict_rules(sam.a(), kUser2Char, u2, h, kHostKey, kUserVt, on).why == HoldWhy::Restored);
    // Users and children keep their rules.
    CHECK(hold_verdict_rules(sam.a(), kUser2Key, u2, h, kHostKey, kUserVt, on).why == HoldWhy::NotChildKey);
    FakeActor pet(0xB010052D, u2, kUser2Key, 6, 0);
    sam.add(pet);
    v = hold_verdict_rules(sam.a(), 0xB010052D, u2, h, kHostKey, kUserVt, on);
    CHECK(v.hold && !v.character);
    on.children = false;
    CHECK(hold_verdict_rules(sam.a(), 0xB010052D, u2, h, kHostKey, kUserVt, on).why == HoldWhy::ChildrenOff);
    CHECK(std::strcmp(hold_why_name(HoldWhy::NotMain), "not its user's main character") == 0);
}

// ---------------------------------------------------------------- the pure release rules

HoldEntry char_entry(uintptr_t observer_user, uintptr_t owner, uintptr_t actor) {
    HoldEntry e;
    e.key = kUser2Char;
    e.observer = 0x51000;
    e.observer_user = observer_user;
    e.owner = owner;
    e.owner_who = OwnerWho::Guest;
    e.observer_host = true;
    e.character = true;
    e.actor = actor;
    e.epoch = 1;
    return e;
}

CharReleaseReadings ok(uintptr_t actor, uintptr_t owner, uint8_t restore, uintptr_t observer_user) {
    CharReleaseReadings r;
    r.epoch_now = 1;
    r.actor = actor;
    r.owner = owner;
    r.restore = restore;
    r.observer_valid = true;
    r.observer_user = observer_user;
    r.observes = true;
    return r;
}

void test_release_rules() {
    constexpr uintptr_t host = 0xA000, u2 = 0xB000, actor = 0x7777;
    const HoldEntry e = char_entry(host, u2, actor);
    const char* why = "";
    CharRelease kind = CharRelease::None;
    CHECK(char_release_step(e, ok(actor, u2, 0, host), 300, why, &kind) == ReleaseStep::Keep &&
          std::strcmp(why, "restore 0") == 0);
    CHECK(char_release_step(e, ok(actor, u2, 1, host), 300, why, &kind) == ReleaseStep::Release &&
          kind == CharRelease::Restore1);
    // Never before the timeout with restore 0; at the timeout, announced with restore 0.
    CHECK(char_release_step(e, ok(actor, u2, 0, host), hold::kCharTimeoutMs - 1, why, &kind) == ReleaseStep::Keep);
    CHECK(char_release_step(e, ok(actor, u2, 0, host), hold::kCharTimeoutMs, why, &kind) == ReleaseStep::Release &&
          kind == CharRelease::Timeout && std::strncmp(why, "timeout", 7) == 0);
    // Only while known to be in range.
    CharReleaseReadings r = ok(actor, u2, 1, host);
    r.observes = std::nullopt;
    CHECK(char_release_step(e, r, 300, why, &kind) == ReleaseStep::Keep);
    CHECK(char_release_step(e, r, 3 * hold::kCharTimeoutMs, why, &kind) == ReleaseStep::Drop);
    // Drops: the observer cannot see it now.
    r = ok(actor, u2, 0, host);
    r.observes = false;
    CHECK(char_release_step(e, r, 300, why, &kind) == ReleaseStep::Drop && std::strcmp(why, "observer out of range") == 0);
    r = ok(0, u2, 0, host);
    r.actor = uintptr_t{0};
    CHECK(char_release_step(e, r, 300, why, &kind) == ReleaseStep::Drop && std::strncmp(why, "despawned", 9) == 0);
    r = ok(actor, u2, 0, host);
    r.observer_valid = false;
    CHECK(char_release_step(e, r, 300, why, &kind) == ReleaseStep::Drop && std::strcmp(why, "observer gone") == 0);
    r = ok(actor, u2, 0, host);
    r.epoch_now = 2;
    CHECK(char_release_step(e, r, 300, why, &kind) == ReleaseStep::Drop && std::strcmp(why, "another guest epoch") == 0);
    // In range, would have been dropped: announced natively instead (never invisible).
    CHECK(char_release_step(e, ok(0x8888, u2, 0, host), 300, why, &kind) == ReleaseStep::Release &&
          kind == CharRelease::Native && std::strncmp(why, "another actor", 13) == 0);
    CHECK(char_release_step(e, ok(actor, 0xC000, 0, host), 300, why, &kind) == ReleaseStep::Release &&
          kind == CharRelease::Native && std::strncmp(why, "owner changed", 13) == 0);
    r = ok(actor, u2, 0, host);
    r.owner = std::nullopt;
    CHECK(char_release_step(e, r, 300, why, &kind) == ReleaseStep::Keep);
    CHECK(char_release_step(e, r, 3 * hold::kCharTimeoutMs, why, &kind) == ReleaseStep::Release &&
          kind == CharRelease::Native);
    r.actor = std::nullopt;
    CHECK(char_release_step(e, r, 300, why, &kind) == ReleaseStep::Keep);
    CHECK(char_release_step(e, r, 3 * hold::kCharTimeoutMs, why, &kind) == ReleaseStep::Drop);
    // A duplicate keeps the first moment; another actor behind the key restarts it.
    CharHoldTable t;
    HoldEntry a = e;
    a.since_ms = 100;
    CHECK(t.add(a) == CharHoldTable::Add::Added);
    a.since_ms = 900;
    CHECK(t.add(a) == CharHoldTable::Add::Duplicate && t.at(0).since_ms == 100);
    a.actor = 0x8888;
    CHECK(t.add(a) == CharHoldTable::Add::Duplicate && t.at(0).since_ms == 900 && t.at(0).actor == 0x8888);
    for (uint32_t i = 1; i < hold::kCharTableCapacity; ++i) {
        HoldEntry x = e;
        x.key = 0xA0200000 + i;
        CHECK(t.add(x) == CharHoldTable::Add::Added);
    }
    HoldEntry over = e;
    over.key = 0xA0300000;
    CHECK(t.add(over) == CharHoldTable::Add::Full);
    CHECK(std::strcmp(char_release_name(CharRelease::Timeout), "TIMEOUT, announced with restore 0") == 0);
}

// ---------------------------------------------------------------- hooks end to end (test seams)

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

// The host and user 2 in one sector with their characters (the host's has
// acted: restore 1; user 2's is the obsolete script's: restore 0).
struct World {
    FakeUser host{kHostKey}, user2{kUser2Key};
    FakeSam sam;
    FakeSector sector;
    FakeActor kliff1{kHostChar, 0, kHostKey, 1, 1};
    FakeActor kliff2{kUser2Char, 0, kUser2Key, 1, 0};
    uint32_t slot1 = 0, slot2 = 0;
    hold_test::Seams seams;
    explicit World(bool children = true, bool observers = false, bool characters = true) {
        kliff1.set_owner(host.a());
        kliff2.set_owner(user2.a());
        host.set_main(kliff1.a());
        user2.set_main(kliff2.a());
        slot1 = sam.add(kliff1);
        slot2 = sam.add(kliff2);
        sector.set({host.a(), user2.a()});
        kliff1.set_sector(&sector);
        kliff2.set_sector(&sector);
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
        hold_test::reset(&seams, children, observers, characters);
    }
    ~World() { hold_test::reset(nullptr, true, false, false); }
    void appear(std::vector<uint32_t> keys, std::vector<uintptr_t> sessions = {kS1, kS2}) {
        const GameVec s{sessions.data(), static_cast<uint32_t>(sessions.size()), static_cast<uint32_t>(sessions.size())};
        const GameVec k{keys.data(), static_cast<uint32_t>(keys.size()), static_cast<uint32_t>(keys.size())};
        hold_test::notify(0, &s, &k);
    }
};

bool native_has(size_t i, std::vector<uintptr_t> sessions, std::vector<uint32_t> keys) {
    return i < g_native.size() && g_native[i].sessions == sessions && g_native[i].keys == keys;
}

bool contains(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

void test_user2_char_held_from_host() {
    World w;
    // E1.T2: user 2's character enters the host's field (one NotifyAppear to both sessions).
    w.appear({kUser2Char});
    // Its owner (user 2) gets it at once in its own 2005; the host's session does not.
    CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kUser2Char}));
    CHECK(hold_test::char_size() == 1);
    HoldEntry e;
    CHECK(hold_test::char_entry(0, e) && e.key == kUser2Char && e.observer == kS1 && e.observer_host &&
          e.owner == w.user2.a() && e.owner_who == OwnerWho::Guest && e.actor == w.kliff2.a() && e.character);
    hold_test::network_tick();
    std::string lines = hold_test::lines();
    CHECK(contains(lines, "held character 0xA010052C") && contains(lines, "owner user 2 0x9010052B"));
    // A StartActionReq of another actor: still restore 0, nothing.
    g_native.clear();
    g_now = 1100;
    FakeReq other(0xB0100555);
    hold_test::start_action(other.wrapper.a());
    CHECK(g_native.empty() && hold_test::char_size() == 1);
    // 3 s: the "still held" line (once), still held.
    g_now = 1000 + hold::kTimeoutMs;
    hold_test::start_action(other.wrapper.a());
    hold_test::start_action(other.wrapper.a());
    hold_test::network_tick();
    lines = hold_test::lines();
    CHECK(g_native.empty() && contains(lines, "CharHold wait 0xA010052C"));
    CHECK(lines.find("CharHold wait") == lines.rfind("CharHold wait"));
    // Its first action is accepted (script step 14): released to the host's session only, restore 1.
    w.kliff2.set_restore(1);
    g_now = 6100;
    FakeReq act(kUser2Char);
    hold_test::start_action(act.wrapper.a());
    CHECK(g_native.size() == 1 && native_has(0, {kS1}, {kUser2Char}) && hold_test::char_size() == 0);
    const auto c = hold_test::char_counters();
    CHECK(c.held == 1 && c.released == 1 && c.restore1 == 1 && c.timeouts == 0 && c.dropped == 0);
    hold_test::network_tick();
    lines = hold_test::lines();
    CHECK(contains(lines, "released character 0xA010052C") && contains(lines, "after 5100 ms, restore 1") &&
          !contains(lines, "WRN"));
}

void test_host_char_held_from_user2() {
    World w;
    w.kliff1.set_restore(0);  // the host's character just reloaded / respawned
    w.appear({kHostChar});
    CHECK(native_has(0, {kS1}, {kHostChar}) && g_native.size() == 1);
    HoldEntry e;
    CHECK(hold_test::char_entry(0, e) && e.observer == kS2 && !e.observer_host && e.owner_who == OwnerWho::Host);
    // User 2 entering the host's sectors (the moving user's call): only user 2 receives: nothing sent now.
    g_native.clear();
    w.appear({kHostChar}, {kS2});
    CHECK(g_native.empty() && hold_test::char_size() == 1 && hold_test::char_counters().again == 1);
    w.kliff1.set_restore(1);
    FakeReq act(kHostChar);
    g_now = 1200;
    hold_test::start_action(act.wrapper.a());
    CHECK(native_has(0, {kS2}, {kHostChar}) && hold_test::char_size() == 0);
}

void test_owner_never_held() {
    World w;
    // User 2's own login (its own field): only its session receives its character: unchanged, nothing held.
    w.appear({kUser2Key, kUser2Char}, {kS2});
    CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kUser2Key, kUser2Char}) && hold_test::char_size() == 0);
    // The host's own character to the host only: unchanged.
    w.kliff1.set_restore(0);
    g_native.clear();
    w.appear({kHostChar}, {kS1});
    CHECK(g_native.size() == 1 && native_has(0, {kS1}, {kHostChar}) && hold_test::char_size() == 0);
    // With restore 1 it is never held.
    w.kliff2.set_restore(1);
    g_native.clear();
    w.appear({kUser2Char});
    CHECK(g_native.size() == 1 && native_has(0, {kS1, kS2}, {kUser2Char}) && hold_test::char_size() == 0);
}

void test_off_unchanged() {
    World w(true, false, false);
    w.appear({kUser2Char});
    CHECK(g_native.size() == 1 && native_has(0, {kS1, kS2}, {kUser2Char}) && hold_test::char_size() == 0);
}

void test_timeout_announces_with_restore0() {
    World w;
    w.appear({kUser2Char});
    g_native.clear();
    FakeReq other(0xB0100555);
    g_now = 1000 + hold::kCharTimeoutMs - 1;
    hold_test::start_action(other.wrapper.a());
    CHECK(g_native.empty() && hold_test::char_size() == 1);
    // No StartActionReq at all: the tail of the game's own NotifyAppear runs the pass too.
    g_now = 1000 + hold::kCharTimeoutMs;
    w.appear({0xB0100777});  // unknown key: passes, then the pass
    CHECK(g_native.size() == 2 && native_has(0, {kS1, kS2}, {0xB0100777}) && native_has(1, {kS1}, {kUser2Char}));
    CHECK(hold_test::char_size() == 0 && hold_test::char_counters().timeouts == 1);
    hold_test::network_tick();
    const std::string lines = hold_test::lines();
    CHECK(contains(lines, "WRN Session2 character hold: released character 0xA010052C") &&
          contains(lines, "TIMEOUT, announced with restore 0"));
}

void test_drops() {
    {
        // The host leaves the character's range (a field change): dropped, nothing sent.
        World w;
        w.appear({kUser2Char});
        w.sector.set({w.user2.a()});
        g_native.clear();
        g_now = 1100;
        FakeReq other(0xB0100555);
        hold_test::start_action(other.wrapper.a());
        CHECK(g_native.empty() && hold_test::char_size() == 0 && hold_test::char_counters().dropped == 1);
        hold_test::network_tick();
        CHECK(contains(hold_test::lines(), "dropped after 100 ms: observer out of range"));
    }
    {
        // Despawned (user 2's logout removed its character).
        World w;
        w.appear({kUser2Char});
        w.sam.remove(kUser2Char);
        g_native.clear();
        FakeReq other(0xB0100555);
        hold_test::start_action(other.wrapper.a());
        CHECK(g_native.empty() && hold_test::char_size() == 0 && hold_test::char_counters().dropped == 1);
    }
    {
        // Another guest epoch.
        World w;
        w.appear({kUser2Char});
        t_epoch = 2;
        g_native.clear();
        FakeReq other(0xB0100555);
        hold_test::start_action(other.wrapper.a());
        CHECK(g_native.empty() && hold_test::char_size() == 0 && hold_test::char_counters().dropped == 1);
    }
    {
        // User 2 leaving (its 2001 handed): the network tick drops its entries (both directions).
        World w;
        w.kliff1.set_restore(0);
        w.appear({kUser2Char, kHostChar});
        CHECK(hold_test::char_size() == 2);
        t_leaving = true;
        hold_test::network_tick();
        CHECK(hold_test::char_size() == 0 && hold_test::char_counters().cleared == 2);
        CHECK(contains(hold_test::lines(), "user 2 is leaving"));
    }
    {
        // The receiving thread's fallback only drops: a due release waits.
        World w;
        w.appear({kUser2Char});
        w.kliff2.set_restore(1);
        g_native.clear();
        g_now = 1000 + hold::kNpcFallbackMs;
        hold_test::server_tick();
        CHECK(g_native.empty() && hold_test::char_size() == 1 && hold_test::char_counters().deferred == 1);
        FakeReq other(0xB0100555);
        hold_test::start_action(other.wrapper.a());
        CHECK(native_has(0, {kS1}, {kUser2Char}) && hold_test::char_size() == 0);
    }
}

void test_native_instead_of_drop() {
    World w;
    w.appear({kUser2Char});
    // Another actor behind the key while the host still observes it: announced natively.
    FakeActor reborn(kUser2Char, w.user2.a(), kUser2Key, 1, 0);
    reborn.set_sector(&w.sector);
    w.sam.replace(w.slot2, reborn);
    g_native.clear();
    FakeReq other(0xB0100555);
    hold_test::start_action(other.wrapper.a());
    CHECK(native_has(0, {kS1}, {kUser2Char}) && hold_test::char_size() == 0 && hold_test::char_counters().native == 1);
    hold_test::network_tick();
    CHECK(contains(hold_test::lines(), "WRN Session2 character hold: released character 0xA010052C"));
}

void test_engine_announces_itself() {
    World w;
    w.appear({kUser2Char});
    // Its restore became 1 and the engine announces it to the host itself (another sector): forgotten.
    w.kliff2.set_restore(1);
    g_native.clear();
    w.appear({kUser2Char}, {kS1});
    CHECK(native_has(0, {kS1}, {kUser2Char}) && hold_test::char_size() == 0 &&
          hold_test::char_counters().forgotten == 1 && hold_test::char_counters().released == 0);
}

void test_pieces_and_children() {
    World w;
    // Its equipment piece (owner user 2, no CC; p8-crash-esc-1831598 0xB01002DA) passes; its
    // pet (D-53) is held as before; the parent is held: the piece reaches the host without it.
    FakeActor piece(0xB01002DA, w.user2.a(), 0, 7, 0);
    piece.no_cc();
    FakeActor pet(0xB0100269, w.user2.a(), kUser2Key, 6, 0);
    pet.set_sector(&w.sector);
    w.sam.add(piece);
    w.sam.add(pet);
    w.appear({kUser2Char, 0xB0100269, 0xB01002DA});
    CHECK(g_native.size() == 2 && native_has(0, {kS1, kS2}, {0xB01002DA}) &&
          native_has(1, {kS2}, {kUser2Char, 0xB0100269}));
    CHECK(hold_test::char_size() == 1 && hold_test::child_size() == 1);
    // child_hold=0 with character_hold=1: the pet is not held (ChildrenOff), the character is.
    World w2(false, false, true);
    FakeActor pet2(0xB0100269, w2.user2.a(), kUser2Key, 6, 0);
    w2.sam.add(pet2);
    w2.appear({kUser2Char, 0xB0100269});
    CHECK(native_has(0, {kS1, kS2}, {0xB0100269}) && native_has(1, {kS2}, {kUser2Char}) &&
          hold_test::char_size() == 1 && hold_test::child_size() == 0);
}

void test_room_reserved() {
    World w;
    hold_test::fill_char(hold::kCharTableCapacity);
    CHECK(hold_test::char_size() == hold::kCharTableCapacity);
    g_native.clear();
    w.appear({kUser2Char});
    // No room: left native (to both at once, restore 0, a warning), never withheld without an entry.
    CHECK(g_native.size() == 1 && native_has(0, {kS1, kS2}, {kUser2Char}) && hold_test::char_counters().full == 1);
    hold_test::network_tick();
    CHECK(contains(hold_test::lines(), "WRN Session2 character hold: table full"));
}

// The thread rule (p8-auditoria-hilos.md §4 case 3).
void test_thread_rule() {
    {
        // character_hold: the StartActionReq runs on thread 9, which never called NotifyAppear.
        World w;
        hold_test::set_thread_rule(true);
        g_tid = 7;
        w.appear({kUser2Char});  // thread 7: a native NotifyAppear thread now
        CHECK(hold_test::char_counters().native_threads == 1);
        w.kliff2.set_restore(1);
        g_native.clear();
        g_tid = 9;
        g_now = 1100;
        FakeReq act(kUser2Char);
        hold_test::start_action(act.wrapper.a());
        CHECK(g_native.empty() && hold_test::char_size() == 1 && hold_test::char_counters().deferred == 1);
        // The tail of the next native NotifyAppear (thread 7) releases it.
        g_tid = 7;
        g_now = 1100 + hold::kCharPassMs;
        w.appear({0xB0100777});
        CHECK(native_has(1, {kS1}, {kUser2Char}) && hold_test::char_size() == 0);
        hold_test::network_tick();
        CHECK(contains(hold_test::lines(), "calls NotifyAppear natively"));
    }
    {
        // D-53 (children): the same.
        World w(true, false, false);
        hold_test::set_thread_rule(true);
        FakeActor pet(0xB0100269, w.user2.a(), kUser2Key, 6, 0);
        pet.set_sector(&w.sector);
        w.sam.add(pet);
        w.appear({0xB0100269});
        CHECK(hold_test::child_size() == 1);
        pet.set_restore(1);
        g_native.clear();
        g_tid = 9;
        FakeReq act(0xB0100269);
        hold_test::start_action(act.wrapper.a());
        CHECK(g_native.empty() && hold_test::child_size() == 1 && hold_test::char_counters().child_deferred == 1);
        g_tid = 7;
        g_now = 1000 + 50;
        w.appear({0xB0100777});
        CHECK(native_has(1, {kS1}, {0xB0100269}) && hold_test::child_size() == 0 &&
              hold_test::char_counters().child_released == 1);
        // On a native thread the release happens right after the StartActionReq, as before.
        pet.set_restore(0);
        g_now = 2000;
        w.appear({0xB0100269});
        pet.set_restore(1);
        g_native.clear();
        hold_test::start_action(act.wrapper.a());
        CHECK(native_has(0, {kS1}, {0xB0100269}) && hold_test::child_size() == 0);
    }
    {
        // observer_hold (NPCs): the targeted pass on thread 9 defers; the tail on 7 releases.
        World w(true, true, false);
        hold_test::set_thread_rule(true);
        FakeActor crow(0xB01002A0, 0, kHostKey, 6, 0);
        crow.set_sector(&w.sector);
        w.sam.add(crow);
        w.appear({0xB01002A0});
        CHECK(hold_test::npc_size() == 1);
        crow.set_restore(1);
        g_native.clear();
        g_tid = 9;
        g_now = 1010;
        FakeReq act(0xB01002A0);
        hold_test::start_action(act.wrapper.a());
        CHECK(g_native.empty() && hold_test::npc_size() == 1 && hold_test::char_counters().npc_deferred == 1);
        g_tid = 7;
        g_now = 1010 + hold::kNpcPassMs;
        w.appear({0xB0100777});
        CHECK(native_has(1, {kS2}, {0xB01002A0}) && hold_test::npc_size() == 0);
    }
}

// notify_thread_rule=0 (the default, P8 review I3): the threads as before the rule.
void test_thread_rule_off() {
    {
        // D-53: the StartActionReq on thread 9 (never a NotifyAppear thread) releases at once, as before.
        World w(true, false, false);
        CHECK(!hold_test::char_counters().native_threads);
        FakeActor pet(0xB0100269, w.user2.a(), kUser2Key, 6, 0);
        pet.set_sector(&w.sector);
        w.sam.add(pet);
        w.appear({0xB0100269});
        CHECK(hold_test::child_size() == 1);
        pet.set_restore(1);
        g_native.clear();
        g_tid = 9;
        FakeReq act(0xB0100269);
        hold_test::start_action(act.wrapper.a());
        CHECK(native_has(0, {kS1}, {0xB0100269}) && hold_test::child_size() == 0 &&
              hold_test::char_counters().child_deferred == 0);
        // No children's sweep at the tail of NotifyAppear (none before the rule): held, then a native call.
        pet.set_restore(0);
        g_tid = 7;
        g_now = 2000;
        w.appear({0xB0100269});
        CHECK(hold_test::child_size() == 1);
        pet.set_restore(1);
        g_native.clear();
        g_now = 2100;
        w.appear({0xB0100777});
        CHECK(g_native.size() == 1 && hold_test::child_size() == 1);  // only the native call itself
        // The fallback (receiving thread) releases only on the last NotifyAppear caller (7 here).
        g_now = 2200;
        hold_test::server_tick();
        CHECK(native_has(1, {kS1}, {0xB0100269}) && hold_test::child_size() == 0);
    }
    {
        // observer_hold (NPCs): the StartActionReq's thread releases at once, nothing deferred for the thread.
        World w(true, true, false);
        FakeActor crow(0xB01002A0, 0, kHostKey, 6, 0);
        crow.set_sector(&w.sector);
        w.sam.add(crow);
        w.appear({0xB01002A0});
        crow.set_restore(1);
        g_native.clear();
        g_tid = 9;
        g_now = 1010;
        FakeReq act(0xB01002A0);
        hold_test::start_action(act.wrapper.a());
        CHECK(native_has(0, {kS2}, {0xB01002A0}) && hold_test::npc_size() == 0 &&
              hold_test::char_counters().npc_deferred == 0);
    }
    {
        // character_hold: the same, on thread 9.
        World w;
        w.appear({kUser2Char});
        w.kliff2.set_restore(1);
        g_native.clear();
        g_tid = 9;
        g_now = 1100;
        FakeReq act(kUser2Char);
        hold_test::start_action(act.wrapper.a());
        CHECK(native_has(0, {kS1}, {kUser2Char}) && hold_test::char_size() == 0 &&
              hold_test::char_counters().deferred == 0);
    }
}

// A due release left waiting for a thread that may call NotifyAppear: one WRN line past 2 s, one line when
// it runs (P8 review I3), whatever the rule.
void test_defer_warning() {
    {
        hold::DeferWatch d;
        CHECK(!d.poll(100, 2000).warn && d.open_ms(100) == 0);
        d.deferred(1000, 9, hold::DeferWatch::kChar);
        d.deferred(1500, 11, hold::DeferWatch::kNpc);
        CHECK(!d.poll(2999, 2000).warn && d.open_ms(2999) == 1999);
        const auto r = d.poll(3000, 2000);
        CHECK(r.warn && r.age_ms == 2000 && r.tid == 9 && r.count == 2 &&
              r.kinds == (hold::DeferWatch::kChar | hold::DeferWatch::kNpc));
        CHECK(!d.poll(5000, 2000).warn);  // once per episode
        d.ran(5000);
        const auto e = d.poll(5001, 2000);
        CHECK(e.resolved && e.age_ms == 4000 && !e.warn && d.max_ms() == 4000);
        CHECK(!d.poll(9000, 2000).warn && !d.poll(9000, 2000).resolved);
        d.deferred(10000, 3, hold::DeferWatch::kChild);
        d.ran(10500);  // under the limit: no line at all
        const auto q = d.poll(20000, 2000);
        CHECK(!q.warn && !q.resolved && d.max_ms() == 4000);
    }
    {
        // End to end, rule on: the deferred character release, no native NotifyAppear for 2.1 s.
        World w;
        hold_test::set_thread_rule(true);
        g_tid = 7;
        w.appear({kUser2Char});
        w.kliff2.set_restore(1);
        g_tid = 9;
        g_now = 1100;
        FakeReq act(kUser2Char);
        hold_test::start_action(act.wrapper.a());
        CHECK(hold_test::char_size() == 1 && hold_test::char_counters().deferred == 1);
        (void)hold_test::lines();
        g_now = 1100 + hold::kDeferWarnMs - 1;
        hold_test::network_tick();
        CHECK(!contains(hold_test::lines(), "a due release has waited"));
        g_now = 1100 + hold::kDeferWarnMs + 100;
        hold_test::network_tick();
        const std::string l = hold_test::lines();
        CHECK(contains(l, "WRN Session2 child hold: a due release has waited 2100 ms") && contains(l, "thread 9") &&
              contains(l, "characters") && contains(l, "notify_thread_rule=1"));
        g_tid = 7;
        g_now = 3500;
        w.appear({0xB0100777});
        CHECK(hold_test::char_size() == 0);
        hold_test::network_tick();
        CHECK(contains(hold_test::lines(), "the deferred release(s) ran after 2400 ms") &&
              hold_test::defer_max_ms() == 2400);
    }
}

// ---------------------------------------------------------------- observer_hold_acks with a character

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

// The split's native calls, with "another thread" emitting a per-actor packet of the withheld host
// character to user 2 while the owner's NotifyAppear runs (step 2).
void fake_notify_race(uintptr_t flag, const GameVec* s, const GameVec* k) {
    fake_notify_2005(flag, s, k);
    const auto* sd = static_cast<const uintptr_t*>(s->data);
    const auto* kd = static_cast<const uint32_t*>(k->data);
    if (s->count == 1 && sd[0] == kS1 && k->count == 1 && kd[0] == kHostChar) {
        take(actor_packet(2697, kHostChar, 7));
    }
}

void test_acks_with_character() {
    t_live_s2 = kS2;
    observer_hold_acks_configure(true);
    observer_hold_acks_clear(oha::Discard::Epoch);
    {
        // P8 review I5, window 2: the key is held from BEFORE the split's native calls: a packet of it emitted
        // meanwhile waits for its 2005 instead of reaching user 2 first.
        World w;
        w.seams.notify = fake_notify_race;
        w.kliff1.set_restore(0);
        g_out.clear();
        w.appear({kHostChar});
        CHECK(g_out.empty() && observer_hold_acks_keys() == 1);
        w.kliff1.set_restore(1);
        g_now = 1100;
        FakeReq act(kHostChar);
        hold_test::start_action(act.wrapper.a());
        CHECK(g_out.size() == 2);
        if (g_out.size() == 2) {
            CHECK(op_of(g_out[0]) == 2005 && key_of(g_out[0]) == kHostChar);
            CHECK(op_of(g_out[1]) == 2697 && g_out[1][9] == 7);
        }
        CHECK(observer_hold_acks_keys() == 0);
    }
    {
        // P8 review I5, window 1: a 2005 that ends a hold with NOTHING held is queued by the route itself
        // (Pushed, under its lock), so no packet of the key can be queued between the hold's end and it.
        observer_hold_acks_clear(oha::Discard::Epoch);
        g_out.clear();
        CHECK(observer_hold_acks_begin(0xB0100888, 1) == oha::Begin::Started);
        std::vector<uint8_t> a = announce({0xB0100888});
        const std::vector<uint8_t> copy = a;
        AckRouteOut o;
        CHECK(observer_hold_acks_route(2005, std::span<const uint8_t>(copy).subspan(5), a, &test_push, o) ==
                  AckRoute::Pushed &&
              o.self_pushed && o.extra_pushed == 0);
        CHECK(g_out.size() == 1 && op_of(g_out[0]) == 2005 && observer_hold_acks_keys() == 0);
        // A 2005 of keys without a hold: queued as usual by the caller (Pass).
        std::vector<uint8_t> b = announce({0xB0100889});
        const std::vector<uint8_t> copy_b = b;
        CHECK(observer_hold_acks_route(2005, std::span<const uint8_t>(copy_b).subspan(5), b, &test_push, o) ==
              AckRoute::Pass);
        CHECK(g_out.size() == 1);
    }
    {
        World w;
        w.seams.notify = fake_notify_2005;
        w.kliff1.set_restore(0);
        g_out.clear();
        w.appear({kHostChar});  // withheld from user 2: its packets wait
        CHECK(g_out.empty() && observer_hold_acks_keys() == 1);
        take(actor_packet(2697, kHostChar, 1));  // MoveActorAck
        take(actor_packet(2991, kHostChar, 2));  // StartActionAck
        take(actor_packet(2697, kUser2Char, 9)); // user 2's own character: passes
        CHECK(g_out.size() == 1 && key_of(g_out[0]) == kUser2Char);
        w.kliff1.set_restore(1);
        g_now = 1100;
        FakeReq act(kHostChar);
        hold_test::start_action(act.wrapper.a());
        CHECK(g_out.size() == 4);
        if (g_out.size() == 4) {
            CHECK(op_of(g_out[1]) == 2005 && key_of(g_out[1]) == kHostChar);
            CHECK(op_of(g_out[2]) == 2697 && g_out[2][9] == 1 && op_of(g_out[3]) == 2991 && g_out[3][9] == 2);
        }
        CHECK(observer_hold_acks_keys() == 0);
    }
    {
        // A dropped hold (user 2 left the host's range): what was kept goes, nothing is sent.
        World w;
        w.seams.notify = fake_notify_2005;
        w.kliff1.set_restore(0);
        g_out.clear();
        w.appear({kHostChar});
        take(actor_packet(2697, kHostChar, 1));
        CHECK(g_out.empty() && observer_hold_acks_keys() == 1);
        w.sector.set({w.host.a()});
        FakeReq other(0xB0100555);
        hold_test::start_action(other.wrapper.a());
        CHECK(g_out.empty() && observer_hold_acks_keys() == 0);
    }
    {
        // User 2's character withheld from the HOST: nothing waits here (the host's client is not routed).
        World w;
        w.seams.notify = fake_notify_2005;
        g_out.clear();
        w.appear({kUser2Char});
        CHECK(hold_test::char_size() == 1 && observer_hold_acks_keys() == 0);
    }
    observer_hold_acks_configure(false);
    t_live_s2 = 0;
    // The route's key: NPCs (as before) and characters; npc_key_of itself is unchanged.
    std::vector<uint8_t> payload(8, 0);
    put(payload.data(), 0, kHostChar);
    CHECK(oha::held_key_of(2697, payload) == std::optional<uint32_t>(kHostChar));
    CHECK(!oha::npc_key_of(2697, payload));
    put(payload.data(), 0, uint32_t{0x90100000});
    CHECK(!oha::held_key_of(2697, payload));
}

}  // namespace

int main() {
    test_predicate();
    test_release_rules();
    test_user2_char_held_from_host();
    test_host_char_held_from_user2();
    test_owner_never_held();
    test_off_unchanged();
    test_timeout_announces_with_restore0();
    test_drops();
    test_native_instead_of_drop();
    test_engine_announces_itself();
    test_pieces_and_children();
    test_room_reserved();
    test_thread_rule();
    test_thread_rule_off();
    test_defer_warning();
    test_acks_with_character();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_character_hold: all tests passed\n");
    return 0;
}
