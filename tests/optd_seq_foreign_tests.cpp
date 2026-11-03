// S44 (docs/research/s25/s44-escenas-del-anfitrion.md): guest_seq_foreign's pure logic (guest_seq_foreign_logic.h), with
// no game: the 2661 parser and 0xC05BB0's binding vector on real 2661s of the captures, the per-binding decision of
// guest_seq_foreign_player (only the scene owner, only another player's character, never an NPC, never this client's
// own), the modes 0 / probe / 1, the limits, and the read-only probes' classifications (P1, P2's 0x205DC30 replica, P3).
#include "mod/session2/guest_seq_foreign_logic.h"

#include <cstdio>
#include <string>
#include <vector>

namespace gsf = cdmp::session2::gsf;

namespace {

int failures = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
            ++failures;                                             \
        }                                                           \
    } while (0)

std::vector<uint8_t> hex(const char* s) {
    std::vector<uint8_t> out;
    auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    for (; s[0] && s[1]; s += 2) out.push_back(static_cast<uint8_t>(nib(s[0]) * 16 + nib(s[1])));
    return out;
}

constexpr uint32_t kHost = 0xA0100001;

// Real 2661 payloads (S>C, no 5-byte header) from the guest captures in C:\Emulador CD\backups.
// p8-2inst-pose-anfitrion\cdmp_packets.guest_20261015-083521.bin t=176.834, sequencer 0x440: cd_seq_boss_rematch_matthias
// (0x9586FFA4, @17/@18 = 2/2), the one alive at marks 2 and 3 when the host's copy was frozen (the guest was 0xA0100067).
const char* kRematch =
    "04010010a0010010a04004000000000000020235010000a31010b0a4ff8695000000000000000000000000000000000000000000000000a310"
    "10b0000000000000000000ffffff7f02000000000000000000ffffffffffffffff0d04000020a31010b005000002000020010010a00500000600"
    "0020c91010b0050000";
// p8-2inst-s43-activo\cdmp_packets.guest_20261015-200615.bin t=113.110, sequencer 0x2F1: the same scene in s43-activo.
const char* kRematchActivo =
    "04010010a0010010a0f102000000000000020235010000070c10b0a4ff8695000000000000000000000000000000000000000000000000070c"
    "10b0000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d04000020070c10b005000002000020010010a00500000600"
    "0020a10c10b0050000";
// p8-2inst-pose-anfitrion ... t=133.146, sequencer 0x265: the host's cd_seq_funcnpc_smithy (0xA7132561, 1/0).
const char* kSmithyHost =
    "04010010a0010010a0650200000000000001001043934100000000612513a710439341d43526c6d55819440c9670c54859353f00000000000000"
    "00000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d02000020010010a0050000080000206b0210b0050000";
// p8-2inst-tenderos-p2b\cdmp_packets.guest_20261015-144332.bin t=2541.472, sequencer 0x143: the GUEST's own smithy
// (owner 0xA0100066, the guest in 0x20000002): never touched.
const char* kSmithyGuestOwn =
    "04660010a0660010a0430100000000000001001043934100000000612513a710439341d43526c6d55819440c9670c54859353f00000000000000"
    "00000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d02000020660010a005000008000020ec0210b0050000";
// p8-2inst-s43-probe\cdmp_packets.guest_20261015-195503.bin t=427.069, sequencer 0x5C2: cd_seq_contents_weather_cloudy_0007
// (0xCD952BCB, 1/0, the host alone in 0x20000002).
const char* kCloudy7 =
    "04010010a0010010a0c2050000000000000100b44c0f0000000000cb2b95cdb44c0f00bc0926c6a24d1b442c546ec59877563e000000000000"
    "0000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d02000020010010a0050000";
// p8-sesion10-shadow\cdmp_packets_20261010-173057.bin t=642.447, sequencer 0x3E7: the host's cd_seq_use_sealedartifact_0001
// (0x70AE382E, @17/@18 = 6/3, @55 = the user).
const char* kSealedHost =
    "04010010a0010010a0e70300000000000006035f670f00000000002e38ae70000000004c0727c6f1390b44a02c83c5f7ca39c000000000010010"
    "a0010010a00000000000ffffff7f02007a450f007a450f00a5420f00000000000002000020010010a0050000";
// p8-sesion7-shadow\cdmp_packets_20261010-050511.bin t=180.929, sequencer 0x1A3: the guest's own (0xA01002E9) item use.
const char* kSealedGuestOwn =
    "04e90210a0e90210a0a30100000000000006035f670f00000000002e38ae7000000000795727c6b9201a449cb774c5e675fb3e00000000e90210"
    "a0e90210a00000000000ffffff7f01007a450f007a450f00a5420f00000000000002000020e90210a0050000";
// p8-2inst-pose-anfitrion ... t=121.073, sequencer 0x149: cd_minigame_seotda_base_hernand (0x1A1786F0): binds only an NPC.
const char* kSeotda =
    "04010010a0010010a0490100000000000001001b430f0000000000f086171a1b430f0000000000000000000000000000000000000000000000000"
    "0000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d01000020460210b0050000";
// p8-sesion9-shadow\...151331.bin t=722.169, sequencer 0x213: cd_seq_faction_bluemontwinehouse_loading (0x8CD771B1).
const char* kLoading =
    "04010010a0010010a01302000000000000010068450f0000000000b171d78c68450f00000000000000000000000000000000000000000000000000"
    "000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d02000020010010a0050000";
// p8-2inst-s43-activo\cdmp_packets.guest_20261015-200615.bin t=129.360, sequencer 0x453: the host's
// cd_seq_spawn_auto_animal_bush_bird (0x2BC74C06, @17/@18 = 10/1): the owner at 0x20000001 and three NPCs (review H-6).
const char* kFauna =
    "04010010a0010010a053040000000000000a0100000000ea030000064cc72b00000000001c26c670991b4400f06dc56bd41440000000000000"
    "0000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d01000020010010a005000003000020b40e10b00500000400"
    "0020b50e10b005000005000020b60e10b0050000";
// p8-sesion10-shadow\cdmp_packets_20261010-173057.bin t=637.572, sequencer 0x3E6: an item use whose owner is ANOTHER
// character key, 0xA0100363 (review H-6: the owner "A?" that is neither the host nor this client).
const char* kSealedThird =
    "04630310a0630310a0e60300000000000006035f670f00000000002e38ae7000000000c90427c6b1370b44244883c5eb5836c000000000630310"
    "a0630310a00000000000ffffff7f01007a450f007a450f00a5420f00000000000002000020630310a0050000";

gsf::Prepare parsed(const char* h) {
    const auto p = gsf::parse_2661(hex(h));
    CHECK(p.has_value());
    return p.value_or(gsf::Prepare{});
}

// What the hook reads at 0xC0612A for a payload (the routed case, a verified layout).
gsf::Facts facts_for(const gsf::Prepare& p, const std::vector<uint8_t>& vec, gsf::Mode mode, uint32_t own,
                     bool routed = true) {
    gsf::Facts f;
    f.mode = mode;
    f.layout_ok = true;
    f.routed = routed;
    f.would_route = !routed;
    f.owner = p.owner;
    f.owner_pd = p.owner;
    f.own_key = own;
    f.m_key = own;
    f.vec = vec.empty() ? 0 : 0x10000000ull;
    f.count = static_cast<uint32_t>(vec.size() / gsf::kEntrySize);
    f.capacity = f.count;
    return f;
}
std::vector<gsf::Entry> entries_of(const std::vector<uint8_t>& vec) {
    std::vector<gsf::Entry> e;
    for (size_t i = 0; i < vec.size() / gsf::kEntrySize; ++i) e.push_back(gsf::entry_at(vec, i));
    return e;
}

void test_parse() {
    const gsf::Prepare r = parsed(kRematch);
    CHECK(r.kind == 4 && r.actor == kHost && r.owner == kHost && r.seq == 0x440);
    CHECK(r.b17 == 2 && r.b18 == 2 && r.resource == 0x9586FFA4);
    CHECK(r.stage == 0xB01010A300000135ull);  // the NPC's key << 32 | 0x135
    CHECK(r.binds.size() == 3);
    CHECK((r.binds[0] == gsf::Entry{0x20000004, 0xB01010A3, 5}));
    CHECK((r.binds[1] == gsf::Entry{0x20000002, kHost, 5}));
    CHECK((r.binds[2] == gsf::Entry{0x20000006, 0xB01010C9, 5}));
    const gsf::Prepare a = parsed(kRematchActivo);
    CHECK(a.seq == 0x2F1 && a.resource == 0x9586FFA4 && a.binds.size() == 3 && a.binds[1].actor == kHost);
    const gsf::Prepare s = parsed(kSmithyHost);
    CHECK(s.resource == 0xA7132561 && s.b17 == 1 && s.b18 == 0 && s.binds.size() == 2);
    CHECK((s.binds[0] == gsf::Entry{0x20000002, kHost, 5}) && (s.binds[1] == gsf::Entry{0x20000008, 0xB010026B, 5}));
    const gsf::Prepare g = parsed(kSmithyGuestOwn);
    CHECK(g.owner == 0xA0100066 && g.resource == 0xA7132561 && g.binds[0].actor == 0xA0100066);
    const gsf::Prepare c = parsed(kCloudy7);
    CHECK(c.resource == 0xCD952BCB && c.binds.size() == 1 && (c.binds[0] == gsf::Entry{0x20000002, kHost, 5}));
    const gsf::Prepare u = parsed(kSealedHost);
    CHECK(u.resource == 0x70AE382E && u.b17 == 6 && u.b18 == 3 && u.binds.size() == 1 && u.binds[0].actor == kHost);
    const gsf::Prepare o = parsed(kSeotda);
    CHECK(o.resource == 0x1A1786F0 && o.binds.size() == 1 && o.binds[0].actor == 0xB0100246);
    const gsf::Prepare l = parsed(kLoading);
    CHECK(l.resource == 0x8CD771B1 && l.seq == 0x213 && l.binds.size() == 1 && l.binds[0].actor == kHost);
    // Not a 2661: short, or not 91 + 11 n.
    CHECK(!gsf::parse_2661(hex("04010010a0")));
    std::vector<uint8_t> odd = hex(kCloudy7);
    odd.push_back(0);
    CHECK(!gsf::parse_2661(odd) && gsf::handler_vector(odd).empty());
}

void test_handler_vector() {
    // The 12-byte entries 0xC05BB0 builds: the 11 wire bytes at +0..+0xA.
    const std::vector<uint8_t> v = gsf::handler_vector(hex(kRematch));
    CHECK(v.size() == 3 * gsf::kEntrySize);
    const auto e = entries_of(v);
    CHECK(e.size() == 3 && (e[1] == gsf::Entry{0x20000002, kHost, 5}) && (e[0] == gsf::Entry{0x20000004, 0xB01010A3, 5}));
    CHECK(v[1 * 12 + 8] == 5 && v[1 * 12 + 9] == 0 && v[1 * 12 + 10] == 0);
    static_assert(gsf::kEntrySize == 12 && gsf::kEntryType == 8 && gsf::kEntryActor == 4);
    static_assert(gsf::kVecPtrSlot == -0x80 && gsf::kVecCountSlot == -0x78 && gsf::kVecCapSlot == -0x74);
    CHECK(gsf::handler_vector(hex(kSeotda)).size() == 12);
}

// The node types P1 would have learnt for every entry of a vector (the same type for all, or per entry).
std::vector<uint8_t> nodes_all(const std::vector<uint8_t>& vec, uint8_t t) {
    return std::vector<uint8_t>(vec.size() / gsf::kEntrySize, t);
}
gsf::Plan plan_for(const gsf::Facts& f, const std::vector<uint8_t>& vec, const std::vector<uint8_t>& nodes) {
    const auto e = entries_of(vec);
    return gsf::plan(f, e, nodes);
}

void test_rematch_modes() {
    const gsf::Prepare r = parsed(kRematch);
    const std::vector<uint8_t> vec0 = gsf::handler_vector(hex(kRematch));
    const std::vector<uint8_t> player = nodes_all(vec0, gsf::kNodeCurrentControl);
    const uint32_t guest = 0xA0100067;
    // 0: exactly as before (nothing judged, nothing written).
    {
        std::vector<uint8_t> vec = vec0;
        const gsf::Plan p = plan_for(facts_for(r, vec, gsf::Mode::Off, guest), vec, player);
        CHECK(p.verdict == gsf::Verdict::Off && !p.write() && gsf::apply(vec, p) == 0 && vec == vec0);
    }
    // probe: the owner's entry (index 1) would be neutralized; nothing written, whatever P1 learnt.
    for (uint8_t nt : {gsf::kNodeCurrentControl, gsf::kNodeUnknown, uint8_t{4}}) {
        std::vector<uint8_t> vec = vec0;
        const gsf::Plan p = plan_for(facts_for(r, vec, gsf::Mode::Probe, guest), vec, nodes_all(vec, nt));
        CHECK(p.verdict == gsf::Verdict::Would && p.n == 1 && p.idx[0] == 1 && p.was[0] == 5 && p.node[0] == nt);
        CHECK(p.owner_entries == 1 && p.others == 2 && p.owner_other == 0 && p.write_mask == 0);
        CHECK(!p.write() && gsf::apply(vec, p) == 0 && vec == vec0);
    }
    // 1 with Route and the owner's node learnt as CurrentControl: only the owner's type byte goes 5 -> 0.
    {
        std::vector<uint8_t> vec = vec0;
        const gsf::Plan p = plan_for(facts_for(r, vec, gsf::Mode::On, guest), vec, player);
        CHECK(p.verdict == gsf::Verdict::Applied && p.write() && p.n == 1 && p.writes(0) && !p.writes(1));
        CHECK(gsf::apply(vec, p) == 1);
        const auto e = entries_of(vec);
        CHECK((e[1] == gsf::Entry{0x20000002, kHost, 0}));
        CHECK((e[0] == gsf::Entry{0x20000004, 0xB01010A3, 5}) && (e[2] == gsf::Entry{0x20000006, 0xB01010C9, 5}));
        for (size_t i = 0; i < vec.size(); ++i) {
            if (i != 1 * 12 + 8) CHECK(vec[i] == vec0[i]);  // one byte only
        }
        CHECK(gsf::apply(vec, p) == 0);  // applied twice: the re-check sees 0, not 5
    }
    // S44 live: 1 and the owner's node not seen by P1 yet (first appearance, e.g. the first boss-memory rematch or the
    // live scenes copied when the guest joins): neutralized all the same (no Learning state any more).
    {
        std::vector<uint8_t> vec = vec0;
        const gsf::Plan p = plan_for(facts_for(r, vec, gsf::Mode::On, guest), vec, {});
        CHECK(p.verdict == gsf::Verdict::Applied && p.unknown == 1 && p.writes(0) && gsf::apply(vec, p) == 1);
        CHECK(entries_of(vec)[1].type == 0 && entries_of(vec)[0].type == 5 && entries_of(vec)[2].type == 5);
    }
    // ... the exceptions: learnt as a non-player node (PivotActor 4, SpawnCharacter 2, fixed 1) or mixed: never touched.
    for (uint8_t nt : {uint8_t{1}, uint8_t{2}, uint8_t{4}, gsf::kNodeMixed}) {
        std::vector<uint8_t> vec = vec0;
        const gsf::Plan p = plan_for(facts_for(r, vec, gsf::Mode::On, guest), vec, nodes_all(vec, nt));
        CHECK(p.verdict == gsf::Verdict::NotPlayerNode && p.not_player == 1 && gsf::apply(vec, p) == 0 && vec == vec0);
    }
    // 1 while guest_seq_route is probe (WouldRoute: the scene goes to M): only WOULD.
    {
        std::vector<uint8_t> vec = vec0;
        const gsf::Plan p = plan_for(facts_for(r, vec, gsf::Mode::On, guest, /*routed=*/false), vec, player);
        CHECK(p.verdict == gsf::Verdict::Would && !p.write() && gsf::apply(vec, p) == 0 && vec == vec0);
    }
    // The s43-activo instance: the same decision.
    {
        const gsf::Prepare a = parsed(kRematchActivo);
        std::vector<uint8_t> vec = gsf::handler_vector(hex(kRematchActivo));
        const gsf::Plan p = plan_for(facts_for(a, vec, gsf::Mode::On, 0xA0100066), vec, nodes_all(vec, 0));
        CHECK(p.verdict == gsf::Verdict::Applied && gsf::apply(vec, p) == 1 && entries_of(vec)[1].type == 0);
    }
}

void test_other_scenes() {
    const uint32_t guest = 0xA0100066;
    // The host's shop: the host's binding neutralized, the shopkeeper's kept.
    {
        const gsf::Prepare s = parsed(kSmithyHost);
        std::vector<uint8_t> vec = gsf::handler_vector(hex(kSmithyHost));
        const gsf::Plan p = plan_for(facts_for(s, vec, gsf::Mode::On, guest), vec, nodes_all(vec, 0));
        CHECK(p.verdict == gsf::Verdict::Applied && p.n == 1 && p.idx[0] == 0 && p.others == 1);
        CHECK(gsf::apply(vec, p) == 1);
        const auto e = entries_of(vec);
        CHECK(e[0].type == 0 && e[0].actor == kHost && (e[1] == gsf::Entry{0x20000008, 0xB010026B, 5}));
    }
    // The guest's own shop (owner = this client's character): never touched, in any mode.
    for (gsf::Mode m : {gsf::Mode::Probe, gsf::Mode::On}) {
        const gsf::Prepare g = parsed(kSmithyGuestOwn);
        std::vector<uint8_t> vec = gsf::handler_vector(hex(kSmithyGuestOwn));
        const std::vector<uint8_t> before = vec;
        const gsf::Plan p = plan_for(facts_for(g, vec, m, guest), vec, nodes_all(vec, 0));
        CHECK(p.verdict == gsf::Verdict::OwnScene && gsf::apply(vec, p) == 0 && vec == before);
    }
    // The own item use (another session's guest key): own too.
    {
        const gsf::Prepare g = parsed(kSealedGuestOwn);
        std::vector<uint8_t> vec = gsf::handler_vector(hex(kSealedGuestOwn));
        const gsf::Plan p = plan_for(facts_for(g, vec, gsf::Mode::On, 0xA01002E9), vec, nodes_all(vec, 0));
        CHECK(p.verdict == gsf::Verdict::OwnScene);
    }
    // Weather (cloudy_0007) and the host's item use (6/3): one binding, the owner's.
    for (const char* h : {kCloudy7, kSealedHost}) {
        const gsf::Prepare s = parsed(h);
        std::vector<uint8_t> vec = gsf::handler_vector(hex(h));
        const gsf::Plan p = plan_for(facts_for(s, vec, gsf::Mode::On, guest), vec, nodes_all(vec, 0));
        CHECK(p.verdict == gsf::Verdict::Applied && p.n == 1 && p.idx[0] == 0 && gsf::apply(vec, p) == 1);
        CHECK(entries_of(vec)[0].type == 0);
    }
    // Review H-5: the loading cutscene 0x8CD771B1 never reaches the hook live (guest_seq_route_deny refuses it before:
    // Verdict::Denied is not Route / WouldRoute, see test_wants). Here only the pure logic, as if it were routed.
    {
        const gsf::Prepare s = parsed(kLoading);
        std::vector<uint8_t> vec = gsf::handler_vector(hex(kLoading));
        CHECK(plan_for(facts_for(s, vec, gsf::Mode::On, guest), vec, nodes_all(vec, 0)).verdict == gsf::Verdict::Applied);
    }
    // A scene of X that binds only an NPC (seotda): nothing to do, counted.
    {
        const gsf::Prepare s = parsed(kSeotda);
        std::vector<uint8_t> vec = gsf::handler_vector(hex(kSeotda));
        const std::vector<uint8_t> before = vec;
        const gsf::Plan p = plan_for(facts_for(s, vec, gsf::Mode::On, guest), vec, nodes_all(vec, 1));
        CHECK(p.verdict == gsf::Verdict::NoOwnerBind && p.others == 1 && gsf::apply(vec, p) == 0 && vec == before);
    }
    // A scene with no bindings at all (quest_hernand_safezone_2500: 0 binds).
    {
        gsf::Facts f;
        f.mode = gsf::Mode::On;
        f.layout_ok = true;
        f.routed = true;
        f.owner = f.owner_pd = kHost;
        f.own_key = f.m_key = guest;
        const gsf::Plan p = gsf::plan(f, {});
        CHECK(p.verdict == gsf::Verdict::NoOwnerBind);
    }
}

// Review H-6: fauna (the owner at 0x20000001 with three NPCs) and the owner "A?" that is a third character.
void test_fauna_and_third() {
    const uint32_t guest = 0xA0100066;
    const gsf::Prepare f0 = parsed(kFauna);
    CHECK(f0.owner == kHost && f0.resource == 0x2BC74C06 && f0.b17 == 10 && f0.b18 == 1 && f0.seq == 0x453);
    CHECK(f0.binds.size() == 4 && (f0.binds[0] == gsf::Entry{0x20000001, kHost, 5}));
    CHECK((f0.binds[1] == gsf::Entry{0x20000003, 0xB0100EB4, 5}) && (f0.binds[3] == gsf::Entry{0x20000005, 0xB0100EB6, 5}));
    const std::vector<uint8_t> vec0 = gsf::handler_vector(hex(kFauna));
    // The owner's node at 0x20000001 learnt as PivotActor (4): never touched; the NPCs never.
    {
        std::vector<uint8_t> vec = vec0;
        std::vector<uint8_t> nodes = {4, 2, 2, 2};
        const gsf::Plan p = plan_for(facts_for(f0, vec, gsf::Mode::On, guest), vec, nodes);
        CHECK(p.verdict == gsf::Verdict::NotPlayerNode && p.others == 3 && gsf::apply(vec, p) == 0 && vec == vec0);
    }
    // Not learnt yet: neutralized from the first appearance (only the owner's entry 0).
    {
        std::vector<uint8_t> vec = vec0;
        const gsf::Plan p = plan_for(facts_for(f0, vec, gsf::Mode::On, guest), vec, {});
        CHECK(p.verdict == gsf::Verdict::Applied && p.unknown == 1 && gsf::apply(vec, p) == 1);
        const auto e = entries_of(vec);
        CHECK(e[0].type == 0 && e[1].type == 5 && e[2].type == 5 && e[3].type == 5);
    }
    // The NPCs' node types learnt (2) but the owner's not: the owner still neutralized, the NPCs never.
    {
        std::vector<uint8_t> vec = vec0;
        std::vector<uint8_t> nodes = {gsf::kNodeUnknown, 2, 2, 2};
        const gsf::Plan p = plan_for(facts_for(f0, vec, gsf::Mode::On, guest), vec, nodes);
        CHECK(p.verdict == gsf::Verdict::Applied && p.writes(0) && p.n == 1 && gsf::apply(vec, p) == 1);
    }
    // probe: WOULD whatever P1 knows, nothing written.
    for (uint8_t nt : {gsf::kNodeUnknown, uint8_t{0}, uint8_t{4}}) {
        std::vector<uint8_t> vec = vec0;
        const std::vector<uint8_t> nodes = {nt, 2, 2, 2};
        const gsf::Plan p = plan_for(facts_for(f0, vec, gsf::Mode::Probe, guest), vec, nodes);
        CHECK(p.verdict == gsf::Verdict::Would && !p.write() && gsf::apply(vec, p) == 0 && vec == vec0);
    }
    // Learnt as CurrentControl: only entry 0, the three NPCs untouched.
    {
        std::vector<uint8_t> vec = vec0;
        std::vector<uint8_t> nodes = {0, 2, 2, 2};
        const gsf::Plan p = plan_for(facts_for(f0, vec, gsf::Mode::On, guest), vec, nodes);
        CHECK(p.verdict == gsf::Verdict::Applied && gsf::apply(vec, p) == 1);
        const auto e = entries_of(vec);
        CHECK(e[0].type == 0 && e[1].type == 5 && e[2].type == 5 && e[3].type == 5);
    }
    // The owner 0xA0100363: another player for this client (0xA0100066): judged like the host.
    const gsf::Prepare t = parsed(kSealedThird);
    CHECK(t.owner == 0xA0100363 && t.actor == 0xA0100363 && t.resource == 0x70AE382E && t.binds.size() == 1);
    {
        std::vector<uint8_t> vec = gsf::handler_vector(hex(kSealedThird));
        const gsf::Plan p = plan_for(facts_for(t, vec, gsf::Mode::On, guest), vec, nodes_all(vec, 0));
        CHECK(p.verdict == gsf::Verdict::Applied && gsf::apply(vec, p) == 1 && entries_of(vec)[0].type == 0);
    }
    // ... and its own scene when this client is 0xA0100363.
    {
        std::vector<uint8_t> vec = gsf::handler_vector(hex(kSealedThird));
        CHECK(plan_for(facts_for(t, vec, gsf::Mode::On, 0xA0100363), vec, nodes_all(vec, 0)).verdict ==
              gsf::Verdict::OwnScene);
    }
}

void test_wants() {
    // guest_seq_route_hook's call: only a 2661 with its raw packet and resource, Route or WouldRoute.
    CHECK(gsf::wants_2661(true, true, true, false));
    CHECK(gsf::wants_2661(true, true, false, true));
    CHECK(!gsf::wants_2661(true, true, false, false));  // Denied / refused / own / NPC: not judged
    CHECK(!gsf::wants_2661(false, true, true, false));
    CHECK(!gsf::wants_2661(true, false, true, false));
}

void test_judge_and_limits() {
    const uint32_t guest = 0xA0100066;
    // judge_bind: only the owner, only another player's character, only 4 / 5.
    CHECK(gsf::judge_bind({0x20000002, kHost, 5}, kHost, guest, guest) == gsf::Bind::OwnerLinked);
    CHECK(gsf::judge_bind({0x20000002, kHost, 4}, kHost, guest, guest) == gsf::Bind::OwnerLinked);
    CHECK(gsf::judge_bind({0x20000002, kHost, 7}, kHost, guest, guest) == gsf::Bind::OwnerOther);
    CHECK(gsf::judge_bind({0x20000002, kHost, 0}, kHost, guest, guest) == gsf::Bind::OwnerOther);
    CHECK(gsf::judge_bind({0x20000004, 0xB01010A3, 5}, kHost, guest, guest) == gsf::Bind::Keep);  // NPC
    CHECK(gsf::judge_bind({0x20000003, guest, 5}, kHost, guest, guest) == gsf::Bind::Keep);       // this client's own
    CHECK(gsf::judge_bind({0x20000003, 0xA0100777, 5}, kHost, guest, guest) == gsf::Bind::Keep);  // a third player
    CHECK(gsf::judge_bind({0x20000002, 0xB0100001, 5}, 0xB0100001, guest, guest) == gsf::Bind::Keep);  // NPC "owner"
    CHECK(gsf::judge_bind({0x20000002, guest, 5}, guest, guest, guest) == gsf::Bind::Keep);

    const gsf::Prepare r = parsed(kRematch);
    const std::vector<uint8_t> vec0 = gsf::handler_vector(hex(kRematch));
    const auto e0 = entries_of(vec0);
    const std::vector<uint8_t> player = nodes_all(vec0, 0);
    auto run = [&](auto&& tweak) {
        gsf::Facts f = facts_for(r, vec0, gsf::Mode::On, guest);
        tweak(f);
        return gsf::plan(f, e0, player).verdict;
    };
    CHECK(run([](gsf::Facts& f) { f.routed = f.would_route = false; }) == gsf::Verdict::NotX);
    CHECK(run([](gsf::Facts& f) { f.layout_ok = false; }) == gsf::Verdict::LayoutUnverified);
    CHECK(run([](gsf::Facts& f) { f.owner_pd = 0; }) == gsf::Verdict::OwnerMismatch);
    CHECK(run([](gsf::Facts& f) { f.owner = f.owner_pd = 0xB0100001; }) == gsf::Verdict::OwnerNotPlayer);
    CHECK(run([](gsf::Facts& f) { f.owner = f.owner_pd = 0; }) == gsf::Verdict::OwnerNotPlayer);
    CHECK(run([&](gsf::Facts& f) { f.m_key = kHost; }) == gsf::Verdict::OwnScene);  // M's own character
    CHECK(run([](gsf::Facts& f) { f.own_key = 0; }) == gsf::Verdict::Applied);    // own key unknown: M's is enough
    CHECK(run([](gsf::Facts& f) { f.read_ok = false; }) == gsf::Verdict::Unreadable);
    CHECK(run([](gsf::Facts& f) { f.count = 4; }) == gsf::Verdict::Unreadable);       // entries read != count
    CHECK(run([](gsf::Facts& f) { f.capacity = 2; }) == gsf::Verdict::Unreadable);    // count > capacity
    CHECK(run([](gsf::Facts& f) { f.vec = 0; }) == gsf::Verdict::Unreadable);
    CHECK(run([](gsf::Facts& f) { f.count = f.capacity = gsf::kMaxEntries + 1; }) == gsf::Verdict::Unreadable);
    // The owner bound with another type only: not touched.
    {
        std::vector<gsf::Entry> e = e0;
        e[1].type = 7;
        gsf::Facts f = facts_for(r, vec0, gsf::Mode::On, guest);
        CHECK(gsf::plan(f, e, player).verdict == gsf::Verdict::OwnerOtherType);
    }
    // The owner pending (type 4): neutralized as well (0x205B4D0 would make it 5).
    {
        std::vector<uint8_t> vec = vec0;
        vec[1 * 12 + 8] = 4;
        const gsf::Plan p = plan_for(facts_for(r, vec, gsf::Mode::On, guest), vec, player);
        CHECK(p.verdict == gsf::Verdict::Applied && p.was[0] == 4 && gsf::apply(vec, p) == 1 && vec[1 * 12 + 8] == 0);
    }
    // The write re-checks the byte: if it is no longer the planned type, nothing is written.
    {
        std::vector<uint8_t> vec = vec0;
        const gsf::Plan p = plan_for(facts_for(r, vec, gsf::Mode::On, guest), vec, player);
        vec[1 * 12 + 8] = 9;
        CHECK(gsf::apply(vec, p) == 0 && vec[1 * 12 + 8] == 9);
    }
    // Two owner bindings, one at the player node and one at a learnt exception: only the first is written; an unknown
    // one is written too.
    {
        std::vector<gsf::Entry> e = {{0x20000002, kHost, 5}, {0x20000001, kHost, 5}, {0x20000004, 0xB0100001, 5}};
        gsf::Facts f = facts_for(r, vec0, gsf::Mode::On, guest);
        f.count = f.capacity = 3;
        const std::vector<uint8_t> nodes = {0, 4, 1};
        const gsf::Plan p = gsf::plan(f, e, nodes);
        CHECK(p.verdict == gsf::Verdict::Applied && p.n == 2 && p.writes(0) && !p.writes(1) && p.not_player == 1);
        const std::vector<uint8_t> nodes2 = {gsf::kNodeUnknown, gsf::kNodeMixed, 1};
        const gsf::Plan q = gsf::plan(f, e, nodes2);
        CHECK(q.verdict == gsf::Verdict::Applied && q.writes(0) && !q.writes(1) && q.unknown == 1 && q.not_player == 1);
    }
    // More owner bindings than the plan holds: all or nothing -> nothing.
    {
        std::vector<gsf::Entry> e(gsf::kMaxPlan + 1, gsf::Entry{0x20000002, kHost, 5});
        gsf::Facts f = facts_for(r, vec0, gsf::Mode::On, guest);
        f.count = f.capacity = static_cast<uint32_t>(e.size());
        const gsf::Plan p = gsf::plan(f, e, std::vector<uint8_t>(e.size(), 0));
        CHECK(p.verdict == gsf::Verdict::Unreadable && p.n == 0 && !p.write());
    }
    // Exactly kMaxPlan: applied, every one.
    {
        std::vector<gsf::Entry> e(gsf::kMaxPlan, gsf::Entry{0x20000002, kHost, 5});
        gsf::Facts f = facts_for(r, vec0, gsf::Mode::On, guest);
        f.count = f.capacity = static_cast<uint32_t>(e.size());
        const gsf::Plan p = gsf::plan(f, e, std::vector<uint8_t>(e.size(), 0));
        CHECK(p.verdict == gsf::Verdict::Applied && p.n == gsf::kMaxPlan && p.write_mask == 0xFF);
    }
    // Modes.
    bool bad = false;
    CHECK(gsf::parse_mode("", &bad) == gsf::Mode::Off && !bad);
    CHECK(gsf::parse_mode("0", &bad) == gsf::Mode::Off && !bad);
    CHECK(gsf::parse_mode("probe", &bad) == gsf::Mode::Probe && !bad);
    CHECK(gsf::parse_mode("1", &bad) == gsf::Mode::On && !bad);
    CHECK(gsf::parse_mode("2", &bad) == gsf::Mode::Off && bad);
    CHECK(gsf::parse_mode("Probe", &bad) == gsf::Mode::Off && bad);
    CHECK(std::string(gsf::mode_name(gsf::Mode::Probe)) == "probe");
    for (size_t k = 0; k < static_cast<size_t>(gsf::Verdict::Count); ++k) {
        CHECK(std::string(gsf::verdict_name(static_cast<gsf::Verdict>(k))) != "?");
    }
}

void test_maps() {
    // The learnt node map (review H-1 b).
    static gsf::NodeMap<64> m;
    CHECK(m.find(0x9586FFA4, 0x20000002) == gsf::kNodeUnknown);
    CHECK(m.note(0x9586FFA4, 0x20000002, 0) && m.find(0x9586FFA4, 0x20000002) == gsf::kNodeCurrentControl);
    CHECK(m.note(0x9586FFA4, 0x20000002, 0) && m.find(0x9586FFA4, 0x20000002) == 0);  // the same again
    CHECK(m.note(0x2BC74C06, 0x20000001, 4) && m.find(0x2BC74C06, 0x20000001) == 4);
    CHECK(m.find(0x2BC74C06, 0x20000002) == gsf::kNodeUnknown && m.find(0x9586FFA4, 0x20000001) == gsf::kNodeUnknown);
    CHECK(m.note(0x2BC74C06, 0x20000001, 0) && m.find(0x2BC74C06, 0x20000001) == gsf::kNodeMixed);  // conflict: mixed
    CHECK(m.note(0x2BC74C06, 0x20000001, 4) && m.find(0x2BC74C06, 0x20000001) == gsf::kNodeMixed);  // for good
    CHECK(!m.note(0, 0x20000001, 0) && !m.note(1, 0, 0) && !m.note(1, 1, gsf::kNodeMixed) && !m.note(1, 1, 0xFF));
    CHECK(m.used() == 2);
    // Full: noting fails (counted), finding the absent ones answers unknown.
    static gsf::NodeMap<16> small;
    size_t ok = 0;
    for (uint32_t i = 1; i <= 40; ++i) ok += small.note(0x100 + i, 0x20000002, 0);
    CHECK(ok == 16 && small.used() == 16 && small.full() == 24);
    CHECK(small.find(0x101, 0x20000002) == 0 && small.find(0x999, 0x20000002) == gsf::kNodeUnknown);
    // sequencer -> resource.
    static gsf::SeqResRing<4> s;
    CHECK(s.find(0x440) == 0);
    s.note(0x440, 0x9586FFA4);
    s.note(0x453, 0x2BC74C06);
    CHECK(s.find(0x440) == 0x9586FFA4 && s.find(0x453) == 0x2BC74C06 && s.find(0) == 0);
    s.note(0x440, 0x11111111);  // the same sequencer again: updated in place
    CHECK(s.find(0x440) == 0x11111111);
    for (uint64_t q = 1; q <= 4; ++q) s.note(0x1000 + q, 0x2000 + static_cast<uint32_t>(q));  // a ring of 4
    CHECK(s.find(0x440) == 0 && s.find(0x1004) == 0x2004);
    s.note(0, 5);
    s.note(5, 0);
    CHECK(s.find(5) == 0);
}

void test_probes() {
    const uint32_t guest = 0xA0100066;
    // P1: the stage's actors at its start (rematch: the host at CurrentControl).
    std::vector<gsf::StageBind> b = {{0xB01010A3, 0x20000004, 0, 1, 5}, {kHost, 0x20000002, 0, 0, 5},
                                     {0xB01010C9, 0x20000006, 0, 1, 5}};
    CHECK(gsf::classify_p1(kHost, guest, b) == gsf::P1::OwnerActive);
    b[1].link = 0;
    CHECK(gsf::classify_p1(kHost, guest, b) == gsf::P1::OwnerNeutral);
    b[1].link = 4;
    CHECK(gsf::classify_p1(kHost, guest, b) == gsf::P1::OwnerPending);
    b[1].link = 5;
    b[1].node_type = 2;
    CHECK(gsf::classify_p1(kHost, guest, b) == gsf::P1::OwnerOtherNode);
    CHECK(gsf::classify_p1(guest, guest, b) == gsf::P1::OwnScene);
    CHECK(gsf::classify_p1(kHost, 0, b) == gsf::P1::OwnUnknown);  // review H-9
    CHECK(gsf::classify_p1(kHost, guest, std::vector<gsf::StageBind>{{0xB0100246, 0x20000001, 0, 1, 5}}) ==
          gsf::P1::NoOwner);
    CHECK(gsf::classify_p1(kHost, guest, b, false) == gsf::P1::Unreadable);
    b[1].node_type = 0;

    // P2: 0x205DC30's replica. Ids match node+0x30 or +0x34; link 0 is not even counted; link 4 counted, not resolved.
    const uint32_t ids_player[] = {0x20000002};
    gsf::Resolved r = gsf::resolve(ids_player, b);
    CHECK(r.n == 1 && r.keys[0] == kHost && r.counted == 1 && !r.mission_node);
    CHECK(gsf::classify_p2(true, r, guest) == gsf::P2::OtherPlayer);
    CHECK(gsf::classify_p2(true, r, 0) == gsf::P2::OwnUnknown);  // review H-9
    CHECK(gsf::classify_p2(false, r, guest) == gsf::P2::NotStarted);
    b[1].link = 0;  // the fix reached the stage
    r = gsf::resolve(ids_player, b);
    CHECK(r.n == 0 && r.counted == 0 && gsf::classify_p2(true, r, guest) == gsf::P2::NothingResolved);
    b[1].link = 4;
    r = gsf::resolve(ids_player, b);
    CHECK(r.n == 0 && r.counted == 1);
    b[1].link = 5;
    const uint32_t ids_npcs[] = {0x20000004, 0x1FFFFFFF};
    r = gsf::resolve(ids_npcs, b);
    CHECK(r.n == 1 && r.keys[0] == 0xB01010A3 && r.mission_node && gsf::classify_p2(true, r, guest) == gsf::P2::NpcOnly);
    std::vector<gsf::StageBind> b2 = {{guest, 0x20000002, 0x20000009, 0, 5}};
    const uint32_t ids2[] = {0x20000009};
    r = gsf::resolve(ids2, b2);
    CHECK(r.n == 1 && gsf::classify_p2(true, r, guest) == gsf::P2::LocalPlayer);
    const uint32_t ids_both[] = {0x20000002, 0x20000006};
    r = gsf::resolve(ids_both, b);
    CHECK(r.n == 2 && gsf::classify_p2(true, r, guest) == gsf::P2::OtherPlayer);
    std::vector<gsf::StageBind> many(gsf::kMaxIds + 4, gsf::StageBind{0xB0100001, 0x20000001, 0, 1, 5});
    const uint32_t one[] = {0x20000001};
    r = gsf::resolve(one, many);
    CHECK(r.n == gsf::kMaxIds && r.counted == static_cast<uint8_t>(gsf::kMaxIds + 4));

    // P3.
    CHECK(gsf::classify_p3(kHost, guest) == gsf::P3::OtherPlayer);
    CHECK(gsf::classify_p3(guest, guest) == gsf::P3::OwnCharacter);
    CHECK(gsf::classify_p3(0xB01010C9, guest) == gsf::P3::NonCharacter);
    CHECK(gsf::classify_p3(0, guest) == gsf::P3::NonCharacter);

    // P4 (0x5EB310: 0x5EB41F lets {0,3,6,7} through, refuses {1,2,4,5} and > 7).
    for (uint8_t t = 0; t < 10; ++t) CHECK(gsf::p4_accepts(t) == (t == 0 || t == 3 || t == 6 || t == 7));
    CHECK(gsf::classify_p4(false, true, kHost, 0, kHost) == gsf::P4::NotForeign);
    CHECK(gsf::classify_p4(true, false, 0, 0xFF, kHost) == gsf::P4::NotFound);
    CHECK(gsf::classify_p4(true, true, kHost, 5, kHost) == gsf::P4::Rejected);
    CHECK(gsf::classify_p4(true, true, kHost, 1, kHost) == gsf::P4::Rejected);
    CHECK(gsf::classify_p4(true, true, kHost, 0, kHost) == gsf::P4::SpawnOnOwner);
    CHECK(gsf::classify_p4(true, true, 0xB0100001, 0, kHost) == gsf::P4::SpawnOther0);
    CHECK(gsf::classify_p4(true, true, 0xB0100001, 7, kHost) == gsf::P4::SpawnOtherType);

    for (size_t k = 0; k < static_cast<size_t>(gsf::P1::Count); ++k) {
        CHECK(std::string(gsf::p1_name(static_cast<gsf::P1>(k))) != "?");
    }
    for (size_t k = 0; k < static_cast<size_t>(gsf::P2::Count); ++k) {
        CHECK(std::string(gsf::p2_name(static_cast<gsf::P2>(k))) != "?");
    }
    for (size_t k = 0; k < static_cast<size_t>(gsf::P4::Count); ++k) {
        CHECK(std::string(gsf::p4_name(static_cast<gsf::P4>(k))) != "?");
    }
    static_assert(gsf::kStage_Binds == 0x120 && gsf::kStage_BindCount == 0x128 && gsf::kBindStride == 0x28);
    static_assert(gsf::kBind_Key == 8 && gsf::kBind_Type == 0xC && gsf::kNode_Type == 0xAC && gsf::kNode_Id == 0x30);
    static_assert(gsf::kStage_Owner == 0x98 && gsf::kStage_Seq == 0x30 && gsf::kStage_Started == 0x28B);
}

}  // namespace

int main() {
    test_parse();
    test_handler_vector();
    test_rematch_modes();
    test_other_scenes();
    test_fauna_and_third();
    test_wants();
    test_judge_and_limits();
    test_maps();
    test_probes();
    if (failures) {
        std::printf("optd_seq_foreign: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("optd_seq_foreign: OK\n");
    return 0;
}
