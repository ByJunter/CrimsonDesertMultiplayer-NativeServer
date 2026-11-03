#pragma once
// P8 seq_actor_restore (docs/research/pendientes/p8-impl-seq-restore.md): [session2] seq_actor_restore =
// 0 | probe | 1, GUEST. The pure logic, with no game access (the reads go through a Reader the caller gives).
//
// The fault [V] (p8-s8-escenas.md §4.2, session 8): the host announces (2005) NPCs bound to one of its
// sequencers while they are in the middle of a scene action, seq_bodyanim_action (0x837487D4) on layer 0 and
// seq_bodyanim_action_loop (0x35DD392F) on layer 1. On the guest, which does not control them (mode 2, restore
// byte 1), the login restores those actions: RestoreActionsFromLoadData 0x8646D0 -> RestartLayerAction
// 0x863880 looks each hash up in the layer's chart ([layer+0x110]) and then in the dynamic graph [CC+0x70+8i],
// which only BodyAnim 0xA79770 installs when the scene plays. Neither has it: eErrNoInvalidActionChart, the
// login fails (FinishGroup 0x4FD491) and the actor is destroyed. 0.03-0.1 s later the copied 2661 of the
// sequencer arrives and 0xA9CE81 rejects the whole sequencer (a bound actor is missing). Session 8: the
// arm-wrestling table (sequencer 0x1B0, Freeswords 0xB0100F36-F3A) and Damiane's group (0x13, 7 actors).
// A native client never restores an NPC (it controls them all), so this path is new to the guest.
//
// The complement (p8-actores-anfitrion.md §4.1 (ii)), at a mid hook on 0x86490F inside 0x8646D0 (restore byte
// != 0, both layers' hashes already copied to the function's own stack entries, lock released, before the
// call 0x864AF9 of 0x863880): when a layer's hash is a seq_bodyanim one and neither chart has it (the exact
// lookup 0x863880 makes), that layer's entry gets the idle wait 0x26107D3C instead, when its layer chart has
// it (session 8: every layer-0 chart of 27173/28096/20268/19788 entries and layer-1 chart of 4645/7183 entries
// resolved 0x26107D3C for other NPCs), or hash 0 (0x863880 skips a layer with hash 0). If both layers end at
// 0, the native "restore byte 0" result is used: continue at 0x864B1A (mov [rsi],r14d with r14d = 0) and
// return success without restarting anything. The login then ends, the actor stays, and the copied 2661 finds
// it; the scene (guest_seq_play) installs the real action when it plays. A layer whose failing hash is NOT a
// seq_bodyanim one leaves the whole call untouched (the native error, counted).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace cdmp::session2::sar {

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
        case Mode::Probe: return "PROBE (decides and counts; writes nothing)";
        case Mode::On: return "ON";
        default: return "?";
    }
}

// The hashes (names: P8Probe's table, p8_probe_logic.h; the wait: p8-actores-anfitrion.md §1.3).
inline constexpr uint32_t kHashSeqBodyAnim = 0x837487D4;      // seq_bodyanim_action_xxx
inline constexpr uint32_t kHashSeqBodyAnimLoop = 0x35DD392F;  // seq_bodyanim_action_loop_xxx
inline constexpr uint32_t kHashWait = 0x26107D3C;             // the idle wait most NPCs restore with
inline bool seq_family(uint32_t hash) { return hash == kHashSeqBodyAnim || hash == kHashSeqBodyAnimLoop; }

// Actor key classes the complement may touch: other players' characters (0xA...) and other actors (0xB...).
inline bool key_class_ok(uint32_t key) {
    const uint32_t c = key >> 28;
    return c == 0xA || c == 0xB;
}
inline bool user_key(uint32_t key) { return (key >> 28) == 0x9; }

// ---------------------------------------------------------------- the chart lookup (0x24314C0) [V capstone]
//
// 0x24314C0(chart, hash, out): count u32 [chart+0x20], array [chart+0x18] of 24-byte entries {p0, p1, p2},
// key u32 [p2+0x18], sorted; binary search lo = 0, hi = count - 1, mid = lo + (hi - lo) / 2:
//   p0 && p1 && p2 && hash < key -> hi = mid - 1;
//   else k = (p0 && p1 && p2) ? key : 0; k >= hash -> found (the triple is copied, AL = 1); else lo = mid + 1.
// 0x863880 then needs the triple complete (all three non-null, 0x863952..0x863968). A pure read: no lock, no
// allocation. The Reader: bool r32(uint64_t addr, uint32_t&) and bool r64(uint64_t addr, uint64_t&).
inline constexpr uint32_t kChart_Array = 0x18, kChart_Count = 0x20, kEntry_Size = 24, kAction_Hash = 0x18;
inline constexpr uint32_t kMaxSteps = 40;  // > log2(2^32): a torn count cannot loop for long

enum class Lookup : uint8_t { NoChart, NotFound, Found, Unreadable };
inline const char* lookup_name(Lookup l) {
    switch (l) {
        case Lookup::NoChart: return "no chart";
        case Lookup::NotFound: return "not found";
        case Lookup::Found: return "found";
        case Lookup::Unreadable: return "unreadable";
        default: return "?";
    }
}

template <class Reader>
Lookup chart_find(const Reader& rd, uint64_t chart, uint32_t hash) {
    if (!chart) return Lookup::NoChart;
    uint32_t count = 0;
    uint64_t arr = 0;
    if (!rd.r32(chart + kChart_Count, count) || !rd.r64(chart + kChart_Array, arr)) return Lookup::Unreadable;
    if (count == 0 || count > 0x7FFFFFFFu) return Lookup::NotFound;
    if (!arr) return Lookup::Unreadable;
    int64_t lo = 0, hi = static_cast<int64_t>(count) - 1;
    for (uint32_t step = 0; hi >= lo; ++step) {
        if (step >= kMaxSteps) return Lookup::Unreadable;
        const int64_t mid = lo + (hi - lo) / 2;
        const uint64_t e = arr + static_cast<uint64_t>(mid) * kEntry_Size;
        uint64_t p0 = 0, p1 = 0, p2 = 0;
        if (!rd.r64(e, p0) || !rd.r64(e + 8, p1) || !rd.r64(e + 16, p2)) return Lookup::Unreadable;
        uint32_t key = 0;
        const bool full = p0 && p1 && p2;
        if (full && !rd.r32(p2 + kAction_Hash, key)) return Lookup::Unreadable;
        if (full && hash < key) {
            hi = mid - 1;
            continue;
        }
        const uint32_t k = full ? key : 0;
        if (k >= hash) return full ? Lookup::Found : Lookup::NotFound;  // the triple 0x863880 checks
        lo = mid + 1;
    }
    return Lookup::NotFound;
}

// ---------------------------------------------------------------- the decision

// The entry slot of the action's "index" word: entry +0x3A (0x864834 movzx eax,word [rcx+0x38] -> 0x864838
// mov [rbp-0x76],ax for entry 0 = [rsp+0x50+0x3A]; 0x8648A0 mov [rbp+2],ax for entry 1). It is copied from the
// layer's CURRENT action (the seq_bodyanim one), so a substituted entry gets the value a fresh entry has:
// 0xFFFF (the entry constructor 0x1EC98F0 at 0x1EC994F, and 0x1EC72E0 when the game itself replaces an entry's
// action by a dynamic one). Every other field the restore writes (0x86481C..0x86485F / 0x864884..0x8648C7) is
// the same constant for every restore, whatever the action.
inline constexpr uint16_t kNoIndex = 0xFFFF;

// What the hook read at 0x86490F (SEH reads; the tests by hand).
struct Input {
    Mode mode = Mode::Off;
    uint32_t key = 0;       // [[CC+8]+0x60]
    uint32_t control = 0;   // [[CC+8]+0x90]
    uint32_t local = 0;     // this client's user key (0: not known yet)
    uint64_t chart[2] = {};     // [rsp+0x38] / [rsp+0x40] = [layer i +0x110] (0x8647FB / 0x864870)
    uint64_t fallback[2] = {};  // [CC+0x70] / [CC+0x78]
    // [rsp+0x50] / [rbp-0x38]: the entries' hashes, read always (0x863880 reads them always). Without a chart
    // the restore does not write it and it is the constructor's 0 (0x1EC98F0: mov [rcx],r9 with r9 = 0, run on
    // both entries by the loop 0x864702..0x86471A).
    uint32_t hash[2] = {};
    bool frame_ok = false;      // rbp == rsp + 0x100 (the prologue 0x8646E6 lea rbp,[rsp-0x60] ; sub rsp,0x160)
    bool exit_regs_ok = false;  // r14d == 0 and rsi == the res home slot [rbp+0xA8]: 0x864B1A is safe
};

enum class Act : uint8_t { Keep, Substitute, Drop };
inline const char* act_name(Act a) {
    switch (a) {
        case Act::Keep: return "kept";
        case Act::Substitute: return "-> wait 0x26107D3C (index word 0xFFFF)";
        case Act::Drop: return "-> 0 (layer skipped)";
        default: return "?";
    }
}

enum class Verdict : uint8_t {
    NoSeqHash,      // neither layer carries a seq_bodyanim hash: native (the hook's fast path)
    NotRemote,      // the actor is this client's own, or its key class is not 0xA/0xB: native
    LocalUnknown,   // this client's user key is not known yet (no controlled actor): native, counted
    BadFrame,       // rbp != rsp + 0x100: the stack slots are not the ones verified: untouched
    Resolvable,     // the seq_bodyanim hashes resolve (a chart has them): native
    OtherFailure,   // another layer would fail with a hash that is not seq_bodyanim: untouched (native error)
    Unreadable,     // a chart could not be read: untouched
    Substituted,    // at least one layer gets the wait; 0x863880 runs natively on the entries
    Skipped,        // both layers end at 0: continue at 0x864B1A, result 0 (the native "restore byte 0")
    CannotSkip,     // both would end at 0 but the exit registers are not the expected ones: untouched
    Count
};
inline const char* verdict_name(Verdict v) {
    switch (v) {
        case Verdict::NoSeqHash: return "no seq_bodyanim hash (native)";
        case Verdict::NotRemote: return "not a remote actor (native)";
        case Verdict::LocalUnknown: return "local user not known yet (native)";
        case Verdict::BadFrame: return "unexpected stack frame: untouched";
        case Verdict::Resolvable: return "seq_bodyanim hash resolvable (native)";
        case Verdict::OtherFailure: return "another hash fails too: untouched (native error)";
        case Verdict::Unreadable: return "a chart is unreadable: untouched";
        case Verdict::Substituted: return "SUBSTITUTED (idle wait), 0x863880 runs natively";
        case Verdict::Skipped: return "SKIPPED (no restore, result 0: the native restore-byte-0 path)";
        case Verdict::CannotSkip: return "cannot skip (unexpected r14/rsi): untouched";
        default: return "?";
    }
}
// The verdicts that change what the game does (with mode On).
inline bool changes(Verdict v) { return v == Verdict::Substituted || v == Verdict::Skipped; }

struct LayerPlan {
    Lookup in_chart = Lookup::NoChart, in_fallback = Lookup::NoChart, wait = Lookup::NoChart;
    bool fails = false;  // the native 0x863880 would fail on this layer
    Act act = Act::Keep;
    uint32_t new_hash = 0;
};
struct Plan {
    Verdict verdict = Verdict::NoSeqHash;
    LayerPlan layer[2];
    bool write[2] = {};       // write new_hash into the entry (Substituted, mode On only)
    bool write_index[2] = {}; // also its index word = kNoIndex (a Substitute layer, mode On only)
    bool skip = false;        // continue at 0x864B1A (Skipped, mode On only)
};

// Every decision, in order. Lookups only for the layers that need one (the hook calls this only when a hash
// is seq_bodyanim, so the common restore costs two compares).
template <class Reader>
Plan plan(const Reader& rd, const Input& in) {
    Plan p;
    bool any_seq = false;
    for (int i = 0; i < 2; ++i) any_seq |= seq_family(in.hash[i]);
    if (!any_seq) return p;  // NoSeqHash
    if (!key_class_ok(in.key) || !in.control || in.control == in.local) {
        p.verdict = Verdict::NotRemote;
        return p;
    }
    if (!in.local) {  // review 5: never decide "remote" without knowing who we are
        p.verdict = Verdict::LocalUnknown;
        return p;
    }
    if (!in.frame_ok) {
        p.verdict = Verdict::BadFrame;
        return p;
    }
    bool seq_fail = false, other_fail = false, unreadable = false;
    for (int i = 0; i < 2; ++i) {
        LayerPlan& l = p.layer[i];
        if (!in.hash[i]) continue;  // 0x863880 skips a layer with hash 0 (0x8638F6)
        // 0x863880: the layer chart if there is one (0x863905), then the fallback (0x863918).
        l.in_chart = chart_find(rd, in.chart[i], in.hash[i]);
        if (l.in_chart == Lookup::Found) continue;
        l.in_fallback = chart_find(rd, in.fallback[i], in.hash[i]);
        if (l.in_fallback == Lookup::Found) continue;
        if (l.in_chart == Lookup::Unreadable || l.in_fallback == Lookup::Unreadable) {
            unreadable = true;
            continue;
        }
        l.fails = true;
        (seq_family(in.hash[i]) ? seq_fail : other_fail) = true;
    }
    if (unreadable) {
        p.verdict = Verdict::Unreadable;
        return p;
    }
    if (other_fail) {
        p.verdict = Verdict::OtherFailure;
        return p;
    }
    if (!seq_fail) {
        p.verdict = Verdict::Resolvable;
        return p;
    }
    bool any_left = false;
    for (int i = 0; i < 2; ++i) {
        LayerPlan& l = p.layer[i];
        if (!l.fails) {
            if (in.hash[i]) any_left = true;
            continue;
        }
        l.wait = chart_find(rd, in.chart[i], kHashWait);
        if (l.wait == Lookup::Found) {
            l.act = Act::Substitute;
            l.new_hash = kHashWait;
            any_left = true;
        } else {
            l.act = Act::Drop;
            l.new_hash = 0;
        }
    }
    if (!any_left) {
        if (!in.exit_regs_ok) {
            for (LayerPlan& l : p.layer) l.act = Act::Keep;
            p.verdict = Verdict::CannotSkip;
            return p;
        }
        p.verdict = Verdict::Skipped;
        p.skip = in.mode == Mode::On;
        return p;  // nothing written: the entries are never read
    }
    p.verdict = Verdict::Substituted;
    for (int i = 0; i < 2; ++i) {
        p.write[i] = in.mode == Mode::On && p.layer[i].act != Act::Keep;
        p.write_index[i] = in.mode == Mode::On && p.layer[i].act == Act::Substitute;
    }
    return p;
}

// Review 4: the dynamic graph [CC+0x70/+0x78] the plan saw is still the one there right before the writes
// (BodyAnim 0xA79770 installs it; in session 8 it ran on the same thread as every restore, so this only
// narrows a window that was not observed). False: something changed, write nothing.
inline bool still_valid(const Plan& p, const Input& in, const uint64_t now_fallback[2]) {
    for (int i = 0; i < 2; ++i) {
        if (p.layer[i].act != Act::Keep && now_fallback[i] != in.fallback[i]) return false;
    }
    return true;
}

// Review 1: a site another module may have mid-hooked (guest_actor_diag / companion_probe at 0x8646FC and
// 0x864B1D). Original: the build's bytes. ForeignHook: a SafetyHook jmp (E9 rel32) whose target is outside the
// image (its trampoline, which runs the relocated original instructions and comes back). Anything else: Mismatch.
enum class Shared : uint8_t { Original, ForeignHook, Mismatch };
inline Shared classify_shared(std::span<const uint8_t> got, std::span<const uint8_t> original, uint64_t site,
                              uint64_t image_lo, uint64_t image_hi) {
    if (got.size() != original.size() || got.size() < 5) return Shared::Mismatch;
    bool same = true;
    for (size_t i = 0; i < got.size(); ++i) same = same && got[i] == original[i];
    if (same) return Shared::Original;
    if (got[0] != 0xE9) return Shared::Mismatch;
    const int32_t rel = static_cast<int32_t>(static_cast<uint32_t>(got[1]) | static_cast<uint32_t>(got[2]) << 8 |
                                             static_cast<uint32_t>(got[3]) << 16 | static_cast<uint32_t>(got[4]) << 24);
    const uint64_t target = site + 5 + static_cast<int64_t>(rel);
    return target >= image_lo && target < image_hi ? Shared::Mismatch : Shared::ForeignHook;
}

// Review 8: what the summary counts per decision (layers only for Substituted; a skip is its own count).
struct LayerCounts {
    uint32_t substituted = 0, dropped = 0;
};
inline LayerCounts layer_counts(const Plan& p) {
    LayerCounts c;
    if (p.verdict != Verdict::Substituted) return c;
    for (const LayerPlan& l : p.layer) {
        if (l.act == Act::Substitute) ++c.substituted;
        if (l.act == Act::Drop) ++c.dropped;
    }
    return c;
}

// ---------------------------------------------------------------- the keys the complement touched

// A small set of keys (writers: the game threads that run logins; reader: the diagnostics thread), for the
// 60 s summary's "is it still there" check. Lock-free (a slot is reserved by CAS on the count), bounded; a full
// set only stops remembering. A reader can see a reserved slot before its key: it reads 0 and skips it. Two
// writers adding the same key at once may both keep it (a duplicate line, nothing else).
template <size_t N>
class KeyLog {
public:
    // false: already there or full.
    bool add(uint32_t key) {
        if (!key) return false;
        size_t n = n_.load(std::memory_order_acquire);
        for (;;) {
            for (size_t i = 0; i < n && i < N; ++i) {
                if (keys_[i].load(std::memory_order_relaxed) == key) return false;
            }
            if (n >= N) {
                full_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            if (n_.compare_exchange_weak(n, n + 1, std::memory_order_acq_rel, std::memory_order_acquire)) break;
        }
        keys_[n].store(key, std::memory_order_release);
        return true;
    }
    size_t size() const { return n_.load(std::memory_order_acquire); }
    uint32_t at(size_t i) const { return i < size() ? keys_[i].load(std::memory_order_relaxed) : 0; }
    uint64_t full() const { return full_.load(std::memory_order_relaxed); }

private:
    std::atomic<uint32_t> keys_[N] = {};
    std::atomic<size_t> n_{0};
    std::atomic<uint64_t> full_{0};
};

}  // namespace cdmp::session2::sar
