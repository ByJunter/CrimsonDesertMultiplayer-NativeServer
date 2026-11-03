// P8 session 25, grabs by the other player (docs/research/s25/finisher-sigilo.md §4): the pure logic of catch_seq
// (src/mod/session2/catch_seq_logic.h): the mode, the decision at ClientCatch_ApplyResult's entry (S25's real
// numbers), the counters and the self-consistency of the verified build-2976 bytes.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "mod/session2/catch_seq_logic.h"

using namespace cdmp::session2;
using csq::Decision;
using csq::Inputs;
using csq::Mode;
using csq::Verdict;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

// Everything readable; c8 = 1 as in all 20 grabs of S25.
Inputs all_read(uint8_t seq, uint8_t b, uint8_t grabber_ctl, uint8_t victim_ctl) {
    Inputs in;
    in.c8_ok = in.seq_ok = in.grabber_ok = in.b_ok = in.victim_ok = true;
    in.c8 = 1;
    in.seq = seq;
    in.b = b;
    in.grabber_ctl = grabber_ctl;
    in.victim_ctl = victim_ctl;
    return in;
}

void test_modes() {
    CHECK(csq::parse_mode("0").mode == Mode::Off);
    CHECK(csq::parse_mode("probe").mode == Mode::Probe);
    CHECK(csq::parse_mode("1").mode == Mode::On);
    CHECK(csq::parse_mode(" PROBE ; step 1").mode == Mode::Probe);
    CHECK(!csq::parse_mode("selftest").known);  // not a value of this key
    CHECK(csq::parse_mode("selftest").mode == Mode::Off);
    CHECK(std::strcmp(csq::mode_name(Mode::Probe), "probe") == 0);
}

// The cases S25 had (finisher-sigilo.md §1.3, §2.2).
void test_s25_cases() {
    // The host grabs its own enemy (H 837.714): seq 0x85, the host's character holds 0x85, it controls both.
    const Inputs host = all_read(0x85, 0x85, 2, 1);
    for (Mode m : {Mode::Probe, Mode::On}) {
        const Decision d = csq::decide(m, host);
        CHECK(d.verdict == Verdict::LocalMatch);
        CHECK(!d.write);
    }
    // The guest grabs the host's enemy, seen on the host (H 1978.405): seq 0x5D, the guest's character is remote
    // here (+0xC8 = 0) and holds 0 (relayed Acks carry 0), the victim is the host client's.
    const Inputs guest = all_read(0x5D, 0x00, 0, 1);
    Decision d = csq::decide(Mode::Probe, guest);
    CHECK(d.verdict == Verdict::RemoteFix);
    CHECK(!d.write);  // probe: nothing changes
    d = csq::decide(Mode::On, guest);
    CHECK(d.verdict == Verdict::RemoteFix);
    CHECK(d.write);
    CHECK(d.value == 0x00);  // the grabber's byte, not the packet's
    // The same 2156 seen on the guest's own client: the grabber is local there and matches.
    CHECK(csq::decide(Mode::On, all_read(0x5D, 0x5D, 2, 0)).verdict == Verdict::LocalMatch);
    // The host's grab seen on the guest (G1 666.341): remote grabber, mismatch, the victim is not the guest's.
    d = csq::decide(Mode::On, all_read(0x85, 0x00, 0, 0));
    CHECK(d.verdict == Verdict::RemoteVictimNotOurs);
    CHECK(!d.write);
}

void test_pass_through() {
    // Arguments unreadable: never written.
    Inputs in = all_read(0x5D, 0x00, 0, 1);
    in.seq_ok = false;
    CHECK(csq::decide(Mode::On, in).verdict == Verdict::Unreadable);
    CHECK(!csq::decide(Mode::On, in).write);
    in = all_read(0x5D, 0x00, 0, 1);
    in.c8_ok = false;
    CHECK(csq::decide(Mode::On, in).verdict == Verdict::Unreadable);
    // c8 = 0: the native code does not compare; nothing to fix.
    in = all_read(0x5D, 0x00, 0, 1);
    in.c8 = 0;
    CHECK(csq::decide(Mode::On, in).verdict == Verdict::NoCheck);
    CHECK(!csq::decide(Mode::On, in).write);
    // The grabber or its byte unreadable.
    in = all_read(0x5D, 0x00, 0, 1);
    in.grabber_ok = false;
    CHECK(csq::decide(Mode::On, in).verdict == Verdict::Unreadable);
    in = all_read(0x5D, 0x00, 0, 1);
    in.b_ok = false;
    CHECK(csq::decide(Mode::On, in).verdict == Verdict::Unreadable);
    CHECK(!csq::decide(Mode::On, in).write);
    // The victim not found here: passed through.
    in = all_read(0x5D, 0x00, 0, 1);
    in.victim_ok = false;
    CHECK(csq::decide(Mode::On, in).verdict == Verdict::RemoteVictimUnknown);
    CHECK(!csq::decide(Mode::On, in).write);
    // A local grabber that mismatches is the game's own business: never written.
    CHECK(csq::decide(Mode::On, all_read(0x85, 0x84, 2, 1)).verdict == Verdict::LocalMismatch);
    CHECK(!csq::decide(Mode::On, all_read(0x85, 0x84, 2, 1)).write);
    // A remote grabber that already matches.
    CHECK(csq::decide(Mode::On, all_read(0x41, 0x41, 0, 1)).verdict == Verdict::RemoteMatch);
    CHECK(!csq::decide(Mode::On, all_read(0x41, 0x41, 0, 1)).write);
    // Off never writes (the hook is not installed then anyway).
    CHECK(!csq::decide(Mode::Off, all_read(0x5D, 0x00, 0, 1)).write);
    // The victim's control byte decides only for a remote grabber; any non-zero value is "controlled here".
    CHECK(csq::decide(Mode::On, all_read(0x5D, 0x07, 0, 3)).write);
    CHECK(csq::decide(Mode::On, all_read(0x5D, 0x07, 0, 3)).value == 0x07);
}

void test_tally() {
    csq::Tally t;
    const auto note = [&](const Inputs& in, Mode m, bool wrote, bool caller) {
        const Decision d = csq::decide(m, in);
        t.note(in, d, wrote, caller);
    };
    note(all_read(0x85, 0x85, 2, 1), Mode::On, false, true);   // local match
    note(all_read(0x85, 0x84, 2, 1), Mode::On, false, true);   // local mismatch
    note(all_read(0x5D, 0x00, 0, 1), Mode::On, true, true);    // fix, written
    note(all_read(0x5E, 0x00, 0, 1), Mode::On, false, true);   // fix, write failed
    note(all_read(0x5F, 0x00, 0, 1), Mode::Probe, false, true);  // fix, probe (no write)
    note(all_read(0x60, 0x00, 0, 0), Mode::On, false, true);   // victim not ours
    Inputs unknown = all_read(0x61, 0x00, 0, 1);
    unknown.victim_ok = false;
    note(unknown, Mode::On, false, true);                      // victim not found
    note(all_read(0x62, 0x62, 0, 1), Mode::On, false, false);  // remote match, unexpected caller
    Inputs nocheck = all_read(0x63, 0x00, 0, 1);
    nocheck.c8 = 0;
    note(nocheck, Mode::On, false, true);
    Inputs a = all_read(0x64, 0x00, 0, 1);
    a.seq_ok = false;
    note(a, Mode::On, false, true);
    Inputs g = all_read(0x65, 0x00, 0, 1);
    g.grabber_ok = false;
    note(g, Mode::On, false, true);
    Inputs b = all_read(0x66, 0x00, 0, 1);
    b.b_ok = false;
    note(b, Mode::On, false, true);
    const auto n = [](const std::atomic<uint64_t>& c) { return c.load(); };
    CHECK(n(t.calls) == 12);
    CHECK(n(t.local) == 2);
    CHECK(n(t.remote) == 6);
    CHECK(n(t.match) == 2);
    CHECK(n(t.mismatch) == 6);
    CHECK(n(t.fix) == 3);
    CHECK(n(t.rewritten) == 1);
    CHECK(n(t.write_failed) == 1);
    CHECK(n(t.victim_not_ours) == 1);
    CHECK(n(t.victim_unknown) == 1);
    CHECK(n(t.no_check) == 1);
    CHECK(n(t.unreadable) == 3);
    CHECK(n(t.unread_args) == 1);
    CHECK(n(t.unread_grabber) == 1);
    CHECK(n(t.unread_byte) == 1);
    CHECK(n(t.unexpected_caller) == 1);
}

void test_constants() {
    // The head: whole instructions, ascending, the last one ends the verified bytes.
    uint32_t prev = 0;
    for (uint32_t b : csq::kApplyBoundaries) {
        CHECK(b > prev);
        prev = b;
    }
    CHECK(prev == sizeof csq::kApplyHead);
    CHECK(csq::kApplyHead[0] == 0x48 && csq::kApplyHead[1] == 0x89 && csq::kApplyHead[2] == 0x5C);
    // A 5-byte jmp and a 14-byte one both end on a boundary (5 and 15).
    bool has5 = false, has15 = false;
    for (uint32_t b : csq::kApplyBoundaries) {
        has5 = has5 || b == 5;
        has15 = has15 || b == 15;
    }
    CHECK(has5 && has15);
    // The only caller calls 0x935DD0 and returns to 0xB58948.
    CHECK(csq::call_targets(csq::kApplyCall, csq::kApplyCallBytes, csq::kApply));
    static_assert(csq::kApplyReturn == csq::kApplyCall + 5);
    // The comparison site: its size, the cmp's place and displacement, and the hops this module reads.
    static_assert(sizeof csq::kCheckSiteBytes == 0x935E9A - 0x935E43);
    const uint8_t* s = csq::kCheckSiteBytes;
    const uint8_t cmp[] = {0x38, 0x91, 0x93, 0x00, 0x00, 0x00};
    CHECK(std::memcmp(s + csq::kCheckCmpAt, cmp, sizeof cmp) == 0);
    CHECK(s[csq::kCheckCmpAt + 2] == csq::kAction_Seq);
    const size_t hops = 0x935E7E - csq::kCheckSite;  // mov rax,[r15+8] ; mov rcx,[rax+0x68] ; ...[rcx+0x40] ; [rax+0x48]
    CHECK(s[hops + 3] == csq::kComp_Owner);
    CHECK(s[hops + 7] == csq::kActor_Comps);
    CHECK(s[hops + 11] == csq::kComps_Control);
    CHECK(s[hops + 15] == csq::kControl_Action);
    // The Actor_Lock call inside the site lands on 0x393650.
    uint8_t lock_call[5];
    std::memcpy(lock_call, s + (0x935E6E - csq::kCheckSite), 5);
    CHECK(csq::call_targets(0x935E6E, lock_call, 0x393650));
    // [rbp+0xB7] / [rbp+0xCF] with rbp = entry rsp - 0x2F (5 pushes + lea rbp,[rsp-7]): arguments 17 and 20.
    CHECK(s[3] == 0xB7);
    CHECK((s[3] - 0x2F) / 8 == csq::kArgC8);
    const size_t seq_load = 0x935E74 - csq::kCheckSite;
    CHECK(s[seq_load + 3] == 0xCF);
    CHECK((s[seq_load + 3] - 0x2F) / 8 == csq::kArgSeq);
}

}  // namespace

int main() {
    test_modes();
    test_s25_cases();
    test_pass_through();
    test_tally();
    test_constants();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_catch_seq: all passed\n");
    return 0;
}
