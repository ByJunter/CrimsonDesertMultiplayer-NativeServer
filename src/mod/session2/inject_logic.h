#pragma once
// Option D, P2: how the guest's Req reach the host's server without any game
// heap work on a thread of the mod (docs/research/pendientes/p8-heap-inyeccion.md).
//
// The native path of a C->S packet (build 1.0.0.2976, static analysis):
//   the client's logic thread  ClientNet::PostPacket 0x35C670 allocates the
//                              0x8030 buffer with ITS OWN mimalloc heap
//                              (TLS+0x1FD == 0 -> 0x47EF9B0) and posts it to
//                              [ClientNet+0xE0] (0x143AB10, no allocation of
//                              the buffer there)
//   the client's NetworkThread 0x35B800, at 0x35BAD4 every turn (<= 16 ms):
//                              FlushSendQueue 0x35BE90 -> NwVirtualSyncSession
//                              ::Send 0x14380B0 -> queue push 0x35DF50 into Q0
//                              (its queue blocks from ITS heap, 0x35FB00)
//   ServerRunThread            0x27733F0 pops Q0, OnRecvPacket 0x2773C20, and
//                              hands the buffer to Q3 [ServerNet+0x78]
//                              (0x27735E4); it never frees it
//   the client's NetworkThread drains Q3 [ClientNet+0x28] and frees
//                              [pb+0x8028] with 0x47F02BC (0x35BB7B-0x35BBE6)
// The mod used to allocate with 0x47EFA78 (a locked global heap no game thread
// uses) and push on its own network thread. Now the network thread only
// queues bytes here; the NetworkThread hook (server.cpp) allocates with the
// heap PostPacket would use on that thread, pushes with the same 0x35DF50 the
// native forwarder calls there, and later frees the buffer itself (Q3).
//
// This header is pure logic (no game, no Windows): the queue between the two
// threads, the classification of an injection attempt, the paced retry of the
// 2001, the stall watch and the exception notes. Tested offline
// (tests/optd_inject_tests.cpp).

#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace cdmp::session2::inj {

// ------------------------------------------------------------------ outcome of one injection attempt

enum class Outcome : uint8_t {
    Queued,       // in our queue; the NetworkThread hands it to the server in order
    NotReady,     // session 2 / the server instance is not there right now: hold it
    Full,         // our queue is at its bound (the server is not taking them): hold it
    Refused,      // not a Req this server takes (unknown opcode, too long): drop it
    Blocked,      // policy (save/cheat/dev, identity, CrossSave slot): drop it
    AfterLogout,  // user 2's logout was asked for: nothing else of it goes
    Halted,       // an exception inside the game's code in this epoch: injection stopped
};

inline const char* outcome_name(Outcome o) {
    switch (o) {
        case Outcome::Queued: return "queued";
        case Outcome::NotReady: return "not ready";
        case Outcome::Full: return "queue full";
        case Outcome::Refused: return "refused";
        case Outcome::Blocked: return "blocked";
        case Outcome::AfterLogout: return "after logout";
        case Outcome::Halted: return "halted";
    }
    return "?";
}

// What the network thread does with the packet it offered.
enum class Disposition : uint8_t {
    Done,  // taken
    Hold,  // keep it (in order) and offer it again later; never a logout
    Drop,  // counted and dropped; the conversation goes on
    Stop,  // the epoch's injection is over (exception): no retry, no logout by injection
};

constexpr Disposition disposition(Outcome o) {
    switch (o) {
        case Outcome::Queued: return Disposition::Done;
        case Outcome::NotReady:
        case Outcome::Full: return Disposition::Hold;
        case Outcome::Refused:
        case Outcome::Blocked:
        case Outcome::AfterLogout: return Disposition::Drop;
        case Outcome::Halted: return Disposition::Stop;
    }
    return Disposition::Stop;
}

// The only reasons the network thread may end a conversation (C4 logout).
// A host pause, a busy server or a full queue are never among them.
enum class EndReason : uint8_t { LinkBroken, GuestGone, ConnectionRestarted, OutgoingLost, HoldLimit };

// ------------------------------------------------------------------ the queue (network thread -> NetworkThread)

struct Pending {
    std::vector<uint8_t> packet;  // the Troc packet (header + payload), as the buffer carries it at +3
    uint8_t channel = 0;
    uint32_t tag = 0;    // pb_tag(epoch), written at +0x8010
    uint32_t epoch = 0;
    uint16_t opcode = 0;
    uint64_t queued_ms = 0;
};

struct QueueLimits {
    size_t max_packets = 65536;          // ~10 min of a guest's ~100 Req/s while the host is paused
    size_t max_bytes = 32u << 20;
    size_t control_reserve = 8;          // room kept for the logout sequence (2549 / 2001)
    uint64_t in_flight_max = 512;        // our buffers in Q0 not yet seen by the server
    size_t pump_budget = 256;            // buffers handed per NetworkThread turn
};

// FIFO; not thread safe (server.cpp holds its mutex around every call).
class InjectQueue {
public:
    explicit InjectQueue(QueueLimits l = {}) : lim_(l) {}

    // Room for one more packet of `size` bytes. A control packet (the logout sequence) may use the reserve.
    bool has_room(size_t size, bool control) const {
        const size_t extra = control ? lim_.control_reserve : 0;
        return q_.size() < lim_.max_packets + extra && bytes_ + size <= lim_.max_bytes + extra * 0x8000;
    }
    // Queued, or Full.
    Outcome push(Pending&& p, bool control) {
        if (!has_room(p.packet.size(), control)) {
            ++full_;
            return Outcome::Full;
        }
        bytes_ += p.packet.size();
        q_.push_back(std::move(p));
        ++queued_;
        if (q_.size() > peak_) peak_ = q_.size();
        return Outcome::Queued;
    }
    bool pop(Pending& out) {
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop_front();
        bytes_ -= out.packet.size();
        return true;
    }
    const Pending* front() const { return q_.empty() ? nullptr : &q_.front(); }
    // Everything not of `epoch` (a new epoch begins after a clean barrier: normally nothing).
    size_t drop_other_epochs(uint32_t epoch) {
        size_t n = 0;
        for (auto it = q_.begin(); it != q_.end();) {
            if (it->epoch != epoch) {
                bytes_ -= it->packet.size();
                it = q_.erase(it);
                ++n;
            } else {
                ++it;
            }
        }
        return n;
    }
    size_t clear() {
        const size_t n = q_.size();
        q_.clear();
        bytes_ = 0;
        return n;
    }
    size_t size() const { return q_.size(); }
    size_t bytes() const { return bytes_; }
    size_t peak() const { return peak_; }
    uint64_t queued() const { return queued_; }
    uint64_t full() const { return full_; }
    uint64_t oldest_age(uint64_t now) const { return q_.empty() || now < q_.front().queued_ms ? 0 : now - q_.front().queued_ms; }
    const QueueLimits& limits() const { return lim_; }

private:
    QueueLimits lim_;
    std::deque<Pending> q_;
    size_t bytes_ = 0, peak_ = 0;
    uint64_t queued_ = 0, full_ = 0;
};

// How many may go into Q0 now: our buffers the server has not taken yet are
// bounded (a paused server keeps the rest in our queue, not in the game's).
constexpr size_t may_hand(uint64_t pushed, uint64_t seen, size_t waiting, const QueueLimits& l) {
    const uint64_t in_flight = pushed > seen ? pushed - seen : 0;
    if (in_flight >= l.in_flight_max) return 0;
    const uint64_t room = l.in_flight_max - in_flight;
    size_t n = waiting < l.pump_budget ? waiting : l.pump_budget;
    return room < n ? static_cast<size_t>(room) : n;
}

// ------------------------------------------------------------------ paced retry (the 2001)

// At most one attempt every interval_ms, at most max_attempts; the error line
// at most once every log_ms (with the count since the last one).
class RetryPacer {
public:
    RetryPacer(uint32_t interval_ms = 250, uint32_t max_attempts = 240, uint32_t log_ms = 5000)
        : interval_(interval_ms), max_(max_attempts), log_ms_(log_ms) {}
    void reset() {
        attempts_ = 0;
        last_ = 0;
        any_ = false;
        last_log_ = 0;
        logged_any_ = false;
        since_log_ = 0;
    }
    bool exhausted() const { return attempts_ >= max_; }
    bool due(uint64_t now) const { return !exhausted() && (!any_ || now - last_ >= interval_); }
    void attempted(uint64_t now) {
        ++attempts_;
        last_ = now;
        any_ = true;
    }
    // A failed attempt: true if its line should be written now; `count` = failures since the last line.
    bool failed_should_log(uint64_t now, uint32_t& count) {
        ++since_log_;
        if (logged_any_ && now - last_log_ < log_ms_ && !exhausted()) return false;
        count = since_log_;
        since_log_ = 0;
        last_log_ = now;
        logged_any_ = true;
        return true;
    }
    uint32_t attempts() const { return attempts_; }

private:
    uint32_t interval_, max_, log_ms_;
    uint32_t attempts_ = 0, since_log_ = 0;
    uint64_t last_ = 0, last_log_ = 0;
    bool any_ = false, logged_any_ = false;
};

// ------------------------------------------------------------------ stall watch

// "Work is waiting and the progress counter has not moved for threshold_ms."
// Enter once, Still at most every repeat_ms, Exit once.
class StallWatch {
public:
    enum class Event : uint8_t { None, Enter, Still, Exit };
    explicit StallWatch(uint64_t threshold_ms = 1000, uint64_t repeat_ms = 5000)
        : threshold_(threshold_ms), repeat_(repeat_ms) {}
    Event update(uint64_t now, uint64_t progress, bool work_waiting) {
        if (!init_ || progress != last_progress_ || !work_waiting) {
            const bool was = stalled_;
            stalled_for_ = was ? now - since_ : 0;
            init_ = true;
            last_progress_ = progress;
            since_ = now;
            stalled_ = false;
            return was ? Event::Exit : Event::None;
        }
        if (!stalled_) {
            if (now - since_ < threshold_) return Event::None;
            stalled_ = true;
            last_report_ = now;
            return Event::Enter;
        }
        if (now - last_report_ >= repeat_) {
            last_report_ = now;
            return Event::Still;
        }
        return Event::None;
    }
    bool stalled() const { return stalled_; }
    // While stalled: how long the counter has not moved; after Exit: how long it had not.
    uint64_t stalled_ms(uint64_t now) const { return stalled_ ? now - since_ : stalled_for_; }

private:
    uint64_t threshold_, repeat_;
    bool init_ = false, stalled_ = false;
    uint64_t last_progress_ = 0, since_ = 0, last_report_ = 0, stalled_for_ = 0;
};

// ------------------------------------------------------------------ exceptions inside the game's code

// The steps of the injection and of session 2's build. P8 review I1
// (p8-revision-noche.md): only the mod's own steps are handled (the note, then
// the injection stops: EXCEPTION_EXECUTE_HANDLER); an exception inside game
// code that may hold a lock or leave a half-built state is noted and passed on
// (EXCEPTION_CONTINUE_SEARCH: a real crash where it happened).
//   Alloc         0x47EF9B0, the thread's own heap: no lock of its own [V] (p8-heap-inyeccion.md §3)
//   AllocGlobal   0x47EFA78, the global heap: under its CRITICAL_SECTION
//   Fill          our writes to the buffer we just got (and reads of ServerNet)
//   Push          0x35DF50: Q0's lock (vf8) and its signal
//   BuildSession  our writes to session 2's block before/after the constructor
//   BuildCtor     0x1436EE0, the game's NwSession constructor: it allocates (either heap) and raises
//                 0xA0000002 itself when an allocation fails
enum class Step : uint8_t { Alloc, Fill, Push, BuildSession, AllocGlobal, BuildCtor };
inline const char* step_name(Step s) {
    switch (s) {
        case Step::Alloc: return "allocating from the thread's own heap (0x47EF9B0)";
        case Step::Fill: return "filling the packet buffer";
        case Step::Push: return "pushing to the server's queue Q0 (0x35DF50)";
        case Step::BuildSession: return "building session 2 (our writes)";
        case Step::AllocGlobal: return "allocating from the game's global heap (0x47EFA78, under its lock)";
        case Step::BuildCtor: return "building session 2 (the game's constructor 0x1436EE0)";
    }
    return "?";
}
// True: the step is the mod's own (handled, the injection stops). False: the
// game's code with a possible lock or half-built state (noted, passed on).
inline bool step_is_ours(Step s) {
    switch (s) {
        case Step::Alloc:
        case Step::Fill:
        case Step::BuildSession: return true;
        case Step::Push:
        case Step::AllocGlobal:
        case Step::BuildCtor: return false;
    }
    return false;
}

struct ExceptionNote {
    uint32_t code = 0;
    uint64_t address = 0;
    uint64_t rva = 0;        // address - exe base, if inside the exe (else 0)
    bool in_exe = false;
    uint32_t nparams = 0;
    uint64_t params[2] = {};  // for an access violation: read/write flag and the address
    uint32_t thread = 0;
    Step step = Step::Alloc;
    uint16_t opcode = 0;
    uint32_t epoch = 0;
    uint64_t when_ms = 0;
};

inline std::string describe(const ExceptionNote& e) {
    std::string where = e.in_exe ? std::format("exe+0x{:X}", e.rva) : std::format("0x{:X}", e.address);
    std::string s = std::format("code 0x{:08X} at {} while {} (thread {}, epoch {}, Req {}){}", e.code, where,
                                step_name(e.step), e.thread, e.epoch, e.opcode,
                                step_is_ours(e.step) ? "" : " | inside the game's code: passed on (a real crash)");
    if (e.code == 0xC0000005u && e.nparams >= 2) {
        s += std::format(" | {} of 0x{:X}", e.params[0] == 0 ? "read" : e.params[0] == 1 ? "write" : "execute",
                         e.params[1]);
    }
    return s;
}

}  // namespace cdmp::session2::inj
