// housing_share (src/mod/session2/housing_share_logic.h), against the housing Acks both copies got at login on
// 2026-11-06 (3549, cdmp_housing_<pid>.txt: the host's 0xA0100001, the guest's 0xA0100066).

#include <cstdint>
#include <cstdio>
#include <vector>

#include "mod/session2/housing_share_logic.h"

using namespace cdmp::session2;

namespace {
int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++g_failures; } } while (0)

std::vector<uint8_t> with_header(uint16_t op, std::vector<uint8_t> pay) {
    std::vector<uint8_t> v = {static_cast<uint8_t>(op), static_cast<uint8_t>(op >> 8), 0xFF,
                              static_cast<uint8_t>(pay.size()), static_cast<uint8_t>(pay.size() >> 8)};
    v.insert(v.end(), pay.begin(), pay.end());
    return v;
}

void test_sets() {
    CHECK(hsh::is_login(35245) && hsh::is_login(2733) && hsh::is_login(35741) && hsh::is_login(35274));
    CHECK(hsh::is_update(2623) && hsh::is_update(2859) && !hsh::is_login(2623));
    CHECK(!hsh::is_housing(2661) && !hsh::is_housing(2005));
}

void test_owner_and_rekey() {
    // SyncPlayerHouseDataListAck of the host: 04 | owner | house 0xF4342 | 1 | ... | housing stage 0xF4416 ...
    const auto house = with_header(35245, {0x04, 0x01, 0x00, 0x10, 0xA0, 0x42, 0x43, 0x0F, 0x00, 0x01, 0x00, 0x00, 0x00,
                                           0x4A, 0x42, 0x16, 0x44, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4B, 0x42});
    CHECK(hsh::owner_of(house) == 0xA0100001u);
    size_t n = 0;
    const auto g = hsh::rekey(house, 0xA0100001, 0xA0100066, &n);
    CHECK(n == 1 && hsh::owner_of(g) == 0xA0100066u);
    CHECK(g.size() == house.size() && g[0] == house[0] && g[10] == 0x42);
    // SetLoadedHousingObjectSyncDataIdMaxAck: the owner first (no kind byte).
    const auto idmax = with_header(35741, {0x01, 0x00, 0x10, 0xA0, 0x1A, 0x01, 0x00, 0x00});
    CHECK(hsh::owner_of(idmax) == 0xA0100001u);
    // SyncHousingPlacedSceneObjectListAck: a flags byte, then the owner.
    const auto objs = with_header(2733, {0x01, 0x01, 0x00, 0x10, 0xA0, 0x16, 0x44, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00});
    CHECK(hsh::owner_of(objs) == 0xA0100001u);
    CHECK(!hsh::owner_of(with_header(2733, {0x01, 0x02, 0x03})));
    // The header is never rewritten, even if it looked like the key.
    const std::vector<uint8_t> tricky = {0x01, 0x00, 0x10, 0xA0, 0x00, 0x01, 0x00, 0x10, 0xA0};
    size_t m = 0;
    const auto t = hsh::rekey(tricky, 0xA0100001, 0xA0100066, &m);
    CHECK(m == 1 && t[0] == 0x01 && t[5] == 0x66);
}

void test_bursts() {
    hsh::Slot s;
    const auto a = with_header(2733, {1, 1, 0, 0x10, 0xA0});
    const auto b = with_header(2733, {3, 1, 0, 0x10, 0xA0});
    hsh::record(s, a, 1000);
    hsh::record(s, b, 1010);
    CHECK(s.packets.size() == 2);
    hsh::record(s, a, 1010 + hsh::kBurstMs + 1);  // a later login: a new burst
    CHECK(s.packets.size() == 1);
}

}  // namespace

// housing_share=1 never forwards a live change (the 2500 crash of 2026-11-06); only housing_share=live does.
void test_live_gate() {
    CHECK(!hsh::forward_live(false, hsh::kWaitingSyncList));
    CHECK(!hsh::forward_live(true, hsh::kWaitingSyncList));  // never
    for (const uint16_t op : hsh::kUpdates) {
        CHECK(!hsh::forward_live(false, op));
        CHECK(hsh::forward_live(true, op) == (op != hsh::kWaitingSyncList));
    }
    for (const uint16_t op : hsh::kLoginSet) CHECK(!hsh::forward_live(true, op));  // the login set has its own path
    CHECK(hsh::is_update(hsh::kWaitingSyncList));
    // live with a list: only the listed changes; 2500 never, even when listed or with an empty list.
    const uint16_t place[] = {2623};
    CHECK(hsh::forward_live(true, 2623, place));
    CHECK(!hsh::forward_live(true, 2859, place));
    CHECK(!hsh::forward_live(false, 2623, place));
    const uint16_t with_2500[] = {2500, 2623};
    CHECK(!hsh::forward_live(true, hsh::kWaitingSyncList, with_2500));
    size_t bad = 0;
    const auto ops = hsh::parse_live_ops(" 2623, 2859,2500, 35245, abc, 99999999", &bad);
    CHECK(ops.size() == 2 && ops[0] == 2623 && ops[1] == 2859);
    CHECK(bad == 4);  // 2500 (never), 35245 (a login opcode), abc, the overflow
    CHECK(hsh::parse_live_ops("").empty());
}

// A construction-mode edit (2408, the host's client) becomes the login Ack 2733 of the same stage.
void test_scene_req_to_ack() {
    // 2408 part: flags 1 | stage 0xF4416 | 0 | 0x1401 ...
    const auto req = [] {
        std::vector<uint8_t> pay = {0x01, 0x16, 0x44, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x14, 0x00, 0x00};
        std::vector<uint8_t> v = {0x68, 0x09, 0x00, static_cast<uint8_t>(pay.size()), 0x00};  // 2408, slot 0
        v.insert(v.end(), pay.begin(), pay.end());
        return v;
    }();
    const auto ack = hsh::scene_req_to_ack(req, 0xA0100001);
    CHECK(ack.has_value());
    if (ack) {
        const auto want = with_header(2733, {0x01, 0x01, 0x00, 0x10, 0xA0, 0x16, 0x44, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,
                                             0x01, 0x14, 0x00, 0x00});
        CHECK(*ack == want);
        CHECK(hsh::owner_of(*ack) == 0xA0100001u);
        CHECK(hsh::stage_of_list_ack(*ack) == 0xF4416u);
        CHECK(!hsh::is_last_part((*ack)[hsh::kHeader]));
    }
    CHECK(hsh::is_last_part(0x03));
    // Not a 2408, a wrong length field, too short: nothing.
    CHECK(!hsh::scene_req_to_ack(with_header(2733, {0x01, 0x16, 0x44, 0x0F, 0x00, 0x00}), 1));
    auto bad = req;
    bad[3] = 0x20;
    CHECK(!hsh::scene_req_to_ack(bad, 1));
    CHECK(!hsh::scene_req_to_ack(std::vector<uint8_t>{0x68, 0x09, 0x00, 0x02, 0x00, 0x01, 0x02}, 1));
    // The biggest part the client sends (32645 B) still fits as an Ack; one byte more than a buffer does not.
    std::vector<uint8_t> big(hsh::kHeader + 32645, 0);
    big[0] = 0x68; big[1] = 0x09; big[3] = static_cast<uint8_t>(32645 & 0xFF); big[4] = static_cast<uint8_t>(32645 >> 8);
    const auto b = hsh::scene_req_to_ack(big, 1);
    CHECK(b && b->size() == hsh::kHeader + 32649);
    const size_t over = hsh::kMaxPacket - hsh::kHeader - 3;
    std::vector<uint8_t> huge(hsh::kHeader + over, 0);
    huge[0] = 0x68; huge[1] = 0x09; huge[3] = static_cast<uint8_t>(over & 0xFF); huge[4] = static_cast<uint8_t>(over >> 8);
    CHECK(!hsh::scene_req_to_ack(huge, 1));
}

// An edit replaces only its stage's parts in the recorded login set.
void test_replace_stage() {
    const auto part = [](uint8_t flags, uint32_t stage, uint8_t tag) {
        return with_header(2733, {flags, 0x01, 0x00, 0x10, 0xA0, static_cast<uint8_t>(stage), static_cast<uint8_t>(stage >> 8),
                                  static_cast<uint8_t>(stage >> 16), static_cast<uint8_t>(stage >> 24), tag});
    };
    hsh::Slot s;
    s.packets = {part(1, 0xF4416, 1), part(3, 0xF4416, 2), part(3, 0xF4500, 3)};
    s.last_ms = 77;
    hsh::replace_stage(s, 0xF4416, {part(3, 0xF4416, 9)});
    CHECK(s.packets.size() == 2 && s.packets[0].back() == 9 && s.packets[1].back() == 3 && s.last_ms == 77);
    hsh::replace_stage(s, 0xF4600, {part(1, 0xF4600, 5), part(3, 0xF4600, 6)});  // a stage the login did not have
    CHECK(s.packets.size() == 4 && s.packets[2].back() == 5 && s.packets[3].back() == 6);
}

void test_rekey_set() {
    CHECK(hsh::parse_rekey("1").all && hsh::parse_rekey("1").has(2733));
    CHECK(!hsh::parse_rekey("").all && hsh::parse_rekey("").ops.empty());  // empty = the default, none
    const auto none = hsh::parse_rekey("0");
    CHECK(!none.all && none.ops.empty() && !none.has(2733));
    size_t bad = 0;
    const auto some = hsh::parse_rekey("35245, 2733, 2005, x", &bad);
    CHECK(!some.all && some.has(35245) && some.has(2733) && !some.has(2719) && !some.has(2005) && bad == 2);
}

// The category-4 records leave a 2733 part; the length field follows.
void test_drop_gimmick_records() {
    const auto rec = [](uint32_t id, uint8_t cat) {
        std::vector<uint8_t> r(hsh::kSceneRecord, 0);
        std::memcpy(r.data(), &id, 4);
        r[hsh::kRecordCategory] = cat;
        return r;
    };
    std::vector<uint8_t> pay = {0x03, 0x01, 0x00, 0x10, 0xA0, 0x16, 0x44, 0x0F, 0x00};
    for (const auto& r : {rec(0x7E, 0), rec(0x80, 4), rec(0x81, 4), rec(0xA0, 2), rec(0xFFFFFFFF, 4)}) {
        pay.insert(pay.end(), r.begin(), r.end());
    }
    const auto part = with_header(2733, pay);
    size_t n = 0;
    const auto all = hsh::drop_gimmick_records(part, {}, &n);
    CHECK(n == 2 && all.size() == part.size() - 2 * hsh::kSceneRecord);
    CHECK((all[3] | all[4] << 8) == static_cast<int>(all.size() - hsh::kHeader));
    CHECK(hsh::stage_of_list_ack(all) == 0xF4416u && hsh::owner_of(all) == 0xA0100001u);
    const uint32_t only81[] = {0x81};
    const auto some = hsh::drop_gimmick_records(part, only81, &n);
    CHECK(n == 1 && some.size() == part.size() - hsh::kSceneRecord);
    uint32_t second = 0;
    std::memcpy(&second, some.data() + hsh::kSceneRecordsAt + hsh::kSceneRecord, 4);
    CHECK(second == 0x80);
    auto odd = part;
    odd.push_back(0);
    CHECK(hsh::drop_gimmick_records(odd, {}, &n) == odd && n == 0);  // not whole records: unchanged
    // 2500: kind, key, stage, ids
    const auto w = with_header(2500, {0x04, 0x01, 0x00, 0x10, 0xA0, 0x16, 0x44, 0x0F, 0x00, 0x80, 0, 0, 0, 0x81, 0, 0, 0});
    uint32_t stage = 0;
    const auto ids = hsh::waiting_ids(w, &stage);
    CHECK(stage == 0xF4416u && ids.size() == 2 && ids[0] == 0x80 && ids[1] == 0x81);
    CHECK(hsh::parse_drop_gimmicks("all") == hsh::DropGimmicks::All);
    CHECK(hsh::parse_drop_gimmicks("waiting") == hsh::DropGimmicks::Waiting);
    CHECK(hsh::parse_drop_gimmicks("0") == hsh::DropGimmicks::None);
}

void test_guest_spawn_set() {
    CHECK(hsh::is_guest_spawn_req(2492) && hsh::is_guest_spawn_req(2446));
    CHECK(!hsh::is_guest_spawn_req(2408) && !hsh::is_guest_spawn_req(2539) && !hsh::is_guest_spawn_req(2500));
}

int main() {
    test_sets();
    test_guest_spawn_set();
    test_rekey_set();
    test_drop_gimmick_records();
    test_scene_req_to_ack();
    test_replace_stage();
    test_live_gate();
    test_owner_and_rekey();
    test_bursts();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_housing_share: all passed\n");
    return 0;
}
