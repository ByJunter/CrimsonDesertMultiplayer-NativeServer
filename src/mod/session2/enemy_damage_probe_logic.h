#pragma once
// S42, build 2976: observación de 0x209CB30. Ningún resultado decide por el
// juego.
#include <cstdint>
#include <string_view>
namespace cdmp::session2::edp {
enum class Mode : uint8_t { Off, Probe, Detail };
inline Mode mode(std::string_view s) noexcept {
  return s == "probe" ? Mode::Probe : s == "1" ? Mode::Detail : Mode::Off;
}
struct Sample {
  uint32_t valid = 0, key = 0, controller = 0;
  uintptr_t vtable = 0;
  uint8_t type = 0, context_flags = 0;
};
// Cada bit distingue cero leído de memoria desconocida. Sólo direcciones
// locales temporales.
template <class Read>
Sample sample(Mode m, uintptr_t actor, uintptr_t context,
              Read &&read) noexcept {
  Sample s;
  if (m == Mode::Off)
    return s;
  uintptr_t type_info = 0;
  if (actor) {
    if (read(actor, &s.vtable, sizeof s.vtable))
      s.valid |= 1;
    if (read(actor + 0x60, &s.key, sizeof s.key))
      s.valid |= 2;
    if (read(actor + 0x90, &s.controller, sizeof s.controller))
      s.valid |= 4;
    if (read(actor + 0x88, &type_info, sizeof type_info) && type_info &&
        read(type_info + 1, &s.type, sizeof s.type))
      s.valid |= 8;
  }
  if (context && read(context + 4, &s.context_flags, sizeof s.context_flags))
    s.valid |= 16;
  return s;
}
} // namespace cdmp::session2::edp
