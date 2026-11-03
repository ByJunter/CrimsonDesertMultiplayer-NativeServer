// P8 S1+S2, user 2 as an observer of the field's existing stages/sequencers
// (src/mod/session2/observer_stage_logic.h; docs/research/pendientes/p8-observador-escenas.md).
//
// SCOPE: offline logic only. The parsers run on the PrepareSequencerAck 2661,
// DeleteSequencerAck 2810 and PlayAllPreLoadSequencerAck 2322 packets the host's
// own client received in P8 session 5 (capture cdmp_packets_20261009-203910.bin,
// every one of them, in order), and the ledger replays them. They do NOT test
// the hooks, the byte checks against the running game, the register reading
// at 0x2A4DEF0/0x2A4E4C0/0x2A4F2D0/0x2999D80/0x2BC84E2 nor the game's
// behaviour (that is the static analysis of the document and a live session).

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "mod/session2/observer_stage_logic.h"

using namespace cdmp::session2;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

struct Fixture {
    int record;       // CDPK record number (#n)
    double t;         // the capture's t column
    int opcode;
    const char* hex;  // the whole packet: u16 opcode | i8 slot | u16 length | payload
};

// Every 2661 / 2810 / 2322 server -> client packet of the host's own capture
// (P8 session 5, 20:39:10), generated from the .bin with the CDPK reader.
const Fixture kHostCapture[] = {
    {794, 19.703625, 2322,
         "1209ff0400010010a0"},
    {1680, 50.420055, 2661,
         "650affb30004010010a0010010a0130000000000000001007a4f0f00000000000ca0e5c47a4f0f000000000000000000"
         "00000000000000000000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "03000020010010a00500000c0000206d0010b005000005000020710010b005000007000020720010b005000008000020"
         "730010b005000009000020740010b00500000b000020750010b00500000d000020760010b0050000"},
    {1744, 50.437282, 2661,
         "650aff7c0004010010a0010010a01900000000000000010053fa0f0000000000cffb40bd53fa0f000000000000000000"
         "00000000000000000000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "01000020010010a005000003000020770010b005000004000020780010b0050000"},
    {2699, 50.447256, 2661,
         "650aff710004010010a0010010a07300000000000000010080fe0f0000000000ebf7b34280fe0f000000000000000000"
         "00000000000000000000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "01000020010010a005000003000020790010b0050000"},
    {3385, 50.454826, 2661,
         "650aff710004010010a0010010a04701000000000000010068930f0000000000566aed0368930f000000000000000000"
         "00000000000000000000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "02000020010010a0050000040000207a0010b0050000"},
    {3411, 50.457298, 2661,
         "650aff710004010010a0010010a04d01000000000000010027b30f0000000000f9d7610e27b30f000000000000000000"
         "00000000000000000000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "02000020010010a0050000040000207b0010b0050000"},
    {8477, 50.492463, 2661,
         "650aff710004010010a0010010a063010000000000000100fe440f0000000000432aa257fe440f002a7727c6a00a1b44"
         "eaa874c595ace1bf0000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "01000020010010a0050000030000207c0010b0050000"},
    {8483, 50.493883, 2661,
         "650aff660004010010a0010010a06401000000000000010014480f0000000000382fa49c14480f002a7727c6a00a1b44"
         "eaa874c595ace1bf0000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "01000020010010a0050000"},
    {8489, 50.495051, 2661,
         "650aff660004010010a0010010a06501000000000000010093480f00000000009308d5cd93480f002a7727c6a00a1b44"
         "eaa874c595ace1bf0000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "01000020010010a0050000"},
    {8505, 50.496051, 2661,
         "650aff660004010010a0010010a066010000000000000100af4c0f000000000070b0ce43af4c0f002a7727c6a00a1b44"
         "eaa874c595ace1bf0000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "01000020010010a0050000"},
    {8523, 50.497206, 2661,
         "650aff660004010010a0010010a067010000000000000100b74c0f0000000000208b8eeeb74c0f002a7727c6a00a1b44"
         "eaa874c595ace1bf0000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "01000020010010a0050000"},
    {9187, 50.504749, 2661,
         "650aff870004010010a0010010a06c010000000000000100b2420f0000000000562972dcb2420f000000000000000000"
         "00000000000000000000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "01000020010010a0050000030000207e0010b0050000040000207f0010b005000006000020800010b0050000"},
    {38031, 50.859367, 2661,
         "650aff710004010010a0010010a090010000000000000100288b0f00000000007c075b07288b0f000000000000000000"
         "00000000000000000000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "02000020010010a005000004000020820010b0050000"},
    {38370, 50.925809, 2322,
         "1209ff0400010010a0"},
    {47638, 51.547483, 2661,
         "650aff710004010010a0010010a0920100000000000001005e470f0000000000ca4583975e470f00d4d426c68fe21844"
         "e90674c5875e2cc00000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "02000020010010a005000004000020580110b0050000"},
    {47642, 51.550150, 2661,
         "650aff7c0004010010a0010010a093010000000000000100d94f0f00000000005ec8426fd94f0f00def528c6df6a1844"
         "f06479c54d02bfbf0000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "02000020010010a005000004000020590110b0050000080000205a0110b0050000"},
    {47647, 51.553162, 2661,
         "650aff710004010010a0010010a094010000000000000100fc69da05000000005e60a18ffc69da05607a26c6b0121944"
         "107672c5b9e1fe3f0000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "02000020010010a0050000040000205b0110b0050000"},
    {47651, 51.556887, 2661,
         "650aff710004010010a0010010a0950100000000000001001043934100000000612513a710439341d43526c6d5581944"
         "0c9670c54859353f0000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "02000020010010a0050000080000205c0110b0050000"},
    {47656, 51.559778, 2661,
         "650aff710004010010a0010010a09601000000000000010024439341000000001ad7fa2c24439341394a26c614861944"
         "48e371c50d9309400000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "02000020010010a0050000060000205d0110b0050000"},
    {52224, 53.348262, 2661,
         "650aff7c0004010010a0010010a097010000000000000603d0600f00000000008006a072000000000000000000000000"
         "00000000000000000000000000000000fb0110b00000000000ffffff7f010000000000804c0f00ffffffffffffffff04"
         "03000020010010a005000005000020130210b005000006000020140210b0050000"},
    {64698, 81.131245, 2810,
         "fa0aff1100010010a06301000000000000fe440f0005"},
    {219966, 327.518266, 2810,
         "fa0aff1100010010a09701000000000000000000000c"},
    {314524, 477.489610, 2810,
         "fa0aff1100010010a06601000000000000af4c0f0000"},
    {314552, 477.515263, 2661,
         "650aff660004010010a0010010a098010000000000000100b14c0f000000000052ad0ec6b14c0f00f19f27c65f751a44"
         "a70275c5cf4b753f0000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d"
         "01000020010010a0050000"},
};

std::vector<uint8_t> unhex(const char* s) {
    std::vector<uint8_t> v;
    auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    for (; s[0] && s[1]; s += 2) v.push_back(static_cast<uint8_t>(nib(s[0]) << 4 | nib(s[1])));
    return v;
}

const Fixture* by_record(int n) {
    for (const Fixture& f : kHostCapture) {
        if (f.record == n) return &f;
    }
    return nullptr;
}

constexpr uint32_t kHostChar = 0xA0100001, kHostUser = 0x90100000;
constexpr uint32_t kGuestChar = 0xA0100334, kGuestUser = 0x90100333;

// The 15 NPCs whose restore failed with eErrNoInvalidActionChart on the guest
// in session 5 (Shadow cdmp.log, "RestartLayerAction 0x863880 FAILED").
constexpr uint32_t kGuestFailed[] = {0xB010006D, 0xB0100071, 0xB0100072, 0xB0100073, 0xB0100074,
                                     0xB0100075, 0xB0100076, 0xB0100077, 0xB0100078, 0xB010007E,
                                     0xB0100080, 0xB0100082, 0xB010015A, 0xB0100213, 0xB0100214};

std::optional<obs::Packet> packet_of(const std::vector<uint8_t>& raw) { return obs::split_packet(raw); }

// ---------------------------------------------------------------- parsers

void test_split_packet() {
    const std::vector<uint8_t> raw = unhex(by_record(794)->hex);
    const auto pk = packet_of(raw);
    CHECK(pk && pk->opcode == obs::kOpMilestone && pk->payload.size() == 4);
    if (pk) CHECK(obs::parse_milestone_payload(pk->payload) == kHostChar);
    // A length field that does not match the bytes is refused.
    std::vector<uint8_t> bad = raw;
    bad.push_back(0);
    CHECK(!obs::split_packet(bad));
    bad = raw;
    bad[3] = 5;
    CHECK(!obs::split_packet(bad));
    CHECK(!obs::split_packet(std::span<const uint8_t>(raw.data(), 4)));
}

void test_parse_prepare_seq13() {
    const std::vector<uint8_t> raw = unhex(by_record(1680)->hex);
    CHECK(raw.size() == 184 && obs::prepare_wire_size(8) == 184);
    const auto pk = packet_of(raw);
    CHECK(pk && pk->opcode == obs::kOpPrepare);
    if (!pk) return;
    const auto pr = obs::parse_prepare_payload(pk->payload);
    CHECK(pr.has_value());
    if (!pr) return;
    CHECK(pr->kind == 4 && pr->actor == kHostChar && pr->owner_actor == kHostChar && pr->seq == 0x13);
    const std::vector<obs::Binding> want = {{0x20000003, 0xA0100001}, {0x2000000C, 0xB010006D},
                                            {0x20000005, 0xB0100071}, {0x20000007, 0xB0100072},
                                            {0x20000008, 0xB0100073}, {0x20000009, 0xB0100074},
                                            {0x2000000B, 0xB0100075}, {0x2000000D, 0xB0100076}};
    CHECK(pr->bindings == want);
}

void test_parse_prepare_shapes() {
    // No binding but the owner's own character (sequencer 0x164) and wrong lengths.
    const std::vector<uint8_t> raw = unhex(by_record(8483)->hex);
    const auto pk = packet_of(raw);
    CHECK(pk.has_value());
    if (!pk) return;
    const auto pr = obs::parse_prepare_payload(pk->payload);
    CHECK(pr && pr->seq == 0x164 && pr->bindings.size() == 1 && pr->bindings[0].actor == kHostChar);
    CHECK(!obs::parse_prepare_payload(pk->payload.subspan(0, pk->payload.size() - 1)));
    CHECK(!obs::parse_prepare_payload(pk->payload.subspan(0, 90)));
    // Every 2661 of the capture: the size rule 5 + 91 + 11 n holds, actor and owner are the host's character.
    int n = 0;
    for (const Fixture& f : kHostCapture) {
        if (f.opcode != obs::kOpPrepare) continue;
        const std::vector<uint8_t> r2 = unhex(f.hex);
        const auto p2 = packet_of(r2);
        const auto pr2 = p2 ? obs::parse_prepare_payload(p2->payload) : std::nullopt;
        CHECK(pr2.has_value());
        if (!pr2) continue;
        ++n;
        CHECK(r2.size() == obs::prepare_wire_size(pr2->bindings.size()));
        CHECK(pr2->actor == kHostChar && pr2->owner_actor == kHostChar);
    }
    CHECK(n == 19);
}

void test_parse_delete() {
    const std::vector<uint8_t> raw = unhex(by_record(64698)->hex);
    const auto pk = packet_of(raw);
    CHECK(pk && pk->opcode == obs::kOpDelete && pk->payload.size() == obs::kDeletePayload);
    if (!pk) return;
    const auto d = obs::parse_delete_payload(pk->payload);
    CHECK(d && d->actor == kHostChar && d->seq == 0x163);
    CHECK(!obs::parse_delete_payload(pk->payload.subspan(1)));
}

// ---------------------------------------------------------------- the ledger on the whole capture

obs::SeqEntry entry_of(const obs::Prepare& pr, uint32_t user, double t) {
    obs::SeqEntry e;
    e.actor = pr.actor;
    e.user = user;
    e.owner_actor = pr.owner_actor;
    e.seq = pr.seq;
    e.t_us = static_cast<uint64_t>(t * 1e6);
    e.bindings = pr.bindings;
    e.bindings_total = static_cast<uint32_t>(pr.bindings.size());
    return e;
}

// Replays the capture up to time `until` (the host's client is user 0x90100000).
obs::Ledger replay(double until) {
    obs::Ledger l;
    for (const Fixture& f : kHostCapture) {
        if (f.t > until) break;
        const std::vector<uint8_t> raw = unhex(f.hex);
        const auto pk = packet_of(raw);
        if (!pk) continue;
        if (pk->opcode == obs::kOpPrepare) {
            if (const auto pr = obs::parse_prepare_payload(pk->payload)) l.prepare(entry_of(*pr, kHostUser, f.t));
        } else if (pk->opcode == obs::kOpDelete) {
            if (const auto d = obs::parse_delete_payload(pk->payload)) l.remove(d->actor, d->seq);
        }
    }
    return l;
}

void test_ledger_replay() {
    // At user 2's non-owner entry into the host's field (host log: "[owner 0x2BC8470] ... capture
    // t=195.206405 ... NOT the owner"), and after the three 2810 of the capture (t = 81.1, 327.5, 477.5).
    const obs::Ledger at_entry = replay(195.206405);
    CHECK(at_entry.prepared() == 18 && at_entry.deleted() == 1 && at_entry.live() == 17);
    CHECK(!at_entry.get(kHostChar, 0x163) && at_entry.get(kHostChar, 0x13));
    const obs::Ledger all = replay(1e9);
    CHECK(all.prepared() == 19 && all.deleted() == 3 && all.delete_unknown() == 0 && all.live() == 16);

    const obs::ObserverNeed n = at_entry.need(kHostUser, kGuestUser);
    CHECK(n.owner_live == 17 && n.delivered == 0 && n.missing.size() == 17);
    // Sorted by sequencer id; the owner's own character is never a "bound NPC".
    CHECK(std::is_sorted(n.missing.begin(), n.missing.end(),
                         [](const obs::SeqEntry* a, const obs::SeqEntry* b) { return a->seq < b->seq; }));
    CHECK(std::find(n.bound_actors.begin(), n.bound_actors.end(), kHostChar) == n.bound_actors.end());
    // The decisive cross-check: every NPC whose restore failed on the guest is bound in one of the
    // host's live sequencers the guest never received.
    for (uint32_t k : kGuestFailed) {
        CHECK(std::binary_search(n.bound_actors.begin(), n.bound_actors.end(), k));
    }
    // And nothing of it exists for an owner with no sequencer.
    CHECK(at_entry.need(kGuestUser, kHostUser).owner_live == 0);
    CHECK(at_entry.need(0, kGuestUser).owner_live == 0);
}

void test_ledger_delivery_and_branch() {
    obs::Ledger l = replay(60.0);
    const size_t before = l.need(kHostUser, kGuestUser).missing.size();
    // The same (owner actor, sequencer) prepared for the observer's character counts as delivered.
    const std::vector<uint8_t> raw = unhex(by_record(1744)->hex);
    const auto pk = packet_of(raw);
    const auto pr = pk ? obs::parse_prepare_payload(pk->payload) : std::nullopt;
    CHECK(pr.has_value());
    if (!pr) return;
    obs::SeqEntry mine = entry_of(*pr, kGuestUser, 70.0);
    mine.actor = kGuestChar;  // addressed to user 2's character, owner actor unchanged
    CHECK(l.prepare(mine));
    const obs::ObserverNeed n = l.need(kHostUser, kGuestUser);
    CHECK(n.delivered == 1 && n.missing.size() == before - 1);
    CHECK(std::none_of(n.missing.begin(), n.missing.end(), [](const obs::SeqEntry* e) { return e->seq == 0x19; }));
    // 2981 on a live sequencer keeps its last value; on an unknown one it is counted only.
    CHECK(l.branch(kHostChar, 0x13, 7, 1));
    CHECK(l.branch(kHostChar, 0x13, 9, 2));
    CHECK(!l.branch(kHostChar, 0x999, 1, 3));
    const obs::SeqEntry* s13 = l.get(kHostChar, 0x13);
    CHECK(s13 && s13->has_branch && s13->branch_value == 9 && s13->branches == 2);
    CHECK(l.branch_unknown() == 1);
    if (!s13) return;
    // A re-prepare replaces the bindings but keeps the branch already seen.
    obs::SeqEntry again = *s13;
    again.has_branch = false;
    again.bindings.resize(1);
    again.bindings_total = 1;
    CHECK(l.prepare(again));
    s13 = l.get(kHostChar, 0x13);
    CHECK(s13 && s13->bindings.size() == 1 && s13->has_branch && s13->branch_value == 9 && l.replaced() == 1);
    // A 2810 of an unknown sequencer is counted, nothing removed.
    const size_t live = l.live();
    CHECK(!l.remove(kHostChar, 0x12345));
    CHECK(l.live() == live && l.delete_unknown() == 1);
}

void test_ledger_bounds() {
    obs::Ledger l(2);
    obs::SeqEntry e;
    e.actor = kHostChar;
    e.user = kHostUser;
    e.owner_actor = kHostChar;
    for (uint64_t s = 1; s <= 3; ++s) {
        e.seq = s;
        const bool ok = l.prepare(e);
        CHECK(ok == (s <= 2));
    }
    CHECK(l.live() == 2 && l.full() == 1);
    // Bindings over kMaxBindings are cut and reported as unknown in need().
    obs::Ledger big;
    obs::SeqEntry b;
    b.actor = kHostChar;
    b.user = kHostUser;
    b.owner_actor = kHostChar;
    b.seq = 9;
    for (uint32_t i = 0; i < obs::kMaxBindings + 5; ++i) b.bindings.push_back({0x20000000u + i, 0xB0100000u + i});
    b.bindings_total = static_cast<uint32_t>(b.bindings.size());
    CHECK(big.prepare(b));
    const obs::ObserverNeed n = big.need(kHostUser, kGuestUser);
    CHECK(n.missing.size() == 1 && n.bindings_unknown == 5 && n.bound_actors.size() == obs::kMaxBindings);
    // The owner's other characters can be excluded explicitly.
    const uint32_t excl[] = {0xB0100000u};
    CHECK(big.need(kHostUser, kGuestUser, excl).bound_actors.size() == obs::kMaxBindings - 1);
}

// ---------------------------------------------------------------- after the entry; the mode

void test_entry_watch() {
    obs::EntryWatch w;
    w.observer_user = kGuestUser;
    w.owner_user = kHostUser;
    w.t_us = 1'000'000;
    w.count(obs::kOpPrepare, kHostUser, true);  // the owner's: not the observer's
    w.count(obs::kOpBranch, kHostUser, false);  // the owner's sequencer through a near finder
    w.count(obs::kOpBranch, kHostUser, true);   // targeted at the owner: not counted
    w.count(obs::kOpMilestone, kGuestUser, true);
    w.count(obs::kOpPrepare, kGuestUser, true);
    CHECK(w.prepares == 1 && w.milestones == 1 && w.branches == 0 && w.owner_branches_untargeted == 1);
    CHECK(obs::follow_up_due(w, 1'000'000) == -1);
    CHECK(obs::follow_up_due(w, 11'000'000) == 0);
    w.reported |= 1;
    CHECK(obs::follow_up_due(w, 11'000'000) == -1);
    CHECK(obs::follow_up_due(w, 400'000'000) == 1);  // in order, one at a time
    w.reported |= 2;
    CHECK(obs::follow_up_due(w, 400'000'000) == 2);
    w.reported |= 4;
    CHECK(obs::follow_up_due(w, 900'000'000) == -1);
}

void test_mode() {
    CHECK(obs::choose_mode(0).mode == obs::Mode::Off && !obs::choose_mode(0).send_refused);
    CHECK(obs::choose_mode(1).mode == obs::Mode::Measure && !obs::choose_mode(1).send_refused);
    // The send mode is refused: measure only.
    CHECK(obs::choose_mode(2).mode == obs::Mode::Measure && obs::choose_mode(2).send_refused);
    CHECK(obs::choose_mode(0xFFFFFFFF).mode == obs::Mode::Measure && obs::choose_mode(0xFFFFFFFF).send_refused);
}

}  // namespace

int main() {
    test_split_packet();
    test_parse_prepare_seq13();
    test_parse_prepare_shapes();
    test_parse_delete();
    test_ledger_replay();
    test_ledger_delivery_and_branch();
    test_ledger_bounds();
    test_entry_watch();
    test_mode();
    if (g_failures) {
        std::printf("optd_observer_stage: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("optd_observer_stage: all passed\n");
    return 0;
}
