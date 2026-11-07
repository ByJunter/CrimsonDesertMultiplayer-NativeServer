// The rule of the field leave guard (src/mod/session2/field_leave_guard_logic.h), with the values ManagerProbe logged when the
// guest left on 2026-11-05: host's manager 0x2FE061EE700, user 2's 0x2FE72BFC400; host's user key 0x90100000, user 2's 0x901004D7.

#include <cstdint>
#include <cstdio>

#include "mod/session2/field_leave_guard_logic.h"

using namespace cdmp::session2;

namespace {
int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++g_failures; } } while (0)

struct FakeContext {
    uintptr_t rax = 0;
    uintptr_t rip = 0;
};

constexpr uintptr_t kBase = 0x140000000;
constexpr uintptr_t kNative = kBase + flg::kPoint;
constexpr uintptr_t kHostManager = 0x2FE061EE700, kGuestManager = 0x2FE72BFC400;
constexpr uint32_t kHost = 0x90100000, kGuest = 0x901004D7;

void test_the_guest_leaves_the_hosts_field() {
    FakeContext c{kHostManager, kNative};
    CHECK(flg::skip_owner_unload(c, kBase, kGuestManager, kGuest, kHost));
    CHECK(c.rip == kBase + flg::kOwnCall);  // its own manager still unloads there
}

void test_one_player_is_untouched() {
    FakeContext c{kHostManager, kNative};
    CHECK(!flg::skip_owner_unload(c, kBase, kHostManager, kHost, kHost));
    CHECK(c.rip == kNative);
}

void test_the_guest_leaves_its_own_field() {
    FakeContext c{kGuestManager, kNative};
    CHECK(!flg::skip_owner_unload(c, kBase, kGuestManager, kGuest, kGuest));
    CHECK(c.rip == kNative);
}

// The same user's other character, with a manager of its own: as the game does it.
void test_the_owners_other_character_stays_native() {
    FakeContext c{kHostManager, kNative};
    CHECK(!flg::skip_owner_unload(c, kBase, 0x2FE0AAAA000, kHost, kHost));
    CHECK(c.rip == kNative);
}

void test_anything_unread_stays_native() {
    FakeContext c{kHostManager, kNative};
    CHECK(!flg::skip_owner_unload(c, kBase, 0, kGuest, kHost));
    CHECK(!flg::skip_owner_unload(c, kBase, kGuestManager, 0, kHost));
    CHECK(!flg::skip_owner_unload(c, kBase, kGuestManager, kGuest, 0));
    CHECK(c.rip == kNative);
}

void test_no_owner_manager() {
    FakeContext c{0, kNative};
    CHECK(!flg::skip_owner_unload(c, kBase, kGuestManager, kGuest, kHost));
    CHECK(c.rip == kNative);
}

void test_code_tables() {
    CHECK(sizeof flg::kPointBytes == flg::kPatch);
    CHECK(flg::kPoint + flg::kPatch <= flg::kOwnCall);
    CHECK(flg::kEntry < flg::kPoint);
}

}  // namespace

int main() {
    test_the_guest_leaves_the_hosts_field();
    test_one_player_is_untouched();
    test_the_guest_leaves_its_own_field();
    test_the_owners_other_character_stays_native();
    test_anything_unread_stays_native();
    test_no_owner_manager();
    test_code_tables();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("field leave guard: ok\n");
    return 0;
}
