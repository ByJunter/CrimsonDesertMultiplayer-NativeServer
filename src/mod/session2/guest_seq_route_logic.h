#pragma once
// P8 S1 option A, the GUEST's half (docs/research/pendientes/p8-impl-opcion-a.md): [session2]
// guest_seq_route = 0 | probe | 1, guest=1. The pure logic, with no game access.
//
// The fact it answers [V] (p8-observador-escenas.md §14.1-§14.2; live, session 7: "GuestStage [2981 route
// 0xC07749] ... manager ... of actor 0xA01002E9 ... sequencer owner 0xA0100001 -> ANOTHER character's
// manager"): the client's stage Ack handlers (2661 0xC05BB0, 2981 0xC073A0, 2810 0xC06330, 2322 0xB87890)
// take the manager from the field the client is in (0x17FA6C0 -> ClientField::vf48 0x9A5BE0): the
// manager M of the field owner's character, which the rewritten 2174 makes user 2's own. The NPCs of the
// host's sequencers look in X = [[[A0100001+0x68]+0xA8]+0x18] (0x9ED45B; G3: X exists, 0 stages).
//
// What it does: right after vf48 returned (RAX = M), when the packet's actor is a CHARACTER (0xA...) that
// is not M's own and has a ClientSequencerStageManager X of its own, RAX becomes X: the native handler
// then creates/updates/deletes the sequencer (or writes +0x231) in the manager of the character the
// packet names, as the host's own client does for itself. Nothing else changes; user 2's own scenes (its
// own character) and every packet of an NPC/user key stay as they are.
//
// The delivery gate: a 2661 / 2810 / 2322 naming another character never reaches this client natively
// (session 7: the guest got 2661 x1, 2810 x1, 2322 x2, all its own); only observer_seq_copy sends them.
// Unless the routing for that opcode is ON and hooked, such a packet is dropped before OnRecvPacket: it
// would land in M (forbidden: p8-s1-escenas-npc.md §5.5).

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <span>
#include <string_view>

namespace cdmp::session2::gsr {

// P2524 (guest_seq_play_live=1, p8-impl-escenas-play.md §3): StageOnBranchEventAck 2524 (handler 0xC07D00;
// the vf48 manager at 0xC08178 goes to 0xA9E480, which hooks actors onto a live scene: .xtls 0x8F80300). It arrives natively (144 in session 8, into M):
// never gated, only routed.
enum class Point : uint8_t { P2661, P2981, P2810, P2322, P2524, Count };
inline constexpr size_t kPoints = static_cast<size_t>(Point::Count);
inline constexpr std::array<uint16_t, kPoints> kPointOp = {2661, 2981, 2810, 2322, 2524};
inline const char* point_name(Point p) {
    switch (p) {
        case Point::P2661: return "2661 PrepareSequencerAck 0xC0612A";
        case Point::P2981: return "2981 StageBranchStateAck 0xC07749";
        case Point::P2810: return "2810 DeleteSequencerAck 0xC0659E";
        case Point::P2322: return "2322 PlayAllPreLoadSequencerAck 0xB87A67";
        case Point::P2524: return "2524 StageOnBranchEventAck 0xC08178";
        default: return "?";
    }
}
inline std::optional<Point> point_of_op(uint16_t op) {
    for (size_t i = 0; i < kPoints; ++i) {
        if (kPointOp[i] == op) return static_cast<Point>(i);
    }
    return std::nullopt;
}

enum class Mode : uint8_t { Off, Probe, On };
inline Mode parse_mode(std::string_view v, bool* bad = nullptr) {
    if (bad) *bad = false;
    if (v.empty() || v == "0") return Mode::Off;
    if (v == "probe") return Mode::Probe;
    if (v == "1") return Mode::On;
    if (bad) *bad = true;
    return Mode::Off;
}

inline uint32_t le32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}
inline constexpr uint32_t kCharClass = 0xA;

// What a routing point read (SEH reads in the hook; the tests by hand).
struct RouteFacts {
    Mode mode = Mode::Off;
    bool point_on = false;      // this point is hooked (here or by its owner module) and its code verified
    bool kind_ok = true;        // 2661: kind (BL) == 4, the only kind 0xA9CA40 runs for
    uint64_t m = 0;             // RAX: what vf48 returned
    uint32_t m_key = 0;         // key of [M+0x18], M's own character (this client's field owner's)
    uint64_t actor = 0;         // the packet's resolved actor (0 = none)
    uint32_t actor_key = 0;     // [actor+0x60]
    uint32_t packet_key = 0;    // the key in the packet (0 = not read here)
    uint64_t x = 0;             // [[[actor+0x68]+0xA8]+0x18]
    uint64_t x_vt = 0;          // [X]
    uint64_t want_vt = 0;       // exe + pa::ClientSequencerStageManager's vtable
    uint32_t x_key = 0;         // key of [X+0x18]
    // The 2524 (review 11): routed only to an X guest_seq_play ticks (in its table on this connection).
    bool need_known = false;
    bool x_known = false;
    // Session 9 (p8-impl-escenas-play.md §15): the sequencer's resource is on the deny list (a cutscene that
    // reloads the field: its branch in X started THIS client's field load and the server logged user 2 out).
    bool denied = false;
    // Session 10 (§16): the 2524's consumer 0xA9E480 assumes a loaded, playing scene; for X it is checked first.
    uint8_t ready2524 = 0;  // a Ready2524 (0 = Ok); only read when need_known
    // guest_sched_route (p8-impl-horarios-x.md): the schedule handlers all go through [G+0x148] (0xA9FEE0 / 0xAA0F00
    // return at once when it is 0; 0xC0BA52 passes it to the delete 0x43C750 unchecked): X must have its container.
    bool need_sched = false;
    bool x_sched_ok = false;  // [X+0x148] != 0
    // Session 13 (guest_seq_route_2524 = probe | 1): the 2524 only for a scene classify_2524 calls World.
    bool need_world = false;
    bool world_ok = false;
};

// ---------------------------------------------------------------- the 2524 into X (session 10)
//
// 0xA9E480(G, field, seq, branch, ...) [V capstone]: takes [G+8]'s lock (0xA9E4FA), finds the instance by
// sequencer id (0x1F6C190, 0xA9E50F) and, when found (rdi != 0 at 0xA9E561), does NOT check the scene:
// r13 = [inst+0x58] (0xA9E62E), rax = [r13+0x38] (0xA9E637), rdx = [rax+8] (0xA9E63B), [rdx + branch*8]
// (0xA9E63F). Natively a 2524 only comes for a scene that plays. In session 10 a 2524 routed into X while this
// client was still loading (M+0x231 2, X+0x231 2) found an instance whose scene data [stage+0x38] was still 0:
// read of 0x8 at 0xA9E63B, the guest crashed.
//
// Two checks (review of b728ec7):
//  1. in the routing hook (0xC08178), before RAX := X: X and this client are playing (pre_2524). Cheap fields of
//     X and M; nothing of X's map is walked there.
//  2. inside 0xA9E480 itself, at 0xA9E55B (after the native lookup, with [G+8]'s lock held, before `test rdi,rdi`),
//     only for a call the routing hook sent to X: the instance the game found (rdi) is checked (scene_2524); when
//     it cannot be used, rdi := 0 and the game takes its own "not found" path (0xA9E566: releases the lock, then
//     0xA9ECBC releases the instance's reference [rsp+0x58]). No race: the instance and its scene are read under
//     the game's lock, as the game then reads them.
enum class Ready2524 : uint8_t { Ok, NotPlaying, NoInstance, SceneNotLoaded, BadBranch };
inline Ready2524 pre_2524(uint8_t x_ready, uint32_t x_loading, uint8_t m_ready) {
    // +0x174: the entry count of X's loading-task map (nodes +0x188; 0xA94660 frees them; 0xA97366 and the state
    // 0x14 predicate 0xA97DE8 wait for 0): not instances.
    return x_ready == 3 && x_loading == 0 && m_ready == 3 ? Ready2524::Ok : Ready2524::NotPlaying;
}
// The scene behind the instance 0xA9E480 found (read under its lock, by the inner hook).
struct Scene2524 {
    uint64_t inst = 0;        // rdi at 0xA9E55B (0: the game did not find it)
    uint64_t stage = 0;       // [inst+0x58]
    uint64_t data = 0;        // [stage+0x38] (the scene's data: 0 until loaded)
    uint64_t branches = 0;    // [data+8]
    uint32_t count = 0;       // [data+0x10] [E: the engine's vector {ptr, u32 count, u32 capacity} at data+8]
    uint32_t capacity = 0;    // [data+0x14]
    uint32_t branch = 0;      // r12d (the packet's u32 @13, 0xA9E4B4 mov r12d,r9d)
    uint64_t entry = 0;       // [branches + branch*8] (read only when branch < count)
};
// No code of the game bounds the index (0xA9E63F, 0xA9953C, 0xAA1B4D only check -1): the table's own size is
// taken from its vector header, which must look like one (count <= capacity, both < 4096); otherwise refused.
inline constexpr uint32_t kMaxBranchTable = 4096;
inline Ready2524 scene_2524(const Scene2524& s) {
    if (!s.inst) return Ready2524::NoInstance;  // the game's own not-found path anyway
    if (!s.stage || !s.data || !s.branches) return Ready2524::SceneNotLoaded;
    if (!s.count || s.count > s.capacity || s.capacity > kMaxBranchTable) return Ready2524::BadBranch;
    if (s.branch >= s.count || !s.entry) return Ready2524::BadBranch;
    return Ready2524::Ok;
}

// ---------------------------------------------------------------- payload layouts [V, session 9 captures]

// 2981 StageBranchStateAck and 2524 StageOnBranchEventAck: u8 kind | u32 actor @1 | u64 sequencer @5 | u32 @13
// (the 2981's branch state). GC2 t=61.409 2524: 04 | 01 00 10 A0 | 64 01 00 00 00 00 00 00 | 01 00 00 00 00.
inline constexpr size_t kBranchActorAt = 1, kBranchSeqAt = 5, kBranchStateAt = 13, kBranchMinPayload = 17;
// 2661 PrepareSequencerAck: u8 kind | u32 actor @1 | u32 owner @5 | u64 sequencer @9 | u8 | u16 | u64 stage @19 |
// u32 resource @27 | ... (GC1 t=722.169, sequencer 0x213: stage 0xF4568, resource 0x8CD771B1).
inline constexpr size_t kPrepareActorAt = 1, kPrepareSeqAt = 9, kPrepareStageAt = 19, kPrepareResourceAt = 27,
                        kPrepareMinPayload = 31;

struct BranchIds {
    uint32_t actor = 0;
    uint64_t seq = 0;
    uint32_t state = 0;
};
inline std::optional<BranchIds> branch_ids(std::span<const uint8_t> p) {
    if (p.size() < kBranchMinPayload) return std::nullopt;
    BranchIds b;
    b.actor = le32(&p[kBranchActorAt]);
    std::memcpy(&b.seq, &p[kBranchSeqAt], 8);
    b.state = le32(&p[kBranchStateAt]);
    return b;
}
struct PrepareIds {
    uint32_t actor = 0;
    uint64_t seq = 0;
    uint32_t resource = 0;
};
inline std::optional<PrepareIds> prepare_ids(std::span<const uint8_t> p) {
    if (p.size() < kPrepareMinPayload) return std::nullopt;
    PrepareIds r;
    r.actor = le32(&p[kPrepareActorAt]);
    std::memcpy(&r.seq, &p[kPrepareSeqAt], 8);
    r.resource = le32(&p[kPrepareResourceAt]);
    return r;
}

// ---------------------------------------------------------------- the cutscene deny list (session 9)
//
// No flag in the 2661 tells a field-reloading cutscene from a shop or table scene: in the session 9 capture the
// 86-byte play data of 0x213 (the camp liberation, resource 0x8CD771B1) has the same layout and constants as
// 0x13 / 0x19 / 0x73 / 0x147 (kind 01 00, zero position, ff ff ff 7f 01, ff x8, 0d) [V]. So the guard is a list of
// resources, guest_seq_route_deny (hex, default 8CD771B1): a 2661 with such a resource is not created in X
// (refused, as before option A: nothing in M either), and its 2981 / 2524 are not routed (M has no such id: no
// effect, as before option A).
template <size_t N>
struct DenyList {
    uint32_t res[N] = {};
    size_t n = 0;
    std::string bad;  // tokens that are not hex (for the log)
    static DenyList parse(std::string_view s) {
        DenyList d;
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && (s[i] == ',' || s[i] == ' ' || s[i] == ';' || s[i] == '\t')) ++i;
            size_t j = i;
            while (j < s.size() && s[j] != ',' && s[j] != ' ' && s[j] != ';' && s[j] != '\t') ++j;
            if (j > i) {
                std::string_view t = s.substr(i, j - i);
                if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t.remove_prefix(2);
                uint64_t v = 0;
                bool ok = !t.empty() && t.size() <= 8;
                for (char c : t) {
                    const int h = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                     : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                    if (h < 0) {
                        ok = false;
                        break;
                    }
                    v = v << 4 | static_cast<uint64_t>(h);
                }
                const std::string_view tok = s.substr(i, j - i);
                const auto note_bad = [&d, tok](const char* why) {
                    d.bad += (d.bad.empty() ? "" : " ") + std::string(tok) + why;
                };
                if (!ok) {
                    note_bad("");
                } else if (!v) {
                    note_bad(" (zero: no resource)");  // review 6
                } else if (d.has(static_cast<uint32_t>(v))) {
                    // a repeat: already listed
                } else if (d.n >= N) {
                    note_bad(" (over the limit of the list)");  // review 6
                } else {
                    d.res[d.n++] = static_cast<uint32_t>(v);
                }
            }
            i = j;
        }
        return d;
    }
    bool has(uint32_t r) const {
        for (size_t i = 0; i < n; ++i) {
            if (res[i] == r) return true;
        }
        return false;
    }
};

// The resource of each sequencer a 2661 brought (routed or denied), so a later 2981 / 2524 of it can be judged
// and logged with it. The routing hooks only (the game's main thread: the Ack handlers run in 0xAD2380 ->
// 0x35D0C0). A ring of the last N; an unknown id (prepared before this table, or evicted) is not denied.
// Session 13 (p8-impl-secuenciador-bodyanim.md): also the actor keys its 2661 binds (up to kSeqBinds), so a later
// 2524 of it can be classified by the NPCs it moves (gsr::classify_2524).
inline constexpr size_t kSeqBinds = 8;
template <size_t N>
class SeqResources {
public:
    struct Entry {
        uint64_t seq = 0;
        uint32_t resource = 0;
        bool denied = false;
        uint8_t nbinds = 0;
        uint32_t binds[kSeqBinds] = {};
    };
    void note(uint64_t seq, uint32_t resource, bool denied, std::span<const uint32_t> binds = {}) {
        Entry* slot = nullptr;
        for (Entry& e : e_) {
            if (e.seq == seq && seq) {
                slot = &e;
                break;
            }
        }
        if (!slot) {
            slot = &e_[next_ % N];
            ++next_;
        }
        *slot = Entry{};
        slot->seq = seq;
        slot->resource = resource;
        slot->denied = denied;
        for (uint32_t k : binds) {
            if (slot->nbinds < kSeqBinds) slot->binds[slot->nbinds++] = k;
        }
    }
    const Entry* find(uint64_t seq) const {
        if (!seq) return nullptr;
        for (const Entry& e : e_) {
            if (e.seq == seq) return &e;
        }
        return nullptr;
    }

private:
    Entry e_[N] = {};
    size_t next_ = 0;
};

enum class Verdict : uint8_t {
    Route,           // RAX := X
    WouldRoute,      // probe: the same decision, not applied
    Off,
    NotHooked,
    NotKind4,
    NoManager,
    NoActor,
    KeyMismatch,     // the resolved actor is not the packet's key
    NotCharacter,    // an NPC/user key: never routed
    OwnCharacter,    // M's own character (user 2's own scenes): native
    NoX,
    NotStageManager,
    XNotItsOwn,      // [X+0x18] is not the actor
    AlreadyX,        // vf48 already returned X
    NotPlayed,       // 2524: X is not one guest_seq_play ticks (it would only queue actors nobody updates)
    Denied,          // the sequencer's resource is on guest_seq_route_deny (a field-reloading cutscene, session 9)
    NotPlaying2524,  // 2524: X or this client not at 3, or X loading (session 10)
    NoInstance2524,  // 2524: the sequencer is not (or no longer) in X
    NotLoaded2524,   // 2524: its scene data [[inst+0x58]+0x38] / branch table not there yet (the session 10 crash)
    BadBranch2524,   // 2524: the branch index or its entry is not usable
    // Session 13 (p8-impl-secuenciador-bodyanim.md): guest_seq_route_2524 routes only WORLD scenes (classify_2524).
    NotWorld2524,
    // guest_sched_route (p8-impl-horarios-x.md): the NPC schedule stages.
    NoSchedContainer,  // [X+0x148] is 0: the schedule handler would do nothing (create) or fault (delete) in X
    CreatedInM,        // 2126/2958/2909: its 2335 went to M (before the option, refused, or not routable): native
    NotInX,            // 2126/2958/2909 of a key the ledger does not know, and X's container does not have it: native
    LedgerStale,       // 2126/2958/2909: its 2335 went to another X than the actor's current one: native
    // p8-impl-horarios-x.md §10: a creating 2335 (kind 1/4) of a key X+0x148 already holds (observer_sched_copy's copy
    // may come before the native 2335 of the same key; 0xA9FEE0 only dedups by wake spot): refused (RAX := a sink whose
    // +0x148 is 0, 0xA9FEE0 returns at 0xA9FF14).
    DuplicateCreate,
    // A 2335 that does not create (kind != 1/4: 0xA9FEE0 looks the key up with 0x43C3A0 and calls 0x43A890 on what it
    // found, WITHOUT a null check: .xtls 0x88E5F80 reads [stage+0x138]) whose key is in neither the manager it would go
    // to: refused the same way (it would fault).
    NoStageForKey,
    Count
};
inline const char* verdict_name(Verdict v) {
    switch (v) {
        case Verdict::Route: return "ROUTED to the actor's own manager X";
        case Verdict::WouldRoute: return "WOULD route to X (probe)";
        case Verdict::Off: return "off";
        case Verdict::NotHooked: return "point not hooked";
        case Verdict::NotKind4: return "kind != 4 (0xA9CA40 does not run)";
        case Verdict::NoManager: return "no manager from vf48";
        case Verdict::NoActor: return "no resolved actor";
        case Verdict::KeyMismatch: return "the resolved actor is not the packet's key";
        case Verdict::NotCharacter: return "not a character (0xA...) key: native";
        case Verdict::OwnCharacter: return "M's own character: native";
        case Verdict::NoX: return "the actor has no stage manager";
        case Verdict::NotStageManager: return "X is not a ClientSequencerStageManager";
        case Verdict::XNotItsOwn: return "X's character is not the actor";
        case Verdict::AlreadyX: return "vf48 already returned X";
        case Verdict::NotPlayed: return "X is not ticked by guest_seq_play (2524 left native)";
        case Verdict::Denied: return "DENIED: the sequencer's resource is on guest_seq_route_deny (cutscene / field reload)";
        case Verdict::NotPlaying2524: return "2524 left native: X / this client not at 3 or X loading";
        case Verdict::NoInstance2524: return "2524 left native: its sequencer is not in X";
        case Verdict::NotLoaded2524: return "2524 left native: its scene data is not loaded in X (0xA9E63B would fault)";
        case Verdict::BadBranch2524: return "2524 left native: branch index / entry not usable";
        case Verdict::NotWorld2524: return "2524 left native: not a world scene (guest_seq_route_2524)";
        case Verdict::NoSchedContainer: return "X has no schedule container +0x148: native (M)";
        case Verdict::CreatedInM: return "its 2335 went to M: native (M), the same manager";
        case Verdict::NotInX: return "key unknown to the ledger and not in X+0x148: native (M)";
        case Verdict::LedgerStale: return "its 2335 went to another X: native (M)";
        case Verdict::DuplicateCreate: return "DUPLICATE create refused: X+0x148 already holds this key";
        case Verdict::NoStageForKey:
            return "REFUSED: a non-creating 2335 whose key is not in the manager it would reach (0x88E5F80 would fault)";
        default: return "?";
    }
}

// In this order (the first that fails is the reason).
inline Verdict decide(const RouteFacts& f) {
    if (f.mode == Mode::Off) return Verdict::Off;
    if (!f.point_on) return Verdict::NotHooked;
    if (!f.kind_ok) return Verdict::NotKind4;
    if (!f.m) return Verdict::NoManager;
    if (!f.actor || !f.actor_key) return Verdict::NoActor;
    if (f.packet_key && f.packet_key != f.actor_key) return Verdict::KeyMismatch;
    if ((f.actor_key >> 28) != kCharClass) return Verdict::NotCharacter;
    if (f.actor_key == f.m_key) return Verdict::OwnCharacter;
    if (!f.x) return Verdict::NoX;
    if (!f.want_vt || f.x_vt != f.want_vt) return Verdict::NotStageManager;
    if (f.x_key != f.actor_key) return Verdict::XNotItsOwn;
    if (f.x == f.m) return Verdict::AlreadyX;
    if (f.need_sched && !f.x_sched_ok) return Verdict::NoSchedContainer;
    if (f.denied) return Verdict::Denied;
    if (f.need_world && !f.world_ok) return Verdict::NotWorld2524;
    if (f.need_known && !f.x_known) return Verdict::NotPlayed;
    if (f.need_known) {
        switch (static_cast<Ready2524>(f.ready2524)) {
            case Ready2524::Ok: break;
            case Ready2524::NotPlaying: return Verdict::NotPlaying2524;
            case Ready2524::NoInstance: return Verdict::NoInstance2524;
            case Ready2524::SceneNotLoaded: return Verdict::NotLoaded2524;
            default: return Verdict::BadBranch2524;
        }
    }
    return f.mode == Mode::On ? Verdict::Route : Verdict::WouldRoute;
}

// ---------------------------------------------------------------- the delivery gate

// The character a 2661 (u8 kind | u32 actor), 2810 (u32 actor | ...) or 2322 (u32 actor) names;
// nullopt for any other opcode or a short payload.
inline std::optional<uint32_t> named_actor(uint16_t op, std::span<const uint8_t> payload) {
    switch (op) {
        case 2661:
            if (payload.size() >= 5) return le32(&payload[1]);
            return std::nullopt;
        case 2810:
        case 2322:
            if (payload.size() >= 4) return le32(&payload[0]);
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

enum class Gate : uint8_t { Deliver, DropNotRouted, DropOwnUnknown };
// own_key: this client's character on this connection (its MainPlayerDataStartAck; 0 = not known yet).
// route_active: the routing of that opcode is ON and hooked.
// Fails closed: while the own key is unknown, a 2661/2810/2322 naming any character is not delivered.
// [V] session 7 (both guest captures, first login and reconnection): no 2661/2810/2322 reaches the client
// before its 2756 (8.905 s / 4.663 s), so this never drops one of its own.
inline Gate gate(uint16_t op, std::span<const uint8_t> payload, uint32_t own_key, bool route_active) {
    const auto a = named_actor(op, payload);
    if (!a || (*a >> 28) != kCharClass) return Gate::Deliver;
    if (!own_key) return Gate::DropOwnUnknown;
    if (*a == own_key) return Gate::Deliver;
    return route_active ? Gate::Deliver : Gate::DropNotRouted;
}

// With the routing ON, another character's 2661 / 2322 that cannot go to its own manager is refused
// rather than left to M (the 2661: kind := 0, the handler's native "not 4" path; the 2322: its byte
// write to a sink). 2981 / 2810 into M only look up an id M does not have (as the 129 native foreign
// 2981 of session 7) and stay native.
inline bool refuse(Point p, const RouteFacts& f, Verdict v) {
    // Only a packet whose own key names another character than M's (never this client's own 2322: S2).
    if (f.mode != Mode::On || (p != Point::P2661 && p != Point::P2322)) return false;
    if ((f.packet_key >> 28) != kCharClass || !f.m_key || f.packet_key == f.m_key) return false;
    switch (v) {
        case Verdict::Denied:  // a denied 2661 is created nowhere (as before option A)
            return p == Point::P2661;
        case Verdict::NoActor:
        case Verdict::KeyMismatch:
        case Verdict::NoX:
        case Verdict::NotStageManager:
        case Verdict::XNotItsOwn:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------- the NPC schedule stages (guest_sched_route)
//
// p8-impl-horarios-x.md [V capstone, build 2976]: the four schedule Ack handlers take the manager from the field's
// vf48 (M, the field owner's: the rewritten 2174 makes it this client's own character's), as the sequencer ones do;
// 0xA9FEE0 (2335) creates the pa::ClientNPCScheduleStage (0x1A0) in [M+0x148]+0x18. The NPCs look for it in X:
// 0x9ED290 (ClientSequencerPlayDataActorComponent::vf13, run once per actor unpack) takes the owner actor
// FindCharacter(comp+0x2D8) -> X = [[[A+0x68]+0xA8]+0x18] -> [X+0x148]->vf8(&comp+0x2E0) (0x9ED45B..0x9ED48C), then
// 0x43B4B0 and comp+0x318 = stage. S11: X+0x148 empty, M+0x148 115 / 25.
//
// guest_sched_route = 0 | probe | 1: right after vf48, RAX := X when the packet's actor is a character (0xA...) other
// than M's own whose X is a ClientSequencerStageManager with a container +0x148 (gsr::decide with need_sched).
//   0xC0B5C1  2335 CreateNPCScheduleStageAck  (handler 0xC0AFC0)  -> 0xC0B616 mov rcx,rax ; call 0xA9FEE0
//   0xC0BA4E  2126 DeleteNPCScheduleStageAck  (handler 0xC0B840)  -> 0xC0BA52 mov rcx,[rax+0x148] ; call 0x43C750
//   0xC0C761  2958 ChangeNPCScheduleMiseenscene (handler 0xC0C3F0)  -> 0xC0C76D mov rcx,rax ; call 0xAA0F00
//   0xC0CC91  2909 DeactivateMiseensceneScheduleEvadeObstacle (0xC0C920) -> 0xC0CC9D mov rcx,rax ; call 0xAA1850
// Consistency: the 2126 / 2958 / 2909 of a scene key go to the manager its 2335 went to (SchedLedger, the routing
// hooks only: the game's main thread). A key the ledger does not know goes to X only if X's container has it.
// Wire (S11 captures): 2335 u8 kind | u32 actor @1 | u32 @5 | u32 resource @9 | u8 @13 | u64 key @14 | ...;
// 2126 u32 actor | u64 key @4; 2958 / 2909 u8 | u32 actor @1 | u32 resource @5 | u64 key @9 | u32 x n.
enum class SchedPoint : uint8_t { P2335, P2126, P2958, P2909, Count };
inline constexpr size_t kSchedPoints = static_cast<size_t>(SchedPoint::Count);
inline constexpr std::array<uint16_t, kSchedPoints> kSchedOp = {2335, 2126, 2958, 2909};
inline const char* sched_point_name(SchedPoint p) {
    switch (p) {
        case SchedPoint::P2335: return "2335 CreateNPCScheduleStageAck 0xC0B5C1";
        case SchedPoint::P2126: return "2126 DeleteNPCScheduleStageAck 0xC0BA4E";
        case SchedPoint::P2958: return "2958 ChangeNPCScheduleMiseensceneAck 0xC0C761";
        case SchedPoint::P2909: return "2909 DeactivateMiseensceneScheduleEvadeObstacleAck 0xC0CC91";
        default: return "?";
    }
}

enum class SchedWhere : uint8_t { Unknown, M, X };
inline const char* sched_where_name(SchedWhere w) {
    return w == SchedWhere::X ? "X" : w == SchedWhere::M ? "M" : "unknown";
}

// Where a 2335 put its stage, from its verdict: X when routed (or would be, in probe: the follow-ups then keep the
// same decision), M for another character's 2335 that could not be routed, Unknown (not noted) for a packet the
// routing never judges (off, not hooked, an NPC / user key, M's own character: those stay native anyway).
inline SchedWhere sched_created_where(Verdict v) {
    switch (v) {
        case Verdict::Route:
        case Verdict::WouldRoute: return SchedWhere::X;
        case Verdict::Off:
        case Verdict::NotHooked:
        case Verdict::NotCharacter:
        case Verdict::OwnCharacter: return SchedWhere::Unknown;
        default: return SchedWhere::M;
    }
}

// A 2126 / 2958 / 2909: v is gsr::decide's verdict for it now; where / same_x what the ledger says of its key (same_x:
// the X recorded is the actor's current X); x_has_key: X's container has the key (read only when where is Unknown).
inline Verdict sched_followup(Verdict v, SchedWhere where, bool same_x, bool x_has_key) {
    if (v != Verdict::Route && v != Verdict::WouldRoute) return v;  // X not usable now: native (M)
    switch (where) {
        case SchedWhere::M: return Verdict::CreatedInM;
        case SchedWhere::X: return same_x ? v : Verdict::LedgerStale;
        default: return x_has_key ? v : Verdict::NotInX;
    }
}

// p8-impl-horarios-x.md §10 [V capstone, 0xA9FEE0]: the kind (u8 @0, BL, 0xA9FEE0's r8b -> [rbp+0x57]) decides:
// 1 / 4 create (0xAA0066 / 0xAA0072; deduplicated only by wake spot: stage+0x58 == the id the u32 @5 maps to in the
// table at [0x6D6C1F8], when it maps to one: 0xAA010D..0xAA0137, never by key); any other kind looks the key up
// (0x43C3A0, 0xAA0089) and calls 0x43A890 (.xtls 0x88E5F80) on the result, null or not.
inline bool sched_creates(uint32_t kind) { return kind == 1 || kind == 4; }
// A creating 2335 routed (or that would be) into X whose key X's container already has.
inline Verdict sched_2335_create(Verdict v, bool x_has_key) {
    return (v == Verdict::Route || v == Verdict::WouldRoute) && x_has_key ? Verdict::DuplicateCreate : v;
}
// A non-creating 2335 after sched_followup (fv): judged = the routing looked at it (another character's packet);
// target_has_key = the manager fv sends it to (X when Route/WouldRoute, else M) has the key.
inline Verdict sched_2335_other(Verdict fv, bool judged, bool target_has_key) {
    return judged && !target_has_key ? Verdict::NoStageForKey : fv;
}
inline bool sched_refused(Verdict v) { return v == Verdict::DuplicateCreate || v == Verdict::NoStageForKey; }
// The routing judged it (another character's schedule packet): everything but these four.
inline bool sched_judged(Verdict v) {
    return v != Verdict::Off && v != Verdict::NotHooked && v != Verdict::NotCharacter && v != Verdict::OwnCharacter;
}

// p8-impl-horarios-x.md §11: the X managers stages were routed into, with their character (a few: the host's). A stage
// outlives its X when an NPC still holds it (comp+0x318) after the host character left this client; its last release
// runs 0x43AE50 again, which calls [stage+0x28]->vf12 (0x43B0B2..0x43B0BC): on a freed X. The guard (0x43B0B6) sends
// that call to a do-nothing manager of the mod when the stage's X is one of these and no longer its character's.
// One writer (the routing hook, the game's main thread); readers on any thread (the guard runs wherever a stage's
// last reference goes): atomics, the owner stored before its X.
template <size_t N>
class XRegistry {
public:
    // Known X: its owner refreshed; else a free slot, else the oldest goes.
    void note(uint64_t x, uint32_t owner) {
        if (!x || !owner) return;
        for (size_t i = 0; i < N; ++i) {
            if (x_[i].load(std::memory_order_acquire) == x) {
                owner_[i].store(owner, std::memory_order_relaxed);
                return;
            }
        }
        size_t slot = N;
        for (size_t i = 0; i < N && slot == N; ++i) {
            if (!x_[i].load(std::memory_order_relaxed)) slot = i;
        }
        if (slot == N) slot = next_++ % N;
        x_[slot].store(0, std::memory_order_release);
        owner_[slot].store(owner, std::memory_order_relaxed);
        x_[slot].store(x, std::memory_order_release);
    }
    uint32_t owner_of(uint64_t x) const {
        if (!x) return 0;
        for (size_t i = 0; i < N; ++i) {
            if (x_[i].load(std::memory_order_acquire) == x) return owner_[i].load(std::memory_order_relaxed);
        }
        return 0;
    }
    uint64_t x_at(size_t i) const { return i < N ? x_[i].load(std::memory_order_acquire) : 0; }
    uint32_t owner_at(size_t i) const { return i < N ? owner_[i].load(std::memory_order_relaxed) : 0; }

private:
    std::atomic<uint64_t> x_[N] = {};
    std::atomic<uint32_t> owner_[N] = {};
    size_t next_ = 0;  // the writer only
};
// The guard's decision: the stage's X (rcx) is the stage's +0x28, is one the routing put stages into, and its character
// no longer has it as its manager (live_x: manager_of(find_actor(owner)), 0 if the character is gone).
enum class Teardown : uint8_t { NotOurs, Mismatch, Live, Redirect, Count };
inline Teardown teardown_guard(uint64_t rcx, uint64_t stage_mgr, uint32_t owner, uint64_t live_x) {
    if (!rcx || rcx != stage_mgr) return Teardown::Mismatch;
    if (!owner) return Teardown::NotOurs;
    return live_x == rcx ? Teardown::Live : Teardown::Redirect;
}

// Scene key -> the manager its 2335 went to. Fixed size, no allocation; a 2126 frees its slot; when all N are live a
// ring evicts one (an evicted key is judged as Unknown: X's container decides). The routing hooks only (one thread:
// res_thread_ok in the hook).
template <size_t N>
class SchedLedger {
public:
    struct Entry {
        uint64_t key = 0;
        uint64_t mgr = 0;      // the manager the 2335 went to (X or M), for the log and same_x
        uint32_t actor = 0;    // the packet's character
        SchedWhere where = SchedWhere::Unknown;
        uint8_t lines = 0;     // log lines of this key (sched_line_ok)
    };
    // A new 2335 of a live key replaces its entry (the native 0xA9FEE0 dedups by wake spot, not by key).
    Entry* note(uint64_t key, uint64_t mgr, uint32_t actor, SchedWhere where) {
        if (!key || where == SchedWhere::Unknown) return nullptr;
        if (Entry* e = find_mut(key)) {
            e->mgr = mgr, e->actor = actor, e->where = where;
            return e;
        }
        for (Entry& e : e_) {  // a free slot (a deleted key's) first
            if (!e.key) {
                e = Entry{key, mgr, actor, where, 0};
                return &e;
            }
        }
        Entry& e = e_[next_++ % N];  // full: the ring evicts
        ++evicted_;
        e = Entry{key, mgr, actor, where, 0};
        return &e;
    }
    const Entry* find(uint64_t key) const {
        if (!key) return nullptr;
        for (const Entry& e : e_) {
            if (e.key == key) return &e;
        }
        return nullptr;
    }
    Entry* find_mut(uint64_t key) { return const_cast<Entry*>(static_cast<const SchedLedger*>(this)->find(key)); }
    void erase(uint64_t key) {
        if (Entry* e = find_mut(key)) *e = Entry{};
    }
    size_t live(SchedWhere w) const {
        size_t n = 0;
        for (const Entry& e : e_) n += e.key && e.where == w;
        return n;
    }
    uint64_t evicted() const { return evicted_; }
    // P8 re-entry (s25/reentrada.md §1.4): a new remote conversation (its owner thread only). Returns the live keys
    // dropped.
    size_t clear() {
        size_t n = 0;
        for (Entry& e : e_) {
            n += e.key != 0;
            e = Entry{};
        }
        next_ = 0;
        return n;
    }

private:
    Entry e_[N] = {};
    size_t next_ = 0;
    uint64_t evicted_ = 0;
};
// At most this many lines per key (create, delete and a couple of changes), beyond the window limiter.
inline constexpr uint8_t kSchedLinesPerKey = 4;
template <class E>
inline bool sched_line_ok(E* e) {
    if (!e) return true;  // a key the ledger does not hold: only the window limiter
    if (e->lines >= kSchedLinesPerKey) return false;
    ++e->lines;
    return true;
}

// The diagnostics thread's census of one manager's schedule container (SEH reads; p8-impl-horarios-x.md §5):
// [G+0x148] = C; C+0x18 the stage vector, C+0x20 its count (0xA9FEE0, 0x43C930); a stage: +0x28 its manager G
// (0xC183AB0), +0x30 the scene key (0x43C930 compares it), +0x178 active (0x43AA30 / 0x43CEF0), nodes +0x138 (count
// +0x140, stride 0x168), a node's +0x28 = the NPC bound to it (0x43B4B0 writes it; 0x43CEF0 reads it).
inline constexpr uint32_t kMaxCensusStages = 1024, kMaxCensusNodes = 256;
struct SchedCensus {
    bool readable = false;
    uint32_t stages = 0;
    uint32_t active = 0;        // +0x178 != 0
    uint32_t other_mgr = 0;     // stage+0x28 != this manager (should stay 0)
    uint32_t nodes = 0;
    uint32_t bound = 0;         // nodes with an NPC key (+0x28)
    bool operator==(const SchedCensus&) const = default;
};
inline void census_add_stage(SchedCensus& c, bool active, bool own_mgr, uint32_t nodes, uint32_t bound) {
    ++c.stages;
    c.active += active;
    c.other_mgr += !own_mgr;
    c.nodes += nodes;
    c.bound += bound;
}

// ---------------------------------------------------------------- guest_sched_bind (p8-impl-horarios-enlace.md)
//
// [V capstone, build 2976] 0x9F2FA0(comp, const u64* key) is vf13 0x9ED290's binding body with the scene key as a
// parameter (callers: the 2874 handler 0xC0D24D and the AI's vf107 0x91CE05, both on the main thread, both after
// 0x9F3230 + 0x20853D0). It derives everything else itself: actor [comp+8]; the node handle ("local_res8") from the
// actor (type 7: [[[a+0x68]+0x30]+0x50]; else the AI's [[[[a+0x68]+0x58]+0x90]+8] -> 0x15EE200); the owner
// 0x8AE1A0(comp+0x2D8) -> X = [[[owner+0x68]+0xA8]+0x18] -> [X+0x148]->vf8(&stage, key) (NO null check of [X+0x148]);
// 0x43B4B0(stage, &node, actor) and [[a+0x68]+0xB0]+0x318 = stage (+1 reference, stage+0x16 = 1). It never touches
// comp+0x3D9 nor the AI's +0x66 (vf13 does), never sends, allocates only in 0x43B4B0's small vector (> 32 refs).
// guest_sched_bind calls it right after 0xA9FEE0 put a routed 2335's stage into X (0xC0B626, the same handler, the
// main thread), for the host character's NPCs of that scene whose vf13 found nothing (comp+0x318 == 0).
//
// The mode: guest_sched_bind needs guest_sched_route=1 (a stage in X to bind to); otherwise it is off (refused).
inline Mode bind_mode_of(Mode route, Mode bind, bool* refused = nullptr) {
    const bool r = bind != Mode::Off && route != Mode::On;
    if (refused) *refused = r;
    return r ? Mode::Off : bind;
}

// What the pre hook (0xC0B5C1) left for the point after 0xA9FEE0 (0xC0B626) of the SAME handler call: set on every
// 2335 that reaches 0xC0B5C1 (armed only when routed into X), consumed by 0xC0B626. The handler goes straight from
// one to the other (0xC0B5C1..0xC0B626: no branch, the only call is 0xA9FEE0); the frame and the scene key in it
// ([rbp-0x38]) must still match.
struct BindPending {
    bool armed = false;
    uint64_t rbp = 0;
    uint64_t key = 0;       // the 2335's scene key
    uint64_t x = 0;         // the X the stage went to
    uint32_t owner = 0;     // the character (the NPCs' comp+0x2D8)
    uint32_t resource = 0;  // the 2335's resource (log only)
    uint64_t m = 0;         // this client's own manager M as the handler's vf48 gave it (the retry's M+0x231 gate)
};
inline bool bind_pending_matches(const BindPending& p, uint64_t rbp, uint64_t frame_key) {
    return p.armed && p.key && p.x && p.owner && p.rbp == rbp && frame_key == p.key;
}

// One actor of the client table, as the walk read it (SEH reads).
struct BindActor {
    uint32_t key = 0;
    bool comp = false;      // [[a+0x68]+0xB0] is a ClientSequencerPlayDataActorComponent whose [comp+8] is the actor
    uint32_t owner = 0;     // comp+0x2D8 (dword: 0x9F30DF mov r8d,[rdi+0x2D8])
    uint64_t scene = 0;     // comp+0x2E0
    uint64_t bound = 0;     // comp+0x318
    bool ai_path = false;   // what 0x9F2FA0 dereferences unchecked is there: [a+0x88]; type 7: [[a+0x68]+0x30];
                            // else [[a+0x68]+0x58], its +0x90 and that +8
};
enum class BindWhy : uint8_t { Candidate, Self, NoComp, OtherOwner, OtherScene, AlreadyBound, NoAiPath, Count };
inline const char* bind_why_name(BindWhy w) {
    switch (w) {
        case BindWhy::Candidate: return "candidate (comp+0x318 0)";
        case BindWhy::Self: return "the character itself";
        case BindWhy::NoComp: return "no sequencer component";
        case BindWhy::OtherOwner: return "another owner";
        case BindWhy::OtherScene: return "another scene";
        case BindWhy::AlreadyBound: return "already bound (comp+0x318 != 0)";
        case BindWhy::NoAiPath: return "no AI node path (0x9F2FA0 would read a null pointer)";
        default: return "?";
    }
}
inline BindWhy bind_judge(const BindActor& a, uint32_t owner, uint64_t scene) {
    if (!a.key || a.key == owner) return BindWhy::Self;
    if (!a.comp) return BindWhy::NoComp;
    if (a.owner != owner) return BindWhy::OtherOwner;
    if (!scene || a.scene != scene) return BindWhy::OtherScene;
    if (a.bound) return BindWhy::AlreadyBound;
    if (!a.ai_path) return BindWhy::NoAiPath;
    return BindWhy::Candidate;
}

// After the call: comp+0x318 read back (and that stage's +0x30 / +0x28).
// Deferred (session 12): the NPC is not logged in yet (u16 actor+0x5E without 0x10, so the game's lookup 0x8AE1A0
// gives an empty holder, 0x8AE2C9): it goes to the retry queue (BindQueue) and is called later, once logged in.
enum class BindResult : uint8_t { Bound, StillZero, OtherStage, NotFound, Changed, Deferred, Count };
inline const char* bind_result_name(BindResult r) {
    switch (r) {
        case BindResult::Bound: return "BOUND to the stage in X";
        case BindResult::StillZero: return "still 0 (0x9F2FA0 returned early: node handle or owner not resolved)";
        case BindResult::OtherStage: return "bound to ANOTHER stage";
        case BindResult::NotFound: return "not called: the game's lookup 0x8AE1A0 did not give this actor";
        case BindResult::Changed: return "not called: the actor changed under its reference";
        case BindResult::Deferred:
            return "DEFERRED: it or its owner not logged in yet (+0x5E & 0x10 0: 0x8AE1A0 refuses it): queued for a retry";
        default: return "?";
    }
}
inline BindResult bind_result(uint64_t after, uint64_t after_key, uint64_t after_mgr, uint64_t scene, uint64_t x) {
    if (!after) return BindResult::StillZero;
    return after_key == scene && after_mgr == x ? BindResult::Bound : BindResult::OtherStage;
}
inline constexpr uint32_t kBindCap = 64;  // native calls per 2335 at most (the rest counted)

// The client actor table (game/actor_lookup.cpp; 0x464290 walks one bucket the same way): table +0x88 bucket count,
// +0x8C elements, +0x98 buckets (stride 0x100: u32 count, then {u32 key, u32 node index} x 31), +0xA0 node pointers
// (node +4 key, +8 actor). The walk only collects; every actor it yields is taken again with the game's own locked
// lookup (0x8AE1A0, a reference) before anything is called on it.
struct ActorTableLayout {
    static constexpr uint32_t kBucketCount = 0x88, kElements = 0x8C, kBuckets = 0x98, kNodes = 0xA0;
    static constexpr uint32_t kBucketStride = 0x100, kBucketMax = (0x100 - 8) / 8, kNodeKey = 0x04, kNodeActor = 0x08;
};
struct WalkLimits {
    uint32_t max_buckets = 1u << 16;
    uint32_t max_entries = 16384;
};
struct WalkStats {
    bool ok = false;
    bool truncated = false;  // max_entries reached
    uint32_t buckets = 0, entries = 0, actors = 0;
};
// Mem: bool u32(uint64_t, uint32_t&) const; bool ptr(uint64_t, uint64_t&) const. fn(key, actor) -> false stops.
template <class Mem, class Fn>
WalkStats walk_actor_table(const Mem& m, uint64_t table, const WalkLimits& lim, Fn&& fn) {
    using L = ActorTableLayout;
    WalkStats s;
    uint32_t buckets = 0;
    uint64_t base = 0, nodes = 0;
    if (!table || !m.u32(table + L::kBucketCount, buckets) || !buckets || buckets > lim.max_buckets ||
        !m.ptr(table + L::kBuckets, base) || !base || !m.ptr(table + L::kNodes, nodes) || !nodes) {
        return s;
    }
    s.ok = true;
    s.buckets = buckets;
    for (uint32_t b = 0; b < buckets; ++b) {
        const uint64_t bucket = base + static_cast<uint64_t>(b) * L::kBucketStride;
        uint32_t count = 0;
        if (!m.u32(bucket, count)) continue;
        if (count > L::kBucketMax) count = L::kBucketMax;
        for (uint32_t i = 0; i < count; ++i) {
            if (s.entries >= lim.max_entries) {
                s.truncated = true;
                return s;
            }
            ++s.entries;
            uint32_t key = 0, idx = 0, node_key = 0;
            uint64_t node = 0, actor = 0;
            if (!m.u32(bucket + 8 + i * 8, key) || !key || !m.u32(bucket + 0xC + i * 8, idx)) continue;
            if (!m.ptr(nodes + static_cast<uint64_t>(idx) * 8, node) || !node) continue;
            if (!m.u32(node + L::kNodeKey, node_key) || node_key != key) continue;
            if (!m.ptr(node + L::kNodeActor, actor) || !actor) continue;
            ++s.actors;
            if (!fn(key, actor)) return s;
        }
    }
    return s;
}

// The counts of one routed 2335 at the point (and their sums in the summary).
struct BindTally {
    uint32_t why[static_cast<size_t>(BindWhy::Count)] = {};
    uint32_t result[static_cast<size_t>(BindResult::Count)] = {};
    uint32_t capped = 0;       // candidates over kBindCap (not called this time)
    uint32_t bound_in_x = 0;   // AlreadyBound whose stage is the one in X (vf13 found it natively)
    uint32_t node_taken = 0;   // after a Bound: a node of the stage carries the NPC's key (0x43B4B0 bound it)
    uint32_t calls = 0;
};

// ---------------------------------------------------------------- guest_sched_bind's retry (session 12)
//
// Session 12 [V log + capstone]: the snapshot's 2335s are handled while the NPCs they concern are still loading
// (u16 actor+0x5E 0x08, +0xC2 0, +0xCF 0; the loading state's wait list had 184 of 194 not ready). 0x8AE1A0 gives
// an empty holder for an actor without +0x5E & 0x10 (0x8AE2C9 test byte [rax+0x5e],0x10 ; je): every one of the 196
// candidates was refused. 0x9F2FA0 looks its OWNER up the same way (and the owner was not logged in either during
// the load: vf13 of the same NPCs never got past its owner lookup), and the reference on the NPC is what keeps it
// alive across our call. So the NPC is queued here and called later from a main-thread point (the frame hook
// 0x8B6144 of guest_seq_play, else the next routed 2335), once it and its owner are logged in and the client's own
// load is over (the NPCs were ready ~2.7 s later in session 12, the load over ~12 s later).
struct BindRetry {
    uint32_t key = 0;       // the NPC
    uint32_t owner = 0;     // the character (comp+0x2D8)
    uint64_t scene = 0;     // comp+0x2E0 == the 2335's key
    uint64_t x = 0;         // the X the stage went to
    uint64_t since_us = 0;  // first queued
    uint32_t tries = 0;     // passes that found it (or its owner) not ready yet
    uint32_t still0 = 0;    // retried calls that left comp+0x318 at 0
    uint32_t refused = 0;   // retried calls whose 0x8AE1A0 gave no reference although +0x5E had 0x10
};
enum class RetryStep : uint8_t {
    Wait,            // not logged in yet (or its AI node path not there yet): stays queued
    Call,            // logged in, still a candidate, its stage still in X, the owner's X still X: 0x8AE1A0 + 0x9F2FA0
    BoundMeanwhile,  // comp+0x318 != 0 now (vf13 / the game bound it): dropped
    Gone,            // not in the client actor table any more: dropped
    Changed,         // another owner / scene / no component: dropped
    NoStage,         // the stage with that key is not in X any more (a 2126, X torn down): dropped
    OwnerMoved,      // the character's own X is not that X any more: dropped
    TimedOut,        // still queued kRetryTimeoutUs after it was first queued (ready or not): dropped
    Refused,         // 0x8AE1A0 kept refusing it (kRetryRefusedMax calls) although +0x5E had 0x10: dropped
    StillZero,       // 0x9F2FA0 kept leaving comp+0x318 at 0 (kRetryStillZeroMax calls): dropped
    Count
};
inline const char* retry_step_name(RetryStep s) {
    switch (s) {
        case RetryStep::Wait: return "waits (it or its owner not logged in yet, or the load not over)";
        case RetryStep::Call: return "called";
        case RetryStep::BoundMeanwhile: return "bound meanwhile (comp+0x318 set by the game)";
        case RetryStep::Gone: return "gone from the actor table";
        case RetryStep::Changed: return "changed (owner / scene / component)";
        case RetryStep::NoStage: return "its stage is no longer in X";
        case RetryStep::OwnerMoved: return "the character's X is no longer that X";
        case RetryStep::TimedOut: return "timed out";
        case RetryStep::Refused: return "0x8AE1A0 kept refusing it although logged in";
        case RetryStep::StillZero: return "0x9F2FA0 kept leaving comp+0x318 at 0";
        default: return "?";
    }
}
inline constexpr uint64_t kRetryTimeoutUs = 120'000'000;  // 2 min after it was first queued
inline constexpr uint64_t kRetryPeriodUs = 250'000;       // one pass every 250 ms at most
inline constexpr uint32_t kRetryCallsPerPass = 32;        // native calls per pass at most (the rest next pass)
inline constexpr uint32_t kRetryCap = 512;                // queued NPCs at most (196 in session 12)
// After a retried call that left comp+0x318 at 0 (0x9F2FA0 returned early: the owner or the node not resolved) or
// whose 0x8AE1A0 gave no reference, the NPC stays queued until this many such calls, then it is dropped.
inline constexpr uint32_t kRetryStillZeroMax = 3;
inline constexpr uint32_t kRetryRefusedMax = 3;

// What one pass read for one queued NPC.
// The OWNER matters as much as the NPC: 0x9F2FA0 looks the character up with the same 0x8AE1A0 (0x9F30DF..0x9F30FB)
// and returns early when it is not logged in (session 12: vf13 0x9ED290 of these 196 NPCs passed its node check and
// then never reached its owner probe 0x9ED453 during the load: the same early exit). So a call waits for both, and
// for this client's own load to be over (M+0x231 == 3, read at the pass: the frame hook's M or the 2335's M).
struct RetryFacts {
    bool table_ok = false;          // the pass read the whole actor table (else only the timeout applies)
    bool found = false;             // the NPC is in it
    bool owner_known = false;       // the pass knows the owner's actor (else no call and no OwnerMoved)
    BindActor actor;                // read_actor against the entry's owner / scene
    bool logged_in = false;         // the NPC: u16 +0x5E & 0x10 (the condition of 0x8AE1A0)
    bool owner_logged_in = false;   // the character: the same flag (0x9F2FA0's own lookup of it)
    bool load_done = false;         // this client's own M+0x231 == 3, read at the pass from ITS M (the frame hook's
                                    // RAX or the 2335's vf48 M; vtable checked): no M = not done (review of 51f9599)
    bool stage_in_x = false;        // a stage with the entry's key is in [X+0x148] (X's vtable checked)
    bool owner_x_ok = false;        // the character's own manager (as the table gives it) is the entry's X
};
inline bool retry_timed_out(const BindRetry& r, uint64_t now_us) {
    return now_us >= r.since_us && now_us - r.since_us >= kRetryTimeoutUs;
}
// Review of 51f9599: what no longer applies is dropped first, then the timeout (ready or not: a call that keeps
// failing must not keep an entry for ever), then the call. Without a whole table or a known owner only the timeout
// can drop an entry.
inline RetryStep retry_judge(const BindRetry& r, const RetryFacts& f, uint64_t now_us) {
    const bool timed_out = retry_timed_out(r, now_us);
    if (!f.table_ok) return timed_out ? RetryStep::TimedOut : RetryStep::Wait;
    if (!f.found) return RetryStep::Gone;
    const BindWhy why = bind_judge(f.actor, r.owner, r.scene);
    if (why == BindWhy::AlreadyBound) return RetryStep::BoundMeanwhile;
    if (why != BindWhy::Candidate && why != BindWhy::NoAiPath) return RetryStep::Changed;
    if (!f.stage_in_x) return RetryStep::NoStage;
    if (f.owner_known && !f.owner_x_ok) return RetryStep::OwnerMoved;
    if (timed_out) return RetryStep::TimedOut;
    if (f.owner_known && why == BindWhy::Candidate && f.logged_in && f.owner_logged_in && f.load_done) {
        return RetryStep::Call;
    }
    return RetryStep::Wait;
}
// After a retried call: true = keep it queued (counts the refusal / the still-0), false = drop it with `drop`.
inline bool retry_keep_after(BindRetry& r, BindResult res, RetryStep& drop) {
    drop = RetryStep::Call;
    if (res == BindResult::NotFound) {
        if (++r.refused < kRetryRefusedMax) return true;
        drop = RetryStep::Refused;
        return false;
    }
    if (res == BindResult::StillZero) {
        if (++r.still0 < kRetryStillZeroMax) return true;
        drop = RetryStep::StillZero;
        return false;
    }
    return false;  // Bound, OtherStage, Changed: done
}
inline bool retry_due(uint64_t now_us, uint64_t last_us) { return !last_us || now_us - last_us >= kRetryPeriodUs; }
// Review of 51f9599: the calls of a pass start where the previous pass stopped (a fixed start let 32 failing entries
// take every pass's budget). The i-th entry visited of n, starting at `start`.
inline uint32_t retry_rotated(uint32_t start, uint32_t n, uint32_t i) { return n ? (start % n + i) % n : 0; }

// The queue (main thread only; fixed storage, no allocation). One entry per NPC key: queuing a key again updates
// its scene / X (a newer 2335) and keeps its first time.
template <uint32_t N>
struct BindQueue {
    BindRetry e[N];
    uint32_t n = 0;
    uint64_t full = 0;  // refused: the queue was full
    int index_of(uint32_t key) const {
        for (uint32_t i = 0; i < n; ++i) {
            if (e[i].key == key) return static_cast<int>(i);
        }
        return -1;
    }
    bool add(const BindRetry& r) {
        if (!r.key) return false;
        if (const int i = index_of(r.key); i >= 0) {
            e[i].owner = r.owner;
            e[i].scene = r.scene;
            e[i].x = r.x;
            return true;
        }
        if (n >= N) {
            ++full;
            return false;
        }
        e[n++] = r;
        return true;
    }
    void remove_at(uint32_t i) {
        if (i >= n) return;
        e[i] = e[n - 1];
        --n;
    }
    // Removes every entry whose mark (indexed as the entries are now) is set; the others keep their data.
    void remove_marked(const bool* mark) {
        for (uint32_t i = n; i-- > 0;) {
            if (mark[i]) remove_at(i);  // the entry moved into i comes from above: already kept
        }
    }
};

// ---------------------------------------------------------------- G5 (the watched NPCs)

// One read of a watched actor (diagnostics thread, SEH reads; the tests by hand).
struct NpcState {
    bool exists = false;
    uint8_t state = 0;    // +0x5E (0x10 = logged in, 0x8B53C0)
    uint8_t ready_c2 = 0, ready_cf = 0;  // +0xC2, +0xCF (the login finished, 0x43B4B0)
    bool comp = false;    // [[actor+0x68]+0xB0] is a ClientSequencerPlayDataActorComponent
    uint32_t owner = 0;   // comp+0x2D8 (low 32: the sequencer owner's key)
    uint64_t scene = 0;   // comp+0x2E0
    // guest_seq_play (p8-xtls-lectura.md §2): the login flag +0xCF waits for vf88 0x9EE850 = comp+0x3D8 of the
    // topmost parent found through actor+0xAC; 0x602940 (the instance's start) / .xtls 0x8F80300 (hooked onto a
    // live scene) put it to 0 when comp+0x2E0 == the instance's +0x30.
    uint8_t flag_3d8 = 0;   // comp+0x3D8 (1 = still waits for its scene)
    uint64_t link_320 = 0;  // comp+0x320 (!= 0: bound; vf78 then sets +0x3D8 = 1 at creation)
    uint32_t parent = 0;    // actor+0xAC (0 = none)
    bool parent_found = false;
    uint8_t parent_flag = 0;  // the parent's comp+0x3D8 (when found)
    // guest_sched_route (p8-impl-horarios-x.md §5): what vf13 0x9ED290 bound. comp+0x318 = the stage it found in
    // [X+0x148] (0 = never bound: vf13 runs once per unpack); comp+0x3D9 = 1 when that stage was not active yet.
    uint64_t stage = 0;       // comp+0x318
    uint8_t flag_3d9 = 0;     // comp+0x3D9
    uint8_t stage_active = 0; // stage+0x178
    uint64_t stage_key = 0;   // stage+0x30
    uint64_t stage_mgr = 0;   // stage+0x28 (X or M)
    bool operator==(const NpcState&) const = default;
};

// G5 for the host character's manager X (diagnostics thread). Lists are {ptr, u32 count @+8}.
struct XState {
    bool readable = false;  // X found, vtable checked
    uint8_t ready = 0;      // +0x231 (2 after the copied 2322; guest_seq_play writes 3)
    uint32_t loading = 0;   // +0x174 (SequencerLoadingTask in flight; 0xA97300 does nothing while != 0)
    uint32_t created = 0;   // +0x17C
    uint32_t n1b0 = 0;      // +0x1B8: loaded, waiting for the tick
    uint32_t n1c0 = 0;      // +0x1C8: preloaded (state 1/2), played by the tick in state 3
    uint32_t n1d0 = 0;      // +0x1D8: loaded with [[play]+0x60] == 3 (played at once by the tick)
    uint32_t live = 0;      // +0x30: the live scenes vector +0x28 (0xAA3E70 walks it)
    uint32_t engage = 0;    // +0x194: 0xAA3E70's queue of actors waiting to hook onto a live scene
    bool operator==(const XState&) const = default;
};


// ---------------------------------------------------------------- the 2524 of a WORLD scene (session 13)
//
// p8-impl-secuenciador-bodyanim.md, family (d) of p8-s12-npc-familias.md §5: the host's sequencer NPCs (Stranger
// 0xB0100077/78 of 0x19, guards 0x7E/7F/80 of 0x16C, DF4-DF9 of 0x1BD/0x1BE...) stand in the idle wait on the guest:
// their SeqBodyAnim graph (CC+0x70/+0x78) is only built by the scene event (2524 0xC07D00 -> 0xA9E480 -> ... ->
// 0xADED90 -> BodyAnim 0xA79770), and every 2524 of them went to M (session 12: 117 x 2524, not routed). Session 11
// phase 2 routed EVERY 2524 into X (with guest_seq_play_live) and the picture got worse: so only the 2524s of world
// scenes, and first in probe.
//
// [session2] guest_seq_route_2524 = 0 | probe | 1 (independent of guest_seq_play_live):
//   0      as before: the 2524 point only with guest_seq_play_live (legacy, whatever it routes there);
//   probe  the point (and the inner check 0xA9E55B) installed; every 2524 classified and judged, nothing routed;
//   1      routed into X only when classify_2524 says World and the existing checks pass (X ticked by guest_seq_play,
//          pre_2524, the inner check under [G+8]'s lock).
// guest_seq_route_2524_allow = <hex resources> (optional): with 1, also only those 2661 resources (empty = every World).
enum class Mode2524 : uint8_t { Legacy, Probe, On };
inline Mode2524 parse_mode_2524(std::string_view v, bool* bad = nullptr) {
    switch (parse_mode(v, bad)) {
        case Mode::Probe: return Mode2524::Probe;
        case Mode::On: return Mode2524::On;
        default: return Mode2524::Legacy;
    }
}
// The mode a 2524 is judged in. route: guest_seq_route's own mode (Off: nothing at all); legacy_routes: the old
// switch (guest_seq_play=1 with _live and its skip hook, guest_seq_play_routes_2524); play_ticks: guest_seq_play is
// 1 (not probe) and not switched off (guest_seq_play_ticks). Review of 5f30a52: a 2524 routed into X needs X's tick;
// guest_seq_play_knows_x is true in probe too, so guest_seq_route_2524=1 alone is not enough.
inline Mode mode_2524(Mode route, Mode2524 m2524, bool legacy_routes, bool play_ticks) {
    if (route == Mode::Off) return Mode::Off;
    if (m2524 == Mode2524::Legacy) return route == Mode::On && legacy_routes ? Mode::On : Mode::Probe;
    if (m2524 == Mode2524::Probe) return Mode::Probe;
    return route == Mode::On && play_ticks ? Mode::On : Mode::Probe;  // 1: routed only with guest_seq_route=1 too
}
// The 2524 point (0xC08178) is installed for the legacy switch (want_2524) or for guest_seq_route_2524 != 0.
inline bool want_point_2524(bool legacy_want, Mode2524 m2524) { return legacy_want || m2524 != Mode2524::Legacy; }
// The world classification applies only with guest_seq_route_2524 = probe | 1 (the legacy path is left unchanged).
inline bool need_world_2524(Mode2524 m2524) { return m2524 != Mode2524::Legacy; }

inline constexpr uint8_t kActorTypeNpc = 3;  // [[actor+0x88]+1] (guest_sched_bind's "actor type 3", session 12)

// What a 2524's scene is, from its 2661 (SeqResources) and the hook's SEH reads.
struct Scene2524Facts {
    bool known = false;        // its 2661 came (resource and binds noted)
    bool denied = false;       // the resource is on guest_seq_route_deny
    uint32_t resource = 0;
    uint8_t binds = 0;         // keys its 2661 binds (the host character included)
    uint8_t npcs = 0;          // of them NPC keys (0xB...)
    uint8_t npcs_found = 0;    // of those, in this client's actor table now
    uint8_t type3 = 0;         // of those, [[actor+0x88]+1] == 3
    bool name_ok = false;      // the scene's name read and checked (its key == the sequencer)
    bool function = false;     // its name is a shop / function scene (cd_seq_funcnpc_*)
    bool allow_empty = true;   // guest_seq_route_2524_allow is empty
    bool allowed = false;      // the resource is on guest_seq_route_2524_allow
};
enum class World2524 : uint8_t {
    World,        // NPCs of type 3, not a function scene, name read (and allowed): routable
    UnknownSeq,   // no 2661 of it seen on this client (prepared before, or evicted)
    Denied,       // on guest_seq_route_deny (cutscene / field reload)
    NoNpc,        // binds no NPC: the host's own / personal scene (0x164-0x167 bind only 0xA0100001)
    NoNpcHere,    // its NPCs are not in this client now
    NoType3,      // none of its NPCs present is of type 3 (fauna, pets, named sequencer NPCs only)
    NameUnknown,  // the name could not be read / checked: fail closed (could be a shop)
    Function,     // cd_seq_funcnpc_* (shop / function NPC scenes: their dialogue is the host's)
    NotAllowed,   // a world scene whose resource is not on guest_seq_route_2524_allow
    Count
};
inline const char* world_2524_name(World2524 w) {
    switch (w) {
        case World2524::World: return "WORLD";
        case World2524::UnknownSeq: return "unknown sequencer (no 2661 seen)";
        case World2524::Denied: return "denied (guest_seq_route_deny)";
        case World2524::NoNpc: return "no NPC bound (host's own / personal)";
        case World2524::NoNpcHere: return "its NPCs are not in this client";
        case World2524::NoType3: return "no NPC of type 3";
        case World2524::NameUnknown: return "name not read (fail closed)";
        case World2524::Function: return "function scene (cd_seq_funcnpc_*)";
        case World2524::NotAllowed: return "world, NOT on guest_seq_route_2524_allow";
        default: return "?";
    }
}
inline World2524 classify_2524(const Scene2524Facts& s) {
    if (!s.known) return World2524::UnknownSeq;
    if (s.denied) return World2524::Denied;
    if (!s.npcs) return World2524::NoNpc;
    if (!s.npcs_found) return World2524::NoNpcHere;
    if (!s.type3) return World2524::NoType3;
    if (!s.name_ok) return World2524::NameUnknown;
    if (s.function) return World2524::Function;
    if (!s.allow_empty && !s.allowed) return World2524::NotAllowed;
    return World2524::World;
}
// Shop / function NPC scenes (p8-s12-tenderos.md: cd_seq_funcnpc_butcher 0x197, _seatdeale 0x198, _grocery 0x199,
// _smithy 0x19A, _general 0x19B, _bank 0x1A4, _pub 0x1A5, _contribut 0x1B2, _clothes 0x1BF, _craftsmit 0x1C0).
inline bool is_function_scene(std::string_view name) {
    constexpr std::string_view kPrefix = "cd_seq_funcnpc";
    if (name.size() < kPrefix.size()) return false;
    for (size_t i = 0; i < kPrefix.size(); ++i) {
        const char c = name[i] >= 'A' && name[i] <= 'Z' ? static_cast<char>(name[i] - 'A' + 'a') : name[i];
        if (c != kPrefix[i]) return false;
    }
    return true;
}
// A scene name read raw (up to 8 * nq bytes): printable ASCII up to the first NUL. False when empty or when a
// non-printable byte comes before the NUL (not a name: the pointer chain was wrong).
inline bool scene_name(const uint64_t* q, size_t nq, char* out, size_t cap) {
    if (!cap) return false;
    size_t n = 0;
    bool ok = true;
    for (size_t i = 0; i < nq * 8 && n + 1 < cap; ++i) {
        const auto c = static_cast<unsigned char>(q[i / 8] >> (8 * (i % 8)));
        if (c == 0) break;
        if (c < 0x20 || c >= 0x7F) ok = false;
        out[n++] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
    }
    out[n] = 0;
    return ok && n > 0;
}
// X's instance of a sequencer, as 0x1F6C190 keys it (read-only peek): count [G+0x10C], nodes [G+0x120][i],
// node +8 = sequencer id, +0x10 = the instance, instance +0x15 = dead [V capstone 0x1F6C190, 10-10]. The game walks
// one hash bucket ([G+0x118] x [G+0x108]); the peek walks the dense node array (b728ec7's find_instance).
inline constexpr uint32_t kMgr_MapCount = 0x10C, kMgr_MapNodes = 0x120, kMapNode_Seq = 0x08, kMapNode_Inst = 0x10,
                          kInst_Dead = 0x15;
inline constexpr uint32_t kMaxMapNodes = 4096;
// The scene's name candidates: [[obj+0x48]+0x1B8] for the object whose +0x30 is the sequencer id (guest_x_veto's
// stage layout: proc+0x10 = the stage, +0x30 its key, +0x48 its info, name at +0x1B8; session 12:
// 'cd_seq_funcnpc_butcher' key 0x197). Tried on the instance first, then on [inst+0x58] (0xA9E480's stage). [E] which
// of the two is that stage is checked at run time by the key; the lines say which one matched (or none).
inline constexpr uint32_t kStage_KeyOff = 0x30, kStage_InfoOff = 0x48, kInfo_NameOff = 0x1B8, kInst_StageOff = 0x58;
enum class NameVia : uint8_t { None, Instance, Stage58 };
inline const char* name_via_name(NameVia v) {
    return v == NameVia::Instance ? "inst" : v == NameVia::Stage58 ? "[inst+0x58]" : "none";
}

}  // namespace cdmp::session2::gsr
