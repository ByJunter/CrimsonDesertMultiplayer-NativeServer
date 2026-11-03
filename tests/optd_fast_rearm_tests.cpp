// S37 phase D (docs/research/s25/s37-fase-d.md): the pure logic of [session2] user2_fast_rearm (fast_rearm_logic.h).
//
// SCOPE: offline logic only:
//  - the fast verdict: S34 1b's departure (1 -> 61 cells, the host's L 0.8 s before) is FAST; walking past the
//    threshold, the guest's own travel, the host's ARRIVAL L (review 3a), a missing / invalid / late sample, another
//    field or character, an old or future L, every pending state and the 60 s gap are not; boundaries 7 / 8 / 9 / 10;
//  - the confirmation (review 3b): a guest that loads after the jump (it follows the host) drops the candidate;
//  - the one-shot payload (+0xB 0, the rest the periodic one's) and its delay unit (100 ms; never the 53d70df
//    confusion: the period is raw x 1000 ms);
//  - the one-shot slot: one pending per generation, consumed once, cancel / lost / late (5 s window), settle at each
//    arm, failed queue, arm -> drain -> arm; a one-shot never counts as a chain tick (it would mask chain_kick_due);
//  - the time saved, the leftover WRN verdict, the sample history.
// They do NOT test the hooks, the game's code nor the server.

#include <cstdint>
#include <cstdio>

#include "mod/session2/fast_rearm_logic.h"

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

constexpr uint64_t kS = 1'000'000;

frm::Sample sample(uint64_t t, uint32_t dist, u2s::Cell host, u2s::Cell u2, uintptr_t field = 0x1000,
                   uintptr_t chr = 0x2000) {
    frm::Sample s;
    s.valid = true;
    s.t_us = t;
    s.dist = dist;
    s.host = host;
    s.user2 = u2;
    s.field = field;
    s.u2_char = chr;
    return s;
}

// S34 1b (4-contencion §2): Hernand, the host 1 cell from user 2, its load L at 18:55:51.646, the first far sample
// (61 cells) ~0.8 s later.
frm::FastIn departure() {
    frm::FastIn in;
    const uint64_t t0 = 1000 * kS;
    in.prev = sample(t0, 1, {-168, -62}, {-167, -62});
    in.cur = sample(t0 + 1 * kS, 61, {-228, -40}, {-167, -62});
    in.host_load_us = t0 + 200'000;
    in.near_since_us = t0 - 60 * kS;  // the host had been next to user 2 for a minute
    return in;
}

void test_departure_is_fast() {
    frm::FastIn in = departure();
    CHECK(u2s::cheb(in.cur.host, in.cur.user2) == 61);
    CHECK(frm::fast_verdict(in) == frm::Fast::Fast);
    // A long departure load: L 29 s before the far sample (the position moved late), still inside the near streak.
    in.host_load_us = in.cur.t_us - 29 * kS;
    CHECK(frm::fast_verdict(in) == frm::Fast::Fast);
}

// Review 3a: the host fast-travels IN next to user 2 (its arrival L at t0 - 20 s, near from t0 - 15 s); at t0 + 1 s a
// stale / corrected reading puts it 12 cells away for one sample. L is within 30 s but it is the arrival's: no.
void test_arrival_is_not_fast() {
    frm::FastIn in = departure();
    const uint64_t t0 = in.prev.t_us;
    in.host_load_us = t0 - 20 * kS;
    in.near_since_us = t0 - 15 * kS;
    in.cur = sample(t0 + kS, 12, {-180, -62}, {-167, -62});
    CHECK(frm::fast_verdict(in) == frm::Fast::LoadBeforeNear);
    in.host_load_us = in.near_since_us;  // the same instant is not "after"
    CHECK(frm::fast_verdict(in) == frm::Fast::LoadBeforeNear);
    in.near_since_us = 0;  // no near streak known (the machine was reset at this very sample)
    in.host_load_us = t0 + 200'000;
    CHECK(frm::fast_verdict(in) == frm::Fast::LoadBeforeNear);
}

// Review 3b: the candidate's confirmation over kConfirmUs.
frm::ConfirmIn confirm_base() {
    frm::ConfirmIn ci;
    ci.jump = sample(1000 * kS, 61, {-228, -40}, {-167, -62});
    ci.cur = sample(1000 * kS + 3 * kS, 61, {-228, -40}, {-167, -62});
    return ci;
}
void test_confirm() {
    frm::ConfirmIn ci = confirm_base();
    CHECK(frm::confirm_verdict(ci) == frm::Confirm::Go);
    ci.cur.t_us = ci.jump.t_us + 2 * kS;
    CHECK(frm::confirm_verdict(ci) == frm::Confirm::Wait);
    // The guest follows the host: its load Troc starts 2 s after the jump -> dropped before anything is asked.
    ci = confirm_base();
    ci.u2_load_us = ci.jump.t_us + 2 * kS;
    CHECK(frm::confirm_verdict(ci) == frm::Confirm::GuestLoad);
    ci.u2_load_us = ci.jump.t_us - 1;  // a load of user 2 before the jump is not this one
    CHECK(frm::confirm_verdict(ci) == frm::Confirm::Go);
    ci = confirm_base();
    ci.guest_loading = true;
    CHECK(frm::confirm_verdict(ci) == frm::Confirm::GuestLoad);
    ci = confirm_base();
    ci.blocked = true;
    CHECK(frm::confirm_verdict(ci) == frm::Confirm::Blocked);
    ci = confirm_base();
    ci.cur.valid = false;
    CHECK(frm::confirm_verdict(ci) == frm::Confirm::Invalid);
    ci = confirm_base();
    ci.cur.field = 0x9999;
    CHECK(frm::confirm_verdict(ci) == frm::Confirm::Invalid);
    ci = confirm_base();
    ci.cur.t_us = ci.jump.t_us + frm::kConfirmUs + frm::kMaxSampleGapUs + 1;
    CHECK(frm::confirm_verdict(ci) == frm::Confirm::Stale);
    ci = confirm_base();
    ci.cur.dist = 9;  // the host came back within the confirmation (a short hop)
    CHECK(frm::confirm_verdict(ci) == frm::Confirm::NotFar);
    ci = confirm_base();
    ci.cur.user2 = {-160, -62};  // user 2 moved since the jump
    CHECK(frm::confirm_verdict(ci) == frm::Confirm::GuestMoved);
    for (size_t i = 0; i < static_cast<size_t>(frm::Confirm::Count); ++i) {
        CHECK(frm::confirm_name(static_cast<frm::Confirm>(i))[0] != '?');
    }
}

void test_walking_is_not_fast() {
    // Walking / riding away: 7 -> 8 -> 9 -> 10 never jumps; the band never counts.
    u2s::Thresholds t;
    const uint64_t t0 = 50 * kS;
    CHECK(!frm::jump(sample(t0, 7, {0, 0}, {7, 0}), sample(t0 + kS, 8, {0, 0}, {8, 0}), t));
    CHECK(!frm::jump(sample(t0, 8, {0, 0}, {8, 0}), sample(t0 + kS, 10, {0, 0}, {10, 0}), t));
    CHECK(!frm::jump(sample(t0, 7, {0, 0}, {7, 0}), sample(t0 + kS, 9, {0, 0}, {9, 0}), t));
    CHECK(frm::jump(sample(t0, 7, {0, 0}, {7, 0}), sample(t0 + kS, 10, {0, 0}, {10, 0}), t));
    frm::FastIn in;
    in.prev = sample(t0, 7, {7, 0}, {0, 0});
    in.cur = sample(t0 + kS, 9, {9, 0}, {0, 0});
    CHECK(frm::fast_verdict(in) == frm::Fast::NoJump);
    // A jump without the host's load step (a position correction, a stale read, a mount warp): no.
    in.prev = sample(t0, 7, {7, 0}, {0, 0});
    in.cur = sample(t0 + kS, 10, {10, 0}, {0, 0});
    CHECK(frm::fast_verdict(in) == frm::Fast::NoHostLoad);
}

void test_who_moved() {
    frm::FastIn in = departure();
    // The guest's own fast travel (the host still): no.
    in.prev = sample(in.prev.t_us, 1, {0, 0}, {1, 0});
    in.cur = sample(in.cur.t_us, 61, {0, 0}, {61, 0});
    CHECK(frm::fast_verdict(in) == frm::Fast::GuestMoved);
    // Both moved: no.
    in.cur = sample(in.cur.t_us, 61, {-30, 0}, {31, 0});
    CHECK(frm::fast_verdict(in) == frm::Fast::GuestMoved);
    // User 2 one cell off (a cell boundary): still the host's departure.
    in.cur = sample(in.cur.t_us, 61, {-60, 0}, {1, 1});
    CHECK(frm::fast_verdict(in) == frm::Fast::Fast);
    // The distances claim a jump the cells do not show (a stale distance / reading artefact): the host moved only 2
    // cells (below on - off = 3) and user 2 one: no.
    frm::FastIn h = departure();
    h.prev = sample(h.prev.t_us, 7, {0, 0}, {7, 0});
    h.cur = sample(h.cur.t_us, 10, {-2, 0}, {8, 0});
    CHECK(frm::fast_verdict(h) == frm::Fast::HostStill);
    // The host moved 3 cells in one sample with its load: fast (the smallest genuine jump).
    h.cur = sample(h.cur.t_us, 10, {-3, 0}, {7, 0});
    CHECK(frm::fast_verdict(h) == frm::Fast::Fast);
}

void test_samples() {
    frm::FastIn in = departure();
    // An unknown sample between (the machine reset): prev invalid -> no jump.
    in.prev.valid = false;
    CHECK(frm::fast_verdict(in) == frm::Fast::NoJump);
    in = departure();
    in.cur.t_us = in.prev.t_us + 3 * kS;  // a skipped sample
    CHECK(frm::fast_verdict(in) == frm::Fast::NotConsecutive);
    in = departure();
    in.cur.t_us = in.prev.t_us;  // the clock did not move
    CHECK(frm::fast_verdict(in) == frm::Fast::NotConsecutive);
    in = departure();
    in.cur.field = 0x1001;  // another field
    CHECK(frm::fast_verdict(in) == frm::Fast::OtherField);
    in = departure();
    in.prev.field = in.cur.field = 0;
    CHECK(frm::fast_verdict(in) == frm::Fast::OtherField);
    in = departure();
    in.cur.u2_char = 0x2001;  // another character
    CHECK(frm::fast_verdict(in) == frm::Fast::OtherEpoch);
}

void test_host_load_window() {
    frm::FastIn in = departure();
    in.host_load_us = 0;
    CHECK(frm::fast_verdict(in) == frm::Fast::NoHostLoad);
    in.host_load_us = in.cur.t_us - 31 * kS;
    CHECK(frm::fast_verdict(in) == frm::Fast::NoHostLoad);
    in.host_load_us = in.cur.t_us + 1;  // after the sample (a clock mix-up): no
    CHECK(frm::fast_verdict(in) == frm::Fast::NoHostLoad);
    in.host_load_us = in.cur.t_us;
    CHECK(frm::fast_verdict(in) == frm::Fast::Fast);
}

void test_states() {
    const auto with = [](void (*set)(frm::FastIn&)) {
        frm::FastIn in = departure();
        set(in);
        return frm::fast_verdict(in);
    };
    CHECK(with([](frm::FastIn& i) { i.aborted = true; }) == frm::Fast::Aborted);
    CHECK(with([](frm::FastIn& i) { i.armed = true; }) == frm::Fast::Armed);
    CHECK(with([](frm::FastIn& i) { i.machine_on = true; }) == frm::Fast::MachineOn);
    CHECK(with([](frm::FastIn& i) { i.pending = true; }) == frm::Fast::Pending);
    CHECK(with([](frm::FastIn& i) { i.drain_hold = true; }) == frm::Fast::DrainHold);
    CHECK(with([](frm::FastIn& i) { i.draining = true; }) == frm::Fast::Draining);
    CHECK(with([](frm::FastIn& i) { i.guest_loading = true; }) == frm::Fast::GuestLoading);
    CHECK(with([](frm::FastIn& i) { i.logout = true; }) == frm::Fast::Logout);
    CHECK(with([](frm::FastIn& i) { i.last_fast_us = i.cur.t_us - 59 * frm::kMinGapUs / 60; }) == frm::Fast::TooSoon);
    CHECK(with([](frm::FastIn& i) { i.last_fast_us = i.cur.t_us - frm::kMinGapUs; }) == frm::Fast::Fast);
    CHECK(with([](frm::FastIn& i) { i.fast_count = frm::kOneShotCap; }) == frm::Fast::Capped);
    // The re-check at the arm.
    CHECK(frm::fast_still_valid(false, false, false));
    CHECK(!frm::fast_still_valid(true, false, false));
    CHECK(!frm::fast_still_valid(false, true, false));
    CHECK(!frm::fast_still_valid(false, false, true));
    for (size_t i = 0; i < static_cast<size_t>(frm::Fast::Count); ++i) {
        CHECK(frm::fast_name(static_cast<frm::Fast>(i))[0] != '?');
    }
}

void test_payload_and_delay() {
    const uint8_t cell[8] = {};
    uint8_t chain[u2s::kTimerPayload] = {}, shot[u2s::kTimerPayload] = {};
    u2s::timer_2043_payload(chain, cell);
    frm::oneshot_2043_payload(shot, cell);
    CHECK(shot[0] == 0xFB && shot[1] == 0x07 && shot[2] == 0xFF);
    CHECK(chain[11] == 1);
    CHECK(shot[11] == 0);  // 0x287DBB0: param_3 == 0 -> evaluates, never reschedules
    CHECK(shot[12] == 0);  // +0xC 0: not the drain's immediate 2043 (+0xC 1)
    for (size_t i = 0; i < u2s::kTimerPayload; ++i) {
        if (i != 11) CHECK(shot[i] == chain[i]);
    }
    CHECK(frm::oneshot_shape(0, 0));
    CHECK(!frm::oneshot_shape(1, 0));  // the periodic chain
    CHECK(!frm::oneshot_shape(0, 1));  // the drain's immediate 2043 (0x102C2A30: u16 0x100 at +0xB)
    // The delay argument counts 100 ms: the chain's delay for raw 10 is 100 = the measured ~10 s period.
    CHECK(frm::delay_ms(u2s::timer_2043_delay_ms(10)) == u2s::timer_2043_period_ms(10));
    CHECK(u2s::timer_2043_period_ms(10) == 10'000);  // S33: raw x 1000 ms, never the delay taken as ms
    static_assert(frm::delay_ms(frm::kOneShotDelay) == 100);
    static_assert(frm::delay_ms(frm::kOneShotDelay) < u2s::kMinPeriodMs);
    // A late one-shot of generation N can never meet a pending one of N + 1: fast arms are >= 60 s apart and a one-shot
    // is lost after 5 s, both far above its 100 ms delay.
    static_assert(frm::kMinGapUs > frm::kOneShotLostUs);
    static_assert(frm::kOneShotLostUs > frm::delay_ms(frm::kOneShotDelay) * 1000);
}

void test_slot() {
    frm::OneShotSlot s;
    uint64_t gen = 0, q = 0;
    CHECK(!s.consume(gen, q));  // idle: a native one-shot is not ours
    CHECK(!s.late(1, gen));
    CHECK(!s.may_queue(0));
    CHECK(s.may_queue(1));
    CHECK(s.queued(1, 100));
    CHECK(!s.may_queue(1));
    CHECK(!s.may_queue(2));  // never two pending
    CHECK(!s.queued(2, 150));
    CHECK(s.consume(gen, q) && gen == 1 && q == 100);
    CHECK(!s.consume(gen, q));  // once
    CHECK(!s.late(200, gen));   // consumed: a later one-shot-shaped tick is someone else's
    CHECK(!s.may_queue(1));     // generation 1 is spent
    CHECK(s.may_queue(2));
    // arm -> drain -> arm: the drain cancels, the late tick is counted once, the next arm queues its own.
    CHECK(s.queued(2, 200));
    uint64_t c = 0;
    CHECK(s.cancel(frm::Cancel::Drain, 250, c) && c == 2);
    CHECK(!s.cancel(frm::Cancel::Drain, 260, c));
    CHECK(frm::OneShotSlot::cancel_of(s.raw()) == frm::Cancel::Drain);
    CHECK(!s.consume(gen, q));
    CHECK(s.late(300, gen) && gen == 2);
    CHECK(!s.late(310, gen));
    CHECK(s.may_queue(3));
    CHECK(s.queued(3, 300));
    // Lost: pending for 5 s with no tick.
    CHECK(!s.expire(300 + frm::kOneShotLostUs - 1, gen));
    CHECK(s.expire(300 + frm::kOneShotLostUs, gen) && gen == 3);
    CHECK(!s.expire(300 + 2 * frm::kOneShotLostUs, gen));
    CHECK(s.late(300 + frm::kOneShotLostUs + 1000, gen) && gen == 3);
    // A failed queue spends its generation, nothing pending.
    CHECK(s.may_queue(4));
    s.failed(4);
    CHECK(!s.may_queue(4));
    CHECK(!s.consume(gen, q));
    CHECK(s.may_queue(5));
    // Re-arm cancels a pending one of the old generation.
    CHECK(s.queued(5, 500));
    CHECK(s.cancel(frm::Cancel::Rearm, 550, c) && c == 5);
    CHECK(s.may_queue(6) && s.queued(6, 600));
    s.reset();
    CHECK(frm::OneShotSlot::state_of(s.raw()) == frm::Shot::Idle);
    // Packing keeps state, reason and generation apart.
    const uint64_t v = frm::OneShotSlot::pack(frm::Shot::Cancelled, frm::Cancel::NewCharacter, 0x123456789ull);
    CHECK(frm::OneShotSlot::state_of(v) == frm::Shot::Cancelled);
    CHECK(frm::OneShotSlot::cancel_of(v) == frm::Cancel::NewCharacter);
    CHECK(frm::OneShotSlot::gen_of(v) == 0x123456789ull);
}

// Review 2a: a cancelled / lost one-shot is "late" only for 5 s, and every arm settles it back to idle, so a native
// one-shot-shaped tick minutes later (the "not ready" one of user 2's own preparation) is a normal tick.
void test_slot_late_window_and_settle() {
    frm::OneShotSlot s;
    uint64_t gen = 0, c = 0;
    const uint64_t t = 1000 * kS;
    CHECK(s.queued(1, t));
    CHECK(s.cancel(frm::Cancel::Drain, t + 50'000, c));
    CHECK(!s.late(t + 50'000 + frm::kOneShotLostUs + 1, gen));  // too late to be ours
    CHECK(frm::OneShotSlot::state_of(s.raw()) == frm::Shot::Cancelled);
    s.settle();  // a hysteresis arm
    CHECK(frm::OneShotSlot::state_of(s.raw()) == frm::Shot::Idle);
    CHECK(frm::OneShotSlot::gen_of(s.raw()) == 1);
    CHECK(!s.late(t + 60'000, gen));
    CHECK(!s.may_queue(1));  // the generation stays spent
    CHECK(s.may_queue(2));
    // settle never touches a pending one.
    CHECK(s.queued(2, t + 70 * kS));
    s.settle();
    CHECK(frm::OneShotSlot::state_of(s.raw()) == frm::Shot::Pending);
    // Lost then settled.
    CHECK(s.expire(t + 70 * kS + frm::kOneShotLostUs, gen) && gen == 2);
    s.settle();
    CHECK(frm::OneShotSlot::state_of(s.raw()) == frm::Shot::Idle);
    // A late one, then settled.
    CHECK(s.queued(3, t + 200 * kS));
    CHECK(s.cancel(frm::Cancel::Rearm, t + 200 * kS + 10, c));
    CHECK(s.late(t + 200 * kS + 100'000, gen) && gen == 3);
    s.settle();
    CHECK(frm::OneShotSlot::state_of(s.raw()) == frm::Shot::Idle);
}

void test_chain_untouched() {
    // Why the one-shot is never a watchdog tick: a fast arm at t0 on a manager whose chain is dead; the one-shot
    // evaluates at t0 + 0.1 s. Counting it as user 2's last tick would push chain_kick_due (and NoTicks) past the
    // dead chain's 1.5 periods.
    const uint64_t t0 = 500 * kS;
    const uint32_t period = u2s::timer_2043_period_ms(10);
    const uint64_t at = t0 + 15 * kS + 50'000;
    CHECK(u2s::chain_kick_due(at, t0, 0, 0, period));                // the one-shot excluded: the restart comes
    CHECK(!u2s::chain_kick_due(at, t0, t0 + 100'000, 0, period));    // counted: masked
    CHECK(!u2s::chain_kick_due(at + 10 * kS, t0, 0, 1, period));     // still one restart per arm (53d70df)
    // The arm's own repair is unchanged: a chain silent for 1.5 periods is dead, whatever one-shots came.
    CHECK(u2s::chain_dead(t0, t0 - 15 * kS, period));
    CHECK(!u2s::chain_dead(t0, t0 - 14 * kS, period));
}

void test_saved() {
    const uint64_t arm = 1000 * kS;
    const uint32_t period = 10'000;
    CHECK(frm::next_chain_tick_us(arm, true, 0, period) == arm + 10 * kS);           // queued at the arm (S34 1b)
    CHECK(frm::next_chain_tick_us(arm, false, arm - 3 * kS, period) == arm + 7 * kS); // alive: 2b's 7.7 s shape
    CHECK(frm::next_chain_tick_us(arm, false, arm - 25 * kS, period) == arm + 5 * kS);
    CHECK(frm::next_chain_tick_us(arm, false, 0, period) == arm + 10 * kS);
    CHECK(frm::next_chain_tick_us(arm, true, 0, 0) == 0);
    CHECK(frm::saved_us(arm + 10 * kS, arm + 100'000) == 9'900'000);
    CHECK(frm::saved_us(arm, arm + 1) == 0);
}

void test_leftover() {
    const uint64_t read = 100 * kS;
    CHECK(frm::leftover_verdict(u2s::Mode::Off, true, read, read + 1) == frm::Leftover::Ask);  // key 0: today
    CHECK(frm::leftover_verdict(u2s::Mode::On, true, read, 0) == frm::Leftover::Skip);         // S34: during it
    CHECK(frm::leftover_verdict(u2s::Mode::On, false, read, read + 217'000) == frm::Leftover::Skip);  // ended after
    CHECK(frm::leftover_verdict(u2s::Mode::On, false, read, read - 1) == frm::Leftover::Ask);  // a real leftover
    CHECK(frm::leftover_verdict(u2s::Mode::On, false, read, 0) == frm::Leftover::Ask);
    CHECK(frm::leftover_verdict(u2s::Mode::Probe, true, read, 0) == frm::Leftover::WouldSkip);
}

void test_history() {
    frm::History h;
    CHECK(h.size() == 0);
    CHECK(!h.last().valid);
    for (uint32_t i = 0; i < 10; ++i) h.push(sample(i * kS, i, {0, 0}, {0, 0}));
    CHECK(h.size() == frm::kHistory);
    CHECK(h.at(0).dist == 2);
    CHECK(h.last().dist == 9);
    CHECK(h.last(1).dist == 8);
    CHECK(!h.last(frm::kHistory).valid);
    h.clear();
    CHECK(h.size() == 0);
}

}  // namespace

int main() {
    test_departure_is_fast();
    test_arrival_is_not_fast();
    test_confirm();
    test_walking_is_not_fast();
    test_who_moved();
    test_samples();
    test_host_load_window();
    test_states();
    test_payload_and_delay();
    test_slot();
    test_slot_late_window_and_settle();
    test_chain_untouched();
    test_saved();
    test_leftover();
    test_history();
    if (g_failures) {
        std::printf("optd_fast_rearm: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_fast_rearm: all passed\n");
    return 0;
}
