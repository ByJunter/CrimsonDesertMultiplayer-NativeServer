// P8 S2: the native 2322 for user 2 as an observer (src/mod/session2/observer_milestone_logic.h,
// load_trip_logic.h; docs/research/pendientes/p8-2322-observador.md).
//
// SCOPE: offline logic only. The host's trip ledger and its decision are driven
// with the sequences take_frame / inject / the entry hook produce (order,
// duplicates, invalidation, reconnection, queue failure, wrong recipient,
// switch off), and the guest's loading-cycle tracker with the second trip of
// P8 session 5 (guest capture cdmp_packets_20261009-194117.bin: 2174 #1190,
// 2549 #1451, 2144 #9114, 3066 #9132, no 2322, 0x11 -> 0x18 after 250 s) and
// its first trip (2144 #797, 3066 #798, 2322 #802, 2445 #1143, 2762 #1144).
// They do NOT test the hooks, the byte checks, the ABI of 0x2A757A0 /
// 0x2999D80, the thread the game runs them on, nor the game's behaviour: that
// is the static analysis of the document and the Shadow session.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "mod/session2/load_trip_logic.h"
#include "mod/session2/observer_milestone_logic.h"
#include "mod/session2/observer_milestone_reload_logic.h"
#include "mod/session2/travel_probe_logic.h"

using namespace cdmp::session2;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

struct Sink final : oms::NoteSink {
    std::vector<p8p::Event> notes;
    void note(const p8p::Event& e) override { notes.push_back(e); }
    size_t count(uint16_t kind) const {
        size_t n = 0;
        for (const auto& e : notes) n += e.kind == kind;
        return n;
    }
    const p8p::Event* last(uint16_t kind) const {
        for (auto it = notes.rbegin(); it != notes.rend(); ++it) {
            if (it->kind == kind) return &*it;
        }
        return nullptr;
    }
};

// The values of session 5 (GL / HL): user 2 0x90100333 / its Kliff 0xA0100334, the host 0x90100000.
constexpr uint32_t kUser2 = 0x90100333, kActor2 = 0xA0100334, kHost = 0x90100000;
constexpr uintptr_t kSession2 = 0x2000, kSession2b = 0x3000, kField = 0x7000, kActor = 0x8000, kUserObj = 0x9000;
constexpr uint32_t kLogic = 29232;  // the server's logic thread (T29232 in the live C2 test)
constexpr uint32_t kRecv = 15520;   // another server thread

// A clean observer entry of user 2 into the host's field, on the logic thread.
oms::EntryFacts facts(uint32_t epoch = 1, uintptr_t live = kSession2) {
    oms::EntryFacts f;
    f.tid = kLogic;
    f.t_us = 1'000'000;
    f.epoch = epoch;
    f.reads_ok = true;
    f.field = kField;
    f.actor = kActor;
    f.user = kUserObj;
    f.user_session = live;
    f.main_character = kActor;
    f.fm_field = kField;
    f.field_owner = kHost;
    f.field_key = 0x1B;
    f.user_key = kUser2;
    f.actor_key = kActor2;
    f.live_session = live;
    f.user2_user_key = kUser2;
    f.user2_actor_key = kActor2;
    f.host_user_key = kHost;
    f.host_field = kField;
    f.realm = true;
    f.send_ctx = 0x5000;
    return f;
}

// take_frame's sequence for one shared-field trip: 2174 (owner = the host), 2144 and 3066 on the logic thread.
void trip_until_entry(oms::Ledger& l, Sink& s, bool push2144 = true, uint32_t tid3066 = kLogic) {
    l.on_out(oms::kOpMoveFieldAck, true, true, kRecv, 100, kHost, s);
    l.on_in(oms::kOpMoveFieldCompleteReq, 200, s);
    l.on_out(oms::kOpMoveFieldCompleteAck, true, push2144, kLogic, 300, 0, s);
    l.on_out(oms::kOpMoveFieldCompletePostAck, true, true, tid3066, 310, 0, s);
}

// --------------------------------------------------------------------------------------------- switch

void test_switch() {
    CHECK(oms::choose_mode(false, false) == oms::Mode::Off);
    CHECK(oms::choose_mode(true, false) == oms::Mode::DryRun);
    CHECK(oms::choose_mode(false, true) == oms::Mode::Send);  // send implies the decision
    CHECK(oms::choose_mode(true, true) == oms::Mode::Send);
    // Off: the ledger never decides to emit, whatever happens.
    oms::Ledger l(oms::Mode::Off);
    Sink s;
    trip_until_entry(l, s);
    const oms::Decision d = l.decide(facts(), s);
    CHECK(!d.emit);
    CHECK(d.why == oms::Why::Off);
    CHECK(!l.emitting(kLogic));
    CHECK(s.count(oms::kNDecision) == 0);
}

// --------------------------------------------------------------------------------------------- order

void test_order_and_emit() {
    oms::Ledger l(oms::Mode::Send);
    Sink s;
    trip_until_entry(l, s);
    CHECK(l.trip().gen == 1);
    CHECK(l.trip().owner == kHost);
    CHECK(l.trip().seq_3066_after == l.trip().seq_2144);
    const oms::Decision d = l.decide(facts(), s);
    CHECK(d.emit);
    CHECK(d.why == oms::Why::None);
    CHECK(d.gen == 1);
    CHECK(l.emitting(kLogic));
    CHECK(!l.emitting(kRecv));
    // The call: its 2322 goes through take_frame on this thread, to the live session, pushed.
    l.on_out(oms::kOpMilestone, true, true, kLogic, 1'000'050, kActor2, s);
    l.on_called(d.gen, oms::CallResult::Returned, kSession2, 1'000'100, 40, kLogic, s);
    CHECK(!l.emitting(kLogic));
    CHECK(l.trip().ours_seen && l.trip().ours_pushed && l.trip().ours_count == 1);
    CHECK(l.counters().enqueued == 1);
    CHECK(oms::Ledger::outcome_of(l.trip()) == oms::Outcome::Emitted);
    CHECK(s.count(oms::kNOurs2322) == 1);
    // The load's exit: 2445 handed, 2762 out.
    l.on_in(oms::kOpLoadingCompleteReq, 15'000'000, s);
    l.on_out(oms::kOpLoadingCompleteAck, true, true, kLogic, 15'050'000, 0, s);
    CHECK(l.trip().n_2445 == 1 && l.trip().n_2762 == 1);
    const p8p::Event* e = s.last(oms::kNOut2762);
    CHECK(e && e->v[2] == 15'050'000 - 1'000'100 && e->v[3] == 50'000);
    // Follow-ups: none once 2762 is out.
    l.follow_ups(400'000'000, s);
    CHECK(s.count(oms::kNFollowUp) == 0);

    // The entry before the trip's 2144 (no 2144 yet): refused, final.
    oms::Ledger l2(oms::Mode::Send);
    Sink s2;
    l2.on_out(oms::kOpMoveFieldAck, true, true, kRecv, 100, kHost, s2);
    const oms::Decision d2 = l2.decide(facts(), s2);
    CHECK(!d2.emit && d2.why == oms::Why::No2144);
    l2.on_out(oms::kOpMoveFieldCompleteAck, true, true, kLogic, 300, 0, s2);
    l2.on_out(oms::kOpMoveFieldCompletePostAck, true, true, kLogic, 310, 0, s2);
    CHECK(l2.decide(facts(), s2).why == oms::Why::AlreadyDecided);  // no second chance in that trip

    // The 2144 on another thread (not this handler's): refused.
    oms::Ledger l3(oms::Mode::Send);
    Sink s3;
    l3.on_out(oms::kOpMoveFieldAck, true, true, kRecv, 100, kHost, s3);
    l3.on_out(oms::kOpMoveFieldCompleteAck, true, true, kRecv, 300, 0, s3);
    l3.on_out(oms::kOpMoveFieldCompletePostAck, true, true, kRecv, 310, 0, s3);
    CHECK(l3.decide(facts(), s3).why == oms::Why::OtherThread);

    // A 3066 that did not follow the 2144 on this thread (e.g. a bundle): refused.
    oms::Ledger l4(oms::Mode::Send);
    Sink s4;
    trip_until_entry(l4, s4, true, kRecv);
    CHECK(l4.trip().seq_3066_after == 0);
    CHECK(l4.decide(facts(), s4).why == oms::Why::No3066);

    // No 3066 at all.
    oms::Ledger l5(oms::Mode::Send);
    Sink s5;
    l5.on_out(oms::kOpMoveFieldAck, true, true, kRecv, 100, kHost, s5);
    l5.on_out(oms::kOpMoveFieldCompleteAck, true, true, kLogic, 300, 0, s5);
    CHECK(l5.decide(facts(), s5).why == oms::Why::No3066);

    // A second 2144 in the same trip (the client sent 2549 again): the LAST one binds.
    oms::Ledger l6(oms::Mode::Send);
    Sink s6;
    trip_until_entry(l6, s6);
    l6.on_out(oms::kOpMoveFieldCompleteAck, true, true, kLogic, 400, 0, s6);  // its 3066 is still to come
    CHECK(l6.decide(facts(), s6).why == oms::Why::No3066);
}

// --------------------------------------------------------------------------------------------- duplicates

void test_duplicates() {
    // A repeated entry / callback in one trip: one decision, one emission.
    oms::Ledger l(oms::Mode::Send);
    Sink s;
    trip_until_entry(l, s);
    CHECK(l.decide(facts(), s).emit);
    l.on_out(oms::kOpMilestone, true, true, kLogic, 1'000'050, kActor2, s);
    l.on_called(1, oms::CallResult::Returned, kSession2, 1'000'100, 40, kLogic, s);
    const oms::Decision again = l.decide(facts(), s);
    CHECK(!again.emit && again.why == oms::Why::AlreadyDecided);
    CHECK(l.counters().duplicates_avoided == 1);
    CHECK(l.counters().emits == 1);
    // A native 2322 after ours: counted as a duplicate milestone (cannot be prevented, must be visible).
    l.on_out(oms::kOpMilestone, true, true, kLogic, 2'000'000, kActor2, s);
    CHECK(l.counters().native_after_ours == 1);
    const p8p::Event* n = s.last(oms::kNNative2322);
    CHECK(n && n->v[4] == 1);

    // A native 2322 before the entry (e.g. the native path did send one): ours is not sent.
    oms::Ledger l2(oms::Mode::Send);
    Sink s2;
    trip_until_entry(l2, s2);
    l2.on_out(oms::kOpMilestone, true, true, kLogic, 320, kActor2, s2);
    const oms::Decision d2 = l2.decide(facts(), s2);
    CHECK(!d2.emit && d2.why == oms::Why::NativeAlready);
    CHECK(l2.counters().duplicates_avoided == 1);

    // User 2 owns the field (its own first field): the native path, never ours; the trip stays undecided.
    oms::Ledger l3(oms::Mode::Send);
    Sink s3;
    l3.on_out(oms::kOpMoveFieldAck, true, true, kRecv, 100, kUser2, s3);
    l3.on_out(oms::kOpMoveFieldCompleteAck, true, true, kLogic, 300, 0, s3);
    l3.on_out(oms::kOpMoveFieldCompletePostAck, true, true, kLogic, 310, 0, s3);
    oms::EntryFacts own = facts();
    own.field_owner = kUser2;
    const oms::Decision d3 = l3.decide(own, s3);
    CHECK(!d3.emit && d3.why == oms::Why::Owner && !d3.tracked);
    CHECK(!l3.trip().decided);
    l3.on_out(oms::kOpMilestone, true, true, kLogic, 320, kActor2, s3);  // the native one
    CHECK(l3.trip().native_2322 == 1);
    CHECK(l3.counters().native_after_ours == 0);

    // The host's own entries: not user 2, counted only, no line.
    oms::Ledger l4(oms::Mode::Send);
    Sink s4;
    trip_until_entry(l4, s4);
    oms::EntryFacts host = facts();
    host.user_key = kHost;
    host.actor_key = 0xA0100001;
    CHECK(l4.decide(host, s4).why == oms::Why::NotUser2);
    CHECK(s4.count(oms::kNDecision) == 0);
    CHECK(!l4.trip().decided);
    CHECK(l4.decide(facts(), s4).emit);  // user 2's entry still decides
}

// --------------------------------------------------------------------------------------------- invalidation

void test_invalidation() {
    struct Case {
        const char* name;
        void (*mutate)(oms::EntryFacts&);
        oms::Why want;
    };
    const Case cases[] = {
        {"reused address: another key", [](oms::EntryFacts& f) { f.actor_key = 0xA0100999; }, oms::Why::ActorMismatch},
        {"unknown character", [](oms::EntryFacts& f) { f.user2_actor_key = 0; }, oms::Why::ActorUnknown},
        {"not the main character", [](oms::EntryFacts& f) { f.main_character = 0x8888; }, oms::Why::NotMainCharacter},
        {"field mover elsewhere", [](oms::EntryFacts& f) { f.fm_field = 0x7777; }, oms::Why::FieldMismatch},
        {"field owner changed", [](oms::EntryFacts& f) { f.field_owner = 0x90100005; }, oms::Why::OwnerMismatch},
        {"host key unknown", [](oms::EntryFacts& f) { f.host_user_key = 0; }, oms::Why::OwnerMismatch},
        {"not the host's field", [](oms::EntryFacts& f) { f.host_field = 0x6666; }, oms::Why::NotHostField},
        {"host field unknown", [](oms::EntryFacts& f) { f.host_field = 0; }, oms::Why::NotHostField},
        {"not server realm", [](oms::EntryFacts& f) { f.realm = false; }, oms::Why::NotRealm},
        {"no send context", [](oms::EntryFacts& f) { f.send_ctx = 0; }, oms::Why::NoSendContext},
        {"scope open", [](oms::EntryFacts& f) { f.scope_flag = 1; f.scope_count = 2; }, oms::Why::ScopeOpen},
        {"inside take_frame", [](oms::EntryFacts& f) { f.in_take_frame = true; }, oms::Why::InTakeFrame},
        {"logout asked", [](oms::EntryFacts& f) { f.logout_asked = true; }, oms::Why::LogoutAsked},
        {"wrong recipient (session)", [](oms::EntryFacts& f) { f.user_session = kSession2b; }, oms::Why::SessionMismatch},
        {"no live session", [](oms::EntryFacts& f) { f.live_session = 0; }, oms::Why::SessionMismatch},
    };
    for (const Case& c : cases) {
        oms::Ledger l(oms::Mode::Send);
        Sink s;
        trip_until_entry(l, s);
        oms::EntryFacts f = facts();
        c.mutate(f);
        const oms::Decision d = l.decide(f, s);
        if (d.why != c.want || d.emit) std::printf("  case '%s': why %d\n", c.name, static_cast<int>(d.why));
        CHECK(d.why == c.want && !d.emit && d.tracked);
        CHECK(!l.emitting(kLogic));
        // Final: the same trip never emits afterwards, even with clean facts.
        CHECK(l.decide(facts(), s).why == oms::Why::AlreadyDecided);
    }
    // Scope mode on but no scope sender open: not a refusal.
    {
        oms::Ledger l(oms::Mode::Send);
        Sink s;
        trip_until_entry(l, s);
        oms::EntryFacts f = facts();
        f.scope_flag = 1;
        f.scope_count = 0;
        CHECK(l.decide(f, s).emit);
    }
    // Unreadable frame: not tracked, not emitted.
    {
        oms::Ledger l(oms::Mode::Send);
        Sink s;
        trip_until_entry(l, s);
        oms::EntryFacts f = facts();
        f.reads_ok = false;
        const oms::Decision d = l.decide(f, s);
        CHECK(!d.emit && d.why == oms::Why::Unreadable && !d.tracked);
    }
    // Leaving and a new 2174 before the entry: the old trip ends undecided, the new one needs ITS 2144.
    {
        oms::Ledger l(oms::Mode::Send);
        Sink s;
        trip_until_entry(l, s);
        l.on_out(oms::kOpMoveFieldAck, true, true, kRecv, 500, kHost, s);
        CHECK(l.trip().gen == 2);
        const p8p::Event* end = s.last(oms::kNTripEnd);
        CHECK(end && end->v[0] == 1 && static_cast<oms::Outcome>(end->v[2]) == oms::Outcome::NoEntry);
        CHECK(l.decide(facts(), s).why == oms::Why::No2144);
    }
    // Logout asked before the entry: the trip is cancelled and stays without 2322.
    {
        oms::Ledger l(oms::Mode::Send);
        Sink s;
        trip_until_entry(l, s);
        l.on_logout(400, s);
        CHECK(l.trip().cancelled && l.trip().decided);
        // e.g. the logout sequence's own 2549 -> 2144 -> entry (salida-en-carga.md 4): still no 2322
        CHECK(l.decide(facts(), s).why == oms::Why::LogoutAsked);
        CHECK(l.decide(facts(), s).why == oms::Why::LogoutAsked);
        CHECK(l.counters().emits == 0);
        CHECK(l.counters().cancelled == 1);
    }
    // The 2174's owner layout unknown: refused.
    {
        oms::Ledger l(oms::Mode::Send);
        Sink s;
        l.on_out(oms::kOpMoveFieldAck, true, true, kRecv, 100, 0, s);
        l.on_out(oms::kOpMoveFieldCompleteAck, true, true, kLogic, 300, 0, s);
        l.on_out(oms::kOpMoveFieldCompletePostAck, true, true, kLogic, 310, 0, s);
        CHECK(l.decide(facts(), s).why == oms::Why::OwnerUnknown);
    }
}

// --------------------------------------------------------------------------------------------- reconnection

void test_reconnection() {
    oms::Ledger l(oms::Mode::Send);
    Sink s;
    trip_until_entry(l, s);
    // A new guest epoch before the entry: the trip is cancelled.
    l.on_epoch(2, 500, s);
    CHECK(l.epoch() == 2);
    CHECK(l.trip().gen == 0);
    CHECK(l.counters().cancelled == 1);
    const p8p::Event* ep = s.last(oms::kNEpoch);
    CHECK(ep && ep->v[0] == 2 && ep->v[1] == 1 && ep->v[2] == 0);
    // An entry of the old epoch's user (or before the new 2174): no trip.
    CHECK(l.decide(facts(1), s).why == oms::Why::NoTrip);
    CHECK(l.decide(facts(2, kSession2b), s).why == oms::Why::NoTrip);
    // What the OLD session still writes (not the live one) never builds a trip.
    l.on_out(oms::kOpMoveFieldAck, false, true, kRecv, 600, kHost, s);
    CHECK(l.trip().gen == 0);
    // The new epoch's own trip.
    l.on_out(oms::kOpMoveFieldAck, true, true, kRecv, 700, kHost, s);
    l.on_out(oms::kOpMoveFieldCompleteAck, true, true, kLogic, 800, 0, s);
    l.on_out(oms::kOpMoveFieldCompletePostAck, true, true, kLogic, 810, 0, s);
    CHECK(l.trip().epoch == 2 && l.trip().gen == 2);
    // The old session as the traveller's: refused (the live one is the new session).
    oms::EntryFacts stale = facts(2, kSession2b);
    stale.user_session = kSession2;
    CHECK(l.decide(stale, s).why == oms::Why::SessionMismatch);
    // Its next trip, with the new session: emitted.
    l.on_out(oms::kOpMoveFieldAck, true, true, kRecv, 900, kHost, s);
    l.on_out(oms::kOpMoveFieldCompleteAck, true, true, kLogic, 1000, 0, s);
    l.on_out(oms::kOpMoveFieldCompletePostAck, true, true, kLogic, 1010, 0, s);
    CHECK(l.decide(facts(2, kSession2b), s).emit);
}

// --------------------------------------------------------------------------------------------- queue failure

void test_queue_failure() {
    // The trip's 2144 lost in the outgoing queue: no 2322.
    {
        oms::Ledger l(oms::Mode::Send);
        Sink s;
        trip_until_entry(l, s, false);
        CHECK(l.trip().poisoned);
        CHECK(l.decide(facts(), s).why == oms::Why::TripPoisoned);
    }
    // The 2174 lost.
    {
        oms::Ledger l(oms::Mode::Send);
        Sink s;
        l.on_out(oms::kOpMoveFieldAck, true, false, kRecv, 100, kHost, s);
        l.on_out(oms::kOpMoveFieldCompleteAck, true, true, kLogic, 300, 0, s);
        l.on_out(oms::kOpMoveFieldCompletePostAck, true, true, kLogic, 310, 0, s);
        CHECK(l.decide(facts(), s).why == oms::Why::TripPoisoned);
    }
    // The 3066 lost.
    {
        oms::Ledger l(oms::Mode::Send);
        Sink s;
        l.on_out(oms::kOpMoveFieldAck, true, true, kRecv, 100, kHost, s);
        l.on_out(oms::kOpMoveFieldCompleteAck, true, true, kLogic, 300, 0, s);
        l.on_out(oms::kOpMoveFieldCompletePostAck, true, false, kLogic, 310, 0, s);
        CHECK(l.decide(facts(), s).why == oms::Why::TripPoisoned);
    }
    // OUR 2322 lost in the queue: recorded as lost, never retried (the trip is decided).
    {
        oms::Ledger l(oms::Mode::Send);
        Sink s;
        trip_until_entry(l, s);
        CHECK(l.decide(facts(), s).emit);
        l.on_out(oms::kOpMilestone, true, false, kLogic, 1'000'050, kActor2, s);
        l.on_called(1, oms::CallResult::Returned, kSession2, 1'000'100, 40, kLogic, s);
        CHECK(oms::Ledger::outcome_of(l.trip()) == oms::Outcome::QueueLost);
        CHECK(l.counters().queue_lost == 1);
        CHECK(l.decide(facts(), s).why == oms::Why::AlreadyDecided);
        l.follow_ups(400'000'000, s);
        CHECK(s.count(oms::kNFollowUp) == 0);  // no 2322 queued: nothing to follow
    }
    // Called, but nothing reached take_frame (no recipient): "not seen", not retried.
    {
        oms::Ledger l(oms::Mode::Send);
        Sink s;
        trip_until_entry(l, s);
        CHECK(l.decide(facts(), s).emit);
        l.on_called(1, oms::CallResult::Returned, kSession2, 1'000'100, 40, kLogic, s);
        CHECK(oms::Ledger::outcome_of(l.trip()) == oms::Outcome::CalledNotSeen);
        CHECK(l.counters().not_seen == 1);
    }
    // Emitted and queued but no 2445/2762: report-only follow-ups at +10/+60/+300 s, each once.
    {
        oms::Ledger l(oms::Mode::Send);
        Sink s;
        trip_until_entry(l, s);
        CHECK(l.decide(facts(), s).emit);
        l.on_out(oms::kOpMilestone, true, true, kLogic, 1'000'050, kActor2, s);
        l.on_called(1, oms::CallResult::Returned, kSession2, 1'000'000, 40, kLogic, s);
        l.follow_ups(5'000'000, s);
        CHECK(s.count(oms::kNFollowUp) == 0);
        l.follow_ups(11'000'000, s);
        l.follow_ups(12'000'000, s);
        CHECK(s.count(oms::kNFollowUp) == 1);
        l.follow_ups(400'000'000, s);
        CHECK(s.count(oms::kNFollowUp) == 3);
        CHECK(l.counters().emits == 1);  // reports only: nothing emitted again
    }
}

// --------------------------------------------------------------------------------------------- wrong recipient

void test_wrong_recipient() {
    // The ToTarget the constructor built names another session: not called, the trip stays decided.
    {
        oms::Ledger l(oms::Mode::Send);
        Sink s;
        trip_until_entry(l, s);
        CHECK(l.decide(facts(), s).emit);
        l.on_called(1, oms::CallResult::WrongRecipient, kSession2b, 1'000'100, 5, kLogic, s);
        CHECK(oms::Ledger::outcome_of(l.trip()) == oms::Outcome::CallFailed);
        CHECK(!l.faulted());
        CHECK(l.decide(facts(), s).why == oms::Why::AlreadyDecided);
    }
    // Our 2322 seen for a session that is not the live one: flagged, not counted as queued.
    {
        oms::Ledger l(oms::Mode::Send);
        Sink s;
        trip_until_entry(l, s);
        CHECK(l.decide(facts(), s).emit);
        l.on_out(oms::kOpMilestone, false, true, kLogic, 1'000'050, kActor2, s);
        l.on_called(1, oms::CallResult::Returned, kSession2, 1'000'100, 40, kLogic, s);
        const p8p::Event* c = s.last(oms::kNCalled);
        CHECK(c && c->v[2] == 1 && c->v[3] == 0 && c->v[7] == 1);
        CHECK(oms::Ledger::outcome_of(l.trip()) == oms::Outcome::QueueLost);
    }
    // A 2322 on ANOTHER thread during our call is not ours (it is native).
    {
        oms::Ledger l(oms::Mode::Send);
        Sink s;
        trip_until_entry(l, s);
        CHECK(l.decide(facts(), s).emit);
        l.on_out(oms::kOpMilestone, true, true, kRecv, 1'000'040, kActor2, s);
        CHECK(l.trip().native_2322 == 1);
        l.on_called(1, oms::CallResult::Returned, kSession2, 1'000'100, 40, kLogic, s);
        CHECK(oms::Ledger::outcome_of(l.trip()) == oms::Outcome::CalledNotSeen);
    }
    // A fault: emission off for the run (later trips refused with Faulted).
    {
        oms::Ledger l(oms::Mode::Send);
        Sink s;
        trip_until_entry(l, s);
        CHECK(l.decide(facts(), s).emit);
        l.on_called(1, oms::CallResult::FaultEmit, kSession2, 1'000'100, 40, kLogic, s);
        CHECK(l.faulted());
        trip_until_entry(l, s);
        CHECK(l.decide(facts(), s).why == oms::Why::Faulted);
    }
}

// --------------------------------------------------------------------------------------------- dry run

void test_dry_run() {
    oms::Ledger l(oms::Mode::DryRun);
    Sink s;
    trip_until_entry(l, s);
    const oms::Decision d = l.decide(facts(), s);
    CHECK(d.emit && d.why == oms::Why::None);
    CHECK(!l.emitting(kLogic));  // nothing is called: no emission marker
    CHECK(l.counters().dry_emits == 1 && l.counters().emits == 0);
    const p8p::Event* n = s.last(oms::kNDecision);
    CHECK(n && n->v[2] == 1 && ((n->v[11] >> 2) & 1) == 1);
    CHECK(oms::Ledger::outcome_of(l.trip()) == oms::Outcome::DryWouldEmit);
    CHECK(l.decide(facts(), s).why == oms::Why::AlreadyDecided);
    // The next 2174 ends it with that outcome.
    l.on_out(oms::kOpMoveFieldAck, true, true, kRecv, 2'000'000, kHost, s);
    const p8p::Event* end = s.last(oms::kNTripEnd);
    CHECK(end && static_cast<oms::Outcome>(end->v[2]) == oms::Outcome::DryWouldEmit);
    // 2042 bundles to session 2 are counted (their contents are not parsed).
    l.on_out(oms::kOpScopeSenderAck, true, true, kLogic, 2'000'100, 0, s);
    CHECK(l.counters().bundles == 1);
    // An opcode the ledger does not follow is ignored.
    l.on_out(2005, true, true, kLogic, 2'000'200, 0, s);
    CHECK(l.trip().gen == 2);
}

// --------------------------------------------------------------------------------------------- the guest's trips

p8p::Event ev(uint16_t kind, uint64_t t_us, uint64_t v0, uint64_t v1 = 0) {
    p8p::Event e;
    e.kind = kind;
    e.qpc_us = t_us;
    e.tid = 1860;
    e.v[0] = v0;
    e.v[1] = v1;
    e.v[2] = 0x10;
    return e;
}

uint64_t us(double s) { return static_cast<uint64_t>(s * 1e6 + 0.5); }

bool has(const std::vector<std::string>& lines, const char* needle) {
    for (const auto& l : lines) {
        if (l.find(needle) != std::string::npos) return true;
    }
    return false;
}

void test_guest_trips() {
    ltr::Tracker t([](uint64_t q) { return std::to_string(q); });
    std::vector<std::string> lines;
    // Trip 1 of session 5 (its own field): 2174 #539 15.602835, 2549 28.345295, 2144 #797, 3066 #798, 2322 #802,
    // 2445 #1143 42.565281, 2762 #1144 42.617567; 0x11 left in 0 s (GL L:137, the local baseline).
    t.on_event(ev(ltr::kIn, us(15.602835), ltr::kOpMoveFieldAck, kUser2), lines);
    t.on_event(ev(ltr::kOut, us(28.345295), ltr::kOpMoveFieldCompleteReq), lines);
    t.on_event(ev(ltr::kIn, us(28.843599), ltr::kOpMoveFieldCompleteAck), lines);
    t.on_event(ev(ltr::kIn, us(28.843604), ltr::kOpPostAck), lines);
    t.on_event(ev(ltr::kIn, us(28.843617), ltr::kOpMilestone, kActor2), lines);
    t.on_event(ev(ltr::kDelivered, us(28.850000), ltr::kOpMilestone), lines);
    t.on_event(ev(ltr::kOut, us(42.565281), ltr::kOpLoadingCompleteReq), lines);
    t.on_event(ev(ltr::kIn, us(42.617567), ltr::kOpLoadingCompleteAck), lines);
    // The states arrive late (the 5 s drain): 0x10 -> 0x11 -> 0x12 -> 0x13 inside trip 1.
    t.on_event(ev(ltr::kState, us(35.0), 0x10, 0x11), lines);
    t.on_event(ev(ltr::kState, us(35.0001), 0x11, 0x12), lines);
    t.on_event(ev(ltr::kState, us(35.01), 0x12, 0x13), lines);
    CHECK(has(lines, "GuestLoad [in 2322]"));
    CHECK(has(lines, "corr=2322:A0100334:0x0:0x0"));
    CHECK(t.trips().size() == 1);
    CHECK(t.trips().back().n2322 == 1 && t.trips().back().out11_to == 0x12);
    lines.clear();
    t.tick(us(42.617567) + ltr::kSettleUs - 1, lines);
    CHECK(lines.empty());  // not settled yet
    t.tick(us(42.617567) + ltr::kSettleUs, lines);
    CHECK(lines.size() == 1);
    CHECK(has(lines, "VERDICT: one 2322, after its 2144 | 0x11 -> 0x12"));
    CHECK(has(lines, "normal load completion"));

    // Trip 2 (the host's field, session 5): 2174 #1190 46.732033, 2549 #1451 55.259972, 2144 #9114 67.556077,
    // 3066 #9132 67.571811, NO 2322; 0x11 at 74.054, 0x11 -> 0x18 after 250.306 s.
    lines.clear();
    t.on_event(ev(ltr::kIn, us(46.732033), ltr::kOpMoveFieldAck, kUser2), lines);
    t.on_event(ev(ltr::kOut, us(55.259972), ltr::kOpMoveFieldCompleteReq), lines);
    t.on_event(ev(ltr::kIn, us(67.556077), ltr::kOpMoveFieldCompleteAck), lines);
    t.on_event(ev(ltr::kIn, us(67.571811), ltr::kOpPostAck), lines);
    t.on_event(ev(ltr::kState, us(74.054), 0x10, 0x11), lines);
    CHECK(t.trips().back().t_in11 == us(74.054));
    t.on_event(ev(ltr::kState, us(74.054 + 250.306), 0x11, 0x18), lines);
    CHECK(t.trips().back().out11_to == 0x18);
    // No 2322: no "+N s after the 2322" reports; the summary comes with the next trip / connection.
    lines.clear();
    t.tick(us(400.0), lines);
    CHECK(lines.empty());
    t.on_event(ev(ltr::kIn, us(401.0), ltr::kOpMoveFieldAck, kUser2), lines);
    lines.clear();
    t.tick(us(401.0) + ltr::kSettleUs, lines);
    CHECK(has(lines, "VERDICT: NO 2322 in this trip"));
    CHECK(has(lines, "0x11 -> 0x18: NOT the normal exit"));

    // Trip 3: a 2322 that came BEFORE its 2144, then a duplicate, then stuck (reports at +10/+60/+300 s).
    lines.clear();
    t.on_event(ev(ltr::kIn, us(402.0), ltr::kOpMilestone, kActor2), lines);
    CHECK(has(lines, "BEFORE any 2144 of this trip"));
    t.on_event(ev(ltr::kIn, us(403.0), ltr::kOpMoveFieldCompleteAck), lines);
    t.on_event(ev(ltr::kIn, us(403.1), ltr::kOpMilestone, kActor2), lines);
    CHECK(has(lines, "DUPLICATE in this trip"));
    lines.clear();
    t.tick(us(402.0 + 61), lines);
    CHECK(lines.size() == 2);  // +10 and +60
    CHECK(has(lines, "VERDICT: DUPLICATE: 2 x 2322 in one trip"));
    // States not fed at all: the summary says so.
    ltr::Tracker t2([](uint64_t q) { return std::to_string(q); });
    std::vector<std::string> l2;
    t2.on_event(ev(ltr::kIn, us(1.0), ltr::kOpMoveFieldAck, kUser2), l2);
    t2.on_event(ev(ltr::kIn, us(2.0), ltr::kOpMoveFieldCompleteAck), l2);
    t2.on_event(ev(ltr::kIn, us(2.1), ltr::kOpMilestone, kActor2), l2);
    t2.on_event(ev(ltr::kIn, us(3.0), ltr::kOpLoadingCompleteAck), l2);
    l2.clear();
    t2.tick(us(3.0) + ltr::kSettleUs, l2);
    CHECK(has(l2, "loading states not observed"));
}

// --------------------------------------------------------------------------------------------- P8 S16 reload
//
// observer_milestone_reload_logic.h. The sequence of session 15 (host capture cdmp_packets_20261011-113242,
// guest 103850): F3 accepted user 2's 2283, its 2437 entered, 2643 + 3066 to user 2 (no 2174), its 2873
// 2.309 s after the 2437 (0 bytes), no 2322 to user 2.

constexpr uint64_t kT2873 = 10'000'000;

void reload_until_2873(omr::Ledger& l, Sink& s) {
    l.on_f3_accepted(1'000'000, s);
    l.on_2437(7'690'000, s);
    l.on_out(omr::kOpLoadingFieldDataAck, true, true, kLogic, 7'700'000, 0, false, s);
    l.on_out(omr::kOpMoveFieldCompletePostAck, true, true, kLogic, 7'701'000, 0, false, s);
}

omr::OfferFacts offer(omr::Mode m, ftl::Where w = ftl::Where::HostField, bool ready = true) {
    omr::OfferFacts f;
    f.mode = m;
    f.where = w;
    f.emitter_ready = ready;
    return f;
}

oms::EntryFacts exit_facts(uint64_t t_us) {
    oms::EntryFacts f = facts();
    f.t_us = t_us;
    return f;
}

void test_reload_modes() {
    CHECK(omr::parse_mode("0").mode == omr::Mode::Off && omr::parse_mode("0").known);
    CHECK(omr::parse_mode("").mode == omr::Mode::Off);
    CHECK(omr::parse_mode("probe").mode == omr::Mode::Probe);
    CHECK(omr::parse_mode(" 1 ").mode == omr::Mode::On);
    CHECK(!omr::parse_mode("yes").known && omr::parse_mode("yes").mode == omr::Mode::Off);
    // Off: nothing is relevant, nothing armed.
    omr::Ledger l(omr::Mode::Off);
    Sink s;
    reload_until_2873(l, s);
    CHECK(l.judge(offer(omr::Mode::Off)) == omr::Gate::NotRelevant);
    CHECK(omr::followed_out(2643) && omr::followed_out(2322) && omr::followed_out(2174) && !omr::followed_out(2144));
}

void test_reload_emit_once() {
    omr::Ledger l(omr::Mode::On);
    Sink s;
    reload_until_2873(l, s);
    CHECK(l.trip().phase == omr::Phase::Reload);
    CHECK(l.trip().n2643 == 1 && l.trip().n3066 == 1);
    const omr::Gate g = l.judge(offer(omr::Mode::On));
    CHECK(g == omr::Gate::Drop && omr::drops(g));
    l.record_offer(g, false, ftl::Where::HostField, kT2873, s);
    CHECK(l.waiting());
    CHECK(l.counters().dropped == 1);
    // Too early (< 300 ms after the drop): nothing, not even a refusal.
    omr::ExitDecision d = l.at_exit(exit_facts(kT2873 + 100'000), false, s);
    CHECK(!d.act && l.waiting());
    // The next Req of user 2 after 300 ms: emit, once.
    d = l.at_exit(exit_facts(kT2873 + 350'000), false, s);
    CHECK(d.act && d.emit && !d.dry && d.why == omr::Why::None);
    CHECK(l.emitting(kLogic));
    // The emitter's own 2322 goes through take_frame on this thread.
    l.on_out(omr::kOpMilestone, true, true, kLogic, kT2873 + 351'000, kActor2, true, s);
    l.on_called(d.gen, oms::CallResult::Returned, kSession2, kT2873 + 352'000, 900, kLogic, s);
    CHECK(!l.emitting(kLogic));
    CHECK(l.trip().ours_seen && l.trip().ours_pushed && l.trip().ours_count == 1);
    CHECK(l.trip().native_2322 == 0);  // ours is not counted as native
    CHECK(omr::Ledger::outcome_of(l.trip()) == omr::Outcome::Emitted);
    CHECK(l.counters().enqueued == 1);
    // Exactly one per trip: no more decisions. Review 1: once decided, a later 2873 PASSES natively (never swallowed
    // without a 2322) and does not re-arm.
    CHECK(!l.waiting());
    CHECK(!l.at_exit(exit_facts(kT2873 + 900'000), false, s).act);
    const omr::Gate g2 = l.judge(offer(omr::Mode::On));
    CHECK(g2 == omr::Gate::Over && !omr::drops(g2));
    l.record_offer(g2, true, ftl::Where::HostField, kT2873 + 1'000'000, s);
    CHECK(!l.waiting());
    CHECK(l.counters().emits == 1 && l.counters().decisions == 1);
    // The load ends normally: the trip is over (Loaded).
    l.on_out(omr::kOpLoadingCompleteAck, true, true, kRecv, kT2873 + 5'000'000, 0, false, s);
    CHECK(l.trip().t_2762 != 0 && l.trip().phase == omr::Phase::Loaded);
    CHECK(s.count(omr::kNLoaded) == 1);
    CHECK(l.judge(offer(omr::Mode::On)) == omr::Gate::Over);
    // A new trip (next F3 acceptance) ends this one with its line.
    l.on_f3_accepted(kT2873 + 60'000'000, s);
    const p8p::Event* end = s.last(omr::kNTripEnd);
    CHECK(end && end->v[1] == static_cast<uint64_t>(omr::Outcome::Emitted) && end->v[3] == 2 && end->v[4] == 1);
}

// Review 1: a trip drops only one window of 2873s. Loaded (2762) before any 2873, or a 2873 older than kMaxAgeUs
// after its 2437 (a respawn / another path without a fresh F3 2283 and without a 2174): passes natively.
void test_reload_trip_over() {
    Sink s;
    {
        omr::Ledger l(omr::Mode::On);
        reload_until_2873(l, s);
        l.on_out(omr::kOpLoadingCompleteAck, true, true, kRecv, 9'000'000, 0, false, s);
        const omr::Gate g = l.judge(offer(omr::Mode::On));
        CHECK(g == omr::Gate::Over);
        l.record_offer(g, true, ftl::Where::HostField, kT2873, s);
        CHECK(!l.waiting());
        CHECK(omr::Ledger::outcome_of(l.trip()) == omr::Outcome::Passed);
    }
    {
        omr::Ledger l(omr::Mode::On);
        reload_until_2873(l, s);  // its 2437 at 7.69 s
        omr::OfferFacts f = offer(omr::Mode::On);
        f.t_us = 7'690'000 + omr::kMaxAgeUs - 1;
        CHECK(l.judge(f) == omr::Gate::Drop);
        f.t_us = 7'690'000 + omr::kMaxAgeUs + 1;
        CHECK(l.judge(f) == omr::Gate::Over);
    }
}

void test_reload_not_ours() {
    Sink s;
    {  // no F3 acceptance: a 2873 (e.g. of an entry trip) passes
        omr::Ledger l(omr::Mode::On);
        CHECK(l.judge(offer(omr::Mode::On)) == omr::Gate::NoTrip);
    }
    {  // a 2174 after the 2437: a field change, observer_milestone's (never ours)
        omr::Ledger l(omr::Mode::On);
        l.on_f3_accepted(1, s);
        l.on_2437(2, s);
        l.on_out(omr::kOpMoveFieldAck, true, true, kRecv, 3, 0, false, s);
        CHECK(l.trip().phase == omr::Phase::FieldChange);
        // a 2643 after it does not turn it into a reload
        l.on_out(omr::kOpLoadingFieldDataAck, true, true, kRecv, 4, 0, false, s);
        CHECK(l.trip().phase == omr::Phase::FieldChange);
        const omr::Gate g = l.judge(offer(omr::Mode::On));
        CHECK(g == omr::Gate::NotReload && !omr::drops(g));
        l.record_offer(g, true, ftl::Where::HostField, 5, s);
        CHECK(!l.waiting());
    }
    {  // the 2437 not answered yet: not a reload
        omr::Ledger l(omr::Mode::On);
        l.on_f3_accepted(1, s);
        l.on_2437(2, s);
        CHECK(l.judge(offer(omr::Mode::On)) == omr::Gate::NotReload);
    }
    {  // a 2643 to ANOTHER session (not the live session 2) is not this trip's
        omr::Ledger l(omr::Mode::On);
        l.on_f3_accepted(1, s);
        l.on_2437(2, s);
        l.on_out(omr::kOpLoadingFieldDataAck, false, true, kRecv, 3, 0, false, s);
        CHECK(l.trip().phase == omr::Phase::Asked);
    }
    {  // a 2643 without a 2437 of the trip
        omr::Ledger l(omr::Mode::On);
        l.on_f3_accepted(1, s);
        l.on_out(omr::kOpLoadingFieldDataAck, true, true, kRecv, 3, 0, false, s);
        CHECK(l.trip().phase == omr::Phase::Prepared);
    }
    {  // refused: 2679
        omr::Ledger l(omr::Mode::On);
        l.on_f3_accepted(1, s);
        l.on_2437(2, s);
        l.on_out(omr::kOpLogoutCharacterAck, true, true, kRecv, 3, 0, false, s);
        CHECK(l.trip().phase == omr::Phase::Refused);
        CHECK(l.judge(offer(omr::Mode::On)) == omr::Gate::NotReload);
    }
    {  // user 2 in a field of its own / unknown / another user's: passes
        omr::Ledger l(omr::Mode::On);
        reload_until_2873(l, s);
        CHECK(l.judge(offer(omr::Mode::On, ftl::Where::OwnField)) == omr::Gate::OwnField);
        CHECK(l.judge(offer(omr::Mode::On, ftl::Where::Unknown)) == omr::Gate::NotShared);
        CHECK(l.judge(offer(omr::Mode::On, ftl::Where::OtherField)) == omr::Gate::NotShared);
        // mode 1 without the verified SEND emitter: never dropped
        const omr::Gate g = l.judge(offer(omr::Mode::On, ftl::Where::HostField, false));
        CHECK(g == omr::Gate::EmitterOff && !omr::drops(g));
        l.record_offer(g, true, ftl::Where::HostField, kT2873, s);
        CHECK(!l.waiting());
        CHECK(l.counters().passed == 1 && l.counters().dropped == 0);
    }
}

void test_reload_probe() {
    omr::Ledger l(omr::Mode::Probe);
    Sink s;
    reload_until_2873(l, s);
    const omr::Gate g = l.judge(offer(omr::Mode::Probe));
    CHECK(g == omr::Gate::WouldDrop && !omr::drops(g));
    // judged but held: no state change until it is recorded
    CHECK(!l.waiting());
    l.record_offer(g, true, ftl::Where::HostField, kT2873, s);  // it went to the server
    CHECK(l.waiting() && l.trip().dry);
    const omr::ExitDecision d = l.at_exit(exit_facts(kT2873 + 400'000), false, s);
    CHECK(d.act && d.emit && d.dry);
    CHECK(!l.emitting(kLogic));  // nothing is called in probe
    CHECK(l.counters().dry_emits == 1 && l.counters().emits == 0);
    CHECK(omr::Ledger::outcome_of(l.trip()) == omr::Outcome::DryWouldEmit);
    CHECK(l.counters().would_drop == 1 && l.counters().dropped == 0);
}

void test_reload_gates() {
    Sink s;
    // transient: an open scope sender, retried at the next Req; then emitted
    {
        omr::Ledger l(omr::Mode::On);
        reload_until_2873(l, s);
        l.record_offer(omr::Gate::Drop, false, ftl::Where::HostField, kT2873, s);
        oms::EntryFacts f = exit_facts(kT2873 + 400'000);
        f.scope_flag = 1;
        f.scope_count = 1;
        CHECK(!l.at_exit(f, false, s).act);
        CHECK(l.waiting() && l.trip().attempts == 1);
        // Review 3: a burst of Reqs within 50 ms counts once (hundreds of 3067 while loading).
        for (int i = 0; i < 500; ++i) CHECK(!l.at_exit(f, false, s).act);
        CHECK(l.trip().attempts == 1);
        f.realm = false;
        f.scope_flag = 0;
        f.t_us += omr::kAttemptGapUs;
        CHECK(!l.at_exit(f, false, s).act);
        CHECK(l.trip().attempts == 2);
        const omr::ExitDecision d = l.at_exit(exit_facts(kT2873 + 500'000), false, s);
        CHECK(d.act && d.emit);
    }
    // transient forever, one Req every 10 ms: the 15 s timeout governs, not the attempt budget
    {
        omr::Ledger l(omr::Mode::On);
        reload_until_2873(l, s);
        l.record_offer(omr::Gate::Drop, false, ftl::Where::HostField, kT2873, s);
        oms::EntryFacts f = exit_facts(kT2873 + 400'000);
        f.in_take_frame = true;
        omr::ExitDecision d;
        for (uint32_t i = 0; i < 3000 && !d.act; ++i, f.t_us += 10'000) d = l.at_exit(f, false, s);
        CHECK(d.act && !d.emit && d.why == omr::Why::Timeout);
        CHECK(l.trip().attempts < omr::kMaxAttempts && l.trip().attempts >= 280);
        CHECK(!l.waiting());
    }
    // Review 2: the trip changes between the drop and the next Req's exit (>= 300 ms): never a 2322 then
    for (const uint16_t op : {omr::kOpMoveFieldAck, omr::kOpLogoutCharacterAck, omr::kOpLoadingCompleteAck}) {
        omr::Ledger l(omr::Mode::On);
        reload_until_2873(l, s);
        l.record_offer(omr::Gate::Drop, false, ftl::Where::HostField, kT2873, s);
        l.on_out(op, true, true, kRecv, kT2873 + 100'000, 0, false, s);
        CHECK(l.trip().phase != omr::Phase::Reload);
        const omr::ExitDecision d = l.at_exit(exit_facts(kT2873 + 400'000), false, s);
        CHECK(d.act && !d.emit && d.why == omr::Why::TripChanged);
        CHECK(!l.emitting(kLogic) && !l.waiting());
    }
    // final refusals
    struct Case {
        omr::Why why;
        void (*mutate)(oms::EntryFacts&);
    };
    const Case cases[] = {
        {omr::Why::ActorMismatch, [](oms::EntryFacts& f) { f.actor_key = 0xA0100001; }},
        {omr::Why::ActorUnknown, [](oms::EntryFacts& f) { f.user2_actor_key = 0; }},
        {omr::Why::NotMainCharacter, [](oms::EntryFacts& f) { f.main_character = 0x1234; }},
        {omr::Why::SessionMismatch, [](oms::EntryFacts& f) { f.user_session = kSession2b; }},
        {omr::Why::OwnFieldNow, [](oms::EntryFacts& f) { f.field_owner = kUser2; }},
        {omr::Why::OwnerNotHost, [](oms::EntryFacts& f) { f.field_owner = 0x90100999; }},
        {omr::Why::NotHostField, [](oms::EntryFacts& f) { f.host_field = 0x7777; }},
        {omr::Why::LogoutAsked, [](oms::EntryFacts& f) { f.logout_asked = true; }},
        {omr::Why::EpochChanged, [](oms::EntryFacts& f) { f.epoch = 2; }},
    };
    for (const Case& c : cases) {
        omr::Ledger l(omr::Mode::On);
        reload_until_2873(l, s);
        l.record_offer(omr::Gate::Drop, false, ftl::Where::HostField, kT2873, s);
        oms::EntryFacts f = exit_facts(kT2873 + 400'000);
        c.mutate(f);
        const omr::ExitDecision d = l.at_exit(f, false, s);
        CHECK(d.act && !d.emit && d.why == c.why);
        CHECK(!l.waiting());
        CHECK(omr::Ledger::outcome_of(l.trip()) == omr::Outcome::Refused);
    }
    // a native 2322 for user 2's character after its 2437: never a second one
    {
        omr::Ledger l(omr::Mode::On);
        reload_until_2873(l, s);
        l.on_out(omr::kOpMilestone, true, true, kRecv, 8'000'000, kActor2, true, s);
        CHECK(l.trip().native_2322 == 1);
        l.record_offer(omr::Gate::Drop, false, ftl::Where::HostField, kT2873, s);
        const omr::ExitDecision d = l.at_exit(exit_facts(kT2873 + 400'000), false, s);
        CHECK(d.act && !d.emit && d.why == omr::Why::NativeAlready);
    }
    // a 2322 for ANOTHER character (the host's) is not user 2's
    {
        omr::Ledger l(omr::Mode::On);
        reload_until_2873(l, s);
        l.on_out(omr::kOpMilestone, true, true, kRecv, 8'000'000, 0xA0100001, false, s);
        CHECK(l.trip().native_2322 == 0);
    }
    // the emitter faulted earlier in the run
    {
        omr::Ledger l(omr::Mode::On);
        reload_until_2873(l, s);
        l.record_offer(omr::Gate::Drop, false, ftl::Where::HostField, kT2873, s);
        const omr::ExitDecision d = l.at_exit(exit_facts(kT2873 + 400'000), true, s);
        CHECK(d.act && !d.emit && d.why == omr::Why::Faulted);
    }
    // no fitting Req within 15 s
    {
        omr::Ledger l(omr::Mode::On);
        reload_until_2873(l, s);
        l.record_offer(omr::Gate::Drop, false, ftl::Where::HostField, kT2873, s);
        const omr::ExitDecision d = l.at_exit(exit_facts(kT2873 + omr::kTimeoutUs + 1), false, s);
        CHECK(d.act && !d.emit && d.why == omr::Why::Timeout);
    }
}

void test_reload_cancel_and_failures() {
    Sink s;
    {  // a new epoch before the decision: cancelled, nothing carries over
        omr::Ledger l(omr::Mode::On);
        reload_until_2873(l, s);
        l.record_offer(omr::Gate::Drop, false, ftl::Where::HostField, kT2873, s);
        l.on_epoch(2, kT2873 + 100, s);
        CHECK(!l.waiting());
        CHECK(l.trip().gen == 0 && l.epoch() == 2);
        CHECK(l.judge(offer(omr::Mode::On)) == omr::Gate::NoTrip);
        const p8p::Event* end = s.last(omr::kNTripEnd);
        CHECK(end && end->v[1] == static_cast<uint64_t>(omr::Outcome::Cancelled));
    }
    {  // logout asked
        omr::Ledger l(omr::Mode::On);
        reload_until_2873(l, s);
        l.record_offer(omr::Gate::Drop, false, ftl::Where::HostField, kT2873, s);
        l.on_logout(kT2873 + 100, s);
        CHECK(!l.waiting());
        CHECK(omr::Ledger::outcome_of(l.trip()) == omr::Outcome::Cancelled);
        CHECK(!l.at_exit(exit_facts(kT2873 + 400'000), false, s).act);
    }
    {  // the 2322 lost in the queue / not seen / wrong session
        omr::Ledger l(omr::Mode::On);
        reload_until_2873(l, s);
        l.record_offer(omr::Gate::Drop, false, ftl::Where::HostField, kT2873, s);
        omr::ExitDecision d = l.at_exit(exit_facts(kT2873 + 400'000), false, s);
        l.on_out(omr::kOpMilestone, true, false, kLogic, kT2873 + 401'000, kActor2, true, s);
        l.on_called(d.gen, oms::CallResult::Returned, kSession2, kT2873 + 402'000, 10, kLogic, s);
        CHECK(omr::Ledger::outcome_of(l.trip()) == omr::Outcome::QueueLost);
        CHECK(l.counters().lost == 1);
    }
    {
        omr::Ledger l(omr::Mode::On);
        reload_until_2873(l, s);
        l.record_offer(omr::Gate::Drop, false, ftl::Where::HostField, kT2873, s);
        omr::ExitDecision d = l.at_exit(exit_facts(kT2873 + 400'000), false, s);
        // a 2322 on ANOTHER thread during the call is not ours (and is user 2's native)
        l.on_out(omr::kOpMilestone, true, true, kRecv, kT2873 + 401'000, kActor2, true, s);
        l.on_called(d.gen, oms::CallResult::Returned, kSession2, kT2873 + 402'000, 10, kLogic, s);
        CHECK(omr::Ledger::outcome_of(l.trip()) == omr::Outcome::CalledNotSeen);
        CHECK(l.trip().native_2322 == 1);
    }
    {
        omr::Ledger l(omr::Mode::On);
        reload_until_2873(l, s);
        l.record_offer(omr::Gate::Drop, false, ftl::Where::HostField, kT2873, s);
        omr::ExitDecision d = l.at_exit(exit_facts(kT2873 + 400'000), false, s);
        l.on_called(d.gen, oms::CallResult::WrongRecipient, 0x9999, kT2873 + 402'000, 10, kLogic, s);
        CHECK(omr::Ledger::outcome_of(l.trip()) == omr::Outcome::CallFailed);
        CHECK(l.counters().failed == 1);
    }
    {  // a reload whose 2873 never came
        omr::Ledger l(omr::Mode::On);
        reload_until_2873(l, s);
        CHECK(omr::Ledger::outcome_of(l.trip()) == omr::Outcome::No2873);
        l.record_offer(omr::Gate::EmitterOff, true, ftl::Where::HostField, kT2873, s);
        CHECK(omr::Ledger::outcome_of(l.trip()) == omr::Outcome::Passed);
    }
}

// --------------------------------------------------------------------------------------------- P8 S27 revive
//
// observer_milestone_reload_any (docs/research/s25/reaparicion.md). The host times of session 27, step 1 (host
// capture cdmp_packets_20261012-220503 / its log, t in us of the capture): the guest's second load of its entry
// (2437 at 205.573, answered by a 2174: TravelConsume trip 0, +0x568 field change), its fast travel R (2283 at
// 241.028, 2437 243.024, 2873 246.901, our 2322 247.226, 2762 250.563), then its death and revive: 2362 1361.752,
// 2437 1361.835 (2643 + 3066 inside it), 2988, 3053 + 2475, 2873 1361.976. In the session the 2873 passed natively
// (the fast-travel trip was over) and the guest stayed in load state 0x11. Every 2873 is offered at its real time,
// so the 45 s bound of the gate is live in these tests.

constexpr uint64_t kS = 1'000'000;
// user 2's TS before the revive's 2437 (TravelConsume: +0x550 0x1 +0x568 0 +0x569 1 +0x591 1) and before the entry's
// 2437 (+0x568 set: a field change)
const uint64_t kTsRevive = tpl::pack(tpl::TsSnap{true, 1, 0, 1, 1});
const uint64_t kTsEntry = tpl::pack(tpl::TsSnap{true, 1, 1, 0, 1});

omr::OfferFacts offer_at(omr::Mode m, uint64_t t_us, ftl::Where w = ftl::Where::HostField, bool ready = true) {
    omr::OfferFacts f = offer(m, w, ready);
    f.t_us = t_us;
    return f;
}

// The entry's second load (a field change): with the key on, a trip at its 2437 that ends as a field change.
void s27_entry(omr::Ledger& l, Sink& s) {
    l.on_2437_any(205'573'428, 0, 0, kTsEntry, s);
    l.on_out(omr::kOpMoveFieldAck, true, true, kRecv, 205'589'000, kHost, false, s);
    CHECK(!l.last_2437().unfinished_reload());
}

// The fast travel of S27 (an F3 trip, driven through the server thread's entry point on_2437_any): dropped, emitted,
// loaded.
void s27_fast_travel(omr::Ledger& l, Sink& s) {
    l.on_f3_accepted(241'028'160, s);
    l.on_2437_any(243'022'853, omr::kOpReloadStageStartReq, 1'994'693, kTsRevive, s);
    CHECK(l.trip().phase == omr::Phase::Asked && l.trip().origin == omr::Origin::F3_2283);
    l.on_out(omr::kOpLoadingFieldDataAck, true, true, kLogic, 243'023'946, 0, false, s);
    l.on_out(omr::kOpMoveFieldCompletePostAck, true, true, kLogic, 243'023'950, 0, false, s);
    CHECK(l.trip().phase == omr::Phase::Reload);
    const omr::Gate g = l.judge(offer_at(l.mode(), 246'900'523));
    CHECK(g == omr::Gate::Drop);
    l.record_offer(g, false, ftl::Where::HostField, 246'900'523, s);
    const omr::ExitDecision d = l.at_exit(exit_facts(247'226'085), false, s);
    CHECK(d.act && d.emit && !d.dry);
    l.on_out(omr::kOpMilestone, true, true, kLogic, 247'226'095, kActor2, true, s);
    l.on_called(d.gen, oms::CallResult::Returned, kSession2, 247'226'096, 7, kLogic, s);
    l.on_out(omr::kOpLoadingCompleteAck, true, true, kRecv, 250'563'114, 0, false, s);
    CHECK(l.trip().phase == omr::Phase::Loaded);
    CHECK(omr::Ledger::outcome_of(l.trip()) == omr::Outcome::Emitted);
}

// The revive up to its 2873 (2988 / 3053 / 2475 are not followed by the ledger).
void s27_revive_until_2873(omr::Ledger& l, Sink& s) {
    l.on_2437_any(1361'835'000, omr::kOpReviveLoadingReq, 83'000, kTsRevive, s);
    l.on_out(omr::kOpLoadingFieldDataAck, true, true, kLogic, 1361'835'189, 0, false, s);
    l.on_out(omr::kOpMoveFieldCompletePostAck, true, true, kLogic, 1361'835'195, 0, false, s);
}

constexpr uint64_t kRevive2873 = 1361'976'403;

void test_reload_any_revive_s27() {
    {  // key 1: R1 = the entry (field change), R2 = the fast travel (F3), R3 = the revive: dropped, emitted once
        omr::Ledger l(omr::Mode::On);
        l.set_any(omr::Mode::On);
        Sink s;
        s27_entry(l, s);
        CHECK(l.trip().gen == 1 && l.trip().origin == omr::Origin::At2437 && l.trip().phase == omr::Phase::FieldChange);
        s27_fast_travel(l, s);
        CHECK(l.trip().gen == 2 && l.counters().trips == 2 && l.counters().trips_at_2437 == 1);
        const p8p::Event* end1 = s.last(omr::kNTripEnd);  // R1 ended when R2 began
        CHECK(end1 && end1->v[0] == 1 && end1->v[1] == static_cast<uint64_t>(omr::Outcome::FieldChange) &&
              (end1->v[10] & 0xFF) == static_cast<uint64_t>(omr::Origin::At2437));
        s27_revive_until_2873(l, s);
        CHECK(l.trip().gen == 3 && l.trip().origin == omr::Origin::At2437 && !l.trip().shadow);
        CHECK(l.trip().load_op == omr::kOpReviveLoadingReq);
        CHECK(l.trip().phase == omr::Phase::Reload);
        CHECK(l.counters().trips_at_2437 == 2 && l.counters().reloads_at_2437 == 1 && l.counters().reloads == 2);
        CHECK(l.counters().stale_closed == 0 && l.counters().nested_2437 == 0);
        const p8p::Event* begin = s.last(omr::kNTrip2437);
        CHECK(begin && begin->v[0] == 3 && begin->v[2] == 2 && begin->v[3] == omr::kOpReviveLoadingReq &&
              begin->v[4] == 83'000 && begin->v[5] == kTsRevive && begin->v[6] == 0);
        const p8p::Event* end2 = s.last(omr::kNTripEnd);  // R2 (F3) ended when R3 began
        CHECK(end2 && end2->v[0] == 2 && end2->v[1] == static_cast<uint64_t>(omr::Outcome::Emitted) &&
              (end2->v[10] & 0xFF) == static_cast<uint64_t>(omr::Origin::F3_2283));
        const omr::Gate g = l.judge(offer_at(omr::Mode::On, kRevive2873));
        CHECK(g == omr::Gate::Drop);
        l.record_offer(g, false, ftl::Where::HostField, kRevive2873, s);
        CHECK(l.waiting() && l.counters().native_in_reload == 0);
        CHECK(!l.at_exit(exit_facts(kRevive2873 + 100'000), false, s).act);  // < 300 ms
        const omr::ExitDecision d = l.at_exit(exit_facts(kRevive2873 + 330'000), false, s);
        CHECK(d.act && d.emit && !d.dry && d.gen == 3);
        l.on_out(omr::kOpMilestone, true, true, kLogic, kRevive2873 + 330'010, kActor2, true, s);
        l.on_called(d.gen, oms::CallResult::Returned, kSession2, kRevive2873 + 330'020, 7, kLogic, s);
        CHECK(omr::Ledger::outcome_of(l.trip()) == omr::Outcome::Emitted);
        CHECK(l.counters().emits == 2 && l.counters().enqueued == 2);
        // its 2445 -> 2762: over; a later 2873 passes natively (and is not a warning: the load is finished)
        l.on_out(omr::kOpLoadingCompleteAck, true, true, kRecv, kRevive2873 + 2'500'000, 0, false, s);
        CHECK(l.trip().phase == omr::Phase::Loaded);
        const omr::Gate g2 = l.judge(offer_at(omr::Mode::On, kRevive2873 + 3'000'000));
        CHECK(g2 == omr::Gate::Over);
        l.record_offer(g2, true, ftl::Where::HostField, kRevive2873 + 3'000'000, s);
        CHECK(l.counters().native_in_reload == 0 && s.count(omr::kNNativeInReload) == 0);
        l.on_epoch(2, kRevive2873 + 4'000'000, s);
        const p8p::Event* end3 = s.last(omr::kNTripEnd);
        CHECK(end3 && end3->v[0] == 3 && end3->v[1] == static_cast<uint64_t>(omr::Outcome::Emitted) &&
              (end3->v[10] & 0xFF) == static_cast<uint64_t>(omr::Origin::At2437) &&
              (end3->v[10] >> 16) == omr::kOpReviveLoadingReq);
    }
    {  // key 0 (session 27 as it ran): no trip at the 2437s; the 2873 is judged against the finished F3 trip and
       // passes; the warning names it
        omr::Ledger l(omr::Mode::On);
        Sink s;
        s27_entry(l, s);
        CHECK(l.trip().gen == 0);
        s27_fast_travel(l, s);
        s27_revive_until_2873(l, s);
        CHECK(l.trip().gen == 1 && l.trip().phase == omr::Phase::Loaded);  // the F3 trip untouched
        CHECK(l.last_2437().unfinished_reload());
        const omr::Gate g = l.judge(offer_at(omr::Mode::On, kRevive2873));
        CHECK(g == omr::Gate::Over);
        l.record_offer(g, true, ftl::Where::HostField, kRevive2873, s);
        CHECK(!l.waiting());
        CHECK(l.counters().native_in_reload == 1);
        const p8p::Event* w = s.last(omr::kNNativeInReload);
        CHECK(w && w->v[0] == 1 && w->v[1] == static_cast<uint64_t>(omr::Gate::Over) &&
              w->v[3] == kRevive2873 - 1361'835'000);
    }
    {  // key probe: shadow trips; the revive's 2873 goes to the server (WOULD DROP), the decision is dry, the warning
        omr::Ledger l(omr::Mode::On);
        l.set_any(omr::Mode::Probe);
        Sink s;
        s27_entry(l, s);
        CHECK(l.trip().gen == 1 && l.trip().shadow);
        s27_fast_travel(l, s);  // the F3 trip is a real one whatever the key
        CHECK(!l.trip().shadow && l.counters().emits == 1);
        s27_revive_until_2873(l, s);
        CHECK(l.trip().gen == 3 && l.trip().shadow && l.trip().phase == omr::Phase::Reload);
        const omr::Gate g = l.judge(offer_at(omr::Mode::On, kRevive2873));
        CHECK(g == omr::Gate::WouldDrop && !omr::drops(g));
        l.record_offer(g, true, ftl::Where::HostField, kRevive2873, s);
        CHECK(l.waiting() && l.trip().dry);
        CHECK(l.counters().native_in_reload == 1);
        const omr::ExitDecision d = l.at_exit(exit_facts(kRevive2873 + 330'000), false, s);
        CHECK(d.act && d.emit && d.dry);
        CHECK(!l.emitting(kLogic));
        CHECK(omr::Ledger::outcome_of(l.trip()) == omr::Outcome::DryWouldEmit);
        CHECK(l.counters().emits == 1 && l.counters().dry_emits == 1);
    }
    {  // key 1 but observer_milestone_reload=probe: the at-2437 trip is judged as probe (WOULD)
        omr::Ledger l(omr::Mode::Probe);
        l.set_any(omr::Mode::On);
        Sink s;
        s27_revive_until_2873(l, s);
        CHECK(l.trip().origin == omr::Origin::At2437 && l.trip().phase == omr::Phase::Reload);
        CHECK(l.judge(offer_at(omr::Mode::Probe, kRevive2873)) == omr::Gate::WouldDrop);
    }
    {  // key 1, emitter not ready: passes (never dropped without the emitter), and the warning says so
        omr::Ledger l(omr::Mode::On);
        l.set_any(omr::Mode::On);
        Sink s;
        s27_revive_until_2873(l, s);
        const omr::Gate g = l.judge(offer_at(omr::Mode::On, kRevive2873, ftl::Where::HostField, false));
        CHECK(g == omr::Gate::EmitterOff);
        l.record_offer(g, true, ftl::Where::HostField, kRevive2873, s);
        CHECK(!l.waiting() && l.counters().native_in_reload == 1);
    }
    {  // key 1, the revive's 2873 more than 45 s after its 2437 (never seen live): passes natively, with the warning
        omr::Ledger l(omr::Mode::On);
        l.set_any(omr::Mode::On);
        Sink s;
        s27_revive_until_2873(l, s);
        const uint64_t late = 1361'835'000 + omr::kMaxAgeUs + 1;
        const omr::Gate g = l.judge(offer_at(omr::Mode::On, late));
        CHECK(g == omr::Gate::Over);
        l.record_offer(g, true, ftl::Where::HostField, late, s);
        CHECK(l.counters().native_in_reload == 1);
        CHECK(l.judge(offer_at(omr::Mode::On, 1361'835'000 + omr::kMaxAgeUs - 1)) == omr::Gate::Drop);
    }
}

// An at-2437 trip is classified exactly as an F3 trip: a 2174 = a field change (observer_milestone's), never ours.
void test_reload_any_classification() {
    Sink s;
    {
        omr::Ledger l(omr::Mode::On);
        l.set_any(omr::Mode::On);
        l.on_2437_any(1 * kS, 0, 0, 0, s);
        CHECK(l.trip().gen == 1 && l.trip().phase == omr::Phase::Asked && l.trip().load_op == 0);
        CHECK(l.judge(offer_at(omr::Mode::On, 1 * kS + 10)) == omr::Gate::NotReload);  // not answered yet
        l.on_out(omr::kOpMoveFieldAck, true, true, kRecv, 1 * kS + 50'000, kHost, false, s);
        CHECK(l.trip().phase == omr::Phase::FieldChange);
        l.on_out(omr::kOpMoveFieldCompletePostAck, true, true, kRecv, 8 * kS, 0, false, s);
        CHECK(l.trip().phase == omr::Phase::FieldChange && !l.last_2437().unfinished_reload());
        const omr::Gate g = l.judge(offer_at(omr::Mode::On, 9 * kS));
        CHECK(g == omr::Gate::NotReload);
        l.record_offer(g, true, ftl::Where::HostField, 9 * kS, s);
        CHECK(!l.waiting() && l.counters().native_in_reload == 0);
        // the next 2437 (the trip is over: field change) opens the next trip
        l.on_2437_any(30 * kS, omr::kOpReviveLoadingReq, 50'000, kTsRevive, s);
        CHECK(l.trip().gen == 2 && l.trip().phase == omr::Phase::Asked);
        const p8p::Event* end = s.last(omr::kNTripEnd);
        CHECK(end && end->v[0] == 1 && end->v[1] == static_cast<uint64_t>(omr::Outcome::FieldChange));
    }
    {  // a 2643 to another session is not this trip's; a 2679 refuses it
        omr::Ledger l(omr::Mode::On);
        l.set_any(omr::Mode::On);
        l.on_2437_any(1 * kS, omr::kOpReviveLoadingReq, 0, 0, s);
        l.on_out(omr::kOpLoadingFieldDataAck, false, true, kRecv, 1 * kS + 1, 0, false, s);
        CHECK(l.trip().phase == omr::Phase::Asked && !l.last_2437().unfinished_reload());
        l.on_out(omr::kOpLogoutCharacterAck, true, true, kRecv, 1 * kS + 2, 0, false, s);
        CHECK(l.trip().phase == omr::Phase::Refused);
    }
    {  // user 2 in a field of its own: the native path is its own (no drop, no warning)
        omr::Ledger l(omr::Mode::On);
        l.set_any(omr::Mode::On);
        Sink s2;
        s27_revive_until_2873(l, s2);
        const omr::Gate g = l.judge(offer_at(omr::Mode::On, kRevive2873, ftl::Where::OwnField));
        CHECK(g == omr::Gate::OwnField);
        l.record_offer(g, true, ftl::Where::OwnField, kRevive2873, s2);
        CHECK(l.counters().native_in_reload == 0 && s2.count(omr::kNNativeInReload) == 0);
    }
}

// The warning (review of d09254d): only in the host's field; a NoTrip gate in the host's field warns too.
void test_reload_warning_where() {
    for (const ftl::Where w : {ftl::Where::OwnField, ftl::Where::OtherField, ftl::Where::Unknown, ftl::Where::HostField}) {
        omr::Ledger l(omr::Mode::On);  // key 0: no trip at the 2437, so the gate is NoTrip / NotShared / OwnField
        Sink s;
        s27_revive_until_2873(l, s);
        CHECK(l.trip().gen == 0 && l.last_2437().unfinished_reload());
        const omr::Gate g = l.judge(offer_at(omr::Mode::On, kRevive2873, w));
        CHECK(g == omr::Gate::NoTrip);
        l.record_offer(g, true, w, kRevive2873, s);
        const bool host = w == ftl::Where::HostField;
        CHECK(l.counters().native_in_reload == (host ? 1u : 0u));
        CHECK(s.count(omr::kNNativeInReload) == (host ? 1u : 0u));
        if (host) CHECK(s.last(omr::kNNativeInReload)->v[0] == 0);
    }
    {  // at most kNativeInReloadLines notes, all counted
        omr::Ledger l(omr::Mode::On);
        Sink s;
        s27_revive_until_2873(l, s);
        for (uint64_t i = 0; i < omr::kNativeInReloadLines + 4; ++i) {
            l.record_offer(omr::Gate::NoTrip, true, ftl::Where::HostField, kRevive2873 + i, s);
        }
        CHECK(l.counters().native_in_reload == omr::kNativeInReloadLines + 4);
        CHECK(s.count(omr::kNNativeInReload) == omr::kNativeInReloadLines);
    }
}

// A 2437 while a YOUNG trip is live is kept; a trip stuck in Asked (or an undecided reload) for kMaxAgeUs or more is
// closed by the next 2437 and a new trip opens (review of d09254d: else a later revive would be judged with the old
// 2437 and pass natively). Once decided or over, the next 2437 opens a new trip. An F3 trip takes its 2437 as in S16.
void test_reload_any_nested_and_f3() {
    Sink s;
    {
        omr::Ledger l(omr::Mode::On);
        l.set_any(omr::Mode::On);
        l.on_2437_any(1 * kS, omr::kOpReviveLoadingReq, 0, 0, s);
        l.on_2437_any(1 * kS + 10, omr::kOpReviveLoadingReq, 0, 0, s);  // Asked: kept
        CHECK(l.trip().gen == 1 && l.counters().nested_2437 == 1 && s.count(omr::kNNested) == 1);
        l.on_out(omr::kOpLoadingFieldDataAck, true, true, kRecv, 1 * kS + 20, 0, false, s);
        l.on_2437_any(1 * kS + 30, 0, 0, 0, s);  // Reload, undecided, young: kept
        CHECK(l.trip().gen == 1 && l.counters().nested_2437 == 2);
        l.record_offer(omr::Gate::Drop, false, ftl::Where::HostField, 2 * kS, s);
        const omr::ExitDecision d = l.at_exit(exit_facts(2 * kS + 400'000), false, s);
        CHECK(d.act && d.emit);
        l.on_called(d.gen, oms::CallResult::Returned, kSession2, 2 * kS + 400'001, 7, kLogic, s);
        l.on_2437_any(3 * kS, omr::kOpReviveLoadingReq, 0, 0, s);  // decided: a new trip
        CHECK(l.trip().gen == 2 && l.trip().phase == omr::Phase::Asked && l.counters().stale_closed == 0);
    }
    {  // stuck in Asked: a 2437 30 s later is kept; one 45 s or more after its 2437 closes it and opens a new trip
        omr::Ledger l(omr::Mode::On);
        l.set_any(omr::Mode::On);
        Sink s2;
        l.on_2437_any(10 * kS, 0, 0, 0, s2);  // never answered
        l.on_2437_any(40 * kS, 0, 0, 0, s2);
        CHECK(l.trip().gen == 1 && l.counters().nested_2437 == 1);
        l.on_2437_any(10 * kS + omr::kMaxAgeUs, omr::kOpReviveLoadingReq, 50'000, kTsRevive, s2);
        CHECK(l.trip().gen == 2 && l.trip().phase == omr::Phase::Asked && l.counters().stale_closed == 1);
        CHECK(l.trip().t_2437 == 10 * kS + omr::kMaxAgeUs && l.trip().load_op == omr::kOpReviveLoadingReq);
        const p8p::Event* end = s2.last(omr::kNTripEnd);
        CHECK(end && end->v[0] == 1 && end->v[1] == static_cast<uint64_t>(omr::Outcome::NotReload));
        // the revive behind it is judged with ITS 2437: dropped and emitted
        const uint64_t t0 = 10 * kS + omr::kMaxAgeUs;
        l.on_out(omr::kOpLoadingFieldDataAck, true, true, kRecv, t0 + 100, 0, false, s2);
        l.on_out(omr::kOpMoveFieldCompletePostAck, true, true, kRecv, t0 + 110, 0, false, s2);
        const omr::Gate g = l.judge(offer_at(omr::Mode::On, t0 + 141'000));
        CHECK(g == omr::Gate::Drop);
        l.record_offer(g, false, ftl::Where::HostField, t0 + 141'000, s2);
        const omr::ExitDecision d = l.at_exit(exit_facts(t0 + 500'000), false, s2);
        CHECK(d.act && d.emit && d.gen == 2);
    }
    {  // an undecided reload (no 2873 came) is closed by a 2437 kMaxAgeUs later
        omr::Ledger l(omr::Mode::On);
        l.set_any(omr::Mode::On);
        Sink s3;
        l.on_2437_any(5 * kS, omr::kOpReviveLoadingReq, 0, 0, s3);
        l.on_out(omr::kOpLoadingFieldDataAck, true, true, kRecv, 5 * kS + 100, 0, false, s3);
        l.on_2437_any(5 * kS + omr::kMaxAgeUs + 5, omr::kOpReviveLoadingReq, 0, 0, s3);
        CHECK(l.trip().gen == 2 && l.counters().stale_closed == 1);
        CHECK(s3.last(omr::kNTripEnd)->v[1] == static_cast<uint64_t>(omr::Outcome::No2873));
    }
    {  // an F3 trip (Prepared) takes the next 2437 whatever the key says
        omr::Ledger l(omr::Mode::On);
        l.set_any(omr::Mode::On);
        Sink s2;
        l.on_f3_accepted(1 * kS, s2);
        l.on_2437_any(3 * kS, omr::kOpReloadStageStartReq, 2 * kS, kTsRevive, s2);
        CHECK(l.trip().gen == 1 && l.trip().origin == omr::Origin::F3_2283 && l.trip().phase == omr::Phase::Asked);
        CHECK(l.counters().trips_at_2437 == 0 && s2.count(omr::kNTrip2437) == 0);
    }
    {  // key 0: on_2437_any is exactly S16's on_2437 (no trip without F3)
        omr::Ledger l(omr::Mode::On);
        l.on_2437_any(1 * kS, omr::kOpReviveLoadingReq, 0, kTsRevive, s);
        CHECK(l.trip().gen == 0 && l.trip().phase == omr::Phase::None);
        CHECK(l.judge(offer_at(omr::Mode::On, 1 * kS + 10)) == omr::Gate::NoTrip);
    }
    {  // a new epoch / a logout forgets the last 2437 (no warning for a 2873 of the next connection)
        omr::Ledger l(omr::Mode::On);
        s27_revive_until_2873(l, s);
        CHECK(l.last_2437().unfinished_reload());
        l.on_epoch(2, 2 * kS, s);
        CHECK(!l.last_2437().unfinished_reload());
        s27_revive_until_2873(l, s);
        l.on_logout(2 * kS, s);
        CHECK(!l.last_2437().unfinished_reload());
    }
}

// The S15/S16 fast travel (F3 trip) gives the same result whether the server thread calls on_2437 (S16) or
// on_2437_any, with the key off or on: the key changes nothing for F3 trips.
void test_reload_any_f3_regression() {
    for (const omr::Mode any : {omr::Mode::Off, omr::Mode::Probe, omr::Mode::On}) {
        omr::Ledger l(omr::Mode::On);
        l.set_any(any);
        Sink s;
        l.on_f3_accepted(1'000'000, s);
        l.on_2437_any(7'690'000, omr::kOpReloadStageStartReq, 6'690'000, kTsRevive, s);
        l.on_out(omr::kOpLoadingFieldDataAck, true, true, kLogic, 7'700'000, 0, false, s);
        l.on_out(omr::kOpMoveFieldCompletePostAck, true, true, kLogic, 7'701'000, 0, false, s);
        CHECK(l.trip().phase == omr::Phase::Reload && l.trip().origin == omr::Origin::F3_2283 && !l.trip().shadow);
        const omr::Gate g = l.judge(offer_at(omr::Mode::On, kT2873));
        CHECK(g == omr::Gate::Drop);
        l.record_offer(g, false, ftl::Where::HostField, kT2873, s);
        const omr::ExitDecision d = l.at_exit(exit_facts(kT2873 + 350'000), false, s);
        CHECK(d.act && d.emit && !d.dry);
        l.on_out(omr::kOpMilestone, true, true, kLogic, kT2873 + 351'000, kActor2, true, s);
        l.on_called(d.gen, oms::CallResult::Returned, kSession2, kT2873 + 352'000, 900, kLogic, s);
        CHECK(omr::Ledger::outcome_of(l.trip()) == omr::Outcome::Emitted);
        CHECK(l.counters().trips == 1 && l.counters().trips_at_2437 == 0 && l.counters().native_in_reload == 0);
        CHECK(s.count(omr::kNTrip2437) == 0 && s.count(omr::kNNativeInReload) == 0);
    }
}

}  // namespace

int main() {
    test_reload_modes();
    test_reload_emit_once();
    test_reload_trip_over();
    test_reload_not_ours();
    test_reload_probe();
    test_reload_gates();
    test_reload_cancel_and_failures();
    test_reload_any_revive_s27();
    test_reload_any_classification();
    test_reload_warning_where();
    test_reload_any_nested_and_f3();
    test_reload_any_f3_regression();
    test_switch();
    test_order_and_emit();
    test_duplicates();
    test_invalidation();
    test_reconnection();
    test_queue_failure();
    test_wrong_recipient();
    test_dry_run();
    test_guest_trips();
    if (g_failures) {
        std::printf("optd_observer_milestone: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("optd_observer_milestone: all passed\n");
    return 0;
}
