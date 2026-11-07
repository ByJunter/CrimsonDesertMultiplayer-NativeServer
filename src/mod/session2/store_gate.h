#pragma once
// [session2] store_gate = 0 (default) | probe | 1, BOTH roles, build 3549 only.
//
// The store check 0x23C6600(storeComp, int* err, NPC, u16* store) (2976: 0x21F2780, s46-datos/nativo.md section 1.3) runs
// in the client's buy/sell UI pre-checks (0xB25704 ... 0xB27BEC) and in the server's store handlers (0x2DE2778,
// 0x2EA341F ...). Besides the store, distance and wanted level, it demands that the shopkeeper's scene be the CURRENT
// scene of the manager the field gives (field.vf48 at 0x23C66F8): the field owner's (the host's) on the server, the
// client's own on a client. A guest never has that: on its client its own manager has no current scene, and at the
// host's server the host's manager has another one (or none) -> eErrNoInvalidStageInfo.
//   0x23C676B  test r12d,0x1FFFFFFF ; jne       "the manager has a current scene" (r12 is reused after it)
//   0x23C6A37  cmp [rbp-0x31],rbx   ; je        "it is the shopkeeper's scene" ([rbp-0x31] = current, rbx = NPC's)
// With 1, both pass for the requests of the other player only: on the host, when the requester ([[[r13+8]+0xA0]+0x60],
// r13 = the store component) is not the host's own user; on the guest, when it is the guest itself while it plays on a
// host. Money, stock and inventory are already the requester's own (storeComp is the requester's). probe: counted
// and logged, nothing changed.

#include <string>

namespace cdmp::session2 {

void store_gate_start(const std::string& mode, bool guest);

}  // namespace cdmp::session2
