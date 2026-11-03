// Option D, C4 second part (reconnect): src/mod/session2/guest_lifecycle.cpp.
// The guest's state machine and epochs, the barrier over fake readings, the
// epoch tag of the packet buffers, and the read-only probes (SAM lookup by
// key, user list, field, user 2's keys) on fake game memory. The game side
// (server.cpp, session2.cpp) needs the game.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "mod/game/safe_memory.h"
#include "mod/session2/field_share.h"
#include "mod/session2/guest_lifecycle.h"

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

struct FakeActor : Blob {
    explicit FakeActor(uint32_t key) : Blob(0x100) { put(p(), c4::kActorKey, key); }
};

// A ServerActorManager with the map of 0x2A827B0 and the user list of 0x10530180.
struct FakeSam : Blob {
    static constexpr uint32_t kBuckets = 4;
    Blob buckets{kBuckets * c4::kSamBucketBytes};
    std::vector<uintptr_t> holders = std::vector<uintptr_t>(64, 0);
    std::vector<std::unique_ptr<Blob>> holder_blobs;
    std::vector<uintptr_t> users;
    uint32_t size = 0;
    FakeSam() : Blob(0x100) {
        put(p(), c4::kSamMapBuckets, kBuckets);
        put(p(), c4::kSamMapBucketBase, buckets.a());
        put(p(), c4::kSamMapHolders, addr(holders.data()));
        sync_users();
    }
    void add(FakeActor& actor, uint32_t key_for_index) {
        const uint32_t idx = key_for_index & c4::kSamKeyIndexMask;
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
    // What the free path does: the bucket entry goes away (here: its index is spoiled).
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
        put(p(), c4::kSamMapSize, --size);
    }
    void sync_users() {
        put(p(), c4::kSamUserList, users.empty() ? uintptr_t{0x10000} : addr(users.data()));
        put(p(), c4::kSamUserCount, static_cast<uint32_t>(users.size()));
    }
};

// A ServerUserActor with its main character and child slots (vf10's walk).
struct FakeUser : FakeActor {
    Blob comps{0x40}, slots{0x40}, arr{0x10 * 8};
    FakeUser(uint32_t key, FakeActor* main, std::vector<FakeActor*> children) : FakeActor(key) {
        if (main) put(p(), c4::kUserMainCharacter, main->a());
        put(p(), c4::kUserComponents, comps.a());
        put(comps.p(), c4::kComponentsSlots, slots.a());
        put(slots.p(), c4::kSlotsArray, arr.a());
        put(slots.p(), c4::kSlotsCount, static_cast<int8_t>(children.size()));
        for (size_t i = 0; i < children.size(); ++i) {
            put(arr.p(), static_cast<uint32_t>(i * c4::kSlotStride), children[i] ? children[i]->a() : uintptr_t{0});
        }
    }
};

struct FakeField : Blob {
    std::vector<FieldUserEntry> entries;
    FakeField() : Blob(0x300) {}
    void set(uint32_t owner, uint32_t user_actors, uint32_t gen, uint8_t open) {
        put(p(), fld::kOwner, owner);
        put(p(), fld::kUserActors, user_actors);
        put(p(), fld::kKey, gen);
        put(p(), fld::kOpen, open);
        sync();
    }
    void sync() {
        put(p(), fld::kUsers, entries.empty() ? uintptr_t{0x10000} : addr(entries.data()));
        put(p(), fld::kUserCount, static_cast<uint32_t>(entries.size()));
    }
};

// ------------------------------------------------------------------ probes

void test_sam_lookup() {
    FakeSam sam;
    FakeActor a(0xA01002EC), b(0x901002EB);
    sam.add(a, 0xA01002EC);
    sam.add(b, 0x901002EB);
    CHECK(sam_key_resolves(sam.a(), 0xA01002EC) == std::optional<bool>(true));
    CHECK(sam_key_resolves(sam.a(), 0x901002EB) == std::optional<bool>(true));
    CHECK(sam_key_resolves(sam.a(), 0x90100777) == std::optional<bool>(false));  // no such index
    CHECK(sam_key_resolves(sam.a(), 0) == std::optional<bool>(false));
    CHECK(!sam_key_resolves(0, 0xA01002EC).has_value());
    // The index reused by another actor (another generation): the old key no longer resolves.
    FakeActor reused(0xA02002EC);
    put(a.p(), c4::kActorKey, 0u);  // the free path zeroes it (0x1053024F)
    sam.remove(0xA01002EC);
    sam.add(reused, 0xA02002EC);
    CHECK(sam_key_resolves(sam.a(), 0xA01002EC) == std::optional<bool>(false));
    CHECK(sam_key_resolves(sam.a(), 0xA02002EC) == std::optional<bool>(true));
    // A torn bucket (more entries than fit) is unreadable, not "absent".
    put(sam.buckets.p() + (0x2EC % FakeSam::kBuckets) * c4::kSamBucketBytes, 0, 99u);
    CHECK(!sam_key_resolves(sam.a(), 0xA02002EC).has_value());
    // An empty map finds nothing.
    FakeSam empty;
    CHECK(sam_key_resolves(empty.a(), 0xA01002EC) == std::optional<bool>(false));
}

void test_sam_user_list() {
    FakeSam sam;
    FakeActor host(0x90100000), guest(0x901002EB);
    sam.users = {host.a(), guest.a()};
    sam.sync_users();
    CHECK(sam_user_listed(sam.a(), 0x901002EB) == std::optional<bool>(true));
    sam.users = {host.a()};
    sam.sync_users();
    CHECK(sam_user_listed(sam.a(), 0x901002EB) == std::optional<bool>(false));
    put(sam.p(), c4::kSamUserCount, 100000u);
    CHECK(!sam_user_listed(sam.a(), 0x901002EB).has_value());
}

void test_user_keys_and_capture() {
    FakeSam sam;
    FakeActor main(0xA01002EC), child1(0xA01002ED), child2(0xA01002EE), stray(0xA0100999);
    FakeUser user(0x901002EB, &main, {&child1, nullptr, &child2, &main, &stray});
    const auto keys = user_actor_keys(user.a());
    CHECK(keys.size() == 5);  // user, main, child1, child2, stray (main once)
    CHECK(keys.size() >= 1 && keys[0] == 0x901002EB);
    sam.add(user, 0x901002EB);
    sam.add(main, 0xA01002EC);
    sam.add(child1, 0xA01002ED);
    sam.add(child2, 0xA01002EE);  // stray is not in the SAM: never captured
    sam.users = {user.a()};
    sam.sync_users();
    DepartedSnap snap;
    capture_departed(sam.a(), user.a(), snap);
    CHECK(snap.user == user.a() && snap.user_key == 0x901002EB);
    CHECK(snap.keys.size() == 4);
    CHECK(snap.lookup_verified && snap.user_listed);
    capture_departed(sam.a(), user.a(), snap);  // again: no duplicates
    CHECK(snap.keys.size() == 4);
    // A user whose key does not resolve: nothing verified.
    FakeSam other;
    DepartedSnap snap2;
    capture_departed(other.a(), user.a(), snap2);
    CHECK(snap2.any() && !snap2.lookup_verified && snap2.keys.empty());
}

void test_field() {
    FakeField f;
    FakeActor host(0x90100000), guest(0x901002EB);
    f.entries = {{host.a(), 2, 0}, {guest.a(), 2, 0}};
    f.set(0x90100000, 4, 0x81020001, 1);
    FieldSnap s;
    CHECK(read_field(f.a(), s));
    CHECK(s.field == f.a() && s.owner == 0x90100000 && s.users == 2 && s.user_actors == 4 && s.generation == 0x81020001 &&
          s.open == 1);
    CHECK(field_lists_user(f.a(), guest.a()) == std::optional<bool>(true));
    f.entries = {{host.a(), 2, 0}};
    f.set(0x90100000, 2, 0x81020001, 1);
    CHECK(field_lists_user(f.a(), guest.a()) == std::optional<bool>(false));
    FieldSnap bad;
    CHECK(!read_field(0, bad));
    // P8 S29: the list read once for the structural check c.
    f.entries = {{host.a(), 2, 0}, {guest.a(), 3, 0}};
    f.set(0x90100000, 5, 0x81020001, 1);
    auto scan = scan_field_list(f.a(), guest.a(), host.a());
    CHECK(scan && scan->entries == 2 && scan->actors == 5 && scan->departed_listed && scan->foreign == 1 &&
          scan->first_foreign == guest.a());
    f.entries = {{host.a(), 2, 0}};
    f.set(0x90100000, 2, 0x81020001, 1);
    scan = scan_field_list(f.a(), guest.a(), host.a());
    CHECK(scan && scan->entries == 1 && scan->actors == 2 && !scan->departed_listed && scan->foreign == 0);
    f.entries = {{0, 0, 0}};  // a null entry counts as foreign
    f.sync();
    scan = scan_field_list(f.a(), guest.a(), host.a());
    CHECK(scan && scan->foreign == 1 && scan->first_foreign == 0);
    f.entries.clear();
    f.sync();
    scan = scan_field_list(f.a(), guest.a(), host.a());
    CHECK(scan && scan->entries == 0 && scan->foreign == 0);
    CHECK(!scan_field_list(0, guest.a(), host.a()).has_value());
}

// ------------------------------------------------------------------ the barrier

FieldSnap baseline() {
    FieldSnap b;
    b.ok = true;
    b.field = 0x206C6D17A10;
    b.host_key = 0x90100000;
    b.owner = 0x90100000;
    b.users = 1;
    b.user_actors = 2;  // C4b's baseline with the host alone: NOT 1
    b.generation = 0x81020001;
    b.open = 1;
    return b;
}

DepartedSnap departed() {
    DepartedSnap s;
    s.user = 0x2074BB58980;
    s.user_key = 0x901002EB;
    s.keys = {0x901002EB, 0xA01002EC};
    s.lookup_verified = true;
    s.user_listed = true;
    return s;
}

BarrierReadings clean_readings() {
    BarrierReadings r;
    r.session_read = true;
    r.sam_read = true;
    r.keys_checked = 2;
    r.field = baseline();
    r.field_list_read = true;
    r.listed_actors = r.field.user_actors;  // the list agrees with the total
    r.list_entries = r.field.users;         // the host's user alone (P8 S29 structural check c)
    r.foreign_entries = 0;
    r.pb_pushed = r.pb_seen = 11;
    r.logout_handed = 1;
    return r;
}

void test_barrier() {
    const auto b = baseline();
    const auto s = departed();
    auto r = clean_readings();
    CHECK(evaluate_barrier(b, s, r).all());
    CHECK(evaluate_barrier(b, s, r).first_failed().empty());

    r = clean_readings();
    r.session_user = 0x2074BB58980;  // a: still attached
    CHECK(!evaluate_barrier(b, s, r).session.pass && evaluate_barrier(b, s, r).first_failed() == "a session");

    r = clean_readings();
    r.keys_resolving = 1;  // b: a character key still resolves
    r.first_resolving = 0xA01002EC;
    CHECK(!evaluate_barrier(b, s, r).sam.pass && evaluate_barrier(b, s, r).first_failed() == "b sam");
    r = clean_readings();
    r.user_listed = true;
    CHECK(!evaluate_barrier(b, s, r).sam.pass);
    r = clean_readings();
    r.sam_read = false;
    CHECK(!evaluate_barrier(b, s, r).sam.pass);
    auto unverified = s;
    unverified.lookup_verified = false;
    CHECK(!evaluate_barrier(b, unverified, clean_readings()).sam.pass);
    CHECK(!evaluate_barrier(b, DepartedSnap{}, clean_readings()).sam.pass);

    // c: the user-actor total may differ from the baseline (the host called its
    // horse: 2 -> 3, live 2026-10-09) as long as the list's counts agree with it.
    r = clean_readings();
    r.field.user_actors = 3;
    r.listed_actors = 3;
    CHECK(evaluate_barrier(b, s, r).field.pass);
    r = clean_readings();
    r.field.user_actors = 3;
    r.listed_actors = 2;  // a count the list does not explain
    CHECK(!evaluate_barrier(b, s, r).field.pass);
    r = clean_readings();
    r.listed_actors = std::nullopt;  // list unreadable
    CHECK(!evaluate_barrier(b, s, r).field.pass);
    r = clean_readings();
    r.field.users = 2;  // the field counts a user the list read does not show
    CHECK(!evaluate_barrier(b, s, r).field.pass);
    r = clean_readings();
    r.list_entries = std::nullopt;
    CHECK(!evaluate_barrier(b, s, r).field.pass);
    // P8 S29 (reentrada.md §6): the baseline's generation and open are information only.
    r = clean_readings();
    r.field.generation = 0x81020002;
    CHECK(evaluate_barrier(b, s, r).field.pass);
    CHECK(evaluate_barrier(b, s, r).field.detail.find("(CHANGED)") != std::string::npos);
    r = clean_readings();
    r.field.open = 0;
    CHECK(evaluate_barrier(b, s, r).field.pass);
    // The host changed field since the baseline: structural check, passes.
    r = clean_readings();
    r.field.field = 0x1234560;
    r.field.generation = 0x81030001;
    CHECK(evaluate_barrier(b, s, r).field.pass);
    CHECK(evaluate_barrier(b, s, r).field.detail.find("changed field since the baseline") != std::string::npos);
    // The host does not own the field it is in.
    r = clean_readings();
    r.field.owner = 0x901002EB;
    CHECK(!evaluate_barrier(b, s, r).field.pass);
    r = clean_readings();
    r.field.host_key = 0;
    r.field.owner = 0;
    CHECK(!evaluate_barrier(b, s, r).field.pass);
    // The departed user still listed.
    r = clean_readings();
    r.departed_in_field = true;
    CHECK(!evaluate_barrier(b, s, r).field.pass);
    r = clean_readings();
    r.field_list_read = false;
    CHECK(!evaluate_barrier(b, s, r).field.pass);
    // A foreign entry (anyone but the host's user), even with the counts agreeing.
    r = clean_readings();
    r.field.users = 2;
    r.list_entries = 2;
    r.foreign_entries = 1;
    r.first_foreign = 0x2074BB59000;
    CHECK(!evaluate_barrier(b, s, r).field.pass);
    CHECK(evaluate_barrier(b, s, r).field.detail.find("not the host's user: 1 (first 0x2074BB59000)") !=
          std::string::npos);
    r = clean_readings();
    r.foreign_entries = std::nullopt;  // the host's user or the list unreadable
    CHECK(!evaluate_barrier(b, s, r).field.pass);
    // An EMPTY list (a transient field mid-travel) proves nothing about user 2 (review of 967c6ca, A-M2).
    r = clean_readings();
    r.field.users = 0;
    r.field.user_actors = 0;
    r.list_entries = 0;
    r.listed_actors = 0;
    CHECK(!evaluate_barrier(b, s, r).field.pass);
    CHECK(!evaluate_barrier(FieldSnap{}, s, r).field.pass);
    r = clean_readings();
    r.field.ok = false;
    CHECK(!evaluate_barrier(b, s, r).field.pass);
    // No baseline (S29 step 2: user 2 logged in while the host was at its title screen) and a clean field: passes.
    CHECK(evaluate_barrier(FieldSnap{}, s, clean_readings()).field.pass);
    CHECK(evaluate_barrier(FieldSnap{}, s, clean_readings()).all());
    CHECK(evaluate_barrier(FieldSnap{}, s, clean_readings()).field.detail.find("no baseline") != std::string::npos);
    // ... but not one with the sum of the per-user counts off.
    r = clean_readings();
    r.field.user_actors = 3;  // 2 listed
    CHECK(!evaluate_barrier(FieldSnap{}, s, r).field.pass);

    // d
    r = clean_readings();
    r.pb_pushed = 12;  // one buffer of the old epoch not seen by the server yet
    CHECK(!evaluate_barrier(b, s, r).queues.pass && evaluate_barrier(b, s, r).first_failed() == "d queues");
    r = clean_readings();
    r.logout_handed = 2;  // the 2001 twice: never
    CHECK(!evaluate_barrier(b, s, r).queues.pass);
    r = clean_readings();
    r.early = 3;
    CHECK(!evaluate_barrier(b, s, r).queues.pass);
    r = clean_readings();
    r.outgoing_bytes = 7;
    CHECK(!evaluate_barrier(b, s, r).queues.pass);
    r = clean_readings();
    r.mirror_pending = 1;
    CHECK(!evaluate_barrier(b, s, r).queues.pass);
    r = clean_readings();
    r.logout_handed = 0;
    r.expect_handed = false;
    CHECK(evaluate_barrier(b, s, r).queues.pass);

    // The pattern changes with the checks; describe names all four.
    r = clean_readings();
    const unsigned all = evaluate_barrier(b, s, r).pattern();
    r.session_user = 1;
    CHECK(evaluate_barrier(b, s, r).pattern() != all);
    const std::string d = evaluate_barrier(b, s, r).describe();
    CHECK(d.find("a session FAIL") != std::string::npos && d.find("b sam pass") != std::string::npos &&
          d.find("c field pass") != std::string::npos && d.find("d queues pass") != std::string::npos);
}

// ------------------------------------------------------------------ epochs of the packet buffers

void test_pb_tags() {
    CHECK(pb_tag(1) == 0xD0D00001u);
    CHECK(pb_tag_epoch(pb_tag(7)) == std::optional<uint32_t>(7));
    CHECK(!pb_tag_epoch(0xD0D10001u).has_value());
    CHECK(!pb_tag_epoch(0).has_value());
    const uintptr_t sessions[4] = {0, 0x1000, 0x2000, 0x3000};
    // The current epoch's buffer goes to the current session.
    auto r = route_pb(pb_tag(3), 3, sessions, 4);
    CHECK(r.session == 0x3000 && r.epoch == 3 && !r.stale);
    // An old buffer goes to ITS session, never to the new one.
    r = route_pb(pb_tag(1), 3, sessions, 4);
    CHECK(r.session == 0x1000 && r.epoch == 1 && r.stale);
    // Unknown epochs / foreign tags: no session (the caller keeps its old behaviour).
    CHECK(route_pb(pb_tag(9), 3, sessions, 4).session == 0);
    CHECK(route_pb(0x12345678, 3, sessions, 4).session == 0);
    CHECK(route_pb(pb_tag(0), 3, sessions, 4).session == 0);
    CHECK(epoch_of_session(0x2000, sessions, 4) == 2);
    CHECK(epoch_of_session(0x9999, sessions, 4) == 0);
    CHECK(epoch_of_session(0, sessions, 4) == 0);
}

// ------------------------------------------------------------------ the state machine

BarrierResult passing() { return evaluate_barrier(baseline(), departed(), clean_readings()); }
BarrierResult failing_sam() {
    auto r = clean_readings();
    r.keys_resolving = 1;
    r.first_resolving = 0xA01002EC;
    return evaluate_barrier(baseline(), departed(), r);
}

void test_lifecycle_cycle() {
    GuestLifecycle lc;
    CHECK(lc.state() == GuestState::Active && lc.epoch() == 1);
    // The first packet binds the conversation to its connection.
    CHECK(!lc.conversation());
    CHECK(lc.admit(1));
    CHECK(lc.conversation() && lc.bound_link() == std::optional<uint32_t>(1));
    CHECK(lc.admit(1));
    CHECK(!lc.admit(2));  // another connection: not this conversation
    // Goodbye.
    CHECK(lc.begin_logout(LogoutTrigger::Goodbye, 1000));
    CHECK(lc.state() == GuestState::Retiring && lc.trigger() == LogoutTrigger::Goodbye);
    CHECK(!lc.begin_logout(LogoutTrigger::LinkLost, 1001));  // once
    CHECK(!lc.admit(1));  // nothing more of it reaches the server
    lc.note_handed(1000);
    CHECK(lc.handed());
    // Passing once is not enough: twice, at least 500 ms apart.
    CHECK(lc.on_barrier(1100, false, passing()) == GuestLifecycle::Step::Wait);
    CHECK(lc.on_barrier(1300, false, passing()) == GuestLifecycle::Step::Wait);
    CHECK(lc.on_barrier(1400, false, failing_sam()) == GuestLifecycle::Step::Wait);  // resets the streak
    CHECK(lc.on_barrier(1500, false, passing()) == GuestLifecycle::Step::Wait);
    CHECK(lc.on_barrier(2000, false, passing()) == GuestLifecycle::Step::Clean);
    CHECK(lc.state() == GuestState::Clean);
    CHECK(!lc.start_epoch(5));  // not READY yet
    // READY on connection 1 (the old guest may still be connected).
    CHECK(lc.make_ready(1));
    CHECK(lc.state() == GuestState::Ready && lc.epoch() == 1);
    CHECK(!lc.admit(1) && !lc.admit(2));  // nothing reaches the server while READY
    CHECK(!lc.may_start(1));             // the old connection never starts an epoch
    CHECK(!lc.start_epoch(1));
    CHECK(lc.may_start(2));
    CHECK(lc.start_epoch(2));
    CHECK(lc.state() == GuestState::Active && lc.epoch() == 2 && !lc.conversation());
    CHECK(lc.min_link() == 2);
    CHECK(!lc.admit(1));  // a packet of the old connection: never
    CHECK(lc.admit(3));   // a newer connection may bind (the new guest reconnected before speaking)
    CHECK(!lc.admit(2));
    // Link lost this time; the barrier never passes -> quarantine after the budget, naming the check.
    CHECK(lc.begin_logout(LogoutTrigger::LinkLost, 10000));
    lc.note_handed(10000);
    CHECK(lc.on_barrier(15000, false, failing_sam()) == GuestLifecycle::Step::Wait);
    CHECK(lc.on_barrier(20000, false, failing_sam()) == GuestLifecycle::Step::Quarantined);
    CHECK(lc.state() == GuestState::Quarantined);
    CHECK(lc.quarantine_reason().find("b sam") == 0);
    // Quarantine is final: no login, no new epoch.
    CHECK(!lc.admit(3) && !lc.admit(4));
    CHECK(!lc.begin_logout(LogoutTrigger::Goodbye, 21000));
    CHECK(!lc.may_start(9) && !lc.start_epoch(9));
    CHECK(!lc.make_ready(9));
    lc.quarantine("another reason");
    CHECK(lc.quarantine_reason().find("b sam") == 0);
}

// P8 re-entry (rejoin_logic.h, s25/reentrada.md §3): READY -> ACTIVE on the SAME connection, asked for by the
// guest's REJOIN; then the usual cycle again, and a newer connection still works as before.
void test_lifecycle_inband() {
    GuestLifecycle lc;
    CHECK(lc.admit(4));  // the guest's first conversation on connection 4
    CHECK(!lc.may_start_inband(4) && !lc.start_epoch_inband(4));  // only from READY
    CHECK(lc.begin_logout(LogoutTrigger::Goodbye, 1000));
    lc.note_handed(1000);
    CHECK(lc.on_barrier(1100, false, passing()) == GuestLifecycle::Step::Wait);
    CHECK(!lc.start_epoch_inband(4));  // RETIRING: never
    CHECK(lc.on_barrier(1700, false, passing()) == GuestLifecycle::Step::Clean);
    CHECK(!lc.start_epoch_inband(4));  // CLEAN: never (make_ready first)
    CHECK(lc.make_ready(4));
    CHECK(!lc.may_start(4));           // a transport Hello would need connection 5
    CHECK(!lc.may_start_inband(3));    // never an older connection
    CHECK(!lc.admit(4));               // nothing is admitted while READY (what the guest sent before its REJOIN)
    CHECK(lc.may_start_inband(4));
    CHECK(lc.start_epoch_inband(4));
    CHECK(lc.state() == GuestState::Active && lc.epoch() == 2 && !lc.conversation());
    CHECK(lc.min_link() == 4);
    CHECK(!lc.admit(3));  // an older connection: never
    CHECK(lc.admit(4));   // the same connection now carries the new conversation
    CHECK(lc.conversation() && lc.bound_link() == std::optional<uint32_t>(4));
    CHECK(!lc.start_epoch_inband(4));  // ACTIVE: a second REJOIN starts nothing
    // The second conversation leaves by link loss; READY again; this time the guest restarted its game (new Hello).
    CHECK(lc.begin_logout(LogoutTrigger::LinkLost, 5000));
    lc.note_handed(5000);
    CHECK(lc.on_barrier(5100, false, passing()) == GuestLifecycle::Step::Wait);
    CHECK(lc.on_barrier(5700, false, passing()) == GuestLifecycle::Step::Clean);
    CHECK(lc.make_ready(4));
    CHECK(lc.may_start(5) && lc.start_epoch(5));
    CHECK(lc.epoch() == 3 && lc.min_link() == 5 && !lc.admit(4) && lc.admit(5));
    // The epoch cap holds for in-band starts too.
    GuestLifecycle small{GuestLifecycle::Config{10000, 60000, 500, 2}};
    CHECK(small.admit(1));
    CHECK(small.begin_logout(LogoutTrigger::Goodbye, 0));
    small.note_handed(0);
    CHECK(small.on_barrier(100, false, passing()) == GuestLifecycle::Step::Wait);
    CHECK(small.on_barrier(700, false, passing()) == GuestLifecycle::Step::Clean);
    CHECK(small.make_ready(1) && small.start_epoch_inband(1) && small.epoch() == 2);
    CHECK(small.admit(1));
    CHECK(small.begin_logout(LogoutTrigger::Goodbye, 1000));
    small.note_handed(1000);
    CHECK(small.on_barrier(1100, false, passing()) == GuestLifecycle::Step::Wait);
    CHECK(small.on_barrier(1700, false, passing()) == GuestLifecycle::Step::Clean);
    CHECK(!small.make_ready(1) && small.state() == GuestState::Quarantined);
    CHECK(!small.start_epoch_inband(1));
}

void test_lifecycle_edges() {
    // Nothing of the epoch reached the server: clean without a 2001.
    {
        GuestLifecycle lc;
        CHECK(lc.admit(1));
        CHECK(lc.begin_logout(LogoutTrigger::LinkLost, 0));
        CHECK(lc.on_barrier(10, true, passing()) == GuestLifecycle::Step::Clean);
    }
    // Packets reached the server but no user 2 ever appeared: wait for the 2001, then quarantine.
    {
        GuestLifecycle lc;
        CHECK(lc.admit(1));
        CHECK(lc.begin_logout(LogoutTrigger::LinkLost, 0));
        CHECK(lc.on_barrier(30000, false, passing()) == GuestLifecycle::Step::Wait);
        CHECK(lc.on_barrier(60000, false, passing()) == GuestLifecycle::Step::Quarantined);
        CHECK(lc.quarantine_reason().find("no 2001") != std::string::npos);
    }
    // ...or the user appears, the 2001 goes and the barrier passes.
    {
        GuestLifecycle lc;
        CHECK(lc.admit(1));
        CHECK(lc.begin_logout(LogoutTrigger::LinkLost, 0));
        CHECK(lc.on_barrier(5000, false, passing()) == GuestLifecycle::Step::Wait);
        lc.note_handed(20000);  // 20 s later: the budget counts from here
        CHECK(lc.on_barrier(29000, false, failing_sam()) == GuestLifecycle::Step::Wait);
        CHECK(lc.on_barrier(29500, false, passing()) == GuestLifecycle::Step::Wait);
        CHECK(lc.on_barrier(30100, false, passing()) == GuestLifecycle::Step::Clean);
    }
    // A clean barrier when the budget runs out is not a quarantine.
    {
        GuestLifecycle lc;
        CHECK(lc.begin_logout(LogoutTrigger::Goodbye, 0));
        lc.note_handed(0);
        CHECK(lc.on_barrier(10000, false, passing()) == GuestLifecycle::Step::Wait);
        CHECK(lc.on_barrier(10600, false, passing()) == GuestLifecycle::Step::Clean);
    }
    // A refused login (buffers reached the server, no user 2 ever, all quiet):
    // clean without a 2001, not a quarantine after 60 s.
    {
        GuestLifecycle lc;
        CHECK(lc.begin_logout(LogoutTrigger::LinkLost, 0));
        CHECK(lc.on_barrier(1000, false, passing()) == GuestLifecycle::Step::Wait);
        CHECK(lc.on_barrier(6000, false, passing(), true) == GuestLifecycle::Step::Clean);
        CHECK(!lc.handed());
    }
    // The epoch cap: each session 2 stays alive, so their number is bounded.
    {
        GuestLifecycle lc(GuestLifecycle::Config{10000, 60000, 500, 2});
        uint32_t link = 1;
        for (int round = 0; round < 2; ++round) {
            CHECK(lc.admit(link));
            CHECK(lc.begin_logout(LogoutTrigger::Goodbye, 0));
            CHECK(lc.on_barrier(0, true, passing()) == GuestLifecycle::Step::Clean);
            if (round == 0) {
                CHECK(lc.make_ready(link));
                CHECK(lc.start_epoch(++link));
            } else {
                CHECK(!lc.make_ready(link));
                CHECK(lc.state() == GuestState::Quarantined);
                CHECK(lc.quarantine_reason().find("epoch cap") == 0);
            }
        }
        CHECK(lc.epoch() == 2);
    }
    // Names for the log.
    CHECK(std::string(guest_state_name(GuestState::Quarantined)) == "QUARANTINED");
    CHECK(std::string(logout_trigger_name(LogoutTrigger::LinkLost)) == "link_lost");
    CHECK(std::string(logout_trigger_name(LogoutTrigger::Goodbye)) == "bye");
}

// salida-en-carga.md §4: user 2's move phase from the Req and Acks of the E3
// login (login_script_112627.json), at the cuts of cut3/cut8/cut9.
void test_move_phase_tracker() {
    MovePhaseTracker t;
    CHECK(t.phase() == MovePhase::None);
    // Not followed: phase and counts unchanged.
    CHECK(!t.on_req(2319) && !t.on_req(2490) && !t.on_ack(2756) && !t.on_ack(2174 + 1));
    CHECK(!t.on_req(mv::kMoveFieldAck));  // an Ack opcode as a Req: ignored
    CHECK(!t.on_ack(mv::kMoveFieldCompleteReq));
    CHECK(t.phase() == MovePhase::None && t.count(mv::kMoveFieldAck) == 0);
    // Steps 1-3: login and the parking MoveFieldAck (cut3: window A).
    CHECK(t.on_req(mv::kLoginCharacterReq));
    CHECK(t.phase() == MovePhase::LoggingIn);
    CHECK(t.on_ack(mv::kMoveFieldAck));
    CHECK(t.phase() == MovePhase::WindowA);
    // Its MoveFieldCompleteReq, Ack, LoadingCompleteReq, Ack.
    CHECK(t.on_req(mv::kMoveFieldCompleteReq));
    CHECK(t.phase() == MovePhase::Completing);
    CHECK(t.on_ack(mv::kMoveFieldCompleteAck));
    CHECK(t.phase() == MovePhase::WindowB);
    CHECK(t.on_req(mv::kLoadingCompleteReq));
    CHECK(t.phase() == MovePhase::LoadingSent);
    CHECK(t.on_ack(mv::kLoadingCompleteAck));
    CHECK(t.phase() == MovePhase::Stable);
    // Step 8: the shared move (cut8: window A again).
    CHECK(t.on_req(mv::kGameLoadingStartReq));
    CHECK(t.phase() == MovePhase::MoveAsked);
    CHECK(t.on_ack(mv::kMoveFieldAck));
    CHECK(t.phase() == MovePhase::WindowA);
    // Step 9 (cut9: window B).
    CHECK(t.on_req(mv::kMoveFieldCompleteReq));
    CHECK(t.on_ack(mv::kMoveFieldCompleteAck));
    CHECK(t.phase() == MovePhase::WindowB);
    CHECK(t.on_req(mv::kLoadingCompleteReq));
    CHECK(t.on_ack(mv::kLoadingCompleteAck));
    CHECK(t.phase() == MovePhase::Stable);
    CHECK(t.count(mv::kMoveFieldAck) == 2 && t.count(mv::kMoveFieldCompleteAck) == 2 &&
          t.count(mv::kLoginCharacterReq) == 1 && t.count(mv::kGameLoadingStartReq) == 1 &&
          t.count(mv::kLoadingCompleteAck) == 2 && t.count(1234) == 0);
    CHECK(t.describe().find("STABLE (3093 1, 2437 1, 2174 2, 2549 2, 2144 2, 2445 2, 2762 2)") == 0);
    // A new epoch starts from nothing.
    t.reset();
    CHECK(t.phase() == MovePhase::None && t.count(mv::kMoveFieldAck) == 0);
    CHECK(std::string(move_phase_name(MovePhase::WindowA)) == "WINDOW_A");
    CHECK(std::string(move_phase_name(MovePhase::LoadingSent)) == "LOADING_SENT");
}

struct MoveEvent {
    bool req;  // a Req of user 2 (else an Ack for it)
    uint16_t op;
};
MovePhaseTracker tracker_at(std::initializer_list<MoveEvent> events) {
    MovePhaseTracker t;
    for (const auto& e : events) {
        if (e.req) {
            t.on_req(e.op);
        } else {
            t.on_ack(e.op);
        }
    }
    return t;
}

void test_move_close_sequence() {
    using A = MoveCloseSequence::Action;
    using S = MoveCloseSequence::Stage;
    constexpr bool R = true, K = false;  // Req / Ack
    std::string note;
    // cut8: window A after a complete parking load (one 2144 already seen).
    {
        MovePhaseTracker t = tracker_at({{R, 3093}, {K, 2174}, {R, 2549}, {K, 2144}, {R, 2445}, {K, 2762},
                                         {R, 2437}, {K, 2174}});
        CHECK(t.phase() == MovePhase::WindowA);
        MoveCloseSequence s;
        CHECK(s.step(1000, t, &note) == A::InjectMoveComplete);
        CHECK(note.find("window A") != std::string::npos);
        t.on_req(mv::kMoveFieldCompleteReq);  // server.cpp records it before the push
        s.injected(true, 1000, tracker_at({{R, 3093}, {K, 2174}, {R, 2549}, {K, 2144}}));
        CHECK(s.stage() == S::WaitCompleteAck && s.closed_move());
        // The parking's earlier 2144 does not count; no 2144 yet: wait.
        CHECK(s.step(1500, t, &note) == A::Wait && note.empty());
        CHECK(s.step(3999, t, &note) == A::Wait);
        t.on_ack(mv::kMoveFieldCompleteAck);
        CHECK(s.step(4100, t, &note) == A::HandLogout);
        CHECK(note.find("MoveFieldCompleteAck 2144 out 3100 ms") != std::string::npos);
        CHECK(s.stage() == S::Done);
        CHECK(s.step(9000, t, &note) == A::HandLogout && note.empty());
        // Once done, a later window A of the same sequence injects nothing more.
        t.on_ack(mv::kMoveFieldAck);
        CHECK(s.step(9100, t) == A::HandLogout);
        s.reset();
        CHECK(s.stage() == S::Idle && !s.closed_move());
    }
    // cut3: window A in the parking load (no 2144 ever); the 2144 never comes: timeout.
    {
        const MovePhaseTracker t = tracker_at({{R, 3093}, {K, 2174}});
        MoveCloseSequence s(MoveCloseSequence::Config{3000, 3000});
        CHECK(s.step(10, t) == A::InjectMoveComplete);
        s.injected(true, 10, t);
        MovePhaseTracker after = t;
        after.on_req(mv::kMoveFieldCompleteReq);
        CHECK(s.step(3009, after) == A::Wait);
        CHECK(s.step(3010, after, &note) == A::HandLogout);
        CHECK(note.find("no MoveFieldCompleteAck 2144 in 3000 ms") != std::string::npos);
        CHECK(s.closed_move());
    }
    // The 2549 could not be handed: the 2001 as before, at once.
    {
        const MovePhaseTracker t = tracker_at({{R, 2437}, {K, 2174}});
        MoveCloseSequence s;
        CHECK(s.step(0, t) == A::InjectMoveComplete);
        s.injected(false, 0, t);
        CHECK(s.stage() == S::Done && !s.closed_move());
        CHECK(s.step(1, t) == A::HandLogout);
    }
    // Left right after its GameLoadingStartReq: wait for the server's 2174, then close.
    {
        MovePhaseTracker t = tracker_at({{R, 3093}, {K, 2174}, {R, 2549}, {K, 2144}, {R, 2437}});
        MoveCloseSequence s;
        CHECK(s.step(100, t, &note) == A::Wait);
        CHECK(s.stage() == S::WaitMoveAck && note.find("GameLoadingStartReq") != std::string::npos);
        CHECK(s.step(600, t, &note) == A::Wait && note.empty());
        t.on_ack(mv::kMoveFieldAck);
        CHECK(s.step(700, t, &note) == A::InjectMoveComplete);
        CHECK(note.find("MoveFieldAck 2174 out after 600 ms") != std::string::npos);
        s.injected(true, 700, t);
        CHECK(s.stage() == S::WaitCompleteAck);
        t.on_req(mv::kMoveFieldCompleteReq);
        t.on_ack(mv::kMoveFieldCompleteAck);
        CHECK(s.step(800, t) == A::HandLogout);
    }
    // ... and the 2174 never comes: the 2001 after the limit, nothing injected.
    {
        const MovePhaseTracker t = tracker_at({{R, 2437}});
        MoveCloseSequence s;
        CHECK(s.step(0, t) == A::Wait);
        CHECK(s.step(2999, t) == A::Wait);
        CHECK(s.step(3000, t, &note) == A::HandLogout);
        CHECK(note.find("no MoveFieldAck 2174") != std::string::npos && !s.closed_move());
    }
    // ... or the phase moves elsewhere meanwhile (not window A): the 2001.
    {
        MovePhaseTracker t = tracker_at({{R, 2437}});
        MoveCloseSequence s;
        CHECK(s.step(0, t) == A::Wait);
        t.on_req(mv::kLoginCharacterReq);
        CHECK(s.step(10, t) == A::HandLogout && !s.closed_move());
    }
    // No open move: the 2001 directly (window B = cut9, stable, unknown, the client's own 2549 in flight).
    for (const MovePhaseTracker& t :
         {tracker_at({}), tracker_at({{R, 3093}}), tracker_at({{R, 2437}, {K, 2174}, {R, 2549}}),
          tracker_at({{R, 2437}, {K, 2174}, {R, 2549}, {K, 2144}}),
          tracker_at({{R, 2437}, {K, 2174}, {R, 2549}, {K, 2144}, {R, 2445}}),
          tracker_at({{R, 2437}, {K, 2174}, {R, 2549}, {K, 2144}, {R, 2445}, {K, 2762}})}) {
        MoveCloseSequence s;
        CHECK(s.step(0, t, &note) == A::HandLogout);
        CHECK(note.find("no open move") != std::string::npos);
        CHECK(s.stage() == S::Done && !s.closed_move());
    }
    // The tick counter wraps (GetTickCount64 cut to 32 bits).
    {
        const MovePhaseTracker t = tracker_at({{R, 2437}, {K, 2174}});
        MoveCloseSequence s;
        CHECK(s.step(0xFFFFFF00u, t) == A::InjectMoveComplete);
        s.injected(true, 0xFFFFFF00u, t);
        CHECK(s.step(0x100u, t) == A::Wait);  // 0x200 ms later
        CHECK(s.step(0xFFFFFF00u + 3000u, t) == A::HandLogout);
    }
}

// The barrier's read-only diagnostics through a still-attached user.
void test_move_diag() {
    Blob user(0x200), pj(0x200), comps(0x200), ts(0x600);
    put<uint16_t>(user.p(), c4::kActorState, 0x20);
    put<uintptr_t>(user.p(), c4::kUserMainCharacter, pj.a());
    put<uint16_t>(pj.p(), c4::kActorState, 0x20);
    put<uintptr_t>(pj.p(), c4::kActorComponents, comps.a());
    put<uintptr_t>(comps.p(), c4::kComponentsTransformSync, ts.a());
    put<uint8_t>(ts.p(), c4::kTsMoving, 1);
    put<uint8_t>(ts.p(), fld::kTsFieldMover + c4::kFmMoving, 2);
    put<uint16_t>(ts.p(), fld::kTsFieldMover + fld::kFmBusy, 0);
    const MoveDiag d = read_move_diag(user.a());
    CHECK(d.user_ok && d.pj_ok && d.fm_ok && d.ts_ok);
    CHECK(d.user_state == 0x20 && d.pj_state == 0x20 && d.fm_moving == 2 && d.fm_busy == 0 && d.ts_moving == 1);
    CHECK(d.describe() == "user +0x5E 0x20 | character +0x5E 0x20 | FM+0x58 0x2 FM+0x5A 0x0 | TS+0x3DC 0x1");
    // No character: only the user's state.
    put<uintptr_t>(user.p(), c4::kUserMainCharacter, 0);
    const MoveDiag u = read_move_diag(user.a());
    CHECK(u.user_ok && !u.pj_ok && !u.fm_ok && !u.ts_ok);
    CHECK(u.describe().find("character +0x5E ?") != std::string::npos);
    CHECK(!read_move_diag(0).user_ok);
}

// p8-tunel-rendimiento.md: the probes read the same values with either kind
// of read; memory that is not readable fails both ways (never a crash); and
// the SEH-only reads do not pay VirtualQuery's walk of a large region.
void test_probe_reads() {
    for (const bool safe : {false, true}) {
        set_probe_safe_reads(safe);
        CHECK(probe_safe_reads() == safe);
        // A reserved, never committed page: unreadable.
        auto* reserved = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x10000, MEM_RESERVE, PAGE_NOACCESS));
        CHECK(reserved != nullptr);
        CHECK(!probe<uint32_t>(addr(reserved)).has_value());
        CHECK(!probe_ptr(addr(reserved)).has_value());
        uint8_t buf[16] = {};
        CHECK(!probe_read(addr(reserved) + 8, buf, sizeof buf));
        CHECK(!probe<uint32_t>(0).has_value() && !probe<uint32_t>(0x1000).has_value());
        // A SAM whose holder points at that page: unreadable, not "absent".
        FakeSam sam;
        FakeActor a(0xA01002EC);
        sam.add(a, 0xA01002EC);
        CHECK(sam_key_resolves(sam.a(), 0xA01002EC) == std::optional<bool>(true));
        sam.holders[0] = addr(reserved);
        CHECK(!sam_key_resolves(sam.a(), 0xA01002EC).has_value());
        // A user list whose data is not readable.
        FakeSam s2;
        put(s2.p(), c4::kSamUserList, addr(reserved));
        put(s2.p(), c4::kSamUserCount, 3u);
        CHECK(!sam_user_listed(s2.a(), 0x901002EB).has_value());
        // An implausible pointer value is no pointer.
        uint64_t small = 0x42;
        CHECK(!probe_ptr(addr(&small)).has_value());
        VirtualFree(reserved, 0, MEM_RELEASE);
    }
    set_probe_safe_reads(false);
    for (const bool safe : {false, true}) {  // the probe tests above, with each kind of read
        set_probe_safe_reads(safe);
        test_sam_lookup();
        test_sam_user_list();
        test_user_keys_and_capture();
        test_field();
        test_move_diag();
    }
    set_probe_safe_reads(false);
    // Cost inside a large committed region (the game's heap regions are hundreds of MB).
    constexpr size_t kRegion = 256u << 20;
    auto* big = static_cast<uint8_t*>(VirtualAlloc(nullptr, kRegion, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(big != nullptr);
    if (!big) return;
    for (size_t off = 0; off < kRegion; off += 4096) big[off] = 1;
    constexpr int kReads = 200;
    double us[2] = {};
    for (const bool safe : {false, true}) {
        set_probe_safe_reads(safe);
        LARGE_INTEGER f, t0, t1;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t0);
        uint64_t sum = 0;
        for (int i = 0; i < kReads; ++i) sum += probe<uint8_t>(addr(big) + (((kRegion / kReads) * i) & ~size_t{0xFFF})).value_or(0);
        QueryPerformanceCounter(&t1);
        CHECK(sum == kReads);
        us[safe ? 1 : 0] = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1e6 / static_cast<double>(f.QuadPart) / kReads;
    }
    set_probe_safe_reads(false);
    std::printf("optd_c4: a read in a 256 MB committed region: %.3f us SEH only, %.1f us with VirtualQuery\n", us[0],
                us[1]);
    CHECK(us[0] * 10 < us[1]);
    VirtualFree(big, 0, MEM_RELEASE);
}

}  // namespace

// P8 review I2: a fast read that touches a guard page fails like an unreadable address AND the guard is put
// back (as another thread's stack guard must stay); safe_read refuses it without touching it.
bool raw_touch_raises_guard(const volatile uint8_t* p) {
    __try {
        (void)*p;
        return false;
    } __except (GetExceptionCode() == EXCEPTION_GUARD_PAGE ? EXCEPTION_EXECUTE_HANDLER
                                                           : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
}

void test_guard_page_reads() {
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE | PAGE_GUARD));
    CHECK(page != nullptr);
    if (!page) return;
    const auto a = reinterpret_cast<uintptr_t>(page) + 64;
    const auto before = cdmp::game::guard_stats();
    uint64_t v = 0;
    CHECK(!cdmp::game::safe_read(a, &v, sizeof v));  // VirtualQuery: refused, untouched
    MEMORY_BASIC_INFORMATION mbi{};
    CHECK(VirtualQuery(page, &mbi, sizeof mbi) && (mbi.Protect & PAGE_GUARD));
    for (int i = 0; i < 3; ++i) {
        CHECK(!cdmp::game::fast_read(a, &v, sizeof v));
        CHECK(VirtualQuery(page, &mbi, sizeof mbi) && (mbi.Protect & PAGE_GUARD) &&
              (mbi.Protect & 0xFF) == PAGE_READWRITE);
    }
    uint8_t bytes[24] = {};
    CHECK(!cdmp::game::fast_read(a, bytes, sizeof bytes));  // the byte loop too
    CHECK(cdmp::game::guard_stats().rearmed == before.rearmed + 4 && cdmp::game::guard_stats().failed == before.failed);
    // The guard really is armed again: the next raw touch still raises it (one shot), and then it is gone.
    CHECK(raw_touch_raises_guard(page));
    CHECK(VirtualQuery(page, &mbi, sizeof mbi) && !(mbi.Protect & PAGE_GUARD));
    CHECK(cdmp::game::fast_read(a, &v, sizeof v) && v == 0);  // a plain page now: read
    VirtualFree(page, 0, MEM_RELEASE);
    // A reserved, uncommitted page: an access violation, as before.
    auto* res = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE, PAGE_NOACCESS));
    CHECK(res && !cdmp::game::fast_read(reinterpret_cast<uintptr_t>(res), &v, sizeof v));
    if (res) VirtualFree(res, 0, MEM_RELEASE);
}

int main() {
    test_guard_page_reads();
    test_sam_lookup();
    test_sam_user_list();
    test_user_keys_and_capture();
    test_field();
    test_barrier();
    test_pb_tags();
    test_lifecycle_cycle();
    test_lifecycle_inband();
    test_lifecycle_edges();
    test_move_phase_tracker();
    test_move_close_sequence();
    test_move_diag();
    test_probe_reads();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_c4: all tests passed\n");
    return 0;
}
