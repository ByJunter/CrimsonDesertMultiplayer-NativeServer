#pragma once
// Datagrams over Steam (ISteamNetworkingMessages): two friends connect with no
// port forwarding, through Valve's relays when needed. Uses the game's own,
// already initialised steam_api64.dll through its flat C exports. Never calls
// SteamAPI_RunCallbacks or ManualDispatch (that would break the game's own
// callbacks) and registers no callbacks: incoming sessions are accepted by
// polling AcceptSessionWithUser for friends who are playing Crimson Desert.
// Design and sources: docs/research/steam-transporte.md.

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/send_gate.h"
#include "common/transport.h"

namespace cdmp::net {

struct SteamFriend {
    uint64_t id;
    std::string name;
};

// Steam networking's send limits, set (process-wide) before our session
// opens. Steam's defaults, 256 KB/s and a 512 KB buffer, made the ~12 MB of a
// P8 world load take minutes and overflow (docs/research/steam-transporte.md).
struct SteamSendLimits {
    int32_t rate_min = 1024 * 1024;     // bytes/s; 0 = leave all three at Steam's defaults
    int32_t rate_max = 8 * 1024 * 1024; // bytes/s
    int32_t buffer = 4 * 1024 * 1024;   // bytes queued before Send returns LimitExceeded
    // Received and not read yet, before Steam drops packets (its defaults 1 MB /
    // 1000 messages, ~1 s of a field load; p8-hilo-red.md). 0 = Steam's default.
    int32_t recv_buffer = 16 * 1024 * 1024;  // bytes
    int32_t recv_messages = 16384;
};

class SteamTransport final : public Transport {
public:
    // accept_from: 0 = accept any immediate friend playing the game (host),
    // otherwise only that SteamID64.
    SteamTransport(int channel, bool host, uint64_t accept_from, SteamSendLimits limits = {});
    ~SteamTransport() override;

    // A message Steam refuses because its buffer is full (LimitExceeded) is
    // kept here, in order, and retried; send() then still returns true.
    bool send(const PeerAddr& to, std::span<const uint8_t> data) override;
    std::optional<size_t> recv(PeerAddr& from, std::span<uint8_t> buf, int timeout_ms) override;
    void poll(uint32_t now_ms) override;
    bool ready() const override { return ready_; }
    // Steam's queue to the peer holds ~100 ms of data or more (SendGate), or
    // refused messages are waiting here.
    bool congested() const override;
    std::string status() const override;

    // Immediate friends currently in Crimson Desert (empty until ready).
    std::vector<SteamFriend> friends_playing();
    uint64_t own_id() const { return own_id_; }
    // Whom we talk to (for connection status in the log).
    void set_peer(uint64_t id) { peer_ = id; }

private:
    struct Api;
    bool resolve();
    void become_ready();
    void fail(const char* what);
    bool apply_limits();
    void drain_incoming();
    void sample_queue(uint32_t now_ms);
    // SendMessageToUser's EResult, or nullopt after an exception (disabled).
    std::optional<int32_t> send_now(uint64_t to, std::span<const uint8_t> data);
    void flush_held(uint32_t now_ms);
    bool hold(uint64_t to, std::span<const uint8_t> data);

    static constexpr uint32_t kSampleEveryMs = 20;     // Steam queue readings for the gate
    static constexpr uint32_t kHeldRetryMs = 10;
    static constexpr size_t kMaxHeldBytes = 1024 * 1024;

    std::unique_ptr<Api> api_;
    int channel_;
    bool host_;
    uint64_t accept_from_;
    SteamSendLimits limits_;
    SendGate gate_;
    uint32_t last_sample_ms_ = 0;
    struct Held {
        uint64_t to;
        std::vector<uint8_t> bytes;
    };
    std::deque<Held> held_;
    size_t held_bytes_ = 0;
    uint32_t last_held_try_ms_ = 0;
    uint64_t limit_exceeded_ = 0, held_total_ = 0, held_dropped_ = 0;
    bool disabled_ = false;
    bool ready_ = false;
    uint64_t own_id_ = 0;
    uint64_t peer_ = 0;
    uint32_t last_poll_ms_ = 0;
    uint32_t started_ms_ = 0;
    int last_session_state_ = -1;
    int last_relay_status_ = -999;
    std::string last_status_ = "-";  // ping, throughput, queue of the session
    uint64_t sent_ = 0, received_ = 0, send_errors_ = 0;
    std::deque<std::pair<uint64_t, std::vector<uint8_t>>> inbox_;
};

}  // namespace cdmp::net
