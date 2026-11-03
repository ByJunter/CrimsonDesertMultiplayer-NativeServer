#pragma once
// P8 session 15: user 2's fast travel (docs/research/pendientes/p8-impl-viaje-rapido.md; fast_travel_logic.h).
//
// F2 HOST (always installed with the host's session 2): user 2's ReloadStageStartReq 2283 is dropped on the network
//    thread before it reaches the host's server unless F3 (user2_funnels, user2_fast_travel=1) is live and
//    ExecuteTroc's subject test passes on user 2's character now, or user 2 is in a field of its own (probe drops
//    too: it only checks that F3 installs). One WRN line per drop (the first 16),
//    counted in the summary. The guest is NOT saved by this alone: its client already started loading and its 2437
//    will be refused (eErrNoInvalidFieldInfoKey -> 2679): that is F1's job.
// F1 GUEST [session2] guest_fast_travel_veto = 0 | probe | 1: an inline hook at the ENTRY of the client's fast-travel
//    function 0x655110 (its only callee that sends the 2283, 0xA9A860, is called from it alone). While the connection
//    plays on the host (guest_remote_live): 1 returns 0 without calling it (no 0x778AB0, no 2283, no loading; the
//    world map's three callers ignore the result) and shows a HUD notice; probe calls it and logs its arguments.
//    Its own thread: build check, the world, the bytes (.rsrc, build 2976), then the hook; lines every 250 ms.

#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "mod/session2/fast_travel_logic.h"

namespace cdmp::session2 {

// HOST, worker thread, once (the value of user2_fast_travel, for the lines only: F3's state is user2_funnels').
void fast_travel_host_configure(const std::string& user2_fast_travel);
// HOST, network thread (session2.cpp offer). judge: reads and decides, no counter, no line (offer may be called
// again for the same Req when the server holds it). record: once per Req, on its final disposition (dropped by F2,
// handed to the server, dropped by policy): counts it and writes its line (the first 16).
struct FastTravelVerdict {
    bool relevant = false;  // a 2283
    bool drop = false;      // F2 drops it: do not hand it to the server
    uint8_t gate = 0;       // ftl::Gate
    std::string line;
    // For the travel probe's TravelBegin (travel_probe.h).
    std::optional<ftl::Reload> reload;
    size_t size = 0;
    uint32_t user2_user = 0, char_key = 0, owner = 0, host_user = 0;
    ftl::Where where = ftl::Where::Unknown;
};
FastTravelVerdict fast_travel_host_judge(uint16_t opcode, std::span<const uint8_t> payload);
void fast_travel_host_record(const FastTravelVerdict& v, const char* outcome);
std::string fast_travel_host_describe();
// HOST, network thread: where user 2's main character is now (the owner of its field-mover field vs the host's and
// user 2's user keys; guarded reads). observer_milestone_reload's "never in user 2's own field".
ftl::Where fast_travel_user2_where(uint32_t* owner = nullptr) noexcept;

// GUEST, worker thread, once. No-op with 0 (or an unknown value: warned).
void guest_fast_travel_start(const std::string& mode);

}  // namespace cdmp::session2
