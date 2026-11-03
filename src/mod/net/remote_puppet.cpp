#include "mod/net/remote_puppet.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <map>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>

#include <safetyhook.hpp>

#include "common/equip_state.h"
#include "mod/game/actor_lookup.h"
#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/net/enemy_relay.h"
#include "mod/net/horse_diag.h"
#include "mod/net/keys.h"
#include "mod/session2/guest_actor_diag_logic.h"

namespace cdmp::net {
namespace {
namespace off = game::build_2976;
using Bytes = std::vector<uint8_t>;

// Fresh keys for the puppet. Real serials seen so far are below 0x1000, so
// 0xF001 will not collide with anything the server allocates in a session.
constexpr uint32_t kPuppetUser = 0x9010F001;
constexpr uint32_t kPuppetPlayer = 0xA010F001;
constexpr uint32_t kPuppetBody = 0xB010F001;
// The sender's horse (caballo-coop.md §2.3) and the extra member of the
// sender's party list inside the horse packet.
constexpr uint32_t kPuppetHorse = 0xB010F002;
constexpr uint32_t kPuppetHorseParty = 0xB010F003;

// The character number in the player's load packets (user +0x17/+0x43, body
// +0x1839/+0x1865). The client keeps ONE equipment-list cache keyed by it and
// applies cached lists to the first actor with that number: a copy sharing the
// local player's number mixed up both players' equipment (live test 1,
// apariencia-estado-coop.md §5). Give the copy its own.
constexpr uint64_t kSenderCharacterNo = 0x7FFFFFFFFFFFFFFF;
constexpr uint64_t kPuppetCharacterNo = 0x7FFFFFFFFFFFF001;

constexpr size_t kCharacterNoAt[] = {0x17, 0x43};

bool set_character_no(Bytes& user, Bytes& body) {
    if (user.size() < 0x60 || body.size() < 0x60) return false;
    auto patch = [](Bytes& v, size_t base) {
        for (size_t at : kCharacterNoAt) {
            if (base + at + 8 > v.size()) return false;
            uint64_t x;
            std::memcpy(&x, v.data() + base + at, 8);
            if (x != kSenderCharacterNo) return false;
        }
        for (size_t at : kCharacterNoAt) std::memcpy(v.data() + base + at, &kPuppetCharacterNo, 8);
        return true;
    };
    // The body's copy of the user record starts with the same 0x17 header bytes.
    size_t base = std::string::npos;
    for (size_t i = 1; i + 0x17 <= body.size(); ++i) {
        if (std::memcmp(body.data() + i, user.data(), 0x17) == 0) {
            base = i;
            break;
        }
    }
    if (base == std::string::npos) return false;
    Bytes u = user, b = body;
    if (!patch(u, 0) || !patch(b, base)) return false;
    user = std::move(u);
    body = std::move(b);
    return true;
}

bool is_actor_key(uint32_t v) { return keys::is_actor_key(v); }
bool is_puppet_key(uint32_t v) {
    return v == kPuppetUser || v == kPuppetPlayer || v == kPuppetBody || v == kPuppetHorse ||
           v == kPuppetHorseParty;
}
// True if a key of some other actor (of the sender's world) is left anywhere.
bool has_foreign_key(const Bytes& v) {
    for (size_t i = 0; i + 4 <= v.size(); ++i) {
        uint32_t x;
        std::memcpy(&x, v.data() + i, 4);
        if (is_actor_key(x) && !is_puppet_key(x)) return true;
    }
    return false;
}

// Sideways offset (world X, metres) of the puppet relative to the sender.
// 0 = the puppet walks exactly the sender's path; then climbing etc. lines up
// with the real geometry.
std::atomic<float> g_offset_x{0.0f};

uint64_t steady_ms() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

// Replaces every (unaligned) occurrence; packets are packed, keys can sit anywhere.
int replace_u32(Bytes& v, uint32_t from, uint32_t to) {
    int n = 0;
    for (size_t i = 0; i + 4 <= v.size(); ++i) {
        if (std::memcmp(v.data() + i, &from, 4) == 0) {
            std::memcpy(v.data() + i, &to, 4);
            ++n;
            i += 3;
        }
    }
    return n;
}

uint32_t u32_at(const Bytes& v, size_t off) {
    uint32_t x = 0;
    std::memcpy(&x, v.data() + off, sizeof x);
    return x;
}

Bytes make_packet(uint16_t op, const Bytes& payload) {
    Bytes p(5 + payload.size());
    const uint16_t len = static_cast<uint16_t>(payload.size());
    std::memcpy(p.data(), &op, 2);
    p[2] = 0xFF;  // slot -1, as in every captured load/remove packet
    std::memcpy(p.data() + 3, &len, 2);
    std::memcpy(p.data() + 5, payload.data(), payload.size());
    return p;
}

// ---------------------------------------------------------------- spawn state
std::mutex g_mutex;  // spawn state (never taken on game threads)
bool g_spawned = false;
std::atomic<uint32_t> g_src_player_key{0};  // the sender's own player key
std::atomic<uint32_t> g_src_horse_key{0};   // the sender's horse key (0 = no horse)
std::atomic<uint32_t> g_src_user_key{0};    // the sender's user key
std::atomic<uint32_t> g_sender_horse{0};    // the sender's horse key, even when not loaded here
bool g_horse_spawned = false;   // a horse copy belongs to this puppet (pending or loaded)
// The horse's load waits for the sender horse's first StartAction: a copy
// with no action running crashed the game (caballo-coop.md §9.2). Both are
// then queued together, the action right behind the load.
Bytes g_horse_pending;           // its load packet, until then (under g_mutex)
// Dismount: the copy has no real rider<->horse link, so RideOff::Enter fails
// and the rider freezes mid-animation (live test 2026-10-03). Only the rider
// is reloaded; the horse copy stays in the world (under g_mutex).
bool g_keep_horse = false;       // the next removal keeps the horse
bool g_horse_kept = false;       // a horse copy is in the world without its rider
uint64_t g_horse_not_before = 0; // and not before this (player packets first)
std::atomic<bool> g_horse_enabled{false};
std::atomic<bool> g_equip_enabled{true};
// Serial of the equipment messages we inject. The client applies an
// OtherEquipItemAck only if its serial is above the copy's last one
// (comp+0x18), and a reloaded copy may carry the serial of an earlier
// injection through the equipment cache, so ours only grow, and never stay
// below the sender's (ropa-al-desmontar.md section 5.3).
std::mutex g_equip_mutex;
uint32_t g_equip_serial_out = 0;               // under g_equip_mutex
std::atomic<uint64_t> g_equip_check_at{0};    // when to log the copy's gear (diagnostic)

uint32_t next_equip_serial(uint32_t sender_serial) {
    std::lock_guard lock(g_equip_mutex);
    g_equip_serial_out = std::max(g_equip_serial_out + 1, sender_serial);
    return g_equip_serial_out;
}

// OtherUnequipItemAck: u32 key | u16 slot | u32 serial (no fragment byte).
Bytes unequip_payload(uint32_t key, uint16_t slot, uint32_t serial) {
    Bytes un(10);
    std::memcpy(un.data(), &key, 4);
    std::memcpy(un.data() + 4, &slot, 2);
    std::memcpy(un.data() + 6, &serial, 4);
    return un;
}

// ---------------------------------------------------------------- injection queue
struct Pending {
    uint64_t due_ms;  // not delivered before this time
    bool movement;    // purged when the puppet goes away
    Bytes packet;
};

SRWLOCK g_queue_lock = SRWLOCK_INIT;  // the NetworkThread only ever try-acquires
std::deque<Pending> g_queue;          // ordered by due time
constexpr uint64_t kStaleMovementMs = 2000;
uint64_t g_stale_dropped = 0;  // under g_queue_lock
// The last position we told the copy to be at (a delivered MoveActorAck) and
// when, under g_queue_lock: puppet_tick reloads a copy that stopped following
// them (live test, 4 Oct 22:08: after dying twice through D1/D2 and five
// minutes dead, the copy played its actions in place and ignored its moves).
float g_told_pos[3] = {};
uint64_t g_told_ms = 0;

// Inserts keeping the queue sorted by due time (gap-fill samples are created
// after later packets were already queued). Stable for equal due times.
void enqueue_at(Bytes packet, uint64_t due_ms, bool movement) {
    AcquireSRWLockExclusive(&g_queue_lock);
    auto it = std::upper_bound(g_queue.begin(), g_queue.end(), due_ms,
                               [](uint64_t due, const Pending& p) { return due < p.due_ms; });
    g_queue.insert(it, {due_ms, movement, std::move(packet)});
    ReleaseSRWLockExclusive(&g_queue_lock);
}

void purge_movement() {
    AcquireSRWLockExclusive(&g_queue_lock);
    std::erase_if(g_queue, [](const Pending& p) { return p.movement; });
    ReleaseSRWLockExclusive(&g_queue_lock);
}

// ---------------------------------------------------------------- Req -> Ack recipes
// From docs/research/mirror-ack-formats.md (disassembly of both handlers; both
// sides share the struct deserializers, only the outer wrapper differs). The
// Ack handler drops a packet silently unless it consumes exactly payload_len
// bytes, so sizes must be exact.

Bytes slice(const Bytes& v, size_t from, size_t to) {
    return Bytes(v.begin() + static_cast<ptrdiff_t>(from), v.begin() + static_cast<ptrdiff_t>(to));
}
void append(Bytes& out, const Bytes& part) { out.insert(out.end(), part.begin(), part.end()); }

// Parent/mount key: when the sender rides, it points at the sender's horse,
// which the puppet must not try to mount. All-FF = no parent.
// Riding the sender's horse is kept: its key was translated to our copy of
// the horse, which exists here.
void clear_parent(Bytes& ack, size_t off) {
    if (off + 8 > ack.size()) return;
    uint32_t parent = 0;
    std::memcpy(&parent, ack.data() + off, 4);
    if (parent == kPuppetHorse) return;
    std::memset(ack.data() + off, 0xFF, 8);
}

void shift_x(Bytes& ack, size_t off) {
    if (off + 4 > ack.size()) return;
    float x;
    std::memcpy(&x, ack.data() + off, 4);
    if (std::isfinite(x)) {
        x += g_offset_x;
        std::memcpy(ack.data() + off, &x, 4);
    }
}

// Sender's latest (unshifted) world position, from its Moves. Used to
// recognise which optional vec3 fields in other messages are positions near
// the sender (and therefore must be shifted too).
float g_ref_pos[3] = {NAN, NAN, NAN};

// Shifts X of the vec3 at `off` only if it lies within 30 m of the sender:
// those fields are not always positions (RotateActor 0x14/0x5C held one in
// only 6-15% of captured packets).
void shift_x_if_near_sender(Bytes& ack, size_t off) {
    if (off + 12 > ack.size() || !std::isfinite(g_ref_pos[0])) return;
    float v[3];
    std::memcpy(v, ack.data() + off, sizeof v);
    const float dx = v[0] - g_ref_pos[0], dy = v[1] - g_ref_pos[1], dz = v[2] - g_ref_pos[2];
    if (dx * dx + dy * dy + dz * dz < 30.0f * 30.0f) shift_x(ack, off);
}

// The sender's clock (ms) drives the receiver's interpolation. Events reach
// us late (network, playout delay) and on another game's clock, so move every
// timestamp onto our clock plus the delay; otherwise the client sees stale
// samples and snaps ("lag teleports").
int64_t g_ts_shift = 0;  // for the event being processed (event thread only)

void rebase_timestamp(Bytes& ack, size_t off) {
    if (off + 8 > ack.size()) return;
    uint64_t ts;
    std::memcpy(&ts, ack.data() + off, 8);
    ts = static_cast<uint64_t>(static_cast<int64_t>(ts) + g_ts_shift);
    std::memcpy(ack.data() + off, &ts, 8);
}

// MoveActor: 514 -> 235. Ack = req[0:0xDE] + u8 0 ("also send to controller",
// read and ignored by the client) + req[0xDE:0xEA]; the Req's trailing
// 10 x 28-byte history array is not part of the Ack.
std::optional<Bytes> move_ack(const Bytes& req) {
    if (req.size() != 514) return std::nullopt;
    std::memcpy(g_ref_pos, req.data() + 0x04, sizeof g_ref_pos);
    Bytes ack = slice(req, 0, 0xDE);
    ack.push_back(0);
    append(ack, slice(req, 0xDE, 0xEA));
    shift_x(ack, 0x04);
    shift_x(ack, 0x4C);
    clear_parent(ack, 0x8C);
    rebase_timestamp(ack, 0xD5);  // advances 25 ms per packet in captura-2
    return ack;
}

// StartAction: 725 -> 725, same bytes but the Req's last u8 (a "force" flag)
// moves to 0x8E, before the big block.
std::optional<Bytes> start_action_ack(const Bytes& req) {
    if (req.size() != 725) return std::nullopt;
    Bytes ack = slice(req, 0, 0x8E);
    ack.push_back(req[0x2D4]);
    append(ack, slice(req, 0x8E, 0x2D4));
    clear_parent(ack, 0x117);
    // Action target (Req +0x16C, Ack +0x16D): 0 almost always; when it names
    // another actor (an enemy, a grabbed animal) it is one of the SENDER's
    // world, which here may be a different actor (interacciones-mundo.md R3).
    if (ack.size() >= 0x171) {
        uint32_t target;
        std::memcpy(&target, ack.data() + 0x16D, 4);
        if (is_actor_key(target) && !is_puppet_key(target)) std::memset(ack.data() + 0x16D, 0, 4);
    }
    // World positions found in captura-2 at Req 0x8E (393/400 packets) and
    // 0xD6 (247/400); they sit one byte later in the Ack. Unshifted, they
    // snapped the puppet onto the sender every ~0.5 s.
    shift_x_if_near_sender(ack, 0x8F);
    shift_x_if_near_sender(ack, 0xD7);
    // StartAction feeds the same position buffer as MoveActor; its u64
    // timestamp (Ack+0x160) needs the same rebase (movimiento-remoto-cliente.md).
    rebase_timestamp(ack, 0x160);
    return ack;
}

// RotateActor: 176 -> 177. Ack = req[0:0xA4] + u8 0 + req[0xA4:0xB0].
std::optional<Bytes> rotate_ack(const Bytes& req) {
    if (req.size() != 176) return std::nullopt;
    Bytes ack = slice(req, 0, 0xA4);
    ack.push_back(0);
    append(ack, slice(req, 0xA4, 0xB0));
    clear_parent(ack, 0x54);
    rebase_timestamp(ack, 0x9C);
    shift_x_if_near_sender(ack, 0x14);
    shift_x_if_near_sender(ack, 0x5C);
    return ack;
}

// UpdateWallUpData: Ack = Req (132 bytes, no extra byte). Points (not
// directions) follow the puppet's sideways offset.
std::optional<Bytes> wallup_ack(const Bytes& req) {
    if (req.size() != 132) return std::nullopt;
    Bytes ack = req;
    for (size_t at : {size_t{0x09}, size_t{0x15}, size_t{0x21}, size_t{0x2D}, size_t{0x39},
                      size_t{0x45}, size_t{0x5C}}) {
        shift_x(ack, at);
    }
    return ack;
}

// ---------------------------------------------------------------- event-thread state
std::atomic<uint64_t> g_events{0};
// Riding state of the sender, from Path.parent (+0x8C) of its Moves.
bool g_sender_riding = false;
int g_unmounted_moves = 0;
std::atomic<bool> g_reload_request{false};
uint64_t g_relays_dropped = 0;
Bytes g_last_move;              // last Move (Req format, puppet key)
uint64_t g_last_move_ms = 0;    // steady_ms when it was processed
uint64_t g_last_move_ts = 0;    // its timestamp (sender clock)
float g_last_move_pos[3] = {NAN, NAN, NAN};
constexpr uint64_t kMoveSilenceMs = 150;  // real MoveActorReqs come every ~25 ms

// Gap filling. During a ledge climb-up / vault the sender sends no Move for
// 0.3-1.5 s and even its physics position is frozen; the first Move after the
// gap jumps 1-3 m. The puppet's buffer resets after 1 s without useful samples
// and then covers the jump in ~200 ms (a visible pop). Because events are
// delayed, we can still insert the missing samples behind the jump before
// they are due: a path that goes up first and then forward (forward then
// down when dropping). Fully smooth only if the delay exceeds the gap.
constexpr uint64_t kGapMinMs = 150;
constexpr uint64_t kGapMaxMs = 3000;
constexpr float kGapMinJump = 0.5f;   // metres
constexpr float kGapMaxJump = 15.0f;  // beyond this it's a real teleport (fast travel)
constexpr uint64_t kGapStepMs = 50;
uint64_t g_gap_fills = 0;

struct RecentGap {
    bool valid;
    uint64_t detected_ms;  // steady_ms when the resuming Move was seen
    float start[3];        // frozen position
    float end[3];          // position after the climb-up
};
RecentGap g_recent_gap{};

void fill_gap_before(const Bytes& next, uint64_t delay) {
    if (g_last_move.empty()) return;
    uint64_t ts_next = 0;
    std::memcpy(&ts_next, next.data() + 0xD5, 8);
    if (ts_next <= g_last_move_ts) return;
    const uint64_t gap = ts_next - g_last_move_ts;
    if (gap < kGapMinMs || gap > kGapMaxMs) return;
    float a[3], b[3];
    std::memcpy(a, g_last_move.data() + 0x04, sizeof a);
    std::memcpy(b, next.data() + 0x04, sizeof b);
    const float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < kGapMinJump || dist > kGapMaxJump) return;

    const bool rising = dy >= 0;
    // Point of the climb path at fraction f (0 = before the gap, 1 = after).
    // Rising: vertical in the first 60 %, then horizontal. Falling: the reverse.
    auto path_at = [&](float f, float out[3]) {
        const float split = 0.6f;
        float fv, fh;
        if (rising) {
            fv = std::min(f / split, 1.0f);
            fh = f <= split ? 0.0f : (f - split) / (1.0f - split);
        } else {
            fh = std::min(f / split, 1.0f);
            fv = f <= split ? 0.0f : (f - split) / (1.0f - split);
        }
        out[0] = a[0] + dx * fh;
        out[1] = a[1] + dy * fv;
        out[2] = a[2] + dz * fh;
    };

    // StartActions sent during (and right at the end of) the freeze carry the
    // frozen START position and feed the same position buffer as Moves, so they
    // dragged the puppet back to the foot of the ledge after our samples lifted
    // it (captura 134008). They are still in the delay queue: move their
    // positions onto the path at their own timestamp.
    const int64_t shift = g_ts_shift;
    const uint64_t win_lo = static_cast<uint64_t>(static_cast<int64_t>(g_last_move_ts) + shift);
    // Up to 300 ms past the gap: StartActions right after the resuming Move
    // can still carry the frozen start position; those go to the top.
    const uint64_t win_hi = static_cast<uint64_t>(static_cast<int64_t>(ts_next) + shift) + 300;
    int patched = 0;
    AcquireSRWLockExclusive(&g_queue_lock);
    for (Pending& item : g_queue) {
        Bytes& pk = item.packet;
        if (!item.movement || pk.size() != 5 + 725) continue;
        uint16_t op;
        std::memcpy(&op, pk.data(), 2);
        if (op != off::kOpStartActionAck) continue;
        uint64_t ts;
        std::memcpy(&ts, pk.data() + 5 + 0x160, 8);
        if (ts < win_lo || ts > win_hi) continue;
        float p[3];
        path_at(std::min(1.0f, static_cast<float>(ts - win_lo) / static_cast<float>(gap)), p);
        p[0] += g_offset_x;
        for (size_t at : {size_t{0x8F}, size_t{0xD7}}) {
            float v[3];
            std::memcpy(v, pk.data() + 5 + at, sizeof v);
            const float ex = v[0] - (a[0] + g_offset_x), ey = v[1] - a[1], ez = v[2] - a[2];
            if (ex * ex + ey * ey + ez * ez < 4.0f) {  // only the frozen start position
                std::memcpy(pk.data() + 5 + at, p, sizeof p);
                ++patched;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_queue_lock);

    // StartActions arriving shortly AFTER the resuming Move (not queued yet)
    // get fixed on arrival, see puppet_event.
    g_recent_gap = {true, steady_ms(), {a[0], a[1], a[2]}, {b[0], b[1], b[2]}};

    const uint64_t steps = gap / kGapStepMs;
    for (uint64_t k = 1; k < steps; ++k) {
        const float f = static_cast<float>(k) / static_cast<float>(steps);
        float p[3];
        path_at(f, p);
        Bytes req = g_last_move;
        std::memcpy(req.data() + 0x04, p, sizeof p);
        std::memcpy(req.data() + 0x4C, p, sizeof p);
        const uint64_t ts = g_last_move_ts + k * kGapStepMs;
        std::memcpy(req.data() + 0xD5, &ts, 8);
        auto ack = move_ack(req);
        if (!ack) return;
        // Due when the original sample would have been: received at the time
        // matching its timestamp, plus the delay.
        enqueue_at(make_packet(off::kOpMoveActorAck, *ack), g_last_move_ms + k * kGapStepMs + delay,
                   /*movement=*/true);
    }
    if (g_gap_fills++ < 200) {  // rare events: log them all
        log::info("Puppet: filled a {} ms gap ({:.2f} m, {}) with {} samples, re-pathed {} "
                  "queued StartAction positions [{} gaps so far]",
                  gap, dist, rising ? "up" : "down", steps - 1, patched, g_gap_fills);
    }
}

// ---------------------------------------------------------------- free wall climb
// Free wall climb (movement method 18, PointClimb). Each StartAction stores
// the method's Enter() result at method+0x44, and while it is 0 the per-frame
// code skips both the network interpolation and the SetPosition call. Enter
// needs a climb point that only the local character's sensor computes, so on
// the puppet it always failed: the puppet hung at the grab point for the
// whole climb (measured live: its network buffer and TS+0x534 kept rising,
// its transform and physics stayed frozen) and popped ~10 m at the top. For
// the puppet only, report success: in control mode 2 the flag just enables
// the interpolation, exactly as for walking (docs/research/pointclimb-remoto.md).
SafetyHookInline g_climb_enter_hook;
std::atomic<uint64_t> g_climb_forced{0};
std::atomic<uint64_t> g_climb_other_fails{0};
std::atomic<uint32_t> g_climb_last_owner{0};
uint64_t g_climb_forced_logged = 0;
uint64_t g_climb_other_logged = 0;

uint32_t owner_of_method(void* method) {
    __try {
        const auto actor = *reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(method) +
                                                               off::kMethod_Actor);
        return actor ? *reinterpret_cast<const uint32_t*>(actor + off::kActor_OwnerUser) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Mounting and dismounting (RideOn/RideOff, methods 6/7): same pattern, their
// Enter needs the rider<->horse link that a remote copy never gets.
SafetyHookMid g_layer_guard_hook;
uint8_t* g_ret_stub = nullptr;  // a lone `ret`: resume there to return at once
std::atomic<uint64_t> g_layer_guard_fired{0};
std::atomic<uintptr_t> g_layer_guard_caller{0};
uint64_t g_layer_guard_logged = 0;

bool layer_has_no_playback(uintptr_t slot) {
    __try {
        return *reinterpret_cast<const uintptr_t*>(slot) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void zero_float_out(uintptr_t rsp) {
    __try {
        if (auto* out = *reinterpret_cast<float**>(rsp + 0x28)) *out = 0.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void action_layer_advance(safetyhook::Context& ctx) {
    if (!layer_has_no_playback(ctx.rcx)) return;
    // Nothing is playing on this layer: advance nothing, like the caller
    // does for a rate <= 0. rsp still points at the return address.
    zero_float_out(ctx.rsp);
    std::memset(&ctx.xmm0, 0, sizeof ctx.xmm0);
    __try {
        g_layer_guard_caller.store(*reinterpret_cast<const uintptr_t*>(ctx.rsp), std::memory_order_relaxed);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_layer_guard_fired.fetch_add(1, std::memory_order_relaxed);
    ctx.rip = reinterpret_cast<uintptr_t>(g_ret_stub);
}

// One level up: skip the whole layer update of one of OUR copies while a
// layer has a chart but nothing playing (until its first action applies).
SafetyHookMid g_layers_skip_hook;
std::atomic<uint64_t> g_layers_skipped{0};
std::atomic<uint32_t> g_layers_skipped_key{0};
uint64_t g_layers_skipped_logged = 0;

std::atomic<uint32_t> g_guard_extra_key{0};  // option D: user 2's character
std::atomic<uint32_t> g_guard_owner{0};      // option D: everything user 2 owns
std::atomic<uint32_t> g_guard_local{0};      // P8 guest: everything NOT controlled by this local user
std::atomic<PuppetGuardObserver> g_guard_observer{nullptr};
std::atomic<uint64_t> g_guard_remote_skipped{0};

// How long each held actor has waited for its first action (a few at a time).
struct Held {
    uint32_t key = 0;
    uint64_t since_ms = 0;
    bool reported = false;
};
constexpr uint64_t kHeldTooLongMs = 3000;  // companeros.md §C
Held g_held[16];
std::mutex g_held_mutex;

void note_held(uint32_t key) {
    std::lock_guard lock(g_held_mutex);
    const uint64_t now = GetTickCount64();
    Held* slot = nullptr;
    for (Held& h : g_held) {
        if (h.key == key) {
            slot = &h;
            break;
        }
        if (!slot && (!h.key || now - h.since_ms > 60000)) slot = &h;
    }
    if (!slot) return;
    if (slot->key != key) {
        *slot = Held{key, now, false};
        log::info("Puppet: held 0x{:08X} (a remote actor of option D with a chart and no action yet): its layers "
                  "wait for its first action",
                  key);
        return;
    }
    if (!slot->reported && now - slot->since_ms >= kHeldTooLongMs) {
        slot->reported = true;
        log::warn("Puppet: 0x{:08X} has been held {} ms without a first action (its controller never sent one; "
                  "with cdmp_peer that is expected for companions: the peer runs no companion AI)",
                  key, now - slot->since_ms);
    }
}
std::atomic<bool> g_layer_guards_on{false};   // set once both guards are in (any thread reads it)
std::mutex g_layer_guards_mutex;              // co-op and option D may both ask for them

constexpr uint32_t kLayer_CurrentAction = 0x10;  // horse_diag's cur(+10)

// Why the last skip on this thread happened (p8-raiz-invitado.md §5.2: the
// predicate is chart && (!current || !playback); the log must say which).
// bits: 1 = no current action (+0x10), 2 = no playback (+0x18); << 2 for layer 1.
thread_local uint8_t t_guard_reason = 0;
std::atomic<uint64_t> g_skip_no_current{0}, g_skip_no_playback{0}, g_skip_both{0};

// 0 = fine; otherwise the actor key of a copy whose layer has no action.
// `remote` = it is guarded only because it is not controlled locally (P8 guest);
// `ctrl` = its +0x90.
uint32_t puppet_layer_without_action(uintptr_t control, bool& remote, uint32_t& ctrl) {
    remote = false;
    ctrl = 0;
    t_guard_reason = 0;
    __try {
        const auto actor = *reinterpret_cast<const uintptr_t*>(control + 0x08);
        if (!actor) return 0;
        const uint32_t key = *reinterpret_cast<const uint32_t*>(actor + off::kActor_Key);
        const uint32_t extra = g_guard_extra_key.load(std::memory_order_relaxed);
        const uint32_t owner = g_guard_owner.load(std::memory_order_relaxed);
        const uint32_t local = g_guard_local.load(std::memory_order_relaxed);
        ctrl = *reinterpret_cast<const uint32_t*>(actor + off::kActor_OwnerUser);
        if (!is_puppet_key(key) && !is_enemy_copy_key(key) && !(extra && key == extra) &&
            !(owner && ctrl == owner)) {
            if (!session2::gad::remote_controlled(ctrl, local)) return 0;
            remote = true;
        }
        for (uint32_t i = 0; i < 2; ++i) {
            const auto layer = *reinterpret_cast<const uintptr_t*>(control + off::kControl_Layer0 + 8 * i);
            if (!layer) continue;
            // No playback (+0x18) or no current action (+0x10): 0x232D5FF reads
            // the current action (companion probe: its r8 copies layer +0..+0x18).
            const bool no_play = *reinterpret_cast<const uintptr_t*>(layer + off::kLayer_Playback) == 0;
            const bool no_cur = *reinterpret_cast<const uintptr_t*>(layer + kLayer_CurrentAction) == 0;
            if (*reinterpret_cast<const uintptr_t*>(layer + off::kLayer_Chart) != 0 && (no_play || no_cur)) {
                t_guard_reason = static_cast<uint8_t>(((no_cur ? 1 : 0) | (no_play ? 2 : 0)) << (2 * i));
                return key;
            }
        }
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void action_layers_advance(safetyhook::Context& ctx) {
    bool remote = false;
    uint32_t ctrl = 0;
    const uint32_t key = puppet_layer_without_action(ctx.rcx, remote, ctrl);
    if (!key) return;
    g_layers_skipped_key.store(key, std::memory_order_relaxed);
    g_layers_skipped.fetch_add(1, std::memory_order_relaxed);
    if (remote) {
        g_guard_remote_skipped.fetch_add(1, std::memory_order_relaxed);
        const uint8_t why = t_guard_reason | static_cast<uint8_t>(t_guard_reason >> 2);
        ((why & 3) == 3 ? g_skip_both : (why & 1) ? g_skip_no_current : g_skip_no_playback)
            .fetch_add(1, std::memory_order_relaxed);
        if (const auto obs = g_guard_observer.load(std::memory_order_relaxed)) obs(key, ctrl);
    } else if (g_guard_owner.load(std::memory_order_relaxed)) {
        note_held(key);
    }
    ctx.rip = reinterpret_cast<uintptr_t>(g_ret_stub);  // return at once
}

SafetyHookInline g_ride_on_hook;
SafetyHookInline g_ride_off_hook;
std::atomic<uint64_t> g_ride_forced{0};

uint8_t force_for_puppet(uint8_t ok, void* method, std::atomic<uint64_t>& forced) {
    if (ok) return ok;
    if (owner_of_method(method) == kPuppetUser) {
        forced.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }
    return ok;
}

uint8_t ride_on_enter(void* method, uint8_t same_method) {
    return force_for_puppet(g_ride_on_hook.unsafe_call<uint8_t>(method, same_method), method, g_ride_forced);
}

// Not forced: forcing RideOff::Enter left the copy stuck in the dismount
// pose for good (live test 1). Unforced, the dismount freezes briefly and the
// next action recovers.
uint8_t ride_off_enter(void* method, uint8_t same_method) {
    return g_ride_off_hook.unsafe_call<uint8_t>(method, same_method);
}

uint8_t point_climb_enter(void* method, uint8_t same_method) {
    const uint8_t ok = g_climb_enter_hook.unsafe_call<uint8_t>(method, same_method);
    if (ok) return ok;
    const uint32_t owner = owner_of_method(method);
    if (owner == kPuppetUser) {
        g_climb_forced.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }
    g_climb_last_owner.store(owner, std::memory_order_relaxed);
    g_climb_other_fails.fetch_add(1, std::memory_order_relaxed);
    return ok;
}

}  // namespace

// The horse's load packet carries its position three times (caballo-coop.md
// §2.2); the last Transform is found from its end marker: scale (1,1,1)
// followed by an all-FF parent. Every copy of that position is moved.
bool spawn_horse(const proto::SpawnInfo& info, uint64_t now) {
    Bytes horse = info.horse;
    if (horse.size() < 0x40) return false;
    replace_u32(horse, info.user_key, kPuppetUser);
    replace_u32(horse, info.player_key, kPuppetPlayer);
    replace_u32(horse, info.horse_key, kPuppetHorse);
    // Note: loading it without an owner record (+0x12 = 0, caballo-coop.md §8)
    // crashed the game right after the load (live test 2, access violation at
    // exe+0x1F63FE6 on another thread). With the owner it is silently not
    // created; the horse stays experimental and off by default.
    // Party list: ... | player | other member | horse | ... The other member
    // is an actor of the sender's world; give it a key of ours.
    for (size_t i = 0; i + 12 <= horse.size(); ++i) {
        uint32_t a, b, c;
        std::memcpy(&a, horse.data() + i, 4);
        std::memcpy(&b, horse.data() + i + 4, 4);
        std::memcpy(&c, horse.data() + i + 8, 4);
        if (a == kPuppetPlayer && c == kPuppetHorse && keys::is_class(b, keys::kClassWorld) && !is_puppet_key(b)) {
            std::memcpy(horse.data() + i + 4, &kPuppetHorseParty, 4);
        }
    }
    static const uint8_t kMarker[20] = {0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00,
                                        0x80, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    size_t marker = std::string::npos;
    for (size_t i = horse.size() - sizeof kMarker; i > 28; --i) {
        if (std::memcmp(horse.data() + i, kMarker, sizeof kMarker) == 0) {
            marker = i;
            break;
        }
    }
    if (marker == std::string::npos) {
        log::warn("Puppet: horse packet has no Transform marker; horse not spawned");
        return false;
    }
    uint8_t old_pos[12];
    std::memcpy(old_pos, horse.data() + marker - 28, sizeof old_pos);
    const float pos[3] = {info.horse_pos.x + g_offset_x, info.horse_pos.y, info.horse_pos.z};
    int copies = 0;
    for (size_t i = 0; i + 12 <= horse.size(); ++i) {
        if (std::memcmp(horse.data() + i, old_pos, 12) == 0) {
            std::memcpy(horse.data() + i, pos, 12);
            ++copies;
            i += 11;
        }
    }
    // At least 3 s after the player's packets (the copy's user must be logged
    // in first), and only together with the horse's first action (horse_event).
    {
        std::lock_guard lock(g_mutex);
        g_horse_pending = make_packet(off::kOpActorLoadDataAck, horse);
        g_horse_not_before = now + 3000;
    }
    horse_diag_watch(info.horse_key);
    log::info("Puppet: horse prepared at ({:.1f}, {:.1f}, {:.1f}) ({} bytes, position x{}, sender "
              "horse 0x{:08X}); loaded with its first action",
              pos[0], pos[1], pos[2], horse.size(), copies, info.horse_key);
    return true;
}

bool puppet_spawn(const proto::SpawnInfo& info) {
    Bytes user = info.user, body = info.body;
    {
        std::lock_guard lock(g_mutex);
        if (g_spawned) {
            log::warn("Puppet: already spawned; remove it first");
            return false;
        }
        for (auto* v : {&user, &body}) {
            const int a = replace_u32(*v, info.user_key, kPuppetUser);
            const int b = replace_u32(*v, info.player_key, kPuppetPlayer);
            const int c = replace_u32(*v, info.body_key, kPuppetBody);
            log::info("Puppet: rewrote keys in {} packet: user x{}, player x{}, body x{}",
                      v == &user ? "user" : "body", a, b, c);
        }
        g_spawned = true;
        g_sender_riding = false;
        g_unmounted_moves = 0;
        g_src_player_key = info.player_key;
        g_src_user_key = info.user_key;
        g_sender_horse = info.horse_key;
        g_src_horse_key = 0;  // set once the horse load is queued
        g_horse_spawned = false;
    }

    // Both packets carry the world position (3 floats) 154 bytes before the
    // end. Changing only the body's copy had no effect in game (the copy kept
    // appearing at the load spot), so write both.
    // At user +0x17 and +0x43; the body embeds a copy of the user record
    // (found by its header). Other fields can hold the same max value, so only
    // these positions are touched.
    if (!set_character_no(user, body)) {
        log::warn("Puppet: character number not where expected; the copy keeps the sender's "
                  "(equipment screens may get mixed up)");
    }
    if (user.size() < 256 || body.size() < 256) {
        std::lock_guard lock(g_mutex);
        g_spawned = false;
        log::warn("Puppet: spawn data too short; ignored");
        return false;
    }
    const float pos[3] = {info.pos.x + g_offset_x, info.pos.y, info.pos.z};
    const float rot[4] = {info.rot.x, info.rot.y, info.rot.z, info.rot.w};
    const bool rot_ok = std::fabs(rot[0] * rot[0] + rot[1] * rot[1] + rot[2] * rot[2] + rot[3] * rot[3] - 1.0f) < 0.05f;
    for (auto* v : {&user, &body}) {
        const size_t pos_off = v->size() - off::kLoadPositionFromEnd;
        std::memcpy(v->data() + pos_off, pos, sizeof pos);
        // Facing: the quaternion right after the position (Transform).
        if (rot_ok) std::memcpy(v->data() + v->size() - off::kLoadRotationFromEnd, rot, sizeof rot);
    }
    // The body packet is [u16 len][record]... : its own record, then a byte
    // copy of the user packet. For the real player the client skips the
    // repeated player record (it exists, owned by the local user); for the
    // copy it tries to create 0xA010F001 again and logs "Actor creation
    // failed" (caballo-coop.md §9.7). Send only the body's own record.
    {
        size_t at = 0;
        int records = 0;
        uint16_t first = 0;
        while (at + 2 <= body.size()) {
            uint16_t len = 0;
            std::memcpy(&len, body.data() + at, 2);
            if (records == 0) first = len;
            at += 2 + len;
            ++records;
        }
        if (at == body.size() && records >= 2 && first >= 0x40) {
            body.resize(2 + first);
        } else {
            log::info("Puppet: body packet layout not as expected ({} records); sent whole", records);
        }
    }
    const uint64_t now = steady_ms();
    enqueue_at(make_packet(off::kOpActorLoadDataAck, user), now, /*movement=*/false);
    enqueue_at(make_packet(off::kOpActorLoadDataAck, body), now, /*movement=*/false);
    bool reuse = false;
    {
        std::lock_guard lock(g_mutex);
        reuse = g_horse_kept && info.horse_key == g_sender_horse && game::find_actor(kPuppetHorse);
        g_horse_kept = false;
        if (reuse) {
            g_horse_spawned = true;
            g_src_horse_key = info.horse_key;
        }
    }
    if (reuse) {
        log::info("Puppet: rider reloaded next to the horse copy that stayed");
    } else if (g_horse_enabled && !info.horse.empty() && info.horse_key && spawn_horse(info, now)) {
        std::lock_guard lock(g_mutex);
        g_horse_spawned = true;
        g_src_horse_key = info.horse_key;
    }
    log::info("Puppet: spawn queued at ({:.1f}, {:.1f}, {:.1f}) (2 packets, {} + {} bytes, "
              "sender player 0x{:08X}, generation {})",
              pos[0], pos[1], pos[2], user.size(), body.size(), info.player_key, info.generation);
    // Gear the sender changed since its templates were captured. The copy
    // loads wearing the template's gear, and the client drops a whole put
    // list if one item lands on an occupied slot: first empty every slot the
    // template had filled (OtherUnequipItemAck, no serial gate), then put the
    // items on, as the live messages left them (ropa-al-desmontar.md section
    // 5.2). Not movement: a purge or a late delivery must not lose them.
    if (g_equip_enabled && EquipState::valid(info.equip.data(), info.equip.size())) {
        EquipState state;
        state.apply(info.equip);
        const auto removals = state.removals();
        const auto puts = state.puts();
        const uint64_t at = now + 1000;
        for (uint16_t slot : removals) {
            enqueue_at(make_packet(off::kOpOtherUnequipItemAck,
                                   unequip_payload(kPuppetPlayer, slot, next_equip_serial(state.serial()))),
                       at, /*movement=*/false);
        }
        uint32_t serial = 0;
        if (!puts.empty()) {
            serial = next_equip_serial(state.serial());
            enqueue_at(make_packet(off::kOpOtherEquipItemAck, EquipState::payload(kPuppetPlayer, serial, puts)), at,
                       /*movement=*/false);
        }
        g_equip_check_at = at + 1500;
        log::info("Puppet: gear changed since the sender's load, applied after spawn: {} slot(s) emptied, "
                  "{} item(s) put (serial {})",
                  removals.size(), puts.size(), serial);
    }
    return true;
}

bool puppet_remove() {
    {
        std::lock_guard lock(g_mutex);
        if (!g_spawned) return false;
        g_spawned = false;
    }
    purge_movement();  // no movement for an actor that is about to disappear
    // RemoveActorAck payload: 04 00 00 00 00 | type | u32 key. The type byte
    // varies per actor kind and is not understood yet; 0x15 is the most common.
    bool horse = false;
    {
        std::lock_guard lock(g_mutex);
        horse = g_horse_spawned;
        g_horse_spawned = false;
    }
    bool kept = false;
    {
        std::lock_guard lock(g_mutex);
        if (!g_horse_pending.empty()) horse = false;  // never loaded
        g_horse_pending.clear();
        if (g_horse_kept) horse = true;  // left without its rider earlier
        if (horse && g_keep_horse) {
            horse = false;
            kept = g_horse_kept = true;
        } else {
            g_horse_kept = false;
        }
        g_keep_horse = false;
    }
    if (kept) log::info("Puppet: the horse copy stays while its rider reloads");
    if (horse) {
        Bytes payload = {0x04, 0x00, 0x00, 0x00, 0x00, 0x15, 0, 0, 0, 0};
        std::memcpy(payload.data() + 6, &kPuppetHorse, 4);
        enqueue_at(make_packet(off::kOpRemoveActorAck, payload), steady_ms(), /*movement=*/false);
    }
    for (uint32_t key : {kPuppetBody, kPuppetPlayer}) {
        Bytes payload = {0x04, 0x00, 0x00, 0x00, 0x00, 0x15, 0, 0, 0, 0};
        std::memcpy(payload.data() + 6, &key, 4);
        enqueue_at(make_packet(off::kOpRemoveActorAck, payload), steady_ms(), /*movement=*/false);
    }
    log::info("Puppet: remove queued (body 0x{:08X}, player 0x{:08X})", kPuppetBody, kPuppetPlayer);
    return true;
}

void puppet_forget() {
    {
        std::lock_guard lock(g_mutex);
        if (!g_spawned) return;
        g_spawned = false;
        g_horse_spawned = false;
        g_horse_pending.clear();
        g_horse_kept = g_keep_horse = false;
    }
    purge_movement();
    log::info("Puppet: forgotten (no longer in the game's actor list)");
}

bool puppet_take_reload_request() { return g_reload_request.exchange(false); }
void puppet_request_reload() { g_reload_request = true; }
bool puppet_spawn_enemy(uint32_t copy_key, std::vector<uint8_t> load, std::vector<uint8_t> action_req) {
    // Every copy of its position moves by the puppet offset (found from the
    // last Transform, as for the horse).
    static const uint8_t kMarker[20] = {0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00,
                                        0x80, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    int copies = 0;
    float pos[3] = {NAN, NAN, NAN};
    float orig[3] = {NAN, NAN, NAN};
    float rot[4] = {0, 0, 0, 1};
    for (size_t i = load.size() - sizeof kMarker; i > 28; --i) {
        if (std::memcmp(load.data() + i, kMarker, sizeof kMarker) != 0) continue;
        uint8_t old_pos[12];
        std::memcpy(old_pos, load.data() + i - 28, sizeof old_pos);
        std::memcpy(orig, old_pos, sizeof orig);
        std::memcpy(rot, load.data() + i - 16, sizeof rot);
        std::memcpy(pos, old_pos, sizeof pos);
        pos[0] += g_offset_x;
        for (size_t j = 0; j + 12 <= load.size(); ++j) {
            if (std::memcmp(load.data() + j, old_pos, 12) == 0) {
                std::memcpy(load.data() + j, pos, 12);
                ++copies;
                j += 11;
            }
        }
        break;
    }
    const uint64_t now = steady_ms();
    enqueue_at(make_packet(off::kOpActorLoadDataAck, load), now, /*movement=*/false);
    // Its recent actions, oldest first, right behind the load (one per action
    // layer at least; see enemy_relay.cpp).
    int with_action = 0;
    for (size_t at = 0; at + 725 <= action_req.size(); at += 725) {
        const Bytes one(action_req.begin() + at, action_req.begin() + at + 725);
        if (auto ack = start_action_ack(one)) {
            enqueue_at(make_packet(off::kOpStartActionAck, *ack), now, /*movement=*/false);
            ++with_action;
        }
    }
    // A network-driven actor is placed (and drawn) from its movement samples:
    // without one the first copies loaded but never showed (live test). One
    // still sample where it stands, built on the other player's last Move.
    bool with_move = false;
    if (g_last_move.size() == 514 && std::isfinite(orig[0])) {
        Bytes m = g_last_move;
        std::memcpy(m.data() + 0x00, &copy_key, 4);
        std::memcpy(m.data() + 0x04, orig, 12);
        std::memcpy(m.data() + 0x10, rot, 16);
        std::memset(m.data() + 0x20, 0, 4);
        std::memcpy(m.data() + 0x4C, orig, 12);  // Path.local = world, no parent
        std::memcpy(m.data() + 0x58, rot, 16);
        const float one[3] = {1.0f, 1.0f, 1.0f};
        std::memcpy(m.data() + 0x68, one, sizeof one);
        std::memset(m.data() + 0x8C, 0xFF, 8);
        std::memset(m.data() + 0x94, 0, 0x40);
        float ref[3];
        std::memcpy(ref, g_ref_pos, sizeof ref);  // the sender's reference stays
        auto ack = move_ack(m);
        std::memcpy(g_ref_pos, ref, sizeof ref);
        if (ack) {
            enqueue_at(make_packet(off::kOpMoveActorAck, *ack), now + 200, /*movement=*/false);
            with_move = true;
        }
    }
    log::info("Enemy relay: copy 0x{:08X} queued at ({:.1f}, {:.1f}, {:.1f}) ({} B, position x{}, {}, {})",
              copy_key, pos[0], pos[1], pos[2], load.size(), copies,
              with_action ? std::to_string(with_action) + " actions" : std::string("NO action"),
              with_move ? "placed by a Move" : "NO move");
    return true;
}

// Each enemy copy's latest (unshifted) position: its own actions carry
// positions near IT, which shift_x_if_near_sender must recognise.
std::map<uint32_t, std::array<float, 3>> g_enemy_pos;

void puppet_enemy_move(uint32_t copy_key, uint64_t ts, const proto::Vec3& pos, const proto::Quat& rot,
                       int64_t ts_shift_ms, uint64_t delay_ms) {
    if (g_last_move.size() != 514) return;
    Bytes m = g_last_move;
    const float p[3] = {pos.x, pos.y, pos.z};
    const float q[4] = {rot.x, rot.y, rot.z, rot.w};
    std::memcpy(m.data() + 0x00, &copy_key, 4);
    std::memcpy(m.data() + 0x04, p, sizeof p);
    std::memcpy(m.data() + 0x10, q, sizeof q);
    std::memset(m.data() + 0x20, 0, 4);
    std::memcpy(m.data() + 0x4C, p, sizeof p);  // Path.local = world, no parent
    std::memcpy(m.data() + 0x58, q, sizeof q);
    const float one[3] = {1.0f, 1.0f, 1.0f};
    std::memcpy(m.data() + 0x68, one, sizeof one);
    std::memset(m.data() + 0x8C, 0xFF, 8);
    std::memset(m.data() + 0x94, 0, 0x40);
    std::memcpy(m.data() + 0xD5, &ts, 8);
    g_enemy_pos[copy_key] = {p[0], p[1], p[2]};
    float ref[3];
    std::memcpy(ref, g_ref_pos, sizeof ref);
    g_ts_shift = ts_shift_ms;
    auto ack = move_ack(m);
    std::memcpy(g_ref_pos, ref, sizeof ref);
    if (ack) enqueue_at(make_packet(off::kOpMoveActorAck, *ack), steady_ms() + delay_ms, /*movement=*/false);
}

void puppet_enemy_action(uint16_t req_op, const std::vector<uint8_t>& req, int64_t ts_shift_ms,
                         uint64_t delay_ms) {
    if (req.size() < 4) return;
    uint32_t key;
    std::memcpy(&key, req.data(), 4);
    float ref[3];
    std::memcpy(ref, g_ref_pos, sizeof ref);
    if (auto it = g_enemy_pos.find(key); it != g_enemy_pos.end()) {
        std::memcpy(g_ref_pos, it->second.data(), sizeof ref);
    }
    g_ts_shift = ts_shift_ms;
    std::optional<Bytes> ack;
    uint16_t op = 0;
    if (req_op == off::kOpStartActionReq) {
        ack = start_action_ack(req);
        op = off::kOpStartActionAck;
    } else if (req_op == off::kOpRotateActorReq) {
        ack = rotate_ack(req);
        op = off::kOpRotateActorAck;
    }
    std::memcpy(g_ref_pos, ref, sizeof ref);
    if (ack) enqueue_at(make_packet(op, *ack), steady_ms() + delay_ms, /*movement=*/false);
}

void puppet_remove_actor(uint32_t key) {
    g_enemy_pos.erase(key);
    Bytes payload = {0x04, 0x00, 0x00, 0x00, 0x00, 0x15, 0, 0, 0, 0};
    std::memcpy(payload.data() + 6, &key, 4);
    enqueue_at(make_packet(off::kOpRemoveActorAck, payload), steady_ms(), /*movement=*/false);
}

void puppet_inject_s2c(uint16_t op, const std::vector<uint8_t>& payload, uint64_t delay_ms) {
    enqueue_at(make_packet(op, payload), steady_ms() + delay_ms, /*movement=*/false);
}

void puppet_inject_s2c_batch(const std::vector<std::pair<uint16_t, std::vector<uint8_t>>>& packets) {
    const uint64_t due = steady_ms();
    AcquireSRWLockExclusive(&g_queue_lock);
    auto it = std::upper_bound(g_queue.begin(), g_queue.end(), due,
                               [](uint64_t d, const Pending& p) { return d < p.due_ms; });
    for (const auto& [op, payload] : packets) {
        it = g_queue.insert(it, {due, /*movement=*/false, make_packet(op, payload)});
        ++it;
    }
    ReleaseSRWLockExclusive(&g_queue_lock);
}

bool puppet_spawned() {
    std::lock_guard lock(g_mutex);
    return g_spawned;
}

std::atomic<uint8_t> g_pin_shape{4}, g_pin_colour{14};

void puppet_map_pin_style(uint8_t shape, uint8_t colour) {
    if (shape < off::kPinShapeCount) g_pin_shape = shape;
    if (colour < off::kPinColourCount) g_pin_colour = colour;
}

void puppet_map_pin(uint32_t owner, const std::optional<proto::Vec3>& pos, bool replace) {
    const uint64_t id = off::kCoopPinId;
    const uint64_t now = steady_ms();
    if (!pos || replace) {
        Bytes r(13, 0);
        std::memcpy(r.data(), &owner, 4);
        std::memcpy(r.data() + 4, &id, 8);
        r[12] = off::kPinGroupNormal;
        enqueue_at(make_packet(off::kOpRemovePinMarkerAck, r), now, /*movement=*/false);
    }
    if (!pos) return;
    // Same due time as the removal: the queue keeps their order.
    Bytes c(6 + 22, 0);
    c[0] = 0x04;
    std::memcpy(c.data() + 1, &owner, 4);
    c[5] = off::kPinGroupNormal;
    std::memcpy(c.data() + 6, &id, 8);
    const float xyz[3] = {pos->x, pos->y, pos->z};
    std::memcpy(c.data() + 14, xyz, sizeof xyz);
    c[26] = g_pin_shape.load();
    c[27] = g_pin_colour.load();
    enqueue_at(make_packet(off::kOpCreateOrChangePinMarkerAck, c), now, /*movement=*/false);
}

std::optional<proto::Vec3> puppet_position() {
    if (!puppet_spawned()) return std::nullopt;
    return game::actor_world_position(kPuppetPlayer);
}

// ---------------------------------------------------------------- horse
// While the sender rides, its own Moves carry the horse's world transform in
// Path.world (+0x94 position, +0xA0 rotation; caballo-coop.md §1.2) at ~36 Hz,
// whereas the horse itself reports only once a second. Derive a horse Move
// from every rider Move, with the same timestamp, so both copies interpolate
// in step and the rider stays in the saddle.
uint64_t g_horse_ridden_until = 0;

void ride_horse(const Bytes& rider_req, uint64_t now, uint64_t delay_ms) {
    if (!g_horse_spawned) return;
    Bytes h = rider_req;
    std::memcpy(h.data() + 0x00, &kPuppetHorse, 4);
    std::memcpy(h.data() + 0x04, rider_req.data() + 0x94, 12);  // horse position
    std::memcpy(h.data() + 0x10, rider_req.data() + 0xA0, 16);  // horse rotation
    std::memset(h.data() + 0x20, 0, 4);
    std::memcpy(h.data() + 0x4C, rider_req.data() + 0x94, 12);  // Path.local = world, no parent
    std::memcpy(h.data() + 0x58, rider_req.data() + 0xA0, 16);
    const float one[3] = {1.0f, 1.0f, 1.0f};
    std::memcpy(h.data() + 0x68, one, sizeof one);
    std::memset(h.data() + 0x8C, 0xFF, 8);
    std::memset(h.data() + 0x94, 0, 0x40);
    float hp[3];
    std::memcpy(hp, h.data() + 0x04, sizeof hp);
    if (!std::isfinite(hp[0]) || (hp[0] == 0.0f && hp[2] == 0.0f)) return;
    float ref[3];
    std::memcpy(ref, g_ref_pos, sizeof ref);  // keep the rider's reference position
    auto ack = move_ack(h);
    std::memcpy(g_ref_pos, ref, sizeof ref);
    if (!ack) return;
    enqueue_at(make_packet(off::kOpMoveActorAck, *ack), now + delay_ms, /*movement=*/true);
    g_horse_ridden_until = now + 200;
}

// The horse's own messages (unridden moves, actions, turns).
void horse_event(proto::EventKind kind, const Bytes& payload, uint64_t now, uint64_t delay_ms) {
    if (!g_horse_spawned) return;
    Bytes load;
    uint64_t load_at = 0;
    {
        std::lock_guard lock(g_mutex);
        if (!g_horse_pending.empty()) {
            // Not loaded yet: only its first action loads it; the rest is moot.
            if (kind != proto::EventKind::StartAction) return;
            if (!game::find_actor(kPuppetPlayer)) return;  // the copy's player first
            load = std::move(g_horse_pending);
            g_horse_pending.clear();
            load_at = std::max(now, g_horse_not_before);
        }
    }
    if (!load.empty()) {
        float keep[3];
        std::memcpy(keep, g_ref_pos, sizeof keep);
        auto first = start_action_ack(payload);
        std::memcpy(g_ref_pos, keep, sizeof keep);
        if (!first) {
            std::lock_guard lock(g_mutex);
            g_horse_pending = std::move(load);  // wait for a usable one
            return;
        }
        // Same due time: the queue is stable, so the action lands right
        // behind the load (retried by the client until the horse exists).
        enqueue_at(std::move(load), load_at, /*movement=*/false);
        enqueue_at(make_packet(off::kOpStartActionAck, *first), load_at, /*movement=*/false);
        log::info("Puppet: horse load queued together with its first action ({} bytes)", payload.size());
        return;
    }
    uint16_t op = 0;
    std::optional<Bytes> ack;
    float ref[3];
    std::memcpy(ref, g_ref_pos, sizeof ref);  // the shift reference stays the rider's
    switch (kind) {
        case proto::EventKind::Move:
            // Ridden: its rare own Move repeats a timestamp we already used.
            if (now < g_horse_ridden_until) return;
            op = off::kOpMoveActorAck;
            ack = move_ack(payload);
            break;
        case proto::EventKind::StartAction:
            op = off::kOpStartActionAck;
            ack = start_action_ack(payload);
            break;
        case proto::EventKind::Rotate:
            op = off::kOpRotateActorAck;
            ack = rotate_ack(payload);
            break;
        default:
            return;
    }
    std::memcpy(g_ref_pos, ref, sizeof ref);
    if (ack) enqueue_at(make_packet(op, *ack), now + delay_ms, /*movement=*/true);
}

void puppet_event(const proto::GameEvent& ev, int64_t ts_shift_ms, uint64_t delay_ms) {
    if (!puppet_spawned() || ev.payload.size() > proto::kMaxEventPayload) return;
    const uint32_t src = g_src_player_key;
    Bytes payload = ev.payload;
    // Moves arrive without their unused trailing history (local_capture emit).
    if (ev.kind == proto::EventKind::Move && payload.size() == 0xEA) payload.resize(514, 0);
    // The actor key leads every payload, after a fragment marker byte in
    // OtherEquipItemAck.
    const size_t key_at = ev.kind == proto::EventKind::Equip   ? 1
                          : ev.kind == proto::EventKind::Relay ? 2
                                                               : 0;
    if (payload.size() < key_at + 4) return;
    const uint32_t key = u32_at(payload, key_at);
    const uint32_t src_horse = g_src_horse_key;
    const bool is_horse = src_horse && key == src_horse;
    if (key != src && !is_horse) return;
    replace_u32(payload, src, kPuppetPlayer);
    if (src_horse) replace_u32(payload, src_horse, kPuppetHorse);
    if (const uint32_t user = g_src_user_key) {
        // The user key is often 0x90100000, whose bytes "00 00 10 90" also turn
        // up by chance inside other fields (a ReviveAck's "02 00 00 00 10 90 ..."
        // became 0xF0010002): in relayed server messages, only at 4-byte
        // aligned field offsets.
        if (ev.kind == proto::EventKind::Relay) {
            for (size_t i = 2; i + 4 <= payload.size(); i += 4) {
                if (u32_at(payload, i) == user) std::memcpy(payload.data() + i, &kPuppetUser, 4);
            }
        } else {
            replace_u32(payload, user, kPuppetUser);
        }
    }
    g_ts_shift = ts_shift_ms;
    const uint64_t now = steady_ms();

    if (is_horse) {
        horse_event(ev.kind, payload, now, delay_ms);
        return;
    }

    uint16_t ack_op = 0;
    std::optional<Bytes> ack;
    switch (ev.kind) {
        case proto::EventKind::Move:
            if (payload.size() != 514) return;
            fill_gap_before(payload, delay_ms);
            g_last_move = payload;
            g_last_move_ms = now;
            std::memcpy(&g_last_move_ts, payload.data() + 0xD5, 8);
            std::memcpy(g_last_move_pos, payload.data() + 0x04, sizeof g_last_move_pos);
            ack_op = off::kOpMoveActorAck;
            ack = move_ack(payload);
            if (u32_at(payload, 0x8C) == kPuppetHorse) ride_horse(payload, now, delay_ms);
            {
                // Dismounted (parent back to none for a few Moves): unless our
                // copy really rides our copy of the horse, reload it. Only the
                // sender's horse counts: other parents (a pulled tree, a carried
                // animal, a boat) caused reload loops in live test 4.
                const uint32_t parent = u32_at(payload, 0x8C);
                const uint32_t horse = g_sender_horse;
                const bool riding = horse && (parent == kPuppetHorse || parent == horse);
                if (riding) {
                    g_sender_riding = true;
                    g_unmounted_moves = 0;
                } else if (g_sender_riding && ++g_unmounted_moves >= 5) {
                    g_sender_riding = false;
                    // Our horse copy may have been queued but not created by the
                    // game (live tests): look it up before trusting it.
                    bool keep = false;
                    {
                        std::lock_guard lock(g_mutex);
                        keep = g_horse_spawned && g_horse_pending.empty() && game::find_actor(kPuppetHorse);
                        g_keep_horse = keep;
                    }
                    g_reload_request = true;
                    log::info("Puppet: the sender dismounted; reloading the {} (no rider link)",
                              keep ? "rider, the horse stays" : "copy");
                }
            }
            break;

        case proto::EventKind::StartAction:
            // A StartAction right after a filled climb-up that still carries the
            // frozen start position: move it to where the sender ended up.
            if (payload.size() == 725 && g_recent_gap.valid) {
                if (now - g_recent_gap.detected_ms <= 300) {
                    for (size_t at : {size_t{0x8E}, size_t{0xD6}}) {
                        float v[3];
                        std::memcpy(v, payload.data() + at, sizeof v);
                        const float ex = v[0] - g_recent_gap.start[0], ey = v[1] - g_recent_gap.start[1],
                                    ez = v[2] - g_recent_gap.start[2];
                        if (ex * ex + ey * ey + ez * ez < 4.0f) {
                            std::memcpy(payload.data() + at, g_recent_gap.end, sizeof g_recent_gap.end);
                        }
                    }
                } else {
                    g_recent_gap.valid = false;
                }
            }
            ack_op = off::kOpStartActionAck;
            ack = start_action_ack(payload);
            break;

        case proto::EventKind::Rotate:
            // During a Move gap (climbing...), RotateActorReq keeps sending the
            // frozen grab position at +0x14; applied to the puppet it would drag
            // it back to the wall's base between synthesised moves. Use the
            // latest Move position instead (only when that field actually holds
            // a nearby position).
            if (payload.size() == 176 && now - g_last_move_ms >= kMoveSilenceMs &&
                std::isfinite(g_last_move_pos[0])) {
                float v[3];
                std::memcpy(v, payload.data() + 0x14, sizeof v);
                const float dx = v[0] - g_last_move_pos[0], dz = v[2] - g_last_move_pos[2];
                if (dx * dx + dz * dz < 30.0f * 30.0f) {
                    std::memcpy(payload.data() + 0x14, g_last_move_pos, sizeof g_last_move_pos);
                }
            }
            ack_op = off::kOpRotateActorAck;
            ack = rotate_ack(payload);
            break;

        case proto::EventKind::Equip:
            // OtherEquipItemAck: 04 | key | serial | n x {u32 item, u16 slot}.
            if (!g_equip_enabled || payload.size() < 9 || (payload.size() - 9) % 6 != 0) return;
            if (payload.size() == 15 && u32_at(payload, 9) == 0) {
                // A single removal: the MMO sent OtherUnequipItemAck for that.
                const uint16_t slot = static_cast<uint16_t>(payload[13] | (payload[14] << 8));
                enqueue_at(make_packet(off::kOpOtherUnequipItemAck,
                                       unequip_payload(kPuppetPlayer, slot, next_equip_serial(u32_at(payload, 5)))),
                           now + delay_ms, /*movement=*/false);
                g_equip_check_at = now + delay_ms + 1500;
                log::info("Puppet: equipment removed (slot {})", slot);
                return;
            }
            {
                const uint32_t serial = next_equip_serial(u32_at(payload, 5));
                std::memcpy(payload.data() + 5, &serial, 4);
            }
            // Not movement: a menu pause longer than the stale limit must not drop it.
            enqueue_at(make_packet(off::kOpOtherEquipItemAck, payload), now + delay_ms, /*movement=*/false);
            g_equip_check_at = now + delay_ms + 1500;
            log::info("Puppet: equipment change ({} slot(s))", (payload.size() - 9) / 6);
            return;

        case proto::EventKind::Relay: {
            // u16 opcode + a message the sender's server sent about the sender.
            uint16_t op = 0;
            std::memcpy(&op, payload.data(), 2);
            const off::RelayedAck* rule = nullptr;
            for (const auto& r : off::kRelayedAcks) {
                if (r.op == op) rule = &r;
            }
            if (!rule) return;
            Bytes body(payload.begin() + 2, payload.end());
            if (rule->size && body.size() != rule->size) return;
            // Secondary key fields (who caused it) name actors of the sender's
            // world, unknown or different here: make them the copy itself.
            for (uint16_t at : rule->other) {
                if (!at || at + 4 > body.size()) continue;
                uint32_t v;
                std::memcpy(&v, body.data() + at, 4);
                if (is_actor_key(v) && !is_puppet_key(v)) std::memcpy(body.data() + at, &kPuppetPlayer, 4);
            }
            if (has_foreign_key(body)) {
                if (g_relays_dropped++ % 50 == 0) {
                    log::info("Puppet: relayed opcode {} names another actor of the sender's world; "
                              "dropped ({} so far)",
                              op, g_relays_dropped);
                }
                return;
            }
            enqueue_at(make_packet(op, body), now + delay_ms, /*movement=*/true);
            return;
        }

        case proto::EventKind::Docking:
            // Resolved against the action the puppet is playing, which was
            // queued before with the same delay.
            if (payload.size() != 16) return;
            enqueue_at(make_packet(off::kOpChangeDockingSocketByFrameEventAck, payload), now + delay_ms,
                       /*movement=*/true);
            return;

        case proto::EventKind::WallUp: {
            // Must arrive just before the climb-up StartAction it belongs to.
            constexpr uint64_t kLead = 40;
            if (auto w = wallup_ack(payload)) {
                enqueue_at(make_packet(off::kOpUpdateWallUpDataAck, *w),
                           now + (delay_ms > kLead ? delay_ms - kLead : 0), /*movement=*/true);
                g_events.fetch_add(1);
            }
            return;
        }
    }
    if (!ack) {
        log::warn("Puppet: event kind {} has unexpected size {}; dropped",
                  static_cast<int>(ev.kind), ev.payload.size());
        return;
    }
    enqueue_at(make_packet(ack_op, *ack), now + delay_ms, /*movement=*/true);
    if (g_events.fetch_add(1) % 500 == 0) {
        log::info("Puppet: {} events replayed (ts shift {} ms, delay {} ms)", g_events.load(),
                  ts_shift_ms, delay_ms);
    }
}

uint64_t g_ride_forced_logged = 0;

// A copy that stays far from where its moves put it, while they keep coming,
// is stuck: load it again. Not while riding (the horse moves it) and at most
// every 20 s.
void watch_stuck() {
    static uint64_t off_since = 0, last_reload = 0;
    const uint64_t now = steady_ms();
    float told[3];
    uint64_t told_ms;
    AcquireSRWLockExclusive(&g_queue_lock);
    std::memcpy(told, g_told_pos, sizeof told);
    told_ms = g_told_ms;
    ReleaseSRWLockExclusive(&g_queue_lock);
    const auto at = puppet_spawned() ? game::actor_world_position(kPuppetPlayer) : std::nullopt;
    bool off_path = false;
    if (at && told_ms && now - told_ms < 1000 && now >= g_horse_ridden_until + 2000) {
        const float dx = at->x - told[0], dz = at->z - told[2];
        off_path = dx * dx + dz * dz > 6.0f * 6.0f;
    }
    if (!off_path) {
        off_since = 0;
        return;
    }
    if (!off_since) off_since = now;
    if (now - off_since < 4000 || now - last_reload < 20000) return;
    log::warn("Puppet: the copy is stuck ({:.1f} m from where its moves put it for 4 s); reloading it",
              std::sqrt((at->x - told[0]) * (at->x - told[0]) + (at->z - told[2]) * (at->z - told[2])));
    g_reload_request = true;
    last_reload = now;
    off_since = 0;
}

// What the copy's equipment component holds after an injection, to tell
// whether the client applied it: ClientEquipSlotActorComponent
// [[actor+0x68]+0x38], serial at +0x18, slot list [+0x90] (data +8, count
// +0x10, 0xD0-byte entries with the u16 slot at +0xC8; ropa-al-desmontar.md
// section 5.4). Read only, every read guarded.
void log_copy_gear() {
    const auto actor = game::find_actor(kPuppetPlayer);
    if (!actor) return;
    const auto comps = game::read_ptr(*actor + off::kActor_ComponentTable);
    const auto comp = comps ? game::read_ptr(*comps + 0x38) : std::nullopt;
    if (!comp) {
        log::info("Puppet: copy gear: equipment component not readable");
        return;
    }
    const auto serial = game::read<uint32_t>(*comp + 0x18);
    const auto list = game::read_ptr(*comp + 0x90);
    const auto data = list ? game::read_ptr(*list + 8) : std::nullopt;
    const auto count = list ? game::read<uint32_t>(*list + 0x10) : std::nullopt;
    std::string slots;
    if (data && count && *count <= 64) {
        for (uint32_t i = 0; i < *count; ++i) {
            if (const auto slot = game::read<uint16_t>(*data + i * 0xD0 + 0xC8)) slots += std::format(" {}", *slot);
        }
    }
    log::info("Puppet: copy gear now: serial {} | {} slot(s):{}", serial ? static_cast<int64_t>(*serial) : -1,
              count ? static_cast<int64_t>(*count) : -1, slots.empty() ? std::string(" ?") : slots);
}

void puppet_tick() {
    horse_diag_tick();
    watch_stuck();
    if (const uint64_t at = g_equip_check_at.load(); at && steady_ms() >= at) {
        g_equip_check_at = 0;
        log_copy_gear();
    }
    const uint64_t skipped = g_layers_skipped.load(std::memory_order_relaxed);
    if (skipped != g_layers_skipped_logged) {
        static uint64_t last_log = 0;
        const uint64_t now = steady_ms();
        if (g_layers_skipped_logged == 0 || now - last_log >= 5000) {
            log::info("Puppet: copy 0x{:08X} has no action yet; its animation update was skipped {} times",
                      g_layers_skipped_key.load(std::memory_order_relaxed), skipped);
            last_log = now;
            g_layers_skipped_logged = skipped;
        }
    }
    const uint64_t guard = g_layer_guard_fired.load(std::memory_order_relaxed);
    if (guard != g_layer_guard_logged && g_layer_guard_logged < 1000000) {
        static const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        static uint64_t last_log = 0;
        const uint64_t now = steady_ms();
        if (g_layer_guard_logged == 0 || now - last_log >= 5000) {
            const uintptr_t caller = g_layer_guard_caller.load(std::memory_order_relaxed);
            log::info("Puppet: crash guard: an action layer had no action ({} times, from exe+0x{:X})",
                      guard, caller >= base ? caller - base : caller);
            last_log = now;
            g_layer_guard_logged = guard;
        }
    }
    const uint64_t ride = g_ride_forced.load(std::memory_order_relaxed);
    if (ride != g_ride_forced_logged && g_ride_forced_logged < 50) {
        log::info("Puppet: mount/dismount enabled ({} times)", ride);
        g_ride_forced_logged = ride;
    }
    const uint64_t forced = g_climb_forced.load(std::memory_order_relaxed);
    if (forced != g_climb_forced_logged && g_climb_forced_logged < 50) {
        log::info("Puppet: wall climb enabled ({} times)", forced);
        g_climb_forced_logged = forced;
    }
    const uint64_t other = g_climb_other_fails.load(std::memory_order_relaxed);
    if (other != g_climb_other_logged && g_climb_other_logged < 20) {
        log::info("Puppet: PointClimb Enter failed for another actor (owner 0x{:08X}) [{}]",
                  g_climb_last_owner.load(std::memory_order_relaxed), other);
        g_climb_other_logged = other;
    }
}

bool puppet_take_injection(std::vector<uint8_t>& out) {
    if (!TryAcquireSRWLockExclusive(&g_queue_lock)) return false;
    // Movement that is long overdue (the game sat in a menu and did not
    // receive packets) would replay seconds of the past at once (capture
    // 183530: 722 packets, 14 s): dropped. Loads and states still go in.
    const uint64_t now = steady_ms();
    while (!g_queue.empty() && g_queue.front().movement && g_queue.front().due_ms + kStaleMovementMs < now) {
        g_queue.pop_front();
        ++g_stale_dropped;
    }
    const bool has = !g_queue.empty() && g_queue.front().due_ms <= now;
    if (has) {
        out = std::move(g_queue.front().packet);
        g_queue.pop_front();
        // MoveActorAck: u32 key | f32 x, y, z | ...
        if (out.size() >= 5 + 16 && out[0] == (off::kOpMoveActorAck & 0xFF) && out[1] == (off::kOpMoveActorAck >> 8)) {
            uint32_t key;
            std::memcpy(&key, out.data() + 5, 4);
            if (key == kPuppetPlayer || key == kPuppetBody) {
                std::memcpy(g_told_pos, out.data() + 9, sizeof g_told_pos);
                g_told_ms = now;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_queue_lock);
    return has;
}

bool puppet_install_layer_guards() {
    std::lock_guard lock(g_layer_guards_mutex);
    if (g_layer_guards_on.load()) return true;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    {
        uint8_t h[sizeof off::kActionLayerAdvanceHead];
        if (game::safe_read(base + off::kActionLayerAdvance, h, sizeof h) &&
            std::memcmp(h, off::kActionLayerAdvanceHead, sizeof h) == 0) {
            g_ret_stub = static_cast<uint8_t*>(
                VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
            if (g_ret_stub) {
                g_ret_stub[0] = 0xC3;
                DWORD old = 0;
                VirtualProtect(g_ret_stub, 16, PAGE_EXECUTE_READ, &old);
                FlushInstructionCache(GetCurrentProcess(), g_ret_stub, 16);
                auto hook = safetyhook::MidHook::create(reinterpret_cast<void*>(base + off::kActionLayerAdvance),
                                                        action_layer_advance);
                if (hook) g_layer_guard_hook = std::move(*hook);
                uint8_t h2[sizeof off::kActionLayersAdvanceHead];
                if (game::safe_read(base + off::kActionLayersAdvance, h2, sizeof h2) &&
                    std::memcmp(h2, off::kActionLayersAdvanceHead, sizeof h2) == 0) {
                    auto skip = safetyhook::MidHook::create(
                        reinterpret_cast<void*>(base + off::kActionLayersAdvance), action_layers_advance);
                    if (skip) g_layers_skip_hook = std::move(*skip);
                }
            }
        }
        if (!g_layer_guard_hook || !g_layers_skip_hook) {
            g_layer_guard_hook = {};
            g_layers_skip_hook = {};
            return false;
        }
        log::info("Puppet: action-layer crash guard installed");
    }
    g_layer_guards_on.store(true);
    return true;
}

void puppet_guard_also_key(uint32_t key) { g_guard_extra_key.store(key, std::memory_order_relaxed); }

void puppet_guard_owner(uint32_t user_key) { g_guard_owner.store(user_key, std::memory_order_relaxed); }

void puppet_guard_not_controlled_by(uint32_t local_user_key, PuppetGuardObserver observer) {
    // The observer first: the hook may see the key on another thread at once.
    g_guard_observer.store(observer, std::memory_order_relaxed);
    g_guard_local.store(local_user_key, std::memory_order_relaxed);
}

std::string puppet_guard_describe() {
    std::string s = std::format("layer guards {} | animation skipped {} (last 0x{:08X}) | empty layer guarded {}",
                                g_layer_guards_on.load() ? "on" : "off",
                                g_layers_skipped.load(std::memory_order_relaxed),
                                g_layers_skipped_key.load(std::memory_order_relaxed),
                                g_layer_guard_fired.load(std::memory_order_relaxed));
    if (const uint32_t local = g_guard_local.load(std::memory_order_relaxed)) {
        s += std::format(" | not controlled by 0x{:08X}: skipped {} (chart && !current only {}, chart && !playback "
                         "only {}, both {})",
                         local, g_guard_remote_skipped.load(std::memory_order_relaxed),
                         g_skip_no_current.load(std::memory_order_relaxed),
                         g_skip_no_playback.load(std::memory_order_relaxed),
                         g_skip_both.load(std::memory_order_relaxed));
    }
    return s;
}

uint8_t puppet_guard_last_reason() { return t_guard_reason; }

std::string puppet_guard_reason_text(uint8_t reason) {
    for (uint32_t i = 0; i < 2; ++i) {
        const uint8_t r = static_cast<uint8_t>((reason >> (2 * i)) & 3);
        if (!r) continue;
        return std::format("layer {} has a chart and {}", i,
                           r == 3   ? "neither a current action (+0x10) nor a playback (+0x18)"
                           : r == 1 ? "NO CURRENT ACTION (+0x10; its playback +0x18 is set)"
                                    : "NO PLAYBACK (+0x18; its current action +0x10 is set)");
    }
    return "a layer has a chart and no action (reason not recorded)";
}

bool puppet_install_climb_fix() {
    if (g_climb_enter_hook) return true;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const uintptr_t enter = base + off::kPointClimbEnter;
    uint8_t head[sizeof off::kPointClimbEnterHead];
    if (base != off::kImageBase || !game::safe_read(enter, head, sizeof head) ||
        std::memcmp(head, off::kPointClimbEnterHead, sizeof head) != 0) {
        log::warn("Puppet: PointClimb::Enter not recognised; wall climbs will freeze");
        return false;
    }
    g_climb_enter_hook = safetyhook::create_inline(reinterpret_cast<void*>(enter),
                                                   reinterpret_cast<void*>(point_climb_enter));
    const bool ok = static_cast<bool>(g_climb_enter_hook);
    log::info("Puppet: wall-climb fix {}", ok ? "installed" : "FAILED to install");
    // Mount / dismount (same idea). Both share one prologue. Only with the
    // horse: without one, the copy stayed stuck in the dismount pose.
    if (!g_horse_enabled) return ok;
    // A horse copy must never load without the guard (caballo-coop.md §9.4 R1).
    if (!puppet_install_layer_guards()) {
        g_horse_enabled = false;
        log::warn("Puppet: action-layer crash guard not installed; horse copy disabled");
        return ok;
    }
    for (auto [rva, hook, fn, name] :
         {std::tuple{off::kRideOnEnter, &g_ride_on_hook, reinterpret_cast<void*>(ride_on_enter), "RideOn"},
         }) {
        uint8_t h[sizeof off::kRideEnterHead];
        if (!game::safe_read(base + rva, h, sizeof h) || std::memcmp(h, off::kRideEnterHead, sizeof h) != 0) {
            log::warn("Puppet: {}::Enter not recognised; mounting may freeze the other player", name);
            continue;
        }
        *hook = safetyhook::create_inline(reinterpret_cast<void*>(base + rva), fn);
        log::info("Puppet: {} fix {}", name, *hook ? "installed" : "FAILED to install");
    }
    return ok;
}

void puppet_set_offset(float metres) {
    g_offset_x = metres;
    log::info("Puppet offset set to {:.2f} m", metres);
}

}  // namespace cdmp::net

namespace cdmp::net {
void puppet_set_horse_enabled(bool on) {
    g_horse_enabled = on;
    log::info("Puppet: horse {}", on ? "enabled (experimental)" : "disabled");
}

void puppet_set_equip_enabled(bool on) {
    g_equip_enabled = on;
    log::info("Puppet: equipment relay {}", on ? "enabled (experimental)" : "disabled");
}
}  // namespace cdmp::net
