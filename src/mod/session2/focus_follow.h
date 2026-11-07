#pragma once
// [session2] focus_follow = 1 (default) | 0, both PCs, builds 3527 and 3549 (the reasons and the rules:
// focus_follow_logic.h). Each client tells the other which of its characters is in control, and shows that copy and
// hides the one left behind.
//
//   sender    its own thread, 10 Hz: clan = [[[user+0xD0]+0x68]+0x110] (vtable checked), key at clan+0x118. Tunnel
//             control state 16, at once on a change and every 2 s. A single slot, never a backlog.
//   receiver  the frame only stores what is wanted (network thread). The change is made on the client's main thread at
//             the StartActionAck dispatch mount_sync hooks (0xB57720), with Status' own setter 0x18CC450, the call the
//             game's routine makes. Needs mount_sync on (0 leaves the old behaviour, with a warning).
//   guard     remote_visible refuses the clan's hide for the character in control and lets it through for the main
//             character left behind (remote_visible_logic.h refuse_client_hide).
//
// Not touched: the panel and marker, which still read the main character, and every rule of the mod keyed to it
// (mount_sync's rider above all).

#include <cstdint>
#include <span>
#include <vector>

namespace cdmp::session2 {

// From the worker, once, on either PC.
void focus_follow_start(bool enabled, bool guest);

// The frame for the other PC, if there is one to send now.
bool focus_follow_pop_outbound(std::vector<uint8_t>& frame) noexcept;
void focus_follow_discard_outbound() noexcept;

// A tunnel packet from the other PC: true when it was this module's (taken out of the stream).
bool focus_follow_on_frame(std::span<const uint8_t> frame, uint64_t epoch) noexcept;

// The client's main thread, at the top of a StartActionAck dispatch.
void focus_follow_on_dispatch() noexcept;

// What the other player said (focus_follow_logic.h pack; 0 = nothing yet). Any thread.
uint64_t focus_follow_remote() noexcept;

// Measures only. Host: a packet queued for user 2. Guest: a packet from the host. Whole payload, key at +0.
void focus_follow_count_out(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
void focus_follow_count_in(uint16_t opcode, std::span<const uint8_t> payload) noexcept;

}  // namespace cdmp::session2
