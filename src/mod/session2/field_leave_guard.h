#pragma once
// [session2] field_leave_guard = 1 (default) | 0, both PCs, builds 3527 and 3549. The other player's character leaving a field
// no longer unloads the sequencer manager of that field's owner: the host keeps its town NPCs when the guest leaves.
// The rule and the evidence: field_leave_guard_logic.h.

namespace cdmp::session2 {

// From the worker, once. Its own thread waits for the world, verifies the code and hooks it.
void field_leave_guard_start(bool enabled, bool guest);

}  // namespace cdmp::session2
