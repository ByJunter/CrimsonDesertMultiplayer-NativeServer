#pragma once
// P8 S16, HOST: user 2's PlayAllPreLoadSequencerAck 2322 after a fast travel that RELOADS the shared field
// (docs/research/pendientes/p8-impl-viaje-rapido.md §12; the root cause: p8-s15-viaje-tunel.md). The pure logic of
// [session2] observer_milestone_reload = 0 | probe | 1, with no game access, so the tests can drive it.
//
// What it models (build 1.0.0.2976, RVAs; capstone on the read-only PE):
//   A reload trip of user 2 = its ReloadStageStartReq 2283 ACCEPTED BY F3 (the vf48 at 0x2A5F7FC answered user 2's
//   own manager or the block: user2_funnels), then its GameLoadingStartReq 2437 entering the server, answered by
//   0x2BCA3D0 with LoadingFieldDataAck 2643 + MoveFieldCompletePostAck 3066 and NO MoveFieldAck 2174 (a 2174 =
//   a field change: observer_milestone's entry path, never ours; a 2679 = refused).
//   The guest's client then sends PreLoadSequencerReq 2873 (0 bytes, load state 0x0D) and waits in state 0x11 for a
//   2322 naming ITS character (M+0x231 1 -> 2 at 0xB87A67). On the server the 2873's handler 0x2A5F2B0 -> .xtls
//   0x104D7F20 takes the field OWNER's manager (vf48, return 0x104D801E) and 0x2895EF0 -> 0x287EF60 re-prepares the
//   HOST's content sequencers and sends the 2322 to the host's character (0x287F1D4): user 2 never gets one.
//   Mode 1 (variant B, §12.2): the 2873 of a reload trip is DROPPED on the network thread (the host's sequencers are
//   left alone) and the 2322 for user 2's character is emitted ONCE on the server's thread, at the exit of user 2's
//   next ExecuteTroc at least kMinDelayUs after the drop, with observer_milestone's verified emitter (ToTarget
//   0x2A757A0 + 0x2999D80). Probe: the 2873 goes on (as today) and the same decision is logged ("WOULD").
//
// One reload trip = one F3 acceptance: R<gen>. At most ONE 2322 per trip; a native 2322 for user 2's character
// after its 2437, a 2174, a 2679, a new epoch or a logout end it without ours. Transient thread states (not the
// server realm, no send context, an open scope sender, inside take_frame, unreadable) are retried at the next Req,
// counted at most once per kAttemptGapUs (the guest sends hundreds of Reqs while loading: kTimeoutUs governs);
// anything else is final.
// Review 1 (S16): a trip only drops ONE window of 2873s: once decided, once its 2762 went out (phase Loaded) or
// kMaxAgeUs after its 2437, a 2873 of user 2 passes natively (a respawn or another path without a fresh F3 2283 is
// never swallowed). Review 2: the emission re-checks the phase (a 2174/2679 after the drop = "the trip changed").
// Review 3: no re-injection of the dropped 2873 on a final refusal (pendientes/p8-impl-viaje-rapido.md §12.8).
//
// P8 S27 (docs/research/s25/reaparicion.md): [session2] observer_milestone_reload_any = 0 | probe | 1. A revive
// (ReviveLoadingReq 2362 -> 2993 -> 2437 -> 2643 + 3066 -> 2988 -> 3053 + 2475 -> 2873) is the same reload in the
// same field (the 2362 arms user 2's own TS +0x569; 0x2BCA3D0 answers the 2437) but has no 2283, so no F3 trip was
// open and its 2873 passed natively: its 2322 went to the host's character and the guest stayed in load state 0x11
// for ever. With the key on, user 2's 2437 opens a trip whenever none is open (origin At2437), whatever Req started
// the load; it is classified exactly as an F3 trip (2643/3066 with no 2174 = reload; 2174 = field change, never
// ours; 2762 = over). probe: such a trip only ever says WOULD DROP / WOULD EMIT (its 2873 goes to the server).
// Whatever the key, the last 2437 of user 2 is followed and a 2873 handed to the server while that 2437's reload
// is unfinished (2643/3066, no 2174, no 2762) in the host's field is noted as a warning (log only).
// Review (d09254d): a trip only absorbs a later 2437 while it is younger than kMaxAgeUs (an old trip stuck in
// Asked / an undecided Reload is closed and a new one opens: else a later revive would be judged with the old 2437
// and pass natively).

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

#include "mod/session2/fast_travel_logic.h"
#include "mod/session2/observer_milestone_logic.h"
#include "mod/session2/p8_probe_logic.h"

namespace cdmp::session2::omr {

using NoteSink_t = oms::NoteSink;  // the notes share observer_milestone's ring

inline constexpr uint16_t kOpPreLoadSequencerReq = 2873;
inline constexpr uint16_t kOpReloadStageStartReq = 2283;
inline constexpr uint16_t kOpGameLoadingStartReq = 2437;
inline constexpr uint16_t kOpMoveFieldAck = 2174;
inline constexpr uint16_t kOpLoadingFieldDataAck = 2643;
inline constexpr uint16_t kOpMoveFieldCompletePostAck = 3066;
inline constexpr uint16_t kOpMilestone = 2322;
inline constexpr uint16_t kOpLoadingCompleteAck = 2762;
inline constexpr uint16_t kOpLogoutCharacterAck = 2679;
inline constexpr uint16_t kOpReviveLoadingReq = 2362;  // P8 S27: the revive's load request (C>S)

inline constexpr uint64_t kMinDelayUs = 300'000;     // native: the 2322 leaves 460-501 ms after the 2873
inline constexpr uint64_t kTimeoutUs = 15'000'000;   // no fitting Req of user 2 within 15 s: given up (logged)
inline constexpr uint64_t kAttemptGapUs = 50'000;    // transient refusals count at most once per 50 ms
inline constexpr uint32_t kMaxAttempts = 400;        // > kTimeoutUs / kAttemptGapUs: the timeout governs
inline constexpr uint64_t kMaxAgeUs = 45'000'000;    // a 2873 later than this after its trip's 2437 passes natively
inline constexpr uint64_t kNativeInReloadLines = 16; // P8 S27: warnings for a 2873 handed during an unfinished reload

// The take_frame opcodes the reload ledger follows.
constexpr bool followed_out(uint16_t op) {
    return op == kOpMoveFieldAck || op == kOpLoadingFieldDataAck || op == kOpMoveFieldCompletePostAck ||
           op == kOpMilestone || op == kOpLoadingCompleteAck || op == kOpLogoutCharacterAck;
}

enum class Mode : uint8_t { Off, Probe, On };
struct ParsedMode {
    Mode mode = Mode::Off;
    bool known = true;
};
inline ParsedMode parse_mode(std::string_view v) {
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
    if (v.empty() || v == "0") return {Mode::Off, true};
    if (v == "probe") return {Mode::Probe, true};
    if (v == "1") return {Mode::On, true};
    return {Mode::Off, false};
}
inline const char* mode_name(Mode m) {
    return m == Mode::On ? "1 (drop the 2873, emit the 2322)" : m == Mode::Probe ? "probe (decide and log only)" : "0";
}

// Where a reload trip of user 2 stands.
enum class Phase : uint8_t {
    None,         // no trip
    Prepared,     // its 2283 was accepted by F3 (user 2's manager / block at 0x2A5F7FC)
    Asked,        // its 2437 entered the server
    Reload,       // a 2643 or 3066 reached user 2's queue after it, no 2174: a reload in the same field
    FieldChange,  // a 2174 after it: a field change (observer_milestone's path), never ours
    Refused,      // a 2679 after it
    Cancelled,    // a new epoch / logout
    Loaded,       // its LoadingCompleteAck 2762 went out: the trip is over
    Count
};
inline const char* phase_name(Phase p) {
    switch (p) {
        case Phase::None: return "none";
        case Phase::Prepared: return "2283 accepted by F3";
        case Phase::Asked: return "2437 entered the server";
        case Phase::Reload: return "RELOAD (2643/3066, no 2174)";
        case Phase::FieldChange: return "field change (2174): observer_milestone's";
        case Phase::Refused: return "refused (2679)";
        case Phase::Cancelled: return "cancelled";
        case Phase::Loaded: return "loaded (2762): over";
        default: return "?";
    }
}

// P8 S27: where a reload trip came from.
enum class Origin : uint8_t {
    F3_2283,  // its ReloadStageStartReq 2283 was accepted by F3 (S16)
    At2437,   // observer_milestone_reload_any: its 2437 entered with no trip open (a revive 2362, or any other path)
    Count
};
inline const char* origin_name(Origin o) {
    switch (o) {
        case Origin::F3_2283: return "F3 2283";
        case Origin::At2437: return "at its 2437";
        default: return "?";
    }
}
// The last Req of user 2 that starts a load (user2_funnels follows 2283 and 2362), for the lines only.
inline const char* load_req_name(uint16_t op) {
    switch (op) {
        case 0: return "no 2283 / 2362 of user 2 seen before it";
        case kOpReloadStageStartReq: return "ReloadStageStartReq 2283 (fast travel)";
        case kOpReviveLoadingReq: return "ReviveLoadingReq 2362 (revive)";
        default: return "another Req";
    }
}

// The network thread's verdict on user 2's 2873.
enum class Gate : uint8_t {
    NotRelevant,  // not a 2873, or the key is 0
    NoTrip,       // no reload trip of user 2 (e.g. a 2873 of an entry trip): passes, as natively
    NotReload,    // its trip is not (or not yet) a reload: passes
    OwnField,     // user 2 is in a field of its own: the native path is right there
    NotShared,    // user 2's field is unknown or another user's: passes
    Over,         // its trip is over: decided, loaded (2762) or older than kMaxAgeUs: passes (native)
    EmitterOff,   // mode 1, but observer_milestone's emitter is not verified / not SEND: passes (no drop without it)
    WouldDrop,    // probe: passes, and the decision is taken (dry) at the next Req
    Drop,         // mode 1: dropped; the 2322 is emitted at the next Req
    Count
};
inline const char* gate_name(Gate g) {
    switch (g) {
        case Gate::NotRelevant: return "-";
        case Gate::NoTrip: return "no reload trip of user 2: passes (native)";
        case Gate::NotReload: return "its trip is not a reload (no 2643/3066 yet, or a 2174/2679): passes";
        case Gate::OwnField: return "user 2 is in a field of its own: passes (the native 2322 is its own)";
        case Gate::NotShared: return "user 2's field is not the host's (or unknown): passes";
        case Gate::Over: return "its reload trip is over (decided, loaded or > 45 s after its 2437): passes (native)";
        case Gate::EmitterOff: return "mode 1 but observer_milestone's emitter is not verified/SEND: passes, NOT dropped";
        case Gate::WouldDrop: return "probe: WOULD DROP (it goes on to the server: the host's sequencers are re-prepared)";
        case Gate::Drop: return "DROPPED (the host's sequencers are left alone); our 2322 at user 2's next Req";
        default: return "?";
    }
}
constexpr bool drops(Gate g) { return g == Gate::Drop; }

struct OfferFacts {
    Mode mode = Mode::Off;
    ftl::Where where = ftl::Where::Unknown;  // user 2's field (fast_travel's reading)
    bool emitter_ready = false;              // observer_milestone verified, SEND, not faulted
    uint64_t t_us = 0;                       // now (p8_now_us), for the age bound
};

// Why the server-side decision gives no 2322 (None = emit).
enum class Why : uint8_t {
    None = 0,
    Faulted,           // the emitter faulted earlier in this run
    LogoutAsked,       // user 2's 2001 was asked for
    EpochChanged,      // a new guest epoch
    NativeAlready,     // a native 2322 for user 2's character reached its queue after its 2437
    Timeout,           // no fitting Req of user 2 within kTimeoutUs of the 2873
    TripChanged,       // after the drop the trip stopped being a reload (2174, 2679, 2762)
    // transient (retried at the next Req, kMaxAttempts at most)
    Unreadable,        // the actor / user / field could not be read
    InTakeFrame,       // inside take_frame
    NotRealm,          // TLS +0x1EC = 0
    NoSendContext,     // TLS +0x250 = 0
    ScopeOpen,         // a native scope sender is open (+0x2AC/+0x2A0)
    // final
    ActorUnknown,      // user 2's character key is unknown
    ActorMismatch,     // ExecuteTroc's subject is not user 2's character
    NotMainCharacter,  // [user+0xD0] is not it
    SessionMismatch,   // its user's session is not the live session 2
    OwnFieldNow,       // its field's owner is user 2: the native path
    OwnerNotHost,      // its field's owner is not the host
    NotHostField,      // its field is not the host's current field
    Count
};
inline const char* why_name(Why w) {
    switch (w) {
        case Why::None: return "eligible";
        case Why::Faulted: return "the emitter faulted earlier: emission off for this run";
        case Why::LogoutAsked: return "user 2's logout was asked for";
        case Why::EpochChanged: return "a new guest epoch";
        case Why::NativeAlready: return "a native 2322 for user 2's character already reached its queue";
        case Why::Timeout: return "no fitting Req of user 2 within 15 s of its 2873";
        case Why::TripChanged: return "the trip changed after the drop (2174 / 2679 / 2762): not a reload any more";
        case Why::Unreadable: return "user 2's character/user/field unreadable";
        case Why::InTakeFrame: return "inside take_frame";
        case Why::NotRealm: return "not a server-realm thread (TLS +0x1EC = 0)";
        case Why::NoSendContext: return "no send context (TLS +0x250 = 0)";
        case Why::ScopeOpen: return "a native scope sender is open on this thread";
        case Why::ActorUnknown: return "user 2's character key is unknown";
        case Why::ActorMismatch: return "the Req's actor is not user 2's character";
        case Why::NotMainCharacter: return "the actor is not its user's main character";
        case Why::SessionMismatch: return "its user's session is not the live session 2";
        case Why::OwnFieldNow: return "user 2 owns its field now: the native path";
        case Why::OwnerNotHost: return "its field's owner is not the host";
        case Why::NotHostField: return "its field is not the host's current field";
        default: return "?";
    }
}
constexpr bool transient(Why w) {
    return w == Why::Unreadable || w == Why::InTakeFrame || w == Why::NotRealm || w == Why::NoSendContext ||
           w == Why::ScopeOpen;
}

// The gate on the server's thread at the exit of a Req of user 2 (f: observer_milestone's facts, read with
// field = the actor's field-mover field).
inline Why exit_gate(const oms::EntryFacts& f) {
    if (f.logout_asked) return Why::LogoutAsked;
    if (!f.reads_ok) return Why::Unreadable;
    if (f.in_take_frame) return Why::InTakeFrame;
    if (!f.realm) return Why::NotRealm;
    if (!f.send_ctx) return Why::NoSendContext;
    if (f.scope_flag && f.scope_count) return Why::ScopeOpen;
    if (!f.user2_actor_key) return Why::ActorUnknown;
    if (f.actor_key != f.user2_actor_key) return Why::ActorMismatch;
    if (f.main_character != f.actor) return Why::NotMainCharacter;
    if (!f.live_session || f.user_session != f.live_session) return Why::SessionMismatch;
    if (f.user2_user_key && f.field_owner == f.user2_user_key) return Why::OwnFieldNow;
    if (!f.host_user_key || f.field_owner != f.host_user_key) return Why::OwnerNotHost;
    if (!f.host_field || f.host_field != f.fm_field) return Why::NotHostField;
    return Why::None;
}

enum class Outcome : uint8_t {
    NotReload,      // the trip never became a reload (no 2643/3066)
    FieldChange,    // a 2174 (observer_milestone's)
    Refused2679,    // a 2679
    No2873,         // a reload, but no 2873 of user 2 came
    Passed,         // its 2873 went to the server (not dropped: off / not eligible)
    Undecided,      // armed, no decision yet (the trip was replaced)
    Refused,        // decided: no 2322 (why)
    DryWouldEmit,   // probe: would have emitted
    Emitted,        // our 2322 is in user 2's queue
    CalledNotSeen,  // the emitter returned, no 2322 reached take_frame
    QueueLost,      // our 2322 was lost in the outgoing queue
    CallFailed,     // not called / faulted
    Cancelled,      // epoch / logout before the decision
    Count
};
inline const char* outcome_name(Outcome o) {
    switch (o) {
        case Outcome::NotReload: return "not a reload trip (no 2643/3066)";
        case Outcome::FieldChange: return "field change (2174): observer_milestone's";
        case Outcome::Refused2679: return "refused by the server (2679)";
        case Outcome::No2873: return "reload, but user 2 sent no 2873";
        case Outcome::Passed: return "its 2873 went to the server (not ours)";
        case Outcome::Undecided: return "armed, never decided";
        case Outcome::Refused: return "no 2322 from us";
        case Outcome::DryWouldEmit: return "probe: WOULD have emitted";
        case Outcome::Emitted: return "EMITTED: our 2322 in user 2's queue";
        case Outcome::CalledNotSeen: return "called, but NO 2322 reached take_frame";
        case Outcome::QueueLost: return "called, 2322 LOST in the outgoing queue";
        case Outcome::CallFailed: return "call failed";
        case Outcome::Cancelled: return "cancelled";
        default: return "?";
    }
}

// The log notes (p8p::Event: kind = NoteKind, v[] per kind). Kinds start at kNoteBase so that they share
// observer_milestone's ring.
inline constexpr uint16_t kNoteBase = 64;
enum NoteKind : uint16_t {
    kNTrip = kNoteBase,  // v0 gen, v1 epoch, v2 previous gen
    kNPhase,             // v0 gen, v1 phase, v2 opcode, v3 us since its 2437 (0 none), v4 pushed
    kNOffer,             // v0 gen, v1 gate, v2 handed (1) / not (0), v3 2873 count, v4 where, v5 phase, v6 mode
    kNDecision,          // v0 gen, v1 why, v2 emit, v3 dry, v4 attempts, v5 us since the 2873, v6 actor | user << 32,
                         // v7 owner | host << 32, v8 live session, v9 user session, v10 tid, v11 realm | ctx << 1
    kNTransient,         // v0 gen, v1 why, v2 attempts
    kNCalled,            // v0 gen, v1 CallResult, v2 seen, v3 pushed, v4 count, v5 finder session, v6 dur us,
                         // v7 wrong session
    kNNative,            // v0 gen, v1 actor key, v2 pushed, v3 user 2's character (1)
    kNOurs,              // v0 gen, v1 pushed, v2 actor key, v3 live (1)
    kNLoaded,            // v0 gen, v1 us since our 2322 (0 none), v2 pushed
    kNTripEnd,           // v0 gen, v1 outcome, v2 why, v3 2873 seen, v4 dropped, v5 us 2437->2873,
                         // v6 us 2873->ours, v7 us ours->2762, v8 native 2322 for user 2, v9 phase
    kNCancel,            // v0 gen, v1 epoch (0 = logout), v2 decided before
    // P8 S27 (observer_milestone_reload_any)
    kNTrip2437,          // v0 gen, v1 epoch, v2 previous gen, v3 load op, v4 us since that Req (0 none), v5 user 2's TS
                         // before the 2437 (tpl::pack, 0 unread), v6 shadow, v7 mode, v8 any mode
    kNNested,            // v0 gen, v1 phase, v2 load op: a 2437 inside a live trip (kept)
    kNNativeInReload,    // v0 gen (0 none), v1 gate, v2 2873 count, v3 us since its 2437, v4 where, v5 mode, v6 any mode
    kNoteEnd
};

struct Trip {
    uint32_t gen = 0, epoch = 0;
    Phase phase = Phase::None;
    uint64_t t_f3 = 0, t_2437 = 0, t_reply = 0;
    uint32_t n2643 = 0, n3066 = 0;
    uint32_t n2873 = 0, dropped = 0, passed = 0;
    uint64_t t_2873 = 0;
    bool armed = false, dry = false;
    bool decided = false, emit = false;
    Why why = Why::None;
    uint32_t attempts = 0;
    uint64_t t_attempt = 0;  // the last counted transient refusal
    bool called = false;
    oms::CallResult call = oms::CallResult::Returned;
    bool ours_seen = false, ours_pushed = false;
    uint32_t ours_count = 0;
    uint64_t t_ours = 0, t_2762 = 0;
    uint32_t native_2322 = 0;  // for user 2's character, after its 2437
    bool cancelled = false;
    // P8 S27: who opened it; shadow = opened by observer_milestone_reload_any=probe (never drops); load_op = the last
    // Req of user 2 that started a load before its 2437 (2283 / 2362 / 0).
    Origin origin = Origin::F3_2283;
    bool shadow = false;
    uint16_t load_op = 0;
};

// P8 S27: user 2's last 2437 and how the server answered it, followed whatever the trips (the warning).
struct Last2437 {
    uint64_t t = 0;
    bool reload = false;        // a 2643 / 3066 to user 2 after it
    bool field_change = false;  // a 2174 after it
    bool loaded = false;        // its 2762 went out
    bool unfinished_reload() const { return t && reload && !field_change && !loaded; }
};

struct Counters {
    uint64_t trips = 0, reloads = 0, field_changes = 0, refused_2679 = 0;
    uint64_t trips_at_2437 = 0, reloads_at_2437 = 0, nested_2437 = 0;  // P8 S27 (observer_milestone_reload_any)
    uint64_t stale_closed = 0;  // P8 S27 review: live trips older than kMaxAgeUs closed by a new 2437
    uint64_t native_in_reload = 0;  // P8 S27: 2873s handed to the server during an unfinished reload (warnings)
    uint64_t seen_2873 = 0, dropped = 0, would_drop = 0, passed = 0;
    uint64_t decisions = 0, emits = 0, dry_emits = 0, calls = 0, enqueued = 0, lost = 0, not_seen = 0, failed = 0;
    uint64_t native = 0, transient = 0, cancelled = 0;
    std::array<uint64_t, static_cast<size_t>(Gate::Count)> gates{};
    std::array<uint64_t, static_cast<size_t>(Why::Count)> refused{};
};

struct ExitDecision {
    bool act = false;   // a final decision was taken now (emit, or a final refusal)
    bool emit = false;  // Send: the caller calls the emitter once (dry: only logged)
    bool dry = false;
    Why why = Why::None;
    uint32_t gen = 0;
};

class Ledger {
public:
    explicit Ledger(Mode m = Mode::Off) : mode_(m) {}
    void set_mode(Mode m) { mode_ = m; }
    Mode mode() const { return mode_; }
    // P8 S27: observer_milestone_reload_any (Off: trips only from F3, as in S16).
    void set_any(Mode m) { any_ = m; }
    Mode any() const { return any_; }
    const Last2437& last_2437() const { return l2437_; }
    const Trip& trip() const { return trip_; }
    const Counters& counters() const { return c_; }
    uint32_t epoch() const { return epoch_; }
    // The server thread's cheap test: an undecided armed trip.
    bool waiting() const { return trip_.armed && !trip_.decided; }

    // Server thread (ExecuteTroc of user 2's 2283 returned): F3 gave it user 2's manager (or the block). A new trip.
    void on_f3_accepted(uint64_t t_us, NoteSink_t& s) {
        const uint32_t prev = trip_.gen;
        end_trip(s, t_us);
        trip_ = Trip{};
        trip_.gen = ++gen_;
        trip_.epoch = epoch_;
        trip_.phase = Phase::Prepared;
        trip_.t_f3 = t_us;
        ++c_.trips;
        note(s, kNTrip, t_us, 0, {trip_.gen, epoch_, prev});
    }

    // Server thread, BEFORE the 2437 runs (its 2643/3066 go out inside it).
    void on_2437(uint64_t t_us, NoteSink_t& s) {
        if (trip_.phase != Phase::Prepared) return;
        trip_.phase = Phase::Asked;
        trip_.t_2437 = t_us;
        note(s, kNPhase, t_us, 0, {trip_.gen, static_cast<uint64_t>(trip_.phase), kOpGameLoadingStartReq, 0, 1});
    }

    // P8 S27, server thread, BEFORE user 2's 2437 runs (what the server thread calls; on_2437 is its F3 part). The
    // last 2437 is always followed. An F3 trip waiting for its 2437 takes it as in S16; with
    // observer_milestone_reload_any on and no trip open (none, or the last one over: loaded, field change, refused,
    // cancelled, decided), a new trip opens here (origin At2437). A 2437 inside a live trip (its 2437 not answered
    // yet, or a reload not decided yet) is only noted. load_op / load_age_us / ts_packed are for the line only.
    void on_2437_any(uint64_t t_us, uint16_t load_op, uint64_t load_age_us, uint64_t ts_packed, NoteSink_t& s) {
        l2437_ = Last2437{};
        l2437_.t = t_us;
        if (any_ == Mode::Off || trip_.phase == Phase::Prepared) {
            on_2437(t_us, s);
            return;
        }
        const bool open = trip_.gen && (trip_.phase == Phase::Asked ||
                                        (trip_.phase == Phase::Reload && !trip_.decided && !trip_.t_2762));
        // Review: only a YOUNG live trip absorbs this 2437 (an old one stuck in Asked / undecided is closed).
        const bool young = trip_.t_2437 && t_us >= trip_.t_2437 && t_us - trip_.t_2437 < kMaxAgeUs;
        if (open && young) {
            ++c_.nested_2437;
            note(s, kNNested, t_us, 0, {trip_.gen, static_cast<uint64_t>(trip_.phase), load_op});
            return;
        }
        if (open) ++c_.stale_closed;
        const uint32_t prev = trip_.gen;
        end_trip(s, t_us);
        trip_ = Trip{};
        trip_.gen = ++gen_;
        trip_.epoch = epoch_;
        trip_.origin = Origin::At2437;
        trip_.shadow = any_ == Mode::Probe;
        trip_.load_op = load_op;
        trip_.phase = Phase::Asked;
        trip_.t_2437 = t_us;
        ++c_.trips;
        ++c_.trips_at_2437;
        note(s, kNTrip2437, t_us, 0,
             {trip_.gen, epoch_, prev, load_op, load_age_us, ts_packed, trip_.shadow ? 1u : 0u,
              static_cast<uint64_t>(mode_), static_cast<uint64_t>(any_)});
    }

    // take_frame. live = to the live session 2; pushed = g_outgoing.push accepted it; value: the 2322's actor key;
    // user2_char = the 2322 names user 2's character.
    void on_out(uint16_t op, bool live, bool pushed, uint32_t tid, uint64_t t_us, uint32_t value, bool user2_char,
                NoteSink_t& s) {
        if (!followed_out(op)) return;
        if (op == kOpMilestone && emitting_ && tid == emit_tid_) {
            ++emit_count_;
            emit_seen_ = true;
            emit_pushed_ = emit_pushed_ || (pushed && live);
            if (!live) emit_wrong_session_ = true;
            note(s, kNOurs, t_us, tid, {trip_.gen, pushed ? 1u : 0u, value, live ? 1u : 0u});
            return;
        }
        if (live) follow_last_2437(op);
        if (!live || !trip_.gen) return;
        const bool active = trip_.phase == Phase::Prepared || trip_.phase == Phase::Asked || trip_.phase == Phase::Reload;
        const uint64_t since = trip_.t_2437 && t_us >= trip_.t_2437 ? t_us - trip_.t_2437 : 0;
        switch (op) {
            case kOpMoveFieldAck:
                if (!active) return;
                trip_.phase = Phase::FieldChange;
                ++c_.field_changes;
                note(s, kNPhase, t_us, tid, {trip_.gen, static_cast<uint64_t>(trip_.phase), op, since, pushed ? 1u : 0u});
                return;
            case kOpLogoutCharacterAck:
                if (!active) return;
                trip_.phase = Phase::Refused;
                ++c_.refused_2679;
                note(s, kNPhase, t_us, tid, {trip_.gen, static_cast<uint64_t>(trip_.phase), op, since, pushed ? 1u : 0u});
                return;
            case kOpLoadingFieldDataAck:
            case kOpMoveFieldCompletePostAck:
                if (trip_.phase != Phase::Asked && trip_.phase != Phase::Reload) return;
                (op == kOpLoadingFieldDataAck ? trip_.n2643 : trip_.n3066)++;
                if (trip_.phase == Phase::Asked) {
                    trip_.phase = Phase::Reload;
                    trip_.t_reply = t_us;
                    ++c_.reloads;
                    if (trip_.origin == Origin::At2437) ++c_.reloads_at_2437;
                    note(s, kNPhase, t_us, tid,
                         {trip_.gen, static_cast<uint64_t>(trip_.phase), op, since, pushed ? 1u : 0u});
                }
                return;
            case kOpMilestone:
                if (!user2_char || !trip_.t_2437) return;
                ++trip_.native_2322;
                ++c_.native;
                note(s, kNNative, t_us, tid, {trip_.gen, value, pushed ? 1u : 0u, 1});
                return;
            case kOpLoadingCompleteAck:
                if (!trip_.t_2437 || trip_.t_2762) return;
                trip_.t_2762 = t_us;
                if (active) trip_.phase = Phase::Loaded;  // over: a later 2873 passes natively
                note(s, kNLoaded, t_us, tid, {trip_.gen, trip_.t_ours ? t_us - trip_.t_ours : 0, pushed ? 1u : 0u});
                return;
            default: return;
        }
    }

    // Network thread: the verdict on a 2873 of user 2, no state change (offer may see the same Req again).
    Gate judge(const OfferFacts& f) const {
        if (f.mode == Mode::Off) return Gate::NotRelevant;
        if (!trip_.gen || trip_.phase == Phase::None || trip_.phase == Phase::Cancelled) return Gate::NoTrip;
        if (trip_.phase == Phase::Loaded) return Gate::Over;
        if (trip_.phase != Phase::Reload) return Gate::NotReload;
        if (trip_.decided || trip_.t_2762) return Gate::Over;
        if (trip_.t_2437 && f.t_us > trip_.t_2437 + kMaxAgeUs) return Gate::Over;
        if (f.where == ftl::Where::OwnField) return Gate::OwnField;
        if (f.where != ftl::Where::HostField) return Gate::NotShared;
        if (f.mode == Mode::Probe) return Gate::WouldDrop;
        if (trip_.shadow) return Gate::WouldDrop;  // P8 S27: opened by observer_milestone_reload_any=probe
        if (!f.emitter_ready) return Gate::EmitterOff;
        return Gate::Drop;
    }

    // Network thread, once per 2873 on its final disposition: handed = it went to the server.
    void record_offer(Gate g, bool handed, ftl::Where where, uint64_t t_us, NoteSink_t& s) {
        if (g == Gate::NotRelevant) return;
        ++c_.seen_2873;
        ++c_.gates[static_cast<size_t>(g)];
        const bool mine = g != Gate::NoTrip;
        if (mine) ++trip_.n2873;
        if (g == Gate::Drop && !handed) {
            ++c_.dropped;
            ++trip_.dropped;
        } else {
            if (g == Gate::WouldDrop) ++c_.would_drop;
            ++c_.passed;
            if (mine) ++trip_.passed;
        }
        // The first fitting 2873 of the trip arms the decision (mode 1: dropped; probe: handed, decided dry).
        const bool arm = (g == Gate::Drop && !handed) || (g == Gate::WouldDrop && handed);
        if (arm && !trip_.armed && !trip_.decided) {
            trip_.armed = true;
            trip_.dry = g == Gate::WouldDrop;
            trip_.t_2873 = t_us;
            trip_.attempts = 0;
        }
        if (c_.seen_2873 <= 32) {
            note(s, kNOffer, t_us, 0,
                 {mine ? trip_.gen : 0, static_cast<uint64_t>(g), handed ? 1u : 0u, trip_.n2873,
                  static_cast<uint64_t>(where), static_cast<uint64_t>(trip_.phase), static_cast<uint64_t>(mode_)});
        }
        // P8 S27: handed to the server while user 2's last 2437 is an unfinished reload in a field it does not own:
        // the native 2322 goes to the field owner's character and the guest's load waits in state 0x11 (log only).
        if (native_in_reload(g, handed, where)) {
            ++c_.native_in_reload;
            if (c_.native_in_reload <= kNativeInReloadLines) {
                note(s, kNNativeInReload, t_us, 0,
                     {mine ? trip_.gen : 0, static_cast<uint64_t>(g), c_.seen_2873,
                      t_us >= l2437_.t ? t_us - l2437_.t : 0, static_cast<uint64_t>(where),
                      static_cast<uint64_t>(mode_), static_cast<uint64_t>(any_)});
            }
        }
    }

    // P8 S27: the warning's rule (no state change): only in the host's field (where the server's 2873 path sends
    // the 2322 to the host's character).
    bool native_in_reload(Gate g, bool handed, ftl::Where where) const {
        return handed && g != Gate::NotRelevant && where == ftl::Where::HostField && l2437_.unfinished_reload();
    }

    // Server thread, at the exit of any Req of user 2 while waiting(): under the caller's lock.
    ExitDecision at_exit(const oms::EntryFacts& f, bool faulted, NoteSink_t& s) {
        ExitDecision d;
        if (!waiting()) return d;
        if (f.t_us < trip_.t_2873 + kMinDelayUs) return d;
        d.gen = trip_.gen;
        d.dry = trip_.dry;
        Why why = Why::None;
        if (trip_.phase != Phase::Reload) {
            why = Why::TripChanged;  // a 2174 / 2679 / 2762 after the drop: never a 2322 now
        } else if (faulted) {
            why = Why::Faulted;
        } else if (f.epoch != trip_.epoch || f.epoch != epoch_) {
            why = Why::EpochChanged;
        } else if (trip_.native_2322) {
            why = Why::NativeAlready;
        } else if (f.t_us > trip_.t_2873 + kTimeoutUs) {
            why = Why::Timeout;
        } else {
            why = exit_gate(f);
        }
        if (transient(why)) {
            // At most one counted attempt per kAttemptGapUs: a burst of Reqs cannot use the budget up.
            if (trip_.t_attempt && f.t_us < trip_.t_attempt + kAttemptGapUs) return ExitDecision{};
            trip_.t_attempt = f.t_us;
            ++trip_.attempts;
            ++c_.transient;
            if (trip_.attempts < kMaxAttempts) {
                if (trip_.attempts <= 4) note(s, kNTransient, f.t_us, f.tid, {trip_.gen, static_cast<uint64_t>(why), trip_.attempts});
                return ExitDecision{};
            }
        }
        d.act = true;
        d.why = why;
        d.emit = why == Why::None;
        trip_.decided = true;
        trip_.why = why;
        trip_.emit = d.emit;
        ++c_.decisions;
        ++c_.refused[static_cast<size_t>(why)];
        if (d.emit) {
            if (d.dry) {
                ++c_.dry_emits;
            } else {
                ++c_.emits;
                emitting_ = true;
                emit_tid_ = f.tid;
                emit_seen_ = emit_pushed_ = emit_wrong_session_ = false;
                emit_count_ = 0;
            }
        }
        note(s, kNDecision, f.t_us, f.tid,
             {trip_.gen, static_cast<uint64_t>(why), d.emit ? 1u : 0u, d.dry ? 1u : 0u, trip_.attempts,
              f.t_us - trip_.t_2873, f.actor_key | static_cast<uint64_t>(f.user_key) << 32,
              f.field_owner | static_cast<uint64_t>(f.host_user_key) << 32, f.live_session, f.user_session, f.tid,
              (f.realm ? 1u : 0u) | (f.send_ctx ? 2u : 0u)});
        return d;
    }

    // Send: after the emitter returned (same thread), under the caller's lock.
    void on_called(uint32_t gen, oms::CallResult r, uintptr_t finder_session, uint64_t t_us, uint64_t dur_us,
                   uint32_t tid, NoteSink_t& s) {
        ++c_.calls;
        const bool seen = emitting_ && emit_seen_;
        const bool pushed = seen && emit_pushed_;
        if (trip_.gen == gen) {
            trip_.called = true;
            trip_.call = r;
            trip_.ours_seen = seen;
            trip_.ours_pushed = pushed;
            trip_.ours_count = emit_count_;
            trip_.t_ours = seen ? t_us : 0;
        }
        if (r != oms::CallResult::Returned) {
            ++c_.failed;
        } else if (!seen) {
            ++c_.not_seen;
        } else if (!pushed) {
            ++c_.lost;
        } else {
            ++c_.enqueued;
        }
        note(s, kNCalled, t_us, tid,
             {gen, static_cast<uint64_t>(r), seen ? 1u : 0u, pushed ? 1u : 0u, emit_count_, finder_session, dur_us,
              emit_wrong_session_ ? 1u : 0u});
        clear_emit();
    }

    bool emitting(uint32_t tid) const { return emitting_ && emit_tid_ == tid; }

    // A new guest epoch / the server replaced: the trip ends, nothing carries over.
    void on_epoch(uint32_t epoch, uint64_t t_us, NoteSink_t& s) {
        cancel(t_us, epoch, s);
        end_trip(s, t_us);
        trip_ = Trip{};
        epoch_ = epoch;
        l2437_ = Last2437{};
        clear_emit();
    }
    // User 2's 2001 was asked for.
    void on_logout(uint64_t t_us, NoteSink_t& s) {
        cancel(t_us, 0, s);
        l2437_ = Last2437{};
    }

    static Outcome outcome_of(const Trip& t) {
        if (t.decided) {
            if (!t.emit) return t.cancelled ? Outcome::Cancelled : Outcome::Refused;
            if (t.dry) return Outcome::DryWouldEmit;
            if (!t.called) return Outcome::Undecided;
            if (t.call != oms::CallResult::Returned) return Outcome::CallFailed;
            if (!t.ours_seen) return Outcome::CalledNotSeen;
            if (!t.ours_pushed) return Outcome::QueueLost;
            return Outcome::Emitted;
        }
        if (t.armed) return t.cancelled ? Outcome::Cancelled : Outcome::Undecided;
        if (t.phase == Phase::FieldChange) return Outcome::FieldChange;
        if (t.phase == Phase::Refused) return Outcome::Refused2679;
        if (t.phase == Phase::Cancelled) return Outcome::Cancelled;
        if (!t.n2643 && !t.n3066) return Outcome::NotReload;
        return t.n2873 ? Outcome::Passed : Outcome::No2873;
    }

    // Ends the current trip with its line (also used by the drain for a summary at shutdown: not needed).
    void end_trip(NoteSink_t& s, uint64_t t_us) {
        if (!trip_.gen) return;
        note(s, kNTripEnd, t_us, 0,
             {trip_.gen, static_cast<uint64_t>(outcome_of(trip_)), static_cast<uint64_t>(trip_.why), trip_.n2873,
              trip_.dropped, trip_.t_2437 && trip_.t_2873 ? trip_.t_2873 - trip_.t_2437 : 0,
              trip_.t_ours && trip_.t_2873 ? trip_.t_ours - trip_.t_2873 : 0,
              trip_.t_ours && trip_.t_2762 && trip_.t_2762 >= trip_.t_ours ? trip_.t_2762 - trip_.t_ours : 0,
              trip_.native_2322, static_cast<uint64_t>(trip_.phase),
              static_cast<uint64_t>(trip_.origin) | (trip_.shadow ? 1ull << 8 : 0) |
                  static_cast<uint64_t>(trip_.load_op) << 16});
    }

private:
    void cancel(uint64_t t_us, uint32_t epoch, NoteSink_t& s) {
        if (!trip_.gen) return;
        const bool before = trip_.decided;
        const bool open = trip_.phase == Phase::Prepared || trip_.phase == Phase::Asked || trip_.phase == Phase::Reload;
        if (!trip_.decided && (trip_.armed || open)) {
            if (trip_.armed) {
                trip_.decided = true;
                trip_.why = epoch ? Why::EpochChanged : Why::LogoutAsked;
                ++c_.refused[static_cast<size_t>(trip_.why)];
            }
            trip_.cancelled = true;
            trip_.phase = Phase::Cancelled;
            ++c_.cancelled;
        }
        note(s, kNCancel, t_us, 0, {trip_.gen, epoch, before ? 1u : 0u});
    }

    void follow_last_2437(uint16_t op) {
        if (!l2437_.t) return;
        switch (op) {
            case kOpMoveFieldAck: l2437_.field_change = true; break;
            case kOpLoadingFieldDataAck:
            case kOpMoveFieldCompletePostAck:
                if (!l2437_.field_change) l2437_.reload = true;
                break;
            case kOpLoadingCompleteAck: l2437_.loaded = true; break;
            default: break;
        }
    }

    void clear_emit() {
        emitting_ = false;
        emit_tid_ = emit_count_ = 0;
        emit_seen_ = emit_pushed_ = emit_wrong_session_ = false;
    }

    static void note(NoteSink_t& s, uint16_t kind, uint64_t t_us, uint32_t tid, std::initializer_list<uint64_t> v) {
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
    Mode any_ = Mode::Off;  // P8 S27
    uint32_t epoch_ = 1, gen_ = 0;
    Trip trip_;
    Last2437 l2437_;  // P8 S27
    bool emitting_ = false;
    uint32_t emit_tid_ = 0, emit_count_ = 0;
    bool emit_seen_ = false, emit_pushed_ = false, emit_wrong_session_ = false;
    Counters c_;
};

}  // namespace cdmp::session2::omr
