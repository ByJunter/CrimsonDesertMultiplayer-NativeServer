#pragma once
// S42, build 2976: extensión experimental de B para la familia medida C12CB783.
// No amplía npc_family_types ni la fase C. El modo 0 usa íntegra la política
// anterior. 0x2797510 (.rsrc) crea _LinkedVehicle con desc base, +2F0/+2F8 y
// +490=clave del padre tipo 5. La carga tipo 7 bajo un gimmick queda entera con
// el anfitrión; nunca se le concede IA.
#include "mod/session2/npc_family_logic.h"

namespace cdmp::session2::famveh {
inline constexpr uint32_t kMeasuredResource = 0xC12CB783;
enum class Mode : uint8_t { Off, Probe, On };
// Esta entrega NO autoriza asignar vehículos de horario: el Logout/carga nativo
// todavía puede vaciar su stage por fuera de drain_now. Pedir 1 se degrada a
// observación; no se publica una promesa de seguridad parcial.
inline constexpr Mode effective_mode(Mode requested) noexcept {
  return requested == Mode::Off ? Mode::Off : Mode::Probe;
}
// Sólo los tardíos de una unidad viva usaban la lectura actual del vector en B.
// Una lectura ADICIONAL de la sonda nunca reemplaza la clase reservada del
// preexamen (aunque sea ilegible o contradiga lo observado antes).
inline constexpr npcfam::Cls actual_class(npcfam::Cls reserved,
                                          npcfam::Cls observed, bool late,
                                          bool live) noexcept {
  return late && live ? observed : reserved;
}
enum class Projected : uint8_t { Unknown, Assigned, Kept, Split };
enum class Kind : uint8_t {
  Ordinary,
  ScheduleVehicle,
  Linked,
  Gimmick,
  Cargo,
  Other
};
inline bool scope(uint64_t node) noexcept {
  return static_cast<uint32_t>(node) == kMeasuredResource;
}
inline bool projected_root(const npcfam::UnitSum &u) noexcept {
  return u.admitted || u.would || u.admit == npcfam::Admit::NodeSplitBefore;
}
struct In {
  npcfam::MemberIn member;
  uint32_t vt = 0, wrap = 0;
  uint8_t parent_type = 0, depth = 0;
  int in_vector = -1;
  bool parent_kept = false, parent_key_matches = false, player_rides = false;
};
struct Out {
  npcfam::MemberOut member;
  Kind kind = Kind::Ordinary;
};
inline Out decide(const In &in) noexcept {
  using namespace npcfam;
  MemberIn m = in.member;
  if (!m.admitted)
    return {{MemberVerdict::HostUnit, SplitWhy::None}, Kind::Ordinary};
  if (in.depth > kMaxDepth)
    return {{MemberVerdict::Split, SplitWhy::LateNoRoom}, Kind::Other};
  // Todos los elementos, incluidos los host-kept, deben seguir en el vector del
  // padre observado.
  if (in.in_vector != 1)
    return {{MemberVerdict::Split, SplitWhy::LateNotInVector}, Kind::Other};
  Kind k = Kind::Ordinary;
  if (in.parent_kept) {
    k = Kind::Cargo;
    if (m.type != 7 || (in.vt != kVtDescBase && in.vt != kVtInstantGimmick))
      return {{MemberVerdict::Split, SplitWhy::Type}, k};
    m.cls = Cls::Gimmick;
    m.types.mask = static_cast<uint16_t>(m.types.mask & ~(1u << 7));
  } else if (in.vt == kVtInstantGimmick && m.type == 7) {
    k = Kind::Gimmick;
    m.cls = Cls::Gimmick;
    m.types.mask = static_cast<uint16_t>(m.types.mask & ~(1u << 7));
  } else if (in.vt == kVtNpcSchedule && m.cls == Cls::Schedule && m.type == 5) {
    k = Kind::ScheduleVehicle;
    m.types.mask = static_cast<uint16_t>(m.types.mask | 1u << 5);
  } else if (in.vt == kVtDescBase && in.wrap == kVtWrapLinkedVehicle) {
    k = Kind::Linked;
    if (m.type != 5 || in.parent_type != 5 || !in.parent_key_matches)
      return {{MemberVerdict::Split, SplitWhy::Type}, k};
    m.cls = Cls::Schedule;
    m.types.mask = static_cast<uint16_t>(m.types.mask | 1u << 5);
  }
  if (m.type == 5 && in.player_rides)
    return {{MemberVerdict::Split, SplitWhy::UserChild}, k};
  return {member_verdict(m), k};
}
inline Projected projection(npcfam::MemberOut o) noexcept {
  return o.v == npcfam::MemberVerdict::Assign     ? Projected::Assigned
         : o.v == npcfam::MemberVerdict::HostKept ? Projected::Kept
                                                  : Projected::Split;
}
} // namespace cdmp::session2::famveh
