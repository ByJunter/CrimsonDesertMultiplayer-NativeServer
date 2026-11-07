#pragma once
// The rule of field_leave_guard (field_leave_guard.h), with no game access (tests: tests/optd_field_leave_guard_tests.cpp).
//
// Builds 1.0.0.3527 and 1.0.0.3549 (the same code here; capstone on the PE, and ManagerProbe live on 3549, 2026-11-05):
//   0xE55E320  ServerField's "an actor leaves" (reached by jmp from 0x2D95AD0): every component's vf34 (+0x110) with the field
//   0x23BAFB0  vf34 of the sequencer components (Server 0x5EF4E50, Common 0x5CCDC20, ClientSelf 0x59520A8):
//                manager = field->vf48()                 0x23BAFC6
//                if (manager) manager->vf18(field)       0x23BAFD7, return 0x23BAFDB
//                [this+0x18]->vf18(field)                0x23BAFEF, jmp
//   vf48       the field OWNER's manager (ServerField 0x2D97840, ClientField 0xAAA7A0: by the user key at field+0x180)
//   vf18       unloads the manager (Server 0x2AD40B0: every schedule, faction and sequencer stage, then state +0x6D0 = 0)
// With one player both are the same manager. With two, the guest leaving the host's field unloads the HOST's: ManagerProbe,
// 22:44:58.037, the same millisecond: 0x2AD40B0 on the host's manager (return 0x23BAFDB), then on user 2's (return 0xE55E49F).
// So the host's town NPCs went at every guest logout, and nothing armed the manager again until the host's next trip.
//
// The rule: a character leaving a field unloads its own manager; the owner's too only if it is the owner's character. A
// user's other characters (Damiane, when the host plays her) stay native, and with one player this never fires.

#include <cstddef>
#include <cstdint>

namespace cdmp::session2::flg {

inline constexpr uintptr_t kEntry = 0x23BAFB0, kPoint = 0x23BAFCE, kOwnCall = 0x23BAFDB;
inline constexpr uint32_t kPatch = 6;
// mov r8,[rax] ; mov rdx,rbx: two whole instructions; the function's only branch goes past them (je 0x23BAFDB).
inline constexpr uint8_t kPointBytes[kPatch] = {0x4C, 0x8B, 0x00, 0x48, 0x8B, 0xD3};
inline constexpr uint8_t kEntryBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x02, 0x48, 0x8B,
                                          0xF9, 0x48, 0x8B, 0xCA, 0x48, 0x8B, 0xDA, 0xFF, 0x50, 0x48, 0x48, 0x85, 0xC0, 0x74, 0x0D};
inline constexpr uint8_t kOwnCallBytes[] = {0x48, 0x8B, 0x4F, 0x18, 0x48, 0x8B, 0xD3, 0x48, 0x8B, 0x01};

// Build 1.0.0.3576 (static, 2026-11-06; not yet run): the vf34 (+0x110) of all three sequencer components'
// vtables (by RTTI: Server 0x5EF7040, Common 0x5CCFD18, ClientSelf 0x59541A8) is 0x23BBD00 = 3527's +0xD50, .pdata
// 0x23BBD00..0x23BBD43; kEntryBytes, kPointBytes and kOwnCallBytes are the same at the same offsets.
inline constexpr uintptr_t kEntry3576 = 0x23BBD00, kPoint3576 = 0x23BBD1E, kOwnCall3576 = 0x23BBD2B;
inline constexpr uintptr_t kVtServerComponent3576 = 0x5EF7040;
static_assert(kPoint3576 - kEntry3576 == kPoint - kEntry && kOwnCall3576 - kEntry3576 == kOwnCall - kEntry);

inline constexpr uint32_t kComponentActor = 0x08, kComponentOwnManager = 0x18;
inline constexpr uint32_t kActorKey = 0x60, kActorUser = 0xA0, kUserKey = 0x60;
inline constexpr uint32_t kFieldOwnerKey = 0x180;
inline constexpr uint32_t kManagerActor = 0x18;

// At kPoint: ctx.rax the owner's manager, ctx.rdi the component, ctx.rbx the field. own: [component+0x18]. leaver_user: the user
// key of the component's actor. field_owner: the field's. Anything unread is 0, and stays native. own_call: the
// build's kOwnCall.
template <class Context>
bool skip_owner_unload(Context& ctx, uintptr_t base, uintptr_t own, uint32_t leaver_user, uint32_t field_owner,
                       uintptr_t own_call = kOwnCall) noexcept {
    if (!own || !ctx.rax || ctx.rax == own) return false;
    if (!leaver_user || !field_owner || leaver_user == field_owner) return false;
    ctx.rip = base + own_call;
    return true;
}

}  // namespace cdmp::session2::flg
