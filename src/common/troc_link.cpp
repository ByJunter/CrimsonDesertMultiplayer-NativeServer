#include "common/troc_link.h"

#include <format>

namespace cdmp::troc {

TrocLink::TrocLink(ClientSession& session, Dir out, Dir in, PacketFn on_packet)
    : session_(session),
      sender_(out,
              [this](const std::vector<uint8_t>& m) {
                  // While the transport's queue is full, the batches wait here
                  // (bounded by kMaxBacklogBytes), not in the channel: at most
                  // a window's worth is ready for when it drains.
                  const size_t cap = session_.congested() ? session_.reliable_window() : kMaxPendingFragments;
                  return session_.reliable_pending() < cap &&
                         session_.send_reliable(proto::RelKind::TrocFrame, m, now_);
              }),
      receiver_(in),
      caps_in_dir_(in == Dir::ToHostServer),
      on_packet_(std::move(on_packet)),
      epoch_(session.epoch()) {
    session_.set_game_handlers({}, [this](const ReliableChannel::Message& msg, uint32_t now_ms) {
        if (msg.kind != proto::RelKind::TrocFrame) return;
        if (broken_) {
            ++dropped_;
            return;
        }
        for (auto& p : receiver_.on_message(msg.bytes)) {
            // The other end's capabilities: the link's own business, never its owner's.
            if (const auto v = caps_in_dir_ ? parse(p) : std::optional<PacketView>{}; v && v->opcode == kCapsOpcode) {
                peer_caps_ = v->payload.empty() ? 0 : v->payload[0];
                peer_caps_epoch_ = session_.epoch();
                ++caps_in_;
                continue;
            }
            on_packet_(std::move(p), now_ms);
        }
    });
}

TrocLink::~TrocLink() { session_.set_game_handlers({}, {}); }

bool TrocLink::push(std::span<const uint8_t> packet) {
    if (broken_) {
        ++dropped_;
        return false;
    }
    // Our capabilities go once per connection, right before the first packet
    // of ours on it: never on their own, so a link that has said nothing yet
    // stays one that said nothing (TrocLink::tick's reconnection rule).
    if (caps_ && connected() && announced_epoch_ != session_.epoch()) {
        const uint8_t caps = caps_;
        if (sender_.push(make(kCapsOpcode, -1, std::span<const uint8_t>(&caps, 1)))) announced_epoch_ = session_.epoch();
    }
    if (!sender_.push(packet)) {
        // Not a packet (a bug on our side) or a stalled link: either way the
        // conversation can no longer be trusted.
        broken_ = true;
        return false;
    }
    return true;
}

uint8_t TrocLink::peer_caps() const {
    return peer_caps_epoch_ != 0 && peer_caps_epoch_ == session_.epoch() ? peer_caps_ : uint8_t{0};
}

bool TrocLink::peer_decodes_compressed() const { return (peer_caps() & kCapsCompressed) != 0; }

void TrocLink::tick(uint32_t now_ms) {
    now_ = now_ms;
    if (session_.epoch() != epoch_) {
        // A new connection: the other side's server/client state is gone.
        if (epoch_ != 0 || sender_.packets_sent() > 0 || receiver_.packets_received() > 0) broken_ = true;
        epoch_ = session_.epoch();
        sender_.reset();
    }
    sender_.set_compress(compressing());
    if (!broken_ && connected()) sender_.flush();
}

void TrocLink::reset() {
    sender_.reset();
    broken_ = false;
    epoch_ = session_.epoch();
    // The capabilities belong to the connection, not to the conversation:
    // they stay (a C4 epoch on the same connection keeps them).
}

std::string TrocLink::describe() const {
    std::string s = std::format("Troc tunnel: {}{} | out {} packets in {} batches, backlog {} B | in {} packets, "
                                "{} rejected | dropped {}",
                                connected() ? "connected" : "not connected", broken_ ? " BROKEN" : "",
                                sender_.packets_sent(), sender_.batches_sent(), sender_.backlog_bytes(),
                                receiver_.packets_received(), receiver_.rejected(), dropped_);
    if (want_compress_ || sender_.compressed_batches()) {
        const uint64_t raw = sender_.raw_bytes_sent(), wire = sender_.wire_bytes_sent();
        s += std::format(" | compression {} ({}): {} batches compressed, out {} KB as {} KB (x{:.1f})",
                         compressing() ? "ON" : "off",
                         peer_decodes_compressed() ? "the other end decodes it" : "the other end did not say it decodes it",
                         sender_.compressed_batches(), raw / 1024, wire / 1024,
                         wire ? static_cast<double>(raw) / static_cast<double>(wire) : 1.0);
    }
    if (caps_ || receiver_.compressed_received() || caps_in_) {
        s += std::format(" | in compressed batches {}", receiver_.compressed_received());
    }
    return s;
}

}  // namespace cdmp::troc
