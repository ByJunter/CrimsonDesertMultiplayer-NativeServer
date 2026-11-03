#pragma once
// P8: menus do not pause the world while another player is connected
// (docs/research/pendientes/p8-pausa-esc.md, section "Implementado").
//
// [session2] ui_pause_block = 0 (default) | probe | 1 | force, on the host's
// and on the guest's PC (ui_pause_logic.h has the modes and the decision).
//
// One INLINE hook at the entry of 0x35CD990 (.rsrc), the UI time scale the
// frame's dt is multiplied by (callers 0xAD143C / 0xAD14D6 only; rcx = the
// first root scene, result in xmm0, no other argument). Before it goes in:
// the build, its 30-byte prologue (8 whole instructions, no RIP-relative one;
// no rel32/rel8 branch of .rsrc/.xtls/.text/.xtext lands inside it, no
// pointer to it outside .pdata), both callers' `call rel32` and the plain
// `return 1.0f` 0x63DA70 the walk relies on; the hook must patch only .rsrc
// and SafetyHook's patch must end on one of those instruction boundaries.
// Otherwise "UiPause: ... REFUSED" and nothing is written.
//
// The detour calls the original (the only game call) and then, on the game's
// thread, only reads (SEH-guarded, no allocation, no lock): on a change of the
// original value, or every 30 frames while it is not 1, it replays the
// original's walk of the open views to name the one that asked for the value
// and pushes one fixed event to a lock-free ring. It returns 1.0f instead of
// a UI pause / slowdown when mode is force, or 1 while a P8 session is live.
//
// When (P8 review of 10-10, I4): the hook goes in only once the local player's
// controlled actor has existed for 3 s (the world is up), never during the
// game's start-up nor on the title screen; while it waits, one start-up line a
// minute says whether the game clock moves and what safetyhook's trap handler
// did (a fault on a page the mod patched: at once, as a warning). Once in, no
// call of 0x35CD990 (every frame) for 10 s gives one "NO FRAMES" warning.
//
// Its own thread (the install thread, which stays as the drain): every 100 ms
// it recomputes "session live" (host: user 2 logged in; guest: its connection
// plays on the host) and every second formats the events (class names from
// RTTI there, never on the game's thread); every 5 s while a UI scale is in
// force it logs how much the game clock (0x1416B80's globals) moved; every
// 60 s a summary with the blocked frames per UI.

#include <string>

namespace cdmp::session2 {

// From the worker thread, once, on either PC (host: guest = false). Starts
// its own thread; no-op for ui_pause_block=0. `raw` is the ini value.
void ui_pause_start(const std::string& raw, bool guest);
bool ui_pause_active();        // the hook is in
std::string ui_pause_describe();  // atomics only ("" when off)

}  // namespace cdmp::session2
