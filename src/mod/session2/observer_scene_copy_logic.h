#pragma once
// P8, the scenes user 2 starts in the host's field (docs/research/pendientes/p8-escenas-y-acks.md
// §2; p8-sesion6.md §4): [session2] observer_scene_copy=1, HOST. The pure logic, with no game
// access, so the tests can run it on the packets of the captures.
//
// The fault [V] (session 6B): user 2's ESC "view item" (UseItemReq 2855) makes the server start a
// sequencer whose OWNER is user 2's character (0x1A4: 2981 and 2661 to A010031F, play data +0 =
// A010031F, binding 0x20000002 -> A010031F), but it stores it in the stage manager of the field's
// OWNER (the host). Its 2981, 2661, 2111 and 2524 reach user 2 (ToTarget its character, or a near
// finder), yet the closing DeleteSequencerAck 2810 is built at 0x2879819 with rcx = [manager+0x18]
// and a ToTarget for [manager+0x18]->vfE8(): the host's Kliff A0100001. User 2's client never
// closes the scene and keeps the camera without control.
//
// What it does: when the server deletes a sequencer whose owner ([seq+0x98]) is user 2's
// character, whose 2661 reached user 2's queue in this trip, and whose native 2810 goes to
// another character, the native emitter 0x2A4E4C0 runs once more on that same thread with user
// 2's character and a ToTarget for its session: user 2 gets the 2810 its own client expects (the
// actor key of its 2661/2981, the same sequencer id, u32 and u8), routed by its client to the
// manager its 2661 went to (0xC06330 -> 0x17FA6C0 -> ClientField::vf48 0x9A5BE0). The host's own
// scenes are never copied; the host's 2810 is not touched.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace cdmp::session2::osc {

inline constexpr uint16_t kOpPrepare = 2661;    // PrepareSequencerAck
inline constexpr uint16_t kOpDelete = 2810;     // DeleteSequencerAck
inline constexpr uint16_t kOpMoveField = 2174;  // MoveFieldAck: a new trip (the client's field changes)
inline constexpr size_t kPreparePrefix = 91;    // u8 kind | u32 actor | 86-byte play data (owner +0, seq +8)
inline constexpr size_t kBindingWire = 11;
inline constexpr size_t kDeletePayload = 17;    // u32 actor | u64 seq | u32 | u8

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

// A sequencer whose 2661 reached user 2's queue.
struct Delivered {
    uint64_t seq = 0;
    uint32_t actor = 0;  // the 2661's addressed actor (+1)
    uint32_t owner = 0;  // its play data +0 (+5)
    uint32_t epoch = 0, trip = 0;
    uint64_t t_us = 0;
};

// The 2661s that reached user 2's queue (take_frame, after the push), per sequencer id, until
// its 2810 reaches it too (native or our copy). Bounded; a new guest epoch empties it; a
// MoveFieldAck 2174 starts a trip (what came before stays, marked by its trip).
class SceneBook {
public:
    explicit SceneBook(size_t capacity = 256) : cap_(capacity) {}

    enum class Seen : uint8_t { Other, Prepared, Replaced, Closed, ClosedUnknown, Trip };

    void on_epoch(uint32_t epoch) {
        v_.clear();
        epoch_ = epoch;
        trip_ = 0;
    }
    uint32_t epoch() const { return epoch_; }
    uint32_t trip() const { return trip_; }

    // A packet that WAS pushed to user 2's queue, in guest epoch `epoch` (another epoch than the
    // book's: the book starts again).
    Seen on_out(uint16_t opcode, std::span<const uint8_t> payload, uint64_t now_us, uint32_t epoch,
                Delivered* what = nullptr) {
        if (epoch != epoch_) on_epoch(epoch);
        if (opcode == kOpMoveField) {
            ++trip_;
            return Seen::Trip;
        }
        if (opcode == kOpPrepare) {
            if (payload.size() < kPreparePrefix || (payload.size() - kPreparePrefix) % kBindingWire) return Seen::Other;
            Delivered d;
            d.actor = le32(&payload[1]);
            d.owner = le32(&payload[5]);
            d.seq = le64(&payload[9]);
            d.epoch = epoch_;
            d.trip = trip_;
            d.t_us = now_us;
            if (what) *what = d;
            for (Delivered& x : v_) {
                if (x.seq == d.seq) {
                    x = d;
                    ++replaced_;
                    return Seen::Replaced;
                }
            }
            if (v_.size() >= cap_) {
                v_.erase(v_.begin());  // the oldest
                ++evicted_;
            }
            v_.push_back(d);
            ++prepared_;
            return Seen::Prepared;
        }
        if (opcode == kOpDelete) {
            if (payload.size() != kDeletePayload) return Seen::Other;
            const uint64_t seq = le64(&payload[4]);
            for (size_t i = 0; i < v_.size(); ++i) {
                if (v_[i].seq != seq) continue;
                if (what) *what = v_[i];
                v_.erase(v_.begin() + static_cast<std::ptrdiff_t>(i));
                ++closed_;
                return Seen::Closed;
            }
            ++closed_unknown_;
            return Seen::ClosedUnknown;
        }
        return Seen::Other;
    }

    std::optional<Delivered> find(uint64_t seq) const {
        for (const Delivered& d : v_) {
            if (d.seq == seq) return d;
        }
        return std::nullopt;
    }
    size_t live() const { return v_.size(); }
    uint64_t prepared() const { return prepared_; }
    uint64_t replaced() const { return replaced_; }
    uint64_t closed() const { return closed_; }
    uint64_t closed_unknown() const { return closed_unknown_; }
    uint64_t evicted() const { return evicted_; }

private:
    size_t cap_;
    std::vector<Delivered> v_;
    uint32_t epoch_ = 0, trip_ = 0;
    uint64_t prepared_ = 0, replaced_ = 0, closed_ = 0, closed_unknown_ = 0, evicted_ = 0;
};

// What the decision at the 2810 serializer's entry reads (observer_scene_copy.cpp fills it with SEH
// reads on the server's logic thread; the tests by hand).
struct CopyFacts {
    bool on = false;          // observer_scene_copy=1, code verified, not faulted
    bool reentry = false;     // our own call of the emitter, seen by its hook
    bool from_delete = false; // return address == 0x287981E (the only call, in 0x2879440)
    bool consistent = false;  // [r8] == [rsi+0x30] (the sequencer's id) and rcx == [r12+0x18]
    uint64_t seq = 0;
    uint32_t owner_key = 0;      // u32 [rsi+0x98]: the sequencer's owner (0x205D7C0 looks it up)
    uint32_t recipient_key = 0;  // [rcx+0x60]: the native 2810's actor ([manager+0x18])
    uint32_t user2_key = 0;      // user 2's character (its MainPlayerDataStartAck)
    bool live_session = false;   // a live session 2 of the current epoch
    bool logout = false;         // user 2's logout was asked for
    uint32_t epoch = 0;          // the current guest epoch
    std::optional<Delivered> delivered;  // its 2661 in user 2's queue (SceneBook)
    uint32_t book_epoch = 0, book_trip = 0;
    bool realm = false;          // TLS +0x1EC (the server's realm)
    bool send_ctx = false;       // TLS +0x250
    bool scope_open = false;     // +0x2AC && +0x2A0: a native scope sender is open here
    bool actor_ok = false;       // user 2's character: [user+0xD0], key == user2_key, [actor+0xA0] == user
};

enum class Why : uint8_t {
    Copy,
    Off,
    Reentry,
    NotDeleteSite,
    Inconsistent,
    NoUser2,
    NotUser2Scene,   // the sequencer's owner is not user 2's character (the host's scenes: never copied)
    AlreadyToUser2,  // the native 2810 already goes to user 2's character (user 2 owns the manager)
    LogoutAsked,
    NotDelivered,    // its 2661 never reached user 2's queue
    OtherEpoch,
    OtherTrip,       // its 2661 reached user 2 before its last MoveFieldAck
    OwnerMismatch,   // the 2661's owner/actor is not user 2's character
    NoRealm,
    NoSendCtx,
    ScopeOpen,
    NoActor,
    Count
};

inline const char* why_name(Why w) {
    switch (w) {
        case Why::Copy: return "COPY";
        case Why::Off: return "off";
        case Why::Reentry: return "our own call";
        case Why::NotDeleteSite: return "not called from 0x2879819 (DeleteSequencer 0x2879440)";
        case Why::Inconsistent: return "the frame is not the expected one ([r8] != [rsi+0x30] or rcx != [r12+0x18])";
        case Why::NoUser2: return "no live user 2 (or its character key is unknown)";
        case Why::NotUser2Scene: return "not user 2's scene (owner [seq+0x98] is another character)";
        case Why::AlreadyToUser2: return "the native 2810 already goes to user 2's character";
        case Why::LogoutAsked: return "user 2's logout was asked for";
        case Why::NotDelivered: return "its 2661 never reached user 2's queue";
        case Why::OtherEpoch: return "its 2661 belongs to another guest epoch";
        case Why::OtherTrip: return "its 2661 reached user 2 before its last MoveFieldAck 2174";
        case Why::OwnerMismatch: return "the 2661 user 2 got names another owner/actor";
        case Why::NoRealm: return "not the server's realm (TLS +0x1EC)";
        case Why::NoSendCtx: return "no send context (TLS +0x250)";
        case Why::ScopeOpen: return "a native scope sender is open on this thread (+0x2AC/+0x2A0)";
        case Why::NoActor: return "user 2's character is not readable as expected";
        default: return "?";
    }
}

// One decision per native 2810, in this order (the first that fails is the reason).
inline Why decide(const CopyFacts& f) {
    if (!f.on) return Why::Off;
    if (f.reentry) return Why::Reentry;
    if (!f.from_delete) return Why::NotDeleteSite;
    if (!f.consistent) return Why::Inconsistent;
    if (!f.live_session || !f.user2_key) return Why::NoUser2;
    if (f.owner_key != f.user2_key) return Why::NotUser2Scene;
    if (f.recipient_key == f.user2_key) return Why::AlreadyToUser2;
    if (f.logout) return Why::LogoutAsked;
    if (!f.delivered) return Why::NotDelivered;
    if (f.delivered->epoch != f.epoch || f.book_epoch != f.epoch) return Why::OtherEpoch;
    if (f.delivered->trip != f.book_trip) return Why::OtherTrip;
    if (f.delivered->owner != f.user2_key || f.delivered->actor != f.user2_key) return Why::OwnerMismatch;
    if (!f.realm) return Why::NoRealm;
    if (!f.send_ctx) return Why::NoSendCtx;
    if (f.scope_open) return Why::ScopeOpen;
    if (!f.actor_ok) return Why::NoActor;
    return Why::Copy;
}

}  // namespace cdmp::session2::osc
