#include "mod/session2/enemy_damage_probe_logic.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
using namespace cdmp::session2;
int main() {
  int failures = 0;
  const auto check = [&](bool v) {
    if (!v)
      ++failures;
  };
  check(edp::mode("bad") == edp::Mode::Off);
  check(edp::mode("0") == edp::Mode::Off);
  check(edp::mode("probe") == edp::Mode::Probe);
  check(edp::mode("1") == edp::Mode::Detail);
  std::array<uint8_t, 0x300> arena{};
  auto put = [&](size_t at, auto v) {
    std::memcpy(arena.data() + at, &v, sizeof v);
  };
  put(0x100, uintptr_t{0x145123456});
  put(0x160, uint32_t{0xB0101650});
  put(0x190, uint32_t{0});
  put(0x188, uintptr_t{0x200});
  put(0x201, uint8_t{3});
  put(0x284, uint8_t{0x20});
  unsigned reads = 0;
  auto read = [&](uintptr_t at, void *out, size_t n) noexcept {
    ++reads;
    if (at > arena.size() || n > arena.size() - at)
      return false;
    std::memcpy(out, arena.data() + at, n);
    return true;
  };
  auto s = edp::sample(edp::Mode::Off, 0x100, 0x280, read);
  check(reads == 0 && s.valid == 0);
  s = edp::sample(edp::Mode::Probe, 0x100, 0x280, read);
  check(s.valid == 31 && s.key == 0xB0101650 && s.type == 3 &&
        s.controller == 0 && s.context_flags == 0x20);
  auto bad = [](uintptr_t, void *, size_t) noexcept { return false; };
  s = edp::sample(edp::Mode::Detail, 0x100, 0x280, bad);
  check(s.valid ==
        0); // Ilegible no se registra como controlador o flags cero conocidos.
  put(0x188, uintptr_t{0x999999});
  s = edp::sample(edp::Mode::Probe, 0x100, 0x280, read);
  check((s.valid & 8) == 0 && (s.valid & 16) != 0);
  s = edp::sample(edp::Mode::Detail, 0, 0, read);
  check(s.valid == 0);
  std::printf("EnemyDamageProbe: %d fallos\n", failures);
  return failures ? 1 : 0;
}
