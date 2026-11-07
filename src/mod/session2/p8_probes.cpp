#include "mod/session2/p8_probes.h"
#include "common/native_opcode.h"
#include "mod/game/build_profile.h"

#include <windows.h>

#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "mod/game/hook_prepare.h"
#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/net/troc_sniffer.h"
#include "mod/session2/freeze_watchdog.h"
#include "mod/session2/guest.h"
#include "mod/session2/guest_seq_route.h"
#include "mod/session2/offsets_s2.h"
#include "mod/session2/p8_probe_logic.h"

namespace cdmp::session2 {
namespace {

namespace p = p8p;

// ---------------------------------------------------------------- the points (build 2976, .rsrc)
//
// Every byte array below was read from the authorised read-only PE
// (D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe, SHA-256 57DA440D...E1E7) and
// is compared with the running game before anything is hooked. "patch" is the
// number of bytes safetyhook relocates for its 5-byte jmp: whole instructions,
// checked with capstone (instruction boundaries from the function start), and
// no branch of the function nor any rel32 (E8/E9/0F8x) of .rsrc/.xtls/.text
// lands strictly inside them (docs/research/pendientes/p8-impl-s1s2.md §2).

// S2, the guest's loading state 0x7797F0 (RDI = P from 0x779823, never written
// again before these points).
//   0x779E39 mov rcx,[rdi+0xC0] ; mov rcx,[rcx] ; call 0x9A5BE0      (RAX = M)
//   0x779E48 cmp byte [rax+0x231],2 ; jb 0x77A1A8                    patch 7
constexpr uintptr_t kPred1 = 0x779E48;
constexpr uint32_t kPred1Patch = 7;
constexpr uint8_t kPred1Bytes[] = {0x80, 0xB8, 0x31, 0x02, 0x00, 0x00, 0x02, 0x0F, 0x82, 0x53, 0x03, 0x00, 0x00};
constexpr uintptr_t kPred1Ctx = 0x779E39;
constexpr uint8_t kPred1CtxBytes[] = {0x48, 0x8B, 0x8F, 0xC0, 0x00, 0x00, 0x00, 0x48,
                                      0x8B, 0x09, 0xE8, 0x98, 0xBD, 0x22, 0x00};
//   0x779E55 vmovsd xmm0,[rdi+0x150] ; ... ; mov rcx,rax ; call 0xA97D60
//   0x779E77 test al,al ; je 0x77A1A8 ; mov byte [rdi+0x15C],0x12     patch 8 (the je rel32 is relocated)
constexpr uintptr_t kPred2 = 0x779E77;
constexpr uint32_t kPred2Patch = 8;
constexpr uint8_t kPred2Bytes[] = {0x84, 0xC0, 0x0F, 0x84, 0x29, 0x03, 0x00, 0x00,
                                   0xC6, 0x87, 0x5C, 0x01, 0x00, 0x00, 0x12};
constexpr uintptr_t kPred2Ctx = 0x779E55;
constexpr uint8_t kPred2CtxBytes[] = {0xC5, 0xFB, 0x10, 0x87, 0x50, 0x01, 0x00, 0x00, 0xC5, 0xFB, 0x11, 0x45,
                                      0xF0, 0x8B, 0x8F, 0x58, 0x01, 0x00, 0x00, 0x89, 0x4D, 0xF8, 0x48, 0x8D,
                                      0x55, 0xF0, 0x48, 0x8B, 0xC8, 0xE8, 0xE9, 0xDE, 0x31, 0x00};

// S2, PlayAllPreLoadSequencerAck 2322's handler 0xB87890 (rcx, rdx = int* res,
// r8 = the packet wrapper): r14 = [r8+0x18] (raw: u16 opcode | i8 slot | u16
// len | payload), the actor key is the u32 at raw+5 (0xB87918 lea rax,[r14+5]).
constexpr uintptr_t kH2322 = 0xB87890;
constexpr uint32_t kH2322Patch = 5;
constexpr uint8_t kH2322Bytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57,
                                   0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
constexpr uintptr_t kH2322Ctx = 0xB878AC;
constexpr uint8_t kH2322CtxBytes[] = {0x49, 0x8B, 0xF8, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF1, 0x45, 0x33,
                                      0xED, 0x4D, 0x8B, 0x70, 0x18, 0x41, 0x0F, 0xB7, 0x4E, 0x03};
constexpr uintptr_t kH2322Payload = 0xB87918;
constexpr uint8_t kH2322PayloadBytes[] = {0x49, 0x8D, 0x46, 0x05};
//   0xB87A5D mov rcx,[rbp-0x58] ; mov rax,[rcx] ; call [rax+0x48]      (RAX = M)
//   0xB87A67 mov byte [rax+0x231],2                                   patch 7
constexpr uintptr_t kW2322 = 0xB87A67;
constexpr uint32_t kW2322Patch = 7;
constexpr uint8_t kW2322Bytes[] = {0xC6, 0x80, 0x31, 0x02, 0x00, 0x00, 0x02,
                                   0x48, 0x8D, 0x05, 0xEB, 0xB3, 0xD8, 0x04};
constexpr uintptr_t kW2322Ctx = 0xB87A5D;
constexpr uint8_t kW2322CtxBytes[] = {0x48, 0x8B, 0x4D, 0xA8, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x48};

// S2 host, MoveFieldCompleteCore 0x2BC7670 (r13 = field from r8 at 0x2BC769E,
// r14 = the travelling actor):
//   0x2BC8452 mov rax,[r14+0xA0] ; ... ; mov eax,[rax+0x60] (or r12d=0) ;
//             mov r8d,[r13+0x180]
//   0x2BC8470 cmp eax,r8d ; jne 0x2BC84D9                             patch 5 (the short jne is relocated)
//   0x2BC84D4 call 0x287EF60 (only on the owner's path)
constexpr uintptr_t kOwner = 0x2BC8470;
constexpr uint32_t kOwnerPatch = 5;
constexpr uint8_t kOwnerBytes[] = {0x41, 0x3B, 0xC0, 0x75, 0x64, 0x48, 0x8D, 0x55, 0x80};
constexpr uintptr_t kOwnerCtx = 0x2BC8452;
constexpr uint8_t kOwnerCtxBytes[] = {0x49, 0x8B, 0x86, 0xA0, 0x00, 0x00, 0x00, 0x45, 0x33, 0xE4,
                                      0x48, 0x85, 0xC0, 0x74, 0x05, 0x8B, 0x40, 0x60, 0xEB, 0x03,
                                      0x41, 0x8B, 0xC4, 0x45, 0x8B, 0x85, 0x80, 0x01, 0x00, 0x00};
constexpr uintptr_t kOwnerPrep = 0x2BC84D4;
constexpr uint8_t kOwnerPrepBytes[] = {0xE8, 0x87, 0x6A, 0xCB, 0xFF};

// S2 host, 0x287EF60 (r14 = the target actor, from r8 at 0x287EF84):
//   0x287F1CC lea rdx,[rsp+0x50] ; mov rcx,r14 ; call 0x2999D80 ; nop  patch 5 (the lea)
// Hooked one instruction before the call the report names (0x287F1D4): the
// same path (no branch lands in 0x287F1CD..0x287F1D4) without relocating a call
// (its return address would point into a trampoline with no unwind data).
constexpr uintptr_t kEmit = 0x287F1CC;
constexpr uint32_t kEmitPatch = 5;
constexpr uint8_t kEmitBytes[] = {0x48, 0x8D, 0x54, 0x24, 0x50, 0x49, 0x8B, 0xCE, 0xE8, 0xA7,
                                  0xAB, 0x11, 0x00, 0x90, 0x48, 0x89, 0x7C, 0x24, 0x50};

// S2 host, pa::uiCommonScript::UIGamePlayControl_Root_ExtraReward: its vtable
// exe+0x569C6B8, slot 113 (exe+0x569CA40) = 0xE919C0, the only reference to it.
// 0xE919C0(this, rdx, r8 = event object, r9b, ...): with r9b == 0 and r8 ==
// [this+0x160] it tail-calls [vtable+0x28] (the close branch); r8 == [this+0x168]
// is the other button (decompilation 0xe919c0.c).
constexpr uintptr_t kUi = 0xE919C0;
constexpr uint32_t kUiPatch = 5;
constexpr uint8_t kUiBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
                                0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40};
constexpr uintptr_t kUiVtable = 0x569C6B8;
constexpr uintptr_t kUiVtableSlot = 0x569CA40;
constexpr uint32_t kUiClose = 0x160, kUiOther = 0x168;

// S1, ActionGraph_Find 0x24314C0 (rcx = graph, edx = hash, r8 = out): its whole
// body (0xDA bytes) is checked, because the probe replays it (graph_find).
constexpr uintptr_t kFind = 0x24314C0;
constexpr uint32_t kFindPatch = 5;
constexpr uint8_t kFindBytes[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x44, 0x8B, 0x51, 0x20,
    0x49, 0x8B, 0xF0, 0x44, 0x8B, 0xDA, 0x45, 0x85, 0xD2, 0x74, 0x7C, 0x45, 0x33, 0xC9, 0x41, 0x83, 0xEA, 0x01, 0x78,
    0x73, 0x48, 0x8B, 0x79, 0x18, 0x0F, 0x1F, 0x44, 0x00, 0x00, 0x41, 0x8B, 0xC2, 0x41, 0x2B, 0xC1, 0x79, 0x02, 0xFF,
    0xC0, 0xD1, 0xF8, 0x42, 0x8D, 0x14, 0x08, 0x48, 0x63, 0xC2, 0x48, 0x8D, 0x0C, 0x40, 0x4C, 0x8B, 0x04, 0xCF, 0x4D,
    0x85, 0xC0, 0x74, 0x3A, 0x48, 0x83, 0x7C, 0xCF, 0x08, 0x00, 0x74, 0x16, 0x48, 0x8B, 0x44, 0xCF, 0x10, 0x48, 0x85,
    0xC0, 0x74, 0x0C, 0x44, 0x3B, 0x58, 0x18, 0x73, 0x06, 0x44, 0x8D, 0x52, 0xFF, 0xEB, 0x27, 0x4D, 0x85, 0xC0, 0x74,
    0x17, 0x48, 0x83, 0x7C, 0xCF, 0x08, 0x00, 0x74, 0x0F, 0x48, 0x8B, 0x44, 0xCF, 0x10, 0x48, 0x85, 0xC0, 0x74, 0x05,
    0x8B, 0x40, 0x18, 0xEB, 0x02, 0x33, 0xC0, 0x41, 0x3B, 0xC3, 0x73, 0x1B, 0x44, 0x8D, 0x4A, 0x01, 0x45, 0x3B, 0xD1,
    0x7D, 0x96, 0x32, 0xC0, 0x48, 0x8B, 0x5C, 0x24, 0x08, 0x48, 0x8B, 0x74, 0x24, 0x10, 0x48, 0x8B, 0x7C, 0x24, 0x18,
    0xC3, 0x85, 0xD2, 0x78, 0xEA, 0x48, 0x8B, 0x5C, 0x24, 0x08, 0x48, 0x8D, 0x0C, 0x52, 0xC5, 0xF8, 0x10, 0x04, 0xCF,
    0xC5, 0xF8, 0x11, 0x06, 0xC5, 0xFB, 0x10, 0x4C, 0xCF, 0x10, 0x48, 0x8B, 0x7C, 0x24, 0x18, 0xB0, 0x01, 0xC5, 0xFB,
    0x11, 0x4E, 0x10, 0x48, 0x8B, 0x74, 0x24, 0x10, 0xC3};
// Its two callers in RestartLayerAction: the return addresses.
constexpr uintptr_t kFindRetPrimary = 0x863914, kFindRetFallback = 0x86392A;

// S1, RestartLayerAction 0x863880's loop (r15 = CC from 0x86389C; rdi =
// CC+0x70+8i, the fallback slot; [rdi+0x18] = the layer; rbx = the triple
// being filled; esi = the hash):
//   0x8638EA lea rdi,[r15+0x70] ; lea rbx,[rsp+0x28] ; mov esi,[rbx-8] ; ...
//            both calls of 0x24314C0 (checked whole: the probe replays them)
//   0x86392A add rdi,8 ; add rbx,0x78 ; sub rbp,1 ; jne 0x8638F3       patch 8
// 0x863914 (the report's point) is NOT usable: its 5-byte jmp would cover
// 0x863918, the target of "je 0x863918" at 0x863908.
constexpr uintptr_t kRestart = 0x86392A;
constexpr uint32_t kRestartPatch = 8;
constexpr uint8_t kRestartBytes[] = {0x48, 0x83, 0xC7, 0x08, 0x48, 0x83, 0xC3,
                                     0x78, 0x48, 0x83, 0xED, 0x01, 0x75, 0xBB};
constexpr uintptr_t kRestartLoop = 0x8638EA;
constexpr uint8_t kRestartLoopBytes[] = {
    0x49, 0x8D, 0x7F, 0x70, 0x48, 0x8D, 0x5C, 0x24, 0x28, 0x8B, 0x73, 0xF8, 0x85, 0xF6, 0x74, 0x30,
    0x48, 0x8B, 0x47, 0x18, 0x48, 0x8B, 0x88, 0x10, 0x01, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x0E,
    0x4C, 0x8B, 0xC3, 0x8B, 0xD6, 0xE8, 0xAC, 0xDB, 0xBC, 0x01, 0x84, 0xC0, 0x75, 0x12, 0x48, 0x8B,
    0x0F, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0x4C, 0x8B, 0xC3, 0x8B, 0xD6, 0xE8, 0x96, 0xDB, 0xBC, 0x01};
constexpr uintptr_t kRestartCc = 0x86389C;  // mov r15,rcx
constexpr uint8_t kRestartCcBytes[] = {0x4C, 0x8B, 0xF9};

// S1, the BodyAnimation setup 0xA79770(context, desc, r8b, r9b): rsi = context
// (0xA797BF, never written again), r14 = desc, CC = [context].
//   0xA79770 mov rax,rsp ; mov [rax+0x18],rbx                         patch 7
//   0xA79B78 mov rbx,[rsi] ; add rbx,0x70 ; ... ; 0xA79BA9 mov rcx,[rdi]
//   0xA79BAC mov [rbx],rcx ; mov [rdi],r15                            patch 6 (CC+0x70 = graph)
//   0xA79F18 mov rbx,[rsi] ; add rbx,0x78 ; ... ; 0xA79F49 mov rcx,[r15]
//   0xA79F4C mov [rbx],rcx ; xor eax,eax                              patch 5 (CC+0x78 = graph)
constexpr uintptr_t kBodyAnim = 0xA79770;
constexpr uint32_t kBodyAnimPatch = 7;
constexpr uint8_t kBodyAnimBytes[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x44,
                                      0x88, 0x48, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54};
constexpr uintptr_t kBodyAnimArgs = 0xA797B8;
constexpr uint8_t kBodyAnimArgsBytes[] = {0x45, 0x0F, 0xB6, 0xF8, 0x4C, 0x8B, 0xF2, 0x48, 0x8B, 0xF1};
constexpr uintptr_t kInstall70 = 0xA79BAC;
constexpr uint32_t kInstall70Patch = 6;
constexpr uint8_t kInstall70Bytes[] = {0x48, 0x89, 0x0B, 0x4C, 0x89, 0x3F, 0x48, 0x8B, 0x03, 0x48, 0x85, 0xC0};
constexpr uintptr_t kInstall70Ctx = 0xA79B78;
constexpr uint8_t kInstall70CtxBytes[] = {0x48, 0x8B, 0x1E, 0x48, 0x83, 0xC3, 0x70};
constexpr uintptr_t kInstall70Load = 0xA79BA9;
constexpr uint8_t kInstall70LoadBytes[] = {0x48, 0x8B, 0x0F};
constexpr uintptr_t kInstall78 = 0xA79F4C;
constexpr uint32_t kInstall78Patch = 5;
constexpr uint8_t kInstall78Bytes[] = {0x48, 0x89, 0x0B, 0x33, 0xC0, 0x49, 0x89, 0x07, 0x48, 0x8B, 0x03};
constexpr uintptr_t kInstall78Ctx = 0xA79F18;
constexpr uint8_t kInstall78CtxBytes[] = {0x48, 0x8B, 0x1E, 0x48, 0x83, 0xC3, 0x78};
constexpr uintptr_t kInstall78Load = 0xA79F49;
constexpr uint8_t kInstall78LoadBytes[] = {0x49, 0x8B, 0x0F};

// S1, ClientSequencerPlayDataActorComponent::vf13 0x9ED290 (r14 = component
// from 0x9ED2B5, rbp = rsp from 0x9ED2A8; neither written again):
//   0x9ED415 mov r8d,[r14+0x2D8] ; lea rdx,[rbp-0x28] ; ... ; call 0x8AE1A0 ; nop
//   0x9ED431 cmp byte [rbp-0x18],0 ; jne 0x9ED453                      patch 6 (actor found?)
//   0x9ED470 lea r8,[r14+0x2E0] ; lea rdx,[rbp-0x58] ; call [rax+8] ; nop
//   0x9ED47F mov rdi,[rbp-0x58] ; test rdi,rdi ; je 0x9ED575 (rel32)   patch 7; the je is checked whole
constexpr uintptr_t kBind = 0x9ED431;
constexpr uint32_t kBindPatch = 6;
constexpr uint8_t kBindBytes[] = {0x80, 0x7D, 0xE8, 0x00, 0x75, 0x1C, 0x44, 0x89, 0x2E};
constexpr uintptr_t kBindFrame = 0x9ED2A8;
constexpr uint8_t kBindFrameBytes[] = {0x48, 0x8B, 0xEC};
constexpr uintptr_t kBindComp = 0x9ED2B5;
constexpr uint8_t kBindCompBytes[] = {0x4C, 0x8B, 0xF1};
constexpr uintptr_t kBindCtx = 0x9ED415;
constexpr uint8_t kBindCtxBytes[] = {0x45, 0x8B, 0x86, 0xD8, 0x02, 0x00, 0x00, 0x48, 0x8D, 0x55, 0xD8};
constexpr uintptr_t kSeq = 0x9ED47F;
constexpr uint32_t kSeqPatch = 7;
constexpr uint8_t kSeqBytes[] = {0x48, 0x8B, 0x7D, 0xA8, 0x48, 0x85, 0xFF, 0x0F, 0x84, 0xE9, 0x00, 0x00, 0x00};
constexpr uintptr_t kSeqCtx = 0x9ED470;
constexpr uint8_t kSeqCtxBytes[] = {0x4D, 0x8D, 0x86, 0xE0, 0x02, 0x00, 0x00, 0x48,
                                    0x8D, 0x55, 0xA8, 0xFF, 0x50, 0x08, 0x90};

// Structure offsets the hooks read (p8-raiz-invitado.md §3, §4).
constexpr uint32_t kP_State = 0x15C, kP_Flag162 = 0x162, kP_Flag163 = 0x163, kP_Pos = 0x150;
constexpr uint32_t kM_Ready = 0x231, kM_Busy = 0x174, kM_TimerOn = 0x2D4, kM_Timer = 0x2D0;
constexpr uint32_t kM_List1 = 0x1B0, kM_List1N = 0x1B8, kM_List2 = 0x1C0, kM_List2N = 0x1C8;
constexpr uint32_t kE_Flag28C = 0x28C, kE_Mode60 = 0x60, kE_State61 = 0x61, kE_Box = 0xD8;
constexpr uintptr_t kTimerThreshold = 0x6CC8898;  // f32 DAT_146cc8898 (.rdata)
constexpr uint32_t kActor_Key = 0x60, kActor_Control = 0x90, kActor_User = 0xA0, kCc_Actor = 0x08;
constexpr uint32_t kCc_Fallback0 = 0x70, kLayer_Chart = 0x110;
constexpr uint32_t kComp_Key2D8 = 0x2D8, kComp_Key2E0 = 0x2E0;
constexpr size_t kList1Read = 32;  // entries of M+0x1B0 read per frame (the rest: "truncated")

// ---------------------------------------------------------------- events and limits

enum Kind : uint16_t {
    kKPred1 = 0, kKPred2, kKH2322, kKW2322, kKOwner, kKEmit, kKUi,
    kKFind, kKRestart, kKBodyAnim, kKInstall70, kKInstall78, kKBind, kKSeq, kKindCount
};
constexpr const char* kKindName[kKindCount] = {
    "loading 0x779E48", "loading 0x779E77", "2322 entry 0xB87890", "2322 write 0xB87A67", "owner 0x2BC8470",
    "2322 path 0x287F1CC", "ExtraReward 0xE919C0", "Find 0x24314C0", "Restart 0x86392A", "BodyAnim 0xA79770",
    "install +0x70 0xA79BAC", "install +0x78 0xA79F4C", "binding 0x9ED431", "instance 0x9ED47F"};

constexpr uint64_t kWindowUs = 5'000'000;
constexpr uint32_t kPerWindow = 256;   // events per point per 5 s
constexpr uint32_t kFirstPerKey = 2;   // S1: the first events of every actor key
constexpr uint32_t kFirstPerKeyBody = 4;
constexpr uint32_t kFirstPerKeyInstall = 8;
constexpr uint64_t kMaxLinesPerKind = 4000;  // formatted lines per point for the whole run

struct KindStats {
    std::atomic<uint64_t> hits{0}, queued{0}, limited{0}, dropped{0}, over_budget{0};
};

uintptr_t g_base = 0, g_image_end = 0;
// Read once when the mod loads (QueryPerformanceFrequency is fixed at boot).
const uint64_t g_qpc_freq = [] {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return static_cast<uint64_t>(f.QuadPart);
}();
std::vector<gad::Section> g_secs;
std::atomic<bool> g_started{false}, g_host{false}, g_load{false}, g_s1{false};
// P8 S1 option A (guest_seq_route.h): every point the probe thread installs is in place (or refused).
std::atomic<bool> g_settled{false};
p::EventRing<8192> g_ring;
KindStats g_stats[kKindCount];
p::WindowLimiter g_lim[kKindCount] = {
    {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs},
    {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs},
    {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs},
    {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}};
std::atomic<uint64_t> g_lines[kKindCount] = {};  // written by the drain thread
p::ChangeGate g_gate_pred1, g_gate_pred2, g_gate_ui;
p::KeyCounter<8192> g_keys_restart, g_keys_find, g_keys_body, g_keys_install, g_keys_bind, g_keys_seq;
std::atomic<uintptr_t> g_last_m{0}, g_last_p{0};  // the M / P 0x779E48 saw last
std::atomic<uint64_t> g_ui_other_vt{0}, g_ui_other_calls{0}, g_find_other_hash{0};

thread_local uintptr_t t_pred_p = 0, t_pred_m = 0;  // 0x779E48 -> 0x779E77, same frame
thread_local uint32_t t_2322_key = 0;               // 0xB87890 -> 0xB87A67, same handler call

struct Installed {
    const char* name;
    bool wanted = false, on = false;
};
Installed g_points[kKindCount] = {};
SafetyHookMid g_hooks[kKindCount];

// ---------------------------------------------------------------- hot-path helpers (no CRT, no allocation)

template <class T>
bool rd(uintptr_t a, T& v) noexcept {
    if (a < 0x10000) return false;
    __try {
        v = *reinterpret_cast<const volatile T*>(a);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t rdp(uintptr_t a) noexcept {
    uintptr_t v = 0;
    return rd(a, v) ? v : 0;
}

template <class T>
T rdv(uintptr_t a, T fallback = T{}) noexcept {
    T v{};
    return rd(a, v) ? v : fallback;
}

struct HookMem {
    bool u32(uintptr_t a, uint32_t& v) const noexcept { return rd(a, v); }
    bool ptr(uintptr_t a, uintptr_t& v) const noexcept { return rd(a, v); }
};

uint64_t now_us() noexcept {
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return p::qpc_to_us(static_cast<uint64_t>(q.QuadPart), g_qpc_freq);
}

uint32_t key_of_actor(uintptr_t actor) noexcept { return actor ? rdv<uint32_t>(actor + kActor_Key) : 0; }

uint64_t bits(float f) noexcept { return std::bit_cast<uint32_t>(f); }

// Counts the hit, applies the point's window limit and queues the event.
void emit(Kind k, p::Event& e) noexcept {
    e.kind = k;
    if (!e.qpc_us) e.qpc_us = now_us();
    e.tid = GetCurrentThreadId();
    if (!g_lim[k].take(e.qpc_us)) {
        g_stats[k].limited.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (g_ring.push(e)) {
        g_stats[k].queued.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_stats[k].dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

uint64_t epoch_now() noexcept { return g_host.load(std::memory_order_relaxed) ? 0 : guest_connection_epoch(); }

// ---------------------------------------------------------------- S2, guest

// The loading state's M fields, read the same way at both predicates.
struct MRead {
    bool ok = false;
    uint8_t b231 = 0, b2d4 = 0;
    int32_t i174 = 0;
    float f2d0 = 0.0f, thr = 0.0f;
    uintptr_t l1 = 0, l2 = 0;
    uint32_t l1n = 0, l2n = 0;
};

MRead read_m(uintptr_t m) noexcept {
    MRead r;
    r.ok = rd(m + kM_Ready, r.b231) && rd(m + kM_Busy, r.i174) && rd(m + kM_TimerOn, r.b2d4) &&
           rd(m + kM_Timer, r.f2d0) && rd(m + kM_List1, r.l1) && rd(m + kM_List1N, r.l1n) &&
           rd(m + kM_List2, r.l2) && rd(m + kM_List2N, r.l2n);
    rd(g_base + kTimerThreshold, r.thr);
    return r;
}

void on_pred1(safetyhook::Context& ctx) noexcept {
    g_stats[kKPred1].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t pp = ctx.rdi, m = ctx.rax;
    t_pred_p = pp;
    t_pred_m = m;
    g_last_m.store(m, std::memory_order_relaxed);
    g_last_p.store(pp, std::memory_order_relaxed);
    uint8_t state = 0, f162 = 0, f163 = 0;
    const bool p_ok = rd(pp + kP_State, state) && rd(pp + kP_Flag162, f162) && rd(pp + kP_Flag163, f163);
    const MRead r = read_m(m);
    float pos[3] = {};
    rd(pp + kP_Pos, pos[0]);
    rd(pp + kP_Pos + 4, pos[1]);
    rd(pp + kP_Pos + 8, pos[2]);
    const bool timer_blocks = r.b2d4 != 0 && r.f2d0 < r.thr;
    uint64_t sig = p::mix(0, m);
    sig = p::mix(sig, static_cast<uint64_t>(state) | static_cast<uint64_t>(f162) << 8 | static_cast<uint64_t>(f163) << 16 |
                          static_cast<uint64_t>(r.b231) << 24 | static_cast<uint64_t>(r.b2d4) << 32 |
                          static_cast<uint64_t>(timer_blocks) << 40 | static_cast<uint64_t>(p_ok) << 41 |
                          static_cast<uint64_t>(r.ok) << 42);
    sig = p::mix(sig, static_cast<uint32_t>(r.i174));
    sig = p::mix(sig, static_cast<uint64_t>(r.l1n) | static_cast<uint64_t>(r.l2n) << 32);
    if (!g_gate_pred1.changed(sig)) return;
    p::Event e;
    e.v[0] = pp;
    e.v[1] = m;
    e.v[2] = static_cast<uint64_t>(state) | static_cast<uint64_t>(f162) << 8 | static_cast<uint64_t>(f163) << 16 |
             static_cast<uint64_t>(r.b231) << 24 | static_cast<uint64_t>(r.b2d4) << 32 |
             static_cast<uint64_t>(p_ok) << 40 | static_cast<uint64_t>(r.ok) << 41;
    e.v[3] = static_cast<uint32_t>(r.i174) | bits(r.f2d0) << 32;
    e.v[4] = r.l1;
    e.v[5] = r.l1n | static_cast<uint64_t>(r.l2n) << 32;
    e.v[6] = r.l2;
    e.v[7] = bits(pos[0]) | bits(pos[1]) << 32;
    e.v[8] = bits(pos[2]) | bits(r.thr) << 32;
    e.v[9] = epoch_now();
    emit(kKPred1, e);
}

void on_pred2(safetyhook::Context& ctx) noexcept {
    g_stats[kKPred2].hits.fetch_add(1, std::memory_order_relaxed);
    const bool al = (ctx.rax & 0xFF) != 0;
    const uintptr_t pp = ctx.rdi;
    const uintptr_t m = t_pred_p == pp ? t_pred_m : 0;
    p::LoadSnapshot s;
    const MRead r = m ? read_m(m) : MRead{};
    s.m_ok = r.ok;
    s.b231 = r.b231;
    s.i174 = r.i174;
    s.b2d4 = r.b2d4;
    s.f2d0 = r.f2d0;
    s.threshold = r.thr;
    s.list1_count = r.l1n;
    s.list2_count = r.l2n;
    rd(pp + kP_Pos, s.pos[0]);
    rd(pp + kP_Pos + 4, s.pos[1]);
    rd(pp + kP_Pos + 8, s.pos[2]);
    p::List1Entry entries[kList1Read];  // POD: only the first n are written and read
    uintptr_t entry_ptr[kList1Read];
    const size_t n = r.ok ? std::min<size_t>(r.l1n, kList1Read) : 0;
    for (size_t i = 0; i < n; ++i) {
        p::List1Entry& en = entries[i];
        en.flag28c = en.mode60 = en.state61 = 0;
        for (uint32_t b = 0; b < 6; ++b) en.box[b] = 0.0f;
        const uintptr_t ep = rdp(r.l1 + 8 * i);
        entry_ptr[i] = ep;
        en.readable = ep && rd(ep + kE_Flag28C, en.flag28c) && rd(ep + kE_Mode60, en.mode60) &&
                      rd(ep + kE_State61, en.state61);
        for (uint32_t b = 0; en.readable && b < 6; ++b) en.readable = rd(ep + kE_Box + 4 * b, en.box[b]);
    }
    s.list1 = std::span<const p::List1Entry>(entries, n);
    const p::Attribution a = p::attribute_a97d60(s, al);
    uint64_t sig = p::mix(0, m);
    sig = p::mix(sig, static_cast<uint64_t>(al) | static_cast<uint64_t>(a.blocker) << 8 |
                          static_cast<uint64_t>(static_cast<uint32_t>(a.index)) << 16);
    sig = p::mix(sig, static_cast<uint64_t>(a.unknown_before) | static_cast<uint64_t>(r.l1n) << 20 |
                          static_cast<uint64_t>(r.l2n) << 40);
    if (!g_gate_pred2.changed(sig) && !al) return;
    p::Event e;
    e.v[0] = pp;
    e.v[1] = m;
    e.v[2] = static_cast<uint64_t>(al) | static_cast<uint64_t>(a.blocker) << 8 |
             static_cast<uint64_t>(a.list1_truncated) << 16 | static_cast<uint64_t>(r.b231) << 24;
    e.v[3] = static_cast<uint32_t>(a.index) | static_cast<uint64_t>(a.unknown_before) << 32;
    e.v[4] = r.l1n | static_cast<uint64_t>(r.l2n) << 32;
    if (a.index >= 0 && static_cast<size_t>(a.index) < n) {
        const p::List1Entry& en = entries[a.index];
        e.v[5] = entry_ptr[a.index];
        e.v[6] = en.flag28c | static_cast<uint64_t>(en.mode60) << 8 | static_cast<uint64_t>(en.state61) << 16;
    }
    e.v[7] = static_cast<uint32_t>(r.i174) | bits(r.f2d0) << 32;
    e.v[8] = r.b2d4 | bits(r.thr) << 32;
    e.v[9] = bits(s.pos[0]) | bits(s.pos[1]) << 32;
    e.v[10] = bits(s.pos[2]) | static_cast<uint64_t>(rdv<uint8_t>(pp + kP_State)) << 32;
    e.v[11] = epoch_now();
    emit(kKPred2, e);
}

void on_h2322(safetyhook::Context& ctx) noexcept {
    g_stats[kKH2322].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t wrapper = ctx.r8;
    const uintptr_t raw = rdp(wrapper + 0x18);
    const uint16_t size = rdv<uint16_t>(wrapper + 0x10);
    const uint16_t op = native_opcode::decode(game::current_build(), rdv<uint16_t>(raw)).value_or(0);
    const uint16_t len = rdv<uint16_t>(raw + 3);
    uint32_t key = 0;
    const bool key_ok = rd(raw + 5, key);
    const uint8_t disabled = rdv<uint8_t>(ctx.rcx + 0x21, 0xFF);
    t_2322_key = key;
    p::Event e;
    e.v[0] = ctx.rcx;
    e.v[1] = wrapper;
    e.v[2] = raw;
    e.v[3] = key | static_cast<uint64_t>(op) << 32 | static_cast<uint64_t>(len) << 48;
    e.v[4] = size | static_cast<uint64_t>(disabled) << 16 | static_cast<uint64_t>(key_ok) << 24;
    e.v[5] = epoch_now();
    emit(kKH2322, e);
}

void on_w2322(safetyhook::Context& ctx) noexcept {
    // P8 S1 option A (guest_seq_route.h): one hook at this address; the routing (a 2322 naming another
    // character may write into THAT character's manager) runs first, so this line shows the manager written.
    guest_seq_route_hook(gsr::Point::P2322, ctx);
    g_stats[kKW2322].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t m = ctx.rax;
    uint8_t prev = 0;
    const bool prev_ok = rd(m + kM_Ready, prev);
    p::Event e;
    e.v[0] = m;
    e.v[1] = prev | static_cast<uint64_t>(prev_ok) << 8;
    e.v[2] = g_last_m.load(std::memory_order_relaxed);
    e.v[3] = t_2322_key;
    e.v[4] = epoch_now();
    const uintptr_t pp = g_last_p.load(std::memory_order_relaxed);
    e.v[5] = pp ? rdv<uint8_t>(pp + kP_State, 0xFF) : 0xFF;
    t_2322_key = 0;
    emit(kKW2322, e);
}

// ---------------------------------------------------------------- S2, host

void on_owner(safetyhook::Context& ctx) noexcept {
    g_stats[kKOwner].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t actor = ctx.r14;
    const uintptr_t user = rdp(actor + kActor_User);
    p::Event e;
    e.v[0] = static_cast<uint32_t>(ctx.rax) | static_cast<uint64_t>(static_cast<uint32_t>(ctx.r8)) << 32;
    e.v[1] = ctx.r13;
    e.v[2] = actor;
    e.v[3] = key_of_actor(actor) | static_cast<uint64_t>(key_of_actor(user)) << 32;
    e.v[4] = user;
    emit(kKOwner, e);
}

void on_emit(safetyhook::Context& ctx) noexcept {
    g_stats[kKEmit].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t actor = ctx.r14;
    const uintptr_t user = rdp(actor + kActor_User);
    p::Event e;
    e.v[0] = actor;
    e.v[1] = key_of_actor(actor) | static_cast<uint64_t>(key_of_actor(user)) << 32;
    e.v[2] = ctx.rsp + 0x50;  // the finder 0x2999D80 receives in rdx
    e.v[3] = rdv<uint32_t>(actor + kActor_Control);
    emit(kKEmit, e);
}

void on_ui(safetyhook::Context& ctx) noexcept {
    const uintptr_t self = ctx.rcx;
    const uintptr_t vt = rdp(self);
    if (vt != g_base + kUiVtable) {
        g_ui_other_calls.fetch_add(1, std::memory_order_relaxed);
        g_ui_other_vt.store(vt, std::memory_order_relaxed);
        return;
    }
    g_stats[kKUi].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t ev = ctx.r8;
    const uint8_t r9b = static_cast<uint8_t>(ctx.r9);
    const uintptr_t c160 = rdp(self + kUiClose), c168 = rdp(self + kUiOther);
    const bool close = r9b == 0 && ev && ev == c160;
    const bool other = r9b == 0 && ev && ev == c168;
    const uint64_t sig = p::mix(p::mix(0, ev), r9b);
    if (!close && !other && !g_gate_ui.changed(sig)) return;
    p::Event e;
    e.v[0] = self;
    e.v[1] = ev;
    e.v[2] = r9b | static_cast<uint64_t>(close) << 8 | static_cast<uint64_t>(other) << 9;
    e.v[3] = c160;
    e.v[4] = c168;
    e.v[5] = ctx.rdx;
    emit(kKUi, e);
}

// ---------------------------------------------------------------- S1, host and guest

void fill_triple(p::Event& e, size_t at, const uintptr_t (&t)[3]) noexcept {
    e.v[at] = t[0];
    e.v[at + 1] = t[1];
    e.v[at + 2] = t[2];
}

// 0x24314C0 is ActionGraph_Find (81 callers): every action lookup of the
// process. An INLINE hook (not a mid hook: no context save per call) whose
// detour only counts the call in a per-thread shard and compares EDX with the
// two S1 hashes before calling the original through its trampoline with the
// four argument registers untouched (the function takes rcx graph, edx hash,
// r8 out; r9 is overwritten at 0x24314DD, no stack arguments: its body only
// writes its home slots [rsp+8..0x18]; the relocated prologue is the single
// instruction mov [rsp+8],rbx, checked whole). The probe runs only for those
// two hashes.
SafetyHookInline g_find_hook;
struct CallShard {
    std::atomic<uint64_t> n{0};
    char pad[56];  // one cache line each
};
CallShard g_find_calls[64];  // approximate: load + store (no lock), shard = thread id

uint64_t find_calls() {
    uint64_t n = 0;
    for (const CallShard& s : g_find_calls) n += s.n.load(std::memory_order_relaxed);
    return n;
}

void on_find(uintptr_t graph, uint32_t hash, uintptr_t out, uintptr_t ret) noexcept {
    g_stats[kKFind].hits.fetch_add(1, std::memory_order_relaxed);
    const p::FindResult fr = p::graph_find(HookMem{}, graph, hash);
    // The first events of each (graph, caller); every miss while the window lasts.
    const uint32_t k = static_cast<uint32_t>(p::mix(p::mix(0, graph), ret)) | 1u;
    if (fr.found && !p::within_key_budget(g_keys_find.bump(k), kFirstPerKey)) {
        g_stats[kKFind].over_budget.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    p::Event e;
    e.v[0] = graph;
    e.v[1] = hash | static_cast<uint64_t>(ret >= g_base && ret < g_image_end ? ret - g_base : 0) << 32;
    e.v[2] = out;
    e.v[3] = fr.count | static_cast<uint64_t>(fr.found) << 32 | static_cast<uint64_t>(fr.read_ok) << 33 |
             static_cast<uint64_t>(fr.iterations) << 40;
    e.v[4] = fr.table;
    e.v[5] = static_cast<uint32_t>(fr.index);
    fill_triple(e, 6, fr.triple);
    emit(kKFind, e);
}

uintptr_t __fastcall find_detour(uintptr_t graph, uintptr_t rdx, uintptr_t out, uintptr_t r9) noexcept {
    CallShard& s = g_find_calls[(__readgsdword(0x48) >> 2) & 63];  // TEB ClientId.UniqueThread
    s.n.store(s.n.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    const uint32_t hash = static_cast<uint32_t>(rdx);
    if (p::s1_hash(hash)) {  // the filter: two compares
        on_find(graph, hash, out, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    }
    return g_find_hook.unsafe_call<uintptr_t>(graph, rdx, out, r9);
}

void on_restart(safetyhook::Context& ctx) noexcept {
    const uint32_t hash = static_cast<uint32_t>(ctx.rsi);
    if (!hash) return;  // je 0x86392A at 0x8638F8: no lookup, nothing to say
    g_stats[kKRestart].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t cc = ctx.r15, slot = ctx.rdi;
    const uintptr_t actor = rdp(cc + kCc_Actor);
    const uint32_t key = key_of_actor(actor), control = actor ? rdv<uint32_t>(actor + kActor_Control) : 0;
    const uint64_t layer = slot >= cc + kCc_Fallback0 ? (slot - cc - kCc_Fallback0) / 8 : 0xFF;
    const uintptr_t layer_obj = rdp(slot + 0x18);
    const uintptr_t chart = layer_obj ? rdp(layer_obj + kLayer_Chart) : 0;
    const uintptr_t fallback = rdp(slot);
    const p::FindResult primary = chart ? p::graph_find(HookMem{}, chart, hash) : p::FindResult{};
    const p::FindResult fb = (!(chart && primary.found) && fallback) ? p::graph_find(HookMem{}, fallback, hash)
                                                                    : p::FindResult{};
    const p::RestartPath path = p::classify_restart(hash, chart != 0, primary, fallback != 0, fb);
    uintptr_t t[3] = {rdp(ctx.rbx), rdp(ctx.rbx + 8), rdp(ctx.rbx + 0x10)};
    const bool complete = p::triple_complete(t);
    const uint32_t k = static_cast<uint32_t>(p::mix(key, layer)) | 1u;
    if (complete && !p::within_key_budget(g_keys_restart.bump(k), kFirstPerKey)) {
        g_stats[kKRestart].over_budget.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    p::Event e;
    e.v[0] = cc;
    e.v[1] = key | static_cast<uint64_t>(control) << 32;
    e.v[2] = hash | (layer & 0xFF) << 32 | static_cast<uint64_t>(path) << 40 | static_cast<uint64_t>(complete) << 48;
    e.v[3] = chart;
    e.v[4] = fallback;
    e.v[5] = primary.count | static_cast<uint64_t>(fb.count) << 32;
    fill_triple(e, 6, t);
    e.v[9] = static_cast<uint32_t>(primary.index) | static_cast<uint64_t>(static_cast<uint32_t>(fb.index)) << 32;
    e.v[10] = epoch_now();
    emit(kKRestart, e);
}

void on_bodyanim(safetyhook::Context& ctx) noexcept {
    g_stats[kKBodyAnim].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t context = ctx.rcx, desc = ctx.rdx;
    const uintptr_t cc = rdp(context);
    const uintptr_t actor = cc ? rdp(cc + kCc_Actor) : 0;
    const uint32_t key = key_of_actor(actor);
    if (!p::within_key_budget(g_keys_body.bump(key ? key : 1u), kFirstPerKeyBody)) {
        g_stats[kKBodyAnim].over_budget.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    p::Event e;
    e.v[0] = context;
    e.v[1] = cc;
    e.v[2] = key | static_cast<uint64_t>(actor ? rdv<uint32_t>(actor + kActor_Control) : 0) << 32;
    e.v[3] = desc;
    e.v[4] = rdp(desc + 0x18);
    e.v[5] = rdv<uint32_t>(desc + 0x28) | static_cast<uint64_t>(rdv<uint32_t>(desc + 0x30)) << 32;
    e.v[6] = rdv<uint32_t>(desc + 0x3C) | static_cast<uint64_t>(ctx.r8 & 0xFF) << 32 |
             static_cast<uint64_t>(ctx.r9 & 0xFF) << 40;
    e.v[7] = cc ? rdp(cc + 0x70) : 0;
    e.v[8] = cc ? rdp(cc + 0x78) : 0;
    e.v[9] = epoch_now();
    emit(kKBodyAnim, e);
}

void on_install(safetyhook::Context& ctx, Kind kind, uint32_t off) noexcept {
    g_stats[kind].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t slot = ctx.rbx, graph = ctx.rcx, context = ctx.rsi;
    const uintptr_t cc = rdp(context);
    const uintptr_t actor = cc ? rdp(cc + kCc_Actor) : 0;
    const uint32_t key = key_of_actor(actor);
    if (!p::within_key_budget(g_keys_install.bump(static_cast<uint32_t>(p::mix(key, off)) | 1u),
                              kFirstPerKeyInstall)) {
        g_stats[kind].over_budget.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const p::FindResult a = p::graph_find(HookMem{}, graph, p::kHashSeqBodyAnim);
    const p::FindResult b = p::graph_find(HookMem{}, graph, p::kHashSeqBodyAnimLoop);
    p::Event e;
    e.v[0] = slot;
    e.v[1] = graph;
    e.v[2] = context;
    e.v[3] = cc;
    e.v[4] = key | static_cast<uint64_t>(actor ? rdv<uint32_t>(actor + kActor_Control) : 0) << 32;
    e.v[5] = rdp(slot);  // what the slot holds before the write
    e.v[6] = a.count | static_cast<uint64_t>(a.found) << 32 | static_cast<uint64_t>(b.found) << 33 |
             static_cast<uint64_t>(cc && slot == cc + off) << 34 | static_cast<uint64_t>(a.read_ok) << 35;
    e.v[7] = epoch_now();
    emit(kind, e);
}

void on_install70(safetyhook::Context& ctx) noexcept { on_install(ctx, kKInstall70, 0x70); }
void on_install78(safetyhook::Context& ctx) noexcept { on_install(ctx, kKInstall78, 0x78); }

void on_bind(safetyhook::Context& ctx) noexcept {
    g_stats[kKBind].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t comp = ctx.r14, rbp = ctx.rbp;
    const uintptr_t own = rdp(comp + kCc_Actor);
    const uint32_t own_key = key_of_actor(own);
    uint8_t found = 0;
    const bool found_ok = rd(rbp - 0x18, found);
    const uintptr_t bound = rdp(rbp - 0x20);
    if (found && !p::within_key_budget(g_keys_bind.bump(own_key ? own_key : 1u), kFirstPerKey)) {
        g_stats[kKBind].over_budget.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    p::Event e;
    e.v[0] = comp;
    e.v[1] = own_key | static_cast<uint64_t>(rdv<uint32_t>(comp + kComp_Key2D8)) << 32;
    e.v[2] = rdv<uint32_t>(comp + kComp_Key2E0) | static_cast<uint64_t>(found) << 32 |
             static_cast<uint64_t>(found_ok) << 40;
    e.v[3] = bound;
    e.v[4] = found ? key_of_actor(bound) : 0;
    e.v[5] = epoch_now();
    emit(kKBind, e);
}

void on_seq(safetyhook::Context& ctx) noexcept {
    g_stats[kKSeq].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t comp = ctx.r14;
    const uintptr_t inst = rdp(ctx.rbp - 0x58);
    const uint32_t own_key = key_of_actor(rdp(comp + kCc_Actor));
    if (inst && !p::within_key_budget(g_keys_seq.bump(own_key ? own_key : 1u), kFirstPerKey)) {
        g_stats[kKSeq].over_budget.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    p::Event e;
    e.v[0] = comp;
    e.v[1] = own_key | static_cast<uint64_t>(rdv<uint32_t>(comp + kComp_Key2E0)) << 32;
    e.v[2] = inst;
    e.v[3] = epoch_now();
    emit(kKSeq, e);
}

// ---------------------------------------------------------------- formatting (the drain thread only)

std::string rva(uintptr_t a) {
    if (a >= g_base && a < g_image_end) return std::format("exe+0x{:X}", a - g_base);
    return std::format("0x{:X}", a);
}

float fl(uint64_t b) { return std::bit_cast<float>(static_cast<uint32_t>(b)); }

// "qpc 123.456789 s (~hh:mm:ss.mmm) capture t=12.345678": the event's QPC time,
// the local wall clock it corresponds to and the sniffer's "t" column.
std::string when(uint64_t qpc_us) {
    const uint64_t now = now_us();
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    if (now >= qpc_us) u.QuadPart -= (now - qpc_us) * 10;
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    FILETIME lft{};
    SYSTEMTIME st{};
    FileTimeToLocalFileTime(&ft, &lft);
    FileTimeToSystemTime(&lft, &st);
    const uint64_t t0 = net::sniffer_first_packet_us();
    return std::format("qpc {}.{:06} s (~{:02}:{:02}:{:02}.{:03}) capture t={}", qpc_us / 1000000, qpc_us % 1000000,
                       st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                       t0 && qpc_us >= t0 ? std::format("{:.6f}", static_cast<double>(qpc_us - t0) / 1e6)
                                          : std::string("-"));
}

const char* hash_name(uint32_t h) {
    return h == p::kHashSeqBodyAnim ? "seq_bodyanim_action_xxx"
           : h == p::kHashSeqBodyAnimLoop ? "seq_bodyanim_action_loop_xxx"
                                          : "other";
}

std::string format_event(const p::Event& e) {
    const uint64_t* v = e.v;
    const std::string head = std::format("P8Probe [{}] {} T{} |", kKindName[e.kind], when(e.qpc_us), e.tid);
    switch (e.kind) {
        case kKPred1: {
            const bool p_ok = (v[2] >> 40) & 1, m_ok = (v[2] >> 41) & 1;
            return std::format(
                "{} S2 state [P+0x15C] 0x{:02X} P 0x{:X} M 0x{:X} | M+0x231 {} (needs >= 2) | M+0x174 {} | M+0x2D4 {} "
                "M+0x2D0 {:.3f} (threshold {:.3f}) | list M+0x1B0 n {} (0x{:X}) | list M+0x1C0 n {} (0x{:X}) | P+0x162 {} "
                "P+0x163 {} | pos ({:.2f}, {:.2f}, {:.2f}) | epoch 0x{:X}{}",
                head, v[2] & 0xFF, v[0], v[1], (v[2] >> 24) & 0xFF, static_cast<int32_t>(v[3] & 0xFFFFFFFF),
                (v[2] >> 32) & 0xFF, fl(v[3] >> 32), fl(v[8] >> 32), v[5] & 0xFFFFFFFF, v[4], v[5] >> 32, v[6],
                (v[2] >> 8) & 0xFF, (v[2] >> 16) & 0xFF, fl(v[7]), fl(v[7] >> 32), fl(v[8]), v[9],
                !p_ok ? " | P UNREADABLE" : !m_ok ? " | M UNREADABLE" : "");
        }
        case kKPred2: {
            const auto blocker = static_cast<p::Blocker>((v[2] >> 8) & 0xFF);
            const int32_t index = static_cast<int32_t>(v[3] & 0xFFFFFFFF);
            std::string entry;
            if (index >= 0) {
                entry = std::format(" [entry #{} 0x{:X}: +0x28C {} +0x60 {} +0x61 {}]", index, v[5], v[6] & 0xFF,
                                    (v[6] >> 8) & 0xFF, (v[6] >> 16) & 0xFF);
            }
            return std::format(
                "{} S2 0xA97D60(M, pos) -> {} | first certain blocker: {}{}{} | list M+0x1B0 entries before it "
                "undetermined (0x1AC2830): {} | lists {}/{} | M 0x{:X} M+0x231 {} M+0x174 {} M+0x2D4 {} M+0x2D0 {:.3f} "
                "(threshold {:.3f}) | state 0x{:02X} | pos ({:.2f}, {:.2f}, {:.2f}) | epoch 0x{:X}",
                head, v[2] & 1, p::blocker_name(blocker), entry, ((v[2] >> 16) & 1) ? " (list read TRUNCATED)" : "",
                v[3] >> 32, v[4] & 0xFFFFFFFF, v[4] >> 32, v[1], (v[2] >> 24) & 0xFF,
                static_cast<int32_t>(v[7] & 0xFFFFFFFF), v[8] & 0xFF, fl(v[7] >> 32), fl(v[8] >> 32), (v[10] >> 32) & 0xFF,
                fl(v[9]), fl(v[9] >> 32), fl(v[10]), v[11]);
        }
        case kKH2322: {
            const uint8_t disabled = (v[4] >> 16) & 0xFF;
            return std::format(
                "{} S2 PlayAllPreLoadSequencerAck 2322 handler: actor key {} | opcode {} payload len {} size {} | handler "
                "+0x21 {} ({}) | wrapper 0x{:X} raw 0x{:X} | epoch 0x{:X}",
                head, ((v[4] >> 24) & 1) ? std::format("0x{:08X}", v[3] & 0xFFFFFFFF) : std::string("UNREADABLE"),
                (v[3] >> 32) & 0xFFFF, v[3] >> 48, v[4] & 0xFFFF, disabled,
                disabled == 0 ? "handled" : disabled == 0xFF ? "unreadable" : "the handler skips it", v[1], v[2], v[5]);
        }
        case kKW2322: {
            const bool same = v[0] == v[2];
            return std::format(
                "{} S2 [M+0x231] = 2 (0xB87A67): M 0x{:X}, byte before {} | M at 0x779E48 0x{:X} ({}) | 2322 key 0x{:08X} "
                "| loading state 0x{:02X} | epoch 0x{:X}",
                head, v[0], ((v[1] >> 8) & 1) ? std::to_string(v[1] & 0xFF) : std::string("unreadable"), v[2],
                !v[2] ? "never seen" : same ? "SAME manager" : "DIFFERENT manager", v[3] & 0xFFFFFFFF, v[5] & 0xFF,
                v[4]);
        }
        case kKOwner: {
            const uint32_t user_key = v[0] & 0xFFFFFFFF, owner = static_cast<uint32_t>(v[0] >> 32);
            return std::format(
                "{} S2 host MoveFieldCompleteCore: traveller actor 0x{:08X} (0x{:X}) user 0x{:08X} | userKey(EAX) "
                "0x{:08X} field owner [field+0x180](R8D) 0x{:08X} field 0x{:X} -> {}",
                head, v[3] & 0xFFFFFFFF, v[2], v[3] >> 32, user_key, owner, v[1],
                user_key == owner ? "OWNER: stage/sequencer preparation runs (0x2BC84D4 call 0x287EF60)"
                                  : "NOT the owner: jne 0x2BC84D9 skips the preparation and its 2322");
        }
        case kKEmit:
            return std::format(
                "{} S2 host 2322 emission path (0x287F1D4 call 0x2999D80): target actor 0x{:08X} (0x{:X}) user "
                "0x{:08X} control 0x{:08X} | finder 0x{:X}",
                head, v[1] & 0xFFFFFFFF, v[0], v[1] >> 32, v[3] & 0xFFFFFFFF, v[2]);
        case kKUi: {
            const bool close = (v[2] >> 8) & 1, other = (v[2] >> 9) & 1;
            return std::format(
                "{} S2 host ExtraReward UI event: this 0x{:X} event object R8 0x{:X} R9B {} | [this+0x160] 0x{:X} "
                "[this+0x168] 0x{:X} -> {}",
                head, v[0], v[1], v[2] & 0xFF, v[3], v[4],
                close ? "CLOSE branch (vf+0x28)" : other ? "the +0x168 button branch" : "other event");
        }
        case kKFind: {
            const uint32_t hash = v[1] & 0xFFFFFFFF, caller = static_cast<uint32_t>(v[1] >> 32);
            const bool found = (v[3] >> 32) & 1, ok = (v[3] >> 33) & 1;
            return std::format(
                "{} S1 ActionGraph_Find: hash 0x{:08X} ({}) graph 0x{:X} count {} table 0x{:X} caller {} | replay: "
                "{} index {} triple (0x{:X}, 0x{:X}, 0x{:X}) in {} steps",
                head, hash, hash_name(hash), v[0], v[3] & 0xFFFFFFFF, v[4],
                caller == kFindRetPrimary   ? "RestartLayerAction primary (exe+0x863914)"
                : caller == kFindRetFallback ? "RestartLayerAction fallback (exe+0x86392A)"
                : caller                     ? std::format("exe+0x{:X}", caller)
                                             : std::string("?"),
                !ok ? "UNREADABLE" : found ? "found" : "NOT FOUND", static_cast<int32_t>(v[5] & 0xFFFFFFFF), v[6],
                v[7], v[8], (v[3] >> 40) & 0xFF);
        }
        case kKRestart: {
            const uint32_t hash = v[2] & 0xFFFFFFFF;
            const uint32_t layer = (v[2] >> 32) & 0xFF;
            const auto path = static_cast<p::RestartPath>((v[2] >> 40) & 0xFF);
            const bool complete = (v[2] >> 48) & 1;
            return std::format(
                "{} S1 RestartLayerAction: actor 0x{:08X} control 0x{:08X} CC 0x{:X} layer {} hash 0x{:08X} ({}) -> {} "
                "| layer chart 0x{:X} (count {}) fallback [CC+0x{:X}] 0x{:X} (count {}) | triple {} (0x{:X}, 0x{:X}, "
                "0x{:X}) | epoch 0x{:X}",
                head, v[1] & 0xFFFFFFFF, v[1] >> 32, v[0], layer, hash, hash_name(hash), p::restart_path_name(path),
                v[3], v[5] & 0xFFFFFFFF, 0x70 + 8 * layer, v[4], v[5] >> 32, complete ? "complete" : "INCOMPLETE",
                v[6], v[7], v[8], v[10]);
        }
        case kKBodyAnim:
            return std::format(
                "{} S1 BodyAnimation setup 0xA79770: actor 0x{:08X} control 0x{:08X} CC 0x{:X} context 0x{:X} | desc "
                "0x{:X} resource [desc+0x18] 0x{:X} f28 {:.3f} f30 {:.3f} f3C {:.3f} R8B {} R9B {} | before: CC+0x70 "
                "0x{:X} CC+0x78 0x{:X} | epoch 0x{:X}",
                head, v[2] & 0xFFFFFFFF, v[2] >> 32, v[1], v[0], v[3], v[4], fl(v[5]), fl(v[5] >> 32), fl(v[6]),
                (v[6] >> 32) & 0xFF, (v[6] >> 40) & 0xFF, v[7], v[8], v[9]);
        case kKInstall70:
        case kKInstall78: {
            const bool slot_ok = (v[6] >> 34) & 1, ok = (v[6] >> 35) & 1;
            return std::format(
                "{} S1 install CC+0x{:X}: actor 0x{:08X} control 0x{:08X} CC 0x{:X} slot 0x{:X} ({}) new graph 0x{:X} "
                "(count {}, seq_bodyanim_action_xxx {}, loop {}) slot held 0x{:X} | context 0x{:X} | epoch 0x{:X}",
                head, e.kind == kKInstall70 ? 0x70 : 0x78, v[4] & 0xFFFFFFFF, v[4] >> 32, v[3], v[0],
                slot_ok ? "= CC+off" : "NOT CC+off", v[1], v[6] & 0xFFFFFFFF,
                !ok ? "?" : ((v[6] >> 32) & 1) ? "yes" : "no", !ok ? "?" : ((v[6] >> 33) & 1) ? "yes" : "no", v[5],
                v[2], v[7]);
        }
        case kKBind: {
            const bool found = (v[2] >> 32) & 1, ok = (v[2] >> 40) & 1;
            return std::format(
                "{} S1 sequencer binding 0x9ED431: component 0x{:X} of actor 0x{:08X} | comp+0x2D8 0x{:08X} comp+0x2E0 "
                "0x{:08X} | binding actor {} | epoch 0x{:X}",
                head, v[0], v[1] & 0xFFFFFFFF, v[1] >> 32, v[2] & 0xFFFFFFFF,
                !ok ? std::string("UNREADABLE")
                : found ? std::format("found 0x{:X} key 0x{:08X}", v[3], v[4] & 0xFFFFFFFF)
                        : std::string("NOT FOUND ([RBP-0x18] = 0): success without binding"),
                v[5]);
        }
        case kKSeq:
            return std::format(
                "{} S1 sequencer instance 0x9ED47F: component 0x{:X} of actor 0x{:08X} comp+0x2E0 0x{:08X} | instance "
                "[RBP-0x58] {} | epoch 0x{:X}",
                head, v[0], v[1] & 0xFFFFFFFF, v[1] >> 32,
                v[2] ? std::format("0x{:X}", v[2]) : std::string("NULL: success without binding"), v[3]);
        default:
            return head + " ?";
    }
}

std::string counters() {
    std::string s = std::format("P8Probe {} | ring dropped {}", g_host.load() ? "host" : "guest", g_ring.dropped());
    for (int k = 0; k < kKindCount; ++k) {
        if (!g_points[k].wanted) continue;
        const KindStats& st = g_stats[k];
        s += std::format(" | {} {}: hits {} queued {} limited {} dropped {} per-key skipped {} lines {}",
                         kKindName[k], g_points[k].on ? "on" : "OFF", st.hits.load(), st.queued.load(),
                         st.limited.load(), st.dropped.load(), st.over_budget.load(), g_lines[k].load());
    }
    if (g_points[kKFind].on) {
        s += std::format(" | 0x24314C0 calls, every hash (approximate, the filter's load) {}", find_calls());
    }
    if (g_points[kKUi].wanted && g_ui_other_calls.load()) {
        s += std::format(" | 0xE919C0 with another vtable {} (last {})", g_ui_other_calls.load(),
                         rva(g_ui_other_vt.load()));
    }
    return s;
}

void drain() {
    p::Event e;
    uint64_t suppressed = 0;
    while (g_ring.pop(e)) {
        if (e.kind >= kKindCount) continue;
        if (g_lines[e.kind].fetch_add(1) + 1 > kMaxLinesPerKind) {
            ++suppressed;
            continue;
        }
        try {
            log::info("{}", format_event(e));
        } catch (...) {
        }
    }
    if (suppressed) log::info("P8Probe: {} lines over the per-point cap ({}) not written", suppressed, kMaxLinesPerKind);
}

// ---------------------------------------------------------------- sections, bytes, install

template <class T>
bool peek(uintptr_t a, T& v) {
    return a && game::safe_read(a, &v, sizeof v);
}

bool read_sections() {
    g_secs.clear();
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!peek(g_base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || !peek(g_base + dos.e_lfanew, nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    g_image_end = g_base + nt.OptionalHeader.SizeOfImage;
    const uintptr_t first = g_base + dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
                            nt.FileHeader.SizeOfOptionalHeader;
    for (WORD i = 0; i < nt.FileHeader.NumberOfSections && i < 64; ++i) {
        IMAGE_SECTION_HEADER h{};
        if (!peek(first + i * sizeof h, h)) return false;
        gad::Section s;
        std::memcpy(s.name, h.Name, 8);
        s.va = h.VirtualAddress;
        s.size = h.Misc.VirtualSize;
        s.flags = h.Characteristics;
        g_secs.push_back(s);
    }
    return !g_secs.empty();
}

bool bytes_at(uintptr_t base, uintptr_t rva, std::span<const uint8_t> expected) {
    std::vector<uint8_t> actual(expected.size());
    return game::safe_read(base + rva, actual.data(), actual.size()) &&
           std::memcmp(actual.data(), expected.data(), expected.size()) == 0;
}

struct Ctx {
    uintptr_t rva;
    std::span<const uint8_t> bytes;
};

struct PointDef {
    Kind kind;
    uintptr_t rva;
    uint32_t patch;
    std::span<const uint8_t> bytes;
    std::vector<Ctx> ctx;  // code the probe's register reading relies on (checked, never patched)
    safetyhook::MidHookFn fn;
    enum Group : uint8_t { GuestLoad, HostLoad, S1 } group;
};

std::vector<PointDef> points() {
    using G = PointDef;
    return {
        {kKPred1, kPred1, kPred1Patch, kPred1Bytes, {{kPred1Ctx, kPred1CtxBytes}}, on_pred1, G::GuestLoad},
        {kKPred2, kPred2, kPred2Patch, kPred2Bytes, {{kPred2Ctx, kPred2CtxBytes}}, on_pred2, G::GuestLoad},
        {kKH2322, kH2322, kH2322Patch, kH2322Bytes, {{kH2322Ctx, kH2322CtxBytes}, {kH2322Payload, kH2322PayloadBytes}},
         on_h2322, G::GuestLoad},
        {kKW2322, kW2322, kW2322Patch, kW2322Bytes, {{kW2322Ctx, kW2322CtxBytes}}, on_w2322, G::GuestLoad},
        {kKOwner, kOwner, kOwnerPatch, kOwnerBytes, {{kOwnerCtx, kOwnerCtxBytes}, {kOwnerPrep, kOwnerPrepBytes}},
         on_owner, G::HostLoad},
        {kKEmit, kEmit, kEmitPatch, kEmitBytes, {}, on_emit, G::HostLoad},
        {kKUi, kUi, kUiPatch, kUiBytes, {}, on_ui, G::HostLoad},
        {kKFind, kFind, kFindPatch, kFindBytes, {}, nullptr, G::S1},  // inline hook (install_point)
        {kKRestart, kRestart, kRestartPatch, kRestartBytes, {{kRestartLoop, kRestartLoopBytes}, {kRestartCc, kRestartCcBytes}},
         on_restart, G::S1},
        {kKBodyAnim, kBodyAnim, kBodyAnimPatch, kBodyAnimBytes, {{kBodyAnimArgs, kBodyAnimArgsBytes}}, on_bodyanim, G::S1},
        {kKInstall70, kInstall70, kInstall70Patch, kInstall70Bytes,
         {{kInstall70Ctx, kInstall70CtxBytes}, {kInstall70Load, kInstall70LoadBytes}, {kBodyAnimArgs, kBodyAnimArgsBytes}},
         on_install70, G::S1},
        {kKInstall78, kInstall78, kInstall78Patch, kInstall78Bytes,
         {{kInstall78Ctx, kInstall78CtxBytes}, {kInstall78Load, kInstall78LoadBytes}, {kBodyAnimArgs, kBodyAnimArgsBytes}},
         on_install78, G::S1},
        {kKBind, kBind, kBindPatch, kBindBytes,
         {{kBindFrame, kBindFrameBytes}, {kBindComp, kBindCompBytes}, {kBindCtx, kBindCtxBytes}}, on_bind, G::S1},
        {kKSeq, kSeq, kSeqPatch, kSeqBytes,
         {{kBindFrame, kBindFrameBytes}, {kBindComp, kBindCompBytes}, {kSeqCtx, kSeqCtxBytes}}, on_seq, G::S1},
    };
}

bool wanted(const PointDef& d) {
    switch (d.group) {
        case PointDef::GuestLoad: return g_load.load() && !g_host.load();
        case PointDef::HostLoad: return g_load.load() && g_host.load();
        case PointDef::S1: return g_s1.load();
    }
    return false;
}

bool all_bytes(const PointDef& d) {
    if (!bytes_at(g_base, d.rva, d.bytes)) return false;
    return std::all_of(d.ctx.begin(), d.ctx.end(), [](const Ctx& c) { return bytes_at(g_base, c.rva, c.bytes); });
}

// 0x24314C0's inline hook, with the same checks as p8_install_mid: the patch
// inside .rsrc, the function's verified bytes (all 0xDA), and the bytes
// safetyhook relocates exactly the verified 5-byte first instruction.
bool install_find(const PointDef& d) {
    const char* what = kKindName[d.kind];
    if (!gad::in_real_code(g_secs, static_cast<uint32_t>(d.rva), static_cast<uint32_t>(d.bytes.size()))) {
        log::error("P8Probe: {} (exe+0x{:X}) is not inside .rsrc; REFUSED, not hooked", what, d.rva);
        return false;
    }
    if (!bytes_at(g_base, d.rva, d.bytes)) {
        log::error("P8Probe: {} (exe+0x{:X}) is not the expected code of build 2976; REFUSED, not hooked", what, d.rva);
        return false;
    }
    auto h = game::prepare_inline(
        g_base + d.rva, reinterpret_cast<void*>(&find_detour),
        [&](const std::vector<uint8_t>& o) {
            return o.size() == d.patch && std::memcmp(o.data(), d.bytes.data(), d.patch) == 0;
        },
        "P8Probe", what);
    if (!h) {
        log::error("P8Probe: {} (exe+0x{:X}): the hook could not be prepared ({}); REFUSED", what, d.rva,
                   game::inline_error_text(h.error()));
        return false;
    }
    const auto& orig = h->original_bytes();
    if (orig.size() != d.patch || std::memcmp(orig.data(), d.bytes.data(), d.patch) != 0) {
        log::error("P8Probe: {} (exe+0x{:X}): the hook would patch {} bytes, {} were verified; REFUSED, nothing written",
                   what, d.rva, orig.size(), d.patch);
        return false;
    }
    g_find_hook = std::move(*h);  // the detour calls through it: in place before enabling
    if (!g_find_hook.enable()) {
        log::error("P8Probe: {} (exe+0x{:X}): the hook could not be enabled; REFUSED", what, d.rva);
        return false;
    }
    log::info("P8Probe: {} hooked inline (filter on EDX before anything else; every call counted per thread shard). "
              "s1_diag is meant for SHORT sessions: this function runs on every action lookup",
              what);
    return true;
}

void install_point(const PointDef& d) {
    Installed& st = g_points[d.kind];
    st.name = kKindName[d.kind];
    st.wanted = true;
    for (const Ctx& c : d.ctx) {
        if (!gad::in_real_code(g_secs, static_cast<uint32_t>(c.rva), static_cast<uint32_t>(c.bytes.size())) ||
            !bytes_at(g_base, c.rva, c.bytes)) {
            log::error("P8Probe: {} REFUSED: the code it relies on at exe+0x{:X} ({} bytes) is not the expected .rsrc "
                       "code of build 2976",
                       kKindName[d.kind], c.rva, c.bytes.size());
            return;
        }
    }
    if (d.kind == kKUi) {
        uintptr_t slot = 0;
        if (!peek(g_base + kUiVtableSlot, slot) || slot != g_base + kUi) {
            log::error("P8Probe: {} REFUSED: the ExtraReward vtable slot exe+0x{:X} is not exe+0x{:X}",
                       kKindName[d.kind], kUiVtableSlot, kUi);
            return;
        }
    }
    if (d.kind == kKFind) {
        st.on = install_find(d);
        return;
    }
    st.on = p8_install_mid(g_base, g_secs, d.rva, d.patch, d.bytes, d.fn, g_hooks[d.kind], "P8Probe",
                           kKindName[d.kind]);
}

DWORD WINAPI probe_thread(LPVOID) {
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (g_base != off::kImageBase || !peek(g_base, dos) || !peek(g_base + dos.e_lfanew, nt) ||
        nt.FileHeader.TimeDateStamp != off::kPeTimestamp) {
        log::error("P8Probe: unsupported game build; load_diag / s1_diag stay off");
        g_settled.store(true);
        return 0;
    }
    if (!read_sections()) {
        log::error("P8Probe: the game's section table is unreadable; nothing is hooked");
        g_settled.store(true);
        return 0;
    }
    std::vector<PointDef> defs;
    for (PointDef& d : points()) {
        if (wanted(d)) defs.push_back(std::move(d));
    }
    // Nothing they watch happens before the title screen: wait for WorldSystem
    // (as late_init does, up to 10 minutes), then for the code (up to 2 minutes).
    for (int i = 0; i < 1200; ++i) {
        uintptr_t world = 0;
        if (peek(g_base + game::build_2976::kWorldSystemSlot, world) && world) break;
        Sleep(500);
    }
    for (int i = 0; i < 24000; ++i) {
        if (std::all_of(defs.begin(), defs.end(), all_bytes)) break;
        Sleep(5);
    }
    for (const PointDef& d : defs) install_point(d);
    g_settled.store(true, std::memory_order_release);
    std::string on, off_list;
    for (const PointDef& d : defs) (g_points[d.kind].on ? on : off_list) += std::format(" {}", kKindName[d.kind]);
    log::info("P8Probe ON (read only, {}): load_diag {} s1_diag {} | hooked:{} | REFUSED:{} | ring {} events, {} per "
              "point per 5 s, drained every 5 s",
              g_host.load() ? "host" : "guest", g_load.load(), g_s1.load(), on.empty() ? " none" : on,
              off_list.empty() ? " none" : off_list, g_ring.capacity(), kPerWindow);
    for (uint64_t tick = 1;; ++tick) {
        Sleep(5000);
        drain();
        if (tick % 12 == 0) log::info("{}", counters());
    }
}

}  // namespace

uint64_t p8_now_us() noexcept { return now_us(); }

std::string p8_when(uint64_t qpc_us) { return when(qpc_us); }

bool p8_exe_sections(uintptr_t base, std::vector<gad::Section>& out) {
    out.clear();
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!game::safe_read(base, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        !game::safe_read(base + dos.e_lfanew, &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    const uintptr_t first = base + dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
                            nt.FileHeader.SizeOfOptionalHeader;
    for (WORD i = 0; i < nt.FileHeader.NumberOfSections && i < 64; ++i) {
        IMAGE_SECTION_HEADER h{};
        if (!game::safe_read(first + i * sizeof h, &h, sizeof h)) return false;
        gad::Section sec;
        std::memcpy(sec.name, h.Name, 8);
        sec.va = h.VirtualAddress;
        sec.size = h.Misc.VirtualSize;
        sec.flags = h.Characteristics;
        out.push_back(sec);
    }
    return !out.empty();
}

bool p8_install_mid(uintptr_t base, std::span<const gad::Section> secs, uintptr_t rva, uint32_t patch,
                    std::span<const uint8_t> expected, safetyhook::MidHookFn fn, SafetyHookMid& out,
                    const char* tag, const char* what) {
    const uint32_t span_len = static_cast<uint32_t>(std::max<size_t>(patch, expected.size()));
    if (patch < 5 || expected.size() < patch) {
        log::error("{}: {} (exe+0x{:X}) REFUSED: {} verified bytes do not cover a {}-byte patch", tag, what, rva,
                   expected.size(), patch);
        return false;
    }
    if (!gad::in_real_code(secs, static_cast<uint32_t>(rva), span_len)) {
        const gad::Section* s = gad::section_of(secs, static_cast<uint32_t>(rva));
        log::error("{}: {} (exe+0x{:X}, {} bytes) is not inside .rsrc (section {}); REFUSED, not hooked", tag, what,
                   rva, span_len, s ? s->name : "none");
        return false;
    }
    if (!bytes_at(base, rva, expected)) {
        log::error("{}: {} (exe+0x{:X}) is not the expected code of build 2976; REFUSED, not hooked", tag, what, rva);
        return false;
    }
    // freeze_watchdog=1 (p8-impl-freeze-watchdog.md): a wrapper that marks the thread while `fn` runs; else `fn`.
    fn = freeze_watchdog_wrap_mid(fn, tag, what);
    // P8 S26 (montura-regresion.md section 7.1): refused attempts logged and tried again (bounded), nothing written.
    auto h = game::prepare_mid(
        base + rva, fn,
        [&](const std::vector<uint8_t>& o) {
            return o.size() == patch && std::memcmp(o.data(), expected.data(), patch) == 0;
        },
        tag, what);
    if (!h) {
        log::error("{}: {} (exe+0x{:X}): the hook could not be prepared ({}); REFUSED", tag, what, rva,
                   game::mid_error_text(h.error()));
        return false;
    }
    const auto& orig = h->original_bytes();
    if (orig.size() != patch || std::memcmp(orig.data(), expected.data(), patch) != 0) {
        // E.g. safetyhook's 14-byte fallback: bytes nobody verified for branch targets.
        log::error("{}: {} (exe+0x{:X}): the hook would patch {} bytes, {} were verified; REFUSED, nothing written",
                   tag, what, rva, orig.size(), patch);
        return false;  // h is destroyed disabled: the game's code was never touched
    }
    if (!h->enable()) {
        log::error("{}: {} (exe+0x{:X}): the hook could not be enabled; REFUSED", tag, what, rva);
        return false;
    }
    out = std::move(*h);
    return true;
}

void p8_probes_start(const P8ProbeOptions& opt) {
    if (!opt.load_diag && !opt.s1_diag) return;
    if (g_started.exchange(true)) return;
    g_load = opt.load_diag;
    g_s1 = opt.s1_diag;
    g_host = opt.host;
    CloseHandle(CreateThread(nullptr, 0, probe_thread, nullptr, 0, nullptr));
}

bool p8_probes_wanted() { return g_started.load(); }

bool p8_probes_settled() { return g_settled.load(std::memory_order_acquire); }

bool p8_probes_point_hooked(uintptr_t rva) {
    if (!p8_probes_settled()) return false;
    for (const PointDef& d : points()) {
        if (d.rva == rva && g_points[d.kind].on) return true;
    }
    return false;
}

std::string p8_probes_describe() { return g_started.load() ? counters() : std::string(); }

}  // namespace cdmp::session2
