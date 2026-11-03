// S45 (docs/research/s25/s45-escenas-ajenas.md): guest_seq_present's pure logic (guest_seq_present_logic.h), with no
// game: whose a stage is (S44's judgement), the processor vtable's class, R-A's table from three vtables (only the
// StageChart function slots), the stage table P1 fills (timeline id / sequencer -> stage), the timeline root walk of
// S-B, the event's verdict, S-D's change detection, foreign liveness and the cost counter.
#include "mod/session2/guest_seq_present_logic.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace gpr = cdmp::session2::gpr;

namespace {

int failures = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
            ++failures;                                             \
        }                                                           \
    } while (0)

constexpr uint32_t kHost = 0xA0100001, kGuest = 0xA0100067, kNpc = 0xB0100500;

void test_whose() {
    using W = gpr::Whose;
    // Nothing is foreign while this client's own key is unknown (S44 H-9).
    CHECK(gpr::whose(kHost, 0, 0) == W::OwnUnknown);
    CHECK(gpr::whose(kHost, 0, kGuest) == W::OwnUnknown);
    // The guest's own scenes (its smithy 0x143, its sealed artifact, its world entry) are never foreign.
    CHECK(gpr::whose(kGuest, kGuest, 0) == W::Own);
    CHECK(gpr::whose(kGuest, kGuest, kGuest) == W::Own);
    // M's live owner counts as own too (S44: != own AND != M's).
    CHECK(gpr::whose(0xA0100066, kGuest, 0xA0100066) == W::Own);
    // The host's scene on the guest (smithy 0xA7132561, the jail 0xE5B490F1): foreign.
    CHECK(gpr::whose(kHost, kGuest, kGuest) == W::Foreign);
    CHECK(gpr::whose(kHost, kGuest, 0) == W::Foreign);
    // A world stage (an NPC owner, or none) is not a player's.
    CHECK(gpr::whose(kNpc, kGuest, kGuest) == W::NotCharacter);
    CHECK(gpr::whose(0, kGuest, kGuest) == W::NotCharacter);
    CHECK(!gpr::is_character(0) && gpr::is_character(kHost) && !gpr::is_character(kNpc));
    for (size_t w = 0; w < static_cast<size_t>(W::Count); ++w) CHECK(gpr::whose_name(static_cast<W>(w))[0] != '?');
}

void test_proc_vt() {
    using P = gpr::ProcVt;
    CHECK(gpr::classify_proc(0, false, 0) == P::NoProc);
    CHECK(gpr::classify_proc(0x1000, false, 0) == P::Unreadable);
    CHECK(gpr::classify_proc(0x1000, true, 0x558C080) == P::Client);
    CHECK(gpr::classify_proc(0x1000, true, 0x590DF78) == P::Common);
    CHECK(gpr::classify_proc(0x1000, true, 0x5B1FE10) == P::Server);
    CHECK(gpr::classify_proc(0x1000, true, 0x558C088) == P::Other);
    CHECK(gpr::classify_proc(0x1000, true, 0) == P::Other);
    for (size_t k = 0; k < static_cast<size_t>(P::Count); ++k) CHECK(gpr::proc_vt_name(static_cast<P>(k))[0] != '?');
}

void test_ra_table() {
    constexpr uint32_t ret = gpr::kRetRva;
    CHECK(gpr::slot_kind(ret, ret, ret) == gpr::SlotKind::Same);
    CHECK(gpr::slot_kind(0x5E4E60, ret, ret) == gpr::SlotKind::ClientOnly);          // InputBlock +0x150
    CHECK(gpr::slot_kind(0x5F2A00, ret, 0x293BA60) == gpr::SlotKind::ClientServer);  // Loading +0x218
    CHECK(gpr::slot_kind(0x204D7E0, 0x204D7E0, 0x204D7E0) == gpr::SlotKind::Same);   // SetPhase +0x230
    CHECK(gpr::slot_kind(0x1111, 0x2222, 0x3333) == gpr::SlotKind::CommonDiffers);
    CHECK(gpr::slot_kind(ret, ret, 0x293D390) == gpr::SlotKind::Same);  // CreateItem +0x108: server only, not the client's

    std::vector<uint32_t> c(gpr::kProcSlots, ret), m(gpr::kProcSlots, ret), s(gpr::kProcSlots, ret);
    auto at = [](uint32_t off) { return static_cast<size_t>(off / 8); };
    c[at(0x18)] = 0x5EB000;   // the processor's own slot, client only: NOT presentation (outside the function range)
    c[at(0x150)] = 0x5E4E60;  // InputBlock
    c[at(0x158)] = 0x5E4EB0;  // UIControl
    c[at(0x1C8)] = 0x5EE250;  // SequencerCamera
    c[at(0x2A0)] = 0x600000;  // the last function slot
    c[at(0x2A8)] = 0x600010;  // the first slot after it: outside
    c[at(0x218)] = 0x5F2A00;  // Loading: client and server
    s[at(0x218)] = 0x293BA60;
    c[at(0x230)] = m[at(0x230)] = s[at(0x230)] = 0x204D7E0;  // SetPhase: common
    c[at(0x298)] = 0x7777;    // SetHologramActor-like: common implements it differently
    m[at(0x298)] = 0x8888;
    const gpr::RaTable t = gpr::ra_table(c, m, s);
    CHECK(t.n_client_only == 4);
    CHECK(t.n_client_only >= 4 && t.client_only[0] == 0x150 && t.client_only[1] == 0x158 && t.client_only[2] == 0x1C8 &&
          t.client_only[3] == 0x2A0);
    CHECK(t.n_client_server == 1 && t.client_server[0] == 0x218);
    CHECK(t.n_common_differs == 1 && t.common_differs[0] == 0x298);
    CHECK(t.outside_client_only == 2);  // 0x18 and 0x2A8
    // Shorter spans: only what all three have.
    const gpr::RaTable u = gpr::ra_table(std::span<const uint32_t>(c.data(), at(0x158)), m, s);
    CHECK(u.n_client_only == 1 && u.client_only[0] == 0x150);
}

void test_stage_table() {
    gpr::StageTable<4> t;
    gpr::StageRec out;
    CHECK(!t.find_tl(5, out) && !t.find_seq(0x29A, out) && !t.latest_foreign(out));
    gpr::StageRec a;
    a.seq = 0x29A;  // the host's smithy on the guest (probe 22:50:56)
    a.tl = 1234;
    a.owner = kHost;
    a.whose = gpr::Whose::Foreign;
    a.us = 100;
    a.mgr = 0x3A53A33F300;
    a.stage = 0x5000;
    t.note(a);
    CHECK(t.find_tl(1234, out) && out.seq == 0x29A && out.owner == kHost && out.whose == gpr::Whose::Foreign &&
          out.mgr == 0x3A53A33F300 && out.stage == 0x5000);
    CHECK(t.find_seq(0x29A, out) && out.tl == 1234);
    CHECK(!t.find_tl(0, out) && !t.find_tl(0xFFFFFFFFu, out) && !t.find_seq(0, out));
    gpr::StageRec own = a;
    own.seq = 0x143;  // the guest's own smithy
    own.tl = 999;
    own.owner = kGuest;
    own.whose = gpr::Whose::Own;
    own.us = 300;
    t.note(own);
    CHECK(t.latest_foreign(out) && out.seq == 0x29A);  // an own stage is never "the latest foreign"
    gpr::StageRec jail = a;
    jail.seq = 0x30A;  // the jail, later
    jail.tl = 1300;
    jail.us = 200;
    t.note(jail);
    CHECK(t.latest_foreign(out) && out.seq == 0x30A);
    // A stage noted again keeps its slot (updated, not duplicated).
    jail.tl = 1301;
    t.note(jail);
    CHECK(t.noted() == 4 && t.overwritten() == 0);
    CHECK(t.find_seq(0x30A, out) && out.tl == 1301 && !t.find_tl(1300, out));
    // Ring: the 5th distinct stage overwrites the oldest slot (counted).
    gpr::StageRec x = a;
    x.seq = 0x500;
    x.tl = 50;
    t.note(x);
    gpr::StageRec y = a;
    y.seq = 0x501;
    y.tl = 51;
    t.note(y);
    CHECK(t.overwritten() == 1);
    CHECK(!t.find_seq(0x29A, out));  // slot 0 (the first noted) went
    CHECK(t.find_seq(0x501, out) && t.find_seq(0x143, out) && t.find_seq(0x30A, out) && t.find_seq(0x500, out));
    gpr::StageRec zero;
    t.note(zero);  // seq 0 is never noted
    CHECK(t.noted() == 6);
    // Review of R-B P3-1: two stages with the same timeline id: the most recent start wins.
    gpr::StageTable<4> u;
    gpr::StageRec old = a, young = a;
    old.seq = 0x700;
    old.tl = 777;
    old.us = 10;
    young.seq = 0x701;
    young.tl = 777;
    young.us = 20;
    u.note(young);
    u.note(old);
    CHECK(u.find_tl(777, out) && out.seq == 0x701);
}

struct FakeMem {
    std::map<uint64_t, uint64_t> q;
    std::set<uint64_t> bad;
    bool operator()(uint64_t a, uint64_t& out) const {
        if (bad.count(a)) return false;
        const auto it = q.find(a);
        out = it == q.end() ? 0 : it->second;
        return true;
    }
};

void test_root_of() {
    FakeMem m;
    // node 0x1000 -> 0x2000 -> 0x3000 (root: its +0x98 = 0)
    m.q[0x1000 + gpr::kNode_Parent] = 0x2000;
    m.q[0x2000 + gpr::kNode_Parent] = 0x3000;
    uint32_t depth = 99;
    bool cut = true;
    CHECK(gpr::root_of(0x1000, m, depth, cut) == 0x3000 && depth == 2 && !cut);
    CHECK(gpr::root_of(0x3000, m, depth, cut) == 0x3000 && depth == 0 && !cut);  // the node is the timeline itself
    CHECK(gpr::root_of(0, m, depth, cut) == 0 && depth == 0);
    m.bad.insert(0x2000 + gpr::kNode_Parent);
    CHECK(gpr::root_of(0x1000, m, depth, cut) == 0);  // a failed read: no root
    m.bad.clear();
    // A cycle is cut at the bound.
    m.q[0x3000 + gpr::kNode_Parent] = 0x1000;
    CHECK(gpr::root_of(0x1000, m, depth, cut, 8) != 0 && cut && depth == 8);
}

void test_events() {
    using T = gpr::Tl;
    CHECK(gpr::classify_tl(false, false, gpr::Whose::Foreign) == T::Unreadable);
    CHECK(gpr::classify_tl(true, false, gpr::Whose::Foreign) == T::NotAStage);
    CHECK(gpr::classify_tl(true, true, gpr::Whose::Foreign) == T::Foreign);
    CHECK(gpr::classify_tl(true, true, gpr::Whose::Own) == T::Own);
    CHECK(gpr::classify_tl(true, true, gpr::Whose::NotCharacter) == T::World);
    CHECK(gpr::classify_tl(true, true, gpr::Whose::OwnUnknown) == T::OwnUnknown);
    for (size_t k = 0; k < static_cast<size_t>(T::Count); ++k) CHECK(gpr::tl_name(static_cast<T>(k))[0] != '?');
    // The six classes: their vtables, COLs and slot-99 entries as read in the 2976 exe (scratchpad sondas\slots.py).
    std::set<uint32_t> vts, entries;
    for (size_t i = 0; i < gpr::kEvs; ++i) {
        const gpr::EvInfo e = gpr::ev_info(static_cast<gpr::Ev>(i));
        CHECK(e.name[0] != '?' && e.vt && e.col && e.entry);
        vts.insert(e.vt);
        entries.insert(e.entry);
    }
    CHECK(vts.size() == gpr::kEvs && entries.size() == gpr::kEvs);
    CHECK(gpr::ev_info(gpr::Ev::CameraSelector).entry == 0x34CFC20 && gpr::ev_info(gpr::Ev::CameraSelector).vt == 0x558B4A8);
    CHECK(gpr::ev_info(gpr::Ev::PlayerCameraPreset).entry == 0xB27880);
    CHECK(gpr::ev_info(gpr::Ev::LetterBox).entry == 0xC38F30);
    CHECK(gpr::ev_info(gpr::Ev::PostProcess).entry == 0xC3A440);
    CHECK(gpr::ev_info(gpr::Ev::AIDialog).entry == 0xC4D6B0);
    CHECK(gpr::ev_info(gpr::Ev::StartLoading).entry == 0xC45740 && gpr::ev_info(gpr::Ev::StartLoading).col == 0x5E07EE8);
}

void test_present_diff() {
    gpr::Present a;
    a.world = true;
    a.cam = 0x7000;
    a.cam_vt = 0x5600000;
    a.tl_n[0] = 3;
    a.m_block = 0;
    gpr::Present b = a;
    CHECK(gpr::diff(a, b) == 0 && !gpr::makes_line(0));
    b.tl_n[2] = 9;  // the player's lists move with every timeline of the game: counted, no line alone
    CHECK(gpr::diff(a, b) == gpr::kFTlCounts && !gpr::makes_line(gpr::diff(a, b)));
    b.cam = 0x7100;  // the camera entry changed: a line
    CHECK((gpr::diff(a, b) & gpr::kFCam) && gpr::makes_line(gpr::diff(a, b)));
    gpr::Present c = a;
    c.x_block = 1;
    CHECK(gpr::diff(a, c) == gpr::kFXBlock && gpr::makes_line(gpr::kFXBlock));
    c = a;
    c.fseq = 0x30A;
    c.ftl = 2;
    c.fplay = 1;
    CHECK(gpr::diff(a, c) == (gpr::kFForeign | gpr::kFForeignTl | gpr::kFForeignPlay));
    c = a;
    c.de8_ok = true;
    CHECK(gpr::diff(a, c) == gpr::kFDe8);
    c = a;
    c.world = false;
    CHECK(gpr::diff(a, c) == gpr::kFWorld);
}

void test_foreign_live() {
    gpr::StageRec r;
    r.seq = 0x30A;
    r.owner = kHost;
    r.us = 1'000'000;
    CHECK(gpr::foreign_live(r, 2'000'000, 0x30A, kHost));
    CHECK(!gpr::foreign_live(r, 2'000'000, 0x30B, kHost));  // the stage memory holds another sequencer: gone
    CHECK(!gpr::foreign_live(r, 2'000'000, 0x30A, kGuest));
    CHECK(!gpr::foreign_live(r, 500'000, 0x30A, kHost));    // clock before its start
    CHECK(!gpr::foreign_live(r, 1'000'000 + gpr::kForeignLiveUs + 1, 0x30A, kHost));
    gpr::StageRec none;
    CHECK(!gpr::foreign_live(none, 2'000'000, 0, 0));
}

// ---------------------------------------------------------------- R-A
// The three processor vtables' 105 slots (RVAs) as read from the 2976 exe (scratchpad s45\ra\slots.py, file SHA-256
// 57DA440D...DE1E7, the same as the installed game's).
const std::vector<uint32_t> kExeClient = {
    0x5D9750, 0x5DA1C0, 0x359B50, 0x5DAD20, 0x5E0690, 0x5E3640, 0x5FA3E0, 0x5FC580, 0x5E18F0, 0x5EE270, 0x5F1500,
    0x5EE330, 0x5EE3C0, 0x359B50, 0x5EEAA0, 0x5EE600, 0x5EE670, 0x5EE9E0, 0x2047880, 0x5EEBB0, 0x359B50, 0x5EEBD0,
    0x5EF1C0, 0x5EF680, 0x5EF730, 0x5EFDB0, 0x5EFE70, 0x5F0780, 0x359B50, 0x5EFF90, 0x5F0060, 0x5F00E0, 0x359B50,
    0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x5E36A0, 0x5E4400, 0x5E4B00, 0x5E4B60, 0x359B50, 0x5E4E60, 0x5E4EB0,
    0x5E9750, 0x5ECD90, 0x5ED9A0, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x5E9790, 0x5E99C0, 0x5E9B00, 0x5E9C40,
    0x5EE210, 0x5EE230, 0x5EE250, 0x5EE0C0, 0x5EE160, 0x5EDD30, 0x5EE1F0, 0x5EAE90, 0x5F2980, 0x5F4BC0, 0x5F2970,
    0x5F52E0, 0x5F2A00, 0x5F3600, 0x5F3810, 0x204D7E0, 0x5F3A90, 0x5F4050, 0x5F4110, 0x5F4200, 0x5F4280, 0x5F4440,
    0x5F4590, 0x359B50, 0x359B50, 0x359B50, 0x5F4EC0, 0x359B50, 0x204DEB0, 0x204DFB0, 0x5F53E0, 0x359B50, 0x5FA490,
    0x5FC280, 0x359B50, 0x5E0670, 0x5E0680, 0x359B50, 0x359B50, 0x5D99E0, 0x5D9EA0, 0x5DA100, 0x5DA110, 0x5EB200,
    0x5EB2C0, 0x5EC3F0, 0x5EC510, 0x5ECD10, 0x5F2490, 0x2050020};
const std::vector<uint32_t> kExeCommon = {
    0x2042530, 0x2042A60, 0x359B50, 0x359B50, 0x2043670, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x359B50,
    0x359B50,  0x359B50,  0x359B50, 0x359B50, 0x359B50,  0x359B50, 0x359B50, 0x2047880, 0x359B50, 0x359B50, 0x359B50,
    0x359B50,  0x359B50,  0x359B50, 0x359B50, 0x35EA90,  0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x359B50,
    0x359B50,  0x359B50,  0x359B50, 0x359B50, 0x359B50,  0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x359B50,
    0x359B50,  0x359B50,  0x359B50, 0x359B50, 0x359B50,  0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x359B50,
    0x359B50,  0x359B50,  0x359B50, 0x359B50, 0x359B50,  0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x359B50,
    0x359B50,  0x359B50,  0x359B50, 0x359B50, 0x204D7E0, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x359B50,
    0x359B50,  0x359B50,  0x359B50, 0x359B50, 0x204DDD0, 0x359B50, 0x204DEB0, 0x204DFB0, 0x359BA0, 0x359B50, 0x359B50,
    0x359B50,  0x359B50,  0x359BA0, 0x359BA0, 0x359B50, 0x359B50, 0x359B50,  0x359B50, 0x359B50, 0x359B50, 0x359B50,
    0x359B50,  0x359B50,  0x359B50, 0x359B50, 0x359B50, 0x2050020};
const std::vector<uint32_t> kExeServer = {
    0x292EC30, 0x2042A60, 0x359B50, 0x359B50, 0x2043670, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x359B50,
    0x359B50,  0x359B50,  0x293E350, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x2047880, 0x359B50, 0x293E4F0, 0x359B50,
    0x359B50,  0x359B50,  0x359B50, 0x359B50, 0x293E750, 0x359B50, 0x293F100, 0x293F1D0, 0x359B50, 0x359B50, 0x293E0D0,
    0x293D390, 0x293D750, 0x293DC30, 0x293DDE0, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x293B5E0, 0x359B50, 0x359B50,
    0x359B50,  0x359B50,  0x359B50, 0x293DC80, 0x293E060, 0x293E0A0, 0x2937F40, 0x359B50, 0x359B50, 0x359B50, 0x359B50,
    0x359B50,  0x359B50,  0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x293AEB0, 0x359B50,
    0x359B50,  0x293BA60, 0x359B50, 0x359B50, 0x204D7E0, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x293BC60, 0x359B50,
    0x359B50,  0x293C870, 0x293CF70, 0x293D220, 0x2939E90, 0x293A970, 0x204DEB0, 0x204DFB0, 0x359BA0, 0x293AB30,
    0x359B50,  0x359B50,  0x293F2B0, 0x359BA0, 0x359BA0, 0x2936DB0, 0x2937320, 0x359B50, 0x359B50, 0x359B50, 0x359B50,
    0x359B50,  0x2937F90, 0x359B50, 0x359B50, 0x359B50, 0x359B50, 0x2050020};

void test_ra_mode() {
    using M = gpr::RaMode;
    bool bad = true;
    CHECK(gpr::parse_ra_mode("0", &bad) == M::Off && !bad);
    CHECK(gpr::parse_ra_mode("", &bad) == M::Off && !bad);
    CHECK(gpr::parse_ra_mode("probe", &bad) == M::Probe && !bad);
    CHECK(gpr::parse_ra_mode(" Probe ", &bad) == M::Probe && !bad);
    CHECK(gpr::parse_ra_mode("1", &bad) == M::On && !bad);
    CHECK(gpr::parse_ra_mode("2", &bad) == M::Off && bad);  // anything else: off, with a warning
    CHECK(gpr::parse_ra_mode("yes", &bad) == M::Off && bad);
    CHECK(gpr::parse_ra_mode("probes", &bad) == M::Off && bad);
    CHECK(std::string(gpr::ra_mode_name(M::Probe)) == "probe" && std::string(gpr::ra_mode_name(M::On)) == "1");
}

void test_ra_plan_exe() {
    CHECK(kExeClient.size() == gpr::kProcSlots && kExeCommon.size() == gpr::kProcSlots &&
          kExeServer.size() == gpr::kProcSlots);
    const gpr::RaPlan p = gpr::ra_plan(kExeClient, kExeCommon, kExeServer);
    CHECK(p.ok);
    // 21 client-only + 3 client-and-server slots that turn presentation ON to the common's ret, +0x288 to the common's
    // own, 2 world slots, 9 kept native because they turn it OFF or carry the direction in their data (review P2-1).
    CHECK(p.n_ret == 24 && p.n_common == 1 && p.n_world == 2 && p.n_off == 9 && p.n_client_server == 3);
    auto use = [&](uint32_t off) { return p.use[off / 8]; };
    using U = gpr::RaUse;
    // What the user sees cut: input, the shop UI, panels, the minimap, the camera preset, the letter box, the seq
    // camera back on, LookAt, the hint, loading, reward.
    for (uint32_t off : {0x150u, 0x158u, 0x160u, 0x168u, 0x1C0u, 0x1E0u, 0x208u, 0x218u, 0x220u, 0x238u, 0x248u,
                         0x260u, 0x200u, 0x258u}) {
        CHECK(use(off) == U::Ret);
    }
    // P2-1: the offs (and the slots whose direction is in their data) stay the client's, so a pair is never split.
    for (uint32_t off : {0x130u, 0x1B8u, 0x1C8u, 0x1D0u, 0x1D8u, 0x1E8u, 0x210u, 0x228u, 0x250u}) {
        CHECK(use(off) == U::Off);
        CHECK(gpr::ra_keeps_native(use(off)));
    }
    CHECK(use(0x288) == U::Common);
    CHECK(use(0x1A0) == U::World && use(0x1A8) == U::World);  // DisableCharacterCollision: world
    // Everything the three share, and everything outside the function range, stays the client's.
    CHECK(use(0x230) == U::Native && use(0x298) == U::Native && use(0x2A0) == U::Native);  // SetPhase, hologram
    CHECK(use(0x00) == U::Native && use(0x18) == U::Native && use(0x2B8) == U::Native && use(0x340) == U::Native);
    CHECK(use(0x108) == U::Native && use(0x148) == U::Native && use(0x278) == U::Native);  // server-only: ret already
    size_t cut = 0;
    for (size_t k = 0; k < gpr::kProcSlots; ++k) {
        if (!gpr::ra_keeps_native(p.use[k])) {
            ++cut;
            CHECK(8 * k >= 0x108 && 8 * k <= 0x2A0);  // only in the StageChart function range
        }
    }
    CHECK(cut == 25);
    // Every decision is a slot the raw classification (the probe's 'R-A table') also lists.
    const gpr::RaTable t = gpr::ra_table(kExeClient, kExeCommon, kExeServer);
    CHECK(t.n_client_only == 32 && t.n_client_server == 3 && t.n_common_differs == 1);
    CHECK(gpr::kRaExpected == t.n_client_only + t.n_client_server + t.n_common_differs);
}

void test_ra_plan_refuses() {
    // A handler moved (another build): refused as a whole.
    std::vector<uint32_t> c = kExeClient;
    c[0x150 / 8] = 0x5E4E70;
    gpr::RaPlan p = gpr::ra_plan(c, kExeCommon, kExeServer);
    CHECK(!p.ok && p.bad_off == 0x150);
    // A new client-only slot without a decision.
    c = kExeClient;
    c[0x108 / 8] = 0x600000;
    p = gpr::ra_plan(c, kExeCommon, kExeServer);
    CHECK(!p.ok && p.bad_off == 0x108);
    // A decided slot whose kind changed (the server now implements InputBlock).
    std::vector<uint32_t> s = kExeServer;
    s[0x150 / 8] = 0x2930000;
    p = gpr::ra_plan(kExeClient, kExeCommon, s);
    CHECK(!p.ok && p.bad_off == 0x150);
    // A decided slot that became the same everywhere.
    std::vector<uint32_t> m = kExeCommon;
    m[0x288 / 8] = kExeClient[0x288 / 8];
    s = kExeServer;
    s[0x288 / 8] = kExeClient[0x288 / 8];
    p = gpr::ra_plan(kExeClient, m, s);
    CHECK(!p.ok && p.bad_off == 0x288);
    // Too few slots.
    p = gpr::ra_plan(std::span<const uint32_t>(kExeClient.data(), 50), kExeCommon, kExeServer);
    CHECK(!p.ok);
    // A slot outside the range differing between the three never matters (the processor's own).
    c = kExeClient;
    c[0x18 / 8] = 0x123456;
    CHECK(gpr::ra_plan(c, kExeCommon, kExeServer).ok);
}

void test_ra_build() {
    const gpr::RaPlan p = gpr::ra_plan(kExeClient, kExeCommon, kExeServer);
    constexpr uint64_t base = 0x140000000;
    std::vector<uint64_t> c(gpr::kProcSlots), m(gpr::kProcSlots);
    for (size_t k = 0; k < gpr::kProcSlots; ++k) {
        c[k] = base + kExeClient[k];
        m[k] = base + kExeCommon[k];
    }
    std::vector<uint64_t> out(1 + gpr::kProcSlots, 0);
    CHECK(gpr::ra_build(p, c, m, base + gpr::kColProcClient, out));
    CHECK(out[0] == base + 0x5DB7A20);                      // [-1]: the client's COL (RTTI keeps working)
    CHECK(out[1 + 0] == base + 0x5D9750);                   // slot 0, the deleting destructor: native
    CHECK(out[1 + 0x150 / 8] == base + gpr::kRetRva);       // InputBlock: the common's ret
    CHECK(out[1 + 0x218 / 8] == base + gpr::kRetRva);       // Loading
    CHECK(out[1 + 0x288 / 8] == base + 0x204DDD0);          // VarySharpness: the common's own
    CHECK(out[1 + 0x1A0 / 8] == base + 0x5E99C0);           // world: the client's
    CHECK(out[1 + 0x1B8 / 8] == base + 0x5EE210);           // DeactiveSequencerCamera (off): the client's
    CHECK(out[1 + 0x1C0 / 8] == base + gpr::kRetRva);       // its end (back on): cut
    CHECK(out[1 + 0x1E8 / 8] == base + 0x5EE1F0);           // CameraPreset's end (off): the client's
    CHECK(out[1 + 0x210 / 8] == base + 0x5F52E0);           // FadeInOut (direction in data): the client's
    CHECK(out[1 + 0x230 / 8] == base + 0x204D7E0);          // SetPhase
    size_t differ = 0;
    for (size_t k = 0; k < gpr::kProcSlots; ++k) differ += out[1 + k] != c[k];
    CHECK(differ == 25);
    // Refusals: no plan, no COL, a short output.
    gpr::RaPlan bad;
    CHECK(!gpr::ra_build(bad, c, m, base + gpr::kColProcClient, out));
    CHECK(!gpr::ra_build(p, c, m, 0, out));
    std::vector<uint64_t> shorter(gpr::kProcSlots, 0);
    CHECK(!gpr::ra_build(p, c, m, base + gpr::kColProcClient, shorter));
}

void test_ra_tramp() {
    uint8_t b[gpr::kTrampBytes] = {};
    gpr::tramp_bytes(0x150, b);  // InputBlock's Execute 0x2368240, as in the exe
    const uint8_t want[] = {0x48, 0x8B, 0x02, 0x4C, 0x8B, 0xC1, 0x48, 0x8B, 0x48, 0x58,
                            0x48, 0x8B, 0x01, 0x48, 0xFF, 0xA0, 0x50, 0x01, 0x00, 0x00};
    for (size_t i = 0; i < gpr::kTrampBytes; ++i) CHECK(b[i] == want[i]);
    std::set<uint32_t> tramps, offs;
    for (const gpr::RaExpect& e : gpr::kRaExpect) {
        tramps.insert(e.tramp);
        offs.insert(e.off);
        CHECK(e.off >= 0x108 && e.off <= 0x2A0 && e.off % 8 == 0);
        CHECK(gpr::ra_expect(e.off) == &e);
    }
    CHECK(tramps.size() == gpr::kRaExpected && offs.size() == gpr::kRaExpected);
    CHECK(gpr::ra_expect(0x230) == nullptr);
}

void test_ra_judge() {
    using V = gpr::RaVerdict;
    gpr::RaFacts f;
    f.mode = gpr::RaMode::On;
    f.whose = gpr::Whose::Foreign;
    f.stage = 0x5000;
    f.stage_mgr = 0x9000;
    f.proc = 0x6000;
    f.vt_read = true;
    f.client_vt = 0x14558C080;
    f.mod_vt = 0x7FF000000008;
    f.vt = f.client_vt;
    f.proc_stage = f.stage;
    f.proc_mgr = f.stage_mgr;
    f.tid = f.main_tid = 1234;
    f.m = 0x8000;       // M seen, not this stage's manager
    f.x_known = true;   // G is guest_seq_play's X of the owner
    CHECK(gpr::ra_judge(f) == V::Write);
    gpr::RaFacts g = f;
    g.mode = gpr::RaMode::Probe;
    CHECK(gpr::ra_judge(g) == V::Would);
    // The guest's own scene, a world stage, an unknown own key: never touched.
    for (auto w : {gpr::Whose::Own, gpr::Whose::NotCharacter, gpr::Whose::OwnUnknown}) {
        g = f;
        g.whose = w;
        CHECK(gpr::ra_judge(g) == V::NotForeign);
    }
    g = f;
    g.proc = 0;
    CHECK(gpr::ra_judge(g) == V::NoProcessor);
    g = f;
    g.vt_read = false;
    CHECK(gpr::ra_judge(g) == V::Unreadable);
    g = f;
    g.vt = g.mod_vt;  // a second P1 of the same stage
    CHECK(gpr::ra_judge(g) == V::AlreadyMod);
    g = f;
    g.vt = 0x14590DF78;  // the common processor
    CHECK(gpr::ra_judge(g) == V::OtherVtable);
    g = f;
    g.client_vt = 0;  // not armed
    CHECK(gpr::ra_judge(g) == V::OtherVtable);
    g = f;
    g.proc_stage = 0x5100;  // a processor that is not this stage's
    CHECK(gpr::ra_judge(g) == V::NotThisStage);
    g = f;
    g.proc_mgr = 0x9100;
    CHECK(gpr::ra_judge(g) == V::OtherManager);
    g = f;
    g.tid = 99;  // not the frame thread
    CHECK(gpr::ra_judge(g) == V::NotMainThread);
    g = f;
    g.main_tid = 0;  // guest_seq_play's frame never seen
    CHECK(gpr::ra_judge(g) == V::NotMainThread);
    // Review P1-1: never a stage of M; nothing while M is unknown; only a known X of the owner.
    g = f;
    g.stage_mgr = g.m;
    g.proc_mgr = g.m;
    CHECK(gpr::ra_judge(g) == V::InM);
    g.mode = gpr::RaMode::Probe;
    CHECK(gpr::ra_judge(g) == V::InM);
    g = f;
    g.m = 0;
    CHECK(gpr::ra_judge(g) == V::MUnknown);
    g = f;
    g.x_known = false;
    CHECK(gpr::ra_judge(g) == V::NotKnownX);
    g = f;
    g.stage_mgr = g.proc_mgr = g.m;  // even with the mod's table on it, an M stage is InM (and restored)
    g.vt = g.mod_vt;
    CHECK(gpr::ra_judge(g) == V::InM);
    // Review P2-2: restore a stage that must never carry the table.
    CHECK(gpr::ra_restore_due(g, gpr::ra_judge(g)));
    g = f;
    g.whose = gpr::Whose::Own;
    g.vt = g.mod_vt;
    CHECK(gpr::ra_judge(g) == V::NotForeign && gpr::ra_restore_due(g, V::NotForeign));
    g.m = 0;
    g.whose = gpr::Whose::Foreign;
    CHECK(gpr::ra_judge(g) == V::MUnknown && gpr::ra_restore_due(g, V::MUnknown));
    g = f;
    g.vt = g.mod_vt;  // a foreign stage in X that already has it: kept
    CHECK(gpr::ra_judge(g) == V::AlreadyMod && !gpr::ra_restore_due(g, V::AlreadyMod));
    g = f;
    g.whose = gpr::Whose::Own;  // an own stage with the client vtable: nothing to restore
    CHECK(!gpr::ra_restore_due(g, gpr::ra_judge(g)));
    g.vt = g.mod_vt;
    g.client_vt = 0;  // not armed: never restored
    CHECK(!gpr::ra_restore_due(g, V::NotForeign));
    for (size_t k = 0; k < static_cast<size_t>(V::Count); ++k) CHECK(gpr::ra_verdict_name(static_cast<V>(k))[0] != '?');
}

// R-B (guest_seq_foreign_timeline): the decision at a camera event's start (vivo1.md: callers 0x36574CD / 0x36575D6,
// [ctx+8] the timeline object 0x5C75F30, its +0x1D8 = the stage's +0x298).
void test_rb_judge() {
    using V = gpr::RbVerdict;
    CHECK(gpr::rb_caller_ok(0x36574CD) && gpr::rb_caller_ok(0x36575D6));
    CHECK(!gpr::rb_caller_ok(0x36574C7) && !gpr::rb_caller_ok(0) && !gpr::rb_caller_ok(0x36575E6 + 6));
    static_assert(gpr::kVtTimeline == 0x5C75F30);
    gpr::RbFacts f;
    f.mode = gpr::RaMode::On;
    f.caller = 0x36574CD;
    f.event_class = true;
    f.timeline = true;
    f.found = true;
    f.alive = true;
    f.whose = gpr::Whose::Foreign;  // the host's jail 0x33A / smithy 0x2E4 on the guest
    f.stage_mgr = 0x5F3BB716600;    // X
    f.m = 0x5F3BB700000;            // M
    f.x_known = true;
    f.tid = f.main_tid = 20128;
    CHECK(gpr::rb_judge(f) == V::Skip && gpr::rb_makes_line(f, V::Skip));
    gpr::RbFacts g = f;
    g.mode = gpr::RaMode::Probe;
    CHECK(gpr::rb_judge(g) == V::Would);
    g = f;
    g.caller = 0x36575D6;  // the second call site (then slot 101): also slot 99
    CHECK(gpr::rb_judge(g) == V::Skip);
    g = f;
    g.caller = 0x1234567;  // another slot / caller (e.g. a property change): never skipped
    CHECK(gpr::rb_judge(g) == V::NotSlot99);
    g = f;
    g.event_class = false;  // review P3-2: an event of another class
    CHECK(gpr::rb_judge(g) == V::NotClass);
    g = f;
    g.timeline = false;
    CHECK(gpr::rb_judge(g) == V::NoTimeline);
    g = f;
    g.found = false;  // the loading timelines 1 / 8 / 500 of step 1
    CHECK(gpr::rb_judge(g) == V::NoStage && !gpr::rb_makes_line(g, V::NoStage));
    g = f;
    g.alive = false;
    CHECK(gpr::rb_judge(g) == V::Gone);
    // The guest's own scenes (cd_common_abyssoneloading) and world stages: native, no line.
    for (auto w : {gpr::Whose::Own, gpr::Whose::NotCharacter, gpr::Whose::OwnUnknown}) {
        g = f;
        g.whose = w;
        CHECK(gpr::rb_judge(g) == V::NotForeign && !gpr::rb_makes_line(g, V::NotForeign));
    }
    g = f;
    g.m = 0;
    CHECK(gpr::rb_judge(g) == V::MUnknown && gpr::rb_makes_line(g, V::MUnknown));  // foreign: a line says why
    g = f;
    g.stage_mgr = g.m;
    CHECK(gpr::rb_judge(g) == V::InM);
    g = f;
    g.x_known = false;
    CHECK(gpr::rb_judge(g) == V::NotKnownX);
    g = f;
    g.tid = 1;
    CHECK(gpr::rb_judge(g) == V::NotMainThread);
    g = f;
    g.main_tid = 0;
    CHECK(gpr::rb_judge(g) == V::NotMainThread);
    g = f;
    g.mode = gpr::RaMode::Off;  // (never called when off; would not skip)
    CHECK(gpr::rb_judge(g) == V::Would);
    for (size_t k = 0; k < static_cast<size_t>(V::Count); ++k) CHECK(gpr::rb_verdict_name(static_cast<V>(k))[0] != '?');
}

// revision.md B3: S-D's timeline scan only on a new foreign stage or every kSdTlEvery frames.
void test_sd_scan_due() {
    CHECK(gpr::sd_scan_due(0x30A, 0, 0));                            // the first foreign stage: at once
    CHECK(!gpr::sd_scan_due(0x30A, 0x30A, 0));                       // the same stage, right after its scan
    CHECK(!gpr::sd_scan_due(0x30A, 0x30A, gpr::kSdTlEvery - 1));
    CHECK(gpr::sd_scan_due(0x30A, 0x30A, gpr::kSdTlEvery));          // every N frames
    CHECK(gpr::sd_scan_due(0x29A, 0x30A, 1));                        // another stage: at once
    static_assert(gpr::kSdTlEvery >= 2);
}

// revision.md B1: the start's callers per class (the letter box's slot 67 vs 99).
void test_callers() {
    gpr::CallerCounts<2> c;
    CHECK(c.rva(0) == 0 && c.n(0) == 0 && c.other() == 0);
    c.add(0x377FC09);
    c.add(0x377FC09);
    c.add(0x1234567);
    c.add(0x377FC09);
    c.add(0x7654321);  // the table is full: "other"
    c.add(0);          // outside the exe: "other"
    CHECK(c.rva(0) == 0x377FC09 && c.n(0) == 3);
    CHECK(c.rva(1) == 0x1234567 && c.n(1) == 1);
    CHECK(c.other() == 2);
    CHECK(c.rva(5) == 0 && c.n(5) == 0);
}

void test_cost() {
    gpr::Cost c;
    CHECK(c.n() == 0 && c.total() == 0 && c.max() == 0);
    c.add(10);
    c.add(30);
    c.add(20);
    CHECK(c.n() == 3 && c.total() == 60 && c.max() == 30);
    CHECK(gpr::ticks_ns(10'000'000, 10'000'000) == 1'000'000'000);
    CHECK(gpr::ticks_ns(1, 10'000'000) == 100);
    CHECK(gpr::ticks_ns(5, 0) == 0);
}

}  // namespace

int main() {
    test_whose();
    test_proc_vt();
    test_ra_table();
    test_stage_table();
    test_root_of();
    test_events();
    test_present_diff();
    test_foreign_live();
    test_ra_mode();
    test_ra_plan_exe();
    test_ra_plan_refuses();
    test_ra_build();
    test_ra_tramp();
    test_ra_judge();
    test_rb_judge();
    test_sd_scan_due();
    test_callers();
    test_cost();
    if (failures) {
        std::printf("optd_seq_present: %d FAILED\n", failures);
        return 1;
    }
    std::printf("optd_seq_present: all passed\n");
    return 0;
}
