#include "mod/session2/companion_probe.h"

#include <windows.h>

#include <safetyhook.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <mutex>

#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

// Expected code, decoded on instruction boundaries with capstone over the
// read-only copy of build 2976 (scripts in %TEMP%\claude\compprobe). No direct
// branch lands inside the bytes a hook overwrites (13 sites): rel32 jmp/call
// and jcc32 scanned over every executable section (one raw hit, 0x263D7CD ->
// 0x206B3A0, is inside non-code bytes), short ones by linear disassembly of the
// containing function. Jump tables not checked.

// ---- 1. creation of a user-owned actor on the client
// CreateOwnedActor(mgr, out, u8 type, u32 key, u32 owner [rsp+0x28], u8 index
// [rsp+0x30], u64 [rsp+0x38], bool [rsp+0x40]) -> out; out+8 = actor, 0 = failed.
constexpr uintptr_t kCreate = 0x8AF580;
constexpr uint8_t kCreateHead[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56,
                                   0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C,
                                   0x24, 0xF9, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00, 0x41, 0x8B, 0xD9,
                                   0x45, 0x0F, 0xB6, 0xF8, 0x48, 0x8B, 0xF2, 0x4C, 0x8B, 0xE9};
// lea rdx,[rbp-0x21] ; mov rcx,r13 ; call 0x8ADFE0 (FindChecked(mgr, &att, owner))
// ; nop ; cmp byte [rbp-0x11],0 ; jne. At 0x8AF61B the attacher is filled:
// [rbp-0x19] = the owner user, [rbp-0x11] = its "valid" byte.
constexpr uintptr_t kFindSite = 0x8AF60E;
constexpr uint8_t kFindSiteBytes[] = {0x48, 0x8D, 0x55, 0xDF, 0x49, 0x8B, 0xCD, 0xE8, 0xC6, 0xE9,
                                      0xFF, 0xFF, 0x90, 0x80, 0x7D, 0xEF, 0x00, 0x75, 0x45};
constexpr uintptr_t kFindHook = kFindSite + 13;  // 0x8AF61B
// ... mov rdx,[rbp-0x19] ; call 0x4634C0 (the child factory) ; mov rcx,r12 ;
// mov [rbp-0x51],rcx. At 0x8AF696 RAX = the new actor (0 = the factory failed).
constexpr uintptr_t kFactorySite = 0x8AF685;
constexpr uint8_t kFactorySiteBytes[] = {0x44, 0x0F, 0xB6, 0x4D, 0x6F, 0x44, 0x8B, 0xC3, 0x48, 0x8B, 0x55, 0xE7,
                                         0xE8, 0x2A, 0x3E, 0xBB, 0xFF, 0x49, 0x8B, 0xCC, 0x48, 0x89, 0x4D, 0xAF};
constexpr uintptr_t kFactoryHook = kFactorySite + 17;  // 0x8AF696
// lea r8,[rbp-0x51] ; lea rdx,[rbp+0x57] ; mov rcx,[r13+8] ; call 0x463320 (map
// insert) ; mov edi,[rax] ; mov rax,[rbx]. At 0x8AF6D7 RAX = int* result.
constexpr uintptr_t kMapSite = 0x8AF6C6;
constexpr uint8_t kMapSiteBytes[] = {0x4C, 0x8D, 0x45, 0xAF, 0x48, 0x8D, 0x55, 0x57, 0x49, 0x8B, 0x4D,
                                     0x08, 0xE8, 0x49, 0x3C, 0xBB, 0xFF, 0x8B, 0x38, 0x48, 0x8B, 0x03};
constexpr uintptr_t kMapHook = kMapSite + 17;  // 0x8AF6D7
// ConfirmSlot(user, int* res, child, u8 slot) -> res: r12 = user, r13 = res,
// r15 = child, esi = slot; rdi = [[user+0x68]+0x18] (its child container).
constexpr uintptr_t kConfirm = 0x206B2D0;
constexpr uint8_t kConfirmHead[] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x44, 0x88, 0x4C, 0x24, 0x20, 0x48, 0x89,
                                    0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
                                    0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,
                                    0x41, 0x0F, 0xB6, 0xF1, 0x4D, 0x8B, 0xF8, 0x4C, 0x8B, 0xEA, 0x4C, 0x8B,
                                    0xE1, 0x48, 0x8B, 0x41, 0x68, 0x48, 0x8B, 0x78, 0x18};
// mov eax,[global] ; test eax,eax ; je ; mov [r13],eax ; mov rdi,[rbp-0x40].
// 0x206B39E runs only with EAX != 0: the slot error (vf+0x140's, or the global
// when the user is not in state 4 / +0x4A).
constexpr uintptr_t kSlotErrSite = 0x206B394;
constexpr uint8_t kSlotErrSiteBytes[] = {0x8B, 0x05, 0x52, 0x06, 0xC7, 0x04, 0x85, 0xC0, 0x74,
                                         0x55, 0x41, 0x89, 0x45, 0x00, 0x48, 0x8B, 0x7D, 0xC0};
constexpr uintptr_t kSlotErrHook = kSlotErrSite + 10;  // 0x206B39E
// mov edx,4 ; mov r8d,0x10 ; mov rax,[rdi] ; mov rcx,rdi ; call [rax+0xC0] ;
// test al,al ; je ; mov al,1. RDI = the main ([user+0xD0]), AL = acquired.
constexpr uintptr_t kMainAcqSite = 0x206B49E;
constexpr uint8_t kMainAcqSiteBytes[] = {0xBA, 0x04, 0x00, 0x00, 0x00, 0x41, 0xB8, 0x10, 0x00, 0x00,
                                         0x00, 0x48, 0x8B, 0x07, 0x48, 0x8B, 0xCF, 0xFF, 0x90, 0xC0,
                                         0x00, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x04, 0xB0, 0x01};
constexpr uintptr_t kMainAcqHook = kMainAcqSite + 23;  // 0x206B4B5
// mov rax,[rdi+0x68] ; mov r14,[rax+0x18] (the main's container), then
// call ; nop ; mov rax,[r14+8] ; test byte [rax+0x5E],0x10.
constexpr uintptr_t kMainCompSite = 0x206B4FD;
constexpr uint8_t kMainCompSiteBytes[] = {0x48, 0x8B, 0x47, 0x68, 0x4C, 0x8B, 0x70, 0x18};
constexpr uintptr_t kMainStateSite = 0x206B52F;
constexpr uint8_t kMainStateSiteBytes[] = {0xE8, 0x1C, 0x81, 0x32, 0xFE, 0x90, 0x49,
                                           0x8B, 0x46, 0x08, 0xF6, 0x40, 0x5E, 0x10};
constexpr uintptr_t kMainStateHook = kMainStateSite + 6;  // 0x206B535
// lea rcx,[rdi+0x18] ; mov rax,[rcx] ; call [rax+0x10] ; test r13d,r13d ; je.
// R13D = the attach result: 0 attached (0x206B51B), else vf+0x148's or the
// global (main not in state 0x10 / +0x4A).
constexpr uintptr_t kAttachSite = 0x206B5D0;
constexpr uint8_t kAttachSiteBytes[] = {0x48, 0x8D, 0x4F, 0x18, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x10,
                                        0x45, 0x85, 0xED, 0x0F, 0x84, 0xCE, 0x00, 0x00, 0x00};
constexpr uintptr_t kAttachHook = kAttachSite + 10;  // 0x206B5DA

// ---- 2. restore and the crash
// RestoreActionsFromLoadData 0x8646D0: its points and bytes are in the header
// (cmp::kRestore*), shared with the P8 guest's diagnostic (guest_actor_diag.cpp).
using cmp::kRestoreExit;
using cmp::kRestoreExitBytes;
using cmp::kRestoreExitHook;
using cmp::kRestoreHook;
using cmp::kRestoreLock;
using cmp::kRestoreLockBytes;
using cmp::kRestoreSite;
using cmp::kRestoreSiteBytes;
// 0x232D5C0(CC, u8 layer, r8 = copy of the layer's +0x00..+0x18, r9 = copy of
// +0x20..+0x38, ...): its only caller 0x232D2A0 (rdi = CC) loads the layer at
// 0x232D51E, copies it at 0x232D543 and calls at 0x232D5AD. 0x232D5FB reads
// [r8+0x10] and 0x232D5FF dereferences it: the crash of C2-0.
constexpr uintptr_t kCrash = 0x232D5C0;
constexpr uint8_t kCrashHead[] = {0x4C, 0x89, 0x4C, 0x24, 0x20, 0x4C, 0x89, 0x44, 0x24, 0x18,
                                  0x88, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4C, 0x24, 0x08};
constexpr uintptr_t kCrashSite = 0x232D5EE;
constexpr uint8_t kCrashSiteBytes[] = {0x49, 0x8B, 0xF1, 0x4D, 0x8B, 0xF8, 0x44, 0x0F, 0xB6, 0xEA, 0x48, 0x8B,
                                       0xF9, 0x49, 0x8B, 0x40, 0x10, 0x0F, 0xB7, 0x80, 0xE4, 0x00, 0x00, 0x00};
constexpr uintptr_t kCallerLayer = 0x232D51E;
constexpr uint8_t kCallerLayerBytes[] = {0x48, 0x8B, 0x8C, 0xDF, 0x88, 0x00, 0x00, 0x00};
constexpr uintptr_t kCallerCopy = 0x232D543;
constexpr uint8_t kCallerCopyBytes[] = {0xC5, 0xF8, 0x10, 0x01, 0xC5, 0xF8, 0x11, 0x44, 0x24, 0x48, 0xC5,
                                        0xFB, 0x10, 0x49, 0x10, 0xC5, 0xFB, 0x11, 0x4C, 0x24, 0x58};
constexpr uintptr_t kCallerCall = 0x232D59D;
constexpr uint8_t kCallerCallBytes[] = {0x4C, 0x8D, 0x4C, 0x24, 0x30, 0x4C, 0x8D, 0x44, 0x24, 0x48, 0x0F,
                                        0xB6, 0xD3, 0x48, 0x8B, 0xCF, 0xE8, 0x0E, 0x00, 0x00, 0x00};

// ---- 3. ScopeSenderAck 2042 (companeros.md, Revisión 2)
constexpr uintptr_t kScopeAck = 0xCB4160;
constexpr uint8_t kScopeAckHead[] = {0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55,
                                     0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};

// ---- 4. The crash after user 2's logout with its held child (diary, D-52, 12:01:50):
// 0xAAFC00 (generic, ~180 callers) reads [this+0x210] at 0xAAFC1D (rdi = this)
// and faulted on an unreadable this. Read-only: name the callers, then let it go.
constexpr uintptr_t kBadThisFn = 0xAAFC00;
constexpr uint8_t kBadThisFnHead[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89,
                                      0x7C, 0x24, 0x18, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9,
                                      0xE8, 0x33, 0xA2, 0xFF, 0xFF};
constexpr uintptr_t kBadThis = 0xAAFC1D;  // mov rbx, [rdi+0x210]
constexpr uint8_t kBadThisBytes[] = {0x48, 0x8B, 0x9F, 0x10, 0x02, 0x00, 0x00};

enum Point : int {
    kPtCreate, kPtFind, kPtFactory, kPtMap, kPtConfirm, kPtSlotErr, kPtMainAcq, kPtMainState, kPtAttach,
    kPtRestore, kPtRestoreExit, kPtCrash, kPtScopeAck, kPtBadThis, kPoints
};
constexpr const char* kPointName[kPoints] = {
    "create 0x8AF580",     "find 0x8AF61B",       "factory 0x8AF696",  "map 0x8AF6D7",
    "confirm 0x206B2D0",   "slot error 0x206B39E", "main acq 0x206B4B5", "main state 0x206B535",
    "attach 0x206B5DA",    "restore 0x8646FC",    "restore exit 0x864B1D", "crash 0x232D5C0",
    "2042 0xCB4160",       "bad this 0xAAFC00"};
// Lines per point (the mid hooks of creation only fill the CompCreate line).
constexpr uint64_t kPointCap[kPoints] = {80, 0, 0, 0, 12, 0, 0, 0, 0, 40, 40, 24, 4, 4};
constexpr uint64_t kMaxHostLines = 6;  // the host's own actors, for comparison (per point)
constexpr uint64_t kMaxLogged = 200;

struct PointStats {
    std::atomic<bool> on{false};
    std::atomic<uint64_t> calls{0}, user2{0}, tries{0}, host_tries{0}, lines{0};
};

uintptr_t g_base = 0, g_image_end = 0;
SafetyHookInline g_create, g_confirm;
SafetyHookMid g_find, g_factory, g_map, g_slot_err, g_main_acq, g_main_state, g_attach, g_restore,
    g_restore_exit, g_crash, g_scope_ack, g_bad_this;
std::atomic<bool> g_active{false};
PointStats g_pt[kPoints];
std::atomic<uint64_t> g_logged{0}, g_creates{0}, g_created{0}, g_rejected{0}, g_crash_nulls{0};
std::atomic<uint32_t> g_host_key{0}, g_user2_key{0};
std::atomic<uint64_t> g_keys_at{0};

template <class T>
bool peek(uintptr_t a, T& v) {
    return a && game::safe_read(a, &v, sizeof v);
}

uintptr_t peekp(uintptr_t a) {
    uintptr_t v = 0;
    return peek(a, v) ? v : 0;
}

std::string rva(uintptr_t a) {
    if (a >= g_base && a < g_image_end) return std::format("exe+0x{:X}", a - g_base);
    return std::format("0x{:X}", a);
}

// Limited logging; callers are inside try blocks. True = written.
template <class... Args>
bool note(Point p, bool host, std::format_string<Args...> fmt, Args&&... args) {
    PointStats& s = g_pt[p];
    if (host && s.host_tries.fetch_add(1) >= kMaxHostLines) return false;
    if (s.tries.fetch_add(1) >= kPointCap[p]) return false;
    if (g_logged.fetch_add(1) >= kMaxLogged) return false;
    s.lines.fetch_add(1);
    log::info("Session2 Comp probe: {}{}", host ? "[host, comparison] " : "",
              std::format(fmt, std::forward<Args>(args)...));
    return true;
}

// The host's and user 2's user keys, from the server (our own reads, no game
// call), at most every 500 ms. The last non-zero value is kept.
void refresh_keys() {
    const uint64_t now = GetTickCount64();
    if (now - g_keys_at.load(std::memory_order_relaxed) < 500) return;
    g_keys_at.store(now, std::memory_order_relaxed);
    uintptr_t u = 0;
    uint32_t key = 0;
    if (server_host_user(u) && peek(u + cmp::kActorKey, key) && key) g_host_key.store(key);
    key = 0;
    if (server_user2(u) && peek(u + cmp::kActorKey, key) && key) g_user2_key.store(key);
}

CompWho who_of(uint32_t owner) {
    refresh_keys();
    return comp_who(owner, g_host_key.load(), g_user2_key.load());
}

// What CompCreate learnt about an owned actor's key (type, slot, record u64),
// so the restore/crash lines can say which child it is.
struct Known {
    uint32_t key = 0, owner = 0;
    uint8_t type = 0, slot = 0;
    uint64_t data = 0;
};
constexpr int kKnown = 48;
Known g_known[kKnown];
int g_known_next = 0;
std::mutex g_known_mutex;

void remember(const Known& k) {
    std::lock_guard lock(g_known_mutex);
    for (Known& e : g_known) {
        if (e.key == k.key) {
            e = k;
            return;
        }
    }
    g_known[g_known_next] = k;
    g_known_next = (g_known_next + 1) % kKnown;
}

bool recall(uint32_t key, Known& out) {
    if (!key) return false;
    std::lock_guard lock(g_known_mutex);
    for (const Known& e : g_known) {
        if (e.key == key) {
            out = e;
            return true;
        }
    }
    return false;
}

// Crash lines: once per actor key.
constexpr int kCrashKeys = 32;
uint32_t g_crash_keys[kCrashKeys] = {};
int g_crash_key_count = 0;
std::mutex g_crash_mutex;

bool first_crash_of(uint32_t key) {
    std::lock_guard lock(g_crash_mutex);
    for (int i = 0; i < g_crash_key_count; ++i) {
        if (g_crash_keys[i] == key) return false;
    }
    if (g_crash_key_count < kCrashKeys) g_crash_keys[g_crash_key_count++] = key;
    return true;
}

std::string info_text(bool ok, const uint8_t (&b)[8]) {
    if (!ok) return "unreadable";
    return std::format("{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}", b[0], b[1], b[2], b[3], b[4], b[5],
                       b[6], b[7]);
}

// A client actor in a few words (key, class, owner, vtable, static info's first
// bytes), read while it is alive and kept as values.
struct ActorSnap {
    uintptr_t p = 0, vt = 0;
    uint32_t key = 0, owner = 0;
    bool key_ok = false, owner_ok = false, info_ok = false;
    uint8_t info[8] = {};
};

ActorSnap read_actor(uintptr_t actor) {
    ActorSnap a;
    a.p = actor;
    if (!actor) return a;
    a.key_ok = peek(actor + cmp::kActorKey, a.key);
    a.owner_ok = peek(actor + cmp::kActorOwnerUser, a.owner);
    a.vt = peekp(actor);
    const uintptr_t info = peekp(actor + cmp::kActorInfo);
    a.info_ok = info && game::safe_read(info, a.info, sizeof a.info);
    return a;
}

std::string actor_text(const ActorSnap& a) {
    if (!a.p) return "none";
    return std::format("0x{:X} key {} ({}) owner {} vt {} info {}", a.p,
                       a.key_ok ? std::format("0x{:08X}", a.key) : std::string("?"), comp_key_class_name(a.key),
                       a.owner_ok ? std::format("0x{:08X}", a.owner) : std::string("?"), rva(a.vt),
                       info_text(a.info_ok, a.info));
}

std::string actor_text(uintptr_t actor) { return actor_text(read_actor(actor)); }

// An actor's +0x5E/+0x4A, kept as values: the per-thread frames hold no strings.
struct StateSnap {
    bool ok = false;
    uint16_t st = 0;
    uint8_t ready = 0;
};

StateSnap read_state(uintptr_t actor) {
    StateSnap s;
    s.ok = actor && peek(actor + cmp::kActorState, s.st) && peek(actor + cmp::kActorReady, s.ready);
    return s;
}

std::string state_text(const StateSnap& s) {
    if (!s.ok) return "state unreadable";
    return std::format("+5E 0x{:04X} (4:{} 0x10:{}) +4A {}", s.st, (s.st & 4) ? 1 : 0, (s.st & 0x10) ? 1 : 0,
                       unsigned{s.ready});
}

std::string layer_text(const CompLayer& l) {
    if (!l.present) return "none";
    return std::format("cur {} play {} chart {}{}", l.current ? 1 : 0, l.playback ? 1 : 0, l.chart ? 1 : 0,
                       l.would_crash() ? " (CHART, NO ACTION)" : "");
}

std::string known_text(uint32_t key) {
    Known k;
    if (!recall(key, k)) return "record unknown";
    return std::format("record type {} slot {} u64 0x{:X}", unsigned{k.type}, unsigned{k.slot}, k.data);
}

std::string control_text(const CompControl& c) {
    return std::format("restore byte {} pending {} | L0 {} | L1 {} | CC vt {}",
                       c.restore < 0 ? std::string("?") : std::to_string(c.restore),
                       c.pending < 0 ? std::string("?") : std::to_string(c.pending), layer_text(c.layer[0]),
                       layer_text(c.layer[1]), rva(c.cc_vt));
}

// ---------------------------------------------------------------- creation frames

struct ConfirmFrame {
    uintptr_t user = 0;
    ActorSnap child, main_entry;  // read at entry
    uint8_t slot = 0;
    bool slot_err_seen = false, acq_seen = false, main_state_seen = false, attach_seen = false;
    uint32_t slot_err = 0, attach = 0;
    uint8_t acq = 0;
    uintptr_t acq_main = 0, state_main = 0;
    StateSnap user_state, main_entry_state, main_state;
    bool result_ok = false;
    uint32_t result = 0;
};

struct CreateFrame {
    uint64_t id = 0;
    uint32_t key = 0, owner = 0;
    uint8_t type = 0, slot = 0;
    uint64_t data = 0;
    bool flag = false;
    CompWho who = CompWho::None;
    bool find_seen = false, factory_seen = false, map_seen = false;
    uintptr_t find_user = 0;
    ActorSnap child;  // read at 0x8AF696: the factory's object is deleted if the map insert fails
    uint8_t find_ok = 0;
    uint32_t map = 0;
    bool map_ok = false;
    int confirms = 0;
    ConfirmFrame confirm;  // the last ConfirmSlot inside this creation
};
constexpr int kMaxDepth = 4;
thread_local CreateFrame t_create[kMaxDepth];
thread_local int t_create_depth = 0;
thread_local ConfirmFrame t_confirm[kMaxDepth];
thread_local int t_confirm_depth = 0;
thread_local uintptr_t t_restore_cc = 0;  // the CC whose "before" line was written on this thread

CreateFrame* create_frame() {
    return t_create_depth > 0 && t_create_depth <= kMaxDepth ? &t_create[t_create_depth - 1] : nullptr;
}
ConfirmFrame* confirm_frame() {
    return t_confirm_depth > 0 && t_confirm_depth <= kMaxDepth ? &t_confirm[t_confirm_depth - 1] : nullptr;
}

std::string confirm_text(const ConfirmFrame& c) {
    std::string s = std::format("user 0x{:X} {} | main at entry {}{} | slot arg {}", c.user, state_text(c.user_state),
                                actor_text(c.main_entry),
                                c.main_entry.p ? " " + state_text(c.main_entry_state) : std::string(),
                                unsigned{c.slot});
    s += c.slot_err_seen ? std::format(" | SLOT ERROR 0x{:X}", c.slot_err) : std::string(" | slot error none");
    if (c.acq_seen) {
        s += std::format(" | main acquired {} (main 0x{:X})", unsigned{c.acq}, c.acq_main);
    } else {
        s += " | main acquisition not reached";
    }
    if (c.main_state_seen) {
        s += std::format(" | at the attach test: main 0x{:X} {}", c.state_main, state_text(c.main_state));
    }
    s += c.attach_seen ? std::format(" | attach 0x{:X}", c.attach) : std::string(" | attach not reached");
    s += c.result_ok ? std::format(" | result 0x{:X}", c.result) : std::string(" | result unreadable");
    return s;
}

// ---------------------------------------------------------------- entry hooks

// All args as full registers / stack qwords: the original gets exactly what
// its caller passed (the u8/u32 ones included, upper bits and all).
void* __fastcall create_detour(void* mgr, void* out, uint64_t type, uint64_t key, uint64_t owner, uint64_t index,
                               uint64_t data, uint64_t flag) noexcept {
    // Unowned actors (NPCs, objects): not ours, straight through.
    if (static_cast<uint32_t>(owner) == 0) {
        return g_create.unsafe_call<void*>(mgr, out, type, key, owner, index, data, flag);
    }
    const int depth = t_create_depth++;
    CreateFrame* f = depth < kMaxDepth ? &t_create[depth] : nullptr;
    try {
        g_pt[kPtCreate].calls.fetch_add(1);
        if (f) {
            *f = CreateFrame{};
            f->id = g_creates.fetch_add(1) + 1;
            f->key = static_cast<uint32_t>(key);
            f->owner = static_cast<uint32_t>(owner);
            f->type = static_cast<uint8_t>(type);
            f->slot = static_cast<uint8_t>(index);
            f->data = data;
            f->flag = static_cast<uint8_t>(flag) != 0;
            f->who = who_of(f->owner);
            if (f->who == CompWho::User2) g_pt[kPtCreate].user2.fetch_add(1);
        }
    } catch (...) {
    }
    void* r = g_create.unsafe_call<void*>(mgr, out, type, key, owner, index, data, flag);
    if (f) {
        try {
            const uintptr_t o = reinterpret_cast<uintptr_t>(r);
            const uintptr_t actor = peekp(o + 8);
            (actor ? g_created : g_rejected).fetch_add(1);
            if (f->who != CompWho::Host) remember({f->key, f->owner, f->type, f->slot, f->data});
            const std::string find = f->find_seen ? std::format("user 0x{:X} ok {}", f->find_user, unsigned{f->find_ok})
                                                  : std::string("not reached");
            const std::string factory = f->factory_seen ? actor_text(f->child) : std::string("not reached");
            const std::string map = !f->map_seen ? std::string("not reached")
                                    : f->map_ok  ? std::format("0x{:X}", f->map)
                                                 : std::string("unreadable");
            const std::string confirm = f->confirms ? confirm_text(f->confirm) : std::string("not called");
            note(kPtCreate, f->who == CompWho::Host,
                 "CompCreate #{} owner 0x{:08X} ({}) child 0x{:08X} ({}) type {} slot {} u64 0x{:X} flag {} | "
                 "find: {} | factory: {} | confirm x{}: {} | map {} | out actor 0x{:X} -> {}",
                 f->id, f->owner, comp_who_name(f->who), f->key, comp_key_class_name(f->key), unsigned{f->type},
                 unsigned{f->slot}, f->data, f->flag ? 1 : 0, find, factory, f->confirms, confirm, map, actor,
                 actor ? std::string("CREATED")
                       : std::format("REJECTED (the game error \"Actor ... failed\" from exe+0x8BDF7C should name "
                                     "0x{:08X})",
                                     f->key));
        } catch (...) {
        }
    }
    --t_create_depth;
    return r;
}

int* __fastcall confirm_detour(void* user, int* res, void* child, uint64_t slot) noexcept {
    const int depth = t_confirm_depth++;
    ConfirmFrame* c = depth < kMaxDepth ? &t_confirm[depth] : nullptr;
    try {
        g_pt[kPtConfirm].calls.fetch_add(1);
        if (c) {
            *c = ConfirmFrame{};
            c->user = reinterpret_cast<uintptr_t>(user);
            c->child = read_actor(reinterpret_cast<uintptr_t>(child));
            c->slot = static_cast<uint8_t>(slot);
            c->user_state = read_state(c->user);
            c->main_entry = read_actor(peekp(c->user + cmp::kUserMain));
            c->main_entry_state = read_state(c->main_entry.p);
        }
    } catch (...) {
    }
    int* r = g_confirm.unsafe_call<int*>(user, res, child, slot);
    if (c) {
        try {
            uint32_t code = 0;
            c->result_ok = peek(reinterpret_cast<uintptr_t>(r), code);
            c->result = code;
            if (CreateFrame* f = create_frame()) {
                f->confirm = *c;
                ++f->confirms;
            } else {
                // Called from 0x2A7DBC2, outside a creation: only for users that are not the host's.
                uint32_t ukey = 0;
                peek(c->user + cmp::kActorKey, ukey);
                const CompWho w = who_of(ukey);
                if (w == CompWho::User2) g_pt[kPtConfirm].user2.fetch_add(1);
                if (w != CompWho::Host) {
                    note(kPtConfirm, false, "CompConfirm (outside 0x8AF580) user key 0x{:08X} ({}) child {} | {}", ukey,
                         comp_who_name(w), actor_text(c->child), confirm_text(*c));
                }
            }
        } catch (...) {
        }
    }
    --t_confirm_depth;
    return r;
}

// ---------------------------------------------------------------- mid hooks (read only)

void on_find(safetyhook::Context& ctx) noexcept {
    try {
        g_pt[kPtFind].calls.fetch_add(1);
        CreateFrame* f = create_frame();
        if (!f) return;
        f->find_seen = true;
        f->find_user = peekp(ctx.rbp - 0x19);
        uint8_t ok = 0;
        peek(ctx.rbp - 0x11, ok);
        f->find_ok = ok;
    } catch (...) {
    }
}

void on_factory(safetyhook::Context& ctx) noexcept {
    g_pt[kPtFactory].calls.fetch_add(1);
    if (CreateFrame* f = create_frame()) {
        f->factory_seen = true;
        try {
            f->child = read_actor(ctx.rax);
        } catch (...) {
        }
    }
}

void on_map(safetyhook::Context& ctx) noexcept {
    g_pt[kPtMap].calls.fetch_add(1);
    if (CreateFrame* f = create_frame()) {
        f->map_seen = true;
        uint32_t v = 0;
        f->map_ok = peek(ctx.rax, v);
        f->map = v;
    }
}

void on_slot_err(safetyhook::Context& ctx) noexcept {
    g_pt[kPtSlotErr].calls.fetch_add(1);
    if (ConfirmFrame* c = confirm_frame()) {
        c->slot_err_seen = true;
        c->slot_err = static_cast<uint32_t>(ctx.rax);
    }
}

void on_main_acq(safetyhook::Context& ctx) noexcept {
    g_pt[kPtMainAcq].calls.fetch_add(1);
    if (ConfirmFrame* c = confirm_frame()) {
        c->acq_seen = true;
        c->acq = static_cast<uint8_t>(ctx.rax);
        c->acq_main = ctx.rdi;
    }
}

void on_main_state(safetyhook::Context& ctx) noexcept {
    g_pt[kPtMainState].calls.fetch_add(1);
    if (ConfirmFrame* c = confirm_frame()) {
        c->main_state_seen = true;
        c->state_main = peekp(ctx.r14 + 8);
        c->main_state = read_state(c->state_main);
    }
}

void on_attach(safetyhook::Context& ctx) noexcept {
    g_pt[kPtAttach].calls.fetch_add(1);
    if (ConfirmFrame* c = confirm_frame()) {
        c->attach_seen = true;
        c->attach = static_cast<uint32_t>(ctx.r13);
    }
}

// Is this control component's actor a remote option-D one (or the host's)?
struct Interest {
    bool remote = false, host = false;
    CompWho who = CompWho::None;
};

Interest interest(const CompControl& c) {
    Interest i;
    Known k;
    i.who = who_of(c.owner);
    i.remote = i.who == CompWho::User2 || i.who == CompWho::OtherUser ||
               (recall(c.key, k) && k.owner != g_host_key.load());
    i.host = !i.remote && i.who == CompWho::Host;
    return i;
}

void on_restore(safetyhook::Context& ctx) noexcept {
    try {
        g_pt[kPtRestore].calls.fetch_add(1);
        t_restore_cc = 0;
        CompControl c;
        if (!comp_read_control(ctx.rcx, c)) return;
        const Interest i = interest(c);
        if (!i.remote && !i.host) return;
        if (i.who == CompWho::User2) g_pt[kPtRestore].user2.fetch_add(1);
        if (note(kPtRestore, i.host, "CompAction restore BEFORE actor {} ({}) {} | {}", actor_text(c.actor),
                 comp_who_name(i.who), known_text(c.key), control_text(c))) {
            t_restore_cc = ctx.rcx;
        }
    } catch (...) {
    }
}

void on_restore_exit(safetyhook::Context& ctx) noexcept {
    try {
        g_pt[kPtRestoreExit].calls.fetch_add(1);
        if (!t_restore_cc || ctx.r13 != t_restore_cc) return;
        t_restore_cc = 0;
        CompControl c;
        if (!comp_read_control(ctx.r13, c)) return;
        const Interest i = interest(c);
        uint32_t code = 0;
        const bool code_ok = peek(ctx.rsi, code);
        note(kPtRestoreExit, i.host, "CompAction restore AFTER actor 0x{:08X} | result {} | {}", c.key,
             code_ok ? std::format("0x{:X}", code) : std::string("unreadable"), control_text(c));
    } catch (...) {
    }
}

// The hot path: one read of a stack slot of the caller.
bool null_current(uintptr_t r8) {
    __try {
        return *reinterpret_cast<const uintptr_t*>(r8 + cmp::kLayerCurrent) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void on_crash(safetyhook::Context& ctx) noexcept {
    g_pt[kPtCrash].calls.fetch_add(1, std::memory_order_relaxed);
    if (!null_current(ctx.r8)) return;
    try {
        g_crash_nulls.fetch_add(1);
        CompControl c;
        const bool ok = comp_read_control(ctx.rcx, c);
        if (!first_crash_of(ok ? c.key : 0)) return;
        const Interest i = ok ? interest(c) : Interest{};
        if (i.who == CompWho::User2) g_pt[kPtCrash].user2.fetch_add(1);
        const uintptr_t ret = peekp(ctx.rsp);
        const uintptr_t r8_0 = peekp(ctx.r8), r8_8 = peekp(ctx.r8 + 8);
        note(kPtCrash, false,
             "CompCrash 0x232D5C0 about to read [[r8+0x10]+0xE4] with [r8+0x10] = 0 (exe+0x232D5FF): CC 0x{:X} "
             "layer {} (copy +0 {} +8 {}) caller {} | actor {} ({}) {} | {}",
             ctx.rcx, static_cast<unsigned>(ctx.rdx & 0xFF), r8_0 ? 1 : 0, r8_8 ? 1 : 0, rva(ret),
             ok ? actor_text(c.actor) : std::string("unreadable"), comp_who_name(i.who), known_text(c.key),
             ok ? control_text(c) : std::string("control unreadable"));
    } catch (...) {
    }
}

// A plain guarded read: 0xAAFC00 is hot, no VirtualQuery here (safe_read).
bool readable_qword(uintptr_t a, uint64_t& v) {
    __try {
        v = *reinterpret_cast<const volatile uint64_t*>(a);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void on_bad_this(safetyhook::Context& ctx) noexcept {
    uint64_t v = 0;
    if (readable_qword(ctx.rdi + 0x210, v)) return;
    try {
        g_pt[kPtBadThis].calls.fetch_add(1);
        // push r14 + sub rsp,0x20: the return address is at rsp+0x28. Then the
        // stack's values that point into the image: candidate callers above it.
        uint64_t ret = 0;
        readable_qword(ctx.rsp + 0x28, ret);
        std::string chain;
        int found = 0;
        for (uintptr_t off = 0x30; off < 0x600 && found < 14; off += 8) {
            uint64_t q = 0;
            if (readable_qword(ctx.rsp + off, q) && q >= g_base + 0x1000 && q < g_image_end) {
                chain += std::format(" {}@+0x{:X}", rva(q), off);
                ++found;
            }
        }
        note(kPtBadThis, false,
             "CompBadThis 0xAAFC00 is about to read [this+0x210] with an unreadable this 0x{:X} (exe+0xAAFC1D) | "
             "caller {} | stack values in the image:{}",
             ctx.rdi, rva(ret), chain.empty() ? std::string(" none") : chain);
    } catch (...) {
    }
}

void on_scope_ack(safetyhook::Context& ctx) noexcept {
    try {
        g_pt[kPtScopeAck].calls.fetch_add(1);
        note(kPtScopeAck, false, "ScopeSenderAck 2042 entry 0xCB4160 (rcx 0x{:X}) caller {}", ctx.rcx,
             rva(peekp(ctx.rsp)));
    } catch (...) {
    }
}

// ---------------------------------------------------------------- install

bool code_ok(uintptr_t at, const uint8_t* bytes, size_t n, Point p) {
    uint8_t actual[64] = {};
    if (n <= sizeof actual && game::safe_read(g_base + at, actual, n) && std::memcmp(actual, bytes, n) == 0) {
        return true;
    }
    log::warn("Session2 Comp probe: {} (exe+0x{:X}) is not the expected code; that point stays off", kPointName[p], at);
    return false;
}

template <class Fn>
bool hook_entry(Point p, uintptr_t at, const uint8_t* head, size_t n, SafetyHookInline& h, Fn detour) {
    if (!code_ok(at, head, n, p)) return false;
    h = safetyhook::create_inline(reinterpret_cast<void*>(g_base + at), reinterpret_cast<void*>(detour),
                                  SafetyHookInline::StartDisabled);
    if (!h || !h.enable()) {
        h = {};
        log::warn("Session2 Comp probe: cannot hook {}; that point stays off", kPointName[p]);
        return false;
    }
    g_pt[p].on.store(true);
    return true;
}

bool hook_mid(Point p, uintptr_t at, SafetyHookMid& h, safetyhook::MidHookFn fn) {
    h = safetyhook::create_mid(reinterpret_cast<void*>(g_base + at), fn);
    if (!h) {
        log::warn("Session2 Comp probe: cannot hook {}; that point stays off", kPointName[p]);
        return false;
    }
    g_pt[p].on.store(true);
    return true;
}

template <size_t N>
bool site_ok(uintptr_t at, const uint8_t (&b)[N], Point p) {
    return code_ok(at, b, N, p);
}

}  // namespace

KeyClass comp_key_class(uint32_t key) { return static_cast<KeyClass>((key >> 28) & 3); }

const char* comp_key_class_name(uint32_t key) {
    if (!key) return "no key";
    switch (comp_key_class(key)) {
    case KeyClass::User:
        return "user";
    case KeyClass::Character:
        return "player character";
    case KeyClass::Actor:
        return "actor";
    default:
        return "other";
    }
}

CompWho comp_who(uint32_t owner, uint32_t host_key, uint32_t user2_key) {
    if (!owner) return CompWho::None;
    if (user2_key && owner == user2_key) return CompWho::User2;
    if (host_key && owner == host_key) return CompWho::Host;
    return CompWho::OtherUser;
}

const char* comp_who_name(CompWho w) {
    switch (w) {
    case CompWho::Host:
        return "host";
    case CompWho::User2:
        return "user 2";
    case CompWho::OtherUser:
        return "another user (not the host)";
    default:
        return "no owner";
    }
}

bool comp_read_control(uintptr_t cc, CompControl& out) {
    out = CompControl{};
    if (!cc || !peek(cc, out.cc_vt) || !peek(cc + cmp::kCcActor, out.actor) || !out.actor) return false;
    if (!peek(out.actor, out.actor_vt)) return false;
    peek(out.actor + cmp::kActorKey, out.key);
    peek(out.actor + cmp::kActorOwnerUser, out.owner);
    out.info = peekp(out.actor + cmp::kActorInfo);
    out.info_ok = out.info && game::safe_read(out.info, out.info_bytes, sizeof out.info_bytes);
    uint8_t b = 0;
    const uintptr_t st = peekp(cc + cmp::kCcRestore);
    if (st && peek(st + cmp::kRestoreByte, b)) out.restore = b;
    if (peek(cc + cmp::kCcPending, b)) out.pending = b;
    for (int i = 0; i < 2; ++i) {
        const uintptr_t l = peekp(cc + cmp::kCcLayer0 + 8 * static_cast<uint32_t>(i));
        CompLayer& L = out.layer[i];
        if (!l) continue;
        L.present = true;
        L.current = peekp(l + cmp::kLayerCurrent);
        L.playback = peekp(l + cmp::kLayerPlayback);
        L.chart = peekp(l + cmp::kLayerChart);
    }
    return true;
}

bool companion_probe_install() {
    if (g_active.load()) return true;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(g_base + dos->e_lfanew);
    g_image_end = g_base + nt->OptionalHeader.SizeOfImage;
    refresh_keys();
    // Creation: the mid hooks only fill the frame the entry hook opens.
    if (hook_entry(kPtCreate, kCreate, kCreateHead, sizeof kCreateHead, g_create, &create_detour)) {
        if (site_ok(kFindSite, kFindSiteBytes, kPtFind)) hook_mid(kPtFind, kFindHook, g_find, on_find);
        if (site_ok(kFactorySite, kFactorySiteBytes, kPtFactory)) {
            hook_mid(kPtFactory, kFactoryHook, g_factory, on_factory);
        }
        if (site_ok(kMapSite, kMapSiteBytes, kPtMap)) hook_mid(kPtMap, kMapHook, g_map, on_map);
    }
    if (hook_entry(kPtConfirm, kConfirm, kConfirmHead, sizeof kConfirmHead, g_confirm, &confirm_detour)) {
        if (site_ok(kSlotErrSite, kSlotErrSiteBytes, kPtSlotErr)) {
            hook_mid(kPtSlotErr, kSlotErrHook, g_slot_err, on_slot_err);
        }
        if (site_ok(kMainAcqSite, kMainAcqSiteBytes, kPtMainAcq)) {
            hook_mid(kPtMainAcq, kMainAcqHook, g_main_acq, on_main_acq);
        }
        if (site_ok(kMainCompSite, kMainCompSiteBytes, kPtMainState) &&
            site_ok(kMainStateSite, kMainStateSiteBytes, kPtMainState)) {
            hook_mid(kPtMainState, kMainStateHook, g_main_state, on_main_state);
        }
        if (site_ok(kAttachSite, kAttachSiteBytes, kPtAttach)) hook_mid(kPtAttach, kAttachHook, g_attach, on_attach);
    }
    // Restore: "after" only with "before" (it pairs with it).
    if (site_ok(kRestoreSite, kRestoreSiteBytes, kPtRestore) && site_ok(kRestoreLock, kRestoreLockBytes, kPtRestore) &&
        hook_mid(kPtRestore, kRestoreHook, g_restore, on_restore) &&
        site_ok(kRestoreExit, kRestoreExitBytes, kPtRestoreExit)) {
        hook_mid(kPtRestoreExit, kRestoreExitHook, g_restore_exit, on_restore_exit);
    }
    if (site_ok(kCrash, kCrashHead, kPtCrash) && site_ok(kCrashSite, kCrashSiteBytes, kPtCrash) &&
        site_ok(kCallerLayer, kCallerLayerBytes, kPtCrash) && site_ok(kCallerCopy, kCallerCopyBytes, kPtCrash) &&
        site_ok(kCallerCall, kCallerCallBytes, kPtCrash)) {
        hook_mid(kPtCrash, kCrash, g_crash, on_crash);
    }
    if (site_ok(kScopeAck, kScopeAckHead, kPtScopeAck)) hook_mid(kPtScopeAck, kScopeAck, g_scope_ack, on_scope_ack);
    if (site_ok(kBadThisFn, kBadThisFnHead, kPtBadThis) && site_ok(kBadThis, kBadThisBytes, kPtBadThis)) {
        hook_mid(kPtBadThis, kBadThis, g_bad_this, on_bad_this);
    }

    std::string on, off;
    for (int p = 0; p < kPoints; ++p) (g_pt[p].on.load() ? on : off) += std::format(" {},", kPointName[p]);
    if (on.empty()) {
        log::warn("Session2 Comp probe: no point could be installed; the probe stays off");
        return false;
    }
    g_active.store(true);
    log::info("Session2 Comp probe ON (read only: owned-actor creation, restore of remote actors, the 0x232D5C0 "
              "null, 2042 entries): on{} off{} | host user key 0x{:08X} user 2 0x{:08X}",
              on, off.empty() ? std::string(" none") : off, g_host_key.load(), g_user2_key.load());
    return true;
}

bool companion_probe_active() { return g_active.load(); }

std::string companion_probe_describe() {
    std::string s = std::format("Comp probe: owned creations {} (created {}, rejected {}) | 0x232D5C0 nulls {} |",
                                g_creates.load(), g_created.load(), g_rejected.load(), g_crash_nulls.load());
    for (int p = 0; p < kPoints; ++p) {
        const PointStats& t = g_pt[p];
        if (!t.on.load()) {
            s += std::format(" {} off |", kPointName[p]);
            continue;
        }
        s += std::format(" {} calls {} user 2 {} lines {} |", kPointName[p], t.calls.load(), t.user2.load(),
                         t.lines.load());
    }
    s += std::format(" lines {} of {} | keys host 0x{:08X} user 2 0x{:08X}", std::min(g_logged.load(), kMaxLogged),
                     kMaxLogged, g_host_key.load(), g_user2_key.load());
    return s;
}

}  // namespace cdmp::session2
