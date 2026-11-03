#pragma once
// P8 title fix (docs/research/pendientes/p8-titulo.md §3): the pure part of
// the guest's local refresh of the save manager's slot table. When the
// guest's 2490 goes to the host, its own server never runs UserLogin, so
// nothing rebuilds the table the title reads and Play/Load stay grey. The
// guest then runs the native refresh (0x1870C20, guarded like
// SaveDataSlotRefreshReq 0x2A22380) once per remote connection, on its
// NetworkThread (guest.cpp). This file holds only what can be tested without
// the game: when to try, how often, when to give up, and how to describe the
// table.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cdmp::session2::slot_refresh {

// What one try found.
enum class Attempt : uint8_t {
    NotReady,  // the save manager, its storage or its lock is not there yet: try again
    Busy,      // byte +0x60 is set (a login/refresh of the game holds the table): try again
    Done,      // refreshed
    Fault,     // an exception inside the game's code: never again in this run
    Refused,   // the code or the objects are not the expected ones: never again in this run
};

// What the caller does after reporting a try.
enum class Outcome : uint8_t {
    Retry,     // still pending: next try at next_try_ms()
    Finished,  // done for this connection
    GaveUp,    // too many NotReady/Busy tries: done for this connection, not refreshed
    Stopped,   // Fault/Refused: disabled for the rest of the run
};

struct Limits {
    uint32_t interval_ms = 100;   // between tries (the pump turns every <= 16 ms)
    uint32_t max_attempts = 50;   // ~5 s of NotReady/Busy
};

// Not thread-safe: the caller serialises (guest.cpp: one mutex).
class Planner {
public:
    // A refresh is wanted for connection `key` (the epoch, or the diagnostic's
    // own key). Once per key: false if that key was already armed (pending or
    // finished) or if the planner is disabled. A refresh still pending for
    // another key is replaced: the new connection wins.
    bool arm(uint64_t key, uint64_t now_ms, Limits limits = {});
    // Whether a try should run now.
    bool due(uint64_t now_ms) const { return pending_ && !disabled_ && now_ms >= next_ms_; }
    Outcome report(Attempt a, uint64_t now_ms);
    // The connection it was for is over before a try ran: drop it (the key
    // stays used).
    void cancel() { pending_ = false; }

    bool pending() const { return pending_; }
    bool disabled() const { return disabled_; }
    uint32_t attempts() const { return attempts_; }
    uint64_t key() const { return key_; }
    uint64_t next_try_ms() const { return next_ms_; }
    uint32_t done_count() const { return done_; }

private:
    bool pending_ = false, disabled_ = false, has_key_ = false;
    uint64_t key_ = 0, next_ms_ = 0;
    uint32_t attempts_ = 0, done_ = 0;
    Limits limits_{};
};

// One entry of the table as the title reads it (0x9096890): u32 slot @0, i64 time @8.
struct Entry {
    uint32_t slot = 0;
    int64_t time = 0;
};
inline constexpr uint32_t kEntrySize = 0x68;
inline constexpr uint32_t kMaxEntriesRead = 64;

// The entries of a raw copy of `count` entries (fewer if the copy is short).
std::vector<Entry> parse_entries(std::span<const uint8_t> raw, uint32_t count);
// Whether the title enables Play/Load with this table (slot 0-2 or 100-108, 0x65EAD0).
bool title_would_enable(const std::vector<Entry>& entries);
// "[0 @1234, 100 @5678]".
std::string describe(const std::vector<Entry>& entries);
// Index of the first byte that differs, or -1 if equal (sizes must match, else 0).
int first_mismatch(std::span<const uint8_t> actual, std::span<const uint8_t> expected);

}  // namespace cdmp::session2::slot_refresh
