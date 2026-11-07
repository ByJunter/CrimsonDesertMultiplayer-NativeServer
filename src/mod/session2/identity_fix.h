#pragma once
// Option D, C2 (docs/research/pendientes/identidad.md §5): user 2 gets a
// characterNo of its own. The host's server gives every character the same
// 0x7FFFFFFFFFFFFFFF from two independent places, so both are normalised,
// each right after the native code that sets it and before anything sends or
// looks it up:
//
//   lobby   ServerLobbyActorComponent::LoadSaveData 0x2B7F4D0 (comp, int* res,
//           -, data**) -> res, the lobby component's vf(+0x50) (vtable
//           0x5B2BE58, the only reference). It adds the entry with the
//           constant (0x2B7F66C) under the owner's actor lock and releases
//           the lock before returning. After the original, for user 2's
//           lobby only, the ONE entry holding the constant gets the assigned
//           number. The list: [comp+0x18] = {entry* data; u32 count; u32 cap},
//           entries of 0xD0, characterNo at +0 (.rsrc readers 0x2C3E126 and
//           the copies .xtls 0x103C3160 / 0x1042AB45 / 0xE4A211A, read only).
//           It runs in UserLogin (2490, 0x2C17881) before the CharacterLobby
//           DataAck 2869 is built (0x2C179E5), and again in the 3093's reload
//           (0x2C3E1CE) before the requested number is checked against it.
//   status  LoadComponentsFromFile 0x20688A0 (shared with the C2-0 probe:
//           identity_probe.cpp owns that hook and calls us after the
//           original). For user 2's login character, *(Status+0x70) (a
//           pointer to the u64) = the assigned number, replacing the second
//           constant (.xtls 0x1042756B). Before 0x2941020, the 2358 and the
//           packer (measured D-53: they all run after the return).
//   check   0x2941020 entry (also through the probe's hook): lobby == Status
//           == assigned, else ERROR and user 2's native logout.
//
// The host keeps 0x7FFFFFFFFFFFFFFF. Nothing here calls the game (only the
// originals through their hooks); writes are aligned 8-byte stores on the
// server thread. Fail safe: if the fix is wanted and not active, or user 2's
// lobby could not be normalised, its LoginUserToFieldServerReq 2490 /
// LoginCharacterToFieldServerReq 3093 never reach the server.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cdmp::session2 {

// Offsets, build 1.0.0.2976 (evidence in identidad.md and the comments above).
namespace idf {
inline constexpr uint64_t kNative = 0x7FFFFFFFFFFFFFFF;   // the constant (0x2B7F66C, .xtls 0x1042755C)
inline constexpr uint64_t kUser2 = 0x7FFFFFFFFFFFF002;    // identidad.md §5.1 (a mod value, not an official id)
inline constexpr uint64_t kOldPuppet = 0x7FFFFFFFFFFFF001;
inline constexpr uint32_t kLobbyOwner = 0x08;     // comp -> its ServerUserActor (0x2B7F649, 0x2C3E107)
inline constexpr uint32_t kLobbyList = 0x18;      // comp -> list (0x2C3E126, .xtls 0x1042AB45)
inline constexpr uint32_t kListCount = 0x08;      // u32 (.xtls 0x1042AB49, 0x103C31FB)
inline constexpr uint32_t kEntryStride = 0xD0;    // (.xtls 0x1042AB62, 0x103C31B2)
inline constexpr uint32_t kEntryNumber = 0x00;    // u64 characterNo (.xtls 0x1042AB6C, 0xE4A214D)
inline constexpr uint32_t kUserComponents = 0x68; // user/actor -> component table
inline constexpr uint32_t kTableLobby = 0x10;     // table -> lobby component (0x2C3E103, .xtls 0x1042AB21)
inline constexpr uint32_t kTableStatus = 0x20;    // table -> Status (0x206A894)
inline constexpr uint32_t kStatusActor = 0x08;    // Status -> its actor
inline constexpr uint32_t kStatusNumber = 0x70;   // Status -> u64* characterNo (0x206A8A5)
inline constexpr uint32_t kStatusNumber3527 = 0x90; // build 3527 Status -> u64* characterNo
inline constexpr uint32_t kActorKey = 0x60;       // u32
inline constexpr uint32_t kActorUser = 0xA0;      // character -> ServerUserActor (.xtls 0x1042AB16)
inline constexpr uint32_t kLockObject = 0x08;     // owner -> its lock object (0x2B7F64D); u32 count at +8
inline constexpr uint32_t kMaxEntries = 64;       // a lobby has a handful of characters
inline constexpr uint32_t kMaxActors = 0x4000;    // logged-in list scanned for uniqueness
inline constexpr uint32_t kCharacterClass = 0xA;  // key >> 28 of characters (users 0x9, NPCs/children 0xB)
}  // namespace idf

// Never -1, 0, the native constant or the old puppet's F001.
bool identity_number_allowed(uint64_t n);

// ---- the lobby list (pure, on the numbers of its entries)
enum class LobbyEdit : uint8_t { Write, Already, Empty, Ambiguous, Unreadable, NotAllowed };
const char* lobby_edit_name(LobbyEdit e);
struct LobbyPlan {
    LobbyEdit what = LobbyEdit::Unreadable;
    uint32_t count = 0;        // entries
    uint32_t index = 0;        // the one to write (Write) or that holds it (Already)
    uint32_t native = 0;       // entries with the constant
    uint32_t assigned = 0;     // entries with the assigned number
};
// Write only if exactly one entry holds the constant and none the assigned
// number (the native add refuses a duplicate number, .xtls 0xE4A214D);
// Already if exactly one holds the assigned number and none the constant.
LobbyPlan plan_lobby(std::span<const uint64_t> numbers, uint64_t assigned);
// The lobby's number for the invariant: the single entry, or the assigned one
// if it is there exactly once and no entry still has the constant.
std::optional<uint64_t> lobby_identity(std::span<const uint64_t> numbers, uint64_t assigned);

// ---- memory (read through safe reads; the one write is guarded)
// The numbers of a lobby component's list; false if unreadable or too long.
bool read_lobby(uintptr_t comp, std::vector<uint64_t>& numbers, uintptr_t& data);
// Plan and, for Write, store the assigned number into that entry and read it back.
LobbyPlan lobby_normalise(uintptr_t comp, uint64_t assigned);
// The lobby component of a ServerUserActor ([[user+0x68]+0x10] with [comp+8] == user), 0 if none.
uintptr_t user_lobby(uintptr_t user);

enum class StatusEdit : uint8_t { Written, Already, NotNative, Unreadable, NotAllowed };
const char* status_edit_name(StatusEdit e);
// actor -> [[actor+0x68]+0x20] = Status ([Status+8] == actor, vtable ==
// status_vtable unless 0) -> u64* [Status+build offset]: the constant becomes
// `assigned`; anything else is left alone. `before` = the value found.
StatusEdit status_normalise(uintptr_t actor, uintptr_t status_vtable, uint64_t assigned, uint64_t& before);
// Pure-layout variant used by the build-profile tests. Only +0x70/+0x90 are accepted.
StatusEdit status_normalise_at(uintptr_t actor, uintptr_t status_vtable, uint64_t assigned, uint64_t& before,
                               uint32_t status_number_offset);

// lobby == status == assigned. why = what failed (empty if ok).
bool identity_invariant(std::optional<uint64_t> lobby, std::optional<uint64_t> status, uint64_t assigned,
                        std::string& why);

// Read only: how many of `count` actor pointers at `data` (the logged-in list
// that 0x2A83F80 searches) other than `except` have characterNo == number.
// nullopt = the list itself is unreadable.
std::optional<uint32_t> actors_with_number(uintptr_t data, uint32_t count, uint64_t number, uintptr_t except);

// ---- the game side
// From the ini: wanted (default 1 with create_session). Before install.
void identity_fix_set_wanted(bool wanted);
bool identity_fix_wanted();
// Byte checks, the lobby hook and the two shared entry hooks (identity_probe.h).
// False = not active (the logins of user 2 are then refused).
bool identity_fix_install();
bool identity_fix_active();
// Network thread, from server_inject: may this Req of user 2 reach the server?
// 2490/3093 are refused while the fix is wanted but not active; the 3093 also
// while user 2's lobby is not normalised or it asks for another number; every
// Req but the 2001 after an identity failure. why = the reason.
bool identity_fix_admit(uint16_t opcode, std::span<const uint8_t> payload, std::string& why);
// Network thread: an identity failure asks for user 2's native logout (once).
bool identity_fix_take_logout(std::string& why);
// C4: a new guest epoch starts clean (its user 2 is a new one).
void identity_fix_new_epoch();
std::string identity_fix_describe();

}  // namespace cdmp::session2
