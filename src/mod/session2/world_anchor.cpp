#include "mod/session2/world_anchor.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <format>
#include <optional>

#include "common/clock.h"
#include "mod/game/actor_lookup.h"
#include "mod/game/game_fault.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/guest_lifecycle.h"
#include "mod/session2/server.h"
#include "mod/session2/world_anchor_logic.h"

namespace cdmp::session2 {
namespace {

namespace w = wal;

// Build 1.0.0.2976, checked at configure (docs/research/noche/mundo-invitado.md, "Implementado"): the two writes of
// the native 2245 handler and the collector's read of the slot. Anything else: off.
constexpr uintptr_t kHandlerWrite = 0x29C2915;  // mov byte [rsi+0x2E4], r14b ; mov dword [rsi+0x2E8], ebx
constexpr uint8_t kHandlerWriteBytes[] = {0x44, 0x88, 0xB6, 0xE4, 0x02, 0x00, 0x00, 0x89, 0x9E, 0xE8, 0x02, 0x00, 0x00};
constexpr uintptr_t kCollectorRead = 0x2C38E28;  // movzx ebx, byte [r15+0x2E4] (anchor 2 of 0x2C37340)
constexpr uint8_t kCollectorReadBytes[] = {0x41, 0x0F, 0xB6, 0x9F, 0xE4, 0x02, 0x00, 0x00};

uintptr_t g_base = 0;
std::atomic<bool> g_active{false};
std::atomic<uint8_t> g_mode{0};  // w::Mode, fixed at configure

// ---- ServerRunThread only (world_anchor_server_tick, world_anchor_on_server_replaced)
w::Anchor g_anchor;
uint32_t g_epoch = 0;
w::Slot g_last_busy{};
bool g_refused_logged = false;
uint64_t g_lines_stale = 0;
constexpr uint64_t kStaleLines = 16;
// A leave's clear (2437 / 2283 / 2001) tries the field's TryLock for up to this long (SwitchToThread between) before
// its packet is handled: the collector holds the lock shared for its whole pass (every 5 s). Past it the clear is
// pending: every following packet tries again with the same budget until it is done (counted, logged).
constexpr uint64_t kLeaveBudgetUs = 5000;
uint16_t g_leave_pending = 0;     // the leave whose clear missed its own packet (0 = none)
uint64_t g_leave_pending_n = 0;   // packets since then

// ---- published by the server thread, read by the lines (any thread)
std::atomic<uint64_t> c_evals{0}, c_set{0}, c_verified{0}, c_lost{0}, c_busy{0}, c_clear{0}, c_leave_clear{0},
    c_fallback{0}, c_forget{0}, c_retry{0}, c_stale{0}, c_refused{0}, c_would_set{0}, c_would_clear{0}, c_errors{0};
std::atomic<uint64_t> c_leave[3] = {};  // user 2's 2437, 2283, 2001 seen at OnRecvPacket
// A leave's clear not done before its own packet was handled (the lock stayed busy), and cleared at a later packet.
std::atomic<uint64_t> c_leave_missed{0}, c_leave_late{0};
std::atomic<uint16_t> g_v_leave_pending{0};  // g_leave_pending, for the lines
// UpdateSyncSceneObjectOutOfLevelBoundaryReq 2573 reaching the server: [from user 2][the slot held (probe: would be)].
std::atomic<uint64_t> c_2573[2][2] = {};
std::atomic<uintptr_t> g_v_field{0}, g_v_owned{0};
std::atomic<uint64_t> g_v_slot{0};  // on << 32 | key, of user 2's field
std::atomic<uint32_t> g_v_gen{0}, g_v_loads{0}, g_v_armed_after{0};
std::atomic<uint8_t> g_v_why{0}, g_v_same{0};  // same: 0 unknown, 1 the host's own field, 2 another one
std::atomic<uint32_t> g_host_char{0}, g_user2_char{0}, g_user2_cur{0};

// ---- the probe: take_frame's threads and the minute's line; its own lock (a leaf: nothing is taken under it)
SRWLOCK g_stats_lock = SRWLOCK_INIT;
w::RemovalStats g_stats;  // under g_stats_lock
struct Players {
    uint64_t at = 0;
    bool host_ok = false, u2_ok = false;
    float host[3] = {}, u2[3] = {};
};
Players g_players;  // under g_stats_lock
constexpr uint64_t kPlayersMs = 1000;
constexpr uint32_t kLookupsPerSecond = 64;
std::atomic<uint64_t> g_budget_second{0};
std::atomic<uint32_t> g_budget_used{0};
std::atomic<uint64_t> c_over_budget{0};
uint64_t g_last_minute = 0;  // network thread
std::atomic<uint64_t> c_pos_origin{0};  // positions at the origin (a seated rider): taken as unknown

std::string slot_text(const w::Slot& s) { return std::format("{:02X}/{:08X}", unsigned{s.on}, s.key); }
std::string slot_text(uint64_t packed) {
    return slot_text(w::Slot{static_cast<uint8_t>(packed >> 32), static_cast<uint32_t>(packed)});
}

size_t leave_index(uint16_t op) {
    return op == w::kOpGameLoadingStartReq ? 0 : op == w::kOpReloadStageStartReq ? 1 : 2;
}

// ---------------------------------------------------------------- guarded reads (SEH; ServerRunThread hot path)

template <class T>
std::optional<T> rd(uintptr_t a) {
    if (!a) return std::nullopt;
    return game::fast<T>(a);
}
std::optional<uintptr_t> rp(uintptr_t a) {
    if (!a) return std::nullopt;
    return game::fast_ptr(a);
}

// A ServerField (its vtable checked), all or nothing.
w::FieldView read_field(uintptr_t f) {
    w::FieldView v;
    const auto vt = rp(f);
    if (!vt || *vt != g_base + w::kVtServerField) return v;
    const auto open = rd<uint8_t>(f + w::kFieldOpen);
    const auto owner = rd<uint32_t>(f + w::kFieldOwner);
    const auto gen = rd<uint32_t>(f + w::kFieldGen);
    const auto on = rd<uint8_t>(f + w::kFieldSlotOn);
    const auto key = rd<uint32_t>(f + w::kFieldSlotKey);
    if (!open || !owner || !gen || !on || !key) return v;
    v.ok = true;
    v.field = f;
    v.gen = *gen;
    v.open = *open;
    v.owner = *owner;
    v.slot = w::Slot{*on, *key};
    return v;
}

// The field a server character stands in: [[[pj+0x68]+0x1A0]+0x4F0+0x10] (field_share.h).
uintptr_t character_field(uintptr_t pj) {
    const auto comps = rp(pj + w::kActorComponents);
    const auto ts = comps ? rp(*comps + w::kComponentsTs) : std::nullopt;
    const auto f = ts ? rp(*ts + w::kTsField) : std::nullopt;
    return f.value_or(0);
}

// [user+slot] (a character) and its key; 0 = none.
uint32_t character_key(uintptr_t user, uint32_t slot, uintptr_t* pj_out) {
    const auto pj = rp(user + slot);
    if (!pj) return 0;
    if (pj_out) *pj_out = *pj;
    return rd<uint32_t>(*pj + w::kActorKey).value_or(0);
}

w::Inputs read_inputs(uint64_t now, uintptr_t& host_field) {
    w::Inputs in;
    in.now_ms = now;
    host_field = 0;
    uintptr_t host = 0;
    if (server_host_user(host) && host) {
        in.host_user = rd<uint32_t>(host + w::kUserKey).value_or(0);
        uintptr_t hpj = 0;
        if (const uint32_t k = character_key(host, w::kUserMainCharacter, &hpj)) g_host_char.store(k, std::memory_order_relaxed);
        if (hpj) host_field = character_field(hpj);
    }
    in.logout = server_logout_requested();
    const MovePhaseTracker t = server_user2_phase();
    in.stable = t.phase() == MovePhase::Stable;
    in.loads = t.count(mv::kLoadingCompleteAck);
    uintptr_t u2 = 0;
    if (server_user2(u2) && u2) {
        in.user2 = true;
        uintptr_t pj = 0;
        const uint32_t k = character_key(u2, w::kUserMainCharacter, &pj);
        // The key user 2's own MainPlayerDataStartAck named (E4), checked against [[user 2+0xD0]+0x60].
        if (k && k == server_user2_player_key()) in.key = k;
        g_user2_char.store(k, std::memory_order_relaxed);
        g_user2_cur.store(character_key(u2, w::kUserCurrentCharacter, nullptr), std::memory_order_relaxed);
        if (pj) in.f2 = read_field(character_field(pj));
    }
    if (const auto& o = g_anchor.owned()) in.owned = read_field(o->field);
    return in;
}

// ---------------------------------------------------------------- the write (ServerRunThread)

using TryFn = bool(__fastcall*)(uintptr_t lock, uint8_t shared);
using UnlockFn = void(__fastcall*)(uintptr_t lock, uint8_t shared);

// The field's lock object [field+8] and its TryLock / Unlock, checked against build 2976 (guarded reads).
// Read before the lock on purpose: fields are reset off ServerRunThread too (Pool<ServerField>::Acquire on the thread
// that moves a user, -> Reset 0x2AF7360), but never freed while their server lives (pooled, reused in place; the only
// free is the deleting destructor vf0 0x2AF6A40, with no direct caller), and [field+8] = field+0x1B8 is written once
// by the constructor (0x1434920) and never by Reset. Open / generation are then checked under the lock.
bool lock_of(uintptr_t field, uintptr_t& lock, uintptr_t& fn_try, uintptr_t& fn_unlock) {
    const auto l = rp(field + w::kFieldLock);
    const auto vt = l ? rp(*l) : std::nullopt;
    const auto t = vt ? rp(*vt + w::kLockSlotTry) : std::nullopt;
    const auto u = vt ? rp(*vt + w::kLockSlotUnlock) : std::nullopt;
    if (!vt || *vt != g_base + w::kVtFieldLock || !t || *t != g_base + w::kLockTry || !u ||
        *u != g_base + w::kLockUnlock) {
        return false;
    }
    lock = *l;
    fn_try = *t;
    fn_unlock = *u;
    return true;
}

// Inside the game's code (its lock): an exception is passed on (game_fault.h), never handled here.
bool call_try(uintptr_t fn, uintptr_t lock) noexcept {
    __try {
        return reinterpret_cast<TryFn>(fn)(lock, 0);
    } __except (game::game_fault_continue(GetExceptionInformation(), "WorldAnchor",
                                          "TryLock (exclusive) of a ServerField's lock (vf+0x28, exe+0x1371BA0)")) {
        return false;  // never reached: the filter passes it on
    }
}
void call_unlock(uintptr_t fn, uintptr_t lock) noexcept {
    __try {
        reinterpret_cast<UnlockFn>(fn)(lock, 0);
    } __except (game::game_fault_continue(GetExceptionInformation(), "WorldAnchor",
                                          "Unlock (exclusive) of a ServerField's lock (vf+0x20, exe+0x1371B70)")) {
    }
}

// Under the field's lock: our own reads and writes of the slot (an access violation here is ours: handled).
// set: {1, key}, only while the field is open with generation `gen`, unless the slot holds a key that is neither `key`
// nor one of `ours`. clear: {0, 0}, only while it holds {1, key} with generation `gen` (open or already closed: a
// closed field keeps its slot until the pool hands it out again).
w::Result write_locked(uintptr_t f, uint32_t gen, bool set, uint32_t key, const uint32_t* ours, size_t n_ours,
                       uint8_t& found_on, uint32_t& found_key) noexcept {
    __try {
        auto* const open = reinterpret_cast<volatile uint8_t*>(f + w::kFieldOpen);
        auto* const fgen = reinterpret_cast<volatile uint32_t*>(f + w::kFieldGen);
        auto* const on = reinterpret_cast<volatile uint8_t*>(f + w::kFieldSlotOn);
        auto* const k = reinterpret_cast<volatile uint32_t*>(f + w::kFieldSlotKey);
        found_on = *on;
        found_key = *k;
        if (*fgen != gen || (set && *open != 1)) return w::Result::Stale;
        if (set) {
            bool foreign = found_on && found_key && found_key != key;
            for (size_t i = 0; foreign && i < n_ours; ++i) {
                if (ours[i] == found_key) foreign = false;
            }
            if (foreign) return w::Result::Stale;
            *k = key;  // the handler writes the byte first; under the exclusive lock the order is not seen
            *on = 1;
        } else {
            if (found_on != 1 || found_key != key) return w::Result::Stale;
            *on = 0;
            *k = 0;
        }
        return w::Result::Written;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return w::Result::Stale;
    }
}

// TryLock (once, or again until `budget_us` has passed), the write, Unlock. `refused`: the field's lock is not the
// expected object.
w::Result write_slot(const w::Decision& d, bool set, uint64_t budget_us, w::Slot& found, bool& refused) {
    refused = false;
    const auto vt = rp(d.field);
    if (!vt || *vt != g_base + w::kVtServerField) return w::Result::Stale;
    uintptr_t lock = 0, fn_try = 0, fn_unlock = 0;
    if (!lock_of(d.field, lock, fn_try, fn_unlock)) {
        refused = true;
        return w::Result::Stale;
    }
    const uint64_t start = cdmp::now_us();
    bool locked = false;
    for (;;) {
        if (call_try(fn_try, lock)) {
            locked = true;
            break;
        }
        if (!budget_us || cdmp::now_us() - start >= budget_us) break;
        SwitchToThread();
    }
    if (!locked) return w::Result::Retry;
    const auto& ours = g_anchor.our_keys();
    const w::Result r = write_locked(d.field, d.gen, set, d.key, ours.data(), ours.size(), found.on, found.key);
    call_unlock(fn_unlock, lock);
    return r;
}

void note_result(const w::Decision& d, w::Result r, bool refused, const w::Slot& found) {
    if (r == w::Result::Retry) {
        c_retry.fetch_add(1, std::memory_order_relaxed);
        if (d.leave_op) {
            c_leave_missed.fetch_add(1, std::memory_order_relaxed);
            log::warn("WorldAnchor [clear] before {}: the field's lock stayed busy for {} ms; its handler may run with the "
                      "slot still set: tried again at every following packet until cleared",
                      d.leave_op, kLeaveBudgetUs / 1000);
        }
        return;
    }
    if (r != w::Result::Stale) return;
    c_stale.fetch_add(1, std::memory_order_relaxed);
    if (refused) {
        c_refused.fetch_add(1, std::memory_order_relaxed);
        if (!g_refused_logged) {
            g_refused_logged = true;
            log::error("WorldAnchor: the lock of field 0x{:X} is not pa::WindowsRWLock<4,1> with TryLock exe+0x1371BA0 / "
                       "Unlock exe+0x1371B70 (build 1.0.0.2976 only): nothing written",
                       d.field);
        }
        return;
    }
    if (g_lines_stale++ < kStaleLines) {
        log::info("WorldAnchor [{}] not written: field 0x{:X} is no longer open with gen 0x{:08X}, or its slot holds {} "
                  "(key 0x{:08X})",
                  w::act_name(d.act), d.field, d.gen, slot_text(found), d.key);
    }
}

void execute(const w::Decision& d, uint64_t now, uintptr_t host_field) {
    const bool probe = g_anchor.probe();
    switch (d.act) {
        case w::Act::None:
            if (d.verified) {
                c_verified.fetch_add(1, std::memory_order_relaxed);
                log::info("WorldAnchor [verify] slot 01/{:08X} ok after {} ms (field 0x{:X} gen 0x{:08X})", d.key,
                          d.since_ms, d.field, d.gen);
            }
            if (d.why == w::Why::Busy && d.found != g_last_busy) {
                g_last_busy = d.found;
                c_busy.fetch_add(1, std::memory_order_relaxed);
                log::warn("WorldAnchor [busy] the slot of field 0x{:X} holds {} (not user 2's 0x{:08X} nor a key of ours: "
                          "a native world observer of the host?): left as it is",
                          d.field, slot_text(d.found), d.key);
            }
            return;
        case w::Act::Forget:
            g_leave_pending = 0;  // nothing of ours is held any more
            c_forget.fetch_add(1, std::memory_order_relaxed);
            log::info("WorldAnchor [forget] field 0x{:X} gen 0x{:08X} key 0x{:08X}: {} (slot now {}); nothing written",
                      d.field, d.gen, d.key, w::why_name(d.why), slot_text(d.found));
            return;
        case w::Act::Set: {
            if (probe) {
                c_would_set.fetch_add(1, std::memory_order_relaxed);
                g_anchor.applied(d, w::Result::Written, now);
                log::info("WorldAnchor [probe] would set key 0x{:08X} field 0x{:X} gen 0x{:08X} ({}; slot now {}; user 2's "
                          "current character 0x{:08X}; the host's own field: {})",
                          d.key, d.field, d.gen, w::why_name(d.why), slot_text(d.found),
                          g_user2_cur.load(std::memory_order_relaxed), host_field == d.field ? "the same" : "another");
                return;
            }
            w::Slot found;
            bool refused = false;
            const w::Result r = write_slot(d, true, 0, found, refused);
            g_anchor.applied(d, r, now);
            note_result(d, r, refused, found);
            if (r != w::Result::Written) return;
            g_last_busy = {};
            if (d.why == w::Why::Lost) {
                const uint64_t n = c_lost.fetch_add(1, std::memory_order_relaxed) + 1;
                log::warn("WorldAnchor [lost] slot {} of field 0x{:X} while user 2 is in it -> set again #{} (key 0x{:08X})",
                          slot_text(d.found), d.field, n, d.key);
            } else {
                c_set.fetch_add(1, std::memory_order_relaxed);
                log::info("WorldAnchor [set] key 0x{:08X} field 0x{:X} gen 0x{:08X} slot was {} (user 2's current "
                          "character 0x{:08X}; the host's own field: {})",
                          d.key, d.field, d.gen, slot_text(found), g_user2_cur.load(std::memory_order_relaxed),
                          host_field == d.field ? "the same" : "another");
            }
            return;
        }
        case w::Act::Clear: {
            const std::string why =
                d.leave_op ? std::format("before {}", d.leave_op) : std::string(w::why_name(d.why));
            if (probe) {
                c_would_clear.fetch_add(1, std::memory_order_relaxed);
                g_anchor.applied(d, w::Result::Written, now);
                log::info("WorldAnchor [probe] would clear field 0x{:X} key 0x{:08X} ({}{})", d.field, d.key, why,
                          d.fallback ? ": the fallback, no 2001 reached the server" : "");
                return;
            }
            w::Slot found;
            bool refused = false;
            // A leave, or the clear of a leave that missed its packet: the leave's budget; any other clear: one try.
            const bool leave_budget = d.leave_op || g_leave_pending;
            const w::Result r = write_slot(d, false, leave_budget ? kLeaveBudgetUs : 0, found, refused);
            g_anchor.applied(d, r, now);
            note_result(d, r, refused, found);
            if (r == w::Result::Retry) {
                if (d.leave_op) {
                    g_leave_pending = d.leave_op;
                    g_leave_pending_n = 0;
                }
                return;
            }
            const uint16_t late = g_leave_pending;
            g_leave_pending = 0;  // Written or Stale: nothing of ours is left to clear
            if (r != w::Result::Written) return;
            if (late && !d.leave_op) {
                c_leave_late.fetch_add(1, std::memory_order_relaxed);
                log::info("WorldAnchor [clear] after {} ({} packets later: its lock was busy at its own; field 0x{:X} gen "
                          "0x{:08X} key 0x{:08X})",
                          late, g_leave_pending_n, d.field, d.gen, d.key);
            } else if (d.leave_op) {
                c_leave_clear.fetch_add(1, std::memory_order_relaxed);
                log::info("WorldAnchor [clear] before {} (field 0x{:X} gen 0x{:08X} key 0x{:08X})", d.leave_op, d.field,
                          d.gen, d.key);
            } else if (d.fallback) {
                c_fallback.fetch_add(1, std::memory_order_relaxed);
                log::info("WorldAnchor [fallback clear] user 2 is gone and no 2001 of it cleared the slot: field 0x{:X} "
                          "gen 0x{:08X} key 0x{:08X} cleared",
                          d.field, d.gen, d.key);
            } else {
                c_clear.fetch_add(1, std::memory_order_relaxed);
                log::info("WorldAnchor [clear] {} (field 0x{:X} gen 0x{:08X} key 0x{:08X})", why, d.field, d.gen, d.key);
            }
            return;
        }
    }
}

void publish(const w::Inputs& in, uintptr_t host_field) {
    g_v_field.store(in.f2.ok ? in.f2.field : 0, std::memory_order_relaxed);
    g_v_slot.store(in.f2.ok ? (uint64_t{in.f2.slot.on} << 32 | in.f2.slot.key) : 0, std::memory_order_relaxed);
    g_v_gen.store(in.f2.gen, std::memory_order_relaxed);
    g_v_loads.store(in.loads, std::memory_order_relaxed);
    g_v_armed_after.store(g_anchor.armed_after(), std::memory_order_relaxed);
    g_v_why.store(static_cast<uint8_t>(g_anchor.wanted(in)), std::memory_order_relaxed);
    g_v_same.store(!host_field || !in.f2.ok ? 0 : host_field == in.f2.field ? 1 : 2, std::memory_order_relaxed);
    const auto& o = g_anchor.owned();
    g_v_owned.store(o ? o->field : 0, std::memory_order_relaxed);
}

void tick_body(uint16_t opcode, bool from_user2) {
    if (GetCurrentThreadId() != server_thread_id()) return;  // ServerRunThread only
    if (opcode == w::kOpOutOfLevelBoundaryReq) {
        c_2573[from_user2 ? 1 : 0][g_anchor.owned() ? 1 : 0].fetch_add(1, std::memory_order_relaxed);
    }
    if (g_leave_pending) ++g_leave_pending_n;
    const uint64_t now = GetTickCount64();
    if (const uint32_t e = server_epoch(); e != g_epoch) {
        g_epoch = e;
        g_anchor.on_epoch();
    }
    if (from_user2 && w::leaves_field(opcode)) {
        c_leave[leave_index(opcode)].fetch_add(1, std::memory_order_relaxed);
        const MovePhaseTracker t = server_user2_phase();
        const w::Decision d = g_anchor.on_leave(opcode, t.count(mv::kLoadingCompleteAck), now);
        if (d.act == w::Act::None) {
            log::info("WorldAnchor [leave] user 2's {} reaches the server: nothing {}set; the next set waits for a 2762 "
                      "after its {}",
                      opcode, g_anchor.probe() ? "(would be) " : "", t.count(mv::kLoadingCompleteAck));
        } else {
            execute(d, now, 0);
            // Its lock stayed busy for the whole budget: the next packet tries again (not this one twice).
            if (g_leave_pending) {
                g_v_leave_pending.store(g_leave_pending, std::memory_order_relaxed);
                return;
            }
        }
    }
    if (g_anchor.due(now)) {
        c_evals.fetch_add(1, std::memory_order_relaxed);
        uintptr_t host_field = 0;
        const w::Inputs in = read_inputs(now, host_field);
        const w::Decision d = g_anchor.step(in);
        execute(d, now, host_field);
        publish(in, host_field);
    }
    g_v_leave_pending.store(g_leave_pending, std::memory_order_relaxed);
}

// ---------------------------------------------------------------- the probe (take_frame's threads)

bool take_budget(uint64_t now) {
    const uint64_t second = now / 1000;
    uint64_t cur = g_budget_second.load(std::memory_order_relaxed);
    if (cur != second && g_budget_second.compare_exchange_strong(cur, second, std::memory_order_relaxed)) {
        g_budget_used.store(0, std::memory_order_relaxed);
    }
    return g_budget_used.fetch_add(1, std::memory_order_relaxed) < kLookupsPerSecond;
}

bool position(uint32_t key, float (&out)[3]) {
    if (!key) return false;
    const auto p = game::actor_world_position(key);  // the host client's actor manager, guarded reads
    if (!p) return false;
    // S25 (docs/research/s25/mundo-teleport.md): a seated rider's TransformSync is relative to its mount (about 0,0,0),
    // which printed "host - user 2 now 11425 m" (user 2's distance from the origin). Unknown, never a distance.
    if (p->x * p->x + p->y * p->y + p->z * p->z < 1.0f) {
        c_pos_origin.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    out[0] = p->x;
    out[1] = p->y;
    out[2] = p->z;
    return true;
}

Players players(uint64_t now) {
    AcquireSRWLockShared(&g_stats_lock);
    Players p = g_players;
    ReleaseSRWLockShared(&g_stats_lock);
    if (p.at && now - p.at < kPlayersMs) return p;
    p = Players{};
    p.at = now ? now : 1;
    p.host_ok = position(g_host_char.load(std::memory_order_relaxed), p.host);
    p.u2_ok = position(server_user2_player_key(), p.u2);
    AcquireSRWLockExclusive(&g_stats_lock);
    g_players = p;
    ReleaseSRWLockExclusive(&g_stats_lock);
    return p;
}

}  // namespace

bool world_anchor_configure(const std::string& raw) {
    const auto p = w::parse_mode(raw);
    if (!p.known) log::warn("Session2: user2_world_anchor='{}' is not 0, probe or 1; off", raw);
    if (p.mode == w::Mode::Off) return false;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint8_t a[sizeof kHandlerWriteBytes] = {}, b[sizeof kCollectorReadBytes] = {};
    if (!game::safe_read(g_base + kHandlerWrite, a, sizeof a) || std::memcmp(a, kHandlerWriteBytes, sizeof a) != 0 ||
        !game::safe_read(g_base + kCollectorRead, b, sizeof b) || std::memcmp(b, kCollectorReadBytes, sizeof b) != 0) {
        log::error("Session2: user2_world_anchor: the slot's writer exe+0x29C2915 / reader exe+0x2C38E28 are not the bytes "
                   "of build 1.0.0.2976; off");
        return false;
    }
    g_anchor = w::Anchor(p.mode);
    g_mode.store(static_cast<uint8_t>(p.mode), std::memory_order_relaxed);
    g_active.store(true, std::memory_order_release);
    log::info("WorldAnchor ON ({}): {} (field +0x2E4/+0x2E8 of a field the host owns; set after user 2's 2762, cleared at "
              "its 2437/2283/2001 and when it is gone; read back every {} s) | the collector's reason-25 RemoveActorAck to "
              "user 2 measured (positions from this client), a 'WorldAnchor [minute]' line every 60 s",
              w::mode_name(p.mode),
              p.mode == w::Mode::On ? "user 2's main character is the field's world-observer anchor, written on "
                                      "ServerRunThread under the field's lock (TryLock)"
                                    : "decisions logged as 'would', the slot only read",
              w::kWatchdogMs / 1000);
    return true;
}

bool world_anchor_active() { return g_active.load(std::memory_order_relaxed); }

bool world_anchor_on() {
    return g_active.load(std::memory_order_relaxed) &&
           static_cast<w::Mode>(g_mode.load(std::memory_order_relaxed)) == w::Mode::On;
}

void world_anchor_server_tick(uint16_t opcode, bool from_user2) noexcept {
    if (!g_active.load(std::memory_order_acquire)) return;
    try {
        tick_body(opcode, from_user2);
    } catch (...) {
        c_errors.fetch_add(1, std::memory_order_relaxed);
    }
}

void world_anchor_on_server_replaced() noexcept {
    if (!g_active.load(std::memory_order_acquire)) return;
    g_anchor.forget();
    g_leave_pending = 0;
    g_v_leave_pending.store(0, std::memory_order_relaxed);
    g_v_owned.store(0, std::memory_order_relaxed);
}

void world_anchor_on_user2_out(uint16_t opcode, std::span<const uint8_t> payload) noexcept {
    if (opcode != w::kOpRemoveActorAck || !g_active.load(std::memory_order_relaxed)) return;
    try {
        const w::Removal r = w::read_remove(payload);
        if (!r.ok) return;
        if (r.reason != w::kReasonCollector) {
            AcquireSRWLockExclusive(&g_stats_lock);
            g_stats.other_reason();
            ReleaseSRWLockExclusive(&g_stats_lock);
            return;
        }
        const uint64_t now = GetTickCount64();
        const Players p = players(now);
        for (size_t i = 0; i < r.total; ++i) {  // every key (the lookups are bounded by the budget, not the count)
            const uint32_t key = r.key(i);
            if ((key >> 28) != 0xB) continue;  // NPCs, animals and objects only
            float npc[3] = {};
            bool ok = false;
            if (take_budget(now)) {
                ok = position(key, npc);  // still in this client: its own 2516 is handled later
            } else {
                c_over_budget.fetch_add(1, std::memory_order_relaxed);
            }
            AcquireSRWLockExclusive(&g_stats_lock);
            g_stats.add(now, ok ? &npc : nullptr, p.host_ok ? &p.host : nullptr, p.u2_ok ? &p.u2 : nullptr);
            ReleaseSRWLockExclusive(&g_stats_lock);
        }
    } catch (...) {
        c_errors.fetch_add(1, std::memory_order_relaxed);
    }
}

std::string world_anchor_describe() {
    if (!g_active.load(std::memory_order_relaxed)) return {};
    const uint8_t same = g_v_same.load(std::memory_order_relaxed);
    const auto why = static_cast<w::Why>(g_v_why.load(std::memory_order_relaxed));
    const uintptr_t owned = g_v_owned.load(std::memory_order_relaxed);
    return std::format(
        "user2_world_anchor {}: user 2 0x{:08X} (current 0x{:08X}) in field 0x{:X} gen 0x{:08X} ({}) slot {} | 2762s {} "
        "(a set needs > {}) | now: {} | held: {} | set {} verified {} lost {} busy {} clear {} before a leave {} "
        "(2437 {}, 2283 {}, 2001 {} seen; missed at its packet {}, cleared later {}{}) fallback {} forget {} | lock busy "
        "{} not written {} refused {} | would set {} "
        "would clear {} | reads {} errors {} | OutOfLevelBoundaryReq 2573 host/user 2: slot off {}/{}, on {}/{}",
        w::mode_name(static_cast<w::Mode>(g_mode.load(std::memory_order_relaxed))), g_user2_char.load(), g_user2_cur.load(), g_v_field.load(), g_v_gen.load(),
        same == 1 ? "the host's own" : same == 2 ? "not the host's current one" : "host's field unknown",
        slot_text(g_v_slot.load()), g_v_loads.load(), g_v_armed_after.load(), w::why_name(why),
        owned ? std::format("field 0x{:X}", owned) : std::string("nothing"), c_set.load(), c_verified.load(),
        c_lost.load(), c_busy.load(), c_clear.load(), c_leave_clear.load(), c_leave[0].load(), c_leave[1].load(),
        c_leave[2].load(), c_leave_missed.load(), c_leave_late.load(),
        g_v_leave_pending.load() ? ", ONE STILL PENDING" : "", c_fallback.load(), c_forget.load(),
        c_retry.load(), c_stale.load(), c_refused.load(),
        c_would_set.load(), c_would_clear.load(), c_evals.load(), c_errors.load(), c_2573[0][0].load(),
        c_2573[1][0].load(), c_2573[0][1].load(), c_2573[1][1].load());
}

void world_anchor_tick() {
    if (!g_active.load(std::memory_order_relaxed)) return;
    const uint64_t now = GetTickCount64();
    if (!g_last_minute) {
        g_last_minute = now;
        return;
    }
    if (now - g_last_minute < 60000) return;
    g_last_minute = now;
    w::RemovalStats::Summary s;
    AcquireSRWLockExclusive(&g_stats_lock);
    s = g_stats.take();
    ReleaseSRWLockExclusive(&g_stats_lock);
    std::string apart = "unknown";
    float h[3] = {}, u[3] = {};
    if (position(g_host_char.load(), h) && position(server_user2_player_key(), u)) {
        apart = std::format("{:.0f} m", w::dist2d(h, u));
    }
    const std::string near_text =
        s.near_user2 ? std::format(": d(host) min {:.0f} / median {:.0f} / max {:.0f} m, d(user 2) max {:.0f} m",
                                   s.near_host_min, s.near_host_med, s.near_host_max, s.near_u2_max)
                     : std::string();
    log::info("WorldAnchor [minute] {} | collector reason 25 to user 2: {} removals in {} passes (position unknown {}, "
              "over budget {}), near user 2 (< {:.0f} m, 2D) {}, of them beyond {:.0f} m from the host {}{} | median "
              "d(host) of all {:.0f} m | other reasons {} | host - user 2 now {}, max with a removal {:.0f} m | "
              "player positions at the origin (riding: unknown) {}",
              world_anchor_describe(), s.removals, s.passes, s.unresolved, c_over_budget.exchange(0), w::kNearM,
              s.near_user2, w::kNearM, s.near_far_host, near_text, s.all_host_med, s.other_reason, apart, s.players_max,
              c_pos_origin.exchange(0));
}

}  // namespace cdmp::session2
