#pragma once
// P8: remote actors' interpolation delay per actor (docs/research/pendientes/p8-movimiento-remoto.md
// §3 F1; implemented in docs/research/pendientes/p8-impl-interp-remota.md). Host AND guest.
//
// [session2] remote_interp = 0 (default, nothing installed) | probe | 1 (remote_interp_logic.h).
//
// Four mid hooks (.rsrc, each exactly one verified 7-byte `movsxd reg,[rip->exe+0x6CB9548]`, the
// global interpolation delay):
//   0x11B200E  setInterpolatePositionData   4C 63 15 33 75 B0 05  r10   rdi = mc+0x3D0, rbp = sample
//   0x11B2730  setInterpolateRotateData     4C 63 1D 11 6E B0 05  r11   rdi = mc+0x488
//   0x11B249A  updateInterpolatePosition    48 63 05 A7 70 B0 05  rax   rdi = mc+0x3D0, rcx = now
//   0x11B2B62  updateInterpolateRotate      48 63 05 DF 69 B0 05  rax   rdi = mc+0x488, rcx = now
// The hook reads the buffer's owner (mc = rdi - off; [mc+0x60] = CC with [CC+0xC8] == mc;
// actor [mc+0x68]; key u32 [actor+0x60]), keeps per-mc state in a fixed lock-free table of the
// mod (never the game's heap; no allocation, no lock, SEH-guarded reads only), and for an 0xB
// actor whose buffer the mode-2 update reads, with 1, puts the actor's own delay in the
// destination register and resumes after the instruction (ctx.rip). Everything else: the game's
// own read runs unchanged. With 1 also the emptying threshold 1000 -> 2500 ms:
//   0x11B2073 / 0x11B2775  49 81 F8 E8 03 00 00 (cmp r8,0x3E8) -> 49 81 F8 C4 09 00 00
// written with safetyhook::trap_threads after both were verified (the first restored if the
// second cannot be written), and only once the four hooks are in.
//
// When: as ui_pause, only once the local player has been in the world for 3 s, after the code
// matches build 1.0.0.2976 byte for byte (else an error line and nothing installed).
// Its thread: a summary every 60 s (calls, threads per site, resets per class, gaps, delays).
//
// P8 session 25, [session2] remote_interp_combat = 0 (default) | probe | 1, GUEST, needs remote_interp (the combat
// stream, docs/research/noche/combate-retardo.md §6.2; remote_interp_logic.h "the combat class"): the host's
// enemies in combat (dense arrivals, or engaged with this client's character) get D = max(last 3 arrival gaps) +
// 80 ms within [120, 400], falling at 1 ms per ms. No new hook: the set-position site keeps the arrival gaps, the
// update-position site steps the class, the mod's network thread feeds the engagement table
// (remote_interp_on_host_packet). 'RemoteInterp summary combat' line every minute.

#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

// From the worker thread, once, on either PC. Starts its own thread; no-op for 0 (or an unknown
// value: warned). `raw` is the ini value. owned_cap_ms = [session2] remote_interp_owned_cap_ms (P8
// session 8 pets, default 0 = as before; remote_interp_logic.h "actors with an owner"): an 0xB actor
// with an owner ([actor+0xA0] != 0) gets at most this delay, and its gaps are not recorded while it
// rides a character (attach_follow.h's registry, enabled here).
// pair = [session2] remote_interp_pair (P8 mount ride, default 0; remote_interp_logic.h "one clock for a rider and
// its horse"): a ridden horse's delay targets its rider's while its samples are dense enough. Needs [sniffer]
// enabled=1 (the pairs come from the client's OnRecvPacket: horse_follow_client_attach, from this thread).
// combat_raw = [session2] remote_interp_combat (P8 session 25, default "0"; the guest only, warned and off on the
// host).
void remote_interp_start(const std::string& raw, bool guest, uint32_t owned_cap_ms = 0, bool pair = false,
                         const std::string& combat_raw = "0");
// P8 session 25, GUEST: a host packet about to be queued for this client (guest_deliver, the mod's network thread):
// its own character (2756) and the 2922 / 2991 engaging an enemy with it. Lock-free, no allocation, no log.
void remote_interp_on_host_packet(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
bool remote_interp_active();          // the four hooks are in
std::string remote_interp_describe();  // atomics only ("" when off)
// P8 mount ride: a rider's (0xA) latest Move Ack names `horse` as its Path parent (0 = on foot / another parent).
// The client's NetworkThread (inside OnRecvPacket): lock-free, no allocation, no log.
void remote_interp_note_ride(uint32_t rider, uint32_t horse) noexcept;

}  // namespace cdmp::session2
