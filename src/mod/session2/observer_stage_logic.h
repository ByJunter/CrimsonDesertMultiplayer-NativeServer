#pragma once
// P8, user 2 as an OBSERVER of the stages/sequencers that already live in the
// field it enters (docs/research/pendientes/p8-observador-escenas.md). The pure
// logic of the host's [session2] observer_stage_sync=1 ledger, with no game
// access, so the tests can run it on the packets the captures kept.
//
// What it models (build 1.0.0.2976, p8-observador-escenas.md §2-§4):
//   PrepareSequencerAck 2661   serializer 0x2A4DEF0(actor, finder, play data, bindings): only caller
//                              0x287C200 (the server's sequencer preparation), ToTarget the sequencer
//                              owner's character. Wire payload: u8 kind | u32 actor | 86-byte play data
//                              (u32 owner actor +0, u64 sequencer id +8, ...) | n x 11-byte bindings
//                              (u32 node id, u32 actor key, 3 x u8).
//   DeleteSequencerAck 2810    serializer 0x2A4E4C0(actor, finder, &u64 seq, &u32, ...): u32 actor | u64
//                              sequencer id | u32 | u8.
//   StageBranchStateAck 2981   serializer 0x2A4F2D0(actor, finder, &u64 seq, &u32, ...), reached only from
//                              .xtls 0x103FB3FD (never called by us, its entry only observed).
//   PlayAllPreLoadSequencerAck 2322  emitter 0x2999D80(actor, finder): u32 actor key.
// The ledger keeps the live sequencers per (addressed actor, sequencer id): a
// 2661 adds or replaces one, a 2810 removes it, a 2981 records its last branch
// value. When a user enters a field it does not own, need() answers "which of
// the owner's live sequencers (and the actors they bind) never reached this
// user" - the snapshot an observer would need before its 2322.
//
// Observation only: nothing here decides for the game or sends anything.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cdmp::session2::obs {

inline constexpr uint16_t kOpPrepare = 2661;    // PrepareSequencerAck
inline constexpr uint16_t kOpDelete = 2810;     // DeleteSequencerAck
inline constexpr uint16_t kOpBranch = 2981;     // StageBranchStateAck
inline constexpr uint16_t kOpMilestone = 2322;  // PlayAllPreLoadSequencerAck

inline constexpr size_t kHeader = 5;          // u16 opcode | i8 slot | u16 payload length
inline constexpr size_t kPreparePrefix = 91;  // u8 kind | u32 actor | 86-byte play data
inline constexpr size_t kBindingWire = 11;    // u32 node | u32 actor | 3 x u8
inline constexpr size_t kDeletePayload = 17;  // u32 actor | u64 seq | u32 | u8
inline constexpr size_t kMilestonePayload = 4;
inline constexpr size_t kMaxBindings = 64;    // per sequencer kept by the ledger (the rest counted)

struct Binding {
    uint32_t node = 0;
    uint32_t actor = 0;
    bool operator==(const Binding&) const = default;
};

inline uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}
inline uint64_t le64(const uint8_t* p) { return static_cast<uint64_t>(le32(p)) | static_cast<uint64_t>(le32(p + 4)) << 32; }

// A whole Troc packet as the sniffer keeps it: the opcode and its payload, if
// the length field matches the bytes.
struct Packet {
    uint16_t opcode = 0;
    std::span<const uint8_t> payload;
};
inline std::optional<Packet> split_packet(std::span<const uint8_t> raw) {
    if (raw.size() < kHeader) return std::nullopt;
    const uint16_t len = static_cast<uint16_t>(raw[3] | raw[4] << 8);
    if (raw.size() != kHeader + len) return std::nullopt;
    return Packet{static_cast<uint16_t>(raw[0] | raw[1] << 8), raw.subspan(kHeader)};
}

// The wire size of a 2661 with n bindings (the rule all 19 of the host's own
// capture follow; p8-observador-escenas.md §3.2).
constexpr size_t prepare_wire_size(size_t bindings) { return kHeader + kPreparePrefix + kBindingWire * bindings; }

struct Prepare {
    uint8_t kind = 0;
    uint32_t actor = 0;        // the actor the packet is addressed to (0x2A4DEF0's rcx, [rcx+0x60])
    uint32_t owner_actor = 0;  // play data +0
    uint64_t seq = 0;          // play data +8
    std::vector<Binding> bindings;
};
inline std::optional<Prepare> parse_prepare_payload(std::span<const uint8_t> p) {
    if (p.size() < kPreparePrefix || (p.size() - kPreparePrefix) % kBindingWire != 0) return std::nullopt;
    Prepare r;
    r.kind = p[0];
    r.actor = le32(&p[1]);
    r.owner_actor = le32(&p[5]);
    r.seq = le64(&p[9]);
    const size_t n = (p.size() - kPreparePrefix) / kBindingWire;
    r.bindings.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* b = &p[kPreparePrefix + i * kBindingWire];
        r.bindings.push_back({le32(b), le32(b + 4)});
    }
    return r;
}

struct Delete {
    uint32_t actor = 0;
    uint64_t seq = 0;
    uint32_t extra = 0;
};
inline std::optional<Delete> parse_delete_payload(std::span<const uint8_t> p) {
    if (p.size() != kDeletePayload) return std::nullopt;
    return Delete{le32(&p[0]), le64(&p[4]), le32(&p[12])};
}

inline std::optional<uint32_t> parse_milestone_payload(std::span<const uint8_t> p) {
    if (p.size() != kMilestonePayload) return std::nullopt;
    return le32(p.data());
}

// ---------------------------------------------------------------- the ledger

struct SeqEntry {
    uint32_t actor = 0;        // addressed actor (the owner's character for the native 2661)
    uint32_t user = 0;         // that actor's user key ([[actor+0xA0]+0x60]); 0 = none
    uint32_t owner_actor = 0;  // play data +0
    uint64_t seq = 0;
    uint64_t t_us = 0;
    std::vector<Binding> bindings;
    uint32_t bindings_total = 0;  // as the game had them (bindings keeps at most kMaxBindings)
    bool targeted = true;         // the finder was a ToTargetSendTargetFinder
    bool has_branch = false;
    uint32_t branch_value = 0;
    uint64_t branch_t_us = 0;
    uint32_t branches = 0;
};

struct ObserverNeed {
    size_t owner_live = 0;  // live sequencers prepared for a character of the field owner's user
    size_t delivered = 0;   // of those, the same (owner actor, seq) also prepared for the observer's user
    std::vector<const SeqEntry*> missing;  // sorted by sequencer id
    std::vector<uint32_t> bound_actors;    // unique, sorted; the owner's own characters excluded
    uint32_t bindings_unknown = 0;         // bindings the ledger did not keep (over kMaxBindings)
};

class Ledger {
public:
    explicit Ledger(size_t capacity = 1024) : cap_(capacity) {}

    // A 2661 left the server. Replaces the entry of the same (actor, seq).
    // False (and counted) when the ledger is full.
    bool prepare(SeqEntry e) {
        if (e.bindings.size() > kMaxBindings) e.bindings.resize(kMaxBindings);
        if (e.bindings_total < e.bindings.size()) e.bindings_total = static_cast<uint32_t>(e.bindings.size());
        ++prepared_;
        if (SeqEntry* cur = find(e.actor, e.seq)) {
            ++replaced_;
            const bool hb = cur->has_branch;
            const uint32_t bv = cur->branch_value, bn = cur->branches;
            const uint64_t bt = cur->branch_t_us;
            *cur = std::move(e);
            if (!cur->has_branch && hb) {  // a re-prepare keeps the branch already seen
                cur->has_branch = true;
                cur->branch_value = bv;
                cur->branch_t_us = bt;
                cur->branches = bn;
            }
            return true;
        }
        if (entries_.size() >= cap_) {
            ++full_;
            return false;
        }
        entries_.push_back(std::move(e));
        return true;
    }

    // A 2810 left the server: the sequencer (actor, seq) is gone.
    bool remove(uint32_t actor, uint64_t seq) {
        ++deleted_;
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].actor == actor && entries_[i].seq == seq) {
                entries_[i] = std::move(entries_.back());
                entries_.pop_back();
                return true;
            }
        }
        ++delete_unknown_;
        return false;
    }

    // A 2981 for (actor, seq): its last value, when the sequencer is live.
    bool branch(uint32_t actor, uint64_t seq, uint32_t value, uint64_t t_us) {
        ++branches_;
        if (SeqEntry* cur = find(actor, seq)) {
            cur->has_branch = true;
            cur->branch_value = value;
            cur->branch_t_us = t_us;
            ++cur->branches;
            return true;
        }
        ++branch_unknown_;
        return false;
    }

    // What an observer (a user entering a field owned by owner_user) has not
    // received of the owner's live sequencers. `owner_actors` are the owner's
    // own characters (never counted as bound NPCs).
    ObserverNeed need(uint32_t owner_user, uint32_t observer_user, std::span<const uint32_t> owner_actors = {}) const {
        ObserverNeed n;
        if (!owner_user) return n;
        for (const SeqEntry& e : entries_) {
            if (e.user != owner_user) continue;
            ++n.owner_live;
            const bool got = observer_user && std::any_of(entries_.begin(), entries_.end(), [&](const SeqEntry& o) {
                                 return o.user == observer_user && o.owner_actor == e.owner_actor && o.seq == e.seq;
                             });
            if (got) {
                ++n.delivered;
                continue;
            }
            n.missing.push_back(&e);
            n.bindings_unknown += e.bindings_total - static_cast<uint32_t>(e.bindings.size());
            for (const Binding& b : e.bindings) {
                const bool own = b.actor == e.actor || b.actor == e.owner_actor ||
                                 std::find(owner_actors.begin(), owner_actors.end(), b.actor) != owner_actors.end();
                if (b.actor && !own) n.bound_actors.push_back(b.actor);
            }
        }
        std::sort(n.missing.begin(), n.missing.end(), [](const SeqEntry* a, const SeqEntry* b) {
            return a->seq != b->seq ? a->seq < b->seq : a->actor < b->actor;
        });
        std::sort(n.bound_actors.begin(), n.bound_actors.end());
        n.bound_actors.erase(std::unique(n.bound_actors.begin(), n.bound_actors.end()), n.bound_actors.end());
        return n;
    }

    // The user completed an entry into a field it OWNS at `t_us`: what was
    // prepared for it before belonged to the field it left (its client resets
    // the manager on the change, 0xA94660 clears the +0x170 map) and is no
    // longer part of any observer's snapshot. Returns how many were dropped.
    size_t drop_user_before(uint32_t user, uint64_t t_us) {
        if (!user) return 0;
        const size_t before = entries_.size();
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                      [&](const SeqEntry& e) { return e.user == user && e.t_us < t_us; }),
                       entries_.end());
        const size_t n = before - entries_.size();
        invalidated_ += n;
        return n;
    }

    const SeqEntry* get(uint32_t actor, uint64_t seq) const {
        for (const SeqEntry& e : entries_) {
            if (e.actor == actor && e.seq == seq) return &e;
        }
        return nullptr;
    }
    size_t live() const { return entries_.size(); }
    uint64_t prepared() const { return prepared_; }
    uint64_t replaced() const { return replaced_; }
    uint64_t deleted() const { return deleted_; }
    uint64_t delete_unknown() const { return delete_unknown_; }
    uint64_t branches() const { return branches_; }
    uint64_t branch_unknown() const { return branch_unknown_; }
    uint64_t full() const { return full_; }
    uint64_t invalidated() const { return invalidated_; }

private:
    SeqEntry* find(uint32_t actor, uint64_t seq) {
        for (SeqEntry& e : entries_) {
            if (e.actor == actor && e.seq == seq) return &e;
        }
        return nullptr;
    }

    size_t cap_;
    std::vector<SeqEntry> entries_;
    uint64_t prepared_ = 0, replaced_ = 0, deleted_ = 0, delete_unknown_ = 0, branches_ = 0, branch_unknown_ = 0,
             full_ = 0, invalidated_ = 0;
};

// The order the owner's client received what an observer is missing (host
// capture, every pair: 2981 then its 2661 in the same millisecond, sequencers
// in preparation order), then the milestone. Printed in the PLAN line; nothing
// is sent (p8-observador-escenas.md "Fase 2": the replay is not coherent).
struct PlanStep {
    uint16_t opcode = 0;
    uint64_t seq = 0;
    uint64_t t_us = 0;
};
inline std::vector<PlanStep> native_order(const ObserverNeed& n) {
    std::vector<const SeqEntry*> by_time(n.missing.begin(), n.missing.end());
    std::stable_sort(by_time.begin(), by_time.end(),
                     [](const SeqEntry* a, const SeqEntry* b) { return a->t_us < b->t_us; });
    std::vector<PlanStep> out;
    for (const SeqEntry* e : by_time) {
        if (e->has_branch) out.push_back({kOpBranch, e->seq, e->branch_t_us});
        out.push_back({kOpPrepare, e->seq, e->t_us});
    }
    out.push_back({kOpMilestone, 0, 0});
    return out;
}

// ---------------------------------------------------------------- after an observer's entry

// What reached the observer's user after it entered a field it does not own,
// counted by the addressed actor's user (the native finders of these four
// packets are ToTarget that actor's session, except 2981, whose finder kind is
// recorded separately).
struct EntryWatch {
    uint32_t observer_user = 0;
    uint32_t observer_actor = 0;
    uint32_t owner_user = 0;
    uint64_t t_us = 0;
    uint32_t prepares = 0, deletes = 0, branches = 0, milestones = 0;
    uint32_t owner_branches_untargeted = 0;  // 2981 of the owner's sequencers with a non-ToTarget finder
    uint8_t reported = 0;                    // bit i: follow-up i written

    void count(uint16_t opcode, uint32_t addressed_user, bool targeted) {
        if (addressed_user == observer_user) {
            if (opcode == kOpPrepare) ++prepares;
            if (opcode == kOpDelete) ++deletes;
            if (opcode == kOpBranch) ++branches;
            if (opcode == kOpMilestone) ++milestones;
        } else if (opcode == kOpBranch && !targeted && addressed_user == owner_user) {
            ++owner_branches_untargeted;
        }
    }
};

// The follow-ups after an entry: +10 s, +60 s, +300 s. Returns the index of the
// first one due and not written yet, or -1.
inline constexpr uint64_t kFollowUpUs[] = {10'000'000, 60'000'000, 300'000'000};
inline int follow_up_due(const EntryWatch& w, uint64_t now_us) {
    for (int i = 0; i < 3; ++i) {
        if (w.reported & (1u << i)) continue;
        if (now_us >= w.t_us + kFollowUpUs[i]) return i;
        return -1;
    }
    return -1;
}

// observer_stage_sync's value: 0 off, 1 measure. Anything above asks for the
// send mode, which is NOT implemented: phase 1 found no native snapshot path
// (p8-observador-escenas.md §5) and phase 2 found that replaying the owner's
// exact 2661/2981 to user 2 is not coherent (its client routes every stage Ack
// to the manager of its field's owner = user 2's own character, never to the
// host character's manager its NPCs search; "Fase 2"). It runs as 1.
enum class Mode : uint8_t { Off, Measure };
struct ModeChoice {
    Mode mode = Mode::Off;
    bool send_refused = false;
};
constexpr ModeChoice choose_mode(uint32_t ini_value) {
    if (ini_value == 0) return {Mode::Off, false};
    return {Mode::Measure, ini_value >= 2};
}

}  // namespace cdmp::session2::obs
