// P8 option U, piece U-0 (docs/research/s25/mundo-escenarios-propios.md §5.7, "Implementado (U-0)").
//
// SCOPE: offline logic only (user2_stages_logic.h):
//  - the key (0 / probe / 1 held as probe / bad);
//  - 0x288B7A0's cell arithmetic (negative exact multiples go one cell further down, as the game does), Chebyshev;
//  - 0x4F8220 / 0x50EA80's world position: sectors, an identity parent, a translated / rotated / scaled parent;
//  - the arm check (every verdict in order), the would-be state machine (dwell, hysteresis band, unknown resets);
//  - who, the timer route table, the (reason, owner, caller) count table (packing, overflow), rva_in;
//  - the overlap counts against the S25 measurement's shape;
//  - §8.16: the [host cadence] baseline gate and the host's 2043 chain count (the logs' 10.1 s spacing, chains
//    0.05 / 0.46 / 0.87 s apart, a chain that stops, the login's two chains collapsing to one after a long load).
//  - S31 (§8.17): U-e's verdict (each condition in order, S31's case assigned), the key table (put / mark / reuse /
//    overflow), the stats and the control abort criteria, the 2005 record walk (S31's records: type, key, controller
//    0x90100000), the drain at the host's preparation (verdicts in order, the three callers of 0x287EF60).
// They do NOT test the hooks, the game's code nor the server.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "mod/session2/user2_stages_logic.h"

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

void test_key() {
    CHECK(u2s::parse_mode("").mode == u2s::Mode::Off);
    CHECK(u2s::parse_mode("0").mode == u2s::Mode::Off);
    CHECK(!u2s::parse_mode("0").bad);
    const auto p = u2s::parse_mode("probe");
    CHECK(p.mode == u2s::Mode::Probe && !p.asked_on && !p.bad);
    const auto one = u2s::parse_mode("1");
    CHECK(one.mode == u2s::Mode::On && !one.asked_on && !one.bad);
    const auto bad = u2s::parse_mode("yes");
    CHECK(bad.mode == u2s::Mode::Off && bad.bad);
}

void test_cells() {
    CHECK(u2s::cell_axis(0.0) == 0);
    CHECK(u2s::cell_axis(63.9) == 0);
    CHECK(u2s::cell_axis(64.0) == 1);
    CHECK(u2s::cell_axis(-0.5) == -1);
    CHECK(u2s::cell_axis(-63.9) == -1);
    CHECK(u2s::cell_axis(-64.0) == -2);  // (int)(-1.0) - 1: the game's own rounding
    CHECK(u2s::cell_axis(-64.5) == -2);
    CHECK(u2s::cell_axis(-10729.0) == -168);
    const u2s::Cell a{-168, -62}, b{-158, -60};
    CHECK(u2s::cheb(a, b) == 10);
    CHECK(u2s::cheb(b, a) == 10);
    CHECK(u2s::cheb(a, a) == 0);
    CHECK(u2s::cheb({0, 0}, {-3, 7}) == 7);
}

bool near_eq(double a, double b, double eps = 1e-3) { return std::fabs(a - b) < eps; }

void test_world() {
    // Sector -11 / -4 with a local (271, 620, 74): world (-10729, 620, -3926).
    u2s::SectorPos lp;
    lp.x = 271.0f;
    lp.y = 620.0f;
    lp.z = 74.0f;
    lp.sx = -11;
    lp.sz = -4;
    u2s::ParentXf id;
    CHECK(u2s::parent_is_identity(id));
    const u2s::World w = u2s::world_of(lp, id);
    CHECK(near_eq(w.x, -10729.0) && near_eq(w.y, 620.0) && near_eq(w.z, -3926.0));
    // A rider: local (0.5, 1.6, -0.2) on a mount at sector (-11, -4) + (300, 610, 80), no rotation.
    u2s::SectorPos rider;
    rider.x = 0.5f;
    rider.y = 1.6f;
    rider.z = -0.2f;
    u2s::ParentXf mount;
    mount.t[0] = 300.0f;
    mount.t[1] = 610.0f;
    mount.t[2] = 80.0f;
    mount.sx = -11;
    mount.sz = -4;
    CHECK(!u2s::parent_is_identity(mount));
    const u2s::World r = u2s::world_of(rider, mount);
    CHECK(near_eq(r.x, -10699.5) && near_eq(r.y, 611.6) && near_eq(r.z, -3920.2));
    // 90 degrees about Y (q = (0, sin45, 0, cos45)): local (1, 0, 0) -> (0, 0, -1); scale 2 first.
    u2s::SectorPos one;
    one.x = 1.0f;
    u2s::ParentXf rot;
    rot.q[0] = 0.0f;
    rot.q[1] = static_cast<float>(std::sqrt(0.5));
    rot.q[2] = 0.0f;
    rot.q[3] = static_cast<float>(std::sqrt(0.5));
    rot.scale[0] = 2.0f;
    const u2s::World q = u2s::world_of(one, rot);
    CHECK(near_eq(q.x, 0.0, 1e-4) && near_eq(q.y, 0.0, 1e-4) && near_eq(q.z, -2.0, 1e-4));
    // A rider's cell is its mount's (a metre or two away): the case the anchor's [minute] got wrong.
    CHECK(u2s::cell_of(r) == u2s::cell_of(u2s::World{-10700.0, 0.0, -3920.0}));
    CHECK(u2s::plausible(-10729.0f, 620.0f, -3926.0f));
    CHECK(!u2s::plausible(NAN, 0.0f, 0.0f));
    CHECK(!u2s::plausible(3.0e5f, 0.0f, 0.0f));
}

void test_arm() {
    u2s::MgrFacts f;
    CHECK(u2s::arm_verdict(f) == u2s::Arm::Unreadable);
    f.readable = true;
    f.prep_pending = 1;
    CHECK(u2s::arm_verdict(f) == u2s::Arm::Preparing);
    f.prep_pending = 0;
    f.preparing = 1;
    CHECK(u2s::arm_verdict(f) == u2s::Arm::Preparing);
    f.preparing = 0;
    f.state = 2;
    CHECK(u2s::arm_verdict(f) == u2s::Arm::Running);
    f.state = 7;
    CHECK(u2s::arm_verdict(f) == u2s::Arm::Running);
    f.state = 1;
    CHECK(u2s::arm_verdict(f) == u2s::Arm::NoSched);
    f.sm_ok = true;
    f.sm_count = 3;
    CHECK(u2s::arm_verdict(f) == u2s::Arm::ListsNotEmpty);
    f.sm_count = 0;
    f.fm_ok = true;
    f.fm_count = 1;
    CHECK(u2s::arm_verdict(f) == u2s::Arm::ListsNotEmpty);
    f.fm_count = 0;
    f.seq = 1;
    CHECK(u2s::arm_verdict(f) == u2s::Arm::ListsNotEmpty);
    f.seq = 0;
    f.sm_pending = 1;  // a pending 2045 does not stop the arm
    CHECK(u2s::arm_verdict(f) == u2s::Arm::Ready);
    // S28: the state user 2's manager really has in the host's field (reset): armable too.
    f.state = 0;
    f.sm_pending = 0;
    CHECK(u2s::arm_verdict(f) == u2s::Arm::Ready);
    for (size_t i = 0; i < static_cast<size_t>(u2s::Arm::Count); ++i) {
        CHECK(*u2s::arm_name(static_cast<u2s::Arm>(i)) != '?');
    }
}

void test_machine() {
    const u2s::Thresholds t;  // on >= 10 for 10 s, off <= 7 for 5 s
    u2s::Machine m;
    const uint64_t s = 1'000'000;
    CHECK(u2s::step(m, true, 12, 100 * s, t) == u2s::Step::None);
    CHECK(u2s::step(m, true, 12, 105 * s, t) == u2s::Step::None);
    CHECK(u2s::step(m, true, 11, 110 * s, t) == u2s::Step::WouldArm);
    CHECK(m.state == u2s::Would::On);
    CHECK(u2s::step(m, true, 15, 120 * s, t) == u2s::Step::None);  // already on
    // The band 8..9 stops both clocks.
    CHECK(u2s::step(m, true, 8, 130 * s, t) == u2s::Step::None);
    CHECK(u2s::step(m, true, 7, 131 * s, t) == u2s::Step::None);
    CHECK(u2s::step(m, true, 9, 134 * s, t) == u2s::Step::None);  // back in the band: the near clock restarts
    CHECK(u2s::step(m, true, 7, 135 * s, t) == u2s::Step::None);
    CHECK(u2s::step(m, true, 6, 139 * s, t) == u2s::Step::None);
    CHECK(u2s::step(m, true, 3, 140 * s, t) == u2s::Step::WouldDrain);
    CHECK(m.state == u2s::Would::Off);
    // Unknown positions reset the clocks.
    CHECK(u2s::step(m, true, 20, 200 * s, t) == u2s::Step::None);
    CHECK(u2s::step(m, false, 0, 205 * s, t) == u2s::Step::None);
    CHECK(u2s::step(m, true, 20, 209 * s, t) == u2s::Step::None);  // 4 s since it is known again
    CHECK(u2s::step(m, true, 20, 219 * s, t) == u2s::Step::WouldArm);
    // A brief dip below "on" restarts the far clock.
    u2s::Machine m2;
    CHECK(u2s::step(m2, true, 10, 0, t) == u2s::Step::None);
    CHECK(u2s::step(m2, true, 9, 6 * s, t) == u2s::Step::None);
    CHECK(u2s::step(m2, true, 10, 7 * s, t) == u2s::Step::None);
    CHECK(u2s::step(m2, true, 10, 16 * s, t) == u2s::Step::None);
    CHECK(u2s::step(m2, true, 10, 17 * s, t) == u2s::Step::WouldArm);
}

void test_who_and_tables() {
    CHECK(u2s::who_of(0xA0100001, 0xA0100001, 0xA010034E) == u2s::Who::Host);
    CHECK(u2s::who_of(0xA010034E, 0xA0100001, 0xA010034E) == u2s::Who::User2);
    CHECK(u2s::who_of(0xB0100003, 0xA0100001, 0xA010034E) == u2s::Who::Other);
    CHECK(u2s::who_of(0, 0xA0100001, 0xA010034E) == u2s::Who::Unknown);
    CHECK(u2s::who_of(0xA010034E, 0xA0100001, 0) == u2s::Who::Other);  // user 2 not known yet
    CHECK(u2s::timer_of(2043) == u2s::Timer::T2043);
    CHECK(u2s::timer_of(2045) == u2s::Timer::T2045);
    CHECK(u2s::timer_of(2046) == u2s::Timer::T2046);
    CHECK(u2s::timer_of(2556) == u2s::Timer::NotTimer);

    static u2s::RouteTable rt;
    rt.add(u2s::Timer::T2043, true, u2s::Who::Host);
    rt.add(u2s::Timer::T2043, true, u2s::Who::Host);
    rt.add(u2s::Timer::T2043, false, u2s::Who::Host);
    rt.add(u2s::Timer::T2045, true, u2s::Who::User2);
    CHECK(rt.get(u2s::Timer::T2043, true, u2s::Who::Host) == 2);
    CHECK(rt.get(u2s::Timer::T2043, false, u2s::Who::Host) == 1);
    CHECK(rt.get(u2s::Timer::T2045, true, u2s::Who::User2) == 1);
    CHECK(rt.get(u2s::Timer::T2046, true, u2s::Who::User2) == 0);

    // The key packing round trip.
    const uint64_t k = u2s::close_key(1, 19, u2s::Who::User2, 0x287D8D5);
    CHECK(k != 0);
    const u2s::CloseKey c = u2s::split_close_key(k);
    CHECK(c.tag == 1 && c.reason == 19 && c.owner == u2s::Who::User2 && c.ret_rva == 0x287D8D5);
    const u2s::CloseKey z = u2s::split_close_key(u2s::close_key(2, 0, u2s::Who::Host, 0));
    CHECK(z.tag == 2 && z.reason == 0 && z.owner == u2s::Who::Host && z.ret_rva == 0);
    CHECK(u2s::close_key(1, 30, u2s::Who::Host, 5) != u2s::close_key(1, 19, u2s::Who::Host, 5));
    CHECK(u2s::close_key(1, 30, u2s::Who::Host, 5) != u2s::close_key(2, 30, u2s::Who::Host, 5));

    static u2s::CountTable<8> t;  // at most 4 keys
    CHECK(!t.add(0));
    for (uint64_t i = 1; i <= 4; ++i) CHECK(t.add(i * 1000));
    CHECK(t.add(2000));  // an existing key still counts
    CHECK(!t.add(5000));  // a fifth key: overflow
    CHECK(t.overflow() == 1);
    u2s::CountTable<8>::Row rows[8];
    const size_t n = t.rows(rows, 8);
    CHECK(n == 4);
    uint64_t sum = 0, two = 0;
    for (size_t i = 0; i < n; ++i) {
        sum += rows[i].count;
        if (rows[i].key == 2000) two = rows[i].count;
    }
    CHECK(sum == 5 && two == 2);

    CHECK(u2s::rva_in(0x140000000 + 0x287D8D5, 0x140000000, 0x18000000) == 0x287D8D5);
    CHECK(u2s::rva_in(0x13FFFFFFF, 0x140000000, 0x18000000) == 0);
    CHECK(u2s::rva_in(0x140000000 + 0x18000000, 0x140000000, 0x18000000) == 0);
    CHECK(*u2s::reason_name(19) != 0);
    CHECK(*u2s::reason_name(77) == 0);
}

void test_fields_and_offsets() {
    CHECK(u2s::field_rel(0, 0) == u2s::FieldRel::Unknown);
    CHECK(u2s::field_rel(0x1000, 0) == u2s::FieldRel::Unknown);
    CHECK(u2s::field_rel(0x1000, 0x1000) == u2s::FieldRel::Same);
    CHECK(u2s::field_rel(0x1000, 0x2000) == u2s::FieldRel::Different);
    // The stage transform: scale at +0x90 (S28 read (1, 1) there), rotation 16 bytes, then the position.
    static_assert(u2s::kSt_Pos == u2s::kSt_Scale + 12 + 16);
    static_assert(u2s::kSt_Pos == 0xAC);
    // The published cells: (0, 0) is a valid cell and never packs to 0 ("unknown").
    CHECK(u2s::pack_cell({0, 0}) != 0);
    const u2s::Cell cs[] = {{0, 0}, {-168, -62}, {3200, -3200}, {-1, 1}};
    for (const u2s::Cell& c : cs) CHECK(u2s::unpack_cell(u2s::pack_cell(c)) == c);
}

void test_near() {
    // The host at cell (0,0), user 2 at (8,0): host stages at 1..4 cells around the host (as measured).
    const u2s::Cell host{0, 0}, u2{8, 0};
    const u2s::Cell cells[] = {{1, 0}, {2, 1}, {3, -2}, {4, 0}, {-3, 2}, {0, 4}};
    const u2s::Near r = u2s::near_counts(cells, 6, u2, host);
    CHECK(r.counted == 6);
    CHECK(r.within[0] == 0);  // <= 3 of user 2: none (the nearest is (4,0) at 4)
    CHECK(r.within[1] == 1);  // <= 4: (4,0)
    CHECK(r.within[2] == 2);  // <= 5: (4,0), (3,-2)
    CHECK(r.min_from == 1 && r.max_from == 4);
    const u2s::Near e = u2s::near_counts(cells, 0, u2, host);
    CHECK(e.counted == 0 && e.within[0] == 0 && e.max_from == 0);
}

// The review of c9d2e68: a burst of the host's closes (its fast travel, ~40 at once) must not eat user 2's lines; the
// direct S/F calls have their own class; what is refused is counted per class.
void test_budget() {
    CHECK(u2s::limit_class(0, false) == 0 && u2s::limit_class(0, true) == 1);
    CHECK(u2s::limit_class(5, false) == 10 && u2s::limit_class(6, true) == 13);
    CHECK(u2s::limit_class(5, true) != u2s::limit_class(6, true));  // timer vs direct: different classes
    static u2s::ClassBudget<16> b;
    constexpr uint64_t kWin = 5'000'000;
    const size_t host_close = u2s::limit_class(0, false), u2_close = u2s::limit_class(0, true);
    b.set(host_close, 64, 1500);
    b.set(u2_close, 256, 3);
    // 100 host closes in one window: 64 pass, 36 limited.
    uint64_t passed = 0;
    for (int i = 0; i < 100; ++i) passed += b.take_rate(host_close, 7 * kWin + 10 + i, kWin);
    CHECK(passed == 64);
    CHECK(b.limited(host_close) == 36);
    // User 2's closes in the same window still pass.
    CHECK(b.take_rate(u2_close, 7 * kWin + 200, kWin));
    CHECK(b.take_rate(u2_close, 7 * kWin + 201, kWin));
    CHECK(b.limited(u2_close) == 0);
    // A new window resets the host's class.
    CHECK(b.take_rate(host_close, 8 * kWin + 1, kWin));
    // Line caps per class: 3 for user 2's closes, then counted over.
    CHECK(b.take_line(u2_close) && b.take_line(u2_close) && b.take_line(u2_close));
    CHECK(!b.take_line(u2_close));
    CHECK(b.over(u2_close) == 1);
    CHECK(b.take_line(host_close));  // the host's class is not affected
    CHECK(b.over(host_close) == 0);
    // An unset class refuses everything; out of range is refused, never a crash.
    CHECK(!b.take_rate(7, kWin, kWin));
    CHECK(!b.take_line(7));
    CHECK(!b.take_rate(16, kWin, kWin) && !b.take_line(99) && b.limited(99) == 0 && b.over(99) == 0);
}

void test_u1() {
    using u2s::Element;
    CHECK(u2s::element_of(0x1000, 0, -1) == Element::Sequencer);
    CHECK(u2s::element_of(0x1000, 0x2000, 3) == Element::Sequencer);  // [elem+8] first, as 0x288A970
    CHECK(u2s::element_of(0, 0x2000, 3) == Element::Schedule);
    CHECK(u2s::element_of(0, 0, 3) == Element::Class2);
    CHECK(u2s::element_of(0, 0, -1) == Element::Empty);
    CHECK(u2s::skip_element(true, u2s::Mode::On, true, true, Element::Sequencer));
    CHECK(u2s::skip_element(true, u2s::Mode::On, true, true, Element::Class2));
    CHECK(!u2s::skip_element(true, u2s::Mode::On, true, true, Element::Schedule));
    CHECK(!u2s::skip_element(true, u2s::Mode::On, true, true, Element::Empty));
    CHECK(!u2s::skip_element(false, u2s::Mode::On, true, true, Element::Sequencer));  // the host's manager: never
    CHECK(!u2s::skip_element(true, u2s::Mode::Probe, true, true, Element::Sequencer));  // probe changes nothing
    CHECK(!u2s::skip_element(true, u2s::Mode::On, false, true, Element::Sequencer));  // A3: not armed
    CHECK(!u2s::skip_element(true, u2s::Mode::On, true, false, Element::Sequencer));  // A3: its own field

    using u2s::Route;
    CHECK(u2s::route_of(u2s::Mode::On, 2043, true) == Route::Own);  // always: armed or not (A1)
    CHECK(u2s::route_of(u2s::Mode::On, 2045, true) == Route::Own);
    CHECK(u2s::route_of(u2s::Mode::On, 2046, true) == Route::Own);
    CHECK(u2s::route_of(u2s::Mode::On, 2043, false) == Route::None);  // its own field: native already
    CHECK(u2s::route_of(u2s::Mode::On, 2283, true) == Route::None);   // not a timer
    CHECK(u2s::route_of(u2s::Mode::Probe, 2043, true) == Route::Would);
    CHECK(u2s::route_of(u2s::Mode::Off, 2043, true) == Route::None);
    CHECK(std::string(u2s::mode_name(u2s::Mode::On)) == "1" && std::string(u2s::mode_name(u2s::Mode::Probe)) == "probe");

    using u2s::Abort;
    const uint64_t s = 1'000'000;
    u2s::AbortIn a;
    a.now_us = 100 * s;
    CHECK(u2s::check_abort(a) == Abort::None);  // not armed: never
    a.armed = true;
    a.arm_done_us = 98 * s;
    a.mgr2_state = 2;
    a.last_mgr2_tick_us = 99 * s;
    CHECK(u2s::check_abort(a) == Abort::None);
    a.mgr2_state = 0;
    CHECK(u2s::check_abort(a) == Abort::None);  // < 5 s after the arm
    a.now_us = 104 * s;
    CHECK(u2s::check_abort(a) == Abort::NotRunning);
    a.mgr2_state = 2;
    a.now_us = 115 * s;
    CHECK(u2s::check_abort(a) == Abort::None);  // S33: no restart yet -> no abort at 15 s
    a.kick_us = 100 * s;                        // the chain restarted at 100 s
    a.now_us = 114 * s;
    CHECK(u2s::check_abort(a) == Abort::None);
    a.now_us = 115 * s;
    CHECK(u2s::check_abort(a) == Abort::NoTicks);  // still nothing 15 s after the restart
    a.last_mgr2_tick_us = 114 * s;
    CHECK(u2s::check_abort(a) == Abort::None);   // a tick after the restart
    a.kick_us = 0;
    a.last_mgr2_tick_us = 99 * s;
    a.now_us = 143 * s;
    CHECK(u2s::check_abort(a) == Abort::None);
    a.now_us = 144 * s;
    CHECK(u2s::check_abort(a) == Abort::NoTicks);  // no restart possible: 3 x 15 s
    a.last_mgr2_tick_us = 114 * s;
    a.now_us = 115 * s;
    // The host's manager out of state 2: a host load explains it (S28: 6 s from the 2437 to its 0x287EF60).
    a.host_not2_since_us = 100 * s;
    a.last_host_load_us = 99 * s;
    CHECK(u2s::check_abort(a) == Abort::None);  // 15 s, but the load was 16 s ago (and no preparation yet)
    a.last_host_load_us = 90 * s;
    CHECK(u2s::check_abort(a) == Abort::None);  // M2: still in its load (no preparation since)
    a.last_host_prep_us = 112 * s;
    CHECK(u2s::check_abort(a) == Abort::None);  // prepared 3 s ago: its client is acking
    a.last_host_prep_us = 104 * s;
    CHECK(u2s::check_abort(a) == Abort::HostTouched);  // 25 s since the load, prepared 11 s ago, 15 s out of state 2
    a.last_host_load_us = 0;
    a.last_host_prep_us = 0;
    CHECK(u2s::check_abort(a) == Abort::HostTouched);
    a.host_not2_since_us = 0;
    a.dup_far = 1;
    CHECK(u2s::check_abort(a) == Abort::Duplicate);
    a.dup_far = 0;
    a.unexpected_2322 = 1;
    CHECK(u2s::check_abort(a) == Abort::Unexpected2322);
    a.unexpected_2322 = 0;
    CHECK(u2s::check_abort(a) == Abort::None);
    CHECK(!u2s::cadence_changed(19, 13));  // max(19.5, 18) = 19: not above
    CHECK(u2s::cadence_changed(20, 13));   // M1: a WRN, never an abort
    CHECK(!u2s::cadence_changed(7, 2));    // baseline + 5
    CHECK(!u2s::cadence_changed(50, 0));   // no baseline yet
    for (size_t i = 0; i < static_cast<size_t>(Abort::Count); ++i) {
        CHECK(*u2s::abort_name(static_cast<Abort>(i)) != '?');
    }

    // The 2043 payload, byte for byte as 0x287F0CC..0x287F0EF builds it.
    const uint8_t cell[8] = {};
    uint8_t p[u2s::kTimerPayload] = {};
    u2s::timer_2043_payload(p, cell);
    const uint8_t want[u2s::kTimerPayload] = {0xFB, 0x07, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    for (size_t i = 0; i < u2s::kTimerPayload; ++i) CHECK(p[i] == want[i]);
    CHECK(u2s::timer_2043_delay_ms(460) == 4600);
    CHECK(u2s::timer_2043_delay_ms(0) == 0);
    CHECK(u2s::timer_2043_delay_ms(-3) == 0);
    // The 53d70df bug: the live raw value 10 is EnqueueTimer's delay 100 (100 ms units), a ~10 s period.
    CHECK(u2s::timer_2043_delay_ms(10) == 100);
    CHECK(u2s::timer_2043_period_ms(10) == 10000);
    CHECK(u2s::timer_2043_period_ms(100) == 100000);
    CHECK(u2s::timer_2043_period_ms(0) == 0 && u2s::timer_2043_period_ms(-1) == 0);
    CHECK(u2s::chain_dead(100 * s, 0, 4600));          // never ticked
    // No period read / implausible (< 2 s): the 15 s / 3-period fallback.
    CHECK(!u2s::chain_dead(100 * s, 90 * s, 0));
    CHECK(!u2s::chain_dead(100 * s, 90 * s, 100));     // the 53d70df misreading (100 'ms'): not dead at 10 s
    CHECK(u2s::chain_dead(106 * s, 90 * s, 100));      // 16 s
    // S33 (period 10.1 s): its last rescheduling tick 11.4 s before the arm -> alive (< 15.15 s); 16 s -> dead.
    CHECK(!u2s::chain_dead(111'400'000, 100 * s, 10100));
    CHECK(u2s::chain_dead(116 * s, 100 * s, 10100));
    CHECK(u2s::chain_dead(100 * s, 85 * s, 10000));    // 15 s >= 1.5 x 10 s
    CHECK(!u2s::chain_dead(100 * s, 86 * s, 10000));   // 14 s
    // S33: after the arm, the restart is due 1.5 periods after the arm / its last tick, once.
    CHECK(!u2s::chain_kick_due(100 * s, 0, 0, 0, 10100));                 // not armed
    CHECK(!u2s::chain_kick_due(115 * s, 100 * s, 0, 0, 10100));           // 15 s < 15.15 s
    CHECK(u2s::chain_kick_due(116 * s, 100 * s, 0, 0, 10100));
    CHECK(!u2s::chain_kick_due(116 * s, 100 * s, 110 * s, 0, 10100));     // a tick at 110 s
    CHECK(u2s::chain_kick_due(126 * s, 100 * s, 110 * s, 0, 10100));
    CHECK(!u2s::chain_kick_due(500 * s, 100 * s, 110 * s, 1, 10100));     // once per arm, whatever the ticks
    CHECK(!u2s::chain_kick_due(500 * s, 100 * s, 0, 0, 0));               // no period read
    CHECK(!u2s::chain_kick_due(500 * s, 100 * s, 0, 0, 100));             // the 53d70df misreading: never
    CHECK(u2s::chain_kick_due(116 * s, 100 * s, 0, 0, u2s::timer_2043_period_ms(10)));  // raw 10 -> 10 s
}

// §8.16: the host's own 2043 chains (log only), with the S30 / one-PC logs' shapes: period 10 s, ticks 10.1 s apart.
void test_host_chains() {
    constexpr uint64_t ms = 1'000;
    constexpr uint64_t s = 1'000'000;
    constexpr uint64_t P = 10 * s;
    const uint64_t t0 = 1000 * s;

    // The baseline gate: complete minutes >= 60 s after the host's last preparation or owner load.
    CHECK(u2s::baseline_minute_ok(t0, 0, 0));                    // neither seen yet
    CHECK(!u2s::baseline_minute_ok(t0, t0 - 30 * s, 0));         // the login's minute
    CHECK(u2s::baseline_minute_ok(t0, t0 - 60 * s, 0));
    CHECK(!u2s::baseline_minute_ok(t0, t0 - 90 * s, t0 - 10 * s));  // a later owner load counts too
    CHECK(u2s::baseline_minute_ok(t0, t0 - 90 * s, t0 - 61 * s));

    // Nothing, or no period.
    CHECK(u2s::count_chains(nullptr, 0, P, t0).alive == 0);
    const uint64_t one[] = {t0};
    CHECK(u2s::count_chains(one, 1, 0, t0).alive == 0);
    CHECK(u2s::count_chains(one, 1, P, t0).alive == 1);

    // One chain, 10.1 s apart and one 10.4 s hop (guardfix 21:40:10 -> 21:40:20.7): 1.
    uint64_t a[8];
    uint64_t t = t0;
    for (size_t i = 0; i < 8; ++i) {
        a[i] = t;
        t += (i == 2 ? 10'400 : 10'100) * ms;
    }
    auto c = u2s::count_chains(a, 8, P, a[7] + 2 * s);
    CHECK(c.alive == 1 && c.seen == 1);

    // Two chains 0.46 s apart (guardfix) and 0.87 s apart (S30 162816): 2, never merged.
    for (const uint64_t off : {460 * ms, 870 * ms, 50 * ms}) {
        uint64_t b[12];
        for (size_t i = 0; i < 6; ++i) {
            b[2 * i] = t0 + i * 10'100 * ms;
            b[2 * i + 1] = t0 + off + i * 10'100 * ms;
        }
        c = u2s::count_chains(b, 12, P, b[11] + 1 * s);
        CHECK(c.alive == 2 && c.seen == 2);
    }

    // A chain that stops: alive until period + slack (max(1 s, P / 5) = 2 s) after its last tick.
    c = u2s::count_chains(a, 8, P, a[7] + 12 * s);
    CHECK(c.alive == 1);
    c = u2s::count_chains(a, 8, P, a[7] + 12 * s + 1);
    CHECK(c.alive == 0 && c.seen == 1);

    // The login (two chains), then a load with a 10 s state-1 window (both end) and its preparation (one new chain).
    uint64_t d[16];
    size_t n = 0;
    for (size_t i = 0; i < 4; ++i) {
        d[n++] = t0 + i * 10'100 * ms;
        d[n++] = t0 + 870 * ms + i * 10'100 * ms;
    }
    const uint64_t prep = d[n - 1] + 15 * s;
    for (size_t i = 1; i <= 6; ++i) d[n++] = prep + i * 10'100 * ms;
    c = u2s::count_chains(d, n, P, d[n - 1] + 1 * s);
    CHECK(c.alive == 1 && c.seen == 3);
    // Before the load: still two.
    c = u2s::count_chains(d, 8, P, d[7] + 1 * s);
    CHECK(c.alive == 2);
}

// S31 (u0-resultados-s28.md §8.17): U-e, the controller at birth.
void test_npc_control() {
    // S31's NPC: born in user 2's 2045, NPCSchedule descriptor, eax = the host's user key 0x90100000 = [field+0x180].
    u2s::CtlIn in;
    in.opcode = 2045;
    in.own_mgr_troc = true;
    in.armed = true;
    in.desc_npc_schedule = true;
    in.eax = 0x90100000u;
    in.field_owner = 0x90100000u;
    in.host_field = true;
    in.type = 3;
    in.key2 = 0x9010006Eu;
    in.session_key2 = 0x9010006Eu;
    CHECK(u2s::ctl_verdict(in) == u2s::Ctl::Assign);
    auto t = in;
    t.type = 6;  // type 3 only until type 6 is measured (the 6c12c82 crash review)
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::Type);
    t.type = 3;
    t.opcode = 2043;
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::Assign);
    t.opcode = 2046;
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::Assign);
    t.session_key2 = 0;  // unknown: not compared
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::Assign);
    // Each condition, in order.
    t = in;
    t.opcode = 2550;  // its road spawns: not U-e's
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::NotTimer);
    t = in;
    t.own_mgr_troc = false;  // review 6: a Troc whose vf48 gave the HOST's manager (the host's stages)
    t.aborted = true;
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::NotOwnTroc);
    t = in;
    t.aborted = true;
    t.armed = false;
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::Aborted);
    t = in;
    t.armed = false;
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::NotArmed);
    t = in;
    t.desc_npc_schedule = false;
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::OtherDesc);
    t = in;
    t.old_ctl = 0x90100000u;
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::HasController);
    t = in;
    t.desc_user = 0x1234;
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::DescUser);
    t = in;
    t.parent = 0x1234;
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::HasParent);
    t = in;
    t.host_field = false;
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::NotHostField);
    t = in;
    t.eax = 0x9010006Eu;  // another branch gave it
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::NotOwnerBranch);
    t = in;
    t.eax = t.field_owner = 0;
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::NotOwnerBranch);
    t = in;
    t.type = 7;  // a gimmick
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::Type);
    t = in;
    t.key2 = 0;
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::NoKey);
    t = in;
    t.session_key2 = 0x90100071u;
    CHECK(u2s::ctl_verdict(t) == u2s::Ctl::KeyMismatch);
    for (size_t i = 0; i < static_cast<size_t>(u2s::Ctl::Count); ++i) {
        CHECK(std::string(u2s::ctl_name(static_cast<u2s::Ctl>(i))) != "?");
    }
    static_assert(u2s::kVtDescNpcSchedule == 0x5B04CB0 && u2s::kActor_Ctl == 0x90 && u2s::kDesc_User == 0x40 &&
                  u2s::kLogoutAiReason == 42);
}

void test_key_table() {
    static u2s::KeyTable<16> t;  // 12 keys at most (3/4)
    const uint64_t s = 1'000'000;
    CHECK(!t.put(0, u2s::kKeyAssigned, 1));  // 0 is never a key
    CHECK(t.put(0xB010060Cu, u2s::kKeyAssigned, 100 * s));
    CHECK(t.flags(0xB010060Cu) == u2s::kKeyAssigned);
    CHECK(t.flags(0xB010060Du) == 0);
    uint16_t before = 0xFFFF;
    CHECK(t.mark(0xB010060Cu, u2s::kKeyDriven, 101 * s, &before) && before == u2s::kKeyAssigned);
    CHECK(!t.mark(0xB010060Du, u2s::kKeyDriven, 101 * s));  // not tracked
    CHECK(t.mark(0xB010060Cu, u2s::kKeyRemoved | u2s::kKeyR42, 105 * s, &before));
    CHECK(!(before & u2s::kKeyRemoved));
    CHECK(t.mark(0xB010060Cu, u2s::kKeyRemoved, 200 * s));  // the first removal's time stays
    u2s::KeyTable<16>::View v;
    bool found = false;
    for (size_t i = 0; i < 16; ++i) {
        if (t.get(i, v) && v.key == 0xB010060Cu) {
            found = true;
            CHECK(v.removed_us == 105 * s && v.put_us == 100 * s);
            CHECK(v.flags == (u2s::kKeyAssigned | u2s::kKeyDriven | u2s::kKeyRemoved | u2s::kKeyR42));
        }
    }
    CHECK(found);
    // A reused key starts over.
    CHECK(t.put(0xB010060Cu, u2s::kKeyWould, 300 * s));
    CHECK(t.flags(0xB010060Cu) == u2s::kKeyWould);
    CHECK(t.used() == 1);
    // S43: the stage slot is given with put (0 without one), kept by mark, replaced by the next put of the key; an
    // untracked key has none.
    CHECK(t.slot_of(0xB010060Cu) == 0);
    CHECK(t.put(0xB010060Cu, u2s::kKeyAssigned, 301 * s, false, 0x1234u));
    CHECK(t.slot_of(0xB010060Cu) == 0x1234u);
    CHECK(t.mark(0xB010060Cu, u2s::kKeyRemoved | u2s::kKeyR42, 302 * s));
    CHECK(t.mark(0xB010060Cu, u2s::kKeyDriven, 303 * s));
    CHECK(t.slot_of(0xB010060Cu) == 0x1234u);
    CHECK(t.put(0xB010060Cu, u2s::kKeyWould, 304 * s, false, 0x5678u));
    CHECK(t.slot_of(0xB010060Cu) == 0x5678u && t.flags(0xB010060Cu) == u2s::kKeyWould);
    CHECK(t.slot_of(0xB010060Du) == 0);
    CHECK(t.used() == 1);
    // Full at 12 of 16: the 13th is overflow.
    for (uint32_t k = 1; k < 12; ++k) CHECK(t.put(0xB0100000u + k, u2s::kKeyAssigned, 1));
    CHECK(t.used() == 12);
    CHECK(!t.put(0xB0100100u, u2s::kKeyAssigned, 1));
    CHECK(t.overflow() == 1);
    CHECK(t.put(0xB0100001u, u2s::kKeyAssigned, 2));  // an existing key still refreshes
    // Review 4: a removed key's slot is taken over 30 s after its removal, never before, and never leaving a hole:
    // a new key whose probe chain meets a removed slot before an empty one takes it (the others are not tracked).
    static u2s::KeyTable<16> r;
    for (uint32_t k = 1; k <= 12; ++k) CHECK(r.put(0xB0300000u + k, u2s::kKeyWould, 1, false, 0x300u + k));
    for (uint32_t k = 1; k <= 12; ++k) CHECK(r.mark(0xB0300000u + k, u2s::kKeyRemoved, 400 * s));
    int early = 0, ok = 0;
    for (uint32_t i = 0; i < 16; ++i) early += r.put(0xB0400000u + i, u2s::kKeyAssigned, 429 * s) ? 1 : 0;
    CHECK(early == 0);
    uint32_t taken[16] = {};
    for (uint32_t i = 0; i < 16; ++i) {
        if (r.put(0xB0500000u + i, u2s::kKeyAssigned, 430 * s, false, 0x500u + i)) taken[ok++] = 0xB0500000u + i;
    }
    CHECK(ok >= 6 && ok <= 12);
    CHECK(r.reused() == static_cast<uint64_t>(ok) && r.used() == 12);
    for (int i = 0; i < ok; ++i) CHECK(r.flags(taken[i]) == u2s::kKeyAssigned);
    // S43: a key that took a removed key's slot over carries its own stage slot, not the old key's.
    for (int i = 0; i < ok; ++i) CHECK(r.slot_of(taken[i]) == 0x500u + (taken[i] - 0xB0500000u));
    int old_left = 0;
    for (uint32_t k = 1; k <= 12; ++k) {
        if (r.flags(0xB0300000u + k)) {
            ++old_left;
            CHECK(r.slot_of(0xB0300000u + k) == 0x300u + k);
        }
    }
    CHECK(old_left == 12 - ok);
}

// S43: the last reason-42 logout of each stage slot; a repeat is another one in the same slot within 60 s.
void test_r42_slot_table() {
    const uint64_t s = 1'000'000;
    static_assert(u2s::kR42LoopWindowUs == 60'000'000);
    // The slot id: 0 only without schedule data; distinct (data, node) pairs give distinct ids (here).
    CHECK(u2s::r42_slot_id(0, 0) == 0 && u2s::r42_slot_id(0, 0x2C) == 0);
    CHECK(u2s::r42_slot_id(0x50BC12F0u, 0x2C) != 0);
    CHECK(u2s::r42_slot_id(0x50BC12F0u, 0x2C) == u2s::r42_slot_id(0x50BC12F0u, 0x2C));
    CHECK(u2s::r42_slot_id(0x50BC12F0u, 0x2C) != u2s::r42_slot_id(0x50BC12F0u, 0x1C));
    CHECK(u2s::r42_slot_id(0x50BC12F0u, 0x2C) != u2s::r42_slot_id(0x3537D7CEu, 0x2C));
    static u2s::R42SlotTable<64> t;
    const uint32_t a = u2s::r42_slot_id(0x50BC12F0u, 0x2C), b = u2s::r42_slot_id(0x50BC12F0u, 0x1C);
    const uint64_t t0 = 1000 * s;
    CHECK(!t.repeat(a, t0));                  // the slot's first
    CHECK(!t.repeat(b, t0 + s));              // another slot: not a repeat
    CHECK(t.repeat(a, t0 + 5'800'000));       // put back and gone again 5.8 s later: the loop
    CHECK(t.repeat(a, t0 + 65'800'000));      // exactly 60 s after the last one: still a repeat
    CHECK(!t.repeat(a, t0 + 125'800'001));    // more than 60 s after the last one: a first again
    CHECK(!t.repeat(b, t0 + 200 * s));        // b's last was 199 s ago: stale, a first
    CHECK(t.firsts() == 4 && t.used() == 2 && t.overflow() == 0 && t.unknown() == 0);
    // P2-1: within the window, but put before the slot's last logout (b's last: t0 + 200 s): not a repeat.
    CHECK(!t.repeat(b, t0 + 201 * s, u2s::kR42LoopWindowUs, t0 + 150 * s));
    CHECK(t.concurrent() == 1);
    CHECK(t.repeat(b, t0 + 202 * s, u2s::kR42LoopWindowUs, t0 + 201 * s + 1));  // put after it: the loop
    // An unknown slot is always a repeat (fails closed) and never enters the table.
    CHECK(t.repeat(0, t0));
    CHECK(t.repeat(0, t0 + 500 * s));
    CHECK(t.unknown() == 2 && t.used() == 2);
    // Full: 16 entries, all used within the window; a new slot is a repeat (counted as overflow), an existing one
    // still works; once the window passes, a stale entry is taken over.
    static u2s::R42SlotTable<16> f;
    for (uint32_t k = 1; k <= 16; ++k) CHECK(!f.repeat(u2s::r42_slot_id(0x3537D7CEu, 4 * k), t0));
    CHECK(f.used() == 16);
    const uint32_t extra = u2s::r42_slot_id(0x59D5C885u, 8);
    CHECK(f.repeat(extra, t0 + s));
    CHECK(f.overflow() == 1);
    CHECK(f.repeat(u2s::r42_slot_id(0x3537D7CEu, 4), t0 + 2 * s));  // tracked: a real repeat, no overflow
    CHECK(f.overflow() == 1);
    CHECK(!f.repeat(extra, t0 + 61 * s));  // 15 of the 16 are stale now: one is taken over
    CHECK(f.overflow() == 1 && f.used() == 16);
    CHECK(f.repeat(extra, t0 + 62 * s));   // and it is found again
    CHECK(f.repeat(u2s::r42_slot_id(0x3537D7CEu, 4), t0 + 62 * s));  // the one renewed at t0+2 s: still there
}

// S43: what ctl_on_logout does with a server Logout of reason 42 (user2_stages.cpp): mark the key (a second reason-42
// logout of a key is not counted again), count it raw, and as a repeat when its stage slot (given with its put) had
// another one within 60 s or is unknown.
struct R42Sim {
    u2s::KeyTable<256> keys;
    u2s::R42SlotTable<256> slots;
    uint32_t raw = 0, loop = 0, next = 0xB0100000u, last = 0;
    // An NPC given to user 2 in `slot` (a new key each life, as the stage's respawn), logged out with reason 42.
    void life(uint32_t slot, uint64_t born_us, uint64_t gone_us) {
        last = ++next;
        CHECK(keys.put(last, u2s::kKeyAssigned, born_us, false, slot));
        logout(last, gone_us);
    }
    void again(uint64_t now_us) { logout(last, now_us); }
    void logout(uint32_t key, uint64_t now_us) {
        uint16_t before = 0;
        if (!keys.mark(key, u2s::kKeyRemoved | u2s::kKeyR42, now_us, &before) || (before & u2s::kKeyR42)) return;
        ++raw;
        if (slots.repeat(keys.slot_of(key), now_us, u2s::kR42LoopWindowUs, keys.put_time(key))) ++loop;
    }
};

void test_ctl_stats_and_abort() {
    const uint64_t s = 1'000'000;
    const uint64_t now = 1000 * s;
    static u2s::KeyTable<64> t;
    // 10 keys given 30 s ago, alive; 4 driven.
    for (uint32_t k = 0; k < 10; ++k) {
        t.put(0xB0100000u + k, u2s::kKeyAssigned, now - 30 * s);
        if (k < 4) t.mark(0xB0100000u + k, u2s::kKeyDriven, now - 29 * s);
    }
    // One probe key (another kind: not counted with kKeyAssigned).
    t.put(0xB0100100u, u2s::kKeyWould, now - 30 * s);
    // One given 5 s ago (too young), one given 200 s ago (too old), one removed 2 s after its birth (did not live).
    t.put(0xB0100200u, u2s::kKeyAssigned, now - 5 * s);
    t.put(0xB0100201u, u2s::kKeyAssigned, now - 200 * s);
    t.put(0xB0100202u, u2s::kKeyAssigned, now - 30 * s);
    t.mark(0xB0100202u, u2s::kKeyRemoved | u2s::kKeyR42, now - 28 * s);
    u2s::CtlStats st = u2s::ctl_stats(t, u2s::kKeyAssigned, now, 0);
    CHECK(st.tracked == 13 && st.alive == 12 && st.removed == 1 && st.r42 == 1 && st.driven == 4);
    CHECK(st.eligible == 10 && st.eligible_driven == 4);
    CHECK(st.left_after_drain == 0);
    u2s::CtlAbortIn in;
    in.s = st;
    CHECK(u2s::ctl_undriven(st));                       // 4 of 10 < half: a WRN line (review 7)
    CHECK(u2s::ctl_abort(in) == u2s::CtlAbort::None);   // never an abort
    // A drain 10 s ago: the 12 alive keys given before it are left.
    st = u2s::ctl_stats(t, u2s::kKeyAssigned, now, now - 10 * s);
    CHECK(st.left_after_drain == 11);  // all alive except the one given 5 s ago (after the drain)
    // With enough driven: no undriven abort; the drain check only once asked.
    for (uint32_t k = 4; k < 6; ++k) t.mark(0xB0100000u + k, u2s::kKeyDriven, now - 29 * s);
    st = u2s::ctl_stats(t, u2s::kKeyAssigned, now, 0);
    CHECK(st.eligible_driven == 6);
    in = {};
    in.s = st;
    CHECK(u2s::ctl_abort(in) == u2s::CtlAbort::None);
    in.s.left_after_drain = 3;
    CHECK(u2s::ctl_abort(in) == u2s::CtlAbort::None);  // not a drain check
    in.drain_checked = true;
    CHECK(u2s::ctl_abort(in) == u2s::CtlAbort::LeftAfterDrain);
    // Fewer than 8 eligible: never undriven.
    u2s::CtlStats u;
    u.eligible = 7;
    CHECK(!u2s::ctl_undriven(u));
    u.eligible = 8;
    u.eligible_driven = 4;
    CHECK(!u2s::ctl_undriven(u));  // exactly half
    u.eligible_driven = 3;
    CHECK(u2s::ctl_undriven(u));
    in = {};
    in.s = u;
    CHECK(u2s::ctl_abort(in) == u2s::CtlAbort::None);
    // The order: a wrong 2005 first, then reason 42 (> 10 same-slot repeats a minute), then the host driving.
    in = {};
    in.r42_loop_last_minute = 10;
    CHECK(u2s::ctl_abort(in) == u2s::CtlAbort::None);
    in.r42_raw_last_minute = 500;  // S43: the raw count never aborts
    CHECK(u2s::ctl_abort(in) == u2s::CtlAbort::None);
    in.r42_loop_last_minute = 11;
    CHECK(u2s::ctl_abort(in) == u2s::CtlAbort::Reason42);
    in.s.bad_2005 = 1;
    CHECK(u2s::ctl_abort(in) == u2s::CtlAbort::Bad2005);
    // S43 (docs/research/s25/s43-datos/motivo42-informe.md), the reason-42 logouts as ctl_on_logout counts them (raw,
    // and repeats of a stage slot).
    {
        // 15-10 (p8-2inst-s42-probe host.log:57320, the false positive): 5 NPCs of 0x59D5C885 / 0xDFA491AB go at
        // 17:09:29-55; then a native group, 24 of 0x3537D7CE (nodes 0..92), leaves in under a second at 17:11:14 and 8 of
        // 0x50BC12F0 (nodes 8..44) at 17:11:17. No slot is put back: no repeat, no abort.
        static R42Sim today;
        const uint64_t t0 = 148718 * s;
        today.life(u2s::r42_slot_id(0x59D5C885u, 0), t0 - 28 * s, t0);
        today.life(u2s::r42_slot_id(0x59D5C885u, 8), t0 - 28 * s, t0 + 3 * s);
        today.life(u2s::r42_slot_id(0x59D5C885u, 4), t0 - 28 * s, t0 + 23 * s);
        today.life(u2s::r42_slot_id(0xDFA491ABu, 0), t0 + 13 * s, t0 + 25 * s);
        today.life(u2s::r42_slot_id(0xDFA491ABu, 8), t0 + 13 * s, t0 + 26 * s);
        for (uint32_t n = 0; n < 24; ++n) {
            today.life(u2s::r42_slot_id(0x3537D7CEu, 4 * n), t0 + 96 * s, t0 + 105 * s + n * 30'000);
        }
        const uint32_t nodes[8] = {8, 12, 16, 20, 28, 32, 36, 44};
        for (uint32_t n : nodes) today.life(u2s::r42_slot_id(0x50BC12F0u, n), t0 + 93 * s, t0 + 108 * s + n * 1000);
        CHECK(today.raw == 37 && today.loop == 0);
        u2s::CtlAbortIn ti;
        ti.r42_raw_last_minute = 32;  // >= 14 of them in the last minute (the old guard's 11)
        ti.r42_loop_last_minute = today.loop;
        CHECK(u2s::ctl_abort(ti) == u2s::CtlAbort::None);
        ti.s.bad_2005 = 1;
        CHECK(u2s::ctl_abort(ti) == u2s::CtlAbort::Bad2005);
    }
    {
        // S31 / S34's loop: one slot put back with a new key every 5.8 s and logged out with reason 42 each life.
        static R42Sim loop;
        const uint32_t slot = u2s::r42_slot_id(0x50BC12F0u, 0x2C);
        const uint64_t t0 = 2000 * s;
        for (uint32_t i = 0; i < 11; ++i) loop.life(slot, t0 + i * 5'800'000, t0 + i * 5'800'000 + 5'200'000);
        CHECK(loop.raw == 11 && loop.loop == 10);
        u2s::CtlAbortIn li;
        li.r42_raw_last_minute = loop.raw;
        li.r42_loop_last_minute = loop.loop;
        CHECK(u2s::ctl_abort(li) == u2s::CtlAbort::None);
        loop.life(slot, t0 + 11 * 5'800'000, t0 + 11 * 5'800'000 + 5'200'000);
        CHECK(loop.raw == 12 && loop.loop == 11);
        li.r42_raw_last_minute = loop.raw;
        li.r42_loop_last_minute = loop.loop;
        CHECK(u2s::ctl_abort(li) == u2s::CtlAbort::Reason42);
        li.s.bad_2005 = 1;  // a wrong 2005 still comes first
        CHECK(u2s::ctl_abort(li) == u2s::CtlAbort::Bad2005);
    }
    {
        // A mix: 20 distinct slots once, and 3 slots put back 5 + 4 + 5 times (4 + 3 + 4 = 11 repeats): abort.
        static R42Sim mix;
        const uint64_t t0 = 3000 * s;
        for (uint32_t n = 0; n < 20; ++n) mix.life(u2s::r42_slot_id(0x3537D7CEu, 4 * n), t0, t0 + 10 * s + n * 1000);
        const uint32_t lives[3] = {5, 4, 5};
        for (uint32_t j = 0; j < 3; ++j) {
            for (uint32_t i = 0; i < lives[j]; ++i) {
                const uint64_t born = t0 + j * s + i * 5'800'000;
                mix.life(u2s::r42_slot_id(0xA0F3AE60u, j), born, born + 5'200'000);
            }
        }
        CHECK(mix.raw == 34 && mix.loop == 11);
        u2s::CtlAbortIn mi;
        mi.r42_raw_last_minute = mix.raw;
        mi.r42_loop_last_minute = mix.loop;
        CHECK(u2s::ctl_abort(mi) == u2s::CtlAbort::Reason42);
    }
    {
        // Review of a4c2661 (P2-1): two instances of the same stage slot, both put BEFORE either reason-42 logout (a
        // schedule ending), leave together: not the loop. 12 such pairs: 0 repeats, no abort.
        static R42Sim twins;
        const uint64_t t0 = 3500 * s;
        for (uint32_t n = 0; n < 12; ++n) {
            const uint32_t slot = u2s::r42_slot_id(0xA0F3AE60u, 4 * n);
            const uint32_t k1 = ++twins.next, k2 = ++twins.next;
            CHECK(twins.keys.put(k1, u2s::kKeyAssigned, t0, false, slot));
            CHECK(twins.keys.put(k2, u2s::kKeyAssigned, t0 + 100'000, false, slot));
            twins.logout(k1, t0 + 20 * s + n * 1000);
            twins.logout(k2, t0 + 20 * s + n * 1000 + 500);
        }
        CHECK(twins.raw == 24 && twins.loop == 0 && twins.slots.concurrent() == 12);
        u2s::CtlAbortIn wi;
        wi.r42_raw_last_minute = twins.raw;
        wi.r42_loop_last_minute = twins.loop;
        CHECK(u2s::ctl_abort(wi) == u2s::CtlAbort::None);
        // The real loop with a known birth: each new key put after the slot's previous logout (every 5.8 s) still counts.
        const uint32_t slot = u2s::r42_slot_id(0x50BC12F0u, 0x30);
        for (uint32_t i = 0; i < 12; ++i) twins.life(slot, t0 + 30 * s + i * 5'800'000, t0 + 30 * s + i * 5'800'000 + 5'200'000);
        CHECK(twins.loop == 11);
        wi.r42_loop_last_minute = twins.loop;
        CHECK(u2s::ctl_abort(wi) == u2s::CtlAbort::Reason42);
    }
    {
        // Unknown slots (no schedule handle) fail closed: 11 reason-42 logouts with no slot abort, as before S43.
        static R42Sim unknown;
        const uint64_t t0 = 4000 * s;
        for (uint32_t i = 0; i < 10; ++i) unknown.life(0, t0, t0 + 10 * s + i * s);
        CHECK(unknown.raw == 10 && unknown.loop == 10);
        u2s::CtlAbortIn ui;
        ui.r42_loop_last_minute = unknown.loop;
        CHECK(u2s::ctl_abort(ui) == u2s::CtlAbort::None);
        unknown.life(0, t0, t0 + 20 * s);
        ui.r42_loop_last_minute = unknown.loop;
        CHECK(unknown.loop == 11 && u2s::ctl_abort(ui) == u2s::CtlAbort::Reason42);
        // A second reason-42 logout of the same key is never counted again (kKeyR42).
        unknown.again(t0 + 21 * s);
        CHECK(unknown.raw == 11 && unknown.loop == 11);
    }
    in = {};
    in.s.host_req = 1;
    CHECK(u2s::ctl_abort(in) == u2s::CtlAbort::HostDrives);
    // The 6c12c82 crash review: an assigned NPC alive 5 s with no accepted StartActionReq (only with the hook in).
    {
        static u2s::KeyTable<64> a;
        a.put(0xB0100700u, u2s::kKeyAssigned, now - 6 * s);
        a.put(0xB0100701u, u2s::kKeyAssigned, now - 6 * s);
        a.mark(0xB0100701u, u2s::kKeyAccepted, now - 5 * s);
        a.put(0xB0100702u, u2s::kKeyAssigned, now - 4 * s);  // too young yet
        a.put(0xB0100703u, u2s::kKeyAssigned, now - 9 * s);
        a.mark(0xB0100703u, u2s::kKeyRemoved, now - 8 * s);  // gone: not counted
        const u2s::CtlStats as = u2s::ctl_stats(a, u2s::kKeyAssigned, now, 0);
        CHECK(as.unaccepted == 1 && as.accepted == 1);
        u2s::CtlAbortIn ai;
        ai.s = as;
        CHECK(u2s::ctl_abort(ai) == u2s::CtlAbort::None);  // child_hold's hook not in: never seen, never an abort
        ai.accept_tracked = true;
        CHECK(u2s::ctl_abort(ai) == u2s::CtlAbort::Unaccepted);
        ai.s.bad_2005 = 1;
        CHECK(u2s::ctl_abort(ai) == u2s::CtlAbort::Bad2005);
    }
    // S33 (14-10, 15:42): a unit abandoned by npc_activation=1 stays alive and unaccepted; it is never counted as
    // 'unaccepted', so even with the old net back (accept_tracked) it cannot trigger the Unaccepted abort by itself.
    {
        static u2s::KeyTable<64> a;
        a.put(0xB01008E1u, u2s::kKeyAssigned, now - 36 * s);
        a.mark(0xB01008E1u, u2s::kKeyAbandoned, now - 1 * s);
        u2s::CtlStats as = u2s::ctl_stats(a, u2s::kKeyAssigned, now, 0);
        CHECK(as.unaccepted == 0 && as.abandoned == 1 && as.alive == 1);
        u2s::CtlAbortIn ai;
        ai.s = as;
        ai.accept_tracked = true;
        CHECK(u2s::ctl_abort(ai) == u2s::CtlAbort::None);
        // Another unaccepted key still triggers it (the old net keeps working for the units not abandoned).
        a.put(0xB01008E2u, u2s::kKeyAssigned, now - 6 * s);
        as = u2s::ctl_stats(a, u2s::kKeyAssigned, now, 0);
        CHECK(as.unaccepted == 1 && as.abandoned == 1);
        ai.s = as;
        CHECK(u2s::ctl_abort(ai) == u2s::CtlAbort::Unaccepted);
        // A reused key (put again: a new incarnation) starts over, without the abandoned bit; gone ones count nowhere.
        a.put(0xB01008E1u, u2s::kKeyAssigned, now - 6 * s);
        CHECK(!(a.flags(0xB01008E1u) & u2s::kKeyAbandoned));
        a.mark(0xB01008E2u, u2s::kKeyAbandoned | u2s::kKeyRemoved, now);
        as = u2s::ctl_stats(a, u2s::kKeyAssigned, now, 0);
        CHECK(as.unaccepted == 1 && as.abandoned == 0 && as.removed == 1);
    }
    for (size_t i = 0; i < static_cast<size_t>(u2s::CtlAbort::Count); ++i) {
        CHECK(std::string(u2s::ctl_abort_name(static_cast<u2s::CtlAbort>(i))) != "?");
    }
    CHECK(u2s::ai_req(2556) && u2s::ai_req(2223) && u2s::ai_req(2734) && u2s::ai_req(2405) && u2s::ai_req(2291) &&
          u2s::ai_req(2828));
    CHECK(!u2s::ai_req(2005) && !u2s::ai_req(2380));
}

void test_2005_walk() {
    // Two records (S31's shape: type 3, key 0xB010060C, controller 0x90100000 at +0x12; a type-1 0xB0100001 with user
    // 2's key), then a length that does not fit.
    uint8_t p[2 + 0x20 + 2 + 0x18 + 2] = {};
    size_t o = 0;
    p[o++] = 0x20;
    p[o++] = 0;
    uint8_t* r = p + o;
    r[0] = 3;
    r[1] = 0x0C; r[2] = 0x06; r[3] = 0x10; r[4] = 0xB0;
    r[0x12] = 0x00; r[0x13] = 0x00; r[0x14] = 0x10; r[0x15] = 0x90;
    o += 0x20;
    p[o++] = 0x18;
    p[o++] = 0;
    r = p + o;
    r[0] = 1;
    r[1] = 0x01; r[2] = 0x00; r[3] = 0x10; r[4] = 0xB0;
    r[0x12] = 0x6E; r[0x13] = 0x00; r[0x14] = 0x10; r[0x15] = 0x90;
    o += 0x18;
    p[o++] = 0xFF;  // 255 bytes: does not fit
    p[o++] = 0;
    uint32_t keys[4] = {}, ctls[4] = {};
    uint8_t types[4] = {};
    size_t n = 0;
    const size_t k = u2s::for_each_2005(p, sizeof p, [&](uint8_t ty, uint32_t key, uint32_t ctl) {
        if (n < 4) {
            types[n] = ty;
            keys[n] = key;
            ctls[n] = ctl;
        }
        ++n;
    });
    CHECK(k == 2 && n == 2);
    CHECK(types[0] == 3 && keys[0] == 0xB010060Cu && ctls[0] == 0x90100000u);
    CHECK(types[1] == 1 && keys[1] == 0xB0100001u && ctls[1] == 0x9010006Eu);
    // A record shorter than 0x16 is skipped, not read past.
    uint8_t q[2 + 8] = {8, 0, 3, 1, 2, 3, 4, 0, 0, 0};
    CHECK(u2s::for_each_2005(q, sizeof q, [&](uint8_t, uint32_t, uint32_t) {}) == 0);
    CHECK(u2s::for_each_2005(q, 1, [&](uint8_t, uint32_t, uint32_t) {}) == 0);
}

void test_prep_drain() {
    u2s::PrepDrainIn in;
    in.host_mgr = in.armed = in.same_field = in.cells_known = in.server_thread = in.caller_known = true;
    in.trocs_in_flight = 0;
    in.dist = 1;  // S31: the host 1 cell from user 2 at its preparation
    CHECK(u2s::prep_drain_verdict(in) == u2s::PrepDrain::Drain);
    in.dist = 7;
    CHECK(u2s::prep_drain_verdict(in) == u2s::PrepDrain::Drain);
    in.dist = 8;
    CHECK(u2s::prep_drain_verdict(in) == u2s::PrepDrain::TooFar);
    in.dist = 1;
    auto t = in;
    t.host_mgr = false;
    t.armed = false;
    CHECK(u2s::prep_drain_verdict(t) == u2s::PrepDrain::NotHost);
    t = in;
    t.armed = false;
    t.server_thread = false;
    CHECK(u2s::prep_drain_verdict(t) == u2s::PrepDrain::NotArmed);
    t = in;
    t.server_thread = false;
    t.nested = true;
    CHECK(u2s::prep_drain_verdict(t) == u2s::PrepDrain::NotServerThread);
    t = in;
    t.nested = true;
    t.caller_known = false;
    CHECK(u2s::prep_drain_verdict(t) == u2s::PrepDrain::Nested);
    t = in;
    t.caller_known = false;
    t.same_field = false;
    CHECK(u2s::prep_drain_verdict(t) == u2s::PrepDrain::UnknownCaller);
    t = in;
    t.same_field = false;
    t.cells_known = false;
    CHECK(u2s::prep_drain_verdict(t) == u2s::PrepDrain::NotSameField);
    t = in;
    t.cells_known = false;
    CHECK(u2s::prep_drain_verdict(t) == u2s::PrepDrain::PositionUnknown);
    // Review 1: never with a Troc of user 2 in flight (checked last: everything else holds).
    t = in;
    t.trocs_in_flight = 1;
    CHECK(u2s::prep_drain_verdict(t) == u2s::PrepDrain::Busy);
    t.dist = 9;
    CHECK(u2s::prep_drain_verdict(t) == u2s::PrepDrain::TooFar);
    // Review 8: not armed, but stages left by an async drain: drained too.
    t = in;
    t.armed = false;
    t.leftovers = true;
    CHECK(u2s::prep_drain_verdict(t) == u2s::PrepDrain::Drain);
    t.leftovers = false;
    CHECK(u2s::prep_drain_verdict(t) == u2s::PrepDrain::NotArmed);
    // Review 2: the distance drain held while the host's load is pending (L after P), at most 15 s.
    const uint64_t sec = 1'000'000;
    CHECK(!u2s::hold_distance_drain(false, 100 * sec, 99 * sec, 50 * sec, 0));   // the key at 0 / probe
    CHECK(!u2s::hold_distance_drain(true, 100 * sec, 0, 0, 0));                  // no load seen
    CHECK(!u2s::hold_distance_drain(true, 100 * sec, 90 * sec, 95 * sec, 0));    // P after L: nothing pending
    CHECK(u2s::hold_distance_drain(true, 100 * sec, 95 * sec, 90 * sec, 0));     // starts holding
    CHECK(u2s::hold_distance_drain(true, 114 * sec, 95 * sec, 90 * sec, 100 * sec));
    CHECK(!u2s::hold_distance_drain(true, 115 * sec, 95 * sec, 90 * sec, 100 * sec));  // 15 s: expired
    CHECK(u2s::prep_caller_known(0x287DCD5) && u2s::prep_caller_known(0x289604E) && u2s::prep_caller_known(0x2BC84D9));
    CHECK(!u2s::prep_caller_known(0x287DCD0) && !u2s::prep_caller_known(0));
    CHECK(std::string(u2s::prep_caller_name(0x1234)) == "an UNKNOWN caller");
    for (size_t i = 0; i < static_cast<size_t>(u2s::PrepDrain::Count); ++i) {
        CHECK(std::string(u2s::prep_drain_name(static_cast<u2s::PrepDrain>(i))) != "?");
    }
}

}  // namespace

int main() {
    test_key();
    test_cells();
    test_world();
    test_arm();
    test_machine();
    test_who_and_tables();
    test_near();
    test_fields_and_offsets();
    test_u1();
    test_budget();
    test_host_chains();
    test_npc_control();
    test_key_table();
    test_r42_slot_table();
    test_ctl_stats_and_abort();
    test_2005_walk();
    test_prep_drain();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_user2_stages: all passed\n");
    return 0;
}
