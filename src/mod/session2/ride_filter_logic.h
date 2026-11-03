#pragma once
// P8 session 9, D1 (docs/research/pendientes/p8-s9-combate-montura.md §4, §6; the implementation:
// docs/research/pendientes/p8-impl-s9-movimiento.md §1): the pure logic of ride_filter.cpp, with no game
// access, so the tests can run it.
//
// The fault [V, session 9]: on the OBSERVING client the other player's character (control mode 2) never has
// the rider<->vehicle link (the interaction Acks are ToTarget). Its mount action enters RideOn (method 6) and
// its dismount action enters RideOff (method 7); both Enter fail without the link (+0x44 = 0): RideOn freezes
// it on the ground for ~2 s, RideOff freezes it in the saddle FOREVER (83.6 s, every later action ignored).
//
// [session2] ride_filter = 0 (default) | probe | 1, on BOTH clients, at the point where each one receives the
// other player's StartActionAck 2991 (725 B): the guest in guest_deliver (the mod's network thread, before
// the packet is queued for its client), the host in ClientNet::OnRecvPacket (the sniffer's mid hook, the
// host client's NetworkThread; session 1 is native, that is the only point where user 2's Acks are seen).
// Only exactly the known remote player character is touched (the host: user 2's; the guest: the host's, from the
// host's tunnel control state 4; never the local one):
//   mount    A0 hash in {0x91605616, 0x872A6750}            -> A0 := the native on-foot block 0x26107D3C,
//                                                               A1 := the native empty block
//   dismount A1 hash == 0x7B6AFF57 and the parent is an 0xB key -> A0 := the native riding block 0xF79C7424,
//                                                               A1 := the native empty block
// Everything else of the packet (position, parent, target, ts, the u8 flag, block B) stays. The two results
// are byte for byte StartActionAcks the observing clients applied natively in session 9 (H2 2535.247 /
// 2547.697 on foot with the horse as target; H2 2540.633 / G2 riding with the horse as parent), whose methods
// (0 or 4) Enter with 1. The actor never enters method 6/7; its height comes from the Move Acks (with the
// parent: the saddle's world position; after: the ground). Cost: the mount/dismount animations are not shown.
// probe: nothing is changed; the would-be rewrites are counted, and every StartActionAck of the remote
// character that names an 0xB key (target or parent) or comes within kWindowMs of a change of its parent is
// recorded as a (A0, A1) pair to complete the lists.
//
// Layout [V, mirror-ack-formats.md §2 + session 9 captures]: StartActionAck payload 725 B:
//   +0x000 u32 actor | +0x004 A[0] 69 B | +0x049 A[1] 69 B | +0x08E u8 flag | +0x08F B 582 B
//   A: +0x00 u32 (0 in Acks) | +0x04 u32 action hash (0 = no change on that layer) | ... (69 B)
//   B: MoveData; parent u64 at B+0x88 (payload +0x117), target u32 at B+0xDE (payload +0x16D).

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "mod/session2/user2_funnels_logic.h"

namespace cdmp::session2::rfl {

using u2f::Mode;
using u2f::mode_name;
using u2f::parse_mode;

inline constexpr uint16_t kOpStartActionAck = 2991;
inline constexpr size_t kHeader = 5;
inline constexpr size_t kAckSize = 725;
inline constexpr size_t kA0 = 0x004;
inline constexpr size_t kA1 = 0x049;
inline constexpr size_t kBlock = 69;
inline constexpr size_t kBlockHash = 0x04;
inline constexpr size_t kParent = 0x117;  // u64, all ones = none
inline constexpr size_t kTarget = 0x16D;  // u32

inline constexpr uint32_t kMountA0[] = {0x91605616u, 0x872A6750u};  // the guest's / the host's (session 9)
inline constexpr uint32_t kDismountA1 = 0x7B6AFF57u;                // with A0 0x667E3B35 / 0xC9356CEB / 0x412C2C2B
inline constexpr uint32_t kFootHash = 0x26107D3Cu;
inline constexpr uint32_t kRidingHash = 0xF79C7424u;

// The native 69-byte blocks [V]: the dominant form in the session 9 captures (H2 48 of 51 / G2 29 of 31 for
// 0x26107D3C in A0; F79C7424 in A0 as at H2 2540.633 and G2 3 of 4; the empty block 208 of 208).
inline constexpr std::array<uint8_t, kBlock> kBlockFoot = {
    0x00, 0x00, 0x00, 0x00, 0x3C, 0x7D, 0x10, 0x26, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x89, 0x88, 0x08, 0xBD,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD7, 0xA3, 0x70, 0x3F, 0x57, 0x57, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
inline constexpr std::array<uint8_t, kBlock> kBlockRiding = {
    0x00, 0x00, 0x00, 0x00, 0x24, 0x74, 0x9C, 0xF7, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x89, 0x88, 0x08, 0xBD,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD7, 0xA3, 0x70, 0x3F, 0x57, 0x57, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
inline constexpr std::array<uint8_t, kBlock> kBlockEmpty = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x02, 0x00, 0x00, 0x80, 0xBF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x57, 0x57, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Byte reads and writes without memcpy (the CRT's may use AVX inside a mid hook: p8_probe_logic.h).
inline uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline uint64_t rd64(const uint8_t* p) { return rd32(p) | (static_cast<uint64_t>(rd32(p + 4)) << 32); }

// ---------------------------------------------------------------- one StartActionAck

struct Ack {
    bool ok = false;  // a whole 725-byte StartActionAck payload
    uint32_t key = 0;
    uint32_t a0 = 0, a1 = 0;
    uint64_t parent = ~0ull;
    uint32_t target = 0;
};

inline Ack read_ack(const uint8_t* payload, size_t size) {
    Ack a;
    if (!payload || size != kAckSize) return a;
    a.ok = true;
    a.key = rd32(payload);
    a.a0 = rd32(payload + kA0 + kBlockHash);
    a.a1 = rd32(payload + kA1 + kBlockHash);
    a.parent = rd64(payload + kParent);
    a.target = rd32(payload + kTarget);
    return a;
}

inline bool is_character(uint32_t key) { return (key >> 28) == 0xA; }
inline bool is_b_key(uint64_t k) { return (k >> 32) == 0 && (static_cast<uint32_t>(k) >> 28) == 0xB; }
inline bool has_b_parent(const Ack& a) { return a.parent != ~0ull && is_b_key(a.parent); }

enum class Kind : uint8_t {
    None,      // not a ride action
    Mount,     // A0 is a known mount action
    Dismount,  // A1 is the dismount layer and the parent is still an 0xB key
};
inline constexpr size_t kKinds = 3;
inline const char* kind_name(Kind k) {
    switch (k) {
        case Kind::None: return "none";
        case Kind::Mount: return "mount";
        case Kind::Dismount: return "dismount";
    }
    return "?";
}

inline Kind classify(const Ack& a) {
    if (!a.ok || !is_character(a.key)) return Kind::None;
    for (const uint32_t h : kMountA0) {
        if (a.a0 == h) return Kind::Mount;
    }
    if (a.a1 == kDismountA1 && has_b_parent(a)) return Kind::Dismount;
    return Kind::None;
}

// Is `key` the remote player character this client should filter? Exactly the one known remote character
// (review 2026-10-10: never "any 0xA but mine"): the host passes user 2's (its MainPlayerDataStartAck to session
// 2), the guest the host's (the host's control state 4 through the tunnel); 0 = unknown -> nothing. local_key
// (the guest's own character, 0 = unknown) is a second guard: never this client's own.
inline bool is_remote_player(uint32_t key, uint32_t remote_key, uint32_t local_key) {
    return is_character(key) && remote_key != 0 && key == remote_key && key != local_key;
}

// The host's control state 4 (troc_frame.h kControlHostCharacter): u8 4 | u32 key. 0 = not that / not a 0xA key.
inline uint32_t host_character_from_control(const uint8_t* payload, size_t size) {
    if (!payload || size != 5 || payload[0] != 4) return 0;
    const uint32_t k = rd32(payload + 1);
    return is_character(k) ? k : 0;
}

inline void put_block(uint8_t* dst, const std::array<uint8_t, kBlock>& src) {
    volatile uint8_t* d = dst;  // no memcpy, no vectorised copy (mid hook)
    for (size_t i = 0; i < kBlock; ++i) d[i] = src[i];
}

// The rewrite, in place, on a payload read_ack() accepted. False: nothing to do.
inline bool rewrite(uint8_t* payload, size_t size, Kind k) {
    if (!payload || size != kAckSize) return false;
    switch (k) {
        case Kind::Mount:
            put_block(payload + kA0, kBlockFoot);
            put_block(payload + kA1, kBlockEmpty);
            return true;
        case Kind::Dismount:
            put_block(payload + kA0, kBlockRiding);
            put_block(payload + kA1, kBlockEmpty);
            return true;
        case Kind::None: break;
    }
    return false;
}

// ---------------------------------------------------------------- probe: the hashes around riding

// Every StartActionAck of the remote character that names an 0xB key (target or parent) or comes within
// kWindowMs after its parent changed: its (A0, A1) pair, whether it had a parent/target and its kind.
inline constexpr uint64_t kWindowMs = 3000;

// The remote character's last parent and the moment it changed (one writer per process: the guest's network
// thread or the host's client NetworkThread; relaxed atomics so the minute's line may read them).
struct WindowNote {
    bool in_window = false;     // record this Ack's pair
    bool after_change = false;  // within kWindowMs after the parent changed
};
struct ParentWatch {
    std::atomic<uint32_t> key{0};
    std::atomic<uint64_t> parent{~0ull};
    std::atomic<uint64_t> changed_ms{0};
    WindowNote note(const Ack& a, uint64_t now_ms) noexcept {
        if (key.load(std::memory_order_relaxed) != a.key) {
            key.store(a.key, std::memory_order_relaxed);
            parent.store(a.parent, std::memory_order_relaxed);
            changed_ms.store(0, std::memory_order_relaxed);
        } else if (parent.load(std::memory_order_relaxed) != a.parent) {
            parent.store(a.parent, std::memory_order_relaxed);
            changed_ms.store(now_ms ? now_ms : 1, std::memory_order_relaxed);
        }
        WindowNote w;
        const uint64_t c = changed_ms.load(std::memory_order_relaxed);
        w.after_change = c != 0 && now_ms >= c && now_ms - c <= kWindowMs;
        w.in_window = w.after_change || has_b_parent(a) || (a.target != 0 && is_b_key(a.target));
        return w;
    }
};

// 1 parent 0xB, 2 target 0xB, 4 within kWindowMs after a parent change; the kind in the high nibble.
inline uint8_t pair_flags(const Ack& a, bool after_change, Kind k) {
    uint8_t f = 0;
    if (has_b_parent(a)) f |= 1;
    if (a.target != 0 && is_b_key(a.target)) f |= 2;
    if (after_change) f |= 4;
    return static_cast<uint8_t>(f | (static_cast<uint8_t>(k) << 4));
}

// Distinct (A0, A1, flags) with counts: fixed, no allocation. add() takes a try-lock (one writer in
// practice); busy -> counted, never waits. The minute's line reads the atomics.
struct PairTable {
    static constexpr size_t kSlots = 64;
    struct Slot {
        std::atomic<bool> used{false};
        std::atomic<uint32_t> a0{0}, a1{0};
        std::atomic<uint8_t> flags{0};
        std::atomic<uint64_t> n{0};
    };
    std::array<Slot, kSlots> slot{};
    std::atomic<uint64_t> overflow{0}, busy{0};
    std::atomic_flag lock{};
    void add(uint32_t a0, uint32_t a1, uint8_t flags) noexcept {
        if (lock.test_and_set(std::memory_order_acquire)) {
            busy.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        bool done = false;
        for (Slot& s : slot) {
            if (!s.used.load(std::memory_order_relaxed)) {
                s.a0.store(a0, std::memory_order_relaxed);
                s.a1.store(a1, std::memory_order_relaxed);
                s.flags.store(flags, std::memory_order_relaxed);
                s.n.store(1, std::memory_order_relaxed);
                s.used.store(true, std::memory_order_release);
                done = true;
                break;
            }
            if (s.a0.load(std::memory_order_relaxed) == a0 && s.a1.load(std::memory_order_relaxed) == a1 &&
                s.flags.load(std::memory_order_relaxed) == flags) {
                s.n.fetch_add(1, std::memory_order_relaxed);
                done = true;
                break;
            }
        }
        if (!done) overflow.fetch_add(1, std::memory_order_relaxed);
        lock.clear(std::memory_order_release);
    }
    size_t used() const noexcept {
        size_t n = 0;
        for (const Slot& s : slot) n += s.used.load(std::memory_order_acquire) ? 1 : 0;
        return n;
    }
};

// ---------------------------------------------------------------- P8 S18: ride_dismount (the dismount layer)
//
// Session 18 (pendientes/p8-s18-desmontar-nativo.md §6) [V code]: every action received over the network carries
// bit 1 of entry+0x50 (0x1EC9C00, 0x1EC9C26); the Common CC request function (.xtls 0xEE58A40, behind the thunk
// 0x2329A90) drops such a request silently while the current or pending action of either layer has def+0xA & 0x40
// ("get off the vehicle"). A remote player copy (actor+0x96 == 0) never runs its local chart, so once a 0x40 action
// is current (C9356CEB / 412C2C2B; 667E3B35, the first part, has no 0x40) it stays in RideOff (method 7) for ever.
// [session2] ride_dismount (host AND guest; default rewrite):
//   rewrite  every dismount action of the remote rider is rewritten (session 17: never RideOff, no animation)
//   partial  only the 0x40 dismount actions (kGateDismountA0 + the hashes learned from def+0xA) are rewritten;
//            667E3B35 goes through (the first part of the animation), so the rider ends on foot
//   native   with mount_sync=1 and a validated transaction the dismount is left to mount_sync (session 18), whose
//            detour of the thunk 0x2329A90 lets the next network action leave RideOff (the full animation)
enum class Dismount : uint8_t { Rewrite = 0, Partial = 1, Native = 2 };
inline const char* dismount_name(Dismount d) {
    switch (d) {
        case Dismount::Rewrite: return "rewrite";
        case Dismount::Partial: return "partial";
        case Dismount::Native: return "native";
    }
    return "?";
}
struct ParsedDismount {
    bool known = false;
    Dismount mode = Dismount::Rewrite;
};
inline ParsedDismount parse_dismount(std::string_view raw) {
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.remove_prefix(1);
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) raw.remove_suffix(1);
    if (raw.empty() || raw == "rewrite" || raw == "0") return {true, Dismount::Rewrite};
    if (raw == "partial") return {true, Dismount::Partial};
    if (raw == "native" || raw == "1") return {true, Dismount::Native};
    return {false, Dismount::Rewrite};
}

// The dismount actions whose definition carries def+0xA & 0x40 [V log S18 + nativo §6.1]: C9356CEB is current while
// the copy is stuck; 412C2C2B is the other side's. 667E3B35 (method 4) is not one of them (S18 sonda #2).
inline constexpr uint32_t kGateDismountA0[] = {0xC9356CEBu, 0x412C2C2Bu};

// Hashes seen with def+0xA & 0x40 at run time (the local player's own RideOff, the remote rider's current action),
// on top of the fixed list. Lock-free, bounded; written by the client main thread, read by the network threads.
struct GateHashes {
    static constexpr size_t kSlots = 8;
    std::array<std::atomic<uint32_t>, kSlots> h{};
    std::atomic<uint64_t> full{0};
    bool fixed(uint32_t hash) const noexcept {
        for (const uint32_t g : kGateDismountA0) {
            if (g == hash) return true;
        }
        return false;
    }
    bool contains(uint32_t hash) const noexcept {
        if (!hash) return false;
        if (fixed(hash)) return true;
        for (const auto& s : h) {
            if (s.load(std::memory_order_relaxed) == hash) return true;
        }
        return false;
    }
    // True: newly learned (not fixed, not already there).
    bool learn(uint32_t hash) noexcept {
        if (!hash || contains(hash)) return false;
        for (auto& s : h) {
            uint32_t want = 0;
            if (s.compare_exchange_strong(want, hash, std::memory_order_relaxed)) return true;
            if (want == hash) return false;
        }
        full.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    size_t learned() const noexcept {
        size_t n = 0;
        for (const auto& s : h) n += s.load(std::memory_order_relaxed) ? 1 : 0;
        return n;
    }
};
// A dismount Ack whose A0 or A1 is a 0x40 action (fixed or learned).
inline bool gate_dismount(const Ack& a, const GateHashes& g) { return g.contains(a.a0) || g.contains(a.a1); }

// What ride_filter does with one action of the remote player character (mode 1). `ms_defers`: mount_sync=1 holds a
// validated transaction of this rider (only asked when it matters: see wants_mount_sync).
enum class Route : uint8_t { Pass, Rewrite, Defer };
inline const char* route_name(Route r) {
    switch (r) {
        case Route::Pass: return "pass";
        case Route::Rewrite: return "rewrite";
        case Route::Defer: return "defer";
    }
    return "?";
}
// Does the route depend on mount_sync's transaction? (Its lock is not taken otherwise.)
inline bool wants_mount_sync(Kind k, Dismount d, bool gate) {
    if (k == Kind::Mount) return true;
    if (k != Kind::Dismount) return false;
    if (d == Dismount::Rewrite) return false;
    if (d == Dismount::Partial && gate) return false;
    return true;
}
inline Route route(Kind k, Dismount d, bool gate, bool ms_defers) {
    switch (k) {
        case Kind::None: return Route::Pass;
        case Kind::Mount: return ms_defers ? Route::Defer : Route::Rewrite;
        case Kind::Dismount:
            switch (d) {
                case Dismount::Rewrite: return Route::Rewrite;   // session 17
                case Dismount::Partial:                          // fix A: only the 0x40 part
                    if (gate) return Route::Rewrite;
                    return ms_defers ? Route::Defer : Route::Pass;
                case Dismount::Native: return ms_defers ? Route::Defer : Route::Rewrite;  // fix B (session 18 + gate)
            }
            break;
    }
    return Route::Rewrite;
}

// The whole decision for one received packet (header + payload), in place. remote_key / local_key as for
// is_remote_player. Mode Off: nothing. Probe: never writes. On: rewrites a Mount / Dismount.
struct Outcome {
    bool start_action = false;  // a whole StartActionAck
    bool remote = false;        // of the remote player character
    Kind kind = Kind::None;
    bool rewritten = false;
    bool deferred = false;      // mode 1: left to mount_sync's dispatch (a validated mount transaction)
    bool passed = false;        // mode 1, ride_dismount=partial: a dismount without 0x40 let through untouched
    bool gate = false;          // a 0x40 dismount action (gate_dismount)
    Ack ack;                    // as received (before any rewrite)
};
inline Outcome filter_packet(Mode mode, uint8_t* packet, size_t size, uint32_t remote_key, uint32_t local_key) {
    Outcome o;
    if (mode == Mode::Off || !packet || size != kHeader + kAckSize) return o;
    const uint16_t op = static_cast<uint16_t>(packet[0] | (packet[1] << 8));
    const uint16_t len = static_cast<uint16_t>(packet[3] | (packet[4] << 8));
    if (op != kOpStartActionAck || len != kAckSize) return o;
    o.ack = read_ack(packet + kHeader, kAckSize);
    o.start_action = o.ack.ok;
    if (!o.ack.ok) return o;
    o.remote = is_remote_player(o.ack.key, remote_key, local_key);
    if (!o.remote) return o;
    o.kind = classify(o.ack);
    if (mode == Mode::On && o.kind != Kind::None) o.rewritten = rewrite(packet + kHeader, kAckSize, o.kind);
    return o;
}

}  // namespace cdmp::session2::rfl
