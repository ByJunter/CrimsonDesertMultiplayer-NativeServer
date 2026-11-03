#pragma once
// P8 session 25, weather (docs/research/noche/clima-sincronizar.md §5, "Implementado"): the pure part of
// weather_sync.h. Build 1.0.0.2976 (PE 0x6AB28F00). No game, no threads, no Windows: unit-tested in
// tests/optd_weather_sync_tests.cpp.
//
// The guest cannot simulate the host's weather (per-thread LCG, accumulators: §3). The game has a "Weather Viewer"
// mode instead: byte X+0x31 of the scene weather system X = [[[exe+0x6C8CEC8]+0x60]+0xF68] makes its four
// simulators and its mixer 0x3DC0340 stand still, and the update 0x3DC4730(X, dt) derives rain or snow from the
// precipitation command A+0x12C (0x382A1A0, with the local temperature sample A+0x198). The host reads its final
// values (the holder [X+0x60]: blocks A = [holder+0x18], B = [holder+0x20], C = [holder+0x28], and X+0x124/+0x128)
// and the guest writes them into its own X in viewer mode, on the thread that updates X, right before 0x3DC4730.
//
// Verified in Ghidra (2026-10-12, build 2976):
//   0x3DC4730  48 89 5C 24 10 | 55 | 56 | 57 | 48 83 EC 50 | C5 F8 29 74 24 40 (18 bytes, no RIP-relative one);
//              its only caller 0x3B10737 = E8 F4 3F 2B 00 (after 0x3B10730 mov rcx,[rcx+0xF68]); dt in XMM1.
//              if (X+0x31) 0x382A1A0([X+0x60]-0x28); 0x3DC0340(X); A+0x198 <- 0x3DC0FE0's sample;
//              X+0x124 += A+0x138 * A+0x190 * dt * B+0xB4; X+0x128 += A+0x138 * A+0x194 * dt * B+0xB4.
//   0x3DC0340  the mixer: does nothing when X+0x31 != 0; otherwise blends the registered WeatherComponents
//              (X+0x80, n = X+0x88) into the holder through its vf+0x160 (only when n > 0).
//   0x382A1A0  temp A+0x198 <= 0: A+0x16C = A+0x160 = A+0x164 = 0, A+0x168 = A+0x12C (snow);
//              else A+0x16C = A+0x160 = A+0x12C, A+0x164 = min(10 * A+0x12C, 1), A+0x168 = 0 (rain).
//   0xCDCA40   the weather simulator returns at once when [[[exe+0x6C8CEC8]+0x60]+0xF68]+0x31 != 0 (via vf60 =
//              0x6DA0A0: 48 8B 41 60 C3, mov rax,[rcx+0x60]; ret).
//   0x3DC3A10  the rain getter: viewer -> A+0x16C raw; otherwise attenuated by the camera height against B+0xB8.
//   0x3DBB060  X's constructor: vtable 0x145D51268, X+0x30 (u16) = 0, X+0x124 = X+0x128 = 0.
//   0x41DB60   GameGlobalEffectInfo_Weather -> blocks: floats with the FLT_MAX "not set" sentinel; B+0xEC, B+0xF0,
//              C+0x1B4 and C+0x1D4 are RGBA colours (u8 x 4, 0xFF sentinel); B+0xE8 (_cloudSeed) is a float.
//   0x9ADF20   the 2560 reads X as [[[exe+0x6D69198]+0x38]+0x38] (the second path the probe compares).

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace cdmp::session2::wsy {

// ---------------------------------------------------------------- the game (exe+, build 2976)

inline constexpr uintptr_t kModuleSlot = 0x6C8CEC8;  // [exe+] -> M; M.vf60 = 0x6DA0A0 returns [M+0x60]
inline constexpr uint32_t kModuleScene = 0x60;       // S = [M+0x60]
inline constexpr uint32_t kSceneWeather = 0xF68;     // X = [S+0xF68]
inline constexpr uint32_t kVtGetterSlot = 0x60;      // [[M]+0x60] = the getter the simulators call
inline constexpr uintptr_t kGetterVf60 = 0x6DA0A0;
inline constexpr uint8_t kGetterVf60Bytes[5] = {0x48, 0x8B, 0x41, 0x60, 0xC3};  // mov rax,[rcx+0x60]; ret
inline constexpr uintptr_t kClientSlot = 0x6D69198;  // 0x9ADF61: X_b = [[[exe+0x6D69198]+0x38]+0x38]
inline constexpr uint32_t kClientHop1 = 0x38, kClientHop2 = 0x38;
inline constexpr uintptr_t kVtWeatherSystem = 0x5D51268;  // [X] (VA 0x145D51268)

inline constexpr uint32_t kXViewer = 0x31;   // u8: 1 = the Weather Viewer's external values
inline constexpr uint32_t kXHolder = 0x60;
inline constexpr uint32_t kXWcCount = 0x88;  // u32: WeatherComponents registered with the mixer
inline constexpr uint32_t kXScroll = 0x124;  // 2 floats: the cloud scroll accumulators
inline constexpr uint32_t kHolderA = 0x18, kHolderB = 0x20, kHolderC = 0x28;
inline constexpr uint32_t kATempSample = 0x198;  // the ClimateTexture sample at the local camera (local, never sent)

inline constexpr uintptr_t kUpdate = 0x3DC4730;
inline constexpr uint8_t kUpdateHead[18] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x48,
                                            0x83, 0xEC, 0x50, 0xC5, 0xF8, 0x29, 0x74, 0x24, 0x40};
inline constexpr uint32_t kUpdateBoundaries[] = {5, 6, 7, 8, 12, 18};  // whole instructions of kUpdateHead
inline constexpr uintptr_t kUpdateCall = 0x3B10737;
inline constexpr uint8_t kUpdateCallBytes[5] = {0xE8, 0xF4, 0x3F, 0x2B, 0x00};  // call 0x3DC4730

// The server tick as the game's client threads compute it (combat_follow.cpp, remote_motion.cpp): frame ms +
// frame us / 1000 + the 2188 bias (ServerTimeInfoAck). On the guest it is the host's server tick.
inline constexpr uintptr_t kClockBase = 0x6C8CD68, kClockFrac = 0x6C8CD70, kClockBias = 0x6D69438;

// ---------------------------------------------------------------- the vector (v1)

enum class Block : uint8_t { A, B, C, X };
// Lerp: a float, interpolated. Hold: a float copied from the newest sample, never interpolated (an angle, the seed),
// checked like any float. Raw: u8 x 4 colours (0x41DB60), copied bit for bit, never checked. Accum: a float that
// grows with time (interpolated, extrapolated up to kMaxExtrapolateMs). Derived: a float 0x382A1A0 rewrites every
// frame in viewer mode (sent and logged, never written; put back on release). Unknown: logged only (type unknown,
// could be per-PC data: never written nor checked).
enum class Kind : uint8_t { Lerp, Hold, Raw, Accum, Derived, Unknown };

struct Field {
    Block block;
    uint16_t off;
    Kind kind;
    const char* name;
};

// Names: GameGlobalEffectInfo_Weather in 0x1504FC0's read order mapped by 0x41DB60 (clima-sincronizar.md §1.1;
// the name <-> index pairing is [E]); "?" where the report has no name.
inline constexpr Field kFields[] = {
    // A (17)
    {Block::A, 0x12C, Kind::Lerp, "precipitation"},
    {Block::A, 0x130, Kind::Lerp, "cloudiness"},
    {Block::A, 0x134, Kind::Lerp, "humidity"},
    {Block::A, 0x138, Kind::Lerp, "windSpeed"},
    {Block::A, 0x13C, Kind::Lerp, "puddleRate"},
    {Block::A, 0x144, Kind::Lerp, "snowPuddleRate"},
    {Block::A, 0x148, Kind::Lerp, "snowAmount"},
    {Block::A, 0x14C, Kind::Lerp, "snowRate"},
    {Block::A, 0x150, Kind::Lerp, "iceRatio"},
    {Block::A, 0x154, Kind::Hold, "windDegree"},
    {Block::A, 0x158, Kind::Lerp, "altitudeWindRatio"},
    {Block::A, 0x160, Kind::Derived, "rainOut160"},
    {Block::A, 0x164, Kind::Derived, "rainFx164"},
    {Block::A, 0x168, Kind::Derived, "snow"},
    {Block::A, 0x16C, Kind::Derived, "rain"},
    {Block::A, 0x190, Kind::Lerp, "windDirX"},
    {Block::A, 0x194, Kind::Lerp, "windDirZ"},
    // B (36): +0x64..+0xE8 and the two colours +0xEC/+0xF0
    {Block::B, 0x64, Kind::Lerp, "sunDirX"},
    {Block::B, 0x68, Kind::Lerp, "sunDirY"},
    {Block::B, 0x6C, Kind::Lerp, "moonSizeAngle"},
    {Block::B, 0x70, Kind::Lerp, "moonDirX"},
    {Block::B, 0x74, Kind::Lerp, "moonDirY"},
    {Block::B, 0x78, Kind::Unknown, "B78?"},  // not from 0x41DB60: type unknown (review 6), logged only
    {Block::B, 0x7C, Kind::Unknown, "B7C?"},
    {Block::B, 0x80, Kind::Unknown, "B80?"},
    {Block::B, 0x84, Kind::Lerp, "mieB84"},
    {Block::B, 0x88, Kind::Lerp, "mieB88"},
    {Block::B, 0x8C, Kind::Lerp, "mieB8C"},
    {Block::B, 0x90, Kind::Lerp, "ozoneB90"},
    {Block::B, 0x94, Kind::Lerp, "lightB94"},
    {Block::B, 0x98, Kind::Lerp, "heightFogB98"},
    {Block::B, 0x9C, Kind::Lerp, "heightFogB9C"},
    {Block::B, 0xA0, Kind::Lerp, "heightFogBA0"},
    {Block::B, 0xA4, Kind::Lerp, "heightFogBA4"},
    {Block::B, 0xA8, Kind::Lerp, "cloudBaseDensity"},
    {Block::B, 0xAC, Kind::Lerp, "cloudBAC"},
    {Block::B, 0xB0, Kind::Lerp, "cloudBB0"},
    {Block::B, 0xB4, Kind::Lerp, "cloudScrollMultiplier"},
    {Block::B, 0xB8, Kind::Lerp, "cloudAltitude"},
    {Block::B, 0xBC, Kind::Lerp, "cloudBBC"},
    {Block::B, 0xC0, Kind::Lerp, "cloudBC0"},
    {Block::B, 0xC4, Kind::Lerp, "cloudBC4"},
    {Block::B, 0xC8, Kind::Lerp, "cloudBC8"},
    {Block::B, 0xCC, Kind::Lerp, "cloudBCC"},
    {Block::B, 0xD0, Kind::Lerp, "cloudBD0"},
    {Block::B, 0xD4, Kind::Lerp, "cloudLayerBD4"},
    {Block::B, 0xD8, Kind::Lerp, "cloudLayerBD8"},
    {Block::B, 0xDC, Kind::Lerp, "cloudLayerBDC"},
    {Block::B, 0xE0, Kind::Lerp, "cloudLayerBE0"},
    {Block::B, 0xE4, Kind::Lerp, "cloudBE4"},
    {Block::B, 0xE8, Kind::Hold, "cloudSeed"},  // a float (0x41DB60 compares it with FLT_MAX)
    {Block::B, 0xEC, Kind::Raw, "colourBEC"},
    {Block::B, 0xF0, Kind::Raw, "colourBF0"},
    // C (6)
    {Block::C, 0x178, Kind::Unknown, "C178?"},
    {Block::C, 0x1B0, Kind::Lerp, "underWaterC1B0"},
    {Block::C, 0x1B4, Kind::Raw, "colourC1B4"},
    {Block::C, 0x1B8, Kind::Lerp, "underWaterC1B8"},
    {Block::C, 0x1D4, Kind::Raw, "colourC1D4"},
    {Block::C, 0x1E0, Kind::Lerp, "extinctionC1E0"},
    // X (2)
    {Block::X, 0x124, Kind::Accum, "cloudScrollX"},
    {Block::X, 0x128, Kind::Accum, "cloudScrollZ"},
};
inline constexpr size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);
static_assert(kFieldCount == 61);

constexpr size_t index_of(Block b, uint16_t off) {
    for (size_t i = 0; i < kFieldCount; ++i) {
        if (kFields[i].block == b && kFields[i].off == off) return i;
    }
    return kFieldCount;
}
inline constexpr size_t kIdxPrecip = index_of(Block::A, 0x12C);
inline constexpr size_t kIdxCloud = index_of(Block::A, 0x130);
inline constexpr size_t kIdxHumid = index_of(Block::A, 0x134);
inline constexpr size_t kIdxWind = index_of(Block::A, 0x138);
inline constexpr size_t kIdxSnow = index_of(Block::A, 0x168);
inline constexpr size_t kIdxRain = index_of(Block::A, 0x16C);
inline constexpr size_t kIdxCloudBase = index_of(Block::B, 0xA8);
inline constexpr size_t kIdxScrollX = index_of(Block::X, 0x124);
inline constexpr size_t kIdxScrollZ = index_of(Block::X, 0x128);
static_assert(kIdxPrecip == 0 && kIdxRain == 14 && kIdxSnow == 13 && kIdxScrollZ == kFieldCount - 1);

// What the reader copies in one guarded read per block: [off, off + size).
struct Range {
    uint16_t off, size;
};
inline constexpr Range kReadA{0x12C, 0x198 - 0x12C};  // +0x12C .. +0x197 (the vector's A fields)
inline constexpr Range kReadB{0x64, 0xF4 - 0x64};     // +0x64 .. +0xF3
inline constexpr Range kReadC{0x178, 0x1E4 - 0x178};  // +0x178 .. +0x1E3
inline constexpr Range kReadX{0x124, 8};
constexpr Range read_range(Block b) {
    return b == Block::A ? kReadA : b == Block::B ? kReadB : b == Block::C ? kReadC : kReadX;
}
constexpr bool fields_inside_ranges() {
    for (const Field& f : kFields) {
        const Range r = read_range(f.block);
        if (f.off < r.off || f.off + 4u > static_cast<uint32_t>(r.off) + r.size || (f.off & 3) != 0) return false;
    }
    return true;
}
static_assert(fields_inside_ranges());

// The vector's words from the four block copies (each starting at its Range's off). A missing block (nullptr)
// leaves its words at 0.
inline void extract(const uint8_t* a, const uint8_t* b, const uint8_t* c, const uint8_t* x, uint32_t (&w)[kFieldCount]) {
    for (size_t i = 0; i < kFieldCount; ++i) {
        const Field& f = kFields[i];
        const uint8_t* src = f.block == Block::A ? a : f.block == Block::B ? b : f.block == Block::C ? c : x;
        w[i] = 0;
        if (src) std::memcpy(&w[i], src + (f.off - read_range(f.block).off), 4);
    }
}

inline float as_float(uint32_t w) {
    float f = 0;
    std::memcpy(&f, &w, 4);
    return f;
}
inline uint32_t as_word(float f) {
    uint32_t w = 0;
    std::memcpy(&w, &f, 4);
    return w;
}

// ---------------------------------------------------------------- the snapshot and its wire form (state 12)

inline constexpr uint16_t kHasA = 0x01, kHasB = 0x02, kHasC = 0x04, kHasX = 0x08, kHasPos = 0x10;
inline constexpr uint16_t kHostViewer = 0x20;  // the host's own X+0x31 was not 0 (never expected; logged)
inline constexpr uint16_t kKnownMask = 0x3F;

inline constexpr uint16_t block_bit(Block b) {
    return b == Block::A ? kHasA : b == Block::B ? kHasB : b == Block::C ? kHasC : kHasX;
}

struct Snapshot {
    uint32_t seq = 0;   // the host's sample number (1, 2, ...)
    uint64_t tick = 0;  // the host's server tick (ms) when it was read; 0 = unreadable
    uint16_t mask = 0;
    float pos[3] = {};  // the host's character (world, Y up), with kHasPos
    uint32_t w[kFieldCount] = {};
    float f(size_t i) const { return as_float(w[i]); }
};

// Tunnel control packet (troc_frame.h): opcode 1, slot -1, payload
//   u8 12 | u8 version 1 | u16 mask | u32 seq | u64 tick | f32 pos[3] | u32 words[61]   = 272 bytes.
// Only in ToGuestClient batches, only to a guest that announced kCapsWeatherSync (an older guest ignores an unknown
// state). The mod's protocol (protocol.cpp) is untouched: kVersion stays.
inline constexpr uint16_t kControlOpcode = 1;     // troc_frame.h kControlOpcode
inline constexpr uint8_t kControlWeather = 12;    // troc_frame.h: 1 ready, 2 dead, 3, 4, 5; 6-11 the guest's save
inline constexpr uint8_t kCapsWeatherSync = 0x04;  // troc_frame.h kCapsOpcode flags: 0x01 compressed, 0x02 own save
inline constexpr uint8_t kWireVersion = 1;
inline constexpr size_t kHeader = 5;
inline constexpr size_t kPayloadSize = 28 + 4 * kFieldCount;
inline constexpr size_t kFrameSize = kHeader + kPayloadSize;
static_assert(kPayloadSize == 272 && kFrameSize == 277);

inline void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
inline void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
inline uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline std::array<uint8_t, kFrameSize> frame(const Snapshot& s) {
    std::array<uint8_t, kFrameSize> f{};
    put16(&f[0], kControlOpcode);
    f[2] = 0xFF;
    put16(&f[3], static_cast<uint16_t>(kPayloadSize));
    uint8_t* p = f.data() + kHeader;
    p[0] = kControlWeather;
    p[1] = kWireVersion;
    put16(p + 2, s.mask);
    put32(p + 4, s.seq);
    put32(p + 8, static_cast<uint32_t>(s.tick));
    put32(p + 12, static_cast<uint32_t>(s.tick >> 32));
    for (int i = 0; i < 3; ++i) put32(p + 16 + 4 * i, as_word(s.pos[i]));
    for (size_t i = 0; i < kFieldCount; ++i) put32(p + 28 + 4 * i, s.w[i]);
    return f;
}

// A whole state-12 frame of this exact form (opcode 1, slot -1, 272-byte payload, state 12): what the guest takes
// out of the host's stream. Any other opcode-1 frame goes on as before.
inline bool is_frame(std::span<const uint8_t> f) {
    return f.size() == kFrameSize && get16(f.data()) == kControlOpcode && f[2] == 0xFF &&
           get16(f.data() + 3) == kPayloadSize && f[kHeader] == kControlWeather;
}

// is_frame, this version, no unknown mask bit.
inline std::optional<Snapshot> decode(std::span<const uint8_t> f) {
    if (!is_frame(f)) return std::nullopt;
    const uint8_t* p = f.data() + kHeader;
    if (p[1] != kWireVersion) return std::nullopt;
    Snapshot s;
    s.mask = get16(p + 2);
    if (s.mask & ~kKnownMask) return std::nullopt;
    s.seq = get32(p + 4);
    s.tick = get32(p + 8) | (static_cast<uint64_t>(get32(p + 12)) << 32);
    for (int i = 0; i < 3; ++i) s.pos[i] = as_float(get32(p + 16 + 4 * i));
    for (size_t i = 0; i < kFieldCount; ++i) s.w[i] = get32(p + 28 + 4 * i);
    return s;
}

// ---------------------------------------------------------------- sanity

inline constexpr float kHuge = 1.0e7f;
inline bool checked(Kind k) { return k == Kind::Lerp || k == Kind::Hold || k == Kind::Accum || k == Kind::Derived; }
inline bool unit_field(size_t i) {
    return i == kIdxPrecip || i == kIdxCloud || i == kIdxHumid || i == kIdxRain || i == kIdxSnow;
}
// +-FLT_MAX: the data's "not set" (0x41DB60's sentinel). Should a block hold it, it is a valid value: passed through
// as it is (never interpolated), counted apart (review item 8).
inline constexpr uint32_t kFltMaxBits = 0x7F7FFFFFu, kNegFltMaxBits = 0xFF7FFFFFu;
inline bool is_unset(uint32_t w) { return w == kFltMaxBits || w == kNegFltMaxBits; }

struct Sanity {
    uint32_t nonfinite = 0, huge = 0, outside_unit = 0, unset = 0;
    int first_bad = -1;  // the first field index that failed (kFieldCount: the position)
    bool ok() const { return nonfinite == 0 && huge == 0; }
};

// The float fields of the present blocks (Lerp, Hold, Accum, Derived): finite and |v| < 1e7, +-FLT_MAX allowed
// ("not set"); the five [0, 1] fields (precipitation, cloudiness, humidity, rain, snow) outside [-0.001, 1.001] are
// only counted (the range is [E]). Colours and words of unknown type are not looked at. The position with kHasPos.
inline Sanity check(const Snapshot& s) {
    Sanity r;
    const auto look = [&](float v, int idx) {
        if (!std::isfinite(v)) {
            ++r.nonfinite;
        } else if (std::fabs(v) >= kHuge) {
            ++r.huge;
        } else {
            return;
        }
        if (r.first_bad < 0) r.first_bad = idx;
    };
    for (size_t i = 0; i < kFieldCount; ++i) {
        if (!(s.mask & block_bit(kFields[i].block)) || !checked(kFields[i].kind)) continue;
        if (is_unset(s.w[i])) {
            ++r.unset;
            continue;
        }
        const float v = s.f(i);
        look(v, static_cast<int>(i));
        if (unit_field(i) && std::isfinite(v) && (v < -0.001f || v > 1.001f)) ++r.outside_unit;
    }
    if (s.mask & kHasPos) {
        for (float v : s.pos) look(v, static_cast<int>(kFieldCount));
    }
    return r;
}
inline const char* field_name(int idx) {
    return idx >= 0 && static_cast<size_t>(idx) < kFieldCount ? kFields[idx].name : idx >= 0 ? "position" : "-";
}

// What the guest needs to apply anything: A, B and X (C is written when present).
inline bool applicable(const Snapshot& s) {
    return (s.mask & (kHasA | kHasB | kHasX)) == (kHasA | kHasB | kHasX) && check(s).ok();
}

// ---------------------------------------------------------------- interpolation (guest, the hook's thread)

inline constexpr uint32_t kDelayMs = 300;           // render this far behind the host's newest sample
inline constexpr uint32_t kMaxGapMs = 2000;         // two samples further apart are not blended
inline constexpr uint32_t kMaxExtrapolateMs = 1000;  // the accumulators beyond the newest sample
inline constexpr uint32_t kFreshMs = 1000;          // a sample older than this (local receive time) is stale

struct Timed {
    Snapshot s;
    uint64_t recv_ms = 0;  // the guest's GetTickCount64 when it arrived (0 = none)
};

// A sample's age: 0 when it arrived after `now_ms` was read (the network thread stamps it between the reader's
// clock read and its mailbox read: S25 printed ages of 2^64 - 32 ms).
inline uint64_t age_ms(uint64_t now_ms, uint64_t recv_ms) { return now_ms > recv_ms ? now_ms - recv_ms : 0; }
inline bool fresh(uint64_t now_ms, uint64_t recv_ms) { return recv_ms != 0 && age_ms(now_ms, recv_ms) < kFreshMs; }

struct Blend {
    bool blended = false;      // prev and cur were mixed (false: cur as it is)
    bool extrapolated = false;  // the render time was past cur: the accumulators went on with their slope
    float t = 1.0f;            // 0 = prev, 1 = cur
};

// The vector to write at the guest's local time now_ms. The time base is the host's server tick when both samples
// carry one and it grows; otherwise the guest's receive times. The render time is the host's estimated "now"
// (cur's time + what elapsed here since cur arrived) minus kDelayMs.
inline Blend blend(const Timed* prev, const Timed& cur, uint64_t now_ms, uint32_t (&out)[kFieldCount]) {
    Blend b;
    for (size_t i = 0; i < kFieldCount; ++i) out[i] = cur.s.w[i];
    if (!prev || !prev->recv_ms || prev->s.seq == cur.s.seq) return b;
    int64_t tp = 0, tc = 0;
    if (prev->s.tick && cur.s.tick && cur.s.tick > prev->s.tick) {
        tp = static_cast<int64_t>(prev->s.tick);
        tc = static_cast<int64_t>(cur.s.tick);
    } else if (cur.recv_ms > prev->recv_ms) {
        tp = static_cast<int64_t>(prev->recv_ms);
        tc = static_cast<int64_t>(cur.recv_ms);
    } else {
        return b;
    }
    if (tc - tp > static_cast<int64_t>(kMaxGapMs)) return b;
    const int64_t elapsed = now_ms > cur.recv_ms ? static_cast<int64_t>(now_ms - cur.recv_ms) : 0;
    const int64_t render = tc + elapsed - static_cast<int64_t>(kDelayMs);
    const double span = static_cast<double>(tc - tp);
    double t = static_cast<double>(render - tp) / span;
    double tx = t;  // the accumulators' own t (may go past 1)
    if (t < 0.0) t = tx = 0.0;
    if (t > 1.0) {
        t = 1.0;
        tx = std::min(1.0 + static_cast<double>(kMaxExtrapolateMs) / span, static_cast<double>(render - tp) / span);
        b.extrapolated = true;
    }
    b.blended = true;
    b.t = static_cast<float>(t);
    for (size_t i = 0; i < kFieldCount; ++i) {
        const Kind k = kFields[i].kind;
        if (k != Kind::Lerp && k != Kind::Accum && k != Kind::Derived) continue;
        if (is_unset(prev->s.w[i]) || is_unset(cur.s.w[i])) continue;  // "not set" on either side: cur's word stays
        const double a = prev->s.f(i), c = cur.s.f(i);
        if (!std::isfinite(a) || !std::isfinite(c)) continue;  // cur's word stays
        const double u = k == Kind::Accum ? tx : t;
        out[i] = as_word(static_cast<float>(a + (c - a) * u));
    }
    return b;
}

// The precipitation command the guest writes to A+0x12C: the host's CURRENT rain or snow (max(A+0x16C, A+0x168)),
// not its target (§5.2); 0x382A1A0 then makes it rain or snow with the guest's own temperature. In [0, 1].
inline float precip_command(const uint32_t (&w)[kFieldCount]) {
    const auto usable = [](uint32_t word) {
        const float f = as_float(word);
        return is_unset(word) || !std::isfinite(f) ? 0.0f : f;
    };
    return std::clamp(std::max(usable(w[kIdxRain]), usable(w[kIdxSnow])), 0.0f, 1.0f);
}

// The words the guest writes (Lerp, Hold, Raw and Accum of the present blocks; A+0x12C is the command above).
inline bool written(size_t i, uint16_t mask) {
    const Kind k = kFields[i].kind;
    return (mask & block_bit(kFields[i].block)) &&
           (k == Kind::Lerp || k == Kind::Hold || k == Kind::Raw || k == Kind::Accum);
}

// What the guest keeps (on its first write to an X) and puts back when it lets that X go (review item 1: with no
// WeatherComponent registered, X+0x88 == 0, the mixer never rewrites the holder): every holder word it may write,
// and the four A outputs 0x382A1A0 rewrites in viewer mode. Not the accumulators (X+0x124/+0x128: putting minutes-
// old values back would jump the clouds; the game integrates on from the host's).
inline bool kept_for_release(size_t i) {
    const Kind k = kFields[i].kind;
    return kFields[i].block != Block::X && (k == Kind::Lerp || k == Kind::Hold || k == Kind::Raw || k == Kind::Derived);
}
inline uint64_t field_bit(size_t i) { return uint64_t{1} << i; }
static_assert(kFieldCount <= 64);
// The fields a release writes back: kept at the first write AND written since (the derived outputs once the flag was
// ours: 0x382A1A0 changed them).
inline uint64_t restore_bits(uint64_t kept, uint64_t written_bits, bool flag_was_ours) {
    uint64_t derived = 0;
    for (size_t i = 0; i < kFieldCount; ++i) {
        if (kFields[i].kind == Kind::Derived) derived |= field_bit(i);
    }
    return kept & (written_bits | (flag_was_ours ? derived : 0));
}
inline uint32_t value_to_write(size_t i, const uint32_t (&w)[kFieldCount]) {
    return i == kIdxPrecip ? as_word(precip_command(w)) : w[i];
}

// ---------------------------------------------------------------- the guest's gates (its worker thread)

inline constexpr uint64_t kStableMs = 3000;  // the local player in the world (not parked) this long: ready
inline constexpr uint64_t kLeaveMs = 2000;   // and not readable this long: not ready any more
inline constexpr uint32_t kDefaultRadiusM = 2000;

// The local player in the world, with hysteresis (a single failed read never ends a selftest or a mirror): ready
// after kStableMs of good reads (PlayerReader: the controlled actor, not parked at the loading spot), not ready
// after kLeaveMs of failed ones.
class WorldWatch {
public:
    void feed(uint64_t now_ms, bool ok) {
        if (ok) {
            fail_since_ = 0;
            if (!ok_since_) ok_since_ = now_ms ? now_ms : 1;
            if (now_ms - ok_since_ >= kStableMs) ready_ = true;
        } else {
            ok_since_ = 0;
            if (!fail_since_) fail_since_ = now_ms ? now_ms : 1;
            if (now_ms - fail_since_ >= kLeaveMs) ready_ = false;
        }
    }
    bool ready() const { return ready_; }

private:
    uint64_t ok_since_ = 0, fail_since_ = 0;
    bool ready_ = false;
};

enum class Gate : uint8_t { Apply, NotRemote, NotInWorld, NoPosition, TooFar, Cutscene, Count };
inline const char* gate_name(Gate g) {
    switch (g) {
        case Gate::Apply: return "apply";
        case Gate::NotRemote: return "this game does not play on the host now";
        case Gate::NotInWorld: return "the local player is not in the world (loading, title, or not 3 s yet)";
        case Gate::NoPosition: return "no position (the host's in the newest sample, or ours)";
        case Gate::TooFar: return "farther from the host than weather_sync_radius_m";
        case Gate::Cutscene: return "a local cutscene";
        case Gate::Count: break;
    }
    return "?";
}

struct GateIn {
    bool remote_live = false;  // guest_remote_live(): remote, tunnel up, not stopped
    bool in_world = false;     // WorldWatch::ready()
    bool own_pos = false;      // the last good PlayerReader position
    float own[3] = {};
    bool host_pos = false;  // the newest sample has kHasPos
    float host[3] = {};
    uint32_t radius_m = kDefaultRadiusM;
    bool cutscene = false;  // not detectable in this build (always false; clima-sincronizar.md "Implementado")
    bool was_open = false;  // the previous decision was Apply (the radius' hysteresis)
};

// The radius' hysteresis (review item 5): a closed gate opens inside R - d, an open one closes outside R + d, with
// d = 5 % of R, between 10 and 100 m (and never more than R / 2).
inline double radius_margin_m(uint32_t radius_m) {
    const double r = radius_m;
    return std::min(std::clamp(r * 0.05, 10.0, 100.0), r / 2.0);
}

inline Gate decide_gate(const GateIn& in) {
    if (!in.remote_live) return Gate::NotRemote;
    if (!in.in_world) return Gate::NotInWorld;
    if (in.cutscene) return Gate::Cutscene;
    if (!in.host_pos || !in.own_pos) return Gate::NoPosition;
    double d2 = 0;
    for (int i = 0; i < 3; ++i) {
        const double d = static_cast<double>(in.own[i]) - in.host[i];
        d2 += d * d;
    }
    const double m = radius_margin_m(in.radius_m);
    const double r = in.was_open ? in.radius_m + m : in.radius_m - m;
    if (!std::isfinite(d2) || d2 > r * r) return Gate::TooFar;
    return Gate::Apply;
}

// At most one gate line per min_gap_ms (review item 5); the changes not logged are counted into the next line.
class LineGate {
public:
    explicit LineGate(uint64_t min_gap_ms = 5000) : gap_(min_gap_ms) {}
    // True: log now (suppressed() says how many changes went unlogged before it, then restarts).
    bool take(uint64_t now_ms) {
        if (logged_ && now_ms - last_ < gap_) {
            ++suppressed_;
            return false;
        }
        logged_ = true;
        last_ = now_ms;
        return true;
    }
    uint64_t suppressed_and_reset() {
        const uint64_t n = suppressed_;
        suppressed_ = 0;
        return n;
    }
    uint64_t pending() const { return suppressed_; }

private:
    uint64_t gap_, last_ = 0, suppressed_ = 0;
    bool logged_ = false;
};

// Mode 1's fault latch (review item 2): a failed guarded access stops the writes; they come back after
// kCleanFrames hook calls in a row whose reads all succeed, or on the next closed -> open edge of the gate, at most
// kMaxFaults times per game run (then off for good, logged).
class FaultLatch {
public:
    static constexpr uint32_t kCleanFrames = 300;  // ~5 s at 60 fps
    static constexpr uint32_t kMaxFaults = 10;

    void on_fault() {
        blocked_ = true;
        clean_ = 0;
        ++faults_;
    }
    // A hook call while blocked whose reads (vtable, flag, holder, A, B) all succeeded / one failed. True: cleared.
    bool on_frame(bool reads_ok) {
        if (!blocked_ || exhausted()) return false;
        clean_ = reads_ok ? clean_ + 1 : 0;
        if (clean_ < kCleanFrames) return false;
        blocked_ = false;
        clean_ = 0;
        return true;
    }
    // The gate's open-edge counter (the worker bumps it on each closed -> open). True: cleared by a new edge.
    bool on_gate_epoch(uint64_t epoch) {
        const bool edge = epoch != seen_epoch_;
        seen_epoch_ = epoch;
        if (!edge || !blocked_ || exhausted()) return false;
        blocked_ = false;
        clean_ = 0;
        return true;
    }
    bool blocked() const { return blocked_; }
    bool exhausted() const { return faults_ >= kMaxFaults; }
    uint32_t faults() const { return faults_; }

private:
    bool blocked_ = false;
    uint32_t clean_ = 0, faults_ = 0;
    uint64_t seen_epoch_ = 0;
};

// ---------------------------------------------------------------- what the hook set (both modes)

// The X objects the hook put in viewer mode (and what it found there). An X is written only from the hook, while
// the game updates THAT X (it is alive then): a record of an X no longer updated is never touched again.
inline constexpr uint16_t kRestoreA[5] = {0x12C, 0x160, 0x164, 0x168, 0x16C};  // selftest: what 0x382A1A0 / we change
struct Owned {
    uintptr_t x = 0;
    uint8_t orig_flag = 0;
    bool has_orig_a = false;
    uintptr_t a = 0;  // the A block orig_a came from (another A block later: only the flag goes back)
    uint32_t orig_a[5] = {};
    uint64_t frames = 0;  // frames written since this record began
    // Mode 1 (review item 1): the holder words kept at the first write (kept_for_release), the blocks they came from
    // (a block that changed since is not written back), the fields written since and whether the flag became ours.
    uintptr_t blk[3] = {};  // A, B, C
    uint64_t kept = 0, written = 0;
    bool flag_ours = false;
    uint32_t orig_w[kFieldCount] = {};
};

template <size_t N>
class OwnedSet {
public:
    Owned* find(uintptr_t x) {
        for (Owned& o : slots_) {
            if (x && o.x == x) return &o;
        }
        return nullptr;
    }
    // A new record for x (the existing one if any); nullptr when full.
    Owned* add(uintptr_t x) {
        if (!x) return nullptr;
        if (Owned* o = find(x)) return o;
        for (Owned& o : slots_) {
            if (!o.x) {
                o = Owned{};
                o.x = x;
                return &o;
            }
        }
        return nullptr;
    }
    void remove(uintptr_t x) {
        if (Owned* o = find(x)) *o = Owned{};
    }
    // Review item 3: every record of another X (a re-created X has a new address; the game updates one X) is handed
    // to `fn` (which may put its flag back) and dropped, so stale records never fill the set. Returns how many.
    template <class F>
    size_t evict_others(uintptr_t x, F&& fn) {
        size_t n = 0;
        for (Owned& o : slots_) {
            if (o.x && o.x != x) {
                fn(o);
                o = Owned{};
                ++n;
            }
        }
        return n;
    }
    size_t size() const {
        size_t n = 0;
        for (const Owned& o : slots_) n += o.x ? 1 : 0;
        return n;
    }

private:
    std::array<Owned, N> slots_{};
};

// ---------------------------------------------------------------- selftest (P1, one PC, the host's hook)

// WaitWorld -> Settle (settle_ms with the world ready: the natural weather first) -> Apply (duration_ms: X+0x31 = 1
// and A+0x12C = 1.0 every frame) -> Done (restore once). The world leaving, another X or a fault while it applies:
// Aborted (restore once). One run per game start.
enum class StPhase : uint8_t { WaitWorld, Settle, Apply, Done, Aborted };
inline const char* st_phase_name(StPhase p) {
    switch (p) {
        case StPhase::WaitWorld: return "waiting for the world";
        case StPhase::Settle: return "world ready, natural weather first";
        case StPhase::Apply: return "APPLYING (viewer mode, precipitation 1.0)";
        case StPhase::Done: return "done (restored)";
        case StPhase::Aborted: return "ABORTED (restored)";
    }
    return "?";
}

struct StStep {
    bool write = false;    // this frame: X+0x31 = 1 and A+0x12C = 1.0 on the X the run started on
    bool restore = false;  // this frame: put back what the run changed (once)
    bool began = false;    // the first write of the run (capture what is there first)
};

class Selftest {
public:
    struct Config {
        uint64_t settle_ms = 15000;
        uint64_t duration_ms = 30000;
    };
    Selftest() : Selftest(Config{}) {}
    explicit Selftest(Config c) : cfg_(c) {}

    // Once per hook call. world_ready: the local player has been in the world for kStableMs; same_x: the hook's X is
    // the run's X (or the run has none yet) and its vtable is right.
    StStep step(uint64_t now_ms, bool world_ready, bool same_x) {
        StStep s;
        switch (phase_) {
            case StPhase::WaitWorld:
                if (world_ready && same_x) enter(StPhase::Settle, now_ms);
                break;
            case StPhase::Settle:
                if (!world_ready || !same_x) {
                    enter(StPhase::WaitWorld, now_ms);
                } else if (now_ms - since_ >= cfg_.settle_ms) {
                    enter(StPhase::Apply, now_ms);
                    s.write = s.began = true;
                }
                break;
            case StPhase::Apply:
                if (!world_ready || !same_x) {
                    enter(StPhase::Aborted, now_ms);
                    s.restore = true;
                } else if (now_ms - since_ >= cfg_.duration_ms) {
                    enter(StPhase::Done, now_ms);
                    s.restore = true;
                } else {
                    s.write = true;
                }
                break;
            case StPhase::Done:
            case StPhase::Aborted: break;
        }
        return s;
    }
    // A read or write failed while applying: Aborted, restore asked for (true) once.
    bool fault(uint64_t now_ms) {
        if (phase_ != StPhase::Apply) return false;
        enter(StPhase::Aborted, now_ms);
        return true;
    }
    StPhase phase() const { return phase_; }
    uint64_t since() const { return since_; }
    const Config& config() const { return cfg_; }

private:
    void enter(StPhase p, uint64_t now_ms) {
        phase_ = p;
        since_ = now_ms;
    }
    Config cfg_;
    StPhase phase_ = StPhase::WaitWorld;
    uint64_t since_ = 0;
};

// ---------------------------------------------------------------- modes

enum class Mode : uint8_t { Off, Probe, Selftest, On };
struct ParsedMode {
    Mode mode = Mode::Off;
    bool known = true;  // false: an unknown value, treated as Off (warned)
};

inline ParsedMode parse_mode(std::string_view raw) {
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.remove_prefix(1);
    if (const size_t c = raw.find_first_of(";#"); c != std::string_view::npos) raw = raw.substr(0, c);
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '\r' || raw.back() == '\n')) {
        raw.remove_suffix(1);
    }
    std::string v(raw);
    for (char& ch : v) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    if (v.empty() || v == "0" || v == "off" || v == "no" || v == "false") return {Mode::Off, true};
    if (v == "probe") return {Mode::Probe, true};
    if (v == "selftest") return {Mode::Selftest, true};
    if (v == "1" || v == "on" || v == "yes" || v == "true") return {Mode::On, true};
    return {Mode::Off, false};
}

inline const char* mode_name(Mode m) {
    switch (m) {
        case Mode::Off: return "0";
        case Mode::Probe: return "probe";
        case Mode::Selftest: return "selftest";
        case Mode::On: return "1";
    }
    return "?";
}

// ---------------------------------------------------------------- the guest's lock-free double buffer

// One writer (the mod's network thread), any number of readers (the hook's thread, the worker). The writer fills
// the slot not published and then publishes it; a reader copies the published slot and keeps the copy only when no
// writer started rewriting that slot meanwhile (begin_ <= seq + 1). Words are relaxed atomics (no data race).
template <size_t N>
class DoubleBuffer {
public:
    void publish(const uint32_t (&w)[N]) {
        const uint64_t next = seq_.load(std::memory_order_relaxed) + 1;
        begin_.store(next, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        auto& slot = slots_[next & 1];
        for (size_t i = 0; i < N; ++i) slot[i].store(w[i], std::memory_order_relaxed);
        seq_.store(next, std::memory_order_release);
    }
    uint64_t seq() const { return seq_.load(std::memory_order_acquire); }
    // False when nothing was published yet or a writer overtook this read (try again next time).
    bool read(uint32_t (&out)[N], uint64_t& seq) const {
        const uint64_t s = seq_.load(std::memory_order_acquire);
        if (s == 0) return false;
        const auto& slot = slots_[s & 1];
        for (size_t i = 0; i < N; ++i) out[i] = slot[i].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (begin_.load(std::memory_order_relaxed) > s + 1) return false;
        seq = s;
        return true;
    }

private:
    std::atomic<uint64_t> seq_{0}, begin_{0};
    std::array<std::array<std::atomic<uint32_t>, N>, 2> slots_{};
};

// A Timed in the buffer's words: seq, tick (2), mask, pos (3), recv (2), the vector.
inline constexpr size_t kBoxWords = 9 + kFieldCount;
inline void pack(const Timed& t, uint32_t (&w)[kBoxWords]) {
    w[0] = t.s.seq;
    w[1] = static_cast<uint32_t>(t.s.tick);
    w[2] = static_cast<uint32_t>(t.s.tick >> 32);
    w[3] = t.s.mask;
    for (int i = 0; i < 3; ++i) w[4 + i] = as_word(t.s.pos[i]);
    w[7] = static_cast<uint32_t>(t.recv_ms);
    w[8] = static_cast<uint32_t>(t.recv_ms >> 32);
    for (size_t i = 0; i < kFieldCount; ++i) w[9 + i] = t.s.w[i];
}
inline void unpack(const uint32_t (&w)[kBoxWords], Timed& t) {
    t.s.seq = w[0];
    t.s.tick = w[1] | (static_cast<uint64_t>(w[2]) << 32);
    t.s.mask = static_cast<uint16_t>(w[3]);
    for (int i = 0; i < 3; ++i) t.s.pos[i] = as_float(w[4 + i]);
    t.recv_ms = w[7] | (static_cast<uint64_t>(w[8]) << 32);
    for (size_t i = 0; i < kFieldCount; ++i) t.s.w[i] = w[9 + i];
}

// The hook's own history (its thread only): the two newest samples it has seen.
struct History {
    Timed prev, cur;
    bool has_prev = false, has_cur = false;
    uint64_t seen = 0;  // the buffer sequence of cur
    void push(const Timed& t, uint64_t box_seq) {
        if (has_cur && t.s.seq != cur.s.seq) {
            prev = cur;
            has_prev = true;
        }
        cur = t;
        has_cur = true;
        seen = box_seq;
    }
};

}  // namespace cdmp::session2::wsy
