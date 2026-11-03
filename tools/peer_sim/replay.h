#pragma once
// cdmp_peer --mode replay (option D, docs/OPCION-D-EMULADOR.md §5): stands in
// for the guest's client. The host's mod connects as usual; once connected,
// cdmp_peer plays Req to the host's game server through the Troc tunnel
// (session 2) and records what the server answers.
//
//   --script FILE     a login script (docs/research/data/*.troc, troc_script.h),
//                     waiting for each Ack it expects
//   --capture FILE    or: the client->server packets of a capture (.bin), at their
//                     original pace, from --from to --to seconds (since the
//                     capture's first packet), only --only / except --skip opcodes
//   --var NAME=VALUE  script variable (ACCOUNT, SAVE_SLOT, CHILD_HASH...); it wins over
//                     a value the script learns
//   --record FILE     write both directions to a .bin capture (CDPK v2)
//   --start-after MS  wait after connecting before the first Req (default 3000)
//   --linger MS       keep recording the server's answers after the last Req (10000)
//   --raw-tunnel      do not tell the host that compressed tunnel batches are
//                     welcome (by default they are; the host compresses only
//                     with [session2] tunnel_compress=1, p8-tunel-rendimiento.md)
//   --guest-save / --guest-slot / --guest-id / --base-gen / --save-at / --save-before-bye / --save-results:
//                     the guest's own save on one PC (P8 S25 phase B, save_sim.h)
//   --rejoin N        P8 re-entry (docs/research/s25/reentrada.md, session2/rejoin_logic.h): after the script, play it
//                     N more times on the SAME ClientSession and tunnel, as a guest that goes back to the title and
//                     presses Continue: announce caps 0x08, ask with REJOIN (control state 13) once a second until the
//                     host's EPOCH (state 14) says it began the next epoch for that conversation, wait for a "ready"
//                     after it, play again. The own save (--guest-save) is uploaded only once, like the guest's mod.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "save_sim.h"

struct ReplayOptions {
    uint16_t port = 37015;
    bool public_bind = false;
    bool quiet = false;
    int seconds = 0;
    std::wstring script;
    std::wstring capture;
    double from_s = 0.0;
    double to_s = 1e12;
    std::vector<uint16_t> only, skip;
    std::vector<std::pair<std::string, uint64_t>> vars;
    std::wstring record;
    int start_after_ms = 3000;
    int linger_ms = 10000;  // keep recording after the last Req
    bool announce_compress = true;  // the tunnel's caps: compressed batches welcome
    SaveSimOptions save;            // P8 S25 phase B (save_sim.h)
    int rejoin = 0;                 // P8 re-entry: rounds after the first, on the same session
    bool rejoin_probe = false;      // P8 re-entry: the REJOIN says "probe" (the host starts nothing)
};

int run_replay(const ReplayOptions& opt);
// --mode tunnel-measure (p8-tunel-rendimiento.md): offline. The server->client
// packets of opt.capture between opt.from_s and opt.to_s (seconds since its
// first packet) go through a TunnelSender as the host's tunnel would send them,
// raw and compressed; prints bytes, ratio, compression time and what that
// takes at 1 MB/s (Steam's rate in P8 session 6B).
int run_tunnel_measure(const ReplayOptions& opt);
