#pragma once
// The overlay's first lines: who you are in the session and what state it is in. Pure logic, no game access or ImGui
// (tests: tests/optd_hud_status_tests.cpp), so every state can be checked without running the game.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cdmp::render {

enum class HudLevel : uint8_t { Ok, Waiting, Alert };

struct HudStatusIn {
    bool host = false;
    bool connected = false;              // the mod's link to the other PC
    std::string_view peer_name;          // "" = not known yet
    std::optional<bool> same_world;      // nullopt = no session (legacy modes); see HudData::same_world
    bool host_ready = false;             // guest: the host said it can take this guest now
    bool alone_after_remote = false;     // guest: went back to the title and plays on its own server
};

struct HudStatus {
    const char* role = "";
    std::string line;
    HudLevel level = HudLevel::Waiting;
};

inline HudStatus hud_status(const HudStatusIn& in) {
    const std::string name = in.peer_name.empty() ? std::string("the other player") : "'" + std::string(in.peer_name) + "'";
    const bool in_world = in.same_world.value_or(false);
    if (in.host) {
        if (!in.connected) return {"HOST", "Waiting for a player to join", HudLevel::Waiting};
        if (in_world) return {"HOST", name + " is in your world", HudLevel::Ok};
        return {"HOST", name + " is connected and joining your world", HudLevel::Waiting};
    }
    // Before the connection: going back alone is told apart, it needs the player to do something.
    if (in.alone_after_remote) return {"GUEST", "Playing alone, not in the host's world", HudLevel::Alert};
    if (!in.connected) return {"GUEST", "Looking for the host", HudLevel::Waiting};
    if (in_world) return {"GUEST", "In " + name + "'s world", HudLevel::Ok};
    if (!in.host_ready) return {"GUEST", "Connected to " + name + ": wait before pressing Play", HudLevel::Waiting};
    return {"GUEST", "Connected to " + name + ": press Play to join", HudLevel::Ok};
}

// How far the mod's reading of the local character may be from the game's own position. It reads the character the
// session started with: after a character switch that one stays behind (1,660 m seen on 2026-11-05, build 3549), and a
// distance or a marker computed from it would be wrong.
inline constexpr float kHudLocalTrackedMaxError = 50.0f;
inline bool hud_tracks_local(float static_pos_error) { return static_pos_error <= kHudLocalTrackedMaxError; }

}  // namespace cdmp::render
