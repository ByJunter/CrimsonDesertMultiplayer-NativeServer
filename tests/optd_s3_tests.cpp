// S3 (docs/research/pendientes/p8-impl-s3.md): the pure logic of
//  - the host's observer hold (child_hold.cpp, observer_hold): the NPC
//    predicate by controller, the split, the bounded NPC table, the drop on
//    user 2's departure and the release rules (despawn, another actor behind
//    the key, owner, observer, epoch, timeout, controller change, restore 1),
//    and, through child_hold.h's test seams (a fake NotifyAppear, SAM, clock
//    and realm), the hooks' own work end to end: the split with its room
//    reserved, record(), the targeted pass after a StartActionReq (its key
//    read from the packet wrapper), the budgeted pass at most every 100 ms
//    (also after NotifyAppear), the timeout and drop-to-native releases, the
//    fallback that only drops, user 2 leaving, and the lines formatted by the
//    network tick;
//  - the bounded lock-free sets once full and the crash capture's gate;
//  - the guest's probes (guest_s3_logic.h): the lock-free ring's bounds (also
//    with concurrent writers), the key set, the update guard's per-key table
//    and predicate, 0x863530's caller ranges, and the 2005 parser on two
//    whole payloads of the P8 session-4 guest capture (tests/data,
//    cdmp_packets_20261009-170454.bin records #12334 = Crow B01002A0 with
//    restore 0, and #1191 = four NPCs with restore 1) plus synthetic edge
//    cases;
//  - the guest's exit (pendientes/p8-salida-invitado.md): the goodbye decision
//    (guest_goodbye_logic.h: local answer at the title, never with a field of
//    the host; the end of the process when the host cannot answer) and what
//    the top-level filter does after its capture (s3::uef_end).
// The hooks themselves need the game.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <optional>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "mod/session2/child_hold.h"
#include "mod/session2/family_order_logic.h"
#include "mod/session2/guest_goodbye_logic.h"
#include "mod/session2/guest_lifecycle.h"
#include "mod/session2/guest_s3_logic.h"
#include "mod/session2/observer_hold_acks.h"
#include "mod/session2/npc_retirement_logic.h"
#include "mod/session2/publication_gate_logic.h"

namespace cdmp::session2 {
// The real ones are in server.cpp (need the game). The integration tests of
// the observer hold (record + passes) set who the users and sessions are.
uintptr_t t_host = 0, t_user2 = 0;
std::vector<std::pair<uintptr_t, uintptr_t>> t_sessions;  // session -> its user
bool t_leaving = false;
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
uint32_t t_epoch = 1;  // S35 review: a retained entry across a new guest epoch
uint32_t server_epoch() { return t_epoch; }
bool server_logout_handed() { return t_leaving; }
// observer_hold_acks (observer_hold_acks.h): the live session 2 its holds are for (0 = none).
uintptr_t t_live_s2 = 0;
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
constexpr uint32_t kHostKey = 0x90100000, kUser2Key = 0x9010052B, kOtherKey = 0x90100777;

struct FakeUser : Blob {
    explicit FakeUser(uint32_t key, uintptr_t vt = kUserVt) : Blob(0x220) {
        put(p(), 0, vt);
        put(p(), hold::kUserKey, key);
    }
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

// A server actor: owner +0xA0, controller +0x90, CC chain with the restore byte, TS with the sector.
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
    void set_controller(uint32_t v) { put(p(), hold::kActorController, v); }
    void set_owner(uintptr_t v) { put(p(), hold::kActorOwner, v); }
    void set_sector(FakeSector* s) { put(ts.p(), hold::kTsSector, s ? s->a() : uintptr_t{0}); }
    // observer_hold_pieces: no control component (a carried piece: NoCc), and the hierarchy component
    // [comps+0x1B8] naming its parent at +0x1C, its own actor at +8 (`back`, 0 = this actor).
    Blob hier{0x40};
    void no_cc() { put(comps.p(), hold::kComponentsCc, uintptr_t{0}); }
    void set_parent(uint32_t parent, uintptr_t back = 0) {
        put(comps.p(), hold::kComponentsHierarchy, hier.a());
        put(hier.p(), hold::kHierActor, back ? back : a());
        put(hier.p(), hold::kHierParent, parent);
    }
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
    // Another actor behind the same slot (despawn + respawn with the same key).
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

// ---------------------------------------------------------------- host: observer hold

void test_npc_predicate() {
    FakeUser host(kHostKey), user2(kUser2Key), other(kOtherKey);
    const uintptr_t h = host.a(), u2 = user2.a();
    FakeSam sam;
    FakeActor crow(0xB01002A0, 0, kHostKey, 6, 0);       // session 4's Crow: owner 0, host controls, restore 0
    FakeActor guard(0xB0100078, 0, kHostKey, 3, 1);      // an NPC that already acted
    FakeActor u2npc(0xB0100500, 0, kUser2Key, 3, 0);     // an NPC user 2's client would control
    FakeActor server_npc(0xB0100501, 0, 0, 3, 0);        // the server's own (controller 0)
    FakeActor stranger(0xB0100502, 0, kOtherKey, 3, 0);  // a controller nobody knows
    FakeActor gimmick(0xB0100503, 0, kHostKey, 7, 0);    // no CC
    put(gimmick.cc.p(), hold::kCcActor, uintptr_t{0x1234});
    FakeActor pet(0xB0100002, h, kHostKey, 6, 0);        // a user's child (D-53's case)
    for (FakeActor* a : {&crow, &guard, &u2npc, &server_npc, &stranger, &gimmick, &pet}) sam.add(*a);

    ObserverHoldUsers ou{h, kHostKey, u2, kUser2Key};
    // Unchanged without observer_hold: an NPC is NoOwner.
    CHECK(hold_verdict(sam.a(), 0xB01002A0, u2, h, kHostKey, kUserVt).why == HoldWhy::NoOwner);
    CHECK(hold_verdict_ex(sam.a(), 0xB01002A0, u2, h, kHostKey, kUserVt, true, nullptr).why == HoldWhy::NoOwner);

    HoldVerdict v = hold_verdict_ex(sam.a(), 0xB01002A0, u2, h, kHostKey, kUserVt, true, &ou);
    CHECK(v.hold && v.npc && v.why == HoldWhy::Hold && v.owner == h && v.owner_who == OwnerWho::Host &&
          v.controller == kHostKey && v.type == 6 && v.actor == crow.a());
    v = hold_verdict_ex(sam.a(), 0xB0100500, u2, h, kHostKey, kUserVt, true, &ou);
    CHECK(v.hold && v.npc && v.owner == u2 && v.owner_who == OwnerWho::Guest);
    CHECK(hold_verdict_ex(sam.a(), 0xB0100078, u2, h, kHostKey, kUserVt, true, &ou).why == HoldWhy::Restored);
    CHECK(hold_verdict_ex(sam.a(), 0xB0100501, u2, h, kHostKey, kUserVt, true, &ou).why == HoldWhy::CtrlNotUser);
    CHECK(hold_verdict_ex(sam.a(), 0xB0100502, u2, h, kHostKey, kUserVt, true, &ou).why == HoldWhy::CtrlNotUser);
    CHECK(hold_verdict_ex(sam.a(), 0xB0100503, u2, h, kHostKey, kUserVt, true, &ou).why == HoldWhy::NoCc);
    // The host's user pointer not read now: its NPCs are left native.
    ObserverHoldUsers no_host{0, 0, u2, kUser2Key};
    CHECK(hold_verdict_ex(sam.a(), 0xB01002A0, u2, h, kHostKey, kUserVt, true, &no_host).why ==
          HoldWhy::CtrlNotUser);
    // A user's child is still D-53's (children on) or left alone (children off).
    v = hold_verdict_ex(sam.a(), 0xB0100002, u2, h, kHostKey, kUserVt, true, &ou);
    CHECK(v.hold && !v.npc && v.owner == h);
    CHECK(hold_verdict_ex(sam.a(), 0xB0100002, u2, h, kHostKey, kUserVt, false, &ou).why == HoldWhy::ChildrenOff);
    // NPCs with children off are still held.
    CHECK(hold_verdict_ex(sam.a(), 0xB01002A0, u2, h, kHostKey, kUserVt, false, &ou).hold);
    // Controller -> user.
    uintptr_t who = 0;
    CHECK(npc_controller_who(kHostKey, ou, who) == OwnerWho::Host && who == h);
    CHECK(npc_controller_who(kUser2Key, ou, who) == OwnerWho::Guest && who == u2);
    CHECK(npc_controller_who(0, ou, who) == OwnerWho::None && who == 0);
    CHECK(npc_controller_who(kOtherKey, ou, who) == OwnerWho::None);
    CHECK(std::strcmp(hold_why_name(HoldWhy::CtrlNotUser), "NPC not controlled by a known user") == 0);
}

void test_npc_split() {
    constexpr uintptr_t s1 = 0x1000, s2 = 0x2000, host = 0xA000, u2 = 0xB000;
    HoldVerdict npc;
    npc.hold = true;
    npc.npc = true;
    npc.why = HoldWhy::Hold;
    npc.owner = host;
    npc.owner_who = OwnerWho::Host;
    npc.controller = kHostKey;
    npc.type = 6;
    npc.actor = 0x7777;
    SplitPlan plan;
    {
        // Session 4's burst: the new NPC enters a sector both users observe.
        const uintptr_t sessions[] = {s1, s2};
        const uintptr_t users[] = {host, u2};
        const uint32_t keys[] = {0xB0100100, 0xB01002A0};
        const HoldVerdict v[] = {HoldVerdict{}, npc};
        CHECK(plan_split(sessions, users, host, keys, v, plan));
        CHECK(plan.pass == std::vector<uint32_t>{0xB0100100});
        // The controller gets it now, in its own 2005; it is never held.
        CHECK(plan.groups.size() == 1 && plan.groups[0].owner == host &&
              plan.groups[0].sessions == std::vector<uintptr_t>{s1} &&
              plan.groups[0].keys == std::vector<uint32_t>{0xB01002A0});
        CHECK(plan.records.size() == 1 && plan.records[0].observer == s2 && plan.records[0].npc &&
              plan.records[0].controller == kHostKey && plan.records[0].actor == 0x7777 &&
              !plan.records[0].observer_host);
    }
    {
        // User 2 entering the host's sectors (second call): only user 2 receives.
        const uintptr_t sessions[] = {s2};
        const uintptr_t users[] = {u2};
        const uint32_t keys[] = {0xB01002A0};
        const HoldVerdict v[] = {npc};
        CHECK(plan_split(sessions, users, host, keys, v, plan));
        CHECK(plan.pass.empty() && plan.groups.size() == 1 && plan.groups[0].sessions.empty());
        CHECK(plan.records.size() == 1 && plan.records[0].observer == s2);
    }
    {
        // Only the controller receives it: nothing is withheld.
        const uintptr_t sessions[] = {s1};
        const uintptr_t users[] = {host};
        const uint32_t keys[] = {0xB01002A0};
        const HoldVerdict v[] = {npc};
        CHECK(!plan_split(sessions, users, host, keys, v, plan));
    }
}

HoldEntry npc_entry(uint32_t key, uintptr_t observer, uintptr_t observer_user, bool observer_host,
                    uintptr_t actor = 0x7777, uint64_t since = 0) {
    HoldEntry e;
    e.key = key;
    e.observer = observer;
    e.observer_user = observer_user;
    e.observer_host = observer_host;
    e.owner_who = observer_host ? OwnerWho::Guest : OwnerWho::Host;
    e.npc = true;
    e.controller = observer_host ? kUser2Key : kHostKey;
    e.actor = actor;
    e.epoch = 1;
    e.since_ms = since;
    return e;
}

void test_npc_table() {
    NpcHoldTable t;
    CHECK(t.capacity() == hold::kNpcTableCapacity && t.room() == hold::kNpcTableCapacity);
    CHECK(t.add(npc_entry(0xB01002A0, 0x2000, 0xB000, false)) == NpcHoldTable::Add::Added);
    const uint64_t stamp0 = t.at(0).stamp;
    t.at(0).timeout_noted = true;
    // Duplicate (held again from the same session: a fresh announcement
    // withheld again, M6): its moment, actor and notes restart, its stamp changes.
    HoldEntry again = npc_entry(0xB01002A0, 0x2000, 0xB000, false, 0x8888, 999);
    again.controller = kUser2Key;
    CHECK(t.add(again) == NpcHoldTable::Add::Duplicate);
    CHECK(t.size() == 1 && t.at(0).since_ms == 999 && t.at(0).controller == kUser2Key && t.at(0).actor == 0x8888 &&
          !t.at(0).timeout_noted && t.at(0).stamp != stamp0);
    CHECK(t.locate(t.at(0).stamp, 5) == 0 && t.locate(stamp0, 0) == t.size());
    // A child's duplicate keeps its first moment (D-53 unchanged).
    HoldTable children;
    HoldEntry child = npc_entry(0xB0100002, 0x2000, 0xB000, false);
    child.npc = false;
    child.since_ms = 100;
    CHECK(children.add(child) == HoldTable::Add::Added);
    child.since_ms = 900;
    CHECK(children.add(child) == HoldTable::Add::Duplicate && children.at(0).since_ms == 100);
    // Bounded.
    for (uint32_t i = 1; i < hold::kNpcTableCapacity; ++i) {
        CHECK(t.add(npc_entry(0xB0110000 + i, 0x2000, 0xB000, false)) == NpcHoldTable::Add::Added);
    }
    CHECK(t.room() == 0);
    CHECK(t.add(npc_entry(0xB0200000, 0x2000, 0xB000, false)) == NpcHoldTable::Add::Full);
    // The engine announced some itself.
    const uint32_t keys[] = {0xB0110001, 0xB0110002};
    const uintptr_t sessions[] = {0x2000};
    CHECK(t.forget_announced(keys, sessions) == 2);
    CHECK(t.size() == hold::kNpcTableCapacity - 2);
    // The children's table is unchanged in size (D-53).
    CHECK(HoldTable::capacity() == hold::kTableCapacity);

    // User 2 leaves: what it observes goes; an NPC it controlled, observed by the host, stays.
    NpcHoldTable u;
    u.add(npc_entry(0xB0100001, 0x2000, 0xB000, false));  // withheld from user 2
    u.add(npc_entry(0xB0100002, 0x1000, 0xA000, true));   // user 2's NPC, withheld from the host
    CHECK(npc_entry_of_leaving_user2(u.at(0)) && !npc_entry_of_leaving_user2(u.at(1)));
    CHECK(u.drop_if([](const HoldEntry& e) { return npc_entry_of_leaving_user2(e); }) == 1);
    CHECK(u.size() == 1 && u.at(0).key == 0xB0100002);
}

NpcReleaseReadings ok_readings(uintptr_t actor, uint32_t controller, uint8_t restore, uintptr_t observer_user,
                               uint32_t observer_key) {
    NpcReleaseReadings r;
    r.epoch_now = 1;
    r.actor = actor;
    r.owner = uintptr_t{0};
    r.controller = controller;
    r.restore = restore;
    r.observer_valid = true;
    r.observer_user = observer_user;
    r.observer_key = observer_key;
    r.observes = true;
    return r;
}

// P8 U-e, the 6c12c82 crash (exe+0x1831598): an NPC user 2 controls is never released to the HOST's session with
// restore 0 by the timeout (the host's client built it with no current action and an attached piece read it).
void test_npc_withheld_from_host() {
    constexpr uintptr_t host_user = 0xA000, actor = 0x8885;
    const HoldEntry e = npc_entry(0xB0100885, 0x1000, host_user, true, actor);
    const char* why = "";
    NpcRelease kind = NpcRelease::None;
    NpcReleaseReadings r = ok_readings(actor, kUser2Key, 0, host_user, kHostKey);
    r.user2_key = kUser2Key;
    CHECK(npc_release_step(e, r, 300, why, &kind) == ReleaseStep::Keep && kind == NpcRelease::None);
    CHECK(npc_release_step(e, r, hold::kNpcTimeoutMs, why, &kind) == ReleaseStep::Keep &&
          kind == NpcRelease::WithheldUser2);
    CHECK(std::strcmp(why, "withheld from the host: user 2 controls it, restore 0") == 0);
    // No stale drop for it: still withheld long after.
    CHECK(npc_release_step(e, r, 10 * hold::kNpcTimeoutMs, why, &kind) == ReleaseStep::Keep &&
          kind == NpcRelease::WithheldUser2);
    // Its first action accepted (restore 1): released.
    NpcReleaseReadings r1 = ok_readings(actor, kUser2Key, 1, host_user, kHostKey);
    r1.user2_key = kUser2Key;
    CHECK(npc_release_step(e, r1, 10 * hold::kNpcTimeoutMs, why, &kind) == ReleaseStep::Release &&
          kind == NpcRelease::Restore1);
    // The host became its controller: released.
    NpcReleaseReadings rh = ok_readings(actor, kHostKey, 0, host_user, kHostKey);
    rh.user2_key = kUser2Key;
    CHECK(npc_release_step(e, rh, 10 * hold::kNpcTimeoutMs, why, &kind) == ReleaseStep::Release &&
          kind == NpcRelease::ByControl);
    // Out of range / despawned / another epoch: dropped as before.
    NpcReleaseReadings ro = r;
    ro.observes = false;
    CHECK(npc_release_step(e, ro, 10 * hold::kNpcTimeoutMs, why) == ReleaseStep::Drop);
    NpcReleaseReadings rd = r;
    rd.actor = uintptr_t{0};
    CHECK(npc_release_step(e, rd, 10 * hold::kNpcTimeoutMs, why) == ReleaseStep::Drop);
    NpcReleaseReadings re = r;
    re.epoch_now = 2;
    CHECK(npc_release_step(e, re, 10 * hold::kNpcTimeoutMs, why) == ReleaseStep::Drop);
    // Without user 2's key known, or another user controlling it: the timeout as before.
    NpcReleaseReadings r0 = ok_readings(actor, kUser2Key, 0, host_user, kHostKey);
    CHECK(npc_release_step(e, r0, hold::kNpcTimeoutMs, why, &kind) == ReleaseStep::Release &&
          kind == NpcRelease::Timeout);
    NpcReleaseReadings rx = ok_readings(actor, kOtherKey, 0, host_user, kHostKey);
    rx.user2_key = kUser2Key;
    CHECK(npc_release_step(e, rx, hold::kNpcTimeoutMs, why, &kind) == ReleaseStep::Release &&
          kind == NpcRelease::Timeout);
    // Held from USER 2's session (the host controls it): the timeout as before.
    const HoldEntry eu = npc_entry(0xB01002A0, 0x2000, 0xB000, false, actor);
    NpcReleaseReadings ru = ok_readings(actor, kHostKey, 0, 0xB000, kUser2Key);
    ru.user2_key = kUser2Key;
    CHECK(npc_release_step(eu, ru, hold::kNpcTimeoutMs, why, &kind) == ReleaseStep::Release &&
          kind == NpcRelease::Timeout);
    CHECK(std::string(npc_release_name(NpcRelease::WithheldUser2)) != "?");
}

void test_npc_release() {
    constexpr uintptr_t u2 = 0xB000, actor = 0x7777;
    const HoldEntry e = npc_entry(0xB01002A0, 0x2000, u2, false, actor);
    const char* why = "";
    // Restore 0, young: kept (never released with restore 0).
    CHECK(npc_release_step(e, ok_readings(actor, kHostKey, 0, u2, kUser2Key), 300, why) == ReleaseStep::Keep);
    CHECK(std::strcmp(why, "restore 0") == 0);
    // Its first action was accepted: released (a fresh native 2005).
    CHECK(npc_release_step(e, ok_readings(actor, kHostKey, 1, u2, kUser2Key), 300, why) == ReleaseStep::Release);
    CHECK(std::strcmp(why, "restore 1") == 0);
    // ...but not to an observer out of range (left the field or the sectors).
    NpcReleaseReadings r = ok_readings(actor, kHostKey, 1, u2, kUser2Key);
    r.observes = false;
    CHECK(npc_release_step(e, r, 300, why) == ReleaseStep::Drop);
    r.observes = std::nullopt;
    CHECK(npc_release_step(e, r, 300, why) == ReleaseStep::Keep);
    // Out of range with restore 0: dropped at once (no need to wait for the timeout).
    r = ok_readings(actor, kHostKey, 0, u2, kUser2Key);
    r.observes = false;
    CHECK(npc_release_step(e, r, 300, why) == ReleaseStep::Drop && std::strcmp(why, "observer out of range") == 0);
    // Timeout: released as the engine would (restore 0); some ambient NPCs never
    // serialise restore 1 although their controller keeps starting actions.
    CHECK(npc_release_step(e, ok_readings(actor, kHostKey, 0, u2, kUser2Key), hold::kNpcTimeoutMs, why) ==
          ReleaseStep::Release);
    CHECK(std::strncmp(why, "timeout", 7) == 0);
    // The controller changed to another user: still waiting for its first action.
    CHECK(npc_release_step(e, ok_readings(actor, kOtherKey, 0, u2, kUser2Key), 300, why) == ReleaseStep::Keep);
    CHECK(std::strcmp(why, "restore 0 (controller changed)") == 0);
    // The observer is now its controller: released at once (a controller is never held).
    CHECK(npc_release_step(e, ok_readings(actor, kUser2Key, 0, u2, kUser2Key), 300, why) == ReleaseStep::Release);
    CHECK(std::strcmp(why, "the observer is now its controller") == 0);
    // Despawned: dropped (nothing to show).
    r = ok_readings(actor, kHostKey, 1, u2, kUser2Key);
    r.actor = uintptr_t{0};
    CHECK(npc_release_step(e, r, 300, why) == ReleaseStep::Drop && std::strcmp(why, "despawned") == 0);
    // I2: what used to drop while the actor exists and the observer observes
    // it now announces it natively (else the NPC stayed invisible in range).
    NpcRelease kind = NpcRelease::None;
    r = ok_readings(0x8888, kHostKey, 0, u2, kUser2Key);
    CHECK(npc_release_step(e, r, 300, why, &kind) == ReleaseStep::Release && kind == NpcRelease::Native &&
          std::strncmp(why, "another actor behind the key", 28) == 0);
    r.observes = std::nullopt;  // ...only when it is known to be in range
    CHECK(npc_release_step(e, r, 300, why, &kind) == ReleaseStep::Keep);
    CHECK(npc_release_step(e, r, 3 * hold::kNpcTimeoutMs, why, &kind) == ReleaseStep::Drop);
    r.observes = false;
    CHECK(npc_release_step(e, r, 300, why, &kind) == ReleaseStep::Drop &&
          std::strcmp(why, "observer out of range") == 0);
    // Now owned by a user: restore 1 -> released as D-53 would; restore 0 ->
    // waits (child_hold on: never announced with 0) or native (child_hold off).
    r = ok_readings(actor, kHostKey, 1, u2, kUser2Key);
    r.owner = uintptr_t{0xA000};
    CHECK(npc_release_step(e, r, 300, why, &kind) == ReleaseStep::Release && kind == NpcRelease::Restore1);
    r.restore = uint8_t{0};
    CHECK(npc_release_step(e, r, 300, why, &kind, true) == ReleaseStep::Keep);
    CHECK(npc_release_step(e, r, 3 * hold::kNpcTimeoutMs, why, &kind, true) == ReleaseStep::Drop);
    CHECK(npc_release_step(e, r, 300, why, &kind, false) == ReleaseStep::Release && kind == NpcRelease::Native);
    // Unreadable for too long while in range: announced natively, not dropped.
    r = ok_readings(actor, kHostKey, 1, u2, kUser2Key);
    r.restore = std::nullopt;
    CHECK(npc_release_step(e, r, 3 * hold::kNpcTimeoutMs, why, &kind) == ReleaseStep::Release &&
          kind == NpcRelease::Native);
    // The kinds.
    CHECK(npc_release_step(e, ok_readings(actor, kHostKey, 0, u2, kUser2Key), hold::kNpcTimeoutMs, why, &kind) ==
              ReleaseStep::Release &&
          kind == NpcRelease::Timeout);
    CHECK(npc_release_step(e, ok_readings(actor, kUser2Key, 0, u2, kUser2Key), 300, why, &kind) ==
              ReleaseStep::Release &&
          kind == NpcRelease::ByControl);
    r = ok_readings(actor, kHostKey, 1, u2, kUser2Key);
    r.epoch_now = 2;
    CHECK(npc_release_step(e, r, 300, why) == ReleaseStep::Drop && std::strcmp(why, "another guest epoch") == 0);
    r = ok_readings(actor, kHostKey, 1, u2, kUser2Key);
    r.observer_valid = false;
    CHECK(npc_release_step(e, r, 300, why) == ReleaseStep::Drop && std::strcmp(why, "observer gone") == 0);
    r = ok_readings(actor, kHostKey, 1, 0xC000, kUser2Key);  // the session now has another user
    CHECK(npc_release_step(e, r, 300, why) == ReleaseStep::Drop);
    // Unreadable: kept, then dropped as stale.
    r = ok_readings(actor, kHostKey, 1, u2, kUser2Key);
    r.actor = std::nullopt;
    CHECK(npc_release_step(e, r, 300, why) == ReleaseStep::Keep);
    CHECK(npc_release_step(e, r, 3 * hold::kNpcTimeoutMs, why) == ReleaseStep::Drop);
    r = ok_readings(actor, kHostKey, 1, u2, kUser2Key);
    r.restore = std::nullopt;
    CHECK(npc_release_step(e, r, 300, why) == ReleaseStep::Keep);
    r.controller = std::nullopt;
    CHECK(npc_release_step(e, r, 300, why) == ReleaseStep::Keep);
}

void test_npc_read_release() {
    FakeUser host(kHostKey), user2(kUser2Key);
    FakeSam sam;
    FakeActor crow(0xB01002A0, 0, kHostKey, 6, 0);
    sam.add(crow);
    FakeSector sector;
    sector.set({host.a(), user2.a()});
    crow.set_sector(&sector);
    NpcReleaseReadings r = read_npc_release(sam.a(), 0xB01002A0, user2.a());
    CHECK(r.actor == std::optional<uintptr_t>(crow.a()) && r.owner == std::optional<uintptr_t>(0) &&
          r.controller == std::optional<uint32_t>(kHostKey) && r.restore == std::optional<uint8_t>(uint8_t{0}) &&
          r.observes == std::optional<bool>(true));
    // The host's client gave it its first action (0x2328894).
    crow.set_restore(1);
    r = read_npc_release(sam.a(), 0xB01002A0, user2.a());
    r.epoch_now = 1;
    r.observer_valid = true;
    r.observer_user = user2.a();
    r.observer_key = kUser2Key;
    const char* why = "";
    CHECK(npc_release_step(npc_entry(0xB01002A0, 0x2000, user2.a(), false, crow.a()), r, 200, why) ==
          ReleaseStep::Release);
    // Gone from the map.
    sam.remove(0xB01002A0);
    r = read_npc_release(sam.a(), 0xB01002A0, user2.a());
    CHECK(r.actor == std::optional<uintptr_t>(0));
}

// ---------------------------------------------------------------- host: observer hold, record + passes

// What the native NotifyAppear was asked (the seams' notify).
struct NotifyCall {
    std::vector<uintptr_t> sessions;
    std::vector<uint32_t> keys;
};
std::vector<NotifyCall> g_native;
uint64_t g_now = 1000;

void fake_notify(uintptr_t, const GameVec* s, const GameVec* k) {
    NotifyCall c;
    const auto* sd = static_cast<const uintptr_t*>(s->data);
    const auto* kd = static_cast<const uint32_t*>(k->data);
    c.sessions.assign(sd, sd + s->count);
    c.keys.assign(kd, kd + k->count);
    g_native.push_back(std::move(c));
}
uint64_t fake_now() { return g_now; }

// A StartActionReq's packet wrapper as vf2 gets it (hold::kReq*).
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

// The world of these tests: the host and user 2, both in one sector, and the
// Crow (an NPC the host controls, restore 0).
struct World {
    FakeUser host{kHostKey}, user2{kUser2Key};
    FakeSam sam;
    FakeSector sector;
    FakeActor crow{0xB01002A0, 0, kHostKey, 6, 0};
    hold_test::Seams seams;
    explicit World(bool children = true) {
        sam.add(crow);
        sector.set({host.a(), user2.a()});
        crow.set_sector(&sector);
        t_host = host.a();
        t_user2 = user2.a();
        t_sessions = {{kS1, host.a()}, {kS2, user2.a()}};
        t_leaving = false;
        seams.notify = fake_notify;
        seams.sam = sam.a();
        seams.base = 0x140000000;
        seams.realm = true;
        seams.now_ms = fake_now;
        g_now = 1000;
        g_native.clear();
        hold_test::reset(&seams, children, true);
    }
    ~World() { hold_test::reset(nullptr, true, false); }
    // The engine announcing `keys` to both users (one NotifyAppear).
    void appear(std::vector<uint32_t> keys, std::vector<uintptr_t> sessions = {kS1, kS2}) {
        const GameVec s{sessions.data(), static_cast<uint32_t>(sessions.size()), static_cast<uint32_t>(sessions.size())};
        const GameVec k{keys.data(), static_cast<uint32_t>(keys.size()), static_cast<uint32_t>(keys.size())};
        hold_test::notify(0, &s, &k);
    }
    bool consistent() const { return hold_test::npc_size() == hold_test::npc_size_atomic(); }
};

bool native_has(size_t i, std::vector<uintptr_t> sessions, std::vector<uint32_t> keys) {
    return i < g_native.size() && g_native[i].sessions == sessions && g_native[i].keys == keys;
}

void test_start_action_key() {
    FakeReq req(0xB01002A0);
    CHECK(start_action_req_key(req.wrapper.a()) == std::optional<uint32_t>(0xB01002A0u));
    // The sizes vf2 itself checks must agree.
    put(req.wrapper.p(), hold::kReqWrapperSize, uint16_t{0x10});
    CHECK(!start_action_req_key(req.wrapper.a()));
    FakeReq tiny(0xB01002A0, 2);
    CHECK(!start_action_req_key(tiny.wrapper.a()));
    CHECK(!start_action_req_key(0));
}

void test_hold_targeted_release() {
    World w;
    // The engine announces an unknown key and the Crow to both users.
    w.appear({0xB0100100, 0xB01002A0});
    // 1. the other key to everyone, 2. the Crow to its controller (the host) only.
    CHECK(g_native.size() == 2 && native_has(0, {kS1, kS2}, {0xB0100100}) && native_has(1, {kS1}, {0xB01002A0}));
    CHECK(hold_test::npc_size() == 1 && w.consistent() && hold_test::npc_reserved() == 0);
    HoldEntry e;
    CHECK(hold_test::npc_entry(0, e) && e.key == 0xB01002A0 && e.observer == kS2 && e.actor == w.crow.a() &&
          e.since_ms == 1000 && e.stamp != 0);
    CHECK(hold_test::counters().held == 1);
    // A StartActionReq of another actor, its first action not there yet: nothing.
    g_native.clear();
    g_now = 1005;
    FakeReq other(0xB0100555);
    hold_test::start_action(other.wrapper.a());
    CHECK(g_native.empty() && hold_test::npc_size() == 1);
    // The host's client starts the Crow's first action: restore 1, released at once by the targeted pass
    // (the budgeted one is not due: 5 ms after the last).
    w.crow.set_restore(1);
    g_now = 1010;
    const uint64_t periodic = hold_test::counters().periodic;
    FakeReq crow(0xB01002A0);
    hold_test::start_action(crow.wrapper.a());
    CHECK(g_native.size() == 1 && native_has(0, {kS2}, {0xB01002A0}));
    CHECK(hold_test::npc_size() == 0 && w.consistent());
    const auto c = hold_test::counters();
    CHECK(c.released == 1 && c.timeouts == 0 && c.native == 0 && c.targeted >= 1 && c.periodic == periodic);
    // M7: the lines are formatted by the network tick, not by the hooks.
    hold_test::network_tick();
    const std::string lines = hold_test::lines();
    CHECK(lines.find("held NPC 0xB01002A0") != std::string::npos);
    CHECK(lines.find("released NPC 0xB01002A0") != std::string::npos && lines.find("restore 1") != std::string::npos);
}

void test_hold_periodic_and_timeout() {
    World w;
    w.appear({0xB01002A0});
    CHECK(hold_test::npc_size() == 1 && w.consistent());
    // Restore 1 by another way (no StartActionReq of the Crow): only the budgeted pass sees it, at most every
    // hold::kNpcPassMs.
    w.crow.set_restore(1);
    g_native.clear();
    FakeReq other(0xB0100555);
    g_now = 1000 + hold::kNpcPassMs - 1;
    hold_test::start_action(other.wrapper.a());
    CHECK(g_native.empty() && hold_test::npc_size() == 1);
    g_now = 1000 + hold::kNpcPassMs;
    hold_test::start_action(other.wrapper.a());
    CHECK(native_has(0, {kS2}, {0xB01002A0}) && hold_test::npc_size() == 0 && w.consistent());

    // M5: without any StartActionReq, the tail of the game's own NotifyAppear runs the budgeted pass too.
    w.crow.set_restore(0);
    g_now = 2000;
    w.appear({0xB01002A0});
    CHECK(hold_test::npc_size() == 1);
    w.crow.set_restore(1);
    g_native.clear();
    g_now = 2000 + hold::kNpcPassMs;
    w.appear({0xA0100001});  // a character: passes unchanged, then the pass
    CHECK(g_native.size() == 2 && native_has(0, {kS1, kS2}, {0xA0100001}) && native_has(1, {kS2}, {0xB01002A0}));
    CHECK(hold_test::npc_size() == 0 && w.consistent());

    // Timeout: still restore 0 after hold::kNpcTimeoutMs: released with restore 0 as the engine would.
    w.crow.set_restore(0);
    g_now = 10000;
    w.appear({0xB01002A0});
    g_native.clear();
    g_now = 10000 + hold::kNpcTimeoutMs - hold::kNpcPassMs;
    hold_test::start_action(other.wrapper.a());
    CHECK(g_native.empty() && hold_test::npc_size() == 1);
    g_now = 10000 + hold::kNpcTimeoutMs;
    hold_test::start_action(other.wrapper.a());
    CHECK(native_has(0, {kS2}, {0xB01002A0}) && hold_test::npc_size() == 0 && w.consistent());
    CHECK(hold_test::counters().timeouts == 1);
}

void test_hold_reannounce_refresh() {
    // M6: held again by a later announcement: its moment restarts (no early timeout).
    World w;
    w.appear({0xB01002A0});
    g_now = 9000;
    w.appear({0xB01002A0});  // the engine announces it again (the observer entered other sectors)
    HoldEntry e;
    CHECK(hold_test::npc_entry(0, e) && e.since_ms == 9000 && hold_test::counters().again == 1);
    g_native.clear();
    g_now = 1000 + hold::kNpcTimeoutMs;  // 10 s after the FIRST announcement, 2 s after the second
    FakeReq other(0xB0100555);
    hold_test::start_action(other.wrapper.a());
    CHECK(g_native.empty() && hold_test::npc_size() == 1);
}

void test_hold_drop_to_native() {
    // I2: another actor behind the key while the observer still observes it: announced natively.
    World w;
    w.appear({0xB01002A0});
    FakeActor reborn(0xB01002A0, 0, kHostKey, 6, 0);
    reborn.set_sector(&w.sector);
    w.sam.replace(0, reborn);
    g_native.clear();
    g_now = 1050;
    FakeReq crow(0xB01002A0);
    hold_test::start_action(crow.wrapper.a());
    CHECK(native_has(0, {kS2}, {0xB01002A0}) && hold_test::npc_size() == 0 && w.consistent());
    CHECK(hold_test::counters().native == 1 && hold_test::counters().dropped == 0);
    hold_test::network_tick();
    CHECK(hold_test::lines().find("announced natively instead of dropped: another actor behind the key") !=
          std::string::npos);

    // Out of the observer's range: dropped (the engine announces it again when it comes back), nothing sent.
    World w2;
    w2.appear({0xB01002A0});
    w2.sector.set({w2.host.a()});
    g_native.clear();
    g_now = 1050;
    FakeReq crow2(0xB01002A0);
    hold_test::start_action(crow2.wrapper.a());
    CHECK(g_native.empty() && hold_test::npc_size() == 0 && w2.consistent() && hold_test::counters().dropped == 1);
}

void test_hold_fallback_defers() {
    // M5: the receiving thread's pass drops but never calls NotifyAppear; the release waits for a pass
    // on a thread that may call it.
    World w;
    w.appear({0xB01002A0});
    w.crow.set_restore(1);
    g_native.clear();
    g_now = 1000 + hold::kNpcFallbackMs;
    hold_test::server_tick();
    CHECK(g_native.empty() && hold_test::npc_size() == 1 && hold_test::counters().deferred == 1);
    FakeReq crow(0xB01002A0);
    hold_test::start_action(crow.wrapper.a());
    CHECK(native_has(0, {kS2}, {0xB01002A0}) && hold_test::npc_size() == 0 && w.consistent());
    // The fallback does drop: user 2's session gone.
    w.crow.set_restore(0);
    g_now = 5000;
    w.appear({0xB01002A0});
    t_sessions = {{kS1, w.host.a()}};
    g_native.clear();
    g_now = 5000 + hold::kNpcFallbackMs;
    hold_test::server_tick();
    CHECK(g_native.empty() && hold_test::npc_size() == 0 && w.consistent() && hold_test::counters().dropped == 1);
}

void test_hold_room_reserved() {
    // I2: the room for every (NPC, observer) is reserved in the split itself: a full table leaves the NPC
    // native (announced with restore 0), never withheld without an entry.
    World w;
    hold_test::fill_npc(hold::kNpcTableCapacity);
    CHECK(hold_test::npc_size() == hold::kNpcTableCapacity);
    g_native.clear();
    g_now = 1000;
    w.appear({0xB01002A0});
    CHECK(!g_native.empty() && native_has(0, {kS1, kS2}, {0xB01002A0}));  // unchanged: to both users at once
    auto c = hold_test::counters();
    CHECK(c.full == 1 && c.held == 0 && c.unrecorded == 0 && hold_test::npc_reserved() == 0 && w.consistent());
    // One slot left: exactly the Crow's one observer fits.
    World w2;
    hold_test::fill_npc(hold::kNpcTableCapacity - 1);
    g_native.clear();
    w2.appear({0xB01002A0});
    CHECK(native_has(0, {kS1}, {0xB01002A0}));
    c = hold_test::counters();
    CHECK(c.full == 0 && c.held == 1 && c.unrecorded == 0 && hold_test::npc_reserved() == 0 && w2.consistent());
    // The budgeted pass after it (the tail of that NotifyAppear) evaluated at most its budget.
    CHECK(c.evaluated <= hold::kNpcPassBudget);
}

void test_hold_user2_leaves() {
    World w;
    w.appear({0xB01002A0});
    t_leaving = true;
    hold_test::network_tick();
    CHECK(hold_test::npc_size() == 0 && w.consistent());
    CHECK(hold_test::lines().find("withheld from it dropped") != std::string::npos);
}

// ---------------------------------------------------------------- host: observer_hold_acks with observer_hold
//
// pendientes/p8-escenas-y-acks.md §3: child_hold tells observer_hold_acks which NPC is withheld from user 2's
// session; take_frame (simulated here, as the game calls it: synchronously inside NotifyAppear) keeps that NPC's
// packets and queues them right after its 2005.

std::vector<std::vector<uint8_t>> g_out;  // user 2's outgoing queue (take_frame's g_outgoing)

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

// A per-actor packet of `key` (actor key at +0) with a marker byte.
std::vector<uint8_t> npc_packet(uint16_t op, uint32_t key, uint8_t marker) {
    std::vector<uint8_t> payload(12, 0);
    put(payload.data(), 0, key);
    payload[4] = marker;
    return wire(op, payload);
}

// An ActorLoadDataAck 2005 announcing `keys` (records [u16 len][u8 type][u32 key][u32 owner]).
std::vector<uint8_t> announce(const std::vector<uint32_t>& keys) {
    std::vector<uint8_t> payload;
    for (const uint32_t k : keys) {
        const auto at = static_cast<uint32_t>(payload.size());
        payload.resize(at + 2 + 9, 0);
        put(payload.data(), at, uint16_t{9});
        payload[at + 2] = 6;
        put(payload.data(), at + 3, k);
    }
    return wire(2005, payload);
}

// take_frame for the live session 2.
void take(std::vector<uint8_t> pkt) {
    const uint16_t op = static_cast<uint16_t>(pkt[0] | pkt[1] << 8);
    AckRouteOut o;
    const std::vector<uint8_t> copy = pkt;  // the payload span must outlive a move from pkt
    const AckRoute r =
        observer_hold_acks_route(op, std::span<const uint8_t>(copy).subspan(5), pkt, &test_push, o);
    if (r == AckRoute::Pass) g_out.push_back(std::move(pkt));
}

// The native NotifyAppear: what it serialises for user 2's session goes through take_frame at once.
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

void test_hold_acks_with_observer_hold() {
    constexpr uint32_t kCrow = 0xB01002A0, kOther = 0xB0100100;
    t_live_s2 = kS2;
    observer_hold_acks_configure(true);
    observer_hold_acks_clear(oha::Discard::Epoch);
    const oha::Counters c0 = observer_hold_acks_counters();
    {
        World w;
        w.seams.notify = fake_notify_2005;
        g_out.clear();
        // 1. Another key to both users (its 2005 reaches user 2 at once); the Crow to the host only: withheld.
        w.appear({kOther, kCrow});
        CHECK(g_out.size() == 1 && op_of(g_out[0]) == 2005 && key_of(g_out[0]) == kOther);
        CHECK(observer_hold_acks_keys() == 1);
        // 2. The server relays the Crow's AI changes and its spawn action to user 2 meanwhile (session 6B order):
        //    kept; another key's packet passes.
        take(npc_packet(2404, kCrow, 1));
        take(npc_packet(2404, kCrow, 2));
        take(npc_packet(2404, kOther, 9));
        take(npc_packet(2404, kCrow, 3));
        take(npc_packet(2723, kCrow, 4));
        CHECK(g_out.size() == 2 && op_of(g_out[1]) == 2404 && key_of(g_out[1]) == kOther);
        // 3. Its first action: observer_hold re-announces it to user 2: the 2005, then its packets in order.
        w.crow.set_restore(1);
        g_now = 1160;
        FakeReq crow(kCrow);
        hold_test::start_action(crow.wrapper.a());
        CHECK(g_out.size() == 7);
        if (g_out.size() == 7) {
            CHECK(op_of(g_out[2]) == 2005 && key_of(g_out[2]) == kCrow);
            const uint16_t ops[] = {2404, 2404, 2404, 2723};
            for (size_t i = 0; i < 4; ++i) {
                CHECK(op_of(g_out[3 + i]) == ops[i] && key_of(g_out[3 + i]) == kCrow && g_out[3 + i][9] == i + 1);
            }
        }
        CHECK(observer_hold_acks_keys() == 0);
        // 4. After its 2005 its packets pass again.
        take(npc_packet(2404, kCrow, 5));
        CHECK(g_out.size() == 8 && op_of(g_out[7]) == 2404);
    }
    oha::Counters c = observer_hold_acks_counters();
    CHECK(c.held - c0.held == 4 && c.released_pkts - c0.released_pkts == 4 && c.released_keys - c0.released_keys == 1);
    CHECK(c.held_by_op[static_cast<size_t>(oha::op_index(2723))] - c0.held_by_op[static_cast<size_t>(oha::op_index(2723))] == 1);
    {
        // A dropped hold (out of user 2's range): its packets go, nothing is sent for it.
        World w;
        w.seams.notify = fake_notify_2005;
        g_out.clear();
        w.appear({kCrow});
        take(npc_packet(2404, kCrow, 1));
        CHECK(g_out.empty() && observer_hold_acks_keys() == 1);
        w.sector.set({w.host.a()});
        g_now = 1050;
        FakeReq crow(kCrow);
        hold_test::start_action(crow.wrapper.a());
        CHECK(g_out.empty() && observer_hold_acks_keys() == 0);
        take(npc_packet(2404, kCrow, 2));
        CHECK(g_out.size() == 1);
    }
    c = observer_hold_acks_counters();
    CHECK(c.discarded_pkts[static_cast<size_t>(oha::Discard::Dropped)] -
              c0.discarded_pkts[static_cast<size_t>(oha::Discard::Dropped)] ==
          1);
    {
        // Never for the controller: an NPC user 2 controls is withheld from the HOST, nothing waits here.
        World w;
        w.seams.notify = fake_notify_2005;
        FakeActor mine(0xB0100600, 0, kUser2Key, 6, 0);
        mine.set_sector(&w.sector);
        w.sam.add(mine);
        g_out.clear();
        w.appear({0xB0100600});
        CHECK(native_has(0, {kS2}, {0xB0100600}) && hold_test::npc_size() == 1 && observer_hold_acks_keys() == 0);
        take(npc_packet(2404, 0xB0100600, 1));
        CHECK(g_out.size() == 2);  // its 2005 to its controller, then the packet: nothing held
    }
    {
        // User 2 leaving: what is held goes.
        World w;
        w.seams.notify = fake_notify_2005;
        w.appear({kCrow});
        take(npc_packet(2723, kCrow, 1));
        CHECK(observer_hold_acks_keys() == 1);
        t_leaving = true;
        observer_hold_acks_tick();
        CHECK(observer_hold_acks_keys() == 0);
        t_leaving = false;
    }
    c = observer_hold_acks_counters();
    CHECK(c.discarded_pkts[static_cast<size_t>(oha::Discard::UserGone)] -
              c0.discarded_pkts[static_cast<size_t>(oha::Discard::UserGone)] ==
          1);
    {
        // Off: nothing is told, nothing kept.
        observer_hold_acks_configure(false);
        World w;
        w.seams.notify = fake_notify_2005;
        g_out.clear();
        w.appear({kCrow});
        take(npc_packet(2723, kCrow, 1));
        CHECK(observer_hold_acks_keys() == 0 && g_out.size() == 1);
    }
    t_live_s2 = 0;
}

// ---------------------------------------------------------------- host: observer_load_defer (P8 session 14)

void test_hold_load_defer() {
    constexpr uint32_t kCrow = 0xB01002A0;
    {
        // 1. On, user 2 loading: an NPC that already acted (restore 1) goes to the host only; its actions during
        //    the load and observer_hold's 10 s timeout do not release it; user 2's 2762 does.
        World w;
        w.crow.set_restore(1);
        hold_test::set_load_defer(2, true);
        w.appear({0xB0100100, kCrow});
        CHECK(g_native.size() == 2 && native_has(0, {kS1, kS2}, {0xB0100100}) && native_has(1, {kS1}, {kCrow}));
        HoldEntry e;
        CHECK(hold_test::npc_size() == 1 && hold_test::npc_entry(0, e) && e.defer && e.observer == kS2 &&
              w.consistent());
        CHECK(hold_test::load_defer_counters().deferred == 1);
        g_native.clear();
        FakeReq crow(kCrow);
        g_now = 1000 + hold::kNpcPassMs;
        hold_test::start_action(crow.wrapper.a());
        CHECK(g_native.empty() && hold_test::npc_size() == 1);
        g_now = 1000 + hold::kNpcTimeoutMs + 500;
        hold_test::start_action(crow.wrapper.a());
        CHECK(g_native.empty() && hold_test::npc_size() == 1);
        hold_test::set_load_defer(2, false);  // the 2762 written for user 2
        g_now += hold::kNpcPassMs;
        hold_test::start_action(crow.wrapper.a());
        CHECK(native_has(0, {kS2}, {kCrow}) && hold_test::npc_size() == 0 && w.consistent());
        const auto c = hold_test::load_defer_counters();
        CHECK(c.released == 1 && c.released_at_cap == 0 && c.dropped == 0 &&
              c.waited_max_ms >= hold::kNpcTimeoutMs);
        CHECK(hold_test::counters().timeouts == 0);
        hold_test::network_tick();
        const std::string lines = hold_test::lines();
        CHECK(lines.find("observer load defer: NPC 0xB01002A0") != std::string::npos);
        CHECK(lines.find("observer load defer: released NPC 0xB01002A0") != std::string::npos &&
              lines.find("user 2's 2762 written") != std::string::npos);
        CHECK(lines.find("starts a login / field load") != std::string::npos &&
              lines.find("has its 2762") != std::string::npos);
        // Stable: announced natively to both.
        g_native.clear();
        w.appear({kCrow});
        CHECK(native_has(0, {kS1, kS2}, {kCrow}) && hold_test::npc_size() == 0);
    }
    {
        // 2. The cap: no 2762 for user 2: released after hold::kLoadDeferMaxMs, as the engine would (restore 1).
        World w;
        w.crow.set_restore(1);
        hold_test::set_load_defer(2, true);
        w.appear({kCrow});
        CHECK(native_has(0, {kS1}, {kCrow}) && hold_test::npc_size() == 1);
        g_native.clear();
        FakeReq crow(kCrow);
        g_now = 1000 + hold::kLoadDeferMaxMs - 1;
        hold_test::start_action(crow.wrapper.a());
        CHECK(g_native.empty() && hold_test::npc_size() == 1);
        g_now = 1000 + hold::kLoadDeferMaxMs;
        hold_test::start_action(crow.wrapper.a());
        CHECK(native_has(0, {kS2}, {kCrow}) && hold_test::npc_size() == 0);
        const auto c = hold_test::load_defer_counters();
        CHECK(c.released == 1 && c.released_at_cap == 1);
    }
    {
        // 3. Restore 0 during the load: observer_hold's own hold (not a deferral), whose release waits too.
        World w;
        hold_test::set_load_defer(2, true);
        w.appear({kCrow});
        HoldEntry e;
        CHECK(hold_test::npc_size() == 1 && hold_test::npc_entry(0, e) && !e.defer);
        w.crow.set_restore(1);
        g_native.clear();
        FakeReq crow(kCrow);
        g_now = 1100;
        hold_test::start_action(crow.wrapper.a());
        CHECK(g_native.empty() && hold_test::npc_size() == 1);
        hold_test::set_load_defer(2, false);
        g_now = 1200;
        hold_test::start_action(crow.wrapper.a());
        CHECK(native_has(0, {kS2}, {kCrow}) && hold_test::npc_size() == 0);
        CHECK(hold_test::load_defer_counters().deferred == 0 && hold_test::load_defer_counters().released == 0);
    }
    {
        // 4. probe: nothing withheld, counted; observer_hold's own releases do not wait.
        World w;
        w.crow.set_restore(1);
        hold_test::set_load_defer(1, true);
        w.appear({kCrow});
        CHECK(native_has(0, {kS1, kS2}, {kCrow}) && hold_test::npc_size() == 0);
        auto c = hold_test::load_defer_counters();
        CHECK(c.would == 1 && c.deferred == 0);
        w.crow.set_restore(0);
        g_native.clear();
        w.appear({kCrow});
        CHECK(native_has(0, {kS1}, {kCrow}) && hold_test::npc_size() == 1);
        w.crow.set_restore(1);
        g_native.clear();
        g_now = 1100;
        FakeReq crow(kCrow);
        hold_test::start_action(crow.wrapper.a());
        CHECK(native_has(0, {kS2}, {kCrow}) && hold_test::npc_size() == 0);
        c = hold_test::load_defer_counters();
        CHECK(c.would == 1 && c.released == 0);
    }
    {
        // 5. Native: user 2 not loading; user 2 not a recipient; an NPC user 2 controls; the key off.
        World w;
        w.crow.set_restore(1);
        hold_test::set_load_defer(2, false);
        w.appear({kCrow});
        CHECK(native_has(0, {kS1, kS2}, {kCrow}) && hold_test::npc_size() == 0);
        hold_test::set_load_defer(2, true);
        g_native.clear();
        w.appear({kCrow}, {kS1});
        CHECK(native_has(0, {kS1}, {kCrow}) && hold_test::npc_size() == 0);
        FakeActor mine(0xB0100600, 0, kUser2Key, 6, 1);
        mine.set_sector(&w.sector);
        w.sam.add(mine);
        g_native.clear();
        w.appear({0xB0100600});
        CHECK(native_has(0, {kS1, kS2}, {0xB0100600}) && hold_test::npc_size() == 0);
        hold_test::set_load_defer(0, true);
        g_native.clear();
        w.appear({kCrow});
        CHECK(native_has(0, {kS1, kS2}, {kCrow}) && hold_test::npc_size() == 0);
        CHECK(hold_test::load_defer_counters().deferred == 0 && hold_test::load_defer_counters().would == 0);
    }
    {
        // 6. observer_hold_acks: a deferred key's packets are not kept (user 2's client drops them as without
        //    the hold); its 2005 goes after the 2762.
        t_live_s2 = kS2;
        observer_hold_acks_configure(true);
        observer_hold_acks_clear(oha::Discard::Epoch);
        {
            World w;
            w.seams.notify = fake_notify_2005;
            w.crow.set_restore(1);
            hold_test::set_load_defer(2, true);
            g_out.clear();
            w.appear({kCrow});
            CHECK(g_out.empty() && observer_hold_acks_keys() == 0 && hold_test::npc_size() == 1);
            take(npc_packet(2697, kCrow, 1));
            CHECK(g_out.size() == 1 && op_of(g_out[0]) == 2697);
            hold_test::set_load_defer(2, false);
            g_now = 1200;
            FakeReq crow(kCrow);
            hold_test::start_action(crow.wrapper.a());
            CHECK(g_out.size() == 2 && op_of(g_out[1]) == 2005 && key_of(g_out[1]) == kCrow);
        }
        observer_hold_acks_configure(false);
        t_live_s2 = 0;
    }
    {
        // 7. User 2 leaves while its NPCs are withheld: they go with it.
        World w;
        w.crow.set_restore(1);
        hold_test::set_load_defer(2, true);
        w.appear({kCrow});
        CHECK(hold_test::npc_size() == 1);
        t_leaving = true;
        hold_test::network_tick();
        CHECK(hold_test::npc_size() == 0 && w.consistent());
        t_leaving = false;
    }
    {
        // 8. Regression 2026-10-11 (8 type-6 creatures dropped as "despawned" while user 2 loaded; the server sent
        //    the host its 2516 then): drops by reason. A despawn drops at once (harmless). Out of user 2's range
        //    during the load is kept (the sector's observers may still be settling) and decided after the 2762:
        //    back in range -> released; still out -> dropped and counted as OTHER.
        World w;
        FakeActor deer(0xB0100210, 0, kHostKey, 6, 1);
        deer.set_sector(&w.sector);
        w.sam.add(deer);
        FakeSector far;
        far.set({w.host.a()});
        FakeActor fox(0xB0100211, 0, kHostKey, 6, 1);
        fox.set_sector(&w.sector);
        w.sam.add(fox);
        w.crow.set_restore(1);
        hold_test::set_load_defer(2, true);
        w.appear({kCrow, 0xB0100210, 0xB0100211});
        CHECK(native_has(0, {kS1}, {kCrow, 0xB0100210, 0xB0100211}) && hold_test::npc_size() == 3);
        w.sam.remove(0xB0100210);  // despawned by the server
        w.crow.set_sector(&far);   // user 2 does not observe its sector (for now)
        fox.set_sector(&far);
        g_native.clear();
        FakeReq other(0xB0100555);
        g_now = 1000 + hold::kNpcPassMs;
        hold_test::start_action(other.wrapper.a());
        auto c = hold_test::load_defer_counters();
        CHECK(g_native.empty() && hold_test::npc_size() == 2 && c.dropped == 1 && c.dropped_despawned == 1);
        // Past 3 x the NPC timeout (the "stale" age), still loading: kept.
        g_now = 1000 + 3 * hold::kNpcTimeoutMs + hold::kNpcPassMs;
        hold_test::start_action(other.wrapper.a());
        CHECK(g_native.empty() && hold_test::npc_size() == 2);
        // The Crow comes back in range; the fox does not. The 2762.
        w.crow.set_sector(&w.sector);
        hold_test::set_load_defer(2, false);
        g_now += hold::kNpcPassMs;
        hold_test::start_action(other.wrapper.a());
        CHECK(native_has(0, {kS2}, {kCrow}) && hold_test::npc_size() == 0);
        c = hold_test::load_defer_counters();
        CHECK(c.released == 1 && c.dropped == 2 && c.dropped_despawned == 1 && c.dropped_gone == 0 &&
              c.dropped_other == 1);
    }
    {
        // 9. Review M1: a load without its 2762 is taken as finished hold::kLoadDeferMaxMs after it began (one
        //    warning line): what it held is released, nothing new is withheld for it.
        World w;
        w.crow.set_restore(1);
        hold_test::set_load_defer(2, true);  // the load begins at g_now = 1000
        g_now = 1000 + hold::kLoadDeferMaxMs - 100;
        w.appear({kCrow});
        CHECK(native_has(0, {kS1}, {kCrow}) && hold_test::npc_size() == 1);
        g_native.clear();
        FakeReq crow(kCrow);
        g_now = 1000 + hold::kLoadDeferMaxMs;  // the entry is 100 ms old; the load has hit the cap
        hold_test::start_action(crow.wrapper.a());
        CHECK(native_has(0, {kS2}, {kCrow}) && hold_test::npc_size() == 0);
        auto c = hold_test::load_defer_counters();
        CHECK(c.stuck == 1 && c.released == 1 && c.released_at_cap == 1);
        g_native.clear();
        w.appear({kCrow});
        CHECK(native_has(0, {kS1, kS2}, {kCrow}) && hold_test::npc_size() == 0);
        hold_test::network_tick();
        CHECK(hold_test::lines().find("without its 2762 (a stuck move phase?)") != std::string::npos);
        // A new load starts the clock again.
        hold_test::set_load_defer(2, false);
        hold_test::set_load_defer(2, true);
        g_native.clear();
        w.appear({kCrow});
        CHECK(native_has(0, {kS1}, {kCrow}) && hold_test::npc_size() == 1);
        c = hold_test::load_defer_counters();
        CHECK(c.stuck == 1 && c.deferred == 2);
    }
    {
        // 10. Review L2: a restore-0 entry held through the load gets its whole 10 s timeout after the 2762.
        World w;  // crow restore 0
        hold_test::set_load_defer(2, true);
        w.appear({kCrow});
        CHECK(hold_test::npc_size() == 1);
        FakeReq other(0xB0100555);
        g_now = 1000 + 20000;
        hold_test::set_load_defer(2, false);  // the 2762 at t 21000
        g_native.clear();
        hold_test::start_action(other.wrapper.a());
        CHECK(g_native.empty() && hold_test::npc_size() == 1);  // not timed out at once (it is 20 s old)
        g_now = 21000 + hold::kNpcTimeoutMs - hold::kNpcPassMs;
        hold_test::start_action(other.wrapper.a());
        CHECK(g_native.empty() && hold_test::npc_size() == 1);
        g_now = 21000 + hold::kNpcTimeoutMs;
        hold_test::start_action(other.wrapper.a());
        CHECK(native_has(0, {kS2}, {kCrow}) && hold_test::npc_size() == 0 && hold_test::counters().timeouts == 1);
    }
    {
        // 11. Review L5: session 2's user pointer changes while user 2 is still there: the drop counts as OTHER.
        World w;
        w.crow.set_restore(1);
        hold_test::set_load_defer(2, true);
        w.appear({kCrow});
        CHECK(hold_test::npc_size() == 1);
        FakeUser again(kUser2Key);
        t_sessions = {{kS1, w.host.a()}, {kS2, again.a()}};
        t_user2 = again.a();
        g_now = 1000 + hold::kNpcPassMs;
        FakeReq other(0xB0100555);
        hold_test::start_action(other.wrapper.a());
        const auto c = hold_test::load_defer_counters();
        CHECK(hold_test::npc_size() == 0 && c.dropped == 1 && c.dropped_other == 1 && c.dropped_gone == 0);
    }
    // move_phase_loading (review M1): only from user 2's 2549 until its 2762.
    CHECK(!move_phase_loading(MovePhase::None) && !move_phase_loading(MovePhase::Stable));
    CHECK(!move_phase_loading(MovePhase::LoggingIn) && !move_phase_loading(MovePhase::MoveAsked) &&
          !move_phase_loading(MovePhase::WindowA));
    CHECK(move_phase_loading(MovePhase::Completing) && move_phase_loading(MovePhase::WindowB) &&
          move_phase_loading(MovePhase::LoadingSent));
}

// ---------------------------------------------------------------- host: observer_hold_pieces (P8 sessions 13/14)
//
// pendientes/p8-s14-viajeros.md §6: an NPC's carried piece (no CC, its hierarchy naming the NPC) withheld with
// its parent and announced right after it.

void test_piece_logic() {
    // fam::family_owner: the user that gets the member now (plan_split's owner), or 0 (left native).
    constexpr uintptr_t h = 0x10, u2 = 0x20, third = 0x30;
    {
        const uintptr_t s[] = {kS1, kS2, 0};
        const uintptr_t u[] = {h, u2, 0};
        const bool need_s2[] = {false, true, false};
        CHECK(fam::family_owner(s, u, need_s2, h, u2) == h);
        const bool need_s1[] = {true, false, false};
        CHECK(fam::family_owner(s, u, need_s1, h, u2) == u2);  // the host's session waits (user 2's NPC's piece)
        const bool none[] = {false, false, false};
        CHECK(fam::family_owner(s, u, none, h, u2) == 0);
        const bool both[] = {true, true, false};
        CHECK(fam::family_owner(s, u, both, h, u2) == 0);  // nobody could get it now: native
    }
    {
        // Only user 2 receives: withheld from it, "owned" by the host (no group session).
        const uintptr_t s[] = {kS2};
        const uintptr_t u[] = {u2};
        const bool need[] = {true};
        CHECK(fam::family_owner(s, u, need, h, u2) == h);
        CHECK(fam::family_owner(s, u, need, 0, u2) == 0);  // the host's user unknown
    }
    {
        // A third user that would get it now while the host would too: mixed, native.
        const uintptr_t s[] = {kS1, kS2, 0x53000};
        const uintptr_t u[] = {h, u2, third};
        const bool need[] = {false, true, false};
        CHECK(fam::family_owner(s, u, need, h, u2) == 0);
        const uintptr_t unread[] = {h, 0, third};
        CHECK(fam::family_owner(s, unread, need, h, u2) == 0);
    }

    // family_release_step.
    HoldEntry e;
    e.key = 0xB01002A1;
    e.observer = kS2;
    e.observer_user = 0x20;
    e.epoch = 1;
    e.family = true;
    e.parent = 0xB01002A0;
    FamilyReadings r;
    r.epoch_now = 1;
    r.actor = uintptr_t{0x1234};
    r.observer_valid = true;
    r.observer_user = 0x20;
    r.observes = true;
    r.parent_held = true;
    const char* why = "";
    FamilyRelease k = FamilyRelease::None;
    CHECK(family_release_step(e, r, 100, 100, why, &k) == ReleaseStep::Keep);
    CHECK(family_release_step(e, r, hold::kFamilyMaxHoldMs, 100, why, &k) == ReleaseStep::Release &&
          k == FamilyRelease::MaxAge);
    // The host's session (not tracked): its parent's entry went -> natively now (observer_hold_pieces' rule).
    r.parent_held = false;
    CHECK(family_release_step(e, r, 100, 100, why, &k) == ReleaseStep::Release && k == FamilyRelease::Orphan);
    // User 2's session: its parent reached it -> right after; not yet -> waits, then natively (an orphan).
    r.tracks = true;
    CHECK(family_release_step(e, r, 100, 100, why, &k) == ReleaseStep::Keep &&
          std::strcmp(why, "waiting for its parent's announcement") == 0);
    CHECK(family_release_step(e, r, 100, hold::kFamilyOrphanMs, why, &k) == ReleaseStep::Release &&
          k == FamilyRelease::OrphanTimeout);
    r.parent_known = true;
    CHECK(family_release_step(e, r, 100, 100, why, &k) == ReleaseStep::Release && k == FamilyRelease::AfterParent);
    r.observes = std::nullopt;  // unknown range: announced (never kept invisible)
    CHECK(family_release_step(e, r, 100, 100, why, &k) == ReleaseStep::Release);
    r.observes = false;
    CHECK(family_release_step(e, r, 100, 100, why, &k) == ReleaseStep::Drop);
    r.observes = true;
    r.actor = uintptr_t{0};
    CHECK(family_release_step(e, r, 100, 100, why, &k) == ReleaseStep::Drop);
    r.actor = std::nullopt;
    CHECK(family_release_step(e, r, 100, 100, why, &k) == ReleaseStep::Keep);
    CHECK(family_release_step(e, r, hold::kFamilyMaxHoldMs, 100, why, &k) == ReleaseStep::Drop);
    r.actor = uintptr_t{0x1234};
    r.observer_user = 0x30;
    CHECK(family_release_step(e, r, 100, 100, why, &k) == ReleaseStep::Drop);
    r.observer_user = 0x20;
    r.epoch_now = 2;
    CHECK(family_release_step(e, r, 100, 100, why, &k) == ReleaseStep::Drop);

    // piece_parent_key: the hierarchy's own actor must be this actor; an 0xB parent other than its own key.
    FakeActor piece(0xB01002A1, 0, kHostKey, 7, 0);
    CHECK(piece_parent_key(piece.a()) == std::optional<uint32_t>(0u));  // no hierarchy component: no parent
    piece.set_parent(0xB01002A0);
    CHECK(piece_parent_key(piece.a()) == std::optional<uint32_t>(0xB01002A0u));
    piece.set_parent(0xA0100001);
    CHECK(piece_parent_key(piece.a()) == std::optional<uint32_t>(0u));  // a character is not a parent here
    piece.set_parent(0xB01002A1);
    CHECK(piece_parent_key(piece.a()) == std::optional<uint32_t>(0u));  // itself
    piece.set_parent(0xB01002A0, 0x5000);
    CHECK(!piece_parent_key(piece.a()));  // another actor's component: unlinked
}

// The world's Crow carrying a piece (a basket: type 7, no CC) in the same sector.
struct PieceWorld : World {
    static constexpr uint32_t kCrow = 0xB01002A0, kPiece = 0xB01002A1;
    FakeActor piece{kPiece, 0, kHostKey, 7, 0};
    explicit PieceWorld(uint8_t mode = 2) {
        piece.no_cc();
        piece.set_parent(kCrow);
        piece.set_sector(&sector);
        sam.add(piece);
        hold_test::set_family(mode);
    }
    void release_crow(uint64_t at) {
        crow.set_restore(1);
        g_now = at;
        FakeReq req(kCrow);
        hold_test::start_action(req.wrapper.a());
    }
};

void test_hold_pieces() {
    constexpr uint32_t kCrow = PieceWorld::kCrow, kPiece = PieceWorld::kPiece;
    {
        // 1. In the same NotifyAppear: both to the controller (in their order), both withheld from user 2; the
        //    Crow's release announces the piece right after it, in the same 2005.
        PieceWorld w;
        w.appear({kCrow, kPiece});
        CHECK(g_native.size() == 1 && native_has(0, {kS1}, {kCrow, kPiece}));
        HoldEntry e;
        CHECK(hold_test::family_size() == 1 && hold_test::family_entry(0, e) && e.family && e.parent == kCrow &&
              e.observer == kS2 && e.key == kPiece);
        CHECK(hold_test::npc_size() == 1 && hold_test::npc_reserved() == 0);
        g_native.clear();
        w.release_crow(1010);
        CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kCrow, kPiece}));
        CHECK(hold_test::family_size() == 0 && hold_test::npc_size() == 0);
        const auto c = hold_test::family_counters();
        CHECK(c.held == 1 && c.released == 1 && c.orphans == 0 && c.dropped == 0 && c.unlinked == 0);
        CHECK(hold_test::counters().released == 1);  // the NPC counters count the NPC only
        hold_test::network_tick();
        const std::string lines = hold_test::lines();
        CHECK(lines.find("held member 0xB01002A1") != std::string::npos);
        CHECK(lines.find("released member 0xB01002A1") != std::string::npos &&
              lines.find("behind its parent, in its release") != std::string::npos);
    }
    {
        // 2. The piece in a later NotifyAppear (its parent held earlier), and listed before its parent: the
        //    release still puts the parent first.
        PieceWorld w;
        w.appear({kCrow});
        CHECK(native_has(0, {kS1}, {kCrow}) && hold_test::npc_size() == 1);
        g_native.clear();
        w.appear({kPiece});
        CHECK(g_native.size() == 1 && native_has(0, {kS1}, {kPiece}) && hold_test::family_size() == 1);
        g_native.clear();
        w.appear({kPiece, kCrow});  // announced again (another sector): refreshed, still parent-first on release
        CHECK(native_has(0, {kS1}, {kPiece, kCrow}) && hold_test::family_counters().again == 1);
        g_native.clear();
        w.release_crow(1010);
        CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kCrow, kPiece}) && hold_test::family_size() == 0);
    }
    {
        // 3. Dropped with its parent (out of user 2's range): nothing is published.
        PieceWorld w;
        w.appear({kCrow, kPiece});
        w.sector.set({w.host.a()});
        g_native.clear();
        w.release_crow(1050);
        CHECK(g_native.empty() && hold_test::family_size() == 0 && hold_test::npc_size() == 0);
        CHECK(hold_test::family_counters().dropped == 1 && hold_test::family_counters().released == 0);
    }
    {
        // 4. The piece despawned before its parent's release: the parent alone.
        PieceWorld w;
        w.appear({kCrow, kPiece});
        w.sam.remove(kPiece);
        g_native.clear();
        w.release_crow(1010);
        CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kCrow}) && hold_test::family_size() == 0);
        CHECK(hold_test::family_counters().dropped == 1);
    }
    {
        // 5. Orphaned: the engine announced its parent to user 2 itself (restore 1 by then): the next periodic
        //    pass announces the piece natively (after its parent, never invisible).
        PieceWorld w;
        w.appear({kCrow, kPiece});
        w.crow.set_restore(1);
        g_native.clear();
        w.appear({kCrow}, {kS2});
        CHECK(native_has(0, {kS2}, {kCrow}) && hold_test::npc_size() == 0 && hold_test::family_size() == 1);
        g_native.clear();
        g_now = 1000 + hold::kNpcPassMs;
        FakeReq other(0xB0100555);
        hold_test::start_action(other.wrapper.a());
        CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kPiece}) && hold_test::family_size() == 0);
        const auto c = hold_test::family_counters();
        CHECK(c.released == 1 && c.orphans == 1);
        hold_test::network_tick();
        CHECK(hold_test::lines().find("its parent's entry went without it") != std::string::npos);
    }
    {
        // 6. The receiving thread's fallback never releases an orphan (it waits for an allowed thread).
        PieceWorld w;
        w.appear({kCrow, kPiece});
        w.crow.set_restore(1);
        w.appear({kCrow}, {kS2});
        g_native.clear();
        g_now = 1000 + hold::kNpcFallbackMs;
        hold_test::server_tick();
        CHECK(g_native.empty() && hold_test::family_size() == 1 && hold_test::family_counters().deferred == 1);
    }
    {
        // 7. probe: nothing withheld (the piece goes to both at once, before its parent's re-announce), counted.
        PieceWorld w(1);
        w.appear({kCrow, kPiece});
        CHECK(g_native.size() == 2 && native_has(0, {kS1, kS2}, {kPiece}) && native_has(1, {kS1}, {kCrow}));
        CHECK(hold_test::family_size() == 0);
        auto c = hold_test::family_counters();
        CHECK(c.would == 1 && c.held == 0 && c.seen == 1);
        g_native.clear();
        w.appear({kPiece});  // its parent still held from user 2: would again
        CHECK(native_has(0, {kS1, kS2}, {kPiece}) && hold_test::family_counters().would == 2);
        hold_test::network_tick();
        CHECK(hold_test::lines().find("WOULD be withheld") != std::string::npos);
    }
    {
        // 8. Off: exactly as before (not even looked at).
        PieceWorld w(0);
        w.appear({kCrow, kPiece});
        CHECK(native_has(0, {kS1, kS2}, {kPiece}) && native_has(1, {kS1}, {kCrow}));
        CHECK(hold_test::family_size() == 0 && hold_test::family_counters().seen == 0);
    }
    {
        // 9. A full table: the piece native (as without the key); its room is never promised without an entry.
        PieceWorld w;
        hold_test::fill_family(hold::kFamilyTableCapacity);
        w.appear({kCrow, kPiece});
        CHECK(native_has(0, {kS1, kS2}, {kPiece}) && native_has(1, {kS1}, {kCrow}));
        const auto c = hold_test::family_counters();
        CHECK(c.full == 1 && c.held == 0 && c.unrecorded == 0);
    }
    {
        // 10. Partial: a third user receives it whose session its parent is not withheld from -> native.
        PieceWorld w;
        w.appear({kCrow});
        FakeUser third(kOtherKey);
        constexpr uintptr_t kS3 = 0x53000;
        t_sessions.push_back({kS3, third.a()});
        g_native.clear();
        w.appear({kPiece}, {kS1, kS2, kS3});
        CHECK(native_has(0, {kS1, kS2, kS3}, {kPiece}) && hold_test::family_size() == 0);
        CHECK(hold_test::family_counters().partial == 1);
    }
    {
        // 11. The hierarchy component is another actor's and +0xAC names nobody: no parent, native (the record's
        //     own parent, +0xAC, is the source since the 2026-10-11 regression; "unlinked" = neither readable).
        PieceWorld w;
        w.piece.set_parent(kCrow, 0x5000);
        w.appear({kCrow, kPiece});
        CHECK(native_has(0, {kS1, kS2}, {kPiece}) && native_has(1, {kS1}, {kCrow}) && hold_test::family_size() == 0);
        CHECK(hold_test::family_counters().unlinked == 0 && hold_test::family_counters().seen == 0);
    }
    {
        // 12. A piece whose parent is NOT withheld (restore 1, goes to both now): native, not counted as partial.
        PieceWorld w;
        w.crow.set_restore(1);
        w.appear({kCrow, kPiece});
        CHECK(g_native.size() == 1 && native_has(0, {kS1, kS2}, {kCrow, kPiece}));
        const auto c = hold_test::family_counters();
        CHECK(c.partial == 0 && c.would == 0 && c.held == 0);
    }
    {
        // 13. User 2 leaving: the pieces withheld from it go with their parents.
        PieceWorld w;
        w.appear({kCrow, kPiece});
        t_leaving = true;
        hold_test::network_tick();
        CHECK(hold_test::family_size() == 0 && hold_test::npc_size() == 0 && hold_test::family_counters().cleared == 1);
        t_leaving = false;
    }
}

void test_hold_pieces_acks() {
    // With observer_hold_acks the piece's own packets (2697, 2356) wait for its 2005, right after its parent's.
    constexpr uint32_t kCrow = PieceWorld::kCrow, kPiece = PieceWorld::kPiece;
    t_live_s2 = kS2;
    observer_hold_acks_configure(true);
    observer_hold_acks_clear(oha::Discard::Epoch);
    {
        PieceWorld w;
        w.seams.notify = fake_notify_2005;
        g_out.clear();
        w.appear({kCrow, kPiece});
        CHECK(g_out.empty() && observer_hold_acks_keys() == 2);
        take(npc_packet(2697, kPiece, 1));
        take(npc_packet(2356, kPiece, 2));
        take(npc_packet(2404, kCrow, 3));
        CHECK(g_out.empty());
        w.release_crow(1160);
        CHECK(g_out.size() == 4);
        if (g_out.size() == 4) {
            // One 2005 with the Crow then the piece, then each key's packets.
            CHECK(op_of(g_out[0]) == 2005 && key_of(g_out[0]) == kCrow);
            std::vector<uint32_t> keys;
            oha::announce_keys(std::span<const uint8_t>(g_out[0]).subspan(5), keys);
            CHECK(keys == std::vector<uint32_t>({kCrow, kPiece}));
            size_t piece_pkts = 0;
            for (size_t i = 1; i < g_out.size(); ++i) piece_pkts += key_of(g_out[i]) == kPiece ? 1 : 0;
            CHECK(piece_pkts == 2);
        }
        CHECK(observer_hold_acks_keys() == 0);
    }
    {
        // Dropped with its parent: its packets go, nothing is sent.
        PieceWorld w;
        w.seams.notify = fake_notify_2005;
        g_out.clear();
        w.appear({kCrow, kPiece});
        take(npc_packet(2697, kPiece, 1));
        w.sector.set({w.host.a()});
        w.release_crow(1050);
        CHECK(g_out.empty() && observer_hold_acks_keys() == 0);
    }
    observer_hold_acks_configure(false);
    t_live_s2 = 0;
}

// ---------------------------------------------------------------- host: observer_hold_family (P8 S15/S16)
//
// pendientes/p8-s14-viajeros.md §7: user 2 never gets an actor record before its parent's (the record's +0xAC,
// the server's hierarchy +0x1C). The fake NotifyAppear serialises every 2005 for user 2's live session 2 with
// each record's parent and hands it to take_frame's hook, whose orphan count must stay 0.

// An ActorLoadDataAck 2005 payload: records [u16 len][u8 type][u32 key][8 bytes][u32 parent][u16 n = 0].
std::vector<std::pair<uint32_t, uint32_t>> g_fam_parent;  // key -> its parent (0: none)
std::vector<uint8_t> family_2005(const std::vector<uint32_t>& keys, uint8_t type = 3) {
    std::vector<uint8_t> payload;
    for (const uint32_t k : keys) {
        uint32_t parent = 0;
        for (const auto& [key, p] : g_fam_parent) {
            if (key == k) parent = p;
        }
        const auto at = static_cast<uint32_t>(payload.size());
        constexpr uint16_t kLen = 1 + 4 + 8 + 4 + 2;
        payload.resize(at + 2 + kLen, 0);
        put(payload.data(), at, kLen);
        payload[at + 2] = type;
        put(payload.data(), at + 3, k);
        put(payload.data(), at + 2 + 1 + 4 + 8, parent);
    }
    return payload;
}

// Records per 2005 (0: one 2005 per NotifyAppear). The engine splits a long NotifyAppear into several 2005s
// (regression 2026-10-11: a 256-key release went out as 2005s of 3-5 large records).
size_t g_fam_chunk = 0;

void fake_notify_family(uintptr_t flag, const GameVec* s, const GameVec* k) {
    fake_notify(flag, s, k);
    const auto* sd = static_cast<const uintptr_t*>(s->data);
    const auto* kd = static_cast<const uint32_t*>(k->data);
    for (uint32_t i = 0; i < s->count; ++i) {
        if (sd[i] != kS2) continue;
        const size_t step = g_fam_chunk ? g_fam_chunk : k->count;
        for (size_t at = 0; at < k->count; at += step) {
            const size_t end = std::min<size_t>(k->count, at + step);
            const std::vector<uint8_t> p = family_2005(std::vector<uint32_t>(kd + at, kd + end));
            child_hold_on_user2_out(2005, p);
        }
    }
}

std::vector<uint8_t> remove_2516(uint32_t key) {
    std::vector<uint8_t> p(10, 0);
    p[0] = 4;
    p[5] = 0x1E;
    put(p.data(), 6, key);
    return p;
}

struct FamilyWorld : World {
    std::vector<std::unique_ptr<FakeActor>> actors;
    explicit FamilyWorld(uint8_t mode = 2) {
        t_live_s2 = kS2;
        seams.notify = fake_notify_family;
        g_fam_parent.clear();
        g_fam_chunk = 0;
        hold_test::set_family(mode);
    }
    ~FamilyWorld() {
        t_live_s2 = 0;
        g_fam_parent.clear();
        g_fam_chunk = 0;
    }
    // An NPC the host controls in the shared sector; parent != 0 names it in its hierarchy component, or (ac) only
    // in the server actor's +0xAC (no hierarchy component: most actors, regression 2026-10-11).
    FakeActor& add(uint32_t key, uint8_t type, uint8_t restore, uint32_t parent, bool cc = true, bool ac = false) {
        actors.push_back(std::make_unique<FakeActor>(key, 0, kHostKey, type, restore));
        FakeActor& a = *actors.back();
        if (!cc) a.no_cc();
        if (parent && ac) put(a.p(), hold::kActorParent, parent);
        if (parent && !ac) a.set_parent(parent);
        a.set_sector(&sector);
        sam.add(a);
        g_fam_parent.push_back({key, parent});
        return a;
    }
    static void pass_at(uint64_t at) {
        g_now = at;
        FakeReq other(0xB0100555);
        hold_test::start_action(other.wrapper.a());
    }
    static void release(FakeActor& a, uint32_t key, uint64_t at) {
        a.set_restore(1);
        g_now = at;
        FakeReq req(key);
        hold_test::start_action(req.wrapper.a());
    }
};

// S33: retirar una familia cancela también miembros retenidos, Acks, snapshots
// tardíos y anuncios mixtos, sin afectar a un NPC sano ni al RemoveActor.
void test_npc_local_retirement() {
    constexpr uint32_t root = 0xB0100310, child = 0xB0100311, grandchild = 0xB0100312, healthy = 0xB0100313;
    FamilyWorld w;
    w.add(root, 3, 0, 0);
    w.add(child, 6, 1, root);
    w.add(grandchild, 7, 1, child, false);
    w.add(healthy, 3, 1, 0);
    child_hold_set_retirement(true);
    observer_hold_acks_configure(true);
    observer_hold_acks_clear(oha::Discard::Epoch);
    w.appear({grandchild, child, root});
    CHECK(hold_test::npc_size() == 1 && hold_test::family_size() == 2);
    g_out.clear();
    take(npc_packet(2697, child, 1));
    take(npc_packet(2404, root, 2));
    CHECK(g_out.empty() && observer_hold_acks_keys() == 3);
    const uint32_t roots[] = {root};
    CHECK(!child_hold_retire_family(server_epoch() + 1, 10, roots));
    CHECK(hold_test::npc_size() == 1); // época incorrecta: nada se cancela
    CHECK(!child_hold_retire_family(server_epoch(), 10, roots)); // plan incompleto: no cancela ningún miembro
    CHECK(!child_hold_retired(root) && hold_test::family_size() == 2 && observer_hold_acks_keys() == 3);
    const uint32_t complete[] = {root, child, grandchild};
    CHECK(child_hold_retire_family(server_epoch(), 10, complete));
    CHECK(hold_test::npc_size() == 0 && hold_test::family_size() == 0 && observer_hold_acks_keys() == 0);
    CHECK(child_hold_retired(root) && child_hold_retired(child) && child_hold_retired(grandchild));
    g_native.clear();
    w.appear({child, healthy, root, grandchild}); // un snapshot tardío del motor
    CHECK(g_native.size() == 1 && native_has(0, {kS1, kS2}, {healthy}));
    CHECK(!hold_test::family_known(root) && !hold_test::family_known(child));
    CHECK(hold_test::family_known(healthy));
    g_out.clear();
    take(npc_packet(2697, child, 3)); // tampoco pasa tras desaparecer la retención
    take(npc_packet(2404, healthy, 4));
    CHECK(g_out.size() == 1 && key_of(g_out[0]) == healthy);
    auto mixed = wire(2005, family_2005({root, healthy, child}));
    CHECK(observer_hold_acks_filter_retired(mixed));
    std::vector<uint32_t> keys;
    oha::announce_keys(std::span<const uint8_t>(mixed).subspan(5), keys);
    CHECK(keys == std::vector<uint32_t>({healthy}));
    CHECK(static_cast<size_t>(mixed[3] | (mixed[4] << 8)) == mixed.size() - 5);
    auto removed = wire(2516, remove_2516(root));
    const auto original_removed = removed;
    CHECK(observer_hold_acks_filter_retired(removed) && removed == original_removed);
    constexpr uint32_t late = 0xB0100314;
    w.add(late, 6, 1, root); // nació DESPUÉS de retirar el padre, fuera del plan original
    g_native.clear();
    w.appear({late});
    // Revisión: se bloquea el alta y la lápida se escribe desde su propia publicación (antes fallaba siempre).
    CHECK(g_native.empty() && child_hold_retired(late));
    CHECK(child_hold_retire_family(server_epoch(), 10, std::span<const uint32_t>(&late, 1)));
    CHECK(child_hold_retired(late));
    CHECK(!hold_test::family_known(late));
    child_hold_actor_created(server_epoch(), 10, child); // confirmación vieja no revive
    CHECK(child_hold_retired(child));
    // Creación nueva real, vista en el mid hook: sólo se apunta; la lápida se levanta en la siguiente publicación.
    child_hold_actor_born(server_epoch(), 11, child);
    child_hold_actor_born(server_epoch(), 12, healthy); // sin lápida: nada que apuntar
    CHECK(child_hold_retired(child));
    child_hold_settle_births();
    CHECK(!child_hold_retired(child) && child_hold_retired(root));
    observer_hold_acks_configure(false);
}

// Un2005 ya construido en otra hebra no cruza la barrera tras el retiro.
void test_npc_retirement_publication_race() {
    World w;
    child_hold_set_retirement(true);
    observer_hold_acks_configure(false); // la contención no depende de hold_acks=1
    constexpr uint32_t key = 0xB0100390;
    // Una publicación nativa activa NO espera un retiro: éste se pospone sin marcar claves.
    {
        NpcPublicationScope active;
        CHECK(!child_hold_retire_family(server_epoch(), 19, std::span<const uint32_t>(&key, 1)));
        CHECK(!child_hold_retired(key));
    }
    // Revisión: desde su PROPIA publicación (un hijo tardío) la lápida sí se escribe; una de otro hilo la aplaza.
    constexpr uint32_t own = 0xB0100391, other_key = 0xB0100392;
    {
        NpcPublicationScope active;
        CHECK(child_hold_retire_family(server_epoch(), 19, std::span<const uint32_t>(&own, 1), true));
        CHECK(child_hold_retired(own));
    }
    {
        std::atomic<bool> inside{false}, done{false};
        std::thread other([&] {
            NpcPublicationScope active;
            inside.store(true, std::memory_order_release);
            while (!done.load(std::memory_order_acquire)) std::this_thread::yield();
        });
        while (!inside.load(std::memory_order_acquire)) std::this_thread::yield();
        CHECK(!child_hold_retire_family(server_epoch(), 19, std::span<const uint32_t>(&other_key, 1), true));
        CHECK(!child_hold_retired(other_key));
        done.store(true, std::memory_order_release);
        other.join();
    }
    auto publication = observer_hold_acks_publication_guard();
    std::atomic<bool> entering{false};
    g_out.clear();
    std::thread emitter([&] {
        auto packet = wire(2005, family_2005({key}));
        entering.store(true, std::memory_order_release);
        take(std::move(packet));
    });
    while (!entering.load(std::memory_order_acquire)) std::this_thread::yield();
    CHECK(child_hold_retire_family(server_epoch(), 20, std::span<const uint32_t>(&key, 1)));
    publication.unlock();
    emitter.join();
    CHECK(g_out.empty());
}

void test_npc_retirement_bounds() {
    nret::Retired<3> retired;
    constexpr uint32_t a = 0xB0100001, b = 0xB0100002, c = 0xB0100003, d = 0xB0100004;
    const uint32_t first[] = {a, b, a}, full[] = {c, d};
    CHECK(retired.retire(1, 10, first) && retired.size() == 2);
    CHECK(!retired.retire(1, 11, full)); // todo o nada, no media familia
    CHECK(retired.size() == 2 && !retired.contains(c, 1));
    retired.created(1, 9, a);
    CHECK(retired.contains(a, 1));
    retired.created(1, 12, a);
    CHECK(!retired.contains(a, 1) && retired.contains(b, 1));
    CHECK(retired.retire(2, 13, full));
    CHECK(!retired.contains(b, 2) && retired.contains(c, 2));
    CHECK(!retired.retire(1, 99, first)); // no revivir épocas caducadas
    auto malformed = wire(2005, {6, 0, 3, 1, 0});
    const auto before = malformed;
    CHECK(nret::filter_packet(malformed, [](uint32_t) { return true; }) == nret::Filter::Malformed);
    CHECK(malformed == before);
    // Un replay2005 sin las claves retiradas queda byte a byte igual.
    auto other = wire(2005, family_2005({a}));
    const auto unchanged = other;
    CHECK(nret::filter_packet(other, [&](uint32_t k) { return retired.contains(k, 2); }) == nret::Filter::Unchanged);
    CHECK(other == unchanged);
}

void test_family_logic() {
    // KeySet: linear probing with backward-shift deletion, against a reference set.
    fam::KeySet<64> ks;
    std::vector<uint32_t> ref;
    uint32_t x = 12345;
    for (int step = 0; step < 4000; ++step) {
        x = x * 1103515245u + 12345u;
        const uint32_t key = 0xB0100000u + ((x >> 8) % 40);
        const bool in = std::find(ref.begin(), ref.end(), key) != ref.end();
        if ((x >> 4) & 1) {
            CHECK(ks.insert(key) == !in);
            if (!in) ref.push_back(key);
        } else {
            CHECK(ks.erase(key) == in);
            if (in) ref.erase(std::find(ref.begin(), ref.end(), key));
        }
        if (step % 97 == 0) {
            for (uint32_t k = 0xB0100000u; k < 0xB0100000u + 40; ++k) {
                CHECK(ks.contains(k) == (std::find(ref.begin(), ref.end(), k) != ref.end()));
            }
        }
    }
    CHECK(ks.size() == ref.size() && !ks.contains(0));
    // Full: it overflows and answers "present" for everything (fail open) until cleared.
    fam::KeySet<16> small;
    for (uint32_t k = 1; k <= fam::KeySet<16>::capacity(); ++k) CHECK(small.insert(0xB0000000u + k));
    CHECK(!small.insert(0xB0000100u) && small.overflow() && small.contains(0xBFFFFFFFu));
    small.clear();
    CHECK(!small.overflow() && !small.contains(0xB0000001u) && small.size() == 0);

    // The 2005 records and their family tail (analyze_capture.py's heuristic).
    g_fam_parent = {{0xB0100201, 0xB0100200}};
    const std::vector<uint8_t> lone = family_2005({0xB0100201});
    uint32_t parent = 0;
    uint16_t n = 0;
    size_t seen = fam::scan_2005(lone, [&](const fam::Record& r) {
        CHECK(r.key == 0xB0100201 && r.type == 3 && fam::record_family(r.bytes, parent, n));
    });
    CHECK(seen == 1 && parent == 0xB0100200 && n == 0);
    // A leader's record with one child: its tail is parent 0 | n 1 | the child.
    std::vector<uint8_t> leader(1 + 4 + 8 + 4 + 2 + 4, 0);
    leader[0] = 3;
    put(leader.data(), 1, uint32_t{0xB0100200});
    put(leader.data(), 1 + 4 + 8 + 4, uint16_t{1});
    put(leader.data(), 1 + 4 + 8 + 4 + 2, uint32_t{0xB0100201});
    CHECK(fam::record_family(leader, parent, n) && parent == 0 && n == 1);

    // The orphan count: a child before its parent, the parent in the same 2005, a 2516, type-0 records.
    fam::OrphanCounter<64> oc;
    auto r = oc.on_2005(lone);
    CHECK(r.records == 1 && r.orphans == 1 && r.first.key == 0xB0100201 && r.first.parent == 0xB0100200);
    g_fam_parent.push_back({0xB0100200, 0});
    r = oc.on_2005(family_2005({0xB0100201, 0xB0100200}));  // its parent in the same 2005 (any order)
    CHECK(r.records == 2 && r.orphans == 0);
    r = oc.on_2005(lone);  // its parent alive on that client now
    CHECK(r.orphans == 0 && oc.alive(0xB0100200));
    CHECK(oc.on_2516(remove_2516(0xB0100200)) == 1 && !oc.alive(0xB0100200));
    r = oc.on_2005(lone);
    CHECK(r.orphans == 1 && oc.orphans() == 2 && oc.records() == 5);
    r = oc.on_2005(family_2005({0xB0100201}, 0));  // a type-0 (user) record is never a child
    CHECK(r.orphans == 0);
    g_fam_parent.clear();
}

void test_family_cc_child() {
    // S15 B010012D / B010012E: a leader withheld from user 2 (restore 0) and its member with CC (restore 1) in the
    // same NotifyAppear, the member listed first. Before: the member went to both at once, the leader to the host.
    constexpr uint32_t kLeader = 0xB0100310, kMember = 0xB0100311;
    FamilyWorld w;
    FakeActor& leader = w.add(kLeader, 3, 0, 0);
    w.add(kMember, 3, 1, kLeader);
    w.appear({kMember, kLeader});
    CHECK(g_native.size() == 1 && native_has(0, {kS1}, {kMember, kLeader}));
    HoldEntry e;
    CHECK(hold_test::family_size() == 1 && hold_test::family_entry(0, e) && e.key == kMember && e.parent == kLeader &&
          e.observer == kS2 && e.family_why == static_cast<uint8_t>(FamilyWhy::ParentInCall));
    CHECK(hold_test::npc_size() == 1);
    g_native.clear();
    FamilyWorld::release(leader, kLeader, 1010);
    CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kLeader, kMember}));
    CHECK(hold_test::family_size() == 0 && hold_test::npc_size() == 0);
    const auto c = hold_test::family_counters();
    CHECK(c.held == 1 && c.released == 1 && c.with_parent == 1 && c.out_records == 2 && c.out_orphans == 0);
    CHECK(hold_test::family_known(kLeader) && hold_test::family_known(kMember));
    hold_test::network_tick();
    const std::string lines = hold_test::lines();
    CHECK(lines.find("held member 0xB0100311") != std::string::npos);
    CHECK(lines.find("behind its parent, in its release") != std::string::npos);
}

void test_family_parent_later() {
    // S15 B01000A4 / S14: a carried piece whose sector reached user 2 before its parent's: it waits for its
    // parent's announcement and goes right after it.
    constexpr uint32_t kCart = 0xB0100320, kLoad = 0xB0100321;
    FamilyWorld w;
    w.add(kCart, 7, 1, 0);
    w.add(kLoad, 7, 0, kCart, false);
    w.appear({kLoad});
    CHECK(g_native.size() == 1 && native_has(0, {kS1}, {kLoad}) && hold_test::family_size() == 1);
    HoldEntry e;
    CHECK(hold_test::family_entry(0, e) && e.family_why == static_cast<uint8_t>(FamilyWhy::ParentUnknown));
    g_native.clear();
    g_now = 1020;
    w.appear({kCart});  // restore 1: to both at once; the piece right after it, to user 2
    CHECK(g_native.size() == 2 && native_has(0, {kS1, kS2}, {kCart}) && native_has(1, {kS2}, {kLoad}));
    CHECK(hold_test::family_size() == 0);
    const auto c = hold_test::family_counters();
    CHECK(c.held_unknown == 1 && c.after_parent == 1 && c.out_orphans == 0 && c.out_records == 2);
    hold_test::network_tick();
    CHECK(hold_test::lines().find("right after its parent reached that session") != std::string::npos);
}

void test_family_load_defer() {
    // S15 (user 2's 2549 .. 2762): a group withheld by observer_load_defer (leader and member, restore 1) and the
    // leader's piece, in user 2's snapshot. After its 2762 one 2005, parents first.
    constexpr uint32_t kLead = 0xB0100330, kMemb = 0xB0100331, kPiece = 0xB0100332;
    FamilyWorld w;
    hold_test::set_load_defer(2, true);
    w.add(kLead, 3, 1, 0);
    w.add(kMemb, 3, 1, kLead);
    w.add(kPiece, 7, 0, kLead, false);
    w.appear({kPiece, kMemb, kLead}, {kS2});
    CHECK(g_native.empty() && hold_test::npc_size() == 2 && hold_test::family_size() == 1);
    hold_test::set_load_defer(2, false);  // its 2762
    FamilyWorld::pass_at(1000 + hold::kNpcPassMs);
    CHECK(g_native.size() == 1 && g_native[0].sessions == std::vector<uintptr_t>{kS2} &&
          g_native[0].keys == std::vector<uint32_t>({kLead, kMemb, kPiece}));
    CHECK(hold_test::npc_size() == 0 && hold_test::family_size() == 0);
    const auto c = hold_test::family_counters();
    CHECK(c.with_parent == 1 && c.out_orphans == 0 && c.out_records == 3);
    CHECK(hold_test::load_defer_counters().released == 2);
}

void test_family_multilevel() {
    // A rider (with CC) on a mount withheld from user 2, and the rider's piece: three levels, one 2005.
    constexpr uint32_t kMount = 0xB0100340, kRider = 0xB0100341, kBag = 0xB0100342;
    {
        FamilyWorld w;
        FakeActor& mount = w.add(kMount, 6, 0, 0);
        w.add(kRider, 3, 1, kMount);
        w.add(kBag, 7, 0, kRider, false);
        w.appear({kBag, kRider, kMount});
        CHECK(g_native.size() == 1 && native_has(0, {kS1}, {kBag, kRider, kMount}) && hold_test::family_size() == 2);
        g_native.clear();
        FamilyWorld::release(mount, kMount, 1010);
        CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kMount, kRider, kBag}));
        const auto c = hold_test::family_counters();
        CHECK(c.with_parent == 2 && c.out_orphans == 0 && hold_test::family_size() == 0);
    }
    {
        // Dropped with the mount (user 2 out of range): the whole family goes, nothing is published.
        FamilyWorld w;
        FakeActor& mount = w.add(kMount, 6, 0, 0);
        w.add(kRider, 3, 1, kMount);
        w.add(kBag, 7, 0, kRider, false);
        w.appear({kBag, kRider, kMount});
        w.sector.set({w.host.a()});
        g_native.clear();
        FamilyWorld::release(mount, kMount, 1010);
        CHECK(g_native.empty() && hold_test::family_size() == 0 && hold_test::npc_size() == 0);
        CHECK(hold_test::family_counters().dropped == 2 && hold_test::family_counters().released == 0);
    }
}

void test_family_orphan_timeout() {
    // A parent that never reaches user 2: the member is announced natively after hold::kFamilyOrphanMs (an
    // orphan, as without the key; the count sees it).
    constexpr uint32_t kNone = 0xB0100350, kLost = 0xB0100351;
    FamilyWorld w;
    w.add(kLost, 7, 0, kNone, false);
    w.appear({kLost});
    CHECK(native_has(0, {kS1}, {kLost}) && hold_test::family_size() == 1);
    g_native.clear();
    FamilyWorld::pass_at(1000 + hold::kFamilyOrphanMs - hold::kNpcPassMs);
    CHECK(g_native.empty() && hold_test::family_size() == 1);
    FamilyWorld::pass_at(1000 + hold::kFamilyOrphanMs);
    CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kLost}) && hold_test::family_size() == 0);
    const auto c = hold_test::family_counters();
    CHECK(c.orphan_timeouts == 1 && c.out_orphans == 1);
    hold_test::network_tick();
    const std::string lines = hold_test::lines();
    CHECK(lines.find("WRN Session2 observer hold family: released member 0xB0100351") != std::string::npos &&
          lines.find("ITS PARENT NEVER CAME") != std::string::npos);
    CHECK(lines.find("ORPHAN queued for user 2: record 0xB0100351") != std::string::npos);
}

void test_family_release_gate() {
    // An NPC observer_hold withholds (restore 0) whose leader user 2 has not got yet: when its first action comes
    // it waits for its leader (a family entry) and goes right after the leader reaches user 2.
    constexpr uint32_t kLeader = 0xB0100360, kMember = 0xB0100361;
    FamilyWorld w;
    w.add(kLeader, 3, 1, 0);
    FakeActor& member = w.add(kMember, 3, 0, kLeader);
    w.appear({kMember});
    CHECK(native_has(0, {kS1}, {kMember}) && hold_test::npc_size() == 1 && hold_test::family_size() == 0);
    g_native.clear();
    FamilyWorld::release(member, kMember, 1010);
    CHECK(g_native.empty() && hold_test::npc_size() == 0 && hold_test::family_size() == 1);
    HoldEntry e;
    CHECK(hold_test::family_entry(0, e) && e.npc_origin && e.parent == kLeader &&
          e.family_why == static_cast<uint8_t>(FamilyWhy::ReleaseBlocked));
    CHECK(hold_test::family_counters().converted == 1);
    g_now = 1020;
    w.appear({kLeader});
    CHECK(g_native.size() == 2 && native_has(0, {kS1, kS2}, {kLeader}) && native_has(1, {kS2}, {kMember}));
    CHECK(hold_test::counters().released == 1);  // an observer_hold release, counted as such
    const auto c = hold_test::family_counters();
    CHECK(c.after_parent == 1 && c.out_orphans == 0);
}

void test_family_reverse() {
    // User 2 loses the leader (2516) and keeps the member; the member is announced again alone, then the leader
    // comes back withheld (restore 0): still parent first.
    constexpr uint32_t kLeader = 0xB0100370, kMember = 0xB0100371;
    FamilyWorld w;
    FakeActor& leader = w.add(kLeader, 3, 1, 0);
    w.add(kMember, 3, 1, kLeader);
    w.appear({kLeader, kMember});
    CHECK(g_native.size() == 1 && native_has(0, {kS1, kS2}, {kLeader, kMember}));
    CHECK(hold_test::family_known(kLeader) && hold_test::family_known(kMember));
    child_hold_on_user2_out(2516, remove_2516(kLeader));
    CHECK(!hold_test::family_known(kLeader) && hold_test::family_known(kMember));
    g_native.clear();
    w.appear({kMember});
    CHECK(native_has(0, {kS1}, {kMember}) && hold_test::family_size() == 1);
    leader.set_restore(0);
    w.appear({kLeader});
    CHECK(native_has(1, {kS1}, {kLeader}) && hold_test::npc_size() == 1 && hold_test::family_size() == 1);
    g_native.clear();
    FamilyWorld::release(leader, kLeader, 1010);
    CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kLeader, kMember}) && hold_test::family_size() == 0);
    CHECK(hold_test::family_counters().out_orphans == 0);
}

void test_family_valve_and_source() {
    // The safety valve: members that wait in vain for parents user 2 never gets (the hierarchy would name parents
    // the engine does not send) switch the "not announced yet" rule off; then such a member goes at once.
    FamilyWorld w;
    std::vector<uint32_t> keys;
    for (uint32_t i = 0; i < 16; ++i) {
        w.add(0xB0100400 + i, 7, 0, 0xB0100500 + i, false);
        keys.push_back(0xB0100400 + i);
    }
    w.appear(keys);
    CHECK(hold_test::family_size() == 16 && !hold_test::family_counters().unknown_off);
    FamilyWorld::pass_at(1000 + hold::kFamilyOrphanMs);
    auto c = hold_test::family_counters();
    CHECK(c.orphan_timeouts == 16 && c.unknown_off && hold_test::family_size() == 0);
    hold_test::network_tick();
    CHECK(hold_test::lines().find("rule is OFF for the rest of the process") != std::string::npos);
    g_native.clear();
    w.add(0xB0100420, 7, 0, 0xB0100520, false);
    w.appear({0xB0100420});
    CHECK(g_native.size() == 1 && native_has(0, {kS1, kS2}, {0xB0100420}) && hold_test::family_size() == 0);

    // The source probe: the server actor's own +0xAC against the hierarchy's parent (counted, never decides).
    FamilyWorld w2;
    FakeActor& same = w2.add(0xB0100430, 3, 1, 0xB0100431);
    put(same.p(), hold::kActorParent, uint32_t{0xB0100431});
    FakeActor& other = w2.add(0xB0100432, 3, 1, 0xB0100431);
    put(other.p(), hold::kActorParent, uint32_t{0xB0100499});
    FakeActor& only = w2.add(0xB0100433, 3, 1, 0);
    put(only.p(), hold::kActorParent, uint32_t{0xB0100431});
    w2.add(0xB0100431, 3, 1, 0);
    w2.appear({0xB0100431, 0xB0100430, 0xB0100432, 0xB0100433});
    c = hold_test::family_counters();
    CHECK(c.ac_same == 1 && c.ac_other == 1 && c.ac_only == 1 && c.out_orphans == 0);
}

void test_family_ac_source() {
    // One-PC regression 2026-10-11 (observer_hold_family=1, 65 orphans): most actors have no hierarchy component
    // (parent 0) but their server +0xAC names the parent their record carries.
    {
        // 13:09:54, user 2's observer trip: a load_defer parent and its piece (+0xAC only) in its snapshot. Before:
        // the piece went out alone (7D -> 7C, A3 -> A2, 273 -> 264).
        constexpr uint32_t kParent = 0xB010047C, kPiece = 0xB010047D;
        FamilyWorld w;
        hold_test::set_load_defer(2, true);
        w.add(kParent, 3, 1, 0);
        w.add(kPiece, 7, 0, kParent, false, true);
        w.appear({kPiece, kParent}, {kS2});
        CHECK(g_native.empty() && hold_test::npc_size() == 1 && hold_test::family_size() == 1);
        hold_test::set_load_defer(2, false);
        FamilyWorld::pass_at(1000 + hold::kNpcPassMs);
        CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kParent, kPiece}));
        const auto c = hold_test::family_counters();
        CHECK(c.out_orphans == 0 && c.ac_only == 1 && c.with_parent == 1);
    }
    {
        // 13:09:59: observer_hold's periodic release of 256 keys went out as 2005s of a few records each, a member
        // before its leader (300 -> 2FC, 263 -> 262). Parents first by depth, from +0xAC.
        constexpr uint32_t kLeader = 0xB01004FC, kMember = 0xB0100500, kOther = 0xB01004F0;
        FamilyWorld w;
        g_fam_chunk = 1;
        FakeActor& leader = w.add(kLeader, 3, 0, 0);
        FakeActor& member = w.add(kMember, 6, 0, kLeader, true, true);
        FakeActor& other = w.add(kOther, 3, 0, 0);
        w.appear({kLeader, kOther, kMember});  // the table: leader, other, member (released highest index first)
        CHECK(hold_test::npc_size() == 3 && hold_test::family_size() == 0);
        leader.set_restore(1);
        member.set_restore(1);
        other.set_restore(1);
        g_native.clear();
        FamilyWorld::pass_at(1000 + hold::kNpcPassMs);
        CHECK(g_native.size() == 1 && g_native[0].sessions == std::vector<uintptr_t>{kS2} &&
              g_native[0].keys.size() == 3);
        if (g_native.size() == 1 && g_native[0].keys.size() == 3) {
            const auto& k = g_native[0].keys;
            const auto at = [&](uint32_t key) { return std::find(k.begin(), k.end(), key) - k.begin(); };
            CHECK(at(kLeader) < at(kMember));
        }
        CHECK(hold_test::family_counters().out_orphans == 0 && hold_test::family_counters().out_records == 3);
    }
    {
        // 13:10:53: a piece announced alone while its parent is not with user 2: it waits for it (+0xAC only).
        constexpr uint32_t kParent = 0xB01004A3, kPiece = 0xB01004A4;
        FamilyWorld w;
        w.add(kParent, 7, 1, 0);
        w.add(kPiece, 7, 0, kParent, false, true);
        w.appear({kPiece});
        CHECK(native_has(0, {kS1}, {kPiece}) && hold_test::family_size() == 1);
        g_native.clear();
        g_now = 1020;
        w.appear({kParent});
        CHECK(g_native.size() == 2 && native_has(0, {kS1, kS2}, {kParent}) && native_has(1, {kS2}, {kPiece}));
        CHECK(hold_test::family_counters().out_orphans == 0);
    }
}

void test_family_field_change_and_gate() {
    // Review 4: a MoveFieldAck 2174 to user 2 empties what it has (the announced set and the orphan count's).
    constexpr uint32_t kLeader = 0xB0100390, kMember = 0xB0100391, kLone = 0xB0100392;
    FamilyWorld w;
    w.add(kLeader, 3, 1, 0);
    w.add(kMember, 3, 1, kLeader, true, true);
    w.appear({kLeader, kMember});
    CHECK(hold_test::family_known(kLeader) && hold_test::family_known(kMember));
    const std::vector<uint8_t> move(17, 0);
    child_hold_on_user2_out(2174, move);
    CHECK(!hold_test::family_known(kLeader) && !hold_test::family_known(kMember));
    // The member alone after the field change: user 2 has no leader any more -> it waits for it.
    g_native.clear();
    w.appear({kMember});
    CHECK(native_has(0, {kS1}, {kMember}) && hold_test::family_size() == 1);
    // The count forgot the leader too: a 2005 with only the member is an orphan for it.
    child_hold_on_user2_out(2005, family_2005({kMember}));
    CHECK(hold_test::family_counters().out_orphans == 1);
    // Review 5: a call only the host receives, with nothing held anywhere, is not even looked at.
    FamilyWorld w2;
    w2.add(kLone, 7, 0, kLeader, false, true);
    w2.appear({kLone}, {kS1});
    CHECK(native_has(0, {kS1}, {kLone}) && hold_test::family_counters().seen == 0 &&
          hold_test::family_counters().ac_only == 0);
}

void test_family_off_and_probe() {
    constexpr uint32_t kLeader = 0xB0100380, kMember = 0xB0100381;
    for (const uint8_t mode : {uint8_t{0}, uint8_t{1}}) {
        // Off and probe: as before (the member to both before its leader); the count sees the orphan.
        FamilyWorld w(mode);
        w.add(kLeader, 3, 0, 0);
        w.add(kMember, 3, 1, kLeader);
        w.appear({kMember, kLeader});
        CHECK(g_native.size() == 2 && native_has(0, {kS1, kS2}, {kMember}) && native_has(1, {kS1}, {kLeader}));
        CHECK(hold_test::family_size() == 0);
        const auto c = hold_test::family_counters();
        CHECK(c.out_orphans == 1 && c.held == 0);
        CHECK(mode ? c.would == 1 && c.seen == 1 : c.seen == 0);
    }
}

// ---------------------------------------------------------------- S35 publication_gate (every exit)
// docs/research/s25/s35-fase-a.md; AUDITORIA.md §1 "Cambios obligatorios": no exit announces to an observer that does
// not control it an actor whose state is not proven complete. Each test drives one exit end to end through the seams.

bool none_to_user2(const std::vector<NotifyCall>& calls) {
    return std::none_of(calls.begin(), calls.end(), [](const NotifyCall& c) {
        return std::find(c.sessions.begin(), c.sessions.end(), kS2) != c.sessions.end();
    });
}
size_t verdicts(pubg::Verdict v) { return hold_test::pub_counters().by_verdict[static_cast<size_t>(v)]; }

void test_pub_gate_timeout() {
    FakeReq other(0xB0100555);
    {
        // Timeout (the S34 exposure A/C): retained, never published with restore 0; its first action releases it.
        World w;
        child_hold_set_publication_gate(2, 60000);
        w.appear({0xB01002A0});
        CHECK(hold_test::npc_size() == 1);
        g_native.clear();
        g_now = 1000 + hold::kNpcTimeoutMs;
        hold_test::start_action(other.wrapper.a());
        CHECK(g_native.empty() && hold_test::npc_size() == 1 && w.consistent());
        auto p = hold_test::pub_counters();
        CHECK(p.retained == 1 && p.would == 0 && verdicts(pubg::Verdict::Restore0) == 1);
        CHECK(hold_test::counters().timeouts == 0);
        HoldEntry e;
        CHECK(hold_test::npc_entry(0, e) && e.unsafe_since_ms == g_now && e.unsafe_retry_ms == g_now + pubg::kRetryMs);
        // Inside its retry: not decided again.
        const uint64_t evaluated = p.evaluated;
        g_now += hold::kNpcPassMs;
        hold_test::start_action(other.wrapper.a());
        CHECK(hold_test::pub_counters().evaluated == evaluated && g_native.empty());
        // Its first action (restore 1): published now (bounded recovery by the controller's state).
        w.crow.set_restore(1);
        g_now = 1000 + hold::kNpcTimeoutMs + pubg::kRetryMs;
        FakeReq crow(0xB01002A0);
        hold_test::start_action(crow.wrapper.a());
        CHECK(g_native.size() == 1 && native_has(0, {kS2}, {0xB01002A0}) && hold_test::npc_size() == 0);
        hold_test::network_tick();
        const std::string lines = hold_test::lines();
        CHECK(lines.find("publication gate: RETAINED, not published 0xB01002A0") != std::string::npos);
        CHECK(lines.find("restore 0 (observer is not its controller)") != std::string::npos);
    }
    {
        // Bounded: past publication_retain_max_ms it is dropped WITHOUT publishing (a WRN line), never announced.
        World w;
        child_hold_set_publication_gate(2, 5000);
        w.appear({0xB01002A0});
        g_native.clear();
        for (uint64_t t = 1000 + hold::kNpcTimeoutMs; t <= 1000 + hold::kNpcTimeoutMs + 5000; t += 1000) {
            g_now = t;
            hold_test::start_action(other.wrapper.a());
        }
        CHECK(g_native.empty() && hold_test::npc_size() == 0);
        const auto p = hold_test::pub_counters();
        CHECK(p.dropped_expired == 1 && p.retained == 5 && hold_test::counters().timeouts == 0);
        hold_test::network_tick();
        CHECK(hold_test::lines().find("WRN Session2 publication gate: DROPPED WITHOUT PUBLISHING 0xB01002A0") !=
              std::string::npos);
    }
    {
        // probe: exactly as before (released with restore 0), counted and logged as WOULD RETAIN.
        World w;
        child_hold_set_publication_gate(1, 60000);
        w.appear({0xB01002A0});
        g_native.clear();
        g_now = 1000 + hold::kNpcTimeoutMs;
        hold_test::start_action(other.wrapper.a());
        CHECK(native_has(0, {kS2}, {0xB01002A0}) && hold_test::counters().timeouts == 1);
        CHECK(hold_test::pub_counters().would == 1 && hold_test::pub_counters().retained == 0);
        hold_test::network_tick();
        CHECK(hold_test::lines().find("WOULD RETAIN (probe, published as before) 0xB01002A0") != std::string::npos);
    }
}

void test_pub_gate_native() {
    // Native (child_hold.cpp npc_release_step: "another actor behind the key: announced natively"): the new actor is
    // read again; restore 0 -> retained; once restore 1 -> published natively.
    World w;
    child_hold_set_publication_gate(2, 60000);
    w.appear({0xB01002A0});
    FakeActor reborn(0xB01002A0, 0, kHostKey, 6, 0);
    reborn.set_sector(&w.sector);
    w.sam.replace(0, reborn);
    g_native.clear();
    g_now = 1050;
    FakeReq crow(0xB01002A0);
    hold_test::start_action(crow.wrapper.a());
    CHECK(g_native.empty() && hold_test::npc_size() == 1 && hold_test::counters().native == 0);
    CHECK(verdicts(pubg::Verdict::Restore0) == 1);
    reborn.set_restore(1);
    g_now = 1050 + pubg::kRetryMs;
    hold_test::start_action(crow.wrapper.a());
    CHECK(native_has(0, {kS2}, {0xB01002A0}) && hold_test::npc_size() == 0 && hold_test::counters().native == 1);
    // Without the gate (0) the restore-0 actor went out natively (the previous behaviour).
    World old;
    old.appear({0xB01002A0});
    FakeActor reborn2(0xB01002A0, 0, kHostKey, 6, 0);
    reborn2.set_sector(&old.sector);
    old.sam.replace(0, reborn2);
    g_native.clear();
    g_now = 1050;
    hold_test::start_action(crow.wrapper.a());
    CHECK(native_has(0, {kS2}, {0xB01002A0}) && hold_test::counters().native == 1);
}

void test_pub_gate_cc_states() {
    // child_hold.cpp:62-69 returned nullopt for "no CC" and "could not read it"; :146-149 called both NoCc (native).
    constexpr uint32_t kUnknown = 0xB0100503, kAbsent = 0xB0100504;
    for (const uint8_t mode : {uint8_t{2}, uint8_t{1}}) {
        World w;
        child_hold_set_publication_gate(mode, 60000);
        FakeActor unknown(kUnknown, 0, kHostKey, 7, 0);
        put(unknown.cc.p(), hold::kCcActor, uintptr_t{0x1234});  // the slot's +8 is not this actor: UNKNOWN
        FakeActor absent(kAbsent, 0, kHostKey, 7, 0);
        absent.no_cc();  // the slot reads 0: proven absent
        for (FakeActor* a : {&unknown, &absent}) {
            a->set_sector(&w.sector);
            w.sam.add(*a);
        }
        g_native.clear();
        w.appear({kUnknown, kAbsent});
        const auto p = hold_test::pub_counters();
        if (mode == 2) {
            // Proven absent: native to both. Unknown: withheld from user 2 (its controller, the host, gets it).
            CHECK(g_native.size() == 2 && native_has(0, {kS1, kS2}, {kAbsent}) && native_has(1, {kS1}, {kUnknown}));
            CHECK(p.cc_unknown_held == 1 && hold_test::npc_size() == 1);
            // Still unreadable when it goes stale: retained (CcUnknown), never published.
            g_native.clear();
            FakeReq other(0xB0100555);
            g_now = 1000 + 3 * hold::kNpcTimeoutMs;
            hold_test::start_action(other.wrapper.a());
            CHECK(g_native.empty() && verdicts(pubg::Verdict::CcUnknown) == 1 && hold_test::npc_size() == 1);
        } else {
            CHECK(g_native.size() == 1 && native_has(0, {kS1, kS2}, {kUnknown, kAbsent}));  // as before
            CHECK(p.cc_unknown_would == 1 && p.cc_unknown_held == 0 && hold_test::npc_size() == 0);
        }
    }
}

void test_pub_gate_full_table() {
    // A full table (child_hold.cpp prepare): native with restore 0 before; withheld WITHOUT an entry now.
    {
        World w;
        child_hold_set_publication_gate(2, 60000);
        hold_test::fill_npc(hold::kNpcTableCapacity);
        g_native.clear();
        w.appear({0xB01002A0});
        CHECK(!g_native.empty() && native_has(0, {kS1}, {0xB01002A0}) && none_to_user2(g_native));
        const auto p = hold_test::pub_counters();
        CHECK(p.withheld_no_entry == 1 && hold_test::counters().full == 1 && hold_test::counters().unrecorded == 0);
        for (size_t i = 0; i < hold_test::npc_size(); ++i) {
            HoldEntry e;
            CHECK(hold_test::npc_entry(i, e) && e.key != 0xB01002A0);
        }
        CHECK(hold_test::npc_reserved() == 0 && w.consistent());
    }
    {
        World w;
        child_hold_set_publication_gate(1, 60000);
        hold_test::fill_npc(hold::kNpcTableCapacity);
        g_native.clear();
        w.appear({0xB01002A0});
        CHECK(native_has(0, {kS1, kS2}, {0xB01002A0}) && hold_test::pub_counters().would_no_entry == 1);
    }
}

void test_pub_gate_family() {
    constexpr uint32_t kLeader = 0xB0100390, kMember = 0xB0100391, kPiece = 0xB0100392, kParent = 0xB0100393;
    {
        // S34 B01003AE: an observer_hold entry due while its leader was not with user 2 waits as a family member and
        // was then announced with restore 0 by the family release. AfterParent with restore 0: retained.
        FamilyWorld w;
        child_hold_set_publication_gate(2, 60000);
        w.add(kLeader, 3, 1, 0);
        FakeActor& member = w.add(kMember, 6, 0, kLeader);
        w.appear({kMember});
        CHECK(native_has(0, {kS1}, {kMember}) && hold_test::npc_size() == 1);
        g_native.clear();
        FamilyWorld::pass_at(1000 + hold::kNpcTimeoutMs);  // its timeout: converted (ReleaseBlocked), not published
        CHECK(g_native.empty() && hold_test::family_size() == 1 && hold_test::family_counters().converted == 1);
        g_now = 1000 + hold::kNpcTimeoutMs + 50;
        w.appear({kLeader});
        CHECK(g_native.size() == 1 && native_has(0, {kS1, kS2}, {kLeader}));  // the leader only
        CHECK(verdicts(pubg::Verdict::Restore0) == 1 && hold_test::family_size() == 1);
        member.set_restore(1);
        g_native.clear();
        FamilyWorld::pass_at(1000 + hold::kNpcTimeoutMs + 50 + pubg::kRetryMs);
        CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kMember}) && hold_test::family_size() == 0);
    }
    {
        // WithParent behind a refused leader: the member (restore 1) never goes before it (ParentInBatch).
        FamilyWorld w;
        child_hold_set_publication_gate(2, 60000);
        FakeActor& leader = w.add(kLeader, 3, 0, 0);
        w.add(kMember, 3, 1, kLeader);
        w.appear({kMember, kLeader});
        CHECK(native_has(0, {kS1}, {kMember, kLeader}) && hold_test::family_size() == 1 && hold_test::npc_size() == 1);
        g_native.clear();
        FamilyWorld::pass_at(1000 + hold::kNpcTimeoutMs);
        CHECK(g_native.empty() && hold_test::npc_size() == 1 && hold_test::family_size() == 1);
        CHECK(verdicts(pubg::Verdict::Restore0) == 1 && verdicts(pubg::Verdict::ParentInBatch) == 1);
        g_native.clear();
        FamilyWorld::release(leader, kLeader, 1000 + hold::kNpcTimeoutMs + pubg::kRetryMs);
        CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kLeader, kMember}));  // parents first, one 2005
        CHECK(hold_test::npc_size() == 0 && hold_test::family_size() == 0);
    }
    {
        // The hierarchy case (S34 C: B0100578 queried B01004BF's layer): a piece without CC whose parent never reaches
        // user 2 but exists with restore 0: OrphanTimeout used to announce it; its dependency is unsafe now.
        FamilyWorld w;
        child_hold_set_publication_gate(2, 60000);
        w.add(kParent, 6, 0, 0);            // in the SAM, host-controlled, restore 0, never announced to user 2
        w.add(kPiece, 7, 0, kParent, false);
        w.appear({kPiece});
        CHECK(native_has(0, {kS1}, {kPiece}) && hold_test::family_size() == 1);
        g_native.clear();
        FamilyWorld::pass_at(1000 + hold::kFamilyOrphanMs);
        CHECK(g_native.empty() && verdicts(pubg::Verdict::Dependency) == 1 && hold_test::family_size() == 1);
    }
    {
        // ... while a parent that is GONE (not in the SAM) is the client's native orphan route: published as before.
        FamilyWorld w;
        child_hold_set_publication_gate(2, 60000);
        w.add(kPiece, 7, 0, kParent, false);
        w.appear({kPiece});
        g_native.clear();
        FamilyWorld::pass_at(1000 + hold::kFamilyOrphanMs);
        CHECK(native_has(0, {kS2}, {kPiece}) && hold_test::family_counters().orphan_timeouts == 1);
        CHECK(hold_test::pub_counters().published == 1);
    }
    {
        // probe: the hierarchy case goes out as before, counted.
        FamilyWorld w;
        child_hold_set_publication_gate(1, 60000);
        w.add(kParent, 6, 0, 0);
        w.add(kPiece, 7, 0, kParent, false);
        w.appear({kPiece});
        g_native.clear();
        FamilyWorld::pass_at(1000 + hold::kFamilyOrphanMs);
        CHECK(native_has(0, {kS2}, {kPiece}) && hold_test::pub_counters().would == 1);
    }
}

// S35 review: the cases the independent review added (losses, epoch, re-announce, deep chain, order without a
// serialized link, members of a parent dropped unpublished, a server-controlled parent, the retention bound per pass).
void test_pub_gate_review() {
    FakeReq other(0xB0100555);
    {
        // Losses: a retained actor that despawns is dropped, never published.
        World w;
        child_hold_set_publication_gate(2, 60000);
        w.appear({0xB01002A0});
        g_native.clear();
        g_now = 1000 + hold::kNpcTimeoutMs;
        hold_test::start_action(other.wrapper.a());
        CHECK(hold_test::npc_size() == 1 && g_native.empty());
        w.sam.remove(0xB01002A0);
        g_now += pubg::kRetryMs;
        hold_test::start_action(other.wrapper.a());
        CHECK(hold_test::npc_size() == 0 && none_to_user2(g_native));
    }
    {
        // A new guest epoch: the retained entry goes, unpublished.
        World w;
        child_hold_set_publication_gate(2, 60000);
        w.appear({0xB01002A0});
        g_native.clear();
        g_now = 1000 + hold::kNpcTimeoutMs;
        hold_test::start_action(other.wrapper.a());
        CHECK(hold_test::npc_size() == 1);
        t_epoch = 2;
        g_now += pubg::kRetryMs;
        hold_test::start_action(other.wrapper.a());
        CHECK(hold_test::npc_size() == 0 && g_native.empty());
        t_epoch = 1;
    }
    {
        // Re-announced while retained (a fresh hold of the same key): its fresh fields, but the retention bound is
        // the first refusal's, and it is checked on every pass (review 6 and 8), not only on a new refusal.
        World w;
        child_hold_set_publication_gate(2, 5000);
        w.appear({0xB01002A0});
        g_native.clear();
        const uint64_t refused = 1000 + hold::kNpcTimeoutMs;
        g_now = refused;
        hold_test::start_action(other.wrapper.a());
        g_now = refused + 2000;
        w.appear({0xB01002A0});  // the engine announces it again: held again (Duplicate refresh)
        HoldEntry e;
        CHECK(hold_test::npc_entry(0, e) && e.since_ms == refused + 2000 && e.unsafe_since_ms == refused);
        g_now = refused + 5000;  // fresh since_ms: no Timeout due now, but the retention is over
        hold_test::start_action(other.wrapper.a());
        CHECK(hold_test::npc_size() == 0 && none_to_user2(g_native));
        const auto p = hold_test::pub_counters();
        CHECK(p.dropped_expired == 1 && p.retain_max_ms >= 5000);
    }
    {
        // Order without a serialized link (review 10): no observer_hold_family, so no depth; the child (restore 1,
        // hierarchy parent = the Crow) is due in the same pass as its parent's Timeout and must not go first.
        World w;
        child_hold_set_publication_gate(2, 60000);
        FakeActor child(0xB01002A1, 0, kHostKey, 6, 0);
        child.set_parent(0xB01002A0);
        child.set_sector(&w.sector);
        w.sam.add(child);
        w.appear({0xB01002A0, 0xB01002A1});  // the child recorded after its parent: decided first (index order)
        CHECK(hold_test::npc_size() == 2);
        child.set_restore(1);
        g_native.clear();
        g_now = 1000 + hold::kNpcTimeoutMs;
        hold_test::start_action(other.wrapper.a());
        CHECK(none_to_user2(g_native) && hold_test::npc_size() == 2);
        CHECK(verdicts(pubg::Verdict::Restore0) == 1 && verdicts(pubg::Verdict::ParentInBatch) == 1);
    }
    {
        // A deep refused chain: leader (restore 0) -> member (restore 1) -> piece (no CC): all three wait, then one
        // 2005 parents first.
        constexpr uint32_t kL = 0xB01003A0, kM = 0xB01003A1, kP = 0xB01003A2;
        FamilyWorld w;
        child_hold_set_publication_gate(2, 60000);
        FakeActor& leader = w.add(kL, 3, 0, 0);
        w.add(kM, 3, 1, kL);
        w.add(kP, 7, 0, kM, false);
        w.appear({kP, kM, kL});
        CHECK(hold_test::npc_size() == 1 && hold_test::family_size() == 2);
        g_native.clear();
        FamilyWorld::pass_at(1000 + hold::kNpcTimeoutMs);
        CHECK(g_native.empty() && verdicts(pubg::Verdict::ParentInBatch) == 2);
        CHECK(hold_test::npc_size() == 1 && hold_test::family_size() == 2);
        FamilyWorld::release(leader, kL, 1000 + hold::kNpcTimeoutMs + pubg::kRetryMs);
        CHECK(g_native.size() == 1 && native_has(0, {kS2}, {kL, kM, kP}));
    }
    {
        // Members of a parent dropped unpublished go with it (review 7).
        constexpr uint32_t kL = 0xB01003B0, kM = 0xB01003B1;
        FamilyWorld w;
        child_hold_set_publication_gate(2, 5000);
        w.add(kL, 3, 0, 0);
        w.add(kM, 3, 1, kL);
        w.appear({kM, kL});
        g_native.clear();
        for (uint64_t t = 1000 + hold::kNpcTimeoutMs; t <= 1000 + hold::kNpcTimeoutMs + 5000; t += 1000) {
            FamilyWorld::pass_at(t);
        }
        CHECK(g_native.empty() && hold_test::npc_size() == 0 && hold_test::family_size() == 0);
        CHECK(hold_test::pub_counters().dropped_expired >= 1);
    }
    {
        // A gimmick piece whose parent no user controls (the server's, controller 0) and whose CC cannot be read: the
        // gate leaves such a parent native, so the piece is not made invisible (review 4).
        constexpr uint32_t kSrv = 0xB01003C0, kPiece = 0xB01003C1;
        FamilyWorld w;
        child_hold_set_publication_gate(2, 60000);
        FakeActor& srv = w.add(kSrv, 7, 0, 0);
        srv.set_controller(0);
        put(srv.cc.p(), hold::kCcActor, uintptr_t{0x1234});  // its CC unreadable
        w.add(kPiece, 7, 0, kSrv, false);
        w.appear({kPiece});
        CHECK(hold_test::family_size() == 1);  // waits for a parent user 2 was not given
        g_native.clear();
        FamilyWorld::pass_at(1000 + hold::kFamilyOrphanMs);
        CHECK(native_has(0, {kS2}, {kPiece}) && verdicts(pubg::Verdict::Dependency) == 0);
    }
    {
        // probe shadow (review 4): a WOULD RETAIN whose controller later produces a usable state counts as "would
        // recover"; one that never does, as "would drop" once the retention is over.
        World w;
        child_hold_set_publication_gate(1, 5000);
        w.appear({0xB01002A0});
        g_native.clear();
        g_now = 1000 + hold::kNpcTimeoutMs;
        hold_test::start_action(other.wrapper.a());  // published as before, recorded in the shadow
        CHECK(native_has(0, {kS2}, {0xB01002A0}) && hold_test::pub_counters().shadow_recorded == 1);
        w.crow.set_restore(1);
        g_now += 2000;
        hold_test::start_action(other.wrapper.a());
        auto p = hold_test::pub_counters();
        CHECK(p.shadow_recovered == 1 && p.shadow_pending == 0);
    }
    {
        World w;
        child_hold_set_publication_gate(1, 5000);
        w.appear({0xB01002A0});
        g_now = 1000 + hold::kNpcTimeoutMs;
        hold_test::start_action(other.wrapper.a());
        g_now += 5000;
        hold_test::start_action(other.wrapper.a());
        const auto p = hold_test::pub_counters();
        CHECK(p.shadow_would_drop == 1 && p.shadow_recovered == 0);
    }
}

// ---------------------------------------------------------------- guest: the ring

void test_ring_bounds() {
    s3::Ring<8> ring;
    CHECK(ring.head() == 0 && ring.oldest() == 0);
    s3::Record r;
    CHECK(!ring.read(0, r));
    for (uint32_t i = 0; i < 5; ++i) {
        s3::Record w;
        w.key = 0xB0000000 + i;
        ring.push(w);
    }
    CHECK(ring.head() == 5 && ring.oldest() == 0);
    CHECK(ring.read(4, r) && r.key == 0xB0000004);
    CHECK(!ring.read(5, r));  // not written yet
    for (uint32_t i = 5; i < 20; ++i) {
        s3::Record w;
        w.key = 0xB0000000 + i;
        ring.push(w);
    }
    CHECK(ring.head() == 20 && ring.oldest() == 12);
    CHECK(!ring.read(11, r));  // overwritten
    CHECK(ring.read(12, r) && r.key == 0xB000000C);
    CHECK(ring.read(19, r) && r.key == 0xB0000013);
    CHECK(s3::Ring<8>::capacity() == 8 && s3::Ring<8>::bytes() >= 8 * sizeof(s3::Record));
}

void test_ring_concurrent() {
    // Four writers, one reader: a record read back is never torn (its fields agree).
    static s3::Ring<1024> ring;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> torn{0}, seen{0};
    std::thread reader([&] {
        uint64_t cursor = 0;
        while (!stop.load()) {
            const uint64_t head = ring.head();
            if (cursor < ring.oldest()) cursor = ring.oldest();
            for (; cursor < head; ++cursor) {
                s3::Record r;
                if (!ring.read(cursor, r)) continue;
                seen.fetch_add(1);
                if (r.a != (r.key ^ 0x5A5A5A5Au) || r.v[9] != r.key || r.b != r.tid) torn.fetch_add(1);
            }
        }
    });
    std::vector<std::thread> writers;
    for (uint32_t t = 0; t < 4; ++t) {
        writers.emplace_back([t] {
            for (uint32_t i = 0; i < 50000; ++i) {
                s3::Record w;
                w.key = (t << 24) | i;
                w.a = w.key ^ 0x5A5A5A5Au;
                w.tid = t;
                w.b = t;
                w.v[9] = w.key;
                ring.push(w);
            }
        });
    }
    for (auto& w : writers) w.join();
    stop = true;
    reader.join();
    CHECK(ring.head() == 200000);
    CHECK(torn.load() == 0);
    CHECK(seen.load() > 0);
    // After the writers: the last 1024 are all readable.
    size_t ok = 0;
    for (uint64_t i = ring.oldest(); i < ring.head(); ++i) {
        s3::Record r;
        if (ring.read(i, r)) ++ok;
    }
    CHECK(ok == 1024);
}

void test_sets() {
    // Bounded (I4): at most N/2 keys; reaching that load closes the set for good.
    s3::KeySet<4> ks;
    CHECK(ks.insert(0) == s3::KeySet<4>::Insert::Present);
    CHECK(ks.insert(0xB01002A0) == s3::KeySet<4>::Insert::Added);
    CHECK(ks.insert(0xB01002A0) == s3::KeySet<4>::Insert::Present);
    CHECK(ks.contains(0xB01002A0) && !ks.contains(0xB01002A1));
    CHECK(ks.insert(0xB0100001) == s3::KeySet<4>::Insert::Added);
    CHECK(!ks.closed());
    CHECK(ks.insert(0xB0100002) == s3::KeySet<4>::Insert::Full && ks.closed());
    CHECK(ks.insert(0xB0100003) == s3::KeySet<4>::Insert::Full);
    CHECK(ks.insert(0xB01002A0) == s3::KeySet<4>::Insert::Present);  // known keys are still found
    CHECK(ks.size() == 2 && ks.full() == 2);

    s3::GuardTable<4> gt;
    bool first = false;
    auto* s = gt.touch(0xB01002A0, kHostKey, 100, first);
    CHECK(s && first && gt.size() == 1 && gt.open() == 1);
    s = gt.touch(0xB01002A0, kHostKey, 200, first);
    CHECK(s && !first && s->skips.load() == 2 && s->first_qpc.load() == 100 && s->last_qpc.load() == 200);
    CHECK(gt.find(0xB01002A0) == s && !gt.find(0xB01002A1));
    CHECK(gt.recover(*s, 300) && !gt.recover(*s, 400) && gt.open() == 0 && s->recovered_qpc.load() == 300);
    CHECK(gt.touch(0xB0100001, kHostKey, 1, first) && first);
    CHECK(!gt.touch(0xB0109999, kHostKey, 1, first) && gt.full() == 1 && gt.closed());
    CHECK(gt.touch(0xB0100001, kHostKey, 2, first) && !first);  // a guarded key still counts its skips
    CHECK(!gt.touch(0, kHostKey, 1, first));
}

// I4: the production sizes, filled. Every insert after the table closed is a
// bounded lookup (never a scan of all the slots), present keys are still found.
void test_full_sets() {
    static s3::KeySet<16384> big;
    size_t added = 0, full = 0;
    for (uint32_t k = 1; k <= 20000; ++k) {
        const auto r = big.insert(0xB0000000u + k * 7919u);
        added += r == s3::KeySet<16384>::Insert::Added;
        full += r == s3::KeySet<16384>::Insert::Full;
    }
    CHECK(big.closed() && big.size() <= s3::KeySet<16384>::kMaxKeys && big.size() + 64 >= s3::KeySet<16384>::kMaxKeys);
    CHECK(added == big.size() && added + full == 20000);
    CHECK(big.insert(0xB0000000u + 7919u) == s3::KeySet<16384>::Insert::Present);  // the first key is still there
    CHECK(big.contains(0xB0000000u + 7919u * 2));
    // Concurrent inserts into a closing set: never more than N/2 + the writers.
    static s3::KeySet<1024> shared;
    std::vector<std::thread> threads;
    for (uint32_t t = 0; t < 4; ++t) {
        threads.emplace_back([t] {
            for (uint32_t k = 1; k <= 2000; ++k) shared.insert(0xB0000000u + t * 100000u + k);
        });
    }
    for (auto& th : threads) th.join();
    CHECK(shared.closed() && shared.size() <= s3::KeySet<1024>::kMaxKeys + 4);
    static s3::GuardTable<2048> guards;
    bool first = false;
    size_t taken = 0;
    for (uint32_t k = 1; k <= 3000; ++k) taken += guards.touch(0xB0000000u + k, kHostKey, k, first) && first;
    CHECK(guards.closed() && taken == guards.size() && taken <= s3::GuardTable<2048>::kMaxKeys);
}

// M2: one capture at a time, never twice for one address, at most N.
void test_crash_gate() {
    using G = s3::CrashGate<2>;
    G g;
    CHECK(g.enter(0x232D5FF) == G::Enter::Go);
    CHECK(g.enter(0x1234) == G::Enter::Busy);  // a concurrent crash goes on at once
    g.leave();
    CHECK(g.enter(0x232D5FF) == G::Enter::Repeat);  // the vectored handler did it; the filter does not
    CHECK(g.enter(0x1234) == G::Enter::Go);
    g.leave();
    CHECK(g.enter(0x5678) == G::Enter::Spent && g.used() == 2);
    // Many threads at once: exactly one enters while it is busy.
    static G many;
    std::atomic<int> go{0}, busy{0};
    std::vector<std::thread> threads;
    std::atomic<bool> start{false};
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&, t] {
            while (!start.load()) {
            }
            const auto r = many.enter(0x1000 + static_cast<uintptr_t>(t));
            if (r == G::Enter::Go) {
                go.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            } else if (r == G::Enter::Busy) {
                busy.fetch_add(1);
            }
        });
    }
    start = true;
    for (auto& th : threads) th.join();
    CHECK(go.load() == 1 && busy.load() == 7);
    // busy(): what the end of the process waits for.
    G one;
    CHECK(!one.busy());
    CHECK(one.enter(0x10) == G::Enter::Go && one.busy());
    CHECK(one.enter(0x10) == G::Enter::Busy && one.busy());
    one.leave();
    CHECK(!one.busy());
    CHECK(one.enter(0x10) == G::Enter::Repeat && !one.busy());  // Repeat/Spent leave it free
}

// After the capture (p8-salida-invitado.md §3): only an access violation, only
// with guest_crash_dump=1 and without crashpad_handler.exe, ends the process.
void test_uef_end() {
    using s3::UefEnd;
    CHECK(s3::uef_end(0xC0000005u, true, false) == UefEnd::Terminate);   // session 6: exe+0x2045A6A
    CHECK(s3::uef_end(0xC0000005u, true, true) == UefEnd::Chain);        // crashpad there: as before
    CHECK(s3::uef_end(0xC0000005u, false, false) == UefEnd::Chain);      // crash capture off
    CHECK(s3::uef_end(0xE06D7363u, true, false) == UefEnd::Chain);       // a C++ exception: the CRT's filter
    CHECK(s3::uef_end(0xC00000FDu, true, false) == UefEnd::Chain);       // stack overflow
    CHECK(s3::uef_end(0x80000003u, true, false) == UefEnd::Chain);       // breakpoint
    CHECK(s3::uef_end(0xC0000409u, true, false) == UefEnd::Chain);       // fail-fast
}

// The client's goodbye 2001 (guest_goodbye_logic.h), on the timelines of the
// live sessions.
void test_goodbye() {
    using goodbye::Action;
    using goodbye::State;
    const auto d = [](uint64_t now, uint64_t at, uint64_t last_in, bool answered, bool broken, bool field) {
        return goodbye::decide(State{now, at, last_in, answered, broken, field});
    };
    // Nothing pending, or already answered (by the host or by us): nothing.
    CHECK(d(100000, 0, 0, false, true, true) == Action::Wait);
    CHECK(d(100000, 1000, 0, true, true, true) == Action::Wait);
    CHECK(d(100000, 1000, 0, true, true, false) == Action::Wait);

    // The title (no 3093 to the host; session 1, 14:46-14:47): as before the
    // change - at once when broken, after 5 s when silent.
    CHECK(d(10000, 10000, 0, false, true, false) == Action::AnswerLocally);
    CHECK(d(14999, 10000, 0, false, false, false) == Action::Wait);
    CHECK(d(15000, 10000, 0, false, false, false) == Action::AnswerLocally);
    CHECK(d(15000, 10000, 14900, false, false, false) == Action::AnswerLocally);  // packets do not matter there

    // Session 6B (field loaded, link broken 13 s before the 2001): never the
    // local answer; the process ends at once.
    CHECK(d(57518, 57518, 44333, false, true, true) == Action::EndProcess);
    CHECK(d(57534, 57518, 44333, false, true, true) == Action::EndProcess);
    // Session 5 (field half loaded, state 0x11): the same.
    CHECK(d(10658, 10658, 0, false, true, true) == Action::EndProcess);

    // Session 6A (field, host alive): its unload stream (>5285 Acks) arrives
    // within 0.41 s, then the real 2003/2002: never ended, never answered.
    CHECK(d(13531 + 100, 13531, 13531 + 90, false, false, true) == Action::Wait);
    CHECK(d(13531 + 300, 13531, 13531 + 290, false, false, true) == Action::Wait);
    CHECK(d(13531 + 407, 13531, 13531 + 407, true, false, true) == Action::Wait);

    // Field, host alive but slow: still unloading after 5 s -> wait; silent 5 s -> end.
    CHECK(d(20000, 10000, 19000, false, false, true) == Action::Wait);
    CHECK(d(24000, 10000, 19000, false, false, true) == Action::EndProcess);
    CHECK(d(14999, 10000, 0, false, false, true) == Action::Wait);
    CHECK(d(15000, 10000, 0, false, false, true) == Action::EndProcess);  // nothing at all after the goodbye
    CHECK(d(15000, 10000, 9000, false, false, true) == Action::EndProcess);  // only before it
    // Field, packets keep coming but no 2002: the 30 s cap.
    CHECK(d(39999, 10000, 39990, false, false, true) == Action::Wait);
    CHECK(d(40000, 10000, 39990, false, false, true) == Action::EndProcess);
    // A packet stamped after the clock was read (another thread): never "silent" by wrap-around.
    CHECK(d(16000, 10000, 16005, false, false, true) == Action::Wait);
    CHECK(d(16000, 10000, 0, false, false, true) == Action::EndProcess);

    // The reason in the line.
    CHECK(std::string(goodbye::end_reason(State{57518, 57518, 44333, false, true, true})).find("broken") !=
          std::string::npos);
    CHECK(std::string(goodbye::end_reason(State{24000, 10000, 19000, false, false, true})).find("5 s") !=
          std::string::npos);
    CHECK(std::string(goodbye::end_reason(State{40000, 10000, 39990, false, false, true})).find("30 s") !=
          std::string::npos);
}

void test_guard_predicate() {
    // The local key unknown: nothing is remote, nothing guarded.
    CHECK(!s3::remote_controlled(kHostKey, 0));
    CHECK(s3::remote_controlled(kHostKey, kUser2Key));
    CHECK(!s3::remote_controlled(kUser2Key, kUser2Key));
    CHECK(s3::remote_controlled(0, kUser2Key));  // the server's own actors count as remote
    CHECK(s3::guard_skips(true, true, kHostKey, kUser2Key));
    CHECK(!s3::guard_skips(false, true, kHostKey, kUser2Key));    // guard off
    CHECK(!s3::guard_skips(true, false, kHostKey, kUser2Key));    // it has an action
    CHECK(!s3::guard_skips(true, true, kUser2Key, kUser2Key));    // controlled here: never touched
    CHECK(!s3::guard_skips(true, true, kHostKey, 0));             // local key unknown
    CHECK(s3::start_caller(0x48B2E0) == s3::StartCaller::Spawn2723);
    CHECK(s3::start_caller(0x48AEA2) == s3::StartCaller::Ack2991);
    CHECK(s3::start_caller(0x864B00) == s3::StartCaller::Other);
    CHECK(std::strcmp(s3::ev_name(s3::Ev::NullAction), "NULL ACTION at 0x232D5C0") == 0);
}

// ---------------------------------------------------------------- guest: 2005

std::vector<uint8_t> load(const char* name) {
    const std::string path = std::string(CDMP_SOURCE_DIR) + "/tests/data/" + name;
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void test_2005_fixtures() {
    // #12334: Crow, restore 0 at +0x123B (p8-raiz-invitado.md §5.2).
    const std::vector<uint8_t> crow = load("p8s4_2005_12334.bin");
    CHECK(crow.size() == 6637);
    std::array<s3::Rec2005, 16> out{};
    size_t n = s3::parse_2005(crow, out);
    CHECK(n == 1);
    CHECK(out[0].type == 6 && out[0].key == 0xB01002A0 && out[0].owner == 0 && out[0].controller == 0x90100000);
    CHECK(out[0].restore == 0 && out[0].restore_at == 0x123B && out[0].offset == 2 && out[0].size == 6635);
    // #1191: four NPCs of the host's field, restore 1 (S1's B0100078 among them).
    const std::vector<uint8_t> four = load("p8s4_2005_1191.bin");
    CHECK(four.size() == 28532);
    n = s3::parse_2005(four, out);
    CHECK(n == 4);
    const uint32_t keys[] = {0xB0100078, 0xB0100079, 0xB010007A, 0xB010007B};
    const uint32_t at[] = {4808, 4804, 4828, 4824};
    for (size_t i = 0; i < 4 && i < n; ++i) {
        CHECK(out[i].type == 3 && out[i].key == keys[i] && out[i].controller == 0x90100000 && out[i].restore == 1 &&
              out[i].restore_at == at[i]);
    }
    // The output bound.
    std::array<s3::Rec2005, 2> two{};
    CHECK(s3::parse_2005(four, two) == 2 && two[1].key == 0xB0100079);
    // A truncated payload stops at the first record that does not fit.
    CHECK(s3::parse_2005(std::span<const uint8_t>(four.data(), 7134 + 2 + 100), out) == 1);
}

void test_2005_synthetic() {
    // [u16 9][user record] [u16 n][record without a CC block] [u16 0]
    std::vector<uint8_t> p;
    auto u16 = [&](uint16_t v) { p.push_back(static_cast<uint8_t>(v)); p.push_back(static_cast<uint8_t>(v >> 8)); };
    u16(9);
    const uint8_t user[9] = {0x00, 0x2B, 0x05, 0x10, 0x90, 0, 0, 0, 0};
    p.insert(p.end(), user, user + 9);
    std::vector<uint8_t> gim(0x40, 0);
    gim[0] = 7;
    put(gim.data(), 1, uint32_t{0xB0100151});
    put(gim.data(), 0x12, uint32_t{0x90100000});
    u16(static_cast<uint16_t>(gim.size()));
    p.insert(p.end(), gim.begin(), gim.end());
    u16(0);
    std::array<s3::Rec2005, 8> out{};
    CHECK(s3::parse_2005(p, out) == 2);
    CHECK(out[0].type == 0 && out[0].key == 0x9010052B && out[0].restore == -1);
    CHECK(out[1].type == 7 && out[1].key == 0xB0100151 && out[1].restore == -1 && out[1].controller == 0x90100000);
    // A path whose length prefix does not match: not taken as the restore byte.
    std::vector<uint8_t> rec(0x80, 0);
    const char path[] = "a/b.app_xml";
    std::memcpy(rec.data() + 0x24, path, sizeof path - 1);
    put(rec.data(), 0x20, uint32_t{99});
    CHECK(s3::restore_byte_of(rec) == -1);
    put(rec.data(), 0x20, uint32_t{sizeof path - 1});
    rec[0x24 + sizeof path - 1 + 8] = 1;
    uint32_t pos = 0;
    CHECK(s3::restore_byte_of(rec, &pos) == 1 && pos == 0x24 + sizeof path - 1 + 8);
    rec[pos] = 7;  // not a flag
    CHECK(s3::restore_byte_of(rec) == -1);
    CHECK(s3::restore_byte_of(std::span<const uint8_t>(rec.data(), 10)) == -1);
}

}  // namespace

int main() {
    test_npc_predicate();
    test_npc_split();
    test_npc_table();
    test_npc_release();
    test_npc_withheld_from_host();
    test_npc_read_release();
    test_start_action_key();
    test_hold_targeted_release();
    test_hold_periodic_and_timeout();
    test_hold_reannounce_refresh();
    test_hold_drop_to_native();
    test_hold_fallback_defers();
    test_hold_room_reserved();
    test_hold_user2_leaves();
    test_hold_acks_with_observer_hold();
    test_hold_load_defer();
    test_piece_logic();
    test_hold_pieces();
    test_hold_pieces_acks();
    test_npc_local_retirement();
    test_npc_retirement_bounds();
    test_npc_retirement_publication_race();
    test_family_logic();
    test_family_cc_child();
    test_family_parent_later();
    test_family_load_defer();
    test_family_multilevel();
    test_family_orphan_timeout();
    test_family_release_gate();
    test_family_reverse();
    test_family_valve_and_source();
    test_family_ac_source();
    test_family_field_change_and_gate();
    test_family_off_and_probe();
    test_pub_gate_timeout();
    test_pub_gate_native();
    test_pub_gate_cc_states();
    test_pub_gate_full_table();
    test_pub_gate_family();
    test_pub_gate_review();
    test_ring_bounds();
    test_ring_concurrent();
    test_sets();
    test_full_sets();
    test_crash_gate();
    test_uef_end();
    test_goodbye();
    test_guard_predicate();
    test_2005_fixtures();
    test_2005_synthetic();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("optd_s3 tests passed\n");
    return 0;
}
