// P8 strategy B, step 2: the owner fan-out probe (src/mod/session2/owner_fanout_logic.h;
// docs/research/pendientes/p8-impl-owner-fanout.md).
//
// SCOPE: offline logic only. The mode (1 reserved), the generated writer table (sorted, inside .rsrc,
// the writers of p8-arquitectura-observador.md §3.4 with their return addresses), the families, the
// observer-trip rule, the verdict of one ToTarget::vf1 call, the lock-free counters (also from several
// threads), the top list and the comparison. They do NOT test the hook, its byte checks against the
// running game, the ABI nor the game's behaviour (that is the one-PC test of the doc, §5).

#include <cstdio>
#include <cstring>
#include <set>
#include <thread>
#include <vector>

#include "mod/session2/owner_fanout_logic.h"

using namespace cdmp::session2;
using ofo::Family;
using ofo::Verdict;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

// Keys of session 8: the host user 0x90100000, user 2 0x9010031E.
constexpr uint32_t kHostUser = 0x90100000, kUser2 = 0x9010031E;
// Return RVAs of the writers' finder->vf1 call (capstone over the PE, owner_fanout_writers.h).
constexpr uint32_t kRet2661 = 0x2A4DF57, kRet2810 = 0x2A4E51E, kRet2322 = 0x2999DD6, kRet2195 = 0x2A52C1E,
                   kRet2126 = 0x2A53F09, kRet2264 = 0x2A56C1E, kRet2203 = 0x2A42E52;

const ofo::WriterRow* row_of(uint32_t ret) {
    const int i = ofo::writer_index(ret);
    return i >= 0 ? &ofo::kWriters[static_cast<size_t>(i)] : nullptr;
}

// ---------------------------------------------------------------- the option

void test_parse_mode() {
    struct Case {
        const char* raw;
        ofo::Mode mode;
        bool reserved, unknown;
    };
    const Case cases[] = {
        {"", ofo::Mode::Off, false, false},          {"0", ofo::Mode::Off, false, false},
        {" off ", ofo::Mode::Off, false, false},     {"probe", ofo::Mode::Probe, false, false},
        {"PROBE\r", ofo::Mode::Probe, false, false}, {"probe ; step 2", ofo::Mode::Probe, false, false},
        {"1", ofo::Mode::Off, true, false},          {"on", ofo::Mode::Off, true, false},
        {"1 # step 3", ofo::Mode::Off, true, false}, {"2", ofo::Mode::Off, false, true},
        {"probes", ofo::Mode::Off, false, true},     {"send-all", ofo::Mode::Off, false, true},
    };
    for (const Case& c : cases) {
        const ofo::ParsedMode p = ofo::parse_mode(c.raw);
        const bool ok = p.mode == c.mode && p.reserved_send == c.reserved && p.unknown == c.unknown;
        CHECK(ok);
        if (!ok) std::printf("  case '%s'\n", c.raw);
    }
    CHECK(std::strcmp(ofo::mode_name(ofo::Mode::Probe), "probe") == 0);
    CHECK(std::strcmp(ofo::mode_name(ofo::Mode::Off), "0") == 0);
}

// ---------------------------------------------------------------- the table

void test_table() {
    static_assert(ofo::kWriterCount >= 300);
    static_assert(ofo::writers_sorted());
    std::set<uint32_t> rets;
    for (const ofo::WriterRow& w : ofo::kWriters) {
        // Every writer and every return address in .rsrc [0x1000, 0x51EB000); the return a little after the writer.
        CHECK(w.writer >= 0x1000 && w.writer < 0x51EB000);
        CHECK(w.ret > w.writer && w.ret - w.writer < 0x200);
        CHECK(w.opcode >= 2000 && w.opcode < 3200);
        CHECK(w.name && std::strlen(w.name) > 3);
        rets.insert(w.ret);
    }
    CHECK(rets.size() == ofo::kWriterCount);
    // Which finders reach each writer. ToControlUser (the same vf1) reaches 2339, 2979, 3018, 2954, 2652, 3097
    // and 2184 only: none is listed, doubtful or excluded in §3.4 (review of 9e8dada, 3).
    std::set<uint16_t> by_control;
    for (const ofo::WriterRow& w : ofo::kWriters) {
        CHECK(w.finders >= 1 && w.finders <= 3);
        if (w.finders & ofo::kByToControlUser) {
            by_control.insert(w.opcode);
            CHECK(ofo::family_of(w.opcode) == Family::Other);
        }
    }
    CHECK(by_control == (std::set<uint16_t>{2339, 2979, 3018, 2954, 2652, 3097, 2184}));
    CHECK(row_of(kRet2661) && row_of(kRet2661)->finders == ofo::kByToTarget);
    // The writers of §3.4, by their return address.
    struct Known {
        uint32_t ret, writer;
        uint16_t op;
    };
    const Known known[] = {{kRet2661, 0x2A4DEF0, 2661}, {kRet2810, 0x2A4E4C0, 2810}, {kRet2322, 0x2999D80, 2322},
                           {kRet2195, 0x2A52BC0, 2195}, {kRet2126, 0x2A53EB0, 2126}, {kRet2264, 0x2A56BC0, 2264},
                           {kRet2203, 0x2A42DF0, 2203}, {0x2A530BE, 0x2A53060, 2130}, {0x2A54D37, 0x2A54CD0, 2958},
                           {0x2A4EE94, 0x2A4EE30, 2797}, {0x2A55F04, 0x2A55EA0, 2224}, {0x2A4242E, 0x2A423D0, 2165}};
    for (const Known& k : known) {
        const ofo::WriterRow* r = row_of(k.ret);
        CHECK(r && r->writer == k.writer && r->opcode == k.op);
        if (!r || r->writer != k.writer || r->opcode != k.op) std::printf("  ret 0x%X\n", k.ret);
    }
    // Not a return address: the writer's entry, one byte off, zero, past the end.
    CHECK(ofo::writer_index(0x2A4DEF0) < 0);
    CHECK(ofo::writer_index(kRet2661 + 1) < 0 && ofo::writer_index(kRet2661 - 1) < 0);
    CHECK(ofo::writer_index(0) < 0 && ofo::writer_index(0xFFFFFFFF) < 0);
    CHECK(ofo::writer_index(ofo::kWriters[0].ret) == 0);
    CHECK(ofo::writer_index(ofo::kWriters[ofo::kWriterCount - 1].ret) == static_cast<int>(ofo::kWriterCount - 1));
    // Every listed and doubtful opcode of §3.4 has a writer in the table.
    const uint16_t wanted[] = {2661, 2810, 2322, 2797, 2224, 2226, 2140, 2675, 2222, 2918, 2416, 2126, 2958,
                               2909, 2499, 2127, 2839, 2195, 2130, 2264, 2165, 2724};
    for (const uint16_t op : wanted) {
        bool found = false;
        for (const ofo::WriterRow& w : ofo::kWriters) found = found || w.opcode == op;
        CHECK(found);
        if (!found) std::printf("  opcode %u has no writer\n", op);
    }
}

void test_families() {
    CHECK(ofo::family_of(2661) == Family::Sequencer && ofo::family_of(2416) == Family::Sequencer);
    CHECK(ofo::family_of(2126) == Family::Schedule && ofo::family_of(2839) == Family::Schedule);
    CHECK(ofo::family_of(2195) == Family::Faction && ofo::family_of(2130) == Family::Faction);
    CHECK(ofo::family_of(2264) == Family::Doubtful && ofo::family_of(2724) == Family::Doubtful);
    CHECK(ofo::family_of(2203) == Family::Excluded && ofo::family_of(2218) == Family::Excluded &&
          ofo::family_of(2297) == Family::Excluded);
    CHECK(ofo::family_of(2665) == Family::Other && ofo::family_of(2981) == Family::Other);  // ToField / ToNear
    CHECK(ofo::listed(Family::Sequencer) && ofo::listed(Family::Schedule) && ofo::listed(Family::Faction));
    CHECK(!ofo::listed(Family::Doubtful) && !ofo::listed(Family::Excluded) && !ofo::listed(Family::Other));
    CHECK(ofo::copied_by_a(2661) && ofo::copied_by_a(2810) && ofo::copied_by_a(2322));
    // observer_sched_copy (p8-impl-horarios-snapshot.md): the schedules' live copies.
    CHECK(ofo::copied_by_a(2126) && ofo::copied_by_a(2958) && ofo::copied_by_a(2909));
    CHECK(!ofo::copied_by_a(2981) && !ofo::copied_by_a(2335) && !ofo::copied_by_a(2499));
    const auto ops = ofo::compared_opcodes();
    CHECK(ops.size() >= 22 && ops[0] == 2661 && ops[1] == 2810 && ops[2] == 2322);
    CHECK(ops[3] == 2126 && ops[4] == 2958 && ops[5] == 2909);
    CHECK(std::set<uint16_t>(ops.begin(), ops.end()).size() == ops.size());
    for (size_t i = 7; i < ops.size(); ++i) CHECK(ops[i - 1] < ops[i]);
}

// ---------------------------------------------------------------- the trip and the verdict

void test_trip_rule() {
    CHECK(ofo::observer_trip(kHostUser, kUser2));    // the host's field
    CHECK(!ofo::observer_trip(kUser2, kUser2));      // its own field
    CHECK(!ofo::observer_trip(kHostUser, 0));        // user 2 unknown yet
    CHECK(!ofo::observer_trip(0, kUser2));           // owner unknown (another layout)
    CHECK(!ofo::observer_trip(0xA0100001, kUser2));  // not a user key
}

ofo::CallIn host_call() {
    ofo::CallIn in;
    in.observing = true;
    in.finder_read = true;
    in.to_target_vt = 0x145B25438;
    in.vtable = 0x145B25438;
    in.s1 = 0x1000;
    in.target = 0x1000;
    in.s2_live = 0x2000;
    in.trip_s2 = 0x2000;
    return in;
}

void test_judge() {
    CHECK(ofo::judge(host_call()) == Verdict::HostTarget);
    ofo::CallIn in = host_call();
    in.observing = false;
    CHECK(ofo::judge(in) == Verdict::NotObserving);
    in = host_call();
    in.finder_read = false;
    CHECK(ofo::judge(in) == Verdict::Unreadable);
    in = host_call();
    in.vtable = 0x145B25400;  // neither ToTarget's nor ToControlUser's
    CHECK(ofo::judge(in) == Verdict::OtherFinder);
    // ToControlUser (same vf1): the same target checks, its own bucket.
    in = host_call();
    in.to_control_vt = 0x145B252F8;
    in.vtable = 0x145B252F8;
    CHECK(ofo::judge(in) == Verdict::ControlUserHost);
    in.target = 0x2000;
    CHECK(ofo::judge(in) == Verdict::ToUser2);
    in.target = 0x4000;
    CHECK(ofo::judge(in) == Verdict::OtherTarget);
    in.target = 0x1000;
    in.s2_in_array = ofo::kArrayTooLong;
    CHECK(ofo::judge(in) == Verdict::ArrayTooLong);
    in = host_call();
    in.vtable = 0x145B252F8;  // ToControlUser's vtable unknown (0): not told apart
    CHECK(ofo::judge(in) == Verdict::OtherFinder);
    in = host_call();
    in.s2_in_array = ofo::kArrayTooLong;
    CHECK(ofo::judge(in) == Verdict::ArrayTooLong);
    in = host_call();
    in.target = 0;
    CHECK(ofo::judge(in) == Verdict::EmptyFinder);
    in = host_call();
    in.s2_live = 0;
    CHECK(ofo::judge(in) == Verdict::NoUser2);
    in = host_call();
    in.logout = true;
    CHECK(ofo::judge(in) == Verdict::NoUser2);
    in = host_call();
    in.s2_live = 0x3000;  // a new epoch's session 2: the trip belongs to the old one
    CHECK(ofo::judge(in) == Verdict::StaleTrip);
    in = host_call();
    in.target = 0x2000;  // user 2's own Ack: it gets it natively
    CHECK(ofo::judge(in) == Verdict::ToUser2);
    in = host_call();
    in.target = 0x4000;
    CHECK(ofo::judge(in) == Verdict::OtherTarget);
    in = host_call();
    in.s1 = 0;
    CHECK(ofo::judge(in) == Verdict::OtherTarget);
    in = host_call();
    in.s2_in_array = 1;
    CHECK(ofo::judge(in) == Verdict::AlreadyThere);
    in = host_call();
    in.s2_in_array = -1;
    CHECK(ofo::judge(in) == Verdict::Unreadable);
    for (size_t v = 0; v < ofo::kVerdicts; ++v) CHECK(std::strcmp(ofo::verdict_name(static_cast<Verdict>(v)), "?") != 0);
}

void test_scan_array() {
    constexpr uintptr_t s2 = 0x2000;
    std::vector<uintptr_t> arr = {0x1000};
    auto reader = [&arr](uint32_t i, uintptr_t& out) {
        if (i >= arr.size()) return false;
        out = arr[i];
        return true;
    };
    CHECK(ofo::scan_array(false, false, 1, s2, reader) == ofo::kArrayUnreadable);
    CHECK(ofo::scan_array(true, true, 0, s2, reader) == ofo::kNotInArray);       // empty array, no buffer
    CHECK(ofo::scan_array(true, true, 1, s2, reader) == ofo::kArrayUnreadable);  // a count with no buffer
    CHECK(ofo::scan_array(true, false, 1, s2, reader) == ofo::kNotInArray);
    arr.push_back(s2);
    CHECK(ofo::scan_array(true, false, 2, s2, reader) == ofo::kInArray);
    CHECK(ofo::scan_array(true, false, 3, s2, reader) == ofo::kInArray);         // found before the bad entry
    arr = {0x1000};
    CHECK(ofo::scan_array(true, false, 2, s2, reader) == ofo::kArrayUnreadable);  // entry 1 unreadable
    // Longer than the scan: unknown, never "not there"; found inside the first 64: there.
    arr.assign(ofo::kMaxArrayScan + 10, 0x1000);
    CHECK(ofo::scan_array(true, false, ofo::kMaxArrayScan + 10, s2, reader) == ofo::kArrayTooLong);
    CHECK(ofo::scan_array(true, false, ofo::kMaxArrayScan, s2, reader) == ofo::kNotInArray);
    arr[ofo::kMaxArrayScan - 1] = s2;
    CHECK(ofo::scan_array(true, false, ofo::kMaxArrayScan + 10, s2, reader) == ofo::kInArray);
    arr[ofo::kMaxArrayScan - 1] = 0x1000;
    arr[ofo::kMaxArrayScan] = s2;  // beyond the scan
    CHECK(ofo::scan_array(true, false, ofo::kMaxArrayScan + 10, s2, reader) == ofo::kArrayTooLong);
}

// ---------------------------------------------------------------- the hook's own patch

std::vector<uint8_t> ff_bytes(uint64_t detour, size_t len) {
    std::vector<uint8_t> b = {0xFF, 0x25, 0, 0, 0, 0};
    for (int i = 0; i < 8; ++i) b.push_back(static_cast<uint8_t>(detour >> (8 * i)));
    b.resize(len, 0x90);
    return b;
}

void test_patch_is_ours() {
    constexpr uint64_t at = 0x142A757F0, detour = 0x7FF812345678, tramp = 0x142A00000, tramp_size = 0x40;
    // The E9 form: jmp rel32 to the epilogue's `FF 25` + detour (trampoline + 0x20 here).
    const uint64_t stub_at = tramp + 0x20;
    const int32_t rel = static_cast<int32_t>(stub_at - (at + 5));
    std::vector<uint8_t> e9 = {0xE9, static_cast<uint8_t>(rel), static_cast<uint8_t>(rel >> 8),
                               static_cast<uint8_t>(rel >> 16), static_cast<uint8_t>(rel >> 24)};
    CHECK(ofo::e9_destination(e9, at) == stub_at);
    const std::vector<uint8_t> stub = ff_bytes(detour, 14);
    CHECK(ofo::patch_is_ours(e9, at, detour, stub, tramp, tramp + tramp_size));
    CHECK(!ofo::patch_is_ours(e9, at, detour, {}, tramp, tramp + tramp_size));                  // stub not read
    CHECK(!ofo::patch_is_ours(e9, at, detour + 1, stub, tramp, tramp + tramp_size));            // another detour
    CHECK(!ofo::patch_is_ours(e9, at, detour, stub, tramp + 0x100, tramp + 0x200));             // outside the trampoline
    CHECK(!ofo::patch_is_ours(e9, at, detour, ff_bytes(detour, 14), tramp, stub_at + 13));      // stub crosses the end
    std::vector<uint8_t> e9_long = e9;
    e9_long.resize(15, 0x90);
    CHECK(ofo::patch_is_ours(e9_long, at, detour, stub, tramp, tramp + tramp_size));            // padded with nops
    e9_long[10] = 0xCC;
    CHECK(!ofo::patch_is_ours(e9_long, at, detour, stub, tramp, tramp + tramp_size));           // not a nop
    // The FF form: FF 25 00000000 + detour, then one nop (15 relocated bytes).
    CHECK(ofo::patch_is_ours(ff_bytes(detour, 15), at, detour, {}, tramp, tramp + tramp_size));
    CHECK(!ofo::patch_is_ours(ff_bytes(detour + 8, 15), at, detour, {}, tramp, tramp + tramp_size));
    std::vector<uint8_t> ff_bad = ff_bytes(detour, 15);
    ff_bad[2] = 1;  // rip+1: not ours
    CHECK(!ofo::patch_is_ours(ff_bad, at, detour, {}, tramp, tramp + tramp_size));
    // The original bytes (not enabled yet) and a foreign jump: not ours, the live bytes go to the checker.
    const std::vector<uint8_t> orig = {0x48, 0x89, 0x5C, 0x24, 0x08};
    CHECK(ofo::e9_destination(orig, at) == 0);
    CHECK(!ofo::patch_is_ours(orig, at, detour, {}, tramp, tramp + tramp_size));
    std::vector<uint8_t> foreign = {0xE9, 0x00, 0x10, 0x00, 0x00};  // somewhere else
    CHECK(!ofo::patch_is_ours(foreign, at, detour, stub, tramp, tramp + tramp_size));
    CHECK(!ofo::patch_is_ours(std::vector<uint8_t>{0xE9, 0, 0}, at, detour, stub, tramp, tramp + tramp_size));
}

// ---------------------------------------------------------------- counters, top, comparison

void test_counters() {
    static ofo::Counters c;  // large: not on the stack
    for (int i = 0; i < 27; ++i) c.count_host(kRet2661);
    for (int i = 0; i < 22; ++i) c.count_host(kRet2810);
    c.count_host(kRet2322);
    for (int i = 0; i < 23; ++i) c.count_host(kRet2126);
    for (int i = 0; i < 350; ++i) c.count_host(kRet2195);
    for (int i = 0; i < 327; ++i) c.count_host(kRet2264);
    for (int i = 0; i < 5; ++i) c.count_host(kRet2203);
    c.count_host(0x1234567);  // a return the scan missed
    c.count_host(0x1234567);
    c.count_host(0);          // outside the image: overflow
    c.count(Verdict::ToUser2);
    // ToControlUser to the host: its own bucket, never in the ToTarget writers or families.
    constexpr uint32_t kRet3097 = 0x297395E, kRet2339 = 0x2976686;
    for (int i = 0; i < 4; ++i) c.count_control_host(kRet3097);
    c.count_control_host(kRet2339);
    c.count_control_host(0x1234000);
    const ofo::Snapshot s0 = ofo::snapshot(c);
    CHECK(s0.verdicts[static_cast<size_t>(Verdict::ControlUserHost)] == 6);
    CHECK(ofo::count_of_opcode(s0.by_writer_control, 3097) == 4);
    CHECK(ofo::count_of_opcode(s0.by_writer_control, 2339) == 1);
    CHECK(ofo::count_of_opcode(s0.by_writer, 3097) == 0);
    CHECK(c.unknown_control.rows().size() == 1);
    CHECK(s0.verdicts[static_cast<size_t>(Verdict::HostTarget)] == 27 + 22 + 1 + 23 + 350 + 327 + 5 + 3);
    CHECK(s0.verdicts[static_cast<size_t>(Verdict::ToUser2)] == 1);
    CHECK(ofo::count_of_opcode(s0.by_writer, 2661) == 27);
    CHECK(ofo::count_of_opcode(s0.by_writer, 2810) == 22);
    CHECK(ofo::count_of_opcode(s0.by_writer, 2322) == 1);
    CHECK(ofo::count_of_opcode(s0.by_writer, 2981) == 0);
    const auto fam = ofo::family_totals(s0.by_writer);
    CHECK(fam[static_cast<size_t>(Family::Sequencer)] == 50);
    CHECK(fam[static_cast<size_t>(Family::Schedule)] == 23);
    CHECK(fam[static_cast<size_t>(Family::Faction)] == 350);
    CHECK(fam[static_cast<size_t>(Family::Doubtful)] == 327);
    CHECK(fam[static_cast<size_t>(Family::Excluded)] == 5);
    const auto unknown = c.unknown.rows();
    CHECK(unknown.size() == 1 && unknown[0].first == 0x1234567 && unknown[0].second == 2);
    CHECK(c.unknown.overflow() == 1);

    // Top list: by total, then the last window's delta.
    for (int i = 0; i < 10; ++i) c.count_host(kRet2661);
    const ofo::Snapshot s1 = ofo::snapshot(c);
    const auto top = ofo::top_writers(s1.by_writer, s0.by_writer, 3);
    CHECK(top.size() == 3);
    CHECK(ofo::kWriters[top[0].index].opcode == 2195 && top[0].total == 350 && top[0].delta == 0);
    CHECK(ofo::kWriters[top[1].index].opcode == 2264 && top[1].total == 327);
    CHECK(ofo::kWriters[top[2].index].opcode == 2661 && top[2].total == 37 && top[2].delta == 10);
    CHECK(ofo::top_writers(s1.by_writer, {}, 100).size() == 7);  // an empty `before`: delta = total
    CHECK(ofo::top_writers(std::vector<uint32_t>(ofo::kWriterCount, 0), {}, 5).empty());
}

void test_threads() {
    static ofo::Counters c;
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) {
        ts.emplace_back([t] {
            for (int i = 0; i < 10000; ++i) {
                c.calls.fetch_add(1, std::memory_order_relaxed);
                c.count_host(i % 2 ? kRet2661 : kRet2126);
                if (i % 100 == 0) c.count_host(0x7000000u + static_cast<uint32_t>(t));
            }
        });
    }
    for (auto& t : ts) t.join();
    const ofo::Snapshot s = ofo::snapshot(c);
    CHECK(s.calls == 40000);
    CHECK(ofo::count_of_opcode(s.by_writer, 2661) == 20000);
    CHECK(ofo::count_of_opcode(s.by_writer, 2126) == 20000);
    CHECK(c.unknown.rows().size() == 4);
    uint64_t n = 0;
    for (const auto& r : c.unknown.rows()) n += r.second;
    CHECK(n == 400);
}

void test_agree() {
    CHECK(ofo::agree(0, 0, 1) == ofo::Agreement::NoData);
    CHECK(ofo::agree(27, 27, 0) == ofo::Agreement::Match);
    CHECK(ofo::agree(27, 26, 1) == ofo::Agreement::Match);   // one in flight at a trip's edge
    CHECK(ofo::agree(26, 27, 1) == ofo::Agreement::Match);
    CHECK(ofo::agree(29, 27, 1) == ofo::Agreement::HookMore);
    CHECK(ofo::agree(0, 23, 1) == ofo::Agreement::HookLess);  // e.g. an opcode that also has a ToNear site
    CHECK(std::strcmp(ofo::agreement_name(ofo::Agreement::Match), "MATCH") == 0);
}

}  // namespace

int main() {
    test_parse_mode();
    test_table();
    test_families();
    test_trip_rule();
    test_judge();
    test_scan_array();
    test_patch_is_ours();
    test_counters();
    test_threads();
    test_agree();
    if (g_failures) {
        std::printf("optd_owner_fanout: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("optd_owner_fanout: all passed (%zu writers)\n", ofo::kWriterCount);
    return 0;
}
