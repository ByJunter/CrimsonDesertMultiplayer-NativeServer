#pragma once
// S33: lectores puros de sondas, build 2976. Nunca deciden ni modifican el juego.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include "mod/session2/family_order_logic.h"
namespace cdmp::session2::npcp {
inline uint32_t u32(std::span<const uint8_t> p, size_t o) {
    uint32_t v{};
    if (o <= p.size() && p.size() - o >= 4) std::memcpy(&v, p.data() + o, 4);
    return v;
}
inline bool npc(uint32_t k) { return (k >> 28) == 0xB; }
struct Load {
    uint32_t key = 0, controller = 0, parent = 0, requester = 0, road = 0, road_sub = 0;
    uint8_t type = 0;
    uint16_t children_count = 0;
    bool family_valid = false, road_fields = false;
    std::array<uint32_t, 32> children{};
};
inline bool load(std::span<const uint8_t> r, Load& out) {
    out = {};
    if (r.size() < 0x16) return false;
    out.key = u32(r, 1); out.controller = u32(r, 0x12); out.type = r[0];
    if (!npc(out.key)) return false; // Selección de sonda, nunca filtro del paquete.
    out.family_valid = fam::record_family(r, out.parent, out.children_count);
    if (out.family_valid) {
        const size_t begin = r.size() - 4 * out.children_count;
        for (size_t i = 0; i < out.children_count; ++i) out.children[i] = u32(r, begin + 4 * i);
    }
    // Procedencia candidata S28/S33 tras nombre UTF-16; requiere correlación con 2550.
    if (r.size() >= 34) {
        const uint32_t n = u32(r, 30);
        if (n <= (r.size() - 34) / 2) {
            const size_t end = 34 + static_cast<size_t>(n) * 2;
            if (r.size() - end >= 100) {
                out.requester = u32(r, end + 88); out.road_sub = u32(r, end + 92); out.road = u32(r, end + 96);
                out.road_fields = (out.requester >> 28) == 0xA && out.road != 0;
            }
        }
    }
    return true;
}
struct Motion { uint32_t key = 0; float x = 0, y = 0, z = 0; bool position = false; };
inline bool motion(uint16_t op, std::span<const uint8_t> p, Motion& out) {
    out = {};
    if (p.size() < 4 || !npc(u32(p, 0))) return false;
    if (op != 2223 && op != 2556 && op != 2734 && op != 2405 && op != 2291 && op != 2828 && op != 2697 && op != 2991) return false;
    out.key = u32(p, 0);
    const size_t off = op == 2223 ? 0x8E : op == 2991 ? 0x8F : 4;
    if ((op == 2223 || op == 2991 || op == 2556 || op == 2697) && p.size() >= off + 12) {
        std::memcpy(&out.x, p.data() + off, 4); std::memcpy(&out.y, p.data() + off + 4, 4); std::memcpy(&out.z, p.data() + off + 8, 4);
        out.position = std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
    }
    return true;
}
// La entrega puede llegar después de una baja y de la reutilización de la clave.
// El instante de encolado correlaciona ambos extremos de la cola local, no confirma
// una generación: dos paquetes podrían compartir el mismo microsegundo.
enum class DeliveryRelation : uint8_t { Unknown, Historical, Candidate };
inline DeliveryRelation delivery_relation(uint64_t birth_queued_us, uint64_t packet_queued_us,
                                          bool removed) noexcept {
    if (!birth_queued_us || !packet_queued_us) return DeliveryRelation::Unknown;
    if (removed || packet_queued_us < birth_queued_us) return DeliveryRelation::Historical;
    return DeliveryRelation::Candidate;
}
// Revisión S33, volumen del log (objetivo: muy por debajo de 10 líneas/s estable con ~300 NPC).
// Por clave: alta, familia (una línea con sus hijos), primera Req, primer estado y baja. Los cambios de
// estado posteriores sólo si cambia un hito (presencia, cerebro, horario, stage, vínculos, controlador)
// y la clave interesa (familia, viajero o aún sin Req), hasta kMaxStatusLines. El resto, resúmenes.
inline constexpr uint8_t kMaxStatusLines = 6;
inline constexpr uint64_t kSummaryUs = 10'000'000;
inline constexpr float kMovedMeters = 1.0f;
inline constexpr size_t kTravellersPerLine = 8;
inline bool status_line_wanted(bool first, bool milestone_changed, bool interesting, uint8_t lines) noexcept {
    if (lines >= kMaxStatusLines) return false;
    return first || (milestone_changed && interesting);
}
// Desplazamiento X/Z dentro de una ventana de resumen (sin 2556 no basta: 2223 también lleva posición).
struct Track {
    float x0 = 0, z0 = 0, x = 0, z = 0;
    bool has = false;
    void add(float px, float pz) noexcept {
        if (!has) { x0 = px; z0 = pz; has = true; }
        x = px; z = pz;
    }
    float moved() const noexcept { return has ? std::sqrt((x - x0) * (x - x0) + (z - z0) * (z - z0)) : 0.f; }
    void restart() noexcept { x0 = x; z0 = z; }  // la ventana siguiente parte de la última posición
};
// Entregado no equivale a presente, ni a acción aceptada.
struct Timeline {
    uint64_t received = 0, handed = 0, present = 0, first_req = 0;
    void receive(uint64_t t) { if (!received || t < received) received = t; }
    void hand(uint64_t t) { if (!handed || t < handed) handed = t; }
    void see(uint64_t t) { if (!present || t < present) present = t; }
    void request(uint64_t t) { if (!first_req || t < first_req) first_req = t; }
};
// S38 fase C (docs/research/s25/s38-fase-c.md; 3-viajeros.md §2.2): el resumen «viajeros activos/desplazados» mezclaba
// los viajeros que pidió el ANFITRIÓN (cerca de su personaje: se mueven) con los del invitado. Ahora por (solicitante,
// controlador): con dos jugadores se separan solos los propios / ajenos y quién los controla. Un grupo por par hasta N;
// el resto en «otros».
struct RoadGroup {
    uint32_t requester = 0, controller = 0;
    uint32_t active = 0, moved = 0, with_req = 0;
};
template <size_t N>
struct RoadGroups {
    std::array<RoadGroup, N> g{};
    size_t n = 0;
    uint32_t other_active = 0, other_moved = 0;
    void add(uint32_t requester, uint32_t controller, bool moved, bool own_req) noexcept {
        RoadGroup* at = nullptr;
        for (size_t i = 0; i < n; ++i) {
            if (g[i].requester == requester && g[i].controller == controller) { at = &g[i]; break; }
        }
        if (!at && n < N) {
            at = &g[n++];
            at->requester = requester;
            at->controller = controller;
        }
        if (!at) {
            ++other_active;
            if (moved) ++other_moved;
            return;
        }
        ++at->active;
        if (moved) ++at->moved;
        if (own_req) ++at->with_req;
    }
};
} // namespace cdmp::session2::npcp
