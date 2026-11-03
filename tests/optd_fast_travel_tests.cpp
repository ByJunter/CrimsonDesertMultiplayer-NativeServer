// P8 session 15: user 2's fast travel (src/mod/session2/fast_travel_logic.h;
// docs/research/pendientes/p8-impl-viaje-rapido.md).
//
// SCOPE: offline logic only. The modes, the 2283 payload of the session 14 capture, F2's gate (where user 2 is, F3
// live or not, user 2's manager readable), F3's decision at vf48 (return address, shared field, probe vs 1), user 2's
// sequencer-manager chain on fake memory (vtable and back pointer), and F1's guest decision. They do NOT test the
// hooks, the byte checks against the running game nor the game's behaviour after the redirect (live, Shadow).

#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "mod/session2/fast_travel_logic.h"
#include "mod/session2/observer_seq_copy_logic.h"
#include "mod/session2/travel_probe_logic.h"

using namespace cdmp::session2;
using ftl::F3Out;
using ftl::Gate;
using ftl::GuestAct;
using ftl::Mode;
using ftl::Where;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

constexpr uint32_t kHostUser = 0x90100000, kUser2 = 0x9010031E;

std::vector<uint8_t> hex(const char* s) {
    std::vector<uint8_t> out;
    unsigned v = 0;
    int n = 0;
    for (const char* p = s; *p; ++p) {
        const char c = *p;
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        if (d < 0) continue;
        v = v * 16 + static_cast<unsigned>(d);
        if (++n == 2) {
            out.push_back(static_cast<uint8_t>(v));
            v = 0;
            n = 0;
        }
    }
    return out;
}

// The guest's 2283 of session 14 (cdmp_packets_20261011-011835.log, t = 649.399): the log shows its first 48 bytes;
// the last 5 (scale z = 1.0f and the flag) are filled in here (the flag's real value is not in the log).
const char* k2283 =
    "00000000 01000000 00000000 BC0926C6 A24D1B44 2C546EC5 00000000 4B13D63D 00000000 FC987E3F 0000803F 0000803F"
    " 0000803F 00";

void test_parse_mode() {
    CHECK(ftl::parse_mode("").mode == Mode::Off && ftl::parse_mode("").known);
    CHECK(ftl::parse_mode("0").mode == Mode::Off);
    CHECK(ftl::parse_mode(" probe ; S15").mode == Mode::Probe);
    CHECK(ftl::parse_mode("PROBE\r").mode == Mode::Probe);
    CHECK(ftl::parse_mode("1").mode == Mode::On && ftl::parse_mode("on").mode == Mode::On);
    CHECK(ftl::parse_mode("2").mode == Mode::Off && !ftl::parse_mode("2").known);
    CHECK(ftl::parse_mode("probeprobe").mode == Mode::Off && !ftl::parse_mode("probeprobe").known);
    CHECK(std::strcmp(ftl::mode_name(Mode::Probe), "probe") == 0);
}

void test_parse_reload() {
    const auto p = hex(k2283);
    CHECK(p.size() == ftl::kReloadPayload);
    const auto r = ftl::parse_reload(p);
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->a == 0);
    CHECK(r->field_info_key == 1);
    CHECK(r->target_actor == 0);
    CHECK(r->pos[0] < -10626.0f && r->pos[0] > -10627.0f);  // (-10626.4; 621.2; -3813.3)
    CHECK(r->pos[1] > 621.0f && r->pos[1] < 621.5f);
    CHECK(r->pos[2] < -3813.0f && r->pos[2] > -3814.0f);
    CHECK(r->rot[1] > 0.10f && r->rot[1] < 0.11f && r->rot[3] > 0.99f);  // yaw ~12 degrees
    CHECK(r->scale[0] == 1.0f && r->scale[2] == 1.0f);
    CHECK(!ftl::parse_reload(std::span<const uint8_t>(p.data(), 52)).has_value());
}

// observer_seq_copy no longer reads a "sequencer id" in the 2283 (its u64 @0 is FieldInfoKey << 32).
// The host's 2661 of Grover's 0x1AB (optd_seq_copy_tests.cpp, session 7 capture): the ledger knows sequencer 0x1AB.
const char* k2661_1AB =
    "650aff710004010010a0010010a0ab01000000000000010009450f0000000000275501aa09450f00ea3327c6e4991b44466c6dc50c3ef13e"
    "0000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d02000020010010a0050000040000200f0c10b0"
    "050000";

void test_seq_copy_ignores_2283() {
    osq::Ledger l;
    l.on_host_packet(hex(k2661_1AB), 1);
    osq::Copier c;
    c.on_user2_move(true, 1, l);
    // Control: a sequencer Req about 0x1AB is still the host's.
    CHECK(c.judge_req(osq::kOpChartEvent, hex("ab01000000000000020000000000000001"), l) ==
          osq::Copier::Req::HostSequencer);
    // A 2283 passes the sequencer filter even when its u64 @0 would read 0x1AB (F2/F3 own it now).
    auto p = hex(k2283);
    CHECK(c.judge_req(osq::kOpReloadStage, p, l) == osq::Copier::Req::Pass);
    p[0] = 0xAB;
    p[1] = 0x01;
    p[4] = 0;
    CHECK(c.judge_req(osq::kOpReloadStage, p, l) == osq::Copier::Req::Pass);
}

void test_where() {
    CHECK(ftl::where_is(false, kHostUser, kHostUser, kUser2) == Where::Unknown);
    CHECK(ftl::where_is(true, 0, kHostUser, kUser2) == Where::Unknown);
    CHECK(ftl::where_is(true, kHostUser, kHostUser, kUser2) == Where::HostField);
    CHECK(ftl::where_is(true, kUser2, kHostUser, kUser2) == Where::OwnField);
    CHECK(ftl::where_is(true, 0x90100777, kHostUser, kUser2) == Where::OtherField);
    // Host key unknown: the owner cannot be called the host's.
    CHECK(ftl::where_is(true, kHostUser, 0, kUser2) == Where::OtherField);
}

void test_gate() {
    ftl::GateIn in;
    in.opcode = 2001;
    CHECK(ftl::decide_gate(in) == Gate::Pass);  // not a 2283
    in.opcode = ftl::kOpReloadStageStartReq;
    // F3 off or probe: dropped in the shared field, unknown counts as shared, a field of another owner too. Probe
    // only checks that F3 installs: it never lets a 2283 through (review 3).
    for (Where w : {Where::HostField, Where::Unknown, Where::OtherField}) {
        in.where = w;
        for (Mode m : {Mode::Off, Mode::Probe}) {
            in.f3 = m;
            in.f3_live = true;
            in.subject_ok = true;
            CHECK(ftl::decide_gate(in) == Gate::Drop);
        }
    }
    // User 2 in its own field: the native code is right (the owner is user 2).
    in.where = Where::OwnField;
    in.f3 = Mode::Off;
    CHECK(ftl::decide_gate(in) == Gate::Pass);
    // F3 = 1 needs it live AND the subject recognising user 2's character now (review 2b); else F2 drops.
    in.where = Where::HostField;
    in.f3 = Mode::On;
    in.f3_live = true;
    in.subject_ok = true;
    CHECK(ftl::decide_gate(in) == Gate::PassF3);
    in.f3_live = false;
    CHECK(ftl::decide_gate(in) == Gate::Drop);
    in.f3_live = true;
    in.subject_ok = false;  // keys not live yet, another character key, or the class / controller do not match
    CHECK(ftl::decide_gate(in) == Gate::Drop);
    in.where = Where::Unknown;
    in.subject_ok = true;
    CHECK(ftl::decide_gate(in) == Gate::PassF3);  // F3 substitutes whatever the field
}

void test_f3() {
    ftl::F3In in;
    in.mode = Mode::On;
    in.subject = true;
    in.opcode = ftl::kOpReloadStageStartReq;
    in.ret_rva = ftl::kReloadVf48Ret;
    in.mgr_ok = true;
    CHECK(ftl::decide_f3(in) == F3Out::Substitute);
    // Review 2a: with 1 the original is never returned there; an unreadable manager gives the block.
    in.mgr_ok = false;
    CHECK(ftl::decide_f3(in) == F3Out::SubstituteBlock);
    in.mgr_ok = true;
    in.mode = Mode::Probe;
    CHECK(ftl::decide_f3(in) == F3Out::Probe);
    in.mode = Mode::Off;
    CHECK(ftl::decide_f3(in) == F3Out::Off);
    in.mode = Mode::On;
    // Another vf48 caller (e.g. a scene Req or the 2322 path) is never touched here.
    in.ret_rva = 0x2BC76BC;
    CHECK(ftl::decide_f3(in) == F3Out::NotHere);
    in.ret_rva = ftl::kApplyVf48Ret;  // the second call is decide_apply's
    CHECK(ftl::decide_f3(in) == F3Out::NotHere);
    in.ret_rva = ftl::kReloadVf48Ret;
    // The return address decides, not the opcode (a Troc outside the handler table reads as 0).
    in.opcode = 0;
    CHECK(ftl::decide_f3(in) == F3Out::Substitute);
    in.subject = false;  // the host's own 2283
    CHECK(ftl::decide_f3(in) == F3Out::NotHere);
}

void test_apply() {
    constexpr uintptr_t kMine = 0x7FF612340000;
    // Review 1: the second vf48 (0x2BC4C53) of the same 2283 gets user 2's REAL manager.
    CHECK(ftl::decide_apply(ftl::kApplyVf48Ret, kMine, false) == ftl::ApplyOut::Real);
    // The block cannot go there ([mgr+8] is referenced): the original, logged as an ERR.
    CHECK(ftl::decide_apply(ftl::kApplyVf48Ret, 0, true) == ftl::ApplyOut::Original);
    // Not armed (no F3 answer in this Troc), or another return address: untouched.
    CHECK(ftl::decide_apply(ftl::kApplyVf48Ret, 0, false) == ftl::ApplyOut::NotHere);
    CHECK(ftl::decide_apply(ftl::kReloadVf48Ret, kMine, false) == ftl::ApplyOut::NotHere);
    CHECK(ftl::decide_apply(0x2BC4C54, kMine, false) == ftl::ApplyOut::NotHere);
    // The block holds the subject at +0x18, where 0x2896155 reads it.
    CHECK(ftl::kManagerActor + sizeof(uintptr_t) <= ftl::kSubstituteBlock);
}

// P8 S28, F4: user 2's ReviveLoadingReq 2362 at the reload setter's vf48 (0x2BC4C53).
void test_f4() {
    using ftl::F4Out;
    ftl::F4In in;
    in.subject = true;
    in.opcode = ftl::kOpReviveLoadingReq;
    in.ret_rva = ftl::kApplyVf48Ret;
    in.mgr_ok = true;
    in.mode = ftl::Mode::On;
    CHECK(ftl::decide_f4(in) == F4Out::Substitute);
    in.mgr_ok = false;
    CHECK(ftl::decide_f4(in) == F4Out::NoManager);  // never a block here: [mgr+8] is referenced
    in.mgr_ok = true;
    in.mode = ftl::Mode::Probe;
    CHECK(ftl::decide_f4(in) == F4Out::Probe);
    in.mode = ftl::Mode::Off;
    CHECK(ftl::decide_f4(in) == F4Out::Off);
    in.mode = ftl::Mode::On;
    // In user 2's own field the original already is its own manager.
    in.own_field = true;
    CHECK(ftl::decide_f4(in) == F4Out::OwnField);
    in.own_field = false;
    // Another Req of user 2 at the setter in a field it does not own: decided by the field, like the 2362 (the
    // setter resets whatever manager vf48 returns; S29 one-PC: a 2601 at the login reset the host's).
    in.opcode = 2601;
    CHECK(ftl::decide_f4(in) == F4Out::OtherSubstitute);
    in.mgr_ok = false;
    CHECK(ftl::decide_f4(in) == F4Out::NoManager);
    in.mgr_ok = true;
    in.mode = ftl::Mode::Probe;
    CHECK(ftl::decide_f4(in) == F4Out::OtherProbe);
    in.mode = ftl::Mode::Off;
    CHECK(ftl::decide_f4(in) == F4Out::OtherReq);
    in.mode = ftl::Mode::On;
    in.own_field = true;
    CHECK(ftl::decide_f4(in) == F4Out::OwnField);
    in.own_field = false;
    in.opcode = ftl::kOpReviveLoadingReq;
    // F3 armed this Troc (a 2283): F3 owns the second vf48.
    in.f3_armed = true;
    CHECK(ftl::decide_f4(in) == F4Out::NotHere);
    in.f3_armed = false;
    // Not the setter's return address / no subject: not F4's.
    in.ret_rva = ftl::kReloadVf48Ret;
    CHECK(ftl::decide_f4(in) == F4Out::NotHere);
    in.ret_rva = ftl::kApplyVf48Ret;
    in.subject = false;
    CHECK(ftl::decide_f4(in) == F4Out::NotHere);
    for (size_t i = 0; i < static_cast<size_t>(F4Out::Count); ++i) {
        CHECK(std::string_view(ftl::f4_out_name(static_cast<F4Out>(i))) != "?");
    }
}

struct FakeMem {
    std::map<uintptr_t, uintptr_t> m;
    bool ptr(uintptr_t a, uintptr_t& v) const {
        const auto it = m.find(a);
        if (it == m.end()) return false;
        v = it->second;
        return true;
    }
};

void test_seq_manager_chain() {
    constexpr uintptr_t kVt = 0x145B17A58, kActor = 0x1000, kComps = 0x2000, kComp = 0x3000, kMgr = 0x4000;
    FakeMem f;
    f.m[kActor + 0x68] = kComps;
    f.m[kComps + 0xA8] = kComp;
    f.m[kComp + 0x18] = kMgr;
    f.m[kMgr] = kVt;
    f.m[kMgr + 0x18] = kActor;
    f.m[kMgr + 0x8] = 0x5000;  // the object Actor_Lock references
    f.m[0x5000] = 0x145000000;
    ftl::MgrFail why = ftl::MgrFail::Count;
    auto r = ftl::seq_manager_of(f, kActor, kVt, &why);
    CHECK(r.has_value() && *r == kMgr && why == ftl::MgrFail::None);
    // Review of 17c376b, 2: no +0x8 object (or no vtable) -> never handed to the game (Actor_Lock has no null check).
    f.m[kMgr + 0x8] = 0;
    r = ftl::seq_manager_of(f, kActor, kVt, &why);
    CHECK(!r && why == ftl::MgrFail::LockObject);
    f.m[kMgr + 0x8] = 0x5000;
    f.m[0x5000] = 0;
    r = ftl::seq_manager_of(f, kActor, kVt, &why);
    CHECK(!r && why == ftl::MgrFail::LockObject);
    f.m[0x5000] = 0x145000000;
    // The back pointer must be the character itself (0x2896155 takes r13 = [mgr+0x18]).
    f.m[kMgr + 0x18] = 0x9999;
    r = ftl::seq_manager_of(f, kActor, kVt, &why);
    CHECK(!r && why == ftl::MgrFail::BackPointer);
    f.m[kMgr + 0x18] = kActor;
    f.m[kMgr] = 0x145B1FA48;  // a quest manager's vtable
    r = ftl::seq_manager_of(f, kActor, kVt, &why);
    CHECK(!r && why == ftl::MgrFail::ManagerClass);
    f.m[kMgr] = kVt;
    f.m[kComp + 0x18] = 0;
    r = ftl::seq_manager_of(f, kActor, kVt, &why);
    CHECK(!r && why == ftl::MgrFail::Manager);
    f.m.erase(kComps + 0xA8);
    r = ftl::seq_manager_of(f, kActor, kVt, &why);
    CHECK(!r && why == ftl::MgrFail::Component);
    r = ftl::seq_manager_of(f, 0, kVt, &why);
    CHECK(!r && why == ftl::MgrFail::Components);
}

void test_guest() {
    CHECK(ftl::decide_guest(Mode::Off, true) == GuestAct::Native);
    CHECK(ftl::decide_guest(Mode::On, false) == GuestAct::Native);   // a local game: never vetoed
    CHECK(ftl::decide_guest(Mode::Probe, false) == GuestAct::Native);
    CHECK(ftl::decide_guest(Mode::Probe, true) == GuestAct::NativeLogged);
    CHECK(ftl::decide_guest(Mode::On, true) == GuestAct::Veto);
}

// ---------------------------------------------------------------- travel_probe_logic.h (P8 S15, external report)

constexpr size_t kHdr = 5;  // the Troc header in front of the payload (opcode u16, slot u8, length u16)

// A whole S->C packet: header (opcode, slot 0xFF, length) + payload.
std::vector<uint8_t> packet(uint16_t op, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> p = {static_cast<uint8_t>(op), static_cast<uint8_t>(op >> 8), 0xFF,
                              static_cast<uint8_t>(payload.size()), static_cast<uint8_t>(payload.size() >> 8)};
    p.insert(p.end(), payload.begin(), payload.end());
    return p;
}

void put32(std::vector<uint8_t>& p, size_t at, uint32_t v) { std::memcpy(p.data() + at, &v, 4); }
uint32_t get32(const std::vector<uint8_t>& p, size_t at) {
    uint32_t v = 0;
    std::memcpy(&v, p.data() + at, 4);
    return v;
}

// A LoadingFieldDataAck 2643 as 0x299B520 writes it: u8 4, actor, FieldInfoKey 1, owner = the host, flags, selection,
// append 0, then `records` 48-byte records (filled with a pattern so a stray write shows).
std::vector<uint8_t> loading_field_data(uint32_t owner, size_t records) {
    std::vector<uint8_t> p(tpl::kLoadingFieldHead + records * tpl::kRecord, 0);
    p[0] = 4;
    put32(p, 1, 0xA0100A4C);
    put32(p, 5, 1);
    put32(p, 9, owner);
    p[13] = 1;
    p[14] = 0;
    put32(p, 15, 0x11223344);
    p[19] = 0;
    for (size_t i = tpl::kLoadingFieldHead; i < p.size(); ++i) p[i] = static_cast<uint8_t>(i * 7);
    return p;
}

void test_owner_alias_2643() {
    const auto payload = loading_field_data(kHostUser, 2);
    auto pkt = packet(tpl::kOpLoadingFieldDataAck, payload);
    const auto before = pkt;
    uint32_t orig = 0;
    // User 2's copy: only the u32 +9 changes, to user 2; the owner the server wrote is reported.
    CHECK(tpl::alias_owner(pkt, kHdr, tpl::kOpLoadingFieldDataAck, kUser2, true, &orig) == tpl::AliasOut::Rewrite);
    CHECK(orig == kHostUser);
    CHECK(get32(pkt, kHdr + 9) == kUser2);
    for (size_t i = 0; i < pkt.size(); ++i) {
        if (i < kHdr + 9 || i >= kHdr + 13) CHECK(pkt[i] == before[i]);
    }
    // The parsed head of the rewritten copy: everything but the owner as written.
    const auto d = tpl::parse_loading_field_data(std::span<const uint8_t>(pkt).subspan(kHdr));
    CHECK(d && d->kind == 4 && d->actor == 0xA0100A4C && d->field_key == 1 && d->owner == kUser2 && d->flag13 == 1 &&
          d->selection == 0x11223344 && d->append == 0 && d->records == 2);
    // Head only (the probe's copy): same answer with the whole size.
    const auto h = tpl::parse_loading_field_data(std::span<const uint8_t>(payload.data(), 24), payload.size());
    CHECK(h && h->owner == kHostUser && h->records == 2);
    // No records at all is still the layout (20 + 48 * 0).
    auto empty = packet(tpl::kOpLoadingFieldDataAck, loading_field_data(kHostUser, 0));
    CHECK(tpl::alias_owner(empty, kHdr, tpl::kOpLoadingFieldDataAck, kUser2, true) == tpl::AliasOut::Rewrite);
    // Switched off: left as the server wrote it.
    pkt = before;
    CHECK(tpl::alias_owner(pkt, kHdr, tpl::kOpLoadingFieldDataAck, kUser2, false, &orig) == tpl::AliasOut::Off);
    CHECK(pkt == before);
    // Not the layout (one byte short of a record; the 2174's 17 + 48n): nothing written, owner unknown.
    for (size_t n : {size_t{19}, size_t{20 + 47}, size_t{17 + 48}, size_t{21}}) {
        std::vector<uint8_t> bad(n, 0x90);
        auto bp = packet(tpl::kOpLoadingFieldDataAck, bad);
        const auto bb = bp;
        CHECK(tpl::alias_owner(bp, kHdr, tpl::kOpLoadingFieldDataAck, kUser2, true, &orig) ==
              tpl::AliasOut::BadLayout);
        CHECK(orig == 0 && bp == bb);
    }
    // User 2's key unknown, the owner already user 2, the u32 +9 not a user key: untouched.
    pkt = before;
    CHECK(tpl::alias_owner(pkt, kHdr, tpl::kOpLoadingFieldDataAck, 0, true) == tpl::AliasOut::NoKey && pkt == before);
    auto mine = packet(tpl::kOpLoadingFieldDataAck, loading_field_data(kUser2, 1));
    const auto mine_before = mine;
    CHECK(tpl::alias_owner(mine, kHdr, tpl::kOpLoadingFieldDataAck, kUser2, true) == tpl::AliasOut::AlreadyMine);
    CHECK(mine == mine_before);
    auto npc = packet(tpl::kOpLoadingFieldDataAck, loading_field_data(0xB0100123, 1));
    const auto npc_before = npc;
    CHECK(tpl::alias_owner(npc, kHdr, tpl::kOpLoadingFieldDataAck, kUser2, true) == tpl::AliasOut::NotAUser);
    CHECK(npc == npc_before);
    // Another opcode (3066 carries the traveller's own actor key, no owner): never touched.
    auto post = packet(tpl::kOpMoveFieldCompletePostAck, std::vector<uint8_t>{0x4C, 0x0A, 0x10, 0xA0, 1, 0});
    const auto post_before = post;
    CHECK(tpl::alias_owner(post, kHdr, tpl::kOpMoveFieldCompletePostAck, kUser2, true) == tpl::AliasOut::NotOwnerAck);
    CHECK(post == post_before);
}

void test_owner_alias_2174() {
    // The 2174's own layout (17 + 48n) and the same +9; the 2643's switch does not apply to it (server.cpp passes
    // enabled = true for 2174).
    std::vector<uint8_t> payload(17 + 48, 0x33);
    put32(payload, 9, kHostUser);
    auto pkt = packet(tpl::kOpMoveFieldAck, payload);
    uint32_t orig = 0;
    CHECK(tpl::alias_owner(pkt, kHdr, tpl::kOpMoveFieldAck, kUser2, true, &orig) == tpl::AliasOut::Rewrite);
    CHECK(orig == kHostUser && get32(pkt, kHdr + 9) == kUser2 && pkt[kHdr + 8] == 0x33 && pkt[kHdr + 13] == 0x33);
    // A 2643-sized payload is not a 2174.
    std::vector<uint8_t> wrong(20 + 48, 0);
    auto wp = packet(tpl::kOpMoveFieldAck, wrong);
    CHECK(tpl::alias_owner(wp, kHdr, tpl::kOpMoveFieldAck, kUser2, true) == tpl::AliasOut::BadLayout);
    CHECK(tpl::owner_layout_ok(tpl::kOpMoveFieldAck, 17) && !tpl::owner_layout_ok(tpl::kOpMoveFieldAck, 20));
    CHECK(tpl::owner_layout_ok(tpl::kOpLoadingFieldDataAck, 20) && !tpl::owner_layout_ok(tpl::kOpLoadingFieldDataAck, 17));
}

void test_parse_logout() {
    // S14's 2679 (G bin r640299): 4C 0A 10 A0 | 0B | 0A 63 19 8E.
    const auto l = tpl::parse_logout(hex("4C0A10A0 0B 0A63198E"));
    CHECK(l && l->actor == 0xA0100A4C && l->reason == 0x0B && l->error == tpl::kErrInvalidFieldInfoKey);
    CHECK(!tpl::parse_logout(hex("4C0A10A0 0B 0A6319")));
}

void test_ts_pack() {
    tpl::TsSnap s;
    s.ok = true;
    s.pending = 1;
    s.f568 = 0;
    s.f569 = 0xFF;
    s.armed = 1;
    const tpl::TsSnap u = tpl::unpack(tpl::pack(s));
    CHECK(u.ok && u.pending == 1 && u.f568 == 0 && u.f569 == 0xFF && u.armed == 1 && tpl::same(s, u));
    CHECK(!tpl::unpack(tpl::pack(tpl::TsSnap{})).ok);  // unreadable stays unreadable
    tpl::TsSnap zero;
    zero.ok = true;  // read fine, all zero: not the same as unreadable
    CHECK(tpl::unpack(tpl::pack(zero)).ok && tpl::pack(zero) != 0);
}

void test_prepared() {
    tpl::TsSnap empty, set, host;
    empty.ok = set.ok = host.ok = true;
    set.pending = 1;
    set.f569 = 1;
    set.armed = 1;
    host.pending = 0;
    tpl::PreparedIn in;
    in.u2_before = empty;
    in.u2_after = set;
    in.host_before = host;
    in.host_after = host;
    in.host_mgr_ok = true;
    in.host_6a0_before = in.host_6a0_after = 1;
    CHECK(tpl::decide_prepared(in) == tpl::PreparedOut::Prepared);
    // S14's failure as the probe would see it: user 2's TS still empty...
    in.u2_after = empty;
    CHECK(tpl::decide_prepared(in) == tpl::PreparedOut::NotPrepared);
    // ...and the host's TS written: that wins over everything (abort criterion).
    in.host_after = set;
    CHECK(tpl::decide_prepared(in) == tpl::PreparedOut::HostTouched);
    in.host_after = host;
    // The host's manager +0x6A0 zeroed by the second vf48 (the block case): host touched too.
    in.u2_after = set;
    in.host_6a0_after = 0;
    CHECK(tpl::decide_prepared(in) == tpl::PreparedOut::HostTouched);
    in.host_mgr_ok = false;  // unreadable manager: no claim about it
    CHECK(tpl::decide_prepared(in) == tpl::PreparedOut::Prepared);
    // Armed but no key, or a key but not armed: not prepared.
    in.u2_after.armed = 0;
    CHECK(tpl::decide_prepared(in) == tpl::PreparedOut::NotPrepared);
    in.u2_after = tpl::TsSnap{};
    CHECK(tpl::decide_prepared(in) == tpl::PreparedOut::Unreadable);
}

void test_consume() {
    tpl::TsSnap s;
    CHECK(tpl::decide_consume(s) == tpl::ConsumeOut::Unreadable);
    s.ok = true;
    CHECK(tpl::decide_consume(s) == tpl::ConsumeOut::Empty);  // 0x2BC9256 -> 0x8E19630A -> 2679
    s.pending = 1;
    s.f569 = 1;
    CHECK(tpl::decide_consume(s) == tpl::ConsumeOut::Pending);
    CHECK(std::strstr(tpl::consume_branch(s), "0x2BCA3D0") != nullptr);
    s.f568 = 1;
    CHECK(std::strstr(tpl::consume_branch(s), "GameLoadingStart") != nullptr);
}

void test_context_ok() {
    constexpr uintptr_t kMine = 0x1000, kOwners = 0x2000;
    constexpr uint32_t kChar2 = 0xA0100A4C;
    CHECK(tpl::context_ok(kChar2, kChar2, kMine, kOwners));
    CHECK(!tpl::context_ok(0xA0100001, kChar2, kOwners, kOwners));  // the owner's (S14)
    CHECK(!tpl::context_ok(kChar2, kChar2, kOwners, kOwners));       // same object as the owner's
    CHECK(!tpl::context_ok(0, kChar2, kMine, kOwners));
    CHECK(tpl::context_ok(kChar2, kChar2, kMine, 0));  // owner's manager not readable: judged on +0x18 alone
}

void test_reply_and_window() {
    tpl::ReplyCounts c;
    CHECK(tpl::classify_reply(c) == tpl::Reply::Nothing);
    c.add(tpl::kOpLoadingFieldDataAck);
    c.add(tpl::kOpMoveFieldCompletePostAck);
    CHECK(tpl::classify_reply(c) == tpl::Reply::Reload2643);
    tpl::ReplyCounts f;
    f.add(tpl::kOpMoveFieldAck);
    CHECK(tpl::classify_reply(f) == tpl::Reply::FieldChange2174);
    c.add(tpl::kOpLogoutCharacterAck);  // a refusal wins
    CHECK(tpl::classify_reply(c) == tpl::Reply::Refused2679);
    tpl::ReplyCounts n;
    n.add(tpl::kOpNak);
    n.add(2005);  // other traffic is not counted
    CHECK(tpl::classify_reply(n) == tpl::Reply::Nak && n.n2174 == 0);
    // The window: from its opening, kWindowMs and kMaxAcks.
    CHECK(tpl::ack_in_window(1000, 1000, 0));
    CHECK(tpl::ack_in_window(1000 + tpl::kWindowMs, 1000, tpl::kMaxAcks - 1));
    CHECK(!tpl::ack_in_window(1001 + tpl::kWindowMs, 1000, 0));
    CHECK(!tpl::ack_in_window(1500, 1000, tpl::kMaxAcks));
    CHECK(!tpl::ack_in_window(1500, 0, 0));  // never opened
    CHECK(!tpl::ack_in_window(999, 1000, 0));
    CHECK(tpl::always_noted(tpl::kOpLogoutCharacterAck) && tpl::always_noted(tpl::kOpNak) && !tpl::always_noted(2643));
    // P8 S16: the 2322s to user 2 are counted, without changing the reply class.
    tpl::ReplyCounts r;
    r.add(tpl::kOpLoadingFieldDataAck);
    r.add(tpl::kOpPlayAllPreLoadSequencerAck);
    CHECK(r.n2322 == 1 && tpl::classify_reply(r) == tpl::Reply::Reload2643);
}

// P8 S16: TravelAck's host side effect (p8-s15-viaje-tunel.md §5: user 2's 2873 at host t=561.078, the host's client
// got 21 x 2981 + 21 x 2661 at 561.227-561.507 and its 2322 at 561.538, without a 2873 of its own).
void test_host_burst() {
    constexpr uint64_t t = 561'078'000;
    tpl::HostBurst b;
    CHECK(tpl::classify_host(b) == tpl::HostEffect::No2873);
    b.open(t, /*to_server=*/true);
    for (int i = 0; i < 21; ++i) {
        b.on_host(tpl::kOpStageBranchStateAck, true, t + 149'000 + i * 1000);
        b.on_host(tpl::kOpPrepareSequencerAck, true, t + 150'000 + i * 1000);
    }
    b.on_host(tpl::kOpPlayAllPreLoadSequencerAck, true, t + 460'000);
    b.on_host(2005, true, t + 470'000);                                     // other traffic
    b.on_host(tpl::kOpPrepareSequencerAck, true, t + tpl::kHostBurstUs + 1);  // after the window
    b.on_host(tpl::kOpPrepareSequencerAck, true, t - 1);                     // before it
    CHECK(b.n2661 == 21 && b.n2981 == 21 && b.n2322 == 1);
    CHECK(tpl::classify_host(b) == tpl::HostEffect::SideEffect);
    tpl::HostBurstView v = tpl::unpack_host(tpl::pack(b));
    CHECK(v.armed && v.handed && !v.host_own && v.n2661 == 21 && v.n2981 == 21 && v.n2322 == 1);
    CHECK(v.effect == tpl::HostEffect::SideEffect);
    // Dropped by observer_milestone_reload: no burst -> clean.
    tpl::HostBurst d;
    d.open(t, /*to_server=*/false);
    d.on_host(2005, true, t + 100'000);
    CHECK(tpl::classify_host(d) == tpl::HostEffect::Clean);
    v = tpl::unpack_host(tpl::pack(d));
    CHECK(v.armed && !v.handed && v.effect == tpl::HostEffect::Clean);
    // The host's own reload (its client's C>S 2873 shortly before): ambiguous, not blamed on user 2.
    tpl::HostBurst h;
    h.on_host(tpl::kOpPreLoadSequencerReq, false, t - 200'000);
    h.open(t, true);
    h.on_host(tpl::kOpPrepareSequencerAck, true, t + 150'000);
    CHECK(tpl::classify_host(h) == tpl::HostEffect::Ambiguous);
    CHECK(tpl::unpack_host(tpl::pack(h)).effect == tpl::HostEffect::Ambiguous);
    // ... but one long before (its own earlier reload) is not.
    tpl::HostBurst o;
    o.on_host(tpl::kOpPreLoadSequencerReq, false, t - 30'000'000);
    o.open(t, true);
    o.on_host(tpl::kOpPrepareSequencerAck, true, t + 150'000);
    CHECK(tpl::classify_host(o) == tpl::HostEffect::SideEffect);
}

}  // namespace

int main() {
    test_parse_mode();
    test_parse_reload();
    test_seq_copy_ignores_2283();
    test_where();
    test_gate();
    test_f3();
    test_apply();
    test_f4();
    test_seq_manager_chain();
    test_guest();
    test_owner_alias_2643();
    test_owner_alias_2174();
    test_parse_logout();
    test_ts_pack();
    test_prepared();
    test_consume();
    test_context_ok();
    test_reply_and_window();
    test_host_burst();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_fast_travel: all passed\n");
    return 0;
}
