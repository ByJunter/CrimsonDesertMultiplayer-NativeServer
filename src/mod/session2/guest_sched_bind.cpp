#include "mod/session2/guest_sched_bind.h"

#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <string>
#include <vector>

#include "mod/game/game_fault.h"
#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/guest.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"

namespace cdmp::session2 {
namespace {

namespace p = p8p;
namespace off = game::build_2976;
using gsr::BindResult;
using gsr::BindWhy;

// ---------------------------------------------------------------- the code (build 2976, all in .rsrc)
//
// The point (p8-impl-horarios-enlace.md §3), in the 2335 handler 0xC0AFC0 (RUNTIME_FUNCTION 0xC0AFC0-0xC0B83E):
//   0xC0B612 mov rdx,[rbp-0x28] ; mov rcx,rax ; call 0xA9FEE0 ; nop ; lea rax,[rip+..] (0x5912E60)
//   0xC0B626 mov [rbp-0x30],rax ; cmp byte [rbp-0x20],0   THE POINT (8 bytes, two whole instructions, no branch of
//            the function and no rel32 of .rsrc/.xtls into 0xC0B627..0xC0B62D, no RIP-relative operand)
//   0xC0B62E je 0xC0B63E ...   (the field holder [rbp-0x30] is released after the point)
// The frame: 0xC0AFD0 lea rbp,[rsp-0x10A0] (rbp not written again in the function); [rbp-0x38] = the scene key
// (0xC0B242), passed by address to 0xA9FEE0 and compared again here. 0xC0B5C1 (guest_seq_route's 2335 point) runs
// first in the same call: 0xC0B5C1..0xC0B626 is straight-line code whose only call is 0xA9FEE0.
constexpr uintptr_t kPoint = 0xC0B626;
constexpr uint8_t kPointBytes[] = {0x48, 0x89, 0x45, 0xD0, 0x80, 0x7D, 0xE0, 0x00,
                                   0x74, 0x0E, 0x48, 0x8B, 0x4D, 0xF8, 0x48, 0x8B};
constexpr uint8_t kCallBytes[] = {0x48, 0x8B, 0x55, 0xD8, 0x48, 0x8B, 0xC8, 0xE8, 0xC2, 0x48,
                                  0xE9, 0xFF, 0x90, 0x48, 0x8D, 0x05, 0x3A, 0x78, 0xD0, 0x04};  // 0xC0B612
constexpr uint8_t kFrameBytes[] = {0x48, 0x8D, 0xAC, 0x24, 0x60, 0xEF, 0xFF, 0xFF};             // 0xC0AFD0
constexpr uint8_t kKeyBytes[] = {0x48, 0x8B, 0x45, 0xA8, 0x48, 0x89, 0x45, 0xC8};               // 0xC0B242
constexpr uint8_t kArgKeyBytes[] = {0x48, 0x8D, 0x4D, 0xC8, 0x48, 0x89, 0x4C, 0x24, 0x30};       // 0xC0B5EC
constexpr uint32_t kFrameKey = 0x38;  // [rbp-0x38]

// The functions called (whole bodies, FNV-1a 64 of build 2976's bytes, compared before the point is installed):
//   0x9F2FA0 (0x9F2FA0-0x9F322F) the bind: (ClientSequencerPlayDataActorComponent*, const u64* scene key)
//   0x8AE1A0 (0x8AE1A0-0x8AE385) the actor manager's locked lookup (mgr, holder* out, u32 key): the manager's lock
//            +0x10, 0x464290, actor+0x5E & 0x10, a reference (actor vf24(4, 0x10)); holder {vt, actor +8, ref +0x10}
//   0x1434880 (0x1434880-0x143491C) the holder's destructor (drops the reference when holder+0x10)
//   0x43B4B0 (0x43B4B0-0x43B781) called by 0x9F2FA0: the node bind (node+0x28 = actor key if free) and, when the
//            stage is active and the actor ready (+0xC2, +0xCF), the stage's references to the actor's components
struct Body {
    uintptr_t rva, end;
    uint64_t fnv;
};
constexpr Body kBind{0x9F2FA0, 0x9F322F, 0xD15E3D78D6C57955ull};
constexpr Body kFind{0x8AE1A0, 0x8AE385, 0xCBE3B75431B9931Bull};
constexpr Body kRelease{0x1434880, 0x143491C, 0x3FA7949A2E381275ull};
constexpr Body kLink{0x43B4B0, 0x43B781, 0x06AD352D3DCC67B3ull};
constexpr uintptr_t kVtHolderDtor = 0x5725E48;  // what the game writes into a holder before 0x1434880 (0x9F3206)
constexpr uint32_t kTlsRealm = 0x1EC;           // the exe's TLS block byte: 0 client, 1 server (0xA9FF2A)

using BindFn = void(__fastcall*)(uintptr_t comp, const uint64_t* key);
using FindFn = void*(__fastcall*)(uintptr_t mgr, void* holder, uint32_t key);
using ReleaseFn = void(__fastcall*)(void* holder);

// Structure offsets (guest_seq_route.cpp; the disassembly of 0x9F2FA0 / 0x9ED290).
constexpr uintptr_t kVtStageManager = 0x55C9720;  // pa::ClientSequencerStageManager
constexpr uintptr_t kVtSeqPlayComp = 0x55BEE78;   // pa::ClientSequencerPlayDataActorComponent
constexpr uint32_t kActor_Key = 0x60, kActor_Comps = 0x68, kActor_Type = 0x88, kActor_C2 = 0xC2, kActor_CF = 0xCF;
// u16 actor state (guest_lifecycle.h: 0x10 logged in, 0x20 logging out, 0x40 released). 0x8AE1A0 refuses an actor
// without 0x10 (0x8AE2C9 test byte [rax+0x5e],0x10 ; je 0x8AE331: the empty holder). Session 12: 0x08 while loading.
constexpr uint32_t kActor_State = 0x5E;
constexpr uint16_t kStateLoggedIn = 0x10;
constexpr uint32_t kComps_Ctrl = 0x30, kComps_Ai = 0x58, kComps_Seq = 0xA8, kComps_Play = 0xB0, kSeqComp_Mgr = 0x18;
constexpr uint32_t kAi_X1 = 0x90, kX1_Handle = 0x08, kX1_Mode = 0x66;
constexpr uint32_t kComp_Actor = 0x08, kComp_Owner = 0x2D8, kComp_Key = 0x2E0, kComp_Stage = 0x318;
constexpr uint32_t kMgr_Sched = 0x148, kSched_Vec = 0x18, kSched_Count = 0x20;
constexpr uint32_t kStage_Mgr = 0x28, kStage_Key = 0x30, kStage_Active = 0x178, kStage_Nodes = 0x138,
                   kStage_NodeCount = 0x140, kNodeStride = 0x168, kNode_Npc = 0x28;
constexpr uint32_t kManager_Table = 0x08;
constexpr uint32_t kMgr_State = 0x231;  // a manager's load state (3 = loaded; guest_seq_play's M+0x231 gate)
constexpr uint8_t kTypeCharacter = 7;  // [[actor+0x88]+1] == 7: 0x9F2FA0's path A (0x9F2FDC)

uintptr_t g_base = 0;
std::atomic<uint8_t> g_mode{0};          // gsr::Mode, effective (Off when guest_sched_route != 1)
std::atomic<bool> g_on{false};           // the point is in
std::atomic<bool> g_latched{false};      // a fault inside a called game function: off for good
BindFn g_bind = nullptr;
FindFn g_find = nullptr;
ReleaseFn g_release = nullptr;
SafetyHookMid g_hook;
// Why the point is not in (written once by the configure / install thread, read by the diagnostics thread).
enum Refused : uint8_t { kRefNone, kRefNeedsRoute, kRefNo2335, kRefCode, kRefBody, kRefInstall };
std::atomic<uint8_t> g_refused{kRefNone};
std::atomic<uintptr_t> g_refused_rva{0};
std::string refused_text() {
    switch (g_refused.load()) {
        case kRefNeedsRoute: return "guest_sched_route is not 1";
        case kRefNo2335: return "guest_sched_route's 2335 point 0xC0B5C1 is not in (nothing would arm it)";
        case kRefCode: return "the handler's code around the point is not build 2976's";
        case kRefBody: return std::format("the body of exe+0x{:X} is not build 2976's", g_refused_rva.load());
        case kRefInstall: return "p8_install_mid refused it (see its line)";
        default: return "not installed yet";
    }
}
thread_local gsr::BindPending t_pending;  // set by 0xC0B5C1, consumed by 0xC0B626 (same call, same thread)

// Counters (the main thread writes, the diagnostics thread reads).
std::atomic<uint64_t> g_reached{0}, g_mismatch{0}, g_realm_bad{0}, g_no_stage{0}, g_no_table{0}, g_walks{0},
    g_truncated{0}, g_actors{0}, g_faults{0}, g_calls{0}, g_capped{0}, g_bound_in_x{0}, g_node_taken{0};
std::atomic<uint64_t> g_why[static_cast<size_t>(BindWhy::Count)] = {};
std::atomic<uint64_t> g_result[static_cast<size_t>(BindResult::Count)] = {};
std::atomic<uint64_t> g_ai_key_other{0};  // candidates whose AI scene key [X1] is not comp+0x2E0 (log only)
// p8-impl-horarios-enlace.md §9: the AI's X1+0x66 of every NPC BOUND here (0, 1, 2, other). 2 = as the 2874's native
// re-bind leaves it (0x20853D0 -> 0x1EB83D0); 0/1 = what vf13 wrote when it found no stage. 0x2086000 / 0x20863A0
// return an error unless it is 2.
std::atomic<uint64_t> g_mode66[4] = {};
std::atomic<uint64_t> g_owner_x{0};       // the character's X in the table is not the routed X: nothing called

// The retry queue (session 12, gsr::BindQueue): main thread only (0xC0B626 and the frame hook 0x8B6144 both run on
// it); g_queue_busy only guards against an unexpected second thread (the pass is skipped, never waited for).
gsr::BindQueue<gsr::kRetryCap> g_queue;
std::atomic<bool> g_queue_busy{false};
uint64_t g_last_pass_us = 0;         // main thread only
std::atomic<uint32_t> g_queue_n{0};  // a copy of g_queue.n for the diagnostics thread
std::atomic<uint64_t> g_queued{0}, g_queue_full{0}, g_passes_frame{0}, g_passes_2335{0}, g_pass_realm_bad{0},
    g_pass_busy{0}, g_retry_calls{0}, g_retry_refused{0}, g_retry_max_wait_ms{0}, g_frames_seen{0};
std::atomic<uint64_t> g_step[static_cast<size_t>(gsr::RetryStep::Count)] = {};
std::atomic<uint64_t> g_retry_result[static_cast<size_t>(BindResult::Count)] = {};
std::atomic<uint64_t> g_retry_active[2] = {};  // the stage's +0x178 at the retried calls: 0, 1

enum Kind : uint16_t { kECall, kECand, kEPass, kERetry, kKindCount };
p::EventRing<512> g_ring;
p::WindowLimiter g_lim[kKindCount] = {{64, 5'000'000}, {64, 5'000'000}, {32, 5'000'000}, {64, 5'000'000}};
std::atomic<uint64_t> g_limited{0}, g_lost{0}, g_lines{0};
constexpr uint64_t kMaxLines = 2000;
uint64_t g_ticks = 0;  // diagnostics thread only

// ---------------------------------------------------------------- reads (no CRT, no allocation)

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

struct GameMem {
    bool u32(uint64_t a, uint32_t& v) const noexcept { return rd(static_cast<uintptr_t>(a), v); }
    bool ptr(uint64_t a, uint64_t& v) const noexcept {
        uintptr_t p = 0;
        if (!rd(static_cast<uintptr_t>(a), p)) return false;
        v = p;
        return true;
    }
};

int tls_realm() noexcept {
    __try {
        const uintptr_t arr = static_cast<uintptr_t>(__readgsqword(0x58));
        if (!arr) return -1;
        const uintptr_t blk = *reinterpret_cast<const volatile uintptr_t*>(arr);
        if (!blk) return -1;
        return *reinterpret_cast<const volatile uint8_t*>(blk + kTlsRealm);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
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

// The stage with `key` in [X+0x148] (the vector 0xA9FEE0 pushes to; bounded), 0 if none.
uintptr_t stage_in_x(uintptr_t x, uint64_t key) noexcept {
    const uintptr_t c = x ? rdp(x + kMgr_Sched) : 0;
    const uintptr_t vec = c ? rdp(c + kSched_Vec) : 0;
    const uint32_t n = c ? rdv<uint32_t>(c + kSched_Count) : 0;
    if (!vec || !key || n > gsr::kMaxCensusStages) return 0;
    for (uint32_t i = 0; i < n; ++i) {
        const uintptr_t s = rdp(vec + static_cast<uintptr_t>(i) * 8);
        if (s && rdv<uint64_t>(s + kStage_Key) == key) return s;
    }
    return 0;
}

// A node of `stage` carries the actor's key (0x43B4B0's node+0x28).
bool node_has(uintptr_t stage, uint32_t key) noexcept {
    const uintptr_t nodes = stage ? rdp(stage + kStage_Nodes) : 0;
    const uint32_t n = stage ? rdv<uint32_t>(stage + kStage_NodeCount) : 0;
    if (!nodes || !key || n > gsr::kMaxCensusNodes) return false;
    for (uint32_t j = 0; j < n; ++j) {
        if (rdv<uint32_t>(nodes + static_cast<uintptr_t>(j) * kNodeStride + kNode_Npc) == key) return true;
    }
    return false;
}

// The actor's own stage manager [[[a+0x68]+0xA8]+0x18] (0x9F3105..0x9F3114 for the owner).
uintptr_t manager_of(uintptr_t a) noexcept {
    const uintptr_t comps = a ? rdp(a + kActor_Comps) : 0;
    const uintptr_t seq = comps ? rdp(comps + kComps_Seq) : 0;
    return seq ? rdp(seq + kSeqComp_Mgr) : 0;
}

// The playdata component of an actor ([[a+0x68]+0xB0], vtable checked, [comp+8] == a), 0 if none.
uintptr_t play_comp(uintptr_t a) noexcept {
    const uintptr_t comps = a ? rdp(a + kActor_Comps) : 0;
    const uintptr_t comp = comps ? rdp(comps + kComps_Play) : 0;
    return comp && rdp(comp) == g_base + kVtSeqPlayComp && rdp(comp + kComp_Actor) == a ? comp : 0;
}

struct AiView {
    bool path = false;   // the unchecked reads of 0x9F2FA0 have something to read
    uint8_t type = 0;    // [[a+0x88]+1]
    uint64_t x1_key = 0; // [X1] (the AI's scene key, 0x1EB83D0 / 0x20853D0)
    uint8_t x1_mode = 0; // [X1+0x66] (2 = as constructed / re-bound; vf13 sets 0/1 when it found no stage)
};
AiView ai_view(uintptr_t a) noexcept {
    AiView v;
    const uintptr_t t = rdp(a + kActor_Type);
    const uintptr_t comps = rdp(a + kActor_Comps);
    if (!t || !comps) return v;
    v.type = rdv<uint8_t>(t + 1);
    if (v.type == kTypeCharacter) {  // 0x9F3002: [[a+0x68]+0x30] then +0x50 (checked)
        v.path = rdp(comps + kComps_Ctrl) != 0;
        return v;
    }
    const uintptr_t ai = rdp(comps + kComps_Ai);           // 0x9F3071
    const uintptr_t x1 = ai ? rdp(ai + kAi_X1) : 0;        // 0x9F3075
    const uintptr_t h = x1 ? rdp(x1 + kX1_Handle) : 0;     // 0x9F307C -> 0x15EE200
    v.path = h != 0;
    v.x1_key = x1 ? rdv<uint64_t>(x1) : 0;
    v.x1_mode = x1 ? rdv<uint8_t>(x1 + kX1_Mode) : 0;
    return v;
}

// What bind_judge needs; the binding and the AI path only for this owner's NPCs of this scene (the walk passes over
// every actor of the client).
gsr::BindActor read_actor(uint32_t key, uintptr_t a, uint32_t owner, uint64_t scene, uintptr_t* comp_out) noexcept {
    gsr::BindActor b;
    b.key = key;
    const uintptr_t comp = key && key != owner ? play_comp(a) : 0;
    if (comp_out) *comp_out = comp;
    if (!comp) return b;
    b.comp = true;
    b.owner = rdv<uint32_t>(comp + kComp_Owner);
    if (b.owner != owner) return b;
    b.scene = rdv<uint64_t>(comp + kComp_Key);
    if (b.scene != scene) return b;
    b.bound = rdp(comp + kComp_Stage);
    b.ai_path = ai_view(a).path;
    return b;
}

// ---------------------------------------------------------------- the calls (the main thread)

// P8 review I1 (game_fault.h): an exception inside the game's code is the game's (an actor lock, the manager's lock
// or the stage container's may be held): the feature latches off, one ERR line goes straight to the log, it is
// passed on.
int call_fault(EXCEPTION_POINTERS* ep, const char* where) noexcept {
    g_latched.store(true);
    g_faults.fetch_add(1);
    return game::game_fault_continue(ep, "GuestSchedBind", where);
}
struct alignas(16) Holder {
    uint8_t b[0x20] = {};
};
bool guarded_find(uintptr_t mgr, Holder* h, uint32_t key) noexcept {
    __try {
        g_find(mgr, h, key);
        return true;
    } __except (call_fault(GetExceptionInformation(), "0x8AE1A0(actor manager, &holder, key) took an NPC")) {
        return false;  // never reached: the filter passes it on
    }
}
bool guarded_bind(uintptr_t comp, const uint64_t* key) noexcept {
    __try {
        g_bind(comp, key);
        return true;
    } __except (call_fault(GetExceptionInformation(), "0x9F2FA0(component, &scene key) bound an NPC to its stage")) {
        return false;
    }
}
bool guarded_release(Holder* h) noexcept {
    __try {
        *reinterpret_cast<uintptr_t*>(h->b) = g_base + kVtHolderDtor;  // as 0x9F3206..0x9F320D before the call
        g_release(h);
        return true;
    } __except (call_fault(GetExceptionInformation(), "0x1434880(&holder) released an NPC's reference")) {
        return false;
    }
}

struct Cand {
    uint32_t key;
    uintptr_t actor, comp;
};

bool logged_in(uintptr_t a) noexcept { return a && (rdv<uint16_t>(a + kActor_State) & kStateLoggedIn) != 0; }

uint64_t npc_flags(uintptr_t actor, uint8_t active, const AiView& ai) noexcept {
    return active | static_cast<uint64_t>(ai.type) << 16 | static_cast<uint64_t>(ai.x1_mode) << 24 |
           static_cast<uint64_t>(rdv<uint8_t>(actor + kActor_C2)) << 32 |
           static_cast<uint64_t>(rdv<uint8_t>(actor + kActor_CF)) << 40;
}

// One native call: 0x8AE1A0 (a reference on the NPC; an empty holder while it is not logged in), the re-check,
// 0x9F2FA0(comp, &scene), comp+0x318 read back, 0x1434880 (also for an empty holder, as 0x9F3206 does).
// false only after a fault inside the game (never: the filter passes it on).
struct CallOut {
    BindResult r = BindResult::NotFound;
    uint64_t after = 0;
    bool called = false;
    bool node = false;  // after a Bound: a node of the stage carries the NPC
};
bool call_one(uintptr_t mgr, const Cand& c, uint32_t owner, uint64_t scene, uintptr_t x, uintptr_t stage,
              CallOut& o) noexcept {
    o = CallOut{};
    Holder h;
    if (!guarded_find(mgr, &h, c.key)) return false;
    const uintptr_t got = *reinterpret_cast<const uintptr_t*>(h.b + 8);
    const bool ref = h.b[0x10] != 0;
    if (ref && got == c.actor) {
        uintptr_t comp = 0;
        const gsr::BindActor again = read_actor(c.key, c.actor, owner, scene, &comp);
        if (comp != c.comp || gsr::bind_judge(again, owner, scene) != BindWhy::Candidate ||
            rdv<uint32_t>(c.actor + kActor_Key) != c.key) {
            o.r = BindResult::Changed;
        } else {
            const uint64_t key = scene;  // 0x9F2FA0 only reads [rdx] (0x9F3122 -> [X+0x148]->vf8)
            o.called = true;
            g_calls.fetch_add(1, std::memory_order_relaxed);
            if (!guarded_bind(c.comp, &key)) return false;  // never reached
            o.after = rdp(c.comp + kComp_Stage);
            o.r = gsr::bind_result(o.after, o.after ? rdv<uint64_t>(o.after + kStage_Key) : 0,
                                   o.after ? rdp(o.after + kStage_Mgr) : 0, scene, x);
            o.node = o.r == BindResult::Bound && node_has(stage, c.key);
        }
    }
    return guarded_release(&h);
}

struct QueueLock {
    bool ok;
    QueueLock() noexcept : ok(!g_queue_busy.exchange(true, std::memory_order_acquire)) {}
    ~QueueLock() {
        if (ok) g_queue_busy.store(false, std::memory_order_release);
    }
};

// P8 re-entry (s25/reentrada.md §1.4): the queue holds X pointers and owner keys of one remote conversation (the host
// character's key 0xA0100001 is reused by the next one). Under QueueLock, on the main thread (its two users).
uint32_t g_queue_conv = 0;
void queue_conversation_check() noexcept {
    const uint32_t conv = guest_conversation_id();
    if (conv == g_queue_conv) return;
    g_queue_conv = conv;
    if (conv < 2 || g_queue.n == 0) return;
    const uint32_t dropped = g_queue.n;
    g_queue.n = 0;
    g_queue_n.store(0, std::memory_order_relaxed);
    try {
        log::info("GuestSchedBind: remote conversation {} (re-entry): {} queued NPC binding(s) of the previous "
                  "conversation dropped",
                  conv, dropped);
    } catch (...) {
    }
}

// 0xC0B626: an NPC (or its owner) not logged in yet goes to the retry queue. false: the queue is full (or busy).
bool queue_npc(uint32_t key, uint32_t owner, uint64_t scene, uintptr_t x) noexcept {
    QueueLock lk;
    if (!lk.ok) return false;
    queue_conversation_check();
    const uint32_t before = g_queue.n;
    gsr::BindRetry r;
    r.key = key;
    r.owner = owner;
    r.scene = scene;
    r.x = x;
    r.since_us = p8_now_us();
    const bool ok = g_queue.add(r);
    if (!ok) g_queue_full.fetch_add(1, std::memory_order_relaxed);
    if (g_queue.n > before) g_queued.fetch_add(1, std::memory_order_relaxed);
    g_queue_n.store(g_queue.n, std::memory_order_relaxed);
    return ok;
}

// M+0x231 as the last pass read it (3 = this client's own load is over; -1 = no M / not a stage manager): the
// summary only. Each pass reads it from ITS M (the frame hook's RAX, or the M the 0xC0B5C1 hook saw for that 2335):
// review of 51f9599, never a value left over from before a field reload or from no frame at all.
std::atomic<int> g_m_state{-1};
uint32_t g_rotate = 0;  // main thread only: where the next pass's calls start (gsr::retry_rotated)

int m_load_state(uintptr_t m) noexcept {
    return m && rdp(m) == g_base + kVtStageManager ? rdv<uint8_t>(m + kMgr_State) : -1;
}

// One retry pass over the queue (the main thread: the frame hook 0x8B6144 or a routed 2335's 0xC0B626), at most
// every gsr::kRetryPeriodUs, at most gsr::kRetryCallsPerPass native calls. One bounded walk of the client actor table
// finds the queued NPCs and their owners; each NPC is judged again (gsr::retry_judge) and called as at 0xC0B626.
// Review of 51f9599: every entry is judged on every pass (the timeout applies even without a whole table or a
// known owner); the calls start where the last pass stopped; a call that keeps failing is dropped
// (gsr::retry_keep_after).
void retry_pass(bool from_frame, uintptr_t m) noexcept {
    if (static_cast<gsr::Mode>(g_mode.load(std::memory_order_relaxed)) != gsr::Mode::On ||
        g_latched.load(std::memory_order_relaxed) || g_queue_n.load(std::memory_order_relaxed) == 0) {
        return;
    }
    QueueLock lk;
    if (!lk.ok) {
        g_pass_busy.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    queue_conversation_check();  // P8 re-entry
    if (g_queue.n == 0) return;
    const uint64_t now = p8_now_us();
    if (!gsr::retry_due(now, g_last_pass_us)) return;
    g_last_pass_us = now;
    (from_frame ? g_passes_frame : g_passes_2335).fetch_add(1, std::memory_order_relaxed);
    const bool realm_ok = tls_realm() == 0;
    if (!realm_ok) g_pass_realm_bad.fetch_add(1, std::memory_order_relaxed);
    const int m_state = m_load_state(m);
    g_m_state.store(m_state, std::memory_order_relaxed);
    const bool load_done = m_state == 3;
    const uintptr_t world = realm_ok ? rdp(g_base + off::kWorldSystemSlot) : 0;
    const uintptr_t mgr = world ? rdp(world + off::kWorldSystem_ActorManager) : 0;
    const uintptr_t table = mgr ? rdp(mgr + kManager_Table) : 0;
    // The queued keys, sorted (main thread only: static storage, no allocation, no big stack frame).
    struct KeyAt {
        uint32_t key;
        uint32_t i;
    };
    static KeyAt keys[gsr::kRetryCap];
    static uintptr_t seen[gsr::kRetryCap], stage_of[gsr::kRetryCap], comp_of[gsr::kRetryCap];
    static gsr::RetryStep step_of[gsr::kRetryCap];
    static bool drop[gsr::kRetryCap];
    const uint32_t n = g_queue.n;
    for (uint32_t i = 0; i < n; ++i) {
        keys[i] = KeyAt{g_queue.e[i].key, i};
        seen[i] = stage_of[i] = comp_of[i] = 0;
        drop[i] = false;
    }
    std::sort(keys, keys + n, [](const KeyAt& a, const KeyAt& b) { return a.key < b.key; });
    constexpr uint32_t kOwners = 8;  // the host's character (one in practice); more: those entries only time out
    uint32_t owner_key[kOwners] = {};
    uintptr_t owner_actor[kOwners] = {};
    uint32_t owners = 0;
    for (uint32_t i = 0; i < n && owners < kOwners; ++i) {
        const uint32_t o = g_queue.e[i].owner;
        if (std::find(owner_key, owner_key + owners, o) == owner_key + owners) owner_key[owners++] = o;
    }
    gsr::WalkStats ws;
    if (table) {
        ws = gsr::walk_actor_table(GameMem{}, table, gsr::WalkLimits{}, [&](uint32_t key, uint64_t a) {
            for (uint32_t j = 0; j < owners; ++j) {
                if (owner_key[j] == key) owner_actor[j] = static_cast<uintptr_t>(a);
            }
            const KeyAt* it =
                std::lower_bound(keys, keys + n, key, [](const KeyAt& k, uint32_t v) { return k.key < v; });
            if (it != keys + n && it->key == key) seen[it->i] = static_cast<uintptr_t>(a);
            return true;
        });
    }
    // A partial view would drop live NPCs as "gone": then only the timeout applies.
    const bool table_ok = realm_ok && table && ws.ok && !ws.truncated;
    auto event_of = [&](uint32_t k, gsr::RetryStep step) {
        const gsr::BindRetry& r = g_queue.e[k];
        p::Event e;
        e.flags = from_frame ? 1 : 0;
        e.v[1] = r.key | static_cast<uint64_t>(r.owner) << 32;
        e.v[2] = r.scene;
        e.v[3] = comp_of[k];
        e.v[4] = stage_of[k];
        e.v[8] = r.x;
        e.v[9] = (now - r.since_us) / 1000;
        e.v[10] = r.tries;
        e.v[11] = static_cast<uint64_t>(step);
        return e;
    };
    auto finish = [&](uint32_t k, gsr::RetryStep step, p::Event& e) {
        g_step[static_cast<size_t>(step)].fetch_add(1, std::memory_order_relaxed);
        const uint64_t waited = e.v[9];
        uint64_t prev = g_retry_max_wait_ms.load(std::memory_order_relaxed);
        while (waited > prev && !g_retry_max_wait_ms.compare_exchange_weak(prev, waited, std::memory_order_relaxed)) {
        }
        emit(kERetry, e);
        drop[k] = true;
    };
    // 1. Judge every entry (reads only).
    for (uint32_t k = 0; k < n; ++k) {
        gsr::BindRetry& r = g_queue.e[k];
        const uintptr_t a = seen[k];
        gsr::RetryFacts f;
        f.table_ok = table_ok;
        f.found = a != 0;
        if (table_ok && f.found) {
            const uint32_t* oj = std::find(owner_key, owner_key + owners, r.owner);
            const uintptr_t oa = oj != owner_key + owners ? owner_actor[oj - owner_key] : 0;
            f.owner_known = oa != 0;  // not tracked / not in the table this pass: no call, no OwnerMoved
            f.actor = read_actor(r.key, a, r.owner, r.scene, &comp_of[k]);
            f.logged_in = logged_in(a);
            f.owner_logged_in = logged_in(oa);
            f.load_done = load_done;
            stage_of[k] = rdp(static_cast<uintptr_t>(r.x)) == g_base + kVtStageManager
                              ? stage_in_x(static_cast<uintptr_t>(r.x), r.scene)
                              : 0;
            f.stage_in_x = stage_of[k] != 0;
            f.owner_x_ok = oa && manager_of(oa) == static_cast<uintptr_t>(r.x);
        }
        const gsr::RetryStep step = gsr::retry_judge(r, f, now);
        step_of[k] = step;
        if (step == gsr::RetryStep::Wait) {
            ++r.tries;
        } else if (step != gsr::RetryStep::Call) {
            p::Event e = event_of(k, step);
            finish(k, step, e);
        }
    }
    // 2. The calls, from where the last pass stopped.
    uint32_t calls = 0;
    const uint32_t start = g_rotate;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t k = gsr::retry_rotated(start, n, i);
        if (step_of[k] != gsr::RetryStep::Call) continue;
        if (calls >= gsr::kRetryCallsPerPass) {
            g_rotate = k;  // the next pass starts here
            break;
        }
        ++calls;
        gsr::BindRetry& r = g_queue.e[k];
        const uintptr_t a = seen[k];
        const uintptr_t stage = stage_of[k];
        g_retry_calls.fetch_add(1, std::memory_order_relaxed);
        const AiView ai = ai_view(a);
        const uint8_t active = rdv<uint8_t>(stage + kStage_Active);
        uint64_t flags = npc_flags(a, active, ai);
        CallOut o;
        const Cand c{r.key, a, comp_of[k]};
        if (!call_one(mgr, c, r.owner, r.scene, static_cast<uintptr_t>(r.x), stage, o)) return;  // never reached
        if (o.r == BindResult::NotFound) g_retry_refused.fetch_add(1, std::memory_order_relaxed);
        gsr::RetryStep why = gsr::RetryStep::Call;
        if (gsr::retry_keep_after(r, o.r, why)) continue;  // refused / still 0: again on a later pass
        g_retry_result[static_cast<size_t>(o.r)].fetch_add(1, std::memory_order_relaxed);
        if (o.called) g_retry_active[active ? 1 : 0].fetch_add(1, std::memory_order_relaxed);
        if (o.r == BindResult::Bound) {
            g_mode66[ai.x1_mode < 3 ? ai.x1_mode : 3].fetch_add(1, std::memory_order_relaxed);
            if (o.node) {
                g_node_taken.fetch_add(1, std::memory_order_relaxed);
                flags |= 1ull << 8;
            }
        }
        p::Event e = event_of(k, why);
        e.v[0] = static_cast<uint64_t>(o.r);
        e.v[5] = o.after;
        e.v[6] = flags;
        e.v[7] = ai.x1_key;
        finish(k, why, e);
    }
    // 3. What is done leaves the queue.
    g_queue.remove_marked(drop);
    if (g_rotate >= g_queue.n) g_rotate = 0;
    g_queue_n.store(g_queue.n, std::memory_order_relaxed);
}

void on_point(safetyhook::Context& ctx) noexcept {
    const gsr::BindPending pend = t_pending;
    t_pending = gsr::BindPending{};  // consumed: one 2335, one chance
    const auto mode = static_cast<gsr::Mode>(g_mode.load(std::memory_order_relaxed));
    if (mode == gsr::Mode::Off || g_latched.load(std::memory_order_relaxed) || !pend.armed) return;
    g_reached.fetch_add(1, std::memory_order_relaxed);
    // The queue, when no frame hook has done it lately (rate-limited, the main thread); M = what this 2335's vf48 gave.
    retry_pass(false, static_cast<uintptr_t>(pend.m));
    if (g_latched.load(std::memory_order_relaxed)) return;
    if (!gsr::bind_pending_matches(pend, ctx.rbp, rdv<uint64_t>(ctx.rbp - kFrameKey))) {
        g_mismatch.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const uintptr_t x = static_cast<uintptr_t>(pend.x);
    // 0x9F2FA0 re-derives X from the owner and calls [X+0x148]->vf8 without a null check: X must be what the route
    // hook saw and its container must hold the stage 0xA9FEE0 just made.
    const uintptr_t stage = rdp(x) == g_base + kVtStageManager ? stage_in_x(x, pend.key) : 0;
    p::Event pass;
    pass.v[1] = pend.owner | static_cast<uint64_t>(pend.resource) << 32;
    pass.v[2] = pend.key;
    pass.v[4] = stage;
    pass.v[5] = x;
    if (!stage) {
        g_no_stage.fetch_add(1, std::memory_order_relaxed);
        pass.v[0] = 1;  // no stage
        emit(kEPass, pass);
        return;
    }
    if (tls_realm() != 0) {
        g_realm_bad.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const uintptr_t world = rdp(g_base + off::kWorldSystemSlot);
    const uintptr_t mgr = world ? rdp(world + off::kWorldSystem_ActorManager) : 0;  // 0x9F30EA..0x9F30F1
    const uintptr_t table = mgr ? rdp(mgr + kManager_Table) : 0;
    Cand cands[gsr::kBindCap];
    uint32_t capped_keys[gsr::kBindCap] = {};  // the first candidates over the cap (the retry calls them)
    uint32_t n = 0;
    gsr::BindTally t;
    uintptr_t owner_x = 0;  // the character's X as the table gives it (0x9F2FA0 finds the owner in this table)
    uintptr_t owner_actor = 0;
    const gsr::WalkStats ws = gsr::walk_actor_table(GameMem{}, table, gsr::WalkLimits{}, [&](uint32_t key, uint64_t a) {
        if (key == pend.owner) {
            owner_actor = static_cast<uintptr_t>(a);
            owner_x = manager_of(owner_actor);
        }
        uintptr_t comp = 0;
        const gsr::BindActor b = read_actor(key, static_cast<uintptr_t>(a), pend.owner, pend.key, &comp);
        const BindWhy why = gsr::bind_judge(b, pend.owner, pend.key);
        // Only what concerns this scene is counted (the others are every actor of the client).
        if (why == BindWhy::AlreadyBound || why == BindWhy::NoAiPath || why == BindWhy::Candidate) {
            ++t.why[static_cast<size_t>(why)];
        }
        if (why == BindWhy::AlreadyBound && b.bound == stage) ++t.bound_in_x;
        if (why != BindWhy::Candidate) return true;
        if (n < gsr::kBindCap) {
            cands[n++] = Cand{key, static_cast<uintptr_t>(a), comp};
        } else {
            ++t.capped;
            if (t.capped <= gsr::kBindCap) capped_keys[t.capped - 1] = key;  // queued below (mode 1, the same X)
        }
        return true;
    });
    const bool owner_in = logged_in(owner_actor);
    g_walks.fetch_add(1, std::memory_order_relaxed);
    g_actors.fetch_add(ws.actors, std::memory_order_relaxed);
    if (!ws.ok) g_no_table.fetch_add(1, std::memory_order_relaxed);
    if (ws.truncated) g_truncated.fetch_add(1, std::memory_order_relaxed);
    const uint8_t active = rdv<uint8_t>(stage + kStage_Active);
    // 0x9F2FA0 calls [X'+0x148]->vf8 with X' = the owner's manager as ITS lookup gives it, unchecked: only when that is
    // the X whose container holds the stage (checked above). No candidate without a table (and so without mgr).
    if (n && (!mgr || owner_x != x)) {
        g_owner_x.fetch_add(1, std::memory_order_relaxed);
        n = 0;
    } else if (mode == gsr::Mode::On) {
        // Over the cap: not called now; the retry pass calls them (it re-judges everything).
        for (uint32_t i = 0; i < t.capped && i < gsr::kBindCap; ++i) queue_npc(capped_keys[i], pend.owner, pend.key, x);
    }
    for (uint32_t i = 0; i < n; ++i) {
        const Cand& c = cands[i];
        const AiView ai = ai_view(c.actor);
        if (ai.x1_key && ai.x1_key != pend.key) g_ai_key_other.fetch_add(1, std::memory_order_relaxed);
        p::Event e;
        e.v[1] = c.key | static_cast<uint64_t>(pend.owner) << 32;
        e.v[2] = pend.key;
        e.v[3] = c.comp;
        e.v[4] = stage;
        e.v[7] = ai.x1_key;
        e.v[8] = x;
        uint64_t flags = npc_flags(c.actor, active, ai) | static_cast<uint64_t>(logged_in(c.actor)) << 48 |
                         static_cast<uint64_t>(owner_in) << 49;
        if (mode != gsr::Mode::On) {
            e.v[6] = flags;
            emit(kECand, e);
            continue;
        }
        // Session 12: while the field loads, the NPC (u16 +0x5E 0x08) and the character are not logged in: the game's
        // lookup 0x8AE1A0 refuses the NPC (our reference) and 0x9F2FA0's own lookup of the owner would return early.
        // Such an NPC is queued; retry_pass calls it once both are logged in (and the client's own load is over).
        CallOut o;
        if (!logged_in(c.actor) || !owner_in) {
            o.r = queue_npc(c.key, pend.owner, pend.key, x) ? BindResult::Deferred : BindResult::NotFound;
        } else {
            // The game's own locked lookup: a reference on the actor for the call (0x9F2FA0 takes its owner so).
            if (!call_one(mgr, c, pend.owner, pend.key, x, stage, o)) return;  // never reached (the fault is passed on)
            if (o.r == BindResult::NotFound && queue_npc(c.key, pend.owner, pend.key, x)) o.r = BindResult::Deferred;
            if (o.called) ++t.calls;
            if (o.r == BindResult::Bound) {
                g_mode66[ai.x1_mode < 3 ? ai.x1_mode : 3].fetch_add(1, std::memory_order_relaxed);
                if (o.node) {
                    ++t.node_taken;
                    flags |= 1ull << 8;
                }
            }
        }
        ++t.result[static_cast<size_t>(o.r)];
        e.v[0] = static_cast<uint64_t>(o.r);
        e.v[5] = o.after;
        e.v[6] = flags;
        emit(kECall, e);
    }
    for (size_t k = 0; k < static_cast<size_t>(BindWhy::Count); ++k) {
        if (t.why[k]) g_why[k].fetch_add(t.why[k], std::memory_order_relaxed);
    }
    for (size_t k = 0; k < static_cast<size_t>(BindResult::Count); ++k) {
        if (t.result[k]) g_result[k].fetch_add(t.result[k], std::memory_order_relaxed);
    }
    g_capped.fetch_add(t.capped, std::memory_order_relaxed);
    g_bound_in_x.fetch_add(t.bound_in_x, std::memory_order_relaxed);
    g_node_taken.fetch_add(t.node_taken, std::memory_order_relaxed);
    const uint32_t cand = t.why[static_cast<size_t>(BindWhy::Candidate)];
    if (cand || t.why[static_cast<size_t>(BindWhy::AlreadyBound)] || t.why[static_cast<size_t>(BindWhy::NoAiPath)]) {
        pass.v[3] = ws.entries | static_cast<uint64_t>(ws.buckets) << 32;
        pass.v[6] = cand | static_cast<uint64_t>(t.why[static_cast<size_t>(BindWhy::AlreadyBound)]) << 16 |
                    static_cast<uint64_t>(t.bound_in_x) << 32 |
                    static_cast<uint64_t>(t.why[static_cast<size_t>(BindWhy::NoAiPath)]) << 48;
        pass.v[7] = t.calls | static_cast<uint64_t>(t.result[static_cast<size_t>(BindResult::Bound)]) << 16 |
                    static_cast<uint64_t>(t.result[static_cast<size_t>(BindResult::StillZero)]) << 32 |
                    static_cast<uint64_t>(t.capped) << 48;
        pass.v[8] = active;
        emit(kEPass, pass);
    }
}

// ---------------------------------------------------------------- lines (diagnostics thread)

std::string line_of(const p::Event& e) {
    const uint64_t* v = e.v;
    const std::string when = p8_when(e.qpc_us);
    if (e.kind == kEPass) {
        if (v[0] == 1) {
            return std::format("GuestSchedBind [2335 0xC0B626] {} T{} | scene 0x{:X} of character 0x{:08X} (resource "
                               "0x{:08X}) | X 0x{:X}: NO stage with that key in X+0x148 after 0xA9FEE0 (its schedule "
                               "file not in the container at this time: see 'GuestSchedLoad [lookup miss]'; or "
                               "deduplicated by wake spot / another kind): nothing to bind",
                               when, e.tid, v[2], static_cast<uint32_t>(v[1]), static_cast<uint32_t>(v[1] >> 32), v[5]);
        }
        return std::format(
            "GuestSchedBind [2335 0xC0B626] {} T{} | scene 0x{:X} of character 0x{:08X} (resource 0x{:08X}) | stage "
            "0x{:X} in X 0x{:X} (active +0x178 {}) | table: {} entries in {} buckets | its NPCs: {} with comp+0x318 0, "
            "{} already bound ({} to this stage: vf13 found it), {} without an AI node path | called {}: bound {}, "
            "still 0 {}; over the cap {}",
            when, e.tid, v[2], static_cast<uint32_t>(v[1]), static_cast<uint32_t>(v[1] >> 32), v[4], v[5], v[8],
            static_cast<uint32_t>(v[3]), static_cast<uint32_t>(v[3] >> 32), v[6] & 0xFFFF, (v[6] >> 16) & 0xFFFF,
            (v[6] >> 32) & 0xFFFF, v[6] >> 48, v[7] & 0xFFFF, (v[7] >> 16) & 0xFFFF, (v[7] >> 32) & 0xFFFF, v[7] >> 48);
    }
    if (e.kind == kERetry) {
        const auto step = static_cast<gsr::RetryStep>(v[11]);
        const std::string head = std::format(
            "GuestSchedBind [retry {}] {} T{} | NPC 0x{:08X} (owner 0x{:08X}, scene 0x{:X}, X 0x{:X}) | queued {} ms ago, "
            "{} passes not ready",
            e.flags ? "frame 0x8B6144" : "2335 0xC0B626", when, e.tid, static_cast<uint32_t>(v[1]),
            static_cast<uint32_t>(v[1] >> 32), v[2], v[8], v[9], v[10]);
        if (step != gsr::RetryStep::Call) return std::format("{} | dropped: {}", head, gsr::retry_step_name(step));
        const uint64_t f = v[6];
        const auto r = static_cast<BindResult>(v[0]);
        return std::format(
            "{} | comp 0x{:X} stage 0x{:X} (active +0x178 {}) | actor type {}, +0xC2 {}, +0xCF {}, AI mode [X1+0x66] {} | "
            "0x9F2FA0: {}{}{}",
            head, v[3], v[4], f & 0xFF, (f >> 16) & 0xFF, (f >> 32) & 0xFF, (f >> 40) & 0xFF, (f >> 24) & 0xFF,
            gsr::bind_result_name(r), v[5] ? std::format(": comp+0x318 0x{:X}", v[5]) : std::string(),
            r == BindResult::Bound ? ((f >> 8) & 1 ? ", a node of it now carries the NPC (0x43B4B0)"
                                                   : ", NO node carries the NPC (its node was taken)")
                                   : std::string());
    }
    const uint64_t f = v[6];
    const std::string npc = std::format(
        "NPC 0x{:08X} comp 0x{:X} (owner 0x{:08X}, scene 0x{:X}) | stage 0x{:X} in X 0x{:X}, active +0x178 {} | actor "
        "type {}, +0xC2 {}, +0xCF {}, logged in (+0x5E & 0x10) {}, its owner {} | AI scene key [X1] 0x{:X}{}, AI mode "
        "[X1+0x66] {}",
        static_cast<uint32_t>(v[1]), v[3], static_cast<uint32_t>(v[1] >> 32), v[2], v[4], v[8], f & 0xFF,
        (f >> 16) & 0xFF, (f >> 32) & 0xFF, (f >> 40) & 0xFF, (f >> 48) & 1, (f >> 49) & 1, v[7],
        v[7] && v[7] != v[2] ? " (NOT this scene)" : "", (f >> 24) & 0xFF);
    if (e.kind == kECand) {
        return std::format("GuestSchedBind [probe 0xC0B626] {} T{} | {} | WOULD call 0x9F2FA0(comp, &scene)", when,
                           e.tid, npc);
    }
    const auto r = static_cast<BindResult>(v[0]);
    return std::format("GuestSchedBind [0x9F2FA0] {} T{} | {} | {}{}{}", when, e.tid, npc, gsr::bind_result_name(r),
                       v[5] ? std::format(": comp+0x318 0x{:X}", v[5]) : std::string(),
                       r == BindResult::Bound ? ((f >> 8) & 1 ? ", a node of it now carries the NPC (0x43B4B0)"
                                                              : ", NO node carries the NPC (its node was taken)")
                                              : std::string());
}

}  // namespace

void guest_sched_bind_configure(const std::string& mode, gsr::Mode route_mode) {
    bool bad = false, refused = false;
    const gsr::Mode m = gsr::bind_mode_of(route_mode, gsr::parse_mode(mode, &bad), &refused);
    if (bad) log::warn("GuestSchedBind: guest_sched_bind={} is not 0, probe or 1: off", mode);
    if (refused) {
        g_refused.store(kRefNeedsRoute);
        log::warn("GuestSchedBind: guest_sched_bind={} needs guest_sched_route=1 (a stage in X to bind to): off", mode);
    }
    g_mode.store(static_cast<uint8_t>(m));
}

void guest_sched_bind_install(uintptr_t base, std::span<const gad::Section> secs, bool p2335_on) {
    const auto mode = static_cast<gsr::Mode>(g_mode.load());
    if (mode == gsr::Mode::Off) return;
    g_base = base;
    Refused why = kRefNone;
    auto bytes_at = [&](uintptr_t rva, std::span<const uint8_t> expected) {
        std::vector<uint8_t> actual(expected.size());
        return gad::in_real_code(secs, static_cast<uint32_t>(rva), static_cast<uint32_t>(expected.size())) &&
               game::safe_read(base + rva, actual.data(), actual.size()) &&
               std::equal(actual.begin(), actual.end(), expected.begin());
    };
    auto body_is = [&](const Body& b) {
        std::vector<uint8_t> v(b.end - b.rva);
        if (!gad::in_real_code(secs, static_cast<uint32_t>(b.rva), static_cast<uint32_t>(v.size())) ||
            !game::safe_read(base + b.rva, v.data(), v.size())) {
            return false;
        }
        uint64_t h = 0xCBF29CE484222325ull;
        for (uint8_t c : v) h = (h ^ c) * 0x100000001B3ull;
        return h == b.fnv;
    };
    if (!p2335_on) {
        why = kRefNo2335;
    } else if (!bytes_at(0xC0B612, kCallBytes) || !bytes_at(0xC0AFD0, kFrameBytes) || !bytes_at(0xC0B242, kKeyBytes) ||
               !bytes_at(0xC0B5EC, kArgKeyBytes)) {
        why = kRefCode;
    } else {
        for (const Body* b : {&kBind, &kFind, &kRelease, &kLink}) {
            if (!body_is(*b)) {
                g_refused_rva.store(b->rva);
                why = kRefBody;
                break;
            }
        }
    }
    if (why == kRefNone) {
        g_bind = reinterpret_cast<BindFn>(base + kBind.rva);
        g_find = reinterpret_cast<FindFn>(base + kFind.rva);
        g_release = reinterpret_cast<ReleaseFn>(base + kRelease.rva);
        if (!p8_install_mid(base, secs, kPoint, 8, kPointBytes, on_point, g_hook, "GuestSchedBind",
                            "after 0xA9FEE0 in the 2335 handler 0xC0B626")) {
            why = kRefInstall;
        }
    }
    g_refused.store(why);
    g_on.store(why == kRefNone, std::memory_order_release);
    if (why != kRefNone) {
        log::error("GuestSchedBind: NOT installed ({}): no NPC is re-bound", refused_text());
        return;
    }
    log::info("GuestSchedBind {} (guest_sched_bind): point 0xC0B626 (after 0xA9FEE0 in the 2335 handler) | for a 2335 "
              "routed into X whose stage is now in X+0x148: the host character's NPCs of that scene with comp+0x318 0 "
              "(client actor table, at most {} per 2335) {} | bodies checked: 0x9F2FA0 0x8AE1A0 0x1434880 0x43B4B0",
              mode == gsr::Mode::On ? "ON" : "PROBE (lists them, calls nothing)", gsr::kBindCap,
              mode == gsr::Mode::On ? "-> 0x8AE1A0 (a reference), 0x9F2FA0(comp, &scene), comp+0x318 read back, 0x1434880; "
                                      "an NPC or owner not logged in yet (+0x5E & 0x10) is queued and retried from the "
                                      "frame hook 0x8B6144 (guest_seq_play) once both are and M+0x231 == 3, else at the "
                                      "next routed 2335 (every 250 ms at most, 32 calls per pass, 120 s per NPC)"
                                    : "are logged");
}

void guest_sched_bind_frame(uintptr_t m) noexcept {
    if (!g_on.load(std::memory_order_relaxed)) return;
    g_frames_seen.fetch_add(1, std::memory_order_relaxed);
    // This client's own manager M (RAX at 0x8B6144, from the field's vf48): the pass reads its +0x231 now.
    retry_pass(true, m);
}

void guest_sched_bind_note(bool routed, uint64_t rbp, uint64_t key, uint64_t x, uint32_t owner, uint32_t resource,
                           uint64_t m) noexcept {
    gsr::BindPending p;
    if (routed && g_on.load(std::memory_order_relaxed)) {
        p.armed = true;
        p.rbp = rbp;
        p.key = key;
        p.x = x;
        p.owner = owner;
        p.resource = resource;
        p.m = m;
    }
    t_pending = p;  // every 2335 resets it: a stale one never survives into another call
}

void guest_sched_bind_tick() noexcept {
    try {
        if (g_mode.load() == 0) return;
        ++g_ticks;
        p::Event e;
        while (g_ring.pop(e)) {
            if (e.kind >= kKindCount) continue;
            if (g_lines.fetch_add(1) < kMaxLines) log::info("{}", line_of(e));
        }
        if (g_ticks % 12 != 0) return;
        std::string whys, results;
        for (size_t k = 0; k < static_cast<size_t>(BindWhy::Count); ++k) {
            if (const uint64_t c = g_why[k].load()) {
                whys += std::format(" {} {};", gsr::bind_why_name(static_cast<BindWhy>(k)), c);
            }
        }
        for (size_t k = 0; k < static_cast<size_t>(BindResult::Count); ++k) {
            if (const uint64_t c = g_result[k].load()) {
                results += std::format(" {} {};", gsr::bind_result_name(static_cast<BindResult>(k)), c);
            }
        }
        const auto mode = static_cast<gsr::Mode>(g_mode.load());
        log::info("GuestSchedBind summary (mode {}{}) | point {} | routed 2335 at the point {} (frame mismatch {}, no "
                  "stage in X {}, realm not client {}) | table walks {} (no table {}, truncated {}), actors seen {} | "
                  "NPCs of those scenes:{} (already bound to the stage in X: {}) | over the cap {} | the character's X in the "
                  "table not the routed X {} | calls {}:{} | a node carries the NPC after {} | AI scene key not the "
                  "scene's {} | AI mode X1+0x66 of the bound: 0 {}, 1 {}, 2 {}, other {} | faults {} | limited {} lost {}",
                  mode == gsr::Mode::On ? "1" : "probe", g_latched.load() ? ", LATCHED OFF after a fault" : "",
                  g_on.load() ? std::string("in") : "NOT installed: " + refused_text(), g_reached.load(), g_mismatch.load(),
                  g_no_stage.load(), g_realm_bad.load(), g_walks.load(), g_no_table.load(), g_truncated.load(),
                  g_actors.load(), whys.empty() ? " none" : whys, g_bound_in_x.load(), g_capped.load(), g_owner_x.load(),
                  g_calls.load(),
                  results.empty() ? " none" : results, g_node_taken.load(), g_ai_key_other.load(), g_mode66[0].load(),
                  g_mode66[1].load(), g_mode66[2].load(), g_mode66[3].load(), g_faults.load(),
                  g_limited.load(), g_lost.load());
        if (mode != gsr::Mode::On) return;
        std::string steps, rres;
        for (size_t k = 1; k < static_cast<size_t>(gsr::RetryStep::Count); ++k) {  // not Wait (counted as tries)
            if (const uint64_t c = g_step[k].load()) {
                steps += std::format(" {} {};", gsr::retry_step_name(static_cast<gsr::RetryStep>(k)), c);
            }
        }
        for (size_t k = 0; k < static_cast<size_t>(BindResult::Count); ++k) {
            if (const uint64_t c = g_retry_result[k].load()) {
                rres += std::format(" {} {};", gsr::bind_result_name(static_cast<BindResult>(k)), c);
            }
        }
        const int m_state = g_m_state.load();
        log::info("GuestSchedBind retry summary | queue now {} (queued {}, full {}) | passes: frame 0x8B6144 {} (frames "
                  "seen {}{}), routed 2335 {}, realm not client {}, busy {} | this client's M+0x231 {} | outcomes:{} | "
                  "retried calls {}:{} | refused by 0x8AE1A0 although logged in {} | stage active +0x178 at the retried "
                  "calls: 0 {}, 1 {} | longest wait {} ms",
                  g_queue_n.load(), g_queued.load(), g_queue_full.load(), g_passes_frame.load(), g_frames_seen.load(),
                  g_frames_seen.load() ? "" : ": guest_seq_play's hook not in, only routed 2335s retry",
                  g_passes_2335.load(), g_pass_realm_bad.load(), g_pass_busy.load(),
                  m_state < 0 ? std::string("not seen") : std::to_string(m_state), steps.empty() ? " none" : steps,
                  g_retry_calls.load(), rres.empty() ? " none" : rres, g_retry_refused.load(), g_retry_active[0].load(),
                  g_retry_active[1].load(), g_retry_max_wait_ms.load());
    } catch (...) {
    }
}

std::string guest_sched_bind_describe() {
    if (g_mode.load() == 0) return {};
    const uint64_t bound = g_result[static_cast<size_t>(BindResult::Bound)].load() +
                           g_retry_result[static_cast<size_t>(BindResult::Bound)].load();
    return std::format(" | sched bind {} (calls {}, bound {}, candidates {}, queued {}{})",
                       g_mode.load() == 2 ? "on" : "probe", g_calls.load(), bound,
                       g_why[static_cast<size_t>(BindWhy::Candidate)].load(), g_queue_n.load(),
                       g_latched.load() ? ", LATCHED OFF" : g_on.load() ? "" : ", not installed");
}

}  // namespace cdmp::session2
