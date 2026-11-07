#pragma once
// [session2] manager_probe = 1 | 0 (default), HOST, build 3549 only. Log only: nothing the game does changes.
//
// When user 2 logs out the host's town NPCs go: every schedule, faction and sequencer stage of the host's own
// ServerSequencerStageManager is unloaded and the manager stops evaluating cells (docs/ESTADO-3527.md). What is not known is
// which native call reaches the HOST's manager, and the state it leaves it in. This logs both:
//   - a thread reads both managers' state byte +0x6D0 and flag +0x6BA ten times a second and logs each change;
//   - mid hooks at the entries that write that state log the manager (by its character's key), the state, the return
//     address and the code pointers on the stack. Bounded per point.
// Found on the PE of 3549 from every byte access of +0x6D0 (the offset 2976 had at +0x6A0):
//   0x2AD9C00 jmp 0xE2437B0  state 1, the unload request (2976's L 0x287DA50)
//   0x2ADABE0 jmp 0xE243CC0  state 0 and the resets
//   0x2ADB5E0 jmp 0xE2452B0  state 2, the arm (2976's 0x287F450)
//   0x2AD40B0                state 0
//   0x2ADB090                the preparation (2976's P 0x287EF60: the same first bytes), called from the tick 0x2AD9D60

namespace cdmp::session2 {

// From the worker at start-up. Its own thread waits for the world, verifies the code, hooks it and then samples.
void manager_probe_start(bool enabled);

}  // namespace cdmp::session2
