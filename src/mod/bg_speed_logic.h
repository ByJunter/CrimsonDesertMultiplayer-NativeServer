#pragma once
// [general] background_full_speed = 0 | 1 (docs/research/dos-instancias.md §2.6), with CDMP_ROLE only.
//
// Why the copy that is NOT in the foreground drops to 2-4 fps (build 1.0.0.2976): not a throttle of the game. Its
// main thread waits for its own GPU fence every frame (FreezeWatch samples of 14-10 on both roles: 0x3E00790, a
// D3D12 fence SetEventOnCompletion + WaitForSingleObject(10 ms) loop, <- 0x38D9F20 <- 0x3837640, from the main loop's
// 0xAD2380): its GPU work finishes late because the foreground copy, uncapped and with DLSS frame generation, fills
// the GPU and Windows schedules the foreground process's GPU work first. The game's own reaction to losing the focus
// stays as it is: WM_ACTIVATE inactive -> the input manager stops reading input (0x2DFB1A0), WM_ACTIVATEAPP inactive
// -> Reflex and frame generation off (0x14A8D6D0). The run loop's Sleep (0x14C87FD0) depends on two GlobalVariables
// (0x6C9B6B0 bool, 0x6C9B750 ms) that nothing in the game sets (only WinAppLauncher, 0x359C90).
//
// So the option touches no game code and reads no game memory. A thread of the mod looks at the foreground window
// every 250 ms: while it belongs to another process, this process's GPU scheduling priority class
// (D3DKMTSetProcessSchedulingPriorityClass) goes up to High (Above normal if High is refused); when this process has
// the foreground again, back to what it was. Once, at start, it also opts the process out of Windows' power
// throttling (EcoQoS and the timer-resolution clamp for hidden windows). Input routing is untouched: the game still
// sees its real focus state.
//
// Pure logic (no Windows call) so the tests can run it.

#include <cstdint>
#include <string>

namespace cdmp::bgspeed {

// D3DKMT_SCHEDULINGPRIORITYCLASS (d3dkmthk.h).
enum : int32_t {
    kIdle = 0,
    kBelowNormal = 1,
    kNormal = 2,
    kAboveNormal = 3,
    kHigh = 4,
    kRealtime = 5,
    kUnknown = -1,
};

inline const char* class_name(int32_t c) {
    switch (c) {
        case kIdle: return "Idle";
        case kBelowNormal: return "BelowNormal";
        case kNormal: return "Normal";
        case kAboveNormal: return "AboveNormal";
        case kHigh: return "High";
        case kRealtime: return "Realtime";
        default: break;
    }
    return "?";
}

// Whether the option runs in this process.
enum class Gate : uint8_t {
    Off,            // background_full_speed=0
    On,             // =1, a role, inside the game
    IgnoredNoRole,  // =1 without CDMP_ROLE: a WRN, nothing done (one copy per PC needs nothing)
    NotInGame,      // =1 in a test host: nothing done (cdmp_host unloads the mod)
};
inline Gate gate(bool wanted, bool has_role, bool in_game) {
    if (!wanted) return Gate::Off;
    if (!has_role) return Gate::IgnoredNoRole;
    if (!in_game) return Gate::NotInGame;
    return Gate::On;
}

// Who has the foreground window.
enum class Fg : uint8_t { Own, Other, None };
// `fg_null`: GetForegroundWindow returned nothing (a switch in progress, the secure desktop): counts as another's.
inline Fg classify(bool fg_null, uint32_t fg_pid, uint32_t own_pid) {
    if (fg_null || fg_pid == 0) return Fg::None;
    return fg_pid == own_pid ? Fg::Own : Fg::Other;
}

// The next class to try after `refused` was refused for the background: High -> AboveNormal -> none (kUnknown).
// Never a class at or below the original one (that would not raise anything).
inline int32_t next_boost(int32_t refused, int32_t original) {
    const int32_t next = refused == kHigh ? kAboveNormal : kUnknown;
    return next > original ? next : kUnknown;
}

// The decision, one call per poll. poll() says which class to set now (kUnknown = nothing to do); the caller sets it
// and reports with result().
class Controller {
public:
    static constexpr uint32_t kMaxFailures = 8;  // then it stops trying (said once by the caller)

    // `original`: the class read at start (kUnknown if it could not be read: Normal is assumed, the default).
    explicit Controller(int32_t original = kNormal)
        : original_(original == kUnknown ? kNormal : original), applied_(original_),
          boost_(kHigh > original_ ? kHigh : kUnknown) {}

    int32_t poll(Fg fg, uint64_t now_ms) {
        const bool bg = fg != Fg::Own;
        if (have_) {
            const uint64_t dt = now_ms >= last_ms_ ? now_ms - last_ms_ : 0;
            (background_ ? bg_ms_ : fg_ms_) += dt;
            if (bg != background_) ++(bg ? to_bg_ : to_fg_);
        }
        have_ = true;
        background_ = bg;
        last_ms_ = now_ms;
        if (gave_up()) return kUnknown;
        const int32_t want = bg && boost_ != kUnknown ? boost_ : original_;
        return want != applied_ ? want : kUnknown;
    }

    void result(int32_t cls, bool ok) {
        if (ok) {
            ++set_ok_;
            applied_ = cls;
            return;
        }
        ++set_failed_;
        if (cls == boost_) boost_ = next_boost(cls, original_);  // a refused boost: try a lower one next poll
    }

    bool gave_up() const { return set_failed_ >= kMaxFailures; }
    bool background() const { return background_; }
    int32_t original() const { return original_; }
    int32_t applied() const { return applied_; }
    int32_t boost() const { return boost_; }
    uint64_t to_background() const { return to_bg_; }
    uint64_t to_foreground() const { return to_fg_; }
    uint64_t background_ms() const { return bg_ms_; }
    uint64_t foreground_ms() const { return fg_ms_; }
    uint64_t set_ok() const { return set_ok_; }
    uint64_t set_failed() const { return set_failed_; }

    std::string describe() const {
        std::string s = background_ ? "background" : "foreground";
        s += " now | background " + std::to_string(bg_ms_ / 1000) + " s, foreground " + std::to_string(fg_ms_ / 1000) +
             " s | switches to background " + std::to_string(to_bg_) + ", to foreground " + std::to_string(to_fg_) +
             " | GPU class " + class_name(applied_) + " (original " + class_name(original_) + ", background " +
             (boost_ == kUnknown ? std::string("none: every boost refused") : std::string(class_name(boost_))) +
             ") | sets ok " + std::to_string(set_ok_) + ", failed " + std::to_string(set_failed_);
        if (gave_up()) s += " (GAVE UP)";
        return s;
    }

private:
    int32_t original_;
    int32_t applied_;
    int32_t boost_;
    bool have_ = false, background_ = false;
    uint64_t last_ms_ = 0;
    uint64_t bg_ms_ = 0, fg_ms_ = 0, to_bg_ = 0, to_fg_ = 0, set_ok_ = 0, set_failed_ = 0;
};

}  // namespace cdmp::bgspeed
