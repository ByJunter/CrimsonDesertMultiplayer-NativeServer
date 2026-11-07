#pragma once
// [session2] seq_marker_guard = 1 (default) | 0, host and guest. Builds 1.0.0.3527 / 3549 and 3576 (3576 static, not yet run).
//
// When another player's character finishes loading on a client, its sequencer component (0xCD03720) binds the field to
// the manager of field->vf48(), which is the LOCAL player's, and that bind (0x9A7F860) leaves the marker at 1. So the
// host's marker fell from 3 to 1 with no load running when the guest arrived, and stayed there. Below 2 the manager
// starts no sequencer, and that is what the death screen hangs from (no UpdateSequencerGamePlayDataEventReq, no UI, no
// way to revive). docs/ESTADO-3527.md has the stack. seq_marker_logic.h has the rule for undoing it.
//
// Its own thread, 20 looks a second with guarded reads and no hook. The only write is that one byte, and only on the
// local character's manager, checked by its vtable. seq_marker_trace=1 (default 0) also logs who calls the manager's
// bind, with an inline hook at its entry: a diagnostic, kept off in the profiles.
namespace cdmp::session2 {

void seq_marker_start(bool enabled, bool guest, bool trace);

}  // namespace cdmp::session2
