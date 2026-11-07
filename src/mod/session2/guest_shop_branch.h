#pragma once
// [session2] guest_shop_branch = 0 (default) | 1 | 2 | 3, GUEST: the shopkeepers' window on the guest's own client
// (guest_shop_branch_logic.h has the why and the wire). Needs interact_gate=1 with the NPC types (3, 4) so the guest's
// "Shop" press reaches the server, guest_x_interaction_stage=1 for the prompt, and store_gate=1 on both PCs so its
// buy / sell pass the shop-scene test.
// 2 (experiment, live 2026-11-06): as 1, and while the copy is in the shop branch this client is the shopkeeper's
// controller ([npc+0x90] := its user, the host's put back after kNpcControlMs): natively the scene's owner client runs
// the shopkeeper's AI step (2606 / 2202 for the NPC right after branch 6) before the menu's UIControl events, and the
// guest never controls the host's NPCs. The client's own Reqs for that NPC are withheld meanwhile (never sent).
// Live 07:37: swapped and put back cleanly, but no effect (no AI step, no UIControl): see docs/ESTADO-3527.md.
// 3 (experiment): the copy is made BEFORE the press, as soon as this client looks at a shopkeeper (C1's point A sees
// an entry with the shop key 0x000F4321, or after one 'Shop' press on it), and once it runs, C1 points the NPC's
// interaction entries at the copy ([entry+0x20] := its id). Then the press, its success and the branch request are
// the copy's own, as on the host (the native scene got the interaction and asked branch 6 itself; a branch handed
// from outside never ran the menu's UIControl). Its 2436 are answered locally; nothing is handed after a 2916.
// Live 08:07: DO NOT USE. Once the copy exists the client stops evaluating interaction prompts at all (no "R" on any
// NPC) and the copy poses the guest's character; the native scene needs the server running it too (docs/ESTADO-3527.md).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cdmp::session2 {

// ini_path: the role's ini (guest_shop_sell_node is read from it again at each Sell request).
void guest_shop_branch_start(const std::string& mode, bool guest, const std::wstring& ini_path = {});
// interact_gate's press hook (the game's main thread): the guest pressed an interaction on `npc` whose entry names the
// scene `stage` ([entry+0x20]).
void guest_shop_note_press(uint32_t npc, uint64_t stage, uintptr_t actor = 0, uint32_t own_user = 0,
                           uint32_t npc_controller = 0) noexcept;
// guest.cpp's delivery (NetworkThread), after a host packet reached the client: learns the scenes' owners (2661) and,
// after this client's own 2916 for the shop, appends the Acks to hand the client now (logical packets).
void guest_shop_after_deliver(std::span<const uint8_t> packet, std::vector<std::vector<uint8_t>>& out) noexcept;
// Also on the NetworkThread: the answers queued by guest_shop_withhold.
void guest_shop_drain(std::vector<std::vector<uint8_t>>& out) noexcept;
// guest.cpp's send path: true = a 2436 for a scene this client opened locally; withheld and answered locally.
bool guest_shop_withhold(const uint8_t* packet, size_t size) noexcept;
// True while this client has a shop scene open locally (guest_x_veto then lets the host scenes' UI handlers run).
bool guest_shop_open_now() noexcept;
// guest_shop_branch=3, for C1's point A (the game's main thread). On: mode 3. want_copy: make this client's copy of
// the host scene (on the NetworkThread, from its 2661). ready_copy: the copy's id once its stage runs, else 0.
// host_of: the host scene a copy id was made from (also after the copy is gone), else 0.
bool guest_shop_proactive() noexcept;
void guest_shop_want_copy(uint64_t host_scene, const char* why) noexcept;
uint64_t guest_shop_ready_copy(uint64_t host_scene) noexcept;
uint64_t guest_shop_host_of(uint64_t copy) noexcept;
bool guest_shop_copy_alive(uint64_t copy) noexcept;

}  // namespace cdmp::session2
