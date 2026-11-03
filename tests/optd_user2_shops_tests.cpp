// P8 option U, step U2 case A (docs/research/s25/tiendas-u2.md): the shop route's pure logic.
//
// SCOPE: offline logic only (user2_shops_logic.h):
//  - the keys (user2_stages_elements, user2_shop_route through u2s::parse_mode);
//  - the shop-scene name test and U-f's verdict for every combination (U-1's skip, element kind, funcnpc, the
//    elements key, the route live);
//  - the scene sites (opcodes, return addresses, where the id is: Reqs at +5, the 2044 timer at +3) and the id read
//    from real packets of the S13 / S12 captures;
//  - the route decision on fake managers (0x1F6C190's table, isl::find_stage): user 2's, the host's, both, unreadable;
//  - the 2916 / 3000 parse on the S13 smithy's bytes, the follow-up judge (followed / stuck / pending), the drain wait.
// They do NOT test the hooks, the game's code nor the server.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "mod/session2/user2_shops_logic.h"

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

struct FakeMem {
    std::map<uintptr_t, uint8_t> bytes;
    template <class T>
    void put(uintptr_t a, T v) {
        uint8_t b[sizeof(T)];
        std::memcpy(b, &v, sizeof(T));
        for (size_t i = 0; i < sizeof(T); ++i) bytes[a + i] = b[i];
    }
    template <class T>
    bool rd(uintptr_t a, T& v) const {
        uint8_t b[sizeof(T)];
        for (size_t i = 0; i < sizeof(T); ++i) {
            const auto it = bytes.find(a + i);
            if (it == bytes.end()) return false;
            b[i] = it->second;
        }
        std::memcpy(&v, b, sizeof(T));
        return true;
    }
};

// A fake ServerSequencerStageManager table (the layout 0x1F6C190 walks, as in optd_shop_c1_tests).
void build(FakeMem& m, uintptr_t mgr, const std::vector<uint64_t>& ids, bool dead_last = false) {
    const uintptr_t barr = mgr + 0x100000, elems = mgr + 0x200000;
    const uint32_t buckets = 8;
    m.put<uint32_t>(mgr + isl::kMgr_Buckets, buckets);
    m.put<uint32_t>(mgr + isl::kMgr_Count, static_cast<uint32_t>(ids.size()));
    m.put<uintptr_t>(mgr + isl::kMgr_BucketArray, barr);
    m.put<uintptr_t>(mgr + isl::kMgr_Elements, elems);
    std::vector<uint32_t> fill(buckets, 0);
    for (uint32_t b = 0; b < buckets; ++b) m.put<uint32_t>(barr + b * isl::kBucketStride, 0);
    for (uint32_t i = 0; i < ids.size(); ++i) {
        const uint32_t h = isl::hash_id(ids[i]);
        const uintptr_t bucket = barr + (h % buckets) * isl::kBucketStride;
        const uint32_t k = fill[h % buckets]++;
        m.put<uint32_t>(bucket, fill[h % buckets]);
        m.put<uint32_t>(bucket + 8 + 8 * k, h);
        m.put<uint32_t>(bucket + 0xC + 8 * k, i);
        const uintptr_t elem = mgr + 0x300000 + i * 0x100;
        const uintptr_t inst = mgr + 0x400000 + i * 0x1000;
        m.put<uintptr_t>(elems + 8 * i, elem);
        m.put<uint64_t>(elem + isl::kElem_Id, ids[i]);
        m.put<uintptr_t>(elem + isl::kElem_Inst, inst);
        m.put<uint8_t>(inst + isl::kInst_Dead, dead_last && i + 1 == ids.size() ? 1 : 0);
    }
}

std::vector<uint8_t> bytes_of(std::initializer_list<int> v) {
    std::vector<uint8_t> out;
    for (int b : v) out.push_back(static_cast<uint8_t>(b));
    return out;
}

void test_keys() {
    CHECK(u2sh::parse_elements("").elements == u2sh::Elements::Schedule);
    CHECK(u2sh::parse_elements("schedule").elements == u2sh::Elements::Schedule);
    CHECK(!u2sh::parse_elements("schedule").bad);
    const auto f = u2sh::parse_elements("schedule+funcnpc");
    CHECK(f.elements == u2sh::Elements::ScheduleFuncnpc && !f.bad);
    const auto bad = u2sh::parse_elements("all");
    CHECK(bad.bad && bad.elements == u2sh::Elements::Schedule);
    CHECK(u2s::parse_mode("probe").mode == u2sh::Mode::Probe);
    CHECK(u2s::parse_mode("1").mode == u2sh::Mode::On);
}

void test_names_and_verdict() {
    CHECK(u2sh::base_name("sequencer/funcnpc/cd_seq_funcnpc_smithy.paseq") == "cd_seq_funcnpc_smithy.paseq");
    CHECK(u2sh::base_name("a\\b\\cd_seq_funcnpc_pub") == "cd_seq_funcnpc_pub");
    CHECK(u2sh::base_name("cd_seq_funcnpc_butcher") == "cd_seq_funcnpc_butcher");
    CHECK(u2sh::is_funcnpc("cd_seq_funcnpc_butcher"));
    CHECK(u2sh::is_funcnpc("cd_seq_funcnpc_craftsmit"));  // the 40-byte log truncation of S12
    CHECK(!u2sh::is_funcnpc("cd_seq_funcnpc_"));
    CHECK(!u2sh::is_funcnpc("cd_seq_node_her_bluemont"));
    CHECK(!u2sh::is_funcnpc("cd_minigame_armwrest_bas"));
    CHECK(!u2sh::is_funcnpc(""));
    using V = u2sh::ElemVerdict;
    using E = u2s::Element;
    const auto S = u2sh::Elements::Schedule, SF = u2sh::Elements::ScheduleFuncnpc;
    // U-1 does not skip it: the native path, whatever it is.
    CHECK(u2sh::element_verdict(false, E::Sequencer, true, SF, true, false) == V::Original);
    CHECK(u2sh::element_verdict(false, E::Schedule, false, S, false, true) == V::Original);
    // U-1 skips it: only a shop scene with both keys, the route able to prepare, and far from the host gets through.
    CHECK(u2sh::element_verdict(true, E::Sequencer, true, SF, true, false) == V::PassFuncnpc);
    CHECK(u2sh::element_verdict(true, E::Sequencer, true, SF, true, true) == V::SkipNear);  // review A1
    CHECK(u2sh::element_verdict(true, E::Sequencer, true, SF, false, false) == V::SkipNoRoute);
    CHECK(u2sh::element_verdict(true, E::Sequencer, true, S, true, false) == V::SkipFuncnpc);
    CHECK(u2sh::element_verdict(true, E::Sequencer, false, SF, true, false) == V::Skip);
    CHECK(u2sh::element_verdict(true, E::Class2, true, SF, true, false) == V::Skip);  // a name never makes a faction pass
    static_assert(u2sh::kNewShopMinCells == 10);
    // And U-1's own skip (unchanged): mode 1, armed, the host's field, not a schedule.
    CHECK(u2s::skip_element(true, u2s::Mode::On, true, true, E::Sequencer));
    CHECK(!u2s::skip_element(true, u2s::Mode::Probe, true, true, E::Sequencer));
    CHECK(!u2s::skip_element(true, u2s::Mode::On, true, true, E::Schedule));
}

void test_sites() {
    static_assert(u2sh::kSiteCount == 15);
    for (size_t i = 0; i < u2sh::kSiteCount; ++i) {
        CHECK(u2sh::site_of(u2sh::kSites[i].op) == &u2sh::kSites[i]);
        CHECK(u2sh::site_index(u2sh::kSites[i].op) == i);
        for (size_t j = i + 1; j < u2sh::kSiteCount; ++j) CHECK(u2sh::kSites[i].op != u2sh::kSites[j].op);
    }
    CHECK(!u2sh::site_of(2671) && !u2sh::site_of(2720) && !u2sh::site_of(2100) && !u2sh::site_of(2043));
    CHECK(u2sh::site_index(2671) == u2sh::kSiteCount);
    CHECK(u2sh::site_of(2436)->ret_rva == 0x2A5C6CB);
    CHECK(u2sh::site_of(2591)->ret_rva == 0x2C4A8FC);
    CHECK(u2sh::site_of(2044)->ret_rva == 0x1082FBE7 && u2sh::site_of(2044)->id_at == u2sh::kTimerId);
    CHECK(u2sh::site_of(2138)->ret_rva == 0x1089F284);
    // The deferred delete (DeleteSequencerOnceTimer 2054): .xtls 0x10835C57, the id at +3 like the 2044.
    CHECK(u2sh::site_of(2054)->ret_rva == 0x10835C57 && u2sh::site_of(2054)->id_at == u2sh::kTimerId);
    CHECK(u2sh::kOpDeleteTimer == 2054);
    // S13 (the host's smithy 0x19D): a 2436 as a packet (u16 opcode | i8 slot | u16 length | payload).
    const auto b2436 = bytes_of({0x84, 0x09, 0x00, 0x0E, 0x00, 0x9D, 0x01, 0, 0, 0, 0, 0, 0, 0x06, 0, 0, 0, 0, 0});
    uint64_t id = 0;
    CHECK(u2sh::scene_id(2436, b2436, id) && id == 0x19D);
    // A 2767 (S13, 541.621) and a 2173.
    const auto b2767 = bytes_of({0xCF, 0x0A, 0x00, 0x12, 0x00, 0x9D, 0x01, 0, 0, 0, 0, 0, 0, 0x14, 0, 0, 0x20, 0x00, 0x02,
                                 0x00, 0x10, 0xB0, 0x01});
    CHECK(u2sh::scene_id(2767, b2767, id) && id == 0x19D);
    // The 2044 timer: u16 opcode | u8 | u64 id | u32 (0x2895820(mgr, packet+3, [packet+0xB])).
    const auto b2044 = bytes_of({0xFC, 0x07, 0xFF, 0xA7, 0x01, 0, 0, 0, 0, 0, 0, 0x06, 0, 0, 0});
    CHECK(u2sh::scene_id(2044, b2044, id) && id == 0x1A7);
    // The 2054 (16 bytes: u16 opcode | u8 | u64 id @3 | u8 flag @0xB | ...): 0x2879440(mgr, &res, packet+3, [+0xB]).
    const auto b2054 = bytes_of({0x06, 0x08, 0xFF, 0xC4, 0x02, 0, 0, 0, 0, 0, 0, 0x0E, 0, 0, 0, 0});
    CHECK(u2sh::scene_id(2054, b2054, id) && id == 0x2C4);
    // Too short, not a site, no id (2173's "u32 1 | u32 actor" form still gives a non-zero id: the lookup refuses it).
    CHECK(!u2sh::scene_id(2436, std::span<const uint8_t>(b2436.data(), 12), id));
    CHECK(!u2sh::scene_id(2671, b2436, id));
    const auto zero = bytes_of({0x84, 0x09, 0x00, 0x0E, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0});
    CHECK(!u2sh::scene_id(2436, zero, id));
}

void test_route() {
    FakeMem host, u2;
    const uintptr_t mh = 0x10000000, m2 = 0x50000000;
    build(host, mh, {0x197, 0x198, 0x19D, 0x1A5});
    build(u2, m2, {0x2C1, 0x2C4, 0x2D0}, /*dead_last=*/true);
    u2sh::RouteIn in;
    in.mode = u2sh::Mode::On;
    in.host_field = true;
    in.armed = true;
    in.mgr2_ok = true;
    // User 2's own shop scene: routed (mode 1), would (probe / aborted).
    in.in_mgr2 = isl::find_stage(u2, m2, 0x2C4);
    in.in_orig = isl::find_stage(host, mh, 0x2C4);
    CHECK(in.in_mgr2.found && in.in_orig.read_ok && !in.in_orig.found);
    CHECK(u2sh::decide_route(in) == u2sh::RouteOut::Routed);
    in.mode = u2sh::Mode::Probe;
    CHECK(u2sh::decide_route(in) == u2sh::RouteOut::Would);
    in.mode = u2sh::Mode::On;
    in.aborted = true;
    CHECK(u2sh::decide_route(in) == u2sh::RouteOut::Would);
    in.aborted = false;
    // The host's scene: the original.
    in.in_mgr2 = isl::find_stage(u2, m2, 0x19D);
    in.in_orig = isl::find_stage(host, mh, 0x19D);
    CHECK(u2sh::decide_route(in) == u2sh::RouteOut::NotInUser2);
    // A dead instance of user 2's: not found (0x1F6C190 returns null): the original.
    in.in_mgr2 = isl::find_stage(u2, m2, 0x2D0);
    CHECK(in.in_mgr2.dead && u2sh::decide_route(in) == u2sh::RouteOut::NotInUser2);
    // An id in both (impossible): InBoth.
    FakeMem both;
    build(both, mh, {0x2C1});
    in.in_mgr2 = isl::find_stage(u2, m2, 0x2C1);
    in.in_orig = isl::find_stage(both, mh, 0x2C1);
    CHECK(u2sh::decide_route(in) == u2sh::RouteOut::InBoth);
    // Unreadable: user 2's table or the original's.
    FakeMem empty;
    in.in_mgr2 = isl::find_stage(empty, m2, 0x2C1);
    CHECK(!in.in_mgr2.read_ok && u2sh::decide_route(in) == u2sh::RouteOut::Unreadable);
    in.in_mgr2 = isl::find_stage(u2, m2, 0x2C1);
    in.in_orig = isl::find_stage(empty, mh, 0x2C1);
    CHECK(u2sh::decide_route(in) == u2sh::RouteOut::Unreadable);
    // Not the host's field, no manager, the original already user 2's.
    in.in_orig = isl::find_stage(host, mh, 0x2C1);
    CHECK(u2sh::decide_route(in) == u2sh::RouteOut::Routed);
    in.host_field = false;
    CHECK(u2sh::decide_route(in) == u2sh::RouteOut::NotHostField);
    in.host_field = true;
    // Review M4: never without U-1 armed, never with user 2's goodbye asked (an id of user 2's: here 0x2C1).
    in.armed = false;
    CHECK(u2sh::decide_route(in) == u2sh::RouteOut::NotArmed);
    in.armed = true;
    in.logout = true;
    CHECK(u2sh::decide_route(in) == u2sh::RouteOut::NotArmed);
    // ... but a scene of the HOST is "not in user 2's" whether U-1 is armed or not (the step A run of ca1830a).
    in.armed = false;
    in.in_mgr2 = isl::find_stage(u2, m2, 0x19D);
    CHECK(u2sh::decide_route(in) == u2sh::RouteOut::NotInUser2);
    in.armed = true;
    in.in_mgr2 = isl::find_stage(u2, m2, 0x2C1);
    in.logout = false;
    in.mgr2_ok = false;
    CHECK(u2sh::decide_route(in) == u2sh::RouteOut::NoUser2Mgr);
    in.mgr2_ok = true;
    in.same_mgr = true;
    CHECK(u2sh::decide_route(in) == u2sh::RouteOut::NoUser2Mgr);
    in.same_mgr = false;
    in.mode = u2sh::Mode::Off;
    CHECK(u2sh::decide_route(in) == u2sh::RouteOut::Would);  // never Routed when off (the caller returns before)
}

void test_shop_watch() {
    // S13, 541.423: the 2916 of the smithy (requester A0100001, NPC B0100159, interaction 0x000F4321).
    const auto p2916 = bytes_of({0x01, 0x00, 0x10, 0xA0, 0x01, 0x00, 0x10, 0xA0, 0x59, 0x01, 0x10, 0xB0, 0x21, 0x43,
                                 0x0F, 0x00, 0, 0, 0, 0});
    const u2sh::StepAck a = u2sh::parse_step_ack(p2916);
    CHECK(a.ok && a.requester == 0xA0100001 && a.npc == 0xB0100159 && a.interaction == u2sh::kShopInteraction);
    CHECK(!u2sh::parse_step_ack(std::span<const uint8_t>(p2916.data(), 15)).ok);
    // A static interaction (S14, 0x000000AA) is not the shop's.
    auto other = p2916;
    other[12] = 0xAA;
    other[13] = other[14] = 0;
    CHECK(u2sh::parse_step_ack(other).interaction != u2sh::kShopInteraction);
    // S46 (ported from opcion-d-tenderos, S41 P2): the p2b host capture's real 3000 (offset 137772823). The failed
    // packet does NOT repeat the requester: the old parser read the interaction as the NPC and the error as the
    // interaction.
    const auto p3000 = bytes_of({0x01, 0x00, 0x10, 0xA0, 0xEC, 0x02, 0x10, 0xB0, 0x21, 0x43, 0x0F, 0x00, 0x5D, 0x34,
                                 0xC8, 0x58, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0x00});
    const auto fail = u2sh::parse_step_ack(p3000, u2sh::kOpPreemptionFail);
    CHECK(fail.ok && fail.requester == 0xA0100001 && fail.npc == 0xB01002EC);
    CHECK(fail.interaction == u2sh::kShopInteraction && fail.native_error == 0x58C8345D);
    CHECK(a.native_error == 0);
    for (size_t size = 0; size < 16; ++size) {
        CHECK(!u2sh::parse_step_ack(std::span<const uint8_t>(p3000.data(), size), u2sh::kOpPreemptionFail).ok);
    }
    CHECK(!u2sh::parse_step_ack(p3000, 65535).ok);
    CHECK(u2sh::parse_step_ack(p3000).interaction != u2sh::kShopInteraction);  // the old reading: not the shop
    const auto success_explicit = u2sh::parse_step_ack(p2916, u2sh::kOpDoStepOk);
    CHECK(success_explicit.ok && success_explicit.npc == a.npc && success_explicit.interaction == a.interaction);
    auto guest_fail = p3000;
    guest_fail[0] = 0x66;
    CHECK(u2sh::parse_step_ack(guest_fail, u2sh::kOpPreemptionFail).requester == 0xA0100066);
    // 2743 InteractionResetAck: the success layout (serializer 0x29E8AF0).
    CHECK(u2sh::shop_reset_matches(a, 0xB0100159, 0xA0100001));
    CHECK(!u2sh::shop_reset_matches(a, 0xB010015A, 0xA0100001));
    CHECK(!u2sh::shop_reset_matches(a, 0xB0100159, 0xA0100002));
    CHECK(!u2sh::shop_reset_matches(u2sh::parse_step_ack(other), 0xB0100159, 0xA0100001));
    // The judge: S13 followed 6 ms later; S14 never.
    const uint64_t t = 100'000'000;
    CHECK(u2sh::judge_open(0, 0, t) == u2sh::Judge::Pending);
    CHECK(u2sh::judge_open(t, t + 6'000, t + 10'000) == u2sh::Judge::Followed);
    CHECK(u2sh::judge_open(t, 0, t + 4'000'000) == u2sh::Judge::Pending);
    CHECK(u2sh::judge_open(t, 0, t + 5'000'001) == u2sh::Judge::Stuck);
    CHECK(u2sh::judge_open(t, t - 1, t + 6'000'000) == u2sh::Judge::Stuck);       // a Req before the 2916 does not count
    CHECK(u2sh::judge_open(t, t + 5'100'000, t + 6'000'000) == u2sh::Judge::Stuck);  // too late
    // The drain wait: from the 2916 for 120 s.
    CHECK(!u2sh::holding(0, t));
    CHECK(u2sh::holding(t, t));
    CHECK(u2sh::holding(t, t + 119'000'000));
    CHECK(!u2sh::holding(t, t + 120'000'000));
    CHECK(!u2sh::holding(t, t - 1));
    CHECK(u2sh::within(t, t + 1'000'000, 1'000'000));
    CHECK(!u2sh::within(t, t + 1'000'001, 1'000'000));
    CHECK(!u2sh::within(0, t, 1'000'000));
    // Review A1: the wait, only > 5 cells apart, once per arm (a later 2916 never renews it).
    using H = u2sh::Hold;
    u2sh::HoldIn h;
    h.now_us = t + 10'000'000;
    h.dist = 8;
    CHECK(u2sh::hold_decision(h) == H::None);
    h.open_us = t;
    CHECK(u2sh::hold_decision(h) == H::Wait);
    h.dist = 5;
    CHECK(u2sh::hold_decision(h) == H::TooNear);
    h.dist = UINT32_MAX;
    CHECK(u2sh::hold_decision(h) == H::TooNear);
    h.dist = 6;
    CHECK(u2sh::hold_decision(h) == H::Wait);
    h.hold_start_us = t;  // the wait began from that 2916
    h.open_us = t + 100'000'000;  // a later 2916
    h.now_us = t + 130'000'000;   // 130 s after the first: over, the later one does not renew it
    CHECK(u2sh::hold_decision(h) == H::Used);
    h.hold_start_us = 0;
    h.open_us = t;
    CHECK(u2sh::hold_decision(h) == H::Expired);
    // Review B4: the Req opcode of a Nak 1013 (u16 0x3F5 | u8 0xFF | i32 error | u16 opcode @7 | u64).
    const auto nak = bytes_of({0xF5, 0x03, 0xFF, 0x05, 0, 0, 0, 0x84, 0x09, 0, 0, 0, 0, 0, 0, 0, 0});
    CHECK(u2sh::nak_req_opcode(nak) == 2436 && u2sh::site_of(u2sh::nak_req_opcode(nak)));
    CHECK(u2sh::nak_req_opcode(std::span<const uint8_t>(nak.data(), 8)) == 0);
    CHECK(u2sh::nak_error(nak) == 5);  // S46: the i32 error @3
    CHECK(u2sh::nak_error(std::span<const uint8_t>(nak.data(), 8)) == 0);
    auto other_nak = nak;
    other_nak[0] = 0x01;
    CHECK(u2sh::nak_req_opcode(other_nak) == 0);
    for (int i = 0; i < static_cast<int>(u2sh::Abort::Count); ++i) {
        CHECK(std::strlen(u2sh::abort_name(static_cast<u2sh::Abort>(i))) > 0);
    }
}

}  // namespace

// S46 T1, the verdict only: measured on the shop elements U-f skips today; the exact trade list and the distance.
void test_t1() {
    using V = u2sh::ElemVerdict;
    CHECK(u2sh::t1_measured(V::SkipFuncnpc));
    CHECK(u2sh::t1_measured(V::SkipNoRoute));
    CHECK(!u2sh::t1_measured(V::PassFuncnpc));  // already let through (elements=schedule+funcnpc): not a verdict
    CHECK(!u2sh::t1_measured(V::Skip) && !u2sh::t1_measured(V::Original) && !u2sh::t1_measured(V::SkipNear));
    const auto trade = [](std::string_view n) { return u2sh::is_funcnpc(n) && u2trade::is_trade_resource(n); };
    CHECK(u2sh::t1_verdict(trade("cd_seq_funcnpc_smithy"), false) == u2sh::T1::WouldPass);
    CHECK(u2sh::t1_verdict(trade("cd_seq_funcnpc_butcher"), true) == u2sh::T1::Near);
    CHECK(u2sh::t1_verdict(trade("cd_seq_funcnpc_grocery"), false) == u2sh::T1::NotTrade);
    CHECK(u2sh::t1_verdict(trade("cd_seq_funcnpc_research_ptm_contact_00"), false) == u2sh::T1::NotTrade);
    for (int i = 0; i < static_cast<int>(u2sh::T1::Count); ++i) {
        CHECK(std::strlen(u2sh::t1_name(static_cast<u2sh::T1>(i))) > 0);
    }
    // With elements=schedule every shop element is SkipFuncnpc before anything else: T1 is measured, never prepared.
    CHECK(u2sh::element_verdict(true, u2s::Element::Sequencer, true, u2sh::Elements::Schedule, true, false) ==
          V::SkipFuncnpc);
}

// S46 phase 3, T1 ACTIVE: the list by the exact dictionary hash of the base name, instead of user2_stages_elements.
void test_t1_active() {
    using V = u2sh::ElemVerdict;
    using E = u2sh::Elements;
    // The dictionary hash (lookup3, seed 0xC5EDE): the two trade resources, and two others of funcnpc.json.
    CHECK(u2trade::dict_hash("cd_seq_funcnpc_smithy") == 0xA7132561u);
    CHECK(u2trade::dict_hash("cd_seq_funcnpc_butcher") == 0x978345CAu);
    CHECK(u2trade::dict_hash("cd_seq_funcnpc_grocery_pdem_contact_00") == 0x0E965A02u);
    CHECK(u2trade::dict_hash("cd_seq_funcnpc_research_ptm_contact_00") == 0xD6B8EB6Fu);
    CHECK(u2trade::is_trade_name_by_hash("cd_seq_funcnpc_smithy") && u2trade::is_trade_name_by_hash("cd_seq_funcnpc_butcher"));
    // Hernand's other shop scenes (p1b host.log): none is on the list.
    for (const char* n : {"cd_seq_funcnpc_bank", "cd_seq_funcnpc_church", "cd_seq_funcnpc_clothes",
                          "cd_seq_funcnpc_contribution", "cd_seq_funcnpc_craftsmith", "cd_seq_funcnpc_general",
                          "cd_seq_funcnpc_goblinbroker", "cd_seq_funcnpc_grocery", "cd_seq_funcnpc_leather",
                          "cd_seq_funcnpc_pub", "cd_seq_funcnpc_smithy_", "cd_seq_funcnpc_smith", ""}) {
        CHECK(!u2trade::is_trade_name_by_hash(n));
    }
    const auto seq = u2s::Element::Sequencer;
    // T1 on: the list decides whatever the elements key says.
    for (const E e : {E::Schedule, E::ScheduleFuncnpc}) {
        CHECK(u2sh::element_verdict(true, seq, true, e, true, false, true, true) == V::PassFuncnpc);
        CHECK(u2sh::element_verdict(true, seq, true, e, true, false, true, false) == V::SkipNotTrade);
        CHECK(u2sh::element_verdict(true, seq, true, e, true, true, true, true) == V::SkipNear);
        CHECK(u2sh::element_verdict(true, seq, true, e, false, false, true, true) == V::SkipNoRoute);
        CHECK(u2sh::element_verdict(true, seq, false, e, true, false, true, false) == V::Skip);  // not a funcnpc
        CHECK(u2sh::element_verdict(true, u2s::Element::Schedule, false, e, true, false, true, false) == V::Skip);
        CHECK(u2sh::element_verdict(false, seq, true, e, true, false, true, true) == V::Original);  // U-1 not skipping
    }
    // T1 off (0 / probe / latched): exactly the old verdicts.
    CHECK(u2sh::element_verdict(true, seq, true, E::Schedule, true, false, false, true) == V::SkipFuncnpc);
    CHECK(u2sh::element_verdict(true, seq, true, E::ScheduleFuncnpc, true, false, false, false) == V::PassFuncnpc);
    CHECK(u2sh::t1_list_on(u2s::Mode::On, false));
    CHECK(!u2sh::t1_list_on(u2s::Mode::On, true) && !u2sh::t1_list_on(u2s::Mode::Probe, false) &&
          !u2sh::t1_list_on(u2s::Mode::Off, false));
    CHECK(std::strlen(u2sh::elem_verdict_name(V::SkipNotTrade)) > 0);
    // The abort: a funcnpc of user 2's manager off the list, with T1 on (a hash 0 or a failed prepare never).
    CHECK(u2sh::t1_breach(true, true, true, 0x0E965A02u, 0));
    CHECK(!u2sh::t1_breach(true, true, true, 0xA7132561u, 0));
    CHECK(!u2sh::t1_breach(true, true, true, 0, 0) && !u2sh::t1_breach(true, true, true, 0x0E965A02u, 5));
    CHECK(!u2sh::t1_breach(false, true, true, 0x0E965A02u, 0) && !u2sh::t1_breach(true, false, true, 0x0E965A02u, 0));
    CHECK(!u2sh::t1_breach(true, true, false, 0x0E965A02u, 0));
    CHECK(std::strlen(u2sh::abort_name(u2sh::Abort::NotTradeList)) > 0);
}

int main() {
    test_t1();
    test_t1_active();
    test_keys();
    test_names_and_verdict();
    test_sites();
    test_route();
    test_shop_watch();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_user2_shops: all passed\n");
    return 0;
}
