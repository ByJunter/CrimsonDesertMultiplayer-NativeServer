// The rule of the achievement guard (src/mod/session2/achievement_guard_logic.h).

#include <cstdint>
#include <cstdio>

#include "mod/session2/achievement_guard_logic.h"

using namespace cdmp::session2;

namespace {
int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++g_failures; } } while (0)

struct FakeContext {
    uintptr_t rip = 0;
};

constexpr uintptr_t kBase = 0x140000000;

void test_alone_unlocks() {
    agd::Gate g;
    CHECK(!g.blocks(false));
    FakeContext c{kBase + agd::kPoint};
    CHECK(!agd::skip_unlock(c, kBase, g.blocks(false)));
    CHECK(c.rip == kBase + agd::kPoint);
}

// 2026-11-05: the guest got the host's achievements the moment it joined.
void test_in_the_hosts_world_nothing_unlocks() {
    agd::Gate g;
    FakeContext c{kBase + agd::kPoint};
    CHECK(agd::skip_unlock(c, kBase, g.blocks(true)));
    CHECK(c.rip == kBase + agd::kWayOut);
}

void test_still_blocked_after_leaving() {
    agd::Gate g;
    (void)g.blocks(true);
    CHECK(g.blocks(false));  // the host's progress stays in memory until the game restarts
    CHECK(g.blocked());
}

static_assert(sizeof agd::kPointBytes == agd::kPatch);
static_assert(agd::kUnlock < agd::kPoint && agd::kPoint < agd::kWayOut);
static_assert(agd::kPoint - agd::kUnlock == sizeof agd::kUnlockBytes + 0x18);  // the pushes, then lea, sub and three movs

}  // namespace

int main() {
    test_alone_unlocks();
    test_in_the_hosts_world_nothing_unlocks();
    test_still_blocked_after_leaving();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("achievement guard: ok\n");
    return 0;
}
