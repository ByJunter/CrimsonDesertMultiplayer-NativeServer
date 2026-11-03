#pragma once
// The asynchronous logger's queue (log.cpp; docs/research/pendientes/p8-impl-log-async.md).
//
// P8 session 8 (p8-s8-congelacion.md §2): the logger held its lock for the whole WriteFile; one slow
// write (8.8 s on Shadow's disk) stopped at once the mod's network thread (and with it the Troc
// tunnel: the session fell), the diagnostics worker and the game's present thread. Now:
//
//   - producers (any thread, the game's too) copy one whole line into a FIXED buffer of the mod
//     (Capacity bytes, never allocated, never the game's heap) under a lock held only for that copy
//     (RAII guard); only producers ever take it;
//   - the consumer takes the bytes [tail, head) WITHOUT any lock: head is published (release) after
//     the bytes, producers never write into [tail, head), and the tail only moves forward
//     (compare-and-swap max, release) after the bytes were written, so no consumer path (the writer,
//     a crash path, the process-exit drain) can ever wait for the producers' lock;
//   - a line that does not fit is DROPPED and counted, never waited for; the next line that fits
//     (or the writer, when nobody logs) first puts "Log: N line(s) dropped here" where they were
//     lost, so the order of what is kept is the order of the pushes;
//   - a line longer than kMaxLine is cut (marked " [line cut: N bytes]");
//   - raw records (the packet sniffer's disk queue) may keep a reserve free for its loss notes.
//
// Pure logic: no Windows call, no allocation, no I/O. The lock is a template parameter (an SRW lock
// in the mod, std::mutex in the tests). head/tail are monotonic byte counters.

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string_view>

namespace cdmp::log {

template <size_t Capacity, class Lock>
class LineRing {
public:
    static_assert(Capacity >= 4096, "the ring must hold a few lines");
    static constexpr size_t kCapacity = Capacity;
    // A longer line is cut: one line never takes more than 1/8 of the ring (and never > 16 KB).
    static constexpr size_t kMaxLine = Capacity / 8 < 16384 ? Capacity / 8 : 16384;

    constexpr LineRing() = default;
    LineRing(const LineRing&) = delete;
    LineRing& operator=(const LineRing&) = delete;

    // One line: prefix + text + "\r\n", whole or not at all (dropped and counted). If lines were
    // dropped since the last note and `note_prefix` is given, the note goes first (only if the note
    // AND the line fit: else both wait, the line being dropped too). Never waits for anything but the
    // lock, which is only ever held for a copy.
    bool push(std::string_view prefix, std::string_view text, std::string_view note_prefix = {}) noexcept {
        char cut_mark[48];
        size_t cut_len = 0;
        size_t keep = text.size();
        const size_t fixed = prefix.size() + 2;
        if (fixed + keep > kMaxLine) {
            const size_t room = kMaxLine > fixed + sizeof cut_mark ? kMaxLine - fixed - sizeof cut_mark : 0;
            keep = std::min(keep, room);
            cut_len = cut_note(cut_mark, sizeof cut_mark, text.size() - keep);
        }
        const size_t line_len = fixed + keep + cut_len;
        std::lock_guard<Lock> guard(lock_);
        const uint64_t head = head_.load(std::memory_order_relaxed);
        char note[160];
        size_t note_len = 0;
        if (pending_dropped_ && !note_prefix.empty()) note_len = drop_note(note, sizeof note, note_prefix);
        if (line_len + note_len > free_bytes(head)) {
            ++pending_dropped_;
            count_drop();
            pending_atomic_.store(pending_dropped_, std::memory_order_relaxed);
            return false;
        }
        uint64_t at = head;
        if (note_len) {
            put(at, note, note_len);
            at += note_len;
            pending_dropped_ = 0;
            pending_atomic_.store(0, std::memory_order_relaxed);
            bump_lines();
        }
        put(at, prefix.data(), prefix.size());
        at += prefix.size();
        put(at, text.data(), keep);
        at += keep;
        if (cut_len) {
            put(at, cut_mark, cut_len);
            at += cut_len;
        }
        put(at, "\r\n", 2);
        at += 2;
        bump_lines();
        note_high(at);
        head_.store(at, std::memory_order_release);  // after the bytes: consumers read only whole lines
        return true;
    }

    // Raw bytes (a record of another file: no prefix, no line end, no note), whole or not at all; a record
    // that does not fit with `reserve` bytes still free after it is dropped and counted in dropped_total().
    // The packet sniffer's disk queue keeps a reserve for its own loss notes (pushed with reserve 0).
    bool push_bytes(std::string_view a, std::string_view b = {}, size_t reserve = 0) noexcept {
        const size_t n = a.size() + b.size();
        std::lock_guard<Lock> guard(lock_);
        const uint64_t head = head_.load(std::memory_order_relaxed);
        if (n + reserve > free_bytes(head)) {
            count_drop();
            return false;
        }
        put(head, a.data(), a.size());
        put(head + a.size(), b.data(), b.size());
        bump_lines();
        note_high(head + n);
        head_.store(head + n, std::memory_order_release);
        return true;
    }

    // The writer's note when lines were dropped and nobody has logged since (it fits or it waits).
    bool push_drop_note(std::string_view note_prefix) noexcept {
        std::lock_guard<Lock> guard(lock_);
        if (!pending_dropped_) return false;
        char note[160];
        const size_t n = drop_note(note, sizeof note, note_prefix);
        const uint64_t head = head_.load(std::memory_order_relaxed);
        if (n > free_bytes(head)) return false;
        put(head, note, n);
        pending_dropped_ = 0;
        pending_atomic_.store(0, std::memory_order_relaxed);
        bump_lines();
        head_.store(head + n, std::memory_order_release);
        return true;
    }

    // ------------------------------------------------------------ the consumer (never the lock)

    struct Span {
        const char* p = nullptr;
        size_t n = 0;
    };
    // The bytes [begin, end): at most two spans (the second after the wrap).
    struct Readable {
        Span a, b;
        uint64_t begin = 0, end = 0;
        size_t size() const { return a.n + b.n; }
    };

    // One consumer at a time (the writer thread; the closing thread once the writer is gone).
    bool try_begin_drain() noexcept { return !draining_.exchange(true); }  // seq_cst: against the exit flag
    void end_drain() noexcept { draining_.store(false, std::memory_order_release); }
    bool draining() const noexcept { return draining_.load(); }

    // What can be written now, at most `max_bytes` of it; with `whole_lines`, a limited pass ends after
    // its last '\n' (so no other writer of the same file, e.g. an emergency line, lands inside a line).
    // The bytes stay valid until consume(). No lock.
    Readable readable(size_t max_bytes = SIZE_MAX, bool whole_lines = false) const noexcept {
        const uint64_t tail = tail_.load(std::memory_order_acquire);
        const uint64_t head = head_.load(std::memory_order_acquire);
        uint64_t end = head;
        if (head - tail > max_bytes) {
            end = tail + max_bytes;
            if (whole_lines) {
                for (uint64_t at = end; at > tail; --at) {
                    if (data_[(at - 1) % Capacity] == '\n') {
                        end = at;
                        break;
                    }
                }
            }
        }
        return spans(tail, end);
    }
    // Frees [tail, to) once it is written. Monotonic: a consumer that is behind (two drains raced at
    // process exit) never moves the tail backwards.
    void consume(uint64_t to) noexcept {
        uint64_t cur = tail_.load(std::memory_order_relaxed);
        while (to > cur && !tail_.compare_exchange_weak(cur, to, std::memory_order_release, std::memory_order_relaxed)) {
        }
    }

    // ------------------------------------------------------------ measures (any thread, no lock)

    uint64_t pushed_bytes() const noexcept { return head_.load(std::memory_order_acquire); }
    uint64_t consumed_bytes() const noexcept { return tail_.load(std::memory_order_acquire); }
    uint64_t queued_bytes() const noexcept {
        const uint64_t t = consumed_bytes();
        const uint64_t h = pushed_bytes();
        return h > t ? h - t : 0;
    }
    uint64_t dropped_total() const noexcept { return dropped_atomic_.load(std::memory_order_relaxed); }
    uint64_t dropped_pending() const noexcept { return pending_atomic_.load(std::memory_order_relaxed); }
    bool note_due() const noexcept { return dropped_pending() != 0; }
    uint64_t lines() const noexcept { return lines_.load(std::memory_order_relaxed); }
    uint64_t high_water() const noexcept { return high_water_.load(std::memory_order_relaxed); }

private:
    // Under the lock. Acquire: the consumer finished reading the freed bytes before we overwrite them.
    uint64_t free_bytes(uint64_t head) const noexcept {
        return Capacity - (head - tail_.load(std::memory_order_acquire));
    }
    void count_drop() noexcept {
        ++dropped_total_;
        dropped_atomic_.store(dropped_total_, std::memory_order_relaxed);
    }
    void bump_lines() noexcept { lines_.store(lines_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed); }
    Readable spans(uint64_t from, uint64_t to) const noexcept {
        Readable r;
        r.begin = from;
        r.end = to;
        if (to <= from) {
            r.end = from;
            return r;
        }
        const size_t pos = static_cast<size_t>(from % Capacity);
        const size_t n = static_cast<size_t>(to - from);
        const size_t first = std::min(n, Capacity - pos);
        r.a = {data_ + pos, first};
        if (first < n) r.b = {data_, n - first};
        return r;
    }
    // Under the lock: the largest queue seen (bytes), with the new head `end`.
    void note_high(uint64_t end) noexcept {
        const uint64_t q = end - tail_.load(std::memory_order_relaxed);
        if (q > high_water_.load(std::memory_order_relaxed)) high_water_.store(q, std::memory_order_relaxed);
    }
    void put(uint64_t at, const char* src, size_t n) noexcept {
        if (!n) return;
        const size_t pos = static_cast<size_t>(at % Capacity);
        const size_t first = std::min(n, Capacity - pos);
        std::memcpy(data_ + pos, src, first);
        if (first < n) std::memcpy(data_, src + first, n - first);
    }
    static size_t append(char* out, size_t room, size_t at, std::string_view s) noexcept {
        const size_t n = std::min(s.size(), room > at ? room - at : 0);
        std::memcpy(out + at, s.data(), n);
        return at + n;
    }
    static size_t append_u64(char* out, size_t room, size_t at, uint64_t v) noexcept {
        char digits[24];
        const auto r = std::to_chars(digits, digits + sizeof digits, v);
        return append(out, room, at, std::string_view(digits, static_cast<size_t>(r.ptr - digits)));
    }
    static size_t cut_note(char* out, size_t room, size_t cut) noexcept {
        size_t n = append(out, room, 0, " [line cut: ");
        n = append_u64(out, room, n, cut);
        return append(out, room, n, " bytes]");
    }
    // Under the lock.
    size_t drop_note(char* out, size_t room, std::string_view note_prefix) const noexcept {
        size_t n = append(out, room, 0, note_prefix.substr(0, room / 2));
        n = append(out, room, n, "Log: ");
        n = append_u64(out, room, n, pending_dropped_);
        n = append(out, room, n, " line(s) dropped here (the log ring was full; ");
        n = append_u64(out, room, n, dropped_total_);
        n = append(out, room, n, " in all)");
        if (n > room - 2) n = room - 2;
        out[n++] = '\r';
        out[n++] = '\n';
        return n;
    }

    char data_[Capacity] = {};
    Lock lock_{};
    std::atomic<uint64_t> head_{0}, tail_{0};
    std::atomic<bool> draining_{false};
    std::atomic<uint64_t> dropped_atomic_{0}, pending_atomic_{0};
    // Under lock_.
    uint64_t pending_dropped_ = 0, dropped_total_ = 0;
    // Written under lock_ only; atomics so the measures can be read from any thread.
    std::atomic<uint64_t> lines_{0}, high_water_{0};
};

}  // namespace cdmp::log
