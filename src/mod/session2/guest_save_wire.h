#pragma once
// P8 session 25, the guest's own save, PHASE B (docs/research/noche/progreso-invitado.md §3.2, §3.5-§3.9 and
// "Implementado (fase B)"): the tunnel protocol between the guest's mod, the host's mod and cdmp_peer, and the pure
// state machines around it. No game, no files, no sockets: tests/optd_guest_save_b_tests.cpp runs all of it.
//
// The save files travel as they are (the game encrypts, decrypts and validates them with its own code; the mods only
// check the header as the game does, gsv::check_header, and a SHA-256).
//
// Control packets of the Troc tunnel (troc_frame.h kControlOpcode = 1, slot -1), payload "u8 state | ...", LE:
//   6  SAVE_OFFER       G->A  u8 6 | u8 ver 1 | u32 xfer | u64 guest SteamID64 | u32 N | u32 base_gen | u8 nfiles 2 |
//                             2 x {u8 kind (0 lobby, 1 save) | u32 size | 32 B SHA-256}                    (97 B)
//   7  SAVE_CHUNK       both  u8 7 | u32 xfer | u8 kind | u32 offset | u16 len | len B (1..28672)          (12 B + len)
//   8  SAVE_ACK         A->G  the answer to an offer: u8 8 | u32 xfer | u8 result | u32 gen (the host's mailbox
//                             generation it staged INSTEAD of the offered files, 0 = the offered ones)    (10 B)
//                       G->A  the same layout, result 8 (cancelled): the guest gave up waiting (fallback)
//   9  SAVE_RESULT      A->G  u8 9 | u8 ver 2 | u32 xfer | u32 gen | u32 N | u8 reason | u64 unix time (UTC) |
//                             u8 nfiles 2 | 2 x {u8 kind | u32 size | 32 B SHA-256} | 32 B line root (the
//                             SHA-256 of the guest's save.save its line started from; 0 = not known) | 32 B base
//                             (the save.save the guest offered for the session that made it); then its SAVE_CHUNKs
//                             (162 B; ver 1 had neither: refused since session 25, answered SAVE_RESULT_ACK 9)
//   10 SAVE_STATUS      A->G  u8 10 | u8 ver 1 | u8 mode | u32 Ng | u32 last game error | u64 host SteamID64 |
//                             u32 last exported gen                                                       (23 B)
//   11 SAVE_RESULT_ACK  G->A  u8 11 | u32 xfer | u8 result | u32 gen                                        (10 B)
// A peer that does not know these states ignores them (the guest: unknown control states are dropped) or drops them
// (the host: no server Req). The guest says it speaks them with the capability bit kCapsGuestSave (0x02) of the
// tunnel's caps packet (troc_frame.h kCapsOpcode); the host sends states 9 and 10 only to a guest that said so on the
// current connection. protocol.cpp is unchanged (kVersion stays).
//
// Build 1.0.0.2976 (opcodes: SaveAutoReq 3037, SaveToFileCurrentPlayerReq 2910, SaveToFileCurrentPlayerAck 3016,
// CreateCharacterReq 2711, NewGameStartReq 2178, LoginCharacterToFieldServerReq 3093, LogoutSessionToServerReq 2001).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/sha256.h"
#include "mod/session2/guest_save_logic.h"

namespace cdmp::session2::gsw {

// ---------------------------------------------------------------- the wire

inline constexpr uint16_t kControlOpcode = 1;  // troc_frame.h kControlOpcode
inline constexpr uint8_t kStateOffer = 6, kStateChunk = 7, kStateAck = 8, kStateResult = 9, kStateStatus = 10,
                         kStateResultAck = 11;
inline constexpr uint8_t kWireVersion = 1;
inline constexpr uint8_t kResultVersion = 2;  // SAVE_RESULT only: ver 2 adds the line root (session 25)
inline constexpr uint8_t kCapsGuestSave = 0x02;  // troc_frame.h kCapsGuestSave
// One chunk's data: the Troc packet holds 0x8000 - 0x20 bytes (troc_frame.h kMaxPacketSize); 12 + 5 bytes of headers.
inline constexpr size_t kMaxChunk = 28672;
inline constexpr size_t kTrocHeader = 5;
inline constexpr size_t kOfferSize = 97, kChunkHeader = 12, kAckSize = 10, kResultSize = 162, kStatusSize = 23;
static_assert(kTrocHeader + kChunkHeader + kMaxChunk <= 0x8000 - 0x20);

inline constexpr uint64_t kLoginHoldMs = 30000;  // a 3093 waits at most this for the host's SAVE_ACK
inline constexpr uint64_t kFinalHoldMs = 15000;  // a goodbye waits at most this for the final save's SAVE_RESULT
inline constexpr uint32_t kOfferRetries = 1;     // a refused upload is offered once more
inline constexpr uint16_t kOpSaveAutoReq = 3037;
inline constexpr uint16_t kOpSaveToFileCurrentPlayerReq = 2910;
inline constexpr uint16_t kOpSaveToFileCurrentPlayerAck = 3016;
inline constexpr uint16_t kOpCreateCharacterReq = 2711;
inline constexpr uint16_t kOpNewGameStartReq = 2178;
inline constexpr uint16_t kOpLoginCharacterReq = 3093;  // u8 | u64 characterNo @1 | u32 slot @9
inline constexpr uint16_t kOpLogoutSessionReq = 2001;   // D1 07 FF (internal form, 3 bytes)

// The two files, always in this order in an offer and a result.
enum class Kind : uint8_t { Lobby = 0, Save = 1 };
inline constexpr size_t kFiles = 2;
inline const char* kind_name(Kind k) { return k == Kind::Lobby ? "lobby" : "save"; }
inline const char* kind_file(Kind k) { return k == Kind::Lobby ? "lobby.save" : "save.save"; }
inline const wchar_t* kind_wfile(Kind k) { return k == Kind::Lobby ? L"lobby.save" : L"save.save"; }

struct FileMeta {
    uint32_t size = 0;
    sha256::Digest sha{};
    bool operator==(const FileMeta&) const = default;
};
using Files = std::array<FileMeta, kFiles>;  // [0] lobby, [1] save

enum class AckResult : uint8_t {
    Ok = 0,
    Hash = 1,       // a file's SHA-256 is not the announced one
    Header = 2,     // a file's header is not one the game would load
    Size = 3,       // a size is 0, above 16 MB, or not what the chunks gave
    Busy = 4,       // the host is in the middle of something else (a transfer of the other way, a load)
    Off = 5,        // guest_own_save is off on the host, or off for this run (failed / poisoned)
    Slot = 6,       // N is not a slot a "Continue" loads (0-2, 100-108): never 3 (CrossSave) or 206 (one use)
    Identity = 7,   // the offer's SteamID64 is not the guest the host's Steam session talks to
    Cancelled = 8,  // G->A: the guest gave up waiting (it falls back: no own save in this session)
    Protocol = 9,   // a chunk out of order, past the end, of another transfer, or a malformed message
    Io = 10,        // the receiver could not write the files
};
inline const char* ack_result_name(uint8_t r) {
    switch (static_cast<AckResult>(r)) {
        case AckResult::Ok: return "ok";
        case AckResult::Hash: return "hash mismatch";
        case AckResult::Header: return "bad header";
        case AckResult::Size: return "bad size";
        case AckResult::Busy: return "busy";
        case AckResult::Off: return "own save off";
        case AckResult::Slot: return "slot not allowed";
        case AckResult::Identity: return "not the connected guest";
        case AckResult::Cancelled: return "cancelled by the guest";
        case AckResult::Protocol: return "protocol error";
        case AckResult::Io: return "I/O error";
    }
    return "unknown";
}

struct Offer {
    uint32_t xfer = 0;
    uint64_t guest_id = 0;  // its SteamID64 (0 = unknown: cdmp_peer, UDP)
    uint32_t slot = 0;      // N: the guest's own slot
    uint32_t base_gen = 0;  // the last generation of this host it applied (0 = its own native save)
    Files files{};
};
struct Chunk {
    uint32_t xfer = 0;
    Kind kind = Kind::Lobby;
    uint32_t offset = 0;
    std::span<const uint8_t> data;
};
struct Ack {  // states 8 and 11
    uint32_t xfer = 0;
    uint8_t result = 0;
    uint32_t gen = 0;
};
enum class Reason : uint8_t { Unknown = 0, Autosave = 1, Manual = 2, Final = 3, LinkLost = 4 };
inline const char* reason_name(uint8_t r) {
    switch (static_cast<Reason>(r)) {
        case Reason::Unknown: return "unknown";
        case Reason::Autosave: return "autosave";
        case Reason::Manual: return "manual (2910)";
        case Reason::Final: return "final (the guest's 2910 before its goodbye)";
        case Reason::LinkLost: return "link lost (the host's 2910 before the native 2001)";
    }
    return "?";
}
struct Result {
    uint32_t xfer = 0;
    uint32_t gen = 0;
    uint32_t slot = 0;  // N: the guest's slot it belongs to
    uint8_t reason = 0;
    uint64_t time_unix = 0;
    Files files{};
    sha256::Digest root{};  // its line (find_continuation): all zero = not known (never applied by the script)
    // The save.save the guest offered for the session that made it (a member of the line even after the guest pruned
    // the generation it came from: the apply script's review item 1). All zero = not known.
    sha256::Digest base{};
};
// SAVE_STATUS modes: what this session does with the guest's save.
enum class HostMode : uint8_t { Off = 0, Ready = 1, Active = 2, Poisoned = 3, Probe = 4 };
inline const char* host_mode_name(uint8_t m) {
    switch (static_cast<HostMode>(m)) {
        case HostMode::Off: return "off (user 2 plays a clone: this session does NOT keep your progress)";
        case HostMode::Ready: return "ready (your save is staged or loading)";
        case HostMode::Active: return "active (user 2 plays YOUR save; its saves come back to you)";
        case HostMode::Poisoned: return "POISONED (your save did not load as it should: no saves in this session)";
        case HostMode::Probe: return "probe (your save is checked but not loaded: this session does NOT keep your progress)";
    }
    return "unknown";
}
struct Status {
    uint8_t mode = 0;
    uint32_t guest_slot = 0;  // Ng on the host (204)
    uint32_t last_error = 0;  // the last game error code seen by the host's own-save hooks (0 = none)
    uint64_t host_id = 0;     // the host's SteamID64 (0 = unknown: UDP)
    uint32_t last_gen = 0;    // the last generation the host exported for this guest
};

// Little-endian helpers.
inline void put8(std::vector<uint8_t>& v, uint8_t x) { v.push_back(x); }
inline void put16(std::vector<uint8_t>& v, uint16_t x) {
    for (int i = 0; i < 2; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
inline void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
inline void put64(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
inline uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline uint64_t get64(const uint8_t* p) { return static_cast<uint64_t>(get32(p)) | (static_cast<uint64_t>(get32(p + 4)) << 32); }

inline void put_files(std::vector<uint8_t>& v, const Files& f) {
    put8(v, static_cast<uint8_t>(kFiles));
    for (size_t i = 0; i < kFiles; ++i) {
        put8(v, static_cast<uint8_t>(i));
        put32(v, f[i].size);
        v.insert(v.end(), f[i].sha.begin(), f[i].sha.end());
    }
}
// nfiles 2, kinds 0 then 1, each size in 1..16 MB.
inline bool get_files(const uint8_t* p, Files& f) {
    if (p[0] != kFiles) return false;
    p += 1;
    for (size_t i = 0; i < kFiles; ++i, p += 37) {
        if (p[0] != i) return false;
        f[i].size = get32(p + 1);
        if (f[i].size == 0 || f[i].size > gsv::kMaxFileSize) return false;
        std::memcpy(f[i].sha.data(), p + 5, 32);
    }
    return true;
}

inline std::vector<uint8_t> encode_offer(const Offer& o) {
    std::vector<uint8_t> v;
    v.reserve(kOfferSize);
    put8(v, kStateOffer);
    put8(v, kWireVersion);
    put32(v, o.xfer);
    put64(v, o.guest_id);
    put32(v, o.slot);
    put32(v, o.base_gen);
    put_files(v, o.files);
    return v;
}
inline std::vector<uint8_t> encode_chunk(uint32_t xfer, Kind kind, uint32_t offset, std::span<const uint8_t> data) {
    std::vector<uint8_t> v;
    v.reserve(kChunkHeader + data.size());
    put8(v, kStateChunk);
    put32(v, xfer);
    put8(v, static_cast<uint8_t>(kind));
    put32(v, offset);
    put16(v, static_cast<uint16_t>(data.size()));
    v.insert(v.end(), data.begin(), data.end());
    return v;
}
inline std::vector<uint8_t> encode_ack(uint8_t state, const Ack& a) {
    std::vector<uint8_t> v;
    v.reserve(kAckSize);
    put8(v, state);
    put32(v, a.xfer);
    put8(v, a.result);
    put32(v, a.gen);
    return v;
}
inline std::vector<uint8_t> encode_result(const Result& r) {
    std::vector<uint8_t> v;
    v.reserve(kResultSize);
    put8(v, kStateResult);
    put8(v, kResultVersion);
    put32(v, r.xfer);
    put32(v, r.gen);
    put32(v, r.slot);
    put8(v, r.reason);
    put64(v, r.time_unix);
    put_files(v, r.files);
    v.insert(v.end(), r.root.begin(), r.root.end());
    v.insert(v.end(), r.base.begin(), r.base.end());
    return v;
}
inline std::vector<uint8_t> encode_status(const Status& s) {
    std::vector<uint8_t> v;
    v.reserve(kStatusSize);
    put8(v, kStateStatus);
    put8(v, kWireVersion);
    put8(v, s.mode);
    put32(v, s.guest_slot);
    put32(v, s.last_error);
    put64(v, s.host_id);
    put32(v, s.last_gen);
    return v;
}

// `payload` = the control packet's payload (state byte first). Exact sizes, version 1, the files' form.
inline std::optional<Offer> decode_offer(std::span<const uint8_t> p) {
    if (p.size() != kOfferSize || p[0] != kStateOffer || p[1] != kWireVersion) return std::nullopt;
    Offer o;
    o.xfer = get32(&p[2]);
    o.guest_id = get64(&p[6]);
    o.slot = get32(&p[14]);
    o.base_gen = get32(&p[18]);
    if (!get_files(&p[22], o.files)) return std::nullopt;
    return o;
}
inline std::optional<Chunk> decode_chunk(std::span<const uint8_t> p) {
    if (p.size() < kChunkHeader + 1 || p[0] != kStateChunk || p[5] > 1) return std::nullopt;
    Chunk c;
    c.xfer = get32(&p[1]);
    c.kind = static_cast<Kind>(p[5]);
    c.offset = get32(&p[6]);
    const uint16_t len = get16(&p[10]);
    if (len == 0 || len > kMaxChunk || len != p.size() - kChunkHeader) return std::nullopt;
    c.data = p.subspan(kChunkHeader);
    return c;
}
inline std::optional<Ack> decode_ack(std::span<const uint8_t> p, uint8_t state) {
    if (p.size() != kAckSize || p[0] != state || (state != kStateAck && state != kStateResultAck)) return std::nullopt;
    return Ack{get32(&p[1]), p[5], get32(&p[6])};
}
inline std::optional<Result> decode_result(std::span<const uint8_t> p) {
    if (p.size() != kResultSize || p[0] != kStateResult || p[1] != kResultVersion) return std::nullopt;
    Result r;
    r.xfer = get32(&p[2]);
    r.gen = get32(&p[6]);
    r.slot = get32(&p[10]);
    r.reason = p[14];
    r.time_unix = get64(&p[15]);
    if (!get_files(&p[23], r.files)) return std::nullopt;
    std::memcpy(r.root.data(), &p[98], r.root.size());
    std::memcpy(r.base.data(), &p[130], r.base.size());
    return r;
}
// A SAVE_RESULT of another version (an older build on the other PC): its xfer (at the same offset in every version),
// to answer it with SAVE_RESULT_ACK Protocol instead of leaving the host to send it again and again.
inline std::optional<uint32_t> other_version_result_xfer(std::span<const uint8_t> p) {
    if (p.size() < 6 || p[0] != kStateResult || p[1] == kResultVersion) return std::nullopt;
    return get32(&p[2]);
}
inline std::optional<Status> decode_status(std::span<const uint8_t> p) {
    if (p.size() != kStatusSize || p[0] != kStateStatus || p[1] != kWireVersion) return std::nullopt;
    Status s;
    s.mode = p[2];
    s.guest_slot = get32(&p[3]);
    s.last_error = get32(&p[7]);
    s.host_id = get64(&p[11]);
    s.last_gen = get32(&p[19]);
    return s;
}

// The whole Troc packet of a control payload: u16 opcode 1 | i8 -1 | u16 len | payload.
inline std::vector<uint8_t> frame(std::span<const uint8_t> payload) {
    std::vector<uint8_t> f;
    f.reserve(kTrocHeader + payload.size());
    put16(f, kControlOpcode);
    put8(f, 0xFF);
    put16(f, static_cast<uint16_t>(payload.size()));
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}
// The payload of a whole save control packet (opcode 1, slot -1, length = the rest, state 6..11); nullopt for any
// other packet, which then goes on as before (mount_sync, re-announce, the host's ready/dead/character states).
inline std::optional<std::span<const uint8_t>> save_payload(std::span<const uint8_t> packet) {
    if (packet.size() < kTrocHeader + 1 || get16(packet.data()) != kControlOpcode || packet[2] != 0xFF ||
        get16(&packet[3]) != packet.size() - kTrocHeader) {
        return std::nullopt;
    }
    const uint8_t state = packet[kTrocHeader];
    if (state < kStateOffer || state > kStateResultAck) return std::nullopt;
    return packet.subspan(kTrocHeader);
}

// ---------------------------------------------------------------- the final save's Req (live test of 12-10 04:12)

// SaveAutoReq 3037 does NOT save now [E + V]: its handler 0x2A21E60 calls SaveGameRequest(pj, &res, reason 7, &opts =
// the payload's u32) -> .xtls 0x103BC7A0 queues the internal 2077 with reason 7 -> 0x28FC6F0 -> SavePlayer (.xtls
// 0xCBD84D0, thunk 0x186E1A0), which (with its flag [exe+0x6CE4A28] set and opts & 1 == 0) maps the reason to a class
// = reason - 1 (the constant at .xtls 0xD7D6BB0 is -0x2DDEB700): classes 0, 1 and 3 save, class 2 only 900 s after
// the last save, any other class (reason 7 -> 6) returns 0 with no save and no 3016. Live 12-10 04:12:45: the 3037 of
// user 2 was handed and nothing was saved.
// SaveToFileCurrentPlayerReq 2910 saves NOW: handler 0x2A21B80 (-> .xtls 0x104B93B0) reads
//   u32 slot | u32 n (<= 30) | n UTF-16 chars | u16 0   (the client's writer .xtls 0xA4624C0 sends exactly that)
// then 0x28FD6E0 wants slot 100-108 (or 3, never sent here), the session's actor = its user's main character and a
// name the bad-word filter 0x12C35A0 lets through; it stores the name as the save's title and calls .xtls 0x103BE8E0:
// CanSave 0x186F000 (opts 0), the manager's busy byte, SavePlayerToSlot 0x186E450 (call 0x103BE99A; phase A remaps it
// to slot 204 for user 2) and SaveToFileCurrentPlayerAck 3016 (writer 0x2A1FD40):
//   u32 the character's key | u32 slot | u8 ok (result == 0) | u8 (opts & 3) == 0 | u32 result.
// The mod's final save is therefore a 2910 for slot kFinalSaveSlot named kFinalSaveName (slot byte 0, as the client's
// Req of the same flags).
inline constexpr uint32_t kFinalSaveSlot = 100;
inline constexpr std::u16string_view kFinalSaveName = u"Co-op";
inline constexpr uint32_t kMaxSaveName = 30;

struct SaveReq2910 {
    uint32_t slot = 0;
    std::u16string name;
};
// The whole Troc packet of a 2910.
inline std::vector<uint8_t> make_save_req(uint32_t slot, std::u16string_view name) {
    std::vector<uint8_t> p;
    const size_t n = std::min<size_t>(name.size(), kMaxSaveName);
    put32(p, slot);
    put32(p, static_cast<uint32_t>(n));
    for (size_t i = 0; i < n; ++i) put16(p, static_cast<uint16_t>(name[i]));
    put16(p, 0);
    std::vector<uint8_t> f;
    put16(f, kOpSaveToFileCurrentPlayerReq);
    put8(f, 0);
    put16(f, static_cast<uint16_t>(p.size()));
    f.insert(f.end(), p.begin(), p.end());
    return f;
}
inline std::vector<uint8_t> make_final_save_req() { return make_save_req(kFinalSaveSlot, kFinalSaveName); }
// A 2910's payload exactly as the handler takes it (size 10 + 2n, n <= 30, the terminator); nullopt otherwise.
inline std::optional<SaveReq2910> parse_save_req(std::span<const uint8_t> p) {
    if (p.size() < 10) return std::nullopt;
    SaveReq2910 r;
    r.slot = get32(p.data());
    const uint32_t n = get32(p.data() + 4);
    if (n > kMaxSaveName || p.size() != 10 + 2ull * n || get16(p.data() + 8 + 2ull * n) != 0) return std::nullopt;
    for (uint32_t i = 0; i < n; ++i) {
        const uint16_t c = get16(p.data() + 8 + 2ull * i);
        if (c == 0) return std::nullopt;  // the handler's length would differ from n
        r.name.push_back(static_cast<char16_t>(c));
    }
    return r;
}
inline bool is_final_save_req(std::span<const uint8_t> payload) {
    const auto r = parse_save_req(payload);
    return r && r->name == kFinalSaveName;
}

struct SaveAck3016 {
    uint32_t key = 0;
    uint32_t slot = 0;
    bool ok = false;
    uint8_t flag = 0;
    uint32_t result = 0;
};
inline std::optional<SaveAck3016> parse_save_ack(std::span<const uint8_t> p) {
    if (p.size() != 14) return std::nullopt;
    return SaveAck3016{get32(p.data()), get32(p.data() + 4), p[8] != 0, p[9], get32(p.data() + 10)};
}

// ---------------------------------------------------------------- slots of the guest

// The slots a "Continue" of the guest's title loads (the most recent save.save of 0-2 / 100-108,
// UserLogin 0x2C17520): the only ones offered. Never 3 (CrossSave: saving it uploads to Pearl Abyss) nor 206
// (deleted after loading), nor anything the host's LoadSlot does not take.
constexpr bool offer_slot_ok(uint32_t n) { return n < 3 || (n >= 100 && n <= 108); }

// A folder under save\<accountID>\: "slot1" (Plain), "slot1_new" (New), "slot1_backup" (Backup); the game's other
// names ("slot1_backup_<stamp>_delete") and anything else: nullopt.
enum class DirKind : uint8_t { Plain, New, Backup };
struct SlotDir {
    uint32_t slot = 0;
    DirKind kind = DirKind::Plain;
};
inline std::optional<SlotDir> parse_slot_dir(std::wstring_view name) {
    if (name.size() < 5 || name.substr(0, 4) != L"slot") return std::nullopt;
    size_t i = 4;
    uint64_t n = 0;
    while (i < name.size() && name[i] >= L'0' && name[i] <= L'9' && i < 4 + 4) n = n * 10 + (name[i++] - L'0');
    if (i == 4 || (name[4] == L'0' && i > 5)) return std::nullopt;
    const std::wstring_view rest = name.substr(i);
    if (rest.empty()) return SlotDir{static_cast<uint32_t>(n), DirKind::Plain};
    if (rest == L"_new") return SlotDir{static_cast<uint32_t>(n), DirKind::New};
    if (rest == L"_backup") return SlotDir{static_cast<uint32_t>(n), DirKind::Backup};
    return std::nullopt;
}

struct SlotSeen {
    uint32_t slot = 0;
    bool has_lobby = false, has_save = false;
    uint64_t save_time = 0;  // save.save's last write (FILETIME ticks)
    bool has_new = false, has_backup = false;
};
struct Pick {
    std::optional<uint32_t> slot;
    std::string why;  // why nothing is offered (or which one and why)
};
// The slot "Continue" would load: the most recent save.save among the offerable slots with both files (ties: the
// lower number). Refused when that slot has a _new or _backup folder: a save was interrupted and the game repairs it
// at the next load; play once alone first (§3.2.3).
inline Pick pick_continue_slot(std::span<const SlotSeen> seen) {
    const SlotSeen* best = nullptr;
    size_t candidates = 0;
    for (const SlotSeen& s : seen) {
        if (!offer_slot_ok(s.slot) || !s.has_lobby || !s.has_save) continue;
        ++candidates;
        if (!best || s.save_time > best->save_time || (s.save_time == best->save_time && s.slot < best->slot)) best = &s;
    }
    Pick p;
    if (!best) {
        p.why = "no slot 0-2 / 100-108 with lobby.save and save.save";
        return p;
    }
    if (best->has_new || best->has_backup) {
        p.why = std::format("slot{} (the most recent) has a slot{}{} folder: a save was interrupted; load it once alone "
                            "so the game repairs it",
                            best->slot, best->slot, best->has_new ? "_new" : "_backup");
        return p;
    }
    p.slot = best->slot;
    p.why = std::format("slot{}: the most recent save.save of {} candidate(s)", best->slot, candidates);
    return p;
}

// The guest's account folder under save\: the one named after its Steam account id (the low 32 bits of its
// SteamID64) when that is known and present, else the only all-digit folder; nullopt if neither.
inline std::optional<std::wstring> pick_account(std::span<const std::wstring> folders, uint64_t own_steam_id) {
    std::vector<std::wstring> digits;
    for (const std::wstring& f : folders) {
        if (!f.empty() && f.find_first_not_of(L"0123456789") == std::wstring::npos) digits.push_back(f);
    }
    if (own_steam_id) {
        const std::wstring mine = std::to_wstring(own_steam_id & 0xFFFFFFFFull);
        for (const std::wstring& f : digits) {
            if (f == mine) return f;
        }
    }
    if (digits.size() == 1) return digits[0];
    return std::nullopt;
}

// ---------------------------------------------------------------- the tunnel's Req policy (§3.7)

// In own-save mode (host guest_own_save=1, user 2's own slot live and not failed): SaveToFileCurrentPlayerReq 2910
// for a manual slot 100-108 with a well-formed payload, and SaveAutoReq 3037 (which never saves now, see above, but
// is harmless: SavePlayer remaps or refuses it too), reach the server (SavePlayerToSlot remaps them to the guest's
// slot, or refuses them: never the host's slot). A 2910 for slot 3 (CrossSave) or with a malformed payload, and
// CreateCharacterReq 2711 / NewGameStartReq 2178, never do (a new character on the host's server would be saved as
// "its" game). Default = the tunnel's rule as before ("Save*" blocked, the rest as the handler table says).
enum class ReqPolicy : uint8_t { Default, Allow, Block };
inline ReqPolicy req_policy(gsv::Mode mode, const gsv::SlotState& s, uint16_t opcode,
                            std::span<const uint8_t> payload = {}) {
    if (mode != gsv::Mode::On) return ReqPolicy::Default;
    if (opcode == kOpCreateCharacterReq || opcode == kOpNewGameStartReq) return ReqPolicy::Block;
    const bool live = s.live && !s.disabled && !s.poisoned;
    if (opcode == kOpSaveToFileCurrentPlayerReq) {
        const auto r = parse_save_req(payload);
        if (!r || r->slot < 100 || r->slot > 108) return ReqPolicy::Block;
        return live ? ReqPolicy::Allow : ReqPolicy::Default;
    }
    if (opcode == kOpSaveAutoReq) return live ? ReqPolicy::Allow : ReqPolicy::Default;
    return ReqPolicy::Default;
}

// The SAVE_STATUS mode of the host's own-save state.
inline HostMode host_mode(gsv::Mode mode, const gsv::SlotState& s) {
    if (mode == gsv::Mode::Off) return HostMode::Off;
    if (mode == gsv::Mode::Probe) return HostMode::Probe;
    if (s.poisoned) return HostMode::Poisoned;
    if (s.disabled) return HostMode::Off;
    if (gsv::slot_healthy(s)) return HostMode::Active;
    return HostMode::Ready;
}

// ---------------------------------------------------------------- one transfer: sender and receiver

// The chunks of one transfer (an offer's or a result's): lobby first, then save, each in order, kMaxChunk at most.
class Outgoing {
public:
    Outgoing() = default;
    Outgoing(uint32_t xfer, std::vector<uint8_t> lobby, std::vector<uint8_t> save, size_t max_chunk = kMaxChunk)
        : xfer_(xfer), max_(std::clamp<size_t>(max_chunk, 1, kMaxChunk)) {
        files_[0] = std::move(lobby);
        files_[1] = std::move(save);
    }
    // The next chunk's payload; false when every byte went.
    bool next(std::vector<uint8_t>& payload) {
        while (kind_ < kFiles && at_ >= files_[kind_].size()) {
            ++kind_;
            at_ = 0;
        }
        if (kind_ >= kFiles) return false;
        const size_t n = std::min(max_, files_[kind_].size() - at_);
        payload = encode_chunk(xfer_, static_cast<Kind>(kind_), static_cast<uint32_t>(at_),
                               std::span<const uint8_t>(files_[kind_]).subspan(at_, n));
        at_ += n;
        sent_ += n;
        ++chunks_;
        return true;
    }
    bool done() const { return sent_ >= total(); }
    uint64_t total() const { return files_[0].size() + files_[1].size(); }
    uint64_t sent() const { return sent_; }
    uint32_t chunks() const { return chunks_; }
    uint32_t xfer() const { return xfer_; }
    static uint32_t chunk_count(uint64_t lobby, uint64_t save, size_t max_chunk = kMaxChunk) {
        const uint64_t m = std::clamp<size_t>(max_chunk, 1, kMaxChunk);
        return static_cast<uint32_t>((lobby + m - 1) / m + (save + m - 1) / m);
    }

private:
    uint32_t xfer_ = 0;
    size_t max_ = kMaxChunk;
    std::array<std::vector<uint8_t>, kFiles> files_;
    size_t kind_ = 0, at_ = 0;
    uint64_t sent_ = 0;
    uint32_t chunks_ = 0;
};

inline Files files_of(std::span<const uint8_t> lobby, std::span<const uint8_t> save) {
    Files f;
    f[0] = {static_cast<uint32_t>(lobby.size()), sha256::of(lobby)};
    f[1] = {static_cast<uint32_t>(save.size()), sha256::of(save)};
    return f;
}

// Both files as the game would load them: header (gsv::check_header, which also wants the size to be exactly
// 0x80 + data and at most 16 MB).
inline AckResult check_pair(std::span<const uint8_t> lobby, std::span<const uint8_t> save, std::string& why) {
    for (size_t i = 0; i < kFiles; ++i) {
        const auto& f = i == 0 ? lobby : save;
        const gsv::HeaderCheck c = gsv::check_header(f, f.size());
        if (c != gsv::HeaderCheck::Ok) {
            why = std::format("{} ({} bytes): {}", kind_file(static_cast<Kind>(i)), f.size(), gsv::header_check_name(c));
            return AckResult::Header;
        }
    }
    return AckResult::Ok;
}

// The receiving side of one transfer: the announced sizes and hashes, then the chunks in order per file (the tunnel
// is ordered and exactly once: anything else is a protocol error), then the check.
class Assembler {
public:
    enum class Step : uint8_t { Ignored, Need, Complete, Error };

    AckResult begin(uint32_t xfer, const Files& files) {
        reset();
        for (const FileMeta& f : files) {
            if (f.size == 0 || f.size > gsv::kMaxFileSize) return AckResult::Size;
        }
        xfer_ = xfer;
        meta_ = files;
        active_ = true;
        for (size_t i = 0; i < kFiles; ++i) data_[i].reserve(files[i].size);
        return AckResult::Ok;
    }
    Step add(const Chunk& c, AckResult& error) {
        if (!active_ || c.xfer != xfer_) return Step::Ignored;
        const size_t k = static_cast<size_t>(c.kind);
        std::vector<uint8_t>& d = data_[k];
        if (complete() || c.offset != d.size() || static_cast<uint64_t>(c.offset) + c.data.size() > meta_[k].size) {
            error = AckResult::Protocol;
            active_ = false;
            return Step::Error;
        }
        d.insert(d.end(), c.data.begin(), c.data.end());
        received_ += c.data.size();
        return complete() ? Step::Complete : Step::Need;
    }
    bool complete() const { return active_ && data_[0].size() == meta_[0].size && data_[1].size() == meta_[1].size; }
    // Complete: each file's SHA-256 against the announced one, then the game's header check.
    AckResult verify(std::string& why) const {
        if (!complete()) {
            why = "incomplete";
            return AckResult::Size;
        }
        for (size_t i = 0; i < kFiles; ++i) {
            if (sha256::of(data_[i]) != meta_[i].sha) {
                why = std::format("{}: SHA-256 is not the announced one", kind_file(static_cast<Kind>(i)));
                return AckResult::Hash;
            }
        }
        return check_pair(data_[0], data_[1], why);
    }
    const std::vector<uint8_t>& file(Kind k) const { return data_[static_cast<size_t>(k)]; }
    // The file's bytes moved out (after verify; then reset()).
    std::vector<uint8_t> take(Kind k) { return std::move(data_[static_cast<size_t>(k)]); }
    const Files& meta() const { return meta_; }
    bool active() const { return active_; }
    uint32_t xfer() const { return xfer_; }
    uint64_t received() const { return received_; }
    uint64_t total() const { return static_cast<uint64_t>(meta_[0].size) + meta_[1].size; }
    void reset() {
        active_ = false;
        xfer_ = 0;
        received_ = 0;
        meta_ = {};
        for (auto& d : data_) {
            d.clear();
            d.shrink_to_fit();
        }
    }

private:
    bool active_ = false;
    uint32_t xfer_ = 0;
    Files meta_{};
    std::array<std::vector<uint8_t>, kFiles> data_;
    uint64_t received_ = 0;
};

// ---------------------------------------------------------------- the host's results (download, §3.5)

// The download's pace (§3.8): a new chunk only while the tunnel's own backlog is under this, so ~1.2 MB spreads over
// about a second without holding the game's packets back.
inline constexpr size_t kDownloadBacklog = 256 * 1024;
inline constexpr uint64_t kResultAckTimeoutMs = 60000;  // after the last chunk
inline constexpr uint32_t kMaxResultAttempts = 3;       // per generation and connection

struct Generation {
    uint32_t gen = 0;
    uint32_t slot = 0;  // N
    uint8_t reason = 0;
    uint64_t time_unix = 0;
    Files files{};
    sha256::Digest root{};  // its line (all zero: not known)
    sha256::Digest base{};  // the save the guest offered for its session (all zero: not known)
};

// Which generation goes to the guest, and when. Only the newest undelivered one is kept: a newer export replaces a
// pending one and aborts one in flight (the guest drops a transfer that a newer SAVE_RESULT replaces). Delivered =
// the guest's SAVE_RESULT_ACK said ok. A connection that ends puts the one in flight back (sent again from the start
// on the next connection, its generation unchanged).
class ResultSender {
public:
    enum class Export : uint8_t { Queued, Superseded, Old };
    enum class AckVerdict : uint8_t { Delivered, Retry, GaveUp, Stale };

    void set_delivered(uint32_t gen) { delivered_ = std::max(delivered_, gen); }
    uint32_t delivered() const { return delivered_; }

    Export on_export(const Generation& g) {
        const uint32_t newest = std::max({delivered_, pending_ ? pending_->gen : 0u, flight_ ? flight_->gen : 0u});
        if (g.gen <= newest) return Export::Old;
        const bool aborted = flight_.has_value();
        flight_.reset();
        sent_all_ = false;
        pending_ = g;
        attempts_ = 0;
        parked_ = false;
        return aborted ? Export::Superseded : Export::Queued;
    }
    // A generation from the mailbox that the guest never acknowledged (a reconnection, or another run of the host).
    bool resume(const Generation& g) {
        if (g.gen <= delivered_ || (pending_ && pending_->gen >= g.gen) || (flight_ && flight_->gen >= g.gen)) return false;
        return on_export(g) != Export::Old;
    }
    // Nothing in flight and something pending (not parked after kMaxResultAttempts): it starts with this xfer.
    std::optional<Generation> start(uint32_t xfer, uint64_t now_ms) {
        if (flight_ || !pending_ || parked_) return std::nullopt;
        flight_ = pending_;
        xfer_ = xfer;
        sent_all_ = false;
        started_ms_ = now_ms;
        ++attempts_;
        return flight_;
    }
    void all_sent(uint64_t now_ms) {
        sent_all_ = true;
        last_chunk_ms_ = now_ms;
    }
    // The guest's SAVE_RESULT_ACK.
    AckVerdict on_ack(const Ack& a) {
        if (!flight_ || a.xfer != xfer_) return AckVerdict::Stale;
        const Generation g = *flight_;
        flight_.reset();
        sent_all_ = false;
        if (a.result == static_cast<uint8_t>(AckResult::Ok) && a.gen == g.gen) {
            delivered_ = std::max(delivered_, g.gen);
            if (pending_ && pending_->gen <= delivered_) pending_.reset();
            attempts_ = 0;
            return AckVerdict::Delivered;
        }
        if (attempts_ >= kMaxResultAttempts) {
            parked_ = true;
            return AckVerdict::GaveUp;
        }
        return AckVerdict::Retry;  // still pending: start() sends it again
    }
    // No answer kResultAckTimeoutMs after the last chunk: as a failed attempt.
    std::optional<AckVerdict> check_timeout(uint64_t now_ms) {
        if (!flight_ || !sent_all_ || now_ms - last_chunk_ms_ < kResultAckTimeoutMs) return std::nullopt;
        return on_ack(Ack{xfer_, static_cast<uint8_t>(AckResult::Protocol), 0});
    }
    // The connection ended: the one in flight is pending again; a parked one is tried again on the next connection.
    void on_link_lost() {
        flight_.reset();
        sent_all_ = false;
        attempts_ = 0;
        parked_ = false;
    }
    bool in_flight() const { return flight_.has_value(); }
    bool sending() const { return flight_.has_value() && !sent_all_; }
    const std::optional<Generation>& current() const { return flight_; }
    const std::optional<Generation>& pending() const { return pending_; }
    uint32_t xfer() const { return xfer_; }
    bool parked() const { return parked_; }
    uint32_t attempts() const { return attempts_; }

private:
    std::optional<Generation> pending_, flight_;
    uint32_t delivered_ = 0, xfer_ = 0, attempts_ = 0;
    bool sent_all_ = false, parked_ = false;
    uint64_t started_ms_ = 0, last_chunk_ms_ = 0;
};

// The newest generation above `floor` (the mailbox's undelivered one); nullopt = none.
inline std::optional<uint32_t> newest_above(std::span<const uint32_t> gens, uint32_t floor) {
    std::optional<uint32_t> best;
    for (const uint32_t g : gens) {
        if (g > floor && (!best || g > *best)) best = g;
    }
    return best;
}

// ---------------------------------------------------------------- lines (session 25)

// A line: the generations that continue one save of the guest, named by its root (the SHA-256 of the save.save its
// first session uploaded). §3.5.6 as fixed in session 25: the host stages a mailbox generation INSTEAD of an upload
// only when the upload is that line's root or one of its generations, and the line has a newer one. Before, any
// generation of the same guest newer than its base_gen replaced any upload, so a guest that came with another save
// played the old one (S25: the advanced slot0 offered, the test save's gen2 loaded).
inline bool digest_zero(const sha256::Digest& d) {
    return std::all_of(d.begin(), d.end(), [](uint8_t b) { return b == 0; });
}
struct LineGen {
    uint32_t gen = 0;
    sha256::Digest save{};  // its save.save
    sha256::Digest root{};  // its line (all zero: an older build's generation, never matched)
};
struct Continuation {
    bool found = false;             // the offered save is a line's root or one of its generations
    sha256::Digest root{};          // that line; a new line (the offered save itself) when !found
    uint32_t at = 0;                // the newest generation of the line that IS the offered save (0: its root)
    std::optional<uint32_t> newer;  // the line's newest generation when newer than `at`: staged instead
};
// The line of `offered`: the one whose root it is, else the line of the newest generation that it is.
inline Continuation find_continuation(std::span<const LineGen> gens, const sha256::Digest& offered) {
    Continuation c;
    c.root = offered;
    for (const LineGen& g : gens) {
        if (!digest_zero(g.root) && g.root == offered) c.found = true;
    }
    if (!c.found) {
        uint32_t best = 0;
        for (const LineGen& g : gens) {
            if (!digest_zero(g.root) && g.save == offered && g.gen > best) {
                best = g.gen;
                c.root = g.root;
                c.found = true;
            }
        }
    }
    if (!c.found) return c;
    uint32_t newest = 0;
    for (const LineGen& g : gens) {
        if (digest_zero(g.root) || g.root != c.root) continue;
        newest = std::max(newest, g.gen);
        if (g.save == offered) c.at = std::max(c.at, g.gen);
    }
    if (newest > c.at) c.newer = newest;
    return c;
}

// A generation's SAVE_RESULT reason from the phase-A manifest's reason text ("autosave (ret ...)", "manual 2910 ...").
inline uint8_t reason_from_text(std::string_view text) {
    if (text.starts_with("manual")) return static_cast<uint8_t>(Reason::Manual);
    if (text.starts_with("autosave")) return static_cast<uint8_t>(Reason::Autosave);
    return static_cast<uint8_t>(Reason::Unknown);
}
// The reason of a live export: the host's own final 2910 (link lost) or the guest's final 2910 (named kFinalSaveName)
// within kRecentReqMs of it
// make it final; else what the save hook's caller says.
inline constexpr uint64_t kRecentReqMs = 20000;
inline uint8_t live_reason(std::string_view text, uint64_t now_ms, uint64_t host_final_ms, uint64_t guest_final_ms) {
    if (host_final_ms && now_ms - host_final_ms <= kRecentReqMs) return static_cast<uint8_t>(Reason::LinkLost);
    if (guest_final_ms && now_ms - guest_final_ms <= kRecentReqMs) return static_cast<uint8_t>(Reason::Final);
    return reason_from_text(text);
}

// ---------------------------------------------------------------- the host's final save on a link loss (§3.6)

// One SaveToFileCurrentPlayerReq 2910 (make_final_save_req) for user 2 before its native 2001 when the guest's link is lost and its own save is active,
// then the 2001 once an export of the guest's slot followed it, or kFinalHoldMs (the guest that says goodbye itself
// sends its own 2910 first: guest side). At most once per logout.
class FinalSave {
public:
    enum class Step : uint8_t { Go, Inject, Wait };  // Go: the 2001 may go now
    enum class Outcome : uint8_t { None, Exported, TimedOut, NotQueued };

    void reset() { *this = FinalSave{}; }
    void arm() {
        if (state_ == State::Idle) state_ = State::Armed;
    }
    bool armed() const { return state_ != State::Idle; }
    Step step(uint64_t now_ms, uint64_t exports_now) {
        switch (state_) {
            case State::Idle:
            case State::Done: return Step::Go;
            case State::Armed: return Step::Inject;
            case State::Waiting:
                if (exports_now > exports_before_) return finish(Outcome::Exported);
                if (now_ms - since_ >= kFinalHoldMs) return finish(Outcome::TimedOut);
                return Step::Wait;
        }
        return Step::Go;
    }
    void injected(bool queued, uint64_t now_ms, uint64_t exports_now) {
        if (state_ != State::Armed) return;
        if (!queued) {
            finish(Outcome::NotQueued);
            return;
        }
        state_ = State::Waiting;
        since_ = now_ms;
        exports_before_ = exports_now;
    }
    Outcome outcome() const { return outcome_; }
    uint64_t waited(uint64_t now_ms) const { return now_ms - since_; }

private:
    enum class State : uint8_t { Idle, Armed, Waiting, Done };
    Step finish(Outcome o) {
        state_ = State::Done;
        outcome_ = o;
        return Step::Go;
    }
    State state_ = State::Idle;
    Outcome outcome_ = Outcome::None;
    uint64_t since_ = 0, exports_before_ = 0;
};
inline const char* final_outcome_name(FinalSave::Outcome o) {
    switch (o) {
        case FinalSave::Outcome::None: return "none";
        case FinalSave::Outcome::Exported: return "user 2's save was exported";
        case FinalSave::Outcome::TimedOut: return "no export in 15 s";
        case FinalSave::Outcome::NotQueued: return "the 2910 could not be queued";
    }
    return "?";
}

// ---------------------------------------------------------------- key=value files (manifests, records)

// "key=value" lines; repeated keys keep every value in order. A "file=NAME a=b c=d" line is also split into
// "file.NAME.a" = "b" ... (the phase-A manifest's form).
using KeyValues = std::multimap<std::string, std::string>;
inline KeyValues parse_key_values(std::string_view text) {
    KeyValues kv;
    while (!text.empty()) {
        size_t eol = text.find('\n');
        std::string_view line = text.substr(0, eol);
        text = eol == std::string_view::npos ? std::string_view() : text.substr(eol + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0) continue;
        const std::string key(line.substr(0, eq));
        const std::string_view value = line.substr(eq + 1);
        kv.emplace(key, std::string(value));
        if (key == "file") {
            // NAME a=b c=d
            const size_t sp = value.find(' ');
            const std::string name(value.substr(0, sp));
            std::string_view rest = sp == std::string_view::npos ? std::string_view() : value.substr(sp + 1);
            while (!rest.empty()) {
                const size_t end = rest.find(' ');
                const std::string_view item = rest.substr(0, end);
                rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
                const size_t ieq = item.find('=');
                if (ieq == std::string_view::npos || ieq == 0) continue;
                kv.emplace("file." + name + "." + std::string(item.substr(0, ieq)), std::string(item.substr(ieq + 1)));
            }
        }
    }
    return kv;
}
inline std::optional<std::string> kv_get(const KeyValues& kv, const std::string& key) {
    const auto it = kv.find(key);
    if (it == kv.end()) return std::nullopt;
    return it->second;
}
inline std::optional<uint64_t> kv_u64(const KeyValues& kv, const std::string& key) {
    const auto v = kv_get(kv, key);
    if (!v || v->empty() || v->size() > 20) return std::nullopt;
    uint64_t n = 0;
    for (const char c : *v) {
        if (c < '0' || c > '9') return std::nullopt;
        const uint64_t next = n * 10 + static_cast<uint64_t>(c - '0');
        if (next / 10 != n) return std::nullopt;
        n = next;
    }
    return n;
}
// The two files' sizes and hashes of a manifest ("file=lobby.save size=N sha256=HEX ..."; the phase-A mailbox and
// the guest's incoming generations both write them so).
inline std::optional<Files> kv_files(const KeyValues& kv) {
    Files f;
    for (size_t i = 0; i < kFiles; ++i) {
        const std::string base = std::string("file.") + kind_file(static_cast<Kind>(i));
        const auto size = kv_u64(kv, base + ".size");
        const auto sha = kv_get(kv, base + ".sha256");
        if (!size || *size == 0 || *size > gsv::kMaxFileSize || !sha) return std::nullopt;
        const auto d = sha256::parse_hex(*sha);
        if (!d) return std::nullopt;
        f[i] = {static_cast<uint32_t>(*size), *d};
    }
    return f;
}

// The guest's incoming\gen<n>\manifest.txt (written last: a generation folder without it is incomplete).
struct IncomingManifest {
    uint32_t gen = 0;
    uint64_t host_id = 0;
    uint32_t slot = 0;  // N: the guest slot it belongs to (apply-guest-progress.ps1 applies it there)
    uint8_t reason = 0;
    uint64_t time_unix = 0;
    std::string received_utc;
    Files files{};
    // Its line (SAVE_RESULT's root; all zero = not known, never written): the apply script applies a generation only
    // to a slot that holds its root, one of its generations or the base of one of them (or no save at all).
    sha256::Digest root{};
    sha256::Digest base{};  // SAVE_RESULT's base (all zero = not known, never written)
};
// "lineage_root=HEX" / "lineage_base=HEX": nullopt when absent; an all-zero digest when present but not a SHA-256
// (refused by the callers).
inline std::optional<sha256::Digest> kv_digest(const KeyValues& kv, const std::string& key) {
    const auto v = kv_get(kv, key);
    if (!v) return std::nullopt;
    return sha256::parse_hex(*v).value_or(sha256::Digest{});
}
inline std::optional<sha256::Digest> kv_root(const KeyValues& kv) { return kv_digest(kv, "lineage_root"); }
inline std::optional<sha256::Digest> kv_base(const KeyValues& kv) { return kv_digest(kv, "lineage_base"); }
inline std::string incoming_manifest_text(const IncomingManifest& m) {
    std::string s = "cdmp guest progress (P8 session 25, phase B; build 1.0.0.2976)\n";
    s += std::format("generation={}\nhost={}\nslot={}\nreason={}\nreason_text={}\ntime_unix={}\nreceived_utc={}\n", m.gen,
                     m.host_id, m.slot, m.reason, reason_name(m.reason), m.time_unix, m.received_utc);
    if (!digest_zero(m.root)) s += std::format("lineage_root={}\n", sha256::hex(m.root));
    if (!digest_zero(m.base)) s += std::format("lineage_base={}\n", sha256::hex(m.base));
    for (size_t i = 0; i < kFiles; ++i) {
        s += std::format("file={} size={} sha256={}\n", kind_file(static_cast<Kind>(i)), m.files[i].size,
                         sha256::hex(m.files[i].sha));
    }
    return s;
}
inline std::optional<IncomingManifest> parse_incoming_manifest(std::string_view text) {
    const KeyValues kv = parse_key_values(text);
    IncomingManifest m;
    const auto gen = kv_u64(kv, "generation");
    const auto host = kv_u64(kv, "host");
    const auto slot = kv_u64(kv, "slot");
    const auto reason = kv_u64(kv, "reason");
    const auto time = kv_u64(kv, "time_unix");
    const auto files = kv_files(kv);
    if (!gen || *gen == 0 || *gen > 0xFFFFFFFFull || !host || !slot || !offer_slot_ok(static_cast<uint32_t>(*slot)) ||
        !reason || *reason > 255 || !time || !files) {
        return std::nullopt;
    }
    m.gen = static_cast<uint32_t>(*gen);
    m.host_id = *host;
    m.slot = static_cast<uint32_t>(*slot);
    m.reason = static_cast<uint8_t>(*reason);
    m.time_unix = *time;
    m.received_utc = kv_get(kv, "received_utc").value_or("");
    m.files = *files;
    if (const auto root = kv_root(kv)) {
        if (digest_zero(*root)) return std::nullopt;
        m.root = *root;
    }
    if (const auto base = kv_base(kv)) {
        if (digest_zero(*base)) return std::nullopt;
        m.base = *base;
    }
    return m;
}

// ---------------------------------------------------------------- the guest's holds (§3.2, §3.6)


// The guest's side: its upload and the two holds of the client's packets (guest_own_save=1 only; probe never holds).
//   login hold: the client's packets wait (in order) while the start offer is still being prepared (so the upload is
//               in the tunnel before them: before the 2490), and its 3093 (and everything after it) waits until the
//               host acknowledged the offer (or it gave up: kLoginHoldMs, the retries). A 3093 for another slot than
//               the offered one asks for that slot's offer first (prepared by the caller: offer_sent or
//               reoffer_failed, in which case the offered one stays: SlotKept).
//   final hold: the client's goodbye 2001 (and everything after it) waits behind one SaveToFileCurrentPlayerReq 2910
//               (make_final_save_req) until a generation newer than the ones stored at the goodbye has arrived,
//               the server's 3016 says that save failed (FinalFailed: nothing will come), or kFinalHoldMs.
// Callers keep the packets; this only decides. Not thread-safe (the caller's lock).
class GuestHold {
public:
    enum class Upload : uint8_t { None, Pending, Preparing, Sent, Acked, Failed, GaveUp };
    enum class Hold : uint8_t { Off, Login, Final };
    enum class Release : uint8_t {
        None, Queued, Acked, GaveUp, NoOffer, Timeout, FinalDone, FinalTimeout, SlotKept, FinalFailed
    };
    struct OnSend {
        bool hold = false;        // keep this packet (and the following ones) back
        bool save_first = false;  // push the final 2910 (make_final_save_req) now, before the held goodbye
        std::optional<uint32_t> reoffer;  // the caller prepares this slot's offer (offer_sent / reoffer_failed)
        // P8 re-entry (review of c4367f8, 3): a 3093 for another slot in a re-entry conversation offers nothing (the
        // local slot lacks the previous conversation's progress); the host's live slot stays. The caller says so.
        std::optional<uint32_t> slot_kept;
    };

    explicit GuestHold(bool enabled = false) : enabled_(enabled) {}
    void set_enabled(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }

    // The start offer is being prepared (the slot is picked and read): the client's packets wait for it.
    void prepare_started() {
        if (upload_ == Upload::None) upload_ = Upload::Pending;
    }
    // Nothing can be offered (no slot, an interrupted save, a bad header...): no own save, nothing waits.
    void prepare_failed() {
        if (upload_ == Upload::Pending) upload_ = Upload::None;
    }
    // An offer went to the host (the first one, a retry, or the one a 3093 asked for).
    void offer_sent(uint32_t xfer, uint32_t slot) {
        upload_ = Upload::Sent;
        xfer_ = xfer;
        slot_ = slot;
        asked_slot_ = slot;  // a later 3093 for another slot asks again; the offered one never twice
    }
    // The slot a 3093 asked for cannot be offered: the offered one (and its state) stays.
    void reoffer_failed() {
        if (upload_ != Upload::Preparing) return;
        upload_ = before_reoffer_;
        kept_ = true;
    }
    // The host's SAVE_ACK. True when it is for the current offer. `retry` = offer the same slot again (once).
    bool on_ack(const Ack& a, bool& retry) {
        retry = false;
        if (upload_ == Upload::None || upload_ == Upload::Pending || a.xfer != xfer_) return false;
        const bool ok = a.result == static_cast<uint8_t>(AckResult::Ok);
        if (upload_ == Upload::Preparing) {
            // The previous offer's answer while the 3093's one is prepared: it counts if that one cannot be made.
            before_reoffer_ = ok ? Upload::Acked : Upload::GaveUp;
            return true;
        }
        if (ok) {
            upload_ = Upload::Acked;
            return true;
        }
        if (retries_ < kOfferRetries && a.result != static_cast<uint8_t>(AckResult::Off) &&
            a.result != static_cast<uint8_t>(AckResult::Slot) && a.result != static_cast<uint8_t>(AckResult::Identity)) {
            ++retries_;
            upload_ = Upload::Failed;
            retry = true;
            return true;
        }
        upload_ = Upload::GaveUp;
        return true;
    }

    // A packet of the client (opcode, the 3093's slot). `final_ok`: the host plays this guest's own save now (status
    // active) and its 3093 went out. `stored`: the generations stored so far in this run.
    OnSend on_send(uint16_t opcode, std::optional<uint32_t> slot_3093, uint64_t now_ms, bool final_ok, uint32_t stored) {
        OnSend r;
        if (!enabled_) return r;
        const bool holding = hold_ != Hold::Off;
        if (opcode == kOpLoginCharacterReq && hold_ != Hold::Final) {
            if (upload_ == Upload::None || upload_ == Upload::GaveUp) {  // no own save: as before
                r.hold = holding;
                return r;
            }
            if (reentry_ && slot_3093 && *slot_3093 != slot_ && asked_slot_ != slot_3093) {
                asked_slot_ = *slot_3093;
                kept_ = true;
                r.slot_kept = *slot_3093;
            } else if (upload_ != Upload::Pending && slot_3093 && *slot_3093 != slot_ && offer_slot_ok(*slot_3093) &&
                asked_slot_ != slot_3093) {
                asked_slot_ = *slot_3093;
                before_reoffer_ = upload_;
                upload_ = Upload::Preparing;
                r.reoffer = *slot_3093;
            }
            if (holding || upload_ == Upload::Pending || upload_ == Upload::Preparing || upload_ == Upload::Sent ||
                upload_ == Upload::Failed) {
                if (!holding) {
                    hold_ = Hold::Login;
                    since_ = now_ms;
                }
                has_3093_ = true;
                r.hold = true;
            }
            return r;
        }
        if (holding) {
            r.hold = true;
            return r;
        }
        if (upload_ == Upload::Pending) {
            hold_ = Hold::Login;
            since_ = now_ms;
            r.hold = true;
            return r;
        }
        if (opcode == kOpLogoutSessionReq && final_ok && upload_ == Upload::Acked && !final_done_) {
            hold_ = Hold::Final;
            since_ = now_ms;
            stored_before_ = stored;
            final_done_ = true;
            r.hold = true;
            r.save_first = true;
        }
        return r;
    }

    // Whether the held packets go now, and why.
    Release poll(uint64_t now_ms, uint32_t stored) {
        switch (hold_) {
            case Hold::Off: return Release::None;
            case Hold::Login:
                if (upload_ == Upload::Acked) return end(kept_ ? Release::SlotKept : Release::Acked);
                if (upload_ == Upload::GaveUp) return end(Release::GaveUp);
                if (upload_ == Upload::None) return end(Release::NoOffer);
                if (!has_3093_ && (upload_ == Upload::Sent || upload_ == Upload::Failed)) return end(Release::Queued);
                if (now_ms - since_ >= kLoginHoldMs) {
                    upload_ = Upload::GaveUp;
                    return end(Release::Timeout);
                }
                return Release::None;
            case Hold::Final:
                if (stored > stored_before_) return end(Release::FinalDone);
                if (final_failed_) return end(Release::FinalFailed);
                if (now_ms - since_ >= kFinalHoldMs) return end(Release::FinalTimeout);
                return Release::None;
        }
        return Release::None;
    }
    // The server's 3016 for the final 2910 while the goodbye waits: a failure means no generation will come.
    void final_answer(bool ok) {
        if (hold_ == Hold::Final && !ok) final_failed_ = true;
    }
    // The connection to the host ended: whatever was held is not sent (the caller drops it).
    void on_link_lost() {
        hold_ = Hold::Off;
        has_3093_ = false;
        kept_ = false;
    }
    // P8 re-entry (s25/reentrada.md §3.6): a new remote conversation in the same game run. The upload's state stays
    // (no second upload: the host plays its live guest slot, written by the previous conversation's final save); the
    // holds and the once-per-conversation final save start again.
    // From then on a 3093 for another slot never offers it (OnSend::slot_kept).
    void new_conversation() {
        hold_ = Hold::Off;
        has_3093_ = false;
        kept_ = false;
        final_done_ = false;
        final_failed_ = false;
        since_ = 0;
        reentry_ = true;
        asked_slot_ = slot_;
    }
    bool final_done() const { return final_done_; }
    bool reentry() const { return reentry_; }

    Upload upload() const { return upload_; }
    Hold hold() const { return hold_; }
    uint32_t xfer() const { return xfer_; }
    uint32_t slot() const { return slot_; }
    uint64_t held_since() const { return since_; }

private:
    Release end(Release r) {
        hold_ = Hold::Off;
        kept_ = false;
        has_3093_ = false;
        final_failed_ = false;
        return r;
    }
    bool enabled_ = false;
    Upload upload_ = Upload::None, before_reoffer_ = Upload::None;
    Hold hold_ = Hold::Off;
    uint32_t xfer_ = 0, slot_ = 0, retries_ = 0, stored_before_ = 0;
    std::optional<uint32_t> asked_slot_;
    bool kept_ = false, final_done_ = false, has_3093_ = false, final_failed_ = false;
    bool reentry_ = false;  // a re-entry conversation (new_conversation): no new offer
    uint64_t since_ = 0;
};

inline const char* upload_name(GuestHold::Upload u) {
    switch (u) {
        case GuestHold::Upload::None: return "none";
        case GuestHold::Upload::Pending: return "being prepared";
        case GuestHold::Upload::Preparing: return "the 3093's slot being prepared";
        case GuestHold::Upload::Sent: return "sent, waiting for the host";
        case GuestHold::Upload::Acked: return "acknowledged";
        case GuestHold::Upload::Failed: return "refused once, sent again";
        case GuestHold::Upload::GaveUp: return "GAVE UP";
    }
    return "?";
}

inline const char* release_name(GuestHold::Release r) {
    switch (r) {
        case GuestHold::Release::None: return "none";
        case GuestHold::Release::Queued: return "the upload is in the tunnel before them";
        case GuestHold::Release::Acked: return "the host acknowledged the upload";
        case GuestHold::Release::GaveUp: return "the upload failed (no own save in this session)";
        case GuestHold::Release::NoOffer: return "nothing could be offered (no own save in this session)";
        case GuestHold::Release::Timeout: return "no answer from the host in 30 s (no own save in this session)";
        case GuestHold::Release::FinalDone: return "the final save came back";
        case GuestHold::Release::FinalTimeout: return "no final save in 15 s";
        case GuestHold::Release::SlotKept:
            return "the slot the client asked for cannot be offered: the host keeps the offered one";
        case GuestHold::Release::FinalFailed: return "the server refused the final save (its 3016)";
    }
    return "?";
}
}  // namespace cdmp::session2::gsw
