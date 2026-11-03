#pragma once
// P8 session 25, grabs by the other player: finishers and takedowns of a distracted enemy
// (docs/research/s25/finisher-sigilo.md §4, section "Implementado"). Build 1.0.0.2976. catch_seq_logic.h has the
// verified addresses and the decision.
//
// [session2] catch_seq = 0 (default) | probe | 1, HOST only (on the guest's PC it stays off, warned):
//   An inline hook at the ENTRY of ClientCatch_ApplyResult 0x935DD0 (the client side of CatchActionResultAck 2156),
//   installed by its own thread once the local player has been in the world for 3 s; every head byte, the only
//   caller and the comparison it mirrors are checked first (refused and off otherwise).
//   probe  every call: one line 'CatchSeq [probe] #n: ...' (at most 300 in a run) with the grabber and victim keys,
//          who controls each, c8, the packet's sequence byte, the grabber's byte, match or not, the return address
//          and the capture time. Also the host's own grabs (one PC: there the bytes must match).
//   1      the same, and when the grabber is NOT controlled here, the victim IS and the bytes differ, the grabber's
//          byte goes into the argument the native comparison reads (only that one): the native catch then applies
//          (the victim is grabbed, its AI paused, its paired action sent with parent = the grabber).
//   Every 60 s: 'CatchSeq summary (catch_seq=...)'. Nothing unreadable is ever changed.
//
// Threads: the hook runs on the game's thread that applies the Acks; it reads with SEH-guarded reads, pushes one
// event into a lock-free ring and counts. The module's thread installs the hook, drains the ring and logs.

#include <string>

namespace cdmp::session2 {

// mod.cpp's worker, both roles (guest = the P8 guest's PC: off there). Nothing for 0.
void catch_seq_start(const std::string& raw, bool guest);

}  // namespace cdmp::session2
