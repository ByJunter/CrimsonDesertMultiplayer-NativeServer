#pragma once
// [session2] guest_shop_branch = 0 (default) | 1, GUEST. Pure logic (tests: tests/optd_guest_shop_branch_tests.cpp).
//
// A shopkeeper's window opens from its scene (cd_seq_funcnpc_*): after InteractionDoStepSuccessAck 2916 for the shop
// interaction (0x000F4321), the client that owns the scene asks StageBranchStateReq 2436 (scene, branch 6) and the
// server answers SetAvoidByCutSceneAck 2561, StageOnBranchEventAck 2524 and StageBranchStateAck 2981; the branch shows
// "<shop> - <keeper>: Buy / Sell / Leave" (live on the host, 3549, 2026-11-06: scene 0x1AF, packets below). In the
// host's world every such scene is the HOST's: the guest's copy never asks for the branch, and if it did the server
// would move the host's scene (its 2981 goes to the host too). Here the guest answers ITSELF, for its own client only:
// after its own 2916 for a shop interaction it hands its client the same three Acks for the scene the pressed entry
// named ([entry+0x20], noted at the press), and a later 2436 of its client for that scene (Leave: branch 0) is
// withheld and answered the same way. Nothing reaches the host's server for that scene (the host's observer already
// drops the guest's 2436 / 2591 / 2767 / 2173 for its scenes).
//
// Wire (logical opcodes; payloads as captured, 3549):
//   2916 InteractionDoStepSuccessAck  u32 requester | u32 actor | u32 target | u32 interaction | u32      (20 B)
//   2661 PrepareSequencerAck          u8 kind | u32 actor @1 | u32 owner @5 | u64 scene @9 | ...
//   2436 StageBranchStateReq          u64 scene | u32 branch | u16                                         (14 B)
//   2561 SetAvoidByCutSceneAck        04 | u32 owner | u64 scene | 01 | u32 owner                           (18 B)
//   2524 StageOnBranchEventAck        04 | u32 owner | u64 scene | u32 branch | 00                          (18 B)
//   2981 StageBranchStateAck          04 | u32 owner | u64 scene | u32 1 | u32 branch                       (21 B)

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cdmp::session2::gsb {

inline constexpr uint16_t kOpDoStepOk = 2916, kOpPrepare = 2661, kOpBranchReq = 2436;
inline constexpr uint16_t kOpAvoidCutScene = 2561, kOpOnBranchEvent = 2524, kOpBranchState = 2981;
inline constexpr uint32_t kShopInteraction = 0x000F4321;
inline constexpr uint32_t kShopBranch = 6;
inline constexpr size_t kHeader = 5;  // u16 opcode | i8 slot | u16 payload size

inline uint16_t op_of(std::span<const uint8_t> p) noexcept {
    return p.size() >= kHeader ? static_cast<uint16_t>(p[0] | p[1] << 8) : 0;
}
template <class T>
T at(std::span<const uint8_t> pay, size_t off) noexcept {
    T v{};
    std::memcpy(&v, pay.data() + off, sizeof v);
    return v;
}

struct DoStep {
    uint32_t requester = 0, actor = 0, target = 0, interaction = 0;
};
inline std::optional<DoStep> parse_dostep(std::span<const uint8_t> p) noexcept {
    if (op_of(p) != kOpDoStepOk || p.size() < kHeader + 16) return std::nullopt;
    const auto pay = p.subspan(kHeader);
    return DoStep{at<uint32_t>(pay, 0), at<uint32_t>(pay, 4), at<uint32_t>(pay, 8), at<uint32_t>(pay, 12)};
}
// The scene and its owner of a 2661.
inline std::optional<std::pair<uint64_t, uint32_t>> parse_prepare(std::span<const uint8_t> p) noexcept {
    if (op_of(p) != kOpPrepare || p.size() < kHeader + 17) return std::nullopt;
    const auto pay = p.subspan(kHeader);
    return std::pair{at<uint64_t>(pay, 9), at<uint32_t>(pay, 5)};
}
// The scene and branch of a 2436 the client sends.
inline std::optional<std::pair<uint64_t, uint32_t>> parse_branch_req(std::span<const uint8_t> p) noexcept {
    if (op_of(p) != kOpBranchReq || p.size() < kHeader + 12) return std::nullopt;
    const auto pay = p.subspan(kHeader);
    return std::pair{at<uint64_t>(pay, 0), at<uint32_t>(pay, 8)};
}

inline std::vector<uint8_t> packet(uint16_t op, std::span<const uint8_t> payload) {
    std::vector<uint8_t> v(kHeader + payload.size());
    v[0] = static_cast<uint8_t>(op);
    v[1] = static_cast<uint8_t>(op >> 8);
    v[2] = 0xFF;  // slot -1, as the server's Acks
    v[3] = static_cast<uint8_t>(payload.size());
    v[4] = static_cast<uint8_t>(payload.size() >> 8);
    std::memcpy(v.data() + kHeader, payload.data(), payload.size());
    return v;
}
inline void put32(uint8_t* d, uint32_t v) noexcept { std::memcpy(d, &v, 4); }
inline void put64(uint8_t* d, uint64_t v) noexcept { std::memcpy(d, &v, 8); }

// on: 1 when the scene enters a branch that avoids cut scenes (the shop), 0 when it leaves it (live, 2026-11-06).
inline std::vector<uint8_t> avoid_cut_scene(uint32_t owner, uint64_t scene, bool on = true) {
    std::array<uint8_t, 18> b{};
    b[0] = 4;
    put32(&b[1], owner);
    put64(&b[5], scene);
    b[13] = on ? 1 : 0;
    put32(&b[14], owner);
    return packet(kOpAvoidCutScene, b);
}
inline std::vector<uint8_t> on_branch_event(uint32_t owner, uint64_t scene, uint32_t branch) {
    std::array<uint8_t, 18> b{};
    b[0] = 4;
    put32(&b[1], owner);
    put64(&b[5], scene);
    put32(&b[13], branch);
    return packet(kOpOnBranchEvent, b);
}
inline std::vector<uint8_t> branch_state(uint32_t owner, uint64_t scene, uint32_t branch) {
    std::array<uint8_t, 21> b{};
    b[0] = 4;
    put32(&b[1], owner);
    put64(&b[5], scene);
    put32(&b[13], 1);
    put32(&b[17], branch);
    return packet(kOpBranchState, b);
}
// The shop's MENU step (live on the host, 3549, 2026-11-06 16:28, Delkin, scene 0x1AD; packet log at 386.4-388.4 s):
// 2 s after the shop branch the owner's client asks branch 0 (2436, its first node InputBlock +
// DeactivateSequencerCameraFadeIn is over) and the server answers 2524 branch 0, 2561 off and a 2981 of 29 B
//   04 | u32 owner | u64 scene | u32 2 | u32 0 | u32 0x979C909D | u32 0
// (the usual 29-B 2981 carries 1 | branch | 0x22B6EC91 | 0; the shop branch's own one is the 21-B 1 | 6). With it the
// client's stage goes to its menu node (ChangePhasePanelTag + 3 UIControl: "Buy / Sell / Leave"). The server's step is
// 0x2AF1300 -> 0x2BA1420 -> 0x21FDE10 on its own thread; a client gets it only through this answer.
inline constexpr uint32_t kShopMenuPhase = 2, kShopMenuTag = 0x979C909Du;
inline std::vector<uint8_t> branch_state_phase(uint32_t owner, uint64_t scene, uint32_t phase, uint32_t branch,
                                               uint32_t tag) {
    std::array<uint8_t, 29> b{};
    b[0] = 4;
    put32(&b[1], owner);
    put64(&b[5], scene);
    put32(&b[13], phase);
    put32(&b[17], branch);
    put32(&b[21], tag);
    put32(&b[25], 0);
    return packet(kOpBranchState, b);
}
// A 2981 as the server writes it: 21 B (state | branch) or, with a tag, 29 B (state | branch | tag | 0).
inline std::vector<uint8_t> branch_state_raw(uint32_t owner, uint64_t scene, uint32_t state, uint32_t branch,
                                             std::optional<uint32_t> tag) {
    if (tag) return branch_state_phase(owner, scene, state, branch, *tag);
    std::array<uint8_t, 21> b{};
    b[0] = 4;
    put32(&b[1], owner);
    put64(&b[5], scene);
    put32(&b[13], state);
    put32(&b[17], branch);
    return packet(kOpBranchState, b);
}
// The server's answers to the owner's branch requests of a shop scene, as captured on the host (3549, 2026-11-06; five
// native shop sessions, the same every time) and what each branch is (the guest's copy, live 17:38-17:41: Buy asks
// branch 0, Sell branch 1, Leave branch 3; Esc in the buy window branch 0 again):
//   branch 6 (Shop)                        2561 on, 2524 6, 2981 1 | 6
//   branch 0, 2 s later (the menu step)    2524 0, 2561 off, 2981 2 | 0 | 0x979C909D
//   branch 0 from the menu (Buy)           2524 0, 2981 3 | 0      (08:13 host; live on the guest: the buy window)
//   branch 1 from the menu (Sell)          2524 1, 2981 4 | 1      (NOT captured: node 4 is the chart's NpcStoreSellView)
//   branch 3 (Leave)                       2524 3, 2981 7 | 3      (07:05 host)
//   branch 1 after Leave (3 s later)       2524 1, 2981 0 | 1 | 0x979C909D (07:05 host)
// menu_shown: the scene had its menu step since its last branch 6 / window; left: it asked Leave since.
// The first u32 of the 2981 reads as the chart node the scene goes to. The grocer's chart, read in the guest's memory
// (node definitions inline, in order): 0 idle, 1 InputBlock + DeactivateSequencerCameraFadeIn, 2 the menu, 3
// NpcStoreBuyView, 4 NpcStoreSellView, 5 a second buy view, 6 a camera fade, 7 ChangePhasePanelTag (Leave). sell_node:
// guest_shop_sell_node (default 4), read again at each request.
inline std::vector<std::vector<uint8_t>> native_answer(uint32_t owner, uint64_t scene, uint32_t branch, bool menu_shown,
                                                      bool left = false, uint32_t sell_node = 4) {
    std::vector<std::vector<uint8_t>> out;
    switch (branch) {
    case kShopBranch:
        out.push_back(avoid_cut_scene(owner, scene, true));
        out.push_back(on_branch_event(owner, scene, branch));
        out.push_back(branch_state_raw(owner, scene, 1, branch, std::nullopt));
        break;
    case 0:
        out.push_back(on_branch_event(owner, scene, 0));
        if (!menu_shown) {
            out.push_back(avoid_cut_scene(owner, scene, false));
            out.push_back(branch_state_raw(owner, scene, kShopMenuPhase, 0, kShopMenuTag));
        } else {
            out.push_back(branch_state_raw(owner, scene, 3, 0, std::nullopt));
        }
        break;
    case 3:
        out.push_back(on_branch_event(owner, scene, 3));
        out.push_back(branch_state_raw(owner, scene, 7, 3, std::nullopt));
        break;
    case 1:
        out.push_back(on_branch_event(owner, scene, 1));
        if (menu_shown && !left) {
            out.push_back(branch_state_raw(owner, scene, sell_node, 1, std::nullopt));
        } else {
            out.push_back(branch_state_raw(owner, scene, 0, 1, kShopMenuTag));
        }
        break;
    default:
        out.push_back(on_branch_event(owner, scene, branch));
        out.push_back(branch_state_raw(owner, scene, 1, branch, std::nullopt));
        break;
    }
    return out;
}
inline std::vector<std::vector<uint8_t>> menu_answer(uint32_t owner, uint64_t scene, uint32_t tag = kShopMenuTag) {
    std::vector<std::vector<uint8_t>> out;
    out.push_back(on_branch_event(owner, scene, 0));
    out.push_back(avoid_cut_scene(owner, scene, false));
    out.push_back(branch_state_phase(owner, scene, kShopMenuPhase, 0, tag));
    return out;
}
// What the server sends for a branch request of the owner: on entering the shop all three, afterwards the last two.
inline std::vector<std::vector<uint8_t>> answer(uint32_t owner, uint64_t scene, uint32_t branch, bool with_avoid) {
    std::vector<std::vector<uint8_t>> out;
    if (with_avoid) out.push_back(avoid_cut_scene(owner, scene, branch != 0));
    out.push_back(on_branch_event(owner, scene, branch));
    out.push_back(branch_state(owner, scene, branch));
    return out;
}

}  // namespace cdmp::session2::gsb
