#include "mod/session2/guest_sched_load.h"

#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <iterator>
#include <string>
#include <vector>

#include "mod/game/game_fault.h"
#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/guest.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/guest_sched_load_logic.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"
#include "mod/session2/scene_diag_logic.h"

namespace cdmp::session2 {
namespace {

namespace p = p8p;
namespace off = game::build_2976;
using gsl::KeyState;
using gsl::Why;
using GL = gsl::Layout;

// ---------------------------------------------------------------- the code (build 2976, all in .rsrc)
//
// Verified with capstone on the read-only PE (p8-impl-horarios.md §1, §9):
//   0x1864140 (unwind 0x1864140-0x18643A8) the loader: 0x1422F90 + 0x1364780 (key), [rcx+8] 0x186A9A0 (find),
//             else "%#/%#" ("sequencer/binary__", name) 0x5C13C0 and 0x18643B0(mgr, out, path, 0); returns out.
//             Its 4th argument (r9b) is stored at entry and overwritten by the key (0x18641F0): unused.
//   0x18643B0 (0x18643B0-0x1864CD8) reads [mgr+0x90]->vf40, allocates the file (game heap), deserializes it,
//             0x1864CE0, inserts it (0x186AB90 -> .xtls, the container's lock).
//   0x186A810 (0x186A810-0x186A999) the container's lookup (container, File** out, const u32* key): the
//             container's RW lock (users +0x68, lock object vf40, vf18 / vf20), the holder's idle counter reset
//             (holder+0x10 = 0), out = the file with a reference, or 0 also when it is being unloaded (+0x15).
//   0x38AD90  (0x38AD90-0x38ADEC) the handle release (refcount at +0x10, atomic when +0x14; vf0(1) at zero).
//   Watched: 0x1869D70 (entry: mov [rsp+0x10],rdx), 0x1862A80 (entry: mov [rsp+0x18],r8d).
// The whole bodies of the four called functions are compared (FNV-1a 64 of the build's bytes) before any call.
struct Body {
    uintptr_t rva, end;
    uint64_t fnv;
};
constexpr Body kLoader{0x1864140, 0x18643A8, 0xE06FA55A0E6EA986ull};
constexpr Body kReader{0x18643B0, 0x1864CD8, 0x90E1F0C9AAC28DB5ull};
constexpr Body kLookup{0x186A810, 0x186A999, 0x8D729B4530462A50ull};
constexpr Body kRelease{0x38AD90, 0x38ADEC, 0x9F799744CF2F1875ull};
constexpr uintptr_t kNodeLookupRva = 0x1869D70;
constexpr uint8_t kNodeLookupBytes[] = {0x48, 0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4C,
                                        0x24, 0x08, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56};
constexpr uintptr_t kKeyLookupRva = 0x1862A80;
constexpr uint8_t kKeyLookupBytes[] = {0x44, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54,
                                       0x24, 0x10, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48};
// Session 12 (the hold): 0xA9FEE0 (only caller 0xC0B619 in the 2335 handler 0xC0AFC0) calls 0x1862A80 at 0xA9FFC3
// (E8 B8 2A DC 00): at the lookup's entry, [rsp] = 0xA9FFC8 means "this 2335 is running now" (checked at install).
constexpr uintptr_t kStageCallRva = 0xA9FFC3, kStageRetRva = 0xA9FFC8;
constexpr uint8_t kStageCallBytes[] = {0xE8, 0xB8, 0x2A, 0xDC, 0x00};
// pa::NPCScheduleData_File (ctor 0x1860C10 [V]): vtable, refcount u32 (+0x10), "atomic count" byte (+0x14 = 1).
constexpr uintptr_t kVtFile = 0x57DB080;
constexpr uint32_t kFile_Refs = 0x10, kFile_Atomic = 0x14;
constexpr uint32_t kTlsRealm = 0x1EC;      // the server-realm byte of the exe's TLS block (0x281B757, 0xA9FF2A)
constexpr uint32_t kLookupSample = 8;      // the lookup watch's full path: 1 call in 8 per thread (+ verifications)
constexpr uint32_t kMaxKeySetBuilds = 32;  // the key set is rebuilt when the context table changes, at most this

using LoadFn = uintptr_t*(__fastcall*)(uintptr_t mgr, uintptr_t* out, uintptr_t name_field, uint8_t unused);
using LookupFn = uintptr_t*(__fastcall*)(uintptr_t container, uintptr_t* out, const uint32_t* key);
using ReleaseFn = void(__fastcall*)(uintptr_t* handle);

// Set once, at the DLL's static initialization (before any thread of this module exists).
const uintptr_t g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
std::atomic<bool> g_started{false}, g_code_ok{false}, g_disabled{false}, g_watch_on{false};
std::atomic<uint8_t> g_mode{0};
LoadFn g_load = nullptr;
LookupFn g_lookup = nullptr;
ReleaseFn g_release = nullptr;
SafetyHookMid g_hook_node, g_hook_key;
std::atomic<uint32_t> g_hooked{0};

std::atomic<const gsl::KeySet*> g_keys{nullptr};  // published by the diagnostics thread, never freed
gsl::Engine<4096> g_eng;                           // the book, budgets and counters (decisions: NetworkThread)

// Counters (any thread; read by the diagnostics thread).
std::atomic<uint64_t> g_packets[3] = {};  // 2005, 2335, 2874 seen before delivery
std::atomic<uint64_t> g_npc_records{0}, g_scan_hits{0}, g_spilled{0}, g_untiled{0}, g_keys_named{0};
std::atomic<uint64_t> g_faults{0}, g_realm_bad{0}, g_present_unloading{0};
// The lookups: [0] 0x1869D70 (node), [1] 0x1862A80 (key). all = every call; the rest on the sampled path.
std::atomic<uint64_t> g_lk_all[2] = {}, g_lk_calls[2] = {}, g_lk_found[2] = {}, g_lk_missing[2] = {},
                      g_lk_server[2] = {}, g_lk_other_mgr[2] = {};
std::atomic<uint64_t> g_found_after_load{0}, g_missing_after_load{0}, g_reentry{0};
// Session 12 (the hold). The 2335's own lookup (0x1862A80 called from 0xA9FEE0, client thread): found / missing for
// a key a packet named; of the found, while we held it; of the missing, after our hold ended / never held by us.
std::atomic<bool> g_stage_ret_ok{false};
std::atomic<uint64_t> g_stage_found{0}, g_stage_found_held{0}, g_stage_missing{0}, g_stage_missing_after{0},
    g_stage_unnamed{0};
std::atomic<uint64_t> g_hold_odd{0}, g_hold_last{0}, g_hold_services{0};
thread_local bool t_busy = false;       // inside our own call into the game (review 6: RAII below)
thread_local uint32_t t_sample = 0;     // the lookup watch's sampling counter

struct BusyGuard {
    BusyGuard() noexcept { t_busy = true; }
    ~BusyGuard() { t_busy = false; }
    BusyGuard(const BusyGuard&) = delete;
    BusyGuard& operator=(const BusyGuard&) = delete;
};

enum Kind : uint16_t { kELoad, kEWould, kEDecided, kEMiss, kERealm, kELatch, kEHoldEnd, kEEarly, kEStage, kELate, kEBlock, kKindCount };
p::EventRing<512> g_ring;
p::WindowLimiter g_lim[kKindCount] = {{64, 5'000'000}, {64, 5'000'000}, {32, 5'000'000},
                                      {32, 5'000'000}, {4, 60'000'000},  {4, 60'000'000},
                                      {64, 5'000'000}, {32, 5'000'000},  {32, 5'000'000},
                                      {32, 5'000'000}, {4, 60'000'000}};
std::atomic<uint64_t> g_limited{0}, g_lost{0}, g_lines{0};
constexpr uint64_t kMaxLines = 3000;
constexpr uint64_t kMaxLateLines = 200;  // session 13: [LATE] lines, outside kMaxLines (should be none)
std::atomic<uint64_t> g_late_lines{0};

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
    bool u8(uintptr_t a, uint8_t& v) const noexcept { return rd(a, v); }
    bool u32(uintptr_t a, uint32_t& v) const noexcept { return rd(a, v); }
    bool ptr(uintptr_t a, uintptr_t& v) const noexcept { return rd(a, v); }
};

// The exe's TLS block (index 0: mov rax,gs:[0x58]; mov rdx,[rax]) +0x1EC: 0 client, 1 server, -1 unreadable.
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

struct ManagerView {
    uintptr_t mgr = 0, vt = 0, client_mgr = 0, server_mgr = 0, container = 0, fs = 0;
    uint32_t ctx_gen = 0, ctx_used = 0;
    bool vt_ok = false, ok = false;
};
ManagerView manager_view() noexcept {
    ManagerView v;
    v.mgr = rdp(g_base + GL::kManagerSlot);
    if (!v.mgr) return v;
    v.vt = rdp(v.mgr);
    v.vt_ok = v.vt == g_base + GL::kVtManager;
    const uintptr_t croot = rdp(g_base + GL::kClientRootSlot), sroot = rdp(g_base + GL::kServerRootSlot);
    v.client_mgr = croot ? rdp(croot + GL::kRoot_Manager) : 0;
    v.server_mgr = sroot ? rdp(sroot + GL::kRoot_Manager) : 0;
    v.container = rdp(v.mgr + GL::kManager_Container);
    v.fs = rdp(v.mgr + GL::kManager_FileSystem);
    v.ctx_gen = rdv<uint32_t>(v.mgr + GL::kCtx_Gen);
    v.ctx_used = rdv<uint32_t>(v.mgr + GL::kCtx_Used);
    // 0x1869D70 (the AI block, 2874) looks up [0x6C8CDA0]; 0xA9FEE0 (2335) [[0x6D69518]+0x40]: one object.
    v.ok = v.vt_ok && v.container && v.fs && v.client_mgr == v.mgr;
    return v;
}

// The lock-free replica (diagnostics thread, self-check, the lookup watch): a torn read only miscounts. A file
// being unloaded counts as present (never loaded twice).
scd::Lookup replica_lookup(uintptr_t container, uint32_t key) noexcept {
    return container ? scd::schedule_lookup(GameMem{}, container, key, 0, false).what : scd::Lookup::NoContainer;
}
bool replica_present(uintptr_t container, uint32_t key) noexcept {
    const scd::Lookup l = replica_lookup(container, key);
    return l == scd::Lookup::Found || l == scd::Lookup::FileUnloading;
}
uint32_t container_used(uintptr_t container) noexcept {
    return container ? rdv<uint32_t>(container + scd::ScheduleLayout::kUsed) : 0;
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

// ---------------------------------------------------------------- the calls (the NetworkThread)

// P8 review I1 (game_fault.h): an exception inside the game's code is the game's (the container's or the file
// system's lock may be held): the module switches off, one line goes straight to the log, it is passed on.
int call_fault(EXCEPTION_POINTERS* ep, const char* where) noexcept {
    g_disabled.store(true);
    g_faults.fetch_add(1);
    return game::game_fault_continue(ep, "GuestSchedLoad", where);
}
bool guarded_load(uintptr_t mgr, uintptr_t* out, uintptr_t name_field) noexcept {
    __try {
        g_load(mgr, out, name_field, 0);
        return true;
    } __except (call_fault(GetExceptionInformation(), "0x1864140(mgr, &handle, name, 0) loaded a schedule file")) {
        return false;  // never reached: the filter passes it on
    }
}
bool guarded_lookup(uintptr_t container, uintptr_t* out, const uint32_t* key) noexcept {
    __try {
        g_lookup(container, out, key);
        return true;
    } __except (call_fault(GetExceptionInformation(), "0x186A810(container, &file, &key) looked a schedule file up")) {
        return false;
    }
}
bool guarded_release(uintptr_t* handle) noexcept {
    __try {
        g_release(handle);
        return true;
    } __except (call_fault(GetExceptionInformation(), "0x38AD90(&handle) released a schedule file's handle")) {
        return false;
    }
}

// Session 12, the hold: gives back one reference we kept on a schedule file (the NetworkThread). The checks before
// 0x38AD90 (reads only): it is still a pa::NPCScheduleData_File (vtable 0x57DB080, ctor 0x1860C10), its count is
// atomic (+0x14 = 1, the ctor) and it is NOT 1 (1 = only ours: the container dropped it and 0x38AD90 would run its
// destructor on this thread). Any doubt: kept (a leak of one file, never a free). 1 / 0 / -1 as GameEnv::release.
// last_ok: a handle the game gave us in THIS same call on this thread (a refresh, a full table, a load whose file
// the container lacks): given back at once with no check, exactly as the module did before the hold (sessions
// 9-12). If it is the last reference, 0x38AD90 destroys the file here, as the loader does itself on its error path
// (0x1864705-0x1864736, inline release and vf0(1) after the error line 0x35DE50) on the same calling thread.
int release_handle(uintptr_t file, bool last_ok) noexcept {
    if (g_disabled.load(std::memory_order_relaxed) || !g_release) return -1;
    if (last_ok) {
        uintptr_t handle = file;
        BusyGuard busy;
        return guarded_release(&handle) ? 1 : -1;  // -1 never reached: the filter passes it on
    }
    uintptr_t vt = 0;
    uint32_t refs = 0;
    uint8_t atomic = 0;
    if (!file || !rd(file, vt) || vt != g_base + kVtFile || !rd(file + kFile_Refs, refs) ||
        !rd(file + kFile_Atomic, atomic) || !atomic) {
        g_hold_odd.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    if (refs <= 1) {
        g_hold_last.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    uintptr_t handle = file;
    BusyGuard busy;
    return guarded_release(&handle) ? 1 : -1;  // -1 never reached: the filter passes it on
}

// The key's context entry and whether its name hashes back to the key (reads only). Returns the entry (0: none).
uintptr_t entry_and_name(uintptr_t mgr, uint32_t key, bool& name_ok, uint32_t& name_key, gsl::NameRead* out = nullptr) noexcept {
    name_ok = false;
    name_key = 0;
    const uintptr_t entry = gsl::ctx_find(GameMem{}, mgr, key);
    if (!entry) return 0;
    char buf[300];
    const gsl::NameRead n = gsl::read_name(GameMem{}, entry, buf, sizeof buf);
    if (out) *out = n;
    if (!n.ok) return entry;
    name_key = gsl::schedule_key_of_name(std::string_view(buf, n.len));
    name_ok = name_key == key;
    return entry;
}

// The game, for gsl::process_key (the NetworkThread: a game thread, its own heap; nothing of the game held at
// 0x35BAD4, where the game's own loop calls OnRecvPacket later in the same turn).
struct GameEnv {
    const ManagerView& mv;
    uint16_t op = 0;

    // Under the container's own reader lock: the native lookup (it also resets the holder's idle counter, as any
    // client lookup does). 0 absent, 1 present, 2 being unloaded (the native lookup returns 0 for it: the
    // replica tells; review L4: NOT present for the packet, see gsl::process_key).
    // keep (session 12): when found, the handle (a reference) is NOT released but handed to the caller (the hold).
    int present(uint32_t key, uintptr_t* keep) noexcept {
        if (g_disabled.load(std::memory_order_relaxed) || !g_lookup) return replica_present(mv.container, key) ? 1 : 0;
        uintptr_t file = 0;
        bool got = false;
        {
            BusyGuard busy;
            if (!guarded_lookup(mv.container, &file, &key)) return 0;  // never reached: the filter passes it on
            // 0x38AD90 zeroes the handle (0x38AD9F) before dropping its count: decide BEFORE releasing (session 9:
            // 68 files loaded, every check said "absent", 204 calls instead of 68).
            got = file != 0;
            if (got && keep) {
                *keep = file;
            } else if (got && !guarded_release(&file)) {
                return 0;
            }
        }
        if (got) return 1;
        if (replica_lookup(mv.container, key) == scd::Lookup::FileUnloading) {
            g_present_unloading.fetch_add(1, std::memory_order_relaxed);
            return 2;
        }
        return 0;
    }
    gsl::CtxInfo ctx(uint32_t key) noexcept {
        gsl::CtxInfo c;
        gsl::NameRead n;
        c.gen = rdv<uint32_t>(mv.mgr + GL::kCtx_Gen);
        c.used = rdv<uint32_t>(mv.mgr + GL::kCtx_Used);
        c.entry = entry_and_name(mv.mgr, key, c.name_ok, c.name_key, &n);
        c.data = n.data;
        c.chars = n.chars;
        return c;
    }
    // Review 5: right before the call, the table's generation and count, the entry, its String and the hash.
    bool ctx_still(uint32_t key, const gsl::CtxInfo& c) noexcept {
        if (rdv<uint32_t>(mv.mgr + GL::kCtx_Gen) != c.gen || rdv<uint32_t>(mv.mgr + GL::kCtx_Used) != c.used) return false;
        const gsl::CtxInfo again = ctx(key);
        return again.entry == c.entry && again.data == c.data && again.chars == c.chars && again.name_ok;
    }
    gsl::LoadOutcome load(const gsl::CtxInfo& c, bool keep) noexcept {
        gsl::LoadOutcome o;
        if (g_disabled.load(std::memory_order_relaxed) || !g_load) return o;
        uintptr_t handle = 0;
        const uint64_t t0 = p8_now_us();
        {
            BusyGuard busy;
            if (!guarded_load(mv.mgr, &handle, c.entry + GL::kEntry_Name)) return o;  // never reached
            o.got = handle != 0;
            if (handle && keep) {
                o.kept = handle;  // session 12: the loader's reference is the hold (given back by release())
            } else if (handle && !guarded_release(&handle)) {
                return o;
            }
        }
        const uint64_t t1 = p8_now_us();
        o.us = t1 > t0 ? t1 - t0 : 0;
        o.called = true;
        return o;
    }
    // Session 12: gives back a handle (0x38AD90, on this same NetworkThread). 1 released; 0 kept (a held handle whose
    // refcount says we are its LAST reference, i.e. the container dropped it: 0x38AD90 would run its destructor
    // here, so it is never freed by us; or it is unreadable / not a NPCScheduleData_File); -1 not called (off).
    // last_ok: a handle taken in this same call (see release_handle).
    int release(uintptr_t file, bool last_ok) noexcept { return release_handle(file, last_ok); }
    // Who a hold belongs to now: the client connection's epoch and liveness, this manager and its container.
    gsl::HoldOwner hold_owner() noexcept {
        gsl::HoldOwner o;
        o.epoch = guest_connection_epoch();
        o.live = guest_remote_live();
        o.mgr = mv.mgr;
        o.container = mv.container;
        return o;
    }
    uint64_t now() noexcept { return p8_now_us(); }
    void note(gsl::Note n, uint32_t key, uint16_t kop, uint64_t a, uint64_t b) noexcept {
        p::Event e;
        e.v[0] = key;
        e.v[1] = static_cast<uint64_t>(n);
        e.v[2] = kop;
        e.v[3] = a;
        e.v[4] = b;
        e.v[5] = container_used(mv.container);
        e.v[6] = mv.mgr;
        e.v[7] = mv.client_mgr;
        e.v[8] = static_cast<uint64_t>(mv.vt_ok) | static_cast<uint64_t>(mv.container != 0) << 1 |
                 static_cast<uint64_t>(mv.fs != 0) << 2;
        switch (n) {
            case gsl::Note::Load: emit(kELoad, e); break;
            case gsl::Note::Would: emit(kEWould, e); break;
            case gsl::Note::Latch: emit(kELatch, e); break;
            case gsl::Note::HoldEnd: emit(kEHoldEnd, e); break;
            case gsl::Note::Early: emit(kEEarly, e); break;
            case gsl::Note::Late: emit(kELate, e); break;
            default: emit(kEDecided, e); break;
        }
    }
};

// ---------------------------------------------------------------- the lookups (read-only watch, any thread)

// Session 12: the 2335's own lookup (0xA9FEE0 -> 0x1862A80, a client thread): when, found or not (the replica, at
// the lookup's entry). Every call of a key a packet named (not sampled): it ends the hold (service_holds) and it
// is the direct measure of family (c) ("no stage in X": the file was gone when the 2335 ran).
void on_stage_lookup(uint32_t key, uintptr_t mgr) noexcept {
    auto* s = g_eng.book.get(key, false);
    if (!s) {
        g_stage_unnamed.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (tls_realm() != 0 || rdp(mgr) != g_base + GL::kVtManager) return;
    s->stage_lookup_us.store(p8_now_us(), std::memory_order_release);
    const bool held = s->held.load(std::memory_order_acquire) != 0;
    if (replica_present(rdp(mgr + GL::kManager_Container), key)) {
        s->stage_found.fetch_add(1, std::memory_order_relaxed);
        g_stage_found.fetch_add(1, std::memory_order_relaxed);
        if (held) g_stage_found_held.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    s->stage_missing.fetch_add(1, std::memory_order_relaxed);
    g_stage_missing.fetch_add(1, std::memory_order_relaxed);
    const KeyState st = g_eng.book.state(s);
    const bool ours = st == KeyState::Loaded || st == KeyState::Present;
    if (ours) g_stage_missing_after.fetch_add(1, std::memory_order_relaxed);
    p::Event e;
    e.v[0] = key;
    e.v[1] = static_cast<uint64_t>(st);
    e.v[2] = held;
    e.v[3] = s->hold_end.load(std::memory_order_relaxed);
    e.v[4] = s->wanted.load(std::memory_order_relaxed);
    e.v[5] = s->by2335.load(std::memory_order_relaxed);
    emit(kEStage, e);
}

// Review 8: every call is counted (one relaxed add); the full path (TLS realm, the replica, the book) runs for
// 1 call in kLookupSample per thread, and always for a key we loaded and have not seen found yet.
void on_lookup(uint32_t key, uintptr_t mgr, int which, bool stage = false) noexcept {
    if (!key) return;
    g_lk_all[which].fetch_add(1, std::memory_order_relaxed);
    if (t_busy) {  // inside our own call (the loader's own lookups, our presence check): not the client's
        g_reentry.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (stage) on_stage_lookup(key, mgr);
    auto* known = g_eng.book.get(key, false);
    const bool verify = known && g_eng.book.state(known) == KeyState::Loaded &&
                        known->found_after_load.load(std::memory_order_relaxed) == 0;
    if (!verify && ++t_sample % kLookupSample != 0) return;
    if (tls_realm() != 0) {
        g_lk_server[which].fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (rdp(mgr) != g_base + GL::kVtManager) {
        g_lk_other_mgr[which].fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_lk_calls[which].fetch_add(1, std::memory_order_relaxed);
    const bool present = replica_present(rdp(mgr + GL::kManager_Container), key);
    auto* slot = known ? known : g_eng.book.get(key);
    const KeyState st = g_eng.book.state(slot);
    if (present) {
        g_lk_found[which].fetch_add(1, std::memory_order_relaxed);
        if (slot) slot->found.fetch_add(1, std::memory_order_relaxed);
        if (st == KeyState::Loaded) {
            g_found_after_load.fetch_add(1, std::memory_order_relaxed);
            if (slot) slot->found_after_load.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }
    g_lk_missing[which].fetch_add(1, std::memory_order_relaxed);
    if (slot) slot->missing.fetch_add(1, std::memory_order_relaxed);
    if (st == KeyState::Loaded) g_missing_after_load.fetch_add(1, std::memory_order_relaxed);
    uint8_t zero = 0;
    if (slot && !slot->first_miss_noted.compare_exchange_strong(zero, 1, std::memory_order_relaxed)) return;
    p::Event e;
    e.v[0] = key;
    e.v[1] = static_cast<uint64_t>(which);
    e.v[2] = static_cast<uint64_t>(st);
    e.v[3] = slot ? slot->wanted.load(std::memory_order_relaxed) : 0;
    e.v[4] = mgr;
    emit(kEMiss, e);
}
void on_node_lookup(safetyhook::Context& ctx) noexcept {
    if (!g_watch_on.load(std::memory_order_relaxed)) return;
    on_lookup(static_cast<uint32_t>(ctx.rdx), rdp(g_base + GL::kManagerSlot), 0);
}
void on_key_lookup(safetyhook::Context& ctx) noexcept {
    if (!g_watch_on.load(std::memory_order_relaxed)) return;
    // At the entry, [rsp] is the return address (as remote_puppet's entry hook reads it): 0xA9FFC8 = the 2335.
    const bool stage = g_stage_ret_ok.load(std::memory_order_relaxed) && rdp(ctx.rsp) == g_base + kStageRetRva;
    on_lookup(static_cast<uint32_t>(ctx.r8), ctx.rcx, 1, stage);
}

// ---------------------------------------------------------------- lines (diagnostics thread)

std::string name_of(uint32_t key) {
    const ManagerView mv = manager_view();
    const uintptr_t entry = gsl::ctx_find(GameMem{}, mv.mgr, key);
    if (!entry) return "(no context entry)";
    char buf[300];
    const gsl::NameRead n = gsl::read_name(GameMem{}, entry, buf, sizeof buf);
    if (!n.ok) return "(name unreadable)";
    const std::string_view s(buf, n.len);
    const uint32_t h = gsl::schedule_key_of_name(s);
    return std::format("'{}'{}", s, h == key ? "" : std::format(" (hashes to 0x{:08X}, not the key)", h));
}

const char* op_text(uint64_t op) {
    switch (op) {
        case gsl::kOpActorLoad: return "a 2005 (NPC records)";
        case gsl::kOpScheduleCreate: return "a 2335";
        case gsl::kOpInitSchedule: return "a 2874";
        case 0: return "the retry set";
        default: return "?";
    }
}
const char* note_text(gsl::Note n) {
    switch (n) {
        case gsl::Note::Gate: return "a gate failed (thread / manager)";
        case gsl::Note::NoCtx: return "no context entry (decided again on the next packet)";
        case gsl::Note::BadName: return "the context name does not hash to the key (latched)";
        case gsl::Note::Budget: return "budget spent: kept in the retry set";
        case gsl::Note::Cap: return "the session cap is reached";
        case gsl::Note::GaveUp: return "gave up after failures";
        case gsl::Note::CtxChanged: return "the context table changed right before the call: kept in the retry set";
        default: return "?";
    }
}

std::string line_of(const p::Event& e) {
    const uint64_t* v = e.v;
    const std::string when = p8_when(e.qpc_us);
    const uint32_t key = static_cast<uint32_t>(v[0]);
    switch (e.kind) {
        case kELoad: {
            const auto st = static_cast<KeyState>(v[4] & 0xFF);
            return std::format("GuestSchedLoad [load] {} T{} | resource 0x{:08X} {} named by {}: 0x1864140 -> handle {}, "
                               "container {} -> {} in {:.2f} ms{} (attempts {}; container files {}){}",
                               when, e.tid, key, name_of(key), op_text(v[2]), (v[4] >> 8) & 1 ? "set" : "NULL",
                               (v[4] >> 9) & 1 ? "has it" : "LACKS it", gsl::state_name(st),
                               static_cast<double>(v[3]) / 1000.0, v[3] > gsl::kSlowLoadUs ? " SLOW" : "",
                               (v[4] >> 16) & 0xFF, v[5],
                               (v[4] >> 24) & 1 ? " | HELD (handle kept until its 2335 ran)" : "");
        }
        case kEHoldEnd: {
            const auto why = static_cast<gsl::HoldEnd>(v[3]);
            return std::format("GuestSchedLoad [hold end] {} T{} | resource 0x{:08X} {}: our handle {} {}, after {:.1f} "
                               "s held (container files {})",
                               when, e.tid, key, name_of(key),
                               why == gsl::HoldEnd::Kept || why == gsl::HoldEnd::Dropped ? "is" : "given back (0x38AD90)", gsl::hold_end_name(why),
                               static_cast<double>(v[4]) / 1e6, v[5]);
        }
        case kEEarly:
            return std::format("GuestSchedLoad [released early] {} T{} | resource 0x{:08X} {}: a 2335 named it, the "
                               "container had it ({}) and it is GONE before that 2335 ran (no 0xA9FEE0 lookup since); "
                               "named now by {}; last hold: {}",
                               when, e.tid, key, name_of(key), gsl::state_name(static_cast<KeyState>(v[3])),
                               op_text(v[2]),
                               v[4] ? gsl::hold_end_name(static_cast<gsl::HoldEnd>(v[4] - 1)) : "never held");
        case kEStage:
            return std::format("GuestSchedLoad [2335 lookup miss] {} T{} | resource 0x{:08X} {}: the 2335 ran "
                               "(0xA9FEE0 -> 0x1862A80) and the container has NO file: its stage is not created | "
                               "state {}, {}named by a 2335, held now {}, last hold: {}, named {} packet(s)",
                               when, e.tid, key, name_of(key), gsl::state_name(static_cast<KeyState>(v[1])),
                               v[5] ? "" : "NOT ", v[2] ? "YES (?)" : "no",
                               v[3] ? gsl::hold_end_name(static_cast<gsl::HoldEnd>(v[3] - 1)) : "never held", v[4]);
        case kEWould:
            return std::format("GuestSchedLoad [would load] {} T{} | resource 0x{:08X} {} named by {}: not in the "
                               "container ({} files), context entry 0x{:X}, name hashes back to the key: 0x1864140 "
                               "WOULD be called here ({})",
                               when, e.tid, key, name_of(key), op_text(v[2]), v[5], v[3],
                               g_eng.latched.load() ? "LATCHED to probe after a slow load" : "probe");
        case kEDecided: {
            const auto n = static_cast<gsl::Note>(v[1]);
            return std::format("GuestSchedLoad [not loaded] {} T{} | resource 0x{:08X} named by {}: {}{} | manager "
                               "0x{:X} (client's 0x{:X}; vtable {}, container {}, fs {})",
                               when, e.tid, key, op_text(v[2]), note_text(n),
                               n == gsl::Note::Gate     ? std::format(" ({})", gsl::why_name(static_cast<Why>(v[3])))
                               : n == gsl::Note::BadName && v[3]
                                   ? std::format(" (the name hashes to 0x{:08X})", static_cast<uint32_t>(v[3]))
                                   : std::string(),
                               v[6], v[7], v[8] & 1 ? "ok" : "WRONG", v[8] & 2 ? "ok" : "none", v[8] & 4 ? "ok" : "none");
        }
        case kEMiss:
            return std::format("GuestSchedLoad [lookup miss] {} T{} | resource 0x{:08X} {}: {} found no file (first "
                               "sampled miss of this key; its state: {}, named by {} packet(s) before delivery){}",
                               when, e.tid, key, name_of(key),
                               v[1] == 0 ? "0x1869D70 (AI block / 2874 node)" : "0x1862A80 (2335 / stage lookup)",
                               gsl::state_name(static_cast<KeyState>(v[2])), v[3],
                               v[3] == 0 ? " | NO host packet named it before this lookup (the scan missed it, or it "
                                           "is this client's own)"
                                         : "");
        case kELate:
            if (static_cast<Why>(v[3]) == Why::Count) {
                return std::format("GuestSchedLoad [LATE] {} T{} | {} named {} schedule key(s) beyond the per-packet cap "
                                   "of {}: they wait in the retry set and the packet is delivered WITHOUT them",
                                   when, e.tid, op_text(v[2]), v[4], gsl::kMaxKeysPerPacket);
            }
            return std::format("GuestSchedLoad [LATE] {} T{} | resource 0x{:08X} {} named by {} is NOT in the container "
                               "when that packet is delivered: {} (state {}; container files {}) | the game reads the "
                               "packet without the file (a 2005's AI block resolves no node for good)",
                               when, e.tid, key, name_of(key), op_text(v[2]),
                               static_cast<Why>(v[3]) == Why::Load ? "loaded, but the container still lacks it"
                                                                    : gsl::why_name(static_cast<Why>(v[3])),
                               gsl::state_name(static_cast<KeyState>(v[4])), v[5]);
        case kERealm:
            return std::format("GuestSchedLoad [thread] {} T{} | the delivering thread's TLS realm byte +0x1EC is {} "
                               "(expected 0, the client's): nothing is loaded from it",
                               when, e.tid, static_cast<int64_t>(v[0]));
        default:
            return std::format("GuestSchedLoad: event {}?", e.kind);
    }
}

// The schedule-context keys for the 2005 scan: built when the table is there, REBUILT when its generation or count
// changes (review 9; the old set stays allocated: the NetworkThread may still hold it).
uint32_t g_built_gen = 0, g_built_used = 0, g_builds = 0;  // diagnostics thread only
void build_keys() {
    const ManagerView mv = manager_view();
    if (!mv.vt_ok || !mv.ctx_used) return;
    const bool have = g_keys.load(std::memory_order_acquire) != nullptr;
    if (have && mv.ctx_gen == g_built_gen && mv.ctx_used == g_built_used) return;
    if (g_builds >= kMaxKeySetBuilds) return;
    std::vector<uint32_t> keys;
    if (!gsl::ctx_keys(GameMem{}, mv.mgr, keys)) return;
    size_t sampled = 0, matched = 0;
    std::string samples;
    for (size_t i = 0; i < keys.size() && sampled < 64; i += std::max<size_t>(1, keys.size() / 64)) {
        bool ok = false;
        uint32_t nk = 0;
        if (!entry_and_name(mv.mgr, keys[i], ok, nk)) continue;
        ++sampled;
        if (ok) ++matched;
        if (sampled <= 4) samples += std::format(" 0x{:08X} {};", keys[i], name_of(keys[i]));
    }
    auto* set = new gsl::KeySet(std::move(keys));  // never freed (the NetworkThread may hold it)
    g_keys.store(set, std::memory_order_release);
    ++g_builds;
    log::info("GuestSchedLoad [context] {} the schedule-context table (mgr+0x10, generation {} -> {}, count {} -> {}): "
              "{} keys, names hash back to their key {}/{} sampled (e.g.{}){}",
              have ? "REBUILT the key set from" : "the key set from", g_built_gen, mv.ctx_gen, g_built_used,
              mv.ctx_used, set->size(), matched, sampled, samples,
              matched == sampled && sampled ? "" : " | NOT ALL NAMES HASH TO THEIR KEY: those keys are never loaded");
    g_built_gen = mv.ctx_gen;
    g_built_used = mv.ctx_used;
}

std::string state_text(const ManagerView& mv) {
    return std::format("manager [exe+0x{:X}] 0x{:X} (vtable {}), the client's [[exe+0x{:X}]+0x40] 0x{:X} ({}), the "
                       "server's [[exe+0x{:X}]+0x40] 0x{:X} ({}) | container 0x{:X} (files {}) | fs 0x{:X} | context "
                       "generation {} count {} | ready for loads: {}",
                       GL::kManagerSlot, mv.mgr, mv.vt_ok ? "pa::NPCScheduleManager" : "WRONG", GL::kClientRootSlot,
                       mv.client_mgr, mv.client_mgr == mv.mgr ? "the same" : "DIFFERENT", GL::kServerRootSlot,
                       mv.server_mgr,
                       !mv.server_mgr ? "not set" : mv.server_mgr == mv.mgr ? "the same" : "DIFFERENT", mv.container,
                       container_used(mv.container), mv.fs, mv.ctx_gen, mv.ctx_used, mv.ok ? "yes" : "NO");
}

std::string g_last_state;  // diagnostics thread only
void state_line(bool always) {
    const ManagerView mv = manager_view();
    const std::string s = state_text(mv);
    if (!always && s == g_last_state) return;
    g_last_state = s;
    log::info("GuestSchedLoad [state] {} | {}{}", p8_when(p8_now_us()), s,
              guest_remote_live() ? " | playing on the host" : " | not playing on the host");
}

uint64_t g_tick_n = 0;  // diagnostics thread only
uint64_t g_last_sum_us = 0, g_last_lk_all[2] = {};

void summary() {
    std::string why;
    for (size_t k = 0; k < static_cast<size_t>(Why::Count); ++k) {
        if (const uint64_t n = g_eng.why[k].load()) why += std::format(" {} x{};", gsl::why_name(static_cast<Why>(k)), n);
    }
    uint32_t st[static_cast<size_t>(KeyState::Count)] = {};
    std::string missing;
    size_t listed = 0;
    for (size_t i = 0; i < g_eng.book.capacity(); ++i) {
        const auto& s = g_eng.book.at(i);
        const uint32_t k = s.key.load();
        if (!k) continue;
        const auto x = static_cast<KeyState>(s.state.load());
        if (static_cast<size_t>(x) < std::size(st)) ++st[static_cast<size_t>(x)];
        if (s.missing.load() && listed < 12) {
            ++listed;
            missing += std::format(" 0x{:08X} ({}, found {}, missing {}, named {})", k, gsl::state_name(x),
                                   s.found.load(), s.missing.load(), s.wanted.load());
        }
    }
    std::string hist;
    for (size_t b = 0; b < gsl::Histogram::kBuckets; ++b) {
        hist += std::format(" {}{}ms {}", b < std::size(gsl::Histogram::kEdgesUs) ? "<" : ">=",
                            (b < std::size(gsl::Histogram::kEdgesUs) ? gsl::Histogram::kEdgesUs[b]
                                                                       : gsl::Histogram::kEdgesUs[b - 1]) /
                                1000,
                            g_eng.hist.n[b].load());
    }
    const uint64_t now = p8_now_us();
    const double secs = g_last_sum_us && now > g_last_sum_us ? static_cast<double>(now - g_last_sum_us) / 1e6 : 0.0;
    double rate[2] = {};
    for (int w = 0; w < 2; ++w) {
        const uint64_t all = g_lk_all[w].load();
        rate[w] = secs > 0 ? static_cast<double>(all - g_last_lk_all[w]) / secs : 0.0;
        g_last_lk_all[w] = all;
    }
    g_last_sum_us = now;
    const gsl::KeySet* ks = g_keys.load();
    log::info("GuestSchedLoad summary (guest_schedule_load={}{}): context keys {} (key set builds {}) | before delivery: "
              "2005 x{} (NPC records {}, key hits {}, untiled {}), 2335 x{}, 2874 x{}, keys named {}, spilled to the "
              "retry set {} | decisions:{} | LOADED {} failed {} (calls {} of cap {}) | load time: total {:.1f} ms, max "
              "{:.2f} ms, slow (>{} ms) {}, histogram:{} | retry set {} (dropped {}, retried {}) | present while being "
              "unloaded {} | keys: present {} loaded {} would {} failed {} no-context {} bad-name {} (book {} / {}, "
              "untracked {}, overflow book {}, untracked failures {}) | lookups 0x1869D70 {:.0f}/s ({} sampled: found "
              "{}, missing {}), 0x1862A80 {:.0f}/s ({} sampled: found {}, missing {}), server-thread {} / {}, other "
              "manager {} / {}, inside our calls {} | VERIFY: found after our load {}, missing after our load {} | keys "
              "with misses:{} | hooks {}/2, faults {}{}, limited {} lost {}",
              g_mode.load() == 2 ? "1" : "probe", g_eng.latched.load() ? ", LATCHED TO PROBE after a slow load" : "",
              ks ? ks->size() : 0, g_builds, g_packets[0].load(), g_npc_records.load(), g_scan_hits.load(),
              g_untiled.load(), g_packets[1].load(), g_packets[2].load(), g_keys_named.load(), g_spilled.load(),
              why.empty() ? " none" : why, g_eng.loads_ok.load(), g_eng.loads_failed.load(), g_eng.loads_total.load(),
              gsl::kSessionCap, static_cast<double>(g_eng.hist.total_us.load()) / 1000.0,
              static_cast<double>(g_eng.hist.max_us.load()) / 1000.0, gsl::kSlowLoadUs / 1000, g_eng.slow.load(), hist,
              g_eng.retry.size(), g_eng.retry.dropped(), g_eng.retried.load(), g_present_unloading.load(),
              st[static_cast<size_t>(KeyState::Present)], st[static_cast<size_t>(KeyState::Loaded)],
              st[static_cast<size_t>(KeyState::Would)], st[static_cast<size_t>(KeyState::Failed)],
              st[static_cast<size_t>(KeyState::NoCtx)], st[static_cast<size_t>(KeyState::BadName)], g_eng.book.used(),
              g_eng.book.capacity(), g_eng.book.untracked(), g_eng.overflow.used(), g_eng.untracked_failures.load(),
              rate[0], g_lk_calls[0].load(), g_lk_found[0].load(), g_lk_missing[0].load(), rate[1],
              g_lk_calls[1].load(), g_lk_found[1].load(), g_lk_missing[1].load(), g_lk_server[0].load(),
              g_lk_server[1].load(), g_lk_other_mgr[0].load(), g_lk_other_mgr[1].load(), g_reentry.load(),
              g_found_after_load.load(), g_missing_after_load.load(), missing.empty() ? " none" : missing,
              g_hooked.load(), g_faults.load(), g_disabled.load() ? " (SWITCHED OFF after a fault in the game's code)" : "",
              g_limited.load(), g_lost.load());
    // Session 13: the keys a packet named that were not in the container when it was delivered (§11).
    std::string late;
    for (size_t k = 0; k < static_cast<size_t>(Why::Count); ++k) {
        if (const uint64_t n = g_eng.late_why[k].load()) {
            late += std::format(" {} x{};",
                                static_cast<Why>(k) == Why::Load ? "loaded, container still lacks it"
                                                                  : gsl::why_name(static_cast<Why>(k)),
                                n);
        }
    }
    const uint64_t recent_block = g_eng.block.take_recent();
    log::info("GuestSchedLoad [before delivery] every key a packet names is loaded before it is delivered (no count; "
              "{} ms of loads per packet and {} ms per second; the retry set: {} ms per packet, {} ms per second; "
              "probe after one load over {} ms or {} over {} ms: slow loads {}) | NAMED BUT NOT IN THE CONTAINER AT "
              "DELIVERY (LATE, should be 0): {}{} | keys beyond the per-packet cap of {}: {} | decided again after a "
              "context change {} | files being unloaded met {} (decided again at the end of their packet {}) | the "
              "longest delivery's loads {:.2f} ms | NetworkThread time in this module: max per second {:.1f} ms "
              "(since the last summary {:.1f} ms), one delivery {:.1f} ms, seconds over {} ms {}",
              gsl::kOwnPacketCapUs / 1000, gsl::kOwnWindowLoadUs / 1000, gsl::kRetryPacketUs / 1000,
              gsl::kWindowLoadUs / 1000, gsl::kLatchUs / 1000, gsl::kSlowLatchCount, gsl::kSlowLoadUs / 1000,
              g_eng.slow.load(), g_eng.late.load(), late.empty() ? "" : std::format(" ({})", late),
              gsl::kMaxKeysPerPacket, g_eng.late_spilled.load(), g_eng.ctx_redo.load(), g_eng.unloading.load(),
              g_eng.unloading_rechecked.load(), static_cast<double>(g_eng.packet_max_us.load()) / 1000.0,
              static_cast<double>(g_eng.block.max_second_us.load()) / 1000.0,
              static_cast<double>(recent_block) / 1000.0, static_cast<double>(g_eng.block.max_call_us.load()) / 1000.0,
              gsl::kBlockWarnUs / 1000, g_eng.block.seconds_over.load());
    // Session 12: the hold (p8-impl-horarios.md §10).
    const auto& he = g_eng.hold_ends;
    log::info("GuestSchedLoad [hold] (until the 2335 ran + {} s, at most {} s after the last 2335 named it): held now "
              "{} (peak {} of {}) | taken {}, named again while held {}, table full {} | given back: after the 2335 "
              "ran {}, by TIMEOUT {} | not given back: last reference {} (odd file {}), module off {}, DROPPED (another "
              "connection / manager / container, or the connection ended: no call) {} | 2335 lookups "
              "(0xA9FEE0 -> 0x1862A80{}): found {} (while held {}), MISSING {} (of files the container had for us {}), "
              "keys no packet named {} | RELEASED BEFORE THEIR 2335 RAN (seen at load) {}",
              gsl::kHoldGraceUs / 1'000'000, gsl::kHoldMaxUs / 1'000'000, g_eng.held_now.load(), g_eng.held_peak.load(),
              gsl::kHolds, g_eng.holds_taken.load(), g_eng.holds_refreshed.load(), g_eng.holds_full.load(),
              he[static_cast<size_t>(gsl::HoldEnd::Seen)].load(), he[static_cast<size_t>(gsl::HoldEnd::Timeout)].load(),
              he[static_cast<size_t>(gsl::HoldEnd::Kept)].load(), g_hold_odd.load(), g_eng.holds_dropped.load(),
              he[static_cast<size_t>(gsl::HoldEnd::Dropped)].load(),
              g_stage_ret_ok.load() ? "" : "; NOT WATCHED: the call at 0xA9FFC3 differs, holds end by timeout",
              g_stage_found.load(), g_stage_found_held.load(), g_stage_missing.load(), g_stage_missing_after.load(),
              g_stage_unnamed.load(), g_eng.early.load());
}

// ---------------------------------------------------------------- install

uint64_t fnv1a(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}
bool body_is(const Body& b) {
    std::vector<uint8_t> v(b.end - b.rva);
    return game::safe_read(g_base + b.rva, v.data(), v.size()) && fnv1a(v.data(), v.size()) == b.fnv;
}

DWORD WINAPI install_thread(LPVOID) {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::vector<gad::Section> secs;
    if (g_base != off::kImageBase || !game::safe_read(g_base, &dos, sizeof dos) ||
        !game::safe_read(g_base + dos.e_lfanew, &nt, sizeof nt) || nt.FileHeader.TimeDateStamp != off::kPeTimestamp ||
        !p8_exe_sections(g_base, secs)) {
        log::error("GuestSchedLoad: unsupported game build or unreadable sections; nothing installed");
        return 0;
    }
    for (int i = 0; i < 1200; ++i) {  // WorldSystem (up to 10 minutes), as the other guest modules
        uintptr_t world = 0;
        if (game::safe_read(g_base + off::kWorldSystemSlot, &world, sizeof world) && world) break;
        Sleep(500);
    }
    const Body bodies[] = {kLoader, kReader, kLookup, kRelease};
    bool code = false;
    for (int i = 0; i < 24000 && !code; ++i) {  // the code (up to 2 minutes)
        code = std::all_of(std::begin(bodies), std::end(bodies), body_is);
        if (!code) Sleep(5);
    }
    const bool in_rsrc = std::all_of(std::begin(bodies), std::end(bodies), [&](const Body& b) {
        return gad::in_real_code(secs, static_cast<uint32_t>(b.rva), static_cast<uint32_t>(b.end - b.rva));
    });
    if (code && in_rsrc) {
        g_load = reinterpret_cast<LoadFn>(g_base + kLoader.rva);
        g_lookup = reinterpret_cast<LookupFn>(g_base + kLookup.rva);
        g_release = reinterpret_cast<ReleaseFn>(g_base + kRelease.rva);
        g_code_ok.store(true, std::memory_order_release);
    } else {
        log::error("GuestSchedLoad: the loader 0x1864140 / 0x18643B0, the lookup 0x186A810 or the release 0x38AD90 is "
                   "not build 2976's .rsrc code; nothing will be loaded (the lookups are still watched)");
    }
    // Session 12: the 2335's call into the hooked lookup (read only; it is not hooked).
    uint8_t call[sizeof kStageCallBytes] = {};
    g_stage_ret_ok.store(game::safe_read(g_base + kStageCallRva, call, sizeof call) &&
                         std::equal(std::begin(call), std::end(call), std::begin(kStageCallBytes)) &&
                         gad::in_real_code(secs, static_cast<uint32_t>(kStageCallRva), sizeof call));
    if (p8_install_mid(g_base, secs, kNodeLookupRva, 5, kNodeLookupBytes, on_node_lookup, g_hook_node,
                       "GuestSchedLoad", "the node lookup 0x1869D70 (entry)")) {
        g_hooked.fetch_add(1);
    }
    if (p8_install_mid(g_base, secs, kKeyLookupRva, 5, kKeyLookupBytes, on_key_lookup, g_hook_key, "GuestSchedLoad",
                       "the file lookup 0x1862A80 (entry)")) {
        g_hooked.fetch_add(1);
    }
    g_watch_on.store(true);
    const auto mode = static_cast<gsl::Mode>(g_mode.load());
    log::info("GuestSchedLoad {} (guest_schedule_load): loader {} | lookups watched {}/2 (0x1869D70, 0x1862A80; read "
              "only, 1 in {} sampled) | before each host 2005 / 2335 / 2874 is delivered (client NetworkThread): the "
              "schedule files it names that the container lacks (native lookup 0x186A810, its reader lock) {} with "
              "0x1864140(mgr, &handle, context name, 0) + 0x38AD90 | session 13: a key the packet names is not "
              "deferred by a count (only once this packet's loads took {} ms or this second's {} ms: LATE); the retry "
              "set: {} ms per packet, {} ms per second; {} loads per session; one load over {} ms, or {} over {} ms, "
              "latch probe | session 12: the "
              "handle of a file a 2335 names is KEPT until that 2335 ran (0xA9FEE0's call at 0xA9FFC3 {}) + {} s, at "
              "most {} s; up to {} files",
              gsl::mode_name(mode),
              g_code_ok.load() ? "verified (whole bodies of 0x1864140, 0x18643B0, 0x186A810, 0x38AD90)" : "REFUSED",
              g_hooked.load(), kLookupSample, mode == gsl::Mode::On ? "are LOADED" : "would be loaded (probe: nothing called)",
              gsl::kOwnPacketCapUs / 1000, gsl::kOwnWindowLoadUs / 1000, gsl::kRetryPacketUs / 1000,
              gsl::kWindowLoadUs / 1000, gsl::kSessionCap, gsl::kLatchUs / 1000, gsl::kSlowLatchCount,
              gsl::kSlowLoadUs / 1000,
              g_stage_ret_ok.load() ? "verified" : "DIFFERS: holds end by timeout only", gsl::kHoldGraceUs / 1'000'000,
              gsl::kHoldMaxUs / 1'000'000, gsl::kHolds);
    return 0;
}

// ---------------------------------------------------------------- the read-only self-check (host or guest)
//
// [session2] schedule_load_selfcheck = 1: what guest_schedule_load=1 would rely on, measured on THIS process
// (the host's PC has the same client): the manager's identity (the client's and the server's roots), its container
// and file system, EVERY schedule-context name hashed back to its key, and which of the 25 resources the guest
// lacked in session 8 this client's container holds. Its own thread, SEH-guarded reads only: no hook, no call into
// the game, no write. A line on change (every 30 s), and every 10 minutes.

// The 25 resources of session 8's guest G2 ("container empty"; the most NPCs first: 0xAC00B3BB 54 ... 0xF56759DA
// the sheep 12C/134 of scene 2000018B).
constexpr uint32_t kS8GuestMissing[] = {
    0xAC00B3BB, 0x5FB728ED, 0x79AF6B66, 0xAD5D3465, 0x3F8972FE, 0x6FF7E5A6, 0xA6BD9C7B, 0xF56759DA, 0x7B788062,
    0x57360156, 0x6C4DE020, 0xDE65F7C5, 0xB9F63F22, 0xDA8B15D0, 0xB1AB5208, 0xF47139E7, 0xA8B92663, 0x91E0319E,
    0x9BEEBAD1, 0xA32AB809, 0x33DAD3EF, 0x5A97CE56, 0xA8BB67EE, 0xBC8A6B7F, 0xFC6638C8};

bool g_selfcheck_host = false;

void selfcheck_context(const ManagerView& mv) {
    std::vector<uint32_t> keys;
    if (!mv.vt_ok || !gsl::ctx_keys(GameMem{}, mv.mgr, keys)) {
        log::info("ScheduleSelfCheck [context] the schedule-context table (mgr+0x10) is EMPTY or unreadable (manager "
                  "0x{:X}, vtable {}): no name to load any file by",
                  mv.mgr, mv.vt_ok ? "ok" : "WRONG");
        return;
    }
    size_t named = 0, matched = 0;
    std::string bad, samples;
    for (const uint32_t k : keys) {
        bool ok = false;
        uint32_t nk = 0;
        if (!entry_and_name(mv.mgr, k, ok, nk)) continue;
        if (nk) ++named;
        if (ok) {
            ++matched;
            if (matched <= 4) samples += std::format(" 0x{:08X} {};", k, name_of(k));
        } else if (bad.size() < 600) {
            bad += std::format(" 0x{:08X} {};", k, name_of(k));
        }
    }
    log::info("ScheduleSelfCheck [context] the schedule-context table (mgr+0x10) has {} keys; names read {}; names that "
              "hash back to their key (lookup3 0xC5EDE of the base name, as 0x1864140): {}/{} {} | e.g.{}{}",
              keys.size(), named, matched, keys.size(),
              matched == keys.size() ? "ALL (the loader would file each under the same key)" : "NOT ALL",
              samples.empty() ? " none" : samples, bad.empty() ? "" : std::format(" | mismatches:{}", bad));
}

std::string selfcheck_keys(const ManagerView& mv) {
    std::string present, absent_ctx, no_ctx;
    size_t n_present = 0;
    for (const uint32_t k : kS8GuestMissing) {
        const bool in_container = mv.vt_ok && replica_present(mv.container, k);
        bool ok = false;
        uint32_t nk = 0;
        const uintptr_t entry = mv.vt_ok ? entry_and_name(mv.mgr, k, ok, nk) : 0;
        if (in_container) {
            ++n_present;
            present += std::format(" 0x{:08X}", k);
        } else if (entry) {
            absent_ctx += std::format(" 0x{:08X}{}", k, ok ? "" : "(bad name)");
        } else {
            no_ctx += std::format(" 0x{:08X}", k);
        }
    }
    return std::format("this {} client's container holds {}/{} of the resources the guest lacked in session 8 | "
                       "present:{} | absent, with a context entry (loadable):{} | without a context entry:{}",
                       g_selfcheck_host ? "HOST's" : "guest's", n_present, std::size(kS8GuestMissing),
                       present.empty() ? " none" : present, absent_ctx.empty() ? " none" : absent_ctx,
                       no_ctx.empty() ? " none" : no_ctx);
}

DWORD WINAPI selfcheck_thread(LPVOID) {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (g_base != off::kImageBase || !game::safe_read(g_base, &dos, sizeof dos) ||
        !game::safe_read(g_base + dos.e_lfanew, &nt, sizeof nt) || nt.FileHeader.TimeDateStamp != off::kPeTimestamp) {
        log::error("ScheduleSelfCheck: unsupported game build; nothing read");
        return 0;
    }
    for (int i = 0; i < 1200; ++i) {  // WorldSystem (up to 10 minutes)
        uintptr_t world = 0;
        if (game::safe_read(g_base + off::kWorldSystemSlot, &world, sizeof world) && world) break;
        Sleep(500);
    }
    log::info("ScheduleSelfCheck ON ({}, read only: no hook, no call, no write): the schedule manager's identity, its "
              "context names and the session-8 resources, every 30 s on change",
              g_selfcheck_host ? "host" : "guest");
    bool context_done = false;
    std::string last_state, last_keys;
    for (uint64_t n = 0;; ++n) {
        try {
            const ManagerView mv = manager_view();
            if (!context_done && mv.vt_ok && mv.ctx_used) {
                selfcheck_context(mv);
                context_done = true;
            }
            const bool periodic = n % 20 == 0;  // 10 minutes
            const std::string s = state_text(mv);
            if (periodic || s != last_state) log::info("ScheduleSelfCheck [state] {} | {}", p8_when(p8_now_us()), s);
            last_state = s;
            const std::string k = selfcheck_keys(mv);
            if (periodic || k != last_keys) log::info("ScheduleSelfCheck [keys] {} | {}", p8_when(p8_now_us()), k);
            last_keys = k;
        } catch (...) {
        }
        Sleep(30'000);
    }
}

}  // namespace

void guest_sched_load_start(const std::string& mode) {
    bool bad = false;
    const gsl::Mode m = gsl::parse_mode(mode, &bad);
    if (bad) log::warn("GuestSchedLoad: guest_schedule_load={} is not 0, probe or 1: off", mode);
    g_mode.store(static_cast<uint8_t>(m));
    if (m == gsl::Mode::Off) return;
    if (g_started.exchange(true)) return;
    CloseHandle(CreateThread(nullptr, 0, install_thread, nullptr, 0, nullptr));
}

void schedule_selfcheck_start(bool on, bool host) {
    static std::atomic<bool> started{false};
    if (!on || started.exchange(true)) return;
    g_selfcheck_host = host;
    CloseHandle(CreateThread(nullptr, 0, selfcheck_thread, nullptr, 0, nullptr));
}

bool guest_sched_load_wanted() { return g_started.load(); }

// Review M3: the NetworkThread's time in guest_sched_load_before_deliver, summed per second (gsl::BlockMeter); a
// [BLOCK] line the first time a second reaches gsl::kBlockWarnUs.
struct BlockTimer {
    uint64_t t0 = p8_now_us();
    uint16_t op = 0;
    BlockTimer() = default;
    BlockTimer(const BlockTimer&) = delete;
    BlockTimer& operator=(const BlockTimer&) = delete;
    ~BlockTimer() {
        const uint64_t t1 = p8_now_us();
        const uint64_t us = t1 > t0 ? t1 - t0 : 0;
        if (g_eng.block.add(t0, us)) {
            p::Event e;
            e.v[0] = g_eng.block.second_us();
            e.v[1] = us;
            e.v[2] = op;
            emit(kEBlock, e);
        }
    }
};

void guest_sched_load_before_deliver(std::span<const uint8_t> packet) noexcept {
    if (!g_started.load(std::memory_order_relaxed)) return;
    BlockTimer timer;
    if (packet.size() >= gsl::kHeader) timer.op = gsl::rd16(packet.data());
    // Session 12: the held handles that are due go back here (any host packet; at most once a second), on the
    // NetworkThread that took them, with 0x38AD90 (the module's own proven call), never while switched off.
    // Review 2997dbb: deliver_all only calls this for the CURRENT connection's packets, so the holds of an ended
    // connection (or of another manager / container) are seen here at the next connection's first packets and
    // DROPPED without any call (service_holds compares each hold's owner: epoch, manager, container, live).
    if (g_eng.holds.size()) {
        const uint64_t now = p8_now_us();
        if (g_disabled.load(std::memory_order_relaxed)) {
            const ManagerView mv = manager_view();
            GameEnv env{mv, 0};
            gsl::drop_holds(g_eng, env, now);  // no call into the game after a fault: the references are leaked
        } else if (now >= g_eng.next_service_us && g_code_ok.load(std::memory_order_acquire) && tls_realm() == 0) {
            const ManagerView mv = manager_view();
            GameEnv env{mv, 0};
            g_hold_services.fetch_add(1, std::memory_order_relaxed);
            gsl::service_holds(g_eng, env, now);
        }
    }
    if (packet.size() < gsl::kHeader) return;
    const uint16_t op = gsl::rd16(packet.data());
    const int slot = op == gsl::kOpActorLoad ? 0 : op == gsl::kOpScheduleCreate ? 1 : op == gsl::kOpInitSchedule ? 2 : -1;
    if (slot < 0) return;
    g_packets[slot].fetch_add(1, std::memory_order_relaxed);
    // The gates first (review 4): nothing touches the book, the budget or the retry set when they fail.
    const int realm = tls_realm();
    if (realm != 0) {
        g_realm_bad.fetch_add(1, std::memory_order_relaxed);
        p::Event e;
        e.v[0] = static_cast<uint64_t>(static_cast<int64_t>(realm));
        emit(kERealm, e);
    }
    const ManagerView mv = manager_view();
    gsl::Gates g;
    g.mode = static_cast<gsl::Mode>(g_mode.load(std::memory_order_relaxed));
    g.code_ok = g_code_ok.load(std::memory_order_acquire) && !g_disabled.load(std::memory_order_relaxed);
    g.client_realm = realm == 0;
    g.remote_live = guest_remote_live();
    g.manager_ok = mv.ok;
    const bool open = gsl::early_gate(g) == Why::Load;
    const gsl::PacketKeys pk =
        gsl::packet_keys(packet, g_keys.load(std::memory_order_acquire), open ? &g_eng.retry : nullptr);
    if (op == gsl::kOpActorLoad) {
        g_npc_records.fetch_add(pk.npc_records, std::memory_order_relaxed);
        g_scan_hits.fetch_add(pk.hits, std::memory_order_relaxed);
        if (pk.untiled) g_untiled.fetch_add(1, std::memory_order_relaxed);
    }
    g_spilled.fetch_add(pk.spilled, std::memory_order_relaxed);
    g_keys_named.fetch_add(pk.n, std::memory_order_relaxed);
    GameEnv env{mv, op};
    gsl::PacketBudget budget;
    // Session 13: every key the packet names first, with no budget but the packet's time cap (LATE ones noted),
    // then the retry set under its time budgets.
    gsl::process_packet(g_eng, env, g, pk, budget);
}

void guest_sched_load_tick() noexcept {
    if (!g_started.load()) return;
    try {
        ++g_tick_n;
        build_keys();
        state_line(g_tick_n % 12 == 0);
        p::Event e;
        while (g_ring.pop(e)) {
            if (e.kind >= kKindCount) continue;
            if (e.kind == kELatch) {  // review 3: always written (ERR), whatever the line budget
                log::error("GuestSchedLoad [LATCH] {} T{} | {} resource 0x{:08X} {} took {:.1f} ms{}: "
                           "guest_schedule_load is PROBE for the rest of this session (nothing more is loaded; what "
                           "is loaded stays). The NetworkThread was held that long",
                           p8_when(e.qpc_us), e.tid, e.v[4] ? "the load that made the slow ones too many:" : "one load of",
                           static_cast<uint32_t>(e.v[0]), name_of(static_cast<uint32_t>(e.v[0])),
                           static_cast<double>(e.v[3]) / 1000.0,
                           e.v[4] ? std::format(" (the {}th load over {} ms this session; {} make it latch)", e.v[4],
                                                gsl::kSlowLoadUs / 1000, gsl::kSlowLatchCount)
                                  : std::format(" (> {} ms)", gsl::kLatchUs / 1000));
                continue;
            }
            if (e.kind == kEBlock) {  // review M3: always written (WRN), 4 a minute at most (its limiter)
                log::warn("GuestSchedLoad [BLOCK] {} T{} | the client's NetworkThread has spent {:.1f} ms in this module "
                          "within one second (>= {} ms; the last delivery {:.1f} ms, before a host packet {}): its other work (the "
                          "guest's own packets to the host) waited that long",
                          p8_when(e.qpc_us), e.tid, static_cast<double>(e.v[0]) / 1000.0, gsl::kBlockWarnUs / 1000,
                          static_cast<double>(e.v[1]) / 1000.0, e.v[2]);
                continue;
            }
            if (e.kind == kELate) {  // session 13: outside kMaxLines (should be none), with its own cap
                if (g_late_lines.fetch_add(1) < kMaxLateLines) log::warn("{}", line_of(e));
                continue;
            }
            if (g_lines.fetch_add(1) < kMaxLines) log::info("{}", line_of(e));
        }
        if (g_tick_n % 12 == 0) summary();
    } catch (...) {
    }
}

std::string guest_sched_load_describe() {
    if (!g_started.load()) return {};
    return std::format(" | sched load {}{} (loaded {}, failed {}, would {}, lookup misses {}, held {}, 2335 misses {}, "
                       "released early {}, LATE {}{})",
                       g_mode.load() == 2 ? "on" : "probe", g_eng.latched.load() ? " LATCHED" : "",
                       g_eng.loads_ok.load(), g_eng.loads_failed.load(),
                       g_eng.why[static_cast<size_t>(Why::WouldLoad)].load(),
                       g_lk_missing[0].load() + g_lk_missing[1].load(), g_eng.held_now.load(), g_stage_missing.load(),
                       g_eng.early.load(), g_eng.late.load() + g_eng.late_spilled.load(),
                       g_disabled.load() ? ", OFF after a fault" : "");
}

}  // namespace cdmp::session2
