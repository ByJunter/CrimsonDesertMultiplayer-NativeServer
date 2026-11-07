#include "mod/session2/guest_lifecycle.h"

#include <algorithm>
#include <atomic>
#include <format>

#include "mod/game/safe_memory.h"
#include "mod/session2/field_share.h"

namespace cdmp::session2 {

// ------------------------------------------------------------------ how the probes read (p8-tunel-rendimiento.md)

namespace {
std::atomic<bool> g_safe_reads{false};
constexpr uintptr_t kMinPtr = 0x10000;
constexpr uintptr_t kMaxPtr = 0x00007FFFFFFFFFFFull;
}  // namespace

void set_probe_safe_reads(bool on) { g_safe_reads.store(on, std::memory_order_relaxed); }
bool probe_safe_reads() { return g_safe_reads.load(std::memory_order_relaxed); }

bool probe_read(uintptr_t addr, void* out, size_t size) {
    return probe_safe_reads() ? game::safe_read(addr, out, size) : game::fast_read(addr, out, size);
}

std::optional<uintptr_t> probe_ptr(uintptr_t addr) {
    const auto p = probe<uintptr_t>(addr);
    if (!p || *p < kMinPtr || *p > kMaxPtr) return std::nullopt;
    return p;
}

namespace {
template <class T>
std::optional<T> read(uintptr_t addr) {
    return probe<T>(addr);
}
std::optional<uintptr_t> read_ptr(uintptr_t addr) { return probe_ptr(addr); }
}  // namespace

const char* guest_state_name(GuestState s) {
    switch (s) {
        case GuestState::Active: return "ACTIVE";
        case GuestState::Retiring: return "RETIRING";
        case GuestState::Clean: return "CLEAN";
        case GuestState::Ready: return "READY";
        case GuestState::Quarantined: return "QUARANTINED";
    }
    return "?";
}

const char* logout_trigger_name(LogoutTrigger t) { return t == LogoutTrigger::Goodbye ? "bye" : "link_lost"; }

// ------------------------------------------------------------------ read-only probes

std::optional<uintptr_t> sam_find_actor(uintptr_t sam, uint32_t key) {
    return probe_safe_reads() ? sam_find_actor_with(game::SafeReads{}, sam, key)
                              : sam_find_actor_with(game::FastReads{}, sam, key);
}

std::optional<bool> sam_key_resolves(uintptr_t sam, uint32_t key) {
    const auto actor = sam_find_actor(sam, key);
    if (!actor) return std::nullopt;
    return *actor != 0;
}

std::optional<bool> sam_user_listed(uintptr_t sam, uint32_t key) {
    if (!sam) return std::nullopt;
    const auto count = read<uint32_t>(sam + c4::kSamUserCount);
    if (!count || *count > c4::kSamUserListMax) return std::nullopt;
    if (*count == 0) return false;
    const auto data = read_ptr(sam + c4::kSamUserList);
    if (!data) return std::nullopt;
    std::vector<uintptr_t> users(*count);
    if (!probe_read(*data, users.data(), users.size() * sizeof(uintptr_t))) return std::nullopt;
    for (uintptr_t u : users) {
        if (!u) continue;
        const auto k = read<uint32_t>(u + c4::kActorKey);
        if (k && *k == key) return true;
    }
    return false;
}

bool read_field(uintptr_t field, FieldSnap& out) {
    const auto& layout = runtime_field_layout();
    const auto open = read<uint8_t>(field + fld::kOpen);
    const auto owner = read<uint32_t>(field + fld::kOwner);
    const auto users = read<uint32_t>(field + layout.user_count);
    const auto actors = read<uint32_t>(field + layout.user_actors);
    const auto gen = read<uint32_t>(field + layout.key);
    if (!field || !open || !owner || !users || !actors || !gen) return false;
    out.field = field;
    out.open = *open;
    out.owner = *owner;
    out.users = *users;
    out.user_actors = *actors;
    out.generation = *gen;
    return true;
}

std::optional<bool> field_lists_user(uintptr_t field, uintptr_t user) {
    const auto& layout = runtime_field_layout();
    const auto count = read<uint32_t>(field + layout.user_count);
    if (!count || *count > 64) return std::nullopt;
    if (*count == 0) return false;
    const auto data = read_ptr(field + layout.users);
    if (!data) return std::nullopt;
    std::vector<FieldUserEntry> entries(*count);
    if (!probe_read(*data, entries.data(), entries.size() * sizeof(FieldUserEntry))) return std::nullopt;
    for (const auto& e : entries) {
        if (e.user == user) return true;
    }
    return false;
}

std::optional<uint64_t> field_listed_actors(uintptr_t field) {
    const auto& layout = runtime_field_layout();
    const auto count = read<uint32_t>(field + layout.user_count);
    if (!count || *count > 64) return std::nullopt;
    if (*count == 0) return 0;
    const auto data = read_ptr(field + layout.users);
    if (!data) return std::nullopt;
    std::vector<FieldUserEntry> entries(*count);
    if (!probe_read(*data, entries.data(), entries.size() * sizeof(FieldUserEntry))) return std::nullopt;
    uint64_t sum = 0;
    for (const auto& e : entries) sum += e.actors;
    return sum;
}

std::optional<FieldListScan> scan_field_list(uintptr_t field, uintptr_t departed, uintptr_t host_user) {
    const auto& layout = runtime_field_layout();
    const auto count = read<uint32_t>(field + layout.user_count);
    if (!count || *count > 64) return std::nullopt;
    FieldListScan s;
    s.entries = *count;
    if (*count == 0) return s;
    const auto data = read_ptr(field + layout.users);
    if (!data) return std::nullopt;
    std::vector<FieldUserEntry> entries(*count);
    if (!probe_read(*data, entries.data(), entries.size() * sizeof(FieldUserEntry))) return std::nullopt;
    for (const auto& e : entries) {
        s.actors += e.actors;
        if (departed && e.user == departed) s.departed_listed = true;
        if (!e.user || e.user != host_user) {
            if (!s.foreign++) s.first_foreign = e.user;
        }
    }
    return s;
}

// ------------------------------------------------------------------ user 2's field move

std::string MoveDiag::describe() const {
    auto u16 = [](bool ok, uint16_t v) { return ok ? std::format("0x{:X}", v) : std::string("?"); };
    const auto& layout = runtime_field_layout();
    return std::format("user +0x5E {} | character +0x5E {} | FM+0x{:X} {} FM+0x{:X} {} | TS+0x3DC {}",
                       u16(user_ok, user_state), u16(pj_ok, pj_state), layout.fm_moving, u16(fm_ok, fm_moving),
                       layout.fm_busy, u16(fm_ok, fm_busy),
                       u16(ts_ok, ts_moving));
}

MoveDiag read_move_diag(uintptr_t user) {
    MoveDiag d;
    if (!user) return d;
    if (const auto s = read<uint16_t>(user + c4::kActorState)) {
        d.user_ok = true;
        d.user_state = *s;
    }
    const auto pj = read_ptr(user + c4::kUserMainCharacter);
    if (!pj || !*pj) return d;
    if (const auto s = read<uint16_t>(*pj + c4::kActorState)) {
        d.pj_ok = true;
        d.pj_state = *s;
    }
    const auto comps = read_ptr(*pj + c4::kActorComponents);
    const auto ts = comps && *comps ? read_ptr(*comps + c4::kComponentsTransformSync) : std::nullopt;
    if (!ts || !*ts) return d;
    const auto& layout = runtime_field_layout();
    if (layout.ts_moving_verified) {
        if (const auto m = read<uint8_t>(*ts + c4::kTsMoving)) {
            d.ts_ok = true;
            d.ts_moving = *m;
        }
    }
    const auto moving = read<uint8_t>(*ts + layout.ts_field_mover + layout.fm_moving);
    const auto busy = read<uint16_t>(*ts + layout.ts_field_mover + layout.fm_busy);
    if (moving && busy) {
        d.fm_ok = true;
        d.fm_moving = *moving;
        d.fm_busy = *busy;
    }
    return d;
}

const char* move_phase_name(MovePhase p) {
    switch (p) {
        case MovePhase::None: return "NONE";
        case MovePhase::LoggingIn: return "LOGGING_IN";
        case MovePhase::MoveAsked: return "MOVE_ASKED";
        case MovePhase::WindowA: return "WINDOW_A";
        case MovePhase::Completing: return "COMPLETING";
        case MovePhase::WindowB: return "WINDOW_B";
        case MovePhase::LoadingSent: return "LOADING_SENT";
        case MovePhase::Stable: return "STABLE";
    }
    return "?";
}

namespace {
constexpr uint16_t kTracked[7] = {mv::kLoginCharacterReq, mv::kGameLoadingStartReq, mv::kMoveFieldAck,
                                  mv::kMoveFieldCompleteReq, mv::kMoveFieldCompleteAck, mv::kLoadingCompleteReq,
                                  mv::kLoadingCompleteAck};
int tracked_index(uint16_t op) {
    for (int i = 0; i < 7; ++i) {
        if (kTracked[i] == op) return i;
    }
    return -1;
}
}  // namespace

bool MovePhaseTracker::on_req(uint16_t opcode) {
    MovePhase next;
    switch (opcode) {
        case mv::kLoginCharacterReq: next = MovePhase::LoggingIn; break;
        case mv::kGameLoadingStartReq: next = MovePhase::MoveAsked; break;
        case mv::kMoveFieldCompleteReq: next = MovePhase::Completing; break;
        case mv::kLoadingCompleteReq: next = MovePhase::LoadingSent; break;
        default: return false;
    }
    phase_ = next;
    ++n_[tracked_index(opcode)];
    return true;
}

bool MovePhaseTracker::on_ack(uint16_t opcode) {
    MovePhase next;
    switch (opcode) {
        case mv::kMoveFieldAck: next = MovePhase::WindowA; break;
        case mv::kMoveFieldCompleteAck: next = MovePhase::WindowB; break;
        case mv::kLoadingCompleteAck: next = MovePhase::Stable; break;
        default: return false;
    }
    phase_ = next;
    ++n_[tracked_index(opcode)];
    return true;
}

uint32_t MovePhaseTracker::count(uint16_t opcode) const {
    const int i = tracked_index(opcode);
    return i < 0 ? 0 : n_[i];
}

std::string MovePhaseTracker::describe() const {
    std::string s = std::format("{} (", move_phase_name(phase_));
    for (int i = 0; i < 7; ++i) s += std::format("{}{} {}", i ? ", " : "", kTracked[i], n_[i]);
    return s + ")";
}

MoveCloseSequence::Action MoveCloseSequence::step(uint32_t now_ms, const MovePhaseTracker& t, std::string* note) {
    if (note) note->clear();
    auto say = [note](std::string s) {
        if (note) *note = std::move(s);
    };
    switch (stage_) {
        case Stage::Idle:
            if (t.phase() == MovePhase::WindowA) {
                say(std::format("user 2 left in window A ({}): its move is closed natively first, "
                                "MoveFieldCompleteReq 2549 00",
                                t.describe()));
                return Action::InjectMoveComplete;
            }
            if (t.phase() == MovePhase::MoveAsked) {
                stage_ = Stage::WaitMoveAck;
                since_ms_ = now_ms;
                say(std::format("user 2 left after its GameLoadingStartReq ({}): waiting up to {} ms for the "
                                "server's MoveFieldAck 2174 before deciding",
                                t.describe(), cfg_.move_ack_ms));
                return Action::Wait;
            }
            stage_ = Stage::Done;
            say(std::format("user 2's move phase {}: no open move to close, the 2001 directly", t.describe()));
            return Action::HandLogout;
        case Stage::WaitMoveAck:
            if (t.phase() == MovePhase::WindowA) {
                say(std::format("MoveFieldAck 2174 out after {} ms ({}): window A, MoveFieldCompleteReq 2549 00 first",
                                now_ms - since_ms_, t.describe()));
                return Action::InjectMoveComplete;
            }
            if (t.phase() != MovePhase::MoveAsked) {
                stage_ = Stage::Done;
                say(std::format("move phase changed to {} while waiting for the MoveFieldAck: the 2001 as before",
                                t.describe()));
                return Action::HandLogout;
            }
            if (now_ms - since_ms_ >= cfg_.move_ack_ms) {
                stage_ = Stage::Done;
                say(std::format("no MoveFieldAck 2174 in {} ms ({}): the 2001 as before", cfg_.move_ack_ms,
                                t.describe()));
                return Action::HandLogout;
            }
            return Action::Wait;
        case Stage::WaitCompleteAck:
            if (t.count(mv::kMoveFieldCompleteAck) > acks_before_) {
                stage_ = Stage::Done;
                say(std::format("MoveFieldCompleteAck 2144 out {} ms after the injected 2549 ({}): the move is "
                                "closed; now the 2001",
                                now_ms - since_ms_, t.describe()));
                return Action::HandLogout;
            }
            if (now_ms - since_ms_ >= cfg_.complete_ack_ms) {
                stage_ = Stage::Done;
                say(std::format("no MoveFieldCompleteAck 2144 in {} ms after the injected 2549 ({}): the 2001 as "
                                "before (the barrier decides)",
                                cfg_.complete_ack_ms, t.describe()));
                return Action::HandLogout;
            }
            return Action::Wait;
        case Stage::Done:
            return Action::HandLogout;
    }
    return Action::HandLogout;
}

void MoveCloseSequence::injected(bool ok, uint32_t now_ms, const MovePhaseTracker& t) {
    if (stage_ == Stage::Done || stage_ == Stage::WaitCompleteAck) return;
    if (!ok) {
        stage_ = Stage::Done;
        return;
    }
    injected_ = true;
    stage_ = Stage::WaitCompleteAck;
    since_ms_ = now_ms;
    acks_before_ = t.count(mv::kMoveFieldCompleteAck);
}

std::vector<uint32_t> user_actor_keys(uintptr_t user) {
    std::vector<uint32_t> keys;
    auto add = [&keys](std::optional<uint32_t> k) {
        if (k && *k && std::find(keys.begin(), keys.end(), *k) == keys.end()) keys.push_back(*k);
    };
    add(read<uint32_t>(user + c4::kActorKey));
    if (const auto main = read_ptr(user + c4::kUserMainCharacter)) add(read<uint32_t>(*main + c4::kActorKey));
    const auto comps = read_ptr(user + c4::kUserComponents);
    const auto slots = comps ? read_ptr(*comps + c4::kComponentsSlots) : std::nullopt;
    if (!slots) return keys;
    const auto count = read<int8_t>(*slots + c4::kSlotsCount);  // signed in vf10 (0x2A7D2CB..D6)
    const auto arr = read_ptr(*slots + c4::kSlotsArray);
    if (!count || *count <= 0 || !arr) return keys;
    for (int i = 0; i < *count && keys.size() < kMaxDepartedKeys; ++i) {
        if (const auto child = read_ptr(*arr + static_cast<uintptr_t>(i) * c4::kSlotStride)) {
            add(read<uint32_t>(*child + c4::kActorKey));
        }
    }
    return keys;
}

void capture_departed(uintptr_t sam, uintptr_t user, DepartedSnap& snap) {
    const auto user_key = read<uint32_t>(user + c4::kActorKey);
    if (!user || !user_key || !*user_key) return;
    snap.user = user;
    snap.user_key = *user_key;
    for (uint32_t k : user_actor_keys(user)) {
        if (snap.keys.size() >= kMaxDepartedKeys) break;
        if (std::find(snap.keys.begin(), snap.keys.end(), k) != snap.keys.end()) continue;
        if (sam_key_resolves(sam, k) != std::optional<bool>(true)) continue;  // positive control
        snap.keys.push_back(k);
        if (k == snap.user_key) snap.lookup_verified = true;
    }
    if (!snap.user_listed && sam_user_listed(sam, snap.user_key) == std::optional<bool>(true)) snap.user_listed = true;
}

// ------------------------------------------------------------------ the barrier

std::string BarrierResult::first_failed() const {
    if (!session.pass) return "a session";
    if (!sam.pass) return "b sam";
    if (!field.pass) return "c field";
    if (!queues.pass) return "d queues";
    return {};
}

std::string BarrierResult::describe() const {
    auto one = [](const char* name, const CheckResult& c) {
        return std::format("{} {} ({})", name, c.pass ? "pass" : "FAIL", c.detail);
    };
    return one("a session", session) + " | " + one("b sam", sam) + " | " + one("c field", field) + " | " +
           one("d queues", queues);
}

unsigned BarrierResult::pattern() const {
    return (session.pass ? 1u : 0u) | (sam.pass ? 2u : 0u) | (field.pass ? 4u : 0u) | (queues.pass ? 8u : 0u);
}

BarrierResult evaluate_barrier(const FieldSnap& baseline, const DepartedSnap& snap, const BarrierReadings& r) {
    BarrierResult out;
    // a. The native Close cleared session+0x90 (0xB8A9031).
    if (!r.session_read) {
        out.session = {false, "retired session 2 unreadable"};
    } else if (r.session_user) {
        out.session = {false, std::format("user 0x{:X} still attached (+0x90)", r.session_user)};
    } else {
        out.session = {true, "native session detached, +0x90 0x0"};
    }
    // b. Keys captured while valid, checked by key only.
    if (!snap.any()) {
        out.sam = {false, "no snapshot of user 2 was taken: the SAM cannot be checked"};
    } else if (!snap.lookup_verified) {
        out.sam = {false, std::format("SAM lookup not verified (user key 0x{:08X} did not resolve while logged in)",
                                      snap.user_key)};
    } else if (!r.sam_read) {
        out.sam = {false, "SAM unreadable"};
    } else if (r.keys_resolving || r.user_listed) {
        out.sam = {false, std::format("{} of {} keys still resolve (first 0x{:08X}); user key 0x{:08X} {} the user "
                                      "list",
                                      r.keys_resolving, r.keys_checked, r.first_resolving, snap.user_key,
                                      r.user_listed ? "still in" : "not in")};
    } else {
        out.sam = {true, std::format("sam user/children absent: {} keys checked, user 0x{:08X} not in the user "
                                     "list{}",
                                     r.keys_checked, snap.user_key,
                                     snap.user_listed ? "" : " (list not verified at login)")};
    }
    // c. The host's field NOW, structurally (P8 S29, reentrada.md §6): no baseline from before user 2's login is
    // needed. User 2's logout must have taken it out of the host's field: its entry gone, no entry of anyone but the
    // host's user (stronger than "users as in the baseline", which a departed entry swapped for another would pass),
    // and the user-actor total equal to the sum of the list's per-user counts (the departed user's actors not still
    // counted; the total itself may change: live 2026-10-09 the host called its horse, 2 -> 3). The host owns its
    // field. A baseline of the SAME field adds its generation and open as information only; a missing one (S29 step 2:
    // user 2 logged in while the host was still at its title screen) or one of another field (the host travelled) is
    // not a failure any more.
    if (!r.field.ok) {
        out.field = {false, "the host's field is unreadable now"};
    } else {
        const auto& f = r.field;
        const bool owner_ok = f.host_key != 0 && f.owner == f.host_key;
        const bool counts_ok = r.listed_actors && *r.listed_actors == f.user_actors;
        // At least one entry (review of 967c6ca, A-M2): with no foreign entry that is the host's own, measured in every
        // S29 log (users 0x1 without user 2); an EMPTY list (a transient field mid-travel) proves nothing about user 2.
        const bool entries_ok = r.list_entries && *r.list_entries >= 1 && *r.list_entries == f.users;
        const bool foreign_ok = r.foreign_entries && *r.foreign_entries == 0;
        out.field.pass = owner_ok && r.field_list_read && !r.departed_in_field && counts_ok && entries_ok && foreign_ok;
        std::string base;
        if (!baseline.ok) {
            base = "no baseline (user 2 came in before the host was in its world): structural check";
        } else if (baseline.field != f.field) {
            base = std::format("the host changed field since the baseline 0x{:X}: structural check", baseline.field);
        } else {
            base = std::format("baseline of this field (information only): users 0x{:X} userActors 0x{:X} owner "
                               "0x{:08X} open {}/{} generation 0x{:08X}/0x{:08X} (now/baseline){}",
                               baseline.users, baseline.user_actors, baseline.owner, f.open, baseline.open,
                               f.generation, baseline.generation,
                               f.generation == baseline.generation && f.open == baseline.open ? "" : " (CHANGED)");
        }
        out.field.detail = std::format(
            "field 0x{:X} owner 0x{:08X} host 0x{:08X}{} users 0x{:X} userActors 0x{:X} open {} generation 0x{:08X} | "
            "list: entries {}, departed user in it: {}, not the host's user: {}, listed actors {} | {}",
            f.field, f.owner, f.host_key, owner_ok ? "" : " (NOT THE OWNER)", f.users, f.user_actors, f.open,
            f.generation,
            r.list_entries ? std::format("{}{}", *r.list_entries, entries_ok ? "" : " (MISMATCH)")
                           : std::string("unreadable"),
            !r.field_list_read ? "unreadable" : (r.departed_in_field ? "YES" : "no"),
            !r.foreign_entries ? std::string("unreadable")
            : *r.foreign_entries ? std::format("{} (first 0x{:X})", *r.foreign_entries, r.first_foreign)
                                 : std::string("0"),
            r.listed_actors ? std::format("0x{:X}{}", *r.listed_actors, counts_ok ? "" : " (MISMATCH)")
                            : std::string("unreadable"),
            base);
    }
    // d. Nothing of the old epoch left in the mod.
    const uint64_t want_handed = r.expect_handed ? 1 : 0;
    out.queues.pass = r.early == 0 && r.outgoing_bytes == 0 && r.pb_pushed == r.pb_seen &&
                      r.logout_handed == want_handed && r.mirror_pending == 0;
    out.queues.detail = std::format("early {} outgoing {} B packet buffers {} handed/{} seen 2001 handed {}x mirror {}",
                                    r.early, r.outgoing_bytes, r.pb_pushed, r.pb_seen, r.logout_handed,
                                    r.mirror_pending);
    return out;
}

// ------------------------------------------------------------------ epochs of the packet buffers

namespace {
constexpr uint32_t kTagHigh = 0xD0D0;
}

uint32_t pb_tag(uint32_t epoch) { return (kTagHigh << 16) | (epoch & 0xFFFF); }

std::optional<uint32_t> pb_tag_epoch(uint32_t tag) {
    if ((tag >> 16) != kTagHigh) return std::nullopt;
    return tag & 0xFFFF;
}

PbRoute route_pb(uint32_t tag, uint32_t current_epoch, const uintptr_t* sessions, size_t count) {
    PbRoute r;
    const auto e = pb_tag_epoch(tag);
    if (!e || *e == 0 || *e >= count) return r;
    r.epoch = *e;
    r.session = sessions[*e];
    r.stale = *e < current_epoch;
    return r;
}

uint32_t epoch_of_session(uintptr_t session, const uintptr_t* sessions, size_t count) {
    if (!session) return 0;
    for (size_t e = 1; e < count; ++e) {
        if (sessions[e] == session) return static_cast<uint32_t>(e);
    }
    return 0;
}

// ------------------------------------------------------------------ the state machine

GuestLifecycle::GuestLifecycle(Config c) : cfg_(c) {}

bool GuestLifecycle::admit(uint32_t link_epoch) {
    if (state_ != GuestState::Active || link_epoch < min_link_) return false;
    if (!bound_) {
        bound_ = link_epoch;
        return true;
    }
    return *bound_ == link_epoch;
}

bool GuestLifecycle::begin_logout(LogoutTrigger t, uint32_t now_ms) {
    if (state_ != GuestState::Active) return false;
    state_ = GuestState::Retiring;
    trigger_ = t;
    begin_ms_ = now_ms;
    handed_ = false;
    passing_ = false;
    return true;
}

void GuestLifecycle::note_handed(uint32_t now_ms) {
    if (state_ != GuestState::Retiring || handed_) return;
    handed_ = true;
    handed_ms_ = now_ms;
}

GuestLifecycle::Step GuestLifecycle::on_barrier(uint32_t now_ms, bool nothing_reached, const BarrierResult& r,
                                                bool quiet_without_user) {
    if (state_ != GuestState::Retiring) return state_ == GuestState::Quarantined ? Step::Quarantined : Step::Wait;
    if (!handed_) {
        if ((nothing_reached || (quiet_without_user && r.session.pass)) && r.queues.pass) {
            state_ = GuestState::Clean;
            return Step::Clean;
        }
        if (now_ms - begin_ms_ >= cfg_.pending_ms) {
            quarantine(std::format("logout: no 2001 was handed in {} s (no user 2 appeared) and packets of epoch {} "
                                   "reached the server",
                                   cfg_.pending_ms / 1000, epoch_));
            return Step::Quarantined;
        }
        return Step::Wait;
    }
    if (r.all()) {
        if (!passing_) {
            passing_ = true;
            pass_since_ms_ = now_ms;
        } else if (now_ms - pass_since_ms_ >= cfg_.stable_ms) {
            state_ = GuestState::Clean;
            return Step::Clean;
        }
        return Step::Wait;
    }
    passing_ = false;
    if (now_ms - handed_ms_ >= cfg_.budget_ms) {
        const std::string which = r.first_failed();
        const CheckResult& c = which == "a session" ? r.session
                               : which == "b sam"   ? r.sam
                               : which == "c field" ? r.field
                                                    : r.queues;
        quarantine(std::format("{} ({}), {} s after the 2001", which, c.detail, cfg_.budget_ms / 1000));
        return Step::Quarantined;
    }
    return Step::Wait;
}

bool GuestLifecycle::make_ready(uint32_t link_epoch_now) {
    if (state_ != GuestState::Clean) return false;
    if (epoch_ >= cfg_.max_epochs) {
        quarantine(std::format("epoch cap: {} session 2 objects built in this run (each one stays alive)", epoch_));
        return false;
    }
    state_ = GuestState::Ready;
    ready_link_ = link_epoch_now;
    bound_.reset();
    handed_ = false;
    passing_ = false;
    return true;
}

bool GuestLifecycle::start_epoch(uint32_t link_epoch) {
    if (!may_start(link_epoch)) return false;
    ++epoch_;
    state_ = GuestState::Active;
    bound_.reset();
    min_link_ = ready_link_ + 1;
    handed_ = false;
    passing_ = false;
    return true;
}

bool GuestLifecycle::start_epoch_inband(uint32_t link_epoch) {
    if (!may_start_inband(link_epoch)) return false;
    ++epoch_;
    state_ = GuestState::Active;
    bound_.reset();
    min_link_ = link_epoch;  // this connection (and newer ones) may carry the new conversation
    handed_ = false;
    passing_ = false;
    return true;
}

void GuestLifecycle::quarantine(const std::string& why) {
    if (state_ == GuestState::Quarantined) return;
    state_ = GuestState::Quarantined;
    reason_ = why;
}

}  // namespace cdmp::session2
