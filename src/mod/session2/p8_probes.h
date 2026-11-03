#pragma once
// P8 root causes S1 and S2 (docs/research/pendientes/p8-raiz-invitado.md §3,
// §4, §7.1, §7.2; the implementation and the hook table:
// docs/research/pendientes/p8-impl-s1s2.md). READ-ONLY probes, two [session2]
// keys, both off by default; with both at 0 nothing here runs.
//
//   load_diag=1 (S2)
//     guest:  0x779E48  the loading state's first predicate (RDI = P, RAX = M):
//                       state, P+0x162/0x163, M, u8 M+0x231, M+0x174, M+0x2D4,
//                       f32 M+0x2D0, lists M+0x1B0/+0x1B8 and M+0x1C0/+0x1C8,
//                       position P+0x150; one event per change
//             0x779E77  AL = 0xA97D60(M, pos): the first dependency that
//                       certainly makes it false, read from those structures
//                       (p8_probe_logic.h attribute_a97d60); per change
//             0xB87890  PlayAllPreLoadSequencerAck 2322 handler entry: actor
//                       key [[R8+0x18]+5], the guest's connection epoch
//             0xB87A67  [M+0x231] = 2: RAX = M, the byte before, and whether M
//                       is the one 0x779E48 saw
//     host:   0x2BC8470 MoveFieldCompleteCore's owner check: traveller user key
//                       (EAX), field owner (R8D), field R13, actor R14, branch
//             0x287F1CC the 2322 emission path (0x287F1D4 calls 0x2999D80):
//                       target actor R14, finder RSP+0x50
//             0xE919C0  ExtraReward UI events (vtable exe+0x569C6B8): R8 vs
//                       [RCX+0x160] (the close branch) / [RCX+0x168], R9B
//   s1_diag=1 (S1, host and guest, the same NPC compared by key; meant for
//             SHORT sessions: 0x24314C0 runs on every action lookup)
//             0x24314C0 ActionGraph_Find entry, an INLINE hook (no context
//                       save): every call counted per thread shard, then
//                       only for EDX = 0x837487D4 / 0x35DD392F: graph, count
//                       +0x20, table +0x18, caller, the result replayed from
//                       the table (no call); then the original
//             0x86392A  RestartLayerAction's loop, per hash: CC, actor key,
//                       layer, hash, path (layer chart / fallback / missing)
//                       and the resulting triple (replaces 0x863914: a short
//                       je at 0x863908 lands on 0x863918, inside its patch)
//             0xA79770  BodyAnimation setup entry: context, CC, actor, args
//             0xA79BAC / 0xA79F4C  CC+0x70 / CC+0x78 installs (RBX slot,
//                       new graph, RSI context, CC = [RSI], actor key)
//             0x9ED431  sequencer binding: actor resolved? ([RBP-0x18],
//                       [RBP-0x20]), comp+0x2D8 / +0x2E0
//             0x9ED47F  sequencer instance [RBP-0x58] (the je rel32 that
//                       follows is verified whole, never patched)
//
// Every point: its patched bytes inside .rsrc (the PE's section table in
// memory), its bytes and context compared with build 1.0.0.2976 first, then
// a mid hook created disabled whose relocated bytes must be exactly the
// verified instructions (else refused and logged). Hooks only copy registers
// and SEH-guarded reads into a lock-free bounded ring (drops counted,
// per-point rate and per-key limits); a thread of its own formats them every
// 5 s with QPC times (the sniffer's clock) and the capture's t column.

#include <safetyhook.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "mod/session2/guest_actor_diag_logic.h"

namespace cdmp::session2 {

struct P8ProbeOptions {
    bool load_diag = false;  // [session2] load_diag
    bool s1_diag = false;    // [session2] s1_diag
    bool host = false;       // the host's PC (guest=0); the guest's points otherwise
};

// From the worker thread. Starts its own thread (it waits up to 2 minutes for
// the game's code). No-op when both keys are off.
void p8_probes_start(const P8ProbeOptions& opt);
bool p8_probes_wanted();
// P8 S1 option A (guest_seq_route.h): the probe thread installed (or refused) every point it wanted, and
// whether one of them is hooked at va (false until settled).
bool p8_probes_settled();
bool p8_probes_point_hooked(uintptr_t rva);
std::string p8_probes_describe();  // atomics only

// The probes' clock: QPC in microseconds, the sniffer's t_us (any thread, no
// allocation), and its text form for a log line: "qpc S.US s (~hh:mm:ss.mmm)
// capture t=..." (the cdmp_packets_*.log "t" column; drain threads only).
uint64_t p8_now_us() noexcept;
std::string p8_when(uint64_t qpc_us);

// The checked mid hook every P8 point uses (also guest_actor_diag's state
// transitions): [rva, rva + max(patch, expected)) inside .rsrc, the bytes at
// rva equal to `expected`, a disabled hook whose relocated bytes are exactly
// expected[0..patch), then enabled. On any failure nothing is written to the
// game, an error line names the reason, and false is returned.
// The exe's section table at `base` (read with SEH-guarded reads), for
// p8_install_mid's ".rsrc only" check. False: unreadable.
bool p8_exe_sections(uintptr_t base, std::vector<gad::Section>& out);

bool p8_install_mid(uintptr_t base, std::span<const gad::Section> secs, uintptr_t rva, uint32_t patch,
                    std::span<const uint8_t> expected, safetyhook::MidHookFn fn, SafetyHookMid& out,
                    const char* tag, const char* what);

}  // namespace cdmp::session2
