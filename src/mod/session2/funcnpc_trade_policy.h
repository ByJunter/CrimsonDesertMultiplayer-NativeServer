#pragma once
// S41 review W4 (the other AI's branch opcion-d-tenderos), ported BY HAND in S46 phase 1, with the resources' file-name
// hashes (the 2661's @27, playData +0x20) from the game's dictionary (docs/research/s25/s46-datos/funcnpc.json):
// build 1.0.0.2976. The positive list of the measured trade interactions:
//   cd_seq_funcnpc_smithy  0xA7132561  S13 (host.log line 893): native 2916 then 2436 (tiendas-u2.md 11.1)
//   cd_seq_funcnpc_butcher 0x978345CA  S14 (host.log line 1934; guest BIN offsets 38406114..38444220)
// These traces establish the resource and the shop interaction, NOT the absence of every script side effect; native
// world eligibility still applies. Never widen this list from the funcnpc prefix or a guessed name.
#include <array>
#include <cstdint>
#include <string_view>

namespace cdmp::session2::u2trade {

struct Resource {
    std::string_view name;
    uint32_t hash;
};
inline constexpr std::array<Resource, 2> kResources = {{
    {"cd_seq_funcnpc_smithy", 0xA7132561u},
    {"cd_seq_funcnpc_butcher", 0x978345CAu},
}};
inline constexpr bool is_trade_resource(std::string_view base) noexcept {
    for (const Resource& r : kResources) {
        if (base == r.name) return true;
    }
    return false;
}
inline constexpr bool is_trade_hash(uint32_t hash) noexcept {
    for (const Resource& r : kResources) {
        if (hash == r.hash) return true;
    }
    return false;
}
inline constexpr std::string_view trade_name(uint32_t hash) noexcept {
    for (const Resource& r : kResources) {
        if (hash == r.hash) return r.name;
    }
    return {};
}

// S46 phase 3, T1 by EXACT hash: the dictionary's file-name hash of a resource base name is Bob Jenkins' lookup3
// hashlittle(name, 0x000C5EDE) over its bytes [V offline: 156 of 156 names of s46-datos/funcnpc.json, among them the
// two above]. U-f reads the base name (the hash itself is only in playData +0x20, after the prepare): it hashes the
// name it read and the LIST decides by that hash.
inline constexpr uint32_t kDictSeed = 0x000C5EDEu;
inline constexpr uint32_t rotl32(uint32_t x, int k) noexcept { return (x << k) | (x >> (32 - k)); }
inline constexpr uint32_t dict_hash(std::string_view s, uint32_t seed = kDictSeed) noexcept {
    const auto byte = [&](size_t i) { return static_cast<uint32_t>(static_cast<unsigned char>(s[i])); };
    const auto word = [&](size_t i, size_t n) {  // n (1..4) bytes little-endian from i
        uint32_t w = 0;
        for (size_t b = 0; b < n; ++b) w |= byte(i + b) << (8 * b);
        return w;
    };
    size_t len = s.size(), i = 0;
    uint32_t a = 0xDEADBEEFu + static_cast<uint32_t>(len) + seed, b = a, c = a;
    while (len > 12) {
        a += word(i, 4);
        b += word(i + 4, 4);
        c += word(i + 8, 4);
        a -= c; a ^= rotl32(c, 4);  c += b;
        b -= a; b ^= rotl32(a, 6);  a += c;
        c -= b; c ^= rotl32(b, 8);  b += a;
        a -= c; a ^= rotl32(c, 16); c += b;
        b -= a; b ^= rotl32(a, 19); a += c;
        c -= b; c ^= rotl32(b, 4);  b += a;
        len -= 12;
        i += 12;
    }
    if (len == 0) return c;
    a += word(i, len < 4 ? len : 4);
    if (len > 4) b += word(i + 4, len - 4 < 4 ? len - 4 : 4);
    if (len > 8) c += word(i + 8, len - 8);
    c ^= b; c -= rotl32(b, 14);
    a ^= c; a -= rotl32(c, 11);
    b ^= a; b -= rotl32(a, 25);
    c ^= b; c -= rotl32(b, 16);
    a ^= c; a -= rotl32(c, 4);
    b ^= a; b -= rotl32(a, 14);
    c ^= b; c -= rotl32(b, 24);
    return c;
}
static_assert(dict_hash("cd_seq_funcnpc_smithy") == 0xA7132561u, "lookup3 seed 0xC5EDE: smithy");
static_assert(dict_hash("cd_seq_funcnpc_butcher") == 0x978345CAu, "lookup3 seed 0xC5EDE: butcher");
// T1's decision: the hash of the base name U-f read is on the list.
inline constexpr bool is_trade_name_by_hash(std::string_view base) noexcept {
    return !base.empty() && is_trade_hash(dict_hash(base));
}

}  // namespace cdmp::session2::u2trade
