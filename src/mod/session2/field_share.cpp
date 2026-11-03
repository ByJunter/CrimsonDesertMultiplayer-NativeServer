#include "mod/session2/field_share.h"

#include <windows.h>

#include <safetyhook.hpp>

#include <atomic>
#include <cstring>
#include <format>

#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

// Entries checked by hand (docs/research/opcion-d-campo-compartido.md §7.1).
// AcquireField and EnterField share 24 bytes of prologue; byte 24 on differs.
constexpr uintptr_t kAcquireField = 0x2B07790;
constexpr uint8_t kAcquireFieldHead[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
                                         0x89, 0x7C, 0x24, 0x20, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
                                         0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x70};
constexpr uintptr_t kEnterField = 0x2BC04F0;
constexpr uint8_t kEnterFieldHead[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
                                       0x89, 0x7C, 0x24, 0x20, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
                                       0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC};
constexpr uintptr_t kTryAddRef = 0x14343A0;
constexpr uint8_t kTryAddRefHead[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
                                      0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9};
constexpr uintptr_t kVecPush = 0x40B5B0;
constexpr uint8_t kVecPushHead[] = {0x40, 0x53, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0xD9,
                                    0x4C, 0x8B, 0xF2, 0x8B, 0x49, 0x08, 0x8B, 0x43, 0x0C};
// Host character -> TransformSync: [[pj+0x68]+0x1A0] (the probe's chain, live in B3).
constexpr uint32_t kUserMainCharacter = 0xD0;
constexpr uint32_t kUserKey = 0x60;
constexpr uint32_t kActorUser = 0xA0;  // actor -> ServerUserActor (AddActor 0x1064C78F)
constexpr uint64_t kMaxLogged = 24;

FieldGame g_game{};
SafetyHookInline g_h1, g_h2;
std::atomic<bool> g_active{false};
std::atomic<uint64_t> g_h1_calls{0}, g_h1_shared{0}, g_h1_refused{0}, g_h2_inserted{0}, g_h2_undone{0},
    g_logged{0};

template <class T>
bool peek(uintptr_t a, T& v) {
    return a && game::safe_read(a, &v, sizeof v);
}

using LockFn = void(__fastcall*)(void*);
// The field's locks are objects with a vtable: +8 lock, +0x10 unlock (AddActor
// 0x1064C7AE / 0x1064C877).
bool lock_call(uintptr_t obj, uint32_t slot) {
    uintptr_t vt = 0, fn = 0;
    if (!peek(obj, vt) || !peek(vt + slot, fn) || !fn) return false;
    reinterpret_cast<LockFn>(fn)(reinterpret_cast<void*>(obj));
    return true;
}

// Both locks in AddActor's order (+0x18, then [+8]); false = none taken.
bool lock_field(uintptr_t field, uintptr_t& second) {
    second = 0;
    if (!peek(field + fld::kLock2Ptr, second) || !second) return false;
    if (!lock_call(field + fld::kLock1, 8)) return false;
    if (!lock_call(second, 8)) {
        lock_call(field + fld::kLock1, 0x10);
        return false;
    }
    return true;
}

void unlock_field(uintptr_t field, uintptr_t second) {
    lock_call(second, 0x10);
    lock_call(field + fld::kLock1, 0x10);
}

// Index of user's entry in field+0x2A0, or -1.
int find_user(uintptr_t field, uintptr_t user, uintptr_t& data, uint32_t& count) {
    data = 0;
    count = 0;
    if (!peek(field + fld::kUsers, data) || !peek(field + fld::kUserCount, count) || !data) return -1;
    for (uint32_t i = 0; i < count && i < 64; ++i) {
        uint64_t u = 0;
        if (peek(data + i * sizeof(FieldUserEntry), u) && u == user) return static_cast<int>(i);
    }
    return -1;
}

// Logs at most kMaxLogged lines; callers are inside try blocks.
template <class... Args>
void note(std::format_string<Args...> fmt, Args&&... args) {
    if (g_logged.fetch_add(1) >= kMaxLogged) return;
    log::info(fmt, std::forward<Args>(args)...);
}

std::string hex(uintptr_t v) { return std::format("0x{:X}", v); }

// H1: FieldManager::AcquireField(manager, res*, u8 pool type, out*, travelling user).
// Nothing may throw out of here: any exception falls back to the original.
uint32_t* __fastcall acquire_detour(void* mgr, uint32_t* res, uint8_t type, void* out, void* traveller) noexcept {
    bool shared = false;
    try {
        uintptr_t host = 0, user2 = 0;
        const auto t = reinterpret_cast<uintptr_t>(traveller);
        if (t && server_users(host, user2) && t == user2) {
            g_h1_calls.fetch_add(1);
            std::string why;
            uint32_t host_key = 0, info_key = 0;
            uintptr_t pj = 0, c68 = 0, ts = 0, field = 0, entry = 0;
            if (!peek(reinterpret_cast<uintptr_t>(out) + fld::kOutInfoKey, info_key) ||
                !(entry = level_entry_for(reinterpret_cast<uintptr_t>(mgr), info_key))) {
                why = "destination level not found";
            } else if (!peek(host + kUserKey, host_key) || !peek(host + kUserMainCharacter, pj) || !pj ||
                       !peek(pj + 0x68, c68) || !c68 || !peek(c68 + 0x1A0, ts) || !ts) {
                why = "host character unreadable";
            } else {
                field = share_candidate(ts, host_key, entry, why);
            }
            // With two users only the host can reopen a field from the pool, so a
            // field that changed between these reads and TryAddRef is the host's
            // new field, still a right one to share (E3b review, race C3).
            shared = field && share_give_field(g_game, field, reinterpret_cast<uintptr_t>(out), res);
            if (shared) {
                g_h1_shared.fetch_add(1);
                note("Session2 E3b H1: user 2 travels to the host's level (FieldInfoKey 0x{:X}): given the host's "
                     "field {} (no new field from the pool)",
                     info_key, hex(field));
            } else {
                g_h1_refused.fetch_add(1);
                note("Session2 E3b H1: user 2 travels (FieldInfoKey 0x{:X}): own field as before ({})", info_key,
                     why.empty() ? std::string("the reference could not be taken") : why);
            }
        }
    } catch (...) {
    }
    if (shared) return res;
    return g_h1.unsafe_call<uint32_t*>(mgr, res, type, out, traveller);
}

// H2: FieldMover::EnterField(FM, res*, field, companion).
uint32_t* __fastcall enter_detour(void* fm, uint32_t* res, void* field, void* companion) noexcept {
    const auto f = reinterpret_cast<uintptr_t>(field);
    uintptr_t host = 0, user2 = 0;
    bool inserted = false;
    try {
        if (f && server_users(host, user2) && enter_is_shared(reinterpret_cast<uintptr_t>(fm), f, user2)) {
            inserted = enter_insert(g_game, f, user2);
            if (inserted) {
                g_h2_inserted.fetch_add(1);
                uint32_t owner = 0;
                peek(f + fld::kOwner, owner);
                note("Session2 E3b H2: user 2 enters field {} (owner 0x{:X}): put in its user list before AddActor",
                     hex(f), owner);
            }
        }
    } catch (...) {
    }
    uint32_t* r = g_h2.unsafe_call<uint32_t*>(fm, res, field, companion);
    if (inserted) {
        try {
            uint32_t code = 0;
            if (peek(reinterpret_cast<uintptr_t>(r), code) && code != 0) {
                enter_undo(f, user2);
                g_h2_undone.fetch_add(1);
                note("Session2 E3b H2: entering field {} failed (0x{:X}); user 2 taken out of its list", hex(f), code);
            } else {
                uint32_t users = 0, uactors = 0;
                peek(f + fld::kUserCount, users);
                peek(f + fld::kUserActors, uactors);
                note("Session2 E3b H2: user 2 is in field {}: users {} user actors {}", hex(f), users, uactors);
            }
        } catch (...) {
        }
    }
    return r;
}

bool head_ok(uintptr_t base, uintptr_t rva, const uint8_t* head, size_t n, const char* what) {
    uint8_t actual[40] = {};
    if (n <= sizeof actual && game::safe_read(base + rva, actual, n) && std::memcmp(actual, head, n) == 0) return true;
    log::error("Session2 E3b: {} (exe+0x{:X}) is not the expected code; field sharing stays off", what, rva);
    return false;
}

}  // namespace

uintptr_t level_entry_for(uintptr_t manager, uint32_t info_key) {
    uint32_t entries = 0, buckets = 0, n = 0;
    uintptr_t bucket_base = 0, holders = 0;
    if (!peek(manager + fld::kMgrEntries, entries) || !entries || !peek(manager + fld::kMgrBucketCount, buckets) ||
        !buckets || !peek(manager + fld::kMgrBuckets, bucket_base) || !peek(manager + fld::kMgrHolders, holders)) {
        return 0;
    }
    const uintptr_t bucket = bucket_base + (static_cast<uintptr_t>(info_key % buckets) << 8);
    if (!peek(bucket, n)) return 0;
    for (uint32_t i = 0; i < n && i < 31; ++i) {  // 31 pairs fit in a 0x100-byte bucket
        uint32_t key = 0, index = 0, holder_key = 0;
        uintptr_t holder = 0, entry = 0;
        if (!peek(bucket + 8 + i * 8ull, key) || key != info_key || !peek(bucket + 0xC + i * 8ull, index)) continue;
        if (peek(holders + index * 8ull, holder) && holder && peek(holder + 4, holder_key) && holder_key == info_key &&
            peek(holder + 8, entry)) {
            return entry;
        }
    }
    return 0;
}

uintptr_t share_candidate(uintptr_t host_ts, uint32_t host_key, uintptr_t level_entry, std::string& why) {
    const uintptr_t fm = host_ts + fld::kTsFieldMover;
    uintptr_t field = 0, entry = 0;
    uint32_t fm_key = 0, key = 0, owner = 0;
    uint16_t busy = 0;
    uint8_t open = 0;
    if (!host_ts || !peek(fm + fld::kFmField, field) || !field) {
        why = "the host has no field";
        return 0;
    }
    if (!peek(fm + fld::kFmFieldKey, fm_key) || !peek(field + fld::kKey, key) || fm_key != key) {
        why = "the host's field key does not match (being replaced?)";
        return 0;
    }
    if (!peek(field + fld::kOpen, open) || open != 1) {
        why = "the host's field is not open";
        return 0;
    }
    if (!peek(field + fld::kOwner, owner) || owner != host_key) {
        why = "the host does not own the field";
        return 0;
    }
    if (!peek(fm + fld::kFmBusy, busy) || busy != 0) {
        why = "the host is moving between fields";
        return 0;
    }
    if (!peek(field + fld::kLevelEntry, entry) || !entry || entry != level_entry) {
        why = "another level than the host's";
        return 0;
    }
    return field;
}

bool enter_is_shared(uintptr_t fm, uintptr_t field, uintptr_t user2) {
    uintptr_t holder = 0, actor = 0, user = 0;
    uint32_t owner = 0, key2 = 0;
    // The character: [[FM]+8] (0x2BC0520..23), its user at actor+0xA0 (0x1064C78F).
    return user2 && peek(fm, holder) && holder && peek(holder + 8, actor) && actor && peek(actor + kActorUser, user) &&
           user == user2 && peek(field + fld::kOwner, owner) && peek(user2 + kUserKey, key2) && owner != key2;
}

bool share_give_field(const FieldGame& g, uintptr_t field, uintptr_t out, uint32_t* res) {
    uintptr_t vt = 0, release = 0;
    if (!out || !res || !peek(out, vt) || !peek(vt + 0x10, release) || !release) return false;
    if (!g.try_add_ref(reinterpret_cast<void*>(field))) return false;
    // As 0x2B07A10..0x2B07A47: release what out held, then fill it.
    reinterpret_cast<LockFn>(release)(reinterpret_cast<void*>(out));
    *reinterpret_cast<uint64_t*>(out + fld::kOutHolder) = 0;
    *reinterpret_cast<uintptr_t*>(out + fld::kOutField) = field;
    *reinterpret_cast<uint8_t*>(out + fld::kOutValid) = 1;
    *reinterpret_cast<uint8_t*>(out + fld::kOutNew) = 0;  // never "newly created"
    *res = 0;
    return true;
}

bool enter_insert(const FieldGame& g, uintptr_t field, uintptr_t user) {
    uintptr_t second = 0, data = 0;
    uint32_t count = 0;
    if (!user || !lock_field(field, second)) return false;
    bool inserted = false;
    if (find_user(field, user, data, count) < 0) {
        const FieldUserEntry e{user, 0, 0};
        g.vec_push(reinterpret_cast<void*>(field + fld::kUsers), &e);
        inserted = true;
    }
    unlock_field(field, second);
    return inserted;
}

void enter_undo(uintptr_t field, uintptr_t user) {
    uintptr_t second = 0, data = 0;
    uint32_t count = 0;
    if (!lock_field(field, second)) return;
    const int i = find_user(field, user, data, count);
    FieldUserEntry e{};
    if (i >= 0 && game::safe_read(data + i * sizeof e, &e, sizeof e) && e.actors == 0) {
        // As RemoveActor: the last entry takes its place.
        auto* entries = reinterpret_cast<FieldUserEntry*>(data);
        entries[i] = entries[count - 1];
        *reinterpret_cast<uint32_t*>(field + fld::kUserCount) = count - 1;
    }
    unlock_field(field, second);
}

bool field_share_install() {
    if (g_active.load()) return true;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!head_ok(base, kAcquireField, kAcquireFieldHead, sizeof kAcquireFieldHead, "FieldManager::AcquireField") ||
        !head_ok(base, kEnterField, kEnterFieldHead, sizeof kEnterFieldHead, "FieldMover::EnterField") ||
        !head_ok(base, kTryAddRef, kTryAddRefHead, sizeof kTryAddRefHead, "TryAddRef") ||
        !head_ok(base, kVecPush, kVecPushHead, sizeof kVecPushHead, "vector push")) {
        return false;
    }
    g_game.try_add_ref = reinterpret_cast<decltype(g_game.try_add_ref)>(base + kTryAddRef);
    g_game.vec_push = reinterpret_cast<decltype(g_game.vec_push)>(base + kVecPush);
    // H2 first: H1 without H2 would send user 2 into a field AddActor refuses.
    g_h2 = safetyhook::create_inline(reinterpret_cast<void*>(base + kEnterField),
                                     reinterpret_cast<void*>(&enter_detour), SafetyHookInline::StartDisabled);
    if (!g_h2 || !g_h2.enable()) {
        g_h2 = {};
        log::error("Session2 E3b: cannot hook FieldMover::EnterField; field sharing stays off");
        return false;
    }
    g_h1 = safetyhook::create_inline(reinterpret_cast<void*>(base + kAcquireField),
                                     reinterpret_cast<void*>(&acquire_detour), SafetyHookInline::StartDisabled);
    if (!g_h1 || !g_h1.enable()) {
        g_h1 = {};
        (void)g_h2.disable();  // disabled, not destroyed: a thread may be inside
        log::error("Session2 E3b: cannot hook FieldManager::AcquireField; field sharing stays off");
        return false;
    }
    g_active.store(true);
    log::info("Session2 E3b ON: user 2 shares the host's field when it travels to the host's level "
              "(hooks exe+0x{:X} AcquireField, exe+0x{:X} EnterField)",
              kAcquireField, kEnterField);
    return true;
}

bool field_share_active() { return g_active.load(); }

std::string field_share_describe() {
    return std::format("E3b: user 2 travels {} shared {} own field {} | list inserts {} undone {}", g_h1_calls.load(),
                       g_h1_shared.load(), g_h1_refused.load(), g_h2_inserted.load(), g_h2_undone.load());
}

}  // namespace cdmp::session2
