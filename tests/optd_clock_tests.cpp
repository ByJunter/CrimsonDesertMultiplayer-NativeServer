// Option D, P-RELOJ: the clock gate's logic (src/mod/session2/clock_gate.cpp)
// on fake game memory: a TransformSync with its character and user, and a
// fake GetCurrentFieldTime. The hooks themselves need the game.

#include <cstdio>
#include <cstring>

#include "mod/session2/clock_gate.h"

namespace cdmp::session2 {
// The real ones are in server.cpp (need the game); the logic tested here does not use them.
bool server_users(uintptr_t&, uintptr_t&) { return false; }
bool server_host_user(uintptr_t&) { return false; }
bool server_user2(uintptr_t&) { return false; }
// clock_push.cpp (P8 session 25) needs the server too; its logic is tested in optd_clock_push_tests.cpp.
void clock_push_tick(uint64_t) noexcept {}
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

struct FakeUser {
    uint8_t bytes[0x100] = {};
    explicit FakeUser(uint32_t key = 0) { std::memcpy(bytes + clk::kUserKey, &key, sizeof key); }
};
struct FakeActor {
    uint8_t bytes[0x100] = {};
    void set_user(const void* u) { std::memcpy(bytes + clk::kActorUser, &u, sizeof u); }
};
struct FakeTs {
    uint8_t bytes[0x500] = {};
    void set_actor(const void* a) { std::memcpy(bytes + clk::kTsActor, &a, sizeof a); }
    uintptr_t addr() { return reinterpret_cast<uintptr_t>(bytes); }
};

// GetCurrentFieldTime: day 6 13:37:44.760, scale 12, anchor 0x18A12 (C1g's world clock).
int g_getter_calls = 0;
void* g_getter_field = nullptr;
void* __fastcall fake_field_time(void* field, void* out) {
    ++g_getter_calls;
    g_getter_field = field;
    uint8_t ft[clk::kFieldTimeSize] = {};
    const uint32_t v[5] = {6, 13, 37, 44, 760};
    std::memcpy(ft, v, sizeof v);
    ft[0x14] = 6;
    const uint16_t scale = 12;
    std::memcpy(ft + 0x16, &scale, sizeof scale);
    const uint64_t anchor = 0x18A12;
    std::memcpy(ft + 0x18, &anchor, sizeof anchor);
    std::memcpy(out, ft, sizeof ft);
    return out;
}
void* __fastcall null_field_time(void*, void*) { return nullptr; }

void test_who() {
    constexpr uint32_t kHostKey = 0x90100000, kUser2Key = 0x90100001;
    FakeUser host(kHostKey), user2(kUser2Key), other(0x90100002);
    FakeActor a_host, a_user2, a_other, a_none;
    a_host.set_user(&host);
    a_user2.set_user(&user2);
    a_other.set_user(&other);
    FakeTs ts_host, ts_user2, ts_other, ts_none, ts_empty;
    ts_host.set_actor(&a_host);
    ts_user2.set_actor(&a_user2);
    ts_other.set_actor(&a_other);
    ts_none.set_actor(&a_none);
    const auto u2 = reinterpret_cast<uintptr_t>(&user2);
    CHECK(clock_who(ts_user2.addr(), kHostKey, u2) == ClockWho::User2);
    CHECK(clock_who(ts_host.addr(), kHostKey, u2) == ClockWho::Host);
    CHECK(clock_who(ts_none.addr(), kHostKey, u2) == ClockWho::Other);   // a character without a user
    CHECK(clock_who(ts_empty.addr(), kHostKey, u2) == ClockWho::Other);  // no character
    CHECK(clock_who(0, kHostKey, u2) == ClockWho::Other);
    // Fail safe: session 2's lookup missed (0) or another user: gated all the same.
    CHECK(clock_who(ts_user2.addr(), kHostKey, 0) == ClockWho::User2);
    CHECK(clock_who(ts_other.addr(), kHostKey, u2) == ClockWho::User2);
    CHECK(clock_who(ts_host.addr(), kHostKey, 0) == ClockWho::Host);
    // The host not known yet (the game's first load): only user 2 itself is gated.
    CHECK(clock_who(ts_host.addr(), 0, 0) == ClockWho::Other);
    CHECK(clock_who(ts_host.addr(), 0, u2) == ClockWho::Other);
    CHECK(clock_who(ts_user2.addr(), 0, u2) == ClockWho::User2);
}

void test_travel() {
    FakeUser user2;
    FakeActor actor;
    actor.set_user(&user2);
    FakeTs ts;
    ts.set_actor(&actor);
    std::memset(ts.bytes + clk::kTsFieldTime, 0xAB, clk::kFieldTimeSize);
    std::memset(ts.bytes + clk::kTsFieldTime + clk::kFieldTimeSize, 0xCD, 8);
    uint8_t field[0x300] = {};
    ClockGame g{&fake_field_time};
    CHECK(clock_user2_travel(g, ts.addr(), reinterpret_cast<uintptr_t>(field)));
    CHECK(g_getter_calls == 1 && g_getter_field == field);
    uint32_t day = 0, hour = 0;
    uint16_t scale = 0;
    std::memcpy(&day, ts.bytes + clk::kTsFieldTime, 4);
    std::memcpy(&hour, ts.bytes + clk::kTsFieldTime + 4, 4);
    std::memcpy(&scale, ts.bytes + clk::kTsFieldTime + 0x16, 2);
    CHECK(day == 6 && hour == 13 && scale == 12);
    // Exactly 32 bytes: what follows TS+0x450 is untouched.
    CHECK(ts.bytes[clk::kTsFieldTime + clk::kFieldTimeSize] == 0xCD);
    CHECK(ts.bytes[clk::kTsFieldTime + clk::kFieldTimeSize + 7] == 0xCD);

    // Failures write nothing.
    std::memset(ts.bytes + clk::kTsFieldTime, 0xAB, clk::kFieldTimeSize);
    ClockGame bad{&null_field_time};
    CHECK(!clock_user2_travel(bad, ts.addr(), reinterpret_cast<uintptr_t>(field)));
    CHECK(!clock_user2_travel(g, ts.addr(), 0));
    CHECK(!clock_user2_travel(g, 0, reinterpret_cast<uintptr_t>(field)));
    CHECK(!clock_user2_travel(ClockGame{}, ts.addr(), reinterpret_cast<uintptr_t>(field)));
    CHECK(ts.bytes[clk::kTsFieldTime] == 0xAB && ts.bytes[clk::kTsFieldTime + 31] == 0xAB);
}

void test_loading_flag() {
    CHECK(clock_loading_flag(ClockWho::User2, 1) == 0);
    CHECK(clock_loading_flag(ClockWho::User2, 0) == 0);
    CHECK(clock_loading_flag(ClockWho::Host, 1) == 1);
    CHECK(clock_loading_flag(ClockWho::Other, 1) == 1);
    CHECK(clock_loading_flag(ClockWho::Host, 0) == 0);
}

}  // namespace

int main() {
    test_who();
    test_travel();
    test_loading_flag();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_clock: all tests passed\n");
    return 0;
}
