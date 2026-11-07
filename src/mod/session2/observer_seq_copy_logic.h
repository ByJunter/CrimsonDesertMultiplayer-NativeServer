#pragma once
// P8 S1 option A, sequencer part (docs/research/pendientes/p8-impl-opcion-a.md): [session2]
// observer_seq_copy = 0 | probe | 1, HOST. The pure logic, with no game access, so the tests can
// run it on the packets of the session 7 captures.
//
// The fault [V] (session 7, p8-npc-tiendas-animales-escenas.md §3.2): the named shopkeepers (Grover
// 0x1AB, Dahlia 0x1AD, the innkeeper 0x1A9, the street vendor 0x19E...) and the "chat"/"sweep" NPCs
// are bound to a sequencer of the host's Kliff. The server sends its PrepareSequencerAck 2661 ToTarget
// the field owner's character only (the host's client); the StageBranchStateAck 2981 of a sequencer
// prepared while user 2 is near reaches both clients byte for byte (a near finder), the
// DeleteSequencerAck 2810 again only the owner. User 2's client never builds those sequencers: 0 of
// 40 dialogs with sequencer NPCs, 419/422 with schedule NPCs.
//
// What this does, on the host:
//   ledger   the host's own client's S>C capture (the sniffer's writer thread) keeps, per sequencer
//            the host's client was given, the exact bytes of its last 2661 and 2981 (a 2981 that came
//            before its 2661, the native order, is attached), drops it on its 2810, and forgets the
//            field on the host's own MoveFieldAck 2174; also the host's last 2322 (its milestone).
//   trip     user 2's MoveFieldAck 2174 (take_frame, the owner as the SERVER wrote it, before user
//            2's copy is rewritten) starts a trip; it is an OBSERVER trip when that owner is not user
//            2's user. A snapshot of the ledger becomes the trip's pending copies.
//   order    a pending copy waits until EVERY actor its 2661 binds with type 4/5 (0xA9CA40 drops the
//            sequencer when one is missing, 0xA9CE81) has been announced to user 2 by a 2005 that
//            went to its queue in this trip (and not removed since by a 2516), and until the trip's
//            gate is open: user 2's own 2322 (S2, observer_milestone) went to its queue, or 30 s
//            after its 2144. Then: its last 2981 (only if the last 2981 of that sequencer user 2 got
//            in this trip is another state: payload hash) and its 2661, byte for byte. Live: a 2661
//            the host's client gets during an observer trip becomes a pending copy too; a 2981 of a
//            copied sequencer that did not reach user 2 natively is copied; the 2810 of a sequencer
//            copied in this trip is copied (also when a newer 2661 of it was still pending, which is
//            cancelled); a pending one never copied is just cancelled.
//   2322     once per observer trip, when the gate opens and the host's character was announced,
//            after the copies that are ready: the host's last 2322 (its own character), so the
//            guest's manager of the host's Kliff leaves its preload state 1 (0xA977B6) [E].
//   filter   user 2's C>S 2767/2591/2436/2138/2173 whose sequencer id is one the host's client
//            was given are dropped (they would drive the host's sequencers; user 2's own ids, from
//            the 2661s to its own character, always pass); the same for its 2276 DeleteSequencerReq and
//            2105 StageBranchStateDelayEndReq (the guest's guest_seq_play ticks the host character's
//            manager X: p8-impl-escenas-play.md), each with its own counter; optionally (observer_seq_copy_2100=1,
//            default 0) its 2100 SyncStageCandidatePostionReq while it is an observer (the server
//            applies it to the field's manager: 0x2A5E9D0 -> 0x17FA6C0 -> vf48, the host's) [E fuerte].
// The guest's half (guest_seq_route.h) puts these Acks in the manager of the packet's actor (X);
// without it the guest's client drops them before OnRecvPacket (never into its own manager M).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cdmp::session2::osq {

inline constexpr uint16_t kOpPrepare = 2661;     // PrepareSequencerAck
inline constexpr uint16_t kOpBranch = 2981;      // StageBranchStateAck
inline constexpr uint16_t kOpDelete = 2810;      // DeleteSequencerAck
inline constexpr uint16_t kOpMilestone = 2322;   // PlayAllPreLoadSequencerAck
inline constexpr uint16_t kOpMoveField = 2174;   // MoveFieldAck
inline constexpr uint16_t kOpAnnounce = 2005;    // ActorLoadDataAck
inline constexpr uint16_t kOpRemove = 2516;      // RemoveActorAck: u8 | u32 | u8 reason | n x u32 key
inline constexpr uint16_t kOpComplete = 2144;    // MoveFieldCompleteAck
inline constexpr uint16_t kOpLoaded = 2762;      // LoadingCompleteAck (the end of the 2322's hold, review M2)
// C>S of user 2 the filter looks at.
inline constexpr uint16_t kOpTriggerVolume = 2767;  // u64 seq @0
inline constexpr uint16_t kOpChartEvent = 2591;     // u64 seq @0
inline constexpr uint16_t kOpBranchReq = 2436;      // u64 seq @0
inline constexpr uint16_t kOpStageFail = 2138;      // u64 seq @0
// ReloadStageStartReq: NOT a sequencer Req (P8 session 15, p8-impl-viaje-rapido.md §5). Its 53 bytes (S14 capture,
// the server's reader 0x2A5F580) are u32 0 | u32 FieldInfoKey | u32 target actor | position | rotation | scale | u8:
// "u64 @0" would be FieldInfoKey << 32, never a sequencer id. Not filtered here; fast_travel_logic.h (F2/F3) owns it.
inline constexpr uint16_t kOpReloadStage = 2283;
inline constexpr uint16_t kOpGamePlayData = 2173;   // u64 seq @0, or u32 1 | u32 actor (never a sequencer id)
inline constexpr uint16_t kOpCandidatePos = 2100;   // SyncStageCandidatePostionReq: positions, no scene id
// P8 guest_seq_play (pendientes/p8-impl-escenas-play.md §5): the guest's tick / live update of the host
// character's manager X can send these two about the host's sequencers (p8-xtls-lectura.md §6 R1).
inline constexpr uint16_t kOpDeleteSeqReq = 2276;   // DeleteSequencerReq: u64 seq @0 | u8 code (0xC02CB0/0xC02D1E [V])
// ChangeSequencerPivotReq: u64 seq @0 | position / orientation (24 bytes; session 9 guest capture, e.g.
// "63 01 00 00 00 00 00 00 | 88 73 27 C6 ..." = sequencer 0x163, the host's).
inline constexpr uint16_t kOpPivotReq = 2888;
inline constexpr uint16_t kOpDelayEndReq = 2105;    // StageBranchStateDelayEndReq: one u64 @0, the sequencer id [V]:
                                                    // the server's handler 0x2A5C920 reads 8 bytes (0x2A5C9BC), the
                                                    // whole payload (0x2A5C9F6), and 0x2892210 looks them up with
                                                    // 0x1F6C190(manager, &out, id) (0x289231B), the lookup that the
                                                    // client's 2810 path (0xA9DA0B, the 2810's u64 sequencer) and
                                                    // 0xA95530 (the 2276's u64, 0xA95577) use
// Review 7, the intent: user 2 never legitimately deletes (2276) or advances (2105) a sequencer of the HOST in
// the shared field: natively its client never holds one (the host's 2661 goes ToTarget the host's character
// only), so such a Req can only come from the copy in X. Its own sequencers (own_) always pass, checked first.
// The drop is by id, whatever the trip; the module counts those outside an observer trip apart.

inline constexpr size_t kHeader = 5;           // u16 opcode | i8 slot | u16 payload length
inline constexpr size_t kPreparePrefix = 91;   // u8 kind | u32 actor | 86-byte play data (owner +0, seq +8)
// Build 1.0.0.3527 [V capture host_20261105-074344: 2661 lengths 92 + 11 n; the 2005, 2174, 2981, 2810, 2516 and the C>S
// Reqs this file reads keep their fields]: one byte more of play data, before the bindings.
inline constexpr size_t kPreparePrefix3527 = 92;
inline constexpr size_t kBindingWire = 11;     // u32 node | u32 actor | u8 type | 2 x u8
inline constexpr size_t kBranchMin = 17;       // u8 kind | u32 actor | u64 seq | u32 ...
inline constexpr size_t kDeletePayload = 17;   // u32 actor | u64 seq | u32 | u8
inline constexpr size_t kMilestonePayload = 4; // u32 actor
inline constexpr uint32_t kUserClass = 0x9, kCharClass = 0xA, kNpcClass = 0xB;

inline uint32_t le32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}
inline uint64_t le64(const uint8_t* p) {
    uint64_t v = 0;
    std::memcpy(&v, p, 8);
    return v;
}

// ---------------------------------------------------------------- the key's value

enum class Mode : uint8_t { Off, Probe, On };
inline Mode parse_mode(std::string_view v, bool* bad = nullptr) {
    if (bad) *bad = false;
    if (v.empty() || v == "0") return Mode::Off;
    if (v == "probe") return Mode::Probe;
    if (v == "1") return Mode::On;
    if (bad) *bad = true;
    return Mode::Off;
}
inline const char* mode_name(Mode m) {
    switch (m) {
        case Mode::Off: return "off";
        case Mode::Probe: return "PROBE (nothing sent, nothing dropped)";
        case Mode::On: return "ON";
        default: return "?";
    }
}

// observer_seq_copy_only = "1AB,1AD,B0100C0F": hex sequencer ids (< 0x10000000) and/or actor keys
// (>= 0x10000000) one of its bindings must name. Empty = every sequencer.
struct OnlyFilter {
    std::vector<uint64_t> seqs;
    std::vector<uint32_t> keys;
    std::string bad;  // tokens that are not hex

    bool empty() const { return seqs.empty() && keys.empty(); }

    static OnlyFilter parse(std::string_view list) {
        OnlyFilter f;
        size_t i = 0;
        while (i <= list.size()) {
            size_t j = list.find_first_of(", ;", i);
            if (j == std::string_view::npos) j = list.size();
            std::string_view t = list.substr(i, j - i);
            while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.remove_prefix(1);
            while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.remove_suffix(1);
            if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t.remove_prefix(2);
            if (!t.empty()) {
                uint64_t v = 0;
                bool ok = t.size() <= 16;
                for (char c : t) {
                    int d = -1;
                    if (c >= '0' && c <= '9') d = c - '0';
                    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                    if (d < 0) {
                        ok = false;
                        break;
                    }
                    v = v << 4 | static_cast<uint64_t>(d);
                }
                if (!ok) {
                    f.bad += (f.bad.empty() ? "" : ",") + std::string(t);
                } else if (v >= 0x10000000ull && v <= 0xFFFFFFFFull) {
                    f.keys.push_back(static_cast<uint32_t>(v));
                } else {
                    f.seqs.push_back(v);
                }
            }
            if (j == list.size()) break;
            i = j + 1;
        }
        return f;
    }

    bool matches(uint64_t seq, std::span<const uint32_t> bound) const {
        if (empty()) return true;
        if (std::find(seqs.begin(), seqs.end(), seq) != seqs.end()) return true;
        for (uint32_t k : bound) {
            if (std::find(keys.begin(), keys.end(), k) != keys.end()) return true;
        }
        return false;
    }
};

// ---------------------------------------------------------------- packets

struct Pkt {
    uint16_t op = 0;
    std::span<const uint8_t> payload;
};
// A whole Troc packet (the sniffer's and take_frame's form): its length field must match.
inline std::optional<Pkt> split(std::span<const uint8_t> raw) {
    if (raw.size() < kHeader) return std::nullopt;
    const uint16_t len = static_cast<uint16_t>(raw[3] | raw[4] << 8);
    if (raw.size() != kHeader + len) return std::nullopt;
    return Pkt{static_cast<uint16_t>(raw[0] | raw[1] << 8), raw.subspan(kHeader)};
}

struct Binding {
    uint32_t node = 0, actor = 0;
    uint8_t type = 0;
};
struct Prepare {
    uint8_t kind = 0;
    uint32_t actor = 0;  // addressed actor (+1)
    uint32_t owner = 0;  // play data +0
    uint64_t seq = 0;    // play data +8
    std::vector<Binding> bindings;
};
inline std::optional<Prepare> parse_prepare(std::span<const uint8_t> p, size_t prefix = kPreparePrefix) {
    if (p.size() < prefix || (p.size() - prefix) % kBindingWire != 0) return std::nullopt;
    Prepare r;
    r.kind = p[0];
    r.actor = le32(&p[1]);
    r.owner = le32(&p[5]);
    r.seq = le64(&p[9]);
    const size_t n = (p.size() - prefix) / kBindingWire;
    r.bindings.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* b = &p[prefix + i * kBindingWire];
        r.bindings.push_back({le32(b), le32(b + 4), b[8]});
    }
    return r;
}
// The actors 0xA9CA40 needs before it creates the instance (types 4 and 5), unique, in order.
inline std::vector<uint32_t> required_actors(const Prepare& p) {
    std::vector<uint32_t> out;
    for (const Binding& b : p.bindings) {
        if ((b.type == 4 || b.type == 5) && b.actor && std::find(out.begin(), out.end(), b.actor) == out.end()) {
            out.push_back(b.actor);
        }
    }
    return out;
}
inline std::optional<uint64_t> branch_seq(std::span<const uint8_t> p) {
    if (p.size() < kBranchMin) return std::nullopt;
    return le64(&p[5]);
}
inline std::optional<uint64_t> delete_seq(std::span<const uint8_t> p) {
    if (p.size() != kDeletePayload) return std::nullopt;
    return le64(&p[4]);
}
// The actor keys a 2005 announces: records [u16 len][u8 type][u32 key]...; type 0 is the user record.
inline void announce_keys(std::span<const uint8_t> p, std::vector<uint32_t>& out, size_t max = 4096) {
    size_t n = 0, i = 0;
    while (n < max && i + 2 <= p.size()) {
        const uint16_t len = static_cast<uint16_t>(p[i] | p[i + 1] << 8);
        if (len == 0 || i + 2 + len > p.size()) break;
        if (len >= 5 && p[i + 2] != 0) {
            out.push_back(le32(&p[i + 3]));
            ++n;
        }
        i += 2 + len;
    }
}
// RemoveActorAck 2516: u8 | u32 | u8 reason | n x u32 key (session 7: 04 00000000 1E 570110B0).
inline void removed_keys(std::span<const uint8_t> p, std::vector<uint32_t>& out) {
    for (size_t i = 6; i + 4 <= p.size(); i += 4) out.push_back(le32(&p[i]));
}
// MoveFieldAck 2174: u8 | u32 actor | u32 | u32 owner user @9 | ... (17 + 0x30 n bytes).
inline std::optional<uint32_t> move_owner(std::span<const uint8_t> p) {
    if (p.size() < 17 || (p.size() - 17) % 0x30 != 0) return std::nullopt;
    return le32(&p[9]);
}

// ---------------------------------------------------------------- the host's ledger

struct SeqRec {
    uint64_t seq = 0;
    uint32_t actor = 0, owner = 0;
    uint64_t order = 0;  // the ledger's arrival order (the 2661's)
    uint64_t t_us = 0;
    std::vector<uint8_t> prepare;  // the whole 2661 packet
    std::vector<uint8_t> branch;   // the whole last 2981 packet (may be empty)
    uint64_t branch_t_us = 0;
    uint32_t branches = 0;
    std::vector<uint32_t> required;
    std::vector<Binding> bindings;
};

enum class HostSeen : uint8_t {
    Other,
    Prepared,
    Replaced,
    NotOwn,        // a 2661 whose owner (play data +0) is not its addressed actor: never copied
    Branch,        // of a live sequencer
    BranchEarly,   // before its 2661 (the native order), kept for it
    Deleted,
    DeleteUnknown,
    Milestone,
    NewField,      // the host's own MoveFieldAck: its sequencers start again
    Full,
    Bad
};
inline const char* host_seen_name(HostSeen s) {
    switch (s) {
        case HostSeen::Other: return "other";
        case HostSeen::Prepared: return "prepared";
        case HostSeen::Replaced: return "prepared again";
        case HostSeen::NotOwn: return "not its own (owner != addressed actor): never copied";
        case HostSeen::Branch: return "branch";
        case HostSeen::BranchEarly: return "branch before its 2661";
        case HostSeen::Deleted: return "deleted";
        case HostSeen::DeleteUnknown: return "deleted (not in the ledger)";
        case HostSeen::Milestone: return "2322";
        case HostSeen::NewField: return "the host's MoveFieldAck: ledger emptied";
        case HostSeen::Full: return "LEDGER FULL";
        case HostSeen::Bad: return "malformed";
        default: return "?";
    }
}

struct LedgerLimits {
    size_t max_live = 1024;
    size_t max_early = 64;    // 2981s waiting for their 2661 (user 2's own scenes' too: they never match)
    size_t max_known = 8192;  // sequencer ids ever given to the host's client (the C>S filter)
};

class Ledger {
public:
    explicit Ledger(LedgerLimits l = {}) : l_(l) {}

    // Build 1.0.0.3527: kPreparePrefix3527. Set before the first packet.
    void set_prepare_prefix(size_t prefix) { prefix_ = prefix; }

    // One S>C packet of the host's own client (whole). seq: the sequencer it is about.
    HostSeen on_host_packet(std::span<const uint8_t> raw, uint64_t t_us, uint64_t* seq_out = nullptr) {
        const auto v = split(raw);
        if (!v) return HostSeen::Other;
        if (seq_out) *seq_out = 0;
        switch (v->op) {
            case kOpMoveField:
                // A new field of the host: its sequencers, the 2981s waiting for them and its last 2322 (the
                // milestone of its previous field) start again; the ids stay known (unique per server).
                live_.clear();
                early_.clear();
                milestone_.clear();
                milestone_actor_ = 0;
                ++fields_;
                return HostSeen::NewField;
            case kOpPrepare: {
                const auto p = parse_prepare(v->payload, prefix_);
                if (!p || p->kind != 4) return HostSeen::Bad;
                if (seq_out) *seq_out = p->seq;
                if (p->owner != p->actor) {
                    ++not_own_;
                    return HostSeen::NotOwn;
                }
                host_char_ = p->actor;
                remember(p->seq);
                auto it = live_.find(p->seq);
                const bool again = it != live_.end();
                if (!again && live_.size() >= l_.max_live) {
                    ++full_;
                    return HostSeen::Full;
                }
                SeqRec& r = live_[p->seq];
                const std::vector<uint8_t> keep_branch = again ? r.branch : std::vector<uint8_t>{};
                const uint64_t keep_bt = again ? r.branch_t_us : 0;
                const uint32_t keep_bn = again ? r.branches : 0;
                r = SeqRec{};
                r.seq = p->seq;
                r.actor = p->actor;
                r.owner = p->owner;
                r.order = ++order_;
                r.t_us = t_us;
                r.prepare.assign(raw.begin(), raw.end());
                r.required = required_actors(*p);
                r.bindings = p->bindings;
                r.branch = keep_branch;
                r.branch_t_us = keep_bt;
                r.branches = keep_bn;
                if (auto e = early_.find(p->seq); e != early_.end()) {
                    r.branch = std::move(e->second.bytes);
                    r.branch_t_us = e->second.t_us;
                    ++r.branches;
                    early_.erase(e);
                }
                ++prepared_;
                return again ? HostSeen::Replaced : HostSeen::Prepared;
            }
            case kOpBranch: {
                const auto seq = branch_seq(v->payload);
                if (!seq) return HostSeen::Bad;
                if (seq_out) *seq_out = *seq;
                if (auto it = live_.find(*seq); it != live_.end()) {
                    it->second.branch.assign(raw.begin(), raw.end());
                    it->second.branch_t_us = t_us;
                    ++it->second.branches;
                    return HostSeen::Branch;
                }
                if (early_.size() >= l_.max_early && !early_.count(*seq)) {
                    // the oldest goes (user 2's own scenes' 2981 reach the host too and never get a 2661 here)
                    auto oldest = early_.begin();
                    for (auto it = early_.begin(); it != early_.end(); ++it) {
                        if (it->second.t_us < oldest->second.t_us) oldest = it;
                    }
                    early_.erase(oldest);
                }
                early_[*seq] = Early{std::vector<uint8_t>(raw.begin(), raw.end()), t_us};
                return HostSeen::BranchEarly;
            }
            case kOpDelete: {
                const auto seq = delete_seq(v->payload);
                if (!seq) return HostSeen::Bad;
                if (seq_out) *seq_out = *seq;
                early_.erase(*seq);
                if (live_.erase(*seq)) {
                    ++deleted_;
                    return HostSeen::Deleted;
                }
                return HostSeen::DeleteUnknown;
            }
            case kOpMilestone: {
                if (v->payload.size() != kMilestonePayload) return HostSeen::Bad;
                const uint32_t actor = le32(v->payload.data());
                if ((actor >> 28) != kCharClass) return HostSeen::Other;
                milestone_.assign(raw.begin(), raw.end());
                milestone_actor_ = actor;
                return HostSeen::Milestone;
            }
            default:
                return HostSeen::Other;
        }
    }

    const SeqRec* find(uint64_t seq) const {
        auto it = live_.find(seq);
        return it == live_.end() ? nullptr : &it->second;
    }
    // The live sequencers in the order their 2661 came.
    std::vector<const SeqRec*> live_in_order() const {
        std::vector<const SeqRec*> v;
        v.reserve(live_.size());
        for (const auto& [k, r] : live_) v.push_back(&r);
        std::sort(v.begin(), v.end(), [](const SeqRec* a, const SeqRec* b) { return a->order < b->order; });
        return v;
    }
    // A sequencer id the host's client was given a 2661 of (ever, bounded).
    bool known(uint64_t seq) const { return known_.count(seq) != 0; }
    size_t live() const { return live_.size(); }
    uint32_t host_char() const { return host_char_; }
    const std::vector<uint8_t>& milestone() const { return milestone_; }
    uint32_t milestone_actor() const { return milestone_actor_; }
    uint64_t prepared() const { return prepared_; }
    uint64_t deleted() const { return deleted_; }
    uint64_t full() const { return full_; }
    uint64_t not_own() const { return not_own_; }
    uint64_t fields() const { return fields_; }
    size_t early() const { return early_.size(); }

private:
    struct Early {
        std::vector<uint8_t> bytes;
        uint64_t t_us = 0;
    };
    void remember(uint64_t seq) {
        if (known_.size() < l_.max_known) known_.insert(seq);
        else if (!known_.count(seq)) ++known_full_;
    }

public:
    // A new server instance (the host back at the title): its sequencer ids start again.
    void clear_all() {
        live_.clear();
        early_.clear();
        known_.clear();
        milestone_.clear();
        milestone_actor_ = 0;
        host_char_ = 0;
    }
    uint64_t known_full() const { return known_full_; }

private:
    uint64_t known_full_ = 0;

    LedgerLimits l_;
    size_t prefix_ = kPreparePrefix;
    std::unordered_map<uint64_t, SeqRec> live_;
    std::unordered_map<uint64_t, Early> early_;
    std::unordered_set<uint64_t> known_;
    std::vector<uint8_t> milestone_;
    uint32_t milestone_actor_ = 0, host_char_ = 0;
    uint64_t order_ = 0, prepared_ = 0, deleted_ = 0, full_ = 0, not_own_ = 0, fields_ = 0;
};

// ---------------------------------------------------------------- user 2's trip and its copies

enum class Origin : uint8_t { Snapshot, Live };

struct CopierLimits {
    size_t max_pending = 1024;    // = LedgerLimits::max_live: a pending copy is a live sequencer of the ledger
    size_t max_announced = 65536;
    size_t max_delivered = 1024;  // over it the oldest delivered mark goes (counted: its 2810 would not be copied)
    size_t max_branch_seen = 4096;
    size_t max_own = 4096;
    uint64_t gate_after_complete_us = 30'000'000;  // no 2322 of user 2 this long after its 2144: open anyway
};

// FNV-1a 64 of a 2981's payload: which state of a sequencer user 2 got last.
inline uint64_t payload_hash(std::span<const uint8_t> b) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (uint8_t c : b) {
        h ^= c;
        h *= 0x100000001B3ull;
    }
    return h;
}
inline uint64_t packet_payload_hash(std::span<const uint8_t> raw) {
    return raw.size() >= kHeader ? payload_hash(raw.subspan(kHeader)) : 0;
}

// What release() pushed (or, in probe, would have pushed), for the lines.
struct Released {
    enum Kind : uint8_t { Pair, Milestone, Delete } kind = Pair;
    uint64_t seq = 0;
    Origin origin = Origin::Snapshot;
    bool with_branch = false;
    size_t required = 0;
    uint64_t waited_us = 0;   // since it became pending
    size_t bytes = 0;
    bool pushed = true;       // false: the queue refused it (it stays pending)
    bool after_milestone = false;  // a snapshot copy that went after the host's 2322 (review M2)
};

class Copier {
public:
    explicit Copier(CopierLimits l = {}) : l_(l) {}

    void set_filter(OnlyFilter f) { only_ = std::move(f); }
    void set_milestone(bool on) { milestone_on_ = on; }
    // P8 session 14 review M2 (observer_load_defer): the host's NPCs announced to user 2 during its load reach it
    // only after its 2762, so snapshot copies that bind them become ready after it. With hold_us > 0 the host's
    // 2322 waits while a snapshot copy still waits for its actors: until user 2's 2762 went to its queue plus
    // hold_us, or kMilestoneHoldMaxUs after the trip began (no 2762 seen). 0 = as before (the 2322 goes once the
    // gate is open and the host's character was announced).
    void set_milestone_hold(uint64_t hold_us) { milestone_hold_us_ = hold_us; }
    static constexpr uint64_t kMilestoneHoldMaxUs = 62'000'000;  // observer_load_defer's 60 s cap + 2 s
    void set_filter_2100(bool on) { filter_2100_ = on; }
    const OnlyFilter& filter() const { return only_; }

    // A new guest epoch, user 2 gone, a new trip: nothing of the old trip goes on.
    void clear() {
        observer_ = false;
        trip_active_ = false;
        gate_open_ = false;
        milestone_sent_ = false;
        milestone_hold_noted_ = false;
        complete_us_ = 0;
        loaded_us_ = 0;
        trip_us_ = 0;
        announced_.clear();
        branch_seen_.clear();
        pending_.clear();
        delivered_.clear();
    }
    // A new server instance: user 2's own sequencer ids start again too.
    void clear_server() {
        clear();
        own_.clear();
    }

    // User 2's MoveFieldAck went to its queue. observer: the field's owner (as the server wrote it)
    // is not user 2. The snapshot: every live sequencer of the ledger that the filter lets through.
    // Returns how many became pending.
    size_t on_user2_move(bool observer, uint64_t now_us, const Ledger& ledger) {
        clear();
        ++trips_;
        trip_active_ = true;
        observer_ = observer;
        trip_us_ = now_us ? now_us : 1;
        if (!observer) return 0;
        ++observer_trips_;
        size_t n = 0;
        for (const SeqRec* r : ledger.live_in_order()) {
            if (add_pending(*r, Origin::Snapshot, now_us, ledger)) ++n;
        }
        return n;
    }
    // A 2005 to user 2's queue.
    void on_user2_announce(std::span<const uint32_t> keys) {
        for (uint32_t k : keys) {
            if (announced_.size() >= l_.max_announced && !announced_.count(k)) {
                ++announced_overflow_;
                continue;
            }
            announced_.insert(k);
        }
    }
    // A 2516 to user 2's queue. Returns the delivered sequencers that bind one of these keys (their
    // instance in the guest's X still names it: there is no 2810 for user 2 alone) [E].
    std::vector<uint64_t> on_user2_remove(std::span<const uint32_t> keys, const Ledger& ledger) {
        std::vector<uint64_t> hit;
        for (uint32_t k : keys) announced_.erase(k);
        for (const auto& [seq, d] : delivered_) {
            const SeqRec* r = ledger.find(seq);
            if (!r) continue;
            for (uint32_t k : keys) {
                if (std::find(r->required.begin(), r->required.end(), k) != r->required.end()) {
                    hit.push_back(seq);
                    ++bound_removed_;
                    break;
                }
            }
        }
        return hit;
    }
    // A 2981 of `seq` (its payload's hash) went to user 2's queue natively (a near finder). The copies
    // send a 2981 only when the ledger's last one is not the last one user 2 got.
    void on_user2_branch(uint64_t seq, uint64_t hash) {
        if (branch_seen_.size() < l_.max_branch_seen || branch_seen_.count(seq)) branch_seen_[seq] = hash;
    }
    // A 2661 to user 2's queue naming user 2's own character: its own scene (the C>S filter lets its Reqs pass).
    void on_user2_own(uint64_t seq) {
        if (own_.size() < l_.max_own) own_.insert(seq);
    }
    // User 2's own 2322 (S2) went to its queue: from now on the copies may go.
    void on_user2_milestone() { gate_open_ = true; }
    void on_user2_complete(uint64_t now_us) {
        if (!complete_us_) complete_us_ = now_us;
    }
    // User 2's LoadingCompleteAck 2762 went to its queue (review M2: the end of the 2322's hold).
    void on_user2_loaded(uint64_t now_us) {
        if (trip_active_ && !loaded_us_) loaded_us_ = now_us ? now_us : 1;
    }
    // Is the host's 2322 held now (review M2)? A snapshot copy still waits for one of its actors, and the hold
    // has not run out (user 2's 2762 + hold, or the trip's start + kMilestoneHoldMaxUs).
    bool milestone_held(uint64_t now_us) const {
        if (!milestone_hold_us_) return false;
        const uint64_t until = loaded_us_ ? loaded_us_ + milestone_hold_us_ : trip_us_ + kMilestoneHoldMaxUs;
        if (now_us >= until) return false;
        // Called after release()'s walk: a snapshot copy still pending there waits for its actors.
        return std::any_of(pending_.begin(), pending_.end(),
                           [](const Pending& p) { return p.origin == Origin::Snapshot; });
    }

    // The host's client got a 2661 (the ledger has it already). During an observer trip it becomes
    // pending (live). A sequencer already copied in this trip keeps its "delivered" mark: if the host
    // deletes it, user 2 gets that 2810 too (the guest's X has the old instance).
    bool on_host_prepared(const SeqRec& r, uint64_t now_us, const Ledger& ledger) {
        if (!trip_active_ || !observer_) return false;
        return add_pending(r, Origin::Live, now_us, ledger);
    }
    // The host's client got a 2810. A pending copy is cancelled; if a copy of that sequencer reached
    // user 2's queue in this trip, the 2810 is copied (Copy), whether or not a newer one was pending.
    enum class DeleteFate : uint8_t { None, Cancelled, Copy };
    DeleteFate on_host_deleted(uint64_t seq) {
        if (!trip_active_ || !observer_) return DeleteFate::None;
        bool cancelled = false;
        for (auto it = pending_.begin(); it != pending_.end(); ++it) {
            if (it->seq == seq) {
                pending_.erase(it);
                ++cancelled_;
                cancelled = true;
                break;
            }
        }
        if (delivered_.erase(seq)) return DeleteFate::Copy;
        return cancelled ? DeleteFate::Cancelled : DeleteFate::None;
    }
    // The host's client got a 2981 of `seq` (the ledger has it). True: copy it now — the sequencer was
    // copied in this trip and user 2's last 2981 of it is another state (it did not reach user 2
    // natively). After a successful push the caller calls on_user2_branch with its hash.
    bool host_branch_needs_copy(uint64_t seq, uint64_t hash) const {
        if (!trip_active_ || !observer_ || !gate_open_ || !delivered_.count(seq)) return false;
        auto it = branch_seen_.find(seq);
        return it == branch_seen_.end() || it->second != hash;
    }

    bool gate_open(uint64_t now_us) {
        if (!gate_open_ && complete_us_ && now_us >= complete_us_ + l_.gate_after_complete_us) {
            gate_open_ = true;
            ++gate_by_timeout_;
        }
        return gate_open_;
    }

    // Pushes what is ready, in the ledger's order: [its 2981 if user 2's last one of it is another
    // state] then its 2661; then, once per observer trip, the host's 2322. push(bytes) -> false: the
    // queue refused; that copy and the rest stay pending (tried again on the next call). out: one
    // entry per copy. Bounded: at most max_pending entries walked, kept in order on insertion.
    template <class Push>
    size_t release(uint64_t now_us, const Ledger& ledger, Push&& push, std::vector<Released>* out = nullptr) {
        if (!trip_active_ || !observer_ || !gate_open(now_us)) return 0;
        if (pending_.empty() && (milestone_sent_ || !milestone_on_)) return 0;
        size_t n = 0;
        for (auto it = pending_.begin(); it != pending_.end();) {
            const SeqRec* r = ledger.find(it->seq);
            if (!r) {  // deleted from the ledger meanwhile (a new field of the host)
                it = pending_.erase(it);
                ++vanished_;
                continue;
            }
            if (!all_announced(r->required)) {
                ++it;
                continue;
            }
            const uint64_t bh = r->branch.empty() ? 0 : packet_payload_hash(r->branch);
            auto seen = branch_seen_.find(r->seq);
            const bool with_branch = !r->branch.empty() && (seen == branch_seen_.end() || seen->second != bh);
            Released rel{Released::Pair, r->seq, it->origin, with_branch, r->required.size(),
                         now_us >= it->since_us ? now_us - it->since_us : 0,
                         r->prepare.size() + (with_branch ? r->branch.size() : 0), true};
            if (with_branch && !push(std::span<const uint8_t>(r->branch))) {
                rel.pushed = false;
                if (out) out->push_back(rel);
                ++refused_;
                return n;
            }
            if (with_branch) on_user2_branch(r->seq, bh);  // it went: next time without it
            if (!push(std::span<const uint8_t>(r->prepare))) {
                rel.pushed = false;
                if (out) out->push_back(rel);
                ++refused_;
                return n;
            }
            mark_delivered(r->seq, now_us);
            ++copied_;
            if (with_branch) ++copied_branches_;
            // Review M2: a snapshot copy that goes after the host's 2322 (in S13 they all went before it).
            if (milestone_sent_ && it->origin == Origin::Snapshot) {
                rel.after_milestone = true;
                ++snapshot_after_milestone_;
            }
            ++n;
            if (out) out->push_back(rel);
            it = pending_.erase(it);
        }
        if (milestone_on_ && !milestone_sent_ && !ledger.milestone().empty() &&
            announced_.count(ledger.milestone_actor()) && milestone_held(now_us)) {
            if (!milestone_hold_noted_) {
                milestone_hold_noted_ = true;
                ++milestone_holds_;
            }
            return n;
        }
        if (milestone_on_ && !milestone_sent_ && !ledger.milestone().empty() &&
            announced_.count(ledger.milestone_actor())) {
            // Held before and it ran out with a snapshot copy still waiting for its actors.
            if (milestone_hold_noted_ && std::any_of(pending_.begin(), pending_.end(), [](const Pending& p) {
                    return p.origin == Origin::Snapshot;
                })) {
                ++milestone_hold_expired_;
            }
            if (push(std::span<const uint8_t>(ledger.milestone()))) {
                milestone_sent_ = true;
                ++milestones_;
                ++n;
                if (out) out->push_back({Released::Milestone, 0, Origin::Snapshot, false, 1, 0, ledger.milestone().size(), true});
            } else {
                ++refused_;
                if (out) out->push_back({Released::Milestone, 0, Origin::Snapshot, false, 1, 0, ledger.milestone().size(), false});
            }
        }
        return n;
    }

    // ---- C>S filter
    // HostDelete / HostDelayEnd: 2276 / 2105 about a host's sequencer (their own counters and lines).
    // HostPivot: 2888 ChangeSequencerPivotReq about a host's sequencer (session 9: the guest sent it about the
    // host's 0x163 at ~90 Hz once X played: a second conductor of the host's scene).
    enum class Req : uint8_t { Pass, HostSequencer, Candidate, HostDelete, HostDelayEnd, HostPivot, Count };
    Req judge_req(uint16_t op, std::span<const uint8_t> p, const Ledger& ledger) const {
        switch (op) {
            case kOpPivotReq: {
                if (p.size() < 8) return Req::Pass;
                const uint64_t seq = le64(p.data());
                if (own_.count(seq)) return Req::Pass;  // user 2's own scene: never dropped
                return ledger.known(seq) ? Req::HostPivot : Req::Pass;
            }
            case kOpDeleteSeqReq:
            case kOpDelayEndReq: {
                if (p.size() < 8) return Req::Pass;
                const uint64_t seq = le64(p.data());
                if (own_.count(seq)) return Req::Pass;  // user 2's own scene: never dropped
                if (!ledger.known(seq)) return Req::Pass;
                return op == kOpDeleteSeqReq ? Req::HostDelete : Req::HostDelayEnd;
            }
            case kOpTriggerVolume:
            case kOpChartEvent:
            case kOpBranchReq:
            case kOpStageFail:
            case kOpGamePlayData: {
                if (p.size() < 8) return Req::Pass;
                const uint64_t seq = le64(p.data());
                if (own_.count(seq)) return Req::Pass;  // user 2's own scene: always its own to drive
                return ledger.known(seq) ? Req::HostSequencer : Req::Pass;
            }
            case kOpCandidatePos:
                return filter_2100_ && trip_active_ && observer_ ? Req::Candidate : Req::Pass;
            default:
                return Req::Pass;
        }
    }

    bool observer() const { return trip_active_ && observer_; }
    bool trip_active() const { return trip_active_; }
    bool gate_is_open() const { return gate_open_; }
    bool milestone_sent() const { return milestone_sent_; }
    bool announced(uint32_t k) const { return announced_.count(k) != 0; }
    bool delivered(uint64_t seq) const { return delivered_.count(seq) != 0; }
    bool own(uint64_t seq) const { return own_.count(seq) != 0; }
    size_t pending() const { return pending_.size(); }
    size_t announced_count() const { return announced_.size(); }
    size_t delivered_count() const { return delivered_.size(); }
    // The keys a pending copy still waits for (for the lines).
    std::vector<uint32_t> waiting_for(uint64_t seq, const Ledger& ledger) const {
        std::vector<uint32_t> w;
        if (const SeqRec* r = ledger.find(seq)) {
            for (uint32_t k : r->required) {
                if (!announced_.count(k)) w.push_back(k);
            }
        }
        return w;
    }
    std::vector<uint64_t> pending_seqs() const {
        std::vector<uint64_t> v;
        for (const Pending& p : pending_) v.push_back(p.seq);
        return v;
    }

    struct Counters {
        uint64_t trips = 0, observer_trips = 0, filtered = 0, pending_full = 0, copied = 0, copied_branches = 0,
                 milestones = 0, cancelled = 0, vanished = 0, refused = 0, bound_removed = 0, announced_overflow = 0,
                 gate_by_timeout = 0, delivered_evicted = 0, purged = 0;
        // Review M2: trips whose 2322 waited for snapshot copies; of them, those whose wait ran out with a snapshot
        // copy still pending; snapshot copies that went after the 2322 anyway.
        uint64_t milestone_holds = 0, milestone_hold_expired = 0, snapshot_after_milestone = 0;
    };
    Counters counters() const {
        return {trips_,         observer_trips_, filtered_,      pending_full_,       copied_,
                copied_branches_, milestones_,   cancelled_,     vanished_,           refused_,
                bound_removed_, announced_overflow_, gate_by_timeout_, delivered_evicted_, purged_,
                milestone_holds_, milestone_hold_expired_, snapshot_after_milestone_};
    }
    bool milestone_waiting() const { return milestone_hold_noted_ && !milestone_sent_; }

private:
    struct Pending {
        uint64_t seq = 0;
        uint64_t order = 0;
        uint64_t since_us = 0;
        Origin origin = Origin::Snapshot;
    };
    bool add_pending(const SeqRec& r, Origin o, uint64_t now_us, const Ledger& ledger) {
        std::vector<uint32_t> bound;
        for (const Binding& b : r.bindings) bound.push_back(b.actor);
        if (!only_.matches(r.seq, bound)) {
            ++filtered_;
            return false;
        }
        for (auto it = pending_.begin(); it != pending_.end(); ++it) {
            if (it->seq == r.seq) {  // prepared again: its new place in the order
                Pending p = *it;
                p.order = r.order;
                pending_.erase(it);
                insert_ordered(p);
                return true;
            }
        }
        if (pending_.size() >= l_.max_pending) {
            // Entries whose sequencer left the ledger make room (the release would drop them anyway).
            const size_t before = pending_.size();
            pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                          [&](const Pending& p) { return ledger.find(p.seq) == nullptr; }),
                           pending_.end());
            purged_ += before - pending_.size();
        }
        if (pending_.size() >= l_.max_pending) {
            ++pending_full_;
            return false;
        }
        insert_ordered({r.seq, r.order, now_us, o});
        return true;
    }
    void insert_ordered(const Pending& p) {
        auto at = std::upper_bound(pending_.begin(), pending_.end(), p.order,
                                   [](uint64_t o, const Pending& x) { return o < x.order; });
        pending_.insert(at, p);
    }
    void mark_delivered(uint64_t seq, uint64_t now_us) {
        if (!delivered_.count(seq) && delivered_.size() >= l_.max_delivered) {
            auto oldest = delivered_.begin();
            for (auto it = delivered_.begin(); it != delivered_.end(); ++it) {
                if (it->second < oldest->second) oldest = it;
            }
            delivered_.erase(oldest);
            ++delivered_evicted_;
        }
        delivered_[seq] = now_us;
    }
    bool all_announced(const std::vector<uint32_t>& keys) const {
        for (uint32_t k : keys) {
            if (!announced_.count(k)) return false;
        }
        return true;
    }

    CopierLimits l_;
    OnlyFilter only_;
    bool milestone_on_ = true;
    bool filter_2100_ = false;  // observer_seq_copy_2100 (default 0: p8-impl-opcion-a.md §7)
    bool trip_active_ = false, observer_ = false, gate_open_ = false, milestone_sent_ = false;
    bool milestone_hold_noted_ = false;  // this trip's 2322 waited for snapshot copies (review M2)
    uint64_t milestone_hold_us_ = 0;     // 0 = no hold (set_milestone_hold)
    uint64_t complete_us_ = 0;
    uint64_t loaded_us_ = 0;  // user 2's 2762 went to its queue in this trip
    uint64_t trip_us_ = 0;    // this trip's MoveFieldAck
    uint64_t milestone_holds_ = 0, milestone_hold_expired_ = 0, snapshot_after_milestone_ = 0;
    std::unordered_set<uint32_t> announced_;
    std::unordered_map<uint64_t, uint64_t> branch_seen_;  // seq -> hash of the last 2981 payload user 2 got
    std::vector<Pending> pending_;                        // ascending order (the ledger's)
    std::map<uint64_t, uint64_t> delivered_;              // seq -> when its 2661 copy went (this trip)
    std::unordered_set<uint64_t> own_;                    // user 2's own sequencer ids (this server)
    uint64_t trips_ = 0, observer_trips_ = 0, filtered_ = 0, pending_full_ = 0, copied_ = 0, copied_branches_ = 0,
             milestones_ = 0, cancelled_ = 0, vanished_ = 0, refused_ = 0, bound_removed_ = 0, announced_overflow_ = 0,
             gate_by_timeout_ = 0, delivered_evicted_ = 0, purged_ = 0;
};

// ================================================================ schedule stages (observer_sched_copy)
// docs/research/pendientes/p8-impl-horarios-snapshot.md (design: p8-impl-horarios.md §5). [session2]
// observer_sched_copy = 0 | 1, HOST, on top of observer_seq_copy (its sniffer path, trips, gate and probe).
//
// The fault [V] (session 11, npc-raiz-s11 INFORME §4): the 2335 CreateNPCScheduleStageAck is ToNear, so user
// 2 gets only the stages created while it is near; the 2126 DeleteNPCScheduleStageAck, 2958
// ChangeNPCScheduleMiseensceneAck and 2909 DeactivateMiseensceneScheduleEvadeObstacleAck go ToTarget the owner
// (the host's client) only (2126 also has a ToNear site: 67 of 189 reached the guest). Before the guest's entry
// 42/46 live stages of the host never reach it; 17/18 exact ids its NPCs name are missing.
//
//   ledger   the host's own client's S>C capture keeps, per scene key, the whole last CREATING 2335 (kind 1/4,
//            the kinds 0xA9FEE0 creates for) and, after it, in order, the 2958/2909 (and a non-creating 2335)
//            of that key: the state a late copy must reach, not only the creation. Its 2126 drops it, and the
//            2411 that closes it (u32 actor | u32 id = the key's low 32 bits, and the same actor [E: format]);
//            the host's own 2174 empties it (as the 2661 ledger). Fixed size: no allocation.
//   trip     user 2's observer trip (the same rule as the 2661s) turns the ledger's keys into pending copies,
//            in the ledger's order; user 2's own character's stages are not copied.
//   release  once user 2 finished loading the field (its 2549, or its 2144) and the 2335's actor was announced
//            to user 2 by a 2005 in this trip — right behind the host character's 2005, before the later NPC
//            2005s, because an NPC binds to its stage only when its 2005 is unpacked (p8-impl-horarios-x.md
//            §4.1) — the 2335 and its follow-ups, byte for byte, resumable after a refusal. A key whose 2335
//            reached user 2 natively first is not repeated (its follow-ups still go: they are ToTarget); a
//            native 2335 after our copy is counted, not merged.
//   live     a creating 2335 user 2 did not get natively becomes pending (same gate); a 2126/2958/2909 of a
//            key user 2 has (native or copied) is copied, unless the same payload just went to user 2 natively
//            (take_frame sees it before the host's client reads it: one emission). A 2126 of a key whose copy
//            has not gone cancels it; a 2958/2909 of a pending key goes with that copy, in order.

inline constexpr uint16_t kOpSchedCreate = 2335;  // CreateNPCScheduleStageAck (ToNear)
inline constexpr uint16_t kOpSchedDelete = 2126;  // DeleteNPCScheduleStageAck (ToTarget the owner; one ToNear site)
inline constexpr uint16_t kOpSchedClosed = 2411;  // ScheduleStageClosedAck (ToNear) [E: layout, no capture]
inline constexpr uint16_t kOpSchedChange = 2958;  // ChangeNPCScheduleMiseensceneAck (ToTarget)
inline constexpr uint16_t kOpSchedEvade = 2909;   // DeactivateMiseensceneScheduleEvadeObstacleAck (ToTarget)
// User 2's C>S MoveFieldCompleteReq: its client finished loading the field; the server answers with the field's
// actors (2005) and the 2144. Session 11 G1: 2549 t46.07, first NPC 2005 t46.12, 2144 t46.94 (after 545 records).
inline constexpr uint16_t kOpMoveFieldCompleteReq = 2549;

// Payload layouts (build 1.0.0.2976; scene_diag_logic.h, and the session 11 host capture H1 [V]):
//   2335 u8 kind | u32 actor @1 | u32 @5 | u32 resource @9 | u8 @13 | u64 key @14 | ...   (94..302 B)
//   2126 u32 actor | u64 key @4                                                            (12 B)
//   2411 u32 actor | u32 id (the key's low 32 bits)                                        (8 B) [E]
//   2958 / 2909 u8 kind | u32 actor @1 | u32 resource @5 | u64 key @9 | n x u32            (21/25/29 B)
//        (H1 R284867 2958 key 0x20000181 resource 0x8EFF20F5 = that key's 2335 at t50.47)
inline constexpr size_t kSchedCreateMin = 22;
inline constexpr size_t kSchedDeletePayload = 12;
inline constexpr size_t kSchedClosedPayload = 8;
inline constexpr size_t kSchedChangeMin = 17;

inline constexpr bool sched_op(uint16_t op) {
    return op == kOpSchedCreate || op == kOpSchedDelete || op == kOpSchedClosed || op == kOpSchedChange ||
           op == kOpSchedEvade;
}
// The kinds 0xA9FEE0 creates a ClientNPCScheduleStage for; another kind looks an existing one up.
inline constexpr bool sched_creates(uint8_t kind) { return kind == 1 || kind == 4; }
// The live forwards' counters: [0] 2126, [1] 2958, [2] 2909, [3] a 2335 that does not create (kind not 1/4: it
// updates an existing stage; none in the session 11 captures).
inline constexpr size_t kSchedLiveOps = 4;
inline constexpr int sched_live_index(uint16_t op, uint8_t kind = 0) {
    return op == kOpSchedDelete   ? 0
           : op == kOpSchedChange ? 1
           : op == kOpSchedEvade  ? 2
           : op == kOpSchedCreate && !sched_creates(kind) ? 3
                                                          : -1;
}
inline constexpr uint16_t kSchedLiveOp[kSchedLiveOps] = {kOpSchedDelete, kOpSchedChange, kOpSchedEvade, kOpSchedCreate};

struct SchedMsg {
    uint16_t op = 0;
    uint8_t kind = 0;        // 2335 / 2958 / 2909
    uint32_t actor = 0;
    uint32_t resource = 0;   // 2335 / 2958 / 2909
    uint64_t key = 0;        // 2335 / 2126 / 2958 / 2909
    uint32_t closed_id = 0;  // 2411
};
inline std::optional<SchedMsg> parse_sched(uint16_t op, std::span<const uint8_t> p) {
    SchedMsg m;
    m.op = op;
    switch (op) {
        case kOpSchedCreate:
            if (p.size() < kSchedCreateMin) return std::nullopt;
            m.kind = p[0];
            m.actor = le32(&p[1]);
            m.resource = le32(&p[9]);
            m.key = le64(&p[14]);
            return m;
        case kOpSchedDelete:
            if (p.size() != kSchedDeletePayload) return std::nullopt;
            m.actor = le32(&p[0]);
            m.key = le64(&p[4]);
            return m;
        case kOpSchedClosed:
            if (p.size() != kSchedClosedPayload) return std::nullopt;
            m.actor = le32(&p[0]);
            m.closed_id = le32(&p[4]);
            return m;
        case kOpSchedChange:
        case kOpSchedEvade:
            if (p.size() < kSchedChangeMin || (p.size() - kSchedChangeMin) % 4 != 0) return std::nullopt;
            m.kind = p[0];
            m.actor = le32(&p[1]);
            m.resource = le32(&p[5]);
            m.key = le64(&p[9]);
            return m;
        default:
            return std::nullopt;
    }
}

// One live stage of the host: the whole creating 2335 and its follow-ups, in order (fixed storage).
struct SchedEntry {
    static constexpr size_t kCreateBytes = 512;  // the whole 2335 packet (session 11: 99..307 B)
    static constexpr size_t kFollow = 6;         // 2958/2909/non-creating 2335 kept after it
    static constexpr size_t kFollowBytes = 160;  // each, whole packet (session 11: 26..34 B)

    bool used = false;
    bool follow_lost = false;  // a follow-up did not fit (dropped oldest / too big): the copy may lag the host
    uint8_t nfollow = 0;
    uint16_t create_len = 0;
    uint32_t actor = 0, resource = 0;
    uint64_t key = 0;
    uint64_t order = 0;  // the ledger's arrival order (the creating 2335's): also its generation (a new one per create)
    uint64_t t_us = 0;
    // Follow-ups stored since its 2335 (also the dropped oldest ones): follow[i] is follow-up number
    // follow_total - nfollow + i, so a copy can resume after a refusal even if the oldest went meanwhile.
    uint32_t follow_total = 0;
    uint16_t follow_op[kFollow]{};
    uint16_t follow_len[kFollow]{};
    uint8_t create[kCreateBytes]{};
    uint8_t follow[kFollow][kFollowBytes]{};

    std::span<const uint8_t> create_bytes() const { return {create, create_len}; }
    std::span<const uint8_t> follow_bytes(size_t i) const { return {follow[i], follow_len[i]}; }
};

enum class SchedSeen : uint8_t {
    Other,
    Created,        // a creating 2335 of a new key
    Recreated,      // a creating 2335 of a key already live: replaces it (its follow-ups go)
    Followed,       // a 2958/2909/non-creating 2335 kept after its key's 2335
    FollowUnknown,  // the same, of a key the ledger does not have
    Deleted,
    DeleteUnknown,
    Closed,
    CloseUnknown,   // no live key with that low half and actor
    NewField,       // the host's own MoveFieldAck: ledger emptied
    Full,
    TooBig,         // a creating 2335 over kCreateBytes: not kept (an older one of that key goes)
    Bad
};
inline const char* sched_seen_name(SchedSeen s) {
    switch (s) {
        case SchedSeen::Other: return "other";
        case SchedSeen::Created: return "created";
        case SchedSeen::Recreated: return "created again (replaces the older one)";
        case SchedSeen::Followed: return "kept after its 2335";
        case SchedSeen::FollowUnknown: return "of a key not in the ledger";
        case SchedSeen::Deleted: return "deleted";
        case SchedSeen::DeleteUnknown: return "deleted (not in the ledger)";
        case SchedSeen::Closed: return "closed";
        case SchedSeen::CloseUnknown: return "closed (no live key with that id and actor)";
        case SchedSeen::NewField: return "the host's MoveFieldAck: ledger emptied";
        case SchedSeen::Full: return "LEDGER FULL";
        case SchedSeen::TooBig: return "TOO BIG to keep";
        case SchedSeen::Bad: return "malformed";
        default: return "?";
    }
}

template <size_t N>
class SchedLedgerT {
public:
    static constexpr size_t kCapacity = N;

    // One S>C packet of the host's own client (whole). out: what it named.
    SchedSeen on_host_packet(std::span<const uint8_t> raw, uint64_t t_us, SchedMsg* out = nullptr) {
        const auto v = split(raw);
        if (!v) return SchedSeen::Other;
        if (v->op == kOpMoveField) {
            clear_entries();
            ++fields_;
            return SchedSeen::NewField;
        }
        if (!sched_op(v->op)) return SchedSeen::Other;
        const auto m = parse_sched(v->op, v->payload);
        if (!m) {
            ++bad_;
            return SchedSeen::Bad;
        }
        if (out) *out = *m;
        switch (v->op) {
            case kOpSchedCreate: {
                if (!sched_creates(m->kind)) return follow(*m, raw);
                SchedEntry* e = slot_of(m->key);
                const bool again = e != nullptr;
                if (raw.size() > SchedEntry::kCreateBytes) {
                    ++too_big_;
                    if (e) erase(*e);  // the older one would no longer be the host's state
                    return SchedSeen::TooBig;
                }
                if (!e) e = free_slot();
                if (!e) {
                    ++full_;
                    return SchedSeen::Full;
                }
                if (!again) ++live_;
                e->used = true;
                e->follow_lost = false;
                e->nfollow = 0;
                e->follow_total = 0;
                e->key = m->key;
                e->actor = m->actor;
                e->resource = m->resource;
                e->order = ++order_;
                e->t_us = t_us;
                e->create_len = static_cast<uint16_t>(raw.size());
                std::memcpy(e->create, raw.data(), raw.size());
                ++created_;
                return again ? SchedSeen::Recreated : SchedSeen::Created;
            }
            case kOpSchedChange:
            case kOpSchedEvade:
                return follow(*m, raw);
            case kOpSchedDelete:
                if (SchedEntry* e = slot_of(m->key)) {
                    erase(*e);
                    ++deleted_;
                    return SchedSeen::Deleted;
                }
                return SchedSeen::DeleteUnknown;
            case kOpSchedClosed: {
                bool hit = false, other_actor = false;
                for (SchedEntry& e : e_) {
                    if (!e.used || static_cast<uint32_t>(e.key) != m->closed_id) continue;
                    if (e.actor != m->actor) {
                        other_actor = true;  // the [E] layout's actor does not match: kept
                        continue;
                    }
                    erase(e);
                    hit = true;
                }
                if (hit) {
                    ++closed_;
                    return SchedSeen::Closed;
                }
                if (other_actor) ++closed_other_actor_;
                return SchedSeen::CloseUnknown;
            }
            default:
                return SchedSeen::Other;
        }
    }

    const SchedEntry* find(uint64_t key) const {
        for (const SchedEntry& e : e_) {
            if (e.used && e.key == key) return &e;
        }
        return nullptr;
    }
    // The live keys in the order their 2335 came (insertion sort into out; returns how many).
    size_t keys_in_order(uint64_t* out, size_t cap) const {
        uint64_t ord[N];
        size_t n = 0;
        for (const SchedEntry& e : e_) {
            if (!e.used || n >= cap) continue;
            size_t i = n++;
            while (i > 0 && ord[i - 1] > e.order) {
                ord[i] = ord[i - 1];
                out[i] = out[i - 1];
                --i;
            }
            ord[i] = e.order;
            out[i] = e.key;
        }
        return n;
    }
    // A new server instance: everything goes (the counters stay).
    void clear_all() { clear_entries(); }

    size_t live() const { return live_; }
    uint64_t created() const { return created_; }
    uint64_t followed() const { return followed_; }
    uint64_t follow_dropped() const { return follow_dropped_; }
    uint64_t follow_unknown() const { return follow_unknown_; }
    uint64_t deleted() const { return deleted_; }
    uint64_t closed() const { return closed_; }
    uint64_t closed_other_actor() const { return closed_other_actor_; }
    uint64_t full() const { return full_; }
    uint64_t too_big() const { return too_big_; }
    uint64_t bad() const { return bad_; }
    uint64_t fields() const { return fields_; }

private:
    SchedSeen follow(const SchedMsg& m, std::span<const uint8_t> raw) {
        SchedEntry* e = slot_of(m.key);
        if (!e) {
            ++follow_unknown_;
            return SchedSeen::FollowUnknown;
        }
        if (raw.size() > SchedEntry::kFollowBytes) {
            e->follow_lost = true;
            ++follow_dropped_;
            return SchedSeen::Followed;
        }
        if (e->nfollow >= SchedEntry::kFollow) {  // the oldest goes
            for (size_t i = 1; i < SchedEntry::kFollow; ++i) {
                e->follow_op[i - 1] = e->follow_op[i];
                e->follow_len[i - 1] = e->follow_len[i];
                std::memcpy(e->follow[i - 1], e->follow[i], SchedEntry::kFollowBytes);
            }
            --e->nfollow;
            e->follow_lost = true;
            ++follow_dropped_;
        }
        const size_t i = e->nfollow++;
        e->follow_op[i] = m.op;
        e->follow_len[i] = static_cast<uint16_t>(raw.size());
        std::memcpy(e->follow[i], raw.data(), raw.size());
        ++e->follow_total;
        ++followed_;
        return SchedSeen::Followed;
    }
    SchedEntry* slot_of(uint64_t key) {
        for (SchedEntry& e : e_) {
            if (e.used && e.key == key) return &e;
        }
        return nullptr;
    }
    SchedEntry* free_slot() {
        for (SchedEntry& e : e_) {
            if (!e.used) return &e;
        }
        return nullptr;
    }
    void erase(SchedEntry& e) {
        e.used = false;
        e.nfollow = 0;
        e.create_len = 0;
        --live_;
    }
    void clear_entries() {
        for (SchedEntry& e : e_) {
            e.used = false;
            e.nfollow = 0;
            e.create_len = 0;
        }
        live_ = 0;
    }

    SchedEntry e_[N];
    size_t live_ = 0;
    uint64_t order_ = 0, created_ = 0, followed_ = 0, follow_dropped_ = 0, follow_unknown_ = 0, deleted_ = 0,
             closed_ = 0, closed_other_actor_ = 0, full_ = 0, too_big_ = 0, bad_ = 0, fields_ = 0;
};

// What the trip did with a host schedule Ack the sniffer saw (SchedTripT::on_host_live).
enum class SchedLive : uint8_t {
    None,              // not an observer trip
    Forwarded,         // copied to user 2 (probe: would be)
    Refused,           // the queue refused it (LOST)
    NativeAlready,     // the same payload just went to user 2 natively: not repeated
    NotUser2,          // a key user 2 does not have
    CancelledPending,  // a 2126 of a key whose 2335 copy had not gone yet: the copy is cancelled
    KeptInPending,     // a 2958/2909 (or a 2335 again) of a pending key: it goes with that copy, in order
    Native2335,        // a host 2335 user 2 also got natively
    Queued2335,        // a creating 2335 user 2 did NOT get natively: pending, copied with the snapshot's gate
    Requeued2335,      // created again while user 2 has the key only by our copy: pending again (the new bytes)
    PendingFull        // a creating 2335 to copy, but the pending table is full (counted: not copied)
};
// What SchedTripT::on_user2_native did with a schedule Ack that reached user 2 natively (for the lines).
enum class SchedNative : uint8_t {
    None,             // not an observer trip, or not one of ours
    Recorded,         // remembered (a stage user 2 has, or a payload for the live dedup)
    DuplicateSame,    // the 2335 of a key whose copy had not gone yet, same bytes: it stands in for our copy
                      // (the ledger's follow-ups still go: 2958/2909 never arrive natively)
    DuplicateOther,   // the same, other bytes: our copy (and its follow-ups) is dropped
    NativeAfterCopy   // the 2335 of a key our copy already reached user 2 with (the guest refuses a 2nd create)
};
// What SchedTripT::release did with one pending key (for the lines).
struct SchedSent {
    enum Kind : uint8_t { Copied, FollowUps, Refused, Duplicate, Vanished } kind = Copied;
    uint64_t key = 0;
    uint32_t actor = 0, resource = 0;
    size_t follow = 0;          // follow-ups pushed now
    bool follow_lost = false;   // the ledger dropped follow-ups of it before they went (bound / too big)
    bool follow_wait = false;   // the queue refused a follow-up: the key stays pending from that one
    bool same_bytes = false;    // Duplicate: the native 2335 had exactly these bytes
    bool live = false;          // queued from a live 2335 of the host (not the trip's snapshot)
    size_t bytes = 0;
    uint64_t waited_us = 0;     // since it became pending
    uint64_t npc_before = 0;    // NPC (0xB) 2005 records already queued to user 2 in this trip when it went
};

// User 2's trip, for the schedule stages: the pending copies, the keys it has, the native packets just sent.
// H: keys user 2 has (native or copied); R: the native 2126/2958/2909 payload hashes waiting for their host copy.
//
// The gate (p8-impl-horarios-x.md §4.1: an NPC binds to its stage once, when its 2005 is unpacked, with no native
// retry): user 2 finished loading the field (its 2549 MoveFieldCompleteReq went to the server, or its 2144 to its
// queue: on_user2_loaded) AND the 2335's actor was announced to user 2. Not the 2661s' gate (2322 / 30 s): that
// opens after most NPC 2005s (session 11 G1: 545 of 653 NPC records before user 2's 2144).
template <size_t N, size_t H = 1024, size_t R = 64>
class SchedTripT {
public:
    static constexpr uint64_t kNativeTtlUs = 10'000'000;  // a native payload hash older than this matches nothing

    // Nothing of the old trip goes on (a new trip, a new epoch, user 2 gone).
    void clear() {
        active_ = observer_ = loaded_ = false;
        load_signal_ = 0;
        npc_announced_ = 0;
        npend_ = 0;
        for (Has& h : has_) h.used = false;
        nhas_ = 0;
        for (Native& r : ring_) r.used = false;
        trip_us_ = 0;
    }

    // User 2's MoveFieldAck went to its queue. observer: the 2661s' rule. exclude_actor: user 2's own character
    // (its stages are its own). Returns how many keys became pending (the ledger's order).
    size_t on_trip(bool observer, uint64_t now_us, const SchedLedgerT<N>& l, uint32_t exclude_actor = 0) {
        clear();
        active_ = true;
        observer_ = observer;
        trip_us_ = now_us;
        if (!observer) return 0;
        ++snapshots_;
        uint64_t keys[N];
        const size_t n = l.keys_in_order(keys, N);
        for (size_t i = 0; i < n; ++i) {
            const SchedEntry* e = l.find(keys[i]);
            if (!e) continue;
            if (exclude_actor && e->actor == exclude_actor) {
                ++excluded_;
                continue;
            }
            add_pending(keys[i], false, now_us);
        }
        snapshot_keys_ += npend_;
        return npend_;
    }

    // User 2 finished loading the field: signal 1 = its 2549 MoveFieldCompleteReq reached the server path,
    // 2 = its 2144 went to its queue. The first one opens the gate. True when it opened now.
    bool on_user2_loaded(uint8_t signal) {
        if (!observer() || loaded_) return false;
        loaded_ = true;
        load_signal_ = signal;
        return true;
    }
    // A 2005 went to user 2's queue in this trip with `npc_records` NPC (0xB) records (the measure of the lines).
    void on_user2_announced(size_t npc_records) {
        if (observer()) npc_announced_ += npc_records;
    }

    // take_frame: a schedule Ack went to user 2's queue natively (pushed) during its observer trip.
    SchedNative on_user2_native(uint16_t op, std::span<const uint8_t> payload, uint64_t now_us, const SchedLedgerT<N>& l) {
        if (!observer()) return SchedNative::None;
        const auto m = parse_sched(op, payload);
        if (!m) return SchedNative::None;
        switch (op) {
            case kOpSchedCreate: {
                ++native_[3];
                const uint64_t h = payload_hash(payload);
                if (!sched_creates(m->kind)) {
                    remember_native(op, h, now_us);
                    return SchedNative::Recorded;
                }
                Pending* p = find_pending(m->key);
                const Has* had = find_has(m->key);
                if ((p && p->create_sent) || (had && !had->native)) {
                    ++native_after_copy_;  // counted and said, not merged (the guest refuses the second create)
                    return SchedNative::NativeAfterCopy;
                }
                if (p) {  // our copy had not gone yet: the native one stands in for it
                    const SchedEntry* e = l.find(m->key);
                    add_has(m->key, true, h);
                    if (e && packet_payload_hash(e->create_bytes()) == h) {
                        ++dup_same_;
                        p->create_sent = true;  // its follow-ups still go (release), in order
                        p->gen = e->order;
                        p->next_follow = 0;
                        p->native_create = true;
                        return SchedNative::DuplicateSame;
                    }
                    ++dup_other_;
                    remove_pending(m->key);
                    return SchedNative::DuplicateOther;
                }
                add_has(m->key, true, h);
                return SchedNative::Recorded;
            }
            case kOpSchedDelete:
                ++native_[0];
                remove_has(m->key);
                remember_native(op, payload_hash(payload), now_us);
                return SchedNative::Recorded;
            case kOpSchedChange:
            case kOpSchedEvade:
                ++native_[op == kOpSchedChange ? 1 : 2];
                remember_native(op, payload_hash(payload), now_us);
                return SchedNative::Recorded;
            default:  // 2411: the guest closes it itself; its 2126 still has to come
                ++native_closed_;
                return SchedNative::Recorded;
        }
    }

    using Live = SchedLive;
    using Sent = SchedSent;
    // The sniffer: a schedule Ack of the host's client (whole packet `raw`), AFTER the ledger took it.
    template <class Push>
    Live on_host_live(const SchedMsg& m, std::span<const uint8_t> raw, uint64_t now_us, Push&& push) {
        if (!observer()) return Live::None;
        if (m.op == kOpSchedCreate && sched_creates(m.kind)) {
            if (has_native(m.key)) {
                ++live_2335_native_;
                return Live::Native2335;
            }
            if (find_pending(m.key)) return Live::KeptInPending;  // release() sees the new generation
            const bool by_copy = has(m.key);
            if (!add_pending(m.key, true, now_us)) {
                ++pending_full_;
                return Live::PendingFull;
            }
            ++(by_copy ? live_2335_requeued_ : live_2335_queued_);
            return by_copy ? Live::Requeued2335 : Live::Queued2335;
        }
        const int li = sched_live_index(m.op, m.kind);
        if (li < 0) return Live::None;
        const uint64_t h = packet_payload_hash(raw);
        if (consume_native(m.op, h, now_us)) {
            ++live_native_[li];
            return Live::NativeAlready;
        }
        if (Pending* p = find_pending(m.key)) {
            if (m.op != kOpSchedDelete) {  // the ledger has it after its 2335: it goes with the copy, in order
                ++live_kept_[li];
                return Live::KeptInPending;
            }
            const bool guest_has = p->create_sent || has(m.key);
            remove_pending(m.key);
            if (!guest_has) {
                ++cancelled_;
                return Live::CancelledPending;
            }
            // else: user 2 has an instance (our earlier copy): its 2126 goes
        }
        if (has(m.key)) {
            const bool ok = push(raw);
            if (m.op == kOpSchedDelete) remove_has(m.key);  // gone on the host either way
            if (!ok) {
                ++live_refused_[li];
                return Live::Refused;
            }
            ++live_fwd_[li];
            return Live::Forwarded;
        }
        ++live_not_user2_[li];
        return Live::NotUser2;
    }

    // Pushes the pending copies once user 2 finished loading: per key in order, its 2335 (once its actor was
    // announced to user 2: announced(actor)) and then its follow-ups. A refused 2335 or follow-up stops; the key
    // stays pending from that packet (no follow-up is lost to a refusal, none is sent twice). each(Sent).
    template <class Announced, class Push, class Each>
    size_t release(uint64_t now_us, const SchedLedgerT<N>& l, Announced&& announced, Push&& push, Each&& each) {
        if (!observer() || !loaded_ || !npend_) return 0;
        size_t sent = 0, w = 0;
        bool stop = false;
        for (size_t i = 0; i < npend_; ++i) {
            Pending p = pend_[i];
            if (stop) {
                pend_[w++] = p;
                continue;
            }
            const SchedEntry* e = l.find(p.key);
            Sent s;
            s.key = p.key;
            s.live = p.live;
            s.waited_us = now_us >= p.since_us ? now_us - p.since_us : 0;
            s.npc_before = npc_announced_;
            if (!e) {
                ++vanished_;
                s.kind = Sent::Vanished;
                each(s);
                continue;
            }
            s.actor = e->actor;
            s.resource = e->resource;
            if (p.create_sent && p.gen != e->order) {  // created again meanwhile: the new one goes from the start
                p.create_sent = p.native_create = false;
                p.next_follow = 0;
            }
            if (!p.create_sent) {
                if (const Has* hn = find_has(p.key); hn && hn->native) {  // (on_user2_native settles these first)
                    s.kind = Sent::Duplicate;
                    s.same_bytes = hn->hash == packet_payload_hash(e->create_bytes());
                    ++(s.same_bytes ? dup_same_ : dup_other_);
                    each(s);
                    continue;
                }
                if (!announced(e->actor)) {
                    pend_[w++] = p;
                    continue;
                }
                s.bytes = e->create_len;
                if (!push(e->create_bytes())) {
                    ++refused_;
                    s.kind = Sent::Refused;
                    each(s);
                    pend_[w++] = p;
                    stop = true;
                    continue;
                }
                p.create_sent = true;
                p.gen = e->order;
                p.next_follow = 0;
                add_has(p.key, false, 0);
                ++copied_;
                ++sent;
                s.kind = Sent::Copied;
                if (e->follow_lost) s.follow_lost = true;
            } else {
                s.kind = p.native_create ? Sent::Duplicate : Sent::FollowUps;
                s.same_bytes = p.native_create;
            }
            const uint32_t base = e->follow_total - e->nfollow;
            if (p.next_follow < base) {  // the ledger dropped some before they went
                s.follow_lost = true;
                p.next_follow = base;
            }
            for (; p.next_follow < e->follow_total; ++p.next_follow) {
                const std::span<const uint8_t> f = e->follow_bytes(p.next_follow - base);
                if (!push(f)) {
                    ++follow_refused_;
                    s.follow_wait = true;
                    stop = true;
                    break;
                }
                ++s.follow;
                s.bytes += f.size();
            }
            follow_copied_ += s.follow;
            if (s.follow_wait) pend_[w++] = p;  // resumes from the refused one
            if (s.kind == Sent::Copied || s.follow || s.follow_wait || s.follow_lost) each(s);
        }
        npend_ = w;
        return sent;
    }

    bool observer() const { return active_ && observer_; }
    bool active() const { return active_; }
    bool loaded() const { return loaded_; }
    uint8_t load_signal() const { return load_signal_; }
    uint64_t npc_announced() const { return npc_announced_; }
    size_t pending() const { return npend_; }
    bool pending(uint64_t key) const { return find_pending(key) != nullptr; }
    // A pending key whose 2335 already reached user 2 (ours or native) and that only waits for follow-ups.
    bool pending_create_sent(uint64_t key) const {
        const Pending* p = find_pending(key);
        return p && p->create_sent;
    }
    bool has(uint64_t key) const { return find_has(key) != nullptr; }
    bool has_native(uint64_t key) const {
        const Has* h = find_has(key);
        return h && h->native;
    }
    size_t has_count() const { return nhas_; }
    // The first pending key (for the lines).
    uint64_t first_pending() const { return npend_ ? pend_[0].key : 0; }

    struct Counters {
        uint64_t snapshots = 0, snapshot_keys = 0, excluded = 0, copied = 0, follow_copied = 0, follow_refused = 0,
                 dup_same = 0, dup_other = 0, vanished = 0, cancelled = 0, refused = 0, has_full = 0,
                 ring_overwritten = 0, live_2335_native = 0, live_2335_queued = 0, live_2335_requeued = 0,
                 pending_full = 0, native_after_copy = 0, native_closed = 0;
        uint64_t live_fwd[kSchedLiveOps]{}, live_native[kSchedLiveOps]{}, live_not_user2[kSchedLiveOps]{},
            live_kept[kSchedLiveOps]{}, live_refused[kSchedLiveOps]{};
        uint64_t native[4]{};  // what went to user 2 natively in its observer trips: 2126, 2958, 2909, 2335
    };
    Counters counters() const {
        Counters c;
        c.snapshots = snapshots_;
        c.snapshot_keys = snapshot_keys_;
        c.excluded = excluded_;
        c.copied = copied_;
        c.follow_copied = follow_copied_;
        c.follow_refused = follow_refused_;
        c.dup_same = dup_same_;
        c.dup_other = dup_other_;
        c.vanished = vanished_;
        c.cancelled = cancelled_;
        c.refused = refused_;
        c.has_full = has_full_;
        c.ring_overwritten = ring_overwritten_;
        c.live_2335_native = live_2335_native_;
        c.live_2335_queued = live_2335_queued_;
        c.live_2335_requeued = live_2335_requeued_;
        c.pending_full = pending_full_;
        c.native_after_copy = native_after_copy_;
        c.native_closed = native_closed_;
        for (size_t i = 0; i < kSchedLiveOps; ++i) {
            c.live_fwd[i] = live_fwd_[i];
            c.live_native[i] = live_native_[i];
            c.live_not_user2[i] = live_not_user2_[i];
            c.live_kept[i] = live_kept_[i];
            c.live_refused[i] = live_refused_[i];
        }
        for (size_t i = 0; i < 4; ++i) c.native[i] = native_[i];
        return c;
    }

private:
    struct Pending {
        uint64_t key = 0;
        uint64_t gen = 0;          // the ledger entry's order when its 2335 went (a re-create starts it again)
        uint64_t since_us = 0;
        uint32_t next_follow = 0;  // the next follow-up number to push (SchedEntry::follow_total numbering)
        bool create_sent = false;  // its 2335 reached user 2 (our copy, or the native one: native_create)
        bool native_create = false;
        bool live = false;         // from a live 2335 (not the snapshot)
    };
    struct Has {
        bool used = false;
        bool native = false;  // its 2335 reached user 2 natively (else: our copy)
        uint64_t key = 0;
        uint64_t hash = 0;    // the native 2335's payload hash
    };
    struct Native {
        bool used = false;
        uint16_t op = 0;
        uint64_t hash = 0;
        uint64_t t_us = 0;
    };
    Pending* find_pending(uint64_t key) {
        for (size_t i = 0; i < npend_; ++i) {
            if (pend_[i].key == key) return &pend_[i];
        }
        return nullptr;
    }
    const Pending* find_pending(uint64_t key) const {
        for (size_t i = 0; i < npend_; ++i) {
            if (pend_[i].key == key) return &pend_[i];
        }
        return nullptr;
    }
    bool add_pending(uint64_t key, bool live, uint64_t now_us) {
        if (npend_ >= N) return false;
        Pending p;
        p.key = key;
        p.live = live;
        p.since_us = now_us;
        pend_[npend_++] = p;
        return true;
    }
    void remove_pending(uint64_t key) {
        size_t w = 0;
        for (size_t i = 0; i < npend_; ++i) {
            if (pend_[i].key != key) pend_[w++] = pend_[i];
        }
        npend_ = w;
    }
    const Has* find_has(uint64_t key) const {
        for (const Has& h : has_) {
            if (h.used && h.key == key) return &h;
        }
        return nullptr;
    }
    void add_has(uint64_t key, bool native, uint64_t hash) {
        Has* free = nullptr;
        for (Has& h : has_) {
            if (h.used && h.key == key) {
                h.native = h.native || native;
                if (native) h.hash = hash;
                return;
            }
            if (!h.used && !free) free = &h;
        }
        if (!free) {
            ++has_full_;  // not tracked: its 2126/2958/2909 will not be copied (counted as "not user 2's")
            return;
        }
        *free = Has{true, native, key, hash};
        ++nhas_;
    }
    void remove_has(uint64_t key) {
        for (Has& h : has_) {
            if (h.used && h.key == key) {
                h.used = false;
                --nhas_;
                return;
            }
        }
    }
    void remember_native(uint16_t op, uint64_t hash, uint64_t now_us) {
        Native* slot = nullptr;
        for (Native& r : ring_) {
            if (!r.used || now_us > r.t_us + kNativeTtlUs) {
                slot = &r;
                break;
            }
        }
        if (!slot) {  // the oldest goes
            slot = &ring_[0];
            for (Native& r : ring_) {
                if (r.t_us < slot->t_us) slot = &r;
            }
            ++ring_overwritten_;
        }
        *slot = Native{true, op, hash, now_us};
    }
    // One native payload matches one host copy (two equal payloads can be two real events).
    bool consume_native(uint16_t op, uint64_t hash, uint64_t now_us) {
        for (Native& r : ring_) {
            if (r.used && r.op == op && r.hash == hash && now_us <= r.t_us + kNativeTtlUs) {
                r.used = false;
                return true;
            }
        }
        return false;
    }

    bool active_ = false, observer_ = false, loaded_ = false;
    uint8_t load_signal_ = 0;
    uint64_t trip_us_ = 0, npc_announced_ = 0;
    Pending pend_[N]{};
    size_t npend_ = 0;
    Has has_[H]{};
    size_t nhas_ = 0;
    Native ring_[R]{};
    uint64_t snapshots_ = 0, snapshot_keys_ = 0, excluded_ = 0, copied_ = 0, follow_copied_ = 0, follow_refused_ = 0,
             dup_same_ = 0, dup_other_ = 0, vanished_ = 0, cancelled_ = 0, refused_ = 0, has_full_ = 0,
             ring_overwritten_ = 0, live_2335_native_ = 0, live_2335_queued_ = 0, live_2335_requeued_ = 0,
             pending_full_ = 0, native_after_copy_ = 0, native_closed_ = 0;
    uint64_t live_fwd_[kSchedLiveOps]{}, live_native_[kSchedLiveOps]{}, live_not_user2_[kSchedLiveOps]{},
        live_kept_[kSchedLiveOps]{}, live_refused_[kSchedLiveOps]{};
    uint64_t native_[4]{};
};

inline constexpr size_t kSchedLedgerEntries = 256;  // session 11: 42/46 live before the entry
using SchedLedger = SchedLedgerT<kSchedLedgerEntries>;
using SchedTrip = SchedTripT<kSchedLedgerEntries>;

}  // namespace cdmp::session2::osq
