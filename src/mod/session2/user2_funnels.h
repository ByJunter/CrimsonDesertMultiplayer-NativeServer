#pragma once
// P8: user 2's rank permit on the host's client and the per-user owner
// funnels on the host's server (docs/research/pendientes/p8-sistemas-juego.md
// §11.1 and §11.7; the implementation, the verified bytes and the test plan:
// docs/research/pendientes/p8-impl-rango-embudos.md). HOST only.
//
// [session2] rank_permit / quest_per_user / kill_loot_per_user = 0 (default,
// nothing installed) | probe (count only) | 1 (user2_funnels_logic.h).
//
// A  rank_permit (the host's CLIENT, its AI threads). The two
//    `call [rax+0x188]` (FF 90 88 01 00 00) at 0x221E281 (0x221E110, before
//    GrantRankPermit 0x2220850) and 0x221F29F (0x221EF50, before the permit's
//    removal) become `call rel32` to a stub near them (`jmp [rip]` to
//    rank_vf188_thunk) + `nop`. The thunk calls the original vf188 (the only
//    game call) and, for dl = 5 and X = user 2's main character (its key and
//    its controller +0x90 = user 2's keys), answers 1 with rank_permit=1 when
//    X's target component [[X+0x68]+0x90] exists. Probe: counts only.
//    Written once with safetyhook::trap_threads (the page non-executable while
//    the 12 bytes change); both sites verified before either is written, the
//    first restored if the second cannot be written.
//
// B/C, the subject (the host's SERVER threads): an inline hook at ExecuteTroc
//    0x2774B20(int* res, Troc*, TrocParams* params) sets a thread_local
//    "subject = [params]" for the Req's duration when that actor is user 2's
//    main character (ServerNormalInGameActor exe+0x5B1DE20 with user 2's keys),
//    restoring the previous one on return (nesting).
// B  quest_per_user: inline hook at ServerField::vf40 0x2B00C70(field). With a
//    subject and [field+0x180] = the host's user (the shared field):
//    [[[subject+0x68]+0x138]+0x18] (vtables checked) instead of the owner's
//    ServerQuestManager; anything unreadable: the original.
// C  kill_loot_per_user: inline hooks at 0x2AFFF50(field, out) and at the
//    `jmp .xtls` stub 0x20DEE60(field, out) (the stub in .rsrc; .xtls is never
//    touched), each for ONE return address (0x29432E3, DeadActor 0x62; 0x2B83D29,
//    the loot recipient). Both fill a lock handle `out` (+8 object, +0x10 locked).
//    0x2AFFF50: the original is called with a field of the mod's whose +0x180
//    is user 2's user key (it reads nothing else of the field: 0x2AFFF78), so
//    the game itself looks user 2 up and locks its character. 0x20DEE60: the
//    original's FindUser 0x2A82590([[exe+0x6D696B8]], out, user 2's key), the
//    same lookup and handle vf40 uses, instead of the .xtls body.
// F3 user2_fast_travel (fast_travel_logic.h): vf48's detour, for the one return
//    address 0x2A5F7FC (the 2283 handler 0x2A5F580) under user 2's subject in the
//    shared field: [[[subject+0x68]+0xA8]+0x18] (ServerSequencerStageManager,
//    [mgr+0x18] == subject) instead of the owner's, so 0x2896120 / 0x2BC4950
//    write the destination into user 2's TransformSync+0x550; when that chain
//    cannot be read, a thread_local block whose +0x18 is the subject (0x2896120
//    reads only that). The second vf48 of the same Troc (0x2BC4C53, inside
//    0x2BC4950: a reference on [mgr+8] and [mgr+0x6A0] = 0) gets the same real
//    manager. Probe: logs only (and F2 drops every 2283 with it).
// Counters (probe and 1): per funnel, per Req opcode and per caller; vf48
//    0x2B00CE0 counted under a subject whenever B or C is on; FindUser
//    0x2A82590 counted only when B or C is `probe`. No lock, no allocation,
//    no log line on a game thread: relaxed atomics, a lock-free ring.
//
// When: only once the local player has been in the world for 3 s (as
// ui_pause), after the game's code matches build 1.0.0.2976 byte for byte.
// Its thread updates the keys every 100 ms, writes the ring's events every
// second and a summary every 60 s.

#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

struct User2FunnelOptions {
    std::string rank_permit = "0";
    std::string quest_per_user = "0";
    std::string kill_loot_per_user = "0";
    // F3 (P8 session 15, docs/research/pendientes/p8-impl-viaje-rapido.md; fast_travel_logic.h): inside user 2's
    // ReloadStageStartReq 2283, the vf48 call returning to 0x2A5F7FC answers user 2's own sequencer manager.
    std::string user2_fast_travel = "0";
    // F4 (P8 S28, fast_travel_logic.h; docs/research/s25/u0-resultados-s28.md): inside user 2's ReviveLoadingReq
    // 2362, the reload setter 0x2BC4950's vf48 (return 0x2BC4C53) answers user 2's own sequencer manager, so the
    // host's +0x6A0 is never reset by user 2's revive. 0 | probe | 1.
    std::string user2_revive_manager = "0";
    bool create_session = false;  // the host's session 2 is built (option D's host)
    // quest_policy (docs/research/s25/politica-misiones.md, user2_funnels_logic.h): the rule of 2026-10-13, the guest
    // follows the host's progress. 0 | probe | 1, independent of quest_per_user. S34 (2026-10-14): the guest's actions
    // count for the host's quests as a companion's (the native event path 0x290DC0B, the 2112, challenges: the host's
    // quest manager; 2372 / 2477 / 2354: its own; 2369 dropped; DeadActor native), plus a read-only probe.
    std::string quest_policy = "0";
};

// From the worker thread on the HOST, once. Starts its own thread; no-op when
// the four keys are 0 (or unknown: warned).
void user2_funnels_start(const User2FunnelOptions& opt);
bool user2_funnels_active();
std::string user2_funnels_describe();  // atomics only ("" when off)
// F3 is installed: vf48 hooked, the subject hooked and the 2283 handler's call site (0x2A5F7F2..0x2A5F830) and
// 0x2896120's head verified; user2_fast_travel = 1. Any thread (an atomic). fast_travel.cpp's F2 drops user 2's
// 2283 whenever this is false.
bool user2_funnels_fast_travel_live() noexcept;
// ExecuteTroc's subject test (key, controller +0x90, class; the keys the thread publishes) on this actor now, with
// guarded reads: F2 hands user 2's 2283 to the server only if F3 will see it as user 2's. Any thread.
bool user2_funnels_subject_ready(uintptr_t actor) noexcept;
// P8 option U, U-0 (user2_stages.h): the Req of user 2 this thread is inside, as the subject hook decided it (user 2's
// main character and the Troc's opcode; 0 / 0 outside one or for anyone else). hooked: the subject hook is in.
void user2_funnels_subject_now(uintptr_t& actor, uint16_t& opcode, bool& hooked) noexcept;
// P8 option U, U-1: the subject and ServerField::vf48 hooks are both in (user2_stages_route is asked). Any thread.
bool user2_funnels_vf48_live() noexcept;
// P8 option U, review 1 of ae14ab6: the ExecuteTrocs of user 2 (subject = its main character) running now on every
// server thread (seq_cst). Any thread.
int32_t user2_funnels_trocs_in_flight() noexcept;
// The subject and ServerField::vf40 hooks are both in (quest_per_user or quest_policy not 0). Any thread.
bool user2_funnels_vf40_live() noexcept;
// quest_policy at the tunnel's ingress (session2.cpp, before the server; the network thread; never throws).
// judge: on every offer, no counting; drop = with 1, user 2's 2372 that could reach anything but its own quest manager.
// record: once, on the Req's final disposition (handed to the server or not): the counts and the first lines.
struct QuestPolicyVerdict {
    bool relevant = false;  // a 2372 or a 2112 of user 2 with quest_policy not 0
    bool drop = false;
    uint16_t opcode = 0;
    uint8_t route = 0;      // u2f::RouteFail (2372)
    uint8_t field = 0;      // 0 unknown, 1 the host's, 2 its own, 3 another
    uint32_t first_u32 = 0;
    uint8_t head[16] = {};
    uint16_t size = 0;
};
QuestPolicyVerdict user2_funnels_policy_judge(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
void user2_funnels_policy_record(const QuestPolicyVerdict& v, bool handed) noexcept;
// quest_policy's probe (S34): one packet of the host's own client (the sniffer's writer thread): its quest Acks 3009 /
// 3088 / 2738 / 2346 are counted. Never throws.
void user2_funnels_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept;

}  // namespace cdmp::session2
