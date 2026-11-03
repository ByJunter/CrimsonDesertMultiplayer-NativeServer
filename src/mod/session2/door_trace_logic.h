#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
namespace cdmp::session2::doortrace {
inline constexpr uint32_t kResource = 0xDE7B52AA;
// Candidata, NO nombre de puerta demostrado. Identidad de escena medida en las
// cuatro capturas S42.
inline constexpr std::array<uint8_t, 16> kScene{
    0x4f, 0xb9, 0xee, 0xab, 0x67, 0x0f, 0x74, 0x56,
    0x3c, 0x34, 0x3d, 0x64, 0x0c, 0,    0,    0};
inline bool observe_tap(bool guest, bool host_client_tap) {
  return !guest || !host_client_tap;
}
inline bool verified_patch(std::span<const uint8_t> bytes) {
  constexpr std::array<uint8_t, 5> head{0x48, 0x89, 0x5c, 0x24, 0x08};
  return bytes.size() == head.size() &&
         std::memcmp(bytes.data(), head.data(), head.size()) == 0;
}
inline uint16_t u16(const uint8_t *p) {
  uint16_t x;
  std::memcpy(&x, p, 2);
  return x;
}
inline uint32_t u32(const uint8_t *p) {
  uint32_t x;
  std::memcpy(&x, p, 4);
  return x;
}
inline bool is_scene(std::span<const uint8_t> bytes) {
  return bytes.size() == 16 &&
         std::memcmp(bytes.data(), kScene.data(), 16) == 0;
}
enum class Kind : uint8_t { Spawn, Accepted, Rejected, Sync, Count };
struct Packet {
  bool valid = false, matched = false;
  Kind kind = Kind::Spawn;
  uint32_t subject = 0, resource = 0, items = 0;
  uint8_t active = 0;
};
inline bool relevant(uint16_t op) {
  return op == 2928 || op == 2145 || op == 2953 || op == 2665;
}
// Todas las posiciones son payload, no trama. No busca UUID en bytes
// arbitrarios de otro mensaje.
inline Packet parse(uint16_t op, std::span<const uint8_t> p) {
  Packet r;
  size_t begin = 0;
  if (op == 2928) {
    if (p.size() < 9 || p[0] != 4 || (p.size() - 9) % 16)
      return r;
    // 0x29C1C80 sólo exige que el u32 inicial no sea cero; deriva n por
    // longitud. No imponer igualdad no demostrada.
    r.items = static_cast<uint32_t>((p.size() - 9) / 16);
    r.resource = u32(p.data() + 5);
    begin = 9;
    if (!u32(p.data() + 1) || !r.items)
      return r;
    r.kind = Kind::Spawn;
  } else if (op == 2145) {
    if (p.size() < 22 || p[0] != 4 || (p.size() - 6) % 16)
      return r;
    r.subject = u32(p.data() + 1);
    r.active = p[5];
    r.items = static_cast<uint32_t>((p.size() - 6) / 16);
    begin = 6;
    r.kind = Kind::Accepted;
  } else if (op == 2953) {
    if (p.size() < 21 || p[0] != 4 || (p.size() - 5) % 16)
      return r;
    r.subject = u32(p.data() + 1);
    r.items = static_cast<uint32_t>((p.size() - 5) / 16);
    begin = 5;
    r.kind = Kind::Rejected;
  } else if (op == 2665) {
    // Sólo el registro de tamaño 615 y n=1 medido. Otras variantes se observan
    // como no decodificadas, no inválidas.
    if (p.size() != 615 || u32(p.data() + 4) != 1)
      return r;
    r.valid = true;
    r.kind = Kind::Sync;
    r.items = 1;
    r.matched = is_scene(p.subspan(8, 16));
    return r;
  } else
    return r;
  r.valid = true;
  for (size_t i = begin; i + 16 <= p.size(); i += 16)
    if (is_scene(p.subspan(i, 16)))
      r.matched = true;
  return r;
}
inline Packet frame(std::span<const uint8_t> bytes) {
  if (bytes.size() < 5 || u16(bytes.data() + 3) != bytes.size() - 5)
    return {};
  return parse(u16(bytes.data()), bytes.subspan(5));
}
enum class Result : uint8_t {
  Success,
  ResourceMissing,
  ResourceNotReady,
  NoCandidates,
  CreationFailed,
  Other,
  Unreadable,
  Count
};
inline Result classify(bool readable, uint32_t value,
                       const std::array<uint32_t, 4> &native,
                       bool constants_readable) {
  if (!readable)
    return Result::Unreadable;
  if (value == 0)
    return Result::Success;
  if (constants_readable) {
    size_t count = 0, which = 0;
    for (size_t i = 0; i < native.size(); ++i)
      if (native[i] && value == native[i]) {
        ++count;
        which = i;
      }
    if (count == 1)
      return static_cast<Result>(which + 1);
  }
  return Result::Other;
}
inline const char *result_name(Result r) {
  switch (r) {
  case Result::Success:
    return "exito";
  case Result::ResourceMissing:
    return "recurso-sin-nombre-6CF7018";
  case Result::ResourceNotReady:
    return "recurso-no-listo-6CF701C";
  case Result::NoCandidates:
    return "sin-candidatos-6CF7024";
  case Result::CreationFailed:
    return "creacion-fallida-6CF7028";
  case Result::Other:
    return "otro";
  default:
    return "lectura-fallida";
  }
}
} // namespace cdmp::session2::doortrace
