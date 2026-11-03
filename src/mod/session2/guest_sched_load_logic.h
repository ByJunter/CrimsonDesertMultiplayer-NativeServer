#pragma once
// P8 guest_schedule_load (docs/research/pendientes/p8-impl-horarios.md): [session2] guest_schedule_load =
// 0 | probe | 1, GUEST. The pure logic, with no game access (the tests run it on fake memory and on 2005 records
// of the session-6 captures).
//
// The fault [V] (p8-arquitectura-observador.md §2.4, session 8): pa::NPCScheduleManager ([exe+0x6C8CDA0], one per
// process) keeps the NPCScheduleData_File's in its container [mgr+8]; only 0x1864140 fills it, and only server code
// calls it. In option D the guest's own server never runs, so the container stays empty: 477+ schedule NPCs of the
// host's world resolve no node (G1 "FILE NOT LOADED"), 2335 finds no file (0xA9FEE0 only looks up).
//
// The loader [V static, capstone, p8-impl-horarios.md §1]:
//   0x1864140(mgr, Handle* out, const String* name, u8 unused) -> out
//     key = 0x1364780(base_without_ext(*name)) (0x1422F90; 0x1364780 = lookup3 hashlittle, initval 0xC5EDE);
//     the container's 0x186A9A0 (find, its RW lock) -> out; else 0x18643B0(mgr, out, "sequencer/binary__/" + name, 0):
//     the file system [mgr+0x90]->vf40 reads the whole file (synchronous), new 0xE0-byte file on the game heap
//     (0x1447EFA78 / 0x1447EF9B0 by the TLS byte +0x1FD), deserialize 0x18610E0, insert 0x186AB90 (-> .xtls, the
//     container's lock). No server state: no TLS realm byte +0x1EC, no ServerModule; only `mgr` and its fs.
//   The name: the server's callers pass &entry+0x10 of the manager's schedule-context table (mgr+0x10, filled at
//     every start by the CLIENT's NPCScheduleManagerLoadAsync too, from "scheduleContext.paschedulectx"), keyed by
//     the same u32 (0x281B700: r14 = entry + 0x10 -> 0x1864140's r8; 0xA9FEE0 reads the same table by the 2335's
//     resource). So: key -> context entry -> its name -> the native loader, the exact call the server makes.
//   The handle: released with 0x38AD90(&handle) (the container keeps its own reference). Session 12: for a key a
//   2335 names it is kept until that 2335 ran (HoldTable below), so the idle sweep cannot evict the file meanwhile.
//
// Where the mod calls it (guest_sched_load.cpp): only on the client's NetworkThread, in deliver_all, right BEFORE
// the host's packet that names the file is handed to ClientNet::OnRecvPacket (the main thread handles it later):
// 2335 (resource at +9), 2874 (+8) and every NPC record of a 2005 (any u32 of the record that is a key of the
// context table: offline, all 461 session-8 NPCs with a schedule resource were found this way). It is the order a
// single-player process has (its server loads before it sends). Presence is asked with the native lookup 0x186A810
// (the container's reader lock); a file being unloaded is NOT present (review L4: decided again at the end of the
// packet, then retried; never loaded over). Session 13: a key the packet names is never deferred by a count (only by
// generous time caps per packet and per second); a key that is not in the container when its packet is delivered is
// counted LATE (should be 0). Keys that cannot be loaded now (a failure, a changed context table, beyond a cap) wait
// in a retry set worked through after each packet's own keys, under time budgets. The lookups 0x1869D70 / 0x1862A80
// are only watched (entry mid hooks, read-only, 1 call in 8 sampled): they verify the result ("found after our
// load") and catch what the scan missed. gsl::process_key is the whole
// decision, on an environment the tests replace with a stub.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>

namespace cdmp::session2::gsl {

// ---------------------------------------------------------------- the key

enum class Mode : uint8_t { Off, Probe, On };
inline Mode parse_mode(std::string_view v, bool* bad = nullptr) {
    if (bad) *bad = false;
    if (v.empty() || v == "0") return Mode::Off;
    if (v == "probe") return Mode::Probe;
    if (v == "1") return Mode::On;
    if (bad) *bad = true;
    return Mode::Off;
}
inline const char* mode_name(Mode m) {
    switch (m) {
        case Mode::Off: return "off";
        case Mode::Probe: return "PROBE (decides and counts; nothing is loaded)";
        case Mode::On: return "ON";
    }
    return "?";
}

// ---------------------------------------------------------------- the name -> key hash (0x1422F90 + 0x1364780)

inline constexpr uint32_t rot(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }

// Bob Jenkins' lookup3 hashlittle, byte-wise (the result of the game's aligned path on little endian).
// 0x1364780: a = b = c = 0xDEADBEEF + len + initval (lea ebx,[rdx-0x2145E233]: initval 0xC5EDE), mix with
// rotations 4/6/8/16/19/4, final 14/11/25/16/4/14/24, returns c.
inline uint32_t hashlittle(const uint8_t* k, size_t length, uint32_t initval) {
    uint32_t a, b, c;
    a = b = c = 0xDEADBEEFu + static_cast<uint32_t>(length) + initval;
    auto u = [&](size_t i) { return static_cast<uint32_t>(k[i]); };
    while (length > 12) {
        a += u(0) + (u(1) << 8) + (u(2) << 16) + (u(3) << 24);
        b += u(4) + (u(5) << 8) + (u(6) << 16) + (u(7) << 24);
        c += u(8) + (u(9) << 8) + (u(10) << 16) + (u(11) << 24);
        a -= c; a ^= rot(c, 4); c += b;
        b -= a; b ^= rot(a, 6); a += c;
        c -= b; c ^= rot(b, 8); b += a;
        a -= c; a ^= rot(c, 16); c += b;
        b -= a; b ^= rot(a, 19); a += c;
        c -= b; c ^= rot(b, 4); b += a;
        length -= 12;
        k += 12;
    }
    switch (length) {
        case 12: c += u(11) << 24; [[fallthrough]];
        case 11: c += u(10) << 16; [[fallthrough]];
        case 10: c += u(9) << 8; [[fallthrough]];
        case 9: c += u(8); [[fallthrough]];
        case 8: b += u(7) << 24; [[fallthrough]];
        case 7: b += u(6) << 16; [[fallthrough]];
        case 6: b += u(5) << 8; [[fallthrough]];
        case 5: b += u(4); [[fallthrough]];
        case 4: a += u(3) << 24; [[fallthrough]];
        case 3: a += u(2) << 16; [[fallthrough]];
        case 2: a += u(1) << 8; [[fallthrough]];
        case 1: a += u(0); break;
        case 0: return c;
    }
    c ^= b; c -= rot(b, 14);
    a ^= c; a -= rot(c, 11);
    b ^= a; b -= rot(a, 25);
    c ^= b; c -= rot(b, 16);
    a ^= c; a -= rot(c, 4);
    b ^= a; b -= rot(a, 14);
    c ^= b; c -= rot(b, 24);
    return c;
}
inline constexpr uint32_t kNameHashInit = 0xC5EDE;
inline constexpr size_t kBaseNameMax = 0x104;  // strncpy_s into 0x105 bytes, _TRUNCATE

// 0x1422F90(src, dst, 0x105): after the last '\\' or '/', at most 0x104 characters, cut at the last '.'.
inline std::string_view base_without_ext(std::string_view path) {
    const size_t sep = path.find_last_of("\\/");
    std::string_view s = sep == std::string_view::npos ? path : path.substr(sep + 1);
    if (s.size() > kBaseNameMax) s = s.substr(0, kBaseNameMax);
    const size_t dot = s.rfind('.');
    if (dot != std::string_view::npos) s = s.substr(0, dot);
    return s;
}
// The u32 0x1864140 / 0x18643B0 file the name under (the container's key, the 2335's resource).
inline uint32_t schedule_key_of_name(std::string_view name) {
    const std::string_view b = base_without_ext(name);
    return hashlittle(reinterpret_cast<const uint8_t*>(b.data()), b.size(), kNameHashInit);
}

// ---------------------------------------------------------------- the manager (build 1.0.0.2976)

struct Layout {
    static constexpr uintptr_t kManagerSlot = 0x6C8CDA0;   // exe+..: pa::NPCScheduleManager* (ctor 0x1861F00)
    static constexpr uintptr_t kClientRootSlot = 0x6D69518;  // [..]+0x40: the manager the client's 0xA9FEE0 uses
    static constexpr uintptr_t kServerRootSlot = 0x6D69520;  // [..]+0x40: the one the server uses (TLS +0x1EC = 1)
    static constexpr uint32_t kRoot_Manager = 0x40;
    static constexpr uintptr_t kVtManager = 0x57DB090;     // pa::NPCScheduleManager (RTTI; written at 0x1861F27)
    static constexpr uint32_t kManager_Container = 0x08;   // ThreadSafeRefCountedContainer<u32, NPCScheduleData_File>
    static constexpr uint32_t kManager_FileSystem = 0x90;  // 0x18643B0: [mgr+0x90]->vf40 reads the file
    // The schedule-context table, inline at mgr+0x10 (0x1862F53 deserializes it; 0x281B78D / 0xA9FF4D read it).
    static constexpr uint32_t kCtx_Buckets = 0x10, kCtx_Used = 0x14, kCtx_Array = 0x20, kCtx_Entries = 0x28;
    // Its generation: +0xC of the table (inc at 0x186C036 per insert and at 0x1863190 after a whole fill; -1 from the ctor).
    static constexpr uint32_t kCtx_Gen = 0x1C;
    static constexpr uint32_t kBucketSize = 0x100, kBucketMax = 31, kEntry_Key = 0x04;
    static constexpr uint32_t kEntry_Name = 0x10;  // a String: [[entry+0x10]] = char* (0x1864140: mov rcx,[r8]; mov rcx,[rcx])
    static constexpr uint32_t kMaxBuckets = 1u << 20;
};

// Mem: bool u8(uintptr_t, uint8_t&), u32(uintptr_t, uint32_t&), ptr(uintptr_t, uintptr_t&): checked reads.
// 0x281B78D..0x281B7D0 replayed: the context entry of `key`, 0 when absent / unreadable.
template <class Mem>
uintptr_t ctx_find(const Mem& m, uintptr_t mgr, uint32_t key) {
    using L = Layout;
    uint32_t used = 0, buckets = 0;
    uintptr_t array = 0, entries = 0;
    if (!mgr || !key || !m.u32(mgr + L::kCtx_Used, used) || !used || !m.u32(mgr + L::kCtx_Buckets, buckets) ||
        !buckets || buckets > L::kMaxBuckets || !m.ptr(mgr + L::kCtx_Array, array) || !array ||
        !m.ptr(mgr + L::kCtx_Entries, entries) || !entries) {
        return 0;
    }
    const uintptr_t bucket = array + static_cast<uintptr_t>(key % buckets) * L::kBucketSize;
    uint32_t count = 0;
    if (!m.u32(bucket, count)) return 0;
    for (uint32_t i = 0; i < count && i < L::kBucketMax; ++i) {
        uint32_t k = 0, index = 0, ek = 0;
        uintptr_t entry = 0;
        if (!m.u32(bucket + 8 + 8 * i, k) || k != key) continue;
        if (!m.u32(bucket + 0xC + 8 * i, index) || !m.ptr(entries + 8 * static_cast<uintptr_t>(index), entry) ||
            !entry || !m.u32(entry + L::kEntry_Key, ek) || ek != key) {
            continue;
        }
        return entry;
    }
    return 0;
}

// Every key of the context table (bucket walk), at most `max`. False: unreadable / empty.
template <class Mem>
bool ctx_keys(const Mem& m, uintptr_t mgr, std::vector<uint32_t>& out, size_t max = 65536) {
    using L = Layout;
    uint32_t used = 0, buckets = 0;
    uintptr_t array = 0;
    if (!mgr || !m.u32(mgr + L::kCtx_Used, used) || !used || !m.u32(mgr + L::kCtx_Buckets, buckets) || !buckets ||
        buckets > L::kMaxBuckets || !m.ptr(mgr + L::kCtx_Array, array) || !array) {
        return false;
    }
    for (uint32_t b = 0; b < buckets && out.size() < max; ++b) {
        uint32_t count = 0;
        const uintptr_t bucket = array + static_cast<uintptr_t>(b) * L::kBucketSize;
        if (!m.u32(bucket, count)) return false;
        for (uint32_t i = 0; i < count && i < L::kBucketMax && out.size() < max; ++i) {
            uint32_t k = 0;
            if (m.u32(bucket + 8 + 8 * i, k) && k) out.push_back(k);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return !out.empty();
}

// The entry's name: String field -> StringData* -> char*, copied (NUL excluded) into buf.
struct NameRead {
    bool ok = false;       // a NUL-terminated string within the buffer
    size_t len = 0;
    uintptr_t data = 0;    // StringData*
    uintptr_t chars = 0;   // char*
};
template <class Mem>
NameRead read_name(const Mem& m, uintptr_t entry, char* buf, size_t cap) {
    NameRead r;
    if (!entry || cap < 2 || !m.ptr(entry + Layout::kEntry_Name, r.data) || !r.data || !m.ptr(r.data, r.chars) ||
        !r.chars) {
        return r;
    }
    for (size_t i = 0; i + 1 < cap; ++i) {
        uint8_t c = 0;
        if (!m.u8(r.chars + i, c)) return r;
        if (!c) {
            buf[i] = 0;
            r.len = i;
            r.ok = i > 0;
            return r;
        }
        buf[i] = static_cast<char>(c);
    }
    buf[cap - 1] = 0;
    return r;  // too long for us: not ok
}

// ---------------------------------------------------------------- the keys of the context table (immutable)

// Built once on a mod thread from ctx_keys; read by the NetworkThread without a lock (published by pointer).
class KeySet {
public:
    KeySet() = default;
    explicit KeySet(std::vector<uint32_t> keys) : keys_(std::move(keys)) {
        std::sort(keys_.begin(), keys_.end());
        keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());
        for (uint32_t k : keys_) hi_[k >> 16 >> 6] |= 1ull << ((k >> 16) & 63);
    }
    bool has(uint32_t k) const {
        if (!(hi_[k >> 16 >> 6] >> ((k >> 16) & 63) & 1)) return false;  // ~N/65536 pass this
        return std::binary_search(keys_.begin(), keys_.end(), k);
    }
    size_t size() const { return keys_.size(); }
    std::span<const uint32_t> keys() const { return keys_; }

private:
    std::vector<uint32_t> keys_;
    uint64_t hi_[1024] = {};  // 65536 bits: one per (key >> 16)
};

// ---------------------------------------------------------------- keys to try again (the NetworkThread only)

// A small set of keys to try again on a later delivery (budget spent, the context table changed, a failed load,
// keys beyond a packet's cap). Single owner (the NetworkThread): not thread-safe on purpose. Full: counted.
template <size_t N>
class RetrySet {
public:
    bool add(uint32_t k) {
        if (!k) return false;
        for (uint32_t i = 0; i < n_; ++i) {
            if (keys_[i] == k) return false;
        }
        if (n_ >= N) {
            ++dropped_;
            return false;
        }
        keys_[n_++] = k;
        return true;
    }
    bool remove(uint32_t k) {
        for (uint32_t i = 0; i < n_; ++i) {
            if (keys_[i] == k) {
                keys_[i] = keys_[--n_];
                return true;
            }
        }
        return false;
    }
    bool has(uint32_t k) const {
        for (uint32_t i = 0; i < n_; ++i) {
            if (keys_[i] == k) return true;
        }
        return false;
    }
    // Up to `max` keys, oldest first (a copy: the caller may add / remove while it works through them).
    uint32_t snapshot(uint32_t* out, uint32_t max) const {
        const uint32_t n = n_ < max ? n_ : max;
        for (uint32_t i = 0; i < n; ++i) out[i] = keys_[i];
        return n;
    }
    uint32_t size() const { return n_; }
    uint64_t dropped() const { return dropped_; }
    static constexpr size_t capacity() { return N; }

private:
    uint32_t keys_[N] = {};
    uint32_t n_ = 0;
    uint64_t dropped_ = 0;
};

// ---------------------------------------------------------------- the keys a host packet names

inline uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
inline uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

inline constexpr uint16_t kOpActorLoad = 2005, kOpScheduleCreate = 2335, kOpInitSchedule = 2874;
inline constexpr size_t kHeader = 5;
inline constexpr size_t kScheduleCreateResource = 9, kScheduleCreateMin = 22;  // scene_diag_logic.h
inline constexpr size_t kInitScheduleResource = 8, kInitScheduleMin = 24;
// Session 8's first batch named 25; session 13: 686 keys over 13234 x 2005 (at most a few per packet). Session 13
// (p8-impl-horarios.md §11): every key a packet names is loaded before it is delivered, so the cap is the most a
// 2005 of kMaxRecords records could plausibly name; more go to the spill set and count as LATE.
inline constexpr uint32_t kMaxKeysPerPacket = 512;
inline constexpr uint32_t kMaxRecords = 256;
inline constexpr size_t kRecordKeyFrom = 5;  // after u8 kind | u32 actor key
inline constexpr size_t kRetryKeys = 256;
using Spill = RetrySet<kRetryKeys>;

struct PacketKeys {
    uint16_t op = 0;
    bool ok = false;           // a packet this module reads, with at least one whole record / field
    bool untiled = false;      // 2005: the record lengths do not tile the payload (the keys before the break kept)
    uint32_t keys[kMaxKeysPerPacket] = {};
    uint32_t n = 0;
    uint32_t spilled = 0;      // distinct keys beyond kMaxKeysPerPacket (into `spill` when given)
    uint32_t npc_records = 0;  // 2005: NPC records scanned
    uint32_t hits = 0;         // 2005: positions whose u32 is a context key
};
inline bool add_key(PacketKeys& r, uint32_t k, Spill* spill) {
    if (!k) return false;
    for (uint32_t i = 0; i < r.n; ++i) {
        if (r.keys[i] == k) return false;
    }
    if (r.n >= kMaxKeysPerPacket) {
        if (spill && !spill->has(k) && spill->add(k)) ++r.spilled;
        return false;
    }
    r.keys[r.n++] = k;
    return true;
}

// The keys this packet names (no allocation: the NetworkThread). 2335 / 2874: their resource field; 2005: every
// u32 at any offset >= 5 of an NPC record (key class 0xB) that is a context key (set may be null: none). A 2005
// whose lengths stop tiling keeps the keys of its whole records before the break (ok, untiled).
inline PacketKeys packet_keys(std::span<const uint8_t> packet, const KeySet* set, Spill* spill = nullptr) {
    PacketKeys r;
    if (packet.size() < kHeader) return r;
    r.op = rd16(packet.data());
    const uint8_t* p = packet.data() + kHeader;
    const size_t size = packet.size() - kHeader;
    switch (r.op) {
        case kOpScheduleCreate:
            if (size < kScheduleCreateMin) return r;
            r.ok = true;
            add_key(r, rd32(p + kScheduleCreateResource), spill);
            return r;
        case kOpInitSchedule:
            if (size < kInitScheduleMin) return r;
            r.ok = true;
            add_key(r, rd32(p + kInitScheduleResource), spill);
            return r;
        case kOpActorLoad: {
            size_t o = 0;
            uint32_t records = 0;
            while (o < size) {
                if (o + 2 > size || records >= kMaxRecords) {
                    r.untiled = true;
                    break;
                }
                const size_t n = rd16(p + o);
                if (n < 5 || o + 2 + n > size) {
                    r.untiled = true;
                    break;
                }
                const uint8_t* rec = p + o + 2;
                const uint32_t actor = rd32(rec + 1);
                ++records;
                if ((actor >> 28) == 0xB && set && set->size()) {
                    ++r.npc_records;
                    for (size_t i = kRecordKeyFrom; i + 4 <= n; ++i) {
                        const uint32_t k = rd32(rec + i);
                        if (k != actor && set->has(k)) {
                            ++r.hits;
                            add_key(r, k, spill);
                        }
                    }
                }
                o += 2 + n;
            }
            r.ok = records > 0;
            return r;
        }
        default:
            return r;
    }
}

// ---------------------------------------------------------------- the per-key book (lock-free, fixed)

enum class KeyState : uint8_t {
    Unknown,   // not decided yet
    Present,   // the file was already in the container when a packet named it (natively or before)
    Would,     // probe: would have been loaded
    Loaded,    // loaded by us and found in the container right after
    Failed,    // the loader gave nothing / the container still lacks it (retried up to kMaxAttempts)
    NoCtx,     // no schedule-context entry yet (decided again on the next packet: the table may be refilled)
    BadName,   // the entry's name is unreadable or does not hash to the key (latched)
    Count
};
inline const char* state_name(KeyState s) {
    switch (s) {
        case KeyState::Unknown: return "unknown";
        case KeyState::Present: return "present";
        case KeyState::Would: return "would load";
        case KeyState::Loaded: return "LOADED";
        case KeyState::Failed: return "FAILED";
        case KeyState::NoCtx: return "no context entry";
        case KeyState::BadName: return "bad name";
        default: return "?";
    }
}
inline constexpr uint8_t kMaxAttempts = 3;

// Per key: its state (written by the NetworkThread only) and lookup counters (any thread). Open addressing,
// at most kMaxProbe slots per call; a full table stops tracking new keys (the OverflowBook takes them).
template <size_t N>
class KeyBook {
    static_assert(N >= 16 && (N & (N - 1)) == 0, "N must be a power of two");

public:
    struct Slot {
        std::atomic<uint32_t> key{0};
        std::atomic<uint8_t> state{0};
        std::atomic<uint8_t> attempts{0};
        std::atomic<uint8_t> first_miss_noted{0};
        std::atomic<uint32_t> wanted{0};          // packets that named it
        std::atomic<uint32_t> found{0};           // sampled lookups (0x1869D70 / 0x1862A80) that found the file
        std::atomic<uint32_t> missing{0};         // sampled lookups that did not
        std::atomic<uint32_t> found_after_load{0};
        std::atomic<uint64_t> loaded_us{0};
        // Session 12 (the hold, see HoldTable): written by the NetworkThread, except stage_* (the lookup hook).
        std::atomic<uint8_t> by2335{0};            // a 2335 named it at least once
        std::atomic<uint8_t> held{0};              // we hold a handle (a reference) on its file right now
        std::atomic<uint8_t> hold_end{0};          // how the last hold ended (HoldEnd + 1; 0: never held)
        std::atomic<uint64_t> named2335_us{0};     // the last 2335 that named it, before delivery
        std::atomic<uint64_t> stage_lookup_us{0};  // the last 0x1862A80 from 0xA9FEE0 (the 2335 ran) for it
        std::atomic<uint32_t> stage_found{0}, stage_missing{0};
    };
    static constexpr uint32_t kMaxProbe = 32;

    // The key's slot (claimed when new and `claim`), or nullptr (0, or no room within kMaxProbe).
    Slot* get(uint32_t key, bool claim = true) {
        if (!key) return nullptr;
        size_t h = (key * 0x9E3779B1u) & (N - 1);
        for (uint32_t i = 0; i < kMaxProbe; ++i, h = (h + 1) & (N - 1)) {
            Slot& s = slots_[h];
            uint32_t k = s.key.load(std::memory_order_acquire);
            if (k == key) return &s;
            if (k == 0) {
                if (!claim) return nullptr;
                if (s.key.compare_exchange_strong(k, key, std::memory_order_acq_rel)) {
                    used_.fetch_add(1, std::memory_order_relaxed);
                    return &s;
                }
                if (k == key) return &s;
            }
        }
        if (claim) untracked_.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    KeyState state(const Slot* s) const {
        return s ? static_cast<KeyState>(s->state.load(std::memory_order_acquire)) : KeyState::Unknown;
    }
    static void set_state(Slot* s, KeyState v) {
        if (s) s->state.store(static_cast<uint8_t>(v), std::memory_order_release);
    }
    uint32_t used() const { return used_.load(std::memory_order_relaxed); }
    uint64_t untracked() const { return untracked_.load(std::memory_order_relaxed); }
    const Slot& at(size_t i) const { return slots_[i]; }
    static constexpr size_t capacity() { return N; }

private:
    Slot slots_[N];
    std::atomic<uint32_t> used_{0};
    std::atomic<uint64_t> untracked_{0};
};

// The keys the KeyBook had no room for (the NetworkThread only): their state and attempts. When this is full
// too, the key is untracked: its failures go to one shared counter, and after kUntrackedFailures of them no
// untracked key is tried again (a full book must not mean endless retries).
template <size_t N>
class OverflowBook {
public:
    struct Entry {
        uint32_t key = 0;
        KeyState state = KeyState::Unknown;
        uint8_t attempts = 0;
    };
    Entry* get(uint32_t key) {
        if (!key) return nullptr;
        for (size_t i = 0; i < n_; ++i) {
            if (e_[i].key == key) return &e_[i];
        }
        if (n_ >= N) return nullptr;
        e_[n_].key = key;
        return &e_[n_++];
    }
    size_t used() const { return n_; }

private:
    Entry e_[N] = {};
    size_t n_ = 0;
};
inline constexpr uint32_t kUntrackedFailures = 16;

// ---------------------------------------------------------------- the load budget (the NetworkThread only)

// At most max_us of load time per window (session 13: time, not a count), CHECKED BEFORE each load: one load that
// starts inside the budget can still take longer (a slow disk) - the histogram and the latch below see it. Every
// load spends it; only the retry set's keys are gated by it (process_key).
class Budget {
public:
    constexpr Budget(uint64_t max_us, uint64_t window_us) : max_us_(max_us), window_us_(window_us) {}
    bool allow(uint64_t now_us) { return allow_up_to(now_us, max_us_); }
    // The same window with another ceiling (review of b90e32a, M3: the packet's own keys, kOwnWindowLoadUs).
    bool allow_up_to(uint64_t now_us, uint64_t max_us) {
        roll(now_us);
        return spent_us_ < max_us;
    }
    void spend(uint64_t now_us, uint64_t us) {
        roll(now_us);
        ++loads_;
        spent_us_ += us;
    }
    uint32_t loads_in_window() const { return loads_; }
    uint64_t us_in_window() const { return spent_us_; }

private:
    void roll(uint64_t now_us) {
        const uint64_t w = window_us_ ? now_us / window_us_ : 0;
        if (w != window_) {
            window_ = w;
            loads_ = 0;
            spent_us_ = 0;
        }
    }
    uint64_t max_us_, window_us_;
    uint64_t window_ = ~0ull;
    uint32_t loads_ = 0;
    uint64_t spent_us_ = 0;
};
// The budgets (p8-impl-horarios.md §3.2, §11). Session 13: the old COUNT budgets (16 loads per packet, 32 per
// second) deferred 956 decisions in a burst of 2335s at the field entry (32 loads in 10 ms, then every key of the
// same second waited); the 2005 that named two of those files was delivered without them and its 13 NPCs never got
// a node. Loads take 0.02-2.83 ms (85 loads, 21.6 ms in all). Now:
//  - a key the packet being delivered names ("own") is NEVER deferred by a COUNT: it is loaded before delivery,
//    the order the host's own server keeps. Its bounds are time only, for a pathological disk (~170 x session 13's
//    worst packet): kOwnPacketCapUs of loads in that packet and (review of b90e32a, M3) kOwnWindowLoadUs of loads in
//    this second; past either the rest wait in the retry set, counted LATE;
//  - the retry set's keys (failures, a changed context table, own keys beyond those caps, files being unloaded)
//    are worked through after the own keys while this packet's loads took < kRetryPacketUs and this second's
//    < kWindowLoadUs;
//  - probe for the rest of the session after one load over kLatchUs, or (M3) after kSlowLatchCount loads over
//    kSlowLoadUs (a slow disk whose loads stay under kLatchUs: Shadow's cloud disk, a cold pak cache);
//  - at most kSessionCap loads per session.
// A load is not interruptible: the worst case on the NetworkThread is a bound plus one load. BlockMeter measures the
// time the NetworkThread really spends in this module per second.
inline constexpr uint32_t kSessionCap = 1024;  // the host had 36..50 files live; session 13 loaded 85
inline constexpr uint64_t kOwnPacketCapUs = 500'000;    // own keys: no new load once this packet's loads took this
inline constexpr uint64_t kOwnWindowLoadUs = 500'000;   // own keys: no new load once this second's loads took this
inline constexpr uint64_t kRetryPacketUs = 20'000;      // retry-set keys: none once this packet's loads took this
inline constexpr uint64_t kWindowUs = 1'000'000, kWindowLoadUs = 200'000;  // retry-set keys: per second
inline constexpr uint64_t kSlowLoadUs = 50'000, kLatchUs = 250'000;
inline constexpr uint32_t kSlowLatchCount = 4;          // loads over kSlowLoadUs in a session: probe
inline constexpr uint64_t kBlockWarnUs = 250'000;       // a second with more NetworkThread time here: a [BLOCK] line
inline constexpr uint32_t kRetryPerDelivery = 8;  // retry-set keys tried after each packet's own keys

// Load durations: < 1, 5, 20, 50, 100, 250 ms and over (lock-free; read by the diagnostics thread).
struct Histogram {
    static constexpr uint64_t kEdgesUs[] = {1'000, 5'000, 20'000, 50'000, 100'000, 250'000};
    static constexpr size_t kBuckets = std::size(kEdgesUs) + 1;
    std::atomic<uint64_t> n[kBuckets] = {};
    std::atomic<uint64_t> max_us{0}, total_us{0};
    static size_t bucket(uint64_t us) {
        size_t b = 0;
        while (b < std::size(kEdgesUs) && us >= kEdgesUs[b]) ++b;
        return b;
    }
    void add(uint64_t us) {
        n[bucket(us)].fetch_add(1, std::memory_order_relaxed);
        total_us.fetch_add(us, std::memory_order_relaxed);
        uint64_t m = max_us.load(std::memory_order_relaxed);
        while (us > m && !max_us.compare_exchange_weak(m, us, std::memory_order_relaxed)) {
        }
    }
};

inline void raise_max(std::atomic<uint64_t>& m, uint64_t v) {
    uint64_t cur = m.load(std::memory_order_relaxed);
    while (v > cur && !m.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
    }
}

// Review of b90e32a (M3): the time the NetworkThread spends in this module (guest_sched_load_before_deliver: the
// holds, the presence checks, the loads), summed per second (the same windows as Budget). add() is the NetworkThread
// only; the maxima are read by the diagnostics thread (recent: taken and zeroed by each summary).
class BlockMeter {
public:
    // True the first time this second's sum reaches kBlockWarnUs (a [BLOCK] line).
    bool add(uint64_t start_us, uint64_t us) {
        const uint64_t w = start_us / kWindowUs;
        if (w != window_) {
            window_ = w;
            sum_ = 0;
            warned_ = false;
        }
        sum_ += us;
        raise_max(max_second_us, sum_);
        raise_max(recent_second_us, sum_);
        raise_max(max_call_us, us);
        if (sum_ >= kBlockWarnUs && !warned_) {
            warned_ = true;
            seconds_over.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    uint64_t second_us() const { return sum_; }  // the NetworkThread only
    uint64_t take_recent() { return recent_second_us.exchange(0, std::memory_order_relaxed); }
    std::atomic<uint64_t> max_second_us{0}, recent_second_us{0}, max_call_us{0}, seconds_over{0};

private:
    uint64_t window_ = ~0ull, sum_ = 0;
    bool warned_ = false;
};

// ---------------------------------------------------------------- the decision for one named key

enum class Why : uint8_t {
    Load,        // ON: loaded (or tried: see the state)
    WouldLoad,   // PROBE (or latched): everything holds; nothing is called
    Off,         // mode 0
    CodeOff,     // not installed, refused, or switched off after a fault
    ServerRealm, // the calling thread is the server's (TLS +0x1EC != 0): never here
    NotRemote,   // this connection does not play on the host (a local game loads its own files)
    NoManager,   // the manager / its vtable / its container / its fs / the client's manager differ
    Present,     // already in the container
    Decided,     // probe: already decided (Would) / a latched BadName
    GaveUp,      // failed kMaxAttempts times (or untracked keys after kUntrackedFailures)
    NoCtx,       // no context entry: no name (not latched)
    BadName,     // the name does not hash to the key (latched)
    Cap,         // the session's load cap
    Budget,      // a retry-set key: this window's / packet's time budget is spent: kept in the retry set
    CtxChanged,  // the context table changed between the decision and the call: kept in the retry set
    OwnCap,      // a key the packet names: its packet's / second's own-key time cap is spent: kept in the retry set, LATE
    Unloading,   // review L4: the container's file is being unloaded (+0x15): not loaded over it, decided again at the
                 // end of the packet's keys, then kept in the retry set (LATE if its packet names it)
    Count
};
inline const char* why_name(Why w) {
    switch (w) {
        case Why::Load: return "load";
        case Why::WouldLoad: return "WOULD LOAD (probe)";
        case Why::Off: return "off";
        case Why::CodeOff: return "code off";
        case Why::ServerRealm: return "server thread";
        case Why::NotRemote: return "not playing on the host";
        case Why::NoManager: return "manager check failed";
        case Why::Present: return "already present";
        case Why::Decided: return "already decided";
        case Why::GaveUp: return "gave up after failures";
        case Why::NoCtx: return "no context entry (no name)";
        case Why::BadName: return "name does not hash to the key";
        case Why::Cap: return "session cap";
        case Why::Budget: return "budget (kept for a retry)";
        case Why::CtxChanged: return "context table changed (kept for a retry)";
        case Why::OwnCap: return "own-key time cap of the packet / second (kept for a retry, LATE)";
        case Why::Unloading: return "file being unloaded (kept for a retry)";
        default: return "?";
    }
}

// The per-delivery gates, checked BEFORE anything touches the book, the budget or the retry set.
struct Gates {
    Mode mode = Mode::Off;
    bool code_ok = false;
    bool client_realm = false;
    bool remote_live = false;
    bool manager_ok = false;
};
// Why::Load = every gate passes.
inline Why early_gate(const Gates& g) {
    if (g.mode == Mode::Off) return Why::Off;
    if (!g.code_ok) return Why::CodeOff;
    if (!g.client_realm) return Why::ServerRealm;
    if (!g.remote_live) return Why::NotRemote;
    if (!g.manager_ok) return Why::NoManager;
    return Why::Load;
}

// After a call: the loader's handle and the container read back.
inline KeyState state_after_load(bool got_handle, bool present_after) {
    return got_handle && present_after ? KeyState::Loaded : KeyState::Failed;
}

// What the environment (the game, or a test's stub) tells and does. ctx() resolves the key's context entry and
// checks its name; ctx_still() re-reads the table's generation, the entry and its String right before the call.
struct CtxInfo {
    uintptr_t entry = 0;
    bool name_ok = false;
    uint32_t name_key = 0;
    uint32_t gen = 0, used = 0;  // the table's generation counter (mgr+0x1C) and count when it was read
    uintptr_t data = 0, chars = 0;
};
struct LoadOutcome {
    bool called = false;  // false: the call faulted (the environment switched the module off)
    bool got = false;     // a handle came back
    uint64_t us = 0;
    uintptr_t kept = 0;   // load(c, keep = true): the loader's handle, NOT released (the caller owns it); else 0
};
enum class Note : uint8_t {
    Gate, Would, Load, NoCtx, BadName, Budget, Cap, GaveUp, CtxChanged, Latch,
    HoldEnd,  // a held handle was given back (a = HoldEnd, b = microseconds held)
    Early,    // at load: a file the container had for a 2335 is gone before that 2335 ran (a = the state before)
    Late,     // session 13: a key the packet being delivered names is NOT in the container at its delivery
              // (a = Why, b = KeyState; key 0 and a = Why::Count: b keys beyond kMaxKeysPerPacket)
    Count
};

// ---------------------------------------------------------------- holding the file until its 2335 runs (session 12)
//
// The fault [V] (p8-s12-npc-familias.md §4, p8-impl-horarios.md §10): the client defers the 2335s until the field
// load ends (3.2 s in session 12). Meanwhile the container went from 35 files to 15: its idle sweep 0x186AE50
// [V static] evicted the files we had loaded and released at once. It only looks at files whose refcount (+0x10)
// is 1 (the container's own reference), arms holder+0x10 = now + holder+8 (seconds, x1000) on the first pass after
// a lookup zeroed it, and evicts them (+0x15 = 1, vf30) once that is past. A file anyone else holds a reference on
// is skipped.
// The native way to retain it is the handle the loader (or the lookup 0x186A810) already gives: a reference, given
// back with 0x38AD90 (atomic: +0x14 = 1 from the ctor 0x1860C10 [V]). So for a key a 2335 names, that handle is
// kept here (the NetworkThread only) and given back on the same thread with 0x38AD90 once the 2335 ran (0xA9FEE0
// looked the resource up, seen by the lookup hook) plus kHoldGraceUs, or kHoldMaxUs after the last 2335 named it.
inline constexpr size_t kHolds = 128;                  // session 12: 43 x 2335 before the field entry
inline constexpr uint64_t kHoldGraceUs = 30'000'000;   // after 0xA9FEE0 looked it up (the stage holds it by then)
inline constexpr uint64_t kHoldMaxUs = 120'000'000;    // after the last 2335 named it, if its lookup is never seen
inline constexpr uint64_t kHoldServiceUs = 1'000'000;  // the release check runs at most once a second

// Kept: we would be its last reference (not released). Dropped: taken in another connection / for another manager
// or container, or the connection ended: never given back (no 0x38AD90 on it), a counted leak (review 2997dbb).
enum class HoldEnd : uint8_t { Seen, Timeout, Kept, Dropped, Count };
inline const char* hold_end_name(HoldEnd e) {
    switch (e) {
        case HoldEnd::Seen: return "after its 2335 ran";
        case HoldEnd::Timeout: return "TIMEOUT (its 2335's lookup was never seen)";
        case HoldEnd::Kept: return "NOT released (we were its last reference: the container dropped it)";
        case HoldEnd::Dropped: return "DROPPED, not released (the connection or the manager changed: a counted leak)";
        default: return "?";
    }
}

// Who a hold belongs to: the client connection (its epoch, and whether it still plays on the host) and the schedule
// manager / container the handle came from. A hold is only given back while all of them are the same.
struct HoldOwner {
    uint64_t epoch = 0;
    uintptr_t mgr = 0, container = 0;
    bool live = false;
    bool same(const HoldOwner& o) const { return epoch == o.epoch && mgr == o.mgr && container == o.container; }
};
// Due: its 2335's lookup came after the last naming and kHoldGraceUs passed, or kHoldMaxUs since that naming.
inline bool hold_due(uint64_t named_us, uint64_t seen_us, uint64_t now, HoldEnd& why) {
    if (seen_us && seen_us >= named_us && now >= seen_us + kHoldGraceUs) {
        why = HoldEnd::Seen;
        return true;
    }
    if (now >= named_us + kHoldMaxUs) {
        why = HoldEnd::Timeout;
        return true;
    }
    return false;
}

// The handles we hold (the NetworkThread only; not thread-safe on purpose). One per key.
template <size_t N>
class HoldTable {
public:
    struct Entry {
        uint32_t key = 0;
        uintptr_t file = 0;   // the handle: a NPCScheduleData_File* with one reference that is ours
        uint64_t since_us = 0;  // held since
        uint64_t named_us = 0;  // the last 2335 that named it
        HoldOwner owner;        // the connection and manager / container it was taken in
    };
    enum class Add { Held, Refreshed, Replaced, Full };
    // Refreshed: the key is held already by the same owner (named_us moves; the caller gives the new handle back).
    // Replaced: held by ANOTHER owner: that old handle is dropped (never given back) and the new one is held.
    // Full: not held.
    Add add(uint32_t key, uintptr_t file, uint64_t now, const HoldOwner& owner) {
        for (uint32_t i = 0; i < n_; ++i) {
            if (e_[i].key != key) continue;
            if (e_[i].owner.same(owner)) {
                e_[i].named_us = now;
                return Add::Refreshed;
            }
            if (!file) return Add::Full;
            e_[i] = Entry{key, file, now, now, owner};
            return Add::Replaced;
        }
        if (!key || !file || n_ >= N) return Add::Full;
        e_[n_++] = Entry{key, file, now, now, owner};
        if (n_ > peak_) peak_ = n_;
        return Add::Held;
    }
    bool has(uint32_t key) const {
        for (uint32_t i = 0; i < n_; ++i) {
            if (e_[i].key == key) return true;
        }
        return false;
    }
    // fn(const Entry&) -> true: remove it (its handle was dealt with).
    template <class Fn>
    uint32_t sweep(Fn fn) {
        uint32_t removed = 0;
        for (uint32_t i = 0; i < n_;) {
            if (fn(static_cast<const Entry&>(e_[i]))) {
                e_[i] = e_[--n_];
                ++removed;
            } else {
                ++i;
            }
        }
        return removed;
    }
    uint32_t size() const { return n_; }
    uint32_t peak() const { return peak_; }
    static constexpr size_t capacity() { return N; }

private:
    Entry e_[N] = {};
    uint32_t n_ = 0, peak_ = 0;
};

// Everything the NetworkThread keeps (one per process). Counters are atomics: the diagnostics thread reads them.
template <size_t N>
struct Engine {
    KeyBook<N> book;
    OverflowBook<256> overflow;  // the NetworkThread only
    Spill retry;                 // the NetworkThread only
    Budget window{kWindowLoadUs, kWindowUs};
    Histogram hist;
    std::atomic<uint64_t> why[static_cast<size_t>(Why::Count)] = {};
    std::atomic<uint64_t> loads_ok{0}, loads_failed{0}, slow{0}, untracked_failures{0}, retried{0};
    // Session 13 (process_named): keys the packet being delivered names that are NOT in the container when it is
    // delivered (should stay 0), by the Why of their decision (Why::Load: loaded, the container still lacks it);
    // keys beyond kMaxKeysPerPacket; own keys decided again at once after a context change; the most load time
    // one delivery took (own + retry keys).
    std::atomic<uint64_t> late{0}, late_spilled{0}, ctx_redo{0}, packet_max_us{0};
    std::atomic<uint64_t> late_why[static_cast<size_t>(Why::Count)] = {};
    // Review L4: decisions that met a file being unloaded; own keys decided again at the end of their packet.
    std::atomic<uint64_t> unloading{0}, unloading_rechecked{0};
    BlockMeter block;  // review M3: the NetworkThread's time in this module per second
    std::atomic<uint32_t> loads_total{0};
    std::atomic<bool> latched{false};
    // The hold (session 12). holds and next_service_us: the NetworkThread only; the counters: any thread.
    HoldTable<kHolds> holds;
    uint64_t next_service_us = 0;
    std::atomic<uint32_t> held_now{0}, held_peak{0};
    std::atomic<uint64_t> holds_taken{0}, holds_refreshed{0}, holds_full{0}, holds_dropped{0}, early{0};
    std::atomic<uint64_t> hold_ends[static_cast<size_t>(HoldEnd::Count)] = {};
    void count(Why w) { why[static_cast<size_t>(w)].fetch_add(1, std::memory_order_relaxed); }
};

// A handle the environment kept for `key` (present(key, &h) / load(c, true)), taken just now on this thread: into
// the hold table, or given back at once as the module always did (release(h, true): even if it were the last
// reference). Env: HoldOwner hold_owner(); int release(uintptr_t handle, bool last_ok): 1 given back (0x38AD90),
// 0 kept (last reference while !last_ok / not a schedule file), -1 not called (the module is off).
template <size_t N, class Env>
void hold_keep(Engine<N>& eng, Env& env, uint32_t key, uintptr_t handle, typename KeyBook<N>::Slot* slot) {
    if (!handle) return;
    switch (eng.holds.add(key, handle, env.now(), env.hold_owner())) {
        case HoldTable<kHolds>::Add::Replaced:  // the old handle (another connection / manager): never given back
            eng.hold_ends[static_cast<size_t>(HoldEnd::Dropped)].fetch_add(1, std::memory_order_relaxed);
            env.note(Note::HoldEnd, key, 0, static_cast<uint64_t>(HoldEnd::Dropped), 0);
            [[fallthrough]];
        case HoldTable<kHolds>::Add::Held:
            eng.holds_taken.fetch_add(1, std::memory_order_relaxed);
            if (slot) slot->held.store(1, std::memory_order_release);
            break;
        case HoldTable<kHolds>::Add::Refreshed:
            eng.holds_refreshed.fetch_add(1, std::memory_order_relaxed);
            env.release(handle, true);
            break;
        case HoldTable<kHolds>::Add::Full:
            eng.holds_full.fetch_add(1, std::memory_order_relaxed);
            env.release(handle, true);
            break;
    }
    eng.held_now.store(eng.holds.size(), std::memory_order_relaxed);
    eng.held_peak.store(eng.holds.peak(), std::memory_order_relaxed);
}

// The NetworkThread, at most once per kHoldServiceUs (every call when `all`): gives back the handles that are due
// (all of them when `all`). A hold of another connection / manager / container, or any hold once the connection
// no longer plays on the host, is DROPPED instead: removed without calling the game (a counted leak). The caller
// checks that the game may be called (code verified, not switched off, the client's thread). Returns how many
// holds ended.
template <size_t N, class Env>
uint32_t service_holds(Engine<N>& eng, Env& env, uint64_t now, bool all = false) {
    if (!eng.holds.size()) return 0;
    if (!all && now < eng.next_service_us) return 0;
    eng.next_service_us = now + kHoldServiceUs;
    const HoldOwner cur = env.hold_owner();
    const uint32_t ended = eng.holds.sweep([&](const typename HoldTable<kHolds>::Entry& e) {
        auto* s = eng.book.get(e.key, false);
        const uint64_t seen = s ? s->stage_lookup_us.load(std::memory_order_acquire) : 0;
        HoldEnd why = HoldEnd::Timeout;
        if (!cur.live || !e.owner.same(cur)) {
            why = HoldEnd::Dropped;
        } else if (!all && !hold_due(e.named_us, seen, now, why)) {
            return false;
        }
        if (why != HoldEnd::Dropped) {
            // 0 (Kept): the reference stays ours for good (a leak of one file, never a free on this thread).
            const int r = env.release(e.file, false);
            if (r < 0) {  // the module is off: the reference stays ours (never given back), the entry goes
                eng.holds_dropped.fetch_add(1, std::memory_order_relaxed);
                if (s) s->held.store(0, std::memory_order_release);
                return true;
            }
            if (r == 0) why = HoldEnd::Kept;
        }
        eng.hold_ends[static_cast<size_t>(why)].fetch_add(1, std::memory_order_relaxed);
        env.note(Note::HoldEnd, e.key, 0, static_cast<uint64_t>(why), now > e.since_us ? now - e.since_us : 0);
        if (s) {
            s->hold_end.store(static_cast<uint8_t>(static_cast<uint8_t>(why) + 1), std::memory_order_relaxed);
            s->held.store(0, std::memory_order_release);
        }
        return true;
    });
    eng.held_now.store(eng.holds.size(), std::memory_order_relaxed);
    return ended;
}

// Every hold dropped at once without calling the game (the connection ended / the module switched off). The
// NetworkThread only (the table's owner).
template <size_t N, class Env>
uint32_t drop_holds(Engine<N>& eng, Env& env, uint64_t now) {
    const uint32_t n = eng.holds.sweep([&](const typename HoldTable<kHolds>::Entry& e) {
        eng.hold_ends[static_cast<size_t>(HoldEnd::Dropped)].fetch_add(1, std::memory_order_relaxed);
        env.note(Note::HoldEnd, e.key, 0, static_cast<uint64_t>(HoldEnd::Dropped),
                 now > e.since_us ? now - e.since_us : 0);
        if (auto* s = eng.book.get(e.key, false)) {
            s->hold_end.store(static_cast<uint8_t>(static_cast<uint8_t>(HoldEnd::Dropped) + 1),
                              std::memory_order_relaxed);
            s->held.store(0, std::memory_order_release);
        }
        return true;
    });
    eng.held_now.store(0, std::memory_order_relaxed);
    return n;
}

// One delivery's loads (own and retry keys). Time only (session 13).
struct PacketBudget {
    uint32_t loads = 0;
    uint64_t us = 0;
    bool allow_own() const { return us < kOwnPacketCapUs; }
    bool allow_retry() const { return us < kRetryPacketUs; }
};

// One named key (the NetworkThread). Env: int present(u32, uintptr_t* keep) (0 absent, 1 present, 2 being
// unloaded; keep != nullptr and found: the lookup's handle is NOT released but stored in *keep), CtxInfo ctx(u32),
// bool ctx_still(u32, const CtxInfo&), LoadOutcome load(const CtxInfo&, bool keep), int release(uintptr_t, bool),
// uint64_t now(), void note(Note, u32 key, u16 op, u64 a, u64 b). The order: the gates (nothing touched when they
// fail), the container (a present file needs no name), the latched states, the context and its name, probe, the
// cap, the budgets, the re-check of the context right before the call, the call, the container read back.
// Session 12: for a key a 2335 names (now, or before when it comes back from the retry set) in mode 1, the handle of
// the presence check or of the load is kept in the hold table until that 2335 ran (hold_keep / service_holds).
// Session 13: own = the packet being delivered names the key (op != 0): never deferred by the time budgets, only by
// the packet's kOwnPacketCapUs (Why::OwnCap); a retry-set key (op 0) waits for the window and kRetryPacketUs
// (Why::Budget). st_out (optional): the key's state after this decision (Present / Loaded / Failed / ...).
template <size_t N, class Env>
Why process_key(Engine<N>& eng, Env& env, Gates g, uint32_t key, uint16_t op, PacketBudget& pk,
                KeyState* st_out = nullptr) {
    if (st_out) *st_out = KeyState::Unknown;
    if (!key) return Why::Decided;
    const bool own = op != 0;
    if (g.mode == Mode::On && eng.latched.load(std::memory_order_relaxed)) g.mode = Mode::Probe;
    const Why gate = early_gate(g);
    if (gate != Why::Load) {
        eng.count(gate);
        if (gate == Why::NoManager || gate == Why::ServerRealm) env.note(Note::Gate, key, op, static_cast<uint64_t>(gate), 0);
        return gate;
    }
    auto* slot = eng.book.get(key);
    typename OverflowBook<256>::Entry* ov = slot ? nullptr : eng.overflow.get(key);
    if (slot) slot->wanted.fetch_add(1, std::memory_order_relaxed);
    auto state = [&] { return slot ? eng.book.state(slot) : ov ? ov->state : KeyState::Unknown; };
    auto set_state = [&](KeyState s) {
        if (slot) KeyBook<N>::set_state(slot, s);
        else if (ov) ov->state = s;
    };
    auto finish = [&](Why w) {
        eng.count(w);
        if (st_out) *st_out = state();
        return w;
    };
    const KeyState before = state();
    // The hold: a 2335 names it now, or named it before (a key back from the retry set has op 0).
    const uint64_t named_before = slot ? slot->named2335_us.load(std::memory_order_relaxed) : 0;
    const uint64_t seen_before = slot ? slot->stage_lookup_us.load(std::memory_order_acquire) : 0;
    if (op == kOpScheduleCreate && slot) {
        slot->by2335.store(1, std::memory_order_relaxed);
        slot->named2335_us.store(env.now(), std::memory_order_relaxed);
    }
    const bool hold = g.mode == Mode::On &&
                      (op == kOpScheduleCreate || (slot && slot->by2335.load(std::memory_order_relaxed)));
    uintptr_t kept = 0;
    const int presence = env.present(key, hold ? &kept : nullptr);
    if (presence == 2 && !kept) {
        // Review L4: the file is being unloaded (+0x15, the idle sweep 0x186AE50 is removing it): the game's lookups
        // no longer find it, so it is NOT present for the packet. The loader is not called over it (its find
        // 0x186A9A0 may hand back the dying file, or insert a second one while the old entry is still there [E]):
        // kept in the retry set; process_packet decides it again after the packet's other keys (the sweep is short),
        // and loads it then if it is gone. The state is left as it was.
        eng.unloading.fetch_add(1, std::memory_order_relaxed);
        eng.retry.add(key);
        return finish(Why::Unloading);
    }
    if (presence) {
        if (kept) hold_keep(eng, env, key, kept, slot);
        if (before != KeyState::Loaded) set_state(KeyState::Present);
        eng.retry.remove(key);
        return finish(Why::Present);
    }
    // The safety net (log only): the container had it (we loaded it / found it) for a 2335 whose lookup has not
    // been seen since: it was released before that 2335 ran.
    if ((before == KeyState::Loaded || before == KeyState::Present) && named_before && seen_before < named_before) {
        eng.early.fetch_add(1, std::memory_order_relaxed);
        env.note(Note::Early, key, op, static_cast<uint64_t>(before),
                 slot ? static_cast<uint64_t>(slot->hold_end.load(std::memory_order_relaxed)) : 0ull);
    }
    const uint8_t attempts = slot ? slot->attempts.load(std::memory_order_relaxed) : ov ? ov->attempts : 0;
    if (before == KeyState::BadName) return finish(Why::Decided);
    if (g.mode == Mode::Probe && before == KeyState::Would) return finish(Why::Decided);
    if (before == KeyState::Failed && attempts >= kMaxAttempts) {
        eng.retry.remove(key);
        return finish(Why::GaveUp);
    }
    if (!slot && !ov && eng.untracked_failures.load(std::memory_order_relaxed) >= kUntrackedFailures) {
        eng.retry.remove(key);
        return finish(Why::GaveUp);
    }
    const CtxInfo c = env.ctx(key);
    if (!c.entry) {
        if (before != KeyState::NoCtx) {
            set_state(KeyState::NoCtx);
            env.note(Note::NoCtx, key, op, 0, 0);
        }
        return finish(Why::NoCtx);
    }
    if (!c.name_ok) {
        set_state(KeyState::BadName);
        eng.retry.remove(key);
        env.note(Note::BadName, key, op, c.name_key, 0);
        return finish(Why::BadName);
    }
    if (g.mode == Mode::Probe) {
        set_state(KeyState::Would);
        env.note(Note::Would, key, op, c.entry, 0);
        return finish(Why::WouldLoad);
    }
    if (eng.loads_total.load(std::memory_order_relaxed) >= kSessionCap) {
        env.note(Note::Cap, key, op, 0, 0);
        return finish(Why::Cap);
    }
    if (own ? (!pk.allow_own() || !eng.window.allow_up_to(env.now(), kOwnWindowLoadUs))
            : (!eng.window.allow(env.now()) || !pk.allow_retry())) {
        eng.retry.add(key);
        env.note(Note::Budget, key, op, eng.window.us_in_window(), pk.us);
        return finish(own ? Why::OwnCap : Why::Budget);
    }
    if (!env.ctx_still(key, c)) {
        eng.retry.add(key);
        env.note(Note::CtxChanged, key, op, c.gen, 0);
        return finish(Why::CtxChanged);
    }
    eng.loads_total.fetch_add(1, std::memory_order_relaxed);
    const LoadOutcome o = env.load(c, hold);
    if (!o.called) return finish(Why::CodeOff);
    eng.window.spend(env.now(), o.us);
    ++pk.loads;
    pk.us += o.us;
    eng.hist.add(o.us);
    // The latch to probe (Note::Latch: a = this load, b = 0 for one load over kLatchUs, else the slow-load count).
    const uint64_t slow = o.us > kSlowLoadUs ? eng.slow.fetch_add(1, std::memory_order_relaxed) + 1 : 0;
    if (o.us > kLatchUs) {
        if (!eng.latched.exchange(true)) env.note(Note::Latch, key, op, o.us, 0);
    } else if (slow >= kSlowLatchCount) {  // review M3: a slow disk whose loads stay under kLatchUs
        if (!eng.latched.exchange(true)) env.note(Note::Latch, key, op, o.us, slow);
    }
    const bool after = env.present(key, nullptr) != 0;
    const KeyState st = state_after_load(o.got, after);
    if (o.kept) {
        if (st == KeyState::Loaded) hold_keep(eng, env, key, o.kept, slot);
        // A file the container lacks: our handle may be its ONLY reference. Given back at once as before the hold
        // (0x38AD90 then destroys it on this thread, as the loader 0x18643B0 itself does on its error paths).
        else env.release(o.kept, true);
    }
    uint8_t tries = attempts;
    if (st == KeyState::Failed) {
        tries = static_cast<uint8_t>(attempts + 1);
        if (slot) slot->attempts.store(tries, std::memory_order_relaxed);
        else if (ov) ov->attempts = tries;
        else eng.untracked_failures.fetch_add(1, std::memory_order_relaxed);
        if (tries < kMaxAttempts) eng.retry.add(key);
        else eng.retry.remove(key);
        eng.loads_failed.fetch_add(1, std::memory_order_relaxed);
    } else {
        eng.retry.remove(key);
        eng.loads_ok.fetch_add(1, std::memory_order_relaxed);
        if (slot) slot->loaded_us.store(env.now(), std::memory_order_relaxed);
    }
    set_state(st);
    env.note(Note::Load, key, op, o.us,
             static_cast<uint64_t>(st) | static_cast<uint64_t>(o.got) << 8 | static_cast<uint64_t>(after) << 9 |
                 static_cast<uint64_t>(tries) << 16 |
                 static_cast<uint64_t>(o.kept && st == KeyState::Loaded && eng.holds.has(key)) << 24);
    return finish(Why::Load);
}

// Session 13: is a key the packet being delivered names in the container at its delivery? Present, or loaded by us
// just now (Why::Load with KeyState::Loaded). Anything else, in mode 1 with the gates open and not latched to probe,
// is LATE: the game reads that packet without the file (the 2005's AI block resolves no node, for good).
inline bool in_container_at_delivery(Why w, KeyState st) {
    return w == Why::Present || (w == Why::Load && st == KeyState::Loaded);
}

// One key the packet being delivered names (op != 0), in the order of the packet: process_key with the packet's
// priority; a context table that changed right before the call is decided again at once (one more time); then the
// check above: a LATE key is counted by its reason and noted (Note::Late: a = Why, b = KeyState).
// last = false (review L4): a file being unloaded is returned as Why::Unloading without counting it LATE: the caller
// decides it again at the end of the packet (last = true).
template <size_t N, class Env>
Why process_named(Engine<N>& eng, Env& env, const Gates& g, uint32_t key, uint16_t op, PacketBudget& pk,
                  bool last = true) {
    // Only mode 1 loads: in probe, or once latched to probe, nothing is loaded by design (not LATE).
    const bool loading = early_gate(g) == Why::Load && g.mode == Mode::On && !eng.latched.load(std::memory_order_relaxed);
    KeyState st = KeyState::Unknown;
    Why w = process_key(eng, env, g, key, op, pk, &st);
    if (w == Why::CtxChanged) {
        eng.ctx_redo.fetch_add(1, std::memory_order_relaxed);
        w = process_key(eng, env, g, key, op, pk, &st);
    }
    if (w == Why::Unloading && !last) return w;
    if (loading && key && w != Why::WouldLoad && !in_container_at_delivery(w, st)) {
        eng.late.fetch_add(1, std::memory_order_relaxed);
        eng.late_why[static_cast<size_t>(w)].fetch_add(1, std::memory_order_relaxed);
        env.note(Note::Late, key, op, static_cast<uint64_t>(w), static_cast<uint64_t>(st));
    }
    return w;
}

// After a packet's own keys: up to `max` keys of the retry set (each decided again from the start, with the retry
// set's budgets: Why::Budget leaves the key there for a later delivery).
template <size_t N, class Env>
uint32_t process_retry(Engine<N>& eng, Env& env, const Gates& g, PacketBudget& pk, uint32_t max = kRetryPerDelivery) {
    if (early_gate(g) != Why::Load) return 0;
    uint32_t keys[kRetryPerDelivery * 4];
    const uint32_t n = eng.retry.snapshot(keys, max < std::size(keys) ? max : static_cast<uint32_t>(std::size(keys)));
    for (uint32_t i = 0; i < n; ++i) {
        eng.retried.fetch_add(1, std::memory_order_relaxed);
        process_key(eng, env, g, keys[i], 0, pk);
    }
    return n;
}

// The whole work before one delivery (the NetworkThread): every key the packet names first (process_named), then
// the retry set. `spilled`: keys of this packet beyond kMaxKeysPerPacket (in the retry set): counted LATE, one note.
template <size_t N, class Env>
void process_packet(Engine<N>& eng, Env& env, const Gates& g, const PacketKeys& keys, PacketBudget& pk) {
    // Review L4: the keys whose file was being unloaded are decided once more after the others (loaded if the sweep
    // removed it meanwhile; LATE, Why::Unloading, if it is still being unloaded).
    uint32_t again[32];
    uint32_t n_again = 0;
    for (uint32_t i = 0; i < keys.n; ++i) {
        if (process_named(eng, env, g, keys.keys[i], keys.op, pk, false) != Why::Unloading) continue;
        if (n_again < std::size(again)) again[n_again++] = keys.keys[i];
        else process_named(eng, env, g, keys.keys[i], keys.op, pk);  // no room: decided again right away
    }
    for (uint32_t i = 0; i < n_again; ++i) {
        eng.unloading_rechecked.fetch_add(1, std::memory_order_relaxed);
        process_named(eng, env, g, again[i], keys.op, pk);
    }
    if (keys.spilled && early_gate(g) == Why::Load && g.mode == Mode::On) {
        eng.late_spilled.fetch_add(keys.spilled, std::memory_order_relaxed);
        env.note(Note::Late, 0, keys.op, static_cast<uint64_t>(Why::Count), keys.spilled);
    }
    process_retry(eng, env, g, pk);
    uint64_t m = eng.packet_max_us.load(std::memory_order_relaxed);
    while (pk.us > m && !eng.packet_max_us.compare_exchange_weak(m, pk.us, std::memory_order_relaxed)) {
    }
}

}  // namespace cdmp::session2::gsl
