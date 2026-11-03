#pragma once
// P8 S1, NPC scenes on the guest: the pure logic of the MEASUREMENT probes of
// docs/research/pendientes/p8-s1-escenas-npc.md §4-§5 (H1-H3 on the host,
// G1-G4 on the guest; "Implementado para la medida"). No game access, so the
// tests run it on records copied from the session-6 captures.
//
// Everything here is observation: nothing decides for the game, nothing calls
// it, nothing is sent. The hooks (host_scene_diag.cpp, guest_scene_diag.cpp)
// copy registers and SEH-guarded reads into fixed events; the diagnostic
// threads (observer_stage's on the host, guest_diag_worker on the guest)
// format them with this file's helpers.
//
// Wire layouts (build 1.0.0.2976; read order of each client handler, capstone,
// and the captures; payload = after the 5-byte u16 opcode | i8 | u16 len):
//   2335 CreateNPCScheduleStageAck  0xC0AFC0: u8 kind | u32 actor | u32 | u32 resource (+9) | u8 | u64 scene
//                                   key (+14) | 4 x 12-byte vectors, a list... (94/110/126/142/158/206 B)
//   2126 DeleteNPCScheduleStageAck  0xC0B840: u32 actor | u64 scene key                         (12 B)
//   2411 ScheduleStageClosedAck     0xC0DB40: u32 actor | u32                                   (8 B)
//   2874 InitializeNPCScheduleAck   0xC0CE50: u32 actor | u32 NPC | u32 resource | u32 node | u64 scene key
//                                   | 10 x 4 x u8                                               (64 B)
//   2661 PrepareSequencerAck        (observer_stage_logic.h) the play data's u32 at +18 = the resource
//                                   (payload +27, p8-s1-escenas-npc.md §2.2)
//   2005 ActorLoadDataAck           n x (u16 len | record); record: u8 kind | u32 actor key | ...; an
//                                   NPC's AI block somewhere inside: u8 | u32 resource | u32 node | u64
//                                   scene key (the reader 0x1EB88C0, §2.1)
//   2223 StartActionReq             u32 key | 2 x 69-byte action layers (u32 at +0/+4 each: the action
//                                   hashes) | B (0x8E..0x2D3; its last byte 0x2D3 = B+0x281) | u8 (725 B)
//   C>S scene Reqs of a client (G4): 2767 u64 scene | u32 | u8 | u32 actor (+13) | u8 (18 B); 2591 u64
//                                   scene | ... (17 B); 2436 u64 scene | ... (14 B); 2138 u64 scene | u32
//                                   (12 B); 2173 u32 | u32 actor (+4) | ... (15 B); 3020 u32 actor | u8 (5 B)

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cdmp::session2::scd {

// ---------------------------------------------------------------- opcodes

inline constexpr uint16_t kOpActorLoad = 2005;          // ActorLoadDataAck
inline constexpr uint16_t kOpStartActionReq = 2223;     // StartActionReq
inline constexpr uint16_t kOpScheduleCreate = 2335;     // CreateNPCScheduleStageAck
inline constexpr uint16_t kOpScheduleDelete = 2126;     // DeleteNPCScheduleStageAck
inline constexpr uint16_t kOpScheduleClosed = 2411;     // ScheduleStageClosedAck
inline constexpr uint16_t kOpInitSchedule = 2874;       // InitializeNPCScheduleAck
inline constexpr uint16_t kOpPrepare = 2661;            // PrepareSequencerAck
inline constexpr uint16_t kOpDeleteSequencer = 2810;    // DeleteSequencerAck
inline constexpr uint16_t kOpBranch = 2981;             // StageBranchStateAck
inline constexpr uint16_t kOpFactionSpawn = 2195;       // CreateFactionSpawnStageAck
inline constexpr uint16_t kOpMoveFieldAck = 2174;
inline constexpr uint16_t kOpMoveFieldCompleteReq = 2549;
inline constexpr uint16_t kOpMoveFieldCompleteAck = 2144;
inline constexpr uint16_t kOpMilestone = 2322;          // PlayAllPreLoadSequencerAck
// The client's scene/sequencer Reqs (p8-s1-escenas-npc.md §4 A.3, G4).
inline constexpr uint16_t kOpTriggerVolume = 2767;      // SequencerTriggerVolumeInOutReq
inline constexpr uint16_t kOpChartEvent = 2591;         // UpdateStageChartEventReq
inline constexpr uint16_t kOpBranchReq = 2436;          // StageBranchStateReq
inline constexpr uint16_t kOpEvadeLogout = 2547;        // SequencerEvadeTriggerLogoutReq
inline constexpr uint16_t kOpGamePlayData = 2173;       // UpdateSequencerGamePlayDataEventReq
inline constexpr uint16_t kOpReloadStage = 2283;        // ReloadStageStartReq
inline constexpr uint16_t kOpStageFail = 2138;          // SequencerStageFailReq
inline constexpr uint16_t kOpNextBranch = 3020;         // SetNextBranchTypeFromAIReq
inline constexpr uint16_t kOpCandidatePos = 2100;       // SyncStageCandidatePostionReq

// The Reqs G4 counts, in the order its line prints them.
inline constexpr uint16_t kSceneReqs[] = {kOpTriggerVolume, kOpChartEvent, kOpBranchReq, kOpEvadeLogout,
                                          kOpGamePlayData, kOpReloadStage, kOpStageFail, kOpNextBranch,
                                          kOpCandidatePos, kOpStartActionReq};
inline constexpr size_t kSceneReqCount = sizeof(kSceneReqs) / sizeof(kSceneReqs[0]);
constexpr int scene_req_index(uint16_t op) {
    for (size_t i = 0; i < kSceneReqCount; ++i) {
        if (kSceneReqs[i] == op) return static_cast<int>(i);
    }
    return -1;
}

inline constexpr size_t kHeader = 5;

inline uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}
inline uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
inline uint64_t le64(const uint8_t* p) { return static_cast<uint64_t>(le32(p)) | static_cast<uint64_t>(le32(p + 4)) << 32; }

// An actor key's class: 0x9... user, 0xA... character, 0xB... NPC.
constexpr bool is_npc_key(uint32_t k) { return (k >> 28) == 0xB; }
constexpr bool is_character_key(uint32_t k) { return (k >> 28) == 0xA; }

// ---------------------------------------------------------------- scene packets (S>C)

struct ScheduleCreate {
    uint8_t kind = 0;
    uint32_t actor = 0;
    uint32_t stamp = 0;     // u32 at +5 (not interpreted)
    uint32_t resource = 0;  // the NPCScheduleData_File id
    uint8_t b13 = 0;
    uint64_t scene = 0;     // 0x2000xxxx
    size_t size = 0;
};
inline constexpr size_t kScheduleCreateMin = 22;
inline std::optional<ScheduleCreate> parse_schedule_create(std::span<const uint8_t> p) {
    if (p.size() < kScheduleCreateMin) return std::nullopt;
    return ScheduleCreate{p[0], le32(&p[1]), le32(&p[5]), le32(&p[9]), p[13], le64(&p[14]), p.size()};
}

struct ScheduleDelete {
    uint32_t actor = 0;
    uint64_t scene = 0;
};
inline constexpr size_t kScheduleDeleteSize = 12;
inline std::optional<ScheduleDelete> parse_schedule_delete(std::span<const uint8_t> p) {
    if (p.size() < kScheduleDeleteSize) return std::nullopt;
    return ScheduleDelete{le32(&p[0]), le64(&p[4])};
}

struct ScheduleClosed {
    uint32_t actor = 0;
    uint32_t id = 0;  // compared with the low 32 bits of the live schedule keys
};
inline constexpr size_t kScheduleClosedSize = 8;
inline std::optional<ScheduleClosed> parse_schedule_closed(std::span<const uint8_t> p) {
    if (p.size() < kScheduleClosedSize) return std::nullopt;
    return ScheduleClosed{le32(&p[0]), le32(&p[4])};
}

struct InitSchedule {
    uint32_t actor = 0;  // the handler resolves it first (0x8A67E0) and puts it in the wrapper
    uint32_t npc = 0;    // resolved with 0x8ADFE0: the NPC whose AI is re-bound
    uint32_t resource = 0, node = 0;
    uint64_t scene = 0;
    bool complete = false;  // the whole 64 bytes (10 x 4 x u8 after the key)
};
inline constexpr size_t kInitScheduleMin = 24, kInitScheduleSize = 64;
inline std::optional<InitSchedule> parse_init_schedule(std::span<const uint8_t> p) {
    if (p.size() < kInitScheduleMin) return std::nullopt;
    return InitSchedule{le32(&p[0]), le32(&p[4]), le32(&p[8]), le32(&p[12]), le64(&p[16]), p.size() == kInitScheduleSize};
}

// 2661: the resource its play data names (+18 of the 86-byte play data, payload +27).
inline constexpr size_t kPrepareResourceAt = 27;
inline std::optional<uint32_t> prepare_resource(std::span<const uint8_t> p) {
    if (p.size() < 91) return std::nullopt;  // obs::kPreparePrefix
    return le32(&p[kPrepareResourceAt]);
}

// ---------------------------------------------------------------- 2005 records

struct Record2005 {
    size_t off = 0;  // payload offset of the record (after its u16 length)
    size_t len = 0;
    uint8_t kind = 0;
    uint32_t key = 0;
};

// The whole partition, or nothing when the lengths do not tile the payload.
inline std::optional<std::vector<Record2005>> parse_2005(std::span<const uint8_t> p, size_t max_records = 256) {
    std::vector<Record2005> out;
    size_t o = 0;
    while (o < p.size()) {
        if (o + 2 > p.size() || out.size() >= max_records) return std::nullopt;
        const size_t n = le16(&p[o]);
        if (n < 5 || o + 2 + n > p.size()) return std::nullopt;
        out.push_back({o + 2, n, p[o + 2], le32(&p[o + 3])});
        o += 2 + n;
    }
    if (out.empty()) return std::nullopt;
    return out;
}

// The same walk without allocation, for a game thread (take_frame): how many
// records, how many are NPCs and the first NPC key. ok = the lengths tile the
// payload within `max_records`.
struct Scan2005 {
    bool ok = false;
    uint32_t records = 0, npcs = 0, first_npc = 0;
};
inline Scan2005 scan_2005(const uint8_t* p, size_t size, uint32_t max_records = 256) {
    Scan2005 s;
    size_t o = 0;
    while (o < size) {
        if (o + 2 > size || s.records >= max_records) return s;
        const size_t n = le16(p + o);
        if (n < 5 || o + 2 + n > size) return s;
        const uint32_t key = le32(p + o + 3);
        ++s.records;
        if (is_npc_key(key)) {
            if (!s.npcs) s.first_npc = key;
            ++s.npcs;
        }
        o += 2 + n;
    }
    s.ok = s.records > 0;
    return s;
}

// An NPC's AI scene block, as 0x1EB88C0 reads it: u8 | u32 resource | u32 node | u64 scene key.
struct AiBlock {
    size_t off = 0;  // record offset of the u8
    uint8_t flag = 0;
    uint32_t resource = 0, node = 0;
    uint64_t scene = 0;
};
inline constexpr size_t kAiBlockSize = 17;
inline std::optional<AiBlock> read_ai_block(std::span<const uint8_t> rec, size_t off) {
    if (off + kAiBlockSize > rec.size()) return std::nullopt;
    return AiBlock{off, rec[off], le32(&rec[off + 1]), le32(&rec[off + 5]), le64(&rec[off + 9])};
}
// The record has no parser of its own here: the block is found by the resource
// (and the scene key, when known) the NPC's AI read on the host's own client
// (G1 there gives both). Every match is returned (normally one).
inline std::vector<AiBlock> find_ai_blocks(std::span<const uint8_t> rec, uint32_t resource,
                                           std::optional<uint64_t> scene = std::nullopt, size_t max = 4) {
    std::vector<AiBlock> out;
    if (!resource || rec.size() < kAiBlockSize) return out;
    for (size_t off = 0; off + kAiBlockSize <= rec.size() && out.size() < max; ++off) {
        if (le32(&rec[off + 1]) != resource) continue;
        const auto b = read_ai_block(rec, off);
        if (b && (!scene || b->scene == *scene)) out.push_back(*b);
    }
    return out;
}

// ---------------------------------------------------------------- 2223 StartActionReq (H1)

struct StartActionWire {
    uint32_t key = 0;
    uint32_t h[4] = {};  // layer 0 +0/+4, layer 1 +0/+4 (wire +4, +8, +73, +77)
    uint8_t flag = 0;    // wire +0x2D3 = B+0x281: the server applies it and does NOT relay it (0x2A78D0D)
};
inline constexpr size_t kStartActionSize = 725, kStartActionFlagAt = 0x2D3;
inline std::optional<StartActionWire> parse_start_action(std::span<const uint8_t> p) {
    if (p.size() != kStartActionSize) return std::nullopt;
    StartActionWire w;
    w.key = le32(&p[0]);
    w.h[0] = le32(&p[4]);
    w.h[1] = le32(&p[8]);
    w.h[2] = le32(&p[73]);
    w.h[3] = le32(&p[77]);
    w.flag = p[kStartActionFlagAt];
    return w;
}
// The in-memory A array the server's logic receives ([r12] in 0x2A78CA0): two
// 0x50-byte layers, u32 +0 and +4 each (0x2442610 reads them first).
inline constexpr uint32_t kLayerStride = 0x50;
// The hash a line names: layer 0's +4, else its +0, else layer 1's (the
// session-6 capture: 0x35DD392F sits at +4 and/or +0 of layer 0).
constexpr uint32_t action_hash(uint32_t l0a, uint32_t l0b, uint32_t l1a, uint32_t l1b) {
    return l0b ? l0b : l0a ? l0a : l1b ? l1b : l1a;
}

// ---------------------------------------------------------------- the client's scene Reqs (G4)

struct SceneReq {
    uint16_t op = 0;
    bool has_scene = false, has_actor = false;
    uint64_t scene = 0;
    uint32_t actor = 0;
};
inline SceneReq parse_scene_req(uint16_t op, std::span<const uint8_t> p) {
    SceneReq r;
    r.op = op;
    switch (op) {
        case kOpTriggerVolume:
            if (p.size() >= 8) r.has_scene = true, r.scene = le64(&p[0]);
            if (p.size() >= 17) r.has_actor = true, r.actor = le32(&p[13]);
            break;
        case kOpChartEvent:
        case kOpBranchReq:
        case kOpStageFail:
        case kOpEvadeLogout:  // layout not seen in a capture: the u64 at +0 as for its family [E]
            if (p.size() >= 8) r.has_scene = true, r.scene = le64(&p[0]);
            break;
        case kOpReloadStage:  // P8 S14 capture [V]: u32 0 | u32 FieldInfoKey | u32 actor | transform: no scene id
            break;
        case kOpGamePlayData:
            if (p.size() >= 8) r.has_actor = true, r.actor = le32(&p[4]);
            break;
        case kOpNextBranch:
        case kOpStartActionReq:
            if (p.size() >= 4) r.has_actor = true, r.actor = le32(&p[0]);
            break;
        default:
            break;
    }
    return r;
}

// ---------------------------------------------------------------- lock-free counts (hook side)

// K u32 counters per u64 key (0 is never a key), from game threads. Open
// addressing, at most kMaxProbe slots per call and N/2 keys; reaching that
// closes the table for new keys (known keys still count). rows() is for the
// drain thread (it allocates). An untracked add is counted, never blocks.
template <size_t N, size_t K>
class CountTable {
    static_assert(N >= 4 && (N & (N - 1)) == 0, "N must be a power of two");

public:
    static constexpr size_t kMaxProbe = N < 32 ? N : 32;
    static constexpr size_t kMaxKeys = N / 2;

    // True the first time a key is inserted (the caller may queue a "new key" event).
    bool add(uint64_t key, size_t which, uint32_t n = 1, bool* inserted = nullptr) {
        if (!key || which >= K) return false;
        size_t i = hash(key);
        const bool closed = closed_.load(std::memory_order_relaxed);
        for (size_t probe = 0; probe < kMaxProbe; ++probe, i = (i + 1) & (N - 1)) {
            uint64_t cur = keys_[i].load(std::memory_order_acquire);
            if (cur == 0) {
                if (closed) break;
                if (size_.load(std::memory_order_relaxed) >= kMaxKeys) {
                    closed_.store(true, std::memory_order_relaxed);
                    break;
                }
                if (keys_[i].compare_exchange_strong(cur, key, std::memory_order_acq_rel)) {
                    size_.fetch_add(1, std::memory_order_relaxed);
                    if (inserted) *inserted = true;
                    cur = key;
                }
            }
            if (cur == key) {
                counts_[i][which].fetch_add(n, std::memory_order_relaxed);
                return true;
            }
        }
        untracked_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    uint32_t get(uint64_t key, size_t which) const {
        if (!key || which >= K) return 0;
        size_t i = hash(key);
        for (size_t probe = 0; probe < kMaxProbe; ++probe, i = (i + 1) & (N - 1)) {
            const uint64_t cur = keys_[i].load(std::memory_order_acquire);
            if (cur == key) return counts_[i][which].load(std::memory_order_relaxed);
            if (cur == 0) return 0;
        }
        return 0;
    }
    struct Row {
        uint64_t key = 0;
        uint32_t c[K] = {};
    };
    std::vector<Row> rows() const {
        std::vector<Row> out;
        for (size_t i = 0; i < N; ++i) {
            const uint64_t k = keys_[i].load(std::memory_order_acquire);
            if (!k) continue;
            Row r;
            r.key = k;
            for (size_t j = 0; j < K; ++j) r.c[j] = counts_[i][j].load(std::memory_order_relaxed);
            out.push_back(r);
        }
        return out;
    }
    size_t size() const { return size_.load(std::memory_order_relaxed); }
    bool closed() const { return closed_.load(std::memory_order_relaxed); }
    uint64_t untracked() const { return untracked_.load(std::memory_order_relaxed); }

private:
    static size_t hash(uint64_t k) { return static_cast<size_t>((k * 0x9E3779B97F4A7C15ull) >> 40) & (N - 1); }
    std::atomic<uint64_t> keys_[N] = {};
    std::atomic<uint32_t> counts_[N][K] = {};
    std::atomic<size_t> size_{0};
    std::atomic<bool> closed_{false};
    std::atomic<uint64_t> untracked_{0};
};

// H1's columns per NPC key and per hash: kept local (flag set) / not flagged,
// each with user 2 absent or present, then the broadcast decision of 0x2A79060.
enum H1Col : size_t {
    kFlagNoU2 = 0, kFlagU2, kPlainNoU2, kPlainU2, kBroadcast, kNotBroadcast, kH1Cols
};

// ---------------------------------------------------------------- G2: pa::NPCScheduleManager's container

// [[exe+0x6C8CDA0]+8], ThreadSafeRefCountedContainer<u32, NPCScheduleData_File>
// (created by 0x1861F00). 0x186A810 (lookup) and 0x1869D70 (the node), replayed
// with our own reads, no lock, no call:
//   u32 [C+0x24] == 0 or u32 [C+0x20] (buckets) == 0: nothing
//   bucket = [C+0x30] + (id % buckets) * 0x100: u32 count, then {u32 id, u32 index} from +8
//   entry = [[C+0x38] + index*8], u32 [entry+4] == id; file = [[entry+8]]
//   file == 0: none; u8 [file+0x15] != 0 (unloading): none
//   node: binary search of (node >> 2) over the 12-byte entries [file+0xD0] (count u32 [file+0xD8])
//   by u32 [e+4] >> 2; found: index u32 [e+8] < u32 [file+0x58]
// (The game's lookup also takes a reader lock and writes [obj+0x10] = 0: our
// replica does neither; it may read a container being changed: every read is
// checked and the walk bounded.)
struct ScheduleLayout {
    static constexpr uintptr_t kManagerSlot = 0x6C8CDA0;  // exe+..: pa::NPCScheduleManager*
    static constexpr uint32_t kManager_Container = 0x08;
    static constexpr uint32_t kUsed = 0x24, kBuckets = 0x20, kBucketArray = 0x30, kEntries = 0x38;
    static constexpr uint32_t kBucketSize = 0x100, kBucketMax = 31, kEntryKey = 4, kEntryHolder = 8;
    static constexpr uint32_t kFileUnloading = 0x15, kFileNodes = 0xD0, kFileNodeCount = 0xD8, kFileEntries = 0x58;
    static constexpr uint32_t kNodeStride = 12, kNodeKey = 4, kNodeIndex = 8;
};

enum class Lookup : uint8_t { NoContainer, Empty, FileNotLoaded, FileUnloading, NodeNotFound, Found, Unreadable };
inline const char* lookup_text(Lookup l) {
    switch (l) {
        case Lookup::NoContainer: return "no NPCScheduleManager container";
        case Lookup::Empty: return "container empty (no file loaded at all)";
        case Lookup::FileNotLoaded: return "FILE NOT LOADED";
        case Lookup::FileUnloading: return "file present but UNLOADING (+0x15)";
        case Lookup::NodeNotFound: return "file loaded, NODE NOT FOUND";
        case Lookup::Found: return "file loaded and node found";
        case Lookup::Unreadable: return "unreadable";
    }
    return "?";
}

struct LookupResult {
    Lookup what = Lookup::NoContainer;
    uintptr_t file = 0;
    uint32_t index = 0xFFFFFFFFu, nodes = 0, buckets = 0, used = 0, steps = 0;
};

// Mem: bool u8(uintptr_t, uint8_t&), u32(uintptr_t, uint32_t&), ptr(uintptr_t, uintptr_t&): checked reads.
template <class Mem>
LookupResult schedule_lookup(const Mem& m, uintptr_t container, uint32_t resource, uint32_t node, bool want_node = true,
                             uint32_t max_steps = 40) {
    using L = ScheduleLayout;
    LookupResult r;
    if (!container) return r;
    if (!m.u32(container + L::kUsed, r.used) || !m.u32(container + L::kBuckets, r.buckets)) {
        r.what = Lookup::Unreadable;
        return r;
    }
    if (!r.used || !r.buckets) {
        r.what = Lookup::Empty;
        return r;
    }
    uintptr_t array = 0, entries = 0;
    uint32_t count = 0;
    if (!m.ptr(container + L::kBucketArray, array) || !array || !m.ptr(container + L::kEntries, entries) || !entries) {
        r.what = Lookup::Unreadable;
        return r;
    }
    const uintptr_t bucket = array + static_cast<uintptr_t>(resource % r.buckets) * L::kBucketSize;
    if (!m.u32(bucket, count)) {
        r.what = Lookup::Unreadable;
        return r;
    }
    r.what = Lookup::FileNotLoaded;
    for (uint32_t i = 0; i < count && i < L::kBucketMax; ++i) {
        uint32_t k = 0, index = 0, ek = 0;
        uintptr_t entry = 0, holder = 0, file = 0;
        if (!m.u32(bucket + 8 + 8 * i, k) || k != resource) continue;
        if (!m.u32(bucket + 0xC + 8 * i, index) || !m.ptr(entries + 8 * static_cast<uintptr_t>(index), entry) ||
            !entry || !m.u32(entry + L::kEntryKey, ek) || ek != resource) {
            continue;
        }
        if (!m.ptr(entry + L::kEntryHolder, holder) || !holder || !m.ptr(holder, file)) {
            r.what = Lookup::Unreadable;
            return r;
        }
        if (!file) return r;  // FileNotLoaded (the game returns null)
        uint8_t unloading = 0;
        if (!m.u8(file + L::kFileUnloading, unloading)) {
            r.what = Lookup::Unreadable;
            return r;
        }
        r.file = file;
        if (unloading) {
            r.what = Lookup::FileUnloading;
            return r;
        }
        break;
    }
    if (!r.file) return r;
    if (!want_node) {
        r.what = Lookup::Found;
        return r;
    }
    // 0x1869E2A..0x1869E9A
    uintptr_t table = 0;
    uint32_t limit = 0;
    if (!m.u32(r.file + L::kFileNodeCount, r.nodes) || !m.ptr(r.file + L::kFileNodes, table) ||
        !m.u32(r.file + L::kFileEntries, limit)) {
        r.what = Lookup::Unreadable;
        return r;
    }
    r.what = Lookup::NodeNotFound;
    if (!r.nodes) return r;
    int32_t hi = static_cast<int32_t>(r.nodes - 1), lo = 0;
    if (hi < 0) return r;
    const uint32_t target = node >> 2;
    while (true) {
        if (++r.steps > max_steps) {
            r.what = Lookup::Unreadable;
            return r;
        }
        int32_t d = hi - lo;
        if (d < 0) ++d;
        const int32_t mid = (d >> 1) + lo;
        const uintptr_t e = table + static_cast<uintptr_t>(static_cast<intptr_t>(mid)) * L::kNodeStride;
        uint32_t k = 0;
        if (!m.u32(e + L::kNodeKey, k)) {
            r.what = Lookup::Unreadable;
            return r;
        }
        k >>= 2;
        if (target < k) {
            hi = mid - 1;
        } else if (target == k) {
            if (mid < 0) return r;
            uint32_t index = 0;
            if (!m.u32(e + L::kNodeIndex, index)) {
                r.what = Lookup::Unreadable;
                return r;
            }
            if (index < limit) {
                r.what = Lookup::Found;
                r.index = index;
            }
            return r;
        } else {
            lo = mid + 1;
        }
        if (hi < lo) return r;
    }
}

// G1/H2: what the AI's handle says right after 0x1EB88C0 (H = [[ai+0x90]+8],
// written by 0x1869D70): a valid one has the requested (resource | node << 32)
// and an index; a failed one 0 / -1, and its file pointer says why.
inline const char* handle_text(uint64_t h0, uint32_t index, uintptr_t file) {
    if (static_cast<uint32_t>(h0) != 0 && static_cast<uint32_t>(h0 >> 32) != 0xFFFFFFFFu && index != 0xFFFFFFFFu) {
        return "VALID (the stage node resolves)";
    }
    return file ? "INVALID: file loaded, node not found" : "INVALID: FILE NOT LOADED (NPCScheduleData_File absent)";
}

// ---------------------------------------------------------------- H3: the live scene ledger (session 1)

struct ScheduleStage {
    uint64_t scene = 0;
    uint32_t actor = 0, resource = 0;
    uint64_t t_us = 0;
};
struct SequencerStage {
    uint64_t seq = 0;
    uint32_t actor = 0, owner = 0, resource = 0, bindings = 0;
    uint64_t t_us = 0;
};
struct NpcInit {
    uint32_t npc = 0, actor = 0, resource = 0, node = 0;
    uint64_t scene = 0, t_us = 0;
};

// What the host's own client (session 1, the field owner) received of the scene
// family, per field: the host's 2174 starts a new field (its client resets the
// managers on a field change). Drain thread only; bounded.
class SceneLedger {
public:
    explicit SceneLedger(size_t cap = 4096) : cap_(cap) {}

    void new_field(uint32_t owner, uint64_t t_us) {
        ++field_;
        owner_ = owner;
        field_t_us_ = t_us;
        dropped_ += schedules_.size() + sequencers_.size();
        schedules_.clear();
        sequencers_.clear();
        inits_.clear();
    }
    void schedule_create(const ScheduleStage& s) {
        ++n_create_;
        if (!schedules_.count(s.scene) && schedules_.size() >= cap_) {
            ++full_;
            return;
        }
        schedules_[s.scene] = s;
    }
    bool schedule_delete(uint64_t scene) {
        ++n_delete_;
        return schedules_.erase(scene) > 0;
    }
    // 2411 names a u32: the schedule whose key's low 32 bits match.
    bool schedule_closed(uint32_t id) {
        ++n_closed_;
        for (auto it = schedules_.begin(); it != schedules_.end(); ++it) {
            if (static_cast<uint32_t>(it->first) == id) {
                schedules_.erase(it);
                return true;
            }
        }
        return false;
    }
    void prepare(const SequencerStage& s) {
        ++n_prepare_;
        if (!sequencers_.count(s.seq) && sequencers_.size() >= cap_) {
            ++full_;
            return;
        }
        sequencers_[s.seq] = s;
    }
    bool remove_sequencer(uint64_t seq) {
        ++n_remove_;
        return sequencers_.erase(seq) > 0;
    }
    void init(const NpcInit& i) {
        ++n_init_;
        if (!inits_.count(i.npc) && inits_.size() >= cap_) {
            ++full_;
            return;
        }
        inits_[i.npc] = i;
    }

    // The scene an NPC's AI block names: a live schedule (by key) or sequencer
    // (by id), or by resource. Kind: 1 schedule, 2 sequencer, 0 none.
    struct Hit {
        int kind = 0;
        uint64_t scene = 0;
        uint64_t t_us = 0;
        uint32_t resource = 0;
    };
    Hit find(uint64_t scene, uint32_t resource) const {
        if (auto it = schedules_.find(scene); scene && it != schedules_.end()) {
            return {1, scene, it->second.t_us, it->second.resource};
        }
        if (auto it = sequencers_.find(scene); scene && it != sequencers_.end()) {
            return {2, scene, it->second.t_us, it->second.resource};
        }
        for (const auto& [k, s] : schedules_) {
            if (resource && s.resource == resource) return {1, k, s.t_us, s.resource};
        }
        for (const auto& [k, s] : sequencers_) {
            if (resource && s.resource == resource) return {2, k, s.t_us, s.resource};
        }
        return {};
    }
    std::vector<uint32_t> resources() const {
        std::vector<uint32_t> r;
        for (const auto& [k, s] : schedules_) r.push_back(s.resource);
        for (const auto& [k, s] : sequencers_) r.push_back(s.resource);
        std::sort(r.begin(), r.end());
        r.erase(std::unique(r.begin(), r.end()), r.end());
        r.erase(std::remove(r.begin(), r.end(), 0u), r.end());
        return r;
    }

    const std::map<uint64_t, ScheduleStage>& schedules() const { return schedules_; }
    const std::map<uint64_t, SequencerStage>& sequencers() const { return sequencers_; }
    const std::map<uint32_t, NpcInit>& inits() const { return inits_; }
    uint32_t field() const { return field_; }
    uint32_t owner() const { return owner_; }
    uint64_t field_t_us() const { return field_t_us_; }
    uint64_t created() const { return n_create_; }
    uint64_t deleted() const { return n_delete_; }
    uint64_t closed() const { return n_closed_; }
    uint64_t prepared() const { return n_prepare_; }
    uint64_t removed() const { return n_remove_; }
    uint64_t initialized() const { return n_init_; }
    uint64_t full() const { return full_; }
    uint64_t dropped_at_field_change() const { return dropped_; }

private:
    size_t cap_;
    uint32_t field_ = 0, owner_ = 0;
    uint64_t field_t_us_ = 0;
    std::map<uint64_t, ScheduleStage> schedules_;
    std::map<uint64_t, SequencerStage> sequencers_;
    std::map<uint32_t, NpcInit> inits_;
    uint64_t n_create_ = 0, n_delete_ = 0, n_closed_ = 0, n_prepare_ = 0, n_remove_ = 0, n_init_ = 0, full_ = 0,
             dropped_ = 0;
};

// ---------------------------------------------------------------- H3: one trip of user 2 (session 2)

// Followed from take_frame (S>C to session 2: 2174, 2005, 2144, 2322) and the
// injection (C>S of the guest: 2549), plus observer_stage's entry hook
// (0x2BC84E2, the END of MoveFieldCompleteCore). Where does the first NPC 2005
// sit, relative to 2549 (the START of MoveFieldCompleteCore 0x2BC7670 on the
// server) and 2144? That is where a snapshot would have to be queued.
struct TripFacts {
    uint32_t index = 0;
    uint64_t t2174 = 0, t2549 = 0, t_entry = 0, t2144 = 0, t2322 = 0, t_first_npc = 0;
    uint32_t packets = 0;           // to session 2 in this trip (every opcode)
    uint32_t first_npc_packet = 0;  // its number among them (1-based), 0 = none
    uint32_t first_npc_key = 0;
    uint32_t npc_before_2549 = 0, npc_2549_to_2144 = 0, npc_after_2144 = 0;  // NPC records (2005)
    uint32_t n2549 = 0;
    bool owner_entry = false;  // the entry hook said the traveller owns the field
};

enum class FirstNpc : uint8_t { None, Before2549, Between2549And2144, After2144 };
inline FirstNpc first_npc_position(const TripFacts& f) {
    if (!f.t_first_npc) return FirstNpc::None;
    if (!f.t2549 || f.t_first_npc < f.t2549) return FirstNpc::Before2549;
    if (!f.t2144 || f.t_first_npc < f.t2144) return FirstNpc::Between2549And2144;
    return FirstNpc::After2144;
}
inline const char* first_npc_text(FirstNpc p) {
    switch (p) {
        case FirstNpc::None: return "no NPC 2005 in this trip";
        case FirstNpc::Before2549:
            return "BEFORE the guest's 2549: a snapshot must be queued before MoveFieldCompleteCore (at the 2174)";
        case FirstNpc::Between2549And2144:
            return "between 2549 and 2144: the START of MoveFieldCompleteCore 0x2BC7670 (not its end 0x2BC84E2) is "
                   "early enough";
        case FirstNpc::After2144: return "after 2144: the entry hook 0x2BC84E2 would be early enough";
    }
    return "?";
}

class TripTracker {
public:
    // take_frame: a followed packet to session 2. `packet_no` = its number among
    // every packet queued to session 2 since this trip's 2174 (the 2174 is 1);
    // npc_records / first_npc only for 2005.
    void on_out(uint16_t op, uint64_t t_us, uint32_t packet_no, uint32_t npc_records = 0, uint32_t first_npc = 0) {
        if (op == kOpMoveFieldAck) {
            finish();
            cur_ = TripFacts{};
            cur_.index = ++trips_;
            cur_.t2174 = t_us;
            open_ = true;
        }
        if (!open_) return;
        cur_.packets = std::max(cur_.packets, packet_no);
        if (op == kOpActorLoad && npc_records) {
            if (!cur_.t_first_npc) {
                cur_.t_first_npc = t_us;
                cur_.first_npc_packet = packet_no;
                cur_.first_npc_key = first_npc;
            }
            if (!cur_.t2549) {
                cur_.npc_before_2549 += npc_records;
            } else if (!cur_.t2144) {
                cur_.npc_2549_to_2144 += npc_records;
            } else {
                cur_.npc_after_2144 += npc_records;
            }
        }
        if (op == kOpMoveFieldCompleteAck && !cur_.t2144) cur_.t2144 = t_us;
        if (op == kOpMilestone && !cur_.t2322) cur_.t2322 = t_us;
    }
    // the injection: a Req of the guest handed to the server
    void on_in(uint16_t op, uint64_t t_us) {
        if (!open_ || op != kOpMoveFieldCompleteReq) return;
        ++cur_.n2549;
        if (!cur_.t2549) cur_.t2549 = t_us;
    }
    void on_entry(uint64_t t_us, bool owner) {
        if (!open_ || cur_.t_entry) return;
        cur_.t_entry = t_us;
        cur_.owner_entry = owner;
    }
    // A trip is reported when the next one starts, or `quiet_us` after its 2144.
    std::optional<TripFacts> take_due(uint64_t now_us, uint64_t quiet_us = 15'000'000) {
        if (open_ && cur_.t2144 && now_us >= cur_.t2144 + quiet_us) finish();
        if (done_.empty()) return std::nullopt;
        TripFacts f = done_.front();
        done_.erase(done_.begin());
        return f;
    }
    const TripFacts* current() const { return open_ ? &cur_ : nullptr; }
    uint32_t trips() const { return trips_; }

private:
    void finish() {
        if (!open_) return;
        if (done_.size() < 16) done_.push_back(cur_);
        open_ = false;
    }
    TripFacts cur_;
    bool open_ = false;
    uint32_t trips_ = 0;
    std::vector<TripFacts> done_;
};

// ---------------------------------------------------------------- H2/G1: per NPC, the binding timeline

struct NpcTimeline {
    uint32_t npc = 0;
    uint32_t resource = 0, node = 0;
    uint64_t scene = 0;
    bool handle_valid = false, file_loaded = false;
    uint32_t owner = 0;           // comp+0x2D8 of its ClientSequencerPlayDataActorComponent, when readable
    uint64_t t_2005 = 0;          // G1: its AI block read (0x206DE6B)
    uint64_t t_vf13 = 0;          // 0x9ED401 (host only): local_res8 first seen
    bool vf13_passed = false;
    uint64_t t_bind_ai = 0, t_bind_2874 = 0;  // 0x20853D0 by the AI's vf107 / by a 2874
    uint64_t t_rebind = 0;        // 0x9F2FA0's local_res8 check (0x9F30CB)
    bool rebind_passed = false;
    uint64_t t_instance = 0;      // 0x9F312D with an instance (the binding 0x43B4B0 follows)
    uint32_t reads = 0, binds = 0;
};

// Drain thread only; bounded (new NPCs past `cap` are counted, not kept).
class NpcBook {
public:
    explicit NpcBook(size_t cap = 4096) : cap_(cap) {}
    NpcTimeline* get(uint32_t npc) {
        if (!npc) return nullptr;
        auto it = m_.find(npc);
        if (it != m_.end()) return &it->second;
        if (m_.size() >= cap_) {
            ++overflow_;
            return nullptr;
        }
        NpcTimeline t;
        t.npc = npc;
        return &m_.emplace(npc, t).first->second;
    }
    const NpcTimeline* find(uint32_t npc) const {
        auto it = m_.find(npc);
        return it == m_.end() ? nullptr : &it->second;
    }
    const std::map<uint32_t, NpcTimeline>& all() const { return m_; }
    uint64_t overflow() const { return overflow_; }

    struct Summary {
        size_t npcs = 0, with_scene = 0, valid = 0, file_missing = 0, node_missing = 0, vf13_passed = 0,
               bound_ai = 0, bound_2874 = 0, rebind_passed = 0, instance = 0;
    };
    Summary summary() const {
        Summary s;
        for (const auto& [k, t] : m_) {
            ++s.npcs;
            if (t.scene || t.resource) ++s.with_scene;
            if (t.handle_valid) ++s.valid;
            if (t.reads && !t.handle_valid && !t.file_loaded) ++s.file_missing;
            if (t.reads && !t.handle_valid && t.file_loaded) ++s.node_missing;
            if (t.vf13_passed) ++s.vf13_passed;
            if (t.t_bind_ai) ++s.bound_ai;
            if (t.t_bind_2874) ++s.bound_2874;
            if (t.rebind_passed) ++s.rebind_passed;
            if (t.t_instance) ++s.instance;
        }
        return s;
    }

private:
    size_t cap_;
    std::map<uint32_t, NpcTimeline> m_;
    uint64_t overflow_ = 0;
};

// "+12.345 s" / "-0.082 s" between two QPC microsecond times (0 = unknown: "?").
inline std::string rel_text(uint64_t t, uint64_t ref) {
    if (!t || !ref) return "?";
    const double d = (static_cast<double>(t) - static_cast<double>(ref)) / 1e6;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%+.3f s", d);
    return buf;
}

// ---------------------------------------------------------------- G3: X, the host character's manager on the guest

struct XSnapshot {
    uint32_t owner = 0;         // the character key it was looked up for
    uintptr_t actor = 0, manager = 0, manager_vt = 0, stages = 0, stages_vt = 0;
    uint32_t stage_count = 0;   // u32 [[X+0x148]+0x20] (the vector at +0x18)
    uintptr_t first[8] = {};    // the vector's first elements
    int32_t i174 = -1;          // u32 X+0x174 (entries of the loading-task map: nodes +0x188), -1 unreadable
    int ready = -1;             // u8 X+0x231, -1 unreadable
    bool operator==(const XSnapshot& o) const {
        if (owner != o.owner || actor != o.actor || manager != o.manager || stages != o.stages ||
            stage_count != o.stage_count || i174 != o.i174 || ready != o.ready) {
            return false;
        }
        for (size_t i = 0; i < 8; ++i) {
            if (first[i] != o.first[i]) return false;
        }
        return true;
    }
};

}  // namespace cdmp::session2::scd
