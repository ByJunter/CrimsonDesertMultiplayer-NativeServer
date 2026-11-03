#pragma once
// S42: observación de la aparición de la escena candidata de puerta; nunca abre
// puertas ni modifica datos.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
namespace cdmp::session2 {
enum class DoorTraceStage : uint8_t {
  HostClientReq,
  HostClientAck,
  GuestSend,
  GuestDeliver,
  ServerHostReq,
  ServerGuestReq,
  ServerGuestOut,
  Count
};
// 0: inerte; probe: totales + resultados nativos; 1: añade detalles de
// paquetes. Ambos son sólo observación.
void door_trace_start(const std::string &raw, bool guest);
// Llamar en worker una vez con el código del juego disponible; huésped no
// instala gancho.
bool door_trace_install(uintptr_t base);
// Trama completa propia del mod (no puntero a memoria del juego). t_us es QPC
// del productor, 0 usa reloj actual.
void door_trace_packet(DoorTraceStage stage, std::span<const uint8_t> frame,
                       uint64_t t_us = 0) noexcept;
// Servidor: copia SEH acotada de memoria del juego, antes de su handler; no
// modifica la petición.
void door_trace_ingress(uint16_t op, uintptr_t packet, uint16_t size,
                        bool from_user2) noexcept;
// Sólo worker/network tick; formatea fuera de cualquier gancho. No invocar
// desde take_frame ni un hook.
void door_trace_flush();
} // namespace cdmp::session2
