// S46 phase 1: the pure headers ported BY HAND from the other AI's branch opcion-d-tenderos (a294fac; it cannot be
// merged): shop_troc_logic.h, the 3000 layout of shop_probe_logic.h (the p2b fixture) and funcnpc_trade_policy.h (the
// trade list, now with the resources' hashes). Offline logic only: no hook, no game.
#include <array>
#include <cstdio>

#include "mod/session2/funcnpc_trade_policy.h"
#include "mod/session2/shop_probe_logic.h"
#include "mod/session2/shop_troc_logic.h"

using namespace cdmp::session2::shop_troc;
namespace shp = cdmp::session2::shp;
namespace u2trade = cdmp::session2::u2trade;

int main() {
    int bad = 0;
#define CHECK(x)                                              \
    do {                                                      \
        if (!(x)) {                                           \
            std::printf("FAIL %d: %s\n", __LINE__, #x);       \
            ++bad;                                            \
        }                                                     \
    } while (0)
    // The p2b FailAck 3000 (host capture offset 137772823), not the success layout with two requester keys.
    constexpr std::array<uint8_t, 23> fail_packet{0x01, 0x00, 0x10, 0xA0, 0xEC, 0x02, 0x10, 0xB0, 0x21, 0x43, 0x0F, 0x00,
                                                  0x5D, 0x34, 0xC8, 0x58, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0x00};
    const auto fail_ack = shp::parse_interaction_ack(fail_packet, shp::kOpPreemptionFail);
    CHECK(fail_ack.ok && fail_ack.requester == 0xA0100001 && fail_ack.npc == 0xB01002EC);
    CHECK(fail_ack.interaction == 0x000F4321);
    CHECK(shp::fail_ack_error(fail_packet) == 0x58C8345D);
    for (size_t n = 0; n < 16; ++n) {
        CHECK(!shp::parse_interaction_ack(std::span<const uint8_t>(fail_packet.data(), n), shp::kOpPreemptionFail).ok);
    }
    CHECK(!shp::parse_interaction_ack(fail_packet, 65535).ok);
    CHECK(retained_dispatch(0x27742AE));
    CHECK(retained_dispatch(0x2774A0D));
    CHECK(!retained_dispatch(0x27742A9));
    CHECK(!retained_dispatch(0x2774A08));
    CHECK(!retained_dispatch(0));
    MutationFacts f;
    CHECK(!may_enter(f));
    f = {true, true, true, true, 1};
    CHECK(may_enter(f));
    f.on = false;
    CHECK(!may_enter(f));
    f.on = true;
    f.guest = false;
    CHECK(!may_enter(f));
    f.guest = true;
    f.current = false;
    CHECK(!may_enter(f));
    f.current = true;
    f.retained = false;
    CHECK(!may_enter(f));
    f.retained = true;
    f.in_flight = 2;
    CHECK(!may_enter(f));
    f.in_flight = 0;
    CHECK(!may_enter(f));
    CHECK(recovery_ready(true, true, true, true));
    CHECK(!recovery_ready(false, true, true, true));
    CHECK(!recovery_ready(true, false, true, true));
    CHECK(!recovery_ready(true, true, false, true));
    CHECK(!recovery_ready(true, true, true, false));
    CHECK(abort_needs_drain(false, true, true, true));
    CHECK(!abort_needs_drain(false, true, false, true));
    CHECK(!abort_needs_drain(false, true, true, false));
    CHECK(!abort_needs_drain(false, false, true, true));
    CHECK(abort_needs_drain(true, false, false, false));
    ArmRequest a;
    a.asked(20'000'000);
    a.observe(true, true);
    CHECK(a.due(20'000'001) && a.fast_pending);
    for (int i = 0; i < 100; ++i) {
        a.observe(false, true);
        CHECK(a.fast_pending && a.last_asked == 20'000'000);
    }
    CHECK(a.due(21'000'000));
    a.asked(21'000'000);
    CHECK(!a.fast_pending && !a.due(21'000'001));
    CHECK(a.due(31'000'000));
    a.observe(true, true);
    a.observe(false, false);
    CHECK(!a.fast_pending);
    DrainRequest d;
    d.asked(50'000'000);
    CHECK(!d.due(50'000'001));
    d.observe(true);
    CHECK(d.due(50'000'001));
    for (int i = 0; i < 100; ++i) {
        d.observe(true);
        CHECK(d.due(50'000'002));
    }
    d.asked(50'000'003);
    d.observe(true);
    CHECK(!d.due(50'000'004));
    CHECK(d.due(60'000'003));
    d.observe(false);
    d.observe(true);
    CHECK(d.due(50'000'005));
    // The trade list: exact names and hashes (funcnpc.json), nothing from the prefix.
    CHECK(u2trade::is_trade_resource("cd_seq_funcnpc_smithy"));
    CHECK(u2trade::is_trade_resource("cd_seq_funcnpc_butcher"));
    CHECK(!u2trade::is_trade_resource("cd_seq_funcnpc_"));
    CHECK(!u2trade::is_trade_resource("cd_seq_funcnpc_smithy_01"));
    CHECK(!u2trade::is_trade_resource("cd_seq_funcnpc_grocery"));
    CHECK(u2trade::is_trade_hash(0xA7132561) && u2trade::is_trade_hash(0x978345CA));
    CHECK(!u2trade::is_trade_hash(0) && !u2trade::is_trade_hash(0x0E965A02));
    CHECK(u2trade::trade_name(0xA7132561) == "cd_seq_funcnpc_smithy");
    CHECK(u2trade::trade_name(0x12345678).empty());
    std::printf("optd_shop_troc: %s\n", bad ? "FAIL" : "OK");
    return bad ? 1 : 0;
}
