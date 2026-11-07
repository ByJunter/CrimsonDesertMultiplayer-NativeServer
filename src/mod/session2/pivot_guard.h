#pragma once
// [session2] change_pivot_guard = 1 (default) | 0, host and guest. Builds 3527, 3549 and 3576 (3576 static, not yet run).
//
// A scene's ChangePivot command whose target resolves to nobody on this client read a null pointer and closed the game
// (crash dump of 2026-11-05, first session between two PCs: the guest, playing Damiane, 0.7 s after a fast travel it had
// done twice before as its main character). The guard sends that one case to the function's own way out: the scene goes
// on without changing its pivot. It does not explain why the target is missing. Rule and addresses: pivot_guard_logic.h.

namespace cdmp::session2 {

// From the worker at start-up. Its own short thread waits for the world, verifies the code, hooks it and ends.
void pivot_guard_start(bool enabled, bool guest);

}  // namespace cdmp::session2
