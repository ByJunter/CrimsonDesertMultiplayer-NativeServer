// P8 session 13, the shopkeepers (docs/research/pendientes/p8-impl-tenderos-c1.md).
//
// SCOPE: offline logic only (stage_lookup_logic.h, guest_x_interaction_logic.h, shop_probe_logic.h):
//  - isl::hash_id against lookup3 hashlittle (gsl::hashlittle, its published vectors are tested there) with initval
//    0xC5EDE on the 8 bytes of the id, and against a literal transliteration of 0x1364780's case-8 path;
//  - isl::find_stage on a fake manager (0x1F6C190's table: counts, buckets of 0x100, {hash, index} pairs, elements with
//    the u64 id at +8 and the instance at +0x10, the dead flag +0x15): found, absent, another id in the same bucket,
//    a full bucket, dead, empty table, unreadable memory;
//  - isl::IdBook (first K per id, full table), the points' constants;
//  - gxi: the key, check_x (every state), decide (every verdict in order) and act per mode, the X slots (note, update,
//    full -> round robin), Call::same;
//  - shp: the per-character table, who (by user key, by character key, unknown), base_name / is_funcnpc, the id ring,
//    the packet parsers (session 11's 2671 / 2663, a 2436, a 2981).
// They do NOT test the hooks, the game's code nor the server.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <span>
#include <string_view>
#include <vector>

#include "mod/session2/guest_sched_load_logic.h"
#include "mod/session2/guest_x_interaction_logic.h"
#include "mod/session2/shop_probe_logic.h"
#include "mod/session2/stage_lookup_logic.h"

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

// ---------------------------------------------------------------- a fake memory

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

// 0x1364780 for length 8 transliterated instruction by instruction (registers: r11d = a, r9d = b, ebx = c).
uint32_t asm_hash8(uint64_t id) {
    auto ror = [](uint32_t x, int k) { return (x >> k) | (x << (32 - k)); };
    uint32_t ebx = static_cast<uint32_t>(8 - 0x2145E233);  // lea ebx,[rdx-0x2145E233]
    uint32_t r9 = ebx, r11 = ebx;
    r9 += static_cast<uint32_t>(id >> 32);  // 0x1364874 add r9d,[rcx+4]
    r11 += static_cast<uint32_t>(id);       // 0x1364878 add r11d,[rcx]
    ebx ^= r9;                              // 0x1364C06
    ebx -= ror(r9, 0x12);
    uint32_t ecx = ebx ^ r11;
    ecx -= ror(ebx, 0x15);
    uint32_t r8 = ecx ^ r9;
    r8 -= ror(ecx, 7);
    r9 = r8 ^ ebx;
    r9 -= ror(r8, 0x10);
    uint32_t edx = r9 ^ ecx;
    edx -= ror(r9, 0x1C);
    ecx = edx;
    edx ^= r8;
    edx -= ror(ecx, 0x12);
    ecx = edx;
    edx ^= r9;
    edx -= ror(ecx, 8);
    return edx;
}

void test_hash() {
    for (uint64_t id : {0x197ull, 0x198ull, 0x19Dull, 0x1C0ull, 0x1ull, 0xFFFFFFFFull, 0x0000000100000197ull,
                        0x123456789ABCDEF0ull}) {
        uint8_t b[8];
        std::memcpy(b, &id, 8);
        CHECK(isl::hash_id(id) == gsl::hashlittle(b, 8, isl::kIdHashInit));
        CHECK(isl::hash_id(id) == asm_hash8(id));
    }
    static_assert(isl::kIdHashInit == gsl::kNameHashInit);
    static_assert(isl::hash_id(0x197) == isl::hash_id(0x197));  // constexpr
    CHECK(isl::hash_id(0x197) != isl::hash_id(0x198));
}

// A fake ClientSequencerStageManager table at `mgr` with `buckets` buckets holding `ids` (element i -> instance
// 0x9000000 + i * 0x1000), dead instances where asked.
struct FakeMgr {
    uintptr_t mgr = 0x10000000, barr = 0x20000000, elems = 0x30000000;
    uint32_t buckets = 8;
};
void build(FakeMem& m, const FakeMgr& f, const std::vector<uint64_t>& ids, const std::vector<bool>& dead = {}) {
    m.put<uint32_t>(f.mgr + isl::kMgr_Buckets, f.buckets);
    m.put<uint32_t>(f.mgr + isl::kMgr_Count, static_cast<uint32_t>(ids.size()));
    m.put<uintptr_t>(f.mgr + isl::kMgr_BucketArray, f.barr);
    m.put<uintptr_t>(f.mgr + isl::kMgr_Elements, f.elems);
    std::vector<uint32_t> fill(f.buckets, 0);
    for (uint32_t b = 0; b < f.buckets; ++b) m.put<uint32_t>(f.barr + b * isl::kBucketStride, 0);
    for (uint32_t i = 0; i < ids.size(); ++i) {
        const uint32_t h = isl::hash_id(ids[i]);
        const uint32_t b = h % f.buckets;
        const uintptr_t bucket = f.barr + b * isl::kBucketStride;
        const uint32_t k = fill[b]++;
        m.put<uint32_t>(bucket, fill[b]);
        m.put<uint32_t>(bucket + 8 + 8 * k, h);
        m.put<uint32_t>(bucket + 0xC + 8 * k, i);
        const uintptr_t elem = 0x40000000 + i * 0x100;
        const uintptr_t inst = 0x09000000 + i * 0x1000;
        m.put<uintptr_t>(f.elems + 8 * i, elem);
        m.put<uint64_t>(elem + isl::kElem_Id, ids[i]);
        m.put<uintptr_t>(elem + isl::kElem_Inst, inst);
        m.put<uint8_t>(inst + isl::kInst_Dead, i < dead.size() && dead[i] ? 1 : 0);
    }
}

void test_find() {
    FakeMem m;
    FakeMgr f;
    // Session 12: the shops of X.
    const std::vector<uint64_t> ids = {0x197, 0x198, 0x199, 0x19A, 0x19B, 0x1A4, 0x1A5, 0x1B2, 0x1BF, 0x1C0};
    build(m, f, ids, {false, false, false, false, false, false, false, false, false, true});
    for (size_t i = 0; i + 1 < ids.size(); ++i) {
        const isl::Lookup r = isl::find_stage(m, f.mgr, ids[i]);
        CHECK(r.read_ok && r.found && !r.dead);
        CHECK(r.inst == 0x09000000 + i * 0x1000);
        CHECK(r.count == ids.size());
    }
    const isl::Lookup dead = isl::find_stage(m, f.mgr, 0x1C0);
    CHECK(dead.read_ok && !dead.found && dead.dead && dead.inst != 0);
    const isl::Lookup absent = isl::find_stage(m, f.mgr, 0x19D);  // the guest's own scene: not in X
    CHECK(absent.read_ok && !absent.found && !absent.dead && absent.inst == 0);
    // The full u64 is compared (0x1F6C217 cmp [rcx+8],rax): the same low dword with high bits is another id.
    CHECK(!isl::find_stage(m, f.mgr, 0x0000000100000197ull).found);
    // Null manager, unreadable manager.
    CHECK(!isl::find_stage(m, 0, 0x197).read_ok);
    CHECK(!isl::find_stage(m, 0x50000000, 0x197).read_ok);
    // An element pointer that cannot be read: not read_ok.
    {
        FakeMem m2;
        build(m2, f, {0x197});
        m2.bytes.erase(f.elems);
        const isl::Lookup r = isl::find_stage(m2, f.mgr, 0x197);
        CHECK(!r.read_ok && !r.found);
    }
    // Empty table: read, not found (0x1F6C1B9 je on count 0).
    {
        FakeMem m3;
        build(m3, f, {});
        const isl::Lookup r = isl::find_stage(m3, f.mgr, 0x197);
        CHECK(r.read_ok && !r.found && r.count == 0);
    }
    // One bucket: every id collides in it, still found by the id compare.
    {
        FakeMem m4;
        FakeMgr one = f;
        one.buckets = 1;
        build(m4, one, ids);
        CHECK(isl::find_stage(m4, one.mgr, 0x1A5).found);
        CHECK(isl::find_stage(m4, one.mgr, 0x1A5).inst == 0x09000000 + 6 * 0x1000);
        CHECK(!isl::find_stage(m4, one.mgr, 0x19D).found);
    }
    // A bucket count claiming more than 31 pairs is capped (a bucket is 0x100 bytes).
    {
        FakeMem m5;
        FakeMgr one = f;
        one.buckets = 1;
        build(m5, one, {0x197});
        m5.put<uint32_t>(one.barr, 1000);
        CHECK(isl::find_stage(m5, one.mgr, 0x197).found);
        CHECK(!isl::find_stage(m5, one.mgr, 0x198).read_ok);  // the 2nd..31st pairs are unmapped in the fake
    }
}

void test_book() {
    isl::IdBook<4> b;
    CHECK(!b.take(0, 3));
    CHECK(b.take(0x197, 2));
    CHECK(b.take(0x197, 2));
    CHECK(!b.take(0x197, 2));
    CHECK(b.count_of(0x197) == 3);
    CHECK(b.take(0x198, 1) && b.take(0x199, 1) && b.take(0x19A, 1));
    CHECK(!b.take(0x19B, 1));  // full
    CHECK(b.full() == 1);
    CHECK(b.used() == 4);
}

void test_points() {
    static_assert(isl::kPatchA == sizeof isl::kBytesA);
    static_assert(isl::kPatchR == sizeof isl::kBytesR);
    static_assert(isl::kPatchF == sizeof isl::kBytesF);
    static_assert(isl::kBytesA[0] == 0x48 && isl::kBytesA[1] == 0x8B && isl::kBytesA[2] == 0xF8);  // mov rdi,rax
    static_assert(isl::kCtxA + sizeof isl::kCtxABytes == isl::kPointA);  // call [rax+0x48] ends at A
    // The call 0x1F6C190 in the lookup context: E8 rel32 from 0x2126A6A.
    const uintptr_t call_at = isl::kCtxLookup + sizeof isl::kCtxLookupBytes - 5;
    int32_t rel = 0;
    std::memcpy(&rel, isl::kCtxLookupBytes + sizeof isl::kCtxLookupBytes - 4, 4);
    static_assert(isl::kCtxLookup + sizeof isl::kCtxLookupBytes - 5 == 0x2126A6A);
    CHECK(call_at + 5 + rel == 0x1F6C190);
    static_assert(isl::kPointR > isl::kPointA && isl::kPointF > isl::kPointR);
}

// ---------------------------------------------------------------- C1 (guest)

constexpr uintptr_t kVt = 0x1455C9720;
constexpr uintptr_t kX = 0x40FC0000100, kM = 0x40FC16EAE00;
constexpr uint32_t kHost = 0xA0100001, kOwn = 0xA0100336;

gxi::XCheck good_x() {
    gxi::XCheck c;
    c.x = kX;
    c.vt = c.want_vt = kVt;
    c.key = kHost;
    c.actor_mgr = kX;
    return c;
}

void test_check_x() {
    CHECK(gxi::check_x(good_x(), kM, kOwn, kOwn) == gxi::XState::Ok);
    gxi::XCheck c = good_x();
    c.vt = 0x1455C9000;
    CHECK(gxi::check_x(c, kM, kOwn, kOwn) == gxi::XState::NotManager);
    c = good_x();
    c.x = kM;
    c.actor_mgr = kM;
    CHECK(gxi::check_x(c, kM, kOwn, kOwn) == gxi::XState::IsM);
    c = good_x();
    c.key = 0;
    CHECK(gxi::check_x(c, kM, kOwn, kOwn) == gxi::XState::NoCharacter);
    c = good_x();
    c.key = kOwn;
    CHECK(gxi::check_x(c, kM, 0, kOwn) == gxi::XState::OwnCharacter);  // the own character pinned at login
    CHECK(gxi::check_x(c, kM, kOwn, 0) == gxi::XState::OwnCharacter);  // M's live owner
    c = good_x();
    c.actor_mgr = 0x40FC9999999;
    CHECK(gxi::check_x(c, kM, kOwn, kOwn) == gxi::XState::NotItsManager);
    CHECK(std::string_view(gxi::xstate_name(gxi::XState::Ok)) == "ok");
}

gxi::Facts shop_entry() {  // session 12: the butcher's entry 0x197 in X, the guest 2.7 m away
    gxi::Facts f;
    f.client_field = true;
    f.remote = true;
    f.m = kM;
    f.id = 0x197;
    f.m_read = true;
    f.in_m = false;
    f.x_known = 1;
    f.x_valid = 1;
    f.x_read = true;
    f.x = kX;
    return f;
}

void test_decide() {
    using gxi::Verdict;
    CHECK(gxi::decide(shop_entry()) == Verdict::Redirect);
    gxi::Facts f = shop_entry();
    f.client_field = false;
    CHECK(gxi::decide(f) == Verdict::NotClient);
    f = shop_entry();
    f.remote = false;
    CHECK(gxi::decide(f) == Verdict::NotRemote);
    f = shop_entry();
    f.m = 0;
    CHECK(gxi::decide(f) == Verdict::NoM);
    f = shop_entry();
    f.id = 0x2000000000000000ull;  // & 0x1FFFFFFF == 0
    CHECK(gxi::decide(f) == Verdict::NoId);
    f = shop_entry();
    f.m_read = false;
    CHECK(gxi::decide(f) == Verdict::MUnread);
    f = shop_entry();
    f.in_m = true;  // a native entry: M has it
    CHECK(gxi::decide(f) == Verdict::InM);
    f = shop_entry();
    f.x_valid = 0;
    f.x = 0;
    CHECK(gxi::decide(f) == Verdict::NoX);
    f = shop_entry();
    f.x = 0;
    CHECK(gxi::decide(f) == Verdict::NotInX);
    f.x_dead = true;
    CHECK(gxi::decide(f) == Verdict::DeadInX);
    for (size_t k = 0; k < static_cast<size_t>(Verdict::Count); ++k) {
        CHECK(std::string_view(gxi::verdict_name(static_cast<Verdict>(k))) != "?");
    }
    // Modes.
    CHECK(gxi::act(gxi::Mode::Off, Verdict::Redirect) == gxi::Act::None);
    CHECK(gxi::act(gxi::Mode::Probe, Verdict::Redirect) == gxi::Act::Would);
    CHECK(gxi::act(gxi::Mode::On, Verdict::Redirect) == gxi::Act::Redirect);
    for (auto v : {Verdict::InM, Verdict::NotInX, Verdict::DeadInX, Verdict::NoX, Verdict::NotClient}) {
        CHECK(gxi::act(gxi::Mode::On, v) == gxi::Act::None);  // only an entry found in X is ever redirected
    }
    CHECK(gxi::worth_a_line(Verdict::Redirect) && gxi::worth_a_line(Verdict::NotInX));
    CHECK(!gxi::worth_a_line(Verdict::InM) && !gxi::worth_a_line(Verdict::NotClient));
}

void test_mode() {
    bool bad = false;
    CHECK(gxi::parse_mode("0") == gxi::Mode::Off);
    CHECK(gxi::parse_mode("") == gxi::Mode::Off);
    CHECK(gxi::parse_mode("probe") == gxi::Mode::Probe);
    CHECK(gxi::parse_mode("1") == gxi::Mode::On);
    CHECK(gxi::parse_mode("yes", &bad) == gxi::Mode::Off && bad);
    CHECK(std::string_view(gxi::mode_name(gxi::Mode::Probe)) == "probe");
}

void test_slots() {
    gxi::XSlots<2> s;
    uintptr_t xs[4] = {};
    uint32_t keys[4] = {};
    CHECK(s.snapshot(xs, keys, 4) == 0);
    s.note(0, kHost);  // ignored
    s.note(kX, 0);
    s.note(kX, kHost);  // the same X again: its key updated, no new slot
    CHECK(s.snapshot(xs, keys, 4) == 1 && xs[0] == kX && keys[0] == kHost);
    s.note(kX + 0x100, 0xA0100002);
    CHECK(s.snapshot(xs, keys, 4) == 2);
    s.note(kX + 0x200, 0xA0100003);  // full: replaces the oldest slot (round robin)
    CHECK(s.snapshot(xs, keys, 4) == 2);
    CHECK(s.replaced() == 1);
    bool found_new = false;
    for (size_t i = 0; i < 2; ++i) found_new |= xs[i] == kX + 0x200 && keys[i] == 0xA0100003;
    CHECK(found_new);
    s.clear();
    CHECK(s.snapshot(xs, keys, 4) == 0);
}

void test_call() {
    gxi::Call c;
    CHECK(!c.same(0, 0));
    c.entry = 0x7000;
    c.rsp = 0x14F000;
    CHECK(c.same(0x7000, 0x14F000));
    CHECK(!c.same(0x7000, 0x14F008));
    CHECK(!c.same(0x7008, 0x14F000));
    CHECK(std::string_view(gxi::result_name(0)).starts_with("0"));
    CHECK(std::string_view(gxi::result_name(9)) == "?");
}

// ---------------------------------------------------------------- the shop probe (host)

void test_keys() {
    shp::KeyTable<2> t;
    using T = shp::KeyTable<2>;
    t.add(kHost, T::Entries);
    t.add(kHost, T::Gate, 0x90100000, 0x90100000);
    t.add(kHost, T::Owner, 0x90100000, 0x90100000);
    t.add(kOwn, T::Entries);
    t.add(kOwn, T::Gate, 0x90100335, 0x90100000);
    t.add(kOwn, T::NotOwner, 0x90100335, 0x90100000);
    t.add(kOwn, T::NotOwner, 0x90100335, 0x90100000);
    t.add(0xA0100777, T::Entries);  // a third character: overflow
    t.add(0, T::Entries);           // no key: overflow
    shp::KeyRow rows[4];
    CHECK(t.rows(rows, 4) == 2);
    CHECK(t.of(kOwn, T::NotOwner) == 2 && t.of(kOwn, T::Owner) == 0);
    CHECK(t.of(kHost, T::Owner) == 1 && t.of(kHost, T::Entries) == 1);
    CHECK(t.overflow(T::Entries) == 2);
    for (size_t i = 0; i < 2; ++i) {
        if (rows[i].key == kOwn) CHECK(rows[i].user_key == 0x90100335 && rows[i].field_owner == 0x90100000);
    }
}

void test_who() {
    using shp::Who;
    CHECK(shp::who(0x90100335, kOwn, 0x90100335, kOwn) == Who::User2);
    CHECK(shp::who(0x90100000, kHost, 0x90100335, kOwn) == Who::Host);
    CHECK(shp::who(0, kOwn, 0x90100335, kOwn) == Who::User2);  // no user read: by the character
    CHECK(shp::who(0, kHost, 0, kOwn) == Who::Host);
    CHECK(shp::who(0x90100335, kOwn, 0, 0) == Who::Unknown);  // user 2 not known yet
    CHECK(std::string_view(shp::who_name(Who::User2)) == "USER 2");
}

void test_names() {
    CHECK(shp::base_name("sequencer/stage/funcnpc/cd_seq_funcnpc_butcher.paseq") == "cd_seq_funcnpc_butcher.paseq");
    CHECK(shp::base_name("a\\b\\cd_seq_funcnpc_pub") == "cd_seq_funcnpc_pub");
    CHECK(shp::base_name("cd_seq_funcnpc_bank") == "cd_seq_funcnpc_bank");
    CHECK(shp::is_funcnpc("x/cd_seq_funcnpc_smithy"));
    CHECK(!shp::is_funcnpc("x/cd_seq_node_her_bluemont"));
    CHECK(!shp::is_funcnpc("cd_seq_funcnpc_/../cd_minigame_armwrest_bas"));
    shp::IdRing<2> r;
    r.add(0x197);
    r.add(0x197);
    CHECK(r.has(0x197) && r.size() == 1);
    r.add(0x198);
    r.add(0x199);  // replaces the oldest
    CHECK(r.has(0x199) && r.size() == 2 && !r.has(0x197));
    CHECK(!r.has(0));
}

void test_packets() {
    // Session 11 (H1): C>S 2671 A0100001 -> B0100153 interaction 000F4321 ... 00000001.
    const uint8_t p2671[] = {0x01, 0x00, 0x10, 0xA0, 0x53, 0x01, 0x10, 0xB0, 0x21, 0x43, 0x0F, 0x00,
                             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    const shp::Interaction i = shp::parse_interaction(p2671);
    CHECK(i.ok && i.requester == 0xA0100001 && i.npc == 0xB0100153 && i.interaction == 0x000F4321);
    CHECK(!shp::parse_interaction(std::span<const uint8_t>(p2671, 11)).ok);
    // S>C 2663 A0100001 A0100001 B0100153 000F4321.
    const uint8_t p2663[] = {0x01, 0x00, 0x10, 0xA0, 0x01, 0x00, 0x10, 0xA0,
                             0x53, 0x01, 0x10, 0xB0, 0x21, 0x43, 0x0F, 0x00};
    const shp::Interaction a = shp::parse_interaction_ack(p2663);
    CHECK(a.ok && a.requester == 0xA0100001 && a.npc == 0xB0100153 && a.interaction == 0x000F4321);
    // Session 11: C>S 2436 96 01 00 00 00 00 00 00 | 06 ...
    const uint8_t p2436[] = {0x96, 0x01, 0, 0, 0, 0, 0, 0, 0x06, 0, 0, 0};
    const shp::SeqReq s = shp::parse_seq_req(p2436);
    CHECK(s.ok && s.seq == 0x196 && s.after == 6);
    CHECK(!shp::parse_seq_req(std::span<const uint8_t>(p2436, 7)).ok);
    // 2981: u8 4 | u32 actor | u64 seq | u32 state.
    const uint8_t p2981[] = {0x04, 0x01, 0x00, 0x10, 0xA0, 0x97, 0x01, 0, 0, 0, 0, 0, 0, 0x06, 0, 0, 0};
    const shp::BranchAck b = shp::parse_branch_ack(p2981);
    CHECK(b.ok && b.actor == 0xA0100001 && b.seq == 0x197 && b.state == 6);
    const shp::BranchAck b13 = shp::parse_branch_ack(std::span<const uint8_t>(p2981, 13));
    CHECK(b13.ok && b13.seq == 0x197 && b13.state == 0);
    CHECK(!shp::parse_branch_ack(std::span<const uint8_t>(p2981, 12)).ok);
}

void test_twin() {
    using shp::TwinVerdict;
    const shp::TwinKey off = shp::parse_twin("0");
    CHECK(off.mode == shp::TwinMode::Off && !off.asked_on && !off.bad);
    CHECK(shp::parse_twin("").mode == shp::TwinMode::Off);
    CHECK(shp::parse_twin("probe").mode == shp::TwinMode::Probe && !shp::parse_twin("probe").asked_on);
    const shp::TwinKey one = shp::parse_twin("1");  // held as probe in this build
    CHECK(one.mode == shp::TwinMode::Probe && one.asked_on);
    CHECK(shp::parse_twin("yes").bad && shp::parse_twin("yes").mode == shp::TwinMode::Off);
    // Session 13: cd_seq_funcnpc_grocery 0x19D prepared for the host's Kliff, user 2 = 0xA0100318 known.
    shp::TwinFacts f;
    f.funcnpc = f.prepared = f.host_scene = f.user2_known = f.user2_mgr_ok = true;
    CHECK(shp::decide_twin(f) == TwinVerdict::Would);
    shp::TwinFacts g = f;
    g.already = true;
    CHECK(shp::decide_twin(g) == TwinVerdict::Already);
    g = f;
    g.user2_mgr_ok = false;
    CHECK(shp::decide_twin(g) == TwinVerdict::NoUser2Manager);
    g = f;
    g.user2_known = false;
    CHECK(shp::decide_twin(g) == TwinVerdict::NoUser2);
    g = f;
    g.host_scene = false;  // e.g. a scene of user 2's own character while in its own field
    CHECK(shp::decide_twin(g) == TwinVerdict::NotHostScene);
    g = f;
    g.prepared = false;
    CHECK(shp::decide_twin(g) == TwinVerdict::Failed);
    g = f;
    g.funcnpc = false;
    CHECK(shp::decide_twin(g) == TwinVerdict::NotFuncnpc);
    for (size_t k = 0; k < static_cast<size_t>(TwinVerdict::Count); ++k) {
        CHECK(std::string_view(shp::twin_verdict_name(static_cast<TwinVerdict>(k))) != "?");
    }
}

}  // namespace

int main() {
    test_twin();
    test_hash();
    test_find();
    test_book();
    test_points();
    test_check_x();
    test_decide();
    test_mode();
    test_slots();
    test_call();
    test_keys();
    test_who();
    test_names();
    test_packets();
    if (g_failures) {
        std::printf("%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("optd_shop_c1: all checks passed\n");
    return 0;
}
