#pragma once
// P8 strategy B, step 2 (docs/research/pendientes/p8-impl-owner-fanout.md; p8-arquitectura-observador.md
// §3.4 and §7.2): the owner fan-out PROBE. HOST only, needs create_session=1.
//
// [session2] owner_fanout = 0 (default, nothing installed) | probe. The value 1 (step 3: really add
// user 2's session) is reserved and refused with a warning.
//
// An inline hook at ToTarget::vf1 0x2A757F0 (14 instructions in .rsrc, shared with ToControlUser; its 95
// bytes verified): every Ack writer with a ToTarget finder calls it as `finder->vf1(finder, &tls_array)`,
// and its return address names the writer (owner_fanout_writers.h, generated from the scan) and so the
// opcode. After the original, while user 2 is on an OBSERVER trip in the host's field (the rule of
// observer_seq_copy: user 2's MoveFieldAck as the server wrote it names an owner user other than user 2),
// a call whose finder is ToTarget and whose target is the HOST's session is counted by writer: one Ack
// step 3 "would add user 2 to" if the writer is listed (§3.4). Nothing is changed or sent.
//
// In the hook: one relaxed counter when no trip is on; during a trip, a few SEH-guarded reads, the
// live-session check and relaxed counters in fixed tables. No lock, no allocation, no log line.
//
// Comparison (60 s summary): per opcode, the hook's count against what the host's client received
// during the trips (the sniffer, when [sniffer] enabled=1) and against option A's copies queued for
// user 2 (server_push_extra): 2661/2810/2322 should agree.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

// session2::install, before the tunnel opens. raw = the ini value.
void owner_fanout_configure(const std::string& raw);
bool owner_fanout_wanted();  // probe configured (the hook may still have been refused)
// session2::install, after server_install, before observer_stage_install: verifies 0x2A757F0's 95 bytes
// (inside .rsrc) and the ToTarget vtable, then hooks. False = refused (logged), nothing written.
bool owner_fanout_install(uintptr_t base);
bool owner_fanout_active();  // hooked

// take_frame (server.cpp), for every MoveFieldAck 2174 written to a session 2: begins or ends the
// observer trip. server_owner = the owner as the SERVER wrote it.
void owner_fanout_on_move_field(uintptr_t self, bool pushed, uint32_t server_owner) noexcept;
// server_push_extra (server.cpp): a copy option A queued (or not) for user 2.
void owner_fanout_on_extra(std::span<const uint8_t> packet, bool queued) noexcept;
// The sniffer's writer thread: one whole packet of the host's own client.
void owner_fanout_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept;
// The network thread, every loop turn: user 2 gone ends the trip; its lines; the 60 s summary.
void owner_fanout_tick() noexcept;
std::string owner_fanout_describe();  // atomics only ("" when off)

// The bytes of the game's code at [addr, addr+n) as they were before this module's hook: a guarded
// read with the hook's original bytes laid over its patch. For the modules that verify ToTarget::vf1
// byte for byte (observer_milestone, observer_scene_copy). False if the read fails.
bool owner_fanout_read_unpatched(uintptr_t addr, void* out, size_t n);

}  // namespace cdmp::session2
