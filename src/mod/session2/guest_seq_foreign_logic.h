#pragma once
// S44 (docs/research/s25/s44-escenas-del-anfitrion.md), GUEST: the host's scenes on the guest's client, root fix.
// The pure logic (no game access): the 2661's bindings as 0xC05BB0 holds them, the per-binding decision of
// guest_seq_foreign_player = 0 | probe | 1, and the classification of the read-only probes
// guest_seq_foreign_probe = 0 | 1 (P1 0x602940, P2 0x601470 / 0x601C10, P3 0x6034FA).
//
// Why [V, build 2976, s44-datos/nativo.md]: the native client creates every 2661 with kind 4 in the field owner's manager
// and links each binding of type 4/5 to its actor (0xA9CA40 -> 0x8AF170). The .paseq's player node
// (ActorTrackNodeData_CurrentControl, node+0xAC == 0) is assumed to be the LOCAL player: ClientActor::vf178 0x4FE260
// tells it, and when it is not (the copy of the host's Kliff on the guest) the engine treats it as an NPC: alignment
// 0x602940 -> 0x6030D0 (TransformSync vf1A0 at 0x603522) and SequencerControl_Input/_AI/_Off through vf21 0x601470 /
// vf22 0x601C10 -> 0x9F8980 -> the per-actor control in .xtls. Every one of those targets is resolved by 0x205DC30,
// which only yields an actor for a binding of type 5 and does not even count a binding of type 0 (only 4 -> 5 is ever
// promoted, 0x205B4D0). The copy then stays frozen in a pose.
//
// The design principle: on the guest's client, ANOTHER player's character is never moved by a scene; its authoritative
// state comes over the network from its own client (which plays its personal scenes for real). So in every scene whose
// owner is another player and that goes to X (guest_seq_route's Route: owner != this client's character), the binding
// to that player (the scene's owner) is neutralized (type 5/4 -> 0) in 0xC05BB0's own binding vector, before 0xA9CA40
// reads it. No name lists: every host scene alike; NPC, gimmick and shop bindings untouched.
//
// Review S44 H-1 [V capstone]: type 0 is NOT inert. It is the native "unbound node" state: 0x5EB310 (the stage order that
// builds a CreateClientActorDesc_Sequencer) goes on for a binding of type {0,3,6,7} and refuses {1,2,4,5}
// (0x5EB41F bt 0xC9), and 0x205E520 has a branch of its own for 0 (0x205E6AF). And the owner is not always at the
// player node (weather / fauna bind it at 0x20000001, memory / quest at 0x20000003). So with 1 the owner's binding is
// neutralized by default from its first appearance, and a learnt map holds the EXCEPTIONS: a (resource, node id) P1 has
// seen with the owner at a node that is not CurrentControl (node+0xAC != 0) is never touched again (S44 live: the first
// boss-memory rematch stayed frozen while waiting to be learnt; the 133 pairs learnt in two sessions were all
// CurrentControl, P4 none). No other value is
// inert in every .rsrc reader either (1 / 2 make 0x20532B0 take its actor-bound path, s44-escenas-del-anfitrion.md §2.3).

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace cdmp::session2::gsf {

enum class Mode : uint8_t { Off, Probe, On };
inline Mode parse_mode(std::string_view v, bool* bad = nullptr) {
    if (bad) *bad = false;
    if (v.empty() || v == "0") return Mode::Off;
    if (v == "probe") return Mode::Probe;
    if (v == "1") return Mode::On;
    if (bad) *bad = true;
    return Mode::Off;
}
inline const char* mode_name(Mode m) { return m == Mode::On ? "1" : m == Mode::Probe ? "probe" : "0"; }

inline uint32_t le32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}
inline constexpr uint32_t kCharClass = 0xA;
inline bool is_character(uint32_t key) { return key && (key >> 28) == kCharClass; }
// Binding types (0xA9CA40 links 4 / 5 to an actor; 0x205B4D0 turns 4 into 5; 0x205DC30 resolves only 5).
inline constexpr uint8_t kLinkNone = 0, kLinkPending = 4, kLinkActive = 5;
// The node type (node+0xAC, nativo.md §1.3): 0 CurrentControl (the player), 1 fixed actor, 2 SpawnCharacter, 4 PivotActor.
// The learnt map's own values: unknown (not seen by P1 yet) and mixed (two different types seen: never applied).
inline constexpr uint8_t kNodeCurrentControl = 0, kNodeUnknown = 0xFF, kNodeMixed = 0xFE;

// guest_seq_route_hook's call (review H-6): only a 2661 with its raw packet and resource read, routed to X or would be.
inline bool wants_2661(bool raw, bool resource_known, bool routed, bool would_route) {
    return raw && resource_known && (routed || would_route);
}

// ---------------------------------------------------------------- the 2661 on the wire [V captures]
//
// u8 kind | u32 actor @1 | 86-byte play data @5 (owner @5 = pd+0, sequencer @9 = pd+8, pd+0x10/+0x11 @17/@18, stage
// @19, resource @27) | n x 11 bytes of binding @91: u32 node | u32 actor | u8 type | u8 | u8.
inline constexpr size_t kPrefix = 91, kWire = 11;
inline constexpr size_t kKindAt = 0, kActorAt = 1, kOwnerAt = 5, kSeqAt = 9, kB17At = 17, kB18At = 18, kStageAt = 19,
                        kResourceAt = 27;

// The handler's own binding vector (0xC05BB0, [V capstone build 2976]):
//   0xC05E12 / 0xC05E19 call the allocator (n * 12 bytes, this thread's local or the global heap)
//   0xC05E44 mov [rbp-0x80],r14 (the pointer) ; mov [rbp-0x74],ebx (the capacity)
//   0xC05E60 each entry initialized {u32 -1, u32 0, u16 7, u8 0} (stride 0xC)
//   0xC05E84 mov [rbp-0x78],ebx (the count)
//   0xC05EA0 lea rcx,[rax+rax*2] ; lea rbx,[rsi+rcx*4]: entry i at ptr + 12 i, read field by field from the packet:
//            +0 u32 node (4 B), +4 u32 actor (4 B), +8 u8 type, +9 u8, +0xA u8 (0xC05EC3 / 0xC05EE9 / 0xC05F1D /
//            0xC05F51 / 0xC05F85)
//   0xC06179 lea r9,[rbp-0x80] ; lea r8,[rbp] ; ... call 0xA9CA40 (the vector by address, the play data at [rbp])
//   0xA9CB2E mov r14,[rsi] ; mov eax,[rsi+8] ; ... each entry COPIED (0xA9CC28 vmovsd [r14] ; 0xA9CC32 mov eax,[r14+8])
//            into the loading task's own entries (task+0xA8, 0x30 B); 0xA9CB87 movzx eax,byte [r14+8] ; cmp al,4 ; je ;
//            cmp al,5 ; jne 0xA9CC20: only types 4/5 get an actor reference (0x8AF170); any other type is stored
//            without one
//   0xC0618D mov rsi,[rbp-0x80] ... 0xC061EF test rsi,rsi ; ... 0xC0620E / 0xC06222 free it (this call)
// So the vector is memory of this handler call only: allocated, filled, passed by address, copied and freed inside
// 0xC05BB0's frame. The raw packet ([R12+0x18]) is never written.
inline constexpr size_t kEntrySize = 12, kEntryNode = 0, kEntryActor = 4, kEntryType = 8;
inline constexpr int32_t kVecPtrSlot = -0x80, kVecCountSlot = -0x78, kVecCapSlot = -0x74;  // rbp-relative
inline constexpr uint32_t kMaxEntries = 64;  // the inventory's largest 2661 binds 24; more is refused (nothing touched)

struct Entry {
    uint32_t node = 0;
    uint32_t actor = 0;
    uint8_t type = 0;
    bool operator==(const Entry&) const = default;
};

struct Prepare {
    uint8_t kind = 0;
    uint32_t actor = 0;
    uint32_t owner = 0;
    uint64_t seq = 0;
    uint8_t b17 = 0, b18 = 0;
    uint64_t stage = 0;
    uint32_t resource = 0;
    std::vector<Entry> binds;
};
inline std::optional<Prepare> parse_2661(std::span<const uint8_t> p) {
    if (p.size() < kPrefix || (p.size() - kPrefix) % kWire != 0) return std::nullopt;
    Prepare r;
    r.kind = p[kKindAt];
    r.actor = le32(&p[kActorAt]);
    r.owner = le32(&p[kOwnerAt]);
    std::memcpy(&r.seq, &p[kSeqAt], 8);
    r.b17 = p[kB17At];
    r.b18 = p[kB18At];
    std::memcpy(&r.stage, &p[kStageAt], 8);
    r.resource = le32(&p[kResourceAt]);
    const size_t n = (p.size() - kPrefix) / kWire;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* b = &p[kPrefix + i * kWire];
        r.binds.push_back(Entry{le32(b), le32(b + 4), b[8]});
    }
    return r;
}
// The vector 0xC05BB0 builds from those bindings (tests: what the hook finds at [rbp-0x80]).
inline std::vector<uint8_t> handler_vector(std::span<const uint8_t> payload) {
    std::vector<uint8_t> v;
    if (payload.size() < kPrefix || (payload.size() - kPrefix) % kWire != 0) return v;
    const size_t n = (payload.size() - kPrefix) / kWire;
    v.assign(n * kEntrySize, 0);
    for (size_t i = 0; i < n; ++i) {
        // 11 wire bytes -> +0..+0xA of the 12-byte entry (the 12th byte keeps the initializer's 0)
        std::memcpy(&v[i * kEntrySize], &payload[kPrefix + i * kWire], kWire);
    }
    return v;
}
inline Entry entry_at(std::span<const uint8_t> vec, size_t i) {
    const uint8_t* b = &vec[i * kEntrySize];
    return Entry{le32(b + kEntryNode), le32(b + kEntryActor), b[kEntryType]};
}

// ---------------------------------------------------------------- guest_seq_foreign_player (the fix)

// Per binding: only the scene owner's binding, only when that owner is ANOTHER player's character, only types 4/5.
enum class Bind : uint8_t {
    Keep,         // not the owner (an NPC, a gimmick, any other key): never touched
    OwnerOther,   // the owner with a type that is neither 4 nor 5: left alone, counted
    OwnerLinked,  // the owner with type 5 (active) or 4 (pending, 0x205B4D0 would make it 5): to neutralize
};
inline Bind judge_bind(const Entry& e, uint32_t owner, uint32_t own_key, uint32_t m_key) {
    if (!is_character(owner) || e.actor != owner) return Bind::Keep;
    if (e.actor == own_key || e.actor == m_key) return Bind::Keep;  // never this client's own character
    return e.type == kLinkActive || e.type == kLinkPending ? Bind::OwnerLinked : Bind::OwnerOther;
}

enum class Verdict : uint8_t {
    Off,             // guest_seq_foreign_player=0 (exactly as before S44)
    NotX,            // guest_seq_route did not send it to X (own scene, NPC key, refused, denied...): not judged
    LayoutUnverified,// the handler's bytes were not build 2976's at install: nothing read nor written
    OwnScene,        // the owner is this client's character (or M's): never touched
    OwnerNotPlayer,  // the owner is not a character key (never seen: 0 of 1 635): not touched
    OwnerMismatch,   // play data +0 ([rbp]) != the packet's owner @5: not touched (the frame is not what was verified)
    Unreadable,      // vector pointer / count not usable (count > kMaxEntries, null pointer, a read failed)
    NoOwnerBind,     // a scene of X with no binding to its owner (nothing to do; counted)
    OwnerOtherType,  // the owner is bound, but with a type other than 4/5 (nothing touched)
    Would,           // probe, or guest_seq_route=probe (the scene goes to M): what 1 would do, nothing written
    NotPlayerNode,   // 1: the owner sits at a node P1 saw NOT to be CurrentControl (or mixed): the exception, never touched
    Applied,         // 1 and Route: the owner's binding type(s) set to 0 in the handler's vector (not a learnt exception)
    Count
};
inline const char* verdict_name(Verdict v) {
    switch (v) {
        case Verdict::Off: return "off";
        case Verdict::NotX: return "not routed to X";
        case Verdict::LayoutUnverified: return "handler layout NOT verified (nothing read / written)";
        case Verdict::OwnScene: return "own scene (never touched)";
        case Verdict::OwnerNotPlayer: return "owner is not a player character (not touched)";
        case Verdict::OwnerMismatch: return "play data owner != packet owner (not touched)";
        case Verdict::Unreadable: return "binding vector unreadable / over the limit (not touched)";
        case Verdict::NoOwnerBind: return "scene of X with NO binding to its owner (nothing to do)";
        case Verdict::OwnerOtherType: return "owner bound with a type other than 4/5 (not touched)";
        case Verdict::Would: return "WOULD neutralize the owner's binding (probe)";
        case Verdict::NotPlayerNode: return "owner at a NON-player node (learnt by P1): never touched";
        case Verdict::Applied: return "owner's binding NEUTRALIZED (type -> 0)";
        default: return "?";
    }
}

struct Facts {
    Mode mode = Mode::Off;
    bool layout_ok = false;   // install verified the handler's / 0xA9CA40's bytes
    bool routed = false;      // guest_seq_route: Verdict::Route (RAX := X)
    bool would_route = false; // guest_seq_route: Verdict::WouldRoute (probe: the scene goes to M)
    uint32_t owner = 0;       // the packet's owner @5
    uint32_t owner_pd = 0;    // u32 [rbp] (the play data the handler passes to 0xA9CA40)
    uint32_t own_key = 0;     // this client's character (its 2756; 0 = unknown)
    uint32_t m_key = 0;       // M's character (the field owner's on this client)
    uint64_t vec = 0;         // [rbp-0x80]
    uint32_t count = 0;       // [rbp-0x78]
    uint32_t capacity = 0;    // [rbp-0x74]
    bool read_ok = true;      // every entry could be read
};
inline constexpr size_t kMaxPlan = 8;
struct Plan {
    Verdict verdict = Verdict::Off;
    uint32_t owner_entries = 0;   // bindings to the owner of type 4/5
    uint32_t owner_other = 0;     // bindings to the owner of another type
    uint32_t others = 0;          // bindings left alone (NPC, gimmick, ...)
    uint32_t unknown = 0;         // owner bindings whose node P1 has not seen yet (written all the same)
    uint32_t not_player = 0;      // owner bindings at a learnt non-player (or mixed) node
    uint8_t n = 0;                // owner bindings of type 4/5 (candidates)
    uint8_t idx[kMaxPlan] = {};   // their entry indices
    uint8_t was[kMaxPlan] = {};   // their type before (4 / 5)
    uint8_t node[kMaxPlan] = {};  // their node type as learnt (kNodeUnknown / kNodeMixed / node+0xAC)
    uint8_t write_mask = 0;       // with Applied: the candidates actually written (all but the learnt exceptions)
    bool write() const { return verdict == Verdict::Applied; }
    bool writes(uint8_t k) const { return write() && k < n && ((write_mask >> k) & 1); }
};

// entries: the n entries read from the vector (n == f.count when f.read_ok). node_types: per entry, the node type P1
// learnt for (resource, entry node) (kNodeUnknown when not learnt; may be empty = all unknown). Only a learnt non-player
// (or mixed) node is an exception; unknown and CurrentControl are written (S44 live).
inline Plan plan(const Facts& f, std::span<const Entry> entries, std::span<const uint8_t> node_types = {}) {
    Plan p;
    if (f.mode == Mode::Off) return p;
    if (!f.routed && !f.would_route) {
        p.verdict = Verdict::NotX;
        return p;
    }
    if (!f.layout_ok) {
        p.verdict = Verdict::LayoutUnverified;
        return p;
    }
    if (f.owner && (f.owner == f.own_key || f.owner == f.m_key)) {
        p.verdict = Verdict::OwnScene;
        return p;
    }
    if (!is_character(f.owner)) {
        p.verdict = Verdict::OwnerNotPlayer;
        return p;
    }
    if (f.owner_pd != f.owner) {
        p.verdict = Verdict::OwnerMismatch;
        return p;
    }
    if (!f.read_ok || f.count > kMaxEntries || (f.count && !f.vec) || f.count > f.capacity ||
        entries.size() != f.count) {
        p.verdict = Verdict::Unreadable;
        return p;
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        switch (judge_bind(entries[i], f.owner, f.own_key, f.m_key)) {
            case Bind::OwnerLinked:
                ++p.owner_entries;
                if (p.n < kMaxPlan) {
                    p.idx[p.n] = static_cast<uint8_t>(i);
                    p.was[p.n] = entries[i].type;
                    p.node[p.n] = i < node_types.size() ? node_types[i] : kNodeUnknown;
                    ++p.n;
                }
                break;
            case Bind::OwnerOther: ++p.owner_other; break;
            default: ++p.others; break;
        }
    }
    if (!p.owner_entries) {
        p.verdict = p.owner_other ? Verdict::OwnerOtherType : Verdict::NoOwnerBind;
        p.n = 0;
        return p;
    }
    if (p.owner_entries > kMaxPlan) {  // never seen (the owner is bound at most once): all or nothing, so nothing
        p.verdict = Verdict::Unreadable;
        p.n = 0;
        return p;
    }
    for (uint8_t k = 0; k < p.n; ++k) {
        if (p.node[k] == kNodeCurrentControl || p.node[k] == kNodeUnknown) {
            p.write_mask = static_cast<uint8_t>(p.write_mask | (1u << k));
            p.unknown += p.node[k] == kNodeUnknown;
        } else {
            ++p.not_player;
        }
    }
    if (f.mode != Mode::On || !f.routed) {
        p.verdict = Verdict::Would;
        p.write_mask = 0;
        return p;
    }
    p.verdict = p.write_mask ? Verdict::Applied : Verdict::NotPlayerNode;
    return p;
}

// The write itself on a vector (tests; the hook does the same entry by entry with SEH writes, each one re-checked).
inline size_t apply(std::span<uint8_t> vec, const Plan& p) {
    size_t done = 0;
    for (uint8_t k = 0; k < p.n; ++k) {
        if (!p.writes(k)) continue;
        const size_t at = static_cast<size_t>(p.idx[k]) * kEntrySize + kEntryType;
        if (at >= vec.size() || vec[at] != p.was[k]) continue;
        vec[at] = kLinkNone;
        ++done;
    }
    return done;
}

// ---------------------------------------------------------------- the learnt node map (review H-1 b)
//
// (resource, node id) -> node type, written by P1 (0x602940, any thread) and read by the 2661 hook (only its non-player
// entries matter: they are the exceptions); lock-free open
// addressing, bounded probes. A pair that shows two different node types becomes kNodeMixed for good (never applied).
template <size_t N>
class NodeMap {
    static_assert(N >= 16 && (N & (N - 1)) == 0, "N must be a power of two");

public:
    static constexpr size_t kProbe = 16;
    NodeMap() {
        for (auto& v : vals_) v.store(kNodeUnknown, std::memory_order_relaxed);
    }
    NodeMap(const NodeMap&) = delete;
    NodeMap& operator=(const NodeMap&) = delete;
    static uint64_t key(uint32_t resource, uint32_t node) { return static_cast<uint64_t>(resource) << 32 | node; }
    // false: not noted (bad input, or the map is full around that key).
    bool note(uint32_t resource, uint32_t node, uint8_t type) {
        if (!resource || !node || type >= kNodeMixed) return false;
        const uint64_t k = key(resource, node);
        const size_t h = hash(k);
        for (size_t i = 0; i < kProbe; ++i) {
            const size_t s = (h + i) & (N - 1);
            uint64_t cur = keys_[s].load(std::memory_order_acquire);
            if (cur == 0) {
                if (keys_[s].compare_exchange_strong(cur, k, std::memory_order_acq_rel)) {
                    used_.fetch_add(1, std::memory_order_relaxed);
                    cur = k;
                }
            }
            if (cur != k) continue;
            uint8_t old = vals_[s].load(std::memory_order_acquire);
            for (;;) {
                const uint8_t want = old == kNodeUnknown || old == type ? type : kNodeMixed;
                if (want == old || vals_[s].compare_exchange_weak(old, want, std::memory_order_acq_rel)) return true;
            }
        }
        full_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    uint8_t find(uint32_t resource, uint32_t node) const {
        if (!resource || !node) return kNodeUnknown;
        const uint64_t k = key(resource, node);
        const size_t h = hash(k);
        for (size_t i = 0; i < kProbe; ++i) {
            const size_t s = (h + i) & (N - 1);
            const uint64_t cur = keys_[s].load(std::memory_order_acquire);
            if (cur == k) return vals_[s].load(std::memory_order_acquire);
            if (cur == 0) return kNodeUnknown;
        }
        return kNodeUnknown;
    }
    size_t used() const { return used_.load(std::memory_order_relaxed); }
    uint64_t full() const { return full_.load(std::memory_order_relaxed); }

private:
    static size_t hash(uint64_t k) {
        k ^= k >> 33;
        k *= 0xFF51AFD7ED558CCDull;
        k ^= k >> 33;
        return static_cast<size_t>(k);
    }
    std::atomic<uint64_t> keys_[N] = {};
    std::atomic<uint8_t> vals_[N];
    std::atomic<size_t> used_{0};
    std::atomic<uint64_t> full_{0};
};

// sequencer id -> resource of the 2661s the hook judged (written by the 2661 hook, read by P1 to key the map).
template <size_t N>
class SeqResRing {
public:
    void note(uint64_t seq, uint32_t resource) {
        if (!seq || !resource) return;
        for (size_t i = 0; i < N; ++i) {
            if (seq_[i].load(std::memory_order_acquire) == seq) {
                res_[i].store(resource, std::memory_order_release);
                return;
            }
        }
        const size_t i = next_.fetch_add(1, std::memory_order_relaxed) % N;
        seq_[i].store(0, std::memory_order_release);
        res_[i].store(resource, std::memory_order_release);
        seq_[i].store(seq, std::memory_order_release);
    }
    uint32_t find(uint64_t seq) const {
        if (!seq) return 0;
        for (size_t i = 0; i < N; ++i) {
            if (seq_[i].load(std::memory_order_acquire) != seq) continue;
            const uint32_t r = res_[i].load(std::memory_order_acquire);
            if (seq_[i].load(std::memory_order_acquire) == seq) return r;
        }
        return 0;
    }

private:
    std::atomic<uint64_t> seq_[N] = {};
    std::atomic<uint32_t> res_[N] = {};
    std::atomic<size_t> next_{0};
};

// ---------------------------------------------------------------- the probes (read-only)
//
// The stage's actor list [V nativo.md §1.3, capstone 0x602940 / 0x205DC30 / 0x6075B0]: {ptr +0x120, u32 count +0x128},
// entries of 0x28 B: +0 the node (node+0x30 / +0x34 = its ids, node+0xAC = the node type: 0 CurrentControl = the
// player, 1 fixed actor, 2 SpawnCharacter, 4 PivotActor), +8 u32 the actor key, +0xC u8 the binding type.
inline constexpr uint32_t kStage_Mgr = 0x28, kStage_Seq = 0x30, kStage_B60 = 0x60, kStage_B61 = 0x61,
                          kStage_Owner = 0x98, kStage_Binds = 0x120, kStage_BindCount = 0x128, kStage_Started = 0x28B;
inline constexpr uint32_t kBindStride = 0x28, kBind_Node = 0x00, kBind_Key = 0x08, kBind_Type = 0x0C;
inline constexpr uint32_t kNode_Id = 0x30, kNode_Id2 = 0x34, kNode_Type = 0xAC;
inline constexpr uint32_t kMaxStageBinds = 64;
// SequencerControl's description (R8 of vf21 / vf22): [R8+8] = data, data+0x30 = u32 ids, data+0x38 = count.
inline constexpr uint32_t kDesc_Data = 0x08, kData_Ids = 0x30, kData_IdCount = 0x38;
inline constexpr uint32_t kMaxIds = 16, kMissionNode = 0x1FFFFFFF;

struct StageBind {
    uint32_t key = 0;
    uint32_t node_id = 0;
    uint32_t node_id2 = 0;
    uint8_t node_type = 0xFF;
    uint8_t link = 0xFF;
};

// P1 (0x602940, the scene's start): what the scene is about to do with the owner's copy on THIS client.
enum class P1 : uint8_t {
    OwnScene,          // owner == this client's character
    OwnUnknown,        // this client's character is not known yet (2756): nothing called FOREIGN (review H-9)
    OwnerActive,       // foreign: the owner bound at the player node (type 0) with link 5 -> moved as an NPC (the bug)
    OwnerPending,      // foreign: ... with link 4 (would become 5)
    OwnerNeutral,      // foreign: the owner's binding kept the 0 (the fix reached the stage)
    OwnerOtherNode,    // foreign: the owner bound at a node that is not CurrentControl (unexpected; counted)
    NoOwner,           // foreign: no entry with the owner's key
    Unreadable,
    Count
};
inline const char* p1_name(P1 c) {
    switch (c) {
        case P1::OwnScene: return "own scene";
        case P1::OwnUnknown: return "this client's character not known yet (not judged)";
        case P1::OwnerActive: return "FOREIGN, owner at the player node with link 5 (moved as an NPC)";
        case P1::OwnerPending: return "FOREIGN, owner at the player node with link 4";
        case P1::OwnerNeutral: return "FOREIGN, owner's binding at link 0 (neutralized)";
        case P1::OwnerOtherNode: return "FOREIGN, owner at a node that is NOT the player node";
        case P1::NoOwner: return "FOREIGN, no binding to the owner";
        case P1::Unreadable: return "unreadable";
        default: return "?";
    }
}
inline P1 classify_p1(uint32_t owner, uint32_t own_key, std::span<const StageBind> binds, bool readable = true) {
    if (!readable) return P1::Unreadable;
    if (!own_key) return P1::OwnUnknown;
    if (!owner || owner == own_key) return P1::OwnScene;
    const StageBind* o = nullptr;
    for (const StageBind& b : binds) {
        if (b.key == owner) {
            o = &b;
            break;
        }
    }
    if (!o) return P1::NoOwner;
    if (o->node_type != kNodeCurrentControl) return P1::OwnerOtherNode;
    if (o->link == kLinkNone) return P1::OwnerNeutral;
    if (o->link == kLinkPending) return P1::OwnerPending;
    return P1::OwnerActive;
}

// P2 (vf21 0x601470 / vf22 0x601C10 entries): which bound keys 0x205DC30 would hand the SequencerControl to.
// Replica: a target id matches an entry whose node+0x30 or +0x34 is that id and whose link is not 0; only link 5
// yields an actor. 0x1FFFFFFF (the field owner's mission node, 0x205EFB0) is only flagged.
struct Resolved {
    uint32_t keys[kMaxIds] = {};
    uint8_t links[kMaxIds] = {};
    uint8_t n = 0;
    bool mission_node = false;
    uint8_t counted = 0;  // matches with a link != 0 (0x205DC30's return value)
};
inline Resolved resolve(std::span<const uint32_t> ids, std::span<const StageBind> binds) {
    Resolved r;
    for (uint32_t id : ids) r.mission_node |= id == kMissionNode;
    for (const StageBind& b : binds) {
        if (b.link == kLinkNone) continue;
        for (uint32_t id : ids) {
            if (b.node_id != id && b.node_id2 != id) continue;
            ++r.counted;
            if (b.link == kLinkActive && r.n < kMaxIds) {
                r.keys[r.n] = b.key;
                r.links[r.n] = b.link;
                ++r.n;
            }
            break;
        }
    }
    return r;
}
enum class P2 : uint8_t {
    NotStarted,      // stage+0x28B == 0: the function returns at once
    OtherPlayer,     // an actor resolved is ANOTHER player's character (the NPC branch on a player: the bug)
    LocalPlayer,     // this client's own character resolved (the native local-player branch)
    NpcOnly,         // only non-character keys resolved
    NothingResolved, // no link-5 match (0x6075B0's pending path: only a link 4 entry is queued there)
    OwnUnknown,      // a character resolved while this client's own key is not known yet (review H-9)
    Count
};
inline const char* p2_name(P2 c) {
    switch (c) {
        case P2::NotStarted: return "stage not started (+0x28B 0)";
        case P2::OtherPlayer: return "control to ANOTHER player's character (NPC branch)";
        case P2::LocalPlayer: return "control to this client's character";
        case P2::NpcOnly: return "control to NPC / gimmick only";
        case P2::NothingResolved: return "nothing resolved (link 0 / unknown ids)";
        case P2::OwnUnknown: return "a character resolved, this client's own key not known yet";
        default: return "?";
    }
}
inline P2 classify_p2(bool started, const Resolved& r, uint32_t own_key) {
    if (!started) return P2::NotStarted;
    bool other = false, local = false;
    for (uint8_t i = 0; i < r.n; ++i) {
        if (!is_character(r.keys[i])) continue;
        if (r.keys[i] == own_key) local = true;
        else other = true;
    }
    if ((other || local) && !own_key) return P2::OwnUnknown;
    if (other) return P2::OtherPlayer;
    if (local) return P2::LocalPlayer;
    return r.n ? P2::NpcOnly : P2::NothingResolved;
}

// P3 (0x6034FA, right before 0x6030D0's TransformSync vf1A0(pos, rot, 4, 5, 0)): the game's own vf178 already said
// "not the local user" to reach it; classified by key.
enum class P3 : uint8_t { OtherPlayer, OwnCharacter, NonCharacter, Count };
inline P3 classify_p3(uint32_t key, uint32_t own_key) {
    if (!is_character(key)) return P3::NonCharacter;
    return key == own_key ? P3::OwnCharacter : P3::OtherPlayer;
}
inline const char* p3_name(P3 c) {
    switch (c) {
        case P3::OtherPlayer: return "ANOTHER player's character aligned to the scene's mark";
        case P3::OwnCharacter: return "this client's character (unexpected: vf178 said not local)";
        case P3::NonCharacter: return "NPC / gimmick aligned";
        default: return "?";
    }
}

// P4 (review H-1 c): the entry of 0x5EB310 (the stage order that builds a client actor, CreateClientActorDesc_Sequencer;
// its only caller is 0x5EB2C0, a vtable slot at .xdata 0x558C398). It finds the stage's binding whose node+0x30 is the
// order's node id (0x5EB3B1) and goes on only for types {0,3,6,7} (0x5EB41F: cmp al,7 ; ja error ; bt 0xC9): a
// binding the fix set to 0 at the owner would let the native path create a client actor there (a double).
enum class P4 : uint8_t {
    NotForeign,     // the stage's owner is this client's character (or it is not known yet)
    NotFound,       // no binding for the order's node: the native path goes on (not about the owner)
    Rejected,       // types 1, 2, 4, 5 (or > 7): the native error, nothing created
    SpawnOnOwner,   // the OWNER's binding at type 0: a client actor would be built for the other player's node
    SpawnOther0,    // another key's binding at type 0
    SpawnOtherType, // types 3 / 6 / 7
    Count
};
inline bool p4_accepts(uint8_t link) { return link <= 7 && ((0xC9u >> link) & 1u); }
inline P4 classify_p4(bool foreign, bool found, uint32_t key, uint8_t link, uint32_t owner) {
    if (!foreign) return P4::NotForeign;
    if (!found) return P4::NotFound;
    if (!p4_accepts(link)) return P4::Rejected;
    if (link == kLinkNone) return key && key == owner ? P4::SpawnOnOwner : P4::SpawnOther0;
    return P4::SpawnOtherType;
}
inline const char* p4_name(P4 c) {
    switch (c) {
        case P4::NotForeign: return "own scene";
        case P4::NotFound: return "FOREIGN, node not among the bindings";
        case P4::Rejected: return "FOREIGN, bound node (1/2/4/5): native error, nothing created";
        case P4::SpawnOnOwner: return "FOREIGN, OWNER's binding at 0: a client actor WOULD be created (double)";
        case P4::SpawnOther0: return "FOREIGN, another key at type 0: client actor path";
        case P4::SpawnOtherType: return "FOREIGN, type 3/6/7: client actor path";
        default: return "?";
    }
}

}  // namespace cdmp::session2::gsf
