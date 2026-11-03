// P8 guest_schedule_load (src/mod/session2/guest_sched_load_logic.h; docs/research/pendientes/p8-impl-horarios.md).
//
// SCOPE: offline logic only:
//  - the key's values;
//  - the name -> key hash: lookup3 hashlittle (its published vectors) and 0x1422F90's base name (directory and
//    extension cut, 0x104-character cap), with the game's initval 0xC5EDE;
//  - the schedule-context table replica (mgr+0x10), its key walk and the entry's String read, on a fake memory;
//  - the key set and the keys a 2005 (NPC records only, session-6 records of tests/data), a 2335 (session-6 hex)
//    or a 2874 (built from the handler's read order) names, malformed packets, the per-packet cap;
//  - the per-key book (also claimed from several threads at once), the retry set, the budget;
//  - gsl::process_key / process_retry (the NetworkThread's whole decision) on a STUB game (StubEnv: container,
//    context table, loader result and duration, clock): gates that touch nothing, present / unloading, NoCtx not
//    latched, BadName latched, the context changing before the call, probe, the slow-load latch and the histogram,
//    packet / second budgets and the retry set, failures and give-up, the overflow book and untracked keys, the cap;
//    session 13: the keys a packet names are never deferred by a budget (a burst of 2335s and the 2005 of the same
//    second all load before delivery), the time budgets of the retry set, and what counts as LATE.
// They do NOT test the hooks, the loader call 0x1864140, the NetworkThread, the file system nor the game.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <algorithm>
#include <string>
#include <thread>
#include <vector>

#include "mod/session2/guest_sched_load_logic.h"

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

std::vector<uint8_t> load(const char* name) {
    const std::string path = std::string(CDMP_SOURCE_DIR) + "/tests/data/" + name;
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> v((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (v.empty()) {
        std::printf("FAIL cannot read %s\n", path.c_str());
        ++g_failures;
    }
    return v;
}

uint32_t hl(const char* s, uint32_t init) {
    return gsl::hashlittle(reinterpret_cast<const uint8_t*>(s), std::strlen(s), init);
}

// A sparse fake memory (reads outside the map fail like a guarded fault).
struct FakeMem {
    std::map<uintptr_t, uint8_t> b;
    void put(uintptr_t a, const void* src, size_t n) {
        const auto* s = static_cast<const uint8_t*>(src);
        for (size_t i = 0; i < n; ++i) b[a + i] = s[i];
    }
    void p32(uintptr_t a, uint32_t v) { put(a, &v, 4); }
    void p64(uintptr_t a, uint64_t v) { put(a, &v, 8); }
    void str(uintptr_t a, const char* s) { put(a, s, std::strlen(s) + 1); }
    template <class T>
    bool rd(uintptr_t a, T& v) const {
        uint8_t tmp[sizeof(T)];
        for (size_t i = 0; i < sizeof(T); ++i) {
            auto it = b.find(a + i);
            if (it == b.end()) return false;
            tmp[i] = it->second;
        }
        std::memcpy(&v, tmp, sizeof(T));
        return true;
    }
    bool u8(uintptr_t a, uint8_t& v) const { return rd(a, v); }
    bool u32(uintptr_t a, uint32_t& v) const { return rd(a, v); }
    bool ptr(uintptr_t a, uintptr_t& v) const { return rd(a, v); }
};

// ---------------------------------------------------------------- the key's values

void test_mode() {
    bool bad = true;
    CHECK(gsl::parse_mode("", &bad) == gsl::Mode::Off && !bad);
    CHECK(gsl::parse_mode("0", &bad) == gsl::Mode::Off && !bad);
    CHECK(gsl::parse_mode("probe", &bad) == gsl::Mode::Probe && !bad);
    CHECK(gsl::parse_mode("1", &bad) == gsl::Mode::On && !bad);
    CHECK(gsl::parse_mode("yes", &bad) == gsl::Mode::Off && bad);
    CHECK(gsl::parse_mode("Probe", &bad) == gsl::Mode::Off && bad);
}

// ---------------------------------------------------------------- the hash

void test_hash() {
    // lookup3.c's own self-test values (driver5).
    CHECK(gsl::hashlittle(nullptr, 0, 0) == 0xDEADBEEFu);
    CHECK(gsl::hashlittle(nullptr, 0, 0xDEADBEEFu) == 0xBD5B7DDEu);
    CHECK(hl("Four score and seven years ago", 0) == 0x17770551u);
    CHECK(hl("Four score and seven years ago", 1) == 0xCD628161u);
    // 0x1364780's seed: a = b = c = 0xDEADBEEF + len + 0xC5EDE = len - 0x2145E233 (lea ebx,[rdx-0x2145E233]).
    static_assert(0xDEADBEEFu + gsl::kNameHashInit == static_cast<uint32_t>(-0x2145E233));
    CHECK(gsl::hashlittle(nullptr, 0, gsl::kNameHashInit) == 0xDEADBEEFu + gsl::kNameHashInit);
    // Every tail length (0..12) and the 12-byte loop give distinct, stable values (no read past the end).
    const char* s = "abcdefghijklmnopqrstuvwxyz";
    std::vector<uint32_t> seen;
    for (size_t n = 0; n <= 26; ++n) {
        const uint32_t h = gsl::hashlittle(reinterpret_cast<const uint8_t*>(s), n, gsl::kNameHashInit);
        CHECK(std::find(seen.begin(), seen.end(), h) == seen.end());
        seen.push_back(h);
    }
}

void test_base_name() {
    CHECK(gsl::base_without_ext("sequencer/binary__/npc_abc.paschedule") == "npc_abc");
    CHECK(gsl::base_without_ext("a\\b/c\\npc_abc.paschedule") == "npc_abc");
    CHECK(gsl::base_without_ext("npc_abc") == "npc_abc");
    CHECK(gsl::base_without_ext("a.b.c") == "a.b");  // the last '.'
    CHECK(gsl::base_without_ext("dir.x/name") == "name");
    CHECK(gsl::base_without_ext("") == "");
    CHECK(gsl::base_without_ext("dir/") == "");
    // The key is the hash of that base: the directory and the extension do not change it.
    const uint32_t k = gsl::schedule_key_of_name("npc_abc");
    CHECK(k == hl("npc_abc", gsl::kNameHashInit));
    CHECK(gsl::schedule_key_of_name("sequencer/binary__/npc_abc.paschedule") == k);
    CHECK(gsl::schedule_key_of_name("npc_abc.other") == k);
    CHECK(gsl::schedule_key_of_name("npc_abd") != k);
    // strncpy_s into 0x105 bytes with _TRUNCATE: at most 0x104 characters (before the '.' cut).
    std::string longname(0x110, 'x');
    CHECK(gsl::base_without_ext(longname).size() == gsl::kBaseNameMax);
    std::string dotted = std::string(0x100, 'y') + ".abcdefgh";
    CHECK(gsl::base_without_ext(dotted).size() == 0x100);
}

// ---------------------------------------------------------------- the context table

constexpr uintptr_t kMgr = 0x100000, kArr = 0x200000, kEnt = 0x300000;
constexpr uint32_t kBk = 4;

// mgr with a context table of `names` (key = the name's hash), and entries at kEnt + 0x1000 * i.
FakeMem make_ctx(const std::vector<const char*>& names, std::vector<uint32_t>* keys = nullptr) {
    using L = gsl::Layout;
    FakeMem m;
    m.p32(kMgr + L::kCtx_Buckets, kBk);
    m.p32(kMgr + L::kCtx_Used, static_cast<uint32_t>(names.size()));
    m.p64(kMgr + L::kCtx_Array, kArr);
    m.p64(kMgr + L::kCtx_Entries, kEnt);
    uint32_t count[kBk] = {};
    for (uint32_t b = 0; b < kBk; ++b) m.p32(kArr + b * L::kBucketSize, 0);
    for (size_t i = 0; i < names.size(); ++i) {
        const uint32_t key = gsl::schedule_key_of_name(names[i]);
        if (keys) keys->push_back(key);
        const uint32_t b = key % kBk;
        const uintptr_t bucket = kArr + b * L::kBucketSize;
        m.p32(bucket + 8 + 8 * count[b], key);
        m.p32(bucket + 0xC + 8 * count[b], static_cast<uint32_t>(i));
        ++count[b];
        m.p32(bucket, count[b]);
        const uintptr_t entry = kEnt + 0x1000 * i;
        m.p64(kEnt + 0x80000 + 8 * i, entry);  // the entries array (pointers)
        m.p32(entry + L::kEntry_Key, key);
        const uintptr_t sd = entry + 0x100, chars = entry + 0x200;
        m.p64(entry + L::kEntry_Name, sd);
        m.p64(sd, chars);
        m.str(chars, names[i]);
    }
    m.p64(kMgr + L::kCtx_Entries, kEnt + 0x80000);
    return m;
}

void test_ctx() {
    std::vector<uint32_t> keys;
    const std::vector<const char*> names = {"npc_town_maid.paschedule", "npc_kids_01.paschedule", "sheep_pen_c",
                                            "sub/guard_patrol.paschedule"};
    FakeMem m = make_ctx(names, &keys);
    for (size_t i = 0; i < names.size(); ++i) {
        const uintptr_t e = gsl::ctx_find(m, kMgr, keys[i]);
        CHECK(e == kEnt + 0x1000 * i);
        char buf[64];
        const gsl::NameRead n = gsl::read_name(m, e, buf, sizeof buf);
        CHECK(n.ok && std::string_view(buf, n.len) == names[i]);
        CHECK(gsl::schedule_key_of_name(std::string_view(buf, n.len)) == keys[i]);
    }
    CHECK(gsl::ctx_find(m, kMgr, 0x12345678) == 0);  // absent
    CHECK(gsl::ctx_find(m, kMgr, 0) == 0);
    CHECK(gsl::ctx_find(m, 0, keys[0]) == 0);
    // The walk: every key, sorted, once.
    std::vector<uint32_t> walk;
    CHECK(gsl::ctx_keys(m, kMgr, walk));
    std::vector<uint32_t> want = keys;
    std::sort(want.begin(), want.end());
    CHECK(walk == want);
    // A name too long for the buffer, an unreadable String, an empty name: not ok.
    char small[8];
    CHECK(!gsl::read_name(m, kEnt, small, sizeof small).ok);
    FakeMem broken = m;
    broken.b.erase(kEnt + 0x100);  // the StringData* pointer's first byte
    char buf[64];
    CHECK(!gsl::read_name(broken, kEnt, buf, sizeof buf).ok);
    FakeMem empty = make_ctx({""});
    std::vector<uint32_t> k2;
    char b2[16];
    const uintptr_t e0 = gsl::ctx_find(empty, kMgr, gsl::schedule_key_of_name(""));
    CHECK(e0 && !gsl::read_name(empty, e0, b2, sizeof b2).ok);
    // An entry whose own key differs from the bucket's (a torn / reused slot) is not returned.
    FakeMem torn = m;
    torn.p32(kEnt + gsl::Layout::kEntry_Key, 0xDEAD0000);
    CHECK(gsl::ctx_find(torn, kMgr, keys[0]) == 0);
    // Empty / unreadable tables.
    FakeMem none;
    CHECK(gsl::ctx_find(none, kMgr, keys[0]) == 0);
    std::vector<uint32_t> w2;
    CHECK(!gsl::ctx_keys(none, kMgr, w2));
    FakeMem zero = m;
    zero.p32(kMgr + gsl::Layout::kCtx_Used, 0);
    CHECK(gsl::ctx_find(zero, kMgr, keys[0]) == 0);
    FakeMem huge = m;
    huge.p32(kMgr + gsl::Layout::kCtx_Buckets, gsl::Layout::kMaxBuckets + 1);
    CHECK(gsl::ctx_find(huge, kMgr, keys[0]) == 0);
}

// ---------------------------------------------------------------- keys of packets

const char* k2335_1609 =
    "1f09ff5e0004010010a0ba440f00bbb300ac00140000200000000000000000000000006ae626c6453a1a447f5270c500000000000000"
    "00000000000000803f0000803f0000803f0000803f1c5c27c6fc1b1a44ef6d73c534d126c63c151b443fe66fc5";
const char* k2335_2391 =
    "1f09ff5e0004010010a05f500f00d0158bda00590000200000000000000000000000007e0124c63d651544137074c50000000000000000"
    "000000000000803f0000803f0000803f0000803fd86926c66ed00c4401f77ec5c02323c6864316445b266fc5";

std::vector<uint8_t> packet(uint16_t op, const std::vector<uint8_t>& pl) {
    std::vector<uint8_t> p = {static_cast<uint8_t>(op), static_cast<uint8_t>(op >> 8), 0xFF,
                              static_cast<uint8_t>(pl.size()), static_cast<uint8_t>(pl.size() >> 8)};
    p.insert(p.end(), pl.begin(), pl.end());
    return p;
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
// A 2005 record: u16 len | u8 kind | u32 key | body.
void add_record(std::vector<uint8_t>& pl, uint8_t kind, uint32_t key, const std::vector<uint8_t>& body) {
    const size_t n = 5 + body.size();
    pl.push_back(static_cast<uint8_t>(n));
    pl.push_back(static_cast<uint8_t>(n >> 8));
    pl.push_back(kind);
    put32(pl, key);
    pl.insert(pl.end(), body.begin(), body.end());
}

void test_packet_keys() {
    // 2335: the resource at +9.
    const auto a = gsl::packet_keys(unhex(k2335_1609), nullptr);
    CHECK(a.ok && a.op == 2335 && a.n == 1 && a.keys[0] == 0xAC00B3BB);
    const auto b = gsl::packet_keys(unhex(k2335_2391), nullptr);
    CHECK(b.ok && b.n == 1 && b.keys[0] == 0xDA8B15D0);
    auto shortp = unhex(k2335_1609);
    shortp.resize(5 + 21);
    CHECK(!gsl::packet_keys(shortp, nullptr).ok);
    // 2874 (0xC0CE50's read order): u32 actor | u32 NPC | u32 resource | u32 node | u64 scene | 10 x 4 x u8.
    std::vector<uint8_t> i2874;
    put32(i2874, 0xA0100001);
    put32(i2874, 0xB0100083);
    put32(i2874, 0xDA8B15D0);
    put32(i2874, 0x20000004);
    put32(i2874, 0x20000059);
    put32(i2874, 0);
    i2874.resize(64, 0);
    const auto c = gsl::packet_keys(packet(2874, i2874), nullptr);
    CHECK(c.ok && c.n == 1 && c.keys[0] == 0xDA8B15D0);
    std::vector<uint8_t> zero_res = i2874;
    zero_res[8] = zero_res[9] = zero_res[10] = zero_res[11] = 0;
    const auto z = gsl::packet_keys(packet(2874, zero_res), nullptr);
    CHECK(z.ok && z.n == 0);  // resource 0: nothing to load
    // Other opcodes and runts: nothing.
    CHECK(!gsl::packet_keys(packet(2661, i2874), nullptr).ok);
    const uint8_t runt[] = {0xD5, 0x07, 0xFF};
    CHECK(!gsl::packet_keys(runt, nullptr).ok);

    // 2005, session 6: Damiane (B010006D, AI block resource 0xC4E5A00C at record +0x2241).
    const gsl::KeySet set({0xC4E5A00C, 0xDA8B15D0, 0xAC00B3BB});
    const auto damiane = load("p8s6_2005_1571.bin");
    const auto d = gsl::packet_keys(damiane, &set);
    CHECK(d.ok && d.op == 2005 && d.npc_records == 1 && d.n == 1 && d.keys[0] == 0xC4E5A00C && d.hits >= 1);
    const auto d0 = gsl::packet_keys(damiane, nullptr);  // no key set yet: nothing scanned
    CHECK(d0.ok && d0.n == 0 && d0.npc_records == 0);
    const gsl::KeySet empty_set;
    CHECK(gsl::packet_keys(damiane, &empty_set).n == 0);
    // B0100083 (scene 0x20000059): its resource twice in the record (two blocks), one key.
    const auto b83 = gsl::packet_keys(load("p8s6_2005_38259.bin"), &set);
    CHECK(b83.ok && b83.n == 1 && b83.keys[0] == 0xDA8B15D0 && b83.hits >= 2);
    // #785: an NPC, a user and a character record: only the NPC one is scanned.
    const auto many = gsl::packet_keys(load("p8s6_2005_785.bin"), &set);
    CHECK(many.ok && many.npc_records == 1);
    // Truncated single record: nothing whole before the break -> not ok, counted as untiled.
    auto trunc = damiane;
    trunc.pop_back();
    const auto tr = gsl::packet_keys(trunc, &set);
    CHECK(!tr.ok && tr.untiled && tr.n == 0);

    // Synthetic: the key in a character record is not taken; the NPC record's own key is never a hit.
    std::vector<uint8_t> pl;
    std::vector<uint8_t> body_c, body_n;
    put32(body_c, 0xAC00B3BB);
    put32(body_n, 0x00000000);
    put32(body_n, 0xDA8B15D0);
    add_record(pl, 1, 0xA0100001, body_c);
    add_record(pl, 3, 0xB0100001, body_n);
    const gsl::KeySet with_own({0xDA8B15D0, 0xB0100001});
    const auto s = gsl::packet_keys(packet(2005, pl), &with_own);
    CHECK(s.ok && s.npc_records == 1 && s.n == 1 && s.keys[0] == 0xDA8B15D0);
    // Review 11: a whole record before a broken one keeps its keys (ok, untiled).
    std::vector<uint8_t> broken = pl;
    broken.push_back(0xFF);  // a length byte with nothing after it
    broken.push_back(0x7F);
    const auto pb = gsl::packet_keys(packet(2005, broken), &with_own);
    CHECK(pb.ok && pb.untiled && pb.n == 1 && pb.keys[0] == 0xDA8B15D0);
    // Review 10: kMaxKeysPerPacket + 22 distinct keys: the cap in the packet, the 22 others in the spill set (none
    // lost); 40 fit whole.
    std::vector<uint32_t> many_keys;
    std::vector<uint8_t> body_many, plm;
    for (uint32_t i = 0; i < gsl::kMaxKeysPerPacket + 22; ++i) {
        many_keys.push_back(0x51000000 + i * 0x10001);
        put32(body_many, many_keys.back());
    }
    add_record(plm, 3, 0xB0100002, body_many);
    const gsl::KeySet big(many_keys);
    gsl::Spill spill;
    const auto cap = gsl::packet_keys(packet(2005, plm), &big, &spill);
    CHECK(cap.ok && cap.n == gsl::kMaxKeysPerPacket && cap.spilled == 22 &&
          cap.hits == gsl::kMaxKeysPerPacket + 22 && spill.size() == 22);
    const auto again = gsl::packet_keys(packet(2005, plm), &big, &spill);
    CHECK(again.spilled == 0 && spill.size() == 22);  // already kept: not added twice
    const auto nospill = gsl::packet_keys(packet(2005, plm), &big);
    CHECK(nospill.n == gsl::kMaxKeysPerPacket && nospill.spilled == 0);
    std::vector<uint8_t> body40, pl40;
    for (uint32_t i = 0; i < 40; ++i) put32(body40, many_keys[i]);
    add_record(pl40, 3, 0xB0100003, body40);
    CHECK(gsl::packet_keys(packet(2005, pl40), &big).n == 40);
}

void test_retry_set() {
    gsl::RetrySet<4> r;
    CHECK(!r.add(0) && r.add(1) && !r.add(1) && r.add(2) && r.add(3) && r.add(4));
    CHECK(!r.add(5) && r.dropped() == 1 && r.size() == 4);
    CHECK(r.remove(2) && !r.remove(2) && r.size() == 3 && !r.has(2) && r.has(4));
    uint32_t out[8];
    CHECK(r.snapshot(out, 8) == 3 && r.snapshot(out, 2) == 2);
}

void test_key_set() {
    const gsl::KeySet s({5, 0x10000, 0xFFFFFFFF, 5, 0xABCD1234});
    CHECK(s.size() == 4);
    CHECK(s.has(5) && s.has(0x10000) && s.has(0xFFFFFFFF) && s.has(0xABCD1234));
    CHECK(!s.has(6) && !s.has(0x10001) && !s.has(0) && !s.has(0xABCD1235));
    const gsl::KeySet e;
    CHECK(e.size() == 0 && !e.has(5));
}

// ---------------------------------------------------------------- the book, the budget, the decision

void test_book() {
    gsl::KeyBook<64> b;
    CHECK(b.get(0) == nullptr);
    auto* s1 = b.get(0xAC00B3BB);
    CHECK(s1 && b.get(0xAC00B3BB) == s1 && b.used() == 1);
    CHECK(b.get(0x11111111, false) == nullptr && b.used() == 1);  // no claim
    CHECK(b.state(s1) == gsl::KeyState::Unknown);
    gsl::KeyBook<64>::set_state(s1, gsl::KeyState::Loaded);
    CHECK(b.state(s1) == gsl::KeyState::Loaded);
    CHECK(b.state(nullptr) == gsl::KeyState::Unknown);
    // Full: no room within kMaxProbe for new keys, known keys still found.
    gsl::KeyBook<16> small;
    for (uint32_t k = 1; k <= 40; ++k) small.get(k * 7919);
    CHECK(small.used() == 16 && small.untracked() > 0);
    // Several threads claiming the same 200 keys: each key gets one slot.
    gsl::KeyBook<1024> shared;
    std::vector<std::thread> th;
    std::atomic<int> mismatches{0};
    for (int t = 0; t < 4; ++t) {
        th.emplace_back([&] {
            for (uint32_t k = 1; k <= 200; ++k) {
                auto* a = shared.get(k * 2654435761u);
                auto* again = shared.get(k * 2654435761u);
                if (!a || a != again) mismatches.fetch_add(1);
                if (a) a->wanted.fetch_add(1);
            }
        });
    }
    for (auto& x : th) x.join();
    CHECK(mismatches.load() == 0 && shared.used() == 200);
    uint64_t total = 0;
    for (size_t i = 0; i < shared.capacity(); ++i) total += shared.at(i).wanted.load();
    CHECK(total == 800);
}

void test_budget() {
    // Session 13: time only (a count no longer defers anything).
    gsl::Budget b(100'000, 1'000'000);
    CHECK(b.allow(10));
    for (int i = 0; i < 64; ++i) b.spend(20, 1'000);  // 64 loads of 1 ms: still inside 100 ms
    CHECK(b.allow(30) && b.loads_in_window() == 64 && b.us_in_window() == 64'000);
    b.spend(40, 36'000);
    CHECK(!b.allow(50));  // 100 ms spent
    CHECK(b.allow(1'000'001) && b.loads_in_window() == 0);  // next window
    b.spend(1'000'002, 150'000);  // one slow load spends the window's time
    CHECK(!b.allow(1'000'003) && b.loads_in_window() == 1);
    CHECK(b.allow(2'500'000));
    gsl::PacketBudget pk;
    CHECK(pk.allow_own() && pk.allow_retry());
    pk.us = gsl::kRetryPacketUs;
    CHECK(pk.allow_own() && !pk.allow_retry());
    pk.us = gsl::kOwnPacketCapUs;
    CHECK(!pk.allow_own() && !pk.allow_retry());
    static_assert(gsl::kRetryPacketUs < gsl::kOwnPacketCapUs && gsl::kLatchUs < gsl::kOwnPacketCapUs);
}

// ---------------------------------------------------------------- the engine with a stub game (the seam)

// The game as gsl::process_key sees it: a container (present / unloading), a context table (entry, name ok,
// generation), a loader whose result and duration each test sets, a clock and the notes.
struct StubEnv {
    std::map<uint32_t, int> container;            // key -> 1 present, 2 being unloaded
    std::set<uint32_t> sweep_ends_after_check;    // being unloaded: gone right after the next presence check
    std::map<uint32_t, gsl::CtxInfo> ctx_table;   // key -> its entry
    bool load_inserts = true, load_gets = true, load_faults = false, ctx_changes = false;
    uint64_t load_us = 2'000, clock = 1'000'000;
    int loads = 0, presence_checks = 0;
    std::vector<std::pair<gsl::Note, uint32_t>> notes;

    // The hold (session 12): handles are fake file addresses 0x7000'0000 + key's low bits. A file's refcount is
    // modelled as the game keeps it: ours (refs[]) plus 1 while the container has it (its own reference). release()
    // behaves as GameEnv's: a held handle (last_ok false) whose count is 1 (only ours) is NOT given back; a handle
    // taken in this same call (last_ok true) always is, and at count 0 the file is destroyed.
    std::map<uintptr_t, int> refs;  // handle -> references we have not given back
    std::map<uintptr_t, uint32_t> key_of;
    int releases = 0, kept_on_present = 0, kept_last = 0, destroyed = 0;
    bool module_off = false;
    gsl::HoldOwner owner{1, 0x1000, 0x2000, true};
    static uintptr_t file_of(uint32_t k) { return 0x7000'0000 + (k & 0xFFFFF) * 0x100; }
    int total_refs(uintptr_t h) const {
        auto r = refs.find(h);
        auto k = key_of.find(h);
        const bool in_container = k != key_of.end() && container.count(k->second) != 0;
        return (r == refs.end() ? 0 : r->second) + (in_container ? 1 : 0);
    }
    uintptr_t take(uint32_t k) {
        const uintptr_t h = file_of(k);
        key_of[h] = k;
        ++refs[h];
        return h;
    }
    int present(uint32_t k, uintptr_t* keep) {
        ++presence_checks;
        auto it = container.find(k);
        const int r = it == container.end() ? 0 : it->second;
        if (r == 2 && sweep_ends_after_check.erase(k)) container.erase(k);  // review L4: the sweep finishes now
        if (r == 1 && keep) {  // the native lookup gives no handle for a file being unloaded (2)
            *keep = take(k);
            ++kept_on_present;
        }
        return r;
    }
    int release(uintptr_t h, bool last_ok) {
        if (module_off) return -1;
        const int total = total_refs(h);
        if (!last_ok && total <= 1) {
            ++kept_last;
            return 0;
        }
        ++releases;
        --refs[h];
        if (total - 1 == 0) ++destroyed;
        return 1;
    }
    gsl::HoldOwner hold_owner() const { return owner; }
    int outstanding() const {
        int n = 0;
        for (auto& [h, c] : refs) n += c;
        return n;
    }
    gsl::CtxInfo ctx(uint32_t k) {
        auto it = ctx_table.find(k);
        return it == ctx_table.end() ? gsl::CtxInfo{} : it->second;
    }
    int ctx_changes_once = 0;  // the next N re-checks see a changed table
    bool ctx_still(uint32_t, const gsl::CtxInfo&) {
        if (ctx_changes_once > 0) {
            --ctx_changes_once;
            return false;
        }
        return !ctx_changes;
    }
    gsl::LoadOutcome load(const gsl::CtxInfo& c, bool keep) {
        gsl::LoadOutcome o;
        if (load_faults) return o;
        ++loads;
        clock += load_us;
        o.called = true;
        o.got = load_gets;
        o.us = load_us;
        uint32_t key = 0;
        for (auto& [k, e] : ctx_table) {
            if (e.entry == c.entry) key = k;
        }
        if (load_inserts && key) container[key] = 1;
        if (keep && o.got) o.kept = take(key);
        return o;
    }
    uint64_t now() { return clock; }
    struct LateNote {
        uint32_t key;
        uint16_t op;
        uint64_t a, b;
    };
    std::vector<LateNote> lates;  // Note::Late (session 13): a = Why, b = KeyState (or the spilled count)
    void note(gsl::Note n, uint32_t k, uint16_t op, uint64_t a, uint64_t b) {
        notes.emplace_back(n, k);
        if (n == gsl::Note::Late) lates.push_back({k, op, a, b});
    }
    size_t count(gsl::Note n) const {
        return static_cast<size_t>(std::count_if(notes.begin(), notes.end(), [&](auto& x) { return x.first == n; }));
    }
    void add_ctx(uint32_t k, bool name_ok = true) {
        gsl::CtxInfo c;
        c.entry = 0x300000 + k % 0x10000 * 0x100;
        c.name_ok = name_ok;
        c.name_key = name_ok ? k : k ^ 1;
        ctx_table[k] = c;
    }
};

gsl::Gates open_gates(gsl::Mode m = gsl::Mode::On) {
    gsl::Gates g;
    g.mode = m;
    g.code_ok = g.client_realm = g.remote_live = g.manager_ok = true;
    return g;
}

void test_engine_gates() {
    using W = gsl::Why;
    auto eng = std::make_unique<gsl::Engine<64>>();
    StubEnv env;
    env.add_ctx(0xAC00B3BB);
    gsl::PacketBudget pk;
    gsl::Gates g = open_gates();
    g.mode = gsl::Mode::Off;
    CHECK(gsl::process_key(*eng, env, g, 0xAC00B3BB, 2005, pk) == W::Off);
    g = open_gates();
    g.code_ok = false;
    CHECK(gsl::process_key(*eng, env, g, 0xAC00B3BB, 2005, pk) == W::CodeOff);
    g = open_gates();
    g.client_realm = false;
    g.remote_live = false;
    CHECK(gsl::process_key(*eng, env, g, 0xAC00B3BB, 2005, pk) == W::ServerRealm);
    g.client_realm = true;
    CHECK(gsl::process_key(*eng, env, g, 0xAC00B3BB, 2005, pk) == W::NotRemote);
    g = open_gates();
    g.manager_ok = false;
    env.container[0xAC00B3BB] = 1;
    CHECK(gsl::process_key(*eng, env, g, 0xAC00B3BB, 2005, pk) == W::NoManager);
    // Review 4: a failed gate touches nothing: no book slot, no presence check, no budget, no retry, no load.
    CHECK(eng->book.used() == 0 && env.presence_checks == 0 && env.loads == 0 && eng->retry.size() == 0);
    CHECK(eng->window.loads_in_window() == 0);
    CHECK(env.count(gsl::Note::Gate) == 2);  // ServerRealm and NoManager are noted
    CHECK(gsl::process_retry(*eng, env, g, pk) == 0);
}

void test_engine_load() {
    using W = gsl::Why;
    using S = gsl::KeyState;
    auto eng = std::make_unique<gsl::Engine<64>>();
    StubEnv env;
    env.add_ctx(0xAC00B3BB);
    env.add_ctx(0xF56759DA);
    gsl::PacketBudget pk;
    const auto g = open_gates();
    // Present: never loaded.
    env.container[0x3F8972FE] = 1;
    CHECK(gsl::process_key(*eng, env, g, 0x3F8972FE, 2005, pk) == W::Present);
    CHECK(eng->book.state(eng->book.get(0x3F8972FE, false)) == S::Present && env.loads == 0);
    // Review L4: being unloaded is NOT present: not loaded over it, kept in the retry set, the state left as it was.
    {
        auto eu = std::make_unique<gsl::Engine<64>>();
        StubEnv su;
        su.container[0x6FF7E5A6] = 2;
        su.add_ctx(0x6FF7E5A6);
        gsl::PacketBudget pu;
        CHECK(gsl::process_key(*eu, su, g, 0x6FF7E5A6, 2005, pu) == W::Unloading);
        CHECK(eu->book.state(eu->book.get(0x6FF7E5A6, false)) == S::Unknown && su.loads == 0);
        CHECK(eu->retry.has(0x6FF7E5A6) && eu->unloading.load() == 1);
        su.container.erase(0x6FF7E5A6);  // the sweep removed it: the retry loads it
        gsl::PacketBudget pu2;
        CHECK(gsl::process_retry(*eu, su, g, pu2) == 1 && su.loads == 1 && !eu->retry.has(0x6FF7E5A6));
        CHECK(eu->book.state(eu->book.get(0x6FF7E5A6, false)) == S::Loaded);
    }
    // A load: LOADED, the histogram, the budgets, the note.
    CHECK(gsl::process_key(*eng, env, g, 0xAC00B3BB, 2005, pk) == W::Load);
    auto* s = eng->book.get(0xAC00B3BB, false);
    CHECK(s && eng->book.state(s) == S::Loaded && env.loads == 1 && eng->loads_ok.load() == 1);
    CHECK(pk.loads == 1 && pk.us == 2'000 && eng->window.loads_in_window() == 1);
    CHECK(eng->hist.n[gsl::Histogram::bucket(2'000)].load() == 1 && eng->hist.max_us.load() == 2'000);
    CHECK(env.count(gsl::Note::Load) == 1 && !eng->latched.load());
    // Named again: present; ours stays LOADED (the verification counts on it).
    CHECK(gsl::process_key(*eng, env, g, 0xAC00B3BB, 2335, pk) == W::Present && eng->book.state(s) == S::Loaded);
    CHECK(env.loads == 1 && s->wanted.load() == 2);
    // A fault inside the call: CodeOff, nothing counted as loaded.
    env.load_faults = true;
    CHECK(gsl::process_key(*eng, env, g, 0xF56759DA, 2005, pk) == W::CodeOff && eng->loads_ok.load() == 1);
}

void test_engine_ctx() {
    using W = gsl::Why;
    using S = gsl::KeyState;
    auto eng = std::make_unique<gsl::Engine<64>>();
    StubEnv env;
    gsl::PacketBudget pk;
    const auto g = open_gates();
    // Review 12: NoCtx is not latched: noted once, decided again, loaded once the entry appears.
    CHECK(gsl::process_key(*eng, env, g, 0x11111111, 2005, pk) == W::NoCtx);
    CHECK(gsl::process_key(*eng, env, g, 0x11111111, 2005, pk) == W::NoCtx);
    CHECK(env.count(gsl::Note::NoCtx) == 1);
    env.add_ctx(0x11111111);
    CHECK(gsl::process_key(*eng, env, g, 0x11111111, 2005, pk) == W::Load);
    // BadName is latched (and never loaded).
    env.add_ctx(0x22222222, false);
    CHECK(gsl::process_key(*eng, env, g, 0x22222222, 2005, pk) == W::BadName);
    env.add_ctx(0x22222222, true);
    CHECK(gsl::process_key(*eng, env, g, 0x22222222, 2005, pk) == W::Decided);
    CHECK(eng->book.state(eng->book.get(0x22222222, false)) == S::BadName && env.loads == 1);
    // Review 5: the context changed right before the call: not called, kept in the retry set.
    env.add_ctx(0x33333333);
    env.ctx_changes = true;
    CHECK(gsl::process_key(*eng, env, g, 0x33333333, 2005, pk) == W::CtxChanged);
    CHECK(env.loads == 1 && eng->retry.has(0x33333333));
    env.ctx_changes = false;
    CHECK(gsl::process_retry(*eng, env, g, pk) == 1 && env.loads == 2 && !eng->retry.has(0x33333333));
}

void test_engine_probe_and_latch() {
    using W = gsl::Why;
    auto eng = std::make_unique<gsl::Engine<64>>();
    StubEnv env;
    env.add_ctx(1);
    env.add_ctx(2);
    env.add_ctx(3);
    gsl::PacketBudget pk;
    // probe: decided once, never called, spends nothing.
    CHECK(gsl::process_key(*eng, env, open_gates(gsl::Mode::Probe), 1, 2005, pk) == W::WouldLoad);
    CHECK(gsl::process_key(*eng, env, open_gates(gsl::Mode::Probe), 1, 2005, pk) == W::Decided);
    CHECK(env.loads == 0 && pk.loads == 0 && env.count(gsl::Note::Would) == 1);
    // ON: a would-load key loads.
    CHECK(gsl::process_key(*eng, env, open_gates(), 1, 2005, pk) == W::Load && env.loads == 1);
    // Review 3: one load over 250 ms latches probe for the session (one note), the next key is only decided.
    env.load_us = 300'000;
    CHECK(gsl::process_key(*eng, env, open_gates(), 2, 2005, pk) == W::Load);
    CHECK(eng->latched.load() && env.count(gsl::Note::Latch) == 1 && eng->slow.load() == 1);
    CHECK(eng->hist.n[gsl::Histogram::kBuckets - 1].load() == 1 && eng->hist.max_us.load() == 300'000);
    CHECK(gsl::process_key(*eng, env, open_gates(), 3, 2005, pk) == W::WouldLoad && env.loads == 2);
    CHECK(gsl::Histogram::bucket(999) == 0 && gsl::Histogram::bucket(1'000) == 1 && gsl::Histogram::bucket(250'000) == 6);
}

void test_engine_budget_and_failures() {
    using W = gsl::Why;
    using S = gsl::KeyState;
    auto eng = std::make_unique<gsl::Engine<64>>();
    StubEnv env;
    for (uint32_t k = 1; k <= 40; ++k) env.add_ctx(k);
    const auto g = open_gates();
    // Session 13: the keys a packet names are never deferred by a count: 20 in one packet, all loaded.
    gsl::PacketBudget pk;
    uint32_t loaded = 0;
    for (uint32_t k = 1; k <= 20; ++k) loaded += gsl::process_key(*eng, env, g, k, 2005, pk) == W::Load;
    CHECK(loaded == 20 && eng->retry.size() == 0 && pk.loads == 20 && pk.us == 40'000);
    // Nor by the second's window (40 ms + 20 more packets in the same second, 2 ms each): all loaded.
    for (uint32_t k = 21; k <= 40; ++k) {
        gsl::PacketBudget p;
        CHECK(gsl::process_key(*eng, env, g, k, 2005, p) == W::Load);
    }
    CHECK(env.loads == 40 && eng->retry.size() == 0 && eng->why[static_cast<size_t>(W::Budget)].load() == 0);
    // The retry set's keys (op 0) ARE gated by time: after kRetryPacketUs of loads in this packet, or once the
    // window's kWindowLoadUs is spent, they stay there for a later delivery.
    env.add_ctx(41);
    env.add_ctx(42);
    eng->retry.add(41);
    eng->retry.add(42);
    gsl::PacketBudget busy;
    busy.us = gsl::kRetryPacketUs;
    CHECK(gsl::process_retry(*eng, env, g, busy) == 2 && eng->retry.size() == 2 && env.loads == 40);
    CHECK(eng->why[static_cast<size_t>(W::Budget)].load() == 2);
    env.load_us = 150'000;  // a slow (not latching) load spends most of this second's window
    gsl::PacketBudget slow1;
    CHECK(gsl::process_key(*eng, env, g, 41, 0, slow1) == W::Load && !eng->latched.load());
    gsl::PacketBudget fresh;
    CHECK(gsl::process_retry(*eng, env, g, fresh) == 1 && eng->retry.has(42) && env.loads == 41);  // window spent
    env.load_us = 2'000;
    env.clock += gsl::kWindowUs;
    gsl::PacketBudget p3;
    gsl::process_retry(*eng, env, g, p3);
    CHECK(eng->retry.size() == 0 && env.loads == 42);
    // Failures: kept for a retry, given up after kMaxAttempts.
    env.add_ctx(77);
    env.load_inserts = false;
    env.clock += gsl::kWindowUs;
    for (int i = 0; i < 3; ++i) {
        gsl::PacketBudget p;
        CHECK(gsl::process_key(*eng, env, g, 77, 2005, p) == W::Load);
    }
    auto* s = eng->book.get(77, false);
    CHECK(s && eng->book.state(s) == S::Failed && s->attempts.load() == gsl::kMaxAttempts && !eng->retry.has(77));
    gsl::PacketBudget p4;
    CHECK(gsl::process_key(*eng, env, g, 77, 2005, p4) == W::GaveUp && eng->loads_failed.load() == 3);
    // A handle without the file in the container is a failure too.
    env.load_inserts = true;
    env.load_gets = false;
    env.add_ctx(78);
    CHECK(gsl::process_key(*eng, env, g, 78, 2005, p4) == W::Load);
    CHECK(eng->book.state(eng->book.get(78, false)) == S::Failed && eng->retry.has(78));
}

void test_engine_untracked() {
    using W = gsl::Why;
    // A 16-slot book: keys beyond it go to the overflow book (state and attempts kept there).
    auto eng = std::make_unique<gsl::Engine<16>>();
    StubEnv env;
    env.load_inserts = false;  // every load fails
    const auto g = open_gates();
    uint32_t key = 0x1000;
    for (int i = 0; i < 16; ++i) eng->book.get(key + i);  // fill the book
    CHECK(eng->book.used() == 16);
    env.add_ctx(0x9000);
    for (int i = 0; i < 3; ++i) {
        env.clock += gsl::kWindowUs;
        gsl::PacketBudget p;
        CHECK(gsl::process_key(*eng, env, g, 0x9000, 2005, p) == W::Load);
    }
    gsl::PacketBudget p;
    CHECK(gsl::process_key(*eng, env, g, 0x9000, 2005, p) == W::GaveUp && eng->overflow.used() == 1);
    // Overflow book full too: the shared failure counter stops retries after kUntrackedFailures.
    for (uint32_t i = 1; i < 256; ++i) eng->overflow.get(0xA000 + i);
    CHECK(eng->overflow.used() == 256);
    uint32_t calls = 0;
    for (uint32_t i = 0; i < 40; ++i) {
        env.add_ctx(0xB000 + i);
        env.clock += gsl::kWindowUs;
        gsl::PacketBudget q;
        if (gsl::process_key(*eng, env, g, 0xB000 + i, 2005, q) == W::Load) ++calls;
    }
    CHECK(calls == gsl::kUntrackedFailures && eng->untracked_failures.load() == gsl::kUntrackedFailures);
}

void test_engine_cap() {
    using W = gsl::Why;
    auto eng = std::make_unique<gsl::Engine<1024>>();
    StubEnv env;
    const auto g = open_gates();
    eng->loads_total.store(gsl::kSessionCap);
    env.add_ctx(5);
    gsl::PacketBudget p;
    CHECK(gsl::process_key(*eng, env, g, 5, 2005, p) == W::Cap && env.loads == 0);
    for (size_t i = 0; i < static_cast<size_t>(W::Count); ++i) CHECK(std::string(gsl::why_name(static_cast<W>(i))) != "?");
    for (size_t i = 0; i < static_cast<size_t>(gsl::KeyState::Count); ++i) {
        CHECK(std::string(gsl::state_name(static_cast<gsl::KeyState>(i))) != "?");
    }
    CHECK(gsl::state_after_load(true, true) == gsl::KeyState::Loaded);
    CHECK(gsl::state_after_load(true, false) == gsl::KeyState::Failed);
    CHECK(gsl::state_after_load(false, true) == gsl::KeyState::Failed);
}

// Session 12 (p8-impl-horarios.md §10): the handle of a file a 2335 names is kept until that 2335 ran.
void test_hold_due() {
    using H = gsl::HoldEnd;
    H why = H::Count;
    const uint64_t t = 100'000'000;
    CHECK(!gsl::hold_due(t, 0, t + 1'000'000, why));                       // not seen, young
    CHECK(gsl::hold_due(t, 0, t + gsl::kHoldMaxUs, why) && why == H::Timeout);
    CHECK(!gsl::hold_due(t, t + 3'200'000, t + 3'200'000 + gsl::kHoldGraceUs - 1, why));
    CHECK(gsl::hold_due(t, t + 3'200'000, t + 3'200'000 + gsl::kHoldGraceUs, why) && why == H::Seen);
    // A lookup BEFORE the last naming does not count (a new 2335 named it again): only the cap ends it.
    CHECK(!gsl::hold_due(t, t - 1, t + gsl::kHoldGraceUs, why));
    CHECK(gsl::hold_due(t, t - 1, t + gsl::kHoldMaxUs, why) && why == H::Timeout);

    gsl::HoldTable<4> ht;
    using A = gsl::HoldTable<4>::Add;
    const gsl::HoldOwner o1{1, 0x1000, 0x2000, true}, o2{2, 0x1000, 0x2000, true};
    CHECK(o1.same(o1) && !o1.same(o2));
    CHECK(!o1.same(gsl::HoldOwner{1, 0x1001, 0x2000, true}) && !o1.same(gsl::HoldOwner{1, 0x1000, 0x2001, true}));
    CHECK(ht.add(1, 0x10, 5, o1) == A::Held && ht.add(2, 0x20, 5, o1) == A::Held && ht.size() == 2);
    CHECK(ht.add(1, 0x11, 9, o1) == A::Refreshed && ht.size() == 2);
    CHECK(ht.add(0, 0x30, 5, o1) == A::Full && ht.add(3, 0, 5, o1) == A::Full);  // no key / no handle: never held
    CHECK(ht.add(3, 0x30, 5, o1) == A::Held && ht.add(4, 0x40, 5, o1) == A::Held && ht.add(5, 0x50, 5, o1) == A::Full);
    // Another connection: the old entry is replaced (its handle dropped by the caller), the size does not change.
    CHECK(ht.add(4, 0x41, 7, o2) == A::Replaced && ht.size() == 4);
    uint64_t named1 = 0;
    uintptr_t file4 = 0;
    CHECK(ht.sweep([&](const auto& e) {
              if (e.key == 1) named1 = e.named_us;
              if (e.key == 4) file4 = e.file;
              return e.key == 2 || e.key == 4;
          }) == 2);
    CHECK(named1 == 9 && file4 == 0x41 && ht.size() == 2 && ht.has(1) && ht.has(3) && !ht.has(2) && ht.peak() == 4);
}

void test_engine_hold() {
    using W = gsl::Why;
    using S = gsl::KeyState;
    using H = gsl::HoldEnd;
    auto eng = std::make_unique<gsl::Engine<256>>();
    StubEnv env;
    const uint32_t K = 0x2E1A6E52, J = 0xAC00B3BB, L = 0x9BEEBAD1, M = 0xFC6638C8;
    for (uint32_t k : {K, J, L, M}) env.add_ctx(k);
    const auto g = open_gates();
    gsl::PacketBudget pk;
    // A 2335 names an absent file: loaded, and the loader's handle is KEPT (no release).
    CHECK(gsl::process_key(*eng, env, g, K, 2335, pk) == W::Load);
    auto* sk = eng->book.get(K, false);
    CHECK(sk && eng->book.state(sk) == S::Loaded && sk->held.load() == 1 && sk->by2335.load() == 1);
    CHECK(eng->holds.size() == 1 && eng->holds_taken.load() == 1 && env.outstanding() == 1 && env.releases == 0);
    CHECK(eng->held_now.load() == 1 && env.count(gsl::Note::Load) == 1);
    // Named again by a 2335 while held: the lookup's new handle goes back at once, the hold is refreshed.
    env.clock += 1'000'000;
    CHECK(gsl::process_key(*eng, env, g, K, 2335, pk) == W::Present);
    CHECK(eng->holds_refreshed.load() == 1 && env.releases == 1 && env.outstanding() == 1 && eng->holds.size() == 1);
    // A 2005 (no 2335 yet): loaded and released at once, as before.
    CHECK(gsl::process_key(*eng, env, g, J, 2005, pk) == W::Load && eng->holds.size() == 1 && env.outstanding() == 1);
    // Then a 2335 names it: present, the lookup's handle is kept.
    CHECK(gsl::process_key(*eng, env, g, J, 2335, pk) == W::Present);
    CHECK(eng->holds.size() == 2 && env.kept_on_present == 2 && env.outstanding() == 2);
    auto* sj = eng->book.get(J, false);
    CHECK(sj && sj->held.load() == 1 && eng->book.state(sj) == S::Loaded);
    // Nothing is due yet; the check runs at most once a second.
    CHECK(gsl::service_holds(*eng, env, env.clock) == 0 && eng->holds.size() == 2);
    const uint64_t next = eng->next_service_us;
    CHECK(gsl::service_holds(*eng, env, env.clock + 500'000) == 0 && eng->next_service_us == next);
    // K's 2335 runs (the hook stores when 0xA9FEE0 looked it up): given back kHoldGraceUs later.
    const uint64_t ran = env.clock + 3'200'000;
    sk->stage_lookup_us.store(ran);
    CHECK(gsl::service_holds(*eng, env, ran + gsl::kHoldGraceUs - 1) == 0);
    CHECK(gsl::service_holds(*eng, env, ran + gsl::kHoldGraceUs + gsl::kHoldServiceUs) == 1);
    CHECK(!eng->holds.has(K) && sk->held.load() == 0 && sk->hold_end.load() == 1 + static_cast<int>(H::Seen));
    CHECK(eng->hold_ends[static_cast<size_t>(H::Seen)].load() == 1 && env.count(gsl::Note::HoldEnd) == 1);
    CHECK(env.outstanding() == 1 && eng->held_now.load() == 1);
    // J's 2335 never runs: TIMEOUT kHoldMaxUs after the last naming.
    const uint64_t named_j = sj->named2335_us.load();
    CHECK(gsl::service_holds(*eng, env, named_j + gsl::kHoldMaxUs + gsl::kHoldServiceUs) == 1);
    CHECK(eng->holds.size() == 0 && env.outstanding() == 0 && eng->hold_ends[static_cast<size_t>(H::Timeout)].load() == 1);
    CHECK(sj->hold_end.load() == 1 + static_cast<int>(H::Timeout));
    // The safety net: J's file is evicted after that timeout, before its 2335 ran; a 2335 names it again: counted
    // at load ("released before its 2335 ran"), loaded and held again.
    env.container.erase(J);
    env.clock = named_j + gsl::kHoldMaxUs + 5'000'000;
    gsl::PacketBudget pk2;
    CHECK(gsl::process_key(*eng, env, g, J, 2335, pk2) == W::Load);
    CHECK(eng->early.load() == 1 && env.count(gsl::Note::Early) == 1 && eng->holds.has(J));
    // K ran its 2335 before it went: not counted.
    env.container.erase(K);
    CHECK(gsl::process_key(*eng, env, g, K, 2335, pk2) == W::Load && eng->early.load() == 1 && eng->holds.has(K));
    // The container drops both (its own references; the same manager and container): ours are now their LAST
    // references -> Kept, counted, never given back (no destructor on this thread), the entries go.
    CHECK(env.outstanding() == 2 && env.total_refs(StubEnv::file_of(J)) == 2);
    env.container.erase(J);
    env.container.erase(K);
    const int rel_before = env.releases;
    CHECK(gsl::service_holds(*eng, env, env.clock + gsl::kHoldMaxUs + 10 * gsl::kHoldServiceUs, true) == 2);
    CHECK(eng->hold_ends[static_cast<size_t>(H::Kept)].load() == 2 && eng->holds.size() == 0);
    CHECK(env.kept_last == 2 && env.releases == rel_before && env.destroyed == 0 && env.outstanding() == 2);
    // A 2335 key whose load waits in the retry set (op 0 later) is still held when it loads.
    gsl::PacketBudget spent;
    spent.us = gsl::kOwnPacketCapUs;  // session 13: only the packet's own-key time cap defers a key it names
    CHECK(gsl::process_key(*eng, env, g, M, 2335, spent) == W::OwnCap && eng->retry.has(M));
    gsl::PacketBudget pk3;
    CHECK(gsl::process_retry(*eng, env, g, pk3) == 1 && eng->holds.has(M));
    // Probe: never held; a 2005-only key: never held.
    auto eng2 = std::make_unique<gsl::Engine<64>>();
    StubEnv env2;
    env2.add_ctx(L);
    env2.container[J] = 1;
    gsl::PacketBudget pk4;
    CHECK(gsl::process_key(*eng2, env2, g, J, 2005, pk4) == W::Present && eng2->holds.size() == 0);
    CHECK(gsl::process_key(*eng2, env2, open_gates(gsl::Mode::Probe), L, 2335, pk4) == W::WouldLoad);
    CHECK(gsl::process_key(*eng2, env2, open_gates(gsl::Mode::Probe), J, 2335, pk4) == W::Present);
    CHECK(eng2->holds.size() == 0 && env2.outstanding() == 0);
    // Latched to probe: no new hold either.
    eng2->latched.store(true);
    env2.container.erase(J);
    env2.add_ctx(J);
    CHECK(gsl::process_key(*eng2, env2, g, J, 2335, pk4) == W::WouldLoad && eng2->holds.size() == 0);
    // A loaded handle the container does not have afterwards (Failed): our reference is its ONLY one; given back at
    // once anyway (as before the hold): the file is destroyed, nothing is held or leaked.
    auto eng3 = std::make_unique<gsl::Engine<64>>();
    StubEnv env3;
    env3.add_ctx(K);
    env3.load_inserts = false;
    gsl::PacketBudget pk5;
    CHECK(gsl::process_key(*eng3, env3, g, K, 2335, pk5) == W::Load);
    CHECK(eng3->book.state(eng3->book.get(K, false)) == S::Failed);
    CHECK(eng3->holds.size() == 0 && env3.releases == 1 && env3.outstanding() == 0);
    CHECK(env3.destroyed == 1 && env3.kept_last == 0);
}

// Review 2997dbb: a hold belongs to one connection (epoch, still playing on the host) and one manager / container.
// Any change: dropped without a call (a counted leak), never given back in another session or world.
void test_engine_hold_owner() {
    using W = gsl::Why;
    using H = gsl::HoldEnd;
    const auto g = open_gates();
    const uint32_t K = 0x2E1A6E52, J = 0xAC00B3BB;
    auto dropped = [](auto& e) { return e->hold_ends[static_cast<size_t>(H::Dropped)].load(); };
    // The next connection (another epoch): dropped, no release.
    for (int change = 0; change < 4; ++change) {
        auto eng = std::make_unique<gsl::Engine<64>>();
        StubEnv env;
        env.add_ctx(K);
        gsl::PacketBudget pk;
        CHECK(gsl::process_key(*eng, env, g, K, 2335, pk) == W::Load && eng->holds.has(K));
        if (change == 0) env.owner.epoch = 2;
        if (change == 1) env.owner.live = false;  // the connection ended (end_remote)
        if (change == 2) env.owner.mgr = 0x1100;
        if (change == 3) env.owner.container = 0x2100;
        CHECK(gsl::service_holds(*eng, env, env.clock) == 1 && eng->holds.size() == 0 && dropped(eng) == 1);
        CHECK(env.releases == 0 && env.outstanding() == 1 && env.kept_last == 0);
        auto* s = eng->book.get(K, false);
        CHECK(s && s->held.load() == 0 && s->hold_end.load() == 1 + static_cast<int>(H::Dropped));
        CHECK(env.count(gsl::Note::HoldEnd) == 1);
    }
    // Named again in the next connection while the old hold is still in the table: the old one is replaced (dropped,
    // no release), the new handle is held and later given back normally.
    {
        auto eng = std::make_unique<gsl::Engine<64>>();
        StubEnv env;
        env.add_ctx(K);
        env.add_ctx(J);
        gsl::PacketBudget pk;
        CHECK(gsl::process_key(*eng, env, g, K, 2335, pk) == W::Load);
        CHECK(gsl::process_key(*eng, env, g, J, 2335, pk) == W::Load);
        env.owner.epoch = 2;
        CHECK(gsl::process_key(*eng, env, g, K, 2335, pk) == W::Present);
        CHECK(dropped(eng) == 1 && eng->holds.size() == 2 && eng->holds_taken.load() == 3 && env.outstanding() == 3);
        CHECK(env.releases == 0);
        // J (the old connection's) is dropped by the service; K (this one's) is given back when due.
        CHECK(gsl::service_holds(*eng, env, env.clock) == 1 && dropped(eng) == 2 && eng->holds.has(K));
        CHECK(gsl::service_holds(*eng, env, env.clock + gsl::kHoldMaxUs + gsl::kHoldServiceUs) == 1);
        CHECK(env.releases == 1 && env.outstanding() == 2 && eng->holds.size() == 0);
        CHECK(eng->hold_ends[static_cast<size_t>(H::Timeout)].load() == 1);
    }
    // The module switched off: every hold dropped at once, nothing called; a release refused (-1) mid-service also
    // only drops the entry.
    {
        auto eng = std::make_unique<gsl::Engine<64>>();
        StubEnv env;
        env.add_ctx(K);
        env.add_ctx(J);
        gsl::PacketBudget pk;
        gsl::process_key(*eng, env, g, K, 2335, pk);
        gsl::process_key(*eng, env, g, J, 2335, pk);
        env.module_off = true;
        CHECK(gsl::service_holds(*eng, env, env.clock, true) == 2 && eng->holds_dropped.load() == 2);
        CHECK(env.releases == 0 && env.outstanding() == 2 && eng->held_now.load() == 0);
        env.module_off = false;
        env.container.erase(K);
        env.container.erase(J);
        gsl::PacketBudget pk2;
        gsl::process_key(*eng, env, g, K, 2335, pk2);
        CHECK(eng->holds.size() == 1 && gsl::drop_holds(*eng, env, env.clock) == 1 && eng->holds.size() == 0);
        CHECK(dropped(eng) == 1 && env.releases == 0 && eng->held_now.load() == 0);
    }
}

void test_engine_hold_full() {
    auto eng = std::make_unique<gsl::Engine<1024>>();
    StubEnv env;
    const auto g = open_gates();
    for (uint32_t i = 0; i < gsl::kHolds + 3; ++i) {
        const uint32_t k = 0x51000000 + i * 0x101;
        env.add_ctx(k);
        env.container[k] = 1;
        gsl::PacketBudget pk;
        gsl::process_key(*eng, env, g, k, 2335, pk);
    }
    CHECK(eng->holds.size() == gsl::kHolds && eng->holds_full.load() == 3 && env.releases == 3);
    CHECK(eng->held_peak.load() == gsl::kHolds && env.outstanding() == static_cast<int>(gsl::kHolds));
    CHECK(gsl::service_holds(*eng, env, env.clock, true) == gsl::kHolds && env.outstanding() == 0);
}

// Session 13 (p8-impl-horarios.md §11): a burst of 2335s at the field entry, then, in the same second, the 2005 that
// names two more files (the sheep / birds of 0xF56759DA, 0x268D4087): every file is in the container BEFORE its
// packet is delivered; nothing waits in the retry set; no LATE.
std::vector<uint8_t> packet_2335(uint32_t resource) {
    std::vector<uint8_t> pl(gsl::kScheduleCreateMin, 0);
    for (int i = 0; i < 4; ++i) pl[gsl::kScheduleCreateResource + i] = static_cast<uint8_t>(resource >> (8 * i));
    return packet(gsl::kOpScheduleCreate, pl);
}

void test_engine_session13_burst() {
    using W = gsl::Why;
    auto eng = std::make_unique<gsl::Engine<1024>>();
    StubEnv env;
    env.load_us = 300;  // session 13: 0.02-2.83 ms a load
    const auto g = open_gates();
    // 40 x 2335 (the old budget stopped at 32 loads a second: the 33rd and later were delivered without their file).
    for (uint32_t i = 0; i < 40; ++i) {
        const uint32_t r = 0x61000000 + i * 0x10101;
        env.add_ctx(r);
        const auto keys = gsl::packet_keys(packet_2335(r), nullptr, &eng->retry);
        CHECK(keys.ok && keys.n == 1 && keys.keys[0] == r);
        gsl::PacketBudget pk;
        gsl::process_packet(*eng, env, g, keys, pk);
        CHECK(env.container.count(r) == 1);  // in the container before this 2335 is delivered
    }
    CHECK(env.loads == 40 && eng->holds.size() == 40 && eng->retry.size() == 0);
    // The 2005 of the 13 NPCs (6 sheep 0xB010013D-142 -> 0xF56759DA, 7 birds 0xB0100143-149 -> 0x268D4087), still in
    // the same second as the burst.
    const uint32_t sheep = 0xF56759DA, birds = 0x268D4087;
    env.add_ctx(sheep);
    env.add_ctx(birds);
    std::vector<uint8_t> pl;
    for (uint32_t a = 0xB010013D; a <= 0xB0100149; ++a) {
        std::vector<uint8_t> body;
        put32(body, 0x2000018B);
        put32(body, a <= 0xB0100142 ? sheep : birds);
        add_record(pl, 3, a, body);
    }
    const gsl::KeySet set({sheep, birds});
    const auto keys = gsl::packet_keys(packet(gsl::kOpActorLoad, pl), &set, &eng->retry);
    CHECK(keys.ok && keys.n == 2 && keys.npc_records == 13 && keys.hits == 13);
    gsl::PacketBudget pk;
    gsl::process_packet(*eng, env, g, keys, pk);
    CHECK(env.container.count(sheep) == 1 && env.container.count(birds) == 1 && env.loads == 42);
    CHECK(eng->why[static_cast<size_t>(W::Budget)].load() == 0 && eng->why[static_cast<size_t>(W::OwnCap)].load() == 0);
    CHECK(eng->late.load() == 0 && eng->late_spilled.load() == 0 && env.lates.empty() && eng->retry.size() == 0);
    CHECK(eng->packet_max_us.load() == 600);  // the 2005's two loads
    // Named again by the next 2005: present, nothing loaded, still no LATE.
    gsl::PacketBudget pk2;
    gsl::process_packet(*eng, env, g, keys, pk2);
    CHECK(env.loads == 42 && eng->late.load() == 0 && eng->why[static_cast<size_t>(W::Present)].load() == 2);
}

// Session 13: what is LATE (a key the packet names, not in the container at its delivery, mode 1) and what is not.
void test_engine_late() {
    using W = gsl::Why;
    using S = gsl::KeyState;
    const auto g = open_gates();
    const uint32_t K = 0xF56759DA;
    auto late_why = [](auto& e, W w) { return e->late_why[static_cast<size_t>(w)].load(); };
    // The packet's own-key time cap (pathological): kept in the retry set, LATE, loaded at the next delivery.
    {
        auto eng = std::make_unique<gsl::Engine<64>>();
        StubEnv env;
        env.add_ctx(K);
        gsl::PacketBudget pk;
        pk.us = gsl::kOwnPacketCapUs;
        CHECK(gsl::process_named(*eng, env, g, K, 2005, pk) == W::OwnCap && eng->retry.has(K) && env.loads == 0);
        CHECK(eng->late.load() == 1 && late_why(eng, W::OwnCap) == 1 && env.lates.size() == 1);
        CHECK(env.lates[0].key == K && env.lates[0].op == 2005 && env.lates[0].a == static_cast<uint64_t>(W::OwnCap));
        gsl::PacketBudget next;
        CHECK(gsl::process_retry(*eng, env, g, next) == 1 && env.loads == 1 && !eng->retry.has(K));
    }
    // A load whose file the container still lacks (a failure): LATE as "loaded" with state FAILED; kept for a retry.
    {
        auto eng = std::make_unique<gsl::Engine<64>>();
        StubEnv env;
        env.add_ctx(K);
        env.load_inserts = false;
        gsl::PacketBudget pk;
        CHECK(gsl::process_named(*eng, env, g, K, 2005, pk) == W::Load && eng->retry.has(K));
        CHECK(eng->late.load() == 1 && late_why(eng, W::Load) == 1);
        CHECK(env.lates.size() == 1 && env.lates[0].b == static_cast<uint64_t>(S::Failed));
    }
    // No context entry: LATE (no name to load it by).
    {
        auto eng = std::make_unique<gsl::Engine<64>>();
        StubEnv env;
        gsl::PacketBudget pk;
        CHECK(gsl::process_named(*eng, env, g, K, 2005, pk) == W::NoCtx && late_why(eng, W::NoCtx) == 1);
    }
    // The context table changed right before the call: decided again at once; loaded, not LATE. Changed again on
    // the second try too: LATE, kept for a retry.
    {
        auto eng = std::make_unique<gsl::Engine<64>>();
        StubEnv env;
        env.add_ctx(K);
        env.add_ctx(K + 1);
        env.ctx_changes_once = 1;
        gsl::PacketBudget pk;
        CHECK(gsl::process_named(*eng, env, g, K, 2005, pk) == W::Load && env.container.count(K) == 1);
        CHECK(eng->ctx_redo.load() == 1 && eng->late.load() == 0 && !eng->retry.has(K));
        env.ctx_changes_once = 2;
        CHECK(gsl::process_named(*eng, env, g, K + 1, 2005, pk) == W::CtxChanged && eng->retry.has(K + 1));
        CHECK(eng->ctx_redo.load() == 2 && late_why(eng, W::CtxChanged) == 1);
    }
    // The session cap: LATE.
    {
        auto eng = std::make_unique<gsl::Engine<64>>();
        StubEnv env;
        env.add_ctx(K);
        eng->loads_total.store(gsl::kSessionCap);
        gsl::PacketBudget pk;
        CHECK(gsl::process_named(*eng, env, g, K, 2005, pk) == W::Cap && late_why(eng, W::Cap) == 1);
    }
    // Not LATE: present; gates closed; probe; latched to probe; a retry-set key (op 0) deferred by its budget.
    {
        auto eng = std::make_unique<gsl::Engine<64>>();
        StubEnv env;
        env.add_ctx(K);
        env.add_ctx(K + 1);
        env.container[K + 2] = 1;
        gsl::PacketBudget pk;
        CHECK(gsl::process_named(*eng, env, g, K + 2, 2005, pk) == W::Present);
        gsl::Gates off = open_gates();
        off.remote_live = false;
        CHECK(gsl::process_named(*eng, env, off, K, 2005, pk) == W::NotRemote);
        CHECK(gsl::process_named(*eng, env, open_gates(gsl::Mode::Probe), K, 2005, pk) == W::WouldLoad);
        CHECK(gsl::process_named(*eng, env, open_gates(gsl::Mode::Probe), K, 2005, pk) == W::Decided);
        eng->latched.store(true);
        CHECK(gsl::process_named(*eng, env, g, K + 1, 2005, pk) == W::WouldLoad);
        eng->latched.store(false);
        eng->retry.add(K + 1);
        gsl::PacketBudget busy;
        busy.us = gsl::kRetryPacketUs;
        CHECK(gsl::process_retry(*eng, env, g, busy) == 1 && eng->retry.has(K + 1));
        CHECK(eng->late.load() == 0 && env.lates.empty() && env.loads == 0);
    }
    // Keys beyond the per-packet cap: counted LATE with one note (key 0, a = Why::Count, b = how many).
    {
        auto eng = std::make_unique<gsl::Engine<64>>();
        StubEnv env;
        gsl::PacketKeys keys;
        keys.op = gsl::kOpActorLoad;
        keys.ok = true;
        keys.spilled = 3;
        gsl::PacketBudget pk;
        gsl::process_packet(*eng, env, g, keys, pk);
        CHECK(eng->late_spilled.load() == 3 && eng->late.load() == 0 && env.lates.size() == 1);
        CHECK(env.lates[0].key == 0 && env.lates[0].a == static_cast<uint64_t>(W::Count) && env.lates[0].b == 3);
        gsl::PacketBudget pk2;
        gsl::process_packet(*eng, env, open_gates(gsl::Mode::Probe), keys, pk2);  // probe: not counted
        CHECK(eng->late_spilled.load() == 3);
    }
    for (size_t i = 0; i < static_cast<size_t>(W::Count); ++i) CHECK(std::string(gsl::why_name(static_cast<W>(i))) != "?");
}

// Review of b90e32a, M3: a slow disk (loads under kLatchUs) must not hold the NetworkThread for seconds: a per-second
// cap for the packet's own keys too, and probe after kSlowLatchCount loads over kSlowLoadUs.
void test_review_m3() {
    using W = gsl::Why;
    const auto g = open_gates();
    // 40 ms loads (not "slow"), one key per packet, all in one second: the own-key cap of the second stops them at
    // kOwnWindowLoadUs; the rest are LATE (OwnCap) and kept; the next second's retry loads them.
    {
        auto eng = std::make_unique<gsl::Engine<256>>();
        StubEnv env;
        env.load_us = 40'000;
        uint32_t loaded = 0, capped = 0;
        for (uint32_t k = 1; k <= 20; ++k) {
            env.add_ctx(k);
            gsl::PacketBudget pk;
            const W w = gsl::process_named(*eng, env, g, k, 2005, pk);
            loaded += w == W::Load;
            capped += w == W::OwnCap;
        }
        const uint32_t fit = static_cast<uint32_t>((gsl::kOwnWindowLoadUs + 39'999) / 40'000);  // 13: checked before
        CHECK(loaded == fit && capped == 20 - fit && eng->retry.size() == 20 - fit);
        CHECK(eng->late.load() == 20 - fit && eng->late_why[static_cast<size_t>(W::OwnCap)].load() == 20 - fit);
        CHECK(eng->window.us_in_window() < gsl::kOwnWindowLoadUs + 40'000 && !eng->latched.load());
        env.clock += gsl::kWindowUs;
        gsl::PacketBudget next;
        CHECK(gsl::process_retry(*eng, env, g, next) == 7 && env.loads == static_cast<int>(fit) + 1);  // 20 ms/packet
    }
    // 60 ms loads: probe after kSlowLatchCount of them (one Latch note), the next key is only decided.
    {
        auto eng = std::make_unique<gsl::Engine<64>>();
        StubEnv env;
        env.load_us = 60'000;
        for (uint32_t k = 1; k <= gsl::kSlowLatchCount + 2; ++k) env.add_ctx(k);
        for (uint32_t k = 1; k < gsl::kSlowLatchCount; ++k) {
            gsl::PacketBudget pk;
            CHECK(gsl::process_named(*eng, env, g, k, 2005, pk) == W::Load && !eng->latched.load());
        }
        gsl::PacketBudget pk;
        CHECK(gsl::process_named(*eng, env, g, gsl::kSlowLatchCount, 2005, pk) == W::Load && eng->latched.load());
        CHECK(eng->slow.load() == gsl::kSlowLatchCount && env.count(gsl::Note::Latch) == 1);
        CHECK(gsl::process_named(*eng, env, g, gsl::kSlowLatchCount + 1, 2005, pk) == W::WouldLoad);
        CHECK(env.loads == static_cast<int>(gsl::kSlowLatchCount) && eng->late.load() == 0);
    }
    // The NetworkThread's time per second.
    gsl::BlockMeter m;
    CHECK(!m.add(1'000'000, 100'000) && !m.add(1'200'000, 100'000) && m.second_us() == 200'000);
    CHECK(m.add(1'300'000, 60'000) && m.second_us() == 260'000);  // reaches kBlockWarnUs: one warning
    CHECK(!m.add(1'400'000, 10'000) && m.seconds_over.load() == 1);
    CHECK(!m.add(2'000'000, 5'000) && m.second_us() == 5'000);  // the next second
    CHECK(m.max_second_us.load() == 270'000 && m.max_call_us.load() == 100'000);
    CHECK(m.take_recent() == 270'000 && m.take_recent() == 0);
    CHECK(!m.add(2'100'000, 1'000) && m.take_recent() == 6'000);
}

// Review of b90e32a, L4: a file being unloaded is not present for the packet that names it.
void test_review_l4() {
    using W = gsl::Why;
    const auto g = open_gates();
    const uint32_t K = 0xF56759DA, J = 0x268D4087;
    auto packet_of = [&](std::initializer_list<uint32_t> ks) {
        gsl::PacketKeys p;
        p.op = gsl::kOpActorLoad;
        p.ok = true;
        for (uint32_t k : ks) p.keys[p.n++] = k;
        return p;
    };
    // The sweep ends between the two passes: K is loaded after J, before the delivery; nothing LATE.
    {
        auto eng = std::make_unique<gsl::Engine<64>>();
        StubEnv env;
        env.add_ctx(K);
        env.add_ctx(J);
        env.container[K] = 2;
        env.sweep_ends_after_check.insert(K);
        gsl::PacketBudget pk;
        gsl::process_packet(*eng, env, g, packet_of({K, J}), pk);
        CHECK(env.container[K] == 1 && env.container[J] == 1 && env.loads == 2);
        CHECK(eng->unloading.load() == 1 && eng->unloading_rechecked.load() == 1 && eng->late.load() == 0);
        CHECK(eng->retry.size() == 0 && env.lates.empty());
    }
    // Still being unloaded at the end of the packet: LATE (Why::Unloading, its own reason), kept for a retry.
    {
        auto eng = std::make_unique<gsl::Engine<64>>();
        StubEnv env;
        env.add_ctx(K);
        env.container[K] = 2;
        gsl::PacketBudget pk;
        gsl::process_packet(*eng, env, g, packet_of({K}), pk);
        CHECK(env.loads == 0 && eng->late.load() == 1 && eng->late_why[static_cast<size_t>(W::Unloading)].load() == 1);
        CHECK(env.lates.size() == 1 && env.lates[0].a == static_cast<uint64_t>(W::Unloading) && eng->retry.has(K));
        env.container.erase(K);
        gsl::PacketBudget pk2;
        CHECK(gsl::process_retry(*eng, env, g, pk2) == 1 && env.loads == 1 && !eng->retry.has(K));
    }
    // Probe: decided, not LATE.
    {
        auto eng = std::make_unique<gsl::Engine<64>>();
        StubEnv env;
        env.add_ctx(K);
        env.container[K] = 2;
        gsl::PacketBudget pk;
        gsl::process_packet(*eng, env, open_gates(gsl::Mode::Probe), packet_of({K}), pk);
        CHECK(eng->late.load() == 0 && env.loads == 0);
    }
}

}  // namespace

int main() {
    test_mode();
    test_hash();
    test_base_name();
    test_ctx();
    test_key_set();
    test_packet_keys();
    test_book();
    test_budget();
    test_retry_set();
    test_engine_gates();
    test_engine_load();
    test_engine_ctx();
    test_engine_probe_and_latch();
    test_engine_budget_and_failures();
    test_engine_untracked();
    test_engine_cap();
    test_hold_due();
    test_engine_hold();
    test_engine_hold_full();
    test_engine_hold_owner();
    test_engine_session13_burst();
    test_engine_late();
    test_review_m3();
    test_review_l4();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_sched_load: all passed\n");
    return 0;
}
