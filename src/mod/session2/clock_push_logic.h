#pragma once
// P8 session 25, hour and weather (docs/research/noche/clima-hora.md §5): the pure part of clock_push.h.
// Build 1.0.0.2976. No game, no threads: unit-tested in tests/optd_clock_push_tests.cpp.
//
// FieldTime (32 B in memory, the server global exe+0x6A6D238 and the client one exe+0x6A6D218):
//   +0x00 u32 day | +0x04 hour | +0x08 minute | +0x0C second | +0x10 millisecond | +0x14 u8 | +0x15 pad |
//   +0x16 u16 scale (game ms per real ms, 12) | +0x18 u64 anchor (the server tick, ms, the base is valid at).
// On the wire it is 31 B, the padding left out (FieldTime::Serialize 0x20CF330 / ::Deserialize 0x20CF3F0, .xtls
// 0xE2B2260: 4,4,4,4,4 | 1 -> +0x14 | 2 -> +0x16 | 8 -> +0x18).
//
// SetFieldTimeAck 2816 (S->C), as the server writes it (0x299AED0: header 00 0B FF, u16 length, then
// u32 [actor+0x60] and FieldTime::Serialize) and as the client reads it (.xtls 0xA41D560: the payload must be
// consumed exactly, actor key then FieldTime; the time is applied to the client's time manager with
// 0xA38B00(mgr, &ft, 1) whether the actor is found or not):
//   00 0B FF 23 00 | u32 character key | FieldTime (31 B)          = 40 bytes, payload 35.
// LoadingCompleteAck 2762 (S->C): u32 actor | u8 flag | u64 now (13 B).

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace cdmp::session2::ckp {

inline constexpr uint16_t kOpLoadingCompleteAck = 2762;
inline constexpr uint16_t kOpSetFieldTimeAck = 2816;
inline constexpr uint16_t kOpSyncWeatherStateReq = 2560;
inline constexpr size_t kHeader = 5;          // u16 opcode | i8 slot (-1) | u16 payload length
inline constexpr size_t kFieldTimeMem = 32;
inline constexpr size_t kFieldTimeWire = 31;
inline constexpr size_t kAckPayload = 4 + kFieldTimeWire;   // 35 (0x23)
inline constexpr size_t kAckPacket = kHeader + kAckPayload;  // 40
inline constexpr size_t kLoadingAckPayload = 13;

// The game's addresses (exe+, build 2976), read only.
inline constexpr uintptr_t kServerClock = 0x6A6D238;   // server FieldTime (the host's world clock)
inline constexpr uintptr_t kClientClock = 0x6A6D218;   // client FieldTime (what this client's sky follows)
inline constexpr uintptr_t kWorldSystem = 0x6D69190;   // [exe+] -> world system; +0x28 -> the time manager
inline constexpr uint32_t kWorldTimeMgr = 0x28;
inline constexpr uint32_t kMgrFlags = 0x28;            // +0x28 active (set by the 2762), +0x29 private calendar,
                                                       // +0x2A its previous value, +0x2B off
inline constexpr uint32_t kMgrPrivateTime = 0x30;      // FieldTime of the private calendar (+0x30..+0x4F)
inline constexpr uint32_t kMgrDumpSize = 0x30;         // +0x28 .. +0x57
inline constexpr uintptr_t kClockBias = 0x6D69438;     // i64: server tick - this client's FrameClock (2188, 0xBDF310)

struct FieldTime {
    uint32_t day = 0, hour = 0, minute = 0, second = 0, ms = 0;
    uint8_t b14 = 0;
    uint16_t scale = 0;
    uint64_t anchor = 0;
};

inline FieldTime decode(const uint8_t* mem) {
    FieldTime t;
    uint32_t v[5] = {};
    std::memcpy(v, mem, sizeof v);
    t.day = v[0];
    t.hour = v[1];
    t.minute = v[2];
    t.second = v[3];
    t.ms = v[4];
    t.b14 = mem[0x14];
    std::memcpy(&t.scale, mem + 0x16, 2);
    std::memcpy(&t.anchor, mem + 0x18, 8);
    return t;
}

// A clock worth handing to user 2: a running calendar (never the parking field's day 0 24:00 scale 0 that froze
// the world in C1e-C4b, nor an unread / zeroed global).
inline bool plausible(const FieldTime& t) {
    return t.hour < 24 && t.minute < 60 && t.second < 60 && t.ms < 1000 && t.scale != 0 && t.scale <= 1000 &&
           t.anchor != 0;
}

// The base as game milliseconds since day 0 00:00 (the linear model; valid as a comparison between two copies of
// the same base, or within a day stretch: noche/clima-hora.md §7, 0x20D3360 has its own night conversion).
inline int64_t base_game_ms(const FieldTime& t) {
    return ((((static_cast<int64_t>(t.day) * 24 + t.hour) * 60 + t.minute) * 60 + t.second) * 1000) + t.ms;
}
// "t0": the calendar the clock extrapolates to at server tick 0 (base - anchor x scale). Two clocks that show the
// same time at the same server tick have the same t0, whatever their base and anchor: the two-PC check compares
// the guest's client clock with the host's server clock by this one number.
inline int64_t t0_game_ms(const FieldTime& t) {
    return base_game_ms(t) - static_cast<int64_t>(t.anchor) * static_cast<int64_t>(t.scale);
}

// Two reads of the 32 bytes that agree (a publish between them is a torn copy).
inline bool consistent(const uint8_t* a, const uint8_t* b) { return std::memcmp(a, b, kFieldTimeMem) == 0; }

inline std::array<uint8_t, kFieldTimeWire> to_wire(const uint8_t* mem) {
    std::array<uint8_t, kFieldTimeWire> w{};
    std::memcpy(w.data(), mem, 0x15);              // day .. millisecond, the u8 at +0x14
    std::memcpy(w.data() + 0x15, mem + 0x16, 2);   // scale
    std::memcpy(w.data() + 0x17, mem + 0x18, 8);   // anchor
    return w;
}

// The native 2816 for `key` with the clock `mem` (32 B, memory layout).
inline std::array<uint8_t, kAckPacket> make_set_field_time_ack(uint32_t key, const uint8_t* mem) {
    std::array<uint8_t, kAckPacket> p{};
    p[0] = static_cast<uint8_t>(kOpSetFieldTimeAck & 0xFF);
    p[1] = static_cast<uint8_t>(kOpSetFieldTimeAck >> 8);
    p[2] = 0xFF;
    p[3] = static_cast<uint8_t>(kAckPayload & 0xFF);
    p[4] = static_cast<uint8_t>(kAckPayload >> 8);
    std::memcpy(p.data() + kHeader, &key, 4);
    const auto w = to_wire(mem);
    std::memcpy(p.data() + kHeader + 4, w.data(), w.size());
    return p;
}

// A whole 2816 back into its key and its clock (memory layout, the padding 0). False for anything else.
inline bool parse_set_field_time_ack(std::span<const uint8_t> p, uint32_t& key, uint8_t* mem) {
    if (p.size() != kAckPacket || static_cast<uint16_t>(p[0] | (p[1] << 8)) != kOpSetFieldTimeAck ||
        static_cast<size_t>(p[3] | (p[4] << 8)) != kAckPayload || p[2] != 0xFF) {
        return false;
    }
    std::memcpy(&key, p.data() + kHeader, 4);
    const uint8_t* w = p.data() + kHeader + 4;
    std::memset(mem, 0, kFieldTimeMem);
    std::memcpy(mem, w, 0x15);
    std::memcpy(mem + 0x16, w + 0x15, 2);
    std::memcpy(mem + 0x18, w + 0x17, 8);
    return true;
}

struct LoadingAck {
    uint32_t actor = 0;
    uint8_t flag = 0;
    uint64_t now = 0;
};
inline std::optional<LoadingAck> parse_loading_ack(std::span<const uint8_t> payload) {
    if (payload.size() < kLoadingAckPayload) return std::nullopt;
    LoadingAck a;
    std::memcpy(&a.actor, payload.data(), 4);
    a.flag = payload[4];
    std::memcpy(&a.now, payload.data() + 5, 8);
    return a;
}

// W1: a guest Req the host's server never gets while block_guest_weather=1 (its 2560 writes the host field's
// weather flags, 0x20E6FE0, with no owner check; the server answers nothing to it).
inline bool guest_req_blocked_by_env(uint16_t opcode, bool block_weather) {
    return block_weather && opcode == kOpSyncWeatherStateReq;
}

// The 2816 actor key: user 2's character (its MainPlayerDataStartAck); while unknown, the 2762's own actor.
inline uint32_t push_key(uint32_t player_key, uint32_t ack_actor) { return player_key ? player_key : ack_actor; }

// ---------------------------------------------------------------- T2: when the world clock is pushed again
//
// The clock gate's watch thread samples the world clock once a second (one good double read each). Every 2762 of
// user 2 re-anchors its client, so T1 first forgets what user 2 was given (forget), then tells this state what
// it did (sent / failed; nothing on its early returns: no key, unreadable or implausible clock). T2 pushes when:
//   - user 2 is in the world (its 2762 written: phase Stable; never during its loads),
//   - the clock differs from the last one user 2 was given, or user 2 has been given nothing since its last 2762
//     (a host publish; T1's push refused by the queue; T1 returned early),
//   - the clock has not changed for settle_ms (a burst of publishes goes once, settled),
//   - it is plausible, and at least min_gap_ms after the last try.
// Not thread safe: the caller locks. Probe mode drives it the same way ("sent" = would have been sent).
class PushWatch {
public:
    struct Config {
        uint32_t settle_ms = 900;    // one sample (1 s) without another change
        uint32_t min_gap_ms = 2000;  // at most one T2 try every 2 s
    };
    enum class Verdict : uint8_t { NotInWorld, Same, Unsettled, Implausible, TooSoon, Push };

    PushWatch() : PushWatch(Config{}) {}
    explicit PushWatch(Config c) : cfg_(c) {}

    Verdict sample(const uint8_t* mem, bool in_world, uint64_t now_ms) {
        observe(mem, now_ms);
        if (!in_world) return Verdict::NotInWorld;
        if (have_sent_ && consistent(mem, sent_)) return Verdict::Same;
        if (now_ms - changed_at_ < cfg_.settle_ms) return Verdict::Unsettled;
        if (!plausible(decode(mem))) return Verdict::Implausible;
        if (tried_ && now_ms - last_try_ < cfg_.min_gap_ms) return Verdict::TooSoon;
        return Verdict::Push;
    }
    // T1 or T2: this clock went to user 2 (or would have, probe). It was also just read: a sample of it.
    void sent(const uint8_t* mem, uint64_t now_ms) {
        observe(mem, now_ms);
        std::memcpy(sent_, mem, kFieldTimeMem);
        have_sent_ = true;
        tried_ = true;
        last_try_ = now_ms;
    }
    // A push the queue refused: user 2 has NOT been given this clock (whatever it had before was re-anchored or
    // superseded); T2 tries again after min_gap_ms, also when the clock equals the last successful push.
    void failed(uint64_t now_ms) {
        have_sent_ = false;
        tried_ = true;
        last_try_ = now_ms;
    }
    // A 2762 of user 2 re-anchored its client: what it was given before no longer holds (T1, before anything else).
    void forget() { have_sent_ = false; }
    // A new guest epoch, or user 2 gone: user 2 has been given nothing.
    void reset() {
        have_sent_ = false;
        tried_ = false;
        last_try_ = 0;
    }
    bool has_sent() const { return have_sent_; }
    const Config& config() const { return cfg_; }

private:
    // The settle time counts from the first time this clock value was seen (by a sample or by T1's read).
    void observe(const uint8_t* mem, uint64_t now_ms) {
        if (!have_seen_ || !consistent(mem, seen_)) {
            std::memcpy(seen_, mem, kFieldTimeMem);
            have_seen_ = true;
            changed_at_ = now_ms;
        }
    }

    Config cfg_;
    uint8_t seen_[kFieldTimeMem] = {};
    uint8_t sent_[kFieldTimeMem] = {};
    bool have_seen_ = false, have_sent_ = false, tried_ = false;
    uint64_t changed_at_ = 0, last_try_ = 0;
};

inline const char* verdict_name(PushWatch::Verdict v) {
    switch (v) {
        case PushWatch::Verdict::NotInWorld: return "user 2 not in the world";
        case PushWatch::Verdict::Same: return "already given";
        case PushWatch::Verdict::Unsettled: return "unsettled";
        case PushWatch::Verdict::Implausible: return "implausible";
        case PushWatch::Verdict::TooSoon: return "too soon";
        case PushWatch::Verdict::Push: return "push";
    }
    return "?";
}

}  // namespace cdmp::session2::ckp
