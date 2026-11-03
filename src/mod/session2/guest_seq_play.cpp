#include "mod/session2/guest_seq_play.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <format>
#include <span>
#include <string>
#include <vector>

#include "mod/config.h"
#include "mod/game/game_fault.h"
#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/freeze_watchdog.h"
#include "mod/session2/guest.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/guest_sched_bind.h"
#include "mod/session2/guest_seq_present.h"
#include "mod/session2/guest_seq_play_logic.h"
#include "mod/session2/guest_x_veto.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"

namespace cdmp::session2 {
namespace {

namespace p = p8p;
namespace off = game::build_2976;
using gsp::Skip;

// ---------------------------------------------------------------- the code (build 2976, all in .rsrc)
//
// Verified with capstone on the read-only PE (p8-impl-escenas-play.md §1):
//   0x8B6060 (unwind 0x8B6060-0x8B6906), its only caller 0xAD277F in 0xAD2380 (once per main loop turn,
//   xmm1 = [[r13+0x60]+0x64], the frame's dt):
//   0x8B6076 lea rbp,[rax-0x5f]                 the frame
//   0x8B608F vmovaps xmm6,xmm1                  dt; xmm6 is not written again before 0x8B6144 (callee-saved)
//   0x8B60AF mov rcx,[rdi+0x50] ; test ; je ; test byte [rcx+0x5e],0x10 ; je   (the local actor, logged in)
//   0x8B60C6 lea rdx,[rbp-0x49] ; call 0x17FA6C0  the FieldAttacher: [rbp-0x41] = Q (held until 0x8B618F)
//   0x8B6102 mov rcx,[rbp-0x41] ; mov rax,[rcx] ; call [rax+0x48]   vf48 (0x9A5BE0) -> M in RAX
//   0x8B613D mov r8,[rdi+0x80]
//   0x8B6144 movzx r8d,byte [r8+0x50]           THE POINT (5 bytes; one instruction, no branch into it)
//   0x8B6149 mov rdx,[rbp-0x41] ; mov rcx,rax ; call 0xA953C0     the native tick of M
//   0x8B63CE / 0x8B67E3 cmp byte [0x6CC0218],0 ; jne ; vmovaps xmm1,xmm6 ; mov rcx,rdi ; call 0x8B2660
//   (0x8B2660: actor50 && +0x5E & 0x10 && M && mode != 2 -> 0xAA3350(M) ; 0xAA3E70(M, xmm1))
constexpr uintptr_t kPoint = 0x8B6144;
constexpr uint32_t kPatch = 5;
constexpr uint8_t kPointBytes[] = {0x45, 0x0F, 0xB6, 0x40, 0x50, 0x48, 0x8B, 0x55,
                                   0xBF, 0x48, 0x8B, 0xC8, 0xE8, 0x6B, 0xF2, 0x1D};
constexpr uint8_t kFrameBytes[] = {0x48, 0x8D, 0x68, 0xA1};                                    // 0x8B6076
constexpr uint8_t kDtBytes[] = {0xC5, 0xF8, 0x28, 0xF1};                                       // 0x8B608F
constexpr uint8_t kActorBytes[] = {0x48, 0x8B, 0x4F, 0x50, 0x48, 0x85, 0xC9, 0x0F, 0x84, 0xD8, 0x00, 0x00,
                                   0x00, 0xF6, 0x41, 0x5E, 0x10, 0x0F, 0x84, 0xCE, 0x00, 0x00, 0x00};  // 0x8B60AF
constexpr uint8_t kAttachBytes[] = {0x48, 0x8D, 0x55, 0xB7, 0xE8, 0xF1, 0x45, 0xF4, 0x00};       // 0x8B60C6
constexpr uint8_t kVf48Bytes[] = {0x48, 0x8B, 0x4D, 0xBF, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x48};   // 0x8B6102
constexpr uint8_t kModePtrBytes[] = {0x4C, 0x8B, 0x87, 0x80, 0x00, 0x00, 0x00};                // 0x8B613D
constexpr uint8_t kLiveGate1Bytes[] = {0x80, 0x3D, 0x43, 0x9E, 0x40, 0x06, 0x00, 0x75,
                                       0x11, 0xC5, 0xF8, 0x28, 0xCE, 0x48, 0x8B, 0xCF};  // 0x8B63CE
constexpr uint8_t kLiveGate2Bytes[] = {0x80, 0x3D, 0x2E, 0x9A, 0x40, 0x06, 0x00, 0x75,
                                       0x0C, 0xC5, 0xF8, 0x28, 0xCE, 0x48, 0x8B, 0xCF};  // 0x8B67E3
// The callees (their first 16 bytes): 0xA953C0(G, Q, u8 mode); 0xAA3E70(G, float dt in xmm1); 0x9A5B70 = the
// ClientField's vf40 (FindUser([field+0x180]) -> its character's missions, 0 when not found).
constexpr uintptr_t kTickRva = 0xA953C0;
constexpr uint8_t kTickBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
                                  0x24, 0x18, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56};
constexpr uintptr_t kLiveRva = 0xAA3E70;
constexpr uint8_t kLiveBytes[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48,
                                  0x89, 0x48, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54};
constexpr uintptr_t kOwnerRva = 0x9A5B70;
constexpr uint8_t kOwnerBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x44, 0x8B,
                                   0x81, 0x80, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x54};

// Phase 2's skip hook inside 0xAA3E70 (p8-impl-escenas-play.md §13.3; the whole 0xAA4B27..0xAA4CB2 range read):
//   0xAA4400 je 0xAA4B27            the only branch to the point (empty live list); none into 0xAA4B28..0xAA4B31
//   0xAA4B27 lea rdx,[rbp+0x20] ; mov rcx,[rip+..] (= [0x6D691E0]) ; mov rcx,[rcx+0x30] ; call 0x8B4570
//            THE POINT (11 bytes: lea + the RIP-relative mov, relocated by safetyhook). [rbp+0x20] is first written
//            by 0x8B4570 here (the local player's handle) and released at 0xAA4CA4..0xAA4CAC;
//            0xAA4B60 takes [G+8]'s lock and releases it before 0xAA4C8B; then 0xA96070 / 0xA9A1F0 / 0xA9A410,
//            0xAA5040, 0xAA5400.
//   0xAA4CB2 lea rcx,[rsp+0x38] ; call 0x3D3000   destroys the live-list copy made at 0xAA43CB (before the point);
//            then [rbp-0x78] (the entry's FieldAttacher) is released with rcx/rax/rsi as vtables, rsi =
//            exe+0x5725E48 (set at 0xAA3EF3 / 0xAA4AE6 / 0xAA4B13 on every path to the point), and the epilogue.
// So for OUR call (thread_local, rcx = X kept in r13 since 0xAA3EA2) the hook resumes at 0xAA4CB2: nothing built
// between the point and 0xAA4CB2 is left unreleased, nothing released there was built (not 0xAA4CA4: it would
// release the handle 0x8B4570 never built), no lock is held at the point (the 0xAA43B5 guard is released at
// 0xAA43ED). rsp is the same at both addresses (every call in between is balanced).
constexpr uintptr_t kSkipPoint = 0xAA4B27, kSkipResume = 0xAA4CB2, kBaseVtable = 0x5725E48;
constexpr uint32_t kSkipPatch = 11;
constexpr uint8_t kSkipBytes[] = {0x48, 0x8D, 0x55, 0x20, 0x48, 0x8B, 0x0D, 0xAE, 0x46, 0x2C,
                                  0x06, 0x48, 0x8B, 0x49, 0x30, 0xE8, 0x35, 0xFA, 0xE0, 0xFF};  // 0xAA4B27..0xAA4B3A
constexpr uint8_t kSkipTailBytes[] = {0x48, 0x89, 0x75, 0x20, 0x48, 0x8D, 0x4D, 0x20, 0xE8, 0xCF, 0xFB, 0x98,
                                      0x00, 0x90, 0x48, 0x8D, 0x4C, 0x24, 0x38, 0xE8, 0x44, 0xE3, 0x92, 0xFF};  // 0xAA4CA4
constexpr uint8_t kSkipCopyBytes[] = {0x49, 0x8D, 0x55, 0x28, 0x48, 0x8D, 0x4C, 0x24, 0x38, 0xE8, 0x3B, 0xCB, 0x92, 0xFF};  // 0xAA43C7
constexpr uint8_t kSkipLoopBytes[] = {0x0F, 0x84, 0x21, 0x07, 0x00, 0x00};                   // 0xAA4400 je 0xAA4B27
constexpr uint8_t kSkipR13Bytes[] = {0x4C, 0x8B, 0xE9};                                       // 0xAA3EA2 mov r13,rcx
constexpr uint8_t kSkipRsi1Bytes[] = {0x48, 0x8D, 0x35, 0x4E, 0x1F, 0xC8, 0x04};            // 0xAA3EF3 lea rsi
constexpr uint8_t kSkipRsi2Bytes[] = {0x48, 0x8D, 0x35, 0x5B, 0x13, 0xC8, 0x04};            // 0xAA4AE6 lea rsi
constexpr uint8_t kSkipRsi3Bytes[] = {0x48, 0x8D, 0x35, 0x2E, 0x13, 0xC8, 0x04};            // 0xAA4B13 lea rsi
// The fields of G that drive 0xAA5040 / 0xAA5400 (the read-only gate).
constexpr uint32_t kMgr_2A4 = 0x2A4, kMgr_288 = 0x288, kMgr_2A8 = 0x2A8, kMgr_2D4 = 0x2D4, kMgr_2CC = 0x2CC;
constexpr uint32_t kMgr_2B0 = 0x2B0, kMgr_2B4 = 0x2B4;  // floats: 0xAA5081 vmovss/vcomiss, 0xAA50B1 vmovss

constexpr uintptr_t kVtStageManager = 0x55C9720;  // pa::ClientSequencerStageManager (RTTI)
constexpr uintptr_t kVtClientField = 0x55BCEA0;   // pa::ClientField (RTTI; +0x40 = 0x9A5B70, +0x48 = 0x9A5BE0)
constexpr uint32_t kField_Vf40 = 0x40;
constexpr uintptr_t kLiveGateByte = 0x6CC0218;    // 0x8B63CE / 0x8B67E3
constexpr uint32_t kFrame_Q = 0x41;               // [rbp-0x41]
constexpr uint32_t kModeByte = 0x50;              // [r8+0x50]
// Structure offsets (guest_seq_route.cpp; p8-s8-escenas.md §3).
constexpr uint32_t kActor_Key = 0x60, kActor_Comps = 0x68, kComps_Seq = 0xA8, kSeqComp_Mgr = 0x18;
constexpr uint32_t kMgr_Ctx = 0x08, kMgr_Live = 0x30, kMgr_Actor = 0x18, kMgr_Ready = 0x231, kMgr_Loading = 0x174;
constexpr uint32_t kMgr_N1B0 = 0x1B8, kMgr_N1C0 = 0x1C8, kMgr_N1D0 = 0x1D8;
// The client actor table (game/actor_lookup.cpp's replica, as guest_seq_route's G5): the liveness check of X's
// character before each call (review 1). Read-only SEH reads without the table's lock: a torn read can only
// make the check fail (a skipped frame), never pass for an actor that is not there with that key and X.
constexpr uint32_t kManager_Table = 0x08, kTable_BucketCount = 0x88, kTable_Buckets = 0x98, kTable_Nodes = 0xA0;
constexpr uint32_t kBucketStride = 0x100, kBucketMaxEntries = (0x100 - 8) / 8, kNode_Key = 0x04, kNode_Actor = 0x08;

using TickFn = void(__fastcall*)(uintptr_t mgr, uintptr_t field, uint8_t mode);
using LiveFn = void(__fastcall*)(uintptr_t mgr, float dt);
using OwnerFn = uintptr_t(__fastcall*)(uintptr_t field);

constexpr size_t kSlots = 8;

uintptr_t g_base = 0;
std::atomic<bool> g_started{false}, g_code_ok{false}, g_disabled{false};
std::atomic<uint8_t> g_mode{0};
std::atomic<bool> g_live{false}, g_route_on{false};
SafetyHookMid g_hook;
TickFn g_tick = nullptr;
LiveFn g_live_fn = nullptr;
OwnerFn g_owner = nullptr;

gsp::XTable<kSlots> g_table;
gsp::WriteMark g_mark[kSlots];  // main thread only
std::atomic<uint64_t> g_written_x[kSlots] = {}, g_written_us[kSlots] = {}, g_written_n[kSlots] = {};
std::atomic<uint64_t> g_written_gen[kSlots] = {}, g_rearms{0};
std::atomic<uint32_t> g_main_tid{0};  // the play hook's thread (the routing hooks are expected on it)
std::atomic<uint64_t> g_note_foreign{0}, g_note_busy{0}, g_reentry{0};
thread_local bool t_in_frame = false;  // review 2: on_frame never runs inside itself
// Phase 2: the X our own 0xAA3E70 call is for (0 otherwise); the skip hook only acts when r13 is it.
thread_local uintptr_t t_live_x = 0;
SafetyHookMid g_skip_hook;
std::atomic<bool> g_skip_ok{false};
std::atomic<uint64_t> g_live_skipped{0}, g_live_not_skipped{0}, g_live_rsi_fixed{0}, g_live_gated{0},
    g_live_no_hook{0}, g_live_held_after_tick{0}, g_live_stuck{0};
// Review of 0383f70, 3: a live call the skip hook did NOT cut (r13 != X) latches the live path off for the session.
std::atomic<bool> g_live_latched{false};
gsp::GateWatch g_gate_watch[kSlots];  // main thread: how long X's gate has been held (review 5)
constexpr uint64_t kGateStuckUs = 10'000'000;
std::atomic<uint64_t> g_first_x[kSlots] = {};  // the X of the slot's first tick (a new X: a new first)
std::atomic<uint8_t> g_last_skip[kSlots] = {};  // main thread: the last reason (a line on change)
std::atomic<int8_t> g_last_owner{-1};           // main thread: the last vf40 answer (a line when it turns null)
std::atomic<uint64_t> g_slot_ticks[kSlots] = {}, g_slot_lives[kSlots] = {}, g_slot_consumed[kSlots] = {},
                      g_slot_grew[kSlots] = {}, g_slot_would[kSlots] = {};
std::atomic<uint64_t> g_frames{0}, g_ticks{0}, g_lives{0}, g_writes{0}, g_would_ticks{0}, g_would_writes{0},
    g_would_lives{0}, g_owner_calls{0}, g_owner_null{0}, g_faults{0}, g_write_faults{0};
std::atomic<uint64_t> g_skips[static_cast<size_t>(Skip::Count)] = {};

enum Kind : uint16_t { kEWrite, kESkip, kEFirst, kEOwner, kEStuck, kELatch, kKindCount };
p::EventRing<256> g_ring;
p::WindowLimiter g_lim[kKindCount] = {{32, 5'000'000}, {32, 5'000'000}, {32, 5'000'000},
                                      {8, 5'000'000},  {16, 5'000'000}, {4, 5'000'000}};
std::atomic<uint64_t> g_limited{0}, g_lost{0}, g_lines{0};
constexpr uint64_t kMaxLines = 2000;

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
uintptr_t manager_of(uintptr_t actor) noexcept {
    const uintptr_t comps = actor ? rdp(actor + kActor_Comps) : 0;
    const uintptr_t seq = comps ? rdp(comps + kComps_Seq) : 0;
    return seq ? rdp(seq + kSeqComp_Mgr) : 0;
}
uintptr_t find_actor(uint32_t key) noexcept {
    const uintptr_t world = rdp(g_base + off::kWorldSystemSlot);
    const uintptr_t mgr = world ? rdp(world + off::kWorldSystem_ActorManager) : 0;
    const uintptr_t table = mgr ? rdp(mgr + kManager_Table) : 0;
    const uint32_t buckets = table ? rdv<uint32_t>(table + kTable_BucketCount) : 0;
    if (!buckets || buckets > (1u << 24)) return 0;
    const uintptr_t base = rdp(table + kTable_Buckets), nodes = rdp(table + kTable_Nodes);
    if (!base || !nodes) return 0;
    const uintptr_t bucket = base + static_cast<uintptr_t>(key % buckets) * kBucketStride;
    const uint32_t count = rdv<uint32_t>(bucket);
    for (uint32_t i = 0; i < count && i < kBucketMaxEntries; ++i) {
        if (rdv<uint32_t>(bucket + 8 + i * 8) != key) continue;
        const uint32_t idx = rdv<uint32_t>(bucket + 0xC + i * 8);
        const uintptr_t node = idx ? rdp(nodes + static_cast<uintptr_t>(idx) * 8) : 0;
        if (!node || rdv<uint32_t>(node + kNode_Key) != key) return 0;
        return rdp(node + kNode_Actor);
    }
    return 0;
}
// Review 1: X's character is still registered in the client actor table under `key`, is X's [X+0x18], and
// still owns X. Every frame, only for a slot that passed the cheaper checks (one bucket walk).
bool actor_live(uintptr_t x, uint32_t key) noexcept {
    const uintptr_t actor = find_actor(key);
    return actor && actor == rdp(x + kMgr_Actor) && key_of(actor) == key && manager_of(actor) == x;
}
uint32_t lists_of(uintptr_t x) noexcept {
    return rdv<uint32_t>(x + kMgr_N1B0) + rdv<uint32_t>(x + kMgr_N1C0) + rdv<uint32_t>(x + kMgr_N1D0);
}

// Our own write (no lock: neither the native 0xB87A67 nor .xtls 0x9F8FD19 takes one for it).
bool write_byte(uintptr_t a, uint8_t v) noexcept {
    if (a < 0x10000) return false;
    __try {
        *reinterpret_cast<volatile uint8_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// P8 review I1: an exception inside the game's code we called is the game's (it may hold [X+8]'s lock or
// a half-done list): the module switches off, one line goes straight to the log, the exception is passed on.
// Review 5 [V, unwind info of the PE]: no frame of the chain catches it: 0x8B6060, 0xAD2380 (main loop),
// 0x617290 and 0xA953C0 / 0xA97300 / 0xAA3E70 have only UNW_FLAG_UHANDLER (0x4898DE8: unwind cleanups, never
// called in the search phase), WinApp::vf9 0x3F41240 has no handler; above it [E] the CRT's main and the
// unhandled-exception filter, which end the process. So nothing in that chain resumes the main loop with
// [X+8] still locked: the fault is a real crash where it happened. Were something above to resume, the module is
// already off (g_disabled) and never calls the game again; the lock would stay taken (cannot be released
// safely from here: the game's guard object lives in the faulting frame).
int call_fault(EXCEPTION_POINTERS* ep, const char* where) noexcept {
    t_live_x = 0;
    guest_x_context_clear();  // guest_x_veto's X context (set by the scopes below), before any unwind
    g_disabled.store(true);
    g_faults.fetch_add(1);
    return game::game_fault_continue(ep, "GuestSeqPlay", where);
}
bool guarded_tick_body(uintptr_t x, uintptr_t q, uint8_t mode) noexcept {
    __try {
        g_tick(x, q, mode);
        return true;
    } __except (call_fault(GetExceptionInformation(), "0xA953C0(X, Q, mode) ran for the host character's manager X")) {
        return false;  // never reached: the filter passes it on
    }
}
// guest_x_veto (p8-s9-cinematica.md §8): our calls for X run in an "X context" (RAII; the filter clears it too):
// a load request 0x778AB0 from inside them is X's, never this client's own.
bool guarded_tick(uintptr_t x, uintptr_t q, uint8_t mode) noexcept {
    const GuestXContextScope scope(x);
    return guarded_tick_body(x, q, mode);
}
bool guarded_live(uintptr_t x, float dt) noexcept {
    __try {
        g_live_fn(x, dt);
        return true;
    } __except (call_fault(GetExceptionInformation(), "0xAA3E70(X, dt) ran for the host character's manager X")) {
        return false;
    }
}
// Review (0383f70) 1: t_live_x is set for exactly our call (RAII; the SEH filter clears it too, before any
// unwind). The skip hook at 0xAA4B27 cuts the local player's part of this call only.
struct LiveScope {
    explicit LiveScope(uintptr_t x) noexcept { t_live_x = x; }
    ~LiveScope() { t_live_x = 0; }
    LiveScope(const LiveScope&) = delete;
    LiveScope& operator=(const LiveScope&) = delete;
};
bool call_live(uintptr_t x, float dt) noexcept {
    const LiveScope scope(x);
    const GuestXContextScope x_scope(x);
    return guarded_live(x, dt);
}

bool guarded_owner(uintptr_t q, uintptr_t& out) noexcept {
    __try {
        out = g_owner(q);
        return true;
    } __except (call_fault(GetExceptionInformation(), "the field's vf40 0x9A5B70 ran before a tick of X")) {
        return false;
    }
}

void emit(Kind k, p::Event& e) noexcept {
    e.kind = k;
    e.qpc_us = p8_now_us();
    e.tid = GetCurrentThreadId();
    if (!g_lim[k].take(e.qpc_us)) {
        g_limited.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!g_ring.push(e)) g_lost.fetch_add(1, std::memory_order_relaxed);
}

// The five fields of X behind 0xAA5040 / 0xAA5400 (an unreadable one is not idle).
void read_gate(gsp::Facts& f) noexcept {
    f.x_2a4 = rdv<uint8_t>(f.x + kMgr_2A4, 0xFF);
    f.x_288 = rdp(f.x + kMgr_288);
    f.x_2a8 = rdp(f.x + kMgr_2A8);
    f.x_2b0 = rdv<float>(f.x + kMgr_2B0, 1.0f);
    f.x_2b4 = rdv<float>(f.x + kMgr_2B4, 0.0f);
    f.x_2d4 = rdv<uint8_t>(f.x + kMgr_2D4, 0xFF);
    f.x_2cc = rdv<float>(f.x + kMgr_2CC, 1.0f);
}

// Review of 0383f70, 5: a gate held for kGateStuckUs gets one line per episode (and a count); never forced.
void watch_gate(size_t i, const gsp::Facts& f, bool held) noexcept {
    const auto bits = [](float v) { return static_cast<uint64_t>(std::bit_cast<uint32_t>(v)); };
    const uint64_t now = p8_now_us();
    if (!g_gate_watch[i].update(held, now, kGateStuckUs)) return;
    g_live_stuck.fetch_add(1, std::memory_order_relaxed);
    p::Event e;
    e.v[0] = i;
    e.v[1] = f.x;
    e.v[2] = f.key;
    e.v[3] = now - g_gate_watch[i].since_us;
    e.v[4] = f.x_2a4;
    e.v[5] = f.x_288;
    e.v[6] = f.x_2a8;
    e.v[7] = f.x_2d4;
    e.v[8] = bits(f.x_2cc);
    e.v[9] = bits(f.x_2b0) | bits(f.x_2b4) << 32;
    // M's own values, to compare (the same fields of this client's own manager).
    e.v[10] = f.m ? rdp(f.m + kMgr_2A8) : 0;
    e.v[11] = f.m ? bits(rdv<float>(f.m + kMgr_2B0, 0.0f)) | bits(rdv<float>(f.m + kMgr_2B4, 0.0f)) << 32 : 0;
    emit(kEStuck, e);
}

void on_frame_body(safetyhook::Context& ctx) noexcept {
    const auto mode = static_cast<gsp::Mode>(g_mode.load(std::memory_order_relaxed));
    if (mode == gsp::Mode::Off) return;
    // guest_x_veto's own-manager check (review 4): this frame's M (vf48 of the field) and its live owner.
    if (ctx.rax) guest_x_note_m(ctx.rax, key_of(rdp(ctx.rax + kMgr_Actor)));
    const uint64_t frame = g_frames.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!g_table.used()) return;
    const bool probe = mode == gsp::Mode::Probe;

    gsp::Facts base;
    base.epoch = guest_connection_epoch();
    base.mode = mode;
    base.live = g_live.load(std::memory_order_relaxed);
    base.code_ok = !g_disabled.load(std::memory_order_relaxed);
    base.field_mode = rdv<uint8_t>(ctx.r8 + kModeByte, gsp::kFieldModeNoTick);
    base.m = ctx.rax;
    base.m_ready = base.m ? rdv<uint8_t>(base.m + kMgr_Ready) : 0;
    base.m_ctx_vt = base.m ? rdp(rdp(base.m + kMgr_Ctx)) : 0;
    const uintptr_t q = rdp(ctx.rbp - kFrame_Q);
    base.q = q;
    const uintptr_t q_vt = q ? rdp(q) : 0;
    base.q_is_field = q_vt == g_base + kVtClientField && rdp(q_vt + kField_Vf40) == g_base + kOwnerRva;
    base.live_gate = rdv<uint8_t>(g_base + kLiveGateByte, 1) == 0;
    base.want_vt = g_base + kVtStageManager;
    base.skip_hook_ok = g_skip_ok.load(std::memory_order_acquire) && !g_live_latched.load(std::memory_order_relaxed);
    const float dt = ctx.xmm6.f32[0];
    int8_t owner = -1;  // vf40(Q): asked at most once per frame

    for (size_t i = 0; i < kSlots; ++i) {
        const auto view = g_table.get(i);
        if (!view.key) continue;
        gsp::Facts f = base;
        f.x = view.x;
        f.key = view.key;
        f.slot_field = view.field;
        f.slot_epoch = view.epoch;
        if (f.x) {
            f.x_vt = rdp(f.x);
            const uintptr_t actor = rdp(f.x + kMgr_Actor);
            f.x_key = key_of(actor);
            f.x_own = manager_of(actor);
            f.x_ctx = rdp(f.x + kMgr_Ctx);
            f.x_ctx_vt = f.x_ctx ? rdp(f.x_ctx) : 0;
            f.x_ready = rdv<uint8_t>(f.x + kMgr_Ready);
            f.x_loading = rdv<uint32_t>(f.x + kMgr_Loading);
            f.n1b0 = rdv<uint32_t>(f.x + kMgr_N1B0);
            f.n1c0 = rdv<uint32_t>(f.x + kMgr_N1C0);
            if (base.live) read_gate(f);  // phase 2's read-only gate
            // The table walk only when the cheaper identity checks hold (it is the most expensive read).
            if (f.x_vt == base.want_vt && f.x_key == f.key && f.x_own == f.x) f.actor_live = actor_live(f.x, f.key);
        }
        // probe never writes: its "would write" mark must not re-arm on X still reading 2.
        f.armed = g_mark[i].armed(f.x, view.gen, probe ? 0 : f.x_ready);
        f.owner_q = owner;
        gsp::Plan pl = gsp::plan(f);
        if (gsp::clears_slot(pl.skip)) g_table.clear(i, view.key);
        if (pl.needs_owner) {
            uintptr_t missions = 0;
            if (!guarded_owner(q, missions)) return;  // never reached: the filter passes it on
            g_owner_calls.fetch_add(1, std::memory_order_relaxed);
            owner = missions ? 1 : 0;
            if (!missions) g_owner_null.fetch_add(1, std::memory_order_relaxed);
            if (g_last_owner.exchange(owner, std::memory_order_relaxed) != owner && !missions) {  // a line per turn to null
                p::Event e;
                e.v[0] = 1;
                e.v[1] = frame;
                emit(kEOwner, e);
            }
            f.owner_q = owner;
            pl = gsp::plan(f);
        }
        const auto skip_now = static_cast<uint8_t>(pl.skip);
        if (g_last_skip[i].exchange(skip_now, std::memory_order_relaxed) != skip_now) {
            p::Event e;
            e.v[0] = i;
            e.v[1] = f.x;
            e.v[2] = f.key;
            e.v[3] = skip_now;
            e.v[4] = f.x_ready;
            e.v[5] = f.m_ready;
            e.v[6] = f.field_mode;
            e.v[7] = probe;
            emit(kESkip, e);
        }
        g_skips[skip_now].fetch_add(1, std::memory_order_relaxed);
        if (pl.skip != Skip::None || !pl.tick) continue;
        if (pl.live_held) (pl.live_no_hook ? g_live_no_hook : g_live_gated).fetch_add(1, std::memory_order_relaxed);
        if (pl.live_held && !pl.live_no_hook && !probe) watch_gate(i, f, true);

        if (g_first_x[i].exchange(f.x, std::memory_order_relaxed) != f.x) {
            p::Event e;
            e.v[0] = i;
            e.v[1] = f.x;
            e.v[2] = f.key;
            e.v[3] = pl.live;
            e.v[4] = f.x_ready;
            e.v[5] = probe;
            e.v[6] = f.n1b0 | static_cast<uint64_t>(f.n1c0) << 32;
            emit(kEFirst, e);
        }
        // The 3 (review 9; p8-xtls-lectura.md §1.1-§1.4, read in Ghidra): the native .xtls 0x9F8FCE0 only
        // writes +0x231 = 3 (0x9F8FD19, no lock) and plays at once the eligible entries of +0x1C0; with the 3
        // the tick below plays every ready entry of +0x1B0/+0x1C0 itself, one per call (0xA97503).
        // Order (review 12): X's tick runs right BEFORE M's native tick in the same frame. The hook cannot sit
        // after 0x8B6150: RAX (M) and R8 (mode) are gone there, and 0x8B6156 is RIP-relative. X and M share
        // nothing but Q (read only) and, maybe, the class of their lock object, each taken and released inside
        // its own call (never nested), so the order changes nothing for M.
        if (pl.write3) {
            const bool ok = probe || write_byte(f.x + kMgr_Ready, gsp::kStatePlay);
            if (ok) {
                if (g_mark[i].mark(f.x, view.gen)) g_rearms.fetch_add(1, std::memory_order_relaxed);
                g_written_x[i].store(f.x, std::memory_order_relaxed);
                g_written_gen[i].store(view.gen, std::memory_order_relaxed);
                g_written_us[i].store(p8_now_us(), std::memory_order_relaxed);
                g_written_n[i].fetch_add(1, std::memory_order_relaxed);
                (probe ? g_would_writes : g_writes).fetch_add(1, std::memory_order_relaxed);
                p::Event e;
                e.v[0] = i;
                e.v[1] = f.x;
                e.v[2] = f.key;
                e.v[3] = view.gen;
                e.v[4] = f.n1b0 | static_cast<uint64_t>(f.n1c0) << 16 | static_cast<uint64_t>(f.m_ready) << 32 |
                         static_cast<uint64_t>(probe) << 40;
                e.v[5] = frame;
                emit(kEWrite, e);
            } else {
                g_write_faults.fetch_add(1, std::memory_order_relaxed);
                continue;  // X not writable: gone
            }
        }
        if (probe) {
            g_would_ticks.fetch_add(1, std::memory_order_relaxed);
            g_slot_would[i].fetch_add(1, std::memory_order_relaxed);
            if (pl.live) g_would_lives.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        const bool playing = f.x_ready == gsp::kStatePlay || pl.write3;
        const uint32_t before = lists_of(f.x);
        const uint32_t live_before = rdv<uint32_t>(f.x + kMgr_Live);
        if (!guarded_tick(f.x, q, f.field_mode)) return;  // never reached
        g_ticks.fetch_add(1, std::memory_order_relaxed);
        g_slot_ticks[i].fetch_add(1, std::memory_order_relaxed);
        if (pl.live) {
            // Review of 0383f70, 2: the tick may have changed X's fields: the gate again, right before the call.
            gsp::Facts now = f;
            read_gate(now);
            if (!gsp::live_fields_idle(now)) {
                g_live_held_after_tick.fetch_add(1, std::memory_order_relaxed);
                watch_gate(i, now, true);
            } else if (!g_live_latched.load(std::memory_order_relaxed)) {
                watch_gate(i, now, false);
                if (!call_live(f.x, dt)) return;  // never reached
                g_lives.fetch_add(1, std::memory_order_relaxed);
                g_slot_lives[i].fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (playing) {
            g_slot_consumed[i].fetch_add(gsp::consumed(before, lists_of(f.x)), std::memory_order_relaxed);
            const uint32_t live_after = rdv<uint32_t>(f.x + kMgr_Live);
            if (live_after > live_before) g_slot_grew[i].fetch_add(live_after - live_before, std::memory_order_relaxed);
        }
    }
}

// Phase 2's skip hook at 0xAA4B27 (inside 0xAA3E70, also for M's native call every frame: then it does nothing).
// For our own call for X (t_live_x, and r13 = X: G since 0xAA4389): resume at 0xAA4CB2, over the local player's
// part (0x8B4570, 0xA96070, 0xA9A1F0, 0xA9A410) and 0xAA5040 / 0xAA5400. rsi must be exe+0x5725E48 there (the
// base vtable 0xAA4CD2 stores); it is on every path [V], and is set anyway (counted if it was not).
void on_skip(safetyhook::Context& ctx) noexcept {
    const uintptr_t x = t_live_x;
    if (!x) return;  // the game's own call (M)
    if (ctx.r13 != x) {
        // Not expected [V call graph]: 0xAA3E70's only caller is 0x8B26B7 in 0x8B2660, whose only callers are
        // 0x8B63DE / 0x8B67F3 in 0x8B6060 (no pointer to either): no 0xAA3E70 for another manager can run inside
        // ours. Should r13 still differ, fail closed: this call runs natively (it cannot be cut safely here),
        // and the live path is off for the session (review of 0383f70, 3).
        g_live_not_skipped.fetch_add(1, std::memory_order_relaxed);
        if (!g_live_latched.exchange(true, std::memory_order_relaxed)) {
            p::Event e;
            e.v[0] = x;
            e.v[1] = ctx.r13;
            emit(kELatch, e);
        }
        return;
    }
    const uintptr_t base_vt = g_base + kBaseVtable;
    if (ctx.rsi != base_vt) {
        g_live_rsi_fixed.fetch_add(1, std::memory_order_relaxed);
        ctx.rsi = base_vt;
    }
    ctx.rip = g_base + kSkipResume;
    g_live_skipped.fetch_add(1, std::memory_order_relaxed);
}

// Review 2: the game calls 0x8B6060 once per main loop turn; nothing it calls reaches 0x8B6144 again, but
// if it ever did (a nested frame update inside a tick of X), the inner call does nothing.
void on_frame(safetyhook::Context& ctx) noexcept {
    if (!g_code_ok.load(std::memory_order_acquire)) return;
    if (t_in_frame) {
        g_reentry.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!g_main_tid.load(std::memory_order_relaxed)) g_main_tid.store(GetCurrentThreadId(), std::memory_order_relaxed);
    freeze_watchdog_frame();  // once per main loop turn: the freeze watchdog's frame source (two relaxed stores)
    t_in_frame = true;
    // guest_sched_bind's retry queue (session 12): the main thread, before the native tick; nothing when it is off
    // or the queue is empty (two relaxed loads). RAX = M (this client's own manager, from the field's vf48).
    guest_sched_bind_frame(ctx.rax);
    on_frame_body(ctx);
    // S45 S-D (guest_seq_present.h): read-only sample of the client's camera / input state after X's tick and before
    // M's native one; one atomic load when guest_seq_foreign_present_probe=0.
    guest_seq_present_frame(ctx.rax);
    t_in_frame = false;
}

// ---------------------------------------------------------------- lines (diagnostics thread)

std::string line_of(const p::Event& e) {
    const uint64_t* v = e.v;
    const std::string when = p8_when(e.qpc_us);
    switch (e.kind) {
        case kEWrite:
            return std::format("GuestSeqPlay [3] {} T{} | slot {} X 0x{:X} (character 0x{:08X}, routed 2322 #{}): {} "
                               "X+0x231 2 -> 3 (lists +0x1B0 {}, +0x1C0 {}; M+0x231 {}) at frame {} | the tick plays "
                               "what is preloaded from now on, at most one per call (0xA97503)",
                               when, e.tid, v[0], v[1], static_cast<uint32_t>(v[2]), v[3],
                               (v[4] >> 40) & 1 ? "WOULD WRITE (probe)" : "WROTE", v[4] & 0xFFFF, (v[4] >> 16) & 0xFFFF,
                               (v[4] >> 32) & 0xFF, v[5]);
        case kESkip:
            return std::format("GuestSeqPlay [state] {} T{} | slot {} X 0x{:X} (character 0x{:08X}): {}{} | X+0x231 {}, "
                               "M+0x231 {}, field mode {}",
                               when, e.tid, v[0], v[1], static_cast<uint32_t>(v[2]),
                               gsp::skip_name(static_cast<Skip>(v[3])),
                               v[3] == static_cast<uint64_t>(Skip::None) && v[7] ? " (probe: would tick)" : "", v[4],
                               v[5], v[6]);
        case kEFirst:
            return std::format("GuestSeqPlay [first tick] {} T{} | slot {} X 0x{:X} (character 0x{:08X}): {} 0xA953C0(X, "
                               "Q, mode){} | X+0x231 {}, +0x1B0 {}, +0x1C0 {}",
                               when, e.tid, v[0], v[1], static_cast<uint32_t>(v[2]), v[5] ? "WOULD call (probe)" : "calls",
                               v[3] ? " + 0xAA3E70(X, dt)" : "", v[4], static_cast<uint32_t>(v[6]), v[6] >> 32);
        case kEOwner:
            return std::format("GuestSeqPlay [vf40] {} T{} | the field's vf40 0x9A5B70 returned NULL at frame {}: no tick "
                               "of X this frame (0x602940 would fault at 0x602997 / 0x1AC2830)",
                               when, e.tid, v[1]);
        case kEStuck:
        {
            const auto fl = [](uint64_t w) { return std::bit_cast<float>(static_cast<uint32_t>(w)); };
            return std::format("GuestSeqPlay [live held] {} T{} | slot {} X 0x{:X} (character 0x{:08X}): its live update "
                               "has been held {:.1f} s: X+0x2A4 {}, +0x288 0x{:X}, +0x2A8 0x{:X} (+0x2B0 {}, +0x2B4 {}), "
                               "+0x2D4 {}, +0x2CC {} | M: +0x2A8 0x{:X} (+0x2B0 {}, +0x2B4 {}) | idle = +0x2A4/+0x288/"
                               "+0x2D4 0, +0x2CC <= 0 and (+0x2A8 0 or +0x2B0 < 0); only 0xAA5040 / 0xAA5400 (skipped "
                               "for X) or the manager's vf1 0xA94660 clear them: not forced, the hold goes on",
                               when, e.tid, v[0], v[1], static_cast<uint32_t>(v[2]),
                               static_cast<double>(v[3]) / 1e6, v[4], v[5], v[6], fl(v[9]), fl(v[9] >> 32), v[7],
                               fl(v[8]), v[10], fl(v[11]), fl(v[11] >> 32));
        }
        case kELatch:
            return std::format("GuestSeqPlay [live OFF] {} T{} | X 0x{:X}: the skip hook at 0xAA4B27 saw r13 0x{:X} in our "
                               "own 0xAA3E70(X) call, so it did NOT cut the local player's part: the live update of every "
                               "X is OFF for this session (fail closed; the tick and the 3 go on)",
                               when, e.tid, v[0], v[1]);
        default:
            return std::format("GuestSeqPlay: event {}?", e.kind);
    }
}

uint64_t g_tick_n = 0;  // diagnostics thread only

void summary() {
    std::string slots;
    const uint64_t now = p8_now_us();
    for (size_t i = 0; i < kSlots; ++i) {
        const auto v = g_table.get(i);
        if (!v.key) continue;
        const uint64_t wus = g_written_us[i].load();
        slots += std::format(" | slot {} character 0x{:08X} X 0x{:X} (field 0x{:X}, 2322 gen {}): ticks {}{}, live calls {}, 3 {} "
                             "{}, started ~{} (instances that left +0x1B0/+0x1C0/+0x1D0 in state 3), live list +0x28 "
                             "grew {}",
                             i, v.key, v.x, v.field, v.gen, g_slot_ticks[i].load(),
                             g_slot_would[i].load() ? std::format(" (probe would {})", g_slot_would[i].load()) : "",
                             g_slot_lives[i].load(),
                             g_written_n[i].load() ? (g_mode.load() == 1 ? "would be written" : "written") : "not written",
                             g_written_n[i].load()
                                 ? std::format("x{} (last {} = {:.1f} s ago, X 0x{:X}, 2322 #{})", g_written_n[i].load(),
                                               p8_when(wus), wus && now > wus ? static_cast<double>(now - wus) / 1e6 : 0.0,
                                               g_written_x[i].load(), g_written_gen[i].load())
                                 : std::string(),
                             g_slot_consumed[i].load(), g_slot_grew[i].load());
    }
    std::string skips;
    for (size_t k = 1; k < static_cast<size_t>(Skip::Count); ++k) {
        if (const uint64_t n = g_skips[k].load()) skips += std::format(" {} x{};", gsp::skip_name(static_cast<Skip>(k)), n);
    }
    if (g_live.load()) {
        log::info("GuestSeqPlay live (phase 2): skip hook {}{} | 0xAA3E70(X) calls {}: local-player part skipped {} (rsi "
                  "set {}), NOT skipped {} | held back: X's +0x2A4/+0x288/+0x2A8/+0x2D4/+0x2CC not idle {} (and right "
                  "after the tick {}), no skip hook or latched off {} | held > {} s (stuck, not forced) {}",
                  g_skip_ok.load() ? "in" : "MISSING (no live call)",
                  g_live_latched.load() ? ", LATCHED OFF (a call was not cut: see the [live OFF] line)" : "",
                  g_lives.load(), g_live_skipped.load(), g_live_rsi_fixed.load(), g_live_not_skipped.load(),
                  g_live_gated.load(), g_live_held_after_tick.load(), g_live_no_hook.load(), kGateStuckUs / 1'000'000,
                  g_live_stuck.load());
    }
    log::info("GuestSeqPlay summary (guest_seq_play={}, live={}): frames {} | ticks {} (would {}), live calls {} (would "
              "{}), 3 written {} (would {}, re-armed after a native 2 {}) | vf40 asked {}, null {} | skipped:{} | table {} "
              "of {} (full {}, cleared {}, writer busy {}, unstable reads {}), routing hooks on another thread {}, "
              "re-entries {}{}{} | faults {}{} | limited {} lost {}",
              g_mode.load() == 2 ? "1" : g_mode.load() == 1 ? "probe" : "0", g_live.load() ? 1 : 0, g_frames.load(),
              g_ticks.load(), g_would_ticks.load(), g_lives.load(), g_would_lives.load(), g_writes.load(),
              g_would_writes.load(), g_rearms.load(), g_owner_calls.load(), g_owner_null.load(),
              skips.empty() ? " none" : skips, g_table.used(), kSlots, g_table.full(), g_table.cleared(),
              g_note_busy.load(), g_table.unstable(), g_note_foreign.load(), g_reentry.load(),
              slots.empty() ? " | no X routed yet" : "", slots, g_faults.load(),
              g_disabled.load() ? " (SWITCHED OFF after a fault in the game's code)" : "", g_limited.load(), g_lost.load());
}

// ---------------------------------------------------------------- install

struct Ctx {
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
        log::error("GuestSeqPlay: unsupported game build or unreadable sections; nothing installed");
        return 0;
    }
    for (int i = 0; i < 1200; ++i) {  // WorldSystem (up to 10 minutes), as the other guest modules
        uintptr_t world = 0;
        if (game::safe_read(g_base + off::kWorldSystemSlot, &world, sizeof world) && world) break;
        Sleep(500);
    }
    const Ctx ctx[] = {
        {0x8B6076, kFrameBytes},      {0x8B608F, kDtBytes},     {0x8B60AF, kActorBytes},
        {0x8B60C6, kAttachBytes},     {0x8B6102, kVf48Bytes},   {0x8B613D, kModePtrBytes},
        {0x8B63CE, kLiveGate1Bytes},  {0x8B67E3, kLiveGate2Bytes}, {kTickRva, kTickBytes},
        {kLiveRva, kLiveBytes},       {kOwnerRva, kOwnerBytes},
    };
    for (const Ctx& c : ctx) {
        if (!gad::in_real_code(secs, static_cast<uint32_t>(c.rva), static_cast<uint32_t>(c.bytes.size())) ||
            !bytes_at(c.rva, c.bytes)) {
            log::error("GuestSeqPlay: the code at exe+0x{:X} ({} bytes) is not build 2976's .rsrc code; REFUSED, nothing "
                       "installed",
                       c.rva, c.bytes.size());
            return 0;
        }
    }
    g_tick = reinterpret_cast<TickFn>(g_base + kTickRva);
    g_live_fn = reinterpret_cast<LiveFn>(g_base + kLiveRva);
    g_owner = reinterpret_cast<OwnerFn>(g_base + kOwnerRva);
    if (!p8_install_mid(g_base, secs, kPoint, kPatch, kPointBytes, on_frame, g_hook, "GuestSeqPlay",
                        "the frame's sequencer tick 0x8B6144")) {
        return 0;  // the line above says why
    }
    // Phase 2: the skip hook first (0xAA3E70(X) is never called without it: gsp::plan).
    std::string skip_line = "not wanted (live=0)";
    if (g_live.load()) {
        const Ctx skip_ctx[] = {
            {0xAA4CA4, kSkipTailBytes},          {0xAA43C7, kSkipCopyBytes}, {0xAA4400, kSkipLoopBytes},
            {0xAA3EA2, kSkipR13Bytes},           {0xAA3EF3, kSkipRsi1Bytes}, {0xAA4AE6, kSkipRsi2Bytes},
            {0xAA4B13, kSkipRsi3Bytes},
        };
        bool ok = true;
        for (const Ctx& c : skip_ctx) {
            if (!gad::in_real_code(secs, static_cast<uint32_t>(c.rva), static_cast<uint32_t>(c.bytes.size())) ||
                !bytes_at(c.rva, c.bytes)) {
                skip_line = std::format("REFUSED: the code at exe+0x{:X} is not build 2976's", c.rva);
                ok = false;
                break;
            }
        }
        if (ok && p8_install_mid(g_base, secs, kSkipPoint, kSkipPatch, kSkipBytes, on_skip, g_skip_hook, "GuestSeqPlay",
                                 "the live update's local-player skip 0xAA4B27")) {
            g_skip_ok.store(true, std::memory_order_release);
            skip_line = "hooked 0xAA4B27 -> 0xAA4CB2 for our calls only";
        } else if (ok) {
            skip_line = "REFUSED (see the line above)";
        }
    }
    g_code_ok.store(true, std::memory_order_release);
    const auto mode = static_cast<gsp::Mode>(g_mode.load());
    log::info("GuestSeqPlay {} (guest_seq_play, live={}): hooked 0x8B6144 (movzx r8d,byte [r8+0x50]; main loop, once per "
              "frame, before the native 0xA953C0(M, Q, mode)) | per X routed by guest_seq_route (the host's character's "
              "ClientSequencerStageManager, vtable exe+0x{:X} checked, never M): 0xA953C0(X, Q, mode) when mode != 2, "
              "M+0x231 == 3, the field's vf40 != 0; X+0x231 = 3 once per routed 2322 (X at 2, +0x174 0, +0x1B0/+0x1C0 "
              "not empty){} | live skip hook: {} | guest_seq_route={}{}",
              gsp::mode_name(mode), g_live.load() ? 1 : 0, kVtStageManager,
              g_live.load() ? "; 0xAA3E70(X, dt) when X is at 3, byte [0x6CC0218] == 0, the skip hook is in and X+0x2A4/"
                              "+0x288/+0x2A8/+0x2D4 are 0 and +0x2CC <= 0 (the local player's part and 0xAA5040/"
                              "0xAA5400 never run for X); the 2524 routed to X"
                            : "",
              skip_line,
              g_route_on.load() ? "1" : "NOT 1",
              g_route_on.load() ? "" : " (probe only: guest_seq_route is not 1, nothing is routed, no X will be known)");
    return 0;
}

}  // namespace

void guest_seq_play_start(const std::string& mode, bool live, const std::string& route_mode) {
    bool bad = false;
    const gsp::Mode asked = gsp::parse_mode(mode, &bad);
    if (bad) log::warn("GuestSeqPlay: guest_seq_play={} is not 0, probe or 1: off", mode);
    const gsp::Mode route = gsp::parse_mode(route_mode);
    const gsp::Mode m = gsp::effective_mode(asked, route);
    if (m != asked) {
        log::error("GuestSeqPlay: guest_seq_play={} REFUSED (off): it needs guest_seq_route=1 (it is {}); only a ROUTED "
                   "packet makes X known, and only the routing creates the host's instances in X",
                   mode, route_mode.empty() ? "0" : route_mode);
    } else if (m == gsp::Mode::Probe && route != gsp::Mode::On) {
        log::warn("GuestSeqPlay: guest_seq_play=probe with guest_seq_route={}: nothing is routed, so no X will be known "
                  "(only frames are counted)",
                  route_mode);
    }
    g_mode.store(static_cast<uint8_t>(m));
    g_live.store(live && m != gsp::Mode::Off);
    g_route_on.store(route == gsp::Mode::On);
    if (m == gsp::Mode::Off) {
        if (live) log::warn("GuestSeqPlay: guest_seq_play_live=1 needs guest_seq_play=probe or 1: off");
        return;
    }
    if (g_started.exchange(true)) return;
    for (auto& s : g_last_skip) s.store(0xFF);  // the first state of each slot (also "ticked") gets its line
    CloseHandle(CreateThread(nullptr, 0, install_thread, nullptr, 0, nullptr));
}

bool guest_seq_play_wanted() { return g_started.load(); }

uint32_t guest_seq_play_main_tid() noexcept { return g_main_tid.load(std::memory_order_relaxed); }

bool guest_seq_play_routes_2524() {
    return g_mode.load(std::memory_order_relaxed) == static_cast<uint8_t>(gsp::Mode::On) &&
           g_live.load(std::memory_order_relaxed) && g_skip_ok.load(std::memory_order_relaxed) &&
           !g_live_latched.load(std::memory_order_relaxed) && !g_disabled.load(std::memory_order_relaxed);
}

bool guest_seq_play_ticks() {
    return g_mode.load(std::memory_order_relaxed) == static_cast<uint8_t>(gsp::Mode::On) &&
           !g_disabled.load(std::memory_order_relaxed);
}

void guest_seq_play_note_x(uintptr_t x, uint32_t key, uintptr_t field, bool milestone) noexcept {
    if (g_mode.load(std::memory_order_relaxed) == 0) return;
    // Expected on the play hook's thread (both are run by 0xAD2380): counted if not (the table stays correct:
    // writers serialize on a try-lock).
    const uint32_t main = g_main_tid.load(std::memory_order_relaxed);
    if (main && main != GetCurrentThreadId()) g_note_foreign.fetch_add(1, std::memory_order_relaxed);
    if (g_table.note(x, key, field, guest_connection_epoch(), milestone) == gsp::XTable<kSlots>::Noted::Busy) {
        g_note_busy.fetch_add(1, std::memory_order_relaxed);
    }
}

bool guest_seq_play_knows_x(uintptr_t x, uint32_t key) noexcept {
    return g_mode.load(std::memory_order_relaxed) != 0 && g_table.knows(x, key, guest_connection_epoch());
}

void guest_seq_play_tick() noexcept {
    if (!g_started.load()) return;
    try {
        ++g_tick_n;
        p::Event e;
        while (g_ring.pop(e)) {
            if (e.kind >= kKindCount) continue;
            if (e.kind == kELatch) {
                log::error("{}", line_of(e));  // always, outside the line budget
            } else if (g_lines.fetch_add(1) < kMaxLines) {
                log::info("{}", line_of(e));
            }
        }
        if (g_tick_n % 12 == 0) summary();
    } catch (...) {
    }
}

std::string guest_seq_play_describe() {
    if (!g_started.load()) return {};
    return std::format(" | seq play {}{} (ticks {}, 3 written {}{})",
                       g_mode.load() == 2 ? "on" : "probe", g_live.load() ? "+live" : "",
                       g_mode.load() == 2 ? g_ticks.load() : g_would_ticks.load(),
                       g_mode.load() == 2 ? g_writes.load() : g_would_writes.load(), g_disabled.load() ? ", OFF after a fault" : "");
}

}  // namespace cdmp::session2
