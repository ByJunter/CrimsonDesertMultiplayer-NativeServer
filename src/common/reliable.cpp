#include "common/reliable.h"

#include <algorithm>
#include <random>

namespace cdmp {
namespace {

uint64_t frag_key(uint32_t msg_id, uint16_t index) {
    return (static_cast<uint64_t>(msg_id) << 16) | index;
}

// Message ids wrap: "a is before b" by signed distance.
bool before(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) < 0; }

uint32_t random_stream() { return std::random_device{}() | 1u; }

}  // namespace

ReliableChannel::ReliableChannel(SendFn send) : send_(std::move(send)) {
    stream_ = next_id_ = random_stream();
}

bool ReliableChannel::send(proto::RelKind kind, const std::vector<uint8_t>& bytes,
                           uint32_t now_ms) {
    if (bytes.size() > proto::kMaxRelMessage) return false;
    const size_t count = std::max<size_t>(1, (bytes.size() + proto::kRelChunk - 1) / proto::kRelChunk);
    if (queued_.size() + count > kMaxQueued) return false;
    const uint32_t id = next_id_++;
    for (size_t i = 0; i < count; ++i) {
        proto::RelData d;
        d.stream = stream_;
        d.msg_id = id;
        d.index = static_cast<uint16_t>(i);
        d.count = static_cast<uint16_t>(count);
        d.kind = kind;
        const size_t from = i * proto::kRelChunk;
        const size_t to = std::min(bytes.size(), from + proto::kRelChunk);
        d.chunk.assign(bytes.begin() + static_cast<ptrdiff_t>(from),
                       bytes.begin() + static_cast<ptrdiff_t>(to));
        queued_.push_back(std::move(d));
    }
    flush(now_ms);
    return true;
}

uint32_t ReliableChannel::resend_timeout_ms(float rtt_ms, uint32_t resends) {
    // Twice the RTT: the ack of a fragment that waited in a queue still has to
    // come back. Bounded so a bad RTT sample cannot stall the channel.
    const float by_rtt = rtt_ms > 0.0f ? std::min(rtt_ms * 2.0f, static_cast<float>(kMaxResendMs)) : 0.0f;
    uint32_t timeout = std::max(kResendMs, static_cast<uint32_t>(by_rtt));
    // Exponential backoff for a fragment that keeps going unacknowledged.
    for (uint32_t i = 0; i < resends && timeout < kMaxResendMs; ++i) timeout *= 2;
    return std::min(timeout, kMaxResendMs);
}

void ReliableChannel::set_window(size_t window) { window_ = std::clamp<size_t>(window, 1, kMaxWindow); }

bool ReliableChannel::can_send() { return !can_send_ || can_send_(); }

void ReliableChannel::flush(uint32_t now_ms) {
    while (!queued_.empty() && outgoing_.size() < window_) {
        if (!can_send()) {
            ++held_back_;
            return;
        }
        proto::RelData d = std::move(queued_.front());
        queued_.pop_front();
        send_(d);
        const uint64_t key = frag_key(d.msg_id, d.index);
        outgoing_[key] = {std::move(d), now_ms};
    }
}

void ReliableChannel::tick(uint32_t now_ms) {
    size_t n = 0;
    for (auto& [key, out] : outgoing_) {
        if (now_ms - out.last_sent_ms < resend_timeout_ms(rtt_ms_, out.resends)) continue;
        // The link is full: what is in flight is probably only queued, not
        // lost. Resend on a later tick, when the queue has drained.
        if (!can_send()) {
            ++held_back_;
            return;
        }
        send_(out.data);
        out.last_sent_ms = now_ms;
        ++out.resends;
        ++resent_;
        if (++n >= kMaxResendPerTick) break;
    }
    flush(now_ms);
}

void ReliableChannel::on_ack(const proto::RelAck& ack) {
    outgoing_.erase(frag_key(ack.msg_id, ack.index));
}

std::vector<ReliableChannel::Message> ReliableChannel::on_data(const proto::RelData& data) {
    std::vector<Message> out;
    if (data.stream != rx_stream_) {
        if (old_streams_.count(data.stream)) {
            // A straggler of a stream the sender already left: acknowledge so
            // it stops, deliver nothing.
            send_(proto::RelAck{data.msg_id, data.index});
            ++refused_;
            return out;
        }
        // The sender (re)started: its ordering starts at its stream id.
        if (rx_stream_) old_streams_.insert(rx_stream_);
        if (old_streams_.size() > 16) old_streams_.erase(old_streams_.begin());
        rx_stream_ = rx_next_ = data.stream;
        incoming_.clear();
        ready_.clear();
    }
    if (before(data.msg_id, rx_next_) || ready_.count(data.msg_id)) {
        send_(proto::RelAck{data.msg_id, data.index});  // a late duplicate of a delivered message
        return out;
    }
    if (data.msg_id - rx_next_ >= kMaxAhead) {
        ++refused_;  // not kept and not acknowledged: the sender will resend it
        return out;
    }
    auto it = incoming_.find(data.msg_id);
    if (it == incoming_.end()) {
        if (incoming_.size() >= kMaxPartials) {
            // No room: never evict a partial whose fragments were acknowledged
            // (they would never come again); refuse this one, it is resent.
            ++refused_;
            return out;
        }
        Partial p{data.kind, data.count};
        p.chunks.resize(data.count);
        p.got.resize(data.count, false);
        it = incoming_.emplace(data.msg_id, std::move(p)).first;
    }
    Partial& p = it->second;
    if (p.count != data.count || p.kind != data.kind || data.index >= p.count) {
        ++refused_;
        return out;
    }
    send_(proto::RelAck{data.msg_id, data.index});
    if (!p.got[data.index]) {
        p.got[data.index] = true;
        p.chunks[data.index] = data.chunk;
        ++p.have;
    }
    if (p.have == p.count) {
        Message m{p.kind, {}};
        for (auto& c : p.chunks) m.bytes.insert(m.bytes.end(), c.begin(), c.end());
        ready_.emplace(data.msg_id, std::move(m));
        incoming_.erase(it);
    }
    for (auto r = ready_.find(rx_next_); r != ready_.end(); r = ready_.find(rx_next_)) {
        out.push_back(std::move(r->second));
        ready_.erase(r);
        ++rx_next_;
    }
    return out;
}

void ReliableChannel::reset() {
    queued_.clear();
    outgoing_.clear();
    incoming_.clear();
    ready_.clear();
    if (rx_stream_) old_streams_.insert(rx_stream_);
    rx_stream_ = rx_next_ = 0;
    stream_ = next_id_ = random_stream();
}

}  // namespace cdmp
