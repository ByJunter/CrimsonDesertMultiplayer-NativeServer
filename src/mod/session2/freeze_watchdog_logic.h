#pragma once
// P8 session 10, the guest's main-loop freezes (docs/research/pendientes/p8-s10.md §2; the implementation:
// docs/research/pendientes/p8-impl-freeze-watchdog.md). The pure logic of freeze_watchdog.cpp, with no game
// and no Windows call, so the tests can run it:
//   - FreezeWatch: when the frame counter stops for > kStallMs, when to take a stack sample (a few at 50 ms,
//     then one per second, bounded per episode and per session), and the episode's end;
//   - is_call_before: does a stack value look like a return address (a call instruction right before it)?
//   - MarkTable: per thread, which mod hook it is in (or last left), written by the hooks with two relaxed
//     stores, read by the watchdog for the frozen thread.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace cdmp::session2::fwd {

inline constexpr uint64_t kStallMs = 300;           // no frame for longer than this: an episode
inline constexpr uint64_t kFastEveryMs = 50;        // the first samples of an episode, this far apart
inline constexpr uint32_t kFastSamples = 5;         // ... this many
inline constexpr uint64_t kSlowEveryMs = 1000;      // then one per second while it lasts
// The caps are on SUSPENSIONS (each one a sample and its line), not only on lines (review of 10-10).
inline constexpr uint32_t kMaxSamplesPerEpisode = 20;
inline constexpr uint32_t kMaxSamplesPerSession = 600;  // then episodes are only timed (no thread is suspended)
inline constexpr uint32_t kMaxEpisodeLines = 300;       // per session (then only counted)

// ---------------------------------------------------------------- the episode machine

class FreezeWatch {
public:
    enum class Kind : uint8_t { None, Sample, End };
    struct Step {
        Kind kind = Kind::None;
        uint32_t episode = 0;
        uint32_t sample = 0;      // 1.. inside the episode (Sample)
        uint64_t stalled_ms = 0;  // since the last frame (Sample) / the whole freeze (End)
        uint32_t samples = 0;     // taken in the episode (End)
        bool log_end = false;     // within the session's episode budget (End)
    };

    // Every poll: the frame counter as read now.
    Step observe(uint64_t now_ms, uint64_t frames) {
        Step s;
        if (!seen_) {
            seen_ = true;
            frames_ = frames;
            last_change_ = now_ms;
            return s;
        }
        if (frames != frames_) {
            frames_ = frames;
            started_ = true;  // episodes only once frames have been seen advancing (not at start-up / title)
            const uint64_t stalled = now_ms - last_change_;
            last_change_ = now_ms;
            if (!in_episode_) return s;
            in_episode_ = false;
            s.kind = Kind::End;
            s.episode = episodes_;
            s.stalled_ms = stalled;
            s.samples = taken_in_episode_;
            s.log_end = episode_lines_ < kMaxEpisodeLines;
            if (s.log_end) ++episode_lines_;
            return s;
        }
        if (!started_ || now_ms - last_change_ <= kStallMs) return s;
        if (!in_episode_) {
            in_episode_ = true;
            ++episodes_;
            samples_ = 0;
            taken_in_episode_ = 0;
            next_sample_ = now_ms;
        }
        if (samples_ >= kMaxSamplesPerEpisode || now_ms < next_sample_) return s;
        next_sample_ = now_ms + (samples_ + 1 < kFastSamples ? kFastEveryMs : kSlowEveryMs);
        // The session's budget is checked BEFORE a sample: past it, nothing is suspended (counted only).
        if (session_samples_ >= kMaxSamplesPerSession) {
            ++skipped_;
            ++samples_;  // the per-episode cadence goes on, so the skip count is what would have been taken
            return s;
        }
        ++samples_;
        ++session_samples_;
        ++taken_in_episode_;
        s.kind = Kind::Sample;
        s.episode = episodes_;
        s.sample = samples_;
        s.stalled_ms = now_ms - last_change_;
        return s;
    }
    bool started() const { return started_; }
    bool in_episode() const { return in_episode_; }
    uint32_t episodes() const { return episodes_; }
    uint32_t samples_taken() const { return session_samples_; }  // = suspensions in the session
    uint32_t samples_skipped() const { return skipped_; }

private:
    bool seen_ = false, started_ = false, in_episode_ = false;
    uint64_t frames_ = 0, last_change_ = 0, next_sample_ = 0;
    uint32_t episodes_ = 0, samples_ = 0, session_samples_ = 0, taken_in_episode_ = 0, episode_lines_ = 0,
             skipped_ = 0;
};

// ---------------------------------------------------------------- return addresses

// p[0..6] = the 7 bytes right BEFORE a candidate return address. True if one of the x64 call forms ends exactly
// there: E8 rel32 (5), FF /2 with ModRM register or [reg] (2), REX FF /2 (3), FF /2 disp8 or SIB (3), FF /2
// SIB+disp8 or REX FF /2 disp8 (4), REX FF /2 SIB+disp8 (5), FF /2 disp32 or [rip+disp32] (6), FF /2
// SIB+disp32 or REX FF /2 disp32 (7). A heuristic (a stack scan, not an unwind): data can look like this.
inline bool modrm_call(uint8_t m, int& extra) {
    // reg field must be 2 (call); returns the bytes after the ModRM (SIB + displacement).
    if (((m >> 3) & 7) != 2) return false;
    const uint8_t mod = m >> 6, rm = m & 7;
    if (mod == 3) {
        extra = 0;
        return true;
    }
    int n = rm == 4 ? 1 : 0;  // SIB
    if (mod == 0 && rm == 5) n += 4;  // [rip+disp32]
    if (mod == 1) n += 1;
    if (mod == 2) n += 4;
    // mod 0 with SIB base 5 (disp32) is not decoded here (rare in calls): counted as no displacement.
    extra = n;
    return true;
}
inline bool is_rex(uint8_t b) { return b >= 0x40 && b <= 0x4F; }

inline bool is_call_before(const uint8_t p[7]) {
    if (p[2] == 0xE8) return true;  // call rel32
    // FF ModRM [extra]: the instruction is 2 + extra bytes, optionally after a REX prefix.
    for (int len = 2; len <= 7; ++len) {
        const int at = 7 - len;  // where FF would be
        if (p[at] == 0xFF) {
            int extra = 0;
            if (modrm_call(p[at + 1], extra) && 2 + extra == len) return true;
        }
        if (len >= 3 && is_rex(p[at]) && p[at + 1] == 0xFF) {
            int extra = 0;
            if (modrm_call(p[at + 2], extra) && 3 + extra == len) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- which hook a thread is in

// One thread: the mod hook it is in now (0 = none; else the hook's id) and the last one it left. Written only by
// that thread (relaxed stores), read by the watchdog (a torn view only mislabels one line).
struct ThreadMark {
    std::atomic<uint32_t> tid{0};
    std::atomic<uint16_t> in{0};
    std::atomic<uint16_t> last{0};
    std::atomic<uint64_t> enter_ms{0};
    std::atomic<uint64_t> leave_ms{0};
};

template <size_t N>
class MarkTable {
public:
    // The calling thread's mark (made once; nullptr when the table is full: that thread is simply not marked).
    ThreadMark* get_or_add(uint32_t tid) noexcept {
        for (auto& m : marks_) {
            uint32_t cur = m.tid.load(std::memory_order_acquire);
            if (cur == tid) return &m;
            if (cur == 0 && m.tid.compare_exchange_strong(cur, tid, std::memory_order_acq_rel)) return &m;
            if (cur == tid) return &m;
        }
        full_.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    const ThreadMark* find(uint32_t tid) const noexcept {
        for (const auto& m : marks_) {
            if (m.tid.load(std::memory_order_acquire) == tid) return &m;
        }
        return nullptr;
    }
    uint64_t full() const noexcept { return full_.load(std::memory_order_relaxed); }

private:
    std::array<ThreadMark, N> marks_{};
    std::atomic<uint64_t> full_{0};
};

// Enter / leave (nesting: the caller keeps what `in` was and puts it back).
inline uint16_t mark_enter(ThreadMark& m, uint16_t id, uint64_t now_ms) noexcept {
    const uint16_t prev = m.in.load(std::memory_order_relaxed);
    m.in.store(id, std::memory_order_relaxed);
    if (!prev) m.enter_ms.store(now_ms, std::memory_order_relaxed);
    return prev;
}
inline void mark_leave(ThreadMark& m, uint16_t id, uint16_t prev, uint64_t now_ms) noexcept {
    m.in.store(prev, std::memory_order_relaxed);
    m.last.store(id, std::memory_order_relaxed);
    m.leave_ms.store(now_ms, std::memory_order_relaxed);
}

// RAII (review of 10-10): the mark is left by the destructor, also when a C++ exception goes through the hook.
// (With /EHsc an SEH exception handled further up does not run destructors: the mark may then stay "in".)
template <class Clock>
class MarkScope {
public:
    MarkScope(ThreadMark* m, uint16_t id, Clock clock) noexcept : m_(id ? m : nullptr), id_(id), clock_(clock) {
        if (m_) prev_ = mark_enter(*m_, id_, clock_());
    }
    ~MarkScope() {
        if (m_) mark_leave(*m_, id_, prev_, clock_());
    }
    MarkScope(const MarkScope&) = delete;
    MarkScope& operator=(const MarkScope&) = delete;

private:
    ThreadMark* m_;
    uint16_t id_, prev_ = 0;
    Clock clock_;
};

// ---------------------------------------------------------------- the main thread

// The frame source's thread (review of 10-10): the FIRST thread that ticks a main-loop hook is the main thread;
// a tick from another thread is not counted (it could hide the main thread's freeze) and is reported.
struct MainThread {
    std::atomic<uint32_t> tid{0};
    std::atomic<uint64_t> frames{0};
    std::atomic<uint64_t> foreign{0};       // ticks from another thread
    std::atomic<uint32_t> foreign_tid{0};   // the last such thread

    void tick(uint32_t me) noexcept {
        uint32_t cur = tid.load(std::memory_order_relaxed);
        if (cur == 0 && tid.compare_exchange_strong(cur, me, std::memory_order_relaxed)) cur = me;
        if (cur != me) {
            foreign.fetch_add(1, std::memory_order_relaxed);
            foreign_tid.store(me, std::memory_order_relaxed);
            return;
        }
        frames.fetch_add(1, std::memory_order_relaxed);
    }
};

}  // namespace cdmp::session2::fwd
