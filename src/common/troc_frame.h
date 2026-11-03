#pragma once
// Option D (docs/OPCION-D-EMULADOR.md §4.7, piece P7): raw game messages
// ("Troc" packets) carried between the two PCs, so the guest's client can talk
// to the host's in-process server as its second user.
//
// A Troc packet is what the game's packet functions handle:
//   u16 opcode | i8 slot | u16 payload_len | payload[payload_len]
// (troc-sintesis.md). Opcodes change between game builds: they travel as
// they are, both PCs must run the same build.
//
// Packets travel in batches, one batch per reliable message
// (proto::RelKind::TrocFrame), so delivery is ordered and exactly once, and
// a login burst of ~60 large Acks does not cost one message each:
//   u8 dir | u16 count | count x (u16 len | packet[len])
//
// Pure logic, no game and no sockets: unit-tested in tests/optd_tests.cpp.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <vector>
#include <utility>

namespace cdmp::troc {

inline constexpr size_t kHeaderSize = 5;
// The game's packet buffers hold 0x8000 bytes including the 0x1F bytes of
// transport frame (troc-dispatch.md); the sniffer never saw a larger packet.
inline constexpr size_t kMaxPacketSize = 0x8000 - 0x20;

struct PacketView {
    uint16_t opcode = 0;
    int8_t slot = -1;
    std::span<const uint8_t> payload;
};

// Null unless the bytes are one whole packet: header present and
// payload_len == size - 5 (the game drops anything else in silence).
std::optional<PacketView> parse(std::span<const uint8_t> packet);
std::vector<uint8_t> make(uint16_t opcode, int8_t slot, std::span<const uint8_t> payload);

// Which way a batch goes. Only the host's server and the guest's client
// talk; the host's own client keeps its in-memory loopback.
enum class Dir : uint8_t {
    ToHostServer = 1,  // the guest's Req, for the host server's session 2
    ToGuestClient = 2, // the host server's Ack/Nak written to session 2
};

// Internal session messages (kind 0x7FFFFFFE) have no u16 length: 3 header
// bytes (opcode, slot) and a fixed body (docs/research/pendientes/salida.md
// §1). The tunnel carries exactly these, nothing else of that kind (not the
// global shutdown 2790/2038 nor 2004, which holds pointers):
//   C->S LogoutSessionToServerReq 2001: D1 07 FF            (3 bytes, 0x8CAD10)
//   S->C LogoutSessionToServerAck 2002: D2 07 FF | u32 index (7 bytes)
//   S->C LogoutUserToServerAck    2003: D3 07 FF | u32       (7 bytes)
inline constexpr uint16_t kOpLogoutSessionReq = 2001;
inline constexpr uint16_t kOpLogoutSessionAck = 2002;
inline constexpr uint16_t kOpLogoutUserAck = 2003;
inline constexpr size_t kInternalHeaderSize = 3;
inline constexpr size_t kLogoutAckSize = 7;
// One of those internal messages, allowed in that direction, exactly as the
// game builds it (size, opcode, slot -1); null for anything else.
std::optional<PacketView> parse_internal(std::span<const uint8_t> packet, Dir dir);
// What crosses the tunnel in that direction: a whole ordinary packet
// (parse()), or one of the internal messages above. The internal opcodes are
// only ever accepted in their exact internal form.
std::optional<PacketView> parse_wire(std::span<const uint8_t> packet, Dir dir);
// The guest's native goodbye, as 0x8CAD10 publishes it: D1 07 FF.
std::vector<uint8_t> make_logout_session_req();

// Control packets of the tunnel itself, inside ToGuestClient batches. Opcode 1
// is no game message (Troc opcodes are 1002-9999): the guest's mod takes them
// out before anything reaches the game. Payload: u8 state.
inline constexpr uint16_t kControlOpcode = 1;
inline constexpr uint8_t kControlHostReady = 1;  // the host's session 2 exists and its tunnel end is up
inline constexpr uint8_t kControlHostDead = 2;   // the host stopped its end (the conversation is lost)
// P8 session 9: payload u8 4 | u32 the host's main character key (0xA...), once a second while known; the guest's
// ride_filter only touches that character's actions. An older guest ignores an unknown state.
inline constexpr uint8_t kControlHostCharacter = 4;
// P8 re-entry (session2/rejoin_logic.h, docs/research/s25/reentrada.md): state 13 REJOIN (ToHostServer, 11-byte payload)
// asks the host for a new guest epoch on the same connection; state 14 EPOCH (ToGuestClient, 12-byte payload) is the
// host's C4 state and epoch, sent only to a guest whose caps packet has kCapsRejoin. Older peers ignore both.
inline constexpr uint8_t kControlRejoin = 13;
inline constexpr uint8_t kControlEpoch = 14;
// P8 session 25 weather (session2/weather_sync_logic.h): state 12, ToGuestClient only, u8 12 | u8 version | 270 bytes
// (the host's weather vector, 277-byte frame), sent only to a guest whose caps packet has 0x04 (kCapsWeatherSync
// there); an older guest ignores it.
// (State 3 travels the other way, ToHostServer: child_reannounce, session2/reannounce_logic.h.)
// P8 mounts: state 5 travels BOTH ways (mount_sync_logic.h): u8 5 | u8 version | 42 bytes, the controller's
// MountBegin / MountCommit / MountDetach of its own rider. Exactly that 44-byte form is taken out; an older peer
// ignores it (guest) or drops it as no server Req (host).
// P8 session 25, the guest's own save (phase B, session2/guest_save_wire.h): states 6 SAVE_OFFER, 7 SAVE_CHUNK,
// 8 SAVE_ACK, 9 SAVE_RESULT, 10 SAVE_STATUS, 11 SAVE_RESULT_ACK, in both directions as that header says. A peer that
// does not know them ignores them (guest) or drops them as no server Req (host).
// P8 session 29, the tree bend (session2/tree_bend_logic.h): state 15 travels BOTH ways, u8 15 | u8 version | 50 bytes
// (57-byte frame), the thrower's own SetBend values. Exactly that form is taken out; an older peer ignores it (guest)
// or drops it as no server Req (host).
// The tunnel's own capability packet (TrocLink, p8-tunel-rendimiento.md):
// opcode 2, payload u8 flags. Only in ToHostServer batches: the guest's end
// says what it can decode. TrocLink takes it out before anything else sees
// it (an older host drops it as no server Req).
inline constexpr uint16_t kCapsOpcode = 2;
inline constexpr uint8_t kCapsCompressed = 0x01;  // decodes compressed batches (below)
inline constexpr uint8_t kCapsGuestSave = 0x02;   // speaks control states 6-11 (the guest's own save, phase B)
// 0x04: the host's weather, state 12 (session2/weather_sync_logic.h kCapsWeatherSync).
inline constexpr uint8_t kCapsRejoin = 0x08;      // speaks control states 13-14 (the re-entry, session2/rejoin_logic.h)

struct Batch {
    Dir dir = Dir::ToHostServer;
    std::vector<std::vector<uint8_t>> packets;
};
inline constexpr size_t kBatchHeader = 3;
inline constexpr size_t kMaxBatchPackets = 0xFFFF;

std::vector<uint8_t> encode_batch(const Batch& batch);
// Null for anything malformed: unknown direction, truncated, trailing bytes,
// or a packet that is not whole (parse_wire() for the batch's direction fails).
std::optional<Batch> decode_batch(std::span<const uint8_t> bytes);

// A compressed batch (p8-tunel-rendimiento.md): the same encoded batch,
// compressed with lz_block.h, in one TrocFrame message:
//   u8 dir | 0x80 | u32 raw size | compressed bytes
// Same order, same packets: only the bytes on the wire change. A receiver
// takes both forms; a sender compresses only for a peer that said it can
// (kCapsCompressed).
inline constexpr uint8_t kCompressedFlag = 0x80;
inline constexpr size_t kCompressedHeader = 5;
inline constexpr size_t kMaxRawBatchBytes = 256 * 1024;  // the decoder's bound
// `encoded` (encode_batch's output) compressed; nullopt when that would not
// save anything (the raw form goes then).
std::optional<std::vector<uint8_t>> compress_batch(std::span<const uint8_t> encoded);
// A TrocFrame message in either form; null for anything malformed.
std::optional<Batch> decode_message(std::span<const uint8_t> bytes);

// Thread-safe FIFO of packets between a game thread (hooks: producer) and
// the mod's network thread (consumer), with a byte budget. Each lock is held
// only for a push or a swap, so a game thread never waits on the network.
class PacketQueue {
public:
    explicit PacketQueue(size_t max_bytes) : max_bytes_(max_bytes) {}

    // False (and counted) if the budget is exhausted. A lost Troc packet
    // breaks the conversation, so the owner treats any drop as fatal.
    bool push(std::span<const uint8_t> packet);
    // Everything queued so far, oldest first.
    std::deque<std::vector<uint8_t>> take_all();
    void clear();
    // S33: filtrado estable sin extraer/reinsertar ni reservar paquetes. El predicado
    // debe ser noexcept y sólo conservar o reducir el vector; se llama bajo mu_.
    template<class Keep> void filter_in_place(Keep keep) noexcept {
        static_assert(noexcept(keep(std::declval<std::vector<uint8_t>&>())));
        std::lock_guard lock(mu_);
        size_t write = 0, total = 0;
        for (size_t read = 0; read < q_.size(); ++read) {
            if (!keep(q_[read])) continue;
            total += q_[read].size();
            if (write != read) q_[write] = std::move(q_[read]);
            ++write;
        }
        while (q_.size() > write) q_.pop_back();
        bytes_ = total;
    }

    size_t bytes() const;
    uint64_t dropped() const;

private:
    mutable std::mutex mu_;
    std::deque<std::vector<uint8_t>> q_;
    size_t bytes_ = 0;
    size_t max_bytes_;
    uint64_t dropped_ = 0;
};

// One direction of the tunnel on the sending side: packets are batched into
// messages of up to kMaxBatchBytes and handed to `send` (the reliable
// channel). When `send` refuses (its queue is full), the batch is kept and
// retried on the next flush, in order; nothing is reordered or dropped here.
class TunnelSender {
public:
    static constexpr size_t kMaxBatchBytes = 64 * 1024;
    // Hard cap on what may wait here for the channel (a stalled link).
    static constexpr size_t kMaxBacklogBytes = 16 * 1024 * 1024;

    using SendFn = std::function<bool(const std::vector<uint8_t>& message)>;
    TunnelSender(Dir dir, SendFn send) : dir_(dir), send_(std::move(send)) {}

    // Raw bytes compressed in one flush() at most: compressing costs CPU on
    // the network thread (~1 MB is a few ms); the rest waits for the next loop.
    static constexpr size_t kMaxCompressPerFlush = 1024 * 1024;
    // Smaller batches go raw (nothing to gain).
    static constexpr size_t kMinCompressBytes = 256;

    // Invalid packets are refused (and counted); false also when the backlog
    // is full, which the owner must treat as a broken tunnel.
    bool push(std::span<const uint8_t> packet);
    // Sends as many whole batches as the channel takes. Returns how many.
    size_t flush();
    void reset();
    // Batches built from now on are compressed (compress_batch) when it saves
    // bytes. Order and content are the same either way.
    void set_compress(bool on) { compress_ = on; }
    bool compress() const { return compress_; }

    size_t backlog_bytes() const { return backlog_bytes_; }
    uint64_t packets_sent() const { return packets_sent_; }
    uint64_t batches_sent() const { return batches_sent_; }
    uint64_t refused() const { return refused_; }
    // Bytes of the batches sent: as encoded, and as they went (compressed or not).
    uint64_t raw_bytes_sent() const { return raw_bytes_sent_; }
    uint64_t wire_bytes_sent() const { return wire_bytes_sent_; }
    uint64_t compressed_batches() const { return compressed_batches_; }

private:
    Dir dir_;
    SendFn send_;
    std::deque<std::vector<uint8_t>> backlog_;  // packets not in a sent batch yet
    size_t backlog_bytes_ = 0;
    std::vector<uint8_t> pending_;  // an encoded batch `send` refused
    size_t pending_packets_ = 0;
    size_t pending_raw_ = 0;        // its size before compression
    bool pending_compressed_ = false;
    bool compress_ = false;
    uint64_t packets_sent_ = 0, batches_sent_ = 0, refused_ = 0;
    uint64_t raw_bytes_sent_ = 0, wire_bytes_sent_ = 0, compressed_batches_ = 0;
};

// Receiving side: checks the direction and unpacks a batch.
class TunnelReceiver {
public:
    explicit TunnelReceiver(Dir expected) : expected_(expected) {}
    // The packets of one TrocFrame message, in order; empty (and counted as
    // rejected) if it is malformed or goes the other way.
    std::vector<std::vector<uint8_t>> on_message(std::span<const uint8_t> message);

    uint64_t packets_received() const { return packets_received_; }
    uint64_t rejected() const { return rejected_; }
    uint64_t compressed_received() const { return compressed_received_; }

private:
    Dir expected_;
    uint64_t packets_received_ = 0, rejected_ = 0, compressed_received_ = 0;
};

}  // namespace cdmp::troc
