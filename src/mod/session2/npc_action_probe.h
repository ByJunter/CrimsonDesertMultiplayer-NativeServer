#pragma once
// S33: resultado interno de acción y veto anterior a la creación del descriptor.
#include <cstdint>
#include <span>
#include <string_view>
#include "mod/session2/guest_actor_diag_logic.h"
namespace cdmp::session2 {
// wanted = user2_stages_npc_probes_on() tras configurar (sin user2_stages no hay sondas ni hooks).
void npc_action_probe_configure(bool wanted);
bool npc_action_probe_wanted() noexcept;
void npc_action_probe_install(uintptr_t base, std::span<const gad::Section> sections, bool applied_mid_ready);
void npc_action_probe_applied(uintptr_t component, uint32_t key) noexcept;
void npc_action_probe_drain();
}
