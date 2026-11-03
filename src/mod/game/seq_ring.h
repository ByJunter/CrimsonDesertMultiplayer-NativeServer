#pragma once
// A fixed ring of u64 values in the order they were pushed, for many writers (a game hook) and one reader (a mod
// thread), lock-free and without allocation. Used by game_errors.cpp for CreateOwnedActor's failures (P8 session 9,
// child_reannounce; review 2026-10-10: the index comes from the writer's own fetch_add, the reader never reads more
// than N - 1 behind the count and checks each slot's sequence).

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace cdmp::game {

template <size_t N>
class SeqRing {
    static_assert(N >= 2, "N >= 2");

public:
    // Any thread: the index from fetch_add; the slot's sequence is cleared, the value written, then the
    // sequence set to index + 1 (a reader that saw the old sequence notices the change).
    void push(uint64_t v) noexcept {
        const uint64_t i = n_.fetch_add(1, std::memory_order_acq_rel);
        Slot& s = s_[i % N];
        s.seq.store(0, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        s.val.store(v, std::memory_order_relaxed);
        s.seq.store(i + 1, std::memory_order_release);
    }
    uint64_t count() const noexcept { return n_.load(std::memory_order_acquire); }

    // One reader: the values from `cursor` on, at most `max`; the cursor moves past what was read or lost. A slot
    // whose writer has not finished stops the read (the next call takes it); one already overwritten is lost.
    size_t read(uint64_t& cursor, uint64_t* out, size_t max, uint64_t* lost = nullptr) const noexcept {
        uint64_t gone = 0;
        const uint64_t n = count();
        if (cursor > n) cursor = n;
        if (n - cursor > N - 1) {
            gone += n - cursor - (N - 1);
            cursor = n - (N - 1);
        }
        size_t k = 0;
        while (cursor < n && k < max) {
            const Slot& s = s_[cursor % N];
            const uint64_t s1 = s.seq.load(std::memory_order_acquire);
            if (s1 == cursor + 1) {
                const uint64_t v = s.val.load(std::memory_order_relaxed);
                std::atomic_thread_fence(std::memory_order_acquire);
                if (s.seq.load(std::memory_order_relaxed) != s1) {
                    ++gone;  // rewritten while read
                } else {
                    out[k++] = v;
                }
                ++cursor;
                continue;
            }
            if (s1 > cursor + 1) {  // already overwritten by a later index
                ++gone;
                ++cursor;
                continue;
            }
            break;  // its writer is still writing: next time
        }
        if (lost) *lost = gone;
        return k;
    }

private:
    struct Slot {
        std::atomic<uint64_t> seq{0};
        std::atomic<uint64_t> val{0};
    };
    Slot s_[N];
    std::atomic<uint64_t> n_{0};
};

}  // namespace cdmp::game
