#pragma once
// Passive logger for the game's internal client<->server messages ("Troc").
// Hooks the two byte-level funnels with mid-hooks at their entry points and
// only COPIES packets out; nothing is modified, blocked or delayed beyond a
// short memcpy (the one exception, off unless set: sniffer_set_recv_rewrite,
// P8 ride_filter on the host). A writer thread turns the copies into:
//   cdmp_packets.log  one line per packet (direction, name, opcode, hex preview)
//                     plus per-opcode statistics every 10 s
//   cdmp_packets.bin  full packet bytes for offline analysis (format in .cpp)

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cdmp::net {

// `dir` is the mod's folder; `stop` is the mod-wide stop event. With
// record=false the hooks still feed co-op but nothing is written to disk.
bool sniffer_install(const std::wstring& dir, HANDLE stop, bool record = true);

// Experimental (combat tests): queue a client->server packet (u16 opcode |
// u8 slot | u16 len | payload) for the game's own in-process server. It is
// posted from inside the next real ClientNet::PostPacket call, on that game
// thread, with the channel/flags real packets use. False if not possible.
bool sniffer_inject_c2s(std::vector<uint8_t> packet);

// The t_us (QPC microseconds) of the first packet written to cdmp_packets_*.log,
// the origin of its "t" column; 0 until there is one (or record=0). Lets other
// read-only probes print times comparable with the capture (P8 S2).
uint64_t sniffer_first_packet_us() noexcept;

// A read-only observer of every whole packet the sniffer copied (both
// directions), called on the sniffer's WRITER thread (a mod thread: never a
// game thread, never the network thread), in capture order, with the record's
// t_us. It must not block. One at a time; nullptr removes it. P8 S1
// (session2/host_scene_diag.h): the host's scene ledger.
using PacketTap = void (*)(const uint8_t* packet, size_t size, uint64_t t_us, bool server_to_client) noexcept;
void sniffer_set_tap(PacketTap tap) noexcept;

// P8 session 9, D1 (session2/ride_filter.h): a callback that may change an S->C packet IN PLACE inside the
// OnRecvPacket mid hook, after it was captured (the capture keeps what the server sent) and before the game
// reads it. Called on the game's thread: it must not allocate, lock or log; any access to the packet must be
// SEH-guarded by the callback. One at a time (nullptr removes it). False: the sniffer's hooks are not in.
using RecvRewrite = void (*)(uint8_t* packet, uint16_t size) noexcept;
bool sniffer_set_recv_rewrite(RecvRewrite fn) noexcept;

// P8 mount ride (session2/horse_follow.h, p8-impl-monturas-cabalgata.md): an observer of every S->C packet in the
// OnRecvPacket mid hook (after the rewrite above, never for a packet the mod itself delivers) that may hand back
// ONE extra packet (header + payload) written into `extra` (cap bytes): the hook delivers it at once through
// ClientNet::OnRecvPacket on the same thread, right before the real one (experiment 4c's path and t_injecting
// guard). The capture keeps it, flagged as the mod's (.bin record pad8 bit 0, "[mod: ...]" in the .log); the taps
// (sniffer_set_tap) and the co-op observer never see it; it is never rewritten nor observed again. Returns the extra
// packet's size, 0 = none. Called on the game's thread: it must not allocate, lock or log; reads of `packet`
// SEH-guarded by the callback. The callback always runs; the delivery only while the sniffer's injection is on
// (opcode table checked, no earlier 4c fault), else counted as refused. An exception inside OnRecvPacket while it
// takes the extra packet is the GAME's (P8 review I1): the path switches off, one ERR line, and the exception is
// passed on (game_fault_continue), never swallowed. One at a time (nullptr removes it). False: hooks not in.
using RecvExtra = size_t (*)(const uint8_t* packet, uint16_t size, uint8_t* extra, size_t cap) noexcept;
bool sniffer_set_recv_extra(RecvExtra fn) noexcept;
struct RecvExtraStats {
    uint64_t delivered = 0;  // extra packets handed to OnRecvPacket
    uint64_t faulted = 0;    // an exception inside it (passed on; the path is off from then on)
    uint64_t refused = 0;    // made by the callback but not delivered (the sniffer's injection is off)
};
RecvExtraStats sniffer_recv_extra_stats() noexcept;

}  // namespace cdmp::net
