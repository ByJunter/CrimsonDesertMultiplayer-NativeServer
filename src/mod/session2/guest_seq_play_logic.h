#pragma once
// P8 guest_seq_play (docs/research/pendientes/p8-impl-escenas-play.md): [session2]
// guest_seq_play = 0 | probe | 1 and guest_seq_play_live = 0 | 1, GUEST. The pure logic, with no game access.
//
// The fault [V] (p8-s8-escenas.md §3, p8-xtls-lectura.md §1-§3): the host's sequencers copied to the guest
// (observer_seq_copy + guest_seq_route) are created in X, the guest's ClientSequencerStageManager of the host's
// character, and stay there: X sits at +0x231 = 2 and nobody updates it. The game updates only M (the field
// owner's manager, this client's own): 0x8B6060 (main loop, once per frame from 0xAD277F) -> 0x8B6150
// 0xA953C0(M, Q, mode) -> 0xA97300 (start instances), and 0x8B2660 -> 0xAA3E70(M, dt) (live scenes); the
// 2 -> 3 step is the load state 0x14's 0x77D061 call 0xA97B30 = jmp .xtls 0x9F8FCE0, only for M.
//
// The 3 [V, p8-xtls-lectura.md §1.1-§1.4, read in Ghidra]: 0x9F8FCE0 only writes G+0x231 = 3 (0x9F8FD19,
// no lock) once the FieldAttacher of [G+0x18] succeeds, and plays the eligible entries of +0x1C0 at once
// (owner's missions / type 5 / AABB); nothing else (no counters, timers, +0x174, +0x1B0/+0x1D0, callbacks).
// 0xA97300 with +0x231 = 3 plays every ready entry of +0x1B0/+0x1C0 itself, one per call (0xA97503): so
// "write 3 + tick" = the native call minus its first unbraked burst (§1.4), without running .xtls elsewhere.
//
// What guest_seq_play does, at a mid hook on 0x8B6144 (right before the native tick of M, same thread and
// frame): for each X a routing hook put in the table (the host's character, never M), when everything below
// holds, 0xA953C0(X, Q, mode) [and with _live 0xAA3E70(X, dt)]; once per routed 2322 of X, before that tick,
// the byte X+0x231 = 3. probe: the same decisions, counted and logged; no call, no write.
//
// Threads [V static, p8-impl-escenas-play.md §1.4]: the routing hooks (Ack handlers, run by 0x35D0C0 from
// 0xAD246A) and the play hook (0x8B6060 from 0xAD277F) are both called by 0xAD2380, the main loop, on the
// game's main thread, one after the other. The table is still written under a non-blocking try-lock and read
// with a per-slot sequence (another thread only reads it: the diagnostics thread).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cdmp::session2::gsp {

enum class Mode : uint8_t { Off, Probe, On };
inline Mode parse_mode(std::string_view v, bool* bad = nullptr) {
    if (bad) *bad = false;
    if (v.empty() || v == "0") return Mode::Off;
    if (v == "probe") return Mode::Probe;
    if (v == "1") return Mode::On;
    if (bad) *bad = true;
    return Mode::Off;
}
inline const char* mode_name(Mode m) {
    switch (m) {
        case Mode::Off: return "off";
        case Mode::Probe: return "PROBE (decides and counts; no call, no write)";
        case Mode::On: return "ON";
        default: return "?";
    }
}
// Review 3: guest_seq_play=1 is refused (off) unless guest_seq_route=1: only a ROUTED packet fills the table
// (the routing creates the instances in X). guest_seq_play=probe also needs guest_seq_route=1 to see any X:
// with guest_seq_route=probe nothing is routed, so the table stays empty (allowed, it only counts frames);
// with guest_seq_route=0 it is off. route: guest_seq_route's value parsed the same way. Returns the mode used.
inline Mode effective_mode(Mode play, Mode route) {
    if (route == Mode::Off) return Mode::Off;
    if (play == Mode::On && route != Mode::On) return Mode::Off;
    return play;
}

// [[this+0x80]+0x50] == 2: 0xA953C0 skips 0xA97300 (0xA953E4) and 0x8B2660 skips 0xAA3E70 (0x8B26A2).
inline constexpr uint8_t kFieldModeNoTick = 2;
inline constexpr uint8_t kStatePreload = 2;  // after the 2322 (0xB87A67)
inline constexpr uint8_t kStatePlay = 3;     // what .xtls 0x9F8FCE0 writes (0x9F8FD19)

// What the hook read for one X in one frame (SEH reads; the tests by hand).
struct Facts {
    Mode mode = Mode::Off;
    bool live = false;        // guest_seq_play_live = 1
    bool code_ok = false;     // the hook and the three callees verified, not switched off after a fault
    uint8_t field_mode = 0;   // [[rdi+0x80]+0x50] (the byte the hooked instruction loads)
    uint64_t m = 0;           // RAX at 0x8B6144: vf48's manager, this client's own (M)
    uint8_t m_ready = 0;      // M+0x231 (3: this client's own load ended)
    uint64_t m_ctx_vt = 0;    // [[M+8]] (the class of M's lock object)
    uint64_t q = 0;           // [rbp-0x41]: this frame's field
    bool q_is_field = false;  // [Q] is pa::ClientField's vtable and its vf40 slot is 0x9A5B70
    int8_t owner_q = -1;      // vf40(Q) 0x9A5B70 (the field owner's missions): -1 not asked, 0 null, 1 not null
    bool live_gate = true;    // byte exe+0x6CC0218 == 0 (0x8B63CE / 0x8B67E3: the native 0xAA3E70 call needs it)
    uint64_t epoch = 0;       // this connection's epoch (guest_connection_epoch)
    // the table's slot
    uint64_t x = 0;
    uint32_t key = 0;         // the character the routing hook saw (0xA...)
    uint64_t slot_field = 0;  // the field the last routed packet of X was handled in
    uint64_t slot_epoch = 0;  // the connection that packet came on
    // X's own fields
    uint64_t x_vt = 0, want_vt = 0;  // [X], exe + pa::ClientSequencerStageManager's vtable
    uint32_t x_key = 0;       // key of [X+0x18]
    uint64_t x_own = 0;       // [[[[X+0x18]+0x68]+0xA8]+0x18]: that actor's own manager (must be X)
    bool actor_live = false;  // the client actor table has key -> [X+0x18] (whose own manager is X)
    uint64_t x_ctx = 0;       // [X+8] (0xA97300 / 0xA953C0 lock it: 0x393650 / 0x393DF0)
    uint64_t x_ctx_vt = 0;    // [[X+8]]
    uint8_t x_ready = 0;      // X+0x231
    uint32_t x_loading = 0;   // X+0x174
    uint32_t n1b0 = 0, n1c0 = 0;  // X+0x1B8, X+0x1C8
    bool armed = false;       // the 3 is due for this X (WriteMark)
    // Phase 2 safety (p8-impl-escenas-play.md §13): 0xAA3E70(X) only with the skip hook at 0xAA4B27 installed
    // (it jumps over the local player's part and 0xAA5040 / 0xAA5400 for our call) and, as an extra read-only
    // gate, with X's fields that drive those parts all idle.
    bool skip_hook_ok = false;
    uint8_t x_2a4 = 0;        // 0xAA5040: != 0 -> [[World+0xA8]+0x208] = 1.0f (0xAA539A)
    uint64_t x_288 = 0;       // 0xAA5040: the _blendRatio block (0xAA50F3)
    uint64_t x_2a8 = 0;       // 0xAA5040: an object handle (set by 0xA9C841, lives with the manager, cleared only
                              // by vf1): interpolated only while the float timer +0x2B0 >= 0 (0xAA5081..0xAA508D)
    float x_2b0 = 0.0f;       // that timer (0xAA50EB: -= dt)
    float x_2b4 = 0.0f;       // the interpolation's target (0xAA50B1; for the line only)
    uint8_t x_2d4 = 0;        // 0xAA5400: flag of the +0x2D0 timer (0xAA5433)
    float x_2cc = 0.0f;       // 0xAA5400: > 0 -> the timer runs; on expiry, _chapterTransition (0xAA5421)
};

// The read-only gate before 0xAA3E70(X): every field that lets 0xAA5040 / 0xAA5400 act on the client's globals
// idle. (The skip hook already jumps over both for our call; this keeps X out of a frame where they would act
// were the hook to miss.)
// +0x2CC: `<= 0.0f`, so a NaN is NOT idle (review of 0383f70, 4).
// +0x2A8 (session 9, p8-s9-npc.md §5): a live object handle, not a request: it was the only field ever non-zero
// and held phase 2 for good. 0xAA5040 only works on it while the float +0x2B0 >= 0 (vcomiss/jb at 0xAA5089: a NaN
// skips natively; here a NaN is NOT idle, the safe side). So: idle when +0x2A8 == 0 or +0x2B0 < 0. X+0x2A8 is never
// written (it would drop the manager's reference to its object).
inline bool live_fields_idle(const Facts& f) {
    return f.x_2a4 == 0 && f.x_288 == 0 && (f.x_2a8 == 0 || f.x_2b0 < 0.0f) && f.x_2d4 == 0 && f.x_2cc <= 0.0f;
}

// Review of 0383f70, 5 [V partial, p8-impl-escenas-play.md §13.5]: X+0x2CC is counted down only by 0xAA5400 and
// X+0x2A4 cleared only by 0xAA5040 (both skipped for X), X+0x2D4 cleared only by the manager's vf1 0xA94660: once
// a scene of X sets one (vf7 0xA99AD0 sets +0x2D4/+0x2CC), the gate holds X's live update for good. It is not
// forced: a value held longer than `limit_us` is reported once per episode (and counted); the hold goes on.
struct GateWatch {
    uint64_t since_us = 0;  // 0: idle (or never seen held)
    bool reported = false;
    // held: this frame's gate was not idle. True once per episode, when it has been held for limit_us.
    bool update(bool held, uint64_t now_us, uint64_t limit_us) {
        if (!held) {
            since_us = 0;
            reported = false;
            return false;
        }
        if (!since_us) since_us = now_us ? now_us : 1;
        if (reported || now_us < since_us || now_us - since_us < limit_us) return false;
        reported = true;
        return true;
    }
};

enum class Skip : uint8_t {
    None,               // ticked (or would be, probe)
    Off,
    NotInstalled,       // the hook or a callee not verified, or switched off after a fault
    FieldModeNoTick,    // [[rdi+0x80]+0x50] == 2: the game does not tick M either
    NoManager,          // RAX 0
    OwnNotReady,        // M+0x231 != 3 (this client's own load not ended: p8-xtls-lectura.md §6 R2)
    NotField,           // Q is not a ClientField with vf40 0x9A5B70
    EmptySlot,          // the slot's X not written yet
    StaleEpoch,         // routed on an earlier connection (the slot is cleared)
    OtherField,         // routed in another field than this frame's Q (the slot is cleared)
    XIsM,
    NotStageManager,    // [X] is not pa::ClientSequencerStageManager (gone, or another object now)
    XKeyMismatch,       // [X+0x18]'s key is not the character the routing saw
    XNotActorsOwn,      // that actor's own manager is not X
    ActorGone,          // the client actor table does not give that actor for the key any more
    NoContext,          // [X+8] 0
    ContextClass,       // [[X+8]] is not the class of [[M+8]]
    OwnerMissionsNull,  // vf40(Q) null: 0x602940 would fault at 0x602997 (and 0x1AC2830) [V]
    Count
};
inline const char* skip_name(Skip s) {
    switch (s) {
        case Skip::None: return "ticked";
        case Skip::Off: return "off";
        case Skip::NotInstalled: return "not installed / switched off";
        case Skip::FieldModeNoTick: return "field mode 2 (the game does not tick M either)";
        case Skip::NoManager: return "no M";
        case Skip::OwnNotReady: return "this client's own M+0x231 is not 3 yet";
        case Skip::NotField: return "Q is not a ClientField (vf40 0x9A5B70)";
        case Skip::EmptySlot: return "slot being filled";
        case Skip::StaleEpoch: return "routed on an earlier connection (slot cleared)";
        case Skip::OtherField: return "routed in another field than this frame's (slot cleared)";
        case Skip::XIsM: return "X is M";
        case Skip::NotStageManager: return "X is not a ClientSequencerStageManager (gone?)";
        case Skip::XKeyMismatch: return "X's character is not the routed one";
        case Skip::XNotActorsOwn: return "X is not its character's own manager";
        case Skip::ActorGone: return "X's character is not in the client actor table any more";
        case Skip::NoContext: return "[X+8] null";
        case Skip::ContextClass: return "[[X+8]] is not the class of [[M+8]]";
        case Skip::OwnerMissionsNull: return "vf40 of the field returned null";
        default: return "?";
    }
}
// The slot is cleared for these (a routed packet on this connection / in this field adds X again).
inline bool clears_slot(Skip s) { return s == Skip::StaleEpoch || s == Skip::OtherField; }

struct Plan {
    Skip skip = Skip::Off;
    bool needs_owner = false;  // On, X passed every check: ask vf40(Q) (once per frame), then plan again
    bool tick = false;         // 0xA953C0(X, Q, field_mode)
    bool write3 = false;       // X+0x231 = 3 before that tick
    bool live = false;         // 0xAA3E70(X, dt) after it
    bool live_held = false;    // live was due but the skip hook is missing or the gate is not idle (counted)
    bool live_no_hook = false; // ... because of the hook
};

// The 3, once: X at 2 (the copied 2322 applied), no SequencerLoadingTask in flight (0xA97366 would do
// nothing), this client's own load ended, something loaded waiting (+0x1B0 or +0x1C0), due (WriteMark).
inline bool write3_due(const Facts& f) {
    return f.armed && f.x_ready == kStatePreload && f.x_loading == 0 && f.m_ready == kStatePlay &&
           (f.n1b0 + f.n1c0) > 0;
}

// In this order (the first that fails is the reason).
inline Plan plan(const Facts& f) {
    Plan p;
    auto skip = [&p](Skip s) {
        p.skip = s;
        return p;
    };
    if (f.mode == Mode::Off) return skip(Skip::Off);
    if (!f.code_ok) return skip(Skip::NotInstalled);
    if (f.field_mode == kFieldModeNoTick) return skip(Skip::FieldModeNoTick);
    if (!f.m) return skip(Skip::NoManager);
    if (f.m_ready != kStatePlay) return skip(Skip::OwnNotReady);
    if (!f.q_is_field) return skip(Skip::NotField);
    if (!f.x || !f.key) return skip(Skip::EmptySlot);
    if (f.slot_epoch != f.epoch) return skip(Skip::StaleEpoch);
    if (f.slot_field != f.q) return skip(Skip::OtherField);
    if (f.x == f.m) return skip(Skip::XIsM);
    if (!f.want_vt || f.x_vt != f.want_vt) return skip(Skip::NotStageManager);
    if (f.x_key != f.key) return skip(Skip::XKeyMismatch);
    if (f.x_own != f.x) return skip(Skip::XNotActorsOwn);
    if (!f.actor_live) return skip(Skip::ActorGone);
    if (!f.x_ctx) return skip(Skip::NoContext);
    if (!f.m_ctx_vt || f.x_ctx_vt != f.m_ctx_vt) return skip(Skip::ContextClass);
    if (f.mode == Mode::On) {
        if (f.owner_q < 0) {
            p.skip = Skip::None;
            p.needs_owner = true;
            return p;
        }
        if (f.owner_q == 0) return skip(Skip::OwnerMissionsNull);
    }
    p.skip = Skip::None;
    p.tick = true;
    p.write3 = write3_due(f);
    // Live scenes can only exist in state 3; in 1/2 0xAA3E70 would run only its local-user part for X.
    const bool due = f.live && f.live_gate && (f.x_ready == kStatePlay || p.write3);
    if (due && !f.skip_hook_ok) {
        p.live_held = p.live_no_hook = true;
    } else if (due && !live_fields_idle(f)) {
        p.live_held = true;
    } else {
        p.live = due;
    }
    return p;
}

// Instances that left X's lists in one tick in state 3 (each was played by 0xA9D860, or deleted with a
// code by 0xA95530: the 2276 the host's filter drops). An estimate of "scenes started".
inline uint32_t consumed(uint32_t before, uint32_t after) { return before > after ? before - after : 0; }

// "The 3 was written for this X and this routed 2322" (main thread only). gen is unique per routed 2322
// (XTable's global counter), so a new 2322, a new X or a slot added again is due again. Fallback (review 4):
// marked but X reads 2 again with the same gen = our 3 was overwritten by a native 2 without a new routed
// 2322 (impossible when the 2322 handler runs on the main thread, as it does [V]): due again, at most
// kMaxRearms times.
struct WriteMark {
    static constexpr uint32_t kMaxRearms = 3;
    uint64_t x = 0;
    uint64_t gen = ~0ull;
    uint32_t rearms = 0;
    bool armed(uint64_t x_now, uint64_t gen_now, uint8_t x_ready) const {
        if (!(x == x_now && gen == gen_now)) return true;
        return x_ready == kStatePreload && rearms < kMaxRearms;
    }
    // True: this write is a re-arm (the same X and gen were written before).
    bool mark(uint64_t x_now, uint64_t gen_now) {
        const bool again = x == x_now && gen == gen_now;
        rearms = again ? rearms + 1 : 0;
        x = x_now;
        gen = gen_now;
        return again;
    }
};

// The X the routing hooks ROUTED a packet into (guest_seq_route, Verdict::Route only). Writers: those hooks
// (and the play hook clearing a stale slot), under a non-blocking try-lock: a writer that finds it taken
// gives up and counts it (the next routed packet tries again). Readers: a per-slot sequence (odd while a
// write is in progress); a reader that cannot get a stable copy in a few tries sees an empty slot. One slot
// per character key; a new X, field or connection for the key replaces the slot's values in place; slots of
// an earlier connection are reused first when the table is full. gen: 0 until a 2322 is routed, then a value
// unique in the table's life (a global counter), so a write mark can never match a new 2322.
template <size_t N>
class XTable {
public:
    struct View {
        uint64_t x = 0;
        uint32_t key = 0;
        uint64_t gen = 0;
        uint64_t field = 0;
        uint64_t epoch = 0;
    };
    enum class Noted : uint8_t { Added, Updated, Busy, Full, Bad };
    Noted note(uint64_t x, uint32_t key, uint64_t field, uint64_t epoch, bool milestone) noexcept {
        if (!x || !key) return Noted::Bad;
        if (lock_.test_and_set(std::memory_order_acquire)) {
            busy_.fetch_add(1, std::memory_order_relaxed);
            return Noted::Busy;
        }
        Slot* s = nullptr;
        bool fresh = false;
        for (Slot& c : s_) {
            if (c.key.load(std::memory_order_relaxed) == key) {
                s = &c;
                break;
            }
        }
        if (!s) {
            for (Slot& c : s_) {
                if (c.key.load(std::memory_order_relaxed) == 0) {
                    s = &c;
                    break;
                }
            }
        }
        if (!s) {
            for (Slot& c : s_) {
                if (c.epoch.load(std::memory_order_relaxed) != epoch) {
                    s = &c;
                    break;
                }
            }
        }
        Noted r = Noted::Full;
        if (s) {
            fresh = s->key.load(std::memory_order_relaxed) != key || s->epoch.load(std::memory_order_relaxed) != epoch;
            begin(*s);
            s->key.store(key, std::memory_order_relaxed);
            s->x.store(x, std::memory_order_relaxed);
            s->field.store(field, std::memory_order_relaxed);
            s->epoch.store(epoch, std::memory_order_relaxed);
            if (milestone) {
                s->gen.store(next_gen_.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_relaxed);
            } else if (fresh) {
                s->gen.store(0, std::memory_order_relaxed);
            }
            end(*s);
            r = fresh ? Noted::Added : Noted::Updated;
        } else {
            full_.fetch_add(1, std::memory_order_relaxed);
        }
        lock_.clear(std::memory_order_release);
        return r;
    }
    // The play hook: empties slot i if it still holds `key` (a stale connection or another field).
    bool clear(size_t i, uint32_t key) noexcept {
        if (i >= N || lock_.test_and_set(std::memory_order_acquire)) return false;
        bool done = false;
        if (s_[i].key.load(std::memory_order_relaxed) == key) {
            begin(s_[i]);
            s_[i].key.store(0, std::memory_order_relaxed);
            s_[i].x.store(0, std::memory_order_relaxed);
            s_[i].field.store(0, std::memory_order_relaxed);
            s_[i].gen.store(0, std::memory_order_relaxed);
            s_[i].epoch.store(0, std::memory_order_relaxed);
            end(s_[i]);
            cleared_.fetch_add(1, std::memory_order_relaxed);
            done = true;
        }
        lock_.clear(std::memory_order_release);
        return done;
    }
    View get(size_t i) const noexcept {
        View v;
        if (i >= N) return v;
        const Slot& s = s_[i];
        for (int tries = 0; tries < 8; ++tries) {
            const uint32_t a = s.seq.load(std::memory_order_acquire);
            if (a & 1) continue;
            View t;
            t.key = s.key.load(std::memory_order_relaxed);
            t.x = s.x.load(std::memory_order_relaxed);
            t.gen = s.gen.load(std::memory_order_relaxed);
            t.field = s.field.load(std::memory_order_relaxed);
            t.epoch = s.epoch.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (s.seq.load(std::memory_order_relaxed) == a) return t;
        }
        unstable_.fetch_add(1, std::memory_order_relaxed);
        return v;
    }
    // Is X the current X of `key` on this connection (for the 2524: only an X the play hook ticks)?
    bool knows(uint64_t x, uint32_t key, uint64_t epoch) const noexcept {
        for (size_t i = 0; i < N; ++i) {
            const View v = get(i);
            if (v.key == key) return v.x == x && v.epoch == epoch;
        }
        return false;
    }
    size_t used() const noexcept {
        size_t n = 0;
        for (const Slot& s : s_) n += s.key.load(std::memory_order_relaxed) != 0;
        return n;
    }
    uint64_t full() const noexcept { return full_.load(std::memory_order_relaxed); }
    uint64_t busy() const noexcept { return busy_.load(std::memory_order_relaxed); }
    uint64_t cleared() const noexcept { return cleared_.load(std::memory_order_relaxed); }
    uint64_t unstable() const noexcept { return unstable_.load(std::memory_order_relaxed); }
    static constexpr size_t size() { return N; }

private:
    struct Slot {
        std::atomic<uint32_t> seq{0};
        std::atomic<uint64_t> x{0};
        std::atomic<uint32_t> key{0};
        std::atomic<uint64_t> gen{0};
        std::atomic<uint64_t> field{0};
        std::atomic<uint64_t> epoch{0};
    };
    static void begin(Slot& s) {
        s.seq.fetch_add(1, std::memory_order_relaxed);  // odd: being written
        std::atomic_thread_fence(std::memory_order_release);
    }
    static void end(Slot& s) { s.seq.fetch_add(1, std::memory_order_release); }
    Slot s_[N];
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::atomic<uint64_t> next_gen_{0};
    std::atomic<uint64_t> full_{0}, busy_{0}, cleared_{0};
    mutable std::atomic<uint64_t> unstable_{0};
};

}  // namespace cdmp::session2::gsp
