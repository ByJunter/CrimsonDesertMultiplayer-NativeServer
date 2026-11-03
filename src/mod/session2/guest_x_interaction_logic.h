#pragma once
// P8 session 13, C1 (docs/research/pendientes/p8-impl-tenderos-c1.md), GUEST: pure logic of
// [session2] guest_x_interaction_stage = 0 | probe | 1. No game call, no allocation, no CRT in what the hooks use.
//
// The interaction prompt's check of a dynamic entry 0x2126920 looks the entry's stage id ([entry+0x20]) up in the
// local player's own manager M (ClientField::vf48). Entries SetInteraction added from the host character's manager X
// name a stage of X: the lookup fails and the shopkeeper never offers its talk (p8-s12-tenderos.md §4). At 0x2126A1F
// (RAX = M), when the id is NOT in M but IS in a known X, '1' sets RAX := X; 'probe' only decides and counts.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cdmp::session2::gxi {

enum class Mode : uint8_t { Off, Probe, On };
inline Mode parse_mode(std::string_view s, bool* bad = nullptr) {
    if (bad) *bad = false;
    if (s.empty() || s == "0") return Mode::Off;
    if (s == "probe") return Mode::Probe;
    if (s == "1") return Mode::On;
    if (bad) *bad = true;
    return Mode::Off;
}
inline const char* mode_name(Mode m) {
    switch (m) {
        case Mode::Off: return "0";
        case Mode::Probe: return "probe";
        case Mode::On: return "1";
    }
    return "?";
}

// ---------------------------------------------------------------- the known X (host characters' managers)

// Up to N managers of OTHER characters this client routed scenes into (guest_x_veto notes each X it sees: a 2981
// routed by guest_seq_route, a handler run with G = X). Lock-free: a slot is claimed with a CAS on x; when the table is
// full the oldest note is replaced (round robin). Each slot keeps the character key it was noted with.
template <size_t N>
class XSlots {
public:
    void note(uintptr_t x, uint32_t key) {
        if (!x) return;
        for (size_t i = 0; i < N; ++i) {
            if (x_[i].load(std::memory_order_acquire) == x) {
                key_[i].store(key, std::memory_order_relaxed);
                return;
            }
        }
        for (size_t i = 0; i < N; ++i) {
            uintptr_t expected = 0;
            if (x_[i].compare_exchange_strong(expected, x, std::memory_order_acq_rel)) {
                key_[i].store(key, std::memory_order_relaxed);
                notes_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (expected == x) return;
        }
        const size_t i = next_.fetch_add(1, std::memory_order_relaxed) % N;
        x_[i].store(x, std::memory_order_release);
        key_[i].store(key, std::memory_order_relaxed);
        replaced_.fetch_add(1, std::memory_order_relaxed);
    }
    // A snapshot (slot order); returns how many were copied.
    size_t snapshot(uintptr_t* xs, uint32_t* keys, size_t cap) const {
        size_t n = 0;
        for (size_t i = 0; i < N && n < cap; ++i) {
            const uintptr_t x = x_[i].load(std::memory_order_acquire);
            if (!x) continue;
            xs[n] = x;
            keys[n] = key_[i].load(std::memory_order_relaxed);
            ++n;
        }
        return n;
    }
    void clear() {
        for (size_t i = 0; i < N; ++i) {
            x_[i].store(0, std::memory_order_release);
            key_[i].store(0, std::memory_order_relaxed);
        }
    }
    uint64_t notes() const { return notes_.load(std::memory_order_relaxed); }
    uint64_t replaced() const { return replaced_.load(std::memory_order_relaxed); }
    static constexpr size_t capacity() { return N; }

private:
    std::atomic<uintptr_t> x_[N] = {};
    std::atomic<uint32_t> key_[N] = {};
    std::atomic<size_t> next_{0};
    std::atomic<uint64_t> notes_{0}, replaced_{0};
};

// One candidate X as the hook sees it now (SEH reads): the identity checks guest_seq_play uses for its X.
struct XCheck {
    uintptr_t x = 0;
    uintptr_t vt = 0, want_vt = 0;  // [X] and exe + 0x55C9720 (ClientSequencerStageManager)
    uint32_t key = 0;               // [[X+0x18]+0x60]: the character X belongs to
    uintptr_t actor_mgr = 0;        // [[[[X+0x18]+0x68]+0xA8]+0x18]: that character's manager now (must be X)
};
enum class XState : uint8_t { Ok, NotManager, IsM, OwnCharacter, NoCharacter, NotItsManager, Count };
inline XState check_x(const XCheck& c, uintptr_t m, uint32_t m_key, uint32_t own_key) {
    if (!c.x || c.vt != c.want_vt || !c.want_vt) return XState::NotManager;
    if (c.x == m) return XState::IsM;
    if (!c.key) return XState::NoCharacter;
    if (c.key == m_key || (own_key && c.key == own_key)) return XState::OwnCharacter;
    if (c.actor_mgr != c.x) return XState::NotItsManager;
    return XState::Ok;
}
inline const char* xstate_name(XState s) {
    switch (s) {
        case XState::Ok: return "ok";
        case XState::NotManager: return "not a ClientSequencerStageManager";
        case XState::IsM: return "is M";
        case XState::OwnCharacter: return "the own character's";
        case XState::NoCharacter: return "no character";
        case XState::NotItsManager: return "its character has another manager now";
        case XState::Count: break;
    }
    return "?";
}

// ---------------------------------------------------------------- the decision at 0x2126A1F

struct Facts {
    bool client_field = false;  // [rsp+0x48] is a ClientField (this is the client's prompt check, not the server's)
    bool remote = false;        // this connection plays on the host (guest_remote_live)
    uintptr_t m = 0;            // RAX: the field's vf48 (the local player's manager)
    uint64_t id = 0;            // [entry+0x20]
    bool m_read = false;        // the replica of 0x1F6C190 on M could read what it needed
    bool in_m = false;          // the id is a live stage of M
    size_t x_known = 0;         // candidates noted
    size_t x_valid = 0;         // candidates that passed check_x
    bool x_read = false;        // the lookup on at least one valid X could read
    uintptr_t x = 0;            // the valid X whose table holds the id (live)
    bool x_dead = false;        // a valid X holds the id but its instance is dead
};
enum class Verdict : uint8_t {
    NotClient,   // the server's call of the same function (not this module's business)
    NotRemote,   // not playing on the host
    NoM,         // RAX null
    NoId,        // [entry+0x20] & 0x1FFFFFFF == 0 (never reaches A natively; kept for safety)
    InM,         // the native lookup will find it: nothing to do
    MUnread,     // M's table could not be read: do nothing
    NoX,         // no valid X known
    NotInX,      // in no valid X either (an entry of nobody we know)
    DeadInX,     // in X but dead: the native lookup on X would fail too
    Redirect,    // in X, not in M
    Count
};
inline Verdict decide(const Facts& f) {
    if (!f.client_field) return Verdict::NotClient;
    if (!f.remote) return Verdict::NotRemote;
    if (!f.m) return Verdict::NoM;
    if ((f.id & 0x1FFFFFFFull) == 0) return Verdict::NoId;
    if (!f.m_read) return Verdict::MUnread;
    if (f.in_m) return Verdict::InM;
    if (f.x_valid == 0) return Verdict::NoX;
    if (f.x) return Verdict::Redirect;
    if (f.x_dead) return Verdict::DeadInX;
    return Verdict::NotInX;
}
inline const char* verdict_name(Verdict v) {
    switch (v) {
        case Verdict::NotClient: return "server call";
        case Verdict::NotRemote: return "not remote";
        case Verdict::NoM: return "no M";
        case Verdict::NoId: return "no stage id";
        case Verdict::InM: return "in M (native)";
        case Verdict::MUnread: return "M unreadable";
        case Verdict::NoX: return "no X known";
        case Verdict::NotInX: return "in neither M nor X";
        case Verdict::DeadInX: return "dead in X";
        case Verdict::Redirect: return "in X, not in M";
        case Verdict::Count: break;
    }
    return "?";
}
enum class Act : uint8_t { None, Would, Redirect };
inline Act act(Mode m, Verdict v) {
    if (v != Verdict::Redirect || m == Mode::Off) return Act::None;
    return m == Mode::On ? Act::Redirect : Act::Would;
}
// The verdicts worth a line (the rest are only counted): the ones about an entry of X.
inline bool worth_a_line(Verdict v) {
    return v == Verdict::Redirect || v == Verdict::NotInX || v == Verdict::DeadInX || v == Verdict::NoX ||
           v == Verdict::MUnread;
}

// ---------------------------------------------------------------- the call in flight (A -> R -> F)

// A thread_local record set at A for a call worth following, read at R (the native lookup's result) and F (the
// function's result). The same call: same entry (R13) and same rsp (the body of 0x2126920 does not move rsp).
struct Call {
    uintptr_t entry = 0, rsp = 0;
    uint64_t id = 0;
    uintptr_t m = 0, x = 0;
    uint8_t verdict = 0, act = 0;
    uint64_t us = 0;
    bool same(uintptr_t e, uintptr_t sp) const { return entry != 0 && entry == e && rsp == sp; }
};

// The function's result (BL at F): 0 = the entry is refused (no prompt for it), 1 / 2 = accepted.
inline const char* result_name(uint8_t bl) {
    switch (bl) {
        case 0: return "0 (refused: no prompt)";
        case 1: return "1 (accepted)";
        case 2: return "2 (accepted)";
        default: return "?";
    }
}

}  // namespace cdmp::session2::gxi
