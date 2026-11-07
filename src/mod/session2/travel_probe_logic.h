#pragma once
// P8 session 15 (docs/research/pendientes/p8-impl-viaje-rapido.md §11): the pure logic of user 2's fast-travel
// reply path on the HOST, with no game access, so the tests can run it. Build 1.0.0.2976.
//
// 1. The owner alias of user 2's copy of LoadingFieldDataAck 2643 [V static, capstone on the 2976 exe]. After user
//    2's 2283 (destination +0x18 = 0, +0x19 = 1), its 2437 goes 0x29C59D0 -> 0x2BC9200 -> 0x2BCA3D0, which writes
//    2643 (writer 0x299B520, `mov eax,0xA53` at 0x299B916; called at 0x2BCA79E) and 3066 (writer 0x299A610,
//    `mov word [r14],0xBFA`; called at 0x2BCA86C) - NOT a 2174 (its writer is 0x29997F0, 0x87E). The 2643's u32 +9
//    is r9 of the writer = [field+0x180] (0x2BCA72B..0x2BCA731 -> [rbp+0x60] -> lea r9,[rbp+0x60]): the FIELD
//    OWNER, the host's user in the shared field. The client's handler 0xB88FD0 keeps it in P+0x12C (external report
//    §10.2). Layout written by 0x299B520: u8 @0, u32 actor @1, u32 FieldInfoKey @5, u32 owner @9, u8 @13, u8 @14,
//    u32 @15, u8 append @19, then records of 48 bytes from +20 (u32, u32, float3 +0x24, float4 +0x14, float3 +8 of
//    each 0x30-byte source record; 0x299B810..0x299B8E2). The 2174's own layout is 17 + 48n (server.cpp).
//    3066 (0x299A610): u32 [character+0x60] (the traveller's own actor key) + u8 + u8: no owner.
// 2. The probe user2_travel_probe = 0 | 1 (travel_probe.h): bounded lines TravelBegin / TravelContext /
//    TravelPrepared / TravelConsume / TravelAck. Their packing and verdicts live here.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include "common/movement_layout.h"

namespace cdmp::session2::tpl {

inline constexpr uint16_t kOpReloadStageStartReq = 2283;
inline constexpr uint16_t kOpGameLoadingStartReq = 2437;
inline constexpr uint16_t kOpMoveFieldAck = 2174;
inline constexpr uint16_t kOpLoadingFieldDataAck = 2643;
inline constexpr uint16_t kOpMoveFieldCompletePostAck = 3066;
inline constexpr uint16_t kOpMoveFieldCompleteAck = 2144;
inline constexpr uint16_t kOpPlayAllPreLoadSequencerAck = 2322;
inline constexpr uint16_t kOpLoadingCompleteAck = 2762;
inline constexpr uint16_t kOpLogoutCharacterAck = 2679;
inline constexpr uint16_t kOpNak = 1013;
// P8 S27 (docs/research/s25/reaparicion.md): the revive's load request; its handler 0x298C490 -> .xtls 0x1048FFC0
// -> 0x2947E00 arms the reviving actor's own TS (+0x569: the 2437 is then answered by 0x2BCA3D0, as after a 2283).
inline constexpr uint16_t kOpReviveLoadingReq = 2362;

inline constexpr size_t kOwnerAt = 9;           // u32 owner, both 2174 and 2643 (payload offset, no Troc header)
inline constexpr size_t kMoveFieldHead = 17;    // 2174: 17 + 48n
inline constexpr size_t kLoadingFieldHead = 20; // 2643: 20 + 48n
inline constexpr size_t kRecord = 0x30;

// ---------------------------------------------------------------- the owner alias (2174 and 2643)

inline bool owner_layout_ok(uint16_t opcode, size_t n, movement::Build build = movement::Build::v2976) {
    const auto* layout = movement::layout(build);
    if (!layout) return false;
    if (opcode == kOpMoveFieldAck) return n >= kMoveFieldHead && (n - kMoveFieldHead) % layout->move_field_record == 0;
    // 3527 handler0xC9E360 uses the same56-byte record reader0x263BDF0
    // as MoveFieldAck, with its own20-byte prefix. No encoded record count.
    if (opcode == kOpLoadingFieldDataAck)
        return n >= kLoadingFieldHead && (n - kLoadingFieldHead) % layout->move_field_record == 0;
    return false;
}

inline bool is_user_key(uint32_t k) { return (k >> 28) == 0x9; }

enum class AliasOut : uint8_t {
    NotOwnerAck,  // neither 2174 nor 2643
    Off,          // the 2643 alias is switched off (user2_loading_owner_alias=0): left as the server wrote it
    BadLayout,    // not the known layout: nothing written (counted, WRN)
    NoKey,        // user 2's user key not known yet: nothing written (counted, WRN)
    NotAUser,     // the u32 +9 is not a user key: left as it is
    AlreadyMine,  // the owner is user 2 already (a field of its own)
    Rewrite,      // user 2's copy names user 2 as the owner (only the u32 +9)
    Count
};

struct AliasIn {
    uint16_t opcode = 0;
    size_t payload_size = 0;
    uint32_t owner = 0;       // the u32 +9 the server wrote
    uint32_t me = 0;          // user 2's user key (its MainUserDataStartAck), 0 = unknown
    bool enabled = true;      // 2174: always; 2643: user2_loading_owner_alias
    movement::Build build = movement::Build::v2976;
};

inline AliasOut decide_owner_alias(const AliasIn& in) {
    if (in.opcode != kOpMoveFieldAck && in.opcode != kOpLoadingFieldDataAck) return AliasOut::NotOwnerAck;
    if (!in.enabled) return AliasOut::Off;
    if (!owner_layout_ok(in.opcode, in.payload_size, in.build)) return AliasOut::BadLayout;
    if (!in.me) return AliasOut::NoKey;
    if (!is_user_key(in.owner)) return AliasOut::NotAUser;
    if (in.owner == in.me) return AliasOut::AlreadyMine;
    return AliasOut::Rewrite;
}

inline const char* alias_name(AliasOut o) {
    switch (o) {
        case AliasOut::NotOwnerAck: return "-";
        case AliasOut::Off: return "alias off";
        case AliasOut::BadLayout: return "UNKNOWN layout, left as it is";
        case AliasOut::NoKey: return "user 2's key unknown, left as it is";
        case AliasOut::NotAUser: return "not a user key, left as it is";
        case AliasOut::AlreadyMine: return "already user 2";
        case AliasOut::Rewrite: return "user 2's copy names user 2";
        default: return "?";
    }
}

// Applies the alias to a whole packet (Troc header of `header` bytes + payload): writes only the u32 at
// header + 9 when the decision is Rewrite. Returns the decision; `owner_out` = the owner the server wrote (0 when the
// layout is not the known one).
inline AliasOut alias_owner(std::span<uint8_t> packet, size_t header, uint16_t opcode, uint32_t me, bool enabled,
                            uint32_t* owner_out = nullptr, movement::Build build = movement::Build::v2976) {
    if (owner_out) *owner_out = 0;
    if (packet.size() < header) return AliasOut::BadLayout;
    AliasIn in;
    in.opcode = opcode;
    in.payload_size = packet.size() - header;
    in.me = me;
    in.enabled = enabled;
    in.build = build;
    if (owner_layout_ok(opcode, in.payload_size, build)) {
        std::memcpy(&in.owner, packet.data() + header + kOwnerAt, 4);
        if (owner_out) *owner_out = in.owner;
    }
    const AliasOut o = decide_owner_alias(in);
    if (o == AliasOut::Rewrite) std::memcpy(packet.data() + header + kOwnerAt, &me, 4);
    return o;
}

// ---------------------------------------------------------------- the 2643 payload

struct LoadingFieldData {
    uint8_t kind = 0;        // @0 (0x299B689 writes 4 on this path)
    uint32_t actor = 0;      // @1
    uint32_t field_key = 0;  // @5
    uint32_t owner = 0;      // @9
    uint8_t flag13 = 0, flag14 = 0;
    uint32_t selection = 0;  // @15
    uint8_t append = 0;      // @19 (0: the client's 0x9404C60 replaces P+0x128/+0x12C/+0x130 and the list)
    size_t records = 0;      // 48-byte records from +20
};

// `p` may be only the head of a payload of `total` bytes (at least its first 20).
inline std::optional<LoadingFieldData> parse_loading_field_data(std::span<const uint8_t> p, size_t total,
                                                             movement::Build build = movement::Build::v2976) {
    if (!owner_layout_ok(kOpLoadingFieldDataAck, total, build) || p.size() < kLoadingFieldHead) return std::nullopt;
    LoadingFieldData d;
    d.kind = p[0];
    std::memcpy(&d.actor, p.data() + 1, 4);
    std::memcpy(&d.field_key, p.data() + 5, 4);
    std::memcpy(&d.owner, p.data() + 9, 4);
    d.flag13 = p[13];
    d.flag14 = p[14];
    std::memcpy(&d.selection, p.data() + 15, 4);
    d.append = p[19];
    d.records = (total - kLoadingFieldHead) / movement::layout(build)->move_field_record;
    return d;
}

inline std::optional<LoadingFieldData> parse_loading_field_data(std::span<const uint8_t> p) {
    return parse_loading_field_data(p, p.size());
}

// LogoutCharacterToFieldServerAck 2679: u32 actor, u8 reason, u32 error (S14: 0B / 0x8E19630A).
struct Logout {
    uint32_t actor = 0;
    uint8_t reason = 0;
    uint32_t error = 0;
};
inline constexpr uint32_t kErrInvalidFieldInfoKey = 0x8E19630A;  // hashlittle("eErrNoInvalidFieldInfoKey")

inline std::optional<Logout> parse_logout(std::span<const uint8_t> p) {
    if (p.size() < 9) return std::nullopt;
    Logout l;
    std::memcpy(&l.actor, p.data(), 4);
    l.reason = p[4];
    std::memcpy(&l.error, p.data() + 5, 4);
    return l;
}

// ---------------------------------------------------------------- TravelPrepared / TravelConsume

// A TransformSync's pending destination: u32 +0x550 (the descriptor 0x2BC4950 copies; 0x2BC9256 tests it against
// 0), u8 +0x568 / +0x569 (destination +0x18 / +0x19: which branch 0x2BC9200 takes) and u8 +0x591 (set to 1 by
// 0x2BC4950). Packed in one u64 for the event ring: bit 63 = read ok.
struct TsSnap {
    bool ok = false;
    uint32_t pending = 0;
    uint8_t f568 = 0, f569 = 0, armed = 0;
};

inline uint64_t pack(const TsSnap& s) {
    if (!s.ok) return 0;
    return (1ull << 63) | (static_cast<uint64_t>(s.f569) << 48) | (static_cast<uint64_t>(s.f568) << 40) |
           (static_cast<uint64_t>(s.armed) << 32) | s.pending;
}

inline TsSnap unpack(uint64_t v) {
    TsSnap s;
    s.ok = (v >> 63) != 0;
    if (!s.ok) return s;
    s.pending = static_cast<uint32_t>(v);
    s.armed = static_cast<uint8_t>(v >> 32);
    s.f568 = static_cast<uint8_t>(v >> 40);
    s.f569 = static_cast<uint8_t>(v >> 48);
    return s;
}

inline bool same(const TsSnap& a, const TsSnap& b) {
    return a.ok == b.ok && a.pending == b.pending && a.armed == b.armed && a.f568 == b.f568 && a.f569 == b.f569;
}

enum class PreparedOut : uint8_t {
    Prepared,     // user 2's +0x550 set and +0x591 = 1 after the 2283; the host's TS and manager +0x6A0 unchanged
    NotPrepared,  // user 2's +0x550 still empty after the 2283 (an early exit of 0x2896120 / Nak): its 2437 will fail
    HostTouched,  // the host's TS (+0x550/+0x568/+0x569/+0x591) or its manager's +0x6A0 changed during user 2's 2283
    Unreadable,   // user 2's TS could not be read
    Count
};

struct PreparedIn {
    TsSnap u2_before, u2_after, host_before, host_after;
    bool host_mgr_ok = false;
    uint8_t host_6a0_before = 0, host_6a0_after = 0;
};

inline PreparedOut decide_prepared(const PreparedIn& in) {
    const bool host_ts_changed = in.host_before.ok && in.host_after.ok && !same(in.host_before, in.host_after);
    const bool host_mgr_changed = in.host_mgr_ok && in.host_6a0_before != in.host_6a0_after;
    if (host_ts_changed || host_mgr_changed) return PreparedOut::HostTouched;
    if (!in.u2_after.ok) return PreparedOut::Unreadable;
    return in.u2_after.pending != 0 && in.u2_after.armed != 0 ? PreparedOut::Prepared : PreparedOut::NotPrepared;
}

inline const char* prepared_name(PreparedOut o) {
    switch (o) {
        case PreparedOut::Prepared: return "PREPARED: user 2's TS set, the host's untouched";
        case PreparedOut::NotPrepared: return "NOT prepared: user 2's TS+0x550 still empty (its 2437 will be refused)";
        case PreparedOut::HostTouched: return "HOST TOUCHED: the host's TS or manager +0x6A0 changed during user 2's 2283";
        case PreparedOut::Unreadable: return "user 2's TS not readable";
        default: return "?";
    }
}

// P8 S27: the same verdict around user 2's ReviveLoadingReq 2362 (TravelRevive), in its own words.
inline const char* revive_name(PreparedOut o) {
    switch (o) {
        case PreparedOut::Prepared: return "ARMED: user 2's TS set by its 2362, the host's untouched";
        case PreparedOut::NotPrepared:
            return "NOT armed: user 2's TS+0x550 still empty after its 2362 (refused: e.g. a living character; a 2679 "
                   "follows)";
        case PreparedOut::HostTouched: return "HOST TOUCHED: the host's TS or manager +0x6A0 changed during user 2's 2362";
        case PreparedOut::Unreadable: return "user 2's TS not readable";
        default: return "?";
    }
}

enum class ConsumeOut : uint8_t {
    Pending,  // user 2's +0x550 was set when its 2437 entered: 0x2BC9200 goes on (+0x568/+0x569 pick the branch)
    Empty,    // empty: 0x2BC9256 -> eErrNoInvalidFieldInfoKey -> 2679 (S14)
    Unreadable,
    Count
};

inline ConsumeOut decide_consume(const TsSnap& before) {
    if (!before.ok) return ConsumeOut::Unreadable;
    return before.pending ? ConsumeOut::Pending : ConsumeOut::Empty;
}

// The branch 0x2BC9200 takes from +0x568/+0x569 (p8-impl-viaje-rapido.md §2.4).
inline const char* consume_branch(const TsSnap& s) {
    if (!s.ok || !s.pending) return "none";
    if (s.f568) return "+0x568 set: GameLoadingStart_Step (field change)";
    if (s.f569) return "+0x569 set: 0x2BCA3D0 (reload in the same field: 2643 + 3066 expected)";
    return "neither flag: no Ack branch known";
}

inline const char* consume_name(ConsumeOut o) {
    switch (o) {
        case ConsumeOut::Pending: return "destination pending";
        case ConsumeOut::Empty: return "EMPTY: eErrNoInvalidFieldInfoKey -> 2679 expected";
        case ConsumeOut::Unreadable: return "user 2's TS not readable";
        default: return "?";
    }
}

// ---------------------------------------------------------------- TravelContext

// The vf48 answer F3 gave for user 2's 2283: right when the selected manager's +0x18 is user 2's character and it
// is not the owner's manager (read, never called).
inline bool context_ok(uint32_t selected_actor_key, uint32_t user2_char, uintptr_t selected, uintptr_t owners) {
    return selected && selected_actor_key && selected_actor_key == user2_char && selected != owners;
}

// ---------------------------------------------------------------- TravelAck

// The Acks queued for user 2 after its 2283 / 2437 entered the server: the first kMaxAcks within kWindowMs are
// logged one by one (opcode, size, owner as the server wrote it and as user 2's copy says); 2679 and Nak 1013 are
// always logged (the first kAlwaysLines).
inline constexpr uint32_t kMaxAcks = 24;
inline constexpr uint64_t kWindowMs = 20000;
inline constexpr uint32_t kAlwaysLines = 8;

inline bool ack_in_window(uint64_t now_ms, uint64_t opened_ms, uint32_t index) {
    return opened_ms && now_ms >= opened_ms && now_ms - opened_ms <= kWindowMs && index < kMaxAcks;
}

inline bool always_noted(uint16_t opcode) { return opcode == kOpLogoutCharacterAck || opcode == kOpNak; }

struct ReplyCounts {
    uint32_t n2174 = 0, n2643 = 0, n3066 = 0, n2144 = 0, n2762 = 0, n2679 = 0, nak = 0;
    uint32_t n2322 = 0;  // P8 S16: PlayAllPreLoadSequencerAck 2322 to user 2 (native or observer_milestone_reload's)
    void add(uint16_t opcode) {
        switch (opcode) {
            case kOpMoveFieldAck: ++n2174; break;
            case kOpLoadingFieldDataAck: ++n2643; break;
            case kOpMoveFieldCompletePostAck: ++n3066; break;
            case kOpMoveFieldCompleteAck: ++n2144; break;
            case kOpLoadingCompleteAck: ++n2762; break;
            case kOpLogoutCharacterAck: ++n2679; break;
            case kOpNak: ++nak; break;
            case kOpPlayAllPreLoadSequencerAck: ++n2322; break;
            default: break;
        }
    }
};

// ---------------------------------------------------------------- P8 S16: the host's side effect of user 2's 2873

// p8-s15-viaje-tunel.md §5: user 2's PreLoadSequencerReq 2873 handed to the server re-prepares the HOST's content
// sequencers (0x2A5F2B0 -> vf48 of the field owner -> 0x2895EF0 -> 0x287EF60): the host's own client gets ~21
// StageBranchStateAck 2981 + ~21 PrepareSequencerAck 2661 (+149..+430 ms) and a 2322 (+460 ms) without having sent a
// 2873 itself. The host client's packets (the sniffer) are counted for kHostBurstUs after user 2's 2873; a 2873 the
// host's own client sent from kHostOwnBeforeUs before it makes the burst ambiguous (its own reload).
inline constexpr uint16_t kOpPreLoadSequencerReq = 2873;
inline constexpr uint16_t kOpPrepareSequencerAck = 2661;
inline constexpr uint16_t kOpStageBranchStateAck = 2981;
inline constexpr uint64_t kHostBurstUs = 3'000'000;
inline constexpr uint64_t kHostOwnBeforeUs = 1'000'000;

struct HostBurst {
    bool armed = false;    // a 2873 of user 2 was seen in this window
    bool handed = false;   // it went to the server (not dropped by observer_milestone_reload)
    uint64_t t_2873 = 0;   // when (the sniffer's clock: p8_now_us)
    uint32_t n2661 = 0, n2981 = 0, n2322 = 0;
    uint64_t t_host_2873 = 0;  // the host client's own last 2873 (C>S), any time

    void open(uint64_t t_us, bool to_server) {
        armed = true;
        handed = to_server;
        t_2873 = t_us;
        n2661 = n2981 = n2322 = 0;
    }
    // The sniffer: one packet of the host's own client.
    void on_host(uint16_t op, bool server_to_client, uint64_t t_us) {
        if (!server_to_client) {
            if (op == kOpPreLoadSequencerReq) t_host_2873 = t_us;
            return;
        }
        if (!armed || t_us < t_2873 || t_us - t_2873 > kHostBurstUs) return;
        if (op == kOpPrepareSequencerAck) ++n2661;
        if (op == kOpStageBranchStateAck) ++n2981;
        if (op == kOpPlayAllPreLoadSequencerAck) ++n2322;
    }
    bool host_own() const {
        return armed && t_host_2873 && t_host_2873 + kHostOwnBeforeUs >= t_2873 && t_host_2873 <= t_2873 + kHostBurstUs;
    }
};

enum class HostEffect : uint8_t {
    No2873,      // user 2 sent no 2873 in this window
    SideEffect,  // its 2873 went to the server and the host's client got a 2661/2322 burst it did not ask for
    Clean,       // no burst on the host's client
    Ambiguous,   // the host's own client sent a 2873 around it (its own reload)
    Count
};

inline HostEffect classify_host(const HostBurst& b) {
    if (!b.armed) return HostEffect::No2873;
    if (b.host_own()) return HostEffect::Ambiguous;
    if (b.n2661 || b.n2322) return HostEffect::SideEffect;
    return HostEffect::Clean;
}

inline const char* host_effect_name(HostEffect e) {
    switch (e) {
        case HostEffect::No2873: return "no 2873 of user 2";
        case HostEffect::SideEffect: return "SIDE EFFECT: the host's client re-prepared its sequencers for user 2's 2873";
        case HostEffect::Clean: return "clean (no 2661/2322 burst on the host's client)";
        case HostEffect::Ambiguous: return "ambiguous (the host's own client sent a 2873 around it)";
        default: return "?";
    }
}

// Packed in one u64 for the summary event: 2661 (16 bits) | 2981 << 16 | 2322 << 32 | armed << 48 | handed << 49 |
// host_own << 50.
inline uint64_t pack(const HostBurst& b) {
    return static_cast<uint64_t>(b.n2661 & 0xFFFF) | static_cast<uint64_t>(b.n2981 & 0xFFFF) << 16 |
           static_cast<uint64_t>(b.n2322 & 0xFFFF) << 32 | static_cast<uint64_t>(b.armed ? 1 : 0) << 48 |
           static_cast<uint64_t>(b.handed ? 1 : 0) << 49 | static_cast<uint64_t>(b.host_own() ? 1 : 0) << 50;
}

struct HostBurstView {
    uint32_t n2661 = 0, n2981 = 0, n2322 = 0;
    bool armed = false, handed = false, host_own = false;
    HostEffect effect = HostEffect::No2873;
};

inline HostBurstView unpack_host(uint64_t v) {
    HostBurstView h;
    h.n2661 = static_cast<uint32_t>(v & 0xFFFF);
    h.n2981 = static_cast<uint32_t>((v >> 16) & 0xFFFF);
    h.n2322 = static_cast<uint32_t>((v >> 32) & 0xFFFF);
    h.armed = (v >> 48) & 1;
    h.handed = (v >> 49) & 1;
    h.host_own = (v >> 50) & 1;
    if (!h.armed) h.effect = HostEffect::No2873;
    else if (h.host_own) h.effect = HostEffect::Ambiguous;
    else if (h.n2661 || h.n2322) h.effect = HostEffect::SideEffect;
    else h.effect = HostEffect::Clean;
    return h;
}

enum class Reply : uint8_t { Nothing, Reload2643, FieldChange2174, Refused2679, Nak, Count };

// What the server answered user 2's travel (the window's counts). A refusal wins over anything else.
inline Reply classify_reply(const ReplyCounts& c) {
    if (c.n2679) return Reply::Refused2679;
    if (c.nak) return Reply::Nak;
    if (c.n2643 || c.n3066) return Reply::Reload2643;
    if (c.n2174) return Reply::FieldChange2174;
    return Reply::Nothing;
}

inline const char* reply_name(Reply r) {
    switch (r) {
        case Reply::Nothing: return "no answer yet";
        case Reply::Reload2643: return "reload in the same field (2643/3066)";
        case Reply::FieldChange2174: return "field change (2174)";
        case Reply::Refused2679: return "REFUSED (2679)";
        case Reply::Nak: return "Nak 1013";
        default: return "?";
    }
}

inline const char* ack_name(uint16_t op) {
    switch (op) {
        case kOpMoveFieldAck: return "MoveFieldAck";
        case kOpLoadingFieldDataAck: return "LoadingFieldDataAck";
        case kOpMoveFieldCompletePostAck: return "MoveFieldCompletePostAck";
        case kOpMoveFieldCompleteAck: return "MoveFieldCompleteAck";
        case kOpPlayAllPreLoadSequencerAck: return "PlayAllPreLoadSequencerAck";
        case kOpLoadingCompleteAck: return "LoadingCompleteAck";
        case kOpLogoutCharacterAck: return "LogoutCharacterToFieldServerAck";
        case kOpNak: return "Nak";
        default: return "";
    }
}

}  // namespace cdmp::session2::tpl
