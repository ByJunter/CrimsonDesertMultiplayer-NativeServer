// Option D, D-53: the child hold (src/mod/session2/child_hold.cpp) on fake game
// memory: the predicate (SAM lookup by key, owner = any validated user,
// restore byte), the split of a NotifyAppear call, the bounded (child,
// observer) table, the drop rules when user 2 leaves and the release rules.
// The hooks themselves need the game.

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "mod/session2/child_hold.h"
#include "mod/session2/guest_lifecycle.h"

namespace cdmp::session2 {
// The real ones are in server.cpp (need the game); the logic tested here does not use them.
bool server_users(uintptr_t&, uintptr_t&) { return false; }
bool server_host_user(uintptr_t&) { return false; }
bool server_user2(uintptr_t&) { return false; }
// P8 U-e (the 6c12c82 crash review): child_hold.cpp asks user 2's key and user2_stages (stubs).
uint32_t server_user2_user_key() { return 0; }
bool user2_stages_wants_start_action() noexcept { return false; }
void user2_stages_start_action_done(uint32_t, int32_t) noexcept {}
void user2_stages_npc_family_observed(uint32_t, uint32_t, uint32_t, uint8_t) noexcept {}
void user2_stages_npc_late_family(uint32_t, uint32_t, uint32_t) noexcept {}
uint64_t user2_stages_npc_birth_token(uint32_t) noexcept { return 100; }
bool server_prune_retired_outgoing() noexcept { return true; }
bool server_session_user(uintptr_t, uintptr_t&) { return false; }
uint32_t server_thread_id() { return 0; }
uint32_t server_epoch() { return 1; }
bool server_logout_handed() { return false; }
uintptr_t server_live_session2() { return 0; }  // observer_hold_acks (off here)
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

constexpr uintptr_t kUserVt = 0x1405B257A8;  // any value: compared, never followed
constexpr uint32_t kHostKey = 0x90100000, kUser2Key = 0x9010052B, kOtherKey = 0x90100777;

struct FakeUser : Blob {
    explicit FakeUser(uint32_t key, uintptr_t vt = kUserVt) : Blob(0x220) {
        put(p(), 0, vt);
        put(p(), hold::kUserKey, key);
    }
};

// A sector with its observer list {ServerUserActor*, refs}[n] (0x2AF2E60's read).
struct FakeSector : Blob {
    std::vector<uint8_t> obs = std::vector<uint8_t>(hold::kObserverStride * 8, 0);
    FakeSector() : Blob(0x100) { set({}); }
    void set(std::vector<uintptr_t> users) {
        for (size_t i = 0; i < users.size(); ++i) put(obs.data(), static_cast<uint32_t>(i * hold::kObserverStride), users[i]);
        put(p(), hold::kSectorObservers, addr(obs.data()));
        put(p(), hold::kSectorObserverCount, static_cast<uint32_t>(users.size()));
    }
};

// A server actor with components, CC (+ restore state), TransformSync and static info.
struct FakeActor : Blob {
    Blob comps{0x200}, cc{0x100}, restore{0x40}, ts{0x600}, info{0x10};
    FakeActor(uint32_t key, uintptr_t owner, uint8_t type, uint8_t restore_byte) : Blob(0x200) {
        put(p(), hold::kActorKey, key);
        put(p(), hold::kActorComponents, comps.a());
        put(p(), hold::kActorInfo, info.a());
        put(p(), hold::kActorOwner, owner);
        put(info.p(), hold::kInfoType, type);
        put(comps.p(), hold::kComponentsCc, cc.a());
        put(comps.p(), hold::kComponentsTs, ts.a());
        put(cc.p(), hold::kCcActor, a());
        put(cc.p(), hold::kCcRestore, restore.a());
        put(restore.p(), hold::kRestoreByte, restore_byte);
        put(ts.p(), hold::kTsActor, a());
    }
    void set_restore(uint8_t v) { put(restore.p(), hold::kRestoreByte, v); }
    void set_sector(FakeSector* s) { put(ts.p(), hold::kTsSector, s ? s->a() : uintptr_t{0}); }
};

// The ServerActorManager's key map, as cdmp_SAM__FindActor 0x2A827B0 reads it.
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
    void add(FakeActor& actor) {
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
    }
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

void test_owner_who() {
    FakeUser host(kHostKey), user2(kUser2Key), other(kOtherKey), not_user(kOtherKey, 0x1234);
    FakeUser host_copy(kHostKey);  // the host's key on another pointer
    const uintptr_t u2 = user2.a(), h = host.a();
    CHECK(hold_owner_who(0, u2, h, kHostKey, kUserVt) == OwnerWho::None);
    CHECK(hold_owner_who(u2, u2, h, kHostKey, kUserVt) == OwnerWho::Guest);
    CHECK(hold_owner_who(h, u2, h, kHostKey, kUserVt) == OwnerWho::Host);
    // Fail safe, as the clock gate: session 2's lookup missed, or another user.
    CHECK(hold_owner_who(u2, 0, h, kHostKey, kUserVt) == OwnerWho::Guest);
    CHECK(hold_owner_who(other.a(), u2, h, kHostKey, kUserVt) == OwnerWho::Guest);
    CHECK(hold_owner_who(h, 0, h, kHostKey, kUserVt) == OwnerWho::Host);
    // The host only by its own pointer: its key elsewhere, or the host's
    // pointer not read now, is left native (its session must find it).
    CHECK(hold_owner_who(host_copy.a(), u2, h, kHostKey, kUserVt) == OwnerWho::None);
    CHECK(hold_owner_who(h, u2, 0, kHostKey, kUserVt) == OwnerWho::None);
    // Not a ServerUserActor: left native.
    CHECK(hold_owner_who(not_user.a(), u2, h, kHostKey, kUserVt) == OwnerWho::None);
    // The host not known yet: only user 2 itself.
    CHECK(hold_owner_who(other.a(), 0, 0, 0, kUserVt) == OwnerWho::None);
    CHECK(hold_owner_who(u2, u2, 0, 0, kUserVt) == OwnerWho::Guest);
    // Unreadable owner.
    CHECK(hold_owner_who(0x10, u2, h, kHostKey, kUserVt) == OwnerWho::None);
    CHECK(std::strcmp(owner_who_name(OwnerWho::Host), "host") == 0);
    CHECK(std::strcmp(owner_who_name(OwnerWho::Guest), "user 2") == 0);
}

void test_predicate() {
    FakeUser host(kHostKey), user2(kUser2Key);
    const uintptr_t u2 = user2.a(), h = host.a();
    FakeSam sam;
    FakeActor child(0xB010052D, u2, 6, 0);        // user 2's pet, no action yet (C1g #7431)
    FakeActor host_child(0xB0100002, host.a(), 6, 0);  // a pet the host just summoned
    FakeActor npc(0xB01005B2, 0, 3, 0);
    FakeActor kliff2(0xA010052C, u2, 1, 0);       // user 2's character: never held
    FakeActor restored(0xB0100244, u2, 6, 1);
    FakeActor no_cc(0xB0100245, u2, 5, 0);
    put(no_cc.cc.p(), hold::kCcActor, uintptr_t{0x1234});  // the CC is not this actor's
    for (FakeActor* a : {&child, &host_child, &npc, &kliff2, &restored, &no_cc}) sam.add(*a);

    HoldVerdict v = hold_verdict(sam.a(), 0xB010052D, u2, h, kHostKey, kUserVt);
    CHECK(v.hold && v.why == HoldWhy::Hold && v.actor == child.a() && v.owner == u2 && v.type == 6 &&
          v.owner_who == OwnerWho::Guest);
    // The host's own child with restore 0: held as well (G1, host side).
    v = hold_verdict(sam.a(), 0xB0100002, u2, h, kHostKey, kUserVt);
    CHECK(v.hold && v.why == HoldWhy::Hold && v.owner == h && v.owner_who == OwnerWho::Host);
    // ...also with user 2 not in (it only matters once someone else receives it).
    CHECK(hold_verdict(sam.a(), 0xB0100002, 0, h, kHostKey, kUserVt).hold);
    // ...but not when the host's user cannot be read now: left native.
    CHECK(hold_verdict(sam.a(), 0xB0100002, u2, 0, kHostKey, kUserVt).why == HoldWhy::NotUser);
    // An owner that is not a user at all: left native.
    FakeUser not_user(kOtherKey, 0x1234);
    FakeActor odd(0xB0100003, not_user.a(), 6, 0);
    sam.add(odd);
    CHECK(hold_verdict(sam.a(), 0xB0100003, u2, h, kHostKey, kUserVt).why == HoldWhy::NotUser);
    CHECK(hold_verdict(sam.a(), 0xB01005B2, u2, h, kHostKey, kUserVt).why == HoldWhy::NoOwner);
    CHECK(hold_verdict(sam.a(), 0xA010052C, u2, h, kHostKey, kUserVt).why == HoldWhy::NotChildKey);
    CHECK(hold_verdict(sam.a(), 0x9010052B, u2, h, kHostKey, kUserVt).why == HoldWhy::NotChildKey);
    CHECK(hold_verdict(sam.a(), 0xB0100244, u2, h, kHostKey, kUserVt).why == HoldWhy::Restored);
    CHECK(hold_verdict(sam.a(), 0xB0100245, u2, h, kHostKey, kUserVt).why == HoldWhy::NoCc);
    CHECK(hold_verdict(sam.a(), 0xB0100999, u2, h, kHostKey, kUserVt).why == HoldWhy::NotFound);
    // Same index, another key (the index was reused): not this child.
    CHECK(hold_verdict(sam.a(), 0xB110052D, u2, h, kHostKey, kUserVt).why == HoldWhy::NotFound);
    // Fail safe: user 2's session not found, the host known -> still held.
    CHECK(hold_verdict(sam.a(), 0xB010052D, 0, h, kHostKey, kUserVt).hold);
    // Nothing known: nothing held.
    CHECK(!hold_verdict(sam.a(), 0xB010052D, 0, 0, 0, kUserVt).hold);
    CHECK(!hold_verdict(sam.a(), 0xB0100002, 0, 0, 0, kUserVt).hold);
    // The host's child once it acted (its client's first 2223): not held.
    host_child.set_restore(1);
    CHECK(hold_verdict(sam.a(), 0xB0100002, u2, h, kHostKey, kUserVt).why == HoldWhy::Restored);
    // Unreadable map: unchanged.
    CHECK(hold_verdict(0, 0xB010052D, u2, h, kHostKey, kUserVt).why == HoldWhy::Unreadable);
    // Once its first action is accepted (0x2328894), it is not held any more.
    child.set_restore(1);
    CHECK(hold_verdict(sam.a(), 0xB010052D, u2, h, kHostKey, kUserVt).why == HoldWhy::Restored);

    // The read helpers.
    CHECK(child_restore_byte(child.a()) == std::optional<uint8_t>(uint8_t{1}));
    CHECK(!child_restore_byte(no_cc.a()));
    CHECK(!child_restore_byte(0));
    FakeSector sector;
    child.set_sector(&sector);
    CHECK(child_sector(child.a()) == std::optional<uintptr_t>(sector.a()));
    child.set_sector(nullptr);
    CHECK(child_sector(child.a()) == std::optional<uintptr_t>(0));
    put(child.ts.p(), hold::kTsActor, uintptr_t{0x1234});
    CHECK(!child_sector(child.a()));
    sector.set({u2, host.a()});
    CHECK(sector_observed_by(sector.a(), host.a()) == std::optional<bool>(true));
    CHECK(sector_observed_by(sector.a(), 0x5555) == std::optional<bool>(false));
    CHECK(sector_observed_by(0, host.a()) == std::optional<bool>(false));
    put(sector.p(), hold::kSectorObserverCount, uint32_t{100000});  // torn
    CHECK(!sector_observed_by(sector.a(), host.a()));
}

HoldVerdict held(uintptr_t owner, uint8_t type = 6, OwnerWho who = OwnerWho::Guest) {
    HoldVerdict v;
    v.hold = true;
    v.why = HoldWhy::Hold;
    v.owner = owner;
    v.owner_who = who;
    v.type = type;
    return v;
}

void test_split() {
    constexpr uintptr_t s1 = 0x1000, s2 = 0x2000, host = 0xA000, u2 = 0xB000;
    const HoldVerdict pass{};
    SplitPlan plan;
    {
        // C1g #7431: the child placed in a sector both users observe.
        const uintptr_t sessions[] = {s1, s2};
        const uintptr_t users[] = {host, u2};
        const uint32_t keys[] = {0xB010052D};
        const HoldVerdict v[] = {held(u2)};
        CHECK(plan_split(sessions, users, host, keys, v, plan));
        CHECK(plan.pass.empty());
        CHECK(plan.groups.size() == 1 && plan.groups[0].owner == u2);
        CHECK(plan.groups[0].sessions == std::vector<uintptr_t>{s2});
        CHECK(plan.groups[0].keys == std::vector<uint32_t>{0xB010052D});
        CHECK(plan.records.size() == 1 && plan.records[0].key == 0xB010052D && plan.records[0].observer == s1 &&
              plan.records[0].observer_user == host && plan.records[0].owner == u2 && plan.records[0].type == 6 &&
              plan.records[0].owner_who == OwnerWho::Guest && plan.records[0].observer_host);
    }
    {
        // G1 (host side): the host summons a horse while user 2 is in. The
        // host's own session gets it at once; user 2's session is noted.
        const uintptr_t sessions[] = {s1, s2};
        const uintptr_t users[] = {host, u2};
        const uint32_t keys[] = {0xB0100002};
        const HoldVerdict v[] = {held(host, 4, OwnerWho::Host)};
        CHECK(plan_split(sessions, users, host, keys, v, plan));
        CHECK(plan.pass.empty());
        CHECK(plan.groups.size() == 1 && plan.groups[0].owner == host);
        CHECK(plan.groups[0].sessions == std::vector<uintptr_t>{s1});
        CHECK(plan.groups[0].keys == std::vector<uint32_t>{0xB0100002});
        CHECK(plan.records.size() == 1 && plan.records[0].observer == s2 && plan.records[0].observer_user == u2 &&
              plan.records[0].owner == host && plan.records[0].owner_who == OwnerWho::Host &&
              !plan.records[0].observer_host);
    }
    {
        // User 2 enters the host's sectors (second call, only its session):
        // the host's restore-0 child is withheld from it, the rest passes.
        const uintptr_t sessions[] = {s2};
        const uintptr_t users[] = {u2};
        const uint32_t keys[] = {0xA0100001, 0xB0100002, 0xB01005B2, 0xB010052D};
        const HoldVerdict v[] = {pass, held(host, 4, OwnerWho::Host), pass, held(u2)};
        CHECK(plan_split(sessions, users, host, keys, v, plan));
        CHECK((plan.pass == std::vector<uint32_t>{0xA0100001, 0xB01005B2, 0xB010052D}));  // its own child passes
        CHECK(plan.groups.size() == 1 && plan.groups[0].owner == host && plan.groups[0].sessions.empty());
        CHECK(plan.records.size() == 1 && plan.records[0].key == 0xB0100002 && plan.records[0].observer == s2);
    }
    {
        // Single player (user 2 not in): the host alone receives its own child: unchanged.
        const uintptr_t sessions[] = {s1};
        const uintptr_t users[] = {host};
        const uint32_t keys[] = {0xB0100002, 0xB01005B2};
        const HoldVerdict v[] = {held(host, 4, OwnerWho::Host), pass};
        CHECK(!plan_split(sessions, users, host, keys, v, plan));
    }
    {
        // Both owners in one list (a sector both observe): each owner gets its
        // own child at once, each other session is noted for it.
        const uintptr_t sessions[] = {s1, s2};
        const uintptr_t users[] = {host, u2};
        const uint32_t keys[] = {0xB0100002, 0xB01005B2, 0xB010052D};
        const HoldVerdict v[] = {held(host, 4, OwnerWho::Host), pass, held(u2)};
        CHECK(plan_split(sessions, users, host, keys, v, plan));
        CHECK(plan.pass == std::vector<uint32_t>{0xB01005B2});  // the NPC to everyone
        CHECK(plan.groups.size() == 2);
        CHECK(plan.groups[0].owner == host && plan.groups[0].sessions == std::vector<uintptr_t>{s1});
        CHECK(plan.groups[1].owner == u2 && plan.groups[1].sessions == std::vector<uintptr_t>{s2});
        CHECK(plan.records.size() == 2);
        CHECK(plan.records[0].key == 0xB0100002 && plan.records[0].observer == s2 && !plan.records[0].observer_host);
        CHECK(plan.records[1].key == 0xB010052D && plan.records[1].observer == s1 && plan.records[1].observer_host);
    }
    {
        // A held verdict without an owner cannot be withheld from anyone: it passes.
        const uintptr_t sessions[] = {s1, s2};
        const uintptr_t users[] = {host, u2};
        const uint32_t keys[] = {0xB0100009};
        const HoldVerdict v[] = {held(0)};
        CHECK(!plan_split(sessions, users, host, keys, v, plan));
    }
    {
        // A mixed list keeps the others in their order; nulls are skipped.
        const uintptr_t sessions[] = {s1, 0, s2};
        const uintptr_t users[] = {host, 0, u2};
        const uint32_t keys[] = {0x9010052B, 0xA010052C, 0xB010052D, 0xB0100002, 0xB010052E};
        const HoldVerdict v[] = {pass, pass, held(u2), pass, held(u2, 5)};
        CHECK(plan_split(sessions, users, host, keys, v, plan));
        CHECK((plan.pass == std::vector<uint32_t>{0x9010052B, 0xA010052C, 0xB0100002}));
        CHECK(plan.groups.size() == 1 && (plan.groups[0].keys == std::vector<uint32_t>{0xB010052D, 0xB010052E}));
        CHECK(plan.groups[0].sessions == std::vector<uintptr_t>{s2});
        CHECK(plan.records.size() == 2 && plan.records[1].key == 0xB010052E && plan.records[1].observer == s1 &&
              plan.records[1].type == 5);
    }
    {
        // Only the owner receives it (its own parking field, or the moving user
        // is the owner itself): nothing to withhold, unchanged.
        const uintptr_t sessions[] = {s2};
        const uintptr_t users[] = {u2};
        const uint32_t keys[] = {0xB010052D, 0xB0100002};
        const HoldVerdict v[] = {held(u2), pass};
        CHECK(!plan_split(sessions, users, host, keys, v, plan));
        CHECK(plan.pass.empty() && plan.groups.empty() && plan.records.empty());
    }
    {
        // The host enters sectors where the child is (second call): the owner is
        // not a recipient; the child goes to nobody now, the host is noted.
        const uintptr_t sessions[] = {s1};
        const uintptr_t users[] = {host};
        const uint32_t keys[] = {0xB01005B2, 0xB010052D};
        const HoldVerdict v[] = {pass, held(u2)};
        CHECK(plan_split(sessions, users, host, keys, v, plan));
        CHECK(plan.pass == std::vector<uint32_t>{0xB01005B2});
        CHECK(plan.groups.size() == 1 && plan.groups[0].sessions.empty());
        CHECK(plan.records.size() == 1 && plan.records[0].observer == s1);
    }
    {
        // Nothing held: unchanged.
        const uintptr_t sessions[] = {s1, s2};
        const uintptr_t users[] = {host, u2};
        const uint32_t keys[] = {0xB0100002};
        const HoldVerdict v[] = {pass};
        CHECK(!plan_split(sessions, users, host, keys, v, plan));
    }
    {
        // The same session twice: one owner session, one record.
        const uintptr_t sessions[] = {s1, s2, s1, s2};
        const uintptr_t users[] = {host, u2, host, u2};
        const uint32_t keys[] = {0xB010052D};
        const HoldVerdict v[] = {held(u2)};
        CHECK(plan_split(sessions, users, host, keys, v, plan));
        CHECK(plan.groups[0].sessions == std::vector<uintptr_t>{s2});
        CHECK(plan.records.size() == 1);
    }
    {
        // Mismatched inputs: unchanged.
        const uintptr_t sessions[] = {s1, s2};
        const uintptr_t users[] = {host};
        const uint32_t keys[] = {0xB010052D};
        const HoldVerdict v[] = {held(u2)};
        CHECK(!plan_split(sessions, users, host, keys, v, plan));
    }
}

HoldEntry entry(uint32_t key, uintptr_t observer, uint64_t since = 0) {
    HoldEntry e;
    e.key = key;
    e.observer = observer;
    e.observer_user = observer + 1;
    e.owner = 0xB000;
    e.epoch = 1;
    e.since_ms = since;
    return e;
}

void test_table() {
    HoldTable t;
    CHECK(t.add(entry(0xB010052D, 0x1000, 100)) == HoldTable::Add::Added);
    CHECK(t.add(entry(0xB010052D, 0x1000, 900)) == HoldTable::Add::Duplicate);
    CHECK(t.size() == 1 && t.at(0).since_ms == 100);  // the first moment stays
    CHECK(t.add(entry(0xB010052D, 0x3000, 200)) == HoldTable::Add::Added);
    CHECK(t.size() == 2);
    CHECK(t.older_than(3100, 3000) == 1);
    CHECK(t.older_than(3200, 3000) == 2);
    // The engine announced it to 0x3000 itself.
    const uint32_t keys[] = {0x11, 0xB010052D};
    const uintptr_t sessions[] = {0x3000};
    CHECK(t.forget_announced(keys, sessions) == 1);
    CHECK(t.size() == 1 && t.at(0).observer == 0x1000);
    t.clear();
    CHECK(t.size() == 0);
    // Bounded: 64 entries, then Full.
    for (uint32_t i = 0; i < hold::kTableCapacity; ++i) CHECK(t.add(entry(0xB0100000 + i, 0x1000)) == HoldTable::Add::Added);
    CHECK(t.add(entry(0xB0200000, 0x1000)) == HoldTable::Add::Full);
    CHECK(t.add(entry(0xB0100005, 0x1000)) == HoldTable::Add::Duplicate);
    CHECK(t.size() == hold::kTableCapacity);
    t.remove_at(0);
    CHECK(t.size() == hold::kTableCapacity - 1 && t.at(0).key == 0xB0100000 + hold::kTableCapacity - 1);
    t.remove_at(1000);  // out of range: nothing
    CHECK(t.size() == hold::kTableCapacity - 1);
    CHECK(t.add(entry(0xB0200000, 0x1000)) == HoldTable::Add::Added);
    // D-53 leftover: user 2's entries go when user 2 is gone or its 2001 was
    // handed (after the logout the user may still read as valid).
    CHECK(!hold_drop_user2(true, false));
    CHECK(hold_drop_user2(false, false));
    CHECK(hold_drop_user2(true, true));
    CHECK(hold_drop_user2(false, true));
}

void test_drop_user2() {
    // user 2's child withheld from the host; the host's child withheld from user 2.
    HoldEntry of_u2 = entry(0xB010052D, 0x1000);
    of_u2.owner_who = OwnerWho::Guest;
    of_u2.observer_host = true;
    HoldEntry to_u2 = entry(0xB0100002, 0x2000);
    to_u2.owner = 0xA000;
    to_u2.owner_who = OwnerWho::Host;
    to_u2.observer_host = false;
    // Fail safe "another user" (user 2's lookup missed): also user 2's.
    HoldEntry other = entry(0xB0100777, 0x1000);
    other.owner_who = OwnerWho::Guest;
    other.observer_host = true;
    // Host-owned and observed by the host: cannot be recorded (the owner's
    // session always gets it); kept by the rule if it ever were.
    HoldEntry host_host = entry(0xB0100003, 0x1000);
    host_host.owner_who = OwnerWho::Host;
    host_host.observer_host = true;
    CHECK(hold_entry_of_user2(of_u2));
    CHECK(hold_entry_of_user2(to_u2));
    CHECK(hold_entry_of_user2(other));
    CHECK(!hold_entry_of_user2(host_host));

    HoldTable t;
    CHECK(t.add(of_u2) == HoldTable::Add::Added);
    CHECK(t.add(to_u2) == HoldTable::Add::Added);
    CHECK(t.add(other) == HoldTable::Add::Added);
    CHECK(t.add(host_host) == HoldTable::Add::Added);
    CHECK(t.drop_user2_entries() == 3);
    CHECK(t.size() == 1 && t.at(0).key == 0xB0100003);
    CHECK(t.drop_user2_entries() == 0);
    // Two users: every real entry involves user 2, so the table empties.
    t.clear();
    CHECK(t.add(of_u2) == HoldTable::Add::Added);
    CHECK(t.add(to_u2) == HoldTable::Add::Added);
    CHECK(t.drop_user2_entries() == 2 && t.size() == 0);
    // A duplicate keeps the latest owner kind (the same child, re-held).
    CHECK(t.add(to_u2) == HoldTable::Add::Added);
    HoldEntry again = to_u2;
    again.owner_who = OwnerWho::Guest;
    CHECK(t.add(again) == HoldTable::Add::Duplicate && t.at(0).owner_who == OwnerWho::Guest);
}

void test_release() {
    const HoldEntry e = entry(0xB010052D, 0x1000);
    {
        // A host-owned child withheld from user 2: the same release rules.
        HoldEntry hc = entry(0xB0100002, 0x2000);
        hc.owner = 0xA000;
        hc.owner_who = OwnerWho::Host;
        ReleaseReadings r;
        r.epoch_now = 1;
        r.actor = uintptr_t{0x7000};
        r.owner = hc.owner;
        r.restore = uint8_t{0};
        r.observer_valid = true;
        r.observer_user = hc.observer_user;
        r.observes = true;
        const char* w = "";
        CHECK(release_step(hc, r, w) == ReleaseStep::Keep);  // until the host's client gives it an action
        r.restore = uint8_t{1};
        CHECK(release_step(hc, r, w) == ReleaseStep::Release);
        r.epoch_now = 2;  // user 2's epoch ended: never to the next one
        CHECK(release_step(hc, r, w) == ReleaseStep::Drop);
    }
    auto ready = [&] {
        ReleaseReadings r;
        r.epoch_now = 1;
        r.actor = uintptr_t{0x7000};
        r.owner = e.owner;
        r.restore = uint8_t{1};
        r.observer_valid = true;
        r.observer_user = e.observer_user;
        r.observes = true;
        return r;
    };
    const char* why = "";
    CHECK(release_step(e, ready(), why) == ReleaseStep::Release);
    ReleaseReadings r = ready();
    r.restore = uint8_t{0};
    CHECK(release_step(e, r, why) == ReleaseStep::Keep);  // never with restore 0
    r = ready();
    r.restore.reset();
    CHECK(release_step(e, r, why) == ReleaseStep::Keep);
    r = ready();
    r.restore = uint8_t{2};
    CHECK(release_step(e, r, why) == ReleaseStep::Keep);
    r = ready();
    r.epoch_now = 2;
    CHECK(release_step(e, r, why) == ReleaseStep::Drop);
    r = ready();
    r.actor = uintptr_t{0};
    CHECK(release_step(e, r, why) == ReleaseStep::Drop);  // gone
    r = ready();
    r.actor.reset();
    CHECK(release_step(e, r, why) == ReleaseStep::Keep);  // unreadable: ask again later
    r = ready();
    r.owner = uintptr_t{0xC000};
    CHECK(release_step(e, r, why) == ReleaseStep::Drop);
    r = ready();
    r.observer_valid = false;
    CHECK(release_step(e, r, why) == ReleaseStep::Drop);
    r = ready();
    r.observer_user = 0x9999;  // the session has another user now
    CHECK(release_step(e, r, why) == ReleaseStep::Drop);
    r = ready();
    r.observes = false;
    CHECK(release_step(e, r, why) == ReleaseStep::Drop);
    r = ready();
    r.observes.reset();
    CHECK(release_step(e, r, why) == ReleaseStep::Keep);

    // The readings, by key on fake memory.
    FakeUser host(kHostKey), user2(kUser2Key);
    FakeSam sam;
    FakeActor child(0xB010052D, user2.a(), 6, 0);
    FakeSector sector;
    sector.set({user2.a(), host.a()});
    child.set_sector(&sector);
    sam.add(child);
    ReleaseReadings rr = read_child_release(sam.a(), 0xB010052D, host.a());
    CHECK(rr.actor == std::optional<uintptr_t>(child.a()) && rr.owner == std::optional<uintptr_t>(user2.a()));
    CHECK(rr.restore == std::optional<uint8_t>(uint8_t{0}) && rr.observes == std::optional<bool>(true));
    child.set_restore(1);
    sector.set({user2.a()});
    rr = read_child_release(sam.a(), 0xB010052D, host.a());
    CHECK(rr.restore == std::optional<uint8_t>(uint8_t{1}) && rr.observes == std::optional<bool>(false));
    sam.remove(0xB010052D);
    rr = read_child_release(sam.a(), 0xB010052D, host.a());
    CHECK(rr.actor == std::optional<uintptr_t>(0) && !rr.restore && !rr.observes);

    // The host's horse, held from user 2 and released once it acted.
    FakeActor horse(0xB0100002, host.a(), 4, 0);
    FakeSector shared;
    shared.set({host.a(), user2.a()});
    horse.set_sector(&shared);
    sam.add(horse);
    const HoldVerdict hv = hold_verdict(sam.a(), 0xB0100002, user2.a(), host.a(), kHostKey, kUserVt);
    CHECK(hv.hold && hv.owner_who == OwnerWho::Host);
    rr = read_child_release(sam.a(), 0xB0100002, user2.a());
    CHECK(rr.restore == std::optional<uint8_t>(uint8_t{0}) && rr.observes == std::optional<bool>(true) &&
          rr.owner == std::optional<uintptr_t>(host.a()));
    horse.set_restore(1);
    rr = read_child_release(sam.a(), 0xB0100002, user2.a());
    HoldEntry he;
    he.key = 0xB0100002;
    he.observer = 0x2000;
    he.observer_user = user2.a();
    he.owner = host.a();
    he.owner_who = OwnerWho::Host;
    he.epoch = 1;
    rr.epoch_now = 1;
    rr.observer_valid = true;
    rr.observer_user = user2.a();
    const char* w = "";
    CHECK(release_step(he, rr, w) == ReleaseStep::Release);
    CHECK(!hold_verdict(sam.a(), 0xB0100002, user2.a(), host.a(), kHostKey, kUserVt).hold);
}

}  // namespace

int main() {
    test_owner_who();
    test_predicate();
    test_split();
    test_table();
    test_drop_user2();
    test_release();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_child_hold: all tests passed\n");
    return 0;
}
