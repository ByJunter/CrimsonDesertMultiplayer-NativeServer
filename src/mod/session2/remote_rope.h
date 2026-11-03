#pragma once
// P8 session 25, the grappling hook's blue rope of the OTHER player (docs/research/s25/gancho-cuerda.md, section
// "Implementado"). Build 1.0.0.2976. remote_rope_logic.h has the verified addresses and the decisions.
//
// [session2] remote_rope = 0 (default) | probe | 1, on BOTH PCs (the server half on the host only):
//   F1  at the entry of RemoteCatchComponent_Apply 0x592340 (the 2763 apply; one caller 0xB5BB7B): for a thrower not
//       controlled here and a tree target (kind 0, class 2, a handle, a scene id), this PC's physics-body handle of
//       the same scene object (the thrower's own mapping) replaces the thrower's handle in the handler's local target
//       block (1) or is only computed and logged (probe). Unresolved: passed through, counted. Own throws: computed
//       as the check, never written. Both bodies' transforms are logged.
//   F3  the same hook, first: a thrower not controlled here whose slot is still busy when its 2763 arrives (a release
//       that never came, or a failed apply that left it filled) is released with the native 0x5A7A10 (1) or logged,
//       ONLY when that stale slot holds a tree (kind 0, class 2, no target actor); any other stale slot is logged only.
//   A fault inside one of the module's game calls switches that call off for the session and the 2763 it happened in
//   is not applied (the native error). probe makes the same native-equivalent reads (the lookup takes the scene
//   manager's lock and a reference; vf70 / vf3B0 are called) and writes no game state.
//   F2  HOST: at the entry of ServerFrameEventReleaseRemoteCatch::vf1 0x27D9110: after a release the server leaves
//       without a 2471 (a tree), the native writer 0x29688C0 sends ReleaseRemoteCatchResultAck ToNear(thrower) (1),
//       as 0x2903950 does for actor targets; probe: 'WOULD SEND' only. The LAUNCH frees the slot without that frame
//       event (the action change, the server component's vf140 0x1807390): F2 also hooks that entry (the server's
//       component only) and sends the same 2471 for a tree slot it freed. One line per launch: the thrower's later
//       release events that find the slot empty are folded into it (counted).
//   And at the entry of the 2471 handler 0xB5BD60 (both PCs, log only): the slot before / after each 2471 applied.
// Lines 'RemoteRope [probe|1] ...' (at most 400 per run) and a 'RemoteRope summary' every 60 s. An apply's line is
// logged once its rope's flight is known (0x5A12A0 sets it after the apply; at most 3 s later).
//
// Threads: the apply and 2471 hooks run on the client thread that applies the Acks, the server hooks on the server's;
// they read with SEH-guarded reads, call the game only where the game itself calls those functions, push events into
// a lock-free ring and count. The module's thread installs the hooks (world up, every byte checked: REFUSED and
// nothing written otherwise), drains the ring and logs.

#include <string>

namespace cdmp::session2 {

// mod.cpp's worker, both roles. Nothing for 0.
void remote_rope_start(const std::string& raw, bool guest);

}  // namespace cdmp::session2
