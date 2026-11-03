// P8 S1 option A, the sequencer part (docs/research/pendientes/p8-impl-opcion-a.md).
//
// SCOPE: offline logic only, on packets of the session 7 captures (host client
// cdmp_packets_20261010-060302.bin = HC, guest client cdmp_packets_20261010-050511.bin = GC).
//  - observer_seq_copy_logic.h: the key's values, the only-list, the host's ledger (2981 before its 2661,
//    replacement, deletion, the host's 2322, a new field), the trip (observer or not, from the 2174's owner),
//    the order (every bound actor announced by a 2005, the gate on user 2's own 2322 or 30 s after its 2144),
//    the 2981 only when user 2 has none, live 2661s, the 2810 of a delivered copy and the cancel of a pending
//    one, 2516, a refused push, bounds, and the C>S filter.
//  - guest_seq_route_logic.h: the routing decision reason by reason, the delivery gate, the opcode map; the NPC
//    schedule points (guest_sched_route): X+0x148, where a 2335 went, the follow-ups, the ledger, the census.
//  - observer_sched_copy (p8-impl-horarios-snapshot.md, same header): the schedule Acks' parsers on session 11
//    payloads, the fixed ledger (create, re-create, follow-ups, 2126, 2411, bounds, the host's 2174), the trip's
//    snapshot (order, user 2's own stages left out, gate, actor announced, native duplicate), the live
//    2126/2958/2909 (copied, native already, not user 2's, pending cancel / ride), refusals and bounds.
// They do NOT test the hooks, take_frame, the sniffer, the threads nor the game's behaviour.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "mod/session2/guest_seq_route_logic.h"
#include "mod/session2/observer_seq_copy_logic.h"

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

std::vector<uint8_t> hex(const char* s) {
    std::vector<uint8_t> out;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; s[i] && s[i + 1]; i += 2) out.push_back(static_cast<uint8_t>(nib(s[i]) << 4 | nib(s[i + 1])));
    return out;
}

// HC #48258/#48259 t=68.422: the street vendor's sequencer 0x19E (A0100001; 0x20000004 -> B0100157 Street Vendor,
// 0x20000008 -> B0100158 Elantrima Packhorse), its 2981 right before its 2661 (the native order).
const char* k2981_19E = "a50bff1d0004010010a09e0100000000000000000000ffffffff9d909c9700000000";
const char* k2661_19E =
    "650aff7c0004010010a0010010a09e010000000000000100d94f0f00000000005ec8426fd94f0f00def528c6df6a1844f06479c54d02bfbf"
    "0000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d02000020010010a005000004000020570110b0"
    "05000008000020580110b0050000";
// HC #364660 t=452.482: its DeleteSequencerAck (u8 7).
const char* k2810_19E = "fa0aff1100010010a09e01000000000000d94f0f0007";
// HC #365051/#365052 t=452.581: Grover's 0x1AB (A0100001 + 0x20000004 -> B0100C0F).
const char* k2981_1AB = "a50bff1d0004010010a0ab0100000000000000000000ffffffff9d909c9700000000";
const char* k2661_1AB =
    "650aff710004010010a0010010a0ab01000000000000010009450f0000000000275501aa09450f00ea3327c6e4991b44466c6dc50c3ef13e"
    "0000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d02000020010010a0050000040000200f0c10b0"
    "050000";
// HC #365067/#365068 t=452.588: Dahlia's 0x1AD (A0100001 + 0x20000004 -> B0100C12).
const char* k2981_1AD = "a50bff1d0004010010a0ad0100000000000000000000ffffffff9d909c9700000000";
const char* k2661_1AD =
    "650aff710004010010a0010010a0ad0100000000000001003843934100000000dfa560ea38439341a9a026c6c3401a44b6d96fc5cc040740"
    "0000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d02000020010010a005000004000020120c10b0"
    "050000";
// HC #364871 t=452.565: the innkeeper's 0x1A9 (8 bindings: A0100001, gimmick C08, Host C09, 5 Wandering Freesword).
const char* k2661_1A9 =
    "650affb30004010010a0010010a0a9010000000000000100ec37f40000000000aea6fe94ec37f400979726c681481a443a706fc58679413f"
    "0000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d02000020010010a005000011000020080c10b0"
    "05000004000020090c10b0050000080000200a0c10b0050000090000200b0c10b00500000b0000200c0c10b00500000c0000200d0c10b0050000"
    "0d0000200e0c10b0050000";
// HC #797: the host's PlayAllPreLoadSequencerAck (its character A0100001).
const char* k2322_host = "1209ff0400010010a0";
// HC #1192 t=60.381: the host's MoveFieldAck to its world field (owner 0x90100000 at +9). GC #1198: user 2's copy
// of a MoveFieldAck into that field, its owner already rewritten to user 2 (0x901002E8).
const char* k2174_host =
    "7e08ff410004010010a001000000000010900100000001000000010010a02a7727c6a00a1b44eaa874c5000000005f8f45bf0000000084ce22"
    "3f0000803f0000803f0000803f";
const char* k2174_guest_copy =
    "7e08ff410004e90210a001000000e80210900100000001000000e90210a02a7727c6a00a1b44eaa874c5000000005f8f45bf0000000084ce22"
    "3f0000803f0000803f0000803f";
// GC #215119: RemoveActorAck of the street vendor B0100157 (reason 0x1E).
const char* k2516_157 = "d409ff0a0004000000001e570110b0";
// GC C>S: user 2's own ESC scene 0x1A3 (2591 #94016) and a 2173 naming its character (#954).
const char* k2591_own = "1f0a001100a301000000000000000000000000000001";
const char* k2173_char = "7d08000f0001000000e90210a00000006000000f";

constexpr uint32_t kHost = 0xA0100001, kUser2Char = 0xA01002E9;
constexpr uint32_t kHostUser = 0x90100000, kUser2User = 0x901002E8;

std::span<const uint8_t> payload_of(const std::vector<uint8_t>& p) { return std::span<const uint8_t>(p).subspan(5); }

// A 2005 payload with these records (type, key) (the real ones are 7 KB; the parser reads only the record heads).
std::vector<uint8_t> make_2005(std::initializer_list<std::pair<int, uint32_t>> recs) {
    std::vector<uint8_t> p;
    for (const auto& [type, key] : recs) {
        const uint16_t len = 12;
        p.push_back(static_cast<uint8_t>(len));
        p.push_back(static_cast<uint8_t>(len >> 8));
        p.push_back(static_cast<uint8_t>(type));
        for (int i = 0; i < 4; ++i) p.push_back(static_cast<uint8_t>(key >> (8 * i)));
        for (int i = 0; i < 7; ++i) p.push_back(0);
    }
    return p;
}

struct Recorder {
    std::vector<std::vector<uint8_t>> out;
    bool refuse = false;
    bool operator()(std::span<const uint8_t> b) {
        if (refuse) return false;
        out.emplace_back(b.begin(), b.end());
        return true;
    }
};

void feed(osq::Ledger& l, const char* h, uint64_t t_us, osq::HostSeen expect) {
    const std::vector<uint8_t> p = hex(h);
    const osq::HostSeen s = l.on_host_packet(p, t_us);
    if (s != expect) std::printf("  ledger: got %s, expected %s\n", osq::host_seen_name(s), osq::host_seen_name(expect));
    CHECK(s == expect);
}

void test_mode_and_filter() {
    bool bad = false;
    CHECK(osq::parse_mode("0") == osq::Mode::Off);
    CHECK(osq::parse_mode("") == osq::Mode::Off);
    CHECK(osq::parse_mode("probe") == osq::Mode::Probe);
    CHECK(osq::parse_mode("1") == osq::Mode::On);
    CHECK(osq::parse_mode("2", &bad) == osq::Mode::Off && bad);
    CHECK(gsr::parse_mode("probe") == gsr::Mode::Probe && gsr::parse_mode("1") == gsr::Mode::On);

    const osq::OnlyFilter f = osq::OnlyFilter::parse("1AB,1AD, 1A9 ,19E");
    CHECK(f.seqs.size() == 4 && f.keys.empty() && f.bad.empty());
    CHECK(f.seqs[0] == 0x1AB && f.seqs[3] == 0x19E);
    const uint32_t none[] = {kHost};
    CHECK(f.matches(0x1AB, none) && !f.matches(0x1AC, none));
    const osq::OnlyFilter k = osq::OnlyFilter::parse("0xB0100C0F;13,zz");
    CHECK(k.keys.size() == 1 && k.keys[0] == 0xB0100C0F && k.seqs.size() == 1 && k.seqs[0] == 0x13 && k.bad == "zz");
    const uint32_t grover[] = {kHost, 0xB0100C0F};
    CHECK(k.matches(0x999, grover) && !k.matches(0x999, none) && k.matches(0x13, none));
    CHECK(osq::OnlyFilter::parse("").empty() && osq::OnlyFilter::parse("").matches(0x1, none));
}

void test_parsers() {
    const auto p = osq::parse_prepare(payload_of(hex(k2661_1A9)));
    CHECK(p && p->kind == 4 && p->actor == kHost && p->owner == kHost && p->seq == 0x1A9 && p->bindings.size() == 8);
    if (p) {
        const std::vector<uint32_t> req = osq::required_actors(*p);
        CHECK(req.size() == 8 && req[0] == kHost && req[2] == 0xB0100C09);
    }
    CHECK(osq::branch_seq(payload_of(hex(k2981_1AB))) == 0x1ABull);
    CHECK(osq::delete_seq(payload_of(hex(k2810_19E))) == 0x19Eull);
    std::vector<uint32_t> keys;
    osq::removed_keys(payload_of(hex(k2516_157)), keys);
    CHECK(keys.size() == 1 && keys[0] == 0xB0100157);
    keys.clear();
    osq::announce_keys(make_2005({{0, kHostUser}, {1, kHost}, {4, 0xB0100C0F}}), keys);
    CHECK(keys.size() == 2 && keys[0] == kHost && keys[1] == 0xB0100C0F);  // the user record (type 0) is skipped
    CHECK(osq::move_owner(payload_of(hex(k2174_host))) == kHostUser);
    CHECK(osq::move_owner(payload_of(hex(k2174_guest_copy))) == kUser2User);
    CHECK(!osq::move_owner(payload_of(hex(k2322_host))));
}

void test_ledger() {
    osq::Ledger l;
    feed(l, k2174_host, 1, osq::HostSeen::NewField);
    feed(l, k2981_19E, 10, osq::HostSeen::BranchEarly);  // native order: the 2981 first
    feed(l, k2661_19E, 11, osq::HostSeen::Prepared);
    const osq::SeqRec* r = l.find(0x19E);
    CHECK(r && r->branch == hex(k2981_19E) && r->prepare == hex(k2661_19E) && r->branches == 1);
    CHECK(r && r->required.size() == 3 && r->required[1] == 0xB0100157 && r->required[2] == 0xB0100158);
    CHECK(l.host_char() == kHost && l.known(0x19E) && !l.known(0x1A3));
    feed(l, k2322_host, 12, osq::HostSeen::Milestone);
    CHECK(l.milestone() == hex(k2322_host) && l.milestone_actor() == kHost);
    feed(l, k2981_1AB, 20, osq::HostSeen::BranchEarly);
    feed(l, k2661_1AB, 21, osq::HostSeen::Prepared);
    feed(l, k2661_1AB, 22, osq::HostSeen::Replaced);
    CHECK(l.find(0x1AB) && l.find(0x1AB)->branch == hex(k2981_1AB));  // kept across the replacement
    feed(l, k2981_1AB, 23, osq::HostSeen::Branch);
    CHECK(l.find(0x1AB)->branches == 2);
    const auto order = l.live_in_order();
    CHECK(order.size() == 2 && order[0]->seq == 0x19E && order[1]->seq == 0x1AB);
    feed(l, k2810_19E, 30, osq::HostSeen::Deleted);
    feed(l, k2810_19E, 31, osq::HostSeen::DeleteUnknown);
    CHECK(!l.find(0x19E) && l.known(0x19E) && l.live() == 1);
    // A 2661 whose owner is not its addressed actor (a scene of another character stored in this manager).
    std::vector<uint8_t> other = hex(k2661_1AD);
    other[5 + 5] = 0xE9;  // play data +0 -> 0xA01002E9 (user 2's character)
    other[5 + 6] = 0x02;
    CHECK(l.on_host_packet(other, 40) == osq::HostSeen::NotOwn && !l.find(0x1AD));
    feed(l, k2174_host, 50, osq::HostSeen::NewField);
    CHECK(l.live() == 0 && l.known(0x1AB));
    // Bounds.
    osq::Ledger small(osq::LedgerLimits{1, 2, 8});
    CHECK(small.on_host_packet(hex(k2661_1AB), 1) == osq::HostSeen::Prepared);
    CHECK(small.on_host_packet(hex(k2661_1AD), 2) == osq::HostSeen::Full);
    for (const char* b : {k2981_19E, k2981_1AD, k2981_19E}) small.on_host_packet(hex(b), 3);
    CHECK(small.early() <= 2);
}

// The street vendor's 0x19E prepared before user 2 came (snapshot), Grover's 0x1AB while it observes (live).
void test_trip_snapshot_and_live() {
    osq::Ledger l;
    for (const char* h : {k2174_host, k2981_19E, k2661_19E, k2322_host}) l.on_host_packet(hex(h), 1);
    osq::Copier c;
    c.set_filter(osq::OnlyFilter::parse("1AB,1AD,1A9,19E"));
    Recorder rec;
    uint64_t now = 1'000'000;
    CHECK(c.on_user2_move(true, now, l) == 1);
    CHECK(c.observer() && c.pending() == 1 && !c.gate_is_open());
    // The 2005 burst: the host's Kliff first, the vendor, not yet its packhorse.
    std::vector<uint32_t> k = {kHost, 0xB0100157};
    c.on_user2_announce(k);
    CHECK(c.release(now, l, std::ref(rec)) == 0 && rec.out.empty());
    CHECK(c.waiting_for(0x19E, l) == std::vector<uint32_t>{0xB0100158});
    k = {0xB0100158};
    c.on_user2_announce(k);
    CHECK(c.release(now, l, std::ref(rec)) == 0);  // the gate: user 2's own 2322 has not gone yet
    c.on_user2_milestone();
    std::vector<osq::Released> rel;
    CHECK(c.release(now + 5, l, std::ref(rec), &rel) == 2);
    CHECK(rec.out.size() == 3);  // 2981, 2661, then the host's 2322
    if (rec.out.size() == 3) {
        CHECK(rec.out[0] == hex(k2981_19E) && rec.out[1] == hex(k2661_19E) && rec.out[2] == hex(k2322_host));
    }
    CHECK(rel.size() == 2 && rel[0].kind == osq::Released::Pair && rel[0].with_branch &&
          rel[0].origin == osq::Origin::Snapshot && rel[1].kind == osq::Released::Milestone);
    CHECK(c.delivered(0x19E) && c.pending() == 0 && c.milestone_sent());
    // Live: the host's client gets Grover's sequencer; its 2981 reached user 2 natively (near finder) first.
    rec.out.clear();
    c.on_user2_branch(0x1AB, osq::packet_payload_hash(hex(k2981_1AB)));  // the same bytes the host gets
    l.on_host_packet(hex(k2981_1AB), 2);
    l.on_host_packet(hex(k2661_1AB), 3);
    CHECK(c.on_host_prepared(*l.find(0x1AB), now + 10, l));
    CHECK(c.release(now + 10, l, std::ref(rec)) == 0);  // Grover's 2005 (observer_hold) not yet
    k = {0xB0100C0F};
    c.on_user2_announce(k);
    CHECK(c.release(now + 20, l, std::ref(rec)) == 1);
    CHECK(rec.out.size() == 1 && rec.out[0] == hex(k2661_1AB));  // no second 2981, no second 2322
    // Tina's 0x1AC would be filtered out by the list; Dahlia's 0x1AD is not.
    std::vector<uint8_t> tina = hex(k2661_1AD);
    tina[5 + 9] = 0xAC;
    l.on_host_packet(tina, 4);
    CHECK(!c.on_host_prepared(*l.find(0x1AC), now + 30, l));
    CHECK(c.counters().filtered == 1);
    // The host deletes the vendor's sequencer: user 2 got its copy, so it gets the 2810 too.
    l.on_host_packet(hex(k2810_19E), 5);
    CHECK(c.on_host_deleted(0x19E) == osq::Copier::DeleteFate::Copy && !c.delivered(0x19E));
    CHECK(c.on_host_deleted(0x19E) == osq::Copier::DeleteFate::None);
    // Dahlia pending, then deleted before her 2005: cancelled, never sent.
    l.on_host_packet(hex(k2661_1AD), 6);
    CHECK(c.on_host_prepared(*l.find(0x1AD), now + 40, l) && c.pending() == 1);
    CHECK(c.on_host_deleted(0x1AD) == osq::Copier::DeleteFate::Cancelled && c.pending() == 0);
    // A bound actor leaves user 2's view: the delivered sequencer that binds it is reported.
    std::vector<uint32_t> gone = {0xB0100C0F};
    const std::vector<uint64_t> hit = c.on_user2_remove(gone, l);
    CHECK(hit.size() == 1 && hit[0] == 0x1AB && !c.announced(0xB0100C0F));
    // A new trip: everything of the old one ends; into its own field: nothing pending.
    CHECK(c.on_user2_move(false, now + 50, l) == 0 && !c.observer() && c.pending() == 0 && !c.delivered(0x1AB));
    CHECK(c.counters().trips == 2 && c.counters().observer_trips == 1);
}

// P8 session 14 review M2 (observer_load_defer): the host's 2322 waits for the snapshot copies whose actors reach
// user 2 only after its 2762, at most 2762 + hold (or the trip + kMilestoneHoldMaxUs without a 2762).
void test_milestone_hold() {
    osq::Ledger l;
    for (const char* h : {k2174_host, k2981_19E, k2661_19E, k2322_host}) l.on_host_packet(hex(h), 1);
    const uint64_t t0 = 1'000'000;
    {
        // 1. Without the hold (as before): the 2322 goes before the snapshot copy, which then goes after it.
        osq::Copier c;
        Recorder rec;
        CHECK(c.on_user2_move(true, t0, l) == 1);
        std::vector<uint32_t> k = {kHost};
        c.on_user2_announce(k);
        c.on_user2_milestone();
        CHECK(c.release(t0 + 1, l, std::ref(rec)) == 1 && rec.out.size() == 1 && rec.out[0] == hex(k2322_host));
        k = {0xB0100157, 0xB0100158};
        c.on_user2_announce(k);
        std::vector<osq::Released> rel;
        CHECK(c.release(t0 + 2, l, std::ref(rec), &rel) == 1 && rel.size() == 1 && rel[0].after_milestone);
        CHECK(c.counters().snapshot_after_milestone == 1 && c.counters().milestone_holds == 0);
    }
    {
        // 2. With the hold: the 2322 waits; the copy goes after user 2's 2762 (its actors' 2005), then the 2322.
        osq::Copier c;
        c.set_milestone_hold(2'000'000);
        Recorder rec;
        CHECK(c.on_user2_move(true, t0, l) == 1);
        std::vector<uint32_t> k = {kHost};
        c.on_user2_announce(k);
        c.on_user2_milestone();
        CHECK(c.release(t0 + 1, l, std::ref(rec)) == 0 && rec.out.empty() && c.milestone_waiting());
        c.on_user2_loaded(t0 + 10'000'000);
        CHECK(c.release(t0 + 10'000'001, l, std::ref(rec)) == 0 && rec.out.empty());
        k = {0xB0100157, 0xB0100158};
        c.on_user2_announce(k);
        std::vector<osq::Released> rel;
        CHECK(c.release(t0 + 10'500'000, l, std::ref(rec), &rel) == 2 && rec.out.size() == 3);
        if (rec.out.size() == 3) {
            CHECK(rec.out[0] == hex(k2981_19E) && rec.out[1] == hex(k2661_19E) && rec.out[2] == hex(k2322_host));
        }
        CHECK(rel.size() == 2 && !rel[0].after_milestone);
        const auto cs = c.counters();
        CHECK(cs.milestone_holds == 1 && cs.milestone_hold_expired == 0 && cs.snapshot_after_milestone == 0);
    }
    {
        // 3. The hold runs out 2 s after the 2762 with the copy still waiting: the 2322 goes (counted).
        osq::Copier c;
        c.set_milestone_hold(2'000'000);
        Recorder rec;
        c.on_user2_move(true, t0, l);
        std::vector<uint32_t> k = {kHost};
        c.on_user2_announce(k);
        c.on_user2_milestone();
        c.on_user2_loaded(t0 + 10'000'000);
        CHECK(c.release(t0 + 11'999'999, l, std::ref(rec)) == 0);
        CHECK(c.release(t0 + 12'000'000, l, std::ref(rec)) == 1 && rec.out.size() == 1 &&
              rec.out[0] == hex(k2322_host));
        CHECK(c.counters().milestone_hold_expired == 1);
    }
    {
        // 4. No 2762 at all: at most kMilestoneHoldMaxUs after the trip.
        osq::Copier c;
        c.set_milestone_hold(2'000'000);
        Recorder rec;
        c.on_user2_move(true, t0, l);
        std::vector<uint32_t> k = {kHost};
        c.on_user2_announce(k);
        c.on_user2_milestone();
        CHECK(c.release(t0 + osq::Copier::kMilestoneHoldMaxUs - 1, l, std::ref(rec)) == 0);
        CHECK(c.release(t0 + osq::Copier::kMilestoneHoldMaxUs, l, std::ref(rec)) == 1);
    }
    {
        // 5. Nothing of the snapshot waiting: the hold does not delay the 2322.
        osq::Copier c;
        c.set_milestone_hold(2'000'000);
        Recorder rec;
        c.on_user2_move(true, t0, l);
        std::vector<uint32_t> k = {kHost, 0xB0100157, 0xB0100158};
        c.on_user2_announce(k);
        c.on_user2_milestone();
        CHECK(c.release(t0 + 1, l, std::ref(rec)) == 2 && rec.out.size() == 3 && c.counters().milestone_holds == 0);
    }
}

void test_gate_timeout_refusal_and_bounds() {
    osq::Ledger l;
    for (const char* h : {k2981_1AB, k2661_1AB, k2981_1AD, k2661_1AD}) l.on_host_packet(hex(h), 1);
    osq::Copier c;
    c.set_milestone(true);  // no 2322 in this ledger: nothing to send for it
    Recorder rec;
    const uint64_t t0 = 5'000'000;
    CHECK(c.on_user2_move(true, t0, l) == 2);
    std::vector<uint32_t> k = {kHost, 0xB0100C0F, 0xB0100C12};
    c.on_user2_announce(k);
    c.on_user2_complete(t0 + 1'000'000);
    CHECK(c.release(t0 + 30'000'000, l, std::ref(rec)) == 0);  // 29 s after the 2144
    CHECK(!c.gate_is_open() && rec.out.empty());
    CHECK(c.release(t0 + 31'000'001, l, std::ref(rec)) == 2);  // 30 s after it: the gate opens without its 2322
    CHECK(c.gate_is_open() && c.counters().gate_by_timeout == 1 && rec.out.size() == 4);
    // The queue refused: everything stays pending, in order, and goes next time.
    osq::Copier d;
    CHECK(d.on_user2_move(true, t0, l) == 2);
    d.on_user2_announce(k);
    d.on_user2_milestone();
    Recorder r2;
    r2.refuse = true;
    std::vector<osq::Released> rel;
    CHECK(d.release(t0, l, std::ref(r2), &rel) == 0 && rel.size() == 1 && !rel[0].pushed && d.pending() == 2);
    r2.refuse = false;
    CHECK(d.release(t0 + 1, l, std::ref(r2)) == 2);
    CHECK(r2.out.size() == 4 && r2.out[0] == hex(k2981_1AB) && r2.out[1] == hex(k2661_1AB) &&
          r2.out[2] == hex(k2981_1AD) && r2.out[3] == hex(k2661_1AD));
    // Not an observer trip: no snapshot, and 2100 passes.
    osq::Copier e;
    CHECK(e.on_user2_move(false, t0, l) == 0);
    const std::vector<uint8_t> pos(1549, 0x04);
    CHECK(e.judge_req(osq::kOpCandidatePos, pos, l) == osq::Copier::Req::Pass);
    // Bounds: one pending at most.
    osq::Copier f(osq::CopierLimits{1, 2, 4, 30'000'000});
    CHECK(f.on_user2_move(true, t0, l) == 1 && f.counters().pending_full == 1);
    k = {1, 2, 3};
    f.on_user2_announce(k);
    CHECK(f.announced_count() == 2 && f.counters().announced_overflow == 1);
}

void test_filter() {
    osq::Ledger l;
    for (const char* h : {k2661_1AB, k2661_1AD}) l.on_host_packet(hex(h), 1);
    osq::Copier c;
    c.on_user2_move(true, 1, l);
    // The host's own C>S for its sequencers, as user 2's client would send them about the copies.
    std::vector<uint8_t> tv = hex("ab01000000000000" "1a00002001" "0f0c10b0" "01");  // 2767 layout: seq @0
    CHECK(c.judge_req(osq::kOpTriggerVolume, tv, l) == osq::Copier::Req::HostSequencer);
    std::vector<uint8_t> chart = hex("ad01000000000000020000000000000001");
    CHECK(c.judge_req(osq::kOpChartEvent, chart, l) == osq::Copier::Req::HostSequencer);
    CHECK(c.judge_req(osq::kOpBranchReq, hex("ad01000000000000000000000000"), l) == osq::Copier::Req::HostSequencer);
    // User 2's own ESC scene 0x1A3 and a 2173 naming its character pass.
    CHECK(c.judge_req(osq::kOpChartEvent, payload_of(hex(k2591_own)), l) == osq::Copier::Req::Pass);
    CHECK(c.judge_req(osq::kOpGamePlayData, payload_of(hex(k2173_char)), l) == osq::Copier::Req::Pass);
    // 2100 while it observes; any other Req passes.
    const std::vector<uint8_t> pos(1549, 0x04);
    CHECK(c.judge_req(osq::kOpCandidatePos, pos, l) == osq::Copier::Req::Pass);  // default: observer_seq_copy_2100=0
    c.set_filter_2100(true);
    CHECK(c.judge_req(osq::kOpCandidatePos, pos, l) == osq::Copier::Req::Candidate);
    c.set_filter_2100(false);
    CHECK(c.judge_req(osq::kOpCandidatePos, pos, l) == osq::Copier::Req::Pass);
    CHECK(c.judge_req(osq::kOpChartEvent, chart, l) == osq::Copier::Req::HostSequencer);
    CHECK(c.judge_req(2556, chart, l) == osq::Copier::Req::Pass);
    CHECK(c.judge_req(osq::kOpChartEvent, hex("ad01"), l) == osq::Copier::Req::Pass);  // too short
    // guest_seq_play (p8-impl-escenas-play.md §5): 2276 DeleteSequencerReq (u64 seq | u8 code, 0xC02CB0) and 2105
    // StageBranchStateDelayEndReq (one u64) about the host's sequencers: dropped, each with its own kind.
    CHECK(c.judge_req(osq::kOpDeleteSeqReq, hex("ab0100000000000006"), l) == osq::Copier::Req::HostDelete);
    CHECK(c.judge_req(osq::kOpDelayEndReq, hex("ad01000000000000"), l) == osq::Copier::Req::HostDelayEnd);
    // An id the host's client never got, a short payload: pass.
    CHECK(c.judge_req(osq::kOpDeleteSeqReq, hex("a30100000000000006"), l) == osq::Copier::Req::Pass);
    CHECK(c.judge_req(osq::kOpDelayEndReq, hex("ad010000"), l) == osq::Copier::Req::Pass);
    // User 2's own sequencer: never dropped, even when the id is also in the ledger (a reused id).
    c.on_user2_own(0x1A3);
    CHECK(c.judge_req(osq::kOpDeleteSeqReq, hex("a30100000000000006"), l) == osq::Copier::Req::Pass);
    c.on_user2_own(0x1AB);
    CHECK(c.judge_req(osq::kOpDeleteSeqReq, hex("ab0100000000000006"), l) == osq::Copier::Req::Pass);
    CHECK(c.judge_req(osq::kOpDelayEndReq, hex("ab01000000000000"), l) == osq::Copier::Req::Pass);
    CHECK(c.judge_req(osq::kOpDelayEndReq, hex("ad01000000000000"), l) == osq::Copier::Req::HostDelayEnd);
    // Not an observer trip: the host's ids are still the host's (the filter is by id, as for 2591).
    osq::Copier n;
    CHECK(n.judge_req(osq::kOpDeleteSeqReq, hex("ad0100000000000006"), l) == osq::Copier::Req::HostDelete);
    // Session 9: 2888 ChangeSequencerPivotReq (u64 seq @0 | 16 bytes), the guest's real one about the host's 0x163
    // (GC1 t=85.111), here re-addressed to the ledger's 0x1AD; own and unknown ids pass; short passes.
    CHECK(c.judge_req(osq::kOpPivotReq, hex("ad01000000000000887327c691111b447bb074c5cb64e1bf"), l) ==
          osq::Copier::Req::HostPivot);
    CHECK(c.judge_req(osq::kOpPivotReq, hex("6301000000000000887327c691111b447bb074c5cb64e1bf"), l) ==
          osq::Copier::Req::Pass);  // 0x163 is not in this ledger
    CHECK(c.judge_req(osq::kOpPivotReq, hex("ab01000000000000887327c691111b447bb074c5cb64e1bf"), l) ==
          osq::Copier::Req::Pass);  // 0x1AB was marked user 2's own above
    CHECK(c.judge_req(osq::kOpPivotReq, hex("ad0100"), l) == osq::Copier::Req::Pass);
    // 2591 / 2436 (the live phase sends them): already dropped for a host id.
    CHECK(n.judge_req(osq::kOpChartEvent, hex("ad01000000000000020000000000000001"), l) == osq::Copier::Req::HostSequencer);
    CHECK(n.judge_req(osq::kOpBranchReq, hex("ad01000000000000000000000000"), l) == osq::Copier::Req::HostSequencer);
}

void test_guest_route() {
    gsr::RouteFacts f;
    f.mode = gsr::Mode::On;
    f.point_on = true;
    f.m = 0x1000;
    f.m_key = kUser2Char;
    f.actor = 0x2000;
    f.actor_key = kHost;
    f.packet_key = kHost;
    f.x = 0x3000;
    f.x_vt = f.want_vt = 0x1455C9720;
    f.x_key = kHost;
    CHECK(gsr::decide(f) == gsr::Verdict::Route);
    gsr::RouteFacts g = f;
    g.mode = gsr::Mode::Probe;
    CHECK(gsr::decide(g) == gsr::Verdict::WouldRoute);
    g = f, g.mode = gsr::Mode::Off;
    CHECK(gsr::decide(g) == gsr::Verdict::Off);
    g = f, g.point_on = false;
    CHECK(gsr::decide(g) == gsr::Verdict::NotHooked);
    g = f, g.kind_ok = false;
    CHECK(gsr::decide(g) == gsr::Verdict::NotKind4);
    g = f, g.m = 0;
    CHECK(gsr::decide(g) == gsr::Verdict::NoManager);
    g = f, g.actor = 0;
    CHECK(gsr::decide(g) == gsr::Verdict::NoActor);
    g = f, g.packet_key = 0xA0100002;
    CHECK(gsr::decide(g) == gsr::Verdict::KeyMismatch);
    g = f, g.actor_key = g.packet_key = 0xB0100C0F;
    CHECK(gsr::decide(g) == gsr::Verdict::NotCharacter);
    g = f, g.actor_key = g.packet_key = kUser2Char;  // user 2's own scene (ESC 0x1A3): native
    CHECK(gsr::decide(g) == gsr::Verdict::OwnCharacter);
    g = f, g.x = 0;
    CHECK(gsr::decide(g) == gsr::Verdict::NoX);
    g = f, g.x_vt = 0x1;
    CHECK(gsr::decide(g) == gsr::Verdict::NotStageManager);
    g = f, g.x_key = 0xA0100002;
    CHECK(gsr::decide(g) == gsr::Verdict::XNotItsOwn);
    g = f, g.x = g.m;
    CHECK(gsr::decide(g) == gsr::Verdict::AlreadyX);
    g = f, g.packet_key = 0;  // 2322: the key is the packet's, the actor found by it
    CHECK(gsr::decide(g) == gsr::Verdict::Route);

    CHECK(gsr::point_of_op(2661) == gsr::Point::P2661 && gsr::point_of_op(2322) == gsr::Point::P2322 &&
          gsr::point_of_op(2524) == gsr::Point::P2524 && !gsr::point_of_op(2111));
    // Session 9: the payload layouts the hooks read (p8-s9-npc.md §6.2; GC1/GC2 captures).
    {
        // GC2 t=61.409 2524: u8 4 | u32 actor @1 | u64 sequencer @5 | ... (the hook read @0/@4 before: x44 KeyMismatch)
        const std::vector<uint8_t> p = hex("04010010a0640100000000000001000000" "00");
        const auto b = gsr::branch_ids(p);
        CHECK(b && b->actor == kHost && b->seq == 0x164 && b->state == 1);
        static_assert(gsr::kBranchActorAt == 1 && gsr::kBranchSeqAt == 5 && gsr::kPrepareResourceAt == 27);
        CHECK(!gsr::branch_ids(hex("04010010a06401000000000000")));  // short
        // GC1 t=722.169 2661 of 0x213 (the camp liberation): resource @27 = 0x8CD771B1, stage @19 = 0xF4568.
        const std::vector<uint8_t> q = hex("04010010a0010010a0130200000000000001006845" "0f0000000000b171d78c68450f0000000000");
        const auto r = gsr::prepare_ids(q);
        CHECK(r && r->actor == kHost && r->seq == 0x213 && r->resource == 0x8CD771B1);
        CHECK(!gsr::prepare_ids(hex("04010010a0010010a013020000")));
        // The deny list (guest_seq_route_deny).
        const auto d = gsr::DenyList<4>::parse("8CD771B1, 0x13a55c51;zz 8cd771b1,0");
        CHECK(d.n == 2 && d.has(0x8CD771B1) && d.has(0x13A55C51) && !d.has(0x94FEA6AE) &&
              d.bad == "zz 0 (zero: no resource)");  // review 6: the 0 is reported, the repeat is not
        CHECK(gsr::DenyList<4>::parse("").n == 0 && gsr::DenyList<4>::parse("").bad.empty());
        const auto over = gsr::DenyList<2>::parse("1 2 3");  // review 6: the one over the limit is reported
        CHECK(over.n == 2 && over.has(1) && over.has(2) && !over.has(3) && over.bad == "3 (over the limit of the list)");
        CHECK(gsr::DenyList<2>::parse("123456789").bad == "123456789" && gsr::DenyList<2>::parse("0x").bad == "0x");
        // The resource table: the 2661's resource judges its later 2981 / 2524.
        gsr::SeqResources<2> t;
        t.note(0x213, 0x8CD771B1, true);
        t.note(0x164, 0x13A55C51, false);
        CHECK(t.find(0x213) && t.find(0x213)->denied && !t.find(0x164)->denied && !t.find(0x999) && !t.find(0));
        t.note(0x165, 1, false);  // a ring: the oldest goes
        CHECK(!t.find(0x213) && t.find(0x165));
        t.note(0x165, 2, true);   // the same id again: updated in place
        CHECK(t.find(0x165)->resource == 2 && t.find(0x165)->denied && t.find(0x164));
        // The verdict: a denied 2661 is refused (created nowhere); a denied 2981 / 2524 stays native.
        gsr::RouteFacts dn = f;
        dn.denied = true;
        CHECK(gsr::decide(dn) == gsr::Verdict::Denied);
        CHECK(gsr::refuse(gsr::Point::P2661, dn, gsr::Verdict::Denied));
        CHECK(!gsr::refuse(gsr::Point::P2981, dn, gsr::Verdict::Denied));
        CHECK(!gsr::refuse(gsr::Point::P2524, dn, gsr::Verdict::Denied));
        dn.mode = gsr::Mode::Probe;
        CHECK(gsr::decide(dn) == gsr::Verdict::Denied && !gsr::refuse(gsr::Point::P2661, dn, gsr::Verdict::Denied));
        gsr::RouteFacts own = f;  // the own character's packets are never judged by it
        own.denied = true, own.actor_key = own.packet_key = kUser2Char;
        CHECK(gsr::decide(own) == gsr::Verdict::OwnCharacter);
    }
    // guest_seq_play_live: the 2524 is routed, never gated (it arrives natively: 144 in session 8, into M).
    {
        const std::vector<uint8_t> p2524 = hex("04010010a0ab010000000000000100000000");  // u8 4 | u32 actor | u64 seq
        CHECK(!gsr::named_actor(2524, p2524));
        CHECK(gsr::gate(2524, p2524, kUser2Char, false) == gsr::Gate::Deliver);
        CHECK(gsr::gate(2524, p2524, 0, false) == gsr::Gate::Deliver);
        gsr::RouteFacts r = f;  // the 2524 point: no kind, the packet's key = the actor's
        r.need_known = true;    // only into an X guest_seq_play ticks (review 11)
        r.x_known = true;
        CHECK(gsr::decide(r) == gsr::Verdict::Route && !gsr::refuse(gsr::Point::P2524, r, gsr::Verdict::NoX));
        r.x_known = false;
        CHECK(gsr::decide(r) == gsr::Verdict::NotPlayed && !gsr::refuse(gsr::Point::P2524, r, gsr::Verdict::NotPlayed));
        r.mode = gsr::Mode::Probe;
        CHECK(gsr::decide(r) == gsr::Verdict::NotPlayed);
        r.x_known = true;
        CHECK(gsr::decide(r) == gsr::Verdict::WouldRoute);
        r = f, r.x_known = false;  // the other points do not ask
        CHECK(gsr::decide(r) == gsr::Verdict::Route);
        // Session 10 (review of b728ec7): the routing hook only checks that X and this client play (pre_2524);
        // the scene is checked inside 0xA9E480 under its lock (scene_2524). The crash at 0xA9E63B: both at 2,
        // [stage+0x38] 0.
        CHECK(gsr::pre_2524(3, 0, 3) == gsr::Ready2524::Ok);
        CHECK(gsr::pre_2524(2, 0, 2) == gsr::Ready2524::NotPlaying);  // the session 10 state
        CHECK(gsr::pre_2524(3, 0, 2) == gsr::Ready2524::NotPlaying);  // this client still loading
        CHECK(gsr::pre_2524(2, 0, 3) == gsr::Ready2524::NotPlaying);
        CHECK(gsr::pre_2524(3, 1, 3) == gsr::Ready2524::NotPlaying);  // a loading task in X's +0x174 map
        gsr::Scene2524 ok;
        ok.inst = 0x3598F717300, ok.stage = 0x359A9A17380, ok.data = 0x1000, ok.branches = 0x2000, ok.count = 3,
        ok.capacity = 4, ok.branch = 1, ok.entry = 0x3000;
        CHECK(gsr::scene_2524(ok) == gsr::Ready2524::Ok);
        gsr::Scene2524 crash = ok;  // the faulting read: the scene data still 0
        crash.data = 0, crash.branches = 0, crash.count = 0, crash.capacity = 0, crash.entry = 0;
        CHECK(gsr::scene_2524(crash) == gsr::Ready2524::SceneNotLoaded);
        gsr::Scene2524 g2 = ok;
        g2.inst = 0;  // the game did not find it: its own path, untouched
        CHECK(gsr::scene_2524(g2) == gsr::Ready2524::NoInstance);
        g2 = ok, g2.stage = 0;
        CHECK(gsr::scene_2524(g2) == gsr::Ready2524::SceneNotLoaded);
        g2 = ok, g2.branches = 0;
        CHECK(gsr::scene_2524(g2) == gsr::Ready2524::SceneNotLoaded);
        g2 = ok, g2.branch = 3;  // = count: out of the table
        CHECK(gsr::scene_2524(g2) == gsr::Ready2524::BadBranch);
        g2 = ok, g2.entry = 0;
        CHECK(gsr::scene_2524(g2) == gsr::Ready2524::BadBranch);
        g2 = ok, g2.count = 0;  // an empty table
        CHECK(gsr::scene_2524(g2) == gsr::Ready2524::BadBranch);
        g2 = ok, g2.count = 5;  // count > capacity: not a vector header (the layout assumption fails closed)
        CHECK(gsr::scene_2524(g2) == gsr::Ready2524::BadBranch);
        g2 = ok, g2.count = 2, g2.capacity = gsr::kMaxBranchTable + 1;
        CHECK(gsr::scene_2524(g2) == gsr::Ready2524::BadBranch);
        g2 = ok, g2.branch = 0, g2.count = 1, g2.capacity = 1;
        CHECK(gsr::scene_2524(g2) == gsr::Ready2524::Ok);
        // In the decision (only for the 2524: need_known).
        r = f, r.need_known = true, r.x_known = true;
        r.ready2524 = static_cast<uint8_t>(gsr::pre_2524(2, 0, 2));
        CHECK(gsr::decide(r) == gsr::Verdict::NotPlaying2524 && !gsr::refuse(gsr::Point::P2524, r, gsr::decide(r)));
        r.ready2524 = static_cast<uint8_t>(gsr::Ready2524::NoInstance);
        CHECK(gsr::decide(r) == gsr::Verdict::NoInstance2524);
        r.ready2524 = static_cast<uint8_t>(gsr::Ready2524::SceneNotLoaded);
        CHECK(gsr::decide(r) == gsr::Verdict::NotLoaded2524);
        r.ready2524 = static_cast<uint8_t>(gsr::Ready2524::BadBranch);
        CHECK(gsr::decide(r) == gsr::Verdict::BadBranch2524);
        r.ready2524 = static_cast<uint8_t>(gsr::Ready2524::Ok);
        CHECK(gsr::decide(r) == gsr::Verdict::Route);
        r.x_known = false, r.ready2524 = static_cast<uint8_t>(gsr::Ready2524::NotPlaying);  // not in the table first
        CHECK(gsr::decide(r) == gsr::Verdict::NotPlayed);
        r = f, r.ready2524 = static_cast<uint8_t>(gsr::Ready2524::NotPlaying);  // other points ignore it
        CHECK(gsr::decide(r) == gsr::Verdict::Route);
        for (size_t k = 0; k < static_cast<size_t>(gsr::Verdict::Count); ++k) {
            CHECK(std::string(gsr::verdict_name(static_cast<gsr::Verdict>(k))) != "?");
        }
    }
    const std::vector<uint8_t> p2661 = hex(k2661_1AB), p2810 = hex(k2810_19E), p2322 = hex(k2322_host);
    CHECK(gsr::named_actor(2661, payload_of(p2661)) == kHost && gsr::named_actor(2810, payload_of(p2810)) == kHost &&
          gsr::named_actor(2322, payload_of(p2322)) == kHost && !gsr::named_actor(2981, payload_of(p2661)));
    // The gate: the host's copies only when routed; the own key unknown or the own character: delivered.
    CHECK(gsr::gate(2661, payload_of(p2661), kUser2Char, false) == gsr::Gate::DropNotRouted);
    CHECK(gsr::gate(2661, payload_of(p2661), kUser2Char, true) == gsr::Gate::Deliver);
    CHECK(gsr::gate(2322, payload_of(p2322), kUser2Char, false) == gsr::Gate::DropNotRouted);
    CHECK(gsr::gate(2810, payload_of(p2810), kUser2Char, false) == gsr::Gate::DropNotRouted);
    // The own character not known yet on this connection: fail closed (none arrives before 2756 in session 7).
    CHECK(gsr::gate(2661, payload_of(p2661), 0, true) == gsr::Gate::DropOwnUnknown);
    CHECK(gsr::gate(2322, payload_of(hex("1209ff0400e90210a0")), 0, true) == gsr::Gate::DropOwnUnknown);
    CHECK(gsr::gate(2322, payload_of(hex("1209ff0400e90210a0")), kUser2Char, false) == gsr::Gate::Deliver);  // GC #800
    CHECK(gsr::gate(2005, payload_of(p2661), kUser2Char, false) == gsr::Gate::Deliver);
}

}  // namespace

// Review of fc49a0d: a re-prepared delivered sequencer, 2981 states, bounds, NewField, own ids, refuse.
void test_review_fixes() {
    const uint64_t t0 = 1'000'000;
    std::vector<uint32_t> k = {kHost, 0xB0100C0F};
    // 1. Delivered, prepared again (pending again), then deleted: its 2810 IS copied, the pending one cancelled.
    {
        osq::Ledger l;
        l.on_host_packet(hex(k2661_1AB), 1);
        osq::Copier c;
        c.on_user2_move(true, t0, l);
        c.on_user2_announce(k);
        c.on_user2_milestone();
        Recorder rec;
        CHECK(c.release(t0, l, std::ref(rec)) == 1 && c.delivered(0x1AB));
        CHECK(l.on_host_packet(hex(k2661_1AB), 2) == osq::HostSeen::Replaced);
        k = {};  // nothing announced: the new one stays pending
        osq::Copier::Counters before = c.counters();
        std::vector<uint32_t> gone = {0xB0100C0F};
        c.on_user2_remove(gone, l);
        CHECK(c.on_host_prepared(*l.find(0x1AB), t0 + 1, l) && c.pending() == 1 && c.delivered(0x1AB));
        CHECK(c.on_host_deleted(0x1AB) == osq::Copier::DeleteFate::Copy);
        CHECK(c.pending() == 0 && !c.delivered(0x1AB) && c.counters().cancelled == before.cancelled + 1);
        CHECK(c.on_host_deleted(0x1AB) == osq::Copier::DeleteFate::None);
        k = {kHost, 0xB0100C0F};
    }
    // 4. The 2981: by payload hash. User 2's last one is the ledger's -> none; another state -> included;
    //    a later host 2981 of a copied sequencer that did not reach user 2 -> host_branch_needs_copy.
    {
        osq::Ledger l;
        l.on_host_packet(hex(k2981_1AB), 1);
        l.on_host_packet(hex(k2661_1AB), 2);
        std::vector<uint8_t> newer = hex(k2981_1AB);
        newer[5 + 13] = 1;  // another branch value
        osq::Copier c;
        c.on_user2_move(true, t0, l);
        CHECK(!c.host_branch_needs_copy(0x1AB, osq::packet_payload_hash(newer)));  // not delivered yet
        c.on_user2_branch(0x1AB, osq::packet_payload_hash(newer));                 // user 2 got another state
        c.on_user2_announce(k);
        c.on_user2_milestone();
        Recorder rec;
        CHECK(c.release(t0, l, std::ref(rec)) == 1 && rec.out.size() == 2 && rec.out[0] == hex(k2981_1AB));
        CHECK(!c.host_branch_needs_copy(0x1AB, osq::packet_payload_hash(hex(k2981_1AB))));  // it has this one now
        CHECK(c.host_branch_needs_copy(0x1AB, osq::packet_payload_hash(newer)));
        c.on_user2_branch(0x1AB, osq::packet_payload_hash(newer));  // the native one went to user 2 first
        CHECK(!c.host_branch_needs_copy(0x1AB, osq::packet_payload_hash(newer)));
        // A re-prepare whose 2981 user 2 already got (native order: 2981 then 2661): no second 2981.
        l.on_host_packet(newer, 3);
        l.on_host_packet(hex(k2661_1AB), 4);
        CHECK(c.on_host_prepared(*l.find(0x1AB), t0 + 2, l));
        rec.out.clear();
        CHECK(c.release(t0 + 2, l, std::ref(rec)) == 1 && rec.out.size() == 1 && rec.out[0] == hex(k2661_1AB));
    }
    // 5. The pending table full: entries whose sequencer left the ledger make room (counted).
    {
        osq::Ledger l;
        l.on_host_packet(hex(k2661_1AB), 1);
        osq::Copier c(osq::CopierLimits{1, 64, 4, 64, 64, 30'000'000});
        CHECK(c.on_user2_move(true, t0, l) == 1);
        l.on_host_packet(hex(k2174_host), 2);  // the host's new field: 0x1AB leaves the ledger
        l.on_host_packet(hex(k2661_1AD), 3);
        CHECK(c.on_host_prepared(*l.find(0x1AD), t0 + 1, l) && c.pending() == 1 && c.counters().purged == 1);
        l.on_host_packet(hex(k2661_19E), 4);
        CHECK(!c.on_host_prepared(*l.find(0x19E), t0 + 2, l) && c.counters().pending_full == 1);
    }
    // 6. Delivered marks over their bound: the oldest goes (counted), its 2810 is no longer copied.
    {
        osq::Ledger l;
        l.on_host_packet(hex(k2661_1AB), 1);
        l.on_host_packet(hex(k2661_1AD), 2);
        osq::Copier c(osq::CopierLimits{8, 64, 1, 64, 64, 30'000'000});
        c.on_user2_move(true, t0, l);
        k = {kHost, 0xB0100C0F, 0xB0100C12};
        c.on_user2_announce(k);
        c.on_user2_milestone();
        Recorder rec;
        CHECK(c.release(t0, l, std::ref(rec)) == 2);
        CHECK(!c.delivered(0x1AB) && c.delivered(0x1AD) && c.counters().delivered_evicted == 1);
        CHECK(c.on_host_deleted(0x1AB) == osq::Copier::DeleteFate::None);
        CHECK(c.on_host_deleted(0x1AD) == osq::Copier::DeleteFate::Copy);
    }
    // 7. The host's new field forgets its last 2322: no stale milestone for user 2.
    {
        osq::Ledger l;
        l.on_host_packet(hex(k2322_host), 1);
        CHECK(!l.milestone().empty());
        l.on_host_packet(hex(k2174_host), 2);
        CHECK(l.milestone().empty() && l.milestone_actor() == 0);
        l.on_host_packet(hex(k2661_1AB), 3);
        osq::Copier c;
        c.on_user2_move(true, t0, l);
        k = {kHost, 0xB0100C0F};
        c.on_user2_announce(k);
        c.on_user2_milestone();
        Recorder rec;
        CHECK(c.release(t0, l, std::ref(rec)) == 1 && rec.out.size() == 1 && !c.milestone_sent());
    }
    // 11. User 2's own sequencer ids pass first; a new server forgets every id.
    {
        osq::Ledger l;
        l.on_host_packet(hex(k2661_1AB), 1);
        osq::Copier c;
        const std::vector<uint8_t> chart = hex("ab01000000000000020000000000000001");
        CHECK(c.judge_req(osq::kOpChartEvent, chart, l) == osq::Copier::Req::HostSequencer);
        c.on_user2_own(0x1AB);  // (a new server reused the id for user 2's own scene)
        CHECK(c.judge_req(osq::kOpChartEvent, chart, l) == osq::Copier::Req::Pass);
        c.clear_server();
        l.clear_all();
        CHECK(!c.own(0x1AB) && !l.known(0x1AB) && l.live() == 0);
        CHECK(c.judge_req(osq::kOpChartEvent, chart, l) == osq::Copier::Req::Pass);
    }
    // Guest: refuse only another character's 2661/2322 that cannot be routed, with the routing ON.
    {
        gsr::RouteFacts f;
        f.mode = gsr::Mode::On;
        f.point_on = true;
        f.m = 0x1000;
        f.m_key = kUser2Char;
        f.packet_key = kHost;
        CHECK(gsr::refuse(gsr::Point::P2661, f, gsr::Verdict::NoX));
        CHECK(gsr::refuse(gsr::Point::P2322, f, gsr::Verdict::NoActor));
        CHECK(!gsr::refuse(gsr::Point::P2981, f, gsr::Verdict::NoX));
        CHECK(!gsr::refuse(gsr::Point::P2810, f, gsr::Verdict::NoX));
        CHECK(!gsr::refuse(gsr::Point::P2661, f, gsr::Verdict::Route));
        gsr::RouteFacts own = f;
        own.packet_key = kUser2Char;  // this client's own 2322 (S2): never refused
        CHECK(!gsr::refuse(gsr::Point::P2322, own, gsr::Verdict::NoActor));
        gsr::RouteFacts probe = f;
        probe.mode = gsr::Mode::Probe;
        CHECK(!gsr::refuse(gsr::Point::P2661, probe, gsr::Verdict::NoX));
        gsr::RouteFacts npc = f;
        npc.packet_key = 0xB0100C0F;
        CHECK(!gsr::refuse(gsr::Point::P2661, npc, gsr::Verdict::NotCharacter));
    }
}

// ================================================================ observer_sched_copy (p8-impl-horarios-snapshot.md)
// Session 11 host capture H1 (npc-raiz-s11 schedules/phase1-packets.json): the 2126/2958/2909 payloads are whole
// (12/21/25/29 B); the 2335 is synthesized on its layout (kind | actor | u32 | resource | u8 | key | zeros), its
// first 22 bytes as H1's (only a 48-byte preview of each is kept there).

constexpr uint64_t kKeyA = 0x2000019E, kKeyB = 0x20000181, kKeyC = 0x2000016E, kKeyD = 0x20000148;

std::vector<uint8_t> pkt(uint16_t op, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> p = {static_cast<uint8_t>(op), static_cast<uint8_t>(op >> 8), 0xFF,
                              static_cast<uint8_t>(payload.size()), static_cast<uint8_t>(payload.size() >> 8)};
    p.insert(p.end(), payload.begin(), payload.end());
    return p;
}
void put32(std::vector<uint8_t>& p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void put64(std::vector<uint8_t>& p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
std::vector<uint8_t> make_2335(uint64_t key, uint32_t resource, uint8_t kind = 4, uint32_t actor = kHost,
                               size_t len = 94, uint8_t fill = 0) {
    std::vector<uint8_t> p = {kind};
    put32(p, actor);
    put32(p, 0x000F47B6);
    put32(p, resource);
    p.push_back(0);
    put64(p, key);
    while (p.size() < len) p.push_back(fill);
    return pkt(osq::kOpSchedCreate, p);
}
std::vector<uint8_t> make_2126(uint64_t key, uint32_t actor = kHost) {
    std::vector<uint8_t> p;
    put32(p, actor);
    put64(p, key);
    return pkt(osq::kOpSchedDelete, p);
}
std::vector<uint8_t> make_change(uint16_t op, uint64_t key, uint32_t resource, uint32_t node) {
    std::vector<uint8_t> p = {4};
    put32(p, kHost);
    put32(p, resource);
    put64(p, key);
    put32(p, node);
    return pkt(op, p);
}
std::vector<uint8_t> make_2411(uint32_t id, uint32_t actor = kHost) {
    std::vector<uint8_t> p;
    put32(p, actor);
    put32(p, id);
    return pkt(osq::kOpSchedClosed, p);
}
bool same(std::span<const uint8_t> a, const std::vector<uint8_t>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

void test_sched_parsers() {
    // H1 R286282 2126 (key 0x20000148), R284867 2958 (0x20000181), R(t409.6) 2909, R(t469.1) 2958 of 29 B.
    const auto d = osq::parse_sched(osq::kOpSchedDelete, hex("010010a048010020" "00000000"));
    CHECK(d && d->actor == kHost && d->key == kKeyD);
    const auto c = osq::parse_sched(osq::kOpSchedChange, hex("04010010a0f520ff8e810100200000000001000000"));
    CHECK(c && c->kind == 4 && c->actor == kHost && c->resource == 0x8EFF20F5 && c->key == kKeyB);
    const auto e = osq::parse_sched(osq::kOpSchedEvade, hex("04010010a0223ff6b96e01002000000000" "3b000000"));
    CHECK(e && e->resource == 0xB9F63F22 && e->key == kKeyC);
    CHECK(osq::parse_sched(osq::kOpSchedChange, hex("04010010a0223ff6b96e0100200000000029000000" "3b0000003f000000")));
    CHECK(!osq::parse_sched(osq::kOpSchedChange, hex("04010010a0223ff6b96e010020000000002900")));  // 19 B
    CHECK(!osq::parse_sched(osq::kOpSchedDelete, hex("010010a04801002000000000ff")));             // 13 B
    const std::vector<uint8_t> a = make_2335(kKeyA, 0xA8B92663);
    const auto cr = osq::parse_sched(osq::kOpSchedCreate, payload_of(a));
    CHECK(cr && cr->kind == 4 && cr->actor == kHost && cr->resource == 0xA8B92663 && cr->key == kKeyA);
    CHECK(!osq::parse_sched(osq::kOpSchedCreate, std::span<const uint8_t>(a).subspan(5, 21)));
    const auto cl = osq::parse_sched(osq::kOpSchedClosed, payload_of(make_2411(0x2000019E)));
    CHECK(cl && cl->actor == kHost && cl->closed_id == 0x2000019E);
    CHECK(osq::sched_op(2335) && osq::sched_op(2126) && osq::sched_op(2411) && osq::sched_op(2958) &&
          osq::sched_op(2909) && !osq::sched_op(2661) && !osq::sched_op(2874));
    CHECK(osq::sched_creates(4) && osq::sched_creates(1) && !osq::sched_creates(2));
    CHECK(osq::sched_live_index(2126) == 0 && osq::sched_live_index(2958) == 1 && osq::sched_live_index(2909) == 2 &&
          osq::sched_live_index(2335, 2) == 3 && osq::sched_live_index(2335, 4) == -1 && osq::sched_live_index(2411) == -1);
}

void test_sched_ledger() {
    auto l = std::make_unique<osq::SchedLedgerT<4>>();
    const std::vector<uint8_t> a = make_2335(kKeyA, 0xA8B92663), b = make_2335(kKeyB, 0x8EFF20F5),
                               c = make_2335(kKeyC, 0xB9F63F22);
    osq::SchedMsg m;
    CHECK(l->on_host_packet(a, 1, &m) == osq::SchedSeen::Created && m.key == kKeyA);
    CHECK(l->on_host_packet(b, 2) == osq::SchedSeen::Created);
    CHECK(l->on_host_packet(c, 3) == osq::SchedSeen::Created && l->live() == 3);
    uint64_t keys[4] = {};
    CHECK(l->keys_in_order(keys, 4) == 3 && keys[0] == kKeyA && keys[1] == kKeyB && keys[2] == kKeyC);
    CHECK(same(l->find(kKeyB)->create_bytes(), b) && l->find(kKeyB)->resource == 0x8EFF20F5);
    // Follow-ups: in order, after their 2335; of an unknown key, counted.
    const std::vector<uint8_t> chg = make_change(osq::kOpSchedChange, kKeyB, 0x8EFF20F5, 1);
    CHECK(l->on_host_packet(chg, 4) == osq::SchedSeen::Followed);
    CHECK(l->find(kKeyB)->nfollow == 1 && same(l->find(kKeyB)->follow_bytes(0), chg) &&
          l->find(kKeyB)->follow_op[0] == osq::kOpSchedChange);
    CHECK(l->on_host_packet(make_change(osq::kOpSchedEvade, kKeyD, 1, 1), 5) == osq::SchedSeen::FollowUnknown);
    // A non-creating 2335 (kind 2) rides after the creating one.
    CHECK(l->on_host_packet(make_2335(kKeyB, 0x8EFF20F5, 2, kHost, 40), 6) == osq::SchedSeen::Followed &&
          l->find(kKeyB)->nfollow == 2);
    // Created again: replaces it, follow-ups gone, last in the order.
    CHECK(l->on_host_packet(make_2335(kKeyA, 0xA8B92663, 4, kHost, 110), 7) == osq::SchedSeen::Recreated);
    CHECK(l->live() == 3 && l->find(kKeyA)->create_len == 115);
    CHECK(l->on_host_packet(make_2335(kKeyB, 0x8EFF20F5, 1), 8) == osq::SchedSeen::Recreated && l->find(kKeyB)->nfollow == 0);
    CHECK(l->keys_in_order(keys, 4) == 3 && keys[0] == kKeyC && keys[1] == kKeyA && keys[2] == kKeyB);
    // 2126 drops it.
    CHECK(l->on_host_packet(make_2126(kKeyC), 9) == osq::SchedSeen::Deleted && !l->find(kKeyC) && l->live() == 2);
    CHECK(l->on_host_packet(make_2126(kKeyC), 10) == osq::SchedSeen::DeleteUnknown);
    // 2411: the key's low half AND the same actor [E: layout].
    CHECK(l->on_host_packet(make_2411(static_cast<uint32_t>(kKeyB), kUser2Char), 11) == osq::SchedSeen::CloseUnknown);
    CHECK(l->closed_other_actor() == 1 && l->find(kKeyB));
    CHECK(l->on_host_packet(make_2411(static_cast<uint32_t>(kKeyB)), 12) == osq::SchedSeen::Closed && !l->find(kKeyB));
    CHECK(l->on_host_packet(make_2411(0x12345678), 13) == osq::SchedSeen::CloseUnknown);
    // Bounds: full, too big (an older one of that key goes), follow-ups over their bound (the oldest goes).
    CHECK(l->on_host_packet(make_2335(1, 1), 14) == osq::SchedSeen::Created);
    CHECK(l->on_host_packet(make_2335(2, 1), 15) == osq::SchedSeen::Created);
    CHECK(l->on_host_packet(make_2335(3, 1), 16) == osq::SchedSeen::Created && l->live() == 4);
    CHECK(l->on_host_packet(make_2335(4, 1), 17) == osq::SchedSeen::Full && l->full() == 1);
    CHECK(l->on_host_packet(make_2335(1, 1, 4, kHost, 600), 18) == osq::SchedSeen::TooBig && !l->find(1) &&
          l->live() == 3 && l->too_big() == 1);
    for (uint32_t i = 0; i < 7; ++i) l->on_host_packet(make_change(osq::kOpSchedChange, 2, 1, i), 20 + i);
    const osq::SchedEntry* e2 = l->find(2);
    CHECK(e2 && e2->nfollow == osq::SchedEntry::kFollow && e2->follow_lost && l->follow_dropped() == 1);
    CHECK(e2 && same(e2->follow_bytes(0), make_change(osq::kOpSchedChange, 2, 1, 1)));  // node 0 went
    CHECK(l->on_host_packet(pkt(osq::kOpSchedDelete, std::vector<uint8_t>(11, 0)), 30) == osq::SchedSeen::Bad);
    // The host's own 2174 empties it; the 2661 opcodes are not its business.
    CHECK(l->on_host_packet(hex(k2661_1AB), 31) == osq::SchedSeen::Other);
    CHECK(l->on_host_packet(hex(k2174_host), 32) == osq::SchedSeen::NewField && l->live() == 0 && !l->find(2));
    CHECK(l->on_host_packet(make_2335(9, 1), 33) == osq::SchedSeen::Created && l->live() == 1);
    l->clear_all();
    CHECK(l->live() == 0 && l->keys_in_order(keys, 4) == 0);
}

using SL = osq::SchedLedgerT<8>;
using ST = osq::SchedTripT<8, 16, 4>;
using Live = osq::SchedLive;
using Sent = osq::SchedSent;
using NativeR = osq::SchedNative;

// The host's live packet as the module feeds it: the ledger first, then the trip.
Live feed_live(SL& l, ST& t, const std::vector<uint8_t>& raw, uint64_t now, Recorder& rec) {
    const auto v = osq::split(raw);
    l.on_host_packet(raw, now);
    return t.on_host_live(*osq::parse_sched(v->op, v->payload), raw, now, std::ref(rec));
}
NativeR feed_native(const SL& l, ST& t, const std::vector<uint8_t>& raw, uint64_t now) {
    const auto v = osq::split(raw);
    return t.on_user2_native(v->op, v->payload, now, l);
}

void test_sched_snapshot_and_live() {
    auto l = std::make_unique<SL>();
    auto t = std::make_unique<ST>();
    const std::vector<uint8_t> a = make_2335(kKeyA, 0xA8B92663), b = make_2335(kKeyB, 0x8EFF20F5),
                               c = make_2335(kKeyC, 0xB9F63F22), d = make_2335(kKeyD, 0x11111111, 4, kUser2Char);
    const std::vector<uint8_t> chg_b = make_change(osq::kOpSchedChange, kKeyB, 0x8EFF20F5, 1);
    l->on_host_packet(a, 1);
    l->on_host_packet(b, 2);
    l->on_host_packet(chg_b, 3);
    l->on_host_packet(c, 4);
    l->on_host_packet(d, 5);  // a stage of user 2's own character: never part of the snapshot
    Recorder rec;
    // Not an observer trip: nothing pending, nothing live, no gate.
    CHECK(t->on_trip(false, 100, *l, kUser2Char) == 0 && t->active() && !t->observer());
    CHECK(feed_live(*l, *t, make_2126(kKeyD), 101, rec) == Live::None && !t->on_user2_loaded(1));
    l->on_host_packet(d, 102);
    // Observer trip: A, B, C in the ledger's order; D left out.
    CHECK(t->on_trip(true, 1000, *l, kUser2Char) == 3 && t->pending() == 3 && t->counters().excluded == 1);
    CHECK(t->first_pending() == kKeyA && !t->loaded());
    std::vector<Sent> sent;
    auto each = [&](const Sent& s) { sent.push_back(s); };
    std::vector<uint32_t> announced = {kHost};
    auto ann = [&](uint32_t k) { return std::find(announced.begin(), announced.end(), k) != announced.end(); };
    // User 2 still loading: nothing, even with the host's character announced.
    CHECK(t->release(1100, *l, ann, std::ref(rec), each) == 0 && rec.out.empty() && t->pending() == 3);
    // Its 2549 opens the gate (once); its 2144 later changes nothing.
    CHECK(t->on_user2_loaded(1) && !t->on_user2_loaded(2) && t->loaded() && t->load_signal() == 1);
    // The host character's 2005 (with 3 NPC records) went: the copies go right behind it, in order, byte for byte.
    t->on_user2_announced(3);
    CHECK(t->release(1200, *l, ann, std::ref(rec), each) == 3 && t->pending() == 0);
    CHECK(rec.out.size() == 4 && rec.out[0] == a && rec.out[1] == b && rec.out[2] == chg_b && rec.out[3] == c);
    CHECK(sent.size() == 3 && sent[0].kind == Sent::Copied && sent[0].npc_before == 3 && !sent[0].live);
    CHECK(sent[1].key == kKeyB && sent[1].follow == 1 && sent[1].waited_us == 200 &&
          sent[1].bytes == b.size() + chg_b.size());
    CHECK(t->has(kKeyA) && t->has(kKeyB) && t->has(kKeyC) && !t->has_native(kKeyA) && t->counters().copied == 3 &&
          t->counters().follow_copied == 1);
    CHECK(t->release(1300, *l, ann, std::ref(rec), each) == 0 && rec.out.size() == 4);
    rec.out.clear();
    // Live 2126 of a copied stage: copied once; the next one is no stage of user 2's.
    CHECK(feed_live(*l, *t, make_2126(kKeyA), 2000, rec) == Live::Forwarded && rec.out.size() == 1 && !t->has(kKeyA));
    CHECK(feed_live(*l, *t, make_2126(kKeyA), 2001, rec) == Live::NotUser2 && rec.out.size() == 1);
    // C's 2335 reaches user 2 natively AFTER our copy: counted and said, not merged.
    CHECK(feed_native(*l, *t, c, 2050) == NativeR::NativeAfterCopy && t->counters().native_after_copy == 1 &&
          !t->has_native(kKeyC));
    // A 2958 of C: the same payload went natively a moment before -> not repeated; another one -> copied.
    const std::vector<uint8_t> chg_c1 = make_change(osq::kOpSchedChange, kKeyC, 0xB9F63F22, 0x29);
    const std::vector<uint8_t> chg_c2 = make_change(osq::kOpSchedChange, kKeyC, 0xB9F63F22, 0x3F);
    feed_native(*l, *t, chg_c1, 2100);
    CHECK(feed_live(*l, *t, chg_c1, 2101, rec) == Live::NativeAlready && rec.out.size() == 1);
    CHECK(feed_live(*l, *t, chg_c2, 2102, rec) == Live::Forwarded && rec.out.size() == 2 && rec.out[1] == chg_c2);
    // Two equal real events, one of them native: one suppressed, the other copied (a hash is consumed once).
    feed_native(*l, *t, chg_c2, 2200);
    CHECK(feed_live(*l, *t, chg_c2, 2201, rec) == Live::NativeAlready);
    CHECK(feed_live(*l, *t, chg_c2, 2202, rec) == Live::Forwarded && rec.out.size() == 3);
    // A 2909 of B; a native hash too old matches nothing (10 s).
    CHECK(feed_live(*l, *t, make_change(osq::kOpSchedEvade, kKeyB, 0x8EFF20F5, 0x3B), 2300, rec) == Live::Forwarded);
    const std::vector<uint8_t> evade_b = make_change(osq::kOpSchedEvade, kKeyB, 0x8EFF20F5, 0x29);
    feed_native(*l, *t, evade_b, 3000);
    CHECK(feed_live(*l, *t, evade_b, 3000 + ST::kNativeTtlUs + 1, rec) == Live::Forwarded && rec.out.size() == 5);
    // The 2126 of C reached user 2 natively (its ToNear site): the host's copy is not repeated.
    feed_native(*l, *t, make_2126(kKeyC), 4000);
    CHECK(!t->has(kKeyC) && feed_live(*l, *t, make_2126(kKeyC), 4001, rec) == Live::NativeAlready && rec.out.size() == 5);
    // A 2958 of a stage user 2 does not have (its own character's, left out): not copied.
    CHECK(feed_live(*l, *t, make_change(osq::kOpSchedChange, kKeyD, 0x11111111, 1), 4100, rec) == Live::NotUser2);
    // A creating 2335 of the trip user 2 did NOT get natively: pending, copied behind its actor (gate open now).
    const std::vector<uint8_t> e = make_2335(0x200003BA, 0x54AC45AA), f = make_2335(0x200003C4, 0x54AC45AA);
    CHECK(feed_live(*l, *t, e, 4200, rec) == Live::Queued2335 && t->pending(0x200003BA) && rec.out.size() == 5);
    // ... its 2958 right after (H1 t878.266: 0.15 ms later) goes with it, in order.
    const std::vector<uint8_t> chg_e = make_change(osq::kOpSchedChange, 0x200003BA, 0x54AC45AA, 1);
    CHECK(feed_live(*l, *t, chg_e, 4201, rec) == Live::KeptInPending);
    sent.clear();
    CHECK(t->release(4202, *l, ann, std::ref(rec), each) == 1 && rec.out.size() == 7 && rec.out[5] == e &&
          rec.out[6] == chg_e && sent.size() == 1 && sent[0].live && t->has(0x200003BA));
    // One that reached user 2 natively: counted; its 2958 is copied (ToTarget).
    feed_native(*l, *t, f, 4300);
    CHECK(feed_live(*l, *t, f, 4301, rec) == Live::Native2335);
    CHECK(feed_live(*l, *t, make_change(osq::kOpSchedChange, 0x200003C4, 0x54AC45AA, 1), 4302, rec) == Live::Forwarded);
    // Created again while user 2 has it only by our copy: pending again, the new bytes go.
    const std::vector<uint8_t> e2 = make_2335(0x200003BA, 0x54AC45AA, 4, kHost, 110);
    CHECK(feed_live(*l, *t, e2, 4400, rec) == Live::Requeued2335);
    CHECK(t->release(4401, *l, ann, std::ref(rec), each) == 1 && rec.out.back() == e2);
    // A non-creating 2335 of a stage user 2 has rides like a 2958.
    CHECK(feed_live(*l, *t, make_2335(kKeyB, 0x8EFF20F5, 2, kHost, 40), 4500, rec) == Live::Forwarded);
    const ST::Counters k = t->counters();
    CHECK(k.live_fwd[0] == 1 && k.live_fwd[1] == 3 && k.live_fwd[2] == 2 && k.live_fwd[3] == 1 &&
          k.live_native[0] == 1 && k.live_native[1] == 2 && k.live_not_user2[0] == 1 && k.live_not_user2[1] == 1);
    CHECK(k.live_2335_queued == 1 && k.live_2335_requeued == 1 && k.live_2335_native == 1 && k.live_kept[1] == 1);
    // The trip ends: nothing more.
    t->clear();
    CHECK(!t->observer() && !t->loaded() && !t->has(kKeyB) && feed_live(*l, *t, make_2126(kKeyB), 4600, rec) == Live::None);
}

void test_sched_native_first() {
    std::vector<uint32_t> announced;
    auto ann = [&](uint32_t k) { return std::find(announced.begin(), announced.end(), k) != announced.end(); };
    std::vector<Sent> sent;
    auto each = [&](const Sent& s) { sent.push_back(s); };
    // 1. A's 2335 reaches user 2 natively before our copy (same bytes): it stands in; A's follow-ups still go
    //    (2958/2909 are ToTarget), also one that came after it, in order; B is copied.
    {
        auto l = std::make_unique<SL>();
        auto t = std::make_unique<ST>();
        const std::vector<uint8_t> a = make_2335(kKeyA, 1), b = make_2335(kKeyB, 2);
        const std::vector<uint8_t> fa1 = make_change(osq::kOpSchedChange, kKeyA, 1, 1),
                                   fa2 = make_change(osq::kOpSchedEvade, kKeyA, 1, 2);
        l->on_host_packet(a, 1);
        l->on_host_packet(fa1, 2);
        l->on_host_packet(b, 3);
        t->on_trip(true, 10, *l);
        t->on_user2_loaded(2);
        CHECK(feed_native(*l, *t, a, 20) == NativeR::DuplicateSame && t->has_native(kKeyA) &&
              t->pending_create_sent(kKeyA) && t->counters().dup_same == 1);
        Recorder rec;
        CHECK(feed_live(*l, *t, fa2, 21, rec) == Live::KeptInPending && rec.out.empty());
        announced = {kHost};
        CHECK(t->release(30, *l, ann, std::ref(rec), each) == 1 && t->pending() == 0);
        CHECK(rec.out.size() == 3 && rec.out[0] == fa1 && rec.out[1] == fa2 && rec.out[2] == b);
        CHECK(sent.size() == 2 && sent[0].kind == Sent::Duplicate && sent[0].same_bytes && sent[0].follow == 2);
        // A later native 2335 of B (after our copy): counted.
        CHECK(feed_native(*l, *t, b, 40) == NativeR::NativeAfterCopy);
        // Live follow-ups of A now: user 2 has it natively -> copied at once.
        CHECK(feed_live(*l, *t, make_change(osq::kOpSchedChange, kKeyA, 1, 3), 50, rec) == Live::Forwarded);
    }
    // 2. Other bytes: our copy and its follow-ups are dropped.
    {
        auto l = std::make_unique<SL>();
        auto t = std::make_unique<ST>();
        l->on_host_packet(make_2335(kKeyA, 1), 1);
        l->on_host_packet(make_change(osq::kOpSchedChange, kKeyA, 1, 1), 2);
        t->on_trip(true, 10, *l);
        t->on_user2_loaded(1);
        CHECK(feed_native(*l, *t, make_2335(kKeyA, 1, 4, kHost, 94, 0x55), 11) == NativeR::DuplicateOther);
        CHECK(t->pending() == 0 && t->counters().dup_other == 1 && t->has_native(kKeyA));
        Recorder rec;
        announced = {kHost};
        CHECK(t->release(20, *l, ann, std::ref(rec), each) == 0 && rec.out.empty());
    }
}

void test_sched_pending_refusal_bounds() {
    std::vector<uint32_t> announced = {kHost};
    auto ann = [&](uint32_t k) { return std::find(announced.begin(), announced.end(), k) != announced.end(); };
    std::vector<Sent> sent;
    auto each = [&](const Sent& s) { sent.push_back(s); };
    // 1. Before the gate: a 2126 cancels a pending stage; a 2958 goes with its stage.
    {
        auto l = std::make_unique<SL>();
        auto t = std::make_unique<ST>();
        l->on_host_packet(make_2335(kKeyA, 1), 1);
        l->on_host_packet(make_2335(kKeyB, 2), 2);
        CHECK(t->on_trip(true, 10, *l) == 2);
        Recorder rec;
        const std::vector<uint8_t> chg_b = make_change(osq::kOpSchedChange, kKeyB, 2, 7);
        CHECK(feed_live(*l, *t, make_2126(kKeyA), 20, rec) == Live::CancelledPending);
        CHECK(feed_live(*l, *t, chg_b, 21, rec) == Live::KeptInPending);
        CHECK(rec.out.empty() && t->pending() == 1 && t->counters().cancelled == 1);
        t->on_user2_loaded(2);
        CHECK(t->release(30, *l, ann, std::ref(rec), each) == 1 && rec.out.size() == 2 && rec.out[1] == chg_b);
    }
    // 2. The host's new field before the gate: the pending stages vanish.
    {
        auto l = std::make_unique<SL>();
        auto t = std::make_unique<ST>();
        l->on_host_packet(make_2335(kKeyA, 1), 1);
        CHECK(t->on_trip(true, 10, *l) == 1);
        l->on_host_packet(hex(k2174_host), 11);
        t->on_user2_loaded(1);
        Recorder rec;
        sent.clear();
        CHECK(t->release(30, *l, ann, std::ref(rec), each) == 0 && rec.out.empty() && t->pending() == 0);
        CHECK(sent.size() == 1 && sent[0].kind == Sent::Vanished && t->counters().vanished == 1);
    }
    // 3. Refusals: a refused 2335 stays pending (and the rest after it); a refused follow-up keeps the key pending
    //    FROM that follow-up: nothing lost, nothing twice; a follow-up arriving meanwhile goes in order.
    {
        auto l = std::make_unique<SL>();
        auto t = std::make_unique<ST>();
        const std::vector<uint8_t> a = make_2335(kKeyA, 1), b = make_2335(kKeyB, 2);
        const std::vector<uint8_t> f1 = make_change(osq::kOpSchedEvade, kKeyA, 1, 1),
                                   f2 = make_change(osq::kOpSchedEvade, kKeyA, 1, 2),
                                   f3 = make_change(osq::kOpSchedChange, kKeyA, 1, 3);
        l->on_host_packet(a, 1);
        l->on_host_packet(f1, 2);
        l->on_host_packet(f2, 3);
        l->on_host_packet(b, 4);
        t->on_trip(true, 10, *l);
        t->on_user2_loaded(1);
        Recorder rec;
        rec.refuse = true;
        sent.clear();
        CHECK(t->release(20, *l, ann, std::ref(rec), each) == 0 && t->pending() == 2 && t->counters().refused == 1 &&
              sent.size() == 1 && sent[0].kind == Sent::Refused);
        rec.refuse = false;
        size_t calls = 0;
        auto second_refused = [&](std::span<const uint8_t> x) {
            if (++calls == 2) return false;
            rec.out.emplace_back(x.begin(), x.end());
            return true;
        };
        sent.clear();
        CHECK(t->release(30, *l, ann, second_refused, each) == 1 && t->pending() == 2);
        CHECK(rec.out.size() == 1 && rec.out[0] == a && sent.size() == 1 && sent[0].follow_wait &&
              t->counters().follow_refused == 1 && t->pending_create_sent(kKeyA) && t->has(kKeyA));
        CHECK(feed_live(*l, *t, f3, 31, rec) == Live::KeptInPending && rec.out.size() == 1);
        sent.clear();
        CHECK(t->release(40, *l, ann, std::ref(rec), each) == 1 && t->pending() == 0);
        CHECK(rec.out.size() == 5 && rec.out[1] == f1 && rec.out[2] == f2 && rec.out[3] == f3 && rec.out[4] == b);
        CHECK(sent.size() == 2 && sent[0].kind == Sent::FollowUps && sent[0].follow == 3 && sent[1].kind == Sent::Copied);
        // Not pending any more and user 2 has it: its 2126 goes at once.
        CHECK(feed_live(*l, *t, make_2126(kKeyA), 50, rec) == Live::Forwarded && rec.out.size() == 6);
    }
    // 4. Follow-ups the ledger dropped while the key waited after a refusal: said (follow_lost), the rest goes.
    {
        auto l = std::make_unique<SL>();
        auto t = std::make_unique<ST>();
        l->on_host_packet(make_2335(kKeyA, 1), 1);
        l->on_host_packet(make_change(osq::kOpSchedChange, kKeyA, 1, 0), 2);
        t->on_trip(true, 10, *l);
        t->on_user2_loaded(1);
        Recorder rec;
        size_t calls = 0;
        auto second_refused = [&](std::span<const uint8_t> x) {
            if (++calls == 2) return false;
            rec.out.emplace_back(x.begin(), x.end());
            return true;
        };
        t->release(20, *l, ann, second_refused, each);
        for (uint32_t i = 1; i < 8; ++i) l->on_host_packet(make_change(osq::kOpSchedChange, kKeyA, 1, i), 20 + i);
        CHECK(l->find(kKeyA)->follow_total == 8 && l->find(kKeyA)->nfollow == osq::SchedEntry::kFollow);
        sent.clear();
        CHECK(t->release(40, *l, ann, std::ref(rec), each) == 0 && t->pending() == 0);
        CHECK(rec.out.size() == 7 && rec.out[1] == make_change(osq::kOpSchedChange, kKeyA, 1, 2));
        CHECK(sent.size() == 1 && sent[0].follow_lost && sent[0].follow == 6);
    }
    // 5. Created again while its follow-ups waited: the new 2335 goes (its own follow-ups from the start).
    {
        auto l = std::make_unique<SL>();
        auto t = std::make_unique<ST>();
        l->on_host_packet(make_2335(kKeyA, 1), 1);
        l->on_host_packet(make_change(osq::kOpSchedChange, kKeyA, 1, 0), 2);
        t->on_trip(true, 10, *l);
        t->on_user2_loaded(1);
        Recorder rec;
        size_t calls = 0;
        auto second_refused = [&](std::span<const uint8_t> x) {
            if (++calls == 2) return false;
            rec.out.emplace_back(x.begin(), x.end());
            return true;
        };
        t->release(20, *l, ann, second_refused, each);
        const std::vector<uint8_t> a2 = make_2335(kKeyA, 1, 4, kHost, 126);
        CHECK(feed_live(*l, *t, a2, 30, rec) == Live::KeptInPending);
        CHECK(t->release(40, *l, ann, std::ref(rec), each) == 1 && rec.out.size() == 2 && rec.out[1] == a2);
    }
    // 6. Natives outside an observer trip are not recorded; the stages user 2 has, the ring and the pending table
    //    are bounded (counted).
    {
        auto l = std::make_unique<SL>();
        auto t = std::make_unique<osq::SchedTripT<8, 2, 2>>();
        t->on_user2_native(osq::kOpSchedCreate, payload_of(make_2335(kKeyA, 1)), 1, *l);
        CHECK(!t->has(kKeyA) && t->counters().native[3] == 0);
        t->on_trip(true, 10, *l);
        for (uint64_t key : {kKeyA, kKeyB, kKeyC}) {
            t->on_user2_native(osq::kOpSchedCreate, payload_of(make_2335(key, 1)), 11, *l);
        }
        CHECK(t->has_count() == 2 && t->counters().has_full == 1 && !t->has(kKeyC));
        for (uint32_t i = 0; i < 3; ++i) {
            t->on_user2_native(osq::kOpSchedChange, payload_of(make_change(osq::kOpSchedChange, kKeyA, 1, i)), 20 + i, *l);
        }
        CHECK(t->counters().ring_overwritten == 1);
        Recorder rec;
        const std::vector<uint8_t> old = make_change(osq::kOpSchedChange, kKeyA, 1, 0);
        const std::vector<uint8_t> newer = make_change(osq::kOpSchedChange, kKeyA, 1, 2);
        CHECK(t->on_host_live(*osq::parse_sched(2958, payload_of(newer)), newer, 30, std::ref(rec)) == Live::NativeAlready);
        CHECK(t->on_host_live(*osq::parse_sched(2958, payload_of(old)), old, 31, std::ref(rec)) == Live::Forwarded);
    }
    {
        auto l = std::make_unique<SL>();
        auto t = std::make_unique<ST>();
        for (uint64_t i = 0; i < 8; ++i) l->on_host_packet(make_2335(0x20000100 + i, 1), i);
        CHECK(t->on_trip(true, 10, *l) == 8);
        l->on_host_packet(make_2126(0x20000100), 20);  // (straight into the ledger: the trip keeps it pending)
        Recorder rec;
        const std::vector<uint8_t> n = make_2335(0x20000200, 1);
        CHECK(feed_live(*l, *t, n, 21, rec) == Live::PendingFull && t->counters().pending_full == 1);
    }
    // 7. Session 11 scale: 46 stages live before the entry (phase 2), the real ledger bound; they all go right
    //    behind the 2005 of the host's character, before the NPC records that come later.
    {
        auto l = std::make_unique<osq::SchedLedger>();
        auto t = std::make_unique<osq::SchedTrip>();
        for (uint32_t i = 0; i < 60; ++i) l->on_host_packet(make_2335(0x20000010 + i, 0x1000 + i, 4, kHost, 94 + (i % 4) * 16), i);
        for (uint32_t i = 0; i < 14; ++i) l->on_host_packet(make_2126(0x20000010 + i * 4), 100 + i);
        CHECK(l->live() == 46);
        CHECK(t->on_trip(true, 1000, *l) == 46);
        t->on_user2_loaded(1);
        std::vector<uint32_t> none;
        auto ann_none = [&](uint32_t k) { return std::find(none.begin(), none.end(), k) != none.end(); };
        Recorder rec;
        uint64_t npc_before = 99;
        auto first_npc = [&](const Sent& s) { npc_before = std::min<uint64_t>(npc_before, s.npc_before); };
        t->on_user2_announced(0);  // a 2005 of the field's gimmicks
        CHECK(t->release(1500, *l, ann_none, std::ref(rec), first_npc) == 0);
        none.push_back(kHost);  // the 2005 with the host's character
        t->on_user2_announced(2);
        CHECK(t->release(2000, *l, ann_none, std::ref(rec), first_npc) == 46);
        CHECK(rec.out.size() == 46 && t->pending() == 0 && t->has_count() == 46 && npc_before == 2);
        const auto first = osq::parse_sched(2335, payload_of(rec.out[0]));
        const auto last = osq::parse_sched(2335, payload_of(rec.out[45]));
        CHECK(first && first->key == 0x20000011 && last && last->key == 0x20000010 + 59);
    }
}

// guest_sched_route (p8-impl-horarios-x.md): the NPC schedule stages into X, and the ledger that keeps the delete /
// changes of a scene key with its 2335. S11 phase 1 (H1 #286431 t=374.333 2335 of 0x2000019E, H1 #286282 2126 of
// 0x20000148, H1 #284867 2958 of 0x20000181), all naming the host's Kliff A0100001.
void test_guest_sched_route() {
    CHECK(gsr::kSchedOp[0] == 2335 && gsr::kSchedOp[1] == 2126 && gsr::kSchedOp[2] == 2958 && gsr::kSchedOp[3] == 2909);
    CHECK(std::string(gsr::sched_point_name(gsr::SchedPoint::P2126)).find("0xC0BA4E") != std::string::npos);
    gsr::RouteFacts f;
    f.mode = gsr::Mode::On;
    f.point_on = true;
    f.need_sched = true;
    f.x_sched_ok = true;
    f.m = 0x1000;
    f.m_key = kUser2Char;
    f.actor = 0x2000;
    f.actor_key = f.packet_key = kHost;
    f.x = 0x3000;
    f.x_vt = f.want_vt = 0x1455C9720;
    f.x_key = kHost;
    CHECK(gsr::decide(f) == gsr::Verdict::Route);
    gsr::RouteFacts g = f;
    g.x_sched_ok = false;  // [X+0x148] == 0: 0xA9FEE0 would return at once, 0x43C750 would get a null container
    CHECK(gsr::decide(g) == gsr::Verdict::NoSchedContainer);
    g.need_sched = false;  // the sequencer points never ask for it
    CHECK(gsr::decide(g) == gsr::Verdict::Route);
    g = f, g.mode = gsr::Mode::Probe;
    CHECK(gsr::decide(g) == gsr::Verdict::WouldRoute);
    g = f, g.actor_key = g.packet_key = kUser2Char;  // this client's own schedules: native
    CHECK(gsr::decide(g) == gsr::Verdict::OwnCharacter);
    g = f, g.actor_key = g.packet_key = 0xB0100083;
    CHECK(gsr::decide(g) == gsr::Verdict::NotCharacter);
    g = f, g.x = g.m;  // vf48 already gave X (the host's own client): native
    CHECK(gsr::decide(g) == gsr::Verdict::AlreadyX);
    g = f, g.actor = 0;  // the handler did not find the actor (0x8A67E0): native
    CHECK(gsr::decide(g) == gsr::Verdict::NoActor);
    g = f, g.packet_key = 0xA0100002;
    CHECK(gsr::decide(g) == gsr::Verdict::KeyMismatch);

    // Where a 2335 put its stage.
    CHECK(gsr::sched_created_where(gsr::Verdict::Route) == gsr::SchedWhere::X);
    CHECK(gsr::sched_created_where(gsr::Verdict::WouldRoute) == gsr::SchedWhere::X);
    CHECK(gsr::sched_created_where(gsr::Verdict::NoSchedContainer) == gsr::SchedWhere::M);
    CHECK(gsr::sched_created_where(gsr::Verdict::NoX) == gsr::SchedWhere::M);
    CHECK(gsr::sched_created_where(gsr::Verdict::KeyMismatch) == gsr::SchedWhere::M);
    CHECK(gsr::sched_created_where(gsr::Verdict::OwnCharacter) == gsr::SchedWhere::Unknown);
    CHECK(gsr::sched_created_where(gsr::Verdict::NotCharacter) == gsr::SchedWhere::Unknown);
    CHECK(gsr::sched_created_where(gsr::Verdict::Off) == gsr::SchedWhere::Unknown);
    CHECK(gsr::sched_created_where(gsr::Verdict::NotHooked) == gsr::SchedWhere::Unknown);

    // The follow-ups: where the 2335 went decides; X only if X is routable now.
    using W = gsr::SchedWhere;
    const auto R = gsr::Verdict::Route, P = gsr::Verdict::WouldRoute;
    CHECK(gsr::sched_followup(R, W::X, true, false) == R);
    CHECK(gsr::sched_followup(P, W::X, true, false) == P);
    CHECK(gsr::sched_followup(R, W::X, false, true) == gsr::Verdict::LedgerStale);  // another X since its 2335
    CHECK(gsr::sched_followup(R, W::M, true, true) == gsr::Verdict::CreatedInM);    // created in M: stays in M
    CHECK(gsr::sched_followup(R, W::Unknown, false, true) == R);                    // X's container has the key
    CHECK(gsr::sched_followup(R, W::Unknown, false, false) == gsr::Verdict::NotInX);
    CHECK(gsr::sched_followup(gsr::Verdict::NoX, W::X, true, true) == gsr::Verdict::NoX);  // X gone: native
    CHECK(gsr::sched_followup(gsr::Verdict::OwnCharacter, W::Unknown, false, false) == gsr::Verdict::OwnCharacter);

    // The ledger.
    gsr::SchedLedger<3> l;
    CHECK(!l.note(0, 0x3000, kHost, W::X));          // no key
    CHECK(!l.note(0x2000019E, 0x3000, kHost, W::Unknown));  // never judged: not noted
    CHECK(l.note(0x2000019E, 0x3000, kHost, W::X));  // 2335 #286431 routed to X
    CHECK(l.note(0x20000148, 0x1000, kHost, W::M));  // one created in M (X had no +0x148 then)
    const auto* e = l.find(0x2000019E);
    CHECK(e && e->where == W::X && e->mgr == 0x3000 && e->actor == kHost);
    CHECK(l.live(W::X) == 1 && l.live(W::M) == 1);
    CHECK(gsr::sched_followup(R, l.find(0x20000148)->where, false, true) == gsr::Verdict::CreatedInM);  // its 2126
    l.erase(0x20000148);  // the 2126 deleted it (in M)
    CHECK(!l.find(0x20000148) && l.live(W::M) == 0);
    CHECK(l.note(0x2000019E, 0x3000, kHost, W::X) && l.live(W::X) == 1);  // a repeated 2335: same entry
    CHECK(l.note(0x20000181, 0x3000, kHost, W::X) && l.note(0x20000182, 0x3000, kHost, W::X));
    CHECK(l.live(W::X) == 3 && l.evicted() == 0);
    CHECK(l.note(0x20000183, 0x3000, kHost, W::X) && l.evicted() == 1);  // the ring: the oldest slot goes
    CHECK(l.live(W::X) == 3 && !l.find(0) && !l.find(0x20000148));
    // An evicted key: Unknown, decided by X's container.
    size_t known = 0;
    for (uint64_t k : {0x2000019EULL, 0x20000181ULL, 0x20000182ULL, 0x20000183ULL}) known += l.find(k) != nullptr;
    CHECK(known == 3);

    // Lines per key.
    auto* m = l.find_mut(0x20000183);
    CHECK(m && m->lines == 0);
    int ok = 0;
    for (int i = 0; i < 10; ++i) ok += gsr::sched_line_ok(m);
    CHECK(ok == gsr::kSchedLinesPerKey);
    decltype(m) none = nullptr;
    CHECK(gsr::sched_line_ok(none));

    // The census.
    gsr::SchedCensus c;
    gsr::census_add_stage(c, true, true, 4, 2);
    gsr::census_add_stage(c, false, false, 3, 0);
    CHECK(c.stages == 2 && c.active == 1 && c.other_mgr == 1 && c.nodes == 7 && c.bound == 2);

    // The G5 state compares the binding fields too (a change is logged).
    gsr::NpcState a, b;
    a.exists = b.exists = true;
    b.stage = 0x5000;
    CHECK(!(a == b));
    b = a, b.stage_active = 1;
    CHECK(!(a == b));
}

// guest_sched_bind (p8-impl-horarios-enlace.md): the mode, the pending of one 2335 call, who is a candidate, the
// result read back, and the bounded walk of the client actor table on a fake memory.
struct FakeMem {
    std::map<uint64_t, uint64_t> q;  // 8-byte cells (u32 reads take the low half)
    void put32(uint64_t a, uint32_t v) { q[a] = (q.count(a) ? q[a] & ~0xFFFFFFFFull : 0) | v; }
    void put64(uint64_t a, uint64_t v) { q[a] = v; }
    bool u32(uint64_t a, uint32_t& v) const {
        auto it = q.find(a);
        if (it == q.end()) return false;
        v = static_cast<uint32_t>(it->second);
        return true;
    }
    bool ptr(uint64_t a, uint64_t& v) const {
        auto it = q.find(a);
        if (it == q.end()) return false;
        v = it->second;
        return true;
    }
};

void test_guest_sched_bind() {
    using M = gsr::Mode;
    bool refused = false;
    CHECK(gsr::bind_mode_of(M::On, M::On, &refused) == M::On && !refused);
    CHECK(gsr::bind_mode_of(M::On, M::Probe, &refused) == M::Probe && !refused);
    CHECK(gsr::bind_mode_of(M::Probe, M::On, &refused) == M::Off && refused);  // nothing goes into X in probe
    CHECK(gsr::bind_mode_of(M::Off, M::Probe, &refused) == M::Off && refused);
    CHECK(gsr::bind_mode_of(M::Off, M::Off, &refused) == M::Off && !refused);

    // The pending: armed only by a routed 2335; the frame and its scene key must still match at 0xC0B626.
    gsr::BindPending p;
    CHECK(!gsr::bind_pending_matches(p, 0x1000, 0x2000019E));
    p.armed = true, p.rbp = 0x1000, p.key = 0x2000019E, p.x = 0x3000, p.owner = kHost;
    CHECK(gsr::bind_pending_matches(p, 0x1000, 0x2000019E));
    CHECK(!gsr::bind_pending_matches(p, 0x1010, 0x2000019E));  // another frame
    CHECK(!gsr::bind_pending_matches(p, 0x1000, 0x20000181));  // the key slot changed under 0xA9FEE0
    gsr::BindPending q = p;
    q.x = 0;
    CHECK(!gsr::bind_pending_matches(q, 0x1000, 0x2000019E));
    q = p, q.owner = 0;
    CHECK(!gsr::bind_pending_matches(q, 0x1000, 0x2000019E));

    // Who is a candidate (S11: NPC 0xB010006D of the host's Kliff, scene 0x2000019E).
    gsr::BindActor a;
    a.key = 0xB010006D, a.comp = true, a.owner = kHost, a.scene = 0x2000019E, a.ai_path = true;
    CHECK(gsr::bind_judge(a, kHost, 0x2000019E) == gsr::BindWhy::Candidate);
    gsr::BindActor b = a;
    b.key = kHost;
    CHECK(gsr::bind_judge(b, kHost, 0x2000019E) == gsr::BindWhy::Self);
    b = a, b.key = 0;
    CHECK(gsr::bind_judge(b, kHost, 0x2000019E) == gsr::BindWhy::Self);
    b = a, b.comp = false;
    CHECK(gsr::bind_judge(b, kHost, 0x2000019E) == gsr::BindWhy::NoComp);
    b = a, b.owner = kUser2Char;  // the guest's own character's NPCs: never
    CHECK(gsr::bind_judge(b, kHost, 0x2000019E) == gsr::BindWhy::OtherOwner);
    b = a, b.scene = 0x20000181;
    CHECK(gsr::bind_judge(b, kHost, 0x2000019E) == gsr::BindWhy::OtherScene);
    CHECK(gsr::bind_judge(a, kHost, 0) == gsr::BindWhy::OtherScene);
    b = a, b.bound = 0x5000;  // vf13 (or an earlier call) bound it: never called again
    CHECK(gsr::bind_judge(b, kHost, 0x2000019E) == gsr::BindWhy::AlreadyBound);
    b = a, b.ai_path = false;  // 0x9F3075 / 0x9F307C would read a null pointer
    CHECK(gsr::bind_judge(b, kHost, 0x2000019E) == gsr::BindWhy::NoAiPath);
    CHECK(std::string(gsr::bind_why_name(gsr::BindWhy::NoAiPath)).find("null") != std::string::npos);

    // The result read back.
    CHECK(gsr::bind_result(0, 0, 0, 0x2000019E, 0x3000) == gsr::BindResult::StillZero);
    CHECK(gsr::bind_result(0x5000, 0x2000019E, 0x3000, 0x2000019E, 0x3000) == gsr::BindResult::Bound);
    CHECK(gsr::bind_result(0x5000, 0x2000019E, 0x1000, 0x2000019E, 0x3000) == gsr::BindResult::OtherStage);  // in M
    CHECK(gsr::bind_result(0x5000, 0x20000181, 0x3000, 0x2000019E, 0x3000) == gsr::BindResult::OtherStage);
    CHECK(std::string(gsr::bind_result_name(gsr::BindResult::Bound)).find("BOUND") != std::string::npos);
    CHECK(std::string(gsr::bind_result_name(gsr::BindResult::Deferred)).find("DEFERRED") != std::string::npos);

    // Session 12's retry. The queue: one entry per NPC key (a newer 2335 updates scene / X, keeps the first time),
    // bounded, swap-removal.
    {
        gsr::BindQueue<3> qu;
        gsr::BindRetry r;
        r.key = 0xB01000AA, r.owner = kHost, r.scene = 0x20000014, r.x = 0x3000, r.since_us = 100;
        CHECK(qu.add(r) && qu.n == 1);
        gsr::BindRetry r2 = r;
        r2.scene = 0x2000001B, r2.since_us = 900;
        CHECK(qu.add(r2) && qu.n == 1 && qu.e[0].scene == 0x2000001B && qu.e[0].since_us == 100);
        r2 = r, r2.key = 0;
        CHECK(!qu.add(r2) && qu.n == 1);  // no key: refused
        r2 = r, r2.key = 0xB01000BD;
        CHECK(qu.add(r2));
        r2.key = 0xB01000D0;
        CHECK(qu.add(r2) && qu.n == 3);
        r2.key = 0xB01000AB;
        CHECK(!qu.add(r2) && qu.full == 1 && qu.n == 3);  // full
        CHECK(qu.index_of(0xB01000BD) == 1);
        qu.remove_at(0);  // the last one moves into the hole
        CHECK(qu.n == 2 && qu.e[0].key == 0xB01000D0 && qu.index_of(0xB01000AA) == -1);
        qu.remove_at(5);  // out of range: nothing
        CHECK(qu.n == 2);
    }
    {
        gsr::BindRetry r;
        r.key = 0xB01000AA, r.owner = kHost, r.scene = 0x20000014, r.x = 0x3000, r.since_us = 1'000'000;
        gsr::RetryFacts f;
        f.found = true;
        f.actor = a;
        f.actor.key = 0xB01000AA, f.actor.scene = 0x20000014;
        f.table_ok = f.owner_known = true;
        f.logged_in = f.owner_logged_in = f.load_done = f.stage_in_x = f.owner_x_ok = true;
        const uint64_t now = 2'000'000;
        using S = gsr::RetryStep;
        CHECK(gsr::retry_judge(r, f, now) == S::Call);
        gsr::RetryFacts g = f;
        g.logged_in = false;  // session 12: +0x5E 0x08 while the field loads
        CHECK(gsr::retry_judge(r, g, now) == S::Wait);
        g = f, g.owner_logged_in = false;  // 0x9F2FA0's own owner lookup would return early
        CHECK(gsr::retry_judge(r, g, now) == S::Wait);
        g = f, g.load_done = false;  // M+0x231 != 3
        CHECK(gsr::retry_judge(r, g, now) == S::Wait);
        CHECK(gsr::retry_judge(r, g, r.since_us + gsr::kRetryTimeoutUs) == S::TimedOut);
        CHECK(gsr::retry_judge(r, g, r.since_us + gsr::kRetryTimeoutUs - 1) == S::Wait);
        g = f, g.actor.ai_path = false;  // its AI node path not there yet: waits (not called)
        CHECK(gsr::retry_judge(r, g, now) == S::Wait);
        g = f, g.found = false;
        CHECK(gsr::retry_judge(r, g, now) == S::Gone);
        g = f, g.actor.bound = 0x5000;  // vf13 / the game bound it meanwhile
        CHECK(gsr::retry_judge(r, g, now) == S::BoundMeanwhile);
        g = f, g.actor.owner = kUser2Char;
        CHECK(gsr::retry_judge(r, g, now) == S::Changed);
        g = f, g.actor.comp = false;
        CHECK(gsr::retry_judge(r, g, now) == S::Changed);
        g = f, g.stage_in_x = false;  // a 2126 removed the stage
        CHECK(gsr::retry_judge(r, g, now) == S::NoStage);
        g = f, g.owner_x_ok = false;
        CHECK(gsr::retry_judge(r, g, now) == S::OwnerMoved);
        g = f, g.stage_in_x = false, g.logged_in = false;  // what no longer applies is dropped even if not ready
        CHECK(gsr::retry_judge(r, g, now) == S::NoStage);
        CHECK(gsr::retry_due(5, 0) && !gsr::retry_due(100 + gsr::kRetryPeriodUs - 1, 100) &&
              gsr::retry_due(100 + gsr::kRetryPeriodUs, 100));
        CHECK(std::string(gsr::retry_step_name(S::TimedOut)).find("timed out") != std::string::npos);
        static_assert(gsr::kRetryCap >= 196 && gsr::kRetryCallsPerPass > 0 && gsr::kRetryStillZeroMax > 0);

        // Review of 51f9599 (1): the timeout applies to an entry that would be called (a call that keeps failing
        // must not keep it for ever); what no longer applies still wins over the timeout.
        const uint64_t late = r.since_us + gsr::kRetryTimeoutUs;
        CHECK(gsr::retry_judge(r, f, late) == S::TimedOut);
        CHECK(gsr::retry_judge(r, f, late - 1) == S::Call);
        g = f, g.actor.bound = 0x5000;
        CHECK(gsr::retry_judge(r, g, late) == S::BoundMeanwhile);
        // (3): without a whole table, or without a known owner, only the timeout can drop it, and nothing is called.
        g = f, g.table_ok = false, g.found = false;  // not "gone": the walk did not finish
        CHECK(gsr::retry_judge(r, g, now) == S::Wait);
        CHECK(gsr::retry_judge(r, g, late) == S::TimedOut);
        g = f, g.owner_known = false, g.owner_x_ok = false;  // an owner over the tracked ones / not in the table
        CHECK(gsr::retry_judge(r, g, now) == S::Wait);
        CHECK(gsr::retry_judge(r, g, late) == S::TimedOut);
        g = f, g.owner_known = false, g.stage_in_x = false;  // what the owner does not decide still drops it
        CHECK(gsr::retry_judge(r, g, now) == S::NoStage);

        // (1): after a call, a refusal / a still-0 keeps it until the cap, then it is dropped with its reason.
        gsr::BindRetry t = r;
        S why = S::Wait;
        CHECK(gsr::retry_keep_after(t, gsr::BindResult::NotFound, why) && t.refused == 1);
        CHECK(gsr::retry_keep_after(t, gsr::BindResult::NotFound, why) && t.refused == 2);
        CHECK(!gsr::retry_keep_after(t, gsr::BindResult::NotFound, why) && why == S::Refused &&
              t.refused == gsr::kRetryRefusedMax);
        t = r;
        CHECK(gsr::retry_keep_after(t, gsr::BindResult::StillZero, why) && t.still0 == 1 && t.refused == 0);
        CHECK(gsr::retry_keep_after(t, gsr::BindResult::StillZero, why));
        CHECK(!gsr::retry_keep_after(t, gsr::BindResult::StillZero, why) && why == S::StillZero);
        t = r;
        CHECK(!gsr::retry_keep_after(t, gsr::BindResult::Bound, why) && why == S::Call);
        CHECK(!gsr::retry_keep_after(t, gsr::BindResult::Changed, why) && why == S::Call);
        CHECK(!gsr::retry_keep_after(t, gsr::BindResult::OtherStage, why) && why == S::Call);
        CHECK(std::string(gsr::retry_step_name(S::Refused)).find("0x8AE1A0") != std::string::npos);

        // (1): the calls start where the last pass stopped, and wrap.
        CHECK(gsr::retry_rotated(0, 5, 0) == 0 && gsr::retry_rotated(0, 5, 4) == 4);
        CHECK(gsr::retry_rotated(3, 5, 0) == 3 && gsr::retry_rotated(3, 5, 2) == 0 && gsr::retry_rotated(3, 5, 4) == 2);
        CHECK(gsr::retry_rotated(7, 5, 0) == 2);  // a start left over from a longer queue
        CHECK(gsr::retry_rotated(3, 0, 0) == 0);
        {  // every index exactly once
            bool hit[7] = {};
            for (uint32_t i = 0; i < 7; ++i) hit[gsr::retry_rotated(4, 7, i)] = true;
            bool all = true;
            for (bool h : hit) all = all && h;
            CHECK(all);
        }
    }
    {  // remove_marked: the marked ones leave, the others keep their data.
        gsr::BindQueue<8> qu;
        for (uint32_t k = 1; k <= 6; ++k) {
            gsr::BindRetry r;
            r.key = 0xB0100000 + k, r.owner = kHost, r.scene = k;
            CHECK(qu.add(r));
        }
        bool mark[8] = {true, false, false, true, false, true, false, false};  // keys ..01, ..04, ..06
        qu.remove_marked(mark);
        CHECK(qu.n == 3);
        CHECK(qu.index_of(0xB0100001) < 0 && qu.index_of(0xB0100004) < 0 && qu.index_of(0xB0100006) < 0);
        for (uint32_t k : {2u, 3u, 5u}) {
            const int i = qu.index_of(0xB0100000 + k);
            CHECK(i >= 0 && qu.e[i].scene == k);
        }
        bool none[8] = {};
        qu.remove_marked(none);
        CHECK(qu.n == 3);
    }

    // The walk: 4 buckets; bucket 1 holds two entries, bucket 3 one whose node key does not match; a node index
    // with no node; an absent bucket count is skipped.
    using L = gsr::ActorTableLayout;
    FakeMem m;
    const uint64_t table = 0x10000, buckets = 0x20000, nodes = 0x30000;
    m.put32(table + L::kBucketCount, 4);
    m.put64(table + L::kBuckets, buckets);
    m.put64(table + L::kNodes, nodes);
    m.put32(buckets + 0 * L::kBucketStride, 0);
    m.put32(buckets + 1 * L::kBucketStride, 2);
    m.put32(buckets + 1 * L::kBucketStride + 8, 0xB010006D);
    m.put32(buckets + 1 * L::kBucketStride + 0xC, 5);
    m.put32(buckets + 1 * L::kBucketStride + 0x10, kHost);
    m.put32(buckets + 1 * L::kBucketStride + 0x14, 6);
    m.put32(buckets + 3 * L::kBucketStride, 2);
    m.put32(buckets + 3 * L::kBucketStride + 8, 0xB0100077);
    m.put32(buckets + 3 * L::kBucketStride + 0xC, 7);
    m.put32(buckets + 3 * L::kBucketStride + 0x10, 0xB0100083);
    m.put32(buckets + 3 * L::kBucketStride + 0x14, 9);  // no node 9
    m.put64(nodes + 5 * 8, 0x40000);
    m.put32(0x40000 + L::kNodeKey, 0xB010006D);
    m.put64(0x40000 + L::kNodeActor, 0x50000);
    m.put64(nodes + 6 * 8, 0x41000);
    m.put32(0x41000 + L::kNodeKey, kHost);
    m.put64(0x41000 + L::kNodeActor, 0x51000);
    m.put64(nodes + 7 * 8, 0x42000);
    m.put32(0x42000 + L::kNodeKey, 0xB0100099);  // a stale node: not the key the bucket names
    m.put64(0x42000 + L::kNodeActor, 0x52000);
    std::vector<std::pair<uint32_t, uint64_t>> seen;
    gsr::WalkStats s = gsr::walk_actor_table(m, table, gsr::WalkLimits{}, [&](uint32_t k, uint64_t act) {
        seen.emplace_back(k, act);
        return true;
    });
    CHECK(s.ok && !s.truncated && s.buckets == 4 && s.entries == 4 && s.actors == 2);
    CHECK(seen.size() == 2 && seen[0] == std::make_pair(0xB010006Du, uint64_t{0x50000}) &&
          seen[1] == std::make_pair(kHost, uint64_t{0x51000}));
    // Bounds: the entries cap truncates; too many buckets or a missing table: nothing walked.
    gsr::WalkLimits lim;
    lim.max_entries = 1;
    seen.clear();
    s = gsr::walk_actor_table(m, table, lim, [&](uint32_t k, uint64_t act) {
        seen.emplace_back(k, act);
        return true;
    });
    CHECK(s.ok && s.truncated && s.entries == 1 && seen.size() == 1);
    lim = gsr::WalkLimits{};
    lim.max_buckets = 3;
    CHECK(!gsr::walk_actor_table(m, table, lim, [](uint32_t, uint64_t) { return true; }).ok);
    CHECK(!gsr::walk_actor_table(m, 0, gsr::WalkLimits{}, [](uint32_t, uint64_t) { return true; }).ok);
    CHECK(!gsr::walk_actor_table(m, 0x99000, gsr::WalkLimits{}, [](uint32_t, uint64_t) { return true; }).ok);
    // The callback stops the walk.
    int calls = 0;
    s = gsr::walk_actor_table(m, table, gsr::WalkLimits{}, [&](uint32_t, uint64_t) { return ++calls < 1; });
    CHECK(calls == 1 && s.actors == 1);
    // A bucket count over 31 is clamped to the bucket's capacity.
    m.put32(buckets + 0 * L::kBucketStride, 1000);
    s = gsr::walk_actor_table(m, table, gsr::WalkLimits{}, [](uint32_t, uint64_t) { return true; });
    CHECK(s.ok && s.entries == L::kBucketMax + 4 && s.actors == 2);
    static_assert(gsr::kBindCap == 64);
}

// p8-impl-horarios-x.md §10-§11: duplicate / non-creating 2335s, the routed X registry and the teardown guard.
void test_guest_sched_kinds_and_teardown() {
    using V = gsr::Verdict;
    using W = gsr::SchedWhere;
    CHECK(gsr::sched_creates(1) && gsr::sched_creates(4));
    CHECK(!gsr::sched_creates(0) && !gsr::sched_creates(2) && !gsr::sched_creates(3) && !gsr::sched_creates(5));

    // A create: the copy came first (X has the key) -> the native one is refused; probe decides the same.
    CHECK(gsr::sched_2335_create(V::Route, true) == V::DuplicateCreate);
    CHECK(gsr::sched_2335_create(V::WouldRoute, true) == V::DuplicateCreate);
    CHECK(gsr::sched_2335_create(V::Route, false) == V::Route);
    CHECK(gsr::sched_2335_create(V::NoX, true) == V::NoX);  // not going to X: native
    CHECK(gsr::sched_2335_create(V::OwnCharacter, true) == V::OwnCharacter);
    // Its ledger entry stays X (sched_created_where is taken before the duplicate check).
    CHECK(gsr::sched_created_where(V::Route) == W::X);

    // A non-creating 2335: a follow-up of its key, refused when the manager it reaches lacks the key.
    V fv = gsr::sched_followup(V::Route, W::X, true, false);
    CHECK(fv == V::Route);
    CHECK(gsr::sched_2335_other(fv, gsr::sched_judged(fv), true) == V::Route);
    CHECK(gsr::sched_2335_other(fv, gsr::sched_judged(fv), false) == V::NoStageForKey);
    fv = gsr::sched_followup(V::Route, W::M, false, false);  // created in M: stays in M
    CHECK(fv == V::CreatedInM && gsr::sched_judged(fv));
    CHECK(gsr::sched_2335_other(fv, true, true) == V::CreatedInM);
    CHECK(gsr::sched_2335_other(fv, true, false) == V::NoStageForKey);
    fv = gsr::sched_followup(V::Route, W::Unknown, false, false);  // unknown key, not in X
    CHECK(fv == V::NotInX);
    // This client's own / an NPC's: never judged, never refused.
    CHECK(!gsr::sched_judged(V::OwnCharacter) && !gsr::sched_judged(V::NotCharacter) && !gsr::sched_judged(V::Off));
    CHECK(gsr::sched_2335_other(V::OwnCharacter, gsr::sched_judged(V::OwnCharacter), false) == V::OwnCharacter);
    CHECK(gsr::sched_refused(V::DuplicateCreate) && gsr::sched_refused(V::NoStageForKey));
    CHECK(!gsr::sched_refused(V::Route) && !gsr::sched_refused(V::CreatedInM));
    CHECK(std::string(gsr::verdict_name(V::DuplicateCreate)).find("DUPLICATE") != std::string::npos);

    // The registry of routed X managers.
    gsr::XRegistry<2> r;
    CHECK(r.owner_of(0x3000) == 0);
    r.note(0, kHost);
    r.note(0x3000, 0);
    CHECK(r.owner_of(0x3000) == 0 && r.x_at(0) == 0);
    r.note(0x3000, kHost);
    r.note(0x3000, kHost);  // again: one slot
    CHECK(r.owner_of(0x3000) == kHost && r.x_at(1) == 0);
    r.note(0x4000, kHost);  // the host character came back with a new X
    CHECK(r.owner_of(0x4000) == kHost && r.owner_of(0x3000) == kHost);
    r.note(0x5000, 0xA0100002);  // full: the oldest slot goes
    CHECK(r.owner_of(0x5000) == 0xA0100002 && r.owner_of(0x3000) == 0 && r.owner_of(0x4000) == kHost);

    // The teardown guard.
    using T = gsr::Teardown;
    CHECK(gsr::teardown_guard(0x3000, 0x3000, 0, 0) == T::NotOurs);
    CHECK(gsr::teardown_guard(0x3000, 0x3000, kHost, 0x3000) == T::Live);
    CHECK(gsr::teardown_guard(0x3000, 0x3000, kHost, 0) == T::Redirect);       // the character is gone
    CHECK(gsr::teardown_guard(0x3000, 0x3000, kHost, 0x4000) == T::Redirect);  // it came back with another X
    CHECK(gsr::teardown_guard(0x3000, 0x3008, kHost, 0) == T::Mismatch);       // RCX is not the stage's +0x28
    CHECK(gsr::teardown_guard(0, 0, kHost, 0) == T::Mismatch);
}

int main() {
    test_mode_and_filter();
    test_parsers();
    test_ledger();
    test_trip_snapshot_and_live();
    test_milestone_hold();
    test_gate_timeout_refusal_and_bounds();
    test_filter();
    test_guest_route();
    test_review_fixes();
    test_sched_parsers();
    test_sched_ledger();
    test_sched_snapshot_and_live();
    test_sched_native_first();
    test_sched_pending_refusal_bounds();
    test_guest_sched_route();
    test_guest_sched_bind();
    test_guest_sched_kinds_and_teardown();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_seq_copy: all OK\n");
    return 0;
}
