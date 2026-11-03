#pragma once
// P8 session 8, pets (docs/research/pendientes/p8-s8-mascotas.md §1, §4.1; the implementation:
// docs/research/pendientes/p8-impl-mascotas.md): the pure logic of [session2] child_settle_ms (HOST,
// child_hold.cpp), with no game access, so the tests can run it.
//
// The fault [V] (7 of 7 Shadow sessions): the host's pet (0xB0100002, type 6, restore 1 since it has
// acted for minutes) reaches user 2's client in the SAME 2005 as the host's character, right behind it;
// the client's CreateOwnedActor 0x8AF580 fails (branch 0x8BDC4A, "Actor 생성에 실패했습니다") because the
// child attaches to its owner's main character, created in that very packet and still loading. The
// same child announced +89 ms / +414 ms / +3 s after its character is created. Nothing re-announces it.
//
// With child_settle_ms = S > 0: a user's child (key class 0xB, owner a validated user, type 4/5/6, ANY
// restore byte) that NotifyAppear is about to announce to a session that is not its owner's is withheld
// from that session when its owner's MAIN character ([owner+0xD0], key class 0xA) goes in the same call,
// was announced to that session less than S ms ago, or is still withheld from it (character_hold). It
// is released by a native re-announce (NotifyAppear([observer], [child]), a fresh 2005) S ms after the
// character's announcement to that observer, on the threads D-53 releases on.
//
// Justification of S = 1500 [V, session 8]: the child is created on the client at +89 ms (host's client),
// +414 ms (guest's own login) and +3 s (horse test C) after its character; it fails at +0 ms (7 of 7).

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace cdmp::session2::cst {

// The types of a user's child that settle (pet, horse, companion: [[actor+0x88]+1]). Session 8: the
// host's pet and user 2's pet are type 6.
inline bool settle_type(uint8_t type) { return type == 4 || type == 5 || type == 6; }

// A release that never comes for a reason of ours (the character never announced, readings that never
// settle) is announced natively at most this long after the hold (only while the observer is in range):
// the settle itself + character_hold's 15 s timeout + margin.
inline constexpr uint64_t kMaxWaitExtraMs = 15000 + 5000;
inline uint64_t max_wait_ms(uint32_t settle_ms) { return static_cast<uint64_t>(settle_ms) + kMaxWaitExtraMs; }

// ---------------------------------------------------------------- the 0xA announcements

// When each (character key, session) was last announced (lock-free; the game's NotifyAppear threads
// write, the split and the passes read). A slot is reused round robin; two writers racing for the same
// pair may make two slots: last() takes the newest. A torn slot is skipped (t is written last and
// checked again).
template <size_t N>
class AnnounceLog {
public:
    void note(uint32_t key, uintptr_t session, uint64_t now_ms) noexcept {
        if (!key || !session) return;
        const uint64_t t = now_ms ? now_ms : 1;
        for (auto& s : slots_) {
            if (s.key.load(std::memory_order_relaxed) == key && s.session.load(std::memory_order_relaxed) == session) {
                s.t.store(t, std::memory_order_release);
                return;
            }
        }
        Slot& s = slots_[next_.fetch_add(1, std::memory_order_relaxed) % N];
        s.t.store(0, std::memory_order_release);
        s.key.store(key, std::memory_order_relaxed);
        s.session.store(session, std::memory_order_relaxed);
        s.t.store(t, std::memory_order_release);
    }
    // The newest moment (key, session) was noted; 0 = never (or overwritten).
    uint64_t last(uint32_t key, uintptr_t session) const noexcept {
        uint64_t best = 0;
        for (const auto& s : slots_) {
            const uint64_t t = s.t.load(std::memory_order_acquire);
            if (!t) continue;
            if (s.key.load(std::memory_order_relaxed) != key || s.session.load(std::memory_order_relaxed) != session) {
                continue;
            }
            if (s.t.load(std::memory_order_acquire) != t) continue;  // rewritten meanwhile
            best = std::max(best, t);
        }
        return best;
    }
    void clear() noexcept {
        for (auto& s : slots_) {
            s.t.store(0, std::memory_order_relaxed);
            s.key.store(0, std::memory_order_relaxed);
            s.session.store(0, std::memory_order_relaxed);
        }
        next_.store(0, std::memory_order_relaxed);
    }

private:
    struct Slot {
        std::atomic<uint32_t> key{0};
        std::atomic<uintptr_t> session{0};
        std::atomic<uint64_t> t{0};
    };
    std::array<Slot, N> slots_{};
    std::atomic<uint32_t> next_{0};
};

// ---------------------------------------------------------------- the split

// Is the child withheld from one recipient session? main_in_call: its owner's main character is among
// this NotifyAppear's keys; main_last_ms: when that character was last announced to this session (0 =
// never seen); main_held: character_hold withholds it from this session now; main_in_range: the
// session's user observes the character's sector (nullopt: unreadable). A character in range that was
// never announced there is about to be (the child came first, e.g. its sector entered first): the child
// waits for it too. One out of range (the horse left far away) is not coming: the child goes.
inline bool settle_needed(uint32_t settle_ms, bool main_in_call, uint64_t main_last_ms, bool main_held,
                          uint64_t now_ms, std::optional<bool> main_in_range = std::nullopt) {
    if (!settle_ms) return false;
    if (main_in_call || main_held) return true;
    if (!main_last_ms) return main_in_range.value_or(false);
    return now_ms >= main_last_ms && now_ms - main_last_ms < settle_ms;
}

// ---------------------------------------------------------------- the release

struct Readings {
    bool epoch_same = true;
    std::optional<uintptr_t> actor;  // nullopt = unreadable, 0 = gone
    uintptr_t actor_then = 0;        // the actor when held
    std::optional<uintptr_t> owner;  // [actor+0xA0]
    uintptr_t owner_then = 0;
    bool observer_valid = false;      // the observer session still has the same validated user
    std::optional<bool> observes;     // that user is among the child's sector's observers
    bool main_held = false;           // character_hold still withholds the main character from it
    uint64_t main_last_ms = 0;        // the main character's last announcement to it (0 = not seen)
    std::optional<bool> main_in_range;  // the observer observes the character's sector (nullopt: unreadable)
    uint64_t since_ms = 0;            // when the child was withheld
    uint64_t now_ms = 0;
    // P8 session 9 (child_reannounce, reannounce_logic.h): an entry the guest asked for again is not released
    // before this moment (its backoff); 0 = no such wait.
    uint64_t not_before_ms = 0;
};
enum class Step : uint8_t { Keep, Drop, Release };
enum class Kind : uint8_t { None, Settled, MaxWait, Native };
inline const char* kind_name(Kind k) {
    switch (k) {
        case Kind::None: return "-";
        case Kind::Settled: return "settled";
        case Kind::MaxWait: return "max wait: announced natively";
        case Kind::Native: return "announced natively instead of dropped";
    }
    return "?";
}

// The moment the child may go: settle_ms after the character's last announcement to that observer (its
// hold's moment when that announcement is not known: overwritten in the log, or before the mod saw it).
inline uint64_t due_ms(uint32_t settle_ms, uint64_t since_ms, uint64_t main_last_ms) {
    return (main_last_ms ? main_last_ms : since_ms) + settle_ms;
}

// Drop only when it is certain that the observer cannot see the child now: another epoch, despawned (the
// actor is gone from the manager), the observer session gone or another user's, the observer out of its
// sectors (the engine announces it again when it comes back, withheld again if its character is new
// there). Anything unreadable is kept, and once past max_wait_ms announced natively (NotifyAppear looks
// the key up itself: an actor that is gone is simply not serialised): never dropped silently.
// Another actor or owner behind the key while in range: announced natively. Released settle_ms after
// the character's announcement; never while character_hold still withholds the character, nor while a
// character in range was never announced there (both: natively after max_wait_ms).
inline Step release_step(const Readings& r, uint32_t settle_ms, const char*& why, Kind* kind = nullptr) {
    auto out = [&](Step s, const char* w, Kind k) {
        why = w;
        if (kind) *kind = k;
        return s;
    };
    const uint64_t age = r.now_ms >= r.since_ms ? r.now_ms - r.since_ms : 0;
    const bool too_old = age >= max_wait_ms(settle_ms);
    if (!r.epoch_same) return out(Step::Drop, "another guest epoch", Kind::None);
    if (r.actor && !*r.actor) return out(Step::Drop, "despawned", Kind::None);
    if (!r.observer_valid) return out(Step::Drop, "observer gone", Kind::None);
    if (r.observes && !*r.observes) return out(Step::Drop, "observer out of range", Kind::None);
    auto keep = [&](const char* w) {
        if (!too_old) return out(Step::Keep, w, Kind::None);
        return out(Step::Release, "unreadable for too long: announced natively", Kind::MaxWait);
    };
    if (!r.actor) return keep("actor unreadable");
    const bool in_range = r.observes.value_or(false);
    if (r.actor_then && *r.actor != r.actor_then) {
        if (!in_range) return keep("another actor behind the key; observers unreadable");
        return out(Step::Release, "another actor behind the key: announced natively", Kind::Native);
    }
    if (!r.owner) return keep("owner unreadable");
    if (*r.owner != r.owner_then) {
        if (!in_range) return keep("owner changed; observers unreadable");
        return out(Step::Release, "owner changed: announced natively", Kind::Native);
    }
    if (r.main_held) {
        if (!too_old) return out(Step::Keep, "its character is still withheld (character_hold)", Kind::None);
        return out(Step::Release, "max wait (its character still withheld): announced natively", Kind::MaxWait);
    }
    if (!r.main_last_ms && r.main_in_range.value_or(false)) {
        if (!too_old) return out(Step::Keep, "its character is in range but not announced there yet", Kind::None);
        return out(Step::Release, "max wait (its character never announced there): announced natively",
                   Kind::MaxWait);
    }
    if (r.now_ms < due_ms(settle_ms, r.since_ms, r.main_last_ms)) {
        return out(Step::Keep, "settling", Kind::None);
    }
    if (r.not_before_ms && r.now_ms < r.not_before_ms) return out(Step::Keep, "re-announce backoff", Kind::None);
    if (!r.observes) return keep("observers unreadable");
    return out(Step::Release, "settled after its character", Kind::Settled);
}

}  // namespace cdmp::session2::cst
