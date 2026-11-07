#pragma once
// Option D, C2-0 (docs/research/pendientes/identidad.md §5.2, §6 "Observación
// mínima"): a READ-ONLY probe of user 2's characterNo during its login, to
// decide whether normalising it after the component-load loop returns is a
// root fix. The host's server gives both characters 0x7FFFFFFFFFFFFFFF, and
// the Status load writes that constant itself (.xtls 0x1042756B, after the
// common load 0x1791980), so the question is whether anything consumes or
// publishes the provisional number between the Status load and the return of
// the loop. Observation points, all in .rsrc (never .xtls):
//
//   load      0x20688A0 LoadComponentsFromFile(actor, res*, store, name,
//             u32 slot) -> res: entry and return, the characterNo before and
//             after, the result.
//   component 0x20693EB, mid hook right after each component's vf(+0x50)
//             call: component (R15), its vtable, the result (RAX = res*), the
//             characterNo at that moment. Gives the ORDER of the loads
//             relative to Status (vtable 0x5B203F0, +0x50 = 0x293FE70).
//   owner     0x2AE1010(component, u16* type, u64* no, u64* no2): builds an
//             owner of type 2 copying *Status+0x70; every call inside user
//             2's load or its login window, with the caller's address.
//   phases    0x2941020(obj, res*, ?, ?) -> res, actor [obj+8]: the
//             post-load login phases (called from .xtls 0x1042AC0C).
//   ack       0x28F31F0(actor): the login path that writes CharacterStatAck
//             2358 with the characterNo pointer (0x206A890 at 0x28F3489).
//   packer    0x17908C0(Status, archive): writes **(Status+0x70); filtered
//             to the login window first (it can be frequent).
//   version   0x1791B98 (server) / 0xB74302 (client), mid hooks on the two
//             writes of global 0x6C8CD98 (_currentRestoreCodeVersionXXX):
//             the old and the new value.
//
// Entry hooks only call the original with the same arguments and return its
// result; mid hooks only read registers and memory. Every point is checked
// against its bytes and goes in on its own; a point whose code differs stays
// off. "User 2's character": actor+0xA0 == session 2's validated user (or the
// actor IS that user, its lobby load); while session 2's user cannot be read
// yet, a user whose key is not the host's, said so in the line. During user
// 2's login window (or while its user exists) a load whose actor has no
// identified user is logged too, as "unidentified": whether the character's
// user link is set before its save loads is not proven. The host's character
// gets at most two comparison lines per point.

#include <cstdint>
#include <string>

namespace cdmp::session2 {

namespace idp {
inline constexpr uint32_t kActorComponents = 0x68;   // actor -> component table (0x206A890, 0x2AE1037)
inline constexpr uint32_t kTableStatus = 0x20;       // table -> Status component (0x206A894)
inline constexpr uint32_t kStatusCharacterNo = 0x70; // Status -> u64* characterNo (0x206A8A5)
inline constexpr uint32_t kStatusCharacterNo3527 = 0x90; // build 3527 Status -> u64* characterNo
inline constexpr uint32_t kComponentOwner = 0x08;    // component -> actor (0x2AE102A, 0x17919A5)
inline constexpr uint32_t kActorUser = 0xA0;         // character -> ServerUserActor (clock gate, P9 §2.2)
inline constexpr uint32_t kKey = 0x60;               // actor/user -> u32 key (offsets_s2.h)
}  // namespace idp

enum class IdWho { Other, Host, User2, User2ByKey };

// The users as the server holds them now (0 = not readable).
struct IdUsers {
    uintptr_t host = 0;
    uint32_t host_key = 0;
    uintptr_t user2 = 0;
};

// The user an actor belongs to: the actor itself when it is a ServerUserActor
// (its vtable is user_vtable: the lobby load), else [actor+0xA0]. 0 = none.
uintptr_t identity_owner(uintptr_t actor, uintptr_t user_vtable, bool& self);

// Whose actor it is. User2ByKey only while session 2's user is not readable
// (users.user2 == 0): a ServerUserActor whose key is not the host's.
IdWho identity_who(uintptr_t actor, const IdUsers& users, uintptr_t user_vtable, bool& self);

// Status -> u64* characterNo. Unsupported/test executables deliberately use
// the legacy layout so the pure fake-memory tests remain deterministic.
constexpr uint32_t identity_status_number_offset_for_build(bool build_3527) noexcept {
    return build_3527 ? idp::kStatusCharacterNo3527 : idp::kStatusCharacterNo;
}
uint32_t identity_status_number_offset() noexcept;

// characterNo of a server actor: *[[[actor+0x68]+0x20]+build offset].
// False if a link is unreadable or null.
bool identity_character_no(uintptr_t actor, uint64_t& no);
// Pure-layout variant used by the build-profile tests. Only the two verified
// Status layouts (+0x70 and +0x90) are accepted.
bool identity_character_no_at(uintptr_t actor, uint64_t& no, uint32_t status_number_offset);

// The actor that owns a Status component, [status+8], only if that actor's
// Status is this component; 0 otherwise.
uintptr_t identity_status_actor(uintptr_t status);

// C2 (identity_fix.h): the entry hooks on 0x20688A0 and 0x2941020 have one
// owner, this module, whether the probe is on or not (two inline hooks on the
// same entry would chain through each other's trampolines). The fix gets
// these calls from them; the probe's logging runs only with the probe on.
struct IdentityFixCalls {
    // 0x20688A0, right after the original returned (r = its int* result) and
    // before the probe's "return" line, so the probe shows the fixed number.
    void (*after_load)(uintptr_t actor, const int* r) noexcept = nullptr;
    // 0x2941020, before the original and before the probe's "enter" line
    // (obj = the character's Status, its actor at [obj+8]).
    void (*before_phases)(uintptr_t obj) noexcept = nullptr;
};
// Installs only those two entry hooks (byte-checked; nothing if the probe
// already has them) and routes them to `calls`. True only if both are on;
// otherwise the calls are cleared again.
bool identity_shared_hooks_install(const IdentityFixCalls& calls);

// P8 session 25 (guest_save_host.h): the guest's own save shares the
// 0x20688A0 entry hook too (only that one).
struct GuestSaveLoadCalls {
    // Before the original, after the probe's "enter" line, on the loading
    // thread. True = the original is NOT called: the hook returns `res`, which
    // the callee has set to an error code.
    bool (*before)(uintptr_t actor, int* res, uintptr_t store, uintptr_t name, uint32_t slot) noexcept = nullptr;
    // Right after the original (before the C2 fix's after_load), only when
    // the original ran.
    void (*after)(uintptr_t actor, const int* r, uint32_t slot) noexcept = nullptr;
};
// Installs the 0x20688A0 entry hook if nobody has yet (byte-checked) and
// routes it to `calls`. False: the hook is not on (the calls are cleared).
bool identity_load_hook_share(const GuestSaveLoadCalls& calls);

// Installs the points that match this build; false = none (only a warning:
// it is diagnostics). The two shared entry hooks are reused if the fix
// installed them first.
bool identity_probe_install();
bool identity_probe_active();
std::string identity_probe_describe();

}  // namespace cdmp::session2
