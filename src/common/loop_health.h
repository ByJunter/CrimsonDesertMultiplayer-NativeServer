#pragma once
// The network thread's health (docs/research/pendientes/p8-hilo-red.md). Pure
// logic with the clock passed in, so the tests can drive it.
//
// Why: in the live P8 session 5 the guest's network thread (the one that pumps
// Steam, delivers the Troc tunnel and writes the 5 s report) spent 4-8 s of
// every 5 s report inside a diagnostic walk of game memory. Nothing measured
// that: the only trace was the report lines drifting from 5.0 to 6.5-8 s apart
// while the host timed the guest out. This says, on every 5 s line, how long
// the loop's iterations took and how long Steam went without being read, and
// hands back any single iteration above `slow_us` with the time of each phase.
//
// Use, once per loop iteration:
//   begin(t); ... mark(Phase::Poll, t) ... pump(start, end, n) ... mark(...) ...
//   if (auto slow = end(t)) log(slow_text(*slow));
// and on the report: window_text(take_window(t)).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace cdmp {

class LoopHealth {
public:
    // What the loop does, in its order. A phase's time is from the previous
    // mark (or begin) to its own mark.
    enum class Phase : uint8_t { Poll, Recv, Session, HostTunnel, GuestTunnel, Coop, State, Report, Count };
    static constexpr size_t kPhases = static_cast<size_t>(Phase::Count);

    static constexpr const char* name(Phase p) {
        switch (p) {
            case Phase::Poll: return "steam poll";
            case Phase::Recv: return "receive";
            case Phase::Session: return "session";
            case Phase::HostTunnel: return "host tunnel";
            case Phase::GuestTunnel: return "guest tunnel";
            case Phase::Coop: return "coop";
            case Phase::State: return "state/hud";
            case Phase::Report: return "5 s report";
            case Phase::Count: break;
        }
        return "?";
    }

    struct Config {
        uint64_t slow_us = 250'000;  // an iteration above this is reported on its own
    };

    // One iteration, as end() hands it back when it was slow.
    struct Iteration {
        uint64_t total_us = 0;
        std::array<uint64_t, kPhases> phase_us{};
        uint32_t received = 0;    // datagrams the receive pumps of this iteration took
        uint64_t no_pump_us = 0;  // the longest stretch of this iteration's window without a receive pump
        Phase worst() const {
            size_t w = 0;
            for (size_t i = 1; i < kPhases; ++i) {
                if (phase_us[i] > phase_us[w]) w = i;
            }
            return static_cast<Phase>(w);
        }
    };

    // Since the last take_window().
    struct Window {
        uint64_t iterations = 0;
        uint64_t total_us = 0;     // sum of the iterations' durations
        uint64_t max_us = 0;       // the longest iteration
        Phase max_phase = Phase::Poll;  // its longest phase
        uint64_t max_no_pump_us = 0;    // the longest gap between two receive pumps
        uint64_t slow = 0;         // iterations above Config::slow_us
        uint64_t received = 0;
        double mean_ms() const { return iterations ? static_cast<double>(total_us) / iterations / 1000.0 : 0.0; }
    };

    LoopHealth() : LoopHealth(Config{}) {}
    explicit LoopHealth(Config config) : config_(config) {}

    void begin(uint64_t now_us) {
        cur_ = Iteration{};
        begin_us_ = mark_us_ = now_us;
        iteration_gap_us_ = 0;
        in_iteration_ = true;
    }

    // `p` ran from the previous mark (or begin) until now.
    void mark(Phase p, uint64_t now_us) {
        if (!in_iteration_) return;
        cur_.phase_us[static_cast<size_t>(p)] += elapsed(mark_us_, now_us);
        mark_us_ = now_us;
    }

    // A receive pump (one transport recv call, waiting included) ran from
    // start_us to end_us. The gap measured is from the end of the previous
    // pump to the start of this one: the time nobody read Steam.
    void pump(uint64_t start_us, uint64_t end_us, uint32_t received) {
        if (have_pump_) note_gap(elapsed(last_pump_end_us_, start_us));
        have_pump_ = true;
        last_pump_end_us_ = end_us;
        cur_.received += received;
        window_.received += received;
    }

    // Ends the iteration; the iteration if it took more than Config::slow_us.
    std::optional<Iteration> end(uint64_t now_us) {
        if (!in_iteration_) return std::nullopt;
        in_iteration_ = false;
        cur_.total_us = elapsed(begin_us_, now_us);
        ++window_.iterations;
        window_.total_us += cur_.total_us;
        if (cur_.total_us > window_.max_us) {
            window_.max_us = cur_.total_us;
            window_.max_phase = cur_.worst();
        }
        if (cur_.total_us <= config_.slow_us) return std::nullopt;
        ++window_.slow;
        ++slow_total_;
        cur_.no_pump_us = have_pump_ ? std::max(iteration_gap_us_, elapsed(last_pump_end_us_, now_us)) : 0;
        return cur_;
    }

    // The window so far, and a new one from now. The stretch since the last
    // pump counts too (a loop stuck right now shows up).
    Window take_window(uint64_t now_us) {
        if (have_pump_) note_gap(elapsed(last_pump_end_us_, now_us));
        Window w = window_;
        window_ = Window{};
        return w;
    }

    uint64_t slow_total() const { return slow_total_; }
    const Config& config() const { return config_; }

    // " | net thread: 812 loops, mean 6.1 ms, max 18.0 ms (receive), longest without
    // a receive 12.0 ms, slow (>250 ms) 0 (0 in all)" - appended to the Net line.
    std::string window_text(const Window& w) const {
        return std::format("net thread: {} loops, mean {:.1f} ms, max {:.1f} ms ({}), longest without a receive "
                           "{:.1f} ms, slow (>{} ms) {} ({} in all)",
                           w.iterations, w.mean_ms(), static_cast<double>(w.max_us) / 1000.0, name(w.max_phase),
                           static_cast<double>(w.max_no_pump_us) / 1000.0, config_.slow_us / 1000, w.slow,
                           slow_total_);
    }

    // "one loop took 6012.3 ms (> 250 ms; mostly 5 s report): steam poll 0.4 ms, receive 1.2 ms
    // (17 datagrams), ... | no receive for 6011.0 ms"
    std::string slow_text(const Iteration& it) const {
        std::string s = std::format("one loop took {:.1f} ms (> {} ms; mostly {}):",
                                    static_cast<double>(it.total_us) / 1000.0, config_.slow_us / 1000,
                                    name(it.worst()));
        bool first = true;
        for (size_t i = 0; i < kPhases; ++i) {
            const uint64_t us = it.phase_us[i];
            if (us < 100 && static_cast<Phase>(i) != Phase::Recv) continue;  // under 0.1 ms: not worth a word
            s += std::format("{} {} {:.1f} ms", first ? "" : ",", name(static_cast<Phase>(i)),
                             static_cast<double>(us) / 1000.0);
            if (static_cast<Phase>(i) == Phase::Recv) s += std::format(" ({} datagrams)", it.received);
            first = false;
        }
        s += std::format(" | no receive for {:.1f} ms", static_cast<double>(it.no_pump_us) / 1000.0);
        return s;
    }

private:
    static uint64_t elapsed(uint64_t from, uint64_t to) { return to > from ? to - from : 0; }
    void note_gap(uint64_t gap) {
        if (gap > window_.max_no_pump_us) window_.max_no_pump_us = gap;
        if (in_iteration_ && gap > iteration_gap_us_) iteration_gap_us_ = gap;
    }

    Config config_;
    Iteration cur_;
    Window window_;
    uint64_t begin_us_ = 0, mark_us_ = 0;
    bool in_iteration_ = false;
    bool have_pump_ = false;
    uint64_t last_pump_end_us_ = 0;
    uint64_t iteration_gap_us_ = 0;  // the longest gap that ended inside the current iteration
    uint64_t slow_total_ = 0;
};

}  // namespace cdmp
