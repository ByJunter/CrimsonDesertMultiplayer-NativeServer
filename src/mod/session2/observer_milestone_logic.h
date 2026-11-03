#pragma once
// P8 S2: the native PlayAllPreLoadSequencerAck 2322 for user 2 when it enters,
// as an OBSERVER, a field it does not own (docs/research/pendientes/
// p8-2322-observador.md; the brief encargo-2322-observador.md and the verdict
// dictamen-2322-observador.md). The pure logic of the HOST's [session2]
// observer_milestone / observer_milestone_send keys, with no game access, so
// the tests can drive it: the trip ledger, the decision and its reasons.
//
// What it models (build 1.0.0.2976, RVAs; checked with capstone on the
// read-only PE, SHA-256 57DA440D...E1E7, see the document §2):
//   2174 MoveFieldAck              starts one client loading cycle (a "trip"); the server
//                                  writes the field owner at payload +9 (server.cpp rewrites
//                                  it in user 2's copy; the ledger keeps the ORIGINAL owner).
//   2144 MoveFieldCompleteAck      0x299A180, called at 0x2BC7433 in 0x2BC6BB0 with
//                                  ToTarget(traveller->vfE8()), BEFORE 0x2BC7475 calls
//                                  MoveFieldCompleteCore 0x2BC7670 on the same thread.
//   3066 MoveFieldCompletePostAck  0x299A610, called at 0x2BC80F0 INSIDE MoveFieldCompleteCore,
//                                  same ToTarget recipe, on every path that reaches 0x2BC84E2.
//   2322 PlayAllPreLoadSequencerAck 0x2999D80(actor, finder), natively only from 0x287F1D4 in
//                                  0x287EF60, which MoveFieldCompleteCore calls at 0x2BC84D4 for
//                                  the field OWNER only (0x2BC8470 cmp / 0x2BC8473 jne).
//   0x2BC84E2                      MoveFieldCompleteCore, both paths, after 0x2841D00; reached
//                                  only by falling through from 0x2BC84DD and only when
//                                  0x2B9B330 and 0x283E5A0 returned 0 (the success path).
// A direct (unbundled) send runs the session's Send synchronously on the
// sending thread: session 2's Send is the mod's take_frame, so the 2144 and
// 3066 of THIS entry have gone through take_frame on THIS thread before the
// entry hook runs, and the 2322 the hook emits goes through it during the
// call. That is what the ledger binds the decision to: the real result of
// g_outgoing.push for this trip's 2144 (and its 3066), never an observed phase.
//
// One trip = one 2174 to the live session 2 of the epoch: E<epoch>.T<gen>.
// At most ONE decision per trip (the first observer entry of user 2 in it),
// final: emit once, or refuse with a reason. No retry, no timer: a refused,
// failed or ambiguous trip stays without a 2322 from us and says why.

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "mod/session2/p8_probe_logic.h"

namespace cdmp::session2::oms {

inline constexpr uint16_t kOpMoveFieldAck = 2174;
inline constexpr uint16_t kOpMoveFieldCompleteAck = 2144;
inline constexpr uint16_t kOpMoveFieldCompletePostAck = 3066;
inline constexpr uint16_t kOpMilestone = 2322;
inline constexpr uint16_t kOpLoadingCompleteAck = 2762;
inline constexpr uint16_t kOpScopeSenderAck = 2042;   // a native bundle (scope sender): contents not parsed
inline constexpr uint16_t kOpMoveFieldCompleteReq = 2549;
inline constexpr uint16_t kOpLoadingCompleteReq = 2445;

// The take_frame opcodes the ledger follows (anything else is never handed to it).
constexpr bool followed_out(uint16_t op) {
    return op == kOpMoveFieldAck || op == kOpMoveFieldCompleteAck || op == kOpMoveFieldCompletePostAck ||
           op == kOpMilestone || op == kOpLoadingCompleteAck || op == kOpScopeSenderAck;
}
constexpr bool followed_in(uint16_t op) { return op == kOpMoveFieldCompleteReq || op == kOpLoadingCompleteReq; }

// [session2] observer_milestone (decide and log, read only) and
// observer_milestone_send (also emit; implies the first). Both 0 = Off.
enum class Mode : uint8_t { Off, DryRun, Send };
constexpr Mode choose_mode(bool diag, bool send) { return send ? Mode::Send : diag ? Mode::DryRun : Mode::Off; }
inline const char* mode_name(Mode m) {
    return m == Mode::Send ? "SEND" : m == Mode::DryRun ? "DRY RUN (decide and log only)" : "off";
}

// Why an entry gets no 2322 from us (None = emit).
enum class Why : uint8_t {
    None = 0,
    Off,
    NotUser2,          // the traveller is not user 2 (the host's own entries): not ours
    Owner,             // user 2 owns the field: the native path (0x287EF60) sends its 2322
    Unreadable,        // the native frame (R13 field, R14 traveller) could not be read
    Faulted,           // an earlier call faulted: emission off for the rest of the run
    InTakeFrame,       // reached from inside take_frame (never call the emitter from there)
    NoTrip,            // no 2174 of this epoch reached user 2's queue
    LogoutAsked,       // user 2's 2001 was asked for
    AlreadyDecided,    // this trip already had its decision (a repeated entry / callback)
    TripPoisoned,      // this trip's 2174, 2144 or 3066 was LOST in the outgoing queue
    NativeAlready,     // a native 2322 already reached user 2's queue in this trip
    No2144,            // no 2144 of this trip reached user 2's queue
    OtherThread,       // the trip's last 2144 went out on another thread (not this handler)
    No3066,            // no 3066 after that 2144 on this thread (not this MoveFieldCompleteCore)
    NotRealm,          // TLS block +0x1EC is 0: not a server-realm thread
    NoSendContext,     // TLS block +0x250 (the send context 0x2999D80 uses) is null
    ScopeOpen,         // a native scope sender is open here (+0x2AC, +0x2A0): the 2322 could be bundled
    ActorUnknown,      // user 2's character key is not known (no MainPlayerDataStartAck this epoch)
    ActorMismatch,     // the traveller's key is not user 2's character (stale or reused address)
    NotMainCharacter,  // [user+0xD0] is not the traveller
    SessionMismatch,   // the traveller's user's session (+0x1F8) is not the live session 2 of the epoch
    FieldMismatch,     // the traveller's field mover's field (TS+0x500) is not this field
    OwnerUnknown,      // the trip's 2174 did not have the known layout (no owner)
    OwnerMismatch,     // the field's owner is not the 2174's, or not the host
    NotHostField,      // the field is not the host's current field
    Count
};

inline const char* why_name(Why w) {
    switch (w) {
        case Why::None: return "eligible";
        case Why::Off: return "off";
        case Why::NotUser2: return "not user 2";
        case Why::Owner: return "user 2 owns this field: the native path sends its 2322";
        case Why::Unreadable: return "the native frame is unreadable";
        case Why::Faulted: return "an earlier call faulted: emission off for this run";
        case Why::InTakeFrame: return "reached from inside take_frame";
        case Why::NoTrip: return "no 2174 of this epoch reached user 2's queue";
        case Why::LogoutAsked: return "user 2's logout was asked for";
        case Why::AlreadyDecided: return "this trip was already decided (duplicate entry avoided)";
        case Why::TripPoisoned: return "a 2174/2144/3066 of this trip was LOST in the outgoing queue";
        case Why::NativeAlready: return "a native 2322 already reached user 2 in this trip";
        case Why::No2144: return "no 2144 of this trip reached user 2's queue";
        case Why::OtherThread: return "the trip's 2144 went out on another thread";
        case Why::No3066: return "no 3066 after that 2144 on this thread (not this MoveFieldCompleteCore)";
        case Why::NotRealm: return "not a server-realm thread (TLS +0x1EC = 0)";
        case Why::NoSendContext: return "no send context (TLS +0x250 = 0)";
        case Why::ScopeOpen: return "a native scope sender is open on this thread (+0x2AC/+0x2A0)";
        case Why::ActorUnknown: return "user 2's character key is unknown";
        case Why::ActorMismatch: return "the traveller is not user 2's character";
        case Why::NotMainCharacter: return "the traveller is not its user's main character";
        case Why::SessionMismatch: return "the traveller's session is not the live session 2";
        case Why::FieldMismatch: return "the traveller's field mover points to another field";
        case Why::OwnerUnknown: return "the trip's 2174 had no readable owner";
        case Why::OwnerMismatch: return "the field's owner is not the 2174's or not the host";
        case Why::NotHostField: return "the field is not the host's current field";
        default: return "?";
    }
}

// What the entry hook read in the native frame, the thread's TLS and the mod's
// state (observer_milestone.cpp fills it with SEH reads; the tests by hand).
struct EntryFacts {
    uint32_t tid = 0;
    uint64_t t_us = 0;
    uint32_t epoch = 0;          // server_epoch()
    bool logout_asked = false;
    bool in_take_frame = false;
    bool reads_ok = false;
    // MoveFieldCompleteCore's R13 (field) and R14 (traveller) and what hangs from them
    uintptr_t field = 0, actor = 0, user = 0, user_session = 0, main_character = 0, fm_field = 0;
    uint32_t field_owner = 0, field_key = 0, user_key = 0, actor_key = 0;
    // the mod's view
    uintptr_t live_session = 0;  // the live session 2 of `epoch` (0 = none)
    uint32_t user2_user_key = 0, user2_actor_key = 0, host_user_key = 0;
    uintptr_t host_field = 0;    // the host's current field (0 = unknown)
    // the thread: TLS block 0 (+0x1EC realm byte, +0x250 send context: +0x2AC scope mode, +0x2A0
    // open scope senders, +0x2A8 current one; 0x2961CBA / 0x2962226)
    bool realm = false;
    uintptr_t send_ctx = 0;
    uint8_t scope_flag = 0;
    uint32_t scope_count = 0;
    int32_t scope_current = -1;
};

struct Decision {
    Why why = Why::Off;
    bool emit = false;     // DryRun: logged as "would emit"; Send: the caller calls 0x2999D80 once
    bool tracked = false;  // it belongs to a trip (and finalized it)
    uint32_t gen = 0;
};

// What the call did (Send only).
enum class CallResult : uint8_t { Returned, WrongRecipient, BadFinder, FaultCtor, FaultEmit };
inline const char* call_name(CallResult r) {
    switch (r) {
        case CallResult::Returned: return "returned";
        case CallResult::WrongRecipient: return "NOT CALLED: the ToTarget session is not the live session 2";
        case CallResult::BadFinder: return "NOT CALLED: the ToTarget object is not as built natively";
        case CallResult::FaultCtor: return "FAULT in the ToTarget constructor 0x2A757A0";
        case CallResult::FaultEmit: return "FAULT in the emitter 0x2999D80";
        default: return "?";
    }
}

// The log notes (p8p::Event: kind = NoteKind, v[] per kind; formatted off the game's threads).
enum NoteKind : uint16_t {
    kNTripStart = 0,  // v0 gen, v1 epoch, v2 owner (2174, original), v3 pushed, v4 superseded gen
    kNTripEnd,        // v0 gen, v1 epoch, v2 outcome (Outcome), v3 why, v4 t_2174, v5 t_2144, v6 t_ours,
                      // v7 t_2445, v8 t_2762, v9 native 2322, v10 ours count, v11 entries
    kN2144,           // v0 gen, v1 pushed, v2 seq, v3 count in trip
    kN3066,           // v0 gen, v1 pushed, v2 seq of the 2144 it follows (0 = none on this thread)
    kNNative2322,     // v0 gen, v1 pushed, v2 actor key, v3 live, v4 after ours (1)
    kNOurs2322,       // v0 gen, v1 pushed, v2 actor key, v3 live session (1) / another (0)
    kNBundle,         // v0 gen, v1 pushed, v2 count
    kNIn,             // v0 gen, v1 opcode, v2 count in trip, v3 us since our 2322 (0 = none)
    kNOut2762,        // v0 gen, v1 pushed, v2 us since our 2322 (0 = none), v3 us since 2445
    kNDecision,       // v0 gen, v1 why, v2 emit, v3 mode, v4 epoch, v5 actor key | user key << 32,
                      // v6 field, v7 owner | field key << 32, v8 live session, v9 user session,
                      // v10 scope flag | count << 8 | current << 40, v11 realm | send ctx != 0 << 1 | dry << 2
    kNCalled,         // v0 gen, v1 CallResult, v2 seen in take_frame, v3 pushed, v4 count, v5 finder session,
                      // v6 duration us, v7 wrong session seen
    kNEpoch,          // v0 new epoch, v1 gen of the trip cancelled (0 none), v2 decided
    kNLogout,         // v0 gen, v1 decided before
    kNFollowUp,       // v0 gen, v1 seconds, v2 have 2445, v3 have 2762
    kNSkip,           // v0 why, v1 user key, v2 actor key, v3 field owner (owner entries and the unreadable)
    kNoteCount
};

enum class Outcome : uint8_t { NoEntry, Refused, DryWouldEmit, Emitted, CalledNotSeen, QueueLost, CallFailed, Cancelled };
inline const char* outcome_name(Outcome o) {
    switch (o) {
        case Outcome::NoEntry: return "no observer entry of user 2 in this trip";
        case Outcome::Refused: return "refused";
        case Outcome::DryWouldEmit: return "dry run: WOULD have emitted";
        case Outcome::Emitted: return "EMITTED: 2322 in user 2's queue";
        case Outcome::CalledNotSeen: return "called, but NO 2322 reached take_frame";
        case Outcome::QueueLost: return "called, 2322 LOST in the outgoing queue";
        case Outcome::CallFailed: return "call failed";
        case Outcome::Cancelled: return "cancelled before its entry";
        default: return "?";
    }
}

struct NoteSink {
    virtual void note(const p8p::Event& e) = 0;

protected:
    ~NoteSink() = default;
};

struct Trip {
    uint32_t gen = 0, epoch = 0;
    uint32_t owner = 0;  // the owner the server wrote in this trip's 2174 (0 = unknown layout)
    uint64_t t_2174 = 0;
    bool poisoned = false;
    // 2144 (the last one of the trip) and the 3066 that followed it on the same thread
    uint32_t n_2144 = 0, tid_2144 = 0;
    uint64_t seq_2144 = 0, t_2144 = 0;
    uint64_t seq_3066_after = 0;  // the seq of the 2144 a pushed 3066 followed on tid_2144
    // the decision (one per trip)
    bool decided = false, emit = false;
    Why why = Why::None;
    uint32_t entries = 0;  // observer entries of user 2 seen in the trip
    // the call (Send)
    bool called = false;
    CallResult call = CallResult::Returned;
    bool ours_seen = false, ours_pushed = false;
    uint32_t ours_count = 0;
    uint64_t t_ours = 0;
    uint32_t native_2322 = 0;
    // the load's exit
    uint32_t n_2549 = 0, n_2445 = 0, n_2762 = 0;
    uint64_t t_2445 = 0, t_2762 = 0;
    uint8_t reported = 0;  // follow-ups written (bit i: kFollowUpS[i])
    bool cancelled = false;
};

inline constexpr uint32_t kFollowUpS[3] = {10, 60, 300};

struct Counters {
    uint64_t entries = 0, observer_entries = 0, owner_entries = 0, host_entries = 0, unreadable = 0;
    uint64_t trips = 0, decisions = 0, emits = 0, dry_emits = 0, calls = 0, enqueued = 0, queue_lost = 0,
             not_seen = 0, call_failed = 0, native_2322 = 0, native_after_ours = 0, bundles = 0, cancelled = 0,
             duplicates_avoided = 0;
    std::array<uint64_t, static_cast<size_t>(Why::Count)> refused{};
};

class Ledger {
public:
    explicit Ledger(Mode m = Mode::Off) : mode_(m) {}
    void set_mode(Mode m) { mode_ = m; }
    Mode mode() const { return mode_; }
    const Trip& trip() const { return trip_; }
    const Counters& counters() const { return c_; }
    uint32_t epoch() const { return epoch_; }
    bool faulted() const { return faulted_; }

    // take_frame, after g_outgoing.push: a followed packet the server wrote to
    // one of the mod's sessions. live = to the live session 2 of the epoch;
    // pushed = g_outgoing.push accepted it. value: 2174 -> the ORIGINAL owner
    // (0 = unknown layout); 2322 -> its actor key.
    void on_out(uint16_t op, bool live, bool pushed, uint32_t tid, uint64_t t_us, uint32_t value, NoteSink& s) {
        if (!followed_out(op)) return;
        // Our own 2322: the call in progress on this thread.
        if (op == kOpMilestone && emit_gen_ && tid == emit_tid_) {
            ++emit_count_;
            emit_seen_ = true;
            emit_pushed_ = emit_pushed_ || (pushed && live);
            if (!live) emit_wrong_session_ = true;
            note(s, kNOurs2322, t_us, tid, {emit_gen_, pushed ? 1u : 0u, value, live ? 1u : 0u});
            return;
        }
        if (!live) return;  // another (older) session: not this epoch's conversation
        if (op == kOpMoveFieldAck) {
            const uint32_t old = trip_.gen;
            end_trip(s, t_us, tid);
            trip_ = Trip{};
            trip_.gen = ++gen_;
            trip_.epoch = epoch_;
            trip_.owner = value;
            trip_.t_2174 = t_us;
            trip_.poisoned = !pushed;
            ++c_.trips;
            note(s, kNTripStart, t_us, tid, {trip_.gen, epoch_, value, pushed ? 1u : 0u, old});
            return;
        }
        if (!trip_.gen) return;
        switch (op) {
            case kOpMoveFieldCompleteAck:
                ++trip_.n_2144;
                trip_.tid_2144 = tid;
                trip_.seq_2144 = ++seq_;
                trip_.t_2144 = t_us;
                if (!pushed) trip_.poisoned = true;
                note(s, kN2144, t_us, tid, {trip_.gen, pushed ? 1u : 0u, trip_.seq_2144, trip_.n_2144});
                break;
            case kOpMoveFieldCompletePostAck: {
                ++seq_;
                const bool follows = trip_.n_2144 && tid == trip_.tid_2144;
                if (follows && pushed) trip_.seq_3066_after = trip_.seq_2144;
                if (follows && !pushed) trip_.poisoned = true;
                note(s, kN3066, t_us, tid, {trip_.gen, pushed ? 1u : 0u, follows ? trip_.seq_2144 : 0});
                break;
            }
            case kOpMilestone:
                ++trip_.native_2322;
                ++c_.native_2322;
                if (trip_.ours_seen) ++c_.native_after_ours;
                note(s, kNNative2322, t_us, tid, {trip_.gen, pushed ? 1u : 0u, value, 1, trip_.ours_seen ? 1u : 0u});
                break;
            case kOpLoadingCompleteAck:
                ++trip_.n_2762;
                trip_.t_2762 = t_us;
                note(s, kNOut2762, t_us, tid,
                     {trip_.gen, pushed ? 1u : 0u, trip_.t_ours ? t_us - trip_.t_ours : 0,
                      trip_.t_2445 ? t_us - trip_.t_2445 : 0});
                break;
            case kOpScopeSenderAck:
                ++c_.bundles;
                if (c_.bundles <= 16) note(s, kNBundle, t_us, tid, {trip_.gen, pushed ? 1u : 0u, c_.bundles});
                break;
            default: break;
        }
    }

    // inject (network thread): a Req of user 2 handed to the server.
    void on_in(uint16_t op, uint64_t t_us, NoteSink& s) {
        if (!followed_in(op) || !trip_.gen) return;
        uint32_t n = 0;
        if (op == kOpMoveFieldCompleteReq) n = ++trip_.n_2549;
        if (op == kOpLoadingCompleteReq) {
            n = ++trip_.n_2445;
            trip_.t_2445 = t_us;
        }
        note(s, kNIn, t_us, 0, {trip_.gen, op, n, trip_.t_ours ? t_us - trip_.t_ours : 0});
    }

    // C4: a new guest epoch (or the server instance was replaced): whatever
    // the current trip had pending is cancelled; nothing carries over.
    void on_epoch(uint32_t epoch, uint64_t t_us, NoteSink& s) {
        const uint32_t g = trip_.gen;
        const bool decided = trip_.decided;
        if (g && !trip_.decided) {
            trip_.cancelled = true;
            ++c_.cancelled;
        }
        end_trip(s, t_us, 0);
        trip_ = Trip{};
        epoch_ = epoch;
        clear_emit();
        note(s, kNEpoch, t_us, 0, {epoch, g, decided ? 1u : 0u});
    }

    // User 2's 2001 was asked for: a trip still undecided will never get a 2322.
    void on_logout(uint64_t t_us, NoteSink& s) {
        if (!trip_.gen) return;
        const bool before = trip_.decided;
        if (!trip_.decided) {
            trip_.decided = true;
            trip_.why = Why::LogoutAsked;
            trip_.cancelled = true;
            ++c_.cancelled;
        }
        note(s, kNLogout, t_us, 0, {trip_.gen, before ? 1u : 0u});
    }

    // The entry hook (0x2BC84E2, the server's logic thread), under the caller's lock.
    Decision decide(const EntryFacts& f, NoteSink& s) {
        ++c_.entries;
        Decision d;
        if (mode_ == Mode::Off) return d;
        if (!f.reads_ok) {
            ++c_.unreadable;
            d.why = Why::Unreadable;
            if (c_.unreadable <= 16) note(s, kNSkip, f.t_us, f.tid, {static_cast<uint64_t>(d.why), 0, 0, 0});
            return d;
        }
        if (!f.user_key || f.user_key != f.user2_user_key) {
            ++c_.host_entries;
            d.why = Why::NotUser2;
            return d;  // the host's own entries: counted only
        }
        if (f.user_key == f.field_owner) {
            ++c_.owner_entries;
            d.why = Why::Owner;
            note(s, kNSkip, f.t_us, f.tid, {static_cast<uint64_t>(d.why), f.user_key, f.actor_key, f.field_owner});
            return d;
        }
        ++c_.observer_entries;
        d.why = gate(f);
        if (d.why != Why::NoTrip) {
            ++trip_.entries;
            d.tracked = true;
            d.gen = trip_.gen;
            if (d.why == Why::AlreadyDecided || d.why == Why::NativeAlready) ++c_.duplicates_avoided;
            if (d.why != Why::AlreadyDecided) {
                trip_.decided = true;
                trip_.why = d.why;
                trip_.emit = d.why == Why::None;
            }
        }
        d.emit = d.why == Why::None;
        ++c_.decisions;
        ++c_.refused[static_cast<size_t>(d.why)];
        if (d.emit) {
            if (mode_ == Mode::Send) {
                ++c_.emits;
                emit_tid_ = f.tid;
                emit_gen_ = trip_.gen;
                emit_seen_ = emit_pushed_ = emit_wrong_session_ = false;
                emit_count_ = 0;
            } else {
                ++c_.dry_emits;
            }
        }
        if (d.why != Why::AlreadyDecided || c_.refused[static_cast<size_t>(Why::AlreadyDecided)] <= 16) {
            note(s, kNDecision, f.t_us, f.tid,
                 {d.gen, static_cast<uint64_t>(d.why), d.emit ? 1u : 0u, static_cast<uint64_t>(mode_), f.epoch,
                  f.actor_key | static_cast<uint64_t>(f.user_key) << 32, f.field,
                  f.field_owner | static_cast<uint64_t>(f.field_key) << 32, f.live_session, f.user_session,
                  static_cast<uint64_t>(f.scope_flag) | static_cast<uint64_t>(f.scope_count) << 8 |
                      static_cast<uint64_t>(static_cast<uint32_t>(f.scope_current)) << 40,
                  (f.realm ? 1u : 0u) | (f.send_ctx ? 2u : 0u) | (mode_ == Mode::DryRun ? 4u : 0u)});
        }
        return d;
    }

    // Send: after the call returned to the entry hook (same thread), under the caller's lock.
    void on_called(uint32_t gen, CallResult r, uintptr_t finder_session, uint64_t t_us, uint64_t dur_us,
                   uint32_t tid, NoteSink& s) {
        ++c_.calls;
        const bool seen = emit_seen_ && emit_gen_ == gen;
        const bool pushed = seen && emit_pushed_;
        if (trip_.gen == gen) {
            trip_.called = true;
            trip_.call = r;
            trip_.ours_seen = seen;
            trip_.ours_pushed = pushed;
            trip_.ours_count = emit_count_;
            trip_.t_ours = seen ? t_us : 0;
        }
        if (r == CallResult::FaultCtor || r == CallResult::FaultEmit) faulted_ = true;
        if (r != CallResult::Returned) {
            ++c_.call_failed;
        } else if (!seen) {
            ++c_.not_seen;
        } else if (!pushed) {
            ++c_.queue_lost;
        } else {
            ++c_.enqueued;
        }
        note(s, kNCalled, t_us, tid,
             {gen, static_cast<uint64_t>(r), seen ? 1u : 0u, pushed ? 1u : 0u, emit_count_, finder_session, dur_us,
              emit_wrong_session_ ? 1u : 0u});
        clear_emit();
    }

    // The drain thread, once a second: report-only follow-ups after our 2322.
    void follow_ups(uint64_t now_us, NoteSink& s) {
        if (!trip_.gen || !trip_.t_ours || !trip_.ours_pushed || trip_.cancelled) return;
        for (int i = 0; i < 3; ++i) {
            if (trip_.reported & (1u << i)) continue;
            if (now_us < trip_.t_ours + static_cast<uint64_t>(kFollowUpS[i]) * 1'000'000) break;
            trip_.reported |= static_cast<uint8_t>(1u << i);
            if (trip_.n_2762) continue;  // finished: nothing to report
            note(s, kNFollowUp, now_us, 0, {trip_.gen, kFollowUpS[i], trip_.n_2445 ? 1u : 0u, trip_.n_2762 ? 1u : 0u});
        }
    }

    // The emission in progress on `tid` (take_frame's correlation).
    bool emitting(uint32_t tid) const { return emit_gen_ && emit_tid_ == tid; }

    static Outcome outcome_of(const Trip& t) {
        if (t.cancelled && !t.emit) return Outcome::Cancelled;
        if (!t.decided) return Outcome::NoEntry;
        if (!t.emit) return Outcome::Refused;
        if (!t.called) return Outcome::DryWouldEmit;
        if (t.call != CallResult::Returned) return Outcome::CallFailed;
        if (!t.ours_seen) return Outcome::CalledNotSeen;
        if (!t.ours_pushed) return Outcome::QueueLost;
        return Outcome::Emitted;
    }

private:
    Why gate(const EntryFacts& f) const {
        if (faulted_) return trip_.gen && trip_.epoch == f.epoch ? Why::Faulted : Why::NoTrip;
        if (!trip_.gen || trip_.epoch != f.epoch || epoch_ != f.epoch) return Why::NoTrip;
        if (trip_.decided) return trip_.cancelled ? Why::LogoutAsked : Why::AlreadyDecided;
        if (f.in_take_frame) return Why::InTakeFrame;
        if (f.logout_asked) return Why::LogoutAsked;
        if (trip_.poisoned) return Why::TripPoisoned;
        if (trip_.native_2322) return Why::NativeAlready;
        if (!trip_.n_2144) return Why::No2144;
        if (trip_.tid_2144 != f.tid) return Why::OtherThread;
        if (trip_.seq_3066_after != trip_.seq_2144) return Why::No3066;
        if (!f.realm) return Why::NotRealm;
        if (!f.send_ctx) return Why::NoSendContext;
        if (f.scope_flag && f.scope_count) return Why::ScopeOpen;
        if (!f.user2_actor_key) return Why::ActorUnknown;
        if (f.actor_key != f.user2_actor_key) return Why::ActorMismatch;
        if (f.main_character != f.actor) return Why::NotMainCharacter;
        if (!f.live_session || f.user_session != f.live_session) return Why::SessionMismatch;
        if (f.fm_field != f.field) return Why::FieldMismatch;
        if (!trip_.owner) return Why::OwnerUnknown;
        if (f.field_owner != trip_.owner || !f.host_user_key || f.field_owner != f.host_user_key) {
            return Why::OwnerMismatch;
        }
        if (!f.host_field || f.host_field != f.field) return Why::NotHostField;
        return Why::None;
    }

    void end_trip(NoteSink& s, uint64_t t_us, uint32_t tid) {
        if (!trip_.gen) return;
        note(s, kNTripEnd, t_us, tid,
             {trip_.gen, trip_.epoch, static_cast<uint64_t>(outcome_of(trip_)), static_cast<uint64_t>(trip_.why),
              trip_.t_2174, trip_.t_2144, trip_.t_ours, trip_.t_2445, trip_.t_2762, trip_.native_2322,
              trip_.ours_count, trip_.entries});
    }

    void clear_emit() {
        emit_tid_ = emit_gen_ = 0;
        emit_seen_ = emit_pushed_ = emit_wrong_session_ = false;
        emit_count_ = 0;
    }

    static void note(NoteSink& s, uint16_t kind, uint64_t t_us, uint32_t tid, std::initializer_list<uint64_t> v) {
        p8p::Event e;
        e.kind = kind;
        e.qpc_us = t_us;
        e.tid = tid;
        size_t i = 0;
        for (const uint64_t x : v) {
            if (i < 12) e.v[i++] = x;
        }
        s.note(e);
    }

    Mode mode_;
    uint32_t epoch_ = 1, gen_ = 0;
    uint64_t seq_ = 0;
    Trip trip_;
    bool faulted_ = false;
    uint32_t emit_tid_ = 0, emit_gen_ = 0, emit_count_ = 0;
    bool emit_seen_ = false, emit_pushed_ = false, emit_wrong_session_ = false;
    Counters c_;
};

}  // namespace cdmp::session2::oms
