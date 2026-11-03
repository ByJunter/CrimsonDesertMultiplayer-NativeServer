#pragma once
// P8 session 15, GUEST (docs/research/pendientes/p8-s15-viaje-tunel.md §8): the pure logic of
// [session2] guest_reload_wait_fix = 0 | probe | 1, with no game access, so the tests can run it.
// The game side is guest_reload_wait.cpp (driven by guest_actor_diag's mid hook at 0x7798D0).
//
// The fault [V, S15 captures]: on the guest, a host NPC whose CHILD record (actor +0xAC = the NPC) reached the
// client in an EARLIER 2005 than the NPC itself stays at +0x5E 8 (waiting login), +0xC2 0, +0xCF 0 for ever:
// the client's pending-login loop 0x8B7B50 destroys the orphan child (its parent is not there, 0x8B7D78) and
// the parent's login group then misses that child (0x4FD530: eErrNoInvalidActorKey, retried every frame). The
// client's field-change state 0x12 (0x779ED0) stops at the first EXISTING actor of the container's wait list
// [[[P+0xB8]+8]+0x78] that is not ready (vf1D8 0x4FE240: +0xC2 && +0xCF); in a reload in the same field
// (2643/3066, no 2174) that list is not emptied, so such an NPC holds the guest's load for ever.
//
// probe: at 0x12 of a trip, the blocking actor with its state, age, parent/children and what mode 1 would do.
// 1:     the same, and the front key of the wait list is removed exactly as 0x12 removes a ready one
//        (0x779EF7..0x779F0B: [keys][0] = [keys][n-1] ; [C+0x80] -= 1, under the lock [M+0x10] 0x12 takes)
//        when ALL of judge()'s conditions hold. Everything else stays native.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace cdmp::session2::grw {

// ---------------------------------------------------------------- the client's loading states

inline constexpr int kStateIdle = 0x18;   // in the field, no load running
inline constexpr int kStateWait = 0x12;   // the wait list (0x779E8B..0x779F25)
inline constexpr uint16_t kOpMoveFieldAck = 2174, kOpReloadPos = 2643, kOpPostAck = 3066;

inline constexpr uint16_t kOpTravelReq = 2283;  // the client's fast-travel / reload request (C>S)
// A trip begun by a 2283 whose state never left 0x18 within this long is stale: the real exit begins a new one.
inline constexpr uint64_t kStaleBeginUs = 10'000'000;

// One loading cycle ("trip"): it begins at whichever comes first, [P+0x15C] leaving 0x18 (0x18 -> 0x01 at the
// 2283, the 2437 a few states later) or the client's C>S 2283 seen while no trip runs (a missed 0x18 exit, or the
// Req written a frame before the state moves); it ends when the state comes back to 0x18.
class TripClock {
public:
    enum class Origin : uint8_t { None, StateExit, Req2283 };
    struct Change {
        bool began = false, ended = false, entered_wait = false, left_wait = false;
    };
    // The client sent a 2283 (seen by the game thread's next pass). True: a trip began here (take the snapshot).
    bool on_2283(uint64_t now_us) {
        if (in_trip_) return false;
        begin(now_us, Origin::Req2283);
        // Already out of 0x18 (its exit was missed): the trip is under way; in 0x18: its exit is still to come.
        left_idle_ = last_state_ >= 0 && last_state_ != kStateIdle;
        return true;
    }
    Change on_state(int old_state, int new_state, uint64_t now_us) {
        Change c;
        last_state_ = new_state;
        if (old_state == new_state) return c;
        if (old_state == kStateIdle && new_state != kStateIdle) {
            if (!in_trip_) {
                begin(now_us, Origin::StateExit);
                c.began = true;
            } else if (!left_idle_) {
                // Begun by its 2283 while still in 0x18: this is the same trip unless that 2283 is stale.
                left_idle_ = true;
                if (now_us - start_us_ > kStaleBeginUs) {
                    begin(now_us, Origin::StateExit);
                    c.began = true;
                }
            }
        }
        if (in_trip_ && left_idle_ && new_state == kStateIdle) {
            in_trip_ = false;
            c.ended = true;
        }
        if (new_state == kStateWait) {
            wait_since_us_ = now_us;
            c.entered_wait = true;
        } else if (old_state == kStateWait) {
            wait_since_us_ = 0;
            c.left_wait = true;
        }
        state_since_us_ = now_us;
        return c;
    }
    bool in_trip() const { return in_trip_; }
    uint32_t trip() const { return trip_; }
    uint64_t start_us() const { return start_us_; }
    uint64_t wait_since_us() const { return wait_since_us_; }
    uint64_t state_since_us() const { return state_since_us_; }
    Origin origin() const { return origin_; }

private:
    void begin(uint64_t now_us, Origin o) {
        in_trip_ = true;
        left_idle_ = o == Origin::StateExit;
        ++trip_;
        start_us_ = now_us;
        origin_ = o;
    }
    bool in_trip_ = false, left_idle_ = false;
    int last_state_ = -1;
    Origin origin_ = Origin::None;
    uint32_t trip_ = 0;
    uint64_t start_us_ = 0, wait_since_us_ = 0, state_since_us_ = 0;
};

// A reload in the same field: this trip saw the host's 2643 (reload position) and no 2174 (MoveFieldAck) since
// it began. Times are the probes' QPC microseconds; 0 = never seen.
constexpr bool is_reload_trip(uint64_t trip_start_us, uint64_t last_2643_us, uint64_t last_2174_us) {
    return trip_start_us != 0 && last_2643_us >= trip_start_us && !(last_2174_us >= trip_start_us);
}

// P8 S27 (docs/research/s25/reaparicion.md; LOG ONLY): state 0x11 (0x779E39..0x779E7F) waits for
// [M+0x231] >= 2, and only the handler of a PlayAllPreLoadSequencerAck 2322 naming this client's own character
// writes 2 (0xB87A67). In a reload with own M+0x231 < 2 for this long, that 2322 is missing: the host's server sent it
// to the field owner's character (its 2873's single-owner path), the load never reaches 0x17 / its 2445.
inline constexpr int kStateOwn2322 = 0x11;
inline constexpr uint64_t kOwn2322WarnUs = 10'000'000;
constexpr bool waiting_own_2322(int state, bool reload, unsigned own_m231, uint64_t in_state_us) {
    return state == kStateOwn2322 && reload && own_m231 != 0xFF && own_m231 < 2 && in_state_us >= kOwn2322WarnUs;
}

// ---------------------------------------------------------------- the not-ready set when the trip began

struct SnapEntry {
    uint32_t key = 0;
    uint16_t state = 0;  // actor +0x5E
    bool ready = false;  // +0xC2 && +0xCF
    uintptr_t actor = 0;
};

// The wait list's keys that existed when the trip began (only the game's thread writes or reads it).
template <size_t N>
class Snapshot {
public:
    void clear() {
        n_ = 0;
        overflow_ = 0;
        taken_us_ = 0;
    }
    void begin(uint64_t now_us) {
        clear();
        taken_us_ = now_us;
    }
    bool add(const SnapEntry& e) {
        if (n_ >= N) {
            ++overflow_;
            return false;
        }
        e_[n_++] = e;
        return true;
    }
    const SnapEntry* find(uint32_t key) const {
        for (size_t i = 0; i < n_; ++i) {
            if (e_[i].key == key) return &e_[i];
        }
        return nullptr;
    }
    size_t size() const { return n_; }
    const SnapEntry& at(size_t i) const { return e_[i < n_ ? i : 0]; }
    size_t not_ready() const {
        size_t k = 0;
        for (size_t i = 0; i < n_; ++i) k += e_[i].ready ? 0 : 1;
        return k;
    }
    uint32_t overflow() const { return overflow_; }
    uint64_t taken_us() const { return taken_us_; }

private:
    SnapEntry e_[N] = {};
    size_t n_ = 0;
    uint32_t overflow_ = 0;
    uint64_t taken_us_ = 0;
};

// ---------------------------------------------------------------- the front key and the decision

// The wait list's first key as 0x12 sees it now.
struct Front {
    uint32_t key = 0;
    uintptr_t actor = 0;   // Find's result (0 = none: 0x12 removes it natively)
    uint16_t state = 0;    // +0x5E
    uint8_t c2 = 0, cf = 0;
    uint32_t control = 0;  // +0x90
    bool vf_native = false;  // the actor's vf1D8 is 0x4FE240 (+0xC2 && +0xCF), the readiness read here
};

constexpr bool front_ready(const Front& f) { return f.c2 != 0 && f.cf != 0; }
// +0x5E: 8 = waiting for its login, 0x10 = logged in (copias-grupo-invisibles.md §2.2).
constexpr bool waiting_login(uint16_t state) { return (state & 0x8) != 0 && (state & 0x10) == 0; }
constexpr bool world_actor_key(uint32_t key) { return (key >> 28) == 0xB; }
constexpr bool user_key(uint32_t key) { return (key >> 28) == 0x9; }

struct Context {
    bool reload = false;        // is_reload_trip
    uint64_t in_wait_us = 0;    // time in 0x12 so far
    uint64_t grace_us = 0;      // the native gets this long first
    uint32_t local_user = 0;    // the guest's user key (0 = unknown)
    uint32_t own_key = 0;       // the guest's controlled actor key
    uint32_t dropped = 0;       // this trip
    uint32_t cap = 0;           // per trip
};

enum class Verdict : uint8_t {
    Drop,            // every condition holds
    NoFront,         // empty list
    Missing,         // Find gives none: 0x12 removes it natively
    Ready,           // 0x12 removes it natively
    NotReload,       // not a reload in the same field (a 2174 came, or no 2643)
    Grace,           // in 0x12 for less than the grace period
    OwnCharacter,    // the guest's own controlled actor
    NotWorldActor,   // not a 0xB... key (characters and users are never touched)
    LocalUnknown,    // the guest's user key is not known yet
    ControlLocal,    // controlled by this guest
    ControlNotUser,  // +0x90 is not a user key
    LoggedIn,        // +0x5E is not "waiting for its login" (8): it may still become ready natively
    VfOther,         // its vf1D8 is not 0x4FE240: its readiness is not the one read here
    NotInSnapshot,   // it was not in the wait list when the trip began (or became not ready during the trip)
    WasReady,        // it was ready when the trip began (it became not ready during the trip)
    OtherInstance,   // same key, another actor than the one in the snapshot
    CapReached,      // this trip already dropped `cap` keys
    kCount
};

inline const char* verdict_text(Verdict v) {
    switch (v) {
        case Verdict::Drop: return "DROP (not ready since before the trip)";
        case Verdict::NoFront: return "list empty";
        case Verdict::Missing: return "missing (0x12 removes it natively)";
        case Verdict::Ready: return "ready (0x12 removes it natively)";
        case Verdict::NotReload: return "kept: not a reload in the same field (2174 or no 2643)";
        case Verdict::Grace: return "kept: inside the grace period";
        case Verdict::OwnCharacter: return "kept: the guest's own character";
        case Verdict::NotWorldActor: return "kept: not a world actor key (0xB...)";
        case Verdict::LocalUnknown: return "kept: the guest's user key is unknown";
        case Verdict::ControlLocal: return "kept: controlled by this guest";
        case Verdict::ControlNotUser: return "kept: its controller (+0x90) is not a user key";
        case Verdict::LoggedIn: return "kept: not waiting for its login (+0x5E not 8)";
        case Verdict::VfOther: return "kept: its vf1D8 is not 0x4FE240";
        case Verdict::NotInSnapshot: return "kept: not in the wait list when the trip began";
        case Verdict::WasReady: return "kept: it was ready when the trip began";
        case Verdict::OtherInstance: return "kept: another actor with that key than when the trip began";
        case Verdict::CapReached: return "kept: the per-trip cap was reached";
        case Verdict::kCount: break;
    }
    return "?";
}

// Mode 1 removes the front key only with Drop. The order of the checks only chooses which reason is logged.
inline Verdict judge(const Front& f, const SnapEntry* snap, const Context& c) {
    if (!f.key) return Verdict::NoFront;
    if (!f.actor) return Verdict::Missing;
    if (front_ready(f)) return Verdict::Ready;
    if (!c.reload) return Verdict::NotReload;
    if (c.own_key && f.key == c.own_key) return Verdict::OwnCharacter;
    if (!world_actor_key(f.key)) return Verdict::NotWorldActor;
    if (!c.local_user) return Verdict::LocalUnknown;
    if (f.control == c.local_user) return Verdict::ControlLocal;
    if (!user_key(f.control)) return Verdict::ControlNotUser;
    if (!waiting_login(f.state)) return Verdict::LoggedIn;
    if (!f.vf_native) return Verdict::VfOther;
    if (!snap) return Verdict::NotInSnapshot;
    if (snap->ready) return Verdict::WasReady;
    if (snap->actor != f.actor) return Verdict::OtherInstance;
    if (c.in_wait_us < c.grace_us) return Verdict::Grace;
    if (c.dropped >= c.cap) return Verdict::CapReached;
    return Verdict::Drop;
}

// ---------------------------------------------------------------- the removal (0x779EF7..0x779F0B)

// Mem: bool u32(uintptr_t, uint32_t&), bool ptr(uintptr_t, uintptr_t&), bool put32(uintptr_t, uint32_t).
//   0x779EF7  mov eax,[rsi+0x80] ; sub eax,1 ; je 0x779F0B
//   0x779F02  mov rdx,[rsi+0x78] ; mov ecx,[rdx+rax*4] ; mov [rdx],ecx
//   0x779F0B  add dword [rsi+0x80],-1
// rsi = C. Returns false (nothing written) when the count is 0 or a read fails; `expected_key` must still be the
// front key (the caller read it under the same lock).
struct WaitListLayout {
    static constexpr uint32_t kKeys = 0x78, kCount = 0x80;
};

template <class Mem>
bool swap_remove_front(Mem& m, uintptr_t c, uint32_t expected_key, uint32_t max_count = 1u << 16) {
    using L = WaitListLayout;
    uint32_t n = 0, front = 0;
    uintptr_t keys = 0;
    if (!c || !m.u32(c + L::kCount, n) || n == 0 || n > max_count) return false;
    if (!m.ptr(c + L::kKeys, keys) || !keys || !m.u32(keys, front) || front != expected_key) return false;
    if (n - 1 != 0) {
        uint32_t last = 0;
        if (!m.u32(keys + 4 * static_cast<uintptr_t>(n - 1), last)) return false;
        if (!m.put32(keys, last)) return false;
    }
    return m.put32(c + L::kCount, n - 1);
}

// ---------------------------------------------------------------- when a blocker is reported

// In 0x12: a report at +2, +10, +30 and +60 s, then every 60 s (the line budget bounds the rest).
class ReportClock {
public:
    void reset() { next_ = 0; }
    bool due(uint64_t in_wait_us) {
        const uint64_t at = at_us(next_);
        if (in_wait_us < at) return false;
        ++next_;
        return true;
    }
    static constexpr uint64_t at_us(uint32_t i) {
        constexpr uint64_t first[4] = {2'000'000, 10'000'000, 30'000'000, 60'000'000};
        return i < 4 ? first[i] : 60'000'000ull * (i - 2);
    }

private:
    uint32_t next_ = 0;
};

// ---------------------------------------------------------------- how long a key has been not ready

// Fed by guest_actor_diag's 5 s walk of the wait list (its own thread): the first time a key was seen not ready
// (cleared when it is seen ready). Read by the line formatter. Lock-free, bounded, open addressing.
template <size_t N>
class AgeTable {
    static_assert(N && (N & (N - 1)) == 0, "N must be a power of two");

public:
    // ready: forget the age; not ready: keep the first time.
    void note(uint32_t key, bool ready, uint64_t now_ms) {
        if (!key) return;
        size_t i = hash(key);
        for (size_t probe = 0; probe < N; ++probe, i = (i + 1) & (N - 1)) {
            uint32_t cur = keys_[i].load(std::memory_order_relaxed);
            if (cur == 0) {
                if (ready) return;  // never seen not ready: nothing to keep
                if (!keys_[i].compare_exchange_strong(cur, key, std::memory_order_relaxed) && cur != key) continue;
                cur = key;
            }
            if (cur != key) continue;
            if (ready) {
                since_[i].store(0, std::memory_order_relaxed);
            } else {
                uint64_t zero = 0;
                since_[i].compare_exchange_strong(zero, now_ms ? now_ms : 1, std::memory_order_relaxed);
            }
            return;
        }
        full_.fetch_add(1, std::memory_order_relaxed);
    }
    // 0 = not known as not ready.
    uint64_t since(uint32_t key) const {
        if (!key) return 0;
        size_t i = hash(key);
        for (size_t probe = 0; probe < N; ++probe, i = (i + 1) & (N - 1)) {
            const uint32_t cur = keys_[i].load(std::memory_order_relaxed);
            if (cur == key) return since_[i].load(std::memory_order_relaxed);
            if (cur == 0) return 0;
        }
        return 0;
    }
    uint64_t full() const { return full_.load(std::memory_order_relaxed); }

private:
    static size_t hash(uint32_t key) { return (static_cast<size_t>(key) * 0x9E3779B1u) & (N - 1); }
    std::atomic<uint32_t> keys_[N] = {};
    std::atomic<uint64_t> since_[N] = {};
    std::atomic<uint64_t> full_{0};
};

}  // namespace cdmp::session2::grw
