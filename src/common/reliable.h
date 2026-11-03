#pragma once
// Reliable, ordered delivery of (large) messages over the unreliable datagram
// link. Pure logic, like ClientSession: the caller provides the clock and a
// send callback, so it is unit-testable without sockets.
//
// A message is split into RelData fragments of up to proto::kRelChunk bytes.
// The receiver acknowledges every fragment it keeps (duplicates too) and
// delivers messages exactly once and in the order they were sent (an enemy's
// spawn before its death before its removal). The sender keeps at most
// kWindow fragments in flight and resends unacknowledged ones until they are
// acknowledged or the channel is reset (reconnection).
//
// Resends wait for the resend timeout: kResendMs, or twice the measured RTT
// when that is longer, doubled for every resend of the same fragment, up to
// kMaxResendMs. Over a transport with its own send queue (Steam) the owner
// also gives a "can send" probe: while it says no, nothing new goes out and
// nothing is resent (a full queue only delays the acks; resending into it
// would multiply the traffic, which is what saturated the first P8 session).
//
// Each channel numbers its messages from a random first id, its "stream",
// which travels in every fragment: a new stream (the other side restarted or
// reset) restarts the receiver's ordering.

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "common/protocol.h"

namespace cdmp {

class ReliableChannel {
public:
    static constexpr uint32_t kResendMs = 250;     // shortest resend timeout
    static constexpr uint32_t kMaxResendMs = 2000; // longest, after backoff
    static constexpr size_t kMaxResendPerTick = 64;
    static constexpr size_t kWindow = 128;        // fragments in flight (unacknowledged), by default
    static constexpr size_t kMaxWindow = 1024;
    static constexpr size_t kMaxQueued = 2048;    // fragments waiting for the window (~2.2 MB)
    static constexpr size_t kMaxPartials = 64;    // incomplete messages held by the receiver
    static constexpr uint32_t kMaxAhead = 4096;   // messages a fragment may be ahead of the next one due

    using SendFn = std::function<void(proto::Body)>;
    using CanSendFn = std::function<bool()>;
    struct Message {
        proto::RelKind kind;
        std::vector<uint8_t> bytes;
    };

    explicit ReliableChannel(SendFn send);

    // How long a fragment waits for its ack before it is resent, given the
    // smoothed RTT (0 = not measured yet) and how often it was resent already.
    static uint32_t resend_timeout_ms(float rtt_ms, uint32_t resends);

    // Asked before every fragment (new or resent); empty = always yes.
    void set_can_send(CanSendFn can_send) { can_send_ = std::move(can_send); }
    // Fragments in flight, 1..kMaxWindow (kWindow by default).
    void set_window(size_t window);
    size_t window() const { return window_; }
    // The link's smoothed RTT in ms, for the resend timeout.
    void set_rtt(float rtt_ms) { rtt_ms_ = rtt_ms; }

    // Fragments and queues; sends what fits in the window now. False if the
    // message is too large or the queue is full.
    bool send(proto::RelKind kind, const std::vector<uint8_t>& bytes, uint32_t now_ms);

    // Resends what is still unacknowledged and sends what the window allows.
    void tick(uint32_t now_ms);

    void on_ack(const proto::RelAck& ack);

    // Acknowledges the fragment when it is kept, and returns the messages it
    // made deliverable, in order (often none, sometimes several).
    std::vector<Message> on_data(const proto::RelData& data);

    // Forget everything (new connection); outgoing messages start a new stream.
    void reset();

    size_t unacked_fragments() const { return outgoing_.size() + queued_.size(); }
    uint64_t resent() const { return resent_; }
    uint64_t refused() const { return refused_; }  // fragments not kept (no room, too far ahead, old stream)
    uint64_t held_back() const { return held_back_; }  // ticks/sends that stopped because the link could not take more

private:
    struct Outgoing {
        proto::RelData data;
        uint32_t last_sent_ms;
        uint32_t resends = 0;
    };
    bool can_send();
    struct Partial {
        proto::RelKind kind;
        uint16_t count;
        uint16_t have = 0;
        std::vector<std::vector<uint8_t>> chunks;
        std::vector<bool> got;
    };
    void flush(uint32_t now_ms);

    SendFn send_;
    CanSendFn can_send_;
    size_t window_ = kWindow;
    float rtt_ms_ = 0.0f;
    // Sending side.
    uint32_t stream_ = 0;
    uint32_t next_id_ = 0;
    std::deque<proto::RelData> queued_;      // waiting for room in the window
    std::map<uint64_t, Outgoing> outgoing_;  // in flight; key: msg_id << 16 | index
    // Receiving side.
    uint32_t rx_stream_ = 0;
    uint32_t rx_next_ = 0;                  // the next message id to deliver
    std::set<uint32_t> old_streams_;        // streams we moved away from: their stragglers are ignored
    std::map<uint32_t, Partial> incoming_;  // msg id -> fragments so far
    std::map<uint32_t, Message> ready_;     // complete, waiting for earlier ones
    uint64_t resent_ = 0, refused_ = 0, held_back_ = 0;
};

}  // namespace cdmp
