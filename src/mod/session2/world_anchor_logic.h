#pragma once
// [session2] user2_world_anchor = 0 (default) | probe | 1, HOST only (create_session=1)
// (docs/research/noche/mundo-invitado.md, section "Implementado"): the pure logic of world_anchor.cpp, with no game
// access, so the tests can run it.
//
// The fault [V/E, build 1.0.0.2976]: the server's actor collector (pa::TrocTrActorGarbageCollectionRepeatTimer ->
// 0x2C37340, every 5 s) removes an NPC (RemoveActorAck 2516, reason 25) once it is out of its spawn radius
// (~200-230 m) from BOTH of its anchors: the field owner's character (the host) and the actor [field+0x2E8] when the
// byte [field+0x2E4] != 0 (0x2C38E28; the native "world observer", free in normal play). User 2 is no anchor: what
// surrounds it dies once it is ~250 m from the host.
//
// The fix: that second anchor is user 2's main character while user 2 stands in a field the host owns. The two slot
// fields are written DIRECTLY, never with the native UpdateWorldObserverStateReq 2245: its handler (0x29C2780) also
// calls 0x2836A20 on the field's ServerSyncSceneObjectManager ([field+0x80]), which empties and refills a set at
// manager+0x278 with the scene objects of its table +0x60 whose +0xB0 == -1 and +0x183/+0x187 != 0; only
// WorldObserverDisableLoadCompleteReq 3060 (0x29C2AB0, with the slot off) and the destructor empty it again, and no
// reader of it was found (static scan): an unknown side effect, so the report's alternative. The write is the
// handler's own (0x29C2915: byte +0x2E4, dword +0x2E8), on ServerRunThread (the OnRecvPacket hook that exists), under
// the field's own lock [field+8] (pa::WindowsRWLock<4,1>): its TryLock vf+0x28 (exclusive, never waits) and its
// Unlock vf+0x20, the same SRW lock the handler (vf+8/+0x10) and the collector (shared, 0x393DF0) take.
//
// When (all on ServerRunThread, evaluated at most every kWaitCheckMs while nothing is set, kWatchdogMs once set):
//   set    user 2 exists, its logout was not asked for, its move phase is Stable (its last load ended with the 2762
//          the server wrote for it) and that 2762 is newer than the last "leave" below, its main character key is
//          known (class 0xA, = its MainPlayerDataStartAck) and that character stands in an open field the host owns
//          ([field+0x180] = the host's user key: the shared field, or the old one after the host travelled away);
//          a slot holding another key that is not ours (a native world observer) is never taken.
//   leave  user 2's GameLoadingStartReq 2437, ReloadStageStartReq 2283 or LogoutSessionToServerReq 2001 reaches the
//          server: the slot is cleared first (at that packet's OnRecvPacket, before its handler runs) and the next set
//          waits for a newer 2762.
//   clear  the conditions no longer hold (user 2 gone without a 2001 = the fallback clear, another field, logout
//          asked...): cleared only while it still holds {1, our key} in the same field generation (SAM::FindActor has
//          no generation check: a stale key could anchor another actor).
//   watch  once set, the slot is read back every kWatchdogMs: lost (not {1, key}) -> set again (at most one per
//          kResetMinMs); held by a foreign key -> left alone (counted); the field closed (same generation) -> cleared
//          like any clear; the field reused (another generation) -> forgotten: Pool<ServerField>::Acquire (.xtls
//          0x1066CDB0) runs ServerField::Reset 0x2AF7360 on every field it hands out (generation + 1 under the
//          field's lock, then +0x2E4 = 0 at 0x2AF74B5).
// When it runs: only inside ServerNet::OnRecvPacket (ServerRunThread pops Q0 with a 1 s timeout and calls it per
// packet; the timers run on another thread): for the host's session as well as user 2's. The host client's own C>S
// keep it turning in play (S16/S22 host captures: after the login, gaps <= 1.8 s / <= 5.0 s, a 2469 every 5 s while
// idle; only its own loading screens are longer), so the watchdog and the fallback clear wait <= ~5 s, not for user
// 2's traffic.
// Field lifetime [E]: fields are pooled and reused in place (the pool's free list); the only free is the deleting
// destructor vf0 0x2AF6A40 (no direct caller: the pool's teardown with the server). [field+8] = field+0x1B8 is set
// once by the constructor (0x1434920) and never rewritten, so reading it and calling its TryLock is safe on any
// generation; open (+0x4A) and generation (+0x2C8) are checked again under the lock: a pooled field is closed
// (0x2AFEA00 sets +0x4A = 0; Reset with r8 = 0 does not reopen it), so a set never lands in a field being reused.
// probe: the same decisions, logged as "would", nothing written; the slot is only read.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

#include "mod/session2/user2_funnels_logic.h"

namespace cdmp::session2::wal {

using u2f::Mode;
using u2f::mode_name;
using u2f::parse_mode;

// ---------------------------------------------------------------- build 1.0.0.2976 (RVAs, checked on the exe)

inline constexpr uintptr_t kVtServerField = 0x5B29A60;  // pa::ServerField
inline constexpr uintptr_t kVtFieldLock = 0x57A30B8;    // pa::WindowsRWLock<4,1>, [field+8] (= field+0x1B8)
inline constexpr uint32_t kLockSlotUnlock = 0x20;       // vf4 0x1371B70 (u8 shared): exclusive -> owner -1, Release
inline constexpr uint32_t kLockSlotTry = 0x28;          // vf5 0x1371BA0 (u8 shared): TryAcquireSRWLockExclusive
inline constexpr uintptr_t kLockUnlock = 0x1371B70, kLockTry = 0x1371BA0;
inline constexpr uint32_t kFieldLock = 0x08, kFieldOpen = 0x4A, kFieldOwner = 0x180, kFieldGen = 0x2C8;
inline constexpr uint32_t kFieldSlotOn = 0x2E4;   // u8, 0x29C2915 / collector 0x2C38E28
inline constexpr uint32_t kFieldSlotKey = 0x2E8;  // u32, 0x29C291C / collector 0x2B06090
inline constexpr uint32_t kUserKey = 0x60, kUserMainCharacter = 0xD0, kUserCurrentCharacter = 0xD8;
inline constexpr uint32_t kActorKey = 0x60, kActorComponents = 0x68, kComponentsTs = 0x1A0;
inline constexpr uint32_t kTsField = 0x4F0 + 0x10;  // FieldMover (TS+0x4F0) +0x10: ServerField*
inline constexpr uint32_t kCharClass = 0xA;         // player characters (key >> 28)

inline constexpr uint16_t kOpGameLoadingStartReq = 2437;
inline constexpr uint16_t kOpReloadStageStartReq = 2283;
inline constexpr uint16_t kOpLogoutSessionReq = 2001;
inline constexpr uint16_t kOpRemoveActorAck = 2516;
inline constexpr uint8_t kReasonCollector = 25;
// The slot's other reader on the server [E]: 0x2826F70 (0x2827094 reads [field+0x2E4]) skips its own level-box check
// (0x184F360) while the slot is on, for UpdateSyncSceneObjectOutOfLevelBoundaryReq 2573 (handler 0x29C31C0 ->
// 0x282EE90 -> 0x28256F0 with the flag 1): the client's report of a scene object out of its level is trusted. Counted.
inline constexpr uint16_t kOpOutOfLevelBoundaryReq = 2573;

inline constexpr uint64_t kWaitCheckMs = 500;   // nothing set: how often the conditions are read
inline constexpr uint64_t kWatchdogMs = 5000;   // set: how often the slot is read back
inline constexpr uint64_t kResetMinMs = 5000;   // lost: at most one new set per this
inline constexpr float kNearM = 150.0f;         // a collector removal "near user 2" (2D)
inline constexpr size_t kOurKeys = 8;

inline constexpr bool leaves_field(uint16_t op) {
    return op == kOpGameLoadingStartReq || op == kOpReloadStageStartReq || op == kOpLogoutSessionReq;
}

// ---------------------------------------------------------------- what is read

struct Slot {
    uint8_t on = 0;
    uint32_t key = 0;
    friend bool operator==(const Slot&, const Slot&) = default;
};

// A ServerField as read (all or nothing).
struct FieldView {
    bool ok = false;
    uintptr_t field = 0;
    uint32_t gen = 0;  // +0x2C8 (index, pool, generation)
    uint8_t open = 0;  // +0x4A
    uint32_t owner = 0;  // +0x180, the owner's user key
    Slot slot;
};

// One evaluation's readings (ServerRunThread).
struct Inputs {
    uint64_t now_ms = 0;
    bool user2 = false;   // user 2 of the live session 2 exists
    bool logout = false;  // its logout was asked for
    bool stable = false;  // its move phase is Stable
    uint32_t loads = 0;   // the 2762s the server wrote for it in this epoch
    uint32_t key = 0;     // its main character key, class 0xA and = its MainPlayerDataStartAck (0 = not that)
    uint32_t host_user = 0;  // the host's user key
    FieldView f2;            // the field user 2's main character stands in
    FieldView owned;         // the field of the slot this side holds, read again now (ok = false: none / unreadable)
};

// ---------------------------------------------------------------- what is decided

enum class Why : uint8_t {
    None,         // wanted
    NoUser2,      // user 2 is gone (no live session 2, no user): the fallback clear
    Logout,       // its logout was asked for
    NotStable,    // its move phase is not Stable (a load runs)
    NotArmed,     // no 2762 newer than the last leave (2437/2283/2001) yet
    NoKey,        // its main character key is unknown or not its MainPlayerDataStartAck's
    FieldUnread,  // its character's field could not be read
    FieldClosed,  // that field is not open
    NotHostField, // that field is not the host's (user 2's own)
    Moved,        // wanted, but in another field / with another key than the slot held
    FieldReset,   // the held slot's field was reused (another generation: the pool reset cleared the slot)
    NotOurs,      // the held slot no longer holds our key (someone else wrote it): not ours to clear
    Busy,         // the slot holds a key that is not ours (a native world observer?): left alone
    First,        // the first set
    Lost,         // set, but the slot no longer holds it
    Leave,        // user 2's 2437 / 2283 / 2001
    Count
};
inline const char* why_name(Why w) {
    switch (w) {
        case Why::None: return "wanted";
        case Why::NoUser2: return "user 2 is gone";
        case Why::Logout: return "user 2's logout was asked for";
        case Why::NotStable: return "user 2 is loading";
        case Why::NotArmed: return "no 2762 since its last leave";
        case Why::NoKey: return "user 2's character key unknown";
        case Why::FieldUnread: return "its field unreadable";
        case Why::FieldClosed: return "its field is not open";
        case Why::NotHostField: return "its field is not the host's";
        case Why::Moved: return "user 2 moved to another field / character";
        case Why::FieldReset: return "the field was reused";
        case Why::NotOurs: return "the slot no longer holds our key";
        case Why::Busy: return "the slot holds a key that is not ours";
        case Why::First: return "first set";
        case Why::Lost: return "lost";
        case Why::Leave: return "user 2 leaves the field";
        case Why::Count: break;
    }
    return "?";
}

enum class Act : uint8_t { None, Set, Clear, Forget };
inline const char* act_name(Act a) {
    switch (a) {
        case Act::None: return "none";
        case Act::Set: return "set";
        case Act::Clear: return "clear";
        case Act::Forget: return "forget";
    }
    return "?";
}

struct Decision {
    Act act = Act::None;
    Why why = Why::None;
    uintptr_t field = 0;  // Set: {1, key} into it; Clear: {0, 0}, only if it still holds {1, key}
    uint32_t gen = 0;     // ... and only while +0x2C8 is still this and the field is open
    uint32_t key = 0;
    Slot found;              // what the slot held when decided
    uint16_t leave_op = 0;   // Leave: 2437 / 2283 / 2001
    bool fallback = false;   // Clear because user 2 is gone (no 2001 handled)
    bool verified = false;   // None: the first watchdog read found the slot as written
    uint64_t since_ms = 0;   // verified: ms since it was written
};

// What the write did (world_anchor.cpp; probe reports Written for a "would").
enum class Result : uint8_t {
    Written,  // the slot now holds what was asked
    Stale,    // the field / slot was not what the decision assumed: nothing written
    Retry,    // the lock was busy (TryLock failed): nothing written, try again at the next packet
};

// ---------------------------------------------------------------- the state machine (ServerRunThread only)

class Anchor {
public:
    struct Owned {
        uintptr_t field = 0;
        uint32_t gen = 0;
        uint32_t key = 0;
        uint64_t since_ms = 0;
        bool verified = false;
    };

    Anchor() = default;
    explicit Anchor(Mode m) : mode_(m) {}

    Mode mode() const { return mode_; }
    bool probe() const { return mode_ == Mode::Probe; }
    bool due(uint64_t now_ms) const { return mode_ != Mode::Off && now_ms >= next_ms_; }
    const std::optional<Owned>& owned() const { return owned_; }
    uint32_t armed_after() const { return suspended_loads_; }
    const std::array<uint32_t, kOurKeys>& our_keys() const { return ours_; }
    bool ours(uint32_t key) const {
        return key && std::find(ours_.begin(), ours_.end(), key) != ours_.end();
    }

    // Why the anchor is not wanted now (Why::None = wanted).
    Why wanted(const Inputs& in) const {
        if (!in.user2) return Why::NoUser2;
        if (in.logout) return Why::Logout;
        if (!in.stable) return Why::NotStable;
        if (in.loads <= suspended_loads_) return Why::NotArmed;
        if (!in.key || (in.key >> 28) != kCharClass) return Why::NoKey;
        if (!in.f2.ok) return Why::FieldUnread;
        if (in.f2.open != 1) return Why::FieldClosed;
        if (!in.host_user || in.f2.owner != in.host_user) return Why::NotHostField;
        return Why::None;
    }

    // User 2's 2437 / 2283 / 2001 reaches the server (its OnRecvPacket): the slot is cleared first and the next set
    // waits for a 2762 newer than `loads`.
    Decision on_leave(uint16_t op, uint32_t loads, uint64_t now_ms) {
        Decision d;
        if (mode_ == Mode::Off) return d;
        suspended_loads_ = std::max(suspended_loads_, loads);
        last_loads_ = std::max(last_loads_, loads);
        d.why = Why::Leave;
        d.leave_op = op;
        next_ms_ = now_ms;
        if (!owned_) return d;
        d.act = Act::Clear;
        d.field = owned_->field;
        d.gen = owned_->gen;
        d.key = owned_->key;
        return d;
    }

    Decision step(const Inputs& in) {
        Decision d;
        if (mode_ == Mode::Off) return d;
        // The phase tracker starts again from nothing (a session 2 rebuilt in the same epoch): so do the leaves.
        if (in.loads < last_loads_) suspended_loads_ = 0;
        last_loads_ = in.loads;
        const Why want = wanted(in);
        d.why = want;
        if (owned_) {
            Owned& o = *owned_;
            next_ms_ = in.now_ms + kWatchdogMs;
            d.field = o.field;
            d.gen = o.gen;
            d.key = o.key;
            d.found = in.owned.slot;
            // Reused (another generation) or unreadable: Reset cleared the slot; nothing of ours is left there. A
            // field only closed (same generation) is cleared below like any other (not wanted any more).
            if (!probe() && (!in.owned.ok || in.owned.field != o.field || in.owned.gen != o.gen)) {
                d.act = Act::Forget;
                d.why = Why::FieldReset;
                owned_.reset();
                next_ms_ = in.now_ms;
                return d;
            }
            const bool keep = want == Why::None && in.f2.field == o.field && in.f2.gen == o.gen && in.key == o.key;
            if (keep) {
                if (probe()) return d;
                const Slot mine{1, o.key};
                if (in.owned.slot == mine) {
                    if (!o.verified) {
                        o.verified = true;
                        d.verified = true;
                        d.since_ms = in.now_ms - o.since_ms;
                    }
                    return d;
                }
                if (in.owned.slot.on && in.owned.slot.key && in.owned.slot.key != o.key && !ours(in.owned.slot.key)) {
                    d.why = Why::Busy;
                    return d;
                }
                d.why = Why::Lost;
                if (in.now_ms - last_write_ms_ < kResetMinMs) {
                    next_ms_ = last_write_ms_ + kResetMinMs;
                    return d;
                }
                d.act = Act::Set;
                return d;
            }
            if (want == Why::None) d.why = Why::Moved;
            next_ms_ = in.now_ms;
            if (probe() || in.owned.slot == Slot{1, o.key}) {
                d.act = Act::Clear;
                d.fallback = want == Why::NoUser2;
                return d;
            }
            d.act = Act::Forget;
            d.why = Why::NotOurs;
            owned_.reset();
            return d;
        }
        next_ms_ = in.now_ms + kWaitCheckMs;
        if (want != Why::None) return d;
        d.field = in.f2.field;
        d.gen = in.f2.gen;
        d.key = in.key;
        d.found = in.f2.slot;
        if (in.f2.slot.on && in.f2.slot.key && in.f2.slot.key != in.key && !ours(in.f2.slot.key)) {
            d.why = Why::Busy;
            return d;
        }
        d.act = Act::Set;
        d.why = Why::First;
        return d;
    }

    // The outcome of a Set / Clear (probe: Written for a "would").
    void applied(const Decision& d, Result r, uint64_t now_ms) {
        if (d.act == Act::Set) {
            if (r == Result::Written) {
                // A set again after a loss is verified again too (its own "ok after N ms").
                owned_ = Owned{d.field, d.gen, d.key, now_ms, false};
                last_write_ms_ = now_ms;
                remember(d.key);
                next_ms_ = now_ms + kWatchdogMs;
            } else {
                next_ms_ = r == Result::Retry ? now_ms : now_ms + kWaitCheckMs;
            }
        } else if (d.act == Act::Clear) {
            if (r == Result::Retry) {
                next_ms_ = now_ms;  // owned_ kept: cleared at the next packet
            } else {
                owned_.reset();
                next_ms_ = now_ms;
            }
        }
    }

    // A new guest epoch: its 2762s count from 0 (the slot held, if any, is cleared by the next step).
    void on_epoch() {
        suspended_loads_ = 0;
        last_loads_ = 0;
        next_ms_ = 0;
    }
    // The server instance was replaced: its fields are not ours to touch any more.
    void forget() {
        owned_.reset();
        on_epoch();
    }

private:
    void remember(uint32_t key) {
        if (ours(key)) return;
        ours_[next_our_ % kOurKeys] = key;
        ++next_our_;
    }

    Mode mode_ = Mode::Off;
    std::optional<Owned> owned_;
    uint32_t suspended_loads_ = 0;  // a set needs loads > this
    uint32_t last_loads_ = 0;
    uint64_t next_ms_ = 0;
    uint64_t last_write_ms_ = 0;
    std::array<uint32_t, kOurKeys> ours_{};  // keys written in this run (a slot holding one is ours, not foreign)
    size_t next_our_ = 0;
};

// ---------------------------------------------------------------- the probe: the collector's removals near user 2

// RemoveActorAck 2516 [V, every capture of P8]: u8 04 | u32 | u8 reason | n x u32 key (n by the size; 10 B for one).
// Every key is read (as family_order_logic.h's on_2516); the view is valid while the payload is.
struct Removal {
    bool ok = false;
    uint8_t reason = 0;
    size_t total = 0;  // keys in the packet
    std::span<const uint8_t> payload;
    uint32_t key(size_t i) const {
        uint32_t k = 0;
        if (i < total) std::memcpy(&k, payload.data() + 6 + 4 * i, 4);
        return k;
    }
};
inline Removal read_remove(std::span<const uint8_t> payload) {
    Removal r;
    if (payload.size() < 10 || (payload.size() - 6) % 4 != 0 || payload[0] != 4) return r;
    r.reason = payload[5];
    r.total = (payload.size() - 6) / 4;
    r.payload = payload;
    r.ok = true;
    return r;
}

// Horizontal (x, z) distance, as the report measured it.
inline float dist2d(const float (&a)[3], const float (&b)[3]) {
    const float dx = a[0] - b[0], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dz * dz);
}

// The reason-25 removals of 0xB actors user 2 was sent, with their distances (not thread-safe: the caller locks).
class RemovalStats {
public:
    static constexpr uint64_t kPassGapMs = 1500;  // removals further apart than this are another pass (5 s apart)
    static constexpr size_t kKept = 256;

    struct Summary {
        uint64_t removals = 0, passes = 0, unresolved = 0, other_reason = 0;
        uint64_t near_user2 = 0;      // within kNearM of user 2
        uint64_t near_far_host = 0;   // ... and beyond kNearM from the host (the fault's signature)
        float near_host_min = 0, near_host_med = 0, near_host_max = 0;  // d(host) of the near ones
        float near_u2_max = 0;
        float all_host_med = 0;       // d(host) of every resolved one
        float players_max = 0;        // the largest host - user 2 distance seen with a removal
    };

    // npc / host / user2: nullptr = unknown. A pass that spans the minute's edge counts in the minute it began.
    void add(uint64_t now_ms, const float (*npc)[3], const float (*host)[3], const float (*user2)[3]) {
        if (!last_ms_ || now_ms - last_ms_ > kPassGapMs) ++passes_;
        last_ms_ = now_ms ? now_ms : 1;
        ++removals_;
        if (!npc || !host || !user2) {
            ++unresolved_;
            return;
        }
        const float dh = dist2d(*npc, *host), du = dist2d(*npc, *user2);
        players_max_ = std::max(players_max_, dist2d(*host, *user2));
        if (all_n_ < kKept) all_host_[all_n_++] = dh;
        if (du < kNearM) {
            ++near_;
            if (dh >= kNearM) ++near_far_host_;
            near_u2_max_ = std::max(near_u2_max_, du);
            if (near_n_ < kKept) near_host_[near_n_++] = dh;
        }
    }
    void other_reason() { ++other_; }

    Summary take() {
        Summary s;
        s.removals = removals_;
        s.passes = passes_;
        s.unresolved = unresolved_;
        s.other_reason = other_;
        s.near_user2 = near_;
        s.near_far_host = near_far_host_;
        s.near_u2_max = near_u2_max_;
        s.players_max = players_max_;
        if (near_n_) {
            std::sort(near_host_.begin(), near_host_.begin() + near_n_);
            s.near_host_min = near_host_[0];
            s.near_host_max = near_host_[near_n_ - 1];
            s.near_host_med = near_host_[near_n_ / 2];
        }
        if (all_n_) {
            std::sort(all_host_.begin(), all_host_.begin() + all_n_);
            s.all_host_med = all_host_[all_n_ / 2];
        }
        const uint64_t keep_last = last_ms_;
        *this = RemovalStats{};
        last_ms_ = keep_last;  // the pass that spans the edge is not counted again
        return s;
    }

private:
    uint64_t last_ms_ = 0;
    uint64_t removals_ = 0, passes_ = 0, unresolved_ = 0, other_ = 0, near_ = 0, near_far_host_ = 0;
    float near_u2_max_ = 0, players_max_ = 0;
    std::array<float, kKept> near_host_{}, all_host_{};
    size_t near_n_ = 0, all_n_ = 0;
};

}  // namespace cdmp::session2::wal
