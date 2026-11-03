#pragma once
// P8 option U (docs/research/s25/mundo-escenarios-propios.md §5, u0-resultados-s28.md, "U-1"): the pure logic of
// [session2] user2_stages = 0 | probe | 1, HOST only. U-0 measured; U-1 (mode 1) gives user 2's own
// ServerSequencerStageManager its stages when it is far from the host (schedule elements only). No game access, no
// allocation in what the hooks use, so the tests run it. The hooks, the calls and the module's thread are
// user2_stages.cpp.
//
// What it answers before option U's mode 1 exists: the real state of user 2's ServerSequencerStageManager (its +0x6A0,
// lists, the schedule spawn manager's pending-2045 counter +0x90, the faction manager's pending-2046 byte +0x31), the
// manager each player's 2043/2045/2046 timers reach (ServerField::vf48 gives the field owner's), who closes stages
// with which reason (19 = 0x287D4C0's unload at the owner's load; 30 = still unknown, by return address), and the
// distance between the two players in 64 m cells computed as 0x288B7A0 does.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cdmp::session2::u2s {

// ---------------------------------------------------------------- the key

// Probe: measures and logs what mode 1 would do, changes nothing. On (U-1): arms / drains user 2's manager.
enum class Mode : uint8_t { Off, Probe, On };
struct ModeKey {
    Mode mode = Mode::Off;
    bool asked_on = false;  // kept for the callers: "1" is now mode 1 itself (never set)
    bool bad = false;       // anything else: off (logged)
};
inline ModeKey parse_mode(std::string_view v) {
    ModeKey k;
    if (v.empty() || v == "0") return k;
    if (v == "probe") {
        k.mode = Mode::Probe;
        return k;
    }
    if (v == "1") {
        k.mode = Mode::On;
        return k;
    }
    k.bad = true;
    return k;
}
inline const char* mode_name(Mode m) {
    switch (m) {
        case Mode::Off: return "0";
        case Mode::Probe: return "probe";
        case Mode::On: return "1";
        default: return "?";
    }
}

// ---------------------------------------------------------------- build 1.0.0.2976 constants

// Opcodes of the three timers (troc_messages_2976.csv).
inline constexpr uint16_t kOp2043 = 2043;  // UpdateStageOnceTimer -> .xtls 0x1082F900 -> vf48 -> 0x287DBB0(mgr)
inline constexpr uint16_t kOp2045 = 2045;  // UpdateScheduleStageOnceTimer -> .xtls 0x108309B0 -> 0x2822D00([mgr+0x148])
inline constexpr uint16_t kOp2046 = 2046;  // UpdateFactionSpawnStageOnceTimer -> .xtls 0x10835A30 -> 0x27FBD60([mgr+0x10])
// ServerSequencerStageManager (vtable 0x5B17A58).
inline constexpr uint32_t kVtStageMgr = 0x5B17A58;
inline constexpr uint32_t kVtSchedMgr = 0x5B16968;  // ServerNPCScheduleSpawnManger
// (+0x74 is not a count of active stages: S28 read 52065 there. Not read any more.)
inline constexpr uint32_t kMgr_Actor = 0x18, kMgr_Faction = 0x10, kMgr_SeqList = 0x28, kMgr_SeqCount = 0x30,
                          kMgr_KeepCount = 0xD0, kMgr_Sched = 0x148, kMgr_CellX = 0x680,
                          kMgr_CellZ = 0x684, kMgr_Preparing = 0x689, kMgr_PrepPending = 0x68A, kMgr_State = 0x6A0;
inline constexpr uint32_t kSm_Actor = 0x18, kSm_Weight = 0x20, kSm_List = 0x30, kSm_Count = 0x38, kSm_Pending = 0x90;
inline constexpr uint32_t kFm_Actor = 0x18, kFm_List = 0x20, kFm_Count = 0x28, kFm_Pending = 0x31;  // byte
// ServerNPCScheduleStage: key u64 +0x30, owner actor +0x50, schedule data -> +0x1C, NPC entries +0x138 (count
// +0x140, 0x168 bytes each, actor key i32 +0x28). The transform: the ctor (CommonNPCScheduleStage, 0x15EE420) copies
// 40 bytes from the desc's +0x1C to +0x90, which 0x288A970 builds as scale (3 floats: 1, 1, 1 - what S28 read at
// +0x90 as "(1, 1)"), rotation quaternion (0x393A90 of [elem+0x218]) and position ([elem+0x20C..+0x214]): the WORLD
// position is at +0x90 + 12 + 16 = +0xAC. The 2335 sends the same 40 bytes reordered: position at +30, rotation +42,
// scale +58 (S25: +30 = (-10681, 616, -3845), +42 = (0, 0, 0, 1), +58 = (1, 1, 1)).
inline constexpr uint32_t kSt_Key = 0x30, kSt_Owner = 0x50, kSt_Data = 0x60, kData_Id = 0x1C, kSt_Scale = 0x90,
                          kSt_Pos = 0xAC,
                          kSt_Entries = 0x138, kSt_EntryCount = 0x140, kSt_EntrySize = 0x168, kEntry_Actor = 0x28;
// Characters: key +0x60, user +0xA0 (key +0x60), components +0x68 -> sequencer +0xA8 -> manager +0x18, transform
// [[+0x68]+0x1A0]. A user's main character: [user+0xD0].
inline constexpr uint32_t kActor_Key = 0x60, kActor_User = 0xA0, kUser_Key = 0x60, kUser_Main = 0xD0,
                          kActor_Comps = 0x68, kComps_Seq = 0xA8, kSeqComp_Mgr = 0x18, kComps_Ts = 0x1A0;
// TransformSync: the local position (x, y, z floats, sector i16 x @+0xC, z @+0xE) at +0xB4 and the parent transform
// at +0xD0 (scale 3 floats, rotation quaternion x y z w, translation 3 floats, sector i16 x @+0x28, z @+0x2A):
// 0x4F8220 = local when the parent is the identity (0x3933A0), else 0x50EA80 (parent * local).
inline constexpr uint32_t kTs_Local = 0xB4, kTs_Parent = 0xD0;
// The field a TransformSync's field mover holds (TS+0x4F0, +0x10: fast_travel.cpp's kTsFieldMoverField) and the
// ServerField vtable it must have.
inline constexpr uint32_t kTs_Field = 0x500;
inline constexpr uint32_t kVtServerField = 0x5B29A60;

// ---------------------------------------------------------------- positions and cells (0x288B7A0's arithmetic)

struct SectorPos {
    float x = 0, y = 0, z = 0;
    int16_t sx = 0, sz = 0;
};
static_assert(sizeof(SectorPos) == 16, "read as TransformSync+0xB4's 16 bytes");
struct ParentXf {
    float scale[3] = {1, 1, 1};
    float q[4] = {0, 0, 0, 1};  // x y z w
    float t[3] = {0, 0, 0};
    int16_t sx = 0, sz = 0;
};
struct World {
    double x = 0, y = 0, z = 0;
};

inline bool parent_is_identity(const ParentXf& p) {
    return p.scale[0] == 1.0f && p.scale[1] == 1.0f && p.scale[2] == 1.0f && p.q[0] == 0.0f && p.q[1] == 0.0f &&
           p.q[2] == 0.0f && p.q[3] == 1.0f && p.t[0] == 0.0f && p.t[1] == 0.0f && p.t[2] == 0.0f && p.sx == 0 &&
           p.sz == 0;
}

// World position of a TransformSync: 0x4F8220 (local with sectors) or, with a parent (a rider on its mount),
// 0x50EA80: parent translation + rotate(q, scale * local). The rotation convention only moves a rider by its local
// offset on the mount (a metre or two): irrelevant for 64 m cells.
inline World world_of(const SectorPos& local, const ParentXf& parent) {
    const double lx = static_cast<double>(local.sx) * 1000.0 + local.x;
    const double ly = local.y;
    const double lz = static_cast<double>(local.sz) * 1000.0 + local.z;
    if (parent_is_identity(parent)) return {lx, ly, lz};
    const double vx = lx * parent.scale[0], vy = ly * parent.scale[1], vz = lz * parent.scale[2];
    const double qx = parent.q[0], qy = parent.q[1], qz = parent.q[2], qw = parent.q[3];
    // v' = v + 2w (q x v) + 2 q x (q x v)
    const double cx = qy * vz - qz * vy, cy = qz * vx - qx * vz, cz = qx * vy - qy * vx;
    const double ccx = qy * cz - qz * cy, ccy = qz * cx - qx * cz, ccz = qx * cy - qy * cx;
    const double rx = vx + 2.0 * (qw * cx + ccx), ry = vy + 2.0 * (qw * cy + ccy), rz = vz + 2.0 * (qw * cz + ccz);
    return {static_cast<double>(parent.sx) * 1000.0 + parent.t[0] + rx, parent.t[1] + ry,
            static_cast<double>(parent.sz) * 1000.0 + parent.t[2] + rz};
}

// One axis of 0x288B7A0's cell: (int)(w * 1/64), minus one when w < 0 (so -64.0 is cell -2, as the game does).
inline int32_t cell_axis(double w) {
    const float f = static_cast<float>(w);
    const int32_t c = static_cast<int32_t>(f * 0.015625f);
    return 0.0f <= f ? c : c - 1;
}
struct Cell {
    int32_t x = 0, z = 0;
    friend bool operator==(const Cell&, const Cell&) = default;
};
inline Cell cell_of(const World& w) { return {cell_axis(w.x), cell_axis(w.z)}; }
// A cell in one atomic word: both axes biased so a valid cell (the map is within +-3200 cells) is never 0, which
// stands for "unknown".
inline constexpr int32_t kCellBias = 0x40000000;
inline uint64_t pack_cell(Cell c) {
    return static_cast<uint64_t>(static_cast<uint32_t>(c.x + kCellBias)) << 32 | static_cast<uint32_t>(c.z + kCellBias);
}
inline Cell unpack_cell(uint64_t v) {
    return {static_cast<int32_t>(static_cast<uint32_t>(v >> 32)) - kCellBias,
            static_cast<int32_t>(static_cast<uint32_t>(v)) - kCellBias};
}
inline uint32_t cheb(Cell a, Cell b) {
    const int64_t dx = static_cast<int64_t>(a.x) - b.x, dz = static_cast<int64_t>(a.z) - b.z;
    return static_cast<uint32_t>(std::max(dx < 0 ? -dx : dx, dz < 0 ? -dz : dz));
}
// A stage position read from memory: finite and on the map (|x|, |z| < 200 km).
inline bool plausible(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::fabs(x) < 2.0e5f && std::fabs(z) < 2.0e5f;
}

// ---------------------------------------------------------------- the managers' facts (the 5 s read)

struct MgrFacts {
    bool readable = false;  // manager found (vtable, [mgr+0x18] == the character)
    uint8_t state = 0;      // +0x6A0
    uint8_t preparing = 0;  // +0x689
    uint8_t prep_pending = 0;  // +0x68A
    Cell cell{};            // +0x680/+0x684 (the last evaluated cell)
    uint32_t seq = 0, keep = 0;
    bool sm_ok = false;
    uint32_t sm_count = 0, sm_weight = 0;
    int32_t sm_pending = 0;  // pending 2045 (+0x90)
    bool fm_ok = false;
    uint32_t fm_count = 0;
    uint8_t fm_pending = 0;  // pending 2046 (+0x31)
};

// Would U-c arm this manager now (§5.4)? The facts only: nothing is decided for the game.
// S28: in the host's field user 2's manager sits in state 0 (reset, lists and pending counters at 0); state 1 (an
// unload pending) is armable the same way. State 2 is a running manager.
enum class Arm : uint8_t { Ready, Unreadable, Preparing, Running, ListsNotEmpty, NoSched, Count };
inline Arm arm_verdict(const MgrFacts& f) {
    if (!f.readable) return Arm::Unreadable;
    if (f.preparing || f.prep_pending) return Arm::Preparing;
    if (f.state != 0 && f.state != 1) return Arm::Running;
    if (!f.sm_ok) return Arm::NoSched;
    if (f.seq || f.sm_count || (f.fm_ok && f.fm_count)) return Arm::ListsNotEmpty;
    return Arm::Ready;
}
inline const char* arm_name(Arm a) {
    switch (a) {
        case Arm::Ready: return "READY (state 0/1, lists empty: 0x287F450 + the 2043 would arm it)";
        case Arm::Unreadable: return "manager unreadable";
        case Arm::Preparing: return "a preparation is running or pending (+0x689/+0x68A)";
        case Arm::Running: return "already running (state 2 or another value: see it)";
        case Arm::ListsNotEmpty: return "its lists are NOT empty (0x287F450 would orphan stages)";
        case Arm::NoSched: return "no schedule spawn manager";
        default: return "?";
    }
}

// ---------------------------------------------------------------- the would-be state machine (§5.2), probe only

struct Thresholds {
    uint32_t on_cells = 10, off_cells = 7;
    uint64_t on_dwell_us = 10'000'000, off_dwell_us = 5'000'000;
};
enum class Would : uint8_t { Off, On };
enum class Step : uint8_t { None, WouldArm, WouldDrain };
struct Machine {
    Would state = Would::Off;
    uint64_t far_since = 0, near_since = 0;  // 0 = not in that band now
};
inline uint64_t elapsed(uint64_t now, uint64_t since) { return now >= since ? now - since : 0; }
// One sample: known = both positions read AND both players in the same ServerField (S28: a would-ARM at 168 cells
// while user 2 was still in its own field); dist in cells. In the band between the thresholds both clocks stop.
inline Step step(Machine& m, bool known, uint32_t dist, uint64_t now_us, const Thresholds& t) {
    if (!known) {
        m.far_since = m.near_since = 0;
        return Step::None;
    }
    if (dist >= t.on_cells) {
        m.near_since = 0;
        if (!m.far_since) m.far_since = now_us ? now_us : 1;
        if (m.state == Would::Off && elapsed(now_us, m.far_since) >= t.on_dwell_us) {
            m.state = Would::On;
            return Step::WouldArm;
        }
        return Step::None;
    }
    m.far_since = 0;
    if (dist <= t.off_cells) {
        if (!m.near_since) m.near_since = now_us ? now_us : 1;
        if (m.state == Would::On && elapsed(now_us, m.near_since) >= t.off_dwell_us) {
            m.state = Would::Off;
            return Step::WouldDrain;
        }
        return Step::None;
    }
    m.near_since = 0;
    return Step::None;
}

// Both players' fields (TransformSync +0x500: its field mover +0x4F0, +0x10 the field), vtable ServerField checked.
enum class FieldRel : uint8_t { Unknown, Same, Different };
inline FieldRel field_rel(uintptr_t host_field, uintptr_t user2_field) {
    if (!host_field || !user2_field) return FieldRel::Unknown;
    return host_field == user2_field ? FieldRel::Same : FieldRel::Different;
}
inline const char* field_rel_name(FieldRel r) {
    switch (r) {
        case FieldRel::Same: return "the same field";
        case FieldRel::Different: return "DIFFERENT fields";
        default: return "a field unknown";
    }
}

// ---------------------------------------------------------------- who

enum class Who : uint8_t { Host, User2, Other, Unknown, Count };
inline Who who_of(uint64_t v, uint64_t host, uint64_t user2) {
    if (!v) return Who::Unknown;
    if (host && v == host) return Who::Host;
    if (user2 && v == user2) return Who::User2;
    return Who::Other;
}
inline const char* who_name(Who w) {
    switch (w) {
        case Who::Host: return "host";
        case Who::User2: return "user 2";
        case Who::Other: return "other";
        default: return "unknown";
    }
}

// The timers: the subject of the running ExecuteTroc (user2_funnels: set only for user 2's main character) and the
// manager it reached. Rows: timer (2043 / 2045 / 2046 / not a timer); columns: subject (user 2 / none) x target.
enum class Timer : uint8_t { T2043, T2045, T2046, NotTimer, Count };
inline Timer timer_of(uint16_t op) {
    switch (op) {
        case kOp2043: return Timer::T2043;
        case kOp2045: return Timer::T2045;
        case kOp2046: return Timer::T2046;
        default: return Timer::NotTimer;
    }
}
inline const char* timer_name(Timer t) {
    switch (t) {
        case Timer::T2043: return "2043";
        case Timer::T2045: return "2045";
        case Timer::T2046: return "2046";
        default: return "no timer";
    }
}
class RouteTable {
public:
    // subject_user2: the running ExecuteTroc's subject is user 2's main character; target: whose manager was reached.
    void add(Timer t, bool subject_user2, Who target) {
        n_[static_cast<size_t>(t)][subject_user2 ? 1 : 0][static_cast<size_t>(target)].fetch_add(
            1, std::memory_order_relaxed);
    }
    uint64_t get(Timer t, bool subject_user2, Who target) const {
        return n_[static_cast<size_t>(t)][subject_user2 ? 1 : 0][static_cast<size_t>(target)].load(
            std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t> n_[static_cast<size_t>(Timer::Count)][2][static_cast<size_t>(Who::Count)] = {};
};

// ---------------------------------------------------------------- (reason, return address, owner) counts

// Lock-free open addressing on a non-zero u64 key; at most N/2 keys (then counted as overflow), bounded probes.
template <size_t N>
class CountTable {
    static_assert(N >= 4 && (N & (N - 1)) == 0, "N must be a power of two");

public:
    bool add(uint64_t key) {
        if (!key) return false;
        size_t i = static_cast<size_t>((key * 0x9E3779B97F4A7C15ull) >> 40) & (N - 1);
        for (size_t probe = 0; probe < N; ++probe, i = (i + 1) & (N - 1)) {
            uint64_t k = k_[i].load(std::memory_order_acquire);
            if (k == key) {
                n_[i].fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            if (!k) {
                if (used_.load(std::memory_order_relaxed) >= N / 2) break;
                if (k_[i].compare_exchange_strong(k, key, std::memory_order_acq_rel)) {
                    used_.fetch_add(1, std::memory_order_relaxed);
                    n_[i].fetch_add(1, std::memory_order_relaxed);
                    return true;
                }
                if (k == key) {
                    n_[i].fetch_add(1, std::memory_order_relaxed);
                    return true;
                }
            }
        }
        overflow_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    struct Row {
        uint64_t key = 0, count = 0;
    };
    size_t rows(Row* out, size_t cap) const {
        size_t n = 0;
        for (size_t i = 0; i < N && n < cap; ++i) {
            const uint64_t k = k_[i].load(std::memory_order_acquire);
            if (k) out[n++] = {k, n_[i].load(std::memory_order_relaxed)};
        }
        return n;
    }
    uint64_t overflow() const { return overflow_.load(std::memory_order_relaxed); }

private:
    std::atomic<uint64_t> k_[N] = {};
    std::atomic<uint64_t> n_[N] = {};
    std::atomic<size_t> used_{0};
    std::atomic<uint64_t> overflow_{0};
};

// A close / delete key: reason (8 bits), owner (4 bits), return RVA (32 bits; 0 = outside the exe), a tag (4 bits).
inline uint64_t close_key(uint8_t tag, uint8_t reason, Who owner, uint32_t ret_rva) {
    return static_cast<uint64_t>(tag & 0xF) << 60 | static_cast<uint64_t>(static_cast<uint8_t>(owner) & 0xF) << 52 |
           static_cast<uint64_t>(reason) << 40 | ret_rva | (1ull << 59);  // bit 59: never 0
}
struct CloseKey {
    uint8_t tag = 0, reason = 0;
    Who owner = Who::Unknown;
    uint32_t ret_rva = 0;
};
inline CloseKey split_close_key(uint64_t k) {
    CloseKey c;
    c.tag = static_cast<uint8_t>(k >> 60);
    c.owner = static_cast<Who>((k >> 52) & 0xF);
    c.reason = static_cast<uint8_t>(k >> 40);
    c.ret_rva = static_cast<uint32_t>(k);
    return c;
}
inline const char* reason_name(uint8_t r) {
    switch (r) {
        case 19: return "19 (unload at the owner's load, 0x287D4C0)";
        case 25: return "25 (the collector's pass)";
        case 30: return "30 (window?)";
        case 9: return "9";
        case 14: return "14";
        default: return "";
    }
}

// The RVA of an address inside the exe (0 when outside [base, base + size)).
inline uint32_t rva_in(uint64_t addr, uint64_t base, uint64_t size) {
    return addr >= base && addr - base < size ? static_cast<uint32_t>(addr - base) : 0;
}

// ---------------------------------------------------------------- rate and line budgets per (kind, user 2)

// Each event kind has two classes: user 2 involved (its stage / manager / timer) or not, so a burst of the host's
// (the reason-19 closes of its fast travel) never eats user 2's lines.
inline constexpr size_t limit_class(size_t kind, bool user2) { return kind * 2 + (user2 ? 1 : 0); }

// Per class: at most `per_window` events per window (the hooks, lock-free; a window boundary may let a few extra
// through, never blocks) and at most `lines` log lines in all (the drain thread). What each refuses is counted.
template <size_t N>
class ClassBudget {
public:
    // Before any take (the module's start, single-threaded).
    void set(size_t c, uint32_t per_window, uint64_t lines) {
        if (c >= N) return;
        per_[c] = per_window;
        line_cap_[c] = lines;
    }
    bool take_rate(size_t c, uint64_t now_us, uint64_t window_us) {
        if (c >= N) return false;
        const uint64_t w = window_us ? now_us / window_us : 0;
        uint64_t cur = window_[c].load(std::memory_order_relaxed);
        if (cur != w && window_[c].compare_exchange_strong(cur, w, std::memory_order_relaxed)) {
            count_[c].store(0, std::memory_order_relaxed);
        }
        if (count_[c].fetch_add(1, std::memory_order_relaxed) < per_[c]) return true;
        limited_[c].fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    bool take_line(size_t c) {
        if (c >= N) return false;
        if (lines_[c].fetch_add(1, std::memory_order_relaxed) < line_cap_[c]) return true;
        over_[c].fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    uint64_t limited(size_t c) const { return c < N ? limited_[c].load(std::memory_order_relaxed) : 0; }
    uint64_t over(size_t c) const { return c < N ? over_[c].load(std::memory_order_relaxed) : 0; }
    static constexpr size_t classes() { return N; }

private:
    uint32_t per_[N] = {};
    uint64_t line_cap_[N] = {};
    std::atomic<uint64_t> window_[N] = {};
    std::atomic<uint32_t> count_[N] = {};
    std::atomic<uint64_t> limited_[N] = {};
    std::atomic<uint64_t> lines_[N] = {};
    std::atomic<uint64_t> over_[N] = {};
};

// ---------------------------------------------------------------- U-1: the element filter (U-f)

// 0x288A970(mgr, &res, stage, element, a5): [elem+8] a sequencer resource (0x287C200: a 2661 scene: shops, quests,
// weather...), else [elem+0x10] a schedule (0x28078E0: a schedule stage, 2335), else [elem+0x18] != -1 a class-2
// stage ([mgr+0x10]'s 0x1501EFB30: factions...), else nothing. U-1 lets user 2's manager start schedules only.
enum class Element : uint8_t { Sequencer, Schedule, Class2, Empty, Count };
inline Element element_of(uintptr_t res_ptr, uintptr_t schedule_ptr, int16_t class2) {
    if (res_ptr) return Element::Sequencer;
    if (schedule_ptr) return Element::Schedule;
    return class2 != -1 ? Element::Class2 : Element::Empty;
}
inline const char* element_name(Element e) {
    switch (e) {
        case Element::Sequencer: return "sequencer (2661 scene)";
        case Element::Schedule: return "schedule (2335 stage)";
        case Element::Class2: return "class-2 stage (factions)";
        case Element::Empty: return "empty";
        default: return "?";
    }
}
// Skip it (*res = 0, the original not called)? Only for user 2's manager, only in mode 1, only while U-1 has it armed
// and its character stands in a field the HOST owns (review A3: in its own field its manager is the native owner's
// and evaluates everything), never a schedule.
inline bool skip_element(bool user2_mgr, Mode mode, bool armed, bool host_field, Element e) {
    return user2_mgr && mode == Mode::On && armed && host_field && e != Element::Schedule && e != Element::Empty;
}

// ---------------------------------------------------------------- U-1: the route of user 2's timers (U-b)

// [session2] user2_timer_route (its own key, review A1): ServerField::vf48 under user 2's subject (user2_funnels), at
// the 2043/2045/2046 handlers' return addresses, in a field the HOST owns: its OWN manager, ALWAYS (armed or not).
// Natively the field owner's manager answers: user 2's periodic 2043 (+0xB = 1) then reschedules itself on the host's
// manager (0x287DBB0 re-enqueues in every state but 1): an extra host chain (S29/S30: 12 -> 18..33 per minute), and
// its 2045/2046 subtract the host's pending counters. On its own manager: state 0 -> the update exits at 0x288B7FF and
// the chain keeps running harmlessly; state 1 -> unload, no reschedule; state 2 -> its own cells.
// Probe: counted as "would". U-1 mode 1 needs it at 1.
enum class Route : uint8_t { None, Would, Own, Count };
inline Route route_of(Mode route_mode, uint16_t opcode, bool host_field) {
    if (route_mode == Mode::Off || !host_field) return Route::None;
    if (opcode != kOp2043 && opcode != kOp2045 && opcode != kOp2046) return Route::None;
    return route_mode == Mode::Probe ? Route::Would : Route::Own;
}

// ---------------------------------------------------------------- U-1: the abort criteria

// Checked every second while armed (mode 1); the first that holds stops U-1 for the rest of the run (a drain, then
// probe behaviour), with an ERR line naming it. All measured live; the thresholds are from S25/S28 (§3.3).
enum class Abort : uint8_t {
    None,
    HostTouched,     // the host's manager not in state 2 for >= 10 s outside its load window (see check_abort)
    NotRunning,      // user 2's manager not in state 2 5 s after the arm
    NoTicks,         // no 2043 reached user 2's manager for tick_timeout after a restart of its chain (S33: only
                     // then; or 3 x tick_timeout with no restart possible: no Req of user 2 to run it)
    Duplicate,       // a schedule stage of the same data alive in both managers with the players >= 8 cells apart
    Unexpected2322,  // a 2322 for user 2's character within 5 s of an arm (U-c never prepares: none expected)
    Count
};
struct AbortIn {
    bool armed = false;
    uint64_t now_us = 0;
    uint64_t arm_done_us = 0;         // when the last arm ran (0 = never)
    uint64_t last_mgr2_tick_us = 0;   // the last 2043 that reached user 2's manager
    uint64_t tick_timeout_us = 15'000'000;
    uint64_t kick_us = 0;             // the last restart of its 2043 chain after the arm (chain_kick_due), 0 = none
    uint8_t mgr2_state = 0;
    uint64_t host_not2_since_us = 0;  // 0 = the host's manager is in state 2 now
    uint64_t last_host_load_us = 0;   // the host's last owner load step (0x287DA50 on its manager)
    uint64_t last_host_prep_us = 0;   // the host's last preparation (0x287EF60 on its manager: its load's end)
    uint64_t dup_far = 0;
    uint64_t unexpected_2322 = 0;
};
inline Abort check_abort(const AbortIn& a) {
    if (!a.armed) return Abort::None;
    const auto since = [&](uint64_t t) { return a.now_us >= t ? a.now_us - t : 0; };
    // Review M2: a host load lasts from its load step (L) to its preparation (P), however long; after P its client
    // acks the 2322 and the arm puts state 2 back: 10 s for that. Plus the old 20 s after L for a load without P.
    const bool in_load = a.last_host_load_us && a.last_host_load_us > a.last_host_prep_us;
    if (a.host_not2_since_us && since(a.host_not2_since_us) >= 10'000'000 && !in_load &&
        (!a.last_host_load_us || since(a.last_host_load_us) >= 20'000'000) &&
        (!a.last_host_prep_us || since(a.last_host_prep_us) >= 10'000'000)) {
        return Abort::HostTouched;
    }
    if (a.arm_done_us && since(a.arm_done_us) >= 5'000'000 && a.mgr2_state != 2) return Abort::NotRunning;
    const uint64_t last = std::max(a.arm_done_us, a.last_mgr2_tick_us);
    if (last && a.kick_us >= last && since(a.kick_us) >= a.tick_timeout_us) return Abort::NoTicks;
    if (last && since(last) >= 3 * a.tick_timeout_us) return Abort::NoTicks;
    if (a.dup_far) return Abort::Duplicate;
    if (a.unexpected_2322) return Abort::Unexpected2322;
    return Abort::None;
}
// Review M1: the host's 2043 cadence already varies 12..33 per minute without U-1 (user 2's chain on the host's
// manager): a change is a WRN line, never an abort. True when the last minute is above max(1.5 x base, base + 5).
inline bool cadence_changed(uint64_t minute, uint64_t baseline) {
    return baseline && minute > std::max<uint64_t>(baseline * 3 / 2, baseline + 5);
}
inline const char* abort_name(Abort a) {
    switch (a) {
        case Abort::None: return "none";
        case Abort::HostTouched: return "the HOST's manager left state 2 (no host load)";
        case Abort::NotRunning: return "user 2's manager not in state 2 5 s after the arm";
        case Abort::NoTicks: return "no 2043 reached user 2's manager";
        case Abort::Duplicate: return "a DUPLICATE schedule stage with the players >= 8 cells apart";
        case Abort::Unexpected2322: return "an unexpected 2322 to user 2 after an arm";
        default: return "?";
    }
}

// The 2043 timer U-c queues for user 2's character (0x287EF60's periodic one, 0x287F0CC..0x287F18F): FB 07 | FF |
// the 8-byte zero cell ([exe+0x5545F50]) | u32 1 (+0xB: reschedule) | 00. 16 bytes.
inline constexpr size_t kTimerPayload = 0x10;
inline void timer_2043_payload(uint8_t (&p)[kTimerPayload], const uint8_t (&cell)[8]) {
    p[0] = 0xFB;
    p[1] = 0x07;
    p[2] = 0xFF;
    for (size_t i = 0; i < 8; ++i) p[3 + i] = cell[i];
    p[11] = 1;
    p[12] = 0;
    p[13] = 0;
    p[14] = 0;
    p[15] = 0;
}
// 0x287F0F9..0x287F15C: delay = [exe+0x6D06098] * 1000 / 100 (the game's own period, in ms).
// NOTE (the 53d70df bug): this is EnqueueTimer's delay ARGUMENT (raw * 1000 / 100, 0x287F139..0x287F15C: imul 0x3E8,
// then / 100), not milliseconds: live raw = 10 -> 100, and the measured period is ~10.1 s (S30 / S31 / S33 logs). Use
// timer_2043_period_ms for any time comparison.
inline uint32_t timer_2043_delay_ms(int32_t raw) { return raw > 0 ? static_cast<uint32_t>(raw) * 10u : 0u; }
// The 2043 period in milliseconds: the delay argument counts 100 ms units (100 -> ~10.1 s measured), i.e. raw seconds.
inline uint32_t timer_2043_period_ms(int32_t raw) { return raw > 0 ? static_cast<uint32_t>(raw) * 1000u : 0u; }
// A period below this is not believed (unknown): the fallback windows apply and the chain is never restarted.
inline constexpr uint32_t kMinPeriodMs = 2000;
// A 2043 chain is dead at the arm when its last rescheduling tick is 1.5 periods old or more (S33: user 2's 2437 ended
// its chain silently 0.8 s after a tick; the arm 11.4 s later took it as alive with the old 3-period / 15 s window).
inline bool chain_dead(uint64_t now_us, uint64_t last_tick_us, uint32_t period_ms) {
    // Period unknown or implausible (< 2 s): the old 15 s / 3-period window.
    const uint64_t window = period_ms >= kMinPeriodMs
                                ? static_cast<uint64_t>(period_ms) * 1500
                                : std::max<uint64_t>(15'000'000, static_cast<uint64_t>(period_ms) * 3000);
    if (!last_tick_us) return true;
    if (now_us < last_tick_us) return false;
    return now_us - last_tick_us >= window;
}

// S33: after an arm, no 2043 reached user 2's manager for 1.5 periods since the arm / its last tick: one 2043 is queued
// again ('[chain restarted]'); NoTicks aborts only after that restart. At most ONE restart per arm (the 53d70df bug:
// with a 150 ms 'period' each tick of the chains it had queued reset the silence and it queued 80 more), and never
// with the period unknown (< 2 s).
inline bool chain_kick_due(uint64_t now_us, uint64_t arm_done_us, uint64_t last_tick_us, uint32_t kicks_this_arm,
                           uint32_t period_ms) {
    if (!arm_done_us || period_ms < kMinPeriodMs || kicks_this_arm) return false;
    const uint64_t last = std::max(arm_done_us, last_tick_us);
    return now_us >= last + static_cast<uint64_t>(period_ms) * 1500;
}

// ---------------------------------------------------------------- the host's own 2043 chains (log only)
//
// Native (u0-resultados-s28.md §8.16): every ready 0x287EF60 queues a NEW periodic 2043 (+0xB = 1) without checking
// for one already running, and a chain ends only on a tick that finds no field, +0x6A0 == 1, +0x68A != 0 or the
// user not "valid logined" (0x287DBB0). The game's login loads twice (the parking field at (0,1000,0), then the save)
// and the 2601 in between resets +0x6A0 straight to 0, so the host leaves the login with TWO chains (12 a minute)
// until its first load whose state-1 window lasts a period or more.

// The [host cadence] baseline only takes a complete minute that starts >= 60 s after the host's last preparation
// or owner load (the login's partial minutes, 0..10 ticks, pulled it down to 7-8 and a native 13 raised a WRN).
inline bool baseline_minute_ok(uint64_t minute_start_us, uint64_t last_prep_us, uint64_t last_load_us) {
    const uint64_t last = std::max(last_prep_us, last_load_us);
    return !last || minute_start_us >= last + 60'000'000;
}

// How many 2043 chains are alive on one manager, from its rescheduling ticks (r8b != 0 in state 0 / 2, +0x68A 0),
// oldest first. A chain fires once per period: its next tick comes period..period + slack after its last one (the
// delay counts from the fire, so the phase only drifts later). Each tick joins the chain whose expected tick is
// closest, or starts one; a chain is alive while its last tick is within period + slack of now. Two chains half a
// second apart stay two: a tick less than period - 100 ms after a chain's last one cannot belong to it.
inline constexpr size_t kChainSlots = 16;
struct ChainCount {
    uint32_t alive = 0;  // chains with a tick within period + slack of now
    uint32_t seen = 0;   // chains in the window (ended ones too)
};
inline uint64_t chain_slack_us(uint64_t period_us) { return std::max<uint64_t>(1'000'000, period_us / 5); }
inline ChainCount count_chains(const uint64_t* ticks_us, size_t n, uint64_t period_us, uint64_t now_us) {
    ChainCount r;
    if (!period_us) return r;
    const uint64_t slack = chain_slack_us(period_us);
    const uint64_t lo = period_us > 100'000 ? period_us - 100'000 : 0;
    std::array<uint64_t, kChainSlots> last{};
    size_t chains = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint64_t t = ticks_us[i];
        if (!t) continue;
        size_t best = kChainSlots;
        uint64_t best_err = UINT64_MAX;
        for (size_t c = 0; c < chains; ++c) {
            if (t < last[c]) continue;
            const uint64_t gap = t - last[c];
            if (gap < lo || gap > period_us + slack) continue;
            const uint64_t err = gap > period_us ? gap - period_us : period_us - gap;
            if (err < best_err) {
                best_err = err;
                best = c;
            }
        }
        if (best < kChainSlots) {
            last[best] = t;
        } else if (chains < kChainSlots) {
            last[chains++] = t;
        }
    }
    r.seen = static_cast<uint32_t>(chains);
    for (size_t c = 0; c < chains; ++c) {
        if (now_us >= last[c] && now_us - last[c] <= period_us + slack) ++r.alive;
    }
    return r;
}

// ---------------------------------------------------------------- U-e: the controller at birth (user2_npc_control)
//
// S31 (u0-resultados-s28.md §8.17): the NPCs of user 2's armed stages were born with the HOST as controller
// (0x2792A70's field-owner branch). The host's client, 3.3-4 km away, ran their AI and sent StartAIFunctionReq 2291
// 'logout' (package 0x1256388E) ~5.2 s after birth: the server logged them out with reason 42 (ServerAiActorComponent
// vf69 0x2AC6C10, return 0x2AC6EA9) and the stage put a new one at its post in user 2's next 2045 (828 of 1206 births,
// a loop the guest saw as guards walking off and reappearing). U-e: at the controller write 0x2792A7B (mov
// [r14+0x90],eax; r14 the new actor, rsi the descriptor, r13 the field) eax := user 2's user key for exactly those
// NPCs, so the guest's client - next to them, with their stage bound in its own M - runs their AI.
inline constexpr uint32_t kVtDescNpcSchedule = 0x5B04CB0;  // pa::CreateServerActorDesc_NPCSchedule (vf3 0x27A6160)
inline constexpr uint32_t kActor_Ctl = 0x90, kActor_TypeInfo = 0x88, kDesc_User = 0x40, kField_Owner = 0x180;
inline constexpr uint8_t kLogoutAiReason = 42;  // ServerAiActorComponent vf69's Logout (the client's AI 'logout')

enum class Ctl : uint8_t {
    Assign,         // every condition holds
    NotTimer,       // under user 2's subject, but not its 2043 / 2045 / 2046
    NotOwnTroc,     // this Troc's vf48 did not answer with user 2's own manager (review 6: never the host's stages)
    NotArmed,       // U-1 does not have its manager armed (mode 1, not aborted)
    OtherDesc,      // the descriptor is not CreateServerActorDesc_NPCSchedule
    HasController,  // [actor+0x90] already set (the game keeps it: jne 0x2792A7B)
    DescUser,       // [desc+0x40] names a user (an explicit controller)
    HasParent,      // [actor+0xA0] (a user's child)
    NotOwnerBranch, // eax is not [field+0x180] (another branch gave it)
    NotHostField,   // the field's owner is not the host
    Type,           // not a type 3 / 6 actor
    NoKey,          // user 2's user key unknown ([pj2+0x90] 0)
    KeyMismatch,    // [pj2+0x90] differs from the session's user 2 key
    Aborted,        // user2_npc_control aborted for this run
    // S36 (npc_family_control=1): never returned by ctl_verdict; a root whose verdict was Assign but whose family
    // unit could not be admitted whole (npc_family_logic.h admit_unit): it stays with the host, its subtree too.
    Family,
    Count
};
struct CtlIn {
    uint16_t opcode = 0;  // the running ExecuteTroc's (its subject is user 2's main character: checked before)
    bool own_mgr_troc = false;  // this Troc's vf48 answered with user 2's own manager (the route or its armed tick)
    bool armed = false;
    bool desc_npc_schedule = false;
    uint32_t old_ctl = 0;     // [actor+0x90] before the write
    uint64_t desc_user = 0;   // [desc+0x40]
    uint64_t parent = 0;      // [actor+0xA0]
    uint32_t eax = 0;         // the value about to be written
    uint32_t field_owner = 0; // [field+0x180]
    bool host_field = false;
    uint8_t type = 0;         // [[actor+0x88]+1]
    uint32_t key2 = 0;        // [pj2+0x90]
    uint32_t session_key2 = 0;  // the session's user 2 key (0 = unknown: not compared)
    bool aborted = false;
};
inline Ctl ctl_verdict(const CtlIn& c) {
    if (c.opcode != kOp2043 && c.opcode != kOp2045 && c.opcode != kOp2046) return Ctl::NotTimer;
    if (!c.own_mgr_troc) return Ctl::NotOwnTroc;
    if (c.aborted) return Ctl::Aborted;
    if (!c.armed) return Ctl::NotArmed;
    if (!c.desc_npc_schedule) return Ctl::OtherDesc;
    if (c.old_ctl) return Ctl::HasController;
    if (c.desc_user) return Ctl::DescUser;
    if (c.parent) return Ctl::HasParent;
    if (!c.host_field) return Ctl::NotHostField;
    if (!c.eax || c.eax != c.field_owner) return Ctl::NotOwnerBranch;
    // Type 3 only until type 6 (ambient creatures) is measured with user 2 as controller (the 6c12c82 crash review).
    if (c.type != 3) return Ctl::Type;
    if (!c.key2) return Ctl::NoKey;
    if (c.session_key2 && c.session_key2 != c.key2) return Ctl::KeyMismatch;
    return Ctl::Assign;
}
inline const char* ctl_name(Ctl c) {
    switch (c) {
        case Ctl::Assign: return "assigned";
        case Ctl::NotTimer: return "not its 2043/2045/2046";
        case Ctl::NotOwnTroc: return "its Troc's vf48 was not its own manager";
        case Ctl::NotArmed: return "U-1 not armed";
        case Ctl::OtherDesc: return "another descriptor";
        case Ctl::HasController: return "a controller already";
        case Ctl::DescUser: return "desc+0x40 set";
        case Ctl::HasParent: return "a user's child";
        case Ctl::NotOwnerBranch: return "eax not the field owner's";
        case Ctl::NotHostField: return "not the host's field";
        case Ctl::Type: return "not type 3";
        case Ctl::NoKey: return "user 2's key unknown";
        case Ctl::KeyMismatch: return "user 2's key MISMATCH";
        case Ctl::Aborted: return "aborted";
        case Ctl::Family: return "family unit not admitted";
        default: return "?";
    }
}

// The NPC keys U-e assigned (mode 1) or would assign (probe), with what was seen of them afterwards. Lock-free open
// addressing on the actor key (never 0); new slots up to 3/4 of the table, and a slot whose key was removed at least
// kReuseAfterUs ago is taken over by a new key (review 4: S31's loop in probe filled 12k slots in ~27 min). When neither
// is possible the key is not tracked (overflow) and U-e does not assign it. A key put again (a reused key) starts over.
enum : uint16_t {
    kKeyAssigned = 1,  // mode 1: the controller was written
    kKeyWould = 2,     // probe: it would have been
    kKeyDriven = 4,    // a Req of user 2 named it (2556 / 2223 / 2734 / 2405 / 2291 / 2828)
    kKeyRemoved = 8,   // the server logged it out (point O)
    kKeyR42 = 16,      // ... with reason 42 (its controller's AI asked for it)
    kKeyHostReq = 32,  // the host's client sent a Req for it (mode 1: it should not, it is remote there)
    kKeyBad2005 = 64,  // a 2005 of it named another controller (mode 1)
    kKeyAccepted = 128,  // a StartActionReq for it returned 0 (child_hold's hook): its first action was accepted
    // S33 (npc_activation=1): its unit was abandoned by the containment (a per-unit decision, never the global net):
    // left alive with its controller and never counted as 'unaccepted', so it cannot trigger the Unaccepted abort.
    kKeyAbandoned = 256,
    // S36 fase B (npc_family_control=1, npc_family_logic.h): kKeyFamily = de una unidad con hijos (raíz o miembro);
    // kKeyMember = miembro (no raíz): lo juzga su raíz, así que nunca cuenta como 'unaccepted' (se cuenta aparte).
    kKeyFamily = 512,
    kKeyMember = 1024,
    // Segunda revisión de la fase B (R1, npc_family_control=1 only): a family cleanup pass (drain, logout, its Troc)
    // left it alive because a child of it could not go, or its own retirement failed: a per-unit result with its own
    // bounded retries, so it never counts in left_after_drain (counted apart, a WRN line): never a global abort.
    kKeyCleanBlocked = 2048,
};
// The 6c12c82 crash: an NPC given to user 2 whose first action nobody starts stays restore 0; held from the host it
// must never reach the host's client like that (child_hold), and U-e gives up on it after this long.
inline constexpr uint64_t kAcceptWithinUs = 5'000'000;
template <size_t N>
class KeyTable {
    static_assert(N >= 16 && (N & (N - 1)) == 0, "N must be a power of two");

public:
    struct View {
        uint32_t key = 0;
        uint16_t flags = 0;
        uint64_t put_us = 0, removed_us = 0;
    };
    static constexpr uint64_t kReuseAfterUs = 30'000'000;
    // S36 (AUDITORIA-2 cambio 1): huecos reservados para una unidad entera antes de asignar su raíz. Una alta sin
    // reserva no puede gastarlos (su límite es 3/4 menos lo reservado); una con reserva consume una y puede llegar
    // hasta 7/8, así que nunca falla por capacidad (las reservas vivas están acotadas por los marcos). Sin reservas
    // pendientes todo es como antes.
    bool can_reserve(size_t n) const noexcept {
        return used_.load(std::memory_order_relaxed) + reserved_.load(std::memory_order_relaxed) + n <= N * 3 / 4;
    }
    bool reserve(size_t n) noexcept {
        size_t r = reserved_.load(std::memory_order_relaxed);
        for (;;) {
            if (used_.load(std::memory_order_relaxed) + r + n > N * 3 / 4) return false;
            if (reserved_.compare_exchange_weak(r, r + n, std::memory_order_acq_rel)) return true;
        }
    }
    void unreserve(size_t n) noexcept {
        size_t r = reserved_.load(std::memory_order_relaxed);
        while (!reserved_.compare_exchange_weak(r, r >= n ? r - n : 0, std::memory_order_acq_rel)) {
        }
    }
    size_t reserved() const noexcept { return reserved_.load(std::memory_order_relaxed); }
    // S43: stage_slot is the NPC's stage slot (r42_slot_id of its descriptor's schedule handle; 0 = unknown), for the
    // reason-42 loop check (R42SlotTable). A reused key takes the new one.
    bool put(uint32_t key, uint16_t kind, uint64_t now_us, bool use_reserved = false, uint32_t stage_slot = 0) {
        if (use_reserved) unreserve(1);
        Slot* s = slot(key, true, now_us, use_reserved);
        if (!s) return false;
        s->put_us.store(now_us, std::memory_order_relaxed);
        s->removed_us.store(0, std::memory_order_relaxed);
        s->stage_slot.store(stage_slot, std::memory_order_relaxed);
        s->flags.store(kind, std::memory_order_release);
        return true;
    }
    // ORs `bits` into a tracked key's flags (and the removal time with kKeyRemoved); false when not tracked.
    bool mark(uint32_t key, uint16_t bits, uint64_t now_us, uint16_t* before = nullptr) {
        Slot* s = slot(key, false, 0);
        if (!s) return false;
        if ((bits & kKeyRemoved) && !(s->flags.load(std::memory_order_relaxed) & kKeyRemoved)) {
            s->removed_us.store(now_us, std::memory_order_relaxed);
        }
        const uint16_t b = s->flags.fetch_or(bits, std::memory_order_acq_rel);
        if (before) *before = b;
        return true;
    }
    // When the key was put (0 = not tracked).
    uint64_t put_time(uint32_t key) const {
        const Slot* s = const_cast<KeyTable*>(this)->slot(key, false, 0);
        return s ? s->put_us.load(std::memory_order_relaxed) : 0;
    }
    uint16_t flags(uint32_t key) const {
        const Slot* s = const_cast<KeyTable*>(this)->slot(key, false, 0);
        return s ? s->flags.load(std::memory_order_acquire) : 0;
    }
    // S43: the stage slot given with the key's last put (0 = unknown, or the key is not tracked).
    uint32_t slot_of(uint32_t key) const {
        const Slot* s = const_cast<KeyTable*>(this)->slot(key, false, 0);
        if (!s) return 0;
        (void)s->flags.load(std::memory_order_acquire);  // pairs with put's release: that put's slot
        return s->stage_slot.load(std::memory_order_relaxed);
    }
    bool get(size_t i, View& v) const {
        if (i >= N) return false;
        v.key = s_[i].key.load(std::memory_order_acquire);
        if (!v.key) return false;
        v.flags = s_[i].flags.load(std::memory_order_acquire);
        v.put_us = s_[i].put_us.load(std::memory_order_relaxed);
        v.removed_us = s_[i].removed_us.load(std::memory_order_relaxed);
        return true;
    }
    static constexpr size_t capacity() { return N; }
    uint64_t overflow() const { return overflow_.load(std::memory_order_relaxed); }
    uint64_t reused() const { return reused_.load(std::memory_order_relaxed); }
    size_t used() const { return used_.load(std::memory_order_relaxed); }

private:
    struct Slot {
        std::atomic<uint32_t> key{0};
        std::atomic<uint16_t> flags{0};
        std::atomic<uint32_t> stage_slot{0};
        std::atomic<uint64_t> put_us{0}, removed_us{0};
    };
    bool reusable(const Slot& s, uint64_t now_us) const {
        const uint64_t r = s.removed_us.load(std::memory_order_relaxed);
        return (s.flags.load(std::memory_order_acquire) & kKeyRemoved) && r && now_us >= r + kReuseAfterUs;
    }
    // A removed key's slot taken over (never left empty: the probe chains of the other keys stay whole).
    Slot* take_over(Slot& s, uint32_t key, uint64_t now_us) {
        uint32_t old = s.key.load(std::memory_order_acquire);
        if (!old || !reusable(s, now_us) || !s.key.compare_exchange_strong(old, key, std::memory_order_acq_rel)) {
            return nullptr;
        }
        reused_.fetch_add(1, std::memory_order_relaxed);
        return &s;
    }
    Slot* slot(uint32_t key, bool insert, uint64_t now_us, bool reserved_insert = false) {
        if (!key) return nullptr;
        const size_t limit = reserved_insert ? N * 7 / 8 : N * 3 / 4;
        const size_t held = reserved_insert ? 0 : reserved_.load(std::memory_order_relaxed);
        size_t i = static_cast<size_t>((static_cast<uint64_t>(key) * 0x9E3779B97F4A7C15ull) >> 40) & (N - 1);
        size_t reuse = N;  // the first slot of the chain a new key may take over
        for (size_t probe = 0; probe < N; ++probe, i = (i + 1) & (N - 1)) {
            uint32_t k = s_[i].key.load(std::memory_order_acquire);
            if (k == key) return &s_[i];
            if (k) {
                if (insert && reuse == N && reusable(s_[i], now_us)) reuse = i;
                continue;
            }
            if (!insert) return nullptr;
            if (reuse != N) {
                if (Slot* t = take_over(s_[reuse], key, now_us)) return t;
            }
            if (used_.load(std::memory_order_relaxed) + held >= limit) break;
            if (s_[i].key.compare_exchange_strong(k, key, std::memory_order_acq_rel)) {
                used_.fetch_add(1, std::memory_order_relaxed);
                return &s_[i];
            }
            if (k == key) return &s_[i];
        }
        if (insert && reuse != N) {
            if (Slot* t = take_over(s_[reuse], key, now_us)) return t;
        }
        if (insert) overflow_.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    Slot s_[N];
    std::atomic<size_t> used_{0}, reserved_{0};
    std::atomic<uint64_t> overflow_{0}, reused_{0};
};

// S43 (docs/research/s25/s43-datos/motivo42-informe.md): the reason-42 guard counts S31's respawn LOOP, not every reason-42 logout. The loop is an NPC
// born in a stage slot, logged out with reason 42 by its controller's AI and put back in the SAME slot (with another
// key), again and again; a native AI logout (on 15-10 a schedule group of 24 NPCs, data 0x3537D7CE, left at once and
// was never put back) is many slots once. A slot is the descriptor's schedule handle ([desc+0x4E8] -> u32 +0 the
// schedule data, +4 the node; build 2976), the values of the [control] line.
inline constexpr uint64_t kR42LoopWindowUs = 60'000'000;
// The slot's id: 0 = unknown (no handle or data 0), counted as a repeat (fails closed, as the old guard did).
inline uint32_t r42_slot_id(uint32_t data, uint32_t node) noexcept {
    if (!data) return 0;
    uint64_t x = (static_cast<uint64_t>(node) << 32 | data) * 0x9E3779B97F4A7C15ull;
    x ^= x >> 29;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 32;
    const auto id = static_cast<uint32_t>(x);
    return id ? id : 1u;
}
// The last reason-42 logout of each slot (lock-free, fixed size, open addressing). Entries are never emptied (the probe
// chains stay whole): one whose last logout is older than the window is taken over by a new slot. repeat() runs in
// the server's Logout hook: atomics only, at most kProbe probes.
template <size_t N>
class R42SlotTable {
    static_assert(N >= 16 && (N & (N - 1)) == 0, "N must be a power of two");

public:
    static constexpr size_t kProbe = N < 64 ? N : 64;
    // True when `slot` had another reason-42 logout `window_us` or less before `now_us` AND this NPC was put after that
    // logout (the loop: logout, put back, logout); records now_us. Review of a4c2661 (P2-1): two instances of the same
    // stage put before both logouts (a schedule ending) are not the loop. put_us = 0 (the birth is not known) counts as
    // put after (fails closed), as do an unknown slot (0) and a full table (no empty or stale entry in kProbe probes).
    bool repeat(uint32_t slot, uint64_t now_us, uint64_t window_us = kR42LoopWindowUs, uint64_t put_us = 0) noexcept {
        if (!slot) {
            unknown_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        size_t i = static_cast<size_t>((static_cast<uint64_t>(slot) * 0x9E3779B97F4A7C15ull) >> 40) & (N - 1);
        size_t stale_at = N;  // the chain's first stale entry
        for (size_t probe = 0; probe < kProbe; ++probe, i = (i + 1) & (N - 1)) {
            uint32_t k = e_[i].slot.load(std::memory_order_acquire);
            if (k == slot) return seen(e_[i], now_us, window_us, put_us);
            if (k) {
                if (stale_at == N && stale(e_[i], now_us, window_us)) stale_at = i;
                continue;
            }
            // The chain's end: the slot is not in the table (a new slot always lands before the first empty entry).
            if (stale_at != N && take(e_[stale_at], slot, now_us, window_us)) return false;
            if (e_[i].slot.compare_exchange_strong(k, slot, std::memory_order_acq_rel)) {
                e_[i].last_us.store(now_us, std::memory_order_release);
                used_.fetch_add(1, std::memory_order_relaxed);
                firsts_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            if (k == slot) return seen(e_[i], now_us, window_us, put_us);
            // Another slot took this entry meanwhile: on along the chain.
        }
        if (stale_at != N && take(e_[stale_at], slot, now_us, window_us)) return false;
        overflow_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    static constexpr size_t capacity() { return N; }
    size_t used() const { return used_.load(std::memory_order_relaxed); }
    // Logouts that were their slot's first in the window (distinct slots), with an unknown slot, and with the table full.
    uint64_t firsts() const { return firsts_.load(std::memory_order_relaxed); }
    uint64_t unknown() const { return unknown_.load(std::memory_order_relaxed); }
    uint64_t overflow() const { return overflow_.load(std::memory_order_relaxed); }
    // Same slot within the window but put BEFORE the slot's last logout (another instance leaving too): not counted.
    uint64_t concurrent() const { return concurrent_.load(std::memory_order_relaxed); }

private:
    struct Entry {
        std::atomic<uint32_t> slot{0};
        std::atomic<uint64_t> last_us{0};
    };
    static bool stale(const Entry& e, uint64_t now_us, uint64_t window_us) {
        return now_us > e.last_us.load(std::memory_order_acquire) + window_us;
    }
    bool seen(Entry& e, uint64_t now_us, uint64_t window_us, uint64_t put_us) {
        const uint64_t t = e.last_us.exchange(now_us, std::memory_order_acq_rel);
        const bool within = now_us <= t + window_us;
        const bool rep = within && (!put_us || put_us > t);
        if (!within) firsts_.fetch_add(1, std::memory_order_relaxed);
        else if (!rep) concurrent_.fetch_add(1, std::memory_order_relaxed);
        return rep;
    }
    bool take(Entry& e, uint32_t slot, uint64_t now_us, uint64_t window_us) {
        uint32_t old = e.slot.load(std::memory_order_acquire);
        if (!old || !stale(e, now_us, window_us) || !e.slot.compare_exchange_strong(old, slot, std::memory_order_acq_rel)) {
            return false;
        }
        e.last_us.store(now_us, std::memory_order_release);
        firsts_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    Entry e_[N];
    std::atomic<size_t> used_{0};
    std::atomic<uint64_t> firsts_{0}, unknown_{0}, overflow_{0}, concurrent_{0};
};

// What the module's thread reads from the table each second (the keys of `kind`: kKeyAssigned or kKeyWould).
struct CtlStats {
    uint32_t tracked = 0, alive = 0, removed = 0, r42 = 0, driven = 0, host_req = 0, bad_2005 = 0;
    // Alive, put >= kAcceptWithinUs ago and no accepted StartActionReq yet; and those with one. abandoned: alive
    // keys of a unit the containment abandoned (kKeyAbandoned), apart from 'unaccepted'.
    uint32_t unaccepted = 0, accepted = 0, abandoned = 0;
    // S36: miembros vivos de una unidad (kKeyMember) sin StartActionReq aceptada propia: los juzga su raíz.
    uint32_t members = 0;
    // Undriven (log only): keys put 10..120 s ago that lived >= 10 s (or still live), and how many user 2 ever drove.
    uint32_t eligible = 0, eligible_driven = 0;
    // Put before `drain_us` (a drain's time, 0 = none) and still not removed. drain_blocked: those of them a family
    // cleanup left alive (kKeyCleanBlocked): not in left_after_drain.
    uint32_t left_after_drain = 0, drain_blocked = 0;
};
inline constexpr uint64_t kDriveWindowLoUs = 10'000'000, kDriveWindowHiUs = 120'000'000;
inline void ctl_stats_add(CtlStats& s, uint16_t flags, uint64_t put_us, uint64_t removed_us, uint64_t now_us,
                          uint64_t drain_us) {
    ++s.tracked;
    const bool removed = (flags & kKeyRemoved) != 0;
    removed ? ++s.removed : ++s.alive;
    if (flags & kKeyR42) ++s.r42;
    if (flags & kKeyDriven) ++s.driven;
    if (flags & kKeyHostReq) ++s.host_req;
    if (flags & kKeyBad2005) ++s.bad_2005;
    const uint64_t age = now_us >= put_us ? now_us - put_us : 0;
    if (flags & kKeyAccepted) {
        ++s.accepted;
    } else if (!removed && (flags & kKeyAbandoned)) {
        ++s.abandoned;
    } else if (!removed && (flags & kKeyMember)) {
        ++s.members;
    } else if (!removed && age >= kAcceptWithinUs) {
        ++s.unaccepted;
    }
    const bool lived = !removed || (removed_us >= put_us && removed_us - put_us >= kDriveWindowLoUs);
    if (age >= kDriveWindowLoUs && age <= kDriveWindowHiUs && lived) {
        ++s.eligible;
        if (flags & kKeyDriven) ++s.eligible_driven;
    }
    if (drain_us && put_us < drain_us && !removed) ++((flags & kKeyCleanBlocked) ? s.drain_blocked : s.left_after_drain);
}
template <size_t N>
CtlStats ctl_stats(const KeyTable<N>& t, uint16_t kind, uint64_t now_us, uint64_t drain_us) {
    CtlStats s;
    typename KeyTable<N>::View v;
    for (size_t i = 0; i < N; ++i) {
        if (!t.get(i, v) || !(v.flags & kind)) continue;
        ctl_stats_add(s, v.flags, v.put_us, v.removed_us, now_us, drain_us);
    }
    return s;
}

// user2_npc_control=1's abort criteria (checked every second): the first one stops ASSIGNING for the rest of the run
// (an ERR line); the keys already assigned stay as they are and U-1's drains go on as before.
enum class CtlAbort : uint8_t {
    None,
    Bad2005,       // a 2005 of an assigned key named another controller (to user 2 or the host's client)
    Unaccepted,    // an assigned NPC alive 5 s with no accepted StartActionReq (the 6c12c82 crash's precondition):
                   // stop assigning, and those NPCs are logged out (their stage respawns them with the host)
    Reason42,      // > 10 reason-42 logouts of assigned keys in the last minute that REPEAT a stage slot (another one
                   // in that slot <= 60 s before, R42SlotTable): S31's respawn loop with the guest's AI. S43: every
                   // reason-42 logout (a native AI logout of a group: many slots once) is only logged
    Undriven,      // not an abort (review 7): ctl_undriven, a WRN line only until Shadow measures it
    HostDrives,    // the host's client sent a Req for an assigned key (it should be remote there)
    LeftAfterDrain,// an assigned key still alive 5 s after a drain of user 2's manager
    Count
};
struct CtlAbortIn {
    CtlStats s;
    bool accept_tracked = false;  // child_hold's StartActionReq hook is in (else 'accepted' is never seen)
    uint32_t r42_loop_last_minute = 0;  // S43: the abort's count (same-slot repeats; unknown slots count too)
    uint32_t r42_raw_last_minute = 0;   // every reason-42 logout of the assigned keys: log only
    bool drain_checked = false;  // left_after_drain counts (5 s after the drain)
};
inline CtlAbort ctl_abort(const CtlAbortIn& a) {
    if (a.s.bad_2005) return CtlAbort::Bad2005;
    if (a.accept_tracked && a.s.unaccepted) return CtlAbort::Unaccepted;
    if (a.r42_loop_last_minute > 10) return CtlAbort::Reason42;
    if (a.s.host_req) return CtlAbort::HostDrives;
    if (a.drain_checked && a.s.left_after_drain) return CtlAbort::LeftAfterDrain;
    return CtlAbort::None;
}
inline const char* ctl_abort_name(CtlAbort a) {
    switch (a) {
        case CtlAbort::None: return "none";
        case CtlAbort::Bad2005: return "a 2005 of an assigned NPC named ANOTHER controller";
        case CtlAbort::Unaccepted: return "an assigned NPC has no accepted StartActionReq 5 s after its birth";
        case CtlAbort::Reason42:
            return "more than 10 reason-42 logouts of assigned NPCs in a minute that repeat the same stage slots (S31's "
                   "respawn loop: their AI despawns them and their stage puts them back)";
        case CtlAbort::Undriven: return "fewer than half of the assigned NPCs ever got a Req from user 2";
        case CtlAbort::HostDrives: return "the host's client sent a Req for an assigned NPC";
        case CtlAbort::LeftAfterDrain: return "an assigned NPC still alive 5 s after a drain of user 2's manager";
        default: return "?";
    }
}

// Review 7 (log only until Shadow): of >= 8 keys given 10..120 s ago that lived >= 10 s, fewer than half ever got a
// Req of user 2 (at any time: a guard standing at its post may send none for a while). A WRN line, never an abort.
inline bool ctl_undriven(const CtlStats& s) { return s.eligible >= 8 && s.eligible_driven * 2 < s.eligible; }

// The AI / movement Reqs whose payload starts with the actor's key (driven / the host's client drives).
inline bool ai_req(uint16_t op) {
    return op == 2556 || op == 2223 || op == 2734 || op == 2405 || op == 2291 || op == 2828;
}

// A 2005 payload: [u16 length][record] repeated; a record: type u8 +0, key u32 +1, controller u32 +0x12. Calls
// fn(type, key, controller, record, length) for each record long enough; stops at the first length that does not fit.
template <class Fn>
inline size_t for_each_2005_rec(const uint8_t* p, size_t n, Fn&& fn) {
    size_t off = 0, k = 0;
    while (off + 2 <= n) {
        const size_t len = static_cast<size_t>(p[off]) | static_cast<size_t>(p[off + 1]) << 8;
        if (len < 5 || off + 2 + len > n) break;
        const uint8_t* r = p + off + 2;
        if (len >= 0x16) {
            const uint32_t key = static_cast<uint32_t>(r[1]) | static_cast<uint32_t>(r[2]) << 8 |
                                 static_cast<uint32_t>(r[3]) << 16 | static_cast<uint32_t>(r[4]) << 24;
            const uint32_t ctl = static_cast<uint32_t>(r[0x12]) | static_cast<uint32_t>(r[0x13]) << 8 |
                                 static_cast<uint32_t>(r[0x14]) << 16 | static_cast<uint32_t>(r[0x15]) << 24;
            fn(r[0], key, ctl, r, len);
            ++k;
        }
        off += 2 + len;
    }
    return k;
}
// The same with fn(type, key, controller).
template <class Fn>
inline size_t for_each_2005(const uint8_t* p, size_t n, Fn&& fn) {
    return for_each_2005_rec(p, n, [&](uint8_t t, uint32_t k, uint32_t c, const uint8_t*, size_t) { fn(t, k, c); });
}

// ---------------------------------------------------------------- the drain at the host's preparation
//
// S31: the distance drain (<= 7 cells for 5 s) emptied user 2's manager 2.1 s before the host's own preparation
// created the host's stages: 2.86 s with no town NPCs on the guest. [session2] user2_drain_at_host_prepare: at the
// entry of 0x287EF60 for the HOST's manager, with user 2's manager armed and the host now within the off threshold of
// user 2 in the same field, user 2's manager is unloaded at once (0x287DA50 + 0x287D4C0), before the host's
// evaluation creates its stages (U-f2 no longer skips them: nothing of user 2's is alive).
// Review 1: the host's preparation runs on ITS thread while user 2's Trocs may run on others (its armed tick evaluates
// user 2's manager without a lock): never while one is in flight (user2_funnels' counter; the Trocs that start meanwhile
// wait at their entry while the drain runs). Review 8: also the stages left in user 2's manager by an async drain.
enum class PrepDrain : uint8_t { Drain, NotHost, NotArmed, NotSameField, PositionUnknown, TooFar, NotServerThread,
                                 Nested, UnknownCaller, Busy, Count };
struct PrepDrainIn {
    bool host_mgr = false, same_field = false, cells_known = false, server_thread = false, nested = false,
         caller_known = false;
    bool armed = false;      // user 2's manager armed (mode 1)
    bool leftovers = false;  // not armed, but its manager still holds stages in the host's field (review 8)
    int32_t trocs_in_flight = 0;  // user 2's Trocs running now (user2_funnels)
    uint32_t dist = 0, off_cells = 7;
};
inline PrepDrain prep_drain_verdict(const PrepDrainIn& p) {
    if (!p.host_mgr) return PrepDrain::NotHost;
    if (!p.armed && !p.leftovers) return PrepDrain::NotArmed;
    if (!p.server_thread) return PrepDrain::NotServerThread;
    if (p.nested) return PrepDrain::Nested;
    if (!p.caller_known) return PrepDrain::UnknownCaller;
    if (!p.same_field) return PrepDrain::NotSameField;
    if (!p.cells_known) return PrepDrain::PositionUnknown;
    if (p.dist > p.off_cells) return PrepDrain::TooFar;
    if (p.trocs_in_flight != 0) return PrepDrain::Busy;
    return PrepDrain::Drain;
}
inline const char* prep_drain_name(PrepDrain d) {
    switch (d) {
        case PrepDrain::Drain: return "drain";
        case PrepDrain::NotHost: return "not the host's manager";
        case PrepDrain::NotArmed: return "user 2's manager neither armed nor holding stages";
        case PrepDrain::NotSameField: return "not the same field";
        case PrepDrain::PositionUnknown: return "a position unreadable";
        case PrepDrain::TooFar: return "the host is beyond the off threshold";
        case PrepDrain::NotServerThread: return "not a server thread";
        case PrepDrain::Nested: return "inside another drain of this module";
        case PrepDrain::UnknownCaller: return "an unknown caller of 0x287EF60";
        case PrepDrain::Busy: return "a Troc of user 2 is running";
        default: return "?";
    }
}
// Review 2: with user2_drain_at_host_prepare=1 a DISTANCE drain waits while the host's own load is pending (its owner
// load step L after its last preparation P), so P finds user 2's manager still armed (S31: the distance drain came
// ~2.1 s before P). At most kPrepHoldUs, then the distance drain as before. hold_since 0: not holding yet.
inline constexpr uint64_t kPrepHoldUs = 15'000'000;
inline bool hold_distance_drain(bool dprep_on, uint64_t now_us, uint64_t last_host_load_us, uint64_t last_host_prep_us,
                                uint64_t hold_since_us) {
    if (!dprep_on || !last_host_load_us || last_host_load_us <= last_host_prep_us) return false;
    return !hold_since_us || now_us < hold_since_us + kPrepHoldUs;
}

// 0x287EF60's three callers (Ghidra xrefs, build 2976): their return addresses.
inline constexpr uint32_t kPrepRetTick = 0x287DCD5;      // 0x287DBB0, the 2043 tick (+0x68A set)
inline constexpr uint32_t kPrepRet2895 = 0x289604E;      // 0x2895EF0
inline constexpr uint32_t kPrepRetMoveField = 0x2BC84D9; // TransformSync::MoveFieldCompleteCore 0x2BC7670
inline const char* prep_caller_name(uint32_t ret_rva) {
    switch (ret_rva) {
        case kPrepRetTick: return "the 2043 tick 0x287DBB0";
        case kPrepRet2895: return "0x2895EF0";
        case kPrepRetMoveField: return "MoveFieldCompleteCore 0x2BC7670";
        default: return "an UNKNOWN caller";
    }
}
inline bool prep_caller_known(uint32_t ret_rva) {
    return ret_rva == kPrepRetTick || ret_rva == kPrepRet2895 || ret_rva == kPrepRetMoveField;
}

// ---------------------------------------------------------------- overlap (the live §3.2)

// Of `n` stage cells, how many lie within k cells of `center`, for k = 3, 4, 5; and the min / max distance to `from`.
struct Near {
    uint32_t within[3] = {};
    uint32_t min_from = UINT32_MAX, max_from = 0, counted = 0;
};
inline Near near_counts(const Cell* cells, size_t n, Cell center, Cell from) {
    Near r;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t d = cheb(cells[i], center);
        for (uint32_t k = 0; k < 3; ++k) {
            if (d <= 3 + k) ++r.within[k];
        }
        const uint32_t f = cheb(cells[i], from);
        r.min_from = std::min(r.min_from, f);
        r.max_from = std::max(r.max_from, f);
        ++r.counted;
    }
    return r;
}

}  // namespace cdmp::session2::u2s
