#pragma once
// S37 phase D (docs/research/s25/s37-fase-d.md; 4-contencion-y-relevo.md §2/§4 #3; AUDITORIA.md §4 "rearmado";
// INFORME-A4 "Rearmado rápido"): the pure logic of [session2] user2_fast_rearm = 0 | probe | 1, HOST only, build
// 1.0.0.2976. No game access; the tests run it. The glue is user2_stages.cpp.
//
// The "ghost city" (S34 1b): when the host fast-travels out of the guest's town, U-1 re-arms user 2's manager after
// 10.8 s of arm hysteresis (>= 10 cells for 10 s) plus 10.05 s waiting for a 2043 queued with a whole period
// (EnqueueTimer delay 100), ~21.4 s from the last reason-19 close to the first 2005 of user 2's stages.
//
// Two parts, one key (the audit: never several new policies at once):
//  (a) the FAST path: skip the hysteresis only for a genuine departure of the HOST, never for walking past the
//      threshold. The audit refuted "a <= 7 -> >= 10 jump proves a fast travel" ([F]): the distance alone does not say
//      who moved, nor rule out a correction, a stale read, a field change or a skipped sample. So a jump counts only
//      with ALL of: two consecutive valid samples (<= 2.5 s apart) in the same field and the same user 2 character;
//      the host's cell moved by at least on - off cells while user 2's moved at most 1; the host's own load step (L,
//      0x287DA50 on its manager: SendMoveFieldAck's owner step) seen in the last 30 s AND after the start of the near
//      streak the jump ends (review 3a: never its arrival's L); U-1 not armed, its machine OFF, no request / drain /
//      held drain / g_p_draining, user 2 not loading nor leaving; and no fast arm in the last 60 s. Then the candidate
//      is confirmed over 3 s more of far samples with user 2 still and no load of user 2 (review 3b: a guest that
//      follows the host drops it before anything is asked). Anything else keeps the hysteresis.
//  (b) the ONE-SHOT 2043 at a fast arm: the native shape of 0x287EF60's "not ready" timer (LAB_14287F254:
//      payload +0xB = 0, Ghidra 0x287ef60.c:44-88: local_2fd = 0 at :75, delay 10 at :85) with delay 1 (100 ms)
//      instead of its 10, and WITHOUT its +0x68A = 1 (:50; that flag makes the tick prepare again, 0x287dbb0.c:148:
//      ours must only evaluate). No new RVA: the same EnqueueTimer 0x2774430, Troc slot and zero cell as the chain.
//      0x287DBB0 reschedules only when param_3 (+0xB) != 0 (0x287dbb0.c:118-140), so a one-shot can never start a
//      chain. It does NOT replace the periodic chain: arm_locked keeps its dead/ended repair and chain_kick_due; the
//      one-shot's tick is not a chain tick (g_last_mgr2_tick_us / g_chain_last_us untouched): it never masks a dead
//      chain.
//      One pending per arm generation; queue result checked; consumed / cancelled (drain, logout, new character,
//      re-arm) / lost (no tick in 5 s) all traced. The cancel is LOGICAL: the game's timer stays queued (at most
//      100 ms) and, if it fires after a drain, 0x287DBB0 does what the manager's state says, as a chain tick would.
//      An exception inside EnqueueTimer is not handled here: call_fault returns EXCEPTION_CONTINUE_SEARCH (game_fault),
//      so it is the game's crash, exactly as for the chain's own EnqueueTimer (same function, same arguments).

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "mod/session2/user2_stages_logic.h"

namespace cdmp::session2::frm {

using u2s::Cell;
using u2s::Mode;

// ---------------------------------------------------------------- parameters (experimental, not engine laws)

inline constexpr uint64_t kMaxSampleGapUs = 2'500'000;     // the module samples every 1 s: two consecutive ones
inline constexpr uint64_t kHostLoadWindowUs = 30'000'000;  // the host's L before the far sample
inline constexpr uint64_t kMinGapUs = 60'000'000;          // never two fast arms within 60 s
inline constexpr uint32_t kGuestStillCells = 1;            // user 2 did not move (the host did)
// Review (fase-d-revision 3b, "the guest follows the host"): a candidate is confirmed only after kConfirmUs more of
// valid far samples with user 2 still and no load of user 2 started since the jump (a guest that fast-travels after
// the host within that window cancels it cleanly, before anything is asked). A guest load within kGuestFollowUs of a
// fast arm (or would) is counted as "followed by the guest" (the cost: stages created then drained by its load).
inline constexpr uint64_t kConfirmUs = 3'000'000;
inline constexpr uint64_t kGuestFollowUs = 30'000'000;
// EnqueueTimer's delay argument counts 100 ms units: 0x287EF60's periodic delay is raw * 1000 / 100 (raw = 10 ->
// 100) and the measured period is ~10.1 s (S30/S31/S33). The 53d70df bug took that argument for milliseconds.
inline constexpr uint64_t kDelayUnitMs = 100;
inline constexpr uint32_t kOneShotDelay = 1;               // 100 ms
inline constexpr uint64_t kOneShotLostUs = 5'000'000;      // a pending one-shot with no tick in 5 s: lost
inline constexpr uint32_t kOneShotCap = 64;                // per run: bounded whatever happens
inline constexpr uint64_t delay_ms(uint32_t delay_arg) { return static_cast<uint64_t>(delay_arg) * kDelayUnitMs; }

// ---------------------------------------------------------------- the samples

struct Sample {
    bool valid = false;  // both positions read, both players in the same ServerField, user 2's character known
    uint64_t t_us = 0;
    uint32_t dist = 0;   // Chebyshev cells (valid only)
    Cell host{}, user2{};
    uintptr_t field = 0;    // the shared field (identity only, never dereferenced here)
    uintptr_t u2_char = 0;  // user 2's character (its epoch)
};

// The last kHistory samples, oldest first on read (module thread only).
inline constexpr size_t kHistory = 8;
class History {
public:
    void push(const Sample& s) {
        ring_[n_ % kHistory] = s;
        ++n_;
    }
    void clear() { n_ = 0; }
    size_t size() const { return n_ < kHistory ? static_cast<size_t>(n_) : kHistory; }
    // i = 0 the oldest kept, size() - 1 the last.
    const Sample& at(size_t i) const { return ring_[(n_ - size() + i) % kHistory]; }
    Sample last(size_t back = 0) const { return back < size() ? at(size() - 1 - back) : Sample{}; }

private:
    Sample ring_[kHistory] = {};
    uint64_t n_ = 0;
};

// ---------------------------------------------------------------- (a) the fast verdict

enum class Fast : uint8_t {
    Fast,           // a genuine departure of the host: arm without the hysteresis
    NoJump,         // the last two samples are not <= off -> >= on (the normal case: not counted)
    NotConsecutive, // a sample missing / invalid in between, or more than 2.5 s apart
    OtherField,     // not the same field in both samples
    OtherEpoch,     // user 2's character changed
    GuestMoved,     // user 2's cell moved more than 1 cell (it moved, or both did)
    HostStill,      // the host's cell did not move by on - off cells (a reading artefact)
    NoHostLoad,     // no load step (L) of the host in the last 30 s: not its fast travel / load
    LoadBeforeNear, // review 3a: that L is not after the start of the current near streak (the host's ARRIVAL)
    Armed,          // U-1 already armed: nothing to do
    MachineOn,      // the hysteresis already says ON
    Aborted,        // U-1 aborted
    Pending,        // a request (arm / drain) is pending
    DrainHold,      // a distance drain held for the host's preparation (user2_drain_at_host_prepare)
    Draining,       // g_p_draining is held: a drain at the host's preparation, or npc_activation's cleanup
    GuestLoading,   // user 2 is loading
    Logout,         // user 2's goodbye was asked
    TooSoon,        // a fast arm (or would) in the last 60 s
    Capped,         // kOneShotCap fast arms this run
    Count
};
inline const char* fast_name(Fast f) {
    switch (f) {
        case Fast::Fast: return "FAST (the host's own departure)";
        case Fast::NoJump: return "no jump";
        case Fast::NotConsecutive: return "the samples are not consecutive";
        case Fast::OtherField: return "not the same field in both samples";
        case Fast::OtherEpoch: return "user 2's character changed";
        case Fast::GuestMoved: return "user 2 moved (not only the host)";
        case Fast::HostStill: return "the host's cell did not jump";
        case Fast::NoHostLoad: return "no load step (L) of the host in the last 30 s";
        case Fast::LoadBeforeNear:
            return "the host's last L is not after the start of the current near streak (its arrival, not a departure)";
        case Fast::Armed: return "already armed";
        case Fast::MachineOn: return "the hysteresis already says ON";
        case Fast::Aborted: return "U-1 aborted";
        case Fast::Pending: return "a request is pending";
        case Fast::DrainHold: return "a distance drain is held for the host's preparation";
        case Fast::Draining: return "g_p_draining is held (a drain at the host's preparation or an NPC cleanup)";
        case Fast::GuestLoading: return "user 2 is loading";
        case Fast::Logout: return "user 2's goodbye was asked";
        case Fast::TooSoon: return "a fast arm in the last 60 s";
        case Fast::Capped: return "the per-run cap of fast arms";
        default: return "?";
    }
}

struct FastIn {
    Sample prev, cur;            // the last two samples (cur = now)
    uint64_t host_load_us = 0;   // the host's last load step L (0 = never)
    uint64_t near_since_us = 0;  // the hysteresis' near_since BEFORE this sample's step (0 = not in a near streak)
    uint64_t last_fast_us = 0;   // the last fast arm (mode 1) or would (probe); 0 = none
    uint32_t fast_count = 0;     // fast arms (or would) this run
    bool armed = false, machine_on = false, aborted = false, pending = false, drain_hold = false,
         draining = false, guest_loading = false, logout = false;
    u2s::Thresholds t{};
};
// The jump itself: <= off -> >= on between two valid samples.
inline bool jump(const Sample& prev, const Sample& cur, const u2s::Thresholds& t) {
    return prev.valid && cur.valid && prev.dist <= t.off_cells && cur.dist >= t.on_cells;
}
inline Fast fast_verdict(const FastIn& in) {
    const Sample& p = in.prev;
    const Sample& c = in.cur;
    if (!jump(p, c, in.t)) return Fast::NoJump;
    if (c.t_us <= p.t_us || c.t_us - p.t_us > kMaxSampleGapUs) return Fast::NotConsecutive;
    if (!p.field || p.field != c.field) return Fast::OtherField;
    if (!p.u2_char || p.u2_char != c.u2_char) return Fast::OtherEpoch;
    if (u2s::cheb(p.user2, c.user2) > kGuestStillCells) return Fast::GuestMoved;
    const uint32_t need = in.t.on_cells > in.t.off_cells ? in.t.on_cells - in.t.off_cells : 1;
    if (u2s::cheb(p.host, c.host) < need) return Fast::HostStill;
    if (!in.host_load_us || in.host_load_us > c.t_us || c.t_us - in.host_load_us > kHostLoadWindowUs) {
        return Fast::NoHostLoad;
    }
    // Review 3a: the L must be the DEPARTURE: later than the first sample of the near streak the jump ends. The host's
    // arrival L always precedes that streak (it was far when it loaded), so a stale / corrected far reading within 30 s
    // of an arrival is refused; a long departure load (the position moving late) still passes.
    if (!in.near_since_us || in.host_load_us <= in.near_since_us) return Fast::LoadBeforeNear;
    if (in.aborted) return Fast::Aborted;
    if (in.armed) return Fast::Armed;
    if (in.machine_on) return Fast::MachineOn;
    if (in.pending) return Fast::Pending;
    if (in.drain_hold) return Fast::DrainHold;
    if (in.draining) return Fast::Draining;
    if (in.guest_loading) return Fast::GuestLoading;
    if (in.logout) return Fast::Logout;
    if (in.last_fast_us && c.t_us - in.last_fast_us < kMinGapUs && c.t_us >= in.last_fast_us) return Fast::TooSoon;
    if (in.fast_count >= kOneShotCap) return Fast::Capped;
    return Fast::Fast;
}
// ---------------------------------------------------------------- the confirmation (review 3b)

enum class Confirm : uint8_t {
    Wait,        // still within kConfirmUs: keep watching
    Go,          // confirmed: arm (mode 1) / would (probe)
    GuestLoad,   // user 2 started a load since the jump (it follows the host): dropped, nothing asked
    Blocked,     // armed / pending / held / g_p_draining / leaving / aborted / ON meanwhile: dropped
    Invalid,     // a sample unreadable or another field / character: dropped
    Stale,       // no sample for too long (more than kConfirmUs + kMaxSampleGapUs since the jump): dropped
    NotFar,      // back under the on threshold: dropped (the hysteresis would not have armed either)
    GuestMoved,  // user 2's cell moved more than 1 cell since the jump: dropped
    Count
};
inline const char* confirm_name(Confirm c) {
    switch (c) {
        case Confirm::Wait: return "waiting";
        case Confirm::Go: return "confirmed";
        case Confirm::GuestLoad: return "user 2 started a load (it follows the host)";
        case Confirm::Blocked: return "something became pending (arm, drain, hold, g_p_draining, goodbye, abort)";
        case Confirm::Invalid: return "a sample unreadable, or another field / character";
        case Confirm::Stale: return "no sample for too long";
        case Confirm::NotFar: return "back under the on threshold";
        case Confirm::GuestMoved: return "user 2 moved since the jump";
        default: return "?";
    }
}
struct ConfirmIn {
    Sample jump, cur;          // the jump sample (the candidate) and this one
    uint64_t u2_load_us = 0;   // the last load Troc of user 2 that started (0 = none)
    bool guest_loading = false;
    bool blocked = false;
    u2s::Thresholds t{};
};
inline Confirm confirm_verdict(const ConfirmIn& in) {
    const Sample& j = in.jump;
    const Sample& c = in.cur;
    if (in.guest_loading || (in.u2_load_us && in.u2_load_us >= j.t_us)) return Confirm::GuestLoad;
    if (in.blocked) return Confirm::Blocked;
    if (!c.valid || c.field != j.field || c.u2_char != j.u2_char) return Confirm::Invalid;
    if (c.t_us < j.t_us || c.t_us - j.t_us > kConfirmUs + kMaxSampleGapUs) return Confirm::Stale;
    if (c.dist < in.t.on_cells) return Confirm::NotFar;
    if (u2s::cheb(j.user2, c.user2) > kGuestStillCells) return Confirm::GuestMoved;
    return c.t_us - j.t_us >= kConfirmUs ? Confirm::Go : Confirm::Wait;
}

// Mode 1 re-checks these at the arm itself (the request may be up to a few seconds old): a fast arm is refused when
// user 2 started loading, its goodbye was asked or g_p_draining is held (a drain at the host's preparation, or
// npc_activation's cleanup), and when the distance recomputed there from both characters is unknown or < on (review
// 3a). Refused either way; for the first three the machine stays ON (control() asks again, as a normal arm, after its
// 10 s: what the hysteresis alone would have done), the distance clears it (a false trigger).
inline bool fast_still_valid(bool guest_loading, bool logout, bool draining) {
    return !guest_loading && !logout && !draining;
}

// A 2043 tick that can be the one-shot: +0xB (r8b) 0 and +0xC (r9b) 0. The drain's immediate 2043 (0x102C2A30) writes
// u16 0x100 at +0xB, i.e. +0xC = 1; the periodic one has +0xB = 1.
inline bool oneshot_shape(uint8_t r8b, uint8_t r9b) { return r8b == 0 && r9b == 0; }

// ---------------------------------------------------------------- (b) the one-shot 2043

// 0x287EF60's "not ready" one-shot: FB 07 | FF | the 8-byte zero cell | +0xB..+0xE u32 0 | +0xF 0. Identical to the
// periodic payload (u2s::timer_2043_payload) but +0xB = 0: 0x287DBB0 evaluates and does not reschedule.
inline void oneshot_2043_payload(uint8_t (&p)[u2s::kTimerPayload], const uint8_t (&cell)[8]) {
    u2s::timer_2043_payload(p, cell);
    p[11] = 0;
}

// Late: a one-shot-shaped tick (+0xB = 0, +0xC = 0) after its one-shot was cancelled or lost (the timer itself cannot
// be taken back from the game's queue): counted once, never a chain tick.
enum class Shot : uint8_t { Idle, Pending, Consumed, Cancelled, Lost, Late, Count };
enum class Cancel : uint8_t { None, Drain, Logout, NewCharacter, Rearm, Count };
inline const char* cancel_name(Cancel c) {
    switch (c) {
        case Cancel::Drain: return "a drain of its manager";
        case Cancel::Logout: return "its logout";
        case Cancel::NewCharacter: return "user 2's character changed";
        case Cancel::Rearm: return "a new arm";
        default: return "none";
    }
}
// One slot: at most ONE pending one-shot, tagged with its arm generation. Packed in one atomic word so the server's
// threads (queue under the module lock, consume in the tick hook) and the module thread (lost) never disagree:
// state (4 bits) | cancel reason (4 bits) | generation (56 bits).
class OneShotSlot {
public:
    static uint64_t pack(Shot s, Cancel c, uint64_t gen) {
        return static_cast<uint64_t>(s) | static_cast<uint64_t>(c) << 4 | (gen & 0x00FFFFFFFFFFFFFFull) << 8;
    }
    static Shot state_of(uint64_t v) { return static_cast<Shot>(v & 0xF); }
    static Cancel cancel_of(uint64_t v) { return static_cast<Cancel>((v >> 4) & 0xF); }
    static uint64_t gen_of(uint64_t v) { return v >> 8; }

    // Before queueing: true when this generation may queue one (none pending, none queued for it yet).
    bool may_queue(uint64_t gen) const {
        const uint64_t v = w_.load(std::memory_order_acquire);
        return gen && state_of(v) != Shot::Pending && gen_of(v) != (gen & 0x00FFFFFFFFFFFFFFull);
    }
    // After EnqueueTimer returned 0: pending for `gen`. False when another is pending (never two). The time is stored
    // BEFORE the state (review 2b): expire() that sees Pending also sees this time, never an older one-shot's.
    bool queued(uint64_t gen, uint64_t now_us) {
        uint64_t v = w_.load(std::memory_order_acquire);
        if (state_of(v) == Shot::Pending) return false;
        queued_us_.store(now_us, std::memory_order_release);
        return w_.compare_exchange_strong(v, pack(Shot::Pending, Cancel::None, gen), std::memory_order_acq_rel);
    }
    // EnqueueTimer returned an error: the generation is spent, nothing pending.
    void failed(uint64_t gen) { w_.store(pack(Shot::Consumed, Cancel::None, gen), std::memory_order_release); }
    // Review 2a, at every arm (the hysteresis' too): a Cancelled / Lost / Late state of an older arm goes back to Idle
    // (its generation kept), so a later native one-shot-shaped tick is never logged as late. Pending is left alone.
    void settle() {
        uint64_t v = w_.load(std::memory_order_acquire);
        while (state_of(v) == Shot::Cancelled || state_of(v) == Shot::Lost || state_of(v) == Shot::Late) {
            if (w_.compare_exchange_weak(v, pack(Shot::Idle, Cancel::None, gen_of(v)), std::memory_order_acq_rel)) {
                return;
            }
        }
    }
    // The tick hook: a 2043 with +0xB = 0 reached user 2's manager. True once per pending one-shot (its generation
    // returned); false when none is pending (another one-shot: the drain's immediate 2043, the native "not ready" one).
    bool consume(uint64_t& gen_out, uint64_t& queued_us_out) {
        uint64_t v = w_.load(std::memory_order_acquire);
        if (state_of(v) != Shot::Pending) return false;
        if (!w_.compare_exchange_strong(v, pack(Shot::Consumed, Cancel::None, gen_of(v)), std::memory_order_acq_rel)) {
            return false;
        }
        gen_out = gen_of(v);
        queued_us_out = queued_us_.load(std::memory_order_acquire);
        return true;
    }
    // Drain / logout / new character / re-arm: a pending one is cancelled (its timer may still fire: counted, never
    // trusted as a chain tick). True when one was pending.
    bool cancel(Cancel why, uint64_t now_us, uint64_t& gen_out) {
        uint64_t v = w_.load(std::memory_order_acquire);
        if (state_of(v) != Shot::Pending) return false;
        changed_us_.store(now_us, std::memory_order_release);  // before the state, as in queued()
        while (state_of(v) == Shot::Pending) {
            if (w_.compare_exchange_weak(v, pack(Shot::Cancelled, why, gen_of(v)), std::memory_order_acq_rel)) {
                gen_out = gen_of(v);
                return true;
            }
        }
        return false;
    }
    // The module thread: pending for kOneShotLostUs with no tick -> lost (the arm's chain repair still covers it).
    bool expire(uint64_t now_us, uint64_t& gen_out) {
        uint64_t v = w_.load(std::memory_order_acquire);
        if (state_of(v) != Shot::Pending) return false;
        const uint64_t q = queued_us_.load(std::memory_order_acquire);
        if (!q || now_us < q || now_us - q < kOneShotLostUs) return false;
        changed_us_.store(now_us, std::memory_order_release);
        if (!w_.compare_exchange_strong(v, pack(Shot::Lost, Cancel::None, gen_of(v)), std::memory_order_acq_rel)) {
            return false;
        }
        gen_out = gen_of(v);
        return true;
    }
    // The tick hook, when consume() said no: the first one-shot-shaped tick after a cancel / loss, within
    // kOneShotLostUs of it (true once). Later ones are someone else's (review 2a).
    bool late(uint64_t now_us, uint64_t& gen_out) {
        uint64_t v = w_.load(std::memory_order_acquire);
        if (state_of(v) != Shot::Cancelled && state_of(v) != Shot::Lost) return false;
        const uint64_t at = changed_us_.load(std::memory_order_acquire);
        if (!at || now_us < at || now_us - at > kOneShotLostUs) return false;
        if (!w_.compare_exchange_strong(v, pack(Shot::Late, cancel_of(v), gen_of(v)), std::memory_order_acq_rel)) {
            return false;
        }
        gen_out = gen_of(v);
        return true;
    }
    uint64_t raw() const { return w_.load(std::memory_order_acquire); }
    void reset() {
        w_.store(0, std::memory_order_release);
        queued_us_.store(0, std::memory_order_release);
        changed_us_.store(0, std::memory_order_release);
    }

private:
    std::atomic<uint64_t> w_{0};
    std::atomic<uint64_t> queued_us_{0};
    std::atomic<uint64_t> changed_us_{0};  // when it was cancelled / lost
};

// ---------------------------------------------------------------- what it saved

// When the next chain tick would have reached user 2's manager after an arm at arm_us: a chain queued at the arm fires
// one period later; an alive one fires a period after its last rescheduling tick (never before the arm).
inline uint64_t next_chain_tick_us(uint64_t arm_us, bool chain_queued, uint64_t chain_last_us, uint32_t period_ms) {
    const uint64_t p = static_cast<uint64_t>(period_ms) * 1000;
    if (!p) return 0;
    if (chain_queued || !chain_last_us) return arm_us + p;
    uint64_t next = chain_last_us + p;
    while (next < arm_us) next += p;  // a chain behind schedule: its next fire after the arm
    return next;
}
inline uint64_t saved_us(uint64_t would_have_us, uint64_t did_us) {
    return would_have_us > did_us ? would_have_us - did_us : 0;
}

// ---------------------------------------------------------------- the leftover WRN (4-contencion §3, #6)

// The module thread read user 2's lists at read_us; a drain at the host's preparation that is running, or finished
// after that read, makes "stages left after a drain" a stale reading (S34 1b: the WRN at 68646.845 between the drain's
// decision 68646.794 and DRAINED 68647.011). Per operation (the real flag / the drain's own time), no fixed wait.
enum class Leftover : uint8_t { Ask, WouldSkip, Skip };
inline Leftover leftover_verdict(Mode m, bool draining_now, uint64_t read_us, uint64_t last_drain_us) {
    const bool stale = draining_now || (last_drain_us && last_drain_us >= read_us);
    if (!stale || m == Mode::Off) return Leftover::Ask;
    return m == Mode::On ? Leftover::Skip : Leftover::WouldSkip;
}

}  // namespace cdmp::session2::frm
