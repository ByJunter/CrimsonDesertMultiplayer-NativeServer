#pragma once
// P8 session 15, HOST: user 2's fast-travel reply path (docs/research/pendientes/p8-impl-viaje-rapido.md §11;
// travel_probe_logic.h).
//
// [session2] user2_loading_owner_alias = 1 (default) | 0: user 2's copy of LoadingFieldDataAck 2643 names user 2 as
//    the field owner (u32 +9 only, layout 20 + 48n checked), as server.cpp already does for MoveFieldAck 2174. The
//    server's field (+0x180), the host's copy and the observer modules keep the real owner.
// [session2] user2_travel_probe = 0 (default) | 1: read-only lines, no new hook (it reuses F2's network-thread
//    judge, ExecuteTroc's subject and the vf48 detour of user2_funnels - so its server lines need
//    user2_fast_travel=1 - and server.cpp's take_frame):
//      TravelBegin    user 2's 2283 on the network thread: WakeSpot u32, FieldInfoKey, actor selector, transform,
//                     where user 2 is and F2's outcome;
//      TravelContext  each vf48 of that 2283 (0x2A5F7FC, 0x2BC4C53): the owner's manager (read, never called), the
//                     one returned and its +0x18;
//      TravelPrepared around the 2283's ExecuteTroc: user 2's TransformSync +0x550/+0x568/+0x569/+0x591 before and
//                     after, the host's TS and its manager's +0x6A0 before and after, the setter seen, the result;
//      TravelConsume  around the 2437's ExecuteTroc: user 2's +0x550 before/after, the branch, the result;
//      TravelAck      the Acks queued for user 2 after them (opcode, size, owner as written / as user 2's copy
//                     says), then one line with the reply class (2643/3066, 2174, 2679, Nak), the 2322s to user 2
//                     and (P8 S16) the host client's 2661/2981/2322 burst after user 2's 2873 (the sniffer: needs
//                     [sniffer] enabled=1): "SIDE EFFECT" when its 2873 went to the server and re-prepared the host's.
//    Game threads only push fixed-size events into a lock-free ring; its own thread formats them. Bounded.

#include <cstdint>
#include <span>
#include <string>

#include "mod/session2/fast_travel_logic.h"
#include "mod/session2/travel_probe_logic.h"

namespace cdmp::session2 {

// HOST, worker thread, once (create_session=1). Starts the probe's thread when user2_travel_probe=1.
void travel_configure(bool loading_owner_alias, bool travel_probe);
// Any thread (atomics).
bool travel_loading_owner_alias() noexcept;
bool travel_probe_on() noexcept;

// Network thread (fast_travel.cpp record, once per 2283 of user 2).
struct TravelBeginInfo {
    const ftl::Reload* reload = nullptr;  // null: not the 53-byte layout
    size_t size = 0;
    uint32_t user2_user = 0, char_key = 0, owner = 0, host_user = 0;
    const char* where = "";
    const char* gate = "";
    const char* outcome = "";
};
void travel_probe_begin(const TravelBeginInfo& b);

// Server thread (user2_funnels' ExecuteTroc detour): user 2's 2283 / 2437 enters the server. Returns the trip number.
uint32_t travel_probe_open_window(uint16_t opcode) noexcept;
uint32_t travel_probe_trip() noexcept;
// Server thread: one event (fixed size, no allocation). kind: tpl event kinds below.
// P8 S27: kTravelRevive = around user 2's ReviveLoadingReq 2362 (TS of both players, the host manager's +0x6A0).
inline constexpr uint16_t kTravelContext = 1, kTravelPrepared = 2, kTravelConsume = 3, kTravelRevive = 4;
void travel_probe_push(uint16_t kind, std::span<const uint64_t> v) noexcept;
// Server thread (take_frame): a packet queued for the LIVE session 2 (after the owner alias). `head` = a copy of
// the first bytes of its payload (taken before the packet is queued), `size` = the whole payload's size.
inline constexpr size_t kTravelHead = 24;
void travel_probe_on_out(uint16_t opcode, size_t size, std::span<const uint8_t> head, uint32_t owner_orig,
                         uint32_t owner_copy, tpl::AliasOut alias, bool pushed) noexcept;
// P8 S16 (TravelAck's host side effect, travel_probe_logic.h HostBurst). The network thread: user 2's
// PreLoadSequencerReq 2873 on its final disposition (handed = it went to the server; false = observer_milestone_reload
// dropped it). The sniffer's writer thread: one packet of the host's own client (2661/2981/2322 S>C, its own 2873).
void travel_probe_on_user2_2873(bool handed) noexcept;
void travel_probe_host_sniffed(const uint8_t* packet, size_t size, uint64_t t_us, bool server_to_client) noexcept;

}  // namespace cdmp::session2
