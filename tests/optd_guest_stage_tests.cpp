// P8 phase 2: user 2 as an observer of the field's stages/sequencers
// (src/mod/session2/guest_stage_diag_logic.h, the new parts of
// src/mod/session2/observer_stage_logic.h; docs/research/pendientes/p8-observador-escenas.md "Fase 2").
//
// SCOPE: offline logic only, on packets copied byte for byte from the P8
// session 5 captures (host's own client cdmp_packets_20261009-203910.bin, the
// guest's cdmp_packets_20261009-194117.bin). They do NOT test the hooks, the
// byte checks against the running game, the register reading nor the game's
// behaviour (static analysis of the document and a live session).

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "mod/session2/guest_stage_diag_logic.h"
#include "mod/session2/observer_stage_logic.h"

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

std::vector<uint8_t> unhex(const char* h) {
    std::vector<uint8_t> out;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; h[i] && h[i + 1]; i += 2) out.push_back(static_cast<uint8_t>(nib(h[i]) << 4 | nib(h[i + 1])));
    return out;
}

// Host capture #1680, t=50.420055: 2661 of sequencer 0x13, 8 bindings.
const char* kP1680 =
    "650affb30004010010a0010010a0130000000000000001007a4f0f00000000000ca0e5c47a4f0f000000000000000000"
    "00000000000000000000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
    "03000020010010a00500000c0000206d0010b005000005000020710010b005000007000020720010b005000008000020"
    "730010b005000009000020740010b00500000b000020750010b00500000d000020760010b0050000";
// #8483, t=50.493883: 2661 of 0x164, the owner's character only (a "global" sequencer).
const char* kP8483 =
    "650aff660004010010a0010010a06401000000000000010014480f0000000000382fa49c14480f002a7727c6a00a1b44"
    "eaa874c595ace1bf0000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
    "01000020010010a0050000";
// #52224, t=53.348262: 2661 of 0x197 (B0100213 / B0100214).
const char* kP52224 =
    "650aff7c0004010010a0010010a097010000000000000603d0600f00000000008006a07200000000000000000000000000"
    "000000000000000000000000000000fb0110b00000000000ffffff7f010000000000804c0f00ffffffffffffffff0403"
    "000020010010a005000005000020130210b005000006000020140210b0050000";
// 2981 pairs of those 2661s (#1679, #8482, #52223) and the 2981 that BOTH clients received:
// host #139918 t=210.833889 and guest #17684 t=74.079556, byte-identical.
const char* kB1679 = "a50bff1d0004010010a0130000000000000000000000ffffffff91ecb62200000000";
const char* kB8482 = "a50bff250004010010a0640100000000000000000000ffffffff91ecb622000000002539d7da00000000";
const char* kB52223 = "a50bff1d0004010010a0970100000000000002000000ffffffff91ecb62200000000";
const char* kBHost139918 = "a50bff250004010010a06401000000000000010000000100000091ecb622000000002539d7da08000000";
const char* kBGuest17684 = "a50bff250004010010a06401000000000000010000000100000091ecb622000000002539d7da08000000";
// #794, t=19.703625: the host's 2322 (its empty parking field).
const char* kM794 = "1209ff0400010010a0";

std::span<const uint8_t> payload_of(const std::vector<uint8_t>& raw) {
    auto pk = obs::split_packet(raw);
    return pk ? pk->payload : std::span<const uint8_t>{};
}

void test_prepare_parse() {
    const auto raw = unhex(kP1680);
    const auto pk = obs::split_packet(raw);
    CHECK(pk && pk->opcode == obs::kOpPrepare);
    const auto p = gsd::parse_prepare(pk->payload);
    CHECK(p.has_value());
    if (!p) return;
    CHECK(p->kind == 4);
    CHECK(p->actor == 0xA0100001u && p->owner_actor == 0xA0100001u);
    CHECK(p->seq == 0x13);
    CHECK(p->bindings.size() == 8);
    CHECK(gsd::required_actors(*p) == 8);  // every binding is type 5: all must be on the client first
    for (const gsd::WireBinding& b : p->bindings) CHECK(b.type == 5 && b.b1 == 0 && b.b2 == 0);
    CHECK(p->bindings[0].node == 0x20000003u && p->bindings[0].actor == 0xA0100001u);  // the owner's own character
    CHECK(p->bindings[1].node == 0x2000000Cu && p->bindings[1].actor == 0xB010006Du);
    CHECK(p->bindings[7].actor == 0xB0100076u);
    // The same parse as the phase-1 parser.
    const auto o = obs::parse_prepare_payload(pk->payload);
    CHECK(o && o->seq == p->seq && o->bindings.size() == p->bindings.size());
    // Its payload digest as the guest's hook computes it (byte by byte).
    CHECK(gsd::fnv1a64(pk->payload) == 0x3DE34A3B016B9685ull);

    const auto g = gsd::parse_prepare(payload_of(unhex(kP8483)));
    CHECK(g && g->seq == 0x164 && g->bindings.size() == 1 && g->bindings[0].actor == 0xA0100001u &&
          g->bindings[0].type == 5);
    const auto l = gsd::parse_prepare(payload_of(unhex(kP52224)));
    CHECK(l && l->seq == 0x197 && l->bindings.size() == 3 && l->bindings[1].actor == 0xB0100213u &&
          l->bindings[2].actor == 0xB0100214u && gsd::required_actors(*l) == 3);
    // Broken lengths.
    std::vector<uint8_t> shorter(pk->payload.begin(), pk->payload.end() - 1);
    CHECK(!gsd::parse_prepare(shorter).has_value());
    CHECK(!gsd::parse_prepare(std::span<const uint8_t>(pk->payload.data(), 90)).has_value());
}

void test_branch_parse_and_corr() {
    struct Row {
        const char* hex;
        uint64_t seq;
        uint32_t value;
        size_t size;
    } rows[] = {{kB1679, 0x13, 0, 29}, {kB8482, 0x164, 0, 37}, {kB52223, 0x197, 2, 29}, {kBHost139918, 0x164, 1, 37}};
    for (const Row& r : rows) {
        const auto raw = unhex(r.hex);
        const auto pk = obs::split_packet(raw);
        CHECK(pk && pk->opcode == obs::kOpBranch);
        const auto b = gsd::parse_branch(pk->payload);
        CHECK(b && b->kind == 4 && b->actor == 0xA0100001u && b->seq == r.seq && b->value == r.value &&
              b->size == r.size);
    }
    // The 2981 both clients received at the same moment: the same bytes, the same token on both PCs.
    const auto host = gsd::parse_branch(payload_of(unhex(kBHost139918)));
    const auto guest = gsd::parse_branch(payload_of(unhex(kBGuest17684)));
    CHECK(host && guest);
    if (host && guest) {
        CHECK(gsd::corr_token(obs::kOpBranch, host->actor, host->seq, host->value) ==
              gsd::corr_token(obs::kOpBranch, guest->actor, guest->seq, guest->value));
        CHECK(gsd::corr_token(obs::kOpBranch, guest->actor, guest->seq, guest->value) == "corr=2981:A0100001:0x164:0x1");
    }
    CHECK(gsd::corr_token(obs::kOpPrepare, 0xA0100001u, 0x13, 8) == "corr=2661:A0100001:0x13:0x8");
    const auto m = obs::parse_milestone_payload(payload_of(unhex(kM794)));
    CHECK(m && *m == 0xA0100001u);
    CHECK(gsd::corr_token(obs::kOpMilestone, *m, 0, 0) == "corr=2322:A0100001:0x0:0x0");
    CHECK(!gsd::parse_branch(std::span<const uint8_t>()).has_value());
}

void test_route() {
    // The guest in the host's field (session 5): its 2174 makes it the owner, so the handlers route to
    // user 2's character's manager; the sequencer belongs to the host's character A0100001.
    gsd::RouteFacts f;
    f.manager = 0x3B07621E400;  // the M P8Probe printed in session 5 (GL)
    f.manager_actor = 0xA0100334u;
    f.field_owner = 0x90100333u;
    f.seq_owner_actor = 0xA0100001u;
    f.seq_owner_manager = 0x2000;  // the host character's manager on the guest (another object)
    CHECK(gsd::classify_route(f) == gsd::Route::ObserverManager);
    f.seq_owner_manager = 0;  // unknown: decided by the keys
    CHECK(gsd::classify_route(f) == gsd::Route::ObserverManager);
    // The native owner case (the host's own client, or user 2 in its own field).
    gsd::RouteFacts o;
    o.manager = 0x1000;
    o.manager_actor = 0xA0100001u;
    o.seq_owner_actor = 0xA0100001u;
    CHECK(gsd::classify_route(o) == gsd::Route::OwnersManager);
    o.manager_actor = 0;
    o.seq_owner_manager = 0x1000;
    CHECK(gsd::classify_route(o) == gsd::Route::OwnersManager);
    gsd::RouteFacts none;
    CHECK(gsd::classify_route(none) == gsd::Route::NoManager);
    CHECK(std::string(gsd::route_text(gsd::Route::ObserverManager)).find("will NOT find it") != std::string::npos);
}

void test_result() {
    const uint32_t dup = 0x80000123u, missing = 0x80000456u;
    CHECK(gsd::classify_result(0, dup, missing) == gsd::Result::Created);
    CHECK(gsd::classify_result(missing, dup, missing) == gsd::Result::MissingBoundActor);
    CHECK(gsd::classify_result(dup, dup, missing) == gsd::Result::Duplicate);
    CHECK(gsd::classify_result(7, dup, missing) == gsd::Result::Other);
    CHECK(gsd::classify_result(7, 0, 0) == gsd::Result::Other);  // globals unreadable: never a false "created"
}

void test_managers() {
    gsd::ManagerBook book(2);
    auto c = book.ready(0x1000, 0xA0100334u, 1, 10);
    CHECK(!c.changed || c.before == -1);  // first sight
    c = book.ready(0x1000, 0xA0100334u, 1, 20);
    CHECK(!c.changed);
    c = book.ready(0x1000, 0xA0100334u, 2, 30);
    CHECK(c.changed && c.before == 1 && c.after == 2);
    c = book.ready(0x1000, 0, -1, 40);  // unreadable: no change, the last value kept
    CHECK(!c.changed && book.find(0x1000)->ready == 2 && book.find(0x1000)->actor == 0xA0100334u);
    book.set_routed(0x1000);
    CHECK(book.last_routed() && book.last_routed()->ptr == 0x1000);
    book.touch(0x2000, 0xA0100001u, 50);
    book.touch(0x3000, 0xA0100002u, 60);  // past the cap: the oldest is forgotten
    CHECK(book.all().size() == 2 && !book.find(0x1000) && book.find(0x3000));
    CHECK(!book.last_routed());
}

void test_npc_tracker() {
    // The NPC side as 0x9ED290 runs it: local_res8 = (u32 lo, u32 hi); lo 0 or hi -1 = exit.
    CHECK(!gsd::NpcTracker::res8_passes(0));
    CHECK(!gsd::NpcTracker::res8_passes(0xFFFFFFFF00000005ull));
    CHECK(gsd::NpcTracker::res8_passes(0x0000000700000005ull));
    gsd::NpcTracker t(3);
    // B010006D (seq 0x13): entered, exits at local_res8 (what session 5 suggests: 0x9ED431 never ran).
    if (gsd::NpcBind* b = t.step(0xB010006Du, gsd::NpcStep::Entry, 1)) {
        b->seq_owner = 0xA0100001u;
        b->seq_key = 0x13;
    }
    t.res8(0xB010006Du, 0, 2);
    // B0100079: passes, the owner is found but its manager is not the routed one, no instance.
    t.step(0xB0100079u, gsd::NpcStep::Entry, 3);
    t.res8(0xB0100079u, 0x0000000100000002ull, 4);
    if (gsd::NpcBind* b = t.step(0xB0100079u, gsd::NpcStep::OwnerFound, 5)) {
        b->seq_owner = 0xA0100001u;
        b->owner_manager = 0x2000;
        b->owner_manager_routed = false;
    }
    // An NPC of the guest's own field: found and bound.
    t.step(0xB0100500u, gsd::NpcStep::Entry, 6);
    t.res8(0xB0100500u, 0x0000000100000002ull, 7);
    if (gsd::NpcBind* b = t.step(0xB0100500u, gsd::NpcStep::OwnerFound, 8)) {
        b->seq_owner = 0xA0100334u;
        b->owner_manager_routed = true;
    }
    t.step(0xB0100500u, gsd::NpcStep::Instance, 9);
    // Past the cap: counted, not kept.
    CHECK(t.step(0xB0100600u, gsd::NpcStep::Entry, 10) == nullptr && t.overflow() == 1);
    CHECK(t.step(0, gsd::NpcStep::Entry, 11) == nullptr);
    const auto s = t.summary();
    CHECK(s.npcs == 3 && s.with_owner == 3);
    CHECK(s.passed_res8 == 2 && s.exited_res8 == 1 && s.owner_found == 2 && s.instance == 1);
    CHECK(s.owner_manager_routed == 1);
    CHECK(s.owners.at(0xA0100001u) == 2 && s.owners.at(0xA0100334u) == 1);
    CHECK(t.get(0xB010006Du)->res8_exit == 1 && t.get(0xB010006Du)->res8_pass == 0);
}

// The host ledger's phase-2 additions: invalidation at the owner's field change and the native order.
obs::SeqEntry entry(uint64_t seq, uint64_t t, uint32_t user = 0x90100000u) {
    obs::SeqEntry e;
    e.actor = 0xA0100001u;
    e.user = user;
    e.owner_actor = 0xA0100001u;
    e.seq = seq;
    e.t_us = t;
    e.bindings = {{0x20000003u, 0xA0100001u}, {0x2000000Cu, static_cast<uint32_t>(0xB0100000u + seq)}};
    return e;
}

void test_ledger_invalidation_and_order() {
    obs::Ledger l(16);
    // Prepared in the old field (t 100..120), then the owner enters a new field at t=500.
    l.prepare(entry(0x10, 100));
    l.prepare(entry(0x11, 110));
    l.prepare(entry(0x12, 120, 0x90100333u));  // user 2's own: untouched by the owner's change
    CHECK(l.drop_user_before(0x90100000u, 500) == 2);
    CHECK(l.live() == 1 && l.invalidated() == 2 && l.get(0xA0100001u, 0x12));
    CHECK(l.drop_user_before(0, 500) == 0);
    // The new field's sequencers, as the host's client received them: 2981 then 2661 (same ms), in
    // preparation order; one without a 2981.
    l.prepare(entry(0x19, 650));
    l.branch(0xA0100001u, 0x19, 0, 649);
    l.prepare(entry(0x13, 600));
    l.branch(0xA0100001u, 0x13, 0, 599);
    l.prepare(entry(0x16C, 700));
    // Prepared after the entry: kept by a second drop at the same entry time.
    CHECK(l.drop_user_before(0x90100000u, 500) == 0);
    const obs::ObserverNeed n = l.need(0x90100000u, 0x90100333u);
    CHECK(n.missing.size() == 3);
    const std::vector<obs::PlanStep> order = obs::native_order(n);
    CHECK(order.size() == 6);
    if (order.size() == 6) {
        CHECK(order[0].opcode == obs::kOpBranch && order[0].seq == 0x13);
        CHECK(order[1].opcode == obs::kOpPrepare && order[1].seq == 0x13);
        CHECK(order[2].opcode == obs::kOpBranch && order[2].seq == 0x19);
        CHECK(order[3].opcode == obs::kOpPrepare && order[3].seq == 0x19);
        CHECK(order[4].opcode == obs::kOpPrepare && order[4].seq == 0x16C);
        CHECK(order[5].opcode == obs::kOpMilestone);
    }
    // A 2810 removes it from the plan.
    l.remove(0xA0100001u, 0x19);
    const obs::ObserverNeed n2 = l.need(0x90100000u, 0x90100333u);
    CHECK(n2.missing.size() == 2 && obs::native_order(n2).size() == 4);
    // Empty: only the milestone.
    obs::Ledger empty;
    CHECK(obs::native_order(empty.need(0x90100000u, 0x90100333u)).size() == 1);
}

// The real host sequence of session 5 through the ledger with the invalidation: the owner's two
// entries (parking field t=19.70, world field t=49.57) come before any 2661, so nothing is dropped
// and the observer's entry (t=195.206) still lacks the 17 live sequencers phase 1 counted.
void test_real_sequence_with_invalidation() {
    obs::Ledger l;
    CHECK(l.drop_user_before(0x90100000u, 19'704'000) == 0);
    CHECK(l.drop_user_before(0x90100000u, 49'569'000) == 0);
    const char* prepares[] = {kP1680, kP8483, kP52224};
    uint64_t t = 50'420'000;
    for (const char* h : prepares) {
        const auto p = gsd::parse_prepare(payload_of(unhex(h)));
        CHECK(p.has_value());
        if (!p) continue;
        obs::SeqEntry e;
        e.actor = p->actor;
        e.user = 0x90100000u;
        e.owner_actor = p->owner_actor;
        e.seq = p->seq;
        e.t_us = t;
        for (const auto& b : p->bindings) e.bindings.push_back({b.node, b.actor});
        e.bindings_total = static_cast<uint32_t>(p->bindings.size());
        l.prepare(e);
        t += 1000;
    }
    const auto n = l.need(0x90100000u, 0x90100333u, std::vector<uint32_t>{0xA0100001u});
    CHECK(n.missing.size() == 3 && n.bound_actors.size() == 9);  // 7 NPCs of 0x13, none of 0x164, 2 of 0x197
    const auto order = obs::native_order(n);
    CHECK(order.size() == 4 && order.back().opcode == obs::kOpMilestone);
}

}  // namespace

int main() {
    test_prepare_parse();
    test_branch_parse_and_corr();
    test_route();
    test_result();
    test_managers();
    test_npc_tracker();
    test_ledger_invalidation_and_order();
    test_real_sequence_with_invalidation();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_guest_stage: all tests passed\n");
    return 0;
}
