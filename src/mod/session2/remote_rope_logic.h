#pragma once
// P8 session 25, the grappling hook's blue rope ("RemoteCatch") of the OTHER player
// (docs/research/s25/gancho-cuerda.md): the pure logic of remote_rope.cpp, with no game access, so the tests can run
// it. Build 1.0.0.2976 only.
//
// The faults [V, S25 captures + decompile]:
//   1. RemoteCatchResultAck 2763 (ToNear, both clients) names the hooked tree by its scene id (16 bytes, the same on
//      every PC) AND by the THROWER's physics-body handle (target block +0x18, from [[obj+0x58]+0xE0]->vf70 on the
//      thrower's client, 0x5947C7..0x59484E), with the hook point in that body's frame. The observer resolves the
//      handle against its own physics ([WorldSystem+0xE8]->vf3B0, 0x5A5370): another body, a rope to a point 189 m
//      or 11.9 km away. Same tree, two handles: 0x800000000100069F (guest) / 0x8000000001000C2C (host).
//   2. The release of a rope to a tree is a client frame event of the thrower only (0x7F8F40); the server
//      (0x27D9110) sends ReleaseRemoteCatchResultAck 2471 only for ACTOR targets (0x2903950, 0x2902DC0). The
//      observer never releases: the rope hangs, its slot stays busy and every later 2763 of that thrower fails in
//      0x1808100 ("slot not empty") -> "RemoteCatch에 실패" from exe+0x592536.
//
// [session2] remote_rope = 0 (default) | probe | 1. BOTH PCs (F1, F3, the 2471 probe); the HOST also F2:
//   F1 (client, entry of RemoteCatchComponent_Apply 0x592340, one caller 0xB5BB7B): for a thrower NOT controlled
//      here (byte [actor+0xC8] == 0), kind 0, target class 2, handle != -1 and a scene id: this PC's handle of the
//      same scene object, by the thrower's own mapping (ResolveUuidLookup 0x1851010, [att+0x10] / [att+8] checked as
//      0x5A5370 does, [obj+0x58], [+0x15] != 1, [+0xE0]->vf70, the attacher's reference released by its dtor
//      0x1434880). 1: written into the handler's local target block (+0x18, the copy 0x5923AD reads); unresolved:
//      passed through, counted. probe: computed and logged. The host's OWN throws are computed too (the check:
//      there the computed handle must equal the packet's), never written. Both bodies' transforms (vf3B0) logged.
//   F3 (the same hook, before F1): a thrower NOT controlled here whose slot is busy (type != 6) when its 2763
//      arrives: the server's own slot passed 0x1807A40, so this one is stale (a release that never came, or a failed
//      apply that left it filled). 1: ONLY when the stale slot holds the measured case (kind 0, class 2, no target
//      actor: a tree), the native release 0x5A7A10(comp, slot, 2, 0, 1) first, as the 2471 handler calls it (on the
//      game's main thread it releases at once, 0x5A7BC0), after the component's vtable and the thrower's lock object
//      were checked. A stale slot holding an ACTOR target is only logged: 0x5A7BC0 -> 0x1807C80 would zero the
//      target's holder key [[target+0x68]+0x78]+0x18 without checking it is this thrower's (the native 2471 path only
//      does it after the server's check in 0x2903950). probe: logged only.
//   F2 (server, HOST only, entry of ServerFrameEventReleaseRemoteCatch::vf1 0x27D9110, vtable slot 0x5B1D858): the
//      server slot is read before the original; after it, with err 0 and a released target the server leaves
//      without a 2471 (kind 0, class 2, target actor 0), the native writer 0x29688C0 is called with a
//      ToNear(thrower) finder exactly as 0x2903950 does (0x2903A79..0x2903AB2): both clients get the release through
//      the native Ack path (0xB5BD60 -> 0x5A7A10 on the main thread). probe: "WOULD SEND" only.
//      The LAUNCH (1F4EF471) frees the server slot without that frame event: the action change calls the server
//      component's vf140 0x1807390, which sends a 2471 only for actor targets (its target's vf148 0x2903950). F2 also
//      hooks the entry of 0x1807390 (server component only, by its vtable): a tree slot busy before the original and
//      empty after gets the same writer call (reason 2, as 0x1807390 passes it). The writer is called where the game
//      itself calls it for an actor target (inside 0x1807390, on the server's thread). One line per launch: the
//      release frame events that follow and find the slot empty are folded into it (LaunchFold, counted).
//   2471 probe (client, entry of ReleaseRemoteCatchResultAck::vf2 0xB5BD60, vtable slot 0x560BA60): log-only, the
//      slot type / state before and after the native handler (the thrower's duplicate, measured).
// probe is not free of side effects in the game: the lookup takes the scene manager's lock and a reference (given
// back by the dtor, which may destroy the object if it held the last one), vf70 / vf3B0 are called. They are the
// game's own read paths (native-equivalent reads); no game state is written by the mod. The host's own throws are
// computed only until kOwnChecks consecutive matches. A fault caught inside any of these calls switches that call off
// for the session and that 2763 is not applied (the native error), so the same stack does not lock again at once.

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "mod/session2/user2_funnels_logic.h"

namespace cdmp::session2::rrp {

using u2f::Mode;
using u2f::mode_name;
using u2f::parse_mode;

// ---------------------------------------------------------------- build 1.0.0.2976: the client apply

// RemoteCatchComponent_Apply(comp, &err, &slot, &kind, &3 bytes, &target, &hash, &speed, u32, &name):
//   48 89 5C 24 08 mov [rsp+8],rbx | 48 89 74 24 10 mov [rsp+0x10],rsi | 48 89 7C 24 18 mov [rsp+0x18],rdi |
//   4C 89 74 24 20 mov [rsp+0x20],r14 | 55 push rbp | 48 8D 6C 24 D9 lea rbp,[rsp-0x27] | 48 81 EC F0 00 00 00.
inline constexpr uintptr_t kApply = 0x592340;
inline constexpr uint8_t kApplyHead[33] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48,
                                           0x89, 0x7C, 0x24, 0x18, 0x4C, 0x89, 0x74, 0x24, 0x20, 0x55, 0x48,
                                           0x8D, 0x6C, 0x24, 0xD9, 0x48, 0x81, 0xEC, 0xF0, 0x00, 0x00, 0x00};
inline constexpr uint32_t kApplyBoundaries[] = {5, 10, 15, 20, 21, 26, 33};
// Its only caller (xrefs): RemoteCatchResultAck::vf2 0xB5B710, `call 0x592340`; returns to 0xB5BB80.
inline constexpr uintptr_t kApplyCall = 0xB5BB7B;
inline constexpr uint8_t kApplyCallBytes[5] = {0xE8, 0xC0, 0x67, 0xA3, 0xFF};
inline constexpr uintptr_t kApplyReturn = 0xB5BB80;
// The copy of the target block this module writes into (read-only check): r9 = argument 6 ([rbp+0x5F]), +0 class,
// +4 actor key, +8 scene id (16 bytes), +0x18 handle.
inline constexpr uintptr_t kInfoCopy = 0x59238F;
inline constexpr uint8_t kInfoCopyBytes[38] = {
    0x4C, 0x8B, 0x4D, 0x5F,              // 0x59238F mov r9,[rbp+0x5F]          (argument 6: &target)
    0x41, 0x0F, 0xB6, 0x01,              // 0x592393 movzx eax,byte [r9]       (+0 class)
    0x88, 0x45, 0xCF,                    // 0x592397 mov [rbp-0x31],al
    0x45, 0x8B, 0x41, 0x04,              // 0x59239A mov r8d,[r9+4]            (+4 actor key)
    0x44, 0x89, 0x45, 0xD3,              // 0x59239E mov [rbp-0x2D],r8d
    0xC4, 0xC1, 0x78, 0x10, 0x41, 0x08,  // 0x5923A2 vmovups xmm0,[r9+8]       (+8 scene id)
    0xC5, 0xF8, 0x11, 0x45, 0xD7,        // 0x5923A8 vmovups [rbp-0x29],xmm0
    0x49, 0x8B, 0x41, 0x18,              // 0x5923AD mov rax,[r9+0x18]         (+0x18 handle)
    0x48, 0x89, 0x45, 0xE7,              // 0x5923B1 mov [rbp-0x19],rax
};
inline constexpr int kArgTarget = 6;  // [entry rsp + 8 * 6]
// The target block (the handler's local, 0x4C bytes, filled by 0x1806940 from the packet).
inline constexpr uint32_t kInfo_Class = 0x00;   // u8: 0 actor+bone, 1 actor body, 2 body / scene object
inline constexpr uint32_t kInfo_Actor = 0x04;   // u32 target actor key
inline constexpr uint32_t kInfo_Scene = 0x08;   // 16 bytes: u64 object id, u64 level id
inline constexpr uint32_t kInfo_Handle = 0x18;  // u64 physics-body handle (-1: none)
inline constexpr uint32_t kInfo_Point = 0x24;   // vec3 in that body's frame
inline constexpr uint64_t kNoHandle = ~uint64_t{0};
inline constexpr uint8_t kClassBody = 2;
inline constexpr uint8_t kKindNormal = 0;

// The thrower's mapping, scene id -> handle (0x5947C7..0x59484E), mirrored (read-only check, never patched):
inline constexpr uintptr_t kMapSite = 0x5947C7;
inline constexpr uint8_t kMapSiteBytes[136] = {
    0x48, 0x8B, 0x05, 0xC2, 0x49, 0x7D, 0x06,  // 0x5947C7 mov rax,[0x146D69190]   WorldSystem
    0x48, 0x8B, 0x88, 0xC8, 0x00, 0x00, 0x00,  // 0x5947CE mov rcx,[rax+0xC8]
    0x48, 0x8B, 0x09,                          // 0x5947D5 mov rcx,[rcx]
    0x4C, 0x8D, 0x45, 0xA8,                    // 0x5947D8 lea r8,[rbp-0x58]        &scene id
    0x48, 0x8D, 0x55, 0x60,                    // 0x5947DC lea rdx,[rbp+0x60]       &attacher
    0x48, 0x8B, 0x89, 0x80, 0x00, 0x00, 0x00,  // 0x5947E0 mov rcx,[rcx+0x80]       the manager
    0xE8, 0x24, 0xC8, 0x2B, 0x01,              // 0x5947E7 call 0x1851010           ResolveUuidLookup
    0x90,                                      // 0x5947EC nop
    0x48, 0x8B, 0x55, 0x68,                    // 0x5947ED mov rdx,[rbp+0x68]       obj = [att+8]
    0x48, 0x8B, 0x4A, 0x58,                    // 0x5947F1 mov rcx,[rdx+0x58]       body
    0x48, 0x85, 0xC9,                          // 0x5947F5 test rcx,rcx
    0x74, 0x06,                                // 0x5947F8 jz
    0x80, 0x79, 0x15, 0x01,                    // 0x5947FA cmp byte [rcx+0x15],1
    0x75, 0x09,                                // 0x5947FE jnz
    0x80, 0xBA, 0xA4, 0x00, 0x00, 0x00, 0x00,  // 0x594800 cmp byte [rdx+0xA4],0
    0x74, 0x21,                                // 0x594807 jz
    0x48, 0x85, 0xC9,                          // 0x594809 test rcx,rcx
    0x74, 0x1C,                                // 0x59480C jz
    0x80, 0x79, 0x15, 0x01,                    // 0x59480E cmp byte [rcx+0x15],1
    0x74, 0x16,                                // 0x594812 jz
    0x48, 0x8B, 0x89, 0xE0, 0x00, 0x00, 0x00,  // 0x594814 mov rcx,[rcx+0xE0]
    0x48, 0x85, 0xC9,                          // 0x59481B test rcx,rcx
    0x74, 0x0A,                                // 0x59481E jz
    0x48, 0x8B, 0x01,                          // 0x594820 mov rax,[rcx]
    0xFF, 0x50, 0x70,                          // 0x594823 call [rax+0x70]          the handle
    0x48, 0x89, 0x45, 0xB8,                    // 0x594826 mov [rbp-0x48],rax
    0x48, 0x8D, 0x05, 0x7F, 0x46, 0x24, 0x05,  // 0x59482A lea rax,SyncSceneObjectDataAttacher vtable 0x57D8EB0
    0x48, 0x89, 0x45, 0x60,                    // 0x594831
    0x48, 0x8D, 0x05, 0x04, 0xF0, 0xFD, 0x04,  // 0x594835 lea rax,0x5573840
    0x48, 0x89, 0x45, 0x60,                    // 0x59483C
    0x4C, 0x8D, 0x3D, 0x01, 0x16, 0x19, 0x05,  // 0x594840 lea r15,ScopeAttacherBase vtable 0x5725E48
    0x4C, 0x89, 0x7D, 0x60,                    // 0x594847 mov [rbp+0x60],r15
    0x48, 0x8D, 0x4D, 0x60,                    // 0x59484B lea rcx,[rbp+0x60]      then call 0x1434880 (the dtor)
};
inline constexpr uintptr_t kWorldSystem = 0x6D69190;  // global WorldSystem*
inline constexpr uint32_t kWs_SceneRoot = 0xC8;       // [[ws+0xC8]] + 0x80 = the scene-object manager
inline constexpr uint32_t kSceneRoot_Mgr = 0x80;
inline constexpr uint32_t kWs_Physics = 0xE8;         // [ws+0xE8]->vf3B0(handle, &xf) (0x5A5370)
inline constexpr uint32_t kPhysics_vfTransform = 0x3B0;
inline constexpr uintptr_t kResolve = 0x1851010;
inline constexpr uint8_t kResolveHead[23] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x48, 0x89,
                                             0x54, 0x24, 0x10, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30};
inline constexpr uintptr_t kAttDtor = 0x1434880;
inline constexpr uint8_t kAttDtorHead[24] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48,
                                             0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x80, 0x79, 0x10, 0x00, 0x74, 0x74};
inline constexpr uintptr_t kScopeAttacherBaseVtbl = 0x5725E48;
// The attacher 0x18510F0 fills (0x20 bytes): +0 vtable, +8 object, +0x10 u8 reference taken, +0x11 u8, +0x18 0.
inline constexpr size_t kAttSize = 0x20;
inline constexpr uint32_t kAtt_Obj = 0x08;
inline constexpr uint32_t kAtt_Ref = 0x10;
inline constexpr uint32_t kObj_Body = 0x58;
inline constexpr uint32_t kBody_Gone = 0x15;  // u8 == 1: not usable
inline constexpr uint32_t kBody_Phys = 0xE0;
inline constexpr uint32_t kPhys_vfHandle = 0x70;

// The component (Common/Client/ServerRemoteCatchActorComponent, the same layout on both sides).
inline constexpr uint32_t kActor_Comps = 0x68;
inline constexpr uint32_t kComps_RemoteCatch = 0x78;
inline constexpr uint32_t kComp_Owner = 0x08;
inline constexpr uint32_t kActor_Key = 0x60;
inline constexpr uint32_t kActor_Control = 0xC8;  // u8: this client controls it
inline constexpr uint32_t kSlotBase = 0x20;       // the slot's target: 0x1808100 / 0x1807C80
inline constexpr uint32_t kSlotStride = 0x58;
inline constexpr uint32_t kSlot_Kind = 0x00;      // u8, 6 = empty (0x10106)
inline constexpr uint32_t kSlot_Class = 0x08;
inline constexpr uint32_t kSlot_Actor = 0x0C;
inline constexpr uint32_t kSlot_Scene = 0x10;
inline constexpr uint32_t kSlot_Handle = 0x20;
inline constexpr uint8_t kSlotEmpty = 6;
inline constexpr uint32_t kStateBase = 0x9BC;     // u8 state per slot: 2 flight, 3 attached ...
inline constexpr uint32_t kFlightBase = 0xA94;    // f32 flight seconds (distance / speed, 0x5A12A0)
inline constexpr uint32_t kStateStride = 0x1A0;
inline constexpr int kSlots = 2;
inline constexpr uintptr_t slot_target(uintptr_t comp, uint8_t slot) { return comp + kSlotBase + slot * kSlotStride; }
inline constexpr uintptr_t slot_state(uintptr_t comp, uint8_t slot) { return comp + kStateBase + slot * kStateStride; }
inline constexpr uintptr_t slot_flight(uintptr_t comp, uint8_t slot) { return comp + kFlightBase + slot * kStateStride; }

// The release request 0x5A7A10(comp, slot, reason, p4, p5): on the main thread (dword 0x6A652D4) at once
// (0x5A7BC0), elsewhere a request at comp+0xC90 that the component's update 0x58B8F0 runs.
inline constexpr uintptr_t kRelease = 0x5A7A10;
inline constexpr uint8_t kReleaseHead[24] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
                                             0x74, 0x24, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x50};
inline constexpr uintptr_t kReleaseThreadCheck = 0x5A7A7D;  // cmp [0x146A652D4],eax ; jnz deferred
inline constexpr uint8_t kReleaseThreadCheckBytes[6] = {0x39, 0x05, 0x51, 0xD8, 0x4B, 0x06};
inline constexpr uintptr_t kMainThreadId = 0x6A652D4;
inline constexpr uint8_t kStaleReason = 2;  // the tick's own releases (0x59D470) use reason 2
// Before F3 calls 0x5A7A10: the component must be the client's (its vtable) and the thrower's lock object (the one
// cdmp_Actor_Lock 0x393650 takes: [[comp+8]+8], its vf18 / vf20 / vf30 / vf40) must have a vtable in the game's code.
inline constexpr uintptr_t kClientRemoteCatchVtbl = 0x55890D8;  // pa::ClientRemoteCatchActorComponent
inline constexpr uint32_t kActor_LockObj = 0x08;
inline constexpr uint32_t kLockVf[] = {0x18, 0x20, 0x30, 0x40};
// The error 0x592340 itself returns when the apply fails (DAT_146cf70a4, filled at run time; 0xD661CF29 in S25's
// "RemoteCatchResultAck처리에 실패" lines): written into *err when a fault stops this 2763.
inline constexpr uintptr_t kApplyFailedErr = 0x6CF70A4;
inline constexpr uint32_t kApplyFailedErrSeen = 0xD661CF29;

// ---------------------------------------------------------------- the 2471 handler (probe only)

// ReleaseRemoteCatchResultAck::vf2 (this, &err, packet): 48 89 5C 24 08 | 55 | 56 | 57 | 41 54 | 41 55 | 41 56 |
// 41 57 | 48 8B EC | 48 81 EC 10 01 00 00. Referenced by its vtable slot 0x560BA60 only.
inline constexpr uintptr_t k2471Handler = 0xB5BD60;
inline constexpr uint8_t k2471HandlerHead[26] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41,
                                                 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8B,
                                                 0xEC, 0x48, 0x81, 0xEC, 0x10, 0x01, 0x00, 0x00};
inline constexpr uint32_t k2471HandlerBoundaries[] = {5, 6, 7, 8, 10, 12, 14, 16, 19, 26};
inline constexpr uintptr_t k2471HandlerSlot = 0x560BA60;
// Its release call (the arguments F3 mirrors): rcx = [[actor+0x68]+0x78], edx = slot, r8d = reason, r9d = 0,
// [rsp+0x20] = 1, call 0x5A7A10.
inline constexpr uintptr_t k2471Release = 0xB5BF44;
inline constexpr uint8_t k2471ReleaseBytes[30] = {
    0x48, 0x8B, 0x49, 0x68,              // mov rcx,[rcx+0x68]
    0xC6, 0x44, 0x24, 0x20, 0x01,        // mov byte [rsp+0x20],1
    0x45, 0x33, 0xC9,                    // xor r9d,r9d
    0x44, 0x0F, 0xB6, 0x45, 0x50,        // movzx r8d,byte [rbp+0x50]  (reason)
    0x0F, 0xB6, 0x55, 0x58,              // movzx edx,byte [rbp+0x58]   (slot)
    0x48, 0x8B, 0x49, 0x78,              // mov rcx,[rcx+0x78]
    0xE8, 0xAE, 0xBA, 0xA4, 0xFF,        // call 0x5A7A10
};
// The packet seen at the handler's entry (third argument p): [p+0x10] u16 total, [p+0x18] buffer; buffer+3 u16 payload
// length, buffer+5 payload = u32 thrower key, u8 slot, u8 reason (0x29688C0's order).
inline constexpr uint32_t kPkt_Total = 0x10;
inline constexpr uint32_t kPkt_Buffer = 0x18;
inline constexpr uint32_t kBuf_Len = 3;
inline constexpr uint32_t kBuf_Payload = 5;
inline constexpr uint16_t k2471Payload = 6;
inline constexpr uint16_t kOp2471 = 2471;

// ---------------------------------------------------------------- build 1.0.0.2976: the server release

// ServerFrameEventReleaseRemoteCatch::vf1 (this, &err, event, ?, &actor holder):
//   48 89 5C 24 08 | 48 89 74 24 10 | 48 89 7C 24 20 | 55 | 41 54 | 41 55 | 41 56 | 41 57 | 48 8D 6C 24 D1.
inline constexpr uintptr_t kSrvRelease = 0x27D9110;
inline constexpr uint8_t kSrvReleaseHead[29] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
                                                0x48, 0x89, 0x7C, 0x24, 0x20, 0x55, 0x41, 0x54, 0x41, 0x55,
                                                0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xD1};
inline constexpr uint32_t kSrvReleaseBoundaries[] = {5, 10, 15, 16, 18, 20, 22, 24, 29};
inline constexpr uintptr_t kSrvReleaseSlot = 0x5B1D858;  // vtable 0x5B1D850 slot 1 (its only reference)
// How it reads its arguments (read-only check): actor = [argument 5] ([rbp+0x7F]), data = [argument 3 + 8],
// slot = byte [data+8] (0xFF: none).
inline constexpr uintptr_t kSrvArgs = 0x27D9134;
inline constexpr uint8_t kSrvArgsBytes[24] = {
    0x4C, 0x8B, 0xE2,              // mov r12,rdx                 &err
    0x33, 0xC9,                    // xor ecx,ecx
    0x48, 0x8B, 0x45, 0x7F,        // mov rax,[rbp+0x7F]          argument 5
    0x4C, 0x8B, 0x38,              // mov r15,[rax]               the thrower (server actor)
    0x4D, 0x8B, 0x70, 0x08,        // mov r14,[r8+8]              the event's data
    0x45, 0x0F, 0xB6, 0x6E, 0x08,  // movzx r13d,byte [r14+8]     slot
    0x41, 0x80, 0xFD,              // cmp r13b,0xFF
};
inline constexpr uint32_t kEv_Data = 0x08;
inline constexpr uint32_t kData_Slot = 0x08;
inline constexpr uint32_t kData_Reason = 0x09;
inline constexpr uint32_t kData_Extra = 0x0A;
// The native 2471 writer and how 0x2903950 calls it (0x2903A79..0x2903AB2): finder at [rsp+0x20] = ToNear vtable,
// [rsp+0x28] u8 1, [rsp+0x30] the thrower; rcx = the thrower, rdx = &finder, r8 = &slot, r9 = &reason.
inline constexpr uintptr_t kWriter = 0x29688C0;
inline constexpr uint8_t kWriterHead[21] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x55,
                                            0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
inline constexpr uintptr_t kWriterSite = 0x2903A79;
inline constexpr uint8_t kWriterSiteBytes[57] = {
    0x48, 0x8D, 0x1D, 0x78, 0x19, 0x22, 0x03,  // 0x2903A79 lea rbx,ISendTargetFinder vtable 0x5B253F8
    0x48, 0x89, 0x5C, 0x24, 0x20,              // 0x2903A80 mov [rsp+0x20],rbx
    0xC6, 0x44, 0x24, 0x28, 0x01,              // 0x2903A85 mov byte [rsp+0x28],1
    0x48, 0x8D, 0x05, 0x87, 0x19, 0x22, 0x03,  // 0x2903A8A lea rax,ToNearSendTargetFinder vtable 0x5B25418
    0x48, 0x89, 0x44, 0x24, 0x20,              // 0x2903A91 mov [rsp+0x20],rax
    0x48, 0x89, 0x7C, 0x24, 0x30,              // 0x2903A96 mov [rsp+0x30],rdi          the thrower
    0x4C, 0x8D, 0x4C, 0x24, 0x78,              // 0x2903A9B lea r9,[rsp+0x78]           &reason
    0x4C, 0x8D, 0x44, 0x24, 0x68,              // 0x2903AA0 lea r8,[rsp+0x68]           &slot
    0x48, 0x8D, 0x54, 0x24, 0x20,              // 0x2903AA5 lea rdx,[rsp+0x20]          &finder
    0x48, 0x8B, 0xCF,                          // 0x2903AAA mov rcx,rdi
    0xE8, 0x0E, 0x4E, 0x06, 0x00,              // 0x2903AAD call 0x29688C0
};
inline constexpr uintptr_t kToNearVtbl = 0x5B25418;
inline constexpr size_t kFinderSize = 0x18;  // +0 vtable, +8 u8 1, +0x10 actor
// What the writer touches first through the finder (checked before each call, like release_safe): ToNear::vf1
// 0x2A75860 = [[finder+0x10]+0x68]+0x1A0 (the thrower's TransformSync component) -> 0x2BC51E0 -> 0x2BC4F70: its
// owner [ts+8] and the owner's vfE8, then vf1D8 / vf160. A TransformSync that is gone would fault inside the writer
// after it reset its send context (TLS+0x250, +0x2B8) and switch F2 off for both hooks.
inline constexpr uint32_t kComps_TransformSync = 0x1A0;
inline constexpr uint32_t kTs_Owner = 0x08;
inline constexpr uint32_t kOwnerVf[] = {0xE8, 0x160, 0x1D8};
// Read-only checks of those reads: ToNear::vf1 (the vtable's slot 1) and 0x2BC4F70's first virtual call.
inline constexpr uintptr_t kToNearVf1 = 0x2A75860;
inline constexpr uint8_t kToNearVf1Bytes[15] = {
    0x48, 0x8B, 0x41, 0x10,                    // mov rax,[rcx+0x10]       the thrower
    0x48, 0x8B, 0x48, 0x68,                    // mov rcx,[rax+0x68]
    0x48, 0x8B, 0x89, 0xA0, 0x01, 0x00, 0x00,  // mov rcx,[rcx+0x1A0]      its TransformSync (then jmp 0x2BC51E0)
};
inline constexpr uintptr_t kTsOwnerCall = 0x2BC4F92;
inline constexpr uint8_t kTsOwnerCallBytes[13] = {
    0x48, 0x8B, 0x49, 0x08,              // mov rcx,[rcx+8]          the owner
    0x48, 0x8B, 0x01,                    // mov rax,[rcx]
    0xFF, 0x90, 0xE8, 0x00, 0x00, 0x00,  // call [rax+0xE8]
};
inline constexpr uint32_t kFinder_Flag = 0x08;
inline constexpr uint32_t kFinder_Actor = 0x10;

// ---------------------------------------------------------------- build 1.0.0.2976: the server's action change (launch)

// The launch (1F4EF471 "salir volando", host's run 10-12 19:11, t=167.6 s) frees the server slot WITHOUT a release frame
// event: the action-change dispatch 0x2327320 (server: from 0x2A7BD50) calls [[actor+0x68]+0x78]->vf140 (0x2328EFC,
// the only vf140 call on that component), which is 0x1807390 for the server component (the common one; the client's
// vf140 0x58B380 calls it first, 0x58B3D9). For each busy slot whose new action lacks the keep flag
// (byte [action+9] & 4, 0x1807610) it calls the TARGET's vf148(target comp, slot, thrower, 2) (0x1807646..0x18076E0:
// 0x2903950 on the server, the 2471 for an actor target) and then its own vf158(comp, slot, 2, 0, 1) = 0x1807C80
// (0x18076DF..0x18076FA), or everything through 0x1807090 (0x180757A). A tree has no target actor: nothing is sent,
// the thrower's release frame events that follow (EB340761 f0/f1) find the server slot empty (0x27D9110: err
// 0xD661CF29), and an observer keeps the rope. F2 at the launch's root: the entry of 0x1807390, the server component
// only (its vtable), the slots read before and after the original; a tree slot it freed gets the 2471 the same way as
// F2 (the writer 0x29688C0, ToNear(thrower), reason 2 as 0x1807390 passes it to vf148).
//   48 89 5C 24 08 | 48 89 74 24 18 | 48 89 7C 24 20 | 55 | 41 54 | 41 55 | 41 56 | 41 57 | 48 8D 6C 24 C9 |
//   48 81 EC 00 01 00 00.
inline constexpr uintptr_t kActRelease = 0x1807390;
inline constexpr uint8_t kActReleaseHead[36] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89,
                                                0x7C, 0x24, 0x20, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                                                0x48, 0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00};
inline constexpr uint32_t kActReleaseBoundaries[] = {5, 10, 15, 16, 18, 20, 22, 24, 29, 36};
inline constexpr uintptr_t kServerRemoteCatchVtbl = 0x5B1F508;  // pa::ServerRemoteCatchActorComponent
inline constexpr uintptr_t kActReleaseSlot = 0x5B1F648;         // its vf140 (0x5B1F508 + 0x140)
inline constexpr uintptr_t kSrvCompVf158Slot = 0x5B1F660;       // its vf158 = 0x1807C80 (the slot reset)
inline constexpr uintptr_t kSlotReset = 0x1807C80;
// Its other caller, the client's vf140 0x58B380 (read-only check): `call 0x1807390` at 0x58B3D9 (the client's
// component: passed straight through).
inline constexpr uintptr_t kActClientCall = 0x58B3D9;
inline constexpr uint8_t kActClientCallBytes[5] = {0xE8, 0xB2, 0xBF, 0x27, 0x01};
// The dispatch's call (read-only check): rax = [r14+8] the actor, rcx = [[rax+0x68]+0x78], call [rax+0x140].
inline constexpr uintptr_t kActDispatch = 0x2328EE4;
inline constexpr uint8_t kActDispatchBytes[30] = {
    0x49, 0x8B, 0x46, 0x08,              // mov rax,[r14+8]
    0x48, 0x8B, 0x48, 0x68,              // mov rcx,[rax+0x68]
    0x48, 0x8B, 0x49, 0x78,              // mov rcx,[rcx+0x78]       the remote catch component
    0x48, 0x8B, 0x01,                    // mov rax,[rcx]
    0x4C, 0x8B, 0xCF,                    // mov r9,rdi
    0x4C, 0x8B, 0xC3,                    // mov r8,rbx
    0x49, 0x8B, 0xD4,                    // mov rdx,r12
    0xFF, 0x90, 0x40, 0x01, 0x00, 0x00,  // call [rax+0x140]
};
// 0x1807390's per-slot release (read-only checks): the keep flag, the target's vf148 (reason 2) and its own vf158.
inline constexpr uintptr_t kActKeepTest = 0x1807610;
inline constexpr uint8_t kActKeepTestBytes[26] = {
    0x41, 0x0F, 0xB6, 0x47, 0x09,        // movzx eax,byte [r15+9]   the new action's flags
    0x83, 0xE0, 0x04,                    // and eax,4                the keep flag
    0x80, 0x7D, 0xD8, 0x00,              // cmp byte [rbp-0x28],0    the slot's byte +1 (0x1418082A0's copy)
    0x0F, 0x84, 0xD8, 0x00, 0x00, 0x00,  // je  next slot
    0x85, 0xC0,                          // test eax,eax
    0x0F, 0x85, 0xD0, 0x00, 0x00, 0x00,  // jne next slot (kept)
};
inline constexpr uintptr_t kActNotify = 0x1807646;
inline constexpr uint8_t kActNotifyBytes[24] = {
    0x48, 0x8B, 0x49, 0x78,              // mov rcx,[rcx+0x78]       the TARGET's component
    0x48, 0x8B, 0x01,                    // mov rax,[rcx]
    0x41, 0xB1, 0x02,                    // mov r9b,2                reason
    0x4C, 0x8B, 0x46, 0x08,              // mov r8,[rsi+8]           the thrower
    0x41, 0x0F, 0xB6, 0xD6,              // movzx edx,r14b           slot
    0xFF, 0x90, 0x48, 0x01, 0x00, 0x00,  // call [rax+0x148]         0x2903950 on the server
};
inline constexpr uintptr_t kActSelfRelease = 0x18076DF;
inline constexpr uint8_t kActSelfReleaseBytes[27] = {
    0x48, 0x8B, 0x06,                    // mov rax,[rsi]
    0xC6, 0x44, 0x24, 0x20, 0x01,        // mov byte [rsp+0x20],1
    0x45, 0x33, 0xC9,                    // xor r9d,r9d
    0x41, 0xB0, 0x02,                    // mov r8b,2                reason
    0x41, 0x0F, 0xB6, 0xD6,              // movzx edx,r14b           slot
    0x48, 0x8B, 0xCE,                    // mov rcx,rsi              this component
    0xFF, 0x90, 0x58, 0x01, 0x00, 0x00,  // call [rax+0x158]         0x1807C80
};
inline constexpr uint8_t kActReason = 2;

// ---------------------------------------------------------------- pure helpers

// The rel32 of a 5-byte `call` (E8) at `at` lands on `target`.
inline bool call_targets(uintptr_t at, const uint8_t* bytes, uintptr_t target) {
    if (bytes[0] != 0xE8) return false;
    int32_t rel = 0;
    std::memcpy(&rel, bytes + 1, 4);
    return at + 5 + static_cast<int64_t>(rel) == target;
}
// The RIP-relative target of an instruction of `len` bytes at `at` whose disp32 starts at `disp_at` (an offset in it).
inline uintptr_t rip_target(uintptr_t at, const uint8_t* insn, size_t disp_at, size_t len) {
    int32_t disp = 0;
    std::memcpy(&disp, insn + disp_at, 4);
    return at + len + static_cast<int64_t>(disp);
}

inline bool scene_nonzero(const uint8_t (&scene)[16]) {
    // As the natives (0x5947B4, 0x5A5370): the first three dwords.
    uint32_t d[3];
    std::memcpy(d, scene, sizeof d);
    return d[0] != 0 || d[1] != 0 || d[2] != 0;
}

struct Vec3 {
    float x = 0, y = 0, z = 0;
};
// The transform vf3B0 fills (0x5A5370's local): scale (3), rotation quaternion x, y, z, w, translation (3).
struct Xf {
    float scale[3] = {1, 1, 1};
    float quat[4] = {0, 0, 0, 1};
    float trans[3] = {0, 0, 0};
};
static_assert(sizeof(Xf) == 40, "vf3B0 writes 10 floats");

// world = trans + rotate(quat, scale * local)  (0x5A5370: component scale, FUN_140394790 rotation, then + trans).
inline Vec3 world_point(const Xf& xf, const Vec3& local) {
    const float px = local.x * xf.scale[0], py = local.y * xf.scale[1], pz = local.z * xf.scale[2];
    const float qx = xf.quat[0], qy = xf.quat[1], qz = xf.quat[2], qw = xf.quat[3];
    // v' = v + 2w (q x v) + 2 q x (q x v)
    const float tx = 2.0f * (qy * pz - qz * py);
    const float ty = 2.0f * (qz * px - qx * pz);
    const float tz = 2.0f * (qx * py - qy * px);
    Vec3 r;
    r.x = px + qw * tx + (qy * tz - qz * ty) + xf.trans[0];
    r.y = py + qw * ty + (qz * tx - qx * tz) + xf.trans[1];
    r.z = pz + qw * tz + (qx * ty - qy * tx) + xf.trans[2];
    return r;
}
inline float distance(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Two floats in one 64-bit event value (bit copies).
inline uint64_t pack2(float lo, float hi) {
    uint32_t a = 0, b = 0;
    std::memcpy(&a, &lo, 4);
    std::memcpy(&b, &hi, 4);
    return uint64_t{a} | (uint64_t{b} << 32);
}
inline float unpack_lo(uint64_t v) {
    const uint32_t a = static_cast<uint32_t>(v);
    float f;
    std::memcpy(&f, &a, 4);
    return f;
}
inline float unpack_hi(uint64_t v) {
    const uint32_t a = static_cast<uint32_t>(v >> 32);
    float f;
    std::memcpy(&f, &a, 4);
    return f;
}
// An optional byte in 16 bits: 0 = unread, else 0x100 | value.
inline uint16_t opt8(bool ok, uint8_t v) { return ok ? static_cast<uint16_t>(0x100 | v) : uint16_t{0}; }
inline bool opt8_ok(uint64_t f) { return (f & 0x100) != 0; }
inline uint8_t opt8_val(uint64_t f) { return static_cast<uint8_t>(f & 0xFF); }

// ---------------------------------------------------------------- F1 + F3: the decision at the apply's entry

// How this PC's handle of the scene object was looked up (resolve_local_handle in remote_rope.cpp).
enum class Resolve : uint8_t {
    NotTried,   // not a candidate
    NoManager,  // WorldSystem / its scene-object manager unreadable
    NotFound,   // ResolveUuidLookup: no object with that scene id here, or no reference taken ([att+0x10] 0)
    NoBody,     // [obj+0x58] 0, gone ([+0x15] == 1), or [+0xE0] 0
    NoHandle,   // vf70 gave -1
    BadCode,    // a vtable entry outside the exe's code: not called
    Faulted,    // an access violation inside a game call: F1 off for the rest of the run
    Ok,
    Count
};
inline const char* resolve_name(Resolve r) {
    switch (r) {
        case Resolve::NotTried: return "not tried";
        case Resolve::NoManager: return "no scene manager";
        case Resolve::NotFound: return "scene object not loaded here";
        case Resolve::NoBody: return "no physics body";
        case Resolve::NoHandle: return "body without handle";
        case Resolve::BadCode: return "vtable outside the code (not called)";
        case Resolve::Faulted: return "FAULTED (F1 off)";
        case Resolve::Ok: return "ok";
        case Resolve::Count: break;
    }
    return "?";
}

struct ApplyInputs {
    bool ctl_ok = false;
    uint8_t ctl = 0;  // byte [thrower+0xC8]: != 0 controlled here
    bool slot_ok = false;
    uint8_t slot = 0;  // *argument 3
    bool slot_type_ok = false;
    uint8_t slot_type = 0;  // byte [comp+0x20+slot*0x58] before anything
    bool kind_ok = false;
    uint8_t kind = 0;  // *argument 4
    bool target_ok = false;  // the target block read (class, actor, scene, handle)
    uint8_t cls = 0;
    uint32_t target_actor = 0;
    uint8_t scene[16] = {};
    uint64_t handle = 0;
    Resolve resolve = Resolve::NotTried;
    uint64_t local_handle = kNoHandle;
    // The busy slot's stored target (slot_type is its kind): F3 releases only the measured case.
    bool stale_cls_ok = false;
    uint8_t stale_cls = 0;
    bool stale_actor_ok = false;
    uint32_t stale_actor = 0;
    // The own-throw check is done (kOwnChecks matches in a row): own throws are no longer computed.
    bool own_check_done = false;
};

// The own throws' handle check: after this many consecutive matches it is not computed any more.
inline constexpr uint32_t kOwnChecks = 20;

enum class Stale : uint8_t {
    None,          // own thrower, or the slot is empty
    Unreadable,    // the slot or the control byte unreadable: nothing done
    Release,       // remote thrower, busy slot holding a tree target: released first (1)
    WouldRelease,  // the same in probe
    OutOfScope,    // remote thrower, busy slot holding anything else (an actor target...): logged only
    Count
};
inline const char* stale_name(Stale s) {
    switch (s) {
        case Stale::None: return "slot free";
        case Stale::Unreadable: return "slot UNREADABLE";
        case Stale::Release: return "STALE slot (remote thrower): native release first";
        case Stale::WouldRelease: return "STALE slot (remote thrower): WOULD release first with remote_rope=1";
        case Stale::OutOfScope:
            return "STALE slot (remote thrower) holding a non-tree target: NOT released (not measured, logged only)";
        case Stale::Count: break;
    }
    return "?";
}

enum class Target : uint8_t {
    Unreadable,        // control byte / kind / target block unreadable: passed through
    NotTree,           // not kind 0 + class 2 + a handle + a scene id: untouched
    OwnMatch,          // thrower controlled here: the computed handle equals the packet's (the check)
    OwnMismatch,       // thrower controlled here: they differ (the mapping is not the thrower's!)
    OwnUnresolved,     // thrower controlled here: not computed
    RemoteSame,        // remote thrower: this PC's handle equals the packet's
    RemoteTranslate,   // remote thrower: this PC's handle differs; written (1) or would be (probe)
    RemoteUnresolved,  // remote thrower: not computed here; passed through unchanged
    OwnSkipped,        // thrower controlled here, the own-throw check already done: not computed
    Count
};
inline const char* target_name(Target t) {
    switch (t) {
        case Target::Unreadable: return "UNREADABLE (passed through)";
        case Target::NotTree: return "not a body target (untouched)";
        case Target::OwnMatch: return "own throw, handle match";
        case Target::OwnMismatch: return "own throw, handle MISMATCH";
        case Target::OwnUnresolved: return "own throw, not resolved";
        case Target::RemoteSame: return "remote thrower, same handle here";
        case Target::RemoteTranslate: return "remote thrower, handle TRANSLATED";
        case Target::RemoteUnresolved: return "remote thrower, UNRESOLVED (passed through)";
        case Target::OwnSkipped: return "own throw (check done, not computed)";
        case Target::Count: break;
    }
    return "?";
}

// A tree target: kind 0, class 2, a handle and a scene id.
inline bool tree_target(const ApplyInputs& in) {
    return in.ctl_ok && in.kind_ok && in.target_ok && in.kind == kKindNormal && in.cls == kClassBody &&
           in.handle != kNoHandle && scene_nonzero(in.scene);
}
// Whether the hook should look this PC's handle up (both modes: probe computes; the own throws are the check until
// kOwnChecks matches in a row).
inline bool wants_resolve(const ApplyInputs& in) {
    return tree_target(in) && !(in.ctl != 0 && in.own_check_done);
}
// The busy slot's stored target is the measured case (kind 0, class 2, no target actor).
inline bool stale_is_tree(const ApplyInputs& in) {
    return in.slot_type == kKindNormal && in.stale_cls_ok && in.stale_cls == kClassBody && in.stale_actor_ok &&
           in.stale_actor == 0;
}

struct ApplyDecision {
    Stale stale = Stale::None;
    Target target = Target::Unreadable;
    bool release = false;      // call the native release first (1, stale slot)
    bool write = false;        // write local_handle into the target block (1)
    uint64_t value = 0;
};

inline ApplyDecision decide_apply(Mode m, const ApplyInputs& in) {
    ApplyDecision d;
    const bool on = m == Mode::On;
    // F3
    if (!in.ctl_ok || !in.slot_ok || in.slot >= kSlots || !in.slot_type_ok) {
        d.stale = (in.ctl_ok && in.ctl != 0) ? Stale::None : Stale::Unreadable;
    } else if (in.ctl == 0 && in.slot_type != kSlotEmpty) {
        if (!stale_is_tree(in)) {
            d.stale = Stale::OutOfScope;
        } else {
            d.stale = on ? Stale::Release : Stale::WouldRelease;
            d.release = on;
        }
    }
    // F1
    if (!in.ctl_ok || !in.kind_ok || !in.target_ok) {
        d.target = Target::Unreadable;
        return d;
    }
    if (!tree_target(in)) {
        d.target = Target::NotTree;
        return d;
    }
    if (!wants_resolve(in)) {
        d.target = Target::OwnSkipped;
        return d;
    }
    const bool resolved = in.resolve == Resolve::Ok && in.local_handle != kNoHandle;
    if (in.ctl != 0) {
        d.target = !resolved ? Target::OwnUnresolved
                             : (in.local_handle == in.handle ? Target::OwnMatch : Target::OwnMismatch);
        return d;
    }
    if (!resolved) {
        d.target = Target::RemoteUnresolved;
    } else if (in.local_handle == in.handle) {
        d.target = Target::RemoteSame;
    } else {
        d.target = Target::RemoteTranslate;
        d.write = on;
        d.value = in.local_handle;
    }
    return d;
}

// ---------------------------------------------------------------- F2: the decision after the server's release

struct ReleaseInputs {
    bool pre_ok = false;  // the server slot read before the original
    uint8_t slot = 0xFF;  // byte [data+8]
    uint8_t reason = 0;
    uint8_t kind = 0;     // the server slot's kind (6 = empty)
    uint8_t cls = 0;
    uint32_t target_actor = 0;
    bool err_ok = false;
    uint32_t err = 0;     // the original's *argument 2
};

enum class Send : uint8_t {
    Unreadable,     // the event / the server slot unreadable: nothing sent
    NoSlot,         // slot 0xFF or >= 2
    NativeError,    // the original failed (err != 0)
    SlotEmpty,      // nothing was hooked on the server (kind 6)
    ActorTarget,    // an actor target: the native code sends its own 2471 (0x2903950) or none
    OutOfScope,     // kind != 0 or class != 2: not measured, nothing sent
    Sent,           // 1: the native writer called
    WouldSend,      // probe
    Count
};
inline const char* send_name(Send s) {
    switch (s) {
        case Send::Unreadable: return "UNREADABLE (nothing sent)";
        case Send::NoSlot: return "no slot";
        case Send::NativeError: return "native error (nothing sent)";
        case Send::SlotEmpty: return "server slot empty (nothing sent)";
        case Send::ActorTarget: return "actor target (the native path; nothing added)";
        case Send::OutOfScope: return "not kind 0 / class 2 (nothing sent)";
        case Send::Sent: return "2471 SENT ToNear(thrower)";
        case Send::WouldSend: return "WOULD SEND 2471 ToNear(thrower) with remote_rope=1";
        case Send::Count: break;
    }
    return "?";
}

inline Send decide_release(Mode m, const ReleaseInputs& in) {
    if (!in.pre_ok) return Send::Unreadable;
    if (in.slot >= kSlots) return Send::NoSlot;
    if (!in.err_ok) return Send::Unreadable;
    if (in.err != 0) return Send::NativeError;
    if (in.kind == kSlotEmpty) return Send::SlotEmpty;
    if (in.target_actor != 0) return Send::ActorTarget;
    if (in.kind != kKindNormal || in.cls != kClassBody) return Send::OutOfScope;
    return m == Mode::On ? Send::Sent : Send::WouldSend;
}

// ---------------------------------------------------------------- F2 at the launch: after the action change

// One server slot around 0x1807390 (read before and after the original, on the server's thread).
struct ActionSlot {
    bool before_ok = false;  // kind, class and target actor read before the original
    uint8_t kind = kSlotEmpty;
    uint8_t cls = 0;
    uint32_t target_actor = 0;
    bool after_ok = false;
    uint8_t after_kind = 0;
};

enum class ActRelease : uint8_t {
    NotHeld,      // the slot was empty: nothing to do (no event)
    Kept,         // still busy after the action change (the new action keeps it): no event
    Unreadable,   // busy before, unreadable before or after: nothing sent
    ActorTarget,  // freed, an actor target: the native vf148 (0x2903950) sent its own 2471 (counted, no line)
    OutOfScope,   // freed, not kind 0 / class 2: not measured, nothing sent
    Sent,         // 1: freed tree, the native writer called
    WouldSend,    // probe
    Count
};
inline const char* act_release_name(ActRelease r) {
    switch (r) {
        case ActRelease::NotHeld: return "slot empty";
        case ActRelease::Kept: return "kept by the new action";
        case ActRelease::Unreadable: return "UNREADABLE (nothing sent)";
        case ActRelease::ActorTarget: return "actor target (the native vf148 sent its 2471)";
        case ActRelease::OutOfScope: return "not kind 0 / class 2 (nothing sent)";
        case ActRelease::Sent: return "2471 SENT ToNear(thrower)";
        case ActRelease::WouldSend: return "WOULD SEND 2471 ToNear(thrower) with remote_rope=1";
        case ActRelease::Count: break;
    }
    return "?";
}

inline ActRelease decide_action_release(Mode m, const ActionSlot& s) {
    if (!s.before_ok) return ActRelease::Unreadable;
    if (s.kind == kSlotEmpty) return ActRelease::NotHeld;
    if (!s.after_ok) return ActRelease::Unreadable;
    if (s.after_kind != kSlotEmpty) return ActRelease::Kept;
    if (s.target_actor != 0) return ActRelease::ActorTarget;
    if (s.kind != kKindNormal || s.cls != kClassBody) return ActRelease::OutOfScope;
    return m == Mode::On ? ActRelease::Sent : ActRelease::WouldSend;
}
// Whether the hook logs a line for it (one line per launch; actor targets and kept slots are only counted).
inline bool act_logged(ActRelease r) {
    return r == ActRelease::Sent || r == ActRelease::WouldSend || r == ActRelease::OutOfScope ||
           r == ActRelease::Unreadable;
}

// ---------------------------------------------------------------- readable launch lines (the module's thread)

// After a launch the thrower's release frame events (EB340761 f0/f1, sometimes two pairs) reach 0x27D9110 with the
// server slot already empty: each was a "native error" line. The worker folds them into the launch's line: per
// thrower and slot it remembers whether the slot was freed (by the action change, or an empty-slot line already
// logged) since the last 2763 applied on this client; such a release event is counted, not logged.
class LaunchFold {
public:
    enum State : uint8_t { kUnknown = 0, kBusy, kFreedByAction, kEmptyLogged };

    void on_apply(uint32_t key, uint8_t slot) { set(key, slot, kBusy); }
    void on_action_free(uint32_t key, uint8_t slot) { set(key, slot, kFreedByAction); }
    void on_release(uint32_t key, uint8_t slot) { set(key, slot, kUnknown); }  // a busy slot released (frame event)
    // A release event that found the server slot empty: true = fold it (not logged).
    bool on_empty_release(uint32_t key, uint8_t slot) {
        const State s = get(key, slot);
        if (s == kFreedByAction || s == kEmptyLogged) return true;
        set(key, slot, kEmptyLogged);
        return false;
    }
    State get(uint32_t key, uint8_t slot) const {
        if (slot >= kSlots) return kUnknown;
        for (const Entry& e : e_) {
            if (e.used && e.key == key) return static_cast<State>(e.state[slot]);
        }
        return kUnknown;
    }

private:
    struct Entry {
        bool used = false;
        uint32_t key = 0;
        uint8_t state[kSlots] = {};
    };
    void set(uint32_t key, uint8_t slot, State s) {
        if (slot >= kSlots) return;
        for (Entry& e : e_) {
            if (e.used && e.key == key) {
                e.state[slot] = s;
                return;
            }
        }
        Entry& e = e_[next_++ % kEntries];  // the oldest thrower makes room
        e = Entry{};
        e.used = true;
        e.key = key;
        e.state[slot] = s;
    }
    static constexpr size_t kEntries = 8;
    Entry e_[kEntries];
    size_t next_ = 0;
};

// ---------------------------------------------------------------- the rope's flight (logged with the apply)

// 0x5A12A0 sets the slot's state to 2 and its flight seconds (distance / speed, +0xA94) AFTER the apply returns (state
// 1 there): the worker reads them later and logs the apply's line then. States 2 (flight) and 3 (attached) hold the
// value of this throw.
inline constexpr uint64_t kFlightWaitMs = 3000;
inline bool flight_ready(uint8_t state) { return state == 2 || state == 3; }

// ---------------------------------------------------------------- the 2471 payload (tests, logging)

struct Payload2471 {
    uint32_t key = 0;
    uint8_t slot = 0;
    uint8_t reason = 0;
};
// The packet as the handler sees it: total (u16 at [p+0x10]) and the buffer's u16 length must agree (total - 5).
inline bool parse_2471(uint16_t total, const uint8_t* buf, size_t buf_len, Payload2471& out) {
    if (buf_len < kBuf_Payload + k2471Payload) return false;
    uint16_t len = 0;
    std::memcpy(&len, buf + kBuf_Len, 2);
    if (len != k2471Payload || static_cast<uint32_t>(total) != uint32_t{len} + kBuf_Payload) return false;
    std::memcpy(&out.key, buf + kBuf_Payload, 4);
    out.slot = buf[kBuf_Payload + 4];
    out.reason = buf[kBuf_Payload + 5];
    return true;
}

// ---------------------------------------------------------------- the counters

struct Tally {
    std::atomic<uint64_t> applies{0}, unexpected_caller{0}, own{0}, remote{0}, unreadable{0}, not_tree{0};
    std::atomic<uint64_t> own_match{0}, own_mismatch{0}, own_unresolved{0};
    std::atomic<uint64_t> remote_same{0}, remote_translate{0}, translated{0}, write_failed{0}, remote_unresolved{0};
    std::atomic<uint64_t> stale{0}, stale_released{0}, stale_deferred{0}, stale_left{0}, release_faulted{0};
    std::atomic<uint64_t> stale_out_of_scope{0}, own_skipped{0}, not_applied_after_fault{0}, release_unsafe{0};
    std::atomic<uint64_t> apply_failed{0}, failed_dirty{0};
    std::atomic<uint64_t> srv_releases{0}, srv_sent{0}, srv_would{0}, srv_actor{0}, srv_other{0}, srv_faulted{0};
    std::atomic<uint64_t> h2471{0}, h2471_own{0}, h2471_remote{0}, h2471_busy_before{0}, h2471_unparsed{0};
    std::atomic<uint64_t> act_freed{0}, act_sent{0}, act_would{0}, act_actor{0}, act_other{0}, act_faulted{0};
    std::atomic<uint64_t> srv_folded{0}, writer_unsafe{0};
    // At each launch the server frees (probe: would send): the thrower's OWN client slot then (thrower controlled
    // here), read by the worker: still busy means a 2471 would cut its rope (measure before remote_rope=1).
    std::atomic<uint64_t> act_thrower_free{0}, act_thrower_busy{0}, act_thrower_unread{0};

    static void add(std::atomic<uint64_t>& c) { c.fetch_add(1, std::memory_order_relaxed); }

    void note_apply(const ApplyInputs& in, const ApplyDecision& d, bool wrote, bool expected_caller) {
        add(applies);
        if (!expected_caller) add(unexpected_caller);
        if (in.ctl_ok) add(in.ctl ? own : remote);
        switch (d.target) {
            case Target::Unreadable: add(unreadable); break;
            case Target::NotTree: add(not_tree); break;
            case Target::OwnMatch: add(own_match); break;
            case Target::OwnMismatch: add(own_mismatch); break;
            case Target::OwnUnresolved: add(own_unresolved); break;
            case Target::RemoteSame: add(remote_same); break;
            case Target::RemoteTranslate:
                add(remote_translate);
                if (d.write) add(wrote ? translated : write_failed);
                break;
            case Target::RemoteUnresolved: add(remote_unresolved); break;
            case Target::OwnSkipped: add(own_skipped); break;
            case Target::Count: break;
        }
        if (d.stale == Stale::Release || d.stale == Stale::WouldRelease) add(stale);
        if (d.stale == Stale::OutOfScope) add(stale_out_of_scope);
    }
};

}  // namespace cdmp::session2::rrp
