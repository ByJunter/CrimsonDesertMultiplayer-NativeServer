#pragma once
// S41 review (the other AI's branch opcion-d-tenderos a294fac), ported BY HAND in S46 phase 1 (that branch cannot be
// merged: it starts at ef5a6b5 and would delete S42-S45). Pure policy, build 1.0.0.2976; nothing here is used by a
// hook yet: it is kept, with its tests, for the near-shop lane (s46-tenderos.md, phase 5), to be reviewed then.
//   retained_dispatch  the two ExecuteTroc call sites whose return keeps the Troc's scope (PE 2976 and the exports
//                      0x2773C20 / 0x2774730 confirm both scopes)
//   may_enter          a mutation of user 2's shop state only inside its own, current, retained, single Troc
//   recovery_ready     never create state U-1's drain cannot take back if Prepare fails
//   ArmRequest / DrainRequest  rate of the arm / drain requests (10 s), a fast arm / the first fallback at once
#include <cstdint>

namespace cdmp::session2::shop_troc {

inline bool retained_dispatch(uint32_t ret) { return ret == 0x27742AE || ret == 0x2774A0D; }

struct MutationFacts {
    bool on = false, guest = false, current = false, retained = false;
    int32_t in_flight = 0;
};
inline bool may_enter(const MutationFacts& f) {
    return f.on && f.guest && f.current && f.retained && f.in_flight == 1;
}
inline bool recovery_ready(bool stages_on, bool calls_verified, bool timers_on, bool funnel_live) {
    return stages_on && calls_verified && timers_on && funnel_live;
}
inline bool abort_needs_drain(bool armed, bool fallback, bool current, bool nonempty) {
    return armed || (fallback && current && nonempty);
}

struct ArmRequest {
    uint64_t last_asked = 0;
    bool fast_pending = false;
    void observe(bool fast, bool applicable) {
        if (!applicable) fast_pending = false;
        else if (fast) fast_pending = true;
    }
    bool due(uint64_t now) const { return fast_pending || (now >= last_asked && now - last_asked >= 10'000'000); }
    void asked(uint64_t now) {
        last_asked = now;
        fast_pending = false;
    }
};
// The first drain after a fallback does not inherit an earlier request's pause; a stale reading or a request already
// made is kept until it is really asked.
struct DrainRequest {
    uint64_t last_asked = 0;
    bool fallback_seen = false, immediate = false;
    void observe(bool fallback) {
        if (fallback && !fallback_seen) immediate = true;
        if (!fallback) immediate = false;
        fallback_seen = fallback;
    }
    bool due(uint64_t now) const { return immediate || (now >= last_asked && now - last_asked >= 10'000'000); }
    void asked(uint64_t now) {
        last_asked = now;
        immediate = false;
    }
};

}  // namespace cdmp::session2::shop_troc
