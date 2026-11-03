#pragma once
// P8 S2, GUEST ([session2] load_diag=1, guest=1): the loading cycle of the
// guest's client seen through its packets (2174, 2549, 2144, 3066, 2322, 2445,
// 2762, 2042) and its loading-state transitions, one "GuestLoad" line per
// packet and a summary per trip with a verdict (load_trip_logic.h;
// pendientes/p8-2322-observador.md §6). Read only, never the game's code:
// the network threads only push fixed records into a lock-free ring; the
// guest's diagnostics thread (guest_diag_worker) formats them every 5 s.
// The loading states come from guest_actor_diag's transitions
// (guest_actor_diag=1); without them the summary says so.

#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

void load_trip_start(bool on);  // guest_start
bool load_trip_wanted();

// The mod's network thread (guest_deliver): a host packet queued for the client.
void load_trip_on_in(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
// The client's NetworkThread: a host packet handed to OnRecvPacket / a client Req sent to the host.
void load_trip_on_delivered(std::span<const uint8_t> packet) noexcept;
void load_trip_on_out(std::span<const uint8_t> packet) noexcept;
// guest_actor_diag's transitions (its own thread): [P+0x15C] old -> new at qpc_us.
void load_trip_on_state(int old_state, int new_state, uint64_t qpc_us) noexcept;

// The guest's diagnostics thread, every 5 s.
void load_trip_tick() noexcept;
std::string load_trip_describe();  // atomics only

}  // namespace cdmp::session2
