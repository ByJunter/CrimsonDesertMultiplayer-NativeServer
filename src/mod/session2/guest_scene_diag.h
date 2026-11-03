#pragma once
// P8 S1, NPC scenes: the GUEST's measurement probes G1-G4
// (docs/research/pendientes/p8-s1-escenas-npc.md §5.2 and "Implementado para la
// medida"). [session2] guest_scene_diag=1 (default 0), guest=1 only. READ ONLY:
// nothing is written to the game, no game function is called, nothing is sent
// or held.
//
//   G1  0x1EB8946 + 0x206DE6B  each NPC's AI block read from its 2005 (vf11
//       0x206DBD0 -> 0x1EB88C0): resource, node, scene key, and the handle
//       0x1869D70 gave (valid / file loaded but node missing / FILE NOT LOADED)
//   G2  every 5 s on the diagnostics thread, a read-only replica of the lookup
//       in pa::NPCScheduleManager's container ([exe+0x6C8CDA0]+8,
//       ThreadSafeRefCountedContainer<u32, NPCScheduleData_File>: 0x186A810 +
//       0x1869D70) for the resources G1 saw: "file not loaded" vs "node not
//       found" (bounded walk, SEH reads, no lock, no call)
//   G3  every 5 s, X = the host character's stage manager inside the guest
//       ([[[A+0x68]+0xA8]+0x18], A found with our replica of
//       ClientActorContainer::Find): X+0x148 (the schedule stages: n and the
//       first elements), X+0x174, X+0x231; a line when it changes
//   G4  the guest's own C>S scene/sequencer Reqs (2767/2591/2436/2547/2173/
//       2283/2138/3020/2100 and 2223 own / NPC), counted per opcode and scene
//       id from guest_send (the client NetworkThread, a fixed record)
//
// Hooks (G1 only): patch inside .rsrc, whole instructions, no branch into it,
// its bytes and the code its register reading relies on compared with build
// 1.0.0.2976 first (p8_install_mid). Not a GuestStage / P8Probe point.
// Callbacks: SEH-guarded reads into a bounded lock-free ring; no allocation, no
// CRT, no game call. Lines ("GuestScene [...]") are formatted by
// guest_scene_diag_tick() on the guest's diagnostics thread
// (guest_diag_worker.h), never on the network thread.

#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

// From guest_start (guest=1). Starts a one-shot install thread. No-op when `on` is false.
void guest_scene_diag_start(bool on);
bool guest_scene_diag_wanted();
// The guest's diagnostics thread, every 5 s.
void guest_scene_diag_tick() noexcept;
std::string guest_scene_diag_describe();  // atomics only; empty when off

// guest_send (the client NetworkThread), after the push to the host: G4.
void guest_scene_diag_on_out(std::span<const uint8_t> packet) noexcept;
// deliver_all (the client NetworkThread), after OnRecvPacket: when each NPC 2005 / scene packet reached the game.
void guest_scene_diag_on_delivered(std::span<const uint8_t> packet) noexcept;

}  // namespace cdmp::session2
