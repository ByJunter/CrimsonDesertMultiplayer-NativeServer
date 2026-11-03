#pragma once
// P8 guest: the host's packets on their way into the client, and which thread
// does the client's network work (docs/research/pendientes/p8-auditoria-hilos.md).
// Pure logic, no game memory and no clock: guest.cpp feeds it the time and the
// thread ids and keeps the queue under its own mutex.
//
// The native rule (p8-heap-inyeccion.md §3, opcion-d-p8.md §1.1), [V] on the PE:
// every direct call of ClientNet::OnRecvPacket 0x35CA00 is inside the client
// NetworkThread's loop 0x35B800 (0x35BB53; 0x35C951 in 0x35C890, called at
// 0x35BD04) or inside FlushSendQueue 0x35BE90 (0x35C0ED; 0x86E55AA through
// 0x35C490), and FlushSendQueue is only called from that loop (0x35BB05,
// 0x35BC99). NwVirtualSyncSession::Send 0x14380B0 has no direct caller: it is
// the session's vf6, reached from FlushSendQueue (0x35BFC7).
//
// So the guest does its game work only there:
//   the mod's network thread  guest_deliver: parses, logs, QUEUES (this queue);
//                             no game call, no game heap
//   the client NetworkThread  guest_pump (mid-hook 0x35BAD4, every turn
//                             <= 16 ms): takes at most kMaxPerTurn packets,
//                             oldest first, into OnRecvPacket (which copies them
//                             with that thread's own heap, 0x47EF9B0); the
//                             local goodbye answer; the title's slot refresh
//                             guest_send (hook of 0x14380B0, inside
//                             FlushSendQueue on the same thread): copies the
//                             frame out and returns the buffer to Q3 with
//                             0x35DF50, freed by this thread in the same turn
// The owner check below measures that guest_send really runs on the pump's
// thread; it never changes what is done.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <utility>
#include <vector>

namespace cdmp::session2::gin {

struct Limits {
    size_t max_packets = 0;
    size_t max_bytes = 0;
};

// What may wait for the NetworkThread. The byte bound is the one the guest
// always had (64 MB: a zone load is 13,000-42,000 Acks in ~12 s); the count
// bound only stops a flood of tiny packets (a 7-byte Ack x 1 M = 7 MB).
inline constexpr Limits kLimits{size_t{1} << 20, size_t{64} << 20};
// Per NetworkThread turn. Live, a zone load is ~3,500 packets/s, ~60 per turn
// at 62 turns/s: never reached in normal play. After a stall of the
// NetworkThread (a hitch, a menu) the backlog goes in 4096 at a time, in
// order, so one turn never holds the client's sends and frees for long.
inline constexpr size_t kMaxPerTurn = 4096;

struct Item {
    std::vector<uint8_t> packet;
    uint64_t queued_us = 0;
};

enum class Push : uint8_t { Queued, Full };

// FIFO with both bounds. Not thread-safe by itself.
class InboundQueue {
public:
    explicit InboundQueue(Limits limits = kLimits) : limits_(limits) {}

    Push push(std::vector<uint8_t>&& packet, uint64_t now_us) {
        if (q_.size() >= limits_.max_packets || packet.size() > limits_.max_bytes ||
            bytes_ + packet.size() > limits_.max_bytes) {
            ++full_;
            return Push::Full;
        }
        bytes_ += packet.size();
        q_.push_back(Item{std::move(packet), now_us});
        peak_ = std::max(peak_, q_.size());
        peak_bytes_ = std::max(peak_bytes_, bytes_);
        return Push::Queued;
    }
    Push push(std::span<const uint8_t> packet, uint64_t now_us) {
        return push(std::vector<uint8_t>(packet.begin(), packet.end()), now_us);
    }

    // Appends up to `max` of the oldest packets to `out`, in order. Returns how many.
    size_t take(size_t max, std::vector<Item>& out) {
        size_t n = 0;
        while (n < max && !q_.empty()) {
            bytes_ -= q_.front().packet.size();
            out.push_back(std::move(q_.front()));
            q_.pop_front();
            ++n;
        }
        return n;
    }

    // Everything waiting is dropped (the connection it belongs to is over). Returns how many.
    size_t clear() {
        const size_t n = q_.size();
        q_.clear();
        bytes_ = 0;
        cleared_ += n;
        return n;
    }

    size_t size() const { return q_.size(); }
    size_t bytes() const { return bytes_; }
    size_t peak() const { return peak_; }
    size_t peak_bytes() const { return peak_bytes_; }
    uint64_t full() const { return full_; }
    uint64_t cleared() const { return cleared_; }
    const Limits& limits() const { return limits_; }

private:
    Limits limits_;
    std::deque<Item> q_;
    size_t bytes_ = 0, peak_ = 0, peak_bytes_ = 0;
    uint64_t full_ = 0, cleared_ = 0;
};

// What the NetworkThread did with the queue. One turn is gathered locally and
// merged once (guest.cpp keeps the total under the queue's mutex).
struct DeliveryStats {
    uint64_t batches = 0;       // turns that took at least one packet
    uint64_t delivered = 0;     // handed to OnRecvPacket and returned normally
    uint64_t batch_max = 0;
    uint64_t capped = 0;        // turns that left packets for the next one (the cap)
    uint64_t wait_sum_us = 0;   // queued -> handed, over `delivered`
    uint64_t wait_max_us = 0;

    void on_batch(size_t taken, size_t left) {
        if (!taken) return;
        ++batches;
        batch_max = std::max<uint64_t>(batch_max, taken);
        if (left) ++capped;
    }
    void on_delivered(uint64_t queued_us, uint64_t now_us) {
        const uint64_t w = now_us > queued_us ? now_us - queued_us : 0;
        ++delivered;
        wait_sum_us += w;
        wait_max_us = std::max(wait_max_us, w);
    }
    void merge(const DeliveryStats& t) {
        batches += t.batches;
        delivered += t.delivered;
        batch_max = std::max(batch_max, t.batch_max);
        capped += t.capped;
        wait_sum_us += t.wait_sum_us;
        wait_max_us = std::max(wait_max_us, t.wait_max_us);
    }
    double wait_mean_ms() const {
        return delivered ? static_cast<double>(wait_sum_us) / static_cast<double>(delivered) / 1000.0 : 0.0;
    }
};

// The thread that owns the client's network work: the first one that claims
// it (guest_pump, at 0x35BAD4). Anyone else is counted, never refused.
class ThreadOwner {
public:
    enum class Seen : uint8_t { Owner, First, Foreign, FirstForeign };

    Seen check(uint32_t tid) noexcept {
        uint32_t expected = 0;
        if (owner_.compare_exchange_strong(expected, tid, std::memory_order_acq_rel)) return Seen::First;
        if (expected == tid) return Seen::Owner;
        last_foreign_.store(tid, std::memory_order_relaxed);
        return foreign_.fetch_add(1, std::memory_order_relaxed) == 0 ? Seen::FirstForeign : Seen::Foreign;
    }
    uint32_t owner() const noexcept { return owner_.load(std::memory_order_relaxed); }
    uint64_t foreign() const noexcept { return foreign_.load(std::memory_order_relaxed); }
    uint32_t last_foreign() const noexcept { return last_foreign_.load(std::memory_order_relaxed); }

private:
    std::atomic<uint32_t> owner_{0}, last_foreign_{0};
    std::atomic<uint64_t> foreign_{0};
};

}  // namespace cdmp::session2::gin
