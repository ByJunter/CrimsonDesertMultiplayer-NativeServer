#include "mod/session2/host_scene_diag.h"
#include "mod/session2/npc_action_probe.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <format>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/guest_stage_diag_logic.h"
#include "mod/session2/observer_stage_logic.h"
#include "mod/session2/offsets_s2.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"
#include "mod/session2/scene_diag_logic.h"
#include "mod/session2/scene_points.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

namespace p = p8p;

// ---------------------------------------------------------------- the points (build 2976, .rsrc)
//
// Read from the authorised read-only PE (D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe) with
// capstone over each whole function (.pdata, chained chunks merged): every patch is whole
// instructions, no branch of the function and no real rel32 (E8/E9/0F8x of .rsrc, each source
// re-disassembled) lands strictly inside it, no RIP-relative operand in it, and the registers the
// callback reads are not written between their definition and the point
// (docs/research/pendientes/p8-s1-escenas-npc.md, "Implementado para la medida"). None of them is
// a P8Probe, GuestStage or ObserverStage point or inside the bytes those verify.

// H1: ServerStartActionLogic 0x2A78CA0 (only caller 0x296CF02, StartActionReq::vf2):
//   0x2A78D0D cmp byte [r15+0x281],0 ; je 0x2A78D28                                patch 8
constexpr uintptr_t kH1 = 0x2A78D0D;
constexpr uint32_t kH1Patch = 8;
constexpr uint8_t kH1Bytes[] = {0x41, 0x80, 0xBF, 0x81, 0x02, 0x00, 0x00, 0x00, 0x74, 0x11, 0x49, 0x8B, 0xCE, 0xE8, 0x71, 0x72};
// 0x2A78CB0 lea rbp,[rsp-0xB0]
constexpr uintptr_t kH1Frame = 0x2A78CB0;
constexpr uint8_t kH1FrameBytes[] = {0x48, 0x8D, 0xAC, 0x24, 0x50, 0xFF, 0xFF, 0xFF};
// 0x2A78CBF mov r13,r9 (session) ; mov r12,r8 (-> the two action layers) ; mov rsi,rdx ; mov r14,rcx
constexpr uintptr_t kH1Args = 0x2A78CBF;
constexpr uint8_t kH1ArgsBytes[] = {0x4D, 0x8B, 0xE9, 0x4D, 0x8B, 0xE0, 0x48, 0x8B, 0xF2, 0x4C, 0x8B, 0xF1};
// 0x2A78CCB mov rbx,[rcx+8] (the actor)
constexpr uintptr_t kH1Actor = 0x2A78CCB;
constexpr uint8_t kH1ActorBytes[] = {0x48, 0x8B, 0x59, 0x08};
// 0x2A78D06 mov r15,[rbp+0x110] (block B, the 5th argument)
constexpr uintptr_t kH1B = 0x2A78D06;
constexpr uint8_t kH1BBytes[] = {0x4C, 0x8B, 0xBD, 0x10, 0x01, 0x00, 0x00};
// 0x2A78D15 je ; mov rcx,r14 ; call 0x232FF90 (-> .xtls) ; xor edi,edi ; mov [rsi],edi ; jmp (return: NO relay)
constexpr uintptr_t kH1Local = 0x2A78D15;
constexpr uint8_t kH1LocalBytes[] = {0x74, 0x11, 0x49, 0x8B, 0xCE, 0xE8, 0x71, 0x72, 0x8B, 0xFF,
                                     0x33, 0xFF, 0x89, 0x3E, 0xE9, 0x70, 0x03, 0x00, 0x00};
//   0x2A79060 cmp byte [rbp+0xF0],0 ; jne 0x2A79082                                 patch 7
constexpr uintptr_t kH1b = 0x2A79060;
constexpr uint32_t kH1bPatch = 7;
constexpr uint8_t kH1bBytes[] = {0x80, 0xBD, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x75, 0x19, 0xC6, 0x44, 0x24, 0x20, 0x00, 0x4D, 0x8B};
// 0x2A78FF8 lea rax,[rbp+0xF0] (ServerStartActionOnServer's out flag)
constexpr uintptr_t kH1bOut = 0x2A78FF8;
constexpr uint8_t kH1bOutBytes[] = {0x48, 0x8D, 0x85, 0xF0, 0x00, 0x00, 0x00};
// 0x2A7906E mov r9,r15 ; lea r8,[rsp+0x60] ; lea rdx,[rbp-0x40] ; mov rcx,r14 ; call 0x2A797C0 (the broadcast)
constexpr uintptr_t kH1bCall = 0x2A7906E;
constexpr uint8_t kH1bCallBytes[] = {0x4D, 0x8B, 0xCF, 0x4C, 0x8D, 0x44, 0x24, 0x60, 0x48, 0x8D,
                                     0x55, 0xC0, 0x49, 0x8B, 0xCE, 0xE8, 0x3E, 0x07, 0x00, 0x00};

// H2: 0x20853D0(ai, handle, &scene key, list): writes [[ai+0x90]] = key and the handle to [[ai+0x90]+8].
//   0x20853D0 push rbx ; push rbp ; push rsi ; push rdi                            patch 5
constexpr uintptr_t kBindAi = 0x20853D0;
constexpr uint8_t kBindAiBytes[] = {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x4D};
// its two callers: the AI's vf107 0x91CD90 and the 2874 handler 0xC0CE50
constexpr uintptr_t kBindAiVf107 = 0x91CDDA, kBindAiVf107Ret = 0x91CDDF;
constexpr uint8_t kBindAiVf107Bytes[] = {0xE8, 0xF1, 0x85, 0x76, 0x01};
constexpr uintptr_t kBindAi2874 = 0xC0D235, kBindAi2874Ret = 0xC0D23A;
constexpr uint8_t kBindAi2874Bytes[] = {0xE8, 0x96, 0x81, 0x47, 0x01};
// 0x9F2FA0(sequencer play-data component, &scene key): vf13's body with the key as a parameter.
//   0x9F2FA0 mov [rsp+0x10],rbx                                                    patch 5
constexpr uintptr_t kRebind = 0x9F2FA0;
constexpr uint8_t kRebindBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20, 0x55, 0x57, 0x41, 0x56, 0x48, 0x8B};
// its callers: vf107 tail-jumps (0x91CE05), the 2874 handler calls (0xC0D24D, returns to 0xC0D252)
constexpr uintptr_t kRebindVf107 = 0x91CE05;
constexpr uint8_t kRebindVf107Bytes[] = {0xE9, 0x96, 0x61, 0x0D, 0x00};
constexpr uintptr_t kRebind2874 = 0xC0D24D, kRebind2874Ret = 0xC0D252;
constexpr uint8_t kRebind2874Bytes[] = {0xE8, 0x4E, 0x5D, 0xDE, 0xFF};
//   0x9F30CB cmp dword [rbp+0x20],0 ; je 0x9F321A (rel32, relocated)                patch 10
constexpr uintptr_t kRebindRes8 = 0x9F30CB;
constexpr uint32_t kRebindRes8Patch = 10;
constexpr uint8_t kRebindRes8Bytes[] = {0x83, 0x7D, 0x20, 0x00, 0x0F, 0x84, 0x45, 0x01, 0x00, 0x00, 0x83, 0x7D, 0x24, 0xFF, 0x0F, 0x84};
// 0x9F2FAE mov rbp,rsp ; sub rsp,0x70 ; mov rsi,rdx ; mov rdi,rcx
constexpr uintptr_t kRebindFrame = 0x9F2FAE;
constexpr uint8_t kRebindFrameBytes[] = {0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x70, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xF9};
// 0x9F30A5 ... mov [rbp+0x20],rax (local_res8 = the stage file's entry, as vf13 0x9ED3CC..)
constexpr uintptr_t kRebindRes8Src = 0x9F30A5;
constexpr uint8_t kRebindRes8SrcBytes[] = {0x48, 0x8B, 0x45, 0xD0, 0x48, 0x8B, 0x48, 0x50,
                                           0x48, 0x8B, 0x04, 0x0A, 0x48, 0x89, 0x45, 0x20};
//   0x9F312D mov rcx,[rbp-0x48] ; test rcx,rcx (the je rel32 after it is checked, not patched)   patch 7
constexpr uintptr_t kRebindInst = 0x9F312D;
constexpr uint32_t kRebindInstPatch = 7;
constexpr uint8_t kRebindInstBytes[] = {0x48, 0x8B, 0x4D, 0xB8, 0x48, 0x85, 0xC9, 0x0F, 0x84, 0xC2, 0x00, 0x00, 0x00};
// 0x9F3118 mov rcx,[rcx+0x148] ; mov rax,[rcx] ; mov r8,rsi ; lea rdx,[rbp-0x48] ; call [rax+8] ; nop
constexpr uintptr_t kRebindLookup = 0x9F3118;
constexpr uint8_t kRebindLookupBytes[] = {0x48, 0x8B, 0x89, 0x48, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x01, 0x4C,
                                          0x8B, 0xC6, 0x48, 0x8D, 0x55, 0xB8, 0xFF, 0x50, 0x08, 0x90};
// 0x9F313A mov r8,[rdi+8] ; lea rdx,[rbp+0x20] ; call 0x43B4B0 (the binding)
constexpr uintptr_t kRebindBind = 0x9F313A;
constexpr uint8_t kRebindBindBytes[] = {0x4C, 0x8B, 0x47, 0x08, 0x48, 0x8D, 0x55, 0x20, 0xE8, 0x69, 0x83, 0xA4, 0xFF};

// G1 on the host's client: the AI block reader's two points (scene_points.h, shared with the guest).
using scp::kAiDone, scp::kAiDoneBytes, scp::kAiDoneCall, scp::kAiDoneCallBytes, scp::kAiDonePatch, scp::kAiDoneThis,
    scp::kAiDoneThisBytes, scp::kAiRead, scp::kAiReadArg, scp::kAiReadArgBytes, scp::kAiReadBytes, scp::kAiReadCall,
    scp::kAiReadCallBytes, scp::kAiReadFrame, scp::kAiReadFrameBytes, scp::kAiReadInit, scp::kAiReadInitBytes,
    scp::kAiReadPatch;

// vf13 0x9ED290's local_res8 on the host (on the guest it is GuestStage's point; never both on one PC).
//   0x9ED401 cmp dword [rbp+0x30],0 ; je 0x9ED63B (rel32, relocated)               patch 10
constexpr uintptr_t kVf13Res8 = 0x9ED401;
constexpr uint32_t kVf13Res8Patch = 10;
constexpr uint8_t kVf13Res8Bytes[] = {0x83, 0x7D, 0x30, 0x00, 0x0F, 0x84, 0x30, 0x02, 0x00, 0x00, 0x83, 0x7D, 0x34, 0xFF, 0x0F, 0x84};
constexpr uintptr_t kVf13Frame = 0x9ED2A8;  // mov rbp,rsp
constexpr uint8_t kVf13FrameBytes[] = {0x48, 0x8B, 0xEC};
constexpr uintptr_t kVf13Comp = 0x9ED2B5;  // mov r14,rcx
constexpr uint8_t kVf13CompBytes[] = {0x4C, 0x8B, 0xF1};

constexpr uint32_t kPatch5 = 5;

// Structures.
using scp::kActor_Comps, scp::kActor_Key, scp::kAi_Scene, scp::kComp_Actor, scp::kComps_PlayData, scp::kHandle_File,
    scp::kHandle_Index, scp::kScene_Flag, scp::kScene_Handle, scp::kVtPlayData;
constexpr uint32_t kActor_Control = 0x90, kActor_User = 0xA0, kUser_Key = 0x60;
constexpr uint32_t kComps_Seq = 0xA8, kSeqComp_Mgr = 0x18;
constexpr uint32_t kComp_Owner = scp::kPlayData_Owner, kComp_Key = scp::kPlayData_Key;
constexpr uint32_t kB_Flag = 0x281, kField_Owner = 0x180, kField_Key = 0x2C8;

// ---------------------------------------------------------------- events

enum Kind : uint16_t {
    kKH1 = 0, kKH1b, kKAiRead, kKVf13, kKBindAi, kKRebind, kKRebindRes8, kKRebindInst, kKOut, kKIn, kKEntry, kKSniff,
    kKindCount
};
constexpr const char* kKindName[kKindCount] = {
    "H1 flag 0x2A78D0D", "H1 relay 0x2A79060",  "G1 AI block 0x206DE6B", "vf13 res8 0x9ED401",
    "AI rebind 0x20853D0", "rebind 0x9F2FA0",   "rebind res8 0x9F30CB",  "rebind instance 0x9F312D",
    "to session 2",       "from the guest",     "entry 0x2BC84E2",       "session 1 (sniffer)"};
constexpr uint64_t kWindowUs = 5'000'000;
constexpr uint32_t kPerWindow = 512;
constexpr uint32_t kPerWindowOut = 4096;  // "to session 2": a zone load queues ~235 NPC 2005 in a second
constexpr uint32_t kFirstPerKey = 2;      // H1: flagged actions per NPC key with a line
constexpr uint32_t kFirstPerNpc = 3;      // the NPC points: events per NPC with a line
constexpr uint64_t kMaxLinesPerKind = 2500;
constexpr size_t kMaxCopies = 512, kMaxCopyBytes = 8u << 20;  // NPC 2005 queued to session 2, kept for the drain

struct KindStats {
    std::atomic<uint64_t> hits{0}, queued{0}, limited{0}, dropped{0}, over_budget{0};
};

uintptr_t g_base = 0;
std::atomic<bool> g_on{false}, g_hooks_done{false};
std::atomic<bool> g_u2_present{false};  // the drain's server_user2_present(), once a second
p::EventRing<8192> g_ring;
KindStats g_stats[kKindCount];
p::WindowLimiter g_lim[kKindCount] = {
    {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs},
    {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs},
    {kPerWindowOut, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}};
std::atomic<uint64_t> g_lines[kKindCount] = {};
std::atomic<uint32_t> g_hooked{0}, g_wanted{0};
SafetyHookMid g_hooks[9];  // the 9 points of host_scene_diag_install
p::KeyCounter<8192> g_keys_h1, g_keys_h1b, g_keys_ai, g_keys_vf13, g_keys_bind, g_keys_rebind, g_keys_res8, g_keys_inst;
// H1 per NPC/actor key and per hash (lock-free, from the server's threads)
scd::CountTable<8192, scd::kH1Cols> g_by_key;
scd::CountTable<1024, scd::kH1Cols> g_by_hash;
// the host client's own StartActionReq (sniffer, C>S): [0] flagged, [1] not
scd::CountTable<8192, 2> g_client_2223;
std::atomic<uint64_t> g_client_2223_bad{0};
// the guest's scene Reqs reaching the server (inject)
std::atomic<uint64_t> g_guest_reqs[scd::kSceneReqCount] = {};
std::atomic<uint32_t> g_trip_seq{0};      // take_frame: 2174s to session 2 (tags the 2005 copies)
std::atomic<uint32_t> g_trip_packets{0};  // take_frame: packets to session 2 since the last 2174

struct Copy {
    uint32_t trip = 0;
    uint64_t t_us = 0;
    std::vector<uint8_t> payload;
};
std::mutex g_copy_mu;
std::deque<Copy> g_copies;  // guarded by g_copy_mu
size_t g_copy_bytes = 0;    // guarded by g_copy_mu
std::atomic<uint64_t> g_copies_dropped{0}, g_copies_taken{0};

// The thread's pending reads between two points of the same call.
struct PendingAi {
    uintptr_t x1 = 0;
    uint32_t resource = 0, node = 0, index = 0;
    uint64_t h0 = 0;
    uintptr_t file = 0;
};
thread_local PendingAi t_ai;
struct PendingH1 {
    uintptr_t comp = 0;
    uint32_t key = 0, hash = 0;
};
thread_local PendingH1 t_h1;
thread_local uintptr_t t_bind_ret = 0;  // 0x20853D0's return address, for the 0x9F2FA0 that follows

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

uint32_t user_key_of(uintptr_t actor) noexcept {
    const uintptr_t user = actor ? rdp(actor + kActor_User) : 0;
    return user ? rdv<uint32_t>(user + kUser_Key) : 0;
}

uintptr_t manager_of(uintptr_t actor) noexcept {
    const uintptr_t comps = actor ? rdp(actor + kActor_Comps) : 0;
    const uintptr_t seq = comps ? rdp(comps + kComps_Seq) : 0;
    return seq ? rdp(seq + kSeqComp_Mgr) : 0;
}

uint32_t rva32(uintptr_t a) noexcept {
    return a >= g_base && a - g_base < 0x10000000 ? static_cast<uint32_t>(a - g_base) : 0;
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

bool budget(Kind k, p::KeyCounter<8192>& keys, uint32_t key, uint32_t first, uint32_t& nth) noexcept {
    nth = keys.bump(key ? key : 1u);
    if (p::within_key_budget(nth, first)) return true;
    g_stats[k].over_budget.fetch_add(1, std::memory_order_relaxed);
    return false;
}

// H1: 0x2A78D0D, every StartActionReq the server's logic handles.
void on_h1(safetyhook::Context& ctx) noexcept {
    g_stats[kKH1].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t actor = ctx.rbx, b = ctx.r15, layers = rdp(ctx.r12);
    const uint32_t key = key_of(actor), control = actor ? rdv<uint32_t>(actor + kActor_Control) : 0;
    uint8_t flag = 0;
    const bool flag_ok = rd(b + kB_Flag, flag);
    uint32_t h[4] = {};
    if (layers) {
        rd(layers, h[0]);
        rd(layers + 4, h[1]);
        rd(layers + scd::kLayerStride, h[2]);
        rd(layers + scd::kLayerStride + 4, h[3]);
    }
    const uint32_t hash = scd::action_hash(h[0], h[1], h[2], h[3]);
    const bool u2 = g_u2_present.load(std::memory_order_relaxed);
    const size_t col = flag ? (u2 ? scd::kFlagU2 : scd::kFlagNoU2) : (u2 ? scd::kPlainU2 : scd::kPlainNoU2);
    bool new_key = false;
    g_by_key.add(key ? key : 1u, col, 1, &new_key);
    g_by_hash.add(static_cast<uint64_t>(hash) | 1ull << 40, col);
    t_h1 = PendingH1{ctx.r14, key, hash};
    // A line: each new key, and the first flagged actions of each NPC key.
    uint32_t nth = 0;
    if (!new_key && !(flag && scd::is_npc_key(key) && budget(kKH1, g_keys_h1, key, kFirstPerKey, nth))) return;
    p::Event e;
    e.v[0] = key | static_cast<uint64_t>(control) << 32;
    e.v[1] = actor;
    e.v[2] = flag | static_cast<uint64_t>(u2) << 8 | static_cast<uint64_t>(flag_ok) << 16 |
             static_cast<uint64_t>(new_key) << 24;
    e.v[3] = h[0] | static_cast<uint64_t>(h[1]) << 32;
    e.v[4] = h[2] | static_cast<uint64_t>(h[3]) << 32;
    e.v[5] = ctx.r13;
    e.v[6] = nth;
    e.v[7] = user_key_of(actor);
    emit(kKH1, e);
}

// H1: 0x2A79060, the plain actions that reached ServerStartActionOnServer with result 0.
void on_h1b(safetyhook::Context& ctx) noexcept {
    npc_action_probe_applied(ctx.r14, key_of(rdp(ctx.r14 + kComp_Actor)));
    if (!g_on.load()) return;
    g_stats[kKH1b].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t comp = ctx.r14, actor = rdp(comp + kComp_Actor);
    const uint32_t key = key_of(actor);
    uint8_t out = 0;
    const bool out_ok = rd(ctx.rbp + 0xF0, out);
    const bool relayed = out_ok && out == 0;
    const size_t col = relayed ? scd::kBroadcast : scd::kNotBroadcast;
    g_by_key.add(key ? key : 1u, col);
    const bool paired = t_h1.comp == comp && t_h1.key == key;
    if (paired) g_by_hash.add(static_cast<uint64_t>(t_h1.hash) | 1ull << 40, col);
    const uint32_t hash = paired ? t_h1.hash : 0;
    t_h1 = PendingH1{};
    uint32_t nth = 0;
    if (!scd::is_npc_key(key) || !budget(kKH1b, g_keys_h1b, key, 1, nth)) return;
    p::Event e;
    e.v[0] = key | static_cast<uint64_t>(out) << 32 | static_cast<uint64_t>(out_ok) << 40;
    e.v[1] = actor;
    e.v[2] = hash | static_cast<uint64_t>(paired) << 32;
    emit(kKH1b, e);
}

uintptr_t play_data_of(uintptr_t actor) noexcept {
    const uintptr_t comps = actor ? rdp(actor + kActor_Comps) : 0;
    const uintptr_t c = comps ? rdp(comps + kComps_PlayData) : 0;
    return c && rdp(c) == g_base + kVtPlayData ? c : 0;
}

// G1 inner: 0x1EB8946, right after 0x1869D70 resolved (resource, node).
void on_ai_read(safetyhook::Context& ctx) noexcept {
    PendingAi a;
    a.x1 = ctx.r14;
    rd(ctx.rbp + 0x50, a.resource);
    rd(ctx.rbp + 0x54, a.node);
    rd(ctx.rax, a.h0);
    rd(ctx.rax + kHandle_Index, a.index);
    rd(ctx.rax + kHandle_File, a.file);
    t_ai = a;
}

// G1: 0x206DE6B, after the AI block reader returned (the handle is in [[ai+0x90]+8]).
void on_ai_done(safetyhook::Context& ctx) noexcept {
    g_stats[kKAiRead].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t ai = ctx.rdi, actor = rdp(ai + kComp_Actor), x1 = rdp(ai + kAi_Scene);
    const uint32_t npc = key_of(actor);
    const PendingAi a = t_ai;
    t_ai = PendingAi{};
    const bool pending = a.x1 && a.x1 == x1;
    uint32_t nth = 0;
    if (!budget(kKAiRead, g_keys_ai, npc, kFirstPerNpc, nth)) return;
    const uintptr_t h = rdp(x1 + kScene_Handle);
    const uintptr_t pd = play_data_of(actor);
    p::Event e;
    e.v[0] = npc | static_cast<uint64_t>(pd ? rdv<uint32_t>(pd + kComp_Owner) : 0) << 32;
    e.v[1] = rdv<uint64_t>(x1);
    e.v[2] = pending ? (a.resource | static_cast<uint64_t>(a.node) << 32) : 0;
    e.v[3] = h ? rdv<uint64_t>(h) : 0;
    e.v[4] = (h ? rdv<uint32_t>(h + kHandle_Index, 0xFFFFFFFFu) : 0xFFFFFFFFu) |
             static_cast<uint64_t>(ctx.rax & 0xFF) << 32 | static_cast<uint64_t>(pending) << 40 |
             static_cast<uint64_t>(pd != 0) << 41 | static_cast<uint64_t>(rdv<uint8_t>(x1 + kScene_Flag)) << 48;
    e.v[5] = h ? rdp(h + kHandle_File) : 0;
    e.v[6] = pd ? rdv<uint64_t>(pd + kComp_Key) : 0;
    e.v[7] = ai;
    e.v[8] = x1;
    e.v[9] = nth;
    emit(kKAiRead, e);
}

void on_vf13(safetyhook::Context& ctx) noexcept {
    g_stats[kKVf13].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t comp = ctx.r14;
    const uint32_t npc = key_of(rdp(comp + kComp_Actor));
    uint32_t nth = 0;
    if (!budget(kKVf13, g_keys_vf13, npc, kFirstPerNpc, nth)) return;
    p::Event e;
    e.v[0] = npc | static_cast<uint64_t>(rdv<uint32_t>(comp + kComp_Owner)) << 32;
    e.v[1] = rdv<uint64_t>(ctx.rbp + 0x30);
    e.v[2] = rdv<uint64_t>(comp + kComp_Key);
    e.v[3] = nth;
    e.v[4] = comp;
    emit(kKVf13, e);
}

void on_bind_ai(safetyhook::Context& ctx) noexcept {
    g_stats[kKBindAi].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t ret = rdp(ctx.rsp), ai = ctx.rcx, handle = ctx.rdx;
    t_bind_ret = ret;
    const uint32_t npc = key_of(rdp(ai + kComp_Actor));
    uint32_t nth = 0;
    if (!budget(kKBindAi, g_keys_bind, npc, kFirstPerNpc + 1, nth)) return;
    p::Event e;
    e.v[0] = npc | static_cast<uint64_t>(rva32(ret)) << 32;
    e.v[1] = rdv<uint64_t>(ctx.r8);
    e.v[2] = rdv<uint64_t>(handle);
    e.v[3] = rdv<uint32_t>(handle + kHandle_Index, 0xFFFFFFFFu);
    e.v[4] = rdp(handle + kHandle_File);
    e.v[5] = ai;
    e.v[6] = nth;
    emit(kKBindAi, e);
}

void on_rebind(safetyhook::Context& ctx) noexcept {
    g_stats[kKRebind].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t ret = rdp(ctx.rsp), comp = ctx.rcx;
    const uintptr_t before = t_bind_ret;
    t_bind_ret = 0;
    const uint32_t npc = key_of(rdp(comp + kComp_Actor));
    uint32_t nth = 0;
    if (!budget(kKRebind, g_keys_rebind, npc, kFirstPerNpc + 1, nth)) return;
    p::Event e;
    e.v[0] = npc | static_cast<uint64_t>(rdv<uint32_t>(comp + kComp_Owner)) << 32;
    e.v[1] = rdv<uint64_t>(ctx.rdx);
    e.v[2] = rva32(ret) | static_cast<uint64_t>(rva32(before)) << 32;
    e.v[3] = comp;
    e.v[4] = nth;
    emit(kKRebind, e);
}

void on_rebind_res8(safetyhook::Context& ctx) noexcept {
    g_stats[kKRebindRes8].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t comp = ctx.rdi;
    const uint32_t npc = key_of(rdp(comp + kComp_Actor));
    uint32_t nth = 0;
    if (!budget(kKRebindRes8, g_keys_res8, npc, kFirstPerNpc + 1, nth)) return;
    p::Event e;
    e.v[0] = npc | static_cast<uint64_t>(rdv<uint32_t>(comp + kComp_Owner)) << 32;
    e.v[1] = rdv<uint64_t>(ctx.rbp + 0x20);
    e.v[2] = rdv<uint64_t>(ctx.rsi);
    e.v[3] = nth;
    emit(kKRebindRes8, e);
}

void on_rebind_inst(safetyhook::Context& ctx) noexcept {
    g_stats[kKRebindInst].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t comp = ctx.rdi;
    const uint32_t npc = key_of(rdp(comp + kComp_Actor));
    uint32_t nth = 0;
    if (!budget(kKRebindInst, g_keys_inst, npc, kFirstPerNpc + 1, nth)) return;
    const uintptr_t owner = rdp(ctx.rbp - 0x20);  // 0x8AE1A0's out (0x9F3105 mov rax,[rbp-0x20])
    p::Event e;
    e.v[0] = npc | static_cast<uint64_t>(rdv<uint32_t>(comp + kComp_Owner)) << 32;
    e.v[1] = rdp(ctx.rbp - 0x48);
    e.v[2] = rdv<uint64_t>(ctx.rsi);
    e.v[3] = manager_of(owner);
    e.v[4] = key_of(owner);
    e.v[5] = nth;
    emit(kKRebindInst, e);
}

// ---------------------------------------------------------------- formatting (observer_stage's thread)

scd::SceneLedger g_ledger;
scd::NpcBook g_book;
scd::TripTracker g_trips;
struct TripScenes {
    uint32_t npc_records = 0, read_on_host = 0, schedule = 0, sequencer = 0, no_scene = 0, carried = 0, differ = 0,
             not_found = 0, unparsed = 0;
};
std::map<uint32_t, TripScenes> g_trip_scenes;     // by take_frame's trip number (drain only)
std::map<uint32_t, scd::Lookup> g_g2_last;        // resource -> last G2 outcome (drain only)
uint64_t g_ticks = 0;                             // drain only


void write_line(Kind k, const std::string& s) {
    if (g_lines[k].fetch_add(1) + 1 > kMaxLinesPerKind) return;
    try {
        log::info("{}", s);
    } catch (...) {
    }
}

std::string head(Kind k, const p::Event& e) {
    return std::format("SceneDiag [{}] {} T{} |", kKindName[k], p8_when(e.qpc_us), e.tid);
}

std::string ret_text(uint32_t r) {
    if (r == kBindAiVf107Ret) return "the AI's vf107 (0x91CDDF)";
    if (r == kBindAi2874Ret) return "the 2874 handler (0xC0D23A)";
    if (r == kRebind2874Ret) return "the 2874 handler (0xC0D252)";
    return r ? std::format("exe+0x{:X}", r) : std::string("?");
}

bool res8_passes(uint64_t v) { return static_cast<uint32_t>(v) != 0 && static_cast<uint32_t>(v >> 32) != 0xFFFFFFFFu; }

std::string res8_text(uint64_t v) {
    return res8_passes(v) ? std::format("0x{:016X} passes", v) : std::format("0x{:016X} EXITS (no stage node)", v);
}

const char* scene_kind_text(int k) { return k == 1 ? "schedule (2335)" : k == 2 ? "sequencer (2661)" : "none"; }

// The ledger line of an NPC: its scene's creation packet on the host's client, relative to its 2005.
std::string scene_rel(const scd::NpcTimeline& t) {
    const scd::SceneLedger::Hit h = g_ledger.find(t.scene, t.resource);
    if (!h.kind) return "its scene is NOT in the host's live ledger";
    return std::format("its scene 0x{:X} = a {} of the host's ledger, created {} relative to its 2005", h.scene,
                       scene_kind_text(h.kind), scd::rel_text(h.t_us, t.t_2005));
}

void on_ai_event(const p::Event& e) {
    const uint32_t npc = static_cast<uint32_t>(e.v[0]), owner = static_cast<uint32_t>(e.v[0] >> 32);
    const uint64_t scene = e.v[1];
    const bool pending = (e.v[4] >> 40) & 1, pd = (e.v[4] >> 41) & 1;
    const uint32_t res = pending ? static_cast<uint32_t>(e.v[2]) : static_cast<uint32_t>(e.v[3]);
    const uint32_t node = pending ? static_cast<uint32_t>(e.v[2] >> 32) : static_cast<uint32_t>(e.v[3] >> 32);
    const uint32_t index = static_cast<uint32_t>(e.v[4]);
    const uintptr_t file = e.v[5];
    const bool valid = static_cast<uint32_t>(e.v[3]) != 0 && static_cast<uint32_t>(e.v[3] >> 32) != 0xFFFFFFFFu &&
                       index != 0xFFFFFFFFu;
    if (scd::NpcTimeline* t = g_book.get(npc)) {
        ++t->reads;
        if (!t->t_2005) t->t_2005 = e.qpc_us;
        if (res) t->resource = res;
        t->node = node;
        t->scene = scene;
        t->handle_valid = valid;
        t->file_loaded = file != 0;
        if (owner) t->owner = owner;
    }
    write_line(kKAiRead,
               std::format("{} NPC 0x{:08X} (#{}) | {} | AI block: resource 0x{:08X} node 0x{:08X} scene key 0x{:X} "
                           "(u8 +0x61 {}){} | handle 0x{:016X} index {} file 0x{:X} -> {} | reader returned {} | its "
                           "play-data component: {}",
                           head(kKAiRead, e), npc, e.v[9], gsd::corr_token(scd::kOpActorLoad, npc, scene, res), res,
                           node, scene, (e.v[4] >> 48) & 0xFF, pending ? "" : " (resource/node from the handle only)",
                           e.v[3], static_cast<int32_t>(index), file, scd::handle_text(e.v[3], index, file),
                           (e.v[4] >> 32) & 0xFF,
                           pd ? std::format("owner comp+0x2D8 0x{:08X} comp+0x2E0 0x{:X}", owner, e.v[6])
                              : std::string("not readable yet")));
}

void drain_one(const p::Event& e) {
    const Kind k = static_cast<Kind>(e.kind);
    switch (k) {
        case kKH1: {
            const uint32_t key = static_cast<uint32_t>(e.v[0]), control = static_cast<uint32_t>(e.v[0] >> 32);
            const uint8_t flag = e.v[2] & 0xFF;
            const bool u2 = (e.v[2] >> 8) & 1, ok = (e.v[2] >> 16) & 1, fresh = (e.v[2] >> 24) & 1;
            const uint32_t h[4] = {static_cast<uint32_t>(e.v[3]), static_cast<uint32_t>(e.v[3] >> 32),
                                   static_cast<uint32_t>(e.v[4]), static_cast<uint32_t>(e.v[4] >> 32)};
            const uintptr_t s2 = server_live_session2();
            write_line(k, std::format(
                              "{} {} actor 0x{:08X} (0x{:X}) controller 0x{:08X} user 0x{:08X} | action hash 0x{:08X} "
                              "(layers 0x{:08X}/0x{:08X} 0x{:08X}/0x{:08X}) | B+0x281 (wire 0x2D3) = {}{} -> {} | from "
                              "{} | user 2 {}",
                              head(k, e), fresh ? "NEW KEY:" : std::format("flagged #{}:", e.v[6]), key, e.v[1], control,
                              e.v[7], scd::action_hash(h[0], h[1], h[2], h[3]), h[0], h[1], h[2], h[3], flag,
                              ok ? "" : " (UNREADABLE)",
                              flag ? "applied by 0x232FF90 and NOT relayed (kept local)"
                                   : "goes on to the checks and the broadcast",
                              e.v[5] && e.v[5] == s2 ? "SESSION 2 (the guest)" : std::format("session 0x{:X}", e.v[5]),
                              u2 ? "logged in" : "absent"));
            break;
        }
        case kKH1b: {
            const uint32_t key = static_cast<uint32_t>(e.v[0]);
            const bool ok = (e.v[0] >> 40) & 1;
            const uint8_t out = static_cast<uint8_t>(e.v[0] >> 32);
            write_line(k, std::format("{} NPC 0x{:08X} (0x{:X}) action 0x{:08X}{}: ServerStartActionOnServer's out flag "
                                      "[rbp+0xF0] = {} -> {}",
                                      head(k, e), key, e.v[1], static_cast<uint32_t>(e.v[2]),
                                      ((e.v[2] >> 32) & 1) ? "" : " (not paired)", ok ? std::to_string(out) : "?",
                                      ok && !out ? "BROADCAST (0x2A7907D call 0x2A797C0)" : "kept (no broadcast)"));
            break;
        }
        case kKAiRead:
            on_ai_event(e);
            break;
        case kKVf13: {
            const uint32_t npc = static_cast<uint32_t>(e.v[0]);
            const bool pass = res8_passes(e.v[1]);
            std::string rel;
            if (scd::NpcTimeline* t = g_book.get(npc)) {
                if (!t->t_vf13) t->t_vf13 = e.qpc_us;
                t->vf13_passed |= pass;
                rel = std::format(" | {} after its 2005; {}", scd::rel_text(e.qpc_us, t->t_2005), scene_rel(*t));
            }
            write_line(k, std::format("{} NPC 0x{:08X} (#{}) owner comp+0x2D8 0x{:08X} comp+0x2E0 0x{:X} | local_res8 {}{}",
                                      head(k, e), npc, e.v[3], static_cast<uint32_t>(e.v[0] >> 32), e.v[2],
                                      res8_text(e.v[1]), rel));
            break;
        }
        case kKBindAi: {
            const uint32_t npc = static_cast<uint32_t>(e.v[0]), ret = static_cast<uint32_t>(e.v[0] >> 32);
            const uint32_t index = static_cast<uint32_t>(e.v[3]);
            std::string rel;
            if (scd::NpcTimeline* t = g_book.get(npc)) {
                ++t->binds;
                if (ret == kBindAiVf107Ret && !t->t_bind_ai) t->t_bind_ai = e.qpc_us;
                if (ret == kBindAi2874Ret && !t->t_bind_2874) t->t_bind_2874 = e.qpc_us;
                rel = std::format(" | {} after its 2005; {}", scd::rel_text(e.qpc_us, t->t_2005), scene_rel(*t));
            }
            write_line(k, std::format("{} NPC 0x{:08X} (#{}) from {} | scene key 0x{:X} | handle 0x{:016X} index {} file "
                                      "0x{:X} -> {} | {}{}",
                                      head(k, e), npc, e.v[6], ret_text(ret), e.v[1], e.v[2],
                                      static_cast<int32_t>(index), e.v[4], scd::handle_text(e.v[2], index, e.v[4]),
                                      gsd::corr_token(scd::kOpActorLoad, npc, e.v[1], static_cast<uint32_t>(e.v[2])),
                                      rel));
            break;
        }
        case kKRebind: {
            const uint32_t npc = static_cast<uint32_t>(e.v[0]);
            const uint32_t ret = static_cast<uint32_t>(e.v[2]), before = static_cast<uint32_t>(e.v[2] >> 32);
            write_line(k, std::format("{} NPC 0x{:08X} (#{}) owner comp+0x2D8 0x{:08X} | scene key 0x{:X} | called {}{}",
                                      head(k, e), npc, e.v[4], static_cast<uint32_t>(e.v[0] >> 32), e.v[1],
                                      ret == kRebind2874Ret ? std::string("by the 2874 handler (0xC0D24D)")
                                                            : std::format("returning to {}", ret_text(ret)),
                                      before == kBindAiVf107Ret ? " | after 0x20853D0 from the AI's vf107 (its tail jump "
                                                                  "0x91CE05)"
                                      : before == kBindAi2874Ret ? " | after 0x20853D0 from the 2874 handler"
                                                                 : ""));
            break;
        }
        case kKRebindRes8: {
            const uint32_t npc = static_cast<uint32_t>(e.v[0]);
            const bool pass = res8_passes(e.v[1]);
            std::string rel;
            if (scd::NpcTimeline* t = g_book.get(npc)) {
                if (!t->t_rebind) t->t_rebind = e.qpc_us;
                t->rebind_passed |= pass;
                rel = std::format(" | {} after its 2005", scd::rel_text(e.qpc_us, t->t_2005));
            }
            write_line(k, std::format("{} NPC 0x{:08X} (#{}) scene key 0x{:X} | local_res8 {}{}", head(k, e), npc, e.v[3],
                                      e.v[2], res8_text(e.v[1]), rel));
            break;
        }
        case kKRebindInst: {
            const uint32_t npc = static_cast<uint32_t>(e.v[0]);
            std::string rel;
            if (scd::NpcTimeline* t = g_book.get(npc)) {
                if (e.v[1] && !t->t_instance) t->t_instance = e.qpc_us;
                rel = std::format(" | {} after its 2005; {}", scd::rel_text(e.qpc_us, t->t_2005), scene_rel(*t));
            }
            write_line(k, std::format("{} NPC 0x{:08X} (#{}) scene key 0x{:X} | owner actor 0x{:08X} manager X 0x{:X} | "
                                      "instance in X+0x148: {}{}",
                                      head(k, e), npc, e.v[5], e.v[2], static_cast<uint32_t>(e.v[4]), e.v[3],
                                      e.v[1] ? std::format("0x{:X} FOUND: 0x43B4B0 binds it now", e.v[1])
                                             : std::string("NONE: no binding"),
                                      rel));
            break;
        }
        case kKOut: {
            const uint16_t op = static_cast<uint16_t>(e.v[0]);
            const uint32_t npcs = static_cast<uint32_t>(e.v[0] >> 32);
            const uint32_t trip = static_cast<uint32_t>(e.v[4]);
            g_trips.on_out(op, e.qpc_us, static_cast<uint32_t>(e.v[2] >> 32), npcs, static_cast<uint32_t>(e.v[1]));
            if (op == scd::kOpMoveFieldAck) {
                const uintptr_t s2 = server_live_session2();
                write_line(k, std::format("{} 2174 to session 0x{:X} ({}), trip {} of user 2: the NPC 2005 queued to it "
                                          "from now on are followed against the host's scene ledger (field #{}: {} "
                                          "schedule(s), {} sequencer(s)){}",
                                          head(k, e), e.v[3], e.v[3] == s2 ? "the live session 2" : "NOT the live one",
                                          trip, g_ledger.field(), g_ledger.schedules().size(),
                                          g_ledger.sequencers().size(), (e.v[2] & 1) ? "" : " | NOT pushed"));
            }
            break;
        }
        case kKIn:
            g_trips.on_in(static_cast<uint16_t>(e.v[0]), e.qpc_us);
            break;
        case kKEntry: {
            const uint32_t user = static_cast<uint32_t>(e.v[0] >> 32), owner = static_cast<uint32_t>(e.v[1]);
            g_trips.on_entry(e.qpc_us, user == owner);
            if (user == owner) break;
            const scd::TripFacts* f = g_trips.current();
            write_line(k, std::format("{} user 0x{:08X} actor 0x{:08X} entered (0x2BC84E2, END of MoveFieldCompleteCore) "
                                      "field 0x{:X} owned by 0x{:08X} | this trip so far: packets to session 2 {}, NPC "
                                      "2005 records before the 2549 {}, after it {} | the host's live scene ledger (field "
                                      "#{}): {} schedule(s) (2335), {} sequencer(s) (2661), {} NPC init(s) (2874)",
                                      head(k, e), user, static_cast<uint32_t>(e.v[0]), e.v[2], owner,
                                      f ? f->packets : 0, f ? f->npc_before_2549 : 0,
                                      f ? f->npc_2549_to_2144 + f->npc_after_2144 : 0, g_ledger.field(),
                                      g_ledger.schedules().size(), g_ledger.sequencers().size(), g_ledger.inits().size()));
            break;
        }
        case kKSniff: {
            const uint16_t op = static_cast<uint16_t>(e.v[0]);
            const uint32_t a = static_cast<uint32_t>(e.v[1]), b = static_cast<uint32_t>(e.v[1] >> 32);
            std::string what;
            switch (op) {
                case scd::kOpMoveFieldAck:
                    g_ledger.new_field(b, e.qpc_us);
                    what = std::format("2174 to the host's client (actor 0x{:08X}, owner 0x{:08X}): NEW FIELD #{} of the "
                                       "ledger (the client resets its managers)",
                                       a, b, g_ledger.field());
                    break;
                case scd::kOpScheduleCreate:
                    g_ledger.schedule_create({e.v[2], a, b, e.qpc_us});
                    what = std::format("2335 CreateNPCScheduleStageAck actor 0x{:08X} scene 0x{:X} resource 0x{:08X} "
                                       "(+5 0x{:X} +13 {}) | {} | live schedules {}",
                                       a, e.v[2], b, static_cast<uint32_t>(e.v[3]), (e.v[3] >> 32) & 0xFF,
                                       gsd::corr_token(op, a, e.v[2], b), g_ledger.schedules().size());
                    break;
                case scd::kOpScheduleDelete: {
                    const bool was = g_ledger.schedule_delete(e.v[2]);
                    what = std::format("2126 DeleteNPCScheduleStageAck actor 0x{:08X} scene 0x{:X} ({}) | {} | live "
                                       "schedules {}",
                                       a, e.v[2], was ? "removed" : "NOT in the ledger", gsd::corr_token(op, a, e.v[2], 0),
                                       g_ledger.schedules().size());
                    break;
                }
                case scd::kOpScheduleClosed: {
                    const bool was = g_ledger.schedule_closed(b);
                    what = std::format("2411 ScheduleStageClosedAck actor 0x{:08X} id 0x{:08X} ({}) | {} | live schedules "
                                       "{}",
                                       a, b, was ? "removed" : "no live schedule with that low key",
                                       gsd::corr_token(op, a, 0, b), g_ledger.schedules().size());
                    break;
                }
                case scd::kOpPrepare: {
                    const uint32_t res = static_cast<uint32_t>(e.v[3]), n = static_cast<uint32_t>(e.v[3] >> 32);
                    g_ledger.prepare({e.v[2], a, b, res, n, e.qpc_us});
                    what = std::format("2661 PrepareSequencerAck actor 0x{:08X} owner 0x{:08X} sequencer 0x{:X} resource "
                                       "0x{:08X} bindings {} | {} | live sequencers {}",
                                       a, b, e.v[2], res, n, gsd::corr_token(op, a, e.v[2], n),
                                       g_ledger.sequencers().size());
                    break;
                }
                case scd::kOpDeleteSequencer: {
                    const bool was = g_ledger.remove_sequencer(e.v[2]);
                    what = std::format("2810 DeleteSequencerAck actor 0x{:08X} sequencer 0x{:X} ({}) | {} | live "
                                       "sequencers {}",
                                       a, e.v[2], was ? "removed" : "NOT in the ledger", gsd::corr_token(op, a, e.v[2], b),
                                       g_ledger.sequencers().size());
                    break;
                }
                case scd::kOpInitSchedule: {
                    const uint32_t res = static_cast<uint32_t>(e.v[2]), node = static_cast<uint32_t>(e.v[2] >> 32);
                    g_ledger.init({b, a, res, node, e.v[3], e.qpc_us});
                    what = std::format("2874 InitializeNPCScheduleAck actor 0x{:08X} NPC 0x{:08X} resource 0x{:08X} node "
                                       "0x{:08X} scene 0x{:X}{} | {} | the native re-binding path (0xC0CE50)",
                                       a, b, res, node, e.v[3], e.v[4] ? "" : " (NOT 64 bytes)",
                                       gsd::corr_token(op, a, e.v[3], b));
                    break;
                }
                default:
                    break;
            }
            if (!what.empty()) {
                write_line(k, std::format("SceneDiag [{}] {} | {}", kKindName[k], p8_when(e.qpc_us), what));
            }
            break;
        }
        default:
            break;
    }
}

// The NPC 2005 queued to session 2: does the record carry the AI block the host's client read?
void drain_copies() {
    std::deque<Copy> mine;
    {
        std::lock_guard lk(g_copy_mu);
        mine.swap(g_copies);
        g_copy_bytes = 0;
    }
    for (const Copy& c : mine) {
        g_copies_taken.fetch_add(1);
        if (g_trip_scenes.size() >= 64 && !g_trip_scenes.count(c.trip)) g_trip_scenes.erase(g_trip_scenes.begin());
        TripScenes& ts = g_trip_scenes[c.trip];
        const auto recs = scd::parse_2005(c.payload);
        if (!recs) {
            ++ts.unparsed;
            continue;
        }
        for (const scd::Record2005& r : *recs) {
            if (!scd::is_npc_key(r.key)) continue;
            ++ts.npc_records;
            const scd::NpcTimeline* t = g_book.find(r.key);
            if (!t || !t->reads) continue;
            ++ts.read_on_host;
            const scd::SceneLedger::Hit h = g_ledger.find(t->scene, t->resource);
            if (h.kind == 1) ++ts.schedule;
            if (h.kind == 2) ++ts.sequencer;
            if (!h.kind) ++ts.no_scene;
            const std::span<const uint8_t> rec(c.payload.data() + r.off, r.len);
            const auto blocks = scd::find_ai_blocks(rec, t->resource, t->scene);
            if (blocks.empty()) {
                ++ts.not_found;
            } else if (blocks.front().node == t->node) {
                ++ts.carried;
            } else {
                ++ts.differ;
            }
            if (ts.npc_records <= 12) {
                write_line(kKOut,
                           std::format("SceneDiag [to session 2] {} | NPC 2005 of 0x{:08X} queued to user 2 (trip {}): "
                                       "{} | the host's client read resource 0x{:08X} node 0x{:08X} scene 0x{:X} ({}) | "
                                       "its scene: {} | user 2 got NO 2335/2661 of it (natively ToTarget the owner)",
                                       p8_when(c.t_us), r.key, c.trip,
                                       blocks.empty() ? std::string("its record does NOT hold that block")
                                                      : std::format("its record holds the same block at +0x{:X} (node "
                                                                    "0x{:08X})",
                                                                    blocks.front().off, blocks.front().node),
                                       t->resource, t->node, t->scene,
                                       t->handle_valid ? "valid there" : "invalid there too",
                                       scene_kind_text(h.kind)));
            }
        }
    }
}

void trip_lines() {
    for (auto f = g_trips.take_due(p8_now_us()); f; f = g_trips.take_due(p8_now_us())) {
        const scd::FirstNpc pos = scd::first_npc_position(*f);
        const auto it = g_trip_scenes.find(f->index);
        const TripScenes ts = it != g_trip_scenes.end() ? it->second : TripScenes{};
        write_line(kKOut,
                   std::format("SceneDiag [trip] {} | trip {} of user 2: 2174 {} | 2549 {} (x{}) | entry 0x2BC84E2 {} | "
                               "2144 {} | 2322 {} | first NPC 2005 {} (packet #{} of {} to session 2, NPC 0x{:08X}) -> {} | "
                               "NPC records: before 2549 {}, 2549..2144 {}, after 2144 {} | of those queued: read on the "
                               "host's client {}, whose scene is a live schedule {} / sequencer {} / none {}; the record "
                               "carries the host's block {} (other node {}, not found {}, unparsed 2005 {}) | PLAN, NOT "
                               "SENT: a snapshot of the host's ledger (field #{}: {} x 2335, {} x 2981+2661) would go "
                               "before packet #{}",
                               p8_when(f->t2174), f->index, "t=0", scd::rel_text(f->t2549, f->t2174), f->n2549,
                               scd::rel_text(f->t_entry, f->t2174), scd::rel_text(f->t2144, f->t2174),
                               scd::rel_text(f->t2322, f->t2174), scd::rel_text(f->t_first_npc, f->t2174),
                               f->first_npc_packet, f->packets, f->first_npc_key, scd::first_npc_text(pos),
                               f->npc_before_2549, f->npc_2549_to_2144, f->npc_after_2144, ts.read_on_host, ts.schedule,
                               ts.sequencer, ts.no_scene, ts.carried, ts.differ, ts.not_found, ts.unparsed,
                               g_ledger.field(), g_ledger.schedules().size(), g_ledger.sequencers().size(),
                               f->first_npc_packet ? f->first_npc_packet : f->packets + 1));
    }
}

struct GameMem {
    bool u8(uintptr_t a, uint8_t& v) const noexcept { return rd(a, v); }
    bool u32(uintptr_t a, uint32_t& v) const noexcept { return rd(a, v); }
    bool ptr(uintptr_t a, uintptr_t& v) const noexcept { return rd(a, v); }
};

// G2 on the host's client: are the ledger's files in its container? (every 5 s, at most 64, changes only)
void g2_pass() {
    using L = scd::ScheduleLayout;
    const uintptr_t mgr = rdp(g_base + L::kManagerSlot);
    const uintptr_t c = mgr ? rdp(mgr + L::kManager_Container) : 0;
    size_t n = 0;
    for (uint32_t res : g_ledger.resources()) {
        if (++n > 64) break;
        const scd::LookupResult r = scd::schedule_lookup(GameMem{}, c, res, 0, false);
        auto [it, fresh] = g_g2_last.try_emplace(res, r.what);
        if (!fresh && it->second == r.what) continue;
        it->second = r.what;
        write_line(kKSniff, std::format("SceneDiag [G2 host] {} | resource 0x{:08X} in the host client's "
                                        "NPCScheduleManager (0x{:X}, buckets {}, used {}): {} (file 0x{:X})",
                                        p8_when(p8_now_us()), res, c, r.buckets, r.used, scd::lookup_text(r.what),
                                        r.file));
    }
}

void summary() {
    // H1
    auto rows = g_by_key.rows();
    uint64_t tot[scd::kH1Cols] = {}, npc_flagged_keys = 0;
    for (const auto& r : rows) {
        for (size_t j = 0; j < scd::kH1Cols; ++j) tot[j] += r.c[j];
        if (scd::is_npc_key(static_cast<uint32_t>(r.key)) && (r.c[scd::kFlagNoU2] + r.c[scd::kFlagU2])) ++npc_flagged_keys;
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.c[scd::kFlagNoU2] + a.c[scd::kFlagU2] > b.c[scd::kFlagNoU2] + b.c[scd::kFlagU2];
    });
    std::string keys;
    size_t shown = 0;
    for (const auto& r : rows) {
        if (shown >= 40 || r.c[scd::kFlagNoU2] + r.c[scd::kFlagU2] == 0) break;
        ++shown;
        keys += std::format(" 0x{:08X}:F{}/{} P{}/{} B{} K{}", static_cast<uint32_t>(r.key), r.c[scd::kFlagNoU2],
                            r.c[scd::kFlagU2], r.c[scd::kPlainNoU2], r.c[scd::kPlainU2], r.c[scd::kBroadcast],
                            r.c[scd::kNotBroadcast]);
    }
    auto hrows = g_by_hash.rows();
    std::sort(hrows.begin(), hrows.end(), [](const auto& a, const auto& b) {
        return a.c[0] + a.c[1] + a.c[2] + a.c[3] > b.c[0] + b.c[1] + b.c[2] + b.c[3];
    });
    std::string hashes;
    for (size_t i = 0; i < hrows.size() && i < 20; ++i) {
        const auto& r = hrows[i];
        hashes += std::format(" 0x{:08X}:F{}/{} P{}/{} B{}", static_cast<uint32_t>(r.key), r.c[scd::kFlagNoU2],
                              r.c[scd::kFlagU2], r.c[scd::kPlainNoU2], r.c[scd::kPlainU2], r.c[scd::kBroadcast]);
    }
    uint64_t cf = 0, cp = 0, cnpc = 0;
    for (const auto& r : g_client_2223.rows()) {
        cf += r.c[0];
        cp += r.c[1];
        if (r.c[0]) ++cnpc;
    }
    const uint64_t plain = tot[scd::kPlainNoU2] + tot[scd::kPlainU2];
    const uint64_t decided = tot[scd::kBroadcast] + tot[scd::kNotBroadcast];
    log::info("SceneDiag [H1 summary] {} | StartActionReq at 0x2A78D0D: flagged (B+0x281 != 0: applied, NOT relayed) {} "
              "(user 2 absent {} / logged in {}) | plain {} ({} / {}) -> broadcast {}, kept by the out flag {}, stopped "
              "before (checks 3-4 or an error) {} | NPC keys with flagged actions {} | per key (F absent/in P absent/in "
              "B broadcast K kept), top by flagged:{} | per hash, top 20:{} | the host client's own 2223 (sniffer, C>S): "
              "flagged {} plain {}, keys with flagged {} (unparsed {}) | tables: keys {}{} untracked {}, hashes {}",
              p8_when(p8_now_us()), tot[scd::kFlagNoU2] + tot[scd::kFlagU2], tot[scd::kFlagNoU2], tot[scd::kFlagU2],
              plain, tot[scd::kPlainNoU2], tot[scd::kPlainU2], tot[scd::kBroadcast], tot[scd::kNotBroadcast],
              plain >= decided ? plain - decided : 0, npc_flagged_keys, keys.empty() ? std::string(" none") : keys,
              hashes.empty() ? std::string(" none") : hashes, cf, cp, cnpc, g_client_2223_bad.load(), g_by_key.size(),
              g_by_key.closed() ? " (CLOSED)" : "", g_by_key.untracked(), g_by_hash.size());
    // H2
    const scd::NpcBook::Summary s = g_book.summary();
    std::string late;
    size_t n = 0;
    for (const auto& [k, t] : g_book.all()) {
        if (!t.t_bind_ai && !t.t_bind_2874 && !t.t_instance) continue;
        if (++n > 24) break;
        late += std::format(" 0x{:08X}(res8 {} | AI {} | 2874 {} | 0x9F30CB {} {} | instance {})", k,
                            t.vf13_passed ? "pass" : t.t_vf13 ? "EXIT" : "?", scd::rel_text(t.t_bind_ai, t.t_2005),
                            scd::rel_text(t.t_bind_2874, t.t_2005), scd::rel_text(t.t_rebind, t.t_2005),
                            t.rebind_passed ? "pass" : "exit", scd::rel_text(t.t_instance, t.t_2005));
    }
    log::info("SceneDiag [H2 summary] {} | the host's client: NPC AI blocks read {} (with a scene {}), handle valid {}, "
              "file NOT loaded {}, node not found {} | vf13 local_res8 passed {} | re-bound by the AI's vf107 {} / by a "
              "2874 {} | 0x9F2FA0 local_res8 passed {} | instance found {} | NPCs bound later (times from their 2005):{} "
              "| book overflow {}",
              p8_when(p8_now_us()), s.npcs, s.with_scene, s.valid, s.file_missing, s.node_missing, s.vf13_passed,
              s.bound_ai, s.bound_2874, s.rebind_passed, s.instance, late.empty() ? std::string(" none") : late,
              g_book.overflow());
    // H3
    std::string reqs;
    for (size_t i = 0; i < scd::kSceneReqCount; ++i) {
        if (const uint64_t v = g_guest_reqs[i].load()) reqs += std::format(" {} x{}", scd::kSceneReqs[i], v);
    }
    log::info("SceneDiag [H3 summary] {} | host scene ledger: field #{} (owner 0x{:08X}, since {}): schedules {} "
              "sequencers {} NPC inits {} | totals: 2335 {} 2126 {} 2411 {} 2661 {} 2810 {} 2874 {} (dropped at field "
              "changes {}, full {}) | user 2 trips {} | NPC 2005 copies taken {} dropped {} | the guest's scene Reqs "
              "that reached the server:{}",
              p8_when(p8_now_us()), g_ledger.field(), g_ledger.owner(), p8_when(g_ledger.field_t_us()),
              g_ledger.schedules().size(), g_ledger.sequencers().size(), g_ledger.inits().size(), g_ledger.created(),
              g_ledger.deleted(), g_ledger.closed(), g_ledger.prepared(), g_ledger.removed(), g_ledger.initialized(),
              g_ledger.dropped_at_field_change(), g_ledger.full(), g_trips.trips(), g_copies_taken.load(),
              g_copies_dropped.load(), reqs.empty() ? std::string(" none") : reqs);
    std::string st;
    for (int k = 0; k < kKindCount; ++k) {
        const KindStats& x = g_stats[k];
        st += std::format(" | {}: hits {} queued {} limited {} over budget {} dropped {} lines {}", kKindName[k],
                          x.hits.load(), x.queued.load(), x.limited.load(), x.over_budget.load(), x.dropped.load(),
                          g_lines[k].load());
    }
    log::info("SceneDiag counters | hooked {}/{} | ring dropped {}{}", g_hooked.load(), g_wanted.load(), g_ring.dropped(),
              st);
}

// ---------------------------------------------------------------- install

struct Ctx {
    uintptr_t rva;
    std::span<const uint8_t> bytes;
};
struct PointDef {
    Kind kind;  // the event kind (kKAiRead also names its inner point)
    const char* name;
    uintptr_t rva;
    uint32_t patch;
    std::span<const uint8_t> bytes;
    std::vector<Ctx> ctx;
    safetyhook::MidHookFn fn;
};

bool bytes_at(uintptr_t rva, std::span<const uint8_t> expected) {
    std::vector<uint8_t> actual(expected.size());
    return game::safe_read(g_base + rva, actual.data(), actual.size()) &&
           std::equal(actual.begin(), actual.end(), expected.begin());
}

}  // namespace

void host_scene_diag_configure(bool on) { g_on.store(on); }

bool host_scene_diag_wanted() { return g_on.load() || npc_action_probe_wanted(); }

bool host_scene_diag_configured() { return g_on.load(); }

void host_scene_diag_install(uintptr_t base, std::span<const gad::Section> secs) {
    if (!host_scene_diag_wanted() || g_hooks_done.exchange(true)) return;
    g_base = base;
    const std::vector<Ctx> h1 = {{kH1Frame, kH1FrameBytes}, {kH1Args, kH1ArgsBytes}, {kH1Actor, kH1ActorBytes},
                                 {kH1B, kH1BBytes},         {kH1Local, kH1LocalBytes}};
    const std::vector<Ctx> rebind = {{kRebindFrame, kRebindFrameBytes}, {kRebindVf107, kRebindVf107Bytes},
                                     {kRebind2874, kRebind2874Bytes}};
    const std::vector<PointDef> defs = {
        {kKH1, "H1 flag 0x2A78D0D", kH1, kH1Patch, kH1Bytes, h1, on_h1},
        {kKH1b, "H1 relay 0x2A79060", kH1b, kH1bPatch, kH1bBytes,
         {{kH1Frame, kH1FrameBytes}, {kH1Args, kH1ArgsBytes}, {kH1bOut, kH1bOutBytes}, {kH1bCall, kH1bCallBytes}}, on_h1b},
        {kKAiRead, "G1 inner 0x1EB8946", kAiRead, kAiReadPatch, kAiReadBytes,
         {{kAiReadFrame, kAiReadFrameBytes}, {kAiReadArg, kAiReadArgBytes}, {kAiReadInit, kAiReadInitBytes},
          {kAiReadCall, kAiReadCallBytes}},
         on_ai_read},
        {kKAiRead, "G1 0x206DE6B", kAiDone, kAiDonePatch, kAiDoneBytes,
         {{kAiDoneThis, kAiDoneThisBytes}, {kAiDoneCall, kAiDoneCallBytes}}, on_ai_done},
        {kKVf13, "vf13 res8 0x9ED401", kVf13Res8, kVf13Res8Patch, kVf13Res8Bytes,
         {{kVf13Frame, kVf13FrameBytes}, {kVf13Comp, kVf13CompBytes}}, on_vf13},
        {kKBindAi, "AI rebind 0x20853D0", kBindAi, kPatch5, kBindAiBytes,
         {{kBindAiVf107, kBindAiVf107Bytes}, {kBindAi2874, kBindAi2874Bytes}}, on_bind_ai},
        {kKRebind, "rebind 0x9F2FA0", kRebind, kPatch5, kRebindBytes, rebind, on_rebind},
        {kKRebindRes8, "rebind res8 0x9F30CB", kRebindRes8, kRebindRes8Patch, kRebindRes8Bytes,
         {{kRebindFrame, kRebindFrameBytes}, {kRebindRes8Src, kRebindRes8SrcBytes}}, on_rebind_res8},
        {kKRebindInst, "rebind instance 0x9F312D", kRebindInst, kRebindInstPatch, kRebindInstBytes,
         {{kRebindFrame, kRebindFrameBytes}, {kRebindLookup, kRebindLookupBytes}, {kRebindBind, kRebindBindBytes}},
         on_rebind_inst},
    };
    g_wanted.store(static_cast<uint32_t>(defs.size()));

    std::string on, refused;
    bool applied_mid_ready = false;
    for (size_t i = 0; i < defs.size(); ++i) {
        const PointDef& d = defs[i];
        if (!g_on.load() && d.rva != kH1b) continue;
        bool ok = true;
        for (const Ctx& c : d.ctx) {
            if (!gad::in_real_code(secs, static_cast<uint32_t>(c.rva), static_cast<uint32_t>(c.bytes.size())) ||
                !bytes_at(c.rva, c.bytes)) {
                log::error("SceneDiag: {} REFUSED: the code it relies on at exe+0x{:X} ({} bytes) is not the expected "
                           ".rsrc code of build 2976",
                           d.name, c.rva, c.bytes.size());
                ok = false;
                break;
            }
        }
        ok = ok && p8_install_mid(base, secs, d.rva, d.patch, d.bytes, d.fn, g_hooks[i], "SceneDiag", d.name);
        if (d.rva == kH1b) applied_mid_ready = ok;
        if (ok) g_hooked.fetch_add(1);
        (ok ? on : refused) += std::format(" {}", d.name);
    }
    npc_action_probe_install(base, secs, applied_mid_ready);
    if (!g_on.load()) return;
    log::info("SceneDiag ON (s1_scene_diag, host, READ ONLY): hooked:{} | REFUSED:{} | nothing is written to the game, no "
              "game function is called, nothing is sent; lines 'SceneDiag [...]' once a second from observer_stage's "
              "thread: H1 (each new actor key at the flag check, the first {} flagged actions of each NPC, the relay "
              "decision), H2 (the host client's NPC AI block, vf13 and the re-binding 0x20853D0/0x9F2FA0, first {} per "
              "NPC), H3 (the host's scene ledger from its client's capture, user 2's trips and the NPC 2005 queued to "
              "it); summaries every 60 s",
              on.empty() ? " none" : on, refused.empty() ? " none" : refused, kFirstPerKey, kFirstPerNpc);
}

// S33 review: npc_action_probe_drain (it formats, so it may throw) runs on user2_stages' thread inside its try:
// nothing here may throw outside this try (observer_stage calls this without one).
void host_scene_diag_drain() {
    if (!g_on.load()) return;
    try {
        g_u2_present.store(server_user2_present(), std::memory_order_relaxed);
        p::Event e;
        while (g_ring.pop(e)) {
            if (e.kind < kKindCount) drain_one(e);
        }
        drain_copies();
        trip_lines();
        ++g_ticks;
        if (g_ticks % 5 == 0) g2_pass();
        if (g_ticks % 60 == 0) summary();
    } catch (...) {
        // allocation failure while formatting: these events are lost, the next pass goes on
    }
}

std::string host_scene_diag_describe() {
    if (!g_on.load()) return {};
    return std::format("SceneDiag hooked {}/{} H1 {} (flag check) G1 {} trips {}", g_hooked.load(), g_wanted.load(),
                       g_stats[kKH1].hits.load(), g_stats[kKAiRead].hits.load(), g_trip_seq.load());
}

void host_scene_diag_on_out(uint16_t opcode, const uint8_t* packet, size_t size, uintptr_t self, bool pushed) noexcept {
    if (!g_on.load(std::memory_order_relaxed) || !packet) return;
    g_stats[kKOut].hits.fetch_add(1, std::memory_order_relaxed);
    // Every packet is numbered within its trip (the 2174 is #1); only the followed ones become events.
    uint32_t packet_no = 0;
    if (opcode == scd::kOpMoveFieldAck) {
        g_trip_seq.fetch_add(1, std::memory_order_relaxed);
        g_trip_packets.store(1, std::memory_order_relaxed);
        packet_no = 1;
    } else {
        packet_no = g_trip_packets.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    scd::Scan2005 s;
    if (opcode == scd::kOpActorLoad && size > scd::kHeader) s = scd::scan_2005(packet + scd::kHeader, size - scd::kHeader);
    const bool followed = opcode == scd::kOpMoveFieldAck || opcode == scd::kOpMoveFieldCompleteAck ||
                          opcode == scd::kOpMilestone || (opcode == scd::kOpActorLoad && s.npcs);
    if (!followed) return;
    p::Event e;
    e.v[0] = opcode | static_cast<uint64_t>(size & 0xFFFF) << 16 | static_cast<uint64_t>(s.npcs) << 32;
    e.v[1] = s.first_npc | static_cast<uint64_t>(s.records) << 32;
    e.v[2] = static_cast<uint64_t>(pushed) | static_cast<uint64_t>(packet_no) << 32;
    e.v[3] = self;  // the session written to (compared with the live session 2 by the drain)
    e.v[4] = g_trip_seq.load(std::memory_order_relaxed);
    emit(kKOut, e);
    if (!s.npcs) return;
    try {
        Copy c;
        c.trip = g_trip_seq.load();
        c.t_us = e.qpc_us;
        c.payload.assign(packet + scd::kHeader, packet + size);
        std::lock_guard lk(g_copy_mu);
        if (g_copies.size() >= kMaxCopies || g_copy_bytes + c.payload.size() > kMaxCopyBytes) {
            g_copies_dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        g_copy_bytes += c.payload.size();
        g_copies.push_back(std::move(c));
    } catch (...) {
        g_copies_dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

void host_scene_diag_on_in(uint16_t opcode, std::span<const uint8_t> packet) noexcept {
    if (!g_on.load(std::memory_order_relaxed)) return;
    if (const int i = scd::scene_req_index(opcode); i >= 0) g_guest_reqs[i].fetch_add(1, std::memory_order_relaxed);
    if (opcode != scd::kOpMoveFieldCompleteReq) return;
    g_stats[kKIn].hits.fetch_add(1, std::memory_order_relaxed);
    p::Event e;
    e.v[0] = opcode | static_cast<uint64_t>(packet.size() & 0xFFFF) << 16;
    emit(kKIn, e);
}

void host_scene_diag_on_entry(uintptr_t field, uintptr_t actor) noexcept {
    if (!g_on.load(std::memory_order_relaxed)) return;
    g_stats[kKEntry].hits.fetch_add(1, std::memory_order_relaxed);
    p::Event e;
    e.v[0] = key_of(actor) | static_cast<uint64_t>(user_key_of(actor)) << 32;
    e.v[1] = rdv<uint32_t>(field + kField_Owner) | static_cast<uint64_t>(rdv<uint32_t>(field + kField_Key)) << 32;
    e.v[2] = field;
    emit(kKEntry, e);
}

void host_scene_diag_sniffed(const uint8_t* packet, size_t size, uint64_t t_us, bool server_to_client) noexcept {
    if (!g_on.load(std::memory_order_relaxed) || !packet || size < scd::kHeader) return;
    const uint16_t op = scd::le16(packet);
    const std::span<const uint8_t> pl(packet + scd::kHeader, size - scd::kHeader);
    if (!server_to_client) {
        if (op != scd::kOpStartActionReq) return;
        const auto w = scd::parse_start_action(pl);
        if (!w) {
            g_client_2223_bad.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        g_client_2223.add(w->key ? w->key : 1u, w->flag ? 0 : 1);
        return;
    }
    p::Event e;
    e.qpc_us = t_us;
    e.v[0] = op | static_cast<uint64_t>(pl.size() & 0xFFFF) << 16;
    switch (op) {
        case scd::kOpMoveFieldAck:
            if (pl.size() < 13) return;
            e.v[1] = scd::le32(&pl[1]) | static_cast<uint64_t>(scd::le32(&pl[9])) << 32;
            break;
        case scd::kOpScheduleCreate: {
            const auto s = scd::parse_schedule_create(pl);
            if (!s) return;
            e.v[1] = s->actor | static_cast<uint64_t>(s->resource) << 32;
            e.v[2] = s->scene;
            e.v[3] = s->stamp | static_cast<uint64_t>(s->b13) << 32 | static_cast<uint64_t>(s->kind) << 40;
            break;
        }
        case scd::kOpScheduleDelete: {
            const auto s = scd::parse_schedule_delete(pl);
            if (!s) return;
            e.v[1] = s->actor;
            e.v[2] = s->scene;
            break;
        }
        case scd::kOpScheduleClosed: {
            const auto s = scd::parse_schedule_closed(pl);
            if (!s) return;
            e.v[1] = s->actor | static_cast<uint64_t>(s->id) << 32;
            break;
        }
        case scd::kOpPrepare: {
            const auto res = scd::prepare_resource(pl);
            if (!res || (pl.size() - obs::kPreparePrefix) % obs::kBindingWire != 0) return;
            e.v[1] = scd::le32(&pl[1]) | static_cast<uint64_t>(scd::le32(&pl[5])) << 32;
            e.v[2] = scd::le64(&pl[9]);
            e.v[3] = *res | static_cast<uint64_t>((pl.size() - obs::kPreparePrefix) / obs::kBindingWire) << 32;
            break;
        }
        case scd::kOpDeleteSequencer: {
            const auto d = obs::parse_delete_payload(pl);
            if (!d) return;
            e.v[1] = d->actor | static_cast<uint64_t>(d->extra) << 32;
            e.v[2] = d->seq;
            break;
        }
        case scd::kOpInitSchedule: {
            const auto s = scd::parse_init_schedule(pl);
            if (!s) return;
            e.v[1] = s->actor | static_cast<uint64_t>(s->npc) << 32;
            e.v[2] = s->resource | static_cast<uint64_t>(s->node) << 32;
            e.v[3] = s->scene;
            e.v[4] = s->complete;
            break;
        }
        default:
            return;
    }
    g_stats[kKSniff].hits.fetch_add(1, std::memory_order_relaxed);
    emit(kKSniff, e);
}

}  // namespace cdmp::session2
