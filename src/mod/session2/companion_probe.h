#pragma once
// Option D, companions step A (docs/research/pendientes/companeros.md §2, §4,
// "A. Creación del mercenario: primero identificar la condición exacta" and
// "Revisión"): a READ-ONLY probe on the host's CLIENT of
//
//   1. the creation of a user-owned actor (a player's character or one of its
//      children: slots 1/2/3, record types 4/5/6 in capture 112627), to name
//      the branch that rejects or accepts user 2's child:
//        create   0x8AF580 CreateOwnedActor(mgr, out, u8 type, u32 key,
//                 u32 owner, u8 index, u64, bool) -> out: entry/return, the
//                 record's fields and the output actor (valid or not)
//        find     0x8AF61B mid: the owner lookup 0x8ADFE0's attacher (user, ok)
//                 (read here, not by hooking 0x8ADFE0: 300+ callers)
//        factory  0x8AF696 mid: RAX = the new child (0 = factory failed)
//        map      0x8AF6D7 mid: *RAX = the map insert's result (0 = inserted)
//        confirm  0x206B2D0 ConfirmSlot(user, int* res, child, u8 slot) -> res:
//                 entry/return, user/main/child and their states, the result,
//                 and inside it (mid hooks, read only):
//                 0x206B39E EAX slot error, 0x206B4B5 AL main acquired,
//                 0x206B535 main and its +0x5E/+0x4A, 0x206B5DA R13D attach result
//      one "CompCreate" line per attempt (owner key, child key, type, slot,
//      every result); the game's own "Actor 생성에 실패했습니다" line (game error
//      hook) carries the same child key.
//   2. the actions of remote option-D actors (user 2's character and its
//      children: owner key [actor+0x90] = user 2's user key):
//        restore  0x8646FC (after the prologue of 0x8646D0, whose first 16
//                 bytes horse_diag checks and hooks) and 0x864B1D (its single
//                 exit): restore byte [[CC+0x40]+0x30], both layers' current
//                 action (+0x10), playback (+0x18) and chart (+0x110), before
//                 and after
//        crash    0x232D5C0 entry (rcx = CC, dl = layer index, r8 = copy of the
//                 layer's +0x00..+0x18): once per actor, the actor that is about
//                 to read [[r8+0x10]+0xE4] with [r8+0x10] null (the crash at
//                 exe+0x232D5FF) and its layers. Nothing is prevented.
//   3. counters: ScopeSenderAck 2042 entries (0xCB4160).
//
// Every point is checked against its bytes (capstone over the read-only copy of
// build 2976) and goes in on its own. Entry hooks call the original with the
// same registers and stack and return its result; mid hooks only read. No game
// function is called from any hook.

#include <cstdint>
#include <string>

namespace cdmp::session2 {

namespace cmp {
inline constexpr uint32_t kActorKey = 0x60;        // client actor -> u32 key
inline constexpr uint32_t kActorInfo = 0x88;       // client actor -> static info; byte +1 == 1: main (0x206B3FA)
inline constexpr uint32_t kActorOwnerUser = 0x90;  // client actor -> u32 owning user key (offsets.h)
inline constexpr uint32_t kActorState = 0x5E;      // u16 state flags (4 / 0x10 tested by 0x206B345 / 0x206B539)
inline constexpr uint32_t kActorReady = 0x4A;      // u8, tested next to it
inline constexpr uint32_t kUserMain = 0xD0;        // user actor -> its main character (0x206B413)
inline constexpr uint32_t kCcActor = 0x08;         // control component -> actor
inline constexpr uint32_t kCcRestore = 0x40;       // CC -> restore data; its byte +0x30 (0x864743)
inline constexpr uint32_t kRestoreByte = 0x30;
inline constexpr uint32_t kCcLayer0 = 0x88;        // CC -> layer 0, +0x90 layer 1 (0x232D51E)
inline constexpr uint32_t kCcPending = 0xA8;       // u8 pending actions flag (horse_diag)
inline constexpr uint32_t kLayerCurrent = 0x10;    // what 0x232D5C0 reads as [r8+0x10]
inline constexpr uint32_t kLayerPlayback = 0x18;
inline constexpr uint32_t kLayerChart = 0x110;

// RestoreActionsFromLoadData 0x8646D0(CC, int* res), .rsrc. Its first 16 bytes
// are horse_diag's (checked and mid-hooked there), so the "before" point is
// after the prologue: vmovaps x2 ; mov rsi,rdx ; mov r13,rcx (RCX = CC, RDX = res).
// Shared by this probe (host) and the P8 guest's diagnostic (guest_actor_diag.h):
// never both in one process (guest=1 excludes create_session).
inline constexpr uintptr_t kRestoreSite = 0x8646F2;
inline constexpr uint8_t kRestoreSiteBytes[] = {0xC5, 0xF8, 0x29, 0x70, 0xB8, 0xC5, 0xF8, 0x29,
                                                0x78, 0xA8, 0x48, 0x8B, 0xF2, 0x4C, 0x8B, 0xE9};
inline constexpr uintptr_t kRestoreHook = kRestoreSite + 10;  // 0x8646FC
// mov rax,[r13+0x40] ; cmp [rax+0x30],r14b ; jne: the restore byte, read under the lock.
inline constexpr uintptr_t kRestoreLock = 0x86473F;
inline constexpr uint8_t kRestoreLockBytes[] = {0x49, 0x8B, 0x45, 0x40, 0x44, 0x38, 0x70, 0x30, 0x75, 0x59};
// The single exit (every path jumps to 0x864B1D): mov [rsi],r14d ; mov rax,rsi ;
// lea r11,[rsp+0x160]. R13 = CC (written only at 0x8646FF), RSI = res (reloaded
// from the home slot at 0x864AE0 after the layer loop).
inline constexpr uintptr_t kRestoreExit = 0x864B1A;
inline constexpr uint8_t kRestoreExitBytes[] = {0x44, 0x89, 0x36, 0x48, 0x8B, 0xC6, 0x4C,
                                                0x8D, 0x9C, 0x24, 0x60, 0x01, 0x00, 0x00};
inline constexpr uintptr_t kRestoreExitHook = kRestoreExit + 3;  // 0x864B1D
// The call of RestartLayerAction 0x863880 and its result: lea r8,[rsp+0x50] ;
// lea rdx,[rbp+0xa0] ; mov rcx,r13 ; call 0x863880 ; mov eax,[rax] ; test ; je ;
// mov [rsi],eax. With restore byte != 0 the exit's [rsi] is its result.
inline constexpr uintptr_t kRestartCall = 0x864AEA;
inline constexpr uint8_t kRestartCallBytes[] = {0x4C, 0x8D, 0x44, 0x24, 0x50, 0x48, 0x8D, 0x95, 0xA0, 0x00,
                                                0x00, 0x00, 0x49, 0x8B, 0xCD, 0xE8, 0x82, 0xED, 0xFF, 0xFF,
                                                0x8B, 0x00, 0x85, 0xC0, 0x74, 0x04, 0x89, 0x06};
// RestoreActionsFromLoadData's reads (0x8647ED..0x86487D): layer i = [CC+0x88+8i],
// its chart +0x110 and the restored hash +0x3C (read only with a chart);
// 0x863880 looks the hash up in the layer's chart, then in [CC+0x70+8i] (0x8638EA).
inline constexpr uint32_t kLayerRestoreHash = 0x3C;
inline constexpr uint32_t kCcDefaultChart0 = 0x70;  // +0x78 for layer 1
}  // namespace cmp

// Key class, bits 28-29 of an actor key (0x8AF5D2 tests 0x10000000 for users):
// 1 user (0x90...), 2 player character (0xA0...), 3 other actor (0xB0...:
// children and NPCs alike), 0 none of those.
enum class KeyClass : uint8_t { Other = 0, User = 1, Character = 2, Actor = 3 };
KeyClass comp_key_class(uint32_t key);
const char* comp_key_class_name(uint32_t key);

// Whose actor it is, from its owner user key.
enum class CompWho : uint8_t { None, Host, User2, OtherUser };
CompWho comp_who(uint32_t owner, uint32_t host_key, uint32_t user2_key);
const char* comp_who_name(CompWho w);

struct CompLayer {
    bool present = false;  // the layer pointer itself
    uintptr_t current = 0, playback = 0, chart = 0;
    // A chart and no current action: what 0x232D5C0 dereferences.
    bool would_crash() const { return present && chart && !current; }
};

// What a control component says about its actor and its action state.
struct CompControl {
    uintptr_t actor = 0, actor_vt = 0, cc_vt = 0, info = 0;
    uint32_t key = 0, owner = 0;
    uint8_t info_bytes[8] = {};
    bool info_ok = false;
    int restore = -1;  // [[CC+0x40]+0x30]; -1 unreadable
    int pending = -1;  // CC+0xA8
    CompLayer layer[2];
};

// Reads it all with checked reads; false if the CC or its actor is unreadable.
bool comp_read_control(uintptr_t cc, CompControl& out);

// Installs the points that match this build; false = none (only a warning).
bool companion_probe_install();
bool companion_probe_active();
std::string companion_probe_describe();

}  // namespace cdmp::session2
