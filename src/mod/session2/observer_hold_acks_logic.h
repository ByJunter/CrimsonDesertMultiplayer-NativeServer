#pragma once
// P8 S3, the observer_hold side effect (docs/research/pendientes/p8-escenas-y-acks.md §3;
// p8-sesion6.md §3.4): [session2] observer_hold_acks=1, HOST. The pure logic, with no game
// access, so the tests can run it on the packets of the captures.
//
// The fault [V] (session 6B, guest capture cdmp_packets_20261009-224848.bin): observer_hold
// withholds an NPC's 2005 from user 2 until the host's client gave it its first action, and
// re-announces it with a fresh native 2005 after StartActionReq::vf2 (child_hold.h). But the
// server keeps relaying that NPC's per-actor packets to user 2 meanwhile (P5 relay_fix): for the
// 465 NPCs observer_hold released, user 2's client got 3 x ChangeAIStateAck 2404 (431 keys:
// 2404, 2404, 2404, 2723), the StartSpawnActionAck 2723 (all 465, the last one, ~1 us before
// the 2005: it is emitted inside vf2, before the release) and for 10 keys
// DataDefinedRegenerateValueAck 2586, ALL BEFORE the key's 2005. A client drops a packet for a
// key it does not know (2991 handler 0xB579F9; companeros-publicacion.md §3): "2723 consumer 0".
//
// What this does: for a key observer_hold withholds from user 2's session (never from its
// controller: the controller is never held), the per-actor packets to session 2 are kept, in
// order, and pushed right after the 2005 that announces the key, in their original order (one
// sequence across keys). A drop of the hold (despawn, out of range, epoch, user 2 leaving)
// discards them: the client would drop them anyway. Bounded per key and in total; over a bound
// the key's packets go out at once in order (the behaviour without this key) and the rest of its
// hold passes natively.
//
// The per-actor opcodes (u32 actor key at payload +0) are those the session 6B captures show
// before a 2005 (and after it, for the same keys): 2723, 2404, 2586 for the held NPCs; 2991,
// 2697, 2642, 2506, 2402, 2645, 2434 for other keys; 2356 for the family members observer_hold_family withholds
// with their parent (P8 S14). Nothing else is held.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace cdmp::session2::oha {

inline constexpr uint16_t kOpAnnounce = 2005;     // ActorLoadDataAck
inline constexpr uint16_t kOpScopeSender = 2042;  // ScopeSenderAck: a native bundle (contents not parsed)
inline constexpr uint32_t kNpcKeyClass = 0xB;     // key >> 28 of NPCs and children

// The per-actor S>C messages held for a withheld key, u32 actor key at payload +0.
inline constexpr std::array<uint16_t, 11> kHeldOps = {
    2723,  // StartSpawnActionAck
    2404,  // ChangeAIStateAck
    2586,  // DataDefinedRegenerateValueAck
    2991,  // StartActionAck
    2697,  // MoveActorAck
    2642,  // UpdateElementalAck
    2506,  // ApplyBuffAck
    2402,  // ClearAllyGroupDataAck
    2645,  // SetMinStatRateAck
    2434,  // VaryDataDefinedStaticStatAck
    2356,  // GimmickCatcherUpdateAck: a carried piece's (observer_hold_family; p8-s14-viajeros.md §3 A.2)
};
inline constexpr size_t kOpCount = kHeldOps.size();

inline int op_index(uint16_t op) {
    for (size_t i = 0; i < kHeldOps.size(); ++i) {
        if (kHeldOps[i] == op) return static_cast<int>(i);
    }
    return -1;
}

inline uint32_t le32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

// The NPC key a held opcode carries (nullopt: not one of them, too short, or not an NPC key).
inline std::optional<uint32_t> npc_key_of(uint16_t opcode, std::span<const uint8_t> payload) {
    if (op_index(opcode) < 0 || payload.size() < 4) return std::nullopt;
    const uint32_t key = le32(payload.data());
    if ((key >> 28) != kNpcKeyClass) return std::nullopt;
    return key;
}

// character_hold (child_hold.h): a user's main character (0xA...) withheld from user 2 is held here
// too. The key a held opcode carries for the route: an NPC/child key (as npc_key_of) or a character
// key. Only keys begun by child_hold are ever held (offer passes any other), so with character_hold=0
// a character's packets pass exactly as before.
inline constexpr uint32_t kCharKeyClass = 0xA;
inline std::optional<uint32_t> held_key_of(uint16_t opcode, std::span<const uint8_t> payload) {
    if (op_index(opcode) < 0 || payload.size() < 4) return std::nullopt;
    const uint32_t key = le32(payload.data());
    if ((key >> 28) != kNpcKeyClass && (key >> 28) != kCharKeyClass) return std::nullopt;
    return key;
}

// The actor keys an ActorLoadDataAck 2005 payload announces: records [u16 len][u8 type][u32 key]...
// (guest_s3_logic.h parse_2005; type 0 is the user record NotifyAppear puts first). Stops at the
// first record that does not fit. Returns how many were appended.
inline size_t announce_keys(std::span<const uint8_t> payload, std::vector<uint32_t>& out, size_t max = 4096) {
    size_t n = 0, p = 0;
    while (n < max && p + 2 <= payload.size()) {
        uint16_t len = 0;
        std::memcpy(&len, payload.data() + p, 2);
        if (len == 0 || p + 2 + len > payload.size()) break;
        if (len >= 5 && payload[p + 2] != 0) {
            out.push_back(le32(payload.data() + p + 3));
            ++n;
        }
        p += 2 + len;
    }
    return n;
}

struct Limits {
    size_t max_keys = 1024;                // = observer_hold's NPC table (hold::kNpcTableCapacity)
    size_t max_packets_per_key = 32;       // session 6B: at most 5 per key (3 x 2404 + 2 x 2586... + 2723)
    size_t max_bytes_per_key = 32 * 1024;  // session 6B: at most 3437 bytes per key
    size_t max_bytes_total = 4u << 20;
    // > observer_hold's longest wait: observer_load_defer's 60 s (hold::kLoadDeferMaxMs, while user 2 loads) and
    // then 3 x its 10 s timeout (dropped). Was 40 s before observer_load_defer (P8 session 14 review L3).
    uint64_t max_hold_ms = 100000;
    uint64_t orphan_ms = 2000;     // a release whose 2005 was not seen: its packets wait this long
};

enum class Begin : uint8_t { Started, Again, Full };
enum class Offer : uint8_t { Pass, Held, Spill };
// Why held packets were thrown away (never delivered: the client would drop them).
enum class Discard : uint8_t { Dropped, UserGone, Epoch, Orphan, Expired, Count };
inline const char* discard_name(Discard d) {
    switch (d) {
        case Discard::Dropped: return "the hold was dropped";
        case Discard::UserGone: return "user 2 gone or leaving";
        case Discard::Epoch: return "a new guest epoch";
        case Discard::Orphan: return "released, but no 2005 for it was seen";
        case Discard::Expired: return "held too long";
        default: return "?";
    }
}

struct HeldPacket {
    uint64_t seq = 0;  // one order across keys
    uint16_t opcode = 0;
    uint32_t key = 0;
    uint64_t t_ms = 0;
    std::vector<uint8_t> bytes;
};

struct Counters {
    uint64_t begun = 0, again = 0, full = 0;
    uint64_t held = 0;
    std::array<uint64_t, kOpCount> held_by_op{};
    uint64_t released_pkts = 0, released_keys = 0, announced_empty = 0;
    uint64_t wait_sum_ms = 0, wait_max_ms = 0;  // first held packet -> its 2005
    uint64_t per_key_max = 0;
    std::array<uint64_t, static_cast<size_t>(Discard::Count)> discarded_pkts{}, discarded_keys{};
    uint64_t spilled_keys = 0, spilled_pkts = 0, spilled_closed = 0;
    uint64_t keys_peak = 0, bytes_peak = 0;
};

// What one 2005 released (for the log).
struct Released {
    uint32_t key = 0;
    uint32_t count = 0;
    uint64_t wait_ms = 0;
    uint16_t first_op = 0, last_op = 0;
};

class AckHold {
public:
    explicit AckHold(Limits l = {}) : l_(l) {}

    // observer_hold withholds `key` from user 2 (its entry was added or refreshed).
    Begin begin(uint32_t key, uint32_t epoch, uint64_t now_ms) {
        auto it = m_.find(key);
        if (it != m_.end()) {
            Entry& e = it->second;
            if (e.epoch != epoch) {
                discard(e, Discard::Epoch);
                e = Entry{};
                e.epoch = epoch;
                e.since_ms = now_ms;
                ++c_.begun;
                return Begin::Started;
            }
            // Withheld again: what it holds stays in order; a spilled key holds again from now.
            // Its 40 s bound counts from this hold (P8 review I5: with the first one's time a
            // key withheld again and again expired early and lost what its 2005 would release).
            e.state = State::Holding;
            e.deadline_ms = 0;
            e.since_ms = now_ms;
            ++c_.again;
            return Begin::Again;
        }
        if (m_.size() >= l_.max_keys) {
            ++c_.full;
            return Begin::Full;
        }
        Entry e;
        e.epoch = epoch;
        e.since_ms = now_ms;
        m_.emplace(key, std::move(e));
        ++c_.begun;
        c_.keys_peak = std::max<uint64_t>(c_.keys_peak, m_.size());
        return Begin::Started;
    }

    // A held opcode for `key` on its way to user 2. Held: moved in (packet emptied). Spill: the key
    // went over a bound: `spill` gets its held packets then this one, in order, to push now (the key
    // passes from now on). Pass: push it as usual.
    Offer offer(uint16_t opcode, uint32_t key, std::vector<uint8_t>& packet, uint64_t now_ms,
                std::vector<std::vector<uint8_t>>& spill, bool* first_for_key = nullptr) {
        if (first_for_key) *first_for_key = false;
        auto it = m_.find(key);
        if (it == m_.end() || it->second.state == State::Spilled) return Offer::Pass;
        Entry& e = it->second;
        const size_t size = packet.size();
        if (e.q.size() + 1 > l_.max_packets_per_key || e.bytes + size > l_.max_bytes_per_key ||
            bytes_ + size > l_.max_bytes_total) {
            ++c_.spilled_keys;
            c_.spilled_pkts += e.q.size() + 1;
            for (HeldPacket& h : e.q) spill.push_back(std::move(h.bytes));
            spill.push_back(std::move(packet));
            packet.clear();
            bytes_ -= e.bytes;
            e.q.clear();
            e.bytes = 0;
            e.state = State::Spilled;
            return Offer::Spill;
        }
        HeldPacket h;
        h.seq = ++seq_;
        h.opcode = opcode;
        h.key = key;
        h.t_ms = now_ms;
        h.bytes = std::move(packet);
        packet.clear();
        if (e.q.empty()) {
            e.first_ms = now_ms;
            if (first_for_key) *first_for_key = true;
        }
        e.q.push_back(std::move(h));
        e.bytes += size;
        bytes_ += size;
        ++c_.held;
        if (const int i = op_index(opcode); i >= 0) ++c_.held_by_op[static_cast<size_t>(i)];
        c_.bytes_peak = std::max<uint64_t>(c_.bytes_peak, bytes_);
        return Offer::Held;
    }

    // A 2005 to user 2 announcing `keys`: their holds end; returns their held packets in their
    // original order (one sequence across keys), to push right after the 2005. matched: how many
    // of the keys had a hold (ended now, with or without packets): the caller then queues the 2005
    // itself under its lock, so no packet of those keys can pass before it (P8 review I5).
    std::vector<HeldPacket> announce(std::span<const uint32_t> keys, uint64_t now_ms,
                                     std::vector<Released>* released = nullptr, size_t* matched = nullptr) {
        std::vector<HeldPacket> out;
        if (matched) *matched = 0;
        for (const uint32_t k : keys) {
            auto it = m_.find(k);
            if (it == m_.end()) continue;
            if (matched) ++*matched;
            Entry& e = it->second;
            if (e.state == State::Spilled) {
                ++c_.spilled_closed;
            } else if (e.q.empty()) {
                ++c_.announced_empty;
            } else {
                const uint64_t wait = now_ms >= e.first_ms ? now_ms - e.first_ms : 0;
                ++c_.released_keys;
                c_.released_pkts += e.q.size();
                c_.wait_sum_ms += wait;
                c_.wait_max_ms = std::max(c_.wait_max_ms, wait);
                c_.per_key_max = std::max<uint64_t>(c_.per_key_max, e.q.size());
                if (released) {
                    released->push_back(
                        {k, static_cast<uint32_t>(e.q.size()), wait, e.q.front().opcode, e.q.back().opcode});
                }
                bytes_ -= e.bytes;
                for (HeldPacket& h : e.q) out.push_back(std::move(h));
            }
            m_.erase(it);
        }
        std::sort(out.begin(), out.end(), [](const HeldPacket& a, const HeldPacket& b) { return a.seq < b.seq; });
        return out;
    }

    // observer_hold dropped the key (no 2005 will come): its packets go. Returns how many.
    size_t drop(uint32_t key, Discard why = Discard::Dropped) {
        auto it = m_.find(key);
        if (it == m_.end()) return 0;
        const size_t n = it->second.q.size();
        discard(it->second, why);
        m_.erase(it);
        return n;
    }

    // observer_hold released these keys (its NotifyAppear returned): a key whose 2005 did not
    // show up during that call waits orphan_ms more, then its packets go.
    void released(std::span<const uint32_t> keys, uint64_t now_ms) {
        for (const uint32_t k : keys) {
            auto it = m_.find(k);
            if (it == m_.end() || it->second.state != State::Holding) continue;
            it->second.state = State::Orphan;
            it->second.deadline_ms = now_ms + l_.orphan_ms;
        }
    }

    // Everything goes (user 2 left, a new epoch). Returns the packets thrown away.
    size_t clear(Discard why) {
        size_t n = 0;
        for (auto& [k, e] : m_) {
            n += e.q.size();
            discard(e, why);
        }
        m_.clear();
        bytes_ = 0;
        return n;
    }

    // Orphans past their deadline and holds older than max_hold_ms go. Returns the keys.
    struct Gone {
        uint32_t key = 0;
        Discard why = Discard::Expired;
        size_t packets = 0;
    };
    size_t expire(uint64_t now_ms, std::vector<Gone>* gone = nullptr) {
        size_t n = 0;
        for (auto it = m_.begin(); it != m_.end();) {
            Entry& e = it->second;
            Discard why = Discard::Count;
            if (e.state == State::Orphan && now_ms >= e.deadline_ms) why = Discard::Orphan;
            else if (now_ms - e.since_ms >= l_.max_hold_ms) why = Discard::Expired;
            if (why == Discard::Count) {
                ++it;
                continue;
            }
            if (gone) gone->push_back({it->first, why, e.q.size()});
            discard(e, why);
            it = m_.erase(it);
            ++n;
        }
        return n;
    }

    // S46 phase 3 (T2 active, hso::SceneHold): one order shared with the packets this table holds, so that a 2005's
    // release merges both by their original sequence.
    uint64_t next_seq() { return ++seq_; }

    bool holding(uint32_t key) const {
        auto it = m_.find(key);
        return it != m_.end() && it->second.state != State::Spilled;
    }
    // S46 phase 3 review M1 (T2): is the key's 2005 still to come through take_frame? Holding, or Spilled (its packets
    // went over a bound and pass natively, but its 2005 has NOT gone yet: announce still matches it). Not Orphan: its
    // NotifyAppear already returned (the 2005 went unseen, e.g. inside a 2042 bundle: the NPC may be on the client).
    bool withheld(uint32_t key) const {
        auto it = m_.find(key);
        return it != m_.end() && it->second.state != State::Orphan;
    }
    size_t keys() const { return m_.size(); }
    size_t bytes() const { return bytes_; }
    size_t packets() const {
        size_t n = 0;
        for (const auto& [k, e] : m_) n += e.q.size();
        return n;
    }
    const Counters& counters() const { return c_; }
    const Limits& limits() const { return l_; }

private:
    enum class State : uint8_t { Holding, Orphan, Spilled };
    struct Entry {
        uint32_t epoch = 0;
        State state = State::Holding;
        uint64_t since_ms = 0, first_ms = 0, deadline_ms = 0;
        size_t bytes = 0;
        std::vector<HeldPacket> q;
    };

    void discard(Entry& e, Discard why) {
        const auto i = static_cast<size_t>(why);
        if (!e.q.empty()) {
            c_.discarded_pkts[i] += e.q.size();
            ++c_.discarded_keys[i];
        }
        bytes_ -= std::min(bytes_, e.bytes);
        e.bytes = 0;
        e.q.clear();
    }

    Limits l_;
    std::unordered_map<uint32_t, Entry> m_;
    size_t bytes_ = 0;
    uint64_t seq_ = 0;
    Counters c_;
};

}  // namespace cdmp::session2::oha
