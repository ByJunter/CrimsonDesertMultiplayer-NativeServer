#include <cstdio>

#include "mod/session2/seq_marker_logic.h"

namespace {
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::printf("FAIL line %d: %s\n", __LINE__, #expr); ++failures; } } while (0)

using namespace cdmp::session2::smk;
constexpr uintptr_t kManager = 0x1000;

Facts facts(uint8_t state, uint8_t marker, bool live = true, uintptr_t manager = kManager) {
    Facts f;
    f.readable = true;
    f.live = live;
    f.state = state;
    f.marker = marker;
    f.manager = manager;
    return f;
}

// The whole of a trip as the native code writes it, ending idle at 3.
void trip(Guard& g) {
    CHECK(!g.look(facts(kIdleState, 3)).restore);
    CHECK(!g.look(facts(0x4, 3)).restore);
    CHECK(!g.look(facts(0x8, 0)).restore);
    CHECK(!g.look(facts(0xA, 1)).restore);
    CHECK(!g.look(facts(0xF, 2)).restore);
    CHECK(!g.look(facts(0x15, 3)).restore);
    CHECK(!g.look(facts(kIdleState, 3)).restore);
}
}  // namespace

int main() {
    {   // The bug: 3 -> 1 idle comes back to 3, on the second look.
        Guard g;
        trip(g);
        CHECK(!g.look(facts(kIdleState, 1)).restore);
        const Decision d = g.look(facts(kIdleState, 1));
        CHECK(d.restore);
        CHECK(d.value == 3);
    }
    {   // Marker 1 inside a trip is the trip's own: before the 2322 nothing is touched, however long it lasts.
        Guard g;
        trip(g);
        CHECK(!g.look(facts(0x1, 3)).restore);
        CHECK(!g.look(facts(0x8, 0)).restore);
        for (int i = 0; i < 10; ++i) CHECK(!g.look(facts(0xA, 1)).restore);
        for (int i = 0; i < 10; ++i) CHECK(!g.look(facts(kWaitState, 1)).restore);
    }
    {   // A revive's trip, the 0 never seen: the marker goes straight from the previous 3 to 1, and the wait is the trip's.
        Guard g;
        trip(g);
        CHECK(!g.look(facts(0x1, 3)).restore);
        CHECK(!g.look(facts(0x6, 3)).restore);
        for (int i = 0; i < 10; ++i) CHECK(!g.look(facts(kWaitState, 1)).restore);
        CHECK(!g.look(facts(0x14, 2)).restore);
        CHECK(!g.look(facts(kIdleState, 3)).restore);
        CHECK(!g.look(facts(kIdleState, 1)).restore);
        CHECK(g.look(facts(kIdleState, 1)).restore);
    }
    {   // The previous trip's 3 still there in the trip's first states is not this trip's value.
        Guard g;
        trip(g);
        CHECK(!g.look(facts(0x4, 3)).restore);
        CHECK(!g.look(facts(kWaitState, 3)).restore);
        for (int i = 0; i < 10; ++i) CHECK(!g.look(facts(kWaitState, 1)).restore);
    }
    {   // After the trip's 2 the wait for 2 cannot end on its own: it comes back to 2. Seen on the guest's load.
        Guard g;
        CHECK(!g.look(facts(0x8, 0)).restore);
        CHECK(!g.look(facts(0xF, 2)).restore);
        CHECK(!g.look(facts(kWaitState, 1)).restore);
        const Decision d = g.look(facts(kWaitState, 1));
        CHECK(d.restore);
        CHECK(d.value == 2);
    }
    {   // A trip that ends idle with the marker fallen: back to what the trip reached.
        Guard g;
        trip(g);
        CHECK(!g.look(facts(0x8, 0)).restore);
        CHECK(!g.look(facts(0xF, 2)).restore);
        CHECK(!g.look(facts(0x15, 3)).restore);
        CHECK(!g.look(facts(kIdleState, 1)).restore);
        const Decision d = g.look(facts(kIdleState, 1));
        CHECK(d.restore);
        CHECK(d.value == 3);
    }
    {   // Fallen in another state of a trip: that trip goes on to write 3 itself.
        Guard g;
        trip(g);
        for (int i = 0; i < 10; ++i) CHECK(!g.look(facts(0x12, 1)).restore);
    }
    {   // One look is not enough: a trip may be writing.
        Guard g;
        trip(g);
        CHECK(!g.look(facts(kIdleState, 1)).restore);
        CHECK(!g.look(facts(kIdleState, 3)).restore);
        CHECK(!g.look(facts(kIdleState, 1)).restore);
    }
    {   // No other player so far: alone, the marker is left to the game.
        Guard g;
        CHECK(!g.look(facts(kIdleState, 3, false)).restore);
        for (int i = 0; i < 10; ++i) CHECK(!g.look(facts(kIdleState, 1, false)).restore);
    }
    {   // The other player left with the marker fallen: still put back.
        Guard g;
        trip(g);
        CHECK(!g.look(facts(kIdleState, 1, false)).restore);
        CHECK(g.look(facts(kIdleState, 1, false)).restore);
    }
    {   // Another manager (a new character) has its own history.
        Guard g;
        trip(g);
        for (int i = 0; i < 10; ++i) CHECK(!g.look(facts(kIdleState, 1, true, kManager + 8)).restore);
    }
    {   // Unreadable facts neither act nor count as a look.
        Guard g;
        trip(g);
        CHECK(!g.look(facts(kIdleState, 1)).restore);
        Facts bad;
        CHECK(!g.look(bad).restore);
        CHECK(!g.look(facts(kIdleState, 1)).restore);
        CHECK(g.look(facts(kIdleState, 1)).restore);
    }
    {   // Put back once, the next fall is handled the same way.
        Guard g;
        trip(g);
        CHECK(!g.look(facts(kIdleState, 1)).restore);
        CHECK(g.look(facts(kIdleState, 1)).restore);
        CHECK(!g.look(facts(kIdleState, 3)).restore);
        CHECK(!g.look(facts(kIdleState, 1)).restore);
        CHECK(g.look(facts(kIdleState, 1)).restore);
    }
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("seq_marker: ok\n");
    return 0;
}
