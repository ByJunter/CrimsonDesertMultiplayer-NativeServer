#include "mod/session2/guest_bodyanim_diag.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/guest_bodyanim_diag_logic.h"
#include "mod/session2/guest_seq_route.h"
#include "mod/session2/guest_seq_route_logic.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"

namespace cdmp::session2 {
namespace {

namespace p = p8p;
namespace off = game::build_2976;

// ---------------------------------------------------------------- the code (build 2976, .rsrc; capstone 10-10)
//
//   0xA79770 mov rax,rsp ; mov [rax+0x18],rbx      THE POINT (7 bytes, two whole instructions; no rel8/rel32 branch of
//            .rsrc lands in 0xA79771..0xA79776; the same bytes P8Probe's s1_diag verifies) - rcx = body, rdx = desc,
//            r8b / r9b = p3 / p4
//   0xA797B8 movzx r15d,r8b ; mov r14,rdx ; mov rsi,rcx   (rsi = body until the epilogue: its only write in the function)
//   0xA79BCD / 0xA79F6F mov rax,[r14+0x18] ; ... lea rdx,[rax+0x18]   (the clip's name ref, both configure calls)
//   0xA79C19 mov r8,r12 ; mov rcx,rbx ; call 0x2432F60     (return 0xA79C24, layer 0: rbx = [CC+0x70])
//   0xA79FBB mov r8,[rbp+0x138] ; mov rcx,rbx ; call 0x2432F60   (return 0xA79FCA, layer 1: rbx = [CC+0x78])
//   0x2432F60 mov [rsp+0x20],rbx                   THE POINT (5 bytes, one instruction; no branch into 0x2432F61..64)
//            rcx = graph, rdx / r8 = name refs, xmm3 = float, [rsp+0x28..0x40] = p5..p8 (floats), [rsp+0x48] = p9
constexpr uintptr_t kBodyAnim = gba::kBodyAnim;
constexpr uint32_t kBodyAnimPatch = 7;
constexpr uint8_t kBodyAnimBytes[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x44,
                                      0x88, 0x48, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54};
constexpr uint8_t kArgsBytes[] = {0x45, 0x0F, 0xB6, 0xF8, 0x4C, 0x8B, 0xF2, 0x48, 0x8B, 0xF1};       // 0xA797B8
constexpr uint8_t kName0Bytes[] = {0x49, 0x8B, 0x46, 0x18, 0x48, 0x8D, 0x8E, 0xE8, 0x00, 0x00, 0x00, 0x48};  // 0xA79BCD
constexpr uint8_t kCall0Bytes[] = {0x4D, 0x8B, 0xC4, 0x48, 0x8B, 0xCB, 0xE8, 0x3C, 0x93, 0x9B, 0x01};  // 0xA79C19
constexpr uint8_t kName1Bytes[] = {0x49, 0x8B, 0x46, 0x18, 0x48, 0x8D, 0x50, 0x18, 0x48, 0x8D, 0x8E, 0xE8};  // 0xA79F6F
constexpr uint8_t kCall1Bytes[] = {0x4C, 0x8B, 0x85, 0x38, 0x01, 0x00, 0x00, 0x48,
                                   0x8B, 0xCB, 0xE8, 0x96, 0x8F, 0x9B, 0x01};                        // 0xA79FBB
constexpr uintptr_t kConfigure = gba::kConfigure;
constexpr uint32_t kConfigurePatch = 5;
constexpr uint8_t kConfigureBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x20, 0x4C, 0x89, 0x44,
                                       0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55};

// Structure offsets (gba; remote_motion / guest_seq_restore / companion_probe).
constexpr uint32_t kActor_Key = 0x60, kActor_Comps = 0x68, kActor_Info = 0x88, kComps_CC = 0x40, kCC_Actor = 0x08;
constexpr uint32_t kCC_G70 = 0x70, kCC_G78 = 0x78, kCC_Layer0 = 0x88, kCC_Pending = 0xA8, kCC_Notified = 0x23A;
constexpr uint32_t kGraph_Dead = 0x15, kLayer_Current = 0x10, kAction_Hash = 0x18, kLayer_Hash = 0x3C,
                   kLayer_Chart = 0x110;
constexpr uint32_t kBody_CC = 0x00, kBody_Actor = 0x08;
constexpr uint32_t kDesc_Res = 0x18, kRes_NameRef = 0x18, kName_Hash = 0x0C;
constexpr uint32_t kDesc_F28 = 0x28, kDesc_F2C = 0x2C, kDesc_F30 = 0x30, kDesc_F3C = 0x3C;
constexpr size_t kWatchMax = 64;  // guest_seq_route's G5 limit

uintptr_t g_base = 0;
std::atomic<bool> g_started{false}, g_on_body{false}, g_on_cfg{false};
std::atomic<bool> g_s1_owns{false};
SafetyHookMid g_hook_body, g_hook_cfg;

std::atomic<uint64_t> g_body_all{0}, g_body_npc{0}, g_body_watched{0};
std::atomic<uint64_t> g_cfg_all{0}, g_cfg_body[2] = {}, g_cfg_other{0}, g_cfg_watched{0};
std::atomic<uint64_t> g_w_body[kWatchMax] = {}, g_w_cfg[kWatchMax][2] = {};

enum Kind : uint16_t { kEBody, kECfg, kKindCount };
p::EventRing<256> g_ring;
p::WindowLimiter g_lim[kKindCount] = {{48, 5'000'000}, {64, 5'000'000}};
std::atomic<uint64_t> g_limited{0}, g_lost{0}, g_lines{0};
constexpr uint64_t kMaxLines = 3000;

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
uint8_t type_of(uintptr_t actor) noexcept {
    const uintptr_t info = actor ? rdp(actor + kActor_Info) : 0;
    return info ? rdv<uint8_t>(info + 1) : 0;
}
// A name ref (what 0x2432F60's rdx / r8 point to): [ref] = {char* @0, u32 len @8, u32 hash @0xC}.
void read_name_ref(uintptr_t ref, uint64_t* q, size_t nq, uint32_t& hash) noexcept {
    hash = 0;
    for (size_t i = 0; i < nq; ++i) q[i] = 0;
    const uintptr_t s = ref ? rdp(ref) : 0;
    if (!s) return;
    hash = rdv<uint32_t>(s + kName_Hash);
    const uintptr_t text = rdp(s);
    for (size_t i = 0; text && i < nq && rd(text + 8 * i, q[i]); ++i) {
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

// 0xA79770's entry: the game's thread that runs the scene event (the main thread in session 8: T13152).
void on_bodyanim(safetyhook::Context& ctx) noexcept {
    g_body_all.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t body = ctx.rcx;
    const uintptr_t cc = rdp(body + kBody_CC);
    const uintptr_t actor = rdp(body + kBody_Actor);
    const uint32_t key = key_of(actor);
    if ((key >> 28) == 0xB) g_body_npc.fetch_add(1, std::memory_order_relaxed);
    const int idx = guest_seq_route_watch_index(key);
    if (idx < 0 || static_cast<size_t>(idx) >= kWatchMax) return;
    g_body_watched.fetch_add(1, std::memory_order_relaxed);
    g_w_body[idx].fetch_add(1, std::memory_order_relaxed);
    const uintptr_t desc = ctx.rdx;
    const uintptr_t res = desc ? rdp(desc + kDesc_Res) : 0;
    p::Event e;
    e.v[0] = key | (ctx.r8 & 0xFF) << 32 | (ctx.r9 & 0xFF) << 40 | static_cast<uint64_t>(idx) << 48 |
             static_cast<uint64_t>(type_of(actor)) << 56;
    e.v[1] = body;
    e.v[2] = cc;
    e.v[3] = cc ? rdp(cc + kCC_G70) : 0;
    e.v[4] = cc ? rdp(cc + kCC_G78) : 0;
    e.v[5] = desc;
    uint32_t hash = 0;
    read_name_ref(res ? res + kRes_NameRef : 0, e.v + 6, 3, hash);
    e.v[9] = hash | static_cast<uint64_t>(desc ? rdv<uint32_t>(desc + kDesc_F2C) : 0) << 32;
    e.v[10] = (desc ? rdv<uint32_t>(desc + kDesc_F28) : 0) |
              static_cast<uint64_t>(desc ? rdv<uint32_t>(desc + kDesc_F30) : 0) << 32;
    e.v[11] = (desc ? rdv<uint32_t>(desc + kDesc_F3C) : 0) |
              static_cast<uint64_t>(cc ? rdv<uint8_t>(cc + kCC_Notified) : 0xFF) << 32;
    emit(kEBody, e);
}

// 0x2432F60's entry: only BodyAnim's two calls are read (their rsi is the body).
void on_configure(safetyhook::Context& ctx) noexcept {
    g_cfg_all.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t ret = rdp(ctx.rsp);
    const int layer = ret > g_base ? gba::configure_layer(ret - g_base) : -1;
    if (layer < 0) {
        g_cfg_other.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_cfg_body[layer].fetch_add(1, std::memory_order_relaxed);
    const uintptr_t body = ctx.rsi;
    const uintptr_t actor = rdp(body + kBody_Actor);
    const uint32_t key = key_of(actor);
    const int idx = guest_seq_route_watch_index(key);
    if (idx < 0 || static_cast<size_t>(idx) >= kWatchMax) return;
    g_cfg_watched.fetch_add(1, std::memory_order_relaxed);
    g_w_cfg[idx][layer].fetch_add(1, std::memory_order_relaxed);
    p::Event e;
    e.v[0] = key | static_cast<uint64_t>(layer) << 32 | static_cast<uint64_t>(idx) << 40;
    e.v[1] = ctx.rcx;
    e.v[2] = body;
    uint32_t ha = 0, hb = 0;
    read_name_ref(ctx.rdx, e.v + 3, 2, ha);
    read_name_ref(ctx.r8, e.v + 6, 2, hb);
    e.v[5] = ha | static_cast<uint64_t>(hb) << 32;
    e.v[8] = gba::bits_of(ctx.xmm3.f32[0]) | static_cast<uint64_t>(rdv<uint32_t>(ctx.rsp + 0x28)) << 32;
    e.v[9] = rdv<uint32_t>(ctx.rsp + 0x30) | static_cast<uint64_t>(rdv<uint32_t>(ctx.rsp + 0x38)) << 32;
    e.v[10] = rdv<uint32_t>(ctx.rsp + 0x40);
    const uintptr_t holder = rdp(ctx.rsp + 0x48);
    e.v[11] = holder ? rdp(holder) : 0;
    emit(kECfg, e);
}

// ---------------------------------------------------------------- lines (diagnostics thread)

std::string text_of(const uint64_t* q, size_t nq) {
    char buf[40];
    gsr::scene_name(q, nq, buf, sizeof buf);
    return buf;
}
float f(uint64_t bits) { return gba::f32_of(static_cast<uint32_t>(bits)); }

std::string line_of(const p::Event& e) {
    const uint64_t* v = e.v;
    const std::string when = p8_when(e.qpc_us);
    const auto key = static_cast<uint32_t>(v[0]);
    if (e.kind == kEBody) {
        return std::format("GuestBodyAnim [BodyAnim 0xA79770] {} T{} | NPC 0x{:08X} (type {}, watched #{}) | body 0x{:X}, CC "
                           "0x{:X}: before +0x70 0x{:X}, +0x78 0x{:X}, +0x23A {} | desc 0x{:X}: clip '{}' hash 0x{:08X}, "
                           "+0x28 {:.3f} +0x2C {:.3f} +0x30 {:.3f} +0x3C {:.3f} | p3 {} p4 {}",
                           when, e.tid, key, (v[0] >> 56) & 0xFF, (v[0] >> 48) & 0xFF, v[1], v[2], v[3], v[4],
                           (v[11] >> 32) & 0xFF, v[5], text_of(v + 6, 3), static_cast<uint32_t>(v[9]), f(v[10]),
                           f(v[9] >> 32), f(v[10] >> 32), f(v[11]), (v[0] >> 32) & 0xFF, (v[0] >> 40) & 0xFF);
    }
    return std::format("GuestBodyAnim [configure 0x2432F60, layer {}] {} T{} | NPC 0x{:08X} (watched #{}) | graph 0x{:X} "
                       "(body 0x{:X}) | name '{}' 0x{:08X}, blend '{}' 0x{:08X} | xmm3 {:.3f}, p5 {:.3f} p6 {:.3f} p7 "
                       "{:.3f} p8 {:.3f} | resource [p9] 0x{:X}",
                       (v[0] >> 32) & 0xFF, when, e.tid, key, (v[0] >> 40) & 0xFF, v[1], v[2], text_of(v + 3, 2),
                       static_cast<uint32_t>(v[5]), text_of(v + 6, 2), static_cast<uint32_t>(v[5] >> 32), f(v[8]),
                       f(v[8] >> 32), f(v[9]), f(v[9] >> 32), f(v[10]), v[11]);
}

gba::BodySnap read_body(uint32_t key, size_t idx) {
    gba::BodySnap s;
    s.bodyanim = g_w_body[idx].load(std::memory_order_relaxed);
    s.cfg[0] = g_w_cfg[idx][0].load(std::memory_order_relaxed);
    s.cfg[1] = g_w_cfg[idx][1].load(std::memory_order_relaxed);
    const uintptr_t a = guest_seq_route_find_actor(key);
    if (!a) return s;
    s.exists = true;
    s.type = type_of(a);
    const uintptr_t comps = rdp(a + kActor_Comps);
    const uintptr_t cc = comps ? rdp(comps + kComps_CC) : 0;
    if (!cc || rdp(cc + kCC_Actor) != a) return s;
    s.cc = true;
    s.g70 = rdp(cc + kCC_G70);
    s.g78 = rdp(cc + kCC_G78);
    s.g70_dead = s.g70 ? rdv<uint8_t>(s.g70 + kGraph_Dead) : 0;
    s.g78_dead = s.g78 ? rdv<uint8_t>(s.g78 + kGraph_Dead) : 0;
    s.notified = rdv<uint8_t>(cc + kCC_Notified);
    s.pending = rdv<uint8_t>(cc + kCC_Pending);
    for (int i = 0; i < 2; ++i) {
        const uintptr_t l = rdp(cc + kCC_Layer0 + 8u * i);
        gba::LayerSnap& ls = s.layer[i];
        if (!l) continue;
        ls.present = true;
        ls.chart = rdp(l + kLayer_Chart) != 0;
        ls.hash = rdv<uint32_t>(l + kLayer_Hash);
        const uintptr_t cur = rdp(l + kLayer_Current);
        ls.current = cur ? rdv<uint32_t>(cur + kAction_Hash) : 0;
    }
    return s;
}

std::string layer_text(const gba::LayerSnap& l) {
    if (!l.present) return "none";
    return std::format("current 0x{:08X}{} (+0x3C 0x{:08X}{}), chart {}", l.current,
                       gba::is_seq_hash(l.current) ? " seq_bodyanim" : l.current == gba::kHashWait ? " WAIT" : "",
                       l.hash, gba::is_seq_hash(l.hash) ? " seq_bodyanim" : l.hash == gba::kHashWait ? " WAIT" : "",
                       l.chart ? "yes" : "NO");
}

std::string body_text(const gba::BodySnap& s) {
    if (!s.exists) return "NOT in this client";
    if (!s.cc) return std::format("type {}, no control component (or CC+8 is not this actor)", s.type);
    return std::format("type {} | CC+0x70 0x{:X}{} | CC+0x78 0x{:X}{} | +0x23A {} | +0xA8 {} | L0 {} | L1 {} | hooks: "
                       "BodyAnim {}, configure L0 {} / L1 {} | {}",
                       s.type, s.g70, s.g70_dead ? " (INVALIDATED +0x15)" : "", s.g78, s.g78_dead ? " (INVALIDATED +0x15)" : "",
                       s.notified, s.pending, layer_text(s.layer[0]), layer_text(s.layer[1]), s.bodyanim, s.cfg[0],
                       s.cfg[1], gba::body_state_name(gba::body_state(s)));
}

std::map<uint32_t, gba::BodySnap> g_last;  // tick thread only
uint64_t g_ticks = 0;

void g5_body() {
    gba::Cohort c;
    const size_t n = std::min(guest_seq_route_watch_count(), kWatchMax);
    for (size_t i = 0; i < n; ++i) {
        const uint32_t key = guest_seq_route_watch_key(i);
        if (!key) continue;
        const gba::BodySnap now = read_body(key, i);
        c.add(now);
        auto it = g_last.find(key);
        const bool known = it != g_last.end();
        const bool changed = !known || !(it->second == now);
        if (!changed && g_ticks % 12 != 1) continue;
        g_last[key] = now;
        if (g_lines.fetch_add(1) < kMaxLines) {
            log::info("GuestBodyAnim [G5 body] 0x{:08X}: {}{}", key, body_text(now), changed ? "" : " (unchanged)");
        }
    }
    if (g_ticks % 12 != 0) return;
    std::string states;
    for (size_t k = 0; k < static_cast<size_t>(gba::BodyState::Count); ++k) {
        if (c.state[k]) states += std::format(" {} {};", gba::body_state_name(static_cast<gba::BodyState>(k)), c.state[k]);
    }
    log::info("GuestBodyAnim summary | BodyAnim 0xA79770 ({}): calls {}, NPC keys {}, watched {} | configure 0x2432F60 "
              "({}): calls {}, from BodyAnim L0 {} / L1 {}, other callers {}, watched {} | watched NPCs {}:{} with "
              "CC+0x78 {}; BodyAnim seen for {} ({} of them without CC+0x70 now) | lines limited {} lost {}",
              g_on_body.load() ? "hooked" : g_s1_owns.load() ? "P8Probe s1_diag owns it: see its lines" : "NOT hooked",
              g_body_all.load(), g_body_npc.load(), g_body_watched.load(), g_on_cfg.load() ? "hooked" : "NOT hooked",
              g_cfg_all.load(), g_cfg_body[0].load(), g_cfg_body[1].load(), g_cfg_other.load(), g_cfg_watched.load(),
              c.watched, states.empty() ? " none" : states, c.g78, c.ran, c.ran_no_graph, g_limited.load(), g_lost.load());
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

bool checks_ok(std::span<const gad::Section> secs, std::span<const Check> checks, const char* what) {
    for (const Check& c : checks) {
        if (!gad::in_real_code(secs, static_cast<uint32_t>(c.rva), static_cast<uint32_t>(c.bytes.size())) ||
            !bytes_at(c.rva, c.bytes)) {
            log::error("GuestBodyAnim: {}: the code at exe+0x{:X} ({} bytes) is not build 2976's .rsrc code (or another "
                       "module hooked it); not installed",
                       what, c.rva, c.bytes.size());
            return false;
        }
    }
    return true;
}

DWORD WINAPI install_thread(LPVOID) {
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::vector<gad::Section> secs;
    if (g_base != off::kImageBase || !game::safe_read(g_base, &dos, sizeof dos) ||
        !game::safe_read(g_base + dos.e_lfanew, &nt, sizeof nt) || nt.FileHeader.TimeDateStamp != off::kPeTimestamp ||
        !p8_exe_sections(g_base, secs)) {
        log::error("GuestBodyAnim: unsupported game build or unreadable sections; nothing installed (G5 body lines only)");
        return 0;
    }
    for (int i = 0; i < 1200; ++i) {  // WorldSystem (up to 10 minutes), as the other guest modules
        uintptr_t world = 0;
        if (game::safe_read(g_base + off::kWorldSystemSlot, &world, sizeof world) && world) break;
        Sleep(500);
    }
    // The configure point's context: BodyAnim's two call sites (their return addresses and rsi = body).
    const Check common[] = {{0xA797B8, kArgsBytes}, {0xA79BCD, kName0Bytes}, {0xA79C19, kCall0Bytes},
                            {0xA79F6F, kName1Bytes}, {0xA79FBB, kCall1Bytes}};
    const bool ctx_ok = checks_ok(secs, common, "BodyAnim's call sites");
    std::string body_text_install;
    if (g_s1_owns.load()) {
        body_text_install = "0xA79770 left to P8Probe (s1_diag=1: its 'BodyAnim 0xA79770' lines)";
    } else if (ctx_ok && p8_install_mid(g_base, secs, kBodyAnim, kBodyAnimPatch, kBodyAnimBytes, on_bodyanim, g_hook_body,
                                        "GuestBodyAnim", "BodyAnim entry 0xA79770")) {
        g_on_body.store(true);
        body_text_install = "0xA79770 hooked";
    } else {
        body_text_install = "0xA79770 NOT hooked (see the line above)";
    }
    if (ctx_ok && p8_install_mid(g_base, secs, kConfigure, kConfigurePatch, kConfigureBytes, on_configure, g_hook_cfg,
                                 "GuestBodyAnim", "graph configuration entry 0x2432F60")) {
        g_on_cfg.store(true);
    }
    log::info("GuestBodyAnim ON (guest_seq_bodyanim_diag=1, READ-ONLY): {} | 0x2432F60 {} | for the actors of "
              "guest_seq_route's G5 watch list: 'GuestBodyAnim [BodyAnim 0xA79770]' (clip name / hash / times, CC+0x70/+0x78 "
              "before) and '[configure 0x2432F60, layer N]' (graph, names, floats, resource); every call counted; "
              "'GuestBodyAnim [G5 body]' every 5 s on change (CC+0x70/+0x78, +0x23A, layers' actions); summary every 60 s",
              body_text_install, g_on_cfg.load() ? "hooked" : "NOT hooked");
    return 0;
}

}  // namespace

void guest_bodyanim_diag_start(bool enabled, const std::string& route_mode, bool s1_diag) {
    if (!enabled) return;
    const gsr::Mode rm = gsr::parse_mode(route_mode);
    if (rm == gsr::Mode::Off) {
        log::warn("GuestBodyAnim: guest_seq_bodyanim_diag=1 needs guest_seq_route=probe or 1 (its G5 watch list); off");
        return;
    }
    g_s1_owns.store(s1_diag);
    if (g_started.exchange(true)) return;
    CloseHandle(CreateThread(nullptr, 0, install_thread, nullptr, 0, nullptr));
}

void guest_bodyanim_diag_tick() noexcept {
    if (!g_started.load()) return;
    try {
        ++g_ticks;
        p::Event e;
        while (g_ring.pop(e)) {
            if (e.kind >= kKindCount) continue;
            if (g_lines.fetch_add(1) < kMaxLines) log::info("{}", line_of(e));
        }
        if (g_base) g5_body();
    } catch (...) {
    }
}

std::string guest_bodyanim_diag_describe() {
    if (!g_started.load()) return {};
    return std::format(" | bodyanim diag: BodyAnim {} (watched {}), configure {} (watched {})", g_body_all.load(),
                       g_body_watched.load(), g_cfg_all.load(), g_cfg_watched.load());
}

}  // namespace cdmp::session2
