#pragma once
// S46 phase 1, T2 in probe (docs/research/s25/s46-tenderos.md; tenderos.md §2.1, revision-diseno.md): the pure logic of
// [session2] observer_hold_scene_order = 0 | probe | 1 (HOST; "1" = T2 ACTIVE since phase 3, SceneHold below; needs observer_hold_acks=1).
// No game access: the tests run it on synthetic packets and on the real layouts.
//
// The fault [V] (p2b): observer_hold withholds the 2005 of an NPC from user 2 until its controller acts (344 / 375 ms
// for Renee and Rhett), but user 2's OWN 2661 that binds that NPC (types 4/5) goes at once: user 2's client creates
// the scene before the NPC exists and drops it (0xA9CA40, 0x3B331F28). Natively the 2005 comes 0.1-0.9 ms BEFORE the
// 2661 (34 of 34 shop scenes measured). T2 would keep such a 2661, and every later packet with its scene id, right
// behind the 2005s that announce its held NPCs. In probe nothing is kept: '[would hold scene]' says which scene, which
// keys, how long and which opcodes it would have held.
//
// Scope: user 2's own 2661 (owner @5 = user 2's character) of a trade scene (resource hash @27 in the trade list:
// smithy 0xA7132561, butcher 0x978345CA) binding at least one key observer_hold_acks holds now.
// Scene-id packets to user 2 (layouts read by the client's readers 0xC073A0 / 0xC07D00, ported from the other AI's
// guest_shop_order_logic.h): 2981 = 21 + 8 n (id @5), 2524 = 18 + 11 n (id @5), 2810 = 17 (id @4), 2661 (id @9).
// The 2005s are read as take_frame queues them (after observer_hold_acks' compaction, possibly batched: every record).
// Field boundaries for user 2 (the same port): 2174 = 17 + 48 n (actor @1), 2643 = 20 + 48 n (actor @1, append @19 =
// 0), 2756 (actor @0).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "mod/session2/funcnpc_trade_policy.h"

namespace cdmp::session2::hso {

// S46 phase 3: "1" is T2 ACTIVE (SceneHold below, inside observer_hold_acks' route and lock).
enum class Mode : uint8_t { Off, Probe, On };
struct Key {
    Mode mode = Mode::Off;
    bool asked_on = false;  // "1"
    bool bad = false;
};
inline Key parse_key(std::string_view v) {
    Key k;
    if (v.empty() || v == "0") return k;
    if (v == "probe" || v == "1") {
        k.mode = v == "1" ? Mode::On : Mode::Probe;
        k.asked_on = v == "1";
        return k;
    }
    k.bad = true;
    return k;
}

inline constexpr uint16_t kOpPrepare = 2661, kOpBranch = 2981, kOpOnBranch = 2524, kOpDelete = 2810,
                          kOpAnnounce = 2005, kOpRemove = 2516, kOpMoveField = 2174, kOpLoadingField = 2643,
                          kOpBoundary2756 = 2756,
                          // S46 phase 3: UpdateStageChartEventAck (ToNear, 0x2936BF3): 22 B, u8 kind | u32 actor @1 |
                          // u64 sequencer id @5 | ... [V p2b captures: guest t=2538.134 ids 0x115..0x117, the same ids
                          // as the 2981s @5 and the 2661s @9 of those scenes].
                          kOpChartEvent = 2111;

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

// 2661: u8 kind | u32 actor @1 | 86-byte play data @5 (owner @5, sequencer @9, resource hash @27) | n x 11 bytes of
// binding @91 (u32 node | u32 actor | u8 type | u8 | u8).
inline constexpr size_t kPreparePrefix = 91, kBindingWire = 11;
struct Prepare {
    bool ok = false;
    uint32_t owner = 0, hash = 0;
    uint64_t id = 0;
    std::vector<uint32_t> bound;  // types 4 / 5 (what 0xA9CA40 needs before it creates the scene), unique, in order
};
inline Prepare parse_prepare(std::span<const uint8_t> p) {
    Prepare r;
    if (p.size() < kPreparePrefix || (p.size() - kPreparePrefix) % kBindingWire != 0) return r;
    r.ok = true;
    r.owner = le32(&p[5]);
    r.id = le64(&p[9]);
    r.hash = le32(&p[27]);
    for (size_t at = kPreparePrefix; at + kBindingWire <= p.size(); at += kBindingWire) {
        const uint32_t actor = le32(&p[at + 4]);
        const uint8_t type = p[at + 8];
        if ((type == 4 || type == 5) && actor && std::find(r.bound.begin(), r.bound.end(), actor) == r.bound.end()) {
            r.bound.push_back(actor);
        }
    }
    return r;
}
// The scene id a packet to user 2 carries (only well-formed layouts; a truncated 17-byte prefix of a 2981 is not one).
inline std::optional<uint64_t> scene_id(uint16_t op, std::span<const uint8_t> p) {
    switch (op) {
        case kOpBranch:
            if (p.size() >= 21 && (p.size() - 21) % 8 == 0) return le64(&p[5]);
            return std::nullopt;
        case kOpOnBranch:
            if (p.size() >= 18 && (p.size() - 18) % 11 == 0) return le64(&p[5]);
            return std::nullopt;
        case kOpDelete:
            if (p.size() == 17) return le64(&p[4]);
            return std::nullopt;
        default:
            return std::nullopt;
    }
}
// T2 active also keeps the 2111 of a held scene (the probe's scene_id stays as it was). Review B2: a 2810 of another
// length than 17 still carries its id @4 (u32 owner | u64 id ...): T2 reads it from 12 bytes on, so that a kept or
// tombstoned scene's delete is never missed (today every 2810 is 17 B: osc::kDeletePayload).
inline std::optional<uint64_t> scene_id_t2(uint16_t op, std::span<const uint8_t> p) {
    if (op == kOpChartEvent) {
        if (p.size() == 22) return le64(&p[5]);
        return std::nullopt;
    }
    if (op == kOpDelete) {
        if (p.size() >= 12) return le64(&p[4]);
        return std::nullopt;
    }
    return scene_id(op, p);
}
// The actor keys a 2005 announces: records [u16 len][u8 type][u32 key]...; type 0 is the user record.
inline void announce_keys(std::span<const uint8_t> p, std::vector<uint32_t>& out) {
    size_t i = 0;
    while (i + 2 <= p.size()) {
        const uint16_t len = static_cast<uint16_t>(p[i] | p[i + 1] << 8);
        if (len == 0 || i + 2 + len > p.size()) break;
        if (len >= 5 && p[i + 2] != 0) out.push_back(le32(&p[i + 3]));
        i += 2 + len;
    }
}
// RemoveActorAck 2516: u8 | u32 | u8 reason | n x u32 key.
inline void removed_keys(std::span<const uint8_t> p, std::vector<uint32_t>& out) {
    if (p.size() < 10 || (p.size() - 6) % 4 != 0) return;
    for (size_t i = 6; i + 4 <= p.size(); i += 4) out.push_back(le32(&p[i]));
}
// A field boundary of user 2's character (`own`): every scene of the old field is gone on its client.
inline bool field_boundary(uint16_t op, std::span<const uint8_t> p, uint32_t own) {
    if (!own) return false;
    if (op == kOpBoundary2756) return p.size() >= 4 && le32(p.data()) == own;
    if (op == kOpMoveField) return p.size() >= 17 && (p.size() - 17) % 48 == 0 && le32(p.data() + 1) == own;
    return op == kOpLoadingField && p.size() >= 20 && (p.size() - 20) % 48 == 0 && le32(p.data() + 1) == own &&
           p[19] == 0;
}

enum class Kind : uint8_t {
    WouldHold,  // the 2661 would be kept: its pending keys
    Released,   // the last pending key's 2005 went: the delay, the opcodes it would have held (in order)
    Cancelled,  // a 2810 of the scene while held: the scene would be dropped with what it held (as the client would)
    Dropped,    // a pending key removed (2516) or user 2's field changed: released in order, the client rejects it
    Expired,    // 40 s without its 2005s: released in order
    Capacity,   // too many scenes / opcodes: released at once (the behaviour without the key)
    NoHold,     // user 2's own trade scene with none of its bound keys held: it would go at once (natively ordered)
    KeyGone,    // T2 active: observer_hold_acks' hold of a pending key ended without its 2005 (drop, expiry, retire)
    Count
};
inline const char* kind_name(Kind k) {
    switch (k) {
        case Kind::WouldHold: return "would hold scene";
        case Kind::Released: return "would release scene";
        case Kind::Cancelled: return "would cancel scene (2810)";
        case Kind::Dropped: return "would release scene (dropped)";
        case Kind::Expired: return "would release scene (40 s)";
        case Kind::Capacity: return "would release scene (capacity)";
        case Kind::NoHold: return "own trade scene, nothing held";
        case Kind::KeyGone: return "scene discarded (its NPC's hold ended without a 2005)";
        default: return "?";
    }
}
struct Event {
    bool active = false;  // T2 active (SceneHold): the packets were really kept / released / discarded
    uint8_t reason = 0xFF;  // KeyGone (review M1): observer_hold_acks' oha::Discard for the key (0xFF: not given)
    Kind kind = Kind::WouldHold;
    uint64_t id = 0;
    uint32_t hash = 0;
    std::vector<uint32_t> keys;  // WouldHold: the pending keys; Dropped: the key removed (or none: a boundary)
    uint64_t delay_us = 0;
    std::vector<uint16_t> ops;   // the opcodes it would have held, in order (the 2661 first)
    uint32_t more_ops = 0;       // past the bound, counted only
};

struct Limits {
    size_t max_scenes = 32, max_ops = 64;
    uint64_t max_hold_us = 40'000'000;
};

class SceneOrder {
public:
    explicit SceneOrder(Limits l = {}) : l_(l) {}

    // One packet to user 2's live session, as queued (pushed). own = user 2's character key; holding(key) -> bool: does
    // observer_hold_acks hold that key now. Events go to `out`.
    template <class Holding>
    void feed(uint16_t op, std::span<const uint8_t> p, uint64_t now_us, uint32_t own, Holding&& holding,
              std::vector<Event>& out) {
        if (field_boundary(op, p, own)) {
            for (Watch& w : w_) {
                Event e = end(w, Kind::Dropped, now_us);
                e.keys.clear();  // no key removed: the field changed
                out.push_back(std::move(e));
            }
            w_.clear();
            return;
        }
        if (op == kOpPrepare) {
            const Prepare pr = parse_prepare(p);
            if (!pr.ok || !own || pr.owner != own || !u2trade::is_trade_hash(pr.hash)) return;
            std::vector<uint32_t> pending;
            for (const uint32_t k : pr.bound) {
                if (holding(k)) pending.push_back(k);
            }
            if (pending.empty()) {
                Event e;
                e.kind = Kind::NoHold;
                e.id = pr.id;
                e.hash = pr.hash;
                e.keys = pr.bound;
                out.push_back(std::move(e));
                return;
            }
            if (find(pr.id)) return;  // the same id again while held: already watched
            if (w_.size() >= l_.max_scenes) {
                Event e;
                e.kind = Kind::Capacity;
                e.id = pr.id;
                e.hash = pr.hash;
                e.keys = pending;
                out.push_back(std::move(e));
                return;
            }
            Watch w;
            w.id = pr.id;
            w.hash = pr.hash;
            w.pending = pending;
            w.since_us = now_us;
            w.ops.push_back(op);
            Event e;
            e.kind = Kind::WouldHold;
            e.id = pr.id;
            e.hash = pr.hash;
            e.keys = pending;
            e.ops = w.ops;
            out.push_back(std::move(e));
            w_.push_back(std::move(w));
            return;
        }
        if (w_.empty()) return;
        if (op == kOpAnnounce) {
            std::vector<uint32_t> keys;
            announce_keys(p, keys);
            for (size_t i = 0; i < w_.size();) {
                Watch& w = w_[i];
                for (const uint32_t k : keys) w.pending.erase(std::remove(w.pending.begin(), w.pending.end(), k), w.pending.end());
                if (w.pending.empty()) {
                    out.push_back(end(w, Kind::Released, now_us));
                    w_.erase(w_.begin() + static_cast<std::ptrdiff_t>(i));
                } else {
                    ++i;
                }
            }
            return;
        }
        if (op == kOpRemove) {
            std::vector<uint32_t> keys;
            removed_keys(p, keys);
            for (size_t i = 0; i < w_.size();) {
                Watch& w = w_[i];
                uint32_t gone = 0;
                for (const uint32_t k : keys) {
                    if (std::find(w.pending.begin(), w.pending.end(), k) != w.pending.end()) gone = k;
                }
                if (gone) {
                    Event e = end(w, Kind::Dropped, now_us);
                    e.keys = {gone};
                    out.push_back(std::move(e));
                    w_.erase(w_.begin() + static_cast<std::ptrdiff_t>(i));
                } else {
                    ++i;
                }
            }
            return;
        }
        const auto id = scene_id(op, p);
        if (!id) return;
        Watch* w = find(*id);
        if (!w) return;
        if (w->ops.size() < l_.max_ops) {
            w->ops.push_back(op);
        } else {
            ++w->more_ops;
        }
        if (op == kOpDelete) {
            out.push_back(end(*w, Kind::Cancelled, now_us));
            erase(*id);
        }
    }

    // Holds older than max_hold_us: released in order (the client rejects the scene as today).
    void tick(uint64_t now_us, std::vector<Event>& out) {
        for (size_t i = 0; i < w_.size();) {
            if (now_us - w_[i].since_us >= l_.max_hold_us) {
                out.push_back(end(w_[i], Kind::Expired, now_us));
                w_.erase(w_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
    }
    // User 2 gone / a new epoch: nothing to release (nobody to send to).
    void clear() { w_.clear(); }
    size_t size() const { return w_.size(); }

private:
    struct Watch {
        uint64_t id = 0;
        uint32_t hash = 0;
        std::vector<uint32_t> pending;
        uint64_t since_us = 0;
        std::vector<uint16_t> ops;
        uint32_t more_ops = 0;
    };
    Watch* find(uint64_t id) {
        for (Watch& w : w_) {
            if (w.id == id) return &w;
        }
        return nullptr;
    }
    void erase(uint64_t id) {
        w_.erase(std::remove_if(w_.begin(), w_.end(), [&](const Watch& w) { return w.id == id; }), w_.end());
    }
    static Event end(const Watch& w, Kind k, uint64_t now_us) {
        Event e;
        e.kind = k;
        e.id = w.id;
        e.hash = w.hash;
        e.keys = w.pending;
        e.delay_us = now_us >= w.since_us ? now_us - w.since_us : 0;
        e.ops = w.ops;
        e.more_ops = w.more_ops;
        return e;
    }

    Limits l_;
    std::vector<Watch> w_;
};

// ---------------------------------------------------------------- S46 phase 3: T2 ACTIVE
//
// observer_hold_scene_order=1 (HOST; needs observer_hold_acks=1). SceneHold lives inside observer_hold_acks' route,
// under ITS lock (observer_hold_acks.cpp g_mu), so that a scene's packets and the 2005s they wait for are queued in one
// atomic order, as the 'Pushed' of observer_hold_acks are. No game call, no game memory: packets only.
//   - user 2's OWN 2661 (owner @5 = user 2's character) of a trade scene (hash @27 on the list) that binds (types 4/5)
//     a key observer_hold_acks holds now: KEPT, with its pending keys;
//   - then every packet to user 2 with that scene id (2981 @5, 2524 @5, 2111 @5, 2810 @4; held_seq order) is kept
//     behind it;
//   - the 2005 (as queued: after the compaction, batched: every record) that announces the LAST pending key queues,
//     right after itself and observer_hold_acks' own packets, the scene's packets in their original order (one
//     sequence shared with observer_hold_acks: `next_seq`);
//   - a 2810 of a kept scene discards it with all it kept (and the 2810 itself: user 2's client never had it);
//   - any other end discards the scene and what it kept, and tombstones its id (later packets of that id, up to its
//     2810, are dropped too): the hold of a pending key ended without its 2005 (key_gone: drop, expiry, retirement,
//     user 2 gone), a 2516 of a pending key, a field boundary of user 2, the 100 s bound (= observer_hold_acks'), and
//     the CAP (scenes, packets or bytes per scene, bytes in all): at the cap the 2661 (or the scene) is REFUSED and
//     discarded together with its packets. Natively the client would have rejected such a scene anyway (0x3B331F28,
//     its NPC not there): discarding is the same outcome without the packets of an id it does not know.
enum class Take : uint8_t { Pass, Held, Discarded };
struct HoldLimits {
    size_t max_scenes = 32, max_packets = 64, max_bytes_scene = 64 * 1024, max_bytes_total = 1u << 20, max_tombs = 64;
    uint64_t max_hold_us = 100'000'000;  // observer_hold_acks' Limits::max_hold_ms
    uint64_t tomb_us = 120'000'000;
};
struct OutPacket {
    uint64_t seq = 0;
    uint16_t op = 0;
    std::vector<uint8_t> bytes;
};
struct HoldCounters {
    uint64_t own_2661 = 0, held = 0, released = 0, packets_held = 0, packets_released = 0, nohold = 0;
    uint64_t discarded[static_cast<size_t>(Kind::Count)] = {};  // by why (Cancelled, Dropped, Expired, Capacity, KeyGone)
    uint64_t packets_discarded = 0, tomb_dropped = 0, delay_max_us = 0, peak_scenes = 0, peak_bytes = 0;
    uint64_t odd_2810 = 0;       // review B2: a 2810 not 17 B long that matched a kept / tombstoned id
    uint64_t tomb_reopened = 0;  // review B4: a 2661 of a tombstoned id, decided again as a new one
};
// A 2005's release: observer_hold_acks' own packets (`after`, Held = oha::HeldPacket: seq, opcode, key, bytes) and
// the scenes' (key 0), merged by their original sequence (stable: equal seqs never happen, one counter).
template <class Held>
void merge_release(std::vector<Held>& after, std::vector<OutPacket>& scene) {
    if (scene.empty()) return;
    for (OutPacket& s : scene) {
        Held h;
        h.seq = s.seq;
        h.opcode = s.op;
        h.key = 0;
        h.bytes = std::move(s.bytes);
        after.push_back(std::move(h));
    }
    std::stable_sort(after.begin(), after.end(), [](const Held& a, const Held& b) { return a.seq < b.seq; });
}
// Every opcode T2 active looks at (the rest pass untouched).
inline bool t2_op(uint16_t op) {
    switch (op) {
        case kOpPrepare: case kOpBranch: case kOpOnBranch: case kOpDelete: case kOpChartEvent: case kOpRemove:
        case kOpMoveField: case kOpLoadingField: case kOpBoundary2756:
            return true;
        default:
            return false;
    }
}

class SceneHold {
public:
    explicit SceneHold(HoldLimits l = {}) : l_(l) {}

    // One packet to user 2's live session (never a 2005: see announced). own = user 2's character key;
    // holding(key) -> bool: AckHold::withheld (review M1: Holding or Spilled, not Orphan); next_seq() -> u64: the shared order. Held: `packet`
    // moved in. Discarded: drop it (it was a kept scene's or a tombstoned id's). Pass: queue it as usual.
    template <class Holding, class NextSeq>
    Take offer(uint16_t op, std::span<const uint8_t> p, std::vector<uint8_t>& packet, uint64_t now_us, uint32_t own,
               Holding&& holding, NextSeq&& next_seq, std::vector<Event>& out) {
        if (field_boundary(op, p, own)) {
            for (Scene& s : s_) {
                discard(s, Kind::Dropped, now_us, {}, out, false);
                out.back().keys.clear();  // review B3: no key removed, the field changed (the line says so)
            }
            s_.clear();
            tombs_.clear();
            bytes_ = 0;
            return Take::Pass;
        }
        if (op == kOpRemove) {
            std::vector<uint32_t> keys;
            removed_keys(p, keys);
            for (const uint32_t k : keys) end_pending(k, Kind::Dropped, now_us, out);
            return Take::Pass;
        }
        if (op == kOpPrepare) {
            const Prepare pr = parse_prepare(p);
            if (!pr.ok || !own || pr.owner != own || !u2trade::is_trade_hash(pr.hash)) return Take::Pass;
            if (Scene* s = find(pr.id)) return keep(*s, op, packet, now_us, next_seq, out);  // again while kept
            // Review B4: a 2661 sent again with a tombstoned id is decided again as a new one (the tombstone goes): with
            // its NPCs already on the client (none withheld) it passes, else it is kept behind their 2005s.
            if (tomb(pr.id)) {
                untomb(pr.id);
                ++c_.tomb_reopened;
            }
            ++c_.own_2661;
            std::vector<uint32_t> pending;
            for (const uint32_t k : pr.bound) {
                if (holding(k)) pending.push_back(k);
            }
            if (pending.empty()) {
                ++c_.nohold;
                Event e;
                e.active = true;
                e.kind = Kind::NoHold;
                e.id = pr.id;
                e.hash = pr.hash;
                e.keys = pr.bound;
                out.push_back(std::move(e));
                return Take::Pass;
            }
            const size_t size = packet.size();
            if (s_.size() >= l_.max_scenes || size > l_.max_bytes_scene || bytes_ + size > l_.max_bytes_total) {
                Scene s;  // refused at the cap: discarded with its 2661 (nothing else of it was kept yet)
                s.id = pr.id;
                s.hash = pr.hash;
                s.pending = pending;
                s.since_us = now_us;
                s.ops.push_back(op);
                ++c_.packets_discarded;
                discard(s, Kind::Capacity, now_us, {}, out, true);
                packet.clear();
                return Take::Discarded;
            }
            Scene s;
            s.id = pr.id;
            s.hash = pr.hash;
            s.pending = pending;
            s.since_us = now_us;
            s_.push_back(std::move(s));
            ++c_.held;
            c_.peak_scenes = std::max<uint64_t>(c_.peak_scenes, s_.size());
            Event e;
            e.active = true;
            e.kind = Kind::WouldHold;
            e.id = pr.id;
            e.hash = pr.hash;
            e.keys = pending;
            e.ops = {op};
            out.push_back(std::move(e));
            return keep(s_.back(), op, packet, now_us, next_seq, out);
        }
        const auto id = scene_id_t2(op, p);
        if (!id) return Take::Pass;
        if (op == kOpDelete && p.size() != 17 && (find(*id) || tomb(*id))) ++c_.odd_2810;
        if (Scene* s = find(*id)) {
            if (op == kOpDelete) {  // its 2810: the scene goes with everything it kept, the 2810 too
                s->ops.push_back(op);
                ++c_.packets_discarded;
                discard(*s, Kind::Cancelled, now_us, {}, out, false);
                erase(*id);
                packet.clear();
                return Take::Discarded;
            }
            return keep(*s, op, packet, now_us, next_seq, out);
        }
        if (tomb(*id)) return drop_tombed(*id, op);
        return Take::Pass;
    }

    // A 2005 to user 2 announced `keys` (observer_hold_acks matched them): the scenes with no pending key left go,
    // their packets (with their seq) appended to `pkts` in their order.
    void announced(std::span<const uint32_t> keys, uint64_t now_us, std::vector<OutPacket>& pkts,
                   std::vector<Event>& out) {
        for (size_t i = 0; i < s_.size();) {
            Scene& s = s_[i];
            for (const uint32_t k : keys) s.pending.erase(std::remove(s.pending.begin(), s.pending.end(), k), s.pending.end());
            if (!s.pending.empty()) {
                ++i;
                continue;
            }
            Event e = event(s, Kind::Released, now_us);
            c_.delay_max_us = std::max(c_.delay_max_us, e.delay_us);
            ++c_.released;
            c_.packets_released += s.q.size();
            bytes_ -= std::min(bytes_, s.bytes);
            for (OutPacket& o : s.q) pkts.push_back(std::move(o));
            out.push_back(std::move(e));
            s_.erase(s_.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    // observer_hold_acks' hold of `key` ended without a 2005 (dropped, expired, retired): its scenes are discarded.
    // reason: observer_hold_acks' oha::Discard (review M1, for the line; 0xFF not given).
    void key_gone(uint32_t key, uint64_t now_us, std::vector<Event>& out, uint8_t reason = 0xFF) {
        const size_t first = out.size();
        end_pending(key, Kind::KeyGone, now_us, out);
        for (size_t i = first; i < out.size(); ++i) out[i].reason = reason;
    }
    // The 100 s bound and the tombstones' age.
    void tick(uint64_t now_us, std::vector<Event>& out) {
        for (size_t i = 0; i < s_.size();) {
            if (now_us - s_[i].since_us >= l_.max_hold_us) {
                discard(s_[i], Kind::Expired, now_us, {}, out, true);
                s_.erase(s_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
        tombs_.erase(std::remove_if(tombs_.begin(), tombs_.end(),
                                    [&](const Tomb& t) { return now_us - t.since_us >= l_.tomb_us; }),
                     tombs_.end());
    }
    // User 2 gone / a new epoch: everything goes, nothing to tell (nobody to send to).
    void clear(uint64_t now_us, std::vector<Event>& out) {
        for (Scene& s : s_) discard(s, Kind::KeyGone, now_us, {}, out, false);
        s_.clear();
        tombs_.clear();
        bytes_ = 0;
    }
    size_t scenes() const { return s_.size(); }
    size_t tombs() const { return tombs_.size(); }
    size_t live() const { return s_.size() + tombs_.size(); }
    size_t bytes() const { return bytes_; }
    bool kept(uint64_t id) const {
        for (const Scene& s : s_) {
            if (s.id == id) return true;
        }
        return false;
    }
    const HoldCounters& counters() const { return c_; }

private:
    struct Scene {
        uint64_t id = 0;
        uint32_t hash = 0;
        std::vector<uint32_t> pending;
        uint64_t since_us = 0;
        std::vector<OutPacket> q;
        std::vector<uint16_t> ops;
        size_t bytes = 0;
    };
    struct Tomb {
        uint64_t id = 0, since_us = 0;
    };
    Scene* find(uint64_t id) {
        for (Scene& s : s_) {
            if (s.id == id) return &s;
        }
        return nullptr;
    }
    void erase(uint64_t id) {
        for (size_t i = 0; i < s_.size(); ++i) {
            if (s_[i].id == id) {
                s_.erase(s_.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
    }
    bool tomb(uint64_t id) const {
        for (const Tomb& t : tombs_) {
            if (t.id == id) return true;
        }
        return false;
    }
    void untomb(uint64_t id) {
        tombs_.erase(std::remove_if(tombs_.begin(), tombs_.end(), [&](const Tomb& t) { return t.id == id; }),
                     tombs_.end());
    }
    Take drop_tombed(uint64_t id, uint16_t op) {
        ++c_.tomb_dropped;
        if (op == kOpDelete) {  // its 2810 ends the tombstone
            tombs_.erase(std::remove_if(tombs_.begin(), tombs_.end(), [&](const Tomb& t) { return t.id == id; }),
                         tombs_.end());
        }
        return Take::Discarded;
    }
    void add_tomb(uint64_t id, uint64_t now_us) {
        if (tomb(id)) return;
        if (tombs_.size() >= l_.max_tombs) tombs_.erase(tombs_.begin());  // the oldest
        tombs_.push_back({id, now_us});
    }
    template <class NextSeq>
    Take keep(Scene& s, uint16_t op, std::vector<uint8_t>& packet, uint64_t now_us, NextSeq&& next_seq,
              std::vector<Event>& out) {
        const size_t size = packet.size();
        if (s.q.size() + 1 > l_.max_packets || s.bytes + size > l_.max_bytes_scene || bytes_ + size > l_.max_bytes_total) {
            // The cap: the scene is refused and discarded with all it kept, this packet too.
            s.ops.push_back(op);
            ++c_.packets_discarded;
            const uint64_t id = s.id;
            discard(s, Kind::Capacity, now_us, {}, out, true);
            erase(id);
            packet.clear();
            return Take::Discarded;
        }
        OutPacket o;
        o.seq = next_seq();
        o.op = op;
        o.bytes = std::move(packet);
        packet.clear();
        s.q.push_back(std::move(o));
        s.ops.push_back(op);
        s.bytes += size;
        bytes_ += size;
        ++c_.packets_held;
        c_.peak_bytes = std::max<uint64_t>(c_.peak_bytes, bytes_);
        return Take::Held;
    }
    void end_pending(uint32_t key, Kind why, uint64_t now_us, std::vector<Event>& out) {
        for (size_t i = 0; i < s_.size();) {
            Scene& s = s_[i];
            if (std::find(s.pending.begin(), s.pending.end(), key) == s.pending.end()) {
                ++i;
                continue;
            }
            discard(s, why, now_us, {key}, out, true);
            s_.erase(s_.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    Event event(const Scene& s, Kind k, uint64_t now_us) const {
        Event e;
        e.active = true;
        e.kind = k;
        e.id = s.id;
        e.hash = s.hash;
        e.keys = s.pending;
        e.delay_us = now_us >= s.since_us ? now_us - s.since_us : 0;
        const size_t n = std::min<size_t>(s.ops.size(), 64);
        e.ops.assign(s.ops.begin(), s.ops.begin() + static_cast<std::ptrdiff_t>(n));
        e.more_ops = static_cast<uint32_t>(s.ops.size() - n);
        return e;
    }
    // The scene goes without being sent: its packets are dropped; tomb: later packets of its id are dropped too.
    void discard(Scene& s, Kind why, uint64_t now_us, std::vector<uint32_t> gone, std::vector<Event>& out, bool tomb) {
        Event e = event(s, why, now_us);
        if (!gone.empty()) e.keys = std::move(gone);
        ++c_.discarded[static_cast<size_t>(why)];
        c_.packets_discarded += s.q.size();
        bytes_ -= std::min(bytes_, s.bytes);
        s.q.clear();
        s.bytes = 0;
        if (tomb) add_tomb(s.id, now_us);
        out.push_back(std::move(e));
    }

    HoldLimits l_;
    std::vector<Scene> s_;
    std::vector<Tomb> tombs_;
    size_t bytes_ = 0;
    HoldCounters c_;
};

}  // namespace cdmp::session2::hso
