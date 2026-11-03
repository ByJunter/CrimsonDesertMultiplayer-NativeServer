#pragma once
// P8 session 25, hour and weather (docs/research/noche/clima-hora.md §5.1, §6). Build 1.0.0.2976.
//
// The guest enters with a fresh time (2144) but afterwards nothing gives it the host's clock again, and its own
// load re-anchors it (its 2762 keeps flag 1 while the server ran with 0: ~3.5 game minutes lost in S16). The
// host's server is the only authority of the world clock; these keys hand its clock to user 2 natively. No new
// hook in the game: take_frame (session 2's own Send) and the clock gate's watch thread.
//
// [session2] user2_clock_push = 0 (default) | probe | 1, HOST, needs create_session=1 and the clock gate
//   (clock_gate=1, installed):
//   T1  take_frame (the server's thread that writes it), right AFTER a LoadingCompleteAck 2762 is queued for the
//       live session 2: a native SetFieldTimeAck 2816 (clock_push_logic.h) for user 2's character with a
//       consistent copy (two equal reads) of the server's world clock exe+0x6A6D238: its base and its anchor, in
//       the server tick domain user 2's client already follows through 2188. The 2762 is left as it is: its flag 1
//       unpauses the guest's field (0x77AFE9 / 0x219D7F0); the 2816 behind it, in the same FIFO, puts the host's
//       base and anchor back (.xtls 0xA41D560 -> 0xA38B00(mgr, ft, 1)).
//   T2  the clock gate's watch thread (1 Hz): the same 2816 when the world clock changed while user 2 is in the
//       world (its 2762 written; never during its loads), debounced (ckp::PushWatch); also the retry of a T1
//       push the queue refused (it is under half its budget or nothing goes).
//   probe: the same decisions and lines, nothing queued ("WOULD queue").
//   Lines 'Session2 clock push [mode] T1|T2: ...' (at most 128), counters in session2's describe().
//
// [session2] clock_probe = 0 (default) | 1, HOST and GUEST, read only (guarded reads, no hook, no call): every 5 s
//   one 'ClockProbe [host|guest]' line with the client clock exe+0x6A6D218, the server clock exe+0x6A6D238 (each
//   with its t0 = base - anchor x scale, the number the two-PC check compares), the time manager
//   [[exe+0x6D69190]+0x28] +0x28..+0x57 (active / private calendar flags, the private clock) and the 2188 bias
//   exe+0x6D69438; on the guest also the 2816 / 2762 its client was handed since the last line.

#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

// session2::install (host): the raw ini value; needs create_session and the clock gate (warned otherwise).
// True = T1/T2 on (probe or 1).
bool clock_push_configure(const std::string& raw, bool create_session);
bool clock_push_active();

// take_frame, for a 2762 really queued for the LIVE session 2: its payload (u32 actor | u8 flag | u64 now) and
// user 2's character key (0 = unknown: the 2762's actor). `push` queues one more packet right behind it.
using ClockPushFn = bool (*)(std::span<const uint8_t> packet);
void clock_push_on_2762(std::span<const uint8_t> payload, uint32_t player_key, ClockPushFn push) noexcept;
// The clock gate's watch thread, once a second (T2).
void clock_push_tick(uint64_t now_ms) noexcept;
std::string clock_push_describe();  // "" when off

// mod.cpp, both roles: starts the probe's thread (nothing when off).
void clock_probe_start(bool on, bool guest);
// The guest's client NetworkThread (guest.cpp deliver_all): a host packet handed to OnRecvPacket (whole packet).
void clock_probe_on_delivered(std::span<const uint8_t> packet) noexcept;

}  // namespace cdmp::session2
