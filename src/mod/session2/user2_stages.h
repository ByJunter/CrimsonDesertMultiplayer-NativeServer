#pragma once
// P8 option U (docs/research/s25/mundo-escenarios-propios.md, u0-resultados-s28.md "U-1"), HOST only:
// [session2] user2_stages = 0 (default) | probe | 1, and user2_timer_route = 0 (default) | probe | 1 (its own key, the
// review of 2a058cf A1: user 2's 2043/2045/2046 timers reach ITS OWN manager in a field the host owns, always; fixes
// user 2's 2043 chain living on the host's manager, S29/S30; U-1's mode 1 needs it at 1).
//   probe (U-0 / U-0b and U-1's decisions): READ ONLY. Nothing is written to the game, no game function is called, no
//         packet is changed; every U-1 decision is logged as what mode 1 WOULD do.
//   1     (U-1): when user 2 stands >= 10 cells (64 m) from the host for 10 s in the field the host owns, its own
//         ServerSequencerStageManager is armed and evaluates its own cells (schedule stages only); at <= 7 cells for
//         5 s, at its loads and before its 2001 it is drained with the game's own unload.
//
// Mid hooks at function entries (build 1.0.0.2976, .rsrc; bytes compared before anything is written, p8_install_mid;
// the thunks are 5-byte `jmp .xtls` stubs, the same kind user2_funnels hooks at 0x20DEE60):
//   C 0x2817E80  ServerNPCScheduleStage vf13, the stage close (rcx stage, rdx field, r8b reason, [rsp] return)
//   D 0x2808D80  the schedule spawn manager's DeleteStage thunk (rcx sm, r8 &key, r9b reason, [rsp] return)
//   L 0x287DA50  the owner's load step thunk (rcx = the manager SendMoveFieldAck puts in state 1)
//   U 0x287D4C0  the manager's unload-all (rcx mgr; the reason-19 closes come from inside it, 0x287D8C0)
//   N 0x28078E0  the schedule stage creation (rcx sm, r9 -> the schedule file: data id [[r9]+0x1C])
//   T 0x287DBB0  the 2043 tick (rcx = the manager the timer reached through ServerField::vf48)
//   S 0x2822D00  the schedule update thunk (rcx sm; r8b != 0: direct from a stage update, else the 2045 timer)
//   F 0x27FBD60  the faction spawn update thunk (rcx fm; r8b != 0: direct, else the 2046 timer)
//   P 0x287EF60  the manager's preparation (rcx mgr, r8 the character)
//   O 0x2068340  ServerActor::Logout's thunk (rcx actor, r8b reason, [rsp] the caller): every removal counted by (reason,
//                who, caller), a line for reason 30 - the SERVER's only (S29: the client's 2516 handler shares it)
// U-1's points:
//   U-a G 0x288B87A  the owner gate cmp [r15+0x180],eax: ShopProbe's mid hook when shop_probe=1 (it calls
//                    user2_stages_gate first), else this module's own (same verified bytes and context). With user 2's
//                    manager armed, in the field the host owns: eax := [r15+0x180] (a register only).
//   U-a2 G2 0x2911435 the candidate collector 0x2911300's manager guard: the context keeps the HOST's
//                    ServerQuestManager (the rule of 2026-10-13: the guest follows the host's progress), so its owner
//                    check and FindUser resolve the host natively; for the call from user 2's armed evaluation
//                    (return 0x28819D4, the caller's rsi = its manager) rbx := its manager before 0x387EF0, so the
//                    guard is its own manager's. r8 / FindUser untouched. This module's own mid hook.
//   U-c2             in its first armed evaluation (at U-a): 0x2896C60 (its id list from the field's stage data, with
//                    the host's quest manager) and 0x287FA30 (its category tables), as the native preparation.
//   U-f2             a schedule element whose stage the other player's manager has alive (same position) is not
//                    created, in user 2's manager and in the host's while armed.
//   U-f E 0x288A970  the stage element (an inline hook at its entry, 5 bytes): for user 2's manager in mode 1 only
//                    schedule elements reach the original; the others return *res = 0 without a call.
//   U-b              user2_funnels' ServerField::vf48 detour asks user2_stages_route under user 2's subject: its
//                    2043/2045/2046 get its own manager (from its first arm until it leaves the host's field).
//   U-c / U-d        calls of the game's own functions, guarded (game_fault.h): the arm 0x287F450(mgr) (state 2,
//                    the cell tables cleared) and, when its 2043 chain is dead, ServerNet::EnqueueTimer 0x2774430 with
//                    0x287EF60's own periodic 2043; the drain 0x287DA50(mgr, 0, 0) (state 1 + an immediate 2043 ->
//                    0x287D4C0) and, at its loads and its 2001, 0x287D4C0(mgr, 0, 0) at once (it attaches to its
//                    character's field itself). On the server's handler threads only: user 2's outermost ExecuteTroc
//                    exit (requests) and its 2283/2362/2437/2001 Troc entry. The OnRecvPacket hook only counts its
//                    2001 (armed or not): no game call from ServerRunThread.
//   U-h              the sniffer (host client's S>C): schedule packets of user 2's stages counted, nothing changed.
// S31 (u0-resultados-s28.md §8.17), two keys of their own, both needing user2_stages=1 to act:
//   user2_npc_control = 0 | probe | 1 (U-e): the NPCs born for user 2's armed stages get USER 2 as controller. At the
//                    server's controller write 0x2792A7B (docked_gimmicks' mid hook, one install for both keys; it asks
//                    user2_stages_npc_control first): under user 2's subject in its 2043/2045/2046, U-1 armed, a
//                    CreateServerActorDesc_NPCSchedule descriptor, no controller / desc+0x40 / parent yet, eax the field
//                    owner's (the host), type 3 (6 not yet): eax := [pj2+0x90] (a register only). probe: '[would control]', and
//                    the reason-42 logouts of those NPCs (point O) counted. Abort criteria (stop assigning; drains go
//                    on): u2s::ctl_abort.
//   user2_drain_at_host_prepare = 0 | probe | 1: at P (the host's preparation 0x287EF60's entry) with user 2's manager
//                    armed and the host within the off threshold (7 cells) of user 2 in the same field: user 2's manager
//                    unloaded at once (0x287DA50 + 0x287D4C0) before the host's evaluation creates its stages; probe:
//                    '[would drain at host prepare]' with the context (caller, thread, subject, states).
//   Review of ae14ab6: (1) arm / drain / cleanup under one module try-lock; the drain at P only with no Troc of user 2
//   in flight (user2_funnels_trocs_in_flight), and the Trocs that start meanwhile wait at their entry (bounded);
//   (2) with user2_drain_at_host_prepare=1 a DISTANCE drain waits for the host's pending preparation (at most 15 s);
//   (3) the NPCs given to user 2 still alive 5 s after a drain, at its load / goodbye Troc and at its character's
//   Logout are logged out (reason 25, inside SAM::FindActor's scope, key and controller checked); (4) a key the table
//   cannot track is not assigned, removed keys' slots are reused; (5) removed keys ignored by the checks; (6) only in
//   a Troc whose vf48 answered with user 2's own manager; (7) 'undriven' is a WRN line, not an abort; (8) P also
//   drains the stages an async drain left.
// Module thread (1 s): both players, the distance (same field only), the arm/drain decisions with hysteresis
// (>= 10 cells for 10 s / <= 7 cells for 5 s), the abort criteria (user2_stages_logic.h check_abort), lines
// 'User2Stages [...]' and a summary every 60 s. Hooks and game threads: SEH reads, atomics, a lock-free ring.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

// session2::install (host, create_session=1). Starts the install / drain thread. No-op when off.
// elements: [session2] user2_stages_elements = schedule (default) | schedule+funcnpc (U2, user2_shops.h: with the shop
// route live, U-f lets user 2's armed manager prepare its cd_seq_funcnpc_* scenes too).
// npc_control / drain_prep: [session2] user2_npc_control / user2_drain_at_host_prepare (0 | probe | 1).
void user2_stages_start(const std::string& mode, const std::string& timer_route, const std::string& elements,
                        const std::string& npc_control, const std::string& drain_prep);
// S37 phase D ([session2] user2_fast_rearm = 0 | probe | 1, docs/research/s25/s37-fase-d.md, fast_rearm_logic.h):
// before user2_stages_start. 1 (with user2_stages=1): a <= 7 -> >= 10 cell jump that is the HOST's own departure (its
// load step L in the last 30 s and after the start of the near streak, consecutive samples, same field and character,
// user 2 still, nothing pending), confirmed over 3 s (far, user 2 still, no load of user 2), arms without the 10 s
// hysteresis (the distance recomputed at the arm), and that arm queues a one-shot 2043 (+0xB 0, delay 1 = 100 ms)
// beside the chain repair.
// probe: '[would FAST REARM]' and the time it would have saved. 0: today's behaviour exactly.
void user2_stages_set_fast_rearm(const std::string& mode);
// S46 phase 3, T1 ([session2] user2_shop_trade_list = 0 | probe | 1, HOST; docs/research/s25/s46-tenderos.md §9):
// right after user2_stages_start. 1: U-f lets through only the cd_seq_funcnpc_* of the trade list by the exact hash of
// the base name (funcnpc_trade_policy.h), INSTEAD of user2_stages_elements (schedule+funcnpc is narrowed to the list);
// the births inside those scenes' prepare skip U-e / families / roads. probe: the verdict in the lines only. 0: as
// before. breach (user2_shops' P point): user 2's manager prepared a funcnpc off the list -> T1 latches off.
void user2_stages_set_trade_list(const std::string& mode);
bool user2_stages_trade_list_active() noexcept;
void user2_stages_trade_list_breach(uint64_t seq_id, uint32_t hash) noexcept;
// Review M2: this thread is inside the original 0x288A970 of an element T1 let through (its mark is set).
bool user2_stages_in_trade_prepare() noexcept;
// Review B5: user2_funnels, when the thread's outermost ExecuteTroc returns (a mark an SEH unwind left behind goes).
void user2_stages_clear_trade_mark() noexcept;

// S33: configuración independiente; todo apagado por defecto. Viajeros sólo admite sonda; familia (S36) 0/probe/1.
// stages_on: user2_stages es probe o 1; sin él las tres quedan apagadas y no instalan nada.
void user2_stages_set_npc_probes(const std::string& activation, const std::string& family,
                                const std::string& road, bool stages_on);
bool user2_stages_npc_probes_on() noexcept;
void user2_stages_npc_action_probe_ready(bool ready) noexcept;
uint64_t user2_stages_npc_birth_token(uint32_t key) noexcept;
// S35: s34_sender = la lectura S34 (param4+0x90 -> +0x60; param4 no es la sesión: sólo npc_action_evidence 0/probe);
// ctl_entry / ctl_exit = actor+0x90 antes / después de la llamada (controlador esperado; 0 = ilegible); h1b = el
// Pending TLS de ESTA llamada llegó a H1b 0x2A79060.
void user2_stages_npc_action_result(uint32_t key, uint32_t s34_sender, uint32_t ctl_entry, uint32_t ctl_exit, bool h1b,
                                    int32_t internal_result, uint8_t branch_flag, int restore_before,
                                    int restore_after, uint64_t birth_token) noexcept;
// S35 fase A ([session2], docs/research/s25/s35-fase-a.md): npc_action_evidence / npc_containment_guard /
// npc_degradation = "0" | "probe" | "1"; los plazos en ms (experimentales). Antes de instalar nada.
struct NpcS35Config {
    std::string evidence = "0", guard = "0", degradation = "0";
    uint32_t no_progress_ms = 5000, req_grace_ms = 8000, guest_silence_ms = 2000, postpone_max_ms = 60000;
};
void user2_stages_set_npc_s35(const NpcS35Config& c);
// S36 fase B ([session2] npc_family_types, docs/research/s25/s36-fase-b.md): la cohorte de miembros de una unidad
// ("3,6" por defecto; sólo 3, 6 y 7). npc_family_control (0 | probe | 1) llega por user2_stages_set_npc_probes; su modo
// efectivo (1 sólo con todos sus requisitos) se fija en user2_stages_start. Antes de instalar nada.
// S42: 0 (anterior), probe (clasificación sombra); 1 se degrada explícitamente a probe hasta cerrar el ciclo nativo.
void user2_stages_set_family_vehicles(const std::string& mode);
void user2_stages_set_npc_family(const std::string& types);
// S38 fase C (revisión §3.2) / S40 carros (docs/research/s25/s40-carros.md): [session2] user2_road_carts = "0" |
// "probe" | "1" (raíces tipo 5 en el modo 1: fuera / medidas enteras sin escribir / asignadas). Antes de instalar.
void user2_stages_set_road_carts(const std::string& value);
// S38 fase C (revisión §2, procedencia positiva): user2_funnels' ExecuteTroc detour, at the entry of a Troc of user 2
// whose opcode is 2550 (after user2_stages_troc_begin): its packet (TrocParams +0x18) and size (+0x10, header
// included). The road classes of its entries are kept for the births of this Troc. Guarded reads only.
void user2_stages_road_request(uintptr_t packet, uint16_t size) noexcept;
void user2_stages_npc_family_observed(uint32_t key, uint32_t parent, uint32_t controller, uint8_t type) noexcept;
// Sólo la entrada nativa de creación de un nodo verificada debe consultar este veto: stage = su rcx, return_rva =
// el retorno del CALL (decide si un false cancela sólo ese nodo). Stages de otro dueño nunca se vetan. node_ptr = su
// rdx; caller_rsp = el rsp del llamador en el CALL (dirección del retorno + 8): en el grupo nativo de 0x2819E00 se
// lee de ahí el grupo entero (lecturas protegidas) para vetar sólo si todos sus nodos están agotados.
bool user2_stages_npc_node_blocked(uintptr_t stage, uint32_t data, uint32_t node, uint32_t return_rva,
                                   uintptr_t node_ptr, uintptr_t caller_rsp) noexcept;
void user2_stages_npc_late_family(uint32_t key, uint32_t parent, uint32_t controller) noexcept;
void user2_stages_npc_node_probe_ready(bool ready) noexcept;

// U-e (user2_npc_control), from docked_gimmicks' mid hook at the server's controller write 0x2792A7B: rax = the
// controller about to be written, desc = rsi (the descriptor), field = r13, actor = r14. May set eax to user 2's user
// key (mode 1); returns at once unless the running ExecuteTroc's subject is user 2's main character. Server threads.
void user2_stages_npc_control(uintptr_t& rax, uintptr_t desc, uintptr_t field, uintptr_t actor) noexcept;

// U-a, from the owner gate's mid hook (ShopProbe's or this module's): rdi = the manager, r13 = its character, r15 =
// the field, rax = the character's user key (the cmp's eax). May set rax to [field+0x180]. The server's threads.
// actor = r12, 0x288B7A0's 4th argument: with user 2's manager armed, its first pass builds the manager's id list and
// category tables (0x2896C60 + 0x287FA30, as the native preparation does after its arm: U-c2).
void user2_stages_gate(uintptr_t mgr, uintptr_t chr, uintptr_t field, uintptr_t actor, uintptr_t& rax) noexcept;

// U-b (user2_timer_route), from user2_funnels' vf48 detour at a timer handler's own call inside a Troc of user 2
// (subject = its main character): 0 = keep the original; else the manager to return (user 2's own, vtable and back
// pointer checked).
uintptr_t user2_stages_route(uint16_t opcode, uintptr_t subject, uintptr_t field) noexcept;

// From user2_funnels' ExecuteTroc detour, user 2's subject: before the original (its 2283/2362/2437/2001: the
// synchronous drain) and after it at the outermost level (the pending arm / drain).
void user2_stages_troc_enter(uintptr_t subject, uint16_t opcode) noexcept;
// S38 fase C (user2_road_control=1, docs/research/s25/s38-fase-c.md): user2_funnels' ExecuteTroc detour, at the exit of
// an outermost Troc whose subject is NOT user 2 (the host's Reqs and timers, on the server's threads): a pending
// retirement of user 2's travellers goes on there too (bounded, at most every 100 ms), so it finishes even when user 2
// sends no more Trocs (its disconnection). One relaxed atomic load when nothing is pending.
void user2_stages_any_troc_exit() noexcept;
// Review M3: any OTHER vf48 inside user 2's 2043/2045/2046 Troc (not the handler's own call): counted by its return.
// Inside its ARMED 2043 tick (mode 1, route 1, the host's field, its manager in state 2): its own manager (else 0 =
// the original), as the owner's own tick gets natively.
uintptr_t user2_stages_timer_vf48(uint16_t opcode, uint32_t ret_rva, uintptr_t subject, uintptr_t field) noexcept;
// user2_funnels' vf40 detour (ServerField::vf40 0x2B00C70) inside a Troc of user 2: counts the vf40 calls of its armed
// 2043 tick (the context builder's, return 0x2880D74, apart) and returns false: by the rule of 2026-10-13 every one
// keeps the owner's (the host's) ServerQuestManager.
bool user2_stages_tick_vf40(uint16_t opcode, uintptr_t subject, uintptr_t field, uint32_t ret_rva) noexcept;
void user2_stages_troc_exit(uintptr_t subject, uint16_t opcode) noexcept;
// user2_funnels' ExecuteTroc detour, around EVERY Troc of user 2 (after its in-flight count goes up / before it goes
// down): the per-Troc 'answered vf48 with its own manager' flag (U-e, review 6) and, at the entry, a bounded wait while
// the host's preparation drains user 2's manager on another thread (review 1).
void user2_stages_troc_begin() noexcept;
// child_hold's StartActionReq::vf2 detour (the server's thread): U-e wants every Req's key (mode 1), and after the
// original su resultado externo: cero NO demuestra aplicación; la sonda S33 mide la rama OnServer aparte.
bool user2_stages_wants_start_action() noexcept;
void user2_stages_start_action_done(uint32_t key, int32_t result) noexcept;
void user2_stages_troc_end() noexcept;

// server.cpp's OnRecvPacket hook (ServerRunThread, before the packet is handled): user 2's 2001 counted; a WRN line
// when it comes with its manager armed. Nothing called. packet / size (0 = unknown): the whole request (5-byte header):
// user 2's AI Reqs mark the NPCs U-e gave it as driven.
void user2_stages_server_packet(uint16_t opcode, bool from_user2, uintptr_t packet, uint16_t size) noexcept;

// server.cpp's take_frame: a packet queued for the live session 2 (the 2322 / 2335 checks; the 2005s: U-e's
// controller check). Any thread.
void user2_stages_on_out(uint16_t opcode, std::span<const uint8_t> payload, bool to_user2) noexcept;

// The sniffer's writer thread (the host client's capture): U-h counts; U-e: the host client's 2005s and AI Reqs of
// the NPCs given to user 2 (it should get them as remote and send nothing for them).
void user2_stages_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept;

// U2 (user2_shops.h), any thread: user 2's ServerSequencerStageManager now (its character from the module's last
// sample; vtable and back pointer re-checked here; 0 = none).
uintptr_t user2_stages_user2_manager() noexcept;
// U2: the field's owner [field+0x180] is the host's user key (ServerField vtable checked).
bool user2_stages_host_field(uintptr_t field) noexcept;
// U2: U-1 has user 2's manager armed now (mode 1, not aborted).
bool user2_stages_armed() noexcept;
// U2: the host's ServerSequencerStageManager now (re-checked; 0 = none).
uintptr_t user2_stages_host_manager() noexcept;
// U2: user 2's character stands in a field the host owns now.
bool user2_stages_user2_in_host_field() noexcept;

}  // namespace cdmp::session2
