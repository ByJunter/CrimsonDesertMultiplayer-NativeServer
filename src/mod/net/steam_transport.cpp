#include "mod/net/steam_transport.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <string>

#include "common/clock.h"
#include "common/protocol.h"
#include "common/steam_config.h"
#include "mod/log.h"

namespace cdmp::net {
namespace {

// ---------------------------------------------------------------- flat API
// Our own declarations: the Steamworks SDK is not vendored. Layouts from the
// public SDK (VALVE_CALLBACK_PACK_LARGE = pack 8 on Windows), checked below.
#pragma pack(push, 8)
struct SteamNetworkingIdentity {  // 136 bytes
    int32_t type;                 // ESteamNetworkingIdentityType: 16 = SteamID
    int32_t size;                 // 8 for a SteamID
    union {
        uint64_t steam_id64;
        uint32_t reserved[32];
    };
};
static_assert(sizeof(SteamNetworkingIdentity) == 136);

struct SteamNetworkingMessage {  // SteamNetworkingMessage_t, 216 bytes
    void* data;
    int32_t size;
    uint32_t conn;
    SteamNetworkingIdentity peer;
    int64_t conn_user_data;
    int64_t usec_received;
    int64_t message_number;
    void (*free_data)(SteamNetworkingMessage*);
    void (*release)(SteamNetworkingMessage*);
    int32_t channel;
    int32_t flags;
    int64_t user_data;
    uint16_t lane;
    uint16_t pad;
};
static_assert(offsetof(SteamNetworkingMessage, peer) == 16);
static_assert(offsetof(SteamNetworkingMessage, channel) == 192);
static_assert(sizeof(SteamNetworkingMessage) == 216);

// SteamNetConnectionRealTimeStatus_t (SDK isteamnetworkingtypes.h), 120 bytes.
struct RealTimeStatus {
    int32_t state;
    int32_t ping_ms;
    float quality_local, quality_remote;
    float out_packets_per_sec, out_bytes_per_sec;
    float in_packets_per_sec, in_bytes_per_sec;
    int32_t send_rate_bytes_per_sec;
    int32_t pending_unreliable, pending_reliable, sent_unacked_reliable;
    int64_t queue_time_usec;
    uint32_t reserved[16];
};
static_assert(offsetof(RealTimeStatus, queue_time_usec) == 48);
static_assert(sizeof(RealTimeStatus) == 120);

struct FriendGameInfo {  // FriendGameInfo_t, 24 bytes
    uint64_t game_id;    // CGameID; a plain app = its AppID in the low 24 bits
    uint32_t game_ip;
    uint16_t game_port, query_port;
    uint64_t steam_id_lobby;
};
static_assert(sizeof(FriendGameInfo) == 24);
#pragma pack(pop)

constexpr int32_t kIdentitySteamID = 16;
constexpr int kSendUnreliableNoNagle = 1;
constexpr int kSendAutoRestartBrokenSession = 32;
constexpr int kFriendFlagImmediate = 0x04;
constexpr int kResultOK = 1;
constexpr int kResultLimitExceeded = 25;
constexpr int kSessionConnected = 3;
// ESteamNetworkingConfigValue / Scope / DataType. The scope and type values
// are what the game's steam_api64.dll itself passes in its
// SetGlobalConfigValueInt32 (Global 1, Int32 1) and
// SetConnectionConfigValueInt32 (Connection 4) thunks. The value ids are
// checked by name at run time (GetConfigValueInfo) before anything is set.
constexpr int32_t kConfigScopeGlobal = 1;
constexpr int32_t kConfigTypeInt32 = 1;
constexpr int32_t kGetConfigOK = 1, kGetConfigOKInherited = 2;
constexpr uint32_t kAppId = 3321460;  // Crimson Desert
constexpr uint32_t kPollEveryMs = 500;
constexpr uint32_t kReadyTimeoutMs = 120000;

SteamNetworkingIdentity identity(uint64_t id) {
    SteamNetworkingIdentity i;
    std::memset(&i, 0, sizeof i);
    i.type = kIdentitySteamID;
    i.size = 8;
    i.steam_id64 = id;
    return i;
}

// Every call into Steam goes through one of these: a crash in there would be
// reported to Pearl Abyss as a game crash, so the first exception disables
// the transport instead. Templates with only trivially copyable arguments,
// so __try is allowed.
template <class R, class... P, class... A>
bool call(R& out, R (*fn)(P...), A... args) {
    if (!fn) return false;
    __try {
        out = fn(args...);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
template <class... P, class... A>
bool call_void(void (*fn)(P...), A... args) {
    if (!fn) return false;
    __try {
        fn(args...);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

struct SteamTransport::Api {
    // Accessors and globals.
    int32_t (*get_hsteam_user)() = nullptr;
    void (*release_thread_memory)() = nullptr;
    void* (*messages)() = nullptr;
    void* (*utils)() = nullptr;
    void* (*user)() = nullptr;
    void* (*friends)() = nullptr;
    // ISteamNetworkingMessages
    int32_t (*send_to_user)(void*, const SteamNetworkingIdentity*, const void*, uint32_t, int, int) = nullptr;
    int (*receive)(void*, int, SteamNetworkingMessage**, int) = nullptr;
    bool (*accept)(void*, const SteamNetworkingIdentity*) = nullptr;
    bool (*close)(void*, const SteamNetworkingIdentity*) = nullptr;
    int (*session_info)(void*, const SteamNetworkingIdentity*, void*, void*) = nullptr;
    void (*message_release)(SteamNetworkingMessage*) = nullptr;
    // ISteamNetworkingUtils
    void (*init_relay)(void*) = nullptr;
    int (*relay_status)(void*, void*) = nullptr;
    // Optional (send limits only). GetConfigValueInfo has this 3-argument
    // form since SDK 1.53 (ISteamNetworkingUtils004, which also brought
    // IterateGenericEditableConfigValues: its export is the marker we
    // require; the game's DLL has it, vtable +0x90 / +0x98).
    bool (*set_global_int32)(void*, int32_t, int32_t) = nullptr;
    int32_t (*get_config)(void*, int32_t, int32_t, intptr_t, int32_t*, void*, size_t*) = nullptr;
    const char* (*config_info)(void*, int32_t, int32_t*, int32_t*) = nullptr;
    void* iterate_marker = nullptr;
    // ISteamUser
    uint64_t (*get_steam_id)(void*) = nullptr;
    bool (*logged_on)(void*) = nullptr;
    // ISteamFriends
    const char* (*persona_name)(void*) = nullptr;
    int (*friend_count)(void*, int) = nullptr;
    uint64_t (*friend_by_index)(void*, int, int) = nullptr;
    const char* (*friend_name)(void*, uint64_t) = nullptr;
    bool (*friend_game)(void*, uint64_t, FriendGameInfo*) = nullptr;

    void* msgs = nullptr;
    void* utl = nullptr;
    void* usr = nullptr;
    void* frn = nullptr;
};

SteamTransport::SteamTransport(int channel, bool host, uint64_t accept_from, SteamSendLimits limits)
    : api_(std::make_unique<Api>()),
      channel_(channel),
      host_(host),
      accept_from_(accept_from),
      limits_(limits),
      gate_(SendGate::Config{.assumed_rate = static_cast<uint32_t>(limits.rate_min > 0 ? limits.rate_min
                                                                                       : 256 * 1024)}) {
    started_ms_ = now_ms();
    if (!resolve()) disabled_ = true;
}

SteamTransport::~SteamTransport() {
    if (ready_ && !disabled_ && peer_) {
        const auto id = identity(peer_);
        bool ignored = false;
        call(ignored, api_->close, api_->msgs, &id);
    }
    if (!disabled_) call_void(api_->release_thread_memory);
}

void SteamTransport::fail(const char* what) {
    if (!disabled_) log::error("Steam: exception in {}; Steam transport disabled", what);
    disabled_ = true;
    ready_ = false;
}

bool SteamTransport::resolve() {
    // The game links steam_api64.dll statically: it is loaded already. Never
    // LoadLibrary a second copy.
    HMODULE dll = GetModuleHandleW(L"steam_api64.dll");
    if (!dll) {
        log::error("Steam: steam_api64.dll is not loaded in this process");
        return false;
    }
    Api& a = *api_;
    int missing = 0;
    auto get = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(dll, name));
        if (!fn) {
            log::error("Steam: export {} not found", name);
            ++missing;
        }
    };
    get(a.get_hsteam_user, "SteamAPI_GetHSteamUser");
    get(a.release_thread_memory, "SteamAPI_ReleaseCurrentThreadMemory");
    get(a.messages, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002");
    get(a.utils, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004");
    get(a.user, "SteamAPI_SteamUser_v023");
    get(a.friends, "SteamAPI_SteamFriends_v017");
    get(a.send_to_user, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser");
    get(a.receive, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel");
    get(a.accept, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser");
    get(a.close, "SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser");
    get(a.session_info, "SteamAPI_ISteamNetworkingMessages_GetSessionConnectionInfo");
    get(a.message_release, "SteamAPI_SteamNetworkingMessage_t_Release");
    get(a.init_relay, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess");
    get(a.relay_status, "SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus");
    get(a.get_steam_id, "SteamAPI_ISteamUser_GetSteamID");
    get(a.logged_on, "SteamAPI_ISteamUser_BLoggedOn");
    get(a.persona_name, "SteamAPI_ISteamFriends_GetPersonaName");
    get(a.friend_count, "SteamAPI_ISteamFriends_GetFriendCount");
    get(a.friend_by_index, "SteamAPI_ISteamFriends_GetFriendByIndex");
    get(a.friend_name, "SteamAPI_ISteamFriends_GetFriendPersonaName");
    get(a.friend_game, "SteamAPI_ISteamFriends_GetFriendGamePlayed");
    if (missing) return false;
    log::info("Steam: flat API resolved ({} exports)", 21);
    // Optional: without them the send limits stay at Steam's defaults.
    a.set_global_int32 = reinterpret_cast<decltype(a.set_global_int32)>(
        GetProcAddress(dll, "SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueInt32"));
    a.get_config = reinterpret_cast<decltype(a.get_config)>(
        GetProcAddress(dll, "SteamAPI_ISteamNetworkingUtils_GetConfigValue"));
    a.config_info = reinterpret_cast<decltype(a.config_info)>(
        GetProcAddress(dll, "SteamAPI_ISteamNetworkingUtils_GetConfigValueInfo"));
    a.iterate_marker = reinterpret_cast<void*>(
        GetProcAddress(dll, "SteamAPI_ISteamNetworkingUtils_IterateGenericEditableConfigValues"));
    return true;
}

void SteamTransport::become_ready() {
    Api& a = *api_;
    int32_t hu = 0;
    if (!call(hu, a.get_hsteam_user)) return fail("GetHSteamUser");
    if (hu == 0) return;  // the game has not initialised Steam yet
    if (!call(a.usr, a.user) || !call(a.frn, a.friends) || !call(a.msgs, a.messages) ||
        !call(a.utl, a.utils)) {
        return fail("interface accessors");
    }
    if (!a.usr || !a.frn || !a.msgs || !a.utl) return;
    bool on = false;
    if (!call(on, a.logged_on, a.usr)) return fail("BLoggedOn");
    if (!on) return;
    if (!call(own_id_, a.get_steam_id, a.usr)) return fail("GetSteamID");
    const char* name = nullptr;
    call(name, a.persona_name, a.frn);
    // Before the first send, which opens the session: it takes these values.
    if (!apply_limits()) return;
    if (!call_void(a.init_relay, a.utl)) return fail("InitRelayNetworkAccess");
    ready_ = true;
    log::info("Steam: ready as '{}' (SteamID64 {}), channel {}, {}", name ? name : "?", own_id_,
              channel_, host_ ? "host" : "guest");
    const auto playing = friends_playing();
    if (playing.empty()) {
        log::info("Steam: no friends are playing Crimson Desert right now");
    }
    for (const auto& f : playing) {
        log::info("Steam: friend playing Crimson Desert: '{}' (SteamID64 {})", f.name, f.id);
    }
}

// Raises Steam's send rate and buffer, and its receive buffer (global values:
// every connection of this process inherits them; the game itself does not use
// Steam networking, steam-transporte.md). Each value is found by name first
// (steam_config.h), then set, read back and logged. False only after an
// exception (the transport is disabled).
//
// The receive buffer (p8-hilo-red.md): Steam keeps at most RecvBufferSize
// bytes / RecvBufferMessages messages (defaults 1 MB / 1000) that we have not
// read yet and DROPS the packets beyond that. At the host's 400-900 KB/s of a
// field load that is ~1 s of a network thread that does not read (session 5:
// 4-8 s): everything after it was lost, pings included.
bool SteamTransport::apply_limits() {
    Api& a = *api_;
    const bool send = limits_.rate_min > 0;
    const bool recv = limits_.recv_buffer > 0 || limits_.recv_messages > 0;
    if (!send) log::info("Steam: send limits left at Steam's defaults ([steam] send_rate_min_kb=0)");
    if (!recv) log::info("Steam: receive buffer left at Steam's defaults ([steam] recv_buffer_kb=0, recv_buffer_messages=0)");
    if (!send && !recv) return true;
    if (!a.set_global_int32 || !a.get_config || !a.config_info || !a.iterate_marker) {
        log::warn("Steam: this steam_api64.dll lacks the config exports we know; send and receive limits left at "
                  "Steam's defaults (256 KB/s, 512 KB send buffer, 1 MB / 1000 messages received)");
        return true;
    }
    struct Item {
        int32_t id;
        const char* name;
        int32_t value;
    };
    std::vector<Item> items;
    if (send) {
        // The maximum before the minimum, so the two are never crossed.
        items.push_back({steam_cfg::kSendRateMax, "SendRateMax", std::max(limits_.rate_max, limits_.rate_min)});
        items.push_back({steam_cfg::kSendRateMin, "SendRateMin", limits_.rate_min});
        items.push_back({steam_cfg::kSendBufferSize, "SendBufferSize", limits_.buffer});
    }
    if (limits_.recv_buffer > 0) items.push_back({steam_cfg::kRecvBufferSize, "RecvBufferSize", limits_.recv_buffer});
    if (limits_.recv_messages > 0) {
        items.push_back({steam_cfg::kRecvBufferMessages, "RecvBufferMessages", limits_.recv_messages});
    }
    for (const Item& it : items) {
        bool threw = false;
        auto info_name = [&](int32_t id) -> const char* {
            if (threw) return nullptr;
            int32_t t = 0, sc = 0;
            const char* n = nullptr;
            if (!call(n, a.config_info, a.utl, id, &t, &sc)) threw = true;
            return n;
        };
        const int32_t id = find_config_id(info_name, it.id, it.name);
        if (threw) {
            fail("GetConfigValueInfo");
            return false;
        }
        if (id < 0) {
            log::warn("Steam: this Steam client has no config value named {} (expected id {}): not set", it.name, it.id);
            continue;
        }
        int32_t type = 0, scope = 0;
        const char* name = nullptr;
        if (!call(name, a.config_info, a.utl, id, &type, &scope)) {
            fail("GetConfigValueInfo");
            return false;
        }
        if (type != kConfigTypeInt32) {
            log::warn("Steam: config value {} ({}) has type {}, not int32: not set", id, it.name, type);
            continue;
        }
        bool set = false;
        if (!call(set, a.set_global_int32, a.utl, id, it.value)) {
            fail("SetGlobalConfigValueInt32");
            return false;
        }
        int32_t now_value = 0, out_type = 0;
        size_t size = sizeof now_value;
        int32_t got = 0;
        if (!call(got, a.get_config, a.utl, id, kConfigScopeGlobal, static_cast<intptr_t>(0), &out_type,
                  static_cast<void*>(&now_value), &size)) {
            fail("GetConfigValue");
            return false;
        }
        const bool read = (got == kGetConfigOK || got == kGetConfigOKInherited) && size == sizeof now_value;
        log::info("Steam: {} = {} (asked {}{}{})", it.name, read ? std::to_string(now_value) : "unreadable", it.value,
                  set ? "" : ", Steam refused the value",
                  id != it.id ? std::format(", found as config id {}, not {}", id, it.id) : std::string());
    }
    return true;
}

// A reading of Steam's queue to the peer for the gate, every kSampleEveryMs.
void SteamTransport::sample_queue(uint32_t now_ms) {
    if (!peer_ || now_ms - last_sample_ms_ < kSampleEveryMs) return;
    last_sample_ms_ = now_ms;
    const auto id = identity(peer_);
    int state = 0;
    RealTimeStatus rt{};
    if (!call(state, api_->session_info, api_->msgs, &id, static_cast<void*>(nullptr), static_cast<void*>(&rt))) {
        return fail("GetSessionConnectionInfo");
    }
    if (state < 1 || state > kSessionConnected) return;
    const int64_t pending = static_cast<int64_t>(rt.pending_unreliable) + rt.pending_reliable;
    gate_.on_status(now_ms, pending > 0 ? static_cast<size_t>(pending) : 0,
                    rt.send_rate_bytes_per_sec > 0 ? static_cast<uint32_t>(rt.send_rate_bytes_per_sec) : 0);
}

std::optional<int32_t> SteamTransport::send_now(uint64_t to, std::span<const uint8_t> data) {
    const auto id = identity(to);
    int32_t result = 0;
    if (!call(result, api_->send_to_user, api_->msgs, &id, static_cast<const void*>(data.data()),
              static_cast<uint32_t>(data.size()), kSendUnreliableNoNagle | kSendAutoRestartBrokenSession,
              channel_)) {
        fail("SendMessageToUser");
        return std::nullopt;
    }
    return result;
}

// Keeps a message Steam had no room for; false (lost, as before) if too much waits already.
bool SteamTransport::hold(uint64_t to, std::span<const uint8_t> data) {
    if (held_bytes_ + data.size() > kMaxHeldBytes) {
        ++held_dropped_;
        return false;
    }
    held_.push_back({to, std::vector<uint8_t>(data.begin(), data.end())});
    held_bytes_ += data.size();
    ++held_total_;
    return true;
}

void SteamTransport::flush_held(uint32_t now_ms) {
    if (held_.empty() || now_ms - last_held_try_ms_ < kHeldRetryMs) return;
    last_held_try_ms_ = now_ms;
    while (!held_.empty()) {
        Held& h = held_.front();
        const auto result = send_now(h.to, h.bytes);
        if (!result) return;
        if (*result == kResultLimitExceeded) {
            ++limit_exceeded_;
            gate_.on_refused(now_ms);
            return;  // still full: next try in kHeldRetryMs
        }
        if (*result == kResultOK) {
            ++sent_;
            gate_.on_sent(now_ms, h.bytes.size());
        } else if (send_errors_++ % 100 == 0) {
            log::warn("Steam: SendMessageToUser returned EResult {} ({} errors)", *result, send_errors_);
        }
        held_bytes_ -= h.bytes.size();
        held_.pop_front();
    }
}

bool SteamTransport::congested() const {
    if (!ready_ || disabled_) return false;
    return !held_.empty() || gate_.congested(cdmp::now_ms());
}

void SteamTransport::poll(uint32_t now_ms) {
    if (disabled_) return;
    if (ready_) {
        sample_queue(now_ms);
        if (!disabled_) flush_held(now_ms);
    }
    if (disabled_ || now_ms - last_poll_ms_ < kPollEveryMs) return;
    last_poll_ms_ = now_ms;
    Api& a = *api_;
    if (!ready_) {
        become_ready();
        if (!ready_ && !disabled_ && now_ms - started_ms_ > kReadyTimeoutMs) {
            log::error("Steam: not logged on after {} s (offline mode?); no co-op over Steam",
                       kReadyTimeoutMs / 1000);
            disabled_ = true;
        }
        return;
    }
    if (!call_void(a.release_thread_memory)) return fail("ReleaseCurrentThreadMemory");

    // Host: accept the session of whoever may connect. Sending to someone
    // accepts implicitly, so a guest needs nothing here.
    if (host_) {
        if (accept_from_) {
            const auto id = identity(accept_from_);
            bool ok = false;
            if (!call(ok, a.accept, a.msgs, &id)) return fail("AcceptSessionWithUser");
        } else {
            for (const auto& f : friends_playing()) {
                const auto id = identity(f.id);
                bool ok = false;
                if (!call(ok, a.accept, a.msgs, &id)) return fail("AcceptSessionWithUser");
            }
        }
    }

    int relay = 0;
    if (!call(relay, a.relay_status, a.utl, static_cast<void*>(nullptr))) return fail("GetRelayNetworkStatus");
    if (relay != last_relay_status_) {
        log::info("Steam: relay network status {} (100 = ready)", relay);
        last_relay_status_ = relay;
    }
    if (peer_) {
        const auto id = identity(peer_);
        int state = 0;
        RealTimeStatus rt{};
        if (!call(state, a.session_info, a.msgs, &id, static_cast<void*>(nullptr), static_cast<void*>(&rt))) {
            return fail("GetSessionConnectionInfo");
        }
        if (state == 3) {
            last_status_ = std::format("ping {} ms, out {:.1f} KB/s ({:.0f} pkt/s), in {:.1f} KB/s, send rate {} KB/s, "
                                       "pending {} B, queue {} ms",
                                       rt.ping_ms, rt.out_bytes_per_sec / 1024.0f, rt.out_packets_per_sec,
                                       rt.in_bytes_per_sec / 1024.0f, rt.send_rate_bytes_per_sec / 1024,
                                       rt.pending_unreliable + rt.pending_reliable, rt.queue_time_usec / 1000);
        }
        if (state != last_session_state_) {
            static const char* kNames[] = {"none", "connecting", "finding route", "connected",
                                           "closed by peer", "problem detected locally"};
            log::info("Steam: session with {} is {}", peer_,
                      state >= 0 && state <= 5 ? kNames[state] : std::to_string(state).c_str());
            last_session_state_ = state;
        }
    }
}

std::vector<SteamFriend> SteamTransport::friends_playing() {
    std::vector<SteamFriend> out;
    if (!ready_ || disabled_) return out;
    Api& a = *api_;
    int n = 0;
    if (!call(n, a.friend_count, a.frn, kFriendFlagImmediate)) {
        fail("GetFriendCount");
        return out;
    }
    for (int i = 0; i < n && i < 2000; ++i) {
        uint64_t id = 0;
        if (!call(id, a.friend_by_index, a.frn, i, kFriendFlagImmediate)) {
            fail("GetFriendByIndex");
            return {};
        }
        FriendGameInfo info{};
        bool playing = false;
        if (!call(playing, a.friend_game, a.frn, id, &info)) {
            fail("GetFriendGamePlayed");
            return {};
        }
        if (!playing || (info.game_id & 0xFFFFFF) != kAppId) continue;
        const char* name = nullptr;
        call(name, a.friend_name, a.frn, id);
        out.push_back({id, name ? name : "?"});
    }
    return out;
}

bool SteamTransport::send(const PeerAddr& to, std::span<const uint8_t> data) {
    if (!ready_ || disabled_ || to.kind != PeerAddr::Kind::Steam) return false;
    const uint32_t now = cdmp::now_ms();
    flush_held(now);
    if (disabled_) return false;
    // Behind what already waits, so nothing overtakes it.
    if (!held_.empty()) return hold(to.value, data);
    const auto result = send_now(to.value, data);
    if (!result) return false;
    if (*result == kResultLimitExceeded) {
        // Steam's buffer is full: not lost, kept and retried as it drains.
        // The gate says congested meanwhile, so the reliable channel stops.
        if (limit_exceeded_++ % 100 == 0) {
            log::warn("Steam: send buffer full (EResult 25, {} times); holding messages until it drains",
                      limit_exceeded_);
        }
        gate_.on_refused(now);
        return hold(to.value, data);
    }
    if (*result != kResultOK) {
        if (send_errors_++ % 100 == 0) {
            log::warn("Steam: SendMessageToUser returned EResult {} ({} errors)", *result, send_errors_);
        }
        return false;
    }
    ++sent_;
    gate_.on_sent(now, data.size());
    return true;
}

void SteamTransport::drain_incoming() {
    // 64 per call (16 until session 5): a backlog after a slow turn of the
    // network loop is pulled out of Steam's receive buffer in fewer calls.
    constexpr int kBatch = 64;
    SteamNetworkingMessage* msgs[kBatch];
    int n = 0;
    if (!call(n, api_->receive, api_->msgs, channel_, static_cast<SteamNetworkingMessage**>(msgs), kBatch)) {
        return fail("ReceiveMessagesOnChannel");
    }
    for (int i = 0; i < n && i < kBatch; ++i) {
        SteamNetworkingMessage* m = msgs[i];
        if (m && m->peer.type == kIdentitySteamID && m->size > 0 &&
            static_cast<size_t>(m->size) <= proto::kMaxPacketSize && m->data) {
            const auto* b = static_cast<const uint8_t*>(m->data);
            inbox_.emplace_back(m->peer.steam_id64, std::vector<uint8_t>(b, b + m->size));
            ++received_;
        }
        if (m && !call_void(api_->message_release, m)) return fail("SteamNetworkingMessage_t_Release");
    }
    while (inbox_.size() > 1024) inbox_.pop_front();
}

std::optional<size_t> SteamTransport::recv(PeerAddr& from, std::span<uint8_t> buf, int timeout_ms) {
    const uint32_t until = now_ms() + static_cast<uint32_t>(timeout_ms);
    for (;;) {
        if (inbox_.empty() && ready_ && !disabled_) drain_incoming();
        if (!inbox_.empty()) {
            auto [id, bytes] = std::move(inbox_.front());
            inbox_.pop_front();
            if (bytes.size() > buf.size()) continue;
            std::memcpy(buf.data(), bytes.data(), bytes.size());
            from = PeerAddr::steam(id);
            return bytes.size();
        }
        if (static_cast<int32_t>(now_ms() - until) >= 0) return std::nullopt;
        Sleep(1);
    }
}

std::string SteamTransport::status() const {
    if (disabled_) return "steam: disabled (see earlier errors)";
    if (!ready_) return "steam: waiting for the game's Steam login";
    const uint32_t now = cdmp::now_ms();
    return std::format("steam: id {} | sent {} recv {} send errors {} | session {} | relay {} | {} | "
                       "gate: est {} B / limit {} B{}, buffer full {}, held {} now ({} total, {} dropped)",
                       own_id_, sent_, received_, send_errors_, last_session_state_, last_relay_status_,
                       last_status_, gate_.estimated_pending(now), gate_.limit_bytes(),
                       congested() ? " CONGESTED" : "", limit_exceeded_, held_.size(), held_total_,
                       held_dropped_);
}

}  // namespace cdmp::net
