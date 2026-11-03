#include "mod/session2/user2_stages.h"

#include <emmintrin.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <vector>

#include <safetyhook.hpp>

#include "mod/game/game_fault.h"
#include "mod/game/hook_prepare.h"
#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/docked_gimmicks.h"
#include "mod/session2/fast_rearm_logic.h"
#include "mod/session2/child_hold.h"
#include "mod/session2/freeze_watchdog.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/guest_s3_logic.h"
#include "mod/session2/host_scene_diag.h"
#include "mod/session2/mount_sync.h"
#include "mod/session2/npc_action_probe.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"
#include "mod/session2/server.h"
#include "mod/session2/shop_probe.h"
#include "mod/session2/shop_trade_probe.h"
#include "mod/session2/user2_funnels.h"
#include "mod/session2/user2_shops.h"
#include "mod/session2/user2_shops_logic.h"
#include "mod/session2/user2_road_logic.h"
#include "mod/session2/user2_stages_logic.h"
#include "mod/session2/npc_activation_logic.h"
#include "mod/session2/npc_family_logic.h"
#include "mod/session2/npc_family_vehicles_logic.h"
#include "mod/session2/world_anchor.h"

namespace cdmp::session2 {
namespace {

namespace p = p8p;
namespace off = game::build_2976;
using u2s::Who;

// ---------------------------------------------------------------- the points (build 2976, .rsrc)
//
// Capstone on the read-only PE (mundo-escenarios-propios.md §5.3, §8; u0-resultados-s28.md "U-1"): whole
// instructions; a raw scan of every executable section found no rel8/rel32 branch landing strictly inside a patch.
struct PointDef {
    uintptr_t rva;
    uint32_t patch;
    std::span<const uint8_t> bytes;
    const char* name;
};
constexpr uint8_t kCBytes[] = {0x4C, 0x89, 0x4C, 0x24, 0x20, 0x44, 0x88, 0x44, 0x24, 0x18};  // mov [rsp+20],r9 ; mov [rsp+18],r8b
constexpr uint8_t kDBytes[] = {0xE9, 0x3B, 0xD2, 0xA1, 0x0D};  // jmp .xtls 0x10225FC0
constexpr uint8_t kLBytes[] = {0xE9, 0xDB, 0x4F, 0xA4, 0x0D};  // jmp .xtls 0x102C2A30
constexpr uint8_t kUBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x88, 0x54, 0x24, 0x10};  // mov [rsp+18],rbx ; mov [rsp+10],dl
constexpr uint8_t kNBytes[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x4C, 0x89, 0x48, 0x20};  // mov rax,rsp ; mov [rax+10],rbx
constexpr uint8_t kTBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x44, 0x88, 0x4C, 0x24, 0x20};  // mov [rsp+18],rbx ; mov [rsp+20],r9b
constexpr uint8_t kSBytes[] = {0xE9, 0xBB, 0xB7, 0xA3, 0x0D};  // jmp .xtls 0x1025E4C0
constexpr uint8_t kFBytes[] = {0xE9, 0x1B, 0x17, 0xA0, 0x0D};  // jmp .xtls 0x101FD480
constexpr uint8_t kPBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x4C, 0x89, 0x4C, 0x24, 0x20};  // mov [rsp+10],rbx ; mov [rsp+20],r9
// U-0b (S29): ServerActor::Logout's thunk (actor rcx, out rdx, reason r8b, error r9d; [rsp] the caller).
constexpr uint8_t kOBytes[] = {0xE9, 0xAB, 0x17, 0x07, 0x0C};  // jmp .xtls 0xE0D9AF0

enum Pt : uint8_t { kPtC, kPtD, kPtL, kPtU, kPtN, kPtT, kPtS, kPtF, kPtP, kPtO, kPtCount };
const PointDef kPoints[kPtCount] = {
    {0x2817E80, 5, kCBytes, "C, the stage close (ServerNPCScheduleStage vf13) 0x2817E80"},
    {0x2808D80, 5, kDBytes, "D, DeleteStage of the schedule spawn manager 0x2808D80"},
    {0x287DA50, 5, kLBytes, "L, the owner's load step 0x287DA50"},
    {0x287D4C0, 5, kUBytes, "U, the manager's unload-all 0x287D4C0"},
    {0x28078E0, 7, kNBytes, "N, the schedule stage creation 0x28078E0"},
    {0x287DBB0, 5, kTBytes, "T, the 2043 tick 0x287DBB0"},
    {0x2822D00, 5, kSBytes, "S, the schedule update 0x2822D00"},
    {0x27FBD60, 5, kFBytes, "F, the faction spawn update 0x27FBD60"},
    {0x287EF60, 5, kPBytes, "P, the manager's preparation 0x287EF60"},
    {0x2068340, 5, kOBytes, "O, ServerActor::Logout 0x2068340 (reasons and callers)"},
};

// U-a: the owner gate (ShopProbe's point; this module installs it only when shop_probe is off).
//   0x288B867 mov rax,[r13+0xA0] ; test ; je ; mov eax,[rax+0x60] ; jmp ; mov eax,esi
//   0x288B87A cmp [r15+0x180],eax (41 39 87 80 01 00 00) ; 0x288B881 jne 0x288BDEE (not patched; 0x288B876 jumps to
//   the patch's first byte)
constexpr uintptr_t kGate = 0x288B87A, kGateCtx = 0x288B867;
constexpr uint32_t kGatePatch = 7;
constexpr uint8_t kGateBytes[] = {0x41, 0x39, 0x87, 0x80, 0x01, 0x00, 0x00, 0x0F, 0x85, 0x67, 0x05, 0x00, 0x00};
constexpr uint8_t kGateCtxBytes[] = {0x49, 0x8B, 0x85, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0,
                                     0x74, 0x05, 0x8B, 0x40, 0x60, 0xEB, 0x02, 0x8B, 0xC6};
// U-a2 (rule of 2026-10-13: the guest follows the HOST's progress). The cell evaluation's context keeps the field
// owner's ServerQuestManager (ServerField::vf40 at 0x2880D6F, not substituted): the candidate collector 0x2911300, the
// acceptance cases of 0x28818F0 (+0x178/+0x188 and the stage states +0x48..+0x60) and 0x2896C60's id list all read
// the HOST's progress. Its character ([QM+0x10]) is the host's, so the collector's owner check passes natively and
// FindUser 0x2A82590 (0x29113F6, r8 = the owner's key) resolves the HOST, natively; then:
//   0x2911402 mov rbx,rdi (0 when FindUser found nobody) / 0x291141D mov rbx,[[[[user+0xD0]+0x68]+0xA8]+0x18]
//   0x2911435 mov rdx,rbx ; lea rcx,[rbp-0x70]   (patched, 3 + 4; no RIP-relative, no branch lands in 0x2911436..B)
//   0x291143C call 0x387EF0(&guard, mgr)          the evaluating manager's guard (natively owner == evaluating)
//   0x2911442 mov ebx,1                           rbx is not read again
// Inside user 2's armed evaluation (0x2911300 called from 0x28818F0, return 0x28819D4, for user 2's manager - the
// caller's rsi), rbx := user 2's manager: the guard is its own manager's. r8 / FindUser are not touched (the review of
// c827729: FindUser on user 2's key fails in its logout window -> rbx 0 -> a null read in 0x387EF0). Frame (prologue:
// 8 pushes, sub rsp 0x1C2C8): the caller's rsi at [rsp+0x1C2F0], the return address at [rsp+0x1C308], the field
// (rdx's home slot) at [rsp+0x1C318].
constexpr uintptr_t kGate2 = 0x2911435, kGate2Ctx = 0x29113F6;
constexpr uint32_t kGate2Patch = 7;
constexpr uint8_t kGate2Bytes[] = {0x48, 0x8B, 0xD3, 0x48, 0x8D, 0x4D, 0x90, 0xE8, 0xAF, 0x6A, 0xA7, 0xFD,
                                   0x90, 0xBB, 0x01, 0x00, 0x00, 0x00};
constexpr uint8_t kGate2CtxBytes[] = {0xE8, 0x95, 0x11, 0x17, 0x00, 0x90, 0x80, 0x7D, 0xB0, 0x00, 0x75, 0x05, 0x48, 0x8B,
                                      0xDF, 0xEB, 0x1A, 0x48, 0x8B, 0x45, 0xA8, 0x48, 0x8B, 0x88, 0xD0, 0x00, 0x00, 0x00,
                                      0x48, 0x8B, 0x41, 0x68, 0x48, 0x8B, 0x88, 0xA8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x59,
                                      0x18};
constexpr uint32_t kG2_CallerRsi = 0x1C2F0, kG2_Ret = 0x1C308, kG2_Field = 0x1C318, kG2_RetRva = 0x28819D4;
// H (log only, the 288f87b run: second gate passed 42, elements still 0): inside the cell evaluation 0x28818F0,
// right after 0x2883AA0(mgr, ctx) and the save manager's call (0x2881A13):
//   0x2881A19 xor edx,edx ; mov [rsp+0x70],rdx   (7 bytes; no branch of the exe lands inside, raw scan)
// rsi = the manager, rbp the frame: ctx at rbp+0x3F0 (ctx[2] = field->vf40() at rbp+0x400), the collector's output at
// rbp+0x430 and its id list at rbp+0x668 / count rbp+0x670 (0x2881E2C..: local_1080 / local_1078). Read: the manager's
// id list [mgr+0x150]/+0x158 (what the element loop walks), its active map count +0x74, the list +0xF0, the context's
// quest manager [+0x10]'s key, [+0x178], [+0x186]/[+0x188] (the two ids accepted outright) and its stage-state map
// count [+0x4C] (the third acceptance case).
constexpr uintptr_t kEval = 0x2881A19, kEvalCtx = 0x2881A04;
constexpr uint32_t kEvalPatch = 7;
constexpr uint8_t kEvalBytes[] = {0x33, 0xD2, 0x48, 0x89, 0x54, 0x24, 0x70};
constexpr uint8_t kEvalCtxBytes[] = {0x48, 0x8D, 0x55, 0x20, 0x48, 0x8B, 0x0D, 0x81, 0x7C, 0x4E, 0x04, 0x48, 0x8B,
                                     0x49, 0x08, 0xE8, 0x68, 0xFC, 0xFE, 0xFE, 0x90};
// U-f: the stage element 0x288A970(mgr rcx, int* res rdx, stage r8, element r9, a5 [rsp+0x28]); one caller,
// 0x288760C in 0x2885BD0. Entry: mov [rsp+8],rbx (5 bytes, the inline hook's jmp) ; mov [rsp+0x10],rsi ...
constexpr uintptr_t kElem = 0x288A970;
constexpr uint32_t kElemPatch = 5;
constexpr uint8_t kElemBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18};
constexpr uint32_t kElem_Res = 0x08, kElem_Schedule = 0x10, kElem_Class2 = 0x18;
// U-f2 (the a5f550a run: 5 schedule stages created by user 2's manager were alive in the host's: the same static stage
// at the same place, e.g. id 0x0F722C at (-11412, 775, -6800), 45 cells from user 2 and 73 from the host, which the
// host's manager had since before user 2 logged in). The schedule branch of 0x288A970 builds the new stage's playData
// from the element: id u64 [stage+8] (playData +0x10), position [elem+0x20C/+0x210/+0x214] (playData +0x38: the stage's
// transform +0x90 is copied from playData +0x1C, so the position lands at stage +0xAC). A stage is "the same" when the
// other player's schedule spawn manager has one at that position (within 0.5 m) - the data id alone is not unique
// (user 2's manager made 0x268D4087 six times at six places).
constexpr uint32_t kElem_Pos = 0x20C, kStage_Id = 0x08, kPlay_Pos = 0x38;
// What 0x288A970 stores in *res when the schedule creation gave no stage (0x288AD21: mov eax,[exe+0x6CF7DFC] ; mov [r14],eax).
constexpr uintptr_t kNoStageCode = 0x6CF7DFC;
// U-c / U-d: the functions called (first bytes compared before the module's hooks are written).
constexpr uintptr_t kArmFn = 0x287F450;      // thunk -> .xtls 0x102CA7F0(mgr): +0x6A0 = 2, the cell tables cleared
constexpr uint8_t kArmFnBytes[] = {0xE9, 0x9B, 0xB3, 0xA4, 0x0D};
constexpr uintptr_t kLoadFn = 0x287DA50;     // thunk -> .xtls 0x102C2A30(mgr, u8, u8): +0x6A0 = 1 + an immediate 2043
constexpr uintptr_t kUnloadFn = 0x287D4C0;   // (mgr, u8, u8): every stage of the manager closed (reason 19)
constexpr uintptr_t kEnqueueFn = 0x2774430;  // thunk -> .xtls 0x100B4050: ServerNet::EnqueueTimer
constexpr uint8_t kEnqueueFnBytes[] = {0xE9, 0x1B, 0xFC, 0x93, 0x0D};
constexpr uintptr_t kServerNetSlot = 0x6D69680;   // [exe+...] = g_ServerNet (0x287F188)
constexpr uintptr_t kPeriodSlot = 0x6D06098;      // i32: the 2043 period / 10 ms (0x287F139)
constexpr uintptr_t kTroc2043Slot = 0x6FA3ED8;    // the periodic 2043's Troc (0x287F171; set by the host's 0x287EF60)
constexpr uintptr_t kZeroCell = 0x5545F50;        // the 8-byte zero cell copied into the payload (0x287F0DB)
constexpr uint16_t kOp2322 = 2322, kOp2335 = 2335, kOp2126 = 2126, kOp2958 = 2958, kOp2909 = 2909;
constexpr uint16_t kOp2283 = 2283, kOp2362 = 2362, kOp2437 = 2437, kOp2001 = 2001;

// U-c2 (the 2e38ce5 run, probe H: its id list [+0x150] stayed 0 while the host's held 52065): what the native
// preparation 0x287EF60 builds right after its arm 0x287F450 (0x287F06F), and the minimal arm skipped:
//   0x287F084 call 0x2896C60(mgr, field, actor): clears [mgr+0x158] and fills the id list [mgr+0x150] from the static
//             stage data of this field (its only caller); 0x28818F0's element loop walks exactly that list.
//   0x287F0A0 call 0x287FA30(mgr, field): the per-category tables at [mgr+0x400] (+0x408 count, ...) that 0x28818F0's
//             fourteen category loops read (from the field's interest node; its only caller is 0x287EF60 too).
// Not called: 0x287F7D0 (the "always active" sequencers: weather, quest progression scenes) and the 2322.
// Called once per arm, inside its first armed tick at the owner gate (0x288B87A, inside 0x288B7A0 under 0x287DBB0 in
// its 2043 Troc: the field attached, no manager lock held), with r12 = 0x288B7A0's 4th argument, the same actor the
// tick passes to 0x287EF60 (0x287DCC2: r9 = [rbp-0x41]).
constexpr uintptr_t kListFn = 0x2896C60;
constexpr uint8_t kListFnBytes[] = {0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4C, 0x24, 0x08};
constexpr uintptr_t kInterestFn = 0x287FA30;
constexpr uint8_t kInterestFnBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x20, 0x48, 0x89, 0x4C, 0x24, 0x08};
using ListFn = void(__fastcall*)(uintptr_t mgr, uintptr_t field, uintptr_t actor);
using InterestFn = void(__fastcall*)(uintptr_t mgr, uintptr_t field);

using ArmFn = void(__fastcall*)(uintptr_t mgr);
using LoadFn = void(__fastcall*)(uintptr_t mgr, uint8_t a, uint8_t b);
using UnloadFn = void(__fastcall*)(uintptr_t mgr, uint8_t a, uint8_t b);
using EnqueueFn = int*(__fastcall*)(uintptr_t net, int* res, uint32_t delay, uintptr_t actor, const void* unused,
                                    uintptr_t troc, const void* payload, uint16_t size);

uintptr_t g_base = 0;
uint64_t g_image_size = 0;
std::atomic<bool> g_started{false};
std::atomic<uint8_t> g_mode{0};        // u2s::Mode: user2_stages
std::atomic<uint8_t> g_route_mode{0};  // u2s::Mode: user2_timer_route (review A1: its own key)
std::atomic<uint8_t> g_elements{0};    // u2sh::Elements: user2_stages_elements (U2)
std::atomic<bool> g_pt_on[kPtCount] = {};
SafetyHookMid g_hooks[kPtCount];
SafetyHookMid g_gate_hook, g_gate2_hook, g_eval_hook;
SafetyHookInline g_elem_hook;
std::atomic<bool> g_gate_own{false}, g_gate2_on{false}, g_eval_on{false}, g_elem_on{false}, g_calls_ok{false};
std::atomic<uint64_t> g_evals_u2{0}, g_evals_host{0}, g_eval_u2_ids{0}, g_eval_u2_collected{0};

// What the module's thread knows of the two players (refreshed every second). Keys: the main characters' keys.
std::atomic<uintptr_t> g_host_char{0}, g_u2_char{0}, g_host_sm{0}, g_u2_sm{0}, g_u2_mgr{0}, g_host_mgr{0};
std::atomic<uint32_t> g_host_key{0}, g_u2_key{0}, g_host_user{0};
std::atomic<uint32_t> g_dist{UINT32_MAX};  // cells, same field only (UINT32_MAX: unknown)

// U-1's state, shared with the server's threads.
enum Req : uint8_t { kReqNone, kReqArm, kReqDrain };
std::atomic<uint8_t> g_req{kReqNone};
// Why a drain was asked / run (the lines say it).
enum Cause : uint8_t {
    kCauseDistance, kCauseField, kCauseLoad, kCauseLogout, kCauseLeftover, kCauseAbort, kCauseHostPrep, kCauseCount
};
const char* cause_name(uint64_t c) {
    switch (c) {
        case kCauseDistance: return "back under the off threshold";
        case kCauseField: return "user 2 left the host's field";
        case kCauseLoad: return "its load";
        case kCauseLogout: return "its logout";
        case kCauseLeftover: return "stages left in its manager after a drain";
        case kCauseAbort: return "U-1 aborted";
        case kCauseHostPrep: return "the host's preparation near user 2 (user2_drain_at_host_prepare)";
        default: return "?";
    }
}
std::atomic<uint8_t> g_req_cause{kCauseDistance};
// g_ever_armed: armed at least once for this character (its leftovers are U-1's).
std::atomic<bool> g_armed{false}, g_aborted{false}, g_reset_machine{false}, g_ever_armed{false};
// U-c2: its id list and category tables built for this arm (reset at each arm and each character).
std::atomic<bool> g_lists_built{false};
std::atomic<uint64_t> g_arm_done_us{0}, g_last_mgr2_tick_us{0}, g_last_host_load_us{0}, g_last_host_prep_us{0},
    g_req_since_us{0};
// Review A5: user 2's periodic 2043 chain (the payload's +0xB, r8b at T != 0) on ITS manager: the last tick that
// rescheduled (0x287DBB0 re-enqueues in state 0 and 2), and "ended" once one met state 1 with +0x68A 0 (unload at
// 0x287DD6B, no reschedule). Reset per character.
std::atomic<uint64_t> g_chain_last_us{0}, g_chain_ticks{0};
// S33: the last restart of its chain after an arm (chain_kick_due) and how many.
std::atomic<uint64_t> g_kick_us{0}, g_kicks{0};
std::atomic<uint32_t> g_kicks_arm{0};  // restarts since the last arm: at most one (the 53d70df bug)
std::atomic<bool> g_chain_ended{false};
// The 2043s that reach the HOST's manager (any subject) and those of them under user 2's subject (A1's bug: its chain
// living on the host's manager), for the cadence per minute.
std::atomic<uint64_t> g_host_2043{0}, g_host_2043_u2{0};
// Log only (§8.16): the host's manager's rescheduling 2043 ticks (r8b != 0, state 0 / 2, +0x68A 0), (time << 1) |
// user 2's subject, for the "host chains" estimate. Written by the server's thread, read by the module's.
constexpr size_t kHostTickRing = 64;
std::array<std::atomic<uint64_t>, kHostTickRing> g_host_ticks{};
std::atomic<uint64_t> g_host_tick_n{0};
// S37 phase D (fast_rearm_logic.h): user2_fast_rearm = 0 | probe | 1. All of it inert with 0.
std::atomic<uint8_t> g_fast_req{0};      // u2s::Mode as asked in the ini (user2_stages_set_fast_rearm)
std::atomic<uint8_t> g_fast_mode{0};     // u2s::Mode (effective: 1 only with user2_stages=1)
std::atomic<bool> g_req_fast{false};     // the pending arm request is a fast one (mode 1)
std::atomic<uint64_t> g_arm_gen{0};      // +1 per arm that ran (the one-shot's generation)
std::atomic<bool> g_arm_fast{false};     // the last arm was a fast one
std::atomic<uint64_t> g_first_tick_us{0};  // the first 2043 (any shape) on user 2's manager after the last arm
frm::OneShotSlot g_oneshot;
std::atomic<uint64_t> g_os_expect_us{0};   // when the next chain tick would have come after the fast arm
std::atomic<uint64_t> g_os_queued{0}, g_os_consumed{0}, g_os_cancelled{0}, g_os_lost{0}, g_os_late{0},
    g_os_failed{0}, g_os_saved_ms_sum{0}, g_os_saved_ms_max{0}, g_os_delay_ms_max{0};
std::atomic<uint64_t> g_fast_arms{0}, g_fast_refused{0}, g_fast_dist_refused{0}, g_fast_jumps{0};
// Review 3b: the last load Troc of user 2 that started (its entry), and fast arms / would followed by one within 30 s.
std::atomic<uint64_t> g_u2_load_us{0};
std::atomic<uint64_t> g_fast_verdicts[static_cast<size_t>(frm::Fast::Count)] = {};
std::atomic<uint64_t> g_left_skip{0}, g_left_would{0};  // the leftover WRN during / after a preparation drain
// User 2's own rescheduling 2043 ticks on its manager (time), like g_host_ticks: its chains (key != 0 only).
std::array<std::atomic<uint64_t>, kHostTickRing> g_u2_ticks{};
std::atomic<uint64_t> g_u2_tick_n{0};
std::atomic<uint64_t> g_dup_far{0}, g_unexp_2322{0}, g_2335_own_to_u2{0}, g_2322_to_u2{0};
// U-a2: passed (r8 := its user key), would (not armed / probe), and hits inside its 2043 for another character.
std::atomic<uint64_t> g_gate2_passed{0}, g_gate2_would{0}, g_gate2_native_null{0};
// Log only (the bddc236 run: "another character" 42 of 42): the last other character's key and vtable RVA there.
std::atomic<uint32_t> g_gate2_other_key{0}, g_gate2_other_vt{0};
// Inside its armed 2043 tick, ServerField::vf40 0x2B00C70 answered with its own ServerQuestManager (user2_funnels).
std::atomic<uint64_t> g_tick_vf40_own{0};
// The 288f87b run: the other vf40 calls of its tick (0x2345xxx / 0x225Axxx, conditions on the field's actors) given
// user 2's quest manager made the NPCs' buffs flip (ApplyBuffAck x13743, UnapplyBuffAck x2494 to the peer, 95x the
// run before): only the context builder's call (return 0x2880D74) gets it now; the others are counted.
std::atomic<uint64_t> g_tick_vf40_other{0};
constexpr uint32_t kCtxVf40Ret = 0x2880D74;
// The other vf48 calls inside its armed 2043 tick answered with its own manager (review M3, the 753f275 run).
std::atomic<uint64_t> g_tick_vf48_own{0};
// U-f2: schedule elements not created because the other player's manager has that stage alive (user 2's / the host's).
std::atomic<uint64_t> g_dup_skip_u2{0}, g_dup_skip_host{0};
std::atomic<uint64_t> g_gate_passed{0}, g_gate_would{0}, g_arms{0}, g_arm_fails{0}, g_drains{0}, g_drains_sync{0};
std::atomic<uint64_t> g_elem_seen[static_cast<size_t>(u2s::Element::Count)] = {},
                      g_elem_skipped[static_cast<size_t>(u2s::Element::Count)] = {};
// U2: user 2's sequencer elements by U-f's verdict (u2sh::ElemVerdict).
std::atomic<uint64_t> g_elem_verdicts[static_cast<size_t>(u2sh::ElemVerdict::Count)] = {};
// S46 T1 (verdict only): the shop elements U-f skips today, by what T1 would do.
std::atomic<uint64_t> g_t1[static_cast<size_t>(u2sh::T1::Count)] = {};
// S46 phase 3, T1 active: user2_shop_trade_list (u2s::Mode), its abort latch (user2_shops_prepared: a funcnpc off the
// list prepared by user 2's manager) and the births inside the prepare of one of its trade scenes (U-e / families /
// roads not consulted for them: excluded by their scene of origin; probe: counted only).
std::atomic<uint8_t> g_trade_list{0};
std::atomic<bool> g_t1_latched{false};
std::atomic<uint64_t> g_t1_latched_id{0}, g_t1_latched_hash{0};
std::atomic<uint64_t> g_t1_origin_births{0}, g_t1_origin_excluded{0}, g_t1_origin_changed{0};
std::atomic<uint32_t> g_t1_origin_last_key{0}, g_t1_origin_last_stage{0};
// The static stage id of the trade element whose original 0x288A970 runs on THIS thread (0: none). Set only around
// the original call of a PassFuncnpc element with T1 on (or probe), cleared right after it.
thread_local uint32_t t_t1_origin = 0;
bool t1_list_on() noexcept {
    return u2sh::t1_list_on(static_cast<u2s::Mode>(g_trade_list.load(std::memory_order_relaxed)),
                            g_t1_latched.load(std::memory_order_relaxed));
}
std::atomic<uint64_t> g_route[static_cast<size_t>(u2s::Route::Count)][3] = {};  // [route][timer 2043/2045/2046]
std::atomic<uint64_t> g_uh[4] = {};  // U-h: 2335 2126 2958 2909 of user 2's stages at the host's client
std::atomic<uint64_t> g_faults{0};
std::atomic<uint64_t> g_2001_seen{0}, g_2001_armed{0}, g_logout_drains{0};
// Review M3: every OTHER vf48 under user 2's 2043/2045/2046 Troc (not the handler's own), by (opcode, return RVA).
u2s::CountTable<64> g_timer_vf48;

// Counters (U-0).
u2s::CountTable<128> g_closes;   // tag 1: C (reason, owner, return RVA)
u2s::CountTable<64> g_deletes;   // tag 2: D (reason, owner of the sm, caller RVA)
u2s::CountTable<128> g_logouts;  // tag 3: O (reason, who the actor is, caller RVA) - the server's threads only
std::atomic<uint64_t> g_host_cell{0}, g_u2_cell{0};
u2s::RouteTable g_routes;
std::atomic<uint64_t> g_loads[static_cast<size_t>(Who::Count)] = {}, g_unloads[static_cast<size_t>(Who::Count)] = {},
                      g_creates[static_cast<size_t>(Who::Count)] = {}, g_preps[static_cast<size_t>(Who::Count)] = {};
std::atomic<uint64_t> g_create_same_other{0}, g_create_checked{0}, g_direct_sched{0}, g_direct_faction{0};
std::atomic<uint64_t> g_closed_npcs[static_cast<size_t>(Who::Count)] = {};
std::atomic<uint64_t> g_logout_client{0};

// S31 (u0-resultados-s28.md §8.17): U-e (user2_npc_control) and the drain at the host's preparation.
std::atomic<uint8_t> g_ctl_mode{0};    // u2s::Mode: user2_npc_control
std::atomic<uint8_t> g_dprep_mode{0};  // u2s::Mode: user2_drain_at_host_prepare
std::atomic<bool> g_ctl_hook_on{false}, g_ctl_aborted{false};
u2s::KeyTable<16384> g_ctl_keys;  // the NPCs given to user 2 (1) or that would be (probe; S31: ~400 a minute)
std::atomic<uint32_t> g_ctl_key2{0};   // the user key last written ([pj2+0x90])
std::atomic<uint64_t> g_ctl_verdicts[static_cast<size_t>(u2s::Ctl::Count)] = {};
u2s::CountTable<32> g_ctl_descs;       // under user 2's armed timers: the other descriptors (vtable RVA | type << 32)
std::atomic<uint64_t> g_ctl_r42[2] = {};  // reason-42 logouts of tracked keys: [0] would (probe), [1] assigned
// S43: of them, those that repeat a stage slot (S31's loop: the abort counts these) and those with an unknown slot (also
// counted as repeats); the last reason-42 logout of each slot.
std::atomic<uint64_t> g_ctl_r42_loop[2] = {}, g_ctl_r42_noslot[2] = {};
u2s::R42SlotTable<4096> g_r42_slots;
std::atomic<uint64_t> g_ctl_drive_first{0}, g_ctl_2005_ok{0}, g_ctl_2005_bad{0}, g_ctl_host_2005_ok{0},
    g_ctl_host_2005_bad{0}, g_ctl_host_req{0};
std::atomic<uint64_t> g_last_drain_us{0};  // the last drain of user 2's manager that ran (any cause)
std::atomic<uint64_t> g_prep_verdicts[static_cast<size_t>(u2s::PrepDrain::Count)] = {};
std::atomic<uint64_t> g_prep_drains{0};
thread_local bool t_in_drain = false;      // inside drain_now's game calls on this thread
// Review 1 of ae14ab6: arm_now / drain_now / ctl_cleanup one at a time, whatever the thread (a try-lock with a short
// bounded spin; a refusal is a line, and a refused request is asked again); while the host's preparation drains user
// 2's manager, user 2's Trocs that start wait at their entry (bounded).
std::atomic<bool> g_mod_busy{false}, g_p_draining{false};
std::atomic<uint64_t> g_busy_refusals{0}, g_troc_waits{0}, g_troc_wait_timeouts{0};
std::atomic<uint8_t> g_last_drain_cause{0};
// Review 2: the distance drain held for the host's preparation (module thread only).
uint64_t g_hold_since = 0;
bool g_hold_probe_logged = false;
std::atomic<uint64_t> g_holds{0}, g_hold_expired{0};
// Review 3: the NPCs given to user 2 that outlive a drain or its departure are logged out (reason 25, as the
// collector), from a server thread, through the game's own SAM::FindActor scope.
std::atomic<bool> g_cleanup_ok{false};
std::atomic<uint64_t> g_cleanup_done_for{0}, g_cleaned{0}, g_cleanup_gone{0}, g_cleanup_other{0}, g_cleanup_runs{0},
    g_cleanup_busy{0}, g_cleanup_no_sam{0};
std::atomic<uint64_t> g_ctl_untracked{0};
// The 6c12c82 crash review: the first accepted StartActionReq of each NPC given to user 2 (child_hold's hook), its
// delay from the birth, the assigned 2005s with restore 0 at the host's client (must be 0), and the cleanup of the
// unaccepted ones after the abort (asked by the module thread, run at user 2's next Req exit).
std::atomic<uint64_t> g_ctl_accepted{0}, g_ctl_accept_us_sum{0}, g_ctl_accept_us_max{0}, g_ctl_accept_slow{0},
    g_ctl_host_2005_r0{0};
std::atomic<bool> g_cleanup_unaccepted_req{false};
// S33: las observaciones son independientes de U-e. Sólo activation=1 puede retirar una familia.
std::atomic<uint8_t> g_activation_mode{0}, g_family_probe{0}, g_road_probe{0};
std::atomic<bool> g_activation_ready{false}, g_node_ready{false}, g_activation_fault{false};
// Prueba en un PC (14-10, 15:42): una unidad abandonada (rechazos agotados o un nodo agotado que renace por un sitio
// sin veto) es una decisión POR UNIDAD: queda viva con su controlador, no se retira otra vez y sus claves salen del
// vigilante antiguo (u2s::kKeyAbandoned). La red antigua de U-e sólo vuelve si la contención deja de poder actuar
// (containment_ready). g_activation_gave_up cuenta las unidades; pasado npcact::kAbandonWarn, un WRN (sin abortar).
std::atomic<bool> g_abandon_warned{false};
std::atomic<uint64_t> g_birth_serial{0}, g_activation_faults{0}, g_activation_refused{0}, g_activation_scan_us{0},
    g_activation_untracked{0}, g_activation_gave_up{0}, g_activation_gave_up_keys{0}, g_node_vetoes{0},
    g_node_group_vetoes{0}, g_node_veto_skips{0}, g_node_group_unread{0}, g_node_other_owner{0};
// Sólo registros de interés (asignados a user 2, candidatos de U-e y su familia): las búsquedas de
// claves ajenas terminan en un hueco bajo cerrojo compartido.
std::atomic<size_t> g_activation_tracked{0};
SRWLOCK g_activation_lock = SRWLOCK_INIT;
npcact::Table<16384, 4096> g_activation;
p::EventRing<4096> g_activation_events;
enum NpcEvent : uint16_t { kNpcBorn, kNpcFamily, kNpcQueued, kNpcReq, kNpcHandler, kNpcAction,
                           kNpcRemoved, kNpcRetired, kNpcRefused, kNpcRoad, kNpcStalled, kNpcNodeBlocked, kNpcPlan,
                           kNpcPlanMember, kNpcNodeSeen, kNpcGaveUp, kNpcVetoSkipped, kNpcPostponed, kNpcCancelled,
                           kNpcPostponeExhausted,
                           // S36 fase B (npc_family_control): censo por raíz al cerrar su marco, miembro partido, hijo
                           // tardío visto en la publicación, miembro sin acción con su raíz progresando, bloqueo
                           // hoja->raíz en una retirada parcial.
                           kNpcFamCensus, kNpcFamSplit, kNpcFamLate, kNpcFamMemberStall, kNpcFamBlocked,
                           // Prueba en dos copias (15-10): diagnóstico de un hijo con +0x2F0 cuyo padre no estaba
                           // en ningún marco, o primer hijo de una raíz que el preexamen vio sin hijos.
                           kNpcFamOrphan,
                           // Prueba en dos copias 2: preexamen fallido de una raíz (envoltorio, interior, padre).
                           kNpcFamScanFail, kNpcFamVehiclesBirth, kNpcFamVehiclesCensus };
npcact::SeenCache<1024> g_node_seen;  // bajo g_activation_lock
npcact::KeyCache<4096> g_road_seen;   // ídem
// S35 fase A (docs/research/s25/s35-fase-a.md; AUDITORIA.md §4). Modos npcact::Mode (0 | probe | 1).
//  npc_action_evidence: el criterio H1b (npcact::classify) en lugar del emisor S34 mal leído.
//  npc_containment_guard: no retirar por silencio/carga del invitado, gracia acotada por Req, revalidar al ejecutar.
//  npc_degradation: una contención no lista con npc_activation=1 no rearma el aborto global de 5 s.
std::atomic<uint8_t> g_evidence_mode{0}, g_guard_mode{0}, g_degrade_mode{0};
std::atomic<uint64_t> g_no_progress_us{npcact::kNoProgressUs}, g_req_grace_us{8'000'000}, g_silence_us{2'000'000};
std::atomic<uint64_t> g_last_guest_req_us{0};  // la última Req de IA real del invitado (cualquier clave)
std::atomic<uint64_t> g_evidence_n[static_cast<size_t>(npcact::Evidence::Count)] = {};
std::atomic<uint64_t> g_evidence_disagree{0};  // probe: UNIDADES en que la S34 y la H1b decidieron distinto
std::atomic<uint64_t> g_evidence_exit_mismatch{0};  // H1b aplicado con actor+0x90 de salida ilegible o cambiado
std::atomic<uint64_t> g_postponed_n[5] = {}, g_guard_would_n[5] = {};  // por npcact::Due
std::atomic<uint64_t> g_recheck_n[5] = {};  // por npcact::Recheck, al ejecutar una retirada elegida
std::atomic<uint64_t> g_assign_paused{0}, g_degrade_would{0};  // asignaciones (nacimientos que U-e daría) pausadas
std::atomic<bool> g_degraded_state{false};
// Revisión S35: el tope de posposición (hallazgo 2) y las unidades sin progreso durante la degradación (hallazgo 3).
std::atomic<uint64_t> g_max_postpone_us{60'000'000}, g_postpone_exhausted{0}, g_postpone_age_max_us{0};
std::atomic<uint64_t> g_degraded_stalled{0}, g_degraded_stalled_max{0};
// S36 fase B (docs/research/s25/s36-fase-b.md; AUDITORIA.md §2, §6, §8.3): npc_family_control = 0 | probe | 1 y la
// cohorte npc_family_types (npc_family_logic.h). g_family_req: lo pedido en el ini; g_family_mode: lo efectivo
// (1 sólo con todos sus requisitos, si no probe; se fija en user2_stages_start, antes de cualquier hook).
std::atomic<uint8_t> g_family_req{0}, g_family_mode{0};
std::atomic<uint16_t> g_family_types{npcfam::kDefaultTypes};
std::atomic<uint8_t> g_family_vehicles{0};
std::atomic<uint64_t> g_family_vehicle_lines{0};
famveh::Mode family_vehicles() noexcept {
    return static_cast<famveh::Mode>(g_family_vehicles.load(std::memory_order_relaxed));
}
// Definidas junto a las lecturas de fase C; se reutiliza el mismo contrato protegido de asientos/montura.
road::Ride road_ride(uint32_t key, uintptr_t actor, bool type5, bool count) noexcept;
std::atomic<uint64_t> g_troc_serial{0};
npcfam::FramePool<8> g_fam_pool;          // ~390 KB en total (1023 registros + 128 unidades por marco), no por hilo
npcact::SeenCache<256> g_fam_split_nodes; // bajo g_activation_lock: nodos (época, recurso, índice, generación) partidos
std::atomic<uint64_t> g_fam_admit[static_cast<size_t>(npcfam::Admit::Count)] = {};
std::atomic<uint64_t> g_fam_scan[static_cast<size_t>(npcfam::ScanVerdict::Count)] = {};
std::atomic<uint64_t> g_fam_member[4] = {};  // por npcfam::MemberVerdict (reservados y tardíos)
std::atomic<uint64_t> g_fam_split_why[static_cast<size_t>(npcfam::SplitWhy::Count)] = {};
std::atomic<uint64_t> g_fam_census[static_cast<size_t>(npcfam::Census::Count)] = {};
std::atomic<uint64_t> g_fam_orphans{0}, g_fam_link_unknown{0}, g_fam_lost{0}, g_fam_no_frame{0}, g_fam_late{0},
    g_fam_late_split{0}, g_fam_pub_late{0}, g_fam_pub_split{0}, g_fam_link_mismatch{0}, g_fam_split_marked{0},
    g_fam_blocked{0}, g_fam_host_roots{0}, g_fam_desc_reborn{0}, g_fam_member_stalled{0};
// Coste (AUDITORIA-2 cambio 10): microsegundos del camino de familias en el mid y ActivationLock tomados desde él.
std::atomic<uint64_t> g_fam_mid_n{0}, g_fam_mid_us{0}, g_fam_mid_max_us{0}, g_fam_mid_locks{0};
npcact::Mode family_mode() noexcept { return static_cast<npcact::Mode>(g_family_mode.load(std::memory_order_relaxed)); }
// Claves nacidas dentro de un marco de familia (conjunto aproximado, atómico): en la publicación, un hijo nuevo de una
// unidad cuya clave no está aquí nació FUERA de su marco (tardío, también en probe). Segunda revisión (R5): cada
// nacimiento de la clave reescribe su marca (npc_observe_birth), con la época: una clave reutilizada ya no oculta a un
// tardío genuino.
npcfam::KeyMarks<8192> g_fam_seen;
// Segunda revisión (R4): claves cuyo descriptor tenía padre (+0x2F0 leído presente) en su último nacimiento: sólo ellas,
// tardías, parten su unidad. Reescrita en cada nacimiento con npc_family_control != 0.
npcfam::KeyMarks<16384> g_fam_lineage;
std::atomic<uint64_t> g_fam_late_hier{0};  // tardíos sin linaje de descriptor: contados, nunca parten
// Prueba en dos copias (15-10, kit 98491fc, probe): ninguna familia real se vio (sin_hijos=111, huérfanos=89) aunque
// U-e asignó guardias cuyos perros (tipo 6) nacieron después con el anfitrión. Las raíces sin hijos en su preexamen
// también se registran en su marco (unidad de 0 nodos): un hijo que nace después en el mismo Troc y cuyo +0x2F0 nombra
// ese descriptor sigue la política de tardíos (pertenencia releída, reserva propia, o la unidad queda partida).
// childless_reg: raíces así registradas; childless_late: hijos encontrados por ellas; orphan_lines: líneas de
// diagnóstico [huérfano] emitidas (acotadas).
std::atomic<uint64_t> g_fam_childless_reg{0}, g_fam_childless_late{0};
// Prueba en dos copias 2 (15-10, kit 165c4aa): el cupo único de 64 líneas lo agotaron los viajeros (30 de 63) y no
// decía qué envoltorio excluía a las 21 raíces. Ahora el cupo es POR CLASE: huérfanos por la vtable de su padre
// (viajeros aparte, fuera de huérfanos_al_nacer: fase C), hijos de raíz sin hijos, y preexámenes fallidos por envoltorio.
inline constexpr uint32_t kFamOrphanLines = 16, kFamTravellerLines = 6, kFamScanFailLines = 8;
inline constexpr size_t kOrphanSlots = static_cast<size_t>(npcfam::OrphanCls::Count) + 1;  // + hijo de raíz sin hijos
inline constexpr size_t kScanSlots = static_cast<size_t>(npcfam::Wrap::Count) + 1;         // + fallo sin envoltorio
npcfam::LineCap<kOrphanSlots> g_fam_orphan_cap;
npcfam::LineCap<kScanSlots> g_fam_scan_cap;
std::atomic<uint64_t> g_fam_orphan_cls[static_cast<size_t>(npcfam::OrphanCls::Count)] = {};
std::atomic<uint64_t> g_fam_scan_wrap[kScanSlots] = {};
std::atomic<uint64_t> g_fam_group_units{0}, g_fam_group_nodes{0};  // unidades (se) admitidas con nodos de grupo
// Prueba en dos copias 3 (marco_lleno=14 en el Troc del armado): ocupación máxima de un marco al cerrarse, raíces sin
// hijos sin registro por falta de sitio, unidades creadas con su primer tardío y las que no tuvieron unidad libre.
std::atomic<uint64_t> g_fam_frame_max_recs{0}, g_fam_frame_max_units{0}, g_fam_childless_lost{0}, g_fam_promoted{0},
    g_fam_promote_full{0};
struct LastRoot {
    uintptr_t desc = 0;  // local al proceso y al hilo: sólo se compara, nunca se lee ni cruza la red
    uint32_t key = 0, count = 0;  // count: +0x300 del descriptor en el mid de la raíz
};
thread_local LastRoot t_last_root;
// R1: limpiezas repetidas tras un drenaje (por bloqueos) y claves que una limpieza de familia dejó vivas.
std::atomic<uint64_t> g_cleanup_rerun_at{0}, g_cleanup_rerun_for{0}, g_cleanup_reruns_run{0}, g_cleanup_passes{0},
    g_cleanup_left_blocked{0};
std::atomic<uint8_t> g_cleanup_reruns{0};
void store_max_u64(std::atomic<uint64_t>& mx, uint64_t v) noexcept {
    uint64_t cur = mx.load(std::memory_order_relaxed);
    while (v > cur && !mx.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
    }
}
bool mode_is(const std::atomic<uint8_t>& m, npcact::Mode v) noexcept {
    return m.load(std::memory_order_relaxed) == static_cast<uint8_t>(v);
}
struct ActivationLock {
    ActivationLock() noexcept { AcquireSRWLockExclusive(&g_activation_lock); }
    ~ActivationLock() {
        g_activation_tracked.store(g_activation.count, std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&g_activation_lock);
    }
};
struct ActivationRead {
    ActivationRead() noexcept { AcquireSRWLockShared(&g_activation_lock); }
    ~ActivationRead() { ReleaseSRWLockShared(&g_activation_lock); }
};
// Filtro barato de los caminos calientes (cada StartAction, Req, 2005, Logout): sin registro no hay trabajo.
bool npc_tracked(uint32_t key) noexcept {
    if (!key || !g_activation_tracked.load(std::memory_order_relaxed)) return false;
    ActivationRead read;
    return g_activation.find(key) != nullptr;
}
bool npc_probes() noexcept {
    return g_activation_mode.load(std::memory_order_relaxed) || g_family_probe.load(std::memory_order_relaxed) ||
           g_road_probe.load(std::memory_order_relaxed);
}
bool activation_on() noexcept {
    return g_activation_mode.load(std::memory_order_relaxed) == static_cast<uint8_t>(npcact::Mode::On);
}
bool containment_ready() noexcept;  // tras ctl_mode_now
// S35: la política de la contención (guard = npc_containment_guard=1; probe la calcula aparte para registrarla).
npcact::Policy containment_policy(bool guard) noexcept {
    npcact::Policy p;
    p.guard = guard;
    p.h1b = mode_is(g_evidence_mode, npcact::Mode::On);
    p.no_progress_us = g_no_progress_us.load(std::memory_order_relaxed);
    p.req_grace_us = g_req_grace_us.load(std::memory_order_relaxed);
    p.silence_us = g_silence_us.load(std::memory_order_relaxed);
    p.max_postpone_us = g_max_postpone_us.load(std::memory_order_relaxed);
    return p;
}
npcact::Context containment_context(uint64_t now) noexcept {
    npcact::Context c;
    c.now = now;
    c.last_guest_req_us = g_last_guest_req_us.load(std::memory_order_relaxed);
    c.guest_loading = child_hold_user2_loading();
    return c;
}
npcact::Degrade degrade_now() noexcept;  // tras containment_ready
void npc_event(NpcEvent kind, const npcact::Actor& a, uint64_t extra = 0, uint64_t more = 0, uint32_t sender = 0) noexcept {
    p::Event e;
    e.kind = kind; e.qpc_us = p8_now_us(); e.tid = GetCurrentThreadId(); e.flags = a.assigned;
    e.v[0] = a.key; e.v[1] = a.birth; e.v[2] = a.controller; e.v[3] = kind == kNpcAction ? sender : a.parent;
    e.v[4] = a.type; e.v[5] = a.node.stage; e.v[6] = a.node.data;
    e.v[7] = a.node.index; e.v[8] = a.node.epoch;
    e.v[9] = a.queued_us && e.qpc_us >= a.queued_us ? e.qpc_us - a.queued_us : 0;
    e.v[10] = extra; e.v[11] = more;
    g_activation_events.push(e);
}
void npc_member_event(NpcEvent kind, const npcact::Member& m, const npcact::Node& node, uint64_t extra = 0,
                      uint64_t more = 0) noexcept {
    npcact::Actor a;
    a.key = m.key; a.birth = m.birth; a.controller = m.controller; a.parent = m.parent; a.type = m.type; a.node = node;
    npc_event(kind, a, extra, more);
}
std::string fam_types_text(uint16_t mask) {
    std::string s;
    for (int t : {3, 6, 7}) {
        if (!(mask >> t & 1)) continue;
        if (!s.empty()) s += ',';
        s += static_cast<char>('0' + t);
    }
    return s;
}
void npc_probe_drain() {
    static constexpr const char* names[] = {"nacimiento", "familia", "2005 encolado al invitado", "primera petición", "retorno externo",
        "acción OnServer", "baja", "retirada local", "contención rechazada", "viajero", "sin progreso", "nodo bloqueado", "plan seco",
        "miembro del plan seco", "nodo observado", "contención abandonada", "veto omitido", "contención pospuesta",
        "retirada cancelada", "posposición agotada", "censo de familia", "miembro partido", "tardío en la publicación",
        "miembro sin acción", "bloqueo hoja-raíz"};
    static constexpr const char* sites[] = {"exacto", "grupo nativo", "padre nativo", "desconocido"};
    const auto split_text = [](uint64_t mask) {
        std::string s;
        for (uint8_t b = 1; b < static_cast<uint8_t>(npcfam::SplitWhy::Count); ++b) {
            if (!(mask >> b & 1)) continue;
            if (!s.empty()) s += "; ";
            s += npcfam::split_name(static_cast<npcfam::SplitWhy>(b));
        }
        return s.empty() ? std::string("-") : s;
    };
    p::Event e;
    while (g_activation_events.pop(e)) {
        if (e.kind == kNpcFamVehiclesBirth) {
            log::info("NpcFamilyVehicles [nacimiento] qpc={} key=0x{:08X} padre=0x{:08X} raíz=0x{:08X} "
                      "nacimiento_raíz={} tipo={} clase={} vt=0x{:X} wrap=0x{:X} "
                      "real={} sombra={} motivo_sombra={} modo={}",
                      e.qpc_us, e.v[0], e.v[1], e.v[2], e.v[3], e.v[4], e.v[5], e.v[6], e.v[7],
                      npcfam::member_name(static_cast<npcfam::MemberVerdict>(e.v[8])),
                      npcfam::member_name(static_cast<npcfam::MemberVerdict>(e.v[9])),
                      npcfam::split_name(static_cast<npcfam::SplitWhy>(e.v[10])), e.flags == 2 ? "1" : "probe");
            continue;
        }
        if (e.kind == kNpcFamVehiclesCensus) {
            log::info("NpcFamilyVehicles [censo sombra] qpc={} raíz=0x{:08X} nacimiento={} recurso=0x{:08X} "
                      "nodo={} miembros={} host-kept={} partidos={} sin_clasificar={} tipo5={} "
                      "alojada={} pérdidas_marco={} modo={} capacidad_sombra_no_reservada=1",
                      e.qpc_us, e.v[0], e.v[1], static_cast<uint32_t>(e.v[2]), e.v[2] >> 32,
                      e.v[3], e.v[4], e.v[5], e.v[6], e.v[7], e.v[8], e.v[9], e.flags == 2 ? "1" : "probe");
            continue;
        }
        if (e.kind == kNpcFamCensus) {
            // S36: una línea por raíz con hijos al cerrar el marco de su Troc (probe y 1).
            const uint64_t c = e.v[3];
            const auto census = static_cast<npcfam::Census>((c >> 56) & 0xFF);
            const auto admit = static_cast<npcfam::Admit>(e.v[4] & 0xFF);
            const uint64_t ctl = (e.v[4] >> 8) & 0xFF;
            const bool admitted = e.v[7] & 1, would = (e.v[7] >> 1) & 1;
            const uint64_t nodes = c & 0xFF, born = (c >> 8) & 0xFF, late = (c >> 40) & 0xFF;
            const uint64_t reserved_born = born >= late ? born - late : 0;
            log::info("NpcFamily [censo] qpc={} T{} | raíz=0x{:08X} nacimiento={} modo={} unidad={} motivo={} "
                      "estado={} nodos={} nacidos={} {}={} host-kept={} partidos={} tardíos={} pendientes={} "
                      "profundidad={} controlador=0x{:08X} recurso=0x{:08X} nodo={} prof_troc={} registros_perdidos={} "
                      "motivos_partido=[{}]",
                      e.qpc_us, e.tid, e.v[0], e.v[1], e.flags ? "probe" : "1",
                      admitted ? "admitida" : would ? "se admitiría" : "con el anfitrión",
                      ctl != 0xFF ? u2s::ctl_name(static_cast<u2s::Ctl>(ctl)) : npcfam::admit_name(admit),
                      npcfam::census_name(census), nodes, born, e.flags ? "miembros_que_asignaría" : "miembros",
                      (c >> 16) & 0xFF, (c >> 24) & 0xFF, (c >> 32) & 0xFF, late,
                      nodes > reserved_born ? nodes - reserved_born : 0, (c >> 48) & 0xFF, e.v[2],
                      static_cast<uint32_t>(e.v[6]), static_cast<uint32_t>(e.v[6] >> 32), e.v[8], e.v[9],
                      split_text(e.v[5]));
            continue;
        }
        if (e.kind == kNpcFamSplit) {
            log::warn("NpcFamily [partida] qpc={} T{} | key=0x{:08X} nacimiento={} padre=0x{:08X} raíz=0x{:08X} "
                      "nacimiento_raíz={} tipo={} clase={} profundidad={} tardío={} motivo={} modo={}{}",
                      e.qpc_us, e.tid, e.v[0], e.v[1], e.v[2], e.v[3], e.v[4], e.v[6],
                      npcfam::cls_name(static_cast<npcfam::Cls>(e.v[7] & 0xFF)), e.v[9], e.v[8],
                      npcfam::split_name(static_cast<npcfam::SplitWhy>(e.v[5] & 0xFF)), e.flags ? "probe" : "1",
                      e.flags ? " (probe: nada cambia)"
                              : " (nace con el anfitrión; su unidad se retira entera de hojas a raíz y su nodo no "
                                "vuelve a admitirse en esta generación)");
            continue;
        }
        if (e.kind == kNpcFamLate) {
            static constexpr const char* what[] = {"vínculo distinto del mid", "tardío host-kept (tipo 7)",
                                                   "tardío PARTIDO (otro controlador)", "tardío del mismo controlador",
                                                   "tardío sólo de jerarquía (sin +0x2F0: no parte la unidad)"};
            static constexpr const char* lineage[] = {"desconocido", "+0x2F0=0", "descriptor"};
            const size_t w = static_cast<size_t>(e.v[10] & 0xFF), l = static_cast<size_t>(e.v[7] & 0xFF);
            log::info("NpcFamily [tardío en {}] qpc={} T{} | key=0x{:08X} padre=0x{:08X} "
                      "controlador=0x{:08X} tipo={} raíz=0x{:08X} nacimiento_raíz={} linaje={} decisión={} modo={}",
                      e.v[9] ? "el nacimiento" : "la publicación", e.qpc_us, e.tid, e.v[0], e.v[3], e.v[2], e.v[4],
                      e.v[5], e.v[6], l < 3 ? lineage[l] : "?", w < 5 ? what[w] : "?", e.flags ? "probe" : "1");
            continue;
        }
        if (e.kind == kNpcFamMemberStall) {
            log::info("NpcFamily [miembro sin acción] key=0x{:08X} nacimiento={} raíz=0x{:08X} tipo={} host-kept={} "
                      "desde2005_encolado_us={}: su raíz progresó y él no (diagnóstico; no se retira por reloj: la unidad "
                      "la juzga su raíz)", e.v[0], e.v[1], e.v[11], e.v[4], e.v[10], e.v[9]);
            continue;
        }
        if (e.kind == kNpcFamOrphan) {
            static constexpr const char* link[] = {"ilegible", "0 (raíz)", "presente"};
            static constexpr const char* inv[] = {"ilegible", "NO", "sí"};
            const size_t l = static_cast<size_t>(e.v[9] & 0xFF), iv = static_cast<size_t>(e.v[5] & 0xFF);
            const auto oc = npcfam::orphan_class(static_cast<uint32_t>(e.v[3]));
            log::info("NpcFamily [{}] qpc={} T{} | key=0x{:08X} tipo={} descriptor=exe+0x{:X} | su padre de descriptor "
                      "(+0x2F0): vtable exe+0x{:X} clase={} hijos_ahora(+0x300)={} contiene_al_hijo={} su_+0x2F0={} | "
                      "¿es la última raíz preexaminada de este hilo? {} (raíz 0x{:08X}, +0x300 en su mid={}) | "
                      "prof_troc={} (diagnóstico acotado a {} líneas por clase)",
                      e.flags ? "hijo de raíz sin hijos en su preexamen" : "huérfano al nacer", e.qpc_us, e.tid, e.v[0],
                      e.v[1], e.v[2], e.v[3], npcfam::orphan_name(oc),
                      static_cast<int64_t>(static_cast<int32_t>(e.v[4])), iv < 3 ? inv[iv] : "?",
                      l < 3 ? link[l] : "?", e.v[6] ? "SÍ" : "no", e.v[7],
                      static_cast<int64_t>(static_cast<int32_t>(e.v[8])), e.v[10],
                      !e.flags && oc == npcfam::OrphanCls::Traveller ? kFamTravellerLines : kFamOrphanLines);
            continue;
        }
        if (e.kind == kNpcFamScanFail) {
            // Prueba en dos copias 2: qué nodo del subárbol hizo fallar el preexamen de una raíz que U-e asignaría.
            const auto v = static_cast<npcfam::ScanVerdict>(e.v[2] & 0xFF);
            const bool wrapped = v == npcfam::ScanVerdict::Excluded || v == npcfam::ScanVerdict::UnknownWrapper;
            log::info("NpcFamily [preexamen fallido] qpc={} T{} | raíz=0x{:08X} tipo={} motivo={} | nodo: envoltorio "
                      "exe+0x{:X} ({}) interior exe+0x{:X} padre exe+0x{:X} profundidad={} | hijos_directos(+0x300)={} "
                      "nodos_aceptados_antes={} recurso=0x{:08X} | modo={}: la unidad entera {} (diagnóstico acotado a "
                      "{} líneas por envoltorio)",
                      e.qpc_us, e.tid, e.v[0], e.v[1], npcfam::scan_name(v), e.v[3],
                      wrapped ? npcfam::wrap_name(npcfam::wrap_of(static_cast<uint32_t>(e.v[3]))) : "-", e.v[4], e.v[5],
                      e.v[6], static_cast<int64_t>(static_cast<int32_t>(e.v[7])), e.v[8], e.v[9],
                      e.flags ? "probe" : "1", e.flags ? "se quedaría con el anfitrión" : "se queda con el anfitrión",
                      kFamScanFailLines);
            continue;
        }
        if (e.kind == kNpcFamBlocked) {
            log::warn("NpcFamily [bloqueo hoja-raíz] key=0x{:08X} nacimiento={} padre=0x{:08X} raíz=0x{:08X}: un "
                      "descendiente suyo no se retiró en esta pasada; no se llama a Logout (fallo acotado, se reintenta)",
                      e.v[0], e.v[1], e.v[3], e.v[10]);
            continue;
        }
        if (e.kind == kNpcRefused && e.v[10] == 6) {
            log::error("NpcActivation [retirada incompleta] key=0x{:08X} nacimiento={} resultado={}: tres fallos locales; "
                       "sin más llamadas para esa encarnación, tombstone conservado, U-e continúa para los demás",
                       e.v[0], e.v[1], e.v[11]);
        }
        if (e.kind == kNpcGaveUp) {
            log::warn("NpcActivation [contención abandonada] key=0x{:08X} nacimiento={} motivo={} rechazos={} familia={} "
                      "claves fuera del vigilante antiguo={}: sólo esta unidad; no se retira otra vez, sigue viva con su "
                      "controlador 0x{:08X}; la contención continúa para las demás (no vuelve la red antigua)",
                      e.v[0], e.v[1], e.v[10], e.v[11] & 0xFF, (e.v[11] >> 8) & 0xFF, (e.v[11] >> 16) & 0xFF, e.v[2]);
        }
        if (e.kind == kNpcVetoSkipped) {
            // dato = retorno del CALL; grupo: nodos leídos del marco de 0x2819E00 (0 = no demostrado) y agotados.
            const auto site = static_cast<size_t>(e.v[11] & 0xFF);
            log::info("NpcActivation [veto omitido] qpc={} T{} | stage=0x{:X} recurso=0x{:08X} nodo={} época={} "
                      "sitio=exe+0x{:X} ({}) grupo: nodos={} agotados={}{}", e.qpc_us, e.tid, e.v[5], e.v[6], e.v[7],
                      e.v[8], e.v[10], site < std::size(sites) ? sites[site] : "?", (e.v[11] >> 8) & 0xFF,
                      (e.v[11] >> 16) & 0xFF,
                      site == static_cast<size_t>(npcact::VetoSite::Group) && !((e.v[11] >> 8) & 0xFF)
                          ? " (grupo no legible: no se veta)" : "");
            continue;
        }
        if (e.kind == kNpcAction) {
            // S35: controlador_actor = actor+0x90 antes/después (controlador ESPERADO, no emisor observado); el emisor
            // real (vf170 del contexto) no se mide; emisor_s34 = la lectura antigua (param4 no es la sesión).
            const uint64_t evbits = (e.v[10] >> 44) & 0xF;  // 0xF: npc_action_evidence=0, no calculada
            const auto ev = static_cast<npcact::Evidence>(evbits);
            static constexpr const char* deciders[] = {"s34", "s34(probe h1b)", "h1b"};
            const size_t decider = (e.v[10] >> 48) & 0x3;
            log::info("NpcActivation [acción OnServer] qpc={} T{} | key=0x{:08X} nacimiento={} controlador=0x{:08X} "
                      "controlador_actor_entrada=0x{:08X} controlador_actor_salida=0x{:08X} emisor=no_medido "
                      "emisor_s34=0x{:08X} resultado_interno={} rama={} aplicada={} evidencia={} decide={} progreso={} "
                      "salida_discrepante={} restore_antes={} restore_después={} desde2005_encolado_us={}", e.qpc_us,
                      e.tid, e.v[0], e.v[1], e.v[2], e.v[4], e.v[5], e.v[3], static_cast<int32_t>(e.v[10]),
                      (e.v[10] >> 32) & 0xFF, (e.v[10] >> 40) & 1,
                      evbits == 0xF ? "no_calculada" : npcact::evidence_name(ev), decider < 3 ? deciders[decider] : "?",
                      (e.v[10] >> 41) & 1, (e.v[10] >> 52) & 1, static_cast<int32_t>(e.v[11]),
                      static_cast<int32_t>(e.v[11] >> 32), e.v[9]);
            continue;
        }
        if (e.kind == kNpcPostponeExhausted) {
            log::warn("NpcActivation [posposición agotada] qpc={} T{} | key=0x{:08X} nacimiento={} controlador=0x{:08X} "
                      "posposiciones={} pospuesta_us={} desde2005_encolado_us={}: el invitado sigue en silencio/carga más "
                      "allá de npc_postpone_max_ms; se retira como sin progreso (no se espera para siempre)",
                      e.qpc_us, e.tid, e.v[0], e.v[1], e.v[2], e.v[11] & 0xFFFF, e.v[10], e.v[9]);
            continue;
        }
        if (e.kind == kNpcPostponed || e.kind == kNpcCancelled) {
            // dato: npcact::Due (pospuesta) o npcact::Recheck (cancelada al ejecutar); adicional: posposiciones.
            const bool cancelled = e.kind == kNpcCancelled;
            log::info("NpcActivation [{}] qpc={} T{} | key=0x{:08X} nacimiento={} controlador=0x{:08X} motivo={} "
                      "posposiciones={} desde2005_encolado_us={} última_req_invitado_hace_us={} probe={}",
                      cancelled ? "retirada cancelada" : "contención pospuesta", e.qpc_us, e.tid, e.v[0], e.v[1], e.v[2],
                      cancelled ? npcact::recheck_name(static_cast<npcact::Recheck>(e.v[10] & 0xFF))
                                : npcact::due_name(static_cast<npcact::Due>(e.v[10] & 0xFF)),
                      e.v[11] & 0xFFFF, e.v[9], e.v[6], (e.v[10] >> 8) & 1);
            continue;
        }
        log::info("NpcActivation [{}] qpc={} T{} | key=0x{:08X} nacimiento={} controlador=0x{:08X} padre=0x{:08X} "
                  "tipo={} stage=0x{:X} recurso=0x{:08X} nodo={} época={} asignado={} desde2005_encolado_us={} dato={} adicional={}",
                  e.kind < std::size(names) ? names[e.kind] : "?", e.qpc_us, e.tid, e.v[0], e.v[1], e.v[2], e.v[3],
                  e.v[4], e.v[5], e.v[6], e.v[7], e.v[8], e.flags, e.v[9], e.v[10], e.v[11]);
    }
    if (!npc_probes()) return;
    // Borrado real, una vez por segundo y fuera de los hooks: bajas naturales, otras épocas, lápidas viejas.
    uint64_t full = 0, pruned = 0;
    size_t tracked = 0, budgets = 0;
    {
        ActivationLock guard;
        g_activation.prune(server_epoch(), p8_now_us());
        full = g_activation.overflow; pruned = g_activation.pruned;
        tracked = g_activation.count; budgets = g_activation.budget_count;
    }
    const uint64_t abandoned = g_activation_gave_up.load();
    if (abandoned > npcact::kAbandonWarn && !g_abandon_warned.exchange(true)) {
        log::warn("NpcActivation [abandonos] {} unidades abandonadas en esta sesión (cota de cordura {}): cada una sigue "
                  "viva con su controlador y fuera del vigilante antiguo; no se aborta nada, revisar el log",
                  abandoned, npcact::kAbandonWarn);
    }
    static unsigned summaries = 0;
    if (++summaries % 60 == 0) {
        log::info("NpcActivation [resumen] registros={} (borrados {}), nacimientos sin interés={}, presupuestos de nodo={}, "
                  "eventos perdidos={}, tabla llena={}, contenciones rechazadas={}, abandonadas={} (por unidad; claves fuera "
                  "del vigilante antiguo {}), fallos nativos={}, vetos={} (de ellos en grupo nativo agotado entero {}; "
                  "omitidos {}, grupo no legible {}), constructores de otro dueño={}, sonda acción={}, veto nodo={}, "
                  "escrituras detenidas={}, red antigua={}, contención lista={}",
                  tracked, pruned, g_activation_untracked.load(), budgets, g_activation_events.dropped(), full,
                  g_activation_refused.load(), abandoned, g_activation_gave_up_keys.load(), g_activation_faults.load(),
                  g_node_vetoes.load(), g_node_group_vetoes.load(), g_node_veto_skips.load(), g_node_group_unread.load(),
                  g_node_other_owner.load(), g_activation_ready.load(), g_node_ready.load(), g_activation_fault.load(),
                  !containment_ready(), containment_ready());
        const auto ev = [](npcact::Evidence k) { return g_evidence_n[static_cast<size_t>(k)].load(); };
        const auto due = [](const std::atomic<uint64_t>* a, npcact::Due d) { return a[static_cast<size_t>(d)].load(); };
        const auto rc = [](npcact::Recheck r) { return g_recheck_n[static_cast<size_t>(r)].load(); };
        log::info("NpcActivation [resumen S35] evidencia={} (aplicada={} rama281={} rechazada={} retorno0={} "
                  "otro_controlador={} otro_nacimiento={} retirada={} ilegible={}; S34 y H1b distintas={}) | guard={} "
                  "plazo_ms={} gracia_req_ms={} silencio_ms={} pospuestas req={} silencio={} carga={} (probe: "
                  "pospondría req={} silencio={} carga={}) | al ejecutar: válidas={} ya_no_está={} progreso={} "
                  "evidencia_nueva={} posponer={} | degradación={} asignaciones_pausadas={} red_antigua_no_rearmada={} "
                  "| salida_discrepante={} posposiciones_agotadas={} posposición_máx_ms={} tope_posposición_ms={} "
                  "degradada_sin_progreso={} (máx {})",
                  npcact::mode_name(static_cast<npcact::Mode>(g_evidence_mode.load())), ev(npcact::Evidence::Applied),
                  ev(npcact::Evidence::Branch281), ev(npcact::Evidence::Rejected), ev(npcact::Evidence::ReturnZeroOnly),
                  ev(npcact::Evidence::OtherController), ev(npcact::Evidence::OldBirth), ev(npcact::Evidence::Retired),
                  ev(npcact::Evidence::Unreadable), g_evidence_disagree.load(),
                  npcact::mode_name(static_cast<npcact::Mode>(g_guard_mode.load())), g_no_progress_us.load() / 1000,
                  g_req_grace_us.load() / 1000, g_silence_us.load() / 1000, due(g_postponed_n, npcact::Due::PostponeReq),
                  due(g_postponed_n, npcact::Due::PostponeSilence), due(g_postponed_n, npcact::Due::PostponeLoading),
                  due(g_guard_would_n, npcact::Due::PostponeReq), due(g_guard_would_n, npcact::Due::PostponeSilence),
                  due(g_guard_would_n, npcact::Due::PostponeLoading), rc(npcact::Recheck::Valid),
                  rc(npcact::Recheck::Gone), rc(npcact::Recheck::Progress), rc(npcact::Recheck::NewEvidence),
                  rc(npcact::Recheck::Postpone), npcact::mode_name(static_cast<npcact::Mode>(g_degrade_mode.load())),
                  g_assign_paused.load(), g_degrade_would.load(), g_evidence_exit_mismatch.load(),
                  g_postpone_exhausted.load(), g_postpone_age_max_us.load() / 1000, g_max_postpone_us.load() / 1000,
                  g_degraded_stalled.load(), g_degraded_stalled_max.load());
        if (family_mode() != npcact::Mode::Off) {
            const auto ad = [](npcfam::Admit a) { return g_fam_admit[static_cast<size_t>(a)].load(); };
            const auto mv = [](npcfam::MemberVerdict m) { return g_fam_member[static_cast<size_t>(m)].load(); };
            const auto ce = [](npcfam::Census c) { return g_fam_census[static_cast<size_t>(c)].load(); };
            std::string why, scans;
            for (uint8_t b = 1; b < static_cast<uint8_t>(npcfam::SplitWhy::Count); ++b) {
                if (const uint64_t n = g_fam_split_why[b].load()) {
                    why += std::format(" {}={};", npcfam::split_name(static_cast<npcfam::SplitWhy>(b)), n);
                }
            }
            for (uint8_t b = 1; b < static_cast<uint8_t>(npcfam::ScanVerdict::Count); ++b) {
                if (const uint64_t n = g_fam_scan[b].load()) {
                    scans += std::format(" {}={};", npcfam::scan_name(static_cast<npcfam::ScanVerdict>(b)), n);
                }
            }
            const uint64_t mid_n = g_fam_mid_n.load();
            // Miembros sin acción cuya raíz progresó (diagnóstico de familia incompleta persistente, AUDITORIA-2
            // cambio 8): una línea por miembro, nunca una retirada por reloj.
            const npcact::Policy pol = containment_policy(false);
            const uint64_t now = p8_now_us();
            uint64_t stalled = 0;
            {
                ActivationLock guard;
                for (size_t i = 0; i < g_activation.keys.size(); ++i) {
                    if (!g_activation.keys[i]) continue;
                    npcact::Actor& a = g_activation.actors[i];
                    if (!npcact::family_member(a) || a.removed || !a.assigned || npcact::has_progress(a, pol) ||
                        !a.queued_us || now < a.queued_us || now - a.queued_us < 2 * pol.no_progress_us) {
                        continue;
                    }
                    const npcact::Actor* r = g_activation.find(a.unit_key);
                    if (!r || r->birth != a.unit_birth || !npcact::has_progress(*r, pol)) continue;
                    ++stalled;
                    if (!a.member_noted) {
                        a.member_noted = true;
                        npc_event(kNpcFamMemberStall, a, a.host_kept, a.unit_key);
                    }
                }
            }
            g_fam_member_stalled.store(stalled, std::memory_order_relaxed);
            log::info("NpcFamily [resumen] modo={} cohorte={} | unidades: admitidas={} sin_hijos={} "
                      "preexamen_fallido={} ({}) gimmick_sin_puerta={} nodo_partido_antes={} hijo_antes_que_raíz={} "
                      "sin_marco={} marco_lleno={} claves_llenas={} activación_llena={} raíces_del_anfitrión={} | "
                      "miembros: {}={} host-kept={} de_unidad_del_anfitrión={} partidos={} ({}) | tardíos admitidos={} "
                      "partidos={} | huérfanos_al_nacer={} viajeros_fase_C={} hijos_de_raíz_sin_hijos={} "
                      "(raíces_registradas={}) vínculo_ilegible={} "
                      "descriptor_renacido={} "
                      "registros_perdidos={} marcos_agotados={} | censo: completas={} partidas={} pendientes={} "
                      "con_el_anfitrión={} | publicación: tardíos={} partidas_marcadas={} vínculo_distinto={} sin_linaje={} | "
                      "retirada: unidades_partidas={} bloqueos_hoja_raíz={} miembros_sin_acción={} | limpieza: "
                      "pasadas_extra={} repeticiones_tras_drenaje={} dejadas_vivas={} | coste_mid: "
                      "llamadas={} total_us={} medio_us={} máx_us={} cerrojos={} | reservas vivas: claves={} "
                      "activación={}",
                      npcact::mode_name(family_mode()), fam_types_text(g_family_types.load()),
                      ad(npcfam::Admit::Admit), ad(npcfam::Admit::Childless), ad(npcfam::Admit::ScanFailed),
                      scans.empty() ? std::string("-") : scans, ad(npcfam::Admit::GimmickNeedsGate),
                      ad(npcfam::Admit::NodeSplitBefore), ad(npcfam::Admit::ChildBornFirst), ad(npcfam::Admit::NoFrame),
                      ad(npcfam::Admit::FrameFull), ad(npcfam::Admit::KeysFull), ad(npcfam::Admit::TrackFull),
                      g_fam_host_roots.load(),
                      family_mode() == npcact::Mode::On ? "asignados" : "asignaría",
                      mv(npcfam::MemberVerdict::Assign), mv(npcfam::MemberVerdict::HostKept),
                      mv(npcfam::MemberVerdict::HostUnit), mv(npcfam::MemberVerdict::Split),
                      why.empty() ? std::string("-") : why, g_fam_late.load(), g_fam_late_split.load(),
                      g_fam_orphans.load(),
                      g_fam_orphan_cls[static_cast<size_t>(npcfam::OrphanCls::Traveller)].load(),
                      g_fam_childless_late.load(), g_fam_childless_reg.load(), g_fam_link_unknown.load(),
                      g_fam_desc_reborn.load(), g_fam_lost.load(),
                      g_fam_no_frame.load(), ce(npcfam::Census::Complete), ce(npcfam::Census::Split),
                      ce(npcfam::Census::Pending), ce(npcfam::Census::Rejected), g_fam_pub_late.load(),
                      g_fam_pub_split.load(), g_fam_link_mismatch.load(), g_fam_late_hier.load(), g_fam_split_marked.load(),
                      g_fam_blocked.load(), stalled, g_cleanup_passes.load(), g_cleanup_reruns_run.load(),
                      g_cleanup_left_blocked.load(), mid_n, g_fam_mid_us.load(), mid_n ? g_fam_mid_us.load() / mid_n : 0,
                      g_fam_mid_max_us.load(), g_fam_mid_locks.load(), g_ctl_keys.reserved(), [] {
                          ActivationRead read;
                          return g_activation.reserved;
                      }());
            // Prueba en dos copias 2: contadores por clase (qué envoltorio rechaza raíces; de qué padre son los
            // huérfanos; cuántas unidades entran por _NPCScheduleGroupActionMember).
            std::string wraps, orphans;
            for (size_t b = 0; b < kScanSlots; ++b) {
                wraps += std::format(" {}={}", b < static_cast<size_t>(npcfam::Wrap::Count)
                                                   ? npcfam::wrap_name(static_cast<npcfam::Wrap>(b))
                                                   : "sin_envoltorio",
                                     g_fam_scan_wrap[b].load());
            }
            for (size_t b = 0; b < static_cast<size_t>(npcfam::OrphanCls::Count); ++b) {
                orphans += std::format(" {}={};", npcfam::orphan_name(static_cast<npcfam::OrphanCls>(b)),
                                       g_fam_orphan_cls[b].load());
            }
            log::info("NpcFamily [clases] preexamen_fallido por envoltorio:{} | huérfanos por padre:{} | unidades con "
                      "_NPCScheduleGroupActionMember: unidades={} nodos={} (horario bajo horario, 0x100E70A0) | marcos: "
                      "registros_máx={}/{} unidades_máx={}/{} raíces_sin_hijos_sin_registro={} unidades_al_primer_tardío={} "
                      "sin_unidad_libre={}",
                      wraps, orphans, g_fam_group_units.load(), g_fam_group_nodes.load(), g_fam_frame_max_recs.load(),
                      npcfam::kFrameRecs, g_fam_frame_max_units.load(), npcfam::kFrameUnits,
                      g_fam_childless_lost.load(), g_fam_promoted.load(), g_fam_promote_full.load());
        }
    }
    // S35: la degradación por unidad, una línea en cada transición (no un aborto): qué pieza falta.
    const npcact::Degrade d = degrade_now();
    const bool degraded = npcact::assignments_paused(d);
    // Revisión S35 (hallazgo 3): mientras dure, las unidades YA asignadas sin progreso pasado el plazo quedan sin quien
    // las retire (la contención no puede actuar y el aborto antiguo no vuelve): se cuentan cada segundo y se avisan cada
    // minuto. No se retiran a ciegas: las piezas que faltan pueden ser justo la evidencia o la limpieza.
    if (degraded) {
        const uint64_t now = p8_now_us();
        const npcact::Policy pol = containment_policy(false);
        uint64_t stalled = 0;
        {
            ActivationRead read;
            for (size_t i = 0; i < g_activation.keys.size(); ++i) {
                if (!g_activation.keys[i]) continue;
                const npcact::Actor& a = g_activation.actors[i];
                if (a.assigned && !a.removed && !a.gave_up && a.queued_us && !npcact::has_progress(a, pol) &&
                    now >= a.queued_us && now - a.queued_us >= pol.no_progress_us) {
                    ++stalled;
                }
            }
        }
        g_degraded_stalled.store(stalled, std::memory_order_relaxed);
        store_max_u64(g_degraded_stalled_max, stalled);
        static unsigned degraded_ticks = 0;
        if (stalled && ++degraded_ticks % 60 == 1) {
            log::warn("NpcActivation [contención degradada] {} unidad(es) asignada(s) al usuario 2 sin progreso pasado el "
                      "plazo y sin contención que pueda retirarlas (máximo {}); U-e no asigna nuevas",
                      stalled, g_degraded_stalled_max.load());
        }
    } else {
        g_degraded_stalled.store(0, std::memory_order_relaxed);
    }
    if (degraded != g_degraded_state.exchange(degraded)) {
        if (degraded) {
            log::warn("NpcActivation [contención degradada] la contención no puede actuar (sonda acción={} veto nodo={} "
                      "limpieza={} U-e=1 {} fallo nativo={}): U-e deja de asignar NPC nuevos al usuario 2; NO vuelve el "
                      "aborto global de 5 s (npc_degradation=1) ni se retira nada en masa; los ya asignados siguen con "
                      "su controlador",
                      g_activation_ready.load(), g_node_ready.load(), g_cleanup_ok.load(),
                      g_ctl_mode.load() == static_cast<uint8_t>(u2s::Mode::On), g_activation_fault.load());
        } else {
            log::info("NpcActivation [contención degradada] fin: la contención vuelve a estar lista; U-e asigna otra vez");
        }
    }
}

// Review 6: per thread and per Troc of user 2 (user2_stages_troc_begin / _end): its vf48 answered with its own
// manager (the route of its timer handler, or inside its armed tick).
struct TrocMark {
    uint32_t depth = 0;
    bool own[16] = {};
    // S36 fase B (AUDITORIA-2 cambio 2): la pila de marcos de familias por profundidad (id de Troc + marco del
    // conjunto compartido g_fam_pool), no un único t_fam.
    npcfam::Stack fam;
};
thread_local TrocMark t_troc;
bool own_mgr_troc() noexcept {
    const TrocMark& t = t_troc;
    return t.depth && t.depth < 16 && t.own[t.depth];
}
void mark_own_mgr_troc() noexcept {
    TrocMark& t = t_troc;
    if (t.depth && t.depth < 16) t.own[t.depth] = true;
}
// Review 1: the module lock (any thread). ok = taken.
struct ModLock {
    bool ok = false;
    explicit ModLock(uint64_t spin_us = 20'000) noexcept {
        const uint64_t until = p8_now_us() + spin_us;
        for (;;) {
            bool expected = false;
            if (g_mod_busy.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                ok = true;
                return;
            }
            if (p8_now_us() >= until) {
                g_busy_refusals.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            YieldProcessor();
        }
    }
    ~ModLock() {
        if (ok) g_mod_busy.store(false, std::memory_order_release);
    }
    ModLock(const ModLock&) = delete;
    ModLock& operator=(const ModLock&) = delete;
};
u2s::CtlStats g_ctl_last_stats;            // the module thread's last read (the summary)
uint32_t g_ctl_r42_minute = 0, g_ctl_r42_loop_minute = 0;  // S43: raw (log only) and same-slot repeats (the abort)

// kETimerDirect: the S / F points' direct calls from inside a stage update (r8b != 0), apart from the real timers.
// kECtl: U-1's actions (arm, drain) and their results; kEElem: an element of user 2's manager skipped / let through
// (the first ones); kERoute: a timer of user 2 routed (the first ones).
enum Kind : uint16_t {
    kEClose, kEDelete, kELoad, kEUnload, kECreate, kETimer, kETimerDirect, kEPrep, kELogout, kECtl, kEElem, kERoute,
    kEEval, kEDup, kENpcCtl, kEPrepDrain, kKindCount
};
const char* const kKindNames[kKindCount] = {"close", "delete", "owner load", "unload", "create", "timer", "direct",
                                            "prepare", "logout 30", "control", "element", "route", "evaluation",
                                            "not created (U-f2)", "npc control (U-e)", "drain at host prepare"};
p::EventRing<1024> g_ring;
// Per (kind, user 2 involved): events per 5 s and log lines in all. User 2's classes get the larger budgets.
constexpr uint64_t kWindowUs = 5'000'000;
struct Caps {
    uint32_t host_rate, user2_rate;
    uint64_t host_lines, user2_lines;
};
constexpr Caps kCaps[kKindCount] = {
    {64, 256, 1500, 4000},  // close (a fast travel closes ~40 at once)
    {64, 256, 1000, 3000},  // delete
    {16, 64, 300, 1000},    // owner load
    {16, 64, 300, 1000},    // unload
    {32, 128, 1000, 3000},  // create
    {32, 64, 500, 2000},    // timer (lines only when user 2 is involved)
    {16, 32, 300, 1000},    // direct
    {16, 64, 300, 1000},    // prepare
    {32, 128, 1000, 3000},  // logout of reason 30 (user 2's class: the actor within 5 cells of user 2)
    {64, 64, 2000, 2000},   // control: every action (never limited in practice)
    {16, 64, 200, 2000},    // element
    {8, 16, 100, 400},      // route
    {4, 64, 40, 2000},      // evaluation (H, log only): every armed tick of user 2's manager, the host's first ones
    {64, 64, 2000, 2000},   // not created (U-f2): host's class = the host's manager skipping, user 2's = user 2's
    {16, 256, 200, 4000},   // npc control (U-e): every NPC given to user 2 (S31's loop: ~6 births a second)
    {16, 16, 200, 200},     // drain at host prepare: each host preparation while user 2's manager is armed
};
u2s::ClassBudget<kKindCount * 2> g_budget;
std::atomic<uint64_t> g_lost{0};

void set_budgets() {
    for (size_t k = 0; k < kKindCount; ++k) {
        g_budget.set(u2s::limit_class(k, false), kCaps[k].host_rate, kCaps[k].host_lines);
        g_budget.set(u2s::limit_class(k, true), kCaps[k].user2_rate, kCaps[k].user2_lines);
    }
}

u2s::Mode mode_now() noexcept { return static_cast<u2s::Mode>(g_mode.load(std::memory_order_relaxed)); }
u2s::Mode route_mode_now() noexcept { return static_cast<u2s::Mode>(g_route_mode.load(std::memory_order_relaxed)); }
u2s::Mode ctl_mode_now() noexcept { return static_cast<u2s::Mode>(g_ctl_mode.load(std::memory_order_relaxed)); }
u2s::Mode dprep_mode_now() noexcept { return static_cast<u2s::Mode>(g_dprep_mode.load(std::memory_order_relaxed)); }
u2s::Mode fast_mode_now() noexcept { return static_cast<u2s::Mode>(g_fast_mode.load(std::memory_order_relaxed)); }
// S33 (revisión): npc_activation=1 sustituye el aborto antiguo de U-e por falta de primera acción sólo
// mientras la contención puede actuar; si falta una pieza o falla (fallo nativo, escrituras detenidas), vuelve.
// Prueba en un PC (14-10, 15:42): abandonar una unidad ya no la devuelve (decisión por unidad, npc_abandon).
bool containment_ready() noexcept {
    npcact::Readiness r;
    r.activation_on = activation_on();
    r.action_probe = g_activation_ready.load(std::memory_order_relaxed);
    r.node_probe = g_node_ready.load(std::memory_order_relaxed);
    r.cleanup_fns = g_cleanup_ok.load(std::memory_order_relaxed);
    r.ctl_on = ctl_mode_now() == u2s::Mode::On;
    r.fault = g_activation_fault.load(std::memory_order_relaxed);
    return npcact::containment_ready(r);
}
npcact::Degrade degrade_now() noexcept {
    npcact::Degrade d;
    d.activation_on = activation_on();
    d.ready = containment_ready();
    d.degradation_on = mode_is(g_degrade_mode, npcact::Mode::On);
    return d;
}
// U-1 has user 2's manager armed (mode 1, not aborted).
bool u1_armed() noexcept {
    return mode_now() == u2s::Mode::On && g_armed.load(std::memory_order_acquire) &&
           !g_aborted.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------- hot path

// Guarded read (game::fast_read: an access violation or in-page error returns false, a guard page is re-armed).
template <class T>
bool rd(uintptr_t a, T& v) noexcept {
    return a >= 0x10000 && game::fast_read(a, &v, sizeof v);
}
template <class T>
T rdv(uintptr_t a, T fallback = T{}) noexcept {
    T v{};
    return rd(a, v) ? v : fallback;
}
uintptr_t rdp(uintptr_t a) noexcept { return rdv<uintptr_t>(a); }

// The key of the character an object (manager / spawn manager: +0x18) belongs to.
uint32_t owner_key(uintptr_t obj, uint32_t actor_off) noexcept {
    const uintptr_t a = obj ? rdp(obj + actor_off) : 0;
    return a ? rdv<uint32_t>(a + u2s::kActor_Key) : 0;
}
Who who_key(uint32_t key) noexcept {
    return u2s::who_of(key, g_host_key.load(std::memory_order_relaxed), g_u2_key.load(std::memory_order_relaxed));
}
uint32_t rva_of(uint64_t addr) noexcept { return u2s::rva_in(addr, g_base, g_image_size); }

// S43: an NPC's stage slot for the reason-42 loop check (u2s::r42_slot_id; 0 = unknown): its schedule handle
// [desc+0x4E8] -> u32 +0 data, +4 node (build 2976), read only on a CreateServerActorDesc_NPCSchedule descriptor.
uint32_t stage_slot_of(uintptr_t desc) noexcept {
    if (!desc || rva_of(rdp(desc)) != u2s::kVtDescNpcSchedule) return 0;
    const uintptr_t h = rdp(desc + 0x4E8);
    return h ? u2s::r42_slot_id(rdv<uint32_t>(h), rdv<uint32_t>(h + 4)) : 0;
}

// The game's TLS byte +0x1EC of TLS block 0: 1 on the server's threads (ServerRunThread 0x27733A6 and its handlers).
bool server_realm() noexcept {
    const uintptr_t tls_array = static_cast<uintptr_t>(__readgsqword(0x58));
    uintptr_t block = 0;
    uint8_t flag = 0;
    return rd(tls_array, block) && block && rd(block + 0x1EC, flag) && flag != 0;
}

// user2: the event is about user 2 (its stage, manager or timer): its own, larger budget.
void emit(Kind k, p::Event& e, bool user2) noexcept {
    if (k != kERoute && mode_now() == u2s::Mode::Off) return;  // user2_timer_route alone: its route lines only
    const size_t c = u2s::limit_class(k, user2);
    e.kind = k;
    e.flags = static_cast<uint16_t>(c);
    e.qpc_us = p8_now_us();
    e.tid = GetCurrentThreadId();
    if (!g_budget.take_rate(c, e.qpc_us, kWindowUs)) return;  // counted per class
    if (!g_ring.push(e)) g_lost.fetch_add(1, std::memory_order_relaxed);
}

// S46 (live phase 1, minor): the [element] lines of the trade list's own elements ([T1 would pass] / T1 active) are
// not eaten by the class budget: their own limiter (16 per 5 s), counted when over it.
p::WindowLimiter g_t1_line_lim{16, 5'000'000};
std::atomic<uint64_t> g_t1_lines_limited{0};
void emit_t1(Kind k, p::Event& e) noexcept {
    if (mode_now() == u2s::Mode::Off) return;
    e.kind = k;
    e.flags = static_cast<uint16_t>(u2s::limit_class(k, true));
    e.qpc_us = p8_now_us();
    e.tid = GetCurrentThreadId();
    if (!g_t1_line_lim.take(e.qpc_us)) {
        g_t1_lines_limited.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!g_ring.push(e)) g_lost.fetch_add(1, std::memory_order_relaxed);
}

// The running ExecuteTroc's subject (user2_funnels): user 2's main character, or none.
bool subject_user2(uint16_t* op = nullptr) noexcept {
    uintptr_t actor = 0;
    uint16_t opcode = 0;
    bool hooked = false;
    user2_funnels_subject_now(actor, opcode, hooked);
    if (op) *op = opcode;
    return hooked && actor && actor == g_u2_char.load(std::memory_order_relaxed);
}

uint64_t fbits(float f) noexcept {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

// A character's ServerSequencerStageManager [[[pj+0x68]+0xA8]+0x18], vtable and back pointer checked; 0 otherwise.
uintptr_t seq_manager(uintptr_t chr) noexcept {
    const uintptr_t comps = chr ? rdp(chr + u2s::kActor_Comps) : 0;
    const uintptr_t seq = comps ? rdp(comps + u2s::kComps_Seq) : 0;
    const uintptr_t mgr = seq ? rdp(seq + u2s::kSeqComp_Mgr) : 0;
    if (!mgr || rdp(mgr) != g_base + u2s::kVtStageMgr || rdp(mgr + u2s::kMgr_Actor) != chr) return 0;
    return mgr;
}
// The ServerField a character stands in (TransformSync +0x500, vtable checked); 0 otherwise.
uintptr_t field_of(uintptr_t chr) noexcept {
    const uintptr_t comps = chr ? rdp(chr + u2s::kActor_Comps) : 0;
    const uintptr_t ts = comps ? rdp(comps + u2s::kComps_Ts) : 0;
    const uintptr_t fld = ts ? rdp(ts + u2s::kTs_Field) : 0;
    return fld && rdp(fld) == g_base + u2s::kVtServerField ? fld : 0;
}
bool host_owns(uintptr_t field) noexcept {
    const uint32_t host = g_host_user.load(std::memory_order_relaxed);
    uint32_t owner = 0;
    return field && host && rd(field + 0x180, owner) && owner == host;
}
// A manager with any sequencer / schedule / faction stage (guarded reads).
bool lists_nonempty(uintptr_t mgr) noexcept {
    if (!mgr) return false;
    const uintptr_t sm = rdp(mgr + u2s::kMgr_Sched), fm = rdp(mgr + u2s::kMgr_Faction);
    return rdv<uint32_t>(mgr + u2s::kMgr_SeqCount) || (sm && rdv<uint32_t>(sm + u2s::kSm_Count)) ||
           (fm && rdv<uint32_t>(fm + u2s::kFm_Count));
}

// C 0x2817E80: rcx stage, rdx field, r8b reason, [rsp] the return address.
void on_close(safetyhook::Context& ctx) noexcept {
    const uintptr_t stage = ctx.rcx;
    const auto reason = static_cast<uint8_t>(ctx.r8 & 0xFF);
    const uint32_t ret = rva_of(rdv<uint64_t>(ctx.rsp));
    const uint32_t owner = owner_key(stage, u2s::kSt_Owner);
    const Who w = who_key(owner);
    g_closes.add(u2s::close_key(1, reason, w, ret));
    // Live NPC entries (at most 64 read).
    const uintptr_t entries = rdp(stage + u2s::kSt_Entries);
    const uint32_t n = std::min<uint32_t>(rdv<uint32_t>(stage + u2s::kSt_EntryCount), 64);
    uint32_t alive = 0;
    for (uint32_t i = 0; entries && i < n; ++i) {
        int32_t actor = 0;
        if (!rd(entries + static_cast<uintptr_t>(i) * u2s::kSt_EntrySize + u2s::kEntry_Actor, actor)) break;
        if (actor) ++alive;
    }
    g_closed_npcs[static_cast<size_t>(w)].fetch_add(alive, std::memory_order_relaxed);
    p::Event e;
    e.v[0] = rdv<uint64_t>(stage + u2s::kSt_Key);
    e.v[1] = owner;
    e.v[2] = reason;
    e.v[3] = ret;
    e.v[4] = alive;
    e.v[5] = n;
    e.v[6] = fbits(rdv<float>(stage + u2s::kSt_Pos));
    e.v[7] = fbits(rdv<float>(stage + u2s::kSt_Pos + 8));
    e.v[8] = static_cast<uint64_t>(w);
    emit(kEClose, e, w == Who::User2);
}

// D 0x2808D80: rcx sm, r8 &key, r9b reason, [rsp] the caller (0x287D8D5 = the unload-all).
void on_delete(safetyhook::Context& ctx) noexcept {
    const auto reason = static_cast<uint8_t>(ctx.r9 & 0xFF);
    const uint32_t ret = rva_of(rdv<uint64_t>(ctx.rsp));
    const uint32_t owner = owner_key(ctx.rcx, u2s::kSm_Actor);
    const Who w = who_key(owner);
    g_deletes.add(u2s::close_key(2, reason, w, ret));
    p::Event e;
    e.v[0] = rdv<uint64_t>(ctx.r8);
    e.v[1] = owner;
    e.v[2] = reason;
    e.v[3] = ret;
    e.v[8] = static_cast<uint64_t>(w);
    emit(kEDelete, e, w == Who::User2);
}

// L 0x287DA50(mgr, dl, r8b): rcx = the manager about to be put in state 1 (SendMoveFieldAck's owner step, or U-d's
// drain, which passes 0, 0: the line shows the native call's two bytes beside it).
void on_load(safetyhook::Context& ctx) noexcept {
    const uint32_t owner = owner_key(ctx.rcx, u2s::kMgr_Actor);
    const Who w = who_key(owner);
    g_loads[static_cast<size_t>(w)].fetch_add(1, std::memory_order_relaxed);
    if (w == Who::Host) g_last_host_load_us.store(p8_now_us(), std::memory_order_relaxed);
    uint16_t op = 0;
    const bool su2 = subject_user2(&op);
    p::Event e;
    e.v[0] = ctx.rcx;
    e.v[1] = owner;
    e.v[2] = su2;
    e.v[3] = op;
    e.v[4] = rdv<uint8_t>(ctx.rcx + u2s::kMgr_State);
    e.v[5] = ctx.rdx & 0xFF;
    e.v[6] = ctx.r8 & 0xFF;
    e.v[8] = static_cast<uint64_t>(w);
    emit(kELoad, e, w == Who::User2 || su2);
}

// U 0x287D4C0: rcx mgr: what it is about to close.
void on_unload(safetyhook::Context& ctx) noexcept {
    const uintptr_t mgr = ctx.rcx;
    const uint32_t owner = owner_key(mgr, u2s::kMgr_Actor);
    const Who w = who_key(owner);
    g_unloads[static_cast<size_t>(w)].fetch_add(1, std::memory_order_relaxed);
    const uintptr_t sm = rdp(mgr + u2s::kMgr_Sched), fm = rdp(mgr + u2s::kMgr_Faction);
    p::Event e;
    e.v[0] = mgr;
    e.v[1] = owner;
    e.v[2] = rdv<uint32_t>(mgr + u2s::kMgr_SeqCount);
    e.v[3] = sm ? rdv<uint32_t>(sm + u2s::kSm_Count) : 0;
    e.v[4] = fm ? rdv<uint32_t>(fm + u2s::kFm_Count) : 0;
    e.v[5] = rdv<uint8_t>(mgr + u2s::kMgr_State);
    const bool su2 = subject_user2();
    e.v[6] = su2;
    e.v[8] = static_cast<uint64_t>(w);
    emit(kEUnload, e, w == Who::User2 || su2);
}

// A live stage of the schedule spawn manager `sm` at this position (within 0.5 m on each axis) and, when data != 0,
// of this data id; 0 when none. A bounded walk (512 stages), guarded reads, the vtable checked first (the module
// thread's pointer may be up to 1 s old); the first failed read ends it (a list being changed under us).
uintptr_t stage_at(uintptr_t sm, const float (&p)[3], uint32_t data) noexcept {
    if (!sm || rdp(sm) != g_base + u2s::kVtSchedMgr) return 0;
    const uintptr_t list = rdp(sm + u2s::kSm_List);
    const uint32_t n = std::min<uint32_t>(rdv<uint32_t>(sm + u2s::kSm_Count), 512);
    for (uint32_t i = 0; list && i < n; ++i) {
        uintptr_t st = 0;
        float q[3] = {};
        if (!rd(list + static_cast<uintptr_t>(i) * 8, st) || !st || !game::fast_read(st + u2s::kSt_Pos, q, sizeof q)) {
            break;
        }
        const auto close_to = [](float a, float b) { return a - b < 0.5f && b - a < 0.5f; };  // false for a NaN
        if (!close_to(q[0], p[0]) || !close_to(q[1], p[1]) || !close_to(q[2], p[2])) continue;
        if (data) {
            const uintptr_t d = rdp(st + u2s::kSt_Data);
            if (!d || rdv<uint32_t>(d + u2s::kData_Id) != data) continue;
        }
        return st;
    }
    return 0;
}

// N 0x28078E0(sm rcx, &res, playData r8, file r9, ...): whether the OTHER player's schedule spawn manager already has
// this very stage alive: the same data at the same position (playData +0x38, U-f2). U-f2 skips those before this
// point; one reaching here while armed (players >= 8 cells apart) is the abort criterion.
void on_create(safetyhook::Context& ctx) noexcept {
    const uintptr_t sm = ctx.rcx;
    const uint32_t owner = owner_key(sm, u2s::kSm_Actor);
    const Who w = who_key(owner);
    g_creates[static_cast<size_t>(w)].fetch_add(1, std::memory_order_relaxed);
    const uintptr_t file = rdp(ctx.r9);
    const uint32_t data = file ? rdv<uint32_t>(file + u2s::kData_Id) : 0;
    uint64_t same = 2;  // 2 = not checked (no other manager known, or no longer a schedule spawn manager)
    const uintptr_t other = w == Who::Host ? g_u2_sm.load(std::memory_order_relaxed)
                            : w == Who::User2 ? g_host_sm.load(std::memory_order_relaxed)
                                              : 0;
    float pos[3] = {};
    const bool pos_ok = ctx.r8 && game::fast_read(ctx.r8 + kPlay_Pos, pos, sizeof pos);
    if (other && data && pos_ok && rdp(other) == g_base + u2s::kVtSchedMgr) {
        same = stage_at(other, pos, data) ? 1 : 0;
        g_create_checked.fetch_add(1, std::memory_order_relaxed);
        if (same == 1) {
            g_create_same_other.fetch_add(1, std::memory_order_relaxed);
            // U-1's abort criterion: the same data in both managers with the players >= 8 cells apart.
            if (g_armed.load(std::memory_order_relaxed) && g_dist.load(std::memory_order_relaxed) != UINT32_MAX &&
                g_dist.load(std::memory_order_relaxed) >= 8) {
                g_dup_far.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    p::Event e;
    e.v[0] = owner;
    e.v[1] = data;
    e.v[2] = same;
    e.v[3] = rdv<uint32_t>(sm + u2s::kSm_Count);
    e.v[4] = rdv<uint32_t>(sm + u2s::kSm_Weight);
    e.v[5] = fbits(pos[0]);
    e.v[6] = fbits(pos[2]);
    e.v[8] = static_cast<uint64_t>(w);
    emit(kECreate, e, w == Who::User2);
}

// The three timers' targets. Lines only when user 2 is involved (its timer, or its manager reached).
bool oneshot_tick(uintptr_t mgr, uint8_t state, uint64_t now) noexcept;
void timer_hit(u2s::Timer t, uintptr_t target, uint32_t actor_off, bool direct, uint8_t resched = 0,
               uint8_t r9b = 0xFF) noexcept {
    uint16_t op = 0;
    const bool su2 = subject_user2(&op);
    const uint32_t owner = owner_key(target, actor_off);
    const Who w = who_key(owner);
    g_routes.add(direct ? u2s::Timer::NotTimer : t, su2, w);
    uint8_t state = 0xFF;
    if (t == u2s::Timer::T2043 && !direct) {
        const uint64_t now = p8_now_us();
        if (w == Who::User2) {
            state = rdv<uint8_t>(target + u2s::kMgr_State, 0xFF);
            // S37 phase D: the fast arm's one-shot (or a late one) is never a chain / watchdog tick (AUDITORIA §4: do
            // not mask a dead chain with one-shots). With user2_fast_rearm=0 nothing here changes.
            const bool shot = fast_mode_now() != u2s::Mode::Off && frm::oneshot_shape(resched, r9b) &&
                              oneshot_tick(target, state, now);
            if (!shot) g_last_mgr2_tick_us.store(now, std::memory_order_relaxed);
            if (fast_mode_now() != u2s::Mode::Off) {
                const uint64_t arm = g_arm_done_us.load(std::memory_order_relaxed);
                uint64_t none = 0;
                if (arm && now >= arm) g_first_tick_us.compare_exchange_strong(none, now, std::memory_order_relaxed);
            }
            if (resched) {
                if (state == 1 && !rdv<uint8_t>(target + u2s::kMgr_PrepPending)) {
                    g_chain_ended.store(true, std::memory_order_relaxed);  // unloads and is not rescheduled
                } else {
                    g_chain_last_us.store(now, std::memory_order_relaxed);
                    g_chain_ended.store(false, std::memory_order_relaxed);
                    g_chain_ticks.fetch_add(1, std::memory_order_relaxed);
                    if (fast_mode_now() != u2s::Mode::Off && state != 0xFF &&
                        !rdv<uint8_t>(target + u2s::kMgr_PrepPending, 1)) {
                        const uint64_t i = g_u2_tick_n.fetch_add(1, std::memory_order_relaxed);
                        g_u2_ticks[i % kHostTickRing].store(now, std::memory_order_relaxed);
                    }
                }
            }
        }
        if (w == Who::Host) {
            g_host_2043.fetch_add(1, std::memory_order_relaxed);
            if (su2) g_host_2043_u2.fetch_add(1, std::memory_order_relaxed);
            // §8.16: only the ticks 0x287DBB0 re-enqueues (state 0 / 2 and +0x68A 0) carry a chain on.
            if (resched) {
                const uint8_t hs = rdv<uint8_t>(target + u2s::kMgr_State, 0xFF);
                if (hs != 1 && hs != 0xFF && !rdv<uint8_t>(target + u2s::kMgr_PrepPending, 1)) {
                    const uint64_t i = g_host_tick_n.fetch_add(1, std::memory_order_relaxed);
                    g_host_ticks[i % kHostTickRing].store((now << 1) | (su2 ? 1u : 0u), std::memory_order_relaxed);
                }
            }
        }
    }
    if (!su2 && w != Who::User2) return;
    p::Event e;
    e.v[0] = static_cast<uint64_t>(t);
    e.v[1] = su2;
    e.v[2] = owner;
    e.v[3] = target;
    e.v[4] = direct;
    e.v[5] = op;
    e.v[6] = resched;
    e.v[7] = state;
    e.v[8] = static_cast<uint64_t>(w);
    emit(direct ? kETimerDirect : kETimer, e, true);  // only user 2's reach this point
}
// T 0x287DBB0(mgr, cell, r8b = the payload's +0xB: reschedule, ...): only called from the 2043 handler
// (.xtls 0x1082F900, Ghidra); rcx = the manager vf48 gave.
void on_tick(safetyhook::Context& ctx) noexcept {
    // r9b = the payload's +0xC (the handler .xtls 0x1082F900 passes +0xB..+0xF as r8b, r9b and three stack bytes).
    timer_hit(u2s::Timer::T2043, ctx.rcx, u2s::kMgr_Actor, false, static_cast<uint8_t>(ctx.r8 & 0xFF),
              static_cast<uint8_t>(ctx.r9 & 0xFF));
}
// S 0x2822D00: the 2045 handler passes r8b = 0; 0x1025C310 inside a stage update passes its non-zero flag.
void on_sched(safetyhook::Context& ctx) noexcept {
    const bool direct = (ctx.r8 & 0xFF) != 0;
    if (direct) g_direct_sched.fetch_add(1, std::memory_order_relaxed);
    timer_hit(u2s::Timer::T2045, ctx.rcx, u2s::kSm_Actor, direct);
}
// F 0x27FBD60: the 2046 handler passes r8b = 0; 0x101FBFA0 inside a stage update its non-zero flag.
void on_faction(safetyhook::Context& ctx) noexcept {
    const bool direct = (ctx.r8 & 0xFF) != 0;
    if (direct) g_direct_faction.fetch_add(1, std::memory_order_relaxed);
    timer_hit(u2s::Timer::T2046, ctx.rcx, u2s::kFm_Actor, direct);
}

bool actor_cell(uintptr_t actor, u2s::Cell& out) noexcept;
bool actor_world(uintptr_t actor, u2s::World& out) noexcept;
bool drain_now(uintptr_t mgr, bool sync, uint16_t op, Cause cause) noexcept;

// user2_drain_at_host_prepare, at P for the HOST's manager (the server's thread that runs its preparation): with user
// 2's manager armed and the host now within the off threshold of user 2 in the same field, that manager is unloaded at
// once (mode 1), before the host's evaluation creates its stages. Every case with user 2's manager armed is a line
// with the context: the caller (one of 0x287EF60's three), the thread, the ExecuteTroc subject, both states.
void prep_drain(uintptr_t host_mgr, uint32_t ret, bool su2, uint16_t op) noexcept {
    const u2s::Mode dm = dprep_mode_now();
    const uint64_t now = p8_now_us();
    const uintptr_t hc = g_host_char.load(std::memory_order_relaxed), uc = g_u2_char.load(std::memory_order_relaxed);
    const uintptr_t mgr2 = seq_manager(uc);
    const uintptr_t hf = field_of(hc), uf = field_of(uc);
    const uint64_t last_drain = g_last_drain_us.load(std::memory_order_relaxed);
    const uint64_t since_drain = last_drain && now >= last_drain ? now - last_drain : UINT64_MAX;
    // Probe (review 2): a DISTANCE drain that came just before this P is what mode 1 would have held for it.
    const bool held_would = dm == u2s::Mode::Probe && since_drain <= 30'000'000 &&
                            g_last_drain_cause.load(std::memory_order_relaxed) == kCauseDistance;
    u2s::PrepDrainIn in;
    in.host_mgr = true;
    in.armed = u1_armed() || held_would;
    // Review 8: an async drain asked by distance whose immediate 2043 has not unloaded it yet.
    in.leftovers = !in.armed && mode_now() == u2s::Mode::On && g_ever_armed.load(std::memory_order_relaxed) && mgr2 &&
                   lists_nonempty(mgr2) && host_owns(uf);
    in.server_thread = server_realm();
    in.nested = t_in_drain;
    in.caller_known = u2s::prep_caller_known(ret);
    in.same_field = hf && hf == uf && host_owns(hf);
    u2s::Cell a, b;
    in.cells_known = actor_cell(hc, a) && actor_cell(uc, b);
    in.dist = in.cells_known ? u2s::cheb(a, b) : UINT32_MAX;
    in.off_cells = u2s::Thresholds{}.off_cells;
    in.trocs_in_flight = user2_funnels_trocs_in_flight();
    u2s::PrepDrain v = u2s::prep_drain_verdict(in);
    bool act = v == u2s::PrepDrain::Drain && dm == u2s::Mode::On && mgr2;
    if (act) {
        // Review 1: the flag first, then the count again (seq_cst both sides): a Troc of user 2 either is counted here
        // or waits at its entry until the drain is done. S33 review: the flag is taken by CAS and only its owner clears
        // it; npc_activation_cleanup may hold it (its retirement) and then this preparation is busy.
        bool idle = false;
        if (!g_p_draining.compare_exchange_strong(idle, true, std::memory_order_seq_cst)) {
            v = u2s::PrepDrain::Busy;
            act = false;
        } else if (in.trocs_in_flight = user2_funnels_trocs_in_flight(); in.trocs_in_flight != 0) {
            g_p_draining.store(false, std::memory_order_seq_cst);
            v = u2s::PrepDrain::Busy;
            act = false;
        }
    }
    g_prep_verdicts[static_cast<size_t>(v)].fetch_add(1, std::memory_order_relaxed);
    if (!in.armed && !in.leftovers && since_drain > 30'000'000) return;  // counted only: nothing of user 2's around
    const uintptr_t sm = mgr2 ? rdp(mgr2 + u2s::kMgr_Sched) : 0, fm = mgr2 ? rdp(mgr2 + u2s::kMgr_Faction) : 0;
    p::Event e;
    e.v[0] = static_cast<uint64_t>(v);
    e.v[1] = ret;
    e.v[2] = static_cast<uint64_t>(in.server_thread) | static_cast<uint64_t>(u1_armed()) << 1 |
             static_cast<uint64_t>(in.leftovers) << 2 | static_cast<uint64_t>(held_would) << 3;
    e.v[3] = static_cast<uint64_t>(su2) | static_cast<uint64_t>(op) << 8;
    e.v[4] = in.dist;
    e.v[5] = mgr2;
    e.v[6] = mgr2 ? rdv<uint8_t>(mgr2 + u2s::kMgr_State, 0xFF) : 0xFF;
    e.v[7] = (mgr2 ? rdv<uint32_t>(mgr2 + u2s::kMgr_SeqCount) : 0) |
             static_cast<uint64_t>(sm ? rdv<uint32_t>(sm + u2s::kSm_Count) : 0) << 20 |
             static_cast<uint64_t>(fm ? rdv<uint32_t>(fm + u2s::kFm_Count) : 0) << 40;
    e.v[9] = rdv<uint8_t>(host_mgr + u2s::kMgr_State, 0xFF);
    e.v[10] = act ? 2 : v == u2s::PrepDrain::Drain ? 1 : 0;
    e.v[11] = static_cast<uint32_t>(in.trocs_in_flight) |
              static_cast<uint64_t>(std::min<uint64_t>(since_drain / 1000, 0xFFFFFFFF)) << 32;
    e.v[8] = static_cast<uint64_t>(Who::User2);
    emit(kEPrepDrain, e, true);
    if (!act) return;
    g_prep_drains.fetch_add(1, std::memory_order_relaxed);
    drain_now(mgr2, true, op, kCauseHostPrep);
    g_p_draining.store(false, std::memory_order_seq_cst);
    g_reset_machine.store(true);  // U-1 waits for a new dwell (as after its loads)
}

// P 0x287EF60: rcx mgr, r8 the character (the 2322 target).
void on_prep(safetyhook::Context& ctx) noexcept {
    const uint32_t owner = owner_key(ctx.rcx, u2s::kMgr_Actor);
    const Who w = who_key(owner);
    g_preps[static_cast<size_t>(w)].fetch_add(1, std::memory_order_relaxed);
    if (w == Who::Host) g_last_host_prep_us.store(p8_now_us(), std::memory_order_relaxed);  // its load's end (M2)
    uint16_t op = 0;
    const bool su2 = subject_user2(&op);
    p::Event e;
    e.v[0] = ctx.rcx;
    e.v[1] = owner;
    e.v[2] = ctx.r8 ? rdv<uint32_t>(ctx.r8 + u2s::kActor_Key) : 0;
    e.v[3] = rdv<uint8_t>(ctx.rcx + u2s::kMgr_State);
    e.v[4] = su2;
    e.v[5] = op;
    e.v[8] = static_cast<uint64_t>(w);
    emit(kEPrep, e, w == Who::User2 || su2);
    // The mid hook sits on the entry: [rsp] is the caller's return address.
    if (w == Who::Host && dprep_mode_now() != u2s::Mode::Off) prep_drain(ctx.rcx, rva_of(rdv<uint64_t>(ctx.rsp)), su2, op);
}

void logout_drain(uintptr_t chr, uint8_t reason) noexcept;
void logout_cleanup(uint8_t reason) noexcept;
void road_request(road::Why why, uint16_t op) noexcept;  // S38 fase C
void road_on_logout(uint32_t key, uint8_t reason, uintptr_t actor) noexcept;

// U-e: a tracked NPC (given to user 2, or that would have been) logged out by the server; reason 42 is its
// controller's AI 'logout' (S31's loop).
void ctl_on_logout(uint32_t key, uint8_t reason) noexcept {
    bool local_call = false;
    if (npc_probes() && npc_tracked(key)) {
        ActivationLock guard;  // la baja se marca aquí; el hilo del módulo la borra (prune)
        if (auto* a = g_activation.find(key); a && !a->removed) {
            npc_event(kNpcRemoved, *a, reason);
            local_call = !npcact::observe_logout_entry(*a);
        }
    }
    if (local_call) return; // sólo cleanup_result confirma nuestras bajas locales
    const bool r42 = reason == u2s::kLogoutAiReason;
    uint16_t before = 0;
    const uint64_t now = p8_now_us();
    if (!g_ctl_keys.mark(key, static_cast<uint16_t>(u2s::kKeyRemoved | (r42 ? u2s::kKeyR42 : 0)), now, &before)) {
        return;
    }
    if (!r42 || (before & u2s::kKeyR42)) return;
    const size_t i = (before & u2s::kKeyAssigned) ? 1 : 0;
    g_ctl_r42[i].fetch_add(1, std::memory_order_relaxed);
    // S43: the abort counts only the repeats of a stage slot (S31's loop); an unknown slot counts too (fails closed).
    // Review of a4c2661 (P2-1): only when this key was put after the slot's last reason-42 logout (a respawn), not two
    // instances of the same stage leaving together.
    const uint32_t slot = g_ctl_keys.slot_of(key);
    if (!slot) g_ctl_r42_noslot[i].fetch_add(1, std::memory_order_relaxed);
    if (g_r42_slots.repeat(slot, now, u2s::kR42LoopWindowUs, g_ctl_keys.put_time(key))) {
        g_ctl_r42_loop[i].fetch_add(1, std::memory_order_relaxed);
    }
}

// O 0x2068340 (ServerActor::Logout's thunk): rcx actor, r8b reason, [rsp] the caller. S29: the client's RemoveActorAck
// 2516 handler (exe+0xB6302A) calls it too: only the SERVER's calls are counted (TLS +0x1EC), the client's counted apart.
void on_logout(safetyhook::Context& ctx) noexcept {
    if (!server_realm()) {
        g_logout_client.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto reason = static_cast<uint8_t>(ctx.r8 & 0xFF);
    // Review A4: user 2's own character leaves the server (its 2001's handler, .xtls 0x1089A3AE, reason 4, or any
    // other logout): its armed manager (or U-1's leftovers) is unloaded here, before the logout runs, on the server's
    // thread that runs it. The character is still whole at this point.
    if (ctx.rcx && ctx.rcx == g_u2_char.load(std::memory_order_relaxed)) {
        logout_drain(ctx.rcx, reason);
        logout_cleanup(reason);
        road_request(road::Why::ItsLogout, static_cast<uint16_t>(0xF000 | reason));  // S38: sus viajeros se van con él
    }
    const uint32_t ret = rva_of(rdv<uint64_t>(ctx.rsp));
    const uint32_t key = rdv<uint32_t>(ctx.rcx + u2s::kActor_Key);
    const Who w = who_key(key);
    g_logouts.add(u2s::close_key(3, reason, w, ret));
    if ((ctl_mode_now() != u2s::Mode::Off || npc_probes()) && key) ctl_on_logout(key, reason);
    road_on_logout(key, reason, ctx.rcx);  // S38: una baja nativa de un viajero registrado (no la entrada de nuestro Logout)
    if (reason != 30) return;
    u2s::Cell c;
    const bool cell_ok = actor_cell(ctx.rcx, c);
    const uint64_t hc = g_host_cell.load(std::memory_order_relaxed), uc = g_u2_cell.load(std::memory_order_relaxed);
    const uint64_t dh = cell_ok && hc ? u2s::cheb(c, u2s::unpack_cell(hc)) : UINT32_MAX;
    const uint64_t du = cell_ok && uc ? u2s::cheb(c, u2s::unpack_cell(uc)) : UINT32_MAX;
    p::Event e;
    e.v[0] = key;
    e.v[1] = reason;
    e.v[2] = ret;
    e.v[3] = dh;
    e.v[4] = du;
    e.v[8] = static_cast<uint64_t>(w);
    emit(kELogout, e, du <= 5);
}

constexpr safetyhook::MidHookFn kFns[kPtCount] = {on_close, on_delete, on_load,  on_unload, on_create,
                                                   on_tick,  on_sched,  on_faction, on_prep, on_logout};

// ---------------------------------------------------------------- U-a: the owner gate

void build_lists(uintptr_t mgr, uintptr_t field, uintptr_t actor) noexcept;

void gate_impl(uintptr_t mgr, uintptr_t chr, uintptr_t field, uintptr_t actor, uintptr_t& rax) noexcept {
    const u2s::Mode m = mode_now();
    if (m == u2s::Mode::Off || !mgr || mgr != g_u2_mgr.load(std::memory_order_relaxed)) return;
    if (!chr || chr != g_u2_char.load(std::memory_order_relaxed) || !host_owns(field)) return;
    uint32_t owner = 0;
    if (!rd(field + 0x180, owner) || static_cast<uint32_t>(rax) == owner) return;  // already passes (own field)
    if (m == u2s::Mode::On && g_armed.load(std::memory_order_acquire) && !g_aborted.load(std::memory_order_relaxed)) {
        rax = (rax & ~static_cast<uintptr_t>(0xFFFFFFFF)) | owner;  // eax := [r15+0x180]: the cmp passes
        g_gate_passed.fetch_add(1, std::memory_order_relaxed);
        // U-c2: the first armed evaluation builds what the native preparation builds after its arm.
        if (!g_lists_built.exchange(true)) build_lists(mgr, field, actor);
    } else {
        g_gate_would.fetch_add(1, std::memory_order_relaxed);
    }
}

// U-a2 at 0x2911435: inside user 2's armed evaluation, the collector's manager guard on its own manager.
void on_gate2(safetyhook::Context& ctx) noexcept {
    const u2s::Mode m = mode_now();
    if (m == u2s::Mode::Off) return;
    // Review B1: only the call from the cell evaluation 0x28818F0 (not cdmp_FieldStageManager_SyncStages' 0x2913F60),
    // for user 2's manager, inside its own 2043 Troc.
    if (rva_of(rdv<uint64_t>(ctx.rsp + kG2_Ret)) != kG2_RetRva) return;
    const uintptr_t mgr = rdp(ctx.rsp + kG2_CallerRsi);
    if (!mgr || mgr != g_u2_mgr.load(std::memory_order_relaxed)) return;
    uint16_t op = 0;
    if (!subject_user2(&op) || op != u2s::kOp2043) return;
    const uintptr_t qchr = rdp(ctx.r13 + 0x10);  // r13 = the context's quest manager: its character, logged
    g_gate2_other_key.store(qchr ? rdv<uint32_t>(qchr + u2s::kActor_Key) : 0, std::memory_order_relaxed);
    g_gate2_other_vt.store(qchr ? rva_of(rdp(qchr)) : 0, std::memory_order_relaxed);
    const uintptr_t field = rdp(ctx.rsp + kG2_Field);
    const uintptr_t chr = g_u2_char.load(std::memory_order_relaxed);
    // Its manager re-checked here (vtable, back pointer, state 2) and the field the host's.
    if (seq_manager(chr) != mgr || rdv<uint8_t>(mgr + u2s::kMgr_State) != 2 || !host_owns(field)) return;
    if (m == u2s::Mode::On && g_armed.load(std::memory_order_acquire) && !g_aborted.load(std::memory_order_relaxed)) {
        if (!ctx.rbx) g_gate2_native_null.fetch_add(1, std::memory_order_relaxed);  // FindUser found nobody
        ctx.rbx = mgr;  // 0x387EF0(&guard, its own manager)
        g_gate2_passed.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_gate2_would.fetch_add(1, std::memory_order_relaxed);
    }
}

// H (log only): what the cell evaluation of user 2's manager (every armed tick) and of the host's (the first ones,
// then one in 20) had to work with.
void on_eval(safetyhook::Context& ctx) noexcept {
    const uintptr_t mgr = ctx.rsi;
    const bool u2 = mgr && mgr == g_u2_mgr.load(std::memory_order_relaxed);
    if (!u2) {
        const uint64_t n = g_evals_host.fetch_add(1, std::memory_order_relaxed);
        if (n >= 10 && n % 20 != 0) return;
    } else {
        g_evals_u2.fetch_add(1, std::memory_order_relaxed);
    }
    const uintptr_t qm = rdp(ctx.rbp + 0x400);
    const uintptr_t qchr = qm ? rdp(qm + 0x10) : 0;
    const uint32_t ids = rdv<uint32_t>(mgr + 0x158);
    const uint32_t collected = rdv<uint32_t>(ctx.rbp + 0x670);
    if (u2) {
        g_eval_u2_ids.fetch_add(ids, std::memory_order_relaxed);
        g_eval_u2_collected.fetch_add(collected, std::memory_order_relaxed);
    }
    p::Event e;
    e.v[0] = mgr;
    e.v[1] = owner_key(mgr, u2s::kMgr_Actor);
    e.v[2] = ids;
    e.v[3] = collected;
    e.v[4] = rdv<uint32_t>(mgr + 0x74) | static_cast<uint64_t>(rdv<uint32_t>(mgr + 0xF0)) << 32;
    e.v[10] = rdv<uint32_t>(mgr + 0x408);
    e.v[5] = qchr ? rdv<uint32_t>(qchr + u2s::kActor_Key) : 0;
    e.v[6] = qm ? (rdv<uint32_t>(qm + 0x178) | static_cast<uint64_t>(rdv<uint32_t>(qm + 0x188)) << 32) : 0;
    e.v[7] = qm ? (rdv<uint8_t>(qm + 0x186) | static_cast<uint64_t>(rdv<uint32_t>(qm + 0x4C)) << 8) : 0;
    e.v[9] = u2 ? g_u2_cell.load(std::memory_order_relaxed) : g_host_cell.load(std::memory_order_relaxed);
    e.v[8] = static_cast<uint64_t>(u2 ? Who::User2 : Who::Host);
    emit(kEEval, e, u2);
}

// This module's own G (only when shop_probe is off): rdi = mgr, r13 = its character, r15 = the field.
void on_gate(safetyhook::Context& ctx) noexcept {
    uintptr_t rax = ctx.rax;
    gate_impl(ctx.rdi, ctx.r13, ctx.r15, ctx.r12, rax);
    ctx.rax = rax;
}

// ---------------------------------------------------------------- U-e: the controller at birth (user2_npc_control)

// The key of a live stage of this schedule data in the schedule spawn manager `sm` (the first; 0 = none). Bounded,
// guarded, the vtable checked first.
uint64_t stage_of_data(uintptr_t sm, uint32_t data) noexcept {
    if (!sm || rdp(sm) != g_base + u2s::kVtSchedMgr) return 0;
    const uintptr_t list = rdp(sm + u2s::kSm_List);
    const uint32_t n = std::min<uint32_t>(rdv<uint32_t>(sm + u2s::kSm_Count), 512);
    for (uint32_t i = 0; list && i < n; ++i) {
        uintptr_t st = 0;
        if (!rd(list + static_cast<uintptr_t>(i) * 8, st) || !st) break;
        const uintptr_t d = rdp(st + u2s::kSt_Data);
        if (d && rdv<uint32_t>(d + u2s::kData_Id) == data) return rdv<uint64_t>(st + u2s::kSt_Key);
    }
    return 0;
}

// ---------------------------------------------------------------- S36 fase B: familias (npc_family_control)
//
// docs/research/s25/s36-fase-b.md; npc_family_logic.h. En el MISMO create_mid 0x2792A7B (ningún gancho nuevo):
//  - una raíz (desc+0x2F0 == 0) que U-e asignaría se preexamina entera y se reserva su unidad (marco del Troc, claves
//    de g_ctl_keys, registros de g_activation) ANTES de cambiar EAX; si algo no cabe o no se demuestra, la unidad
//    entera se queda con el anfitrión (modo 1) o se cuenta (probe);
//  - un descriptor con padre (+0x2F0 != 0) o con el vínculo ilegible nunca usa su propio veredicto de U-e: su
//    controlador es el de su unidad (si fue admitida y él pasa todas las comprobaciones) o el nativo. Si no puede
//    seguir a su raíz admitida, la unidad queda PARTIDA y se retira entera, de hojas a raíz (npc_activation=1).
// Autoridad (AUDITORIA.md §6 «Fronteras»): sólo se cambia el valor que 0x2792A7B escribe en actor+0x90 (simulación de
// IA). Nunca actor+0xA0, desc+0x40, el mundo guardado ni el gestor de misiones: el crédito sigue siendo del anfitrión
// (quest_policy). Los punteros (descriptores, actores) son locales al proceso y al Troc; entre PC sólo viajan claves.

// Lo que el camino de familias del mid deja a npc_observe_birth (el mismo nacimiento, el mismo hilo).
struct FamBirth {
    uint64_t birth = 0;   // token de nacimiento (asignado aquí si el camino lo necesitó; si no, npc_observe_birth)
    bool track = false;   // modo 1, unidad admitida: registrar en g_activation (con su vínculo del mid)
    bool reserved = false;  // ... consumiendo una reserva de su unidad (sólo si se tomó para este nacimiento)
    uint8_t fam = 0, depth = 0;
    uint32_t parent = 0, unit_key = 0;
    uint64_t parent_birth = 0, unit_birth = 0;
    bool host_kept = false, split = false;
    bool probe_root = false;  // probe: raíz de una unidad que se admitiría (sólo para medir tardíos en la publicación)
    npcfam::SplitWhy why = npcfam::SplitWhy::None;
    // Segunda revisión: seen = nació en el marco de su unidad (R5); link = su +0x2F0 si este mid ya lo leyó (R4).
    bool seen = false, link_read = false;
    npcfam::Link link = npcfam::Link::Unknown;
};
struct FamRead {
    bool operator()(uintptr_t at, void* out, size_t n) const noexcept { return at >= 0x10000 && game::fast_read(at, out, n); }
};
uint64_t next_birth() noexcept { return g_birth_serial.fetch_add(1, std::memory_order_relaxed) + 1; }
npcfam::Frame* fam_frame(bool create) noexcept {
    TrocMark& t = t_troc;
    if (!t.depth || t.depth >= npcfam::kDepths || !t.fam.id[t.depth]) return nullptr;
    if (const uint8_t i = t.fam.frame[t.depth]) {
        npcfam::Frame& f = g_fam_pool.at(i - 1u);
        return f.troc == t.fam.id[t.depth] ? &f : nullptr;
    }
    if (!create) return nullptr;
    const int i = g_fam_pool.claim(GetCurrentThreadId());
    if (i < 0) {
        g_fam_no_frame.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    npcfam::Frame& f = g_fam_pool.at(static_cast<size_t>(i));
    npcfam::open_frame(f, t.fam.id[t.depth], static_cast<uint8_t>(t.depth));
    t.fam.frame[t.depth] = static_cast<uint8_t>(i + 1);
    return &f;
}
bool fam_any_frame() noexcept {
    const TrocMark& t = t_troc;
    for (uint32_t d = 1; d <= t.depth && d < npcfam::kDepths; ++d) {
        if (t.fam.frame[d]) return true;
    }
    return false;
}
npcfam::Hit fam_find(uintptr_t desc) noexcept {
    const TrocMark& t = t_troc;
    return npcfam::stack_find(t.fam, t.depth, [](size_t i) -> const npcfam::Frame& { return g_fam_pool.at(i); }, desc);
}
// ¿Algún descriptor del subárbol ya nació en algún marco de la pila (antes que su raíz)?
bool fam_born_first(const npcfam::Scan& s) noexcept {
    for (uint8_t i = 0; i < s.n; ++i) {
        const npcfam::Hit h = fam_find(s.nodes[i].desc);
        if (h.rec >= 0 && g_fam_pool.at(static_cast<size_t>(h.frame)).r[h.rec].birth) return true;
    }
    return false;
}
// El nodo de la raíz para la memoria de unidades partidas: (época, recurso, índice, generación), sin stage.
npcact::Node fam_node(uint32_t data, uint32_t index) noexcept {
    npcact::Node n;
    n.epoch = server_epoch(); n.data = data; n.index = index; n.stage = 0;
    n.generation = g_arm_done_us.load(std::memory_order_relaxed);
    return n;
}
void fam_cost(uint64_t t0) noexcept {
    const uint64_t d = p8_now_us() - t0;
    g_fam_mid_n.fetch_add(1, std::memory_order_relaxed);
    g_fam_mid_us.fetch_add(d, std::memory_order_relaxed);
    store_max_u64(g_fam_mid_max_us, d);
}
void fam_split_event(const npcfam::Rec& r, const npcfam::UnitSum& u, uint32_t parent, uint8_t type, bool late,
                     bool probe) noexcept {
    p::Event e;
    e.kind = kNpcFamSplit; e.qpc_us = p8_now_us(); e.tid = GetCurrentThreadId(); e.flags = probe;
    e.v[0] = r.key; e.v[1] = r.birth; e.v[2] = parent; e.v[3] = u.key; e.v[4] = u.birth;
    e.v[5] = static_cast<uint64_t>(r.why); e.v[6] = type; e.v[7] = static_cast<uint64_t>(r.cls); e.v[8] = late;
    e.v[9] = r.depth;
    g_activation_events.push(e);
}
// Cierre de un marco (fin de su Troc, o un índice que quedó de una salida perdida): censo por raíz, reservas sin usar
// devueltas, marco liberado. Ningún puntero sobrevive.
void fam_close(uint8_t idx1) noexcept {
    if (!idx1 || idx1 > g_fam_pool.size()) return;
    npcfam::Frame& f = g_fam_pool.at(idx1 - 1u);
    const bool probe = family_mode() != npcact::Mode::On;
    size_t track_back = 0;
    const uint64_t now = p8_now_us();
    for (uint8_t i = 0; i < f.nu; ++i) {
        const npcfam::UnitSum& u = f.u[i];
        if (u.keys_reserved > u.keys_used) g_ctl_keys.unreserve(static_cast<size_t>(u.keys_reserved - u.keys_used));
        if (u.track_reserved > u.track_used) track_back += static_cast<size_t>(u.track_reserved - u.track_used);
        // Una raíz registrada sin hijos a la que no le nació ninguno: sin censo (ni línea ni contador), como antes.
        if (u.admit == npcfam::Admit::Childless && !u.born && !u.split && !u.late) continue;
        if (family_vehicles() != famveh::Mode::Off && famveh::scope(u.root_node)) {
            p::Event v;
            v.kind = kNpcFamVehiclesCensus; v.qpc_us = now; v.tid = GetCurrentThreadId();
            v.flags = static_cast<uint16_t>(family_vehicles());
            v.v[0] = u.key; v.v[1] = u.birth; v.v[2] = u.root_node;
            v.v[3] = 0; v.v[4] = 0; v.v[5] = 0; v.v[6] = 0; v.v[7] = 0;
            for (uint16_t j = 0; j < f.n; ++j) {
                const auto& r = f.r[j];
                if (r.unit != i || r.parent == npcfam::kNoRec || !r.birth) continue;
                const auto x = static_cast<famveh::Projected>(r.projected);
                ++v.v[x == famveh::Projected::Assigned ? 3 : x == famveh::Projected::Kept ? 4 :
                      x == famveh::Projected::Split ? 5 : 6];
                if (r.type == 5) ++v.v[7];
            }
            v.v[8] = !u.admitted; v.v[9] = f.lost;
            g_activation_events.push(v);
        }
        const npcfam::Census c = npcfam::census_of(u);
        g_fam_census[static_cast<size_t>(c)].fetch_add(1, std::memory_order_relaxed);
        p::Event e;
        e.kind = kNpcFamCensus; e.qpc_us = now; e.tid = GetCurrentThreadId(); e.flags = probe;
        e.v[0] = u.key; e.v[1] = u.birth; e.v[2] = u.controller;
        e.v[3] = static_cast<uint64_t>(u.nodes) | static_cast<uint64_t>(u.born) << 8 |
                 static_cast<uint64_t>(u.members) << 16 | static_cast<uint64_t>(u.host_kept) << 24 |
                 static_cast<uint64_t>(u.split) << 32 | static_cast<uint64_t>(u.late) << 40 |
                 static_cast<uint64_t>(u.max_depth) << 48 | static_cast<uint64_t>(c) << 56;
        e.v[4] = static_cast<uint64_t>(u.admit) | static_cast<uint64_t>(u.ctl) << 8;
        e.v[5] = u.split_mask; e.v[6] = u.root_node;
        e.v[7] = static_cast<uint64_t>(u.admitted) | static_cast<uint64_t>(u.would) << 1;
        e.v[8] = f.depth; e.v[9] = f.lost;
        g_activation_events.push(e);
    }
    if (f.lost) g_fam_lost.fetch_add(f.lost, std::memory_order_relaxed);
    store_max_u64(g_fam_frame_max_recs, f.n);
    store_max_u64(g_fam_frame_max_units, f.nu);
    if (track_back) {
        ActivationLock guard;
        g_activation.unreserve(track_back);
    }
    g_fam_pool.release(static_cast<int>(idx1 - 1u), GetCurrentThreadId());
}
// Una raíz con hijos que no es (ni sería) una unidad del invitado: sólo su registro (AUDITORIA-2 cambio 5).
void fam_host_root(uintptr_t desc, uintptr_t actor, uint32_t key, FamBirth& fb, npcfam::Admit admit,
                   uint8_t ctl) noexcept {
    uint32_t count = 0;
    if (!rd(desc + npcfam::kDesc_ChildCount, count) || !count) return;
    npcfam::Frame* f = fam_frame(true);
    if (!f || !npcfam::frame_room(*f, 1)) {
        if (f) ++f->lost;
        return;
    }
    if (!fb.birth) fb.birth = next_birth();
    npcfam::add_host_root(*f, desc, actor, key, fb.birth, admit, ctl, static_cast<uint8_t>(std::min<uint32_t>(count, 0xFF)));
    g_fam_host_roots.fetch_add(1, std::memory_order_relaxed);
}
// Prueba en dos copias (15-10): diagnóstico acotado (kFamOrphanLines por sesión) de un hijo con padre de descriptor.
// what 0: su padre no estaba en ningún marco (huérfano al nacer); 1: hijo tardío de una raíz que su preexamen vio sin
// hijos. Dice si ese padre es la última raíz preexaminada en este hilo y su +0x300 entonces y ahora: así se distingue
// «el vector +0x2F8 se llena después del mid de la raíz» de «el padre es otro descriptor». Lecturas protegidas, sin CRT.
// Cupo por clase (prueba en dos copias 2): what 0 por la clase del padre (viajeros kFamTravellerLines), what 1 el suyo.
void fam_orphan_line(uintptr_t desc, uintptr_t actor, uint32_t key, uintptr_t parent, uint8_t what, int in_vec,
                     npcfam::OrphanCls oc = npcfam::OrphanCls::Other) noexcept {
    const size_t slot = what ? kOrphanSlots - 1 : static_cast<size_t>(oc);
    if (!g_fam_orphan_cap.take(slot, !what && oc == npcfam::OrphanCls::Traveller ? kFamTravellerLines : kFamOrphanLines)) {
        return;
    }
    const FamRead read;
    const LastRoot& lr = t_last_root;
    const uintptr_t ti = rdp(actor + u2s::kActor_TypeInfo);
    uint32_t count = 0;
    const bool count_ok = rd(parent + npcfam::kDesc_ChildCount, count);
    if (in_vec > 1) in_vec = npcfam::in_vector(read, parent, desc);  // 2 = sin calcular
    p::Event e;
    e.kind = kNpcFamOrphan; e.qpc_us = p8_now_us(); e.tid = GetCurrentThreadId(); e.flags = what;
    e.v[0] = key; e.v[1] = ti ? rdv<uint8_t>(ti + 1) : 0; e.v[2] = rva_of(rdp(desc)); e.v[3] = rva_of(rdp(parent));
    e.v[4] = count_ok ? count : 0xFFFFFFFFu; e.v[5] = static_cast<uint64_t>(in_vec + 1);
    e.v[6] = lr.desc && lr.desc == parent; e.v[7] = lr.key; e.v[8] = lr.count;
    e.v[9] = static_cast<uint64_t>(npcfam::read_parent(read, parent).state); e.v[10] = t_troc.depth;
    g_activation_events.push(e);
}
// Un huérfano contado por la clase de su padre de descriptor (una lectura protegida de su vtable); los viajeros no
// entran en huérfanos_al_nacer.
npcfam::OrphanCls fam_orphan_count_cls(npcfam::OrphanCls oc) noexcept {
    g_fam_orphan_cls[static_cast<size_t>(oc)].fetch_add(1, std::memory_order_relaxed);
    if (oc != npcfam::OrphanCls::Traveller) g_fam_orphans.fetch_add(1, std::memory_order_relaxed);
    return oc;
}
npcfam::OrphanCls fam_orphan_count(uintptr_t parent) noexcept {
    return fam_orphan_count_cls(npcfam::orphan_class(rva_of(rdp(parent))));
}
// Un descriptor con padre (+0x2F0 != 0) o con el vínculo ilegible. Devuelve true si es de una unidad registrada
// (admitida, sería admitida, o del anfitrión): en modo 1 entonces NADA más decide su controlador.
bool family_child(uintptr_t& rax, uintptr_t desc, uintptr_t field, uintptr_t actor, const npcfam::LinkRead& link,
                  FamBirth& fb) noexcept {
    const uint32_t key = rdv<uint32_t>(actor + u2s::kActor_Key);
    if ((key >> 28) != 0xB) return false;
    const bool on = family_mode() == npcact::Mode::On && ctl_mode_now() == u2s::Mode::On;
    const uint64_t t0 = p8_now_us();
    struct Cost { uint64_t t; ~Cost() { fam_cost(t); } } cost{t0};
    FamRead read;
    npcfam::Frame* f = nullptr;
    npcfam::Rec* r = nullptr;
    if (const npcfam::Hit h = fam_find(desc); h.rec >= 0) {
        f = &g_fam_pool.at(static_cast<size_t>(h.frame));
        r = &f->r[h.rec];
        if (r->birth) {  // el mismo descriptor otra vez en este Troc: no se escribe nada
            g_fam_desc_reborn.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }
    if (link.state == npcfam::Link::Unknown) {
        g_fam_link_unknown.fetch_add(1, std::memory_order_relaxed);
        if (!r) return false;
    }
    npcfam::Frame* pf = nullptr;
    npcfam::Rec* pr = nullptr;
    bool late = false;
    if (r) {
        pf = f;
        if (r->parent != npcfam::kNoRec) pr = &f->r[r->parent];
    } else {
        const npcfam::Hit ph = fam_find(link.parent);
        if (ph.rec < 0) {
            // Su padre no está en ningún marco: nace con su controlador nativo. Se apunta como huérfano para que su
            // raíz, si llega después en este Troc, no se admita (ChildBornFirst). Los viajeros (padre base) se
            // cuentan aparte (fase C).
            const npcfam::OrphanCls oc = fam_orphan_count(link.parent);
            fam_orphan_line(desc, actor, key, link.parent, 0, 2, oc);
            if (npcfam::Frame* top = fam_frame(true)) {
                if (npcfam::Rec* o = npcfam::add_rec(*top, desc, npcfam::Kind::Orphan, npcfam::kNoIndex,
                                                     npcfam::kNoRec, 0, npcfam::Cls::Unknown)) {
                    if (!fb.birth) fb.birth = next_birth();
                    o->actor = actor; o->key = key; o->birth = fb.birth; o->oc = oc;
                }
            }
            return false;
        }
        pf = &g_fam_pool.at(static_cast<size_t>(ph.frame));
        pr = &pf->r[ph.rec];
        late = true;
        // Prueba en dos copias 3: una raíz sin hijos registrada sólo con su registro recibe su unidad ahora, con su
        // primer hijo tardío. Sin unidad libre: en modo 1 la raíz ya es del usuario 2 y este hijo nacería con el
        // anfitrión: la unidad queda PARTIDA (se retira entera); en probe se cuenta.
        if (npcfam::lazy_root(*pr)) {
            const uintptr_t pj2 = g_u2_char.load(std::memory_order_relaxed);
            const uint32_t key2 = pj2 ? rdv<uint32_t>(pj2 + u2s::kActor_Ctl) : 0;
            const uintptr_t h = rdp(pr->desc + 0x4E8);
            const uint64_t node = h ? static_cast<uint64_t>(rdv<uint32_t>(h)) | static_cast<uint64_t>(rdv<uint32_t>(h + 4)) << 32
                                    : 0;
            const bool host = pr->kind == npcfam::Kind::HostRoot;
            if (npcfam::promote_root(*pf, *pr, on, key2, node) != npcfam::kNoIndex) {
                g_fam_promoted.fetch_add(1, std::memory_order_relaxed);
            } else {
                g_fam_promote_full.fetch_add(1, std::memory_order_relaxed);
                g_fam_late_split.fetch_add(1, std::memory_order_relaxed);
                g_fam_split_why[static_cast<size_t>(npcfam::SplitWhy::LateNoRoom)].fetch_add(1, std::memory_order_relaxed);
                if (!fb.birth) fb.birth = next_birth();
                if (on && !host) {
                    fb.track = false;
                    fb.split = true; fb.unit_key = pr->key; fb.unit_birth = pr->birth;
                    fb.why = npcfam::SplitWhy::LateNoRoom;
                }
                return true;  // modo 1: nada más decide su controlador (el nativo)
            }
        }
    }
    if (!pr || pr->unit == npcfam::kNoIndex || pr->unit >= pf->nu) {
        // Hijo de un huérfano (o de un registro sin unidad): del anfitrión; también huérfano para su posible raíz.
        // Prueba en dos copias 3: el descendiente de un viajero es viajero (los 19 tipo 7 bajo el gimmick 0xB01002A9
        // de la carreta 0xB01002A6, carretera 0xFA1, contaban como «padre InstantGimmick»).
        const npcfam::OrphanCls oc = pr && pr->kind == npcfam::Kind::Orphan && pr->oc == npcfam::OrphanCls::Traveller
                                         ? fam_orphan_count_cls(npcfam::OrphanCls::Traveller)
                                         : fam_orphan_count(pr ? pr->desc : link.parent);
        if (late) {
            if (npcfam::Rec* o = npcfam::add_rec(*pf, desc, npcfam::Kind::Orphan, npcfam::kNoIndex, npcfam::kNoRec,
                                                 0, npcfam::Cls::Unknown)) {
                if (!fb.birth) fb.birth = next_birth();
                o->actor = actor; o->key = key; o->birth = fb.birth; o->oc = oc;
            }
        }
        return late;
    }
    npcfam::UnitSum& u = pf->u[pr->unit];
    const bool live = u.admitted || u.would;
    const famveh::Mode vm = family_vehicles();
    const bool vehicles = vm != famveh::Mode::Off && famveh::scope(u.root_node);
    const bool project = vehicles && famveh::projected_root(u);
    if (project && pr->parent == npcfam::kNoRec) {
        pr->projected = static_cast<uint8_t>(famveh::Projected::Assigned);
    }
    const uint16_t parent_index = static_cast<uint16_t>(pr - pf->r);
    npcfam::Cls cls = r ? r->cls : npcfam::Cls::Unknown;
    int in_vec = 1;
    uint32_t wrap = 0, inner = 0;
    npcfam::Cls shadow_cls = cls;
    if ((late && live) || vehicles) {
        in_vec = npcfam::in_vector(read, pr->desc, desc, g_base, g_image_size, &wrap, &inner);
        shadow_cls = npcfam::classify(wrap, inner, rva_of(rdp(pr->desc)));
        cls = famveh::actual_class(cls, shadow_cls, late, live); // las lecturas extra sólo alimentan sombra
    }
    if (late && u.admit == npcfam::Admit::Childless) {
        g_fam_childless_late.fetch_add(1, std::memory_order_relaxed);
        fam_orphan_line(desc, actor, key, pr->desc, 1, late && live ? in_vec : 2);
    }
    // Lo que se comprueba en SU nacimiento (AUDITORIA-2 cambios 4 y 6): vínculo releído, padre vivo con el controlador
    // de la unidad, las condiciones de U-e salvo el tipo, y su tipo dentro de la cohorte para su clase.
    npcfam::MemberIn m;
    m.admitted = live;
    m.cls = cls;
    const uintptr_t ti = rdp(actor + u2s::kActor_TypeInfo);
    m.type = ti ? rdv<uint8_t>(ti + 1) : 0;
    m.types.mask = g_family_types.load(std::memory_order_relaxed);
    m.link = link.state;
    m.link_matches = link.state == npcfam::Link::Present && link.parent == pr->desc;
    m.parent_alive = pr->birth && pr->actor ? npcfam::read_matches(read, pr->actor + npcfam::kActor_Key, pr->key) : 0;
    // El controlador de la unidad en su padre: en modo 1 siempre; en probe sólo si el padre es la raíz y U-e está a 1
    // (U-e de siempre la asignó); un miembro en probe nunca se escribió, así que ahí no hay nada que comprobar.
    const bool parent_written = on || (pr->kind == npcfam::Kind::Root && ctl_mode_now() == u2s::Mode::On);
    m.parent_ctl = !parent_written ? 1
                   : pr->actor     ? npcfam::read_matches(read, pr->actor + npcfam::kActor_Ctl, u.controller)
                                   : 0;
    m.old_ctl_known = rd(actor + u2s::kActor_Ctl, m.old_ctl);
    m.desc_user_known = rd(desc + u2s::kDesc_User, m.desc_user);
    m.actor_user_known = rd(actor + u2s::kActor_User, m.actor_user);
    m.host_field = host_owns(field);
    const uint32_t eax = static_cast<uint32_t>(rax);
    m.owner_branch = eax && eax == rdv<uint32_t>(field + u2s::kField_Owner);
    const uintptr_t pj2 = g_u2_char.load(std::memory_order_relaxed);
    m.key2 = pj2 ? rdv<uint32_t>(pj2 + u2s::kActor_Ctl) : 0;
    const uint32_t sk = server_user2_user_key();
    m.key_ok = (!sk || sk == m.key2) && (!u.controller || u.controller == m.key2);
    // Segunda revisión (R2): la pausa de la degradación no parte unidades ya admitidas (sólo bloquea raíces nuevas).
    m.stopped = npcfam::member_stopped(g_ctl_aborted.load(std::memory_order_relaxed),
                                       npcact::assignments_paused(degrade_now()));
    famveh::In vi;
    famveh::Out simulated;
    if (vehicles) {
        vi.member = m; vi.member.cls = shadow_cls; vi.vt = inner; vi.wrap = wrap; vi.in_vector = in_vec;
        vi.parent_type = pr->type;
        vi.depth = static_cast<uint8_t>(pr->depth + 1);
        uint32_t parent_key = 0;
        vi.parent_key_matches = inner == npcfam::kVtDescBase && wrap == npcfam::kVtWrapLinkedVehicle &&
                                rd(desc + 0x490, parent_key) && parent_key == pr->key;
        // Una prueba positiva de ocupación impide conceder autoridad. El estado ilegible al NACER sólo se mide;
        // no se exige un componente de asientos que puede no estar construido todavía.
        vi.player_rides = m.type == 5 && road_ride(key, 0, false, false) == road::Ride::Rides;
        vi.member.admitted = project;
        const auto pp = static_cast<famveh::Projected>(pr->projected);
        vi.parent_kept = pp == famveh::Projected::Kept;
        vi.member.parent_ctl = pp == famveh::Projected::Assigned ? 1 : 0;
        simulated = famveh::decide(vi);
    }
    npcfam::MemberOut o;
    if (late) {
        o = npcfam::late_verdict(in_vec, true, m);
        // Un tardío necesita su propio sitio (marco + reservas), tomado ahora o nada: su unidad queda partida.
        bool room = live && npcfam::rec_room(*pf, 1) && 2u + u.nodes + u.late <= npcfam::kMaxUnit;
        bool reserved_now = false;
        const bool needs_key = o.v == npcfam::MemberVerdict::Assign;
        if (room && (o.v == npcfam::MemberVerdict::Assign || o.v == npcfam::MemberVerdict::HostKept)) {
            if (on && u.admitted) {
                bool track_ok = false;
                {
                    ActivationLock guard;
                    g_fam_mid_locks.fetch_add(1, std::memory_order_relaxed);
                    track_ok = g_activation.reserve(1);
                }
                const bool keys_ok = !needs_key || (track_ok && g_ctl_keys.reserve(1));
                if (track_ok && !keys_ok) {
                    ActivationLock guard;
                    g_activation.unreserve(1);
                }
                room = track_ok && keys_ok;
                if (room) {
                    ++u.track_reserved;
                    if (needs_key) ++u.keys_reserved;
                    reserved_now = true;
                }
            } else {
                ActivationRead guard;
                room = g_activation.can_reserve(1) && (!needs_key || g_ctl_keys.can_reserve(1));
            }
        }
        if (!room && (o.v == npcfam::MemberVerdict::Assign || o.v == npcfam::MemberVerdict::HostKept)) {
            o.v = npcfam::MemberVerdict::Split;
            o.why = npcfam::SplitWhy::LateNoRoom;
        }
        r = npcfam::add_rec(*pf, desc, npcfam::Kind::Reserved, pr->unit, parent_index,
                            static_cast<uint8_t>(pr->depth + 1), cls);
        fb.reserved = reserved_now;
        if (!r) {  // ni siquiera su registro: contado en lost; la unidad igualmente queda partida
            if (live) {
                if (u.split < 0xFF) ++u.split;
                u.split_mask = static_cast<uint16_t>(u.split_mask | 1u << static_cast<uint8_t>(npcfam::SplitWhy::LateNoRoom));
                g_fam_late_split.fetch_add(1, std::memory_order_relaxed);
                g_fam_split_why[static_cast<size_t>(npcfam::SplitWhy::LateNoRoom)].fetch_add(1, std::memory_order_relaxed);
            }
            if (on && u.admitted) {
                fb.track = false;  // sin registro en el marco no se reserva nada; la publicación lo verá como tardío
                // El hijo nace con el anfitrión: la unidad está partida; npc_observe_birth no lo registra, pero la
                // raíz sí se marca.
                fb.split = true; fb.unit_key = u.key; fb.unit_birth = u.birth; fb.why = npcfam::SplitWhy::LateNoRoom;
            }
            return true;
        }
    } else {
        o = npcfam::member_verdict(m);
        fb.reserved = on && u.admitted;  // su registro se reservó con la raíz
    }
    if (!fb.birth) fb.birth = next_birth();
    const uint64_t now = p8_now_us();
    if (on && u.admitted && o.v == npcfam::MemberVerdict::Assign) {
        // Registrada primero (consume su reserva: nunca falla por capacidad); si aun así no, no se asigna.
        ++u.keys_used;
        if (g_ctl_keys.put(key, u2s::kKeyAssigned | u2s::kKeyFamily | u2s::kKeyMember, now, true, stage_slot_of(desc))) {
            rax = (rax & ~static_cast<uintptr_t>(0xFFFFFFFF)) | m.key2;  // eax: la clave del usuario 2, como su raíz
            g_ctl_key2.store(m.key2, std::memory_order_relaxed);
            // Una raíz registrada sin hijos (unidad de 0 nodos) pasa a ser de una unidad con hijos.
            if (u.admit == npcfam::Admit::Childless) g_ctl_keys.mark(u.key, u2s::kKeyFamily, now);
        } else {
            o.v = npcfam::MemberVerdict::Split;
            o.why = npcfam::SplitWhy::PutFailed;
        }
    }
    npcfam::born(*pf, *r, actor, key, fb.birth, o, late);
    r->type = m.type;
    if (vehicles) {
        r->projected = static_cast<uint8_t>(famveh::projection(simulated.member));
        if (g_family_vehicle_lines.fetch_add(1, std::memory_order_relaxed) < 1024) {
            p::Event e;
            e.kind = kNpcFamVehiclesBirth; e.qpc_us = now; e.tid = GetCurrentThreadId();
            e.flags = static_cast<uint16_t>(vm);
            e.v[0] = key; e.v[1] = pr->key; e.v[2] = u.key; e.v[3] = u.birth;
            e.v[4] = m.type; e.v[5] = static_cast<uint8_t>(simulated.kind);
            e.v[6] = inner; e.v[7] = wrap; e.v[8] = static_cast<uint8_t>(o.v);
            e.v[9] = static_cast<uint8_t>(simulated.member.v); e.v[10] = static_cast<uint8_t>(simulated.member.why);
            g_activation_events.push(e);
        }
    }
    fb.seen = true;  // nacido en el marco de su unidad: npc_observe_birth lo anota (R5)
    g_fam_member[static_cast<size_t>(o.v)].fetch_add(1, std::memory_order_relaxed);
    if (o.v == npcfam::MemberVerdict::Split) g_fam_split_why[static_cast<size_t>(o.why)].fetch_add(1, std::memory_order_relaxed);
    if (late && live) (o.v == npcfam::MemberVerdict::Split ? g_fam_late_split : g_fam_late).fetch_add(1, std::memory_order_relaxed);
    if (o.v == npcfam::MemberVerdict::Split) fam_split_event(*r, u, pr->key, m.type, late, !on);
    if (on && u.admitted) {
        // Registrado en g_activation (también host-kept y partidos: la retirada es por unidad), con su reserva si la
        // tiene; un tardío sin sitio se registra sin ella (puede no caber: la publicación lo verá).
        if (fb.reserved) ++u.track_used;
        fb.track = true; fb.fam = npcact::kFamMember; fb.depth = r->depth;
        fb.parent = pr->key; fb.parent_birth = pr->birth; fb.unit_key = u.key; fb.unit_birth = u.birth;
        fb.host_kept = o.v == npcfam::MemberVerdict::HostKept;
        fb.split = o.v == npcfam::MemberVerdict::Split;
        fb.why = o.why;
    }
    return true;
}

// At the server's controller write 0x2792A7B (docked_gimmicks' mid hook): every actor creation of the process passes
// here; only those under user 2's subject read anything. Guarded reads, a register, atomics and the ring: no call.
// S36: and, with npc_family_control, those whose descriptor names a parent while a family frame is open.
void npc_control_impl(uintptr_t& rax, uintptr_t desc, uintptr_t field, uintptr_t actor, FamBirth& fb) noexcept {
    const u2s::Mode cm = ctl_mode_now();
    if (cm == u2s::Mode::Off) return;
    uint16_t op = 0;
    const bool su2 = subject_user2(&op);
    const npcact::Mode fm = family_mode();
    npcfam::Link root_link = npcfam::Link::Unknown;  // revisión: sólo un +0x2F0 leído a 0 es raíz para las familias
    if (fm != npcact::Mode::Off && (su2 || fam_any_frame())) {
        // Clasificación por parentesco (AUDITORIA.md §2: `not type3` no identifica hijos de stage): con padre, o con
        // el vínculo ilegible, no se usa su propio veredicto de U-e en modo 1. En probe se mide y sigue como antes.
        const npcfam::LinkRead link = npcfam::read_parent(FamRead{}, desc);
        root_link = link.state;
        fb.link = link.state;
        fb.link_read = true;
        if (link.state != npcfam::Link::Absent) {
            family_child(rax, desc, field, actor, link, fb);
            if (fm == npcact::Mode::On) return;  // su controlador es el de su unidad o el nativo, nunca el suyo propio
        }
    }
    if (!su2) return;
    u2s::CtlIn in;
    in.opcode = op;
    in.own_mgr_troc = own_mgr_troc();
    in.aborted = g_ctl_aborted.load(std::memory_order_relaxed);
    // S35 (npc_degradation=1, npc_activation=1): una contención que no puede actuar no rearma el aborto global; los
    // NPC NUEVOS dejan de asignarse (fail-closed) mientras dure. Sólo átomos: esto corre en el mid hook 0x2792A7B.
    // Revisión S35 (hallazgo 12): se cuentan sólo los nacimientos que U-e habría asignado (veredicto Assign), no cada
    // creación bajo el sujeto del usuario 2.
    const bool paused = !in.aborted && npcact::assignments_paused(degrade_now());
    in.armed = u1_armed();
    const uintptr_t vt = rdp(desc);
    in.desc_npc_schedule = vt && vt == g_base + u2s::kVtDescNpcSchedule;
    in.old_ctl = rdv<uint32_t>(actor + u2s::kActor_Ctl, 0xFFFFFFFFu);
    in.desc_user = rdv<uint64_t>(desc + u2s::kDesc_User, 1);
    in.parent = rdv<uint64_t>(actor + u2s::kActor_User, 1);
    in.eax = static_cast<uint32_t>(rax);
    in.field_owner = rdv<uint32_t>(field + u2s::kField_Owner);
    in.host_field = host_owns(field);
    const uintptr_t ti = rdp(actor + u2s::kActor_TypeInfo);
    in.type = ti ? rdv<uint8_t>(ti + 1) : 0;
    const uintptr_t pj2 = g_u2_char.load(std::memory_order_relaxed);
    in.key2 = pj2 ? rdv<uint32_t>(pj2 + u2s::kActor_Ctl) : 0;
    in.session_key2 = server_user2_user_key();
    u2s::Ctl v = u2s::ctl_verdict(in);
    if (paused && v == u2s::Ctl::Assign) {
        g_assign_paused.fetch_add(1, std::memory_order_relaxed);
        in.aborted = true;
        v = u2s::ctl_verdict(in);  // el veredicto de U-e abortado: nada se escribe
    }
    const uint32_t key = rdv<uint32_t>(actor + u2s::kActor_Key);
    // S36: una raíz con hijos dentro del ámbito de U-e (su Troc, armado, sin abortar) que U-e no asignaría también
    // se registra: sus hijos se reconocen como de una unidad del anfitrión (AUDITORIA-2 cambio 5).
    const bool fam_scope = fm != npcact::Mode::Off && root_link == npcfam::Link::Absent && (key >> 28) == 0xB &&
                           v != u2s::Ctl::NotTimer &&
                           v != u2s::Ctl::NotOwnTroc && v != u2s::Ctl::Aborted && v != u2s::Ctl::NotArmed;
    if (fam_scope && v != u2s::Ctl::Assign) {
        const uint64_t t0 = p8_now_us();
        fam_host_root(desc, actor, key, fb, npcfam::Admit::Count, static_cast<uint8_t>(v));
        fam_cost(t0);
    }
    const bool write = cm == u2s::Mode::On;
    // S36 (AUDITORIA-2 cambio 1): la unidad entera se reserva ANTES de cambiar EAX. Modo 1: si no se admite, la raíz
    // tampoco se asigna (toda la unidad se queda con el anfitrión, coherente). probe: se cuenta y U-e sigue como antes.
    bool fam_unit = false;          // modo 1: la raíz consume una reserva de su unidad
    bool fam_childless = false;     // revisión: raíz sin hijos en el preexamen (unidad de 0 nodos)
    npcfam::UnitSum* fam_sum = nullptr;
    npcfam::Frame* fam_f = nullptr;
    npcfam::Rec* fam_rec = nullptr;  // raíz sin hijos: sólo su registro (sin unidad hasta su primer tardío)
    if (fam_scope && v == u2s::Ctl::Assign) {
        const uint64_t t0 = p8_now_us();
        FamRead read;
        npcfam::Scan scan;  // ~1 KB en la pila del mid; sin inicializar los nodos
        npcfam::scan_subtree(read, desc, g_base, g_image_size, scan);
        g_fam_scan[static_cast<size_t>(scan.v)].fetch_add(1, std::memory_order_relaxed);
        fam_childless = scan.v == npcfam::ScanVerdict::Ok && !scan.n;
        {
            LastRoot& lr = t_last_root;  // diagnóstico de huérfanos (fam_orphan_line)
            lr.desc = desc;
            lr.key = key;
            lr.count = rdv<uint32_t>(desc + npcfam::kDesc_ChildCount, 0xFFFFFFFFu);
        }
        if (scan.v != npcfam::ScanVerdict::Ok) {
            // Prueba en dos copias 2: por envoltorio (contador y unas líneas por clase), para saber QUÉ rechaza.
            const bool wrapped =
                scan.v == npcfam::ScanVerdict::Excluded || scan.v == npcfam::ScanVerdict::UnknownWrapper;
            const size_t slot = wrapped ? static_cast<size_t>(npcfam::wrap_of(scan.bad_wrap)) : kScanSlots - 1;
            g_fam_scan_wrap[slot].fetch_add(1, std::memory_order_relaxed);
            if (g_fam_scan_cap.take(slot, kFamScanFailLines)) {
                const uintptr_t h = rdp(desc + 0x4E8);
                p::Event e;
                e.kind = kNpcFamScanFail; e.qpc_us = p8_now_us(); e.tid = GetCurrentThreadId();
                e.flags = !(fm == npcact::Mode::On && write);
                e.v[0] = key; e.v[1] = in.type; e.v[2] = static_cast<uint64_t>(scan.v); e.v[3] = scan.bad_wrap;
                e.v[4] = scan.bad_inner; e.v[5] = scan.bad_parent; e.v[6] = scan.bad_depth; e.v[7] = t_last_root.count;
                e.v[8] = scan.n; e.v[9] = h ? rdv<uint32_t>(h) : 0;
                g_activation_events.push(e);
            }
        }
        if (fam_childless) {
            g_fam_admit[static_cast<size_t>(npcfam::Admit::Childless)].fetch_add(1, std::memory_order_relaxed);
            // Prueba en dos copias (15-10): sus hijos pueden nacer después en este Troc con +0x2F0 = este descriptor;
            // registrada como unidad de 0 nodos (sin reservas), esos hijos siguen la política de tardíos en vez de
            // nacer huérfanos. Modo 1: admitida (cada tardío reserva lo suyo o la parte); probe: «se admitiría».
            // Prueba en dos copias 3: sólo su registro; la unidad (UnitSum) se crea con su primer hijo tardío
            // (npcfam::promote_root en family_child): 157 raíces de un mismo Troc ya no agotan las unidades.
            npcfam::Frame* f = fam_frame(true);
            if (!fb.birth) fb.birth = next_birth();
            fam_rec = f && npcfam::rec_room(*f, 1) ? npcfam::add_childless_root(*f, desc, actor, key, fb.birth) : nullptr;
            if (fam_rec) {
                g_fam_childless_reg.fetch_add(1, std::memory_order_relaxed);
            } else {
                g_fam_childless_lost.fetch_add(1, std::memory_order_relaxed);  // sin registro: sus tardíos, huérfanos
            }
        }
        if (scan.v != npcfam::ScanVerdict::Ok || scan.n) {
            const npcfam::Types types{g_family_types.load(std::memory_order_relaxed), false};
            const bool assignable7 = npcfam::type_in(types, 7);
            const bool probe = !(fm == npcact::Mode::On && write);
            const uintptr_t h = rdp(desc + 0x4E8);
            const npcact::Node node = fam_node(h ? rdv<uint32_t>(h) : 0, h ? rdv<uint32_t>(h + 4) : 0);
            npcfam::AdmitIn ai;
            ai.scan = scan.v;
            ai.nodes = scan.n;
            ai.gimmicks = scan.gimmick;
            ai.gimmicks_assignable = assignable7;
            ai.gate_on = child_hold_publication_gate() == 2;
            // Prueba en dos copias 2: también con el preexamen fallido se toma el marco, para registrar la raíz como
            // unidad del anfitrión (add_host_root ya lo decía; antes sólo con el preexamen bien): sus hijos son
            // «de_unidad_del_anfitrión», no huérfanos. Mismo controlador para todos (el nativo) en probe y en 1.
            npcfam::Frame* f = fam_frame(true);
            ai.frame = f != nullptr || scan.v != npcfam::ScanVerdict::Ok;
            ai.frame_room = f && npcfam::frame_room(*f, 1u + scan.n);
            ai.child_born_first = scan.v == npcfam::ScanVerdict::Ok && fam_born_first(scan);
            const size_t nk = 1u + scan.schedule + (assignable7 ? scan.gimmick : 0u), nt = 1u + scan.n;
            npcfam::Admit adm = npcfam::Admit::ScanFailed;
            if (probe) {
                // Segunda revisión (R8): probe sólo lee (can_reserve, la memoria de nodos partidos): cerrojo compartido.
                ActivationRead guard;
                g_fam_mid_locks.fetch_add(1, std::memory_order_relaxed);
                ai.node_split_before = g_fam_split_nodes.contains(node);
                adm = npcfam::reserve_unit(ai, nk, nt, g_ctl_keys, g_activation, true);
            } else {
                ActivationLock guard;
                g_fam_mid_locks.fetch_add(1, std::memory_order_relaxed);
                ai.node_split_before = g_fam_split_nodes.contains(node);
                adm = npcfam::reserve_unit(ai, nk, nt, g_ctl_keys, g_activation, false);
            }
            g_fam_admit[static_cast<size_t>(adm)].fetch_add(1, std::memory_order_relaxed);
            const bool admitted = adm == npcfam::Admit::Admit && !probe;
            const bool would = adm == npcfam::Admit::Admit && probe;
            if (!fb.birth) fb.birth = next_birth();
            if (f && scan.v == npcfam::ScanVerdict::Ok && npcfam::frame_room(*f, 1u + scan.n)) {
                const uint8_t ui = npcfam::add_unit(*f, desc, actor, key, fb.birth, scan, adm, admitted, would, in.key2);
                fam_sum = &f->u[ui];
                fam_f = f;
                fam_sum->root_node = static_cast<uint64_t>(node.data) | static_cast<uint64_t>(node.index) << 32;
                if (admitted) {
                    fam_sum->keys_reserved = static_cast<uint8_t>(nk);
                    fam_sum->track_reserved = static_cast<uint8_t>(nt);
                }
            } else if (admitted) {
                // No debería ocurrir (frame_room ya se comprobó): sin registro no hay unidad; devolver y no asignar.
                g_ctl_keys.unreserve(nk);
                ActivationLock guard;
                g_activation.unreserve(nt);
                adm = npcfam::Admit::FrameFull;
            } else if (f && npcfam::frame_room(*f, 1)) {
                const uint32_t direct = t_last_root.count;  // +0x300 en este mid (0xFFFFFFFF ilegible)
                const uint8_t children = scan.v == npcfam::ScanVerdict::Ok
                                             ? scan.n
                                             : static_cast<uint8_t>(direct > 0xFF ? 0xFF : direct);
                npcfam::add_host_root(*f, desc, actor, key, fb.birth, adm, 0xFF, children);
                g_fam_host_roots.fetch_add(1, std::memory_order_relaxed);
            }
            if ((admitted || would) && scan.group) {
                g_fam_group_units.fetch_add(1, std::memory_order_relaxed);
                g_fam_group_nodes.fetch_add(scan.group, std::memory_order_relaxed);
            }
            fam_unit = admitted && fam_sum;
            if (would && fam_sum) {
                fb.probe_root = true;
                fb.unit_key = key;
                fb.unit_birth = fb.birth;
            }
            fam_cost(t0);
            if (!fam_unit && !probe) {
                v = u2s::Ctl::Family;  // modo 1: la unidad no se admitió entera; nada se escribe
            }
        }
    }
    g_ctl_verdicts[static_cast<size_t>(v)].fetch_add(1, std::memory_order_relaxed);
    if (v == u2s::Ctl::OtherDesc) g_ctl_descs.add(static_cast<uint64_t>(rva_of(vt)) | static_cast<uint64_t>(in.type) << 32 | 1ull << 48);
    if (v != u2s::Ctl::Assign) return;
    // Review 4: tracked first; a key the table cannot hold is not given to user 2 (nothing could clean it up).
    // S36: la raíz de una unidad admitida consume su reserva (no puede fallar por capacidad).
    const uint16_t kind = write ? (fam_unit ? u2s::kKeyAssigned | u2s::kKeyFamily : u2s::kKeyAssigned) : u2s::kKeyWould;
    if (fam_unit) ++fam_sum->keys_used;
    // Review 6, the line: the descriptor's schedule handle ([desc+0x4E8] -> u32 +0, +4: the schedule data id and the
    // node [H], as the [create] lines' data) and the live stage of that data in user 2's / the host's schedule manager.
    // S43: read before the put, it is the key's stage slot too (Assign: a CreateServerActorDesc_NPCSchedule).
    const uintptr_t h = rdp(desc + 0x4E8);
    const uint32_t data = h ? rdv<uint32_t>(h) : 0;
    const uint32_t node = h ? rdv<uint32_t>(h + 4) : 0;
    if (!g_ctl_keys.put(key, kind, p8_now_us(), fam_unit, u2s::r42_slot_id(data, node))) {
        g_ctl_untracked.fetch_add(1, std::memory_order_relaxed);
        if (fam_sum) {
            // La raíz no se asigna: su unidad deja de estar admitida (sus hijos nacen con el anfitrión, coherente);
            // las reservas se devuelven al cerrar el marco.
            fam_sum->admitted = false;
            fam_sum->admit = npcfam::Admit::KeysFull;
            fam_sum->controller = 0;
            fam_f->r[fam_sum->root_rec].kind = npcfam::Kind::HostRoot;
        }
        if (fam_rec) fam_rec->kind = npcfam::Kind::HostRoot;  // su unidad, si llega, será del anfitrión
        return;
    }
    if (fam_unit) {
        ++fam_sum->track_used;
        fb.track = true; fb.reserved = true; fb.fam = npcact::kFamRoot; fb.depth = 0;
        fb.unit_key = key; fb.unit_birth = fb.birth;
    } else if (fam_childless) {
        // Revisión S36: también la raíz sin hijos lleva su marca de unidad (sin reserva: se registra como siempre), para
        // que un hijo que aparezca después, fuera de todo preexamen, se reconozca (modo 1: parte la unidad; probe: se
        // cuenta). Sin efecto en la contención.
        if (!fb.birth) fb.birth = next_birth();
        if (fm == npcact::Mode::On && write) {
            fb.track = true; fb.reserved = false; fb.fam = npcact::kFamRoot; fb.depth = 0;
        } else {
            fb.probe_root = true;
        }
        fb.unit_key = key; fb.unit_birth = fb.birth;
    }
    if (write) {
        rax = (rax & ~static_cast<uintptr_t>(0xFFFFFFFF)) | in.key2;  // eax: mov [r14+0x90],eax writes user 2's key
        g_ctl_key2.store(in.key2, std::memory_order_relaxed);
    }
    p::Event e;
    e.v[6] = data | static_cast<uint64_t>(node) << 32;
    e.v[9] = data ? stage_of_data(g_u2_sm.load(std::memory_order_relaxed), data) : 0;
    e.v[10] = data ? stage_of_data(g_host_sm.load(std::memory_order_relaxed), data) : 0;
    e.v[7] = in.own_mgr_troc;
    e.v[0] = key;
    e.v[1] = in.key2;
    e.v[2] = in.eax;
    e.v[3] = in.type;
    e.v[4] = op;
    e.v[5] = write;
    e.v[8] = static_cast<uint64_t>(Who::User2);
    emit(kENpcCtl, e, true);
}

// El punto 0x2792A7B ya instalado; no introduce otro gancho ni cambia el registro. Es un mid hook:
// ningún Actor temporal ni copia agregada (memset/memcpy del CRT); el registro se llena campo a campo.
// Revisión: sólo se registran los candidatos de U-e (asignados a user 2 o que lo serían) y los hijos
// de un registrado; el resto sólo se cuenta. Un hijo cuyo vínculo aún no está se añade al publicarse.
// S36, bajo g_activation_lock: la unidad (raíz por clave y nacimiento exactos) quedó partida: se retira entera
// (npcact::containment_due), y su nodo no vuelve a admitirse en esta época/generación (Admit::NodeSplitBefore).
bool fam_mark_split_locked(uint32_t unit_key, uint64_t unit_birth) noexcept {
    npcact::Actor* r = g_activation.find(unit_key);
    if (!r || r->birth != unit_birth || r->removed || r->family_split) return false;
    r->family_split = true;
    npcact::bump(*r);
    g_fam_split_marked.fetch_add(1, std::memory_order_relaxed);
    npcact::Node n = r->node;
    n.stage = 0;
    g_fam_split_nodes.insert(n);
    return true;
}
// S36: con npc_family_control=1 también los miembros de una unidad admitida (host-kept y partidos incluidos), en su
// nacimiento, con el vínculo del mid (padre y su nacimiento, unidad, profundidad) y consumiendo su reserva; un
// miembro partido marca a su raíz.
void npc_observe_birth(uintptr_t rax, uintptr_t desc, uintptr_t actor, const FamBirth& fb) noexcept {
    if (!npc_probes()) return;
    const uint32_t key = rdv<uint32_t>(actor + u2s::kActor_Key);
    if ((key >> 28) != 0xB) return;
    const uint64_t birth = fb.birth ? fb.birth : g_birth_serial.fetch_add(1, std::memory_order_relaxed) + 1;
    const uint32_t epoch = server_epoch();
    // Segunda revisión (R4/R5): cada nacimiento reescribe las marcas de su clave (una clave reutilizada no hereda las de
    // su encarnación anterior). Una lectura protegida de +0x2F0 si el camino de familias del mid no la hizo ya.
    npcfam::Link lineage = npcfam::Link::Unknown;
    if (family_mode() != npcact::Mode::Off) {
        lineage = fb.link_read ? fb.link : npcfam::read_parent(FamRead{}, desc).state;
        if (fb.seen) g_fam_seen.insert(key, epoch);
        else g_fam_seen.erase(key);
        if (lineage == npcfam::Link::Present) g_fam_lineage.insert(key, epoch);
        else g_fam_lineage.erase(key);
    }
    // Una clave retirada puede renacer sin interés para las sondas: su lápida se levanta igualmente, fuera de este
    // mid hook (child_hold_settle_births: purgar la cola usa vector/memcpy del CRT).
    if (activation_on()) child_hold_actor_born(epoch, birth, key);
    const uint32_t controller = static_cast<uint32_t>(rax);
    const uint16_t flags = g_ctl_keys.flags(key);
    const bool candidate = (flags & u2s::kKeyAssigned) && !(flags & u2s::kKeyRemoved);
    const uint32_t parent = fb.track && fb.parent ? fb.parent : child_hold_actor_parent(actor).value_or(0);
    if (fb.split && fb.unit_key) {
        // Revisión S36: la unidad partida se marca ANTES de registrar al miembro (y aunque no quepa en la tabla): su
        // registro puede fallar, la marca no puede depender de él.
        ActivationLock guard;
        fam_mark_split_locked(fb.unit_key, fb.unit_birth);
    }
    if (!candidate && !fb.track && !(parent && npc_tracked(parent))) {
        g_activation_untracked.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const uintptr_t ti = rdp(actor + u2s::kActor_TypeInfo);
    const uint8_t type = ti ? rdv<uint8_t>(ti + 1) : 0;
    const uint32_t descriptor = rva_of(rdp(desc));
    uint32_t data = 0, index = 0;
    if (descriptor == u2s::kVtDescNpcSchedule) {
        if (const uintptr_t handle = rdp(desc + 0x4E8)) { data = rdv<uint32_t>(handle); index = rdv<uint32_t>(handle + 4); }
    }
    const uint64_t generation = g_arm_done_us.load(std::memory_order_relaxed), now = p8_now_us();
    const bool assigned = candidate && controller == g_ctl_key2.load();
    ActivationLock guard;
    if (fb.track) g_fam_mid_locks.fetch_add(1, std::memory_order_relaxed);
    if (!fb.track && parent && family_mode() == npcact::Mode::On) {
        // Revisión S36: un hijo nacido FUERA del marco de su unidad cuyo padre ya se ve al nacer (otro Troc, marco
        // cerrado): nunca se asignó; con otro controlador que su raíz (salvo un tipo 7 fuera de la cohorte) la unidad
        // está partida. La publicación ya no lo verá «nuevo», así que se comprueba aquí.
        // Segunda revisión (R4): sólo si es de la familia de DESCRIPTOR (+0x2F0 presente); un hijo sólo de jerarquía
        // (montura, vehículo, accesorio) se cuenta y se registra, nunca parte la unidad.
        if (const npcact::Actor* p = g_activation.find(parent);
            p && !p->removed && (p->fam == npcact::kFamRoot || npcact::family_member(*p))) {
            const bool root = p->fam == npcact::kFamRoot;
            const uint32_t unit_key = root ? p->key : p->unit_key;
            const uint64_t unit_birth = root ? p->birth : p->unit_birth;
            const npcact::Actor* r = g_activation.find(unit_key);
            const bool keep7 = type == 7 && !npcfam::type_in({g_family_types.load(), false}, 7);
            g_fam_pub_late.fetch_add(1, std::memory_order_relaxed);
            const npcfam::LateWhat w = npcfam::late_child(r && r->birth == unit_birth,
                                                          r && controller != r->controller, keep7, lineage);
            if (w == npcfam::LateWhat::Split && fam_mark_split_locked(unit_key, unit_birth)) {
                g_fam_late_split.fetch_add(1, std::memory_order_relaxed);
            } else if (w == npcfam::LateWhat::NotLineage) {
                g_fam_late_hier.fetch_add(1, std::memory_order_relaxed);
                p::Event e;
                e.kind = kNpcFamLate; e.qpc_us = now; e.tid = GetCurrentThreadId(); e.flags = 0;
                e.v[0] = key; e.v[2] = controller; e.v[3] = parent; e.v[4] = type; e.v[5] = unit_key;
                e.v[6] = unit_birth; e.v[7] = static_cast<uint64_t>(lineage); e.v[9] = 1;
                e.v[10] = static_cast<uint64_t>(w);
                g_activation_events.push(e);
            }
        }
    }
    npcact::Actor* a = g_activation.claim(key, birth, fb.reserved);
    if (!a) return;  // tabla llena: contado en overflow (con reserva: sólo una encarnación igual o más nueva)
    a->controller = controller; a->parent = parent; a->descriptor = descriptor; a->type = type; a->born_us = now;
    a->node.epoch = epoch; a->node.data = data; a->node.index = index; a->node.generation = generation;
    a->assigned = assigned;
    if (fb.track) {
        a->parent_birth = fb.parent_birth; a->unit_key = fb.unit_key; a->unit_birth = fb.unit_birth;
        a->fam = fb.fam; a->fam_depth = fb.depth; a->host_kept = fb.host_kept;
        if (fb.split) fam_mark_split_locked(fb.unit_key, fb.unit_birth);
    } else if (fb.probe_root) {
        // probe: sólo la marca de raíz (sin efecto en la contención: ni miembro ni partida) para medir tardíos.
        a->fam = npcact::kFamRoot; a->unit_key = fb.unit_key; a->unit_birth = fb.unit_birth;
    }
    npc_event(kNpcBorn, *a, descriptor);
}
// Identidad exacta: el actor está en la entrada del nodo de ESTA instancia de stage.
// Una búsqueda ambigua o incompleta no habilita ninguna escritura.
uint64_t npc_exact_stage(uint32_t key, uint32_t data, uint32_t node) noexcept {
    const uintptr_t sm = g_u2_sm.load(std::memory_order_relaxed);
    if (!sm || rdp(sm) != g_base + u2s::kVtSchedMgr) return 0;
    const uintptr_t list = rdp(sm + u2s::kSm_List);
    const uint32_t count = std::min<uint32_t>(rdv<uint32_t>(sm + u2s::kSm_Count), 512);
    uint64_t found = 0;
    for (uint32_t i = 0; list && i < count; ++i) {
        const uintptr_t stage = rdp(list + static_cast<uintptr_t>(i) * 8);
        const uintptr_t resource = stage ? rdp(stage + u2s::kSt_Data) : 0;
        if (!resource || rdv<uint32_t>(resource + u2s::kData_Id) != data) continue;
        const uintptr_t entries = rdp(stage + u2s::kSt_Entries);
        const uint32_t n = std::min<uint32_t>(rdv<uint32_t>(stage + u2s::kSt_EntryCount), 1024);
        for (uint32_t j = 0; entries && j < n; ++j) {
            const uintptr_t entry = entries + static_cast<uintptr_t>(j) * u2s::kSt_EntrySize;
            if (rdv<uint32_t>(entry + u2s::kEntry_Actor) != key) continue;
            const uint64_t candidate = rdv<uint64_t>(stage + u2s::kSt_Key);
            if (found && found != candidate) return 0;
            found = candidate;
        }
    }
    (void)node;  // El índice proviene del descriptor, no del índice del vector de entradas.
    return found;
}

// ---------------------------------------------------------------- U-f: the element filter

// *res = 0 (the caller's stack int; SEH in case it is not).
void put_zero(uintptr_t res) noexcept {
    __try {
        *reinterpret_cast<volatile uint32_t*>(res) = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void put_value(uintptr_t res, uint32_t v) noexcept {
    __try {
        *reinterpret_cast<volatile uint32_t*>(res) = v;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// U-f2: a schedule element whose stage the OTHER player's manager has alive is not created (both directions, while
// U-1 is armed in the host's field): *res = the native "no stage created" code, the original not called.
bool dup_skip(bool user2, uintptr_t res, uintptr_t elem, uint32_t stage_id) noexcept {
    float p[3] = {};
    if (!game::fast_read(elem + kElem_Pos, p, sizeof p)) return false;
    const uintptr_t other = (user2 ? g_host_sm : g_u2_sm).load(std::memory_order_relaxed);
    const uintptr_t st = stage_at(other, p, 0);
    if (!st) return false;
    (user2 ? g_dup_skip_u2 : g_dup_skip_host).fetch_add(1, std::memory_order_relaxed);
    p::Event e;
    e.v[0] = static_cast<uint64_t>(u2s::Element::Schedule);
    e.v[1] = 2;
    e.v[2] = stage_id;
    e.v[3] = fbits(p[0]);
    e.v[4] = fbits(p[2]);
    e.v[5] = rdv<uint32_t>(st + u2s::kSt_Key);
    e.v[8] = static_cast<uint64_t>(user2 ? Who::User2 : Who::Host);
    emit(kEDup, e, user2);
    put_value(res, rdv<uint32_t>(g_base + kNoStageCode));
    return true;
}

uintptr_t elem_detour(uintptr_t mgr, uintptr_t res, uintptr_t stage, uintptr_t elem, uint64_t a5) {
    const u2s::Mode m = mode_now();
    // U-f2 for the HOST's manager: a schedule stage user 2's armed manager already has is not created twice.
    if (m == u2s::Mode::On && mgr && mgr == g_host_mgr.load(std::memory_order_relaxed) &&
        g_armed.load(std::memory_order_acquire) && !g_aborted.load(std::memory_order_relaxed) && rdp(elem + kElem_Schedule) &&
        !rdp(elem + kElem_Res) && dup_skip(false, res, elem, static_cast<uint32_t>(a5))) {
        return res;
    }
    if (m != u2s::Mode::Off && mgr && mgr == g_u2_mgr.load(std::memory_order_relaxed)) {
        const uintptr_t res_ptr = rdp(elem + kElem_Res);
        const u2s::Element el = u2s::element_of(res_ptr, rdp(elem + kElem_Schedule),
                                                rdv<int16_t>(elem + kElem_Class2, -1));
        g_elem_seen[static_cast<size_t>(el)].fetch_add(1, std::memory_order_relaxed);
        // Review A3: only while armed and with its character in a field the host owns.
        const bool armed = g_armed.load(std::memory_order_acquire) && !g_aborted.load(std::memory_order_relaxed);
        const bool skip = armed && u2s::skip_element(true, m, armed,
                                                     host_owns(field_of(g_u2_char.load(std::memory_order_relaxed))), el);
        // U-f2: its schedule element whose stage the host's manager has alive.
        if (el == u2s::Element::Schedule && armed && m == u2s::Mode::On &&
            dup_skip(true, res, elem, static_cast<uint32_t>(a5))) {
            return res;
        }
        p::Event e;
        // U2 (tiendas-u2.md): a sequencer's resource base name (the char* at [[[elem+8]]], read byte by byte, at most
        // 160; 40 logged): a cd_seq_funcnpc_* passes when user2_stages_elements and the shop route allow it.
        bool funcnpc = false, trade = false;
        if (el == u2s::Element::Sequencer) {
            const uintptr_t path = res_ptr ? rdp(rdp(res_ptr)) : 0;
            char name[40] = {};
            size_t n = 0, base = 0, len = 0;
            for (size_t i = 0; path && i < 160; ++i) {
                uint8_t c = 0;
                if (!rd(path + i, c) || c == 0) break;
                if (c == '/' || c == '\\') base = i + 1;
                len = i + 1;
            }
            for (size_t i = base; path && i < len && n < sizeof name; ++i, ++n) {
                uint8_t c = 0;
                if (!rd(path + i, c)) break;
                name[n] = static_cast<char>(c);
                e.v[3 + n / 8] |= static_cast<uint64_t>(c) << (8 * (n % 8));
            }
            funcnpc = u2sh::is_funcnpc(std::string_view(name, n));
            // S46 phase 3: T1 decides by the EXACT dictionary hash of the base name (funcnpc_trade_policy.h dict_hash),
            // only for a name read whole (not cut at 40 bytes).
            const bool whole = n == len - base;
            trade = funcnpc && whole && u2trade::is_trade_name_by_hash(std::string_view(name, n));
            if (funcnpc && whole) e.v[11] = static_cast<uint64_t>(u2trade::dict_hash(std::string_view(name, n))) << 32;
        }
        // Review A1: no NEW shop scene under 10 cells from the host, nor while U-1's drain waits for a shop.
        const uint32_t dist = g_dist.load(std::memory_order_relaxed);
        const bool near_or_waiting =
            dist == UINT32_MAX || dist < u2sh::kNewShopMinCells || user2_shops_waiting();
        // S46 phase 3, T1 active (user2_shop_trade_list=1): the list by hash decides the funcnpc, whatever the elements
        // key says; latched by its abort, no funcnpc is on the list any more (every one SkipNotTrade).
        const auto list_mode = static_cast<u2s::Mode>(g_trade_list.load(std::memory_order_relaxed));
        const bool list_on = list_mode == u2s::Mode::On;
        if (list_on && g_t1_latched.load(std::memory_order_relaxed)) trade = false;
        const u2sh::ElemVerdict verdict = u2sh::element_verdict(
            skip, el, funcnpc, static_cast<u2sh::Elements>(g_elements.load(std::memory_order_relaxed)),
            user2_shops_may_prepare(), near_or_waiting, list_on, trade);
        if (el == u2s::Element::Sequencer) {
            g_elem_verdicts[static_cast<size_t>(verdict)].fetch_add(1, std::memory_order_relaxed);
        }
        // S46 T1, the verdict only: the element stays skipped exactly as above (nothing prepared).
        // Review B2: only with shop_trade_probe=1 or user2_shop_trade_list probe / 1 (with the S46 keys at 0 the lines
        // are exactly the kit 39's). With T1 active every funcnpc verdict carries it (bit 8 of v[10]).
        const bool t1_line = list_on ? (el == u2s::Element::Sequencer && funcnpc && skip)
                                     : (el == u2s::Element::Sequencer && u2sh::t1_measured(verdict) &&
                                        (shop_trade_probe_active() || list_mode != u2s::Mode::Off));
        if (t1_line) {
            const u2sh::T1 t1 = u2sh::t1_verdict(trade, near_or_waiting);
            g_t1[static_cast<size_t>(t1)].fetch_add(1, std::memory_order_relaxed);
            e.v[10] = (1 + static_cast<uint64_t>(t1)) | (list_on ? 0x100u : 0u);
            e.v[11] |= user2_shops_may_prepare() ? 1u : 0u;
        } else {
            e.v[11] = 0;
        }
        const bool skipped = verdict != u2sh::ElemVerdict::Original && verdict != u2sh::ElemVerdict::PassFuncnpc;
        e.v[0] = static_cast<uint64_t>(el);
        e.v[1] = skipped;
        e.v[2] = static_cast<uint32_t>(a5);  // 0x288A970's 5th argument: the static stage id (its FUN_140473f20 key)
        e.v[8] = static_cast<uint64_t>(Who::User2);
        e.v[9] = el == u2s::Element::Sequencer ? 1 + static_cast<uint64_t>(verdict) : 0;
        if (t1_line && trade) {
            emit_t1(kEElem, e);  // a trade-list element: its own budget (live phase 1: the class budget ate them)
        } else {
            emit(kEElem, e, true);
        }
        if (skipped) {
            g_elem_skipped[static_cast<size_t>(el)].fetch_add(1, std::memory_order_relaxed);
            put_zero(res);  // the element "succeeded" without doing anything (the caller reads [rax])
            return res;
        }
        // S46 phase 3 (the review's requirement): the shopkeeper of user 2's own trade scene is born by its
        // SpawnCharacter INSIDE this original call (p2b: NpcProbe's birth 117 us before ShopProbe's P of the same
        // prepare, host.log:12504 / :12540). Marked for this thread so that U-e, the families and the roads leave
        // its births alone (user2_stages_npc_control): excluded by the scene of origin, never by name.
        if (verdict == u2sh::ElemVerdict::PassFuncnpc && list_mode != u2s::Mode::Off) {
            // Review B5: the previous value comes back (a nested PassFuncnpc keeps the outer mark). An SEH exception
            // crossing this frame skips ~Mark under /EHsc: the mod's handlers continue the search (the game's crash),
            // and the end of the thread's outermost Troc clears the mark anyway (user2_funnels, ~Restore).
            struct Mark {
                uint32_t prev;
                explicit Mark(uint32_t id) : prev(t_t1_origin) { t_t1_origin = id ? id : 1; }
                ~Mark() { t_t1_origin = prev; }
                Mark(const Mark&) = delete;
                Mark& operator=(const Mark&) = delete;
            } mark(static_cast<uint32_t>(a5));
            return g_elem_hook.unsafe_call<uintptr_t>(mgr, res, stage, elem, a5);
        }
    }
    return g_elem_hook.unsafe_call<uintptr_t>(mgr, res, stage, elem, a5);
}

// ---------------------------------------------------------------- U-c / U-d: the calls

// P8 review I1 (game_fault.h): an exception inside the game's code is the game's: U-1 latches off (aborted), one ERR
// line goes straight to the log, it is passed on.
int call_fault(EXCEPTION_POINTERS* ep, const char* where) noexcept {
    g_aborted.store(true);
    g_faults.fetch_add(1);
    return game::game_fault_continue(ep, "User2Stages", where);
}
bool guarded_arm(uintptr_t mgr) noexcept {
    __try {
        reinterpret_cast<ArmFn>(g_base + kArmFn)(mgr);
        return true;
    } __except (call_fault(GetExceptionInformation(), "0x287F450(user 2's manager): the arm")) {
        return false;
    }
}
bool guarded_load(uintptr_t mgr) noexcept {
    __try {
        reinterpret_cast<LoadFn>(g_base + kLoadFn)(mgr, 0, 0);
        return true;
    } __except (call_fault(GetExceptionInformation(), "0x287DA50(user 2's manager, 0, 0): the drain")) {
        return false;
    }
}
bool guarded_unload(uintptr_t mgr) noexcept {
    __try {
        reinterpret_cast<UnloadFn>(g_base + kUnloadFn)(mgr, 0, 0);
        return true;
    } __except (call_fault(GetExceptionInformation(), "0x287D4C0(user 2's manager, 0, 0): the synchronous unload")) {
        return false;
    }
}
bool guarded_lists(uintptr_t mgr, uintptr_t field, uintptr_t actor) noexcept {
    __try {
        reinterpret_cast<ListFn>(g_base + kListFn)(mgr, field, actor);
        reinterpret_cast<InterestFn>(g_base + kInterestFn)(mgr, field);
        return true;
    } __except (call_fault(GetExceptionInformation(), "0x2896C60 / 0x287FA30(user 2's manager): its id list and tables")) {
        return false;
    }
}
bool guarded_enqueue(uintptr_t net, int* res, uint32_t delay, uintptr_t actor, const void* cell, uintptr_t troc,
                     const uint8_t* payload) noexcept {
    __try {
        reinterpret_cast<EnqueueFn>(g_base + kEnqueueFn)(net, res, delay, actor, cell, troc, payload,
                                                          static_cast<uint16_t>(u2s::kTimerPayload));
        return true;
    } __except (call_fault(GetExceptionInformation(), "ServerNet::EnqueueTimer 0x2774430: user 2's 2043")) {
        return false;
    }
}

enum Ctl : uint8_t {
    kCtlArm, kCtlArmRefused, kCtlDrain, kCtlDrainSync, kCtlDrainRefused, kCtlGoodbyeArmed, kCtlBuilt, kCtlCleanup,
    kCtlChainRestart,
    // S37 phase D: the one-shot 2043 of a fast arm (queued / its tick or a late one / cancelled).
    kCtlOneShot, kCtlOneShotTick, kCtlOneShotCancel
};
enum ArmWhy : uint8_t {
    kWhyOk, kWhyNoMgr, kWhyState, kWhyLists, kWhyField, kWhyCalls, kWhyAfter, kWhyAborted, kWhyDist, kWhySlots,
    kWhyBusy, kWhyFastStale, kWhyFastDist
};
const char* arm_why_name(uint64_t w) {
    switch (w) {
        case kWhyOk: return "ok";
        case kWhyNoMgr: return "user 2's manager not readable (vtable / back pointer)";
        case kWhyState: return "its state is not 0/1, or a preparation runs";
        case kWhyLists: return "its lists are not empty";
        case kWhyField: return "user 2 is not in a field the host owns";
        case kWhyCalls: return "the called functions are not build 2976's (refused at start)";
        case kWhyAfter: return "the arm ran but its state is not 2";
        case kWhyAborted: return "U-1 aborted";
        case kWhyDist: return "user 2 is no longer >= 10 cells from the host in the same field";
        case kWhySlots: return "ServerNet [exe+0x6D69680], the 2043 Troc [exe+0x6FA3ED8] or the period read 0";
        case kWhyBusy: return "another arm / drain / cleanup of this module was running (asked again)";
        case kWhyFastStale:
            return "a FAST arm no longer valid at the Req (user 2 loading / leaving, or g_p_draining held: a drain at "
                   "the host's preparation or an NPC cleanup): the machine stays ON, a normal arm after its 10 s";
        case kWhyFastDist:
            return "a FAST arm whose distance, recomputed from both characters at the Req, is unknown, in another "
                   "field or < 10 cells: a false trigger, the machine is cleared";
        default: return "?";
    }
}
void ctl_event(Ctl c, uintptr_t mgr, uint8_t before, uint8_t after, uint64_t kicked, uint16_t op, uint64_t why) noexcept {
    if (c == kCtlArmRefused) g_arm_fails.fetch_add(1, std::memory_order_relaxed);
    p::Event e;
    e.v[0] = c;
    e.v[1] = mgr;
    e.v[2] = before;
    e.v[3] = after;
    e.v[4] = kicked;
    e.v[5] = op;
    e.v[6] = why;
    e.v[8] = static_cast<uint64_t>(Who::User2);
    emit(kECtl, e, true);
}

// U-c2: inside its first armed evaluation (the owner gate, see kListFn).
void build_lists(uintptr_t mgr, uintptr_t field, uintptr_t actor) noexcept {
    const uint32_t before = rdv<uint32_t>(mgr + 0x158);
    if (!g_calls_ok.load(std::memory_order_relaxed) || !field || !actor) return;
    if (!guarded_lists(mgr, field, actor)) return;
    p::Event e;
    e.v[0] = kCtlBuilt;
    e.v[1] = mgr;
    e.v[2] = before;
    e.v[3] = rdv<uint32_t>(mgr + 0x158);
    e.v[4] = rdv<uint32_t>(mgr + 0x408);
    e.v[6] = actor;
    e.v[7] = rdv<uint32_t>(actor + u2s::kActor_Key);
    e.v[8] = static_cast<uint64_t>(Who::User2);
    emit(kECtl, e, true);
}

// Its periodic 2043 queued (0x287EF60's own payload, +0xB = 1): bits 0..3 = 1 queued / 2 EnqueueTimer faulted,
// 8..39 its result, 40.. the delay. A server thread, the module lock held.
uint64_t queue_chain_2043(uintptr_t subject, uintptr_t net, uintptr_t troc, uint32_t delay, uint64_t now) noexcept {
    uint8_t cell[8] = {};
    game::safe_read(g_base + kZeroCell, cell, sizeof cell);
    uint8_t payload[u2s::kTimerPayload] = {};
    u2s::timer_2043_payload(payload, cell);
    int res = -1;
    if (!guarded_enqueue(net, &res, delay, subject, cell, troc, payload)) return 2;
    g_chain_ended.store(false);
    g_chain_last_us.store(now);  // the new chain's first tick is one period away
    return 1 | static_cast<uint64_t>(static_cast<uint32_t>(res)) << 8 | static_cast<uint64_t>(delay) << 40;
}

// S33: armed, and no 2043 reached its manager for 1.5 periods since the arm / its last tick: its chain again (once per
// silence; NoTicks aborts only after this). At the exit of user 2's Req, the server's thread.
void kick_chain(uintptr_t subject, uint16_t op) noexcept {
    const uintptr_t net = rdp(g_base + kServerNetSlot), troc = rdp(g_base + kTroc2043Slot);
    const int32_t raw = rdv<int32_t>(g_base + kPeriodSlot);
    const uint32_t delay = u2s::timer_2043_delay_ms(raw), period = u2s::timer_2043_period_ms(raw);
    if (!net || !troc || !delay || !g_calls_ok.load()) return;
    ModLock lock;
    if (!lock.ok) return;  // the next Req tries again
    const uint64_t now = p8_now_us();
    const uint64_t last = std::max(g_arm_done_us.load(), g_last_mgr2_tick_us.load());
    if (!u2s::chain_kick_due(now, g_arm_done_us.load(), g_last_mgr2_tick_us.load(), g_kicks_arm.load(), period)) {
        return;
    }
    g_kicks_arm.fetch_add(1);  // before the call: never a second one for this arm
    const uint64_t kicked = queue_chain_2043(subject, net, troc, delay, now);
    g_kick_us.store(now);
    g_kicks.fetch_add(1);
    p::Event e;
    e.v[0] = kCtlChainRestart;
    e.v[1] = seq_manager(subject);
    e.v[2] = rdv<uint8_t>(e.v[1] + u2s::kMgr_State, 0xFF);
    e.v[4] = kicked;
    e.v[5] = op;
    e.v[6] = (now - last) / 1000;
    e.v[7] = period;
    e.v[8] = static_cast<uint64_t>(Who::User2);
    emit(kECtl, e, true);
}

// ---------------------------------------------------------------- S37 phase D: the one-shot 2043 (fast_rearm_logic.h)

// A pending one-shot invalidated (drain, logout, new character, re-arm). Its timer cannot be taken back from the game's
// queue: if it still fires it is counted as 'late' (oneshot_tick), never as a chain tick.
void oneshot_cancel(frm::Cancel why) noexcept {
    uint64_t gen = 0;
    if (!g_oneshot.cancel(why, p8_now_us(), gen)) return;
    g_os_cancelled.fetch_add(1, std::memory_order_relaxed);
    p::Event e;
    e.v[0] = kCtlOneShotCancel;
    e.v[1] = g_u2_mgr.load(std::memory_order_relaxed);
    e.v[3] = gen;
    e.v[4] = static_cast<uint64_t>(why);
    e.v[8] = static_cast<uint64_t>(Who::User2);
    emit(kECtl, e, true);
}
// The tick hook (a server thread): a 2043 with +0xB = 0 and +0xC = 0 reached user 2's manager. True when it was the
// fast arm's pending one-shot (consumed once) or the first one after its cancel / loss (late): neither is a chain tick.
bool oneshot_tick(uintptr_t mgr, uint8_t state, uint64_t now) noexcept {
    uint64_t gen = 0, queued = 0;
    const bool consumed = g_oneshot.consume(gen, queued);
    if (!consumed && !g_oneshot.late(now, gen)) return false;
    p::Event e;
    e.v[0] = kCtlOneShotTick;
    e.v[1] = mgr;
    e.v[2] = state;
    e.v[3] = gen;
    if (consumed) {
        g_os_consumed.fetch_add(1, std::memory_order_relaxed);
        const uint64_t delay_ms = queued && now >= queued ? (now - queued) / 1000 : 0;
        uint64_t mx = g_os_delay_ms_max.load(std::memory_order_relaxed);
        while (delay_ms > mx && !g_os_delay_ms_max.compare_exchange_weak(mx, delay_ms, std::memory_order_relaxed)) {
        }
        const uint64_t expect = g_os_expect_us.load(std::memory_order_relaxed);
        const uint64_t saved_ms = frm::saved_us(expect, now) / 1000;
        g_os_saved_ms_sum.fetch_add(saved_ms, std::memory_order_relaxed);
        mx = g_os_saved_ms_max.load(std::memory_order_relaxed);
        while (saved_ms > mx && !g_os_saved_ms_max.compare_exchange_weak(mx, saved_ms, std::memory_order_relaxed)) {
        }
        e.v[4] = delay_ms;
        e.v[6] = saved_ms;
        e.v[9] = 1;
    } else {
        g_os_late.fetch_add(1, std::memory_order_relaxed);
        e.v[7] = static_cast<uint64_t>(frm::OneShotSlot::cancel_of(g_oneshot.raw()));
    }
    e.v[8] = static_cast<uint64_t>(Who::User2);
    emit(kECtl, e, true);
    return true;
}
// At a FAST arm (mode 1), right after 0x287F450 put user 2's manager in state 2, on the same server thread with the
// module lock held (arm_locked): one 2043 of 0x287EF60's "not ready" shape (+0xB = 0) with delay 1 (100 ms) through the
// same ServerNet::EnqueueTimer 0x2774430 / Troc slot [exe+0x6FA3ED8] the chain uses. At most one pending per arm
// generation; the result is checked (0 = queued). The chain's own repair ran before (it is not replaced).
void queue_oneshot(uintptr_t subject, uintptr_t mgr, uintptr_t net, uintptr_t troc, uint64_t now, bool chain_queued,
                   uint64_t chain_last_before, uint32_t period_ms, uint16_t op) noexcept {
    const uint64_t gen = g_arm_gen.load(std::memory_order_relaxed);
    p::Event e;
    e.v[0] = kCtlOneShot;
    e.v[1] = mgr;
    e.v[3] = gen;
    e.v[5] = op;
    e.v[8] = static_cast<uint64_t>(Who::User2);
    if (!g_oneshot.may_queue(gen) || g_os_queued.load(std::memory_order_relaxed) >= frm::kOneShotCap) {
        e.v[4] = 3;  // not queued: one already pending / queued for this arm, or the per-run cap
        emit(kECtl, e, true);
        return;
    }
    uint8_t cell[8] = {};
    game::safe_read(g_base + kZeroCell, cell, sizeof cell);
    uint8_t payload[u2s::kTimerPayload] = {};
    frm::oneshot_2043_payload(payload, cell);
    int res = -1;
    // An exception inside EnqueueTimer never comes back here: call_fault returns EXCEPTION_CONTINUE_SEARCH (the game's
    // crash, as for the chain's own call with the same arguments). So only the returned result is handled.
    guarded_enqueue(net, &res, frm::kOneShotDelay, subject, cell, troc, payload);
    const uint64_t expect = frm::next_chain_tick_us(now, chain_queued, chain_last_before, period_ms);
    if (res != 0) {
        g_oneshot.failed(gen);
        g_os_failed.fetch_add(1, std::memory_order_relaxed);
        e.v[4] = 2;  // EnqueueTimer returned an error: nothing queued
    } else if (g_oneshot.queued(gen, now)) {
        g_os_queued.fetch_add(1, std::memory_order_relaxed);
        g_os_expect_us.store(expect, std::memory_order_relaxed);
        e.v[4] = 1;
    } else {
        // Review 2c: queued by the game but the slot refused it (unreachable: may_queue under the module lock). Its tick
        // would count as a normal one; said apart.
        g_os_failed.fetch_add(1, std::memory_order_relaxed);
        e.v[4] = 5;
    }
    e.v[2] = static_cast<uint32_t>(res);
    e.v[6] = expect > now ? (expect - now) / 1000 : 0;
    e.v[7] = static_cast<uint64_t>(chain_queued) | static_cast<uint64_t>(period_ms) << 8;
    e.v[9] = frm::kOneShotDelay;
    emit(kECtl, e, true);
}

// U-c on a server thread (user 2's outermost ExecuteTroc exit): the checks again, then the game's own arm. False: the
// module lock was busy (review 1: the caller asks again). fast: S37 phase D's fast arm (user2_fast_rearm=1).
void arm_locked(uintptr_t subject, uint16_t op, bool fast = false) noexcept;
bool arm_now(uintptr_t subject, uint16_t op, bool fast = false) noexcept {
    ModLock lock;  // review 1
    if (!lock.ok) {
        ctl_event(kCtlArmRefused, seq_manager(subject), 0, 0, 0, op, kWhyBusy);
        return false;
    }
    arm_locked(subject, op, fast);
    return true;
}
void arm_locked(uintptr_t subject, uint16_t op, bool fast) noexcept {
    const uintptr_t mgr = seq_manager(subject);
    if (!g_calls_ok.load()) return ctl_event(kCtlArmRefused, mgr, 0, 0, 0, op, kWhyCalls);
    if (!mgr) return ctl_event(kCtlArmRefused, 0, 0, 0, 0, op, kWhyNoMgr);
    // S37 phase D: a fast arm re-evaluated here (the audit: re-check before acting). Review 3c: user 2 loading /
    // leaving or g_p_draining held refuse it with the machine left ON (control() asks again after its 10 s, as a
    // normal arm: what the hysteresis alone would have done). Review 3a: the distance recomputed here from both
    // characters (as prep_drain does), not the module's g_dist of the very sample that triggered it: unknown, another
    // field or < 10 cells clears the machine (a false trigger).
    fast = fast && fast_mode_now() == u2s::Mode::On;
    if (fast && !frm::fast_still_valid(child_hold_user2_loading(), server_logout_requested(),
                                       g_p_draining.load(std::memory_order_seq_cst))) {
        g_fast_refused.fetch_add(1, std::memory_order_relaxed);
        return ctl_event(kCtlArmRefused, mgr, 0, 0, 0, op, kWhyFastStale);
    }
    if (fast) {
        const uintptr_t hc = g_host_char.load(std::memory_order_relaxed);
        const uintptr_t hf = field_of(hc), uf = field_of(subject);
        u2s::Cell a, b;
        const bool known = hc && hf && hf == uf && actor_cell(hc, a) && actor_cell(subject, b);
        if (!known || u2s::cheb(a, b) < u2s::Thresholds{}.on_cells) {
            g_fast_refused.fetch_add(1, std::memory_order_relaxed);
            g_fast_dist_refused.fetch_add(1, std::memory_order_relaxed);
            g_reset_machine.store(true);
            return ctl_event(kCtlArmRefused, mgr, 0, 0, known ? u2s::cheb(a, b) : UINT32_MAX, op, kWhyFastDist);
        }
    }
    const uint8_t st = rdv<uint8_t>(mgr + u2s::kMgr_State);
    if ((st != 0 && st != 1) || rdv<uint8_t>(mgr + u2s::kMgr_Preparing) || rdv<uint8_t>(mgr + u2s::kMgr_PrepPending)) {
        return ctl_event(kCtlArmRefused, mgr, st, st, 0, op, kWhyState);
    }
    if (lists_nonempty(mgr)) return ctl_event(kCtlArmRefused, mgr, st, st, 0, op, kWhyLists);
    if (!host_owns(field_of(subject))) return ctl_event(kCtlArmRefused, mgr, st, st, 0, op, kWhyField);
    // Review A2: the distance again, here (the request may be up to a few seconds old).
    const uint32_t d = g_dist.load(std::memory_order_relaxed);
    if (d == UINT32_MAX || d < u2s::Thresholds{}.on_cells) return ctl_event(kCtlArmRefused, mgr, st, st, 0, op, kWhyDist);
    // Low: the slots the 2043 needs, read before anything is called.
    const uintptr_t net = rdp(g_base + kServerNetSlot), troc = rdp(g_base + kTroc2043Slot);
    const int32_t raw = rdv<int32_t>(g_base + kPeriodSlot);
    const uint32_t delay = u2s::timer_2043_delay_ms(raw), period = u2s::timer_2043_period_ms(raw);
    if (!net || !troc || !delay) return ctl_event(kCtlArmRefused, mgr, st, st, 0, op, kWhySlots);
    // Low: the active-stage map (+0x70 / +0x74) before the arm (0x102CA7F0 drops what +0xC8 does not keep).
    const uint64_t map = static_cast<uint64_t>(rdv<uint32_t>(mgr + 0x70)) |
                         static_cast<uint64_t>(rdv<uint32_t>(mgr + 0x74)) << 32;
    if (!guarded_arm(mgr)) return;
    const uint8_t after = rdv<uint8_t>(mgr + u2s::kMgr_State);
    // Review A5: its 2043 chain is queued only when it is dead (no rescheduling tick on its manager for 1.5 periods:
    // S33) or ended (a tick met state 1, or its load). If no 2043 comes after the arm, chain_kick_due restarts it.
    uint64_t kicked = 0;
    const uint64_t now = p8_now_us();
    const uint64_t chain_last_before = g_chain_last_us.load(std::memory_order_relaxed);
    const bool dead = u2s::chain_dead(now, chain_last_before, period);
    const bool ended = g_chain_ended.load(std::memory_order_relaxed);
    if (dead || ended) kicked = queue_chain_2043(subject, net, troc, delay, now) | (dead ? 0x10 : 0x20);
    g_kick_us.store(0, std::memory_order_relaxed);
    g_kicks_arm.store(0, std::memory_order_relaxed);
    if (fast_mode_now() != u2s::Mode::Off) {
        // S37 phase D: a new generation; whatever one-shot of an older arm is still pending is invalid now, and a
        // cancelled / lost / late state of an older one goes back to idle (review 2a).
        g_oneshot.settle();
        oneshot_cancel(frm::Cancel::Rearm);
        g_first_tick_us.store(0, std::memory_order_relaxed);
        g_arm_gen.fetch_add(1, std::memory_order_relaxed);
        g_arm_fast.store(fast && after == 2, std::memory_order_relaxed);
    }
    g_arm_done_us.store(now, std::memory_order_relaxed);
    g_lists_built.store(false);  // U-c2: rebuilt in its first armed evaluation
    user2_shops_reset("a new arm");  // U2 (review A1): one drain wait per arm
    g_armed.store(true, std::memory_order_release);
    g_ever_armed.store(true, std::memory_order_relaxed);
    g_arms.fetch_add(1, std::memory_order_relaxed);
    p::Event e;
    e.v[0] = kCtlArm;
    e.v[1] = mgr;
    e.v[2] = st;
    e.v[3] = after;
    e.v[4] = kicked;
    e.v[5] = op;
    e.v[6] = after == 2 ? kWhyOk : kWhyAfter;
    e.v[7] = map;
    e.v[8] = static_cast<uint64_t>(Who::User2);
    e.v[9] = fast;
    emit(kECtl, e, true);
    // S37 phase D: the one-shot only for a fast arm that put the manager in state 2 (0x287DBB0 in state 2 evaluates;
    // +0xB = 0 never reschedules). The chain repair above already ran: the one-shot does not replace it.
    if (fast && after == 2) {
        g_fast_arms.fetch_add(1, std::memory_order_relaxed);
        queue_oneshot(subject, mgr, net, troc, now, (kicked & 0x0F) == 1, chain_last_before, period, op);
    }
}

// U-d: the game's own unload of user 2's manager (state 1 + its immediate 2043 -> 0x287D4C0 through the timer route),
// and with sync the unload at once (its loads: F3 / F4 reset +0x6A0 to 0 inside the 2283 / 2362, before that 2043
// would run; its logout). The timer route stays on (review A1): the immediate 2043 and its periodic one keep going to
// its OWN manager, never to the host's.
bool drain_now(uintptr_t mgr, bool sync, uint16_t op, Cause cause) noexcept {
    if (!mgr || !g_calls_ok.load()) {
        ctl_event(kCtlDrainRefused, mgr, 0, 0, cause, op, mgr ? kWhyCalls : kWhyNoMgr);
        return true;
    }
    ModLock lock;  // review 1
    if (!lock.ok) {
        ctl_event(kCtlDrainRefused, mgr, 0, 0, cause, op, kWhyBusy);
        return false;
    }
    const uint8_t before = rdv<uint8_t>(mgr + u2s::kMgr_State);
    g_armed.store(false, std::memory_order_release);
    // S37 phase D: no one-shot of this arm survives its drain (AUDITORIA §4: invalidate old work; the game's timer
    // itself stays queued <= 100 ms and acts as a chain tick would). A pending FAST arm request keeps its flag (review
    // 3e): arm_locked re-checks it.
    if (fast_mode_now() != u2s::Mode::Off) {
        oneshot_cancel(cause == kCauseLogout ? frm::Cancel::Logout : frm::Cancel::Drain);
    }
    user2_shops_reset("a drain of user 2's manager");  // U2 (review A1): its shop scenes go with it
    t_in_drain = true;  // P inside these calls (not expected) is not drained again
    const bool ok = guarded_load(mgr) && (!sync || guarded_unload(mgr));
    t_in_drain = false;
    if (!ok) return true;
    g_last_drain_cause.store(cause, std::memory_order_relaxed);
    g_last_drain_us.store(p8_now_us(), std::memory_order_relaxed);  // U-e: its NPCs must be gone 5 s later
    (sync ? g_drains_sync : g_drains).fetch_add(1, std::memory_order_relaxed);
    ctl_event(sync ? kCtlDrainSync : kCtlDrain, mgr, before, rdv<uint8_t>(mgr + u2s::kMgr_State), cause, op, kWhyOk);
    return true;
}

// ---------------------------------------------------------------- U-e: the leftovers (review 3)
//
// SAM::FindActor 0x2A827B0(sam = [[exe+0x6D69698]], scope*, key): a ScopeAttacher<ServerActor> of 0x20 bytes (actor
// +8, found +0x10); released as MoveActorReq does (0x2978358: vtable := ScopeAttacherBase 0x5725E48, then
// cdmp_FieldAttacher__dtor 0x1434880). Inside it, with the full key and [+0x90] == user 2's key checked,
// ServerActor::Logout 0x2068340(actor, &out, 0x19, [exe+0x6CF76F8]): the collector's own call (0x2C38F02..0x2C38F14);
// the client AI's 2291 reaches Logout(42) inside its Req's actor scope too (0x2AC6EA4).
constexpr uintptr_t kFindActorFn = 0x2A827B0, kScopeDtorFn = 0x1434880, kLookupSlot = 0x6D69698,
                    kVtScopeBase = 0x5725E48, kLogoutFn = 0x2068340, kLogoutErrSlot = 0x6CF76F8,
                    kAlreadyLogoutingSlot = 0x6CDB934;  // eErrNoUserAlreadyLogouting, Logout's out when already leaving
constexpr uint8_t kFindActorBytes[] = {0x44, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10};
constexpr uint8_t kScopeDtorBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
using FindActorFn = void*(__fastcall*)(uintptr_t sam, void* scope, uint32_t key);
using ScopeDtorFn = void(__fastcall*)(void* scope);
using LogoutFn = int*(__fastcall*)(uintptr_t actor, int* out, uint8_t reason, uint32_t err);
struct alignas(16) ActorScope {
    uintptr_t q[4];
};
int cleanup_fault(EXCEPTION_POINTERS* ep) noexcept {
    // Con la contención lista, el fallo la detiene y vuelve la red antigua (containment_ready pasa a
    // false); sin ella, el comportamiento de c7ae132: U-e deja de asignar.
    if (containment_ready()) {
        g_activation_fault.store(true);
        g_activation_faults.fetch_add(1);
    } else {
        g_ctl_aborted.store(true);
        g_faults.fetch_add(1);
    }
    return game::game_fault_continue(ep, "User2Stages", "limpieza: SAM::FindActor / ServerActor::Logout (motivo 25)");
}
// 0 logged out, 1 not found (gone, or already logging out), 2 another key / controller / parent (not ours any more),
// 3 a fault, 4 Logout refused (local retirement only, expected_parent given: npcact::logout_result). The parent is
// read only for that local retirement (npc_activation=1); modes 0 and probe read what c7ae132 read.
// not_ours (segunda revisión, R1): con 2, true si el actor ya es de otra clave o controlador (no por su padre).
int cleanup_one(uintptr_t sam, uint32_t key, uint32_t key2, uint32_t err,
                uint32_t expected_parent = UINT32_MAX, bool validate_only = false, bool* not_ours = nullptr) noexcept {
    ActorScope sc{};
    int out = 0, r = 1;
    if (not_ours) *not_ours = false;
    __try {
        reinterpret_cast<FindActorFn>(g_base + kFindActorFn)(sam, &sc, key);
        const uintptr_t actor = sc.q[1];
        if ((sc.q[2] & 0xFF) && actor) {
            uint32_t k = 0, c = 0;
            const bool known = rd(actor + u2s::kActor_Key, k) && rd(actor + u2s::kActor_Ctl, c);
            if (known && k == key && c == key2) {
                const bool local = expected_parent != UINT32_MAX;
                const auto parent = local ? child_hold_actor_parent(actor) : std::optional<uint32_t>{};
                if (local && (!parent || *parent != expected_parent)) {
                    r = 2;
                } else if (validate_only) {
                    r = 0;
                } else {
                    reinterpret_cast<LogoutFn>(g_base + kLogoutFn)(actor, &out, 0x19, err);
                    r = npcact::logout_result(static_cast<uint32_t>(out), rdv<uint32_t>(g_base + kAlreadyLogoutingSlot),
                                              local);
                }
            } else {
                r = 2;
                if (not_ours && known) *not_ours = true;  // leído y distinto; ilegible no demuestra nada
            }
        }
        sc.q[0] = g_base + kVtScopeBase;
        reinterpret_cast<ScopeDtorFn>(g_base + kScopeDtorFn)(&sc);
    } __except (cleanup_fault(GetExceptionInformation())) {
        r = 3;
    }
    return r;
}
enum CleanupWhy : uint8_t { kCleanAfterDrain, kCleanItsTroc, kCleanItsLogout, kCleanUnaccepted };
// A server thread (user 2's Troc or its character's Logout): the NPCs given to user 2 put before `before_us` (0 = all)
// and still alive, at most 64 a pass. unaccepted_only: those with no accepted StartActionReq 5 s after their birth.
// Segunda revisión (R1/R10, npc_family_control=1): `tried` = las claves ya miradas en pasadas anteriores de esta misma
// llamada (no se vuelven a mirar: su resultado no cambia antes de su reintento a 1 s).
npcact::CleanPass ctl_cleanup_pass(uint64_t before_us, CleanupWhy why, uint16_t op, bool unaccepted_only,
                                   uint32_t* tried, size_t& ntried, size_t tried_cap, uint8_t pass) noexcept {
    npcact::CleanPass out;
    const uint32_t key2 = g_ctl_key2.load(std::memory_order_relaxed);
    if (!key2) return out;
    uint32_t keys[64], parents[64] = {};
    size_t n = 0;
    decltype(g_ctl_keys)::View v;
    const bool fam = family_mode() == npcact::Mode::On;
    const auto was_tried = [tried, &ntried](uint32_t key) {
        for (size_t i = 0; i < ntried; ++i) {
            if (tried[i] == key) return true;
        }
        return false;
    };
    if (fam) {
        // S36 (AUDITORIA-2 cambio 7): de hojas a raíz también aquí (drenaje, logout, su Troc): las 64 claves MÁS
        // profundas de su unidad, la más profunda primero; ninguna sale antes que un descendiente suyo, ni en esta
        // tanda ni en las siguientes (npcfam::LeafFirst). Sin unidad la profundidad es 0 y el orden es el de la tabla.
        // Revisión: un miembro sigue el filtro de su RAÍZ (su hora de alta), y una unidad nunca la retira la red antigua
        // de 'no aceptadas' (sus miembros no tienen Req propia: los juzga su raíz, la contención).
        npcfam::LeafFirst<64> order;
        for (size_t i = 0; i < decltype(g_ctl_keys)::capacity(); ++i) {
            if (!g_ctl_keys.get(i, v) || !(v.flags & u2s::kKeyAssigned) || (v.flags & u2s::kKeyRemoved)) continue;
            if (unaccepted_only && (v.flags & u2s::kKeyFamily)) continue;
            if (pass && was_tried(v.key)) continue;
            uint64_t put_us = v.put_us;
            uint8_t depth = 0;
            if (v.flags & u2s::kKeyFamily) {
                ActivationRead read;
                if (const npcact::Actor* a = g_activation.find(v.key); a && !a->removed) {
                    depth = a->fam_depth;
                    if (npcact::family_member(*a)) {
                        if (const uint64_t root_put = g_ctl_keys.put_time(a->unit_key)) put_us = root_put;
                    }
                }
            }
            if (before_us && put_us >= before_us) continue;
            if (unaccepted_only && ((v.flags & u2s::kKeyAccepted) || p8_now_us() < v.put_us + u2s::kAcceptWithinUs)) continue;
            order.add(v.key, depth);
        }
        order.sort();
        out.full = order.dropped > 0;  // R10: más claves que la tanda (p. ej. raíces detrás de sus miembros)
        ActivationRead read;
        for (size_t i = 0; i < order.n; ++i) {
            const npcact::Actor* a = g_activation.find(order.key[i]);
            parents[n] = a && !a->removed && a->fam ? a->parent : 0;
            keys[n++] = order.key[i];
        }
    } else {
        for (size_t i = 0; i < decltype(g_ctl_keys)::capacity() && n < 64; ++i) {
            if (!g_ctl_keys.get(i, v) || !(v.flags & u2s::kKeyAssigned) || (v.flags & u2s::kKeyRemoved)) continue;
            if (before_us && v.put_us >= before_us) continue;
            if (unaccepted_only && ((v.flags & u2s::kKeyAccepted) || p8_now_us() < v.put_us + u2s::kAcceptWithinUs)) continue;
            keys[n++] = v.key;
        }
    }
    out.n = n;
    if (!n) return out;
    // Read now, not at install (the 1e20571 run: before the world it may be unset). Unset: this round is skipped.
    const uintptr_t lookup = rdp(g_base + kLookupSlot);
    const uintptr_t sam = lookup ? rdp(lookup) : 0;
    if (!sam) {
        g_cleanup_no_sam.fetch_add(1, std::memory_order_relaxed);
        p::Event e;
        e.v[0] = kCtlCleanup;
        e.v[1] = n;
        e.v[5] = op;
        e.v[6] = why;
        e.v[9] = lookup ? 2 : 1;  // 1: [exe+0x6D69698] is 0; 2: the lookup is there, its SAM pointer is 0
        e.v[8] = static_cast<uint64_t>(Who::User2);
        emit(kECtl, e, true);
        out.busy = true;  // como ocupada: tras un drenaje se reintenta (acotado)
        return out;
    }
    ModLock lock;
    if (!lock.ok) {
        g_cleanup_busy.fetch_add(1, std::memory_order_relaxed);
        out.busy = true;
        return out;
    }
    g_cleanup_runs.fetch_add(1, std::memory_order_relaxed);
    if (fam) {
        for (size_t i = 0; i < n && ntried < tried_cap; ++i) tried[ntried++] = keys[i];
    }
    const uint32_t err = rdv<uint32_t>(g_base + kLogoutErrSlot);
    uint64_t done = 0, gone = 0, other = 0, fault = 0, left = 0;
    // Segunda revisión (R1): lo que una pasada de familia deja vivo es un resultado POR UNIDAD (bloqueo hoja-raíz o fallo
    // con su propio reintento acotado): kKeyCleanBlocked lo saca de left_after_drain (nunca el aborto global).
    const auto leave = [&left](uint32_t key) {
        if (g_ctl_keys.mark(key, u2s::kKeyCleanBlocked, p8_now_us())) ++left;
    };
    npcfam::RetireGuard guard_tree;  // S36: retirada parcial, nunca un padre con un hijo suyo vivo
    for (size_t i = 0; i < n && !fault; ++i) {
        if (fam && guard_tree.is_blocked(keys[i])) {
            g_fam_blocked.fetch_add(1, std::memory_order_relaxed);
            guard_tree.block(parents[i]);
            ++other;
            leave(keys[i]);
            continue;  // sigue asignada: la próxima limpieza (o la red de U-e) la vuelve a mirar
        }
        npcact::Actor local;
        if (activation_on()) {
            ActivationLock guard;
            if (auto* current = g_activation.find(keys[i]); current && current->local_retired) {
                if (!npcact::may_cleanup_member(*current, current->key, current->birth, p8_now_us())) {
                    // Revisión: sin llamada para él en esta pasada y sigue vivo: su padre espera.
                    if (fam && !current->removed) {
                        guard_tree.block(parents[i]);
                        leave(keys[i]);
                    }
                    continue;
                }
                local = *current;
                current->pending = true;
            }
        }
        bool not_ours = false;
        const int result = cleanup_one(sam, keys[i], key2, err, local.key ? local.parent : UINT32_MAX, false, &not_ours);
        if (local.key) {
            ActivationLock guard;
            g_activation.cleanup_result(local.key, local.birth, result, p8_now_us());
        }
        switch (result) {
            case npcact::kCleanDone: ++done; break;
            case npcact::kCleanGone: ++gone; break;
            case npcact::kCleanOther: case npcact::kCleanFailed: ++other; break;  // 4: its own bounded retries
            default: ++fault; break;
        }
        const bool removed = !fault && (!local.key || result == 0 || result == 1);
        if (removed) g_ctl_keys.mark(keys[i], u2s::kKeyRemoved, p8_now_us());
        if (!fam) continue;
        if (!removed && !fault) leave(keys[i]);
        // R1: kCleanOther porque ya es de otra clave o controlador (renació con el anfitrión) no bloquea a su padre.
        if (npcact::cleanup_blocks_parent(result, not_ours)) guard_tree.block(parents[i]);
    }
    g_cleaned.fetch_add(done, std::memory_order_relaxed);
    g_cleanup_gone.fetch_add(gone, std::memory_order_relaxed);
    g_cleanup_other.fetch_add(other, std::memory_order_relaxed);
    if (left) g_cleanup_left_blocked.fetch_add(left, std::memory_order_relaxed);
    out.done = done;
    out.gone = gone;
    out.left = left;
    out.fault = fault != 0;
    p::Event e;
    e.v[0] = kCtlCleanup;
    e.v[1] = n;
    e.v[2] = done;
    e.v[3] = gone;
    e.v[4] = other;
    e.v[5] = op;
    e.v[6] = why;
    e.v[7] = fault;
    e.v[8] = static_cast<uint64_t>(Who::User2);
    e.v[10] = left;
    e.v[11] = fam ? 1u + pass : 0;
    emit(kECtl, e, true);
    return out;
}
// R1/R10: con npc_family_control=1, pasadas acotadas en la misma llamada mientras la anterior estuvo llena y progresó
// (npcact::cleanup_next_pass); con 0/probe, una sola pasada como siempre. Devuelve la última pasada.
npcact::CleanPass ctl_cleanup(uint64_t before_us, CleanupWhy why, uint16_t op, bool unaccepted_only = false) noexcept {
    if (ctl_mode_now() != u2s::Mode::On || !g_cleanup_ok.load(std::memory_order_relaxed) || !server_realm()) return {};
    uint32_t tried[npcact::kCleanupPasses * 64];  // ~1 KB en la pila de un hilo del servidor (no en el mid)
    size_t ntried = 0;
    npcact::CleanPass last;
    size_t left = 0;
    for (uint8_t pass = 0;; ++pass) {
        last = ctl_cleanup_pass(before_us, why, op, unaccepted_only, tried, ntried, std::size(tried), pass);
        left += last.left;
        if (family_mode() != npcact::Mode::On || !npcact::cleanup_next_pass(last, static_cast<uint8_t>(pass + 1))) break;
        g_cleanup_passes.fetch_add(1, std::memory_order_relaxed);
    }
    last.left = left;
    return last;
}
// R1: tras la limpieza de un drenaje (o una repetición suya), si quedó algo bloqueado o la limpieza estaba ocupada, se
// programa otra en un Troc posterior del usuario 2 (kCleanupRetryUs después), como mucho kAfterDrainReruns por drenaje.
// Sólo con npc_family_control=1: con 0/probe la limpieza tras el drenaje sigue siendo una sola, como antes.
void ctl_after_drain_rerun(uint64_t drain, const npcact::CleanPass& r, uint8_t reruns_done, uint64_t now) noexcept {
    if (family_mode() != npcact::Mode::On || !npcact::cleanup_rerun(r, reruns_done)) return;
    g_cleanup_rerun_for.store(drain, std::memory_order_relaxed);
    g_cleanup_rerun_at.store(now + npcact::kCleanupRetryUs, std::memory_order_relaxed);
}

// ---------------------------------------------------------------- S38 fase C: viajeros 2550 del invitado
//
// docs/research/s25/s38-fase-c.md; diseño 3-viajeros.md §4; cambios obligatorios AUDITORIA.md §3 / INFORME-A3 §3-§4.
// [session2] user2_road_control = 0 | probe | 1 (anfitrión). Pieza propia, separada de U-e: su tabla (road::Table), su
// ciclo de vida (retirada SÓLO en la carga y el Logout del invitado, nunca al acercarse el anfitrión), sus paradas (sin
// aborto global) y su limpieza estricta. Ni el vigilante de U-e (g_ctl_keys, CtlAbort) ni la contención de
// npc_activation (g_activation) ven estas claves: un viajero no es un NPC de stage, ningún stage lo hace renacer con el
// anfitrión, y retirarlo por «sin progreso» sólo lo haría desaparecer (y el cliente pediría otro: el bucle de S31).
//
// Procedencia (el mid 0x2792A7B de docked_gimmicks, el único gancho): descriptor ICreateServerActorDesc 0x5B04BE8 con
// +0x3CD = 1, +0x3C8 = el personaje del usuario 2, +0x3B8 = su puntero, Y nacido dentro del Troc 2550 del usuario 2
// (0x29C4930 -> 0x27E8C90 -> 0x27EA160). La marca sola no basta (0x28981B0 y ActivateSpawningPool 0x27EE3E0 escriben el
// mismo descriptor). Unidad entera: raíz + subárbol _Rider del mismo solicitante (road::scan_unit); un miembro que no
// puede seguir a su raíz (o un hijo que no es viajero: gimmick tipo 7) parte la unidad, que (modo 1) se retira entera.
std::atomic<uint8_t> g_road_req{0}, g_road_mode{0};  // npcact::Mode: pedido en el ini / efectivo
std::atomic<uint8_t> g_road_stop{0};                 // road::Stop (sólo esta pieza)
std::atomic<uint32_t> g_road_stop_key{0};
SRWLOCK g_road_lock = SRWLOCK_INIT;
road::Table<256> g_road;
std::atomic<size_t> g_road_live{0};
std::atomic<bool> g_road_pending{false};
std::atomic<uint64_t> g_road_next_us{0}, g_road_troc_serial{0};
std::atomic<uint32_t> g_road_epoch_seen{0};
p::EventRing<1024> g_road_events;
enum RoadEv : uint16_t { kRvBirth, kRvReject, kRvSplit, kRvOrphan, kRvCensus, kRvRequest, kRvRetired, kRvStop, kRv2005,
                         kRvHostReq, kRvHierLate, kRvSplitLoop, kRvCartStop, kRvGone };
constexpr size_t kRoadV = static_cast<size_t>(road::Verdict::Count), kRoadC = road::kClasses;
std::atomic<uint64_t> g_road_v[kRoadV][kRoadC] = {};          // raíces por veredicto y clase
std::atomic<uint64_t> g_road_scan[static_cast<size_t>(road::ScanV::Count)] = {};
std::atomic<uint64_t> g_road_member[3][kRoadC] = {};          // asignado/se asignaría, carro en probe (S40), partido
std::atomic<uint64_t> g_road_split_why[static_cast<size_t>(road::Split::Count)] = {};
// Fase C probe (15-10): sin_unidad por clase y por la vtable de su padre de descriptor; host-kept (gimmicks tipo 7 de
// una caravana, fase B §2.7) por registro / sin registro; preexámenes fallidos por envoltorio; lotes sellados.
std::atomic<uint64_t> g_road_orphan_cls[road::kClasses] = {}, g_road_orphan_parent[4] = {};  // base, gimmick, horario, otro
std::atomic<uint64_t> g_road_host_kept{0}, g_road_host_kept_unrec{0}, g_road_seals{0};
// Revisión de la fase C (build/research/fase-c-revision.md):
//  §2 procedencia positiva: 2550 leídas en su Troc y fallidas; §3.2 bucle de partidas (SplitGuard, bajo g_road_lock),
//  raíces tipo 5 fuera del modo 1 (user2_road_carts), registros con un hijo sin sitio (nunca se retiran);
//  §3.3 retiradas evitadas por dueño (+0xA0) / jinete jugador / nacimiento cambiado; §4.1 cancelación de publicación:
//  hijos retenidos incluidos, negativas por causa, y en probe las unidades cuya cancelación se negaría.
std::atomic<uint8_t> g_road_carts{0};  // road::Carts (S40 carros: 0 | probe | 1)
road::SplitGuard g_road_split_guard;
// S40 carros (docs/research/s25/s40-carros.md): las partidas de unidades NO asignadas (probe, o carros en probe con el
// modo 1) van a su propio contador de bucle (sólo la línea «pararía»): nunca bloquean ni paran las asignaciones reales.
road::SplitGuard g_road_split_would;
// S40 revisión §6: las unidades ASIGNADAS de raíz tipo 5 tienen su propio contador de bucle; su parada global sólo para
// los carros (nunca a los caminantes). Bajo g_road_lock.
road::SplitGuard g_road_split_carts;
// Bajo g_road_lock: composición por clase de carretera ([resumen carros]); revisión §8: las clases de raíz tipo 5 en su
// propio libro (las de caminantes y bandadas no les quitan sitio).
road::CaravanBook g_road_book, g_road_book_carts;
std::atomic<uint64_t> g_road_cart_would_roots{0}, g_road_child[static_cast<size_t>(road::ChildKind::Count)][3] = {},
    g_road_linked_rides{0};
// S40 revisión §6: parada sólo de los carros (doble control / 2005 / restore 0 / bucle de unidades de raíz tipo 5): no
// se asignan raíces tipo 5 nuevas; caminantes y bandadas siguen.
std::atomic<uint8_t> g_road_cart_stop{0};
std::atomic<uint32_t> g_road_cart_stop_key{0};
// S40 revisión §4: antes de cada Logout, quién lo monta (por fuente: +0xAC, mount_sync, vínculo del personaje, asientos)
// y la legibilidad de los asientos del vehículo; al nacer un jinete de un tipo 5, si los asientos de su vehículo se leen
// (sonda de la forma del componente en el servidor: [0] legibles, [1] ilegibles, [2] con un personaje).
std::atomic<uint64_t> g_road_ride_src[4] = {}, g_road_ride_unknown{0}, g_road_seats_logout[2] = {},
    g_road_seats_birth[3] = {}, g_road_kept_changed{0}, g_road_hier_cart_incomplete{0};
std::atomic<uint64_t> g_road_requests_read{0}, g_road_requests_bad{0}, g_road_incomplete{0}, g_road_split_units[2] = {},
    g_road_owned{0}, g_road_occupied{0}, g_road_reborn_skip{0}, g_road_held_added{0}, g_road_held_no_room{0},
    g_road_pub_busy{0}, g_road_pub_would_refuse{0}, g_road_pub_would_children{0}, g_road_pub_would_busy{0};
std::atomic<uint64_t> g_road_scan_wrap[static_cast<size_t>(npcfam::Wrap::Count) + 1] = {};
std::atomic<uint64_t> g_road_orphans{0}, g_road_link_unknown{0}, g_road_foreign{0}, g_road_reborn{0},
    g_road_saved_marked{0};
std::atomic<uint64_t> g_road_guest_2005[2] = {}, g_road_host_2005[3] = {};  // [ok, otro controlador] / [+ restore 0]
std::atomic<uint64_t> g_road_would_guest_2005{0}, g_road_would_host_2005{0}, g_road_would_ctl_diff{0};
// Paridad 1:1 (AUDITORIA §3: descriptor -> unidad -> 2005): registros 2005 AL INVITADO con metadatos de carretera cuyo
// solicitante es su personaje, por clase: con registro en la tabla / sin registro (otro productor, raíz descartada,
// o un nacimiento que el mid no vio).
std::atomic<uint64_t> g_road_meta_registered[road::kClasses] = {}, g_road_meta_unregistered[road::kClasses] = {};
std::atomic<uint64_t> g_road_guest_req{0}, g_road_host_req_ctl{0}, g_road_host_req_2556{0}, g_road_host_req_would{0};
std::atomic<uint64_t> g_road_removed[4] = {};  // bajas nativas: motivo 25, 42, 44, otro
std::atomic<uint64_t> g_road_requests{0}, g_road_req_units{0}, g_road_would_units_at_request{0}, g_road_runs{0},
    g_road_busy{0}, g_road_no_sam{0}, g_road_units_done{0}, g_road_units_failed{0}, g_road_pub_refused{0},
    g_road_faults{0};
std::atomic<uint64_t> g_road_calls[static_cast<size_t>(road::Call::Count)] = {};
std::atomic<uint64_t> g_road_mid_n{0}, g_road_mid_us{0}, g_road_mid_max_us{0};
// Cupos de líneas por clase (atómicos): nacimientos asignados / se asignaría, descartes por veredicto, huérfanos,
// partidos, censos, retiradas, 2005 y Req discrepantes.
npcfam::LineCap<2> g_road_birth_cap;
npcfam::LineCap<kRoadV> g_road_reject_cap;
npcfam::LineCap<8> g_road_misc_cap;  // 0 huérfano, 1 partido, 2 censo, 3 retirada, 4 2005, 5 Req anfitrión, 6 pub
inline constexpr uint32_t kRoadBirthLines[2] = {1500, 3000}, kRoadRejectLines = 24;
inline constexpr uint32_t kRoadMiscLines[8] = {48, 64, 500, 500, 32, 32, 32, 0};  // 6: hijo de jerarquía tardío
// Rotación nativa (build/research/churn-viajeros/informe.md §3): con el modo 1 las bandadas 0x1F5 del invitado nacen y
// caen al ritmo nativo (~80/min) y la línea por nacimiento agotaba su cupo en ~35 min. Muestreo por clase de carretera
// (las primeras 40, luego 1 de cada 25; las de unidades tipo 5 todas; los cupos de arriba quedan como tope); paridad
// invitado / anfitrión ([resumen paridad]) y la rama del recolector en cada baja ([viajero: baja]).
road::Sampler<32> g_road_birth_sample;
inline constexpr uint32_t kGoneFirst = 30, kGoneEvery = 20;  // [viajero: baja] por lado y carretera
road::Sampler<32, kGoneFirst, kGoneEvery> g_road_gone_sample;
SRWLOCK g_parity_lock = SRWLOCK_INIT;
road::Parity g_parity;  // ~40 KB, estática (a cero)
std::atomic<size_t> g_parity_live{0};
std::atomic<uint64_t> g_road_gone[2][static_cast<size_t>(road::Gone::Count)] = {};
npcfam::LineCap<1> g_parity_wrn_cap;
inline constexpr uint32_t kParityWrnLines = 30;
struct ParityLock {
    ParityLock() noexcept { AcquireSRWLockExclusive(&g_parity_lock); }
    ~ParityLock() {
        g_parity_live.store(g_parity.live_n, std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&g_parity_lock);
    }
};
npcact::Mode road_mode() noexcept { return static_cast<npcact::Mode>(g_road_mode.load(std::memory_order_relaxed)); }
struct RoadLock {
    RoadLock() noexcept { AcquireSRWLockExclusive(&g_road_lock); }
    ~RoadLock() {
        g_road_live.store(g_road.live, std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&g_road_lock);
    }
};
struct RoadRead {
    RoadRead() noexcept { AcquireSRWLockShared(&g_road_lock); }
    ~RoadRead() { ReleaseSRWLockShared(&g_road_lock); }
};
// Por hilo: el id de cada Troc del usuario 2 abierto (por profundidad), las unidades que este hilo abrió en ellos (sus
// descriptores sólo valen mientras su Troc vive) y los descriptores marcados nacidos sin unidad abierta (un miembro
// nacido antes que su raíz deja a esa raíz fuera: BornFirst). Unos 300 B por hilo.
struct RoadTls {
    uint64_t id[npcfam::kDepths];
    int16_t unit[16];
    uint8_t udepth[16];
    uint8_t n;
    uintptr_t orphan[16], oparent[16];  // descriptor marcado nacido sin unidad y su +0x2F0
    uint8_t odepth[16];
    uint8_t on;
    uint32_t calling_key;  // la clave cuyo Logout estamos llamando (su entrada en el punto O no es una baja nativa)
    road::Request req;     // las clases de la 2550 del Troc en curso (procedencia positiva)
};
thread_local RoadTls t_road{};
void road_stop(road::Stop why, uint32_t key) noexcept {
    uint8_t none = 0;
    if (!g_road_stop.compare_exchange_strong(none, static_cast<uint8_t>(why))) return;
    g_road_stop_key.store(key, std::memory_order_relaxed);
    p::Event e;
    e.kind = kRvStop; e.qpc_us = p8_now_us(); e.tid = GetCurrentThreadId();
    e.v[0] = static_cast<uint64_t>(why); e.v[1] = key;
    g_road_events.push(e);
}
// S40 revisión §6: una parada que nace de una unidad de raíz tipo 5 (el anfitrión monta un caballo de caravana asignado,
// su 2005, su bucle) sólo para los carros nuevos. Las unidades ya asignadas siguen igual que con road_stop.
void road_cart_stop(road::Stop why, uint32_t key) noexcept {
    uint8_t none = 0;
    if (!g_road_cart_stop.compare_exchange_strong(none, static_cast<uint8_t>(why))) return;
    g_road_cart_stop_key.store(key, std::memory_order_relaxed);
    p::Event e;
    e.kind = kRvCartStop; e.qpc_us = p8_now_us(); e.tid = GetCurrentThreadId();
    e.v[0] = static_cast<uint64_t>(why); e.v[1] = key;
    g_road_events.push(e);
}
// La parada que toca a la unidad de `key` (bajo g_road_lock por el llamador): la de los carros si su raíz es tipo 5.
bool road_cart_unit(const road::Loc& l) noexcept {
    return l && g_road.u[l.unit].n && g_road.u[l.unit].r[0].type == 5;
}
bool road_faulted() noexcept {
    return g_road_stop.load(std::memory_order_relaxed) == static_cast<uint8_t>(road::Stop::Fault);
}
uint64_t road_next_birth() noexcept { return g_birth_serial.fetch_add(1, std::memory_order_relaxed) + 1; }
void road_cost(uint64_t t0) noexcept {
    const uint64_t d = p8_now_us() - t0;
    g_road_mid_n.fetch_add(1, std::memory_order_relaxed);
    g_road_mid_us.fetch_add(d, std::memory_order_relaxed);
    store_max_u64(g_road_mid_max_us, d);
}
uint8_t road_type(uintptr_t actor) noexcept {
    const uintptr_t ti = rdp(actor + u2s::kActor_TypeInfo);
    return ti ? rdv<uint8_t>(ti + 1) : 0;
}
// Paridad (informe churn-viajeros §3.2): el alta de un viajero marcado del invitado o del anfitrión (raíz o miembro).
void road_parity_birth(uint32_t key, const road::Prov& p, uintptr_t desc, uintptr_t actor) noexcept {
    const uint32_t u2 = g_u2_key.load(std::memory_order_relaxed), host = g_host_key.load(std::memory_order_relaxed);
    road::Side side;
    if (u2 && p.req_key == u2) side = road::Side::Guest;
    else if (host && p.req_key == host) side = road::Side::Host;
    else return;
    const uint32_t road_id = rdv<uint32_t>(desc + road::kDesc_Road);
    const uint8_t type = road_type(actor);
    ParityLock guard;
    g_parity.birth(key, side, road_id, type, p8_now_us());
}
// Campos de carretera del descriptor (+0x3D0.., para cotejar con el 2005) y los de datos guardados [H] (0x28981B0).
void road_desc_fields(uintptr_t desc, p::Event& e) noexcept {
    e.v[7] = static_cast<uint64_t>(rdv<uint32_t>(desc + road::kDesc_Road)) |
             static_cast<uint64_t>(rdv<uint32_t>(desc + road::kDesc_RoadB)) << 32;
    e.v[8] = static_cast<uint64_t>(rdv<uint32_t>(desc + road::kDesc_RoadC)) |
             static_cast<uint64_t>(rdv<uint32_t>(desc + road::kDesc_RoadD)) << 32;
    const bool s0 = rdv<uint64_t>(desc + road::kDesc_Saved0) != 0, s1 = rdv<uint64_t>(desc + road::kDesc_Saved1) != 0;
    e.v[9] = static_cast<uint64_t>(s0) | static_cast<uint64_t>(s1) << 1;
    if (s0 || s1) g_road_saved_marked.fetch_add(1, std::memory_order_relaxed);
}
// ¿Algún nodo del preexamen ya nació sin unidad en este hilo (antes que su raíz)?
bool road_born_first(uintptr_t root, const road::Scan& s) noexcept {
    const RoadTls& t = t_road;
    for (uint8_t i = 0; i < s.n; ++i) {
        const uintptr_t parent = s.nodes[i].parent == road::kNoIndex ? root : s.nodes[s.nodes[i].parent].desc;
        for (uint8_t j = 0; j < t.on && j < 16; ++j) {
            if (t.orphan[j] == s.nodes[i].desc && t.oparent[j] == parent) return true;
        }
    }
    return false;
}
void road_close_depth(uint32_t depth) noexcept;
// Revisión §3.3 / S40 (y su revisión §4): ¿lo monta un jugador? Fuentes, todas lecturas protegidas sin cerrojos o
// atómicos: el padre serializado (+0xAC; jerarquía como respaldo, child_hold_actor_parent) del personaje del anfitrión
// o del invitado, mount_sync (el par armado de este PC; el padre de la última acción del otro jugador), el vínculo de
// montura de cada personaje (road::read_char_link) y, con `actor`, los asientos y reservas del propio vehículo
// (road::read_seats). Un tipo 5 sólo sale Free con los asientos del vehículo Y los vínculos de los personajes validados
// (road::ride_check); si no, Unknown: nunca se supone libre. `count`: contadores por fuente (antes de Logout).
road::Ride road_ride(uint32_t key, uintptr_t actor, bool type5, bool count) noexcept {
    if (!key) return road::Ride::Free;
    const uintptr_t chars[2] = {g_host_char.load(std::memory_order_relaxed), g_u2_char.load(std::memory_order_relaxed)};
    bool parent_hit = false;
    road::CharLink links[2];
    for (size_t i = 0; i < 2; ++i) {
        if (!chars[i]) continue;
        const auto parent = child_hold_actor_parent(chars[i]);
        if (parent && *parent == key) parent_hit = true;
        links[i] = road::read_char_link(FamRead{}, chars[i]);
    }
    const bool ms_hit = mount_sync_rides(key);
    road::SeatsRead seats;
    if (actor) seats = road::read_seats(FamRead{}, actor);
    const road::Ride r = road::ride_check(key, type5, parent_hit, ms_hit, links, seats);
    if (count) {
        if (actor) g_road_seats_logout[seats.ok ? 0 : 1].fetch_add(1, std::memory_order_relaxed);
        if (parent_hit) g_road_ride_src[0].fetch_add(1, std::memory_order_relaxed);
        if (ms_hit) g_road_ride_src[1].fetch_add(1, std::memory_order_relaxed);
        for (const road::CharLink& l : links) {
            if (l.ok && (l.cur == key || l.res == key)) { g_road_ride_src[2].fetch_add(1, std::memory_order_relaxed); break; }
        }
        if (seats.ok && seats.player) g_road_ride_src[3].fetch_add(1, std::memory_order_relaxed);
        if (r == road::Ride::Unknown) g_road_ride_unknown.fetch_add(1, std::memory_order_relaxed);
    }
    return r;
}
// Al asignar (tipo 5, raíz o vehículo enlazado): sólo las fuentes positivas (un vehículo recién creado no tiene jinete).
bool road_player_rides(uint32_t key) noexcept { return road_ride(key, 0, false, false) == road::Ride::Rides; }
// La raíz: +0x2F0 leído a 0. Registro de la unidad entera ANTES de cambiar EAX.
void road_root(uintptr_t& rax, uintptr_t desc, uintptr_t field, uintptr_t actor, const road::Prov& p, uint32_t key,
               npcact::Mode mode) noexcept {
    // S40 carros: con el modo 1, una raíz tipo 5 sólo escribe con user2_road_carts=1; con probe su unidad se abre y se
    // mide entera («se asignaría»: ni controlador ni retirada) mientras los demás viajeros se asignan.
    const bool mode_on = mode == npcact::Mode::On;
    const auto carts = static_cast<road::Carts>(g_road_carts.load(std::memory_order_relaxed));
    const uint8_t type = road_type(actor);
    const bool write = road::root_writes(mode_on, carts, type);
    // Fase C probe (15-10): cada entrada 2550 es su propia llamada a 0x27EA160 (todas las raíces llevan +0x3CC = 1, el
    // «primero del lote» de :1478) y sus descriptores se destruyen al terminarla (:1886): un lote nuevo reutiliza sus
    // direcciones. La primera raíz de un lote sella las unidades del lote anterior en este hilo (censo, punteros
    // olvidados): un descriptor de este lote nunca se confunde con uno ya liberado del anterior.
    if (p.first == 1 && t_troc.depth && t_road.n) {
        g_road_seals.fetch_add(1, std::memory_order_relaxed);
        road_close_depth(t_troc.depth);
    }
    road::RootIn in;
    in.prov = p;
    in.host_char_key = g_host_key.load(std::memory_order_relaxed);
    in.u2_char_key = g_u2_key.load(std::memory_order_relaxed);
    in.u2_char = g_u2_char.load(std::memory_order_relaxed);
    uint16_t op = 0;
    const bool su2 = subject_user2(&op);
    in.in_road_troc = su2 && op == road::kOpAutoSpawnRoad;
    const uint32_t road_id = rdv<uint32_t>(desc + road::kDesc_Road);
    {
        const uint32_t d = t_troc.depth;
        in.in_request = d && d < npcfam::kDepths ? road::in_request(t_road.req, t_road.id[d], road_id) : -1;
    }
    // S40 revisión §1: parada y bucle con el modo (como en ef5a6b5), no con la escritura de ESTA raíz; §6: los tipo 5
    // además con la parada y el bucle propios de los carros.
    in.stopped = mode_on && (g_road_stop.load(std::memory_order_relaxed) != 0 ||
                             (type == 5 && g_road_cart_stop.load(std::memory_order_relaxed) != 0));
    if (mode_on) {
        RoadRead read;
        in.road_blocked = g_road_split_guard.blocked(road_id) || (type == 5 && g_road_split_carts.blocked(road_id));
    }
    in.carts_allowed = road::carts_allowed(mode_on, carts);
    in.old_ctl_known = rd(actor + road::kActor_Ctl, in.old_ctl);
    in.desc_user_known = rd(desc + road::kDesc_User, in.desc_user);
    in.actor_user_known = rd(actor + road::kActor_User, in.actor_user);
    in.host_field = host_owns(field);
    in.eax = static_cast<uint32_t>(rax);
    in.field_owner = rdv<uint32_t>(field + road::kField_Owner);
    in.type = type;
    const uintptr_t pj2 = in.u2_char;
    in.key2 = pj2 ? rdv<uint32_t>(pj2 + u2s::kActor_Ctl) : 0;
    in.session_key2 = server_user2_user_key();
    road::Verdict v = road::root_verdict(in);
    // S40: nunca se asigna un vehículo que un jugador ya monta (lecturas protegidas, sin cerrojos; sólo los tipo 5 que
    // pasarían todo lo demás: nada de lecturas para los del anfitrión ni los descartados).
    if (v == road::Verdict::Assign && type == 5 && road_player_rides(key)) {
        in.player_rides = true;
        v = road::root_verdict(in);
    }
    road::Scan scan;  // ~250 B en la pila del mid; los nodos sin inicializar
    scan.v = road::ScanV::Ok;
    scan.n = 0;
    scan.bad_wrap = 0;
    scan.bad_inner = 0;
    if (v == road::Verdict::Assign) {
        road::scan_unit(FamRead{}, desc, g_base, g_image_size, p.req_key, p.req_ptr, scan);
        if (scan.v == road::ScanV::Ok && road_born_first(desc, scan)) scan.v = road::ScanV::BornFirst;
        g_road_scan[static_cast<size_t>(scan.v)].fetch_add(1, std::memory_order_relaxed);
        if (scan.v == road::ScanV::NotRider || scan.v == road::ScanV::NotTraveller || scan.v == road::ScanV::Unmarked) {
            g_road_scan_wrap[static_cast<size_t>(npcfam::wrap_of(scan.bad_wrap))].fetch_add(1, std::memory_order_relaxed);
        }
        if (scan.v != road::ScanV::Ok) v = road::Verdict::UnitRejected;
    }
    uint64_t birth = 0;
    int ui = -1;
    if (v == road::Verdict::Assign) {
        RoadTls& t = t_road;
        const uint32_t depth = t_troc.depth;
        if (!depth || depth >= npcfam::kDepths || !t.id[depth] || t.n >= 16) {
            v = road::Verdict::NoRoom;
        } else {
            birth = road_next_birth();
            const uint64_t now = p8_now_us();
            RoadLock guard;
            ui = g_road.open(desc, key, birth, server_epoch(), p.req_key, p.req_ptr, in.key2, write, t.id[depth],
                             GetCurrentThreadId(), now, scan);
            if (ui >= 0) {
                g_road.u[ui].road = road_id;
                g_road.born(static_cast<size_t>(ui), 0, key, birth, in.type, write, now);
                road::Rec& r = g_road.u[ui].r[0];
                r.actor = actor;
                r.ctl = write ? in.key2 : in.eax;
            } else {
                v = road::Verdict::NoRoom;
            }
        }
        if (ui >= 0) {
            t.unit[t.n] = static_cast<int16_t>(ui);
            t.udepth[t.n] = static_cast<uint8_t>(depth);
            ++t.n;
        }
    }
    const size_t cls = road::class_of(in.type);
    if (v == road::Verdict::Assign && mode_on && !write) {
        g_road_cart_would_roots.fetch_add(1, std::memory_order_relaxed);  // S40: carro en probe con el modo 1
    } else {
        g_road_v[static_cast<size_t>(v)][cls].fetch_add(1, std::memory_order_relaxed);
    }
    p::Event e;
    e.qpc_us = p8_now_us(); e.tid = GetCurrentThreadId(); e.flags = write;
    e.v[0] = key; e.v[1] = in.type | static_cast<uint64_t>(cls) << 24; e.v[2] = key; e.v[3] = p.req_key;
    e.v[4] = in.key2; e.v[5] = in.eax;
    e.v[6] = op | static_cast<uint64_t>(p.first) << 16 | static_cast<uint64_t>(p.mark) << 24 |
             static_cast<uint64_t>(su2) << 32;
    if (v == road::Verdict::Assign) {
        if (write) rax = (rax & ~static_cast<uintptr_t>(0xFFFFFFFF)) | in.key2;  // mov [r14+0x90],eax: el usuario 2
        uint32_t ord = 0;
        if (g_road_birth_sample.take(road::sample_key(road_id, write), in.type == 5, &ord) &&
            g_road_birth_cap.take(write, kRoadBirthLines[write])) {
            e.kind = kRvBirth;
            road_desc_fields(desc, e);
            e.v[9] |= static_cast<uint64_t>(ord) << 32;
            e.v[10] = birth; e.v[11] = static_cast<uint64_t>(scan.n);
            g_road_events.push(e);
        }
        return;
    }
    // Descartes con algo que medir (no los ajenos sin marca): pocas líneas por veredicto.
    if (g_road_reject_cap.take(static_cast<size_t>(v), kRoadRejectLines)) {
        e.kind = kRvReject;
        e.flags = mode_on;  // el modo de la pieza (como en ef5a6b5), no la escritura de esta raíz
        road_desc_fields(desc, e);
        e.v[10] = static_cast<uint64_t>(v) | static_cast<uint64_t>(scan.v) << 8;
        // Revisión §2: la 2550 en curso (1 / 0 / -1 desconocida) y cuántas clases tenía.
        e.v[9] |= static_cast<uint64_t>(in.in_request + 1) << 8 | static_cast<uint64_t>(t_road.req.n) << 16;
        e.v[11] = static_cast<uint64_t>(scan.bad_wrap) | static_cast<uint64_t>(scan.bad_inner) << 32;
        g_road_events.push(e);
    }
}
// Un descriptor marcado con padre (+0x2F0): miembro de una unidad abierta en este hilo, o sin unidad.
void road_member(uintptr_t& rax, uintptr_t desc, uintptr_t field, uintptr_t actor, const road::Prov& p,
                 uintptr_t parent_desc, uint32_t key, npcact::Mode mode) noexcept {
    RoadTls& t = t_road;
    const uint8_t type = road_type(actor);
    const size_t cls = road::class_of(type);
    const uintptr_t pj2 = g_u2_char.load(std::memory_order_relaxed);
    const uint32_t key2 = pj2 ? rdv<uint32_t>(pj2 + u2s::kActor_Ctl) : 0;
    road::MemberIn m;
    m.link_present = true;
    m.old_ctl_known = rd(actor + road::kActor_Ctl, m.old_ctl);
    m.desc_user_known = rd(desc + road::kDesc_User, m.desc_user);
    m.actor_user_known = rd(actor + road::kActor_User, m.actor_user);
    m.host_field = host_owns(field);
    const uint32_t eax = static_cast<uint32_t>(rax);
    m.owner_branch = eax && eax == rdv<uint32_t>(field + road::kField_Owner);
    m.type = type;
    m.key2 = key2;
    // Lecturas sin cerrojo antes de tomarlo: el padre en el vector y su envoltorio (sólo si es tardío).
    bool rider = false;
    const int in_vec = road::in_vector(FamRead{}, parent_desc, desc, g_base, g_image_size, &rider);
    int ui = -1, pi = -1, ri = -1;
    road::MemberOut o;
    bool assigned = false, unit_assigned = false, cart_unit = false;
    uint32_t root_key = 0, controller = 0, unit_road = 0;
    uint64_t birth = 0, unit_birth = 0;
    uintptr_t seat_actor = 0;  // S40 revisión §4: el vehículo tipo 5 de su padre (sonda de asientos)
    if (t.n) {  // revisión §6: sin unidad abierta en este hilo no hay nada que buscar (ni cerrojo)
        RoadLock guard;
        for (uint8_t i = 0; i < t.n && i < 16; ++i) {
            const int at = g_road.find_desc(static_cast<size_t>(t.unit[i]), parent_desc);
            if (at >= 0) { ui = t.unit[i]; pi = at; break; }
        }
        if (ui >= 0) {
            road::Unit& u = g_road.u[ui];
            ri = g_road.find_desc(static_cast<size_t>(ui), desc);
            if (ri >= 0 && u.r[ri].born) {
                g_road_reborn.fetch_add(1, std::memory_order_relaxed);  // el mismo descriptor otra vez: nada
                return;
            }
            const road::Rec& pr = u.r[pi];
            m.unit_live = true;
            m.reserved = ri >= 0;
            m.link_matches = true;  // encontrado por el descriptor de su padre
            m.in_vector = in_vec;
            m.rider = rider;
            m.room = u.n < road::kMaxUnit;
            m.traveller = road::same_requester(p, u.req_key, u.req_ptr);
            m.parent_alive = pr.born && pr.actor ? npcfam::read_matches(FamRead{}, pr.actor + road::kActor_Key, pr.key) : 0;
            m.parent_ctl = !u.assigned ? 1
                           : pr.actor  ? npcfam::read_matches(FamRead{}, pr.actor + road::kActor_Ctl, u.controller)
                                       : 0;
            m.unit_controller = u.controller;
            o = road::member_verdict(m);
            if (ri < 0 && !(o.v == road::MemberV::Split && o.why == road::Split::LateNoRoom)) {
                ri = g_road.add_late(static_cast<size_t>(ui), desc, static_cast<uint8_t>(pi));
            }
            const uint64_t now = p8_now_us();
            if (ri >= 0) {
                assigned = o.v == road::MemberV::Assign && u.assigned;
                birth = road_next_birth();
                if (!g_road.born(static_cast<size_t>(ui), static_cast<size_t>(ri), key, birth, type, assigned, now)) {
                    if (o.v == road::MemberV::Assign) o = {road::MemberV::Split, road::Split::PutFailed};
                    assigned = false;
                } else {
                    road::Rec& r = u.r[ri];
                    r.actor = actor;
                    r.ctl = assigned ? u.controller : eax;
                    r.split = o.v == road::MemberV::Split;
                }
            } else if (o.v == road::MemberV::Assign) {
                o = {road::MemberV::Split, road::Split::LateNoRoom};
            }
            if (o.v == road::MemberV::Split) g_road.mark_split(static_cast<size_t>(ui), o.why, now);
            root_key = u.r[0].key;
            controller = u.controller;
            unit_birth = u.birth;
            unit_assigned = u.assigned;
            unit_road = u.road;
            cart_unit = u.r[0].born && u.r[0].type == 5;
            if (u.r[pi].type == 5 && u.r[pi].born) seat_actor = u.r[pi].actor;
        }
    }
    // S40 revisión §4: sonda de la forma del componente de vehículo en el servidor (lo que la revalidación antes de
    // Logout necesita): al nacer el jinete / conductor de un tipo 5, ¿se leen los asientos de su vehículo? Sólo con
    // user2_road_carts != 0 (con 0, nada nuevo en el mid).
    if (seat_actor && g_road_carts.load(std::memory_order_relaxed) != 0) {
        const road::SeatsRead s = road::read_seats(FamRead{}, seat_actor);
        g_road_seats_birth[!s.ok ? 1 : s.player ? 2 : 0].fetch_add(1, std::memory_order_relaxed);
    }
    if (ui < 0) {
        // Su unidad no está abierta (raíz descartada o asignada fuera de este Troc): nace con su controlador nativo,
        // coherente con su raíz. Se apunta para que su raíz, si llega después en este Troc, no se admita.
        if (p.req_key != g_u2_key.load(std::memory_order_relaxed)) return;  // los del anfitrión: nada que medir
        g_road_orphans.fetch_add(1, std::memory_order_relaxed);
        g_road_orphan_cls[cls].fetch_add(1, std::memory_order_relaxed);
        // Su padre de descriptor: ¿de carretera y del mismo solicitante (su raíz se descartó), gimmick, horario u otro?
        const road::Prov pp = road::read_prov(FamRead{}, parent_desc, g_base, g_image_size);
        const uint32_t pvt = pp.vt ? pp.vt : rva_of(rdp(parent_desc));
        const size_t pc = pvt == road::kVtDescBase ? 0 : pvt == road::kVtInstantGimmick ? 1
                          : pvt == npcfam::kVtNpcSchedule ? 2 : 3;
        g_road_orphan_parent[pc].fetch_add(1, std::memory_order_relaxed);
        if (t.on < 16) {
            t.orphan[t.on] = desc;
            t.oparent[t.on] = parent_desc;
            t.odepth[t.on] = static_cast<uint8_t>(t_troc.depth);
            ++t.on;
        }
        if (g_road_misc_cap.take(0, kRoadMiscLines[0])) {
            p::Event e;
            e.kind = kRvOrphan; e.qpc_us = p8_now_us(); e.tid = GetCurrentThreadId();
            e.v[0] = key; e.v[1] = type; e.v[3] = p.req_key; e.v[4] = t_troc.depth;
            uint16_t op = 0;
            e.v[6] = subject_user2(&op) ? op : 0;
            e.v[7] = pvt; e.v[8] = static_cast<uint64_t>(pp.mark) | static_cast<uint64_t>(pp.first) << 8 |
                                  static_cast<uint64_t>(road::same_requester(pp, p.req_key, p.req_ptr)) << 16;
            e.v[9] = static_cast<uint64_t>(npcfam::read_parent(FamRead{}, parent_desc).state);
            e.v[10] = t.n;
            g_road_events.push(e);
        }
        return;
    }
    // 1: S40, miembro que se asignaría de un carro en probe con el modo 1 (HostUnit no llega aquí: ui >= 0).
    const size_t mv = o.v == road::MemberV::Assign ? (mode == npcact::Mode::On && !unit_assigned ? 1 : 0)
                      : o.v == road::MemberV::Split ? 2
                                                    : 1;
    g_road_member[mv][cls].fetch_add(1, std::memory_order_relaxed);
    if (o.v == road::MemberV::Split) g_road_split_why[static_cast<size_t>(o.why)].fetch_add(1, std::memory_order_relaxed);
    if (assigned && mode == npcact::Mode::On) {
        rax = (rax & ~static_cast<uintptr_t>(0xFFFFFFFF)) | controller;  // el controlador de su unidad
    }
    p::Event e;
    // S40: «asignado» sólo si su unidad lo está (un carro en probe con el modo 1 se mide como «se asignaría»).
    e.qpc_us = p8_now_us(); e.tid = GetCurrentThreadId(); e.flags = mode == npcact::Mode::On && unit_assigned;
    e.v[0] = key; e.v[2] = root_key; e.v[3] = p.req_key; e.v[4] = controller; e.v[5] = eax;
    e.v[1] = type | static_cast<uint64_t>(ri >= 0 && m.reserved ? 1 : 2) << 8 | static_cast<uint64_t>(cls) << 24;
    e.v[10] = birth; e.v[11] = unit_birth;
    if (o.v == road::MemberV::Split) {
        if (g_road_misc_cap.take(1, kRoadMiscLines[1])) {
            e.kind = kRvSplit;
            e.v[6] = static_cast<uint64_t>(o.why);
            g_road_events.push(e);
        }
    } else {
        uint32_t ord = 0;
        if (g_road_birth_sample.take(road::sample_key(unit_road, e.flags), cart_unit || type == 5, &ord) &&
            g_road_birth_cap.take(e.flags, kRoadBirthLines[e.flags])) {
            e.kind = kRvBirth;
            road_desc_fields(desc, e);
            e.v[9] |= static_cast<uint64_t>(ord) << 32;
            e.v[6] = static_cast<uint64_t>(p.first) << 16 | static_cast<uint64_t>(p.mark) << 24;
            g_road_events.push(e);
        }
    }
}
// Un descriptor que no es de carretera nacido mientras este hilo tiene una unidad abierta y cuyo +0x2F0 es un registro
// de esa unidad (road::child_verdict). Un gimmick (InstantGimmick) tipo 7 y (S40) su carga tipo 7 se quedan con el
// anfitrión (host-kept, la regla de la fase B §2.7, con publication_gate=1, que el modo 1 exige): se registran si caben
// (se retiran con su unidad, de hojas a raíz) y nunca la parten. S40 carros: el vehículo enlazado de un tipo 5
// (_LinkedVehicle de 0x2797510, demostrado entero para ESTE padre) sigue a su unidad (asignado como un miembro).
// Cualquier otra cosa la parte (modo 1: se retira entera, también él: su controlador nativo queda en su registro).
void road_foreign_child(uintptr_t& rax, uintptr_t desc, uintptr_t field, uintptr_t actor, npcact::Mode mode) noexcept {
    const npcfam::LinkRead link = npcfam::read_parent(FamRead{}, desc);
    if (link.state != npcfam::Link::Present) return;
    RoadTls& t = t_road;
    const uint32_t key = rdv<uint32_t>(actor + road::kActor_Key);
    if ((key >> 28) != 0xB) return;
    const uint8_t type = road_type(actor);
    const uint32_t vt = rva_of(rdp(desc));
    const uint32_t eax = static_cast<uint32_t>(rax);
    road::ChildIn c;
    // S40 revisión §1: con user2_road_carts=0, la regla de ef5a6b5 (gimmick host-kept, todo lo demás parte) y ninguna
    // lectura nueva.
    c.carts_rules = g_road_carts.load(std::memory_order_relaxed) != 0;
    c.vt = vt;
    c.type = type;
    c.in_vector = 0;
    // S40: lecturas sin cerrojo de un posible vehículo enlazado (descriptor base de tipo 5): su envoltorio en el vector
    // de su padre, los campos que escribe 0x2797510 y la cola común de un miembro.
    uint32_t req = 0, road_id = 0, pkey = 0;
    const bool maybe_linked = c.carts_rules && vt == road::kVtDescBase && type == 5;
    if (maybe_linked) {
        c.in_vector = road::in_vector(FamRead{}, link.parent, desc, g_base, g_image_size, nullptr, &c.wrap);
        uint16_t word = 0xFFFF;
        c.fields_ok = rd(desc + road::kDesc_First, word) && rd(desc + road::kDesc_ReqKey, req) &&
                      rd(desc + road::kDesc_Road, road_id) && rd(desc + road::kDesc_ParentActorKey, pkey);
        c.mark = static_cast<uint8_t>(word >> 8);
        c.player_rides = road_player_rides(key);
        road::MemberIn& m = c.m;
        m.link_present = true;
        m.link_matches = true;
        m.old_ctl_known = rd(actor + road::kActor_Ctl, m.old_ctl);
        m.desc_user_known = rd(desc + road::kDesc_User, m.desc_user);
        m.actor_user_known = rd(actor + road::kActor_User, m.actor_user);
        m.host_field = host_owns(field);
        m.owner_branch = eax && eax == rdv<uint32_t>(field + road::kField_Owner);
        m.type = type;
        const uintptr_t pj2 = g_u2_char.load(std::memory_order_relaxed);
        m.key2 = pj2 ? rdv<uint32_t>(pj2 + u2s::kActor_Ctl) : 0;
    }
    road::ChildOut o;
    uint32_t root_key = 0, controller = 0;
    uint64_t birth = 0, unit_birth = 0;
    bool recorded = false, assigned = false, unit_assigned = false;
    {
        RoadLock guard;
        int ui = -1, pi = -1;
        for (uint8_t i = 0; i < t.n && i < 16; ++i) {
            const int at = g_road.find_desc(static_cast<size_t>(t.unit[i]), link.parent);
            if (at >= 0) { ui = t.unit[i]; pi = at; break; }
        }
        if (ui < 0) return;
        road::Unit& u = g_road.u[ui];
        const uint64_t now = p8_now_us();
        int ri = g_road.find_desc(static_cast<size_t>(ui), desc);  // un gimmick del preexamen
        if (ri >= 0 && u.r[ri].born) return;
        const road::Rec& pr = u.r[pi];
        c.parent_host_kept = pr.host_kept;
        c.parent_type = pr.type;
        if (maybe_linked) {
            c.req_key_match = req && req == u.req_key;
            c.road_match = road_id && road_id == u.road;
            c.parent_key_match = pkey && pr.born && pkey == pr.key;
            road::MemberIn& m = c.m;
            m.unit_live = true;
            m.room = ri >= 0 || u.n < road::kMaxUnit;
            m.parent_alive = pr.born && pr.actor ? npcfam::read_matches(FamRead{}, pr.actor + road::kActor_Key, pr.key) : 0;
            m.parent_ctl = !u.assigned ? 1
                           : pr.actor  ? npcfam::read_matches(FamRead{}, pr.actor + road::kActor_Ctl, u.controller)
                                       : 0;
            m.unit_controller = u.controller;
        }
        o = road::child_verdict(c);
        if (ri < 0) ri = g_road.add_late(static_cast<size_t>(ui), desc, static_cast<uint8_t>(pi));
        assigned = o.v == road::MemberV::Assign && u.assigned;
        birth = road_next_birth();
        if (ri >= 0 && g_road.born(static_cast<size_t>(ui), static_cast<size_t>(ri), key, birth, type, assigned, now)) {
            road::Rec& r = u.r[ri];
            r.actor = actor;
            r.ctl = assigned ? u.controller : eax;
            r.host_kept = o.v == road::MemberV::HostKept;
            r.split = o.v == road::MemberV::Split;
            r.linked = o.kind == road::ChildKind::Linked && o.v == road::MemberV::Assign;
            recorded = true;
        } else {
            // Revisión §3.2: un hijo sin registro: su padre nunca se retira (no un gimmick con su carga viva).
            u.r[pi].incomplete = true;
            g_road_incomplete.fetch_add(1, std::memory_order_relaxed);
            if (o.v == road::MemberV::Assign) o = {road::MemberV::Split, road::Split::PutFailed, o.kind};
            assigned = false;
        }
        if (o.v == road::MemberV::Split) g_road.mark_split(static_cast<size_t>(ui), o.why, now);
        root_key = u.r[0].key;
        controller = u.controller;
        unit_birth = u.birth;
        unit_assigned = u.assigned;
    }
    const size_t kind = static_cast<size_t>(o.kind);
    g_road_child[kind < static_cast<size_t>(road::ChildKind::Count) ? kind : 0]
                [o.v == road::MemberV::Assign ? 0 : o.v == road::MemberV::HostKept ? 1 : 2]
                    .fetch_add(1, std::memory_order_relaxed);
    if (o.v == road::MemberV::HostKept) {
        (recorded ? g_road_host_kept : g_road_host_kept_unrec).fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const size_t cls = road::class_of(type);
    p::Event e;
    e.qpc_us = p8_now_us(); e.tid = GetCurrentThreadId();
    e.flags = mode == npcact::Mode::On && unit_assigned;
    e.v[0] = key; e.v[2] = root_key; e.v[5] = eax;
    if (o.v == road::MemberV::Assign) {
        // S40: el vehículo enlazado toma el controlador de su unidad (sólo si la unidad está asignada).
        if (assigned && mode == npcact::Mode::On) rax = (rax & ~static_cast<uintptr_t>(0xFFFFFFFF)) | controller;
        g_road_member[e.flags ? 0 : mode == npcact::Mode::On ? 1 : 0][cls].fetch_add(1, std::memory_order_relaxed);
        if (g_road_birth_cap.take(e.flags, kRoadBirthLines[e.flags])) {
            e.kind = kRvBirth;
            e.v[1] = type | static_cast<uint64_t>(4) << 8 | static_cast<uint64_t>(cls) << 24;
            e.v[3] = req; e.v[4] = controller;
            road_desc_fields(desc, e);
            e.v[6] = static_cast<uint64_t>(c.mark) << 24;
            e.v[10] = birth; e.v[11] = unit_birth;
            g_road_events.push(e);
        }
        return;
    }
    if (o.why == road::Split::PlayerRides) g_road_linked_rides.fetch_add(1, std::memory_order_relaxed);
    g_road_foreign.fetch_add(1, std::memory_order_relaxed);
    g_road_split_why[static_cast<size_t>(o.why)].fetch_add(1, std::memory_order_relaxed);
    if (g_road_misc_cap.take(1, kRoadMiscLines[1])) {
        e.kind = kRvSplit;
        e.v[1] = type | static_cast<uint64_t>(3) << 8 | static_cast<uint64_t>(cls) << 24;
        e.v[6] = static_cast<uint64_t>(o.why) | static_cast<uint64_t>(o.kind) << 8;
        e.v[7] = vt;
        e.v[8] = c.wrap;
        g_road_events.push(e);
    }
}
// El mid 0x2792A7B (después de U-e y de las familias, sólo si no cambiaron eax). Coste por creación ajena: un atómico,
// la vtable (una lectura protegida) y, con una unidad abierta en este hilo, el +0x2F0.
void road_control_impl(uintptr_t& rax, uintptr_t desc, uintptr_t field, uintptr_t actor) noexcept {
    const npcact::Mode mode = road_mode();
    if (mode == npcact::Mode::Off) return;
    uintptr_t vt = 0;
    if (!rd(desc, vt)) return;
    if (vt != g_base + road::kVtDescBase) {
        if (t_road.n) road_foreign_child(rax, desc, field, actor, mode);
        return;
    }
    const road::Prov p = road::read_prov(FamRead{}, desc, g_base, g_image_size);
    if (!road::marked(p)) {
        g_road_v[static_cast<size_t>(road::Verdict::NotMarked)][3].fetch_add(1, std::memory_order_relaxed);
        // Revisión §2: un descriptor base sin marca enlazado a una unidad abierta es un hijo ajeno (parte la unidad);
        // S40: salvo la carga de un gimmick (host-kept) y el vehículo enlazado de un tipo 5 (sigue a su unidad).
        if (t_road.n) road_foreign_child(rax, desc, field, actor, mode);
        return;
    }
    const uint32_t key = rdv<uint32_t>(actor + road::kActor_Key);
    if ((key >> 28) != 0xB) return;
    const uint64_t t0 = p8_now_us();
    road_parity_birth(key, p, desc, actor);
    const npcfam::LinkRead link = npcfam::read_parent(FamRead{}, desc);
    if (link.state == npcfam::Link::Present) {
        road_member(rax, desc, field, actor, p, link.parent, key, mode);
    } else if (link.state == npcfam::Link::Absent) {
        road_root(rax, desc, field, actor, p, key, mode);
    } else {
        g_road_link_unknown.fetch_add(1, std::memory_order_relaxed);  // ilegible: nunca raíz ni miembro
    }
    road_cost(t0);
}
// Cierre de las unidades que este hilo abrió a `depth` o más (fin de su Troc, o una salida perdida): censo, punteros
// olvidados; una unidad partida (modo 1) pasa a retirarse.
void road_close_depth(uint32_t depth) noexcept {
    RoadTls& t = t_road;
    uint8_t ok = 0;
    for (uint8_t i = 0; i < t.on && i < 16; ++i) {
        if (t.odepth[i] >= depth) continue;
        t.orphan[ok] = t.orphan[i];
        t.oparent[ok] = t.oparent[i];
        t.odepth[ok] = t.odepth[i];
        ++ok;
    }
    t.on = ok;
    if (!t.n) return;
    const uint64_t now = p8_now_us();
    uint8_t keep = 0;
    for (uint8_t i = 0; i < t.n && i < 16; ++i) {
        if (t.udepth[i] < depth) {
            t.unit[keep] = t.unit[i];
            t.udepth[keep] = t.udepth[i];
            ++keep;
            continue;
        }
        p::Event e;
        bool retire = false;
        {
            RoadLock guard;
            road::Unit& u = g_road.u[t.unit[i]];
            if (u.state != road::UState::Open || u.tid != GetCurrentThreadId()) continue;
            if (g_road.close(static_cast<size_t>(t.unit[i]), now)) {
                g_road_split_why[static_cast<size_t>(road::Split::NotBorn)].fetch_add(1, std::memory_order_relaxed);
            }
            retire = u.state == road::UState::Retire;
            uint64_t classes = 0;
            uint8_t members = 0, kept = 0;
            for (uint8_t k = 0; k < u.n; ++k) {
                if (!u.r[k].born) continue;
                if (u.r[k].host_kept) ++kept;
                const size_t c = road::class_of(u.r[k].type);
                if (((classes >> (8 * c)) & 0xFF) < 0xFF) classes += 1ull << (8 * c);
                if (k && u.r[k].assigned) ++members;
            }
            e.kind = kRvCensus; e.qpc_us = now; e.tid = GetCurrentThreadId(); e.flags = u.assigned;
            e.v[0] = u.r[0].key; e.v[1] = u.birth;
            e.v[2] = static_cast<uint64_t>(u.split) | static_cast<uint64_t>(retire) << 1;
            e.v[3] = static_cast<uint64_t>(u.n) | static_cast<uint64_t>(u.nodes) << 8 |
                     static_cast<uint64_t>(u.born) << 16 | static_cast<uint64_t>(u.late) << 24 |
                     static_cast<uint64_t>(members) << 32 | static_cast<uint64_t>(kept) << 40;
            e.v[4] = static_cast<uint64_t>(u.split_why); e.v[5] = classes; e.v[6] = u.req_key; e.v[7] = u.controller;
            // S40 carros: su clase de carretera y su composición (jinetes, vehículos enlazados, gimmicks, carga,
            // ajenos), y el libro por clase del [resumen carros].
            const road::Composition o = road::compose(u);
            e.v[8] = u.road;
            e.v[9] = static_cast<uint64_t>(o.riders) | static_cast<uint64_t>(o.linked) << 8 |
                     static_cast<uint64_t>(o.linked_assigned) << 16 | static_cast<uint64_t>(o.gimmicks) << 24 |
                     static_cast<uint64_t>(o.cargo) << 32 | static_cast<uint64_t>(o.foreign) << 40 |
                     static_cast<uint64_t>(o.max_depth) << 48 | static_cast<uint64_t>(o.incomplete) << 56;
            e.v[10] = o.root_type;
            (o.root_type == 5 ? g_road_book_carts : g_road_book).note(u);
        }
        if (retire) g_road_pending.store(true, std::memory_order_relaxed);
        if (g_road_misc_cap.take(2, kRoadMiscLines[2])) g_road_events.push(e);
        // Revisión §3.2: una unidad partida cuenta para su clase de carretera (bucle nace/retira).
        if (e.v[2] & 1) {
            bool changed = false, stop = false;
            const bool assigned = e.flags != 0;
            const bool cart = (e.v[10] & 0xFF) == 5;
            uint32_t road = 0;
            {
                // S40: sólo las unidades ASIGNADAS cuentan para el bucle real; las demás (probe, carros en probe con
                // el modo 1) para el suyo, que sólo dice «pararía». Revisión §6: las asignadas de raíz tipo 5, para el
                // de los carros (su parada global sólo para los carros).
                RoadLock guard;
                road = g_road.u[t.unit[i]].road;
                road::SplitGuard& sg = !assigned ? g_road_split_would : cart ? g_road_split_carts : g_road_split_guard;
                changed = sg.note(road, now);
                stop = sg.stop;
            }
            g_road_split_units[assigned].fetch_add(1, std::memory_order_relaxed);
            if (changed) {
                p::Event l;
                l.kind = kRvSplitLoop; l.qpc_us = now; l.tid = GetCurrentThreadId(); l.flags = assigned;
                l.v[0] = road; l.v[1] = stop;
                g_road_events.push(l);
                if (stop && assigned && road_mode() == npcact::Mode::On) {
                    if (cart) road_cart_stop(road::Stop::SplitLoop, e.v[0] & 0xFFFFFFFF);
                    else road_stop(road::Stop::SplitLoop, e.v[0] & 0xFFFFFFFF);
                }
            }
        }
    }
    t.n = keep;
}

// ---------------------------------------------------------------- S38: retirada estricta de los viajeros del invitado
// SAM::FindActor + ServerActor::Logout(25) como la limpieza de U-e, pero con su propio estado de fallo (road_stop, no
// g_activation_fault / g_ctl_aborted), resultado ESTRICTO (un error desconocido de Logout nunca es «hecho»),
// desaparición confirmada (el actor deja de estar en el SAM), publicación cancelada antes (child_hold_retire_family:
// tombstones y colas de la unidad), hojas a raíz y continuación en CUALQUIER Troc del servidor (también del anfitrión),
// aunque el invitado ya no envíe ninguno.
int road_fault(EXCEPTION_POINTERS* ep) noexcept {
    road_stop(road::Stop::Fault, t_road.calling_key);
    g_road_faults.fetch_add(1, std::memory_order_relaxed);
    return game::game_fault_continue(ep, "User2Road", "limpieza de viajeros: SAM::FindActor / ServerActor::Logout (motivo 25)");
}
// SAM::FindActor (+ Logout motivo 25 si `logout`). Antes de Logout se revalida: clave y controlador (los del registro),
// dueño (+0xA0 == 0: no es hijo de ningún usuario) y que ningún jugador lo monte (revisión §3.3). peek_ctl: sin
// controlador esperado (un hijo retenido de la unidad): devuelve el suyo en *peek_ctl y Present.
road::Call road_native(uintptr_t sam, uint32_t key, uint32_t ctl, uint32_t err, uint32_t already, bool logout,
                       uint32_t* peek_ctl = nullptr) noexcept {
    ActorScope sc{};
    int out = 0;
    road::Call r = road::Call::NotFound;
    __try {
        reinterpret_cast<FindActorFn>(g_base + kFindActorFn)(sam, &sc, key);
        const uintptr_t actor = sc.q[1];
        if ((sc.q[2] & 0xFF) && actor) {
            uint32_t k = 0, c = 0;
            uint64_t owner = 0;
            const bool known = rd(actor + road::kActor_Key, k) && rd(actor + road::kActor_Ctl, c) &&
                               rd(actor + road::kActor_User, owner);
            if (!known) {
                r = road::Call::Unreadable;  // ilegible no demuestra nada: no se llama a Logout
            } else if (peek_ctl) {
                *peek_ctl = c;
                r = k == key && !owner ? road::Call::Present : road::Call::NotOurs;
            } else if (k != key || c != ctl) {
                r = road::Call::NotOurs;
            } else if (owner) {
                g_road_owned.fetch_add(1, std::memory_order_relaxed);
                r = road::Call::NotOurs;  // ahora es de un usuario (montura, mascota): no es nuestro, no se toca
            } else if (!logout) {
                r = road::Call::Present;
            } else if (const road::Ride ride = road_ride(key, actor,
                                                         g_road_carts.load(std::memory_order_relaxed) != 0 &&
                                                             road_type(actor) == 5,
                                                         true);
                       ride != road::Ride::Free) {
                // Un jugador lo monta (o, S40 revisión §4, un tipo 5 sin prueba de que nadie lo monta): nunca Logout;
                // reintento cada segundo, terminal visible a los ~120 s.
                if (ride == road::Ride::Rides) g_road_occupied.fetch_add(1, std::memory_order_relaxed);
                r = ride == road::Ride::Rides ? road::Call::Occupied : road::Call::RideUnknown;
            } else {
                t_road.calling_key = key;
                reinterpret_cast<LogoutFn>(g_base + kLogoutFn)(actor, &out, 0x19, err);
                t_road.calling_key = 0;
                r = road::logout_call(static_cast<uint32_t>(out), already);
            }
        }
        sc.q[0] = g_base + kVtScopeBase;
        reinterpret_cast<ScopeDtorFn>(g_base + kScopeDtorFn)(&sc);
    } __except (road_fault(GetExceptionInformation())) {
        r = road::Call::Fault;
    }
    g_road_calls[static_cast<size_t>(r)].fetch_add(1, std::memory_order_relaxed);
    return r;
}
// Lo que una pasada necesita de una unidad, copiado bajo el cerrojo (las llamadas nativas van sin él). S40 revisión §8:
// ~5 KB con 48 registros: fuera de la pila (road_cleanup_run se alcanza desde el mid on_logout y no debe llevar
// __chkstk). Una sola copia estática basta: road_cleanup_run sólo corre con la barrera g_p_draining tomada (exclusiva
// en todo el proceso, nunca reentrante: ningún camino de su Logout vuelve a road_cleanup_run).
struct RoadSnap {
    uint64_t birth = 0;
    uint32_t epoch = 0;
    bool pub_cancelled = false;
    road::Unit unit;  // copia de trabajo: sólo los registros
};
RoadSnap g_road_snap;  // sólo bajo g_p_draining (road_cleanup_run)
void road_snap(const road::Unit& u, road::Unit& out) noexcept {
    out.n = u.n;
    for (uint8_t k = 0; k < u.n; ++k) {
        const road::Rec& a = u.r[k];
        road::Rec& b = out.r[k];
        b.key = a.key; b.ctl = a.ctl; b.parent = a.parent; b.depth = a.depth; b.born = a.born; b.clean = a.clean;
        b.tries = a.tries; b.checks = a.checks; b.leave_us = a.leave_us; b.retry_us = a.retry_us; b.removed = a.removed;
        b.birth = a.birth; b.incomplete = a.incomplete; b.occupied = a.occupied; b.host_kept = a.host_kept;
    }
}
void road_apply(road::Unit& u, const road::Unit& w) noexcept {
    for (uint8_t k = 0; k < u.n && k < w.n; ++k) {
        road::Rec& a = u.r[k];
        const road::Rec& b = w.r[k];
        if (a.key != b.key || a.birth != b.birth) continue;
        if (a.clean == road::Clean::Gone) continue;  // una baja nativa vista mientras tanto
        a.clean = b.clean; a.tries = b.tries; a.checks = b.checks; a.leave_us = b.leave_us; a.retry_us = b.retry_us;
        a.occupied = b.occupied;
    }
}
// Revisión §4.3: justo antes de la llamada nativa, el registro sigue siendo el mismo (clave y nacimiento).
bool road_same_rec(int ui, uint8_t k, uint32_t key, uint64_t birth) noexcept {
    RoadRead read;
    const road::Loc l = g_road.find(key);
    const bool same = l.unit == ui && l.rec == k && g_road.u[ui].r[k].birth == birth;
    if (!same) g_road_reborn_skip.fetch_add(1, std::memory_order_relaxed);
    return same;
}
// Revisión §4.1 (b): los hijos de la unidad que observer_hold_family retiene (hijos sólo de jerarquía: los «Game
// Mechanic» de un viajero retenido al anfitrión) entran en la unidad como host-kept, validados en el SAM, para que la
// cancelación de su publicación y su retirada vayan juntas. Sin sitio: su padre queda incompleto (nunca se retira).
// Devuelve false si child_hold estaba ocupado (otra pasada).
bool road_add_held(uintptr_t sam, int ui, uint64_t birth, uint32_t epoch, const uint32_t* keys, size_t nk,
                   uint32_t err, uint32_t already) noexcept {
    uint32_t hk[32], hp[32];
    const int n = child_hold_held_children(epoch, std::span<const uint32_t>(keys, nk), hk, hp, std::size(hk));
    if (n < 0) return false;
    for (int i = 0; i < n && i < 32; ++i) {
        uint32_t ctl = 0;
        if (road_native(sam, hk[i], 0, err, already, false, &ctl) != road::Call::Present) continue;
        RoadLock guard;
        road::Unit& u = g_road.u[ui];
        if (u.state != road::UState::Retire || u.birth != birth) return true;
        if (g_road.find(hk[i]).unit == ui) continue;
        int pi = -1;
        for (uint8_t k = 0; k < u.n; ++k) if (u.r[k].key == hp[i] && u.r[k].born) pi = k;
        if (pi < 0) continue;
        const int ri = g_road.add_late(static_cast<size_t>(ui), 0, static_cast<uint8_t>(pi));
        if (ri >= 0 && g_road.born(static_cast<size_t>(ui), static_cast<size_t>(ri), hk[i], road_next_birth(), 7, false,
                                   p8_now_us())) {
            u.r[ri].ctl = ctl;
            u.r[ri].host_kept = true;
            g_road_held_added.fetch_add(1, std::memory_order_relaxed);
        } else {
            u.r[pi].incomplete = true;
            g_road_held_no_room.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return true;
}
void road_cleanup_run(uint16_t op) noexcept {
    if (road_mode() != npcact::Mode::On || !g_cleanup_ok.load(std::memory_order_relaxed) || !server_realm()) return;
    if (!g_road_pending.load(std::memory_order_relaxed)) return;
    const uintptr_t lookup = rdp(g_base + kLookupSlot);
    const uintptr_t sam = lookup ? rdp(lookup) : 0;
    if (!sam) {
        g_road_no_sam.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    ModLock lock(2'000);  // revisión §4.4: como mucho 2 ms de espera en un hilo del servidor; ocupado: otra pasada
    if (!lock.ok) {
        g_road_busy.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // La barrera de Trocs del usuario 2 (la misma que la contención y prep_drain): ningún Troc suyo publica ni crea
    // mientras se cancela su publicación y se retiran sus actores.
    bool expected = false;
    if (!g_p_draining.compare_exchange_strong(expected, true)) {
        g_road_busy.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    struct Barrier { ~Barrier() { g_p_draining.store(false, std::memory_order_release); } } barrier;
    if (user2_funnels_trocs_in_flight() > static_cast<int32_t>(t_troc.depth)) {
        g_road_busy.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_road_runs.fetch_add(1, std::memory_order_relaxed);
    // S40 revisión §1/§3: KeptChanged sólo con user2_road_carts != 0 (con 0, exactamente la limpieza de ef5a6b5).
    const bool kept_rule = g_road_carts.load(std::memory_order_relaxed) != 0;
    const uint32_t err = rdv<uint32_t>(g_base + kLogoutErrSlot), already = rdv<uint32_t>(g_base + kAlreadyLogoutingSlot);
    const uint64_t start = p8_now_us();
    // Revisión §4.4: las 8 unidades menos recientemente atendidas (no las primeras por índice); las que esperan su
    // siguiente intento de cancelación no cuentan.
    int order[8];
    size_t no = 0;
    {
        RoadRead read;
        for (size_t i = 0; i < g_road.capacity(); ++i) {
            const road::Unit& u = g_road.u[i];
            if (u.state != road::UState::Retire || (!u.pub_cancelled && start < u.pub_next_us)) continue;
            if (no < 8) {
                order[no++] = static_cast<int>(i);
                continue;
            }
            size_t worst = 0;
            for (size_t j = 1; j < no; ++j) if (g_road.u[order[j]].pass_us > g_road.u[order[worst]].pass_us) worst = j;
            if (u.pass_us < g_road.u[order[worst]].pass_us) order[worst] = static_cast<int>(i);
        }
    }
    size_t calls = 0;
    for (size_t oi = 0; oi < no && calls < 32 && !road_faulted(); ++oi) {
        const int ui = order[oi];
        RoadSnap& s = g_road_snap;  // S40 revisión §8: fuera de la pila (bajo la barrera g_p_draining)
        {
            RoadLock guard;
            road::Unit& u = g_road.u[ui];
            if (u.state != road::UState::Retire) continue;
            u.pass_us = start;
            s.birth = u.birth; s.epoch = u.epoch; s.pub_cancelled = u.pub_cancelled;
            road_snap(u, s.unit);
        }
        const uint64_t now = p8_now_us();
        bool pub_refused = false, pub_busy = false;
        // Otra época: su publicación ya no existe (la sesión de ese invitado se fue); no hay nada que cancelar.
        if (s.epoch != server_epoch()) s.pub_cancelled = true;
        if (!s.pub_cancelled) {
            // Publicación pendiente cancelada ANTES de cualquier Logout (INFORME-A3 cambio 4): claves validadas ahora en
            // scopes nativos (presentes, nuestras); las que ya no están quedan fuera. Revisión §4.1: los hijos que
            // observer_hold_family retiene de la unidad entran antes en ella (y en la lista).
            uint32_t keys[road::kMaxUnit];
            size_t nk = 0;
            {
                road::Unit& w = s.unit;
                for (uint8_t k = 0; k < w.n; ++k) {
                    if (w.r[k].born && w.r[k].clean != road::Clean::Gone) keys[nk++] = w.r[k].key;
                }
            }
            if (!road_add_held(sam, ui, s.birth, s.epoch, keys, nk, err, already)) {
                pub_busy = true;
            } else {
                {
                    RoadRead read;
                    road_snap(g_road.u[ui], s.unit);  // con los hijos retenidos añadidos
                }
                road::Unit& w = s.unit;
                nk = 0;
                for (uint8_t k = 0; k < w.n; ++k) {
                    road::Rec& c = w.r[k];
                    if (!c.born || c.clean == road::Clean::Gone || c.clean == road::Clean::Leaving ||
                        c.clean == road::Clean::Failed || c.clean == road::Clean::Unconfirmed) {
                        continue;
                    }
                    // S40 revisión §3: un host-kept vivo con otro controlador / dueño no es «fuera» (KeptChanged).
                    const road::Call r = road::kept_result(road_native(sam, c.key, c.ctl, err, already, false),
                                                           c.host_kept && kept_rule);
                    ++calls;
                    if (r == road::Call::KeptChanged) g_road_kept_changed.fetch_add(1, std::memory_order_relaxed);
                    if (r == road::Call::Present) keys[nk++] = c.key;
                    else road::apply_call(c, r, true, now);
                }
                if (road_faulted()) break;
                if (nk && activation_on() &&
                    !child_hold_retire_family(s.epoch, road_next_birth(), std::span<const uint32_t>(keys, nk))) {
                    pub_refused = true;
                }
            }
        }
        road::Unit& w = s.unit;
        if (!pub_refused && !pub_busy) {
            // Hojas a raíz: un registro con un descendiente aún no fuera espera a otra pasada.
            road::Plan plan;
            road::leaves_first(w, plan);
            for (uint8_t i = 0; i < plan.n && calls < 32; ++i) {
                const uint8_t k = plan.idx[i];
                road::Rec& c = w.r[k];
                const road::Block b = road::block_state(w, k);
                if (b == road::Block::Terminal) {
                    c.clean = road::Clean::Failed;  // un descendiente suyo no saldrá: él tampoco se retira (visible)
                    continue;
                }
                if (b == road::Block::Wait) continue;
                if (c.clean == road::Clean::Pending && now < c.retry_us) continue;
                if (!road_same_rec(ui, k, c.key, c.birth)) {
                    c.clean = road::Clean::Gone;  // otra encarnación de la clave: no es nuestra
                    continue;
                }
                const bool check = c.clean == road::Clean::Leaving;
                road::Call r = road_native(sam, c.key, c.ctl, err, already, !check);
                // S40 revisión §3: antes de NUESTRO Logout, un host-kept vivo con otro controlador / dueño no cuenta como
                // fuera (su padre nunca se retira con él vivo); tras él, NotOurs es otra encarnación (fuera).
                if (!check) r = road::kept_result(r, c.host_kept && kept_rule);
                if (r == road::Call::KeptChanged) g_road_kept_changed.fetch_add(1, std::memory_order_relaxed);
                ++calls;
                road::apply_call(c, r, check, now);
                if (r == road::Call::Fault) break;
            }
        }
        p::Event e;
        bool line = false;
        {
            RoadLock guard;
            road::Unit& u = g_road.u[ui];
            if (u.state != road::UState::Retire || u.birth != s.birth) continue;
            road_apply(u, w);
            if (pub_refused || pub_busy) {
                (pub_busy ? g_road_pub_busy : g_road_pub_refused).fetch_add(1, std::memory_order_relaxed);
                // Revisión §4.1: espaciado creciente y presupuesto de tiempo (no 10 pasadas): FALLIDA sólo pasado él.
                if (road::pub_refused(u, now) == road::PubNext::Failed) {
                    for (uint8_t k = 0; k < u.n; ++k) {
                        if (u.r[k].born && u.r[k].clean != road::Clean::Gone) u.r[k].clean = road::Clean::Failed;
                    }
                }
            } else {
                u.pub_cancelled = true;
            }
            const road::UState st = g_road.settle(static_cast<size_t>(ui), now);
            if (st == road::UState::Done || st == road::UState::Failed) {
                (st == road::UState::Done ? g_road_units_done : g_road_units_failed).fetch_add(1, std::memory_order_relaxed);
                line = true;
                uint8_t gone = 0, failed = 0, unconf = 0, nborn = 0;
                for (uint8_t k = 0; k < u.n; ++k) {
                    if (!u.r[k].born) continue;
                    ++nborn;
                    if (u.r[k].clean == road::Clean::Gone) ++gone;
                    else if (u.r[k].clean == road::Clean::Failed) ++failed;
                    else if (u.r[k].clean == road::Clean::Unconfirmed) ++unconf;
                }
                e.kind = kRvRetired; e.qpc_us = now; e.tid = GetCurrentThreadId();
                e.flags = st == road::UState::Failed;
                e.v[0] = u.r[0].key; e.v[1] = u.birth; e.v[2] = static_cast<uint64_t>(u.why);
                e.v[3] = nborn | static_cast<uint64_t>(gone) << 8 | static_cast<uint64_t>(failed) << 16 |
                         static_cast<uint64_t>(unconf) << 24 | static_cast<uint64_t>(u.pub_tries) << 32;
                e.v[4] = u.retire_us && now >= u.retire_us ? now - u.retire_us : 0; e.v[5] = op;
            }
        }
        if (line && (e.flags || g_road_misc_cap.take(3, kRoadMiscLines[3]))) g_road_events.push(e);
    }
    {
        RoadRead read;
        g_road_pending.store(g_road.pending(), std::memory_order_relaxed);
    }
}
// Su carga / su Logout / su 2001 (hilo del servidor): las unidades asignadas vivas pasan a retirarse y se retiran ya
// (lo que no quepa en esta pasada continúa en los Trocs siguientes, de quien sean). probe: sólo se cuenta, y (revisión
// §4.1 c) cuántas de sus unidades verían negada la cancelación de su publicación por hijos retenidos.
void road_request(road::Why why, uint16_t op) noexcept {
    const npcact::Mode mode = road_mode();
    if (mode == npcact::Mode::Off || !g_road_live.load(std::memory_order_relaxed)) return;
    const uint64_t now = p8_now_us();
    size_t n = 0, would = 0;
    // probe: hasta 16 unidades medidas por petición (sus claves copiadas bajo el cerrojo; child_hold fuera de él).
    uint32_t keys[16][road::kMaxUnit];
    uint8_t nkeys[16] = {};
    uint32_t epochs[16] = {};
    size_t measured = 0;
    {
        RoadLock guard;
        if (mode == npcact::Mode::On) n = g_road.request(why, now);
        for (size_t i = 0; i < g_road.capacity(); ++i) {
            const road::Unit& u = g_road.u[i];
            if (u.assigned || (u.state != road::UState::Live && u.state != road::UState::Open) ||
                !road::Table<256>::anyone_alive(u)) {
                continue;
            }
            ++would;
            if (measured >= 16) continue;
            uint8_t nk = 0;
            for (uint8_t k = 0; k < u.n; ++k) {
                if (u.r[k].born && !u.r[k].removed) keys[measured][nk++] = u.r[k].key;
            }
            nkeys[measured] = nk;
            epochs[measured] = u.epoch;
            ++measured;
        }
    }
    for (size_t m = 0; m < measured; ++m) {
        uint32_t hk[32], hp[32];
        const int h = child_hold_held_children(epochs[m], std::span<const uint32_t>(keys[m], nkeys[m]), hk, hp,
                                               std::size(hk));
        if (h < 0) {
            g_road_pub_would_busy.fetch_add(1, std::memory_order_relaxed);
        } else if (h > 0) {
            g_road_pub_would_refuse.fetch_add(1, std::memory_order_relaxed);
            g_road_pub_would_children.fetch_add(static_cast<uint64_t>(h), std::memory_order_relaxed);
        }
    }
    g_road_requests.fetch_add(1, std::memory_order_relaxed);
    g_road_req_units.fetch_add(n, std::memory_order_relaxed);
    g_road_would_units_at_request.fetch_add(would, std::memory_order_relaxed);
    if (n || would) {
        p::Event e;
        e.kind = kRvRequest; e.qpc_us = now; e.tid = GetCurrentThreadId(); e.flags = mode == npcact::Mode::On;
        e.v[0] = static_cast<uint64_t>(why); e.v[1] = n; e.v[2] = would; e.v[3] = op;
        g_road_events.push(e);
    }
    if (n) {
        g_road_pending.store(true, std::memory_order_relaxed);
        road_cleanup_run(op);
    }
}
// Continuación (cualquier Troc del servidor al salir, como mucho cada 100 ms): una unidad pendiente se sigue retirando
// aunque el invitado ya no mande Trocs (desconexión).
void road_continue() noexcept {
    if (road_mode() != npcact::Mode::On || !g_road_pending.load(std::memory_order_relaxed)) return;
    const uint64_t now = p8_now_us();
    uint64_t next = g_road_next_us.load(std::memory_order_relaxed);
    if (now < next || !g_road_next_us.compare_exchange_strong(next, now + 100'000)) return;
    road_cleanup_run(0);
}
// Punto O (hilo del servidor): una baja de una clave registrada. La entrada de NUESTRO Logout no es una baja: la
// confirma road_cleanup_run.
// Rotación nativa (informe churn-viajeros §3.3): la baja de un viajero seguido por la paridad (del invitado o del
// anfitrión): su posición en el servidor, sus distancias a los dos personajes, el radio de su spawn
// [[actor+0x68]+0x20]+0x468 y su caducidad +0x428 frente a _time64 (lecturas protegidas). Separa la rama de distancia
// del recolector 0x2C37340 de la de caducidad; todas se cuentan, las líneas se muestrean por lado y carretera.
void road_parity_logout(uint32_t key, uint8_t reason, uintptr_t actor) noexcept {
    if (!key || !g_parity_live.load(std::memory_order_relaxed)) return;
    const bool ours = key == t_road.calling_key;  // nuestro Logout: se olvida sin muestra de vida
    road::ParityDeath d;
    {
        ParityLock guard;
        d = g_parity.death(key, reason, p8_now_us(), !ours);
    }
    if (!d.found || ours) return;
    road::GoneIn g;
    u2s::World a, h, u;
    g.pos_ok = actor_world(actor, a);
    const auto dist = [&](uintptr_t chr, u2s::World& w, float& out) {
        if (!g.pos_ok || !chr || !actor_world(chr, w)) return false;
        const double dx = a.x - w.x, dy = a.y - w.y, dz = a.z - w.z;
        // sqrtsd sin el CRT (std::sqrt puede llamar a sqrt por errno dentro del mid)
        out = static_cast<float>(_mm_cvtsd_f64(_mm_sqrt_sd(_mm_setzero_pd(), _mm_set_sd(dx * dx + dy * dy + dz * dz))));
        return true;
    };
    g.d_u2_ok = dist(g_u2_char.load(std::memory_order_relaxed), u, g.d_u2);
    g.d_host_ok = dist(g_host_char.load(std::memory_order_relaxed), h, g.d_host);
    const uintptr_t comps = actor ? rdp(actor + u2s::kActor_Comps) : 0;
    const uintptr_t spawn = comps ? rdp(comps + road::kComps_Spawn) : 0;
    g.radius_ok = spawn && rd(spawn + road::kSpawn_Radius, g.radius);
    g.expiry_ok = spawn && rd(spawn + road::kSpawn_Expiry, g.expiry);
    {
        // _time64 (Unix seconds) sin el CRT dentro del mid: FILETIME (100 ns desde 1601) -> segundos desde 1970.
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        const uint64_t t = static_cast<uint64_t>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;
        g.now = static_cast<int64_t>((t - 116444736000000000ull) / 10000000ull);
    }
    const road::Gone b = road::gone_branch(g);
    const size_t side = d.side == road::Side::Guest ? 0 : 1;
    g_road_gone[side][static_cast<size_t>(b)].fetch_add(1, std::memory_order_relaxed);
    uint32_t ord = 0;
    if (!g_road_gone_sample.take(road::sample_key(d.road, side == 0), false, &ord)) return;
    const auto fb = [](float f) { return static_cast<uint64_t>(std::bit_cast<uint32_t>(f)); };
    p::Event e;
    e.kind = kRvGone; e.qpc_us = p8_now_us(); e.tid = GetCurrentThreadId();
    e.flags = static_cast<uint16_t>((g.pos_ok ? 1 : 0) | (g.d_u2_ok ? 2 : 0) | (g.d_host_ok ? 4 : 0) |
                                    (g.radius_ok ? 8 : 0) | (g.expiry_ok ? 16 : 0));
    e.v[0] = key;
    e.v[1] = d.type | static_cast<uint64_t>(side) << 8 | static_cast<uint64_t>(reason) << 16 |
             static_cast<uint64_t>(b) << 24;
    e.v[2] = d.road;
    e.v[3] = d.life_us;
    e.v[4] = fb(static_cast<float>(a.x)) | fb(static_cast<float>(a.y)) << 32;
    e.v[5] = fb(static_cast<float>(a.z));
    e.v[6] = fb(g.d_u2) | fb(g.d_host) << 32;
    e.v[7] = fb(g.radius);
    e.v[8] = static_cast<uint64_t>(g.expiry);
    e.v[9] = static_cast<uint64_t>(g.now);
    e.v[10] = ord;
    g_road_events.push(e);
}
void road_on_logout(uint32_t key, uint8_t reason, uintptr_t actor) noexcept {
    road_parity_logout(key, reason, actor);
    if (!key || !g_road_live.load(std::memory_order_relaxed) || key == t_road.calling_key) return;
    RoadLock guard;
    const road::Loc l = g_road.on_logout(key, reason);
    if (!l) return;
    g_road_removed[reason == 25 ? 0 : reason == 42 ? 1 : reason == 44 ? 2 : 3].fetch_add(1, std::memory_order_relaxed);
}
// La publicación (child_hold, observer_hold_family) ve un hijo de JERARQUÍA de un viajero registrado que no es de su
// unidad (nacido fuera del Troc de su raíz: p. ej. un «Game Mechanic» tipo 7 enlazado después). Segunda revisión de la
// fase B (R4): sólo el linaje de descriptor (+0x2F0) parte una unidad; esto se cuenta y se apunta (una vez por clave y
// época), nunca retira. Medida pendiente: si son frecuentes con controlador distinto, decidir con datos.
npcfam::KeyMarks<1024> g_road_hier_seen;
std::atomic<uint64_t> g_road_hier_late{0}, g_road_hier_late_other{0};
void road_family_observed(uint32_t key, uint32_t parent, uint32_t controller, uint8_t type) noexcept {
    if (!parent || !g_road_live.load(std::memory_order_relaxed)) return;
    uint32_t root = 0, expected = 0;
    bool assigned = false, cart_unit = false;
    {
        RoadRead read;
        const road::Loc pl = g_road.find(parent);
        if (!pl) return;
        const road::Unit& u = g_road.u[pl.unit];
        const road::Loc cl = g_road.find(key);
        if (cl && cl.unit == pl.unit) return;  // de su unidad
        root = u.r[0].key;
        expected = u.r[pl.rec].ctl;
        assigned = u.r[pl.rec].assigned;
        cart_unit = u.assigned && road_cart_unit(pl);
    }
    const uint32_t epoch = server_epoch();
    if (g_road_hier_seen.contains(key, epoch)) return;
    g_road_hier_seen.insert(key, epoch);
    // S40 revisión §3: en una unidad ASIGNADA de raíz tipo 5 (sólo con user2_road_carts != 0), un hijo nacido fuera de su
    // Troc (p. ej. carga tardía de un gimmick) no tiene registro: su padre queda incompleto (nunca se retira con él vivo;
    // terminal y visible). Los caminantes siguen con la regla R4 de la fase B (sólo se cuenta).
    if (cart_unit && g_road_carts.load(std::memory_order_relaxed) != 0) {
        RoadLock guard;
        const road::Loc pl = g_road.find(parent);
        if (pl && road_cart_unit(pl) && !g_road.u[pl.unit].r[pl.rec].incomplete) {
            g_road.u[pl.unit].r[pl.rec].incomplete = true;
            g_road_hier_cart_incomplete.fetch_add(1, std::memory_order_relaxed);
        }
    }
    g_road_hier_late.fetch_add(1, std::memory_order_relaxed);
    const bool other = controller != expected;
    if (other) g_road_hier_late_other.fetch_add(1, std::memory_order_relaxed);
    if (g_road_misc_cap.take(6, kRoadMiscLines[6])) {
        p::Event e;
        e.kind = kRvHierLate; e.qpc_us = p8_now_us(); e.tid = GetCurrentThreadId(); e.flags = assigned;
        e.v[0] = key; e.v[1] = type; e.v[2] = root; e.v[3] = parent; e.v[4] = controller; e.v[5] = expected;
        g_road_events.push(e);
    }
}
// Un 2005 (al invitado o capturado en el cliente del anfitrión): controlador y restore de las claves registradas.
void road_check_2005(const uint8_t* p, size_t n, bool to_host) noexcept {
    if (road_mode() == npcact::Mode::Off) return;
    const uint64_t now = p8_now_us();
    u2s::for_each_2005_rec(p, n, [&](uint8_t type, uint32_t key, uint32_t ctl, const uint8_t* rec, size_t len) {
        road::Check2005 c = road::Check2005::Ignore;
        bool assigned = false, cart = false;
        uint32_t expected = 0;
        // Paridad 1:1: un 2005 al invitado con metadatos de carretera de SU personaje.
        bool meta = false;
        if (!to_host) {
            const auto m = npcact::road_metadata(std::span<const uint8_t>(rec, len));
            meta = m.valid && m.id && m.requester && m.requester == g_u2_key.load(std::memory_order_relaxed);
        }
        if (!meta && !g_road_live.load(std::memory_order_relaxed)) return;
        {
            RoadLock guard;
            const road::Loc l = g_road.find(key);
            if (meta) {
                (l ? g_road_meta_registered : g_road_meta_unregistered)[road::class_of(type)].fetch_add(
                    1, std::memory_order_relaxed);
            }
            if (!l) return;
            road::Rec& r = g_road.u[l.unit].r[l.rec];
            if (!r.born || r.removed) return;
            assigned = r.assigned;
            expected = r.ctl;
            cart = road_cart_unit(l);
            const int restore = to_host ? s3::restore_byte_of(std::span<const uint8_t>(rec, len)) : -1;
            c = road::check_2005(assigned, expected, ctl, to_host, restore);
            if (to_host) r.host_2005 = true;
            else if (!r.q2005_us) r.q2005_us = now;
            if (!assigned) {
                (to_host ? g_road_would_host_2005 : g_road_would_guest_2005).fetch_add(1, std::memory_order_relaxed);
                if (ctl != expected) g_road_would_ctl_diff.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        if (to_host) {
            g_road_host_2005[c == road::Check2005::Ok ? 0 : c == road::Check2005::BadController ? 1 : 2].fetch_add(1);
        } else {
            g_road_guest_2005[c == road::Check2005::Ok ? 0 : 1].fetch_add(1);
        }
        if (c == road::Check2005::Ok) return;
        // S40 revisión §6: de una unidad de raíz tipo 5, sólo la parada de los carros.
        const road::Stop why = c == road::Check2005::BadController ? road::Stop::Bad2005 : road::Stop::Restore0ToHost;
        if (cart) road_cart_stop(why, key);
        else road_stop(why, key);
        if (g_road_misc_cap.take(4, kRoadMiscLines[4])) {
            p::Event e;
            e.kind = kRv2005; e.qpc_us = now; e.tid = GetCurrentThreadId(); e.flags = to_host;
            e.v[0] = key; e.v[1] = ctl; e.v[2] = expected; e.v[3] = static_cast<uint64_t>(c);
            g_road_events.push(e);
        }
    });
}
// Una Req de IA: del invitado (su primera, y su posición: ¿se mueve?) o del cliente del anfitrión (doble control).
void road_on_req(uint16_t op, uintptr_t payload, uint16_t size, bool guest) noexcept {
    if (!g_road_live.load(std::memory_order_relaxed) || !u2s::ai_req(op) || size < 4) return;
    uint32_t key = 0;
    if (!rd(payload, key)) return;
    float pos[3] = {0, 0, 0};
    size_t off = 0;
    const bool has_pos = guest && road::req_position_offset(op, off) && size >= off + 12 &&
                         game::fast_read(payload + off, pos, sizeof pos);
    bool assigned = false, cart = false;
    {
        RoadLock guard;
        const road::Loc l = g_road.find(key);
        if (!l) return;
        road::Rec& r = g_road.u[l.unit].r[l.rec];
        if (!r.born || r.removed) return;
        assigned = r.assigned;
        cart = road_cart_unit(l);
        if (guest) {
            if (!r.driven) {
                r.driven = true;
                r.req_us = p8_now_us();
            }
            if (has_pos) road::note_position(r, pos[0], pos[2]);
        }
    }
    if (guest) {
        g_road_guest_req.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!assigned) {
        g_road_host_req_would.fetch_add(1, std::memory_order_relaxed);  // probe: el anfitrión lo controla (normal)
        return;
    }
    const bool ctl_only = road::controller_only_req(op);
    (ctl_only ? g_road_host_req_ctl : g_road_host_req_2556).fetch_add(1, std::memory_order_relaxed);
    // S40 revisión §6: el anfitrión montado en un caballo de caravana asignado (o cualquier doble control de una unidad de
    // raíz tipo 5) para sólo los carros nuevos; los caminantes siguen. Las unidades ya asignadas, igual que con la
    // parada global (ninguna parada cambia un controlador ya escrito).
    if (ctl_only && road_mode() == npcact::Mode::On) {
        if (cart) road_cart_stop(road::Stop::DoubleControl, key);
        else road_stop(road::Stop::DoubleControl, key);
    }
    if (g_road_misc_cap.take(5, kRoadMiscLines[5])) {
        p::Event e;
        e.kind = kRvHostReq; e.qpc_us = p8_now_us(); e.tid = GetCurrentThreadId(); e.flags = ctl_only;
        e.v[0] = key; e.v[1] = op;
        g_road_events.push(e);
    }
}
// El hilo del módulo, cada segundo: época nueva, borrado y las líneas.
void road_tick() noexcept {
    if (road_mode() == npcact::Mode::Off) return;
    const uint32_t ep = server_epoch();
    const uint32_t seen = g_road_epoch_seen.exchange(ep, std::memory_order_relaxed);
    const uint64_t now = p8_now_us();
    RoadLock guard;
    if (seen && seen != ep && road_mode() == npcact::Mode::On && g_road.request_epoch(ep, now)) {
        g_road_pending.store(true, std::memory_order_relaxed);
    }
    g_road.prune(ep, now);  // también cierra unidades abiertas de un Troc que nunca terminó
    if (road_mode() == npcact::Mode::On && g_road.pending()) g_road_pending.store(true, std::memory_order_relaxed);
}

// [resumen paridad] (cada 60 s, informe churn-viajeros §3.2): por clase (lado, carretera +0x3D0, tipo) altas/min de
// la ventana y vida mediana de las últimas road::kParityRing bajas nativas; para cada clase del invitado, su veredicto
// frente a la del anfitrión (misma carretera y tipo, o el mismo tipo) y un WRN sólo con muestras suficientes.
void road_parity_summary() {
    struct Row {
        road::Side side;
        uint8_t type;
        uint32_t road, med_ms, n;
        uint64_t rate_x10, births, deaths, r25, stale;
        int peer;
        bool same_road;
    };
    Row rows[road::kParityClasses];
    size_t nrows = 0;
    size_t live = 0, pruned = 0;
    uint64_t live_full = 0, class_full = 0, again = 0, window_s = 0;
    {
        ParityLock guard;
        const uint64_t now = p8_now_us();
        pruned = g_parity.prune(now);
        window_s = g_parity.win_start_us && now > g_parity.win_start_us ? (now - g_parity.win_start_us) / 1'000'000 : 0;
        int map[road::kParityClasses];
        for (size_t i = 0; i < road::kParityClasses; ++i) {
            map[i] = -1;
            const road::ParityClass& c = g_parity.c[i];
            if (!c.used) continue;
            map[i] = static_cast<int>(nrows);
            Row& r = rows[nrows++];
            r.side = c.side; r.type = c.type; r.road = c.road; r.med_ms = g_parity.median_ms(i); r.n = c.ring_n;
            r.rate_x10 = g_parity.win_start_us ? g_parity.rate_x10(i, now) : 0;
            r.births = c.births; r.deaths = c.deaths; r.r25 = c.r25; r.stale = c.stale;
            r.peer = -1; r.same_road = false;
        }
        for (size_t i = 0; i < road::kParityClasses; ++i) {
            if (map[i] < 0 || g_parity.c[i].side != road::Side::Guest) continue;
            const int p = g_parity.counterpart(i, &rows[map[i]].same_road);
            rows[map[i]].peer = p >= 0 ? map[p] : -1;
        }
        live = g_parity.live_n; live_full = g_parity.live_full; class_full = g_parity.class_full; again = g_parity.again;
        g_parity.new_window(now);
    }
    std::string text;
    for (size_t i = 0; i < nrows; ++i) {
        const Row& r = rows[i];
        text += std::format(" | {} 0x{:X} t{}: altas/min={}.{} vida_mediana_s={:.1f} (n={}) altas={} bajas={} (25={}) "
                            "sin_baja={}",
                            road::side_name(r.side), r.road, r.type, r.rate_x10 / 10, r.rate_x10 % 10, r.med_ms / 1000.0,
                            r.n, r.births, r.deaths, r.r25, r.stale);
        if (r.side != road::Side::Guest) continue;
        const Row* h = r.peer >= 0 ? &rows[r.peer] : nullptr;
        const road::ParityV v = road::parity_verdict(r.med_ms, r.n, h ? h->med_ms : 0, h ? h->n : 0);
        text += std::format(" -> frente a {}: {}",
                            h ? std::format("anfitrión 0x{:X} t{}{}", h->road, h->type, r.same_road ? "" : " (mismo tipo)")
                              : std::string("ninguna del anfitrión"),
                            road::parity_name(v));
        if ((v == road::ParityV::TooShort || v == road::ParityV::ShortVsHost) &&
            g_parity_wrn_cap.take(0, kParityWrnLines)) {
            log::warn("User2Road [paridad: rotación anómala] invitado 0x{:X} t{}: vida mediana {:.1f} s (n={}) frente a "
                      "{}: {} (la nativa: ~29 s las bandadas 0x1F5; mirar [viajero: baja] y WorldAnchor: ¿ancla perdida?)",
                      r.road, r.type, r.med_ms / 1000.0, r.n,
                      h ? std::format("anfitrión 0x{:X} t{} {:.1f} s (n={})", h->road, h->type, h->med_ms / 1000.0, h->n)
                        : std::string("ninguna del anfitrión"),
                      road::parity_name(v));
        }
    }
    const auto gone = [](size_t s) {
        return std::format("caducidad={} distancia={} dentro_de_un_ancla={} desconocida={}",
                           g_road_gone[s][static_cast<size_t>(road::Gone::Expired)].load(),
                           g_road_gone[s][static_cast<size_t>(road::Gone::OutOfRadius)].load(),
                           g_road_gone[s][static_cast<size_t>(road::Gone::InsideAnchor)].load(),
                           g_road_gone[s][static_cast<size_t>(road::Gone::Unknown)].load());
    };
    log::info("User2Road [resumen paridad] ventana_s={}{} | seguidos={} (tabla llena {}, clases llenas {}, repetidos {}, "
              "olvidados ahora sin baja {}) | bajas por rama: invitado {} | anfitrión {} | líneas por nacimiento: "
              "admitidas={} fuera_del_muestreo={} | líneas de baja: admitidas={} fuera_del_muestreo={}",
              window_s, text.empty() ? std::string(" | sin viajeros seguidos") : text, live, live_full, class_full, again,
              pruned, gone(0), gone(1), g_road_birth_sample.kept.load(), g_road_birth_sample.dropped.load(),
              g_road_gone_sample.kept.load(), g_road_gone_sample.dropped.load());
}

// Las líneas (hilo del módulo, cada segundo) y el resumen de 60 s.
void road_drain() {
    static constexpr const char* roles[] = {"raíz", "miembro", "tardío", "hijo no viajero", "vehículo enlazado"};
    const auto role = [](uint64_t v1) {
        const size_t r = static_cast<size_t>((v1 >> 8) & 0xFF);
        return r < std::size(roles) ? roles[r] : "?";
    };
    p::Event e;
    while (g_road_events.pop(e)) {
        const uint8_t type = static_cast<uint8_t>(e.v[1] & 0xFF);
        const size_t cls = static_cast<size_t>((e.v[1] >> 24) & 0xFF);
        switch (e.kind) {
            case kRvBirth:
                log::info("User2Road [{}] qpc={} T{} | key=0x{:08X} tipo={} clase={} papel={} raíz=0x{:08X} "
                          "solicitante=0x{:08X} controlador_{}=0x{:08X} (nativo 0x{:08X}) op={} +0x3CC={} +0x3CD={} "
                          "carretera +0x3D0=0x{:X} +0x3D8=0x{:X} +0x3E0=0x{:X} +0x3E8=0x{:X} "
                          "datos_guardados(+0x280/+0x298)={}/{} nacimiento={} {}={} | n.º_en_su_carretera={} (muestreo: "
                          "las primeras {} y 1 de cada {}; tipo 5 todas)",
                          e.flags ? "viajero asignado" : "viajero: se asignaría", e.qpc_us, e.tid, e.v[0], type,
                          road::class_name(cls), role(e.v[1]), e.v[2], e.v[3], e.flags ? "escrito" : "que se escribiría",
                          e.v[4], e.v[5], e.v[6] & 0xFFFF, (e.v[6] >> 16) & 0xFF, (e.v[6] >> 24) & 0xFF,
                          static_cast<uint32_t>(e.v[7]), static_cast<uint32_t>(e.v[7] >> 32),
                          static_cast<uint32_t>(e.v[8]), static_cast<uint32_t>(e.v[8] >> 32), e.v[9] & 1,
                          (e.v[9] >> 1) & 1, e.v[10], ((e.v[1] >> 8) & 0xFF) ? "nacimiento_unidad" : "nodos_preexamen",
                          e.v[11], e.v[9] >> 32, road::kSampleFirst, road::kSampleEvery);
                break;
            case kRvGone: {
                const auto f = [](uint64_t bits) { return std::bit_cast<float>(static_cast<uint32_t>(bits)); };
                const bool pos = e.flags & 1, du = e.flags & 2, dh = e.flags & 4, rad = e.flags & 8, exp = e.flags & 16;
                const auto side = static_cast<road::Side>((e.v[1] >> 8) & 0xFF);
                const auto m = [](bool ok, float v) { return ok ? std::format("{:.1f}", v) : std::string("?"); };
                const int64_t expiry = static_cast<int64_t>(e.v[8]), now = static_cast<int64_t>(e.v[9]);
                log::info("User2Road [viajero: baja] qpc={} T{} | {} key=0x{:08X} tipo={} carretera=0x{:X} motivo={} "
                          "vida_s={:.1f} | servidor=({}, {}, {}) d(user 2)={} m d(anfitrión)={} m radio(+0x468)={} m "
                          "caducidad(+0x428)={} ahora(_time64)={}{} -> rama={} | n.º_en_su_carretera={} (muestreo: las "
                          "primeras {} y 1 de cada {})",
                          e.qpc_us, e.tid, road::side_name(side), e.v[0], e.v[1] & 0xFF, e.v[2], (e.v[1] >> 16) & 0xFF,
                          static_cast<double>(e.v[3]) / 1e6, m(pos, f(e.v[4])), m(pos, f(e.v[4] >> 32)),
                          m(pos, f(e.v[5])), m(du, f(e.v[6])), m(dh, f(e.v[6] >> 32)), m(rad, f(e.v[7])),
                          exp ? std::to_string(expiry) : std::string("?"), now,
                          exp && expiry ? std::format(" (resta {} s)", expiry - now) : std::string(),
                          road::gone_name(static_cast<road::Gone>((e.v[1] >> 24) & 0xFF)), e.v[10], kGoneFirst,
                          kGoneEvery);
                break;
            }
            case kRvReject: {
                const auto v = static_cast<road::Verdict>(e.v[10] & 0xFF);
                const auto sv = static_cast<road::ScanV>((e.v[10] >> 8) & 0xFF);
                log::info("User2Road [viajero descartado] qpc={} T{} | key=0x{:08X} tipo={} solicitante=0x{:08X} "
                          "veredicto={} preexamen={} envoltorio=exe+0x{:X} interior=exe+0x{:X} | op={} sujeto_usuario2={} "
                          "clave_usuario2=0x{:08X} eax=0x{:08X} +0x3CC={} carretera +0x3D0=0x{:X} "
                          "datos_guardados={}/{} en_la_2550={} (clases {}) modo={} (nace con su controlador nativo; diagnóstico acotado a {} líneas "
                          "por veredicto)",
                          e.qpc_us, e.tid, e.v[0], type, e.v[3], road::verdict_name(v),
                          v == road::Verdict::UnitRejected ? road::scan_name(sv) : "-",
                          static_cast<uint32_t>(e.v[11]), static_cast<uint32_t>(e.v[11] >> 32), e.v[6] & 0xFFFF,
                          (e.v[6] >> 32) & 1, e.v[4], e.v[5], (e.v[6] >> 16) & 0xFF, static_cast<uint32_t>(e.v[7]),
                          e.v[9] & 1, (e.v[9] >> 1) & 1,
                          ((e.v[9] >> 8) & 0xFF) == 2 ? "sí" : ((e.v[9] >> 8) & 0xFF) == 1 ? "NO" : "desconocida",
                          (e.v[9] >> 16) & 0xFF, e.flags ? "1" : "probe", kRoadRejectLines);
                break;
            }
            case kRvSplit:
                log::warn("User2Road [viajero partido] qpc={} T{} | key=0x{:08X} tipo={} papel={} raíz=0x{:08X} "
                          "motivo={} controlador_nativo=0x{:08X}{}{} | modo={}: {}",
                          e.qpc_us, e.tid, e.v[0], type, role(e.v[1]), e.v[2],
                          road::split_name(static_cast<road::Split>(e.v[6] & 0xFF)), e.v[5],
                          e.v[7] ? std::format(" descriptor=exe+0x{:X}", e.v[7]) : std::string(),
                          ((e.v[1] >> 8) & 0xFF) == 3
                              ? std::format(" clase_hijo={} envoltorio=exe+0x{:X}",
                                            road::child_kind_name(static_cast<road::ChildKind>((e.v[6] >> 8) & 0xFF)),
                                            e.v[8])
                              : std::string(),
                          e.flags ? "1" : "probe",
                          e.flags ? "nace con el anfitrión; la unidad entera se retira (motivo 25, de hojas a raíz) al "
                                    "cerrarse su Troc"
                                  : "nada cambia (en 1 la unidad entera se retiraría)");
                break;
            case kRvOrphan:
                log::info("User2Road [viajero sin unidad] qpc={} T{} | key=0x{:08X} tipo={} solicitante=0x{:08X} "
                          "prof_troc={} op={} | su padre de descriptor: vtable exe+0x{:X} +0x3CD={} +0x3CC={} "
                          "mismo_solicitante={} su_+0x2F0={} | unidades abiertas en el hilo={}: su padre no es un registro "
                          "de ninguna (raíz descartada o de otro lote): nace con su controlador nativo, como su raíz",
                          e.qpc_us, e.tid, e.v[0], type, e.v[3], e.v[4], e.v[6], e.v[7], e.v[8] & 0xFF,
                          (e.v[8] >> 8) & 0xFF, (e.v[8] >> 16) & 1,
                          e.v[9] == 0 ? "ilegible" : e.v[9] == 1 ? "0 (raíz)" : "presente", e.v[10]);
                break;
            case kRvCensus: {
                const uint64_t c = e.v[3];
                const uint64_t n = c & 0xFF, born = (c >> 16) & 0xFF;
                const bool split = e.v[2] & 1, retire = (e.v[2] >> 1) & 1;
                const uint64_t k = e.v[9];  // S40: composición
                log::info("User2Road [viajero unidad] qpc={} T{} | raíz=0x{:08X} nacimiento={} unidad={} estado={}{} "
                          "registros={} preexamen={} nacidos={} tardíos={} miembros_{}={} host-kept={} | t3={} t5={} t6={} "
                          "otro={} | solicitante=0x{:08X} controlador=0x{:08X}{} | carretera=0x{:X} raíz_tipo={} "
                          "jinetes={} vehículos_enlazados={} ({} {}) gimmicks={} carga={} ajenos={} profundidad={}{}",
                          e.qpc_us, e.tid, e.v[0], e.v[1], e.flags ? "asignada" : "se asignaría",
                          split ? "PARTIDA" : born < n ? "pendiente" : "completa",
                          split ? std::format(" ({})", road::split_name(static_cast<road::Split>(e.v[4] & 0xFF)))
                                : std::string(),
                          n, (c >> 8) & 0xFF, born, (c >> 24) & 0xFF, e.flags ? "asignados" : "que se asignarían",
                          (c >> 32) & 0xFF, (c >> 40) & 0xFF, e.v[5] & 0xFF, (e.v[5] >> 8) & 0xFF, (e.v[5] >> 16) & 0xFF,
                          (e.v[5] >> 24) & 0xFF, e.v[6], e.v[7], retire ? " -> se retira entera" : "", e.v[8],
                          e.v[10], k & 0xFF, (k >> 8) & 0xFF, (k >> 16) & 0xFF,
                          e.flags ? "asignados" : "que se asignarían", (k >> 24) & 0xFF, (k >> 32) & 0xFF,
                          (k >> 40) & 0xFF, (k >> 48) & 0xFF,
                          (k >> 56) & 1 ? " INCOMPLETA (un hijo sin registro: su padre nunca se retira)" : "");
                break;
            }
            case kRvRequest:
                log::info("User2Road [viajeros: retirada pedida] qpc={} T{} | motivo={} unidades_asignadas={} "
                          "unidades_que_se_retirarían(probe)={} op={}",
                          e.qpc_us, e.tid, road::why_name(static_cast<road::Why>(e.v[0] & 0xFF)), e.v[1], e.v[2],
                          e.v[3]);
                break;
            case kRvRetired: {
                const uint64_t c = e.v[3];
                const auto line = std::format(
                    "User2Road [{}] qpc={} T{} | raíz=0x{:08X} nacimiento={} motivo={} nacidos={} fuera={} "
                    "fallidos={} sin_confirmar={} intentos_publicación={} desde_la_petición_ms={} op={}",
                    e.flags ? "viajero: retirada FALLIDA" : "viajero retirado", e.qpc_us, e.tid, e.v[0], e.v[1],
                    road::why_name(static_cast<road::Why>(e.v[2] & 0xFF)), c & 0xFF, (c >> 8) & 0xFF,
                    (c >> 16) & 0xFF, (c >> 24) & 0xFF, (c >> 32) & 0xFF, e.v[4] / 1000, e.v[5]);
                if (e.flags) {
                    log::warn("{}: sin más llamadas para esa unidad; lo no retirado sigue con el usuario 2 (terminal, "
                              "visible); nada global", line);
                } else {
                    log::info("{}", line);
                }
                break;
            }
            case kRvStop:
                log::error("User2Road [PARADA] motivo={} clave=0x{:08X}: no se asignan viajeros nuevos; los asignados "
                           "siguen con su controlador y su retirada en la carga / Logout del invitado continúa; ningún "
                           "aborto global (U-e, contención y familias siguen igual)",
                           road::stop_name(static_cast<road::Stop>(e.v[0] & 0xFF)), e.v[1]);
                break;
            case kRv2005:
                log::warn("User2Road [2005 discrepante] qpc={} | {} key=0x{:08X} controlador=0x{:08X} esperado=0x{:08X} "
                          "resultado={}",
                          e.qpc_us, e.flags ? "cliente del anfitrión" : "al invitado", e.v[0], e.v[1], e.v[2],
                          e.v[3] == static_cast<uint64_t>(road::Check2005::BadController) ? "OTRO controlador"
                                                                                           : "restore 0 al anfitrión");
                break;
            case kRvHierLate:
                log::info("User2Road [viajero: hijo de jerarquía fuera de su unidad] qpc={} | key=0x{:08X} tipo={} "
                          "padre=0x{:08X} raíz=0x{:08X} controlador=0x{:08X} (el de su padre 0x{:08X}{}) unidad={}: sólo "
                          "se cuenta (fase B R4: sin linaje +0x2F0 no parte la unidad)",
                          e.qpc_us, e.v[0], e.v[1], e.v[3], e.v[2], e.v[4], e.v[5],
                          e.v[4] != e.v[5] ? ", DISTINTO" : "", e.flags ? "asignada" : "se asignaría");
                break;
            case kRvSplitLoop:
                log::warn("User2Road [viajeros: bucle de partidas] carretera=0x{:X} {}: {}", e.v[0],
                          e.v[1] ? "y ya van demasiadas en 10 min en total" : "(partidas repetidas de su clase)",
                          e.flags ? (e.v[1] ? "PARADA de las asignaciones nuevas" : "esa carretera deja de asignarse")
                                  : "probe: en 1 pararía");
                break;
            case kRvCartStop:
                log::error("User2Road [PARADA de carros] motivo={} clave=0x{:08X}: no se asignan raíces tipo 5 nuevas "
                           "(caballos con jinete, carros); los caminantes y bandadas siguen asignándose; los carros ya "
                           "asignados siguen con su controlador y su retirada continúa (S40 revisión §6)",
                           road::stop_name(static_cast<road::Stop>(e.v[0] & 0xFF)), e.v[1]);
                break;
            case kRvHostReq:
                log::warn("User2Road [Req del cliente del anfitrión] qpc={} | key=0x{:08X} op={}: {}", e.qpc_us, e.v[0],
                          e.v[1], e.flags ? "doble control (su IA corre también allí): parada"
                                          : "indicio (2556, también la envía un observador): sólo se cuenta");
                break;
            default:
                break;
        }
    }
    static unsigned ticks = 0;
    if (road_mode() == npcact::Mode::Off || ++ticks % 60 != 0) return;
    const auto v = [](road::Verdict k, size_t c) { return g_road_v[static_cast<size_t>(k)][c].load(); };
    std::string verdicts, scans, splits, calls;
    for (size_t k = 1; k < kRoadV; ++k) {
        uint64_t n = 0;
        for (size_t c = 0; c < kRoadC; ++c) n += g_road_v[k][c].load();
        if (n) verdicts += std::format(" {}={};", road::verdict_name(static_cast<road::Verdict>(k)), n);
    }
    for (size_t k = 1; k < static_cast<size_t>(road::ScanV::Count); ++k) {
        if (const uint64_t n = g_road_scan[k].load()) {
            scans += std::format(" {}={};", road::scan_name(static_cast<road::ScanV>(k)), n);
        }
    }
    for (size_t k = 1; k < static_cast<size_t>(road::Split::Count); ++k) {
        if (const uint64_t n = g_road_split_why[k].load()) {
            splits += std::format(" {}={};", road::split_name(static_cast<road::Split>(k)), n);
        }
    }
    for (size_t k = 0; k < static_cast<size_t>(road::Call::Count); ++k) {
        calls += std::format(" {}={}", road::call_name(static_cast<road::Call>(k)), g_road_calls[k].load());
    }
    // Vivos por clase: con controlador escrito (o que se escribiría), con Req del invitado, desplazados >= 1 m (por sus
    // propias Req de posición), vistos por el cliente del anfitrión; latencia 2005 -> primera Req del invitado.
    uint64_t alive[kRoadC] = {}, driven[kRoadC] = {}, moved[kRoadC] = {}, host_seen[kRoadC] = {};
    uint64_t lat_n = 0, lat_sum = 0, lat_max = 0, no2005 = 0, overflow = 0;
    size_t st[6] = {};
    const bool on_mode = road_mode() == npcact::Mode::On;
    // S40 carros: lo mismo para los registros de unidades con raíz tipo 5 (caballos con jinete, carros), por papel:
    // [0] raíz, [1] jinete / conductor, [2] vehículo enlazado; vivos, con Req del invitado, desplazados >= 1 m.
    uint64_t cart_alive[3] = {}, cart_driven[3] = {}, cart_moved[3] = {}, cart_kept = 0;
    {
        RoadRead read;
        overflow = g_road.overflow;
        const uint64_t at = p8_now_us();
        for (size_t i = 0; i < g_road.capacity(); ++i) {
            const road::Unit& u = g_road.u[i];
            st[static_cast<size_t>(u.state)]++;
            if (u.state == road::UState::Free) continue;
            const bool cart = u.n && u.r[0].born && u.r[0].type == 5;
            for (uint8_t k = 0; k < u.n; ++k) {
                const road::Rec& r = u.r[k];
                if (!r.born || r.removed || r.clean == road::Clean::Gone) continue;
                if (cart && r.host_kept) ++cart_kept;
                // Carros (asignados o que se asignarían): sus viajeros por papel.
                if (cart && !r.host_kept && (r.assigned || (!u.assigned && !r.split))) {
                    const size_t part = k == 0 ? 0 : r.linked ? 2 : 1;
                    ++cart_alive[part];
                    if (r.driven) ++cart_driven[part];
                    if (r.moved >= 1.0f) ++cart_moved[part];
                }
                // S40: con el modo 1 sólo cuentan los asignados (un carro en probe no es «asignado»).
                if (!(r.assigned || (!on_mode && !u.assigned && !r.split))) continue;
                const size_t c = road::class_of(r.type);
                ++alive[c];
                if (r.driven) ++driven[c];
                if (r.moved >= 1.0f) ++moved[c];
                if (r.host_2005) ++host_seen[c];
                if (!r.q2005_us && r.born_us && at >= r.born_us + 10'000'000) ++no2005;
                if (r.driven && r.q2005_us && r.req_us >= r.q2005_us) {
                    const uint64_t d = r.req_us - r.q2005_us;
                    ++lat_n;
                    lat_sum += d;
                    lat_max = std::max(lat_max, d);
                }
            }
        }
    }
    const auto per = [](const uint64_t (&a)[kRoadC]) {
        return std::format("t3={} t5={} t6={} otro={}", a[0], a[1], a[2], a[3]);
    };
    const auto per_atomic = [](const std::atomic<uint64_t> (&a)[kRoadC]) {
        return std::format("t3={} t5={} t6={} otro={}", a[0].load(), a[1].load(), a[2].load(), a[3].load());
    };
    uint64_t roots[kRoadC] = {}, members[kRoadC] = {}, msplit[kRoadC] = {};
    for (size_t c = 0; c < kRoadC; ++c) {
        roots[c] = v(road::Verdict::Assign, c);
        members[c] = g_road_member[0][c].load();
        msplit[c] = g_road_member[2][c].load();
    }
    const bool on = road_mode() == npcact::Mode::On;
    log::info("User2Road [resumen] modo={} parada={} | raíces {}: {} | descartes:{} | preexamen fallido:{} | miembros "
              "{}: {} partidos: {} ({}) | sin_unidad={} vínculo_ilegible={} hijos_no_viajeros={} renacidos={} "
              "con_datos_guardados={} hijos_de_jerarquía_tardíos={} (con otro controlador {}) | tabla: libres={} abiertas={} vivas={} retirándose={} retiradas={} FALLIDAS={} "
              "llena={} | coste_mid: llamadas={} medio_us={} máx_us={}",
              npcact::mode_name(road_mode()), road::stop_name(static_cast<road::Stop>(g_road_stop.load())),
              on ? "asignadas" : "que se asignarían", per(roots), verdicts.empty() ? std::string(" -") : verdicts,
              scans.empty() ? std::string(" -") : scans, on ? "asignados" : "que se asignarían", per(members),
              per(msplit), splits.empty() ? std::string("-") : splits, g_road_orphans.load(),
              g_road_link_unknown.load(), g_road_foreign.load(), g_road_reborn.load(), g_road_saved_marked.load(),
              g_road_hier_late.load(), g_road_hier_late_other.load(), st[0], st[1], st[2], st[3], st[4], st[5],
              overflow, g_road_mid_n.load(),
              g_road_mid_n.load() ? g_road_mid_us.load() / g_road_mid_n.load() : 0, g_road_mid_max_us.load());
    log::info("User2Road [resumen vivos] {}: {} | con Req del invitado: {} | desplazados>=1m (sus Req): {} | vistos por el "
              "cliente del anfitrión: {} | 2005->primera Req del invitado: n={} media_ms={} máx_ms={} | 2005: invitado "
              "ok={} OTRO={} | anfitrión ok={} OTRO={} restore0={} | se asignaría: al invitado={} al anfitrión={} "
              "controlador_distinto={} | paridad 1:1 (2005 al invitado con metadatos de su personaje): registrados {} "
              "SIN registro {}; registrados sin 2005 tras 10 s={} | Req: invitado={} anfitrión doble_control={} indicio_2556={} (a los suyos={}) | "
              "bajas nativas: 25={} 42={} 44={} otro={}",
              on ? "asignados" : "que se asignarían", per(alive), per(driven), per(moved), per(host_seen), lat_n,
              lat_n ? lat_sum / lat_n / 1000 : 0, lat_max / 1000, g_road_guest_2005[0].load(),
              g_road_guest_2005[1].load(), g_road_host_2005[0].load(), g_road_host_2005[1].load(),
              g_road_host_2005[2].load(), g_road_would_guest_2005.load(), g_road_would_host_2005.load(),
              g_road_would_ctl_diff.load(), per_atomic(g_road_meta_registered), per_atomic(g_road_meta_unregistered),
              no2005, g_road_guest_req.load(), g_road_host_req_ctl.load(),
              g_road_host_req_2556.load(), g_road_host_req_would.load(), g_road_removed[0].load(),
              g_road_removed[1].load(), g_road_removed[2].load(), g_road_removed[3].load());
    road_parity_summary();
    {
        std::string wraps;
        for (size_t b = 0; b < static_cast<size_t>(npcfam::Wrap::Count); ++b) {
            if (const uint64_t n = g_road_scan_wrap[b].load()) {
                wraps += std::format(" {}={}", npcfam::wrap_name(static_cast<npcfam::Wrap>(b)), n);
            }
        }
        log::info("User2Road [resumen clases] sin_unidad por clase: {} | por su padre de descriptor: carretera={} "
                  "gimmick={} horario={} otro={} | preexamen fallido por envoltorio:{} | host-kept (gimmicks tipo 7): "
                  "registrados={} sin_registro={} | lotes sellados={} | 2550 leídas={} ilegibles={} | partidas: "
                  "asignadas={} que se asignarían={} | registros con un hijo sin sitio={} user2_road_carts={}",
                  per_atomic(g_road_orphan_cls), g_road_orphan_parent[0].load(), g_road_orphan_parent[1].load(),
                  g_road_orphan_parent[2].load(), g_road_orphan_parent[3].load(), wraps.empty() ? std::string(" -") : wraps,
                  g_road_host_kept.load(), g_road_host_kept_unrec.load(), g_road_seals.load(),
                  g_road_requests_read.load(), g_road_requests_bad.load(), g_road_split_units[1].load(),
                  g_road_split_units[0].load(), g_road_incomplete.load(),
                  road::carts_name(static_cast<road::Carts>(g_road_carts.load())));
        // S40 carros: por clase de carretera (composición medida al cerrarse cada unidad) y los vivos de las unidades con
        // raíz tipo 5 por papel. Lo que decide el paso a user2_road_carts=1 (docs/research/s25/s40-carros.md §6).
        std::string book;
        bool would_stop = false, carts_loop_stop = false;
        {
            RoadRead read;
            would_stop = g_road_split_would.stop;
            carts_loop_stop = g_road_split_carts.stop;
            // Revisión §8: primero las clases de raíz tipo 5 (su propio libro), luego las demás.
            for (const road::CaravanBook* bk : {&g_road_book_carts, &g_road_book}) {
                for (uint8_t i = 0; i < bk->n; ++i) {
                    const road::CaravanClass& x = bk->c[i];
                    std::string why;
                    for (size_t w = 1; w < static_cast<size_t>(road::Split::Count); ++w) {
                        if (x.why[w]) why += std::format("{}{}={}", why.empty() ? "" : ", ",
                                                         road::split_name(static_cast<road::Split>(w)), x.why[w]);
                    }
                    book += std::format(" | 0x{:X} raíz t{}: unidades={} asignadas={} completas={} partidas={}{} "
                                        "incompletas={} registros_máx={} carga_máx={} prof_máx={} jinetes={} "
                                        "vehículos_enlazados={} (asignados {}) gimmicks={} carga={} ajenos={}",
                                        x.road, x.root_type, x.units, x.assigned, x.complete, x.split,
                                        why.empty() ? std::string() : std::format(" ({})", why), x.incomplete,
                                        x.max_n, x.max_cargo, x.max_depth, x.riders, x.linked, x.linked_assigned,
                                        x.gimmicks, x.cargo, x.foreign);
                }
                if (bk->other_units) {
                    book += std::format(" | otras clases{}: unidades={}", bk == &g_road_book_carts ? " de carros" : "",
                                        bk->other_units);
                }
            }
        }
        // Revisión §4: el vínculo de montura del servidor de cada personaje, ahora (lecturas protegidas): lo que la
        // revalidación antes de Logout de un tipo 5 necesita leer (con un personaje montado debe nombrar su caballo).
        const auto link_str = [](uintptr_t chr) {
            const road::CharLink l = road::read_char_link(FamRead{}, chr);
            if (!l.present) return std::string("ausente");
            if (!l.ok) return std::string("ILEGIBLE");
            return std::format("legible (vehículo 0x{:08X} reservado 0x{:08X})", l.cur, l.res);
        };
        const auto kid = [](road::ChildKind k) {
            const size_t i = static_cast<size_t>(k);
            return std::format("{}={}/{}/{}", road::child_kind_name(k), g_road_child[i][0].load(),
                               g_road_child[i][1].load(), g_road_child[i][2].load());
        };
        log::info("User2Road [resumen carros] user2_road_carts={} | raíces tipo 5: {}={} que se asignarían (probe con el "
                  "1)={} fuera del 1={} montadas por un jugador={} | miembros de carros en probe con el 1: t3={} t5={} | "
                  "hijos sin marca (asignado/host-kept/partido): {} {} {} {} | vivos en unidades de raíz tipo 5 "
                  "(raíz/jinete/vehículo enlazado): vivos={}/{}/{} con Req del invitado={}/{}/{} desplazados>=1m={}/{}/{} "
                  "host-kept vivos={} | jugador montado: al nacer={} vehículo enlazado={} antes de Logout={} | "
                  "bucle (sólo probe/carros en probe): {}{}",
                  road::carts_name(static_cast<road::Carts>(g_road_carts.load())), on_mode ? "asignadas" : "que se asignarían",
                  g_road_v[static_cast<size_t>(road::Verdict::Assign)][1].load(), g_road_cart_would_roots.load(),
                  g_road_v[static_cast<size_t>(road::Verdict::CartRoot)][1].load(),
                  g_road_v[static_cast<size_t>(road::Verdict::PlayerRides)][1].load(), g_road_member[1][0].load(),
                  g_road_member[1][1].load(), kid(road::ChildKind::Linked), kid(road::ChildKind::Gimmick),
                  kid(road::ChildKind::Cargo), kid(road::ChildKind::Foreign), cart_alive[0], cart_alive[1],
                  cart_alive[2], cart_driven[0], cart_driven[1], cart_driven[2], cart_moved[0], cart_moved[1],
                  cart_moved[2], cart_kept, g_road_v[static_cast<size_t>(road::Verdict::PlayerRides)][1].load(),
                  g_road_linked_rides.load(), g_road_occupied.load(),
                  would_stop ? "pararía la pieza" : "no pararía",
                  book.empty() ? std::string(" | sin unidades cerradas") : book);
        log::info("User2Road [resumen montura] vínculo de montura del servidor: anfitrión {} | invitado {} | mount_sync: "
                  "{} | asientos del vehículo al nacer su jinete (tipo 5): legibles={} ILEGIBLES={} con un personaje={} | "
                  "antes de Logout: asientos legibles={} ILEGIBLES={}; lo monta un jugador por +0xAC={} mount_sync={} "
                  "vínculo={} asiento={}; tipo 5 sin prueba (no se retira aún)={} | host-kept vivo con otro controlador={} "
                  "hijos tardíos de carros (padre sin retirar)={} | parada de carros={} (bucle de carros {})",
                  link_str(g_host_char.load(std::memory_order_relaxed)),
                  link_str(g_u2_char.load(std::memory_order_relaxed)),
                  mount_sync_active() ? "activo" : "APAGADO (sin la fuente del otro jugador)",
                  g_road_seats_birth[0].load(), g_road_seats_birth[1].load(), g_road_seats_birth[2].load(),
                  g_road_seats_logout[0].load(), g_road_seats_logout[1].load(), g_road_ride_src[0].load(),
                  g_road_ride_src[1].load(), g_road_ride_src[2].load(), g_road_ride_src[3].load(),
                  g_road_ride_unknown.load(), g_road_kept_changed.load(), g_road_hier_cart_incomplete.load(),
                  road::stop_name(static_cast<road::Stop>(g_road_cart_stop.load())),
                  carts_loop_stop ? "parado" : "no");
        log::info("User2Road [resumen retirada] cancelación de publicación: negativas={} ocupada={} hijos retenidos "
                  "añadidos={} sin sitio={} | probe: unidades cuya cancelación se negaría={} (hijos retenidos={}, "
                  "child_hold ocupado={}) | revalidación antes de Logout: dueño (+0xA0)={} montado por un jugador={} "
                  "nacimiento cambiado={}",
                  g_road_pub_refused.load(), g_road_pub_busy.load(), g_road_held_added.load(),
                  g_road_held_no_room.load(), g_road_pub_would_refuse.load(), g_road_pub_would_children.load(),
                  g_road_pub_would_busy.load(), g_road_owned.load(), g_road_occupied.load(), g_road_reborn_skip.load());
    }
    log::info("User2Road [resumen limpieza] peticiones={} unidades={} (probe: se retirarían {}) pasadas={} ocupada={} "
              "sin_SAM={} llamadas:{} | unidades retiradas={} FALLIDAS={} publicación_rechazada={} excepciones={} "
              "pendiente={}",
              g_road_requests.load(), g_road_req_units.load(), g_road_would_units_at_request.load(), g_road_runs.load(),
              g_road_busy.load(), g_road_no_sam.load(), calls, g_road_units_done.load(), g_road_units_failed.load(),
              g_road_pub_refused.load(), g_road_faults.load(), g_road_pending.load());
}

// Prueba en un PC (14-10, 15:42): abandonar es una decisión POR UNIDAD. La raíz y su familia (claves y nacimientos
// exactos) no se retiran otra vez y siguen vivas con su controlador; sus claves asignadas salen del vigilante antiguo
// de U-e (kKeyAbandoned: nunca cuentan como 'unaccepted'), así que no pueden disparar el aborto global. Una línea por
// unidad. Esto no toca containment_ready: la red antigua sólo vuelve si la contención deja de poder actuar. Si vuelve
// por su cuenta, su limpieza de no aceptadas trata estas claves como cualquier otra (como en c7ae132).
void npc_abandon(const npcact::Actor& root, uint64_t why, uint8_t refusals) noexcept {
    npcact::Family family;  // ~1,6 KB, se llena en sitio
    {
        ActivationLock guard;
        g_activation.abandon(root.key, root.birth, family);
    }
    const uint64_t now = p8_now_us();
    size_t excluded = 0;
    const auto exclude = [&excluded, now](uint32_t key) {
        const uint16_t f = g_ctl_keys.flags(key);
        if (!(f & u2s::kKeyAssigned) || (f & (u2s::kKeyRemoved | u2s::kKeyAbandoned))) return;
        if (g_ctl_keys.mark(key, u2s::kKeyAbandoned, now)) ++excluded;
    };
    exclude(root.key);
    for (size_t i = 0; i < family.size; ++i) {
        if (family.actors[i].key != root.key) exclude(family.actors[i].key);
    }
    g_activation_gave_up.fetch_add(1, std::memory_order_relaxed);
    g_activation_gave_up_keys.fetch_add(excluded, std::memory_order_relaxed);
    npc_event(kNpcGaveUp, root, why,
              refusals | std::min<uint64_t>(family.size, 0xFF) << 8 | std::min<uint64_t>(excluded, 0xFF) << 16);
}
// Un rechazo de la unidad `root` (libera su reserva). Al agotar kMaxRefusals se abandona esa unidad.
void npc_refuse(const npcact::Actor& root, uint64_t why) noexcept {
    g_activation_refused.fetch_add(1, std::memory_order_relaxed);
    bool exhausted = false;
    uint8_t refusals = 0;
    {
        ActivationLock guard;
        exhausted = g_activation.refused(root.key, root.birth);
        if (const auto* a = g_activation.find(root.key); a && a->birth == root.birth) refusals = a->refusals;
    }
    npc_event(kNpcRefused, root, why, refusals);
    if (exhausted) npc_abandon(root, why, refusals);
}
bool npc_postpone_kind(npcact::Due d) noexcept {
    return d == npcact::Due::PostponeReq || d == npcact::Due::PostponeSilence || d == npcact::Due::PostponeLoading;
}
// S35, bajo g_activation_lock: una decisión destructiva pospuesta (npcact::postpone: sin rechazo ni presupuesto del
// nodo). probe: sólo se cuenta lo que haría npc_containment_guard=1. Una línea por unidad (la primera vez).
void npc_postponed_locked(npcact::Actor& a, npcact::Due why, uint64_t now, bool probe) noexcept {
    if (!npc_postpone_kind(why)) return;
    (probe ? g_guard_would_n : g_postponed_n)[static_cast<size_t>(why)].fetch_add(1, std::memory_order_relaxed);
    if (!probe) {
        npcact::postpone(a, why, now);
        if (now >= a.postponed_us) store_max_u64(g_postpone_age_max_us, now - a.postponed_us);
    }
    const bool line = probe ? !a.guard_noted : a.postpones == 1;
    if (probe) a.guard_noted = true;
    if (!line) return;
    const uint64_t last = g_last_guest_req_us.load(std::memory_order_relaxed);
    npcact::Actor shown = a;
    shown.node.data = static_cast<uint32_t>(std::min<uint64_t>(last && now >= last ? now - last : UINT32_MAX, UINT32_MAX));
    npc_event(kNpcPostponed, shown, static_cast<uint64_t>(why) | static_cast<uint64_t>(probe) << 8, a.postpones);
}
// S33: una unidad lenta no apaga U-e. El hilo periódico sólo mide; la retirada sucede al salir
// del Troc exterior del invitado, con el cerrojo de las llamadas U-1 y sin otro Troc suyo activo.
void npc_activation_cleanup(uint16_t opcode) noexcept {
    if (!npc_probes()) return;
    const uint64_t now = p8_now_us();
    uint64_t scanned = g_activation_scan_us.load(std::memory_order_relaxed);
    if (now < scanned + 250'000 || !g_activation_scan_us.compare_exchange_strong(scanned, now)) return;
    npcact::Actor root;
    // S35: la política (npc_containment_guard=1: silencio/carga posponen, gracia acotada por Req, plazo configurable).
    // Con guard=0 y los valores por defecto es exactamente no_progress de S33/S34.
    const bool guard_on = mode_is(g_guard_mode, npcact::Mode::On);
    const npcact::Policy pol = containment_policy(guard_on);
    const npcact::Context ctx = containment_context(now);
    {
        ActivationLock guard;
        npcact::Actor* selected = nullptr;
        const bool dry = g_activation_mode.load() == static_cast<uint8_t>(npcact::Mode::Probe);
        if (dry) {
            // Preparar el plan de cada nacimiento antes del viejo aborto global (probe no cambia ese comportamiento).
            // Que exista este plan NO significa que haya vencido el plazo ni que vaya a ejecutarse.
            selected = g_activation.next_probe_plan(server_epoch(), g_arm_done_us.load());
        } else {
            selected = g_activation.next_due(pol, ctx, server_epoch(), g_arm_done_us.load());
        }
        if (auto* a = selected) {
            const npcact::Due d = npcact::containment_due(*a, pol, ctx);
            if (!dry && npc_postpone_kind(d)) {
                npc_postponed_locked(*a, d, now, false);  // ni rechazo ni presupuesto: se vuelve a mirar en 1 s
            } else {
                if (d == npcact::Due::Retire && mode_is(g_guard_mode, npcact::Mode::Probe) && !a->orphaned &&
                    !a->local_retired) {
                    // probe: lo que haría el guard; la decisión sigue siendo la de S34.
                    const npcact::Due g = npcact::containment_due(*a, containment_policy(true), ctx);
                    if (npc_postpone_kind(g)) npc_postponed_locked(*a, g, now, true);
                }
                if (d == npcact::Due::Retire && !a->diagnosed) {
                    a->diagnosed = true;
                    npc_event(kNpcStalled, *a, a->queued_us ? now - a->queued_us : 0, a->orphaned ? 1 : 0);
                }
                // Revisión S35 (hallazgo 2): la posposición por silencio/carga tiene tope; agotado, una línea WRN.
                if (d == npcact::Due::Retire && guard_on && a->postpones && !a->exhausted_noted &&
                    npcact::postpone_exhausted(*a, pol, now)) {
                    a->exhausted_noted = true;
                    g_postpone_exhausted.fetch_add(1, std::memory_order_relaxed);
                    npc_event(kNpcPostponeExhausted, *a, a->postponed_us && now >= a->postponed_us ? now - a->postponed_us : 0,
                              a->postpones);
                }
                root = *a;
            }
        }
    }
    if (root.key && g_activation_mode.load() == static_cast<uint8_t>(npcact::Mode::Probe)) {
        root.node.stage = npc_exact_stage(root.key, root.node.data, root.node.index);
        npcact::Family dry;
        bool report = false;
        {
            ActivationLock guard;
            if (auto* a = g_activation.find(root.key); a && a->birth == root.birth && !a->plan_reported) {
                a->plan_reported = true; a->node.stage = root.node.stage;
                g_activation.family_into(root.key, dry); report = true;
            }
        }
        if (report) {
            // Mismas lecturas exactas de stage y vínculos; cero SAM::FindActor/Logout/tombstone.
            const uint64_t why = !root.node.valid() ? 1 : dry.overflow ? 2 : npcact::has_progress(root, pol) ? 7 :
                                 npcact::containment_due(root, pol, ctx) != npcact::Due::Retire ? 8 : 0;
            npc_event(kNpcPlan, root, dry.size, why);
            for (size_t i = 0; i < dry.size; ++i) npc_member_event(kNpcPlanMember, dry.actors[i], root.node, root.key, why);
        }
        return;
    }
    // Revisión: el mismo criterio que desactiva el aborto antiguo (containment_ready): o actúa la contención o la red
    // antigua, nunca ninguna de las dos.
    if (!root.key || !containment_ready() || !server_realm()) return;
    const bool unit = !root.orphaned && !root.local_retired;  // una unidad nueva, no un reintento de su limpieza
    // Resolver antes de tomar cerrojos: el descriptor sólo lleva recurso/nodo y no identifica la instancia.
    if (unit) root.node.stage = npc_exact_stage(root.key, root.node.data, root.node.index);
    if (unit && !root.node.valid()) {
        npc_refuse(root, 1);  // no se pudo demostrar la instancia del nodo
        return;
    }
    if (unit) {
        // Revisión: su nodo ya agotó el presupuesto y aun así renació: el veto no lo detuvo (sitio de padre nativo,
        // grupo con nodos vivos o no legible, otra ruta). Retirarlo otra vez sería el bucle de S33 en local: se abandona
        // ESTA unidad (motivo 9), que sigue viva con su controlador; la contención continúa para las demás.
        bool exhausted = false;
        {
            ActivationLock guard;
            exhausted = g_activation.blocked(root.node);
        }
        if (exhausted) {
            npc_abandon(root, 9, root.refusals);
            return;
        }
    }
    ModLock modification;
    if (!modification.ok) return;
    // El dueño de la barrera es quien la toma por CAS; sólo él la suelta (prep_drain hace lo mismo).
    bool expected = false;
    if (!g_p_draining.compare_exchange_strong(expected, true)) return;
    struct TrocBarrier { ~TrocBarrier() { g_p_draining.store(false, std::memory_order_release); } } barrier;
    if (user2_funnels_trocs_in_flight() > 1) return;
    npcact::Family family;  // ~1,6 KB, se llena en sitio
    uint64_t refuse = 0;
    {
        // Revisión S35 (hallazgo 13): el contexto (lecturas atómicas y la fase de carga) antes del cerrojo.
        const uint64_t at = p8_now_us();
        const npcact::Context reserve_ctx = containment_context(at);
        ActivationLock guard;
        auto* a = g_activation.find(root.key);
        if (!a || a->birth != root.birth) return;
        const npcact::Due d = npcact::containment_due(*a, pol, reserve_ctx);
        if (d != npcact::Due::Retire) {
            if (npc_postpone_kind(d)) npc_postponed_locked(*a, d, at, false);
            return;
        }
        a->node = root.node;
        g_activation.family_into(root.key, family);
        if (!family.size || family.overflow || (unit && !g_activation.budget(root.node, true))) {
            refuse = family.overflow ? 2 : 3;
        } else {
            // Reservar sólo este nacimiento; progreso nuevo antes de las llamadas cancela la retirada.
            a->pending = true;
        }
    }
    if (refuse) {
        npc_refuse(root, refuse);
        return;
    }
    const uintptr_t lookup = rdp(g_base + kLookupSlot);
    const uintptr_t sam = lookup ? rdp(lookup) : 0;
    const uint32_t err = rdv<uint32_t>(g_base + kLogoutErrSlot);
    bool valid = sam != 0;
    // Scopes nativos, lectura guardada y la misma identidad/controlador/padre de todas las piezas.
    for (size_t i = 0; valid && !root.local_retired && i < family.size; ++i) {
        const auto& a = family.actors[i];
        {
            ActivationLock guard;
            const auto* current = g_activation.find(a.key);
            valid = current && npcact::may_cleanup_member(*current, a.key, a.birth, p8_now_us());
        }
        // El padre se compara con el registrado: un vínculo cambiado rechaza (motivo 4), acotado por npc_refuse.
        if (valid && (!a.controller || cleanup_one(sam, a.key, a.controller, err, a.parent, true) != 0)) valid = false;
    }
    // S35 (AUDITORIA.md §4; INFORME-A4 punto 5): revalidar Req Y aplicación al EJECUTAR, no sólo al elegir, con el
    // orden de cerrojos existente (ModLock -> barrera de Trocs -> ActivationLock) y la versión de la unidad elegida.
    // Con guard=1 una Req, aplicación, activación o vínculo nuevos desde la elección, o un silencio/carga del
    // invitado, NO es un rechazo: se libera la reserva y se vuelve a planificar (sin consumir presupuesto).
    // Revisión S35 (hallazgos 5, 12, 13): la decisión es npcact::execution_decision (pura, probada); se cuenta su
    // motivo real (con guard=0 una aplicación es «progreso», no «ya no está»); el contexto se toma antes del cerrojo.
    npcact::ExecDecision exec;
    {
        const uint64_t at = p8_now_us();
        const npcact::Context exec_ctx = containment_context(at);
        ActivationLock guard;
        bool members_gone = !valid;
        npcact::Recheck member = npcact::Recheck::Valid;
        for (size_t i = 0; !members_gone && i < family.size; ++i) {
            const auto& old = family.actors[i];
            const auto* a = g_activation.find(old.key);
            const npcact::Recheck r = a ? npcact::recheck_member(*a, old, pol) : npcact::Recheck::Gone;
            if (r == npcact::Recheck::Gone) members_gone = true;
            else if (r != npcact::Recheck::Valid && member == npcact::Recheck::Valid) member = r;
        }
        auto* a = g_activation.find(root.key);
        const npcact::Recheck r = members_gone ? npcact::Recheck::Gone
                                  : a           ? npcact::recheck_root(*a, root, pol, exec_ctx)
                                                : npcact::Recheck::Gone;
        exec = npcact::execution_decision(members_gone, r, member, guard_on);
        g_recheck_n[static_cast<size_t>(exec.why)].fetch_add(1, std::memory_order_relaxed);
        if (exec.exec == npcact::Exec::Cancel && a && a->birth == root.birth) {
            // Cancelada al ejecutar: la reserva se suelta y la unidad se vuelve a mirar (o ya progresó). Sin rechazo.
            const npcact::Due d = npcact::cancel_execution(*a, pol, exec_ctx);
            if (npc_postpone_kind(d)) g_postponed_n[static_cast<size_t>(d)].fetch_add(1, std::memory_order_relaxed);
            const uint64_t last = exec_ctx.last_guest_req_us;
            npcact::Actor shown = *a;  // v[6] = la edad de la última Req del invitado (como en [contención pospuesta])
            shown.node.data =
                static_cast<uint32_t>(std::min<uint64_t>(last && at >= last ? at - last : UINT32_MAX, UINT32_MAX));
            npc_event(kNpcCancelled, shown, static_cast<uint64_t>(exec.why), a->postpones);
        }
    }
    if (exec.exec == npcact::Exec::Cancel) return;
    if (exec.exec == npcact::Exec::Refuse) {
        npc_refuse(root, 4);
        return;
    }
    uint32_t keys[npcact::kMaxFamily] = {};
    for (size_t i = 0; i < family.size; ++i) keys[i] = family.actors[i].key;
    // La publicación y sus colas tienen su propia barrera: nunca llamarla bajo g_activation_lock.
    const uint64_t token = g_birth_serial.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!child_hold_retire_family(root.node.epoch, token, std::span<const uint32_t>(keys, family.size))) {
        npc_refuse(root, 5);
        return;
    }
    {
        ActivationLock guard;
        if (unit) g_activation.failed(root.node);
        for (size_t i = 0; i < family.size; ++i) {
            const auto& old = family.actors[i];
            if (auto* a = g_activation.find(old.key); a && a->birth == old.birth) a->local_retired = true;
        }
    }
    size_t retired = 0;
    // S36 (npc_family_control=1, AUDITORIA-2 cambio 7): retirada parcial explícita. Un miembro cuya baja no se
    // confirmó en esta pasada bloquea a su padre (y éste al suyo): nunca se retira un padre dejando vivo a un hijo suyo;
    // lo bloqueado cuenta como un fallo acotado de su limpieza y se reintenta (kMaxCleanupFailures).
    const bool tree_guard = family_mode() == npcact::Mode::On;
    npcfam::RetireGuard blocked;
    // Hijos primero, padre al final; ninguna publicación tardía puede reintroducirlos mientras están retirándose.
    for (size_t i = family.size; i > 0; --i) {
        const auto& a = family.actors[i - 1];
        if (tree_guard && blocked.is_blocked(a.key)) {
            blocked.block(a.parent);
            g_fam_blocked.fetch_add(1, std::memory_order_relaxed);
            npc_member_event(kNpcFamBlocked, a, root.node, root.key);
            ActivationLock guard;
            g_activation.cleanup_result(a.key, a.birth, npcact::kCleanBlocked, p8_now_us());
            continue;
        }
        {
            ActivationLock guard;
            auto* current = g_activation.find(a.key);
            if (!current || !npcact::may_cleanup_member(*current, a.key, a.birth, p8_now_us())) {
                // Sin llamada para él en esta pasada (reintento pendiente o agotado) y sigue vivo: sus ancestros esperan.
                if (tree_guard && current && current->birth == a.birth && !current->removed) blocked.block(a.parent);
                continue;
            }
            current->pending = true;
        }
        const int result = cleanup_one(sam, a.key, a.controller, err, a.parent);
        if (tree_guard && result != npcact::kCleanDone && result != npcact::kCleanGone) blocked.block(a.parent);
        npc_member_event(kNpcRetired, a, root.node, result, opcode);
        if (result == 0 || result == 1) {
            ++retired;
            g_ctl_keys.mark(a.key, u2s::kKeyRemoved, p8_now_us());
        } else {
            g_activation_refused.fetch_add(1);
        }
        {
            ActivationLock guard;
            g_activation.cleanup_result(a.key, a.birth, result, p8_now_us());
            if (const auto* current = g_activation.find(a.key); current && current->birth == a.birth &&
                !current->removed && current->cleanup_failures == npcact::kMaxCleanupFailures) {
                // Estado terminal LOCAL: no fingir una baja ni liberar al observador.
                npc_event(kNpcRefused, *current, 6, result);
            }
        }
        if (result == 3) break;  // excepción nativa: la pieza se detiene; el filtro mantiene CONTINUE_SEARCH
    }
    npc_event(kNpcRetired, root, retired, family.size);
}

// O (review A4): user 2's character logs out on a server thread: its armed manager, or what U-1 left in it while the
// character stands in the host's field, is unloaded at once (mode 1 only).
void logout_drain(uintptr_t chr, uint8_t reason) noexcept {
    if (mode_now() != u2s::Mode::On || !g_calls_ok.load(std::memory_order_relaxed)) return;
    const uintptr_t mgr = seq_manager(chr);
    if (!mgr) return;
    const bool armed = g_armed.load(std::memory_order_acquire);
    if (!armed && !(g_ever_armed.load(std::memory_order_relaxed) && lists_nonempty(mgr) && host_owns(field_of(chr)))) {
        return;
    }
    g_logout_drains.fetch_add(1, std::memory_order_relaxed);
    drain_now(mgr, true, static_cast<uint16_t>(0xF000 | reason), kCauseLogout);
    g_reset_machine.store(true);
}
// Review 3: its character leaves the server: every NPC still given to it goes (after the drain above, if any).
void logout_cleanup(uint8_t reason) noexcept { ctl_cleanup(0, kCleanItsLogout, static_cast<uint16_t>(0xF000 | reason)); }

bool is_load(uint16_t op) noexcept { return op == kOp2283 || op == kOp2362 || op == kOp2437; }
// The Reqs whose ExecuteTroc entry drains its armed manager at once: its loads and its goodbye (if the 2001 runs as a
// Troc of its character; the OnRecvPacket path only counts it, see user2_stages_server_packet).
bool drains_at_entry(uint16_t op) noexcept { return is_load(op) || op == kOp2001; }

// ---------------------------------------------------------------- the module's thread: the players

struct Player {
    uintptr_t chr = 0, mgr = 0, sm = 0, fm = 0;
    uint32_t key = 0, user_key = 0;
    u2s::MgrFacts f;
    bool pos_ok = false, mounted = false;
    u2s::World w;
    u2s::Cell cell;
    uintptr_t field = 0;  // TransformSync +0x500 (its field mover +0x4F0, +0x10), a ServerField (vtable checked)
};

bool read_player(uintptr_t user, Player& pl) {
    pl = Player{};
    if (!user) return false;
    pl.user_key = rdv<uint32_t>(user + u2s::kUser_Key);
    pl.chr = rdp(user + u2s::kUser_Main);
    if (!pl.chr) return false;
    pl.key = rdv<uint32_t>(pl.chr + u2s::kActor_Key);
    const uintptr_t comps = rdp(pl.chr + u2s::kActor_Comps);
    if (const uintptr_t mgr = seq_manager(pl.chr)) {
        pl.mgr = mgr;
        u2s::MgrFacts& f = pl.f;
        f.readable = true;
        f.state = rdv<uint8_t>(mgr + u2s::kMgr_State);
        f.preparing = rdv<uint8_t>(mgr + u2s::kMgr_Preparing);
        f.prep_pending = rdv<uint8_t>(mgr + u2s::kMgr_PrepPending);
        f.cell = {rdv<int32_t>(mgr + u2s::kMgr_CellX), rdv<int32_t>(mgr + u2s::kMgr_CellZ)};
        f.seq = rdv<uint32_t>(mgr + u2s::kMgr_SeqCount);
        f.keep = rdv<uint32_t>(mgr + u2s::kMgr_KeepCount);
        const uintptr_t sm = rdp(mgr + u2s::kMgr_Sched);
        if (sm && rdp(sm) == g_base + u2s::kVtSchedMgr) {
            pl.sm = sm;
            f.sm_ok = true;
            f.sm_count = rdv<uint32_t>(sm + u2s::kSm_Count);
            f.sm_weight = rdv<uint32_t>(sm + u2s::kSm_Weight);
            f.sm_pending = rdv<int32_t>(sm + u2s::kSm_Pending);
        }
        const uintptr_t fm = rdp(mgr + u2s::kMgr_Faction);
        if (fm && rdp(fm + u2s::kFm_Actor) == pl.chr) {
            pl.fm = fm;
            f.fm_ok = true;
            f.fm_count = rdv<uint32_t>(fm + u2s::kFm_Count);
            f.fm_pending = rdv<uint8_t>(fm + u2s::kFm_Pending);
        }
    }
    // The server position (0x4F8220's inputs: local +0xB4, parent +0xD0).
    const uintptr_t ts = comps ? rdp(comps + u2s::kComps_Ts) : 0;
    pl.field = field_of(pl.chr);
    u2s::SectorPos lp;
    u2s::ParentXf px;
    uint8_t raw[0x2C] = {};
    if (ts && game::safe_read(ts + u2s::kTs_Local, &lp, sizeof lp) &&
        game::safe_read(ts + u2s::kTs_Parent, raw, sizeof raw)) {
        std::memcpy(px.scale, raw, 12);
        std::memcpy(px.q, raw + 0xC, 16);
        std::memcpy(px.t, raw + 0x1C, 12);
        std::memcpy(&px.sx, raw + 0x28, 2);
        std::memcpy(&px.sz, raw + 0x2A, 2);
        pl.mounted = !u2s::parent_is_identity(px);
        pl.w = u2s::world_of(lp, px);
        pl.pos_ok = u2s::plausible(static_cast<float>(pl.w.x), static_cast<float>(pl.w.y), static_cast<float>(pl.w.z));
        if (pl.pos_ok) pl.cell = u2s::cell_of(pl.w);  // never a NaN through float -> int
    }
    return true;
}

// The world cell of an actor (its TransformSync, a mount's parent included); false when unreadable or implausible.
bool actor_cell(uintptr_t actor, u2s::Cell& out) noexcept {
    u2s::World w;
    if (!actor_world(actor, w)) return false;
    out = u2s::cell_of(w);
    return true;
}
// Its world position (the server's TransformSync: what 0x28F9A20 measures); false when unreadable or implausible.
bool actor_world(uintptr_t actor, u2s::World& out) noexcept {
    const uintptr_t comps = actor ? rdp(actor + u2s::kActor_Comps) : 0;
    const uintptr_t ts = comps ? rdp(comps + u2s::kComps_Ts) : 0;
    u2s::SectorPos lp;
    uint8_t raw[0x2C] = {};
    if (!ts || !game::fast_read(ts + u2s::kTs_Local, &lp, sizeof lp) ||
        !game::fast_read(ts + u2s::kTs_Parent, raw, sizeof raw)) {
        return false;
    }
    u2s::ParentXf px;
    std::memcpy(px.scale, raw, 12);
    std::memcpy(px.q, raw + 0xC, 16);
    std::memcpy(px.t, raw + 0x1C, 12);
    std::memcpy(&px.sx, raw + 0x28, 2);
    std::memcpy(&px.sz, raw + 0x2A, 2);
    const u2s::World w = u2s::world_of(lp, px);
    if (!u2s::plausible(static_cast<float>(w.x), static_cast<float>(w.y), static_cast<float>(w.z))) return false;
    out = w;
    return true;
}

// The cells of a schedule spawn manager's live stages (at most 256, guarded reads).
size_t stage_cells(uintptr_t sm, u2s::Cell* out, size_t cap) {
    if (!sm) return 0;
    const uintptr_t list = rdp(sm + u2s::kSm_List);
    const uint32_t n = std::min<uint32_t>(rdv<uint32_t>(sm + u2s::kSm_Count), static_cast<uint32_t>(cap));
    size_t k = 0;
    for (uint32_t i = 0; list && i < n; ++i) {
        uintptr_t st = 0;
        float pos[3] = {};
        // The first failed read ends the walk (a list being changed under us); an implausible position is skipped.
        if (!rd(list + static_cast<uintptr_t>(i) * 8, st) || !st || !game::fast_read(st + u2s::kSt_Pos, pos, sizeof pos)) {
            break;
        }
        if (!u2s::plausible(pos[0], pos[1], pos[2])) continue;
        out[k++] = u2s::cell_of(u2s::World{pos[0], pos[1], pos[2]});
    }
    return k;
}

std::string facts_text(const char* who, const Player& pl) {
    if (!pl.chr) return std::format("{}: no character", who);
    const u2s::MgrFacts& f = pl.f;
    std::string s = std::format("{}: character 0x{:08X}", who, pl.key);
    if (!f.readable) {
        s += " manager UNREADABLE";
    } else {
        s += std::format(" manager 0x{:X} state {} (+0x689 {} +0x68A {}) last cell ({},{}) seq {} keep {}", pl.mgr,
                         f.state, f.preparing, f.prep_pending, f.cell.x, f.cell.z, f.seq, f.keep);
        s += f.sm_ok ? std::format(" | schedules: stages {} weight {} pending 2045 {}", f.sm_count, f.sm_weight,
                                   f.sm_pending)
                     : std::string(" | schedules: unreadable");
        s += f.fm_ok ? std::format(" | factions: stages {} pending 2046 {}", f.fm_count, f.fm_pending)
                     : std::string(" | factions: unreadable");
    }
    s += pl.pos_ok ? std::format(" | server position ({:.0f}, {:.0f}, {:.0f}){} cell ({},{})", pl.w.x, pl.w.y, pl.w.z,
                                 pl.mounted ? " (child of a mount)" : "", pl.cell.x, pl.cell.z)
                   : std::string(" | server position unreadable");
    return s;
}

u2s::Machine g_machine;
const u2s::Thresholds kThresholds{};
std::string g_last_state;
uint64_t g_last_state_us = 0;
uintptr_t g_seen_u2_char = 0;
uint64_t g_host_not2_since = 0, g_last_arm_ask = 0, g_last_leftover_ask = 0, g_minute_start = 0;
uint64_t g_minute_all0 = 0, g_minute_u20 = 0, g_host_minute = 0, g_host_minute_u2 = 0, g_baseline_sum = 0,
         g_baseline_n = 0;
uint64_t g_u2_zero_since = 0;
bool g_abort_logged = false;

// The preconditions of mode 1 (logged when one fails): the gate, the element filter, the funnels' route, the calls.
std::string mode1_missing() {
    std::string s;
    if (!g_gate_own.load() && !shop_probe_gate_live()) s += " the owner gate (U-a) is not hooked;";
    if (!g_gate2_on.load()) s += " the collector's manager guard 0x2911435 (U-a2) is not hooked;";
    if (!g_elem_on.load()) s += " the element filter (U-f) is not hooked;";
    if (!user2_funnels_vf48_live()) s += " user2_funnels' vf48 / subject hooks (U-b) are not live;";
    if (!g_calls_ok.load()) s += " the called functions are not build 2976's;";
    if (route_mode_now() != u2s::Mode::On) s += " user2_timer_route is not 1 (its timers must reach its own manager);";
    if (!world_anchor_on()) s += " user2_world_anchor is not 1 (mundo-escenarios-propios.md 5.1);";
    return s;
}

// §8.16 (log only): the 2043 chains alive on the host's manager, from its rescheduling ticks (all of them, and those
// under user 2's subject: A1's chain when user2_timer_route is not 1). Native: 2 after the login until the host's
// first load whose state-1 window lasts a period.
struct HostChains {
    uint32_t all = 0, u2 = 0;
    uint32_t period_ms = 0;
};
HostChains host_chains(uint64_t now) {
    HostChains h;
    h.period_ms = u2s::timer_2043_period_ms(rdv<int32_t>(g_base + kPeriodSlot));  // was the delay (100): fixed
    if (!h.period_ms) return h;
    const uint64_t period = static_cast<uint64_t>(h.period_ms) * 1000;
    const uint64_t window = std::max<uint64_t>(60'000'000, period * 6);
    std::array<uint64_t, kHostTickRing> all{}, u2{};
    size_t na = 0, nu = 0;
    for (const auto& slot : g_host_ticks) {
        const uint64_t v = slot.load(std::memory_order_relaxed);
        const uint64_t t = v >> 1;
        if (!t || t > now || now - t > window) continue;
        all[na++] = t;
        if (v & 1) u2[nu++] = t;
    }
    std::sort(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(na));
    std::sort(u2.begin(), u2.begin() + static_cast<std::ptrdiff_t>(nu));
    h.all = u2s::count_chains(all.data(), na, period, now).alive;
    h.u2 = u2s::count_chains(u2.data(), nu, period, now).alive;
    return h;
}
std::string host_chains_text(uint64_t now) {
    const HostChains h = host_chains(now);
    if (!h.period_ms) return "host chains unknown (the period reads 0)";
    return std::format("host chains {} (user 2's {}; period {} ms; native login: 2 until its first long load)", h.all,
                       h.u2, h.period_ms);
}

// The host's 2043s per minute (all, and those of user 2's chain on the host's manager); the baseline from the
// complete minutes before any arm with the host in state 2 that start >= 60 s after its last preparation or owner
// load (§8.16: the login's partial minutes pulled it down to 7-8). Review M1: a change is a WRN line, never an abort.
void cadence(const Player& host, uint64_t now) {
    if (!g_minute_start) {
        g_minute_start = now;
        g_minute_all0 = g_host_2043.load();
        g_minute_u20 = g_host_2043_u2.load();
        return;
    }
    if (now - g_minute_start < 60'000'000) return;
    g_host_minute = g_host_2043.load() - g_minute_all0;
    g_host_minute_u2 = g_host_2043_u2.load() - g_minute_u20;
    const uint64_t base = g_baseline_n ? g_baseline_sum / g_baseline_n : 0;
    if (u2s::cadence_changed(g_host_minute, base)) {
        log::warn("User2Stages [host cadence] {} | the host's manager got {} 2043s in the last minute ({} of them user "
                  "2's own chain), baseline {} | {} | U-1 {} | user2_timer_route {}",
                  p8_when(now), g_host_minute, g_host_minute_u2, base, host_chains_text(now),
                  g_armed.load() ? "ARMED" : "not armed", u2s::mode_name(route_mode_now()));
    }
    if (!g_arms.load() && host.f.state == 2 &&
        u2s::baseline_minute_ok(g_minute_start, g_last_host_prep_us.load(), g_last_host_load_us.load())) {
        g_baseline_sum += g_host_minute;
        ++g_baseline_n;
    }
    g_minute_start = now;
    g_minute_all0 = g_host_2043.load();
    g_minute_u20 = g_host_2043_u2.load();
}

void ask(Req r, Cause c, uint64_t now) {
    g_req_cause.store(c);
    g_req_since_us.store(now);
    g_req.store(r, std::memory_order_release);
}

// S37 phase D (module thread): set by fast_check for this sample's control() only.
bool g_fast_ask = false;
void control(const Player& host, const Player& u2, bool known, u2s::FieldRel rel, uint64_t now,
             const std::string& dist_text, uint64_t read_us) {
    const u2s::Mode m = mode_now();
    if (host.f.readable && host.f.state != 2) {
        if (!g_host_not2_since) g_host_not2_since = now;
    } else {
        g_host_not2_since = 0;
    }
    if (m != u2s::Mode::On) return;
    const bool armed = g_armed.load(std::memory_order_acquire);
    if (g_aborted.load()) {
        if (!g_abort_logged) {
            g_abort_logged = true;
            log::error("User2Stages [ABORTED] {} | U-1 stopped for the rest of this run (behaves as probe){}",
                       p8_when(now), armed ? "; the drain is requested" : "");
        }
        if (armed && g_req.load() == kReqNone) ask(kReqDrain, kCauseAbort, now);
        return;
    }
    const bool on = g_machine.state == u2s::Would::On;
    // Review A2: a pending arm whose reason went away (the host came back, another field) is cancelled.
    if (!on || rel != u2s::FieldRel::Same) {
        uint8_t expected = kReqArm;
        if (g_req.compare_exchange_strong(expected, kReqNone)) {
            g_req_fast.store(false, std::memory_order_relaxed);
            log::info("User2Stages [ARM cancelled] {} | before user 2's next Req: {} | distance {}", p8_when(now),
                      on ? "not in the same field" : "back under the threshold", dist_text);
        }
    }
    // Arm: the hysteresis says ON, user 2's manager armable, the preconditions in, at most one ask per 10 s (S37 phase
    // D: a fast arm asks at once).
    if (!armed && on && known && g_req.load() == kReqNone && !server_logout_requested() &&
        (now - g_last_arm_ask >= 10'000'000 || g_fast_ask)) {
        const u2s::Arm a = u2s::arm_verdict(u2.f);
        const std::string missing = mode1_missing();
        g_last_arm_ask = now;
        if (a == u2s::Arm::Ready && missing.empty()) {
            g_req_fast.store(g_fast_ask, std::memory_order_relaxed);
            ask(kReqArm, kCauseDistance, now);
            log::info("User2Stages [ARM requested] {} | distance {} | user 2's manager 0x{:X} state {} | runs at user "
                      "2's next Req on the server's thread{}",
                      p8_when(now), dist_text, u2.mgr, u2.f.state,
                      g_fast_ask ? " | FAST (user2_fast_rearm=1: no hysteresis, a one-shot 2043 at the arm)" : "");
        } else {
            log::warn("User2Stages [ARM not asked] {} | distance {} | arm check: {} (state {}){}", p8_when(now),
                      dist_text, u2s::arm_name(a), u2.f.state, missing.empty() ? "" : " | missing:" + missing);
        }
    }
    if (!armed) g_hold_since = 0;
    // Review A2: the drain by level, every second while it holds: armed and the machine OFF, or another field.
    if (armed && g_req.load() == kReqNone) {
        const bool other_field = u2.chr && rel == u2s::FieldRel::Different;
        // U2 (user2_shops.h): a DISTANCE drain waits while user 2 is in a shop (at most 120 s after its 2916); another
        // field, its loads and its logout never wait.
        if ((!on || other_field) && (other_field || !user2_shops_hold_drain(now, g_dist.load()))) {
            // Review 2: with user2_drain_at_host_prepare=1, a distance drain waits for the host's pending preparation
            // (its load step after its last preparation), at most 15 s; P drains it then, before the host's stages.
            const uint64_t ll = g_last_host_load_us.load(), lp = g_last_host_prep_us.load();
            const bool hold = !other_field && u2s::hold_distance_drain(dprep_mode_now() == u2s::Mode::On, now, ll, lp,
                                                                        g_hold_since);
            if (hold) {
                if (!g_hold_since) {
                    g_hold_since = now;
                    g_holds.fetch_add(1);
                    log::info("User2Stages [DRAIN held] {} | distance {} | the host's load started {:.1f} s ago and its "
                              "preparation has not run: user2_drain_at_host_prepare drains at it (at most {} s)",
                              p8_when(now), dist_text, static_cast<double>(now - ll) / 1e6,
                              u2s::kPrepHoldUs / 1'000'000);
                }
            } else {
                if (g_hold_since && !other_field && ll > lp) {
                    g_hold_expired.fetch_add(1);
                    log::warn("User2Stages [DRAIN hold expired] {} | the host's preparation did not come in {} s: the "
                              "distance drain goes on",
                              p8_when(now), u2s::kPrepHoldUs / 1'000'000);
                }
                g_hold_since = 0;
                if (!other_field && dprep_mode_now() == u2s::Mode::Probe && ll > lp && !g_hold_probe_logged) {
                    g_hold_probe_logged = true;  // once per run: the probe's 'would' at P says the rest
                    log::info("User2Stages [DRAIN would be held] {} | probe: with user2_drain_at_host_prepare=1 this "
                              "drain would wait for the host's preparation (its load started {:.1f} s ago)",
                              p8_when(now), static_cast<double>(now - ll) / 1e6);
                }
                ask(kReqDrain, other_field ? kCauseField : kCauseDistance, now);
                log::info("User2Stages [DRAIN requested] {} | {} | distance {}", p8_when(now),
                          cause_name(other_field ? kCauseField : kCauseDistance), dist_text);
            }
        }
    }
    // Review A4/A6: stages left in its manager after U-1 (not armed, in the host's field): drained too, at most every 10 s.
    if (!armed && g_ever_armed.load() && rel == u2s::FieldRel::Same && u2.f.readable &&
        (u2.f.seq || u2.f.sm_count || u2.f.fm_count) && g_req.load() == kReqNone && now - g_last_leftover_ask >= 10'000'000) {
        // S37 phase D (4-contencion §3 / #6): lists read while a drain at the host's preparation runs (g_p_draining),
        // or before any drain that ended after the read, are a stale reading, not leftovers. Per operation (the real
        // flag, the drain's own time), no fixed wait; the check runs again next second. user2_fast_rearm=0: Ask.
        const bool draining = g_p_draining.load(std::memory_order_seq_cst);
        const frm::Leftover lv = frm::leftover_verdict(fast_mode_now(), draining, read_us,
                                                       g_last_drain_us.load(std::memory_order_relaxed));
        if (lv == frm::Leftover::Skip) {
            g_left_skip.fetch_add(1);
            // At most one line per 10 s (a long NPC cleanup holds g_p_draining too); every skip is counted.
            static uint64_t logged = 0;
            if (now - logged >= 10'000'000) {
                logged = now;
                log::info("User2Stages [leftover check skipped] {} | its lists (sequencers {} schedules {} factions {}) "
                          "were read {}: a stale reading, no drain asked (checked again next second; skips so far {})",
                          p8_when(now), u2.f.seq, u2.f.sm_count, u2.f.fm_count,
                          draining ? "while g_p_draining is held (a drain at the host's preparation or an NPC cleanup)"
                                   : "before a drain that ended after it",
                          g_left_skip.load());
            }
        } else {
            if (lv == frm::Leftover::WouldSkip) {
                g_left_would.fetch_add(1);
                log::info("User2Stages [leftover check would be skipped] {} | probe (user2_fast_rearm): its lists were "
                          "read {}",
                          p8_when(now),
                          draining ? "while g_p_draining is held (a drain at the host's preparation or an NPC cleanup)"
                                   : "before a drain that ended after it");
            }
            g_last_leftover_ask = now;
            ask(kReqDrain, kCauseLeftover, now);
            log::warn("User2Stages [DRAIN requested] {} | {}: sequencers {} schedules {} factions {}", p8_when(now),
                      cause_name(kCauseLeftover), u2.f.seq, u2.f.sm_count, u2.f.fm_count);
        }
    }
    // A request nobody runs (no Req of user 2 for 10 s): said once per request.
    if (g_req.load() != kReqNone && g_req_since_us.load() && now - g_req_since_us.load() >= 10'000'000) {
        log::warn("User2Stages [request waiting] {} | {} asked {:.0f} s ago: no Req of user 2 reached the server since",
                  p8_when(now), g_req.load() == kReqArm ? "the arm" : "the drain",
                  static_cast<double>(now - g_req_since_us.load()) / 1e6);
        g_req_since_us.store(0);
    }
    // The abort criteria (measured, user2_stages_logic.h).
    u2s::AbortIn in;
    in.armed = g_armed.load();
    in.now_us = now;
    in.arm_done_us = g_arm_done_us.load();
    in.last_mgr2_tick_us = g_last_mgr2_tick_us.load();
    in.tick_timeout_us = std::max<uint64_t>(15'000'000, static_cast<uint64_t>(u2s::timer_2043_period_ms(
                                                            rdv<int32_t>(g_base + kPeriodSlot))) * 3000);
    in.mgr2_state = u2.f.state;
    in.host_not2_since_us = g_host_not2_since;
    in.last_host_load_us = g_last_host_load_us.load();
    in.last_host_prep_us = g_last_host_prep_us.load();
    in.kick_us = g_kick_us.load();
    in.dup_far = g_dup_far.load();
    in.unexpected_2322 = g_unexp_2322.load();
    const u2s::Abort ab = u2s::check_abort(in);
    if (ab != u2s::Abort::None) {
        g_aborted.store(true);
        log::error("User2Stages [ABORT] {} | {} | user 2's manager state {} | host state {} | distance {} | the drain "
                   "is requested",
                   p8_when(now), u2s::abort_name(ab), u2.f.state, host.f.state, dist_text);
        ask(kReqDrain, kCauseAbort, now);
    }
}

// ---------------------------------------------------------------- S37 phase D: the fast re-arm (module thread)

frm::History g_hist;
uint64_t g_last_fast_us = 0;   // the last fast arm asked (mode 1) or would-fast (probe)
uint32_t g_fast_count = 0;
uint64_t g_would_fast_us = 0;  // probe: a would-fast waiting for the hysteresis' arm (0 = none)
std::string g_would_fast_hist;
uint64_t g_follow_until = 0;   // probe: the next arm's first 2043 is reported against the would-fast (until then)
uint64_t g_follow_hyst_ms = 0;
uint64_t g_tick_reported_gen = 0;
uint64_t g_hyst_saved_n = 0, g_hyst_saved_ms_sum = 0, g_hyst_saved_ms_max = 0, g_would_unfollowed = 0;
uint64_t g_first_tick_n = 0, g_first_tick_ms_sum = 0, g_first_tick_ms_max = 0;

std::string hist_text(uint64_t now) {
    std::string s;
    for (size_t i = 0; i < g_hist.size(); ++i) {
        const frm::Sample& h = g_hist.at(i);
        const double ago = now >= h.t_us ? static_cast<double>(now - h.t_us) / 1e6 : 0.0;
        s += h.valid ? std::format("{}-{:.1f}s {} (host {},{} u2 {},{})", s.empty() ? "" : ", ", ago, h.dist, h.host.x,
                                   h.host.z, h.user2.x, h.user2.z)
                     : std::format("{}-{:.1f}s ?", s.empty() ? "" : ", ", ago);
    }
    return s.empty() ? std::string("none") : s;
}
// User 2's own 2043 chains on its manager (its rescheduling ticks, the same estimate as the host's).
uint32_t u2_chains_alive(uint64_t now) {
    const uint32_t period_ms = u2s::timer_2043_period_ms(rdv<int32_t>(g_base + kPeriodSlot));
    if (!period_ms) return 0;
    const uint64_t period = static_cast<uint64_t>(period_ms) * 1000;
    const uint64_t window = std::max<uint64_t>(60'000'000, period * 6);
    std::array<uint64_t, kHostTickRing> t{};
    size_t n = 0;
    for (const auto& slot : g_u2_ticks) {
        const uint64_t v = slot.load(std::memory_order_relaxed);
        if (!v || v > now || now - v > window) continue;
        t[n++] = v;
    }
    std::sort(t.begin(), t.begin() + static_cast<std::ptrdiff_t>(n));
    return u2s::count_chains(t.data(), n, period, now).alive;
}
std::string chains_text(uint64_t now) {
    const uint32_t period = u2s::timer_2043_period_ms(rdv<int32_t>(g_base + kPeriodSlot));
    const uint64_t last = g_chain_last_us.load();
    return std::format("user 2's chain: {} (last rescheduling tick {}), chains alive on its manager {} | {}",
                       g_chain_ended.load() ? "ENDED" : u2s::chain_dead(now, last, period) ? "dead" : "alive",
                       last && now >= last ? std::format("{:.1f} s ago", static_cast<double>(now - last) / 1e6)
                                           : std::string("never"),
                       u2_chains_alive(now), host_chains_text(now));
}
std::string ago_text(uint64_t now, uint64_t t) {
    return t && now >= t ? std::format("{:.1f} s ago", static_cast<double>(now - t) / 1e6) : std::string("never");
}

// Review 3b: a fast candidate (the jump sample that passed fast_verdict) waits kConfirmUs for confirm_verdict; and,
// after a confirmed fast arm (or would), a load of user 2 within kGuestFollowUs counts as "followed by the guest".
frm::Sample g_cand;  // valid: a candidate waiting
std::string g_cand_ctx;
uint64_t g_cand_dropped[static_cast<size_t>(frm::Confirm::Count)] = {};
uint64_t g_cand_n = 0, g_guest_watch_since = 0, g_guest_followed = 0;

void cand_drop(frm::Confirm why, uint64_t now) {
    if (!g_cand.valid) return;
    ++g_cand_dropped[static_cast<size_t>(why)];
    log::info("User2Stages [{}] {} | candidate DROPPED after {:.1f} s: {} | nothing asked, the hysteresis decides | "
              "jump then: {}",
              fast_mode_now() == u2s::Mode::On && mode_now() == u2s::Mode::On ? "FAST REARM" : "would FAST REARM",
              p8_when(now), static_cast<double>(now - g_cand.t_us) / 1e6, frm::confirm_name(why), g_cand_ctx);
    g_cand = {};
}

// After step(): a <= off -> >= on jump of the last two samples is checked against every condition of a genuine
// departure of the HOST (frm::fast_verdict, with the near streak's start before this sample's step: review 3a). A
// candidate is then confirmed over kConfirmUs (frm::confirm_verdict: far, user 2 still, no load of user 2: review 3b).
// Mode 1 (with user2_stages=1): at the confirmation the machine goes ON and control() asks the arm at once, fast (the
// one-shot at the arm). Probe: '[would FAST REARM]', then what it would have saved when the hysteresis arms (or that
// none followed). Returns true when control() must ask now.
bool fast_check(const frm::Sample& prev, const frm::Sample& cur, u2s::Step st, uint64_t now, uint64_t near_since0) {
    const u2s::Mode fm = fast_mode_now();
    const bool act = fm == u2s::Mode::On && mode_now() == u2s::Mode::On;
    const char* tag = act ? "FAST REARM" : "would FAST REARM";
    // Probe: the hysteresis armed after a would-fast: the time the fast path would have saved.
    if (g_would_fast_us && st == u2s::Step::WouldArm) {
        const uint64_t ms = (now - g_would_fast_us) / 1000;
        ++g_hyst_saved_n;
        g_hyst_saved_ms_sum += ms;
        g_hyst_saved_ms_max = std::max(g_hyst_saved_ms_max, ms);
        log::info("User2Stages [would FAST REARM] {} | the hysteresis armed {} ms after the would-fast: a fast arm would "
                  "have saved {} ms of hysteresis | jump then: {}",
                  p8_when(now), ms, ms, g_would_fast_hist);
        g_would_fast_us = 0;
        g_follow_until = now + 30'000'000;
        g_follow_hyst_ms = ms;
    } else if (g_would_fast_us && now - g_would_fast_us >= 30'000'000) {
        ++g_would_unfollowed;
        log::warn("User2Stages [would FAST REARM] {} | NO hysteresis arm followed in 30 s (the host came back, another "
                  "field, a reset): a fast arm would have armed for nothing | jump then: {}",
                  p8_when(now), g_would_fast_hist);
        g_would_fast_us = 0;
    }
    // Review 3b: a load of user 2 soon after a confirmed fast arm (or would): the guest followed the host.
    if (g_guest_watch_since) {
        const uint64_t l = g_u2_load_us.load();
        if (l >= g_guest_watch_since && l - g_guest_watch_since <= frm::kGuestFollowUs) {
            ++g_guest_followed;
            log::warn("User2Stages [{}] {} | user 2 started a load {:.1f} s after the {}: it followed the host (the "
                      "stages {} for nothing; its load drains them)",
                      tag, p8_when(now), static_cast<double>(l - g_guest_watch_since) / 1e6,
                      act ? "fast arm" : "would-fast", act ? "armed" : "would have been armed");
            g_guest_watch_since = 0;
        } else if (now - g_guest_watch_since > frm::kGuestFollowUs) {
            g_guest_watch_since = 0;
        }
    }
    // A candidate waiting for its confirmation.
    if (g_cand.valid) {
        frm::ConfirmIn ci;
        ci.jump = g_cand;
        ci.cur = cur;
        ci.u2_load_us = g_u2_load_us.load();
        ci.guest_loading = child_hold_user2_loading();
        ci.blocked = g_armed.load() || g_aborted.load() || g_machine.state == u2s::Would::On ||
                     g_req.load() != kReqNone || g_hold_since != 0 || g_p_draining.load(std::memory_order_seq_cst) ||
                     server_logout_requested();
        ci.t = kThresholds;
        const frm::Confirm c = frm::confirm_verdict(ci);
        if (c == frm::Confirm::Wait) return false;
        if (c != frm::Confirm::Go) {
            cand_drop(c, now);
            return false;
        }
        const uint64_t waited_ms = (now - g_cand.t_us) / 1000;
        const std::string ctx = g_cand_ctx;
        g_cand = {};
        g_guest_watch_since = now;
        if (act) {
            // The machine ON now: control() asks the arm at once; arm_locked re-checks (distance from both characters).
            g_machine.state = u2s::Would::On;
            log::info("User2Stages [FAST REARM] {} | CONFIRMED after {} ms (far, user 2 still, no load of user 2) | the "
                      "arm is asked now (~{} ms of hysteresis saved, estimated) and queues a one-shot 2043 (delay {} = "
                      "{} ms) | jump then: {}",
                      p8_when(now), waited_ms,
                      kThresholds.on_dwell_us / 1000 > waited_ms ? kThresholds.on_dwell_us / 1000 - waited_ms : 0,
                      frm::kOneShotDelay, frm::delay_ms(frm::kOneShotDelay), ctx);
            return true;
        }
        g_would_fast_us = now;
        g_would_fast_hist = ctx;
        log::info("User2Stages [would FAST REARM] {} | CONFIRMED after {} ms | probe: nothing asked; the hysteresis arms "
                  "at the earliest {} s after the jump (the saving is logged then) | jump then: {}",
                  p8_when(now), waited_ms, kThresholds.on_dwell_us / 1'000'000, ctx);
        return false;
    }
    if (!frm::jump(prev, cur, kThresholds)) return false;
    g_fast_jumps.fetch_add(1);
    frm::FastIn in;
    in.prev = prev;
    in.cur = cur;
    in.host_load_us = g_last_host_load_us.load();
    in.near_since_us = near_since0;
    in.last_fast_us = g_last_fast_us;
    in.fast_count = g_fast_count;
    in.armed = g_armed.load();
    in.machine_on = g_machine.state == u2s::Would::On;
    in.aborted = g_aborted.load();
    in.pending = g_req.load() != kReqNone;
    in.drain_hold = g_hold_since != 0;
    in.draining = g_p_draining.load(std::memory_order_seq_cst);
    in.guest_loading = child_hold_user2_loading();
    in.logout = server_logout_requested();
    in.t = kThresholds;
    const frm::Fast v = frm::fast_verdict(in);
    g_fast_verdicts[static_cast<size_t>(v)].fetch_add(1);
    const std::string ctx = std::format(
        "distance {} -> {} cells in {:.1f} s (host moved {} cells, user 2 {}) | the host's load step (L) {}, its "
        "preparation (P) {}, the near streak since {} (L {} it) | history: {} | {}",
        prev.dist, cur.dist, static_cast<double>(cur.t_us - prev.t_us) / 1e6, u2s::cheb(prev.host, cur.host),
        u2s::cheb(prev.user2, cur.user2), ago_text(now, in.host_load_us), ago_text(now, g_last_host_prep_us.load()),
        ago_text(now, near_since0),
        in.host_load_us && near_since0 ? (in.host_load_us > near_since0 ? "after" : "BEFORE") : "?", hist_text(now),
        chains_text(now));
    if (v != frm::Fast::Fast) {
        log::info("User2Stages [{}] {} | jump NOT taken: {} | {} | the hysteresis decides", tag, p8_when(now),
                  frm::fast_name(v), ctx);
        return false;
    }
    // A candidate: the 60 s gap and the cap count it already (a dropped one too).
    g_last_fast_us = now;
    ++g_fast_count;
    ++g_cand_n;
    g_cand = cur;
    g_cand_ctx = ctx;
    log::info("User2Stages [{}] {} | candidate: the host's own departure | {} | confirming for {} s (far, user 2 still, "
              "no load of user 2, nothing pending)",
              tag, p8_when(now), ctx, frm::kConfirmUs / 1'000'000);
    return false;
}

// Every sample (fast key != 0): a pending one-shot with no tick in 5 s is lost; the first 2043 after each arm.
void fast_watch(uint64_t now) {
    uint64_t gen = 0;
    if (g_oneshot.expire(now, gen)) {
        g_os_lost.fetch_add(1);
        log::warn("User2Stages [FAST REARM] {} | the one-shot 2043 of arm {} LOST: no tick with +0xB 0 reached user 2's "
                  "manager in {} s (the chain's own repair still covers it) | {}",
                  p8_when(now), gen, frm::kOneShotLostUs / 1'000'000, chains_text(now));
    }
    const uint64_t arm = g_arm_done_us.load(), first = g_first_tick_us.load(), g = g_arm_gen.load();
    if (!arm || !first || first < arm || g == g_tick_reported_gen) return;
    g_tick_reported_gen = g;
    const uint64_t ms = (first - arm) / 1000;
    ++g_first_tick_n;
    g_first_tick_ms_sum += ms;
    g_first_tick_ms_max = std::max(g_first_tick_ms_max, ms);
    if (g_arm_fast.load()) {
        log::info("User2Stages [FAST REARM] {} | the first 2043 reached user 2's manager {} ms after the fast arm {} | {}",
                  p8_when(now), ms, g, chains_text(now));
    } else if (g_follow_until && arm <= g_follow_until) {
        const uint64_t os = frm::delay_ms(frm::kOneShotDelay);
        const uint64_t saved = ms > os ? ms - os : 0;
        log::info("User2Stages [would FAST REARM] {} | the first 2043 reached user 2's manager {} ms after the arm {}: a "
                  "one-shot ({} ms) would have saved ~{} ms more (with the hysteresis ~{} ms in all) | {}",
                  p8_when(now), ms, g, os, saved, saved + g_follow_hyst_ms, chains_text(now));
        g_follow_until = 0;
    }
}
std::string fast_text() {
    if (fast_mode_now() == u2s::Mode::Off) return {};
    std::string verdicts;
    for (size_t i = 0; i < static_cast<size_t>(frm::Fast::Count); ++i) {
        if (const uint64_t n = g_fast_verdicts[i].load()) {
            verdicts += std::format(" [{}] {};", frm::fast_name(static_cast<frm::Fast>(i)), n);
        }
    }
    std::string dropped;
    for (size_t i = 0; i < static_cast<size_t>(frm::Confirm::Count); ++i) {
        if (g_cand_dropped[i]) {
            dropped += std::format(" [{}] {};", frm::confirm_name(static_cast<frm::Confirm>(i)), g_cand_dropped[i]);
        }
    }
    const uint64_t c = g_os_consumed.load();
    return std::format(
        " || fast re-arm (user2_fast_rearm={}): jumps <= {} -> >= {} {}, verdicts:{} | candidates {}, dropped while "
        "confirming:{} | followed by a load of user 2 within {} s {} | fast arms {} (refused at the Req {}, of them by "
        "the distance recomputed there {}), would-fast followed by the hysteresis {} (saved mean {} ms, max {} ms), not "
        "followed {} | one-shots queued "
        "{} consumed {} (delay max {} ms, saved vs the chain mean {} ms, max {} ms) cancelled {} lost {} late {} failed {} "
        "| first 2043 after an arm: {} (mean {} ms, max {} ms) | leftover checks skipped {} (would {}) | user 2's chains "
        "alive {}",
        u2s::mode_name(fast_mode_now()), kThresholds.off_cells, kThresholds.on_cells, g_fast_jumps.load(),
        verdicts.empty() ? " none" : verdicts, g_cand_n, dropped.empty() ? " none" : dropped,
        frm::kGuestFollowUs / 1'000'000, g_guest_followed, g_fast_arms.load(), g_fast_refused.load(),
        g_fast_dist_refused.load(), g_hyst_saved_n,
        g_hyst_saved_n ? g_hyst_saved_ms_sum / g_hyst_saved_n : 0, g_hyst_saved_ms_max, g_would_unfollowed,
        g_os_queued.load(), c, g_os_delay_ms_max.load(), c ? g_os_saved_ms_sum.load() / c : 0, g_os_saved_ms_max.load(),
        g_os_cancelled.load(), g_os_lost.load(), g_os_late.load(), g_os_failed.load(), g_first_tick_n,
        g_first_tick_n ? g_first_tick_ms_sum / g_first_tick_n : 0, g_first_tick_ms_max, g_left_skip.load(),
        g_left_would.load(), u2_chains_alive(p8_now_us()));
}

void sample(bool force_log) {
    const uint64_t read_us = p8_now_us();  // S37 phase D: before user 2's facts are read (the leftover check)
    uintptr_t hu = 0, uu = 0;
    Player host, u2;
    if (server_host_user(hu)) read_player(hu, host);
    if (server_user2(uu)) read_player(uu, u2);
    // A user 2 that is not the session's main character (another key) is not used.
    if (u2.chr && server_user2_player_key() && u2.key != server_user2_player_key()) u2 = Player{};
    // Review A4: user 2's character epoch. No character for < 10 s (a transient read) changes nothing; another
    // character, or none for 10 s, starts U-1 over (its manager is another object). Its logout drained an armed
    // manager (point O); if the old one is still readable with stages, that is said (WRN).
    const uint64_t now0 = p8_now_us();
    if (!u2.chr && g_seen_u2_char) {
        if (!g_u2_zero_since) g_u2_zero_since = now0;
        if (now0 - g_u2_zero_since < 10'000'000) return;
    } else {
        g_u2_zero_since = 0;
    }
    if (u2.chr != g_seen_u2_char) {
        const uintptr_t old_mgr = g_seen_u2_char ? seq_manager(g_seen_u2_char) : 0;
        if (g_ever_armed.load() && old_mgr && lists_nonempty(old_mgr)) {
            log::warn("User2Stages [orphan risk] {} | user 2's character changed ({}) with its old manager 0x{:X} still "
                      "readable and not empty (armed {}): nothing unloaded it at its logout",
                      p8_when(now0), u2.chr ? "another one" : "none for 10 s", old_mgr, g_armed.load() ? "yes" : "no");
        }
        g_seen_u2_char = u2.chr;
        user2_shops_reset(u2.chr ? "user 2's character changed" : "user 2 left");  // U2 (review A1)
        g_armed.store(false);
        g_ever_armed.store(false);
        g_req.store(kReqNone);
        g_chain_last_us.store(0);
        g_chain_ended.store(false);
        g_arm_done_us.store(0);
        g_kick_us.store(0);
        g_lists_built.store(false);
        g_machine = {};
        if (fast_mode_now() != u2s::Mode::Off) {  // S37 phase D: nothing of the old character's epoch survives
            g_req_fast.store(false);
            oneshot_cancel(frm::Cancel::NewCharacter);
            g_first_tick_us.store(0);
            g_hist.clear();
            g_would_fast_us = 0;
            g_follow_until = 0;
            g_cand = {};
            g_guest_watch_since = 0;
        }
    }
    if (g_reset_machine.exchange(false)) {
        g_machine = {};
        g_would_fast_us = 0;  // S37 phase D (probe): a reset hysteresis no longer measures that would-fast
        if (g_cand.valid) cand_drop(frm::Confirm::Blocked, now0);  // a drain / load reset the machine
    }
    g_host_char.store(host.chr);
    g_host_key.store(host.key);
    g_host_user.store(host.user_key);
    g_host_sm.store(host.sm);
    g_u2_char.store(u2.chr);
    g_u2_key.store(u2.key);
    g_u2_sm.store(u2.sm);
    g_u2_mgr.store(u2.mgr);
    g_host_mgr.store(host.mgr);
    g_host_cell.store(host.pos_ok ? u2s::pack_cell(host.cell) : 0);
    g_u2_cell.store(u2.pos_ok ? u2s::pack_cell(u2.cell) : 0);
    // U-0b (S29): the distance only counts with both players in the same ServerField.
    const u2s::FieldRel rel = u2s::field_rel(host.field, u2.field);
    const bool known = host.pos_ok && u2.pos_ok && rel == u2s::FieldRel::Same;
    const uint32_t dist = known ? u2s::cheb(host.cell, u2.cell) : 0;
    g_dist.store(known ? dist : UINT32_MAX);
    const uint64_t now = p8_now_us();
    cadence(host, now);
    const u2s::Mode m = mode_now();
    if (m == u2s::Mode::Off) return;  // user2_timer_route alone: no state lines, no decisions
    const uint64_t near_since0 = g_machine.near_since;  // S37 phase D (review 3a): the near streak before this step
    const u2s::Step st = u2s::step(g_machine, known && u2.chr, dist, now, kThresholds);
    const u2s::Arm arm = u2s::arm_verdict(u2.f);
    const std::string dist_text =
        known ? std::format("{} cells (Chebyshev, 64 m; U on at >= {} for {} s, off at <= {} for {} s; {})", dist,
                            kThresholds.on_cells, kThresholds.on_dwell_us / 1'000'000, kThresholds.off_cells,
                            kThresholds.off_dwell_us / 1'000'000, u2s::field_rel_name(rel))
              : std::format("unknown ({})", rel != u2s::FieldRel::Same ? u2s::field_rel_name(rel)
                                                                        : "a position unreadable");
    if (st != u2s::Step::None) {
        const std::string missing = mode1_missing();
        log::info("User2Stages [{}] {} | distance {} | arm check: {} (state {}) | {}{}",
                  st == u2s::Step::WouldArm ? "would ARM" : "would DRAIN", p8_when(now), dist_text, u2s::arm_name(arm),
                  u2.f.state,
                  m == u2s::Mode::On && !g_aborted.load() ? "mode 1: acted on below"
                                                          : "probe: nothing called, nothing written",
                  missing.empty() ? "" : " | mode 1 would NOT act, missing:" + missing);
    }
    // S37 phase D: the last two samples against a genuine departure of the host (user2_fast_rearm=0: nothing here).
    g_fast_ask = false;
    if (fast_mode_now() != u2s::Mode::Off) {
        frm::Sample cur;
        cur.valid = known && u2.chr;
        cur.t_us = now;
        cur.dist = dist;
        cur.host = host.cell;
        cur.user2 = u2.cell;
        cur.field = host.field;
        cur.u2_char = u2.chr;
        const frm::Sample prev = g_hist.last();
        g_hist.push(cur);
        g_fast_ask = fast_check(prev, cur, st, now, near_since0);
        fast_watch(now);
    }
    control(host, u2, known, rel, now, dist_text, read_us);
    g_fast_ask = false;
    if (!force_log) return;
    std::string near_text;
    if (known) {
        u2s::Cell cells[256];
        const size_t n = stage_cells(host.sm, cells, 256);
        const u2s::Near r = u2s::near_counts(cells, n, u2.cell, host.cell);
        near_text = std::format(" | host schedule stages read {}: within 3/4/5 cells of user 2 {}/{}/{}; from the host {}..{} "
                                "cells",
                                r.counted, r.within[0], r.within[1], r.within[2], r.counted ? r.min_from : 0,
                                r.max_from);
        const size_t k = stage_cells(u2.sm, cells, 256);
        const u2s::Near q = u2s::near_counts(cells, k, u2.cell, u2.cell);
        near_text += std::format(" | user 2's schedule stages read {}: from user 2 {}..{} cells", q.counted,
                                 q.counted ? q.min_from : 0, q.max_from);
    }
    const std::string u_state = g_aborted.load()     ? "ABORTED"
                                : g_armed.load()     ? "ARMED"
                                : g_ever_armed.load() ? "drained"
                                                      : "off";
    const std::string text = std::format(
        "{} || {} || distance {}{} || U {} (would be {}) | arm check on user 2's manager: {}", facts_text("host", host),
        facts_text("user 2", u2), dist_text, near_text, m == u2s::Mode::On ? u_state : std::string("probe"),
        g_machine.state == u2s::Would::On ? "ON" : "OFF", u2s::arm_name(arm));
    // The state line: on change of the compact facts, else every 30 s.
    const std::string compact = std::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}", host.f.state, u2.f.state,
                                            host.f.sm_pending, u2.f.sm_pending, u2.f.fm_pending, u2.f.seq,
                                            u2.f.sm_count, dist, static_cast<int>(g_machine.state), u2.chr != 0,
                                            static_cast<int>(rel), u_state);
    if (compact != g_last_state || now - g_last_state_us >= 30'000'000) {
        g_last_state = compact;
        g_last_state_us = now;
        log::info("User2Stages [state] {} | {}", p8_when(now), text);
    }
}

// ---------------------------------------------------------------- U-e: the module thread's checks (each second)

uint64_t g_r42_ring[60] = {}, g_r42_loop_ring[60] = {};
uint64_t g_r42_tick = 0, g_drain_checked_us = 0;

std::string ctl_keys_left(uint64_t drain_us, bool blocked = false) {
    std::string s;
    size_t n = 0;
    decltype(g_ctl_keys)::View v;
    for (size_t i = 0; i < decltype(g_ctl_keys)::capacity() && n < 8; ++i) {
        if (!g_ctl_keys.get(i, v) || !(v.flags & u2s::kKeyAssigned) || (v.flags & u2s::kKeyRemoved) ||
            v.put_us >= drain_us || ((v.flags & u2s::kKeyCleanBlocked) != 0) != blocked) {
            continue;
        }
        s += std::format(" 0x{:08X}", v.key);
        ++n;
    }
    return s;
}

void npc_control_tick(uint64_t now) {
    const u2s::Mode cm = ctl_mode_now();
    if (cm == u2s::Mode::Off) return;
    // Reason-42 logouts of the assigned keys in the last minute (one cumulative sample a second).
    // S43: and of them, the same-slot repeats (the Reason42 abort's count; the raw count is only logged).
    const uint64_t cum = g_ctl_r42[1].load(std::memory_order_relaxed);
    const uint64_t loop_cum = g_ctl_r42_loop[1].load(std::memory_order_relaxed);
    const uint64_t old = g_r42_ring[(g_r42_tick + 1) % 60], loop_old = g_r42_loop_ring[(g_r42_tick + 1) % 60];
    g_r42_ring[g_r42_tick % 60] = cum;
    g_r42_loop_ring[g_r42_tick % 60] = loop_cum;
    ++g_r42_tick;
    g_ctl_r42_minute = static_cast<uint32_t>(cum - std::min(old, cum));
    g_ctl_r42_loop_minute = static_cast<uint32_t>(loop_cum - std::min(loop_old, loop_cum));
    // The keys alive 5 s after a drain of user 2's manager (once per drain).
    const uint64_t drain = g_last_drain_us.load(std::memory_order_relaxed);
    // 10 s: the cleanup runs at user 2's first Req 5 s after the drain (review 3).
    const bool check_drain = drain && now >= drain + 10'000'000 && g_drain_checked_us != drain;
    const uint16_t kind = cm == u2s::Mode::On ? u2s::kKeyAssigned : u2s::kKeyWould;
    const u2s::CtlStats st = u2s::ctl_stats(g_ctl_keys, kind, now, check_drain ? drain : 0);
    g_ctl_last_stats = st;
    if (check_drain) {
        g_drain_checked_us = drain;
        if (st.left_after_drain) {
            log::warn("User2Stages [control leftover] {} | {} NPC(s) {} before the drain of user 2's manager {:.1f} s "
                      "ago still alive (no server Logout seen):{}",
                      p8_when(now), st.left_after_drain,
                      cm == u2s::Mode::On ? "given to user 2" : "that U-e would have given",
                      static_cast<double>(now - drain) / 1e6,
                      cm == u2s::Mode::On ? ctl_keys_left(drain) : std::string(" (probe: not listed)"));
        }
        if (st.drain_blocked) {
            // Segunda revisión (R1): resultados POR UNIDAD de la limpieza de familias (bloqueo hoja-raíz o retirada
            // fallida, con sus reintentos acotados): un aviso, nunca el aborto LeftAfterDrain (AUDITORIA §6).
            log::warn("NpcFamily [control leftover blocked] {} | {} NPC(s) given to user 2 before the drain {:.1f} s ago "
                      "still alive because a family cleanup could not retire them or a child of theirs (per unit, "
                      "bounded retries; not an abort; after-drain reruns {}):{}",
                      p8_when(now), st.drain_blocked, static_cast<double>(now - drain) / 1e6,
                      g_cleanup_reruns_run.load(), ctl_keys_left(drain, true));
        }
    }
    if (cm != u2s::Mode::On || g_ctl_aborted.load()) return;
    // Review 7: log only until Shadow measures how often a guard at its post sends a Req.
    static uint64_t undriven_logged = 0;
    if (u2s::ctl_undriven(st) && now >= undriven_logged + 60'000'000) {
        undriven_logged = now;
        log::warn("User2Stages [control undriven] {} | of {} NPCs given to user 2 10..120 s ago (and alive >= 10 s), "
                  "{} ever got a Req of user 2 (log only; with cdmp_peer none ever does)",
                  p8_when(now), st.eligible, st.eligible_driven);
    }
    u2s::CtlAbortIn in;
    in.s = st;
    in.r42_loop_last_minute = g_ctl_r42_loop_minute;
    in.r42_raw_last_minute = g_ctl_r42_minute;
    in.drain_checked = check_drain;
    // S33: la contención de npc_activation=1 (progreso real, retirada por unidad) sustituye este aborto sólo mientras
    // está lista; si le falta una sonda o falla, vuelve esta red (revisión: siempre hay una). Las claves de una unidad
    // abandonada (kKeyAbandoned) no cuentan como 'unaccepted' (ctl_stats): un abandono por unidad nunca la dispara.
    // S35 (AUDITORIA.md §4, degradación): con npc_degradation=1 y npc_activation=1, una contención no lista ya NO
    // rearma este vigilante de 5 s (npcact::legacy_watchdog_armed); con 0/probe sigue como antes. probe: se cuenta
    // cuándo lo habría evitado.
    const npcact::Degrade deg = degrade_now();
    in.accept_tracked = child_hold_active() && npcact::legacy_watchdog_armed(deg);
    // Segundos en que la red antigua estaría armada por una contención no lista con npc_activation=1: con 1 no se
    // rearmó, con probe sí (y se cuenta lo que se habría evitado).
    if (child_hold_active() && deg.activation_on && !deg.ready && g_degrade_mode.load(std::memory_order_relaxed)) {
        g_degrade_would.fetch_add(1, std::memory_order_relaxed);
    }
    const u2s::CtlAbort ab = u2s::ctl_abort(in);
    if (ab == u2s::CtlAbort::None) return;
    g_ctl_aborted.store(true);
    // The unaccepted ones go (reason 25) at user 2's next Req exit: their stage makes them again with the host.
    if (ab == u2s::CtlAbort::Unaccepted) g_cleanup_unaccepted_req.store(true);
    log::error("User2Stages [CONTROL ABORT] {} | {} | tracked {} (alive {}, removed {}), reason 42 {} (last minute: "
               "raw {}, same-slot repeats {} (the abort's count), first in their slot {}; with no slot {} in all, slot "
               "table full {}), "
               "driven by user 2 {} (eligible {}: driven {}), primer retorno externo 0 (no demuestra aplicación) {} / unaccepted after 5 s {}, "
               "host client Reqs {}, wrong 2005s {}, left after the drain {} | user2_npc_control stops assigning for "
               "this run{}; U-1's drains go on",
               p8_when(now), u2s::ctl_abort_name(ab), st.tracked, st.alive, st.removed, st.r42, g_ctl_r42_minute,
               g_ctl_r42_loop_minute, g_ctl_r42_minute - std::min(g_ctl_r42_loop_minute, g_ctl_r42_minute),
               g_ctl_r42_noslot[1].load(), g_r42_slots.overflow(),
               st.driven, st.eligible, st.eligible_driven, st.accepted, st.unaccepted, st.host_req, st.bad_2005,
               st.left_after_drain,
               ab == u2s::CtlAbort::Unaccepted ? "; the unaccepted ones are logged out at user 2's next Req"
                                               : "; the NPCs already given to user 2 stay");
}

// ---------------------------------------------------------------- lines

std::string ret_text(uint32_t rva) { return rva ? std::format("exe+0x{:X}", rva) : std::string("outside the exe"); }

std::string line_of(const p::Event& e) {
    const uint64_t* v = e.v;
    const std::string when = p8_when(e.qpc_us);
    const auto w = static_cast<Who>(v[8]);
    switch (e.kind) {
        case kEClose: {
            float x = 0, z = 0;
            const auto xb = static_cast<uint32_t>(v[6]), zb = static_cast<uint32_t>(v[7]);
            std::memcpy(&x, &xb, 4);
            std::memcpy(&z, &zb, 4);
            return std::format("User2Stages [close] {} T{} | stage 0x{:X} of 0x{:08X} ({}) reason {}{} | called from {} | "
                               "live NPC entries {} of {} | stage at ({:.0f}, {:.0f})",
                               when, e.tid, v[0], static_cast<uint32_t>(v[1]), u2s::who_name(w), v[2],
                               *u2s::reason_name(static_cast<uint8_t>(v[2]))
                                   ? std::format(" = {}", u2s::reason_name(static_cast<uint8_t>(v[2])))
                                   : std::string(),
                               ret_text(static_cast<uint32_t>(v[3])), v[4], v[5], x, z);
        }
        case kEDelete:
            return std::format("User2Stages [delete] {} T{} | DeleteStage key 0x{:X} of the schedule manager of 0x{:08X} "
                               "({}) reason {} | caller {}",
                               when, e.tid, v[0], static_cast<uint32_t>(v[1]), u2s::who_name(w), v[2],
                               ret_text(static_cast<uint32_t>(v[3])));
        case kELoad:
            return std::format("User2Stages [owner load] {} T{} | 0x287DA50(mgr, {}, {}) puts the manager 0x{:X} of "
                               "0x{:08X} ({}) in state 1 (was {}) | ExecuteTroc subject: {} (opcode {})",
                               when, e.tid, v[5], v[6], v[0], static_cast<uint32_t>(v[1]), u2s::who_name(w), v[4],
                               v[2] ? "user 2" : "none (the host's or another)", v[3]);
        case kEUnload:
            return std::format("User2Stages [unload] {} T{} | 0x287D4C0 on the manager 0x{:X} of 0x{:08X} ({}) state {} | "
                               "lists: sequencers {} schedules {} factions {} | subject {}",
                               when, e.tid, v[0], static_cast<uint32_t>(v[1]), u2s::who_name(w), v[5], v[2], v[3], v[4],
                               v[6] ? "user 2" : "none");
        case kECreate:
        {
            float x = 0, z = 0;
            const auto xb = static_cast<uint32_t>(v[5]), zb = static_cast<uint32_t>(v[6]);
            std::memcpy(&x, &xb, 4);
            std::memcpy(&z, &zb, 4);
            return std::format("User2Stages [create] {} T{} | schedule stage of data 0x{:X} at ({:.0f}, {:.0f}) in "
                               "the manager of 0x{:08X} ({}) (stages {} weight {}) | this stage alive in the other "
                               "player's (same data, same place): {}",
                               when, e.tid, v[1], x, z, static_cast<uint32_t>(v[0]), u2s::who_name(w), v[3], v[4],
                               v[2] == 1 ? "YES (a duplicate)" : v[2] == 0 ? "no" : "not checked");
        }
        case kETimer:
        case kETimerDirect:
            return std::format("User2Stages [timer] {} T{} | {}{} reached the manager 0x{:X} of 0x{:08X} ({}){} | "
                               "subject {} (opcode {})",
                               when, e.tid, u2s::timer_name(static_cast<u2s::Timer>(v[0])),
                               v[4] ? " (direct, inside a stage update)" : "", v[3], static_cast<uint32_t>(v[2]),
                               u2s::who_name(w),
                               v[0] == static_cast<uint64_t>(u2s::Timer::T2043) && v[7] != 0xFF
                                   ? std::format(" in state {}, reschedule {}", v[7], v[6])
                                   : std::string(),
                               v[1] ? "user 2" : "none", v[5]);
        case kELogout: {
            const auto cells = [](uint64_t d) {
                return d == UINT32_MAX ? std::string("?") : std::format("{}", d);
            };
            return std::format("User2Stages [logout] {} T{} | ServerActor::Logout (server) of 0x{:08X} ({}) reason {} | "
                               "called from {} | cells from the host {}, from user 2 {}",
                               when, e.tid, static_cast<uint32_t>(v[0]), u2s::who_name(w), v[1],
                               ret_text(static_cast<uint32_t>(v[2])), cells(v[3]), cells(v[4]));
        }
        case kEPrep:
            return std::format("User2Stages [prepare] {} T{} | 0x287EF60 on the manager 0x{:X} of 0x{:08X} ({}) state {} "
                               "for character 0x{:08X} | subject {} (opcode {})",
                               when, e.tid, v[0], static_cast<uint32_t>(v[1]), u2s::who_name(w), v[3],
                               static_cast<uint32_t>(v[2]), v[4] ? "user 2" : "none", v[5]);
        case kECtl: {
            switch (static_cast<Ctl>(v[0])) {
                case kCtlArm:
                    return std::format(
                        "User2Stages [ARMED] {} T{} | 0x287F450 on user 2's manager 0x{:X}: state {} -> {} ({}) | "
                        "before it, its map +0x70 {} +0x74 {} | its 2043 chain: {} | at the exit of its Req {} | the "
                        "gate (U-a) is live{}",
                        when, e.tid, v[1], v[2], v[3], arm_why_name(v[6]), static_cast<uint32_t>(v[7]),
                        static_cast<uint32_t>(v[7] >> 32),
                        (v[4] & 0x0F) == 1
                            ? std::format("{}, a 2043 queued (result {}, EnqueueTimer delay {}: a period of ~{} s)",
                                          v[4] & 0x10 ? "dead (no rescheduling tick for 1.5 periods)"
                                                      : "ended (a tick met state 1, or its load)",
                                          static_cast<int32_t>((v[4] >> 8) & 0xFFFFFFFF), v[4] >> 40,
                                          (v[4] >> 40) / 10)
                        : (v[4] & 0x0F) == 2 ? std::string("dead or ended, NOT queued (EnqueueTimer faulted)")
                                             : std::string("alive on its own manager, nothing queued"),
                        v[5], v[9] ? " | FAST (user2_fast_rearm=1)" : "");
                case kCtlOneShot:
                    return std::format(
                        "User2Stages [FAST REARM] {} T{} | one-shot 2043 for arm {} on user 2's manager 0x{:X}: {} | "
                        "payload +0xB 0 (evaluates, never reschedules), EnqueueTimer delay {} = {} ms | the chain's next "
                        "tick would have come in {} ms (chain {} at the arm; period {} ms) | at the exit of its Req {}",
                        when, e.tid, v[3], v[1],
                        v[4] == 1   ? std::format("QUEUED (result {})", static_cast<int32_t>(v[2]))
                        : v[4] == 2 ? std::format("NOT queued: EnqueueTimer returned {}", static_cast<int32_t>(v[2]))
                        : v[4] == 5 ? std::string("queued by the game (result 0) but the slot REFUSED it: its "
                                                  "tick counts as a normal one")
                                    : std::string("NOT queued: one is already pending / queued for this arm, or the cap"),
                        v[9], frm::delay_ms(static_cast<uint32_t>(v[9])), v[6],
                        (v[7] & 0xFF) ? "queued anew" : "left as it was", v[7] >> 8, v[5]);
                case kCtlOneShotTick:
                    if (v[9]) {
                        return std::format(
                            "User2Stages [FAST REARM] {} T{} | the one-shot 2043 of arm {} reached user 2's manager 0x{:X} "
                            "(state {}) {} ms after it was queued: {} | ~{} ms before the chain's next tick | not counted "
                            "as a chain tick",
                            when, e.tid, v[3], v[1], v[2], v[4],
                            v[2] == 2 ? "it evaluates its cells now" : "NOT in state 2 (no evaluation from it)", v[6]);
                    }
                    return std::format("User2Stages [FAST REARM] {} T{} | a one-shot-shaped 2043 (+0xB 0, +0xC 0) reached "
                                       "user 2's manager 0x{:X} (state {}) AFTER the one-shot of arm {} was {}: late, "
                                       "counted, not a chain tick",
                                       when, e.tid, v[1], v[2], v[3],
                                       v[7] ? std::format("cancelled ({})", frm::cancel_name(static_cast<frm::Cancel>(v[7])))
                                            : std::string("lost"));
                case kCtlOneShotCancel:
                    return std::format("User2Stages [FAST REARM] {} T{} | the pending one-shot 2043 of arm {} cancelled: "
                                       "{} (its timer stays queued <= 100 ms: if it fires within 5 s it is counted as late)",
                                       when, e.tid, v[3], frm::cancel_name(static_cast<frm::Cancel>(v[4])));
                case kCtlArmRefused:
                    return std::format("User2Stages [arm refused] {} T{} | user 2's manager 0x{:X} state {} | {}{} | at the "
                                       "exit of its Req {} | nothing called",
                                       when, e.tid, v[1], v[2], arm_why_name(v[6]),
                                       v[6] == kWhyFastDist
                                           ? (v[4] == UINT32_MAX ? std::string(" (distance unknown)")
                                                                 : std::format(" ({} cells)", v[4]))
                                           : std::string(),
                                       v[5]);
                case kCtlDrain:
                    return std::format("User2Stages [DRAINED] {} T{} | {} | 0x287DA50 on user 2's manager 0x{:X}: state {} "
                                       "-> {} (its immediate 2043 runs 0x287D4C0 on it: its stages close with reason 19) "
                                       "| at the exit of its Req {}",
                                       when, e.tid, cause_name(v[4]), v[1], v[2], v[3], v[5]);
                case kCtlDrainSync:
                    return std::format("User2Stages [DRAINED now] {} T{} | {} | 0x287DA50 + 0x287D4C0 on user 2's manager "
                                       "0x{:X}: state {} -> {} | {}",
                                       when, e.tid, cause_name(v[4]), v[1], v[2], v[3],
                                       v[4] == kCauseHostPrep
                                           ? std::string("at the host's preparation 0x287EF60, before its evaluation")
                                       : (v[5] & 0xF000) == 0xF000
                                           ? std::format("at ServerActor::Logout of its character (reason {})",
                                                         v[5] & 0xFFF)
                                           : std::format("before its {} reaches the game", v[5]));
                case kCtlBuilt:
                    return std::format("User2Stages [lists built] {} T{} | 0x2896C60 + 0x287FA30 on user 2's manager 0x{:X} "
                                       "(the native preparation's steps after its arm) | id list [+0x150] {} -> {} | "
                                       "category table [+0x408] {} | actor 0x{:X} (key 0x{:08X})",
                                       when, e.tid, v[1], v[2], v[3], v[4], v[6], static_cast<uint32_t>(v[7]));
                case kCtlChainRestart:
                    return std::format("User2Stages [chain restarted] {} T{} | no 2043 reached user 2's manager 0x{:X} "
                                       "(state {}) for {} ms after its arm / its last tick (1.5 periods = {} ms): {} | "
                                       "at the exit of its Req {} | NoTicks aborts only if none comes after this",
                                       when, e.tid, v[1], v[2], v[6], v[7] * 3 / 2,
                                       (v[4] & 0x0F) == 1
                                           ? std::format("a 2043 queued (result {}, EnqueueTimer delay {} = a period "
                                                         "of {} ms; once per arm)",
                                                         static_cast<int32_t>((v[4] >> 8) & 0xFFFFFFFF), v[4] >> 40,
                                                         v[7])
                                           : std::string("EnqueueTimer FAULTED"),
                                       v[5]);
                case kCtlCleanup:
                    if (v[9]) {
                        return std::format("User2Stages [control cleanup] {} T{} | SKIPPED: {} ({} NPC(s) still given to "
                                           "user 2 wait for the next round) | Troc {}",
                                           when, e.tid,
                                           v[9] == 1 ? "the actor lookup [exe+0x6D69698] is 0"
                                                     : "the actor lookup's SAM pointer [[exe+0x6D69698]] is 0",
                                           v[1], v[5]);
                    }
                    return std::format("User2Stages [control cleanup] {} T{} | {}: {} NPC(s) still given to user 2 -> "
                                       "{} logged out (ServerActor::Logout reason 25 in SAM::FindActor's scope), {} "
                                       "already gone, {} with another key or controller (left alone){}{} | Troc {}",
                                       when, e.tid,
                                       v[6] == kCleanAfterDrain   ? "5 s after a drain of its manager"
                                       : v[6] == kCleanItsTroc    ? "at its load / goodbye Troc's entry"
                                       : v[6] == kCleanUnaccepted ? "no accepted StartActionReq 5 s after their birth "
                                                                    "(after the abort: the stage respawns them with "
                                                                    "the host)"
                                                                  : "at its character's Logout",
                                       v[1], v[2], v[3], v[4], v[7] ? " | A FAULT: U-e aborted" : "",
                                       v[11] ? std::format(" | families: pass {}, {} left alive per unit (blocked leaf "
                                                           "to root or a failed retirement; never the drain abort)",
                                                           v[11], v[10])
                                             : std::string(),
                                       v[5]);
                case kCtlGoodbyeArmed:
                    return std::format("User2Stages [goodbye while ARMED] {} T{} | user 2's 2001 reached the server with "
                                       "its manager 0x{:X} armed | its stages are unloaded at ServerActor::Logout of its "
                                       "character ('DRAINED now | its logout'); if that line is missing, nothing did",
                                       when, e.tid, v[1]);
                case kCtlDrainRefused:
                    return std::format("User2Stages [drain refused] {} T{} | {} | manager 0x{:X} | {} | opcode {}", when,
                                       e.tid, cause_name(v[4]), v[1], arm_why_name(v[6]), v[5]);
            }
            return std::format("User2Stages [control] {} ?", when);
        }
        case kEElem:
        case kEDup:
        {
            if (v[1] == 2) {
                float x = 0, z = 0;
                const auto xb = static_cast<uint32_t>(v[3]), zb = static_cast<uint32_t>(v[4]);
                std::memcpy(&x, &xb, 4);
                std::memcpy(&z, &zb, 4);
                return std::format("User2Stages [not created] {} T{} | {}'s manager, static stage index 0x{:X} at ({:.0f}, {:.0f}): "
                                   "a schedule element NOT CREATED (U-f2: the other player's manager has it alive, "
                                   "stage 0x{:X}; *res = the native 'no stage' code)",
                                   when, e.tid, w == Who::User2 ? "user 2" : "the host", v[2], x, z, v[5]);
            }
            if (v[9]) {  // a sequencer: its resource name and U2's verdict
                std::string name;
                for (size_t i = 0; i < 40; ++i) {
                    const auto c = static_cast<unsigned char>(v[3 + i / 8] >> (8 * (i % 8)));
                    if (!c) break;
                    name += (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
                }
                std::string t1;
                if (v[10] & 0xFF) {
                    const auto t = static_cast<u2sh::T1>((v[10] & 0xFF) - 1);
                    const bool may = (v[11] & 1) != 0;
                    const auto hash = static_cast<uint32_t>(v[11] >> 32);
                    if (v[10] & 0x100) {  // S46 phase 3: T1 active decided this verdict
                        t1 = std::format(" | S46 T1 (ACTIVE, by the exact hash 0x{:08X} of its name): {}{}", hash,
                                         t == u2sh::T1::NotTrade ? "not on the trade list"
                                         : t == u2sh::T1::Near   ? "on the trade list, but < 10 cells or the drain waits"
                                                                 : "on the trade list, >= 10 cells",
                                         t == u2sh::T1::WouldPass && !may
                                             ? "; the shop route cannot prepare now (user2_shop_route 0 / requirements)"
                                             : "");
                    } else {
                        t1 = std::format(" | S46 T1 (verdict only, nothing prepared): [{}] {} (hash 0x{:08X}){}",
                                         t == u2sh::T1::WouldPass ? "T1 would pass" : "T1 would skip", u2sh::t1_name(t),
                                         hash,
                                         t == u2sh::T1::WouldPass && !may
                                             ? "; the shop route cannot prepare now (user2_shop_route 0 / requirements)"
                                             : "");
                    }
                }
                return std::format("User2Stages [element] {} T{} | user 2's manager, static stage index 0x{:X}: a {} "
                                   "element '{}' -> {}{}",
                                   when, e.tid, v[2], u2s::element_name(static_cast<u2s::Element>(v[0])), name,
                                   u2sh::elem_verdict_name(static_cast<u2sh::ElemVerdict>(v[9] - 1)), t1);
            }
            return std::format("User2Stages [element] {} T{} | user 2's manager, static stage index 0x{:X}: a {} element -> {}",
                               when, e.tid, v[2], u2s::element_name(static_cast<u2s::Element>(v[0])),
                               v[1] ? "SKIPPED (U-f: *res = 0, the original not called)" : "to the original");
        }
        case kEEval: {
            const u2s::Cell c = u2s::unpack_cell(v[9]);
            return std::format("User2Stages [evaluation] {} T{} | the cell evaluation of the manager 0x{:X} of 0x{:08X} "
                               "({}) near cell ({},{}) | its id list [+0x150] {} | the collector's candidates {} | its "
                               "active map +0x74 {}, list +0xF0 {}, category 0's count +0x408 {} | the context's quest manager of 0x{:08X}: ids "
                               "+0x178 {} / +0x188 {} (+0x186 {}), stage states +0x4C {}",
                               when, e.tid, v[0], static_cast<uint32_t>(v[1]), u2s::who_name(w), c.x, c.z, v[2], v[3],
                               static_cast<uint32_t>(v[4]), static_cast<uint32_t>(v[4] >> 32), v[10],
                               static_cast<uint32_t>(v[5]), static_cast<int32_t>(v[6] & 0xFFFFFFFF),
                               static_cast<int32_t>(v[6] >> 32), v[7] & 0xFF, static_cast<uint32_t>(v[7] >> 8));
        }
        case kENpcCtl:
            return std::format("User2Stages [{}] {} T{} | NPC 0x{:08X} (type {}) of user 2's stage "
                               "(CreateServerActorDesc_NPCSchedule, born in its {}; its vf48 gave its own manager: {}): "
                               "controller 0x{:08X} (the field owner, the host) {} 0x{:08X} (user 2) at 0x2792A7B{} | "
                               "schedule handle data 0x{:08X} node 0x{:X} [H]: a live stage of it in user 2's manager "
                               "0x{:X}, in the host's 0x{:X}",
                               v[5] ? "control" : "would control", when, e.tid, static_cast<uint32_t>(v[0]), v[3], v[4],
                               v[7] ? "yes" : "NO", static_cast<uint32_t>(v[2]), v[5] ? "->" : "would be",
                               static_cast<uint32_t>(v[1]), v[5] ? " (eax only)" : " (probe: nothing written)",
                               static_cast<uint32_t>(v[6]), static_cast<uint32_t>(v[6] >> 32), v[9], v[10]);
        case kEPrepDrain: {
            const auto pd = static_cast<u2s::PrepDrain>(v[0]);
            const auto ret = static_cast<uint32_t>(v[1]);
            const auto cnt = [](uint64_t x, int sh) { return static_cast<uint32_t>((x >> sh) & 0xFFFFF); };
            const uint64_t since_ms = v[11] >> 32;
            return std::format("User2Stages [{}] {} T{} | 0x287EF60 on the host's manager (state {}) called from {} "
                               "({}) | {} | user 2's Trocs in flight {} | U-1 armed {}{}{} | the last drain of user 2's "
                               "manager {} | ExecuteTroc subject {} (opcode {}) | distance {} (off threshold {}) | user "
                               "2's manager 0x{:X} state {}, lists: sequencers {} schedules {} factions {} | {}",
                               v[10] == 2   ? "drain at host prepare"
                               : v[10] == 1 ? "would drain at host prepare"
                                            : "host prepare, no drain",
                               when, e.tid, v[9], u2s::prep_caller_name(ret), ret_text(ret),
                               v[2] & 1 ? "a server thread (TLS +0x1EC)" : "NOT a server thread",
                               static_cast<int32_t>(v[11] & 0xFFFFFFFF), v[2] & 2 ? "yes" : "no",
                               v[2] & 4 ? ", stages left by an async drain (review 8)" : "",
                               v[2] & 8 ? ", probe: mode 1 would have HELD the distance drain for this preparation" : "",
                               since_ms >= 0xFFFFFFFF ? std::string("none")
                                                      : std::format("{} ms before ({})", since_ms,
                                                                    cause_name(g_last_drain_cause.load())),
                               (v[3] & 0xFF) ? "user 2" : "none (the host's or another)", v[3] >> 8,
                               v[4] == UINT32_MAX ? std::string("unknown") : std::format("{} cells", v[4]),
                               u2s::Thresholds{}.off_cells, v[5], v[6], cnt(v[7], 0), cnt(v[7], 20), cnt(v[7], 40),
                               v[10] == 2   ? std::string("user 2's manager drained now (0x287DA50 + 0x287D4C0): see "
                                                          "'DRAINED now'")
                               : v[10] == 1 ? std::string("probe: nothing called")
                                            : std::format("not drained: {}", u2s::prep_drain_name(pd)));
        }
        case kERoute:
            return std::format("User2Stages [route] {} T{} | user 2's {} (vf48 from its Troc{}) -> {} 0x{:X}", when,
                               e.tid, v[0],
                               v[3] ? std::format(", inside its tick, returning to {}", ret_text(static_cast<uint32_t>(v[3])))
                                    : std::string(", the handler's"),
                               v[1] == static_cast<uint64_t>(u2s::Route::Own) ? "ITS OWN manager"
                                                                               : "would go to its own manager",
                               v[2]);
        default:
            return std::format("User2Stages: event {}?", e.kind);
    }
}

void drain() {
    p::Event e;
    while (g_ring.pop(e)) {
        if (e.kind >= kKindCount) continue;
        if (g_budget.take_line(e.flags)) {
            if (e.kind == kECtl && (e.v[0] == kCtlArmRefused || e.v[0] == kCtlDrainRefused || e.v[0] == kCtlGoodbyeArmed)) {
                log::warn("{}", line_of(e));
            } else {
                log::info("{}", line_of(e));
            }
        }
    }
}

template <size_t N>
std::string table_text(const u2s::CountTable<N>& t) {
    typename u2s::CountTable<N>::Row rows[N];
    const size_t n = t.rows(rows, N);
    std::sort(rows, rows + n, [](const auto& a, const auto& b) { return a.count > b.count; });
    std::string s;
    for (size_t i = 0; i < n && i < 24; ++i) {
        const u2s::CloseKey k = u2s::split_close_key(rows[i].key);
        s += std::format(" [reason {} {} from {}] x{};", k.reason, u2s::who_name(k.owner), ret_text(k.ret_rva),
                         rows[i].count);
    }
    if (t.overflow()) s += std::format(" (overflow {})", t.overflow());
    return s.empty() ? std::string(" none") : s;
}

std::string by_who(const std::atomic<uint64_t>* a) {
    return std::format("host {} / user 2 {} / other {} / unknown {}", a[0].load(), a[1].load(), a[2].load(),
                       a[3].load());
}

std::string routes_text() {
    std::string s;
    for (size_t t = 0; t < static_cast<size_t>(u2s::Timer::Count); ++t) {
        const auto tm = static_cast<u2s::Timer>(t);
        s += std::format(" {}: ", tm == u2s::Timer::NotTimer ? "direct" : u2s::timer_name(tm));
        for (int su = 0; su < 2; ++su) {
            s += std::format("{}[subject {}: host {} user2 {} other {} unknown {}]", su ? " " : "",
                             su ? "user 2" : "none", g_routes.get(tm, su, Who::Host), g_routes.get(tm, su, Who::User2),
                             g_routes.get(tm, su, Who::Other), g_routes.get(tm, su, Who::Unknown));
        }
        s += ";";
    }
    return s;
}

// Review M3: the other vf48 calls inside user 2's timer Trocs (they keep the field owner's answer).
std::string timer_vf48_text() {
    u2s::CountTable<64>::Row rows[64];
    const size_t n = g_timer_vf48.rows(rows, 64);
    std::sort(rows, rows + n, [](const auto& a, const auto& b) { return a.count > b.count; });
    std::string s;
    for (size_t i = 0; i < n && i < 16; ++i) {
        s += std::format(" [{} {} from {}] x{};", (rows[i].key >> 32) & 0xFFFF,
                         rows[i].key >> 48 ? "its own" : "the owner's", ret_text(static_cast<uint32_t>(rows[i].key)),
                         rows[i].count);
    }
    if (g_timer_vf48.overflow()) s += std::format(" (overflow {})", g_timer_vf48.overflow());
    return s.empty() ? std::string(" none") : s;
}

std::string budget_text() {
    std::string s;
    for (size_t k = 0; k < kKindCount; ++k) {
        const size_t h = u2s::limit_class(k, false), u = u2s::limit_class(k, true);
        s += std::format(" {} {}/{} | {}/{};", kKindNames[k], g_budget.limited(h), g_budget.over(h),
                         g_budget.limited(u), g_budget.over(u));
    }
    return s;
}

std::string u1_text() {
    const auto r = [](u2s::Route k) {
        return std::format("{}/{}/{}", g_route[static_cast<size_t>(k)][0].load(), g_route[static_cast<size_t>(k)][1].load(),
                           g_route[static_cast<size_t>(k)][2].load());
    };
    std::string el;
    for (size_t i = 0; i < static_cast<size_t>(u2s::Element::Count); ++i) {
        el += std::format(" {} {} (skipped {});", u2s::element_name(static_cast<u2s::Element>(i)), g_elem_seen[i].load(),
                          g_elem_skipped[i].load());
    }
    el += std::format(" its sequencers by U2's verdict (user2_stages_elements={}):",
                      u2sh::elements_name(static_cast<u2sh::Elements>(g_elements.load())));
    for (size_t i = 0; i < static_cast<size_t>(u2sh::ElemVerdict::Count); ++i) {
        if (const uint64_t n = g_elem_verdicts[i].load()) {
            el += std::format(" [{}] {};", u2sh::elem_verdict_name(static_cast<u2sh::ElemVerdict>(i)), n);
        }
    }
    const auto list_mode = static_cast<u2s::Mode>(g_trade_list.load());
    if (list_mode == u2s::Mode::On) {
        el += std::format(" S46 T1 (ACTIVE, user2_shop_trade_list=1{}) on its shop elements: not on the list {}, near {}, "
                          "on the list and far {}; births inside its trade scenes' prepare {} (U-e / families / roads "
                          "not consulted {}; the last NPC 0x{:08X} of static stage 0x{:X}; trade-list lines over their "
                          "limiter {});",
                          g_t1_latched.load()
                              ? std::format(", LATCHED OFF: user 2's manager prepared sequencer 0x{:X} of hash 0x{:08X}, "
                                            "not on the list",
                                            g_t1_latched_id.load(), static_cast<uint32_t>(g_t1_latched_hash.load()))
                              : std::string(),
                          g_t1[0].load(), g_t1[1].load(), g_t1[2].load(), g_t1_origin_births.load(),
                          g_t1_origin_excluded.load(), g_t1_origin_last_key.load(), g_t1_origin_last_stage.load(),
                          g_t1_lines_limited.load());
    } else if (shop_trade_probe_active() || list_mode == u2s::Mode::Probe) {
        el += std::format(" S46 T1 (verdict only) on the shop elements skipped: not on the list {}, near {}, would pass {} "
                          "(trade-list lines over their limiter {});",
                          g_t1[0].load(), g_t1[1].load(), g_t1[2].load(), g_t1_lines_limited.load());
        if (list_mode == u2s::Mode::Probe && g_t1_origin_births.load()) {
            el += std::format(" births inside a trade scene's prepare {} (U-e / families / roads changed {});",
                              g_t1_origin_births.load(), g_t1_origin_changed.load());
        }
    }
    return std::format("U-1 ({}): arms {} (refused {}), drains {} (at once: loads / logout / abort {}; at its logout "
                       "{}), schedules not created (alive in the other's: user 2 {}, host {}), gate passed {} (would {}), collector's guard on its own manager {} (would {}; FindUser had found "
                       "nobody {}; the context's quest manager is the host's by the rule: its character 0x{:08X}{}, "
                       "vtable exe+0x{:X}), vf40 of its tick: the "
                       "context's {} and the others {} (all the owner's), cell "
                       "evaluations of its manager {} (ids in [+0x150] {}, collector candidates {}), other "
                       "vf48 of its tick answered with its own manager {}, its elements:{} | to user 2: 2335 of its own stages {}, 2322 for "
                       "it {} (after an arm {}) | U-h, its stages at the host's client: 2335 {} 2126 {} 2958 {} 2909 {} | "
                       "its 2001s {} (while armed {}) | aborted {} | game faults {} || timer route (user2_timer_route={}): "
                       "2043/2045/2046 to its own manager {} (would {}) | its chain on its own manager: rescheduling "
                       "ticks {}, ended {}, restarted after an arm {} | the host's manager: 2043s last minute {} (user 2's chain {}), baseline {}, {}",
                       mode_now() == u2s::Mode::On ? "mode 1" : mode_now() == u2s::Mode::Probe ? "probe" : "off",
                       g_arms.load(), g_arm_fails.load(), g_drains.load(), g_drains_sync.load(), g_logout_drains.load(),
                       g_dup_skip_u2.load(), g_dup_skip_host.load(), g_gate_passed.load(), g_gate_would.load(), g_gate2_passed.load(), g_gate2_would.load(),
                       g_gate2_native_null.load(), g_gate2_other_key.load(),
                       g_gate2_other_key.load() == g_host_key.load() ? " = the host" : "",
                       g_gate2_other_vt.load(),
                       g_tick_vf40_own.load(), g_tick_vf40_other.load(), g_evals_u2.load(), g_eval_u2_ids.load(),
                       g_eval_u2_collected.load(), g_tick_vf48_own.load(), el, g_2335_own_to_u2.load(), g_2322_to_u2.load(),
                       g_unexp_2322.load(), g_uh[0].load(), g_uh[1].load(), g_uh[2].load(), g_uh[3].load(),
                       g_2001_seen.load(), g_2001_armed.load(), g_aborted.load() ? "YES" : "no", g_faults.load(),
                       u2s::mode_name(route_mode_now()), r(u2s::Route::Own), r(u2s::Route::Would), g_chain_ticks.load(),
                       g_chain_ended.load() ? "yes" : "no", g_kicks.load(), g_host_minute, g_host_minute_u2,
                       g_baseline_n ? g_baseline_sum / g_baseline_n : 0, host_chains_text(p8_now_us())) +
           fast_text();  // S37 phase D: empty with user2_fast_rearm=0
}

std::string ctl_text() {
    std::string verdicts;
    for (size_t i = 0; i < static_cast<size_t>(u2s::Ctl::Count); ++i) {
        if (const uint64_t n = g_ctl_verdicts[i].load()) {
            verdicts += std::format(" [{}] {};", u2s::ctl_name(static_cast<u2s::Ctl>(i)), n);
        }
    }
    u2s::CountTable<32>::Row rows[32];
    const size_t n = g_ctl_descs.rows(rows, 32);
    std::sort(rows, rows + n, [](const auto& a, const auto& b) { return a.count > b.count; });
    std::string descs;
    for (size_t i = 0; i < n && i < 8; ++i) {
        descs += std::format(" [vtable exe+0x{:X} type {}] x{};", static_cast<uint32_t>(rows[i].key),
                             (rows[i].key >> 32) & 0xFF, rows[i].count);
    }
    std::string prep;
    for (size_t i = 0; i < static_cast<size_t>(u2s::PrepDrain::Count); ++i) {
        if (const uint64_t k = g_prep_verdicts[i].load()) {
            prep += std::format(" [{}] {};", u2s::prep_drain_name(static_cast<u2s::PrepDrain>(i)), k);
        }
    }
    const u2s::CtlStats& st = g_ctl_last_stats;
    return std::format(
        "U-e (user2_npc_control={}): hook 0x2792A7B {} | under user 2's subject:{} | other descriptors under its "
        "armed timers:{} | its NPCs ({}): tracked {} (alive {}, removed {}; table {} of {}, overflow {}), reason-42 "
        "logouts {} (would {} / assigned {}; last minute {}; S43, repeating a stage slot: would {} / assigned {}, last "
        "minute {} (the abort's count, > 10); with no slot (counted as repeats) would {} / assigned {}; first in their "
        "slot (distinct slots) {}; same slot but put before its last logout, not counted {}; slot table {} of {}, full {}), driven by user 2 {} (first Reqs {}), the host client's "
        "Reqs for them {}, 2005s to user 2 ok {} WRONG {}, at the host's client ok {} WRONG {}, user 2's key 0x{:08X} | "
        "primer retorno externo 0 (no demuestra aplicación) {} (from its birth: mean {} ms, max {} ms, >= 1 s {}) / unaccepted after 5 s now {} "
        "(+ alive in units abandoned by npc_activation, never counted {}), assigned 2005s with restore 0 at the host's client {} (must be 0) | "
        "aborted {} | not assigned (table full) {}, slots reused {} | cleanup (reason 25): runs {}, logged out {}, gone {}, "
        "other {}, busy {}, skipped (no SAM) {} || drain at host prepare (user2_drain_at_host_prepare={}): host preparations by verdict:{} | "
        "drains {} | distance drains held {} (expired {}) | module lock refusals {} | user 2's Trocs that waited for "
        "a drain at P {} (timed out {})",
        u2s::mode_name(ctl_mode_now()), g_ctl_hook_on.load() ? "in" : "OUT", verdicts.empty() ? " none" : verdicts,
        descs.empty() ? " none" : descs, ctl_mode_now() == u2s::Mode::On ? "given" : "would be given", st.tracked,
        st.alive, st.removed, g_ctl_keys.used(), decltype(g_ctl_keys)::capacity(), g_ctl_keys.overflow(), st.r42,
        g_ctl_r42[0].load(), g_ctl_r42[1].load(), g_ctl_r42_minute, g_ctl_r42_loop[0].load(), g_ctl_r42_loop[1].load(),
        g_ctl_r42_loop_minute, g_ctl_r42_noslot[0].load(), g_ctl_r42_noslot[1].load(), g_r42_slots.firsts(),
        g_r42_slots.concurrent(), g_r42_slots.used(), decltype(g_r42_slots)::capacity(), g_r42_slots.overflow(), st.driven, g_ctl_drive_first.load(),
        g_ctl_host_req.load(), g_ctl_2005_ok.load(), g_ctl_2005_bad.load(), g_ctl_host_2005_ok.load(),
        g_ctl_host_2005_bad.load(), g_ctl_key2.load(), g_ctl_accepted.load(),
        g_ctl_accepted.load() ? g_ctl_accept_us_sum.load() / g_ctl_accepted.load() / 1000 : 0,
        g_ctl_accept_us_max.load() / 1000, g_ctl_accept_slow.load(), st.unaccepted, st.abandoned,
        g_ctl_host_2005_r0.load(),
        g_ctl_aborted.load() ? "YES" : "no", g_ctl_untracked.load(),
        g_ctl_keys.reused(), g_cleanup_runs.load(), g_cleaned.load(), g_cleanup_gone.load(), g_cleanup_other.load(),
        g_cleanup_busy.load(), g_cleanup_no_sam.load(), u2s::mode_name(dprep_mode_now()), prep.empty() ? " none" : prep, g_prep_drains.load(),
        g_holds.load(), g_hold_expired.load(), g_busy_refusals.load(), g_troc_waits.load(), g_troc_wait_timeouts.load());
}

void summary() {
    std::string hooks;
    for (size_t i = 0; i < kPtCount; ++i) {
        hooks += std::format("{}{} {}", i ? " " : "", "CDLUNTSFPO"[i], g_pt_on[i].load() ? "in" : "OUT");
    }
    hooks += std::format(" | G {} E {}",
                         g_gate_own.load() ? "own" : shop_probe_gate_live() ? "ShopProbe's" : "OUT",
                         g_elem_on.load() ? "in" : "OUT");
    uintptr_t a = 0;
    uint16_t op = 0;
    bool funnels = false;
    user2_funnels_subject_now(a, op, funnels);
    log::info("User2Stages summary: hooks {} | timer subject (user2_funnels): {} | {} | {} | closes (C):{} | NPC entries "
              "closed {} | DeleteStage (D):{} | Logout (O, the server's) by reason, who, caller:{} (the client's 2516 "
              "handler's {}) | owner loads (L) {} | unloads (U) {} | preparations (P) {} | schedule stage creations (N) "
              "{}, checked against the other player's {}, same data alive there {} | timers -> manager reached:{} "
              "(direct schedule updates {}, direct faction updates {}) | other vf48 under its timers, by caller "
              "and answer (its own manager inside its armed tick, else the owner's):{} | budgets (rate-limited / over the line cap, host "
              "| user 2):{} | ring lost {}",
              hooks, funnels ? "in" : "OUT (no user 2 subject: its timers count as 'none')", u1_text(), ctl_text(),
              table_text(g_closes), by_who(g_closed_npcs), table_text(g_deletes), table_text(g_logouts),
              g_logout_client.load(), by_who(g_loads), by_who(g_unloads), by_who(g_preps), by_who(g_creates),
              g_create_checked.load(), g_create_same_other.load(), routes_text(), g_direct_sched.load(),
              g_direct_faction.load(), timer_vf48_text(), budget_text(), g_lost.load());
}

// ---------------------------------------------------------------- install

bool bytes_at(uintptr_t rva, std::span<const uint8_t> expected) {
    std::vector<uint8_t> actual(expected.size());
    return game::safe_read(g_base + rva, actual.data(), actual.size()) &&
           std::equal(actual.begin(), actual.end(), expected.begin());
}

bool install_elem() {
    if (!bytes_at(kElem, kElemBytes)) {
        log::error("User2Stages: E, the stage element 0x288A970 is not build 2976's code; REFUSED (mode 1 stays probe)");
        return false;
    }
    auto h = game::prepare_inline(
        g_base + kElem, reinterpret_cast<void*>(&elem_detour),
        [&](const std::vector<uint8_t>& o) { return o.size() == kElemPatch && std::memcmp(o.data(), kElemBytes, kElemPatch) == 0; },
        "User2Stages", "E, the stage element 0x288A970 (U-f)");
    if (!h) {
        log::error("User2Stages: E 0x288A970: the inline hook could not be prepared ({}); REFUSED",
                   game::inline_error_text(h.error()));
        return false;
    }
    const auto& orig = h->original_bytes();
    if (orig.size() != kElemPatch || std::memcmp(orig.data(), kElemBytes, kElemPatch) != 0) {
        log::error("User2Stages: E 0x288A970: the hook would patch {} bytes, {} were verified; REFUSED, nothing written",
                   orig.size(), kElemPatch);
        return false;
    }
    g_elem_hook = std::move(*h);  // the detour calls through it: in place before enabling
    if (!g_elem_hook.enable()) {
        log::error("User2Stages: E 0x288A970: the hook could not be enabled; REFUSED");
        return false;
    }
    return true;
}

void install(std::span<const gad::Section> secs) {
    // The functions U-c / U-d call, compared before any hook of this module is written over one of them.
    const bool calls = bytes_at(kArmFn, kArmFnBytes) && bytes_at(kLoadFn, kLBytes) && bytes_at(kUnloadFn, kUBytes) &&
                       bytes_at(kEnqueueFn, kEnqueueFnBytes) && bytes_at(kListFn, kListFnBytes) &&
                       bytes_at(kInterestFn, kInterestFnBytes);
    g_calls_ok.store(calls);
    if (!calls) log::error("User2Stages: the functions U-1 calls are not build 2976's; mode 1 stays probe");
    // U-e's cleanup (review 3): its functions' bytes, here, BEFORE point O writes its jump over Logout's thunk (the
    // 1e20571 run compared them after it and refused). The actor lookup [exe+0x6D69698] is read when a cleanup runs.
    const bool find_ok = bytes_at(kFindActorFn, kFindActorBytes), dtor_ok = bytes_at(kScopeDtorFn, kScopeDtorBytes),
               logout_ok = bytes_at(kLogoutFn, kOBytes);
    g_cleanup_ok.store(find_ok && dtor_ok && logout_ok);
    const std::string cleanup_bad = std::format("{}{}{}", find_ok ? "" : " SAM::FindActor 0x2A827B0;",
                                                dtor_ok ? "" : " the scope's dtor 0x1434880;",
                                                logout_ok ? "" : " ServerActor::Logout's thunk 0x2068340;");
    std::string state;
    for (size_t i = 0; i < kPtCount; ++i) {
        const PointDef& d = kPoints[i];
        if (p8_install_mid(g_base, secs, d.rva, d.patch, d.bytes, kFns[i], g_hooks[i], "User2Stages", d.name)) {
            g_pt_on[i].store(true, std::memory_order_release);
        }
        state += std::format("{}{} {}", i ? " | " : "", d.name, g_pt_on[i].load() ? "hooked" : "REFUSED");
    }
    // U-a / U-f: only with user2_stages (user2_timer_route alone needs the U-0 points' T only).
    if (mode_now() == u2s::Mode::Off) {
        log::info("User2Stages (user2_stages=0, user2_timer_route={}): {} | G / E not installed",
                  u2s::mode_name(route_mode_now()), state);
        return;
    }
    // U-a: ShopProbe owns the gate when it is on (one owner per address); else this module.
    std::string gate;
    if (shop_probe_active()) {
        gate = "G 0x288B87A: ShopProbe's hook (it calls user2_stages_gate first)";
    } else if (bytes_at(kGateCtx, kGateCtxBytes) &&
               p8_install_mid(g_base, secs, kGate, kGatePatch, kGateBytes, on_gate, g_gate_hook, "User2Stages",
                              "G, the owner gate 0x288B87A (U-a)")) {
        g_gate_own.store(true);
        gate = "G 0x288B87A: hooked here (shop_probe is off)";
    } else {
        gate = "G 0x288B87A: REFUSED";
    }
    // U-f: only needed by mode 1, installed in probe too (it counts what user 2's manager would prepare).
    // U-a2: this module's own (nobody else hooks 0x2911300).
    if (bytes_at(kGate2Ctx, kGate2CtxBytes) &&
        p8_install_mid(g_base, secs, kGate2, kGate2Patch, kGate2Bytes, on_gate2, g_gate2_hook, "User2Stages",
                       "G2, the collector's manager guard 0x2911435 (U-a2)")) {
        g_gate2_on.store(true);
    }
    gate += g_gate2_on.load() ? " | G2 0x2911435 hooked" : " | G2 0x2911435 REFUSED";
    // H, log only.
    if (bytes_at(kEvalCtx, kEvalCtxBytes) &&
        p8_install_mid(g_base, secs, kEval, kEvalPatch, kEvalBytes, on_eval, g_eval_hook, "User2Stages",
                       "H, after the cell evaluation's 0x2883AA0 (0x2881A19, log only)")) {
        g_eval_on.store(true);
    }
    gate += g_eval_on.load() ? " | H 0x2881A19 hooked (log only)" : " | H 0x2881A19 REFUSED";
    g_elem_on.store(install_elem());
    // U-e: the server's controller write, one hook owned by docked_gimmicks (installed from here when it is off).
    std::string ctl = "U-e off";
    if (ctl_mode_now() != u2s::Mode::Off || npc_probes()) {
        // Review 3: the cleanup's functions, compared at the top of install (before point O).
        if (!g_cleanup_ok.load() && ctl_mode_now() == u2s::Mode::On) {
            log::error("User2Stages: U-e's cleanup functions are not build 2976's:{} user2_npc_control=1 stays probe",
                       cleanup_bad);
            g_ctl_mode.store(static_cast<uint8_t>(u2s::Mode::Probe));
            if (family_mode() == npcact::Mode::On) {
                log::warn("NpcFamily: user2_npc_control quedó en probe: npc_family_control=1 queda en probe");
                g_family_mode.store(static_cast<uint8_t>(npcact::Mode::Probe));
            }
        }
        // S38 fase C: la retirada de viajeros usa las mismas funciones (comprobadas arriba, antes del punto O).
        if (!g_cleanup_ok.load() && road_mode() == npcact::Mode::On) {
            log::error("User2Road: las funciones de su retirada no son las de la build 2976:{} user2_road_control=1 "
                       "queda en probe", cleanup_bad);
            g_road_mode.store(static_cast<uint8_t>(npcact::Mode::Probe));
        }
        // Revisión §4.2: sin el punto O (Logout del personaje del invitado) su salida no retiraría nada.
        if (!g_pt_on[kPtO].load() && road_mode() == npcact::Mode::On) {
            log::error("User2Road: el punto O (ServerActor::Logout 0x2068340) no está: user2_road_control=1 queda en "
                       "probe (la salida del invitado no retiraría sus viajeros)");
            g_road_mode.store(static_cast<uint8_t>(npcact::Mode::Probe));
        }
        g_ctl_hook_on.store(docked_gimmicks_ctl_write_for_user2("User2Stages"));
        ctl = g_ctl_hook_on.load() ? std::format("U-e (user2_npc_control={}) at 0x2792A7B: in (docked_gimmicks' mid "
                                                 "hook asks user2_stages first)",
                                                 u2s::mode_name(ctl_mode_now()))
                                   : std::string("U-e at 0x2792A7B: REFUSED (user2_npc_control does nothing)");
    }
    // S33 review: the action / node probes and H1b (0x2A79060, owned by host_scene_diag) go in from here, with
    // user2_stages only; with s1_scene_diag=1 observer_stage's thread installs them with the rest of SceneDiag.
    if (npc_probes() && !host_scene_diag_configured()) host_scene_diag_install(g_base, secs);
    log::info("User2Stages (user2_stages={}, user2_timer_route={}): {} | {} | E 0x288A970 (U-f) {} | calls {} | {} | "
              "user2_drain_at_host_prepare={} (at P)",
              mode_now() == u2s::Mode::On ? "1" : "probe, read only", u2s::mode_name(route_mode_now()), state, gate,
              g_elem_on.load() ? "hooked" : "REFUSED", calls ? "verified" : "REFUSED", ctl,
              u2s::mode_name(dprep_mode_now()));
    if (mode_now() == u2s::Mode::On) {
        const std::string missing = mode1_missing();
        if (!missing.empty()) log::warn("User2Stages: mode 1 cannot arm until these are in:{}", missing);
    }
}

DWORD WINAPI probe_thread(LPVOID) {
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::vector<gad::Section> secs;
    if (g_base != off::kImageBase || !game::safe_read(g_base, &dos, sizeof dos) ||
        !game::safe_read(g_base + dos.e_lfanew, &nt, sizeof nt) || nt.FileHeader.TimeDateStamp != off::kPeTimestamp ||
        !p8_exe_sections(g_base, secs)) {
        log::error("User2Stages: unsupported game build or unreadable sections; nothing installed");
        g_mode.store(static_cast<uint8_t>(u2s::Mode::Off));
        g_route_mode.store(static_cast<uint8_t>(u2s::Mode::Off));
        return 0;
    }
    g_image_size = nt.OptionalHeader.SizeOfImage;
    // Deferred install: only once WorldSystem exists (the game's code and server in place). No time limit: after
    // 10 minutes one line says it is still waiting (a long title screen), and nothing is installed meanwhile.
    for (uint64_t i = 1;; ++i) {
        uintptr_t world = 0;
        if (game::safe_read(g_base + off::kWorldSystemSlot, &world, sizeof world) && world) break;
        if (i == 1200) log::info("User2Stages: no WorldSystem after 10 minutes: still waiting, nothing installed yet");
        Sleep(500);
    }
    try {
        sample(false);  // the players' keys before the hooks read them
        install(secs);
    } catch (...) {
        log::error("User2Stages: install failed (exception); hooks may be partial");
    }
    for (uint64_t tick = 1;; ++tick) {
        Sleep(1000);
        try {
            sample(tick % 5 == 0);
            npc_control_tick(p8_now_us());
            npc_probe_drain();
            road_tick();   // S38 fase C: época nueva, borrado
            road_drain();  // ... y sus líneas / resumen
            npc_action_probe_drain();  // its counters, once a minute (moved here from SceneDiag's drain)
            drain();
            if (tick % 60 == 0) summary();
        } catch (...) {
        }
    }
}

}  // namespace

void user2_stages_start(const std::string& mode, const std::string& timer_route, const std::string& elements,
                        const std::string& npc_control, const std::string& drain_prep) {
    const u2sh::ElementsKey ek = u2sh::parse_elements(elements);
    if (ek.bad) log::warn("User2Stages: user2_stages_elements={} is not schedule or schedule+funcnpc: schedule", elements);
    g_elements.store(static_cast<uint8_t>(ek.elements));
    const u2s::ModeKey k = u2s::parse_mode(mode);
    if (k.bad) log::warn("User2Stages: user2_stages={} is not 0, probe or 1: off", mode);
    const u2s::ModeKey r = u2s::parse_mode(timer_route);
    if (r.bad) log::warn("User2Stages: user2_timer_route={} is not 0, probe or 1: off", timer_route);
    u2s::ModeKey c = u2s::parse_mode(npc_control);
    if (c.bad) log::warn("User2Stages: user2_npc_control={} is not 0, probe or 1: off", npc_control);
    u2s::ModeKey d = u2s::parse_mode(drain_prep);
    if (d.bad) log::warn("User2Stages: user2_drain_at_host_prepare={} is not 0, probe or 1: off", drain_prep);
    if (k.mode == u2s::Mode::Off && (c.mode != u2s::Mode::Off || d.mode != u2s::Mode::Off)) {
        log::warn("User2Stages: user2_npc_control / user2_drain_at_host_prepare need user2_stages; off");
        c = d = u2s::ModeKey{};  // they act on U-1's armed manager only
    } else if (k.mode != u2s::Mode::On && (c.mode != u2s::Mode::Off || d.mode != u2s::Mode::Off)) {
        log::warn("User2Stages: user2_npc_control={} / user2_drain_at_host_prepare={} act only with user2_stages=1 "
                  "armed: with user2_stages={} they never trigger (counts only)",
                  u2s::mode_name(c.mode), u2s::mode_name(d.mode), u2s::mode_name(k.mode));
    }
    if (k.mode == u2s::Mode::Off && npc_probes()) {
        // S33 review: the probes observe U-1 / U-e; without user2_stages they are off and install nothing.
        log::warn("NpcActivation: user2_stages={} is off: npc_activation / npc_family_control / user2_road_control off", mode);
        g_activation_mode.store(0);
        g_family_probe.store(0);
        g_road_probe.store(0);
        g_road_req.store(0);
        child_hold_set_retirement(false);
        child_hold_set_npc_family_observe(false);
    }
    // S36 fase B: npc_family_control efectivo. 1 sólo con todos sus requisitos (la unidad entera necesita U-1/U-e
    // asignando, la contención por unidad con la evidencia H1b, la degradación sin el aborto global y la publicación
    // segura de la fase A para lo que se queda con el anfitrión); si falta alguno, probe (mide, no cambia nada).
    {
        auto fm = static_cast<npcact::Mode>(g_family_req.load());
        if (k.mode == u2s::Mode::Off) fm = npcact::Mode::Off;
        if (fm == npcact::Mode::On) {
            std::string missing;
            const auto need = [&missing](bool ok, const char* what) {
                if (ok) return;
                if (!missing.empty()) missing += ", ";
                missing += what;
            };
            need(k.mode == u2s::Mode::On, "user2_stages=1");
            need(c.mode == u2s::Mode::On, "user2_npc_control=1");
            need(activation_on(), "npc_activation=1");
            need(mode_is(g_evidence_mode, npcact::Mode::On), "npc_action_evidence=1");
            need(mode_is(g_degrade_mode, npcact::Mode::On), "npc_degradation=1");
            need(child_hold_publication_gate() == 2, "publication_gate=1");
            if (!missing.empty()) {
                log::warn("NpcFamily: npc_family_control=1 necesita {}: queda en probe (mide; no cambia ningún "
                          "controlador)", missing);
                fm = npcact::Mode::Probe;
            }
        }
        g_family_mode.store(static_cast<uint8_t>(fm));
        if (fm != npcact::Mode::Off) {
            log::info("NpcFamily S36: npc_family_control={} (pedido {}) npc_family_types={} | unidad = raíz + subárbol "
                      "de descriptores (+0x2F0/+0x2F8) en el mid 0x2792A7B; reserva entera antes de EAX (marco de Troc "
                      "{} registros, {} marcos), pila de marcos por Troc anidado, vínculo por nacimiento del padre, "
                      "hojas a raíz en toda salida; tipo 7 {}; nunca actor+0xA0 ni misiones",
                      npcact::mode_name(fm), npcact::mode_name(static_cast<npcact::Mode>(g_family_req.load())),
                      fam_types_text(g_family_types.load()), npcfam::kFrameRecs, g_fam_pool.size(),
                      npcfam::type_in({g_family_types.load(), false}, 7) ? "asignable (cohorte)"
                                                                         : "con el anfitrión (host-kept, puerta S35)");
        }
    }
    // S37 phase D: user2_fast_rearm (set before by user2_stages_set_fast_rearm). It acts only with user2_stages=1;
    // with user2_stages=probe it measures (probe); without user2_stages it is off.
    {
        auto f = static_cast<u2s::Mode>(g_fast_req.load());
        if (f != u2s::Mode::Off && k.mode == u2s::Mode::Off) {
            log::warn("User2Stages: user2_fast_rearm={} needs user2_stages; off", u2s::mode_name(f));
            f = u2s::Mode::Off;
        } else if (f == u2s::Mode::On && k.mode != u2s::Mode::On) {
            log::warn("User2Stages: user2_fast_rearm=1 acts only with user2_stages=1 (user2_stages={}): probe",
                      u2s::mode_name(k.mode));
            f = u2s::Mode::Probe;
        }
        g_fast_mode.store(static_cast<uint8_t>(f));
        if (f != u2s::Mode::Off) {
            log::info("User2Stages S37 phase D: user2_fast_rearm={} | a <= {} -> >= {} cell jump arms without the {} s "
                      "hysteresis only for the host's own departure (its load step L in the last {} s and after the "
                      "start of the near streak, consecutive samples, same field and character, user 2 still, nothing "
                      "pending, not within {} s of another), confirmed over {} s (far, user 2 still, no load of user 2), "
                      "and queues a one-shot 2043 (+0xB 0, delay {} = {} ms) at that arm; the chain repair stays",
                      u2s::mode_name(f), u2s::Thresholds{}.off_cells, u2s::Thresholds{}.on_cells,
                      u2s::Thresholds{}.on_dwell_us / 1'000'000, frm::kHostLoadWindowUs / 1'000'000,
                      frm::kMinGapUs / 1'000'000, frm::kConfirmUs / 1'000'000, frm::kOneShotDelay,
                      frm::delay_ms(frm::kOneShotDelay));
        }
    }
    // S38 fase C: user2_road_control efectivo. 1 sólo con la retirada estricta posible (npc_activation=1: la barrera
    // de publicación que cancela altas/Acks pendientes antes de Logout) y la publicación segura hacia el anfitrión
    // (observer_hold activo y publication_gate=1: ningún viajero del usuario 2 con restore 0 llega al cliente del
    // anfitrión). No exige user2_stages=1 ni U-e (pieza propia). Los bytes de su limpieza se comprueban en install.
    {
        auto rm = static_cast<npcact::Mode>(g_road_req.load());
        if (k.mode == u2s::Mode::Off) rm = npcact::Mode::Off;
        if (rm == npcact::Mode::On) {
            std::string missing;
            const auto need = [&missing](bool ok, const char* what) {
                if (ok) return;
                if (!missing.empty()) missing += ", ";
                missing += what;
            };
            need(activation_on(), "npc_activation=1");
            need(observer_hold_active(), "observer_hold=1 (instalado)");
            need(child_hold_publication_gate() == 2, "publication_gate=1");
            if (!missing.empty()) {
                log::warn("User2Road: user2_road_control=1 necesita {}: queda en probe (mide; no cambia ningún "
                          "controlador)", missing);
                rm = npcact::Mode::Probe;
            }
        }
        g_road_mode.store(static_cast<uint8_t>(rm));
        if (rm != npcact::Mode::Off) {
            log::info("User2Road S38: user2_road_control={} (pedido {}) | viajeros del AutoSpawnRoadReq 2550 del "
                      "invitado: procedencia = descriptor ICreateServerActorDesc exe+0x{:X} con +0x3CD=1, +0x3C8 = su "
                      "personaje, +0x3B8 = su puntero, nacido DENTRO de su Troc 2550; unidad = raíz + subárbol _Rider "
                      "exe+0x{:X} del mismo solicitante (máx. {} registros), tipos 3/5/6, nunca hijos de usuario (+0xA0, "
                      "desc+0x40); tabla propia ({} unidades), retirada sólo en su carga y su Logout (nada al acercarse el "
                      "anfitrión), parada local sin aborto global",
                      npcact::mode_name(rm), npcact::mode_name(static_cast<npcact::Mode>(g_road_req.load())),
                      road::kVtDescBase, road::kVtWrapRider, road::kMaxUnit, g_road.capacity());
        }
    }
    if (k.mode == u2s::Mode::Off && r.mode == u2s::Mode::Off) return;
    g_ctl_mode.store(static_cast<uint8_t>(c.mode));
    g_dprep_mode.store(static_cast<uint8_t>(d.mode));
    g_mode.store(static_cast<uint8_t>(k.mode));
    g_route_mode.store(static_cast<uint8_t>(r.mode));
    set_budgets();  // before the thread (and so before any hook) exists
    if (g_started.exchange(true)) return;
    CloseHandle(CreateThread(nullptr, 0, probe_thread, nullptr, 0, nullptr));
}

void user2_stages_set_trade_list(const std::string& mode) {
    const u2s::ModeKey k = u2s::parse_mode(mode);
    if (k.bad) log::warn("User2Stages: user2_shop_trade_list={} is not 0, probe or 1: off", mode);
    g_trade_list.store(static_cast<uint8_t>(k.mode));
    if (k.mode == u2s::Mode::Off) return;
    const bool funcnpc_all = g_elements.load() == static_cast<uint8_t>(u2sh::Elements::ScheduleFuncnpc);
    if (k.mode == u2s::Mode::On) {
        log::info("User2Stages: user2_shop_trade_list=1 (S46 phase 3, T1 ACTIVE): with U-1 armed and >= 10 cells, U-f "
                  "lets through ONLY the cd_seq_funcnpc_* whose base name hashes (lookup3, seed 0xC5EDE) to the trade "
                  "list: smithy 0xA7132561, butcher 0x978345CA; every other funcnpc is skipped as before, schedules "
                  "unchanged{}. The NPCs born inside those scenes' prepare are left to the game (U-e / families / "
                  "roads not consulted). Abort: user 2's manager prepares a funcnpc whose playData hash is not on the "
                  "list -> T1 latches off (no shop scene) for the run",
                  funcnpc_all ? "; user2_stages_elements=schedule+funcnpc is NARROWED to the list (T1 decides)" : "");
    } else {
        log::info("User2Stages: user2_shop_trade_list=probe (S46 T1, verdict only): the shop elements U-f skips say "
                  "whether T1 would let them through (by the exact hash of the name); nothing more is prepared");
    }
}
bool user2_stages_trade_list_active() noexcept { return t1_list_on(); }
bool user2_stages_in_trade_prepare() noexcept { return t_t1_origin != 0; }
void user2_stages_clear_trade_mark() noexcept { t_t1_origin = 0; }
void user2_stages_trade_list_breach(uint64_t seq_id, uint32_t hash) noexcept {
    if (!t1_list_on() || g_t1_latched.exchange(true)) return;
    g_t1_latched_id.store(seq_id);
    g_t1_latched_hash.store(hash);
}

void user2_stages_set_fast_rearm(const std::string& mode) {
    const u2s::ModeKey f = u2s::parse_mode(mode);
    if (f.bad) log::warn("User2Stages: user2_fast_rearm={} is not 0, probe or 1: off", mode);
    g_fast_req.store(static_cast<uint8_t>(f.mode));
}

void user2_stages_set_npc_probes(const std::string& activation, const std::string& family,
                                const std::string& road, bool stages_on) {
    auto a = npcact::parse_mode(activation, true);
    auto f = npcact::parse_mode(family, true);  // S36: npc_family_control admite 1 (fase B)
    auto r = npcact::parse_mode(road, true);  // S38 fase C: user2_road_control admite 1
    if (a.bad || f.bad || r.bad)
        log::warn("NpcActivation: valor no autorizado; npc_activation, npc_family_control y user2_road_control admiten "
                  "0/probe/1; inválidos apagados");
    // Revisión: sin user2_stages no hay U-1/U-e que observar; tampoco se instalan sus hooks.
    if (!stages_on && (a.mode != npcact::Mode::Off || f.mode != npcact::Mode::Off || r.mode != npcact::Mode::Off)) {
        log::warn("NpcActivation: npc_activation / npc_family_control / user2_road_control necesitan user2_stages=probe o 1 "
                  "(y user2_npc_control para elegir candidatos): apagados, sin hooks");
        a.mode = f.mode = r.mode = npcact::Mode::Off;
    }
    g_activation_mode.store(static_cast<uint8_t>(a.mode));
    g_family_probe.store(static_cast<uint8_t>(f.mode));
    g_family_req.store(static_cast<uint8_t>(f.mode));
    g_road_probe.store(static_cast<uint8_t>(r.mode));
    g_road_req.store(static_cast<uint8_t>(r.mode));  // S38: el efectivo se fija en user2_stages_start / install
    child_hold_set_retirement(a.mode == npcact::Mode::On);
    // S38: user2_road_control también (cuenta los hijos de jerarquía de sus unidades en la publicación).
    child_hold_set_npc_family_observe(a.mode != npcact::Mode::Off || f.mode != npcact::Mode::Off ||
                                      r.mode != npcact::Mode::Off);
    log::info("NpcActivation: npc_activation={}, npc_family_control={}, user2_road_control={}; plazo={} ms desde2005; "
              "máximo {} fallos por nodo/generación, {} rechazos por unidad; retirada exige sondas de acción y veto de "
              "nodo verificados (si no, sigue el aborto antiguo de U-e)",
              npcact::mode_name(a.mode), npcact::mode_name(f.mode), npcact::mode_name(r.mode),
              npcact::kNoProgressUs / 1000, npcact::kMaxNodeFailures, npcact::kMaxRefusals);
}
void user2_stages_set_npc_s35(const NpcS35Config& c) {
    const auto e = npcact::parse_mode(c.evidence, true);
    const auto g = npcact::parse_mode(c.guard, true);
    const auto d = npcact::parse_mode(c.degradation, true);
    if (e.bad || g.bad || d.bad) {
        log::warn("NpcActivation S35: npc_action_evidence='{}' npc_containment_guard='{}' npc_degradation='{}': sólo "
                  "0 | probe | 1; los inválidos quedan en 0", c.evidence, c.guard, c.degradation);
    }
    // Cotas de cordura de los plazos (experimentales): 1 s .. 120 s; 0 en gracia / silencio = sin esa regla.
    const auto clamp_ms = [](uint32_t ms, uint32_t lo, uint32_t hi) { return std::clamp(ms, lo, hi); };
    const uint32_t plazo = clamp_ms(c.no_progress_ms, 1000, 120000);
    const uint32_t gracia = c.req_grace_ms ? clamp_ms(c.req_grace_ms, 500, 120000) : 0;
    const uint32_t silencio = c.guest_silence_ms ? clamp_ms(c.guest_silence_ms, 500, 60000) : 0;
    g_evidence_mode.store(static_cast<uint8_t>(e.mode));
    g_guard_mode.store(static_cast<uint8_t>(g.mode));
    g_degrade_mode.store(static_cast<uint8_t>(d.mode));
    g_no_progress_us.store(static_cast<uint64_t>(plazo) * 1000);
    g_req_grace_us.store(static_cast<uint64_t>(gracia) * 1000);
    g_silence_us.store(static_cast<uint64_t>(silencio) * 1000);
    // Revisión S35 (hallazgo 2): tope de la posposición por silencio/carga (5 s .. 10 min; 0 = sin tope, NO recomendado).
    const uint32_t tope = c.postpone_max_ms ? clamp_ms(c.postpone_max_ms, 5000, 600000) : 0;
    g_max_postpone_us.store(static_cast<uint64_t>(tope) * 1000);
    log::info("NpcActivation S35: npc_action_evidence={} (0 = emisor S34, probe = H1b sólo registrado, 1 = decide "
              "H1b) npc_containment_guard={} npc_degradation={} | npc_no_progress_ms={} npc_req_grace_ms={} "
              "npc_guest_silence_ms={} npc_postpone_max_ms={} (plazos experimentales)",
              npcact::mode_name(e.mode), npcact::mode_name(g.mode), npcact::mode_name(d.mode), plazo, gracia, silencio,
              tope);
    // Revisión S35 (hallazgo 12): el guard sin la evidencia H1b sigue decidiendo el progreso con el emisor S34 mal leído
    // (nunca hay progreso): pospone, pero cada unidad sin silencio acaba retirada aunque haya actuado.
    if (g.mode == npcact::Mode::On && e.mode != npcact::Mode::On) {
        log::warn("NpcActivation S35: npc_containment_guard=1 con npc_action_evidence={}: el progreso sigue siendo el de "
                  "S34 (emisor mal leído, nunca cuenta); usar npc_action_evidence=1 con el guard",
                  npcact::mode_name(e.mode));
    }
    if (tope == 0 && g.mode == npcact::Mode::On) {
        log::warn("NpcActivation S35: npc_postpone_max_ms=0: la posposición por silencio/carga no tiene tope");
    }
}
void user2_stages_set_family_vehicles(const std::string& value) {
    const auto requested = value == "1" ? famveh::Mode::On : value == "probe" ? famveh::Mode::Probe : famveh::Mode::Off;
    const auto effective = famveh::effective_mode(requested);
    g_family_vehicles.store(static_cast<uint8_t>(effective));
    if (requested == famveh::Mode::On) {
        log::warn("NpcFamilyVehicles: solicitado=1 efectivo=probe: NO se asignan vehículos; falta demostrar "
                  "el ciclo de vida de la stage en Logout/cargas nativos, fuera de nuestro drain_now");
    } else if (value != "0" && value != "probe" && !value.empty()) {
        log::warn("NpcFamilyVehicles: valor '{}' inválido; solicitado=0 efectivo=0", value);
    }
    log::info("NpcFamilyVehicles: solicitado={} efectivo={} recurso=0x{:08X}; sólo clasificación, "
              "no reserva prospectiva ni autoridad/HP/facción/misiones",
              requested == famveh::Mode::On ? "1" : requested == famveh::Mode::Probe ? "probe" : "0",
              effective == famveh::Mode::Off ? "0" : "probe", famveh::kMeasuredResource);
}
void user2_stages_set_npc_family(const std::string& types) {
    const npcfam::Types t = npcfam::parse_types(types);
    if (t.bad) {
        log::warn("NpcFamily: npc_family_types='{}' no es una lista de 3, 6 y 7: queda la cohorte por defecto 3,6", types);
    }
    g_family_types.store(t.mask);
}
bool user2_stages_npc_probes_on() noexcept { return npc_probes(); }
void user2_stages_set_road_carts(const std::string& value) {
    // S40 carros (docs/research/s25/s40-carros.md): 0 | probe | 1.
    road::Carts c = road::Carts::Off;
    if (value == "1") {
        c = road::Carts::On;
    } else if (value == "probe") {
        c = road::Carts::Probe;
    } else if (!value.empty() && value != "0") {
        log::warn("User2Road: user2_road_carts='{}' no es 0, probe ni 1: queda en 0", value);
    }
    g_road_carts.store(static_cast<uint8_t>(c));
    if (c != road::Carts::Off) {
        log::info("User2Road S40: user2_road_carts={} | con user2_road_control=1, raíces tipo 5 (caballo con jinete, "
                  "carro): unidad entera = raíz + jinete/conductor (_Rider) + vehículos enlazados (_LinkedVehicle "
                  "exe+0x{:X} de 0x2797510, sin marca, +0x490 = su padre){}; gimmicks tipo 7 y su carga con el anfitrión "
                  "(registrados, de hojas a raíz); nunca un vehículo que monta un jugador; {} registros por unidad",
                  road::carts_name(c), road::kVtWrapLinkedVehicle,
                  c == road::Carts::Probe ? " medida como «se asignaría» (ningún controlador escrito)" : " asignada",
                  road::kMaxUnit);
    }
}
void user2_stages_road_request(uintptr_t packet, uint16_t size) noexcept {
    if (!g_started.load(std::memory_order_relaxed) || road_mode() == npcact::Mode::Off) return;
    RoadTls& t = t_road;
    const uint32_t d = t_troc.depth;
    t.req.troc = 0;
    t.req.n = 0;
    if (!d || d >= npcfam::kDepths || !t.id[d]) return;
    uint16_t len = 0;
    // 0x29C4930: el tamaño del Troc menos la cabecera de 5 B es la longitud de la carga (+3).
    if (!packet || size < 5 || !rd(packet + 3, len) || len != size - 5 ||
        !road::parse_request(FamRead{}, packet + 5, len, t.req)) {
        g_road_requests_bad.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    t.req.troc = t.id[d];
    g_road_requests_read.fetch_add(1, std::memory_order_relaxed);
}
void user2_stages_npc_action_probe_ready(bool ready) noexcept { g_activation_ready.store(ready); }
void user2_stages_npc_node_probe_ready(bool ready) noexcept { g_node_ready.store(ready); }
// Cada StartAction del servidor pasa por aquí (0x2A78CA0): cerrojo compartido y fin en el primer hueco.
uint64_t user2_stages_npc_birth_token(uint32_t key) noexcept {
    if (!npc_probes() || !key || !g_activation_tracked.load(std::memory_order_relaxed)) return 0;
    ActivationRead read;
    const auto* a = g_activation.find(key);
    return a && !a->removed ? a->birth : 0;
}
void user2_stages_npc_action_result(uint32_t key, uint32_t s34_sender, uint32_t ctl_entry, uint32_t ctl_exit, bool h1b,
                                    int32_t internal_result, uint8_t branch_flag, int restore_before,
                                    int restore_after, uint64_t birth_token) noexcept {
    if (!npc_probes() || !birth_token) return;
    const uint8_t mode = g_evidence_mode.load(std::memory_order_relaxed);
    npcact::ActionCall call;
    call.birth = birth_token; call.h1b = h1b; call.branch_flag = branch_flag; call.internal = internal_result;
    call.ctl_entry = ctl_entry; call.ctl_exit = ctl_exit;
    const uint64_t now = p8_now_us();
    ActivationLock guard;
    auto* a = g_activation.find(key);
    if (!a || a->birth != birth_token) return;
    const bool first_progress = !a->applied_us && !a->activated_us;
    npcact::Evidence ev = npcact::Evidence::Count;
    bool progress = false;
    if (mode == static_cast<uint8_t>(npcact::Mode::On)) {
        // S35 criterio H1b: decide la evidencia nativa autorizada y aplicada (o la rama 281: activación).
        ev = g_activation.record(key, call, now);
        progress = ev == npcact::Evidence::Applied || ev == npcact::Evidence::Branch281;
    } else {
        if (a->removed) return;
        // S34 (conservado): la rama payload+0x281 también es progreso; el emisor es la lectura S34 de param4.
        const bool applied = npcact::action_progress(h1b, branch_flag, internal_result);
        progress = g_activation.progress(key, birth_token, s34_sender, applied, now);
        if (mode == static_cast<uint8_t>(npcact::Mode::Probe)) {
            ev = npcact::classify(*a, call);  // sólo se registra
            const bool h1b_progress = ev == npcact::Evidence::Applied || ev == npcact::Evidence::Branch281;
            // Revisión S35 (hallazgo 12): unidades, no llamadas.
            if (h1b_progress != progress && !a->disagree_noted) {
                a->disagree_noted = true;
                g_evidence_disagree.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    if (ev != npcact::Evidence::Count) g_evidence_n[static_cast<size_t>(ev)].fetch_add(1, std::memory_order_relaxed);
    // Revisión S35 (hallazgo 1): un H1b aplicado con la lectura de salida ilegible o cambiada cuenta como aplicado; la
    // discrepancia se cuenta y va en la línea (salida_discrepante=1).
    const bool mismatch = ev == npcact::Evidence::Applied && npcact::exit_mismatch(call);
    if (mismatch) g_evidence_exit_mismatch.fetch_add(1, std::memory_order_relaxed);
    // Una línea por la primera acción vista y otra por el primer progreso, no una por cada Req rechazada.
    if (a->action_lines < 2 && (!a->action_lines || (progress && first_progress))) {
        ++a->action_lines;
        const uint64_t evbits = ev == npcact::Evidence::Count ? 0xF : static_cast<uint64_t>(ev);
        const uint64_t extra = static_cast<uint32_t>(internal_result) | static_cast<uint64_t>(branch_flag) << 32 |
            static_cast<uint64_t>(h1b) << 40 | static_cast<uint64_t>(progress) << 41 | evbits << 44 |
            static_cast<uint64_t>(std::min<uint8_t>(mode, 2)) << 48 | static_cast<uint64_t>(mismatch) << 52;
        const uint64_t restores = static_cast<uint32_t>(restore_before) | static_cast<uint64_t>(static_cast<uint32_t>(restore_after)) << 32;
        // Su propio registro: v[4]/v[5] llevan actor+0x90 antes/después (npc_event pone tipo y stage ahí).
        p::Event e;
        e.kind = kNpcAction; e.qpc_us = now; e.tid = GetCurrentThreadId(); e.flags = a->assigned;
        e.v[0] = a->key; e.v[1] = a->birth; e.v[2] = a->controller; e.v[3] = s34_sender;
        e.v[4] = ctl_entry; e.v[5] = ctl_exit; e.v[8] = a->node.epoch;
        e.v[9] = a->queued_us && now >= a->queued_us ? now - a->queued_us : 0;
        e.v[10] = extra; e.v[11] = restores;
        g_activation_events.push(e);
    }
}
// Hijo de un registrado que no tenía vínculo al nacer (lo nativo lo enlaza después): entra al observarse, con un
// nacimiento local nuevo, la época/generación de su padre y su controlador observado. Bajo g_activation_lock.
static npcact::Actor* npc_add_member(uint32_t key, uint32_t controller, uint8_t type,
                                     const npcact::Actor* parent) noexcept {
    npcact::Actor* a = g_activation.claim(key, g_birth_serial.fetch_add(1, std::memory_order_relaxed) + 1);
    if (!a) return nullptr;
    a->controller = controller; a->type = type; a->born_us = p8_now_us();
    a->node.epoch = parent ? parent->node.epoch : server_epoch();
    a->node.generation = parent ? parent->node.generation : g_arm_done_us.load(std::memory_order_relaxed);
    return a;
}
void user2_stages_npc_family_observed(uint32_t key, uint32_t parent, uint32_t controller, uint8_t type) noexcept {
    if (!npc_probes() || !key) return;
    road_family_observed(key, parent, controller, type);  // S38 fase C: sólo cuenta
    if (!npc_tracked(key) && !(parent && npc_tracked(parent))) return;  // ni él ni su padre interesan
    ActivationLock guard;
    const npcact::Actor* p = parent ? g_activation.find(parent) : nullptr;
    npcact::Actor* a = g_activation.find(key);
    const bool fresh = !a && p && !p->removed;
    const uint32_t epoch = server_epoch();
    if (fresh && family_mode() != npcact::Mode::Off && !g_fam_seen.contains(key, epoch) &&
        (p->fam == npcact::kFamRoot || npcact::family_member(*p))) {
        // S36: hijo de una unidad que no nació en su marco (tardío, fuera del preexamen; su generación no se conoce y
        // no se inventa, AUDITORIA.md §6.1). Nunca se asigna fuera del marco: con otro controlador que su unidad, la
        // unidad está PARTIDA y (modo 1) se retira entera, salvo un tipo 7 que la cohorte deja al anfitrión. probe:
        // sólo se cuenta. Revisión: antes de registrarlo (su registro puede fallar; la marca no).
        // Segunda revisión (R4): el vínculo de la publicación es de JERARQUÍA (+0xAC / jerarquía nativa); sólo un hijo
        // cuyo descriptor tenía padre (+0x2F0) en su nacimiento parte la unidad. El resto se cuenta, nunca parte.
        const bool on = family_mode() == npcact::Mode::On;
        const bool root = p->fam == npcact::kFamRoot;
        const uint32_t unit_key = root ? p->key : p->unit_key;
        const uint64_t unit_birth = root ? p->birth : p->unit_birth;
        const npcact::Actor* r = g_activation.find(unit_key);
        const bool keep7 = type == 7 && !npcfam::type_in({g_family_types.load(), false}, 7);
        const npcfam::Link lineage =
            g_fam_lineage.contains(key, epoch) ? npcfam::Link::Present : npcfam::Link::Unknown;
        const npcfam::LateWhat w = npcfam::late_child(r && r->birth == unit_birth, r && controller != r->controller,
                                                      keep7, lineage);
        g_fam_pub_late.fetch_add(1, std::memory_order_relaxed);
        if (w == npcfam::LateWhat::Split && (on ? fam_mark_split_locked(unit_key, unit_birth) : true)) {
            g_fam_pub_split.fetch_add(1, std::memory_order_relaxed);
        } else if (w == npcfam::LateWhat::NotLineage) {
            g_fam_late_hier.fetch_add(1, std::memory_order_relaxed);
        }
        p::Event e;
        e.kind = kNpcFamLate; e.qpc_us = p8_now_us(); e.tid = GetCurrentThreadId(); e.flags = !on;
        e.v[0] = key; e.v[2] = controller; e.v[3] = parent; e.v[4] = type; e.v[5] = unit_key;
        e.v[6] = unit_birth; e.v[7] = static_cast<uint64_t>(lineage); e.v[10] = static_cast<uint64_t>(w);
        g_activation_events.push(e);
    }
    if (fresh) a = npc_add_member(key, controller, type, p);
    if (!a || a->removed) return;
    bool changed = false;
    if (a->parent_birth) {
        // S36: su vínculo se demostró en su mid (con el nacimiento de su padre) y se conserva. El nativo debe nombrar
        // al mismo padre; si no, el modelo no vale para esta unidad: se cuenta y, en modo 1, se retira entera. Un 0
        // nativo es «aún sin vínculo», no otro padre.
        if (parent && parent != a->parent) {
            g_fam_link_mismatch.fetch_add(1, std::memory_order_relaxed);
            if (family_mode() == npcact::Mode::On && fam_mark_split_locked(a->unit_key, a->unit_birth)) {
                g_fam_pub_split.fetch_add(1, std::memory_order_relaxed);
            }
            p::Event e;
            e.kind = kNpcFamLate; e.qpc_us = p8_now_us(); e.tid = GetCurrentThreadId();
            e.flags = family_mode() != npcact::Mode::On;
            e.v[0] = key; e.v[2] = controller; e.v[3] = parent; e.v[4] = type; e.v[5] = a->unit_key;
            e.v[6] = a->unit_birth; e.v[10] = 0;
            g_activation_events.push(e);
        }
    } else {
        changed = a->parent != parent;
        a->parent = parent;
        if (changed) npcact::bump(*a);  // S35: un vínculo nuevo invalida un plan elegido antes
    }
    if (p && npcact::parent_requires_retirement(*p) && activation_on()) {
        a->orphaned = true;
        a->node = p->node;
    }
    // Una línea por vínculo nuevo o cambiado, no una por cada NotifyAppear.
    if (g_family_probe.load() && changed) npc_event(kNpcFamily, *a, controller, type);
}
void user2_stages_npc_late_family(uint32_t key, uint32_t parent, uint32_t controller) noexcept {
    if (!activation_on() || !key || !parent) return;
    ActivationLock guard;
    const npcact::Actor* p = g_activation.find(parent);
    npcact::Actor* a = g_activation.find(key);
    if (!a) a = npc_add_member(key, controller, 0, p);  // su padre puede no ser ya una lápida de esta tabla
    if (!a || a->removed) return;
    a->orphaned = true;
    a->parent = parent;
    npcact::bump(*a);
    if (p) a->node = p->node;
    npc_event(kNpcFamily, *a, 1, parent);
}
// 0x281CEE0 (npc_action_probe.cpp). Revisión: sólo stages cuyo dueño (+0x50) es el personaje de user 2, la misma
// identidad que usan U-1 y on_close (who_key); el veto sólo donde un false cancela exactamente ese nodo
// (npcact::veto_site) y con una línea por nodo/generación, no una por cada reintento de la stage.
// Prueba en un PC (14-10, 15:42): en el grupo nativo de 0x2819E00 modo 3 (líder 0x281A4B5, miembro 0x281A589) se veta
// sólo si se lee el grupo entero del marco del llamador (npcact::read_group, lecturas protegidas, papel comprobado) y
// TODOS sus nodos están agotados en esta generación: cancelar el grupo cancela entonces sólo unidades ya retiradas dos
// veces. Si no, se omite como antes y, si el nodo renace, se abandona sólo esa unidad.
bool user2_stages_npc_node_blocked(uintptr_t stage, uint32_t data, uint32_t node, uint32_t return_rva,
                                   uintptr_t node_ptr, uintptr_t caller_rsp) noexcept {
    if (!npc_probes()) return false;
    if (who_key(owner_key(stage, u2s::kSt_Owner)) != Who::User2) {
        g_node_other_owner.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const npcact::Node id{server_epoch(), data, node, rdv<uint64_t>(stage + u2s::kSt_Key),
                          g_arm_done_us.load(std::memory_order_relaxed)};
    if (!id.valid()) return false;
    const bool veto_on = containment_ready();
    {
        ActivationLock guard;
        if (g_node_seen.insert(id)) {
            npcact::Actor a; a.node = id;
            npc_event(kNpcNodeSeen, a, return_rva);
        }
        if (!veto_on || !g_activation.blocked(id)) return false;
    }
    // Nodo agotado: en un sitio de grupo, el grupo se lee fuera del cerrojo (sólo lecturas protegidas).
    const npcact::VetoSite site = npcact::veto_site(return_rva);
    npcact::Group group;  // 520 B
    if (site == npcact::VetoSite::Group) {
        const auto read = [](uintptr_t at, void* out, size_t size) { return game::fast_read(at, out, size); };
        if (!npcact::read_group(read, caller_rsp, return_rva, node_ptr, group)) {
            g_node_group_unread.fetch_add(1, std::memory_order_relaxed);
        }
    }
    ActivationLock guard;
    npcact::NodeBudget* b = g_activation.budget(id, false);
    if (!b || b->failures < npcact::kMaxNodeFailures) return false;
    npcact::Actor a; a.node = id;
    const size_t exhausted = group.size ? g_activation.blocked_in(id, group) : 0;
    const bool whole_group = site == npcact::VetoSite::Group && group.size && exhausted == group.size;
    if (site != npcact::VetoSite::Exact && !whole_group) {
        ++b->skips;
        g_node_veto_skips.fetch_add(1, std::memory_order_relaxed);
        if (!b->skip_logged) {
            b->skip_logged = true;
            npc_event(kNpcVetoSkipped, a, return_rva,
                      static_cast<uint64_t>(site) | std::min<uint64_t>(group.size, 0xFF) << 8 |
                          std::min<uint64_t>(exhausted, 0xFF) << 16);
        }
        return false;
    }
    ++b->vetoes;
    g_node_vetoes.fetch_add(1, std::memory_order_relaxed);
    if (whole_group) g_node_group_vetoes.fetch_add(1, std::memory_order_relaxed);
    if (!b->veto_logged) {
        b->veto_logged = true;
        npc_event(kNpcNodeBlocked, a, return_rva, group.size);  // adicional: nodos del grupo nativo (0 = sitio exacto)
    }
    return true;
}

void user2_stages_gate(uintptr_t mgr, uintptr_t chr, uintptr_t field, uintptr_t actor, uintptr_t& rax) noexcept {
    if (!g_started.load(std::memory_order_relaxed)) return;
    gate_impl(mgr, chr, field, actor, rax);
}

void user2_stages_npc_control(uintptr_t& rax, uintptr_t desc, uintptr_t field, uintptr_t actor) noexcept {
    if (!g_started.load(std::memory_order_relaxed)) return;
    FamBirth fb;  // S36: lo que el camino de familias deja para el registro del nacimiento (vacío con la clave a 0)
    const uintptr_t before = rax;
    // S46 phase 3: a birth inside the prepare of user 2's own TRADE scene (U-f's PassFuncnpc call on this thread: its
    // shopkeeper, by SpawnCharacter). Its controller stays what the game wrote (the field owner, the host, as in p2b);
    // U-e, the families and the roads are not consulted (excluded by its scene of origin, never by name). The birth is
    // still observed (npc_observe_birth: tombstones, activation records). Probe: counted only.
    if (const uint32_t origin = t_t1_origin) {
        g_t1_origin_births.fetch_add(1, std::memory_order_relaxed);
        g_t1_origin_last_key.store(rdv<uint32_t>(actor + u2s::kActor_Key), std::memory_order_relaxed);
        g_t1_origin_last_stage.store(origin, std::memory_order_relaxed);
        if (static_cast<u2s::Mode>(g_trade_list.load(std::memory_order_relaxed)) == u2s::Mode::On) {
            g_t1_origin_excluded.fetch_add(1, std::memory_order_relaxed);
            npc_observe_birth(rax, desc, actor, fb);
            return;
        }
        // probe: would U-e / the families / the roads have changed it? (counted after them)
        npc_control_impl(rax, desc, field, actor, fb);
        if (rax == before) road_control_impl(rax, desc, field, actor);
        if (rax != before) g_t1_origin_changed.fetch_add(1, std::memory_order_relaxed);
        npc_observe_birth(rax, desc, actor, fb);
        return;
    }
    npc_control_impl(rax, desc, field, actor, fb);
    // S38 fase C: los viajeros del invitado, sólo si U-e / las familias no cambiaron eax (excluyentes por vtable: U-e
    // sólo asigna CreateServerActorDesc_NPCSchedule; la carretera sólo ICreateServerActorDesc).
    if (rax == before) road_control_impl(rax, desc, field, actor);
    npc_observe_birth(rax, desc, actor, fb);
}

uintptr_t user2_stages_route(uint16_t opcode, uintptr_t subject, uintptr_t field) noexcept {
    const u2s::Mode rm = route_mode_now();
    if (rm == u2s::Mode::Off || !subject || subject != g_u2_char.load(std::memory_order_relaxed)) return 0;
    // Review A1: always, armed or not, aborted or not (the route is its own key).
    const u2s::Route r = u2s::route_of(rm, opcode, host_owns(field));
    if (r == u2s::Route::None) return 0;
    const size_t t = opcode == u2s::kOp2043 ? 0 : opcode == u2s::kOp2045 ? 1 : 2;
    const uint64_t n = g_route[static_cast<size_t>(r)][t].fetch_add(1, std::memory_order_relaxed);
    const uintptr_t mgr = seq_manager(subject);
    if (n < 3) {  // the first ones of each kind, as lines
        p::Event e;
        e.v[0] = opcode;
        e.v[1] = static_cast<uint64_t>(r);
        e.v[2] = mgr;
        e.v[8] = static_cast<uint64_t>(Who::User2);
        emit(kERoute, e, true);
    }
    // The 2045 / 2046 handlers take +0x148 / +0x10 of what vf48 returns: the same object for the three timers.
    if (r == u2s::Route::Own && mgr) mark_own_mgr_troc();  // U-e (review 6)
    return r == u2s::Route::Own ? mgr : 0;
}

// Inside user 2's ARMED 2043 tick on its own manager (mode 1, route 1, the host's field, state 2)?
static bool armed_tick(uint16_t opcode, uintptr_t subject, uintptr_t field) noexcept {
    if (opcode != u2s::kOp2043 || mode_now() != u2s::Mode::On || route_mode_now() != u2s::Mode::On) return false;
    if (!g_armed.load(std::memory_order_acquire) || g_aborted.load(std::memory_order_relaxed)) return false;
    if (!subject || subject != g_u2_char.load(std::memory_order_relaxed) || !host_owns(field)) return false;
    const uintptr_t mgr = seq_manager(subject);
    return mgr && rdv<uint8_t>(mgr + u2s::kMgr_State) == 2;
}

bool user2_stages_tick_vf40(uint16_t opcode, uintptr_t subject, uintptr_t field, uint32_t ret_rva) noexcept {
    // The rule of 2026-10-13: never substituted (every vf40 of its tick gives the owner's, the host's, quest manager);
    // counted only, the context builder's call apart.
    if (!g_started.load(std::memory_order_relaxed) || !armed_tick(opcode, subject, field)) return false;
    (ret_rva == kCtxVf40Ret ? g_tick_vf40_own : g_tick_vf40_other).fetch_add(1, std::memory_order_relaxed);
    return false;
}

uintptr_t user2_stages_timer_vf48(uint16_t opcode, uint32_t ret_rva, uintptr_t subject, uintptr_t field) noexcept {
    if (!g_started.load(std::memory_order_relaxed)) return 0;
    if (opcode != u2s::kOp2043 && opcode != u2s::kOp2045 && opcode != u2s::kOp2046) return 0;
    // Review M3: counted by (opcode, return, answered with its own manager or the owner's): bit 48 = its own.
    const uint64_t vkey = static_cast<uint64_t>(opcode) << 32 | ret_rva;
    // Inside its ARMED 2043 tick every vf48 of the field belongs to its manager's update (natively the field's owner's
    // tick: the field's vf48 IS the ticking manager). S30 / the 753f275 run: ~46 per tick at 0x1AC99F7
    // (0x1AC9840 from 0x2913080 / 0x29133B0, before and after the collector), which registered on the host's manager.
    const uintptr_t mgr = armed_tick(opcode, subject, field) ? seq_manager(subject) : 0;
    g_timer_vf48.add(vkey | (mgr ? 1ull << 48 : 0));
    if (!mgr) return 0;
    mark_own_mgr_troc();  // U-e (review 6)
    if (g_tick_vf48_own.fetch_add(1, std::memory_order_relaxed) < 3) {
        p::Event e;
        e.v[0] = opcode;
        e.v[1] = static_cast<uint64_t>(u2s::Route::Own);
        e.v[2] = mgr;
        e.v[3] = ret_rva;
        e.v[8] = static_cast<uint64_t>(Who::User2);
        emit(kERoute, e, true);
    }
    return mgr;
}

void user2_stages_troc_begin() noexcept {
    TrocMark& t = t_troc;
    ++t.depth;
    if (t.depth < 16) t.own[t.depth] = false;
    // S36: un id nuevo para el marco de familias de esta profundidad; un marco que quedara de una salida perdida se
    // cierra (sus reservas vuelven).
    if (family_mode() != npcact::Mode::Off) {
        fam_close(npcfam::stack_begin(t.fam, t.depth, g_troc_serial.fetch_add(1, std::memory_order_relaxed) + 1));
    }
    // S38 fase C: un id por Troc del usuario 2 y profundidad; lo que quedara de una salida perdida a esta profundidad se
    // cierra antes.
    if (road_mode() != npcact::Mode::Off && t.depth < npcfam::kDepths) {
        road_close_depth(t.depth);
        t_road.id[t.depth] = g_road_troc_serial.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    if (!g_started.load(std::memory_order_relaxed) || !g_p_draining.load(std::memory_order_seq_cst)) return;
    // Review 1: the host's preparation drains user 2's manager on another thread right now: wait (at most 200 ms).
    g_troc_waits.fetch_add(1, std::memory_order_relaxed);
    const uint64_t until = p8_now_us() + 200'000;
    while (g_p_draining.load(std::memory_order_seq_cst)) {
        if (p8_now_us() >= until) {
            g_troc_wait_timeouts.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        Sleep(0);
    }
}

bool user2_stages_wants_start_action() noexcept {
    return g_started.load(std::memory_order_relaxed) && ctl_mode_now() == u2s::Mode::On;
}

void user2_stages_start_action_done(uint32_t key, int32_t result) noexcept {
    if (npc_probes() && npc_tracked(key)) {
        ActivationLock guard;
        if (auto* a = g_activation.find(key); a && !a->removed && !a->handler_us) {
            a->handler_us = p8_now_us();
            npc_event(kNpcHandler, *a, static_cast<uint32_t>(result));
        }
    }
    if (result != 0 || !key) return;
    uint16_t before = 0;
    const uint64_t now = p8_now_us();
    if (!g_ctl_keys.mark(key, u2s::kKeyAccepted, now, &before) || (before & u2s::kKeyAccepted) ||
        !(before & u2s::kKeyAssigned)) {
        return;
    }
    // Its first accepted action: the delay from its birth (put at 0x2792A7B).
    const uint64_t put = g_ctl_keys.put_time(key);
    const uint64_t d = put && now >= put ? now - put : 0;
    g_ctl_accepted.fetch_add(1, std::memory_order_relaxed);
    g_ctl_accept_us_sum.fetch_add(d, std::memory_order_relaxed);
    uint64_t mx = g_ctl_accept_us_max.load(std::memory_order_relaxed);
    while (d > mx && !g_ctl_accept_us_max.compare_exchange_weak(mx, d, std::memory_order_relaxed)) {
    }
    if (d >= 1'000'000) g_ctl_accept_slow.fetch_add(1, std::memory_order_relaxed);
}

void user2_stages_troc_end() noexcept {
    TrocMark& t = t_troc;
    if (!t.depth) return;
    // S36: el marco de familias de este Troc se cierra con él (censo, reservas sin usar devueltas).
    if (const uint8_t f = npcfam::stack_end(t.fam, t.depth)) fam_close(f);
    // S38 fase C: las unidades de viajeros abiertas en este Troc se cierran con él (censo; ningún puntero sobrevive).
    if (t.depth < npcfam::kDepths && (t_road.id[t.depth] || t_road.n)) {
        road_close_depth(t.depth);
        t_road.id[t.depth] = 0;
    }
    if (t.depth < 16) t.own[t.depth] = false;
    --t.depth;
}

void user2_stages_any_troc_exit() noexcept {
    if (!g_started.load(std::memory_order_relaxed)) return;
    road_continue();
}

void user2_stages_troc_enter(uintptr_t subject, uint16_t opcode) noexcept {
    // S33: its load ends its 2043 chain silently (the 12:24:47 tick, its 2437 0.8 s later, no tick after): taken as
    // ended, so the next arm queues a new one.
    if (subject && subject == g_u2_char.load(std::memory_order_relaxed) && is_load(opcode)) {
        g_chain_ended.store(true, std::memory_order_relaxed);
        // S37 phase D (review 3b): when user 2 started a load (a fast candidate waiting for confirmation drops).
        if (fast_mode_now() != u2s::Mode::Off) g_u2_load_us.store(p8_now_us(), std::memory_order_relaxed);
    }
    // S38 fase C: su carga (viaje rápido, teletransporte, cambio de campo) o su despedida: sus viajeros se retiran ya,
    // con cualquier user2_stages (nadie competente queda junto a ellos). Nada al acercarse el anfitrión.
    if (subject && subject == g_u2_char.load(std::memory_order_relaxed) && drains_at_entry(opcode)) {
        road_request(opcode == kOp2001 ? road::Why::Its2001 : road::Why::ItsLoad, opcode);
    }
    if (mode_now() != u2s::Mode::On || !drains_at_entry(opcode)) return;
    if (!subject || subject != g_u2_char.load(std::memory_order_relaxed)) return;
    // Its load (or its 2001 as its Troc): unload its stages now, before F3 / F4 reset its +0x6A0 inside the Req.
    // Review A6: also when not armed but its manager still holds stages in the host's field (a distance drain whose
    // immediate 2043 has not run yet). U-1 waits for a new dwell.
    const uintptr_t mgr = seq_manager(subject);
    if (g_armed.load(std::memory_order_acquire) || (mgr && lists_nonempty(mgr) && host_owns(field_of(subject)))) {
        drain_now(mgr, true, opcode, opcode == kOp2001 ? kCauseLogout : kCauseLoad);
        g_reset_machine.store(true);
    }
    // Review 3: whatever is still given to it goes before its load / goodbye.
    ctl_cleanup(0, kCleanItsTroc, opcode);
}

void user2_stages_troc_exit(uintptr_t subject, uint16_t opcode) noexcept {
    road_continue();  // S38 fase C: una retirada de viajeros pendiente continúa (acotada, cada 100 ms como mucho)
    if (mode_now() != u2s::Mode::On) return;
    if (!subject || subject != g_u2_char.load(std::memory_order_relaxed) || drains_at_entry(opcode)) return;
    if (!server_realm()) return;  // Low: only on the server's own threads
    // S33: armed and its chain silent for 1.5 periods: queued again (once per silence).
    if (g_armed.load(std::memory_order_acquire) && !g_aborted.load() &&
        u2s::chain_kick_due(p8_now_us(), g_arm_done_us.load(), g_last_mgr2_tick_us.load(), g_kicks_arm.load(),
                            u2s::timer_2043_period_ms(rdv<int32_t>(g_base + kPeriodSlot)))) {
        kick_chain(subject, opcode);
    }
    npc_activation_cleanup(opcode);
    // The 6c12c82 crash review: after the 'unaccepted' abort, those NPCs go now.
    if (!containment_ready() && ctl_mode_now() == u2s::Mode::On && g_cleanup_unaccepted_req.exchange(false)) {
        ctl_cleanup(0, kCleanUnaccepted, opcode, true);
    }
    // Review 3: 5 s after a drain, what it left still given to user 2 goes (once per drain).
    if (ctl_mode_now() == u2s::Mode::On) {
        const uint64_t drain = g_last_drain_us.load(std::memory_order_relaxed);
        uint64_t done = g_cleanup_done_for.load(std::memory_order_relaxed);
        const uint64_t now = p8_now_us();
        if (drain && done != drain && now >= drain + 5'000'000 &&
            g_cleanup_done_for.compare_exchange_strong(done, drain)) {
            g_cleanup_reruns.store(0, std::memory_order_relaxed);
            ctl_after_drain_rerun(drain, ctl_cleanup(drain, kCleanAfterDrain, opcode), 0, now);
        } else if (uint64_t at = g_cleanup_rerun_at.load(std::memory_order_relaxed);
                   at && now >= at && drain && g_cleanup_rerun_for.load(std::memory_order_relaxed) == drain &&
                   g_cleanup_rerun_at.compare_exchange_strong(at, 0)) {
            // Segunda revisión (R1): lo que una pasada de familia dejó bloqueado se mira otra vez (acotado).
            const uint8_t k = static_cast<uint8_t>(g_cleanup_reruns.fetch_add(1, std::memory_order_relaxed) + 1);
            g_cleanup_reruns_run.fetch_add(1, std::memory_order_relaxed);
            ctl_after_drain_rerun(drain, ctl_cleanup(drain, kCleanAfterDrain, opcode), k, now);
        }
    }
    if (g_req.load(std::memory_order_acquire) == kReqNone) return;
    const uint8_t req = g_req.exchange(kReqNone, std::memory_order_acq_rel);
    if (req == kReqArm) {
        // Not after an abort, not with its goodbye asked for (the network thread's flag). Busy: asked again (a fast
        // request stays fast). S37 phase D: g_req_fast is false with user2_fast_rearm=0.
        const bool fast = g_req_fast.load(std::memory_order_acquire);
        if (!g_aborted.load() && !g_armed.load() && !server_logout_requested() && !arm_now(subject, opcode, fast)) {
            uint8_t none = kReqNone;
            g_req.compare_exchange_strong(none, kReqArm);
        } else {
            g_req_fast.store(false, std::memory_order_relaxed);
        }
    } else if (req == kReqDrain) {
        const uintptr_t mgr = seq_manager(subject);
        // After an abort, or for leftovers (a drain whose 2043 did not unload them): the unload at once, here.
        const auto cause = static_cast<Cause>(g_req_cause.load());
        if ((g_armed.load() || (mgr && lists_nonempty(mgr))) &&
            !drain_now(mgr, g_aborted.load() || cause == kCauseLeftover, opcode, cause)) {
            uint8_t none = kReqNone;
            g_req.compare_exchange_strong(none, kReqDrain);  // busy: asked again
        }
    }
}

static void npc_observe_req(uint16_t op, uint32_t key, bool guest) noexcept;
void user2_stages_server_packet(uint16_t opcode, bool from_user2, uintptr_t packet, uint16_t size) noexcept {
    if (!from_user2 || !g_started.load(std::memory_order_relaxed)) return;
    if (npc_probes() && packet && size >= 9) {
        uint32_t key = 0;
        if (rd(packet + 5, key)) npc_observe_req(opcode, key, true);
        if (g_road_probe.load() && opcode == 2550) {
            // 2550 no empieza por una clave: u8 + f32 y entradas de24bytes (p8-s14-viajeros §1.1).
            for (uint16_t at = 10; static_cast<uint32_t>(at) + 24 <= size; at = static_cast<uint16_t>(at + 24)) {
                uint8_t entry[24] = {};
                if (!game::fast_read(packet + at, entry, sizeof entry)) break;
                const std::span<const uint8_t> bytes(entry, sizeof entry);
                npcact::Actor a; a.key = g_u2_key.load(); a.node.epoch = server_epoch();
                a.node.data = npcact::u32(bytes, 0); a.node.index = entry[7];
                npc_event(kNpcRoad, a, npcact::u32(bytes, 8) | static_cast<uint64_t>(npcact::u32(bytes, 16)) << 32,
                          npcact::u32(bytes, 4));
            }
        }
    }
    // S38 fase C: su Req de IA para un viajero registrado (su primera; su posición: ¿se mueve?).
    if (packet && size >= 9) road_on_req(opcode, packet + 5, static_cast<uint16_t>(size - 5), true);
    // U-e: an AI Req of user 2 naming an NPC it was given (or would have been): driven.
    if (ctl_mode_now() != u2s::Mode::Off && u2s::ai_req(opcode) && packet && size >= 9) {
        uint32_t key = 0;
        uint16_t before = 0;
        if (rd(packet + 5, key) && g_ctl_keys.mark(key, u2s::kKeyDriven, p8_now_us(), &before) &&
            !(before & u2s::kKeyDriven)) {
            g_ctl_drive_first.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (opcode != kOp2001 || mode_now() == u2s::Mode::Off) return;
    // Its goodbye, on ServerRunThread before the packet is handled. No game call here: 0x287D4C0 attaches to its
    // character's field itself (cdmp_Actor_GetFieldContext), but nothing shows that is safe from this thread. Only
    // counted: whether it came while armed (its ExecuteTroc entry drains it if the 2001 runs as its Troc).
    g_2001_seen.fetch_add(1, std::memory_order_relaxed);
    if (g_armed.load(std::memory_order_acquire)) {
        g_2001_armed.fetch_add(1, std::memory_order_relaxed);
        p::Event e;
        e.v[0] = kCtlGoodbyeArmed;
        e.v[1] = g_u2_mgr.load(std::memory_order_relaxed);
        e.v[5] = opcode;
        e.v[8] = static_cast<uint64_t>(Who::User2);
        emit(kECtl, e, true);
    }
}

static uint16_t npc_req_bit(uint16_t op) noexcept {
    switch (op) {
        case 2223: return 1; case 2734: return 2; case 2556: return 4;
        case 2405: return 8; case 2291: return 16; case 2828: return 32;
        default: return 0;
    }
}
// Every Req of user 2 and every C>S of the host's client: the opcode first, then a shared lookup. One line per key and
// side (its first Req); the other opcodes only set their bit.
static void npc_observe_req(uint16_t op, uint32_t key, bool guest) noexcept {
    if (!npc_probes()) return;
    const uint16_t bit = npc_req_bit(op);
    if (!bit) return;
    // S35: señal de vida del cliente invitado para la contención (npcact::Context): sólo sus Req de IA reales, de
    // cualquier clave; nunca temporizadores sintéticos (2043/2045/2046 no pasan por aquí).
    if (guest) g_last_guest_req_us.store(p8_now_us(), std::memory_order_relaxed);
    if (!npc_tracked(key)) return;
    ActivationLock guard;
    auto* a = g_activation.find(key);
    if (!a || a->removed) return;
    uint16_t& bits = guest ? a->guest_req_bits : a->host_req_bits;
    const bool first = !bits;
    bits |= bit;
    if (guest) g_activation.guest_req(key, p8_now_us());  // la primera sube la versión de la unidad
    if (first) npc_event(kNpcReq, *a, op, guest ? 2 : 1);
}
static void npc_observe_2005(std::span<const uint8_t> payload, bool to_guest) noexcept {
    // Only what is queued to user 2 (its delay origin and the travellers it asked for); the host client's copy adds
    // nothing the activation table needs.
    if (!npc_probes() || !to_guest) return;
    u2s::for_each_2005_rec(payload.data(), payload.size(),
        [to_guest](uint8_t type, uint32_t key, uint32_t controller, const uint8_t* record, size_t length) {
        const auto road = npcact::road_metadata(std::span<const uint8_t>(record, length));
        const bool road_line = g_road_probe.load(std::memory_order_relaxed) && to_guest && type == 3 && road.valid &&
                               road.requester == g_u2_key.load(std::memory_order_relaxed) && road.id;
        const bool tracked = npc_tracked(key);
        if (!tracked && !road_line) return;
        ActivationLock guard;
        auto* a = g_activation.find(key);
        if (a && !a->removed && to_guest && !a->queued_us) {  // un replay 2005 no crea una nueva generación
            g_activation.queued(key, controller, p8_now_us());
            npc_event(kNpcQueued, *a, s3::restore_byte_of(std::span<const uint8_t>(record, length)), controller);
        }
        // Viajeros: su controlador suele ser el anfitrión (sin registro); una línea por clave (caché acotada).
        if (road_line && g_road_seen.insert(key)) {
            npcact::Actor seen;
            seen.key = key; seen.controller = controller; seen.type = type; seen.node.epoch = server_epoch();
            npc_event(kNpcRoad, a && !a->removed ? *a : seen,
                      static_cast<uint64_t>(road.id) | static_cast<uint64_t>(road.metadata) << 32, road.requester);
        }
    });
}

// U-e (mode 1): a 2005 record of an NPC given to user 2 must name user 2's key as its controller (+0x12).
static bool ctl_live_assigned(uint32_t key) noexcept {
    const uint16_t f = g_ctl_keys.flags(key);
    return (f & u2s::kKeyAssigned) && !(f & u2s::kKeyRemoved);  // review 5: a removed key may be reused by the game
}

static void ctl_check_2005(const uint8_t* p, size_t n, std::atomic<uint64_t>& ok, std::atomic<uint64_t>& bad,
                           std::atomic<uint64_t>* restore0 = nullptr) noexcept {
    const uint32_t key2 = g_ctl_key2.load(std::memory_order_relaxed);
    if (!key2) return;
    u2s::for_each_2005_rec(p, n, [&](uint8_t, uint32_t key, uint32_t ctl, const uint8_t* rec, size_t len) {
        if (!ctl_live_assigned(key)) return;
        // The host's client: an NPC user 2 controls must reach it with its first action (restore 1), never with 0
        // (the exe+0x1831598 crash; child_hold withholds it).
        if (restore0 && s3::restore_byte_of(std::span<const uint8_t>(rec, len)) == 0) {
            restore0->fetch_add(1, std::memory_order_relaxed);
        }
        if (ctl == key2) {
            ok.fetch_add(1, std::memory_order_relaxed);
        } else {
            bad.fetch_add(1, std::memory_order_relaxed);
            g_ctl_keys.mark(key, u2s::kKeyBad2005, p8_now_us());
        }
    });
}

void user2_stages_on_out(uint16_t opcode, std::span<const uint8_t> payload, bool to_user2) noexcept {
    if (!to_user2 || (mode_now() == u2s::Mode::Off && !npc_probes())) return;
    if (opcode == 2005) {
        npc_observe_2005(payload, true);
        if (ctl_mode_now() == u2s::Mode::On) ctl_check_2005(payload.data(), payload.size(), g_ctl_2005_ok, g_ctl_2005_bad);
        road_check_2005(payload.data(), payload.size(), false);  // S38: viajeros, al invitado
        return;
    }
    const uint32_t key = g_u2_key.load(std::memory_order_relaxed);
    if (!key) return;
    uint32_t who = 0;
    if (opcode == kOp2322 && payload.size() >= 4) {
        std::memcpy(&who, payload.data(), 4);
        if (who != key) return;
        g_2322_to_u2.fetch_add(1, std::memory_order_relaxed);
        const uint64_t armed_at = g_arm_done_us.load(std::memory_order_relaxed);
        if (armed_at && p8_now_us() - armed_at <= 5'000'000) g_unexp_2322.fetch_add(1, std::memory_order_relaxed);
    } else if (opcode == kOp2335 && payload.size() >= 5) {
        std::memcpy(&who, payload.data() + 1, 4);
        if (who == key) g_2335_own_to_u2.fetch_add(1, std::memory_order_relaxed);
    }
}

void user2_stages_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept {
    constexpr size_t kHeader = 5;
    if (!packet || size < kHeader + 4 || (mode_now() == u2s::Mode::Off && !npc_probes())) return;
    if (npc_probes()) {
        uint16_t op = 0; uint32_t key = 0;
        std::memcpy(&op, packet, 2); std::memcpy(&key, packet + kHeader, 4);
        if (!server_to_client) npc_observe_req(op, key, false);
        else if (op == 2005) npc_observe_2005(std::span<const uint8_t>(packet + kHeader, size - kHeader), false);
        // S38 fase C: el cliente del anfitrión: 2005 de viajeros (controlador, restore 0) y sus Req (doble control).
        if (!server_to_client) {
            road_on_req(op, reinterpret_cast<uintptr_t>(packet + kHeader),
                        static_cast<uint16_t>(std::min<size_t>(size - kHeader, 0xFFFF)), false);
        } else if (op == 2005) {
            road_check_2005(packet + kHeader, size - kHeader, true);
        }
    }
    // U-e (mode 1): at the host's client the NPCs given to user 2 are remote: their 2005 names user 2, and the client
    // sends no AI Req for them.
    if (ctl_mode_now() == u2s::Mode::On) {
        uint16_t cop = 0;
        std::memcpy(&cop, packet, 2);
        if (!server_to_client && u2s::ai_req(cop)) {
            uint32_t key = 0;
            std::memcpy(&key, packet + kHeader, 4);
            if (ctl_live_assigned(key)) {
                g_ctl_host_req.fetch_add(1, std::memory_order_relaxed);
                g_ctl_keys.mark(key, u2s::kKeyHostReq, p8_now_us());
            }
        } else if (server_to_client && cop == 2005) {
            ctl_check_2005(packet + kHeader, size - kHeader, g_ctl_host_2005_ok, g_ctl_host_2005_bad,
                           &g_ctl_host_2005_r0);
        }
    }
    if (!server_to_client || size < kHeader + 5) return;
    const uint32_t key = g_u2_key.load(std::memory_order_relaxed);
    if (!key) return;
    uint16_t op = 0;
    std::memcpy(&op, packet, 2);
    const uint8_t* pl = packet + kHeader;
    uint32_t who = 0;
    size_t i = 0;
    switch (op) {
        case kOp2335: std::memcpy(&who, pl + 1, 4); i = 0; break;
        case kOp2126: std::memcpy(&who, pl, 4); i = 1; break;
        case kOp2958: std::memcpy(&who, pl + 1, 4); i = 2; break;
        case kOp2909: std::memcpy(&who, pl + 1, 4); i = 3; break;
        default: return;
    }
    if (who == key) g_uh[i].fetch_add(1, std::memory_order_relaxed);
}

uintptr_t user2_stages_user2_manager() noexcept {
    if (!g_started.load(std::memory_order_relaxed)) return 0;
    return seq_manager(g_u2_char.load(std::memory_order_relaxed));
}

bool user2_stages_host_field(uintptr_t field) noexcept {
    return g_started.load(std::memory_order_relaxed) && field && rdp(field) == g_base + u2s::kVtServerField &&
           host_owns(field);
}

bool user2_stages_armed() noexcept {
    return g_started.load(std::memory_order_relaxed) && mode_now() == u2s::Mode::On &&
           g_armed.load(std::memory_order_acquire) && !g_aborted.load(std::memory_order_relaxed);
}

uintptr_t user2_stages_host_manager() noexcept {
    if (!g_started.load(std::memory_order_relaxed)) return 0;
    return seq_manager(g_host_char.load(std::memory_order_relaxed));
}

bool user2_stages_user2_in_host_field() noexcept {
    return g_started.load(std::memory_order_relaxed) && host_owns(field_of(g_u2_char.load(std::memory_order_relaxed)));
}

}  // namespace cdmp::session2
