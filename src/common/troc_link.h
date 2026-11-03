#pragma once
// Option D, P7: the Troc tunnel on top of a connected ClientSession. Packets
// pushed here travel as RelKind::TrocFrame batches (troc_frame.h); packets
// that arrive in the other direction go to `on_packet`, in order.
//
// The host's mod uses it with out = ToGuestClient (what its server wrote to
// session 2) and in = ToHostServer (the guest's Req). cdmp_peer's replay mode,
// standing in for the guest, uses the opposite pair.
//
// A tunnel only makes sense inside one connection: if the session reconnects
// (new epoch), what the server and the client believed about each other is
// gone. The link then reports itself broken and drops everything until
// reset() (the owner decides what a new conversation means).
//
// Takes over the session's game handlers (never used together with CoopLink).

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "common/client_session.h"
#include "common/troc_frame.h"

namespace cdmp::troc {

class TrocLink {
public:
    // Fragments in flight above which batches wait here (keeps the reliable
    // channel's own queue, 2048 fragments, from refusing). While the session
    // reports its transport congested, the bound is the channel's window.
    static constexpr size_t kMaxPendingFragments = 1024;

    using PacketFn = std::function<void(std::vector<uint8_t>&& packet, uint32_t now_ms)>;
    TrocLink(ClientSession& session, Dir out, Dir in, PacketFn on_packet);
    ~TrocLink();
    TrocLink(const TrocLink&) = delete;
    TrocLink& operator=(const TrocLink&) = delete;

    // Queues a packet for the other side. False if it is not a packet, the
    // backlog is full, or the link is broken.
    bool push(std::span<const uint8_t> packet);
    // Sends what the channel takes; notices reconnections. Call every loop.
    void tick(uint32_t now_ms);
    // Forget the backlog and the broken state (a new conversation).
    void reset();

    bool connected() const { return session_.state() == ClientSession::State::Connected; }
    // The connection the session is on now (ClientSession::epoch): a new value
    // means a new connection (C4: the host's guest epochs are bound to one).
    uint32_t session_epoch() const { return session_.epoch(); }
    bool broken() const { return broken_; }
    std::string describe() const;
    uint64_t packets_out() const { return sender_.packets_sent(); }
    uint64_t packets_in() const { return receiver_.packets_received(); }
    size_t backlog_bytes() const { return sender_.backlog_bytes(); }

    // Compression of the outgoing batches (p8-tunel-rendimiento.md). The
    // receiving end says once per connection that it can decode them
    // (set_announce_caps on that end: a kCapsOpcode packet right before its
    // first packet on that connection, taken out by the other TrocLink before
    // its owner sees anything); this end compresses
    // only when it wants to (set_compress) AND the other end said so on the
    // CURRENT connection. Same packets, same order either way.
    void set_compress(bool want) { want_compress_ = want; }
    void set_announce_caps(bool on) { caps_ = on ? kCapsCompressed : 0; }
    // The flags this end announces (kCapsCompressed | kCapsGuestSave ...); 0 = nothing is announced.
    void set_announce_flags(uint8_t flags) { caps_ = flags; }
    uint8_t announce_flags() const { return caps_; }
    // What the other end announced on the CURRENT connection (0 = nothing yet, or on an older one).
    uint8_t peer_caps() const;
    bool peer_decodes_compressed() const;
    bool compressing() const { return want_compress_ && peer_decodes_compressed(); }
    uint64_t raw_bytes_out() const { return sender_.raw_bytes_sent(); }
    uint64_t wire_bytes_out() const { return sender_.wire_bytes_sent(); }

private:
    ClientSession& session_;
    TunnelSender sender_;
    TunnelReceiver receiver_;
    // The caps packet only ever travels in ToHostServer batches (troc_frame.h): only a link that
    // receives those takes opcode 2 out (P8 review M7).
    bool caps_in_dir_ = false;
    PacketFn on_packet_;
    uint32_t now_ = 0;
    uint32_t epoch_ = 0;
    bool broken_ = false;
    uint64_t dropped_ = 0;
    bool want_compress_ = false;
    uint8_t caps_ = 0;  // announced flags (0 = announce nothing)
    uint32_t announced_epoch_ = 0;  // the connection our caps went out on (0 = none yet)
    uint8_t peer_caps_ = 0;
    uint32_t peer_caps_epoch_ = 0;  // the connection the other end's caps came on
    uint64_t caps_in_ = 0;
};

}  // namespace cdmp::troc
