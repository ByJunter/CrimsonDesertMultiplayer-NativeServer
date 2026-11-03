#include "common/troc_frame.h"

#include <cstring>

#include "common/lz_block.h"

namespace cdmp::troc {

std::optional<PacketView> parse(std::span<const uint8_t> packet) {
    if (packet.size() < kHeaderSize || packet.size() > kMaxPacketSize) return std::nullopt;
    const uint16_t len = static_cast<uint16_t>(packet[3] | (packet[4] << 8));
    if (len != packet.size() - kHeaderSize) return std::nullopt;
    PacketView v;
    v.opcode = static_cast<uint16_t>(packet[0] | (packet[1] << 8));
    v.slot = static_cast<int8_t>(packet[2]);
    v.payload = packet.subspan(kHeaderSize);
    return v;
}

std::optional<PacketView> parse_internal(std::span<const uint8_t> packet, Dir dir) {
    if (packet.size() < kInternalHeaderSize) return std::nullopt;
    const auto opcode = static_cast<uint16_t>(packet[0] | (packet[1] << 8));
    const auto slot = static_cast<int8_t>(packet[2]);
    if (slot != -1) return std::nullopt;
    const bool ok = dir == Dir::ToHostServer
                        ? opcode == kOpLogoutSessionReq && packet.size() == kInternalHeaderSize
                        : (opcode == kOpLogoutSessionAck || opcode == kOpLogoutUserAck) && packet.size() == kLogoutAckSize;
    if (!ok) return std::nullopt;
    PacketView v;
    v.opcode = opcode;
    v.slot = slot;
    v.payload = packet.subspan(kInternalHeaderSize);
    return v;
}

std::optional<PacketView> parse_wire(std::span<const uint8_t> packet, Dir dir) {
    if (packet.size() >= 2) {
        const auto opcode = static_cast<uint16_t>(packet[0] | (packet[1] << 8));
        if (opcode == kOpLogoutSessionReq || opcode == kOpLogoutSessionAck || opcode == kOpLogoutUserAck) {
            return parse_internal(packet, dir);
        }
    }
    return parse(packet);
}

std::vector<uint8_t> make_logout_session_req() {
    return {static_cast<uint8_t>(kOpLogoutSessionReq), static_cast<uint8_t>(kOpLogoutSessionReq >> 8), 0xFF};
}

std::vector<uint8_t> make(uint16_t opcode, int8_t slot, std::span<const uint8_t> payload) {
    std::vector<uint8_t> p(kHeaderSize + payload.size());
    p[0] = static_cast<uint8_t>(opcode);
    p[1] = static_cast<uint8_t>(opcode >> 8);
    p[2] = static_cast<uint8_t>(slot);
    p[3] = static_cast<uint8_t>(payload.size());
    p[4] = static_cast<uint8_t>(payload.size() >> 8);
    if (!payload.empty()) std::memcpy(p.data() + kHeaderSize, payload.data(), payload.size());
    return p;
}

std::vector<uint8_t> encode_batch(const Batch& batch) {
    size_t total = kBatchHeader;
    for (const auto& p : batch.packets) total += 2 + p.size();
    std::vector<uint8_t> out;
    out.reserve(total);
    out.push_back(static_cast<uint8_t>(batch.dir));
    const size_t n = batch.packets.size();
    out.push_back(static_cast<uint8_t>(n));
    out.push_back(static_cast<uint8_t>(n >> 8));
    for (const auto& p : batch.packets) {
        out.push_back(static_cast<uint8_t>(p.size()));
        out.push_back(static_cast<uint8_t>(p.size() >> 8));
        out.insert(out.end(), p.begin(), p.end());
    }
    return out;
}

std::optional<Batch> decode_batch(std::span<const uint8_t> bytes) {
    if (bytes.size() < kBatchHeader) return std::nullopt;
    Batch b;
    if (bytes[0] != static_cast<uint8_t>(Dir::ToHostServer) &&
        bytes[0] != static_cast<uint8_t>(Dir::ToGuestClient)) {
        return std::nullopt;
    }
    b.dir = static_cast<Dir>(bytes[0]);
    const size_t n = static_cast<size_t>(bytes[1] | (bytes[2] << 8));
    size_t at = kBatchHeader;
    b.packets.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (bytes.size() - at < 2) return std::nullopt;
        const size_t len = static_cast<size_t>(bytes[at] | (bytes[at + 1] << 8));
        at += 2;
        if (bytes.size() - at < len) return std::nullopt;
        const auto pkt = bytes.subspan(at, len);
        if (!parse_wire(pkt, b.dir)) return std::nullopt;
        b.packets.emplace_back(pkt.begin(), pkt.end());
        at += len;
    }
    if (at != bytes.size()) return std::nullopt;
    return b;
}

std::optional<std::vector<uint8_t>> compress_batch(std::span<const uint8_t> encoded) {
    if (encoded.size() < kBatchHeader || encoded.size() > kMaxRawBatchBytes) return std::nullopt;
    const std::vector<uint8_t> z = lz::compress(encoded);
    if (z.size() + kCompressedHeader >= encoded.size()) return std::nullopt;
    std::vector<uint8_t> out;
    out.reserve(kCompressedHeader + z.size());
    out.push_back(static_cast<uint8_t>(encoded[0] | kCompressedFlag));
    const auto raw = static_cast<uint32_t>(encoded.size());
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(raw >> (8 * i)));
    out.insert(out.end(), z.begin(), z.end());
    return out;
}

std::optional<Batch> decode_message(std::span<const uint8_t> bytes) {
    if (bytes.empty()) return std::nullopt;
    if (!(bytes[0] & kCompressedFlag)) return decode_batch(bytes);
    if (bytes.size() < kCompressedHeader) return std::nullopt;
    const uint32_t raw = static_cast<uint32_t>(bytes[1]) | (static_cast<uint32_t>(bytes[2]) << 8) |
                         (static_cast<uint32_t>(bytes[3]) << 16) | (static_cast<uint32_t>(bytes[4]) << 24);
    if (raw < kBatchHeader || raw > kMaxRawBatchBytes) return std::nullopt;
    std::vector<uint8_t> plain;
    if (!lz::decompress(bytes.subspan(kCompressedHeader), raw, plain)) return std::nullopt;
    // The inner batch says the same direction as the outer byte.
    if (plain[0] != static_cast<uint8_t>(bytes[0] & ~kCompressedFlag)) return std::nullopt;
    return decode_batch(plain);
}

// ---------------------------------------------------------------- PacketQueue

bool PacketQueue::push(std::span<const uint8_t> packet) {
    std::lock_guard lock(mu_);
    if (bytes_ + packet.size() > max_bytes_) {
        ++dropped_;
        return false;
    }
    q_.emplace_back(packet.begin(), packet.end());
    bytes_ += packet.size();
    return true;
}

std::deque<std::vector<uint8_t>> PacketQueue::take_all() {
    std::deque<std::vector<uint8_t>> out;
    std::lock_guard lock(mu_);
    out.swap(q_);
    bytes_ = 0;
    return out;
}

void PacketQueue::clear() {
    std::lock_guard lock(mu_);
    q_.clear();
    bytes_ = 0;
}

size_t PacketQueue::bytes() const {
    std::lock_guard lock(mu_);
    return bytes_;
}

uint64_t PacketQueue::dropped() const {
    std::lock_guard lock(mu_);
    return dropped_;
}

// ---------------------------------------------------------------- TunnelSender

bool TunnelSender::push(std::span<const uint8_t> packet) {
    if (!parse_wire(packet, dir_)) {
        ++refused_;
        return false;
    }
    if (backlog_bytes_ + packet.size() > kMaxBacklogBytes) {
        ++refused_;
        return false;
    }
    backlog_.emplace_back(packet.begin(), packet.end());
    backlog_bytes_ += packet.size();
    return true;
}

size_t TunnelSender::flush() {
    size_t sent = 0;
    size_t compressed_now = 0;  // raw bytes compressed in this call
    for (;;) {
        if (pending_.empty()) {
            if (backlog_.empty()) break;
            if (compress_ && compressed_now >= kMaxCompressPerFlush) break;  // the rest on the next loop
            Batch b{dir_, {}};
            size_t size = kBatchHeader;
            while (!backlog_.empty() && b.packets.size() < kMaxBatchPackets) {
                const size_t add = 2 + backlog_.front().size();
                // Always take at least one packet (each fits in a message).
                if (!b.packets.empty() && size + add > kMaxBatchBytes) break;
                size += add;
                backlog_bytes_ -= backlog_.front().size();
                b.packets.push_back(std::move(backlog_.front()));
                backlog_.pop_front();
            }
            pending_packets_ = b.packets.size();
            pending_ = encode_batch(b);
            pending_raw_ = pending_.size();
            pending_compressed_ = false;
            if (compress_ && pending_.size() >= kMinCompressBytes) {
                compressed_now += pending_.size();
                if (auto z = compress_batch(pending_)) {
                    pending_ = std::move(*z);
                    pending_compressed_ = true;
                }
            }
        }
        if (!send_(pending_)) break;  // channel full: retry this same batch later
        packets_sent_ += pending_packets_;
        ++batches_sent_;
        raw_bytes_sent_ += pending_raw_;
        wire_bytes_sent_ += pending_.size();
        if (pending_compressed_) ++compressed_batches_;
        ++sent;
        pending_.clear();
        pending_packets_ = 0;
        pending_raw_ = 0;
        pending_compressed_ = false;
    }
    return sent;
}

void TunnelSender::reset() {
    backlog_.clear();
    backlog_bytes_ = 0;
    pending_.clear();
    pending_packets_ = 0;
    pending_raw_ = 0;
    pending_compressed_ = false;
}

// ---------------------------------------------------------------- TunnelReceiver

std::vector<std::vector<uint8_t>> TunnelReceiver::on_message(std::span<const uint8_t> message) {
    auto b = decode_message(message);
    if (!b || b->dir != expected_) {
        ++rejected_;
        return {};
    }
    if (!message.empty() && (message[0] & kCompressedFlag)) ++compressed_received_;
    packets_received_ += b->packets.size();
    return std::move(b->packets);
}

}  // namespace cdmp::troc
