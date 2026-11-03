#pragma once
// P8: the other player's character as a native remote actor (control mode 2
// on both PCs): free wall climb, ledge climb-up and mount/dismount
// (docs/research/pendientes/p8-escalada-desmontar.md; the implementation, the
// verified bytes and what to look for in the log:
// docs/research/pendientes/p8-impl-escalada.md). HOST and GUEST.
//
// [session2] keys (remote_motion_logic.h), all 0 by default:
//
// remote_climb = 0 | probe | 1. One INLINE hook at PointClimb::Enter 0x691400
//    (vf7 of PointClimbMovementMethod, .rsrc; its 23-byte prologue verified,
//    the patch must end on one of its instruction boundaries). The detour calls
//    the original (unsafe_call) and, only when it FAILED, reads (fast_read) the
//    actor [m+0x20], the control component [m+0x18], the movement controller
//    [CC+0xC8] (its +0x68 must be that actor) and the key [actor+0x60], then
//    calls the game's own control-mode function 0x88E8E0(mc) (read only; its
//    whole body, its jump table and its callee 0x17F2A50 verified byte for byte
//    before the hook goes in) inside SEH. With mode 2 and a 0xA01... key it
//    returns 1 (remote_climb=1) or only counts (probe). Never in mode 1 / 3.
//    No lock, no allocation, no logging on the game's thread: relaxed atomics
//    and a lock-free ring.
//
// wallup_send = 0 | 1. Writes 1 to the data byte exe+0x6D76D5C, the gate of
//    the client's UpdateWallUpDataReq (2851) sender 0x87DD00 (its only reader,
//    `cmp byte [rip+0x64F9026], r14b` at 0x87DD2F, verified with its context
//    before writing). The byte must be outside .xtls and the code range, in a
//    writable non-executable section, on a committed RW page; read before
//    (logged), SEH-guarded store, read back. Checked again every second: a
//    reset by the game is logged and written again.
//
// dismount_probe = 0 | 1. Read only, from the mod's thread: every 30 ms, the
//    other player's character(s) (0xA01... with an owning user, not this
//    client's controlled actor; found by walking the client actor manager every
//    second): method, its Enter result (+0x44), both layers' action hashes,
//    TransformSync position, the rider<->vehicle link and the WallUp data. A
//    ring of the last 64 samples gives the 1 s before; a change into/out of a
//    ride method (6/7/10) or of the link opens an episode (also PointClimb 18
//    and WallUp 5, tagged), logged as compact segments up to 5 s after.
//
// divergence_probe = 0 | 1 (pendientes/p8-impl-log-async.md §3). Read only,
//    from the mod's thread, with the same 30 ms samples: the other player's
//    character in memory (TS+0xB4 + sector) against the position this mod's
//    own channel reports for that player (the HUD's; the network thread
//    publishes each new one with remote_motion_channel_position). Over 1.5 m
//    (to the channel's trail of the last 600 ms) for more than 1 s: one line
//    per second with method, actions, control mode (the game's 0x88E8E0, its
//    bytes verified), the interpolation buffer mc+0x3D0 (count, ts[0],
//    ts[last], now - delay, whether the update moved the shown sample) and
//    the local character's method/actions (no verified dialogue/scene flag
//    exists: method 13 = Sequencer is the proxy); one line when it ends.
//
// When: only once the local player has been in the world for 3 s (as
// ui_pause), after the build and the bytes match. One summary every 60 s.

#include <string>

namespace cdmp::session2 {

struct RemoteMotionOptions {
    std::string remote_climb = "0";
    bool wallup_send = false;
    bool dismount_probe = false;
    bool divergence_probe = false;
};

// From the worker thread, once, on the host or on the guest. Starts its own
// thread; no-op when everything is 0 (an unknown remote_climb is warned, off).
void remote_motion_start(const RemoteMotionOptions& opt, bool guest);
// The network thread: a new position of the other player from this mod's
// channel (ClientSession::remote_state). A relaxed load when
// divergence_probe is off; else a few floats under a short SRW lock.
void remote_motion_channel_position(float x, float y, float z) noexcept;
bool remote_motion_active();
std::string remote_motion_describe();  // atomics only ("" when off)

}  // namespace cdmp::session2
