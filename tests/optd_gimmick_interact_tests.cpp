// The guest's interaction on gimmicks the host controls (src/mod/session2/gimmick_interact_logic.h).

#include <cstdint>
#include <cstdio>

#include "mod/session2/gimmick_interact_logic.h"

using namespace cdmp::session2;

namespace {
int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++g_failures; } } while (0)

void test_modes() {
    CHECK(gil::parse_mode("") == gil::Mode::Off);
    CHECK(gil::parse_mode("0") == gil::Mode::Off);
    CHECK(gil::parse_mode("trace") == gil::Mode::Trace);
    CHECK(gil::parse_mode("probe") == gil::Mode::Trace);
    CHECK(gil::parse_mode("1") == gil::Mode::On);
    CHECK(!gil::parse_mode("2"));
    CHECK(!gil::parse_mode("on"));
}

void test_events() {
    const auto e = gil::parse_events(" 0x128B06E9, 17ca7748 ,,0x128b06e9");
    CHECK(e && e->n == 2);
    CHECK(e && e->contains(0x128B06E9) && e->contains(0x17CA7748) && !e->contains(0x1E3E141C));
    CHECK(gil::parse_events("") && gil::parse_events("")->n == 0);
    CHECK(!gil::parse_events("0x12G4"));
    CHECK(!gil::parse_events("0x123456789"));  // more than 32 bits
    CHECK(!gil::parse_events("1,2,3,4,5,6,7,8,9,a,b,c,d,e,f,10,11"));  // more than kMaxEvents
}

void test_allow() {
    const auto e = gil::parse_events("0x128B06E9");
    CHECK(e);
    CHECK(gil::allow(gil::Mode::On, *e, 0x128B06E9));
    CHECK(!gil::allow(gil::Mode::Trace, *e, 0x128B06E9));  // trace never changes anything
    CHECK(!gil::allow(gil::Mode::Off, *e, 0x128B06E9));
    CHECK(!gil::allow(gil::Mode::On, *e, 0x17CA7748));
    CHECK(!gil::allow(gil::Mode::On, *e, 0));
    CHECK(!gil::allow(gil::Mode::On, gil::Events{}, 0x128B06E9));  // an empty list allows nothing
}

void test_seen() {
    gil::Seen<16> s;
    CHECK(s.note(gil::pair_key(1, 2)) == 0);
    CHECK(s.note(gil::pair_key(1, 2)) == 1);
    CHECK(s.note(gil::pair_key(1, 2)) == 2);
    CHECK(s.note(gil::pair_key(2, 1)) == 0);
    for (uint32_t i = 10; i < 40; ++i) (void)s.note(i);
    CHECK(s.used() == 12);                              // three quarters of 16
    CHECK(s.note(1000) == gil::Seen<16>::kFull);        // full: new keys are not taken
    CHECK(s.note(gil::pair_key(1, 2)) == 3);            // known keys still count
}

}  // namespace

int main() {
    test_modes();
    test_events();
    test_allow();
    test_seen();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_gimmick_interact: all passed\n");
    return 0;
}
