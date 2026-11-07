// The rule of the ChangePivot guard (src/mod/session2/pivot_guard_logic.h), with the registers of the crash dump of
// 2026-11-05 (r14 0, [rsp+0x78] 0).

#include <cstdint>
#include <cstdio>

#include "mod/session2/pivot_guard_logic.h"

using namespace cdmp::session2;

namespace {
int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++g_failures; } } while (0)

struct FakeContext {
    uintptr_t r14 = 0;
    uintptr_t rip = 0;
};

constexpr uintptr_t kBase = 0x140000000;
constexpr uintptr_t kNative = kBase + pvg::kPoint;

void test_the_crash() {
    FakeContext c{0, kNative};
    CHECK(pvg::skip_no_targets(c, kBase, 0));
    CHECK(c.rip == kBase + pvg::kExit);
}

void test_targets_are_untouched() {
    FakeContext c{0x536BB3E82C0, kNative};
    CHECK(!pvg::skip_no_targets(c, kBase, 1));
    CHECK(c.rip == kNative);
    CHECK(c.r14 == 0x536BB3E82C0);
}

// An array with no elements: the way out frees it (0x2205A08 reads the count, 0x2205A29 frees r14), element 0 is garbage.
void test_allocated_but_empty() {
    FakeContext c{0x536BB3E82C0, kNative};
    CHECK(pvg::skip_no_targets(c, kBase, 0));
    CHECK(c.rip == kBase + pvg::kExit);
    CHECK(c.r14 == 0x536BB3E82C0);  // left for the way out to free
}

// A count with no array cannot be used either.
void test_count_without_array() {
    FakeContext c{0, kNative};
    CHECK(pvg::skip_no_targets(c, kBase, 3));
    CHECK(c.rip == kBase + pvg::kExit);
}

static_assert(pvg::kPatch >= 5);  // room for safetyhook's jump
static_assert(sizeof pvg::kPointBytes == pvg::kPatch);
static_assert(pvg::kPoint > pvg::kEntry && pvg::kExit > pvg::kPoint);
}  // namespace

int main() {
    test_the_crash();
    test_targets_are_untouched();
    test_allocated_but_empty();
    test_count_without_array();
    if (g_failures) {
        std::printf("optd_pivot_guard: FAILED (%d failures)\n", g_failures);
        return 1;
    }
    std::printf("optd_pivot_guard: all OK\n");
    return 0;
}
