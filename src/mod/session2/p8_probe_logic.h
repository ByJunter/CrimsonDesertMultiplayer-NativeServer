#pragma once
// P8 root causes S1/S2 (docs/research/pendientes/p8-raiz-invitado.md §3, §4,
// §7.1, §7.2; docs/research/pendientes/p8-impl-s1s2.md): the pure logic of the
// read-only probes, with no game access, so the tests can run it. The hooks
// are p8_probes.cpp (S1/S2 points) and guest_actor_diag.cpp (the loading
// state's transitions).
//
// Everything here is observation: nothing decides for the game, nothing calls
// the game. The hooks copy registers and a few guarded reads into fixed-size
// events; a periodic thread formats them.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cdmp::session2::p8p {

// ---------------------------------------------------------------- events

// One observation from a hook: a fixed-size POD (no allocation, no strings on
// the game's threads). `kind` says how the drain formats v[].
struct Event {
    uint64_t qpc_us = 0;  // QueryPerformanceCounter in microseconds (the sniffer's t_us clock)
    uint32_t tid = 0;
    uint16_t kind = 0;
    uint16_t flags = 0;
    uint64_t v[12] = {};
};

// Field-by-field copy: no memcpy (the CRT's may use AVX and clobber the upper
// halves of the game's YMM registers inside a mid hook).
inline void copy_event(Event& dst, const Event& src) {
    dst.qpc_us = src.qpc_us;
    dst.tid = src.tid;
    dst.kind = src.kind;
    dst.flags = src.flags;
    for (size_t i = 0; i < 12; ++i) dst.v[i] = src.v[i];
}

// Bounded multi-producer multi-consumer queue (D. Vyukov's): lock-free, no
// allocation after construction. push() fails when full (the caller counts a
// drop); it never blocks a game thread.
template <size_t N>
class EventRing {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two");

public:
    EventRing() {
        for (size_t i = 0; i < N; ++i) cells_[i].seq.store(i, std::memory_order_relaxed);
    }
    EventRing(const EventRing&) = delete;
    EventRing& operator=(const EventRing&) = delete;

    bool push(const Event& e) {
        size_t pos = enq_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& c = cells_[pos & (N - 1)];
            const size_t seq = c.seq.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (dif == 0) {
                if (enq_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    copy_event(c.e, e);
                    c.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (dif < 0) {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return false;  // full
            } else {
                pos = enq_.load(std::memory_order_relaxed);
            }
        }
    }

    bool pop(Event& out) {
        size_t pos = deq_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& c = cells_[pos & (N - 1)];
            const size_t seq = c.seq.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            if (dif == 0) {
                if (deq_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    copy_event(out, c.e);
                    c.seq.store(pos + N, std::memory_order_release);
                    return true;
                }
            } else if (dif < 0) {
                return false;  // empty
            } else {
                pos = deq_.load(std::memory_order_relaxed);
            }
        }
    }

    uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
    static constexpr size_t capacity() { return N; }

private:
    struct Cell {
        std::atomic<size_t> seq{0};
        Event e;
    };
    Cell cells_[N];
    std::atomic<size_t> enq_{0};
    std::atomic<size_t> deq_{0};
    std::atomic<uint64_t> dropped_{0};
};

// ---------------------------------------------------------------- rate limits

// At most `per_window` events per window of `window_us` (QPC microseconds);
// the rest are refused and counted. Lock-free; at a window boundary two
// threads may both reset it (a few extra events, never a block).
class WindowLimiter {
public:
    constexpr WindowLimiter(uint32_t per_window, uint64_t window_us) : per_(per_window), window_us_(window_us) {}
    bool take(uint64_t now_us) {
        const uint64_t w = window_us_ ? now_us / window_us_ : 0;
        uint64_t cur = window_.load(std::memory_order_relaxed);
        if (cur != w && window_.compare_exchange_strong(cur, w, std::memory_order_relaxed)) {
            count_.store(0, std::memory_order_relaxed);
        }
        if (count_.fetch_add(1, std::memory_order_relaxed) < per_) return true;
        refused_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    uint64_t refused() const { return refused_.load(std::memory_order_relaxed); }

private:
    uint32_t per_;
    uint64_t window_us_;
    std::atomic<uint64_t> window_{~0ull};
    std::atomic<uint32_t> count_{0};
    std::atomic<uint64_t> refused_{0};
};

// Per-key event counts (0 is never a key): "how many times was this key seen
// at this point?" from game threads. Open addressing, bounded for the hot
// paths: at most kMaxProbe slots per call and N/2 keys; reaching that load
// closes the table for good (a sticky flag checked first: no insertion is
// tried any more, known keys still count through the bounded lookup). An
// untracked key gets 0 and the caller treats it as over its budget.
template <size_t N>
class KeyCounter {
    static_assert(N >= 4 && (N & (N - 1)) == 0, "N must be a power of two");

public:
    static constexpr size_t kMaxProbe = 32;
    static constexpr size_t kProbe = N < kMaxProbe ? N : kMaxProbe;
    static constexpr size_t kMaxKeys = N / 2;
    uint32_t bump(uint32_t key) {
        if (!key) return 0;
        const bool closed = closed_.load(std::memory_order_relaxed);
        size_t i = hash(key);
        for (size_t probe = 0; probe < kProbe; ++probe, i = (i + 1) & (N - 1)) {
            uint32_t cur = keys_[i].load(std::memory_order_relaxed);
            if (cur == 0) {
                if (closed) break;
                if (size_.load(std::memory_order_relaxed) >= kMaxKeys) {
                    closed_.store(true, std::memory_order_relaxed);
                    break;
                }
                if (keys_[i].compare_exchange_strong(cur, key, std::memory_order_relaxed)) {
                    size_.fetch_add(1, std::memory_order_relaxed);
                    cur = key;
                }
            }
            if (cur == key) return counts_[i].fetch_add(1, std::memory_order_relaxed) + 1;
        }
        full_.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    bool closed() const { return closed_.load(std::memory_order_relaxed); }
    uint32_t count(uint32_t key) const {
        if (!key) return 0;
        size_t i = hash(key);
        for (size_t probe = 0; probe < kProbe; ++probe, i = (i + 1) & (N - 1)) {
            const uint32_t cur = keys_[i].load(std::memory_order_relaxed);
            if (cur == key) return counts_[i].load(std::memory_order_relaxed);
            if (cur == 0) return 0;
        }
        return 0;
    }
    size_t size() const { return size_.load(std::memory_order_relaxed); }
    uint64_t full() const { return full_.load(std::memory_order_relaxed); }

private:
    static size_t hash(uint32_t key) { return (static_cast<size_t>(key) * 0x9E3779B1u) & (N - 1); }
    std::atomic<uint32_t> keys_[N] = {};
    std::atomic<uint32_t> counts_[N] = {};
    std::atomic<size_t> size_{0};
    std::atomic<bool> closed_{false};
    std::atomic<uint64_t> full_{0};
};

// The per-key budget a point applies: the first `first` events of each key, and
// every "interesting" event (a failure) while the point's failure budget lasts.
constexpr bool within_key_budget(uint32_t nth, uint32_t first) { return nth != 0 && nth <= first; }

// Change-only logging: true when `sig` differs from the last one offered
// (the first offer always counts as a change).
class ChangeGate {
public:
    bool changed(uint64_t sig) {
        const uint64_t old = last_.exchange(sig, std::memory_order_relaxed);
        const bool first = !armed_.exchange(true, std::memory_order_relaxed);
        return first || old != sig;
    }
    uint64_t last() const { return last_.load(std::memory_order_relaxed); }

private:
    std::atomic<uint64_t> last_{0};
    std::atomic<bool> armed_{false};
};

// A 64-bit mix for change signatures (splitmix64 finaliser over an accumulator).
constexpr uint64_t mix(uint64_t acc, uint64_t v) {
    uint64_t z = acc ^ (v + 0x9E3779B97F4A7C15ull + (acc << 6) + (acc >> 2));
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// The sniffer's clock (troc_sniffer.cpp now_us): QPC ticks to microseconds
// without overflow for long uptimes.
constexpr uint64_t qpc_to_us(uint64_t ticks, uint64_t freq) {
    return freq ? ticks / freq * 1000000ull + ticks % freq * 1000000ull / freq : 0;
}

// ---------------------------------------------------------------- S1: ActionGraph_Find 0x24314C0

// The graph: triples of 24 bytes at [graph+0x18], u32 count at [graph+0x20];
// a triple's hash is u32 [third+0x18]. Replayed from its instructions
// (0x24314CF..0x243158F, p8-raiz-invitado.md §3.1), with our own reads:
//   hi = count-1, lo = 0; while lo <= hi (signed):
//     mid = lo + (hi-lo)/2 (rounded toward zero, as sar after "jns/inc")
//     e = table + mid*24; p0,p1,p2 = e[0], e[8], e[0x10]
//     if p0 && p1 && p2 && hash < u32[p2+0x18]: hi = mid-1
//     else: k = (p0 && p1 && p2) ? u32[p2+0x18] : 0
//           if k >= hash: found at mid (the copy happens only if mid >= 0)
//           else lo = mid+1
struct GraphLayout {
    static constexpr uint32_t kTable = 0x18, kCount = 0x20, kTripleSize = 0x18, kHashInThird = 0x18;
};

struct FindResult {
    bool read_ok = false;  // every read the replay needed succeeded
    bool found = false;
    int32_t index = -1;
    uint32_t count = 0, iterations = 0;
    uintptr_t table = 0;
    uintptr_t triple[3] = {};
};

// Mem: bool u32(uintptr_t, uint32_t&) and bool ptr(uintptr_t, uintptr_t&), both checked reads.
// max_iter bounds a garbage count (a real search needs log2(count)+1 steps).
template <class Mem>
FindResult graph_find(const Mem& m, uintptr_t graph, uint32_t hash, uint32_t max_iter = 40) {
    using L = GraphLayout;
    FindResult r;
    if (!graph || !m.u32(graph + L::kCount, r.count)) return r;
    if (r.count == 0) {
        r.read_ok = true;
        return r;
    }
    int32_t hi = static_cast<int32_t>(r.count - 1), lo = 0;
    if (hi < 0) {
        r.read_ok = true;  // the native "js": a count above 0x7FFFFFFF finds nothing
        return r;
    }
    if (!m.ptr(graph + L::kTable, r.table)) return r;
    while (lo <= hi) {
        if (++r.iterations > max_iter) return r;  // garbage: read_ok stays false
        int32_t d = hi - lo;
        if (d < 0) ++d;
        const int32_t mid = (d >> 1) + lo;
        const uintptr_t e = r.table + static_cast<uintptr_t>(static_cast<intptr_t>(mid)) * L::kTripleSize;
        uintptr_t p0 = 0, p1 = 0, p2 = 0;
        if (!m.ptr(e, p0)) return r;
        bool go_left = false;
        uint32_t k = 0;
        if (p0) {
            if (!m.ptr(e + 8, p1)) return r;
            if (p1) {
                if (!m.ptr(e + 0x10, p2)) return r;
                if (p2) {
                    if (!m.u32(p2 + L::kHashInThird, k)) return r;
                    go_left = hash < k;
                }
            }
        }
        if (go_left) {
            hi = mid - 1;
            continue;
        }
        if (!(p0 && p1 && p2)) k = 0;
        if (k >= hash) {
            r.read_ok = true;
            if (mid < 0) return r;
            r.found = true;
            r.index = mid;
            r.triple[0] = p0;
            r.triple[1] = p1;
            r.triple[2] = p2;
            return r;
        }
        lo = mid + 1;
    }
    r.read_ok = true;
    return r;
}

// RestartLayerAction 0x863880's loop (0x8638F3..0x863936), per hash: the path it
// took, from the state at 0x86392A (both lookups replayed, no call).
enum class RestartPath : uint8_t {
    HashZero,         // je 0x86392A at 0x8638F8: nothing looked up
    Primary,          // [layer+0x110] had it (jne at 0x863916)
    Fallback,         // not in the layer's chart (or none), found in [CC+0x70+8i]
    FallbackMissing,  // fallback graph present, hash not in it
    NoGraph,          // no layer chart hit and no fallback graph
    Unreadable,       // a replay read failed
};

constexpr RestartPath classify_restart(uint32_t hash, bool chart, const FindResult& primary, bool fallback_graph,
                                       const FindResult& fallback) {
    if (hash == 0) return RestartPath::HashZero;
    if (chart) {
        if (!primary.read_ok) return RestartPath::Unreadable;
        if (primary.found) return RestartPath::Primary;
    }
    if (!fallback_graph) return RestartPath::NoGraph;
    if (!fallback.read_ok) return RestartPath::Unreadable;
    return fallback.found ? RestartPath::Fallback : RestartPath::FallbackMissing;
}

inline const char* restart_path_name(RestartPath p) {
    switch (p) {
        case RestartPath::HashZero: return "hash 0 (not looked up)";
        case RestartPath::Primary: return "layer chart [layer+0x110]";
        case RestartPath::Fallback: return "fallback [CC+0x70+8i]";
        case RestartPath::FallbackMissing: return "NOT FOUND (layer chart and fallback)";
        case RestartPath::NoGraph: return "NOT FOUND (no fallback graph)";
        case RestartPath::Unreadable: return "unreadable";
    }
    return "?";
}

// 0x863952..0x86398F: a layer's triple is usable only with all three pointers.
constexpr bool triple_complete(const uintptr_t (&t)[3]) { return t[0] && t[1] && t[2]; }

// The two hashes of S1 (p8-raiz-invitado.md §3.2): seq_bodyanim_action_xxx and
// seq_bodyanim_action_loop_xxx, lookup3 seed 0xC5EDE of the lower-case names.
inline constexpr uint32_t kHashSeqBodyAnim = 0x837487D4;
inline constexpr uint32_t kHashSeqBodyAnimLoop = 0x35DD392F;
constexpr bool s1_hash(uint32_t h) { return h == kHashSeqBodyAnim || h == kHashSeqBodyAnimLoop; }

// ---------------------------------------------------------------- S2: 0xA97D60's dependencies

// What 0xA97D60(M, pos) reads that we can read too (p8-raiz-invitado.md §4.1,
// decompilation 0xa97d60.c). Not readable without calling the game: the field
// context (0x1F6C540, first), 0x1AC2830 per entry of both lists, 0x5DB510 in
// the second list. An entry of list 1 blocks when [e+0x28C] == 0, or
// 0x1AC2830 != 0, or [e+0x61] == 5, or ([e+0x60] in {0,1} and pos inside the
// box [e+0xD8..+0xEC]); the last three are an OR, so a readable one blocks
// whatever 0x1AC2830 says.
// A POD on purpose (no member initialisers): the hook fills an array of them on
// the game's thread without the compiler zeroing it with a CRT memset call.
// Value-initialise it ({}) everywhere else.
struct List1Entry {
    bool readable;
    uint8_t flag28c, mode60, state61;
    float box[6];  // min x,y,z then max x,y,z
};

struct LoadSnapshot {
    bool m_ok = false;
    uint8_t b231 = 0;
    int32_t i174 = 0;
    uint8_t b2d4 = 0;
    float f2d0 = 0.0f, threshold = 0.0f;  // threshold: the float at exe+0x6CC8898
    float pos[3] = {};
    uint32_t list1_count = 0, list2_count = 0;
    std::span<const List1Entry> list1;  // the entries read (a prefix of list1_count)
};

enum class Blocker : uint8_t {
    None,           // AL = 1: nothing blocked
    Byte231,        // u8 M+0x231 < 2
    Busy174,        // i32 M+0x174 != 0
    Timer2D0,       // M+0x2D4 != 0 and f32 M+0x2D0 < threshold
    List1Flag28C,   // entry [e+0x28C] == 0
    List1State61,   // entry [e+0x61] == 5
    List1InBox,     // entry [e+0x60] in {0,1} and pos inside its box
    Unreadable,     // M or an entry needed for the answer could not be read
    Undetermined,   // AL = 0 and nothing readable explains it (field context, 0x1AC2830, list 2)
};

struct Attribution {
    Blocker blocker = Blocker::Undetermined;
    int32_t index = -1;          // the list-1 entry, for the List1* blockers
    uint32_t unknown_before = 0;  // list-1 entries before it whose 0x1AC2830 we cannot know
    bool list1_truncated = false;
};

constexpr bool in_box(const float (&b)[6], const float (&p)[3]) {
    return b[0] <= p[0] && b[1] <= p[1] && b[2] <= p[2] && p[0] <= b[3] && p[1] <= b[4] && p[2] <= b[5];
}

// The first dependency that certainly makes 0xA97D60 false, in its order.
inline Attribution attribute_a97d60(const LoadSnapshot& s, bool al) {
    Attribution a;
    if (al) {
        a.blocker = Blocker::None;
        return a;
    }
    if (!s.m_ok) {
        a.blocker = Blocker::Unreadable;
        return a;
    }
    if (s.b231 < 2) {
        a.blocker = Blocker::Byte231;
        return a;
    }
    if (s.i174 != 0) {
        a.blocker = Blocker::Busy174;
        return a;
    }
    if (s.b2d4 != 0 && s.f2d0 < s.threshold) {
        a.blocker = Blocker::Timer2D0;
        return a;
    }
    a.list1_truncated = s.list1.size() < s.list1_count;
    for (size_t i = 0; i < s.list1.size(); ++i) {
        const List1Entry& e = s.list1[i];
        if (!e.readable) {
            a.blocker = Blocker::Unreadable;
            a.index = static_cast<int32_t>(i);
            return a;
        }
        Blocker b = Blocker::Undetermined;
        if (e.flag28c == 0) {
            b = Blocker::List1Flag28C;
        } else if (e.state61 == 5) {
            b = Blocker::List1State61;
        } else if ((e.mode60 == 0 || e.mode60 == 1) && in_box(e.box, s.pos)) {
            b = Blocker::List1InBox;
        }
        if (b != Blocker::Undetermined) {
            a.blocker = b;
            a.index = static_cast<int32_t>(i);
            return a;
        }
        ++a.unknown_before;  // its 0x1AC2830 may have blocked already
    }
    a.blocker = Blocker::Undetermined;
    return a;
}

inline const char* blocker_name(Blocker b) {
    switch (b) {
        case Blocker::None: return "none (0xA97D60 true)";
        case Blocker::Byte231: return "M+0x231 < 2";
        case Blocker::Busy174: return "M+0x174 != 0";
        case Blocker::Timer2D0: return "M+0x2D4 set and M+0x2D0 < threshold";
        case Blocker::List1Flag28C: return "list M+0x1B0 entry +0x28C == 0";
        case Blocker::List1State61: return "list M+0x1B0 entry +0x61 == 5";
        case Blocker::List1InBox: return "list M+0x1B0 entry mode +0x60 in {0,1} and position inside its box";
        case Blocker::Unreadable: return "unreadable";
        case Blocker::Undetermined:
            return "undetermined by reads (field context 0x1F6C540, 0x1AC2830 of list M+0x1B0, or list M+0x1C0 "
                   "via 0x1AC2830/0x5DB510)";
    }
    return "?";
}

}  // namespace cdmp::session2::p8p
