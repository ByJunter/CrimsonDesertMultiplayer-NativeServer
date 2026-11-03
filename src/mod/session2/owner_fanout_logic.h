#pragma once
// P8 strategy B, step 2: the owner fan-out PROBE (docs/research/pendientes/p8-impl-owner-fanout.md,
// p8-arquitectura-observador.md §3.4 and §7.2). Pure logic, no game memory, no Windows: the mode,
// the writer table lookup (return RVA of finder->vf1 -> writer -> opcode), the families of §3.4,
// the observer-trip rule, the verdict of one ToTarget::vf1 call and the lock-free counters.
//
// What it decides: for one call of ToTarget::vf1 (0x2A757F0) while user 2 observes the host's
// field, whether the server is collecting the HOST's session for an Ack and, if so, which writer
// (and opcode) it is: that is one Ack user 2 "would also get" in step 3 if its writer is listed.
// Nothing here changes what the game sends.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mod/session2/owner_fanout_writers.h"
#include "mod/session2/user2_funnels_logic.h"  // u2f::SiteCounts, u2f::OpCounts (fixed, lock-free)

namespace cdmp::session2::ofo {

// ---------------------------------------------------------------- the option

// [session2] owner_fanout = 0 | probe. The value 1 (step 3, sending) is reserved: refused, off.
enum class Mode : uint8_t { Off, Probe };

struct ParsedMode {
    Mode mode = Mode::Off;
    bool reserved_send = false;  // "1" / "on": step 3 is not implemented, refused with a warning
    bool unknown = false;        // anything else: off with a warning
};

inline ParsedMode parse_mode(std::string_view raw) {
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.remove_prefix(1);
    if (const size_t c = raw.find_first_of(";#"); c != std::string_view::npos) raw = raw.substr(0, c);
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '\r' || raw.back() == '\n')) {
        raw.remove_suffix(1);
    }
    std::string v(raw);
    for (char& ch : v) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    ParsedMode p;
    if (v.empty() || v == "0" || v == "off" || v == "no" || v == "false") return p;
    if (v == "probe") {
        p.mode = Mode::Probe;
        return p;
    }
    if (v == "1" || v == "on" || v == "yes" || v == "true" || v == "send") {
        p.reserved_send = true;
        return p;
    }
    p.unknown = true;
    return p;
}

inline const char* mode_name(Mode m) { return m == Mode::Probe ? "probe" : "0"; }

// ---------------------------------------------------------------- the writers (§3.4)

// The families of p8-arquitectura-observador.md §3.4. Listed = the ones step 3 would add user 2 to.
enum class Family : uint8_t { Sequencer, Schedule, Faction, Doubtful, Excluded, Other, Count };
inline constexpr size_t kFamilies = static_cast<size_t>(Family::Count);

constexpr Family family_of(uint16_t op) {
    switch (op) {
        // Sequencers: 2661, 2810, 2322 (option A copies these today), 2797, 2224, 2226, 2140, 2675, 2222, 2918, 2416.
        case 2661: case 2810: case 2322: case 2797: case 2224: case 2226: case 2140: case 2675: case 2222:
        case 2918: case 2416:
            return Family::Sequencer;
        // Schedules: 2126, 2958, 2909, 2499, 2127, 2839.
        case 2126: case 2958: case 2909: case 2499: case 2127: case 2839:
            return Family::Schedule;
        // Faction: 2195, 2130.
        case 2195: case 2130:
            return Family::Faction;
        // Doubtful (the owner's missions): 2264, 2165, 2724.
        case 2264: case 2165: case 2724:
            return Family::Doubtful;
        // Excluded: quest / inventory UI, answers to whoever asked, the controller's AI.
        case 2203: case 3009: case 2808: case 2635: case 2102: case 2426: case 2297: case 2167: case 2663:
        case 2916: case 2948: case 2218: case 2273: case 2933:
            return Family::Excluded;
        default:
            return Family::Other;
    }
}

// The families step 3 would add user 2 to (doubtful ones are counted apart).
constexpr bool listed(Family f) { return f == Family::Sequencer || f == Family::Schedule || f == Family::Faction; }

// What option A copies to user 2 today, for the comparison: observer_seq_copy's 2661/2810/2322 and, with
// observer_sched_copy=1 (p8-impl-horarios-snapshot.md), the schedules' 2126/2958/2909 (0 queued when it is off).
inline constexpr uint16_t kCopiedByA[] = {2661, 2810, 2322, 2126, 2958, 2909};
constexpr bool copied_by_a(uint16_t op) {
    for (const uint16_t a : kCopiedByA) {
        if (a == op) return true;
    }
    return false;
}

inline const char* family_name(Family f) {
    switch (f) {
        case Family::Sequencer: return "sequencers";
        case Family::Schedule: return "schedules";
        case Family::Faction: return "faction";
        case Family::Doubtful: return "doubtful (missions)";
        case Family::Excluded: return "excluded";
        case Family::Other: return "other";
        // Family::Count is no family: only a bad cast gets here. The summary never prints it.
        default: return "?";
    }
}

inline constexpr size_t kWriterCount = std::size(kWriters);

constexpr bool writers_sorted() {
    for (size_t i = 1; i < kWriterCount; ++i) {
        if (kWriters[i - 1].ret >= kWriters[i].ret) return false;
    }
    return true;
}
static_assert(writers_sorted(), "owner_fanout_writers.h must be sorted by ret with no duplicates");

// The writer whose finder->vf1 call returns to `ret_rva`; -1 if none.
constexpr int writer_index(uint32_t ret_rva) {
    size_t lo = 0, hi = kWriterCount;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (kWriters[mid].ret < ret_rva) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < kWriterCount && kWriters[lo].ret == ret_rva ? static_cast<int>(lo) : -1;
}

// ---------------------------------------------------------------- the trip

// The same rule as option A (observer_seq_copy, take_frame): user 2's MoveFieldAck 2174 names, as the
// SERVER wrote it (before user 2's copy is rewritten), an owner that is a user (class 9) other than user 2.
constexpr bool observer_trip(uint32_t server_owner, uint32_t user2_user) {
    return user2_user != 0 && (server_owner >> 28) == 0x9 && server_owner != user2_user;
}

// ---------------------------------------------------------------- one vf1 call

enum class Verdict : uint8_t {
    NotObserving,     // no observer trip (only `calls` counts these)
    OtherFinder,      // [finder] is neither ToTarget's nor ToControlUser's vtable
    EmptyFinder,      // [finder+0x10] == 0: vf1 adds nothing
    NoUser2,          // no live session 2, or user 2's logout was asked for
    StaleTrip,        // the live session 2 is not the one the trip began with (new epoch / server)
    ToUser2,          // the target is user 2's own session: it gets it natively
    OtherTarget,      // a session that is neither the host's nor user 2's
    Unreadable,       // the finder or the array could not be read
    ArrayTooLong,     // more than kMaxArrayScan sessions in the array: not scanned to the end (unknown)
    AlreadyThere,     // session 2 is already in the array
    HostTarget,       // THE CASE: a ToTarget Ack for the host's session while user 2 observes its field
    ControlUserHost,  // a ToControlUser Ack for the host's session (the host controls the actor): apart
    Count
};
inline constexpr size_t kVerdicts = static_cast<size_t>(Verdict::Count);

inline const char* verdict_name(Verdict v) {
    switch (v) {
        case Verdict::NotObserving: return "not observing";
        case Verdict::OtherFinder: return "neither ToTarget nor ToControlUser";
        case Verdict::EmptyFinder: return "empty finder";
        case Verdict::NoUser2: return "no live user 2";
        case Verdict::StaleTrip: return "stale trip";
        case Verdict::ToUser2: return "user 2's own session";
        case Verdict::OtherTarget: return "another session";
        case Verdict::Unreadable: return "unreadable";
        case Verdict::ArrayTooLong: return "array too long to scan";
        case Verdict::AlreadyThere: return "session 2 already there";
        case Verdict::HostTarget: return "the host's session (ToTarget)";
        case Verdict::ControlUserHost: return "the host's session (ToControlUser)";
        default: return "?";
    }
}

// The TLS array {T* data; u32 count; ...} after the original vf1: is session 2 in it? `read(i, out)` reads
// entry i (a guarded read in the hook). At most kMaxArrayScan entries are looked at: a longer array that
// does not show session 2 in them is UNKNOWN (kTooLong), never "not there".
inline constexpr uint32_t kMaxArrayScan = 64;
inline constexpr int kInArray = 1, kNotInArray = 0, kArrayUnreadable = -1, kArrayTooLong = -2;

template <class Read>
int scan_array(bool header_read, bool data_null, uint32_t count, uintptr_t s2, Read read) {
    if (!header_read) return kArrayUnreadable;
    if (data_null) return count ? kArrayUnreadable : kNotInArray;
    for (uint32_t i = 0; i < count && i < kMaxArrayScan; ++i) {
        uintptr_t s = 0;
        if (!read(i, s)) return kArrayUnreadable;
        if (s == s2) return kInArray;
    }
    return count > kMaxArrayScan ? kArrayTooLong : kNotInArray;
}

struct CallIn {
    bool observing = false;        // an observer trip is on (its session 2 below)
    bool finder_read = false;      // [finder] and [finder+0x10] were read
    uintptr_t vtable = 0;          // [finder]
    uintptr_t to_target_vt = 0;    // exe + 0x5B25438
    uintptr_t to_control_vt = 0;   // exe + 0x5B252F8 (ToControlUser: the same vf1)
    uintptr_t target = 0;          // [finder+0x10]: the session vf1 pushes
    uintptr_t s1 = 0;              // the host's session (session 1 of the server session 2 lives in)
    uintptr_t s2_live = 0;         // the live session 2 now (0 = none)
    uintptr_t trip_s2 = 0;         // the session 2 the trip began with
    bool logout = false;           // user 2's logout was asked for
    int s2_in_array = 0;           // scan_array's result (only read for host-session candidates)
};

// Both finders that share vf1 get the same target checks; only the last verdict tells them apart.
constexpr Verdict judge(const CallIn& in) {
    if (!in.observing) return Verdict::NotObserving;
    if (!in.finder_read) return Verdict::Unreadable;
    const bool to_target = in.vtable == in.to_target_vt;
    const bool to_control = !to_target && in.to_control_vt && in.vtable == in.to_control_vt;
    if (!to_target && !to_control) return Verdict::OtherFinder;
    if (!in.target) return Verdict::EmptyFinder;
    if (!in.s2_live || in.logout) return Verdict::NoUser2;
    if (in.s2_live != in.trip_s2) return Verdict::StaleTrip;
    if (in.target == in.s2_live) return Verdict::ToUser2;
    if (!in.s1 || in.target != in.s1) return Verdict::OtherTarget;
    if (in.s2_in_array == kArrayTooLong) return Verdict::ArrayTooLong;
    if (in.s2_in_array < 0) return Verdict::Unreadable;
    if (in.s2_in_array > 0) return Verdict::AlreadyThere;
    return to_target ? Verdict::HostTarget : Verdict::ControlUserHost;
}

// ---------------------------------------------------------------- the hook's own patch

// What safetyhook writes at vf1 (build/_deps/safetyhook-src/src/inline_hook.cpp, enable()), padded with
// 0x90 up to the relocated length:
//   E9 form: `E9 rel32` to the trampoline epilogue's `FF 25 00000000` + u64 detour;
//   FF form: `FF 25 00000000` + u64 detour at vf1+6.
// owner_fanout_read_unpatched shows the original bytes ONLY when the live bytes are exactly this.

inline uint64_t le64_at(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = v << 8 | p[i];
    return v;
}

// The E9 form's destination, or 0 if `live` does not start with E9.
inline uint64_t e9_destination(std::span<const uint8_t> live, uint64_t at) {
    if (live.size() < 5 || live[0] != 0xE9) return 0;
    const int32_t rel = static_cast<int32_t>(static_cast<uint32_t>(live[1]) | static_cast<uint32_t>(live[2]) << 8 |
                                             static_cast<uint32_t>(live[3]) << 16 | static_cast<uint32_t>(live[4]) << 24);
    return at + 5 + static_cast<uint64_t>(static_cast<int64_t>(rel));
}

inline bool ff_jump_to(std::span<const uint8_t> b, uint64_t detour) {
    return b.size() >= 14 && b[0] == 0xFF && b[1] == 0x25 && b[2] == 0 && b[3] == 0 && b[4] == 0 && b[5] == 0 &&
           le64_at(b.data() + 6) == detour;
}

// live = the patch range as it is now (its relocated length); stub = 14 bytes read at the E9 destination
// (empty if not read), which must lie inside [tramp_lo, tramp_hi).
inline bool patch_is_ours(std::span<const uint8_t> live, uint64_t at, uint64_t detour, std::span<const uint8_t> stub,
                          uint64_t tramp_lo, uint64_t tramp_hi) {
    size_t jmp = 0;
    if (const uint64_t dst = e9_destination(live, at)) {
        if (dst < tramp_lo || dst + 14 > tramp_hi || !ff_jump_to(stub, detour)) return false;
        jmp = 5;
    } else if (ff_jump_to(live, detour)) {
        jmp = 14;
    } else {
        return false;
    }
    for (size_t i = jmp; i < live.size(); ++i) {
        if (live[i] != 0x90) return false;
    }
    return true;
}

// ---------------------------------------------------------------- counters (relaxed atomics, fixed size)

struct Counters {
    std::atomic<uint64_t> calls{0};      // every ToTarget/ToControlUser vf1 call
    std::atomic<uint64_t> observing{0};  // of those, during an observer trip
    std::array<std::atomic<uint64_t>, kVerdicts> verdicts{};
    std::array<std::atomic<uint32_t>, kWriterCount> by_writer{};  // HostTarget by writer
    std::array<std::atomic<uint32_t>, kWriterCount> by_writer_control{};  // ControlUserHost by writer
    u2f::SiteCounts<64> unknown;                                  // HostTarget from a return not in the table
    u2f::SiteCounts<32> unknown_control;                          // ControlUserHost, the same

    void count(Verdict v) { verdicts[static_cast<size_t>(v)].fetch_add(1, std::memory_order_relaxed); }
    void count_host(uint32_t ret_rva) {
        count(Verdict::HostTarget);
        if (const int i = writer_index(ret_rva); i >= 0) {
            by_writer[static_cast<size_t>(i)].fetch_add(1, std::memory_order_relaxed);
        } else {
            unknown.add(ret_rva);
        }
    }
    void count_control_host(uint32_t ret_rva) {
        count(Verdict::ControlUserHost);
        if (const int i = writer_index(ret_rva); i >= 0) {
            by_writer_control[static_cast<size_t>(i)].fetch_add(1, std::memory_order_relaxed);
        } else {
            unknown_control.add(ret_rva);
        }
    }
};

// A plain copy (the summary's thread): totals now, and the deltas against an older one.
struct Snapshot {
    uint64_t calls = 0, observing = 0;
    std::array<uint64_t, kVerdicts> verdicts{};
    std::vector<uint32_t> by_writer = std::vector<uint32_t>(kWriterCount, 0);
    std::vector<uint32_t> by_writer_control = std::vector<uint32_t>(kWriterCount, 0);
};

inline Snapshot snapshot(const Counters& c) {
    Snapshot s;
    s.calls = c.calls.load(std::memory_order_relaxed);
    s.observing = c.observing.load(std::memory_order_relaxed);
    for (size_t i = 0; i < kVerdicts; ++i) s.verdicts[i] = c.verdicts[i].load(std::memory_order_relaxed);
    for (size_t i = 0; i < kWriterCount; ++i) {
        s.by_writer[i] = c.by_writer[i].load(std::memory_order_relaxed);
        s.by_writer_control[i] = c.by_writer_control[i].load(std::memory_order_relaxed);
    }
    return s;
}

// HostTarget calls by family (the table's opcodes; returns not in the table are apart).
inline std::array<uint64_t, kFamilies> family_totals(const std::vector<uint32_t>& by_writer) {
    std::array<uint64_t, kFamilies> t{};
    for (size_t i = 0; i < kWriterCount && i < by_writer.size(); ++i) {
        t[static_cast<size_t>(family_of(kWriters[i].opcode))] += by_writer[i];
    }
    return t;
}

// HostTarget calls of every writer of one opcode (a few opcodes have several writers).
inline uint64_t count_of_opcode(const std::vector<uint32_t>& by_writer, uint16_t op) {
    uint64_t n = 0;
    for (size_t i = 0; i < kWriterCount && i < by_writer.size(); ++i) {
        if (kWriters[i].opcode == op) n += by_writer[i];
    }
    return n;
}

struct TopRow {
    size_t index = 0;  // into kWriters
    uint64_t total = 0, delta = 0;
};

// The `k` writers with the most HostTarget calls (total), largest first; ties: the larger delta, then
// the table order. `before` may be shorter (all zero).
inline std::vector<TopRow> top_writers(const std::vector<uint32_t>& now, const std::vector<uint32_t>& before,
                                       size_t k) {
    std::vector<TopRow> v;
    for (size_t i = 0; i < kWriterCount && i < now.size(); ++i) {
        if (!now[i]) continue;
        const uint64_t b = i < before.size() ? before[i] : 0;
        v.push_back({i, now[i], now[i] >= b ? now[i] - b : now[i]});
    }
    std::stable_sort(v.begin(), v.end(), [](const TopRow& a, const TopRow& b) {
        return a.total != b.total ? a.total > b.total : a.delta > b.delta;
    });
    if (v.size() > k) v.resize(k);
    return v;
}

// ---------------------------------------------------------------- the comparison with option A

// The hook's count of one opcode against what the host's client received while observing (sniffer).
// They should be equal for an opcode whose only finder is ToTarget; `slack` absorbs the packets in
// flight when a trip begins or ends (the server writes, the host's client reads a moment later).
enum class Agreement : uint8_t { NoData, Match, HookMore, HookLess };

constexpr Agreement agree(uint64_t hook, uint64_t client, uint64_t slack) {
    if (!hook && !client) return Agreement::NoData;
    if (hook + slack >= client && client + slack >= hook) return Agreement::Match;
    return hook > client ? Agreement::HookMore : Agreement::HookLess;
}

inline const char* agreement_name(Agreement a) {
    switch (a) {
        case Agreement::NoData: return "no data";
        case Agreement::Match: return "MATCH";
        case Agreement::HookMore: return "DIFFER (hook more)";
        case Agreement::HookLess: return "DIFFER (hook less)";
    }
    return "?";
}

// The opcodes the comparison line shows: A's first (kCopiedByA), then every listed or doubtful one with data.
inline std::vector<uint16_t> compared_opcodes() {
    std::vector<uint16_t> v(std::begin(kCopiedByA), std::end(kCopiedByA));
    std::vector<uint16_t> rest;
    for (size_t i = 0; i < kWriterCount; ++i) {
        const uint16_t op = kWriters[i].opcode;
        const Family f = family_of(op);
        if ((listed(f) || f == Family::Doubtful) && !copied_by_a(op) &&
            std::find(rest.begin(), rest.end(), op) == rest.end()) {
            rest.push_back(op);
        }
    }
    std::sort(rest.begin(), rest.end());
    v.insert(v.end(), rest.begin(), rest.end());
    return v;
}

}  // namespace cdmp::session2::ofo
