#include "mod/session2/guest_stage_diag.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/guest_seq_route.h"
#include "mod/session2/guest_stage_diag_logic.h"
#include "mod/session2/offsets_s2.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"

namespace cdmp::session2 {
namespace {

namespace p = p8p;

// ---------------------------------------------------------------- the points (build 2976, .rsrc)
//
// Read from the authorised read-only PE (D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe,
// SHA-256 57DA440D...E1E7) with capstone: every patch is whole instructions
// from the function start (.pdata), no branch of the function and no rel32
// (E8/E9/0F8x) of .rsrc/.xtls/.text lands strictly inside it, no RIP-relative
// operand in it (docs/research/pendientes/observador/fase2-verificacion-enganches.txt).
// No point is a P8Probe point or inside the bytes P8Probe verifies (0x9ED2A8,
// 0x9ED2B5, 0x9ED415.., 0x9ED431.., 0x9ED470.., 0x9ED47F.., 0xB87890.., 0xB878AC..,
// 0xB87918, 0xB87A5D.., 0xB87A67..).

// PrepareSequencerAck 2661's client handler 0xC05BB0(this, int* res, wrapper*):
//   raw = [R8+0x18] (u16 opcode | i8 | u16 len | payload), the payload length check at 0xC05BDB.
constexpr uintptr_t kIn2661 = 0xC05BB0;  // mov [rsp+8],rbx ; push rbp ...                      patch 5
constexpr uint8_t kIn2661Bytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57,
                                    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
// Its frame: rbp = rsp - 0xA0 (0xC05BC0); kind [rbp+0xF0] -> ebx (0xC060C6); the resolved actor
// stored in [R12] (0xC060C2; R12 = the wrapper from 0xC05BCF); the field attacher at [rbp-0x48]
// (+8 = [rbp-0x40] the ClientField, +0x10 = [rbp-0x38] valid) whose vf48 is called at 0xC06120;
// 0xA9CA40(manager, &[rbp-0x50] result, &[rbp] play data, &[rbp-0x80] bindings) at 0xC06179.
constexpr uintptr_t kFrame2661 = 0xC05BC0;
constexpr uint8_t kFrame2661Bytes[] = {0x48, 0x8D, 0xAC, 0x24, 0x60, 0xFF, 0xFF, 0xFF};
constexpr uintptr_t kKind2661 = 0xC060C6;
constexpr uint8_t kKind2661Bytes[] = {0x0F, 0xB6, 0x9D, 0xF0, 0x00, 0x00, 0x00};
constexpr uintptr_t kActor2661 = 0xC060C2;
constexpr uint8_t kActor2661Bytes[] = {0x49, 0x89, 0x1C, 0x24};
constexpr uintptr_t kVf48_2661 = 0xC06120;
constexpr uint8_t kVf48_2661Bytes[] = {0x48, 0x8B, 0x4D, 0xC0, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x48};
constexpr uintptr_t kCreate2661 = 0xC06179;
constexpr uint8_t kCreate2661Bytes[] = {0x4C, 0x8D, 0x4D, 0x80, 0x4C, 0x8D, 0x45, 0x00, 0x48, 0x8D,
                                        0x55, 0xB0, 0x48, 0x8B, 0xC8, 0xE8, 0xB3, 0x68, 0xE9, 0xFF};
//   0xC0612A cmp bl,4 ; je 0xC06179 (RAX = the manager vf48 returned)            patch 5 (the short je is relocated)
constexpr uintptr_t kRoute2661 = 0xC0612A;
constexpr uint8_t kRoute2661Bytes[] = {0x80, 0xFB, 0x04, 0x74, 0x4A, 0x48, 0x8D, 0x05,
                                       0x7A, 0x37, 0x9C, 0x04, 0x48, 0x89, 0x44, 0x24};
//   0xC061C1 mov dword [rdi],0 (both the created path and the "no actor" path)   patch 6
constexpr uintptr_t kResult2661 = 0xC061C1;
constexpr uint8_t kResult2661Bytes[] = {0xC7, 0x07, 0x00, 0x00, 0x00, 0x00, 0x4C, 0x89,
                                        0x75, 0x70, 0x4C, 0x89, 0x7D, 0x70, 0x48, 0x8D};

// 0xA9CA40(manager, result*, play data*, bindings*): rbp = rsp - 0x3C8 after the pushes, so
// [rbp+0x410] = the manager (rcx home) and [rbp+0x420] = the play data (r8 home, 0xA9CA40).
// Bindings loop: R14 = the 12-byte binding (0xA9CB87 type, 0xA9CB98 actor key); a type 4/5
// actor not found jumps from 0xA9CC13 to 0xA9CE81 (the error path):
//   0xA9CE87 mov r14,[rbp+0x418]                                                  patch 7
constexpr uintptr_t kMissing = 0xA9CE87;
constexpr uint8_t kMissingBytes[] = {0x4C, 0x8B, 0xB5, 0x18, 0x04, 0x00, 0x00, 0x41,
                                     0x89, 0x06, 0x48, 0x89, 0x74, 0x24, 0x58, 0x80};
constexpr uintptr_t kCreateHomes = 0xA9CA40;
constexpr uint8_t kCreateHomesBytes[] = {0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54,
                                         0x24, 0x10, 0x48, 0x89, 0x4C, 0x24, 0x08};
constexpr uintptr_t kBindType = 0xA9CB87;
constexpr uint8_t kBindTypeBytes[] = {0x41, 0x0F, 0xB6, 0x46, 0x08, 0x3C, 0x04, 0x74, 0x08, 0x3C, 0x05};
constexpr uintptr_t kBindActor = 0xA9CB98;
constexpr uint8_t kBindActorBytes[] = {0x45, 0x8B, 0x46, 0x04};
constexpr uintptr_t kBindMissing = 0xA9CC0E;
constexpr uint8_t kBindMissingBytes[] = {0x80, 0x7C, 0x24, 0x68, 0x00, 0x0F, 0x84, 0x68, 0x02, 0x00, 0x00};

// StageBranchStateAck 2981's handler 0xC073A0 (R15 = the wrapper from 0xC073BC; [R15] = the
// resolved actor at 0xC076E6; field = [rbp-0x38], vf48 at 0xC0773F):
constexpr uintptr_t kIn2981 = 0xC073A0;  //                                                    patch 5
constexpr uint8_t kIn2981Bytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57,
                                    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
constexpr uintptr_t kWrap2981 = 0xC073BC;
constexpr uint8_t kWrap2981Bytes[] = {0x4D, 0x8B, 0xF8};
constexpr uintptr_t kActor2981 = 0xC076E6;
constexpr uint8_t kActor2981Bytes[] = {0x49, 0x89, 0x3F};
constexpr uintptr_t kVf48_2981 = 0xC0773F;
constexpr uint8_t kVf48_2981Bytes[] = {0x48, 0x8B, 0x4D, 0xC8, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x48};
//   0xC07749 lea rcx,[rbp-0x80] ; mov [rsp+0x30],rcx (RAX = the manager)           patch 9
constexpr uintptr_t kRoute2981 = 0xC07749;
constexpr uint32_t kRoute2981Patch = 9;
constexpr uint8_t kRoute2981Bytes[] = {0x48, 0x8D, 0x4D, 0x80, 0x48, 0x89, 0x4C, 0x24,
                                       0x30, 0x48, 0x8D, 0x4D, 0xB0, 0x48, 0x89, 0x4C};

// DeleteSequencerAck 2810's handler 0xC06330 (field = [rbp-0x48], vf48 at 0xC06594):
constexpr uintptr_t kIn2810 = 0xC06330;  //                                                    patch 5
constexpr uint8_t kIn2810Bytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57,
                                    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
constexpr uintptr_t kVf48_2810 = 0xC06594;
constexpr uint8_t kVf48_2810Bytes[] = {0x48, 0x8B, 0x4D, 0xB8, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x48};
//   0xC0659E movzx ecx,byte [rbp+0x70] ; mov [rsp+0x20],cl (RAX = the manager)      patch 8
constexpr uintptr_t kRoute2810 = 0xC0659E;
constexpr uint32_t kRoute2810Patch = 8;
constexpr uint8_t kRoute2810Bytes[] = {0x0F, 0xB6, 0x4D, 0x70, 0x88, 0x4C, 0x24, 0x20,
                                       0x4C, 0x8D, 0x4D, 0xA0, 0x4C, 0x8B, 0x45, 0xA8};

// PlayAllPreLoadSequencerAck 2322's handler 0xB87890: field attacher at [rbp-0x60] (0xB87A28),
// field [rbp-0x58], valid [rbp-0x50]; R14 = raw (0xB879B7), the actor key at raw+5.
//   0xB87A32 cmp byte [rbp-0x50],0 ; jne 0xB87A5D                                 patch 6 (the short jne is relocated)
constexpr uintptr_t kRoute2322 = 0xB87A32;
constexpr uint32_t kRoute2322Patch = 6;
constexpr uint8_t kRoute2322Bytes[] = {0x80, 0x7D, 0xB0, 0x00, 0x75, 0x25, 0x48, 0x8D,
                                       0x05, 0x21, 0xB4, 0xD8, 0x04, 0x48, 0x89, 0x45};
constexpr uintptr_t kField2322 = 0xB87A28;
constexpr uint8_t kField2322Bytes[] = {0x48, 0x8D, 0x55, 0xA0, 0xE8, 0x8F, 0x2C, 0xC7, 0x00};
constexpr uintptr_t kRaw2322 = 0xB879B7;
constexpr uint8_t kRaw2322Bytes[] = {0x41, 0x0F, 0xB7, 0x46, 0x03};

// ClientSequencerPlayDataActorComponent::vf13 0x9ED290 (vtable exe+0x55BEE78 slot 13); r14 =
// comp (0x9ED2B5), rbp = rsp (0x9ED2A8): checked, never patched (P8Probe relies on them too).
constexpr uintptr_t kNpcEntry = 0x9ED290;  // mov [rsp+0x10],rbx                               patch 5
constexpr uint8_t kNpcEntryBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
                                      0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55};
constexpr uintptr_t kNpcFrame = 0x9ED2A8;
constexpr uint8_t kNpcFrameBytes[] = {0x48, 0x8B, 0xEC};
constexpr uintptr_t kNpcComp = 0x9ED2B5;
constexpr uint8_t kNpcCompBytes[] = {0x4C, 0x8B, 0xF1};
//   0x9ED401 cmp dword [rbp+0x30],0 ; je 0x9ED63B (rel32, relocated)              patch 10
constexpr uintptr_t kNpcRes8 = 0x9ED401;
constexpr uint32_t kNpcRes8Patch = 10;
constexpr uint8_t kNpcRes8Bytes[] = {0x83, 0x7D, 0x30, 0x00, 0x0F, 0x84, 0x30, 0x02,
                                     0x00, 0x00, 0x83, 0x7D, 0x34, 0xFF, 0x0F, 0x84};
//   0x9ED453 mov rax,[rbp-0x20] ; mov rcx,[rax+0x68] (the owner actor was found)   patch 8
constexpr uintptr_t kNpcOwner = 0x9ED453;
constexpr uint32_t kNpcOwnerPatch = 8;
constexpr uint8_t kNpcOwnerBytes[] = {0x48, 0x8B, 0x45, 0xE0, 0x48, 0x8B, 0x48, 0x68,
                                      0x48, 0x8B, 0x81, 0xA8, 0x00, 0x00, 0x00, 0x48};
//   0x9ED48C mov r8,[r14+8] ; lea rdx,[rbp+0x30] (RDI = the instance found)       patch 8
constexpr uintptr_t kNpcInstance = 0x9ED48C;
constexpr uint32_t kNpcInstancePatch = 8;
constexpr uint8_t kNpcInstanceBytes[] = {0x4D, 0x8B, 0x46, 0x08, 0x48, 0x8D, 0x55, 0x30,
                                         0x48, 0x8B, 0xCF, 0xE8, 0x14, 0xE0, 0xA4, 0xFF};

// The loading state machine's writes of M+0x231 (M = 0x9A5BE0(Q), p8-raiz-invitado.md §4.1):
//   0x77AA8B mov byte [rax+0x231],0 (RAX = M from 0x77AA6C, RDI = P from 0x77AA5C) patch 7
constexpr uintptr_t kReady0 = 0x77AA8B;
constexpr uint8_t kReady0Bytes[] = {0xC6, 0x80, 0x31, 0x02, 0x00, 0x00, 0x00, 0x80,
                                    0xBF, 0x62, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x84};
constexpr uintptr_t kReady0P = 0x77AA5C;
constexpr uint8_t kReady0PBytes[] = {0x48, 0x8B, 0xF9};
constexpr uintptr_t kReady0M = 0x77AA6C;
constexpr uint8_t kReady0MBytes[] = {0xE8, 0x6F, 0xB1, 0x22, 0x00};
//   0x77B0CE mov byte [r14+0x231],1 (R14 = M from 0x77AE9F, RBP = P from 0x77AE8A) patch 8
constexpr uintptr_t kReady1 = 0x77B0CE;
constexpr uint8_t kReady1Bytes[] = {0x41, 0xC6, 0x86, 0x31, 0x02, 0x00, 0x00, 0x01,
                                    0x45, 0x89, 0xA6, 0xD0, 0x00, 0x00, 0x00, 0x48};
constexpr uintptr_t kReady1M = 0x77AE9F;
constexpr uint8_t kReady1MBytes[] = {0x4C, 0x8B, 0xF0};
constexpr uintptr_t kReady1P = 0x77AE8A;
constexpr uint8_t kReady1PBytes[] = {0x48, 0x8B, 0xE9};

constexpr uint32_t kPatch5 = 5, kPatch6 = 6, kPatch7 = 7;

// 0xA9CA40's error codes (globals of the game, read by the drain thread).
constexpr uintptr_t kErrMissingActor = 0x6CDB9E4;  // 0xA9CE81 mov eax,[rip+..]
constexpr uintptr_t kErrDuplicate = 0x6CF76A4;     // 0xA9D2C4 mov eax,[rip+..]
constexpr uintptr_t kVtStageManager = 0x55C9720;   // pa::ClientSequencerStageManager (RTTI)

// Structure offsets.
constexpr uint32_t kActor_Key = 0x60, kActor_Comps = 0x68, kComps_Seq = 0xA8, kSeqComp_Mgr = 0x18;
constexpr uint32_t kMgr_Actor = 0x18, kMgr_Ready = 0x231, kMgr_Count = 0x174;
constexpr uint32_t kField_Owner = 0x180, kWorld_P = 0x60, kP_Q = 0xC0, kP_State = 0x15C;
constexpr uint32_t kComp_Npc = 0x08, kComp_Owner = 0x2D8, kComp_Key = 0x2E0;
constexpr uint32_t kWrap_Raw = 0x18, kWrap_Len = 0x10, kThis_Busy = 0x21;
constexpr size_t kHeader = 5;

// ---------------------------------------------------------------- events

enum Kind : uint16_t {
    kKIn2661 = 0, kKRoute2661, kKResult2661, kKMissing, kKIn2981, kKRoute2981, kKIn2810, kKRoute2810, kKRoute2322,
    kKNpcEntry, kKNpcRes8, kKNpcOwner, kKNpcInstance, kKReady0, kKReady1, kKindCount
};
constexpr const char* kKindName[kKindCount] = {
    "2661 in 0xC05BB0",       "2661 route 0xC0612A", "2661 result 0xC061C1", "2661 missing actor 0xA9CE87",
    "2981 in 0xC073A0",       "2981 route 0xC07749", "2810 in 0xC06330",     "2810 route 0xC0659E",
    "2322 field 0xB87A32",    "NPC vf13 0x9ED290",   "NPC res8 0x9ED401",    "NPC owner 0x9ED453",
    "NPC instance 0x9ED48C",  "M+0x231=0 0x77AA8B",  "M+0x231=1 0x77B0CE"};

constexpr uint64_t kWindowUs = 5'000'000;
constexpr uint32_t kPerWindow = 256;
constexpr uint32_t kFirstPerNpc = 3;  // the first events of each NPC at each NPC point
constexpr uint64_t kMaxLinesPerKind = 3000;
constexpr size_t kMaxRawBytes = 4096;  // payload bytes hashed per 2661

struct KindStats {
    std::atomic<uint64_t> hits{0}, queued{0}, limited{0}, dropped{0}, over_budget{0};
};

uintptr_t g_base = 0;
std::vector<gad::Section> g_secs;
std::atomic<bool> g_started{false}, g_on{false};
p::EventRing<4096> g_ring;
KindStats g_stats[kKindCount];
p::WindowLimiter g_lim[kKindCount] = {
    {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs},
    {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs},
    {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs},
    {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}};
std::atomic<uint64_t> g_lines[kKindCount] = {};
std::atomic<uint32_t> g_hooked{0}, g_wanted{0};
// P8 S1 option A (guest_seq_route.h): which points are hooked, and the install thread is done.
std::atomic<bool> g_point_ok[kKindCount] = {};
std::atomic<bool> g_settled{false};
std::atomic<uint64_t> g_npc_no_seq{0};  // vf13 calls of components without a sequencer key (not queued)
p::KeyCounter<8192> g_keys_entry, g_keys_res8, g_keys_owner, g_keys_instance;
SafetyHookMid g_hooks[kKindCount];

// One handler call at a time per thread: entry -> routing -> result of the same packet.
struct Pending2661 {
    uint64_t seq = 0, manager = 0;
    uint32_t actor = 0, count_before = 0;
    bool routed = false;
};
thread_local Pending2661 t_2661;

// ---------------------------------------------------------------- hot path (no CRT, no allocation)

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

template <class T>
T rdv(uintptr_t a, T fallback = T{}) noexcept {
    T v{};
    return rd(a, v) ? v : fallback;
}

uintptr_t rdp(uintptr_t a) noexcept { return rdv<uintptr_t>(a); }

uint32_t key_of(uintptr_t actor) noexcept { return actor ? rdv<uint32_t>(actor + kActor_Key) : 0; }

// [[[A+0x68]+0xA8]+0x18]: the stage manager of a character (0x9A5BE0's last step, 0x9ED45B).
uintptr_t manager_of(uintptr_t actor) noexcept {
    const uintptr_t comps = actor ? rdp(actor + kActor_Comps) : 0;
    const uintptr_t seq = comps ? rdp(comps + kComps_Seq) : 0;
    return seq ? rdp(seq + kSeqComp_Mgr) : 0;
}

uint32_t manager_actor_key(uintptr_t m) noexcept { return m ? key_of(rdp(m + kMgr_Actor)) : 0; }

// u8 M+0x231, 0xFFFF when unreadable.
uint64_t ready_of(uintptr_t m) noexcept {
    uint8_t v = 0;
    return m && rd(m + kMgr_Ready, v) ? v : 0xFFFF;
}

uintptr_t loading_q() noexcept {
    const uintptr_t world = rdp(g_base + off::kWorldSystemSlot);
    const uintptr_t pp = world ? rdp(world + kWorld_P) : 0;
    const uintptr_t qq = pp ? rdp(pp + kP_Q) : 0;
    return qq ? rdp(qq) : 0;
}

void emit(Kind k, p::Event& e) noexcept {
    e.kind = k;
    if (!e.qpc_us) e.qpc_us = p8_now_us();
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

bool npc_budget(Kind k, p::KeyCounter<8192>& keys, uint32_t npc, uint32_t& nth) noexcept {
    nth = keys.bump(npc);
    if (p::within_key_budget(nth, kFirstPerNpc)) return true;
    g_stats[k].over_budget.fetch_add(1, std::memory_order_relaxed);
    return false;
}

// The raw packet a handler received: u16 opcode | i8 | u16 len | payload.
struct Raw {
    uintptr_t p = 0;  // payload
    uint16_t op = 0, len = 0, wrapper_len = 0;
};
Raw raw_of(uintptr_t wrapper) noexcept {
    Raw r;
    const uintptr_t raw = wrapper ? rdp(wrapper + kWrap_Raw) : 0;
    if (!raw) return r;
    r.op = rdv<uint16_t>(raw);
    r.len = rdv<uint16_t>(raw + 3);
    r.wrapper_len = rdv<uint16_t>(wrapper + kWrap_Len);
    r.p = raw + kHeader;
    return r;
}

void on_in_2661(safetyhook::Context& ctx) noexcept {
    g_stats[kKIn2661].hits.fetch_add(1, std::memory_order_relaxed);
    t_2661 = Pending2661{};
    const Raw r = raw_of(ctx.r8);
    p::Event e;
    const uint8_t kind = rdv<uint8_t>(r.p), busy = rdv<uint8_t>(ctx.rcx + kThis_Busy);
    e.v[0] = r.op | static_cast<uint64_t>(r.len) << 16 | static_cast<uint64_t>(kind) << 32 |
             static_cast<uint64_t>(busy) << 40 | static_cast<uint64_t>(r.wrapper_len) << 48;
    const uint32_t actor = rdv<uint32_t>(r.p + 1), owner = rdv<uint32_t>(r.p + 5);
    const uint64_t seq = rdv<uint64_t>(r.p + 9);
    e.v[1] = actor | static_cast<uint64_t>(owner) << 32;
    e.v[2] = seq;
    const size_t n = r.len >= obs::kPreparePrefix ? (r.len - obs::kPreparePrefix) / obs::kBindingWire : 0;
    e.v[3] = n;
    uint64_t types = 0;
    for (size_t i = 0; i < n && i < 8; ++i) {
        const uintptr_t b = r.p + obs::kPreparePrefix + i * obs::kBindingWire;
        if (i < 6) e.v[4 + i] = rdv<uint32_t>(b) | static_cast<uint64_t>(rdv<uint32_t>(b + 4)) << 32;
        types |= static_cast<uint64_t>(rdv<uint8_t>(b + 8)) << (8 * i);
    }
    e.v[10] = types;
    uint64_t h = 0xCBF29CE484222325ull;  // gsd::fnv1a64 over the payload, read byte by byte
    const size_t len = r.len < kMaxRawBytes ? r.len : kMaxRawBytes;
    for (size_t i = 0; i < len; ++i) {
        h ^= rdv<uint8_t>(r.p + i);
        h *= 0x100000001B3ull;
    }
    e.v[11] = h;
    t_2661.seq = seq;
    t_2661.actor = actor;
    emit(kKIn2661, e);
}

void route_common(p::Event& e, uintptr_t m, uintptr_t field, uintptr_t actor) noexcept {
    e.v[0] = m;
    e.v[1] = field;
    e.v[2] = rdv<uint32_t>(field + kField_Owner) | static_cast<uint64_t>(manager_actor_key(m)) << 32;
    e.v[3] = actor;
    const uintptr_t xa = manager_of(actor);
    e.v[5] = xa;
    e.v[8] = loading_q();
    e.v[9] = m ? rdp(m) : 0;
    e.v[10] = xa ? rdp(xa) : 0;
    e.v[11] = rdv<uint32_t>(m + kMgr_Count) | ready_of(m) << 32;
}

void on_route_2661(safetyhook::Context& ctx) noexcept {
    // P8 S1 option A (guest_seq_route.h): one hook per address; with guest_seq_route=1 the routing may set RAX
    // (the actor's own manager X) first, and this line then shows the manager the handler really uses.
    guest_seq_route_hook(gsr::Point::P2661, ctx);
    g_stats[kKRoute2661].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t m = ctx.rax, rbp = ctx.rbp;
    const uintptr_t actor = rdp(ctx.r12);
    p::Event e;
    route_common(e, m, rdp(rbp - 0x40), actor);
    e.v[4] = key_of(actor) | static_cast<uint64_t>(ctx.rbx & 0xFF) << 32 |
             static_cast<uint64_t>(rdv<uint8_t>(rbp - 0x38)) << 40;
    e.v[6] = rdv<uint32_t>(rbp + 0) | static_cast<uint64_t>(rdv<uint32_t>(rbp - 0x78)) << 32;  // play +0, bindings n
    e.v[7] = rdv<uint64_t>(rbp + 8);                                                          // play +8
    t_2661.manager = m;
    t_2661.count_before = rdv<uint32_t>(m + kMgr_Count);
    t_2661.routed = true;
    emit(kKRoute2661, e);
}

void on_result_2661(safetyhook::Context& ctx) noexcept {
    g_stats[kKResult2661].hits.fetch_add(1, std::memory_order_relaxed);
    p::Event e;
    const uint8_t kind = static_cast<uint8_t>(ctx.rbx & 0xFF);
    const bool routed = t_2661.routed;
    e.v[0] = (routed && kind == 4 ? rdv<uint32_t>(ctx.rbp - 0x50) : 0) | static_cast<uint64_t>(kind) << 32 |
             static_cast<uint64_t>(routed) << 40;
    e.v[1] = t_2661.manager;
    e.v[2] = t_2661.count_before | static_cast<uint64_t>(routed ? rdv<uint32_t>(t_2661.manager + kMgr_Count) : 0)
                                       << 32;
    e.v[3] = t_2661.seq;
    e.v[4] = t_2661.actor;
    t_2661 = Pending2661{};
    emit(kKResult2661, e);
}

void on_missing(safetyhook::Context& ctx) noexcept {
    g_stats[kKMissing].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t b = ctx.r14, rbp = ctx.rbp;
    const uintptr_t play = rdp(rbp + 0x420);
    p::Event e;
    e.v[0] = rdv<uint32_t>(b) | static_cast<uint64_t>(rdv<uint32_t>(b + 4)) << 32;
    e.v[1] = rdv<uint8_t>(b + 8);
    e.v[2] = rdp(rbp + 0x410);
    e.v[3] = rdv<uint64_t>(play + 8);
    e.v[4] = rdv<uint32_t>(play);
    emit(kKMissing, e);
}

void on_in_branch(Kind k, safetyhook::Context& ctx) noexcept {
    g_stats[k].hits.fetch_add(1, std::memory_order_relaxed);
    const Raw r = raw_of(ctx.r8);
    p::Event e;
    e.v[0] = r.op | static_cast<uint64_t>(r.len) << 16 | static_cast<uint64_t>(rdv<uint8_t>(ctx.rcx + kThis_Busy)) << 40;
    for (size_t i = 0; i < 6; ++i) {  // the first 48 payload bytes (2981: 29 or 37, 2810: 17)
        uint64_t w = 0;
        for (size_t j = 0; j < 8; ++j) {
            const size_t at = i * 8 + j;
            if (at < r.len) w |= static_cast<uint64_t>(rdv<uint8_t>(r.p + at)) << (8 * j);
        }
        e.v[1 + i] = w;
    }
    emit(k, e);
}
void on_in_2981(safetyhook::Context& ctx) noexcept { on_in_branch(kKIn2981, ctx); }
void on_in_2810(safetyhook::Context& ctx) noexcept { on_in_branch(kKIn2810, ctx); }

void on_route_2981(safetyhook::Context& ctx) noexcept {
    guest_seq_route_hook(gsr::Point::P2981, ctx);  // P8 S1 option A, as on_route_2661
    g_stats[kKRoute2981].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t actor = rdp(ctx.r15);
    p::Event e;
    route_common(e, ctx.rax, rdp(ctx.rbp - 0x38), actor);
    e.v[4] = key_of(actor);
    const Raw r = raw_of(ctx.r15);  // R15 = the wrapper (0xC073BC)
    e.v[6] = r.p ? rdv<uint32_t>(r.p + 13) : 0;
    e.v[7] = r.p ? rdv<uint64_t>(r.p + 5) : 0;
    emit(kKRoute2981, e);
}

void on_route_2810(safetyhook::Context& ctx) noexcept {
    guest_seq_route_hook(gsr::Point::P2810, ctx);  // P8 S1 option A, as on_route_2661
    g_stats[kKRoute2810].hits.fetch_add(1, std::memory_order_relaxed);
    p::Event e;
    route_common(e, ctx.rax, rdp(ctx.rbp - 0x48), 0);
    emit(kKRoute2810, e);
}

void on_route_2322(safetyhook::Context& ctx) noexcept {
    g_stats[kKRoute2322].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t rbp = ctx.rbp, field = rdp(rbp - 0x58);
    p::Event e;
    e.v[0] = field;
    e.v[1] = rdv<uint8_t>(rbp - 0x50);
    e.v[2] = rdv<uint32_t>(ctx.r14 + kHeader) | static_cast<uint64_t>(rdv<uint32_t>(field + kField_Owner)) << 32;
    e.v[3] = loading_q();
    emit(kKRoute2322, e);
}

uint32_t npc_key_of_comp(uintptr_t comp) noexcept { return key_of(rdp(comp + kComp_Npc)); }

void on_npc_entry(safetyhook::Context& ctx) noexcept {
    g_stats[kKNpcEntry].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t comp = ctx.rcx;
    const uint64_t key = rdv<uint64_t>(comp + kComp_Key);
    if ((static_cast<uint32_t>(key) & 0x1FFFFFFF) == 0) {  // 0x9ED2BF: returns at once
        g_npc_no_seq.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const uint32_t npc = npc_key_of_comp(comp);
    uint32_t nth = 0;
    if (!npc_budget(kKNpcEntry, g_keys_entry, npc, nth)) return;
    p::Event e;
    e.v[0] = npc | static_cast<uint64_t>(rdv<uint32_t>(comp + kComp_Owner)) << 32;
    e.v[1] = key;
    e.v[2] = nth;
    e.v[3] = comp;
    emit(kKNpcEntry, e);
}

void on_npc_res8(safetyhook::Context& ctx) noexcept {
    g_stats[kKNpcRes8].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t comp = ctx.r14;
    const uint32_t npc = npc_key_of_comp(comp);
    uint32_t nth = 0;
    if (!npc_budget(kKNpcRes8, g_keys_res8, npc, nth)) return;
    p::Event e;
    e.v[0] = npc | static_cast<uint64_t>(rdv<uint32_t>(comp + kComp_Owner)) << 32;
    e.v[1] = rdv<uint64_t>(ctx.rbp + 0x30);
    e.v[2] = nth;
    emit(kKNpcRes8, e);
}

void on_npc_owner(safetyhook::Context& ctx) noexcept {
    g_stats[kKNpcOwner].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t comp = ctx.r14;
    const uint32_t npc = npc_key_of_comp(comp);
    uint32_t nth = 0;
    if (!npc_budget(kKNpcOwner, g_keys_owner, npc, nth)) return;
    const uintptr_t a = rdp(ctx.rbp - 0x20);
    const uintptr_t x = manager_of(a);
    p::Event e;
    e.v[0] = npc | static_cast<uint64_t>(key_of(a)) << 32;
    e.v[1] = x;
    e.v[2] = x ? rdp(x) : 0;
    e.v[3] = rdv<uint32_t>(x + kMgr_Count) | ready_of(x) << 32;
    e.v[4] = rdv<uint64_t>(comp + kComp_Key);
    e.v[5] = nth;
    emit(kKNpcOwner, e);
}

void on_npc_instance(safetyhook::Context& ctx) noexcept {
    g_stats[kKNpcInstance].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t comp = ctx.r14;
    const uint32_t npc = npc_key_of_comp(comp);
    uint32_t nth = 0;
    if (!npc_budget(kKNpcInstance, g_keys_instance, npc, nth)) return;
    p::Event e;
    e.v[0] = npc | static_cast<uint64_t>(rdv<uint32_t>(comp + kComp_Owner)) << 32;
    e.v[1] = ctx.rdi;
    e.v[2] = nth;
    emit(kKNpcInstance, e);
}

p::ChangeGate g_gate_ready[2];

// Change-only: a state handler may write the same value every frame.
void on_ready(Kind k, uintptr_t m, uintptr_t pp) noexcept {
    g_stats[k].hits.fetch_add(1, std::memory_order_relaxed);
    const uint64_t before = ready_of(m);
    if (!g_gate_ready[k == kKReady0 ? 0 : 1].changed(p::mix(m, before))) {
        g_stats[k].over_budget.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    p::Event e;
    e.v[0] = m;
    e.v[1] = before;
    e.v[2] = manager_actor_key(m);
    e.v[3] = pp;
    e.v[4] = rdv<uint8_t>(pp + kP_State);
    emit(k, e);
}
void on_ready0(safetyhook::Context& ctx) noexcept { on_ready(kKReady0, ctx.rax, ctx.rdi); }
void on_ready1(safetyhook::Context& ctx) noexcept { on_ready(kKReady1, ctx.r14, ctx.rbp); }

// ---------------------------------------------------------------- drain (the guest's diagnostics thread)

gsd::ManagerBook g_book;      // drain only
gsd::NpcTracker g_npcs;       // drain only
uint64_t g_ticks = 0;         // drain only
std::map<uint32_t, uint64_t> g_last_in_seq;  // tid -> the seq of its last 2661 entry (drain only)

void write_line(Kind k, const std::string& s) {
    if (g_lines[k].fetch_add(1) + 1 > kMaxLinesPerKind) return;
    try {
        log::info("{}", s);
    } catch (...) {
    }
}

std::string head(Kind k, const p::Event& e) {
    return std::format("GuestStage [{}] {} T{} |", kKindName[k], p8_when(e.qpc_us), e.tid);
}

std::string rva_text(uintptr_t a) {
    return a >= g_base && a - g_base < 0x10000000 ? std::format("exe+0x{:X}", a - g_base) : std::format("0x{:X}", a);
}

std::string ready_text(uint64_t r) { return r > 0xFF ? std::string("unreadable") : std::format("{}", r); }

std::string vt_text(uintptr_t vt) {
    if (!vt) return "-";
    return vt == g_base + kVtStageManager ? std::string("ClientSequencerStageManager") : rva_text(vt);
}

uint32_t read_global(uintptr_t rva) {
    uint32_t v = 0;
    return game::safe_read(g_base + rva, &v, sizeof v) ? v : 0;
}

// The routing lines of 2661/2981/2810: the manager, whose it is, and the verdict.
// `addressed_key` is the key of the actor the hook resolved (v[3]); its own
// manager (v[5]) is the sequencer owner's when the two keys are equal.
std::string route_text(const p::Event& e, uint32_t seq_owner_actor, uint32_t addressed_key) {
    const uintptr_t m = e.v[0], field = e.v[1], actor = e.v[3], xa = e.v[5], q = e.v[8];
    const uint32_t field_owner = static_cast<uint32_t>(e.v[2]), m_actor = static_cast<uint32_t>(e.v[2] >> 32);
    const uint32_t count = static_cast<uint32_t>(e.v[11]);
    const uint64_t ready = e.v[11] >> 32;
    gsd::RouteFacts f;
    f.manager = m;
    f.manager_actor = m_actor;
    f.field_owner = field_owner;
    f.seq_owner_actor = seq_owner_actor;
    f.seq_owner_manager = (seq_owner_actor && seq_owner_actor == addressed_key) ? xa : 0;
    const gsd::Route r = gsd::classify_route(f);
    return std::format("manager 0x{:X} ({}) of actor 0x{:08X}: +0x231 = {}, loading tasks (+0x174) {} | field 0x{:X} "
                       "(owner user in this client's view 0x{:08X}; the loading state's Q 0x{:X}: {}) | addressed "
                       "actor 0x{:X} (0x{:08X}), its own manager 0x{:X} ({}) | sequencer owner 0x{:08X} -> {}",
                       m, vt_text(e.v[9]), m_actor, ready_text(ready), count, field, field_owner, q,
                       q == field ? "same" : "DIFFERENT", actor, addressed_key, xa, vt_text(e.v[10]), seq_owner_actor,
                       gsd::route_text(r));
}

void note_manager(uint64_t m, uint32_t actor, uint64_t ready, uint64_t t_us, Kind k) {
    if (!m) return;
    gsd::ManagerInfo& mi = g_book.touch(m, actor, t_us);
    if (k == kKRoute2661) ++mi.prepares;
    if (k == kKRoute2981) ++mi.branches;
    if (k == kKRoute2810) ++mi.deletes;
    const gsd::ReadyChange c = g_book.ready(m, actor, ready > 0xFF ? -1 : static_cast<int>(ready), t_us);
    if (c.changed && c.before >= 0) {
        write_line(k, std::format("GuestStage [M+0x231 change] {} | manager 0x{:X} (actor 0x{:08X}): {} -> {} (seen at "
                                  "{})",
                                  p8_when(t_us), m, actor, c.before, c.after, kKindName[k]));
    }
    g_book.set_routed(m);
}

void drain_one(const p::Event& e) {
    const Kind k = static_cast<Kind>(e.kind);
    switch (k) {
        case kKIn2661: {
            const uint16_t len = static_cast<uint16_t>(e.v[0] >> 16);
            const uint8_t kind = static_cast<uint8_t>(e.v[0] >> 32), busy = static_cast<uint8_t>(e.v[0] >> 40);
            const uint32_t actor = static_cast<uint32_t>(e.v[1]), owner = static_cast<uint32_t>(e.v[1] >> 32);
            const uint64_t n = e.v[3];
            std::string bs;
            for (size_t i = 0; i < n && i < 6; ++i) {
                bs += std::format(" 0x{:08X}->0x{:08X}(type {})", static_cast<uint32_t>(e.v[4 + i]),
                                  static_cast<uint32_t>(e.v[4 + i] >> 32), static_cast<uint8_t>(e.v[10] >> (8 * i)));
            }
            if (n > 6) bs += std::format(" (+{} more)", n - 6);
            g_last_in_seq[e.tid] = e.v[2];
            write_line(k, std::format("{} 2661 payload {} bytes ({}) kind {} | {} | addressed actor 0x{:08X} owner "
                                      "0x{:08X} sequencer 0x{:X} | bindings {}:{} | payload fnv1a64 0x{:016X}{}",
                                      head(k, e), len,
                                      len >= obs::kPreparePrefix && (len - obs::kPreparePrefix) % obs::kBindingWire == 0
                                          ? std::format("91 + 11 x {}", n)
                                          : std::string("NOT 91 + 11n"),
                                      kind, gsd::corr_token(obs::kOpPrepare, actor, e.v[2], static_cast<uint32_t>(n)),
                                      actor, owner, e.v[2], n, bs.empty() ? std::string(" none") : bs, e.v[11],
                                      busy ? " | handler flag [this+0x21] set: the handler ignores it" : ""));
            break;
        }
        case kKRoute2661: {
            const uint8_t kind = static_cast<uint8_t>(e.v[4] >> 32), valid = static_cast<uint8_t>(e.v[4] >> 40);
            const uint32_t play_owner = static_cast<uint32_t>(e.v[6]), nvec = static_cast<uint32_t>(e.v[6] >> 32);
            const auto it = g_last_in_seq.find(e.tid);
            const bool same_seq = it != g_last_in_seq.end() && it->second == e.v[7];
            note_manager(e.v[0], static_cast<uint32_t>(e.v[2] >> 32), e.v[11] >> 32, e.qpc_us, k);
            write_line(k, std::format("{} kind {} field context {} | {} | play data +0 0x{:08X} +8 0x{:X} ({} the entry's "
                                      "sequencer), bindings vector n {}",
                                      head(k, e), kind, valid ? "valid" : "INVALID",
                                      route_text(e, play_owner, static_cast<uint32_t>(e.v[4])),
                                      play_owner, e.v[7], same_seq ? "=" : "!=", nvec));
            break;
        }
        case kKResult2661: {
            const uint32_t code = static_cast<uint32_t>(e.v[0]);
            const uint8_t kind = static_cast<uint8_t>(e.v[0] >> 32);
            const bool routed = (e.v[0] >> 40) & 1;
            const uint32_t before = static_cast<uint32_t>(e.v[2]), after = static_cast<uint32_t>(e.v[2] >> 32);
            if (!routed) {
                write_line(k, std::format("{} sequencer 0x{:X} of 0x{:08X}: NOT routed (the addressed actor is not on "
                                          "this client or has no field context): the handler did nothing",
                                          head(k, e), e.v[3], static_cast<uint32_t>(e.v[4])));
                break;
            }
            if (kind != 4) {
                write_line(k, std::format("{} sequencer 0x{:X}: kind {} != 4, the handler only logged an error",
                                          head(k, e), e.v[3], kind));
                break;
            }
            const gsd::Result r =
                gsd::classify_result(code, read_global(kErrDuplicate), read_global(kErrMissingActor));
            write_line(k, std::format("{} sequencer 0x{:X} of 0x{:08X} in manager 0x{:X}: 0xA9CA40 result 0x{:X} = {} | "
                                      "loading tasks (+0x174) {} -> {}",
                                      head(k, e), e.v[3], static_cast<uint32_t>(e.v[4]), e.v[1], code,
                                      gsd::result_text(r), before, after));
            break;
        }
        case kKMissing:
            write_line(k, std::format("{} sequencer 0x{:X} (owner 0x{:08X}) in manager 0x{:X}: bound actor 0x{:08X} "
                                      "(node 0x{:08X}, type {}) is NOT on this client -> the whole sequencer is "
                                      "rejected (0xA9CE81)",
                                      head(k, e), e.v[3], static_cast<uint32_t>(e.v[4]), e.v[2],
                                      static_cast<uint32_t>(e.v[0] >> 32), static_cast<uint32_t>(e.v[0]), e.v[1]));
            break;
        case kKIn2981:
        case kKIn2810: {
            const uint16_t len = static_cast<uint16_t>(e.v[0] >> 16);
            uint8_t bytes[48] = {};
            for (size_t i = 0; i < 48; ++i) bytes[i] = static_cast<uint8_t>(e.v[1 + i / 8] >> (8 * (i % 8)));
            const std::span<const uint8_t> pl(bytes, len < 48 ? len : 48);
            std::string hex;
            for (uint8_t b : pl) hex += std::format("{:02x}", b);
            std::string corr = "corr=?";
            if (k == kKIn2981) {
                if (const auto b = gsd::parse_branch(pl)) corr = gsd::corr_token(obs::kOpBranch, b->actor, b->seq, b->value);
            } else if (pl.size() >= obs::kDeletePayload) {
                corr = gsd::corr_token(obs::kOpDelete, obs::le32(&pl[0]), obs::le64(&pl[4]), obs::le32(&pl[12]));
            }
            write_line(k, std::format("{} payload {} bytes | {} | {}{}", head(k, e), len, corr, hex,
                                      (e.v[0] >> 40) & 0xFF ? " | handler flag [this+0x21] set" : ""));
            break;
        }
        case kKRoute2981: {
            const uint32_t addressed = static_cast<uint32_t>(e.v[4]);
            note_manager(e.v[0], static_cast<uint32_t>(e.v[2] >> 32), e.v[11] >> 32, e.qpc_us, k);
            write_line(k, std::format("{} {} | {}", head(k, e),
                                      gsd::corr_token(obs::kOpBranch, addressed, e.v[7], static_cast<uint32_t>(e.v[6])),
                                      route_text(e, addressed, addressed)));
            break;
        }
        case kKRoute2810:
            note_manager(e.v[0], static_cast<uint32_t>(e.v[2] >> 32), e.v[11] >> 32, e.qpc_us, k);
            write_line(k, std::format("{} {}", head(k, e), route_text(e, 0, 0)));
            break;
        case kKRoute2322: {
            const uint32_t actor = static_cast<uint32_t>(e.v[2]), owner = static_cast<uint32_t>(e.v[2] >> 32);
            const gsd::ManagerInfo* last = g_book.last_routed();
            write_line(k, std::format("{} {} | field 0x{:X} context {} (owner user in this client's view 0x{:08X}; Q "
                                      "0x{:X}: {}): its [M+0x231] = 2 goes to that owner's character's manager "
                                      "whatever actor it names (P8Probe [2322 write 0xB87A67] prints M){}",
                                      head(k, e), gsd::corr_token(obs::kOpMilestone, actor, 0, 0), e.v[0],
                                      e.v[1] ? "valid" : "INVALID", owner, e.v[3], e.v[3] == e.v[0] ? "same" : "DIFFERENT",
                                      last ? std::format(" | the stage handlers routed last to 0x{:X} (actor 0x{:08X})",
                                                         last->ptr, last->actor)
                                           : std::string()));
            break;
        }
        case kKNpcEntry: {
            const uint32_t npc = static_cast<uint32_t>(e.v[0]), owner = static_cast<uint32_t>(e.v[0] >> 32);
            if (gsd::NpcBind* b = g_npcs.step(npc, gsd::NpcStep::Entry, e.qpc_us)) {
                b->seq_owner = owner;
                b->seq_key = e.v[1];
            }
            write_line(k, std::format("{} NPC 0x{:08X} (comp 0x{:X}, call #{}) | sequencer owner comp+0x2D8 0x{:08X}, "
                                      "comp+0x2E0 0x{:X}",
                                      head(k, e), npc, e.v[3], e.v[2], owner, e.v[1]));
            break;
        }
        case kKNpcRes8: {
            const uint32_t npc = static_cast<uint32_t>(e.v[0]);
            g_npcs.res8(npc, e.v[1], e.qpc_us);
            write_line(k, std::format("{} NPC 0x{:08X} (owner 0x{:08X}, #{}) | local_res8 0x{:016X} -> {}", head(k, e), npc,
                                      static_cast<uint32_t>(e.v[0] >> 32), e.v[2], e.v[1],
                                      gsd::NpcTracker::res8_passes(e.v[1])
                                          ? "passes: the owner actor is looked up next"
                                          : "EXITS here (no stage node of its own): no lookup, no binding"));
            break;
        }
        case kKNpcOwner: {
            const uint32_t npc = static_cast<uint32_t>(e.v[0]), owner = static_cast<uint32_t>(e.v[0] >> 32);
            const uintptr_t x = e.v[1];
            const gsd::ManagerInfo* routed = g_book.last_routed();
            const bool same = routed && routed->ptr == x;
            if (gsd::NpcBind* b = g_npcs.step(npc, gsd::NpcStep::OwnerFound, e.qpc_us)) {
                b->owner_manager = x;
                b->owner_manager_routed = same;
            }
            if (x) {
                gsd::ManagerInfo& mi = g_book.touch(x, owner, e.qpc_us);
                ++mi.npc_lookups;
            }
            write_line(k, std::format("{} NPC 0x{:08X} (#{}) | owner actor 0x{:08X} found; it searches manager 0x{:X} "
                                      "({}): +0x231 = {}, loading tasks +0x174 {} for comp+0x2E0 0x{:X} | {}",
                                      head(k, e), npc, e.v[5], owner, x, vt_text(e.v[2]), ready_text(e.v[3] >> 32),
                                      static_cast<uint32_t>(e.v[3]), e.v[4],
                                      !routed ? std::string("no stage Ack routed yet on this client")
                                      : same  ? std::format("= the manager the stage Acks are routed to (0x{:X})",
                                                            routed->ptr)
                                              : std::format("NOT the manager the stage Acks are routed to (0x{:X}, actor "
                                                            "0x{:08X}): a replayed 2661 would not be found here",
                                                            routed->ptr, routed->actor)));
            break;
        }
        case kKNpcInstance: {
            const uint32_t npc = static_cast<uint32_t>(e.v[0]);
            g_npcs.step(npc, gsd::NpcStep::Instance, e.qpc_us);
            write_line(k, std::format("{} NPC 0x{:08X} (owner 0x{:08X}, #{}) | instance 0x{:X} FOUND: the binding "
                                      "happens",
                                      head(k, e), npc, static_cast<uint32_t>(e.v[0] >> 32), e.v[2], e.v[1]));
            break;
        }
        case kKReady0:
        case kKReady1: {
            const uint64_t before = e.v[1];
            const uint32_t actor = static_cast<uint32_t>(e.v[2]);
            const int after = k == kKReady0 ? 0 : 1;
            if (e.v[0]) {
                g_book.touch(e.v[0], actor, e.qpc_us);
                g_book.ready(e.v[0], actor, after, e.qpc_us);
            }
            write_line(k, std::format("{} manager 0x{:X} (actor 0x{:08X}) +0x231: {} -> {} | P 0x{:X} state 0x{:X}",
                                      head(k, e), e.v[0], actor, ready_text(before), after, e.v[3], e.v[4]));
            break;
        }
        default:
            break;
    }
}

// Every 60 s: what each known manager holds now, and the NPC side in one line.
void summary() {
    std::string ms;
    const std::vector<gsd::ManagerInfo> known = g_book.all();  // a copy: ready() below updates the book
    for (const gsd::ManagerInfo& m : known) {
        uint8_t ready = 0;
        uint32_t count = 0;
        const bool r1 = game::safe_read(m.ptr + kMgr_Ready, &ready, sizeof ready);
        const bool r2 = game::safe_read(m.ptr + kMgr_Count, &count, sizeof count);
        if (r1 && m.ready >= 0 && ready != m.ready) {
            log::info("GuestStage [M+0x231 change] {} | manager 0x{:X} (actor 0x{:08X}): {} -> {} (poll)",
                      p8_when(p8_now_us()), m.ptr, m.actor, m.ready, ready);
        }
        if (r1) g_book.ready(m.ptr, m.actor, ready, p8_now_us());
        ms += std::format(" 0x{:X}(actor 0x{:08X}: +0x231 {} loading tasks {} | 2661 {} 2981 {} 2810 {} NPC lookups {})",
                          m.ptr, m.actor, r1 ? std::format("{}", ready) : std::string("?"),
                          r2 ? std::format("{}", count) : std::string("?"), m.prepares, m.branches, m.deletes,
                          m.npc_lookups);
    }
    const gsd::NpcTracker::Summary s = g_npcs.summary();
    std::string owners;
    for (const auto& [o, n] : s.owners) owners += std::format(" 0x{:08X} x{}", o, n);
    std::string st;
    for (int k = 0; k < kKindCount; ++k) {
        const KindStats& x = g_stats[k];
        st += std::format(" | {}: hits {} queued {} limited {} over budget {} dropped {}", kKindName[k], x.hits.load(),
                          x.queued.load(), x.limited.load(), x.over_budget.load(), x.dropped.load());
    }
    log::info("GuestStage summary {} | managers seen:{} | NPC side (0x9ED290, components with a sequencer key): {} "
              "NPCs, owners:{} | passed local_res8 {}, exited there {}, owner actor found {} (its manager = the routed "
              "one: {}), instance found {} | vf13 calls without a sequencer key {} | ring dropped {}{}",
              p8_when(p8_now_us()), ms.empty() ? std::string(" none") : ms, s.npcs,
              owners.empty() ? std::string(" none") : owners, s.passed_res8, s.exited_res8, s.owner_found,
              s.owner_manager_routed, s.instance, g_npc_no_seq.load(), g_ring.dropped(), st);
}

// ---------------------------------------------------------------- install

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
    const uintptr_t first = g_base + dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
                            nt.FileHeader.SizeOfOptionalHeader;
    for (WORD i = 0; i < nt.FileHeader.NumberOfSections && i < 64; ++i) {
        IMAGE_SECTION_HEADER h{};
        if (!peek(first + i * sizeof h, h)) return false;
        gad::Section s;
        std::copy(std::begin(h.Name), std::end(h.Name), s.name);
        s.va = h.VirtualAddress;
        s.size = h.Misc.VirtualSize;
        g_secs.push_back(s);
    }
    return !g_secs.empty();
}

bool bytes_at(uintptr_t rva, std::span<const uint8_t> expected) {
    std::vector<uint8_t> actual(expected.size());
    return game::safe_read(g_base + rva, actual.data(), actual.size()) &&
           std::equal(actual.begin(), actual.end(), expected.begin());
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
    std::vector<Ctx> ctx;
    safetyhook::MidHookFn fn;
};

std::vector<PointDef> points() {
    const std::vector<Ctx> frame2661 = {{kFrame2661, kFrame2661Bytes}, {kKind2661, kKind2661Bytes},
                                        {kActor2661, kActor2661Bytes}, {kVf48_2661, kVf48_2661Bytes},
                                        {kCreate2661, kCreate2661Bytes}};
    const std::vector<Ctx> npc = {{kNpcFrame, kNpcFrameBytes}, {kNpcComp, kNpcCompBytes}};
    return {
        {kKIn2661, kIn2661, kPatch5, kIn2661Bytes, {}, on_in_2661},
        {kKRoute2661, kRoute2661, kPatch5, kRoute2661Bytes, frame2661, on_route_2661},
        {kKResult2661, kResult2661, kPatch6, kResult2661Bytes, frame2661, on_result_2661},
        {kKMissing, kMissing, kPatch7, kMissingBytes,
         {{kCreateHomes, kCreateHomesBytes}, {kBindType, kBindTypeBytes}, {kBindActor, kBindActorBytes},
          {kBindMissing, kBindMissingBytes}},
         on_missing},
        {kKIn2981, kIn2981, kPatch5, kIn2981Bytes, {}, on_in_2981},
        {kKRoute2981, kRoute2981, kRoute2981Patch, kRoute2981Bytes,
         {{kWrap2981, kWrap2981Bytes}, {kActor2981, kActor2981Bytes}, {kVf48_2981, kVf48_2981Bytes}},
         on_route_2981},
        {kKIn2810, kIn2810, kPatch5, kIn2810Bytes, {}, on_in_2810},
        {kKRoute2810, kRoute2810, kRoute2810Patch, kRoute2810Bytes, {{kVf48_2810, kVf48_2810Bytes}}, on_route_2810},
        {kKRoute2322, kRoute2322, kRoute2322Patch, kRoute2322Bytes,
         {{kField2322, kField2322Bytes}, {kRaw2322, kRaw2322Bytes}},
         on_route_2322},
        {kKNpcEntry, kNpcEntry, kPatch5, kNpcEntryBytes, {}, on_npc_entry},
        {kKNpcRes8, kNpcRes8, kNpcRes8Patch, kNpcRes8Bytes, npc, on_npc_res8},
        {kKNpcOwner, kNpcOwner, kNpcOwnerPatch, kNpcOwnerBytes, npc, on_npc_owner},
        {kKNpcInstance, kNpcInstance, kNpcInstancePatch, kNpcInstanceBytes, npc, on_npc_instance},
        {kKReady0, kReady0, kPatch7, kReady0Bytes, {{kReady0P, kReady0PBytes}, {kReady0M, kReady0MBytes}}, on_ready0},
        {kKReady1, kReady1, 8, kReady1Bytes, {{kReady1M, kReady1MBytes}, {kReady1P, kReady1PBytes}}, on_ready1},
    };
}

bool all_bytes(const PointDef& d) {
    if (!bytes_at(d.rva, d.bytes)) return false;
    return std::all_of(d.ctx.begin(), d.ctx.end(), [](const Ctx& c) { return bytes_at(c.rva, c.bytes); });
}

bool install_point(const PointDef& d) {
    for (const Ctx& c : d.ctx) {
        if (!gad::in_real_code(g_secs, static_cast<uint32_t>(c.rva), static_cast<uint32_t>(c.bytes.size())) ||
            !bytes_at(c.rva, c.bytes)) {
            log::error("GuestStage: {} REFUSED: the code it relies on at exe+0x{:X} ({} bytes) is not the expected "
                       ".rsrc code of build 2976",
                       kKindName[d.kind], c.rva, c.bytes.size());
            return false;
        }
    }
    return p8_install_mid(g_base, g_secs, d.rva, d.patch, d.bytes, d.fn, g_hooks[d.kind], "GuestStage",
                          kKindName[d.kind]);
}

DWORD WINAPI install_thread(LPVOID) {
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (g_base != off::kImageBase || !peek(g_base, dos) || !peek(g_base + dos.e_lfanew, nt) ||
        nt.FileHeader.TimeDateStamp != off::kPeTimestamp) {
        log::error("GuestStage: unsupported game build; guest_stage_diag stays off");
        g_settled.store(true);
        return 0;
    }
    if (!read_sections()) {
        log::error("GuestStage: the game's section table is unreadable; nothing is hooked");
        g_settled.store(true);
        return 0;
    }
    const std::vector<PointDef> defs = points();
    g_wanted.store(static_cast<uint32_t>(defs.size()));
    for (int i = 0; i < 1200; ++i) {  // WorldSystem, as the P8 probes (up to 10 minutes)
        uintptr_t world = 0;
        if (peek(g_base + off::kWorldSystemSlot, world) && world) break;
        Sleep(500);
    }
    for (int i = 0; i < 24000; ++i) {  // the code (up to 2 minutes)
        if (std::all_of(defs.begin(), defs.end(), all_bytes)) break;
        Sleep(5);
    }
    std::string on, refused;
    for (const PointDef& d : defs) {
        const bool ok = install_point(d);
        if (ok) g_hooked.fetch_add(1);
        g_point_ok[d.kind].store(ok);
        (ok ? on : refused) += std::format(" {}", kKindName[d.kind]);
    }
    g_on.store(g_hooked.load() > 0);
    g_settled.store(true, std::memory_order_release);
    log::info("GuestStage ON (guest_stage_diag, READ ONLY): hooked:{} | REFUSED:{} | nothing is written to the game and "
              "no game function is called; lines 'GuestStage [...]' every 5 s from the diagnostics thread (2661/2981/2810 "
              "entry + routing with the corr= token the host's ObserverStage lines print, 2322's field context, the NPC "
              "side 0x9ED290 for the first {} calls of each NPC, M+0x231 writes) and a summary every 60 s; error codes "
              "missing actor 0x{:X} duplicate 0x{:X}",
              on.empty() ? " none" : on, refused.empty() ? " none" : refused, kFirstPerNpc,
              read_global(kErrMissingActor), read_global(kErrDuplicate));
    return 0;
}

}  // namespace

void guest_stage_diag_start(bool on) {
    if (!on) return;
    if (g_started.exchange(true)) return;
    CloseHandle(CreateThread(nullptr, 0, install_thread, nullptr, 0, nullptr));
}

bool guest_stage_diag_wanted() { return g_started.load(); }

bool guest_stage_diag_settled() { return g_settled.load(std::memory_order_acquire); }

bool guest_stage_diag_point_hooked(uintptr_t rva) {
    if (!guest_stage_diag_settled()) return false;
    for (const PointDef& d : points()) {
        if (d.rva == rva && g_point_ok[d.kind].load()) return true;
    }
    return false;
}

void guest_stage_diag_tick() noexcept {
    if (!g_started.load()) return;
    try {
        p::Event e;
        while (g_ring.pop(e)) {
            if (e.kind < kKindCount) drain_one(e);
        }
        if (++g_ticks % 12 == 0 && g_on.load()) summary();
    } catch (...) {
        // allocation failure while formatting: the events of this run are lost, the next run goes on
    }
}

std::string guest_stage_diag_describe() {
    if (!g_started.load()) return {};
    return std::format(" | stage diag {} hooked {}/{} (2661 in {}, 2981 in {}, NPC vf13 {})", g_on.load() ? "on" : "starting",
                       g_hooked.load(), g_wanted.load(), g_stats[kKIn2661].hits.load(), g_stats[kKIn2981].hits.load(),
                       g_stats[kKNpcEntry].hits.load());
}

}  // namespace cdmp::session2
