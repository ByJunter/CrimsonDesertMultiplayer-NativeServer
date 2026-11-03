// P8 session 13 (docs/research/pendientes/p8-impl-secuenciador-bodyanim.md).
//
// SCOPE: offline logic only.
//  - guest_seq_route_logic.h: guest_seq_route_2524's modes (legacy / probe / 1, with guest_seq_route and the old
//    guest_seq_play_live switch), when the 2524 point is installed, the world classification of a 2524's scene class by
//    class on the session 12 scenes (0x19 Strangers, 0x16C guards, 0x197 butcher, 0x164 only the host, 0x163 pigeon,
//    0x213 the denied cutscene), the function-scene names, the raw name decoding, the routing verdict with the world
//    check (and its place in the order), the 2661 ledger with its binds.
//  - guest_bodyanim_diag_logic.h: the caller of 0x2432F60 by return address, the G5 body states (graph, seq_bodyanim
//    without graph, wait, other) and the summary's cohort.
// They do NOT test the hooks, the reads of the game's memory, the threads nor the game's behaviour.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "mod/session2/guest_bodyanim_diag_logic.h"
#include "mod/session2/guest_seq_route_logic.h"

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

constexpr uint32_t kHost = 0xA0100001, kUser2Char = 0xA0100336;

void pack_name(const char* s, uint64_t q[3]) {
    q[0] = q[1] = q[2] = 0;
    const size_t n = std::strlen(s);
    for (size_t i = 0; i < n && i < 24; ++i) q[i / 8] |= static_cast<uint64_t>(static_cast<unsigned char>(s[i])) << (8 * (i % 8));
}

void test_modes() {
    using gsr::Mode;
    using gsr::Mode2524;
    bool bad = false;
    CHECK(gsr::parse_mode_2524("", &bad) == Mode2524::Legacy && !bad);
    CHECK(gsr::parse_mode_2524("0", &bad) == Mode2524::Legacy && !bad);
    CHECK(gsr::parse_mode_2524("probe", &bad) == Mode2524::Probe && !bad);
    CHECK(gsr::parse_mode_2524("1", &bad) == Mode2524::On && !bad);
    CHECK(gsr::parse_mode_2524("yes", &bad) == Mode2524::Legacy && bad);
    // Legacy: exactly the old rule (into X only with guest_seq_route=1 and the guest_seq_play_live switch).
    CHECK(gsr::mode_2524(Mode::On, Mode2524::Legacy, true, true) == Mode::On);
    CHECK(gsr::mode_2524(Mode::On, Mode2524::Legacy, false, true) == Mode::Probe);
    CHECK(gsr::mode_2524(Mode::Probe, Mode2524::Legacy, true, true) == Mode::Probe);
    CHECK(gsr::mode_2524(Mode::Off, Mode2524::Legacy, true, true) == Mode::Off);
    CHECK(gsr::mode_2524(Mode::On, Mode2524::Legacy, true, false) == Mode::On);  // legacy_routes already implies it
    // probe never routes, whatever guest_seq_play_live says; 1 is independent of it but needs guest_seq_route=1
    // and guest_seq_play=1 (review of 5f30a52: in probe nothing ticks X, though guest_seq_play_knows_x is true).
    CHECK(gsr::mode_2524(Mode::On, Mode2524::Probe, true, true) == Mode::Probe);
    CHECK(gsr::mode_2524(Mode::On, Mode2524::Probe, false, true) == Mode::Probe);
    CHECK(gsr::mode_2524(Mode::On, Mode2524::On, false, true) == Mode::On);
    CHECK(gsr::mode_2524(Mode::On, Mode2524::On, false, false) == Mode::Probe);  // guest_seq_play probe / 0 / off
    CHECK(gsr::mode_2524(Mode::Probe, Mode2524::On, false, true) == Mode::Probe);
    CHECK(gsr::mode_2524(Mode::Off, Mode2524::On, true, true) == Mode::Off);
    // The point: the old switch alone (session 12: _live=0 -> not installed), or the new key alone.
    CHECK(!gsr::want_point_2524(false, Mode2524::Legacy));
    CHECK(gsr::want_point_2524(true, Mode2524::Legacy));
    CHECK(gsr::want_point_2524(false, Mode2524::Probe));
    CHECK(gsr::want_point_2524(false, Mode2524::On));
    CHECK(!gsr::need_world_2524(Mode2524::Legacy) && gsr::need_world_2524(Mode2524::Probe) &&
          gsr::need_world_2524(Mode2524::On));
}

// A 2524 scene as the hook sees it: a world scene with type-3 NPCs present and its name read.
gsr::Scene2524Facts world_scene() {
    gsr::Scene2524Facts s;
    s.known = true;
    s.resource = 0xBD40FBCF;  // session 12: 0x19 (Stranger 0xB0100077 / 78)
    s.binds = 3;
    s.npcs = 2;
    s.npcs_found = 2;
    s.type3 = 2;
    s.name_ok = true;
    return s;
}

void test_classify() {
    using gsr::World2524;
    const gsr::Scene2524Facts w = world_scene();
    CHECK(gsr::classify_2524(w) == World2524::World);
    gsr::Scene2524Facts s = w;
    s.known = false;  // no 2661 seen (prepared before this client, or evicted): never routed
    CHECK(gsr::classify_2524(s) == World2524::UnknownSeq);
    s = w, s.denied = true;  // 0x213 / 0x8CD771B1, the camp cutscene of session 9
    CHECK(gsr::classify_2524(s) == World2524::Denied);
    s = w, s.binds = 1, s.npcs = 0, s.npcs_found = 0, s.type3 = 0;  // 0x164/0x165/0x167: only 0xA0100001
    CHECK(gsr::classify_2524(s) == World2524::NoNpc);
    s = w, s.npcs_found = 0, s.type3 = 0;  // its NPCs left this client
    CHECK(gsr::classify_2524(s) == World2524::NoNpcHere);
    s = w, s.npcs = 1, s.npcs_found = 1, s.type3 = 0;  // 0x163 Homing Pigeon (fauna)
    CHECK(gsr::classify_2524(s) == World2524::NoType3);
    s = w, s.name_ok = false;  // fail closed: it could be a shop
    CHECK(gsr::classify_2524(s) == World2524::NameUnknown);
    s = w, s.function = true;  // 0x197 cd_seq_funcnpc_butcher (B0100156, type 3)
    CHECK(gsr::classify_2524(s) == World2524::Function);
    s = w, s.allow_empty = false, s.allowed = false;
    CHECK(gsr::classify_2524(s) == World2524::NotAllowed);
    s.allowed = true;
    CHECK(gsr::classify_2524(s) == World2524::World);
    // The order: denied before anything about NPCs, the function name before the allow list.
    s = w, s.denied = true, s.function = true, s.npcs = 0;
    CHECK(gsr::classify_2524(s) == World2524::Denied);
    s = w, s.function = true, s.allow_empty = false, s.allowed = true;
    CHECK(gsr::classify_2524(s) == World2524::Function);
    for (size_t k = 0; k < static_cast<size_t>(World2524::Count); ++k) {
        CHECK(std::string_view(gsr::world_2524_name(static_cast<World2524>(k))) != "?");
    }
}

void test_names() {
    // The session 12 shop scenes (GuestXVeto lines, 24-byte names).
    CHECK(gsr::is_function_scene("cd_seq_funcnpc_butcher"));
    CHECK(gsr::is_function_scene("cd_seq_funcnpc_seatdeale"));
    CHECK(gsr::is_function_scene("CD_SEQ_FUNCNPC_PUB"));
    CHECK(gsr::is_function_scene("cd_seq_funcnpc"));
    CHECK(!gsr::is_function_scene("cd_seq_funcnp"));
    CHECK(!gsr::is_function_scene("cd_seq_spawn_docu_nhm_1_sit_lean_01"));
    CHECK(!gsr::is_function_scene("cd_seq_quest_splithorn_boss_0100_schedule"));
    CHECK(!gsr::is_function_scene(""));
    uint64_t q[3];
    char out[32];
    pack_name("cd_seq_funcnpc_seatdeale", q);  // exactly 24 bytes, no NUL inside the read
    CHECK(gsr::scene_name(q, 3, out, sizeof out) && std::string_view(out) == "cd_seq_funcnpc_seatdeale");
    pack_name("npc_city_00_0001_02", q);
    CHECK(gsr::scene_name(q, 3, out, sizeof out) && std::string_view(out) == "npc_city_00_0001_02");
    q[0] = q[1] = q[2] = 0;  // nothing read
    CHECK(!gsr::scene_name(q, 3, out, sizeof out) && out[0] == 0);
    q[0] = 0x0000000000017F41ull;  // 'A', 0x7F, 0x01: not a name (a wrong pointer chain)
    CHECK(!gsr::scene_name(q, 3, out, sizeof out));
    CHECK(!gsr::scene_name(q, 3, out, 0));
    char small[4];
    pack_name("abcdefgh", q);  // truncated to the buffer, still a name
    CHECK(gsr::scene_name(q, 3, small, sizeof small) && std::string_view(small) == "abc");
    CHECK(std::string_view(gsr::name_via_name(gsr::NameVia::Instance)) == "inst");
    CHECK(std::string_view(gsr::name_via_name(gsr::NameVia::None)) == "none");
}

// A routable 2524 of the host's Kliff (the 2524 facts the hook fills).
gsr::RouteFacts base_2524(gsr::Mode mode) {
    gsr::RouteFacts f;
    f.mode = mode;
    f.point_on = true;
    f.m = 0x1000;
    f.m_key = kUser2Char;
    f.actor = 0x2000;
    f.actor_key = kHost;
    f.packet_key = kHost;
    f.x = 0x3000;
    f.x_vt = f.want_vt = 0x1455C9720;
    f.x_key = kHost;
    f.need_known = true;
    f.x_known = true;
    f.ready2524 = static_cast<uint8_t>(gsr::Ready2524::Ok);
    return f;
}

void test_verdict() {
    using gsr::Verdict;
    // Legacy (no world check): as before.
    gsr::RouteFacts f = base_2524(gsr::Mode::On);
    CHECK(gsr::decide(f) == Verdict::Route);
    // guest_seq_route_2524: a world scene goes on to the usual checks; any other is left native.
    f.need_world = true, f.world_ok = true;
    CHECK(gsr::decide(f) == Verdict::Route);
    gsr::RouteFacts p = f;
    p.mode = gsr::Mode::Probe;
    CHECK(gsr::decide(p) == Verdict::WouldRoute);
    f.world_ok = false;
    CHECK(gsr::decide(f) == Verdict::NotWorld2524);
    p.world_ok = false;
    CHECK(gsr::decide(p) == Verdict::NotWorld2524);
    // Order: the deny list first (its own verdict), then the world check, then the X / scene checks.
    gsr::RouteFacts d = f;
    d.denied = true;
    CHECK(gsr::decide(d) == Verdict::Denied);
    gsr::RouteFacts n = f;  // not world AND X not ticked: the world check speaks first
    n.x_known = false;
    CHECK(gsr::decide(n) == Verdict::NotWorld2524);
    n.world_ok = true;  // world, but X not ticked by guest_seq_play: never routed
    CHECK(gsr::decide(n) == Verdict::NotPlayed);
    gsr::RouteFacts r = f;
    r.world_ok = true;
    r.ready2524 = static_cast<uint8_t>(gsr::Ready2524::NotPlaying);  // the session 10 crash state
    CHECK(gsr::decide(r) == Verdict::NotPlaying2524);
    // The own character's 2524 and an NPC key are never judged by it.
    gsr::RouteFacts o = f;
    o.actor_key = o.packet_key = kUser2Char;
    CHECK(gsr::decide(o) == Verdict::OwnCharacter);
    o = f, o.actor_key = o.packet_key = 0xB0100077;
    CHECK(gsr::decide(o) == Verdict::NotCharacter);
    // A 2524 is never refused (it stays native in M) whatever the class.
    CHECK(!gsr::refuse(gsr::Point::P2524, f, Verdict::NotWorld2524));
    CHECK(std::string_view(gsr::verdict_name(Verdict::NotWorld2524)) != "?");
    // The other points never get the world check (need_world only for the 2524).
    gsr::RouteFacts g = base_2524(gsr::Mode::On);
    g.need_known = false;
    CHECK(gsr::decide(g) == Verdict::Route);
}

void test_seq_ledger() {
    gsr::SeqResources<4> r;
    const uint32_t binds19[] = {kHost, 0xB0100077, 0xB0100078};
    r.note(0x19, 0xBD40FBCF, false, binds19);
    const auto* e = r.find(0x19);
    CHECK(e && e->resource == 0xBD40FBCF && !e->denied && e->nbinds == 3 && e->binds[1] == 0xB0100077);
    // Re-noted (a new 2661 of the same id): replaced, binds included.
    const uint32_t binds19b[] = {kHost};
    r.note(0x19, 0x11111111, true, binds19b);
    e = r.find(0x19);
    CHECK(e && e->resource == 0x11111111 && e->denied && e->nbinds == 1 && e->binds[1] == 0);
    // Without binds (the old call): none kept.
    r.note(0x13, 0xC4E5A00C, false);
    CHECK(r.find(0x13) && r.find(0x13)->nbinds == 0);
    // More than kSeqBinds: the first 8 kept (0x1A2 binds 8, 0x13 binds 8).
    uint32_t many[12];
    for (uint32_t i = 0; i < 12; ++i) many[i] = 0xB0100C00 + i;
    r.note(0x1A2, 0xAAAAAAAA, false, many);
    CHECK(r.find(0x1A2) && r.find(0x1A2)->nbinds == gsr::kSeqBinds && r.find(0x1A2)->binds[7] == 0xB0100C07);
    // A ring: the oldest goes.
    r.note(0x16C, 1, false);
    r.note(0x1BD, 2, false);
    CHECK(!r.find(0x19) && r.find(0x1BD) && !r.find(0));
}

void test_bodyanim() {
    CHECK(gba::configure_layer(0xA79C24) == 0);
    CHECK(gba::configure_layer(0xA79FCA) == 1);
    CHECK(gba::configure_layer(0xA79C1F) == -1);  // the call itself, not its return address
    CHECK(gba::configure_layer(0) == -1);
    CHECK(gba::is_seq_hash(0x837487D4) && gba::is_seq_hash(0x35DD392F) && !gba::is_seq_hash(0x26107D3C));

    gba::BodySnap s;
    CHECK(gba::body_state(s) == gba::BodyState::NotHere);
    s.exists = true;
    CHECK(gba::body_state(s) == gba::BodyState::NoCC);
    s.cc = true;
    CHECK(gba::body_state(s) == gba::BodyState::Other);
    // Session 12, 0xB0100077 after GuestSeqRestore: L0 hash 0x837487D4 asked, the wait substituted as current.
    s.layer[0] = {true, true, 0x837487D4, 0x26107D3C};
    CHECK(gba::body_state(s) == gba::BodyState::SeqNoGraph);
    s.layer[0] = {true, true, 0x26107D3C, 0x26107D3C};
    CHECK(gba::body_state(s) == gba::BodyState::Wait);
    s.layer[1] = {true, true, 0x35DD392F, 0};  // the loop asked on layer 1
    CHECK(gba::body_state(s) == gba::BodyState::SeqNoGraph);
    s.g70 = 0x4100AA00000;  // BodyAnim built the graph: that is the answer, whatever the layers say
    CHECK(gba::body_state(s) == gba::BodyState::Graph);
    for (size_t k = 0; k < static_cast<size_t>(gba::BodyState::Count); ++k) {
        CHECK(std::string_view(gba::body_state_name(static_cast<gba::BodyState>(k))) != "?");
    }
    // Change detection is on the whole reading, the hooks' counts included.
    gba::BodySnap t = s;
    CHECK(t == s);
    t.bodyanim = 1;
    CHECK(!(t == s));

    gba::Cohort c;
    c.add(gba::BodySnap{});  // not here
    c.add(s);                // graph, never seen by the hook (built before the hook, or by the ensure only)
    gba::BodySnap ran = s;
    ran.g70 = 0;
    ran.layer[0] = {true, true, 0x26107D3C, 0x26107D3C};
    ran.layer[1] = {};
    ran.bodyanim = 2;  // BodyAnim ran, no graph now
    c.add(ran);
    gba::BodySnap both = s;
    both.g78 = 0x4100BB00000;
    both.bodyanim = 1;
    c.add(both);
    CHECK(c.watched == 4);
    CHECK(c.state[static_cast<size_t>(gba::BodyState::NotHere)] == 1);
    CHECK(c.state[static_cast<size_t>(gba::BodyState::Graph)] == 2);
    CHECK(c.state[static_cast<size_t>(gba::BodyState::Wait)] == 1);
    CHECK(c.g78 == 1 && c.ran == 2 && c.ran_no_graph == 1);

    CHECK(gba::f32_of(gba::bits_of(1.5f)) == 1.5f);
    CHECK(gba::bits_of(0.0f) == 0);
}

}  // namespace

int main() {
    test_modes();
    test_classify();
    test_names();
    test_verdict();
    test_seq_ledger();
    test_bodyanim();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_seq_bodyanim: all OK\n");
    return 0;
}
