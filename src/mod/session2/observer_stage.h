#pragma once
// P8, user 2 as an observer of the field's existing stages/sequencers
// (docs/research/pendientes/p8-observador-escenas.md). HOST side, [session2]
// observer_stage_sync (default 0; needs create_session=1):
//
//   0   nothing is installed.
//   1   MEASURE ONLY (read-only): five checked mid hooks at .rsrc function
//       entries / one instruction of MoveFieldCompleteCore record every
//       PrepareSequencerAck 2661, DeleteSequencerAck 2810, StageBranchStateAck
//       2981 and PlayAllPreLoadSequencerAck 2322 the server serializes (opcode,
//       wire size when it is fixed or computable, addressed actor and user,
//       finder kind, sequencer id, node -> actor bindings) and every field
//       entry. A ledger of the live sequencers says, when a user enters a field
//       it does not own, which of the owner's sequencers (and the NPCs they
//       bind) never reached it and that no 2322 will: the snapshot an observer
//       would need. Follow-ups at +10/+60/+300 s count what reached it.
//   >=2 the SEND mode is NOT implemented and is refused (runs as 1): the
//       native snapshot path could not be isolated (2661's only producer is
//       the sequencer preparation 0x287C200, 2981's emitter is reached only
//       from .xtls; see the document's §5-§6), and phase 2 found that a byte
//       replay is not coherent: user 2's client routes every stage Ack to the
//       manager of its own field owner (itself), never to the host
//       character's manager its NPCs search ("Fase 2", §13-§15). Nothing is
//       ever sent by this module and no game function is called. In
//       particular =2 does NOT send the 2322: that S2 experiment is the
//       separate key observer_milestone_send=1 (observer_milestone.h), which
//       uses this module's entry hook (0x2BC84E2) and installs it in measure
//       mode when observer_stage_sync=0; the hook then calls
//       observer_milestone_on_entry, which returns at once when it is off.
//       Likewise [session2] observer_scene_copy=1 (observer_scene_copy.h) uses
//       its 2810 hook (0x2A4E4C0): the hook calls observer_scene_copy_on_delete,
//       which returns at once when it is off and otherwise may call the native
//       2810 emitter once more for user 2 (a scene of user 2's), on that thread.
//   The 2661/2810/2981/2322 lines carry a "corr=op:actor:seq:value" token the
//   guest's guest_stage_diag lines print too; an owner's entry into a new field
//   drops what was prepared for it before; the PLAN line lists the native order.
//
//   0x2A4DEF0  2661 serializer entry   RCX actor, RDX finder, R8 play data (+0 owner, +8 seq), R9 bindings
//                                     vector {ptr, u32 n @+8}, 12-byte entries (u32 node, u32 actor)
//   0x2A4E4C0  2810 serializer entry   RCX actor, RDX finder, R8 -> u64 seq, R9 -> u32
//   0x2A4F2D0  2981 serializer entry   RCX actor, RDX finder, R8 -> u64 seq, R9 -> u32
//   0x2999D80  2322 emitter entry      RCX actor, RDX finder
//   0x2BC84E2  MoveFieldCompleteCore after the owner branch (both paths): R13 field, R14 traveller
//
// Hooks: patch inside .rsrc, bytes and the code they rely on compared with
// build 2976 first, relocated bytes exactly the verified instructions
// (p8_install_mid). Callbacks: SEH-guarded loads into a bounded lock-free
// ring, no allocation, no CRT, no game call (except the two keys above, each
// in its own module, when on). A thread of its own formats and keeps the
// ledger (once a second).

#include <cstdint>
#include <string>

namespace cdmp::session2 {

// From session2::install (late-init thread), after server_install. Starts its
// own thread (waits for the game's code like the P8 probes). False when off.
bool observer_stage_install(uint32_t ini_value);
bool observer_stage_active();
std::string observer_stage_describe();  // atomics only

}  // namespace cdmp::session2
