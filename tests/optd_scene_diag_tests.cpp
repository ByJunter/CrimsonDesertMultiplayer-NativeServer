// P8 S1, NPC scenes: the measurement probes H1-H3 / G1-G4
// (src/mod/session2/scene_diag_logic.h; docs/research/pendientes/p8-s1-escenas-npc.md,
// "Implementado para la medida").
//
// SCOPE: offline logic only. The packets are copied byte for byte from the P8
// session 6 capture of the host's own client
// (D:\SteamLibrary\...\bin64\cdmp_packets_20261009-234606.bin, read only):
// tests/data/p8s6_*.bin (whole packets) and the hex strings below (#n = record
// number, t = seconds in that capture). 2874, 2126 and 2411 never appear in the
// session-6 captures: their records are built from the handlers' read order
// (0xC0CE50, 0xC0B840, 0xC0DB40; capstone) and say so. These tests do NOT test
// the hooks, the byte checks against the running game, the register reading,
// the threads nor the game's behaviour.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "mod/session2/observer_stage_logic.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/scene_diag_logic.h"

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

std::span<const uint8_t> payload(const std::vector<uint8_t>& packet) {
    return packet.size() >= scd::kHeader ? std::span<const uint8_t>(packet).subspan(scd::kHeader)
                                         : std::span<const uint8_t>();
}

// ---------------------------------------------------------------- the session-6 records (host's client)

// #1609 t=44.213978: 2335 of 0x20000014 (resource 0xAC00B3BB), 94 bytes.
const char* k2335_1609 =
    "1f09ff5e0004010010a0ba440f00bbb300ac00140000200000000000000000000000006ae626c6453a1a447f5270c500000000000000"
    "00000000000000803f0000803f0000803f0000803f1c5c27c6fc1b1a44ef6d73c534d126c63c151b443fe66fc5";
// #2391 t=44.230487: 2335 of 0x20000059 (resource 0xDA8B15D0): B0100083's schedule (p8-s1-escenas-npc.md §2.2).
const char* k2335_2391 =
    "1f09ff5e0004010010a05f500f00d0158bda00590000200000000000000000000000007e0124c63d651544137074c50000000000000000"
    "000000000000803f0000803f0000803f0000803fd86926c66ed00c4401f77ec5c02323c6864316445b266fc5";
// #9734 t=44.278828: a 110-byte 2335 (0x20000172, resource 0xBD40C8EF, u8 +13 = 1).
const char* k2335_9734 =
    "1f09ff6e0004010010a01c4a0f00efc840bd0172010020000000000000000000000000499127c6fd701b4456526fc500000000fb4c27bf"
    "00000000e6c4413f0000803f0000803f0000803ffc0128c604311a44b61c71c5841b27c634b11c44c6896dc5efc840bd0400000000000000"
    "a1500f00";
// #1607 t=44.213752: 2661 of sequencer 0x13 (resource 0xC4E5A00C at payload +27), 8 bindings.
const char* k2661_1607 =
    "650affb30004010010a0010010a0130000000000000001007a4f0f00000000000ca0e5c47a4f0f00000000000000000000000000000000"
    "000000000000000000000000000000000000ffffff7f01000000000000000000ffffffffffffffff0d03000020010010a00500000c000020"
    "6d0010b005000005000020710010b005000007000020720010b005000008000020730010b005000009000020740010b00500000b00002075"
    "0010b00500000d000020760010b0050000";
// The host client's C>S scene Reqs.
const char* k2767_41542 = "cf0a00120013000000000000001a00002001a10010b000";  // t=44.920758
const char* k2591_54633 = "1f0a0011006401000000000000000000000000000001";    // t=55.018711
const char* k2436_54637 = "8409000e006301000000000000000000000000";          // t=55.018741
const char* k2138_290272 = "5a08000c00630100000000000002000000";             // t=372.520549
const char* k3020_290246 = "cc0b0005007c0010b006";                           // t=372.496218
const char* k2173_986 = "7d08000f0001000000010010a00000006000000f";          // t=19.877824

void test_schedule_create() {
    const auto a = unhex(k2335_1609);
    CHECK(scd::le16(a.data()) == scd::kOpScheduleCreate);
    const auto s = scd::parse_schedule_create(payload(a));
    CHECK(s && s->kind == 4 && s->actor == 0xA0100001 && s->resource == 0xAC00B3BB && s->scene == 0x20000014 &&
          s->b13 == 0 && s->size == 94);
    const auto b = scd::parse_schedule_create(payload(unhex(k2335_2391)));
    CHECK(b && b->resource == 0xDA8B15D0 && b->scene == 0x20000059 && b->stamp == 0x000F505F);
    const auto c = scd::parse_schedule_create(payload(unhex(k2335_9734)));
    CHECK(c && c->resource == 0xBD40C8EF && c->scene == 0x20000172 && c->b13 == 1 && c->size == 110);
    const auto p = unhex(k2335_1609);
    CHECK(!scd::parse_schedule_create(payload(p).first(21)));
    CHECK(scd::parse_schedule_create(payload(p).first(22)).has_value());
}

void test_prepare_resource() {
    const auto a = unhex(k2661_1607);
    const auto pl = payload(a);
    CHECK(scd::prepare_resource(pl) == 0xC4E5A00Cu);
    const auto pr = obs::parse_prepare_payload(pl);
    CHECK(pr && pr->seq == 0x13 && pr->bindings.size() == 8 && pr->owner_actor == 0xA0100001);
    CHECK(!scd::prepare_resource(pl.first(90)));
}

// 2874 / 2126 / 2411: built from the handlers' read order (not in any capture).
void test_schedule_init_delete_closed() {
    std::vector<uint8_t> p(64, 0);
    auto put32 = [&](size_t o, uint32_t v) { std::memcpy(&p[o], &v, 4); };
    put32(0, 0xA0100001);  // the actor 0xC0CE50 resolves first
    put32(4, 0xB010006D);  // the NPC (0x8ADFE0)
    put32(8, 0xC4E5A00C);  // resource
    put32(12, 0x2000000C); // node
    const uint64_t key = 0x13;
    std::memcpy(&p[16], &key, 8);
    for (size_t i = 24; i < 64; ++i) p[i] = 0xFF;  // 10 x 4 x u8
    const auto i = scd::parse_init_schedule(p);
    CHECK(i && i->actor == 0xA0100001 && i->npc == 0xB010006D && i->resource == 0xC4E5A00C && i->node == 0x2000000C &&
          i->scene == 0x13 && i->complete);
    const auto j = scd::parse_init_schedule(std::span<const uint8_t>(p).first(24));
    CHECK(j && !j->complete && j->scene == 0x13);
    CHECK(!scd::parse_init_schedule(std::span<const uint8_t>(p).first(23)));

    std::vector<uint8_t> d(12, 0);
    const uint32_t actor = 0xA0100001;
    const uint64_t scene = 0x20000059;
    std::memcpy(&d[0], &actor, 4);
    std::memcpy(&d[4], &scene, 8);
    const auto del = scd::parse_schedule_delete(d);
    CHECK(del && del->actor == actor && del->scene == scene);
    CHECK(!scd::parse_schedule_delete(std::span<const uint8_t>(d).first(11)));
    const auto cl = scd::parse_schedule_closed(std::span<const uint8_t>(d).first(8));
    CHECK(cl && cl->actor == actor && cl->id == 0x20000059);
    CHECK(!scd::parse_schedule_closed(std::span<const uint8_t>(d).first(7)));
}

void test_2005_records_and_ai_block() {
    // Damiane: one record of 10813 bytes, kind 4, key 0xB010006D; its AI block at record +0x2241.
    const auto damiane = load("p8s6_2005_1571.bin");
    CHECK(scd::le16(damiane.data()) == scd::kOpActorLoad);
    const auto pl = payload(damiane);
    const auto recs = scd::parse_2005(pl);
    CHECK(recs && recs->size() == 1);
    if (recs && !recs->empty()) {
        const scd::Record2005& r = recs->front();
        CHECK(r.off == 2 && r.len == 10813 && r.kind == 4 && r.key == 0xB010006D);
        const std::span<const uint8_t> rec = pl.subspan(r.off, r.len);
        const auto blocks = scd::find_ai_blocks(rec, 0xC4E5A00C);
        CHECK(blocks.size() == 1);
        if (!blocks.empty()) {
            CHECK(blocks[0].off == 0x2241 && blocks[0].flag == 0 && blocks[0].node == 0x2000000C &&
                  blocks[0].scene == 0x13);
        }
        CHECK(scd::find_ai_blocks(rec, 0xC4E5A00C, 0x13).size() == 1);
        CHECK(scd::find_ai_blocks(rec, 0xC4E5A00C, 0x14).empty());  // another scene key: not this block
        CHECK(scd::find_ai_blocks(rec, 0xDA8B15D0).empty());
        CHECK(scd::find_ai_blocks(rec, 0).empty());
        CHECK(scd::read_ai_block(rec, rec.size() - 16) == std::nullopt);
    }
    const scd::Scan2005 s = scd::scan_2005(pl.data(), pl.size());
    CHECK(s.ok && s.records == 1 && s.npcs == 1 && s.first_npc == 0xB010006D);

    // B0100083 (a schedule NPC, scene 0x20000059): two identical blocks in its record, u8 = 1.
    const auto b83 = load("p8s6_2005_38259.bin");
    const auto pl83 = payload(b83);
    const auto r83 = scd::parse_2005(pl83);
    CHECK(r83 && r83->size() == 1 && r83->front().key == 0xB0100083 && r83->front().kind == 3);
    if (r83 && !r83->empty()) {
        const auto blocks = scd::find_ai_blocks(pl83.subspan(r83->front().off, r83->front().len), 0xDA8B15D0, 0x20000059);
        CHECK(blocks.size() == 2);
        if (blocks.size() == 2) {
            CHECK(blocks[0].off == 0x13A9 && blocks[0].flag == 1 && blocks[0].node == 0 && blocks[1].off == 0x179A);
        }
    }

    // #785: three records (an NPC, a user, a character).
    const auto many = load("p8s6_2005_785.bin");
    const auto plm = payload(many);
    const auto rm = scd::parse_2005(plm);
    CHECK(rm && rm->size() == 3);
    if (rm && rm->size() == 3) {
        CHECK((*rm)[0].key == 0xB0100065 && (*rm)[0].kind == 7 && (*rm)[0].len == 6168);
        CHECK((*rm)[1].key == 0x90100000 && (*rm)[1].kind == 0 && (*rm)[1].len == 9);
        CHECK((*rm)[2].key == 0xA0100001 && (*rm)[2].kind == 1 && (*rm)[2].len == 11691);
    }
    const scd::Scan2005 sm = scd::scan_2005(plm.data(), plm.size());
    CHECK(sm.ok && sm.records == 3 && sm.npcs == 1 && sm.first_npc == 0xB0100065);
    // Limits: a truncated payload does not tile; a record cap.
    CHECK(!scd::parse_2005(plm.first(plm.size() - 1)));
    CHECK(!scd::scan_2005(plm.data(), plm.size() - 1).ok);
    CHECK(!scd::parse_2005(plm, 2));
    CHECK(!scd::scan_2005(plm.data(), plm.size(), 2).ok);
    CHECK(!scd::parse_2005({}));
    const uint8_t tiny[] = {0x02, 0x00, 0x04, 0x6D};  // a 2-byte record (< 5): refused
    CHECK(!scd::parse_2005(tiny) && !scd::scan_2005(tiny, sizeof tiny).ok);
}

void test_start_action() {
    // #27836 t=44.426: Damiane's StartActionReq with 0x2D3 = 1 (seq_bodyanim): applied, never relayed.
    const auto f = load("p8s6_2223_27836.bin");
    CHECK(scd::le16(f.data()) == scd::kOpStartActionReq);
    const auto w = scd::parse_start_action(payload(f));
    CHECK(w && w->key == 0xB010006D && w->flag == 1 && w->h[0] == 0 && w->h[1] == 0x837487D4 && w->h[2] == 0 &&
          w->h[3] == 0x35DD392F);
    if (w) CHECK(scd::action_hash(w->h[0], w->h[1], w->h[2], w->h[3]) == 0x837487D4);
    // #141350 t=240.05: an NPC action with 0x2D3 = 0 (relayed).
    const auto g = load("p8s6_2223_141350.bin");
    const auto x = scd::parse_start_action(payload(g));
    CHECK(x && x->key == 0xB01000C7 && x->flag == 0 && x->h[0] == 0x360C0E2C && x->h[1] == 0x360C0E2C &&
          x->h[2] == 0x26107D3C);
    CHECK(!scd::parse_start_action(payload(g).first(724)));
    CHECK(scd::action_hash(1, 2, 3, 4) == 2);
    CHECK(scd::action_hash(1, 0, 3, 4) == 1);
    CHECK(scd::action_hash(0, 0, 3, 4) == 4);
    CHECK(scd::action_hash(0, 0, 3, 0) == 3);
    CHECK(scd::action_hash(0, 0, 0, 0) == 0);
}

void test_scene_reqs() {
    const auto a = unhex(k2767_41542);
    auto r = scd::parse_scene_req(scd::le16(a.data()), payload(a));
    CHECK(r.op == 2767 && r.has_scene && r.scene == 0x13 && r.has_actor && r.actor == 0xB01000A1);
    const auto b = unhex(k2591_54633);
    r = scd::parse_scene_req(scd::le16(b.data()), payload(b));
    CHECK(r.op == 2591 && r.has_scene && r.scene == 0x164 && !r.has_actor);
    const auto c = unhex(k2436_54637);
    r = scd::parse_scene_req(scd::le16(c.data()), payload(c));
    CHECK(r.op == 2436 && r.scene == 0x163);
    const auto d = unhex(k2138_290272);
    r = scd::parse_scene_req(scd::le16(d.data()), payload(d));
    CHECK(r.op == 2138 && r.scene == 0x163);
    const auto e = unhex(k3020_290246);
    r = scd::parse_scene_req(scd::le16(e.data()), payload(e));
    CHECK(r.op == 3020 && !r.has_scene && r.has_actor && r.actor == 0xB010007C);
    const auto g = unhex(k2173_986);
    r = scd::parse_scene_req(scd::le16(g.data()), payload(g));
    CHECK(r.op == 2173 && r.has_actor && r.actor == 0xA0100001);
    r = scd::parse_scene_req(2767, std::span<const uint8_t>(a).subspan(5, 4));  // too short for anything
    CHECK(!r.has_scene && !r.has_actor);
    CHECK(scd::scene_req_index(2767) == 0 && scd::scene_req_index(2223) == static_cast<int>(scd::kSceneReqCount) - 1);
    CHECK(scd::scene_req_index(2005) == -1);
    CHECK(scd::is_npc_key(0xB010006D) && !scd::is_npc_key(0xA0100001) && scd::is_character_key(0xA0100001));
}

void test_count_table() {
    scd::CountTable<16, 3> t;  // 8 keys at most, probes of 16
    bool ins = false;
    CHECK(!t.add(0, 0));    // 0 is never a key
    CHECK(!t.add(5, 3));    // column out of range
    CHECK(t.add(5, 0, 1, &ins) && ins);
    ins = false;
    CHECK(t.add(5, 0, 2, &ins) && !ins);
    CHECK(t.add(5, 2) && t.get(5, 0) == 3 && t.get(5, 2) == 1 && t.get(5, 1) == 0 && t.get(6, 0) == 0);
    for (uint64_t k = 100; k < 107; ++k) CHECK(t.add(k, 1));
    CHECK(t.size() == 8);
    CHECK(!t.add(999, 0));  // full: closed for new keys, counted as untracked
    CHECK(t.closed() && t.untracked() == 1);
    CHECK(t.add(5, 1));     // known keys still count
    const auto rows = t.rows();
    CHECK(rows.size() == 8);
    uint64_t sum = 0;
    for (const auto& r : rows) sum += r.c[0] + r.c[1] + r.c[2];
    CHECK(sum == 3 + 1 + 1 + 7);

    // Four writer threads on the same keys: no count lost.
    static scd::CountTable<1024, scd::kH1Cols> h;
    std::vector<std::thread> th;
    for (int w = 0; w < 4; ++w) {
        th.emplace_back([w] {
            for (int i = 0; i < 20000; ++i) h.add(0xB0100000u + static_cast<uint32_t>(i % 50), static_cast<size_t>(w % 2));
        });
    }
    for (auto& x : th) x.join();
    uint64_t c0 = 0, c1 = 0;
    for (const auto& r : h.rows()) {
        c0 += r.c[0];
        c1 += r.c[1];
    }
    CHECK(h.size() == 50 && c0 == 40000 && c1 == 40000 && h.untracked() == 0);
}

// A sparse fake memory for the replica (reads outside the map fail like a guarded fault).
struct FakeMem {
    std::map<uintptr_t, uint8_t> b;
    void put(uintptr_t a, const void* src, size_t n) {
        const auto* s = static_cast<const uint8_t*>(src);
        for (size_t i = 0; i < n; ++i) b[a + i] = s[i];
    }
    void p32(uintptr_t a, uint32_t v) { put(a, &v, 4); }
    void p64(uintptr_t a, uint64_t v) { put(a, &v, 8); }
    void p8(uintptr_t a, uint8_t v) { put(a, &v, 1); }
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

// The container of 0x186A810 with one file (resource R) whose sorted node table has
// three entries: (node 0x20000004 -> index 7), (0x2000000C -> 3), (0x20000020 -> 9), 10 entries in it.
constexpr uintptr_t kC = 0x10000, kArray = 0x20000, kEntries = 0x30000, kEntry = 0x40000, kHolder = 0x50000,
                    kFile = 0x60000, kNodes = 0x70000;
constexpr uint32_t kRes = 0xC4E5A00C, kBuckets = 4;

FakeMem make_container(bool with_file = true, uint8_t unloading = 0) {
    using L = scd::ScheduleLayout;
    FakeMem m;
    m.p32(kC + L::kUsed, 1);
    m.p32(kC + L::kBuckets, kBuckets);
    m.p64(kC + L::kBucketArray, kArray);
    m.p64(kC + L::kEntries, kEntries);
    for (uint32_t b = 0; b < kBuckets; ++b) m.p32(kArray + b * 0x100, 0);
    const uintptr_t bucket = kArray + (kRes % kBuckets) * 0x100;
    m.p32(bucket, 2);
    m.p32(bucket + 8, 0x12345678);  // another id in the same bucket
    m.p32(bucket + 0xC, 0);
    m.p32(bucket + 0x10, kRes);
    m.p32(bucket + 0x14, 1);
    m.p64(kEntries + 8, kEntry);
    m.p32(kEntry + L::kEntryKey, kRes);
    m.p64(kEntry + L::kEntryHolder, kHolder);
    m.p64(kHolder, with_file ? kFile : 0);
    m.p8(kFile + L::kFileUnloading, unloading);
    m.p64(kFile + L::kFileNodes, kNodes);
    m.p32(kFile + L::kFileNodeCount, 3);
    m.p32(kFile + L::kFileEntries, 10);
    const uint32_t nodes[3][2] = {{0x20000004, 7}, {0x2000000C, 3}, {0x20000020, 9}};
    for (uint32_t i = 0; i < 3; ++i) {
        m.p32(kNodes + i * 12, 0);
        m.p32(kNodes + i * 12 + 4, nodes[i][0]);
        m.p32(kNodes + i * 12 + 8, nodes[i][1]);
    }
    return m;
}

void test_schedule_lookup() {
    using scd::Lookup;
    FakeMem m = make_container();
    auto r = scd::schedule_lookup(m, kC, kRes, 0x2000000C);
    CHECK(r.what == Lookup::Found && r.file == kFile && r.index == 3 && r.nodes == 3 && r.buckets == kBuckets);
    r = scd::schedule_lookup(m, kC, kRes, 0x20000004);
    CHECK(r.what == Lookup::Found && r.index == 7);
    r = scd::schedule_lookup(m, kC, kRes, 0x20000020);
    CHECK(r.what == Lookup::Found && r.index == 9);
    // The game compares node >> 2: 0x2000000D is 0x2000000C's slot.
    r = scd::schedule_lookup(m, kC, kRes, 0x2000000D);
    CHECK(r.what == Lookup::Found && r.index == 3);
    r = scd::schedule_lookup(m, kC, kRes, 0x20000010);
    CHECK(r.what == Lookup::NodeNotFound && r.file == kFile);
    r = scd::schedule_lookup(m, kC, kRes, 0, false);
    CHECK(r.what == Lookup::Found && r.file == kFile);
    // Another resource (same bucket or not): the file is not loaded.
    CHECK(scd::schedule_lookup(m, kC, kRes + kBuckets, 0x2000000C).what == Lookup::FileNotLoaded);
    CHECK(scd::schedule_lookup(m, kC, 0x5FB728ED, 0).what == Lookup::FileNotLoaded);
    // index >= u32 [file+0x58]: not found
    m.p32(kFile + scd::ScheduleLayout::kFileEntries, 4);
    CHECK(scd::schedule_lookup(m, kC, kRes, 0x2000000C).what == Lookup::Found);
    CHECK(scd::schedule_lookup(m, kC, kRes, 0x20000004).what == Lookup::NodeNotFound);
    // The holder's file null / unloading.
    CHECK(scd::schedule_lookup(make_container(false), kC, kRes, 0x2000000C).what == Lookup::FileNotLoaded);
    const auto u = scd::schedule_lookup(make_container(true, 1), kC, kRes, 0x2000000C);
    CHECK(u.what == Lookup::FileUnloading && u.file == kFile);
    // An empty container, no container, an unreadable bucket.
    FakeMem e = make_container();
    e.p32(kC + scd::ScheduleLayout::kUsed, 0);
    CHECK(scd::schedule_lookup(e, kC, kRes, 0).what == Lookup::Empty);
    CHECK(scd::schedule_lookup(m, 0, kRes, 0).what == Lookup::NoContainer);
    CHECK(scd::schedule_lookup(FakeMem{}, kC, kRes, 0).what == Lookup::Unreadable);
    FakeMem bad = make_container();
    bad.b.erase(kArray + (kRes % kBuckets) * 0x100);
    CHECK(scd::schedule_lookup(bad, kC, kRes, 0).what == Lookup::Unreadable);
    // A garbage bucket count is bounded (31 pairs at most), never a long walk.
    FakeMem g = make_container();
    g.p32(kArray + (kRes % kBuckets) * 0x100, 0x7FFFFFFF);
    CHECK(scd::schedule_lookup(g, kC, kRes, 0x2000000C).what == Lookup::Found);
    // A garbage node count: the binary search is bounded too.
    FakeMem n = make_container();
    n.p32(kFile + scd::ScheduleLayout::kFileNodeCount, 0x7FFFFFFF);
    CHECK(scd::schedule_lookup(n, kC, kRes, 0x2000000C, true, 40).what == Lookup::Unreadable);
    n.p32(kFile + scd::ScheduleLayout::kFileNodeCount, 0x80000001u);  // the native "js": nothing
    CHECK(scd::schedule_lookup(n, kC, kRes, 0x2000000C).what == Lookup::NodeNotFound);
    for (int i = 0; i <= static_cast<int>(Lookup::Unreadable); ++i) {
        CHECK(std::string(scd::lookup_text(static_cast<Lookup>(i))) != "?");
    }
}

void test_handle_text() {
    CHECK(std::string(scd::handle_text(0x2000000C'C4E5A00Cull, 3, 0x1000)).find("VALID") == 0);
    CHECK(std::string(scd::handle_text(0xFFFFFFFF'00000000ull, 0xFFFFFFFFu, 0)).find("FILE NOT LOADED") !=
          std::string::npos);
    CHECK(std::string(scd::handle_text(0xFFFFFFFF'00000000ull, 0xFFFFFFFFu, 0x1000)).find("node not found") !=
          std::string::npos);
}

void test_ledger() {
    scd::SceneLedger l(4);
    l.new_field(0x90100000, 1);
    CHECK(l.field() == 1 && l.owner() == 0x90100000);
    l.schedule_create({0x20000014, 0xA0100001, 0xAC00B3BB, 100});
    l.schedule_create({0x20000059, 0xA0100001, 0xDA8B15D0, 110});
    l.prepare({0x13, 0xA0100001, 0xA0100001, 0xC4E5A00C, 8, 120});
    l.init({0xB010006D, 0xA0100001, 0xC4E5A00C, 0x2000000C, 0x13, 130});
    CHECK(l.schedules().size() == 2 && l.sequencers().size() == 1 && l.inits().size() == 1);
    auto h = l.find(0x20000059, 0);
    CHECK(h.kind == 1 && h.t_us == 110 && h.resource == 0xDA8B15D0);
    h = l.find(0x13, 0);
    CHECK(h.kind == 2 && h.resource == 0xC4E5A00C);
    h = l.find(0x999, 0xAC00B3BB);  // by resource when the key is unknown
    CHECK(h.kind == 1 && h.scene == 0x20000014);
    CHECK(l.find(0x999, 0x1234).kind == 0);
    const auto res = l.resources();
    CHECK(res.size() == 3);
    CHECK(l.schedule_closed(0x20000014) && l.schedules().size() == 1);
    CHECK(!l.schedule_closed(0x20000014));
    CHECK(l.schedule_delete(0x20000059) && !l.schedule_delete(0x20000059) && l.schedules().empty());
    CHECK(l.remove_sequencer(0x13) && !l.remove_sequencer(0x13));
    // Bounded: a full ledger refuses new scenes (counted), replaces known ones.
    for (uint64_t k = 1; k <= 5; ++k) l.schedule_create({k, 1, 2, k});
    CHECK(l.schedules().size() == 4 && l.full() == 1);
    l.schedule_create({1, 1, 3, 9});
    CHECK(l.schedules().at(1).resource == 3);
    // A new field of the owner drops everything.
    l.new_field(0x90100000, 200);
    CHECK(l.field() == 2 && l.schedules().empty() && l.sequencers().empty() && l.inits().empty());
    CHECK(l.dropped_at_field_change() == 4);  // the first field change dropped nothing; this one the 4 schedules
    CHECK(l.created() == 2 + 5 + 1 && l.prepared() == 1 && l.initialized() == 1);
}

void test_trip_tracker() {
    // Session 6, the guest's capture (p8-s1-escenas-npc.md §4 A.2): trip 2 had its 2549 at t=54.389,
    // the first NPC 2005 at t=54.475 and its 2144 at t=62.095.
    scd::TripTracker t;
    const uint64_t s = 1'000'000;
    t.on_out(scd::kOpMoveFieldAck, 48'944'000, 1);
    t.on_in(scd::kOpMoveFieldCompleteReq, 54'389'000);
    t.on_in(scd::kOpMoveFieldCompleteReq, 54'390'000);
    t.on_out(scd::kOpActorLoad, 54'475'000, 40, 1, 0xB010006D);
    t.on_out(scd::kOpActorLoad, 54'480'000, 41, 3, 0xB0100071);
    t.on_entry(54'500'000, false);
    t.on_entry(54'600'000, true);  // only the first entry counts
    t.on_out(scd::kOpMoveFieldCompleteAck, 62'095'000, 900);
    t.on_out(scd::kOpActorLoad, 62'200'000, 950, 2, 0xB0100090);
    t.on_out(scd::kOpMilestone, 62'300'000, 960);
    CHECK(!t.take_due(62'095'000 + 14 * s));
    const auto f = t.take_due(62'095'000 + 15 * s);
    CHECK(f.has_value());
    if (f) {
        CHECK(f->index == 1 && f->t2549 == 54'389'000 && f->n2549 == 2 && f->t_first_npc == 54'475'000 &&
              f->first_npc_packet == 40 && f->first_npc_key == 0xB010006D && f->packets == 960);
        CHECK(f->npc_before_2549 == 0 && f->npc_2549_to_2144 == 4 && f->npc_after_2144 == 2);
        CHECK(f->t_entry == 54'500'000 && !f->owner_entry && f->t2322 == 62'300'000);
        CHECK(scd::first_npc_position(*f) == scd::FirstNpc::Between2549And2144);
    }
    CHECK(!t.take_due(100 * s) && !t.current());
    // A trip whose NPC 2005 come before the 2549, closed by the next 2174.
    t.on_out(scd::kOpMoveFieldAck, 200 * s, 1);
    t.on_out(scd::kOpActorLoad, 200 * s + 10, 5, 1, 0xB0100001);
    t.on_in(scd::kOpMoveFieldCompleteReq, 201 * s);
    t.on_out(scd::kOpMoveFieldAck, 300 * s, 1);
    const auto g = t.take_due(300 * s);
    CHECK(g && g->index == 2 && scd::first_npc_position(*g) == scd::FirstNpc::Before2549 && g->npc_before_2549 == 1);
    CHECK(t.current() && t.current()->index == 3 && t.trips() == 3);
    // Packets before the first 2174 are not a trip.
    scd::TripTracker u;
    u.on_out(scd::kOpActorLoad, 5, 3, 1, 0xB0100001);
    u.on_in(scd::kOpMoveFieldCompleteReq, 6);
    CHECK(!u.current() && !u.take_due(100 * s));
    scd::TripFacts none;
    CHECK(scd::first_npc_position(none) == scd::FirstNpc::None);
    none.t_first_npc = 10;
    none.t2549 = 5;
    none.t2144 = 8;
    CHECK(scd::first_npc_position(none) == scd::FirstNpc::After2144);
}

void test_npc_book() {
    scd::NpcBook b(2);
    scd::NpcTimeline* x = b.get(0xB010006D);
    CHECK(x && x->npc == 0xB010006D);
    x->reads = 1;
    x->resource = 0xC4E5A00C;
    x->file_loaded = false;
    x->t_2005 = 1'000'000;
    x->t_bind_ai = 9'400'000;
    scd::NpcTimeline* y = b.get(0xB0100083);
    y->reads = 1;
    y->handle_valid = true;
    y->file_loaded = true;
    y->scene = 0x20000059;
    y->vf13_passed = true;
    y->t_instance = 5;
    CHECK(!b.get(0xB0100099) && b.overflow() == 1);  // bounded
    CHECK(!b.get(0));
    const auto s = b.summary();
    CHECK(s.npcs == 2 && s.with_scene == 2 && s.valid == 1 && s.file_missing == 1 && s.node_missing == 0 &&
          s.bound_ai == 1 && s.instance == 1 && s.vf13_passed == 1);
    CHECK(scd::rel_text(9'400'000, 1'000'000) == "+8.400 s");
    CHECK(scd::rel_text(918'000, 1'000'000) == "-0.082 s");
    CHECK(scd::rel_text(0, 1) == "?" && scd::rel_text(1, 0) == "?");
}

void test_x_snapshot() {
    scd::XSnapshot a, b;
    a.owner = b.owner = 0xA0100001;
    a.manager = b.manager = 0x3F2F8F9E380;
    CHECK(a == b);
    b.stage_count = 1;
    CHECK(!(a == b));
    b = a;
    b.first[7] = 1;
    CHECK(!(a == b));
    b = a;
    b.ready = 2;
    CHECK(!(a == b));
}

// The bounded ring and the per-window limit the probes use, at the sizes they use.
void test_ring_and_limits() {
    static p8p::EventRing<8192> ring;
    p8p::Event e;
    size_t pushed = 0;
    for (size_t i = 0; i < 9000; ++i) {
        e.v[0] = i;
        if (ring.push(e)) ++pushed;
    }
    CHECK(pushed == 8192 && ring.dropped() == 9000 - 8192);
    p8p::Event out;
    size_t popped = 0;
    bool ordered = true;
    while (ring.pop(out)) {
        ordered &= out.v[0] == popped;
        ++popped;
    }
    CHECK(popped == 8192 && ordered);
    p8p::WindowLimiter lim(4096, 5'000'000);  // "to session 2"
    size_t taken = 0;
    for (size_t i = 0; i < 5000; ++i) taken += lim.take(1'000'000 + i);
    CHECK(taken == 4096 && lim.refused() == 5000 - 4096);
    CHECK(lim.take(6'000'000));  // the next window
    p8p::KeyCounter<8192> keys;  // the per-NPC budget: the first 3 events of each NPC
    uint32_t lines = 0;
    for (int i = 0; i < 10; ++i) lines += p8p::within_key_budget(keys.bump(0xB010006D), 3) ? 1 : 0;
    CHECK(lines == 3);
}

}  // namespace

int main() {
    test_schedule_create();
    test_prepare_resource();
    test_schedule_init_delete_closed();
    test_2005_records_and_ai_block();
    test_start_action();
    test_scene_reqs();
    test_count_table();
    test_schedule_lookup();
    test_handle_text();
    test_ledger();
    test_trip_tracker();
    test_npc_book();
    test_x_snapshot();
    test_ring_and_limits();
    if (g_failures) {
        std::printf("%d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("optd_scene_diag: all tests passed\n");
    return 0;
}
