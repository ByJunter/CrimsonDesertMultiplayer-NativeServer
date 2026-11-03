// P8 re-entry (docs/research/s25/reentrada.md): src/mod/session2/rejoin_logic.h (the frames 13 / 14, the host's
// answer to a REJOIN, the guest's state machine and decision) and the per-conversation resets of the pure tables it
// relies on: remote_interp's SlotTable generation and forget(), guest_s3's GuardTable generation, GuestHold's
// new_conversation (the final save once per CONVERSATION, no new upload), OutboundDrain::discard, SchedLedger::clear.

#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

#include "common/troc_frame.h"
#include "mod/session2/guest_s3_logic.h"
#include "mod/session2/guest_save_wire.h"
#include "mod/session2/guest_seq_route_logic.h"
#include "mod/session2/mount_sync_logic.h"
#include "mod/session2/rejoin_logic.h"
#include "mod/session2/remote_interp_logic.h"

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

// ---------------------------------------------------------------- frames

void test_frames() {
    // The constants agree with troc_frame.h.
    static_assert(rjl::kControlRejoin == cdmp::troc::kControlRejoin);
    static_assert(rjl::kControlEpoch == cdmp::troc::kControlEpoch);
    static_assert(rjl::kCapsRejoin == cdmp::troc::kCapsRejoin);
    static_assert(rjl::kControlOpcode == cdmp::troc::kControlOpcode);

    const rjl::Rejoin r{rjl::Kind::Real, 2, 7};
    const auto f = rjl::rejoin_frame(r);
    CHECK(f.size() == 16);
    const uint8_t want[16] = {0x01, 0x00, 0xFF, 11, 0, 13, 1, 1, 2, 0, 0, 0, 7, 0, 0, 0};
    CHECK(std::memcmp(f.data(), want, sizeof want) == 0);
    CHECK(rjl::is_rejoin_frame(f));
    CHECK(!rjl::is_epoch_frame(f));
    const auto back = rjl::parse_rejoin_frame(f);
    CHECK(back && back->kind == rjl::Kind::Real && back->conversation == 2 && back->last_epoch == 7);
    // The troc parser takes it as a whole ToHostServer packet (opcode 1, slot -1).
    CHECK(cdmp::troc::parse_wire(f, cdmp::troc::Dir::ToHostServer).has_value());

    const rjl::Epoch e{rjl::HostState::Ready, rjl::Reason::ProbeWould, 3, 2};
    const auto g = rjl::epoch_frame(e);
    CHECK(g.size() == 17);
    const uint8_t want_e[17] = {0x01, 0x00, 0xFF, 12, 0, 14, 1, 3, 3, 3, 0, 0, 0, 2, 0, 0, 0};
    CHECK(std::memcmp(g.data(), want_e, sizeof want_e) == 0);
    CHECK(rjl::is_epoch_frame(g) && !rjl::is_rejoin_frame(g));
    const auto eb = rjl::parse_epoch_frame(g);
    CHECK(eb && eb->state == rjl::HostState::Ready && eb->reason == rjl::Reason::ProbeWould && eb->epoch == 3 &&
          eb->conversation == 2);
    CHECK(cdmp::troc::parse_wire(g, cdmp::troc::Dir::ToGuestClient).has_value());

    // Only the exact forms.
    std::vector<uint8_t> v(f.begin(), f.end());
    v.push_back(0);
    CHECK(!rjl::is_rejoin_frame(v));  // one byte too long
    std::vector<uint8_t> w(f.begin(), f.end());
    w[6] = 2;  // another wire version
    CHECK(!rjl::is_rejoin_frame(w));
    w = std::vector<uint8_t>(f.begin(), f.end());
    w[5] = 3;  // a reannounce's state
    CHECK(!rjl::is_rejoin_frame(w));
    w = std::vector<uint8_t>(f.begin(), f.end());
    w[7] = 9;  // an unknown kind
    CHECK(rjl::is_rejoin_frame(w) && !rjl::parse_rejoin_frame(w));
    w = std::vector<uint8_t>(f.begin(), f.end());
    w[8] = 1;  // conversation 1 is never asked for
    CHECK(!rjl::parse_rejoin_frame(w));
    std::vector<uint8_t> x(g.begin(), g.end());
    x[7] = 9;  // an unknown C4 state
    CHECK(!rjl::parse_epoch_frame(x));
    x = std::vector<uint8_t>(g.begin(), g.end());
    x[8] = 9;  // an unknown reason
    CHECK(!rjl::parse_epoch_frame(x));
}

// ---------------------------------------------------------------- the host

void test_host_decide() {
    rjl::HostIn in;
    in.request = rjl::Rejoin{rjl::Kind::Real, 2, 1};
    in.mode = rjl::Mode::Off;
    in.state = rjl::HostState::Ready;
    CHECK(rjl::host_decide(in) == rjl::HostAction::Off);
    in.mode = rjl::Mode::On;
    CHECK(rjl::host_decide(in) == rjl::HostAction::StartInBand);
    in.request.kind = rjl::Kind::Probe;  // a probing guest never starts an epoch
    CHECK(rjl::host_decide(in) == rjl::HostAction::ProbeWould);
    in.request.kind = rjl::Kind::Real;
    in.mode = rjl::Mode::Probe;  // a probing host neither
    CHECK(rjl::host_decide(in) == rjl::HostAction::ProbeWould);
    in.mode = rjl::Mode::On;
    for (const auto s : {rjl::HostState::Retiring, rjl::HostState::Clean, rjl::HostState::Quarantined}) {
        in.state = s;
        CHECK(rjl::host_decide(in) == rjl::HostAction::Ignore);
    }
    // ACTIVE, started in-band for this very conversation, the guest has not spoken: the same answer again.
    in.state = rjl::HostState::Active;
    in.started_for = 2;
    in.conversation = false;
    CHECK(rjl::host_decide(in) == rjl::HostAction::Repeat);
    in.conversation = true;  // the guest already plays in it: a late request is not a new start
    CHECK(rjl::host_decide(in) == rjl::HostAction::Ignore);
    in.conversation = false;
    in.request.conversation = 3;  // another conversation
    CHECK(rjl::host_decide(in) == rjl::HostAction::Ignore);
    // Review of c4367f8, 8: an unbound epoch begun on a new connection is free: it becomes this conversation's.
    in.started_for = 0;
    CHECK(rjl::host_decide(in) == rjl::HostAction::Repeat);
    in.request.kind = rjl::Kind::Probe;  // never for a probing request
    CHECK(rjl::host_decide(in) == rjl::HostAction::Ignore);
    in.request.kind = rjl::Kind::Real;
    in.conversation = true;  // somebody spoke: not free
    CHECK(rjl::host_decide(in) == rjl::HostAction::Ignore);
}

// ---------------------------------------------------------------- review of c4367f8

// 1: a post-conversation connection decided LOCAL is terminal; a grant never used is given up by the host.
void test_local_connection_terminal() {
    rjl::GuestRejoin g(rjl::Mode::On);
    g.on_epoch(rjl::Epoch{rjl::HostState::Active, rjl::Reason::Heartbeat, 1, 0}, 0);
    g.on_remote_begin(0);
    g.on_remote_over(100);
    CHECK(g.due(100));
    // The client's title connection comes before the grant: decided local.
    CHECK(!g.judge(200).remote);
    g.on_local_connection();
    CHECK(g.phase() == rjl::Phase::Refused && g.refusal() == rjl::Refusal::LocalConnection);
    CHECK(!g.due(5000));  // no more requests
    // The late grant changes nothing and is never shown as "the host waits for you".
    g.on_epoch(rjl::Epoch{rjl::HostState::Active, rjl::Reason::StartRejoin, 2, 2}, 6000);
    g.on_ready();
    CHECK(g.phase() == rjl::Phase::Refused && !g.judge(7000).remote);
    CHECK(g.hud_text().find("Continuar") == std::string::npos);
    CHECK(g.hud_text().find("cierra el juego") != std::string::npos);
    // While playing (or before any remote conversation) a local connection is not a refusal.
    rjl::GuestRejoin h(rjl::Mode::On);
    h.on_local_connection();
    CHECK(h.phase() == rjl::Phase::NoRemote);
    // The host gives an unused grant up after kGrantUnusedMs; never a bound or a connection-begun epoch.
    using rjl::HostState;
    CHECK(!rjl::grant_unused(HostState::Active, false, 2, 1000, 1000 + rjl::kGrantUnusedMs - 1));
    CHECK(rjl::grant_unused(HostState::Active, false, 2, 1000, 1000 + rjl::kGrantUnusedMs));
    CHECK(!rjl::grant_unused(HostState::Active, true, 2, 1000, 1000 + rjl::kGrantUnusedMs));
    CHECK(!rjl::grant_unused(HostState::Active, false, 0, 1000, 1000 + rjl::kGrantUnusedMs));
    CHECK(!rjl::grant_unused(HostState::Retiring, false, 2, 1000, 1000 + rjl::kGrantUnusedMs));
}

// 2: an epoch still being played is never granted for the next conversation.
void test_played_epoch_follows_host() {
    rjl::GuestRejoin g(rjl::Mode::On);
    g.on_epoch(rjl::Epoch{rjl::HostState::Ready, rjl::Reason::Heartbeat, 2, 0}, 0);  // capable, nothing played
    g.on_remote_begin(0);
    CHECK(g.played_epoch() == 2);
    // The epoch it plays is named only after its first packet (an in-band start for another guest run...): 3.
    g.on_epoch(rjl::Epoch{rjl::HostState::Active, rjl::Reason::Heartbeat, 3, 0}, 10);
    CHECK(g.played_epoch() == 3);
    g.on_epoch(rjl::Epoch{rjl::HostState::Active, rjl::Reason::Heartbeat, 2, 0}, 20);  // never backwards
    CHECK(g.played_epoch() == 3);
    g.on_remote_over(100);
    const auto r = g.due(100);
    CHECK(r && r->last_epoch == 3);
    // A stale heartbeat of the played epoch naming this conversation never grants.
    g.on_epoch(rjl::Epoch{rjl::HostState::Active, rjl::Reason::Heartbeat, 3, 2}, 200);
    CHECK(g.phase() == rjl::Phase::Asking);
    g.on_epoch(rjl::Epoch{rjl::HostState::Active, rjl::Reason::StartRejoin, 4, 2}, 300);
    CHECK(g.phase() == rjl::Phase::Granted && g.granted_epoch() == 4);
    // The host names the in-band conversation in its heartbeat only while nobody spoke.
    CHECK(rjl::heartbeat_conversation(false, 2) == 2);
    CHECK(rjl::heartbeat_conversation(true, 2) == 0);
    CHECK(rjl::heartbeat_conversation(false, 0) == 0);
}

// 5: only silence refuses; a request while the tunnel is down is neither sent nor counted.
void test_silence_only() {
    rjl::GuestRejoin g(rjl::Mode::On);
    g.on_epoch(rjl::Epoch{rjl::HostState::Active, rjl::Reason::Heartbeat, 1, 0}, 0);
    g.on_remote_begin(0);
    g.on_remote_over(0);
    uint64_t t = 0;
    for (int i = 0; i < 100; ++i, t += rjl::kRetryMs) {
        if (g.due(t)) g.on_epoch(rjl::Epoch{rjl::HostState::Retiring, rjl::Reason::Ignored, 1, 2}, t);  // "not now"
    }
    CHECK(g.phase() == rjl::Phase::Asking && g.asks() == 100);
    // The tunnel goes down: nothing is sent, nothing counted.
    const uint32_t asks = g.asks();
    for (int i = 0; i < 100; ++i, t += rjl::kRetryMs) CHECK(!g.due(t, /*link_up=*/false));
    CHECK(g.asks() == asks && g.phase() == rjl::Phase::Asking);
    // Back up, and silent: refused after kMaxAsks unanswered requests.
    uint32_t sent = 0;
    for (int i = 0; i < 100; ++i, t += rjl::kRetryMs) sent += g.due(t) ? 1 : 0;
    CHECK(sent == rjl::kMaxAsks && g.refusal() == rjl::Refusal::NoAnswer);
}

// 6: rejoin=0 behaves as before; probe never changes the HUD.
void test_rejoin_off_identity() {
    CHECK(!rjl::reentry_conversation(0) && !rjl::reentry_conversation(1) && rjl::reentry_conversation(2));
    CHECK(!rjl::host_forgets_user2_keys(rjl::Mode::Off));
    CHECK(rjl::host_forgets_user2_keys(rjl::Mode::Probe) && rjl::host_forgets_user2_keys(rjl::Mode::On));
    rjl::GuestRejoin g(rjl::Mode::Off);
    g.on_epoch(rjl::Epoch{rjl::HostState::Active, rjl::Reason::Heartbeat, 1, 0}, 0);
    g.on_remote_begin(0);
    g.on_remote_over(10);
    g.on_local_connection();  // rejoin=0: no refusal, the old message
    CHECK(g.phase() == rjl::Phase::Over && !g.due(20) && g.hud_text().empty());
    CHECK(g.judge(30).why.find("rejoin=0") != std::string::npos);
    rjl::GuestRejoin p(rjl::Mode::Probe);
    p.on_remote_begin(0);
    p.on_remote_over(10);  // the host never said it can
    CHECK(p.phase() == rjl::Phase::Over && p.hud_text().empty());
    rjl::GuestRejoin q(rjl::Mode::Probe);
    q.on_epoch(rjl::Epoch{rjl::HostState::Active, rjl::Reason::Heartbeat, 1, 0}, 0);
    q.on_remote_begin(0);
    q.on_remote_over(10);
    CHECK(q.hud_text().empty());
    q.on_local_connection();
    CHECK(q.hud_text().empty());
}

// ---------------------------------------------------------------- the guest

rjl::Epoch ep(rjl::HostState s, rjl::Reason r, uint32_t epoch, uint32_t conv) { return rjl::Epoch{s, r, epoch, conv}; }

void test_guest_full_cycle() {
    rjl::GuestRejoin g(rjl::Mode::On);
    CHECK(g.phase() == rjl::Phase::NoRemote);
    CHECK(!g.due(0));
    // The host's heartbeat during the first conversation (epoch 1).
    g.on_epoch(ep(rjl::HostState::Active, rjl::Reason::Heartbeat, 1, 0), 10);
    CHECK(g.host_capable());
    g.on_ready();
    CHECK(g.on_remote_begin(100) == 1);
    CHECK(g.phase() == rjl::Phase::Playing && g.played_epoch() == 1);
    CHECK(!g.due(200));  // nothing asked while playing
    g.on_epoch(ep(rjl::HostState::Active, rjl::Reason::Heartbeat, 1, 0), 300);
    // The host answers the goodbye: over -> asking for conversation 2.
    CHECK(g.on_remote_over(1000));
    CHECK(!g.on_remote_over(1001));  // only the first notice
    CHECK(g.phase() == rjl::Phase::Asking && g.asking() == 2);
    const auto r1 = g.due(1000);
    CHECK(r1 && r1->kind == rjl::Kind::Real && r1->conversation == 2 && r1->last_epoch == 1);
    CHECK(!g.due(1500));  // kRetryMs
    // RETIRING: not granted; the guest asks again.
    g.on_epoch(ep(rjl::HostState::Retiring, rjl::Reason::Ignored, 1, 2), 1600);
    CHECK(g.phase() == rjl::Phase::Asking);
    CHECK(g.due(2000));
    // A new connection now: local, with the reason.
    auto v = g.judge(2100);
    CHECK(!v.remote && !v.would && v.why.find("has not begun") != std::string::npos);
    // READY -> the host starts epoch 2 in-band for conversation 2.
    g.on_epoch(ep(rjl::HostState::Active, rjl::Reason::StartRejoin, 2, 2), 2500);
    CHECK(g.phase() == rjl::Phase::Granted && g.granted_epoch() == 2);
    CHECK(!g.due(5000));
    // Granted, but no ready after the grant yet.
    v = g.judge(2600);
    CHECK(!v.remote && v.why.find("no 'ready'") != std::string::npos);
    g.on_ready();
    v = g.judge(2700);
    CHECK(v.remote && v.would);
    CHECK(g.hud_text().find("Continuar") != std::string::npos);
    // The client's new connection goes remote: conversation 2 plays epoch 2.
    CHECK(g.on_remote_begin(3000) == 2);
    CHECK(g.phase() == rjl::Phase::Playing && g.played_epoch() == 2);
    // And a third time.
    CHECK(g.on_remote_over(9000));
    const auto r3 = g.due(9000);
    CHECK(r3 && r3->conversation == 3 && r3->last_epoch == 2);
    // A heartbeat of the CURRENT epoch for another conversation never grants.
    g.on_epoch(ep(rjl::HostState::Active, rjl::Reason::Heartbeat, 2, 2), 9100);
    CHECK(g.phase() == rjl::Phase::Asking);
    // A heartbeat naming this conversation with a newer epoch grants (the start answer and the heartbeat agree).
    g.on_epoch(ep(rjl::HostState::Active, rjl::Reason::Heartbeat, 3, 3), 9200);
    CHECK(g.phase() == rjl::Phase::Granted && g.granted_epoch() == 3);
}

void test_guest_refusals() {
    {  // rejoin=0: over, nothing asked, the old message.
        rjl::GuestRejoin g(rjl::Mode::Off);
        g.on_epoch(ep(rjl::HostState::Active, rjl::Reason::Heartbeat, 1, 0), 0);
        g.on_remote_begin(0);
        g.on_remote_over(10);
        CHECK(g.phase() == rjl::Phase::Over && !g.due(10));
        const auto v = g.judge(20);
        CHECK(!v.remote && v.why.find("rejoin=0") != std::string::npos);
        CHECK(g.hud_text().empty());
    }
    {  // An older host (never an EPOCH): over, nothing asked.
        rjl::GuestRejoin g(rjl::Mode::On);
        g.on_remote_begin(0);
        g.on_remote_over(10);
        CHECK(g.phase() == rjl::Phase::Over && !g.due(10));
        CHECK(g.judge(20).why.find("never said") != std::string::npos);
        CHECK(!g.hud_text().empty());
    }
    {  // A local game after the remote one ends the re-entry for the run.
        rjl::GuestRejoin g(rjl::Mode::On);
        g.on_epoch(ep(rjl::HostState::Active, rjl::Reason::Heartbeat, 1, 0), 0);
        g.on_remote_begin(0);
        g.on_remote_over(10);
        g.on_local_game();
        CHECK(g.phase() == rjl::Phase::Refused && g.refusal() == rjl::Refusal::LocalGame && !g.due(20));
        // A late grant changes nothing.
        g.on_epoch(ep(rjl::HostState::Active, rjl::Reason::StartRejoin, 2, 2), 30);
        g.on_ready();
        CHECK(!g.judge(40).remote);
    }
    {  // Host dead / quarantined.
        rjl::GuestRejoin g(rjl::Mode::On);
        g.on_epoch(ep(rjl::HostState::Active, rjl::Reason::Heartbeat, 1, 0), 0);
        g.on_remote_begin(0);
        g.on_remote_over(10);
        g.on_epoch(ep(rjl::HostState::Quarantined, rjl::Reason::Heartbeat, 1, 0), 20);
        CHECK(g.phase() == rjl::Phase::Refused && g.refusal() == rjl::Refusal::HostQuarantined);
        rjl::GuestRejoin h(rjl::Mode::On);
        h.on_epoch(ep(rjl::HostState::Active, rjl::Reason::Heartbeat, 1, 0), 0);
        h.on_remote_begin(0);
        h.on_host_dead();  // while playing: nothing (end_remote reports the end)
        CHECK(h.phase() == rjl::Phase::Playing);
        h.on_remote_over(10);
        h.on_host_dead();
        CHECK(h.refusal() == rjl::Refusal::HostDead);
    }
    {  // No answer after kMaxAsks requests.
        rjl::GuestRejoin g(rjl::Mode::On);
        g.on_epoch(ep(rjl::HostState::Active, rjl::Reason::Heartbeat, 1, 0), 0);
        g.on_remote_begin(0);
        g.on_remote_over(0);
        uint64_t t = 0;
        uint32_t sent = 0;
        for (int i = 0; i < 100; ++i, t += rjl::kRetryMs) sent += g.due(t) ? 1 : 0;
        CHECK(sent == rjl::kMaxAsks);
        CHECK(g.phase() == rjl::Phase::Refused && g.refusal() == rjl::Refusal::NoAnswer);
    }
}

void test_guest_probe() {
    rjl::GuestRejoin g(rjl::Mode::Probe);
    g.on_epoch(ep(rjl::HostState::Active, rjl::Reason::Heartbeat, 1, 0), 0);
    g.on_remote_begin(0);
    g.on_remote_over(10);
    const auto r = g.due(10);
    CHECK(r && r->kind == rjl::Kind::Probe);
    g.on_epoch(ep(rjl::HostState::Ready, rjl::Reason::ProbeWould, 1, 2), 20);
    CHECK(g.probe_would() && g.phase() == rjl::Phase::Asking);
    CHECK(!g.due(5000));  // answered: nothing more to ask
    const auto v = g.judge(30);
    CHECK(!v.remote && v.would && v.why.find("probe") != std::string::npos);
    CHECK(g.hud_text().empty());  // probe never changes the HUD
    // Even a (wrong) grant never makes a probing guest go remote.
    rjl::GuestRejoin h(rjl::Mode::Probe);
    h.on_epoch(ep(rjl::HostState::Active, rjl::Reason::Heartbeat, 1, 0), 0);
    h.on_remote_begin(0);
    h.on_remote_over(0);
    h.on_epoch(ep(rjl::HostState::Active, rjl::Reason::StartRejoin, 2, 2), 5);
    h.on_ready();
    const auto w = h.judge(10);
    CHECK(!w.remote && w.would);
    // Probe never gives up (it changes nothing anyway).
    rjl::GuestRejoin k(rjl::Mode::Probe);
    k.on_epoch(ep(rjl::HostState::Active, rjl::Reason::Heartbeat, 1, 0), 0);
    k.on_remote_begin(0);
    k.on_remote_over(0);
    uint64_t t = 0;
    for (int i = 0; i < 100; ++i, t += rjl::kRetryMs) (void)k.due(t);
    CHECK(k.phase() == rjl::Phase::Asking);
}

// ---------------------------------------------------------------- the per-conversation resets

void test_slot_table_generation() {
    rip::SlotTable<64> t;
    auto f = t.get(0x1000, 0xA0100001, 100, true);
    CHECK(f.slot && f.created);
    f.slot->d_fix.store(77);
    f = t.get(0x1000, 0xA0100001, 200, true);
    CHECK(f.slot && !f.created && f.slot->d_fix.load() == 77);  // the same actor: its state stays
    t.new_generation();
    f = t.get(0x1000, 0xA0100001, 300, true);
    CHECK(f.slot && f.slot->d_fix.load() == 0);  // same address, same key, a new conversation: from scratch
    CHECK(t.regenerated() == 1);
    f.slot->d_fix.store(5);
    f = t.get(0x1000, 0xA0100001, 400, true);
    CHECK(f.slot->d_fix.load() == 5);  // only once per generation
    // A slot of an old generation may be taken over by another controller even if it is fresh.
    rip::SlotTable<64> u;
    for (uintptr_t mc = 0x10; mc < 0x10 + 64 * 0x10; mc += 0x10) {
        (void)u.get(mc, 0xB0000000u + static_cast<uint32_t>(mc), 1, true);
    }
    u.new_generation();
    const auto g2 = u.get(0x999990, 0xB0999999, 2, true);
    CHECK(g2.slot != nullptr);
}

void test_windows_forget() {
    rip::EngageTable e;
    e.note(0xB0100005, 1000);
    CHECK(e.engaged(0xB0100005, 1100));
    e.forget();
    CHECK(!e.engaged(0xB0100005, 1100));
    rip::RidePairs p;
    p.note(0xA0100001, 0xB0100002, 1000);
    CHECK(p.rider_of(0xB0100002, 1100) == 0xA0100001);
    p.forget();
    CHECK(p.rider_of(0xB0100002, 1100) == 0);
}

void test_guard_table_generation() {
    s3::GuardTable<64> t;
    bool first = false;
    auto* s = t.touch(0xB0100010, 0x90100001, 100, first);
    CHECK(s && first && t.open() == 1);
    CHECK(t.recover(*s, 200) && t.open() == 0);
    CHECK(t.find(0xB0100010) == s);
    t.new_generation();
    CHECK(t.find(0xB0100010) == nullptr);  // the old actor's state is not this conversation's
    s = t.touch(0xB0100010, 0x90100002, 300, first);
    CHECK(s && first && s->recovered_qpc.load() == 0 && s->skips.load() == 1 && t.open() == 1);
    CHECK(s->control.load() == 0x90100002);
    CHECK(t.size() == 1);  // the same slot: the per-run bound counts keys, not conversations
    CHECK(t.find(0xB0100010) == s);
    s = t.touch(0xB0100010, 0x90100002, 400, first);
    CHECK(!first && s->skips.load() == 2);
}

void test_guest_hold_conversation() {
    using H = gsw::GuestHold;
    H h(true);
    h.prepare_started();
    h.offer_sent(0x100, 0);
    bool retry = false;
    gsw::Ack a;
    a.xfer = 0x100;
    a.result = static_cast<uint8_t>(gsw::AckResult::Ok);
    CHECK(h.on_ack(a, retry) && h.upload() == H::Upload::Acked);
    // Conversation 1: its goodbye holds behind the final save.
    auto d = h.on_send(gsw::kOpLogoutSessionReq, std::nullopt, 1000, /*final_ok=*/true, 0);
    CHECK(d.hold && d.save_first && h.final_done());
    CHECK(h.poll(1100, 1) == H::Release::FinalDone);
    // Without the reset the next conversation's goodbye would NOT save (once per run).
    d = h.on_send(gsw::kOpLogoutSessionReq, std::nullopt, 2000, true, 1);
    CHECK(!d.save_first);
    // With it: the final save once per conversation, and no new upload (the 3093 passes at once).
    h.new_conversation();
    CHECK(h.upload() == H::Upload::Acked && !h.final_done() && h.hold() == H::Hold::Off);
    d = h.on_send(gsw::kOpLoginCharacterReq, 0u, 3000, false, 1);
    CHECK(!d.hold && !d.reoffer);
    d = h.on_send(gsw::kOpLogoutSessionReq, std::nullopt, 4000, true, 1);
    CHECK(d.hold && d.save_first);
}

// Review of c4367f8, 3: another slot chosen at the title offers nothing in a re-entry; in the first conversation, as
// before.
void test_guest_hold_reentry_slot() {
    using H = gsw::GuestHold;
    gsw::Ack a;
    a.xfer = 0x100;
    a.result = static_cast<uint8_t>(gsw::AckResult::Ok);
    bool retry = false;
    {  // The first conversation: another slot is offered (as before).
        H h(true);
        h.prepare_started();
        h.offer_sent(0x100, 0);
        CHECK(h.on_ack(a, retry));
        const auto d = h.on_send(gsw::kOpLoginCharacterReq, 101u, 1000, false, 0);
        CHECK(d.reoffer == std::optional<uint32_t>(101u) && !d.slot_kept);
    }
    {  // A re-entry: the live slot stays, the 3093 is not held.
        H h(true);
        h.prepare_started();
        h.offer_sent(0x100, 0);
        CHECK(h.on_ack(a, retry));
        h.new_conversation();
        CHECK(h.reentry());
        auto d = h.on_send(gsw::kOpLoginCharacterReq, 101u, 1000, false, 0);
        CHECK(!d.reoffer && d.slot_kept == std::optional<uint32_t>(101u) && !d.hold);
        CHECK(h.upload() == H::Upload::Acked);
        d = h.on_send(gsw::kOpLoginCharacterReq, 101u, 2000, false, 0);  // the same slot again: said once
        CHECK(!d.reoffer && !d.slot_kept && !d.hold);
        d = h.on_send(gsw::kOpLoginCharacterReq, 0u, 3000, false, 0);  // its own slot: nothing to say
        CHECK(!d.reoffer && !d.hold);
    }
}

void test_outbound_discard() {
    msl::OutboundDrain dr;
    std::deque<std::vector<uint8_t>> src = {{1}, {2}, {3}};
    auto pop = [&](std::vector<uint8_t>& f) {
        if (src.empty()) return false;
        f = src.front();
        src.pop_front();
        return true;
    };
    // A refused push leaves one pending.
    size_t pushed = dr.drain(pop, [](const std::vector<uint8_t>&) { return false; }, true);
    CHECK(pushed == 0 && dr.pending() && src.size() == 2);
    CHECK(dr.discard(pop) == 3);  // the pending one and the two left
    CHECK(!dr.pending() && src.empty());
}

void test_sched_ledger_clear() {
    gsr::SchedLedger<8> l;
    CHECK(l.note(0x20000148, 0x1234, 0xA0100001, gsr::SchedWhere::X) != nullptr);
    CHECK(l.note(0x20000149, 0x5678, 0xA0100001, gsr::SchedWhere::M) != nullptr);
    CHECK(l.clear() == 2);
    CHECK(l.find(0x20000148) == nullptr && l.live(gsr::SchedWhere::X) == 0);
}

}  // namespace

int main() {
    test_frames();
    test_host_decide();
    test_guest_full_cycle();
    test_guest_refusals();
    test_guest_probe();
    test_local_connection_terminal();
    test_played_epoch_follows_host();
    test_silence_only();
    test_rejoin_off_identity();
    test_guest_hold_reentry_slot();
    test_slot_table_generation();
    test_windows_forget();
    test_guard_table_generation();
    test_guest_hold_conversation();
    test_outbound_discard();
    test_sched_ledger_clear();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("optd_rejoin: all checks passed\n");
    return 0;
}
