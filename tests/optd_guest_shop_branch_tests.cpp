// The guest's own shop branch (src/mod/session2/guest_shop_branch_logic.h), against the packets the host's client got
// natively on 2026-11-06 (scene 0x1AF, branch 6, payloads from cdmp_packets.host_20261106-050617).

#include <cstdint>
#include <cstdio>
#include <vector>

#include "mod/session2/guest_shop_branch_logic.h"

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

void test_built_like_the_server() {
    const uint32_t owner = 0xA0100001;
    const uint64_t scene = 0x1AF;
    CHECK(gsb::avoid_cut_scene(owner, scene) ==
          with_header(2561, {0x04, 0x01, 0x00, 0x10, 0xA0, 0xAF, 0x01, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x00, 0x10, 0xA0}));
    CHECK(gsb::on_branch_event(owner, scene, 6) ==
          with_header(2524, {0x04, 0x01, 0x00, 0x10, 0xA0, 0xAF, 0x01, 0, 0, 0, 0, 0, 0, 0x06, 0, 0, 0, 0x00}));
    CHECK(gsb::branch_state(owner, scene, 6) ==
          with_header(2981, {0x04, 0x01, 0x00, 0x10, 0xA0, 0xAF, 0x01, 0, 0, 0, 0, 0, 0, 0x01, 0, 0, 0, 0x06, 0, 0, 0}));
    const auto open = gsb::answer(owner, scene, 6, true);
    CHECK(open.size() == 3 && gsb::op_of(open[0]) == 2561 && gsb::op_of(open[1]) == 2524 && gsb::op_of(open[2]) == 2981);
    const auto close = gsb::answer(owner, scene, 0, false);
    CHECK(close.size() == 2 && gsb::op_of(close[0]) == 2524 && gsb::op_of(close[1]) == 2981);
    // The native close (host, 825.436): SetAvoidByCutSceneAck with 00.
    CHECK(gsb::avoid_cut_scene(owner, scene, false) ==
          with_header(2561, {0x04, 0x01, 0x00, 0x10, 0xA0, 0xAF, 0x01, 0, 0, 0, 0, 0, 0, 0x00, 0x01, 0x00, 0x10, 0xA0}));
    CHECK(gsb::answer(owner, scene, 0, true)[0] == gsb::avoid_cut_scene(owner, scene, false));
}

// The server's menu step for the host's grocery scene 0x1AD (host packet log, 388.420 / 388.422 / 388.433 s).
void test_menu_answer_like_the_server() {
    const uint32_t owner = 0xA0100001;
    const uint64_t scene = 0x1AD;
    const auto m = gsb::menu_answer(owner, scene);
    CHECK(m.size() == 3);
    CHECK(m[0] == with_header(2524, {0x04, 0x01, 0x00, 0x10, 0xA0, 0xAD, 0x01, 0, 0, 0, 0, 0, 0, 0x00, 0, 0, 0, 0x00}));
    CHECK(m[1] == with_header(2561, {0x04, 0x01, 0x00, 0x10, 0xA0, 0xAD, 0x01, 0, 0, 0, 0, 0, 0, 0x00, 0x01, 0x00, 0x10, 0xA0}));
    CHECK(m[2] == with_header(2981, {0x04, 0x01, 0x00, 0x10, 0xA0, 0xAD, 0x01, 0, 0, 0, 0, 0, 0, 0x02, 0, 0, 0, 0x00, 0, 0, 0,
                                     0x9D, 0x90, 0x9C, 0x97, 0, 0, 0, 0}));
}

// The 07:05 host session (scene 0x2A3, a purchase): every answer as the server wrote it.
void test_native_answers() {
    const uint32_t owner = 0xA0100001;
    const uint64_t scene = 0x2A3;
    const std::vector<uint8_t> hdr = {0x04, 0x01, 0x00, 0x10, 0xA0, 0xA3, 0x02, 0, 0, 0, 0, 0, 0};
    auto cat = [&](std::vector<uint8_t> tail) {
        std::vector<uint8_t> v = hdr;
        v.insert(v.end(), tail.begin(), tail.end());
        return v;
    };
    const auto shop = gsb::native_answer(owner, scene, 6, false);
    CHECK(shop.size() == 3 && shop[2] == with_header(2981, cat({0x01, 0, 0, 0, 0x06, 0, 0, 0})));
    const auto menu = gsb::native_answer(owner, scene, 0, false);
    CHECK(menu == gsb::menu_answer(owner, scene));
    const auto leave = gsb::native_answer(owner, scene, 3, true);
    CHECK(leave.size() == 2 && leave[0] == with_header(2524, cat({0x03, 0, 0, 0, 0x00})) &&
          leave[1] == with_header(2981, cat({0x07, 0, 0, 0, 0x03, 0, 0, 0})));
    const auto idle = gsb::native_answer(owner, scene, 1, true, true);
    CHECK(idle.size() == 2 && idle[1] == with_header(2981, cat({0, 0, 0, 0, 0x01, 0, 0, 0, 0x9D, 0x90, 0x9C, 0x97, 0, 0, 0, 0})));
    const auto buy = gsb::native_answer(owner, scene, 0, true);
    CHECK(buy.size() == 2 && buy[1] == with_header(2981, cat({0x03, 0, 0, 0, 0, 0, 0, 0})));
    const auto sell = gsb::native_answer(owner, scene, 1, true, false);
    CHECK(sell.size() == 2 && sell[1] == with_header(2981, cat({0x04, 0, 0, 0, 0x01, 0, 0, 0})));
}

void test_parsers() {
    // The guest's 2916 of the night: requester 0xA0100066 on the grocer 0xB0100BD5, interaction 0x000F4321.
    const auto dostep = with_header(2916, {0x66, 0x00, 0x10, 0xA0, 0x66, 0x00, 0x10, 0xA0, 0xD5, 0x0B, 0x10, 0xB0,
                                           0x21, 0x43, 0x0F, 0x00, 0, 0, 0, 0});
    const auto d = gsb::parse_dostep(dostep);
    CHECK(d && d->requester == 0xA0100066 && d->target == 0xB0100BD5 && d->interaction == gsb::kShopInteraction);
    CHECK(!gsb::parse_dostep(with_header(2916, {1, 2, 3})));
    CHECK(!gsb::parse_dostep(with_header(2794, std::vector<uint8_t>(20))));
    // The host's 2436 for the grocery scene.
    const auto req = with_header(2436, {0xAF, 0x01, 0, 0, 0, 0, 0, 0, 0x06, 0, 0, 0, 0, 0});
    const auto b = gsb::parse_branch_req(req);
    CHECK(b && b->first == 0x1AF && b->second == 6);
    // A 2661: kind | actor | owner | scene ...
    std::vector<uint8_t> prep = {0x04, 0x01, 0x00, 0x10, 0xA0, 0x01, 0x00, 0x10, 0xA0, 0xB2, 0x01, 0, 0, 0, 0, 0, 0, 0x00};
    const auto pr = gsb::parse_prepare(with_header(2661, prep));
    CHECK(pr && pr->first == 0x1B2 && pr->second == 0xA0100001);
}

}  // namespace

int main() {
    test_built_like_the_server();
    test_menu_answer_like_the_server();
    test_native_answers();
    test_parsers();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_guest_shop_branch: all passed\n");
    return 0;
}
