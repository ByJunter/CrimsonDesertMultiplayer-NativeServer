#pragma once
// P8 S2, HOST: the native PlayAllPreLoadSequencerAck 2322 for user 2 when it
// completes an entry, as an OBSERVER, into a field it does not own
// (docs/research/pendientes/p8-2322-observador.md). Two [session2] keys, both
// 0 by default, both need create_session=1:
//
//   observer_milestone=1       DECIDE AND LOG ONLY (stage A): every trip of user 2
//                              (one per 2174) is followed through take_frame (2174,
//                              2144, 3066, 2322, 2762, 2042) and inject (2549, 2445);
//                              at each of its observer entries (the 0x2BC84E2 hook of
//                              observer_stage) the decision is taken and logged as
//                              "WOULD EMIT" or "refused: why". Nothing is called.
//   observer_milestone_send=1  EMIT (stage B; implies the first): the same decision,
//                              and when it says yes the hook calls, on the same thread
//                              and in the same MoveFieldCompleteCore, the game's own
//                              ToTarget constructor 0x2A757A0(finder, traveller) and
//                              emitter 0x2999D80(traveller, finder), once per trip.
//
// observer_stage_sync is unchanged and never sends (=2 is still refused); the
// entry hook these keys need is observer_stage's: they install it in measure
// mode when observer_stage_sync=0.
//
// Threads: the decision and the call run inside the entry hook (the server's
// logic thread, the one MoveFieldCompleteCore runs on); take_frame calls
// observer_milestone_on_out on whatever thread sends to session 2 (the call's
// own 2322 among them, synchronously); inject / epoch / logout on the mod's
// network thread. One lock around the ledger, never held while the game runs.
// Every line is a fixed record in a lock-free ring, formatted once a second by
// observer_stage's own thread (never a game or network thread).

#include <cstdint>
#include <span>
#include <string>

#include "mod/session2/fast_travel_logic.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/observer_milestone_logic.h"
#include "mod/session2/observer_milestone_reload_logic.h"

namespace cdmp::session2 {

// session2::install, before observer_stage_install. Off when both are false.
oms::Mode observer_milestone_configure(bool diag, bool send);
oms::Mode observer_milestone_mode();
bool observer_milestone_active();  // configured and its code verified

// observer_stage's install thread, after the game's code is there and BEFORE
// its own hooks go in: compares every instruction the emission and the
// decision rely on with build 2976. False = off for the run (logged).
bool observer_milestone_verify(uintptr_t base, std::span<const gad::Section> secs);
// observer_stage's install thread: the entry hook could not be installed.
void observer_milestone_disable(const char* why);

// observer_stage's entry hook (0x2BC84E2): R13 = field, R14 = traveller.
void observer_milestone_on_entry(uintptr_t field, uintptr_t actor) noexcept;

// server.cpp. take_frame, after g_outgoing.push, for oms::followed_out opcodes:
// self = the session written to, pushed = the push's result, value = the 2174's
// original owner (0 = unknown layout) / the 2322's actor key.
void observer_milestone_on_out(uint16_t opcode, uintptr_t self, bool pushed, uint32_t value) noexcept;
// inject, after the push: a followed Req of user 2 reached the server's queue.
void observer_milestone_on_in(uint16_t opcode) noexcept;
// server_begin_epoch / a replaced server instance: cancel whatever is pending.
void observer_milestone_on_epoch(uint32_t epoch, const char* why) noexcept;
// server_logout_user2: an undecided trip will never get a 2322.
void observer_milestone_on_logout() noexcept;

// observer_stage's thread, once a second: the queued lines and the follow-ups.
void observer_milestone_drain();
std::string observer_milestone_counters();  // the 60 s line
std::string observer_milestone_describe();  // atomics only

// ---- P8 S16: [session2] observer_milestone_reload = 0 | probe | 1 (observer_milestone_reload_logic.h,
// pendientes/p8-impl-viaje-rapido.md §12). User 2's reload trip (2283 accepted by F3 -> 2437 -> 2643/3066, no
// 2174): mode 1 drops its PreLoadSequencerReq 2873 on the network thread and emits the 2322 for its character ONCE
// at the exit of its next ExecuteTroc (>= 300 ms later) with this module's verified emitter; probe lets the 2873 go
// and logs the same decision. Needs observer_milestone (verified; SEND for mode 1) and user2_fast_travel=1 (F3 and
// ExecuteTroc's subject). HOST only.
omr::Mode observer_milestone_reload_configure(const std::string& value);  // session2::install (after the above)
omr::Mode observer_milestone_reload_mode();
// The network thread (session2 offer). judge: no state change (a held Req is offered again); record: once, on the
// 2873's final disposition (handed = it went to the server).
struct ReloadVerdict {
    bool relevant = false;  // user 2's 2873 and the key is not 0
    bool drop = false;
    omr::Gate gate = omr::Gate::NotRelevant;
    ftl::Where where = ftl::Where::Unknown;
};
ReloadVerdict observer_milestone_reload_judge(uint16_t opcode) noexcept;
void observer_milestone_reload_record(const ReloadVerdict& v, bool handed) noexcept;
// ---- P8 S27: [session2] observer_milestone_reload_any = 0 | probe | 1 (docs/research/s25/reaparicion.md). User 2's
// 2437 opens a reload trip whenever none is open (a revive ReviveLoadingReq 2362 has no 2283, so no F3 trip), with
// the same classification and the same drop + emission as an F3 trip; probe: those trips only say WOULD. Needs
// observer_milestone_reload=probe|1 and observer_milestone_send=1 (else off, logged). HOST only.
omr::Mode observer_milestone_reload_any_configure(const std::string& value);  // session2::install (after the above)
omr::Mode observer_milestone_reload_any_mode();
// The server's thread (user2_funnels' ExecuteTroc detour, user 2's Reqs only).
void observer_milestone_reload_on_f3() noexcept;  // its 2283 returned, F3 gave its manager / the block
// User 2's Req that starts a load (2283 / 2362) entered the server: kept for its next 2437's line (forgotten at a
// new epoch / a logout).
void observer_milestone_reload_note_load_req(uint16_t opcode) noexcept;
// Before user 2's GameLoadingStartReq 2437 runs. ts_packed = user 2's TS before the 2437 (tpl::pack, 0 unread), for
// the line only.
void observer_milestone_reload_before(uint16_t opcode, uint64_t ts_packed) noexcept;
void observer_milestone_reload_at_exit(uintptr_t actor) noexcept;  // after any Req of user 2 (cheap when not armed)
// take_frame, for omr::followed_out opcodes (value: the 2322's actor key).
void observer_milestone_reload_on_out(uint16_t opcode, uintptr_t self, bool pushed, uint32_t value) noexcept;

}  // namespace cdmp::session2
