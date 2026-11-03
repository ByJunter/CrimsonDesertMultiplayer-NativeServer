// Two copies of the game on one PC, [general] background_full_speed (src/mod/bg_speed_logic.h,
// docs/research/dos-instancias.md §2.6): the gate (role only), who has the foreground, the GPU class decisions over
// focus switches, the fallback when a class is refused, the give-up cap and the time/switch counters.
//
// SCOPE: pure logic. Informative only (never fails the test): whether this PC accepts the High GPU scheduling class
// for this test process without admin, set back afterwards. Not the game, not the watch thread.

#include <windows.h>

#include <cstdio>
#include <string>

#include "mod/bg_speed_logic.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

namespace bg = cdmp::bgspeed;
using bg::Fg;
using bg::Gate;

void test_gate() {
    CHECK(bg::gate(false, false, true) == Gate::Off);
    CHECK(bg::gate(false, true, true) == Gate::Off);
    CHECK(bg::gate(true, false, true) == Gate::IgnoredNoRole);  // one copy per PC: a WRN, nothing done
    CHECK(bg::gate(true, false, false) == Gate::IgnoredNoRole);
    CHECK(bg::gate(true, true, false) == Gate::NotInGame);
    CHECK(bg::gate(true, true, true) == Gate::On);
    std::printf("Gate: OK\n");
}

void test_classify() {
    CHECK(bg::classify(false, 100, 100) == Fg::Own);
    CHECK(bg::classify(false, 200, 100) == Fg::Other);
    CHECK(bg::classify(true, 0, 100) == Fg::None);
    CHECK(bg::classify(false, 0, 100) == Fg::None);  // a window whose owner could not be read
    std::printf("Foreground classification: OK\n");
}

void test_next_boost() {
    CHECK(bg::next_boost(bg::kHigh, bg::kNormal) == bg::kAboveNormal);
    CHECK(bg::next_boost(bg::kAboveNormal, bg::kNormal) == bg::kUnknown);
    CHECK(bg::next_boost(bg::kHigh, bg::kAboveNormal) == bg::kUnknown);  // never at or below the original
    CHECK(bg::next_boost(bg::kRealtime, bg::kNormal) == bg::kUnknown);
    std::printf("Boost fallback: OK\n");
}

// Focus switches: background -> High, foreground -> the original; nothing to do while the state holds.
void test_switches() {
    bg::Controller c(bg::kNormal);
    CHECK(c.applied() == bg::kNormal && c.boost() == bg::kHigh);
    // Starts in the foreground: nothing to set.
    CHECK(c.poll(Fg::Own, 1000) == bg::kUnknown);
    CHECK(!c.background());
    CHECK(c.poll(Fg::Own, 1250) == bg::kUnknown);
    // Another window: High.
    CHECK(c.poll(Fg::Other, 1500) == bg::kHigh);
    c.result(bg::kHigh, true);
    CHECK(c.applied() == bg::kHigh && c.background());
    CHECK(c.poll(Fg::Other, 1750) == bg::kUnknown);  // already there
    CHECK(c.poll(Fg::None, 2000) == bg::kUnknown);   // no foreground window: still background, no change
    // Back in the foreground: the original.
    CHECK(c.poll(Fg::Own, 3000) == bg::kNormal);
    c.result(bg::kNormal, true);
    CHECK(c.applied() == bg::kNormal && !c.background());
    CHECK(c.poll(Fg::Own, 4000) == bg::kUnknown);
    CHECK(c.to_background() == 1 && c.to_foreground() == 1);
    // Time: foreground 1000..1500 and 3000..4000 = 1500 ms; background 1500..3000 = 1500 ms.
    CHECK(c.foreground_ms() == 1500 && c.background_ms() == 1500);
    CHECK(c.set_ok() == 2 && c.set_failed() == 0 && !c.gave_up());
    const std::string d = c.describe();
    CHECK(d.find("foreground now") != std::string::npos && d.find("GPU class Normal") != std::string::npos);
    std::printf("Focus switches: OK (%s)\n", d.c_str());
}

// Starting in the background (the other copy was launched later and took the foreground).
void test_start_background() {
    bg::Controller c(bg::kNormal);
    CHECK(c.poll(Fg::Other, 0) == bg::kHigh);
    c.result(bg::kHigh, true);
    CHECK(c.to_background() == 0);  // the first poll is not a switch
    std::printf("Start in the background: OK\n");
}

// High refused -> Above normal; that refused too -> no boost left: the original stays, nothing more is asked.
void test_refused() {
    bg::Controller c(bg::kNormal);
    CHECK(c.poll(Fg::Other, 0) == bg::kHigh);
    c.result(bg::kHigh, false);
    CHECK(c.boost() == bg::kAboveNormal && c.applied() == bg::kNormal);
    CHECK(c.poll(Fg::Other, 250) == bg::kAboveNormal);
    c.result(bg::kAboveNormal, true);
    CHECK(c.applied() == bg::kAboveNormal);
    CHECK(c.poll(Fg::Own, 500) == bg::kNormal);
    c.result(bg::kNormal, true);
    CHECK(c.poll(Fg::Other, 750) == bg::kAboveNormal);  // the lower boost is kept

    bg::Controller d(bg::kNormal);
    CHECK(d.poll(Fg::Other, 0) == bg::kHigh);
    d.result(bg::kHigh, false);
    CHECK(d.poll(Fg::Other, 250) == bg::kAboveNormal);
    d.result(bg::kAboveNormal, false);
    CHECK(d.boost() == bg::kUnknown);
    CHECK(d.poll(Fg::Other, 500) == bg::kUnknown);  // nothing left to try, the original is what is applied
    CHECK(d.poll(Fg::Own, 750) == bg::kUnknown);
    CHECK(d.describe().find("every boost refused") != std::string::npos);
    std::printf("Refused classes: OK\n");
}

// A restore that keeps failing is retried each poll, up to the cap; then nothing more.
void test_give_up() {
    bg::Controller c(bg::kNormal);
    CHECK(c.poll(Fg::Other, 0) == bg::kHigh);
    c.result(bg::kHigh, true);
    uint64_t t = 250;
    uint32_t asked = 0;
    for (int i = 0; i < 20; ++i, t += 250) {
        const int32_t w = c.poll(Fg::Own, t);
        if (w == bg::kUnknown) break;
        CHECK(w == bg::kNormal);
        ++asked;
        c.result(w, false);
    }
    CHECK(asked == bg::Controller::kMaxFailures);
    CHECK(c.gave_up() && c.poll(Fg::Other, t + 250) == bg::kUnknown);
    CHECK(c.describe().find("GAVE UP") != std::string::npos);
    std::printf("Give-up cap: OK\n");
}

// An original class already high (somebody set it) is not lowered in the foreground and gets no boost; an
// unreadable one counts as Normal.
void test_originals() {
    bg::Controller hi(bg::kHigh);
    CHECK(hi.boost() == bg::kUnknown);
    CHECK(hi.poll(Fg::Other, 0) == bg::kUnknown && hi.poll(Fg::Own, 250) == bg::kUnknown);
    bg::Controller above(bg::kAboveNormal);
    CHECK(above.boost() == bg::kHigh);
    CHECK(above.poll(Fg::Other, 0) == bg::kHigh);
    above.result(bg::kHigh, false);
    CHECK(above.boost() == bg::kUnknown);  // Above normal would not raise anything
    bg::Controller unk(bg::kUnknown);
    CHECK(unk.original() == bg::kNormal && unk.boost() == bg::kHigh);
    std::printf("Original classes: OK\n");
}

// Informative: what this PC answers for this process (the game runs as the same user, without admin).
void probe_this_pc() {
    using SetFn = LONG(APIENTRY*)(HANDLE, int32_t);
    using GetFn = LONG(APIENTRY*)(HANDLE, int32_t*);
    const HMODULE gdi = LoadLibraryW(L"gdi32.dll");
    const auto set = gdi ? reinterpret_cast<SetFn>(GetProcAddress(gdi, "D3DKMTSetProcessSchedulingPriorityClass")) : nullptr;
    const auto get = gdi ? reinterpret_cast<GetFn>(GetProcAddress(gdi, "D3DKMTGetProcessSchedulingPriorityClass")) : nullptr;
    if (!set || !get) {
        std::printf("INFO: gdi32 has no D3DKMT{Set,Get}ProcessSchedulingPriorityClass\n");
        return;
    }
    int32_t orig = -1;
    const LONG g0 = get(GetCurrentProcess(), &orig);
    const LONG s1 = set(GetCurrentProcess(), bg::kHigh);
    int32_t now = -1;
    get(GetCurrentProcess(), &now);
    const LONG s2 = set(GetCurrentProcess(), bg::kAboveNormal);
    const LONG s3 = set(GetCurrentProcess(), orig >= 0 ? orig : bg::kNormal);
    std::printf("INFO: GPU class here: original %s (get %#lx) | High: %#lx (read back %s) | AboveNormal: %#lx | back: "
                "%#lx\n",
                bg::class_name(orig), static_cast<unsigned long>(g0), static_cast<unsigned long>(s1),
                bg::class_name(now), static_cast<unsigned long>(s2), static_cast<unsigned long>(s3));
}

}  // namespace

int main() {
    test_gate();
    test_classify();
    test_next_boost();
    test_switches();
    test_start_background();
    test_refused();
    test_give_up();
    test_originals();
    probe_this_pc();
    if (g_failures == 0) std::printf("All background_full_speed tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
