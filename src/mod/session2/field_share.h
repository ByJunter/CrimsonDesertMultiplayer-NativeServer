#pragma once
// Option D, E3b (docs/research/opcion-d-campo-compartido.md): user 2 enters
// the host's ServerField instead of getting one of its own. The ServerField
// is built for several users (the list at +0x2A0, ToField, closing when every
// user actor has left), but ServerField::AddActor (.xtls) refuses a second
// user, and FieldManager::AcquireField always takes a new field from the pool.
// Two hooks on .rsrc function ENTRIES, only for user 2's character:
//
//   H1 FieldManager::AcquireField 0x2B07790: when user 2 travels to the level
//      of the host's open field, hand it the host's field (as the function
//      itself does at 0x2B079D5..0x2B07A47, but "not newly created": +0x20 = 0,
//      or a failure later would close the host's field).
//   H2 FieldMover::EnterField 0x2BC04F0: before the original, put {user 2, 0}
//      into the field's user list under the field's two locks, so AddActor
//      finds the user and skips the one-user cap; taken out again if the
//      original fails and the entry is still unused.
//
// Everything else stays native: AddActor/RemoveActor count the user's actors,
// the field closes only when every user actor has left.

#include <cstdint>
#include <string>

namespace cdmp::session2 {

// The game functions the hooks call (injectable for the tests).
struct FieldGame {
    bool(__fastcall* try_add_ref)(void* field);                 // 0x14343A0 (al = taken)
    void(__fastcall* vec_push)(void* vec, const void* entry);  // 0x40B5B0, 16-byte entries
};

// An entry of field+0x2A0 (AddActor: {user, 1} at [rsp+0x20], 0x1064C852).
struct FieldUserEntry {
    uint64_t user;
    uint32_t actors;
    uint32_t pad;
};

// ServerField and FieldMover offsets (build 1.0.0.2976).
namespace fld {
inline constexpr uint32_t kRefs = 0x10;          // u32 strong references
inline constexpr uint32_t kLock1 = 0x18;         // lock object (vtable: +8 lock, +0x10 unlock)
inline constexpr uint32_t kLock2Ptr = 0x08;      // -> lock object
inline constexpr uint32_t kOpen = 0x4A;          // u8 open
inline constexpr uint32_t kOwner = 0x180;        // u32 owner user key
inline constexpr uint32_t kUsers = 0x2A0;        // FieldUserEntry*
inline constexpr uint32_t kUserCount = 0x2A8;    // u32
inline constexpr uint32_t kUserActors = 0x2B0;   // u32
inline constexpr uint32_t kKey = 0x2C8;          // u32 field key (index, pool, generation)
inline constexpr uint32_t kLevelEntry = 0x2D0;   // -> level entry; u16 static level index at +4
// FieldMover (FM) = TransformSync + 0x4F0.
inline constexpr uint32_t kTsFieldMover = 0x4F0;
inline constexpr uint32_t kFmFieldKey = 0x08;    // u32 copy of field+0x2C8
inline constexpr uint32_t kFmField = 0x10;       // ServerField*
inline constexpr uint32_t kFmBusy = 0x5A;        // u16 accesses in progress
// FieldAttacher (AcquireField's `out`, 0x28 bytes, vtable: +0x10 release):
// written at 0x2B07A19 (+0x18), 0x2B07A1D (+8), 0x2B07A21 (+0x10), 0x2B07A35 (+0x20).
inline constexpr uint32_t kOutField = 0x08;
inline constexpr uint32_t kOutValid = 0x10;      // u8
inline constexpr uint32_t kOutHolder = 0x18;
inline constexpr uint32_t kOutNew = 0x20;        // u8 "newly created": 1 closes the field on failure (0x2BC006A)
inline constexpr uint32_t kOutInfoKey = 0x24;    // u32 FieldInfoKey of the destination (0x2B077C2)
// FieldManager's FieldInfoKey hash (0x2B077BC..0x2B07840): buckets of 0x100
// bytes at [+0x58], bucket = key % [+0x48]; a bucket is {u32 n; pad; n x {u32
// key, u32 index}}; [[+0x60] + index*8] = holder with +4 key and +8 level entry.
inline constexpr uint32_t kMgrBucketCount = 0x48;
inline constexpr uint32_t kMgrEntries = 0x4C;
inline constexpr uint32_t kMgrBuckets = 0x58;
inline constexpr uint32_t kMgrHolders = 0x60;
}  // namespace fld

// The level entry AcquireField would use for this FieldInfoKey (its own
// lookup, read only); 0 if not found.
uintptr_t level_entry_for(uintptr_t manager, uint32_t info_key);

// The host's open field, if user 2 may share it now (H1's conditions 2-5 of
// opcion-d-campo-compartido.md §3.1): host_ts = the host character's
// TransformSync, level_entry = the level entry user 2 travels to (exactly the
// same entry as the host's field, not only the same level number). 0 = no
// (`why` says which check failed).
uintptr_t share_candidate(uintptr_t host_ts, uint32_t host_key, uintptr_t level_entry, std::string& why);

// H2's gate: the character entering (FM's [[FM]+8]) belongs to user2 and the
// field is not user 2's own. False for the host and for user 2's own field.
bool enter_is_shared(uintptr_t fm, uintptr_t field, uintptr_t user2);

// H1's action on AcquireField's `out` (0x2B079DD..0x2B07A47 with +0x20 = 0).
// False = nothing changed (the reference could not be taken).
bool share_give_field(const FieldGame& g, uintptr_t field, uintptr_t out, uint32_t* res);

// H2: {user, 0} into the field's list under its two locks, if not there.
// True = inserted (undo it with enter_undo if the enter fails).
bool enter_insert(const FieldGame& g, uintptr_t field, uintptr_t user);
// Takes user's entry out again if it is still at 0 actors.
void enter_undo(uintptr_t field, uintptr_t user);

// Install (hooks H2 then H1, after checking their first bytes); false = none.
bool field_share_install();
bool field_share_active();
std::string field_share_describe();

}  // namespace cdmp::session2
