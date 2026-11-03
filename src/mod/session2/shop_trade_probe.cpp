#include "mod/session2/shop_trade_probe.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <span>
#include <string>
#include <vector>

#include <safetyhook.hpp>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/eerrno_names.h"
#include "mod/session2/guest.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/guest_x_interaction.h"
#include "mod/session2/guest_x_veto.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"
#include "mod/session2/server.h"
#include "mod/session2/shop_probe_logic.h"
#include "mod/session2/shop_trade_probe_logic.h"
#include "mod/session2/stage_lookup_logic.h"
#include "mod/session2/user2_funnels.h"
#include "mod/session2/user2_shops_logic.h"
#include "mod/session2/user2_stages.h"

namespace cdmp::session2 {
namespace {

namespace p = p8p;
namespace off = game::build_2976;
using shp::Who;

// ---------------------------------------------------------------- the points (build 2976, .rsrc; s46-datos/nativo/
// verify_f1.py: each 40-byte signature is unique in the exe, whole instructions, no branch nor absolute pointer into
// a patch but its first byte; the only relative instruction patched is P6's je rel8, which safetyhook relocates)

enum Pt : uint8_t { kP1, kP1b, kU2c, kP2, kP2a, kP5, kP6, kPoints };
constexpr const char* kPtName[kPoints] = {"P1 0x21F2C45",  "P1b 0x21F2917", "U2-c 0x21F28A1", "P2 0x2369862",
                                          "P2a 0x2369695", "P5 0x2B36FB3",  "P6 0x2B3708C"};

constexpr uint8_t kSigP1[] = {0x48, 0x39, 0x5C, 0x24, 0x28, 0x0F, 0x84, 0xBF, 0x00, 0x00, 0x00, 0x8B, 0x05, 0x0A,
                              0x46, 0xB0, 0x04, 0x41, 0x89, 0x06, 0x48, 0x8D, 0x35, 0xE8, 0x31, 0x53, 0x03, 0x48,
                              0x89, 0x75, 0x87, 0x80, 0x7D, 0x97, 0x00, 0x74, 0x76, 0x80, 0x7D, 0x98};
constexpr uint8_t kSigP1b[] = {0x41, 0xF7, 0xC4, 0xFF, 0xFF, 0xFF, 0x1F, 0x75, 0x38, 0x8B, 0x05, 0x3A, 0x49, 0xB0,
                               0x04, 0x41, 0x89, 0x06, 0x48, 0x8D, 0x05, 0x30, 0x05, 0x72, 0x03, 0x48, 0x89, 0x45,
                               0xA7, 0x80, 0x7D, 0xB7, 0x00, 0x74, 0x0E, 0x48, 0x8B, 0x4D, 0xCF, 0x48};
constexpr uint8_t kSigU2c[] = {0x48, 0x8B, 0xD8, 0x48, 0x8D, 0x50, 0x08, 0x48, 0x85, 0xC0, 0x48, 0x0F, 0x44, 0xD7,
                               0x48, 0x8B, 0x12, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x34, 0x15, 0x1A, 0xFE, 0x90,
                               0x48, 0x89, 0x5C, 0x24, 0x38, 0xB9, 0x02, 0x00, 0x00, 0x00, 0x89, 0x4C};
constexpr uint8_t kSigP2[] = {0x8B, 0x10, 0x49, 0x8B, 0xCC, 0xE8, 0x44, 0xAD, 0xDB, 0xFF, 0x90, 0x48, 0x8B, 0x75,
                              0xC8, 0x48, 0x8B, 0x06, 0x33, 0xD2, 0x48, 0x8B, 0xCE, 0xFF, 0x50, 0x20, 0x80, 0x7D,
                              0xD0, 0x01, 0x0F, 0x85, 0x3D, 0xFF, 0xFF, 0xFF, 0xB8, 0xFF, 0xFF, 0xFF};
constexpr uint8_t kSigP2a[] = {0x45, 0x0F, 0xB7, 0xC6, 0x49, 0x8B, 0xCC, 0xE8, 0xCF, 0xAC, 0xDB, 0xFF, 0x90, 0x48,
                               0x8D, 0x35, 0x97, 0xC2, 0x6F, 0x04, 0x41, 0xF6, 0xC7, 0x02, 0x74, 0x5E, 0x41, 0x83,
                               0xE7, 0xFD, 0x44, 0x89, 0x7C, 0x24, 0x70, 0x48, 0x3B, 0xFE, 0x74, 0x49};
constexpr uint8_t kSigP5[] = {0x48, 0x8B, 0xF8, 0x48, 0x8B, 0x45, 0xCF, 0x48, 0x8B, 0x08, 0x48, 0x8B, 0x91, 0x78,
                              0x01, 0x00, 0x00, 0x48, 0x8B, 0xC8, 0xFF, 0xD2, 0x49, 0x8B, 0x4D, 0x68, 0x4C, 0x8D,
                              0x4D, 0x9F, 0x4C, 0x8B, 0xC7, 0x0F, 0xB6, 0xD0, 0x48, 0x8B, 0x89, 0x98};
constexpr uint8_t kSigP6[] = {0x41, 0x80, 0x3F, 0x02, 0x74, 0x29, 0x41, 0x8B, 0x47, 0x04, 0x89, 0x06, 0x48, 0x8D,
                              0x4D, 0xB7, 0xE8, 0x0F, 0xDD, 0x85, 0xFD, 0x90, 0x48, 0x8D, 0x4D, 0xA7, 0xE8, 0xE5,
                              0xAD, 0x85, 0xFD, 0x90, 0x48, 0x8D, 0x4D, 0x9F, 0xE8, 0xFB, 0xDC, 0x85};

// What the hooks rely on in their function (checked like the signatures; no hook there).
// 0x21F2780's prologue: rbp = the entry rsp - 0x5F (the return address at [rbp+0x5F]); [rsp+0x38] = the manager
// (0x21F28BD); [rsp+0x28] = its current stage (0x21F28FA); rbx = the NPC's stage id (0x21F2C31).
constexpr uint8_t kHead21F2780[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48,
                                    0x89, 0x78, 0x20, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                                    0x48, 0x8D, 0x68, 0xA1, 0x48, 0x81, 0xEC, 0xF0, 0x00, 0x00, 0x00};
constexpr uint8_t kMgrSlot[] = {0x48, 0x89, 0x5C, 0x24, 0x38};                                // 0x21F28BD
constexpr uint8_t kCurSlot[] = {0x48, 0x8B, 0x04, 0x18, 0x48, 0x89, 0x44, 0x24, 0x28};        // 0x21F28FA
constexpr uint8_t kLinkRead[] = {0x48, 0x8B, 0x58, 0x10, 0x48, 0x8B, 0x5B, 0x30};             // 0x21F2C31
// Phase 2, U2-c: the field whose vf48 is called is [rbp-0x51] (0x21F2897 mov rcx,[rbp-0x51] ; mov rax,[rcx] ;
// call [rax+0x48]); 0x17FA6C0 filled the handle at [rbp-0x59] from the store component's actor (0x21F27D6).
constexpr uint8_t kFieldRead[] = {0x48, 0x8B, 0x4D, 0xAF, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x48};  // 0x21F2897
constexpr uint8_t kFieldFill[] = {0x48, 0x8D, 0x55, 0xA7, 0x48, 0x8B, 0x49, 0x08};              // 0x21F27D6
// 0x2369360's prologue: rbp = the entry rsp - 0x848 (rcx / rdx homes at [rbp+0x850] / [rbp+0x858]); the targets'
// cursor [rbp-0x60] (0x2369569, 0x23697C3); the actor and its component (0x2369590).
constexpr uint8_t kHead2369360[] = {0x44, 0x88, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4C, 0x24,
                                    0x08, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48,
                                    0x8D, 0xAC, 0x24, 0xF8, 0xF7, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x08, 0x09, 0x00, 0x00};
constexpr uint8_t kCursor[] = {0x48, 0x89, 0x45, 0xA0};  // 0x2369569 mov [rbp-0x60],rax
constexpr uint8_t kTarget[] = {0x48, 0x8B, 0x48, 0x08, 0x48, 0x8B, 0x41, 0x68, 0x4C, 0x8B, 0xA0, 0x98, 0x00, 0x00, 0x00};
constexpr uint8_t kAltaId[] = {0x48, 0x8B, 0x36, 0x48, 0x8B, 0x76, 0x30};  // 0x23695E8 rsi = [[rsi]+0x30]
// Phase 2, R0: 0x2369845..0x236987B around P2 (P2's own 5 bytes are its signature's, left out here: once P2 is in,
// P2a's check of this context still reads native bytes): the exit's exclusive lock of [actor+8] (0x393650 with
// r8b = 1 from 0x23695A8: its vf18 with dl = 0), the InteractionInfo getter, then the erase call and the unlock (vf20,
// dl = 0) at 0x2369879.
constexpr uint8_t kExitLock[] = {0x48, 0x85, 0xC9, 0x48, 0x0F, 0x44, 0xD0, 0x48, 0x8B, 0x12, 0x48, 0x8D, 0x4D, 0xC8, 0xE8,
                                 0xF8, 0x9D, 0x02, 0xFE, 0x90, 0x49, 0x8D, 0x4E, 0x38, 0xE8, 0x4E, 0xF0, 0x01, 0xFE};
constexpr uint8_t kExitErase[] = {0xE8, 0x44, 0xAD, 0xDB, 0xFF, 0x90, 0x48, 0x8B, 0x75, 0xC8, 0x48, 0x8B,
                                  0x06, 0x33, 0xD2, 0x48, 0x8B, 0xCE, 0xFF, 0x50, 0x20};  // 0x2369867
// 0x2B36EF0's prologue: rbp = rsp - 7 after the pushes (the index home [rbp+0x5F], the result [rbp+0x7F] -> r15);
// r13 = the NPC, r14 = the requester's component.
constexpr uint8_t kHead2B36EF0[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x66, 0x44, 0x89, 0x4C, 0x24, 0x20, 0x55, 0x56,
                                    0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24,
                                    0xF9, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00, 0x41, 0x0F, 0xB7, 0xD9, 0x4D,
                                    0x8B, 0xE8, 0x48, 0x8B, 0xF2, 0x4C, 0x8B, 0xF1, 0x4C, 0x8B, 0x7D, 0x7F};

struct Ctx {
    uint32_t rva;
    std::span<const uint8_t> bytes;
};
const Ctx kCtx21F2780[] = {{0x21F2780, kHead21F2780}, {0x21F28BD, kMgrSlot},   {0x21F28FA, kCurSlot},
                           {0x21F2C31, kLinkRead},    {0x21F2897, kFieldRead}, {0x21F27D6, kFieldFill}};
const Ctx kCtx2369360[] ={{0x2369360, kHead2369360}, {0x2369569, kCursor}, {0x2369590, kTarget},
                           {0x23695E8, kAltaId},      {0x2369845, kExitLock}, {0x2369867, kExitErase}};
// S46 live phase 1, finding B: the 2671's entry copy [rbp-0x61] (0x30 B; its +0x20 stage id = [rbp-0x41]): cleared at
// 0x2B36F90, filled by 0x2124060 (r9 at 0x2B36FCD), handed to vf170 (r8 at 0x2B3707C); between 0x2B36FE3 and P6 only
// the error exits (they leave the function) touch it [V capstone].
constexpr uint8_t kCopyClear[] = {0x48, 0x89, 0x4D, 0xBF};  // 0x2B36F90 mov [rbp-0x41],rcx
constexpr uint8_t kCopyFill[] = {0x4C, 0x8D, 0x4D, 0x9F};   // 0x2B36FCD lea r9,[rbp-0x61]
constexpr uint8_t kCopyCheck[] = {0x4C, 0x8D, 0x45, 0x9F};  // 0x2B3707C lea r8,[rbp-0x61]
const Ctx kCtx2B36EF0[] = {{0x2B36EF0, kHead2B36EF0}, {0x2B36F90, kCopyClear}, {0x2B36FCD, kCopyFill},
                           {0x2B3707C, kCopyCheck}};

void on_p1(safetyhook::Context& c) noexcept;
void on_p1b(safetyhook::Context& c) noexcept;
void on_u2c(safetyhook::Context& c) noexcept;
void on_p2(safetyhook::Context& c) noexcept;
void on_p2a(safetyhook::Context& c) noexcept;
void on_p5(safetyhook::Context& c) noexcept;
void on_p6(safetyhook::Context& c) noexcept;

// Which key installs a point: shop_trade_probe (the host's probes), user2_shop_trade_route (U2-c), or either
// shop_trade_probe or shop_interaction_own_delete (P2, R0's point; on the guest only R0 installs it).
// kWantProbeOrRoute: P6 (shop_trade_probe, or U2-c that needs its record of user 2's 2671 that passed vf170: finding B).
enum Want : uint8_t { kWantProbe, kWantRoute, kWantP2, kWantProbeOrRoute };
struct PointDef {
    Pt pt;
    uint32_t rva, patch, fn_begin, fn_end;
    std::span<const uint8_t> sig;
    std::span<const Ctx> ctx;
    safetyhook::MidHookFn fn;
    Want want;
};
const PointDef kDefs[] = {
    {kP1, 0x21F2C45, 5, 0x21F2780, 0x21F2F2E, kSigP1, kCtx21F2780, on_p1, kWantProbe},
    {kP1b, 0x21F2917, 7, 0x21F2780, 0x21F2F2E, kSigP1b, kCtx21F2780, on_p1b, kWantProbe},
    {kU2c, 0x21F28A1, 7, 0x21F2780, 0x21F2F2E, kSigU2c, kCtx21F2780, on_u2c, kWantRoute},
    {kP2, 0x2369862, 5, 0x2369360, 0x23698E7, kSigP2, kCtx2369360, on_p2, kWantP2},
    {kP2a, 0x2369695, 7, 0x2369360, 0x23698E7, kSigP2a, kCtx2369360, on_p2a, kWantProbe},
    {kP5, 0x2B36FB3, 7, 0x2B36EF0, 0x2B37366, kSigP5, kCtx2B36EF0, on_p5, kWantProbe},
    {kP6, 0x2B3708C, 6, 0x2B36EF0, 0x2B37366, kSigP6, kCtx2B36EF0, on_p6, kWantProbeOrRoute},
};

// Offsets (build 2976; the same ones shop_probe / user2_stages read).
constexpr uint32_t kActor_Key = 0x60, kActor_User = 0xA0, kUser_Key = 0x60, kActor_Comps = 0x68,
                   kComps_Interaction = 0x98, kStore_Owner = 0x08;
constexpr uint32_t kStage_Id = 0x30, kStage_Proc = 0x58, kStage_Data = 0x48, kData_Owner = 0x60, kFunc_Idx = 0x38;
constexpr uintptr_t kVtProcServer = 0x5B1FE10, kVtProcClient = 0x558C080;  // StageChartProcessor (S45 §3.1)
// The guest's client: R-A (guest_seq_present, s45) gives X's processors the mod's copy of 0x558C080, whose [-1] is the
// client's COL (guest_seq_present_logic.h gpr::kColProcClient): a client processor = that vtable or that COL.
constexpr uintptr_t kColProcClient = 0x5DB7A20;
// The client's ClientSequencerStageManager (guest_x_veto kVtStageManager) and a processor's manager [proc+8].
constexpr uintptr_t kVtClientStageMgr = 0x55C9720;
constexpr uint32_t kProc_Mgr = 0x08;
// U2-c: the field [rbp-0x51]; a ServerSequencerStageManager's state +0x6A0 (2 = armed).
constexpr int32_t kU2c_Field = -0x51;
constexpr uint32_t kMgr_State = 0x6A0;
// 0x21F2780's frame: [rsp+0x28] the manager's current stage, [rbp+0x5F] the return. ([rsp+0x38] holds the manager only
// until 0x21F2A56: P1 takes it from P1b's seal, finding A.)
constexpr uint32_t kP1_Cur = 0x28, kP1_Ret = 0x5F;
// 0x2369360's frame: [rbp+0x850] the function (rcx), [rbp+0x858] the context (rdx), [rbp-0x60] the target cursor.
constexpr int32_t kSi_Func = 0x850, kSi_Ctx = 0x858, kSi_Cursor = -0x60;
// 0x2B36EF0's frame: [rbp+0x5F] the u16 index; r14 = the requester's component ([+8] its actor), r13 = the NPC;
// [rbp-0x41] = the stage id (+0x20) of the entry copy 0x2124060 chose (the first with the asked key).
constexpr int32_t kPre_Idx = 0x5F, kPre_EntryStage = -0x41;
constexpr uint32_t kComp_Actor = 0x08;
// The InteractionInfo fields of the native "busy" rule (.xtls 0xE401150; nativo.md §3).
constexpr uint32_t kInfo_Type = 0x12, kInfo_Mode = 0x13, kInfo_Exempt = 0xBC;

uintptr_t g_base = 0;
std::atomic<bool> g_probe{false}, g_started{false}, g_settled{false};
std::atomic<uint8_t> g_route{0}, g_r0{0};  // stp::RouteMode: user2_shop_trade_route, shop_interaction_own_delete
std::atomic<bool> g_guest{false};          // the guest's client (R0 only: client processors, X against M)
std::atomic<bool> g_r0_latched{false};     // a write faulted: R0 native for the rest of the run
std::atomic<bool> g_on[kPoints] = {};
SafetyHookMid g_hooks[kPoints];
std::atomic<uintptr_t> g_mgr_h{0}, g_mgr_2{0};  // the thread's last sample (user2_stages' accessors, re-checked there)
// Finding B: the stage each NPC's last shop 2671 that passed vf170 used (P6), for U2-c during the dialogue.
stp::AcceptedBook<64> g_accepted;
std::atomic<uint64_t> g_accepted_put{0}, g_accepted_user2{0}, g_u2c_by_record{0}, g_accepted_closed{0};
// Finding A: P1b's manager for P1, sealed with the frame's rsp (per thread).
thread_local stp::P1Seal t_p1_seal;
std::atomic<uint64_t> g_p1_unsealed{0};
// Minor (live phase 1): the NPCs the ledger follows, for the hooks (their operations WITHOUT the shop key go to the
// ledger too, so that 'live != expected' means a change nobody saw). Written by the module's thread only.
constexpr size_t kLedgerKeys = 64;
std::atomic<uint32_t> g_ledger_keys[kLedgerKeys] = {};
bool ledger_follows(uint32_t npc) noexcept {
    if (!npc) return false;
    for (const auto& k : g_ledger_keys) {
        if (k.load(std::memory_order_relaxed) == npc) return true;
    }
    return false;
}

// ---------------------------------------------------------------- guarded reads

// game::fast_read (review M1): an access violation or in-page error returns false, and a PAGE_GUARD page a stale
// pointer touches gets its guard put back (safe_memory.cpp's filter), as user2_shops / user2_stages read.
template <class T>
bool srd(uintptr_t a, T& v) noexcept {
    return a >= 0x10000 && game::fast_read(a, &v, sizeof v);
}
template <class T>
T rdv(uintptr_t a, T fallback = T{}) noexcept {
    T v{};
    return srd(a, v) ? v : fallback;
}
uintptr_t rdp(uintptr_t a) noexcept { return rdv<uintptr_t>(a); }
struct Rd {
    template <class T>
    bool rd(uintptr_t a, T& v) const noexcept {
        return srd(a, v);
    }
};

uint32_t rva_of(uintptr_t a) noexcept {
    return a >= g_base && a - g_base < 0x10000000 ? static_cast<uint32_t>(a - g_base) : 0;
}
bool is_server_mgr(uintptr_t m) noexcept { return m && rdp(m) == g_base + isl::kVtServerStageMgr; }
uint64_t current_of(uintptr_t m) noexcept {
    return m ? stp::current_of(rdv<uint64_t>(m + stp::kMgr_Current), rdv<uint64_t>(m + stp::kMgr_Current2)) : 0;
}
stp::Where where_of(uint64_t id) noexcept {
    const Rd m;
    const uintptr_t h = g_mgr_h.load(std::memory_order_relaxed), u = g_mgr_2.load(std::memory_order_relaxed);
    const isl::Lookup lh = h ? isl::find_stage(m, h, id) : isl::Lookup{};
    const isl::Lookup lu = u ? isl::find_stage(m, u, id) : isl::Lookup{};
    return stp::where(h && lh.read_ok, lh.found, u && lu.read_ok, lu.found);
}
// The requester of a store check: the store component -> [+8] its actor -> its key and its user's key.
struct Who3 {
    uint32_t chr = 0, user = 0;
    Who w = Who::Unknown;
};
Who3 who_of_actor(uintptr_t actor) noexcept {
    Who3 r;
    r.chr = actor ? rdv<uint32_t>(actor + kActor_Key) : 0;
    const uintptr_t u = actor ? rdp(actor + kActor_User) : 0;
    r.user = u ? rdv<uint32_t>(u + kUser_Key) : 0;
    r.w = shp::who(r.user, r.chr, server_user2_user_key(), server_user2_player_key());
    return r;
}
// u16 InteractionInfo index -> its u32 key ([[[exe+0x6D69AB8]+0x58] + idx*8]); false: not loaded (or unreadable).
bool key_of_idx(uint16_t idx, uint32_t& key) noexcept {
    const uintptr_t tbl = rdp(g_base + stp::kInfoTableSlot);
    uint32_t count = 0;
    if (!tbl || !srd(tbl + stp::kTbl_Count, count) || idx >= count) return false;
    const uintptr_t arr = rdp(tbl + stp::kTbl_Array);
    const uintptr_t info = arr ? rdp(arr + static_cast<uintptr_t>(idx) * 8) : 0;
    return info && srd(info, key);
}

// ---------------------------------------------------------------- events and counters

enum Kind : uint16_t { kEP1, kEP1b, kEU2c, kEP5, kEP6, kEOut, kKindCount };
p::EventRing<512> g_ring;
stp::FillRing<stp::SiEvent, 256> g_si;
p::WindowLimiter g_lim[kKindCount] = {{48, 5'000'000}, {16, 5'000'000}, {32, 5'000'000},
                                      {16, 5'000'000}, {32, 5'000'000}, {32, 5'000'000}};
p::WindowLimiter g_si_lim{48, 5'000'000};       // the other SetInteraction lines (non-shop, not noop)
p::WindowLimiter g_si_orphan_lim{48, 5'000'000};  // [would orphan]: their own budget
isl::IdBook<256> g_book;
std::atomic<uint64_t> g_limited{0}, g_lines{0};
// Review B4: the shop NPCs' P2 / P2a events always reach the ledger; their LINES are limited here (the drain thread),
// [would orphan] with a budget of its own. What is not written is counted (limited).
p::WindowLimiter g_shop_line_lim{64, 5'000'000}, g_orphan_line_lim{32, 5'000'000};
constexpr uint64_t kMaxLines = 6000;

std::atomic<uint64_t> g_hits[kPoints] = {};
std::atomic<uint64_t> g_p1[2][3][static_cast<size_t>(stp::P1::Count)] = {};  // [server][who][verdict]
std::atomic<uint64_t> g_p1_mgr2_pass{0}, g_p1_mgr2_fail{0}, g_p1b_none{0};
std::atomic<uint64_t> g_u2c[static_cast<size_t>(stp::U2c::Count)] = {};
std::atomic<uint64_t> g_si_ops[2][2] = {};  // [server][add]
std::atomic<uint64_t> g_si_shop[2] = {}, g_si_r0[static_cast<size_t>(stp::R0::Count)] = {}, g_si_orphan[2] = {};
std::atomic<uint64_t> g_si_client{0}, g_si_unknown_proc{0}, g_si_incomplete{0}, g_si_comp_bad{0}, g_si_followed{0};
std::atomic<uint64_t> g_p6_fail{0}, g_p6_ok{0}, g_out3000{0}, g_out_nak{0};
std::atomic<uint64_t> g_ledger_checks{0}, g_ledger_mismatch{0};
// Phase 2: R0's plans [act] and reasons [why], what was applied / refused (latched, lock not proven, a write failed),
// U2-c's routings, and the guest's overlaps (an entry of another stage first with the key).
std::atomic<uint64_t> g_r0_act[static_cast<size_t>(stp::R0Act::Count)] = {};
std::atomic<uint64_t> g_r0_why[static_cast<size_t>(stp::R0Why::Count)] = {};
std::atomic<uint64_t> g_r0_applied[static_cast<size_t>(stp::R0Act::Count)] = {};
// Not applied (review F7): latched, the lock not proven (component / actor / WindowsRWLock vtable), a check before the
// write (count, block, VirtualQuery) refused it, or a write faulted (then latched).
std::atomic<uint64_t> g_r0_not_latched{0}, g_r0_not_lock{0}, g_r0_not_check{0}, g_r0_fault{0};
std::atomic<uint64_t> g_u2c_routed{0}, g_guest_overlap{0};
// [would orphan] exits: the native erase was going to take another stage's entry; avoided by R0 (applied) or left.
std::atomic<uint64_t> g_orphan_avoided{0}, g_orphan_left{0}, g_orphan_left_cross{0};

void emit(Kind k, p::Event& e) noexcept {
    e.kind = k;
    e.qpc_us = p8_now_us();
    e.tid = GetCurrentThreadId();
    if (!g_lim[k].take(e.qpc_us)) {
        g_limited.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_ring.push(e);
}

// ---------------------------------------------------------------- hooks

// P1 0x21F2C45 (0x21F2780): rbx = the NPC's stage, [rsp+0x28] = the manager's current, [rsp+0x38] = the manager,
// rsi = the NPC, r13 = the store component.
void on_p1(safetyhook::Context& c) noexcept {
    if (!g_on[kP1].load(std::memory_order_acquire)) return;
    g_hits[kP1].fetch_add(1, std::memory_order_relaxed);
    const uint64_t linked = c.rbx;
    const uint64_t cur = rdv<uint64_t>(c.rsp + kP1_Cur);
    // Finding A: [rsp+0x38] is overwritten before here (0x21F2A56 / 0x21F2AC5); the manager comes from P1b's seal.
    const uintptr_t mgr = stp::p1_manager(t_p1_seal, c.rsp);
    t_p1_seal = {};
    if (!mgr) g_p1_unsealed.fetch_add(1, std::memory_order_relaxed);
    const bool server = is_server_mgr(mgr);
    const Who3 w = who_of_actor(rdp(c.r13 + kStore_Owner));
    const stp::P1 v = stp::p1_verdict(cur, linked);
    g_p1[server ? 1 : 0][static_cast<size_t>(w.w) % 3][static_cast<size_t>(v)].fetch_add(1, std::memory_order_relaxed);
    const uintptr_t mh = g_mgr_h.load(std::memory_order_relaxed), m2 = g_mgr_2.load(std::memory_order_relaxed);
    const uint64_t cur2 = current_of(m2);
    if (server && w.w == Who::User2) {
        (cur2 == linked && linked ? g_p1_mgr2_pass : g_p1_mgr2_fail).fetch_add(1, std::memory_order_relaxed);
    }
    // Lines: every server check of user 2 and every server failure; the host's passes and the client's: a few per NPC.
    const uint32_t npc = rdv<uint32_t>(c.rsi + kActor_Key);
    if (!(server && (w.w == Who::User2 || v != stp::P1::Pass)) &&
        !g_book.take((static_cast<uint64_t>(server ? 0x11 : 0x12) << 56) | npc, server ? 4 : 2)) {
        return;
    }
    p::Event e;
    e.v[0] = linked;
    e.v[1] = cur;
    e.v[2] = mgr;
    e.v[3] = npc | static_cast<uint64_t>(w.chr) << 32;
    e.v[4] = w.user | static_cast<uint64_t>(w.w) << 32 | static_cast<uint64_t>(server) << 40 |
             static_cast<uint64_t>(v) << 48 | static_cast<uint64_t>(stp::mgr_is(mgr, mh, m2)) << 56;
    e.v[5] = rva_of(rdp(c.rbp + kP1_Ret));
    e.v[6] = current_of(mh);
    e.v[7] = cur2;
    e.v[8] = static_cast<uint64_t>(server ? where_of(linked) : stp::Where::Unknown);
    e.v[9] = rva_of(rdp(c.r13));  // the store component's vtable
    emit(kEP1, e);
}

// P1b 0x21F2917: r12 = the manager's current stage (rbx = the manager); only the "no current stage" case is logged.
void on_p1b(safetyhook::Context& c) noexcept {
    if (!g_on[kP1b].load(std::memory_order_acquire)) return;
    g_hits[kP1b].fetch_add(1, std::memory_order_relaxed);
    // Finding A: rbx = the manager (0x21F28A1 mov rbx,rax, after U2-c; not written up to here), sealed for P1.
    t_p1_seal = {c.rsp, c.rbx};
    if (c.r12 & stp::kStageMask) return;
    g_p1b_none.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t mgr = c.rbx;
    const Who3 w = who_of_actor(rdp(c.r13 + kStore_Owner));
    const uintptr_t mh = g_mgr_h.load(std::memory_order_relaxed), m2 = g_mgr_2.load(std::memory_order_relaxed);
    p::Event e;
    e.v[0] = c.r12;
    e.v[1] = mgr;
    e.v[2] = rdv<uint32_t>(c.rsi + kActor_Key) | static_cast<uint64_t>(w.chr) << 32;
    e.v[3] = w.user | static_cast<uint64_t>(w.w) << 32 | static_cast<uint64_t>(is_server_mgr(mgr)) << 40 |
             static_cast<uint64_t>(stp::mgr_is(mgr, mh, m2)) << 56;
    e.v[4] = rva_of(rdp(c.rbp + kP1_Ret));
    e.v[5] = current_of(m2);
    emit(kEP1b, e);
}

// The NPC's dynamic entries (P2 / P2a: under the actor lock the game holds there; U2-c: without it, SEH reads): at most
// stp::kMaxScan.
uint32_t read_entries(uintptr_t comp, stp::Entry* out, uint32_t& total) noexcept {
    total = 0;
    const uintptr_t vec = rdp(comp + stp::kComp_Vec);
    if (!srd(comp + stp::kComp_Count, total) || !vec) return 0;
    const uint32_t n = total < stp::kMaxScan ? total : stp::kMaxScan;
    uint32_t i = 0;
    for (; i < n; ++i) {
        const uintptr_t a = vec + static_cast<uintptr_t>(i) * stp::kEntrySize;
        uint16_t idx = 0;
        uint64_t stage = 0;
        if (!srd(a + stp::kEntry_Idx, idx) || !srd(a + stp::kEntry_Stage, stage)) break;
        out[i].idx = idx;
        out[i].stage = stage;
        out[i].key = 0;
        out[i].key_known = key_of_idx(idx, out[i].key);
    }
    return i;
}

// U2-c 0x21F28A1: rax = the manager field.vf48 gave; rsi = the NPC; r13 = the store component; [rbp-0x51] = the field.
// Phase 2: user2_shop_trade_route=1 makes RAX := mgr2 for user 2's own trade check (stp::decide_u2c, WouldRoute);
// probe only counts and logs.
void on_u2c(safetyhook::Context& c) noexcept {
    if (!g_on[kU2c].load(std::memory_order_acquire)) return;
    g_hits[kU2c].fetch_add(1, std::memory_order_relaxed);
    const uintptr_t owner = rdp(c.r13 + kStore_Owner);
    const Who3 w = who_of_actor(owner);
    stp::U2cIn in;
    in.got = c.rax;
    in.server = is_server_mgr(c.rax);
    in.user2 = w.w == Who::User2;
    if (in.server && in.user2) {
        uintptr_t subject = 0;
        uint16_t sop = 0;
        bool hooked = false;
        user2_funnels_subject_now(subject, sop, hooked);
        in.subject = hooked && subject != 0 && subject == owner;  // review F9: the Troc's subject IS the store's actor
        in.host_field = user2_stages_host_field(rdp(c.rbp + kU2c_Field));
        const uintptr_t h = user2_stages_host_manager(), u = user2_stages_user2_manager();
        in.host = is_server_mgr(h) ? h : 0;
        in.user2_mgr = is_server_mgr(u) ? u : 0;
        in.mgr2_ok = in.user2_mgr != 0;
        in.armed = user2_stages_armed();
        in.mgr2_state = in.user2_mgr ? rdv<uint8_t>(in.user2_mgr + kMgr_State) : 0;
        in.logout = server_logout_requested();
        in.mgr2_current = current_of(in.user2_mgr);
        const Rd m;
        if (in.mgr2_ok && (in.mgr2_current & stp::kStageMask)) {
            in.mgr2_current_found = isl::find_stage(m, in.user2_mgr, in.mgr2_current).found;
        }
        if (in.mgr2_ok && in.host && in.subject && in.host_field) {
            const uintptr_t comp = rdp(rdp(c.rsi + kActor_Comps) + kComps_Interaction);
            stp::Entry es[stp::kMaxScan];
            uint32_t total = 0;
            const uint32_t n = comp ? read_entries(comp, es, total) : 0;
            if (comp) {
                const uintptr_t hm = in.host, um = in.user2_mgr;
                const auto alive = [&](uint64_t id) {  // review ab M1: proven only
                    const isl::Lookup lh = isl::find_stage(m, hm, id), lu = isl::find_stage(m, um, id);
                    return stp::where_proven(lh.read_ok, lh.found, lu.read_ok, lu.found);
                };
                stp::u2c_npc(es, n, total, alive, in.npc_read, in.npc_in_mgr2, in.npc_in_mgrh);
            }
            // Finding B: the stage of user 2's own 2671 on this NPC that passed vf170 (the entries are gone in the dialogue).
            stp::Accepted a;
            if (g_accepted.get(rdv<uint32_t>(c.rsi + kActor_Key), p8_now_us(), a) &&
                a.who == static_cast<uint8_t>(Who::User2) && (a.stage & stp::kStageMask)) {
                in.rec = true;
                in.rec_stage = a.stage;
                const isl::Lookup lh = isl::find_stage(m, in.host, a.stage), lu = isl::find_stage(m, in.user2_mgr, a.stage);
                in.rec_where = stp::where_proven(lh.read_ok, lh.found, lu.read_ok, lu.found);  // review ab M1
            }
        }
    } else {
        in.host = g_mgr_h.load(std::memory_order_relaxed);
        in.user2_mgr = g_mgr_2.load(std::memory_order_relaxed);
        in.mgr2_ok = is_server_mgr(in.user2_mgr);
    }
    const stp::U2c v = stp::decide_u2c(in);
    g_u2c[static_cast<size_t>(v)].fetch_add(1, std::memory_order_relaxed);
    if (v == stp::U2c::WouldRoute && in.rec) g_u2c_by_record.fetch_add(1, std::memory_order_relaxed);
    bool routed = false;
    if (v == stp::U2c::WouldRoute && g_route.load(std::memory_order_relaxed) == static_cast<uint8_t>(stp::RouteMode::On)) {
        c.rax = in.user2_mgr;  // 0x21F28A1 mov rbx,rax ; lea rdx,[rax+8]: mgr2's lock and its current stage
        routed = true;
        g_u2c_routed.fetch_add(1, std::memory_order_relaxed);
    }
    if (!in.user2 && !g_book.take((0x21ull << 56) | static_cast<uint64_t>(v), 3)) return;
    p::Event e;
    e.v[0] = in.got;
    e.v[1] = in.host;
    e.v[2] = in.user2_mgr;
    e.v[3] = rdv<uint32_t>(c.rsi + kActor_Key) | static_cast<uint64_t>(w.chr) << 32;
    e.v[4] = w.user | static_cast<uint64_t>(w.w) << 32 | static_cast<uint64_t>(v) << 40 |
             static_cast<uint64_t>(routed) << 48 | static_cast<uint64_t>(in.subject) << 49 |
             static_cast<uint64_t>(in.host_field) << 50 | static_cast<uint64_t>(in.armed) << 51 |
             static_cast<uint64_t>(in.logout) << 52 | static_cast<uint64_t>(in.npc_read) << 53 |
             static_cast<uint64_t>(in.npc_in_mgr2) << 54 | static_cast<uint64_t>(in.npc_in_mgrh) << 55 |
             static_cast<uint64_t>(in.mgr2_current_found) << 56 | static_cast<uint64_t>(in.mgr2_state & 0x7F) << 57;
    e.v[5] = in.mgr2_current;
    e.v[6] = current_of(in.got);
    e.v[7] = rva_of(rdp(c.rbp + kP1_Ret));
    e.v[8] = in.rec_stage;
    e.v[9] = static_cast<uint64_t>(in.rec_where) | static_cast<uint64_t>(in.rec) << 8;
    emit(kEU2c, e);
}

// ---------------------------------------------------------------- R0 (phase 2): the two managers of this side

// Host: A = mgrH, B = mgr2 (user2_stages' accessors, vtable re-checked). Guest: A = the X managers guest_x_interaction
// noted (another character's ClientSequencerStageManager), B = this client's M (guest_x_veto, fresh).
struct Side {
    uintptr_t a[4];
    uint32_t na;
    uintptr_t b;
};
bool is_client_mgr(uintptr_t m) noexcept { return m && rdp(m) == g_base + kVtClientStageMgr; }
Side side_now(bool guest) noexcept {
    Side s;
    s.na = 0;
    s.b = 0;
    if (!guest) {
        const uintptr_t h = user2_stages_host_manager(), u = user2_stages_user2_manager();
        if (is_server_mgr(h)) s.a[s.na++] = h;
        if (is_server_mgr(u) && u != h) s.b = u;
        return s;
    }
    const uintptr_t m = guest_x_current_m();
    s.b = is_client_mgr(m) ? m : 0;
    uintptr_t xs[4];
    const size_t n = guest_x_interaction_known_x(xs, 4);
    for (size_t i = 0; i < n && i < 4; ++i) {
        if (xs[i] && xs[i] != s.b && is_client_mgr(xs[i])) s.a[s.na++] = xs[i];
    }
    return s;
}
// Membership (R0): the id has an element in that manager, live or flagged dead (+0x15: a stage being deleted is still
// that manager's). Where::Host = side A, Where::User2 = side B.
stp::Where where_in(const Side& s, uint64_t id) noexcept {
    const Rd m;
    bool a_ok = s.na > 0, a_found = false;
    for (uint32_t i = 0; i < s.na; ++i) {
        const isl::Lookup l = isl::find_stage(m, s.a[i], id);
        a_ok = a_ok && l.read_ok;
        a_found = a_found || l.found || l.dead;
    }
    bool b_ok = false, b_found = false;
    if (s.b) {
        const isl::Lookup l = isl::find_stage(m, s.b, id);
        b_ok = l.read_ok;
        b_found = l.found || l.dead;
    }
    return stp::where(a_ok, a_found, b_ok, b_found);
}
// This stage's manager on the guest: its processor's [proc+8] (guest_x_veto's G) against M and the X noted.
stp::Where own_where_guest(const Side& s, uintptr_t proc) noexcept {
    const uintptr_t g = rdp(proc + kProc_Mgr);
    if (!g) return stp::Where::Unknown;
    if (g == s.b) return stp::Where::User2;
    for (uint32_t i = 0; i < s.na; ++i) {
        if (g == s.a[i]) return stp::Where::Host;
    }
    return stp::Where::Unknown;
}

// R0's swap of entries i and j (j = this stage's own), under the actor's exclusive lock (P2). Both blocks are read
// first (SEH); the vector and its count must be the ones the plan was made on, block j must still carry this stage's
// id, and the whole vector must be committed read-write memory (VirtualQuery, review F8). The 12 u64 writes are
// SEH-guarded; a fault (never expected: the vector was just read under the lock) puts back what can be put back.
bool write_swap(uintptr_t a, uintptr_t b, const uint64_t* va, const uint64_t* vb) noexcept {
    volatile int k = 0, l = 0;
    __try {
        for (; k < 6; ++k) reinterpret_cast<volatile uint64_t*>(a)[k] = vb[k];
        for (; l < 6; ++l) reinterpret_cast<volatile uint64_t*>(b)[l] = va[l];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    __try {
        for (int r = 0; r < k; ++r) reinterpret_cast<volatile uint64_t*>(a)[r] = va[r];
        for (int r = 0; r < l; ++r) reinterpret_cast<volatile uint64_t*>(b)[r] = vb[r];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}
bool writable(uintptr_t begin, uintptr_t end) noexcept {
    for (uintptr_t a = begin; a < end;) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(reinterpret_cast<const void*>(a), &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) {
            return false;
        }
        const uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= a) return false;
        a = next;
    }
    return true;
}
enum class SwapRes : uint8_t { Ok, Refused, Fault };
SwapRes swap_entries(uintptr_t comp, uint32_t total, int i, int j, uint64_t own) noexcept {
    uint32_t count = 0;
    uintptr_t vec = 0;
    if (!srd(comp + stp::kComp_Count, count) || count != total || !srd(comp + stp::kComp_Vec, vec) || !vec) {
        return SwapRes::Refused;
    }
    if (i < 0 || j < 0 || i == j || static_cast<uint32_t>(i) >= count || static_cast<uint32_t>(j) >= count) {
        return SwapRes::Refused;
    }
    const uintptr_t a = vec + static_cast<uintptr_t>(i) * stp::kEntrySize, b = vec + static_cast<uintptr_t>(j) * stp::kEntrySize;
    uint64_t va[6], vb[6];
    for (int k = 0; k < 6; ++k) {
        if (!srd(a + 8u * static_cast<uint32_t>(k), va[k]) || !srd(b + 8u * static_cast<uint32_t>(k), vb[k])) {
            return SwapRes::Refused;
        }
    }
    if (vb[stp::kEntry_Stage / 8] != own) return SwapRes::Refused;
    if (!writable(vec, vec + static_cast<uintptr_t>(count) * stp::kEntrySize)) return SwapRes::Refused;
    return write_swap(a, b, va, vb) ? SwapRes::Ok : SwapRes::Fault;
}

// P2 / P2a: one SetInteraction exit (the erase follows) or entry (the append follows) on one target actor.
// Host: the server's stages only (processor 0x5B1FE10; the host client's 0x558C080 are only counted). Guest (R0 only):
// the client's (0x558C080, or the mod's R-A copy of it), while the connection is the host's.
void on_si(safetyhook::Context& c, bool add) noexcept {
    const Pt pt = add ? kP2a : kP2;
    if (!g_on[pt].load(std::memory_order_acquire)) return;
    g_hits[pt].fetch_add(1, std::memory_order_relaxed);
    const bool guest = g_guest.load(std::memory_order_relaxed);
    const uintptr_t ctxp = rdp(c.rbp + kSi_Ctx);
    const uintptr_t stage = rdp(ctxp);
    const uintptr_t proc = rdp(stage + kStage_Proc);
    const uintptr_t pvt = rdp(proc);
    const bool server = pvt && pvt == g_base + kVtProcServer;
    const bool client = pvt && (pvt == g_base + kVtProcClient || rdp(pvt - 8) == g_base + kColProcClient);
    if (guest ? !(client && guest_remote_live()) : !server) {
        (client ? g_si_client : g_si_unknown_proc).fetch_add(1, std::memory_order_relaxed);
        return;  // host: the host client's own stages (or an unknown processor); guest: not its client's, or local
    }
    g_si_ops[1][add ? 1 : 0].fetch_add(1, std::memory_order_relaxed);
    const uint64_t id = add ? c.rsi : rdv<uint64_t>(stage + kStage_Id);
    uint16_t idx = 0;
    uint32_t key = 0;
    bool key_known = false;
    if (add) {
        idx = static_cast<uint16_t>(c.r14 & 0xFFFF);
        key_known = key_of_idx(idx, key);
    } else {
        idx = rdv<uint16_t>(rdp(c.rbp + kSi_Func) + kFunc_Idx);
        key_known = srd(c.rax, key);  // rax = its InteractionInfo, [rax] = the key the erase compares
    }
    const uintptr_t comp = c.r12;
    const uintptr_t actor = rdp(rdp(c.rbp + kSi_Cursor) + 8);
    const bool comp_ok = actor && rdp(rdp(actor + kActor_Comps) + kComps_Interaction) == comp;
    if (!comp_ok) g_si_comp_bad.fetch_add(1, std::memory_order_relaxed);
    // R0 writes only when the component's own actor is that actor too: the readers (0x2124060) lock [[comp+8]+8], the
    // exit holds [actor+8] exclusively (0x2369853): the same lock object; and that object is a pa::WindowsRWLock (review
    // F5: vf18 0x1371AD0, exclusive with dl = 0).
    const uint32_t lock_vt = actor ? rva_of(rdp(rdp(actor + 8))) : 0;
    const bool lock_ok = comp_ok && rdp(comp + kComp_Actor) == actor && stp::is_rw_lock_vtable(lock_vt);
    stp::Entry es[stp::kMaxScan];
    stp::Where ew[stp::kMaxScan];
    uint32_t total = 0;
    const uint32_t n = read_entries(comp, es, total);
    for (uint32_t i = 0; i < n; ++i) ew[i & (stp::kMaxScan - 1)] = stp::Where::Unknown;
    stp::R0Out r0;
    stp::R0Plan plan;
    stp::Where own_w = stp::Where::Unknown;
    bool looked = false;
    uint8_t done = 0;
    const auto r0m = static_cast<stp::RouteMode>(g_r0.load(std::memory_order_relaxed));
    Side side;
    side.na = 0;
    side.b = 0;
    if (!add) {
        r0 = stp::decide_r0(es, n, total, key, idx, id);
        g_si_r0[static_cast<size_t>(r0.verdict)].fetch_add(1, std::memory_order_relaxed);
        if (r0.orphan) g_si_orphan[1].fetch_add(1, std::memory_order_relaxed);
        if (!r0.complete) g_si_incomplete.fetch_add(1, std::memory_order_relaxed);
        if (r0m != stp::RouteMode::Off) {
            // The lookups only when the native erase would not take this stage's own first entry.
            if (key_known && r0.first >= 0 && r0.verdict != stp::R0::Noop) {
                side = side_now(guest);
                looked = true;
                own_w = guest ? own_where_guest(side, proc) : where_in(side, id);
                for (uint32_t i = 0; i < n; ++i) {
                    if (stp::key_match(es[i], key, idx) && es[i].stage != id) {
                        ew[i & (stp::kMaxScan - 1)] = where_in(side, es[i].stage);
                    }
                }
            }
            plan = stp::plan_r0(es, n, total, key, idx, id, own_w, ew);
            if (!key_known && plan.act != stp::R0Act::Noop) {
                plan.act = stp::R0Act::Native;
                plan.why = stp::R0Why::KeyUnknown;
            }
            g_r0_act[static_cast<size_t>(plan.act)].fetch_add(1, std::memory_order_relaxed);
            g_r0_why[static_cast<size_t>(plan.why)].fetch_add(1, std::memory_order_relaxed);
            // Phase 2 after review F1: the swap is R0's only action (no register is ever changed).
            if (plan.act == stp::R0Act::Swap && r0m == stp::RouteMode::On) {
                if (g_r0_latched.load(std::memory_order_relaxed)) {
                    done = 2;
                    g_r0_not_latched.fetch_add(1, std::memory_order_relaxed);
                } else if (!lock_ok) {
                    done = 2;
                    g_r0_not_lock.fetch_add(1, std::memory_order_relaxed);
                } else {
                    const SwapRes sr = swap_entries(comp, total, plan.first, plan.own, id);
                    if (sr == SwapRes::Ok) {
                        done = 1;
                        g_r0_applied[static_cast<size_t>(plan.act)].fetch_add(1, std::memory_order_relaxed);
                    } else if (sr == SwapRes::Refused) {
                        done = 2;
                        g_r0_not_check.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        done = 3;
                        g_r0_fault.fetch_add(1, std::memory_order_relaxed);
                        g_r0_latched.store(true);
                    }
                }
            }
        }
    }
    if (!add && r0.orphan) {
        if (done == 1) {
            g_orphan_avoided.fetch_add(1, std::memory_order_relaxed);
        } else {
            g_orphan_left.fetch_add(1, std::memory_order_relaxed);
            // Left although an entry of the other manager is involved (R0 off / probe / refused, or not proven).
            if (plan.cross >= 0) g_orphan_left_cross.fetch_add(1, std::memory_order_relaxed);
        }
    }
    bool shop = key_known && key == stp::kShopKey;
    // Review ab B2: the shop entry of a stage appended again on an NPC = its dialogue is over: the record of the 2671
    // that used that stage goes (U2-c no longer routes on it).
    if (add && shop && server && actor) {
        if (g_accepted.close(rdv<uint32_t>(actor + kActor_Key), id)) g_accepted_closed.fetch_add(1, std::memory_order_relaxed);
    }
    for (uint32_t i = 0; i < n && !shop; ++i) shop = es[i].key_known && es[i].key == stp::kShopKey;
    if (shop) g_si_shop[add ? 1 : 0].fetch_add(1, std::memory_order_relaxed);
    // Minor (live phase 1): an NPC the ledger follows: its other operations (F42F6's exit, F42CF's entry...) too.
    if (!shop && !guest && actor && ledger_follows(rdv<uint32_t>(actor + kActor_Key))) {
        shop = true;
        g_si_followed.fetch_add(1, std::memory_order_relaxed);
    }
    if (guest && !add && n > 1) {
        // The guest's client: what entries share a key on one NPC, and whose (documented in s46-tenderos.md).
        if (r0.first >= 0 && r0.first != r0.own) g_guest_overlap.fetch_add(1, std::memory_order_relaxed);
    }
    // Lines: every shop NPC's operation (the ledger needs them; host); the others only when R0 would act or did, or the
    // native erase takes another stage's entry (orphans with their own budget), a few per window.
    const bool r0_line = plan.act == stp::R0Act::Swap || done != 0;
    if (!shop) {
        if (add || (r0.verdict == stp::R0::Noop && !r0_line)) return;
        const uint64_t now = p8_now_us();
        if (!((r0.orphan || r0_line) ? g_si_orphan_lim : g_si_lim).take(now)) {
            g_limited.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    const uint32_t npc = actor ? rdv<uint32_t>(actor + kActor_Key) : 0;
    const uint32_t owner = rdv<uint32_t>(rdp(stage + kStage_Data) + kData_Owner);
    // For the line: every entry's manager (and this stage's) when R0 did not look them up already.
    if (!looked) {
        side = side_now(guest);
        own_w = guest ? own_where_guest(side, proc) : where_in(side, id);
        for (uint32_t i = 0; i < n; ++i) {
            ew[i & (stp::kMaxScan - 1)] =
                (es[i].stage & stp::kStageMask) ? where_in(side, es[i].stage) : stp::Where::Unknown;
        }
    } else {
        for (uint32_t i = 0; i < n; ++i) {
            if (ew[i] == stp::Where::Unknown && (es[i].stage & stp::kStageMask)) {
                ew[i & (stp::kMaxScan - 1)] = where_in(side, es[i].stage);
            }
        }
    }
    const uint64_t now = p8_now_us();
    const DWORD tid = GetCurrentThreadId();
    g_si.push([&](stp::SiEvent& e) {
        e.qpc_us = now;
        e.tid = tid;
        e.add = add;
        e.server = server ? 1 : 0;
        e.client = client ? 1 : 0;
        e.r0 = static_cast<uint8_t>(r0.verdict);
        e.orphan = r0.orphan;
        e.complete = r0.complete;
        e.where = static_cast<uint8_t>(own_w);
        e.comp_ok = comp_ok;
        e.guest = guest ? 1 : 0;
        e.r0_mode = static_cast<uint8_t>(r0m);
        e.r0_act = static_cast<uint8_t>(plan.act);
        e.r0_why = static_cast<uint8_t>(plan.why);
        e.r0_done = done;
        e.first = static_cast<int8_t>(r0.first);
        e.own = static_cast<int8_t>(r0.own);
        e.lock_vt = lock_vt;
        e.cross = static_cast<int8_t>(plan.cross);
        e.npc = npc;
        e.key = key_known ? key : 0;
        e.owner = owner;
        e.n = total;
        e.read = n;
        e.idx = idx;
        e.stage = id;
        e.victim = r0.victim;
        e.comp = comp;
        e.actor = actor;
        volatile uint64_t* s = e.es;
        volatile uint32_t* k = e.ek;
        volatile uint16_t* x = e.ei;
        volatile uint8_t* f = e.ef;
        for (uint32_t i = 0; i < n; ++i) {
            s[i] = es[i].stage;
            k[i] = es[i].key;
            x[i] = es[i].idx;
            f[i] = static_cast<uint8_t>((es[i].key_known ? 1 : 0) | static_cast<uint8_t>(ew[i]) << 1);
        }
    });
}
void on_p2(safetyhook::Context& c) noexcept { on_si(c, false); }
void on_p2a(safetyhook::Context& c) noexcept { on_si(c, true); }

// P5 0x2B36FB3 (0x2B36EF0, the 2671): rax = the InteractionInfo of the asked index; r14 = the requester's component,
// r13 = the NPC.
void on_p5(safetyhook::Context& c) noexcept {
    if (!g_on[kP5].load(std::memory_order_acquire)) return;
    g_hits[kP5].fetch_add(1, std::memory_order_relaxed);
    const uint32_t key = rdv<uint32_t>(c.rax);
    if (!g_book.take((0x50ull << 56) | key, key == stp::kShopKey ? 4 : 2)) return;
    const Who3 w = who_of_actor(rdp(c.r14 + kComp_Actor));
    p::Event e;
    e.v[0] = c.rax;
    e.v[1] = key;
    e.v[2] = rdv<uint8_t>(c.rax + kInfo_Type) | static_cast<uint64_t>(rdv<uint8_t>(c.rax + kInfo_Mode)) << 8 |
             static_cast<uint64_t>(rdv<uint32_t>(c.rax + kInfo_Exempt)) << 16;
    e.v[3] = w.chr | static_cast<uint64_t>(rdv<uint32_t>(c.r13 + kActor_Key)) << 32;
    e.v[4] = static_cast<uint64_t>(w.w) | static_cast<uint64_t>(rdv<uint16_t>(c.rbp + kPre_Idx)) << 8;
    emit(kEP5, e);
}

// P6 0x2B3708C: [r15] = vf170's result (2 = pass), [r15+4] = its error.
void on_p6(safetyhook::Context& c) noexcept {
    if (!g_on[kP6].load(std::memory_order_acquire)) return;
    g_hits[kP6].fetch_add(1, std::memory_order_relaxed);
    const uint8_t res = rdv<uint8_t>(c.r15, 0xFF);
    const uint32_t err = rdv<uint32_t>(c.r15 + 4);
    const uint32_t npc = rdv<uint32_t>(c.r13 + kActor_Key);
    (res == 2 ? g_p6_ok : g_p6_fail).fetch_add(1, std::memory_order_relaxed);
    const uint16_t idx = rdv<uint16_t>(c.rbp + kPre_Idx);
    uint32_t key = 0;
    const bool key_ok = key_of_idx(idx, key);
    Who3 w;
    bool have_who = false;
    // Finding B: a 2671 of the shop key that passed vf170: the stage of the entry it used, per NPC, for U2-c.
    if (res == 2 && key_ok && key == stp::kShopKey && npc) {
        w = who_of_actor(rdp(c.r14 + kComp_Actor));
        have_who = true;
        uint64_t stage = 0;
        if (srd(c.rbp + kPre_EntryStage, stage)) {
            g_accepted.put({npc, stage, static_cast<uint8_t>(w.w), p8_now_us()});
            g_accepted_put.fetch_add(1, std::memory_order_relaxed);
            if (w.w == Who::User2) g_accepted_user2.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (!g_probe.load(std::memory_order_relaxed)) return;  // installed for U2-c only: the record, no lines
    if (res == 2 && !g_book.take((0x60ull << 56) | npc, 2)) return;
    if (!have_who) w = who_of_actor(rdp(c.r14 + kComp_Actor));
    p::Event e;
    e.v[0] = res | static_cast<uint64_t>(err) << 32;
    e.v[1] = w.chr | static_cast<uint64_t>(npc) << 32;
    e.v[2] = idx | static_cast<uint64_t>(key) << 32;
    e.v[3] = static_cast<uint64_t>(w.w) | static_cast<uint64_t>(w.user) << 32;
    emit(kEP6, e);
}

// ---------------------------------------------------------------- P3: the eErrNo names (the module's thread)

stp::ErrTable g_err;  // the thread's
size_t g_err_filled = 0;
void build_err_table() {
    std::vector<uint32_t> values(eerr::kNameCount, 0);
    size_t filled = 0;
    for (size_t i = 0; i < eerr::kNameCount; ++i) {
        uint32_t v = 0;
        if (game::fast_read(g_base + eerr::kNames[i].rva, &v, sizeof v) && v) {
            values[i] = v;
            ++filled;
        }
    }
    g_err.build(values);
    g_err_filled = filled;
}
std::string err_name(uint32_t v) {
    if (!v) return "0";
    const int i = g_err.find(v);
    return i >= 0 ? std::format("{} (0x{:08X})", eerr::kNames[i].name, v) : std::format("0x{:08X} (no eErrNo name)", v);
}

// ---------------------------------------------------------------- lines

const char* who_name(uint64_t w) { return shp::who_name(static_cast<Who>(w % 3)); }
std::string caller_text(uint32_t ret) {
    if (const stp::Caller* k = stp::caller_of(ret)) return k->what;
    return ret ? std::format("caller ret 0x{:X}", ret) : std::string("caller ?");
}

std::string line_of(const p::Event& e) {
    const uint64_t* v = e.v;
    const std::string when = p8_when(e.qpc_us);
    switch (e.kind) {
        case kEP1: {
            const bool server = (v[4] >> 40) & 1;
            const auto verdict = static_cast<stp::P1>((v[4] >> 48) & 0xFF);
            const auto mis = static_cast<stp::MgrIs>((v[4] >> 56) & 0xFF);
            const bool u2 = static_cast<Who>((v[4] >> 32) & 0xFF) == Who::User2;
            std::string tail;
            if (server && u2) {
                tail = std::format(" | user 2: against mgr2's current 0x{:X} (what U2-c would give) it would {}", v[7],
                                   v[7] == v[0] && v[0] ? "PASS" : "FAIL");
            }
            return std::format(
                "ShopTradeProbe [P1] {} T{} | {} 0x21F2780 for {} (character 0x{:08X}, user 0x{:08X}), {}: NPC "
                "0x{:08X} linked to stage 0x{:X} ({}) vs the current stage 0x{:X} of {} -> {} | mgrH current "
                "0x{:X}, mgr2 current 0x{:X} | store comp vtable exe+0x{:X}{}",
                when, e.tid, server ? "SERVER" : "client", who_name((v[4] >> 32) & 0xFF), static_cast<uint32_t>(v[3] >> 32),
                static_cast<uint32_t>(v[4]), caller_text(static_cast<uint32_t>(v[5])), static_cast<uint32_t>(v[3]),
                v[0], server ? stp::where_name(static_cast<stp::Where>(v[8])) : "not looked up", v[1],
                v[2] ? std::format("{} 0x{:X}", stp::mgr_name(mis), v[2])
                     : std::string("a manager P1b did not seal (unknown)"),
                stp::p1_name(verdict), v[6], v[7], v[9], tail);
        }
        case kEP1b: {
            const auto mis = static_cast<stp::MgrIs>((v[3] >> 56) & 0xFF);
            return std::format("ShopTradeProbe [P1b] {} T{} | {} 0x21F2780 for {} (character 0x{:08X}, user "
                               "0x{:08X}), {}: NPC 0x{:08X}: {} 0x{:X} has NO current stage (r12 0x{:X}) -> "
                               "eErrNoInvalidStageInfo | mgr2 current 0x{:X}",
                               when, e.tid, (v[3] >> 40) & 1 ? "SERVER" : "client", who_name((v[3] >> 32) & 0xFF),
                               static_cast<uint32_t>(v[2] >> 32), static_cast<uint32_t>(v[3]),
                               caller_text(static_cast<uint32_t>(v[4])), static_cast<uint32_t>(v[2]),
                               stp::mgr_name(mis), v[1], v[0], v[5]);
        }
        case kEU2c: {
            const auto verdict = static_cast<stp::U2c>((v[4] >> 40) & 0xFF);
            const bool route = verdict == stp::U2c::WouldRoute;
            const bool routed = (v[4] >> 48) & 1;
            const auto bit = [&](int b) { return ((v[4] >> b) & 1) ? "yes" : "no"; };
            return std::format("ShopTradeProbe [{}] {} T{} | U2-c 0x21F28A1 for {} (character 0x{:08X}, user "
                               "0x{:08X}), {}: NPC 0x{:08X} | vf48 gave 0x{:X} ({}, current 0x{:X}); mgrH 0x{:X}, "
                               "mgr2 0x{:X} (current 0x{:X}, alive in mgr2 {}, +0x6A0 {}) | user 2's Troc {}, host "
                               "field {}, U-1 armed {}, leaving {}, NPC entries read {}: a shop entry alive in mgr2 "
                               "{}, in mgrH {}{} -> {} | {}",
                               routed ? "ROUTED to mgr2" : route ? "would route to mgr2" : "U2-c", when, e.tid,
                               who_name((v[4] >> 32) & 0xFF), static_cast<uint32_t>(v[3] >> 32),
                               static_cast<uint32_t>(v[4]), caller_text(static_cast<uint32_t>(v[7])),
                               static_cast<uint32_t>(v[3]), v[0], stp::mgr_name(stp::mgr_is(v[0], v[1], v[2])), v[6],
                               v[1], v[2], v[5], bit(56), (v[4] >> 57) & 0x7F, bit(49), bit(50), bit(51), bit(52),
                               bit(53), bit(54), bit(55),
                               (v[9] >> 8) & 1
                                   ? std::format("; user 2's 2671 (passed vf170) used stage 0x{:X} ({})", v[8],
                                                 stp::where_name(static_cast<stp::Where>(v[9] & 0xFF)))
                                   : std::string("; no open 2671 of user 2 on it (none, closed, or > 10 min)"),
                               stp::u2c_name(verdict),
                               routed ? "RAX := mgr2 (user2_shop_trade_route=1)"
                                      : route ? "probe: RAX unchanged" : "RAX unchanged");
        }
        case kEP5:
            return std::format("ShopTradeProbe [P5] {} T{} | 2671 by {} (character 0x{:08X}) on NPC 0x{:08X}: index {} "
                               "-> InteractionInfo 0x{:X} key 0x{:08X}: +0x12 (type) {} +0x13 (busy mode) {} +0xBC "
                               "(players exempt) 0x{:08X} (u8 {}) -> {}",
                               when, e.tid, who_name(v[4] & 0xFF), static_cast<uint32_t>(v[3]),
                               static_cast<uint32_t>(v[3] >> 32), (v[4] >> 8) & 0xFFFF, v[0],
                               static_cast<uint32_t>(v[1]), v[2] & 0xFF, (v[2] >> 8) & 0xFF,
                               static_cast<uint32_t>(v[2] >> 16), (v[2] >> 16) & 0xFF,
                               ((v[2] >> 16) & 0xFF) && ((v[2] >> 8) & 0xFF) != 3
                                   ? "players never exclusive (both may talk at once)"
                                   : ((v[2] >> 8) & 0xFF) == 0 ? "one at a time (the second gets eErrNoAlreadyPreemptedActor)"
                                                               : "exclusive per key (mode 1) / per key and p5 (mode 2)");
        case kEP6: {
            const auto res = static_cast<uint8_t>(v[0] & 0xFF);
            return std::format("ShopTradeProbe [P6] {} T{} | 2671 by {} (character 0x{:08X}) on NPC 0x{:08X} "
                               "interaction 0x{:08X} (index {}): vf170 0x211FCB0 result {} -> {}{}",
                               when, e.tid, who_name(v[3] & 0xFF), static_cast<uint32_t>(v[1]),
                               static_cast<uint32_t>(v[1] >> 32), static_cast<uint32_t>(v[2] >> 32), v[2] & 0xFFFF, res,
                               res == 2 ? "passes (the reservation follows)" : "REFUSED before the reservation, error ",
                               res == 2 ? std::string() : err_name(static_cast<uint32_t>(v[0] >> 32)));
        }
        case kEOut: {
            const auto op = static_cast<uint16_t>(v[0] & 0xFFFF);
            if (op == u2sh::kOpPreemptionFail) {
                return std::format("ShopTradeProbe [3000 to user 2] {} T{} | requester 0x{:08X} NPC 0x{:08X} "
                                   "interaction 0x{:08X}{}: error {} | {}",
                                   when, e.tid, static_cast<uint32_t>(v[1]), static_cast<uint32_t>(v[2]),
                                   static_cast<uint32_t>(v[3]),
                                   static_cast<uint32_t>(v[3]) == stp::kShopKey ? " (the SHOP interaction)" : "",
                                   err_name(static_cast<uint32_t>(v[4])), (v[0] >> 16) & 1 ? "queued" : "NOT queued");
            }
            return std::format("ShopTradeProbe [Nak to user 2] {} T{} | Nak 1013 of Req {}: error {} | {}", when, e.tid,
                               v[1], err_name(static_cast<uint32_t>(v[2])), (v[0] >> 16) & 1 ? "queued" : "NOT queued");
        }
        default:
            return std::format("ShopTradeProbe: event {}?", e.kind);
    }
}

std::string list_text(const stp::SiEvent& e) {
    std::string s;
    for (uint32_t i = 0; i < e.read; ++i) {
        uint32_t key = e.ek[i];
        bool known = e.ef[i] & 1;
        if (!known) known = key_of_idx(e.ei[i], key);  // loaded since
        const stp::Where w = e.where_of(i);
        s += std::format("{}[{}] {} idx {} stage 0x{:X}{}", i ? ", " : "", i,
                         known ? std::format("key 0x{:08X}", key) : std::string("key ?"), e.ei[i], e.es[i],
                         (e.es[i] & stp::kStageMask) && w != stp::Where::Unknown
                             ? std::format(" ({})", stp::where_name(w, e.guest != 0))
                             : std::string());
    }
    if (e.n > e.read) s += std::format(", ... {} more not read", e.n - e.read);
    return s.empty() ? std::string("(empty)") : s;
}

stp::Ledger g_ledger;  // the thread's
// The ledger's NPC keys for the hooks (ledger_follows), after each drain and check.
void publish_ledger_keys() {
    size_t i = 0;
    g_ledger.each_key([&](uint32_t k) {
        if (i < kLedgerKeys) g_ledger_keys[i++].store(k, std::memory_order_relaxed);
    });
    for (; i < kLedgerKeys; ++i) g_ledger_keys[i].store(0, std::memory_order_relaxed);
}

// R0's part of a P2 line (phase 2): what shop_interaction_own_delete decided and did.
std::string r0_text(const stp::SiEvent& e) {
    const auto mode = static_cast<stp::RouteMode>(e.r0_mode);
    if (mode == stp::RouteMode::Off) return std::string();
    const stp::R0Plan p = e.plan();
    std::string what;
    if (p.act == stp::R0Act::Swap) {
        what = std::format("swap entries {} and {} (0x30 B each), then the native erase takes this stage's own", p.first,
                           p.own);
    } else {
        what = std::format("{} ({})", stp::r0_act_name(p.act), stp::r0_why_name(p.why));
    }
    const char* state = e.r0_done == 1   ? "APPLIED"
                        : e.r0_done == 2 ? "NOT applied (latched native, the lock not proven, or a check refused it)"
                        : e.r0_done == 3 ? "NOT applied: a WRITE FAULTED (R0 latched native)"
                        : mode == stp::RouteMode::Probe && p.act == stp::R0Act::Swap ? "probe: would"
                                                                                    : "";
    return std::format(" | R0 (shop_interaction_own_delete={}): {}{}{}{} | lock vtable exe+0x{:X}{}", stp::mode_name(mode),
                       state, *state ? " " : "", what,
                       p.cross >= 0 ? std::format("; entry {} is another manager's", p.cross) : std::string(), e.lock_vt,
                       stp::is_rw_lock_vtable(e.lock_vt) ? " (WindowsRWLock)" : " (NOT a WindowsRWLock: R0 never writes)");
}

void si_line(const stp::SiEvent& e) {
    const std::string when = p8_when(e.qpc_us);
    const auto wh = static_cast<stp::Where>(e.where);
    const bool g = e.guest != 0;
    const char* side = g ? "GUEST client" : "SERVER";
    const std::string head = std::format(
        "NPC 0x{:08X} (comp 0x{:X}{}) key 0x{:08X} idx {} | stage 0x{:X} owner 0x{:08X} ({}) | {} entries before: {}",
        e.npc, e.comp, e.comp_ok ? "" : ", NOT the actor's component", e.key, e.idx, e.stage, e.owner,
        stp::where_name(wh, g), e.n, list_text(e));
    if (e.add) {
        log::info("ShopTradeProbe [P2a add] {} T{} | {} SetInteraction entry (0xE402CC0 appends) | {}", when, e.tid, side,
                  head);
        return;
    }
    const auto v = static_cast<stp::R0>(e.r0);
    const std::string r0 = std::format("i_first {} i_own {} -> [{}]{}{}", e.first, e.own, stp::r0_name(v),
                                       e.complete ? "" : " (the vector was longer than what was read)", r0_text(e));
    const stp::R0Plan p = e.plan();
    const bool avoided = e.r0_done == 1 && p.act == stp::R0Act::Swap;
    if (e.orphan && !avoided) {
        log::warn("ShopTradeProbe [P2 erase] {} T{} | {} SetInteraction exit (0xE404A10 erases the FIRST match) | "
                  "{} | {} | [would orphan]: the native erase takes entry {} of ANOTHER stage 0x{:X}{}",
                  when, e.tid, side, head, r0, e.first, e.victim,
                  e.own >= 0 ? " and leaves this stage's own entry behind" : "");
    } else {
        log::info("ShopTradeProbe [P2 erase] {} T{} | {} SetInteraction exit (0xE404A10 erases the FIRST match) | "
                  "{} | {}{}",
                  when, e.tid, side, head, r0,
                  avoided ? " | [orphan avoided]: R0 made the erase take this stage's own entry" : "");
    }
}

void drain() {
    p::Event e;
    while (g_ring.pop(e)) {
        if (e.kind >= kKindCount) continue;
        if (g_lines.fetch_add(1) < kMaxLines) log::info("{}", line_of(e));
    }
    stp::SiEvent s;
    while (g_si.pop(s)) {
        const bool line = s.orphan || s.r0_done || s.r0_act == static_cast<uint8_t>(stp::R0Act::Swap)
                              ? g_orphan_line_lim.take(s.qpc_us)
                                   : g_lines.load() < kMaxLines && g_shop_line_lim.take(s.qpc_us);
        if (line) {
            g_lines.fetch_add(1);
            si_line(s);
        } else {
            g_limited.fetch_add(1, std::memory_order_relaxed);
        }
        // The ledger: the shop NPCs' server operations.
        std::vector<stp::Entry> before = s.list();
        for (stp::Entry& x : before) {
            if (!x.key_known) x.key_known = key_of_idx(x.idx, x.key);
        }
        stp::Entry op{s.idx, s.key != 0, s.key, s.stage};
        if (!op.key_known) op.key_known = key_of_idx(op.idx, op.key);
        const bool shop = (op.key_known && op.key == stp::kShopKey) || stp::has_shop(before) ||
                          g_ledger.find(s.npc) != nullptr;  // minor: a followed NPC's operations without the shop key
        if (shop && s.npc && s.comp_ok && !s.guest) {
            const stp::R0Plan plan = s.plan();
            g_ledger.apply(s.npc, s.actor, s.comp, before, s.add != 0, op, s.r0_done == 1 ? &plan : nullptr);
        }
    }
    publish_ledger_keys();
}

// Every second: each ledger NPC's live entries (SEH reads; no lock: a torn read retried once) and their stages.
void ledger_check() {
    g_ledger_checks.fetch_add(1);
    // Review B1: a manager that changed since the last check (a field change, user 2 leaving / arriving) makes this
    // round "unknown" for every stage: no orphan is counted on a lookup in a manager that was just replaced. The
    // ledger only knows mgrH and mgr2; a stage of any third manager reads "in neither" (limitation, in the document).
    static uintptr_t last_h = 0, last_2 = 0;
    const uintptr_t h = g_mgr_h.load(), u = g_mgr_2.load();
    const bool changed = h != last_h || u != last_2;
    last_h = h;
    last_2 = u;
    const auto read_live = [](const stp::LedgerNpc& n, std::vector<stp::Entry>& out) -> bool {
        // Still the same NPC with the same component (it may have been removed and its memory reused).
        if (rdv<uint32_t>(n.actor + kActor_Key) != n.key ||
            rdp(rdp(n.actor + kActor_Comps) + kComps_Interaction) != n.comp) {
            return false;
        }
        for (int attempt = 0; attempt < 2; ++attempt) {
            stp::Entry es[stp::kMaxScan];
            uint32_t total = 0, total2 = 0;
            const uint32_t got = read_entries(n.comp, es, total);
            srd(n.comp + stp::kComp_Count, total2);
            if (total == total2 && got == std::min<uint32_t>(total, stp::kMaxScan)) {
                out.assign(es, es + got);
                return true;
            }
        }
        out = n.expected;  // unreadable twice: keep the expectation (no orphan decided on a torn read)
        return true;
    };
    const auto alive = [changed](uint64_t stage) { return changed ? stp::Where::Unknown : where_of(stage); };
    const auto report = [](const stp::LedgerNpc& n, const stp::Entry& e, stp::Where) {
        log::warn("ShopTradeProbe [orphan] NPC 0x{:08X} (comp 0x{:X}): its entry key 0x{:08X} idx {} names stage 0x{:X}, "
                  "alive neither in mgrH 0x{:X} nor in mgr2 0x{:X} for {} checks (1 s apart): ABORT criterion of S46 "
                  "(any 2671 on it takes this entry and gets a 3000)",
                  n.key, n.comp, e.key, e.idx, e.stage, g_mgr_h.load(), g_mgr_2.load(), stp::kOrphanChecks);
    };
    g_ledger_mismatch.fetch_add(g_ledger.check(read_live, alive, report));
    publish_ledger_keys();
}

void summary() {
    const auto in = [](Pt p) { return g_on[p].load() ? "in" : "NOT in"; };
    const auto p1 = [](int server, Who w) {
        const auto& a = g_p1[server][static_cast<size_t>(w)];
        return std::format("pass {} mismatch {} no-link {}", a[0].load(), a[1].load(), a[2].load());
    };
    std::string u2c;
    for (size_t k = 0; k < static_cast<size_t>(stp::U2c::Count); ++k) {
        if (const uint64_t n = g_u2c[k].load()) u2c += std::format(" [{}] {};", stp::u2c_name(static_cast<stp::U2c>(k)), n);
    }
    const bool active = g_r0.load() == static_cast<uint8_t>(stp::RouteMode::On) ||
                        g_route.load() == static_cast<uint8_t>(stp::RouteMode::On);
    log::info("ShopTradeProbe summary ({}): P1 {} P1b {} U2-c {} P2 {} P2a {} P5 {} P6 {} | hits P1 {} P1b {} U2-c "
              "{} P2 {} P2a {} P5 {} P6 {} | P1 server: host [{}] user 2 [{}] unknown [{}]; client: host [{}]; user 2 "
              "against mgr2's current: would pass {} / fail {}; P1b no current {}; P1 without P1b's manager {} | U2-c:{} "
              "| shop 2671s that passed vf170 recorded {} (user 2's {}; closed with their dialogue {}) | SetInteraction {}: "
              "entries {} exits {} (shop NPCs: entries {} exits {}; other keys of the ledger's NPCs {}); R0 noop {} swap {} "
              "sentinel {}; [would orphan] {}; "
              "incomplete {}; component mismatch {} | host client's {} unknown processor {} | P6 refused {} passed {} | "
              "to user 2: 3000 {} Nak {} | ledger: NPCs {} checks {} orphans {} gone {} full {} live!=expected {} | "
              "eErrNo names {} of {} filled ({} usable, {} ambiguous) | managers mgrH 0x{:X} mgr2 0x{:X} | limited {} "
              "lost {}/{}",
              active ? "R0 / U2-c ACTIVE: see the R0 / U2-c line" : "READ ONLY", in(kP1), in(kP1b), in(kU2c), in(kP2),
              in(kP2a), in(kP5), in(kP6), g_hits[kP1].load(), g_hits[kP1b].load(),
              g_hits[kU2c].load(), g_hits[kP2].load(), g_hits[kP2a].load(), g_hits[kP5].load(), g_hits[kP6].load(),
              p1(1, Who::Host), p1(1, Who::User2), p1(1, Who::Unknown), p1(0, Who::Host), g_p1_mgr2_pass.load(),
              g_p1_mgr2_fail.load(), g_p1b_none.load(), g_p1_unsealed.load(), u2c.empty() ? " none" : u2c,
              g_accepted_put.load(), g_accepted_user2.load(), g_accepted_closed.load(),
              g_guest.load() ? "guest client" : "server", g_si_ops[1][1].load(),
              g_si_ops[1][0].load(), g_si_shop[1].load(), g_si_shop[0].load(), g_si_followed.load(), g_si_r0[0].load(),
              g_si_r0[1].load(),
              g_si_r0[2].load(), g_si_orphan[1].load(), g_si_incomplete.load(), g_si_comp_bad.load(), g_si_client.load(),
              g_si_unknown_proc.load(), g_p6_fail.load(), g_p6_ok.load(), g_out3000.load(), g_out_nak.load(),
              g_ledger.size(), g_ledger_checks.load(), g_ledger.orphans(), g_ledger.gone(), g_ledger.full(),
              g_ledger_mismatch.load(), g_err_filled, eerr::kNameCount, g_err.rows.size(), g_err.dup, g_mgr_h.load(),
              g_mgr_2.load(), g_limited.load(), g_ring.dropped(), g_si.dropped());
    // Phase 2: R0 and U2-c.
    const auto r0m = static_cast<stp::RouteMode>(g_r0.load());
    const auto rtm = static_cast<stp::RouteMode>(g_route.load());
    if (r0m == stp::RouteMode::Off && rtm == stp::RouteMode::Off) return;
    std::string why;
    for (size_t k = 0; k < static_cast<size_t>(stp::R0Why::Count); ++k) {
        if (const uint64_t n = g_r0_why[k].load()) why += std::format(" [{}] {};", stp::r0_why_name(static_cast<stp::R0Why>(k)), n);
    }
    const auto act = [](const std::atomic<uint64_t>* a) {
        return std::format("noop {} native {} swap {}", a[static_cast<size_t>(stp::R0Act::Noop)].load(),
                           a[static_cast<size_t>(stp::R0Act::Native)].load(), a[static_cast<size_t>(stp::R0Act::Swap)].load());
    };
    log::info("ShopTradeProbe R0 / U2-c summary ({}): shop_interaction_own_delete={} | exits planned: {} | why:{} | "
              "applied: swap {} | NOT applied: latched {}, lock not proven {}, a check refused {}, WRITE FAULTS {} | "
              "latched native {} | [would orphan] exits {}: avoided by R0 {}, left {} (of them with another manager's "
              "entry {}) | guest overlaps (another stage's entry first with the key) {} | "
              "user2_shop_trade_route={}: route to mgr2 decided {} (by the stage of user 2's 2671 that passed vf170 {}), RAX := mgr2 {}",
              g_guest.load() ? "guest" : "host", stp::mode_name(r0m), act(g_r0_act), why.empty() ? " none" : why,
              g_r0_applied[static_cast<size_t>(stp::R0Act::Swap)].load(), g_r0_not_latched.load(), g_r0_not_lock.load(),
              g_r0_not_check.load(), g_r0_fault.load(),
              g_r0_latched.load() ? "YES" : "no", g_si_orphan[1].load(),
              g_orphan_avoided.load(), g_orphan_left.load(), g_orphan_left_cross.load(), g_guest_overlap.load(),
              stp::mode_name(rtm),
              g_u2c[static_cast<size_t>(stp::U2c::WouldRoute)].load(), g_u2c_by_record.load(), g_u2c_routed.load());
}

// ---------------------------------------------------------------- install

bool bytes_at(uint32_t rva, std::span<const uint8_t> expected) {
    std::vector<uint8_t> actual(expected.size());
    return game::safe_read(g_base + rva, actual.data(), actual.size()) &&
           std::equal(actual.begin(), actual.end(), expected.begin());
}
bool read_pdata(std::vector<RUNTIME_FUNCTION>& out) {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!game::safe_read(g_base, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
        dos.e_lfanew > 0x100000 || !game::safe_read(g_base + static_cast<uint32_t>(dos.e_lfanew), &nt, sizeof nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    const auto pd = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!pd.VirtualAddress || !pd.Size || pd.Size % sizeof(RUNTIME_FUNCTION) || pd.Size > 0x1000000) return false;
    out.resize(pd.Size / sizeof(RUNTIME_FUNCTION));
    return game::safe_read(g_base + pd.VirtualAddress, out.data(), pd.Size);
}
bool has_function(const std::vector<RUNTIME_FUNCTION>& pdata, uint32_t begin, uint32_t end) {
    return std::any_of(pdata.begin(), pdata.end(),
                       [&](const RUNTIME_FUNCTION& r) { return r.BeginAddress == begin && r.EndAddress == end; });
}

void install(std::span<const gad::Section> secs, const std::vector<RUNTIME_FUNCTION>& pdata) {
    std::string on, refused;
    for (const PointDef& d : kDefs) {
        const bool want = d.want == kWantRoute          ? g_route.load() != 0
                          : d.want == kWantP2           ? g_probe.load() || g_r0.load() != 0
                          : d.want == kWantProbeOrRoute ? g_probe.load() || g_route.load() != 0
                                                        : g_probe.load();
        if (!want) continue;
        std::string why;
        if (!has_function(pdata, d.fn_begin, d.fn_end)) {
            why = std::format(".pdata [0x{:X},0x{:X}) not found", d.fn_begin, d.fn_end);
        } else if (d.rva < d.fn_begin || d.rva + d.patch > d.fn_end) {
            why = "the patch is not inside its function";
        } else if (!gad::in_real_code(secs, d.rva, static_cast<uint32_t>(d.sig.size())) || !bytes_at(d.rva, d.sig)) {
            why = std::format("its {}-byte signature is not build 2976's .rsrc code", d.sig.size());
        } else {
            for (const Ctx& c : d.ctx) {
                if (!gad::in_real_code(secs, c.rva, static_cast<uint32_t>(c.bytes.size())) || !bytes_at(c.rva, c.bytes)) {
                    why = std::format("the code it relies on at exe+0x{:X} ({} bytes) is not build 2976's", c.rva,
                                      c.bytes.size());
                    break;
                }
            }
        }
        if (why.empty() && !p8_install_mid(g_base, secs, d.rva, d.patch, d.sig, d.fn, g_hooks[d.pt], "ShopTradeProbe",
                                           kPtName[d.pt])) {
            why = "p8_install_mid refused it (see the line above)";
        }
        g_on[d.pt].store(why.empty(), std::memory_order_release);
        if (why.empty()) {
            on += std::format(" {} ({} B);", kPtName[d.pt], d.patch);
        } else {
            refused += std::format(" {} ({});", kPtName[d.pt], why);
            log::warn("ShopTradeProbe: {} NOT installed: {}", kPtName[d.pt], why);
        }
    }
    const auto r0m = static_cast<stp::RouteMode>(g_r0.load());
    const auto rtm = static_cast<stp::RouteMode>(g_route.load());
    log::info("ShopTradeProbe ({}; shop_trade_probe={}, user2_shop_trade_route={}, shop_interaction_own_delete={}; {}) | "
              "hooked:{} | refused:{} | ledger {} | eErrNo names: {} of {} variables filled",
              g_guest.load() ? "GUEST client, R0 only" : "host server", g_probe.load() ? 1 : 0, stp::mode_name(rtm),
              stp::mode_name(r0m),
              r0m == stp::RouteMode::On || rtm == stp::RouteMode::On
                  ? std::format("ACTIVE: {}{}{}",
                                r0m == stp::RouteMode::On ? "R0 swaps two 0x30-byte entries of the shop key at P2, only "
                                                            "across managers"
                                                          : "",
                                r0m == stp::RouteMode::On && rtm == stp::RouteMode::On ? "; " : "",
                                rtm == stp::RouteMode::On ? "U2-c sets RAX := mgr2 for user 2's own trade check" : "")
                  : std::string("READ ONLY: nothing written, no game call, no register changed"),
              on.empty() ? " none" : on, refused.empty() ? " none" : refused,
              g_probe.load() ? "on (every second)" : "off", g_err_filled, eerr::kNameCount);
    // Review ab B3: U2-c in without P6 (its record): never routed during the shop dialogue (the NPC has no entries).
    if (g_route.load() != 0 && g_on[kU2c].load() && !g_on[kP6].load()) {
        log::warn("ShopTradeProbe: user2_shop_trade_route={} with U2-c 0x21F28A1 in but P6 0x2B3708C NOT in: U2-c "
                  "without P6: no route during the shop dialogue (it decides by the NPC's entries only)",
                  stp::mode_name(static_cast<stp::RouteMode>(g_route.load())));
    }
    if (r0m != stp::RouteMode::Off && !g_on[kP2].load()) {
        log::warn("ShopTradeProbe: shop_interaction_own_delete={} but P2 0x2369862 is NOT in: R0 does nothing",
                  stp::mode_name(r0m));
    }
    // Review F12: R0 writing without the ledger: no '[orphan]' would ever be reported.
    if (r0m == stp::RouteMode::On && !g_probe.load() && !g_guest.load()) {
        log::warn("ShopTradeProbe: shop_interaction_own_delete=1 with shop_trade_probe=0: R0 is ACTIVE WITHOUT the "
                  "ledger (no '[orphan]' lines); use shop_trade_probe=1 for its tests");
    }
}

void refresh_managers() {
    const uintptr_t h = user2_stages_host_manager(), u = user2_stages_user2_manager();
    g_mgr_h.store(is_server_mgr(h) ? h : 0, std::memory_order_relaxed);
    g_mgr_2.store(is_server_mgr(u) ? u : 0, std::memory_order_relaxed);
}

DWORD WINAPI thread_main(LPVOID) {
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::vector<gad::Section> secs;
    std::vector<RUNTIME_FUNCTION> pdata;
    if (g_base != off::kImageBase || !game::safe_read(g_base, &dos, sizeof dos) ||
        !game::safe_read(g_base + dos.e_lfanew, &nt, sizeof nt) || nt.FileHeader.TimeDateStamp != off::kPeTimestamp ||
        !p8_exe_sections(g_base, secs) || !read_pdata(pdata)) {
        log::warn("ShopTradeProbe: not build 1.0.0.2976 (base / PE TimeDateStamp) or unreadable sections / .pdata: "
                  "NOTHING installed (the 3000 / Nak taps still count)");
    } else {
        for (int i = 0; i < 1200; ++i) {  // WorldSystem (up to 10 minutes): the game's code and its error table in place
            uintptr_t world = 0;
            if (game::safe_read(g_base + off::kWorldSystemSlot, &world, sizeof world) && world) break;
            Sleep(500);
        }
        try {
            build_err_table();
            refresh_managers();
            install(secs, pdata);
        } catch (...) {
            log::error("ShopTradeProbe: install failed (exception); hooks may be partial");
        }
    }
    g_settled.store(true);
    for (uint64_t tick = 1;; ++tick) {
        Sleep(250);
        try {
            drain();
            if (tick % 4 == 0) {
                refresh_managers();
                if (g_probe.load()) ledger_check();
                drain();  // what the check logged meanwhile
            }
            // The eErrNo variables are filled by the game's registration: read again until (nearly) all are.
            if (tick % 40 == 0 && g_err_filled * 10 < eerr::kNameCount * 9 && tick < 40 * 30) build_err_table();
            // Minor (live phase 1): also right when user 2 leaves (the host's log can end with the game, without a
            // last periodic summary).
            uintptr_t user2 = 0;
            const bool here = server_user2(user2) && user2;
            static bool was_here = false;
            const bool left = was_here && !here;
            was_here = here;
            if (tick % 240 == 0 || left) summary();
        } catch (...) {
        }
    }
}

}  // namespace

void shop_trade_probe_start(bool probe, const std::string& route, const std::string& own_delete) {
    const stp::RouteKey rk = stp::parse_route(route);
    if (rk.bad) log::warn("ShopTradeProbe: user2_shop_trade_route={} is not 0, probe or 1: off", route);
    const stp::RouteKey r0 = stp::parse_route(own_delete);
    if (r0.bad) log::warn("ShopTradeProbe: shop_interaction_own_delete={} is not 0, probe or 1: off", own_delete);
    g_probe.store(probe);
    g_route.store(static_cast<uint8_t>(rk.mode));
    g_r0.store(static_cast<uint8_t>(r0.mode));
    g_guest.store(false);
    if (!probe && rk.mode == stp::RouteMode::Off && r0.mode == stp::RouteMode::Off) return;
    if (g_started.exchange(true)) return;
    CloseHandle(CreateThread(nullptr, 0, thread_main, nullptr, 0, nullptr));
}

void shop_trade_probe_start_guest(const std::string& own_delete) {
    const stp::RouteKey r0 = stp::parse_route(own_delete);
    if (r0.bad) log::warn("ShopTradeProbe: shop_interaction_own_delete={} is not 0, probe or 1: off", own_delete);
    if (r0.mode == stp::RouteMode::Off) return;
    // Review F4: nothing of the client side is measured yet (its per-stage registry vf120 / vf128, [comp+8] == actor):
    // on the guest '1' is held as probe.
    stp::RouteMode mode = r0.mode;
    if (mode == stp::RouteMode::On) {
        log::warn("ShopTradeProbe: shop_interaction_own_delete=1 is NOT applied on the guest in phase 2: held as probe "
                  "(nothing of the client side is measured; docs/research/s25/s46-tenderos.md 8.3)");
        mode = stp::RouteMode::Probe;
    }
    g_probe.store(false);
    g_route.store(0);
    g_r0.store(static_cast<uint8_t>(mode));
    g_guest.store(true);
    if (g_started.exchange(true)) return;
    CloseHandle(CreateThread(nullptr, 0, thread_main, nullptr, 0, nullptr));
}

bool shop_trade_probe_active() noexcept { return g_probe.load(std::memory_order_relaxed); }

void shop_trade_probe_on_out(uint16_t opcode, std::span<const uint8_t> payload, bool to_user2, bool pushed) noexcept {
    if (!g_probe.load(std::memory_order_relaxed) || !to_user2) return;
    p::Event e;
    if (opcode == u2sh::kOpPreemptionFail) {
        const u2sh::StepAck a = u2sh::parse_step_ack(payload, opcode);
        if (!a.ok) return;
        g_out3000.fetch_add(1, std::memory_order_relaxed);
        e.v[1] = a.requester;
        e.v[2] = a.npc;
        e.v[3] = a.interaction;
        e.v[4] = a.native_error;
    } else if (opcode == u2sh::kOpResetAck) {
        // Review ab B2: user 2's interaction with the NPC was reset (the shop closed): its record goes.
        const u2sh::StepAck a = u2sh::parse_step_ack(payload, opcode);
        if (a.ok && a.interaction == u2sh::kShopInteraction && pushed && g_accepted.close(a.npc)) {
            g_accepted_closed.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    } else if (opcode == u2sh::kOpNak) {
        const uint16_t req = u2sh::nak_req_opcode(payload);
        if (!req) return;
        g_out_nak.fetch_add(1, std::memory_order_relaxed);
        e.v[1] = req;
        e.v[2] = u2sh::nak_error(payload);
    } else {
        return;
    }
    e.v[0] = opcode | static_cast<uint64_t>(pushed) << 16;
    emit(kEOut, e);
}

}  // namespace cdmp::session2
