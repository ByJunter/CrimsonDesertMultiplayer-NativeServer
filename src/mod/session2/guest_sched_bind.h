#pragma once
// P8 NPC schedule stages, the binding retry (docs/research/pendientes/p8-impl-horarios-enlace.md): [session2]
// guest_sched_bind = 0 | probe | 1, guest=1, needs guest_sched_route=1 (otherwise off, refused with a line).
//
// The gap it closes [V]: an NPC binds to its schedule stage only in vf13 0x9ED290 (once, when its 2005 is unpacked)
// or in 0x9F2FA0 (the 2874 handler and the AI's vf107, neither of which acts on the guest). A 2335 routed into X
// AFTER the NPC's 2005 (observer_sched_copy's snapshot, released at the gate; or NPCs created before the host's
// character existed on this client) leaves that NPC with comp+0x318 == 0 for good.
//
// What it does: one more point of guest_seq_route's 2335 handler, 0xC0B626 (8 bytes: mov [rbp-0x30],rax ;
// cmp byte [rbp-0x20],0), right after `call 0xA9FEE0` put the stage into X. Only for the 2335 the route hook at
// 0xC0B5C1 ROUTED into X in this same handler call (frame and scene key re-checked) and only when the stage with
// that key is in [X+0x148]: a bounded walk of the client actor table collects the host character's NPCs of that
// scene (comp+0x2D8 == the character, comp+0x2E0 == the key, comp+0x318 == 0, the pointers 0x9F2FA0 reads
// unchecked present). probe: logs them. 1: for each (at most gsr::kBindCap per 2335), the game's own locked lookup
// 0x8AE1A0 (a reference, as 0x9F2FA0 takes its owner), a re-check, the native 0x9F2FA0(comp, &key) (the 2874's
// call), comp+0x318 read back, then the reference released with 0x1434880 (the game's holder destructor).
// Thread: the game's main thread inside the native handler (the 2874 handler calls 0x9F2FA0 from the same
// dispatch); no lock of the game is held at 0xC0B626 (0xA9FEE0 released its guards). The bodies of the three called
// functions (and 0x43B4B0) are compared with build 2976's (FNV-1a) before the point is installed; the exe's TLS realm
// must be the client's (0). An exception inside a called game function is not handled (P8 review I1,
// game_fault.h): the feature latches off, one ERR line, the exception goes on.
//
// Session 12 (p8-impl-horarios-enlace.md §10): the snapshot's 2335s are handled while the field loads, when the
// NPCs and the host's character are not logged in yet (u16 actor+0x5E without 0x10): 0x8AE1A0 refuses them
// (0x8AE2C9) and 0x9F2FA0 would return early at its own owner lookup. Such an NPC is QUEUED (gsr::BindQueue, at
// most gsr::kRetryCap) and retried on the main thread from guest_seq_play's frame hook 0x8B6144
// (guest_sched_bind_frame) or, without it, from the next routed 2335's point: once the NPC and its owner are logged
// in and this client's own M+0x231 == 3; every gsr::kRetryPeriodUs at most, gsr::kRetryCallsPerPass calls per
// pass, dropped after gsr::kRetryTimeoutUs or when it no longer applies (bound meanwhile, gone, stage gone...).
//
// Lines 'GuestSchedBind [...]' (per call / per candidate in probe / per routed 2335 / per retry, limited) and two
// summaries every 60 s, from the guest's diagnostics thread (guest_seq_route_tick).

#include <cstdint>
#include <span>
#include <string>

#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/guest_seq_route_logic.h"

namespace cdmp::session2 {

// From guest_seq_route_start: guest_sched_bind's raw ini value and guest_sched_route's parsed mode.
void guest_sched_bind_configure(const std::string& mode, gsr::Mode route_mode);
// From guest_seq_route's install thread, after the schedule points: p2335_on = the 0xC0B5C1 hook is in.
void guest_sched_bind_install(uintptr_t base, std::span<const gad::Section> secs, bool p2335_on);
// From the 2335 route hook (0xC0B5C1), on every 2335 it judges: routed = RAX := X was done for it; m = the M the
// handler's vf48 gave (the retry pass at 0xC0B626 reads its +0x231).
void guest_sched_bind_note(bool routed, uint64_t rbp, uint64_t key, uint64_t x, uint32_t owner, uint32_t resource,
                           uint64_t m) noexcept;
// From guest_seq_play's frame hook 0x8B6144 (the main thread, once per main loop turn, before the native tick;
// m = RAX there, this client's own manager M): the retry pass of the queue (session 12, see above).
void guest_sched_bind_frame(uintptr_t m) noexcept;
void guest_sched_bind_tick() noexcept;  // the diagnostics thread, every 5 s
std::string guest_sched_bind_describe();

}  // namespace cdmp::session2
