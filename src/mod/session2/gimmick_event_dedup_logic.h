#pragma once
// P8 session 28: the duplicated gimmick projectiles (docs/research/s25/gimmicks-dueno.md section 7). The pure logic,
// no game access, so the tests can run it on S27's real bytes. Build 1.0.0.2976.
//
// CreateProjectileFromGimmickEventReq 2779 has NO controller gate on the server (0x2A3E370 -> 0x2BDD1D0(component,
// ...), the requester never compared with the gimmick's +0x90), unlike 2399 / 2202 / 2699 / 2612 / 2999 / 2387. Both
// clients send it for the same gimmick event, so the server creates the projectile twice (2250 index 1 and 2):
// 10 extra projectiles in S27.
//
// [session2] gimmick_event_dedup = 0 | probe | 1 (both PCs):
//   GUEST: every outgoing 2779 is decided against the controller the last 2005 record of its gimmick carried (as the
//     server sent it, before docked_gimmicks rewrote anything): known and NOT this client's user -> "would withhold"
//     (probe) / WITHHELD (1). Unknown controller or unknown own user -> never withheld. A 2779 docked_gimmicks already
//     withholds (its own-key rule) is not counted again: the line says which rule matched.
//   HOST: log only, whatever the mode: 2779 by origin (the host's client / user 2, at the server's receive) and key;
//     a 2250 with index > 1 caused by a 2779 that reached the server within kDupWindowMs, for a gimmick the OTHER
//     origin had also sent a 2779 for, is a "duplicate created".

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "mod/session2/docked_gimmicks_logic.h"

namespace cdmp::session2::ged {

using dgl::le16;
using dgl::le32;

inline constexpr uint16_t kOpCreateFromGimmickEvent = 2779;  // CreateProjectileFromGimmickEventReq: gimmick at +0
inline constexpr uint16_t kOpCreateProjectileAck = 2250;     // CreateProjectileAck
// 2250 (S27, 127 bytes): +0 the owner (the gimmick of a 2779), +0x19 a hash, +0x1D the owner again, +0x21 u32 the
// projectile's index for that owner (1, 2 for the duplicate).
inline constexpr size_t k2250_Owner = 0x00;
inline constexpr size_t k2250_Owner2 = 0x1D;
inline constexpr size_t k2250_Index = 0x21;

struct Ack2250 {
    uint32_t owner = 0;
    uint32_t index = 0;
};
// The 2250 payload (header stripped): its owner and index; owner 0 when not that layout.
inline Ack2250 parse_2250(std::span<const uint8_t> pl) {
    Ack2250 a;
    if (pl.size() < k2250_Index + 4) return a;
    const uint32_t o = le32(&pl[k2250_Owner]);
    if (o == 0 || o != le32(&pl[k2250_Owner2])) return a;
    a.owner = o;
    a.index = le32(&pl[k2250_Index]);
    return a;
}

// ---------------------------------------------------------------- guest: the controller of each gimmick

// key -> the controller (+0x12) of the last 2005 record of that key, as received. Written by the delivery (the
// client's NetworkThread), read by the send path (any thread). Open addressing, at most dgl::kMaxProbe slots.
struct CtlEntry {
    std::atomic<uint32_t> key{0};
    std::atomic<uint32_t> controller{0};
};
using ControllerMap = dgl::Table<CtlEntry, 16384>;

// Every type-7 record of a 2005 payload: its controller into `map` (call it BEFORE anything rewrites the payload).
// Returns how many were stored (a full table counts in map.full()).
template <class Map>
size_t record_controllers(std::span<const uint8_t> pl, Map& map) {
    size_t n = 0;
    (void)dgl::for_each_record(pl, [&](size_t, std::span<const uint8_t> rec) {
        if (rec.size() < dgl::kRecController + 4 || rec[dgl::kRecType] != dgl::kTypeGimmick) return;
        if (auto* e = map.get(le32(&rec[dgl::kRecKey]))) {
            e->controller.store(le32(&rec[dgl::kRecController]), std::memory_order_relaxed);
            ++n;
        }
    });
    return n;
}

enum class Verdict : uint8_t {
    WouldWithhold,      // known controller, a user, not this client's: the server creates it from the controller too
    OwnController,      // this client controls it: its 2779 is the one the server should get
    UnknownController,  // no 2005 seen for the key (or its controller is not a user): never withheld
    OwnUserUnknown,     // this client's user not learned yet: never withheld
    DockedRule,         // docked_gimmicks' own-key rule already withholds it (not counted here)
    Count
};
inline const char* verdict_name(Verdict v) {
    switch (v) {
        case Verdict::WouldWithhold: return "controller is the other user";
        case Verdict::OwnController: return "this client controls it: sent";
        case Verdict::UnknownController: return "controller unknown: sent";
        case Verdict::OwnUserUnknown: return "own user unknown: sent";
        case Verdict::DockedRule: return "docked_gimmicks' own-key rule matched";
        case Verdict::Count: break;
    }
    return "?";
}
// controller: from the map (0 = no record); own_user: this client's user (0 = not known yet).
inline Verdict decide(uint32_t controller, uint32_t own_user) {
    if (own_user == 0) return Verdict::OwnUserUnknown;
    if (controller == 0 || !dgl::user_key(controller)) return Verdict::UnknownController;
    if (controller == own_user) return Verdict::OwnController;
    return Verdict::WouldWithhold;
}

// ---------------------------------------------------------------- host: duplicates

// Per gimmick key: the last 2779 from each origin ([0] the host's client, [1] user 2) and the last 2250 (ms).
struct DupEntry {
    std::atomic<uint32_t> key{0};
    std::atomic<uint64_t> last_2779[2] = {};
    std::atomic<uint32_t> count_2779[2] = {};
    std::atomic<uint64_t> last_2250{0};
    std::atomic<uint32_t> created{0};     // 2250 seen for the key
    std::atomic<uint32_t> duplicates{0};  // of them "duplicate created"
};
using DupTable = dgl::Table<DupEntry, 4096>;

// How long before a 2250 with index > 1 the 2779 that caused it may have reached the server. S27-1: 0.01-0.1 s.
// S27-2: the guest's 2779 sent during its world load was created 2.1 s after it was sent (G 65.19 = H 236.9 ->
// H 239.03), while the host's own 2779 for that gimmick was 170 s older (H 69.38): a duplicate needs the OTHER
// origin's 2779 at any time before, not within the window.
inline constexpr uint64_t kDupWindowMs = 5000;

enum class Created : uint8_t {
    First,        // index 1 (or 0)
    Duplicate,    // index > 1, a 2779 within the window caused it and the OTHER origin had sent one before
    LaterNoBoth,  // index > 1 without that (another cause: counted only)
    Count
};
inline const char* created_name(Created c) {
    switch (c) {
        case Created::First: return "first";
        case Created::Duplicate: return "DUPLICATE created";
        case Created::LaterNoBoth: return "index > 1 without 2779 from both sides";
        case Created::Count: break;
    }
    return "?";
}
inline bool recent(uint64_t now, uint64_t t, uint64_t window) { return t != 0 && t <= now && now - t <= window; }
struct CreatedVerdict {
    Created kind = Created::First;
    int trigger = -1;      // the origin whose 2779 within the window is the latest: 0 the host's client, 1 user 2
    uint64_t gap_ms = 0;   // 2250 time - that 2779's time
};
// last[o] / count[o]: the last 2779 time (ms) and the number of 2779 of origin o for the key, up to this 2250.
inline CreatedVerdict classify_2250(uint32_t index, uint64_t now, const uint64_t last[2], const uint32_t count[2],
                                    uint64_t window = kDupWindowMs) {
    CreatedVerdict v;
    for (int o = 0; o < 2; ++o) {
        if (recent(now, last[o], window) && (v.trigger < 0 || last[o] > last[v.trigger])) v.trigger = o;
    }
    if (v.trigger >= 0) v.gap_ms = now - last[v.trigger];
    if (index <= 1) return v;
    v.kind = (v.trigger >= 0 && count[1 - v.trigger] > 0) ? Created::Duplicate : Created::LaterNoBoth;
    return v;
}

// P8 S29, the far case for mode 1 on the guest (gimmicks-dueno.md section 8.3): a key user 2 sent 2779 for while the
// host's client never sent one (checked `window` after user 2's last). With the guest at 1 such a 2779 would be
// withheld and its projectile lost (the host's client would not create it: e.g. the gimmick is not loaded there). In
// S28 (players close together) there was none: every key had both origins (15 / 15 and 3 / 3).
inline bool lone_user2(const uint32_t count[2], uint64_t last_user2, uint64_t now, uint64_t window = kDupWindowMs) {
    return count[1] > 0 && count[0] == 0 && last_user2 != 0 && now > last_user2 + window;
}

}  // namespace cdmp::session2::ged
