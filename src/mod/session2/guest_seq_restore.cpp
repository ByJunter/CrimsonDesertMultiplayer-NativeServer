#include "mod/session2/guest_seq_restore.h"

#include <windows.h>

#include <safetyhook.hpp>

#include <algorithm>
#include <atomic>
#include <format>
#include <span>
#include <string>
#include <vector>

#include "mod/game/actor_lookup.h"
#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/guest_seq_restore_logic.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"

namespace cdmp::session2 {
namespace {

namespace p = p8p;
namespace off = game::build_2976;
using sar::Verdict;

// ---------------------------------------------------------------- the code (build 2976, all in .rsrc)
//
// Verified with capstone on the read-only PE D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe
// (p8-impl-seq-restore.md §2). RestoreActionsFromLoadData 0x8646D0(CC, int* res):
//   0x8646FC mov rsi,rdx ; mov r13,rcx                      RSI = res, R13 = CC (never written again)
//   0x864724 xor r14d,r14d                                   R14 = 0 until 0x864971
//   0x86473F mov rax,[r13+0x40] ; cmp [rax+0x30],r14b ; jne  restore byte 0 -> mov [rsi],r14d -> exit
//   0x8647ED mov rcx,[r13+0x88] ; mov rax,[rcx+0x110] ; mov [rsp+0x38],rax      layer 0 chart
//   0x864811 mov eax,[rcx+0x3C] ; mov [rsp+0x50],eax         (with a chart) its hash -> entry 0
//   0x864862 mov rcx,[r13+0x90] ; mov rax,[rcx+0x110] ; mov [rsp+0x40],rax      layer 1 chart
//   0x86487A mov eax,[rcx+0x3C] ; mov [rbp-0x38],eax         (with a chart) its hash -> entry 1 = [rsp+0xC8]
//   0x8648CA..0x86490B                                       the lock of 0x864739 released
//   0x86490F mov rbx,[r13+8] ; lea rdx,[rbx+8]               THE POINT (8 bytes; every restore-byte-1 path)
//   0x864954 / 0x864961 -> 0x864AEA, else the timing loop (0x864967, reads the entries' hashes)
//   0x864AE0 mov rsi,[rbp+0xA8] ; xor r14d,r14d              (loop path) res again from its home slot
//   0x864AEA lea r8,[rsp+0x50] ; lea rdx,[rbp+0xA0] ; mov rcx,r13 ; call 0x863880 ; ... mov [rsi],eax
//   0x864B1A mov [rsi],r14d ; mov rax,rsi ; ... ret          THE EXIT the skip continues at
// No branch lands in 0x864910..0x864916 (raw rel32 scan of every executable section and the function's own
// branches, capstone). 0x863880 (RestartLayerAction): per layer with hash != 0, ActionGraph_Find 0x24314C0 in
// [[CC+0x88+8i]+0x110], then in [CC+0x70+8i]; both hashes 0 or an incomplete triple -> eErrNoInvalidActionChart.
constexpr uintptr_t kPoint = 0x86490F;
constexpr uint32_t kPatch = 8;
constexpr uint8_t kPointBytes[] = {0x49, 0x8B, 0x5D, 0x08, 0x48, 0x8D, 0x53, 0x08,
                                   0x48, 0x85, 0xDB, 0x49, 0x0F, 0x44, 0xD6, 0x48};
constexpr uintptr_t kExit = 0x864B1A;
// Review 2: the prologue that fixes the frame (pushes, lea rbp,[rsp-0x60], sub rsp,0x160 -> rbp = rsp + 0x100
// in the body). From 0x8646DB: horse_diag (host only) hooks the head 0x8646D0..0x8646D6, never these bytes.
constexpr uint8_t kPrologueBytes[] = {0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48,
                                      0x8D, 0x6C, 0x24, 0xA0, 0x48, 0x81, 0xEC, 0x60, 0x01, 0x00, 0x00};  // 0x8646DB
// Review 1: only the bytes this module needs. 0x8646FC..0x864701 (mov rsi,rdx ; mov r13,rcx) and
// 0x864B1D..0x864B27 are guest_actor_diag's / companion_probe's mid hooks (cmp::kRestoreHook, kRestoreExitHook):
// there either the original bytes or that hook's jmp (E9) is accepted (its relocated copy runs the same
// instructions), whichever module installs first.
constexpr uint8_t kHeadBytes[] = {0xC5, 0xF8, 0x29, 0x70, 0xB8, 0xC5, 0xF8, 0x29, 0x78, 0xA8};  // 0x8646F2
constexpr uintptr_t kRegsAt = 0x8646FC;
constexpr uint8_t kRegsBytes[] = {0x48, 0x8B, 0xF2, 0x4C, 0x8B, 0xE9};
constexpr uint8_t kR14Bytes[] = {0x45, 0x33, 0xF6};                                // 0x864724
// The entries' constructor loop (0x864702: lea r11,[rsp+0x50] ; ... call 0x1EC98F0 ; add r11,0x78 ; ...) and the
// constructor's start (0x1EC98F0: ... mov [rcx],r9 with r9d = 0): review 3, the hash of a layer without a chart.
constexpr uint8_t kCtorLoopBytes[] = {0x4C, 0x8D, 0x5C, 0x24, 0x50, 0x49, 0x8B, 0xCB, 0xE8, 0xE1, 0x51, 0x66, 0x01,
                                      0x49, 0x83, 0xC3, 0x78, 0x48, 0x8D, 0x45, 0x40, 0x4C, 0x3B, 0xD8, 0x75, 0xEB};
constexpr uint8_t kCtorBytes[] = {0x48, 0x83, 0xEC, 0x28, 0x45, 0x33, 0xC9, 0xC7, 0x41, 0x20, 0xFF, 0xFF,
                                  0xFF, 0xFF, 0x41, 0xB8, 0xFF, 0xFF, 0x00, 0x00, 0x4C, 0x89, 0x09};  // 0x1EC98F0
// The lock of 0x864739 and the byte-0 test (review 6), and its release before the point (0x8648CA..0x86490B).
constexpr uint8_t kLockBytes[] = {0xE8, 0x12, 0xEF, 0xB2, 0xFF, 0x90, 0x49, 0x8B,
                                  0x45, 0x40, 0x44, 0x38, 0x70, 0x30, 0x75, 0x59};  // 0x864739
constexpr uint8_t kReleaseBytes[] = {
    0x48, 0x8B, 0x5C, 0x24, 0x20, 0x48, 0x8B, 0x03, 0x33, 0xD2, 0x48, 0x8B, 0xCB, 0xFF, 0x50, 0x20, 0x80, 0x7C,
    0x24, 0x28, 0x01, 0x75, 0x2E, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x0F, 0xC1, 0x43, 0x08, 0x83, 0xF8, 0x01,
    0x75, 0x12, 0xF7, 0x43, 0x0C, 0x00, 0x00, 0x00, 0x40, 0x74, 0x16, 0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xFF,
    0x50, 0x40, 0xF7, 0x43, 0x0C, 0x00, 0x00, 0x00, 0x40, 0x74, 0x04, 0xF0, 0xFF, 0x4B, 0x0C};  // 0x8648CA
// Review 7: the entries' index word, copied from the layer's current action: entry 0 [rbp-0x76], entry 1 [rbp+2].
constexpr uint8_t kIndex0Bytes[] = {0x0F, 0xB7, 0x41, 0x38, 0x66, 0x89, 0x45, 0x8A};  // 0x864834
constexpr uint8_t kIndex1Bytes[] = {0x0F, 0xB7, 0x41, 0x38, 0x66, 0x89, 0x45, 0x02};  // 0x86489C
constexpr uint8_t kChart0Bytes[] = {0x49, 0x8B, 0x8D, 0x88, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x81,
                                    0x10, 0x01, 0x00, 0x00, 0x48, 0x89, 0x44, 0x24, 0x38};  // 0x8647ED
constexpr uint8_t kHash0Bytes[] = {0x8B, 0x41, 0x3C, 0x89, 0x44, 0x24, 0x50};      // 0x864811
constexpr uint8_t kChart1Bytes[] = {0x49, 0x8B, 0x8D, 0x90, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x81,
                                    0x10, 0x01, 0x00, 0x00, 0x48, 0x89, 0x44, 0x24, 0x40};  // 0x864862
constexpr uint8_t kHash1Bytes[] = {0x8B, 0x41, 0x3C, 0x89, 0x45, 0xC8};            // 0x86487A
constexpr uint8_t kReloadBytes[] = {0x48, 0x8B, 0xB5, 0xA8, 0x00, 0x00, 0x00, 0x45, 0x33, 0xF6};  // 0x864AE0
constexpr uint8_t kCallBytes[] = {0x4C, 0x8D, 0x44, 0x24, 0x50, 0x48, 0x8D, 0x95, 0xA0, 0x00,
                                  0x00, 0x00, 0x49, 0x8B, 0xCD, 0xE8, 0x82, 0xED, 0xFF, 0xFF};  // 0x864AEA
constexpr uint8_t kExitBytes[] = {0x44, 0x89, 0x36};  // 0x864B1A mov [rsi],r14d (what the skip runs)
constexpr uintptr_t kExitTailAt = 0x864B1D;         // guest_actor_diag's exit hook point: original or E9
constexpr uint8_t kExitTailBytes[] = {0x48, 0x8B, 0xC6, 0x4C, 0x8D, 0x9C, 0x24, 0x60, 0x01, 0x00, 0x00};
// 0x863880's lookups (0x8638EA..0x863929; 0x86392A is P8Probe's point, not checked here) and triple checks.
constexpr uint8_t kRestartLookBytes[] = {
    0x49, 0x8D, 0x7F, 0x70, 0x48, 0x8D, 0x5C, 0x24, 0x28, 0x8B, 0x73, 0xF8, 0x85, 0xF6, 0x74, 0x30,
    0x48, 0x8B, 0x47, 0x18, 0x48, 0x8B, 0x88, 0x10, 0x01, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x0E,
    0x4C, 0x8B, 0xC3, 0x8B, 0xD6, 0xE8, 0xAC, 0xDB, 0xBC, 0x01, 0x84, 0xC0, 0x75, 0x12, 0x48, 0x8B,
    0x0F, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0x4C, 0x8B, 0xC3, 0x8B, 0xD6, 0xE8, 0x96, 0xDB, 0xBC, 0x01};
constexpr uint8_t kRestartCheckBytes[] = {
    0x48, 0x8B, 0x8C, 0x24, 0x98, 0x00, 0x00, 0x00, 0x8B, 0x44, 0x24, 0x20, 0x85, 0xC0, 0x75, 0x04,
    0x85, 0xC9, 0x74, 0x49, 0xB2, 0x01, 0x85, 0xC0, 0x74, 0x1A, 0x48, 0x83, 0x7C, 0x24, 0x28, 0x00,
    0x74, 0x10, 0x48, 0x83, 0x7C, 0x24, 0x30, 0x00, 0x74, 0x08, 0x48, 0x83, 0x7C, 0x24, 0x38, 0x00,
    0x75, 0x02, 0x32, 0xD2, 0x85, 0xC9, 0x74, 0x21};  // 0x863938
// ActionGraph_Find 0x24314C0 after its first 15 bytes (P8Probe may hook its head): the search sar::chart_find
// replicates (count +0x20, array +0x18, 24-byte entries, key [p2+0x18], unsigned compares).
constexpr uint8_t kFindBodyBytes[] = {
    0x44, 0x8B, 0x51, 0x20, 0x49, 0x8B, 0xF0, 0x44, 0x8B, 0xDA, 0x45, 0x85, 0xD2, 0x74, 0x7C, 0x45, 0x33, 0xC9,
    0x41, 0x83, 0xEA, 0x01, 0x78, 0x73, 0x48, 0x8B, 0x79, 0x18, 0x0F, 0x1F, 0x44, 0x00, 0x00, 0x41, 0x8B, 0xC2,
    0x41, 0x2B, 0xC1, 0x79, 0x02, 0xFF, 0xC0, 0xD1, 0xF8, 0x42, 0x8D, 0x14, 0x08, 0x48, 0x63, 0xC2, 0x48, 0x8D,
    0x0C, 0x40, 0x4C, 0x8B, 0x04, 0xCF, 0x4D, 0x85, 0xC0, 0x74, 0x3A, 0x48, 0x83, 0x7C, 0xCF, 0x08, 0x00, 0x74,
    0x16, 0x48, 0x8B, 0x44, 0xCF, 0x10, 0x48, 0x85, 0xC0, 0x74, 0x0C, 0x44, 0x3B, 0x58, 0x18, 0x73, 0x06, 0x44,
    0x8D, 0x52, 0xFF, 0xEB, 0x27, 0x4D, 0x85, 0xC0, 0x74, 0x17, 0x48, 0x83, 0x7C, 0xCF, 0x08, 0x00, 0x74, 0x0F,
    0x48, 0x8B, 0x44, 0xCF, 0x10, 0x48, 0x85, 0xC0, 0x74, 0x05, 0x8B, 0x40, 0x18, 0xEB, 0x02, 0x33, 0xC0, 0x41,
    0x3B, 0xC3, 0x73, 0x1B, 0x44, 0x8D, 0x4A, 0x01, 0x45, 0x3B, 0xD1, 0x7D, 0x96};  // 0x24314CF

// The stack frame at the point: the same addressing the native code uses for each slot (review 2): RSP-relative
// for the charts and entry 0's hash (0x8647FB, 0x864870, 0x864814), RBP-relative for entry 1's hash (0x86487D)
// and both index words (0x864838, 0x8648A0); the hook also requires rbp == rsp + 0x100 (the verified prologue).
constexpr uint32_t kSp_Chart0 = 0x38, kSp_Chart1 = 0x40, kSp_Entry0 = 0x50;
constexpr int32_t kBp_Entry1 = -0x38, kBp_Index0 = -0x76, kBp_Index1 = 0x02;
constexpr uint32_t kBp_ResHome = 0xA8;
constexpr uint64_t kFrameDelta = 0x100;
constexpr uint32_t kCc_Actor = 0x08, kCc_Fallback0 = 0x70, kCc_Fallback1 = 0x78;
constexpr uint32_t kActor_State = 0x5E, kActor_C2 = 0xC2, kActor_CF = 0xCF;

uintptr_t g_base = 0;
std::atomic<bool> g_started{false}, g_code_ok{false};
std::atomic<uint8_t> g_mode{0};
SafetyHookMid g_hook;

// Counters (any game thread; relaxed).
std::atomic<uint64_t> g_calls{0}, g_seq_calls{0}, g_verdicts[static_cast<size_t>(Verdict::Count)] = {};
std::atomic<uint64_t> g_layers_sub{0}, g_layers_drop{0}, g_would_sub{0}, g_would_drop{0}, g_write_faults{0},
    g_changed_late{0};
sar::KeyLog<64> g_keys;  // the keys a Substituted/Skipped decision named (applied or, with probe, not)

enum Kind : uint16_t { kEDecision, kKindCount };
p::EventRing<256> g_ring;
p::WindowLimiter g_lim{32, 5'000'000};
std::atomic<uint64_t> g_limited{0}, g_lost{0}, g_lines{0};
constexpr uint64_t kMaxLines = 400;

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
T rdv(uintptr_t a) noexcept {
    T v{};
    return rd(a, v) ? v : T{};
}
template <class T>
bool wr(uintptr_t a, T v) noexcept {
    if (a < 0x10000) return false;
    __try {
        *reinterpret_cast<volatile T*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct GameReader {
    bool r32(uint64_t a, uint32_t& v) const noexcept { return rd(static_cast<uintptr_t>(a), v); }
    bool r64(uint64_t a, uint64_t& v) const noexcept { return rd(static_cast<uintptr_t>(a), v); }
};

// This client's user key: [[[WorldSystem]+0x30]+0x50]+0x90 (the controlled actor's owner; guest_actor_diag's
// read). 0 when not known or not a user key.
uint32_t local_user() noexcept {
    const uintptr_t world = rdv<uintptr_t>(g_base + off::kWorldSystemSlot);
    const uintptr_t mgr = world ? rdv<uintptr_t>(world + off::kWorldSystem_ActorManager) : 0;
    const uintptr_t own = mgr ? rdv<uintptr_t>(mgr + off::kActorManager_ControlledActor) : 0;
    const uint32_t key = own ? rdv<uint32_t>(own + off::kActor_OwnerUser) : 0;
    return sar::user_key(key) ? key : 0;
}

uint64_t pack_layer(const sar::LayerPlan& l) {
    return static_cast<uint64_t>(l.in_chart) | static_cast<uint64_t>(l.in_fallback) << 4 |
           static_cast<uint64_t>(l.wait) << 8 | static_cast<uint64_t>(l.act) << 12;
}

void on_point(safetyhook::Context& ctx) noexcept {
    if (!g_code_ok.load(std::memory_order_acquire)) return;
    g_calls.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t sp = ctx.rsp, bp = ctx.rbp;
    sar::Input in;
    in.frame_ok = bp == sp + kFrameDelta;
    // Review 3: both hashes read always, like 0x863880 (a layer without a chart keeps the constructor's 0).
    in.hash[0] = rdv<uint32_t>(sp + kSp_Entry0);
    in.hash[1] = rdv<uint32_t>(bp + kBp_Entry1);
    // The fast path: every restore without a seq_bodyanim hash (two compares).
    if (!sar::seq_family(in.hash[0]) && !sar::seq_family(in.hash[1])) return;
    g_seq_calls.fetch_add(1, std::memory_order_relaxed);
    in.chart[0] = rdv<uintptr_t>(sp + kSp_Chart0);
    in.chart[1] = rdv<uintptr_t>(sp + kSp_Chart1);

    const auto mode = static_cast<sar::Mode>(g_mode.load(std::memory_order_relaxed));
    in.mode = mode;
    const uintptr_t cc = ctx.r13;
    const uintptr_t actor = rdv<uintptr_t>(cc + kCc_Actor);
    in.key = actor ? rdv<uint32_t>(actor + off::kActor_Key) : 0;
    in.control = actor ? rdv<uint32_t>(actor + off::kActor_OwnerUser) : 0;
    in.local = local_user();
    in.fallback[0] = rdv<uintptr_t>(cc + kCc_Fallback0);
    in.fallback[1] = rdv<uintptr_t>(cc + kCc_Fallback1);
    uintptr_t res_home = 0;
    in.exit_regs_ok = static_cast<uint32_t>(ctx.r14) == 0 && rd(bp + kBp_ResHome, res_home) &&
                      res_home == ctx.rsi && ctx.rsi != 0;

    sar::Plan pl = sar::plan(GameReader{}, in);
    bool applied = false, late = false;
    if (sar::changes(pl.verdict)) {
        bool ok = true;
        if (mode == sar::Mode::On) {
            // Review 4: the dynamic graph is still the one the plan saw (same thread as BodyAnim in session 8;
            // this only narrows a window nobody observed). Else nothing is written and nothing skipped.
            const uint64_t now_fb[2] = {rdv<uintptr_t>(cc + kCc_Fallback0), rdv<uintptr_t>(cc + kCc_Fallback1)};
            late = !sar::still_valid(pl, in, now_fb);
            if (late) {
                g_changed_late.fetch_add(1, std::memory_order_relaxed);
                pl.skip = false;
                for (int i = 0; i < 2; ++i) pl.write[i] = pl.write_index[i] = false;
                ok = false;
            }
            // Index word first, then the hash (a hash is never left pointing at the old index alone).
            for (int i = 0; ok && i < 2; ++i) {
                if (pl.write_index[i] && !wr<uint16_t>(bp + (i ? kBp_Index1 : kBp_Index0), sar::kNoIndex)) ok = false;
                if (ok && pl.write[i] && !wr<uint32_t>(i ? bp + kBp_Entry1 : sp + kSp_Entry0, pl.layer[i].new_hash)) {
                    ok = false;
                }
            }
            if (!ok && !late) g_write_faults.fetch_add(1, std::memory_order_relaxed);
            if (ok && pl.skip) ctx.rip = g_base + kExit;  // [rsi] = r14d (0): the native restore-byte-0 result
            applied = ok;
        }
        // Review 8: layers only for Substituted; a skip is counted by its verdict.
        const sar::LayerCounts lc = sar::layer_counts(pl);
        if (mode != sar::Mode::On || applied) {
            (applied ? g_layers_sub : g_would_sub).fetch_add(lc.substituted, std::memory_order_relaxed);
            (applied ? g_layers_drop : g_would_drop).fetch_add(lc.dropped, std::memory_order_relaxed);
        }
        g_keys.add(in.key);
    }
    g_verdicts[static_cast<size_t>(pl.verdict)].fetch_add(1, std::memory_order_relaxed);

    p::Event e;
    e.kind = kEDecision;
    e.qpc_us = p8_now_us();
    e.tid = GetCurrentThreadId();
    if (!g_lim.take(e.qpc_us)) {
        g_limited.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    e.v[0] = in.key | static_cast<uint64_t>(in.control) << 32;
    e.v[1] = in.hash[0] | static_cast<uint64_t>(in.hash[1]) << 32;
    e.v[2] = in.chart[0];
    e.v[3] = in.chart[1];
    e.v[4] = in.fallback[0];
    e.v[5] = in.fallback[1];
    e.v[6] = pack_layer(pl.layer[0]) | pack_layer(pl.layer[1]) << 16 | static_cast<uint64_t>(pl.verdict) << 32 |
             static_cast<uint64_t>(mode) << 40 | static_cast<uint64_t>(applied) << 48 |
             static_cast<uint64_t>(pl.skip) << 49 | static_cast<uint64_t>(in.exit_regs_ok) << 50 |
             static_cast<uint64_t>(late) << 51 | static_cast<uint64_t>(in.frame_ok) << 52;
    e.v[7] = in.local;
    e.v[8] = rdv<uint32_t>(in.chart[0] + sar::kChart_Count) |
             static_cast<uint64_t>(in.chart[1] ? rdv<uint32_t>(in.chart[1] + sar::kChart_Count) : 0) << 32;
    if (!g_ring.push(e)) g_lost.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------- lines (diagnostics thread)

const char* hash_name(uint32_t h) {
    switch (h) {
        case sar::kHashSeqBodyAnim: return " (seq_bodyanim_action)";
        case sar::kHashSeqBodyAnimLoop: return " (seq_bodyanim_action_loop)";
        case sar::kHashWait: return " (wait)";
        default: return "";
    }
}

std::string layer_text(int i, uint32_t hash, uint64_t chart, uint32_t count, uint64_t fallback, uint64_t packed) {
    const auto in_chart = static_cast<sar::Lookup>(packed & 0xF), in_fb = static_cast<sar::Lookup>((packed >> 4) & 0xF),
               wait = static_cast<sar::Lookup>((packed >> 8) & 0xF);
    const auto act = static_cast<sar::Act>((packed >> 12) & 0xF);
    if (!chart && !hash) return std::format("L{} no chart, hash 0", i);
    return std::format("L{} hash 0x{:08X}{} chart 0x{:X} (count {}): {}, fallback [CC+0x{:X}] 0x{:X}: {}{} -> {}", i,
                       hash, hash_name(hash), chart, count, sar::lookup_name(in_chart), i ? 0x78 : 0x70, fallback,
                       sar::lookup_name(in_fb),
                       act == sar::Act::Keep ? std::string() : std::format(", wait in chart: {}", sar::lookup_name(wait)),
                       sar::act_name(act));
}

std::string line_of(const p::Event& e) {
    const uint64_t* v = e.v;
    const auto verdict = static_cast<Verdict>((v[6] >> 32) & 0xFF);
    const auto mode = static_cast<sar::Mode>((v[6] >> 40) & 0xFF);
    const bool applied = (v[6] >> 48) & 1, skip = (v[6] >> 49) & 1, regs = (v[6] >> 50) & 1, late = (v[6] >> 51) & 1,
               frame = (v[6] >> 52) & 1;
    std::string what;
    if (sar::changes(verdict)) {
        what = mode != sar::Mode::On ? " | probe: nothing written"
               : applied             ? (skip ? " | APPLIED: continues at 0x864B1A, result 0"
                                             : " | APPLIED to the entries (hash, index word 0xFFFF)")
               : late                ? " | NOT applied: [CC+0x70/+0x78] changed before the write"
                                     : " | NOT applied (a stack write failed)";
    }
    return std::format("GuestSeqRestore [restore 0x86490F] {} T{} | actor 0x{:08X} control 0x{:08X} (local user 0x{:08X}) "
                       "| {} | {} | {}{} | exit registers {}, frame {}",
                       p8_when(e.qpc_us), e.tid, static_cast<uint32_t>(v[0]), static_cast<uint32_t>(v[0] >> 32),
                       static_cast<uint32_t>(v[7]),
                       layer_text(0, static_cast<uint32_t>(v[1]), v[2], static_cast<uint32_t>(v[8]), v[4], v[6] & 0xFFFF),
                       layer_text(1, static_cast<uint32_t>(v[1] >> 32), v[3], static_cast<uint32_t>(v[8] >> 32), v[5],
                                  (v[6] >> 16) & 0xFFFF),
                       sar::verdict_name(verdict), what, regs ? "ok" : "NOT as expected",
                       frame ? "ok" : "NOT rbp = rsp + 0x100");
}

uint64_t g_tick_n = 0;  // diagnostics thread only

std::string key_state(uint32_t key) {
    const auto actor = game::find_actor(key);
    if (!actor || !*actor) return std::format("0x{:08X} NOT on this client", key);
    uint16_t state = 0;
    uint8_t c2 = 0, cf = 0;
    const bool ok = game::safe_read(*actor + kActor_State, &state, sizeof state) &&
                    game::safe_read(*actor + kActor_C2, &c2, sizeof c2) &&
                    game::safe_read(*actor + kActor_CF, &cf, sizeof cf);
    if (!ok) return std::format("0x{:08X} exists (unreadable)", key);
    return std::format("0x{:08X} exists +5E 0x{:04X} C2 {} CF {}", key, state, c2, cf);
}

void summary() {
    std::string verdicts;
    for (size_t k = 0; k < static_cast<size_t>(Verdict::Count); ++k) {
        if (k == static_cast<size_t>(Verdict::NoSeqHash)) continue;
        if (const uint64_t n = g_verdicts[k].load()) verdicts += std::format(" {} x{};", sar::verdict_name(static_cast<Verdict>(k)), n);
    }
    std::string keys;
    const size_t n = g_keys.size();
    for (size_t i = 0; i < n; ++i) {
        const uint32_t key = g_keys.at(i);
        if (key) keys += (keys.empty() ? "" : ", ") + key_state(key);
    }
    const auto mode = static_cast<sar::Mode>(g_mode.load());
    log::info("GuestSeqRestore summary (seq_actor_restore={}): restores seen {} (with a seq_bodyanim hash {}) | {} | "
              "layers of substituted entries {} {} -> wait, {} -> 0 | dynamic graph changed before the write {} | stack "
              "write faults {} | actors touched {}{}: {} | lines limited {} lost {}",
              mode == sar::Mode::On ? "1" : "probe", g_calls.load(), g_seq_calls.load(),
              verdicts.empty() ? "no decision yet" : verdicts.substr(1), mode == sar::Mode::On ? "changed" : "would change",
              mode == sar::Mode::On ? g_layers_sub.load() : g_would_sub.load(),
              mode == sar::Mode::On ? g_layers_drop.load() : g_would_drop.load(), g_changed_late.load(),
              g_write_faults.load(), n,
              g_keys.full() ? std::format(" (+{} not remembered)", g_keys.full()) : std::string(),
              keys.empty() ? "none" : keys, g_limited.load(), g_lost.load());
}

// ---------------------------------------------------------------- install

struct Check {
    uintptr_t rva;
    std::span<const uint8_t> bytes;
};

bool bytes_at(uintptr_t rva, std::span<const uint8_t> expected) {
    std::vector<uint8_t> actual(expected.size());
    return game::safe_read(g_base + rva, actual.data(), actual.size()) &&
           std::equal(actual.begin(), actual.end(), expected.begin());
}

DWORD WINAPI install_thread(LPVOID) {
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::vector<gad::Section> secs;
    if (g_base != off::kImageBase || !game::safe_read(g_base, &dos, sizeof dos) ||
        !game::safe_read(g_base + dos.e_lfanew, &nt, sizeof nt) || nt.FileHeader.TimeDateStamp != off::kPeTimestamp ||
        !p8_exe_sections(g_base, secs)) {
        log::error("GuestSeqRestore: unsupported game build or unreadable sections; nothing installed");
        return 0;
    }
    for (int i = 0; i < 1200; ++i) {  // WorldSystem (up to 10 minutes), as the other guest modules
        uintptr_t world = 0;
        if (game::safe_read(g_base + off::kWorldSystemSlot, &world, sizeof world) && world) break;
        Sleep(500);
    }
    const Check checks[] = {
        {0x8646DB, kPrologueBytes}, {0x8646F2, kHeadBytes},     {0x864702, kCtorLoopBytes}, {0x864724, kR14Bytes},
        {0x864739, kLockBytes},     {0x8647ED, kChart0Bytes},   {0x864811, kHash0Bytes},    {0x864834, kIndex0Bytes},
        {0x864862, kChart1Bytes},   {0x86487A, kHash1Bytes},    {0x86489C, kIndex1Bytes},   {0x8648CA, kReleaseBytes},
        {0x864AE0, kReloadBytes},   {0x864AEA, kCallBytes},     {kExit, kExitBytes},        {0x8638EA, kRestartLookBytes},
        {0x863938, kRestartCheckBytes}, {0x24314CF, kFindBodyBytes}, {0x1EC98F0, kCtorBytes},
    };
    for (const Check& c : checks) {
        if (!gad::in_real_code(secs, static_cast<uint32_t>(c.rva), static_cast<uint32_t>(c.bytes.size())) ||
            !bytes_at(c.rva, c.bytes)) {
            log::error("GuestSeqRestore: the code at exe+0x{:X} ({} bytes) is not build 2976's .rsrc code (or another "
                       "module hooked it); REFUSED, nothing installed",
                       c.rva, c.bytes.size());
            return 0;
        }
    }
    // Review 1: guest_actor_diag's (or companion_probe's) mid hooks at 0x8646FC / 0x864B1D: original bytes or
    // their jmp to a trampoline outside the image; either way the same instructions run (relocated).
    const Check shared[] = {{kRegsAt, kRegsBytes}, {kExitTailAt, kExitTailBytes}};
    std::string shared_text;
    for (const Check& c : shared) {
        uint8_t got[16] = {};
        const bool read = game::safe_read(g_base + c.rva, got, c.bytes.size());
        const sar::Shared s = read ? sar::classify_shared(std::span<const uint8_t>(got, c.bytes.size()), c.bytes,
                                                          g_base + c.rva, g_base, g_base + nt.OptionalHeader.SizeOfImage)
                                   : sar::Shared::Mismatch;
        if (s == sar::Shared::Mismatch) {
            log::error("GuestSeqRestore: exe+0x{:X} is neither build 2976's code nor another module's mid hook; REFUSED, "
                       "nothing installed",
                       c.rva);
            return 0;
        }
        shared_text += std::format(" exe+0x{:X} {};", c.rva, s == sar::Shared::Original ? "original" : "another module's hook");
    }
    if (!p8_install_mid(g_base, secs, kPoint, kPatch, kPointBytes, on_point, g_hook, "GuestSeqRestore",
                        "RestoreActionsFromLoadData's point 0x86490F")) {
        return 0;  // the line above says why
    }
    g_code_ok.store(true, std::memory_order_release);
    log::info("GuestSeqRestore {} (seq_actor_restore): hooked 0x86490F inside RestoreActionsFromLoadData 0x8646D0 (restore "
              "byte != 0, entries on its stack, before 0x863880) | for a remote actor (key 0xA/0xB, +0x90 not this "
              "client's user) whose seq_bodyanim hash (0x{:08X} / 0x{:08X}) neither its layer chart nor [CC+0x70/+0x78] "
              "has: that entry {} the wait 0x{:08X} when the layer chart has it, else 0; both 0 -> continue at 0x{:X} "
              "with result 0 (native restore byte 0). Any other failure is left to the game | lines 'GuestSeqRestore "
              "[restore 0x86490F]', summary every 60 s | shared sites:{}",
              sar::mode_name(static_cast<sar::Mode>(g_mode.load())), sar::kHashSeqBodyAnim, sar::kHashSeqBodyAnimLoop,
              g_mode.load() == static_cast<uint8_t>(sar::Mode::On) ? "gets" : "WOULD get (probe)", sar::kHashWait, kExit,
              shared_text);
    return 0;
}

}  // namespace

void guest_seq_restore_start(const std::string& mode) {
    bool bad = false;
    const sar::Mode m = sar::parse_mode(mode, &bad);
    if (bad) log::warn("GuestSeqRestore: seq_actor_restore={} is not 0, probe or 1: off", mode);
    g_mode.store(static_cast<uint8_t>(m));
    if (m == sar::Mode::Off) return;
    if (g_started.exchange(true)) return;
    CloseHandle(CreateThread(nullptr, 0, install_thread, nullptr, 0, nullptr));
}

bool guest_seq_restore_wanted() { return g_started.load(); }

void guest_seq_restore_tick() noexcept {
    if (!g_started.load()) return;
    try {
        ++g_tick_n;
        p::Event e;
        while (g_ring.pop(e)) {
            if (e.kind >= kKindCount) continue;
            if (g_lines.fetch_add(1) < kMaxLines) log::info("{}", line_of(e));
        }
        if (g_tick_n % 12 == 0) summary();
    } catch (...) {
    }
}

std::string guest_seq_restore_describe() {
    if (!g_started.load()) return {};
    const bool on = g_mode.load() == static_cast<uint8_t>(sar::Mode::On);
    return std::format(" | seq restore {}{} (seq hashes {}, substituted {}, skipped {})", on ? "on" : "probe",
                       g_code_ok.load() ? "" : " NOT hooked", g_seq_calls.load(),
                       g_verdicts[static_cast<size_t>(Verdict::Substituted)].load(),
                       g_verdicts[static_cast<size_t>(Verdict::Skipped)].load());
}

}  // namespace cdmp::session2
