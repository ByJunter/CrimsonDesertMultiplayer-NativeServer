#pragma once
// Guest, build 3527: the loading state 0x11 waits for the sequencer manager's
// readiness byte M+0x259 >= 2, and only PlayAllPreLoadSequencerAck 2322 writes
// that 2 (handler exe+0xC9C9B0, a single store at 0xC9CB9B).
//
// On a trip into the host's field the guest's own character is loaded again
// around the 2322 (run 9: its 2005 at t=54.927, the 2322 at t=54.934), and that
// login's ClientSequencerStageManager::vf4 (exe+0x9A7F860, also inlined at
// 0x8571CE) writes M+0x259 = 1 about a second AFTER the 2322 was handled. The
// host cannot order this: the two reach the client in one batch and the client
// applies the login later. So M stays at 1 and the load waits in 0x11 for good
// (journal of run 9: 0xF marker 2 -> 0xF marker 1 -> 0x11 marker 1, no 2445).
//
// The cure is the same message again, once the client really waits for it. The
// handler has no effect other than that store, so repeating it is safe. Pure
// decision here, no game access: preload_release_tick in guest.cpp feeds it.

#include <cstdint>

namespace cdmp::session2::prl {

inline constexpr uint16_t kOpPlayAllPreLoad = 2322;
inline constexpr uint16_t kOpMoveFieldAck = 2174;
inline constexpr uint8_t kStateWaitPreload = 0x11;
inline constexpr uint8_t kMarkerReady = 2;
// How long 0x11 must be seen short of the marker before repeating: long enough
// that a 2322 being applied right now wins, short enough not to be noticed.
inline constexpr uint64_t kSettleMs = 250;
inline constexpr uint64_t kRetryMs = 1000;
// No 2322 on this trip at all: a trip the host's server does not answer for an
// observer (a revive, a reload). A little over the 1.36 s of the owner's own timer
// (the handler only stores the marker: a real 2322 coming later is harmless).
// 3576, 2026-11-07: a fast travel of the guest into the host's field waited the
// whole 15 s it was before (no 2322 came; the host's 2322 copy waits for this
// load's own 2762), and the town's merchants appeared that much later for it.
// 12-nov: 4 s, then 1.5 s (Vellua, 15:55: 8.5 s of the game's own loading, then
// 4.07 s in 0x11 for nothing: every guest trip paid it).
inline constexpr uint64_t kSynthesizeAfterMs = 1500;
inline constexpr uint32_t kMaxPerWait = 16;

enum class Action : uint8_t { None, Repeat, Synthesize };

// What the client shows right now. readable = the whole pointer chain was read.
struct Facts {
    bool readable = false;
    uint8_t state = 0;       // field-change controller +0x15C
    uint8_t marker = 0;      // manager +0x259
    uint32_t own_key = 0;    // the local character's key
    bool field_is_own = false;  // the field's owner is the local user (the 2174's rewrite)
    uint64_t now_ms = 0;
};

class Release {
public:
    // A MoveFieldAck starts a new trip: an older 2322 says nothing about it.
    void on_trip() noexcept { seen_key_ = 0; }

    void on_delivered_2322(uint32_t key) noexcept { seen_key_ = key; }

    // A new connection: nothing carries over.
    void reset() noexcept { *this = Release{}; }

    Action decide(const Facts& f) noexcept {
        const bool waiting = f.readable && f.state == kStateWaitPreload && f.marker < kMarkerReady &&
                             f.own_key != 0 && f.field_is_own;
        if (!waiting) {
            waiting_since_ = 0;
            sent_this_wait_ = 0;
            last_sent_ = 0;
            return Action::None;
        }
        if (waiting_since_ == 0) waiting_since_ = f.now_ms;
        const uint64_t waited = f.now_ms - waiting_since_;
        if (waited < kSettleMs || sent_this_wait_ >= kMaxPerWait) return Action::None;
        if (last_sent_ != 0 && f.now_ms - last_sent_ < kRetryMs) return Action::None;
        if (seen_key_ == f.own_key) return Action::Repeat;
        return waited >= kSynthesizeAfterMs ? Action::Synthesize : Action::None;
    }

    void on_sent(uint64_t now_ms) noexcept {
        last_sent_ = now_ms;
        ++sent_this_wait_;
        ++sent_total_;
    }

    uint32_t sent_total() const noexcept { return sent_total_; }
    uint32_t sent_this_wait() const noexcept { return sent_this_wait_; }

private:
    uint32_t seen_key_ = 0;
    uint64_t waiting_since_ = 0;
    uint64_t last_sent_ = 0;
    uint32_t sent_this_wait_ = 0;
    uint32_t sent_total_ = 0;
};

}  // namespace cdmp::session2::prl
