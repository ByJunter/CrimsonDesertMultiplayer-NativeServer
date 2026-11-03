#pragma once
// P8 session 8, pets (docs/research/pendientes/p8-s8-mascotas.md §2.2, §4.3; the implementation:
// docs/research/pendientes/p8-impl-mascotas.md): the pure logic of attach_follow.cpp (a generalisation
// of horse_follow_logic.h), with no game access, so the tests can run it.
//
// [V] session 8: a pet climbs onto its owner's shoulder with a StartAction whose Path parent (Req
// +0x116 / Ack +0x117, u64) is the owner's character (0xA...); while it rides there it sends NO
// MoveActorReq for 8-12 s, only a StartActionReq every ~4.17 s. The observing client (mode 2) never
// applies the parent: the pet floats at the last sample (1.58 m up) while the character walks away.
//
// [session2] attach_follow = 0 (default) | probe | 1, HOST:
//   - while the last StartAction of a user's child (0xB, owner a user) names a character (0xA) as its
//     Path parent, every 3rd Move of that character yields a Move of the child at
//         pos = K.pos + R(K.quat) * local.pos,  quat = K.quat * local.quat
//     where `local` is the Path's local transform of that StartAction (Ack +0xD7: pos 12, quat 16; THE
//     HYPOTHESIS to validate: probe measures it against the action's own world position);
//   - host's pet -> user 2: a synthetic MoveActorAck 2697 queued in take_frame right after the
//     character's (as horse_follow); with 1 only;
//   - user 2's pet -> host: measured only (a synthetic MoveActorReq 2556 would change the server's state
//     at ~20 Hz; never sent by this version, even with 1: only "would make" is counted);
//   - the attachment ends with a StartAction of the child with another parent (or none), a Move of the
//     child with parent -1, the dismount hash, or kStaleMs without an attached action.
// probe: the same bookkeeping and measures, nothing sent.
//
// The attached-actor registry (both PCs, enabled by remote_interp_owned_cap_ms): which 0xB keys ride an
// 0xA parent now, from the StartActions seen on the network (the guest: the host's Acks it receives;
// the host: user 2's Reqs); remote_interp does not record their sample gaps meanwhile.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include "mod/session2/user2_funnels_logic.h"

namespace cdmp::session2::afl {

using u2f::Mode;
using u2f::mode_name;
using u2f::parse_mode;

// ---------------------------------------------------------------- formats (mirror-ack-formats.md §1, §2)

inline constexpr uint16_t kOpMoveActorReq = 2556;
inline constexpr uint16_t kOpMoveActorAck = 2697;
inline constexpr uint16_t kOpStartActionReq = 2223;
inline constexpr uint16_t kOpStartActionAck = 2991;
inline constexpr size_t kMoveReqSize = 514;
inline constexpr size_t kMoveAckSize = 235;
inline constexpr size_t kActionSize = 725;  // Req and Ack
inline constexpr size_t kHeader = 5;        // u16 opcode | i8 slot | u16 payload_len

// MoveActor (Req and Ack share these).
inline constexpr size_t kMKey = 0x00;
inline constexpr size_t kMPos = 0x04;
inline constexpr size_t kMQuat = 0x10;
inline constexpr size_t kMAux = 0x20;
inline constexpr size_t kMLocalPos = 0x4C;
inline constexpr size_t kMLocalQuat = 0x58;
inline constexpr size_t kMLocalScale = 0x68;
inline constexpr size_t kMParent = 0x8C;
inline constexpr size_t kMWorldPos = 0x94;
inline constexpr size_t kMWorldEnd = 0xD4;
inline constexpr size_t kMTs = 0xD5;
inline constexpr size_t kMReqHistory = 0xEA;  // Req only: 10 x {u32, u64, u64, u64}

// StartAction: block B (MoveData first, = MoveActor +0x04..+0xDD) at Req +0x8E / Ack +0x8F, so a Move
// offset X >= 4 is Req X + 0x8A / Ack X + 0x8B. The action hash: +0x08 (kHash0 below).
struct ActionLayout {
    size_t pos, quat, local_pos, local_quat, parent, world_pos, world_quat, ts, target;
};
inline constexpr size_t kReqShift = 0x8A, kAckShift = 0x8B;
inline constexpr ActionLayout layout(size_t shift) {
    return {kMPos + shift,      kMQuat + shift,    kMLocalPos + shift, kMLocalQuat + shift, kMParent + shift,
            kMWorldPos + shift, 0xA0 + shift,      kMTs + shift,       0xE2 + shift};
}
inline constexpr ActionLayout kReq = layout(kReqShift);  // parent +0x116, ts +0x15F, target +0x16C
inline constexpr ActionLayout kAck = layout(kAckShift);  // parent +0x117, local +0xD7, ts +0x160, target +0x16D
static_assert(kReq.parent == 0x116 && kAck.parent == 0x117 && kAck.local_pos == 0xD7 && kAck.ts == 0x160 &&
              kReq.target == 0x16C && kAck.pos == 0x8F);
// The action's hash [V, session 8 host capture]: u32 at +0x08 in every Req and Ack of the pets
// (0xA497102C 29 times, 0xBC77AF45 29, 0x8276C0C5 28); the Req repeats it at +0x04. +0x04 is read too.
inline constexpr size_t kHash0 = 0x08, kHash1 = 0x04;

// session 8 (HC), by the parent each one carries [V]: 0xA497102C (climbing up) parent -1;
// 0xBC77AF45 (on the shoulder) parent = the character; 0x8276C0C5 (getting down) parent = the character
// STILL: only its hash ends the attachment (the next action / Move then has parent -1).
inline constexpr uint32_t kDismountHash = 0x8276C0C5;
inline constexpr uint64_t kStaleMs = 15000;  // > 3 x the 4.17 s of the shoulder's StartActions
inline constexpr uint32_t kEvery = 3;        // one child Move per 3 of its parent's (~20 Hz from ~60 Hz)
inline constexpr float kMaxLocal = 5.0f;     // a Path local offset farther than this is not an attachment

// ---------------------------------------------------------------- math (quaternions x,y,z,w)

struct Vec3 {
    float x = 0, y = 0, z = 0;
};
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};
struct Pose {
    Vec3 p;
    Quat q;
};

inline Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline float len(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
inline float dist(Vec3 a, Vec3 b) { return len(sub(a, b)); }
inline Quat conj(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }
inline float qlen(Quat q) { return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); }
inline Quat mul(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Quat normalized(Quat q) {
    const float l = qlen(q);
    if (!(l > 1e-6f)) return {};
    return {q.x / l, q.y / l, q.z / l, q.w / l};
}
// v' = q v q*  (t = 2 q.xyz x v; v' = v + w t + q.xyz x t)
inline Vec3 rotate(Quat q, Vec3 v) {
    const Vec3 u{q.x, q.y, q.z};
    auto cross = [](Vec3 a, Vec3 b) { return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
    const Vec3 c = cross(u, v);
    const Vec3 t{2 * c.x, 2 * c.y, 2 * c.z};
    const Vec3 c2 = cross(u, t);
    return {v.x + q.w * t.x + c2.x, v.y + q.w * t.y + c2.y, v.z + q.w * t.z + c2.z};
}
// world = parent (x) local
inline Pose compose(const Pose& parent, const Pose& local) {
    return {add(parent.p, rotate(parent.q, local.p)), normalized(mul(parent.q, local.q))};
}
// local such that compose(parent, local) == world
inline Pose relative(const Pose& parent, const Pose& world) {
    const Quat inv = conj(normalized(parent.q));
    return {rotate(inv, sub(world.p, parent.p)), normalized(mul(inv, world.q))};
}
inline bool finite(float v) { return std::isfinite(v); }
inline bool sane_world(const Pose& a) {
    const float l = qlen(a.q);
    return finite(a.p.x) && finite(a.p.y) && finite(a.p.z) && std::fabs(a.p.x) < 1e7f && std::fabs(a.p.y) < 1e7f &&
           std::fabs(a.p.z) < 1e7f && finite(l) && l > 0.9f && l < 1.1f;
}
// A Path local transform that can be an attachment offset (close to the parent, a unit quaternion).
inline bool sane_local(const Pose& a) { return sane_world(a) && len(a.p) <= kMaxLocal; }

// ---------------------------------------------------------------- reading packets

inline uint32_t rd32(std::span<const uint8_t> p, size_t at) {
    uint32_t v = 0;
    std::memcpy(&v, p.data() + at, 4);
    return v;
}
inline uint64_t rd64(std::span<const uint8_t> p, size_t at) {
    uint64_t v = 0;
    std::memcpy(&v, p.data() + at, 8);
    return v;
}
inline Pose rd_pose(std::span<const uint8_t> p, size_t pos, size_t quat) {
    Pose r;
    std::memcpy(&r.p, p.data() + pos, 12);
    std::memcpy(&r.q, p.data() + quat, 16);
    return r;
}

inline bool child_key(uint32_t key) { return (key >> 28) == 0xB; }
inline bool char_key(uint32_t key) { return (key >> 28) == 0xA; }
// A Path parent that is a character: a u64 whose high half is 0 and whose low half is an 0xA key.
inline bool char_parent(uint64_t parent) { return (parent >> 32) == 0 && char_key(static_cast<uint32_t>(parent)); }
inline constexpr uint64_t kNoParent = ~0ull;

struct Action {
    bool ok = false;
    uint32_t key = 0, hash0 = 0, hash1 = 0, target = 0;
    uint64_t parent = kNoParent, ts = 0;
    Pose world, local, path_world;
};
inline Action read_action(std::span<const uint8_t> payload, bool ack) {
    Action a;
    if (payload.size() != kActionSize) return a;
    const ActionLayout& l = ack ? kAck : kReq;
    a.ok = true;
    a.key = rd32(payload, 0);
    a.hash0 = rd32(payload, kHash0);
    a.hash1 = rd32(payload, kHash1);
    a.parent = rd64(payload, l.parent);
    a.ts = rd64(payload, l.ts);
    a.target = rd32(payload, l.target);
    a.world = rd_pose(payload, l.pos, l.quat);
    a.local = rd_pose(payload, l.local_pos, l.local_quat);
    a.path_world = rd_pose(payload, l.world_pos, l.world_quat);
    return a;
}
inline bool dismount(const Action& a) { return a.hash0 == kDismountHash || a.hash1 == kDismountHash; }

struct Move {
    bool ok = false;
    uint32_t key = 0;
    uint64_t parent = kNoParent, ts = 0;
    Pose pose;
};
inline Move read_move(std::span<const uint8_t> payload, size_t expected) {
    Move m;
    if (payload.size() != expected) return m;
    m.ok = true;
    m.key = rd32(payload, kMKey);
    m.parent = rd64(payload, kMParent);
    m.ts = rd64(payload, kMTs);
    m.pose = rd_pose(payload, kMPos, kMQuat);
    return m;
}

// ---------------------------------------------------------------- the synthetic Moves

// Writes a child's pose into a copy of its parent's Move payload (Ack or Req): the recipe of
// horse_follow_logic.h make_horse_ack (the shape of a child's own Moves: parent -1, Path local = world).
inline void write_child(uint8_t* m, uint32_t child, const Pose& pose) {
    std::memcpy(m + kMKey, &child, 4);
    std::memcpy(m + kMPos, &pose.p, 12);
    std::memcpy(m + kMQuat, &pose.q, 16);
    std::memset(m + kMAux, 0, 4);
    std::memcpy(m + kMLocalPos, &pose.p, 12);
    std::memcpy(m + kMLocalQuat, &pose.q, 16);
    const float one[3] = {1.0f, 1.0f, 1.0f};
    std::memcpy(m + kMLocalScale, one, 12);
    std::memset(m + kMParent, 0xFF, 8);
    std::memset(m + kMWorldPos, 0, kMWorldEnd - kMWorldPos);
}
// The child's 2697 from its parent's (whole packet: header + 235 bytes). ts and tail: the parent's.
inline bool make_child_ack(std::span<const uint8_t> parent_packet, uint32_t child, const Pose& pose,
                           std::vector<uint8_t>& out) {
    if (parent_packet.size() != kHeader + kMoveAckSize || !child_key(child) || !sane_world(pose)) return false;
    const uint16_t op = static_cast<uint16_t>(parent_packet[0] | (parent_packet[1] << 8));
    const uint16_t n = static_cast<uint16_t>(parent_packet[3] | (parent_packet[4] << 8));
    if (op != kOpMoveActorAck || n != kMoveAckSize) return false;
    out.assign(parent_packet.begin(), parent_packet.end());
    write_child(out.data() + kHeader, child, pose);
    return true;
}
// The child's 2556 from its parent's payload (514 bytes), its 10-entry history zeroed. NOT sent by this
// version (user 2's pet -> host stays in probe): built only to count what 1 would make.
inline bool make_child_req(std::span<const uint8_t> parent_payload, uint32_t child, const Pose& pose,
                           std::vector<uint8_t>& out) {
    if (parent_payload.size() != kMoveReqSize || !child_key(child) || !sane_world(pose)) return false;
    out.assign(parent_payload.begin(), parent_payload.end());
    write_child(out.data(), child, pose);
    std::memset(out.data() + kMReqHistory, 0, kMoveReqSize - kMReqHistory);
    return true;
}

// ---------------------------------------------------------------- the tracker (not thread-safe)

enum class Ev : uint8_t { Ignored, NotOwned, Attached, Refreshed, Detached, BadLocal };
inline constexpr size_t kEvents = 6;
inline const char* ev_name(Ev e) {
    switch (e) {
        case Ev::Ignored: return "ignored";
        case Ev::NotOwned: return "not a user's child";
        case Ev::Attached: return "attached";
        case Ev::Refreshed: return "still attached";
        case Ev::Detached: return "detached";
        case Ev::BadLocal: return "attached, Path local not an offset";
    }
    return "?";
}

// What one attached StartAction says about the hypothesis (metres; < 0 = not measurable).
struct Measure {
    float same_local = -1;  // |K + R(K) * local - action pos|, K = the parent's last Move (the hypothesis)
    float path_world = -1;  // |PathWorld + R(PathWorld) * local - action pos| (the other reading of the Path)
    float next_local = -1;  // the previous action's local applied to K now vs this action's pos (what 1 would show)
    float next_rel = -1;    // the previous action's pose relative to K then, applied to K now
    int64_t dt_ms = 0;      // this action's ts - K's last Move ts
    Pose k, rel;            // K's last pose; this action's pose relative to it
    bool have_k = false;
};

struct Make {
    uint32_t child = 0;
    Pose pose;
};

template <size_t N>
class Tracker {
public:
    struct Att {
        uint32_t child = 0, parent = 0, hash = 0;
        Pose local, rel;
        bool local_ok = false, rel_ok = false;
        uint64_t since_ms = 0, last_ms = 0;
        uint32_t moves = 0;
    };
    struct Char {
        uint32_t key = 0;
        Pose pose;
        uint64_t ts = 0, at_ms = 0;
    };

    // A StartAction of `a.key`. owner_ok: the child is a user's (only asked when the parent is a
    // character: the caller's SAM read). m: the measures when it is (still) attached.
    Ev on_action(const Action& a, uint64_t now_ms, bool owner_ok, Measure* m = nullptr) {
        if (!a.ok || !child_key(a.key)) return Ev::Ignored;
        Att* e = find(a.key);
        if (!char_parent(a.parent) || dismount(a)) {
            if (!e) return Ev::Ignored;
            erase(e);
            return Ev::Detached;
        }
        if (!owner_ok) {
            if (e) erase(e);
            return Ev::NotOwned;
        }
        const auto parent = static_cast<uint32_t>(a.parent);
        const Char* k = find_char(parent);
        Measure meas;
        if (k) {
            meas.have_k = true;
            meas.k = k->pose;
            meas.dt_ms = static_cast<int64_t>(a.ts) - static_cast<int64_t>(k->ts);
            meas.same_local = dist(compose(k->pose, a.local).p, a.world.p);
            meas.rel = relative(k->pose, a.world);
            if (e && e->parent == parent) {
                if (e->local_ok) meas.next_local = dist(compose(k->pose, e->local).p, a.world.p);
                if (e->rel_ok) meas.next_rel = dist(compose(k->pose, e->rel).p, a.world.p);
            }
        }
        if (sane_world(a.path_world)) meas.path_world = dist(compose(a.path_world, a.local).p, a.world.p);
        const bool fresh = !e;
        if (!e) e = insert(a.key);
        if (fresh || e->parent != parent) {
            e->since_ms = now_ms;
            e->moves = 0;
        }
        e->parent = parent;
        e->hash = a.hash0;
        e->last_ms = now_ms;
        e->local = a.local;
        e->local_ok = sane_local(a.local);
        e->rel = meas.rel;
        e->rel_ok = meas.have_k && sane_local(meas.rel);
        if (m) *m = meas;
        if (!e->local_ok) return Ev::BadLocal;
        return fresh ? Ev::Attached : Ev::Refreshed;
    }

    // A Move of a child: with no character parent it is on its own again.
    bool on_child_move(uint32_t key, uint64_t parent) {
        Att* e = find(key);
        if (!e || char_parent(parent)) return false;
        erase(e);
        return true;
    }

    // A Move of a character: its pose is kept; each child attached to it gets a Make every `every`
    // Moves (only with a usable Path local). Stale attachments end here. Returns how many Makes.
    size_t on_char_move(const Move& mv, uint64_t now_ms, uint32_t every, std::array<Make, N>& out,
                        size_t* expired = nullptr) {
        if (!mv.ok || !char_key(mv.key) || !sane_world(mv.pose)) return 0;
        note_char(mv, now_ms);
        size_t n = 0, gone = 0;
        for (size_t i = 0; i < n_;) {
            Att& e = a_[i];
            if (now_ms - e.last_ms > kStaleMs) {
                erase(&e);
                ++gone;
                continue;
            }
            if (e.parent == mv.key && e.local_ok && every && ++e.moves % every == 0 && n < out.size()) {
                out[n++] = {e.child, compose(mv.pose, e.local)};
            }
            ++i;
        }
        if (expired) *expired = gone;
        return n;
    }

    // The attachment of `child` ends (owner unreadable, a deferred detach). True: there was one.
    bool drop(uint32_t child) {
        Att* e = find(child);
        if (!e) return false;
        erase(e);
        return true;
    }

    bool attached(uint32_t child) const {
        for (size_t i = 0; i < n_; ++i) {
            if (a_[i].child == child) return true;
        }
        return false;
    }
    size_t size() const { return n_; }
    void clear() {
        n_ = 0;
        for (auto& c : c_) c = Char{};
    }
    const Att* get(uint32_t child) const {
        for (size_t i = 0; i < n_; ++i) {
            if (a_[i].child == child) return &a_[i];
        }
        return nullptr;
    }

private:
    Att* find(uint32_t child) {
        for (size_t i = 0; i < n_; ++i) {
            if (a_[i].child == child) return &a_[i];
        }
        return nullptr;
    }
    Att* insert(uint32_t child) {
        if (n_ < N) {
            a_[n_] = Att{};
            a_[n_].child = child;
            return &a_[n_++];
        }
        // Full: the oldest attachment goes.
        size_t old = 0;
        for (size_t i = 1; i < n_; ++i) {
            if (a_[i].last_ms < a_[old].last_ms) old = i;
        }
        a_[old] = Att{};
        a_[old].child = child;
        return &a_[old];
    }
    void erase(Att* e) {
        const size_t i = static_cast<size_t>(e - a_.data());
        if (i >= n_) return;
        a_[i] = a_[n_ - 1];
        --n_;
    }
    const Char* find_char(uint32_t key) const {
        for (const Char& c : c_) {
            if (c.key == key) return &c;
        }
        return nullptr;
    }
    void note_char(const Move& mv, uint64_t now_ms) {
        Char* slot = nullptr;
        for (Char& c : c_) {
            if (c.key == mv.key) {
                slot = &c;
                break;
            }
        }
        if (!slot) {
            slot = &c_[0];
            for (Char& c : c_) {
                if (c.key == 0 || c.at_ms < slot->at_ms) slot = &c;
                if (c.key == 0) break;
            }
        }
        slot->key = mv.key;
        slot->pose = mv.pose;
        slot->ts = mv.ts;
        slot->at_ms = now_ms;
    }

    std::array<Att, N> a_{};
    size_t n_ = 0;
    std::array<Char, 4> c_{};
};

// Detaches that found the tracker busy (its try-lock): kept here lock-free and applied first by the next
// thread that takes the tracker, so a missed "getting down" never leaves the child glued until kStaleMs.
template <size_t N>
class PendingDetach {
public:
    // False: full (the detach is lost; kStaleMs still ends it).
    bool push(uint32_t key) noexcept {
        if (!key) return true;
        for (auto& s : s_) {
            if (s.load(std::memory_order_relaxed) == key) return true;
        }
        for (auto& s : s_) {
            uint32_t expected = 0;
            if (s.compare_exchange_strong(expected, key, std::memory_order_acq_rel) || expected == key) return true;
        }
        return false;
    }
    // Under the tracker's lock: each pending key once.
    template <class F>
    size_t drain(F&& f) {
        size_t n = 0;
        for (auto& s : s_) {
            if (!s.load(std::memory_order_relaxed)) continue;
            if (const uint32_t k = s.exchange(0, std::memory_order_acq_rel)) {
                f(k);
                ++n;
            }
        }
        return n;
    }
    bool empty() const noexcept {
        for (const auto& s : s_) {
            if (s.load(std::memory_order_relaxed)) return false;
        }
        return true;
    }

private:
    std::array<std::atomic<uint32_t>, N> s_{};
};

// ---------------------------------------------------------------- the registry (lock-free)

enum class Reg : uint8_t { None, Attach, Detach };
// What one packet says for the registry (Ack offsets for 2991/2697, Req offsets for 2223/2556).
struct RegEvent {
    Reg what = Reg::None;
    uint32_t key = 0;
};
inline RegEvent registry_event(uint16_t opcode, std::span<const uint8_t> payload) {
    RegEvent r;
    const bool ack_action = opcode == kOpStartActionAck, req_action = opcode == kOpStartActionReq;
    if (ack_action || req_action) {
        const Action a = read_action(payload, ack_action);
        if (!a.ok || !child_key(a.key)) return r;
        r.key = a.key;
        r.what = char_parent(a.parent) && !dismount(a) ? Reg::Attach : Reg::Detach;
        return r;
    }
    if (opcode == kOpMoveActorAck || opcode == kOpMoveActorReq) {
        const Move m = read_move(payload, opcode == kOpMoveActorAck ? kMoveAckSize : kMoveReqSize);
        if (!m.ok || !child_key(m.key) || char_parent(m.parent)) return r;
        r.key = m.key;
        r.what = Reg::Detach;
    }
    return r;
}

// Which keys ride a character now. The network thread writes; remote_interp's hooks read (any thread).
template <size_t N>
class Registry {
public:
    void attach(uint32_t key, uint64_t now_ms) noexcept {
        if (!key) return;
        const uint64_t until = now_ms + kStaleMs;
        for (size_t i = 0; i < N; ++i) {
            if (key_[i].load(std::memory_order_relaxed) == key) {
                until_[i].store(until, std::memory_order_relaxed);
                return;
            }
        }
        for (size_t i = 0; i < N; ++i) {
            if (until_[i].load(std::memory_order_relaxed) <= now_ms) {
                until_[i].store(0, std::memory_order_relaxed);
                key_[i].store(key, std::memory_order_relaxed);
                until_[i].store(until, std::memory_order_relaxed);
                return;
            }
        }
        const size_t i = next_.fetch_add(1, std::memory_order_relaxed) % N;
        until_[i].store(0, std::memory_order_relaxed);
        key_[i].store(key, std::memory_order_relaxed);
        until_[i].store(until, std::memory_order_relaxed);
    }
    void detach(uint32_t key) noexcept {
        for (size_t i = 0; i < N; ++i) {
            if (key_[i].load(std::memory_order_relaxed) == key) until_[i].store(0, std::memory_order_relaxed);
        }
    }
    bool attached(uint32_t key, uint64_t now_ms) const noexcept {
        for (size_t i = 0; i < N; ++i) {
            if (key_[i].load(std::memory_order_relaxed) == key && until_[i].load(std::memory_order_relaxed) > now_ms) {
                return true;
            }
        }
        return false;
    }
    size_t count(uint64_t now_ms) const noexcept {
        size_t n = 0;
        for (size_t i = 0; i < N; ++i) n += until_[i].load(std::memory_order_relaxed) > now_ms ? 1 : 0;
        return n;
    }
    void clear() noexcept {
        for (size_t i = 0; i < N; ++i) {
            until_[i].store(0, std::memory_order_relaxed);
            key_[i].store(0, std::memory_order_relaxed);
        }
    }

private:
    std::array<std::atomic<uint32_t>, N> key_{};
    std::array<std::atomic<uint64_t>, N> until_{};
    std::atomic<uint32_t> next_{0};
};

// ---------------------------------------------------------------- measures

// Error buckets in millimetres: <=50, <=100, <=200, <=500, <=1000, <=2000, more.
inline constexpr std::array<uint32_t, 6> kErrBounds = {50, 100, 200, 500, 1000, 2000};
struct ErrHist {
    std::array<std::atomic<uint64_t>, kErrBounds.size() + 1> n{};
    void add(float metres) noexcept {
        if (!(metres >= 0)) return;
        const double mm = static_cast<double>(metres) * 1000.0;
        size_t i = 0;
        while (i < kErrBounds.size() && mm > kErrBounds[i]) ++i;
        n[i].fetch_add(1, std::memory_order_relaxed);
    }
    uint64_t total() const noexcept {
        uint64_t t = 0;
        for (const auto& x : n) t += x.load(std::memory_order_relaxed);
        return t;
    }
};

}  // namespace cdmp::session2::afl
