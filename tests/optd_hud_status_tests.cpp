// The overlay's role and session lines (src/mod/render/hud_status.h).

#include <cstdio>
#include <string>

#include "mod/render/hud_status.h"

using namespace cdmp::render;

namespace {
int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++g_failures; } } while (0)

HudStatus host(bool connected, std::optional<bool> world = false) {
    HudStatusIn in;
    in.host = true;
    in.connected = connected;
    in.peer_name = "Raven33be";
    in.same_world = world;
    return hud_status(in);
}

HudStatus guest(bool connected, std::optional<bool> world, bool ready, bool alone = false) {
    HudStatusIn in;
    in.connected = connected;
    in.peer_name = "SamuelCardillo";
    in.same_world = world;
    in.host_ready = ready;
    in.alone_after_remote = alone;
    return hud_status(in);
}

void test_host() {
    CHECK(std::string(host(false).role) == "HOST");
    CHECK(host(false).line == "Waiting for a player to join" && host(false).level == HudLevel::Waiting);
    CHECK(host(true).line == "'Raven33be' is connected and joining your world" && host(true).level == HudLevel::Waiting);
    CHECK(host(true, true).line == "'Raven33be' is in your world" && host(true, true).level == HudLevel::Ok);
    // A stale "in the world" with nobody connected is still waiting: the link is what the player needs to know.
    CHECK(host(false, true).line == "Waiting for a player to join");
    // No session state at all (legacy modes).
    CHECK(host(true, std::nullopt).level == HudLevel::Waiting);
}

void test_guest() {
    CHECK(std::string(guest(false, false, false).role) == "GUEST");
    CHECK(guest(false, false, false).line == "Looking for the host");
    CHECK(guest(true, false, false).line == "Connected to 'SamuelCardillo': wait before pressing Play");
    CHECK(guest(true, false, false).level == HudLevel::Waiting);
    CHECK(guest(true, false, true).line == "Connected to 'SamuelCardillo': press Play to join");
    CHECK(guest(true, false, true).level == HudLevel::Ok);
    CHECK(guest(true, true, true).line == "In 'SamuelCardillo''s world" && guest(true, true, true).level == HudLevel::Ok);
    // Alone wins over everything: the link can be up while the game plays on its own server.
    CHECK(guest(true, false, true, true).level == HudLevel::Alert);
    CHECK(guest(false, false, false, true).line == "Playing alone, not in the host's world");
}

void test_no_name_yet() {
    HudStatusIn in;
    in.host = true;
    in.connected = true;
    CHECK(hud_status(in).line == "the other player is connected and joining your world");
}

void test_local_tracking() {
    CHECK(hud_tracks_local(0.05f));
    CHECK(hud_tracks_local(kHudLocalTrackedMaxError));
    CHECK(!hud_tracks_local(171.41f));   // seen after a character switch
    CHECK(!hud_tracks_local(1660.06f));  // the moment of that switch
}
}  // namespace

int main() {
    test_host();
    test_guest();
    test_no_name_yet();
    test_local_tracking();
    if (g_failures) {
        std::printf("optd_hud_status: FAILED (%d failures)\n", g_failures);
        return 1;
    }
    std::printf("optd_hud_status: all OK\n");
    return 0;
}
