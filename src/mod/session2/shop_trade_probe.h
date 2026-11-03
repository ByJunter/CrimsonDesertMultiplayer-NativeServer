#pragma once
// S46 phases 1 and 2 (docs/research/s25/s46-tenderos.md; research s46-datos/nativo.md §8):
//   [session2] shop_trade_probe = 0 (default: no hook) | 1                   HOST, READ ONLY (phase 1)
//   [session2] user2_shop_trade_route = 0 (default) | probe | 1              HOST (U2-c; 1 = phase 2, RAX := mgr2)
//   [session2] shop_interaction_own_delete = 0 (default) | probe | 1         HOST and GUEST (R0, phase 2)
// With every key at 0 / probe: nothing is written to the game, no game function is called, no register is changed, no
// packet is changed. The two '1's of phase 2:
//   R0 (P2, 0x2369862, the actor's exclusive lock held, a pa::WindowsRWLock): for the shop key 0x000F4321 only, when
//      the first entry with the erased key is another stage's, this stage HAS its own entry, and a key-matching entry
//      belongs to the OTHER manager of this side (host: mgrH / mgr2, server stages only; guest: X / M, client stages
//      only), the two 0x30-byte entries are swapped so that the native erase takes this stage's own entry
//      (stp::plan_r0). Otherwise native (no register is changed; no sentinel since review F1). A write fault latches R0
//      native. On the guest, '1' is held as probe (F4).
//   U2-c (0x21F28A1): for user 2's own trade check (its store component and its Troc), in the host's field, with U-1
//      armed ([mgr2+0x6A0] == 2, not leaving), a shop entry of the NPC alive in mgr2 and none alive in mgrH (F3):
//      RAX := mgr2.
//
// Mid hooks (build 1.0.0.2976, .rsrc; before anything is written: the base and the PE TimeDateStamp, the exact .pdata
// entry of the function, a 40-byte signature at the point that is unique in the exe (checked offline, s46-datos/
// nativo/verify_f1.py and .out), whole instructions, no branch nor absolute pointer into the patch but its first
// byte; then p8_install_mid: inside .rsrc, the bytes, and safetyhook relocating exactly the verified bytes. A point
// that fails is NOT installed and a WRN line says why; the others go on):
//   P1   0x21F2C45 cmp [rsp+0x28],rbx (5 B) in 0x21F2780 (.pdata 0x21F2780-0x21F2F2E), the shop check of 2789 / 2366
//        / 5 other store ops (and the host client's UI pre-checks): the stage linked to the NPC (rbx) against the
//        manager's "current" stage ([rsp+0x28] = [mgr+0xA0] or [mgr+0x90]); the manager [rsp+0x38] (mgrH, mgr2 or
//        another; server or client), who asks (the store component r13 -> [+8] -> its user and character), the NPC
//        (rsi), the caller (the return at [rbp+0x5F]) and the verdict. For user 2: would it pass against mgr2's
//        current stage (what U2-c would give)?
//   P1b  0x21F2917 test r12d,0x1FFFFFFF (7 B), same function: the manager has no current stage (only those calls).
//   P2   0x2369862 mov edx,[rax] ; mov rcx,r12 (5 B) in 0x2369360 (.pdata 0x2369360-0x23698E7), right before the
//        exit's call 0x21245B0 (.xtls 0xE404A10, the native erase) - not ON the call (a mid hook there would leave a
//        return address into its trampoline, which has no unwind data, while the erase runs); the same registers:
//        rax = the InteractionInfo ([rax] = the key), r12 = the NPC's interaction component, [rbp+0x858] = the
//        context ([ctx] = the stage, [stage+0x30] its id, [[stage+0x58]] the processor: server 0x5B1FE10 / client
//        0x558C080), [rbp+0x850] = the function, [[rbp-0x60]+8] = the NPC actor. The actor lock is held.
//        Logs the NPC's entries BEFORE the erase, i_first / i_own and R0's verdict, and [would orphan].
//   P2a  0x2369695 movzx r8d,r14w ; mov rcx,r12 (7 B), right before the entry's call 0x2124370 (.xtls 0xE402CC0, the
//        append): r14w = the index, rsi = the stage id; the list before the append.
//   P5   0x2B36FB3 mov rdi,rax ; mov rax,[rbp-0x31] (7 B) in 0x2B36EF0 (.pdata 0x2B36EF0-0x2B37366), the 2671's
//        InteractionInfo (rax): its key, +0x12, +0x13 and +0xBC (the native "busy" rule of .xtls 0xE401150).
//   P6   0x2B3708C cmp byte [r15],2 ; je +0x29 (6 B; safetyhook relocates the je rel8), same function: vf170's
//        result [r15] and error [r15+4] of each 2671.
//   P3   (no hook) the eErrNo variables (eerrno_names.h) read at install: the 3000s' and Naks' errors named.
//   U2-c 0x21F28A1 mov rbx,rax ; lea rdx,[rax+8] (7 B), the return of 0x21F2780's field.vf48: for user 2's request,
//        '[would route to mgr2]' (probe) / '[ROUTED to mgr2]' (1) with the manager vf48 gave and mgr2.
//        Installed with user2_shop_trade_route=probe or 1 (alone too).
// The ledger (shop_trade_probe=1): the server's entries of the shop NPCs (those with the shop key 0x000F4321) seen by
// P2 / P2a; every second the live vector is read again (SEH) and each entry's stage looked up in mgrH and mgr2
// (read-only replica of 0x1F6C190): a stage in neither for two checks in a row is '[orphan]' (WRN).
// Hooks: SEH reads, atomics and lock-free rings only; the lines and the ledger are the module's thread's.

#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

// session2::install (host, create_session=1). probe: shop_trade_probe=1; route: user2_shop_trade_route's value;
// own_delete: shop_interaction_own_delete's (R0). No-op when all are off.
void shop_trade_probe_start(bool probe, const std::string& route, const std::string& own_delete);
// guest_start (guest=1): R0 on the guest's client (shop_interaction_own_delete); only P2 is installed. No-op when 0.
void shop_trade_probe_start_guest(const std::string& own_delete);
bool shop_trade_probe_active() noexcept;
// server.cpp's take_frame: a packet queued for the live session 2 (its 3000s and Nak 1013s are named). Any thread.
void shop_trade_probe_on_out(uint16_t opcode, std::span<const uint8_t> payload, bool to_user2, bool pushed) noexcept;

}  // namespace cdmp::session2
