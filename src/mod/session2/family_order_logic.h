#pragma once
// P8 S15/S16 (pendientes/p8-s14-viajeros.md §7, p8-s15-viaje-tunel.md §8.1): the pure pieces of
// observer_hold_family (child_hold.cpp). Header-only, no game access: unit-tested in optd_s3_tests.
//
// Every actor record of an ActorLoadDataAck 2005 ends with u32 parent (actor+0xAC) | u16 n | n x u32 children
// (actor+0xB0) (copias-grupo-invisibles.md §1). A client that gets a record whose parent it does not have (no
// earlier 2005 since the last RemoveActorAck 2516, nor in the same 2005) destroys that child in its pending-login
// loop (0x8B7D78) and the parent, when it comes, never forms its login group. The native server never sends one
// (host capture S15: 0); user 2 got 60-106 per session from observer_hold / observer_load_defer.
//
// - KeySet: a fixed, allocation-free set of actor keys (linear probing, backward-shift deletion). Full -> it
//   OVERFLOWS and answers "present" for every key from then on (fail open: the family rule then lets everything
//   through as without the key) until clear().
// - scan_2005 / record_family: the 2005 payload's records and their family tail, exactly as
//   tools/scripts/analyze_capture.py `orphans` reads them (the same heuristic, so both count the same).
// - OrphanCounter: the host-side mirror of that `orphans` command over what take_frame queues for user 2.
// - family_owner: who gets a family member now when it must be withheld from `need` sessions (plan_split's
//   owner), or nothing (a mixed recipient set: left native).

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace cdmp::session2::fam {

inline constexpr uint16_t kOpAnnounce = 2005;  // ActorLoadDataAck
inline constexpr uint16_t kOpRemove = 2516;    // RemoveActorAck: u8 | u32 | u8 reason | n x u32 key (keys at +6)
inline constexpr uint16_t kOpMoveField = 2174;  // MoveFieldAck: a field change (the client starts empty)

inline uint32_t le32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}
inline uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

// ---------------------------------------------------------------- KeySet

template <size_t N>
class KeySet {
    static_assert(N >= 16 && (N & (N - 1)) == 0, "a power of two");

public:
    static constexpr size_t capacity() { return N * 3 / 4; }
    bool contains(uint32_t k) const {
        if (!k) return false;
        if (overflow_) return true;  // fail open
        for (size_t i = home(k);; i = (i + 1) & (N - 1)) {
            if (slot_[i] == k) return true;
            if (!slot_[i]) return false;
        }
    }
    // False = already there, or the set overflowed now (or before).
    bool insert(uint32_t k) {
        if (!k || overflow_) return false;
        size_t i = home(k);
        for (;; i = (i + 1) & (N - 1)) {
            if (slot_[i] == k) return false;
            if (!slot_[i]) break;
        }
        if (n_ >= capacity()) {
            overflow_ = true;
            return false;
        }
        slot_[i] = k;
        ++n_;
        return true;
    }
    bool erase(uint32_t k) {
        if (!k || overflow_) return false;
        size_t i = home(k);
        for (;; i = (i + 1) & (N - 1)) {
            if (slot_[i] == k) break;
            if (!slot_[i]) return false;
        }
        // Backward-shift deletion: every later key of the run that may move into the hole does.
        size_t j = i;
        for (;;) {
            j = (j + 1) & (N - 1);
            if (!slot_[j]) break;
            const size_t h = home(slot_[j]);
            // slot_[j] stays when its home lies cyclically in (i, j].
            const bool stays = i <= j ? (i < h && h <= j) : (i < h || h <= j);
            if (stays) continue;
            slot_[i] = slot_[j];
            i = j;
        }
        slot_[i] = 0;
        --n_;
        return true;
    }
    void clear() {
        slot_.fill(0);
        n_ = 0;
        overflow_ = false;
    }
    size_t size() const { return n_; }
    bool overflow() const { return overflow_; }

private:
    static size_t home(uint32_t k) { return static_cast<size_t>((k * 0x9E3779B1u) >> 7) & (N - 1); }
    std::array<uint32_t, N> slot_{};
    size_t n_ = 0;
    bool overflow_ = false;
};

// ---------------------------------------------------------------- the 2005 records

struct Record {
    std::span<const uint8_t> bytes;  // [u8 type][u32 key]...
    uint8_t type = 0;
    uint32_t key = 0;
};

// Calls f(Record) for each record of a 2005 payload ([u16 len][record]...; records under 5 bytes are skipped,
// a zero or overlong length ends the walk, as analyze_capture.py's actor_records). Returns how many it saw.
template <class F>
size_t scan_2005(std::span<const uint8_t> p, F&& f) {
    size_t i = 0, n = 0;
    while (i + 2 <= p.size()) {
        const uint16_t len = le16(&p[i]);
        if (len == 0 || i + 2 + len > p.size()) break;
        if (len >= 5) {
            Record r;
            r.bytes = p.subspan(i + 2, len);
            r.type = r.bytes[0];
            r.key = le32(&r.bytes[1]);
            f(r);
            ++n;
        }
        i += 2 + len;
    }
    return n;
}

// The record's tail: u32 parent | u16 n | n x u32 children, with n in 0..32 and every key plausible (0xA / 0xB;
// the parent may be 0). The smallest n that fits wins (analyze_capture.py's record_family). False: none fits.
inline bool plausible_key(uint32_t k) { return (k >> 28) == 0xA || (k >> 28) == 0xB; }
inline bool record_family(std::span<const uint8_t> rec, uint32_t& parent, uint16_t& n_children) {
    for (size_t n = 0; n <= 32; ++n) {
        if (rec.size() < 6 + 4 * n) continue;
        if (le16(&rec[rec.size() - 2 - 4 * n]) != n) continue;
        bool ok = true;
        for (size_t c = 0; c < n && ok; ++c) ok = plausible_key(le32(&rec[rec.size() - 4 * n + 4 * c]));
        const uint32_t par = le32(&rec[rec.size() - 6 - 4 * n]);
        if (!ok || (par != 0 && !plausible_key(par))) continue;
        parent = par;
        n_children = static_cast<uint16_t>(n);
        return true;
    }
    return false;
}

// Is `key` among the records of this 2005?
inline bool announces(std::span<const uint8_t> p, uint32_t key) {
    bool found = false;
    scan_2005(p, [&](const Record& r) { found = found || r.key == key; });
    return found;
}

// ---------------------------------------------------------------- what user 2 got (take_frame)

struct OrphanSeen {
    uint32_t key = 0, parent = 0;
    uint8_t type = 0;
};

// analyze_capture.py `orphans` on the host: every 2005 / 2516 queued for user 2, in order. A record of type != 0
// whose family tail names a parent that is neither alive on that client (an earlier 2005, no 2516 since) nor in
// the same 2005 is an ORPHAN. Not thread-safe (the caller locks).
template <size_t N>
class OrphanCounter {
public:
    struct Result {
        size_t records = 0, orphans = 0;
        OrphanSeen first;  // the first orphan of this 2005 (key 0: none)
    };
    Result on_2005(std::span<const uint8_t> payload) {
        Result res;
        res.records = scan_2005(payload, [&](const Record& r) {
            uint32_t parent = 0;
            uint16_t n = 0;
            if (r.type == 0 || !record_family(r.bytes, parent, n) || !parent) return;
            if (alive_.contains(parent) || announces(payload, parent)) return;
            if (!res.orphans) res.first = {r.key, parent, r.type};
            ++res.orphans;
        });
        scan_2005(payload, [&](const Record& r) { alive_.insert(r.key); });
        records_ += res.records;
        orphans_ += res.orphans;
        return res;
    }
    size_t on_2516(std::span<const uint8_t> payload) {
        size_t n = 0;
        for (size_t i = 6; i + 4 <= payload.size(); i += 4) n += alive_.erase(le32(&payload[i])) ? 1 : 0;
        return n;
    }
    void clear() { alive_.clear(); }
    bool alive(uint32_t k) const { return alive_.contains(k); }
    bool overflow() const { return alive_.overflow(); }
    size_t alive_size() const { return alive_.size(); }
    uint64_t records() const { return records_; }
    uint64_t orphans() const { return orphans_; }

private:
    KeySet<N> alive_;
    uint64_t records_ = 0, orphans_ = 0;
};

// RemoveActorAck 2516: the keys it removes (+6, 4 bytes each).
template <class F>
void removed_keys(std::span<const uint8_t> p, F&& f) {
    for (size_t i = 6; i + 4 <= p.size(); i += 4) f(le32(&p[i]));
}

// ---------------------------------------------------------------- who gets a withheld member now

// sessions[i] / users[i] are the call's recipients (null sessions ignored); need[i] = the member must be withheld
// from sessions[i]. The owner (plan_split's: the user whose sessions get it now) is the user of every recipient
// that does NOT need it; when every recipient needs it, the "other" user (all user 2's -> the host, all the
// host's -> user 2). 0 = none fits (some user would get it on one session and not on another, or both users
// need it, or a user is unknown): the member is left native. Returns 0 too when nothing needs withholding.
inline uintptr_t family_owner(std::span<const uintptr_t> sessions, std::span<const uintptr_t> users,
                              std::span<const bool> need, uintptr_t host, uintptr_t user2) {
    if (sessions.size() != users.size() || sessions.size() != need.size()) return 0;
    uintptr_t recv = 0, needer = 0;
    bool any_need = false, any_recv = false;
    for (size_t i = 0; i < sessions.size(); ++i) {
        if (!sessions[i]) continue;
        if (!users[i]) return 0;
        if (need[i]) {
            if (any_need && users[i] != needer) needer = ~uintptr_t{0};  // several users need it
            else needer = users[i];
            any_need = true;
        } else {
            if (any_recv && users[i] != recv) return 0;  // two users would get it now
            recv = users[i];
            any_recv = true;
        }
    }
    if (!any_need) return 0;
    if (any_recv) {
        // The receiving user must not need it on another of its sessions.
        for (size_t i = 0; i < sessions.size(); ++i) {
            if (sessions[i] && need[i] && users[i] == recv) return 0;
        }
        return recv;
    }
    if (needer == user2 && host) return host;
    if (needer == host && user2) return user2;
    return 0;
}

}  // namespace cdmp::session2::fam
