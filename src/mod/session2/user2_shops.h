#pragma once
// P8 option U, step U2 case A (docs/research/s25/tiendas-u2.md, user2_shops_logic.h), HOST only:
// [session2] user2_shop_route = 0 (default) | probe | 1. The guest's OWN shop scenes far from the host: with U-1 armed
// (user2_stages=1) and user2_stages_elements=schedule+funcnpc - or, since S46 phase 3, user2_shop_trade_list=1 (T1:
// only smithy / butcher by the exact hash of the name, s46-tenderos.md §9) - user 2's manager prepares the
// cd_seq_funcnpc_* scenes of its cells (a 2661 to user 2, its M natively); this module sends that scene's Reqs to the
// manager that has it. Nothing here depends on which of the two keys let the scene through.
//   U2-a  user2_funnels' ServerField::vf48 detour, at the return address of each scene handler's own vf48 call
//         (user2_shops_logic.h kSites; the 2044 timer too): the Troc's sequencer id (its packet) live in user 2's
//         manager and not in the original answer -> user 2's manager. Any subject (only user 2's client knows those
//         ids: its 2661 goes ToTarget). The other vf48 of such a Troc are counted (the first of each (opcode, return)
//         is a WRN line, review M2), never changed.
//   U2-b  shop_probe's mid hook at 0x2126A1F (the server's 0x2126920, the 2671's entry check): the entry's stage id
//         [r13+0x20] live in user 2's manager -> RAX := user 2's manager (whoever asks).
//   hold  user2_stages' DISTANCE drain waits while user 2 is in a shop of its own scene (a 2916 of the shop
//         interaction 0x000F4321 right after a U2-b check of its own entry): at most 120 s, only while the players are
//         more than 5 cells apart, once per arm (review A1). Another field, its loads and its logout never wait.
//   probe: every decision counted and logged as "would", nothing routed. With user2_stages_elements=schedule+funcnpc
//         the shop scenes ARE prepared in probe too (review M1, the step A2): their Reqs then reach the host's
//         manager natively (lost, or a Nak) and the module says what it would have done.
// Requirements (else U-f lets no shop scene through): shop_probe=1 with its points S 0x2126A1F and P 0x288AAEF in,
// user2_funnels' vf48 / subject hooks, the call sites of 2436 / 2173 / 2767 verified (review B1).
// Abort criteria (mode 1 -> probe for the rest of the run, one ERR line): user2_shops_logic.h u2sh::Abort.
// The server runs its handlers on SEVERAL threads (review M3): everything here is atomics; the lookups are read-only
// replicas (a table changing under another thread gives "not found": not routed).

#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

// session2::install (host, create_session=1). No-op when the key is 0. shop_probe: shop_probe=1.
void user2_shops_start(const std::string& route_mode, bool shop_probe);
// user2_funnels: whether to read the scene Trocs' sequencer ids at all (the key is not 0). An atomic.
bool user2_shops_wanted() noexcept;
// U-f (user2_stages): shop scenes may be prepared for user 2 (probe or 1, not aborted, every requirement in).
bool user2_shops_may_prepare() noexcept;
// mode 1 and may_prepare: the route really answers with user 2's manager.
bool user2_shops_route_live() noexcept;
// U-f: the drain is waiting for a shop now (no new shop scene meanwhile).
bool user2_shops_waiting() noexcept;
// U2-a, from user2_funnels' vf48 detour inside a Troc whose opcode is a scene site's: 0 = keep `original`; else user 2's
// manager. ret_rva: the vf48 call's return; another return of that Troc is only counted, and only once its site said
// the id is user 2's (mine_troc). mine: this call's site found the id in user 2's manager (routed, or would in probe).
uintptr_t user2_shops_vf48(uint16_t opcode, uint64_t seq_id, uint32_t ret_rva, uintptr_t field, uintptr_t original,
                           bool mine_troc, bool& mine) noexcept;
// U2-b, from shop_probe's on_sa (the SERVER's 0x2126920 only): 0 = keep RAX; else user 2's manager.
uintptr_t user2_shops_server_check(uintptr_t field, uint64_t entry_stage_id, uintptr_t original,
                                   bool host_requester, bool user2_requester) noexcept;
// shop_probe's on_c: the host's own 2671 result (the HostRefused criterion).
void user2_shops_host_2671(uint32_t result) noexcept;
// shop_probe's on_p (0x288AAEF): a stage element prepared by 0x287C200 on `mgr` (user 2's are booked / checked).
// name_known: the resource name could be read (an unknown name is never "not a shop"); hash: playData +0x20, the
// resource's file-name hash (the 2661's @27).
void user2_shops_prepared(uintptr_t mgr, uint64_t seq_id, bool funcnpc, bool name_known, uint32_t hash,
                          uint32_t result) noexcept;
// server.cpp's take_frame: a packet queued for the live session 2 (2916 / 3000 / Nak 1013 are read). Any thread.
void user2_shops_on_out(uint16_t opcode, std::span<const uint8_t> payload, bool to_user2, bool pushed) noexcept;
// user2_stages' control (its thread): should U-1's DISTANCE drain wait now? dist: the cells between the players
// (UINT32_MAX unknown). Counts "would" in probe.
bool user2_shops_hold_drain(uint64_t now_us, uint32_t dist) noexcept;
// user2_stages: a new arm, any drain, or user 2's character changed / left: the shop window and the wait start over.
void user2_shops_reset(const char* why) noexcept;

}  // namespace cdmp::session2
