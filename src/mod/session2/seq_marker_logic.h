#pragma once
// The decision behind seq_marker.h: when the own sequencer manager's load marker fell back on its own.
// Pure logic, no game memory.
//
// Build 1.0.0.3527. The marker is ClientSequencerStageManager+0x259 (M+0x231 in the 2976 notes): each trip of the
// client's load state machine (P+0x15C) takes it 0 (0x856B67) -> 1 (0x8571CE) -> 2 (PlayAllPreLoadSequencerAck,
// 0xC9CB9B) -> 3 (0x9A86A49). Those all happen while the machine runs. Below 2 the manager starts nothing (0xBA2F70).
//
// A marker of 1 is the trip's own until the trip has raised it to 2 or more; after that nothing in the trip lowers it.
// The other player's character arriving does lower it (seq_marker.h): on the host 3 -> 1 with the machine idle, on the
// guest 2 -> 1 during its own load. A trip's writes of 0 and 1 can both fall between two looks, and the marker keeps the previous
// trip's 3 through the trip's first states, so a value counts only once the trip has shown a low one.
// Build 1.0.0.3576 (static): the same marker field and the same states 0x11 / 0x18 (sites in seq_marker.cpp).
#include <cstdint>

namespace cdmp::session2::smk {

inline constexpr uint8_t kIdleState = 0x18;   // nothing is loading
inline constexpr uint8_t kWaitState = 0x11;   // the trip waits for the marker to reach 2
inline constexpr uint8_t kMarkerRuns = 2;     // from here on the manager starts sequencers
inline constexpr uint8_t kMarkerFallen = 1;   // what the stray write leaves
// Consecutive looks at a fallen marker before acting: a single look may race the trip's own writes.
inline constexpr int kRestoreAfterLooks = 2;

struct Facts {
    bool readable = false;
    bool live = false;        // another player is in the session right now
    uint8_t state = 0;        // P+0x15C
    uint8_t marker = 0;       // M+0x259
    uintptr_t manager = 0;    // M
};

struct Decision {
    bool restore = false;
    uint8_t value = 0;
};

class Guard {
public:
    Decision look(const Facts& f) noexcept {
        if (f.live) live_seen_ = true;
        if (!f.readable) {
            low_looks_ = 0;
            return {};
        }
        if (f.manager != manager_) {
            manager_ = f.manager;
            peak_ = 0;
            low_in_trip_ = false;
            was_idle_ = false;
            low_looks_ = 0;
        }
        const bool idle = f.state == kIdleState;
        if (was_idle_ && !idle) {  // a trip starts: the old value no longer counts
            peak_ = 0;
            low_in_trip_ = false;
        }
        was_idle_ = idle;
        if (f.marker < kMarkerRuns) low_in_trip_ = true;
        if (f.marker == 0) peak_ = 0;
        if (f.marker >= kMarkerRuns) {
            if ((idle || low_in_trip_) && f.marker > peak_) peak_ = f.marker;
            low_looks_ = 0;
            return {};
        }
        const bool state_ok = idle || f.state == kWaitState;
        if (!live_seen_ || f.marker != kMarkerFallen || peak_ < kMarkerRuns || !state_ok) {
            low_looks_ = 0;
            return {};
        }
        if (++low_looks_ < kRestoreAfterLooks) return {};
        low_looks_ = 0;
        return {true, peak_};
    }

    uint8_t peak() const noexcept { return peak_; }

private:
    uintptr_t manager_ = 0;
    uint8_t peak_ = 0;
    int low_looks_ = 0;
    bool low_in_trip_ = false;
    bool was_idle_ = false;
    bool live_seen_ = false;  // kept after the other player leaves: the marker stays fallen otherwise
};

}  // namespace cdmp::session2::smk
