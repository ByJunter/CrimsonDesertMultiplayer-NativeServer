#pragma once
// [session2] achievement_guard = 1 (default) | 0, GUEST, builds 3527 and 3549. Once this game has played in a host's
// world, it unlocks no Steam achievement (nor progress stat) until it is restarted: there it holds the host's progress,
// and on 2026-11-05 it gave the guest's account every achievement of the host's. The rule: achievement_guard_logic.h.

namespace cdmp::session2 {

// From the worker, once, on the guest. Its own thread waits for the world, verifies the code and hooks it.
void achievement_guard_start(bool enabled);

}  // namespace cdmp::session2
