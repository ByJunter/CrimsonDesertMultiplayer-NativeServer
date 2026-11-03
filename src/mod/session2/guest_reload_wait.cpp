#include "mod/session2/guest_reload_wait.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/guest_actor_diag.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/guest_reload_wait_logic.h"
#include "mod/session2/offsets_s2.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"
#include "mod/session2/user2_funnels_logic.h"

namespace cdmp::session2 {
namespace {

namespace g2976 = game::build_2976;
using u2f::Mode;

// ---------------------------------------------------------------- the code (build 1.0.0.2976, .rsrc)

// State 0x12 of 0x7797F0 (p8-s15-viaje-tunel.md §3, §8): its lock and the list's count,
//   0x779E8B mov r14,[rdi+0xB8] ; lea rbx,[r14+0x10] ; mov [rbp+0x40],rbx ; mov rax,[rbx] ; mov rcx,rbx ;
//            call [rax+8] (lock) ; nop ; mov rsi,[r14+8] ; mov eax,[rsi+0x80] ; test eax,eax ; jne 0x779ED0
constexpr uintptr_t kWaitHead = 0x779E8B;
constexpr uint8_t kWaitHeadBytes[] = {0x4C, 0x8B, 0xB7, 0xB8, 0x00, 0x00, 0x00, 0x49, 0x8D, 0x5E, 0x10, 0x48, 0x89,
                                      0x5D, 0x40, 0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xFF, 0x50, 0x08, 0x90, 0x49,
                                      0x8B, 0x76, 0x08, 0x8B, 0x86, 0x80, 0x00, 0x00, 0x00, 0x85, 0xC0, 0x75, 0x1E};
//   0x779ED0 the front key [[rsi+0x78]] -> Find 0x464290(C = [r14+8]) -> vf1D8 -> je 0x779F14 (wait)
constexpr uintptr_t kWaitLoop = 0x779ED0;
constexpr uint8_t kWaitLoopBytes[] = {0x48, 0x8B, 0x56, 0x78, 0x8B, 0x12, 0x49, 0x8B, 0x4E, 0x08, 0xE8, 0xB1, 0xA3,
                                      0xCE, 0xFF, 0x48, 0x85, 0xC0, 0x74, 0x13, 0x48, 0x8B, 0x08, 0x48, 0x8B, 0x91,
                                      0xD8, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xC8, 0xFF, 0xD2, 0x84, 0xC0, 0x74, 0x1D};
//   0x779EF7 the removal (grw::swap_remove_front), then 0x779F14 the unlock [rax+0x10]
constexpr uintptr_t kWaitRemove = 0x779EF7;
constexpr uint8_t kWaitRemoveBytes[] = {0x8B, 0x86, 0x80, 0x00, 0x00, 0x00, 0x83, 0xE8, 0x01, 0x74, 0x09, 0x48, 0x8B,
                                        0x56, 0x78, 0x8B, 0x0C, 0x82, 0x89, 0x0A, 0x83, 0x86, 0x80, 0x00, 0x00, 0x00,
                                        0xFF, 0x75, 0xBC, 0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xFF, 0x50, 0x10};
// vf1D8 = 0x4FE240: cmp byte [rcx+0xC2],0 ; jne ; xor al,al ; ret ; movzx eax,byte [rcx+0xCF] ; ret
constexpr uintptr_t kReady = 0x4FE240;
constexpr uint8_t kReadyBytes[] = {0x80, 0xB9, 0xC2, 0x00, 0x00, 0x00, 0x00, 0x75, 0x03, 0x32,
                                   0xC0, 0xC3, 0x0F, 0xB6, 0x81, 0xCF, 0x00, 0x00, 0x00, 0xC3};
// ClientActorContainer::Find 0x464290 (replayed by gad::container_find, never called).
constexpr uintptr_t kFind = 0x464290;
constexpr uint8_t kFindBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x83, 0xB9, 0x8C, 0x00, 0x00, 0x00, 0x00, 0x44,
                                  0x8B, 0xC2, 0x4C, 0x8B, 0xD1, 0x74, 0x4F, 0x8B, 0x89, 0x88, 0x00, 0x00, 0x00};

constexpr uint32_t kP_State = 0x15C, kP_Flags = 0x160, kP_290 = 0x290, kP_Manager = 0xB8;
constexpr uint32_t kM_Lock = 0x10, kM_Container = 0x08;
constexpr uint32_t kLock_Acquire = 0x08, kLock_Release = 0x10;  // the lock's vf1 / vf2 (0x779EA0 / 0x779F1A)
constexpr uint32_t kActor_State = 0x5E, kActor_C2 = 0xC2, kActor_C3 = 0xC3, kActor_CF = 0xCF;
constexpr uint32_t kActor_Parent = 0xAC, kActor_Children = 0xB0, kActor_ChildCount = 0xB8;
constexpr uint32_t kVf_Ready = 0x1D8;
// The guest character's own ClientSequencerStageManager (guest_seq_play.cpp): [[[A+0x68]+0xA8]+0x18], +0x231.
constexpr uint32_t kActor_Comps = 0x68, kComps_Seq = 0xA8, kSeqComp_Mgr = 0x18, kMgr_Ready = 0x231;
constexpr uintptr_t kVtStageManager = 0x55C9720;

constexpr uint32_t kSnapMax = 1024;
// S15 had 75 orphan children in one session (S14: 106): the cap only bounds a runaway; more than kWarnDrops in one
// trip is a warning, not a stop.
constexpr uint32_t kDropCap = 128, kWarnDrops = 16;
constexpr uint64_t kGraceUs = 2'000'000;
constexpr uint64_t kStillUs = 5'000'000;
constexpr uint64_t kTraceLines = 800, kBlockLines = 240, kDropLines = 400, kSnapLines = 48;
constexpr uint32_t kShownChildren = 4;

// ---------------------------------------------------------------- state

std::atomic<uint8_t> g_mode{0};
std::atomic<bool> g_code_ok{false}, g_disabled{false}, g_code_failed_logged{false};
uintptr_t g_base = 0;
std::vector<gad::Section> g_secs;  // written once by the diagnostics thread before g_code_ok (release)
uint64_t g_code_tries = 0;         // diagnostics thread

// The network thread: the host's 2174 / 2643 / 3066, QPC us.
std::atomic<uint64_t> g_last_2174{0}, g_last_2643{0}, g_last_3066{0};
// The client NetworkThread (guest_send): the client's own 2283, QPC us.
std::atomic<uint64_t> g_last_2283{0};

// The game's thread (one at a time: g_busy).
std::atomic_flag g_busy = ATOMIC_FLAG_INIT;
grw::TripClock g_clock;
grw::Snapshot<kSnapMax> g_snap;
grw::ReportClock g_report;
uint32_t g_dropped_trip = 0, g_last_front = 0;
uint64_t g_seen_2283 = 0;  // the last 2283 time this thread acted on
uint64_t g_last_sample_us = 0;
uintptr_t g_lock_vt_seen = 0;

grw::AgeTable<4096> g_ages;  // guest_actor_diag's 5 s walk -> the line formatter

enum Kind : uint16_t { kTrace = 1, kSnap, kBlock, kDrop };
p8p::EventRing<1024> g_ring;
gad::LineBudget g_trace_lines{kTraceLines}, g_block_lines{kBlockLines}, g_drop_lines{kDropLines},
    g_snap_lines{kSnapLines};

std::atomic<uint64_t> c_passes{0}, c_trips{0}, c_snaps{0}, c_blocks{0}, c_would{0}, c_dropped{0}, c_drop_failed{0},
    c_exceptions{0}, c_other_p{0}, c_many_trips{0}, c_trips_by_2283{0};
uint32_t g_warned_trip = 0;  // diagnostics thread: the last trip warned about > kWarnDrops
std::atomic<uint64_t> c_verdict[static_cast<size_t>(grw::Verdict::kCount)] = {};

// ---------------------------------------------------------------- reads and writes (SEH, no VirtualQuery)

template <class T>
bool rd(uintptr_t a, T& v) {
    if (!a) return false;
    __try {
        v = *reinterpret_cast<const volatile T*>(a);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <class T>
T rdv(uintptr_t a) {
    T v{};
    return rd(a, v) ? v : T{};
}

bool wr32(uintptr_t a, uint32_t v) {
    if (!a) return false;
    __try {
        *reinterpret_cast<volatile uint32_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct Mem {
    bool u32(uintptr_t a, uint32_t& v) const { return rd(a, v); }
    bool ptr(uintptr_t a, uintptr_t& v) const { return rd(a, v); }
    bool put32(uintptr_t a, uint32_t v) const { return wr32(a, v); }
};

bool in_image(uintptr_t a) {
    const auto rva = static_cast<uint32_t>(a - g_base);
    return a >= g_base && gad::section_of(g_secs, rva) != nullptr;
}

// A function the game's code may call: .rsrc (the real code) or .xtls.
bool in_code(uintptr_t a) {
    if (a < g_base) return false;
    const gad::Section* s = gad::section_of(g_secs, static_cast<uint32_t>(a - g_base));
    if (!s) return false;
    const std::string_view n(s->name);
    return n == ".rsrc" || n == ".xtls";
}

// ---------------------------------------------------------------- the actors

struct ActorRead {
    uintptr_t actor = 0;
    uint16_t state = 0;
    uint8_t c2 = 0, cf = 0, c3 = 0;
    uint32_t control = 0;
    bool vf_native = false;
};

ActorRead read_actor(uintptr_t a) {
    ActorRead r;
    r.actor = a;
    if (!a) return r;
    rd(a + kActor_State, r.state);
    rd(a + kActor_C2, r.c2);
    rd(a + kActor_CF, r.cf);
    rd(a + kActor_C3, r.c3);
    rd(a + g2976::kActor_OwnerUser, r.control);
    uintptr_t vt = 0, fn = 0;
    r.vf_native = rd(a, vt) && vt && rd(vt + kVf_Ready, fn) && fn == g_base + kReady;
    return r;
}

// The guest's controlled actor ([[[WorldSystem]+0x30]+0x50]), 0 when unknown.
uintptr_t own_actor() {
    const uintptr_t ws = rdv<uintptr_t>(g_base + g2976::kWorldSystemSlot);
    const uintptr_t mgr = ws ? rdv<uintptr_t>(ws + g2976::kWorldSystem_ActorManager) : 0;
    if (!mgr || rdv<uintptr_t>(mgr) != g_base + g2976::kVtClientActorManager) return 0;
    return rdv<uintptr_t>(mgr + g2976::kActorManager_ControlledActor);
}

// Its own stage manager's +0x231 (0xFF = unreadable / not a stage manager).
uint8_t own_m231(uintptr_t a) {
    const uintptr_t comps = a ? rdv<uintptr_t>(a + kActor_Comps) : 0;
    const uintptr_t seq = comps ? rdv<uintptr_t>(comps + kComps_Seq) : 0;
    const uintptr_t m = seq ? rdv<uintptr_t>(seq + kSeqComp_Mgr) : 0;
    uint8_t v = 0xFF;
    if (!m || rdv<uintptr_t>(m) != g_base + kVtStageManager || !rd(m + kMgr_Ready, v)) return 0xFF;
    return v;
}

struct ListView {
    uintptr_t m = 0, c = 0, keys = 0;
    uint32_t n = 0;
    bool ok = false;
};

ListView list_of(uintptr_t p) {
    ListView v;
    v.m = rdv<uintptr_t>(p + kP_Manager);
    v.c = v.m ? rdv<uintptr_t>(v.m + kM_Container) : 0;
    v.ok = v.c && rd(v.c + grw::WaitListLayout::kCount, v.n) && rd(v.c + grw::WaitListLayout::kKeys, v.keys);
    return v;
}

// ---------------------------------------------------------------- events (game thread -> diagnostics thread)

void push(p8p::Event& e, Kind k) {
    e.kind = k;
    e.qpc_us = e.qpc_us ? e.qpc_us : p8_now_us();
    e.tid = GetCurrentThreadId();
    g_ring.push(e);  // full: counted by the ring
}

// One trace record: the change (or a sample) with the fields task 3 asked for.
void push_trace(uintptr_t p, int from, int to, bool sample, uint64_t now) {
    p8p::Event e;
    e.qpc_us = now;
    uint32_t flags = 0;
    uint16_t f290 = 0;
    rd(p + kP_Flags, flags);
    rd(p + kP_290, f290);
    const ListView lv = list_of(p);
    const uintptr_t a = own_actor();
    const ActorRead ar = read_actor(a);
    const uint32_t own_key = a ? rdv<uint32_t>(a + g2976::kActor_Key) : 0;
    const uint64_t start = g_clock.start_us();
    e.v[0] = static_cast<uint64_t>(static_cast<uint8_t>(from)) | static_cast<uint64_t>(to & 0xFF) << 8 |
             static_cast<uint64_t>(sample) << 16 | static_cast<uint64_t>(from < 0) << 17 |
             static_cast<uint64_t>(g_clock.trip()) << 32;
    e.v[1] = flags | static_cast<uint64_t>(f290) << 32;
    e.v[2] = lv.ok ? lv.n : 0xFFFFFFFFu;
    e.v[3] = own_key | static_cast<uint64_t>(ar.c2) << 32 | static_cast<uint64_t>(ar.cf) << 40 |
             static_cast<uint64_t>(ar.c3) << 48 | static_cast<uint64_t>(own_m231(a)) << 56;
    e.v[4] = start;
    e.v[5] = g_last_2174.load(std::memory_order_relaxed);
    e.v[6] = g_last_2643.load(std::memory_order_relaxed);
    e.v[7] = g_last_3066.load(std::memory_order_relaxed);
    e.v[8] = g_clock.state_since_us();
    e.v[9] = p;
    push(e, kTrace);
}

// When a trip begins: the wait list's keys, each with its actor and readiness (Find replayed).
void take_snapshot(uintptr_t p, uint64_t now) {
    g_snap.begin(now);
    const ListView lv = list_of(p);
    if (lv.ok && lv.keys && lv.n <= (1u << 16)) {
        const gad::ContainerView view = gad::container_view(Mem{}, lv.c);
        const uint32_t n = lv.n < kSnapMax ? lv.n : kSnapMax;
        for (uint32_t i = 0; i < n; ++i) {
            grw::SnapEntry s;
            if (!rd(lv.keys + 4 * static_cast<uintptr_t>(i), s.key) || !s.key) continue;
            s.actor = gad::container_find_in(Mem{}, view, s.key);
            if (!s.actor) continue;  // 0x12 drops a missing key natively
            uint8_t c2 = 0, cf = 0;
            rd(s.actor + kActor_State, s.state);
            rd(s.actor + kActor_C2, c2);
            rd(s.actor + kActor_CF, cf);
            s.ready = c2 && cf;
            g_snap.add(s);
        }
    }
    c_snaps.fetch_add(1, std::memory_order_relaxed);
    p8p::Event e;
    e.qpc_us = now;
    e.v[0] = g_clock.trip();
    e.v[1] = lv.ok ? lv.n : 0xFFFFFFFFu;
    e.v[2] = g_snap.size();
    e.v[3] = g_snap.not_ready();
    e.v[4] = g_snap.overflow();
    // The first not-ready keys (up to 8, two per slot), in list order.
    uint32_t shown = 0;
    for (size_t i = 0; i < g_snap.size() && shown < 8; ++i) {
        const grw::SnapEntry& s = g_snap.at(i);
        if (s.ready) continue;
        e.v[5 + shown / 2] |= static_cast<uint64_t>(s.key) << (32 * (shown % 2));
        ++shown;
    }
    e.v[9] = p;
    e.v[10] = static_cast<uint64_t>(g_clock.origin());
    push(e, kSnap);
}

// The front key and everything a line about it needs.
struct FrontRead {
    grw::Front f;
    uint32_t parent = 0, nchildren = 0;
    bool parent_found = false;
    uint32_t child[kShownChildren] = {};
    uint8_t child_found = 0;  // bit i: child[i] exists here
    uint32_t list_n = 0;
};

FrontRead read_front(const ListView& lv) {
    FrontRead r;
    r.list_n = lv.n;
    if (!lv.ok || !lv.n || !lv.keys || !rd(lv.keys, r.f.key) || !r.f.key) return r;
    const gad::ContainerView view = gad::container_view(Mem{}, lv.c);
    const uintptr_t a = gad::container_find_in(Mem{}, view, r.f.key);
    if (!a) return r;
    const ActorRead ar = read_actor(a);
    r.f.actor = a;
    r.f.state = ar.state;
    r.f.c2 = ar.c2;
    r.f.cf = ar.cf;
    r.f.control = ar.control;
    r.f.vf_native = ar.vf_native;
    rd(a + kActor_Parent, r.parent);
    r.parent_found = r.parent && gad::container_find_in(Mem{}, view, r.parent) != 0;
    uintptr_t kids = 0;
    if (rd(a + kActor_ChildCount, r.nchildren) && r.nchildren && r.nchildren < 4096 && rd(a + kActor_Children, kids) &&
        kids) {
        for (uint32_t i = 0; i < r.nchildren && i < kShownChildren; ++i) {
            if (!rd(kids + 4 * static_cast<uintptr_t>(i), r.child[i])) break;
            if (r.child[i] && gad::container_find_in(Mem{}, view, r.child[i])) r.child_found |= 1u << i;
        }
    }
    return r;
}

void push_block(const FrontRead& r, grw::Verdict v, const grw::SnapEntry* snap, const grw::Context& c, uint64_t now,
                bool dropped, int drop_code) {
    p8p::Event e;
    e.qpc_us = now;
    e.flags = static_cast<uint16_t>(v);
    e.v[0] = g_clock.trip() | static_cast<uint64_t>(c.reload) << 32 | static_cast<uint64_t>(dropped) << 33 |
             static_cast<uint64_t>(snap != nullptr) << 34 | static_cast<uint64_t>(snap && snap->ready) << 35 |
             static_cast<uint64_t>(snap && snap->actor == r.f.actor) << 36 |
             static_cast<uint64_t>(r.f.vf_native) << 37 | static_cast<uint64_t>(r.parent_found) << 38 |
             static_cast<uint64_t>(g_mode.load(std::memory_order_relaxed)) << 40 |
             static_cast<uint64_t>(static_cast<uint8_t>(drop_code)) << 48;
    e.v[1] = r.f.key | static_cast<uint64_t>(r.f.control) << 32;
    e.v[2] = r.f.actor;
    e.v[3] = r.f.state | static_cast<uint64_t>(r.f.c2) << 16 | static_cast<uint64_t>(r.f.cf) << 24 |
             static_cast<uint64_t>(snap ? snap->state : 0xFFFF) << 32;
    e.v[4] = c.in_wait_us;
    e.v[5] = r.list_n | static_cast<uint64_t>(c.dropped) << 32;
    e.v[6] = r.parent | static_cast<uint64_t>(r.nchildren) << 32;
    e.v[7] = r.child[0] | static_cast<uint64_t>(r.child[1]) << 32;
    e.v[8] = r.child[2] | static_cast<uint64_t>(r.child[3]) << 32;
    e.v[9] = r.child_found;
    e.v[10] = g_clock.start_us();
    e.v[11] = g_snap.taken_us();
    push(e, dropped || drop_code ? kDrop : kBlock);
}

// ---------------------------------------------------------------- the removal, under 0x12's own lock

using LockFn = void(__fastcall*)(uintptr_t self);

// Still the same blocker under the lock: the front key, the same actor, not ready.
bool still_blocking(uintptr_t c, uint32_t key, uintptr_t actor) {
    uintptr_t keys = 0;
    uint32_t n = 0, front = 0;
    if (!rd(c + grw::WaitListLayout::kCount, n) || !n || !rd(c + grw::WaitListLayout::kKeys, keys) || !keys ||
        !rd(keys, front) || front != key) {
        return false;
    }
    if (gad::container_find(Mem{}, c, key) != actor) return false;
    uint8_t c2 = 0, cf = 0;
    return rd(actor + kActor_C2, c2) && rd(actor + kActor_CF, cf) && !(c2 && cf);
}

bool remove_front(uintptr_t c, uint32_t key) {
    Mem m;
    return grw::swap_remove_front(m, c, key);
}

// 0 removed | 2 the front changed under the lock | 3 the write failed | 4 an exception (lock released if taken).
int locked_remove(uintptr_t lock, LockFn acquire, LockFn release, uintptr_t c, uint32_t key, uintptr_t actor) {
    volatile bool locked = false;
    int code = 0;
    __try {
        acquire(lock);
        locked = true;
        if (!still_blocking(c, key, actor)) {
            code = 2;
        } else if (!remove_front(c, key)) {
            code = 3;
        }
        locked = false;
        release(lock);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        code = 4;
        if (locked) {
            __try {
                release(lock);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }
    return code;
}

// 0 removed | 1 the lock's functions are not the game's code | 2/3/4 locked_remove.
int drop_front(const ListView& lv, uint32_t key, uintptr_t actor) {
    const uintptr_t lock = lv.m + kM_Lock;
    const uintptr_t vt = rdv<uintptr_t>(lock);
    const uintptr_t acq = vt ? rdv<uintptr_t>(vt + kLock_Acquire) : 0;
    const uintptr_t rel = vt ? rdv<uintptr_t>(vt + kLock_Release) : 0;
    if (!vt || !in_image(vt) || !in_code(acq) || !in_code(rel)) return 1;
    // The same lock object type all session long (a change means another object: refused).
    if (g_lock_vt_seen && g_lock_vt_seen != vt) return 1;
    g_lock_vt_seen = vt;
    return locked_remove(lock, reinterpret_cast<LockFn>(acq), reinterpret_cast<LockFn>(rel), lv.c, key, actor);
}

// ---------------------------------------------------------------- state 0x12

void on_wait(uintptr_t p, uint64_t now) {
    const ListView lv = list_of(p);
    const FrontRead r = read_front(lv);
    const grw::SnapEntry* snap = r.f.key ? g_snap.find(r.f.key) : nullptr;
    grw::Context c;
    c.reload = g_clock.in_trip() &&
               grw::is_reload_trip(g_clock.start_us(), g_last_2643.load(std::memory_order_relaxed),
                                   g_last_2174.load(std::memory_order_relaxed));
    c.in_wait_us = g_clock.wait_since_us() && now >= g_clock.wait_since_us() ? now - g_clock.wait_since_us() : 0;
    c.grace_us = kGraceUs;
    c.local_user = guest_actor_diag_local_user();
    const uintptr_t own = own_actor();
    c.own_key = own ? rdv<uint32_t>(own + g2976::kActor_Key) : 0;
    c.dropped = g_dropped_trip;
    c.cap = kDropCap;
    const grw::Verdict v = grw::judge(r.f, snap, c);
    // Only a real blocker is worth a line (an existing, not ready front key).
    const bool blocker = r.f.actor && !grw::front_ready(r.f);
    if (!blocker) return;
    const auto mode = static_cast<Mode>(g_mode.load(std::memory_order_relaxed));
    const bool changed = r.f.key != g_last_front;
    if (changed) g_last_front = r.f.key;
    if (v == grw::Verdict::Drop && mode == Mode::On && g_code_ok.load(std::memory_order_acquire) &&
        !g_disabled.load(std::memory_order_relaxed)) {
        const int code = drop_front(lv, r.f.key, r.f.actor);
        if (code == 2) {
            // The front changed under the lock (another thread touched the list): looked at again next pass.
            c_drop_failed.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (code == 0) {
            ++g_dropped_trip;
            c_dropped.fetch_add(1, std::memory_order_relaxed);
        } else {
            // 1 (the lock is not the game's code), 3 (a write failed), 4 (an exception): never tried again.
            c_drop_failed.fetch_add(1, std::memory_order_relaxed);
            if (code == 4) c_exceptions.fetch_add(1, std::memory_order_relaxed);
            g_disabled.store(true);
        }
        c.dropped = g_dropped_trip;
        push_block(r, v, snap, c, now, code == 0, code);
        g_last_front = 0;  // the next front gets its own line at once
        return;
    }
    // A new blocker gets its line at once; the same one at +2/+10/+30/+60 s in 0x12 and every 60 s after.
    const bool due = g_report.due(c.in_wait_us);
    if (!changed && !due) return;
    while (g_report.due(c.in_wait_us)) {
    }
    c_blocks.fetch_add(1, std::memory_order_relaxed);
    c_verdict[static_cast<size_t>(v)].fetch_add(1, std::memory_order_relaxed);
    if (v == grw::Verdict::Drop) c_would.fetch_add(1, std::memory_order_relaxed);
    push_block(r, v, snap, c, now, false, 0);
}

// ---------------------------------------------------------------- the diagnostics thread

std::string state_text(int s) { return s < 0 ? std::string("?") : std::format("0x{:02X}", static_cast<unsigned>(s)); }

std::string since_text(uint64_t then_us, uint64_t now_us) {
    if (!then_us || now_us < then_us) return "-";
    return std::format("{:.3f} s", static_cast<double>(now_us - then_us) / 1e6);
}

std::string age_text(uint32_t key) {
    const uint64_t since = g_ages.since(key);
    if (!since) return "age unknown (not yet seen by the 5 s walk)";
    const uint64_t now = GetTickCount64();
    return std::format("not ready for >= {} s (5 s walk)", now >= since ? (now - since) / 1000 : 0);
}

// P8 S27: the last trip warned about a missing 2322 of its own character (diagnostics thread only).
uint32_t g_warned_own_2322_trip = 0;
std::atomic<uint64_t> c_own_2322_warned{0};

void line_trace(const p8p::Event& e) {
    const uint64_t* v = e.v;
    const int from = (v[0] >> 17) & 1 ? -1 : static_cast<int>(v[0] & 0xFF);
    const int to = static_cast<int>((v[0] >> 8) & 0xFF);
    const bool sample = (v[0] >> 16) & 1;
    const auto trip = static_cast<uint32_t>(v[0] >> 32);
    const auto flags = static_cast<uint32_t>(v[1]);
    const auto f290 = static_cast<uint16_t>(v[1] >> 32);
    const auto n = static_cast<uint32_t>(v[2]);
    const auto m231 = static_cast<unsigned>((v[3] >> 56) & 0xFF);
    const uint64_t start = v[4];
    // P8 S27 (log only, outside the line budget): held in 0x11 in a reload, its own M+0x231 below 2 for >= 10 s.
    if (sample && trip != g_warned_own_2322_trip) {
        const bool reload = grw::is_reload_trip(start, v[6], v[5]);
        const uint64_t in_state = v[8] && e.qpc_us >= v[8] ? e.qpc_us - v[8] : 0;
        if (grw::waiting_own_2322(to, reload, m231, in_state)) {
            g_warned_own_2322_trip = trip;
            c_own_2322_warned.fetch_add(1, std::memory_order_relaxed);
            log::warn("GuestReloadWait [0x11 own 2322] {} T{} | trip {} (RELOAD (2643, no 2174)) | [P+0x15C] 0x11 for {} "
                      "with own M+0x231 {} (0x779E48 waits for 2): NO PlayAllPreLoadSequencerAck 2322 for this client's "
                      "own character 0x{:08X} came after its PreLoadSequencerReq 2873 - the host's server sent it to the "
                      "field owner's character; the load stays here (no 2445; at the exit the final save is refused "
                      "while loading). Host: observer_milestone_reload(_any), docs/research/s25/reaparicion.md",
                      p8_when(e.qpc_us), e.tid, trip, since_text(v[8], e.qpc_us), m231, v[3] & 0xFFFFFFFF);
        }
    }
    if (!g_trace_lines.take()) return;
    const char* kind = !start ? "?"
                       : v[5] >= start ? "entry (2174)"
                       : v[6] >= start ? "RELOAD (2643, no 2174)"
                                       : "no 2174/2643 yet";
    log::info("GuestReloadWait [{}] {} T{} | trip {} ({}) | [P+0x15C] {}{} | +{} since the trip began | P+0x160..163 "
              "{:02X} {:02X} {:02X} {:02X} | P+0x290 {:02X} P+0x291 {:02X} | wait list {} | own actor 0x{:08X} +C2 {} "
              "+CF {} +C3 {} | own M+0x231 {} | 3066 {}",
              sample ? "still" : "trace", p8_when(e.qpc_us), e.tid, trip, kind,
              sample ? state_text(to) : std::format("{} -> {}", state_text(from), state_text(to)),
              sample ? std::format(" for {}", since_text(v[8], e.qpc_us)) : std::string(), since_text(start, e.qpc_us),
              flags & 0xFF, (flags >> 8) & 0xFF, (flags >> 16) & 0xFF, flags >> 24, f290 & 0xFF, f290 >> 8,
              n == 0xFFFFFFFFu ? std::string("unreadable") : std::to_string(n), v[3] & 0xFFFFFFFF, (v[3] >> 32) & 0xFF,
              (v[3] >> 40) & 0xFF, (v[3] >> 48) & 0xFF, m231 == 0xFF ? std::string("?") : std::to_string(m231),
              v[7] >= start && start ? "seen this trip" : "not this trip");
}

void line_snap(const p8p::Event& e) {
    if (!g_snap_lines.take()) return;
    const uint64_t* v = e.v;
    std::string keys;
    for (uint32_t i = 0; i < 8; ++i) {
        const auto k = static_cast<uint32_t>(v[5 + i / 2] >> (32 * (i % 2)));
        if (!k) break;
        keys += std::format(" 0x{:08X} ({})", k, age_text(k));
    }
    log::info("GuestReloadWait [trip {} begins{}] {} T{} | the wait list now: {} keys, {} existing, {} NOT "
              "READY{}{} | only these (same actor) may be dropped at 0x12 of this trip",
              v[0],
              v[10] == static_cast<uint64_t>(grw::TripClock::Origin::Req2283) ? " at the client's 2283"
                                                                               : " (state left 0x18)",
              p8_when(e.qpc_us), e.tid, v[1] == 0xFFFFFFFFu ? std::string("unreadable") : std::to_string(v[1]),
              v[2], v[3], keys.empty() ? std::string() : std::string(":") + keys,
              v[4] ? std::format(" | {} keys beyond the snapshot's {}", v[4], kSnapMax) : std::string());
}

void line_block(const p8p::Event& e, bool drop) {
    if (!(drop ? g_drop_lines : g_block_lines).take()) return;
    const uint64_t* v = e.v;
    const auto verdict = static_cast<grw::Verdict>(e.flags);
    const bool reload = (v[0] >> 32) & 1, dropped = (v[0] >> 33) & 1, in_snap = (v[0] >> 34) & 1,
               snap_ready = (v[0] >> 35) & 1, same = (v[0] >> 36) & 1, vf_native = (v[0] >> 37) & 1,
               parent_found = (v[0] >> 38) & 1;
    const auto mode = static_cast<Mode>((v[0] >> 40) & 0xFF);
    const int code = static_cast<int>((v[0] >> 48) & 0xFF);
    const auto key = static_cast<uint32_t>(v[1]);
    const auto control = static_cast<uint32_t>(v[1] >> 32);
    const auto parent = static_cast<uint32_t>(v[6]);
    const auto nchildren = static_cast<uint32_t>(v[6] >> 32);
    std::string kids;
    for (uint32_t i = 0; i < nchildren && i < kShownChildren; ++i) {
        const auto k = static_cast<uint32_t>(v[7 + i / 2] >> (32 * (i % 2)));
        kids += std::format("{}0x{:08X}{}", i ? ", " : "", k, (v[9] >> i) & 1 ? "" : " MISSING HERE");
    }
    if (nchildren > kShownChildren) kids += std::format(", ... ({} in all)", nchildren);
    const bool missing_child = nchildren && [&] {
        for (uint32_t i = 0; i < nchildren && i < kShownChildren; ++i) {
            if (!((v[9] >> i) & 1)) return true;
        }
        return false;
    }();
    // In mode 1 a Drop verdict is removed at once; one that reaches a plain line was not allowed to be.
    const char* what = !drop ? (verdict != grw::Verdict::Drop ? "BLOCKER"
                                : mode == Mode::On            ? "WOULD DROP (not removed: code not verified or disabled)"
                                                              : "WOULD DROP (probe)")
                       : dropped            ? "DROPPED"
                       : code == 1          ? "DROP REFUSED (the lock's functions are not the game's code)"
                       : code == 2          ? "DROP SKIPPED (the front changed under the lock)"
                       : code == 3          ? "DROP FAILED (write)"
                                            : "DROP FAILED (exception: guest_reload_wait_fix disabled)";
    const auto snap_state = static_cast<uint32_t>(v[3] >> 32);
    log::info("GuestReloadWait [0x12 {}] {} T{} | trip {} ({}) | in 0x12 for {:.3f} s | wait list {} | front 0x{:08X} "
              "actor 0x{:X} ctrl 0x{:08X} +5E 0x{:04X} C2 {} CF {} vf1D8 {} | {} | parent +0xAC 0x{:08X}{} | children "
              "+0xB0 {}{}{} | when the trip began: {} | verdict: {} | dropped this trip {}",
              what, p8_when(e.qpc_us), e.tid, v[0] & 0xFFFFFFFF, reload ? "reload in the same field" : "not a reload",
              static_cast<double>(v[4]) / 1e6, v[5] & 0xFFFFFFFF, key, v[2], control, v[3] & 0xFFFF, (v[3] >> 16) & 0xFF,
              (v[3] >> 24) & 0xFF, vf_native ? "0x4FE240" : "OTHER", age_text(key), parent,
              parent ? (parent_found ? " (here)" : " (NOT here)") : "", nchildren, kids.empty() ? "" : ": ",
              kids + (missing_child ? " -> a child is missing: its login group can never form (the orphan-child "
                                      "fault, §8.1)"
                                    : ""),
              in_snap ? std::format("in the list, {} (+5E 0x{:04X}), {}", snap_ready ? "ready" : "NOT READY", snap_state,
                                    same ? "same actor" : "ANOTHER actor")
                      : std::string("not in the list"),
              grw::verdict_text(verdict), (v[5] >> 32));
}

// More than kWarnDrops removals in one trip: a warning once per trip (never subject to the line budgets).
void warn_many(const p8p::Event& e) {
    const auto trip = static_cast<uint32_t>(e.v[0]);
    const bool dropped = (e.v[0] >> 33) & 1;
    const auto count = static_cast<uint32_t>(e.v[5] >> 32);
    if (!dropped || count <= kWarnDrops || g_warned_trip == trip) return;
    g_warned_trip = trip;
    c_many_trips.fetch_add(1, std::memory_order_relaxed);
    log::warn("GuestReloadWait: trip {} has dropped {} not-ready actors from the wait list (more than {}; cap {}): the "
              "host is sending many children before their parents (orphans, p8-s15-viaje-tunel.md §8.1). Not an "
              "abort by itself; check the DROPPED lines are all keys of the trip's snapshot",
              trip, count, kWarnDrops, kDropCap);
}

bool check_code() {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (g_base != off::kImageBase || !game::safe_read(g_base, &dos, sizeof dos) ||
        !game::safe_read(g_base + dos.e_lfanew, &nt, sizeof nt) || nt.FileHeader.TimeDateStamp != off::kPeTimestamp) {
        return false;
    }
    std::vector<gad::Section> secs;
    if (!p8_exe_sections(g_base, secs)) return false;
    struct C {
        uintptr_t at;
        std::span<const uint8_t> b;
    };
    const C checks[] = {{kWaitHead, kWaitHeadBytes},
                        {kWaitLoop, kWaitLoopBytes},
                        {kWaitRemove, kWaitRemoveBytes},
                        {kReady, kReadyBytes},
                        {kFind, kFindBytes}};
    for (const C& c : checks) {
        std::vector<uint8_t> got(c.b.size());
        if (!game::safe_read(g_base + c.at, got.data(), got.size()) ||
            std::memcmp(got.data(), c.b.data(), c.b.size()) != 0 ||
            !gad::in_real_code(secs, static_cast<uint32_t>(c.at), static_cast<uint32_t>(c.b.size()))) {
            return false;
        }
    }
    g_secs = std::move(secs);
    g_code_ok.store(true, std::memory_order_release);
    return true;
}

}  // namespace

bool guest_reload_wait_configure(const std::string& raw, bool actor_diag) {
    const auto p = u2f::parse_mode(raw);
    if (!p.known) {
        log::warn("Session2 guest: guest_reload_wait_fix='{}' is not 0, probe or 1; it stays off", raw);
        return false;
    }
    if (p.mode == Mode::Off) return false;
    if (!actor_diag) {
        log::warn("Session2 guest: guest_reload_wait_fix='{}' needs guest_actor_diag=1 (it runs in that module's hook "
                  "at exe+0x7798D0); it stays off",
                  raw);
        return false;
    }
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_mode.store(static_cast<uint8_t>(p.mode));
    log::info("GuestReloadWait ON ({}): the load machine's trace in each trip; at state 0x12 the blocking actor (front "
              "of the wait list) with its state, age and children; {} (a world actor 0xB... controlled by another user, "
              "waiting for its login +0x5E 8, already NOT READY in the wait list when the trip began (its 2283 or the exit from 0x18), same actor; "
              "reload in the same field only (2643, no 2174); after {} ms in 0x12; at most {} per trip, a warning past 16; under 0x12's own "
              "lock [M+0x10], as 0x779EF7..0x779F0B)",
              u2f::mode_name(p.mode),
              p.mode == Mode::On ? "mode 1 REMOVES such a front key" : "probe: what mode 1 would remove is logged",
              kGraceUs / 1000, kDropCap);
    return true;
}

bool guest_reload_wait_active() { return g_mode.load(std::memory_order_relaxed) != 0; }

void guest_reload_wait_on_pass(uintptr_t p, uint8_t state, int previous) noexcept {
    if (!g_mode.load(std::memory_order_relaxed) || !g_base) return;
    if (g_busy.test_and_set(std::memory_order_acquire)) return;
    c_passes.fetch_add(1, std::memory_order_relaxed);
    const uint64_t now = p8_now_us();
    auto new_trip = [&] {
        c_trips.fetch_add(1, std::memory_order_relaxed);
        g_dropped_trip = 0;
        g_last_front = 0;
        g_report.reset();
        take_snapshot(p, now);
    };
    // The client's 2283 (its NetworkThread wrote the time): a trip begins here if none runs yet.
    if (const uint64_t req = g_last_2283.load(std::memory_order_relaxed); req && req != g_seen_2283) {
        g_seen_2283 = req;
        if (g_clock.on_2283(now)) {
            c_trips_by_2283.fetch_add(1, std::memory_order_relaxed);
            new_trip();
        }
    }
    if (previous != state) {
        const grw::TripClock::Change ch = g_clock.on_state(previous, state, now);
        if (ch.began) new_trip();
        if (ch.entered_wait) {
            g_report.reset();
            g_last_front = 0;
        }
        // Every change inside a trip, and the one that ends it (the first value seen is logged too).
        if (g_clock.in_trip() || ch.ended || previous < 0) push_trace(p, previous, state, false, now);
        g_last_sample_us = now;
    } else if (g_clock.in_trip() && state != grw::kStateIdle && now - g_last_sample_us >= kStillUs) {
        push_trace(p, previous, state, true, now);
        g_last_sample_us = now;
    }
    if (state == grw::kStateWait) on_wait(p, now);
    g_busy.clear(std::memory_order_release);
}

void guest_reload_wait_on_in(uint16_t opcode) noexcept {
    if (!g_mode.load(std::memory_order_relaxed)) return;
    switch (opcode) {
        case grw::kOpMoveFieldAck: g_last_2174.store(p8_now_us(), std::memory_order_relaxed); break;
        case grw::kOpReloadPos: g_last_2643.store(p8_now_us(), std::memory_order_relaxed); break;
        case grw::kOpPostAck: g_last_3066.store(p8_now_us(), std::memory_order_relaxed); break;
        default: break;
    }
}

void guest_reload_wait_on_out(uint16_t opcode) noexcept {
    if (!g_mode.load(std::memory_order_relaxed) || opcode != grw::kOpTravelReq) return;
    g_last_2283.store(p8_now_us(), std::memory_order_relaxed);
}

void guest_reload_wait_note_entry(uint32_t key, bool ready) noexcept {
    if (!g_mode.load(std::memory_order_relaxed)) return;
    g_ages.note(key, ready, GetTickCount64());
}

void guest_reload_wait_tick() noexcept {
    if (!g_mode.load(std::memory_order_relaxed)) return;
    try {
        if (!g_code_ok.load(std::memory_order_acquire) && !g_code_failed_logged.load()) {
            if (check_code()) {
                log::info("GuestReloadWait: state 0x12 (exe+0x{:X}, exe+0x{:X}, removal exe+0x{:X}), vf1D8 exe+0x{:X} "
                          "and Find exe+0x{:X} match build 2976 inside .rsrc{}",
                          kWaitHead, kWaitLoop, kWaitRemove, kReady, kFind,
                          g_mode.load() == static_cast<uint8_t>(Mode::On) ? ": removals allowed" : "");
            } else if (++g_code_tries >= 24) {  // 2 minutes of 5 s ticks
                g_code_failed_logged = true;
                log::error("GuestReloadWait: the code of state 0x12 / vf1D8 / Find is not build 2976's (or not in "
                           ".rsrc); nothing will be removed (the trace and the probe lines go on)");
            }
        }
        p8p::Event e;
        while (g_ring.pop(e)) {
            switch (e.kind) {
                case kTrace: line_trace(e); break;
                case kSnap: line_snap(e); break;
                case kBlock: line_block(e, false); break;
                case kDrop:
                    warn_many(e);
                    line_block(e, true);
                    break;
                default: break;
            }
        }
    } catch (...) {
    }
}

std::string guest_reload_wait_describe() {
    const auto mode = static_cast<Mode>(g_mode.load());
    if (mode == Mode::Off) return {};
    std::string v;
    for (size_t i = 0; i < static_cast<size_t>(grw::Verdict::kCount); ++i) {
        if (const uint64_t n = c_verdict[i].load()) {
            v += std::format("{}{} {}", v.empty() ? "" : ", ", grw::verdict_text(static_cast<grw::Verdict>(i)), n);
        }
    }
    return std::format(" | GuestReloadWait [{}] code {} | passes {} | trips {} (snapshots {}) | 0x12 blocker lines {} "
                       "[{}] | would drop {} | DROPPED {} (failed {}, exceptions {}{}; trips with > {} drops {}) | trips "
                       "begun at the client's 2283 {} | ring dropped {} | ages full {} | 0x11 without its own 2322 "
                       "(warned) {}",
                       u2f::mode_name(mode), g_code_ok.load() ? "ok" : (g_code_failed_logged.load() ? "MISMATCH" : "?"),
                       c_passes.load(), c_trips.load(), c_snaps.load(), c_blocks.load(), v, c_would.load(),
                       c_dropped.load(), c_drop_failed.load(), c_exceptions.load(), g_disabled.load() ? ", DISABLED" : "",
                       kWarnDrops, c_many_trips.load(), c_trips_by_2283.load(), g_ring.dropped(), g_ages.full(),
                       c_own_2322_warned.load());
}

}  // namespace cdmp::session2
