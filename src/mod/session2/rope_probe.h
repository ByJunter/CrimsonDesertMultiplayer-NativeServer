#pragma once
// P8 session 27 measure-first probes for the rope's two open problems (docs/research/s25/gancho-cuerda.md section 12,
// "Implementado (sondas)"). Build 1.0.0.2976. Log only: nothing of the game is written, no game function is called.
//
// [session2] rope_probe = 0 (default) | 1, on BOTH PCs:
//   A. Tree bend: log-only hooks at the verified ENTRIES of pa::ClientFrameEventTreePulling::vf1 0x6D46D0 (who runs
//      it, the character's movement-controller timer before / after, the body pair) and of the per-frame tree-pull
//      apply 0x8A30C0 (counted per character key in the hook; a line when a timer run starts and ends). The
//      summary says, for every player character this client knows, whether 0x8A30C0 ever ran for it.
//   B. Grappling device: no hook. From the sniffer's capture (writer thread, session2.cpp's tap fan-out): the device
//      gimmick events this client SENDS (3082 / 2758 with the device hashes, 2609 of a known device) and every device
//      2005 it RECEIVES (key, owner, docked character). Needs [sniffer] enabled=1.
// Lines 'RopeProbe ...' (at most 300 per run) and a 'RopeProbe summary' every 60 s.
//
// Threads: the hooks run on the game's threads (0x8A30C0 under the actor's lock: guarded reads and relaxed atomics
// only); the sniffer's tap only parses and pushes fixed-size events; the module's thread installs the hooks once the
// world is up (every byte checked, REFUSED and nothing written otherwise), drains the ring, looks actors up and logs.

#include <cstddef>
#include <cstdint>

namespace cdmp::session2 {

// mod.cpp's worker, both roles. Nothing with on=false.
void rope_probe_start(bool on, bool guest);

// The sniffer's writer thread (session2.cpp's fan-out): one whole packet (header + payload). Never blocks.
void rope_probe_sniffed(const uint8_t* packet, size_t size, uint64_t t_us, bool server_to_client) noexcept;

// S29: the 0x8A30C0 entry hook is in (it also feeds tree_bend_note_pull, tree_bend.h). Any thread.
bool rope_probe_pull_hooked() noexcept;

}  // namespace cdmp::session2
