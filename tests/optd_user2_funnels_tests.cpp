// P8 gameplay for user 2: the rank permit and the per-user owner funnels
// (src/mod/session2/user2_funnels_logic.h; docs/research/pendientes/p8-impl-rango-embudos.md).
//
// SCOPE: offline logic only. The three modes, the rank-permit decision for
// user 2's character, the subject verdict at ExecuteTroc, the funnel decision
// (shared field, user 2's own field, another caller, unknown keys, probe vs 1),
// the quest-manager chain on fake memory and the lock-free counters. They do
// NOT test the hooks, the call-site patch, the ABI, the byte checks against the
// running game nor the game's behaviour (G1/G9, live).

#include <cstdio>
#include <cstring>
#include <map>
#include <thread>
#include <vector>

#include "mod/session2/user2_funnels_logic.h"

using namespace cdmp::session2;
using u2f::FunnelOut;
using u2f::Mode;
using u2f::RankOut;
using u2f::SubjectVerdict;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

// Keys of the session-6B captures: host user 0x90100000, user 2 0x9010031E, its Kliff 0xA010031F.
constexpr uint32_t kHostUser = 0x90100000, kUser2 = 0x9010031E, kUser2Char = 0xA010031F;
constexpr uint32_t kHostChar = 0xA0100001, kNpc = 0xB0100320;

u2f::Keys live_keys() {
    u2f::Keys k;
    k.live = true;
    k.user2_user = kUser2;
    k.user2_char = kUser2Char;
    k.host_user = kHostUser;
    return k;
}

// ---------------------------------------------------------------- the option

void test_parse_mode() {
    struct Case {
        const char* raw;
        Mode mode;
        bool known;
    };
    const Case cases[] = {
        {"", Mode::Off, true},          {"0", Mode::Off, true},       {" off ", Mode::Off, true},
        {"probe", Mode::Probe, true},   {"PROBE\r", Mode::Probe, true}, {"probe ; G1", Mode::Probe, true},
        {"1", Mode::On, true},          {"on", Mode::On, true},       {"1 # live", Mode::On, true},
        {"2", Mode::Off, false},        {"force", Mode::Off, false},  {"probes", Mode::Off, false},
    };
    for (const Case& c : cases) {
        const u2f::ParsedMode p = u2f::parse_mode(c.raw);
        CHECK(p.mode == c.mode);
        CHECK(p.known == c.known);
        if (p.mode != c.mode || p.known != c.known) std::printf("  case '%s'\n", c.raw);
    }
    CHECK(u2f::vf40_caller_allowed(0x2C4A36F) && u2f::vf40_caller_allowed(0x2C4A0AD) &&
          u2f::vf40_caller_allowed(0x2B03AA7) && u2f::vf40_caller_allowed(0x2896347));  // + the fast-travel apply
    CHECK(!u2f::vf40_caller_allowed(0x2BC76C1) && !u2f::vf40_caller_allowed(0x2947F7B) &&
          !u2f::vf40_caller_allowed(0));  // trip end (2322 path), revive
    CHECK(std::strcmp(u2f::mode_name(Mode::Probe), "probe") == 0);
    CHECK(std::strcmp(u2f::mode_name(Mode::On), "1") == 0);
}

// ---------------------------------------------------------------- keys

void test_keys() {
    CHECK(u2f::is_user_key(kUser2) && !u2f::is_user_key(kUser2Char) && !u2f::is_user_key(0));
    CHECK(u2f::is_character_key(kUser2Char) && !u2f::is_character_key(kNpc) && !u2f::is_character_key(0));
    u2f::Keys k = live_keys();
    CHECK(u2f::keys_for_rank(k) && u2f::keys_for_funnels(k));
    CHECK(u2f::is_user2_character(k, kUser2Char, kUser2));
    CHECK(!u2f::is_user2_character(k, kUser2Char, kHostUser));  // its key, another controller
    CHECK(!u2f::is_user2_character(k, kHostChar, kUser2));       // a character user 2 controls that is not its own
    CHECK(!u2f::is_user2_character(k, kNpc, kUser2));            // its horse / child
    k.live = false;
    CHECK(!u2f::keys_for_rank(k) && !u2f::keys_for_funnels(k));
    k = live_keys();
    k.user2_char = 0;  // the MainPlayerDataStartAck not seen yet
    CHECK(!u2f::keys_for_rank(k));
    CHECK(!u2f::is_user2_character(k, 0, kUser2));
    k = live_keys();
    k.host_user = kUser2;  // never: the host and user 2 the same
    CHECK(!u2f::keys_for_funnels(k));
    k.host_user = 0;
    CHECK(!u2f::keys_for_funnels(k));
}

// ---------------------------------------------------------------- A: the rank permit

u2f::RankIn rank_in(Mode mode, uint32_t key, uint32_t ctrl, bool orig, bool comp) {
    u2f::RankIn in;
    in.mode = mode;
    in.keys = live_keys();
    in.arg = 5;
    in.read_ok = true;
    in.key = key;
    in.controller = ctrl;
    in.orig = orig;
    in.target_comp = comp;
    return in;
}

void test_rank() {
    // Off: always the original.
    for (bool orig : {false, true}) {
        const auto d = u2f::decide_rank(rank_in(Mode::Off, kUser2Char, kUser2, orig, true));
        CHECK(d.what == RankOut::Pass && d.result == orig && !d.user2);
    }
    // The host's own character, an NPC: the original, untouched, in every mode.
    for (Mode m : {Mode::Probe, Mode::On}) {
        for (bool orig : {false, true}) {
            auto d = u2f::decide_rank(rank_in(m, kHostChar, kHostUser, orig, true));
            CHECK(d.what == RankOut::NotUser2 && d.result == orig && !d.user2);
            d = u2f::decide_rank(rank_in(m, kNpc, kHostUser, orig, true));
            CHECK(d.what == RankOut::NotUser2 && d.result == orig);
        }
    }
    // User 2's character, the original already 1: unchanged.
    auto d = u2f::decide_rank(rank_in(Mode::On, kUser2Char, kUser2, true, true));
    CHECK(d.what == RankOut::OrigTrue && d.result && d.user2);
    // Probe: counted as "would", the original 0 stays.
    d = u2f::decide_rank(rank_in(Mode::Probe, kUser2Char, kUser2, false, true));
    CHECK(d.what == RankOut::Would && !d.result && d.user2);
    // 1: answers 1 ...
    d = u2f::decide_rank(rank_in(Mode::On, kUser2Char, kUser2, false, true));
    CHECK(d.what == RankOut::Override && d.result && d.user2);
    // ... but never with no target component (0x221E2B4 would pass a null C_X to GrantRankPermit).
    d = u2f::decide_rank(rank_in(Mode::On, kUser2Char, kUser2, false, false));
    CHECK(d.what == RankOut::NoComp && !d.result && d.user2);
    // Another argument than 5, X unreadable, user 2 not live, its keys unknown: the original.
    auto in = rank_in(Mode::On, kUser2Char, kUser2, false, true);
    in.arg = 4;
    CHECK(u2f::decide_rank(in).what == RankOut::Pass && !u2f::decide_rank(in).result);
    in = rank_in(Mode::On, kUser2Char, kUser2, false, true);
    in.read_ok = false;
    CHECK(u2f::decide_rank(in).what == RankOut::Pass && !u2f::decide_rank(in).result);
    in = rank_in(Mode::On, kUser2Char, kUser2, false, true);
    in.keys.live = false;
    CHECK(u2f::decide_rank(in).what == RankOut::Pass && !u2f::decide_rank(in).result);
    in = rank_in(Mode::On, kUser2Char, kUser2, false, true);
    in.keys.user2_char = 0;
    CHECK(u2f::decide_rank(in).what == RankOut::Pass && !u2f::decide_rank(in).result);
    // Its key under the HOST's controller (a stale key reused): not user 2's.
    d = u2f::decide_rank(rank_in(Mode::On, kUser2Char, kHostUser, false, true));
    CHECK(d.what == RankOut::NotUser2 && !d.result);
}

// ---------------------------------------------------------------- the subject

u2f::SubjectIn subject_in(uint32_t key, uint32_t ctrl, uintptr_t vt) {
    u2f::SubjectIn in;
    in.on = true;
    in.keys = live_keys();
    in.params_ok = true;
    in.actor = 0x10000000;
    in.read_ok = true;
    in.vtable = vt;
    in.expected_vtable = 0x145B1DE20;
    in.key = key;
    in.controller = ctrl;
    return in;
}

void test_subject() {
    CHECK(u2f::decide_subject(subject_in(kUser2Char, kUser2, 0x145B1DE20)) == SubjectVerdict::Subject);
    CHECK(u2f::decide_subject(subject_in(kHostChar, kHostUser, 0x145B1DE20)) == SubjectVerdict::NotUser2);
    CHECK(u2f::decide_subject(subject_in(kUser2Char, kHostUser, 0x145B1DE20)) == SubjectVerdict::NotUser2);
    CHECK(u2f::decide_subject(subject_in(kUser2Char, kUser2, 0x145B25000)) == SubjectVerdict::WrongClass);
    {
        auto c = subject_in(kUser2Char, kUser2, 0x145B1E2B0);  // ServerChildOnlyInGameActor via the second slot
        c.expected_vtable2 = 0x145B1E2B0;
        CHECK(u2f::decide_subject(c) == SubjectVerdict::Subject);
    }
    auto in = subject_in(kUser2Char, kUser2, 0x145B1DE20);
    in.on = false;
    CHECK(u2f::decide_subject(in) == SubjectVerdict::Off);
    in = subject_in(kUser2Char, kUser2, 0x145B1DE20);
    in.keys.live = false;
    CHECK(u2f::decide_subject(in) == SubjectVerdict::NotLive);
    in = subject_in(kUser2Char, kUser2, 0x145B1DE20);
    in.keys.host_user = 0;  // the host's key unknown: nothing to compare the field owner with
    CHECK(u2f::decide_subject(in) == SubjectVerdict::NotLive);
    in = subject_in(kUser2Char, kUser2, 0x145B1DE20);
    in.actor = 0;  // a timer's params without a character
    CHECK(u2f::decide_subject(in) == SubjectVerdict::NoActor);
    in = subject_in(kUser2Char, kUser2, 0x145B1DE20);
    in.params_ok = false;
    CHECK(u2f::decide_subject(in) == SubjectVerdict::NoActor);
    in = subject_in(kUser2Char, kUser2, 0x145B1DE20);
    in.read_ok = false;
    CHECK(u2f::decide_subject(in) == SubjectVerdict::Unreadable);
}

// ---------------------------------------------------------------- the funnels

u2f::FunnelIn funnel_in(Mode mode, uint32_t owner, bool subject = true, bool caller = true) {
    u2f::FunnelIn in;
    in.mode = mode;
    in.subject = subject;
    in.caller_ok = caller;
    in.keys = live_keys();
    in.owner_ok = true;
    in.owner = owner;
    return in;
}

void test_funnels() {
    // The shared field (owner = the host): user 2's in 1, counted in probe.
    CHECK(u2f::decide_funnel(funnel_in(Mode::On, kHostUser)) == FunnelOut::Substitute);
    CHECK(u2f::decide_funnel(funnel_in(Mode::Probe, kHostUser)) == FunnelOut::Probe);
    // No subject (the host's Req, a timer): the original, in every mode.
    CHECK(u2f::decide_funnel(funnel_in(Mode::On, kHostUser, false)) == FunnelOut::NoSubject);
    // Another caller of 0x2AFFF50 / 0x20DEE60 inside user 2's Req: the original.
    CHECK(u2f::decide_funnel(funnel_in(Mode::On, kHostUser, true, false)) == FunnelOut::OtherCaller);
    // User 2's own field (after a trip): already its own.
    CHECK(u2f::decide_funnel(funnel_in(Mode::On, kUser2)) == FunnelOut::OwnerUser2);
    // A field of another owner (none expected): the original.
    CHECK(u2f::decide_funnel(funnel_in(Mode::On, 0x90100400)) == FunnelOut::OwnerOther);
    CHECK(u2f::decide_funnel(funnel_in(Mode::On, 0)) == FunnelOut::OwnerOther);
    // The owner unreadable, the keys unknown: the original.
    auto in = funnel_in(Mode::On, kHostUser);
    in.owner_ok = false;
    CHECK(u2f::decide_funnel(in) == FunnelOut::Unreadable);
    in = funnel_in(Mode::On, kHostUser);
    in.keys.live = false;
    CHECK(u2f::decide_funnel(in) == FunnelOut::NoKeys);
    in = funnel_in(Mode::On, kHostUser);
    in.keys.host_user = 0;
    CHECK(u2f::decide_funnel(in) == FunnelOut::NoKeys);
    // Off never substitutes.
    CHECK(u2f::decide_funnel(funnel_in(Mode::Off, kHostUser)) != FunnelOut::Substitute);
    CHECK(u2f::decide_funnel(funnel_in(Mode::Off, kHostUser)) != FunnelOut::Probe);
}

// ---------------------------------------------------------------- the quest manager chain on fake memory

struct FakeMem {
    std::map<uintptr_t, uint64_t> q;  // 8-byte cells
    void put(uintptr_t a, uint64_t v) { q[a] = v; }
    bool u32(uintptr_t a, uint32_t& v) const {
        auto it = q.find(a);
        if (it == q.end()) return false;
        v = static_cast<uint32_t>(it->second);
        return true;
    }
    bool ptr(uintptr_t a, uintptr_t& v) const {
        auto it = q.find(a);
        if (it == q.end()) return false;
        v = static_cast<uintptr_t>(it->second);
        return true;
    }
};

void test_chain() {
    constexpr uintptr_t kCompVt = 0x145B1F8F8, kMgrVt = 0x145B1FA48;
    constexpr uintptr_t actor = 0x20000000, comps = 0x20001000, comp = 0x20002000, mgr = 0x20003000;
    FakeMem m;
    m.put(actor + 0x68, comps);
    m.put(comps + 0x138, comp);
    m.put(comp, kCompVt);
    m.put(comp + 0x18, mgr);
    m.put(mgr, kMgrVt);
    u2f::ChainFail why = u2f::ChainFail::Components;
    auto r = u2f::quest_manager_of(m, actor, kCompVt, kMgrVt, &why);
    CHECK(r && *r == mgr && why == u2f::ChainFail::None);
    CHECK(!u2f::quest_manager_of(m, 0, kCompVt, kMgrVt));
    // Each hop missing or of another class.
    FakeMem bad = m;
    bad.put(comp, 0x145B1F000);
    CHECK(!u2f::quest_manager_of(bad, actor, kCompVt, kMgrVt, &why) && why == u2f::ChainFail::ComponentClass);
    bad = m;
    bad.put(mgr, 0x145B1F8F8);
    CHECK(!u2f::quest_manager_of(bad, actor, kCompVt, kMgrVt, &why) && why == u2f::ChainFail::ManagerClass);
    bad = m;
    bad.put(comp + 0x18, 0);
    CHECK(!u2f::quest_manager_of(bad, actor, kCompVt, kMgrVt, &why) && why == u2f::ChainFail::Manager);
    bad = m;
    bad.put(comps + 0x138, 0);
    CHECK(!u2f::quest_manager_of(bad, actor, kCompVt, kMgrVt, &why) && why == u2f::ChainFail::Component);
    bad = m;
    bad.q.erase(actor + 0x68);
    CHECK(!u2f::quest_manager_of(bad, actor, kCompVt, kMgrVt, &why) && why == u2f::ChainFail::Components);
}

// ---------------------------------------------------------------- the counters

void test_counters() {
    static u2f::OpCounts ops;  // 16 KB: not on the stack
    for (int i = 0; i < 5; ++i) ops.add(2380);
    for (int i = 0; i < 3; ++i) ops.add(2112);
    ops.add(2372);
    ops.add(9999);  // out of range: opcode 0
    auto top = ops.top(2);
    CHECK(top.size() == 2 && top[0].first == 2380 && top[0].second == 5 && top[1].first == 2112);
    CHECK(ops.at(0) == 1 && ops.at(2372) == 1);
    CHECK(ops.top(10).size() == 4);

    static u2f::SiteCounts<4> sites;
    sites.add(0x2C4A36C);
    sites.add(0x2C4A36C);
    sites.add(0x2947F76);
    sites.add(0x2B03AA2);
    sites.add(0x2BC76BC);
    sites.add(0x2806B78);  // the fifth site: no slot left
    sites.add(0);          // no return RVA
    const auto rows = sites.rows();
    CHECK(rows.size() == 4);
    CHECK(!rows.empty() && rows[0].first == 0x2C4A36C && rows[0].second == 2);
    CHECK(sites.overflow() == 2);

    // Several threads at once: nothing lost, every key in one slot.
    static u2f::SiteCounts<64> shared;
    std::vector<std::thread> th;
    for (int t = 0; t < 4; ++t) {
        th.emplace_back([] {
            for (uint32_t i = 0; i < 10000; ++i) shared.add(0x1000 + (i % 8));
        });
    }
    for (auto& t : th) t.join();
    uint64_t total = 0;
    const auto r = shared.rows();
    for (const auto& [rva, n] : r) total += n;
    CHECK(r.size() == 8 && total == 40000 && shared.overflow() == 0);
}

}  // namespace

void test_quest_policy() {
    using u2f::Mode;
    using u2f::PolicyCaller;
    using u2f::PolicyOut;
    // Off, or not the host's field, or another caller: quest_per_user decides.
    CHECK(u2f::decide_quest_policy(Mode::Off, u2f::kRespawnVf40Ret, true, true) == PolicyOut::None);
    CHECK(u2f::decide_quest_policy(Mode::On, u2f::kRespawnVf40Ret, false, true) == PolicyOut::None);
    CHECK(u2f::decide_quest_policy(Mode::On, 0x2BC76BC, true, true) == PolicyOut::None);  // another caller
    CHECK(u2f::decide_quest_policy(Mode::On, 0x2896347, true, true) == PolicyOut::None);  // fast-travel step: as is
    // The callers kept for the host (S34: the guest's progress counts for the host's quests).
    for (const uint32_t r : {u2f::kRespawnVf40Ret, u2f::kTriggerVf40Ret, u2f::kQuestEventVf40Ret,
                             u2f::kChallengeDiscoverVf40Ret, u2f::kChallengeTimerVf40Ret, u2f::kChangeCharacterVf40Ret}) {
        CHECK(!u2f::caller_answers_own(u2f::policy_caller(r)));
        CHECK(u2f::decide_quest_policy(Mode::On, r, true, true) == PolicyOut::HostKept);
        CHECK(u2f::decide_quest_policy(Mode::On, r, true, false) == PolicyOut::HostKept);
        CHECK(u2f::decide_quest_policy(Mode::Probe, r, true, true) == PolicyOut::HostWould);
        CHECK(!u2f::quest_per_user_may_substitute(Mode::On, u2f::policy_caller(r)));
        CHECK(u2f::quest_per_user_may_substitute(Mode::Probe, u2f::policy_caller(r)));  // probe changes nothing
    }
    CHECK(u2f::policy_caller(0x290DC0B) == PolicyCaller::QuestEvent);
    CHECK(u2f::policy_caller(0x2C4A36F) == PolicyCaller::Trigger);
    // The callers user 2's own manager answers: 2372, 2477, 2354; unreadable is an ERR (the ingress drops first).
    for (const uint32_t r : {u2f::kTraceVf40Ret, u2f::kMissionUiVf40Ret, u2f::kMissionAlarmVf40Ret}) {
        CHECK(u2f::caller_answers_own(u2f::policy_caller(r)));
        CHECK(u2f::decide_quest_policy(Mode::On, r, true, true) == PolicyOut::OwnAnswered);
        CHECK(u2f::decide_quest_policy(Mode::On, r, true, false) == PolicyOut::OwnUnreadable);
        CHECK(u2f::decide_quest_policy(Mode::Probe, r, true, false) == PolicyOut::OwnWould);
        CHECK(u2f::quest_per_user_may_substitute(Mode::On, u2f::policy_caller(r)));
    }
    CHECK(u2f::quest_per_user_may_substitute(Mode::On, PolicyCaller::None));
    CHECK(u2f::decide_quest_policy(Mode::On, PolicyCaller::Count, true, true) == PolicyOut::None);
    // DeadActor stays native only with 1.
    CHECK(u2f::policy_keeps_dead_native(Mode::On));
    CHECK(!u2f::policy_keeps_dead_native(Mode::Probe) && !u2f::policy_keeps_dead_native(Mode::Off));
    // The event probe's actor classes and the host client's Acks.
    using u2f::EventActor;
    CHECK(u2f::classify_event_actor(0, 0xA0100200, 0xA0100001) == EventActor::Zero);
    CHECK(u2f::classify_event_actor(0xA0100200, 0xA0100200, 0xA0100001) == EventActor::User2);
    CHECK(u2f::classify_event_actor(0xA0100001, 0xA0100200, 0xA0100001) == EventActor::Host);
    CHECK(u2f::classify_event_actor(0xA0100001, 0xA0100200, 0) == EventActor::Other);
    CHECK(u2f::host_quest_ack_index(3009) == 0 && u2f::host_quest_ack_index(2346) == 3);
    CHECK(u2f::host_quest_ack_index(2808) == -1);
    for (size_t i = 0; i < static_cast<size_t>(PolicyCaller::Count); ++i) {
        CHECK(*u2f::policy_caller_name(static_cast<PolicyCaller>(i)) != '?');
    }
    // The ingress (review of 7da4dfa, M1): the route first, then the drop.
    using u2f::RouteFail;
    u2f::RouteIn ri;
    CHECK(u2f::trace_route(ri) == RouteFail::NotLive);
    ri.vf40_live = true;
    CHECK(u2f::trace_route(ri) == RouteFail::KeysNotLive);
    ri.keys_live = true;
    CHECK(u2f::trace_route(ri) == RouteFail::NotItsChar);
    ri.char_key = 0xA0100200;
    ri.user2_char = 0xA0100201;
    CHECK(u2f::trace_route(ri) == RouteFail::NotItsChar);
    ri.user2_char = 0xA0100200;
    CHECK(u2f::trace_route(ri) == RouteFail::NotItsChar);  // the subject test fails (vtable, controller)
    ri.subject_ok = true;
    CHECK(u2f::trace_route(ri) == RouteFail::OwnUnreadable);
    ri.own_ok = true;
    CHECK(u2f::trace_route(ri) == RouteFail::None);
    CHECK(!u2f::policy_drops_req(Mode::On, u2f::kOpTraceMission, RouteFail::None, false));
    CHECK(u2f::policy_drops_req(Mode::On, u2f::kOpTraceMission, RouteFail::NotLive, false));
    CHECK(u2f::policy_drops_req(Mode::On, u2f::kOpTraceMission, RouteFail::KeysNotLive, false));
    CHECK(!u2f::policy_drops_req(Mode::On, u2f::kOpTraceMission, RouteFail::NotLive, true));  // its own field
    CHECK(!u2f::policy_drops_req(Mode::Probe, u2f::kOpTraceMission, RouteFail::NotLive, false));
    CHECK(!u2f::policy_drops_req(Mode::On, u2f::kOpMissionTrigger, RouteFail::NotLive, false));  // 2112: never
    // 2477 / 2354 as 2372.
    for (const uint16_t op : {u2f::kOpMissionUiState, u2f::kOpMissionNewAlarm}) {
        CHECK(!u2f::policy_drops_req(Mode::On, op, RouteFail::None, false));
        CHECK(u2f::policy_drops_req(Mode::On, op, RouteFail::OwnUnreadable, false));
        CHECK(!u2f::policy_drops_req(Mode::On, op, RouteFail::OwnUnreadable, true));
        CHECK(u2f::policy_would_drop_req(Mode::Probe, op, RouteFail::OwnUnreadable, false));
        CHECK(!u2f::policy_would_drop_req(Mode::On, op, RouteFail::OwnUnreadable, false));
    }
    // 2369: always dropped with 1 (but in a field of its own); probe counts it.
    CHECK(u2f::policy_drops_req(Mode::On, u2f::kOpChangePlayable, RouteFail::None, false));
    CHECK(!u2f::policy_drops_req(Mode::On, u2f::kOpChangePlayable, RouteFail::None, true));
    CHECK(!u2f::policy_drops_req(Mode::Probe, u2f::kOpChangePlayable, RouteFail::None, false));
    CHECK(u2f::policy_would_drop_req(Mode::Probe, u2f::kOpChangePlayable, RouteFail::None, false));
    // The dialogs and stage choices: measured, never dropped.
    for (const uint16_t op : {u2f::kOpQuestDialogEnd, u2f::kOpSetQuestDialog, u2f::kOpStageBranchState,
                              u2f::kOpUpdateStageChartEvent}) {
        CHECK(u2f::policy_measured_req(op) && u2f::policy_relevant_req(op));
        CHECK(!u2f::policy_drops_req(Mode::On, op, RouteFail::NotLive, false));
    }
    CHECK(!u2f::policy_relevant_req(2556));  // MoveActorReq
    CHECK(!u2f::policy_relevant_req(2627));  // the cheats are blocked by the tunnel, not here
    for (size_t i = 0; i < static_cast<size_t>(RouteFail::Count); ++i) {
        CHECK(*u2f::route_fail_name(static_cast<RouteFail>(i)) != '?');
    }
    for (size_t i = 0; i < static_cast<size_t>(PolicyOut::Count); ++i) {
        CHECK(*u2f::policy_out_name(static_cast<PolicyOut>(i)) != '?');
    }
}

int main() {
    test_parse_mode();
    test_keys();
    test_rank();
    test_subject();
    test_funnels();
    test_chain();
    test_counters();
    test_quest_policy();
    if (g_failures) {
        std::printf("optd_user2_funnels: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("optd_user2_funnels: all passed\n");
    return 0;
}
