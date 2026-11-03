#pragma once
// P8 option U, step U2 case A (docs/research/s25/tiendas-u2.md): the pure logic of the guest's own shop scenes, HOST
// only. No game access, no allocation in what the hooks use (the tests run it). The hooks are their owners':
// user2_funnels (ExecuteTroc 0x2774B20 and ServerField::vf48 0x2B00CE0), shop_probe (0x2126A1F, 0x288AAEF, 0x29E9282)
// and user2_stages (the element filter at 0x288A970, the drain); the module that ties them is user2_shops.cpp.
//
// Build 1.0.0.2976. Two keys:
//   [session2] user2_stages_elements = schedule (default) | schedule+funcnpc   (U-f, in user2_stages)
//   [session2] user2_shop_route = 0 (default) | probe | 1                      (U2-a, U2-b, the drain wait)

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "mod/session2/funcnpc_trade_policy.h"
#include "mod/session2/stage_lookup_logic.h"
#include "mod/session2/user2_stages_logic.h"

namespace cdmp::session2::u2sh {

using Mode = u2s::Mode;

// ---------------------------------------------------------------- the keys

enum class Elements : uint8_t { Schedule, ScheduleFuncnpc };
struct ElementsKey {
    Elements elements = Elements::Schedule;
    bool bad = false;  // anything else: schedule (logged)
};
inline ElementsKey parse_elements(std::string_view v) {
    ElementsKey k;
    if (v.empty() || v == "schedule") return k;
    if (v == "schedule+funcnpc") {
        k.elements = Elements::ScheduleFuncnpc;
        return k;
    }
    k.bad = true;
    return k;
}
inline const char* elements_name(Elements e) {
    return e == Elements::ScheduleFuncnpc ? "schedule+funcnpc" : "schedule";
}

// ---------------------------------------------------------------- U-f: which sequencer elements pass

// The base name of a resource path (after the last '/' or '\').
inline std::string_view base_name(std::string_view path) {
    const size_t at = path.find_last_of("/\\");
    return at == std::string_view::npos ? path : path.substr(at + 1);
}
inline constexpr std::string_view kFuncnpcPrefix = "cd_seq_funcnpc_";
inline bool is_funcnpc(std::string_view base) { return base.size() > kFuncnpcPrefix.size() && base.starts_with(kFuncnpcPrefix); }

// What 0x288A970's hook does with one element of user 2's manager (user2_stages' elem_detour). U-1's own skip comes
// first (u2s::skip_element: mode 1, armed, in the host's field, not a schedule); a sequencer whose resource is a
// cd_seq_funcnpc_* then goes to the original only when the elements key allows it AND the shop route may prepare
// (user2_shop_route probe or 1, not aborted, its points in: review M1, the probe step A2 prepares and only logs the
// route) AND user 2 is far enough (review A1: no NEW shop scene at < 10 cells nor while the drain waits).
enum class ElemVerdict : uint8_t {
    Original,       // not skipped by U-1 at all (the native path)
    Skip,           // U-1's skip (*res = 0, the original not called)
    SkipFuncnpc,    // a funcnpc skipped: elements=schedule (the line says it would pass with schedule+funcnpc)
    SkipNoRoute,    // a funcnpc skipped: elements=schedule+funcnpc but user2_shop_route cannot prepare (0, aborted...)
    SkipNear,       // a funcnpc skipped: user 2 within 10 cells of the host, or U-1's drain waiting for a shop
    PassFuncnpc,    // a funcnpc let through (U2): 0x287C200 on user 2's manager, a 2661 to user 2
    SkipNotTrade,   // S46 T1 active: a funcnpc whose base name's dictionary hash is NOT on the trade list (or T1 latched)
    Count
};
inline constexpr uint32_t kNewShopMinCells = 10;
// S46 phase 3, T1 active (list_on: user2_shop_trade_list=1, not latched): the trade list decides by the exact hash of
// the base name (trade), INSTEAD of user2_stages_elements: a funcnpc on the list passes with the route able to prepare
// and >= 10 cells, any other funcnpc is skipped as today (SkipNotTrade), whatever the elements key says (T1 replaces
// schedule+funcnpc, which lets every funcnpc of the cell through: 12 in Hernand, p1b). Schedules and the other
// sequencers: unchanged (U-1's own skip). list_on = false: exactly the verdict before S46 phase 3.
inline ElemVerdict element_verdict(bool u1_skip, u2s::Element e, bool funcnpc, Elements elements, bool may_prepare,
                                   bool near_or_waiting, bool list_on = false, bool trade = false) {
    if (!u1_skip) return ElemVerdict::Original;
    if (e != u2s::Element::Sequencer || !funcnpc) return ElemVerdict::Skip;
    if (list_on) {
        if (!trade) return ElemVerdict::SkipNotTrade;
    } else if (elements != Elements::ScheduleFuncnpc) {
        return ElemVerdict::SkipFuncnpc;
    }
    if (!may_prepare) return ElemVerdict::SkipNoRoute;
    return near_or_waiting ? ElemVerdict::SkipNear : ElemVerdict::PassFuncnpc;
}
inline const char* elem_verdict_name(ElemVerdict v) {
    switch (v) {
        case ElemVerdict::Original: return "to the original (U-1 does not skip it)";
        case ElemVerdict::Skip: return "SKIPPED (U-f: *res = 0, the original not called)";
        case ElemVerdict::SkipFuncnpc: return "SKIPPED (a shop scene: would pass with user2_stages_elements=schedule+funcnpc)";
        case ElemVerdict::SkipNoRoute: return "SKIPPED (a shop scene: user2_shop_route cannot prepare it)";
        case ElemVerdict::SkipNear: return "SKIPPED (a shop scene: user 2 < 10 cells from the host, or the drain waits)";
        case ElemVerdict::PassFuncnpc: return "LET THROUGH (U2: a shop scene of user 2's own manager)";
        case ElemVerdict::SkipNotTrade:
            return "SKIPPED (S46 T1: a shop scene whose name's hash is NOT on the trade list smithy 0xA7132561 / "
                   "butcher 0x978345CA)";
        default: return "?";
    }
}

// S46 phase 3: the key user2_shop_trade_list = 0 | probe | 1 (u2s::parse_mode). Off and probe: element_verdict with
// list_on = false (probe adds the T1 verdict to the line and the summary, nothing else). On: list_on; latched by T1's
// abort (user 2's manager prepared a funcnpc whose playData hash is not on the list) U-f passes trade = false, so every
// funcnpc is skipped for the rest of the run (SkipNotTrade), whatever the elements key. t1_list_on: T1 on and not
// latched (the abort's own test).
inline bool t1_list_on(Mode m, bool latched) { return m == Mode::On && !latched; }
// T1's abort criterion (shop_probe's P point, user2_shops_prepared): with T1 active, user 2's manager prepared a
// cd_seq_funcnpc_* whose resource hash (playData +0x20) is not on the list (a hash 0 = not read: never the criterion).
inline bool t1_breach(bool list_on, bool user2_mgr, bool funcnpc, uint32_t hash, uint32_t result) {
    return list_on && user2_mgr && funcnpc && result == 0 && hash != 0 && !u2trade::is_trade_hash(hash);
}

// S46 phase 1, T1 as a verdict only (docs/research/s25/s46-tenderos.md): for a shop element U-f skips today
// (SkipFuncnpc: elements=schedule; SkipNoRoute: the route cannot prepare), would T1 (the exact trade list,
// funcnpc_trade_policy.h, and >= 10 cells from the host, no drain waiting) let it through? Measured in U-f's own hook,
// with nothing prepared (the element is skipped as before). Whether the shop route may prepare is said apart.
enum class T1 : uint8_t { NotTrade, Near, WouldPass, Count };
inline T1 t1_verdict(bool trade, bool near_or_waiting) {
    if (!trade) return T1::NotTrade;
    return near_or_waiting ? T1::Near : T1::WouldPass;
}
inline bool t1_measured(ElemVerdict v) { return v == ElemVerdict::SkipFuncnpc || v == ElemVerdict::SkipNoRoute; }
inline const char* t1_name(T1 v) {
    switch (v) {
        case T1::NotTrade: return "not on the trade list (T1 would skip it too)";
        case T1::Near: return "on the trade list, but < 10 cells from the host or the drain waits (T1 would skip it)";
        case T1::WouldPass: return "would pass (on the trade list, >= 10 cells)";
        default: return "?";
    }
}

// ---------------------------------------------------------------- U2-a: the scene Reqs' vf48 sites

// Capstone on the read-only PE (tiendas-u2.md §2): each handler's `call [rax+0x48]` on the field of
// 0x17FA6C0(TrocParams' actor), its return address, and where the u64 sequencer id is in the packet
// (TrocParams +0x18; +0x10 its u16 size): Reqs at +5 (u16 opcode | i8 slot | u16 length | payload, the id at payload
// +0, as observer_seq_copy's R1 reads it), the 2044 and 2054 timers at +3 (u16 opcode | u8 | u64 id | ...: 0x2895820's
// args; the 2054's 0x2879440(mgr, &res, packet+3, [packet+0xB]), its deferred delete of a sequencer).
struct Site {
    uint16_t op;
    uint32_t ret_rva;
    uint8_t id_at;  // in the packet
    const char* name;
};
inline constexpr uint8_t kReqId = 5, kTimerId = 3;
inline constexpr Site kSites[] = {
    {2436, 0x2A5C6CB, kReqId, "StageBranchStateReq (-> 0x2891FB0)"},
    {2591, 0x2C4A8FC, kReqId, "UpdateStageChartEventReq (0x2C4A8A0 -> 0x2936060)"},
    {2105, 0x2A5CA68, kReqId, "StageBranchStateDelayEndReq (-> 0x2892210)"},
    {2704, 0x2A5D0E1, kReqId, "UpdateStageChartDelayedEventReq (-> 0x28928D0)"},
    {2173, 0x2A5C283, kReqId, "UpdateSequencerGamePlayDataEventReq (-> 0x2891CE0)"},
    {2276, 0x2A5B71D, kReqId, "DeleteSequencerReq (-> 0x2879320)"},
    {2767, 0x2978E20, kReqId, "SequencerTriggerVolumeInOutReq (0x1F6C190 at 0x2978E6E)"},
    {2138, 0x1089F284, kReqId, "SequencerStageFailReq (.xtls 0x1089F230)"},
    {2888, 0x2A5E401, kReqId, "ChangeSequencerPivotReq (-> 0x28952F0)"},
    {2325, 0x2A5DDF1, kReqId, "StageQuitReq (-> 0x28934F0)"},
    {2406, 0x2A5D6B1, kReqId, "SubTimelinePauseReq (-> 0x2892FE0)"},
    {2968, 0x2A5DA51, kReqId, "SubTimelineResumeReq (-> 0x2893260)"},
    {3020, 0x2A5BF4D, kReqId, "SetNextBranchTypeFromAIReq (-> 0x2895DC0)"},
    {2044, 0x1082FBE7, kTimerId, "StageBranchStateOnceTimer (.xtls 0x1082FB80 -> 0x2895820)"},
    // The deferred delete (tiendas-u2.md 12.2): without it a delete of one of user 2's scenes reached the host's
    // manager and was lost (an orphan scene in user 2's manager and in the guest's M).
    {2054, 0x10835C57, kTimerId, "DeleteSequencerOnceTimer (.xtls 0x10835BF0 -> 0x2879440, the deferred delete)"},
};
inline constexpr uint16_t kOpDeleteTimer = 2054;
inline constexpr size_t kSiteCount = sizeof kSites / sizeof kSites[0];
inline const Site* site_of(uint16_t op) {
    for (const Site& s : kSites) {
        if (s.op == op) return &s;
    }
    return nullptr;
}
inline size_t site_index(uint16_t op) {
    for (size_t i = 0; i < kSiteCount; ++i) {
        if (kSites[i].op == op) return i;
    }
    return kSiteCount;
}
// The 3 bytes before each return address: call qword ptr [rax+0x48] (checked at install; a site whose bytes differ is
// left out).
inline constexpr uint8_t kCallVf48[] = {0xFF, 0x50, 0x48};

// The sequencer id of a scene Req / timer packet: packet = the bytes at TrocParams +0x18, size = its u16 at +0x10.
inline bool scene_id(uint16_t op, std::span<const uint8_t> packet, uint64_t& id) {
    const Site* s = site_of(op);
    if (!s || packet.size() < static_cast<size_t>(s->id_at) + 8) return false;
    uint64_t v = 0;
    for (size_t i = 0; i < 8; ++i) v |= static_cast<uint64_t>(packet[s->id_at + i]) << (8 * i);
    if ((v & 0x1FFFFFFF) == 0) return false;  // no sequencer id (0x212694D's test)
    id = v;
    return true;
}

// ---------------------------------------------------------------- the route decision (U2-a and U2-b)

enum class RouteOut : uint8_t {
    NotHostField,  // the field is not the host's (user 2 in its own field: natively its own manager)
    NotArmed,      // an id of user 2's manager with U-1 not armed or its goodbye asked (review M4): not routed
    NoUser2Mgr,    // user 2's manager not known / not readable
    Unreadable,    // a lookup's reads failed (a table being changed): never routed
    NotInUser2,    // the id is not a live sequencer of user 2's manager (the host's, or none): the original
    InBoth,        // live in BOTH managers: impossible (one global counter, 0x1F6B6E0): an abort criterion
    Would,         // probe (or aborted): it would be user 2's manager
    Routed,        // mode 1: user 2's manager
    Count
};
struct RouteIn {
    Mode mode = Mode::Off;
    bool aborted = false;
    bool host_field = false;
    bool armed = false;   // U-1 has user 2's manager armed (user2_stages_armed)
    bool logout = false;  // user 2's goodbye was asked (server_logout_requested)
    bool mgr2_ok = false;
    bool same_mgr = false;  // the original answer already IS user 2's manager (never: kept for safety)
    isl::Lookup in_mgr2;
    isl::Lookup in_orig;
};
// The step A run of ca1830a: with "not armed" checked first, every scene Req of the HOST (its own scenes) counted as
// "U-1 not armed" before user 2 even logged in. The membership comes first now: "not armed" only for an id that IS
// user 2's (review M4 unchanged: such an id is never routed without U-1 armed).
inline RouteOut decide_route(const RouteIn& in) {
    if (!in.host_field) return RouteOut::NotHostField;
    if (!in.mgr2_ok || in.same_mgr) return RouteOut::NoUser2Mgr;
    if (!in.in_mgr2.read_ok) return RouteOut::Unreadable;
    if (!in.in_mgr2.found) return RouteOut::NotInUser2;
    if (!in.armed || in.logout) return RouteOut::NotArmed;
    if (!in.in_orig.read_ok) return RouteOut::Unreadable;
    if (in.in_orig.found) return RouteOut::InBoth;
    return in.mode == Mode::On && !in.aborted ? RouteOut::Routed : RouteOut::Would;
}
inline const char* route_out_name(RouteOut o) {
    switch (o) {
        case RouteOut::NotHostField: return "not the host's field";
        case RouteOut::NotArmed: return "user 2's, but U-1 not armed (or user 2 leaving): not routed";
        case RouteOut::NoUser2Mgr: return "user 2's manager not known";
        case RouteOut::Unreadable: return "a lookup could not be read (not routed)";
        case RouteOut::NotInUser2: return "not in user 2's manager (the original)";
        case RouteOut::InBoth: return "IN BOTH managers (abort)";
        case RouteOut::Would: return "would be user 2's manager (probe)";
        case RouteOut::Routed: return "ROUTED to user 2's manager";
        default: return "?";
    }
}

// ---------------------------------------------------------------- the shop interaction (2916) and its follow-up

// The shop interaction key: 0x000F4321 in every native shop talk measured (S11 0x196, S13 0x19D smithy, S14 the
// guest's butcher). 2663 / 2916 / 2743: u32 requester @0, u32 requester @4, u32 NPC @8, u32 interaction @12.
// 3000 is DIFFERENT: u32 requester @0, u32 NPC @4, u32 interaction @8, u32 native error @12 ([E] build 2976 handler
// 0xBB4680 reads 5 x u32 then 3 x u8, 23 B; [V] p2b host capture offset 137772823: A0100001 B01002EC 000F4321 58C8345D
// FFFFFFFF 00 01 00). S46: ported by hand from the other AI's branch opcion-d-tenderos (S41 P2). Before it, a 3000 was
// read with the success layout (its interaction is the error): the Fail3000 criterion never saw a shop 3000.
inline constexpr uint32_t kShopInteraction = 0x000F4321;
inline constexpr uint16_t kOpPreemptionOk = 2663, kOpPreemptionFail = 3000, kOpDoStepOk = 2916, kOpResetAck = 2743,
                          kOpNak = 1013;
struct StepAck {
    bool ok = false;
    uint32_t requester = 0, npc = 0, interaction = 0;
    uint32_t native_error = 0;  // 3000 only: the raw value, not a classification
};
// The identity / error prefix only (the native handler validates the rest). Default: the success layout; a 3000 must
// pass its opcode. Any other opcode: not ok.
inline StepAck parse_step_ack(std::span<const uint8_t> p, uint16_t opcode = kOpDoStepOk) {
    StepAck r;
    if (p.size() < 16 || (opcode != kOpPreemptionOk && opcode != kOpPreemptionFail && opcode != kOpDoStepOk &&
                          opcode != kOpResetAck)) {
        return r;
    }
    const auto u32 = [&](size_t o) {
        return static_cast<uint32_t>(p[o]) | static_cast<uint32_t>(p[o + 1]) << 8 | static_cast<uint32_t>(p[o + 2]) << 16 |
               static_cast<uint32_t>(p[o + 3]) << 24;
    };
    r.ok = true;
    r.requester = u32(0);
    const bool failed = opcode == kOpPreemptionFail;
    r.npc = u32(failed ? 4 : 8);
    r.interaction = u32(failed ? 8 : 12);
    if (failed) r.native_error = u32(12);
    return r;
}
// 0x29E8AF0 writes 2743 as four u32 (actor, actor, NPC, interaction); 0x2B375A0 runs the native reset before it.
// Ends a wait for that interaction; it does NOT prove that every close of the shop UI sends it.
inline bool shop_reset_matches(const StepAck& ack, uint32_t opened_npc, uint32_t opened_requester) {
    return ack.ok && ack.interaction == kShopInteraction && opened_npc && opened_requester && ack.npc == opened_npc &&
           ack.requester == opened_requester;
}

// After a 2916 of the shop interaction to user 2, its client natively drives ITS scene right away (S13: 2436 6 ms
// after the 2916). The S14 failure was exactly the opposite: no 2436 / 2591 at all, the guest stuck. A Req of user 2
// routed (or would-be routed) to its manager within kFollowUs is the scene following.
inline constexpr uint64_t kFollowUs = 5'000'000;
// The drain wait (tiendas-u2.md §5): a distance drain of U-1 waits while user 2 is "in a shop": from that 2916 for at
// most kHoldUs. (Not ended by a 2436 to branch 0: in S13 the native smithy went 6 -> 0 one second after opening.)
inline constexpr uint64_t kHoldUs = 120'000'000;
enum class Judge : uint8_t { Pending, Followed, Stuck };
inline Judge judge_open(uint64_t open_us, uint64_t followed_us, uint64_t now_us) {
    if (!open_us) return Judge::Pending;
    if (followed_us >= open_us && followed_us - open_us <= kFollowUs) return Judge::Followed;
    return now_us >= open_us && now_us - open_us > kFollowUs ? Judge::Stuck : Judge::Pending;
}
inline bool holding(uint64_t open_us, uint64_t now_us, uint64_t hold_us = kHoldUs) {
    return open_us && now_us >= open_us && now_us - open_us < hold_us;
}
// Review A1: the drain waits only while the players are MORE than 5 cells apart (at <= 5 the host's own stage windows
// reach user 2's: drained at once), and at most ONE wait per arm, from the first shop 2916 that opened it (a later
// 2916 never renews it).
inline constexpr uint32_t kHoldMaxNearCells = 5;
enum class Hold : uint8_t { None, Wait, TooNear, Used, Expired, Count };
struct HoldIn {
    uint64_t open_us = 0;        // the last shop 2916 of user 2's own scene (0 = none in this arm)
    uint64_t hold_start_us = 0;  // the 2916 this arm's wait started from (0 = no wait yet)
    uint64_t now_us = 0;
    uint32_t dist = UINT32_MAX;  // cells (UINT32_MAX: unknown)
};
inline Hold hold_decision(const HoldIn& in) {
    if (in.hold_start_us) {  // this arm already waited once: only that window, never renewed
        if (!holding(in.hold_start_us, in.now_us)) return Hold::Used;
    } else if (!holding(in.open_us, in.now_us)) {
        return in.open_us ? Hold::Expired : Hold::None;
    }
    if (in.dist == UINT32_MAX || in.dist <= kHoldMaxNearCells) return Hold::TooNear;
    return Hold::Wait;
}
inline const char* hold_name(Hold h) {
    switch (h) {
        case Hold::None: return "no shop open";
        case Hold::Wait: return "WAIT";
        case Hold::TooNear: return "too near (<= 5 cells) or the distance unknown: drained at once";
        case Hold::Used: return "this arm's one wait is used";
        case Hold::Expired: return "the shop's 120 s are over";
        default: return "?";
    }
}

// Review B4: the Nak 1013 the scene handlers send (ServerNet::SendNak(net, 0x3F5, &buf, 0x11, ...), 0x2A5C6F6..):
// buf = u16 0x3F5 | u8 0xFF | i32 error @3 | u16 the Req's opcode [troc+0xC] @7 | u64 0 @9 [E decompiled; no Nak of a
// scene Req has been captured yet: the line logs its bytes]. The Req opcode at +7 of the payload, or 0.
inline uint16_t nak_req_opcode(std::span<const uint8_t> p) {
    if (p.size() < 9 || p[0] != 0xF5 || p[1] != 0x03) return 0;
    return static_cast<uint16_t>(p[7] | p[8] << 8);
}
// S46 (P3): the i32 error @3 of such a Nak (0 when it is not one), named by shop_trade_probe's eErrNo table.
inline uint32_t nak_error(std::span<const uint8_t> p) {
    if (p.size() < 9 || p[0] != 0xF5 || p[1] != 0x03) return 0;
    return static_cast<uint32_t>(p[3]) | static_cast<uint32_t>(p[4]) << 8 | static_cast<uint32_t>(p[5]) << 16 |
           static_cast<uint32_t>(p[6]) << 24;
}

// ---------------------------------------------------------------- the abort criteria (mode 1 -> probe for the run)

enum class Abort : uint8_t {
    None,
    InBoth,        // a sequencer id live in both managers
    Stuck,         // a 2916 of the shop interaction to user 2 with no routed Req of its own scene within 5 s
    Fail3000,      // a 3000 of the shop interaction to user 2 within 2 s of a routed server check
    Nak,           // a Nak 1013 to user 2 within 1 s of a routed scene Req
    HostRefused,   // the host's own 2671 refused within 1 s of a check routed for the host
    NotFuncnpc,    // user 2's manager prepared a sequencer KNOWN not to be a cd_seq_funcnpc_* (U-f broken)
    NotTradeList,  // S46 phase 3: with T1 active, a cd_seq_funcnpc_* whose playData hash is off the trade list
    Count
};
inline const char* abort_name(Abort a) {
    switch (a) {
        case Abort::None: return "none";
        case Abort::InBoth: return "a sequencer id live in BOTH managers";
        case Abort::Stuck: return "a shop 2916 to user 2 not followed by its own scene's Req within 5 s (S14's stuck)";
        case Abort::Fail3000: return "a 3000 to user 2 on a routed server check";
        case Abort::Nak: return "a Nak 1013 to user 2 right after a routed scene Req";
        case Abort::HostRefused: return "the host's own 2671 refused on a routed check";
        case Abort::NotFuncnpc: return "user 2's manager prepared a sequencer that is not a shop scene (U-f)";
        case Abort::NotTradeList:
            return "S46 T1: user 2's manager prepared a shop scene whose resource hash is NOT on the trade list (T1 "
                   "latched off: no more shop scenes)";
        default: return "?";
    }
}
inline bool within(uint64_t earlier_us, uint64_t now_us, uint64_t window_us) {
    return earlier_us && now_us >= earlier_us && now_us - earlier_us <= window_us;
}

}  // namespace cdmp::session2::u2sh
