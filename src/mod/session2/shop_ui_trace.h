#pragma once
// [session2] shop_ui_trace = 0 (default) | 1, BOTH roles, build 3549 only. LOG ONLY: nothing in the game is changed.
//
// A shopkeeper's window opens from its scene: the scene's chart event "StageChartUIControl" goes through
// 0x6CCA40(list, u32 ActorKey, staticstringA* win, staticstringA* ctrl, SequencerStageId*, CommandData* cmd)
// (caller 0x6A6070, return 0x6A6075), its subscriber 0xFFA820 hands command type 9 to the window, and the window
// 0xE8BD20(window, {handle*, count}*, 0) takes the shopkeeper (window UIGamePlayControlRootNpcBuyStore2, vtable 0x59ECA30).
// With 1, mid hooks at the entry of both (installed 150 s after the world is up, so the guest's RemoteInterp gets its
// near memory first) log every call: the actor, the window and control names, the scene, the command type and its
// parameters (strings of tag 4, actor-key lists of tag 5); and for 0xE8BD20 the window and the shopkeeper's key.
// Purpose: the host's native shop (reference) against the guest's own copy of the shop scene (guest_shop_branch),
// where the scene reaches branch 6 like the native one but no window shows.
// Also (6-nov afternoon, ESTADO-3527.md): the chart functions' vf8 thunks InputBlock 0x255C110 / UIControl 0x255C800
// (each arms a 25 s node trace on its stage and logs the executor's node, r15), the executor 0x2202A60's entry (every
// node definition the armed stage runs, with its functions' RTTI names), the node step 0x21FDE10 and the stage step
// 0x2BA1420 (the host's server thread moves the shop scene to its menu node through it; never called for the guest's
// copy).

#include <string>

namespace cdmp::session2 {

void shop_ui_trace_start(const std::string& mode, bool guest);

}  // namespace cdmp::session2
