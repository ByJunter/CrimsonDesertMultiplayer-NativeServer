#pragma once
// P8 mounts, stages 1 + 2 (docs/research/pendientes/p8-impl-monturas.md; the pure logic and the wire format:
// mount_sync_logic.h). [session2] mount_sync = 0 (default) | probe | 1, on BOTH PCs (symmetric: each client
// controls its own rider + horse and observes the other player's pair).
//
// Stage 1, probes (probe and 1): INLINE hooks, each on build 1.0.0.2976's bytes verified first (inside .rsrc,
// [0x1000, 0x51EB000), never .xtls; the patch must be exactly the verified whole instructions):
//   0x2228260  CommonVehicle::vf53 entry (7 B: mov rax,rsp; mov [rax+0x10],rbx): MountBegin / MountCommit -
//              rider, horse, seat R9b, fifth byte [entry rsp+0x28], caller, *result, the rider's Link
//              +0x08/+0x0C/+0x10/+0x14, the seat's occupant/reservation and the count before/after
//   0xA45740   ClientVehicle vf53 thunk (5 B: jmp 0x9DC90A0): marks the client call and its vtable call site
//   0x2228E40  CommonVehicle RideOff thunk (5 B: jmp 0xE6F2E60): MountDetach (result, link before/after)
//   0xA45760   ClientVehicle vf54 (5 B): lifecycle observation
//   0x681C80 / 0x682C20  RideOn::Enter / RideOff::Enter (7 B each): result and link (MountApply on observers)
//   0xBB4130 / 0xBB4AC0  2663 / 2916 interaction Ack handlers (5 B each): MountReserve (Link+0x08/+0x0C)
//   0xB57720   StartActionAck 2991 handler (5 B): the observer's apply point (stage 2) and its record
// Hooks copy registers and SEH-guarded reads into a bounded lock-free ring; this module's own thread formats.
//
// Stage 2: the controller publishes MountBegin / MountCommit / MountDetach of ITS OWN character through the
// tunnel's control packet (opcode 1, state 5; also in probe, so the observer can log what it would do). The
// observer (mode 1 only) holds the observed stream from a Begin until its Commit (bounded, 500 ms), replays the
// commit with 0x2228260 on the client main thread at the dispatch of the rider's next StartActionAck, checks the
// postconditions, lets the original action go on (ride_filter's rewrite deferred for that transaction and done
// there if the link is missing), and replays the controller's 0x2228E40 once after this client's RideOff::Enter.
//
// Session 17 (pendientes/p8-impl-monturas.md, "Sesión 17"): a player's dismount never calls 0x2228E40, so the
// controller also watches its own Link+0x10 at the dispatches (<= every 30 ms) and publishes an OBSERVED Detach
// (flag 12, p4 0: the client's own retirement of a remote rider) when it leaves the committed horse; the observer
// retires a link it applied without any Detach only after its RideOff::Enter and 6 s of on-foot actions. Both
// observers (probe and 1) measure where the remote rider is shown relative to its horse (TransformSync, by horse
// speed, linked here or not) for the summary line.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cdmp::session2 {

// From the worker thread, once, on either PC. Starts its own thread (install + drain); no-op for 0.
// P8 S21 (pendientes/p8-impl-monturas.md §13), both 0 | probe | 1, default 0:
//   seat_raw ([session2] mount_seat, only with mount_sync=1): after the observer's vf53, ClientVehicle vf54 0xA45760
//     natively (through the vf54 hook's original) seats the remote rider on its horse here (attach type 4, the
//     saddle bone); its release is watched at the dispatches (one 0x2228E40 flag 12 if it stays seated 1.5 s after
//     the dismount). probe: the judge and "would call vf54" only.
//   hold_raw ([session2] ride_hold_pos, installed with mount_sync): an inline hook at the ENTRY of the mode-2 movement
//     update 0x88E6A0; when its interpolation buffer is empty ([mc+0x3D0] == 1, RideOn::Enter) the remote player's
//     output is filled with its current TransformSync pose instead of the zeroed one (mode 1 with mount_sync=1 only;
//     otherwise counted).
// P8 S26 (docs/research/s25/montura-regresion.md sections 7.2 and 10.2), relink_raw ([session2] mount_relink, 0 |
// probe | 1, default 0, only with mount_sync=1; probe otherwise): the observer applies the commit (and the seat) again
// when the game retired the link it applied before the remote rider's RideOn::Enter, at the rider's next pre-mount
// action (parent -1, target its horse). probe: one 'MountSync RELINK (probe: would relink)' line per mount, nothing
// called. Off without the RideOn::Enter / RideOff::Enter hooks (its stop conditions). A failed relink stays with its
// mount (no pair quarantine, no seat switch-off for the session).
void mount_sync_start(const std::string& raw, bool guest, const std::string& seat_raw = "0",
                      const std::string& hold_raw = "0", const std::string& relink_raw = "0");
bool mount_sync_active();   // probe or 1 (the hooks may still be going in)
bool mount_sync_live();     // 1: holds, defers and native calls
// S40 carros (docs/research/s25/s40-carros.md): is `key` the horse / vehicle a player rides, as far as mount_sync knows
// (this PC's own armed pair, or the other player's last mounted transaction)? Atomics only, any thread; false when off.
// A second source next to the server-side parent link: user2_stages never logs out (nor assigns) a vehicle that a
// player rides.
bool mount_sync_rides(uint32_t key) noexcept;

// Keys (network threads). Local: this client's own character (host: its user's +0xD0; guest: its 2756).
// Remote (guest): the host's character from the host's control state 4 (the host reads server_user2_player_key).
void mount_sync_set_local_key(uint32_t key) noexcept;
void mount_sync_set_remote_key(uint32_t key) noexcept;

// A tunnel packet (whole frame) from the other PC: true when it was a mount event (taken out of the stream).
// `epoch`: the connection it came on (a new one forgets every transaction).
bool mount_sync_on_frame(std::span<const uint8_t> frame, uint64_t epoch) noexcept;

// The controller's events, as whole tunnel frames, oldest first. False: none.
bool mount_sync_pop_outbound(std::vector<uint8_t>& frame) noexcept;

// GUEST (the mod's network thread): hold the host's packets now (a Begin waits for its Commit, mode 1).
bool mount_sync_guest_holding() noexcept;
// HOST (its network thread): hold user 2's Req now; `queued` = how many are already held (over the bound the
// holds are cancelled and false is returned).
bool mount_sync_host_holding(size_t queued) noexcept;
// A held stream reached its bound: every waiting Begin is cancelled.
void mount_sync_cancel_holds(const char* why) noexcept;
void mount_sync_note_hold(bool host, size_t released) noexcept;  // counts for the summary (released 0 = a hold began)
// The connection ended or user 2 logs out (host: start_epoch / begin_logout; guest: end of the remote session):
// every observer transaction is forgotten at once (not only at the next event of a new epoch). The guest also forgets
// the host's character key (§12: a later local save may give its own character that key).
void mount_sync_reset(const char* why) noexcept;
// P8 re-entry (rejoin_logic.h): the guest's own character key on the host is forgotten (only with [session2] rejoin
// on, so rejoin=0 behaves as before). Any thread.
void mount_sync_forget_local_key() noexcept;

// ride_filter (mode 1): leave this remote rider's mount/dismount StartActionAck to the dispatch (a validated
// transaction is in place or about to be applied). Try-lock only: busy -> false (ride_filter rewrites as before).
bool mount_sync_ride_filter_defers(uint32_t key) noexcept;

// P8 S18 fix B (ride_dismount=native, pendientes/p8-impl-monturas.md §11): the detour of the CC request gate's thunk
// 0x2329A90 is in and has not faulted (mount_sync=1). ride_filter rewrites the dismount (S17) while it is not.
bool mount_sync_gate_ready() noexcept;
// HOST self-check (the sniffer's writer thread, every captured packet): this client's own C>S StartActionReq 2223
// for the remote rider (or its horse) is counted and warned; an observer must never send one.
void mount_sync_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept;
// GUEST self-check (guest_send, the client's NetworkThread, every packet for the host; p8-impl-monturas.md §12): the
// same count, warning and fix B degraded; with mode 1 true = WITHHOLD it (never sent to the host's server).
bool mount_sync_guest_outgoing(const uint8_t* packet, size_t size) noexcept;

std::string mount_sync_describe();  // "" when off
// P8 S26 (docs/research/s25/montura-regresion.md section 7.1): the overlay's line, in Spanish ("" when all is well):
// this PC's controller publishes no mount (its 0x2228260 hook is not installed), or the other PC said so (Status).
std::string mount_sync_hud_problem();

}  // namespace cdmp::session2
