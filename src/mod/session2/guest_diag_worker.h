#pragma once
// P8, the GUEST's diagnostics thread (docs/research/pendientes/p8-hilo-red.md).
//
// Live P8 session 5 (Shadow PC as the guest): the network thread called the
// guest's diagnostics from its 5 s report, and the actor diagnostic's walk of
// the loading wait list (193 keys, each one through our replay of
// ClientActorContainer::Find with a VirtualQuery per read) took 2.5-8 s per
// report while the guest loaded the host's field. The network thread then
// read Steam once every ~6 s: the host timed the guest out and every
// reconnection timed out again until the guest left the loading state.
//
// Now one low-priority thread (THREAD_PRIORITY_BELOW_NORMAL) runs, every 5 s:
//   guest_actor_diag_tick()   restore lines + the loading state's wait list
//   guest_s3_tick()           the S3 history, the summary, the crash records
//   guest_stage_diag_tick()   P8 phase 2: where the stage Acks land (guest_stage_diag)
//   guest_describe()          the "Session2 guest: hooks ..." status line
// with the same line formats as before. The network thread only kicks it
// (guest_diag_kick: a SetEvent, never a wait); if the network thread is stuck
// the worker still runs on its own every 5.5 s. A run above 1 s writes a
// "GuestDiag worker: one run took ..." warning with the time of each part.

#include <string>

namespace cdmp::session2 {

// From guest_start; once. Nothing else is needed.
void guest_diag_worker_start();
// The network loop's 5 s report: never blocks. No-op before the start.
void guest_diag_kick() noexcept;
// " | diag worker: runs N (timed M), last X ms, max Y ms" (empty when not started).
std::string guest_diag_worker_describe();

}  // namespace cdmp::session2
