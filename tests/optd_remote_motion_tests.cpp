// P8, the other player's character as a native remote actor: free wall climb,
// ledge climb-up and mount/dismount (src/mod/session2/remote_motion_logic.h;
// docs/research/pendientes/p8-impl-escalada.md).
//
// SCOPE: offline logic only. The remote_climb modes, the PointClimb::Enter
// decision (forced only in control mode 2 for a 0xA01 key with remote_climb=1,
// never in mode 1/3), the wall-up byte's section/page checks (the real section
// table of build 2976) and its periodic watch, the probe's triggers, ring,
// episode window and compact segment log. They do NOT test the hook, the ABI,
// the byte write nor the game's behaviour (live).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mod/session2/remote_motion_logic.h"

using namespace cdmp::session2;
using rmo::ClimbOut;
using rmo::Mode;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

// Session-7 keys: the host's character, user 2's (two sessions), user 2's horse, a user key.
constexpr uint32_t kHostChar = 0xA0100001, kGuestChar = 0xA01002E9, kGuestChar2 = 0xA0100309;
constexpr uint32_t kGuestHorse = 0xB010030B, kGuestUser = 0x901002E8;

void test_parse_mode() {
    CHECK(rmo::parse_mode("").mode == Mode::Off && rmo::parse_mode("").known);
    CHECK(rmo::parse_mode("0").mode == Mode::Off);
    CHECK(rmo::parse_mode(" probe ").mode == Mode::Probe);
    CHECK(rmo::parse_mode("PROBE\r").mode == Mode::Probe);
    CHECK(rmo::parse_mode("probe ; F1").mode == Mode::Probe);
    CHECK(rmo::parse_mode("1").mode == Mode::On);
    CHECK(rmo::parse_mode("on").mode == Mode::On);
    const auto bad = rmo::parse_mode("force");
    CHECK(bad.mode == Mode::Off && !bad.known);
    CHECK(std::string(rmo::mode_name(Mode::Probe)) == "probe");
}

void test_keys() {
    CHECK(rmo::is_player_character_key(kHostChar));
    CHECK(rmo::is_player_character_key(kGuestChar));
    CHECK(rmo::is_player_character_key(kGuestChar2));
    CHECK(rmo::is_player_character_key(0xA0101847));
    CHECK(!rmo::is_player_character_key(kGuestHorse));
    CHECK(!rmo::is_player_character_key(kGuestUser));
    CHECK(!rmo::is_player_character_key(0xA0200001));  // another class prefix, not 0xA01
    CHECK(!rmo::is_player_character_key(0));
}

rmo::ClimbFacts facts(bool ok, bool readable, uint8_t mode, uint32_t key) {
    rmo::ClimbFacts f;
    f.orig_ok = ok;
    f.readable = readable;
    f.control_mode = mode;
    f.key = key;
    return f;
}

void test_climb() {
    // The original's success is never touched, whatever the mode.
    for (Mode m : {Mode::Off, Mode::Probe, Mode::On}) {
        CHECK(rmo::climb_decide(m, facts(true, true, 2, kGuestChar)) == ClimbOut::OrigOk);
        CHECK(rmo::climb_result(ClimbOut::OrigOk, 1) == 1);
    }
    // The case of the report: mode 2, a player's character.
    CHECK(rmo::climb_decide(Mode::On, facts(false, true, 2, kGuestChar)) == ClimbOut::Forced);
    CHECK(rmo::climb_decide(Mode::On, facts(false, true, 2, kHostChar)) == ClimbOut::Forced);
    CHECK(rmo::climb_result(ClimbOut::Forced, 0) == 1);
    CHECK(rmo::climb_decide(Mode::Probe, facts(false, true, 2, kGuestChar)) == ClimbOut::WouldForce);
    CHECK(rmo::climb_result(ClimbOut::WouldForce, 0) == 0);
    // Never in mode 1 or 3 (vf1 0x691DB0 would use an invalid climb point), nor another value.
    for (Mode m : {Mode::Probe, Mode::On}) {
        CHECK(rmo::climb_decide(m, facts(false, true, 1, kGuestChar)) == ClimbOut::FailMode1);
        CHECK(rmo::climb_decide(m, facts(false, true, 3, kGuestChar)) == ClimbOut::FailMode3);
        CHECK(rmo::climb_decide(m, facts(false, true, 0, kGuestChar)) == ClimbOut::FailModeOther);
        CHECK(rmo::climb_decide(m, facts(false, true, 4, kGuestChar)) == ClimbOut::FailModeOther);
        CHECK(rmo::climb_decide(m, facts(false, true, 2, kGuestHorse)) == ClimbOut::FailMode2Other);
        CHECK(rmo::climb_decide(m, facts(false, false, 2, kGuestChar)) == ClimbOut::Unreadable);
    }
    for (int o = 0; o < static_cast<int>(ClimbOut::Count); ++o) {
        const auto out = static_cast<ClimbOut>(o);
        CHECK(rmo::climb_result(out, 0) == (out == ClimbOut::Forced ? 1 : 0));
        CHECK(std::string(rmo::climb_out_name(out)) != "?");
    }
}

// The section table of CrimsonDesert.exe 1.0.0.2976 (cdpe.py on the exe on disk).
std::vector<rmo::SectionInfo> sections_2976() {
    struct Row {
        const char* name;
        uint32_t va, vsize, flags;
    };
    const Row rows[] = {{".rsrc", 0x1000, 0x51EA000, 0x60000020},       {".xdata", 0x51EB000, 0x16A8000, 0x40000040},
                        {".rdata", 0x6893000, 0x854000, 0xC0000040},    {".00cfg", 0x70E7000, 0x2CD000, 0x40000040},
                        {".tls$", 0x73B4000, 0x3000, 0x40000040},       {".tls", 0x73B7000, 0x30000, 0x40000040},
                        {".xtext", 0x73E7000, 0x1E2000, 0x42000040},    {".xtls", 0x75C9000, 0xF9466E2, 0xE0000020},
                        {".text", 0x16F10000, 0x1C3986, 0x40000020},    {".shared", 0x170D4000, 0x248, 0xC0000020},
                        {".impdata", 0x170D5000, 0x8DDC, 0x40000040}};
    std::vector<rmo::SectionInfo> v;
    for (const Row& r : rows) {
        rmo::SectionInfo s;
        std::memcpy(s.name, r.name, std::strlen(r.name));
        s.va = r.va;
        s.vsize = r.vsize;
        s.flags = r.flags;
        v.push_back(s);
    }
    return v;
}

void test_byte_site() {
    const auto secs = sections_2976();
    CHECK(rmo::check_data_byte(secs, 0x6D76D5C) == rmo::ByteSite::Ok);  // the wall-up byte (.rdata, RW)
    CHECK(rmo::check_data_byte(secs, 0x87DD2F) == rmo::ByteSite::InCode);
    CHECK(rmo::check_data_byte(secs, 0x8000000) == rmo::ByteSite::Xtls);
    CHECK(rmo::check_data_byte(secs, 0x5300000) == rmo::ByteSite::NotWritable);   // .xdata
    CHECK(rmo::check_data_byte(secs, 0x170D4010) == rmo::ByteSite::Ok);  // .shared: RW (0xC0000020, no MEM_EXECUTE)
    CHECK(rmo::check_data_byte(secs, 0x16F10010) == rmo::ByteSite::NotWritable);  // .text
    auto rwx = secs;
    rmo::SectionInfo x;
    std::memcpy(x.name, ".rwx", 4);
    x.va = 0x18000000;
    x.vsize = 0x1000;
    x.flags = 0xE0000020;
    rwx.push_back(x);
    CHECK(rmo::check_data_byte(rwx, 0x18000010) == rmo::ByteSite::Executable);
    CHECK(rmo::check_data_byte(secs, 0x400) == rmo::ByteSite::NoSection);
    CHECK(rmo::check_data_byte(secs, 0x70E7000) == rmo::ByteSite::NotWritable);  // first byte past .rdata
    CHECK(rmo::page_writable_data(0x1000, 0x04));
    CHECK(rmo::page_writable_data(0x1000, 0x08));
    CHECK(!rmo::page_writable_data(0x1000, 0x40));        // PAGE_EXECUTE_READWRITE
    CHECK(!rmo::page_writable_data(0x1000, 0x02));        // PAGE_READONLY
    CHECK(!rmo::page_writable_data(0x1000, 0x04 | 0x100)); // guard
    CHECK(!rmo::page_writable_data(0x2000, 0x04));        // reserved
}

void test_byte_watch() {
    rmo::ByteWatch w;
    CHECK(!w.observe(true, 1));
    CHECK(w.observe(true, 0) && w.resets == 1);
    CHECK(!w.observe(false, 0) && w.unreadable == 1 && w.resets == 1);
    CHECK(w.observe(true, 2) && w.resets == 2 && w.last_seen == 2);
    CHECK(w.checks == 4);
}

rmo::Sample sample(uint32_t t, uint8_t method, uint32_t link = 0, float y = 616.5f, uint32_t act = 0x25DC7FB9) {
    rmo::Sample s;
    s.t_ms = t;
    s.ok = true;
    s.method = method;
    s.enter_ok = 1;
    s.act0 = act;
    s.act1 = act;
    s.link = link;
    s.pos_ok = true;
    s.pos[0] = -10092.7f;
    s.pos[1] = y;
    s.pos[2] = -3530.7f;
    s.wall_state = 0;
    return s;
}

void test_triggers() {
    CHECK(rmo::trigger_of(sample(0, 0), sample(30, 0)) == 0);
    CHECK(rmo::trigger_of(sample(0, 10), sample(30, 7)) == rmo::kTrigRide);   // Vehicle -> RideOff
    CHECK(rmo::trigger_of(sample(0, 7), sample(30, 0)) == rmo::kTrigRide);    // RideOff -> Normal
    CHECK(rmo::trigger_of(sample(0, 0), sample(30, 6)) == rmo::kTrigRide);    // -> RideOn
    CHECK(rmo::trigger_of(sample(0, 0), sample(30, 18)) == rmo::kTrigClimb);
    CHECK(rmo::trigger_of(sample(0, 18), sample(30, 5)) == (rmo::kTrigClimb | rmo::kTrigWallUp));
    CHECK(rmo::trigger_of(sample(0, 0, 0), sample(30, 0, kGuestHorse)) == rmo::kTrigRide);  // link only
    rmo::Sample bad = sample(30, 7);
    bad.ok = false;
    CHECK(rmo::trigger_of(sample(0, 10), bad) == 0);
    CHECK(rmo::trigger_name(rmo::kTrigRide | rmo::kTrigWallUp) == "ride+wallup");
}

void test_ring() {
    rmo::SampleRing<4> r;
    CHECK(r.size() == 0 && r.last() == nullptr);
    for (uint32_t i = 0; i < 6; ++i) r.push(sample(i * 30, 0));
    CHECK(r.size() == 4);
    CHECK(r.at(0).t_ms == 60 && r.at(3).t_ms == 150 && r.last()->t_ms == 150);
    r.clear();
    CHECK(r.size() == 0);
}

// The session-7 dismount as the host saw it (p8-escalada-desmontar.md §4):
// Vehicle with the saddle height, RideOff, then Normal on the ground.
void test_episode() {
    rmo::SampleRing<64> ring;
    uint32_t t = 100000;
    for (int i = 0; i < 50; ++i, t += 30) ring.push(sample(t, 10, 0, 618.03f, 0x7B6AFF57));
    const rmo::Sample before = *ring.last();
    rmo::Sample first = sample(t, 7, 0, 618.03f, 0x667E3B35);
    first.enter_ok = 0;  // RideOff::Enter failed (no link on the observer)
    rmo::Episode ep;
    CHECK(rmo::trigger_of(before, first) == rmo::kTrigRide);
    ep.start(kGuestChar2, t, rmo::trigger_of(before, first), before, first, ring);
    // 1 s before at 30 ms: the ring's samples from t-1000 on (t-990 .. t-30: 33 of them).
    CHECK(ep.active() && ep.segments() == 1);
    CHECK(ep.segment(0).t0 >= -rmo::Episode::kBeforeMs && ep.segment(0).t1 == -30);
    const uint32_t pre = ep.samples();
    CHECK(pre == 33);
    ep.add(first);
    ring.push(first);
    t += 30;
    for (int i = 0; i < 30; ++i, t += 30) {
        rmo::Sample s = sample(t, 7, 0, 618.03f, 0x667E3B35);
        s.enter_ok = 0;
        ep.add(s);
    }
    // Normal on the ground (a new trigger: extends the window).
    const uint32_t t_ground = t;
    ep.retrigger(t_ground, rmo::kTrigRide);
    for (int i = 0; i < 40; ++i, t += 30) ep.add(sample(t, 0, 0, 616.49f - 0.001f * i));
    CHECK(ep.segments() == 3);
    CHECK(ep.segment(1).st.method == 7 && ep.segment(1).st.enter_ok == 0 && ep.segment(1).n == 31);
    CHECK(ep.segment(2).st.method == 0 && ep.segment(2).ymin < ep.segment(2).ymax);
    CHECK(!ep.done(t_ground + rmo::Episode::kAfterMs - 1));
    CHECK(ep.done(t_ground + rmo::Episode::kAfterMs));
    const auto lines = ep.lines(7, false);
    CHECK(lines.size() == 4);
    CHECK(lines[0].find("RemoteMotion probe #7: actor 0xA0100309 ride") != std::string::npos);
    CHECK(lines[0].find("method 10 Vehicle -> 7 RideOff") != std::string::npos);
    CHECK(lines[0].find("1 more triggers") != std::string::npos);
    CHECK(lines[2].find("method 7 RideOff enter 0") != std::string::npos);
    CHECK(lines[2].find("act 667e3b35/667e3b35") != std::string::npos);
    CHECK(lines[1].find("y 618.03..618.03") != std::string::npos);
    CHECK(lines[3].find("wall") == std::string::npos);  // no wall-up data: not printed
    ep.close();
    CHECK(!ep.active());
}

void test_episode_limits() {
    rmo::SampleRing<64> ring;
    rmo::Episode ep;
    const rmo::Sample a = sample(0, 0), b = sample(30, 18);
    ep.start(kHostChar, 30, rmo::kTrigClimb, a, b, ring);
    // A new trigger every second for a minute: the window is capped at kMaxSpanMs.
    for (uint32_t t = 60; t < 60000; t += 1000) ep.retrigger(t, rmo::kTrigClimb);
    CHECK(ep.end_ms() == 30 + rmo::Episode::kMaxSpanMs);
    // Alternating states past the segment limit: counted, not stored.
    for (uint32_t i = 0; i < 200; ++i) ep.add(sample(60 + 30 * i, i % 2 ? 18 : 5));
    CHECK(ep.segments() == rmo::Episode::kMaxSegments);
    const auto lines = ep.lines(1, true);
    CHECK(lines.size() == rmo::Episode::kMaxSegments + 1);
    CHECK(lines[0].find("past the segment limit") != std::string::npos);
    CHECK(lines[0].find("(cut: actor gone or probe stopped)") != std::string::npos);
    CHECK(lines[1].find("wall D+0xEE0") != std::string::npos);  // WallUp segments show the wall data
    // Unreadable samples get their own segment text.
    rmo::Episode e2;
    e2.start(kHostChar, 0, rmo::kTrigRide, a, b, ring);
    rmo::Sample bad;
    bad.t_ms = 30;
    e2.add(bad);
    CHECK(e2.lines(2, false)[1].find("not readable") != std::string::npos);
}

void test_candidates() {
    const uintptr_t actor = 0x1000, local = 0x2000;
    CHECK(rmo::is_remote_player_candidate(kGuestChar, kGuestUser, actor, local, kHostChar));
    CHECK(!rmo::is_remote_player_candidate(kHostChar, 0x90100000, local, local, kHostChar));  // ours
    CHECK(!rmo::is_remote_player_candidate(kGuestChar, 0, actor, local, kHostChar));          // no owning user
    CHECK(!rmo::is_remote_player_candidate(kGuestHorse, kGuestUser, actor, local, kHostChar)); // a horse
    CHECK(!rmo::is_remote_player_candidate(kGuestChar, kGuestUser, 0, local, kHostChar));
}

// ---------------------------------------------------------------- 4. divergence_probe (p8-impl-log-async.md §3)

void test_channel_trail() {
    rmo::ChannelTrail<64> c;
    const float p0[3] = {0, 0, 0};
    CHECK(!c.distance(p0, 1000).ok);  // nothing from the channel yet
    // The other player runs along x at 7 m/s; the channel gets a position every 50 ms.
    for (int i = 0; i <= 40; ++i) {
        const float p[3] = {7.0f * static_cast<float>(i) * 0.05f, 600.0f, 0.0f};
        c.push(10000 + static_cast<uint64_t>(i) * 50, p);
    }
    const uint64_t now = 12000;  // newest at x = 14 m, 0 ms old
    // In memory it plays ~230 ms late (200 ms delay + latency): x = 12.39 m. 1.61 m from the newest point (the
    // plain distance would say "diverged" all the time) but on the trail.
    const float late[3] = {14.0f - 7.0f * 0.23f, 600.0f, 0.0f};
    auto d = c.distance(late, now);
    CHECK(d.ok);
    CHECK(d.latest > 1.5f);
    CHECK(d.trail < 0.01f);
    CHECK(d.in_trail >= 12);
    CHECK(d.age_ms == 0);
    // Frozen 1.2 s back (x = 5.6 m): the trail covers the last 600 ms plus the segment that enters it (from
    // x = 9.45 m), so it is 3.85 m off.
    const float frozen[3] = {14.0f - 7.0f * 1.2f, 600.0f, 0.0f};
    d = c.distance(frozen, now);
    CHECK(d.ok && d.trail > 3.8f && d.trail < 3.9f && d.latest > 8.0f);
    // Off to the side by 2 m while on the path: the trail does not hide it.
    const float side[3] = {13.0f, 602.0f, 0.0f};
    d = c.distance(side, now);
    CHECK(d.ok && d.trail > 1.9f && d.trail < 2.1f);
    // A stale channel (the other player gone): not compared.
    d = c.distance(late, now + rmo::kChannelStaleMs + 1);
    CHECK(!d.ok);
    CHECK(d.age_ms == rmo::kChannelStaleMs + 1);
    // Standing still: one position repeated, distance to it.
    rmo::ChannelTrail<8> s;
    const float still[3] = {5, 5, 5};
    for (int i = 0; i < 20; ++i) s.push(100 + static_cast<uint64_t>(i) * 50, still);
    CHECK(s.size() == 8);
    const float near[3] = {5, 5, 6};
    d = s.distance(near, 1100);
    CHECK(d.ok && std::fabs(d.latest - 1.0f) < 1e-4f && std::fabs(d.trail - 1.0f) < 1e-4f);
    CHECK(std::fabs(rmo::dist_to_segment(near, still, still) - 1.0f) < 1e-4f);  // a zero-length segment
}

void test_divergence_watch() {
    rmo::DivergenceLedger led;
    rmo::DivergenceWatch w;
    using K = rmo::DivergenceWatch::Kind;
    // A spike of 600 ms: not an episode.
    uint64_t t = 1000;
    for (; t < 1600; t += 30) CHECK(w.observe(t, true, 3.0f, led).kind == K::None);
    CHECK(w.observe(t, true, 0.2f, led).kind == K::None);
    CHECK(led.short_spikes == 1 && led.episodes == 0 && led.lines == 0);
    // Over for 3.5 s: the first line once over for > 1 s, then one per second; the end line after.
    const uint64_t start = 5000;
    std::vector<rmo::DivergenceWatch::Step> lines;
    for (t = start; t <= start + 3500; t += 30) {
        const auto s = w.observe(t, true, t < start + 2000 ? 2.0f : 5.0f, led);
        if (s.kind == K::Line) lines.push_back(s);
        CHECK(s.kind != K::End);
    }
    CHECK(lines.size() == 3);
    CHECK(lines.size() == 3 && lines[0].over_ms > 1000 && lines[0].over_ms <= 1030 && lines[0].line == 1 &&
          lines[0].episode == 1);
    CHECK(lines.size() == 3 && lines[1].line == 2 && lines[1].over_ms >= 2000 && lines[2].line == 3);
    CHECK(lines.size() == 3 && lines[2].max_m == 5.0f);
    const auto end = w.observe(t, true, 1.0f, led);
    CHECK(end.kind == K::End && end.episode == 1 && end.line == 3 && !end.invalid_end && end.max_m == 5.0f);
    CHECK(w.observe(t + 30, true, 1.0f, led).kind == K::None);
    CHECK(led.lines == 4);  // 3 lines + the end line
    // An episode that ends because the comparison stops (actor gone / channel stale).
    for (t = 20000; t <= 21100; t += 30) w.observe(t, true, 9.0f, led);
    const auto gone = w.observe(t, false, 0.0f, led);
    CHECK(gone.kind == K::End && gone.invalid_end && gone.episode == 2);
    // Exactly at the threshold is not over it.
    for (t = 30000; t < 33000; t += 30) CHECK(w.observe(t, true, rmo::kDivergenceThresholdM, led).kind == K::None);
    CHECK(led.lines == 6 && led.episodes == 2);
}

// Review of 10-10: one watch per tracked character, so a second candidate near the channel does not hide the
// real one; episodes are numbered and lines budgeted in the one ledger.
void test_divergence_per_candidate() {
    rmo::DivergenceLedger led;
    rmo::DivergenceWatch frozen, close;
    using K = rmo::DivergenceWatch::Kind;
    uint32_t frozen_lines = 0, close_lines = 0;
    for (uint64_t t = 0; t <= 3000; t += 30) {
        if (frozen.observe(t, true, 6.0f, led).kind == K::Line) ++frozen_lines;
        if (close.observe(t, true, 0.3f, led).kind == K::Line) ++close_lines;
    }
    CHECK(frozen_lines == 2 && close_lines == 0);
    CHECK(led.episodes == 1);
    // Both diverging: two episodes, numbered in order of their first line.
    rmo::DivergenceWatch a, b;
    rmo::DivergenceWatch::Step sa, sb;
    for (uint64_t t = 10000; t <= 11500; t += 30) {
        const auto x = a.observe(t, true, 3.0f, led);
        if (x.kind == K::Line && !sa.episode) sa = x;
        const auto y = b.observe(t + 5, true, 4.0f, led);
        if (y.kind == K::Line && !sb.episode) sb = y;
    }
    CHECK(sa.episode == 2 && sb.episode == 3);
}

void test_divergence_budget() {
    rmo::DivergenceLedger led;
    rmo::DivergenceWatch w;
    uint64_t t = 0;
    uint32_t lines = 0;
    for (; t < (rmo::kDivergenceMaxLines + 50) * 1000ull + 2000; t += 100) {
        if (w.observe(t, true, 10.0f, led).kind == rmo::DivergenceWatch::Kind::Line) ++lines;
    }
    CHECK(lines == rmo::kDivergenceMaxLines);
    CHECK(led.lines == rmo::kDivergenceMaxLines);
    CHECK(led.unlogged >= 49);
    // The end line counts against the same budget (review of 10-10): with it spent, no line, one more unlogged.
    const uint32_t unlogged = led.unlogged;
    CHECK(w.observe(t, true, 0.0f, led).kind == rmo::DivergenceWatch::Kind::None);
    CHECK(led.unlogged == unlogged + 1 && led.lines == rmo::kDivergenceMaxLines);
    // With budget left, the end line takes one.
    rmo::DivergenceLedger led2;
    rmo::DivergenceWatch w2;
    for (t = 0; t <= 1500; t += 30) w2.observe(t, true, 5.0f, led2);
    CHECK(led2.lines == 1);
    CHECK(w2.observe(t, true, 0.0f, led2).kind == rmo::DivergenceWatch::Kind::End && led2.lines == 2);
}

void test_mc_identity() {
    const uintptr_t actor = 0x2A0001000, mc = 0x2B0002000;
    CHECK(rmo::mc_identity_ok(actor, actor, mc, actor));
    CHECK(!rmo::mc_identity_ok(actor, 0x2A0009000, mc, actor));  // the CC names another actor
    CHECK(!rmo::mc_identity_ok(actor, actor, mc, 0x2A0009000));  // the controller does
    CHECK(!rmo::mc_identity_ok(actor, actor, 0, actor));
    CHECK(!rmo::mc_identity_ok(0, 0, mc, 0));
}

// A fake memory for read_interp: the interpolation buffer and its sample array.
struct FakeMem {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x2000);
    uintptr_t base = 0x10000;
    bool in(uintptr_t a, size_t n) const { return a >= base && a + n <= base + bytes.size(); }
    template <class T>
    bool rd(uintptr_t a, T& v) const {
        if (!in(a, sizeof v)) return false;
        std::memcpy(&v, bytes.data() + (a - base), sizeof v);
        return true;
    }
    template <class T>
    void wr(uintptr_t a, const T& v) {
        std::memcpy(bytes.data() + (a - base), &v, sizeof v);
    }
    bool ptr(uintptr_t a, uintptr_t& v) const { return rd(a, v); }
    bool u8(uintptr_t a, uint8_t& v) const { return rd(a, v); }
    bool u32(uintptr_t a, uint32_t& v) const { return rd(a, v); }
    bool u64(uintptr_t a, uint64_t& v) const { return rd(a, v); }
    bool vec3(uintptr_t a, float (&v)[3]) const {
        if (!in(a, sizeof v)) return false;
        std::memcpy(v, bytes.data() + (a - base), sizeof v);
        return true;
    }
};

void test_read_interp() {
    FakeMem m;
    const uintptr_t mc = m.base + 0x100, buf = mc + rmo::kInterpPosBuffer, arr = m.base + 0x800;
    m.wr<uint8_t>(buf + rmo::kInterpDone, 0);
    m.wr<uintptr_t>(buf + rmo::kInterpArray, arr);
    m.wr<uint32_t>(buf + rmo::kInterpCount, 3);
    for (uint64_t i = 0; i < 3; ++i) {
        m.wr<uint64_t>(arr + i * rmo::kInterpSampleSize + rmo::kInterpSampleTs, 934000 + i * 30);
    }
    const float shown[3] = {-10639.56f, 613.96f, -3836.84f};
    m.wr(buf + rmo::kInterpShown, shown);
    m.wr<uint64_t>(buf + rmo::kInterpShown + rmo::kInterpSampleTs, 933950);
    auto v = rmo::read_interp(m, buf);
    CHECK(v.ok && v.count == 3 && v.done == 0 && v.ts_first == 934000 && v.ts_last == 934060);
    CHECK(v.shown_ok && v.shown_ts == 933950 && v.shown[1] == 613.96f);
    // Empty buffer: ok, no ts.
    m.wr<uint32_t>(buf + rmo::kInterpCount, 0);
    v = rmo::read_interp(m, buf);
    CHECK(v.ok && v.count == 0 && v.ts_first == 0 && v.ts_last == 0);
    // Garbage count / unreadable array: not ok.
    m.wr<uint32_t>(buf + rmo::kInterpCount, 1000);
    CHECK(!rmo::read_interp(m, buf).ok);
    m.wr<uint32_t>(buf + rmo::kInterpCount, 2);
    m.wr<uintptr_t>(buf + rmo::kInterpArray, 0x5);
    CHECK(!rmo::read_interp(m, buf).ok);
    CHECK(!rmo::read_interp(m, 0).ok);

    // The shown sample's watch: "update in the last 1 s" from its ts moving.
    rmo::ShownWatch sw;
    CHECK(sw.since_ms(100) == -1);
    sw.observe(100, true, 5000);
    sw.observe(130, true, 5000);
    CHECK(sw.since_ms(130) == -1);  // never seen moving
    sw.observe(160, true, 5030);
    CHECK(sw.since_ms(160) == 0);
    sw.observe(190, false, 0);     // unreadable: ignored
    sw.observe(2160, true, 5030);  // stuck for 2 s
    CHECK(sw.since_ms(2160) == 2000);
}

void test_divergence_text() {
    rmo::DivergenceFacts f;
    f.step.kind = rmo::DivergenceWatch::Kind::Line;
    f.step.episode = 2;
    f.step.line = 3;
    f.step.over_ms = 3010;
    f.step.max_m = 6.5f;
    f.key = kHostChar;
    f.actor = 0x2A0001000;
    f.mc = 0x2B0002000;
    f.s.ok = true;
    f.s.method = 0;
    f.s.enter_ok = 1;
    f.s.act0 = 0x3E005FA1;
    f.s.pos[0] = -10639.56f;
    f.cd.ok = true;
    f.cd.latest = 6.5f;
    f.cd.trail = 6.1f;
    f.control_mode = 2;
    f.iv.ok = true;
    f.iv.count = 2;
    f.iv.ts_first = 934300;
    f.iv.ts_last = 934309;
    f.now_ok = true;
    f.game_now = 936000;
    f.delay = 200;
    f.shown_since_ms = 2500;
    f.local_ok = true;
    f.local_method = 13;
    const std::string s = rmo::divergence_text(f);
    CHECK(s.find("RemoteMotion divergence #2 [3] +3010 ms") == 0);
    CHECK(s.find("key 0xA0100001") != std::string::npos);
    CHECK(s.find("act 3e005fa1/00000000") != std::string::npos);
    CHECK(s.find("control mode 2") != std::string::npos);
    CHECK(s.find("count 2 done 255 ts[0] 934300 ts[last] 934309") != std::string::npos);
    CHECK(s.find("now-delay 935800 (now 936000, delay 200) = ts[last]+1491 ms, ts[0]+1500 ms") != std::string::npos);
    CHECK(s.find("update NO for 2500 ms") != std::string::npos);
    CHECK(s.find("local client: method 13 Sequencer") != std::string::npos);
    CHECK(s.find("no verified dialogue/scene flag") != std::string::npos);
    f.control_mode = 0xFF;
    f.now_ok = false;
    f.shown_since_ms = 400;
    const std::string s2 = rmo::divergence_text(f);
    CHECK(s2.find("control mode ?") != std::string::npos);
    CHECK(s2.find("now-delay ? (the frame clock is unreadable)") != std::string::npos);
    CHECK(s2.find("update yes in the last 1 s") != std::string::npos);
    rmo::DivergenceFacts e;
    e.step.kind = rmo::DivergenceWatch::Kind::End;
    e.step.episode = 2;
    e.step.over_ms = 4000;
    e.step.line = 3;
    e.step.max_m = 6.5f;
    e.key = kHostChar;
    CHECK(rmo::divergence_text(e) == "RemoteMotion divergence #2 END key 0xA0100001 after 4000 ms over 1.5 m (3 line(s), "
                                     "max 6.50 m): back within the threshold");
}

}  // namespace

int main() {
    test_parse_mode();
    test_keys();
    test_climb();
    test_byte_site();
    test_byte_watch();
    test_triggers();
    test_ring();
    test_episode();
    test_episode_limits();
    test_candidates();
    test_channel_trail();
    test_divergence_watch();
    test_divergence_per_candidate();
    test_divergence_budget();
    test_mc_identity();
    test_read_interp();
    test_divergence_text();
    if (g_failures) {
        std::printf("optd_remote_motion: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("optd_remote_motion: all passed\n");
    return 0;
}
