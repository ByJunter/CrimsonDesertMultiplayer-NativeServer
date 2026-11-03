#pragma once
// What the overlay shows: a status panel and a marker over the other player.
// The network thread publishes data with hud_update(); the render hook calls
// hud_draw() once per frame on the game's present thread.

#include <cstdint>
#include <optional>
#include <string>

#include "common/protocol.h"
#include "mod/game/player_reader.h"

namespace cdmp::render {

struct HudData {
    bool connected = false;
    std::string peer_name;
    float rtt_ms = 0.0f;
    std::optional<game::PlayerSnapshot> local;
    std::string local_status;
    std::optional<proto::PlayerState> remote;
    std::string peer_problem;  // another protocol version or mod build ("" if none)
    // Option D (S25, docs/research/s25/invisibles.md): both games play the host's world now (the host: user 2 is in
    // its world; the guest: its connection goes to the host). nullopt = no session 2 (as before). False: no marker
    // and no distance, which the mod's channel alone would draw over a world without the other character.
    std::optional<bool> same_world;
    bool s2_guest = false;
    bool alone_after_remote = false;  // the guest went back to the title and plays on its own server
    // P8 re-entry ([session2] rejoin, session2/rejoin_logic.h): the re-entry's state or why it cannot happen ("" = none).
    std::string rejoin_text;
    // P8 S26 (docs/research/s25/montura-regresion.md section 7.1): a mount problem to show ("" none), Spanish.
    std::string mount_problem;
};

struct HudOptions {
    bool self_marker = false;  // dev aid: draw a cross on our own character
};

void hud_configure(const HudOptions& options);
void hud_update(HudData data);
void hud_draw(float width, float height);

// A short fixed notice under the status panel for `ms` milliseconds. Any
// thread, a game thread included: two relaxed atomic stores, no lock, no
// allocation (the text is chosen by id when the panel is drawn).
// ReentrySlotKept (P8 re-entry): another slot was chosen at the title in a re-entry; the host's live slot is played.
enum class Notice : uint8_t { None = 0, FastTravelVetoed = 1, ReentrySlotKept = 2 };
void hud_notice(Notice n, uint32_t ms) noexcept;

}  // namespace cdmp::render
