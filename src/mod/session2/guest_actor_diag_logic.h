#pragma once
// P8, guest side (docs/research/pendientes/p8-actores-anfitrion.md §3, §4.1(i),
// §4.4): the pure logic of the guest's actor diagnostic and layer guard, with
// no game access, so the tests can run it. The game side is
// guest_actor_diag.cpp.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace cdmp::session2::gad {

// ---------------------------------------------------------------- who controls an actor

// An actor is controlled elsewhere when its controlling user key (actor +0x90)
// is not the local user's. With the local key still unknown (0) nothing is
// "remote": the guard and the probe leave every actor alone.
constexpr bool remote_controlled(uint32_t control, uint32_t local_user) {
    return local_user != 0 && control != local_user;
}

// ---------------------------------------------------------------- guest_remote_update_guard (S34)

// The update guard's actors: controlled by ANOTHER user (+0x90 non-zero and not
// the local user's key). Narrower than remote_controlled: +0x90 = 0 (nobody)
// is left alone, and so is everything while the local key is unknown.
constexpr bool remote_user_controlled(uint32_t control, uint32_t local_user) {
    return local_user != 0 && control != 0 && control != local_user;
}

// One action layer (CC +0x88/+0x90): its chart (+0x110), current action
// (+0x10) and playback (+0x18). horse_diag's "layer without action": a chart
// and nothing playing, the state the 5 known readers crash on.
constexpr bool layer_lacks_action(uintptr_t chart, uintptr_t current, uintptr_t playback) {
    return chart != 0 && (current == 0 || playback == 0);
}

// CharacterControl::Update 0x85FBA0 / PostUpdate 0x860D30 are skipped for this
// actor (Shadow S34: 0x88F832, MovementController_UpdateByMode, reached only
// from 0x860D30, with a host NPC that had a chart and no action).
constexpr bool contain_remote_update(uint32_t control, uint32_t local_user, bool layer0_lacks, bool layer1_lacks) {
    return remote_user_controlled(control, local_user) && (layer0_lacks || layer1_lacks);
}

// The first N (key, value) pairs in arrival order, lock-free: a hook adds (any
// thread, only for a key seen for the first time), one reporter drains them in
// order. Keys are never 0 (a slot at 0 is still being written).
template <size_t N>
class FirstPairs {
public:
    // False once full (the pair is not kept).
    bool add(uint32_t key, uint32_t value) {
        if (!key || next_.load(std::memory_order_relaxed) >= N) return false;
        const uint32_t i = next_.fetch_add(1, std::memory_order_relaxed);
        if (i >= N) return false;
        slots_[i].store(static_cast<uint64_t>(value) << 32 | key, std::memory_order_release);
        return true;
    }
    // The pairs added since the last drain, in order; stops at one still being written.
    template <class F>
    void drain(F&& f) {
        while (drained_ < N) {
            const uint64_t v = slots_[drained_].load(std::memory_order_acquire);
            if (!v) return;
            f(static_cast<uint32_t>(v), static_cast<uint32_t>(v >> 32), drained_);
            ++drained_;
        }
    }
    size_t drained() const { return drained_; }

private:
    std::atomic<uint64_t> slots_[N] = {};
    std::atomic<uint32_t> next_{0};
    size_t drained_ = 0;  // the reporter only
};

// ---------------------------------------------------------------- where a hook may go

// One section of the game's image, as its PE header says (in memory).
struct Section {
    char name[9] = {};
    uint32_t va = 0, size = 0;
};

inline const Section* section_of(std::span<const Section> secs, uint32_t rva) {
    for (const Section& s : secs) {
        if (rva >= s.va && rva - s.va < s.size) return &s;
    }
    return nullptr;
}

// The only code the mod may patch: [rva, rva + len) entirely inside .rsrc (the
// game's real code in this build), never .xtls (Denuvo) or anything else.
inline bool in_real_code(std::span<const Section> secs, uint32_t rva, uint32_t len) {
    const Section* s = section_of(secs, rva);
    if (!s || std::string_view(s->name) != ".rsrc" || len == 0) return false;
    return static_cast<uint64_t>(rva) + len <= static_cast<uint64_t>(s->va) + s->size;
}

// ---------------------------------------------------------------- log budgets

// The first `cap` lines are written; after that only counted.
class LineBudget {
public:
    explicit constexpr LineBudget(uint64_t cap) : cap_(cap) {}
    bool take() { return n_.fetch_add(1, std::memory_order_relaxed) < cap_; }
    uint64_t asked() const { return n_.load(std::memory_order_relaxed); }
    uint64_t suppressed() const {
        const uint64_t n = asked();
        return n > cap_ ? n - cap_ : 0;
    }
    uint64_t cap() const { return cap_; }

private:
    uint64_t cap_;
    std::atomic<uint64_t> n_{0};
};

// A lock-free set of actor keys (0 is never a key): "is this the first time
// this key is seen?" from hot game threads (the layer guard runs per actor per
// frame). Open addressing with linear probing; once full, inserts fail.
template <size_t N>
class KeySet {
    static_assert(N && (N & (N - 1)) == 0, "N must be a power of two");

public:
    enum class Insert : int8_t { Full = -1, Present = 0, Added = 1 };
    Insert insert(uint32_t key) {
        if (!key) return Insert::Present;
        size_t i = hash(key);
        for (size_t probe = 0; probe < N; ++probe, i = (i + 1) & (N - 1)) {
            uint32_t cur = slots_[i].load(std::memory_order_relaxed);
            if (cur == key) return Insert::Present;
            if (cur == 0) {
                if (slots_[i].compare_exchange_strong(cur, key, std::memory_order_relaxed)) {
                    size_.fetch_add(1, std::memory_order_relaxed);
                    return Insert::Added;
                }
                if (cur == key) return Insert::Present;  // another thread put it there
            }
        }
        return Insert::Full;
    }
    bool contains(uint32_t key) const {
        if (!key) return false;
        size_t i = hash(key);
        for (size_t probe = 0; probe < N; ++probe, i = (i + 1) & (N - 1)) {
            const uint32_t cur = slots_[i].load(std::memory_order_relaxed);
            if (cur == key) return true;
            if (cur == 0) return false;
        }
        return false;
    }
    size_t size() const { return size_.load(std::memory_order_relaxed); }

private:
    static size_t hash(uint32_t key) { return (static_cast<size_t>(key) * 0x9E3779B1u) & (N - 1); }
    std::atomic<uint32_t> slots_[N] = {};
    std::atomic<size_t> size_{0};
};

// ---------------------------------------------------------------- the loading state 0x12

// One key of the wait list [[[P+0xB8]+8]+0x78] (u32 keys, count +0x80) as
// ClientActorContainer::Find and vf1D8 (0x4FE240) see it.
struct WaitEntry {
    uint32_t key = 0;
    bool found = false;  // Find gave an actor
    uint16_t state = 0;  // actor +0x5E
    uint8_t c2 = 0, cf = 0;
};

// vf1D8 = 0x4FE240: +0xC2 != 0 && +0xCF != 0.
constexpr bool entry_ready(const WaitEntry& e) { return e.found && e.c2 != 0 && e.cf != 0; }

// State 0x12 of 0x7797F0 (decompiled): it looks at the FIRST key only; a key
// that is missing or ready is removed by moving the LAST key into slot 0, and
// it stops at the first key that exists and is not ready. The index (in the
// list as read) of the key it would wait for now, or -1 if the list would
// empty this frame (then the state moves on to 0x13).
inline int wait_blocker(std::span<const WaitEntry> list) {
    std::vector<int> order(list.size());
    for (size_t i = 0; i < list.size(); ++i) order[i] = static_cast<int>(i);
    size_t n = order.size();
    while (n) {
        const WaitEntry& front = list[static_cast<size_t>(order[0])];
        if (front.found && !entry_ready(front)) return order[0];
        if (n - 1 != 0) order[0] = order[n - 1];
        --n;
    }
    return -1;
}

// The whole list, as the diagnostic reads it (p8-raiz-invitado.md §4.3: the
// first version looked at 64 keys of 173). Each slot read: key_ok = false when
// the key itself could not be read (the slot is "unreadable"); e is then unused.
struct WaitRead {
    bool key_ok = false;
    WaitEntry e;
};

enum class WaitVerdict : uint8_t {
    Empties,       // every key is missing or ready: the list empties this frame
    Blocks,        // the state waits for `key` (index in the list as read)
    Undetermined,  // the replay reached a slot we could not read or did not inspect
};

struct WaitScan {
    uint32_t total = 0;       // the list's count [C+0x80]
    uint32_t inspected = 0;   // slots read (at most the cap)
    uint32_t unreadable = 0;  // slots whose key could not be read
    uint32_t not_found = 0;   // keys Find does not resolve (the state drops them)
    uint32_t ready = 0;       // +0xC2 && +0xCF
    uint32_t not_ready = 0;   // found and not ready: each could block
    bool garbage_count = false;  // total above the sane maximum: no replay
    WaitVerdict verdict = WaitVerdict::Empties;
    int32_t index = -1;  // Blocks: the blocker; Undetermined: the slot the replay could not see
    uint32_t key = 0;    // Blocks: the blocker's key
};

// Counts every slot read and replays state 0x12's swap-remove (wait_blocker)
// over the whole list of `total` keys: a slot beyond `list` (not inspected) or
// unreadable stops the replay as Undetermined instead of being guessed.
inline WaitScan scan_wait_list(std::span<const WaitRead> list, uint32_t total, uint32_t max_total = 1u << 16) {
    WaitScan s;
    s.total = total;
    s.inspected = static_cast<uint32_t>(list.size());
    for (const WaitRead& r : list) {
        if (!r.key_ok) {
            ++s.unreadable;
        } else if (!r.e.found) {
            ++s.not_found;
        } else if (entry_ready(r.e)) {
            ++s.ready;
        } else {
            ++s.not_ready;
        }
    }
    if (total > max_total) {
        s.garbage_count = true;
        s.verdict = WaitVerdict::Undetermined;
        return s;
    }
    std::vector<uint32_t> order(total);
    for (uint32_t i = 0; i < total; ++i) order[i] = i;
    size_t n = order.size();
    while (n) {
        const uint32_t idx = order[0];
        if (idx >= list.size() || !list[idx].key_ok) {
            s.verdict = WaitVerdict::Undetermined;
            s.index = static_cast<int32_t>(idx);
            return s;
        }
        const WaitEntry& front = list[idx].e;
        if (front.found && !entry_ready(front)) {
            s.verdict = WaitVerdict::Blocks;
            s.index = static_cast<int32_t>(idx);
            s.key = front.key;
            return s;
        }
        if (n - 1 != 0) order[0] = order[n - 1];
        --n;
    }
    s.verdict = WaitVerdict::Empties;
    return s;
}

// ---------------------------------------------------------------- ClientActorContainer::Find

// 0x464290 (.rsrc), replayed with our own reads (no game call):
//   if u32 [C+0x8C] == 0 or u32 [C+0x88] (buckets) == 0: none
//   bucket = [C+0x98] + (key % buckets) * 0x100: u32 count, then
//     {u32 key, u32 index} pairs from +8
//   entry = [[C+0xA0] + index * 8]; u32 [entry+4] == key -> actor = [entry+8]
// A bucket holds at most 31 pairs (0x100 bytes); a larger count is garbage.
struct ContainerLayout {
    static constexpr uint32_t kUsed = 0x8C, kBuckets = 0x88, kBucketArray = 0x98, kEntries = 0xA0;
    static constexpr uint32_t kBucketSize = 0x100, kBucketMax = 31;
    static constexpr uint32_t kEntryKey = 4, kEntryActor = 8;
};

// The container's header, read once for a whole walk of the wait list
// (p8-hilo-red.md: the first version re-read it for each of the 193 keys).
// ok = false: Find returns none for every key (no entries, no buckets, or a
// header that cannot be read).
struct ContainerView {
    bool ok = false;
    uint32_t buckets = 0;
    uintptr_t array = 0, entries = 0;
};

// Mem: bool u32(uintptr_t, uint32_t&) and bool ptr(uintptr_t, uintptr_t&), both checked reads.
template <class Mem>
ContainerView container_view(const Mem& m, uintptr_t c) {
    using L = ContainerLayout;
    ContainerView v;
    uint32_t used = 0;
    if (!c || !m.u32(c + L::kUsed, used) || !used || !m.u32(c + L::kBuckets, v.buckets) || !v.buckets) return {};
    if (!m.ptr(c + L::kBucketArray, v.array) || !v.array || !m.ptr(c + L::kEntries, v.entries) || !v.entries) {
        return {};
    }
    v.ok = true;
    return v;
}

template <class Mem>
uintptr_t container_find_in(const Mem& m, const ContainerView& v, uint32_t key) {
    using L = ContainerLayout;
    if (!v.ok || !v.buckets) return 0;
    uint32_t count = 0;
    const uintptr_t bucket = v.array + static_cast<uintptr_t>(key % v.buckets) * L::kBucketSize;
    if (!m.u32(bucket, count) || count > L::kBucketMax) return 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t k = 0, index = 0, ek = 0;
        uintptr_t entry = 0, actor = 0;
        if (!m.u32(bucket + 8 + 8 * i, k) || k != key) continue;
        if (!m.u32(bucket + 0xC + 8 * i, index) || !m.ptr(v.entries + 8 * static_cast<uintptr_t>(index), entry) ||
            !entry || !m.u32(entry + L::kEntryKey, ek) || ek != key) {
            continue;
        }
        return m.ptr(entry + L::kEntryActor, actor) ? actor : 0;
    }
    return 0;
}

// One key, header included (the replay of 0x464290 as it is).
template <class Mem>
uintptr_t container_find(const Mem& m, uintptr_t c, uint32_t key) {
    return container_find_in(m, container_view(m, c), key);
}

}  // namespace cdmp::session2::gad
