// P8 mounts, mount_sync (src/mod/session2/mount_sync_logic.h, docs/research/pendientes/p8-impl-monturas.md): the
// pure logic. The hooks, the native calls and their effects need the game.

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#include "mod/session2/mount_sync_logic.h"
#include "mod/session2/ride_filter_logic.h"

using namespace cdmp::session2;
using namespace cdmp::session2::msl;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

// Session 13's keys: H rider / horse, G rider / horse, the users.
constexpr uint32_t kHRider = 0xA0100001, kHHorse = 0xB0100003, kGRider = 0xA0100318, kGHorse = 0xB010031A;
constexpr uint32_t kHUser = 0x90100000, kGUser = 0x90100317;

Event ev(EvKind k, uint32_t transition, uint32_t nonce = 0x1234, uint32_t rider = kGRider, uint32_t horse = kGHorse) {
    Event e;
    e.kind = k;
    e.rider = rider;
    e.horse = horse;
    e.seat = 0;
    e.fifth = 0;
    e.transition = transition;
    e.nonce = nonce;
    e.flags = k == EvKind::Begin ? 0 : kFlagOk;
    e.tick_ms = 0x1122334455667788ull;
    e.caller_rva = 0x9BCD1F0;
    e.aux = 1;
    return e;
}

void test_wire() {
    Event e = ev(EvKind::Commit, 7);
    e.seat = 1;
    e.fifth = 1;
    e.result = 0;
    const auto f = frame(e);
    CHECK(f.size() == kHeader + kPayloadSize);
    CHECK(is_frame(f));
    CHECK(f[0] == 1 && f[1] == 0 && f[2] == 0xFF && f[3] == kPayloadSize && f[4] == 0 && f[5] == kControlMountSync);
    const auto d = decode(std::span<const uint8_t>(f).subspan(kHeader));
    CHECK(d.has_value());
    if (d) {
        CHECK(d->kind == EvKind::Commit && d->rider == kGRider && d->horse == kGHorse && d->seat == 1 && d->fifth == 1);
        CHECK(d->transition == 7 && d->nonce == 0x1234 && d->tick_ms == 0x1122334455667788ull);
        CHECK(d->caller_rva == 0x9BCD1F0 && d->aux == 1 && d->flags == kFlagOk);
    }
    // Not ours: another state, a wrong size, a wrong version, bad keys, a seat out of range, transition 0.
    std::vector<uint8_t> g = f;
    g[kHeader] = 4;
    CHECK(!is_frame(g));
    std::vector<uint8_t> h(f.begin(), f.end() - 1);
    CHECK(!is_frame(h));
    auto p = encode(e);
    p[1] = 2;
    CHECK(!decode(p));
    p = encode(e);
    p[2] = 4;
    CHECK(!decode(p));
    Event bad = e;
    bad.horse = kGRider;
    CHECK(!decode(encode(bad)));
    bad = e;
    bad.rider = kGHorse;
    CHECK(!decode(encode(bad)));
    bad = e;
    bad.seat = kMaxSeats;
    CHECK(!decode(encode(bad)));
    bad = e;
    bad.transition = 0;
    CHECK(!decode(encode(bad)));
    // The reannounce frame (state 3, 10 bytes) is never taken as ours.
    const uint8_t rean[] = {1, 0, 0xFF, 10, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(!is_frame(rean));
}

void test_publishable() {
    CHECK(publishable(kHRider, kHHorse, 0, kHRider));
    CHECK(!publishable(kHRider, kHHorse, 0, 0));          // own key unknown
    CHECK(!publishable(kGRider, kGHorse, 0, kHRider));    // not this client's character
    CHECK(!publishable(kHRider, 0xC0000001, 0, kHRider)); // not a 0xB vehicle
    CHECK(!publishable(kHRider, kHHorse, kMaxSeats, kHRider));
}

PairState ready() {
    PairState s;
    s.rider_found = s.horse_found = s.rider_vc = s.horse_vc = s.link = true;
    s.horse_has_seats = 1;
    s.rider_ctrl = s.horse_ctrl = kGUser;
    s.count = 0;
    return s;
}

void test_judge() {
    PairState s = ready();
    CHECK(judge_commit(s, kGRider, kGHorse, 0, 0) == Judge::Apply);
    PairState t = s;
    t.rider_found = false;
    CHECK(judge_commit(t, kGRider, kGHorse, 0, 0) == Judge::NoRider && judge_retry(Judge::NoRider));
    t = s;
    t.horse_found = false;
    CHECK(judge_commit(t, kGRider, kGHorse, 0, 0) == Judge::NoHorse);
    t = s;
    t.link = false;
    CHECK(judge_commit(t, kGRider, kGHorse, 0, 0) == Judge::NoVehicle);
    t = s;
    t.horse_has_seats = 0;
    CHECK(judge_commit(t, kGRider, kGHorse, 0, 0) == Judge::NoSeats && !judge_retry(Judge::NoSeats));
    // Idempotent: already there.
    t = s;
    t.cur_vehicle = kGHorse;
    t.cur_seat = 0;
    t.occupant = kGRider;
    t.count = 1;
    CHECK(judge_commit(t, kGRider, kGHorse, 0, 0) == Judge::AlreadyApplied);
    // The same link on another seat is not "already".
    CHECK(judge_commit(t, kGRider, kGHorse, 1, 0) != Judge::AlreadyApplied);
    t = s;
    t.occupant = kHRider;
    CHECK(judge_commit(t, kGRider, kGHorse, 0, 0) == Judge::SeatTaken);
    t = s;
    t.cur_vehicle = kHHorse;
    CHECK(judge_commit(t, kGRider, kGHorse, 0, 0) == Judge::RiderLinkedElsewhere);
    t = s;
    t.horse_ctrl = kHUser;
    CHECK(judge_commit(t, kGRider, kGHorse, 0, 0) == Judge::ControllerMismatch);
    t = s;
    t.rider_ctrl = t.horse_ctrl = 0;
    CHECK(judge_commit(t, kGRider, kGHorse, 0, 0) == Judge::ControllerMismatch);
    t = s;
    t.rider_c8 = 1;
    CHECK(judge_commit(t, kGRider, kGHorse, 0, 0) == Judge::RiderLocal);
    // vf53's action branch: seat 0, fifth != 1, horse C8 != 0. Remote actors have C8 0 (session 8's S3 probe).
    t = s;
    t.horse_c8 = 2;
    CHECK(judge_commit(t, kGRider, kGHorse, 0, 0) == Judge::HorseActionBranch);
    CHECK(judge_commit(t, kGRider, kGHorse, 0, 1) == Judge::Apply);
    CHECK(judge_commit(t, kGRider, kGHorse, 1, 0) == Judge::Apply);
    // Review (2): any hop vf53 dereferences unchecked missing -> not ready (retry), never a call.
    for (uint32_t bit : {kDepRiderComps, kDepRiderCC, kDepRiderCCSlot, kDepRiderComps50, kDepHorseComps,
                         kDepHorseComps1A0, kDepHorseComps90, kDepHorse88, kDepHorseCC, kDepHorseCompsB0}) {
        t = s;
        t.deps_missing = bit;
        CHECK(judge_commit(t, kGRider, kGHorse, 0, 0) == Judge::NoVehicle);
    }
    // Review (3): linked to another horse BY THIS CLIENT -> retire that first; by anyone else -> refused.
    t = s;
    t.cur_vehicle = kHHorse;
    CHECK(judge_commit(t, kGRider, kGHorse, 0, 0, kHHorse) == Judge::RetireFirst);
    CHECK(judge_commit(t, kGRider, kGHorse, 0, 0, 0xB0100999) == Judge::RiderLinkedElsewhere);
    CHECK(!judge_retry(Judge::RetireFirst));
}

// Review (1): 0xE6F2E60 dereferences the horse's comps and vc unchecked: called only on a whole pair.
void test_detach_guard() {
    PairState s = ready();
    s.cur_vehicle = kGHorse;
    s.cur_seat = 0;
    s.occupant = kGRider;
    s.count = 1;
    CHECK(detach_decide(s, kGHorse) == DetachCall::Call);
    PairState t = s;
    t.cur_vehicle = 0;
    CHECK(detach_decide(t, kGHorse) == DetachCall::AlreadyGone);
    t = s;
    t.horse_vc = false;
    CHECK(detach_decide(t, kGHorse) == DetachCall::Refused);
    t = s;
    t.horse_found = false;
    CHECK(detach_decide(t, kGHorse) == DetachCall::Refused);
    t = s;
    t.horse_has_seats = 0;
    CHECK(detach_decide(t, kGHorse) == DetachCall::Refused);
    t = s;
    t.horse_c8 = 1;
    CHECK(detach_decide(t, kGHorse) == DetachCall::Refused);
    t = s;
    t.link = false;  // the rider's link unreadable: nothing can be said, nothing called
    CHECK(detach_decide(t, kGHorse) == DetachCall::Refused);
    t = s;
    t.rider_vc = false;
    CHECK(detach_decide(t, kGHorse) == DetachCall::Refused);
}

void test_verify() {
    const PairState b = ready();
    PairState a = b;
    a.cur_vehicle = kGHorse;
    a.cur_seat = 0;
    a.occupant = kGRider;
    a.count = 1;
    CHECK(verify_commit(b, a, kGRider, kGHorse, 0, 0) == Post::Ok);
    CHECK(verify_commit(b, a, kGRider, kGHorse, 0, 5) == Post::Result);
    PairState x = a;
    x.cur_seat = 1;
    CHECK(verify_commit(b, x, kGRider, kGHorse, 0, 0) == Post::Link);
    x = a;
    x.occupant = 0;
    CHECK(verify_commit(b, x, kGRider, kGHorse, 0, 0) == Post::Occupant);
    x = a;
    x.count = 2;
    CHECK(verify_commit(b, x, kGRider, kGHorse, 0, 0) == Post::Count);
    x = a;
    x.res_vehicle = kGHorse;
    CHECK(verify_commit(b, x, kGRider, kGHorse, 0, 0) == Post::Reserve);
    x = a;
    x.horse_ctrl = kHUser;
    CHECK(verify_commit(b, x, kGRider, kGHorse, 0, 0) == Post::Controller);
    x = a;
    x.rider_c8 = 2;
    CHECK(verify_commit(b, x, kGRider, kGHorse, 0, 0) == Post::C8);
    // Already occupied by the rider before (a re-commit): the count does not grow.
    PairState b2 = a;
    CHECK(verify_commit(b2, a, kGRider, kGHorse, 0, 0) == Post::Ok);
    // The controller accepts its own C8 changing (local horse).
    x = a;
    x.horse_c8 = 1;
    CHECK(controller_commit_ok(b, x, kGRider, kGHorse, 0, 0));
    CHECK(!controller_commit_ok(b, b, kGRider, kGHorse, 0, 0));
    // Detach.
    PairState d = b;
    CHECK(verify_detach(a, d, kGRider, kGHorse, 0) == Post::Ok);
    CHECK(verify_detach(a, a, kGRider, kGHorse, 0) == Post::Link);
    CHECK(verify_detach(a, d, kGRider, kGHorse, 3) == Post::Result);
    PairState e = d;
    e.count = 1;
    CHECK(verify_detach(a, e, kGRider, kGHorse, 0) == Post::Count);
}

void test_table_mount() {
    Table t;
    // Unknown remote key / another rider: nothing.
    CHECK(t.on_event(ev(EvKind::Begin, 1), 100, 0) == Accept::NotRemote);
    CHECK(t.on_event(ev(EvKind::Begin, 1), 100, kHRider) == Accept::NotRemote);
    CHECK(!t.holding(100));
    // Begin: the stream is held, ride_filter defers.
    CHECK(t.on_event(ev(EvKind::Begin, 1), 100, kGRider) == Accept::Begun);
    CHECK(t.holding(100) && t.holding(599) && !t.holding(600));
    CHECK(t.defers(kGRider, 300) && !t.defers(kHRider, 300));
    CHECK(t.on_event(ev(EvKind::Begin, 1), 101, kGRider) == Accept::Dup);
    CHECK(t.plan(kGRider, 110).plan == Plan::None);  // nothing to apply before the Commit
    // Commit: no more hold; applied at the first dispatch (any actor's), at the latest at the rider's own action.
    CHECK(t.on_event(ev(EvKind::Commit, 1), 120, kGRider) == Accept::Committed);
    CHECK(!t.holding(120) && t.has_work() && t.defers(kGRider, 130));
    CHECK(t.on_event(ev(EvKind::Commit, 1), 121, kGRider) == Accept::Dup);
    auto w = t.plan(0xB0000001, 130);  // another actor's action: before the rider's mount action
    CHECK(w.plan == Plan::Commit && w.txn.seat == 0 && w.txn.horse == kGHorse && w.txn.transition == 1 &&
          !w.txn.claimed_at_rider);
    CHECK(t.plan(kGRider, 141).plan == Plan::None);  // claimed: never twice
    t.finish_commit(kGRider, 1, Outcome::Applied);
    CHECK(t.find(kGRider)->phase == Phase::Riding && !t.has_work() && t.defers(kGRider, 200));
    // Dismount: the controller retires; here only after this client's RideOff::Enter (or 3 s).
    CHECK(t.on_event(ev(EvKind::Detach, 1), 1000, kGRider) == Accept::DetachPending);
    CHECK(t.plan(0xB0000001, 1001).plan == Plan::None);
    t.note_ride_off_enter(kGRider);
    w = t.plan(0xB0000001, 1002);  // any actor's dispatch once RideOff::Enter ran
    CHECK(w.plan == Plan::Detach);
    t.finish_detach(kGRider, 1, Outcome::Applied);
    CHECK(t.find(kGRider)->phase == Phase::Detached && !t.defers(kGRider, 1003));
    CHECK(t.on_event(ev(EvKind::Detach, 1), 1004, kGRider) == Accept::Dup);  // exactly once
    CHECK(t.plan(kGRider, 1005).plan == Plan::None);
    // A new mount: a new transition.
    CHECK(t.on_event(ev(EvKind::Begin, 2), 2000, kGRider) == Accept::Begun);
    CHECK(t.on_event(ev(EvKind::Begin, 1), 2001, kGRider) == Accept::Stale);
    // An old Detach never retires the new mount.
    CHECK(t.on_event(ev(EvKind::Detach, 1), 2002, kGRider) == Accept::Stale);
}

void test_table_failures() {
    Table t;
    // The controller's commit failed: cancelled, no hold, ride_filter as before.
    CHECK(t.on_event(ev(EvKind::Begin, 1), 100, kGRider) == Accept::Begun);
    Event c = ev(EvKind::Commit, 1);
    c.result = 0x123;
    c.flags = 0;
    CHECK(t.on_event(c, 110, kGRider) == Accept::CommitFailed);
    CHECK(!t.holding(110) && !t.defers(kGRider, 110) && !t.has_work());
    // No Commit in 500 ms: expired.
    CHECK(t.on_event(ev(EvKind::Begin, 2), 200, kGRider) == Accept::Begun);
    CHECK(t.expire(699) == 0 && t.expire(700) == 1);
    CHECK(t.find(kGRider)->phase == Phase::Cancelled && !t.defers(kGRider, 701));
    // A Commit without its Begin, or with another horse.
    CHECK(t.on_event(ev(EvKind::Commit, 3), 800, kGRider) == Accept::NoTxn);
    CHECK(t.on_event(ev(EvKind::Begin, 4), 900, kGRider) == Accept::Begun);
    Event other = ev(EvKind::Commit, 4);
    other.horse = kHHorse;
    CHECK(t.on_event(other, 901, kGRider) == Accept::Stale);
    // Committed but never applied in 3 s: cancelled.
    CHECK(t.on_event(ev(EvKind::Commit, 4), 902, kGRider) == Accept::Committed);
    CHECK(t.expire(3901) == 0 && t.expire(3902) == 1 && !t.has_work());
    // Not ready: retried at other actors' dispatches; the attempt at the rider's own action is the last one
    // (review (5): never linked after its mount action ran).
    CHECK(t.on_event(ev(EvKind::Begin, 5), 4000, kGRider) == Accept::Begun);
    CHECK(t.on_event(ev(EvKind::Commit, 5), 4001, kGRider) == Accept::Committed);
    for (uint32_t i = 1; i <= 3; ++i) {
        auto w = t.plan(0xB0000001, 4002 + i);
        CHECK(w.plan == Plan::Commit && w.txn.attempts == i);
        t.finish_commit(kGRider, 5, Outcome::NotReady);
        CHECK(t.find(kGRider)->phase == Phase::Committed);
    }
    CHECK(t.plan(kGRider, 4010).plan == Plan::Commit);
    t.finish_commit(kGRider, 5, Outcome::NotReady);
    CHECK(t.find(kGRider)->phase == Phase::Cancelled && !t.has_work());
    // ... and bounded by kMaxAttempts at other actors' dispatches.
    CHECK(t.on_event(ev(EvKind::Begin, 50), 4100, kGRider) == Accept::Begun);
    CHECK(t.on_event(ev(EvKind::Commit, 50), 4101, kGRider) == Accept::Committed);
    for (uint32_t i = 1; i <= kMaxAttempts; ++i) {
        CHECK(t.plan(0xB0000001, 4101 + i).plan == Plan::Commit);
        t.finish_commit(kGRider, 50, Outcome::NotReady);
    }
    CHECK(t.find(kGRider)->phase == Phase::Cancelled);
    // A failed native call quarantines the pair: its next transactions are never applied.
    CHECK(t.on_event(ev(EvKind::Begin, 60), 5000, kGRider) == Accept::Begun);
    CHECK(t.on_event(ev(EvKind::Commit, 60), 5001, kGRider) == Accept::Committed);
    CHECK(t.plan(kGRider, 5002).plan == Plan::Commit);
    t.finish_commit(kGRider, 60, Outcome::Failed);
    CHECK(t.find(kGRider)->phase == Phase::Quarantined && t.quarantined(kGRider, kGHorse));
    CHECK(t.on_event(ev(EvKind::Begin, 70), 6000, kGRider) == Accept::Begun);
    CHECK(t.on_event(ev(EvKind::Commit, 70), 6001, kGRider) == Accept::Committed);
    CHECK(t.find(kGRider)->phase == Phase::Cancelled && !t.has_work());
    // A Detach the controller itself could not do: nothing retired here.
    Table v;
    CHECK(v.on_event(ev(EvKind::Begin, 1), 100, kGRider) == Accept::Begun);
    CHECK(v.on_event(ev(EvKind::Commit, 1), 101, kGRider) == Accept::Committed);
    CHECK(v.plan(kGRider, 102).plan == Plan::Commit);
    v.finish_commit(kGRider, 1, Outcome::Applied);
    Event df = ev(EvKind::Detach, 1);
    df.result = 7;
    df.flags = 0;
    CHECK(v.on_event(df, 103, kGRider) == Accept::DetachFailed && v.find(kGRider)->phase == Phase::Riding);
    // A Detach of a transaction never applied here: dropped (nothing to undo).
    Table u;
    CHECK(u.on_event(ev(EvKind::Begin, 1), 100, kGRider) == Accept::Begun);
    CHECK(u.on_event(ev(EvKind::Commit, 1), 101, kGRider) == Accept::Committed);
    CHECK(u.on_event(ev(EvKind::Detach, 1), 102, kGRider) == Accept::DetachDropped);
    CHECK(!u.has_work() && u.find(kGRider)->phase == Phase::Cancelled);
    // The bound of a held stream cancels the waiting Begin.
    CHECK(u.on_event(ev(EvKind::Begin, 2), 200, kGRider) == Accept::Begun);
    CHECK(u.holding(201) && u.cancel_holds() == 1 && !u.holding(201));
}

void test_table_nonce_and_detach_force() {
    Table t;
    CHECK(t.on_event(ev(EvKind::Begin, 9), 100, kGRider) == Accept::Begun);
    CHECK(t.on_event(ev(EvKind::Commit, 9), 101, kGRider) == Accept::Committed);
    // The controller restarted (a new nonce): its transition 1 is a new transaction, the old one is gone.
    CHECK(t.on_event(ev(EvKind::Commit, 9, 0x9999), 102, kGRider) == Accept::NoTxn);
    CHECK(t.on_event(ev(EvKind::Begin, 1, 0x9999), 103, kGRider) == Accept::Begun);
    CHECK(t.on_event(ev(EvKind::Commit, 1, 0x9999), 104, kGRider) == Accept::Committed);
    // The first dispatch applies it.
    CHECK(t.plan(0xB0000001, 105).plan == Plan::Commit);
    t.finish_commit(kGRider, 1, Outcome::ProbeOnly);  // probe: followed as if applied, nothing called
    CHECK(t.find(kGRider)->phase == Phase::Riding);
    // A Detach without this client's RideOff::Enter: applied after 3 s anyway.
    CHECK(t.on_event(ev(EvKind::Detach, 1, 0x9999), 2000, kGRider) == Accept::DetachPending);
    CHECK(t.plan(kGRider, 4999).plan == Plan::None);
    CHECK(t.plan(0xB0000001, 5000).plan == Plan::Detach);
    t.finish_detach(kGRider, 1, Outcome::ProbeOnly);
    CHECK(t.find(kGRider)->phase == Phase::Detached);
    // reset (a new connection): nothing remains.
    t.reset();
    CHECK(t.find(kGRider) == nullptr && !t.has_work());
}

void test_fallback_and_hold() {
    CHECK(fallback_rewrite(true, true, false));
    CHECK(!fallback_rewrite(true, true, true));    // the link is there: the original action goes on
    CHECK(!fallback_rewrite(false, true, false));  // ride_filter not 1: never written
    CHECK(!fallback_rewrite(true, false, false));  // not a ride action
    CHECK(hold_fits(0, 0, 725));
    CHECK(!hold_fits(kHoldMaxPackets, 0, 1));
    CHECK(!hold_fits(1, kHoldMaxBytes, 1));
    // The dispatch's view of a deferred mount: session 13's G mount still classifies as Mount there.
    std::vector<uint8_t> payload(rfl::kAckSize, 0);
    auto put = [&](size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i) payload[at + i] = static_cast<uint8_t>(v >> (8 * i));
    };
    put(0, kGRider);
    put(rfl::kA0 + rfl::kBlockHash, 0x91605616u);
    put(rfl::kA1 + rfl::kBlockHash, 0x26107D3Cu);
    for (int i = 0; i < 8; ++i) payload[rfl::kParent + i] = 0xFF;
    const rfl::Ack a = rfl::read_ack(payload.data(), payload.size());
    CHECK(rfl::classify(a) == rfl::Kind::Mount);
    CHECK(rfl::rewrite(payload.data(), payload.size(), rfl::Kind::Mount));
    CHECK(rfl::classify(rfl::read_ack(payload.data(), payload.size())) == rfl::Kind::None);
}

// The guest's two drainers (review of c2eed25): events and client packets reach the queue in their order, a
// refused event is kept and offered again first, a busy gate never blocks the non-blocking drainer.
void test_outbound_drain_order() {
    // Source: frames numbered 1..N, popped from a shared FIFO (the lock-free ring's role).
    std::mutex src_mu;
    std::deque<uint32_t> src;
    std::mutex sink_mu;
    std::vector<uint32_t> sink;  // what reached the queue to the host, in order (events < 0x80000000, packets above)
    auto pop = [&](std::vector<uint8_t>& f) {
        std::lock_guard lk(src_mu);
        if (src.empty()) return false;
        const uint32_t v = src.front();
        src.pop_front();
        f.assign(4, 0);
        put32(f.data(), v);
        return true;
    };
    auto push = [&](const std::vector<uint8_t>& f) {
        std::lock_guard lk(sink_mu);
        sink.push_back(get32(f.data()));
        return true;
    };
    OutboundDrain d;
    constexpr uint32_t kEvents = 20000;
    std::atomic<bool> producing{true};
    std::atomic<uint32_t> produced{0};
    // The game thread: events in order; after every 10th, a "client packet" that must follow every event produced
    // before it (the Req after its Begin).
    std::vector<uint32_t> packet_after;  // for packet k: how many events existed when it was sent
    std::thread net([&] {  // the NetworkThread: blocking drain + its own packet in the same step
        uint32_t k = 0;
        while (producing.load() || k < kEvents / 10) {
            const uint32_t seen = produced.load();
            if (seen / 10 <= k) {
                if (!producing.load()) break;
                std::this_thread::yield();
                continue;
            }
            const uint32_t pkt = 0x80000000u | k;
            d.drain(pop, push, /*blocking=*/true, [&] {
                std::lock_guard lk(sink_mu);
                sink.push_back(pkt);
            });
            packet_after.push_back(seen);
            ++k;
        }
    });
    std::thread mod([&] {  // the mod's network thread: try only
        while (producing.load()) d.drain(pop, push, /*blocking=*/false);
    });
    for (uint32_t i = 1; i <= kEvents; ++i) {
        {
            std::lock_guard lk(src_mu);
            src.push_back(i);
        }
        produced.store(i);
    }
    while (true) {
        {
            std::lock_guard lk(src_mu);
            if (src.empty()) break;
        }
        std::this_thread::yield();
    }
    producing.store(false);
    net.join();
    mod.join();
    d.drain(pop, push, true);
    // Every event once, in order; every packet after all the events produced before it.
    uint32_t next = 1, last_event = 0;
    bool order_ok = true, packets_ok = true;
    for (const uint32_t v : sink) {
        if (v & 0x80000000u) {
            const uint32_t k = v & 0x7FFFFFFFu;
            if (k >= packet_after.size() || last_event < packet_after[k]) packets_ok = false;
            continue;
        }
        if (v != next) order_ok = false;
        next = v + 1;
        last_event = v;
    }
    CHECK(order_ok);
    CHECK(next == kEvents + 1);
    CHECK(packets_ok);
    CHECK(d.failed() == 0);

    // A refused push: the event is kept, nothing after it goes (nor the client packet), then it goes first.
    OutboundDrain e;
    std::deque<uint32_t> q = {1, 2, 3};
    std::vector<uint32_t> out;
    bool refuse = true;
    auto pop2 = [&](std::vector<uint8_t>& f) {
        if (q.empty()) return false;
        f.assign(4, 0);
        put32(f.data(), q.front());
        q.pop_front();
        return true;
    };
    auto push2 = [&](const std::vector<uint8_t>& f) {
        if (refuse && get32(f.data()) == 2) return false;
        out.push_back(get32(f.data()));
        return true;
    };
    bool sent = false;
    CHECK(e.drain(pop2, push2, true, [&] { sent = true; }) == 1);
    CHECK(!sent && e.failed() == 1 && e.pending() && out == std::vector<uint32_t>({1}));
    CHECK(e.drain(pop2, push2, true) == 0 && e.failed() == 2);  // still refused: stays pending, 3 not popped
    CHECK(q.size() == 1);
    refuse = false;
    CHECK(e.drain(pop2, push2, true, [&] { sent = true; }) == 2);
    CHECK(sent && !e.pending() && out == std::vector<uint32_t>({1, 2, 3}));
    // A busy gate: the non-blocking drainer returns at once and counts it.
    OutboundDrain g;
    uint64_t inner = 0;
    g.drain(pop2, push2, true, [&] { std::thread([&] { inner = g.drain(pop2, push2, false); }).join(); });
    CHECK(inner == 0 && g.busy() == 1);
}

// Session 17: a player's dismount never calls 0x2228E40; the controller watches its own link and publishes an
// observed Detach (flag 12, p4 0), exactly once per committed pair.
void test_ctl_link_watch() {
    const uint64_t a = arm_pack(kHHorse, 3);
    CHECK(arm_horse(a) == kHHorse && arm_transition(a) == 3 && a != 0);
    PairState s;
    CHECK(ctl_link(s, kHHorse) == CtlLink::Unknown);  // nothing read
    s.rider_found = s.rider_vc = s.link = true;
    s.cur_vehicle = kHHorse;
    CHECK(ctl_link(s, kHHorse) == CtlLink::Riding);
    CHECK(ctl_link(s, 0) == CtlLink::Unknown);
    s.cur_vehicle = 0;
    CHECK(ctl_link(s, kHHorse) == CtlLink::Gone);  // S17: Link+0x10 cleared after RideOff::Enter
    s.cur_vehicle = 0xB0100999;
    CHECK(ctl_link(s, kHHorse) == CtlLink::Gone);  // another vehicle
    s.link = false;
    CHECK(ctl_link(s, kHHorse) == CtlLink::Unknown);
    // The event: a Detach of the armed transition, ok + observed, the client's flag 12, p4 0; on the wire.
    const Event d = observed_detach(kHRider, a, 0x78EDCDA3, 42);
    CHECK(d.kind == EvKind::Detach && d.rider == kHRider && d.horse == kHHorse && d.transition == 3);
    CHECK(d.fifth == kClientDetachFlag && d.aux == 0 && d.result == 0 && d.flags == (kFlagOk | kFlagObserved));
    const auto f = frame(d);
    const auto back = decode(std::span<const uint8_t>(f).subspan(kHeader));
    CHECK(back && back->flags == (kFlagOk | kFlagObserved) && back->fifth == 12 && back->transition == 3);
    // The observer (S17's host transaction 3 as the guest saw it): Begin + Commit together, applied at the rider's
    // mount action, RideOff::Enter here, then the observed Detach: retired at once with flag 12 / p4 0.
    Table t;
    CHECK(t.on_event(ev(EvKind::Begin, 3, 0x78EDCDA3, kHRider, kHHorse), 1000, kHRider) == Accept::Begun);
    CHECK(t.on_event(ev(EvKind::Commit, 3, 0x78EDCDA3, kHRider, kHHorse), 1000, kHRider) == Accept::Committed);
    auto w = t.plan(kHRider, 1030);  // S17: the 2991 mount Ack 18-56 ms after the Begin
    CHECK(w.plan == Plan::Commit && w.txn.fifth == 0 && w.txn.seat == 0);
    t.finish_commit(kHRider, 3, Outcome::Applied);
    t.note_ride_off_enter(kHRider);
    CHECK(t.on_event(*back, 30000, kHRider) == Accept::DetachPending);
    w = t.plan(0xB0000001, 30001);
    CHECK(w.plan == Plan::Detach && w.txn.detach_flag == 12 && w.txn.detach_aux == 0 && !w.txn.no_event);
    t.finish_detach(kHRider, 3, Outcome::Applied);
    CHECK(t.find(kHRider)->phase == Phase::Detached);
    CHECK(t.on_event(*back, 30002, kHRider) == Accept::Dup);
}

// Session 17: no Detach ever comes, but this client ran RideOff::Enter and the rider is on foot for 6 s.
// Review (4): S17's host never ran RideOff::Enter for the guest's dismount (it came as on-foot acks, parent -1).
constexpr uint64_t kNoParent = ~0ull;
void test_no_event_detach() {
    Table t;
    CHECK(t.on_event(ev(EvKind::Begin, 1), 100, kGRider) == Accept::Begun);
    CHECK(t.on_event(ev(EvKind::Commit, 1), 100, kGRider) == Accept::Committed);
    CHECK(t.plan(kGRider, 120).plan == Plan::Commit);
    t.finish_commit(kGRider, 1, Outcome::Applied);
    CHECK(t.find(kGRider)->linked_here == kGHorse);
    // The mount's own action names the horse (S17: parent -1, target the horse): not "on foot".
    t.note_rider_parent(kGRider, kNoParent, kGHorse, 200);
    CHECK(!t.has_work() && t.plan(kGRider, 10000).plan == Plan::None);
    t.note_rider_parent(kGRider, kGHorse, 0, 20000);       // mounted actions
    CHECK(!t.has_work());
    t.note_rider_parent(kGRider, kNoParent, 0, 21000);     // on foot (no RideOff::Enter here at all)
    t.note_rider_parent(kGRider, kNoParent, 0, 22000);     // the first on-foot time stays
    CHECK(t.has_work() && t.plan(kGRider, 26999).plan == Plan::None);
    t.note_rider_parent(kGRider, kGHorse, 0, 26000);       // mounted again: the candidate ends
    CHECK(!t.has_work() && t.plan(kGRider, 30000).plan == Plan::None);
    t.note_rider_parent(kGRider, kNoParent, 0, 30000);
    auto w = t.plan(0xB0000001, 36000);
    CHECK(w.plan == Plan::Detach && w.txn.no_event && w.txn.detach_flag == kClientDetachFlag && w.txn.detach_aux == 0);
    CHECK(t.plan(0xB0000001, 36001).plan == Plan::None);  // claimed: once
    t.finish_detach(kGRider, 1, Outcome::Applied);
    CHECK(t.find(kGRider)->phase == Phase::Detached && !t.has_work() && t.find(kGRider)->linked_here == 0);
    // The controller's Detach arriving late changes nothing.
    CHECK(t.on_event(ev(EvKind::Detach, 1), 36500, kGRider) == Accept::Dup);
    // A new mount clears every candidate field.
    CHECK(t.on_event(ev(EvKind::Begin, 2), 40000, kGRider) == Accept::Begun);
    CHECK(!t.find(kGRider)->ride_off_entered && t.find(kGRider)->on_foot_ms == 0 && !t.find(kGRider)->no_event);

    // An OBSERVED Detach: retired at the first dispatch once the rider is on foot here, without RideOff::Enter.
    Table o;
    CHECK(o.on_event(ev(EvKind::Begin, 3), 100, kGRider) == Accept::Begun);
    CHECK(o.on_event(ev(EvKind::Commit, 3), 100, kGRider) == Accept::Committed);
    CHECK(o.plan(0xB0000001, 101).plan == Plan::Commit);
    o.finish_commit(kGRider, 3, Outcome::Applied);
    const Event obs = observed_detach(kGRider, arm_pack(kGHorse, 3), 0x1234, 5);
    CHECK(o.on_event(obs, 5000, kGRider) == Accept::DetachPending);
    CHECK(o.plan(0xB0000001, 5001).plan == Plan::None);    // still mounted actions here
    o.note_rider_parent(kGRider, kNoParent, 0, 5100);      // its dismount arrives as on-foot acks
    w = o.plan(kGRider, 5100);                              // the same dispatch, before its handler
    CHECK(w.plan == Plan::Detach && w.txn.detach_flag == 12 && w.txn.detach_observed);
    o.finish_detach(kGRider, 3, Outcome::Applied);
    // ... or the on-foot ack came first (the controller's link cleared later): at once.
    Table q;
    CHECK(q.on_event(ev(EvKind::Begin, 4), 100, kGRider) == Accept::Begun);
    CHECK(q.on_event(ev(EvKind::Commit, 4), 100, kGRider) == Accept::Committed);
    CHECK(q.plan(0xB0000001, 101).plan == Plan::Commit);
    q.finish_commit(kGRider, 4, Outcome::Applied);
    q.note_rider_parent(kGRider, kNoParent, 0, 4000);
    CHECK(q.on_event(observed_detach(kGRider, arm_pack(kGHorse, 4), 0x1234, 5), 4200, kGRider) == Accept::DetachPending);
    CHECK(q.plan(0xB0000001, 4201).plan == Plan::Detach);
    // ... else after 1 s (a non-observed Detach still waits 3 s).
    Table r;
    CHECK(r.on_event(ev(EvKind::Begin, 5), 100, kGRider) == Accept::Begun);
    CHECK(r.on_event(ev(EvKind::Commit, 5), 100, kGRider) == Accept::Committed);
    CHECK(r.plan(0xB0000001, 101).plan == Plan::Commit);
    r.finish_commit(kGRider, 5, Outcome::Applied);
    CHECK(r.on_event(observed_detach(kGRider, arm_pack(kGHorse, 5), 0x1234, 5), 7000, kGRider) == Accept::DetachPending);
    CHECK(r.plan(0xB0000001, 7999).plan == Plan::None && r.plan(0xB0000001, 8000).plan == Plan::Detach);
}

// Review (3), (6): a horse change with the old link still applied here; claims that never finish; a Detach while
// the commit is being applied; a Detach after a failed commit.
void test_review_lifecycle() {
    Table t;
    CHECK(t.on_event(ev(EvKind::Begin, 1), 100, kGRider) == Accept::Begun);
    CHECK(t.on_event(ev(EvKind::Commit, 1), 100, kGRider) == Accept::Committed);
    CHECK(t.plan(kGRider, 101).plan == Plan::Commit);
    t.finish_commit(kGRider, 1, Outcome::Applied);
    // A new mount on ANOTHER horse before any retirement here: the link this client made is remembered.
    Event b2 = ev(EvKind::Begin, 2);
    b2.horse = 0xB0100999;
    Event c2 = ev(EvKind::Commit, 2);
    c2.horse = 0xB0100999;
    CHECK(t.on_event(b2, 5000, kGRider) == Accept::Begun && t.on_event(c2, 5000, kGRider) == Accept::Committed);
    auto w = t.plan(kGRider, 5001);
    CHECK(w.plan == Plan::Commit && w.txn.linked_here == kGHorse && w.txn.horse == 0xB0100999);
    t.finish_commit(kGRider, 2, Outcome::Applied, true);   // the old one retired first, then applied
    CHECK(t.find(kGRider)->linked_here == 0xB0100999);
    // A probe transaction never records a link.
    Table p;
    CHECK(p.on_event(ev(EvKind::Begin, 1), 100, kGRider) == Accept::Begun);
    CHECK(p.on_event(ev(EvKind::Commit, 1), 100, kGRider) == Accept::Committed);
    CHECK(p.plan(kGRider, 101).plan == Plan::Commit);
    p.finish_commit(kGRider, 1, Outcome::ProbeOnly);
    CHECK(p.find(kGRider)->linked_here == 0);
    // A claim that never finished (the game's exception caught outside): quarantined after 2 s, not deferring.
    Table s;
    CHECK(s.on_event(ev(EvKind::Begin, 1), 100, kGRider) == Accept::Begun);
    CHECK(s.on_event(ev(EvKind::Commit, 1), 100, kGRider) == Accept::Committed);
    CHECK(s.plan(kGRider, 200).plan == Plan::Commit);
    CHECK(s.defers(kGRider, 300) && s.expire(2199) == 0 && s.expire(2200) == 1);
    CHECK(s.find(kGRider)->phase == Phase::Quarantined && !s.defers(kGRider, 2201) && s.quarantined(kGRider, kGHorse));
    CHECK(s.on_event(ev(EvKind::Detach, 1), 2300, kGRider) == Accept::Dup);  // nothing claimed twice
    // A Detach while the commit runs: retired once the commit is through.
    Table a;
    CHECK(a.on_event(ev(EvKind::Begin, 1), 100, kGRider) == Accept::Begun);
    CHECK(a.on_event(ev(EvKind::Commit, 1), 100, kGRider) == Accept::Committed);
    CHECK(a.plan(kGRider, 101).plan == Plan::Commit);
    CHECK(a.on_event(ev(EvKind::Detach, 1), 102, kGRider) == Accept::DetachPending);
    a.finish_commit(kGRider, 1, Outcome::Applied);
    CHECK(a.find(kGRider)->phase == Phase::DetachPending);
    CHECK(a.plan(0xB0000001, 3102).plan == Plan::Detach);
    // A Detach after a FAILED commit (a part of a link may be there): one retirement attempt.
    Table f;
    CHECK(f.on_event(ev(EvKind::Begin, 1), 100, kGRider) == Accept::Begun);
    CHECK(f.on_event(ev(EvKind::Commit, 1), 100, kGRider) == Accept::Committed);
    CHECK(f.plan(kGRider, 101).plan == Plan::Commit);
    f.finish_commit(kGRider, 1, Outcome::Failed);
    CHECK(f.find(kGRider)->phase == Phase::Quarantined);
    CHECK(f.on_event(ev(EvKind::Detach, 1), 200, kGRider) == Accept::DetachPending);
    CHECK(f.plan(0xB0000001, 3200).plan == Plan::Detach);
    f.finish_detach(kGRider, 1, Outcome::Failed);
    CHECK(f.find(kGRider)->phase == Phase::Quarantined);
    CHECK(f.on_event(ev(EvKind::Detach, 1), 300, kGRider) == Accept::Dup);  // never a second attempt
}

void test_ride_geometry() {
    RideGeo g;
    const float horse[3] = {-10774.6f, 611.5f, -3944.5f};
    const float ident[4] = {0, 0, 0, 1};
    // On the seat (identity rotation): bucket 0.
    float rider[3] = {horse[0] + kSeatRef[0], horse[1] + kSeatRef[1], horse[2] + kSeatRef[2]};
    CHECK(g.add(kGeoLinked, rider, horse, ident, 0.5f));
    CHECK(g.n[kGeoLinked][0] == 1 && g.err[kGeoLinked][0][0] == 1);
    // The horse turned 90 degrees about y (S17's quats are (0, y, 0, w)): the same seat in its frame.
    const float s = std::sqrt(0.5f);
    const float yaw90[4] = {0, s, 0, s};
    float seat_world[3];
    {
        // world = R * seat: rotate by q = rotate_inv by conj(q)
        const float conjq[4] = {0, -s, 0, s};
        rotate_inv(conjq, kSeatRef, seat_world);
    }
    const float r2[3] = {horse[0] + seat_world[0], horse[1] + seat_world[1], horse[2] + seat_world[2]};
    CHECK(g.add(kGeoNotLinked, r2, horse, yaw90, 8.2f));
    CHECK(g.n[0][3] == 1 && g.err[0][3][0] == 1);
    // 1.5 m behind the saddle at a gallop: bucket <= 2 m.
    const float r3[3] = {rider[0], rider[1], rider[2] - 1.5f};
    CHECK(g.add(kGeoNotLinked, r3, horse, ident, 11.0f));
    CHECK(g.n[0][4] == 1 && g.err[0][4][3] == 1 && g.max_mm[0][4] >= 1499 && g.max_mm[0][4] <= 1501);
    // Not on that horse (> 10 m) / a bad quaternion: not counted.
    const float away[3] = {horse[0] + 20.f, horse[1], horse[2]};
    CHECK(!g.add(kGeoLinked, away, horse, ident, 1.f) && g.off_far == 1);
    const float bad[4] = {0, 0, 0, 0};
    CHECK(!g.add(kGeoLinked, rider, horse, bad, 1.f) && g.unreadable == 1);
    CHECK(geo_speed_bucket(0.99f) == 0 && geo_speed_bucket(1.f) == 1 && geo_speed_bucket(9.99f) == 3 &&
          geo_speed_bucket(10.f) == 4);
    CHECK(geo_err_bucket(0.25f) == 0 && geo_err_bucket(0.26f) == 1 && geo_err_bucket(5.01f) == 5);
    // P8 S21: the three buckets. Attach type 4 is "seated here" whatever the Link says; else the Link decides.
    CHECK(geo_link(true, kAttachSeat) == kGeoSeated && geo_link(false, kAttachSeat) == kGeoSeated);
    CHECK(geo_link(true, kAttachNone) == kGeoLinked && geo_link(true, 0xFF) == kGeoLinked);
    CHECK(geo_link(false, kAttachNone) == kGeoNotLinked && geo_link(false, 2) == kGeoNotLinked);
    CHECK(g.add(kGeoSeated, rider, horse, ident, 7.f));
    CHECK(g.n[kGeoSeated][3] == 1 && g.err[kGeoSeated][3][0] == 1 && g.n[kGeoLinked][3] == 0);
    CHECK(g.add(99, rider, horse, ident, 0.f) && g.n[kGeoNotLinked][0] == 1);  // out of range: NOT linked
    // Frozen: the rider still (< 1 cm) while its horse moved > 0.5 m; anything else is not.
    const float h0[3] = {0, 0, 0}, h1[3] = {0.6f, 0, 0}, h_small[3] = {0.4f, 0, 0};
    const float r0[3] = {0, 1.5f, 0}, r_still[3] = {0.005f, 1.5f, 0}, r_moved[3] = {0.6f, 1.5f, 0};
    CHECK(geo_frozen(r0, r_still, h0, h1));
    CHECK(!geo_frozen(r0, r_moved, h0, h1));      // followed its horse
    CHECK(!geo_frozen(r0, r_still, h0, h_small)); // the horse barely moved
    CHECK(!geo_frozen(r0, r0, h0, h0));
}

// ---------------------------------------------------------------- P8 S21: mount_seat, ride_hold_pos

PairState linked_pair(uint32_t horse, int8_t seat) {
    PairState p;
    p.rider_found = p.horse_found = p.rider_vc = p.horse_vc = p.link = true;
    p.cur_vehicle = horse;
    p.cur_seat = seat;
    return p;
}
SeatState free_rider() {
    SeatState s;
    s.has_cc = s.has_mc = s.has_cc100 = s.has_parent = true;
    s.attach_type = kAttachNone;
    s.parent_key = 0;
    s.parent_type = kAttachNone;
    s.cc228 = 1;  // S22: CC vf19 ran (both S21 logs)
    s.rider_type = kActorPlayer;
    s.horse_type = 5;  // a horse (caballo-coop.md: 5 montura)
    s.horse_cc = true;
    s.rider_local = 1;           // S22: what the remote player's character reads (both PCs)
    s.vf178 = kVf178Actor;       // every client in-game actor
    s.rider_user = 0x2000;       // user 2's object ...
    s.local_user = 0x1000;       // ... not this client's
    s.rider_c8 = s.horse_c8 = 0;  // remote copies here (S20-S22 logs)
    return s;
}
bool judged(const SeatJudge& j, SeatVerdict v, SeatWhy w = SeatWhy::None) { return j.verdict == v && j.why == w; }

void test_seat_judge() {
    const PairState p = linked_pair(kHHorse, 0);
    SeatState s = free_rider();
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Apply));
    CHECK(judged(seat_judge(Mode::Probe, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Probe));
    CHECK(judged(seat_judge(Mode::Off, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::Off));
    // The Link vf53 left must name this horse and seat.
    PairState q = p;
    q.rider_found = false;
    CHECK(judged(seat_judge(Mode::On, q, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::NotLinked));
    q = p;
    q.link = false;
    CHECK(judged(seat_judge(Mode::On, q, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::NotLinked));
    CHECK(judged(seat_judge(Mode::On, p, s, kGHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::NotLinked));
    CHECK(judged(seat_judge(Mode::On, p, s, 0, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::NotLinked));
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 1, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::NotLinked));
    CHECK(judged(seat_judge(Mode::On, linked_pair(kHHorse, 1), s, kHHorse, 1, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::NotSeat0));
    // The rider's components.
    s.has_mc = false;
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::NoMc));
    s = free_rider();
    s.has_cc = false;
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::NoMc));
    s = free_rider();
    s.has_cc100 = false;
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::NoAttach));
    s = free_rider();
    s.attach_type = 0xFF;
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::NoAttach));
    // Already seated under this horse (also in probe); type 4 under another parent or any other type: busy.
    s = free_rider();
    s.attach_type = kAttachSeat;
    s.parent_key = kHHorse;
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::AlreadySeated));
    CHECK(judged(seat_judge(Mode::Probe, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::AlreadySeated));
    s.parent_key = kGHorse;
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::AttachBusy));
    s.parent_key = kHHorse;
    s.has_parent = false;
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::AttachBusy));
    s = free_rider();
    s.attach_type = 2;
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::AttachBusy));
    // Actor types: a player rider; a horse 0xA462B0 seats (1, 3..6, 8, 9), never 7.
    s = free_rider();
    s.rider_type = 5;
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::RiderType));
    s.rider_type = 0xFF;
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::RiderType));
    s = free_rider();
    s.horse_type = kActorWorld;
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::HorseType7));
    for (const uint8_t t : {uint8_t{0}, uint8_t{2}, uint8_t{10}, uint8_t{0xFF}}) {
        s.horse_type = t;
        CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::HorseType));
    }
    for (const uint8_t t : {uint8_t{1}, uint8_t{3}, uint8_t{4}, uint8_t{6}, uint8_t{8}, uint8_t{9}}) {
        s.horse_type = t;
        CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Apply));
    }
    s = free_rider();
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kGRider, kGRider), SeatVerdict::Refused, SeatWhy::Local));
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, 0, kGRider), SeatVerdict::Refused, SeatWhy::Local));
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, 0), SeatVerdict::Refused, SeatWhy::Local));
    // S22 [V]: type 9 is "none" (S21 refused it as busy); 0 is never written by the game.
    s = free_rider();
    CHECK(kAttachNone == 9 && judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Apply));
    s.attach_type = 0;
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::AttachBusy));
    s = free_rider();
    s.parent_key = kGHorse;
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::ParentSet));
    s = free_rider();
    s.has_parent = false;
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::ParentSet));
    s = free_rider();
    s.cc228 = 0;
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::NotInit));
    // S22 [V 0x4FE260]: vf178 = +0xA0 is the local user AND +0x94. The remote user (+0x94 = 1, another user): Apply.
    s = free_rider();
    CHECK(!seat_local_user(s) && judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Apply));
    s.rider_local = 0;
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Apply));
    s = free_rider();
    s.rider_user = s.local_user;  // the local user's character
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::LocalCtl));
    s.rider_local = 0;  // ... with +0x94 0: vf178 false
    CHECK(!seat_local_user(s));
    s = free_rider();
    s.local_user = 0;  // unread
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::LocalCtl));
    s = free_rider();
    s.rider_user = 0;  // unread
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::LocalCtl));
    s = free_rider();
    s.vf178 = kVf178User;  // +0x94 alone (the account/server realm's)
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::LocalCtl));
    s.vf178 = 0x123456;  // unknown slot
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::LocalCtl));
    for (const uint8_t c8 : {uint8_t{1}, uint8_t{2}, uint8_t{0xFF}}) {  // controlled here, or unreadable
        s = free_rider();
        s.rider_c8 = c8;
        CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::Controlled));
        s = free_rider();
        s.horse_c8 = c8;
        CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::Controlled));
    }
    s = free_rider();
    s.horse_cc = false;
    CHECK(judged(seat_judge(Mode::On, p, s, kHHorse, 0, kHRider, kGRider), SeatVerdict::Refused, SeatWhy::HorseNoCC));
    for (size_t w = 0; w < static_cast<size_t>(SeatWhy::Count); ++w) {
        CHECK(std::string_view(seat_why_name(static_cast<SeatWhy>(w))) != "?");
    }
}

void test_seat_verify() {
    const SeatState pre = free_rider();
    SeatState post = pre;
    post.attach_type = kAttachSeat;
    post.parent_key = kHHorse;
    post.parent_type = kAttachSeat;
    post.cc228 = 1;
    CHECK(seat_verify(pre, post, 0, kHHorse) == SeatPost::Ok);
    SeatState p3 = post;
    p3.parent_type = kAttachNone;  // S22 review: the hierarchy's own type is logged, never judged
    CHECK(seat_verify(pre, p3, 0, kHHorse) == SeatPost::Ok);
    CHECK(seat_verify(pre, post, 5, kHHorse) == SeatPost::Result);
    CHECK(seat_verify(pre, post, -1, kHHorse) == SeatPost::Result);
    CHECK(seat_verify(pre, post, 0, kGHorse) == SeatPost::Parent);
    SeatState p2 = post;
    p2.has_parent = false;
    CHECK(seat_verify(pre, p2, 0, kHHorse) == SeatPost::Parent);
    p2 = post;
    p2.has_cc100 = false;
    CHECK(seat_verify(pre, p2, 0, kHHorse) == SeatPost::Unreadable);
    p2 = post;
    p2.attach_type = 0xFF;
    CHECK(seat_verify(pre, p2, 0, kHHorse) == SeatPost::Unreadable);
    p2 = post;
    p2.attach_type = kAttachNone;  // nothing happened
    CHECK(seat_verify(pre, p2, 0, kHHorse) == SeatPost::Unchanged);
    p2.attach_type = 8;
    CHECK(seat_verify(pre, p2, 0, kHHorse) == SeatPost::Type);
    for (size_t i = 0; i < static_cast<size_t>(SeatPost::Count); ++i) {
        CHECK(std::string_view(seat_post_name(static_cast<SeatPost>(i))) != "?");
    }
}

void test_seat_release() {
    // Native: seated, the observer's retirement here, the type drops 200 ms later.
    SeatWatch w;
    CHECK(w.step(kAttachSeat, kHHorse, false, 1000).rel == SeatRel::None);  // nothing watched
    w.seat(kHRider, kHHorse, 1000);
    CHECK(w.rider() == kHRider && w.horse() == kHHorse && !w.expecting());
    CHECK(w.step(kAttachSeat, kHHorse, false, 1030).rel == SeatRel::None);
    CHECK(w.step(kAttachSeat, kHHorse, false, 60000).rel == SeatRel::None);  // riding: no expectation, no timeout
    w.expect(kGRider, SeatCause::Detach, 60000);  // another rider: ignored
    CHECK(!w.expecting());
    w.expect(kHRider, SeatCause::Detach, 60000);
    w.expect(kHRider, SeatCause::OnFoot, 60100);  // the first cause stays
    CHECK(w.expecting());
    CHECK(w.step(kAttachSeat, 0, false, 60100).rel == SeatRel::None);
    SeatStep st = w.step(kAttachNone, 0, false, 60200);
    CHECK(st.rel == SeatRel::Released && st.cause == SeatCause::Detach && st.after_ms == 200 && st.seated_ms == 59200 &&
          st.type == kAttachNone);
    CHECK(w.rider() == 0 && w.step(kAttachSeat, kHHorse, false, 70000).rel == SeatRel::None);  // forgotten
    // Released without any dismount seen (another route): reported as such.
    w.seat(kHRider, kHHorse, 0);
    st = w.step(kAttachNone, 0, false, 500);
    CHECK(st.rel == SeatRel::Released && st.cause == SeatCause::None && st.after_ms == ~0ull);

    // Timeout with the Link still on the horse: exactly one retirement, then stuck if still 4.
    w.seat(kHRider, kHHorse, 1000);
    w.expect(kHRider, SeatCause::RideOff, 2000);
    CHECK(w.step(kAttachSeat, kHHorse, false, 3499).rel == SeatRel::None);
    st = w.step(kAttachSeat, kHHorse, false, 3500);
    CHECK(st.rel == SeatRel::Retire && st.cause == SeatCause::RideOff && st.after_ms == 1500);
    CHECK(w.step(kAttachSeat, kHHorse, false, 3530).rel == SeatRel::None);  // its own wait
    CHECK(w.step(kAttachSeat, kHHorse, false, 4999).rel == SeatRel::None);
    st = w.step(kAttachSeat, kHHorse, false, 5000);
    CHECK(st.rel == SeatRel::Stuck && st.cause == SeatCause::Retired);
    CHECK(w.rider() == 0 && w.step(kAttachSeat, kHHorse, false, 9000).rel == SeatRel::None);  // reported once, forgotten
    // The retirement worked: released after it.
    w.seat(kHRider, kHHorse, 1000);
    w.expect(kHRider, SeatCause::Detach, 1000);
    CHECK(w.step(kAttachSeat, kHHorse, false, 2500).rel == SeatRel::Retire);
    st = w.step(kAttachNone, 0, false, 2530);
    CHECK(st.rel == SeatRel::Released && st.cause == SeatCause::Retired && st.after_ms == 30);

    // The Link already gone but still type 4: stuck, reported once, nothing called.
    w.seat(kHRider, kHHorse, 1000);
    w.expect(kHRider, SeatCause::Detach, 2000);
    CHECK(w.step(kAttachSeat, 0, false, 3000).rel == SeatRel::None);
    CHECK(w.step(kAttachSeat, 0, false, 3500).rel == SeatRel::Stuck);
    CHECK(w.step(kAttachSeat, 0, false, 3600).rel == SeatRel::None);
    w.seat(kHRider, kHHorse, 1000);  // linked to ANOTHER horse now: the same
    w.expect(kHRider, SeatCause::OnFoot, 2000);
    CHECK(w.step(kAttachSeat, kGHorse, false, 3500).rel == SeatRel::Stuck);

    // Never a retirement while the rider is in RideOn/RideOff, up to the cap.
    w.seat(kHRider, kHHorse, 1000);
    w.expect(kHRider, SeatCause::RideOff, 2000);
    CHECK(w.step(kAttachSeat, kHHorse, true, 3500).rel == SeatRel::None);
    CHECK(w.step(kAttachSeat, kHHorse, true, 7999).rel == SeatRel::None);
    CHECK(w.step(kAttachSeat, kHHorse, true, 8000).rel == SeatRel::Retire);

    // An on-foot expectation is cancelled by a mounted action; a RideOff one is not.
    w.seat(kHRider, kHHorse, 1000);
    w.expect(kHRider, SeatCause::OnFoot, 2000);
    w.mounted(kGRider);
    CHECK(w.expecting());
    w.mounted(kHRider);
    CHECK(!w.expecting());
    CHECK(w.step(kAttachSeat, kHHorse, false, 9000).rel == SeatRel::None);
    w.expect(kHRider, SeatCause::RideOff, 9000);
    w.mounted(kHRider);
    CHECK(w.expecting());

    // S21 review: a RideOff stamp older than the seat is ignored.
    w.seat(kHRider, kHHorse, 5000);
    w.expect(kHRider, SeatCause::RideOff, 4990);
    CHECK(!w.expecting());
    CHECK(w.step(kAttachSeat, kHHorse, false, 9000).rel == SeatRel::None);
    // An on-foot expectation alone never retires while the Link names the horse; a Detach upgrades it.
    w.seat(kHRider, kHHorse, 1000);
    w.expect(kHRider, SeatCause::OnFoot, 2000);
    CHECK(w.step(kAttachSeat, kHHorse, false, 30000).rel == SeatRel::None);
    w.expect(kHRider, SeatCause::Detach, 30000);
    CHECK(w.step(kAttachSeat, kHHorse, false, 31499).rel == SeatRel::None);
    st = w.step(kAttachSeat, kHHorse, false, 31500);
    CHECK(st.rel == SeatRel::Retire && st.cause == SeatCause::Detach && st.after_ms == 1500);
    // Not the native policy: never a retirement here, a stuck report instead (the setting survives seat/forget).
    w.set_retire(false);
    w.seat(kHRider, kHHorse, 1000);
    w.expect(kHRider, SeatCause::Detach, 2000);
    CHECK(w.step(kAttachSeat, kHHorse, false, 3500).rel == SeatRel::Stuck);
    w.set_retire(true);

    // The rider unreadable (its actor gone): forgotten after kSeatLostMs; readable again before that resets it.
    w.seat(kHRider, kHHorse, 1000);
    CHECK(w.step(0xFF, 0, false, 2000).rel == SeatRel::None);
    CHECK(w.step(0xFF, 0, false, 4999).rel == SeatRel::None);
    CHECK(w.step(kAttachSeat, kHHorse, false, 5000).rel == SeatRel::None);
    CHECK(w.step(0xFF, 0, false, 6000).rel == SeatRel::None);
    CHECK(w.step(0xFF, 0, false, 8999).rel == SeatRel::None);
    CHECK(w.step(0xFF, 0, false, 9000).rel == SeatRel::Lost && w.rider() == 0);
    for (size_t c = 0; c < static_cast<size_t>(SeatCause::Count); ++c) {
        CHECK(std::string_view(seat_cause_name(static_cast<SeatCause>(c))) != "?");
    }
}

void test_hold_pos() {
    // Whose early return: the remote player (never the local one), another 0xA actor, or nobody.
    CHECK(hold_who(true, kHRider, kHRider, kGRider) == HoldWho::Remote);
    CHECK(hold_who(false, kHRider, kHRider, kGRider) == HoldWho::None);
    CHECK(hold_who(true, kGRider, kHRider, kGRider) == HoldWho::None);  // the local player
    CHECK(hold_who(true, kHRider, kHRider, kHRider) == HoldWho::None);  // remote == local: never
    CHECK(hold_who(true, 0xA0100777, kHRider, kGRider) == HoldWho::Other);
    CHECK(hold_who(true, kHRider, 0, kGRider) == HoldWho::Other);  // no remote known: only counted
    CHECK(hold_who(true, kHHorse, kHRider, kGRider) == HoldWho::None);  // a horse
    CHECK(hold_who(true, 0, kHRider, kGRider) == HoldWho::None);
    // A pose that may be written.
    const float ok[3] = {512.25f, 611.5f, -44.5f}, zero[3] = {0, 0, 0}, small[3] = {0.3f, 0.2f, 0.1f};
    const float nan[3] = {std::nanf(""), 0, 0}, inf[3] = {0, INFINITY, 0}, huge[3] = {0, 0, 2.0e5f};
    CHECK(hold_pose_ok(ok, -11, -4));
    CHECK(hold_pose_ok(ok, 0, 0));       // sector 0/0 is a real sector
    CHECK(hold_pose_ok(zero, -11, -4));  // the corner of a real sector
    CHECK(!hold_pose_ok(zero, 0, 0));    // the zeroed pose
    CHECK(!hold_pose_ok(small, 0, 0));
    CHECK(hold_pose_ok(small, 1, 0));
    CHECK(!hold_pose_ok(nan, -11, -4) && !hold_pose_ok(inf, -11, -4) && !hold_pose_ok(huge, -11, -4));
    // The decision: mode 1 fills only the remote player with a good pose; probe and other actors are counted.
    CHECK(hold_decide(Mode::On, HoldWho::Remote, true, ok, -11, -4) == HoldAct::Fill);
    CHECK(hold_decide(Mode::On, HoldWho::Remote, false, ok, -11, -4) == HoldAct::Unreadable);
    CHECK(hold_decide(Mode::On, HoldWho::Remote, true, zero, 0, 0) == HoldAct::Unreadable);
    CHECK(hold_decide(Mode::On, HoldWho::Remote, true, nan, -11, -4) == HoldAct::Unreadable);
    CHECK(hold_decide(Mode::Probe, HoldWho::Remote, true, ok, -11, -4) == HoldAct::CountRemote);
    CHECK(hold_decide(Mode::Off, HoldWho::Remote, true, ok, -11, -4) == HoldAct::None);
    CHECK(hold_decide(Mode::On, HoldWho::Other, true, ok, -11, -4) == HoldAct::CountOther);
    CHECK(hold_decide(Mode::Probe, HoldWho::Other, false, zero, 0, 0) == HoldAct::CountOther);
    CHECK(hold_decide(Mode::On, HoldWho::None, true, ok, -11, -4) == HoldAct::None);
    static_assert(kMcEmptyFlag == 0x3D0);
}


// ---------------------------------------------------------------- P8 S18: ride_dismount, the gate, the native policy

// ride_dismount: what ride_filter does with each remote action (rfl::route) and when it asks mount_sync.
void test_dismount_route() {
    using rfl::Dismount;
    using rfl::Kind;
    using rfl::Route;
    CHECK(rfl::parse_dismount("").known && rfl::parse_dismount("").mode == Dismount::Rewrite);
    CHECK(rfl::parse_dismount(" rewrite ").mode == Dismount::Rewrite);
    CHECK(rfl::parse_dismount("partial").mode == Dismount::Partial);
    CHECK(rfl::parse_dismount("native").mode == Dismount::Native && rfl::parse_dismount("1").mode == Dismount::Native);
    CHECK(!rfl::parse_dismount("full").known && rfl::parse_dismount("full").mode == Dismount::Rewrite);
    for (Dismount d : {Dismount::Rewrite, Dismount::Partial, Dismount::Native}) {
        // Mounts: as before (deferred with a transaction, rewritten without).
        CHECK(rfl::route(Kind::Mount, d, false, true) == Route::Defer);
        CHECK(rfl::route(Kind::Mount, d, false, false) == Route::Rewrite);
        CHECK(rfl::wants_mount_sync(Kind::Mount, d, false));
        CHECK(rfl::route(Kind::None, d, false, true) == Route::Pass && !rfl::wants_mount_sync(Kind::None, d, false));
    }
    // rewrite (S17): every dismount, even with a validated transaction (S18's stuck state never happens).
    CHECK(rfl::route(Kind::Dismount, Dismount::Rewrite, false, true) == Route::Rewrite);
    CHECK(rfl::route(Kind::Dismount, Dismount::Rewrite, true, true) == Route::Rewrite);
    CHECK(!rfl::wants_mount_sync(Kind::Dismount, Dismount::Rewrite, false));
    // partial (fix A): only the 0x40 actions; 667E3B35 goes through (or to mount_sync's dispatch).
    CHECK(rfl::route(Kind::Dismount, Dismount::Partial, true, true) == Route::Rewrite);
    CHECK(rfl::route(Kind::Dismount, Dismount::Partial, true, false) == Route::Rewrite);
    CHECK(!rfl::wants_mount_sync(Kind::Dismount, Dismount::Partial, true));
    CHECK(rfl::route(Kind::Dismount, Dismount::Partial, false, true) == Route::Defer);
    CHECK(rfl::route(Kind::Dismount, Dismount::Partial, false, false) == Route::Pass);
    // native (fix B): S18's deferral while the transaction is valid, else S17.
    CHECK(rfl::route(Kind::Dismount, Dismount::Native, true, true) == Route::Defer);
    CHECK(rfl::route(Kind::Dismount, Dismount::Native, false, true) == Route::Defer);
    CHECK(rfl::route(Kind::Dismount, Dismount::Native, true, false) == Route::Rewrite);
    CHECK(rfl::wants_mount_sync(Kind::Dismount, Dismount::Native, true));

    // The 0x40 hashes: fixed + learned, bounded.
    rfl::GateHashes g;
    CHECK(g.contains(0xC9356CEBu) && g.contains(0x412C2C2Bu) && !g.contains(0x667E3B35u) && !g.contains(0));
    CHECK(!g.learn(0xC9356CEBu) && g.learned() == 0);  // already fixed
    CHECK(g.learn(0x11111111u) && g.contains(0x11111111u) && !g.learn(0x11111111u) && g.learned() == 1);
    for (uint32_t i = 0; i < rfl::GateHashes::kSlots + 2; ++i) g.learn(0x22220000u + i);
    CHECK(g.learned() == rfl::GateHashes::kSlots && g.full.load() == 3);
    rfl::Ack a;
    a.ok = true;
    a.key = kGRider;
    a.a0 = 0xC9356CEBu;
    a.a1 = rfl::kDismountA1;
    a.parent = kGHorse;
    CHECK(rfl::classify(a) == rfl::Kind::Dismount && rfl::gate_dismount(a, g));
    a.a0 = 0x667E3B35u;  // the first part (method 4): not a 0x40 action
    CHECK(rfl::classify(a) == rfl::Kind::Dismount && !rfl::gate_dismount(a, rfl::GateHashes{}));
    a.a1 = 0x11111111u;  // a learned hash on layer 1 counts too
    CHECK(rfl::gate_dismount(a, g));
}

// Fix B: the gate detour's decision and the copy it passes.
GateView stuck_view() {
    GateView v;
    v.network_call = true;
    v.remote_rider = true;
    v.c8 = 0;
    v.readable = true;
    v.method = kMethodRideOff;
    v.pending_busy = false;
    v.cur40 = true;
    v.any_new = true;
    v.net_bits = true;
    v.hash0 = true;
    return v;
}
void test_gate_decide() {
    GateView v = stuck_view();
    CHECK(gate_would_drop(v) && gate_decide(v) == GateVerdict::Unlock);
    GateView t = v;
    t.network_call = false;  // the local chart (0x232D4C1) or any other caller
    CHECK(gate_decide(t) == GateVerdict::NotNetwork);
    t = v;
    t.remote_rider = false;
    CHECK(gate_decide(t) == GateVerdict::NotRider);
    t = v;
    t.c8 = 1;  // a local / controlled actor: never
    CHECK(gate_decide(t) == GateVerdict::NotRider);
    t = v;
    t.readable = false;
    CHECK(gate_decide(t) == GateVerdict::Unreadable);
    t = v;
    t.any_new = false;
    CHECK(gate_decide(t) == GateVerdict::Empty && !gate_would_drop(t));
    t = v;
    t.net_bits = false;  // the gate would not test anything
    CHECK(gate_decide(t) == GateVerdict::NoBits && !gate_would_drop(t));
    t = v;
    t.cur40 = false;  // 667E3B35 current (method 4 in S18): nothing to unlock
    CHECK(gate_decide(t) == GateVerdict::NoGate && !gate_would_drop(t));
    t.pend40 = true;  // a pending 0x40 definition drops it as well
    t.method = kMethodRideOff;
    CHECK(gate_would_drop(t) && gate_decide(t) == GateVerdict::Unlock);
    t = v;
    t.method = 4;  // C9356CEB requested but RideOff not entered yet: untouched
    CHECK(gate_would_drop(t) && gate_decide(t) == GateVerdict::NotStuck);
    t.method = 0xFF;
    CHECK(gate_decide(t) == GateVerdict::NotStuck);
    t = v;
    t.pending_busy = true;  // CC+0xA8: a request this frame (could be the dismount itself)
    CHECK(gate_decide(t) == GateVerdict::Busy);
    t.hash1 = true;  // review (6): a whole request (both layers) may replace the pending one
    CHECK(gate_decide(t) == GateVerdict::Unlock);
    // Review (2): 0xEE58B83 copies both entries: never with an empty entry 0.
    t = v;
    t.hash0 = false;
    t.hash1 = true;
    CHECK(gate_would_drop(t) && gate_decide(t) == GateVerdict::NoHash0);
    // Review (1): a current 0x40 action outside method 7 on a copy that never runs its chart is stuck as well.
    t = v;
    t.method = 4;
    t.chart = 0;
    CHECK(gate_stuck(t) && gate_decide(t) == GateVerdict::Unlock);
    t.method = 0xFF;
    CHECK(gate_decide(t) == GateVerdict::Unlock);
    t.chart = 1;  // a copy that runs its chart gets out by itself
    CHECK(!gate_stuck(t) && gate_decide(t) == GateVerdict::NotStuck);
    t = v;
    t.cur40 = false;
    t.pend40 = true;  // only a stale pending 0x40: needs RideOff
    t.method = 4;
    t.chart = 0;
    CHECK(!gate_stuck(t) && gate_decide(t) == GateVerdict::NotStuck);
    t = v;
    t.new40 = true;  // another RideOff (S18 316,56): never let in
    CHECK(gate_decide(t) == GateVerdict::NewDismount);
    t = v;
    t.new52 = true;
    CHECK(gate_decide(t) == GateVerdict::Flag52);
    for (size_t i = 0; i < static_cast<size_t>(GateVerdict::Count); ++i) {
        CHECK(std::string_view(gate_verdict_name(static_cast<GateVerdict>(i))) != "?");
    }
    // The copy: only +0x50 bit 1 and +0x51 bit 3 of each entry change.
    static_assert(kGateBytes == 0xF0 && kGateEntrySize == 0x78 && kEntryNet == 0x50 && kEntryForce == 0x51);
    uint8_t e[kGateBytes];
    for (uint32_t i = 0; i < kGateBytes; ++i) e[i] = 0xFF;
    gate_clear_bits(e);
    for (uint32_t i = 0; i < kGateBytes; ++i) {
        const uint32_t o = i % kGateEntrySize;
        const uint8_t want = o == kEntryNet ? 0xFD : o == kEntryForce ? 0xF7 : 0xFF;
        CHECK(e[i] == want);
    }
    uint8_t z[kGateBytes] = {};
    z[kEntryNet] = 0x02 | 0x01;  // bit 0 (0x1EC9C10 clears it) stays as it is
    z[kGateEntrySize + kEntryForce] = 0x08 | 0x02;
    gate_clear_bits(z);
    CHECK(z[kEntryNet] == 0x01 && z[kGateEntrySize + kEntryForce] == 0x02);
}

// Review (1): drops in a row without an unlock degrade the detour (ride_filter rewrites again), once.
void test_gate_streak() {
    GateStreak g;
    for (uint32_t i = 1; i < kGateDegradeAfter; ++i) CHECK(!g.on(GateVerdict::NotStuck, true));
    CHECK(!g.on(GateVerdict::NotRider, false) && !g.on(GateVerdict::NotNetwork, false));  // other actors: no effect
    CHECK(g.drops == kGateDegradeAfter - 1 && !g.degraded);
    CHECK(!g.on(GateVerdict::Unlock, true) && g.drops == 0);  // an unlock resets
    for (uint32_t i = 1; i < kGateDegradeAfter; ++i) CHECK(!g.on(GateVerdict::Busy, true));
    CHECK(!g.on(GateVerdict::NoGate, false) && g.drops == 0);  // the rider is not stuck any more: resets
    for (uint32_t i = 1; i < kGateDegradeAfter; ++i) CHECK(!g.on(GateVerdict::NewDismount, true));
    CHECK(g.on(GateVerdict::Flag52, true) && g.degraded);
    CHECK(!g.on(GateVerdict::NotStuck, true) && g.degraded);  // reported once
}

// Nativo §5 (4): never commit while the copy is stuck in a previous RideOff.
void test_judge_stuck() {
    PairState s = ready();
    s.rider_method = kMethodRideOff;
    s.rideoff_key = kGHorse;
    CHECK(judge_commit(s, kGRider, kGHorse, 0, 0) == Judge::StuckInRideOff && judge_retry(Judge::StuckInRideOff));
    s.rideoff_key = 0;  // method 7 with nothing cached (Enter failed / reset): not this case
    CHECK(judge_commit(s, kGRider, kGHorse, 0, 0) == Judge::Apply);
    s.rider_method = 0;
    s.rideoff_key = kGHorse;
    CHECK(judge_commit(s, kGRider, kGHorse, 0, 0) == Judge::Apply);
    // Already linked to that horse: idempotent first (nothing is called either way).
    s = ready();
    s.rider_method = kMethodRideOff;
    s.rideoff_key = kGHorse;
    s.cur_vehicle = kGHorse;
    s.cur_seat = 0;
    s.occupant = kGRider;
    CHECK(judge_commit(s, kGRider, kGHorse, 0, 0) == Judge::AlreadyApplied);
    CHECK(ride_method(6) && ride_method(7) && !ride_method(0) && !ride_method(4) && !ride_method(0xFF));
}

RiderNow rider_now(uint32_t link, uint8_t method, uint32_t rider = kGRider) {
    RiderNow r;
    r.rider = rider;
    r.known = true;
    r.link_vehicle = link;
    r.method = method;
    return r;
}
// Nativo §5: with ride_dismount native/partial the observer waits for the native retirement (RideOff's own, in vf89
// -> 0x397F40); only a link still there 2 s after the Detach, outside RideOn/RideOff, is retired (flag 0, p4 0).
void test_native_detach_policy() {
    Table t;
    t.set_policy(DetachPolicy::Native);
    CHECK(t.on_event(ev(EvKind::Begin, 1), 100, kGRider) == Accept::Begun);
    CHECK(t.on_event(ev(EvKind::Commit, 1), 100, kGRider) == Accept::Committed);
    CHECK(t.plan(kGRider, 120).plan == Plan::Commit);
    t.finish_commit(kGRider, 1, Outcome::Applied);
    const Event obs = observed_detach(kGRider, arm_pack(kGHorse, 1), 0x1234, 5);
    CHECK(t.on_event(obs, 1000, kGRider) == Accept::DetachPending);
    // Not at the Detach event, not on foot, not without reading the rider.
    t.note_rider_parent(kGRider, kNoParent, 0, 1000);
    CHECK(t.plan(kGRider, 1001).plan == Plan::None);
    CHECK(t.plan(kGRider, 9000).plan == Plan::None);  // nothing readable: never a blind call
    // Linked, in RideOff (or RideOn): never, however long.
    CHECK(t.plan(kGRider, 9000, rider_now(kGHorse, 7)).plan == Plan::None);
    CHECK(t.plan(kGRider, 9000, rider_now(kGHorse, 6)).plan == Plan::None);
    // Another rider's reading is ignored.
    CHECK(t.plan(kGRider, 9000, rider_now(0, 0, kHRider)).plan == Plan::None);
    // The native retirement ran (S18 transaction 1: the link is 0 in the same frame as RideOff::Enter).
    auto w = t.plan(kGRider, 1030, rider_now(0, 7));
    CHECK(w.plan == Plan::Detach && w.txn.native_gone && !w.txn.native_fallback && w.txn.detach_flag == 0 &&
          w.txn.detach_aux == 0);
    t.finish_detach(kGRider, 1, Outcome::AlreadyApplied);
    CHECK(t.find(kGRider)->phase == Phase::Detached && t.find(kGRider)->linked_here == 0);
    CHECK(t.policy() == DetachPolicy::Native);
    t.reset();
    CHECK(t.policy() == DetachPolicy::Native);  // a new connection keeps the policy

    // The fallback: still linked here 2 s after the Detach, not in 6/7 (RideOff never installed, horse gone...), and
    // (§12) an on-foot action of the rider already dispatched here.
    Table f;
    f.set_policy(DetachPolicy::Native);
    CHECK(f.on_event(ev(EvKind::Begin, 2), 100, kGRider) == Accept::Begun);
    CHECK(f.on_event(ev(EvKind::Commit, 2), 100, kGRider) == Accept::Committed);
    CHECK(f.plan(kGRider, 120).plan == Plan::Commit);
    f.finish_commit(kGRider, 2, Outcome::Applied);
    CHECK(f.on_event(observed_detach(kGRider, arm_pack(kGHorse, 2), 0x1234, 5), 1000, kGRider) == Accept::DetachPending);
    CHECK(f.plan(kGRider, 2999, rider_now(kGHorse, 0)).plan == Plan::None);
    CHECK(f.plan(kGRider, 3000, rider_now(kGHorse, 0)).plan == Plan::None);  // no on-foot action here yet
    f.note_rider_parent(kGRider, kNoParent, 0, 1900);
    w = f.plan(kGRider, 3000, rider_now(kGHorse, 0));
    CHECK(w.plan == Plan::Detach && w.txn.native_fallback && !w.txn.native_gone && w.txn.detach_flag == kNativeDetachFlag &&
          w.txn.detach_aux == 0);
    CHECK(f.plan(kGRider, 3001, rider_now(kGHorse, 0)).plan == Plan::None);  // claimed once

    // A NON-observed Detach (an NPC-style 0x2228E40 on the controller, flag/p4 its own): the same native wait.
    Table n;
    n.set_policy(DetachPolicy::Native);
    CHECK(n.on_event(ev(EvKind::Begin, 3), 100, kGRider) == Accept::Begun);
    CHECK(n.on_event(ev(EvKind::Commit, 3), 100, kGRider) == Accept::Committed);
    CHECK(n.plan(kGRider, 120).plan == Plan::Commit);
    n.finish_commit(kGRider, 3, Outcome::Applied);
    Event d = ev(EvKind::Detach, 3);
    d.fifth = 12;
    d.aux = 7;
    CHECK(n.on_event(d, 1000, kGRider) == Accept::DetachPending);
    n.note_ride_off_enter(kGRider);  // S18 policy would retire now; native waits for the link
    CHECK(n.plan(kGRider, 1001, rider_now(kGHorse, 7)).plan == Plan::None);
    w = n.plan(kGRider, 1002, rider_now(0, 7));
    CHECK(w.plan == Plan::Detach && w.txn.native_gone && w.txn.detach_flag == 0 && w.txn.detach_aux == 0);

    // No Detach at all: the 6 s on-foot fallback keeps the method condition and uses flag 0.
    Table e;
    e.set_policy(DetachPolicy::Native);
    CHECK(e.on_event(ev(EvKind::Begin, 4), 100, kGRider) == Accept::Begun);
    CHECK(e.on_event(ev(EvKind::Commit, 4), 100, kGRider) == Accept::Committed);
    CHECK(e.plan(kGRider, 120).plan == Plan::Commit);
    e.finish_commit(kGRider, 4, Outcome::Applied);
    e.note_rider_parent(kGRider, kNoParent, 0, 1000);
    CHECK(e.plan(0xB0000001, 7000).plan == Plan::None);                         // unreadable
    CHECK(e.plan(0xB0000001, 7000, rider_now(kGHorse, 7)).plan == Plan::None);  // in RideOff
    w = e.plan(0xB0000001, 7000, rider_now(kGHorse, 0));
    CHECK(w.plan == Plan::Detach && w.txn.no_event && w.txn.detach_flag == kNativeDetachFlag && !w.txn.native_gone);

    // Review (3): a rider that cannot be read here for 15 s: forgotten (nothing called), never work for ever.
    Table u;
    u.set_policy(DetachPolicy::Native);
    CHECK(u.on_event(ev(EvKind::Begin, 6), 100, kGRider) == Accept::Begun);
    CHECK(u.on_event(ev(EvKind::Commit, 6), 100, kGRider) == Accept::Committed);
    CHECK(u.plan(kGRider, 120).plan == Plan::Commit);
    u.finish_commit(kGRider, 6, Outcome::Applied);
    CHECK(u.on_event(observed_detach(kGRider, arm_pack(kGHorse, 6), 0x1234, 5), 1000, kGRider) == Accept::DetachPending);
    CHECK(u.plan(kGRider, 1000).plan == Plan::None);                           // unreadable since 1000
    CHECK(u.plan(kGRider, 9000, rider_now(kGHorse, 7)).plan == Plan::None);    // readable again: the clock restarts
    CHECK(u.plan(kGRider, 9001).plan == Plan::None);
    CHECK(u.plan(kGRider, 24000).plan == Plan::None && u.has_work());
    w = u.plan(kGRider, 24001);
    CHECK(w.plan == Plan::Detach && w.txn.native_lost && !w.txn.native_gone && !w.txn.native_fallback);
    u.finish_detach(kGRider, 6, Outcome::Refused);
    CHECK(u.find(kGRider)->phase == Phase::Detached && !u.has_work() && !u.defers(kGRider, 24002));
    CHECK(u.find(kGRider)->linked_here == kGHorse);  // still remembered for a RetireFirst
    // ... and an on-foot Riding candidate (no Detach) the same.
    Table v2;
    v2.set_policy(DetachPolicy::Native);
    CHECK(v2.on_event(ev(EvKind::Begin, 7), 100, kGRider) == Accept::Begun);
    CHECK(v2.on_event(ev(EvKind::Commit, 7), 100, kGRider) == Accept::Committed);
    CHECK(v2.plan(kGRider, 120).plan == Plan::Commit);
    v2.finish_commit(kGRider, 7, Outcome::Applied);
    v2.note_rider_parent(kGRider, kNoParent, 0, 1000);
    CHECK(v2.plan(0xB0000001, 1000).plan == Plan::None && v2.has_work());
    w = v2.plan(0xB0000001, 16000);
    CHECK(w.plan == Plan::Detach && w.txn.native_lost);
    v2.finish_detach(kGRider, 7, Outcome::Refused);
    CHECK(!v2.has_work());

    // The S18 policy is unchanged by a reading (rewrite mode: first on-foot dispatch, flag 12).
    Table s18;
    CHECK(s18.policy() == DetachPolicy::S18);
    CHECK(s18.on_event(ev(EvKind::Begin, 5), 100, kGRider) == Accept::Begun);
    CHECK(s18.on_event(ev(EvKind::Commit, 5), 100, kGRider) == Accept::Committed);
    CHECK(s18.plan(kGRider, 120).plan == Plan::Commit);
    s18.finish_commit(kGRider, 5, Outcome::Applied);
    CHECK(s18.on_event(observed_detach(kGRider, arm_pack(kGHorse, 5), 0x1234, 5), 1000, kGRider) == Accept::DetachPending);
    s18.note_rider_parent(kGRider, kNoParent, 0, 1100);
    w = s18.plan(kGRider, 1100, rider_now(kGHorse, 7));
    CHECK(w.plan == Plan::Detach && w.txn.detach_flag == kClientDetachFlag && !w.txn.native_gone && !w.txn.native_fallback);
}

// §12: the guest observes the host's rider (mode 1, ride_dismount=native).
void test_guest_observer() {
    // Only the host's main character (the host's control state 4); unknown (0, e.g. after end_remote) = nothing.
    {
        Table t;
        t.set_policy(DetachPolicy::Native);
        CHECK(t.on_event(ev(EvKind::Begin, 1, 0x77, kHRider, kHHorse), 100, 0) == Accept::NotRemote);
        CHECK(t.on_event(ev(EvKind::Begin, 1, 0x77, 0xA0100002, kHHorse), 100, kHRider) == Accept::NotRemote);
        CHECK(t.on_event(ev(EvKind::Begin, 1, 0x77, kHRider, kHHorse), 100, kHRider) == Accept::Begun);
    }
    // The guest's stall: the host's Detach (a control frame) is in the table at once; its dismount action still waits
    // in the inbound queue, the main thread dispatches nothing for 5 s, then the backlog in order. The first dispatch
    // of the backlog (another actor's) must NOT retire the link before the dismount's RideOff::Enter.
    {
        Table t;
        t.set_policy(DetachPolicy::Native);
        CHECK(t.on_event(ev(EvKind::Begin, 1, 0x77, kHRider, kHHorse), 100, kHRider) == Accept::Begun);
        CHECK(t.on_event(ev(EvKind::Commit, 1, 0x77, kHRider, kHHorse), 100, kHRider) == Accept::Committed);
        CHECK(t.plan(0xB0000123, 110).plan == Plan::Commit);  // claimed at an NPC's dispatch (the guest has many)
        t.finish_commit(kHRider, 1, Outcome::Applied);
        // Riding: mounted actions (parent = its horse) keep it mounted.
        t.note_rider_parent(kHRider, kHHorse, 0, 500);
        CHECK(t.on_event(observed_detach(kHRider, arm_pack(kHHorse, 1), 0x77, 9), 1000, kHRider) == Accept::DetachPending);
        // 5 s later the backlog: an NPC's 2991 first (the rider still linked, method 0 riding).
        CHECK(t.plan(0xB0000123, 6000, rider_now(kHHorse, 0, kHRider)).plan == Plan::None);
        // A queued mounted action of the rider (before the dismount in the stream): still nothing.
        t.note_rider_parent(kHRider, kHHorse, 0, 6001);
        CHECK(t.plan(kHRider, 6001, rider_now(kHHorse, 0, kHRider)).plan == Plan::None);
        // The dismount's RideOff::Enter: the native retirement clears the link (vf89 -> 0x397F40): Gone.
        auto w = t.plan(0xB0000123, 6002, rider_now(0, 7, kHRider));
        CHECK(w.plan == Plan::Detach && w.txn.native_gone && !w.txn.native_fallback);
        t.finish_detach(kHRider, 1, Outcome::AlreadyApplied);
        CHECK(t.find(kHRider)->phase == Phase::Detached && !t.has_work() && !t.defers(kHRider, 6003));
    }
    // The dismount rewritten at the dispatch (fix B lost): no RideOff here, the link stays; the next on-foot action
    // lets the fallback (flag 0) retire it once 2 s passed since the Detach.
    {
        Table t;
        t.set_policy(DetachPolicy::Native);
        CHECK(t.on_event(ev(EvKind::Begin, 2, 0x77, kHRider, kHHorse), 100, kHRider) == Accept::Begun);
        CHECK(t.on_event(ev(EvKind::Commit, 2, 0x77, kHRider, kHHorse), 100, kHRider) == Accept::Committed);
        CHECK(t.plan(kHRider, 110).plan == Plan::Commit);
        t.finish_commit(kHRider, 2, Outcome::Applied);
        CHECK(t.on_event(observed_detach(kHRider, arm_pack(kHHorse, 2), 0x77, 9), 1000, kHRider) == Accept::DetachPending);
        t.note_rider_parent(kHRider, kHHorse, 0, 1010);  // the rewritten dismount: riding block, the horse its parent
        CHECK(t.plan(kHRider, 1010, rider_now(kHHorse, 0, kHRider)).plan == Plan::None);
        t.note_rider_parent(kHRider, kNoParent, 0, 1900);  // on foot (841A7C99)
        CHECK(t.plan(kHRider, 1900, rider_now(kHHorse, 0, kHRider)).plan == Plan::None);  // < 2 s
        auto w = t.plan(kHRider, 3000, rider_now(kHHorse, 0, kHRider));
        CHECK(w.plan == Plan::Detach && w.txn.native_fallback && w.txn.detach_flag == kNativeDetachFlag);
        // Retired here: nothing of this client's link is remembered any more.
        t.finish_detach(kHRider, 2, Outcome::Applied);
        CHECK(t.find(kHRider)->linked_here == 0);
    }
    // Review of 458cf49 (3): an on-foot mark from BEFORE the Detach does not count (the Detach resets it); the cap
    // retires a link left over after 30 s without any on-foot action (never in 6/7).
    {
        Table t;
        t.set_policy(DetachPolicy::Native);
        CHECK(t.on_event(ev(EvKind::Begin, 3, 0x77, kHRider, kHHorse), 100, kHRider) == Accept::Begun);
        CHECK(t.on_event(ev(EvKind::Commit, 3, 0x77, kHRider, kHHorse), 100, kHRider) == Accept::Committed);
        CHECK(t.plan(kHRider, 110).plan == Plan::Commit);
        t.finish_commit(kHRider, 3, Outcome::Applied);
        t.note_rider_parent(kHRider, kNoParent, 0, 500);  // a stale on-foot action while Riding (before the Detach)
        CHECK(t.on_event(observed_detach(kHRider, arm_pack(kHHorse, 3), 0x77, 9), 1000, kHRider) == Accept::DetachPending);
        CHECK(t.find(kHRider)->on_foot_ms == 0);
        // The dismount still queued (stall): 2 s and more, method 0, link there -> nothing.
        CHECK(t.plan(0xB0000123, 3000, rider_now(kHHorse, 0, kHRider)).plan == Plan::None);
        CHECK(t.plan(0xB0000123, 30999, rider_now(kHHorse, 0, kHRider)).plan == Plan::None);
        CHECK(t.plan(0xB0000123, 31000, rider_now(kHHorse, 7, kHRider)).plan == Plan::None);  // in RideOff: never
        auto w = t.plan(0xB0000123, 31000, rider_now(kHHorse, 0, kHRider));
        CHECK(w.plan == Plan::Detach && w.txn.native_fallback && w.txn.detach_flag == kNativeDetachFlag);
        // The S18 policy keeps its own rule (the on-foot mark is not reset there).
        Table s;
        CHECK(s.on_event(ev(EvKind::Begin, 4, 0x77, kHRider, kHHorse), 100, kHRider) == Accept::Begun);
        CHECK(s.on_event(ev(EvKind::Commit, 4, 0x77, kHRider, kHHorse), 100, kHRider) == Accept::Committed);
        CHECK(s.plan(kHRider, 110).plan == Plan::Commit);
        s.finish_commit(kHRider, 4, Outcome::Applied);
        s.note_rider_parent(kHRider, kNoParent, 0, 500);
        CHECK(s.on_event(observed_detach(kHRider, arm_pack(kHHorse, 4), 0x77, 9), 1000, kHRider) == Accept::DetachPending);
        CHECK(s.find(kHRider)->on_foot_ms == 500);
    }
    // The dispatch's rewrite: no link, or a dismount whose fix B was lost after ride_filter deferred it.
    CHECK(fallback_rewrite(true, true, false));
    CHECK(!fallback_rewrite(true, true, true));
    CHECK(fallback_rewrite(true, true, true, true));
    CHECK(!fallback_rewrite(false, true, true, true));   // ride_filter not 1: the packet was never ours
    CHECK(!fallback_rewrite(true, false, true, true));   // not a ride action
    CHECK(dismount_gate_lost(true, true, false));
    CHECK(!dismount_gate_lost(true, true, true));        // fix B ready: RideOff and the unlock (S19)
    CHECK(!dismount_gate_lost(true, false, false));      // partial / rewrite: ride_filter decided already
    CHECK(!dismount_gate_lost(false, true, false));      // a mount
    // The self-check: a 2223 naming the remote rider (1) or its horse (2); never another key or opcode.
    uint8_t req[kHeader + 8] = {0xAF, 0x08, 0xFF, 8, 0};  // 2223 = 0x08AF
    put32(req + kHeader, kHRider);
    CHECK(self_check_hit(req, sizeof req, kHRider, kHHorse) == 1);
    put32(req + kHeader, kHHorse);
    CHECK(self_check_hit(req, sizeof req, kHRider, kHHorse) == 2);
    CHECK(self_check_hit(req, sizeof req, kHRider, 0) == 0);  // no transaction: the horse is unknown
    put32(req + kHeader, kGRider);
    CHECK(self_check_hit(req, sizeof req, kHRider, kHHorse) == 0);  // the guest's own actions
    put32(req + kHeader, kHRider);
    req[0] = 0xAE;  // 2222
    CHECK(self_check_hit(req, sizeof req, kHRider, kHHorse) == 0);
    req[0] = 0xAF;
    CHECK(self_check_hit(req, kHeader + 3, kHRider, kHHorse) == 0);  // too short
    CHECK(self_check_hit(req, sizeof req, 0, 0) == 0);              // keys unknown
}
}  // namespace

// ---------------------------------------------------------------- P8 S26 (docs/research/s25/montura-regresion.md)

// The host's rider seen on the guest, with the guest's real times of S26 step 2 (capture t in ms).
RiderAction own_action(uint32_t target, uint64_t parent = kNoParent) {
    RiderAction a;
    a.own = true;
    a.parent = parent;
    a.target = target;
    return a;
}
Event host_ev(EvKind k, uint32_t transition) { return ev(k, transition, 0x6F32C5F1, kHRider, kHHorse); }

// Mount 1 (seated 40.6 s): the Commit at t=221171, applied at ANOTHER actor's dispatch (t=221179), the mount action at
// t=221195 found its link: nothing applied again. RideOn ran in its handler; later actions never relink.
void test_relink_s26_ride1() {
    Table t;
    t.set_policy(DetachPolicy::Native);
    t.set_relink(Mode::On);
    CHECK(t.on_event(host_ev(EvKind::Begin, 1), 221171, kHRider) == Accept::Begun);
    CHECK(t.on_event(host_ev(EvKind::Commit, 1), 221171, kHRider) == Accept::Committed);
    auto w = t.plan(0xB0100420, 221179);  // another actor's 2991
    CHECK(w.plan == Plan::Commit && !w.relink && !w.txn.claimed_at_rider);
    t.finish_commit(kHRider, 1, Outcome::Applied);
    CHECK(t.find(kHRider)->applied && t.find(kHRider)->phase == Phase::Riding);
    // The mount action (A0 0x91605616, parent -1, target the horse): its link is there.
    t.note_rider_parent(kHRider, kNoParent, kHHorse, 221195);
    w = t.plan(kHRider, 221195, rider_now(kHHorse, 0, kHRider), own_action(kHHorse));
    CHECK(w.plan == Plan::None && !w.relink);
    t.note_ride_on_enter(kHRider);
    CHECK(t.find(kHRider)->ride_on_entered);
    // Riding (parent = the horse): never a relink, whatever the link reads.
    t.note_rider_parent(kHRider, kHHorse, 0, 223123);
    CHECK(t.plan(kHRider, 223123, rider_now(0, 6, kHRider), own_action(0, kHHorse)).plan == Plan::None);
    CHECK(t.find(kHRider)->relinks == 0 && t.find(kHRider)->attempts == 1);
}

// Mount 2 (the S26 failure): the pre-mount action (A0 0, A1 0xCE35CA5D, target the horse) arrived 6 ms BEFORE the
// Commit and was dispatched 6 ms AFTER it: the commit was applied there; its handler retired the link natively; the
// mount action 13 ms later found Link+0x10 == 0. Now: applied again before the mount action's handler.
void test_relink_s26_ride2() {
    Table t;
    t.set_policy(DetachPolicy::Native);
    t.set_relink(Mode::On);
    CHECK(t.on_event(host_ev(EvKind::Begin, 2), 309174, kHRider) == Accept::Begun);
    CHECK(t.on_event(host_ev(EvKind::Commit, 2), 309174, kHRider) == Accept::Committed);
    // The pre-mount action's dispatch (t=309180): the commit is claimed there (at the rider's own action).
    t.note_rider_parent(kHRider, kNoParent, kHHorse, 309180);
    auto w = t.plan(kHRider, 309180, rider_now(0, 1, kHRider), own_action(kHHorse));
    CHECK(w.plan == Plan::Commit && !w.relink && w.txn.claimed_at_rider && w.txn.attempts == 1);
    t.finish_commit(kHRider, 2, Outcome::Applied);
    // ... its handler retired the link (S26: Link 0, method 1). The mount action (t=309193): applied again.
    t.note_rider_parent(kHRider, kNoParent, kHHorse, 309193);
    w = t.plan(kHRider, 309193, rider_now(0, 1, kHRider), own_action(kHHorse));
    CHECK(w.plan == Plan::Commit && w.relink && w.txn.attempts == 2 && w.txn.relinks == 1 && w.txn.horse == kHHorse &&
          w.txn.transition == 2);
    CHECK(t.find(kHRider)->phase == Phase::Applying);
    CHECK(t.plan(kHRider, 309194, rider_now(0, 1, kHRider), own_action(kHHorse)).plan == Plan::None);  // claimed
    t.finish_commit(kHRider, 2, Outcome::Applied);
    CHECK(t.find(kHRider)->phase == Phase::Riding && t.find(kHRider)->linked_here == kHHorse);
    // Its RideOn::Enter ran: from here on the link is RideOn's, never applied again (the next action at t=311122).
    t.note_ride_on_enter(kHRider);
    t.note_rider_parent(kHRider, kHHorse, 0, 311122);
    CHECK(t.plan(kHRider, 311122, rider_now(0, 6, kHRider), own_action(0, kHHorse)).plan == Plan::None);
    // The dismount still goes the native way (RideOff, the Detach, already applied).
    CHECK(t.on_event(host_ev(EvKind::Detach, 2), 326830, kHRider) == Accept::DetachPending);
}

// What never relinks: another actor's dispatch, an action that does not name the horse, a reading of the link that
// is not 0 (another horse: the judge's business, not this rule's), a Detach / RideOff seen, a probe transaction,
// past kApplyWaitMs, past kMaxAttempts; a NotReady at the relink cancels (ride_filter's fallback, as before).
void test_relink_bounds() {
    auto ridden = [](Table& t, uint32_t transition, uint64_t at, Outcome o = Outcome::Applied) {
        CHECK(t.on_event(host_ev(EvKind::Begin, transition), at, kHRider) == Accept::Begun);
        CHECK(t.on_event(host_ev(EvKind::Commit, transition), at, kHRider) == Accept::Committed);
        CHECK(t.plan(0xB0100420, at + 1).plan == Plan::Commit);
        t.finish_commit(kHRider, transition, o);
    };
    {
        Table t;
        t.set_relink(Mode::On);
        ridden(t, 1, 1000);
        CHECK(t.plan(0xB0100420, 1010, rider_now(0, 1, kHRider)).plan == Plan::None);           // another actor
        CHECK(t.plan(kHRider, 1010, rider_now(0, 1, kHRider), own_action(0)).plan == Plan::None);  // names no horse
        CHECK(t.plan(kHRider, 1010, rider_now(0xB0100099, 1, kHRider), own_action(kHHorse)).plan == Plan::None);
        RiderNow unknown = rider_now(0, 1, kHRider);
        unknown.known = false;
        CHECK(t.plan(kHRider, 1010, unknown, own_action(kHHorse)).plan == Plan::None);  // unreadable: nothing
        CHECK(t.plan(kHRider, 1010, rider_now(0, 1, kGRider), own_action(kHHorse)).plan == Plan::None);  // other rider
        CHECK(t.plan(kHRider, 1000 + kApplyWaitMs + 1, rider_now(0, 1, kHRider), own_action(kHHorse)).plan ==
              Plan::None);  // past the commit's wait
        CHECK(t.plan(kHRider, 1020, rider_now(0, 1, kHRider), own_action(kHHorse)).relink);  // and now it does
    }
    {
        Table t;  // a Detach of the controller came: retired, never linked again
        t.set_relink(Mode::On);
        ridden(t, 1, 1000);
        CHECK(t.on_event(host_ev(EvKind::Detach, 1), 1005, kHRider) == Accept::DetachPending);
        CHECK(!t.plan(kHRider, 1010, rider_now(0, 1, kHRider), own_action(kHHorse)).relink);
    }
    {
        Table t;  // its RideOff::Enter ran here
        t.set_relink(Mode::On);
        ridden(t, 1, 1000);
        t.note_ride_off_enter(kHRider);
        CHECK(t.plan(kHRider, 1010, rider_now(0, 7, kHRider), own_action(kHHorse)).plan == Plan::None);
    }
    {
        Table t;  // an on-foot action not naming the horse came between: not this rule's (no-event / native paths)
        t.set_relink(Mode::On);
        ridden(t, 1, 1000);
        t.note_rider_parent(kHRider, kNoParent, 0, 1005);
        CHECK(!t.plan(kHRider, 1010, rider_now(0, 1, kHRider), own_action(kHHorse)).relink);
    }
    {
        Table t;  // probe: nothing was applied here
        t.set_relink(Mode::On);
        ridden(t, 1, 1000, Outcome::ProbeOnly);
        CHECK(t.plan(kHRider, 1010, rider_now(0, 1, kHRider), own_action(kHHorse)).plan == Plan::None);
    }
    {
        Table t;  // a NotReady at the relink: cancelled (claimed at the rider's own action)
        t.set_relink(Mode::On);
        ridden(t, 1, 1000);
        auto w = t.plan(kHRider, 1010, rider_now(0, 1, kHRider), own_action(kHHorse));
        CHECK(w.relink);
        t.finish_commit(kHRider, 1, Outcome::NotReady);
        CHECK(t.find(kHRider)->phase == Phase::Cancelled);
        CHECK(t.plan(kHRider, 1020, rider_now(0, 1, kHRider), own_action(kHHorse)).plan == Plan::None);
    }
    {
        Table t;  // the game keeps retiring it: at most kMaxAttempts claims in all
        t.set_relink(Mode::On);
        ridden(t, 1, 1000);
        uint32_t relinks = 0;
        for (uint32_t i = 0; i < 2 * kMaxAttempts; ++i) {
            auto w = t.plan(kHRider, 1010 + i, rider_now(0, 1, kHRider), own_action(kHHorse));
            if (w.plan != Plan::Commit) break;
            CHECK(w.relink);
            ++relinks;
            t.finish_commit(kHRider, 1, Outcome::Applied);
        }
        CHECK(relinks == kMaxAttempts - 1);  // the first claim was the commit itself
        CHECK(t.find(kHRider)->attempts == kMaxAttempts);
    }
}

// Review of bf255bb: mount_relink Off (the default) / probe; only pre-mount actions (parent -1, target the horse);
// a failed relink stays with its transaction (no pair quarantine), unlike a failed first apply.
void test_relink_review() {
    auto ridden = [](Table& t, uint32_t transition, uint64_t at) {
        CHECK(t.on_event(host_ev(EvKind::Begin, transition), at, kHRider) == Accept::Begun);
        CHECK(t.on_event(host_ev(EvKind::Commit, transition), at, kHRider) == Accept::Committed);
        CHECK(t.plan(0xB0100420, at + 1).plan == Plan::Commit);
        t.finish_commit(kHRider, transition, Outcome::Applied);
    };
    {
        Table t;  // Off by default: never, even when due
        CHECK(t.relink() == Mode::Off);
        ridden(t, 1, 1000);
        CHECK(t.plan(kHRider, 1010, rider_now(0, 1, kHRider), own_action(kHHorse)).plan == Plan::None);
    }
    {
        Table t;  // probe: reported once per transaction, nothing claimed or changed
        t.set_relink(Mode::Probe);
        ridden(t, 1, 1000);
        auto w = t.plan(kHRider, 1010, rider_now(0, 1, kHRider), own_action(kHHorse));
        CHECK(w.plan == Plan::None && w.relink_probe && !w.relink && w.txn.horse == kHHorse && w.txn.transition == 1);
        CHECK(t.find(kHRider)->phase == Phase::Riding && t.find(kHRider)->attempts == 1 &&
              t.find(kHRider)->relinks == 0);
        w = t.plan(kHRider, 1020, rider_now(0, 1, kHRider), own_action(kHHorse));
        CHECK(w.plan == Plan::None && !w.relink_probe);  // once per transaction
        ridden(t, 2, 5000);                              // a new mount: reported again
        CHECK(t.plan(kHRider, 5010, rider_now(0, 1, kHRider), own_action(kHHorse)).relink_probe);
    }
    {
        Table t;  // a mounted action (parent = the horse) never qualifies
        t.set_relink(Mode::On);
        ridden(t, 1, 1000);
        CHECK(t.plan(kHRider, 1010, rider_now(0, 1, kHRider), own_action(0, kHHorse)).plan == Plan::None);
        CHECK(t.plan(kHRider, 1010, rider_now(0, 1, kHRider), own_action(kHHorse, kHHorse)).plan == Plan::None);
        CHECK(t.plan(kHRider, 1010, rider_now(0, 1, kHRider), own_action(kHHorse)).relink);  // pre-mount: yes
    }
    {
        Table t;  // a failed relink: this transaction only
        t.set_relink(Mode::On);
        ridden(t, 1, 1000);
        auto w = t.plan(kHRider, 1010, rider_now(0, 1, kHRider), own_action(kHHorse));
        CHECK(w.relink && w.txn.relinking);
        t.finish_commit(kHRider, 1, Outcome::Failed);
        CHECK(t.find(kHRider)->phase == Phase::Quarantined && !t.find(kHRider)->relinking);
        CHECK(!t.quarantined(kHRider, kHHorse));
        CHECK(t.on_event(host_ev(EvKind::Detach, 1), 1100, kHRider) == Accept::DetachPending);  // one retirement
        CHECK(t.on_event(host_ev(EvKind::Begin, 2), 5000, kHRider) == Accept::Begun);
        CHECK(t.on_event(host_ev(EvKind::Commit, 2), 5000, kHRider) == Accept::Committed);  // the next mount: as usual
        Table u;  // a failed FIRST apply still quarantines the pair, as before
        u.set_relink(Mode::On);
        CHECK(u.on_event(host_ev(EvKind::Begin, 1), 100, kHRider) == Accept::Begun);
        CHECK(u.on_event(host_ev(EvKind::Commit, 1), 100, kHRider) == Accept::Committed);
        CHECK(u.plan(kHRider, 101).plan == Plan::Commit);
        u.finish_commit(kHRider, 1, Outcome::Failed);
        CHECK(u.quarantined(kHRider, kHHorse));
    }
}

// The controller's Status (kind 4): wire form, and never a transaction.
void test_status_event() {
    const Event s = status_event(kHRider, 0x6F32C5F1, 12345, true);
    const auto f = frame(s);
    CHECK(is_frame(f));
    const auto d = decode(std::span<const uint8_t>(f).subspan(kHeader));
    CHECK(d && d->kind == EvKind::Status && d->rider == kHRider && d->horse == 0 && (d->flags & kFlagNoPublish) &&
          d->nonce == 0x6F32C5F1 && d->tick_ms == 12345);
    // The key not known yet: rider 0 is a valid Status.
    const auto f0 = frame(status_event(0, 1, 1, true));
    CHECK(decode(std::span<const uint8_t>(f0).subspan(kHeader)).has_value());
    // A Status naming a horse, or a non-character rider: rejected.
    Event bad = s;
    bad.horse = kHHorse;
    CHECK(!decode(std::span<const uint8_t>(frame(bad)).subspan(kHeader)));
    bad = s;
    bad.rider = 0xB0100003;
    CHECK(!decode(std::span<const uint8_t>(frame(bad)).subspan(kHeader)));
    // Unknown kinds beyond Status stay rejected.
    auto f5 = frame(s);
    f5[kHeader + 2] = 5;
    CHECK(!decode(std::span<const uint8_t>(f5).subspan(kHeader)));
    // The table ignores it: a riding transaction of the same rider is not renewed (another nonce in the Status).
    Table t;
    CHECK(t.on_event(host_ev(EvKind::Begin, 1), 100, kHRider) == Accept::Begun);
    CHECK(t.on_event(host_ev(EvKind::Commit, 1), 100, kHRider) == Accept::Committed);
    CHECK(t.on_event(status_event(kHRider, 0x9999, 101, true), 101, kHRider) == Accept::NoTxn);
    CHECK(t.find(kHRider)->phase == Phase::Committed && t.find(kHRider)->transition == 1);
}

int main() {
    test_wire();
    test_publishable();
    test_judge();
    test_verify();
    test_table_mount();
    test_table_failures();
    test_table_nonce_and_detach_force();
    test_fallback_and_hold();
    test_outbound_drain_order();
    test_ctl_link_watch();
    test_no_event_detach();
    test_ride_geometry();
    test_detach_guard();
    test_review_lifecycle();
    test_dismount_route();
    test_gate_decide();
    test_gate_streak();
    test_judge_stuck();
    test_native_detach_policy();
    test_guest_observer();
    test_seat_judge();
    test_seat_verify();
    test_seat_release();
    test_hold_pos();
    test_relink_s26_ride1();
    test_relink_s26_ride2();
    test_relink_bounds();
    test_relink_review();
    test_status_event();
    if (g_failures) {
        std::printf("mount_sync tests: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("mount_sync tests: all passed\n");
    return 0;
}
