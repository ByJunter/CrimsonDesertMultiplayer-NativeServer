// Option D, companions step A: the companion probe's pure logic
// (src/mod/session2/companion_probe.cpp) on fake game memory: key classes, the
// owner predicate and the control component -> actor/restore/layers reads.
// The hooks themselves need the game.

#include <cstdio>
#include <cstring>

#include "mod/session2/companion_probe.h"

namespace cdmp::session2 {
// The real ones are in server.cpp (need the game); the logic tested here does not use them.
bool server_host_user(uintptr_t&) { return false; }
bool server_user2(uintptr_t&) { return false; }
}  // namespace cdmp::session2

using namespace cdmp::session2;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

template <class T>
void put(uint8_t* bytes, uint32_t off, T v) {
    std::memcpy(bytes + off, &v, sizeof v);
}

const uint64_t g_vt_block[4] = {};
const uintptr_t kVt = reinterpret_cast<uintptr_t>(g_vt_block);

struct FakeLayer {
    alignas(8) uint8_t bytes[0x120] = {};
    uintptr_t addr() { return reinterpret_cast<uintptr_t>(bytes); }
};

// A client actor with its static info, and its control component with restore data and two layers.
struct FakeCharacter {
    alignas(8) uint8_t actor[0x100] = {};
    alignas(8) uint8_t info[0x10] = {};
    alignas(8) uint8_t cc[0x100] = {};
    alignas(8) uint8_t restore[0x40] = {};
    FakeLayer layer[2];
    uint8_t dummy = 0;
    FakeCharacter(uint32_t key, uint32_t owner) {
        put(actor, 0, kVt);
        put(actor, cmp::kActorKey, key);
        put(actor, cmp::kActorOwnerUser, owner);
        put(actor, cmp::kActorInfo, reinterpret_cast<uintptr_t>(info));
        info[0] = 0x07;
        info[1] = 0x00;
        put(cc, 0, kVt + 8);
        put(cc, cmp::kCcActor, reinterpret_cast<uintptr_t>(actor));
        put(cc, cmp::kCcRestore, reinterpret_cast<uintptr_t>(restore));
        put(cc, cmp::kCcPending, uint8_t{0});
        for (int i = 0; i < 2; ++i) put(cc, cmp::kCcLayer0 + 8 * static_cast<uint32_t>(i), layer[i].addr());
    }
    uintptr_t cc_addr() { return reinterpret_cast<uintptr_t>(cc); }
    uintptr_t some() { return reinterpret_cast<uintptr_t>(&dummy); }
};

void test_key_class() {
    // Keys of the logs and captures (companeros.md, diary C2-0).
    CHECK(comp_key_class(0x9010052B) == KeyClass::User);
    CHECK(comp_key_class(0x901002BF) == KeyClass::User);
    CHECK(comp_key_class(0xA01002C0) == KeyClass::Character);
    CHECK(comp_key_class(0xA0100001) == KeyClass::Character);
    CHECK(comp_key_class(0xB010052D) == KeyClass::Actor);
    CHECK(comp_key_class(0xB0100234) == KeyClass::Actor);
    CHECK(comp_key_class(0xB01005B2) == KeyClass::Actor);  // an NPC: same class, told apart by its owner
    CHECK(comp_key_class(0x80000001) == KeyClass::Other);
    // The same bit test as 0x8AF5D2: (owner & 0x30000000) == 0x10000000.
    for (uint32_t k : {0x9010052Bu, 0xA0100001u, 0xB0100002u, 0x8000FFFFu}) {
        CHECK((comp_key_class(k) == KeyClass::User) == ((k & 0x30000000u) == 0x10000000u));
    }
    CHECK(std::strcmp(comp_key_class_name(0), "no key") == 0);
    CHECK(std::strcmp(comp_key_class_name(0xA0100001), "player character") == 0);
    CHECK(std::strcmp(comp_key_class_name(0xB0100002), "actor") == 0);
}

void test_who() {
    constexpr uint32_t host = 0x90100001, user2 = 0x9010052B;
    CHECK(comp_who(0, host, user2) == CompWho::None);
    CHECK(comp_who(host, host, user2) == CompWho::Host);
    CHECK(comp_who(user2, host, user2) == CompWho::User2);
    CHECK(comp_who(0x90100777, host, user2) == CompWho::OtherUser);
    // User 2 not readable yet: anyone but the host is "another user".
    CHECK(comp_who(user2, host, 0) == CompWho::OtherUser);
    CHECK(comp_who(host, host, 0) == CompWho::Host);
    // The host not readable: never claims it is the host.
    CHECK(comp_who(host, 0, user2) == CompWho::OtherUser);
    CHECK(comp_who(user2, 0, user2) == CompWho::User2);
}

void test_read_control() {
    FakeCharacter c(0xB0100234, 0x9010052B);
    // C2-0's crash state: restore 0, layer 0 with a chart and nothing playing.
    put(c.layer[0].bytes, cmp::kLayerChart, c.some());
    put(c.layer[1].bytes, cmp::kLayerChart, c.some());
    put(c.layer[1].bytes, cmp::kLayerCurrent, c.some());
    put(c.layer[1].bytes, cmp::kLayerPlayback, c.some());
    c.restore[cmp::kRestoreByte] = 0;
    CompControl r;
    CHECK(comp_read_control(c.cc_addr(), r));
    CHECK(r.actor == reinterpret_cast<uintptr_t>(c.actor));
    CHECK(r.key == 0xB0100234);
    CHECK(r.owner == 0x9010052B);
    CHECK(r.actor_vt == kVt);
    CHECK(r.cc_vt == kVt + 8);
    CHECK(r.info_ok && r.info_bytes[0] == 0x07 && r.info_bytes[1] == 0);
    CHECK(r.restore == 0);
    CHECK(r.pending == 0);
    CHECK(r.layer[0].present && r.layer[0].chart && !r.layer[0].current && !r.layer[0].playback);
    CHECK(r.layer[0].would_crash());
    CHECK(r.layer[1].present && !r.layer[1].would_crash());

    // After a restore 1: the layer has its action.
    c.restore[cmp::kRestoreByte] = 1;
    put(c.layer[0].bytes, cmp::kLayerCurrent, c.some());
    CHECK(comp_read_control(c.cc_addr(), r));
    CHECK(r.restore == 1);
    CHECK(!r.layer[0].would_crash());

    // No chart: nothing to crash on, with or without an action.
    put(c.layer[0].bytes, cmp::kLayerChart, uintptr_t{0});
    put(c.layer[0].bytes, cmp::kLayerCurrent, uintptr_t{0});
    CHECK(comp_read_control(c.cc_addr(), r));
    CHECK(!r.layer[0].would_crash());

    // A missing layer and missing restore data are reported as such, not as zeros.
    put(c.cc, cmp::kCcLayer0 + 8, uintptr_t{0});
    put(c.cc, cmp::kCcRestore, uintptr_t{0});
    CHECK(comp_read_control(c.cc_addr(), r));
    CHECK(!r.layer[1].present && !r.layer[1].would_crash());
    CHECK(r.restore == -1);

    // No actor / no CC / unreadable CC.
    put(c.cc, cmp::kCcActor, uintptr_t{0});
    CHECK(!comp_read_control(c.cc_addr(), r));
    CHECK(!comp_read_control(0, r));
    CHECK(!comp_read_control(0x10, r));
}

}  // namespace

int main() {
    test_key_class();
    test_who();
    test_read_control();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_companion: all passed\n");
    return 0;
}
