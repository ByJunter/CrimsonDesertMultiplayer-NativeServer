#pragma once
// P8 session 9, C1 (docs/research/pendientes/p8-s9-combate-montura.md §1.1, §6 "Combate"; the implementation:
// docs/research/pendientes/p8-impl-s9-movimiento.md §3): the pure logic of combat_follow.cpp, with no game
// access, so the tests can run it.
//
// The fault [V, session 9]: the enemies are controlled by the HOST's client and send ~1.5 samples/s in combat
// (Move + StartAction; gaps p50 500-580 ms, p90 ~1 s). On the guest remote_interp gives them
// D = max(3 gaps) + 150 -> 1.1-1.8 s: the action is applied on arrival, the body 1.2 s later (the guest hits
// where the enemy was 1275 ms ago, p50).
//
// [session2] combat_follow = 0 (default) | probe | 1, HOST only (create_session=1). Engaged enemies are the
// 0xB keys that, in what the server writes to user 2's session (take_frame), (a) are the target or the attacker
// of a PostProcessAttackResult_AttackTargetAck 2922 (+0x00 target, +0x04 attacker) together with user 2's
// character, or (b) have a StartActionAck 2991 whose target (+0x16D) is user 2's character; engaged for
// kEngageMs after the last such packet. A mod thread, every kTickMs (~16 Hz), for at most kMaxFollowed of them:
// reads in the HOST client's memory (SEH reads, no game call) the actor, its control byte [actor+0xC8] (1/2 =
// simulated by this client), TransformSync +0xA4 quat / +0xB4 pos / +0xC0 sector, and user 2's character's
// position; within kRangeM, no parent, not withheld from user 2 (observer_hold_acks), it queues a synthetic
// MoveActorAck 2697 (235 B, the layout of the enemies' own) with server_push_extra.
// ts: the enemy's own client clock. Anchored on its last real sample to user 2 (its ts and the host client's
// game clock read at that moment in take_frame); synthetic ts = anchor_ts + (game_now - anchor_game_now),
// strictly increasing, above every real ts of it sent to user 2, and only while the clock moves; nothing while a
// real ts disagrees with the clock read here by > kMaxOffsetMs, after kRealStaleMs without a real sample, or once
// it died / was removed (2530 / 2516). probe: everything but the push (counted, cadence kept).
//
// P8 session 25, the combat stream (docs/research/noche/combate-retardo.md §5, §6.1): [session2] combat_follow_v2 =
// 0 (default) | probe | 1, HOST, needs combat_follow. The guest's D (remote_interp) was bimodal, ~0.2 s or 0.9-1.3 s,
// because this side's sampling clashed with its controller: it started late, sent nothing for moves under 1 cm and a
// 500 ms keepalive that moved millimetres (the game inserts anything over 1e-4 m), and stopped after 3 s without a
// real sample. With 1: also the enemies "in combat near" user 2 (any 2922 / 2991 to session 2 where an 0xB targets
// or is hit by ANY character 0xA, the host's too) within kRangeV2M; every kTickV2Ms on a high-resolution waitable
// timer with a drift-free schedule; sent when it moved by the game's own thresholds (> 1e-4 m, or rotated by more
// than 1.19e-7 rad); a kKeepaliveV2Ms keepalive that repeats the last pose bit for bit (the game does not insert it,
// so it makes no gap in the buffer, but it reaches the guest as cadence); no cutoff for the lack of real samples
// (the clock gate ClockBad and the 30 s anchor age stay); at most kMaxFollowedV2, the nearest first. v2 has its own
// table (fights included), lock, thread and clock: with probe v1 runs exactly as before and v2 is decided beside it,
// counted, never sent.
//
// MoveActorAck 2697 of an enemy [V, session 9 H1/G1 byte for byte]:
//   +0x00 key | +0x04 pos | +0x10 quat | +0x20 u32 0 | +0x24 Transform identity (pos 0, quat (0,0,0,1),
//   scale 1) | +0x4C Path.local = pos, quat, scale 1 | +0x74 velocity | +0x80 vec3 (angular; 0 here) |
//   +0x8C parent -1 | +0x94 Path.world = pos 0, quat (0,0,0,1), scale 1 | +0xBC, +0xC8 vec3 0 | +0xD4 u8 0 |
//   +0xD5 u64 ts | +0xDD u8 0 | +0xDE u8 0 (the server's flag) | +0xDF f32 0, f32 0 | +0xE7 f32 speed.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

#include "common/movement_layout.h"
#include "mod/session2/user2_funnels_logic.h"

namespace cdmp::session2::cfl {

using u2f::Mode;
using u2f::mode_name;
using u2f::parse_mode;

inline constexpr uint16_t kOpMoveActorAck = 2697;
inline constexpr uint16_t kOpStartActionAck = 2991;
inline constexpr uint16_t kOpAttackTargetAck = 2922;  // PostProcessAttackResult_AttackTargetAck
inline constexpr size_t kHeader = 5;
inline constexpr size_t kMoveAck = 235;
inline constexpr size_t kStartAck = 725;
inline constexpr size_t kMoveParent = 0x8C, kMoveTs = 0xD5;
inline constexpr size_t kStartParent = 0x117, kStartTs = 0x160, kStartTarget = 0x16D;
inline constexpr size_t kAttackMin = 8;
// Liveness (review 2026-10-10) [V layouts, session 9 G1]: NotifyHandleDeadAck 2530 (19 B, +0x00 the dead actor,
// +0x04 the killer); RemoveActorAck 2516 (10 B, `04 00 00 00 00 xx` then the key at +0x06; 8342 of 8342).
inline constexpr uint16_t kOpNotifyHandleDeadAck = 2530;
inline constexpr uint16_t kOpRemoveActorAck = 2516;
inline constexpr size_t kDeadMin = 4, kRemoveSize = 10, kRemoveKey = 6;

inline constexpr uint64_t kEngageMs = 8000;
inline constexpr float kRangeM = 25.0f;
inline constexpr uint32_t kTickMs = 60;          // ~16.7 Hz
inline constexpr uint64_t kKeepaliveMs = 500;    // an enemy standing still: one sample every 0.5 s
inline constexpr size_t kMaxFollowed = 8;
inline constexpr uint64_t kAnchorMaxAgeMs = 30000;  // an anchor older than this (game clock) is not trusted
// No real sample of it for this long (mod clock): it is not moving on the host's client (stunned, dying, gone):
// nothing is made (session 9: in combat the gaps were p90 ~1 s, max 2-2.4 s).
inline constexpr uint64_t kRealStaleMs = 3000;
// |real ts - the host client's game clock when take_frame saw it| beyond this: the clock this side reads is not
// the one of that enemy's ts; nothing is made until a sample agrees again.
inline constexpr int64_t kMaxOffsetMs = 250;

// combat_follow_v2 (P8 session 25).
inline constexpr float kRangeV2M = 35.0f;
inline constexpr uint32_t kTickV2Ms = 33;          // ~30 Hz
inline constexpr uint64_t kKeepaliveV2Ms = 100;    // due from 100 - half a tick: every 3rd tick (99 ms)
inline constexpr size_t kMaxFollowedV2 = 12;
inline constexpr float kMovedV2M = 1.0e-4f;        // setInterpolatePositionData 0x11B1FD0 (0x5D72C34)
inline constexpr double kRotatedV2Rad = 1.19e-7;   // setInterpolateRotateData 0x11B26E0 (0x539B10)

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
inline bool is_b(uint32_t k) { return (k >> 28) == 0xB; }
inline bool is_a(uint32_t k) { return (k >> 28) == 0xA; }

// ---------------------------------------------------------------- what one packet to user 2 says

struct Seen {
    uint32_t engage = 0;   // an 0xB key now engaged with user 2 (0 = none)
    uint32_t fight = 0;    // v2: an 0xB key fighting any character 0xA (user 2's too: engage implies fight)
    uint32_t sample = 0;   // an 0xB key whose real position sample this is (0 = none)
    uint64_t ts = 0;       // the sample's ts (its controller's clock)
    bool parent = false;   // the sample has a parent (a rider, a grabbed body): not followed
    uint32_t gone = 0;     // an 0xB key that died or was removed: forgotten
};

inline Seen read_out(uint16_t op, std::span<const uint8_t> payload, uint32_t user2_char, movement::Build build = movement::Build::v2976) {
    Seen s;
    if (op == kOpNotifyHandleDeadAck) {
        if (payload.size() >= kDeadMin && is_b(rd32(payload, 0))) s.gone = rd32(payload, 0);
        return s;
    }
    if (op == kOpRemoveActorAck) {
        if (payload.size() == kRemoveSize && is_b(rd32(payload, kRemoveKey))) s.gone = rd32(payload, kRemoveKey);
        return s;
    }
    if (!user2_char) return s;
    switch (op) {
        case kOpAttackTargetAck: {
            if (payload.size() < kAttackMin) return s;
            const uint32_t target = rd32(payload, 0), attacker = rd32(payload, 4);
            if (is_b(target) && attacker == user2_char) s.engage = target;
            if (target == user2_char && is_b(attacker)) s.engage = attacker;
            if (is_b(target) && is_a(attacker)) s.fight = target;
            if (is_a(target) && is_b(attacker)) s.fight = attacker;
            return s;
        }
        case kOpStartActionAck: {
            const auto* l = movement::layout(build);
            if (!l || payload.size() != l->action) return s;
            const uint32_t key = rd32(payload, 0);
            if (!is_b(key)) return s;
            const size_t target_at = 143 + l->action_target;
            if (rd32(payload, target_at) == user2_char) s.engage = key;
            if (is_a(rd32(payload, target_at))) s.fight = key;
            s.sample = key;
            s.ts = rd64(payload, l->timestamp + movement::action_shift(true));
            s.parent = rd64(payload, l->parent + movement::action_shift(true)) != ~0ull;
            return s;
        }
        case kOpMoveActorAck: {
            const auto* l = movement::layout(build);
            if (!l || payload.size() != l->move_ack) return s;
            const uint32_t key = rd32(payload, 0);
            if (!is_b(key)) return s;
            s.sample = key;
            s.ts = rd64(payload, l->timestamp);
            s.parent = rd64(payload, l->parent) != ~0ull;
            return s;
        }
        default: return s;
    }
}

// ---------------------------------------------------------------- the engaged enemies (not thread-safe)

struct Enemy {
    uint32_t key = 0;
    uint64_t engaged_ms = 0;     // mod clock of the last engaging packet
    uint64_t fight_ms = 0;       // v2: mod clock of the last packet of it fighting any character (0 = none)
    bool has_anchor = false;
    uint64_t anchor_ts = 0;      // its last real sample's ts
    uint64_t anchor_game = 0;    // the host client's game clock when that sample went to user 2
    uint64_t last_real_ms = 0;   // mod clock
    bool parent = false;         // its last real sample had a parent
    uint64_t max_real_ts = 0;    // the highest real ts that went to user 2 (synthetics stay above it)
    int64_t offset_ms = 0;       // its last real ts - the host client's game clock at that moment
    bool clock_bad = false;      // |offset_ms| > kMaxOffsetMs on its last real sample
    // What this side sent (or, in probe, would have sent).
    bool have_sent = false;
    uint64_t sent_ts = 0;
    uint64_t sent_game = 0;
    uint64_t sent_ms = 0;        // mod clock
    float pos[3] = {};
    float quat[4] = {0, 0, 0, 1};
};

inline bool engaged_v1(const Enemy& x, uint64_t now_ms) {
    return x.key && x.engaged_ms && now_ms >= x.engaged_ms && now_ms - x.engaged_ms <= kEngageMs;
}
// v2: engaged with user 2, or fighting any character, within kEngageMs.
inline bool engaged_v2(const Enemy& x, uint64_t now_ms) {
    const uint64_t last = std::max(x.engaged_ms, x.fight_ms);
    return x.key && last && now_ms >= last && now_ms - last <= kEngageMs;
}

template <size_t N>
class Table {
public:
    // A packet engaging `key` at now_ms (mod clock). A full table replaces the least recently engaged.
    void engage(uint32_t key, uint64_t now_ms) {
        if (Enemy* e = take(key)) e->engaged_ms = now_ms ? now_ms : 1;
    }
    // v2: a packet of `key` fighting a character (not necessarily user 2's).
    void fight(uint32_t key, uint64_t now_ms) {
        if (Enemy* e = take(key)) e->fight_ms = now_ms ? now_ms : 1;
    }
    // A real sample of an engaged enemy (anything else is ignored). game_now = 0: the clock was unreadable.
    void sample(uint32_t key, uint64_t ts, uint64_t game_now, bool parent, uint64_t now_ms) {
        Enemy* e = find(key);
        if (!e) return;
        e->parent = parent;
        e->last_real_ms = now_ms ? now_ms : 1;
        if (ts > e->max_real_ts) e->max_real_ts = ts;
        if (!game_now || !ts) return;
        e->offset_ms = static_cast<int64_t>(ts) - static_cast<int64_t>(game_now);
        e->clock_bad = e->offset_ms > kMaxOffsetMs || e->offset_ms < -kMaxOffsetMs;
        if (!e->clock_bad && ts >= e->anchor_ts) {
            e->has_anchor = true;
            e->anchor_ts = ts;
            e->anchor_game = game_now;
        }
    }
    // Died or removed: forgotten (a later engaging packet starts it again, with no anchor until it moves).
    bool remove(uint32_t key) {
        Enemy* e = find(key);
        if (!e) return false;
        *e = Enemy{};
        return true;
    }
    // The enemies engaged within kEngageMs, most recent first, at most kMaxFollowed (copies).
    size_t followed(uint64_t now_ms, std::array<Enemy, kMaxFollowed>& out) const {
        std::array<const Enemy*, N> v{};
        size_t n = 0;
        for (const Enemy& x : e_) {
            if (engaged_v1(x, now_ms)) v[n++] = &x;
        }
        std::sort(v.begin(), v.begin() + static_cast<ptrdiff_t>(n),
                  [](const Enemy* a, const Enemy* b) { return a->engaged_ms > b->engaged_ms; });
        const size_t m = std::min(n, kMaxFollowed);
        for (size_t i = 0; i < m; ++i) out[i] = *v[i];
        return m;
    }
    // v2: every enemy engaged with user 2 or fighting a character within kEngageMs (copies, table order; the
    // caller keeps the kMaxFollowedV2 nearest once their poses are read).
    size_t followed_v2(uint64_t now_ms, std::array<Enemy, N>& out) const {
        size_t n = 0;
        for (const Enemy& x : e_) {
            if (engaged_v2(x, now_ms)) out[n++] = x;
        }
        return n;
    }
    // After a send (or a probe's would-send): what went, for the next ts and the keepalive.
    void sent(uint32_t key, uint64_t ts, uint64_t game_now, uint64_t now_ms, const float (&pos)[3],
              const float (&quat)[4]) {
        Enemy* e = find(key);
        if (!e) return;
        e->have_sent = true;
        e->sent_ts = ts;
        e->sent_game = game_now;
        e->sent_ms = now_ms;
        std::copy(pos, pos + 3, e->pos);
        std::copy(quat, quat + 4, e->quat);
    }
    size_t engaged(uint64_t now_ms) const {
        size_t n = 0;
        for (const Enemy& x : e_) n += engaged_v1(x, now_ms) ? 1 : 0;
        return n;
    }
    size_t engaged_v2_count(uint64_t now_ms) const {
        size_t n = 0;
        for (const Enemy& x : e_) n += engaged_v2(x, now_ms) ? 1 : 0;
        return n;
    }
    Enemy* find(uint32_t key) {
        for (Enemy& x : e_) {
            if (x.key == key) return &x;
        }
        return nullptr;
    }
    void reset() { e_ = {}; }

private:
    // The entry of `key`, made if new: a free one, else the one engaged or fighting least recently (fight_ms is 0
    // without v2: as before, the least recently engaged).
    Enemy* take(uint32_t key) {
        if (!key) return nullptr;
        if (Enemy* e = find(key)) return e;
        Enemy* e = &e_[0];
        for (Enemy& x : e_) {
            if (!x.key) {
                e = &x;
                break;
            }
            if (std::max(x.engaged_ms, x.fight_ms) < std::max(e->engaged_ms, e->fight_ms)) e = &x;
        }
        *e = Enemy{};
        e->key = key;
        return e;
    }
    std::array<Enemy, N> e_{};
};

// ---------------------------------------------------------------- one synthetic sample

enum class Skip : uint8_t {
    None,       // send
    NoAnchor,   // no real sample of it reached user 2 since it was engaged
    OldAnchor,  // its anchor is older than kAnchorMaxAgeMs of game clock
    Clock,      // the game clock did not move since the last one (paused, or the same frame)
    Parent,     // its last real sample had a parent
    Still,      // the same pose as the last one and the keepalive is not due
    ClockBad,   // its last real ts disagrees with the game clock read here by more than kMaxOffsetMs
    Stale,      // no real sample of it for kRealStaleMs
};
inline constexpr size_t kSkips = 8;
inline const char* skip_name(Skip s) {
    switch (s) {
        case Skip::None: return "sent";
        case Skip::NoAnchor: return "no anchor yet";
        case Skip::OldAnchor: return "anchor too old";
        case Skip::Clock: return "clock still";
        case Skip::Parent: return "has a parent";
        case Skip::Still: return "still";
        case Skip::ClockBad: return "ts/clock disagree";
        case Skip::Stale: return "no real sample lately";
    }
    return "?";
}

// The synthetic ts on the enemy's own clock, or 0 when none may be made now.
// Strictly above both what this side sent and every real ts of it that went to user 2 (the client's buffer
// drops a sample whose ts is not newer than its last).
inline uint64_t synth_ts(const Enemy& e, uint64_t game_now) {
    if (!e.has_anchor || game_now <= e.anchor_game) return 0;
    const uint64_t ts = e.anchor_ts + (game_now - e.anchor_game);
    if (e.have_sent && ts <= e.sent_ts) return 0;
    if (ts <= e.max_real_ts) return 0;
    return ts;
}

inline bool pose_changed(const Enemy& e, const float (&pos)[3], const float (&quat)[4]) {
    if (!e.have_sent) return true;
    const float dx = pos[0] - e.pos[0], dy = pos[1] - e.pos[1], dz = pos[2] - e.pos[2];
    if (dx * dx + dy * dy + dz * dz > 0.01f * 0.01f) return true;
    const float na = quat[0] * quat[0] + quat[1] * quat[1] + quat[2] * quat[2] + quat[3] * quat[3];
    const float nb = e.quat[0] * e.quat[0] + e.quat[1] * e.quat[1] + e.quat[2] * e.quat[2] + e.quat[3] * e.quat[3];
    if (!(na > 0) || !(nb > 0)) return true;
    const float dot = std::fabs(quat[0] * e.quat[0] + quat[1] * e.quat[1] + quat[2] * e.quat[2] + quat[3] * e.quat[3]);
    return dot / std::sqrt(na * nb) < 0.99999f;
}

// v2: changed by the game's own insert tests (0x11B1FD0 / 0x11B26E0): moved more than 1e-4 m, or the angle between
// the quaternions above 1.19e-7 rad (in double: a float change the game may still call "still" is sent, harmless).
inline bool pose_changed_v2(const Enemy& e, const float (&pos)[3], const float (&quat)[4]) {
    if (!e.have_sent) return true;
    const float dx = pos[0] - e.pos[0], dy = pos[1] - e.pos[1], dz = pos[2] - e.pos[2];
    if (dx * dx + dy * dy + dz * dz > kMovedV2M * kMovedV2M) return true;
    double na = 0, nb = 0, dot = 0;
    for (int i = 0; i < 4; ++i) {
        na += static_cast<double>(quat[i]) * quat[i];
        nb += static_cast<double>(e.quat[i]) * e.quat[i];
        dot += static_cast<double>(quat[i]) * e.quat[i];
    }
    if (!(na > 0) || !(nb > 0)) return true;
    const double c = std::min(1.0, std::fabs(dot) / std::sqrt(na * nb));
    return 2.0 * std::acos(c) > kRotatedV2Rad;
}

// How a stream samples: v1 (session 9) or v2 (the combat stream, P8 session 25).
struct Policy {
    uint64_t keepalive_ms = kKeepaliveMs;
    uint64_t keepalive_slack_ms = 0;   // due this much early (half a tick: the 3rd 33 ms tick is 99 ms)
    bool stale_cut = true;             // nothing after kRealStaleMs without a real sample
    bool game_thresholds = false;      // pose_changed_v2 instead of pose_changed (1 cm)
    bool keepalive_same_pose = false;  // a keepalive repeats the last pose sent bit for bit
};
inline constexpr Policy kPolicyV1{};
inline constexpr Policy kPolicyV2{kKeepaliveV2Ms, kTickV2Ms / 2, false, true, true};

// keepalive (optional): None because the keepalive was due and the pose did not change; with keepalive_same_pose the
// caller sends the last pose sent (e.pos / e.quat), not the one read.
inline Skip decide(const Enemy& e, uint64_t game_now, uint64_t now_ms, const float (&pos)[3], const float (&quat)[4],
                   uint64_t& ts, const Policy& p = kPolicyV1, bool* keepalive = nullptr) {
    ts = 0;
    if (keepalive) *keepalive = false;
    if (e.parent) return Skip::Parent;
    if (e.clock_bad) return Skip::ClockBad;
    if (!e.has_anchor) return Skip::NoAnchor;
    if (p.stale_cut && (!e.last_real_ms || now_ms < e.last_real_ms || now_ms - e.last_real_ms > kRealStaleMs)) {
        return Skip::Stale;
    }
    if (game_now > e.anchor_game && game_now - e.anchor_game > kAnchorMaxAgeMs) return Skip::OldAnchor;
    ts = synth_ts(e, game_now);
    if (!ts) return Skip::Clock;
    const bool due =
        !e.have_sent || now_ms < e.sent_ms || now_ms - e.sent_ms + p.keepalive_slack_ms >= p.keepalive_ms;
    const bool changed = p.game_thresholds ? pose_changed_v2(e, pos, quat) : pose_changed(e, pos, quat);
    if (!changed && !due) {
        ts = 0;
        return Skip::Still;
    }
    if (keepalive) *keepalive = !changed;
    return Skip::None;
}

// ---------------------------------------------------------------- v2: schedule and choice

// The next tick of a drift-free schedule: one period after the deadline just served, or, when the thread is a whole
// period behind it (held up), one period from now: no burst of catch-up ticks. late: it was behind.
inline uint64_t next_deadline(uint64_t due, uint64_t now, uint64_t period, bool* late = nullptr) {
    const uint64_t next = due + period;
    const bool behind = now >= next;
    if (late) *late = behind;
    return behind ? now + period : next;
}

// One readable candidate of a tick: its index in the tick's list and its distance to user 2's character.
struct Near {
    size_t index = 0;
    float dist = 0;
};
// The nearest first (ties: list order); returns how many are kept (at most `keep`).
template <size_t M>
size_t nearest_first(std::array<Near, M>& c, size_t n, size_t keep) {
    n = std::min(n, M);
    std::sort(c.begin(), c.begin() + static_cast<ptrdiff_t>(n), [](const Near& a, const Near& b) {
        return a.dist < b.dist || (a.dist == b.dist && a.index < b.index);
    });
    return std::min(n, keep);
}

inline float distance(const float (&a)[3], const float (&b)[3]) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// The velocity from the last pose sent and this one over the game clock (0 when unknown or absurd).
inline void velocity(const Enemy& e, const float (&pos)[3], uint64_t game_now, float (&v)[3]) {
    v[0] = v[1] = v[2] = 0;
    if (!e.have_sent || game_now <= e.sent_game || game_now - e.sent_game > 500) return;
    const float dt = static_cast<float>(game_now - e.sent_game) / 1000.0f;
    for (int i = 0; i < 3; ++i) v[i] = (pos[i] - e.pos[i]) / dt;
    const float s = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!std::isfinite(s) || s > 30.0f) v[0] = v[1] = v[2] = 0;
}

inline bool sane_pose(const float (&pos)[3], const float (&quat)[4]) {
    for (const float x : pos) {
        if (!std::isfinite(x) || std::fabs(x) > 1.0e6f) return false;
    }
    const float n = quat[0] * quat[0] + quat[1] * quat[1] + quat[2] * quat[2] + quat[3] * quat[3];
    return std::isfinite(n) && std::fabs(n - 1.0f) <= 0.02f;
}

inline constexpr size_t kPacketSize = kHeader + kMoveAck;

inline void put(uint8_t* p, size_t at, const void* v, size_t n) { std::memcpy(p + at, v, n); }

inline std::array<uint8_t, kPacketSize> make_move_ack(uint32_t key, const float (&pos)[3], const float (&quat)[4],
                                                     const float (&vel)[3], uint64_t ts) {
    std::array<uint8_t, kPacketSize> pk{};
    pk[0] = static_cast<uint8_t>(kOpMoveActorAck & 0xFF);
    pk[1] = static_cast<uint8_t>(kOpMoveActorAck >> 8);
    pk[2] = 0xFF;  // slot -1, as the server writes them
    pk[3] = static_cast<uint8_t>(kMoveAck & 0xFF);
    pk[4] = static_cast<uint8_t>(kMoveAck >> 8);
    uint8_t* p = pk.data() + kHeader;
    const float ident[4] = {0, 0, 0, 1};
    const float scale[3] = {1, 1, 1};
    put(p, 0x00, &key, 4);
    put(p, 0x04, pos, 12);
    put(p, 0x10, quat, 16);
    // +0x20 u32 0; +0x24 Transform identity
    put(p, 0x30, ident, 16);
    put(p, 0x40, scale, 12);
    // +0x4C Path.local = the world pose, scale 1
    put(p, 0x4C, pos, 12);
    put(p, 0x58, quat, 16);
    put(p, 0x68, scale, 12);
    put(p, 0x74, vel, 12);
    // +0x80 vec3 0
    std::memset(p + 0x8C, 0xFF, 8);  // parent -1
    // +0x94 Path.world: pos 0, quat identity, scale 1
    put(p, 0xA0, ident, 16);
    put(p, 0xB0, scale, 12);
    // +0xBC, +0xC8 vec3 0; +0xD4 u8 0
    put(p, 0xD5, &ts, 8);
    // +0xDD u8 0, +0xDE u8 0, +0xDF f32 0, f32 0
    const float speed = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
    put(p, 0xE7, &speed, 4);
    return pk;
}

// 1.0.0.3527 (tools/porting/data/payload_abi_3527.json, movement_offset_comparison): the same fields in another order.
// +0x00 key | +4 Path.local pos, quat, scale | +44 velocity | +56 angular | +68 parent | +76 Path.world pos, quat, scale
// | +116, +128 vec3 | +140 Transform pos, quat, scale | +180 quat | +196 pos | +208 aux | +212 ts | +220 u32
// | +224, +225 flags | +226 server flag | +227 f32, f32 | +235 speed.
inline constexpr size_t kPacketSize3527 = kHeader + 239;
inline std::array<uint8_t, kPacketSize3527> make_move_ack_3527(uint32_t key, const float (&pos)[3], const float (&quat)[4],
                                                               const float (&vel)[3], uint64_t ts) {
    std::array<uint8_t, kPacketSize3527> pk{};
    pk[0] = static_cast<uint8_t>(kOpMoveActorAck & 0xFF);
    pk[1] = static_cast<uint8_t>(kOpMoveActorAck >> 8);
    pk[2] = 0xFF;
    pk[3] = 239;
    pk[4] = 0;
    uint8_t* p = pk.data() + kHeader;
    const float ident[4] = {0, 0, 0, 1};
    const float scale[3] = {1, 1, 1};
    put(p, 0, &key, 4);
    put(p, 4, pos, 12);
    put(p, 16, quat, 16);
    put(p, 32, scale, 12);
    put(p, 44, vel, 12);
    std::memset(p + 68, 0xFF, 8);
    put(p, 88, ident, 16);
    put(p, 104, scale, 12);
    put(p, 152, ident, 16);
    put(p, 168, scale, 12);
    put(p, 180, quat, 16);
    put(p, 196, pos, 12);
    put(p, 212, &ts, 8);
    const float speed = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
    put(p, 235, &speed, 4);
    return pk;
}

}  // namespace cdmp::session2::cfl
