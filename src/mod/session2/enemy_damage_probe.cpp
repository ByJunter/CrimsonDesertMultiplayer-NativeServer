#include "mod/session2/enemy_damage_probe.h"
#include "mod/game/hook_prepare.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/enemy_damage_probe_logic.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"
#include <atomic>
#include <cstring>
#include <intrin.h>
#include <safetyhook.hpp>
#include <vector>
#include <windows.h>
namespace cdmp::session2 {
namespace {
// .pdata 209CB30..209CD2C; firma de 40 B única en .rsrc del exe 2976.
// Primeras tres instrucciones mov [rsp+off], reg: 5+5+5 B, sin referencias RIP
// ni saltos entrantes al interior. Únicas longitudes aceptadas: 5 y 15.
constexpr uintptr_t kRva = 0x209CB30;
constexpr uint8_t kBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c,
                              0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x48,
                              0x89, 0x4c, 0x24, 0x08, 0x57, 0x41, 0x56, 0x41,
                              0x57, 0x48, 0x83, 0xec, 0x20, 0x41, 0x0f, 0xb6,
                              0xf1, 0x41, 0x0f, 0xb6, 0xd8, 0x48, 0x8b, 0xfa};
SafetyHookInline g_hook;
uintptr_t g_base = 0;
std::atomic<edp::Mode> g_mode{edp::Mode::Off};
std::atomic<uint64_t> g_calls{0}, g_true{0};
p8p::EventRing<256> g_ring;
p8p::WindowLimiter g_probe_limit{12, 1'000'000}, g_detail_limit{60, 1'000'000};
uint64_t g_last_summary = 0;
uint32_t rva(uintptr_t p) noexcept {
  return p >= g_base && p - g_base < 0x20000000u
             ? static_cast<uint32_t>(p - g_base)
             : 0;
}
bool __fastcall check_immune(void *processor, void *actor, uint8_t damage_type,
                             uint8_t skip_buff, void *context) {
  const edp::Mode m = g_mode.load(std::memory_order_acquire);
  const uint64_t now = p8_now_us();
  const bool capture =
      m != edp::Mode::Off && (m == edp::Mode::Probe ? g_probe_limit.take(now)
                                                    : g_detail_limit.take(now));
  edp::Sample s;
  const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
  if (capture)
    s = edp::sample(m, reinterpret_cast<uintptr_t>(actor),
                    reinterpret_cast<uintptr_t>(context),
                    [](uintptr_t at, void *out, size_t n) noexcept {
                      return game::fast_read(at, out, n);
                    });
  // La llamada original se ejecuta una sola vez, con sus cinco argumentos
  // intactos.
  const bool result = g_hook.unsafe_call<bool>(processor, actor, damage_type,
                                               skip_buff, context);
  g_calls.fetch_add(1, std::memory_order_relaxed);
  if (result)
    g_true.fetch_add(1, std::memory_order_relaxed);
  if (capture) {
    p8p::Event e;
    e.qpc_us = now;
    e.tid = GetCurrentThreadId();
    e.flags = static_cast<uint16_t>(s.valid);
    e.v[0] = s.key;
    e.v[1] = s.controller;
    e.v[2] = rva(s.vtable);
    e.v[3] = s.type;
    e.v[4] = damage_type;
    e.v[5] = skip_buff;
    e.v[6] = s.context_flags;
    e.v[7] = result ? 1 : 0;
    e.v[8] = rva(caller);
    g_ring.push(e);
  }
  return result; // No fuerza inmunidad, HP, facción, muerte ni objetivos.
}
} // namespace
void enemy_damage_probe_start(std::string_view raw, bool host) {
  const auto m = edp::mode(raw);
  if (m == edp::Mode::Off || !host || g_hook)
    return;
  g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  std::vector<gad::Section> secs;
  uint8_t actual[sizeof kBytes] = {};
  if (!p8_exe_sections(g_base, secs) ||
      !gad::in_real_code(secs, static_cast<uint32_t>(kRva), sizeof kBytes) ||
      !game::fast_read(g_base + kRva, actual, sizeof actual) ||
      std::memcmp(actual, kBytes, sizeof actual) != 0) {
    log::error("EnemyDamageProbe: 0x209CB30 no coincide con .rsrc 2976; no se "
               "instala");
    return;
  }
  auto accept = [](const std::vector<uint8_t> &v) {
    return (v.size() == 5 || v.size() == 15) &&
           std::memcmp(v.data(), kBytes, v.size()) == 0;
  };
  auto h = game::prepare_inline(g_base + kRva,
                                reinterpret_cast<void *>(&check_immune), accept,
                                "EnemyDamageProbe", "0x209CB30 entrada");
  if (!h || !accept(h->original_bytes())) {
    log::error("EnemyDamageProbe: gancho no preparado con instrucciones "
               "completas; no se instala");
    return;
  }
  g_hook = std::move(*h);
  g_mode.store(m, std::memory_order_release);
  if (!g_hook.enable()) {
    g_mode.store(edp::Mode::Off, std::memory_order_release);
    g_hook = {};
    log::error("EnemyDamageProbe: no se pudo habilitar; apagado");
    return;
  }
  log::info("EnemyDamageProbe: {} SOLO OBSERVACION en 0x209CB30; retorno "
            "intacto; vtable/caller separan "
            "actores cliente y servidor; no equivale a daño aplicado",
            raw);
}
void enemy_damage_probe_poll() {
  if (g_mode.load(std::memory_order_acquire) == edp::Mode::Off)
    return;
  p8p::Event e;
  for (unsigned i = 0; i < 64 && g_ring.pop(e); ++i) {
    log::info("EnemyDamageProbe [inmunidad] {} T{} key=0x{:08X} "
              "controlador=0x{:08X} vt=exe+0x{:X} "
              "tipo={} damage_type={} skip_buff={} ctx4=0x{:02X} "
              "valid=0x{:02X} retorno={} caller=exe+0x{:X}",
              p8_when(e.qpc_us), e.tid, e.v[0], e.v[1], e.v[2], e.v[3], e.v[4],
              e.v[5], e.v[6], e.flags, e.v[7], e.v[8]);
  }
  const uint64_t now = p8_now_us();
  if (now - g_last_summary >= 5'000'000) {
    g_last_summary = now;
    log::info(
        "EnemyDamageProbe [resumen] llamadas={} inmunes={} "
        "sin_detalle_por_limite={} "
        "anillo_perdidos={} (no se deduce daño positivo del retorno false)",
        g_calls.load(), g_true.load(),
        g_probe_limit.refused() + g_detail_limit.refused(), g_ring.dropped());
  }
}
} // namespace cdmp::session2
