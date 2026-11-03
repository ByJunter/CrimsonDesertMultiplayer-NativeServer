#pragma once
// Where one tick of a network-thread step goes, part by part
// (docs/research/pendientes/p8-tunel-rendimiento.md). Pure logic with the
// clock passed in, so the tests can drive it; LoopHealth (loop_health.h)
// says which phase of the loop was slow, this says what inside it.
//
// Use, once per tick:
//   begin(t); ... mark(kPartA, t) ... mark(kPartB, t) ...
//   if (end(t) > slow) log(text(names));
// and on the 5 s report: window_text(names) (then take_window()).

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace cdmp {

class TickProfile {
public:
    static constexpr size_t kMaxParts = 16;

    void begin(uint64_t now_us) {
        parts_.fill(0);
        begin_us_ = mark_us_ = now_us;
        active_ = true;
    }
    // `part` ran from the previous mark (or begin) until now.
    void mark(size_t part, uint64_t now_us) {
        if (!active_ || part >= kMaxParts) return;
        parts_[part] += now_us > mark_us_ ? now_us - mark_us_ : 0;
        mark_us_ = now_us;
    }
    // Ends the tick; its duration in microseconds.
    uint64_t end(uint64_t now_us, uint64_t slow_us) {
        if (!active_) return 0;
        active_ = false;
        total_us_ = now_us > begin_us_ ? now_us - begin_us_ : 0;
        ++w_ticks_;
        if (total_us_ > w_max_us_) {
            w_max_us_ = total_us_;
            w_max_part_ = worst();
        }
        if (total_us_ > slow_us) {
            ++w_slow_;
            ++slow_total_;
        }
        return total_us_;
    }
    uint64_t total_us() const { return total_us_; }
    uint64_t part_us(size_t part) const { return part < kMaxParts ? parts_[part] : 0; }
    size_t worst() const {
        size_t w = 0;
        for (size_t i = 1; i < kMaxParts; ++i) {
            if (parts_[i] > parts_[w]) w = i;
        }
        return w;
    }

    // "412.3 ms: barrier 405.1 ms, user 2 sample 3.0 ms" (parts of 0.1 ms or more).
    std::string text(std::span<const char* const> names) const {
        std::string s = std::format("{:.1f} ms:", static_cast<double>(total_us_) / 1000.0);
        bool first = true;
        for (size_t i = 0; i < kMaxParts && i < names.size(); ++i) {
            if (parts_[i] < 100) continue;
            s += std::format("{} {} {:.1f} ms", first ? "" : ",", names[i], static_cast<double>(parts_[i]) / 1000.0);
            first = false;
        }
        if (first) s += " no part above 0.1 ms";
        return s;
    }

    // "ticks 812, max 3.1 ms (outgoing), above 100 ms 0 (0 in all)" since the last take_window().
    std::string window_text(std::span<const char* const> names, uint64_t slow_us) const {
        return std::format("ticks {}, max {:.1f} ms ({}), above {} ms {} ({} in all)", w_ticks_,
                           static_cast<double>(w_max_us_) / 1000.0,
                           w_max_part_ < names.size() ? names[w_max_part_] : "?", slow_us / 1000, w_slow_, slow_total_);
    }
    void take_window() {
        w_ticks_ = w_slow_ = w_max_us_ = 0;
        w_max_part_ = 0;
    }
    uint64_t window_max_us() const { return w_max_us_; }
    uint64_t slow_total() const { return slow_total_; }

private:
    std::array<uint64_t, kMaxParts> parts_{};
    uint64_t begin_us_ = 0, mark_us_ = 0, total_us_ = 0;
    bool active_ = false;
    uint64_t w_ticks_ = 0, w_slow_ = 0, w_max_us_ = 0, slow_total_ = 0;
    size_t w_max_part_ = 0;
};

}  // namespace cdmp
