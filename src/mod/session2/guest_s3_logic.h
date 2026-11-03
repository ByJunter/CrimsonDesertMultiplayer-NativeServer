#pragma once
// S3, guest side (docs/research/pendientes/p8-impl-s3.md): the pure logic of
// the guest's first-action probes, crash capture and update guard, with no
// game access, so the tests can run it. The game side is guest_s3.cpp.
//
//   Ring       lock-free (wait-free writers), fixed-size records, bounded:
//              the oldest are overwritten and counted, never blocked on.
//   GuardTable lock-free per-key state of the update guard (first skip,
//              skips, when the actor later got an action).
//   KeySet     lock-free "first time this key" set.
//   parse_2005 the records of an ActorLoadDataAck 2005 payload and the
//              restore byte of each (p8-raiz-invitado.md §2, §5.2).

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace cdmp::session2::s3 {

// ---------------------------------------------------------------- events

enum class Ev : uint8_t {
    None = 0,
    Rx2005,          // a record of a 2005 queued for the client (guest_deliver)
    Rx2723,          // a StartSpawnActionAck 2723 queued for the client
    Rx2991,          // a StartActionAck 2991 queued for the client
    Restore,         // RestoreActionsFromLoadData's result (from guest_actor_diag's exit)
    FirstUpdate,     // first CharacterControl::Update 0x85FBA0 of a remote actor
    Spawn2723,       // 0x48B120 entered (2723's consumer)
    Spawn2723Vf188,  // 0x48B197: AL of vf188(5) (0 = success WITHOUT applying)
    Ack2991,         // 0x48AD40 entered (2991's consumer)
    StartAction,     // 0x863530 returned: result, caller, layers after
    NullAction,      // 0x232D5C0 entered with snapshot+0x10 == 0 (detail)
    GuardFirst,      // the update guard skipped this actor for the first time
    GuardRecovered,  // a guarded actor later reached 0x232D5C0 with an action
    Crash,           // first-chance access violation in .rsrc (the crash capture's vectored handler)
    Count
};
constexpr const char* ev_name(Ev e) {
    switch (e) {
        case Ev::None: return "-";
        case Ev::Rx2005: return "rx 2005";
        case Ev::Rx2723: return "rx 2723";
        case Ev::Rx2991: return "rx 2991";
        case Ev::Restore: return "restore";
        case Ev::FirstUpdate: return "first Update";
        case Ev::Spawn2723: return "2723 consumer";
        case Ev::Spawn2723Vf188: return "2723 vf188(5)";
        case Ev::Ack2991: return "2991 consumer";
        case Ev::StartAction: return "StartAction";
        case Ev::NullAction: return "NULL ACTION at 0x232D5C0";
        case Ev::GuardFirst: return "guard first skip";
        case Ev::GuardRecovered: return "guard recovered";
        case Ev::Crash: return "ACCESS VIOLATION (first chance)";
        case Ev::Count: break;
    }
    return "?";
}

// One record. Fixed size; the meaning of a..d / v[] depends on ev (see guest_s3.cpp).
struct Record {
    uint64_t qpc = 0;
    uint32_t tid = 0;
    uint32_t epoch = 0;     // low 32 bits of the guest's connection epoch
    uint32_t key = 0;       // actor key
    uint32_t control = 0;   // actor +0x90 (or the 2005 record's +0x12)
    Ev ev = Ev::None;
    uint8_t flags = 0;
    uint16_t u16 = 0;
    uint32_t a = 0, b = 0, c = 0, d = 0;
    uint64_t v[10] = {};
};
static_assert(sizeof(Record) == 128);

// Field-by-field copy, as p8p::copy_event: no memcpy (the CRT's may use AVX
// and clobber the upper halves of the game's YMM registers inside a mid hook).
inline void copy_record(Record& d, const Record& s) {
    d.qpc = s.qpc;
    d.tid = s.tid;
    d.epoch = s.epoch;
    d.key = s.key;
    d.control = s.control;
    d.ev = s.ev;
    d.flags = s.flags;
    d.u16 = s.u16;
    d.a = s.a;
    d.b = s.b;
    d.c = s.c;
    d.d = s.d;
    for (size_t i = 0; i < 10; ++i) d.v[i] = s.v[i];
}

// Writers: wait-free (one fetch_add + plain stores + one release store).
// Readers: a slot is valid when its sequence is the index they expect, read
// before and after the copy (a slot being rewritten is skipped as torn).
template <size_t N>
class Ring {
    static_assert(N && (N & (N - 1)) == 0, "N must be a power of two");

public:
    uint64_t push(const Record& r) {
        const uint64_t i = head_.fetch_add(1, std::memory_order_relaxed);
        Slot& s = slots_[i & (N - 1)];
        s.seq.store(0, std::memory_order_relaxed);  // busy
        std::atomic_thread_fence(std::memory_order_release);
        copy_record(s.r, r);
        s.seq.store(i + 1, std::memory_order_release);
        return i;
    }
    // Records written so far (the next index).
    uint64_t head() const { return head_.load(std::memory_order_acquire); }
    // The record of index i if it is still there and whole.
    bool read(uint64_t i, Record& out) const {
        const Slot& s = slots_[i & (N - 1)];
        if (s.seq.load(std::memory_order_acquire) != i + 1) return false;
        copy_record(out, s.r);
        std::atomic_thread_fence(std::memory_order_acquire);
        return s.seq.load(std::memory_order_relaxed) == i + 1;
    }
    // The oldest index still readable (head - N, or 0).
    uint64_t oldest() const {
        const uint64_t h = head();
        return h > N ? h - N : 0;
    }
    static constexpr size_t capacity() { return N; }
    const void* data() const { return slots_.data(); }
    static constexpr size_t bytes() { return sizeof(Slot) * N; }

private:
    struct Slot {
        std::atomic<uint64_t> seq{0};
        Record r;
    };
    std::array<Slot, N> slots_{};
    std::atomic<uint64_t> head_{0};
};

// ---------------------------------------------------------------- key sets

// Bounds of the lock-free key tables used on the game's hot paths (on_update
// runs once per remote actor per frame): a lookup or insert looks at no more
// than kMaxProbe slots, and at most N/2 keys are taken. Reaching that load
// closes the table for good (a sticky flag, checked first: no insertion is
// tried any more, only the bounded lookup); a probe run over kMaxProbe
// answers "full" for that key alone.
inline constexpr size_t kMaxProbe = 32;

// A lock-free set of actor keys (0 is never a key).
template <size_t N>
class KeySet {
    static_assert(N >= 4 && (N & (N - 1)) == 0, "N must be a power of two");

public:
    static constexpr size_t kProbe = N < kMaxProbe ? N : kMaxProbe;
    static constexpr size_t kMaxKeys = N / 2;
    enum class Insert : int8_t { Full = -1, Present = 0, Added = 1 };
    Insert insert(uint32_t key) {
        if (!key) return Insert::Present;
        const bool closed = closed_.load(std::memory_order_relaxed);
        size_t i = hash(key);
        for (size_t probe = 0; probe < kProbe; ++probe, i = (i + 1) & (N - 1)) {
            uint32_t cur = slots_[i].load(std::memory_order_relaxed);
            if (cur == key) return Insert::Present;
            if (cur != 0) continue;
            if (closed) break;
            if (size_.load(std::memory_order_relaxed) >= kMaxKeys) {
                closed_.store(true, std::memory_order_relaxed);
                break;
            }
            if (slots_[i].compare_exchange_strong(cur, key, std::memory_order_relaxed)) {
                size_.fetch_add(1, std::memory_order_relaxed);
                return Insert::Added;
            }
            if (cur == key) return Insert::Present;  // another thread added it; else go on probing
        }
        full_.fetch_add(1, std::memory_order_relaxed);
        return Insert::Full;
    }
    bool contains(uint32_t key) const {
        if (!key) return false;
        size_t i = hash(key);
        for (size_t probe = 0; probe < kProbe; ++probe, i = (i + 1) & (N - 1)) {
            const uint32_t cur = slots_[i].load(std::memory_order_relaxed);
            if (cur == key) return true;
            if (cur == 0) return false;
        }
        return false;
    }
    size_t size() const { return size_.load(std::memory_order_relaxed); }
    bool closed() const { return closed_.load(std::memory_order_relaxed); }
    uint64_t full() const { return full_.load(std::memory_order_relaxed); }

private:
    static size_t hash(uint32_t key) { return (static_cast<size_t>(key) * 0x9E3779B1u) & (N - 1); }
    std::atomic<uint32_t> slots_[N] = {};
    std::atomic<size_t> size_{0};
    std::atomic<bool> closed_{false};
    std::atomic<uint64_t> full_{0};
};

// ---------------------------------------------------------------- the update guard's per-key state

// One slot per guarded key: first and last skip, number of skips, and when
// (QPC) the actor was first seen again WITH a current action (0 = never yet:
// still stuck, or gone). Lock-free; bounded as KeySet (kMaxProbe, N/2 keys,
// sticky "closed"); once closed, new keys are only counted.
template <size_t N>
class GuardTable {
    static_assert(N >= 4 && (N & (N - 1)) == 0, "N must be a power of two");

public:
    static constexpr size_t kProbe = N < kMaxProbe ? N : kMaxProbe;
    static constexpr size_t kMaxKeys = N / 2;
    struct Slot {
        std::atomic<uint32_t> key{0};
        std::atomic<uint32_t> control{0};
        std::atomic<uint64_t> first_qpc{0};
        std::atomic<uint64_t> last_qpc{0};
        std::atomic<uint32_t> skips{0};
        std::atomic<uint64_t> recovered_qpc{0};
        std::atomic<uint8_t> logged{0};  // reported by the tick (bit 0 first, bit 1 recovered, bit 2 stuck)
        std::atomic<uint32_t> gen{0};    // P8 re-entry: the table generation this key's state belongs to
    };
    // P8 re-entry (s25/reentrada.md §1.4): a new remote conversation. A key seen again afterwards is a new actor (the
    // keys are reused between servers): its state starts again at its next touch, and find() no longer returns the
    // old one. Lock-free; the slots and the per-run key bound stay. Any thread.
    void new_generation() { gen_.fetch_add(1, std::memory_order_relaxed); }
    uint32_t generation() const { return gen_.load(std::memory_order_relaxed); }
    // The key's slot (inserted if new; first = it was new), or nullptr when full.
    Slot* touch(uint32_t key, uint32_t control, uint64_t qpc, bool& first) {
        first = false;
        if (!key) return nullptr;
        const bool closed = closed_.load(std::memory_order_relaxed);
        const uint32_t g = gen_.load(std::memory_order_relaxed);
        size_t i = hash(key);
        for (size_t probe = 0; probe < kProbe; ++probe, i = (i + 1) & (N - 1)) {
            Slot& s = slots_[i];
            uint32_t cur = s.key.load(std::memory_order_acquire);
            if (cur == 0) {
                if (closed) break;
                if (size_.load(std::memory_order_relaxed) >= kMaxKeys) {
                    closed_.store(true, std::memory_order_relaxed);
                    break;
                }
                if (s.key.compare_exchange_strong(cur, key, std::memory_order_acq_rel)) {
                    s.gen.store(g, std::memory_order_relaxed);
                    s.control.store(control, std::memory_order_relaxed);
                    s.first_qpc.store(qpc, std::memory_order_relaxed);
                    first = true;
                    size_.fetch_add(1, std::memory_order_relaxed);
                    open_.fetch_add(1, std::memory_order_relaxed);
                    cur = key;
                }
            }
            if (cur == key) {
                // An older generation: this key's actor is a new one; one thread re-initializes its state.
                if (uint32_t og = s.gen.load(std::memory_order_relaxed);
                    og != g && s.gen.compare_exchange_strong(og, g, std::memory_order_acq_rel)) {
                    if (s.recovered_qpc.exchange(0, std::memory_order_acq_rel) != 0) {
                        open_.fetch_add(1, std::memory_order_relaxed);
                    }
                    s.control.store(control, std::memory_order_relaxed);
                    s.first_qpc.store(qpc, std::memory_order_relaxed);
                    s.skips.store(0, std::memory_order_relaxed);
                    s.logged.store(0, std::memory_order_relaxed);
                    first = true;
                }
                s.last_qpc.store(qpc, std::memory_order_relaxed);
                s.skips.fetch_add(1, std::memory_order_relaxed);
                return &s;
            }
        }
        full_.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    Slot* find(uint32_t key) {
        if (!key) return nullptr;
        size_t i = hash(key);
        for (size_t probe = 0; probe < kProbe; ++probe, i = (i + 1) & (N - 1)) {
            const uint32_t cur = slots_[i].key.load(std::memory_order_acquire);
            if (cur == key) {
                return slots_[i].gen.load(std::memory_order_relaxed) == gen_.load(std::memory_order_relaxed)
                           ? &slots_[i]
                           : nullptr;
            }
            if (cur == 0) return nullptr;
        }
        return nullptr;
    }
    // The actor has an action again: true only the first time.
    bool recover(Slot& s, uint64_t qpc) {
        uint64_t zero = 0;
        if (!s.recovered_qpc.compare_exchange_strong(zero, qpc ? qpc : 1, std::memory_order_acq_rel)) return false;
        open_.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }
    size_t size() const { return size_.load(std::memory_order_relaxed); }
    // Guarded and not recovered yet (the hot path only looks for recoveries while > 0).
    size_t open() const { return open_.load(std::memory_order_relaxed); }
    uint64_t full() const { return full_.load(std::memory_order_relaxed); }
    bool closed() const { return closed_.load(std::memory_order_relaxed); }
    static constexpr size_t capacity() { return N; }
    Slot& at(size_t i) { return slots_[i]; }

private:
    static size_t hash(uint32_t key) { return (static_cast<size_t>(key) * 0x9E3779B1u) & (N - 1); }
    std::array<Slot, N> slots_{};
    std::atomic<size_t> size_{0}, open_{0};
    std::atomic<bool> closed_{false};
    std::atomic<uint64_t> full_{0};
    std::atomic<uint32_t> gen_{0};
};

// ---------------------------------------------------------------- the crash capture's single entry

// The capture (log lines, history flush, minidump) is entered by one thread
// at a time (concurrent crashes: the others go on to the game's own path at
// once), at most N times in all, and never twice for the same faulting
// address (the vectored handler, then the top-level filter).
template <size_t N>
class CrashGate {
public:
    enum class Enter : uint8_t { Go, Busy, Repeat, Spent };
    Enter enter(uintptr_t at) {
        bool expected = false;
        if (!busy_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return Enter::Busy;
        // Under busy_ from here.
        for (size_t i = 0; i < used_; ++i) {
            if (at_[i] == at) {
                busy_.store(false, std::memory_order_release);
                return Enter::Repeat;
            }
        }
        if (used_ >= N) {
            busy_.store(false, std::memory_order_release);
            return Enter::Spent;
        }
        at_[used_++] = at;
        return Enter::Go;
    }
    // After Enter::Go only.
    void leave() { busy_.store(false, std::memory_order_release); }
    size_t used() const { return used_; }
    // Another thread's capture is running (the end of the process waits for it).
    bool busy() const { return busy_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> busy_{false};
    uintptr_t at_[N] = {};
    size_t used_ = 0;
};

// ---------------------------------------------------------------- after the capture: chain or end

// What our top-level filter does after its capture (pendientes/p8-salida-invitado.md §3).
// The filter behind ours on the guest is the CRT's __scrt_unhandled_exception_filter
// (exe+0x4896E00, installed by 0x4896DF0 -> SetUnhandledExceptionFilter): for anything
// but a C++ exception it returns EXCEPTION_CONTINUE_SEARCH, and Windows' own handling
// (WER) then kept the process alive 5 min 40 s after the dump (session 6, Alt+F4 crash).
// Without crashpad_handler.exe (K2) nothing of the game reports it either. So, with
// guest_crash_dump=1 and no crashpad_handler.exe, an unhandled ACCESS VIOLATION ends the
// process right after our capture, with the exception code as the exit code. Any other
// code, or with crashpad_handler.exe present, chains as before.
enum class UefEnd : uint8_t { Chain, Terminate };
constexpr uint32_t kAccessViolation = 0xC0000005u;
constexpr UefEnd uef_end(uint32_t code, bool crash_dump_on, bool crashpad_present) {
    return crash_dump_on && !crashpad_present && code == kAccessViolation ? UefEnd::Terminate : UefEnd::Chain;
}

// ---------------------------------------------------------------- who controls an actor

// Not controlled here: its +0x90 is not the local user's key. With the local
// key unknown (0) nothing is remote: no guard, no probe.
constexpr bool remote_controlled(uint32_t control, uint32_t local_user) {
    return local_user != 0 && control != local_user;
}

// ---------------------------------------------------------------- the 0x232D5C0 guard

// The guard's decision at 0x232D5C0's entry (rcx = CC, r8 = snapshot of the
// layer's +0/+8/+0x10): return at once only when the snapshot's current action
// is null AND the actor is not controlled locally. 0x232D5C0 is void and does
// nothing when the action's u16 at +0xE4 is 0 (decompiled; its whole body is
// `if (count != 0) {...} return;`): returning at once is that same path.
constexpr bool guard_skips(bool guard_on, bool current_null, uint32_t control, uint32_t local_user) {
    return guard_on && current_null && remote_controlled(control, local_user);
}

// ---------------------------------------------------------------- 2005

// One record of an ActorLoadDataAck 2005 payload: [u16 len][record]... Offsets
// after the u16 (p8-raiz-invitado.md §2): type +0, key +1, owner +5,
// controller +0x12. Type 0 = the 9-byte user record NotifyAppear puts before
// a user's actors.
struct Rec2005 {
    uint8_t type = 0;
    uint32_t key = 0, owner = 0, controller = 0;
    int restore = -1;         // 0/1, -1 = not found (no CC block, e.g. gimmicks)
    uint32_t restore_at = 0;  // its offset in the record (0 = not found)
    uint32_t offset = 0;      // the record's offset in the payload (after its u16)
    uint16_t size = 0;
};

// The restore byte: the CC block ends its appearance path as u32 length +
// bytes (".app_xml"), then a u64 action session, then the u8 restore byte
// (serialiser 0x231FC90, reader 0x2320200; Crow B01002A0: path at +0x11D3,
// length 96, byte at +0x123B). -1 when no such path is found.
inline int restore_byte_of(std::span<const uint8_t> rec, uint32_t* at = nullptr) {
    static constexpr char kExt[] = ".app_xml";
    constexpr size_t kExtLen = sizeof kExt - 1;
    if (rec.size() < kExtLen + 13) return -1;
    for (size_t i = 0; i + kExtLen <= rec.size(); ++i) {
        if (rec[i] != '.' || std::memcmp(rec.data() + i, kExt, kExtLen) != 0) continue;
        const size_t end = i + kExtLen;
        // The u32 length right before the path's first byte: path_len = end - start.
        const size_t lo = i > 512 ? i - 512 : 4;
        for (size_t start = lo < 4 ? 4 : lo; start <= i; ++start) {
            uint32_t len = 0;
            std::memcpy(&len, rec.data() + start - 4, 4);
            if (len == end - start && end + 8 < rec.size()) {
                if (at) *at = static_cast<uint32_t>(end + 8);
                const uint8_t b = rec[end + 8];
                return b <= 1 ? b : -1;
            }
        }
        return -1;  // the first ".app_xml" decides
    }
    return -1;
}

// Up to out.size() records of a 2005 payload; returns how many were parsed
// (stops at the first record that does not fit).
inline size_t parse_2005(std::span<const uint8_t> payload, std::span<Rec2005> out) {
    size_t n = 0, p = 0;
    while (n < out.size() && p + 2 <= payload.size()) {
        uint16_t len = 0;
        std::memcpy(&len, payload.data() + p, 2);
        if (len == 0 || p + 2 + len > payload.size()) break;
        const std::span<const uint8_t> rec = payload.subspan(p + 2, len);
        Rec2005 r;
        r.offset = static_cast<uint32_t>(p + 2);
        r.size = len;
        r.type = rec[0];
        if (len >= 9) {
            std::memcpy(&r.key, rec.data() + 1, 4);
            std::memcpy(&r.owner, rec.data() + 5, 4);
        }
        if (r.type != 0 && len >= 0x16) {
            std::memcpy(&r.controller, rec.data() + 0x12, 4);
            r.restore = restore_byte_of(rec, &r.restore_at);
        }
        out[n++] = r;
        p += 2 + len;
    }
    return n;
}

// ---------------------------------------------------------------- 0x863530's callers

// Where StartAction 0x863530 was called from (its return address, RVA).
enum class StartCaller : uint8_t { Other = 0, Spawn2723 = 1, Ack2991 = 2 };
constexpr StartCaller start_caller(uint64_t ret_rva) {
    // .pdata bounds of the two consumers (build 2976).
    if (ret_rva >= 0x48B120 && ret_rva < 0x48B3F6) return StartCaller::Spawn2723;
    if (ret_rva >= 0x48AD40 && ret_rva < 0x48B11E) return StartCaller::Ack2991;
    return StartCaller::Other;
}

}  // namespace cdmp::session2::s3
