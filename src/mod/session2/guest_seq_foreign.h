#pragma once
// S44 (docs/research/s25/s44-escenas-del-anfitrion.md), GUEST: the host's scenes on the guest's client. [session2]
//
// guest_seq_foreign_player = 0 | probe | 1 (default 0 = exactly as before S44). In guest_seq_route's EXISTING mid hook
// at 0xC0612A (no second hook there): when a 2661 goes to X (guest_seq_route's Route: its owner is another player's
// character, not this client's) the binding to that owner in 0xC05BB0's own binding vector ([rbp-0x80], 12-byte
// entries, count [rbp-0x78]) has its type 5 (or 4) set to 0 before 0xA9CA40 reads it: the engine then never resolves
// the other player's copy for the scene (0x205DC30 ignores type 0), so no alignment, no SequencerControl and no
// track event reaches it; its state keeps coming from its own client over the network. probe logs '[would foreign]'
// and writes nothing. NPC / gimmick / shop bindings are never touched; scenes of this client's own character neither.
// The handler's bytes (vector, call, copy in 0xA9CA40) are verified at install; otherwise nothing is read nor written.
// Review S44 H-1: type 0 is the native "unbound node", not inert (0x5EB310 / 0x205E520), and the owner is not always at
// the player node. S44 live (the first boss-memory rematch stayed frozen while it waited to be learnt): 1 neutralizes
// from the first appearance, and P1's learnt map holds the EXCEPTIONS: a (resource, node) seen with the owner at a node
// that is not CurrentControl is never touched again (a WRN '[foreign NOT player node]' names the instance already done).
//
// guest_seq_foreign_probe = 0 | 1 (default 0), READ-ONLY, mid hooks at verified .rsrc points (build 2976, .pdata,
// 40-byte signature): P1 the entry of 0x602940 (a scene's start: its actor list stage+0x120, node type node+0xAC, key,
// link type; it also feeds the learnt map), P2 the entries of vf21 0x601470 / vf22 0x601C10 (SequencerControl: which
// bound keys 0x205DC30 would hand it to, and desc+0 = [stage+0x98]), P3 0x6034FA in 0x6030D0 (right before the
// alignment's TransformSync vf1A0), P4 the entry of 0x5EB310 (a client actor order on a type-0 node). P1 and P4 are also
// installed with guest_seq_foreign_player != 0. Atomics and a lock-free
// ring in the hooks; lines 'GuestSeqForeign [...]' from the guest's diagnostics thread (budgeted) and a summary every
// 60 s. Actors are identified by key, never by address.

#include <safetyhook.hpp>

#include <cstdint>
#include <string>

namespace cdmp::session2 {

// From guest_start (guest=1), after guest_seq_route_start. probe: guest_seq_foreign_probe; player: the raw value of
// guest_seq_foreign_player; route_mode: guest_seq_route's raw value (the fix acts only through its 0xC0612A hook).
// present (S45, guest_seq_present.h): guest_seq_foreign_present_probe or guest_seq_foreign_present (R-A) on
// (guest_seq_present_needs_p1); P1 is installed for it too and hands every stage start, with its resource, to
// guest_seq_present_p1 (S-A, R-A).
void guest_seq_foreign_start(bool probe, const std::string& player, const std::string& route_mode, bool present = false);
// S45: the install thread has settled (installed or refused all it wanted), and P1 0x602940 is hooked.
bool guest_seq_foreign_settled() noexcept;
bool guest_seq_foreign_p1_hooked() noexcept;
// guest_seq_route_hook, point 2661, for a Route / WouldRoute verdict only (the game's main thread, inside 0xC05BB0's
// frame: ctx.rbp is its frame). owner: the packet's owner @5; m_key: M's character; seq / resource: the packet's;
// actor: the actor the handler resolved ([r12], 0xC060C2), only read (its Status bit 0x200 in the line, review H-3).
void guest_seq_foreign_2661(safetyhook::Context& ctx, bool routed, bool would_route, uint32_t owner, uint32_t m_key,
                            uint64_t seq, uint32_t resource, uintptr_t actor) noexcept;
void guest_seq_foreign_tick() noexcept;  // the diagnostics thread, every 5 s

}  // namespace cdmp::session2
