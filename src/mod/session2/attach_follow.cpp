#include "mod/session2/attach_follow.h"
#include "mod/game/build_profile.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "mod/log.h"
#include "mod/session2/guest.h"
#include "mod/session2/attach_follow_logic.h"
#include "mod/session2/child_hold.h"
#include "mod/session2/observer_hold_acks.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

using afl::Ev;
using afl::Mode;

std::atomic<uint8_t> g_mode{0};
std::atomic<bool> g_registry_on{false};
afl::Registry<32> g_registry;

// Out: the host's children -> user 2 (take_frame, several game threads). In: user 2's -> host (inject,
// the mod's network thread). Each behind a try-lock: a busy tracker skips the packet (counted).
struct Dir {
    SRWLOCK lock = SRWLOCK_INIT;
    afl::Tracker<8> t;
    std::atomic<uint64_t> actions{0}, char_moves{0}, makes{0}, made{0}, push_failed{0}, build_failed{0}, busy{0},
        expired{0}, owner_unknown{0}, child_moves_detached{0};
    std::atomic<uint64_t> ev[afl::kEvents] = {};
    afl::ErrHist same_local, path_world, next_local, next_rel;
    std::atomic<uint32_t> last_child{0}, last_parent{0};
    std::atomic<uint64_t> measure_logged{0}, other_logged{0};
    std::atomic<size_t> attached{0};  // t.size() after the last locked change (read without the lock)
    // Detaches that found the tracker busy: applied first by the next thread that takes it.
    afl::PendingDetach<16> pending;
    std::atomic<uint64_t> deferred_detaches{0}, applied_detaches{0}, lost_detaches{0}, held_skips{0};
};
Dir g_out, g_in;

// The tracker's lock, bounded: a few try-locks with a pause between them (each holder keeps it for a
// few hundred instructions), never a wait that could block a game thread. Taken: the deferred detaches
// are applied first. False: busy (counted; the caller defers a detach, an attach waits for the next action).
bool lock_dir(Dir& d) {
    for (int i = 0; i < 64; ++i) {
        if (TryAcquireSRWLockExclusive(&d.lock)) {
            if (!d.pending.empty()) {
                const size_t n = d.pending.drain([&](uint32_t k) { (void)d.t.drop(k); });
                d.applied_detaches.fetch_add(n, std::memory_order_relaxed);
            }
            return true;
        }
        YieldProcessor();
    }
    d.busy.fetch_add(1, std::memory_order_relaxed);
    return false;
}
void unlock_dir(Dir& d) {
    d.attached.store(d.t.size(), std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&d.lock);
}
// A detach that could not take the lock: never lost while there is room (16 keys).
void defer_detach(Dir& d, uint32_t key) {
    if (d.pending.push(key)) {
        d.deferred_detaches.fetch_add(1, std::memory_order_relaxed);
    } else {
        d.lost_detaches.fetch_add(1, std::memory_order_relaxed);
    }
}

std::atomic<uint64_t> g_reg_attach{0}, g_reg_detach{0};

// ---------------------------------------------------------------- lines (formatted on the network thread)

enum Note : uint16_t { kNMeasure, kNOther };
p8p::EventRing<256> g_notes;
std::atomic<uint64_t> g_notes_lost{0};
constexpr uint64_t kMeasureLines = 24, kOtherLines = 24;

int32_t mm(float metres) {
    if (!std::isfinite(metres)) return INT32_MIN;
    const double v = std::round(static_cast<double>(metres) * 1000.0);
    return v > 2e9 ? INT32_MAX : v < -2e9 ? INT32_MIN + 1 : static_cast<int32_t>(v);
}
uint64_t pack2(int32_t a, int32_t b) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
}
int32_t hi(uint64_t v) { return static_cast<int32_t>(static_cast<uint32_t>(v >> 32)); }
int32_t lo(uint64_t v) { return static_cast<int32_t>(static_cast<uint32_t>(v)); }

void push_note(Note kind, std::initializer_list<uint64_t> v) {
    p8p::Event e;
    e.kind = kind;
    e.tid = GetCurrentThreadId();
    size_t i = 0;
    for (const uint64_t x : v) {
        if (i < 12) e.v[i++] = x;
    }
    if (!g_notes.push(e)) g_notes_lost.fetch_add(1, std::memory_order_relaxed);
}

const char* dir_name(uint64_t in) { return in ? "user 2's -> host (measured only)" : "host's -> user 2"; }
std::string err_mm(int32_t v) { return v < 0 ? std::string("-") : std::format("{}", v); }

std::string note_text(const p8p::Event& e) {
    const uint64_t* v = e.v;
    const auto ev = static_cast<Ev>(v[0] & 0xFF);
    switch (e.kind) {
        case kNMeasure:
            return std::format(
                "AttachFollow [{}]: 0x{:08X} {} on 0x{:08X} (hash 0x{:08X} / 0x{:08X}) | Path local +0xD7 pos mm ({}, "
                "{}, {}), |local| {} mm, quat w {:.4f} | action pos in the parent's frame mm {} | errors mm: parent x "
                "local {}, Path world x local {}, previous local applied now {}, previous relative pose applied now {} | "
                "action ts - parent Move ts {} ms (T{})",
                dir_name(v[0] >> 8), v[1], afl::ev_name(ev), v[2], static_cast<uint32_t>(v[3]),
                static_cast<uint32_t>(v[3] >> 32), hi(v[4]), lo(v[4]), hi(v[5]),
                lo(v[5]), static_cast<double>(lo(v[10])) / 1e6,
                v[11] ? std::format("({}, {}, {})", hi(v[6]), lo(v[6]), hi(v[7])) : std::string("- (no Move of it yet)"),
                err_mm(hi(v[8])), err_mm(lo(v[8])), err_mm(hi(v[9])), err_mm(lo(v[9])), hi(v[10]), e.tid);
        case kNOther:
            return std::format("AttachFollow [{}]: 0x{:08X} {} (parent 0x{:08X}){}", dir_name(v[0] >> 8), v[1],
                               ev == Ev::Ignored ? "owner unreadable: not followed" : afl::ev_name(ev), v[2],
                               ev == Ev::NotOwned ? " - its owner is not that user: never followed" : "");
        default:
            return std::format("AttachFollow: note {}?", e.kind);
    }
}

// ---------------------------------------------------------------- one direction

// The child's owner is that user (the host for the outgoing direction, user 2 for the incoming one).
// nullopt: unreadable (child_hold's hooks not installed, or the actor not found).
std::optional<bool> owner_is(uint32_t child, bool in) {
    const auto owner = hold_actor_owner(child);
    if (!owner) return std::nullopt;
    if (!*owner) return false;
    uintptr_t user = 0;
    const bool ok = in ? server_user2(user) : server_host_user(user);
    if (!ok || !user) return std::nullopt;
    return *owner == user;
}

void on_action(Dir& d, const afl::Action& a, bool in) {
    const bool rides = afl::char_parent(a.parent) && !afl::dismount(a);
    // Every NPC's StartAction passes here: without a character parent it matters only to an attachment.
    if (!rides && !d.attached.load(std::memory_order_relaxed)) return;
    d.actions.fetch_add(1, std::memory_order_relaxed);
    bool owner_ok = false;
    if (rides) {
        const auto o = owner_is(a.key, in);
        if (!o) {
            d.owner_unknown.fetch_add(1, std::memory_order_relaxed);
            if (d.other_logged.fetch_add(1, std::memory_order_relaxed) < kOtherLines) {
                push_note(kNOther, {(static_cast<uint64_t>(in) << 8) | static_cast<uint8_t>(Ev::Ignored), a.key,
                                    static_cast<uint32_t>(a.parent)});
            }
            // Never followed without knowing it is a user's child: an attachment it had (to this or another
            // parent, with an older Path local) ends here too.
            if (d.attached.load(std::memory_order_relaxed)) {
                if (lock_dir(d)) {
                    (void)d.t.drop(a.key);
                    unlock_dir(d);
                } else {
                    defer_detach(d, a.key);
                }
            }
            return;
        }
        owner_ok = *o;
    }
    if (!lock_dir(d)) {
        // Busy: a detach is deferred (applied by the next holder); an attach waits for the next action.
        if (!rides || !owner_ok) defer_detach(d, a.key);
        return;
    }
    afl::Measure m;
    const Ev ev = d.t.on_action(a, GetTickCount64(), owner_ok, &m);
    unlock_dir(d);
    d.ev[static_cast<size_t>(ev)].fetch_add(1, std::memory_order_relaxed);
    if (ev == Ev::Ignored) return;
    const uint64_t head = (static_cast<uint64_t>(in) << 8) | static_cast<uint8_t>(ev);
    if (ev == Ev::Attached || ev == Ev::Refreshed || ev == Ev::BadLocal) {
        d.last_child.store(a.key, std::memory_order_relaxed);
        d.last_parent.store(static_cast<uint32_t>(a.parent), std::memory_order_relaxed);
        d.same_local.add(m.same_local);
        d.path_world.add(m.path_world);
        d.next_local.add(m.next_local);
        d.next_rel.add(m.next_rel);
        if (d.measure_logged.fetch_add(1, std::memory_order_relaxed) < kMeasureLines) {
            const int64_t dt = std::clamp<int64_t>(m.dt_ms, INT32_MIN, INT32_MAX);
            push_note(kNMeasure,
                      {head, a.key, static_cast<uint32_t>(a.parent), pack2(static_cast<int32_t>(a.hash1),
                                                                            static_cast<int32_t>(a.hash0)),
                       pack2(mm(a.local.p.x), mm(a.local.p.y)), pack2(mm(a.local.p.z), mm(afl::len(a.local.p))),
                       pack2(mm(m.rel.p.x), mm(m.rel.p.y)), pack2(mm(m.rel.p.z), 0),
                       pack2(m.same_local < 0 ? -1 : mm(m.same_local), m.path_world < 0 ? -1 : mm(m.path_world)),
                       pack2(m.next_local < 0 ? -1 : mm(m.next_local), m.next_rel < 0 ? -1 : mm(m.next_rel)),
                       pack2(static_cast<int32_t>(dt), static_cast<int32_t>(std::lround(a.local.q.w * 1e6))),
                       static_cast<uint64_t>(m.have_k)});
        }
        return;
    }
    if (d.other_logged.fetch_add(1, std::memory_order_relaxed) < kOtherLines) {
        push_note(kNOther, {head, a.key, static_cast<uint32_t>(a.parent)});
    }
}

// A character's Move: the attached children's poses (Makes) for this one.
size_t on_char_move(Dir& d, const afl::Move& mv, std::array<afl::Make, 8>& makes) {
    d.char_moves.fetch_add(1, std::memory_order_relaxed);
    // Always noted (even with nothing attached): an attaching action is measured against it.
    if (!lock_dir(d)) return 0;  // this Move's Makes are skipped; the next one makes them
    size_t expired = 0;
    const size_t n = d.t.on_char_move(mv, GetTickCount64(), afl::kEvery, makes, &expired);
    unlock_dir(d);
    if (expired) d.expired.fetch_add(expired, std::memory_order_relaxed);
    d.makes.fetch_add(n, std::memory_order_relaxed);
    return n;
}

void on_child_move(Dir& d, const afl::Move& mv) {
    if (!d.attached.load(std::memory_order_relaxed)) return;  // nothing attached (the common case)
    if (!lock_dir(d)) {
        if (!afl::char_parent(mv.parent)) defer_detach(d, mv.key);  // its own Move with parent -1: it got down
        return;
    }
    const bool gone = d.t.on_child_move(mv.key, mv.parent);
    unlock_dir(d);
    if (gone) d.child_moves_detached.fetch_add(1, std::memory_order_relaxed);
}

void registry_feed(uint16_t opcode, std::span<const uint8_t> payload) {
    const afl::RegEvent r = afl::registry_event(opcode, payload, game::current_build());
    if (r.what == afl::Reg::Attach) {
        g_registry.attach(r.key, GetTickCount64());
        g_reg_attach.fetch_add(1, std::memory_order_relaxed);
    } else if (r.what == afl::Reg::Detach && g_registry.attached(r.key, GetTickCount64())) {
        g_registry.detach(r.key);
        g_reg_detach.fetch_add(1, std::memory_order_relaxed);
    }
}

std::string hist(const afl::ErrHist& h) {
    std::string s;
    for (size_t i = 0; i < h.n.size(); ++i) {
        s += std::format("{}{}{} {}", s.empty() ? "" : ", ", i < afl::kErrBounds.size() ? "<=" : ">",
                         i < afl::kErrBounds.size() ? afl::kErrBounds[i] : afl::kErrBounds.back(),
                         h.n[i].load(std::memory_order_relaxed));
    }
    return s;
}

std::string dir_text(const Dir& d, bool in) {
    auto ev = [&](Ev e) { return d.ev[static_cast<size_t>(e)].load(); };
    return std::format(
        "{}: StartActions {} (attached {}, still attached {}, Path local not an offset {}, detached {}, not a user's "
        "child {}; owner unreadable {}) | parent Moves {} -> child Moves {} {} (made {}, skipped while the child is "
        "withheld {}, push refused {}, build failed {}) | detached by its own Move {}, stale {} | busy {} (detaches "
        "deferred {}, applied {}, lost {}) | last 0x{:08X} on 0x{:08X} | errors mm: parent x local "
        "[{}] | Path world x local [{}] | previous local applied later [{}] | previous relative pose applied later [{}]",
        dir_name(in), d.actions.load(), ev(Ev::Attached), ev(Ev::Refreshed), ev(Ev::BadLocal), ev(Ev::Detached),
        ev(Ev::NotOwned), d.owner_unknown.load(), d.char_moves.load(), d.makes.load(),
        in || static_cast<Mode>(g_mode.load()) != Mode::On ? "would be made" : "due", d.made.load(),
        d.held_skips.load(), d.push_failed.load(), d.build_failed.load(), d.child_moves_detached.load(),
        d.expired.load(), d.busy.load(), d.deferred_detaches.load(), d.applied_detaches.load(), d.lost_detaches.load(),
        d.last_child.load(), d.last_parent.load(), hist(d.same_local), hist(d.path_world), hist(d.next_local),
        hist(d.next_rel));
}

}  // namespace

bool attach_follow_configure(const std::string& raw) {
    const auto p = afl::parse_mode(raw);
    if (!p.known) {
        log::warn("AttachFollow: attach_follow='{}' is not 0, probe or 1; it stays off", raw);
        return false;
    }
    g_mode.store(static_cast<uint8_t>(p.mode));
    if (p.mode == Mode::Off) return false;
    log::info("AttachFollow ON ({}): a user's child (0xB) whose last StartAction names a character (0xA) as its Path "
              "parent (+0x116/+0x117) rides it: {}; user 2's children riding user 2 (the host's view) are measured "
              "only, in every mode; attachments end with another parent, a Move of the child with parent -1, the "
              "dismount hash 0x{:08X}, or {} s without an attached action{}",
              afl::mode_name(p.mode),
              p.mode == Mode::On ? "every 3rd MoveActorAck of the character to user 2 is followed by a synthetic 2697 "
                                   "of the child (character pose x the action's Path local +0xD7)"
                                 : "probe: the Path local hypothesis is measured against each action's own position, "
                                   "nothing is made",
              afl::kDismountHash, afl::kStaleMs / 1000,
              child_hold_active() ? "" : " | WARNING: child_hold's hooks are not installed: owners unreadable, nothing "
                                         "is followed");
    return true;
}

bool attach_follow_active() { return g_mode.load(std::memory_order_relaxed) != 0; }

void attach_follow_on_out(uint16_t opcode, std::span<const uint8_t> packet, AttachPushFn push) noexcept {
    const auto mode = static_cast<Mode>(g_mode.load(std::memory_order_relaxed));
    if (mode == Mode::Off || packet.size() < afl::kHeader) return;
    if (opcode != afl::kOpStartActionAck && opcode != afl::kOpMoveActorAck) return;
    try {
        const std::span<const uint8_t> payload = packet.subspan(afl::kHeader);
        if (opcode == afl::kOpStartActionAck) {
            const afl::Action a = afl::read_action(payload, true, game::current_build());
            if (a.ok && afl::child_key(a.key)) on_action(g_out, a, false);
            return;
        }
        const afl::Move mv = afl::read_move(payload, afl::kMoveAckSize, game::current_build());
        if (!mv.ok) return;
        if (afl::child_key(mv.key)) {
            on_child_move(g_out, mv);
            return;
        }
        if (!afl::char_key(mv.key)) return;
        std::array<afl::Make, 8> makes{};
        const size_t n = on_char_move(g_out, mv, makes);
        if (!n || mode != Mode::On || !push) return;
        std::vector<uint8_t> child;  // the mod's heap, never the game's
        const uintptr_t s2 = server_live_session2();
        for (size_t i = 0; i < n; ++i) {
            // Never ahead of the child's own 2005: while child_hold (D-53, child_settle) withholds it from user 2
            // or observer_hold_acks keeps its packets, nothing is made (busy locks count as withheld).
            if (child_hold_withheld(makes[i].child, s2) || observer_hold_acks_holding(makes[i].child)) {
                g_out.held_skips.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (!afl::make_child_ack(packet, makes[i].child, makes[i].pose, child, game::current_build())) {
                g_out.build_failed.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (push(child)) {
                g_out.made.fetch_add(1, std::memory_order_relaxed);
            } else {
                g_out.push_failed.fetch_add(1, std::memory_order_relaxed);
            }
        }
    } catch (...) {
        g_out.build_failed.fetch_add(1, std::memory_order_relaxed);
    }
}

void attach_follow_on_guest_req(uint16_t opcode, std::span<const uint8_t> payload) noexcept {
    const bool follow = g_mode.load(std::memory_order_relaxed) != 0;
    const bool registry = g_registry_on.load(std::memory_order_relaxed);
    if (!follow && !registry) return;
    if (opcode != afl::kOpStartActionReq && opcode != afl::kOpMoveActorReq) return;
    try {
        // The host's client gets these as Acks through session 1 (native): its remote_interp reads the registry.
        if (registry) registry_feed(opcode, payload);
        if (!follow) return;
        if (opcode == afl::kOpStartActionReq) {
            const afl::Action a = afl::read_action(payload, false, game::current_build());
            if (a.ok && afl::child_key(a.key)) on_action(g_in, a, true);
            return;
        }
        const afl::Move mv = afl::read_move(payload, afl::kMoveReqSize, game::current_build());
        if (!mv.ok) return;
        if (afl::child_key(mv.key)) {
            on_child_move(g_in, mv);
            return;
        }
        if (!afl::char_key(mv.key)) return;
        std::array<afl::Make, 8> makes{};
        const size_t n = on_char_move(g_in, mv, makes);
        // Never sent (this direction stays measured): the Req is built only to know 1 could.
        std::vector<uint8_t> req;
        for (size_t i = 0; i < n; ++i) {
            if (!afl::make_child_req(payload, makes[i].child, makes[i].pose, req, game::current_build())) {
                g_in.build_failed.fetch_add(1, std::memory_order_relaxed);
            }
        }
    } catch (...) {
        g_in.build_failed.fetch_add(1, std::memory_order_relaxed);
    }
}

void attach_follow_on_epoch() noexcept {
    for (Dir* d : {&g_out, &g_in}) {
        AcquireSRWLockExclusive(&d->lock);  // the network thread; the holders never block inside
        d->t.clear();
        (void)d->pending.drain([](uint32_t) {});
        d->attached.store(0, std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&d->lock);
    }
    g_registry.clear();
}

void attach_follow_tick() noexcept {
    if (!attach_follow_active()) return;
    try {
        p8p::Event e;
        while (g_notes.pop(e)) log::info("{}", note_text(e));
        static uint64_t lost_logged = 0;
        if (const uint64_t lost = g_notes_lost.load(); lost != lost_logged) {
            log::warn("AttachFollow: {} log line(s) lost (ring full)", lost - lost_logged);
            lost_logged = lost;
        }
    } catch (...) {
    }
}

std::string attach_follow_describe() {
    const auto mode = static_cast<Mode>(g_mode.load());
    if (mode == Mode::Off) return {};
    return std::format("AttachFollow [{}]: {} || {} || registry: attached {} (noted {}, ended {})", afl::mode_name(mode),
                       dir_text(g_out, false), dir_text(g_in, true), g_registry.count(GetTickCount64()),
                       g_reg_attach.load(), g_reg_detach.load());
}

void attach_registry_enable(bool on) { g_registry_on.store(on); }

void attach_registry_on_in(uint16_t opcode, std::span<const uint8_t> payload) noexcept {
    if (!g_registry_on.load(std::memory_order_relaxed)) return;
    // P8 re-entry (s25/reentrada.md §1.4): the guest's registry (the host's actors riding a character) belongs to one
    // remote conversation; the mod's network thread (its only writer) starts it again at the new one's first packet.
    static uint32_t conv_seen = 0;
    if (const uint32_t conv = guest_conversation_id(); conv != conv_seen) {
        conv_seen = conv;
        if (conv >= 2) g_registry.clear();
    }
    if (opcode != afl::kOpStartActionAck && opcode != afl::kOpMoveActorAck) return;
    try {
        registry_feed(opcode, payload);
    } catch (...) {
    }
}

bool attach_registry_attached(uint32_t key, uint64_t now_ms) noexcept {
    return g_registry_on.load(std::memory_order_relaxed) && g_registry.attached(key, now_ms);
}

size_t attach_registry_count(uint64_t now_ms) noexcept { return g_registry.count(now_ms); }

}  // namespace cdmp::session2
