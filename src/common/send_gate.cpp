#include "common/send_gate.h"

#include <algorithm>

namespace cdmp::net {

size_t SendGate::estimated_pending(uint32_t now_ms) const {
    const uint32_t rate = rate_ ? rate_ : config_.assumed_rate;
    // A clock that went backwards (or a reading from the future) drains nothing.
    const uint32_t elapsed = static_cast<int32_t>(now_ms - base_ms_) > 0 ? now_ms - base_ms_ : 0;
    const uint64_t drained = static_cast<uint64_t>(rate) * elapsed / 1000;
    return drained >= base_pending_ ? 0 : base_pending_ - static_cast<size_t>(drained);
}

size_t SendGate::limit_bytes() const {
    const uint32_t rate = rate_ ? rate_ : config_.assumed_rate;
    const size_t by_rate = static_cast<size_t>(static_cast<uint64_t>(rate) * config_.target_queue_ms / 1000);
    return std::max(config_.min_limit_bytes, by_rate);
}

void SendGate::on_status(uint32_t now_ms, size_t pending_bytes, uint32_t rate_bytes_per_sec) {
    if (rate_bytes_per_sec) rate_ = rate_bytes_per_sec;
    base_pending_ = pending_bytes;
    base_ms_ = now_ms;
}

void SendGate::on_sent(uint32_t now_ms, size_t bytes) {
    base_pending_ = estimated_pending(now_ms) + bytes;
    base_ms_ = now_ms;
}

void SendGate::on_refused(uint32_t now_ms) {
    ++refusals_;
    // Its buffer is full: at least as congested as the limit says, whatever
    // we estimated.
    base_pending_ = std::max(estimated_pending(now_ms), limit_bytes());
    base_ms_ = now_ms;
    refused_ms_ = now_ms;
}

bool SendGate::congested(uint32_t now_ms) const {
    if (refusals_ && now_ms - refused_ms_ < config_.refused_hold_ms) return true;
    return estimated_pending(now_ms) >= limit_bytes();
}

}  // namespace cdmp::net
