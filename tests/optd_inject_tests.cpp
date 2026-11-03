// P2 without game heap work on the mod's threads (src/mod/session2/inject_logic.h;
// docs/research/pendientes/p8-heap-inyeccion.md).
//
// SCOPE: offline logic only - the classification of an injection attempt
// (hold / drop / stop, never a logout for a busy or paused server), the queue
// between the network thread and the NetworkThread (order, bounds, the
// logout's reserve, epochs), the in-flight bound, the 2001's pacing, the stall
// watch and the exception notes. A simulated host pause checks that nothing is
// lost or reordered and that nothing asks for a logout. They do NOT test the
// hook at 0x35BAD4, the game's allocator or queue, nor the threads: that is
// the static analysis of the document and the test in the game.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "mod/game/fault_line.h"
#include "mod/session2/inject_logic.h"

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

inj::Pending make(uint16_t op, uint32_t epoch, uint8_t marker, size_t size = 8, uint64_t t = 0) {
    inj::Pending p;
    p.packet.assign(size, marker);
    p.opcode = op;
    p.epoch = epoch;
    p.tag = epoch;
    p.queued_ms = t;
    return p;
}

void test_classification() {
    using inj::Disposition;
    using inj::Outcome;
    CHECK(inj::disposition(Outcome::Queued) == Disposition::Done);
    // A busy or paused server, or a session not ready: held, never a logout.
    CHECK(inj::disposition(Outcome::NotReady) == Disposition::Hold);
    CHECK(inj::disposition(Outcome::Full) == Disposition::Hold);
    // Policy: dropped, the conversation goes on.
    CHECK(inj::disposition(Outcome::Refused) == Disposition::Drop);
    CHECK(inj::disposition(Outcome::Blocked) == Disposition::Drop);
    CHECK(inj::disposition(Outcome::AfterLogout) == Disposition::Drop);
    // An exception in the game's code: stop, no retry.
    CHECK(inj::disposition(Outcome::Halted) == Disposition::Stop);
    for (Outcome o : {Outcome::Queued, Outcome::NotReady, Outcome::Full, Outcome::Refused, Outcome::Blocked,
                      Outcome::AfterLogout, Outcome::Halted}) {
        CHECK(std::string(inj::outcome_name(o)) != "?");
    }
}

void test_queue_order_and_bounds() {
    inj::QueueLimits l;
    l.max_packets = 4;
    l.max_bytes = 1000;
    l.control_reserve = 2;
    inj::InjectQueue q(l);
    for (uint8_t i = 0; i < 4; ++i) CHECK(q.push(make(2556, 1, i), false) == inj::Outcome::Queued);
    CHECK(q.size() == 4);
    CHECK(q.bytes() == 32);
    // At the bound: an ordinary Req is held; the logout sequence uses the reserve.
    CHECK(!q.has_room(8, false));
    CHECK(q.push(make(2556, 1, 9), false) == inj::Outcome::Full);
    CHECK(q.full() == 1);
    CHECK(q.has_room(8, true));
    CHECK(q.push(make(2549, 1, 4), true) == inj::Outcome::Queued);
    CHECK(q.push(make(2001, 1, 5), true) == inj::Outcome::Queued);
    CHECK(q.push(make(2001, 1, 6), true) == inj::Outcome::Full);  // the reserve is bounded too
    CHECK(q.peak() == 6);
    // FIFO.
    inj::Pending p;
    for (uint8_t i = 0; i < 6; ++i) {
        CHECK(q.pop(p));
        CHECK(!p.packet.empty() && p.packet[0] == i);
    }
    CHECK(!q.pop(p));
    CHECK(q.bytes() == 0);
    // Bytes bound.
    inj::InjectQueue qb(l);
    CHECK(qb.push(make(1, 1, 0, 600), false) == inj::Outcome::Queued);
    CHECK(qb.push(make(1, 1, 1, 600), false) == inj::Outcome::Full);
    CHECK(qb.oldest_age(50) == 50);
}

void test_epochs() {
    inj::InjectQueue q;
    q.push(make(1, 1, 0), false);
    q.push(make(1, 2, 1), false);
    q.push(make(1, 1, 2), false);
    q.push(make(1, 2, 3), false);
    CHECK(q.drop_other_epochs(2) == 2);
    inj::Pending p;
    CHECK(q.pop(p) && p.packet[0] == 1);
    CHECK(q.pop(p) && p.packet[0] == 3);
    CHECK(q.size() == 0 && q.bytes() == 0);
    q.push(make(1, 2, 7), false);
    CHECK(q.clear() == 1 && q.bytes() == 0);
}

void test_may_hand() {
    inj::QueueLimits l;
    l.in_flight_max = 10;
    l.pump_budget = 4;
    CHECK(inj::may_hand(0, 0, 0, l) == 0);
    CHECK(inj::may_hand(0, 0, 3, l) == 3);
    CHECK(inj::may_hand(0, 0, 100, l) == 4);   // per-turn budget
    CHECK(inj::may_hand(18, 10, 100, l) == 2);  // 8 not yet taken: room for 2
    CHECK(inj::may_hand(20, 10, 100, l) == 0);  // the server is not taking them: keep the rest here
    CHECK(inj::may_hand(30, 10, 100, l) == 0);
    CHECK(inj::may_hand(5, 9, 2, l) == 2);      // seen > pushed (another epoch's buffers): no underflow
}

void test_pacer() {
    inj::RetryPacer r(250, 5, 5000);
    CHECK(r.due(1000));
    r.attempted(1000);
    uint32_t c = 0;
    CHECK(r.failed_should_log(1000, c) && c == 1);  // the first failure is written
    CHECK(!r.due(1100));
    CHECK(!r.due(1249));
    CHECK(r.due(1250));
    r.attempted(1250);
    CHECK(!r.failed_should_log(1250, c));  // within 5 s: aggregated
    r.attempted(1500);
    CHECK(!r.failed_should_log(1500, c));
    r.attempted(6100);
    CHECK(r.failed_should_log(6100, c) && c == 3);  // the line carries the count since the last one
    r.attempted(6400);
    CHECK(r.exhausted());
    CHECK(!r.due(100000));
    CHECK(r.failed_should_log(6400, c) && c == 1);  // the last one is written (giving up)
    // 39 attempts in 2.8 s as on 2026-10-09 would now be at most 12, and one error line.
    inj::RetryPacer live;
    int attempts = 0, lines = 0;
    for (uint64_t t = 0; t <= 2800; t += 72) {  // the 711 ms loop aside, ~72 ms per network turn
        if (!live.due(t)) continue;
        live.attempted(t);
        ++attempts;
        uint32_t n = 0;
        if (live.failed_should_log(t, n)) ++lines;
    }
    CHECK(attempts <= 12);
    CHECK(lines == 1);
    live.reset();
    CHECK(live.due(0) && live.attempts() == 0);
}

void test_stall_watch() {
    inj::StallWatch w(1000, 5000);
    using E = inj::StallWatch::Event;
    CHECK(w.update(0, 5, true) == E::None);
    CHECK(w.update(999, 5, true) == E::None);
    CHECK(w.update(1000, 5, true) == E::Enter);
    CHECK(w.stalled());
    CHECK(w.update(3000, 5, true) == E::None);
    CHECK(w.update(6000, 5, true) == E::Still);
    CHECK(w.stalled_ms(6000) == 6000);
    CHECK(w.update(6500, 6, true) == E::Exit);
    CHECK(!w.stalled());
    CHECK(w.stalled_ms(6500) == 6500);
    // No work waiting: never a stall, however long.
    inj::StallWatch idle(1000, 5000);
    CHECK(idle.update(0, 1, false) == E::None);
    CHECK(idle.update(60000, 1, false) == E::None);
    CHECK(!idle.stalled());
}

void test_exception_note() {
    inj::ExceptionNote e;
    e.code = 0xC0000005;
    e.in_exe = true;
    e.address = 0x1447EFE20;
    e.rva = 0x47EFE20;
    e.nparams = 2;
    e.params[0] = 0;
    e.params[1] = 0x3DDC43FA000;
    e.thread = 13456;
    e.step = inj::Step::Alloc;
    e.opcode = 2556;
    e.epoch = 1;
    const std::string s = inj::describe(e);
    CHECK(s.find("0xC0000005") != std::string::npos);
    CHECK(s.find("exe+0x47EFE20") != std::string::npos);
    CHECK(s.find("allocating") != std::string::npos);
    CHECK(s.find("read of 0x3DDC43FA000") != std::string::npos);
    CHECK(s.find("Req 2556") != std::string::npos);
    e.in_exe = false;
    e.step = inj::Step::Push;
    CHECK(inj::describe(e).find("0x1447EFE20") != std::string::npos);
    CHECK(inj::describe(e).find("Q0") != std::string::npos);
    // P8 review I1: only the mod's own steps are handled; inside the game's code the note says it was passed on.
    CHECK(inj::step_is_ours(inj::Step::Alloc) && inj::step_is_ours(inj::Step::Fill) &&
          inj::step_is_ours(inj::Step::BuildSession));
    CHECK(!inj::step_is_ours(inj::Step::Push) && !inj::step_is_ours(inj::Step::AllocGlobal) &&
          !inj::step_is_ours(inj::Step::BuildCtor));
    CHECK(inj::describe(e).find("passed on") != std::string::npos);
    e.step = inj::Step::Fill;
    CHECK(inj::describe(e).find("passed on") == std::string::npos);
    e.step = inj::Step::AllocGlobal;
    CHECK(inj::describe(e).find("global heap") != std::string::npos);
    e.step = inj::Step::BuildCtor;
    CHECK(inj::describe(e).find("0x1436EE0") != std::string::npos);
}

// The filters' text without allocating (game/fault_line.h; P8 review I1/M9).
void test_fault_line() {
    namespace game = cdmp::game;
    game::FixedLine l;
    l.str("x ").hex(0).str(" ").hex(0x1A2B).str(" ").hex32(0x80000001u).str(" ").dec(0).str(" ").dec(18446744073709551615ull);
    CHECK(l.view() == "x 0x0 0x1A2B 0x80000001 0 18446744073709551615");
    game::FaultFacts f;
    f.code = 0xC0000005;
    f.address = 0x14035DF77;
    f.base = 0x140000000;
    f.image_size = 0x20000000;
    f.nparams = 2;
    f.p0 = 1;
    f.p1 = 0x10;
    f.thread = 4242;
    game::FixedLine d;
    game::describe_fault(d, "Session2 injection", "pushing to Q0", f);
    CHECK(d.view() == "Session2 injection: exception 0xC0000005 at exe+0x35DF77 (write of 0x10) on thread 4242 while "
                      "pushing to Q0");
    f.address = 0x7FF812345678;
    f.code = 0xA0000002;
    game::FixedLine o;
    game::describe_fault(o, "w", "y", f);
    CHECK(o.view() == "w: exception 0xA0000002 at 0x7FF812345678 on thread 4242 while y");
    // Cut at the capacity, never past it.
    game::FixedLine big;
    for (int i = 0; i < 200; ++i) big.str("0123456789");
    CHECK(big.size() == 767 && big.view().back() == '6');
}

// The host opens its menu: the server stops taking buffers for 5 s while the
// guest keeps sending ~100 Req/s. Simulated network thread + NetworkThread +
// server: nothing is lost or reordered, nothing is a Stop, and the in-flight
// bound holds.
void test_host_pause_simulation() {
    inj::QueueLimits l;
    l.in_flight_max = 64;
    l.pump_budget = 32;
    inj::InjectQueue q(l);
    uint64_t pushed = 0, seen = 0, in_flight_max = 0;
    std::vector<uint32_t> server_order;
    std::vector<uint32_t> q0;  // the game's Q0 (only ours here)
    uint32_t next = 0;
    int stops = 0, holds = 0;
    std::vector<inj::Pending> held;  // the network thread's g_early
    auto offer = [&](inj::Pending&& p) {
        const inj::Outcome o = q.push(std::move(p), false);
        const inj::Disposition d = inj::disposition(o);
        if (d == inj::Disposition::Stop) ++stops;
        return d == inj::Disposition::Done;
    };
    inj::StallWatch watch(1000, 5000);
    int enters = 0, exits = 0;
    for (uint64_t t = 0; t < 12000; t += 10) {
        // The guest: one Req every 10 ms.
        inj::Pending p = make(2556, 1, 0, 4, t);
        p.packet[0] = static_cast<uint8_t>(next & 0xFF);
        p.tag = next++;
        if (!held.empty() || !offer(std::move(p))) {
            held.push_back(std::move(p));
            ++holds;
        }
        // The network thread drains what it held, in order.
        while (!held.empty()) {
            inj::Pending h = held.front();
            if (!offer(std::move(h))) break;
            held.erase(held.begin());
        }
        // The NetworkThread every turn (here every 10 ms).
        {
            size_t n = inj::may_hand(pushed, seen, q.size(), l);
            inj::Pending x;
            while (n-- && q.pop(x)) {
                q0.push_back(x.tag);
                ++pushed;
            }
            in_flight_max = std::max<uint64_t>(in_flight_max, pushed - seen);
        }
        // The server: paused from 3 s to 8 s.
        const bool paused = t >= 3000 && t < 8000;
        if (!paused) {
            for (uint32_t tag : q0) server_order.push_back(tag);
            seen += q0.size();
            q0.clear();
        }
        switch (watch.update(t, seen, pushed > seen)) {
            case inj::StallWatch::Event::Enter: ++enters; break;
            case inj::StallWatch::Event::Exit: ++exits; break;
            default: break;
        }
    }
    // Flush what is left after the pause.
    for (uint64_t t = 12000; q.size() || !q0.empty(); t += 16) {
        size_t n = inj::may_hand(pushed, seen, q.size(), l);
        inj::Pending x;
        while (n-- && q.pop(x)) {
            q0.push_back(x.tag);
            ++pushed;
        }
        for (uint32_t tag : q0) server_order.push_back(tag);
        seen += q0.size();
        q0.clear();
    }
    CHECK(stops == 0);
    CHECK(held.empty());
    CHECK(in_flight_max <= l.in_flight_max);
    CHECK(enters == 1 && exits == 1);  // one "the server has not taken our buffers" episode, then "again"
    CHECK(server_order.size() == next);
    bool ordered = true;
    for (uint32_t i = 0; i < server_order.size(); ++i) ordered = ordered && server_order[i] == i;
    CHECK(ordered);
}

}  // namespace

int main() {
    test_classification();
    test_queue_order_and_bounds();
    test_epochs();
    test_may_hand();
    test_pacer();
    test_stall_watch();
    test_exception_note();
    test_fault_line();
    test_host_pause_simulation();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_inject: all passed\n");
    return 0;
}
