#pragma once
// P8, HOST: [session2] horse_follow = 0 (default) | probe | 1 (needs create_session=1)
// (docs/research/pendientes/p8-movimiento-remoto.md §3 F2; the implementation and what it does NOT
// cover: docs/research/pendientes/p8-impl-interp-remota.md; the pure logic: horse_follow_logic.h).
//
// A rider's MoveActorAck 2697 (~36 Hz, the horse's world transform in its Path) that the server
// writes to user 2's session (take_frame, session 2's Send, the game's threads) is followed, with 1,
// by a synthetic 2697 of the horse built from it, queued right after it in the same queue to the
// tunnel (mod memory only: a std::vector of the mod, never the game's heap; nothing of the game is
// called). Only to the session that gets the rider's Ack (user 2's), never for a horse user 2
// controls. Measures (probe and 1): the riders' Acks to user 2 and their cadence, the horse's own
// Acks while ridden, and user 2's own riding Reqs (the other direction, which this does not cover:
// the server writes the guest rider's Acks to the host's client through session 1, which no code of
// the mod sees; see the document).

#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

// session2::install (host, create_session=1): the raw ini value. False: off (0, unknown: warned).
bool horse_follow_configure(const std::string& raw);
bool horse_follow_active();

// take_frame, for a packet to the LIVE session 2 that was queued as usual (route Pass): its whole
// packet (header + payload). `push` queues one more packet right after it (false: no room).
using HorsePushFn = bool (*)(std::span<const uint8_t> packet);
void horse_follow_on_out(uint16_t opcode, std::span<const uint8_t> packet, HorsePushFn push) noexcept;
// server inject, a Req of user 2 that was queued for the server (its payload).
void horse_follow_on_guest_req(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
// A new guest epoch: user 2's keys are forgotten.
void horse_follow_on_epoch() noexcept;

std::string horse_follow_describe();  // atomics only ("" when off)

// P8 session 9, M1: [session2] guest_horse_probe = 0 (default) | probe, HOST (create_session=1)
// (pendientes/p8-impl-s9-movimiento.md §4). User 2's riding Req cadence vs its horse's own, the horse's own
// position vs the rider's Path world at the same ts, and the synthetic MoveActorReq 2556 of the horse built
// (horse_follow_logic.h make_horse_req) for every riding Req: NEVER sent. Works with horse_follow off; its
// numbers go into horse_follow_describe(). 1 is refused (warned, runs as probe).
bool horse_follow_configure_guest_probe(const std::string& raw);
bool horse_follow_guest_probe_active();

// P8 mount ride (pendientes/p8-impl-monturas-cabalgata.md; the pure logic: horse_follow_logic.h "P8 mount ride").
// [session2] horse_follow_reverse = 0 (default) | probe | 1, HOST (create_session=1, [sniffer] enabled=1, child_hold's
// server reads). User 2's main character riding its own horse (owned by user 2's ServerUserActor): every one of its
// MoveActorAck 2697 reaching the HOST'S CLIENT (ClientNet::OnRecvPacket) is preceded, with 1, by one of the horse
// derived from it (the rider's Path world pose and ts), only if newer than the last horse sample the client got;
// never sent to the server nor to user 2. probe: measured, nothing delivered. Call after the sniffer is in
// (session2::install). False: off.
bool horse_follow_reverse_configure(const std::string& raw);
bool horse_follow_reverse_active();

// The client-side observer (both PCs): installs the sniffer's recv-extra callback once (the reverse direction above,
// and the rider -> horse pairs for remote_interp_pair). `who` names the caller in the log. False: the sniffer's
// hooks are not in ([sniffer] enabled=0, or not yet).
bool horse_follow_client_attach(const char* who);
// remote_interp_pair=1 (remote_interp.cpp): every rider's Move Ack seen by this client tells remote_interp its pair.
void horse_follow_set_pair_notes(bool on);

}  // namespace cdmp::session2
