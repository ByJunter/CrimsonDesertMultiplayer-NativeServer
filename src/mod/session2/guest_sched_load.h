#pragma once
// P8 guest_schedule_load (docs/research/pendientes/p8-impl-horarios.md), GUEST: [session2]
//   guest_schedule_load = 0 | probe | 1
// The NPC schedule files (pa::NPCScheduleManager's NPCScheduleData_File) the host's world needs, loaded into the
// guest's process the way its own server would have done before sending the packets that name them.
//
// Load (1): on the client's NetworkThread, in deliver_all, right BEFORE a host packet is handed to
// ClientNet::OnRecvPacket: the keys a 2335 (resource +9), a 2874 (+8) or the NPC records of a 2005 (any u32 that is
// a key of the manager's schedule-context table) name; for each key whose file is not in the container: its
// context entry's name (hashed back to the key first: lookup3, initval 0xC5EDE, as 0x1864140 does), then the
// native loader 0x1864140(mgr, &handle, &entry+0x10, 0) and 0x38AD90(&handle), under an SEH filter that switches
// the module off and passes the exception on; the container is read back. Session 13: every key the packet names is
// loaded before it is delivered (only time caps bound it: 500 ms of loads per packet and per second; probe after one
// load over 250 ms or 4 over 50 ms; '[BLOCK]' when the NetworkThread spends 250 ms here in a second); the retry
// set's keys have time budgets per packet and per second; a session cap (gsl::k*). A file being unloaded is not
// present: decided again at the end of the packet. A named key that is not in the container at delivery is
// a 'GuestSchedLoad [LATE]' line (should be none). probe: the same decisions, counted and logged, nothing called.
// Watch (probe and 1): read-only mid hooks at the entries of the lookups 0x1869D70 (rdx = resource | node << 32;
// the 2005 AI block, 2874) and 0x1862A80 (r8d = key, rcx = manager; the 2335's 0xA9FEE0): found / missing per key,
// "found after our load" (the verification). Client threads only (TLS byte +0x1EC == 0).
// Hold (1, session 12, p8-impl-horarios.md §10): for a key a 2335 names, the handle of the load (or of the presence
// lookup) is NOT released at once: it is a reference, so the container's idle sweep 0x186AE50 skips the file while
// the client defers the 2335. It goes back with 0x38AD90 on the NetworkThread (before a later delivery) 30 s after
// that 2335 ran (0x1862A80 entered from 0xA9FFC8, seen by the hook above) or 120 s after the last 2335 named it;
// never when we would be its last reference. A hold of another connection (epoch), manager or container, or one
// left when the connection ended, is dropped without any call (a counted leak). At most 128 files.
// 'GuestSchedLoad [hold]' after each summary.
// Lines 'GuestSchedLoad [...]' from the diagnostics thread every 5 s; a summary every 60 s.

#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

// From guest_start (guest=1). mode: the raw ini value.
void guest_sched_load_start(const std::string& mode);
bool guest_sched_load_wanted();
// deliver_all (the client NetworkThread), right BEFORE guarded_on_recv: gives back the held handles that are due
// (any packet), then loads what `packet` names (1) / decides it (probe). Never throws; nothing when off.
void guest_sched_load_before_deliver(std::span<const uint8_t> packet) noexcept;
void guest_sched_load_tick() noexcept;  // the diagnostics thread, every 5 s
std::string guest_sched_load_describe();

// [session2] schedule_load_selfcheck = 1, HOST or GUEST (mod.cpp, worker thread): read only, its own thread (no hook,
// no call, no write). Every 30 s on change: 'ScheduleSelfCheck [state]' (the manager [exe+0x6C8CDA0] vs the
// client's and the server's roots, container files, fs), once '[context]' (EVERY schedule-context name hashed back
// to its key) and '[keys]' (which of the 25 resources the guest lacked in session 8 this client's container holds).
void schedule_selfcheck_start(bool on, bool host);

}  // namespace cdmp::session2
