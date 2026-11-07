#pragma once
// [session2] docking_dedupe = 1 (default) | 0, GUEST. Pure logic (tests: tests/optd_docking_dedupe_tests.cpp).
//
// Each client asks the server, about ten times a second, to spawn the other player's equipment pieces
// (OnDemandEquipDockingGimmickSpawnReq, logical 2249, native 2288 on 3527/3549); the server has nothing to create and
// answers UpdateOnDemandEquipDockingNak every time (docs/ESTADO-3527.md, "Problemas conocidos"). On the guest that is ~1000
// requests and as many Naks through the tunnel per minute (capture of 2026-11-05: 1003 in its last 3 MB), traffic that
// shares the ordered stream with the players' movement. The same request (byte for byte) is sent at most once per
// kWindowMs; a different one (another actor, another piece list) goes at once.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cdmp::session2::ddd {

inline constexpr uint16_t kLogicalOpcode = 2249;  // OnDemandEquipDockingGimmickSpawnReq, as the guest's send path has it
inline constexpr uint64_t kWindowMs = 2000;
inline constexpr size_t kSlots = 32;

// FNV-1a of the whole packet (header and payload).
inline uint64_t fingerprint(std::span<const uint8_t> packet) noexcept {
    uint64_t h = 1469598103934665603ull;
    for (const uint8_t b : packet) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}

class Dedupe {
public:
    // True: the same request went out less than kWindowMs ago (drop this one).
    bool drop(uint64_t fp, uint64_t now_ms) noexcept {
        Entry* oldest = &slots_[0];
        for (Entry& e : slots_) {
            if (e.at && e.fp == fp) {
                if (now_ms - e.at < kWindowMs) return true;
                e.at = now_ms;
                return false;
            }
            if (e.at < oldest->at) oldest = &e;
        }
        *oldest = Entry{fp, now_ms ? now_ms : 1};
        return false;
    }
    void reset() noexcept { slots_ = {}; }

private:
    struct Entry {
        uint64_t fp = 0;
        uint64_t at = 0;
    };
    std::array<Entry, kSlots> slots_{};
};

}  // namespace cdmp::session2::ddd
