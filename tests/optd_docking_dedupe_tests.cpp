// The guest's dedupe of the repeated equipment-docking requests (src/mod/session2/docking_dedupe_logic.h).

#include <cstdint>
#include <cstdio>
#include <vector>

#include "mod/session2/docking_dedupe_logic.h"

using namespace cdmp::session2;

namespace {
int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++g_failures; } } while (0)

// Two requests of the 2026-11-05 capture (header + payload).
const std::vector<uint8_t> kHostKliff = {0xF0, 0x08, 0x00, 0x15, 0x00, 0x04, 0x01, 0x00, 0x10, 0xA0, 0x00, 0x00, 0x01,
                                         0x00, 0x09, 0x00, 0x0D, 0x00, 0x0F, 0x00, 0x11, 0x00, 0x14, 0x00, 0x18, 0x00};
const std::vector<uint8_t> kPiece = {0xF0, 0x08, 0x00, 0x07, 0x00, 0x04, 0x35, 0x27, 0x10, 0xB0, 0x0E, 0x00};

void test_repeats_within_the_window() {
    ddd::Dedupe d;
    const uint64_t a = ddd::fingerprint(kHostKliff);
    CHECK(!d.drop(a, 1000));
    CHECK(d.drop(a, 1100));
    CHECK(d.drop(a, 1000 + ddd::kWindowMs - 1));
    CHECK(!d.drop(a, 1000 + ddd::kWindowMs));  // once per window
    CHECK(d.drop(a, 1000 + ddd::kWindowMs + 100));
}

void test_another_request_goes_at_once() {
    ddd::Dedupe d;
    CHECK(!d.drop(ddd::fingerprint(kHostKliff), 1000));
    CHECK(!d.drop(ddd::fingerprint(kPiece), 1001));
    CHECK(ddd::fingerprint(kHostKliff) != ddd::fingerprint(kPiece));
}

void test_bounded() {
    ddd::Dedupe d;
    for (uint64_t i = 0; i < 100; ++i) CHECK(!d.drop(0x1000 + i, 5000 + i));
    CHECK(d.drop(0x1000 + 99, 5200));  // the newest are kept
    d.reset();
    CHECK(!d.drop(0x1000 + 99, 5300));
}

}  // namespace

int main() {
    test_repeats_within_the_window();
    test_another_request_goes_at_once();
    test_bounded();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("docking dedupe: ok\n");
    return 0;
}
