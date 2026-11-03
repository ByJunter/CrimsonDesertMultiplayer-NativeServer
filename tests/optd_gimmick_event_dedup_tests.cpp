// P8 session 28, the duplicated gimmick projectiles (docs/research/s25/gimmicks-dueno.md section 7): the pure logic of
// gimmick_event_dedup (src/mod/session2/gimmick_event_dedup_logic.h) on S27's real bytes.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <span>
#include <vector>

#include "mod/session2/gimmick_event_dedup_logic.h"

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

// H27-1 t 50.117 / 214.899: CreateProjectileAck 2250 of the host's device gimmick 0xB010014C, index 1 and then 2 (the
// guest's 2779 when it loaded): the first 0x26 of their 127 bytes.
const uint8_t k2250Index1[0x26] = {0x4c, 0x01, 0x10, 0xb0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f,
                                   0x68, 0xed, 0x86, 0x4c, 0x01, 0x10, 0xb0, 0x01, 0x00, 0x00, 0x00, 0x08};
const uint8_t k2250Index2[0x26] = {0x4c, 0x01, 0x10, 0xb0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f,
                                   0x68, 0xed, 0x86, 0x4c, 0x01, 0x10, 0xb0, 0x02, 0x00, 0x00, 0x00, 0x08};
// G27-1 t 59.8: the guest's CreateProjectileFromGimmickEventReq 2779 for that gimmick (header + first 16 bytes).
const uint8_t k2779[5 + 16] = {0xdb, 0x0a, 0xff, 0x00, 0x01, 0x4c, 0x01, 0x10, 0xb0, 0x7f, 0x68,
                               0xed, 0x86, 0x00, 0x00, 0x00, 0x00, 0xdb, 0xdc, 0x25, 0xc6};
// G27-2 t 64.399: the first 0x20 bytes of the 2005 record of the host's device 0xB0100156 (controller 0x90100000 at
// +0x12) and t 64.378 of the guest's character 0xA0100377 (its user 0x90100376 at +0x12).
const uint8_t kHostDeviceHead[0x20] = {0x07, 0x56, 0x01, 0x10, 0xb0, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
                                       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x10, 0x90,
                                       0x71, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x00};
const uint8_t kOwnCharHead[0x20] = {0x01, 0x77, 0x03, 0x10, 0xa0, 0x76, 0x03, 0x10, 0x90, 0x00, 0x02,
                                    0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f, 0x76, 0x03, 0x10, 0x90,
                                    0x01, 0x00, 0x00, 0x00, 0x40, 0x42, 0x0f, 0x00, 0x05, 0x00};

std::vector<uint8_t> payload(std::initializer_list<std::span<const uint8_t>> recs) {
    std::vector<uint8_t> p;
    for (const auto& r : recs) {
        p.push_back(static_cast<uint8_t>(r.size() & 0xFF));
        p.push_back(static_cast<uint8_t>(r.size() >> 8));
        p.insert(p.end(), r.begin(), r.end());
    }
    return p;
}

void test_2250() {
    const auto a = ged::parse_2250(k2250Index1);
    CHECK(a.owner == 0xB010014Cu && a.index == 1);
    const auto b = ged::parse_2250(k2250Index2);
    CHECK(b.owner == 0xB010014Cu && b.index == 2);
    CHECK(ged::parse_2250(std::span<const uint8_t>(k2250Index1, 0x24)).owner == 0);  // short
    uint8_t other[0x26];
    std::memcpy(other, k2250Index1, sizeof other);
    other[0x1D] = 0x4d;  // the two owner copies differ
    CHECK(ged::parse_2250(other).owner == 0);
    // The 2779's gimmick at +0 of its payload: the same key.
    CHECK(dgl::le16(k2779) == ged::kOpCreateFromGimmickEvent && dgl::le32(k2779 + dgl::kHeader) == 0xB010014Cu);
}

void test_decide() {
    const uint32_t host = 0x90100000u, guest = 0x90100376u;
    CHECK(ged::decide(host, guest) == ged::Verdict::WouldWithhold);
    CHECK(ged::decide(guest, guest) == ged::Verdict::OwnController);
    CHECK(ged::decide(0, guest) == ged::Verdict::UnknownController);
    CHECK(ged::decide(0xA0100001u, guest) == ged::Verdict::UnknownController);  // not a user key
    CHECK(ged::decide(host, 0) == ged::Verdict::OwnUserUnknown);
    CHECK(ged::decide(guest, host) == ged::Verdict::WouldWithhold);  // the host's view of a guest-controlled one
    for (int i = 0; i < static_cast<int>(ged::Verdict::Count); ++i) {
        CHECK(std::strcmp(ged::verdict_name(static_cast<ged::Verdict>(i)), "?") != 0);
    }
}

void test_controllers() {
    static ged::ControllerMap map;
    const uint8_t user[9] = {0x00, 0x76, 0x03, 0x10, 0x90, 0x00, 0x00, 0x00, 0x00};
    auto pl = payload({user, kOwnCharHead, kHostDeviceHead});
    CHECK(ged::record_controllers(pl, map) == 1);  // type 7 only
    const ged::CtlEntry* e = map.find(0xB0100156u);
    CHECK(e && e->controller.load() == 0x90100000u);
    CHECK(map.find(0xA0100377u) == nullptr);
    // A later 2005 with another controller replaces it.
    std::vector<uint8_t> moved(std::begin(kHostDeviceHead), std::end(kHostDeviceHead));
    dgl::put32(&moved[dgl::kRecController], 0x90100376u);
    auto pl2 = payload({moved});
    CHECK(ged::record_controllers(pl2, map) == 1 && map.find(0xB0100156u)->controller.load() == 0x90100376u);
    // The own user, learned from the own character's record (docked_gimmicks_logic.h).
    dgl::Self self{0xA0100377u, 0};
    bool ok = false;
    CHECK(dgl::learn_own_user(pl, self, &ok) == 0x90100376u && ok && self.own_user == 0x90100376u);
    CHECK(dgl::learn_own_user(pl, self) == 0);  // unchanged: nothing learned again
    map.clear();
    CHECK(map.find(0xB0100156u) == nullptr && map.used() == 0);
}

void test_created() {
    // S27-1 (H27-1): the guest's 2779 for 0xB010087B reached the server first (index 1 at 312.196), the host's at
    // 312.288, index 2 at 312.296.
    {
        const uint64_t last[2] = {312288, 312150};
        const uint32_t count[2] = {1, 1};
        const auto v = ged::classify_2250(2, 312296, last, count);
        CHECK(v.kind == ged::Created::Duplicate && v.trigger == 0 && v.gap_ms == 8);
        const uint64_t first_last[2] = {0, 312150};
        const uint32_t first_count[2] = {0, 1};
        CHECK(ged::classify_2250(1, 312196, first_last, first_count).kind == ged::Created::First);
    }
    // S27-2: the host's 2779 for 0xB010015E at H 69.376; the guest's, sent at its world load, reached the server
    // around H 236.9; index 2 at H 239.032 (2.1 s later, the host's 2779 170 s older).
    {
        const uint64_t last[2] = {69376, 236900};
        const uint32_t count[2] = {1, 1};
        const auto v = ged::classify_2250(2, 239032, last, count);
        CHECK(v.kind == ged::Created::Duplicate && v.trigger == 1 && v.gap_ms == 2132);
    }
    // Index 2 with 2779 from one origin only: another cause.
    {
        const uint64_t last[2] = {1000, 0};
        const uint32_t count[2] = {2, 0};
        CHECK(ged::classify_2250(2, 1200, last, count).kind == ged::Created::LaterNoBoth);
    }
    // Index 2 with no 2779 within the window: another cause.
    {
        const uint64_t last[2] = {1000, 2000};
        const uint32_t count[2] = {1, 1};
        const auto v = ged::classify_2250(2, 2000 + ged::kDupWindowMs + 1, last, count);
        CHECK(v.kind == ged::Created::LaterNoBoth && v.trigger == -1);
    }
    CHECK(ged::recent(100, 100, 0) && !ged::recent(100, 0, 1000) && !ged::recent(100, 200, 1000));
}

// P8 S29: the far case (a 2779 of user 2 for a key the host's client never sent one for).
void test_lone() {
    const uint64_t w = ged::kDupWindowMs;
    // S28: every key had both origins (15 / 15, 3 / 3): never lone.
    const uint32_t both[2] = {1, 1};
    CHECK(!ged::lone_user2(both, 1000, 1000 + w + 1));
    // Only user 2's, past the window: lone; within it: not yet (the host's may still come, e.g. 16 ms later).
    const uint32_t only_u2[2] = {0, 1};
    CHECK(ged::lone_user2(only_u2, 1000, 1000 + w + 1));
    CHECK(!ged::lone_user2(only_u2, 1000, 1000 + w));
    // Only the host's client's, or nothing: not lone.
    const uint32_t only_host[2] = {1, 0};
    CHECK(!ged::lone_user2(only_host, 0, 1000 + w + 1));
    const uint32_t none[2] = {0, 0};
    CHECK(!ged::lone_user2(none, 0, 99999));
    CHECK(!ged::lone_user2(only_u2, 0, 99999));  // no time recorded
}

}  // namespace

int main() {
    test_2250();
    test_decide();
    test_controllers();
    test_created();
    test_lone();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_gimmick_event_dedup: all passed\n");
    return 0;
}
