#include "mod/session2/rope_probe.h"

#include <windows.h>

#include <safetyhook.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "mod/game/actor_lookup.h"
#include "mod/game/hook_prepare.h"
#include "mod/game/offsets.h"
#include "mod/game/player_reader.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"
#include "mod/session2/rope_probe_logic.h"
#include "mod/session2/session2.h"
#include "mod/session2/tree_bend.h"

namespace cdmp::session2 {
namespace {

namespace g2976 = game::build_2976;

constexpr uintptr_t kCodeBegin = 0x1000, kCodeEnd = 0x51EB000;
constexpr uint32_t kTickMs = 500;
constexpr uint32_t kWorldTicks = 6;
constexpr uint64_t kMinuteMs = 60000;
constexpr uint64_t kEventLines = 300;

enum EventKind : uint16_t {
    kEvTree = 1,       // TreePulling vf1 ran
    kEvRunStart = 2,   // a character's tree-pull timer started running in 0x8A30C0
    kEvRunEnd = 3,     // ... and stopped
    kEvDev2005 = 4,    // a device 2005 received
    kEvDevSent = 5,    // a device gimmick event this client sent
    kEvDevBranch = 6,  // a 2609 of a known device this client sent
    kEvDevRemove = 7,  // a known device removed (2516)
    kEvPlayer = 8      // a player character seen in the server's traffic (2991 / 2763)
};

std::atomic<bool> g_started{false}, g_on{false}, g_guest{false}, g_hooks_on{false};
uintptr_t g_base = 0;
SafetyHookInline g_tree_hook, g_pull_hook;

p8p::EventRing<512> g_ring;
// Player characters only (top nibble 0xA): a handful per session. Every other caller of 0x8A30C0 (NPCs, animals, an
// unreadable key) only adds to g_other_calls.
rpb::KeyTable<64> g_keys;
std::atomic<uint64_t> g_other_calls{0}, g_tree_runs{0}, g_tree_here{0}, g_tree_remote{0};
std::atomic<uint64_t> g_sniffed_dev_events{0}, g_sniffed_2005_dev{0};
std::atomic<uint32_t> g_seq{0};

// Per-run counters of a character (the hook's thread only: one movement update per actor at a time).
struct RunFrames {
    std::atomic<uint32_t> frames{0}, applied{0};
};
RunFrames g_run[rpb::KeyTable<64>::size()];

int access_filter(const EXCEPTION_POINTERS* ep) noexcept {
    const EXCEPTION_RECORD* r = ep ? ep->ExceptionRecord : nullptr;
    if (!r) return EXCEPTION_CONTINUE_SEARCH;
    return r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || r->ExceptionCode == EXCEPTION_IN_PAGE_ERROR
               ? EXCEPTION_EXECUTE_HANDLER
               : EXCEPTION_CONTINUE_SEARCH;
}
bool plausible(uintptr_t a, size_t n) { return a >= 0x10000 && a <= 0x00007FFFFFFFFFFFull - n; }
// The 5th argument of 0x6D46D0, a slot in the CALLER's frame on this same thread (committed stack): a plain SEH
// read. Everything else goes through game::fast_read (it re-arms a guard page it touches, P8 review I2).
bool seh_copy(uintptr_t a, void* out, size_t n) noexcept {
    if (!plausible(a, n)) return false;
    __try {
        const volatile uint8_t* s = reinterpret_cast<const volatile uint8_t*>(a);
        uint8_t* d = static_cast<uint8_t*>(out);
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
        return true;
    } __except (access_filter(GetExceptionInformation())) {
        return false;
    }
}

uint32_t next_seq() { return g_seq.fetch_add(1, std::memory_order_relaxed) + 1; }

uint64_t pack2(float lo, float hi) {
    uint32_t a = 0, b = 0;
    std::memcpy(&a, &lo, 4);
    std::memcpy(&b, &hi, 4);
    return uint64_t{a} | (uint64_t{b} << 32);
}
float lo_f(uint64_t v) {
    const uint32_t a = static_cast<uint32_t>(v);
    float f;
    std::memcpy(&f, &a, 4);
    return f;
}
float hi_f(uint64_t v) {
    const uint32_t a = static_cast<uint32_t>(v >> 32);
    float f;
    std::memcpy(&f, &a, 4);
    return f;
}

// The body pair of a movement controller, as 0x8A30C0 tests it (game::fast reads only). far_vt: the RVA of the far
// object's vtable when it is in the exe image (0 otherwise); far_type: its +0x30; far_key: the u32 at its base +0x60
// (an actor's key when the far object is an actor's; a measurement otherwise).
struct PairInfo {
    uint16_t bits = 0;
    uint8_t far_type = 0;
    uint32_t far_key = 0;
    uint32_t far_vt = 0;
};
PairInfo read_pair(uintptr_t mc) noexcept {
    PairInfo r;
    const auto holder = game::fast_ptr(mc + rpb::kMc_Pair);
    if (!holder) return r;
    r.bits |= rpb::kPairHolder;
    const auto pair = game::fast_ptr(*holder + rpb::kPairHolder_Pair);
    if (!pair) return r;
    r.bits |= rpb::kPairPair;
    const auto far_side = game::fast_ptr(*pair + rpb::kPair_Far);
    const auto far_obj = far_side ? game::fast_ptr(*far_side + rpb::kSide_Obj) : std::nullopt;
    if (far_obj) {
        r.bits |= rpb::kPairFar;
        const auto gone = game::fast<uint8_t>(*far_obj + rpb::kObj_Gone);
        const auto type = game::fast<uint8_t>(*far_obj + rpb::kObj_Type);
        if (type) r.far_type = *type;
        if (gone && type && *gone == 0 && *type > 4) r.bits |= rpb::kPairFarOk;
        const uintptr_t base = *far_obj - rpb::kObj_Base;
        if (const auto vt = game::fast_ptr(base)) {
            if (*vt >= g_base && *vt < g_base + 0x10000000) r.far_vt = static_cast<uint32_t>(*vt - g_base);
        }
        if (const auto k = game::fast<uint32_t>(base + rpb::kActor_Key)) r.far_key = *k;
    }
    const auto near_side = game::fast_ptr(*pair + rpb::kPair_Near);
    const auto near_obj = near_side ? game::fast_ptr(*near_side + rpb::kSide_Obj) : std::nullopt;
    if (near_obj) {
        const auto gone = game::fast<uint8_t>(*near_obj + rpb::kObj_Gone);
        if (gone && *gone == 0) r.bits |= rpb::kPairNear;
    }
    if (const auto link = game::fast_ptr(*pair + rpb::kPair_Link)) {
        const auto a = game::fast<uint8_t>(*link + rpb::kLink_On0);
        const auto b = game::fast<uint8_t>(*link + rpb::kLink_On1);
        if (a && b && *a && *b) r.bits |= rpb::kPairLink;
    }
    r.bits |= rpb::kPairRead;
    return r;
}
uint64_t pair_word(const PairInfo& p) {
    return p.bits | (uint64_t{p.far_type} << 16) | (uint64_t{p.far_key} << 32);
}

// ---------------------------------------------------------------- A1: TreePulling vf1 (log only)

using Arg = uint64_t;
Arg tree_detour(Arg self, Arg err, Arg ev, Arg p4, Arg holder) {
    p8p::Event e;
    e.kind = kEvTree;
    uint64_t actor = 0;
    uintptr_t mc = 0;
    float before = -2.0f;
    uint32_t key = 0;
    uint16_t ctl = 0;
    if (seh_copy(holder, &actor, sizeof actor) && plausible(actor, 0x100)) {
        if (const auto k = game::fast<uint32_t>(actor + rpb::kActor_Key)) key = *k;
        if (const auto c = game::fast<uint8_t>(actor + rpb::kActor_Control)) ctl = static_cast<uint16_t>(0x100 | *c);
        const auto comps = game::fast_ptr(actor + rpb::kActor_Comps);
        const auto control = comps ? game::fast_ptr(*comps + rpb::kComps_Control) : std::nullopt;
        const auto m = control ? game::fast_ptr(*control + rpb::kControl_Mc) : std::nullopt;
        if (m) {
            mc = *m;
            if (const auto t = game::fast<float>(mc + rpb::kMc_Timer)) before = *t;
        }
    }
    // Raw bytes for the offline decode of which action / frame fired it: this +8..+0x28, the event's data
    // ([event+8], 16 bytes) and the fourth argument.
    uint64_t raw[4] = {}, data[2] = {}, data_ptr = 0;
    (void)game::fast_read(self + 8, raw, sizeof raw);
    // The original never reads its 3rd argument: its +8 and that pointer's data, guarded (fast_read checks the range
    // and re-arms a guard page).
    if (game::fast_read(ev + 8, &data_ptr, sizeof data_ptr)) (void)game::fast_read(data_ptr, data, sizeof data);
    // Never inside a __try: the game's own exceptions are the game's.
    const Arg r = g_tree_hook.unsafe_call<Arg>(self, err, ev, p4, holder);
    float after = -2.0f;
    PairInfo pair;
    if (mc) {
        if (const auto t = game::fast<float>(mc + rpb::kMc_Timer)) after = *t;
        pair = read_pair(mc);
    }
    g_tree_runs.fetch_add(1, std::memory_order_relaxed);
    if (ctl & 0xFF) {
        g_tree_here.fetch_add(1, std::memory_order_relaxed);
    } else if (ctl) {
        g_tree_remote.fetch_add(1, std::memory_order_relaxed);
    }
    e.qpc_us = p8_now_us();
    e.tid = GetCurrentThreadId();
    e.flags = static_cast<uint16_t>((mc ? 1u : 0u));
    e.v[0] = key | (uint64_t{ctl} << 32);
    e.v[1] = pack2(before, after);
    e.v[2] = pair_word(pair);
    e.v[3] = pair.far_vt;
    e.v[4] = raw[0];
    e.v[5] = raw[1];
    e.v[6] = raw[2];
    e.v[7] = raw[3];
    e.v[8] = data[0];
    e.v[9] = data[1];
    e.v[10] = p4;
    e.v[11] = next_seq();
    g_ring.push(e);
    return r;
}

// ---------------------------------------------------------------- A2: the per-frame tree-pull apply (counts)

void push_run(uint16_t kind, uintptr_t mc, uint32_t key, uint8_t ctl, float timer, float applied, uint32_t frames,
              uint32_t applied_frames) noexcept {
    const PairInfo pair = read_pair(mc);
    p8p::Event e;
    e.qpc_us = p8_now_us();
    e.tid = GetCurrentThreadId();
    e.kind = kind;
    e.v[0] = key | (uint64_t{ctl} << 32);
    e.v[1] = pack2(timer, applied);
    e.v[2] = pair_word(pair);
    e.v[3] = pair.far_vt;
    e.v[4] = frames | (uint64_t{applied_frames} << 32);
    e.v[5] = next_seq();
    g_ring.push(e);
}

// 0x8A30C0 (movement controller, dt in xmm1). Every frame for every character this client updates: two guarded
// reads for the key; a player character's call also counts and reads the timer / +0x638 before and after; an event
// only when a timer run starts or ends. Runs are per movement controller: a new controller, or the timer going back
// (TreePulling resetting it), starts a new run.
void pull_detour(Arg mc, float dt) {
    uint32_t key = 0;
    uintptr_t actor = 0;
    if (const auto a = game::fast_ptr(mc + rpb::kMc_Actor)) {
        actor = *a;
        if (const auto k = game::fast<uint32_t>(actor + rpb::kActor_Key)) key = *k;
    }
    rpb::KeyCount* entry = rpb::player_key(key) ? g_keys.get(key) : nullptr;
    if (!entry) {
        g_other_calls.fetch_add(1, std::memory_order_relaxed);
        g_pull_hook.unsafe_call<void>(mc, dt);
        return;
    }
    if (entry->ctl.load(std::memory_order_relaxed) == 0xFF) {
        if (const auto c = game::fast<uint8_t>(actor + rpb::kActor_Control)) entry->ctl.store(*c, std::memory_order_relaxed);
    }
    const uint8_t ctl = entry->ctl.load(std::memory_order_relaxed);
    const size_t index = g_keys.index(entry);
    const auto before = game::fast<float>(mc + rpb::kMc_Timer);
    const auto applied_before = game::fast<float>(mc + rpb::kMc_Applied);
    g_pull_hook.unsafe_call<void>(mc, dt);
    entry->calls.fetch_add(1, std::memory_order_relaxed);
    const bool running = before && rpb::timer_running(*before);
    const bool was = entry->running.load(std::memory_order_relaxed) != 0;
    if (!running && !was) {
        tree_bend_note_pull(key, ctl, false, false);  // S29: tree_bend's 'inline bends on a remote' (atomics only)
        return;
    }
    bool inline_bend = false;
    const auto after = game::fast<float>(mc + rpb::kMc_Timer);
    const auto applied = game::fast<float>(mc + rpb::kMc_Applied);
    RunFrames& rf = g_run[index];
    if (running) {
        const bool new_run = !was || entry->run_mc.load(std::memory_order_relaxed) != mc ||
                             *before < entry->last_timer.load(std::memory_order_relaxed);
        if (new_run) {
            if (was) {  // the previous run did not end on its own (another controller, or the timer reset)
                push_run(kEvRunEnd, mc, key, ctl, *before, applied.value_or(-2.0f), rf.frames.load(std::memory_order_relaxed),
                         rf.applied.load(std::memory_order_relaxed));
            }
            entry->running.store(1, std::memory_order_relaxed);
            entry->run_mc.store(mc, std::memory_order_relaxed);
            entry->runs.fetch_add(1, std::memory_order_relaxed);
            rf.frames.store(0, std::memory_order_relaxed);
            rf.applied.store(0, std::memory_order_relaxed);
            push_run(kEvRunStart, mc, key, ctl, *before, applied.value_or(-2.0f), 0, 0);
        }
        entry->last_timer.store(*before, std::memory_order_relaxed);
        entry->running_frames.fetch_add(1, std::memory_order_relaxed);
        rf.frames.fetch_add(1, std::memory_order_relaxed);
        if (rpb::pull_applied(applied_before.has_value(), applied_before.value_or(0.0f), applied.has_value(),
                              applied.value_or(-1.0f))) {
            entry->applied_frames.fetch_add(1, std::memory_order_relaxed);
            rf.applied.fetch_add(1, std::memory_order_relaxed);
            inline_bend = true;  // +0x638 written: 0x8A30C0's inline SetBend copy ran
        }
    }
    tree_bend_note_pull(key, ctl, running, inline_bend);
    if (!after || !rpb::timer_running(*after)) {
        entry->running.store(0, std::memory_order_relaxed);
        entry->last_timer.store(-1.0f, std::memory_order_relaxed);
        push_run(kEvRunEnd, mc, key, ctl, after.value_or(-2.0f), applied.value_or(-2.0f),
                 rf.frames.load(std::memory_order_relaxed), rf.applied.load(std::memory_order_relaxed));
    }
}

// ---------------------------------------------------------------- B: the sniffer's tap (writer thread)

// Tap-local sets (the writer thread only): the device keys seen, the player keys already reported.
uint32_t t_devices[128] = {};
size_t t_devices_n = 0;
uint32_t t_players[32] = {};
size_t t_players_n = 0;
bool t_has(const uint32_t* s, size_t n, uint32_t k) {
    for (size_t i = 0; i < n; ++i) {
        if (s[i] == k) return true;
    }
    return false;
}
void t_add_device(uint32_t k) {
    if (!t_has(t_devices, t_devices_n, k) && t_devices_n < std::size(t_devices)) t_devices[t_devices_n++] = k;
}

void push_simple(uint16_t kind, uint64_t t_us, uint64_t v0, uint64_t v1) noexcept {
    p8p::Event e;
    e.qpc_us = t_us;
    e.tid = GetCurrentThreadId();
    e.kind = kind;
    e.v[0] = v0;
    e.v[1] = v1;
    e.v[2] = next_seq();
    g_ring.push(e);
}

}  // namespace

void rope_probe_sniffed(const uint8_t* packet, size_t size, uint64_t t_us, bool server_to_client) noexcept {
    if (!g_on.load(std::memory_order_relaxed) || !packet || size < rpb::kHeader) return;
    const uint16_t op = static_cast<uint16_t>(packet[0] | (packet[1] << 8));
    const std::span<const uint8_t> pl(packet + rpb::kHeader, size - rpb::kHeader);
    if (!server_to_client) {
        if (op == rpb::kOpGimmickFrameEvent || op == rpb::kOpUpdateGimmick) {
            const auto r = rpb::parse_gimmick_req(pl);
            if (!r || !rpb::dev_event(r->hash)) return;
            t_add_device(r->gimmick);
            g_sniffed_dev_events.fetch_add(1, std::memory_order_relaxed);
            push_simple(kEvDevSent, t_us, r->gimmick | (uint64_t{r->character} << 32), r->hash | (uint64_t{op} << 32));
        } else if (op == rpb::kOpGimmickBranch && pl.size() >= 8) {
            const uint32_t g = rpb::le32(&pl[0]);
            if (t_has(t_devices, t_devices_n, g)) push_simple(kEvDevBranch, t_us, g | (uint64_t{rpb::le32(&pl[4])} << 32), 0);
        }
        return;
    }
    if (op == rpb::kOpActorLoad) {
        const auto l = rpb::parse_2005(pl);
        if (!l || !l->device) return;
        t_add_device(l->key);
        g_sniffed_2005_dev.fetch_add(1, std::memory_order_relaxed);
        push_simple(kEvDev2005, t_us, l->key | (uint64_t{l->owner} << 32), l->parent);
    } else if (op == rpb::kOpRemoveActor && pl.size() >= 10) {
        for (size_t i = 6; i + 4 <= pl.size(); i += 4) {  // every key the Ack lists
            const uint32_t k = rpb::le32(&pl[i]);
            if (t_has(t_devices, t_devices_n, k)) push_simple(kEvDevRemove, t_us, k, 0);
        }
    } else if ((op == rpb::kOpStartActionAck || op == rpb::kOpRemoteCatchAck) && pl.size() >= 4) {
        const uint32_t k = rpb::le32(&pl[0]);
        if (rpb::player_key(k) && !t_has(t_players, t_players_n, k) && t_players_n < std::size(t_players)) {
            t_players[t_players_n++] = k;
            push_simple(kEvPlayer, t_us, k | (uint64_t{op} << 32), 0);
        }
    }
}

namespace {

// ---------------------------------------------------------------- install (the module's thread, world up)

bool bytes_at(uintptr_t rva, std::span<const uint8_t> expected) {
    std::vector<uint8_t> actual(expected.size());
    return game::safe_read(g_base + rva, actual.data(), actual.size()) &&
           std::equal(actual.begin(), actual.end(), expected.begin());
}
bool qword_at(uintptr_t rva, uint64_t expected) {
    uint64_t v = 0;
    return game::safe_read(g_base + rva, &v, sizeof v) && v == expected;
}

struct Prepared {
    SafetyHookInline* slot = nullptr;
    std::optional<SafetyHookInline> hook;
};

bool prepare(uintptr_t rva, void* detour, std::span<const uint8_t> head, std::span<const uint32_t> bounds,
             const char* what, SafetyHookInline& slot, std::vector<Prepared>& out) {
    auto whole = [&](const std::vector<uint8_t>& o) {
        return std::find(bounds.begin(), bounds.end(), static_cast<uint32_t>(o.size())) != bounds.end() &&
               o.size() <= head.size() && std::memcmp(o.data(), head.data(), o.size()) == 0;
    };
    auto h = game::prepare_inline(g_base + rva, detour, whole, "RopeProbe", what);
    if (!h) {
        log::error("RopeProbe: the hook on {} (exe+0x{:X}) could not be prepared ({}); REFUSED, nothing written", what,
                   rva, game::inline_error_text(h.error()));
        return false;
    }
    if (!whole(h->original_bytes())) {
        log::error("RopeProbe: the hook on {} (exe+0x{:X}) would patch {} bytes, not whole verified instructions; "
                   "REFUSED, nothing written",
                   what, rva, h->original_bytes().size());
        return false;
    }
    Prepared p;
    p.slot = &slot;
    p.hook = std::move(*h);
    out.push_back(std::move(p));
    return true;
}

bool install_hooks() {
    std::vector<gad::Section> secs;
    if (!p8_exe_sections(g_base, secs)) {
        log::error("RopeProbe: the exe's sections are unreadable; REFUSED, nothing written");
        return false;
    }
    const auto in_rsrc = [&](uintptr_t rva, size_t n) {
        return rva >= kCodeBegin && rva + n <= kCodeEnd &&
               gad::in_real_code(secs, static_cast<uint32_t>(rva), static_cast<uint32_t>(n));
    };
    struct Check {
        uintptr_t rva;
        std::span<const uint8_t> bytes;
        const char* what;
    };
    const Check checks[] = {
        {rpb::kTreePull, rpb::kTreePullHead, "pa::ClientFrameEventTreePulling::vf1 0x6D46D0's entry"},
        {rpb::kPullApply, rpb::kPullApplyHead, "the tree pull's per-frame apply 0x8A30C0's entry"},
        {rpb::kPullApplyCall, rpb::kPullApplyCallBytes, "its only caller 0x87B2BE..0x87B2CA (call 0x8A30C0)"},
    };
    for (const Check& c : checks) {
        if (!in_rsrc(c.rva, c.bytes.size()) || !bytes_at(c.rva, c.bytes)) {
            log::error("RopeProbe: {} (exe+0x{:X}, {} bytes) is not build 2976's; REFUSED, nothing written", c.what,
                       c.rva, c.bytes.size());
            return false;
        }
    }
    if (!qword_at(rpb::kTreePullSlot, g_base + rpb::kTreePull)) {
        log::error("RopeProbe: the vtable slot 0x5584138 does not hold 0x6D46D0; REFUSED, nothing written");
        return false;
    }
    std::vector<Prepared> ready;
    // Only the 5-byte jump: 5 bytes at 0x6D46D0, 11 (three whole instructions) at 0x8A30C0; never the 14-byte fallback.
    if (!prepare(rpb::kTreePull, reinterpret_cast<void*>(&tree_detour), rpb::kTreePullHead, rpb::kTreePullPatch,
                 "pa::ClientFrameEventTreePulling::vf1 0x6D46D0", g_tree_hook, ready) ||
        !prepare(rpb::kPullApply, reinterpret_cast<void*>(&pull_detour), rpb::kPullApplyHead, rpb::kPullApplyPatch,
                 "the tree pull's per-frame apply 0x8A30C0", g_pull_hook, ready)) {
        return false;
    }
    std::string patched;
    for (Prepared& p : ready) {
        patched += std::format("{}{}", patched.empty() ? "" : "/", p.hook->original_bytes().size());
        *p.slot = std::move(*p.hook);
    }
    std::vector<SafetyHookInline*> enabled;
    for (Prepared& p : ready) {
        if (!p.slot->enable()) {
            log::error("RopeProbe: a hook could not be enabled; the others are put back; REFUSED");
            for (SafetyHookInline* h : enabled) (void)h->disable();
            return false;
        }
        enabled.push_back(p.slot);
    }
    g_hooks_on.store(true);
    log::info("RopeProbe: hooked at their entries ({} bytes patched): exe+0x6D46D0 (pa::ClientFrameEventTreePulling::vf1, "
              "log only) and exe+0x8A30C0 (the tree pull's per-frame apply, counted per character; one caller "
              "0x87B2C6); nothing of the game is written",
              patched);
    return true;
}

// ---------------------------------------------------------------- the module's thread

bool build_ok() {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    return g_base == g2976::kImageBase && game::safe_read(g_base, &dos, sizeof dos) &&
           game::safe_read(g_base + dos.e_lfanew, &nt, sizeof nt) && nt.FileHeader.TimeDateStamp == g2976::kPeTimestamp;
}

// Who a key is on THIS client (the module's thread).
std::string who_here(uint32_t key) {
    if (!key) return "no key";
    const auto actor = game::find_actor(key);
    if (!actor) return "not found on this client";
    const auto ctl = game::read<uint8_t>(*actor + rpb::kActor_Control);
    if (!ctl) return "control unreadable";
    return *ctl ? std::format("controlled HERE (+0xC8 = {})", *ctl) : std::string("REMOTE here (+0xC8 = 0)");
}
std::string ctl_text(uint64_t opt) {
    if (!(opt & 0x100)) return "control unread";
    return (opt & 0xFF) ? std::format("controlled HERE (+0xC8 = {})", opt & 0xFF) : std::string("REMOTE here");
}
std::string pair_text(uint64_t w, uint64_t far_vt) {
    const auto bits = static_cast<uint16_t>(w & 0xFFFF);
    if (!(bits & rpb::kPairHolder)) return "NO pair ([mc+0x188] null)";
    if (!(bits & rpb::kPairPair)) return "NO pair ([[mc+0x188]+0x50] null)";
    return std::format("{}: far {} (ok {}, type {}, object vtable exe+0x{:X}, +0x60 0x{:08X}) near {} link {}{}",
                       rpb::pair_complete(bits) ? "pair complete" : "pair INCOMPLETE",
                       (bits & rpb::kPairFar) ? "yes" : "no", (bits & rpb::kPairFarOk) ? "yes" : "no", (w >> 16) & 0xFF,
                       far_vt, static_cast<uint32_t>(w >> 32), (bits & rpb::kPairNear) ? "yes" : "no",
                       (bits & rpb::kPairLink) ? "on" : "off", (bits & rpb::kPairRead) ? "" : " (a read failed)");
}

struct State {
    rpb::DeviceTable<64> devices;
    uint32_t players[32] = {};
    size_t players_n = 0;
    void add_player(uint32_t k) {
        if (!rpb::player_key(k)) return;
        for (size_t i = 0; i < players_n; ++i) {
            if (players[i] == k) return;
        }
        if (players_n < std::size(players)) players[players_n++] = k;
    }
};

void log_event(const p8p::Event& e, State& st, uint64_t& lines, uint64_t& not_logged) {
    const auto may_log = [&] {
        if (lines++ < kEventLines) return true;
        ++not_logged;
        return false;
    };
    switch (e.kind) {
        case kEvTree: {
            const auto key = static_cast<uint32_t>(e.v[0]);
            st.add_player(key);
            if (!may_log()) return;
            log::info("RopeProbe #{} TreePulling vf1 {}: character 0x{:08X} ({}) | movement-controller timer +0x63C "
                      "{:.3f} -> {:.3f}{} | body pair after: {} | raw: this+8 {:016X} {:016X} {:016X} {:016X}, event "
                      "data {:016X} {:016X}, 4th argument 0x{:X} | thread {}",
                      static_cast<uint32_t>(e.v[11]), p8_when(e.qpc_us), key, ctl_text(e.v[0] >> 32), lo_f(e.v[1]),
                      hi_f(e.v[1]), (e.flags & 1) ? "" : " (no movement controller found)", pair_text(e.v[2], e.v[3]),
                      e.v[4], e.v[5], e.v[6], e.v[7], e.v[8], e.v[9], e.v[10], e.tid);
            return;
        }
        case kEvRunStart:
        case kEvRunEnd: {
            const auto key = static_cast<uint32_t>(e.v[0]);
            st.add_player(key);
            if (!may_log()) return;
            const uint64_t ctl = (e.v[0] >> 32) & 0xFF;
            const std::string who = ctl == 0xFF ? std::string("control unread")
                                    : ctl      ? std::format("controlled HERE (+0xC8 = {})", ctl)
                                               : std::string("REMOTE here");
            if (e.kind == kEvRunStart) {
                log::info("RopeProbe #{} tree-pull run START {}: character 0x{:08X} ({}) | timer {:.3f}, +0x638 {:.3f} | "
                          "body pair: {} | thread {}",
                          static_cast<uint32_t>(e.v[5]), p8_when(e.qpc_us), key, who, lo_f(e.v[1]), hi_f(e.v[1]),
                          pair_text(e.v[2], e.v[3]), e.tid);
            } else {
                log::info("RopeProbe #{} tree-pull run END {}: character 0x{:08X} ({}) | {} frames, {} with a pull "
                          "applied (+0x638 >= 0) | timer {:.3f} | body pair: {}",
                          static_cast<uint32_t>(e.v[5]), p8_when(e.qpc_us), key, who, static_cast<uint32_t>(e.v[4]),
                          static_cast<uint32_t>(e.v[4] >> 32), lo_f(e.v[1]), pair_text(e.v[2], e.v[3]));
            }
            return;
        }
        case kEvDev2005: {
            const auto key = static_cast<uint32_t>(e.v[0]);
            const auto owner = static_cast<uint32_t>(e.v[0] >> 32);
            const auto parent = static_cast<uint32_t>(e.v[1]);
            rpb::Device* d = st.devices.get(key);
            const bool first = d && !d->got_2005;
            if (d) {
                d->owner = owner;
                d->parent = parent;
                d->got_2005 = true;
                d->removed = false;
                if (!d->first_2005_us) d->first_2005_us = e.qpc_us;
            }
            st.add_player(parent);
            if (first && may_log()) {
                log::info("RopeProbe #{} device 2005 RECEIVED {}: device 0x{:08X} docked to character 0x{:08X} ({}) | "
                          "owner +0x14 0x{:08X}",
                          static_cast<uint32_t>(e.v[2]), p8_when(e.qpc_us), key, parent, who_here(parent), owner);
            }
            return;
        }
        case kEvDevSent: {
            const auto key = static_cast<uint32_t>(e.v[0]);
            const auto character = static_cast<uint32_t>(e.v[0] >> 32);
            const auto op = static_cast<uint16_t>(e.v[1] >> 32);
            const auto ev = rpb::dev_event(static_cast<uint32_t>(e.v[1]));
            rpb::Device* d = st.devices.get(key);
            if (!d || !ev) return;
            if (character) d->character = character;
            if (!d->first_event_us) d->first_event_us = e.qpc_us;
            d->last_event_us = e.qpc_us;
            st.add_player(character);
            if (op != rpb::kOpUpdateGimmick) {  // the 3082 copies: counted only
                ++d->sent_3082;
                return;
            }
            const size_t i = static_cast<size_t>(*ev);
            const bool first = d->sent[i] == 0;
            ++d->sent[i];
            const bool key_event =
                *ev == rpb::DevEvent::Deploy || *ev == rpb::DevEvent::Catch || *ev == rpb::DevEvent::Release;
            if ((first || key_event) && may_log()) {
                log::info("RopeProbe #{} device event SENT by this client {}: device 0x{:08X} of character 0x{:08X} ({}) "
                          "| {} | the device's 2005 {} here",
                          static_cast<uint32_t>(e.v[2]), p8_when(e.qpc_us), key, character, who_here(character),
                          rpb::dev_event_name(*ev), d->got_2005 ? "was received" : "NOT received");
            }
            return;
        }
        case kEvDevBranch: {
            if (rpb::Device* d = st.devices.find(static_cast<uint32_t>(e.v[0]))) ++d->sent_branch;
            return;
        }
        case kEvDevRemove: {
            if (rpb::Device* d = st.devices.find(static_cast<uint32_t>(e.v[0]))) d->removed = true;
            return;
        }
        case kEvPlayer: {
            const auto key = static_cast<uint32_t>(e.v[0]);
            st.add_player(key);
            if (may_log()) {
                log::info("RopeProbe #{} player character 0x{:08X} in the server's traffic (op {}) {}: {}",
                          static_cast<uint32_t>(e.v[2]), key, e.v[0] >> 32, p8_when(e.qpc_us), who_here(key));
            }
            return;
        }
        default: return;
    }
}

void log_summary(State& st, uint64_t not_logged) {
    std::string players;
    for (size_t i = 0; i < st.players_n; ++i) {
        const uint32_t k = st.players[i];
        const rpb::KeyCount* c = g_keys.find(k);
        players += std::format("{}0x{:08X} ({}): 0x8A30C0 {} | timer runs {}, running frames {}, applied {}",
                               players.empty() ? "" : "; ", k, who_here(k),
                               c                ? std::format("ran {} times", c->calls.load())
                               : g_keys.full() ? std::string("UNKNOWN (table full)")
                                               : std::string("NEVER ran"),
                               c ? c->runs.load() : 0u, c ? c->running_frames.load() : 0ull,
                               c ? c->applied_frames.load() : 0ull);
    }
    uint64_t player_calls = 0;
    for (size_t i = 0; i < g_keys.size(); ++i) player_calls += g_keys.at(i).calls.load(std::memory_order_relaxed);
    std::string devices;
    for (size_t i = 0; i < st.devices.size(); ++i) {
        const rpb::Device& d = st.devices.at(i);
        const uint32_t ch = d.character ? d.character : d.parent;
        if (!rpb::player_key(ch) && d.sent[0] + d.sent[1] + d.sent[4] == 0) continue;  // NPC devices: counted below
        devices += std::format(
            "{}0x{:08X} docked to 0x{:08X} ({}), owner 0x{:08X}, 2005 {}{}, sent HERE: deploy {} catch {} 9278 {} A55A {} "
            "release {} stow {} (3082 copies {}, branch {})",
            devices.empty() ? "" : "; ", d.key, ch, who_here(ch), d.owner, d.got_2005 ? "received" : "NOT received",
            d.removed ? " (removed)" : "", d.sent[0], d.sent[1], d.sent[2], d.sent[3], d.sent[4], d.sent[5],
            d.sent_3082, d.sent_branch);
    }
    log::info("RopeProbe summary ({}): hooks {} | TreePulling vf1 ran {} times (characters controlled here {}, remote {}) "
              "| 0x8A30C0: players {} calls ({} player characters, table full {}), every other character {} calls | "
              "players: {} | devices ({} known, table full {}; 2005 device lines {}, device events sent {}): {} | lines "
              "not logged {}, ring dropped {}",
              g_guest.load() ? "guest" : "host", g_hooks_on.load() ? "on" : "OFF", g_tree_runs.load(), g_tree_here.load(),
              g_tree_remote.load(), player_calls, g_keys.used(), g_keys.full(), g_other_calls.load(),
              players.empty() ? std::string("none seen yet") : players,
              st.devices.size(), st.devices.full(), g_sniffed_2005_dev.load(), g_sniffed_dev_events.load(),
              devices.empty() ? std::string("none of a player") : devices, not_logged, g_ring.dropped());
}

DWORD WINAPI worker_thread(LPVOID) {
    try {
        game::PlayerReader reader;
        reader.init();
        uint32_t in_world = 0;
        bool hook_tried = false;
        uint64_t lines = 0, not_logged = 0;
        uint64_t last_minute = GetTickCount64();
        State st;
        for (;;) {
            Sleep(kTickMs);
            const uint64_t now = GetTickCount64();
            if (!hook_tried) {
                in_world = reader.read() ? in_world + 1 : 0;
                if (in_world >= kWorldTicks) {
                    hook_tried = true;
                    if (!install_hooks()) {
                        log::error("RopeProbe: the hooks are not in; the tree-bend probe does nothing this run (the "
                                   "device probe from the capture goes on)");
                    }
                    last_minute = now;
                }
            }
            p8p::Event e;
            while (g_ring.pop(e)) log_event(e, st, lines, not_logged);
            if (hook_tried && now - last_minute >= kMinuteMs) {
                last_minute = now;
                log_summary(st, not_logged);
            }
        }
    } catch (...) {
        log::error("RopeProbe: the worker stopped (exception); nothing more is logged (the hooks stay as they are)");
    }
    return 0;
}

}  // namespace

bool rope_probe_pull_hooked() noexcept { return g_hooks_on.load(std::memory_order_relaxed); }

void rope_probe_start(bool on, bool guest) {
    if (!on) return;
    if (g_started.exchange(true)) return;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!build_ok()) {
        log::error("RopeProbe: unsupported game build (not 1.0.0.2976); rope_probe stays off");
        return;
    }
    g_guest.store(guest);
    g_on.store(true);
    // The device part reads the sniffer's capture (session2.cpp's tap fan-out, both roles).
    sniffer_tap_rope_probe();
    log::info("RopeProbe ON ({}): log only. Tree bend: hooks at the entries of pa::ClientFrameEventTreePulling::vf1 "
              "0x6D46D0 and of the per-frame apply 0x8A30C0 once the world is up. Grappling device: the gimmick events "
              "this client sends and the device 2005 it receives, from the sniffer's capture (needs [sniffer] "
              "enabled=1). Nothing of the game is written (docs/research/s25/gancho-cuerda.md section 13)",
              guest ? "guest" : "host");
    if (HANDLE h = CreateThread(nullptr, 0, worker_thread, nullptr, 0, nullptr)) CloseHandle(h);
}

}  // namespace cdmp::session2
