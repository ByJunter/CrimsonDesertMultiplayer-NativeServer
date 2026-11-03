#include "mod/session2/slot_refresh.h"

#include <cstring>
#include <format>

namespace cdmp::session2::slot_refresh {

bool Planner::arm(uint64_t key, uint64_t now_ms, Limits limits) {
    if (disabled_) return false;
    if (has_key_ && key == key_) return false;  // once per connection
    has_key_ = true;
    key_ = key;
    pending_ = true;
    attempts_ = 0;
    next_ms_ = now_ms;
    limits_ = limits;
    if (limits_.max_attempts == 0) limits_.max_attempts = 1;
    return true;
}

Outcome Planner::report(Attempt a, uint64_t now_ms) {
    if (!pending_ || disabled_) return disabled_ ? Outcome::Stopped : Outcome::Finished;
    ++attempts_;
    switch (a) {
        case Attempt::Done:
            pending_ = false;
            ++done_;
            return Outcome::Finished;
        case Attempt::Fault:
        case Attempt::Refused:
            pending_ = false;
            disabled_ = true;
            return Outcome::Stopped;
        case Attempt::NotReady:
        case Attempt::Busy:
            break;
    }
    if (attempts_ >= limits_.max_attempts) {
        pending_ = false;
        return Outcome::GaveUp;
    }
    next_ms_ = now_ms + limits_.interval_ms;
    return Outcome::Retry;
}

std::vector<Entry> parse_entries(std::span<const uint8_t> raw, uint32_t count) {
    std::vector<Entry> out;
    for (uint32_t i = 0; i < count && i < kMaxEntriesRead; ++i) {
        const size_t at = static_cast<size_t>(i) * kEntrySize;
        if (at + 16 > raw.size()) break;
        Entry e;
        std::memcpy(&e.slot, raw.data() + at, sizeof e.slot);
        std::memcpy(&e.time, raw.data() + at + 8, sizeof e.time);
        out.push_back(e);
    }
    return out;
}

bool title_would_enable(const std::vector<Entry>& entries) {
    for (const auto& e : entries) {
        if (e.slot < 3 || e.slot - 100u < 9u) return true;
    }
    return false;
}

std::string describe(const std::vector<Entry>& entries) {
    std::string s = "[";
    for (size_t i = 0; i < entries.size(); ++i) {
        s += std::format("{}{} @{}", i ? ", " : "", entries[i].slot, entries[i].time);
    }
    return s + "]";
}

int first_mismatch(std::span<const uint8_t> actual, std::span<const uint8_t> expected) {
    if (actual.size() != expected.size()) return 0;
    for (size_t i = 0; i < actual.size(); ++i) {
        if (actual[i] != expected[i]) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace cdmp::session2::slot_refresh
