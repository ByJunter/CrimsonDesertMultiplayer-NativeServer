#pragma once
// Steam networking's configuration values by NAME (pure logic, unit-tested;
// the calls into Steam are in src/mod/net/steam_transport.cpp).
//
// ESteamNetworkingConfigValue ids are only numbers in the SDK headers, which
// are not vendored here. Every value the mod sets is first checked by name
// with GetConfigValueInfo: the id we expect is tried first and, if Steam names
// it differently (another client version), the ids are scanned for the name.
// A value Steam does not know is left alone.

#include <cstdint>
#include <cstring>
#include <string_view>

namespace cdmp::net {

// ESteamNetworkingConfigValue as in the public SDK's steamnetworkingtypes.h
// (checked by name at run time, see find_config_id).
namespace steam_cfg {
constexpr int32_t kSendBufferSize = 9;
constexpr int32_t kSendRateMin = 10;
constexpr int32_t kSendRateMax = 11;
// "Upper limit on total size (bytes) / number of received messages that will be
// buffered waiting to be processed by the application. If this limit is
// exceeded, packets will be dropped." Steam's defaults: 1 MB and 1000.
constexpr int32_t kRecvBufferSize = 47;
constexpr int32_t kRecvBufferMessages = 48;
constexpr int32_t kMaxScannedId = 512;
}  // namespace steam_cfg

// info(id) -> const char* name of the value with that id (nullptr: none). The
// id whose name is `name`: `guess` if it has it, else the first id in
// [1, max_id] that has it; -1 if none.
template <class InfoFn>
int32_t find_config_id(InfoFn&& info, int32_t guess, std::string_view name,
                       int32_t max_id = steam_cfg::kMaxScannedId) {
    auto named = [&](int32_t id) {
        const char* n = info(id);
        return n && std::string_view(n) == name;
    };
    if (guess > 0 && named(guess)) return guess;
    for (int32_t id = 1; id <= max_id; ++id) {
        if (id != guess && named(id)) return id;
    }
    return -1;
}

}  // namespace cdmp::net
