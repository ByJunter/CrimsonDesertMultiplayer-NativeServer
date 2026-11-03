#pragma once
// Backpressure for a transport with its own send queue (Steam networking).
// Pure logic, unit-tested: the owner feeds it what it sends, what the
// transport refuses and, now and then, a fresh reading of the transport's
// queue; it says when bulk data (the reliable channel) should wait.
//
// Why: Steam takes every message into a send buffer and drains it at its send
// rate. In the first P8 session (2026-10-09) the reliable channel pushed far
// more than that: the buffer filled (k_EResultLimitExceeded 29,156 times), the
// queue held ~2 s of data, our pings waited behind it (RTT ~2 s) and every
// fragment was resent several times before its ack could come back. Keeping
// the queue to ~100 ms of data leaves the link as fast and pings honest.
//
// Between readings the queue is estimated: what we sent since, minus what the
// send rate drained since.

#include <cstddef>
#include <cstdint>

namespace cdmp::net {

class SendGate {
public:
    struct Config {
        uint32_t target_queue_ms = 100;      // queue allowed before bulk data waits
        size_t min_limit_bytes = 32 * 1024;  // never less than this (slow rates)
        uint32_t assumed_rate = 256 * 1024;  // bytes/s until the transport reports one
        uint32_t refused_hold_ms = 50;       // after a refusal, wait at least this long
    };

    SendGate() : SendGate(Config{}) {}
    explicit SendGate(Config config) : config_(config) {}

    // A fresh reading: bytes the transport holds that are not on the wire yet,
    // and its current send rate in bytes/s (0 = unknown: keep the last one).
    void on_status(uint32_t now_ms, size_t pending_bytes, uint32_t rate_bytes_per_sec);
    // We handed `bytes` to the transport and it took them.
    void on_sent(uint32_t now_ms, size_t bytes);
    // The transport refused a message because its buffer is full.
    void on_refused(uint32_t now_ms);

    size_t estimated_pending(uint32_t now_ms) const;
    // Queue above which congested() is true: target_queue_ms of the send rate.
    size_t limit_bytes() const;
    bool congested(uint32_t now_ms) const;
    uint32_t rate() const { return rate_; }

    uint64_t refusals() const { return refusals_; }

private:
    Config config_;
    uint32_t rate_ = 0;          // bytes/s; 0 = config_.assumed_rate
    size_t base_pending_ = 0;    // the estimate at base_ms_
    uint32_t base_ms_ = 0;
    uint32_t refused_ms_ = 0;
    uint64_t refusals_ = 0;
};

}  // namespace cdmp::net
