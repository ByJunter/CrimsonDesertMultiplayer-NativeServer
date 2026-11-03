#pragma once
// S45 (docs/research/s25/s45-escenas-ajenas.md), GUEST: the scenes of ANOTHER player on this client's own camera,
// input and interface. guest_seq_foreign_present_probe = 0 | 1, READ-ONLY probes (nothing is written in the game).
// The pure logic (no game access; tests: tests/optd_seq_present_tests.cpp).
//
// A stage is FOREIGN as in S44 (guest_seq_foreign_logic.h): its owner [stage+0x98] is a character key (0xA...) that is
// neither this client's own (its 2756) nor M's live owner; nothing is foreign while this client's own key is unknown.
//
// The probes (nativo.md §4; every RVA is build 1.0.0.2976's):
//   S-A   in P1 (0x602940's entry, guest_seq_foreign): the stage's processor [stage+0x58], its vtable compared with the
//         three native StageChartProcessor vtables, [proc+8] / [proc+0x10] against the stage's manager / the stage, and
//         the stage's timeline id stage+0x298; every stage started is noted in a table (sequencer, timeline id, whose).
//   S-A2  guest_x_veto's handlers, G = X: a stage function of a stage that has not passed P1 yet.
//   S-B   the slot-99 (start) entry of five timeline event classes (the camera selector, the player camera preset, the
//         letter box, the post process, the AI dialogue): which timeline the event belongs to (the root of [ctx+8]'s
//         parent chain +0x98, its id +0x1D8) and, through the table, to which stage, foreign or not; count and cost.
//   S-C   StartLoadingSequencer's slot 99 (0xC45740, the write P+0x1E0 = ev+0x38): the same, plus the values.
//   S-D   once per frame (guest_seq_play's 0x8B6144 hook): the client's camera manager's current entry, the timeline
//         player's lists, [[cw]+0xDE8], M's / X's input-block level (+0x230) and the latest live foreign stage's
//         timeline in the player; only the CHANGES are logged.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string_view>

namespace cdmp::session2::gpr {

// ---------------------------------------------------------------- build 2976 layout [V capstone / Ghidra]

// The three StageChartProcessor vtables (RTTI: ClientSequencerStage_ / CommonSequencerStage_ /
// ServerSequencerStage_StageChartProcessor), their complete object locators ([vt-8]) and the common `ret` 0x359B50.
inline constexpr uint32_t kVtProcClient = 0x558C080, kVtProcCommon = 0x590DF78, kVtProcServer = 0x5B1FE10;
inline constexpr uint32_t kColProcClient = 0x5DB7A20, kColProcCommon = 0x5EE6C48, kColProcServer = 0x5F83DF8;
inline constexpr uint32_t kRetRva = 0x359B50;
inline constexpr size_t kProcSlots = 105;  // slot 105 of each is data (its COL area)
// The StageChart_Function_* trampolines reach the slots +0x108 .. +0x2A0 (scf.txt); the rest are the processor's own
// (tick, conditions, binding) and are NOT a presentation classification (they differ between client and common too).
inline constexpr uint32_t kFnSlotLo = 0x100, kFnSlotHi = 0x2A8;

// The stage (S44, nativo.md §2.1): [stage+0x28] its manager G, +0x30 its sequencer id, +0x58 its processor, +0x98 its
// owner key, +0x28B started, +0x298 its timeline's id in the client's player (0xA9D8FD mov edx,[rdx+0x298] ->
// 0x34DC130([[0x6D691E0]+0x120], id)). The processor: +8 G, +0x10 the stage (the Execute trampolines, nativo.md §2.1).
inline constexpr uint32_t kStage_Mgr = 0x28, kStage_Seq = 0x30, kStage_Proc = 0x58, kStage_Owner = 0x98,
                          kStage_Started = 0x28B, kStage_Timeline = 0x298;
inline constexpr uint32_t kProc_Mgr = 0x08, kProc_Stage = 0x10;
// The timeline player [[0x6D691E0]+0x120] (0x34DC130): three lists {ptr, u32 n} at +0x58/+0x60, +0x48/+0x50 (both
// also need T+0x1D1) and +0x38/+0x40; a fourth one, +0xB8/+0xC0 (stride 0x18), is read by the game under the
// player's lock and is left out of the replica. T+0x1D8 = the id (0x34DBB03, a counter), T+0xDC "playing" [E: the
// manager's tick 0xAA3E70 asks it].
inline constexpr uint32_t kTl_Id = 0x1D8, kTl_Listed = 0x1D1, kTl_Playing = 0xDC;
struct TlList {
    uint32_t ptr, count;
    bool needs_listed;
};
inline constexpr TlList kTlLists[3] = {{0x58, 0x60, true}, {0x48, 0x50, true}, {0x38, 0x40, false}};
inline constexpr uint32_t kTlMaxPerList = 512;
// A timeline event's slot 99 (start): rcx = the event, rdx = the context; [ctx+8] a timeline node whose parent chain
// (+0x98) ends at the timeline (0xB27880 / 0xC35790 walk it; their root has the timeline's +0x1F8). Its +0x1D8 is
// compared with stage+0x298: that the root IS the player's timeline object is what S-B measures [H].
inline constexpr uint32_t kCtx_Node = 0x08, kNode_Parent = 0x98, kCtx_Flags = 0x5C, kEv_Data = 0x38;
inline constexpr uint32_t kMaxDepth = 32;
// The client world [0x6D691E0]: +0x10 the loading state P (P+0x1E0 = StartLoadingSequencer's byte), +0x118 the camera
// manager (its +0x40 the current entry, the object at entry - 0x28: 0xB27DFE / 0xB27E05), +0x120 the timeline player.
// [[cw]]+0xDE8: what SequencerCamera's .xtls code writes 0 (p8-s9-cinematica.md §7.3 [V est]).
inline constexpr uint32_t kWorldSlot = 0x6D691E0, kCw_Loading = 0x10, kCw_Camera = 0x118, kCw_Player = 0x120,
                          kCam_Current = 0x40, kCamEntryAdjust = 0x28, kP_StartLoading = 0x1E0, kW0_De8 = 0xDE8;
// A manager's input-block level: 0xA98AD0 (InputBlock 0x5E4E60 -> G) keeps the max level of its stages' blocks at
// G+0x230 (0xA98D3E mov [r13+0x230],dil) and hands it to .xtls. The stage manager's vtable 0x55C9720.
inline constexpr uint32_t kMgr_InputBlock = 0x230, kMgr_Actor = 0x18, kActor_Key = 0x60, kVtStageManager = 0x55C9720;

// ---------------------------------------------------------------- whose stage

inline constexpr uint32_t kCharClass = 0xA;
inline bool is_character(uint32_t key) { return key && (key >> 28) == kCharClass; }

enum class Whose : uint8_t {
    OwnUnknown,    // this client's own character is not known yet: nothing is foreign (S44 H-9)
    Own,           // the owner is this client's character or M's
    NotCharacter,  // no owner / an NPC key (a world stage)
    Foreign,       // another player's character
    Count
};
inline Whose whose(uint32_t owner, uint32_t own_key, uint32_t m_key) {
    if (!own_key) return Whose::OwnUnknown;
    if (owner && (owner == own_key || (m_key && owner == m_key))) return Whose::Own;
    if (!is_character(owner)) return Whose::NotCharacter;
    return Whose::Foreign;
}
inline const char* whose_name(Whose w) {
    switch (w) {
        case Whose::OwnUnknown: return "own key unknown";
        case Whose::Own: return "own";
        case Whose::NotCharacter: return "world (owner not a character)";
        case Whose::Foreign: return "FOREIGN";
        default: return "?";
    }
}

// ---------------------------------------------------------------- S-A: the stage's processor

enum class ProcVt : uint8_t { NoProc, Unreadable, Client, Common, Server, Other, Count };
inline ProcVt classify_proc(uint64_t proc, bool vt_read, uint64_t vt_rva) {
    if (!proc) return ProcVt::NoProc;
    if (!vt_read) return ProcVt::Unreadable;
    if (vt_rva == kVtProcClient) return ProcVt::Client;
    if (vt_rva == kVtProcCommon) return ProcVt::Common;
    if (vt_rva == kVtProcServer) return ProcVt::Server;
    return ProcVt::Other;
}
inline const char* proc_vt_name(ProcVt v) {
    switch (v) {
        case ProcVt::NoProc: return "no processor yet ([stage+0x58] = 0)";
        case ProcVt::Unreadable: return "processor unreadable";
        case ProcVt::Client: return "ClientSequencerStage_StageChartProcessor 0x558C080";
        case ProcVt::Common: return "CommonSequencerStage_StageChartProcessor 0x590DF78";
        case ProcVt::Server: return "ServerSequencerStage_StageChartProcessor 0x5B1FE10";
        case ProcVt::Other: return "ANOTHER vtable (none of the three)";
        default: return "?";
    }
}

// R-A's table, computed (never applied): per processor slot in the function range, its kind by the three natives.
enum class SlotKind : uint8_t { Same, ClientOnly, ClientServer, CommonDiffers };
inline SlotKind slot_kind(uint32_t client, uint32_t common, uint32_t server, uint32_t ret = kRetRva) {
    if (common != ret) return client == common ? SlotKind::Same : SlotKind::CommonDiffers;
    if (client == ret) return SlotKind::Same;
    return server == ret ? SlotKind::ClientOnly : SlotKind::ClientServer;
}
struct RaTable {
    static constexpr size_t kMax = 64;
    uint16_t client_only[kMax] = {};    // slot offsets (bytes), presentation for this client only
    uint16_t client_server[kMax] = {};  // implemented by the client and the server (Loading, Reward, ...)
    uint16_t common_differs[kMax] = {};
    size_t n_client_only = 0, n_client_server = 0, n_common_differs = 0;
    size_t outside_client_only = 0;     // client-only slots OUTSIDE the function range (internal: not presentation)
};
inline RaTable ra_table(std::span<const uint32_t> c, std::span<const uint32_t> m, std::span<const uint32_t> s,
                        uint32_t ret = kRetRva) {
    RaTable t;
    const size_t n = c.size() < m.size() ? (c.size() < s.size() ? c.size() : s.size()) : (m.size() < s.size() ? m.size() : s.size());
    for (size_t k = 0; k < n; ++k) {
        const auto off = static_cast<uint16_t>(8 * k);
        const SlotKind sk = slot_kind(c[k], m[k], s[k], ret);
        const bool in_fn = off >= kFnSlotLo && off < kFnSlotHi;
        if (sk == SlotKind::ClientOnly && !in_fn) {
            ++t.outside_client_only;
            continue;
        }
        if (!in_fn) continue;
        if (sk == SlotKind::ClientOnly && t.n_client_only < RaTable::kMax) t.client_only[t.n_client_only++] = off;
        if (sk == SlotKind::ClientServer && t.n_client_server < RaTable::kMax) t.client_server[t.n_client_server++] = off;
        if (sk == SlotKind::CommonDiffers && t.n_common_differs < RaTable::kMax) t.common_differs[t.n_common_differs++] = off;
    }
    return t;
}

// ---------------------------------------------------------------- R-A: the "world only" processor table
//
// guest_seq_foreign_present = 0 | probe | 1 (s45-escenas-ajenas.md §3.1). In P1, a FOREIGN stage whose processor
// [stage+0x58] has the client vtable 0x558C080 gets (1) or would get (probe) its vptr pointed at the mod's table: a copy
// of 0x558C080 whose presentation slots hold what the COMMON processor 0x590DF78 holds there. Every in-range slot
// whose three natives differ has an evidence-based decision for build 2976 (capstone on the exe, s45-datos/ra/):
//   Ret     what TURNS ON presentation for this client: client-only slots (common = server = ret 0x359B50) and the
//           three client+server ones (+0x200 HireMercenary, +0x218 Loading, +0x258 Reward): the common's `ret 0`;
//   Common  +0x288 VarySharpness: the common's own 0x204DDD0 (the owner-online check the client's handler calls first,
//           0x5F4EF0), without the client's work after it (UI service [cw]+0x28);
//   World   +0x1A0 / +0x1A8 DisableCharacterCollision: its only effect is a collision flag of the actors the function
//           binds (0x2049B90 -> 0x5037B0, comps+0x40 -> +0xE0 -> +0x4C), no client service: a WORLD effect, kept;
//   Off     review P2-1: what TURNS presentation OFF, or whose direction is in its data, is kept native, so an end /
//           off never stays cut while its on came from somewhere R-A does not cut (a world stage of X, before P1, a
//           refused stage): DeactivateMiniGame +0x130, DeactiveSequencerCamera +0x1B8 (0x5EA5B0 with dl = 0),
//           SequencerCamera +0x1C8 (dl = data+0x28: on or off), DeactivateSequencerCameraFadeIn +0x1D0 (sets
//           [[cw]]+0xDE8 = 1, the player camera) and its end +0x1D8 (opaque .xtls), CameraPreset's end +0x1E8
//           (0x5EB200, also the processor's own +0x310), FadeInOut +0x210 (from / to alpha in data+0x20 / +0x24),
//           LookAt's end +0x228, UIHint's end +0x250. Cut on-sides of those pairs: DeactiveSequencerCamera's end
//           +0x1C0 (0x5EA5B0 with dl = 1: back ON), CameraPreset +0x1E0, LookAt +0x220, UIHint +0x248, and LetterBox
//           +0x208 (0x5F9CD0 with dl = 1, always on; its off comes from the processor's own +0x80, 0x5EE93B dl = 0, or
//           the timeline event, both native).
// Any other difference (a slot that is not in this list, a handler at another RVA, another kind) means the exe is not
// the one the decisions were made for: R-A refuses as a whole.
enum class RaMode : uint8_t { Off, Probe, On };
inline RaMode parse_ra_mode(std::string_view v, bool* bad = nullptr) {
    if (bad) *bad = false;
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
    if (v.empty() || v == "0") return RaMode::Off;
    if (v == "1") return RaMode::On;
    if (v.size() == 5 && (v[0] | 0x20) == 'p' && (v[1] | 0x20) == 'r' && (v[2] | 0x20) == 'o' && (v[3] | 0x20) == 'b' &&
        (v[4] | 0x20) == 'e') {
        return RaMode::Probe;
    }
    if (bad) *bad = true;
    return RaMode::Off;
}
inline const char* ra_mode_name(RaMode m) {
    switch (m) {
        case RaMode::Off: return "0";
        case RaMode::Probe: return "probe";
        case RaMode::On: return "1";
        default: return "?";
    }
}

enum class RaUse : uint8_t { Native, Ret, Common, World, Off };
inline bool ra_keeps_native(RaUse u) { return u == RaUse::Native || u == RaUse::World || u == RaUse::Off; }
struct RaExpect {
    uint16_t off;     // the processor slot (bytes)
    uint32_t client;  // 0x558C080's handler there (RVA)
    uint32_t tramp;   // the StageChart_Function_* Execute (vf8/vf9) that jumps to it (the only one, tramp.py)
    SlotKind kind;    // what slot_kind() gives with the three natives
    RaUse use;
};
inline constexpr RaExpect kRaExpect[] = {
    {0x128, 0x5E36A0, 0x2368030, SlotKind::ClientOnly, RaUse::Ret},    // ActivateMiniGame
    {0x130, 0x5E4400, 0x2368050, SlotKind::ClientOnly, RaUse::Off},    // DeactivateMiniGame: off
    {0x138, 0x5E4B00, 0x2368070, SlotKind::ClientOnly, RaUse::Ret},    // ExecuteMiniGameEvent
    {0x140, 0x5E4B60, 0x2368150, SlotKind::ClientOnly, RaUse::Ret},    // Craft (GetLocalPlayer)
    {0x150, 0x5E4E60, 0x2368240, SlotKind::ClientOnly, RaUse::Ret},    // InputBlock
    {0x158, 0x5E4EB0, 0x2368880, SlotKind::ClientOnly, RaUse::Ret},    // UIControl (the shop)
    {0x160, 0x5E9750, 0x23688A0, SlotKind::ClientOnly, RaUse::Ret},    // ChangePhasePanelTag ([cw]+0x20)
    {0x168, 0x5ECD90, 0x2369A80, SlotKind::ClientOnly, RaUse::Ret},    // UIShowMinimap
    {0x170, 0x5ED9A0, 0x2369AA0, SlotKind::ClientOnly, RaUse::Ret},    // UIFindPath
    {0x198, 0x5E9790, 0x2369C30, SlotKind::ClientOnly, RaUse::Ret},    // SetCustomMesh ([cw]+0x120, +0x90)
    {0x1A0, 0x5E99C0, 0x2369C50, SlotKind::ClientOnly, RaUse::World},  // DisableCharacterCollision: world
    {0x1A8, 0x5E9B00, 0x2369C70, SlotKind::ClientOnly, RaUse::World},  // its end: world
    {0x1B0, 0x5E9C40, 0x2369C90, SlotKind::ClientOnly, RaUse::Ret},    // SetPreviewTarget
    {0x1B8, 0x5EE210, 0x2369CB0, SlotKind::ClientOnly, RaUse::Off},    // DeactiveSequencerCamera (dl 0): off
    {0x1C0, 0x5EE230, 0x2369CD0, SlotKind::ClientOnly, RaUse::Ret},    // its end (dl 1): back on
    {0x1C8, 0x5EE250, 0x2369CF0, SlotKind::ClientOnly, RaUse::Off},    // SequencerCamera: on/off in data
    {0x1D0, 0x5EE0C0, 0x2369D10, SlotKind::ClientOnly, RaUse::Off},    // DeactivateSequencerCameraFadeIn: off
    {0x1D8, 0x5EE160, 0x2369D30, SlotKind::ClientOnly, RaUse::Off},    // its end (opaque): kept
    {0x1E0, 0x5EDD30, 0x2369D50, SlotKind::ClientOnly, RaUse::Ret},    // CameraPreset: on
    {0x1E8, 0x5EE1F0, 0x2369D70, SlotKind::ClientOnly, RaUse::Off},    // its end (0x5EB200): off
    {0x1F0, 0x5EAE90, 0x2369D90, SlotKind::ClientOnly, RaUse::Ret},    // ForceLockOn
    {0x1F8, 0x5F2980, 0x2369F00, SlotKind::ClientOnly, RaUse::Ret},    // UIStageIcon
    {0x200, 0x5F4BC0, 0x2369FF0, SlotKind::ClientServer, RaUse::Ret},  // HireMercenary (client: [cw]+0x30)
    {0x208, 0x5F2970, 0x236A010, SlotKind::ClientOnly, RaUse::Ret},    // LetterBox (dl 1): on
    {0x210, 0x5F52E0, 0x236A030, SlotKind::ClientOnly, RaUse::Off},    // FadeInOut: direction in data
    {0x218, 0x5F2A00, 0x236A120, SlotKind::ClientServer, RaUse::Ret},  // Loading (what V1 forces since S9)
    {0x220, 0x5F3600, 0x236A140, SlotKind::ClientOnly, RaUse::Ret},    // LookAt: on
    {0x228, 0x5F3810, 0x236A160, SlotKind::ClientOnly, RaUse::Off},    // its end: off
    {0x238, 0x5F3A90, 0x236A1A0, SlotKind::ClientOnly, RaUse::Ret},    // UIStageGuide
    {0x240, 0x5F4050, 0x236A1C0, SlotKind::ClientOnly, RaUse::Ret},    // UIStageBackgroundGuide
    {0x248, 0x5F4110, 0x236A1E0, SlotKind::ClientOnly, RaUse::Ret},    // UIHint: on
    {0x250, 0x5F4200, 0x236A200, SlotKind::ClientOnly, RaUse::Off},    // its end: off
    {0x258, 0x5F4280, 0x236A2C0, SlotKind::ClientServer, RaUse::Ret},  // Reward (client: a thunk to .xtls)
    {0x260, 0x5F4440, 0x236A360, SlotKind::ClientOnly, RaUse::Ret},    // PostProcess
    {0x268, 0x5F4590, 0x236A400, SlotKind::ClientOnly, RaUse::Ret},    // MaterialParameter ([cw]+0x120)
    {0x288, 0x5F4EC0, 0x236A520, SlotKind::CommonDiffers, RaUse::Common},  // VarySharpness -> the common's 0x204DDD0
};
inline constexpr size_t kRaExpected = std::size(kRaExpect);
inline const RaExpect* ra_expect(uint32_t off) {
    for (const RaExpect& e : kRaExpect) {
        if (e.off == off) return &e;
    }
    return nullptr;
}
// The trampoline every StageChart_Function_* Execute uses (52 of them, tramp.py): mov rax,[rdx] (the stage) ; mov
// r8,rcx ; mov rcx,[rax+0x58] (its processor) ; mov rax,[rcx] (the vptr) ; jmp [rax+off]. A tail jump: what the slot
// returns is what Execute returns, and Execute is void (the base StageChart_Function's vf8 / vf9 are the folded void
// `ret 0` 0x359B50, while the folded bool `xor al,al ; ret` is 0x359BA0).
inline constexpr size_t kTrampBytes = 20;
inline void tramp_bytes(uint32_t off, uint8_t out[kTrampBytes]) {
    constexpr uint8_t head[16] = {0x48, 0x8B, 0x02, 0x4C, 0x8B, 0xC1, 0x48, 0x8B,
                                  0x48, 0x58, 0x48, 0x8B, 0x01, 0x48, 0xFF, 0xA0};
    for (size_t i = 0; i < 16; ++i) out[i] = head[i];
    for (size_t i = 0; i < 4; ++i) out[16 + i] = static_cast<uint8_t>(off >> (8 * i));
}

struct RaPlan {
    bool ok = false;
    const char* why = "";
    uint16_t bad_off = 0;      // the slot that failed, when !ok
    RaUse use[kProcSlots] = {};  // Native everywhere but the decided slots
    size_t n_ret = 0, n_common = 0, n_world = 0, n_off = 0, n_client_server = 0;
};
// c / m / s: the 105 slots (RVAs) of the client, common and server vtables.
inline RaPlan ra_plan(std::span<const uint32_t> c, std::span<const uint32_t> m, std::span<const uint32_t> s,
                      uint32_t ret = kRetRva) {
    RaPlan p;
    if (c.size() < kProcSlots || m.size() < kProcSlots || s.size() < kProcSlots) {
        p.why = "fewer than 105 slots read";
        return p;
    }
    size_t seen = 0;
    for (size_t k = 0; k < kProcSlots; ++k) {
        const auto off = static_cast<uint16_t>(8 * k);
        const RaExpect* e = ra_expect(off);
        if (off < kFnSlotLo || off >= kFnSlotHi) {
            if (e) {
                p.why = "a decided slot is outside the StageChart function range";
                p.bad_off = off;
                return p;
            }
            continue;
        }
        const SlotKind kind = slot_kind(c[k], m[k], s[k], ret);
        if (kind == SlotKind::Same) {
            if (e) {
                p.why = "a decided slot is the same in the three vtables";
                p.bad_off = off;
                return p;
            }
            continue;
        }
        if (!e) {
            p.why = "a slot differs between the vtables and has no decision (not build 2976's table)";
            p.bad_off = off;
            return p;
        }
        if (e->client != c[k] || e->kind != kind) {
            p.why = "a decided slot has another handler or another kind (not build 2976's table)";
            p.bad_off = off;
            return p;
        }
        if (e->use == RaUse::Native || (e->use == RaUse::Ret && m[k] != ret) ||
            (e->use == RaUse::Common && m[k] == ret)) {
            p.why = "the common's slot is not what the decision takes";
            p.bad_off = off;
            return p;
        }
        p.use[k] = e->use;
        ++seen;
        if (e->use == RaUse::Ret) ++p.n_ret;
        if (e->use == RaUse::Common) ++p.n_common;
        if (e->use == RaUse::World) ++p.n_world;
        if (e->use == RaUse::Off) ++p.n_off;
        if (e->kind == SlotKind::ClientServer) ++p.n_client_server;
    }
    if (seen != kRaExpected) {
        p.why = "not every decided slot was found";
        return p;
    }
    p.ok = true;
    return p;
}
// The mod's table: out[0] = the client vtable's [-1] (its COL: typeid / dynamic_cast keep working), out[1 + k] = the
// slot: the client's (Native / World / Off) or the common's (Ret / Common). c_abs / m_abs: the slots as absolute addresses.
inline bool ra_build(const RaPlan& p, std::span<const uint64_t> c_abs, std::span<const uint64_t> m_abs, uint64_t col_abs,
                     std::span<uint64_t> out) {
    if (!p.ok || c_abs.size() < kProcSlots || m_abs.size() < kProcSlots || out.size() < 1 + kProcSlots || !col_abs) {
        return false;
    }
    out[0] = col_abs;
    for (size_t k = 0; k < kProcSlots; ++k) out[1 + k] = ra_keeps_native(p.use[k]) ? c_abs[k] : m_abs[k];
    return true;
}

// One P1 of a stage, judged (any thread; the write itself is the caller's: a compare-exchange of [proc]).
enum class RaVerdict : uint8_t {
    NotForeign,        // not another player's stage: never cut (restored if it has the mod's table: review P2-2)
    NoProcessor,       // [stage+0x58] = 0 at P1
    Unreadable,        // the processor or its vptr unreadable
    MUnknown,          // review P1-1: M (this client's own manager) not seen yet: nothing written
    InM,               // review P1-1: the stage is M's (G = M): never cut (restored if it has the mod's table)
    AlreadyMod,        // its vptr is already the mod's table (a second P1 of the same stage)
    OtherVtable,       // not 0x558C080 (common / server / another class): never touched
    NotThisStage,      // [proc+0x10] is not the stage (the per-stage processor not proven for it)
    OtherManager,      // [proc+8] is not [stage+0x28]
    NotKnownX,         // review P1-1: G is not guest_seq_play's current X of the stage's owner
    NotMainThread,     // P1 is not on the thread that ticks X (guest_seq_play's 0x8B6144), or that one is unknown
    Would,             // probe: all checks passed, 1 would write
    Write,             // 1: all checks passed: write
    Count
};
struct RaFacts {
    RaMode mode = RaMode::Off;
    Whose whose = Whose::OwnUnknown;
    uint64_t stage = 0, stage_mgr = 0;  // the stage and its [stage+0x28]
    uint64_t m = 0;                     // this client's M (0: not seen yet)
    bool x_known = false;               // guest_seq_play_knows_x(stage_mgr, owner): G is a known X of that owner
    uint64_t proc = 0;
    bool vt_read = false;
    uint64_t vt = 0;                    // [proc]
    uint64_t proc_stage = 0, proc_mgr = 0;
    uint64_t client_vt = 0, mod_vt = 0;  // absolute: exe+0x558C080 and &table[1]
    uint32_t tid = 0, main_tid = 0;
};
inline RaVerdict ra_judge(const RaFacts& f) {
    if (f.whose != Whose::Foreign) return RaVerdict::NotForeign;
    if (!f.proc) return RaVerdict::NoProcessor;
    if (!f.vt_read) return RaVerdict::Unreadable;
    if (!f.m) return RaVerdict::MUnknown;
    if (f.stage_mgr == f.m) return RaVerdict::InM;
    if (f.mod_vt && f.vt == f.mod_vt) return RaVerdict::AlreadyMod;
    if (!f.client_vt || f.vt != f.client_vt) return RaVerdict::OtherVtable;
    if (f.proc_stage != f.stage) return RaVerdict::NotThisStage;
    if (f.proc_mgr != f.stage_mgr) return RaVerdict::OtherManager;
    if (!f.x_known) return RaVerdict::NotKnownX;
    if (!f.main_tid || f.tid != f.main_tid) return RaVerdict::NotMainThread;
    return f.mode == RaMode::On ? RaVerdict::Write : RaVerdict::Would;
}
// Review P2-2: a stage that must never carry the mod's table (not foreign, or M's) but does (a recycled stage, or an
// earlier wrong judgement): the caller puts the client vtable back (one compare-exchange) and says so.
inline bool ra_restore_due(const RaFacts& f, RaVerdict v) {
    if (!f.proc || !f.vt_read || !f.mod_vt || !f.client_vt || f.vt != f.mod_vt) return false;
    return v == RaVerdict::NotForeign || v == RaVerdict::MUnknown || v == RaVerdict::InM;
}
inline const char* ra_verdict_name(RaVerdict v) {
    switch (v) {
        case RaVerdict::NotForeign: return "not foreign";
        case RaVerdict::NoProcessor: return "no processor ([stage+0x58] = 0)";
        case RaVerdict::Unreadable: return "processor unreadable";
        case RaVerdict::MUnknown: return "M not seen yet";
        case RaVerdict::InM: return "the stage is M's (G = M)";
        case RaVerdict::AlreadyMod: return "already the mod's table";
        case RaVerdict::OtherVtable: return "not the client processor vtable 0x558C080";
        case RaVerdict::NotThisStage: return "[proc+0x10] is not the stage";
        case RaVerdict::OtherManager: return "[proc+8] is not [stage+0x28]";
        case RaVerdict::NotKnownX: return "G is not guest_seq_play's X of the owner";
        case RaVerdict::NotMainThread: return "P1 not on guest_seq_play's frame thread";
        case RaVerdict::Would: return "WOULD CUT (probe)";
        case RaVerdict::Write: return "CUT";
        default: return "?";
    }
}

// ---------------------------------------------------------------- R-B: the foreign timeline's camera starts
//
// guest_seq_foreign_timeline = 0 | probe | 1 (s45-escenas-ajenas.md §3.2; measured in the step-1 live run, vivo1.md).
// In the S-B hooks of CameraSelectorEventData (0x34CFC20, a 5-byte leaf thunk) and SequencerPlayerCameraPreset
// (0xB27880, an entry with .pdata), slot 99 (the event's START) only: when the event's timeline belongs to a stage R-A
// would cut, probe logs '[R-B WOULD SKIP]' and 1 returns at the entry without running it ('[R-B SKIPPED]').
// - The event -> stage link [V live]: [ctx+8] IS the timeline object (vptr exe+0x5C75F30, depth 0), its +0x1D8 is the
//   stage's +0x298 that P1 noted.
// - Slot 99 only [V]: each entry is that class's slot 99 and no other slot of its vtable; no direct call reaches it;
//   and the call must come back to one of the two verified slot-99 call sites of the event dispatcher (0x36574C7 ->
//   0x36574CD, which jumps to the epilogue; 0x36575D0 -> 0x36575D6, which goes on to slot 101). Both ignore RAX: the
//   dispatcher ends with `mov al,1 ; ret` (0x3657600).
// - Slots 97 / 98 of both classes are 0x359B50 (`ret 0`): returning at the entry with RAX untouched is what the base
//   class's empty void body does. Slots 100, 101 (the end) and 103 stay native.
inline constexpr uint32_t kVtTimeline = 0x5C75F30;  // the timeline object's vptr ([ctx+8]); no RTTI ([-1] is code)
inline constexpr uint32_t kRbCallers[] = {0x36574CD, 0x36575D6};
inline bool rb_caller_ok(uint32_t caller_rva) {
    for (uint32_t c : kRbCallers) {
        if (c == caller_rva) return true;
    }
    return false;
}

enum class RbVerdict : uint8_t {
    NotSlot99,      // the return address is not one of the dispatcher's two slot-99 call sites
    NotClass,       // the event's vptr is not the hooked class's (review of R-B P3-2)
    NoTimeline,     // [ctx+8] unreadable or not the timeline object (vptr != exe+0x5C75F30)
    NoStage,        // its id (+0x1D8) is no stage P1 noted
    Gone,           // that stage no longer reads back its sequencer / owner (destroyed or reused)
    NotForeign,     // the stage is not another player's: runs natively
    MUnknown,       // M not seen yet
    InM,            // the stage is M's
    NotKnownX,      // its G is not guest_seq_play's X of its owner
    NotMainThread,  // not on guest_seq_play's frame thread
    Would,          // probe: 1 would skip
    Skip,           // 1: return at the entry
    Count
};
struct RbFacts {
    RaMode mode = RaMode::Off;
    uint32_t caller = 0;     // RVA of [rsp] at the entry
    bool event_class = false;  // [rcx] is the hooked class's vtable
    bool timeline = false;   // [ctx+8] read and its vptr is exe+0x5C75F30
    bool found = false;      // its id is a stage P1 noted
    bool alive = false;      // that stage still reads back its sequencer, owner and timeline id (+0x298)
    Whose whose = Whose::OwnUnknown;
    uint64_t stage_mgr = 0, m = 0;
    bool x_known = false;
    uint32_t tid = 0, main_tid = 0;
};
inline RbVerdict rb_judge(const RbFacts& f) {
    if (!rb_caller_ok(f.caller)) return RbVerdict::NotSlot99;
    if (!f.event_class) return RbVerdict::NotClass;
    if (!f.timeline) return RbVerdict::NoTimeline;
    if (!f.found) return RbVerdict::NoStage;
    if (!f.alive) return RbVerdict::Gone;
    if (f.whose != Whose::Foreign) return RbVerdict::NotForeign;
    if (!f.m) return RbVerdict::MUnknown;
    if (f.stage_mgr == f.m) return RbVerdict::InM;
    if (!f.x_known) return RbVerdict::NotKnownX;
    if (!f.main_tid || f.tid != f.main_tid) return RbVerdict::NotMainThread;
    return f.mode == RaMode::On ? RbVerdict::Skip : RbVerdict::Would;
}
inline const char* rb_verdict_name(RbVerdict v) {
    switch (v) {
        case RbVerdict::NotSlot99: return "not from the dispatcher's slot-99 call";
        case RbVerdict::NotClass: return "the event is not of the hooked class";
        case RbVerdict::NoTimeline: return "[ctx+8] is not the timeline object";
        case RbVerdict::NoStage: return "timeline of no stage P1 noted";
        case RbVerdict::Gone: return "its stage is gone";
        case RbVerdict::NotForeign: return "not foreign";
        case RbVerdict::MUnknown: return "M not seen yet";
        case RbVerdict::InM: return "the stage is M's";
        case RbVerdict::NotKnownX: return "G is not guest_seq_play's X of the owner";
        case RbVerdict::NotMainThread: return "not on guest_seq_play's frame thread";
        case RbVerdict::Would: return "WOULD SKIP (probe)";
        case RbVerdict::Skip: return "SKIPPED";
        default: return "?";
    }
}
// A line is worth writing when the event belongs to a foreign stage (whatever the verdict) or R-B acts.
inline bool rb_makes_line(const RbFacts& f, RbVerdict v) {
    return v == RbVerdict::Would || v == RbVerdict::Skip || (f.found && f.alive && f.whose == Whose::Foreign);
}

// ---------------------------------------------------------------- the stages P1 saw

struct StageRec {
    uint64_t seq = 0;    // stage+0x30
    uint64_t mgr = 0;    // stage+0x28 (G; this process only, never sent anywhere)
    uint64_t stage = 0;  // the stage (this process only: S-D's liveness check re-reads seq / owner there)
    uint64_t us = 0;     // when P1 saw it
    uint32_t tl = 0;     // stage+0x298
    uint32_t owner = 0;  // stage+0x98
    Whose whose = Whose::OwnUnknown;
};
// Written by P1 (any thread), read by S-A2 / S-B / S-C / S-D (any thread): lock-free, per-slot sequence check (a torn
// read only gives a wrong diagnostic line). The most recent N starts; an older one is overwritten (counted).
template <size_t N>
class StageTable {
public:
    void note(const StageRec& r) {
        if (!r.seq) return;
        size_t i = N;
        for (size_t k = 0; k < N; ++k) {
            if (seq_[k].load(std::memory_order_acquire) == r.seq) {
                i = k;
                break;
            }
        }
        if (i == N) {
            i = next_.fetch_add(1, std::memory_order_relaxed) % N;
            if (seq_[i].load(std::memory_order_relaxed)) overwritten_.fetch_add(1, std::memory_order_relaxed);
        }
        seq_[i].store(0, std::memory_order_release);
        mgr_[i].store(r.mgr, std::memory_order_relaxed);
        stage_[i].store(r.stage, std::memory_order_relaxed);
        us_[i].store(r.us, std::memory_order_relaxed);
        tl_[i].store(r.tl, std::memory_order_relaxed);
        owner_[i].store(r.owner, std::memory_order_relaxed);
        whose_[i].store(static_cast<uint8_t>(r.whose), std::memory_order_relaxed);
        seq_[i].store(r.seq, std::memory_order_release);
        noted_.fetch_add(1, std::memory_order_relaxed);
    }
    // A timeline id: 0 and 0xFFFFFFFF (no timeline) never match. Several slots with the id: the most recent start
    // (review of R-B P3-1).
    bool find_tl(uint32_t tl, StageRec& out) const {
        if (!tl || tl == 0xFFFFFFFFu) return false;
        bool any = false;
        for (size_t k = 0; k < N; ++k) {
            const uint64_t s = seq_[k].load(std::memory_order_acquire);
            if (!s || tl_[k].load(std::memory_order_relaxed) != tl) continue;
            StageRec r;
            if (!read(k, s, r)) continue;
            if (!any || r.us > out.us) {
                out = r;
                any = true;
            }
        }
        return any;
    }
    bool find_seq(uint64_t seq, StageRec& out) const {
        if (!seq) return false;
        for (size_t k = 0; k < N; ++k) {
            if (seq_[k].load(std::memory_order_acquire) != seq) continue;
            if (read(k, seq, out)) return true;
        }
        return false;
    }
    // The most recent foreign start (by P1's time).
    bool latest_foreign(StageRec& out) const {
        bool any = false;
        for (size_t k = 0; k < N; ++k) {
            const uint64_t s = seq_[k].load(std::memory_order_acquire);
            if (!s || whose_[k].load(std::memory_order_relaxed) != static_cast<uint8_t>(Whose::Foreign)) continue;
            StageRec r;
            if (!read(k, s, r)) continue;
            if (!any || r.us > out.us) {
                out = r;
                any = true;
            }
        }
        return any;
    }
    uint64_t noted() const { return noted_.load(std::memory_order_relaxed); }
    uint64_t overwritten() const { return overwritten_.load(std::memory_order_relaxed); }
    static constexpr size_t capacity() { return N; }

private:
    bool read(size_t k, uint64_t s, StageRec& out) const {
        StageRec r;
        r.seq = s;
        r.mgr = mgr_[k].load(std::memory_order_relaxed);
        r.stage = stage_[k].load(std::memory_order_relaxed);
        r.us = us_[k].load(std::memory_order_relaxed);
        r.tl = tl_[k].load(std::memory_order_relaxed);
        r.owner = owner_[k].load(std::memory_order_relaxed);
        r.whose = static_cast<Whose>(whose_[k].load(std::memory_order_relaxed));
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq_[k].load(std::memory_order_relaxed) != s) return false;
        out = r;
        return true;
    }
    std::atomic<uint64_t> seq_[N] = {}, mgr_[N] = {}, stage_[N] = {}, us_[N] = {};
    std::atomic<uint32_t> tl_[N] = {}, owner_[N] = {};
    std::atomic<uint8_t> whose_[N] = {};
    std::atomic<size_t> next_{0};
    std::atomic<uint64_t> noted_{0}, overwritten_{0};
};

// The root of a timeline node's parent chain (+0x98), bounded. rd(addr, out) is the caller's guarded 8-byte read.
// Returns 0 when the node is 0 or a read fails; depth = the links followed; *cut = the chain was longer than max.
template <class Rd>
uint64_t root_of(uint64_t node, Rd&& rd, uint32_t& depth, bool& cut, uint32_t max_depth = kMaxDepth) {
    depth = 0;
    cut = false;
    if (!node) return 0;
    uint64_t cur = node;
    for (;;) {
        uint64_t parent = 0;
        if (!rd(cur + kNode_Parent, parent)) return 0;
        if (!parent) return cur;
        if (depth >= max_depth) {
            cut = true;
            return cur;
        }
        cur = parent;
        ++depth;
    }
}

// ---------------------------------------------------------------- S-B / S-C: the timeline events

enum class Ev : uint8_t { CameraSelector, PlayerCameraPreset, LetterBox, PostProcess, AIDialog, StartLoading, Count };
inline constexpr size_t kEvs = static_cast<size_t>(Ev::Count);
struct EvInfo {
    const char* name;
    uint32_t vt;     // the event class's vtable (RTTI)
    uint32_t col;    // its complete object locator [vt-8]
    uint32_t entry;  // its slot 99 (start), [vt + 99*8]
};
inline constexpr uint32_t kStartSlot = 99;
inline EvInfo ev_info(Ev e) {
    switch (e) {
        case Ev::CameraSelector: return {"CameraSelectorEventData", 0x558B4A8, 0x5DB7610, 0x34CFC20};
        case Ev::PlayerCameraPreset: return {"SequencerPlayerCameraPreset", 0x55F71B8, 0x5DE2538, 0xB27880};
        case Ev::LetterBox: return {"SequencerCustomEventLetterBoxControl", 0x562E0B0, 0x5E08690, 0xC38F30};
        case Ev::PostProcess: return {"SequencerCustomEventPostProcess", 0x562E420, 0x5E085F0, 0xC3A440};
        case Ev::AIDialog: return {"SequencerAIDialog", 0x5629BF0, 0x5E08488, 0xC4D6B0};
        case Ev::StartLoading: return {"SequencerGamePlayEventStartLoadingSequencer", 0x5630920, 0x5E07EE8, 0xC45740};
        default: return {"?", 0, 0, 0};
    }
}

// Whose timeline an event belongs to.
enum class Tl : uint8_t {
    Foreign,     // the root's id is the timeline of a FOREIGN stage P1 saw
    Own,         // ... of this client's own stage
    World,       // ... of a stage without a character owner
    OwnUnknown,  // ... of a stage P1 saw before this client's own key was known
    NotAStage,   // the root's id is no stage's P1 saw (another kind of timeline, or a stage older than the table)
    Unreadable,  // no node, a read failed, or the chain was cut
    Count
};
inline Tl classify_tl(bool root_ok, bool found, Whose w) {
    if (!root_ok) return Tl::Unreadable;
    if (!found) return Tl::NotAStage;
    switch (w) {
        case Whose::Foreign: return Tl::Foreign;
        case Whose::Own: return Tl::Own;
        case Whose::NotCharacter: return Tl::World;
        default: return Tl::OwnUnknown;
    }
}
inline const char* tl_name(Tl t) {
    switch (t) {
        case Tl::Foreign: return "timeline of a FOREIGN stage";
        case Tl::Own: return "timeline of an own stage";
        case Tl::World: return "timeline of a world stage";
        case Tl::OwnUnknown: return "timeline of a stage judged before the own key was known";
        case Tl::NotAStage: return "timeline of no stage P1 saw";
        case Tl::Unreadable: return "context unreadable";
        default: return "?";
    }
}

// ---------------------------------------------------------------- S-A2: stage functions before P1

enum class When : uint8_t { BeforeP1, AfterP1, Count };

// ---------------------------------------------------------------- S-D: the client's presentation state

struct Present {
    bool world = false;     // [0x6D691E0] read
    uint64_t cam = 0;       // [[cw+0x118]+0x40]
    uint32_t cam_vt = 0;    // RVA of the vtable of the object at cam - 0x28 (0: none / unreadable)
    uint32_t tl_n[3] = {};  // the player's three lists' counts (kTlLists order)
    bool de8_ok = false;
    uint32_t de8 = 0;       // [[cw]+0xDE8]
    uint8_t m_block = 0xFF; // M+0x230 (0xFF: unknown)
    uint8_t x_block = 0xFF; // the latest live foreign stage's G+0x230 (0xFF: none / not a stage manager)
    uint64_t fseq = 0;      // that stage's sequencer (0: no live foreign stage)
    uint8_t ftl = 0;        // its timeline in the player: 0 not found, 1..3 the list, 0xFF unreadable
    uint8_t fplay = 0xFF;   // that timeline's +0xDC (0xFF: not found)
};
enum PresentField : uint32_t {
    kFWorld = 1u << 0,
    kFCam = 1u << 1,
    kFCamVt = 1u << 2,
    kFTlCounts = 1u << 3,  // counted, but alone never makes a line (it moves with every timeline of the game)
    kFDe8 = 1u << 4,
    kFMBlock = 1u << 5,
    kFXBlock = 1u << 6,
    kFForeign = 1u << 7,
    kFForeignTl = 1u << 8,
    kFForeignPlay = 1u << 9,
};
inline uint32_t diff(const Present& a, const Present& b) {
    uint32_t d = 0;
    if (a.world != b.world) d |= kFWorld;
    if (a.cam != b.cam) d |= kFCam;
    if (a.cam_vt != b.cam_vt) d |= kFCamVt;
    if (a.tl_n[0] != b.tl_n[0] || a.tl_n[1] != b.tl_n[1] || a.tl_n[2] != b.tl_n[2]) d |= kFTlCounts;
    if (a.de8_ok != b.de8_ok || a.de8 != b.de8) d |= kFDe8;
    if (a.m_block != b.m_block) d |= kFMBlock;
    if (a.x_block != b.x_block) d |= kFXBlock;
    if (a.fseq != b.fseq) d |= kFForeign;
    if (a.ftl != b.ftl) d |= kFForeignTl;
    if (a.fplay != b.fplay) d |= kFForeignPlay;
    return d;
}
// The changes that make a line (every one is counted).
inline constexpr uint32_t kLineFields = ~static_cast<uint32_t>(kFTlCounts);
inline bool makes_line(uint32_t d) { return (d & kLineFields) != 0; }

// S-D's replica of 0x34DC130 (up to 3 x 512 entries, 2-3 guarded reads each) runs only when the latest live foreign
// stage changed or every kSdTlEvery frames; in between the last result is kept (revision.md B3: the cost per frame).
inline constexpr uint32_t kSdTlEvery = 16;
inline bool sd_scan_due(uint64_t fseq, uint64_t last_fseq, uint32_t frames_since_scan) {
    return fseq != last_fseq || frames_since_scan >= kSdTlEvery;
}

// A foreign stage counts as live for S-D while its stage still reads back its sequencer and owner, for at most this
// long after its start (a stage older than that is not "the latest" any more).
inline constexpr uint64_t kForeignLiveUs = 600'000'000;  // 10 min
inline bool foreign_live(const StageRec& r, uint64_t now_us, uint64_t seq_read, uint32_t owner_read) {
    return r.seq && seq_read == r.seq && owner_read == r.owner && now_us >= r.us && now_us - r.us <= kForeignLiveUs;
}

// S-B's callers (revision.md B1): the RVA of each event start's return address ([rsp] at the entry), per class, in a
// few lock-free slots; the letter box's 0xC38F30 is also its vtable's slot 67 (a property change, not a start), and its
// callers tell the two apart. Any thread; a full table counts the rest as "other".
template <size_t N>
class CallerCounts {
public:
    void add(uint32_t rva) {
        for (size_t k = 0; k < N && rva; ++k) {  // 0 (a caller outside the exe / unreadable): "other"
            uint32_t cur = rva_[k].load(std::memory_order_acquire);
            if (cur == 0 && rva_[k].compare_exchange_strong(cur, rva, std::memory_order_acq_rel)) cur = rva;
            if (cur == rva) {
                n_[k].fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        other_.fetch_add(1, std::memory_order_relaxed);
    }
    uint32_t rva(size_t k) const { return k < N ? rva_[k].load(std::memory_order_acquire) : 0; }
    uint64_t n(size_t k) const { return k < N ? n_[k].load(std::memory_order_relaxed) : 0; }
    uint64_t other() const { return other_.load(std::memory_order_relaxed); }
    static constexpr size_t capacity() { return N; }

private:
    std::atomic<uint32_t> rva_[N] = {};
    std::atomic<uint64_t> n_[N] = {};
    std::atomic<uint64_t> other_{0};
};

// ---------------------------------------------------------------- cost of a probe callback (QPC ticks)

class Cost {
public:
    void add(uint64_t ticks) {
        n_.fetch_add(1, std::memory_order_relaxed);
        total_.fetch_add(ticks, std::memory_order_relaxed);
        uint64_t m = max_.load(std::memory_order_relaxed);
        while (ticks > m && !max_.compare_exchange_weak(m, ticks, std::memory_order_relaxed)) {
        }
    }
    uint64_t n() const { return n_.load(std::memory_order_relaxed); }
    uint64_t total() const { return total_.load(std::memory_order_relaxed); }
    uint64_t max() const { return max_.load(std::memory_order_relaxed); }

private:
    std::atomic<uint64_t> n_{0}, total_{0}, max_{0};
};
// ticks -> nanoseconds with the QPC frequency (0 frequency: 0).
inline uint64_t ticks_ns(uint64_t ticks, uint64_t freq) {
    if (!freq) return 0;
    return static_cast<uint64_t>(static_cast<long double>(ticks) * 1e9L / static_cast<long double>(freq));
}

}  // namespace cdmp::session2::gpr
