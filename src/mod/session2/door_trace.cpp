#include "mod/session2/door_trace.h"
#include "mod/game/hook_prepare.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/docked_gimmicks_logic.h"
#include "mod/session2/door_trace_logic.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/p8_probes.h"
#include "mod/session2/server.h"
#include <array>
#include <atomic>
#include <cstring>
#include <safetyhook.hpp>
#include <vector>
#include <windows.h>
namespace cdmp::session2 {
namespace {
namespace dt = doortrace;
constexpr uint32_t kRva = 0x282DC80, kEnd = 0x282E694;
constexpr uint8_t kHead[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x4c, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89,
    0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x8d, 0xac, 0x24, 0xe0, 0xf7, 0xff, 0xff, 0x48, 0x81,
    0xec, 0x20, 0x09, 0,    0,    0x45, 0x8b, 0xf1, 0x4d, 0x8b, 0xe8};
std::atomic<uint8_t> g_mode{0};
bool g_guest = false;
uintptr_t g_base = 0;
SafetyHookInline g_hook;
bool g_installed = false;
std::atomic<uint64_t> g_counts[static_cast<size_t>(DoorTraceStage::Count)]
                              [static_cast<size_t>(dt::Kind::Count)]{};
std::atomic<uint64_t> g_results[static_cast<size_t>(dt::Result::Count)]{};
std::atomic<uint64_t> g_faults{0}, g_undecoded{0}, g_lost{0}, g_capped{0};
// Producers never wait for this lock: a contended observation is lost and
// counted, never delays native code.
struct Event {
  uint64_t qpc = 0;
  uint32_t epoch = 0, subject = 0, controller = 0, field_owner = 0, error = 0,
           resource = 0, items = 0;
  uint8_t stage = 0, kind = 0, result = 0, native = 0, valid = 0;
};
SRWLOCK g_lock = SRWLOCK_INIT;
std::array<Event, 256> g_ring{};
size_t g_read = 0, g_write = 0, g_size = 0;
void push(const Event &e) noexcept {
  if (!TryAcquireSRWLockExclusive(&g_lock)) {
    g_lost.fetch_add(1);
    return;
  }
  if (g_size == g_ring.size()) {
    g_lost.fetch_add(1);
  } else {
    g_ring[g_write] = e;
    g_write = (g_write + 1) % g_ring.size();
    ++g_size;
  }
  ReleaseSRWLockExclusive(&g_lock);
}
uintptr_t native_detour(uintptr_t manager, uintptr_t err, uintptr_t subject,
                        uint32_t resource, uintptr_t scenes) {
  Event e;
  e.native = 1;
  e.resource = resource;
  e.epoch = server_epoch();
  e.qpc = p8_now_us();
  bool selected = false;
  uintptr_t list = 0;
  uint32_t count = 0;
  if (g_mode.load(std::memory_order_relaxed) && resource == dt::kResource &&
      game::fast_read(scenes, &list, sizeof list) &&
      game::fast_read(scenes + 8, &count, sizeof count)) {
    e.items = count;
    // Observation budget only; never reject a native request with more scenes.
    const uint32_t n = count < 32 ? count : 32;
    if (count > n)
      g_capped.fetch_add(1);
    for (uint32_t i = 0; i < n; ++i) {
      std::array<uint8_t, 16> id{};
      if (list > UINTPTR_MAX - size_t(i) * 16 ||
          !game::fast_read(list + size_t(i) * 16, id.data(), id.size())) {
        g_faults.fetch_add(1);
        break;
      }
      if (dt::is_scene(id)) {
        selected = true;
        break;
      }
    }
    if (selected) {
      if (game::fast_read(subject + 0x60, &e.subject, 4))
        e.valid |= 1;
      else
        g_faults.fetch_add(1);
      if (game::fast_read(subject + 0x90, &e.controller, 4))
        e.valid |= 2;
      else
        g_faults.fetch_add(1);
      uintptr_t field = 0;
      if (game::fast_read(manager + 0x210, &field, sizeof field)) {
        e.valid |= 4;
        if (field && game::fast_read(field + 0x180, &e.field_owner, 4))
          e.valid |= 8;
        else
          g_faults.fetch_add(1);
      } else
        g_faults.fetch_add(1);
    }
  }
  // Original is called exactly once with the same five arguments, result and
  // native errors unchanged.
  const uintptr_t result =
      g_hook.unsafe_call<uintptr_t>(manager, err, subject, resource, scenes);
  if (selected) {
    const bool readable = game::fast_read(err, &e.error, 4);
    std::array<uint32_t, 4> constants{};
    bool valid = true;
    constexpr uintptr_t addresses[] = {0x6CF7018, 0x6CF701C, 0x6CF7024,
                                       0x6CF7028};
    for (size_t i = 0; i < 4; ++i)
      valid = game::fast_read(g_base + addresses[i], &constants[i], 4) && valid;
    if (readable)
      e.valid |= 16;
    else
      g_faults.fetch_add(1);
    if (valid)
      e.valid |= 32;
    else
      g_faults.fetch_add(1);
    const auto kind = dt::classify(readable, e.error, constants, valid);
    e.result = static_cast<uint8_t>(kind);
    g_results[e.result].fetch_add(1, std::memory_order_relaxed);
    push(e);
  }
  return result;
}
bool verified_entry(uintptr_t base) {
  if (base != 0x140000000)
    return false;
  IMAGE_DOS_HEADER dos{};
  IMAGE_NT_HEADERS64 nt{};
  if (!game::safe_read(base, &dos, sizeof dos) ||
      dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
      dos.e_lfanew > 0x100000)
    return false;
  if (!game::safe_read(base + static_cast<uint32_t>(dos.e_lfanew), &nt,
                       sizeof nt) ||
      nt.Signature != IMAGE_NT_SIGNATURE ||
      nt.FileHeader.TimeDateStamp != 0x6AB28F00)
    return false;
  std::vector<gad::Section> secs;
  if (!p8_exe_sections(base, secs) ||
      !gad::in_real_code(secs, kRva, sizeof kHead))
    return false;
  uint8_t head[sizeof kHead]{};
  if (!game::safe_read(base + kRva, head, sizeof head) ||
      std::memcmp(head, kHead, sizeof head))
    return false;
  const auto pd =
      nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
  if (!pd.VirtualAddress || !pd.Size || pd.Size % sizeof(RUNTIME_FUNCTION) ||
      pd.Size > 0x1000000)
    return false;
  std::vector<RUNTIME_FUNCTION> entries(pd.Size / sizeof(RUNTIME_FUNCTION));
  if (!game::safe_read(base + pd.VirtualAddress, entries.data(), pd.Size))
    return false;
  for (const auto &r : entries)
    if (r.BeginAddress == kRva)
      return r.EndAddress == kEnd;
  return false;
}
const char *stage_name(uint8_t s) {
  constexpr const char *names[] = {
      "host-cliente-Req",        "host-cliente-Ack",  "invitado-envio",
      "invitado-entrega",        "servidor-host-Req", "servidor-invitado-Req",
      "servidor-invitado-salida"};
  return s < std::size(names) ? names[s] : "?";
}
} // namespace
void door_trace_start(const std::string &raw, bool guest) {
  const auto p = dgl::parse_mode(raw);
  g_guest = guest;
  g_mode.store(static_cast<uint8_t>(p.mode));
  if (p.mode == dgl::Mode::Off)
    return;
  log::info("DoorTrace ON ({}, door_trace={}): SOLO OBSERVACION, no es "
            "arreglo; candidato recurso DE7B52AA escena "
            "4fb9eeab670f74563c343d640c000000. probe: totales/resultados; 1: "
            "detalles acotados. No se infiere apertura de un Ack.",
            guest ? "invitado" : "anfitrion", dgl::mode_name(p.mode));
}
bool door_trace_install(uintptr_t base) {
  if (!g_mode.load() || g_guest)
    return true;
  if (g_hook)
    return g_installed;
  if (!verified_entry(base)) {
    log::error("DoorTrace REFUSED: 0x282DC80 no coincide con build2976/entrada "
               "pdata/firma .rsrc; queda solo traza de paquetes");
    return false;
  }
  g_base = base;
  const auto accept = [](const std::vector<uint8_t> &b) {
    return dt::verified_patch(b);
  };
  auto h = game::prepare_inline(
      base + kRva, reinterpret_cast<void *>(&native_detour), accept,
      "DoorTrace", "resultado SpawnOnDemand entrada 0x282DC80");
  if (!h || !accept(h->original_bytes())) {
    log::error("DoorTrace REFUSED: no se pudo preparar gancho nativo con "
               "exactamente 5 bytes verificados; solo paquetes");
    return false;
  }
  g_hook = std::move(*h);
  if (!g_hook.enable()) {
    log::error(
        "DoorTrace REFUSED: no se pudo habilitar gancho nativo; solo paquetes");
    return false;
  }
  g_installed = true;
  log::info(
      "DoorTrace: entrada 0x282DC80, parche5B 48 89 5C 24 08; observa "
      "resultado original, no escribe estado ni llama creacion adicional");
  return true;
}
void door_trace_packet(DoorTraceStage stage, std::span<const uint8_t> frame,
                       uint64_t t_us) noexcept {
  const uint8_t mode = g_mode.load(std::memory_order_relaxed);
  const size_t s = static_cast<size_t>(stage);
  if (!dt::observe_tap(g_guest, stage == DoorTraceStage::HostClientReq ||
                                    stage == DoorTraceStage::HostClientAck))
    return;
  if (!mode || s >= static_cast<size_t>(DoorTraceStage::Count) ||
      frame.size() < 5 || !dt::relevant(dt::u16(frame.data())))
    return;
  const auto p = dt::frame(frame);
  if (!p.valid) {
    g_undecoded.fetch_add(1);
    return;
  }
  if (!p.matched)
    return;
  g_counts[s][static_cast<size_t>(p.kind)].fetch_add(1,
                                                     std::memory_order_relaxed);
  if (mode < static_cast<uint8_t>(dgl::Mode::On))
    return;
  Event e;
  e.qpc = t_us ? t_us : p8_now_us();
  e.stage = static_cast<uint8_t>(stage);
  e.kind = static_cast<uint8_t>(p.kind);
  e.subject = p.subject;
  e.resource = p.resource;
  e.items = p.items;
  e.epoch = server_epoch();
  push(e);
}
void door_trace_ingress(uint16_t op, uintptr_t packet, uint16_t size,
                        bool from_user2) noexcept {
  if (!g_mode.load(std::memory_order_relaxed) || op != 2928 || size < 14)
    return;
  // Full native packet copied under SEH. Large batches remain native and are
  // counted as unmeasured.
  if (size > 1024) {
    g_capped.fetch_add(1);
    return;
  }
  std::array<uint8_t, 1024> copy;
  if (!game::fast_read(packet, copy.data(), size)) {
    g_faults.fetch_add(1);
    return;
  }
  door_trace_packet(from_user2 ? DoorTraceStage::ServerGuestReq
                               : DoorTraceStage::ServerHostReq,
                    std::span(copy.data(), size));
}
void door_trace_flush() {
  if (!g_mode.load())
    return;
  // One consumer; formatting never occurs while the queue lock is held.
  static uint64_t next = 0, details = 0;
  Event local[32]{};
  size_t n = 0;
  if (TryAcquireSRWLockExclusive(&g_lock)) {
    while (n < std::size(local) && g_size) {
      local[n++] = g_ring[g_read];
      g_read = (g_read + 1) % g_ring.size();
      --g_size;
    }
    ReleaseSRWLockExclusive(&g_lock);
  }
  for (size_t i = 0; i < n; ++i) {
    const Event &e = local[i];
    if (details++ >= 2000) {
      g_capped.fetch_add(1);
      continue;
    }
    if (e.native)
      log::info(
          "DoorTrace [nativo] {} epoca {} sujeto 0x{:08X} controlador 0x{:08X} "
          "dueno-campo 0x{:08X} recurso 0x{:08X} escenas {} resultado 0x{:08X} "
          "{} valido 0x{:02X} [1=sujeto 2=controlador 4=campo 8=dueno 10=error "
          "20=constantes; ausente=NO MEDIDO] (0x282DC80; no modificado)",
          p8_when(e.qpc), e.epoch, e.subject, e.controller, e.field_owner,
          e.resource, e.items, e.error,
          dt::result_name(static_cast<dt::Result>(e.result)), e.valid);
    else
      log::info(
          "DoorTrace [paquete] {} epoca {} {} clase {} sujeto 0x{:08X} recurso "
          "0x{:08X} escenas {} (candidata UUID; no prueba apertura)",
          p8_when(e.qpc), e.epoch, stage_name(e.stage), e.kind, e.subject,
          e.resource, e.items);
  }
  const uint64_t now = p8_now_us();
  if (now < next)
    return;
  next = now + 10000000;
  for (size_t s = 0; s < static_cast<size_t>(DoorTraceStage::Count); ++s) {
    uint64_t c[4]{};
    for (size_t k = 0; k < 4; ++k)
      c[k] = g_counts[s][k].load();
    if (c[0] + c[1] + c[2] + c[3])
      log::info("DoorTrace [resumen] {} candidata: Req2928 {} Ack2145 {} "
                "Nak2953 {} Sync2665 {}",
                stage_name(static_cast<uint8_t>(s)), c[0], c[1], c[2], c[3]);
  }
  log::info("DoorTrace [resultado resumen] exito {} recurso-sin-nombre {} "
            "recurso-no-listo {} sin-candidatos {} creacion-fallida {} otro {} "
            "ilegible {} | perdidos {} lecturas-fallidas {} no-decodificados "
            "{} presupuesto {}",
            g_results[0].load(), g_results[1].load(), g_results[2].load(),
            g_results[3].load(), g_results[4].load(), g_results[5].load(),
            g_results[6].load(), g_lost.load(), g_faults.load(),
            g_undecoded.load(), g_capped.load());
}
} // namespace cdmp::session2
