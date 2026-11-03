#pragma once
// P8 mounts, stages 1 + 2 (docs/research/pendientes/p8-impl-monturas.md; the investigation:
// Documents/Codex/.../mounts-20261011-work/INFORME.txt §5, §6, §8): the pure logic of mount_sync.cpp, with no
// game access, so the tests can run it.
//
// [session2] mount_sync = 0 | probe | 1, on BOTH PCs. Each client is the CONTROLLER of its own rider + horse
// and the OBSERVER of the other player's pair.
//
// Controller: the client's CommonVehicle::vf53 (0x2228260, reached through the ClientVehicle vf53 thunk 0xA45740)
// commits the rider <-> horse VehicleLink natively. Its entry (MountBegin) and its exit (MountCommit, *result and
// the postconditions) are published, versioned, through the mod's own tunnel control packet (opcode 1, state 5),
// so the observer learns the transaction BEFORE the mount action that needs it (Begin precedes every Req the
// commit's callees emit). A native retirement 0x2228E40 of the same pair is published as MountDetach.
//
// Observer (mode 1): after a Begin the observed stream is held (guest: guest_deliver; host: user 2's Req before
// the server), bounded, until the Commit is known; the commit is replayed natively (0x2228260 with the
// controller's seat and fifth byte) on the client main thread at the dispatch of the rider's next StartActionAck
// (the 2991 handler 0xB57720, the same packet-dispatch context in which the native interaction Acks run), the
// postconditions are checked (never written), and only then the original action goes on; ride_filter's rewrite
// is skipped for that validated transaction and is applied there, at the dispatch, if the link is still missing.
// Probe: everything is computed and logged with the exact arguments, nothing is held, called or written.

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

#include "mod/session2/user2_funnels_logic.h"

namespace cdmp::session2::msl {

using u2f::Mode;
using u2f::mode_name;
using u2f::parse_mode;

inline bool is_char(uint32_t key) { return (key >> 28) == 0xA; }
inline bool is_b(uint32_t key) { return (key >> 28) == 0xB; }

// Seats of a vehicle component: u32 occupants at +0x48 and reservations at +0x88, 16 each (0x2228552 / E6F3880).
inline constexpr uint8_t kMaxSeats = 16;

// ---------------------------------------------------------------- wire: tunnel control packet, state 5

inline constexpr uint16_t kControlOpcode = 1;     // troc_frame.h kControlOpcode
inline constexpr uint8_t kControlMountSync = 5;   // troc_frame.h: 1 ready, 2 dead, 3 reannounce, 4 host character
inline constexpr uint8_t kVersion = 1;
inline constexpr size_t kHeader = 5;              // u16 opcode | i8 slot | u16 len
inline constexpr size_t kPayloadSize = 44;

// P8 S26 (docs/research/s25/montura-regresion.md section 7.1): Status is not a transaction. The controller sends it
// while its own 0x2228260 hook is not installed (kFlagNoPublish): it publishes no mount at all, so the observer can
// say so (log, overlay). A receiver of an older build rejects the unknown kind and logs it; nothing else changes.
enum class EvKind : uint8_t { Begin = 1, Commit = 2, Detach = 3, Status = 4 };
inline const char* ev_name(EvKind k) {
    switch (k) {
        case EvKind::Begin: return "MountBegin";
        case EvKind::Commit: return "MountCommit";
        case EvKind::Detach: return "MountDetach";
        case EvKind::Status: return "MountStatus";
    }
    return "?";
}

inline constexpr uint8_t kFlagOk = 0x01;            // Commit/Detach: *result == 0 and the controller's postconditions held
inline constexpr uint8_t kFlagRiderWasLinked = 0x02;  // Begin: the rider's Link+0x10 was not 0
inline constexpr uint8_t kFlagSeatWasTaken = 0x04;    // Begin: the seat's occupant was not 0
// Detach (session 17): not a 0x2228E40 call but the controller's OWN link seen gone (Link+0x10 no longer the
// committed horse). S17 [V]: a player's dismount never goes through the thunk 0x2228E40 (8 dismounts on both
// PCs, client and server realm: 0 calls for a player; only NPCs use it), so the native retirement route is not
// hookable here; the controller watches its link instead and reports the end of the transaction.
inline constexpr uint8_t kFlagObserved = 0x08;
// The 5th byte the CLIENT passes natively to 0x2228E40 when it retires a remote rider (S17 [V]: every NPC
// detach on the client thread, both PCs, caller exe+0xA46C21, p4 0, flag 12; the server realm uses 7, vf53's own
// ride-off of a previous vehicle 14). An observed Detach carries it (and p4 0) for the observer's replay.
inline constexpr uint8_t kClientDetachFlag = 12;
// Status (P8 S26): the controller's CommonVehicle::vf53 hook (0x2228260) is not installed: none of its mounts is
// published, so its rider will never be seated on the other PC.
inline constexpr uint8_t kFlagNoPublish = 0x10;

struct Event {
    EvKind kind = EvKind::Begin;
    uint8_t seat = 0;        // R9b of vf53 (indexes the seats; Link+0x14)
    uint8_t fifth = 0;       // the byte at [entry rsp+0x28]
    uint8_t flags = 0;
    uint32_t rider = 0;      // 0xA... key
    uint32_t horse = 0;      // 0xB... key ([vc+8]+0x60)
    uint32_t transition = 0; // monotonic per controller process (nonce)
    uint32_t result = 0;     // *result after (Commit / Detach)
    uint32_t nonce = 0;      // the controller process' random id: a new one forgets the old transitions
    uint32_t caller_rva = 0; // the vtable call site of the client vf53 (or of 0x2228E40)
    uint32_t aux = 0;        // Commit: occupant count after; Detach: 0x2228E40's 4th argument (low 32 bits)
    uint64_t tick_ms = 0;    // the controller's GetTickCount64
};

inline void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
inline uint32_t get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// u8 5 | u8 version | u8 kind | u8 seat | u8 fifth | u8 flags | u16 0 | u32 rider | u32 horse | u32 transition |
// u32 result | u32 nonce | u32 caller | u32 aux | u64 tick_ms
inline std::array<uint8_t, kPayloadSize> encode(const Event& e) {
    std::array<uint8_t, kPayloadSize> p{};
    p[0] = kControlMountSync;
    p[1] = kVersion;
    p[2] = static_cast<uint8_t>(e.kind);
    p[3] = e.seat;
    p[4] = e.fifth;
    p[5] = e.flags;
    put32(&p[8], e.rider);
    put32(&p[12], e.horse);
    put32(&p[16], e.transition);
    put32(&p[20], e.result);
    put32(&p[24], e.nonce);
    put32(&p[28], e.caller_rva);
    put32(&p[32], e.aux);
    put32(&p[36], static_cast<uint32_t>(e.tick_ms));
    put32(&p[40], static_cast<uint32_t>(e.tick_ms >> 32));
    return p;
}

// Exactly kPayloadSize bytes, state 5, this version, a known kind, a 0xA rider, a 0xB horse, a seat in range
// (Status: a 0xA rider or 0 while the controller does not know its own key yet, no horse, seat 0).
inline std::optional<Event> decode(std::span<const uint8_t> p) {
    if (p.size() != kPayloadSize || p[0] != kControlMountSync || p[1] != kVersion) return std::nullopt;
    if (p[2] < 1 || p[2] > 4 || p[6] != 0 || p[7] != 0) return std::nullopt;
    Event e;
    e.kind = static_cast<EvKind>(p[2]);
    e.seat = p[3];
    e.fifth = p[4];
    e.flags = p[5];
    e.rider = get32(&p[8]);
    e.horse = get32(&p[12]);
    e.transition = get32(&p[16]);
    e.result = get32(&p[20]);
    e.nonce = get32(&p[24]);
    e.caller_rva = get32(&p[28]);
    e.aux = get32(&p[32]);
    e.tick_ms = get32(&p[36]) | (static_cast<uint64_t>(get32(&p[40])) << 32);
    if (e.kind == EvKind::Status) {
        if ((e.rider != 0 && !is_char(e.rider)) || e.horse != 0 || e.seat != 0) return std::nullopt;
        return e;
    }
    if (!is_char(e.rider) || !is_b(e.horse) || e.seat >= kMaxSeats || e.transition == 0) return std::nullopt;
    return e;
}

// P8 S26: the controller's Status (kFlagNoPublish while its 0x2228260 hook is missing).
inline Event status_event(uint32_t local_key, uint32_t nonce, uint64_t now_ms, bool cannot_publish) {
    Event e;
    e.kind = EvKind::Status;
    e.rider = is_char(local_key) ? local_key : 0;
    e.nonce = nonce;
    e.flags = cannot_publish ? kFlagNoPublish : 0;
    e.tick_ms = now_ms;
    return e;
}
// How long the observer keeps showing "the other PC publishes no mounts" after its last such Status (it is sent
// every kStatusEveryMs while true).
inline constexpr uint64_t kStatusEveryMs = 15000;
inline constexpr uint64_t kStatusShowMs = 45000;

// The whole troc frame: u16 opcode 1 | i8 -1 | u16 44 | payload.
inline std::vector<uint8_t> frame(const Event& e) {
    const auto p = encode(e);
    std::vector<uint8_t> f(kHeader + kPayloadSize);
    f[0] = kControlOpcode & 0xFF;
    f[1] = kControlOpcode >> 8;
    f[2] = 0xFF;
    f[3] = kPayloadSize & 0xFF;
    f[4] = kPayloadSize >> 8;
    for (size_t i = 0; i < kPayloadSize; ++i) f[kHeader + i] = p[i];
    return f;
}

// A frame of this exact form (opcode 1, slot -1, 44-byte payload starting with state 5). Nothing else is taken out
// of the stream as a mount event; any other opcode-1 frame goes on as before.
inline bool is_frame(std::span<const uint8_t> f) {
    return f.size() == kHeader + kPayloadSize && f[0] == (kControlOpcode & 0xFF) && f[1] == (kControlOpcode >> 8) &&
           f[2] == 0xFF && f[3] == (kPayloadSize & 0xFF) && f[4] == (kPayloadSize >> 8) &&
           f[kHeader] == kControlMountSync;
}

// The controller publishes only its own character on a 0xB horse.
inline bool publishable(uint32_t rider, uint32_t horse, uint8_t seat, uint32_t local_key) {
    return local_key != 0 && rider == local_key && is_char(rider) && is_b(horse) && seat < kMaxSeats;
}

// ---------------------------------------------------------------- the pair as read from the game

// Read with SEH-guarded reads on the client thread, before and after a native call. `ok` fields say whether the
// hop could be read; a missing hop leaves the values at 0.
struct PairState {
    bool rider_found = false, horse_found = false;  // by key in the client actor table, key read back equal
    bool rider_vc = false, horse_vc = false;        // [[actor+0x68]+0x48] not null
    bool link = false;                              // rider [vc+0x18] not null
    uint8_t horse_has_seats = 0;                    // horse vc+0xEB
    uint32_t cur_vehicle = 0;                       // Link+0x10
    int8_t cur_seat = -1;                           // Link+0x14
    uint32_t res_vehicle = 0;                       // Link+0x08
    int8_t res_seat = -1;                           // Link+0x0C
    uint32_t occupant = 0;                          // horse vc+0x48+4*seat
    uint32_t reserve = 0;                           // horse vc+0x88+4*seat
    uint8_t count = 0;                              // horse vc+0xC8
    uint32_t rider_ctrl = 0, horse_ctrl = 0;        // actor+0x90 (controlling user key)
    uint8_t rider_c8 = 0, horse_c8 = 0;             // actor+0xC8 (ClientActor vf188, 0x4FA620)
    // Session 18 review (2): what 0x2228260 dereferences WITHOUT checking (2228260.c), read under SEH when rider
    // and horse are both given; a set bit = that hop is null or unreadable (kDep*). Any -> NoVehicle (not ready).
    uint32_t deps_missing = 0;
    // P8 S18 (nativo §5 (4)): the rider's movement method (mc+0xF0, 0xFF unknown) and, when it is 7 (RideOff), the
    // horse key RideOff::Enter cached in its method object (+0xD4; 0 = none). Read only by the observer's commit.
    uint8_t rider_method = 0xFF;
    uint32_t rideoff_key = 0;
};

inline constexpr uint32_t kDepRiderComps = 0x001;   // rider actor+0x68
inline constexpr uint32_t kDepRiderCC = 0x002;      // rider comps+0x40 (CC; +0xA8 read, line 261)
inline constexpr uint32_t kDepRiderCCSlot = 0x004;  // CC+0xA8 == 0: [CC+0x30]+0x1B and the pointer at CC+0x88+8*it
inline constexpr uint32_t kDepRiderComps50 = 0x008; // rider comps+0x50 (writes +0x11C, line 282)
inline constexpr uint32_t kDepHorseComps = 0x010;   // horse actor+0x68
inline constexpr uint32_t kDepHorseComps1A0 = 0x020;// horse comps+0x1A0 (+0x98, line 213)
inline constexpr uint32_t kDepHorseComps90 = 0x040; // horse comps+0x90 (0x221C200, seat 0)
inline constexpr uint32_t kDepHorse88 = 0x080;      // horse actor+0x88 (+1, the type switches)
inline constexpr uint32_t kDepHorseCC = 0x100;      // horse comps+0x40 (CC->vf260)
inline constexpr uint32_t kDepHorseCompsB0 = 0x200; // horse comps+0xB0 (+0x288 / +0x300, conditional)

enum class Judge : uint8_t {
    Apply,                // everything in place: call 0x2228260
    AlreadyApplied,       // Link+0x10/+0x14 = horse/seat and the occupant is the rider: nothing to do (idempotent)
    NoRider,              // the rider is not in the client actor table (yet)
    NoHorse,              // the horse is not (yet)
    NoVehicle,            // a Vehicle component or the rider's Link is missing
    NoSeats,              // the horse's vc+0xEB is 0 (not a vehicle with seats)
    SeatTaken,            // another key sits in that seat
    RiderLinkedElsewhere, // the rider's Link+0x10 names another vehicle (vf53 would ride it off first): refused
    ControllerMismatch,   // rider and horse not under the same (non-zero) controlling user
    RiderLocal,           // rider vf188(1) (C8 == 1): vf53 would take the local speed branch; never for a remote
    HorseActionBranch,    // seat 0, fifth != 1 and horse C8 != 0: vf53 would start an action on the horse here
    RetireFirst,          // the rider is linked to ANOTHER horse that this client linked it to: retire that first
    StuckInRideOff,       // P8 S18: the rider's copy is still in RideOff (method 7, +0xD4 set): not yet (retry)
    Count
};
inline const char* judge_name(Judge j) {
    switch (j) {
        case Judge::Apply: return "apply";
        case Judge::AlreadyApplied: return "already applied";
        case Judge::NoRider: return "rider not in the actor table";
        case Judge::NoHorse: return "horse not in the actor table";
        case Judge::NoVehicle: return "no Vehicle component / Link";
        case Judge::NoSeats: return "horse vc+0xEB 0 (no seats)";
        case Judge::SeatTaken: return "seat taken by another key";
        case Judge::RiderLinkedElsewhere: return "rider linked to another vehicle";
        case Judge::ControllerMismatch: return "rider/horse controller mismatch";
        case Judge::RiderLocal: return "rider C8 == 1 (local branch)";
        case Judge::HorseActionBranch: return "would start a horse action (horse C8 != 0, seat 0, fifth != 1)";
        case Judge::RetireFirst: return "linked to the previous horse by this client: retire it first";
        case Judge::StuckInRideOff: return "the rider is still in RideOff here (method 7, +0xD4 set): not yet";
        case Judge::Count: break;
    }
    return "?";
}
// Not ready yet (may be retried before the rider's action) vs refused for this transaction.
inline bool judge_retry(Judge j) {
    return j == Judge::NoRider || j == Judge::NoHorse || j == Judge::NoVehicle || j == Judge::StuckInRideOff;
}

// P8 S18: RideOff is movement method 7 [V, vtable 0x5597190]; RideOn is 6.
inline constexpr uint8_t kMethodRideOn = 6, kMethodRideOff = 7;
inline bool ride_method(uint8_t m) { return m == kMethodRideOn || m == kMethodRideOff; }

// `linked_here`: the horse this client itself linked the rider to (Txn::linked_here), 0 if none.
inline Judge judge_commit(const PairState& s, uint32_t rider, uint32_t horse, uint8_t seat, uint8_t fifth,
                          uint32_t linked_here = 0) {
    if (!s.rider_found) return Judge::NoRider;
    if (!s.horse_found) return Judge::NoHorse;
    if (!s.rider_vc || !s.horse_vc || !s.link || s.deps_missing) return Judge::NoVehicle;
    if (!s.horse_has_seats) return Judge::NoSeats;
    if (s.cur_vehicle == horse && s.cur_seat == static_cast<int8_t>(seat) && s.occupant == rider) {
        return Judge::AlreadyApplied;
    }
    // P8 S18 (nativo §5 (4)): never link a rider whose copy is stuck in a previous RideOff (S18 transaction 2: linked,
    // never RideOn). Not ready: retried; at the rider's own action it is cancelled (ride_filter's fallback).
    if (s.rider_method == kMethodRideOff && s.rideoff_key != 0) return Judge::StuckInRideOff;
    if (s.occupant != 0 && s.occupant != rider) return Judge::SeatTaken;
    if (s.cur_vehicle != 0 && s.cur_vehicle != horse) {
        return s.cur_vehicle == linked_here ? Judge::RetireFirst : Judge::RiderLinkedElsewhere;
    }
    if (s.rider_ctrl == 0 || s.rider_ctrl != s.horse_ctrl) return Judge::ControllerMismatch;
    if (s.rider_c8 == 1) return Judge::RiderLocal;
    if (seat == 0 && fifth != 1 && s.horse_c8 != 0) return Judge::HorseActionBranch;
    return Judge::Apply;
}

enum class Post : uint8_t { Ok, Result, Link, Occupant, Count, Reserve, Controller, C8, Count_ };
inline const char* post_name(Post p) {
    switch (p) {
        case Post::Ok: return "ok";
        case Post::Result: return "*result != 0";
        case Post::Link: return "Link+0x10/+0x14 not horse/seat";
        case Post::Occupant: return "seat occupant not the rider";
        case Post::Count: return "occupant count not +1";
        case Post::Reserve: return "reservation Link+0x08 not cleared";
        case Post::Controller: return "a controller (actor+0x90) changed";
        case Post::C8: return "an actor+0xC8 changed";
        case Post::Count_: break;
    }
    return "?";
}

// After 0x2228260 returned (read back, never written).
inline Post verify_commit(const PairState& before, const PairState& after, uint32_t rider, uint32_t horse,
                          uint8_t seat, int32_t result) {
    if (result != 0) return Post::Result;
    if (after.cur_vehicle != horse || after.cur_seat != static_cast<int8_t>(seat)) return Post::Link;
    if (after.occupant != rider) return Post::Occupant;
    const uint8_t want = static_cast<uint8_t>(before.count + (before.occupant == 0 ? 1 : 0));
    if (after.count != want) return Post::Count;
    if (after.res_vehicle != 0) return Post::Reserve;
    if (after.rider_ctrl != before.rider_ctrl || after.horse_ctrl != before.horse_ctrl) return Post::Controller;
    if (after.rider_c8 != before.rider_c8 || after.horse_c8 != before.horse_c8) return Post::C8;
    return Post::Ok;
}

// After 0x2228E40 returned: the rider no longer names the horse, the seat no longer names the rider, one fewer.
inline Post verify_detach(const PairState& before, const PairState& after, uint32_t rider, uint32_t horse,
                          int32_t result) {
    if (result != 0) return Post::Result;
    if (after.cur_vehicle == horse) return Post::Link;
    if (after.occupant == rider) return Post::Occupant;
    if (before.occupant == rider && after.count + 1 != before.count) return Post::Count;
    if (after.rider_ctrl != before.rider_ctrl || after.horse_ctrl != before.horse_ctrl) return Post::Controller;
    return Post::Ok;
}

// Session 18 review (1): 0xE6F2E60 (behind the thunk 0x2228E40) does mov rax,[rdx+0x68]; mov rcx,[rax+0x48];
// mov rax,[rcx] with no check (a horse half created / being destroyed = an access violation inside the game, passed
// on: a real crash); the native 2938 handler checks vc != 0 and vc+0xEB first. So a retirement here is called only
// with both actors found, both Vehicle components and the rider's Link readable, the horse a vehicle with seats and
// not under local control (C8 0, as every S17 observer read).
enum class DetachCall : uint8_t { Call, AlreadyGone, Refused };
inline DetachCall detach_decide(const PairState& s, uint32_t horse) {
    if (!s.rider_found || !s.rider_vc || !s.link) return DetachCall::Refused;  // nothing can be said
    if (s.cur_vehicle != horse) return DetachCall::AlreadyGone;               // nothing of this pair here
    if (!s.horse_found || !s.horse_vc || !s.horse_has_seats || s.horse_c8 != 0) return DetachCall::Refused;
    return DetachCall::Call;
}

// The controller's own check before it publishes a Commit as ok (the same postconditions, on its own pair).
inline bool controller_commit_ok(const PairState& before, const PairState& after, uint32_t rider, uint32_t horse,
                                 uint8_t seat, int32_t result) {
    return verify_commit(before, after, rider, horse, seat, result) == Post::Ok ||
           // its own C8 may legitimately change on the controller (local horse); the link is what matters there
           (result == 0 && after.cur_vehicle == horse && after.cur_seat == static_cast<int8_t>(seat) &&
            after.occupant == rider);
}

// ---------------------------------------------------------------- the controller's own link (session 17)

// The controller's committed pair, armed by an ok Commit and disarmed (exactly once, compare-exchange) by whoever
// reports its end: the 0x2228E40 hook (native retirement, NPC-style) or the link watch (a player's dismount).
inline constexpr uint64_t arm_pack(uint32_t horse, uint32_t transition) {
    return (static_cast<uint64_t>(transition) << 32) | horse;
}
inline constexpr uint32_t arm_horse(uint64_t a) { return static_cast<uint32_t>(a); }
inline constexpr uint32_t arm_transition(uint64_t a) { return static_cast<uint32_t>(a >> 32); }

enum class CtlLink : uint8_t {
    Unknown,  // the rider, its Vehicle or its Link could not be read: no conclusion
    Riding,   // Link+0x10 is still the committed horse
    Gone,     // Link+0x10 is 0 or another vehicle: the committed transaction ended on the controller
};
// Without the rider's RideOff::Enter since the commit, the link must stay gone this long before it is reported.
inline constexpr uint64_t kCtlGoneMs = 1000;
inline CtlLink ctl_link(const PairState& s, uint32_t horse) {
    if (!s.rider_found || !s.rider_vc || !s.link || horse == 0) return CtlLink::Unknown;
    return s.cur_vehicle == horse ? CtlLink::Riding : CtlLink::Gone;
}
// The Detach the link watch publishes for an armed pair that is gone.
inline Event observed_detach(uint32_t rider, uint64_t armed, uint32_t nonce, uint64_t tick_ms) {
    Event e;
    e.kind = EvKind::Detach;
    e.rider = rider;
    e.horse = arm_horse(armed);
    e.transition = arm_transition(armed);
    e.nonce = nonce;
    e.result = 0;
    e.fifth = kClientDetachFlag;
    e.aux = 0;
    e.flags = kFlagOk | kFlagObserved;
    e.tick_ms = tick_ms;
    return e;
}

// ---------------------------------------------------------------- ride geometry (session 17, observers)

// Where the remote rider actually is relative to its horse on THIS client (both actors' TransformSync, read on the
// client main thread): the offset in the horse's frame, against the seat offset its controller sends. S17 [V],
// 9,732 mounted Move Req of both controllers: rider pos == Path.world (+) Path.local to the mm, Path.local mean
// (-0.01..+0.15, 1.51..1.63, 0.49..0.59) m with sd 0.09-0.16 m at every speed (the rider's bob). An error well
// above that is the rider shown off its saddle.
inline constexpr float kSeatRef[3] = {0.05f, 1.56f, 0.55f};
inline constexpr size_t kGeoSpeeds = 5;  // horse speed m/s: <1, <3, <6, <10, >=10
inline constexpr size_t kGeoErrs = 6;    // |offset - seat| m: <=0.25, <=0.5, <=1, <=2, <=5, >5
inline constexpr uint64_t kGeoEveryMs = 30;
inline size_t geo_speed_bucket(float mps) {
    return mps < 1.f ? 0 : mps < 3.f ? 1 : mps < 6.f ? 2 : mps < 10.f ? 3 : 4;
}
inline size_t geo_err_bucket(float m) {
    return m <= 0.25f ? 0 : m <= 0.5f ? 1 : m <= 1.f ? 2 : m <= 2.f ? 3 : m <= 5.f ? 4 : 5;
}
// v rotated by the inverse of the unit quaternion q (x, y, z, w): the world offset in q's frame.
inline void rotate_inv(const float q[4], const float v[3], float out[3]) {
    const float x = -q[0], y = -q[1], z = -q[2], w = q[3];
    const float tx = 2 * (y * v[2] - z * v[1]), ty = 2 * (z * v[0] - x * v[2]), tz = 2 * (x * v[1] - y * v[0]);
    out[0] = v[0] + w * tx + (y * tz - z * ty);
    out[1] = v[1] + w * ty + (z * tx - x * tz);
    out[2] = v[2] + w * tz + (x * ty - y * tx);
}
inline bool unit_quat(const float q[4]) {
    const float l = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    return l > 0.81f && l < 1.21f;
}

// P8 S21: the rider as this client holds it (geo_link): seated here (attach type 4, mount_seat), linked here (the
// Link names its horse, not seated) or NOT linked here.
inline constexpr size_t kGeoNotLinked = 0, kGeoLinked = 1, kGeoSeated = 2, kGeoLinks = 3;
// [[CC+0x100]+8] (see "seat the remote rider" below). [V Ghidra/capstone, S22] 9 is "not attached": the info reset
// 0x9E990E0 (thunk 0xA65170) starts `mov byte [rcx],9`, the CC constructor 0x85CB00 builds it that way and every detach
// (0xA65960, 0xA6A120) writes it back; 0xA651E0 skips the hierarchy parent for 9. Type 0 is never written.
inline constexpr uint8_t kAttachNone = 9, kAttachSeat = 4;
inline size_t geo_link(bool linked, uint8_t attach_type) {
    return attach_type == kAttachSeat ? kGeoSeated : linked ? kGeoLinked : kGeoNotLinked;
}
// P8 S21 "frozen": between two geometry ticks while seated, the rider moved < 1 cm while its horse moved > 0.5 m
// (a rider attached to a bone that no longer follows the horse).
inline constexpr float kFrozenRiderM = 0.01f, kFrozenHorseM = 0.5f;
inline bool geo_frozen(const float rider_prev[3], const float rider[3], const float horse_prev[3], const float horse[3]) {
    float r = 0, h = 0;
    for (int i = 0; i < 3; ++i) {
        r += (rider[i] - rider_prev[i]) * (rider[i] - rider_prev[i]);
        h += (horse[i] - horse_prev[i]) * (horse[i] - horse_prev[i]);
    }
    return r < kFrozenRiderM * kFrozenRiderM && h > kFrozenHorseM * kFrozenHorseM;
}

// Counters: written by the client main thread only, read by the summary (relaxed atomics).
struct RideGeo {
    std::array<std::array<std::atomic<uint64_t>, kGeoSpeeds>, kGeoLinks> n{};                         // [link][speed]
    std::array<std::array<std::array<std::atomic<uint64_t>, kGeoErrs>, kGeoSpeeds>, kGeoLinks> err{};  // [link][speed][err]
    std::array<std::array<std::array<std::atomic<int64_t>, 3>, kGeoSpeeds>, kGeoLinks> sum_mm{};       // local offset sums
    std::array<std::array<std::atomic<uint32_t>, kGeoSpeeds>, kGeoLinks> max_mm{};
    std::atomic<uint64_t> unreadable{0}, off_far{0}, frozen{0};

    // One sample: `link` (geo_link), the rider's and the horse's world positions, the horse's rotation, the horse's
    // speed (m/s). False: not counted (a bad quaternion, or the rider more than 10 m off the seat: not on that horse).
    bool add(size_t link, const float rider[3], const float horse[3], const float hq[4], float speed) {
        if (!unit_quat(hq)) {
            unreadable.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const float d[3] = {rider[0] - horse[0], rider[1] - horse[1], rider[2] - horse[2]};
        float local[3];
        rotate_inv(hq, d, local);
        const float ex = local[0] - kSeatRef[0], ey = local[1] - kSeatRef[1], ez = local[2] - kSeatRef[2];
        const float e = std::sqrt(ex * ex + ey * ey + ez * ez);
        if (!(e <= 10.f)) {
            off_far.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const size_t l = link < kGeoLinks ? link : kGeoNotLinked, s = geo_speed_bucket(speed);
        n[l][s].fetch_add(1, std::memory_order_relaxed);
        err[l][s][geo_err_bucket(e)].fetch_add(1, std::memory_order_relaxed);
        for (int i = 0; i < 3; ++i) {
            sum_mm[l][s][i].fetch_add(static_cast<int64_t>(local[i] * 1000.f), std::memory_order_relaxed);
        }
        const uint32_t mm = static_cast<uint32_t>(e * 1000.f);
        if (mm > max_mm[l][s].load(std::memory_order_relaxed)) max_mm[l][s].store(mm, std::memory_order_relaxed);
        return true;
    }
};

// ---------------------------------------------------------------- the observer's transactions

inline constexpr uint64_t kCommitWaitMs = 500;     // a Begin holds the observed stream at most this long
inline constexpr uint64_t kApplyWaitMs = 3000;     // a Commit not applied by then is cancelled (fallback)
inline constexpr uint64_t kDetachForceMs = 3000;   // a Detach without the observer's RideOff::Enter: applied anyway
// Session 18 review (4): an OBSERVED Detach (the controller's link is already gone) is retired here at the first
// dispatch once the rider's actions are on foot here (or its RideOff::Enter ran), else after this short wait. S17:
// the host never ran RideOff::Enter for the guest's dismount (it came as on-foot acks, parent -1).
inline constexpr uint64_t kDetachObservedForceMs = 1000;
// No Detach from the controller at all (an older build, or its link watch could not read the rider), but the
// rider's StartActionAcks are on foot (parent -1, not naming its horse) for this long while its transaction rides
// here: the link applied here is retired anyway (0x2228E40 with the client's own flag 12, p4 0), exactly once.
inline constexpr uint64_t kDetachNoEventMs = 6000;
// Session 18 review (6): a claim (Applying / Detaching) that never finished (the game's exception passed on and
// caught by an outer handler): quarantined after this long, never left "deferring" for ever.
inline constexpr uint64_t kClaimStuckMs = 2000;
// Not-ready retries of one commit: at any dispatch BEFORE the rider's own StartActionAck; the attempt at the
// rider's own (its mount action) is the last one (session 18 review (5): never linked after its RideOn).
inline constexpr uint32_t kMaxAttempts = 20;
inline constexpr size_t kHoldMaxPackets = 2048;    // the guest's held stream
inline constexpr size_t kHoldMaxBytes = 4u << 20;
inline constexpr size_t kHoldMaxReq = 256;         // the host's held user 2 Req (its own bound is 512)

enum class Phase : uint8_t {
    Idle,
    Begun,          // Begin seen: the stream is held (mode 1) until the Commit
    Committed,      // the controller's commit succeeded: to be applied at or before the rider's next action
    Applying,       // claimed by the client thread
    Riding,         // applied (or already there): the link is native on this client
    DetachPending,  // the controller retired: to be retired here (RideOff::Enter / on foot / a wait)
    Detaching,      // claimed
    Detached,
    Cancelled,      // never applied (controller failure, timeout, refused): the old behaviour (ride_filter)
    Quarantined,    // a native call here did not leave the expected state: no more applies for this pair
    Count
};
inline const char* phase_name(Phase p) {
    switch (p) {
        case Phase::Idle: return "idle";
        case Phase::Begun: return "begun";
        case Phase::Committed: return "committed";
        case Phase::Applying: return "applying";
        case Phase::Riding: return "riding";
        case Phase::DetachPending: return "detach pending";
        case Phase::Detaching: return "detaching";
        case Phase::Detached: return "detached";
        case Phase::Cancelled: return "cancelled";
        case Phase::Quarantined: return "QUARANTINED";
        case Phase::Count: break;
    }
    return "?";
}

struct Txn {
    uint32_t rider = 0, horse = 0, transition = 0, nonce = 0;
    uint8_t seat = 0, fifth = 0;
    Phase phase = Phase::Idle;
    uint64_t begun_ms = 0, committed_ms = 0, detach_ms = 0, claim_ms = 0;
    uint32_t detach_aux = 0;     // 0x2228E40's 4th argument on the controller
    uint8_t detach_flag = 0;     // its 5th
    bool detach_observed = false;  // the Detach is the controller's link watch (kFlagObserved)
    bool detach_queued = false;    // a Detach that arrived while the commit was being applied (Applying)
    bool detach_tried = false;     // a retirement was claimed for this transaction (never twice)
    bool ride_off_entered = false;
    // P8 S26 (montura-regresion.md section 6, 7.2): this client's RideOn::Enter of the rider ran since the commit (the
    // link is cached by RideOn from then on: never linked again after it), the commit was applied here (Applied or
    // AlreadyApplied, not probe), and how many times it was applied again because the game had retired the fresh
    // link before RideOn (a relink).
    bool ride_on_entered = false;
    bool applied = false;
    uint32_t relinks = 0;
    // Review of bf255bb (2): the claim in progress is a relink (its failure stays with this transaction: no pair
    // quarantine, no seat switch-off), and mount_relink=probe already logged its "would relink" for this transaction.
    bool relinking = false;
    bool relink_probed = false;
    bool claimed_at_rider = false; // the last commit claim was at the rider's own StartActionAck
    uint32_t attempts = 0;
    bool quarantined_pair = false;
    uint64_t on_foot_ms = 0;     // since when the rider's actions are on foot here (0: mounted / not yet)
    bool no_event = false;       // the retirement planned without the controller's Detach (kDetachNoEventMs)
    // The horse this client linked the rider to with its own native call (or found linked), kept across the
    // rider's transactions until a retirement here clears it: a new mount on ANOTHER horse retires it first
    // (session 18 review (3)), never a link this client did not make.
    uint32_t linked_here = 0;
    // P8 S18, DetachPolicy::Native: the planned retirement found the native one done (nothing to call) / is the
    // fallback after kNativeFallbackMs.
    bool native_gone = false;
    bool native_fallback = false;
    bool native_lost = false;     // forgotten after kNativeUnreadableMs without a readable rider: nothing is called
    uint64_t unreadable_ms = 0;   // since when the rider could not be read (DetachPolicy::Native), 0 = readable
};

enum class Accept : uint8_t {
    Begun,         // a new transaction
    Committed,     // its commit
    CommitFailed,  // the controller's commit failed: cancelled
    DetachPending, // a retirement of the pair this client rides
    DetachDropped, // a retirement of a transaction never applied here: cancelled, nothing to undo
    DetachFailed,  // the controller's own retirement failed: nothing to retire here
    Dup,           // already seen
    Stale,         // older transition / another horse
    NotRemote,     // not the known remote player's character (or it is unknown)
    NoTxn,         // a Commit / Detach with no Begin of that transition
    Count
};
inline const char* accept_name(Accept a) {
    switch (a) {
        case Accept::Begun: return "begun";
        case Accept::Committed: return "committed";
        case Accept::CommitFailed: return "commit failed on the controller";
        case Accept::DetachPending: return "detach pending";
        case Accept::DetachDropped: return "detach of an unapplied transaction";
        case Accept::DetachFailed: return "detach failed on the controller";
        case Accept::Dup: return "duplicate";
        case Accept::Stale: return "stale";
        case Accept::NotRemote: return "not the remote player";
        case Accept::NoTxn: return "no such transaction";
        case Accept::Count: break;
    }
    return "?";
}

enum class Plan : uint8_t { None, Commit, Detach };

// P8 S18 (pendientes/p8-s18-desmontar-nativo.md §5): how the observer retires the link it made.
//   S18     (ride_dismount=rewrite) the dismount reaches this client as rewritten actions, RideOff never runs here:
//           retire at the first on-foot dispatch after the controller's Detach (<= 1 s), flag 12 (S18: APPLIED)
//   Native  (ride_dismount=native / partial) RideOff (or the first action that is not mounted) retires it natively
//           here, in vf89 -> 0x397F40, the same frame as RideOff::Enter: wait for that; only if the link this client
//           made is still there kNativeFallbackMs after the Detach, the rider is not in RideOn/RideOff (6/7) AND one
//           of its on-foot actions was dispatched here (on_foot_ms), one 0x2228E40(&res, horse, &key, p4 0, flag 0) as
//           the controller's own (flag 0) - never at the Detach event
// The on-foot condition (guest mode 1, p8-impl-monturas.md §12): the Detach is a control frame and reaches the table
// at once, while the rider's dismount action may still wait in the client's inbound queue (the guest's longer path,
// or a stalled main thread whose backlog is dispatched in order). A retirement BEFORE that action's RideOff::Enter
// would make it fail (Enter 0). An on-foot action comes after the dismount in the stream, so once one was dispatched
// here, the dismount was too: the link that is still there is really left over.
enum class DetachPolicy : uint8_t { S18, Native };
inline constexpr uint64_t kNativeFallbackMs = 2000;
// Without any on-foot action: the fallback after this long in DetachPending (method not 6/7).
inline constexpr uint64_t kNativeFallbackCapMs = 30000;
// Review of 0e8605c (3): a DetachPending (or an on-foot Riding) transaction whose rider cannot be read here for this
// long is forgotten (Detached) WITHOUT any call: otherwise it would stay work for ever (every dispatch takes the lock,
// ride_filter keeps deferring). The link this client made stays remembered (linked_here) for a later RetireFirst.
inline constexpr uint64_t kNativeUnreadableMs = 15000;
inline constexpr uint8_t kNativeDetachFlag = 0;
// The remote rider as the dispatch reads it before planning (known = its Vehicle/Link and mc were readable).
struct RiderNow {
    uint32_t rider = 0;
    bool known = false;
    uint32_t link_vehicle = 0;  // Link+0x10
    uint8_t method = 0xFF;      // mc+0xF0
};

// The remote rider's own StartActionAck being dispatched (P8 S26 relink): its Path parent (all ones = none) and its
// target. `own` false: another actor's dispatch (or the relink's stop conditions, the RideOn::Enter / RideOff::Enter
// hooks, are not installed: review of bf255bb (1)).
struct RiderAction {
    bool own = false;
    uint64_t parent = ~0ull;
    uint32_t target = 0;
};
// Review of bf255bb (1): only what S26 measured before a mount (12 host mounts, S20-S26): the rider still on foot
// (parent -1) with its horse as the action's TARGET. A mounted action (parent = the horse) never qualifies.
inline bool premount_names_horse(const RiderAction& a, uint32_t horse) {
    return horse != 0 && a.parent == ~0ull && a.target == horse;
}

// P8 S26 (docs/research/s25/montura-regresion.md sections 6 and 7.2), the relink. S26 step 2, mount 2: the host's
// commit reached the guest 6 ms after a pre-mount action of the rider (layer 1 only, A0 0, target = the horse) and
// before its dispatch, so the commit was applied at that action; its handler then retired the fresh link natively
// (no 0x2228E40, no RideOff) and the mount action, 13 ms later, found none: rewritten as ride_filter would, the rider
// unseated for the whole ride. The invariant: while a transaction applied here rides and this client's RideOn::Enter
// of its rider has not run, each of the rider's own pre-mount actions (parent -1, target = its horse) must find the
// link. If the link is gone (Link+0x10 == 0) and no Detach, RideOff::Enter or on-foot action was seen, the commit (and
// the seat) is applied again before that action's handler - bounded by kMaxAttempts claims in all and by kApplyWaitMs
// from the Commit. It refuses nothing: the same judge and postconditions as the first apply decide; a failure stays
// with this transaction ([session2] mount_relink = 0 | probe | 1, Table::set_relink).
inline bool relink_due(const Txn& t, uint32_t key, uint64_t now_ms, const RiderNow& rn, const RiderAction& act) {
    return t.phase == Phase::Riding && t.applied && key == t.rider && act.own && premount_names_horse(act, t.horse) &&
           !t.ride_on_entered && !t.ride_off_entered && !t.detach_queued && !t.on_foot_ms && rn.rider == t.rider &&
           rn.known && rn.link_vehicle == 0 && t.attempts < kMaxAttempts && t.committed_ms &&
           now_ms >= t.committed_ms && now_ms - t.committed_ms < kApplyWaitMs;
}

// What the client thread found when it tried a claimed commit / detach.
enum class Outcome : uint8_t { Applied, AlreadyApplied, NotReady, Refused, Failed, ProbeOnly };

// The observer's state: one transaction per rider (in practice one remote player). Not thread-safe: the caller
// holds its lock (never while calling the game).
class Table {
public:
    static constexpr size_t kSlots = 4;

    void reset() {  // the policy stays
        for (Txn& t : slot_) t = Txn{};
        quarantine_n_ = 0;
    }
    void set_policy(DetachPolicy p) { policy_ = p; }
    DetachPolicy policy() const { return policy_; }
    // P8 S26, [session2] mount_relink (review of bf255bb (2)): Off (default) never; Probe: plan() reports a due relink
    // once per transaction (Work::relink_probe), claims nothing; On: claims it (Work::relink).
    void set_relink(Mode m) { relink_ = m; }
    Mode relink() const { return relink_; }

    Accept on_event(const Event& e, uint64_t now_ms, uint32_t remote_key) {
        if (e.kind == EvKind::Status) return Accept::NoTxn;  // P8 S26: never a transaction (and never renews one)
        if (remote_key == 0 || e.rider != remote_key) return Accept::NotRemote;
        Txn* t = slot_of(e.rider);
        // A new controller process: its old transitions mean nothing here. A link this client applied for the old
        // process stays native (linked_here kept); a new Begin re-judges it.
        if (t && t->nonce != e.nonce) renew(*t);
        switch (e.kind) {
            case EvKind::Begin: {
                if (t && t->transition != 0 && t->nonce == e.nonce && e.transition <= t->transition) {
                    return e.transition == t->transition ? Accept::Dup : Accept::Stale;
                }
                if (!t) {
                    t = alloc(e.rider);
                    *t = Txn{};
                }
                renew(*t);
                t->rider = e.rider;
                t->horse = e.horse;
                t->transition = e.transition;
                t->nonce = e.nonce;
                t->seat = e.seat;
                t->fifth = e.fifth;
                t->phase = Phase::Begun;
                t->begun_ms = now_ms ? now_ms : 1;
                t->quarantined_pair = quarantined(e.rider, e.horse);
                return Accept::Begun;
            }
            case EvKind::Commit: {
                if (!t || t->nonce != e.nonce || t->transition == 0) return Accept::NoTxn;
                if (e.transition < t->transition) return Accept::Stale;
                if (e.transition > t->transition) return Accept::NoTxn;  // its Begin never came (ordered stream)
                if (t->phase != Phase::Begun) return Accept::Dup;
                if (e.horse != t->horse || e.seat != t->seat) return Accept::Stale;
                if (e.result != 0 || !(e.flags & kFlagOk)) {
                    t->phase = Phase::Cancelled;
                    return Accept::CommitFailed;
                }
                t->phase = t->quarantined_pair ? Phase::Cancelled : Phase::Committed;
                t->committed_ms = now_ms ? now_ms : 1;
                return Accept::Committed;
            }
            case EvKind::Detach: {
                if (!t || t->nonce != e.nonce || t->transition == 0) return Accept::NoTxn;
                if (e.horse != t->horse) return Accept::Stale;
                if (e.transition != t->transition) return Accept::Stale;  // never retires a newer mount
                if (e.result != 0 || !(e.flags & kFlagOk)) return Accept::DetachFailed;
                switch (t->phase) {
                    case Phase::Riding:
                        take_detach(*t, e, now_ms);
                        t->phase = Phase::DetachPending;
                        return Accept::DetachPending;
                    case Phase::Applying:
                        // The commit is running on the client thread right now: retired once it finished.
                        take_detach(*t, e, now_ms);
                        t->detach_queued = true;
                        return Accept::DetachPending;
                    case Phase::Quarantined:
                        // A failed commit may have left part of a link: one retirement attempt, never two.
                        if (t->detach_tried) return Accept::Dup;
                        take_detach(*t, e, now_ms);
                        t->phase = Phase::DetachPending;
                        return Accept::DetachPending;
                    case Phase::Begun:
                    case Phase::Committed:
                        t->phase = Phase::Cancelled;
                        return Accept::DetachDropped;
                    case Phase::DetachPending:
                    case Phase::Detaching:
                    case Phase::Detached: return Accept::Dup;
                    default: return Accept::DetachDropped;
                }
            }
            case EvKind::Status: break;  // not a transaction (mount_sync_on_frame takes it before the table)
        }
        return Accept::NoTxn;
    }

    // Any Begin still waiting for its Commit (the observed stream is held meanwhile, mode 1).
    bool holding(uint64_t now_ms) const {
        for (const Txn& t : slot_) {
            if (t.phase == Phase::Begun && now_ms - t.begun_ms < kCommitWaitMs) return true;
        }
        return false;
    }

    // Timeouts. Returns how many transactions were cancelled (or quarantined: a stuck claim) now.
    size_t expire(uint64_t now_ms) {
        size_t n = 0;
        for (Txn& t : slot_) {
            if ((t.phase == Phase::Begun && now_ms - t.begun_ms >= kCommitWaitMs) ||
                (t.phase == Phase::Committed && now_ms - t.committed_ms >= kApplyWaitMs)) {
                t.phase = Phase::Cancelled;
                ++n;
            } else if ((t.phase == Phase::Applying || t.phase == Phase::Detaching) && t.claim_ms &&
                       now_ms - t.claim_ms >= kClaimStuckMs) {
                t.phase = Phase::Quarantined;
                t.detach_tried = true;  // whatever was claimed, it is never planned again
                add_quarantine(t.rider, t.horse);
                ++n;
            }
        }
        return n;
    }

    // A bound of a held stream was reached: every waiting Begin is cancelled (the stream goes on as before).
    size_t cancel_holds() {
        size_t n = 0;
        for (Txn& t : slot_) {
            if (t.phase == Phase::Begun) {
                t.phase = Phase::Cancelled;
                ++n;
            }
        }
        return n;
    }

    // ride_filter leaves this rider's action for the dispatch to decide (mode 1): a transaction that will give (or
    // gave) this client the native link.
    bool defers(uint32_t rider, uint64_t now_ms) const {
        const Txn* t = find(rider);
        if (!t) return false;
        switch (t->phase) {
            case Phase::Begun: return now_ms - t->begun_ms < kCommitWaitMs;
            case Phase::Committed:
            case Phase::Applying:
            case Phase::Riding:
            case Phase::DetachPending:
            case Phase::Detaching: return true;
            default: return false;
        }
    }

    // The client thread, at a StartActionAck dispatch of `key` (any actor): the work to do now, claimed.
    // A commit is claimed at the first dispatch after its Commit, whichever actor's (the rider's own mount action
    // is normally a few ms behind: S17 2-56 ms), and at the latest at the rider's own StartActionAck, before its
    // handler: never after it.
    struct Work {
        Plan plan = Plan::None;
        Txn txn;
        bool relink = false;        // P8 S26: the commit applied again (relink_due, mount_relink=1)
        bool relink_probe = false;  // mount_relink=probe: it WOULD be applied again now (nothing claimed)
    };
    // `rn`: the remote rider as read at this dispatch (ignored for another rider). `act`: the remote rider's own
    // action being dispatched (P8 S26 relink; act.own false for another actor's).
    Work plan(uint32_t key, uint64_t now_ms, const RiderNow& rn = {}, const RiderAction& act = {}) {
        const uint64_t stamp = now_ms ? now_ms : 1;
        Txn* probe = nullptr;  // mount_relink=probe: reported only if nothing else is planned now
        for (Txn& t : slot_) {
            if (t.phase == Phase::Committed) {
                t.phase = Phase::Applying;
                t.claim_ms = stamp;
                t.claimed_at_rider = t.rider == key;
                ++t.attempts;
                return {Plan::Commit, t};
            }
            if (relink_ != Mode::Off && relink_due(t, key, now_ms, rn, act)) {
                if (relink_ == Mode::Probe) {
                    if (!t.relink_probed && !probe) probe = &t;
                    continue;
                }
                t.phase = Phase::Applying;
                t.claim_ms = stamp;
                t.claimed_at_rider = true;  // NotReady here: cancelled (never linked after this action ran)
                t.relinking = true;
                ++t.attempts;
                ++t.relinks;
                Work w{Plan::Commit, t};
                w.relink = true;
                return w;
            }
            const RiderNow* r = rn.rider == t.rider && rn.known ? &rn : nullptr;
            if (policy_ == DetachPolicy::Native &&
                (t.phase == Phase::DetachPending || (t.phase == Phase::Riding && t.on_foot_ms))) {
                if (r) {
                    t.unreadable_ms = 0;
                } else if (!t.unreadable_ms) {
                    t.unreadable_ms = stamp;
                } else if (now_ms - t.unreadable_ms >= kNativeUnreadableMs) {
                    t.native_lost = true;
                    t.detach_flag = kNativeDetachFlag;
                    t.detach_aux = 0;
                    if (t.phase == Phase::Riding) t.detach_ms = stamp;
                    claim_detach(t, stamp);
                    return {Plan::Detach, t};
                }
            }
            if (t.phase == Phase::DetachPending) {
                if (policy_ == DetachPolicy::Native) {
                    const NativeDue d = native_due(t, now_ms, r);
                    if (d != NativeDue::No) {
                        t.native_gone = d == NativeDue::Gone;
                        t.native_fallback = d == NativeDue::Fallback;
                        t.detach_flag = kNativeDetachFlag;
                        t.detach_aux = 0;
                        claim_detach(t, stamp);
                        return {Plan::Detach, t};
                    }
                } else if (detach_due(t, now_ms)) {
                    claim_detach(t, stamp);
                    return {Plan::Detach, t};
                }
            }
            if (no_event_due(t, now_ms) && (policy_ != DetachPolicy::Native || (r && !ride_method(r->method)))) {
                t.no_event = true;
                t.detach_aux = 0;
                t.detach_flag = policy_ == DetachPolicy::Native ? kNativeDetachFlag : kClientDetachFlag;
                t.native_gone = policy_ == DetachPolicy::Native && r->link_vehicle != t.horse;
                t.detach_ms = stamp;
                claim_detach(t, stamp);
                return {Plan::Detach, t};
            }
        }
        if (probe) {
            probe->relink_probed = true;
            Work w{Plan::None, *probe};
            w.relink_probe = true;
            return w;
        }
        return {};
    }
    bool has_work() const {
        for (const Txn& t : slot_) {
            if (t.phase == Phase::Committed || t.phase == Phase::DetachPending) return true;
            if (t.phase == Phase::Riding && t.on_foot_ms) return true;  // a no-event candidate
        }
        return false;
    }

    // The remote rider's StartActionAck at the dispatch: its Path parent (u64, all ones = none) and its target.
    // While its transaction rides here (or waits for its retirement): an 0xB parent = mounted (clears the on-foot
    // time); parent -1 and not naming its horse (the mount's own action names it: S17) = on foot since now.
    void note_rider_parent(uint32_t rider, uint64_t parent, uint32_t target, uint64_t now_ms) {
        Txn* t = slot_of(rider);
        if (!t || (t->phase != Phase::Riding && t->phase != Phase::DetachPending)) return;
        const bool mounted = (parent >> 32) == 0 && is_b(static_cast<uint32_t>(parent));
        if (mounted) {
            t->on_foot_ms = 0;
        } else if (parent == ~0ull && target != t->horse && !t->on_foot_ms) {
            t->on_foot_ms = now_ms ? now_ms : 1;
        }
    }

    // `retired_old`: the commit first retired the link this client had made to the rider's previous horse.
    void finish_commit(uint32_t rider, uint32_t transition, Outcome o, bool retired_old = false) {
        Txn* t = slot_of(rider);
        if (!t || t->transition != transition || t->phase != Phase::Applying) return;
        if (retired_old) t->linked_here = 0;
        switch (o) {
            case Outcome::Applied:
            case Outcome::AlreadyApplied:
                t->linked_here = t->horse;
                t->applied = true;
                t->phase = Phase::Riding;
                break;
            case Outcome::ProbeOnly: t->phase = Phase::Riding; break;
            case Outcome::NotReady:
                t->phase = (t->claimed_at_rider || t->attempts >= kMaxAttempts) ? Phase::Cancelled : Phase::Committed;
                break;
            case Outcome::Refused: t->phase = Phase::Cancelled; break;
            case Outcome::Failed:
                // Review of bf255bb (2): a relink failed in a state no session ever captured (the link retired
                // natively before RideOn): only this transaction is quarantined, never the pair for the connection.
                if (!t->relinking) add_quarantine(t->rider, t->horse);
                t->linked_here = t->horse;  // a part of a link may be there: a retirement here is allowed once
                t->phase = Phase::Quarantined;
                break;
        }
        t->relinking = false;
        if (t->detach_queued) {
            t->detach_queued = false;
            if (t->phase == Phase::Riding || t->phase == Phase::Quarantined) t->phase = Phase::DetachPending;
        }
    }
    void finish_detach(uint32_t rider, uint32_t transition, Outcome o) {
        Txn* t = slot_of(rider);
        if (!t || t->transition != transition || t->phase != Phase::Detaching) return;
        if (o == Outcome::Failed) {
            t->phase = Phase::Quarantined;
            add_quarantine(t->rider, t->horse);
        } else {
            if (o == Outcome::Applied || o == Outcome::AlreadyApplied) t->linked_here = 0;
            t->phase = Phase::Detached;  // exactly once: never planned again
        }
    }

    // P8 S26: this client's RideOn::Enter ran for the rider (its mount action's handler): the link is RideOn's from
    // here on, never applied again (relink_due).
    void note_ride_on_enter(uint32_t rider) {
        if (Txn* t = slot_of(rider); t && (t->phase == Phase::Riding || t->phase == Phase::Applying)) {
            t->ride_on_entered = true;
        }
    }

    // This client's RideOff::Enter ran for the rider (the dismount action started here): its retirement may follow.
    void note_ride_off_enter(uint32_t rider) {
        if (Txn* t = slot_of(rider); t && (t->phase == Phase::Riding || t->phase == Phase::DetachPending)) {
            t->ride_off_entered = true;
        }
    }

    const Txn* find(uint32_t rider) const {
        for (const Txn& t : slot_) {
            if (t.rider == rider && rider != 0) return &t;
        }
        return nullptr;
    }
    bool quarantined(uint32_t rider, uint32_t horse) const {
        for (size_t i = 0; i < quarantine_n_; ++i) {
            if (quarantine_[i][0] == rider && quarantine_[i][1] == horse) return true;
        }
        return false;
    }

private:
    // A new transaction of the same rider: everything but the link this client made.
    static void renew(Txn& t) {
        const uint32_t rider = t.rider, linked = t.linked_here;
        t = Txn{};
        t.rider = rider;
        t.linked_here = linked;
    }
    void take_detach(Txn& t, const Event& e, uint64_t now_ms) const {
        t.detach_ms = now_ms ? now_ms : 1;
        t.detach_aux = e.aux;
        t.detach_flag = e.fifth;
        t.detach_observed = (e.flags & kFlagObserved) != 0;
        // Review of 458cf49 (3), Native: an on-foot mark from BEFORE the Detach (an action between mounts, a stale
        // one) must not count for the fallback; only one dispatched after it. The Detach is published 16-31 ms after
        // the controller's RideOff::Enter, its on-foot actions come ~1 s later: resetting here never loses a real one
        // for long (the next on-foot action sets it again).
        if (policy_ == DetachPolicy::Native) t.on_foot_ms = 0;
    }
    static void claim_detach(Txn& t, uint64_t stamp) {
        t.phase = Phase::Detaching;
        t.claim_ms = stamp;
        t.detach_tried = true;
    }
    static bool detach_due(const Txn& t, uint64_t now_ms) {
        if (t.ride_off_entered) return true;
        if (t.detach_observed && (t.on_foot_ms || now_ms - t.detach_ms >= kDetachObservedForceMs)) return true;
        return now_ms - t.detach_ms >= kDetachForceMs;
    }
    // DetachPolicy::Native: Gone = the link this client made is no longer there (the native retirement ran: nothing
    // to call); Fallback = still there kNativeFallbackMs after the Detach, the rider is not in RideOn/RideOff and an
    // on-foot action of it was already dispatched here (the dismount action is behind it, never ahead).
    enum class NativeDue : uint8_t { No, Gone, Fallback };
    static NativeDue native_due(const Txn& t, uint64_t now_ms, const RiderNow* r) {
        if (!r) return NativeDue::No;  // nothing readable: wait (never a blind call)
        if (r->link_vehicle != t.horse) return NativeDue::Gone;
        if (ride_method(r->method)) return NativeDue::No;  // never in RideOn / RideOff
        if (now_ms - t.detach_ms >= kNativeFallbackMs && t.on_foot_ms) return NativeDue::Fallback;
        // Review of 458cf49 (3): a cap, a rider that never sends an on-foot action (idle after a retirement without
        // RideOff): the link left over is retired anyway, long after any queued dismount could still be on its way.
        if (now_ms - t.detach_ms >= kNativeFallbackCapMs) return NativeDue::Fallback;
        return NativeDue::No;
    }
    static bool no_event_due(const Txn& t, uint64_t now_ms) {
        return t.phase == Phase::Riding && t.on_foot_ms && now_ms - t.on_foot_ms >= kDetachNoEventMs;
    }
    Txn* slot_of(uint32_t rider) {
        for (Txn& t : slot_) {
            if (t.rider == rider && rider != 0) return &t;
        }
        return nullptr;
    }
    Txn* alloc(uint32_t rider) {
        for (Txn& t : slot_) {
            if (t.rider == 0) return &t;
        }
        // Full: reuse a finished one, else the oldest.
        Txn* best = &slot_[0];
        for (Txn& t : slot_) {
            if (t.phase == Phase::Detached || t.phase == Phase::Cancelled || t.phase == Phase::Idle) return &t;
            if (t.begun_ms < best->begun_ms) best = &t;
        }
        (void)rider;
        return best;
    }
    void add_quarantine(uint32_t rider, uint32_t horse) {
        if (quarantined(rider, horse)) return;
        if (quarantine_n_ < quarantine_.size()) {
            quarantine_[quarantine_n_++] = {rider, horse};
        } else {
            quarantine_[quarantine_.size() - 1] = {rider, horse};
        }
    }
    std::array<Txn, kSlots> slot_{};
    std::array<std::array<uint32_t, 2>, 8> quarantine_{};
    size_t quarantine_n_ = 0;
    DetachPolicy policy_ = DetachPolicy::S18;
    Mode relink_ = Mode::Off;
};

// ---------------------------------------------------------------- P8 S18 fix B: the request gate (thunk 0x2329A90)

// pendientes/p8-s18-desmontar-nativo.md §6 [V code, build 2976]: 0x863880 (ClientCC vf44, a received 2991/2723)
// calls the thunk 0x2329A90 (jmp .xtls 0xEE58A40) at 0x8639C8 with RCX = the CC and RDX = its two 0x78-byte entries
// (one per layer, built by 0x1EC9A80 / 0x1EC9C00); the return address is exe+0x8639CD. 0xEE58A40 copies both entries
// first (0xEE58A7A) and drops the request when (+0x50 bit 1 or +0x51 bit 3 of either entry) and any of the pending
// [CC+0x80] entries' or the layers' current definitions has def+0xA & 0x40. The other branch (0xEE58B83) is the one
// the controller's local chart takes. Fix B: for the stuck remote rider only, the original is called with a copy of
// the entries without those two bits (never a write to the game's memory).
inline constexpr uint32_t kGateEntrySize = 0x78;
inline constexpr uint32_t kGateEntries = 2;
inline constexpr uint32_t kGateBytes = kGateEntrySize * kGateEntries;
inline constexpr uint32_t kEntryHash = 0x00, kEntryDef = 0x18, kEntryNet = 0x50, kEntryForce = 0x51, kEntryLocal = 0x52;
inline constexpr uint8_t kEntryNetBit = 0x02, kEntryForceBit = 0x08, kEntryLocalBit = 0x01;
inline constexpr uint8_t kDefDismount = 0x40;  // def+0xA: "get off the vehicle"
inline constexpr uint32_t kDefFlags = 0x0A;

struct GateView {
    bool network_call = false;  // the return address is exe+0x8639CD (0x863880: a received action)
    bool remote_rider = false;  // [CC+8] is the known remote player character (never the local one)
    uint8_t c8 = 0xFF;          // its actor+0xC8 (0 = a remote copy here)
    bool readable = false;      // every read below worked
    uint8_t method = 0xFF;      // mc+0xF0
    bool pending_busy = false;  // CC+0xA8 != 0 (a request not yet taken by vf89)
    bool cur40 = false;         // a layer's current definition has 0x40
    bool pend40 = false;        // a pending [CC+0x80] entry's definition has 0x40
    bool new40 = false;         // a new entry's definition has 0x40 (another dismount: never let in twice)
    bool net_bits = false;      // a new entry has +0x50 bit 1 or +0x51 bit 3 (what the gate tests)
    bool new52 = false;         // a new entry has +0x52 bit 0 (the other branch could drop it against a pending one)
    bool any_new = false;       // a new entry with a hash
    // Review of 0e8605c (2), (6): entry 0 / entry 1 carry a hash (0xEE58B83 copies BOTH entries to the pending ones,
    // the network branch only the hashed ones).
    bool hash0 = false, hash1 = false;
    // Review (1): actor+0x96 (0x85FBA0: a player-type actor runs its local chart only if != 0; 0xFF unknown).
    uint8_t chart = 0xFF;
};
enum class GateVerdict : uint8_t {
    NotNetwork,   // another caller (the local chart 0x232D4C1, 0xA7A10E, .xtls): untouched
    NotRider,     // not the remote player character / C8 != 0
    Unreadable,
    Empty,        // no new entry
    NoBits,       // nothing the gate would test: it passes anyway
    NoGate,       // no 0x40 current/pending: the gate passes anyway
    NotStuck,     // the gate would drop it, but the rider is not stuck (see gate_stuck): untouched
    NoHash0,      // entry 0 (layer 0) has no hash: 0xEE58B83 would copy an empty entry over the pending one
    Busy,         // CC+0xA8 set (a request pending this frame) and not both entries hashed
    NewDismount,  // the new action is itself a 0x40 one
    Flag52,       // a new entry has +0x52 bit 0
    Unlock,       // call the original with the bits cleared in a copy
    Count
};
inline const char* gate_verdict_name(GateVerdict v) {
    switch (v) {
        case GateVerdict::NotNetwork: return "not the network path";
        case GateVerdict::NotRider: return "not the remote rider";
        case GateVerdict::Unreadable: return "unreadable";
        case GateVerdict::Empty: return "no new entry";
        case GateVerdict::NoBits: return "no network bits";
        case GateVerdict::NoGate: return "no 0x40 current/pending (passes anyway)";
        case GateVerdict::NotStuck: return "would be dropped, not stuck";
        case GateVerdict::NoHash0: return "entry 0 has no hash";
        case GateVerdict::Busy: return "CC+0xA8 pending";
        case GateVerdict::NewDismount: return "the new action has 0x40";
        case GateVerdict::Flag52: return "+0x52 bit 0 set";
        case GateVerdict::Unlock: return "UNLOCKED";
        case GateVerdict::Count: break;
    }
    return "?";
}
// The gate (0xEE58A40) would drop this request: the network bits and a 0x40 definition current or pending.
inline bool gate_would_drop(const GateView& v) { return v.any_new && v.net_bits && (v.cur40 || v.pend40); }
// Stuck: a 0x40 action is CURRENT and the copy is in RideOff (method 7), or it never runs its chart (actor+0x96 == 0:
// review (1), a current 0x40 action outside method 7 - RideOff::Enter refused, or still method 4 - is stuck the same
// way); or only a stale pending 0x40 entry while in RideOff.
inline bool gate_stuck(const GateView& v) {
    if (v.cur40 && (v.method == kMethodRideOff || v.chart == 0)) return true;
    return v.pend40 && v.method == kMethodRideOff;
}
inline GateVerdict gate_decide(const GateView& v) {
    if (!v.network_call) return GateVerdict::NotNetwork;
    if (!v.remote_rider || v.c8 != 0) return GateVerdict::NotRider;
    if (!v.readable) return GateVerdict::Unreadable;
    if (!v.any_new) return GateVerdict::Empty;
    if (!v.net_bits) return GateVerdict::NoBits;
    if (!v.cur40 && !v.pend40) return GateVerdict::NoGate;
    if (!gate_stuck(v)) return GateVerdict::NotStuck;
    if (!v.hash0) return GateVerdict::NoHash0;
    // Review (6): a second network action in the same frame after an Unlock (CC+0xA8 = 1, our copy pending) may
    // replace it when it is a whole request (both layers); a partial one would mix with the pending one.
    if (v.pending_busy && !(v.hash0 && v.hash1)) return GateVerdict::Busy;
    if (v.new40) return GateVerdict::NewDismount;
    if (v.new52) return GateVerdict::Flag52;
    return GateVerdict::Unlock;
}

// Review (1): the rider stays stuck when the gate keeps dropping its actions without an Unlock (a state this
// decision does not cover). After kGateDegradeAfter such drops in a row the detour is reported degraded: ride_filter
// rewrites the dismounts again (as S17) from then on. An Unlock or a rider that is not stuck resets the streak.
inline constexpr uint32_t kGateDegradeAfter = 8;
struct GateStreak {
    uint32_t drops = 0;
    bool degraded = false;
    // True exactly once: this verdict made it degraded.
    bool on(GateVerdict v, bool would_drop) {
        if (v == GateVerdict::Unlock || (!would_drop && v != GateVerdict::NotNetwork && v != GateVerdict::NotRider)) {
            drops = 0;
            return false;
        }
        if (!would_drop) return false;
        ++drops;
        if (drops >= kGateDegradeAfter && !degraded) {
            degraded = true;
            return true;
        }
        return false;
    }
};
// The mod's copy of the two entries: the network bits cleared (only those two bits; everything else as received).
inline void gate_clear_bits(uint8_t* entries) {
    for (uint32_t i = 0; i < kGateEntries; ++i) {
        entries[i * kGateEntrySize + kEntryNet] = static_cast<uint8_t>(entries[i * kGateEntrySize + kEntryNet] & ~kEntryNetBit);
        entries[i * kGateEntrySize + kEntryForce] =
            static_cast<uint8_t>(entries[i * kGateEntrySize + kEntryForce] & ~kEntryForceBit);
    }
}

// ---------------------------------------------------------------- the dispatch's fallback

// At the dispatch of the remote player's StartActionAck, after any commit work: the action enters RideOn/RideOff
// (ride_filter's kind != none) and the rider has no link here -> rewrite it in place as ride_filter would have
// (only when ride_filter itself is 1: the packet was left to us).
// `gate_lost` (p8-impl-monturas.md §12): a dismount (ride_dismount=native) that ride_filter deferred while fix B was
// ready, but fix B went off / DEGRADED before this dispatch (the guest decides in guest_deliver, before the client's
// inbound queue): RideOff would be entered with nothing to let the copy out (S18), so it is rewritten here, as
// ride_filter does from then on. The link is then retired by the native fallback.
inline bool fallback_rewrite(bool ride_filter_on, bool ride_kind, bool link_present, bool gate_lost = false) {
    return ride_filter_on && ride_kind && (!link_present || gate_lost);
}
// The dismount the dispatch must not let into RideOff any more: native, and fix B not ready now.
inline bool dismount_gate_lost(bool dismount_kind, bool native, bool gate_ready) {
    return dismount_kind && native && !gate_ready;
}

// ---------------------------------------------------------------- P8 S21: seat the remote rider (mount_seat)

// pendientes/p8-impl-monturas.md §13 [V capstone, build 2976]: the game seats a rider only through ClientVehicle vf54
// 0xA45760(RCX vc, RDX &res, R8 rider, R9 seat zero-extended; no stack argument, returns &res), always right after vf53
// (the controller .xtls 0xEE609B8/0xEE609DB, the client's re-commit of remote NPC pairs 0xA469EA/0xA46A08). vf54 ->
// 0x2228A70, then 0xA462B0(vc, rider, seat), which for a horse of actor type [[horse+0x88]+1] in {1, 3..6, 8, 9}
// (jump table 0xA4660C; 7 takes another branch) calls 0xA651E0([CC+0x100], horse, &info type 4): the rider's attach
// type [[CC+0x100]+8] becomes 4 and its hierarchy parent [[[actor+0x68]+0x1B8]+0x1C] the horse key. The movement
// update 0x88EB70 -> 0x87BA80 then treats the rider as attached and skips the network interpolation (0x88F4E0 /
// 0x891180): it follows this client's saddle bone. The observer's vf53 alone left type 9 "none" (S20: up to 1.4 m off).
// vf54 reads [[rider+0x88]+1] unchecked (1, 3..6, 8, 9 seat; anything else *res != 0).
inline constexpr uint8_t kActorPlayer = 1, kActorWorld = 7;
inline bool seat_horse_type(uint8_t t) { return t == 1 || (t >= 3 && t <= 9 && t != kActorWorld); }

// The rider as vf54 / 0xA651E0 see it (SEH-guarded reads on the client main thread).
struct SeatState {
    bool has_cc = false;         // rider comps+0x40
    bool has_mc = false;         // CC+0xC8 (its movement controller)
    bool has_cc100 = false;      // [CC+0x100] (the attach struct) and its +8 readable
    uint8_t attach_type = 0xFF;  // [[CC+0x100]+8] (0xFF unreadable)
    bool has_parent = false;     // [[actor+0x68]+0x1B8] and its +0x1C readable
    uint32_t parent_key = 0;     // [[[actor+0x68]+0x1B8]+0x1C]
    uint8_t cc228 = 0xFF;        // CC+0x228: 1 once CC vf19 (0x85E2B0) ran (component initialised); vf42 detaches only then
    uint8_t rider_type = 0xFF;   // [[rider+0x88]+1] (1 player)
    uint8_t horse_type = 0xFF;   // [[horse+0x88]+1]
    uint8_t parent_type = 0xFF;  // [[[actor+0x68]+0x1B8]+0x18]: the hierarchy's attach type (0x15ECA10 writes it with the key)
    bool horse_cc = false;       // the horse's comps+0x40: without it the native release only clears the parent (stays 4)
    uint8_t rider_local = 0xFF;  // byte rider+0x94 (one half of vf178 0x4FE260; 1 on both players' characters)
    uint32_t vf178 = 0;          // RVA of the rider's vtable slot +0x178 (0x4FE260 every client in-game actor)
    uint64_t rider_user = 0;     // [rider+0xA0] (its user object)
    uint64_t local_user = 0;     // [[WorldSystem+0x30]+0x58] (this client's user object)
    uint8_t rider_c8 = 0xFF;     // rider +0xC8 (0 a remote copy here, 1 / 2 controlled here; 0xFF unreadable)
    uint8_t horse_c8 = 0xFF;     // horse +0xC8
};

// P8 S22 [V capstone 0x4FE260]: what the rider's vf178 answers, i.e. whether vf54's tail would launch 0xA45AC0 on the
// LOCAL player: `[+0xA0] == local user && [+0x94]`. 0x8A67D0 (+0x94 alone) is the account/server realm's, never a
// client in-game actor's; any other slot (or an unread local user) counts as local, so the seat is refused.
inline constexpr uint32_t kVf178Actor = 0x4FE260, kVf178User = 0x8A67D0;
inline bool seat_local_user(const SeatState& s) {
    if (s.vf178 == kVf178Actor) {
        return s.local_user == 0 || s.rider_user == 0 || (s.rider_user == s.local_user && s.rider_local != 0);
    }
    if (s.vf178 == kVf178User) return s.rider_local != 0;
    return true;
}

enum class SeatVerdict : uint8_t { Apply, AlreadySeated, Probe, Refused, Count };
enum class SeatWhy : uint8_t {
    None,
    Off,         // mount_seat 0 (or mount_sync not 1)
    NotLinked,   // the rider's Link+0x10/+0x14 is not this horse / seat (vf53 did not leave it)
    NotSeat0,    // only seat 0 (the saddle) is seated
    NoMc,        // no CC / movement controller
    NoAttach,    // [CC+0x100] or its type unreadable
    AttachBusy,  // already attached some other way (type != 9 "none", or 4 to another parent)
    RiderType,   // not a player character (type != 1)
    HorseType7,  // a type 7 vehicle: 0xA462B0 does not attach to the seat bone
    HorseType,   // a type 0xA462B0 does not seat at all (or unreadable)
    Local,       // the rider is this client's own character (never seated here)
    ParentSet,   // a hierarchy parent already set (or unreadable) while the type says "none"
    NotInit,     // CC+0x228 != 1: its CC vf19 has not run (the native release would not detach it)
    LocalCtl,    // its vf178 says "local user": vf54 would launch a riding action on the local player
    Controlled,  // rider or horse not a remote copy here (C8 != 0, or unreadable)
    HorseNoCC,   // the horse has no CC: the native release would leave the rider at type 4
    Count
};
inline const char* seat_why_name(SeatWhy w) {
    switch (w) {
        case SeatWhy::None: return "-";
        case SeatWhy::Off: return "mount_seat off";
        case SeatWhy::NotLinked: return "the rider's Link is not this horse/seat";
        case SeatWhy::NotSeat0: return "not seat 0";
        case SeatWhy::NoMc: return "no CC / movement controller";
        case SeatWhy::NoAttach: return "[CC+0x100]+8 unreadable";
        case SeatWhy::AttachBusy: return "attach type already set (not by this horse)";
        case SeatWhy::RiderType: return "rider actor type != 1";
        case SeatWhy::HorseType7: return "horse actor type 7";
        case SeatWhy::HorseType: return "horse actor type not seated by 0xA462B0";
        case SeatWhy::Local: return "the rider is the local character";
        case SeatWhy::ParentSet: return "a hierarchy parent is already set (or unreadable)";
        case SeatWhy::NotInit: return "CC+0x228 != 1 (component not initialised)";
        case SeatWhy::LocalCtl: return "its vf178 answers local user (vf54's local action request)";
        case SeatWhy::Controlled: return "rider or horse C8 != 0 (controlled here, or unreadable)";
        case SeatWhy::HorseNoCC: return "the horse has no CC";
        case SeatWhy::Count: break;
    }
    return "?";
}
struct SeatJudge {
    SeatVerdict verdict = SeatVerdict::Refused;
    SeatWhy why = SeatWhy::None;
};
// After vf53 left (or found) the rider's Link to `horse` / `seat`. Apply only for seat 0 of a horse 0xA462B0 seats,
// a player rider with a readable attach struct of type 9 "none" (S22); already type 4 under this horse = AlreadySeated.
inline SeatJudge seat_judge(Mode m, const PairState& p, const SeatState& s, uint32_t horse, uint8_t seat,
                            uint32_t rider_key = 0, uint32_t local_key = 0) {
    if (m == Mode::Off) return {SeatVerdict::Refused, SeatWhy::Off};
    // S21/S22 reviews: never while the local key is unknown (every character starts at 9, the local one too).
    if (rider_key == 0 || local_key == 0 || rider_key == local_key) return {SeatVerdict::Refused, SeatWhy::Local};
    if (!p.rider_found || !p.link || horse == 0 || p.cur_vehicle != horse || p.cur_seat != static_cast<int8_t>(seat)) {
        return {SeatVerdict::Refused, SeatWhy::NotLinked};
    }
    if (seat != 0) return {SeatVerdict::Refused, SeatWhy::NotSeat0};
    if (!s.has_cc || !s.has_mc) return {SeatVerdict::Refused, SeatWhy::NoMc};
    if (!s.has_cc100 || s.attach_type == 0xFF) return {SeatVerdict::Refused, SeatWhy::NoAttach};
    if (s.attach_type == kAttachSeat && s.has_parent && s.parent_key == horse) return {SeatVerdict::AlreadySeated, SeatWhy::None};
    if (s.attach_type != kAttachNone) return {SeatVerdict::Refused, SeatWhy::AttachBusy};
    if (!s.has_parent || s.parent_key != 0) return {SeatVerdict::Refused, SeatWhy::ParentSet};
    if (s.cc228 != 1) return {SeatVerdict::Refused, SeatWhy::NotInit};
    // S22: the remote player's character reads +0x94 = 1 (both PCs); what counts is its vf178's answer.
    if (seat_local_user(s)) return {SeatVerdict::Refused, SeatWhy::LocalCtl};
    // S23 review: only a remote copy here (C8 0, read; 1 / 2 are controlled here, 0xFF unreadable), rider and horse.
    if (s.rider_c8 != 0 || s.horse_c8 != 0) return {SeatVerdict::Refused, SeatWhy::Controlled};
    if (s.rider_type != kActorPlayer) return {SeatVerdict::Refused, SeatWhy::RiderType};
    if (s.horse_type == kActorWorld) return {SeatVerdict::Refused, SeatWhy::HorseType7};
    if (!seat_horse_type(s.horse_type)) return {SeatVerdict::Refused, SeatWhy::HorseType};
    if (!s.horse_cc) return {SeatVerdict::Refused, SeatWhy::HorseNoCC};
    return {m == Mode::On ? SeatVerdict::Apply : SeatVerdict::Probe, SeatWhy::None};
}

enum class SeatPost : uint8_t { Ok, Result, Unreadable, Unchanged, Type, Parent, Count };
inline const char* seat_post_name(SeatPost p) {
    switch (p) {
        case SeatPost::Ok: return "ok";
        case SeatPost::Result: return "*res != 0";
        case SeatPost::Unreadable: return "attach type unreadable after";
        case SeatPost::Unchanged: return "attach type unchanged";
        case SeatPost::Type: return "attach type not 4";
        case SeatPost::Parent: return "hierarchy parent (or its type 4) not the horse";
        case SeatPost::Count: break;
    }
    return "?";
}
// After vf54 returned (read back, never written): *res 0, attach type 4 and the hierarchy parent the horse.
inline SeatPost seat_verify(const SeatState& pre, const SeatState& post, int32_t res, uint32_t horse) {
    if (res != 0) return SeatPost::Result;
    if (!post.has_cc100 || post.attach_type == 0xFF) return SeatPost::Unreadable;
    if (post.attach_type != kAttachSeat) return post.attach_type == pre.attach_type ? SeatPost::Unchanged : SeatPost::Type;
    // The hierarchy's own type (hier+0x18, 0x15ECA10) is logged, not judged: no live read confirms that offset yet.
    if (!post.has_parent || post.parent_key != horse) return SeatPost::Parent;
    return SeatPost::Ok;
}

// The release of a seat this client made. Native [E]: the dismount's retirement (0x2228E40 -> 0xE6F2E60 -> the
// rider's vc vf42 0xA45CA0 -> 0xA65960(CC+0x100, ...) when [CC+0x228] != 0) drops the type 4 -> 9. A release is
// expected after the observer's retirement of that rider, its RideOff::Enter here or an on-foot action of it; the
// main thread's dispatch steps the watch at most every 30 ms. Still 4 kSeatReleaseMs later with the Link still naming
// the horse: ONE retirement 0x2228E40(flag 12) (never while the rider is in RideOn/RideOff, up to kSeatRideCapMs);
// still 4 after that, or the Link already gone: "stuck seated" (reported once, nothing called).
inline constexpr uint64_t kSeatReleaseMs = 1500;
inline constexpr uint64_t kSeatRideCapMs = 6000;
inline constexpr uint64_t kSeatLostMs = 3000;  // the rider unreadable this long: forgotten (its actor is gone)
enum class SeatCause : uint8_t { None, Detach, RideOff, OnFoot, Retired, Count };
inline const char* seat_cause_name(SeatCause c) {
    switch (c) {
        case SeatCause::None: return "no dismount seen";
        case SeatCause::Detach: return "the observer's retirement here";
        case SeatCause::RideOff: return "its RideOff::Enter here";
        case SeatCause::OnFoot: return "its first on-foot action";
        case SeatCause::Retired: return "the mod's retirement 0x2228E40 (flag 12)";
        case SeatCause::Count: break;
    }
    return "?";
}
enum class SeatRel : uint8_t { None, Released, Retire, Stuck, Lost };
struct SeatStep {
    SeatRel rel = SeatRel::None;
    SeatCause cause = SeatCause::None;
    uint64_t after_ms = ~0ull;  // since the expectation (~0: none)
    uint64_t seated_ms = 0;     // since the seat
    uint8_t type = 0xFF;        // the attach type now
};
class SeatWatch {
public:
    void seat(uint32_t rider, uint32_t horse, uint64_t now_ms) {
        forget();
        rider_ = rider;
        horse_ = horse;
        seat_ms_ = now_ms;
    }
    void forget() {
        const bool ok = retire_ok_;
        *this = SeatWatch{};
        retire_ok_ = ok;
    }
    uint32_t rider() const { return rider_; }
    uint32_t horse() const { return horse_; }
    bool expecting() const { return expect_ms_ != 0; }
    // A release is expected from now (the first cause counts; ignored for another rider or when nothing is watched).
    // S21 review: a stamp older than this seat (a RideOff::Enter of the previous ride) is ignored; an on-foot
    // expectation is replaced by a stronger cause (only those may lead to the mod's retirement, see step()).
    void expect(uint32_t rider, SeatCause c, uint64_t now_ms) {
        if (!rider_ || rider != rider_ || now_ms < seat_ms_) return;
        if (expect_ms_ && !(cause_ == SeatCause::OnFoot && c != SeatCause::OnFoot && !retired_)) return;
        expect_ms_ = now_ms ? now_ms : 1;
        cause_ = c;
    }
    // A mounted action of the rider (an 0xB parent): an on-foot expectation was a stale action, not a dismount.
    void mounted(uint32_t rider) {
        if (rider_ && rider == rider_ && cause_ == SeatCause::OnFoot && !retired_) {
            expect_ms_ = 0;
            cause_ = SeatCause::None;
        }
    }
    // `type`: [[CC+0x100]+8] now (0xFF unreadable); `link_vehicle`: its Link+0x10; `ride_method`: in RideOn/RideOff.
    SeatStep step(uint8_t type, uint32_t link_vehicle, bool ride_method, uint64_t now_ms) {
        SeatStep st;
        if (!rider_) return st;
        st.type = type;
        st.cause = cause_;
        st.after_ms = expect_ms_ && now_ms >= expect_ms_ ? now_ms - expect_ms_ : ~0ull;
        st.seated_ms = now_ms >= seat_ms_ ? now_ms - seat_ms_ : 0;
        if (type == 0xFF) {
            if (!lost_ms_) {
                lost_ms_ = now_ms ? now_ms : 1;
            } else if (now_ms - lost_ms_ >= kSeatLostMs) {
                forget();
                st.rel = SeatRel::Lost;
            }
            return st;
        }
        lost_ms_ = 0;
        if (type != kAttachSeat) {
            forget();
            st.rel = SeatRel::Released;
            return st;
        }
        if (!expect_ms_ || now_ms - expect_ms_ < kSeatReleaseMs) return st;
        // An on-foot action alone never retires a rider whose Link still names the horse (a mounted action without
        // the 0xB parent would otherwise unseat a live rider): the table's own on-foot fallback sends a Detach.
        if (cause_ == SeatCause::OnFoot && link_vehicle == horse_) return st;
        if (link_vehicle == horse_ && !retired_ && retire_ok_) {
            if (ride_method && now_ms - expect_ms_ < kSeatRideCapMs) return st;
            retired_ = true;
            expect_ms_ = now_ms ? now_ms : 1;  // the retirement gets its own wait
            cause_ = SeatCause::Retired;
            st.rel = SeatRel::Retire;
            return st;
        }
        // Reported once, then the watch ends (S21 review: no find_actor every 30 ms for the rest of the session).
        forget();
        st.rel = SeatRel::Stuck;
        return st;
    }
    // ride_dismount=native only: otherwise a seat that outlives its release is reported, never retired here.
    void set_retire(bool ok) { retire_ok_ = ok; }

private:
    uint32_t rider_ = 0, horse_ = 0;
    uint64_t seat_ms_ = 0, expect_ms_ = 0, lost_ms_ = 0;
    SeatCause cause_ = SeatCause::None;
    bool retired_ = false, retire_ok_ = true;
};

// ---------------------------------------------------------------- P8 S21: hold the remote rider's pose (ride_hold_pos)

// [V capstone, build 2976] On the observer RideOn::Enter 0x681C80 calls 0x88E520(mc) at 0x681DBC (.xtls body [E]),
// which empties the rider's interpolation buffer and sets byte [mc+0x3D0] = 1 (cleared when a new sample is set,
// 0x87DB2A). The controller sends no Move during the 1-2 s of the mount animation, and each frame the mode-2 update
// 0x88E6A0(RCX mc, XMM1 dt, R8 out) returns at 0x88E6D3/0x88E6D6 (`cmp byte [rcx+0x3D0],1; je 0x88E8B6`) WITHOUT
// writing `out`. S22 [V Ghidra]: that premise was false: UpdateByMode pre-fills `out` with the current pose
// (0x88FD56..0x88FD66), so the hold changes nothing; the blink was RideOn's warp from an unseated attach info
// (p8-impl-monturas.md 14.2). Kept as a counter (ride_hold_pos=probe).
// Its output: float pos[3] at +0x0 (in-sector), int16 sector x / z at +0xC / +0xE (0x88E82A / 0x88E82E). Its own
// current pose comes from 0x4F8220 on TS = [[actor+0x68]+0x1A0], whose fast path copies exactly TS+0xB4..+0xC3 (pos
// 12 bytes, sector u16 x at +0xC0, z at +0xC2). The actor of mc is [[mc+0x60]+8].
inline constexpr uint32_t kMcEmptyFlag = 0x3D0;
enum class HoldWho : uint8_t { None, Remote, Other };
// Whose early return this is: the tracked remote player (never the local one), another 0xA actor, or nobody to count.
inline HoldWho hold_who(bool early, uint32_t key, uint32_t remote, uint32_t local) {
    if (!early || key == 0 || key == local) return HoldWho::None;
    if (remote != 0 && key == remote) return HoldWho::Remote;
    return is_char(key) ? HoldWho::Other : HoldWho::None;
}
// A pose that may be written: finite, in-sector sized, and never the empty one (sector 0/0 with the position near 0).
inline bool hold_pose_ok(const float pos[3], int16_t sector_x, int16_t sector_z) {
    float l = 0;
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(pos[i]) || std::fabs(pos[i]) > 100000.f) return false;
        l += pos[i] * pos[i];
    }
    return !(sector_x == 0 && sector_z == 0 && l < 1.f);
}
enum class HoldAct : uint8_t { None, CountRemote, Fill, Unreadable, CountOther };
// Mode 1 fills `out` for the remote player only (probe and the other 0xA actors: counted, never written).
inline HoldAct hold_decide(Mode m, HoldWho who, bool pose_read, const float pos[3], int16_t sector_x, int16_t sector_z) {
    if (who == HoldWho::Other) return HoldAct::CountOther;
    if (who != HoldWho::Remote || m == Mode::Off) return HoldAct::None;
    if (m != Mode::On) return HoldAct::CountRemote;
    return pose_read && hold_pose_ok(pos, sector_x, sector_z) ? HoldAct::Fill : HoldAct::Unreadable;
}

// ---------------------------------------------------------------- the observer's self-check (both PCs)

// A StartActionReq 2223 (u16 opcode | i8 slot | u16 len | u32 actor ...) this client sends for the remote rider (1) or
// its horse (2); 0 = anything else. An observer must never request actions of the other player's pair (S18 §11.4).
inline constexpr uint16_t kOpStartActionReq = 2223;
inline uint8_t self_check_hit(const uint8_t* packet, size_t size, uint32_t rider, uint32_t horse) {
    if (!packet || size < kHeader + 4 || (packet[0] | (packet[1] << 8)) != kOpStartActionReq) return 0;
    const uint32_t key = get32(packet + kHeader);
    if (rider && key == rider) return 1;
    if (horse && key == horse) return 2;
    return 0;
}

// ---------------------------------------------------------------- the controller's outbound drain (guest)

// Two threads move this client's events from the lock-free ring into the queue to the host: the client's
// NetworkThread (guest_send, before each client packet) and the mod's network thread (guest_take_outgoing). A pop and
// its push are one step under this gate, so no event overtakes an earlier one (a Commit before its Begin), and the
// NetworkThread pushes the client's own packet inside the same step (a Req never overtakes the Begin that precedes
// it). A push the queue refuses is kept and offered again first (never silently lost) and counted.
class OutboundDrain {
public:
    // pop(std::vector<uint8_t>&) -> bool: the next event frame; push(const std::vector<uint8_t>&) -> bool: into the
    // queue. after() runs under the gate once the events are through (and only if none is left pending).
    // blocking = false: a busy gate returns at once (the other drainer is moving them). Returns the frames pushed.
    template <class Pop, class Push, class After>
    size_t drain(Pop pop, Push push, bool blocking, After after) {
        std::unique_lock lk(mu_, std::defer_lock);
        if (blocking) {
            lk.lock();
        } else if (!lk.try_lock()) {
            busy_.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        size_t n = 0;
        if (has_pending_) {
            if (!push(pending_)) {
                failed_.fetch_add(1, std::memory_order_relaxed);
                return 0;
            }
            has_pending_ = false;
            pending_.clear();
            ++n;
        }
        std::vector<uint8_t> f;
        while (pop(f)) {
            if (!push(f)) {
                pending_ = std::move(f);
                has_pending_ = true;
                failed_.fetch_add(1, std::memory_order_relaxed);
                return n;
            }
            ++n;
        }
        pushed_.fetch_add(n, std::memory_order_relaxed);
        after();
        return n;
    }
    template <class Pop, class Push>
    size_t drain(Pop pop, Push push, bool blocking) {
        return drain(pop, push, blocking, [] {});
    }
    // P8 re-entry (s25/reentrada.md §1.4): a new remote conversation drops what was left of the old one (a refused
    // event kept to offer again, and whatever pop still has), under the same gate as the drainers. Returns how many.
    template <class Pop>
    size_t discard(Pop pop) {
        std::lock_guard lk(mu_);
        size_t n = has_pending_ ? 1 : 0;
        has_pending_ = false;
        pending_.clear();
        std::vector<uint8_t> f;
        while (pop(f)) ++n;
        return n;
    }
    uint64_t failed() const { return failed_.load(std::memory_order_relaxed); }
    uint64_t busy() const { return busy_.load(std::memory_order_relaxed); }
    uint64_t pushed() const { return pushed_.load(std::memory_order_relaxed); }
    bool pending() {
        std::lock_guard lk(mu_);
        return has_pending_;
    }

private:
    std::mutex mu_;
    std::vector<uint8_t> pending_;
    bool has_pending_ = false;
    std::atomic<uint64_t> failed_{0}, busy_{0}, pushed_{0};
};

// The guest's held stream: true when one more packet of `bytes` fits.
inline bool hold_fits(size_t packets, size_t held_bytes, size_t bytes) {
    return packets < kHoldMaxPackets && held_bytes + bytes <= kHoldMaxBytes;
}

}  // namespace cdmp::session2::msl
