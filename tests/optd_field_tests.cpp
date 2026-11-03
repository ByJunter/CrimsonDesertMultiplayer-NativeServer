// Option D, E3b: the field-sharing logic (src/mod/session2/field_share.cpp)
// on fake game memory: a ServerField with its two locks and user list, the
// host's FieldMover, and AcquireField's attacher. The hooks themselves need
// the game; this checks what they do to the game's objects.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mod/session2/field_share.h"

namespace cdmp::session2 {
// The real one is in server.cpp (needs the game); the logic tested here does not use it.
bool server_users(uintptr_t&, uintptr_t&) { return false; }
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

// A lock object as the game's: first field a vtable, +8 lock, +0x10 unlock.
struct FakeLock;
void __fastcall fake_nop(void*) {}
void __fastcall fake_lock(void* self);
void __fastcall fake_unlock(void* self);
void* g_lock_vtable[3] = {reinterpret_cast<void*>(&fake_nop), reinterpret_cast<void*>(&fake_lock),
                          reinterpret_cast<void*>(&fake_unlock)};
struct FakeLock {
    void** vt = g_lock_vtable;
    int depth = 0;
    int taken = 0;
};
void __fastcall fake_lock(void* self) {
    ++static_cast<FakeLock*>(self)->depth;
    ++static_cast<FakeLock*>(self)->taken;
}
void __fastcall fake_unlock(void* self) { --static_cast<FakeLock*>(self)->depth; }

// The list must hold at most this many, as the game's would grow it.
FieldUserEntry g_storage[8];

void __fastcall fake_push(void* vec, const void* entry) {
    auto* base = static_cast<uint8_t*>(vec);  // field+0x2A0: data, +8 count, +0xC capacity
    auto*& data = *reinterpret_cast<FieldUserEntry**>(base);
    auto& count = *reinterpret_cast<uint32_t*>(base + 8);
    std::memcpy(&data[count], entry, sizeof(FieldUserEntry));
    ++count;
}
int g_refs_taken = 0;
bool g_refuse_ref = false;
bool __fastcall fake_try_add_ref(void*) {
    if (g_refuse_ref) return false;
    ++g_refs_taken;
    return true;
}

struct World {
    alignas(16) uint8_t field[0x310] = {};
    FakeLock lock2;
    uint8_t level_entry[0x20] = {};
    alignas(16) uint8_t ts[0x600] = {};
    uintptr_t f() { return reinterpret_cast<uintptr_t>(field); }
    template <class T>
    T& at(uint8_t* base, uint32_t off) {
        return *reinterpret_cast<T*>(base + off);
    }
    World() {
        new (field + fld::kLock1) FakeLock();
        at<FakeLock*>(field, fld::kLock2Ptr) = &lock2;
        at<FieldUserEntry*>(field, fld::kUsers) = g_storage;
        at<uint32_t>(field, fld::kUserCount) = 0;
        at<uint32_t>(field, fld::kUsers + 0xC) = 8;
        at<uint8_t>(field, fld::kOpen) = 1;
        at<uint32_t>(field, fld::kOwner) = 0x90100000;
        at<uint32_t>(field, fld::kKey) = 0x81020001;
        at<uint8_t*>(field, fld::kLevelEntry) = level_entry;
        at<uint16_t>(level_entry, 4) = 0x123;
        uint8_t* fm = ts + fld::kTsFieldMover;
        at<uintptr_t>(fm, fld::kFmField) = f();
        at<uint32_t>(fm, fld::kFmFieldKey) = 0x81020001;
        at<uint16_t>(fm, fld::kFmBusy) = 0;
    }
    FakeLock& lock1() { return *reinterpret_cast<FakeLock*>(field + fld::kLock1); }
    uint32_t users() { return at<uint32_t>(field, fld::kUserCount); }
};

// AcquireField's attacher: a vtable with +0x10 release.
int g_released = 0;
void __fastcall fake_release(void*) { ++g_released; }
void* g_out_vtable[3] = {reinterpret_cast<void*>(&fake_nop), reinterpret_cast<void*>(&fake_nop),
                         reinterpret_cast<void*>(&fake_release)};

}  // namespace

int main() {
    const FieldGame game{&fake_try_add_ref, &fake_push};
    const uint32_t host_key = 0x90100000;
    const uintptr_t user2 = 0x0000AABBCCDD0000ull;
    const uintptr_t host_user = 0x0000AABBCCEE0000ull;

    // share_candidate: every condition of opcion-d-campo-compartido.md §3.1.
    {
        World w;
        const auto ts = reinterpret_cast<uintptr_t>(w.ts);
        const auto entry = reinterpret_cast<uintptr_t>(w.level_entry);
        std::string why;
        CHECK(share_candidate(ts, host_key, entry, why) == w.f());
        CHECK(share_candidate(ts, host_key, entry + 8, why) == 0 && why == "another level than the host's");
        CHECK(share_candidate(ts, 0x90100001, entry, why) == 0 && why == "the host does not own the field");
        w.at<uint16_t>(w.ts + fld::kTsFieldMover, fld::kFmBusy) = 1;
        CHECK(share_candidate(ts, host_key, entry, why) == 0 && why == "the host is moving between fields");
        w.at<uint16_t>(w.ts + fld::kTsFieldMover, fld::kFmBusy) = 0;
        w.at<uint8_t>(w.field, fld::kOpen) = 0;
        CHECK(share_candidate(ts, host_key, entry, why) == 0 && why == "the host's field is not open");
        w.at<uint8_t>(w.field, fld::kOpen) = 1;
        w.at<uint32_t>(w.ts + fld::kTsFieldMover, fld::kFmFieldKey) = 0x81020000;
        CHECK(share_candidate(ts, host_key, entry, why) == 0 && why.find("key does not match") != std::string::npos);
        w.at<uint32_t>(w.ts + fld::kTsFieldMover, fld::kFmFieldKey) = 0x81020001;
        w.at<uintptr_t>(w.ts + fld::kTsFieldMover, fld::kFmField) = 0;
        CHECK(share_candidate(ts, host_key, entry, why) == 0 && why == "the host has no field");
        CHECK(share_candidate(0, host_key, entry, why) == 0);
        CHECK(share_candidate(0x10, host_key, entry, why) == 0);  // unreadable: no crash
    }

    // level_entry_for: AcquireField's own FieldInfoKey hash (0x2B077BC..0x2B07840).
    {
        alignas(16) uint8_t mgr[0x80] = {};
        alignas(16) uint8_t buckets[4 * 0x100] = {};
        uint8_t holder_a[0x10] = {}, holder_b[0x10] = {}, entry_a[8] = {}, entry_b[8] = {};
        void* holders[2] = {holder_a, holder_b};
        *reinterpret_cast<uint32_t*>(mgr + fld::kMgrBucketCount) = 4;
        *reinterpret_cast<uint32_t*>(mgr + fld::kMgrEntries) = 2;
        *reinterpret_cast<uint8_t**>(mgr + fld::kMgrBuckets) = buckets;
        *reinterpret_cast<void***>(mgr + fld::kMgrHolders) = holders;
        // key 0x1002 -> bucket 2 (0x1002 % 4), index 0; key 0x2006 -> bucket 2, index 1.
        uint8_t* b2 = buckets + 2 * 0x100;
        *reinterpret_cast<uint32_t*>(b2) = 2;
        *reinterpret_cast<uint32_t*>(b2 + 8) = 0x1002;
        *reinterpret_cast<uint32_t*>(b2 + 0xC) = 0;
        *reinterpret_cast<uint32_t*>(b2 + 0x10) = 0x2006;
        *reinterpret_cast<uint32_t*>(b2 + 0x14) = 1;
        *reinterpret_cast<uint32_t*>(holder_a + 4) = 0x1002;
        *reinterpret_cast<uint8_t**>(holder_a + 8) = entry_a;
        *reinterpret_cast<uint32_t*>(holder_b + 4) = 0x2006;
        *reinterpret_cast<uint8_t**>(holder_b + 8) = entry_b;
        const auto m = reinterpret_cast<uintptr_t>(mgr);
        CHECK(level_entry_for(m, 0x1002) == reinterpret_cast<uintptr_t>(entry_a));
        CHECK(level_entry_for(m, 0x2006) == reinterpret_cast<uintptr_t>(entry_b));
        CHECK(level_entry_for(m, 0x3002) == 0);  // same bucket, unknown key
        CHECK(level_entry_for(m, 0x1003) == 0);  // empty bucket
        *reinterpret_cast<uint32_t*>(holder_b + 4) = 0x9999;  // holder of another key
        CHECK(level_entry_for(m, 0x2006) == 0);
        *reinterpret_cast<uint32_t*>(mgr + fld::kMgrEntries) = 0;
        CHECK(level_entry_for(m, 0x1002) == 0);  // empty table, as the game's 0x2B077C0
        CHECK(level_entry_for(0x10, 0x1002) == 0);
    }

    // enter_is_shared: only user 2's character, only into a field it does not own.
    {
        World w;
        alignas(16) uint8_t user2_obj[0x80] = {}, host_obj[0x80] = {}, actor[0x100] = {}, holder[0x10] = {};
        const auto u2 = reinterpret_cast<uintptr_t>(user2_obj);
        *reinterpret_cast<uint32_t*>(user2_obj + 0x60) = 0x901002D4;
        *reinterpret_cast<uint32_t*>(host_obj + 0x60) = host_key;
        *reinterpret_cast<uint8_t**>(holder + 8) = actor;
        uint8_t* fm_holder = holder;
        const auto fm = reinterpret_cast<uintptr_t>(&fm_holder);  // [FM] = holder, [holder+8] = actor
        *reinterpret_cast<uint8_t**>(actor + 0xA0) = user2_obj;
        CHECK(enter_is_shared(fm, w.f(), u2));  // the host's field (owner 0x90100000)
        w.at<uint32_t>(w.field, fld::kOwner) = 0x901002D4;
        CHECK(!enter_is_shared(fm, w.f(), u2));  // user 2's own field
        w.at<uint32_t>(w.field, fld::kOwner) = host_key;
        *reinterpret_cast<uint8_t**>(actor + 0xA0) = host_obj;
        CHECK(!enter_is_shared(fm, w.f(), u2));  // the host's character
        *reinterpret_cast<uint8_t**>(actor + 0xA0) = nullptr;
        CHECK(!enter_is_shared(fm, w.f(), u2));  // an NPC (no user)
        CHECK(!enter_is_shared(fm, w.f(), 0));
        CHECK(!enter_is_shared(0x10, w.f(), u2));
    }

    // share_give_field: the attacher as AcquireField fills it, but never "newly created".
    {
        World w;
        alignas(16) uint8_t out[0x28] = {};
        *reinterpret_cast<void***>(out) = g_out_vtable;
        std::memset(out + 8, 0xEE, 0x20);  // garbage that must be overwritten
        uint32_t res = 0xDEAD;
        g_released = g_refs_taken = 0;
        CHECK(share_give_field(game, w.f(), reinterpret_cast<uintptr_t>(out), &res));
        CHECK(res == 0);
        CHECK(g_released == 1 && g_refs_taken == 1);
        CHECK(*reinterpret_cast<uintptr_t*>(out + fld::kOutField) == w.f());
        CHECK(out[fld::kOutValid] == 1);
        CHECK(out[fld::kOutNew] == 0);
        CHECK(*reinterpret_cast<uint64_t*>(out + fld::kOutHolder) == 0);
        // The reference refused: nothing touched.
        uint8_t out2[0x28] = {};
        *reinterpret_cast<void***>(out2) = g_out_vtable;
        uint32_t res2 = 7;
        g_refuse_ref = true;
        g_released = 0;
        CHECK(!share_give_field(game, w.f(), reinterpret_cast<uintptr_t>(out2), &res2));
        CHECK(res2 == 7 && g_released == 0 && *reinterpret_cast<uintptr_t*>(out2 + fld::kOutField) == 0);
        g_refuse_ref = false;
        CHECK(!share_give_field(game, w.f(), 0, &res2));
    }

    // enter_insert / enter_undo: under both locks, balanced, no duplicates.
    {
        World w;
        g_storage[0] = {host_user, 2, 0};  // the host is in its field with 2 actors
        w.at<uint32_t>(w.field, fld::kUserCount) = 1;
        CHECK(enter_insert(game, w.f(), user2));
        CHECK(w.users() == 2 && g_storage[1].user == user2 && g_storage[1].actors == 0);
        CHECK(w.lock1().depth == 0 && w.lock2.depth == 0 && w.lock1().taken == 1 && w.lock2.taken == 1);
        CHECK(!enter_insert(game, w.f(), user2));  // already there
        CHECK(w.users() == 2);
        // AddActor would now count it: with 1 actor the undo keeps it.
        g_storage[1].actors = 1;
        enter_undo(w.f(), user2);
        CHECK(w.users() == 2);
        // Still at 0 (the enter failed before AddActor): taken out, the host stays.
        g_storage[1].actors = 0;
        enter_undo(w.f(), user2);
        CHECK(w.users() == 1 && g_storage[0].user == host_user && g_storage[0].actors == 2);
        CHECK(w.lock1().depth == 0 && w.lock2.depth == 0);
        // Undo when user 2 sits before another entry: the last one takes its place.
        g_storage[0] = {user2, 0, 0};
        g_storage[1] = {host_user, 2, 0};
        w.at<uint32_t>(w.field, fld::kUserCount) = 2;
        enter_undo(w.f(), user2);
        CHECK(w.users() == 1 && g_storage[0].user == host_user);
        // No second lock: nothing done.
        w.at<FakeLock*>(w.field, fld::kLock2Ptr) = nullptr;
        CHECK(!enter_insert(game, w.f(), user2));
        CHECK(w.users() == 1 && w.lock1().depth == 0);
    }

    if (g_failures == 0) std::printf("optd field tests: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
