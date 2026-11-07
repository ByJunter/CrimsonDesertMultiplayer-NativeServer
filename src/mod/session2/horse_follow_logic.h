#pragma once
// P8, the rider and its horse on the other user's client (docs/research/pendientes/p8-movimiento-remoto.md
// §3 F2; docs/research/caballo-coop.md §4; the implementation: docs/research/pendientes/p8-impl-interp-remota.md):
// the pure logic of horse_follow.cpp, with no game access, so the tests can run it.
//
// [session2] horse_follow = 0 (default) | probe | 1, HOST only. A mounted rider sends its MoveActorReq at
// ~36 Hz with the horse's world transform in its Path (+0x94 pos, +0xA0 quat) and the horse as parent
// (+0x8C, u64); the horse itself sends only ~1 Hz. On the observing client (mode 2) the parent is never
// applied: the rider glides smoothly and the horse follows in jumps, behind.
// With 1, for every MoveActorAck 2697 (235 B) of a rider that goes to user 2's session (take_frame), a
// synthetic 2697 of the horse is queued right after it, built from it (caballo-coop.md §4's recipe):
//   +0x00 horse key | +0x04 = rider[+0x94..+0xA0) | +0x10 = rider[+0xA0..+0xB0) | +0x20 = 0
//   +0x4C Path.local = the same pos and quat, scale (1,1,1) | +0x8C parent = -1 | +0x94..+0xD4 = 0
//   ts (+0xD5) and the tail (+0xDD..) = the rider's.
// Never for a horse user 2 controls (its own key: user 2's client would ignore it anyway).
// probe: the counts and cadences only.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include "mod/session2/user2_funnels_logic.h"
#include "common/movement_layout.h"

namespace cdmp::session2::hfl {

using u2f::Mode;
using u2f::mode_name;
using u2f::parse_mode;

// ---------------------------------------------------------------- formats (mirror-ack-formats.md §1)

inline constexpr uint16_t kOpMoveActorReq = 2556;
inline constexpr uint16_t kOpMoveActorAck = 2697;
inline constexpr size_t kReqSize = 514;  // payload
inline constexpr size_t kAckSize = 235;  // payload; any other length is dropped by the client in silence
inline constexpr size_t kHeader = 5;     // u16 opcode | i8 slot | u16 payload_len

inline constexpr size_t kKey = 0x00;         // u32
inline constexpr size_t kPos = 0x04;         // vec3 world
inline constexpr size_t kQuat = 0x10;        // quat
inline constexpr size_t kAux = 0x20;         // u32 (sometimes a key)
inline constexpr size_t kLocalPos = 0x4C;    // Path.local Transform: pos 12, quat 16, scale 12
inline constexpr size_t kLocalQuat = 0x58;
inline constexpr size_t kLocalScale = 0x68;
inline constexpr size_t kParent = 0x8C;      // u64 parent / mount key, all ones = none
inline constexpr size_t kWorldPos = 0x94;    // Path.world Transform (the parent's: the horse's)
inline constexpr size_t kWorldQuat = 0xA0;
inline constexpr size_t kWorldEnd = 0xD4;    // world Transform + the two vec3 after it
inline constexpr size_t kTs = 0xD5;          // u64 ms

inline uint32_t rd32(std::span<const uint8_t> p, size_t at) {
    uint32_t v = 0;
    std::memcpy(&v, p.data() + at, 4);
    return v;
}
inline uint64_t rd64(std::span<const uint8_t> p, size_t at) {
    uint64_t v = 0;
    std::memcpy(&v, p.data() + at, 8);
    return v;
}

// ---------------------------------------------------------------- one Move (Req or Ack)

enum class Shape : uint8_t {
    NotMove,      // another opcode or a wrong length
    OnFoot,       // parent -1
    Ridden,       // a character (0xA) with an 0xB parent: a rider
    OtherParent,  // a parent, but not "character on an 0xB actor" (not followed)
};

struct Move {
    Shape shape = Shape::NotMove;
    uint32_t key = 0;
    uint32_t parent = 0;  // Ridden / OtherParent: the low 32 bits (all keys are u32)
    uint64_t ts = 0;
    bool world_zero = false;  // Ridden: the Path's world position is (0,0,0): no horse transform
};

inline Move read_move(std::span<const uint8_t> payload, size_t expected, movement::Build build = movement::Build::v2976) {
    const auto* l = movement::layout(build);
    Move m;
    if (!l || (expected != kAckSize && expected != kReqSize) ||
        payload.size() != (expected == kAckSize ? l->move_ack : l->move_req)) return m;
    m.key = rd32(payload, kKey);
    m.ts = rd64(payload, l->timestamp);
    const uint64_t parent = rd64(payload, l->parent);
    if (parent == ~0ull) {
        m.shape = Shape::OnFoot;
        return m;
    }
    m.parent = static_cast<uint32_t>(parent);
    const bool character = (m.key >> 28) == 0xA;
    const bool npc_parent = (parent >> 32) == 0 && (m.parent >> 28) == 0xB;
    m.shape = character && npc_parent ? Shape::Ridden : Shape::OtherParent;
    if (m.shape == Shape::Ridden) {
        static constexpr uint8_t kZero[12] = {};
        m.world_zero = std::memcmp(payload.data() + l->world_pos, kZero, 12) == 0;
    }
    return m;
}

// The horse's 2697 from its rider's (whole packet: header + 235 bytes), written into `out` (at least
// header + the selected build's Ack size; no allocation in the native hook).
// False: not a rider's Ack (nothing written).
inline bool make_horse_ack_into(std::span<const uint8_t> rider_packet, uint8_t* out, size_t cap, movement::Build build = movement::Build::v2976) {
    const auto* l = movement::layout(build);
    if (!l) return false;
    if (!out || cap < kHeader + l->move_ack || rider_packet.size() != kHeader + l->move_ack) return false;
    const uint16_t op = static_cast<uint16_t>(rider_packet[0] | (rider_packet[1] << 8));
    const uint16_t len = static_cast<uint16_t>(rider_packet[3] | (rider_packet[4] << 8));
    if (op != kOpMoveActorAck || len != l->move_ack) return false;
    const std::span<const uint8_t> r = rider_packet.subspan(kHeader);
    const Move m = read_move(r, kAckSize, build);
    if (m.shape != Shape::Ridden || m.world_zero) return false;
    std::memcpy(out, rider_packet.data(), kHeader + l->move_ack);
    uint8_t* h = out + kHeader;
    std::memcpy(h + kKey, &m.parent, 4);
    std::memcpy(h + l->pos, r.data() + l->world_pos, 12);
    std::memcpy(h + l->quat, r.data() + l->world_quat, 16);
    std::memset(h + l->aux, 0, 4);
    std::memcpy(h + l->local_pos, r.data() + l->world_pos, 12);
    std::memcpy(h + l->local_quat, r.data() + l->world_quat, 16);
    const float one[3] = {1.0f, 1.0f, 1.0f};
    std::memcpy(h + l->local_scale, one, 12);
    std::memset(h + l->parent, 0xFF, 8);
    std::memset(h + l->world_pos, 0, l->world_end - l->world_pos);
    return true;
}

// The horse's 2697 from its rider's (whole packet: header + 235 bytes). False: not a rider's Ack.
inline bool make_horse_ack(std::span<const uint8_t> rider_packet, std::vector<uint8_t>& out, movement::Build build = movement::Build::v2976) {
    const auto* l = movement::layout(build);
    if (!l) return false;
    uint8_t buf[kHeader + movement::k3527.move_ack];
    if (!make_horse_ack_into(rider_packet, buf, sizeof buf, build)) return false;
    out.assign(buf, buf + kHeader + l->move_ack);
    return true;
}

// P8 session 9, M1 (guest_horse_probe; pendientes/p8-impl-s9-movimiento.md §4): the horse's 2556 from its rider's
// MoveActorReq payload (514 B), the same recipe as make_horse_ack plus the Req's 10-entry history (+0xEA..)
// zeroed; the tail (+0xDE..+0xEA: f32, f32, speed) is the rider's. BUILT AND CHECKED ONLY: never sent.
inline constexpr size_t kReqHistory = 0xEA;
inline bool make_horse_req(std::span<const uint8_t> rider_payload, std::vector<uint8_t>& out, movement::Build build = movement::Build::v2976) {
    const auto* l = movement::layout(build);
    if (!l) return false;
    const Move m = read_move(rider_payload, kReqSize, build);
    if (m.shape != Shape::Ridden || m.world_zero) return false;
    out.assign(rider_payload.begin(), rider_payload.end());
    uint8_t* h = out.data();
    std::memcpy(h + kKey, &m.parent, 4);
    std::memcpy(h + l->pos, rider_payload.data() + l->world_pos, 12);
    std::memcpy(h + l->quat, rider_payload.data() + l->world_quat, 16);
    std::memset(h + l->aux, 0, 4);
    std::memcpy(h + l->local_pos, rider_payload.data() + l->world_pos, 12);
    std::memcpy(h + l->local_quat, rider_payload.data() + l->world_quat, 16);
    const float one[3] = {1.0f, 1.0f, 1.0f};
    std::memcpy(h + l->local_scale, one, 12);
    std::memset(h + l->parent, 0xFF, 8);
    std::memset(h + l->world_pos, 0, l->world_end - l->world_pos);
    std::memset(h + l->request_history, 0, l->move_req - l->request_history);
    return true;
}

// Error buckets (mm) of "the horse's own Req position vs its rider's Path world position at ~the same ts".
inline constexpr std::array<uint32_t, 6> kErrBoundsMm = {50, 100, 200, 500, 1000, 2000};
struct ErrHistMm {
    std::array<std::atomic<uint64_t>, kErrBoundsMm.size() + 1> n{};
    void add(uint32_t mm) noexcept {
        size_t i = 0;
        while (i < kErrBoundsMm.size() && mm > kErrBoundsMm[i]) ++i;
        n[i].fetch_add(1, std::memory_order_relaxed);
    }
};

// ---------------------------------------------------------------- what to do with a rider's Ack

enum class Verdict : uint8_t {
    Skip,          // not a rider's Ack
    NoWorld,       // a rider, but the Path's world position is zero (counted, nothing made)
    OwnHorse,      // the horse is one user 2 controls (never sent to its own controller)
    Probe,         // probe: would make one
    Make,          // 1: make and queue one
};
inline constexpr size_t kVerdicts = 5;

inline Verdict decide(Mode mode, const Move& m, bool parent_is_user2s) {
    if (mode == Mode::Off || m.shape != Shape::Ridden) return Verdict::Skip;
    if (m.world_zero) return Verdict::NoWorld;
    if (parent_is_user2s) return Verdict::OwnHorse;
    return mode == Mode::On ? Verdict::Make : Verdict::Probe;
}

inline const char* verdict_name(Verdict v) {
    switch (v) {
        case Verdict::Skip: return "skip";
        case Verdict::NoWorld: return "no world transform";
        case Verdict::OwnHorse: return "user 2's own horse";
        case Verdict::Probe: return "would make one";
        case Verdict::Make: return "made";
    }
    return "?";
}

// ---------------------------------------------------------------- small lock-free tables

// The keys user 2 controls, from its own Move Reqs (the network thread writes, take_frame's threads
// read): the last N distinct keys.
template <size_t N>
struct KeyRing {
    std::array<std::atomic<uint32_t>, N> key{};
    std::atomic<uint32_t> next{0};
    bool contains(uint32_t k) const noexcept {
        if (!k) return false;
        for (const auto& x : key) {
            if (x.load(std::memory_order_relaxed) == k) return true;
        }
        return false;
    }
    void add(uint32_t k) noexcept {
        if (!k || contains(k)) return;
        key[next.fetch_add(1, std::memory_order_relaxed) % N].store(k, std::memory_order_relaxed);
    }
    void clear() noexcept {
        for (auto& x : key) x.store(0, std::memory_order_relaxed);
    }
};

// key -> last ts (and, for parents, the last rider ts naming it). Fixed, lock-free; a full table
// overwrites round-robin. gap(): ms since the key's previous ts (0: first, or not newer).
template <size_t N>
struct Cadence {
    std::array<std::atomic<uint32_t>, N> key{};
    std::array<std::atomic<uint64_t>, N> ts{};
    std::atomic<uint32_t> next{0};
    uint64_t gap(uint32_t k, uint64_t t) noexcept {
        for (size_t i = 0; i < N; ++i) {
            if (key[i].load(std::memory_order_relaxed) == k) {
                const uint64_t prev = ts[i].exchange(t, std::memory_order_relaxed);
                return t > prev ? t - prev : 0;
            }
        }
        const size_t i = next.fetch_add(1, std::memory_order_relaxed) % N;
        ts[i].store(t, std::memory_order_relaxed);
        key[i].store(k, std::memory_order_relaxed);
        return 0;
    }
    // The last ts noted for k (0 = not there).
    uint64_t last(uint32_t k) const noexcept {
        for (size_t i = 0; i < N; ++i) {
            if (key[i].load(std::memory_order_relaxed) == k) return ts[i].load(std::memory_order_relaxed);
        }
        return 0;
    }
};

// A horse's own Move is "while ridden" if a rider named it as parent within this window.
inline constexpr uint64_t kRiddenWindowMs = 3000;
inline bool while_ridden(uint64_t parent_last_ts, uint64_t ts) {
    if (!parent_last_ts) return false;
    const uint64_t d = ts > parent_last_ts ? ts - parent_last_ts : parent_last_ts - ts;
    return d <= kRiddenWindowMs;
}

// Cadence buckets (ms): <=20, <=40, <=80, <=160, <=320, <=640, <=1000, <=2000, more.
inline constexpr std::array<uint32_t, 8> kCadenceBounds = {20, 40, 80, 160, 320, 640, 1000, 2000};
struct CadenceHist {
    std::array<std::atomic<uint64_t>, kCadenceBounds.size() + 1> n{};
    void add(uint64_t v) noexcept {
        size_t i = 0;
        while (i < kCadenceBounds.size() && v > kCadenceBounds[i]) ++i;
        n[i].fetch_add(1, std::memory_order_relaxed);
    }
};

// ================================================================ P8 mount ride (p8-impl-monturas-cabalgata.md)
//
// [session2] horse_follow_reverse = 0 (default) | probe | 1, HOST only (needs create_session, [sniffer] enabled and
// child_hold's server reads: observer_hold or child_settle_ms). The other direction of horse_follow, as a
// PRESENTATION improvement on the host's client only ("variante visual con Ack sólo en H", INFORME §7): user 2's
// rider sends its MoveActorReq at ~36-80 Hz with its horse's world transform in the Path, its horse only ~1-3 Hz
// (S13: 10 horse Acks on the host while G rode, p50 346 ms, max 956 ms). Every MoveActorAck 2697 of user 2's MAIN
// character riding (Path parent = an 0xB key whose server actor is OWNED by user 2's ServerUserActor) that reaches
// the host's client (ClientNet::OnRecvPacket, the client's NetworkThread) is preceded, with 1, by a 2697 of the
// horse built by make_horse_ack_into from it (the rider's Path world pose and ts), delivered through the same
// function on the same thread (troc_sniffer's recv-extra path). Never sent to the server, never to user 2.
//   - one horse sample per rider ts, only if newer than the last horse sample the host's client got (derived or
//     the horse's own): no duplicate tick, never an older one;
//   - the horse's own Acks older than (or equal to) the last derived one are counted "stale": the client's own
//     buffer drops them (0x11B1FD0: ts <= its last sample's; remote_interp's "older samples"); nothing is
//     rewritten, so a StartAction of the horse (another packet) always applies;
//   - the server keeps getting only the guest's real Reqs (its authority, collisions and AI unchanged).
// probe: the same decisions and measures, nothing delivered.

// Fine buckets for the horse's sample intervals (ms): p50 / p90 come out as the upper bound of their bucket.
inline constexpr std::array<uint32_t, 18> kIvBounds = {10,  15,  20,  25,  30,  40,  50,  60,   80,
                                                       100, 150, 200, 300, 400, 600, 800, 1000, 2000};
// A longer interval is a pause between rides (or a still horse), not the cadence: counted apart.
inline constexpr uint64_t kIvPauseMs = 3000;

struct Intervals {
    std::array<std::atomic<uint64_t>, kIvBounds.size() + 1> n{};
    std::atomic<uint64_t> max{0}, pauses{0};
    void add(uint64_t g) noexcept {
        if (g == 0) return;
        if (g > kIvPauseMs) {
            pauses.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        size_t i = 0;
        while (i < kIvBounds.size() && g > kIvBounds[i]) ++i;
        n[i].fetch_add(1, std::memory_order_relaxed);
        uint64_t m = max.load(std::memory_order_relaxed);
        while (g > m && !max.compare_exchange_weak(m, g, std::memory_order_relaxed)) {
        }
    }
    uint64_t count() const noexcept {
        uint64_t t = 0;
        for (const auto& x : n) t += x.load(std::memory_order_relaxed);
        return t;
    }
    // The q-quantile (0 < q <= 1) as the upper bound of the bucket holding it, never above the max seen;
    // 0 = no interval yet.
    uint64_t quantile(double q) const noexcept {
        const uint64_t total = count();
        if (!total) return 0;
        auto rank = static_cast<uint64_t>(q * static_cast<double>(total) + 0.999999);
        rank = std::clamp<uint64_t>(rank, 1, total);
        const uint64_t mx = max.load(std::memory_order_relaxed);
        uint64_t cum = 0;
        for (size_t i = 0; i < n.size(); ++i) {
            cum += n[i].load(std::memory_order_relaxed);
            if (cum >= rank) return i < kIvBounds.size() ? std::min<uint64_t>(kIvBounds[i], mx) : mx;
        }
        return mx;
    }
};

// Per key, the newest ts accepted (a horse's sample stream). note(): First (unknown key), Newer (with the gap),
// Same or Older (not accepted: the stream keeps its newest). Lock-free (a CAS for the newest: take_frame may run
// on several threads); a full table takes slots round-robin.
enum class Seq : uint8_t { First, Newer, Same, Older };
struct SeqNote {
    Seq seq = Seq::First;
    uint64_t gap = 0;  // Newer: ts - the previous newest
};
template <size_t N>
struct MonoTs {
    std::array<std::atomic<uint32_t>, N> key{};
    std::array<std::atomic<uint64_t>, N> ts{};
    std::atomic<uint32_t> next{0};
    uint64_t last(uint32_t k) const noexcept {
        if (!k) return 0;
        for (size_t i = 0; i < N; ++i) {
            if (key[i].load(std::memory_order_relaxed) == k) return ts[i].load(std::memory_order_relaxed);
        }
        return 0;
    }
    SeqNote note(uint32_t k, uint64_t t) noexcept {
        SeqNote r;
        if (!k) return r;
        for (size_t i = 0; i < N; ++i) {
            if (key[i].load(std::memory_order_relaxed) != k) continue;
            uint64_t cur = ts[i].load(std::memory_order_relaxed);
            for (;;) {
                if (t == cur) {
                    r.seq = Seq::Same;
                    return r;
                }
                if (t < cur) {
                    r.seq = Seq::Older;
                    return r;
                }
                if (ts[i].compare_exchange_weak(cur, t, std::memory_order_relaxed)) {
                    r.seq = cur ? Seq::Newer : Seq::First;
                    r.gap = cur ? t - cur : 0;
                    return r;
                }
            }
        }
        const size_t i = next.fetch_add(1, std::memory_order_relaxed) % N;
        key[i].store(0, std::memory_order_relaxed);
        ts[i].store(t, std::memory_order_relaxed);
        key[i].store(k, std::memory_order_relaxed);
        return r;  // First
    }
    void clear() noexcept {
        for (auto& x : key) x.store(0, std::memory_order_relaxed);
        for (auto& x : ts) x.store(0, std::memory_order_relaxed);
    }
};

// The pair check of one horse (user 2's rider on it), kept kPairRecheckMs so the server's actor table is not
// walked for every Ack. Unknown: child_hold's reads off, the actor not found, or user 2 not there.
enum class PairCheck : uint8_t { Unknown, Ok, NotOwned };
inline constexpr uint64_t kPairRecheckMs = 1000;
template <size_t N>
struct PairCache {
    std::array<std::atomic<uint32_t>, N> key{};
    std::array<std::atomic<uint8_t>, N> state{};
    std::array<std::atomic<uint64_t>, N> at{};
    std::atomic<uint32_t> next{0};
    // The cached check of k, if made within kPairRecheckMs of `now`.
    std::optional<PairCheck> get(uint32_t k, uint64_t now) const noexcept {
        if (!k) return std::nullopt;
        for (size_t i = 0; i < N; ++i) {
            if (key[i].load(std::memory_order_relaxed) != k) continue;
            const uint64_t a = at[i].load(std::memory_order_relaxed);
            if (now >= a && now - a < kPairRecheckMs) return static_cast<PairCheck>(state[i].load(std::memory_order_relaxed));
            return std::nullopt;
        }
        return std::nullopt;
    }
    void put(uint32_t k, PairCheck c, uint64_t now) noexcept {
        if (!k) return;
        size_t slot = N;
        for (size_t i = 0; i < N; ++i) {
            if (key[i].load(std::memory_order_relaxed) == k) {
                slot = i;
                break;
            }
        }
        if (slot == N) {
            slot = next.fetch_add(1, std::memory_order_relaxed) % N;
            key[slot].store(0, std::memory_order_relaxed);
        }
        state[slot].store(static_cast<uint8_t>(c), std::memory_order_relaxed);
        at[slot].store(now, std::memory_order_relaxed);
        key[slot].store(k, std::memory_order_relaxed);
    }
    void clear() noexcept {
        for (auto& x : key) x.store(0, std::memory_order_relaxed);
    }
};

// What to do with one rider's Ack reaching the host's client.
enum class Rev : uint8_t {
    Skip,        // not a rider's Ack (on foot, another parent, not a Move)
    NotUser2,    // a rider, but not user 2's main character (or its key not known yet)
    NoWorld,     // user 2's rider, the Path's world position zero: no horse transform
    Unverified,  // the horse's owner could not be read (Unknown): nothing derived
    NotOwned,    // the horse is not owned by user 2: nothing derived
    Same,        // the horse already has a sample of this ts (a duplicate tick)
    Older,       // the horse already has a newer sample: never an older one
    Probe,       // probe: would derive one
    Derive,      // 1: derive and deliver one
};
inline constexpr size_t kRevs = 9;

inline Rev decide_reverse(Mode mode, const Move& rider, uint32_t user2_char, PairCheck pair, uint64_t horse_last_ts) {
    if (mode == Mode::Off || rider.shape != Shape::Ridden) return Rev::Skip;
    if (!user2_char || rider.key != user2_char) return Rev::NotUser2;
    if (rider.world_zero) return Rev::NoWorld;
    if (pair == PairCheck::Unknown) return Rev::Unverified;
    if (pair == PairCheck::NotOwned) return Rev::NotOwned;
    if (horse_last_ts && rider.ts == horse_last_ts) return Rev::Same;
    if (horse_last_ts && rider.ts < horse_last_ts) return Rev::Older;
    return mode == Mode::On ? Rev::Derive : Rev::Probe;
}

inline const char* rev_name(Rev v) {
    switch (v) {
        case Rev::Skip: return "skip";
        case Rev::NotUser2: return "not user 2's rider";
        case Rev::NoWorld: return "no world transform";
        case Rev::Unverified: return "owner unreadable";
        case Rev::NotOwned: return "horse not user 2's";
        case Rev::Same: return "same tick";
        case Rev::Older: return "older than the horse's last";
        case Rev::Probe: return "would derive";
        case Rev::Derive: return "derived";
    }
    return "?";
}

}  // namespace cdmp::session2::hfl
