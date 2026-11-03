#pragma once
// S33: sólo observación, sin hooks nuevos, escrituras ni llamadas al juego.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
namespace cdmp::session2 {
void npc_probe_client_start(std::string_view activation, std::string_view family, std::string_view road, bool guest, void* stop_event);
void npc_probe_client_stop() noexcept;
void npc_probe_client_sniff(const uint8_t* packet, size_t size, uint64_t t_us, bool server_to_client) noexcept;
void npc_probe_client_received(uint16_t op, std::span<const uint8_t> payload, uint64_t epoch, uint64_t queued_us) noexcept;
void npc_probe_client_delivered(std::span<const uint8_t> packet, uint64_t epoch, uint64_t queued_us) noexcept;
void npc_probe_client_out(std::span<const uint8_t> packet, uint64_t epoch, bool transmitted) noexcept;
}
