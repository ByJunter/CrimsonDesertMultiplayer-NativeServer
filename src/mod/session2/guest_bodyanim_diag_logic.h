#pragma once
// P8 session 13, guest_seq_bodyanim_diag (docs/research/pendientes/p8-impl-secuenciador-bodyanim.md), GUEST,
// read-only. The pure part (no game access): what one G5 reading of a watched NPC's control component says, the
// caller of the graph configuration 0x2432F60 and the cohort counts of the summary.
//
// [V] (npc-raiz-s11 bodyanim/informe.txt, re-read with capstone on the build 2976 PE this session):
//   CC = [[actor+0x68]+0x40] (the control component; CC+8 = the actor), body = [CC+0x110].
//   BodyAnim 0xA79770(body, desc, u8 p3, u8 p4, ...): rsi = body from 0xA797BF on (the only write of rsi in the
//   function 0xA79770..0xA7A254), r14 = desc; [body] = CC, [body+8] = the actor.
//   It installs the SeqBodyAnim graph in CC+0x70 (ensure 0xA7987C -> 0xA7D550 -> .xtls 0x9F0FB10, or the store
//   0xA79BAC) and, for a second layer, CC+0x78 (0xA79F4C); configures each with 0x2432F60 (calls 0xA79C1F / 0xA79FC5,
//   return addresses 0xA79C24 / 0xA79FCA): rcx = the graph, rdx = &name ref ([[desc+0x18]+0x18] -> {char*, u32 len,
//   u32 hash @+0xC}), r8 = the blend name ref, xmm3 = [[desc+0x18]+0x8C], stack p5..p8 = desc+0x2C/+0x30/+0x28/+0x3C
//   (floats), p9 = &body+0xE8 (the resource holder); then CC+0x23A = 1 (0xA7A181).
//   Layers: [CC+0x88] (0) / [CC+0x90] (1); the current action's hash u32 [[layer+0x10]+0x18] (remote_motion), the
//   layer's own hash +0x3C and chart +0x110 (guest_seq_restore). A graph whose byte +0x15 == 1 is invalidated
//   (0x9F0FB10 rebuilds it).

#include <bit>
#include <cstddef>
#include <cstdint>

namespace cdmp::session2::gba {

inline constexpr uint32_t kHashSeqBodyAnim = 0x837487D4;      // seq_bodyanim_action (layer 0)
inline constexpr uint32_t kHashSeqBodyAnimLoop = 0x35DD392F;  // seq_bodyanim_action_loop (layer 1)
inline constexpr uint32_t kHashWait = 0x26107D3C;             // the idle wait guest_seq_restore substitutes

inline constexpr uintptr_t kBodyAnim = 0xA79770;
inline constexpr uintptr_t kConfigure = 0x2432F60;
inline constexpr uintptr_t kRetLayer0 = 0xA79C24;  // after 0xA79C1F call 0x2432F60 (CC+0x70's graph)
inline constexpr uintptr_t kRetLayer1 = 0xA79FCA;  // after 0xA79FC5 call 0x2432F60 (CC+0x78's graph)

// Which BodyAnim call configured a graph (the return address at 0x2432F60's entry, as an RVA): 0 / 1 = layer, -1 =
// another caller (counted, not read: its rsi is not a body).
inline int configure_layer(uintptr_t ret_rva) {
    if (ret_rva == kRetLayer0) return 0;
    if (ret_rva == kRetLayer1) return 1;
    return -1;
}

inline bool is_seq_hash(uint32_t h) { return h == kHashSeqBodyAnim || h == kHashSeqBodyAnimLoop; }

struct LayerSnap {
    bool present = false;   // [CC+0x88+8i] != 0
    bool chart = false;     // [layer+0x110] != 0
    uint32_t hash = 0;      // u32 [layer+0x3C] (what a restore asks)
    uint32_t current = 0;   // u32 [[layer+0x10]+0x18] (the action playing; 0 = none / unreadable)
    bool operator==(const LayerSnap&) const = default;
};

// One G5 reading (the diagnostics thread, SEH reads, no lock: a torn read only gives a wrong line).
struct BodySnap {
    bool exists = false;      // in this client's actor table with that key
    uint8_t type = 0;         // [[actor+0x88]+1] (3 = NPC)
    bool cc = false;          // CC found and CC+8 == the actor
    uint64_t g70 = 0;         // [CC+0x70] (the SeqBodyAnim graph of layer 0; 0 = never built)
    uint64_t g78 = 0;         // [CC+0x78] (layer 1)
    uint8_t g70_dead = 0;     // [g70+0x15] (1 = invalidated)
    uint8_t g78_dead = 0;
    uint8_t notified = 0;     // u8 [CC+0x23A] (BodyAnim sets 1 after queueing its entries)
    uint8_t pending = 0;      // u8 [CC+0xA8] (pending actions flag, horse_diag)
    LayerSnap layer[2];
    // From the hooks (cumulative, per watched actor).
    uint64_t bodyanim = 0;    // 0xA79770 entries for this actor
    uint64_t cfg[2] = {};     // 0x2432F60 from BodyAnim, layer 0 / 1
    bool operator==(const BodySnap&) const = default;
};

// What the reading says about the family (d) question (p8-s12-npc-familias.md §5).
enum class BodyState : uint8_t {
    NotHere,       // not in this client
    NoCC,          // no control component (or it does not point back to the actor)
    Graph,         // CC+0x70 built: some BodyAnim ran for it on this client
    SeqNoGraph,    // a layer asks for seq_bodyanim and there is no graph: what the restore substitutes
    Wait,          // a layer is in the idle wait 0x26107D3C, no graph (the restore's substitute, or a real wait)
    Other,         // no graph, some other action (walking, its own idle...)
    Count
};
inline const char* body_state_name(BodyState s) {
    switch (s) {
        case BodyState::NotHere: return "not in this client";
        case BodyState::NoCC: return "no control component";
        case BodyState::Graph: return "GRAPH BUILT (CC+0x70)";
        case BodyState::SeqNoGraph: return "seq_bodyanim WITHOUT graph";
        case BodyState::Wait: return "idle wait 0x26107D3C, no graph";
        case BodyState::Other: return "no graph, other action";
        default: return "?";
    }
}
inline BodyState body_state(const BodySnap& s) {
    if (!s.exists) return BodyState::NotHere;
    if (!s.cc) return BodyState::NoCC;
    if (s.g70) return BodyState::Graph;
    for (const LayerSnap& l : s.layer) {
        if (is_seq_hash(l.hash) || is_seq_hash(l.current)) return BodyState::SeqNoGraph;
    }
    for (const LayerSnap& l : s.layer) {
        if (l.hash == kHashWait || l.current == kHashWait) return BodyState::Wait;
    }
    return BodyState::Other;
}

// The summary's cohort (the watched NPCs at one G5 tick).
struct Cohort {
    uint32_t watched = 0;
    uint32_t state[static_cast<size_t>(BodyState::Count)] = {};
    uint32_t g78 = 0;          // with CC+0x78 too
    uint32_t ran = 0;          // the hooks saw a BodyAnim for it this session
    uint32_t ran_no_graph = 0; // ... and still no CC+0x70 now (installed then torn down, or not kept)
    void add(const BodySnap& s) {
        ++watched;
        ++state[static_cast<size_t>(body_state(s))];
        g78 += s.g78 != 0;
        ran += s.bodyanim != 0;
        ran_no_graph += s.bodyanim != 0 && !s.g70;
    }
};

inline float f32_of(uint32_t bits) { return std::bit_cast<float>(bits); }
inline uint32_t bits_of(float f) { return std::bit_cast<uint32_t>(f); }

}  // namespace cdmp::session2::gba
