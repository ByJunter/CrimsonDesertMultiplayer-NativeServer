#pragma once
// P8, remote actors' interpolation delay per actor (docs/research/pendientes/p8-movimiento-remoto.md
// §3 F1; the implementation: docs/research/pendientes/p8-impl-interp-remota.md): the pure logic of
// remote_interp.cpp, with no game access, so the tests can run it.
//
// [session2] remote_interp = 0 (default, nothing installed) | probe | 1, on the host's and on the
// guest's PC. Every actor 0xB... (NPC, animal, pet, horse) is moved by its controller's client, which
// sends a MoveActorReq only every ~1.1 s; on the observing client the actor is in control mode 2 and
// interpolates a buffer of those samples with ONE global delay of 200 ms (exe+0x6CB9548) and empties
// the buffer when two samples are more than 1000 ms apart: ~1 m in 180 ms, then still for 0.9 s.
//
// With 1: the four reads of that delay (two inserts, two per-frame updates; position and rotation)
// get a delay of the actor's own, from the cadence of ITS position buffer:
//     target = clamp(max(last 3 gaps between accepted samples) + 150, lo, 1800), lo = max(200, game's)
// approached without jumps (up at most 0.5 ms per ms: the actor plays at >= 0.5x; down at most
// 0.25 ms per ms: <= 1.25x), only for actors whose key is 0xB...; a player's character (0xA...)
// keeps the game's value. And the emptying threshold 1000 -> 2500 ms (two `cmp r8,imm32`).
// probe: the same measures (and the delay it WOULD use), nothing changed.
// P8 session 25, remote_interp_combat (guest): the host's enemies in combat get a delay that follows their arrival
// cadence ("the combat class" below; docs/research/noche/combate-retardo.md §6.2).

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "mod/session2/user2_funnels_logic.h"

namespace cdmp::session2::rip {

// The option: 0 | probe | 1 (the same parser as the other P8 keys; an unknown value is 0, warned).
using u2f::Mode;
using u2f::mode_name;
using u2f::parse_mode;

// ---------------------------------------------------------------- constants (build 2976)

inline constexpr uint32_t kPosBuffer = 0x3D0;  // mc + 0x3D0: the position buffer (set 0x11B1FD0, update 0x11B2420)
inline constexpr uint32_t kRotBuffer = 0x488;  // mc + 0x488: the rotation buffer (set 0x11B26E0, update 0x11B2AF0)
// The buffer: +0x08 sample array (0xA0 bytes each), +0x10 u32 count.
inline constexpr uint32_t kBufArray = 0x08;
inline constexpr uint32_t kBufCount = 0x10;
inline constexpr uint32_t kSampleSize = 0xA0;
inline constexpr uint32_t kSampleTs = 0x98;  // u64 ms (server time base)
// The movement controller: +0x60 = its CC ([CC+0xC8] == mc), +0x68 = the actor.
inline constexpr uint32_t kMcCc = 0x60;
inline constexpr uint32_t kMcActor = 0x68;
inline constexpr uint32_t kCcMc = 0xC8;
inline constexpr uint32_t kActorKey = 0x60;  // u32
inline constexpr uint32_t kSaneCount = 64;   // the game keeps <= 30 (exe+0x6CB9598)

inline constexpr uint32_t kOrigThreshold = 1000;    // the game's emptying threshold (ms)
inline constexpr uint32_t kPatchedThreshold = 2500;  // with remote_interp=1
inline constexpr uint32_t kMinDelay = 200;
inline constexpr uint32_t kMaxDelay = 1800;
inline constexpr uint32_t kMargin = 150;
inline constexpr uint32_t kMaxRecordedGap = 5000;  // a longer gap is a pause, not the cadence
inline constexpr uint32_t kMaxStepMs = 100;        // dt of one approach step (no jump after a long absence)
inline constexpr uint64_t kStaleSlotMs = 10000;    // a slot not touched for this long may be taken by another mc

// ---------------------------------------------------------------- keys

enum class KeyClass : uint8_t { Character, Npc, Other };  // 0xA..., 0xB..., anything else
inline constexpr size_t kClasses = 3;

inline KeyClass key_class(uint32_t key) {
    switch (key >> 28) {
        case 0xA: return KeyClass::Character;
        case 0xB: return KeyClass::Npc;
        default: return KeyClass::Other;
    }
}

inline const char* class_name(KeyClass c) {
    switch (c) {
        case KeyClass::Character: return "0xA";
        case KeyClass::Npc: return "0xB";
        case KeyClass::Other: return "other";
    }
    return "?";
}

// ---------------------------------------------------------------- the delay

// The game's global (movsxd of an int): anything outside [0, 60000] is not trusted (200 is used).
inline uint32_t sane_game_delay(int64_t v) { return v >= 0 && v <= 60000 ? static_cast<uint32_t>(v) : kMinDelay; }

// lo = max(200, the game's value): an F0 experiment that raised the global is never undercut.
inline uint32_t delay_floor(uint32_t game_delay) { return std::max(kMinDelay, game_delay); }

// The target of one buffer from its last 3 gaps (0 = no gap yet). cap = the top of the delay (1800, or
// remote_interp_owned_cap_ms for an actor with an owner); never below lo.
inline uint32_t target_delay(const std::array<uint32_t, 3>& gaps, uint32_t game_delay, uint32_t cap = kMaxDelay) {
    const uint32_t lo = delay_floor(game_delay);
    const uint32_t g = std::max({gaps[0], gaps[1], gaps[2]});
    if (g == 0) return lo;
    const uint32_t t = g + kMargin;
    return std::clamp(t, lo, std::max(lo, cap));
}

// ---------------------------------------------------------------- actors with an owner (P8 session 8 pets)
//
// [session2] remote_interp_owned_cap_ms = C (default 0 = as before), host and guest
// (pendientes/p8-s8-mascotas.md §2.3-§2.4, §4.2; pendientes/p8-impl-mascotas.md): an 0xB actor with an
// owner ([actor+0xA0] != 0 on the client: 0x8BDA00 / 0x8BDBF7 read [[actor+0xA0]+0x60] as the owner's
// key) is a user's pet, horse or companion. Its delay is capped at C instead of 1800 (session 8: the
// guest's pet ran 1138-1800 ms behind its owner at 200 ms), and while it rides on a parent (a
// StartAction whose Path parent is an 0xA character, seen on the network: attach_follow.h's registry)
// its sample gaps (4.17 s on the shoulder) are not recorded as its cadence.
enum : uint8_t { kOwnedUnknown = 0, kOwnedNo = 1, kOwnedYes = 2 };
inline constexpr uint32_t kOwnerOffset = 0xA0;  // the client actor's owner (ServerUserActor's client twin)

// The cap in force for one slot: C only for a known owned actor and C > 0 (clamped to [200, 1800]).
inline uint32_t delay_cap(uint8_t owned, uint32_t owned_cap_ms) {
    if (owned != kOwnedYes || owned_cap_ms == 0) return kMaxDelay;
    return std::clamp(owned_cap_ms, kMinDelay, kMaxDelay);
}

// The owner [actor+0xA0] read on the client: a pointer to a USER ([owner+0x60] a key of class 0x9, as
// 0x8BDA10 / 0x8BDC07 compare it with the record's owner and child_hold's owner_who on the server).
// Unreadable -> Unknown (asked again); null, not a pointer, or not a user's key -> No.
inline constexpr uint32_t kOwnerKey = 0x60;
inline uint8_t classify_owner(bool owner_read, uint64_t owner, bool key_read, uint32_t owner_key) {
    if (!owner_read) return kOwnedUnknown;
    if (owner < 0x10000 || owner > 0x00007FFFFFFFFFFFull) return kOwnedNo;
    if (!key_read) return kOwnedUnknown;
    return (owner_key >> 28) == 0x9 ? kOwnedYes : kOwnedNo;
}
// "No" is not final: an actor whose owner is set after its first sample (or read torn) is asked again
// every kOwnedRecheckMs of the hook's clock; Unknown always; Yes stays (another actor resets the slot).
inline constexpr uint64_t kOwnedRecheckMs = 1000;
inline bool owned_recheck_due(uint8_t owned, uint64_t checked_at, uint64_t now) {
    if (owned == kOwnedUnknown) return true;
    if (owned == kOwnedYes) return false;
    return now >= checked_at + kOwnedRecheckMs;
}

// A gap of an owned actor while it is attached to a parent is not its cadence (only with C > 0).
inline bool skip_gap_attached(uint8_t owned, uint32_t owned_cap_ms, bool attached) {
    return owned == kOwnedYes && owned_cap_ms != 0 && attached;
}

// Delays are kept in 1/16 ms so the slow approach does not round away.
inline constexpr uint32_t kFix = 16;
inline uint32_t to_fix(uint32_t ms) { return ms * kFix; }
inline uint32_t from_fix(uint32_t f) { return (f + kFix / 2) / kFix; }

// The way down, in 1/16 ms per ms: 0.25 ms/ms (<= 1.25x), or 1.0 ms/ms in the combat class (<= 2x, P8 session 25).
inline constexpr uint32_t kDownFix = kFix / 4;
inline constexpr uint32_t kCombatDownFix = kFix;

// One step towards `target` after `dt_ms` (capped at kMaxStepMs): up 0.5 ms/ms, down `down_fix` / 16 ms per ms.
inline uint32_t approach(uint32_t d_fix, uint32_t target_ms, uint64_t dt_ms, uint32_t down_fix = kDownFix) {
    const uint32_t dt = static_cast<uint32_t>(std::min<uint64_t>(dt_ms, kMaxStepMs));
    const uint32_t t = to_fix(target_ms);
    if (d_fix < t) return std::min(t, d_fix + dt * kFix / 2);
    if (d_fix > t) {
        const uint32_t step = dt * down_fix;
        return d_fix - std::min(d_fix - t, step);
    }
    return d_fix;
}

// ---------------------------------------------------------------- one insert (set position)

// What the game is about to do with a new position sample (0x11B1FD0 after the delay read):
// the buffer's count and last ts, the new ts, whether it moved more than 1e-4 m (0x5D72C34).
struct SetIn {
    uint32_t count = 0;
    uint64_t ts_last = 0;
    uint64_t ts_new = 0;
    bool moved = true;
    uint32_t threshold = kOrigThreshold;  // the threshold in force (1000, or 2500 once patched)
};
struct SetOut {
    bool older = false;     // ts_new <= ts_last: dropped by the game (count > 0)
    uint64_t gap = 0;       // ts_new - ts_last (count > 0, newer)
    bool reset = false;     // the game empties the buffer now (gap > threshold in force)
    bool reset_1000 = false;// it would with the game's own 1000
    bool record = false;    // a cadence gap of this actor (accepted, moved, <= kMaxRecordedGap)
};

inline SetOut observe_set(const SetIn& in) {
    SetOut o;
    if (in.count == 0) return o;
    if (in.ts_new <= in.ts_last) {
        o.older = true;
        return o;
    }
    o.gap = in.ts_new - in.ts_last;
    o.reset = o.gap > in.threshold;
    o.reset_1000 = o.gap > kOrigThreshold;
    // The reset comes before the 1e-4 m check in the game: a still sample after a reset is
    // compared with the synthetic one; for the cadence only real movement counts.
    o.record = in.moved && o.gap <= kMaxRecordedGap;
    return o;
}

// P8 session 25, the combat class (remote_interp_combat; "the combat class" below): the last arrival gaps kept per
// controller (any newer sample, moved or not, in ms of ts).
inline constexpr uint32_t kArrivals = 4;
inline constexpr uint32_t kMaxArrivalGap = 60000;

// ---------------------------------------------------------------- the per-buffer state (lock-free)

// One movement controller (shared by its position and rotation buffers). Every field is an
// atomic: the inserts run on the thread that applies the Acks and the per-frame updates on the
// actors' update threads (static analysis cannot prove them the same: p8-impl-interp-remota.md
// M5). A torn view between fields only gives one frame a slightly different delay.
struct Slot {
    std::atomic<uintptr_t> mc{0};       // 0 = never used
    std::atomic<uint32_t> key{0};       // the actor's key when the state was made
    std::atomic<uint64_t> touched{0};   // last use (the hook's clock, ms)
    std::atomic<uint32_t> gap[3] = {};  // the last 3 recorded gaps (ms)
    std::atomic<uint32_t> gap_next{0};
    std::atomic<uint32_t> d_fix{0};     // the delay in force, 1/16 ms (0 = not yet: the game's)
    std::atomic<uint64_t> d_at{0};      // the clock of the last approach step
    std::atomic<uint8_t> updated{0};    // a per-frame update read this buffer: mode 2
    std::atomic<uint8_t> owned{0};      // kOwnedUnknown / kOwnedNo / kOwnedYes ([actor+0xA0] and its key)
    std::atomic<uint64_t> owned_at{0};  // when `owned` was last read (the hook's clock)
    // P8 session 25, the combat class (only with remote_interp_combat).
    std::atomic<uint32_t> arr[kArrivals] = {};  // the last arrival gaps (ms of ts), any newer sample, moved or not
    std::atomic<uint32_t> arr_next{0};
    std::atomic<uint64_t> arr_last{0};          // the newest ts that arrived
    std::atomic<uint32_t> d2_fix{0};            // probe: the class's delay beside the one in force (1/16 ms)
    std::atomic<uint64_t> combat_at{0};         // the clock of its last frame in the class (0 = never)

    void reinit(uint32_t k, uint64_t now) noexcept {
        key.store(k, std::memory_order_relaxed);
        for (auto& g : gap) g.store(0, std::memory_order_relaxed);
        gap_next.store(0, std::memory_order_relaxed);
        d_fix.store(0, std::memory_order_relaxed);
        d_at.store(0, std::memory_order_relaxed);
        updated.store(0, std::memory_order_relaxed);
        owned.store(kOwnedUnknown, std::memory_order_relaxed);
        owned_at.store(0, std::memory_order_relaxed);
        for (auto& g : arr) g.store(0, std::memory_order_relaxed);
        arr_next.store(0, std::memory_order_relaxed);
        arr_last.store(0, std::memory_order_relaxed);
        d2_fix.store(0, std::memory_order_relaxed);
        combat_at.store(0, std::memory_order_relaxed);
        touched.store(now, std::memory_order_relaxed);
    }
    // P8 re-entry (s25/reentrada.md §1.4): the table generation the state was made in (SlotTable::new_generation).
    std::atomic<uint32_t> gen{0};
    std::array<uint32_t, 3> gaps() const noexcept {
        return {gap[0].load(std::memory_order_relaxed), gap[1].load(std::memory_order_relaxed),
                gap[2].load(std::memory_order_relaxed)};
    }
    void record_gap(uint32_t g) noexcept {
        const uint32_t i = gap_next.fetch_add(1, std::memory_order_relaxed) % 3;
        gap[i].store(g, std::memory_order_relaxed);
    }
    // set position, before the game's tests: a sample with this ts arrived. Only a newer one than the newest so far
    // counts (its gap to it is kept); false: an older or repeated ts. The set-position hook is not proven to run on
    // one thread per controller (p8-impl-interp-remota.md M5): the newest ts moves by compare-exchange, so each newer
    // sample records its gap to the one it replaced exactly once, and an older one that lost the race records none.
    bool note_arrival(uint64_t ts) noexcept {
        uint64_t last = arr_last.load(std::memory_order_relaxed);
        do {
            if (!ts || ts <= last) return false;
        } while (!arr_last.compare_exchange_weak(last, ts, std::memory_order_relaxed, std::memory_order_relaxed));
        if (!last) return true;
        const auto g = static_cast<uint32_t>(std::min<uint64_t>(ts - last, kMaxArrivalGap));
        const uint32_t i = arr_next.fetch_add(1, std::memory_order_relaxed) % kArrivals;
        arr[i].store(g, std::memory_order_relaxed);
        return true;
    }
    // The last kArrivals arrival gaps, the newest first (0 = not yet).
    std::array<uint32_t, kArrivals> arrivals() const noexcept {
        const uint32_t n = arr_next.load(std::memory_order_relaxed);
        std::array<uint32_t, kArrivals> out{};
        for (uint32_t k = 0; k < kArrivals; ++k) {
            out[k] = arr[(n + kArrivals - 1 - k) % kArrivals].load(std::memory_order_relaxed);
        }
        return out;
    }
};

// Fixed-size open-addressing table (linear probing, kProbe slots). A slot is never emptied, only
// taken over by another mc once it was not touched for kStaleSlotMs (so a lookup can stop at the
// first never-used slot). Two threads adding the same mc at once may make two slots: harmless
// (one of them goes stale). No allocation, no lock.
template <size_t N>
class SlotTable {
public:
    static_assert((N & (N - 1)) == 0, "power of two");
    static constexpr size_t kProbe = 32;

    struct Find {
        Slot* slot = nullptr;
        bool created = false;  // a new (or reclaimed) slot: reinit done
    };

    // The slot of `mc` (made if `create`); its key checked: another actor behind the same mc
    // (a freed controller reused) starts from scratch.
    Find get(uintptr_t mc, uint32_t key, uint64_t now, bool create) noexcept {
        Find f;
        if (!mc) return f;
        const size_t h = hash(mc);
        Slot* stale = nullptr;
        uintptr_t stale_mc = 0;
        const uint32_t gen = gen_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < kProbe; ++i) {
            Slot& s = slots_[(h + i) & (N - 1)];
            const uintptr_t m = s.mc.load(std::memory_order_acquire);
            if (m == mc) {
                if (s.key.load(std::memory_order_relaxed) != key) {
                    s.reinit(key, now);
                    s.gen.store(gen, std::memory_order_relaxed);
                    rekeyed_.fetch_add(1, std::memory_order_relaxed);
                } else if (s.gen.load(std::memory_order_relaxed) != gen) {
                    // An older generation (a previous remote conversation): the same address and key, but not the
                    // same actor's history.
                    s.reinit(key, now);
                    s.gen.store(gen, std::memory_order_relaxed);
                    regen_.fetch_add(1, std::memory_order_relaxed);
                }
                s.touched.store(now, std::memory_order_relaxed);
                f.slot = &s;
                return f;
            }
            if (m == 0) {
                if (!create) return f;
                uintptr_t expected = 0;
                if (s.mc.compare_exchange_strong(expected, mc, std::memory_order_acq_rel)) {
                    s.reinit(key, now);
                    s.gen.store(gen, std::memory_order_relaxed);
                    used_.fetch_add(1, std::memory_order_relaxed);
                    f.slot = &s;
                    f.created = true;
                    return f;
                }
                if (expected == mc) {  // another thread made it just now
                    f.slot = &s;
                    return f;
                }
                continue;
            }
            if (!stale && (now > s.touched.load(std::memory_order_relaxed) + kStaleSlotMs ||
                           s.gen.load(std::memory_order_relaxed) != gen)) {
                stale = &s;
                stale_mc = m;
            }
        }
        if (!create) return f;
        if (stale) {
            uintptr_t expected = stale_mc;
            if (stale->mc.compare_exchange_strong(expected, mc, std::memory_order_acq_rel)) {
                stale->reinit(key, now);
                stale->gen.store(gen, std::memory_order_relaxed);
                reclaimed_.fetch_add(1, std::memory_order_relaxed);
                f.slot = stale;
                f.created = true;
                return f;
            }
        }
        full_.fetch_add(1, std::memory_order_relaxed);
        return f;
    }

    // P8 re-entry: every slot made before this call starts from scratch at its next lookup and may be taken over
    // (lock-free: no slot is emptied, the probe chains stay valid). Any thread.
    void new_generation() noexcept { gen_.fetch_add(1, std::memory_order_relaxed); }
    uint32_t generation() const noexcept { return gen_.load(std::memory_order_relaxed); }
    uint64_t regenerated() const noexcept { return regen_.load(std::memory_order_relaxed); }
    uint64_t used() const noexcept { return used_.load(std::memory_order_relaxed); }
    uint64_t reclaimed() const noexcept { return reclaimed_.load(std::memory_order_relaxed); }
    uint64_t rekeyed() const noexcept { return rekeyed_.load(std::memory_order_relaxed); }
    uint64_t full() const noexcept { return full_.load(std::memory_order_relaxed); }
    // Every slot ever used (the drain thread; a view that may be torn under concurrent use).
    template <class F>
    void for_each(F&& f) const {
        for (const Slot& s : slots_) {
            if (s.mc.load(std::memory_order_relaxed)) f(s);
        }
    }
    // Slots touched since `since` (the drain thread; approximate under concurrent use).
    size_t live(uint64_t since) const noexcept {
        size_t n = 0;
        for (const Slot& s : slots_) {
            if (s.mc.load(std::memory_order_relaxed) && s.touched.load(std::memory_order_relaxed) >= since) ++n;
        }
        return n;
    }

private:
    static size_t hash(uintptr_t mc) noexcept {
        uint64_t x = static_cast<uint64_t>(mc) >> 4;
        x ^= x >> 33;
        x *= 0xFF51AFD7ED558CCDull;
        x ^= x >> 33;
        return static_cast<size_t>(x);
    }
    std::array<Slot, N> slots_{};
    std::atomic<uint64_t> used_{0}, reclaimed_{0}, rekeyed_{0}, full_{0}, regen_{0};
    std::atomic<uint32_t> gen_{0};
};

// ---------------------------------------------------------------- P8 mount ride: one clock for a rider and its horse
//
// [session2] remote_interp_pair = 0 (default) | 1, host AND guest (needs remote_interp and [sniffer] enabled;
// pendientes/p8-impl-monturas-cabalgata.md). A player's character (0xA) keeps the game's delay (~200 ms); its
// horse (0xB) got its own adaptive one (S13: 900 then 626 ms on the host): both correct, shown at different
// instants. With 1, while a rider's latest Move Ack names an 0xB Path parent (the sniffer's OnRecvPacket observer,
// horse_follow.cpp), that horse's TARGET is the rider's delay, provided its own samples are dense enough to be
// read at it (each of its last 3 gaps <= that delay: no buffer running dry); otherwise its own target. The delay
// still moves only through approach() (<= +0.5 / -0.25 ms per ms): no jump on mounting nor on dismounting (the
// pair ends: back to its own target the same way). Only the pair's horse; every other 0xB keeps its own.
inline constexpr size_t kPairSlots = 16;
// A pair ends on the rider's explicit Ack with no parent or another one (dismount, another horse). A rider standing
// still in the saddle sends no Move for a while, so silence alone is NOT an end; only this long a silence (a rider
// gone without a last Ack: despawn, logout, a lost packet) forgets it. Review of 3aaf038: 1.5 s sent a still
// mounted horse back to its own target (900 ms cap) and a 2.8 s ramp.
inline constexpr uint64_t kPairFreshMs = 60000;

struct RidePairs {
    struct Entry {
        std::atomic<uint32_t> rider{0}, horse{0};
        std::atomic<uint64_t> seen{0};  // the mod's clock (GetTickCount64) of the rider's last Ack
    };
    std::array<Entry, kPairSlots> e{};
    std::atomic<uint32_t> next{0};
    std::atomic<uint64_t> last_any{0};  // the last note naming a horse (the hooks skip the lookup when stale)
    std::atomic<uint64_t> began{0}, ended{0};

    // A rider's latest Move Ack: its Path parent `horse` (0 = on foot / another parent: its pair ends).
    void note(uint32_t rider, uint32_t horse, uint64_t now) noexcept {
        if (!rider) return;
        Entry* free = nullptr;
        for (Entry& x : e) {
            const uint32_t r = x.rider.load(std::memory_order_relaxed);
            if (r == rider) {
                const uint32_t was = x.horse.load(std::memory_order_relaxed);
                const bool fresh = now < x.seen.load(std::memory_order_relaxed) + kPairFreshMs;
                if (horse != was || !fresh) {
                    if (was && fresh) ended.fetch_add(1, std::memory_order_relaxed);
                    if (horse) began.fetch_add(1, std::memory_order_relaxed);
                }
                x.horse.store(horse, std::memory_order_relaxed);
                x.seen.store(now, std::memory_order_relaxed);
                if (horse) last_any.store(now, std::memory_order_relaxed);
                return;
            }
            if (!free && (r == 0 || now >= x.seen.load(std::memory_order_relaxed) + kPairFreshMs * 4)) free = &x;
        }
        if (!horse) return;  // nothing to end
        Entry& x = free ? *free : e[next.fetch_add(1, std::memory_order_relaxed) % kPairSlots];
        x.rider.store(0, std::memory_order_relaxed);
        x.horse.store(horse, std::memory_order_relaxed);
        x.seen.store(now, std::memory_order_relaxed);
        x.rider.store(rider, std::memory_order_relaxed);
        last_any.store(now, std::memory_order_relaxed);
        began.fetch_add(1, std::memory_order_relaxed);
    }
    // P8 re-entry: every pair is forgotten (a new remote conversation). Any thread.
    void forget() noexcept {
        last_any.store(0, std::memory_order_relaxed);
        for (Entry& x : e) {
            x.rider.store(0, std::memory_order_relaxed);
            x.horse.store(0, std::memory_order_relaxed);
            x.seen.store(0, std::memory_order_relaxed);
        }
    }
    // The rider of `horse` now (0 = none fresh).
    uint32_t rider_of(uint32_t horse, uint64_t now) const noexcept {
        if (!horse || now >= last_any.load(std::memory_order_relaxed) + kPairFreshMs) return 0;
        for (const Entry& x : e) {
            if (x.horse.load(std::memory_order_relaxed) != horse) continue;
            if (now >= x.seen.load(std::memory_order_relaxed) + kPairFreshMs) continue;
            if (const uint32_t r = x.rider.load(std::memory_order_relaxed)) return r;
        }
        return 0;
    }
    template <class F>
    void for_each_live(uint64_t now, F&& f) const {
        for (const Entry& x : e) {
            const uint32_t r = x.rider.load(std::memory_order_relaxed), h = x.horse.load(std::memory_order_relaxed);
            if (r && h && now < x.seen.load(std::memory_order_relaxed) + kPairFreshMs) f(r, h);
        }
    }
};

// The horse's target while its rider rides it: the rider's delay if the horse's samples are dense enough to be
// read at it (every one of the last 3 gaps known and <= that delay); otherwise `own` (its adaptive target).
inline uint32_t pair_target(const std::array<uint32_t, 3>& gaps, uint32_t own, uint32_t rider_delay,
                            bool* dense = nullptr) {
    const bool ok = rider_delay && gaps[0] && gaps[1] && gaps[2] &&
                    std::max({gaps[0], gaps[1], gaps[2]}) <= rider_delay;
    if (dense) *dense = ok;
    return ok ? rider_delay : own;
}

// The game keeps at most 30 samples per buffer (exe+0x6CB9598; 0x11B1FD0 drops the oldest past it) and, while
// now - D is before the first one, does not move the actor at all (0x11B2420: "T <= ts[0]: no escribe out";
// movimiento-remoto-cliente.md S4). With a ridden horse's dense samples (S13 G->H: the rider every ~12 ms) 30
// samples span ~360 ms: a horse still at its owned cap (900 ms) when the dense samples start would stand frozen
// until the approach brings D under that span (~2 s), then snap. So while paired, once its buffer is full, D never
// asks for a moment at or before its first sample: D = min(D, now - ts[0] - margin) (never under `floor`, never
// raised). The margin keeps T = now - D strictly after ts[0] (the game's rule is "T <= ts[0]: no move", so D = age
// exactly would stall that frame): at least one frame (kBufMarginMs), or the horse's largest recent sample gap if
// larger (a burst pushing ts[0] forward stays inside it), at most kBufMarginMaxMs. At that point the horse was
// standing frozen at its oldest kept pose, so the step is to that pose's neighbour, not a jump in time.
inline constexpr uint32_t kBufFullCount = 29;  // 30 in the game: one early, so the first drop never freezes it
inline constexpr uint32_t kBufMarginMs = 33;   // one frame at 30 fps
inline constexpr uint32_t kBufMarginMaxMs = 100;
inline uint32_t buffer_margin(const std::array<uint32_t, 3>& gaps) {
    const uint32_t g = std::max({gaps[0], gaps[1], gaps[2]});
    return std::clamp(g, kBufMarginMs, kBufMarginMaxMs);
}
inline uint32_t clamp_to_buffer(uint32_t d_ms, uint32_t count, uint64_t ts0, uint64_t now, uint32_t floor,
                                bool* clamped = nullptr, uint32_t margin = kBufMarginMs) {
    if (clamped) *clamped = false;
    if (count < kBufFullCount || !ts0 || now <= ts0) return d_ms;
    const uint64_t age = now - ts0;
    const uint64_t limit = age > margin ? age - margin : 0;
    if (d_ms <= limit) return d_ms;
    const auto v = static_cast<uint32_t>(std::min<uint64_t>(d_ms, std::max<uint64_t>(limit, floor)));
    if (clamped) *clamped = v < d_ms;
    return v;
}

// |D horse - D rider| per frame while paired (ms): 0, <=10, <=50, <=100, <=200, <=400, <=700, more.
inline constexpr std::array<uint32_t, 7> kPairDiffBounds = {0, 10, 50, 100, 200, 400, 700};

// ---------------------------------------------------------------- P8 session 25: the combat class (guest)
//
// [session2] remote_interp_combat = 0 (default) | probe | 1, GUEST, needs remote_interp (the combat stream,
// docs/research/noche/combate-retardo.md §5, §6.2). The host's enemies are mode-2 actors here; their D followed
// their sparse samples (0.9-1.3 s) and fell at only 0.25 ms per ms when combat_follow's stream began, so their
// bodies lagged their animations by 0.2-1.3 s. An 0xB controller is in the combat class when (a) its last 4 arrival
// gaps (every newer sample, moved or not: combat_follow_v2's keepalives repeat the pose and are not inserted, but
// are cadence) are all <= kDenseGapMs, or (b) its key is in this guest's engagement table (a 2922 / 2991 with this
// client's own character, combat_follow_logic.h's read_out rule, within kEngageMs). In the class:
//     target = clamp(max(last 3 arrival gaps) + 80, 120, 400)      (a: dense)
//     target = clamp(max(last 3 moved gaps, each capped at 300) + 80, 120, 400)   (only b, not dense yet)
// approached up at 0.5 ms/ms and down at 1.0 ms/ms; the floor 120 only in the class. An owned actor (pet, horse,
// companion: it follows its owner) and a paired horse (its rider's delay) are never in it. Outside: as before.
// probe: the class's delay runs on a shadow controller (d2_fix) beside the one in force, counted, never applied.
inline constexpr uint32_t kDenseGapMs = 150;
inline constexpr uint32_t kCombatMargin = 80;
inline constexpr uint32_t kCombatFloor = 120;
inline constexpr uint32_t kCombatCap = 400;
inline constexpr uint32_t kCombatMovedCap = 300;
// The engagement table: its keys are engaged this long after their last packet (combat_follow_logic.h's kEngageMs).
inline constexpr size_t kEngageSlots = 32;
inline constexpr uint64_t kEngageMs = 8000;

// (b) The enemies engaged with this client's character: written by ONE thread (the mod's network thread,
// guest_deliver), read by the hooks; relaxed atomics, a fixed table, no allocation. A torn read only makes one frame
// see the key engaged or not.
struct EngageTable {
    struct Entry {
        std::atomic<uint32_t> key{0};
        std::atomic<uint64_t> at{0};  // the mod's clock (GetTickCount64) of its last engaging packet
    };
    std::array<Entry, kEngageSlots> e{};
    std::atomic<uint64_t> last_any{0};  // the hooks skip the lookup when nothing was engaged lately
    std::atomic<uint64_t> notes{0}, evicted{0};

    void note(uint32_t key, uint64_t now) noexcept {
        if (!key || !now) return;
        notes.fetch_add(1, std::memory_order_relaxed);
        last_any.store(now, std::memory_order_relaxed);
        for (Entry& x : e) {
            if (x.key.load(std::memory_order_relaxed) == key) {
                x.at.store(now, std::memory_order_relaxed);
                return;
            }
        }
        // A free or expired entry, else the oldest (counted).
        Entry* slot = nullptr;
        Entry* oldest = &e[0];
        for (Entry& x : e) {
            const uint64_t at = x.at.load(std::memory_order_relaxed);
            if (!x.key.load(std::memory_order_relaxed) || now >= at + kEngageMs) {
                slot = &x;
                break;
            }
            if (at < oldest->at.load(std::memory_order_relaxed)) oldest = &x;
        }
        if (!slot) {
            slot = oldest;
            evicted.fetch_add(1, std::memory_order_relaxed);
        }
        slot->key.store(0, std::memory_order_relaxed);
        slot->at.store(now, std::memory_order_relaxed);
        slot->key.store(key, std::memory_order_relaxed);
    }
    // P8 re-entry: every engaged enemy is forgotten (a new remote conversation). Any thread.
    void forget() noexcept {
        last_any.store(0, std::memory_order_relaxed);
        for (Entry& x : e) {
            x.key.store(0, std::memory_order_relaxed);
            x.at.store(0, std::memory_order_relaxed);
        }
    }
    bool engaged(uint32_t key, uint64_t now) const noexcept {
        if (!key || now >= last_any.load(std::memory_order_relaxed) + kEngageMs) return false;
        for (const Entry& x : e) {
            if (x.key.load(std::memory_order_relaxed) != key) continue;
            return now < x.at.load(std::memory_order_relaxed) + kEngageMs;
        }
        return false;
    }
    size_t live(uint64_t now) const noexcept {
        size_t n = 0;
        for (const Entry& x : e) {
            const bool fresh = now < x.at.load(std::memory_order_relaxed) + kEngageMs;
            n += x.key.load(std::memory_order_relaxed) && fresh ? 1 : 0;
        }
        return n;
    }
};

// (a) Dense: the last kArrivals arrival gaps all known and <= kDenseGapMs.
inline bool dense_arrivals(const std::array<uint32_t, kArrivals>& a) {
    for (const uint32_t g : a) {
        if (g == 0 || g > kDenseGapMs) return false;
    }
    return true;
}

struct CombatClass {
    bool in = false;
    bool dense = false;    // (a)
    bool engaged = false;  // (b)
};
// excluded: an owned actor or a paired horse (they follow their owner's / rider's delay).
inline CombatClass combat_class(KeyClass cls, bool excluded, bool engaged, const std::array<uint32_t, kArrivals>& a) {
    CombatClass c;
    if (cls != KeyClass::Npc || excluded) return c;
    c.dense = dense_arrivals(a);
    c.engaged = engaged;
    c.in = c.dense || c.engaged;
    return c;
}

// The class's target. Dense: from the arrivals; else (engaged only) from the moved gaps, each capped at
// kCombatMovedCap (with none yet: the arrivals, capped the same; with nothing at all: kMinDelay).
inline uint32_t combat_target(const std::array<uint32_t, kArrivals>& a, const std::array<uint32_t, 3>& moved,
                              bool dense) {
    uint32_t g = 0;
    if (dense) {
        g = std::max({a[0], a[1], a[2]});
    } else if (std::max({moved[0], moved[1], moved[2]})) {
        for (const uint32_t m : moved) g = std::max(g, std::min(m, kCombatMovedCap));
    } else {
        for (uint32_t k = 0; k < 3; ++k) g = std::max(g, std::min(a[k], kCombatMovedCap));
        if (!g) return kMinDelay;
    }
    return std::clamp(g + kCombatMargin, kCombatFloor, kCombatCap);
}

// A frame with no sample ahead: T = now - D after the newest ts that arrived (moved or not), so a moving actor
// would stand still (no extrapolation in the game). 0 = nothing arrived yet.
inline bool no_sample_ahead(uint64_t now, uint32_t d_ms, uint64_t newest_ts) {
    return newest_ts && now > newest_ts + d_ms;
}

// update_step's part for the class. In: the mode, (b) and the exclusion. Out: what the step did.
struct CombatStep {
    Mode mode = Mode::Off;
    bool engaged = false;
    bool excluded = false;
    CombatClass cls;
    uint32_t target = 0;  // the class's target (cls.in)
    uint32_t would = 0;   // the class's controller after the step (the one in force with 1; the shadow with probe)
    uint32_t today = 0;   // the delay in force after the step
};

// Delays per class frame (ms): <=120, <=140, <=160, <=200, <=300, <=400, <=700, <=1000, more.
inline constexpr std::array<uint32_t, 8> kCombatDelayBounds = {120, 140, 160, 200, 300, 400, 700, 1000};

// The per-frame step (update position, once per frame per mode-2 actor): marks the buffer as read
// by a mode-2 update and, for an 0xB actor, moves its delay one step towards its target. The first
// step starts from the game's value (what the buffer was read with until now).
// cap: delay_cap(slot's owned, remote_interp_owned_cap_ms) (1800 by default).
// pair_delay: P8 mount ride, the rider's delay when this 0xB actor is a ridden horse with remote_interp_pair=1
// (0 = not paired: its own target). `dense` (optional): whether the pair's target was taken.
// combat (optional, P8 session 25): remote_interp_combat's mode, (b) and the exclusion in; the class and the delays
// out. With 1 the class's target and descent drive d_fix; with probe d_fix is as before and d2_fix runs the class.
inline void update_step(Slot& s, KeyClass cls, uint64_t now, uint32_t game_delay, uint32_t cap = kMaxDelay,
                        uint32_t pair_delay = 0, bool* dense = nullptr, CombatStep* combat = nullptr) noexcept {
    if (!s.updated.load(std::memory_order_relaxed)) s.updated.store(1, std::memory_order_relaxed);
    if (cls != KeyClass::Npc) return;
    uint32_t d = s.d_fix.load(std::memory_order_relaxed);
    if (d == 0) d = to_fix(game_delay ? game_delay : kMinDelay);
    const uint64_t at = s.d_at.load(std::memory_order_relaxed);
    const uint64_t dt = at && now > at ? now - at : 0;
    const std::array<uint32_t, 3> gaps = s.gaps();
    uint32_t target = target_delay(gaps, game_delay, cap);
    if (pair_delay) target = pair_target(gaps, target, pair_delay, dense);
    if (!combat || combat->mode == Mode::Off) {
        d = approach(d, target, dt);
        s.d_fix.store(d, std::memory_order_relaxed);
        s.d_at.store(now, std::memory_order_relaxed);
        return;
    }
    const std::array<uint32_t, kArrivals> arr = s.arrivals();
    combat->cls = combat_class(cls, combat->excluded || pair_delay != 0, combat->engaged, arr);
    combat->target = combat->cls.in ? combat_target(arr, gaps, combat->cls.dense) : 0;
    const uint32_t t2 = combat->cls.in ? combat->target : target;
    const uint32_t down2 = combat->cls.in ? kCombatDownFix : kDownFix;
    if (combat->mode == Mode::On) {
        d = approach(d, t2, dt, down2);
        combat->would = combat->today = from_fix(d);
    } else {
        uint32_t d2 = s.d2_fix.load(std::memory_order_relaxed);
        if (d2 == 0) d2 = d;  // the shadow starts where the delay in force is
        d2 = approach(d2, t2, dt, down2);
        d = approach(d, target, dt);
        s.d2_fix.store(d2, std::memory_order_relaxed);
        combat->would = from_fix(d2);
        combat->today = from_fix(d);
    }
    if (combat->cls.in) s.combat_at.store(now ? now : 1, std::memory_order_relaxed);
    s.d_fix.store(d, std::memory_order_relaxed);
    s.d_at.store(now, std::memory_order_relaxed);
}

// ---------------------------------------------------------------- the decision at a delay read

enum class Site : uint8_t { SetPos, SetRot, UpdPos, UpdRot };
inline constexpr size_t kSites = 4;
inline const char* site_name(Site s) {
    switch (s) {
        case Site::SetPos: return "set pos 0x11B200E";
        case Site::SetRot: return "set rot 0x11B2730";
        case Site::UpdPos: return "update pos 0x11B249A";
        case Site::UpdRot: return "update rot 0x11B2B62";
    }
    return "?";
}

struct DelayIn {
    Mode mode = Mode::Off;
    bool applying = false;  // the hooks may change the register (mode 1, all four in)
    KeyClass cls = KeyClass::Other;
    bool have_slot = false;
    bool updated = false;     // the slot's buffer is read by a per-frame update (mode 2)
    uint32_t d_fix = 0;       // the slot's delay (0 = none yet)
    uint32_t game_delay = kMinDelay;
};
struct DelayOut {
    uint32_t value = kMinDelay;  // what the read yields (the game's own unless replaced)
    bool replaced = false;       // the register gets `value` instead of the game's read
    uint32_t would = kMinDelay;  // what mode 1 would give (probe's measure)
};

// Only an 0xB actor whose buffer is consumed by the mode-2 update gets its own delay; everything
// else (players' characters, unknown buffers, no state yet) reads the game's value.
inline DelayOut decide_delay(const DelayIn& in) {
    DelayOut o;
    o.value = in.game_delay;
    o.would = in.game_delay;
    if (in.mode == Mode::Off || !in.have_slot || in.cls != KeyClass::Npc || !in.updated || in.d_fix == 0) return o;
    o.would = from_fix(in.d_fix);
    if (in.mode == Mode::On && in.applying && o.would != in.game_delay) {
        o.value = o.would;
        o.replaced = true;
    }
    return o;
}

// ---------------------------------------------------------------- measures (lock-free)

// A histogram over fixed upper bounds (the last bucket: above the last bound).
template <size_t B>
struct Histogram {
    explicit Histogram(const std::array<uint32_t, B>& b) : bounds(b) {}
    std::array<uint32_t, B> bounds{};
    std::array<std::atomic<uint64_t>, B + 1> n{};
    void add(uint64_t v) noexcept {
        size_t i = 0;
        while (i < B && v > bounds[i]) ++i;
        n[i].fetch_add(1, std::memory_order_relaxed);
    }
    uint64_t total() const noexcept {
        uint64_t t = 0;
        for (const auto& x : n) t += x.load(std::memory_order_relaxed);
        return t;
    }
};

// Gaps between samples: <=100, <=300, <=600, <=1000, <=1500, <=2500, <=5000, more.
inline constexpr std::array<uint32_t, 7> kGapBounds = {100, 300, 600, 1000, 1500, 2500, 5000};
// Delays applied per update (frames): <=200, <=400, <=700, <=1000, <=1300, <=1600, more (<= 1800).
inline constexpr std::array<uint32_t, 6> kDelayBounds = {200, 400, 700, 1000, 1300, 1600};

// The thread ids seen at one site (M5: are the four sites on one thread?).
template <size_t N>
struct TidSet {
    std::array<std::atomic<uint32_t>, N> tid{};
    std::atomic<uint64_t> overflow{0};
    void add(uint32_t t) noexcept {
        if (!t) return;
        for (auto& x : tid) {
            uint32_t cur = x.load(std::memory_order_relaxed);
            if (cur == t) return;
            if (cur == 0) {
                if (x.compare_exchange_strong(cur, t, std::memory_order_relaxed) || cur == t) return;
            }
        }
        overflow.fetch_add(1, std::memory_order_relaxed);
    }
    size_t count() const noexcept {
        size_t c = 0;
        for (const auto& x : tid) c += x.load(std::memory_order_relaxed) ? 1 : 0;
        return c;
    }
};

// ---------------------------------------------------------------- buffer -> actor (fake-memory testable)

struct Resolved {
    bool ok = false;
    uintptr_t mc = 0, actor = 0;
    uint32_t key = 0;
};
enum class ResolveFail : uint8_t { None, NoCc, NotItsMc, NoActor, NoKey };

// buf - off = mc; [mc+0x60] = CC with [CC+0xC8] == mc (a buffer of a movement controller, not any
// other user of these script functions); [mc+0x68] = the actor; u32 [actor+0x60] = its key.
template <class Mem>
Resolved resolve(const Mem& m, uintptr_t buf, uint32_t off, ResolveFail* why = nullptr) noexcept {
    Resolved r;
    auto fail = [&](ResolveFail f) {
        if (why) *why = f;
        return r;
    };
    if (buf < off) return fail(ResolveFail::NoCc);
    const uintptr_t mc = buf - off;
    uintptr_t cc = 0, back = 0, actor = 0;
    if (!m.ptr(mc + kMcCc, cc) || !cc) return fail(ResolveFail::NoCc);
    if (!m.ptr(cc + kCcMc, back) || back != mc) return fail(ResolveFail::NotItsMc);
    if (!m.ptr(mc + kMcActor, actor) || !actor) return fail(ResolveFail::NoActor);
    uint32_t key = 0;
    if (!m.u32(actor + kActorKey, key) || !key) return fail(ResolveFail::NoKey);
    r.ok = true;
    r.mc = mc;
    r.actor = actor;
    r.key = key;
    if (why) *why = ResolveFail::None;
    return r;
}

// The buffer's last sample: count and ts (count 0: no last).
struct LastSample {
    bool ok = false;
    uint32_t count = 0;
    uint64_t ts = 0;
    float pos[3] = {};
};
template <class Mem>
LastSample last_sample(const Mem& m, uintptr_t buf, bool with_pos) noexcept {
    LastSample s;
    uintptr_t arr = 0;
    if (!m.u32(buf + kBufCount, s.count) || s.count > kSaneCount) return s;
    if (s.count == 0) {
        s.ok = true;
        return s;
    }
    if (!m.ptr(buf + kBufArray, arr) || !arr) return s;
    const uintptr_t last = arr + static_cast<uintptr_t>(s.count - 1) * kSampleSize;
    if (!m.u64(last + kSampleTs, s.ts)) return s;
    if (with_pos && !m.vec3(last, s.pos)) return s;
    s.ok = true;
    return s;
}

// P8 mount ride: the buffer's count and its FIRST sample's ts (clamp_to_buffer). False: unreadable.
template <class Mem>
bool first_sample_ts(const Mem& m, uintptr_t buf, uint32_t& count, uint64_t& ts0) noexcept {
    uintptr_t arr = 0;
    ts0 = 0;
    if (!m.u32(buf + kBufCount, count) || count > kSaneCount) return false;
    if (count == 0) return true;
    if (!m.ptr(buf + kBufArray, arr) || !arr) return false;
    return m.u64(arr + kSampleTs, ts0);
}

// The game's own test (0x11B2192..0x11B21EF): moved iff |new - last| > 1e-4 m.
inline bool moved(const float a[3], const float b[3]) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz > 1e-8f;
}

}  // namespace cdmp::session2::rip
