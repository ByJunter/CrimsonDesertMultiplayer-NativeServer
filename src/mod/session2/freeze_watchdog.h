#pragma once
// P8 session 10 (docs/research/pendientes/p8-s10.md §2; implementation: p8-impl-freeze-watchdog.md):
// [session2] freeze_watchdog = 0 | 1, host AND guest. Read only.
//
// A thread of the mod polls a main-loop frame counter every 25 ms. The counter is advanced by hooks that
// already run once per main loop turn on the game's main thread: ui_pause's inline hook on 0x35CD990 (both
// roles, ui_pause_block != 0) and guest_seq_play's mid hook on 0x8B6144 (guest). The FIRST thread that ticks
// them is the main thread; a tick from another thread is not counted and is reported (fwd::MainThread). Without
// either, the D3D12 Present counter and the present thread are used (said once).
//
// No frame for > 300 ms: an episode. The watchdog then samples the frozen thread: SuspendThread,
// GetThreadContext, a copy of at most 16 KB of its stack from rsp (committed pages only, into a static buffer
// of the mod: no allocation, no lock, no log, no SEH while it is suspended), ResumeThread. Only after that,
// on the mod's thread, the line: RIP (module+RVA), and the first 16 stack values that look like return
// addresses (a call instruction right before them) inside the exe, cdmp.asi or private executable memory
// (safetyhook's trampolines and mid-hook stubs), flagged [mod] / [stub]; plus the mod hook that thread is in,
// or the one it last left (every mid hook installed through p8_install_mid is wrapped with a mark when this
// is on; the mark is RAII). 5 samples 50 ms apart, then one per second; the caps are on SUSPENSIONS: at most 20
// per episode and 600 per session (then episodes are only timed). One END line with the duration when frames
// come back. The module list and stub regions are refreshed only between episodes.

#include <safetyhook.hpp>

#include <cstdint>
#include <string>

namespace cdmp::session2 {

// From the worker, right after the config is read and BEFORE any module installs a hook: with `on`, mid hooks
// installed afterwards through p8_install_mid get the thread mark. Starts the watchdog thread.
void freeze_watchdog_start(bool on, bool guest);
bool freeze_watchdog_wanted() noexcept;

// A main-loop frame, from a hook on the game's main thread: two relaxed stores.
void freeze_watchdog_frame() noexcept;

// p8_install_mid: the callback to install for this hook. A wrapper that marks the thread while `fn` runs when
// the watchdog is on and a wrapper slot is free; else `fn` itself (nothing changes).
safetyhook::MidHookFn freeze_watchdog_wrap_mid(safetyhook::MidHookFn fn, const char* tag, const char* what);

// For an inline detour: an id once (at install), then a FreezeMark on the stack of the detour.
uint16_t freeze_watchdog_register(const char* tag, const char* what);
class FreezeMark {
public:
    explicit FreezeMark(uint16_t id) noexcept;
    ~FreezeMark();
    FreezeMark(const FreezeMark&) = delete;
    FreezeMark& operator=(const FreezeMark&) = delete;

private:
    void* mark_ = nullptr;
    uint16_t id_ = 0, prev_ = 0;
};

}  // namespace cdmp::session2
