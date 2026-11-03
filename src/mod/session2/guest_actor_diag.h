#pragma once
// P8, the GUEST's client (docs/research/pendientes/p8-actores-anfitrion.md):
// the actors of the host's field that fail on the guest. Guest side only, two
// [session2] keys, both off by default:
//
//   guest_actor_diag=1   read only (§4.4), every point in .rsrc:
//     1. FinishGroup's error branch 0x4FD491 (rdi = actor, ebp = error): key
//        [rdi+0x60], control [rdi+0x90], vtable, +0x5E, the error code.
//     2. RestoreActionsFromLoadData 0x8646D0, after its prologue at 0x8646FC
//        (rcx = CC) and at its single exit 0x864B1D (r13 = CC, [rsi] = result),
//        only for actors NOT controlled locally: key, restore byte
//        [[CC+0x40]+0x30], per layer +0x3C / +0x110 / its default chart
//        [CC+0x70/+0x78], and the result (with restore byte != 0, the result of
//        RestartLayerAction 0x863880). The same two points as the host's
//        companion probe (cmp::kRestore*): never both in one process. Since
//        p8-raiz-invitado.md §3.4 the layers' chart/current/playback are read
//        again at the exit (after 0x863880 returned), not only before.
//     3. every 5 s (the diagnostics thread, guest_diag_worker.h; until session 5
//        the network loop's report): the client's field-change state
//        [P+0x15C] (P = [[exe+0x6D69190]+0x60], 0x7797F0) and its wait list
//        [[[P+0xB8]+8]+0x78] (u32 keys, count +0x80), each key with its actor's
//        +0x5E/+0xC2/+0xCF, found with our own replay of
//        ClientActorContainer::Find (0x464290; no game call). All the keys up
//        to 1024 (§4.3), with total / inspected / unreadable counts and the
//        first key state 0x12 waits for (gad::scan_wait_list).
//     3b. the state's transitions when they happen: a checked mid hook on its
//        read at 0x7798D0 queues (old, new, QPC) for each change of [P+0x15C];
//        the install thread writes them every 5 s ("GuestDiag loading
//        transition: ... EXIT FROM 0x11"), with the capture's t column.
//   guest_layer_guard=1  the co-op's action-layer guard (remote_puppet.h:
//     0x1F63F70 for any layer with no playback, 0x2329700 skip) for every actor
//     whose +0x90 is not the local user key (§4.1(i), D-52 on the host). Logs
//     the key of each actor it holds (the first 64), then counts.
//
// The local user key is [ClientActorManager.controlled actor + 0x90]
// ([[[exe+0x6D69190]+0x30]+0x50]+0x90), read every 5 s; until it is known
// nothing is "remote" (no guard, no restore lines). Every read is guarded; no
// game function is called; every hook site is checked against its bytes and
// against the PE section table (.rsrc only, never .xtls) before it goes in.

#include <cstdint>
#include <string>

namespace cdmp::session2 {

// From guest_start (worker thread). Starts its own install thread (it waits
// for the game's code, up to 2 minutes). No-op when all are false.
// remote_update_guard ([session2] guest_remote_update_guard, S34): once the
// local user key is known, net::horse_diag_install_remote (CharacterControl
// Update 0x85FBA0 / PostUpdate 0x860D30 skipped for an actor another user
// controls while a layer has a chart and no action); its first 32 keys are
// logged from this thread, its counters go into guest_actor_diag_describe.
void guest_actor_diag_start(bool diag, bool layer_guard, bool remote_update_guard = false);
// Every 5 s from the guest's diagnostics thread (guest_diag_worker.h; never
// the network thread, never the game's): the restore lines and, with the
// diagnostic, the loading state and its wait list (SEH-guarded reads, no
// VirtualQuery). One caller thread at a time. Never throws.
void guest_actor_diag_tick() noexcept;
bool guest_actor_diag_wanted();
// The guest's user key ([controlled actor +0x90], refreshed every second; 0 = not known yet). Any thread.
uint32_t guest_actor_diag_local_user();
// Atomics only: for the guest's describe line.
std::string guest_actor_diag_describe();

}  // namespace cdmp::session2
