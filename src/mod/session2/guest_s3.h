#pragma once
// S3 on the P8 GUEST's client (docs/research/pendientes/p8-impl-s3.md;
// p8-raiz-invitado.md §5, §6 rows 4-5, §7.3). Three [session2] keys, all off
// by default; with the three at 0 nothing here is installed:
//
//   guest_s3_diag=1      READ ONLY, no engine call. A lock-free ring of
//     fixed-size records (QPC, thread, epoch, actor key, controller) per
//     event: a 2005 / 2723 / 2991 queued for the client (guest_deliver, with
//     each 2005 record's restore byte), the restore's result (from
//     guest_actor_diag's exit 0x864B1D), the first Update 0x85FBA0 of each
//     remote actor, 2723's consumer 0x48B120 and its vf188(5) result at
//     0x48B197 (AL, actor +0xC8), 2991's consumer 0x48AD40 (flag R9B and
//     [[CC+0x60]+0x10]&1), StartAction 0x863530's result, caller and the
//     layers right after it ("returned success" vs "action installed"), and,
//     when 0x232D5C0 is entered with snapshot+0x10 == 0, both layers
//     (+0/+8/+0x10/+0x18/+0x110/+0x3C), CC+0x70/+0x78/+0xA8 and the actor's
//     +0x5E/+0xC1/+0xC2/+0xCF/+0xC8. Formatted only on the 5 s tick
//     (guest_s3_tick, on the guest's diagnostics thread: guest_diag_worker.h),
//     never on a game thread nor on the network thread.
//   guest_crash_dump=1   installed once the game's code is there. An access
//     violation in the game's real code (.rsrc), first chance: the vectored
//     handler (last in the list) only pushes one fixed record to the ring
//     (printed by the 5 s tick). Unhandled exceptions (top-level filter,
//     chained; stack overflows passed straight on) and the known P8 crash
//     site exe+0x232D5FF (from the vectored handler: the game does not handle
//     it) write the last records to cdmp.log and a minidump next to cdmp.asi
//     (local only; the newest 2 kept). One capture at a time, never twice for
//     one address. Never handles the exception: the game's own path (and K1)
//     runs as before. Since session 5 (p8-hilo-red.md): our top-level filter
//     is put back in front every second if the game's crash reporter replaced
//     it (chained to theirs, logged once with its module+offset), and when the
//     dump thread is already gone (ExitProcess) the crashing thread writes the
//     dump itself. Since session 6 (pendientes/p8-salida-invitado.md §3):
//     the filter that replaces ours is the CRT's (exe+0x4896E00), which hands
//     an access violation to Windows' error reporting (the process stayed
//     alive 5 min 40 s). With NO crashpad_handler.exe next to the game (K2),
//     an UNHANDLED ACCESS VIOLATION ends the process right after the capture
//     (one log line, then TerminateProcess with the exception code; s3::uef_end).
//     Other codes, or with crashpad_handler.exe present: chained as before.
//   guest_update_guard=1 CONTENTION, not a fix: 0x232D5C0 returns at once for
//     an actor not controlled locally whose current action (snapshot +0x10,
//     the pointer 0x232D5FF reads) is null. Counts, logs the first keys and
//     whether each later got an action (a stuck one is named).
//
// Every hook site is checked against the build's bytes and must patch only
// .rsrc (never .xtls); the patch length SafetyHook chose is compared with the
// verified bytes before the hook is enabled. Otherwise: "GuestS3: ... REFUSED".

#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

// From guest_start (any thread). Starts its own install thread. No-op when all are false.
// actor_diag: guest_actor_diag is on (it alone feeds guest_s3_note_restore; a warning otherwise).
// update_site_taken: guest_remote_update_guard (S34, net/horse_diag.h) owns 0x85FBA0's entry; the probe's
// first-Update point is then not hooked (one owner per entry; "left to guest_remote_update_guard" in its ON line).
void guest_s3_start(bool diag, bool crash_dump, bool update_guard, bool actor_diag = false,
                    bool update_site_taken = false);
bool guest_s3_wanted();
// Every 5 s from the guest's diagnostics thread (guest_diag_worker.h; not a
// game thread, not the network thread): formats the new records. One caller.
void guest_s3_tick() noexcept;
// Atomics only, for the guest's describe line ("" when off).
std::string guest_s3_describe();

// guest_deliver (the mod's network thread): a host packet that is queued for
// the client. Only 2005, 2723 and 2991 are recorded.
void guest_s3_on_host_packet(uint16_t opcode, std::span<const uint8_t> payload, uint64_t epoch) noexcept;
// guest_actor_diag's restore exit (0x864B1D): the result of
// RestoreActionsFromLoadData for an actor not controlled locally. Only called
// with guest_actor_diag=1 (guest_s3_start warns otherwise).
void guest_s3_note_restore(uint32_t key, uint32_t control, int restore, uint32_t code, bool code_ok) noexcept;

}  // namespace cdmp::session2
