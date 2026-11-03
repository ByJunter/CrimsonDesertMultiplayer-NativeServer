#pragma once
// P8 session 9, P1 (docs/research/pendientes/p8-s9-combate-montura.md §2, §6 "Mascota"; the implementation:
// docs/research/pendientes/p8-impl-s9-movimiento.md §2): the pure logic of [session2] child_reannounce, with
// no game access, so the tests can run it.
//
// The fault [V, session 9]: child_settle held the host's pet 1500 ms and released it natively; the guest's
// client still failed CreateOwnedActor 18 ms later ("Actor 생성에 실패했습니다" from exe+0x8BDF7C, value
// 0x90100000B0100002 = owner user << 32 | actor). Nothing announces it again: the pet never exists there.
//
// child_reannounce = 0 (default) | probe | 1, on BOTH PCs:
//   GUEST: each such failure (game_errors keeps their values) of a child 0xB... owned by a user 0x9... asks the
//          host, through the mod's own tunnel control packet (opcode 1, never a Troc message), to re-announce
//          that key: at most kMaxAttempts per key, never twice within kMinGapMs (the same failure seen again).
//          probe: logged, nothing sent.
//   HOST:  tunnel_on_packet takes the control packet out before anything reaches the server; with 1 the child
//          goes into child_settle's own table for the observer session 2 with a release not before
//          backoff_ms(attempt) (2, 4, 8 s): the release is child_settle's native NotifyAppear([session 2], [K])
//          on its usual threads. Only the HOST's own children (owner = the host's user), at most kMaxAttempts
//          per key and guest epoch. probe: logged, nothing queued.
// A creation failure leaves no half-built actor on the client (c1-actor-rechazado.md §3): a re-announce does
// not duplicate anything.
//
// Control packet (ToHostServer): u16 opcode 1 | i8 -1 | u16 len 10 | u8 kControlReannounce | u32 key |
// u32 owner user | u8 attempt (1..kMaxAttempts). An older host drops it as no server Req (counted there).

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "mod/session2/user2_funnels_logic.h"

namespace cdmp::session2::ran {

using u2f::Mode;
using u2f::mode_name;
using u2f::parse_mode;

inline constexpr uint8_t kControlReannounce = 3;  // troc_frame.h: 1 host ready, 2 host dead (ToGuestClient)
inline constexpr size_t kPayloadSize = 10;
inline constexpr uint8_t kMaxAttempts = 3;
inline constexpr uint64_t kMinGapMs = 1000;

// The host's wait before re-announcing (the guest's character still loading is the probable cause, c5).
inline uint32_t backoff_ms(uint8_t attempt) {
    if (attempt <= 1) return 2000;
    if (attempt == 2) return 4000;
    return 8000;
}

// The failure value game_errors keeps: owner user (0x9...) << 32 | actor (0xB...). nullopt: not that form.
struct Failure {
    uint32_t key = 0;
    uint32_t owner = 0;
};
inline std::optional<Failure> split_failure(uint64_t v) {
    const auto key = static_cast<uint32_t>(v);
    const auto owner = static_cast<uint32_t>(v >> 32);
    if ((key >> 28) != 0xB || (owner >> 28) != 0x9) return std::nullopt;
    return Failure{key, owner};
}

inline std::array<uint8_t, kPayloadSize> encode(uint32_t key, uint32_t owner, uint8_t attempt) {
    std::array<uint8_t, kPayloadSize> p{};
    p[0] = kControlReannounce;
    for (int i = 0; i < 4; ++i) p[1 + i] = static_cast<uint8_t>(key >> (8 * i));
    for (int i = 0; i < 4; ++i) p[5 + i] = static_cast<uint8_t>(owner >> (8 * i));
    p[9] = attempt;
    return p;
}

// The whole frame as the guest sends it: opcode 1 (no Troc message uses it: the game's lowest is 1002), slot -1,
// payload length exactly kPayloadSize, first byte kControlReannounce. Only this exact form is taken out of the
// guest's stream by the host; any other opcode-1 frame goes on to the usual checks (dropped as no server Req).
inline constexpr uint16_t kControlOpcode = 1;  // troc_frame.h kControlOpcode
inline constexpr size_t kFrameSize = 5 + kPayloadSize;
inline bool is_reannounce_frame(std::span<const uint8_t> f) {
    return f.size() == kFrameSize && f[0] == (kControlOpcode & 0xFF) && f[1] == (kControlOpcode >> 8) &&
           f[2] == 0xFF && f[3] == kPayloadSize && f[4] == 0 && f[5] == kControlReannounce;
}

struct Request {
    uint32_t key = 0;
    uint32_t owner = 0;
    uint8_t attempt = 0;
};
inline std::optional<Request> decode(std::span<const uint8_t> payload) {
    if (payload.size() != kPayloadSize || payload[0] != kControlReannounce) return std::nullopt;
    Request r;
    for (int i = 0; i < 4; ++i) r.key |= static_cast<uint32_t>(payload[1 + i]) << (8 * i);
    for (int i = 0; i < 4; ++i) r.owner |= static_cast<uint32_t>(payload[5 + i]) << (8 * i);
    r.attempt = payload[9];
    if ((r.key >> 28) != 0xB || (r.owner >> 28) != 0x9 || r.attempt == 0 || r.attempt > kMaxAttempts) {
        return std::nullopt;
    }
    return r;
}

// ---------------------------------------------------------------- per key: attempts (not thread-safe)

enum class Verdict : uint8_t {
    Ask,         // a new attempt (attempt in the out parameter)
    TooSoon,     // the same failure again within kMinGapMs
    Exhausted,   // kMaxAttempts already asked / accepted
    NotAChild,   // not an owner-0x9 / child-0xB value
    Full,        // the table is full (no attempt)
    EpochCap,    // kMaxPerEpoch attempts already in this epoch / connection (any keys)
};
inline constexpr size_t kVerdicts = 6;
inline const char* verdict_name(Verdict v) {
    switch (v) {
        case Verdict::Ask: return "ask";
        case Verdict::TooSoon: return "too soon";
        case Verdict::Exhausted: return "attempts exhausted";
        case Verdict::NotAChild: return "not a user's child";
        case Verdict::Full: return "table full";
        case Verdict::EpochCap: return "epoch cap reached";
    }
    return "?";
}

// All keys together, per guest epoch (host) / client connection (guest): a bound on what a misbehaving or
// looping peer can make the host do (review 2026-10-10).
inline constexpr uint32_t kMaxPerEpoch = 16;

// A small table of keys and how many attempts each had. The guest uses it for its requests, the host for
// what it accepted (per epoch: reset()). peek() decides without changing anything; commit() records an attempt
// (the host commits only once child_settle confirmed the key is the host's child).
template <size_t N>
class Attempts {
public:
    // The verdict and the attempt number (1..kMaxAttempts) an Ask would get; nothing is recorded.
    Verdict peek(uint32_t key, uint64_t now_ms, uint8_t& attempt) const {
        attempt = 0;
        if ((key >> 28) != 0xB) return Verdict::NotAChild;
        if (total_ >= kMaxPerEpoch) return Verdict::EpochCap;
        const Slot* s = find(key);
        if (!s) {
            if (!has_free()) return Verdict::Full;
            attempt = 1;
            return Verdict::Ask;
        }
        if (s->n >= kMaxAttempts) return Verdict::Exhausted;
        if (s->n && now_ms >= s->last_ms && now_ms - s->last_ms < kMinGapMs) return Verdict::TooSoon;
        attempt = static_cast<uint8_t>(s->n + 1);
        return Verdict::Ask;
    }
    // Records one attempt of `key` (after a peek() that said Ask). False: no room (cannot happen after Ask).
    bool commit(uint32_t key, uint64_t now_ms) {
        Slot* s = find(key);
        if (!s) {
            s = free_slot();
            if (!s) return false;
            *s = Slot{key, 0, 0};
        }
        if (s->n < kMaxAttempts) ++s->n;
        s->last_ms = now_ms ? now_ms : 1;
        ++total_;
        return true;
    }
    // peek + commit (the guest: every Ask is sent).
    Verdict next(uint32_t key, uint64_t now_ms, uint8_t& attempt) {
        const Verdict v = peek(key, now_ms, attempt);
        if (v == Verdict::Ask) commit(key, now_ms);
        return v;
    }
    // Host: the attempt number for the guest's request (peek only), at least the guest's own number.
    Verdict accept_peek(uint32_t key, uint8_t asked, uint64_t now_ms, uint8_t& attempt) const {
        const Verdict v = peek(key, now_ms, attempt);
        if (v == Verdict::Ask && asked > attempt) attempt = asked > kMaxAttempts ? kMaxAttempts : asked;
        return v;
    }
    uint8_t count(uint32_t key) const {
        for (const Slot& s : s_) {
            if (s.key == key) return s.n;
        }
        return 0;
    }
    uint32_t total() const { return total_; }
    void reset() {
        s_ = {};
        total_ = 0;
    }

private:
    struct Slot {
        uint32_t key = 0;
        uint8_t n = 0;
        uint64_t last_ms = 0;
    };
    Slot* find(uint32_t key) {
        for (Slot& s : s_) {
            if (s.key == key) return &s;
        }
        return nullptr;
    }
    const Slot* find(uint32_t key) const {
        for (const Slot& s : s_) {
            if (s.key == key) return &s;
        }
        return nullptr;
    }
    bool has_free() const {
        for (const Slot& s : s_) {
            if (!s.key) return true;
        }
        return false;
    }
    Slot* free_slot() {
        for (Slot& s : s_) {
            if (!s.key) return &s;
        }
        return nullptr;
    }
    std::array<Slot, N> s_{};
    uint32_t total_ = 0;
};

}  // namespace cdmp::session2::ran
