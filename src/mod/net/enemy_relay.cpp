#include "mod/net/enemy_relay.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>

#include "mod/game/actor_lookup.h"
#include "mod/log.h"
#include "mod/net/keys.h"
#include "mod/net/local_capture.h"
#include "mod/net/remote_puppet.h"

namespace cdmp::net {
namespace {

using Bytes = std::vector<uint8_t>;

constexpr uint16_t kOpActorLoadDataAck = 2005;
constexpr uint16_t kOpRemoveActorAck = 2516;
constexpr uint16_t kOpStartActionReq = 2223;
constexpr uint16_t kOpRotateActorReq = 2828;
constexpr uint16_t kOpChangeBattleStateReq = 2257;  // 5 B: u32 actor | u8 state (0 = out of combat)
constexpr uint16_t kOpDoFrameEventReq = 2380;       // u8 4 | u32 attacker | ... | 665-byte target entries at the end
constexpr uint64_t kEngagedMs = 20000;
// Server->client messages about an enemy (key at +0), relayed for its copy
// (enemigos-del-anfitrion.md §3.1): HP and death reliably, hit reactions not.
constexpr uint16_t kOpVaryHPAck = 2147;               // 92 B; +0x40 per-actor sequence, kept
constexpr uint16_t kOpNotifyHandleDeadAck = 2530;     // 19 B: dead | killer | reason
constexpr uint16_t kOpAttackTargetAck = 2922;         // hit effect on the target at +0
constexpr uint16_t kOpApplyActionRestrictionAck = 2162;
constexpr size_t kRotateActorReqSize = 176;
constexpr uint64_t kPosIntervalMs = 66;  // ~15 Hz
constexpr uint64_t kPosKeepAliveMs = 900;  // the remote buffer wants a sample every <= 1 s
constexpr size_t kStartActionReqSize = 725;
constexpr uint32_t kPuppetUser = 0x9010F001;
constexpr uint32_t kPuppetPlayer = 0xA010F001;
constexpr float kPickRadius = 40.0f;

uint32_t u32_at(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

void replace_all(Bytes& v, uint32_t from, uint32_t to) {
    if (!from || from == to) return;
    for (size_t i = 0; i + 4 <= v.size(); ++i) {
        if (u32_at(v.data() + i) == from) {
            std::memcpy(v.data() + i, &to, 4);
            i += 3;
        }
    }
}

// ---- sending side
std::mutex g_mutex;
std::map<uint32_t, Bytes> g_loads;    // world NPC key -> its ActorLoadDataAck payload
// world NPC key -> its last StartActionReqs, oldest first (up to kKeptActions):
// an enemy with two action layers needs one for each (live test: a copy with
// only its layer-0 action crashed on layer 1).
constexpr size_t kKeptActions = 4;
std::map<uint32_t, std::deque<Bytes>> g_actions;
std::map<uint32_t, uint32_t> g_sent;  // world NPC key -> copy key we announced
// World NPC key -> when it last showed it is fighting: battle state != 0, a
// blow at our player or the other player's copy, or a blow from our player.
// "Aimed" alone is not enough: 4,138 fleeing animals aim at the player, and
// 133 of 135 hostile humans but 1 of 26,654 animals enter battle state
// (pendiente-jinetes-animales.md §1.3).
std::map<uint32_t, uint64_t> g_engaged;
std::set<uint32_t> g_refused;          // could not be copied safely (riders...): not retried
uint32_t g_next_net_id = 1;                    // copy key = kEnemyCopyBase + net id, 1..kMaxNetId
constexpr uint32_t kMaxNetId = 0xEFFF;          // index 0xFEFFF: below the client's 0xFFFFE/F sentinels
constexpr uint64_t kNetIdQuarantineMs = 10000;  // a removed copy's id is not reused for 10 s
std::map<uint32_t, uint64_t> g_retired;         // net id -> when its copy was removed
std::deque<std::pair<proto::RelKind, Bytes>> g_outgoing;
std::map<uint32_t, Bytes> g_last_hp;  // world NPC key -> its last VaryHPAck payload
// Every actor of our world we saw loaded (any record of any ActorLoadDataAck)
// and its record type: a copy must not name any of them.
std::map<uint32_t, uint8_t> g_known;
std::deque<proto::GameEvent> g_out_events;  // actions of copied enemies
struct Sampled {
    proto::Vec3 pos{};
    uint64_t ms = 0;
    bool flip = false;
    bool has = false;
    int jumps_logged = 0;
};
constexpr float kMaxSpeed = 30.0f;  // m/s; faster is a bogus read (see below)

// The parent (u32 key) in an enemy's last StartActionReq (+0x116 Path.parent):
// during a finisher the enemy hangs from the player who does it.
uint32_t last_action_parent(uint32_t key) {
    std::lock_guard lock(g_mutex);
    auto a = g_actions.find(key);
    if (a == g_actions.end() || a->second.empty() || a->second.back().size() < 0x11A) return 0;
    return u32_at(a->second.back().data() + 0x116);
}
std::map<uint32_t, Sampled> g_sampled;  // world NPC key -> last position sent (network thread)
uint64_t g_last_sample_ms = 0;

// Our world's keys as the receiver must see them: the enemy becomes its
// copy, we become the receiver's copy of us, and the receiver's copy here
// (0xA010F001, the receiver itself) becomes kSelfMarker, which the receiver
// turns into its own player key. Without that swap, an enemy fighting the
// other player's copy here would fight OUR copy over there
// (pendiente-proyectiles.md §4.3).
void translate(Bytes& v, uint32_t key, uint32_t copy) {
    replace_all(v, kPuppetPlayer, proto::kSelfMarker);
    replace_all(v, key, copy);
    replace_all(v, capture_own_user_key(), kPuppetUser);
    replace_all(v, capture_own_player_key(), kPuppetPlayer);
}

// ---- receiving side (network thread only)
// The sender's kSelfMarker means us. False if our own key is not known yet.
bool untranslate(Bytes& v) {
    const uint32_t own = capture_own_player_key();
    if (!own) return false;
    replace_all(v, proto::kSelfMarker, own);
    return true;
}

// Sizes of the server->client and client->server messages we take from the
// other side and hand to the game (pendiente-robustez.md §4.2).
bool valid_size(uint16_t op, size_t n) {
    switch (op) {
        case kOpStartActionReq: return n == kStartActionReqSize;
        case kOpRotateActorReq: return n == kRotateActorReqSize;
        case kOpAttackTargetAck: return n >= 0xA0 && n <= 0x400;
        case kOpApplyActionRestrictionAck: return n == 77;
        case kOpVaryHPAck: return n == 92;
        case kOpNotifyHandleDeadAck: return n == 19;
        default: return false;
    }
}

std::atomic<bool> g_receive_enabled{false};
std::map<uint32_t, bool> g_copies;          // copy keys loaded here
std::map<uint32_t, uint64_t> g_report_due;  // copy key -> when to log where it is

uint64_t now_ms() { return GetTickCount64(); }

struct Seen {
    uint32_t s390 = 0, s394 = 0;
    uint8_t s340 = 0, ts3dc = 0, login = 0;
    uint16_t state = 0;
};

bool read_seen(uintptr_t a, Seen& f) {
    __try {
        f.state = *reinterpret_cast<const uint16_t*>(a + 0x5E);
        f.login = *reinterpret_cast<const uint8_t*>(a + 0xCF);
        const auto comps = *reinterpret_cast<const uintptr_t*>(a + 0x68);
        if (!comps) return true;
        const auto st = *reinterpret_cast<const uintptr_t*>(comps + 0x20);
        if (st) {
            f.s390 = *reinterpret_cast<const uint32_t*>(st + 0x390);
            f.s394 = *reinterpret_cast<const uint32_t*>(st + 0x394);
            f.s340 = *reinterpret_cast<const uint8_t*>(st + 0x340);
        }
        const auto ts = *reinterpret_cast<const uintptr_t*>(comps + 0x1A0);
        if (ts) f.ts3dc = *reinterpret_cast<const uint8_t*>(ts + 0x3DC);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void report(uint32_t copy) {
    const auto a = game::find_actor(copy);
    if (!a) {
        log::info("Enemy relay: copy 0x{:08X} is NOT in the game's actor list", copy);
        return;
    }
    const auto p = game::actor_world_position(copy);
    const auto me = game::actor_world_position(capture_own_player_key());
    Seen f;
    read_seen(*a, f);
    log::info("Enemy relay: copy 0x{:08X} exists: at ({:.1f}, {:.1f}, {:.1f}), {:.1f} m from you | state +5E 0x{:X} "
              "login +CF {} | Status +390 0x{:X} +394 0x{:X} +340 {} | TS+3DC {}",
              copy, p ? p->x : NAN, p ? p->y : NAN, p ? p->z : NAN,
              p && me ? std::sqrt((p->x - me->x) * (p->x - me->x) + (p->y - me->y) * (p->y - me->y) +
                                  (p->z - me->z) * (p->z - me->z))
                      : NAN,
              f.state, f.login, f.s390, f.s394, f.s340, f.ts3dc);
}

float distance(const proto::Vec3& a, const proto::Vec3& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

std::optional<proto::Vec3> my_position() {
    const uint32_t own = capture_own_player_key();
    return own ? game::actor_world_position(own) : std::nullopt;
}

// Type 3 only: type 6 is every animal down to beetles and dragonflies (the
// first live test copied those).
struct Candidate {
    uint32_t key;
    bool engaged;  // fighting in the last 20 s (g_engaged)
    float d;
};
std::vector<Candidate> candidates() {
    std::vector<Candidate> out;
    const auto me = my_position();
    if (!me) return out;
    std::vector<std::pair<uint32_t, bool>> keys;
    const uint64_t now = GetTickCount64();
    {
        std::lock_guard lock(g_mutex);
        for (const auto& [key, load] : g_loads) {
            if (g_sent.count(key) || g_refused.count(key) || load.size() < 3 || load[2] != 3) continue;
            auto e = g_engaged.find(key);
            keys.emplace_back(key, e != g_engaged.end() && now - e->second < kEngagedMs);
        }
    }
    for (const auto& [key, engaged] : keys) {
        const auto p = game::actor_world_position(key);
        if (!p) continue;
        const float d = distance(*p, *me);
        if (d < kPickRadius) out.push_back({key, engaged, d});
    }
    return out;
}

// Ctrl+F6: the enemy we just hit, else the nearest one fighting us, else the
// nearest (the first picker preferred any attacker up to 45 m away, so the
// copies showed far from the enemy the player was looking at).
std::optional<uint32_t> pick_manual() {
    const auto list = candidates();
    const uint32_t hit = capture_last_attack_target();
    for (const auto& c : list) {
        if (c.key == hit) return c.key;
    }
    const Candidate* best = nullptr;
    for (const auto& c : list) {
        if (!best || (c.engaged && !best->engaged) || (c.engaged == best->engaged && c.d < best->d)) best = &c;
    }
    return best ? std::optional<uint32_t>(best->key) : std::nullopt;
}

// Every enemy load names where it spawns from (ids-enemigos-verificacion.md
// §2): a schedule (hash + element id), a faction camp slot (node + slot) or a
// sequence (hash + id), all relative to its first Transform T0. The copy goes
// without one, like an ambient spawn. (Not the cause of the invisible
// originals, copias-grupo-invisibles.md §6.7: that is the camp group.)
int strip_spawn_identity(Bytes& load) {
    static const uint8_t kSig[14] = {0x0C, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF};
    size_t t0 = 0;
    for (size_t i = 0x24; i + sizeof kSig + 18 + 40 <= load.size(); ++i) {
        if (std::memcmp(load.data() + i, kSig, sizeof kSig) == 0) {
            t0 = i + 18;
            break;
        }
    }
    if (!t0) return -1;
    float scale[3];
    std::memcpy(scale, load.data() + t0 + 28, sizeof scale);
    if (scale[0] != 1.0f || scale[1] != 1.0f || scale[2] != 1.0f) return -1;
    auto at = [&](size_t off) { return t0 + off + 4 <= load.size() ? u32_at(load.data() + t0 + off) : 0u; };
    auto zero = [&](size_t off, size_t n) {
        if (t0 + off + n <= load.size()) std::memset(load.data() + t0 + off, 0, n);
    };
    int stripped = 0;
    if (const uint32_t schedule = at(42)) {
        replace_all(load, schedule, 0);  // also its copy in the sequence block
        zero(46, 6);                     // element id, instance serial
        stripped = 1;
    } else if (const uint32_t seq = at(1043); seq && (at(1047) & 0xF0000000u) == 0x20000000u) {
        replace_all(load, seq, 0);
        zero(1047, 8);
        stripped = 3;
    } else if (at(390)) {
        zero(390, 6);  // faction node + slot
        stripped = 2;
    }
    return stripped;
}

// ---- a copy must name no actor but itself (copias-grupo-invisibles.md §6)
// A camp enemy's load lists its party (number, leader, members) and, at the
// very end, its parent (+0xAC, the camp leader) and children (+0xB0, a
// leader's members). A copy kept them: a member copy joined the real party
// and never finished logging in; a LEADER copy logged in with the real
// members as its children, which set Status+0x390 bit 2 on them (model alpha
// 0) for good: the invisible originals of the live tests. The copy becomes a
// solo enemy instead.

bool is_key_shape(uint32_t v) { return keys::is_class(v, keys::kClassWorld) || keys::is_class(v, keys::kClassPlayer); }

// Our network keys on the receiving side: never a real actor there.
bool is_network_key(uint32_t v) {
    return is_enemy_copy_key(v) || v == kPuppetUser || v == kPuppetPlayer || v == 0xB010F001 || v == 0xB010F002 ||
           v == 0xB010F003;
}

// A copy key for a new enemy copy: net ids go round 1..kMaxNetId, skipping
// those in use and those removed less than 10 s ago (their removal may still
// be on its way). 0 if none is free. Caller holds g_mutex.
uint32_t alloc_copy_key_locked() {
    const uint64_t now = GetTickCount64();
    for (uint32_t tries = 0; tries < kMaxNetId; ++tries) {
        const uint32_t id = g_next_net_id;
        g_next_net_id = id >= kMaxNetId ? 1 : id + 1;
        if (auto r = g_retired.find(id); r != g_retired.end()) {
            if (now - r->second < kNetIdQuarantineMs) continue;
            g_retired.erase(r);
        }
        const uint32_t copy = kEnemyCopyBase + id;
        bool used = false;
        for (const auto& [k, c] : g_sent) used = used || c == copy;
        if (!used) return copy;
    }
    return 0;
}

struct Tail {
    size_t ac = 0;  // offset of actor+0xAC (parent)
    uint32_t parent = 0;
    std::vector<uint32_t> children;  // actor+0xB0
    std::vector<uint32_t> hierarchy;  // Hierarchy children, just before
};

// Read from the end with its own counters: u16 n | n keys ends the record,
// +0xAC parent before it, and the Hierarchy u16 m | m keys before that.
std::optional<Tail> parse_tail(const Bytes& pl) {
    const size_t len = pl.size();
    for (size_t n = 0; n <= 64; ++n) {
        if (len < 2 + 4 * n + 8) break;
        const size_t o = len - 2 - 4 * n;
        if (o < 8) break;
        uint16_t cnt;
        std::memcpy(&cnt, pl.data() + o, 2);
        if (cnt != n) continue;
        bool keys = true;
        for (size_t i = 0; i < n; ++i) keys = keys && is_key_shape(u32_at(pl.data() + o + 2 + 4 * i));
        if (!keys) continue;
        const size_t ac = o - 4;
        for (size_t m = 0; m <= 32; ++m) {
            if (ac < 4 * m + 2 + 8) break;
            const size_t ho = ac - 4 * m - 2;
            uint16_t hm;
            std::memcpy(&hm, pl.data() + ho, 2);
            if (hm != m) continue;
            bool hk = true;
            for (size_t i = 0; i < m; ++i) hk = hk && is_key_shape(u32_at(pl.data() + ho + 2 + 4 * i));
            if (!hk) continue;
            Tail t;
            t.ac = ac;
            t.parent = u32_at(pl.data() + ac);
            for (size_t i = 0; i < n; ++i) t.children.push_back(u32_at(pl.data() + o + 2 + 4 * i));
            for (size_t i = 0; i < m; ++i) t.hierarchy.push_back(u32_at(pl.data() + ho + 2 + 4 * i));
            return t;
        }
    }
    return std::nullopt;
}

struct PartyBlock {
    size_t start = 0, end = 0;
    uint64_t number = 0;
    size_t members = 0, followers = 0;
};

// ClientPartyActorComponent (vf11 0x21D8020), anchored on the actor's own key
// in its member list: u64 number (0x201_xxxxxxxx) | u32 leader | u32 | 12 B |
// u8 | u32 n | n keys | u32 formation | u32 own index | u8 | u32 m | m keys | u32 leader.
std::optional<PartyBlock> find_party(const Bytes& pl, uint32_t own) {
    const size_t len = pl.size();
    for (size_t pos = 8; pos + 4 <= len; ++pos) {
        if (u32_at(pl.data() + pos) != own) continue;
        for (size_t idx = 0; idx < 64; ++idx) {
            if (pos < 4 * idx + 40) break;
            const size_t start_s = pos - 4 * idx;
            const uint32_t n = u32_at(pl.data() + start_s - 4);
            if (!(idx < n && n <= 100)) continue;
            const size_t e = start_s + 4 * n;
            if (e + 13 > len || u32_at(pl.data() + e + 4) != idx) continue;
            const uint32_t m = u32_at(pl.data() + e + 9);
            if (m > 100 || e + 13 + 4 * m + 4 > len) continue;
            const size_t start = start_s - 33;
            uint64_t number;
            std::memcpy(&number, pl.data() + start, 8);
            if ((number >> 32) != 0x201 || !is_key_shape(u32_at(pl.data() + start + 8)) || pl[e + 8] > 1 ||
                pl[start_s - 5] > 1) {
                continue;
            }
            return PartyBlock{start, e + 13 + 4 * m + 4, number, n, m};
        }
    }
    return std::nullopt;
}

// The party block of a solo NPC, byte for byte (once in each of the 764 solo
// type-3 and 727 type-6 loads captured): no group, no members, index -1.
const Bytes kSoloParty = [] {
    Bytes b(50, 0);
    b[28] = 0x01;                          // u8 Party+0x24
    std::memset(b.data() + 37, 0xFF, 4);  // own index -1
    return b;
}();

// CommonTargetActorComponent vf11: u32 | u32 | u32 n | n x 12 B | u64 | u32 | u32 +0x198
// (the leader's key in a member; its own key in solos and leaders).
int fix_target198(Bytes& pl, uint32_t from, uint32_t to) {
    int fixed = 0;
    for (size_t q = 8; q + 4 <= pl.size(); ++q) {
        if (u32_at(pl.data() + q) != from) continue;
        for (uint32_t e = 0; e < 16; ++e) {
            if (q < 12 + 12 * e + 4 + 8) break;
            if (u32_at(pl.data() + q - 12 - 12 * e - 4) == e) {
                std::memcpy(pl.data() + q, &to, 4);
                ++fixed;
                break;
            }
        }
    }
    return fixed;
}

// Caller holds g_mutex. First known actor key left in `v` (not ours), or 0.
uint32_t residual_key_locked(const Bytes& v, size_t from, uint32_t self, size_t* where = nullptr) {
    for (size_t i = from; i + 4 <= v.size(); ++i) {
        const uint32_t k = u32_at(v.data() + i);
        if (k == self || k == 0 || is_network_key(k)) continue;
        if (g_known.count(k)) {
            if (where) *where = i;
            return k;
        }
    }
    return 0;
}

// Turns `load` (keys already translated) into a solo enemy named only by
// `copy`. False (with `why`) when it cannot be done safely: then it is not sent.
bool sanitize_load(Bytes& load, uint32_t copy, std::string& why) {
    const auto tail = parse_tail(load);
    if (!tail) {
        why = "its actor tail could not be read";
        return false;
    }
    if (!tail->hierarchy.empty()) {
        why = "it has hierarchy children";
        return false;
    }
    uint8_t parent_type = 0;
    {
        std::lock_guard lock(g_mutex);
        if (auto k = g_known.find(tail->parent); k != g_known.end()) parent_type = k->second;
    }
    if (tail->parent && (parent_type == 5 || parent_type == 7)) {
        // Riders (mount = type 5) and tower shooters (type 7) are not copied
        // yet (pendiente-jinetes-animales.md §6): say which, to count them.
        why = parent_type == 5 ? std::format("it rides mount 0x{:08X} (riders are not copied yet)", tail->parent)
                               : std::format("it stands on gimmick 0x{:08X}, a tower or cart (not copied yet)",
                                             tail->parent);
        return false;
    }
    // 1) tail: parent 0, no children
    load.resize(tail->ac);
    load.insert(load.end(), 6, 0);
    // 2) party block -> the solo template
    std::string party = "no party";
    if (auto pb = find_party(load, copy)) {
        party = std::format("party 0x{:X} ({} members, {} followers) removed", pb->number, pb->members, pb->followers);
        load.erase(load.begin() + pb->start, load.begin() + pb->end);
        load.insert(load.begin() + pb->start, kSoloParty.begin(), kSoloParty.end());
    }
    // 3) Target+0x198 names the leader in a member: its own key instead
    const int target = tail->parent ? fix_target198(load, tail->parent, copy) : 0;
    // 4) record length
    const uint16_t len = static_cast<uint16_t>(load.size() - 2);
    std::memcpy(load.data(), &len, 2);
    // 5) nothing of our world may be left
    size_t at = 0;
    uint32_t left = 0;
    {
        std::lock_guard lock(g_mutex);
        left = residual_key_locked(load, 5, copy, &at);
    }
    if (left) {
        why = std::format("actor 0x{:08X} still named at +{}", left, at);
        return false;
    }
    why = std::format("parent 0x{:08X} and {} children removed, {}, Target+198 x{}", tail->parent,
                      tail->children.size(), party, target);
    return true;
}

// Caller holds g_mutex. Key fields of a relayed message (§6.5): enemies we
// copied become their copies, our player/user already are translated, and
// any other actor of our world becomes "none" (0, or FF x 8 for u64 parents).
void map_field_locked(Bytes& v, size_t off, bool parent64) {
    if (off + 4 > v.size()) return;
    const uint32_t k = u32_at(v.data() + off);
    if (k == 0 || k == 0xFFFFFFFFu || is_network_key(k)) return;
    if (auto c = g_sent.find(k); c != g_sent.end()) {
        std::memcpy(v.data() + off, &c->second, 4);
        return;
    }
    if (!g_known.count(k)) return;  // not an actor key
    if (parent64 && off + 8 <= v.size()) {
        std::memset(v.data() + off, 0xFF, 8);
    } else {
        std::memset(v.data() + off, 0, 4);
    }
}

// Caller holds g_mutex. Maps a relayed message (payload, keys translated);
// false if a key of our world is still in it (then it is dropped).
uint64_t g_dropped_messages = 0;
bool map_message_locked(uint16_t op, Bytes& v, uint32_t self) {
    switch (op) {
        case kOpStartActionReq: map_field_locked(v, 0x116, true), map_field_locked(v, 0x16C, false); break;
        case kOpRotateActorReq: map_field_locked(v, 0x54, true); break;
        case kOpVaryHPAck: map_field_locked(v, 0x54, false); break;
        case kOpNotifyHandleDeadAck: map_field_locked(v, 0x04, false); break;
        case kOpAttackTargetAck:
            map_field_locked(v, 0x04, false), map_field_locked(v, 0xBC, false), map_field_locked(v, 0xCE, false);
            break;
        case kOpApplyActionRestrictionAck: map_field_locked(v, 0x49, false); break;
        default: break;
    }
    size_t at = 0;
    if (const uint32_t left = residual_key_locked(v, 4, self, &at)) {
        if (g_dropped_messages++ % 50 == 0) {
            log::info("Enemy relay: message {} about 0x{:08X} dropped: actor 0x{:08X} at +{} [{} dropped]", op, self,
                      left, at, g_dropped_messages);
        }
        return false;
    }
    return true;
}

void send_enemy(uint32_t key, const char* why) {
    if (capture_key_alarm()) return;  // the game uses keys in our range (local_capture.cpp)
    Bytes load, action;
    uint32_t copy = 0;
    {
        std::lock_guard lock(g_mutex);
        auto l = g_loads.find(key);
        if (l == g_loads.end() || g_sent.count(key)) return;
        load = l->second;
        auto a = g_actions.find(key);
        if (a != g_actions.end()) {
            for (const Bytes& b : a->second) action.insert(action.end(), b.begin(), b.end());
        }
        copy = alloc_copy_key_locked();
        if (!copy) {
            log::warn("Enemy relay: enemy 0x{:08X} NOT copied: no free copy key", key);
            return;
        }
        g_sent[key] = copy;
    }
    // Keys of OUR world, as the receiver must see them: the enemy gets its
    // copy key, our user and player become the receiver's copies of us.
    translate(load, key, copy);
    translate(action, key, copy);
    const int origin = strip_spawn_identity(load);
    std::string cleaned;
    if (!sanitize_load(load, copy, cleaned)) {
        std::lock_guard lock(g_mutex);
        g_sent.erase(key);
        g_refused.insert(key);
        log::info("Enemy relay: enemy 0x{:08X} NOT copied: {}", key, cleaned);
        return;
    }
    {
        // Its recent actions, each mapped; those still naming our world go.
        std::lock_guard lock(g_mutex);
        Bytes kept;
        for (size_t at = 0; at + kStartActionReqSize <= action.size(); at += kStartActionReqSize) {
            Bytes one(action.begin() + at, action.begin() + at + kStartActionReqSize);
            if (map_message_locked(kOpStartActionReq, one, copy)) kept.insert(kept.end(), one.begin(), one.end());
        }
        action.swap(kept);
    }
    Bytes msg(8);
    std::memcpy(msg.data(), &copy, 4);
    const uint32_t n = static_cast<uint32_t>(load.size());
    std::memcpy(msg.data() + 4, &n, 4);
    msg.insert(msg.end(), load.begin(), load.end());
    msg.insert(msg.end(), action.begin(), action.end());
    {
        std::lock_guard lock(g_mutex);
        g_outgoing.emplace_back(proto::RelKind::EnemySpawn, std::move(msg));
        // Its current HP right behind (the load carries the HP it was loaded with).
        if (auto hp = g_last_hp.find(key); hp != g_last_hp.end()) {
            Bytes state(2);
            std::memcpy(state.data(), &kOpVaryHPAck, 2);
            Bytes body = hp->second;
            translate(body, key, copy);
            if (map_message_locked(kOpVaryHPAck, body, copy)) {
                state.insert(state.end(), body.begin(), body.end());
                g_outgoing.emplace_back(proto::RelKind::EnemyState, std::move(state));
            }
        }
    }
    std::string name;
    if (load.size() > 0x24) {
        uint32_t chars = 0;
        std::memcpy(&chars, load.data() + 0x20, 4);
        for (uint32_t i = 0; i < chars && i < 40 && 0x24 + 2 * i + 1 < load.size(); ++i) {
            const char c = static_cast<char>(load[0x24 + 2 * i]);
            name.push_back(c >= 0x20 && c < 0x7F ? c : '?');
        }
    }
    static const char* const kOrigin[] = {"no spawn id", "schedule id removed", "camp slot removed",
                                          "sequence id removed"};
    log::info("Enemy relay: sending enemy 0x{:08X} \"{}\" as copy 0x{:08X} ({}; {} B load, {} recent actions, {}; "
              "sanitized: {})",
              key, name, copy, why, n, action.size() / kStartActionReqSize,
              origin < 0 ? "T0 not found" : kOrigin[origin], cleaned);
}

void send_picked() {
    if (const auto key = pick_manual()) {
        send_enemy(*key, "picked");
    } else {
        log::info("Enemy relay: no enemy loaded within {:.0f} m", kPickRadius);
    }
}

// Caller holds g_mutex.
void remove_locked(uint32_t key) {
    auto it = g_sent.find(key);
    if (it == g_sent.end()) return;
    Bytes msg(4);
    std::memcpy(msg.data(), &it->second, 4);
    g_retired[it->second - kEnemyCopyBase] = GetTickCount64();
    g_outgoing.emplace_back(proto::RelKind::EnemyRemove, std::move(msg));
    g_sent.erase(it);
}

void remove_sent() {
    Bytes msg;
    {
        std::lock_guard lock(g_mutex);
        for (const auto& [key, copy] : g_sent) {
            const size_t at = msg.size();
            msg.resize(at + 4);
            std::memcpy(msg.data() + at, &copy, 4);
            g_retired[copy - kEnemyCopyBase] = GetTickCount64();
        }
        g_sent.clear();
        if (!msg.empty()) g_outgoing.emplace_back(proto::RelKind::EnemyRemove, msg);
    }
    log::info("Enemy relay: removing {} enemy copies", msg.size() / 4);
}

// Ctrl+F7: every enemy fighting us within kPickRadius gets a copy (up to
// kAutoMax); copies of those gone or far away are removed.
constexpr size_t kAutoMax = 8;
constexpr float kAutoDropRadius = 80.0f;
bool g_auto = false;
// A copy goes only after its enemy stayed away that long: during a finisher
// the PLAYER's own position reads wrong for a moment and every copy was
// dropped and sent again (they blinked; live test 2026-10-04).
constexpr uint64_t kFarForMs = 3000, kGoneForMs = 1500;
std::map<uint32_t, uint64_t> g_away_since;  // world NPC key -> first tick it looked far/gone
proto::Vec3 g_me_last{};
uint64_t g_me_last_ms = 0;

void auto_tick() {
    const auto me = my_position();
    if (!me) return;
    const uint64_t now = GetTickCount64();
    // Our own position jumped: not to be trusted this tick.
    const bool me_jumped = g_me_last_ms && now - g_me_last_ms < 2000 && distance(*me, g_me_last) > 30.0f;
    if (!me_jumped) {
        g_me_last = *me;
        g_me_last_ms = now;
    }
    std::map<uint32_t, uint32_t> sent;
    {
        std::lock_guard lock(g_mutex);
        sent = g_sent;
    }
    for (const auto& [key, copy] : sent) {
        const auto p = game::actor_world_position(key);
        const bool gone = !p, too_far = p && !me_jumped && distance(*p, *me) > kAutoDropRadius;
        if (!gone && !too_far) {
            g_away_since.erase(key);
            continue;
        }
        auto [it, first] = g_away_since.try_emplace(key, now);
        if (now - it->second < (gone ? kGoneForMs : kFarForMs)) continue;
        g_away_since.erase(it);
        std::lock_guard lock(g_mutex);
        remove_locked(key);
        log::info("Enemy relay: enemy 0x{:08X} {}; its copy 0x{:08X} removed", key, gone ? "is gone" : "is far",
                  copy);
    }
    size_t count = 0;
    {
        std::lock_guard lock(g_mutex);
        count = g_sent.size();
    }
    auto list = candidates();
    std::sort(list.begin(), list.end(), [](const Candidate& a, const Candidate& b) { return a.d < b.d; });
    for (const auto& c : list) {
        if (count >= kAutoMax || me_jumped) break;
        if (!c.engaged) continue;
        send_enemy(c.key, "fighting you");
        ++count;
    }
}

}  // namespace

uint32_t enemy_variant(uint32_t key) {
    std::lock_guard lock(g_mutex);
    auto l = g_loads.find(key);
    return l != g_loads.end() && l->second.size() >= 0x1C ? u32_at(l->second.data() + 0x18) : 0;
}

uint32_t enemy_copy_of(uint32_t key) {
    std::lock_guard lock(g_mutex);
    auto c = g_sent.find(key);
    return c != g_sent.end() ? c->second : 0;
}

uint32_t enemy_original_of(uint32_t copy) {
    std::lock_guard lock(g_mutex);
    for (const auto& [key, c] : g_sent) {
        if (c == copy) return key;
    }
    return 0;
}

void enemy_observe_s2c(const uint8_t* packet, size_t size) {
    if (size < 5) return;
    uint16_t op;
    std::memcpy(&op, packet, 2);
    const uint8_t* p = packet + 5;
    const size_t n = size - 5;
    if ((op == kOpVaryHPAck || op == kOpNotifyHandleDeadAck || op == kOpAttackTargetAck ||
         op == kOpApplyActionRestrictionAck) &&
        n >= 4) {
        const uint32_t key = u32_at(p);
        std::lock_guard lock(g_mutex);
        if (op == kOpAttackTargetAck && n >= 8) {
            const uint32_t attacker = u32_at(p + 4), own = capture_own_player_key();
            if (own && attacker == own && g_loads.count(key)) g_engaged[key] = GetTickCount64();
            if (own && key == own && g_loads.count(attacker)) g_engaged[attacker] = GetTickCount64();
        }
        if (op == kOpVaryHPAck && g_loads.count(key)) g_last_hp[key] = Bytes(p, p + n);
        auto sent = g_sent.find(key);
        if (sent == g_sent.end()) return;
        Bytes msg(2);
        std::memcpy(msg.data(), &op, 2);
        Bytes body(p, p + n);
        translate(body, key, sent->second);
        if (!map_message_locked(op, body, sent->second)) return;
        msg.insert(msg.end(), body.begin(), body.end());
        if (op == kOpVaryHPAck || op == kOpNotifyHandleDeadAck) {
            g_outgoing.emplace_back(proto::RelKind::EnemyState, std::move(msg));
        } else if (g_out_events.size() < 512) {
            g_out_events.push_back({proto::EventKind::EnemyAct, std::move(msg)});
        }
        return;
    }
    if (op == kOpActorLoadDataAck && n >= 7) {
        std::lock_guard lock(g_mutex);
        if (g_known.size() > 20000) g_known.clear();
        for (size_t at = 0; at + 7 <= n;) {
            uint16_t rl;
            std::memcpy(&rl, p + at, 2);
            if (rl < 5 || at + 2 + rl > n) break;
            const uint32_t k = u32_at(p + at + 3);
            if (!is_network_key(k)) g_known[k] = p[at + 2];
            at += 2 + static_cast<size_t>(rl);
        }
    }
    if (op == kOpActorLoadDataAck && n >= 0x40) {
        // One record: u16 len | u8 type | u32 key | u32 owner | ...
        uint16_t len;
        std::memcpy(&len, p, 2);
        const uint8_t type = p[2];
        const uint32_t key = u32_at(p + 3), owner = u32_at(p + 7);
        if (static_cast<size_t>(len) + 2 != n || (type != 3 && type != 6) || !keys::is_game_world_key(key) ||
            owner != 0) {
            return;
        }
        std::lock_guard lock(g_mutex);
        if (g_loads.size() > 1500) g_loads.clear(), g_actions.clear(), g_engaged.clear();
        g_loads[key] = Bytes(p, p + n);
    } else if (op == kOpRemoveActorAck && n >= 10) {
        // 04 | u32 | u8 reason | keys...
        std::lock_guard lock(g_mutex);
        for (size_t i = 6; i + 4 <= n; i += 4) {
            const uint32_t key = u32_at(p + i);
            g_loads.erase(key);
            g_actions.erase(key);
            g_engaged.erase(key);
            g_last_hp.erase(key);
            g_known.erase(key);
            g_refused.erase(key);
            remove_locked(key);
        }
    }
}

void enemy_observe_c2s(const uint8_t* packet, size_t size) {
    if (size < 5 + 8) return;
    uint16_t op;
    std::memcpy(&op, packet, 2);
    if (op == kOpChangeBattleStateReq && size == 5 + 5) {
        const uint32_t actor = u32_at(packet + 5);
        std::lock_guard lock(g_mutex);
        if (packet[9] != 0 && g_loads.count(actor)) g_engaged[actor] = GetTickCount64();
        return;
    }
    if (op == kOpDoFrameEventReq && size >= 5 + 0x203 + 665 && packet[5] == 0x04) {
        const uint32_t attacker = u32_at(packet + 6), own = capture_own_player_key();
        const uint8_t* p = packet + 5;
        const size_t n = size - 5;
        bool at_us = false;
        for (size_t k = 1; k <= 30 && n >= k * 665 + 0x203; ++k) {
            const uint32_t t = u32_at(p + n - k * 665);
            if (!keys::is_actor_key(t)) break;
            at_us = at_us || (own && t == own) || t == kPuppetPlayer;
        }
        if (at_us) {
            std::lock_guard lock(g_mutex);
            if (g_loads.count(attacker)) g_engaged[attacker] = GetTickCount64();
        }
        return;
    }
    const bool action = op == kOpStartActionReq && size == 5 + kStartActionReqSize;
    const bool rotate = op == kOpRotateActorReq && size == 5 + kRotateActorReqSize;
    if (!action && !rotate) return;
    const uint32_t key = u32_at(packet + 5);
    std::lock_guard lock(g_mutex);
    // Copied already: relay it as it happens.
    if (auto sent = g_sent.find(key); sent != g_sent.end()) {
        Bytes ev(2);
        std::memcpy(ev.data(), &op, 2);
        ev.insert(ev.end(), packet + 5, packet + size);
        Bytes body(ev.begin() + 2, ev.end());
        translate(body, key, sent->second);
        if (map_message_locked(op, body, sent->second)) {
            std::copy(body.begin(), body.end(), ev.begin() + 2);
            if (g_out_events.size() < 512) g_out_events.push_back({proto::EventKind::EnemyAct, std::move(ev)});
        }
    }
    if (!action || !g_loads.count(key)) return;
    auto& list = g_actions[key];
    list.emplace_back(packet + 5, packet + size);
    while (list.size() > kKeptActions) list.pop_front();
}

// Test aid (copias-grupo-invisibles.md §6.8): every 2 s, our real type-3
// actors within 60 m whose Status+0x390 has bit 2 (the flag that hid the
// originals: model alpha 0) are logged when they get or lose it.
std::map<uint32_t, uint32_t> g_hidden_seen;  // key -> its +0x390 when logged

void watch_hidden() {
    const auto me = my_position();
    if (!me) return;
    std::vector<uint32_t> keys;
    std::set<uint32_t> with_copy;
    {
        std::lock_guard lock(g_mutex);
        for (const auto& [key, load] : g_loads) {
            if (load.size() > 3 && load[2] == 3) keys.push_back(key);
        }
        for (const auto& [key, copy] : g_sent) with_copy.insert(key);
    }
    int hidden_now = 0;
    for (uint32_t key : keys) {
        const auto a = game::find_actor(key);
        const auto pos = game::actor_world_position(key);
        if (!a || !pos || distance(*pos, *me) > 60.0f) continue;
        Seen f;
        if (!read_seen(*a, f)) continue;
        const bool hidden = (f.s390 & 0x2) != 0;
        hidden_now += hidden;
        auto it = g_hidden_seen.find(key);
        const bool was = it != g_hidden_seen.end() && (it->second & 0x2);
        if (hidden != was) {
            log::info("Enemy relay: REAL enemy 0x{:08X} {} the hidden flag (Status+0x390 0x{:X}, +0x394 0x{:X}, "
                      "state +5E 0x{:X}){}",
                      key, hidden ? "GOT" : "lost", f.s390, f.s394, f.state,
                      with_copy.count(key) ? ", it has a copy" : "");
        }
        g_hidden_seen[key] = f.s390;
    }
    static int last_count = -1;
    if (hidden_now != last_count) {
        log::info("Enemy relay: real enemies within 60 m with the hidden flag: {}", hidden_now);
        last_count = hidden_now;
    }
}

void enemy_test_loop(void* stop) {
    log::info("Enemy relay: Ctrl+F6 = copy the enemy you hit / the nearest, Ctrl+Shift+F6 = remove the copies, "
              "Ctrl+F7 = automatic (every enemy fighting you)");
    bool was6 = false, was7 = false;
    uint64_t last_auto = 0, last_watch = 0;
    while (WaitForSingleObject(static_cast<HANDLE>(stop), 50) == WAIT_TIMEOUT) {
        const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool f6 = ctrl && (GetAsyncKeyState(VK_F6) & 0x8000);
        const bool f7 = ctrl && (GetAsyncKeyState(VK_F7) & 0x8000);
        if (f6 && !was6) {
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) remove_sent();
            else send_picked();
        }
        if (f7 && !was7) {
            g_auto = !g_auto;
            log::info("Enemy relay: automatic copies {}", g_auto ? "ON" : "OFF");
            if (!g_auto) remove_sent();
        }
        was6 = f6;
        was7 = f7;
        const uint64_t now = GetTickCount64();
        if (g_auto && now - last_auto >= 500) {
            last_auto = now;
            auto_tick();
        }
        if (now - last_watch >= 2000) {
            last_watch = now;
            watch_hidden();
        }
    }
}

std::optional<std::pair<proto::RelKind, std::vector<uint8_t>>> enemy_take_outgoing() {
    std::lock_guard lock(g_mutex);
    if (g_outgoing.empty()) return std::nullopt;
    auto m = std::move(g_outgoing.front());
    g_outgoing.pop_front();
    return m;
}

std::vector<proto::GameEvent> enemy_host_tick() {
    std::vector<proto::GameEvent> out;
    std::map<uint32_t, uint32_t> sent;
    {
        std::lock_guard lock(g_mutex);
        while (!g_out_events.empty()) {
            out.push_back(std::move(g_out_events.front()));
            g_out_events.pop_front();
        }
        sent = g_sent;
    }
    const uint64_t now = GetTickCount64();
    if (sent.empty() || now - g_last_sample_ms < kPosIntervalMs) return out;
    g_last_sample_ms = now;
    const uint64_t ts = capture_game_clock_ms();
    if (!ts) return out;
    for (const auto& [key, copy] : sent) {
        const auto pos = game::actor_world_position(key);
        const auto rot = game::actor_world_rotation(key);
        if (!pos || !rot) continue;
        Sampled& last = g_sampled[key];
        const float dx = pos->x - last.pos.x, dy = pos->y - last.pos.y, dz = pos->z - last.pos.z;
        // A finisher made the copy vanish for a moment (live test): the
        // enemy is attached to the player then and its transform seems to
        // turn relative to him. Skip impossible jumps (the copy holds still)
        // and log the raw numbers to confirm.
        const float dt = std::max<float>(static_cast<float>(now - last.ms), 66.0f) / 1000.0f;
        if (last.has && dx * dx + dy * dy + dz * dz > (kMaxSpeed * dt) * (kMaxSpeed * dt) + 4.0f) {
            if (last.jumps_logged++ < 12) {
                const auto me = my_position();
                log::info("Enemy relay: enemy 0x{:08X} read at ({:.1f}, {:.1f}, {:.1f}), {:.1f} m from its last "
                          "sample in {:.2f} s; skipped | you at ({:.1f}, {:.1f}, {:.1f}) | its last action's "
                          "parent 0x{:08X}",
                          key, pos->x, pos->y, pos->z, std::sqrt(dx * dx + dy * dy + dz * dz), dt,
                          me ? me->x : NAN, me ? me->y : NAN, me ? me->z : NAN, last_action_parent(key));
            }
            continue;
        }
        const bool moved = dx * dx + dy * dy + dz * dz > 0.05f * 0.05f;
        if (!moved && now - last.ms < kPosKeepAliveMs) continue;
        proto::Vec3 p = *pos;
        if (!moved) {
            // Standing still: the remote buffer still wants a sample every
            // second, and an identical one is ignored. 1 mm up or down.
            last.flip = !last.flip;
            p.y += last.flip ? 0.001f : -0.001f;
        } else {
            last.pos = *pos;
        }
        last.has = true;
        last.ms = now;
        Bytes ev(proto::kEnemyPosEventSize);
        const float f[7] = {p.x, p.y, p.z, rot->x, rot->y, rot->z, rot->w};
        std::memcpy(ev.data(), &copy, 4);
        std::memcpy(ev.data() + 4, &ts, 8);
        std::memcpy(ev.data() + 12, f, sizeof f);
        out.push_back({proto::EventKind::EnemyPos, std::move(ev)});
    }
    return out;
}

void enemy_on_event(const proto::GameEvent& ev, int64_t ts_shift_ms, uint64_t delay_ms) {
    if (ev.payload.size() < 4) return;
    if (ev.kind == proto::EventKind::EnemyPos) {
        if (ev.payload.size() != proto::kEnemyPosEventSize) return;
        const uint32_t copy = u32_at(ev.payload.data());
        if (!g_copies.count(copy)) return;
        uint64_t ts = 0;
        float f[7];
        std::memcpy(&ts, ev.payload.data() + 4, 8);
        std::memcpy(f, ev.payload.data() + 12, sizeof f);
        puppet_enemy_move(copy, ts, {f[0], f[1], f[2]}, {f[3], f[4], f[5], f[6]}, ts_shift_ms, delay_ms);
    } else if (ev.kind == proto::EventKind::EnemyAct) {
        uint16_t op = 0;
        std::memcpy(&op, ev.payload.data(), 2);
        Bytes req(ev.payload.begin() + 2, ev.payload.end());
        if (!valid_size(op, req.size()) || !g_copies.count(u32_at(req.data())) || !untranslate(req)) return;
        if (op == kOpAttackTargetAck || op == kOpApplyActionRestrictionAck) {
            puppet_inject_s2c(op, req, delay_ms);  // hit reaction, in step with its actions
        } else {
            puppet_enemy_action(op, req, ts_shift_ms, delay_ms);
        }
    }
}

void enemy_set_receive_enabled(bool on) { g_receive_enabled = on; }

void enemy_on_spawn(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 8) return;
    if (!g_receive_enabled) {
        static bool told = false;
        if (!told) log::warn("Enemy relay: the other player's enemy copies are refused (no crash guards)");
        told = true;
        return;
    }
    const uint32_t copy = u32_at(bytes.data());
    const uint32_t n = u32_at(bytes.data() + 4);
    if (!is_enemy_copy_key(copy) || n < 0x40 || 8 + static_cast<size_t>(n) > bytes.size()) {
        log::warn("Enemy relay: malformed enemy copy ({} bytes) ignored", bytes.size());
        return;
    }
    Bytes load(bytes.begin() + 8, bytes.begin() + 8 + n);
    Bytes action(bytes.begin() + 8 + n, bytes.end());
    if (action.size() % kStartActionReqSize != 0 || !untranslate(load) || !untranslate(action)) {
        log::warn("Enemy relay: copy 0x{:08X} not loaded: {}", copy,
                  action.size() % kStartActionReqSize ? "malformed actions" : "our own player is not known yet");
        return;
    }
    // Our own barrier against a sender without the sanitiser: a copy may not
    // hang from anything nor name any actor that exists in OUR world.
    const auto tail = parse_tail(load);
    if (!tail || tail->parent || !tail->children.empty() || !tail->hierarchy.empty()) {
        log::warn("Enemy relay: copy 0x{:08X} refused: it has a parent or children", copy);
        return;
    }
    for (size_t i = 5; i + 4 <= load.size(); ++i) {
        const uint32_t k = u32_at(load.data() + i);
        // Our own player is named on purpose (it was the sender's kSelfMarker).
        if (!keys::is_actor_key(k) || k == copy || is_network_key(k) || k == capture_own_player_key()) continue;
        if (game::find_actor(k)) {
            log::warn("Enemy relay: copy 0x{:08X} refused: it names our actor 0x{:08X} at +{}", copy, k, i);
            return;
        }
    }
    if (g_copies.count(copy)) puppet_remove_actor(copy);
    if (puppet_spawn_enemy(copy, std::move(load), std::move(action))) {
        g_copies[copy] = true;
        g_report_due[copy] = now_ms() + 2000;
    }
}

void enemy_tick() {
    const uint64_t t = now_ms();
    for (auto it = g_report_due.begin(); it != g_report_due.end();) {
        if (t >= it->second) {
            report(it->first);
            it = g_report_due.erase(it);
        } else {
            ++it;
        }
    }
}

void enemy_on_state(const std::vector<uint8_t>& bytes, uint64_t delay_ms) {
    if (bytes.size() < 6) return;
    uint16_t op = 0;
    std::memcpy(&op, bytes.data(), 2);
    if (op != kOpVaryHPAck && op != kOpNotifyHandleDeadAck) return;
    Bytes payload(bytes.begin() + 2, bytes.end());
    if (!valid_size(op, payload.size())) return;
    const uint32_t copy = u32_at(payload.data());
    if (!g_copies.count(copy) || !untranslate(payload)) return;
    puppet_inject_s2c(op, payload, delay_ms);  // in step with its actions
    if (op == kOpNotifyHandleDeadAck) log::info("Enemy relay: enemy copy 0x{:08X} died", copy);
}

void enemy_reset_partner() {
    size_t sent = 0, copies = g_copies.size();
    {
        std::lock_guard lock(g_mutex);
        sent = g_sent.size();
        for (const auto& [key, copy] : g_sent) g_retired[copy - kEnemyCopyBase] = GetTickCount64();
        g_sent.clear();
        g_outgoing.clear();
        g_out_events.clear();
    }
    g_sampled.clear();
    for (const auto& [copy, loaded] : g_copies) puppet_remove_actor(copy);
    g_copies.clear();
    g_report_due.clear();
    if (sent || copies) {
        log::info("Enemy relay: other side changed: {} enemies to copy again, {} copies of theirs removed", sent,
                  copies);
    }
}

void enemy_on_remove(const std::vector<uint8_t>& bytes) {
    for (size_t i = 0; i + 4 <= bytes.size(); i += 4) {
        const uint32_t copy = u32_at(bytes.data() + i);
        if (!g_copies.erase(copy)) continue;
        puppet_remove_actor(copy);
        log::info("Enemy relay: enemy copy 0x{:08X} removed", copy);
    }
}

}  // namespace cdmp::net
