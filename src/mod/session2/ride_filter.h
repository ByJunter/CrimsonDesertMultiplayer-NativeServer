#pragma once
// P8 session 9, D1: [session2] ride_filter = 0 (default) | probe | 1, HOST and GUEST
// (docs/research/pendientes/p8-s9-combate-montura.md §4, §6; the implementation and what to look for in the
// log: docs/research/pendientes/p8-impl-s9-movimiento.md §1; the pure logic: ride_filter_logic.h).
//
// The other player's StartActionAck 2991 that would put its character into RideOn (mount) or RideOff
// (dismount) on this client, where both Enter fail without the rider<->vehicle link and freeze it, is
// rewritten in place into the native on-foot / riding action (byte-identical blocks of session 9) before the
// client sees it. probe: counted and the hashes around riding recorded, nothing changed.
//
//   GUEST: guest_deliver (the mod's network thread), on the packet before it is queued for the client. Only
//          the host's character: its key comes from the host once a second (tunnel control state 4).
//   HOST:  ClientNet::OnRecvPacket, through the sniffer's mid hook (the host client's NetworkThread): the only
//          point where user 2's Acks (session 1, native) pass through code of the mod. Needs [sniffer] enabled
//          (the hook); without it the host side stays off (warned). Only user 2's character (its key from the
//          MainPlayerDataStartAck the server writes to session 2) is touched.
// On the game's thread: no allocation, no lock (a try-lock for the probe's table), no logging; a lock-free
// ring and atomics, drained by ride_filter_tick on a mod thread.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mod/session2/ride_filter_logic.h"

namespace cdmp::session2 {

// P8 S18 (pendientes/p8-s18-desmontar-nativo.md §6.3; ride_filter_logic.h Dismount): [session2] ride_dismount =
// rewrite (default) | partial | native, from the worker thread before ride_filter and mount_sync start. False:
// unknown value (stays rewrite, warned).
bool ride_dismount_configure(const std::string& raw);
rfl::Dismount ride_dismount_mode();
// The client main thread: a hash seen with def+0xA & 0x40 (true = newly learned). Any thread: is it one?
bool ride_filter_learn_gate_hash(uint32_t hash) noexcept;
bool ride_filter_gate_hash(uint32_t hash) noexcept;

// From the worker thread at start-up: the raw ini value; guest = this PC is the guest. False: off.
bool ride_filter_configure(const std::string& raw, bool guest);
bool ride_filter_active();
// ride_filter=1 (it rewrites; probe and 0 never do). mount_sync's dispatch fallback rewrites only then.
bool ride_filter_rewrites();

// GUEST, guest_deliver (the mod's network thread): a whole packet from the host's server (header + payload),
// changed in place. Only the HOST's character (its key from the host's control state 4) is touched; a new client
// connection (connection_epoch, guest_connection_epoch()) forgets the keys.
void ride_filter_on_guest_packet(std::vector<uint8_t>& packet, uint64_t connection_epoch) noexcept;
// GUEST, guest_deliver: a tunnel control packet's payload (state 4 = the host's character key).
void ride_filter_guest_control(const uint8_t* payload, size_t size, uint64_t connection_epoch) noexcept;

// HOST: installs the rewrite callback in the sniffer's OnRecvPacket hook (after the sniffer is installed).
// False (logged): no sniffer hook, the host side is off.
bool ride_filter_host_attach();

// A mod thread (the guest's diagnostics worker, the host's network thread): the queued lines, and the
// summary once a minute.
void ride_filter_tick() noexcept;
std::string ride_filter_describe();  // atomics only ("" when off)

}  // namespace cdmp::session2
