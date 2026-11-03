#pragma once
// P8 session 13, the shopkeepers' server side (docs/research/pendientes/p8-impl-tenderos-c1.md,
// p8-s12-tenderos-servidor.md §5.1), HOST only: [session2] shop_probe = 0 | 1 (default 0). READ ONLY: nothing is
// written to the game, no game function is called, no packet is changed, dropped or sent.
//
// Mid hooks (build 1.0.0.2976, .rsrc; bytes and context compared before anything is written, p8_install_mid):
//   E 0x288B7A0  the stage update's entry (mgr, field, actor): per character, how often its manager is updated.
//   G 0x288B87A  the owner gate cmp [r15+0x180],eax (r13 = the manager's character, r15 = the field, eax = the
//                character's user key): per character, owner / not owner. User 2's manager is expected to be turned
//                away every time (p8-s12-tenderos-servidor.md §2.2).
//   P 0x288AAEF  right after 0x288A970's call 0x287C200 (a stage element's sequencer prepared): the resource's base
//                name ([[[elem+8]]]), the owner ([rbp-0x51]), the sequencer id 0x287C200 wrote to playData+8
//                ([rbp-0x49]), the result [r14], the instance [rbp+0x77], the element's binding data [elem+0x200] and
//                the manager's character. cd_seq_funcnpc_* ones are logged and their ids kept (the taps tag them).
//   C 0x29E9282  the 2671 handler right after call 0x2B36860 (.xtls 0x1067B660 -> 0x2B36EF0): the result code
//                [rbp-0x58] (0 = accepted), the requester / NPC / interaction keys, and who asked (user 2 / host).
//   S 0x2126A1F / 0x2126AAB / 0x2126D11  the SERVER's own call of the interaction entry check 0x2126920 (the field
//                at [rsp+0x48] a ServerField): the entry's stage id, the manager vf48 gave (the field owner's), whether
//                the lookup found it and the result BL, for user 2's requests (the host's are counted).
// Taps (no hook): user 2's C>S 2671 / 2720 / 2436 / 2591 as they reach the server path (with the verdict of
// observer_seq_copy's filter R1: dropped or passed, unchanged), and the S>C 2663 / 3000 / 2916 / 2525 / 2981 / 2524
// queued for user 2. Hooks and taps: SEH reads, atomics, a bounded lock-free ring; lines 'ShopProbe [...]' and a
// summary every 60 s from the module's own thread.

#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

// S2 (p8-impl-tenderos-s2.md), [session2] funcnpc_twin = 0 | probe | 1 (1 is held as probe in this build): at P, for
//   each cd_seq_funcnpc_* the server prepared for the field owner's character, whether a twin would be prepared for
//   user 2 (its character and ServerSequencerStageManager known and readable; one per host scene id), with the host's
//   playData logged raw (the twin's would differ in +0 owner and +8 id). Nothing is called or written. Lines
//   'FuncnpcTwin [would create|no twin]' and 'FuncnpcTwin summary' every 60 s. Without shop_probe only P goes in.
//
// session2::install (host, create_session=1). Starts the install / drain thread. No-op when both are off.
void shop_probe_start(bool on, const std::string& twin_mode);
bool shop_probe_active() noexcept;
// P8 option U, U-a: the owner gate G 0x288B87A is hooked here (it calls user2_stages_gate). Any thread.
bool shop_probe_gate_live() noexcept;
// P8 U2 (user2_shops.h, review B1): the points U2 needs are installed: S/A 0x2126A1F (U2-b) and P 0x288AAEF (the book).
bool shop_probe_route_points_live() noexcept;
// The offer path of a guest Req (network thread), after observer_seq_copy_filter_req: filtered = it dropped it.
void shop_probe_on_user2_req(uint16_t opcode, std::span<const uint8_t> payload, bool filtered) noexcept;
// take_frame (a game thread): a packet for session `to_user2` (the live session 2) was queued (pushed) or not.
void shop_probe_on_out(uint16_t opcode, std::span<const uint8_t> payload, bool to_user2, bool pushed) noexcept;

}  // namespace cdmp::session2
