#pragma once
// Option D, C4 second part (docs/research/pendientes/salida.md §4.1-§4.3):
// user 2 leaves and comes back in the same game run.
//
// One guest (structured so it can become one per peer): a mod-owned,
// monotonic EPOCH per conversation and a state machine
//
//   ACTIVE --goodbye / link lost--> RETIRING --barrier--> CLEAN --> READY --new connection--> ACTIVE (epoch+1)
//                                      \--budget exceeded--> QUARANTINED (safe stop: no more logins)
//
// The native session id *(session+0x88) stays 0 (decision D-39): the epoch
// is the mod's alone. The cleanup itself is the game's native logout (the
// 2001 server.cpp hands); this module only decides, from READ-ONLY readings,
// whether that logout has finished (the barrier, salida.md §4.3):
//   a. session: the retired session 2 has no user (+0x90 == 0);
//   b. sam: user 2's user and character keys, captured while they were
//      valid, no longer resolve in the ServerActorManager, and the user key is
//      no longer in its user list;
//   c. field: the host's field is the one of the baseline (same pointer, owner,
//      generation, open) with the users and user actors of the baseline taken
//      before user 2 came in, and no entry of the departed user;
//   d. queues: nothing of the old epoch is left in the mod (early queue,
//      outgoing, packet buffers handed to the server but not seen by it, the
//      2001 handed exactly once, the mirror).
//
// Pure logic plus read-only probes over game memory (probe_read below):
// unit-tested on fake memory in tests/optd_c4_tests.cpp.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cdmp::session2 {

enum class GuestState : uint8_t { Active, Retiring, Clean, Ready, Quarantined };
enum class LogoutTrigger : uint8_t { Goodbye, LinkLost };
const char* guest_state_name(GuestState s);
const char* logout_trigger_name(LogoutTrigger t);

// ------------------------------------------------------------------ read-only probes

// Offsets (build 1.0.0.2976) with their evidence (docs/research/pendientes/salida_evidencias/).
namespace c4 {
// ServerActorManager [exe+0x6DA67D8] (offsets_s2.h kSamSlot; the same object the
// free path 0x10530180 gets: C4b's probe saw +0x9C fall and +0xD0 rise by 0x50 the
// moment the 2001 went in, backups/logs/cdmp_20261009-1016_c4b.log lines 287/340).
// Key -> actor map, as cdmp_SAM__FindActor 0x2A827B0 reads it (0x2A82868..0x2A828FB):
inline constexpr uint32_t kSamMapBuckets = 0x98;   // u32 bucket count
inline constexpr uint32_t kSamMapSize = 0x9C;      // u32 entries (offsets_s2.h kSam_ActorCount)
inline constexpr uint32_t kSamMapBucketBase = 0xA8; // -> buckets of 0x100 B: u32 n, then n x {u32 index, u32 slot}
inline constexpr uint32_t kSamMapHolders = 0xB0;   // -> holder*[slot]; holder: u32 index +4, actor* +8
inline constexpr uint32_t kSamBucketBytes = 0x100;
inline constexpr uint32_t kSamBucketMaxEntries = (kSamBucketBytes - 8) / 8;  // 31
inline constexpr uint32_t kSamKeyIndexMask = 0xFFFFF;
// User list (vector of actor*), as the free path removes from it (0x105301A2..0x105301DD).
inline constexpr uint32_t kSamUserList = 0xB8;
inline constexpr uint32_t kSamUserCount = 0xC0;
// A sane bound. The vector holds more than users (0x10530180 takes out any
// actor it frees) and the key space reaches 15501 (review of f15596e, 5).
inline constexpr uint32_t kSamUserListMax = 0x4000;
// Any server actor: u32 key at +0x60 (zeroed by the free path at 0x1053024F).
inline constexpr uint32_t kActorKey = 0x60;
// ServerUserActor: main character +0xD0 (opcion-d-e3-riesgos.md, probe); the
// child slots as ServerUserActor::vf10 0x2A7D280 walks them (0x2A7D2B9..0x2A7D312):
// [[user+0x68]+0x18] = slots object, u8 count at +0x20, slot i = [[slots+0x18] + i*0x10].
inline constexpr uint32_t kUserMainCharacter = 0xD0;
inline constexpr uint32_t kUserComponents = 0x68;
inline constexpr uint32_t kComponentsSlots = 0x18;
inline constexpr uint32_t kSlotsArray = 0x18;
inline constexpr uint32_t kSlotsCount = 0x20;
inline constexpr uint32_t kSlotStride = 0x10;
}  // namespace c4

// How every probe of this file (and server.cpp's C4 readings) reads game
// memory (p8-tunel-rendimiento.md). By default an SEH-guarded direct read
// (game::fast_read): VirtualQuery, which game::safe_read calls first, walks
// the page table of the whole region to size it, and the game's heap regions
// are hundreds of MB (measured: 64 MB 52 us, 512 MB 0.47 ms, 2 GB 5.9 ms a
// call); the barrier made hundreds of such reads every 250 ms on the
// network thread. Same values, same failures (a stale pointer is caught).
// true = the VirtualQuery-checked reads as before ([session2] c4_safe_reads=1).
void set_probe_safe_reads(bool on);
bool probe_safe_reads();
bool probe_read(uintptr_t addr, void* out, size_t size);
template <class T>
std::optional<T> probe(uintptr_t addr) {
    T v{};
    if (!probe_read(addr, &v, sizeof v)) return std::nullopt;
    return v;
}
// probe() of a pointer, with game::read_ptr's plausibility check.
std::optional<uintptr_t> probe_ptr(uintptr_t addr);

// Does `key` resolve to a live actor with that exact key in the SAM's map?
// The lookup of 0x2A827B0 without its lock (the map can change meanwhile:
// callers ask twice before believing an absence). nullopt = unreadable.
std::optional<bool> sam_key_resolves(uintptr_t sam, uint32_t key);
// The same lookup, returning the actor (0 = not found). Never follow a
// pointer kept from an earlier lookup: ask again by key.
std::optional<uintptr_t> sam_find_actor(uintptr_t sam, uint32_t key);
// Is an actor with `key` in the SAM's user list? nullopt = unreadable.
std::optional<bool> sam_user_listed(uintptr_t sam, uint32_t key);

// sam_find_actor with the caller's reads (game::SafeReads or game::FastReads:
// rd<T>(a) and ptr(a), both checked). sam_find_actor is this with SafeReads.
template <class Mem>
std::optional<uintptr_t> sam_find_actor_with(const Mem& m, uintptr_t sam, uint32_t key) {
    if (!sam) return std::nullopt;
    if (!key) return uintptr_t{0};
    const uint32_t idx = key & c4::kSamKeyIndexMask;
    const auto nb = m.template rd<uint32_t>(sam + c4::kSamMapBuckets);
    const auto size = m.template rd<uint32_t>(sam + c4::kSamMapSize);
    if (!nb || !size) return std::nullopt;
    if (*nb == 0 || *size == 0) return uintptr_t{0};  // 0x2A82874 / 0x2A82885: an empty map finds nothing
    const auto buckets = m.ptr(sam + c4::kSamMapBucketBase);
    const auto holders = m.ptr(sam + c4::kSamMapHolders);
    if (!buckets || !holders) return std::nullopt;
    const uintptr_t bucket = *buckets + static_cast<uintptr_t>(idx % *nb) * c4::kSamBucketBytes;
    const auto n = m.template rd<uint32_t>(bucket);
    if (!n || *n > c4::kSamBucketMaxEntries) return std::nullopt;  // a torn read (the map has no lock here)
    for (uint32_t i = 0; i < *n; ++i) {
        const auto entry_idx = m.template rd<uint32_t>(bucket + 8 + 8ull * i);
        if (!entry_idx) return std::nullopt;
        if (*entry_idx != idx) continue;
        const auto slot = m.template rd<uint32_t>(bucket + 0xC + 8ull * i);
        if (!slot) return std::nullopt;
        const auto holder = m.ptr(*holders + 8ull * *slot);
        if (!holder) return std::nullopt;
        const auto holder_idx = m.template rd<uint32_t>(*holder + 4);
        if (!holder_idx) return std::nullopt;
        if (*holder_idx != idx) continue;  // 0x2A828C4: the game goes on with the next entry
        const auto actor = m.template rd<uintptr_t>(*holder + 8);
        if (!actor) return std::nullopt;
        // 0x2A82902 tests the holder's address (+8), not a null actor, and the
        // game compares only the 20-bit index; this exact-key test is stricter
        // (it can only fail where the game would find, never pass wrongly).
        if (!*actor) return uintptr_t{0};
        const auto actor_key = m.template rd<uint32_t>(*actor + c4::kActorKey);
        if (!actor_key) return std::nullopt;
        // The index can be reused by another actor; only the exact key counts.
        return *actor_key == key ? *actor : uintptr_t{0};
    }
    return uintptr_t{0};
}

// The host's field (opcion-d-campo-compartido.md; field_share.h fld::).
struct FieldSnap {
    bool ok = false;
    uintptr_t field = 0;
    uint32_t host_key = 0;  // the host's user key (session 1)
    uint32_t owner = 0, users = 0, user_actors = 0, generation = 0;
    uint8_t open = 0;
};
// Fills owner/users/user actors/generation/open of `field` (field and host_key
// set by the caller). False = unreadable.
bool read_field(uintptr_t field, FieldSnap& out);
// Is `user` (a pointer VALUE, compared, never followed) in the field's user list?
std::optional<bool> field_lists_user(uintptr_t field, uintptr_t user);
// The sum of the per-user actor counts of the field's list (AddActor and
// RemoveActor keep it equal to field+0x2B0). nullopt = unreadable.
std::optional<uint64_t> field_listed_actors(uintptr_t field);
// P8 S29 (reentrada.md §6, the structural check c): the field's user list read ONCE (one count, one copy): its
// entries, the sum of their actor counts, whether `departed` is among them, and how many are not `host_user` (pointer
// values, compared, never followed; a null entry counts as foreign). nullopt = unreadable.
struct FieldListScan {
    uint32_t entries = 0;
    uint64_t actors = 0;
    bool departed_listed = false;
    uint32_t foreign = 0;
    uintptr_t first_foreign = 0;
};
std::optional<FieldListScan> scan_field_list(uintptr_t field, uintptr_t departed, uintptr_t host_user);

// User 2 of one epoch, captured while it was valid (never followed after its logout).
struct DepartedSnap {
    uintptr_t user = 0;            // pointer value: only compared
    uint32_t user_key = 0;
    std::vector<uint32_t> keys;    // user, characters, children: each one resolved in the SAM when captured
    bool lookup_verified = false;  // positive control: the user key resolved through the map
    bool user_listed = false;      // positive control: the user key was in the user list
    bool any() const { return user != 0; }
};
inline constexpr size_t kMaxDepartedKeys = 160;
// ------------------------------------------------------------------ user 2's field move (salida-en-carga.md)
//
// The native logout cannot finish while the character's field move is open:
// GameLoadingStartReq sets FieldMover+0x58 and only MoveFieldComplete clears
// it; meanwhile FieldMover::GetField refuses the field access whose release
// makes the character leave the field (§1.3). A guest that vanishes between
// MoveFieldAck and its MoveFieldCompleteReq ("window A") left its logout stuck.

// Read-only offsets (build 1.0.0.2976, salida-en-carga.md §1).
namespace c4 {
inline constexpr uint32_t kActorState = 0x5E;           // u16: 0x20 logging out, 0x40 released (0xE0D9AF0)
inline constexpr uint32_t kActorComponents = 0x68;      // [pj+0x68]
inline constexpr uint32_t kComponentsTransformSync = 0x1A0;  // TS = [[pj+0x68]+0x1A0]
inline constexpr uint32_t kTsMoving = 0x3DC;            // u8: 1 from MoveFieldOnceTimer to MoveFieldComplete
inline constexpr uint32_t kFmMoving = 0x58;             // u8 at TS+0x4F0+0x58: the move is open (GetField refuses)
}  // namespace c4

// What the logout waits for, read through a user that is still attached
// (its vtable checked by the caller). Diagnostics only: nothing is written.
struct MoveDiag {
    bool user_ok = false, pj_ok = false, fm_ok = false, ts_ok = false;
    uint16_t user_state = 0, pj_state = 0, fm_busy = 0;
    uint8_t fm_moving = 0, ts_moving = 0;
    std::string describe() const;
};
MoveDiag read_move_diag(uintptr_t user);

// The opcodes the tracker follows (troc_messages_2976.csv).
namespace mv {
inline constexpr uint16_t kLoginCharacterReq = 3093;      // LoginCharacterToFieldServerReq
inline constexpr uint16_t kGameLoadingStartReq = 2437;
inline constexpr uint16_t kMoveFieldCompleteReq = 2549;
inline constexpr uint16_t kLoadingCompleteReq = 2445;
inline constexpr uint16_t kMoveFieldAck = 2174;
inline constexpr uint16_t kMoveFieldCompleteAck = 2144;
inline constexpr uint16_t kLoadingCompleteAck = 2762;
}  // namespace mv

// The phase of user 2's login/move, from what crosses the tunnel only: the
// Req handed to the server (C->S) and the Acks the server wrote for it
// (S->C). The last of them decides:
//   LoggingIn  3093 handed           MoveAsked  2437 handed (its 2174 is due)
//   WindowA    2174 out              Completing 2549 handed (its 2144 is due)
//   WindowB    2144 out              LoadingSent 2445 handed (its 2762 is due)
//   Stable     2762 out
// One per guest epoch (reset when a new one begins). Not thread safe: the
// caller locks.
enum class MovePhase : uint8_t { None, LoggingIn, MoveAsked, WindowA, Completing, WindowB, LoadingSent, Stable };
const char* move_phase_name(MovePhase p);
// P8 session 14 (observer_load_defer, pendientes/p8-impl-spawn-action.md): user 2 is completing a field load,
// from its 2549 (the server announces the field's actors while it processes it) until the server wrote its
// 2762. Review M1: not from its 3093/2437/2174 (a move that never reaches its 2549 must not hold anything).
constexpr bool move_phase_loading(MovePhase p) {
    return p == MovePhase::Completing || p == MovePhase::WindowB || p == MovePhase::LoadingSent;
}

class MovePhaseTracker {
public:
    // A Req of user 2 that went to the server. True if it is one the tracker follows.
    bool on_req(uint16_t opcode);
    // A packet the server wrote for user 2. True if it is one the tracker follows.
    bool on_ack(uint16_t opcode);
    MovePhase phase() const { return phase_; }
    // How many of `opcode` (one of mv::) were seen in this epoch.
    uint32_t count(uint16_t opcode) const;
    void reset() { *this = MovePhaseTracker{}; }
    // "WINDOW_A (3093 1, 2437 1, 2174 2, 2549 1, 2144 1, 2445 1, 2762 1)"
    std::string describe() const;

private:
    MovePhase phase_ = MovePhase::None;
    uint32_t n_[7] = {};
};

// Before user 2's 2001 (salida-en-carga.md §4): if it left in window A, close
// its move natively first (MoveFieldCompleteReq 2549 00, as its client would
// have sent), wait for the MoveFieldCompleteAck 2144, then the 2001. Every
// wait has a limit; past it, or on anything unexpected, the 2001 goes as
// before (barrier and quarantine unchanged). LoadingCompleteReq is never
// sent (window B needs nothing, §1.3; cut9 decides). Network thread.
class MoveCloseSequence {
public:
    struct Config {
        uint32_t move_ack_ms = 3000;      // MoveAsked: wait this long for the server's 2174
        uint32_t complete_ack_ms = 3000;  // 2549 injected: wait this long for its 2144
    };
    enum class Action : uint8_t { Wait, InjectMoveComplete, HandLogout };
    enum class Stage : uint8_t { Idle, WaitMoveAck, WaitCompleteAck, Done };

    MoveCloseSequence() : MoveCloseSequence(Config{}) {}
    explicit MoveCloseSequence(Config c) : cfg_(c) {}

    // One step while the 2001 is due. `note` (if given) gets one line for the
    // log whenever the stage changes, empty otherwise. After
    // InjectMoveComplete, the caller reports the injection with injected().
    Action step(uint32_t now_ms, const MovePhaseTracker& t, std::string* note = nullptr);
    // The 2549 was (ok) or could not be (!ok) handed to the server.
    void injected(bool ok, uint32_t now_ms, const MovePhaseTracker& t);
    Stage stage() const { return stage_; }
    // The 2549 went to the server in this sequence.
    bool closed_move() const { return injected_; }
    void reset() { *this = MoveCloseSequence(cfg_); }

private:
    Config cfg_;
    Stage stage_ = Stage::Idle;
    uint32_t since_ms_ = 0;
    uint32_t acks_before_ = 0;  // 2144 seen when the 2549 went
    bool injected_ = false;
};

// The actor keys a LIVE user holds: its own, its main character's and its
// children's (0 skipped, duplicates removed). Read only.
std::vector<uint32_t> user_actor_keys(uintptr_t user);
// Adds to `snap` what `user` holds now, keeping only keys that resolve in the
// SAM (positive control). Call only while the user is valid (before its logout).
void capture_departed(uintptr_t sam, uintptr_t user, DepartedSnap& snap);

// ------------------------------------------------------------------ the barrier

struct BarrierReadings {
    // a
    bool session_read = false;
    uintptr_t session_user = 0;  // *(retired session 2 + 0x90)
    // b
    bool sam_read = false;
    uint32_t keys_checked = 0, keys_resolving = 0, first_resolving = 0;
    bool user_listed = false;
    // c
    FieldSnap field;
    bool field_list_read = false;
    bool departed_in_field = false;
    std::optional<uint64_t> listed_actors;  // sum of the list's per-user counts
    // P8 S29 structural check c: the list's entries and how many are not the host's user (session 1's +0x90).
    // nullopt = the host's user or the list unreadable.
    std::optional<uint32_t> list_entries;
    std::optional<uint32_t> foreign_entries;
    uintptr_t first_foreign = 0;
    // d
    size_t early = 0, outgoing_bytes = 0, mirror_pending = 0;
    uint64_t pb_pushed = 0, pb_seen = 0, logout_handed = 0;
    bool expect_handed = true;  // the 2001 must have gone exactly once (0 times if none was needed)
    // Diagnostics only (never part of a check, salida-en-carga.md §2): the
    // still-attached user's logout state and its character's field move
    // (MoveDiag::describe), and the Nak 1013 session 2 got after its 2001.
    std::string move_diag;
    uint64_t nak_after_logout = 0;
    // Diagnostics only (p8-tunel-rendimiento.md): how long each reading took, in microseconds.
    uint64_t us_session = 0, us_keys = 0, us_list = 0, us_field = 0;
};

struct CheckResult {
    bool pass = false;
    std::string detail;
};
struct BarrierResult {
    CheckResult session, sam, field, queues;
    bool all() const { return session.pass && sam.pass && field.pass && queues.pass; }
    // "a session", "b sam", ... of the first failing check; "" if all pass.
    std::string first_failed() const;
    // One line: each check pass/FAIL with its values.
    std::string describe() const;
    // The pass/fail pattern (to log only when it changes).
    unsigned pattern() const;
};
BarrierResult evaluate_barrier(const FieldSnap& baseline, const DepartedSnap& snap, const BarrierReadings& r);

// ------------------------------------------------------------------ epochs of the packet buffers

// The tag server.cpp writes at PB+0x8010 (a field the server never reads):
// 0xD0D0 in the high half, the epoch in the low half.
uint32_t pb_tag(uint32_t epoch);
std::optional<uint32_t> pb_tag_epoch(uint32_t tag);
// Where a tagged packet buffer goes: the session of ITS epoch (never the
// current one if it is older). sessions[e] = session of epoch e (0 = none).
struct PbRoute {
    uintptr_t session = 0;  // 0 = no session known for it: the caller keeps the old behaviour
    uint32_t epoch = 0;
    bool stale = false;     // an epoch older than the current one
};
PbRoute route_pb(uint32_t tag, uint32_t current_epoch, const uintptr_t* sessions, size_t count);
// The epoch a session belongs to (0 = none of ours).
uint32_t epoch_of_session(uintptr_t session, const uintptr_t* sessions, size_t count);

// ------------------------------------------------------------------ the state machine

class GuestLifecycle {
public:
    struct Config {
        uint32_t budget_ms = 10000;    // from the 2001 handed to a clean barrier
        uint32_t pending_ms = 60000;   // a 2001 never handed (no user 2 appeared) -> quarantine
        uint32_t stable_ms = 500;      // the barrier must pass twice this far apart
        uint32_t max_epochs = 32;      // sessions built in one run (each one stays alive)
        uint32_t quiet_ms = 5000;      // no buffer taken for this long and no user ever: clean without 2001
    };
    enum class Step { Wait, Clean, Quarantined };

    GuestLifecycle() : GuestLifecycle(Config{}) {}
    explicit GuestLifecycle(Config c);

    GuestState state() const { return state_; }
    uint32_t epoch() const { return epoch_; }
    std::optional<uint32_t> bound_link() const { return bound_; }
    uint32_t min_link() const { return min_link_; }
    LogoutTrigger trigger() const { return trigger_; }
    const std::string& quarantine_reason() const { return reason_; }
    const Config& config() const { return cfg_; }

    // A guest packet that arrived on transport connection `link_epoch`: true if
    // it belongs to this epoch's conversation (ACTIVE; the first packet binds
    // the conversation to its connection; never one older than min_link()).
    bool admit(uint32_t link_epoch);
    // ACTIVE with a conversation (a packet was admitted).
    bool conversation() const { return state_ == GuestState::Active && bound_.has_value(); }

    // ACTIVE -> RETIRING. False (nothing changes) in any other state.
    bool begin_logout(LogoutTrigger t, uint32_t now_ms);
    // The 2001 went to the server.
    void note_handed(uint32_t now_ms);
    bool handed() const { return handed_; }

    // RETIRING: one barrier evaluation. nothing_reached = no packet of this
    // epoch ever reached the server and no user 2 ever existed (nothing native
    // to clean: no 2001 needed).
    // quiet_without_user: the epoch's buffers reached the server but no user
    // 2 ever appeared (e.g. a login refused with Nak 1013), every buffer was
    // taken and none for a while: nothing to log out (review of f15596e, 2).
    Step on_barrier(uint32_t now_ms, bool nothing_reached, const BarrierResult& r, bool quiet_without_user = false);

    // CLEAN -> READY, after the mod reset its per-epoch state. link_epoch_now:
    // the transport connection at that moment (only a NEWER one may start the
    // next epoch). Quarantines instead when max_epochs sessions were built.
    bool make_ready(uint32_t link_epoch_now);
    // READY -> ACTIVE with epoch+1, for a connection newer than make_ready's.
    bool start_epoch(uint32_t link_epoch);
    bool may_start(uint32_t link_epoch) const { return state_ == GuestState::Ready && link_epoch > ready_link_; }
    // A guest lost without a goodbye comes back on a newer connection, which breaks the link. A broken link drops all it
    // receives, the new guest's identity first, and connected() needs that identity, so the start_epoch that would reset
    // the link never runs: BROKEN for good (2026-11-05, build 3549, the first crash and rejoin between two PCs). In READY no
    // conversation is left for the broken state to protect, so the link starts over. During the logout it stays as it is.
    bool link_reset_due(bool link_broken) const { return state_ == GuestState::Ready && link_broken; }
    // P8 re-entry (rejoin_logic.h, s25/reentrada.md §3): READY -> ACTIVE with epoch+1 on the SAME transport connection,
    // asked for by the guest's REJOIN control frame (the reliable channel's order separates the conversations: what
    // the guest sent before its REJOIN was dropped while READY). Never on a connection older than make_ready's.
    bool may_start_inband(uint32_t link_epoch) const {
        return state_ == GuestState::Ready && link_epoch >= ready_link_;
    }
    bool start_epoch_inband(uint32_t link_epoch);

    // Any state -> QUARANTINED (kept: the first reason).
    void quarantine(const std::string& why);

private:
    Config cfg_;
    GuestState state_ = GuestState::Active;
    uint32_t epoch_ = 1;
    std::optional<uint32_t> bound_;
    uint32_t min_link_ = 0;
    uint32_t ready_link_ = 0;
    LogoutTrigger trigger_ = LogoutTrigger::Goodbye;
    uint32_t begin_ms_ = 0, handed_ms_ = 0, pass_since_ms_ = 0;
    bool handed_ = false, passing_ = false;
    std::string reason_;
};

}  // namespace cdmp::session2
