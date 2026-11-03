#pragma once
// S45 (docs/research/s25/s45-escenas-ajenas.md), GUEST: what a scene of ANOTHER player (in practice the host's, routed
// to its manager X by guest_seq_route) does to THIS client's camera, input and interface. [session2]
//
// guest_seq_foreign_present_probe = 0 | 1 (default 0 = no hook, no cost). READ-ONLY: nothing is written in the game.
// Pure logic and layout: guest_seq_present_logic.h. Points (build 1.0.0.2976, each verified before it is installed:
// base and PE TimeDateStamp, .pdata, bytes / signature unique in .rsrc, the event class's vtable and its COL; if one
// check fails that probe is not installed and a WRN says why):
//   S-A   in guest_seq_foreign's P1 (the entry of 0x602940, already verified; installed for this key too): every stage
//         start is noted (sequencer, timeline id stage+0x298, whose); a FOREIGN one gets a line with its processor
//         [stage+0x58], its vtable (= client 0x558C080 / common 0x590DF78 / server 0x5B1FE10 / other), [proc+8] vs
//         [stage+0x28], [proc+0x10] vs the stage. R-A's table is computed once from the three vtables (never applied).
//   S-A2  in guest_x_veto's handler hooks (G = X): a stage function of a stage P1 has not seen yet (counted per handler).
//   S-B   mid hooks at the slot-99 (start) entry of CameraSelectorEventData (thunk 0x34CFC20), SequencerPlayerCamera-
//         Preset 0xB27880, the letter box 0xC38F30, the post process 0xC3A440 and SequencerAIDialog 0xC4D6B0: the
//         event's timeline (root of [rdx+8]'s +0x98 chain, its +0x1D8) matched with the stages P1 noted; count and cost.
//   S-C   0xC45747 inside StartLoadingSequencer 0xC45740 (8 bytes after its RIP-relative load; a 0x16-byte leaf, no
//         branch into it): the same plus the byte it writes to P+0x1E0 and the one there before.
//   S-D   once per frame from guest_seq_play's 0x8B6144 hook: the camera manager's current entry, the timeline player's
//         lists, [[cw]]+0xDE8, M's and the latest live foreign stage's manager's input-block level (+0x230), and that
//         stage's timeline in the player (+0xDC); only the CHANGES are logged.
// Hooks: SEH reads, atomics, a bounded lock-free ring; no allocation, no lock, no CRT, no game call. Lines
// 'GuestSeqPresent [...]' (budgeted) from the guest's diagnostics thread and a summary every 60 s.
//
// R-A, guest_seq_foreign_present = 0 | probe | 1 (default 0 = nothing; s45-escenas-ajenas.md §3.1, the decisions per
// slot in guest_seq_present_logic.h kRaExpect). In P1, a FOREIGN stage whose processor [stage+0x58] has the client
// vtable 0x558C080: probe logs '[R-A WOULD CUT presentation]'; 1 compare-exchanges THAT processor's vptr (8 bytes,
// that instance only) from exe+0x558C080 to the mod's table: built once at install from the three native vtables
// (a copy of 0x558C080, its [-1] = the same COL, the slots that turn presentation ON = the common's; what turns it
// off, or carries the direction in its data, stays native), in a page of its own (read-only after it is filled, never
// freed). Only when: the table verified (COLs, the 36 decisions, their 36 trampolines, the lifetime thunks), M is known
// and the stage is NOT M's, the vptr read is exactly 0x558C080, [proc+0x10] is the stage, [proc+8] is [stage+0x28], G
// is guest_seq_play's current X of the stage's owner, and P1 runs on guest_seq_play's frame thread (the thread that
// ticks X). Otherwise nothing is written and the line says why. A stage that is not foreign, or is M's, but carries the
// mod's table gets the client vtable back ('[R-A RESTORED]', a WRN). Never restored otherwise: the processor is the stage's own (built in the stage's constructor, deleted
// by the stage's destructor through its slot 0, which the table keeps native; that destructor writes 0x558C080 back).
//
// R-B, guest_seq_foreign_timeline = 0 | probe | 1 (default 0 = nothing; s45-escenas-ajenas.md §3.2). In the S-B hooks
// of CameraSelectorEventData 0x34CFC20 and SequencerPlayerCameraPreset 0xB27880 (installed for this key even without
// the probe), slot 99 (the event's start) only: when [ctx+8] is the timeline object (vptr exe+0x5C75F30) whose id
// +0x1D8 is a stage P1 noted, and that stage is one R-A would cut (foreign, M known, G != M, G = guest_seq_play's X of
// the owner, frame thread), probe logs '[R-B WOULD SKIP]' and 1 returns at the entry ('[R-B SKIPPED]'): the hook sets
// trampoline_rsp = rsp, so safetyhook's exit (`pop rsp ; ret`) pops the return address, as the class's empty void slot
// would. Ends (slot 101), slots 100 / 103 and every other event class stay native.

#include <cstddef>
#include <cstdint>
#include <string>

namespace cdmp::session2 {

// From guest_start (guest=1), after guest_seq_foreign_start. probe: guest_seq_foreign_present_probe; ra: the raw
// guest_seq_foreign_present (R-A: 0 | probe | 1); rb: the raw guest_seq_foreign_timeline (R-B: 0 | probe | 1);
// route_mode: guest_seq_route's raw value (without 1 no scene of another player ever reaches X: the probes say so).
void guest_seq_present_start(bool probe, const std::string& ra, const std::string& rb, const std::string& route_mode);
// Whether guest_seq_foreign has to install P1 for this module (the probe, R-A or R-B on).
bool guest_seq_present_needs_p1(bool probe, const std::string& ra, const std::string& rb);
bool guest_seq_present_wanted();
// guest_seq_foreign's P1 (the entry of 0x602940; RCX = the stage; resource = its 2661's resource, 0 if unknown). Any
// thread; nothing unless the probe or R-A is on.
void guest_seq_present_p1(uintptr_t stage, uint32_t resource) noexcept;
// guest_x_veto's handler hook, for a call whose G is X: fn = the gxv::Fn index, proc = the processor (its stage
// [proc+0x10] is read inside, only while the probe is on).
void guest_seq_present_x_fn(size_t fn, uintptr_t proc) noexcept;
// guest_seq_play's frame hook (0x8B6144, the main thread, once per frame): m = RAX (this client's M).
void guest_seq_present_frame(uintptr_t m) noexcept;
void guest_seq_present_tick() noexcept;  // the diagnostics thread, every 5 s

}  // namespace cdmp::session2
