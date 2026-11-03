#include "mod/session2/npc_probe_client.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>
#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/npc_probe_client_logic.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"
#include "mod/session2/npc_activation_logic.h"

namespace cdmp::session2 {
namespace {
constexpr size_t kCapacity = 8192, kSamples = 512;
constexpr uint64_t kBudgetUs = 8000;
enum Kind : uint16_t { Load = 1, Child, Motion, Remove, Reset };
enum Phase : uint16_t { Received = 1, Handed, Captured };
std::atomic<bool> g_on{false};
bool g_guest = false;
HANDLE g_thread = nullptr, g_stop = nullptr;
uintptr_t g_base = 0;
p8p::EventRing<32768> g_events;
std::atomic<uint64_t> g_bad{0}, g_live_epoch{0};
// Sólo el worker toca la tabla; callbacks limitados a registros POD sin espera.
struct Snapshot {
    uint32_t valid = 0, controller = 0, parent = 0;
    uint64_t scene = 0, bound_scene = 0;
    uint32_t children_present = 0;
    uint16_t flags = 0;
    uint8_t state = 0, control_mode = 0, ready_c2 = 0, ready_cf = 0, stage_active = 0, wait_3d9 = 0;
    bool operator==(const Snapshot&) const = default;
};
struct Entry {
    uint32_t key = 0, controller = 0, parent = 0, generation = 1, requester = 0;
    uint16_t children_count = 0;
    std::array<uint32_t, 32> children{};
    npcp::Timeline time;
    Snapshot last;
    uint64_t req[8]{}, ack[8]{}, last_sample = 0;
    uint8_t status_lines = 0;
    uint64_t birth_queued_us = 0;
    // Revisión (volumen): [0] C>S Req del cliente, [1] S>C; actividad de la ventana de resumen.
    npcp::Track track[2];
    bool removed = false, sampled = false, family_logged = false, first_req_logged = false, road = false;
    bool win_req = false, win_ack = false;
};
std::unordered_map<uint32_t, Entry> g_book;
std::vector<uint32_t> g_order;
uint64_t g_epoch = 0, g_full = 0, g_reads_bad = 0, g_read_fail = 0, g_budget_hits = 0;
uint64_t g_handed_loads = 0, g_handed_old = 0, g_handed_other = 0, g_repeat_loads = 0, g_status_quiet = 0;
size_t g_cursor = 0;
uint64_t g_last_summary = 0;
constexpr uint16_t kOps[] = {2223,2556,2734,2405,2291,2828,2697,2991};
int op_index(uint16_t op) { for (int i = 0; i < 8; ++i) if (kOps[i] == op) return i; return -1; }
const char* side() { return g_guest ? "invitado" : "anfitrion"; }
const char* phase_name(uint16_t p) {
    return p == Received ? "recibido-tunel" : p == Handed ? "entregado-OnRecvPacket" : "capturado-cliente-host";
}
void push(p8p::Event& e, uint64_t epoch, uint64_t t, uint64_t queued_us) noexcept {
    e.v[6] = queued_us; e.v[0] = epoch; e.qpc_us = t; e.tid = GetCurrentThreadId(); g_events.push(e);
}
void packet(uint16_t op, std::span<const uint8_t> p, uint64_t epoch, uint64_t t,
            bool incoming, uint16_t phase, bool transmitted, uint64_t queued_us) noexcept {
    if (!g_on.load(std::memory_order_acquire)) return;
    if (incoming && op == 2005) {
        size_t o = 0;
        while (o < p.size()) {
            if (p.size() - o < 2) { ++g_bad; break; }
            const size_t n = fam::le16(p.data() + o);
            if (n < 5 || n > p.size() - o - 2) { ++g_bad; break; }
            npcp::Load a;
            if (npcp::load(p.subspan(o + 2, n), a)) {
                p8p::Event e; e.kind = Load; e.flags = phase;
                e.v[1] = a.key | (uint64_t{a.controller} << 32);
                e.v[2] = a.type | (uint64_t{a.parent} << 32);
                e.v[3] = a.children_count | (uint64_t{a.family_valid} << 16) | (uint64_t{a.road_fields} << 17);
                e.v[4] = a.requester | (uint64_t{a.road} << 32); e.v[5] = a.road_sub;
                push(e, epoch, t, queued_us);
                for (size_t i = 0; i < a.children_count; ++i) {
                    p8p::Event c; c.kind = Child; c.flags = phase;
                    c.v[1] = a.key; c.v[2] = a.children[i]; c.v[3] = i;
                    push(c, epoch, t, queued_us);
                }
            }
            o += n + 2;
        }
    } else if (incoming && op == 2516 && p.size() >= 6) {
        for (size_t o = 6; o + 4 <= p.size(); o += 4) {
            const uint32_t key = npcp::u32(p, o);
            if (!npcp::npc(key)) continue;
            p8p::Event e; e.kind = Remove; e.flags = phase; e.v[1] = key; e.v[2] = p[5]; push(e, epoch, t, queued_us);
        }
    } else if (incoming && op == 2174 && phase != Handed) {
        p8p::Event e; e.kind = Reset; push(e, epoch, t, queued_us);
    } else if (phase != Handed) {
        npcp::Motion m;
        if (!npcp::motion(op, p, m)) return;
        p8p::Event e; e.kind = Motion;
        e.v[1] = m.key | (uint64_t{op} << 32);
        e.v[2] = std::bit_cast<uint32_t>(m.x) | (uint64_t{std::bit_cast<uint32_t>(m.y)} << 32);
        e.v[3] = std::bit_cast<uint32_t>(m.z);
        e.v[4] = uint64_t{incoming} | (uint64_t{transmitted} << 1) | (uint64_t{m.position} << 2);
        push(e, epoch, t, queued_us);
    }
}
void wire(std::span<const uint8_t> p, uint64_t epoch, uint64_t t, bool incoming, uint16_t phase, bool sent, uint64_t queued_us = 0) noexcept {
    if (p.size() < 5) return;
    const size_t n = fam::le16(p.data() + 3);
    if (n > p.size() - 5) { ++g_bad; return; }
    packet(fam::le16(p.data()), p.subspan(5, n), epoch, t, incoming, phase, sent, queued_us ? queued_us : t);
}
template<class T> bool rd(uintptr_t p, T& v) noexcept {
    if (!p) return false;
    if (game::fast_read(p, &v, sizeof v)) return true;
    ++g_read_fail; return false;
}
uintptr_t ptr(uintptr_t p) noexcept { uintptr_t v{}; return rd(p,v) ? v : 0; }
uint32_t key_at(uintptr_t a) noexcept { uint32_t k{}; if (a) rd(a + 0x60,k); return k; }
// Réplica de ClientActorContainer::Find 0x464290, como guest_seq_route.cpp.
// Sólo lectura; nunca se llama a Find ni se toma un cerrojo del juego.
uintptr_t find_actor(uint32_t key) noexcept {
    const uintptr_t w = ptr(g_base + game::build_2976::kWorldSystemSlot);
    const uintptr_t manager = w ? ptr(w + 0x30) : 0;
    const uintptr_t table = manager ? ptr(manager + 8) : 0;
    uint32_t buckets{};
    if (!table || !rd(table + 0x88, buckets) || !buckets || buckets > (1u << 24)) return 0;
    const uintptr_t base = ptr(table + 0x98), nodes = ptr(table + 0xA0);
    if (!base || !nodes) return 0;
    const uintptr_t bucket = base + static_cast<uintptr_t>(key % buckets) * 0x100;
    uint32_t count{}; if (!rd(bucket,count) || count > 31) return 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t k{}, idx{};
        if (!rd(bucket + 8 + i * 8,k) || !rd(bucket + 12 + i * 8,idx)) return 0;
        if (k != key || !idx) continue;
        const uintptr_t node = ptr(nodes + static_cast<uintptr_t>(idx) * 8);
        uint32_t nk{}; if (!node || !rd(node + 4,nk) || nk != key) return 0;
        const uintptr_t a = ptr(node + 8);
        return key_at(a) == key ? a : 0;
    }
    return 0;
}
Snapshot snapshot(const Entry& e) noexcept {
    Snapshot s;
    const uintptr_t a = find_actor(e.key);
    if (!a) return s; // Ausente O lectura fallida: el log conserva esa distinción prudente.
    s.flags |= 1;
    const uintptr_t comps = ptr(a + 0x68);
    if (rd(a + 0x90,s.controller)) s.valid |= 1;
    if (rd(a + 0xAC,s.parent)) s.valid |= 2;
    if (rd(a + 0x5E,s.state)) s.valid |= 4;
    if (rd(a + 0xC8,s.control_mode)) s.valid |= 8;
    if (rd(a + 0xC2,s.ready_c2) && rd(a + 0xCF,s.ready_cf)) s.valid |= 16;
    // ClientAiActorComponent [[actor+68]+58]; 0x9245B0 consulta [[AI+48]+8].
    const uintptr_t ai = comps ? ptr(comps + 0x58) : 0;
    if (ai) {
        s.flags |= 2;
        uintptr_t holder{}, brain{};
        if (rd(ai + 0x48,holder)) {
            if (!holder || rd(holder + 8,brain)) s.valid |= 32;
            if (brain) s.flags |= 4;
        }
        const uintptr_t x1 = ptr(ai + 0x90);
        if (x1 && ptr(x1 + 8)) s.flags |= 8; // Handle de horario, distinto del cerebro.
    }
    const uintptr_t play = comps ? ptr(comps + 0xB0) : 0;
    if (play && ptr(play) == g_base + 0x55BEE78) {
        s.flags |= 16;
        if (rd(play + 0x2E0,s.scene)) s.valid |= 64;
        if (rd(play + 0x3D9,s.wait_3d9)) s.valid |= 128;
        uintptr_t stage{};
        if (rd(play + 0x318,stage)) {
            s.valid |= 256;
            if (stage) {
                s.flags |= 32;
                if (rd(stage + 0x178,s.stage_active)) s.valid |= 512;
                if (rd(stage + 0x30,s.bound_scene)) s.valid |= 1024;
            }
        }
    }
    if (e.parent && find_actor(e.parent)) s.flags |= 64;
    for (size_t i = 0; i < e.children_count; ++i) {
        if (e.children[i] && find_actor(e.children[i])) s.children_present |= 1u << i;
    }
    // Una dirección reciclada durante el muestreo invalida TODA la muestra.
    if (key_at(a) != e.key || ptr(a + 0x68) != comps) { ++g_reads_bad; return {}; }
    return s;
}
void clear_book(uint64_t epoch) {
    g_book.clear(); g_order.clear(); g_cursor = 0; g_epoch = epoch;
}
Entry* entry(uint32_t key) {
    const auto it = g_book.find(key);
    if (it != g_book.end()) return &it->second;
    if (g_book.size() >= kCapacity) { ++g_full; return nullptr; }
    Entry e; e.key = key;
    g_order.push_back(key);
    return &g_book.emplace(key,e).first->second;
}
void consume(const p8p::Event& ev) {
    const uint64_t epoch = ev.v[0];
    if (g_guest && epoch != g_live_epoch.load(std::memory_order_acquire)) return;
    if (g_guest && epoch != g_epoch) {
        // Los productores pueden terminar un evento antiguo después de la reconexión.
        // No se supone que las epocas nativas sean numeros consecutivos ni ordenables.

        clear_book(epoch);
    }
    if (ev.kind == Reset) { clear_book(epoch); return; }
    const uint32_t key = static_cast<uint32_t>(ev.v[1]);
    if (ev.flags == Handed) {
        // Sólo la recepción del túnel gobierna el registro. La entrega de una baja
        // antigua nunca puede retirar la nueva encarnación de la misma clave, ni
        // una alta antigua sobrescribir su controlador, familia o cronología.
        // Revisión (volumen): sin línea por entrega; la primera entrega candidata del alta queda en su
        // cronología (baja, primera Req) y los recuentos en el resumen.
        const auto it = g_book.find(key);
        Entry* current = it != g_book.end() ? &it->second : nullptr;
        const auto relation = npcp::delivery_relation(current ? current->birth_queued_us : 0,
                                                      ev.v[6], current && current->removed);
        if (ev.kind != Load) ++g_handed_other;
        else if (relation == npcp::DeliveryRelation::Candidate) { ++g_handed_loads; current->time.hand(ev.qpc_us); }
        else ++g_handed_old;
        return;
    }
    Entry* a = entry(key); if (!a) return;
    if (ev.kind == Load) {
        if (a->removed) { const uint32_t next = a->generation + 1; *a = {}; a->key = key; a->generation = next; }
        const bool first = !a->time.received;
        a->controller = static_cast<uint32_t>(ev.v[1] >> 32); a->parent = static_cast<uint32_t>(ev.v[2] >> 32);
        a->children_count = static_cast<uint16_t>(ev.v[3]);
        if (first) a->birth_queued_us = ev.v[6];
        a->time.receive(ev.qpc_us);
        a->removed = false;
        a->road = a->road || ((ev.v[3] >> 17) & 1);
        if ((ev.v[3] >> 17) & 1) a->requester = static_cast<uint32_t>(ev.v[4]);
        if (!a->children_count) a->family_logged = true;
        if (!first) { ++g_repeat_loads; return; }  // un 2005 repetido de la misma generación: sólo cuenta
        log::info("NpcProbe [{} alta] {} epoca 0x{:X} clave 0x{:08X} gen {} encolado-us {} fase {} tipo {} controlador 0x{:08X} padre 0x{:08X} hijos {} cola-familiar-heuristica {} procedencia-candidata {} solicitante 0x{:08X} carretera 0x{:X} sub 0x{:X}",
                  side(), p8_when(ev.qpc_us), epoch, key, a->generation, ev.v[6], phase_name(ev.flags), ev.v[2] & 0xFF,
                  a->controller, a->parent, a->children_count, (ev.v[3] >> 16) & 1, (ev.v[3] >> 17) & 1,
                  static_cast<uint32_t>(ev.v[4]), ev.v[4] >> 32, ev.v[5]);
    } else if (ev.kind == Child) {
        const auto i = static_cast<size_t>(ev.v[3]);
        if (i < a->children.size()) a->children[i] = static_cast<uint32_t>(ev.v[2]);
        // Una sola línea por raíz y generación, al llegar su último hijo anunciado.
        if (a->family_logged || i + 1 != a->children_count) return;
        a->family_logged = true;
        std::string list;
        for (size_t c = 0; c < std::min<size_t>(a->children_count, a->children.size()); ++c) {
            list += std::format("{}0x{:08X}", c ? "," : "", a->children[c]);
        }
        log::info("NpcProbe [{} familia] {} epoca 0x{:X} raiz 0x{:08X} gen {} hijos {} [{}] fase {}",
                  side(), p8_when(ev.qpc_us), epoch, key, a->generation, a->children_count, list, phase_name(ev.flags));
    } else if (ev.kind == Remove) {
        a->removed = true;
        log::info("NpcProbe [{} baja] {} epoca 0x{:X} clave 0x{:08X} gen {} motivo {} fase {} primera-Req-us {} alta-us {} entrega-us {} presencia-us {}",
                  side(),p8_when(ev.qpc_us),epoch,key,a->generation,ev.v[2],phase_name(ev.flags),a->time.first_req,a->time.received,
                  a->time.handed,a->time.present);
    } else if (ev.kind == Motion) {
        const auto op = static_cast<uint16_t>(ev.v[1] >> 32); const int oi = op_index(op); if (oi < 0) return;
        const bool incoming = (ev.v[4] & 1) != 0, sent = (ev.v[4] & 2) != 0, pos = (ev.v[4] & 4) != 0;
        uint64_t& count = incoming ? a->ack[oi] : a->req[oi]; ++count;
        if (!incoming && sent) a->time.request(ev.qpc_us);
        const size_t d = incoming ? 1 : 0;
        (incoming ? a->win_ack : a->win_req) = true;
        if (pos) a->track[d].add(std::bit_cast<float>(static_cast<uint32_t>(ev.v[2])), std::bit_cast<float>(static_cast<uint32_t>(ev.v[3])));
        // Por clave sólo la primera Req transmitida; el movimiento va en los resúmenes de 10 s.
        if (incoming || !sent || a->first_req_logged) return;
        a->first_req_logged = true;
        log::info("NpcProbe [{} primera-Req] {} epoca 0x{:X} clave 0x{:08X} gen {} op {} posicion-valida {} xyz ({:.3f},{:.3f},{:.3f}) desde-alta-us {} desde-entrega-us {} (Req no implica aceptacion)",
                  side(),p8_when(ev.qpc_us),epoch,key,a->generation,op,pos,
                  std::bit_cast<float>(static_cast<uint32_t>(ev.v[2])),std::bit_cast<float>(static_cast<uint32_t>(ev.v[2] >> 32)),std::bit_cast<float>(static_cast<uint32_t>(ev.v[3])),
                  a->time.received && ev.qpc_us >= a->time.received ? ev.qpc_us - a->time.received : 0,
                  a->time.handed && ev.qpc_us >= a->time.handed ? ev.qpc_us - a->time.handed : 0);
    }
}
// Hitos de un estado: sin los bytes que oscilan (5E, C8, C2, CF) ni la máscara de lecturas.
bool milestone_changed(const Snapshot& a, const Snapshot& b) {
    return a.flags != b.flags || a.children_present != b.children_present || a.stage_active != b.stage_active ||
           a.wait_3d9 != b.wait_3d9 || a.controller != b.controller || a.parent != b.parent ||
           a.scene != b.scene || a.bound_scene != b.bound_scene;
}
// Resumen de 10 s: actividad y desplazamiento de la ventana; viajeros (procedencia candidata) con su distancia.
void motion_summary(uint64_t now) {
    size_t req = 0, ack = 0, moved = 0, still_with_req = 0, travellers = 0, travellers_moved = 0;
    npcp::RoadGroups<6> groups;  // S38: por (solicitante, controlador)
    std::string listed;
    size_t shown = 0;
    for (auto& [key, a] : g_book) {
        if (a.removed) continue;
        const float d = std::max(a.track[0].moved(), a.track[1].moved());
        const bool active = a.win_req || a.win_ack;
        if (a.win_req) ++req;
        if (a.win_ack) ++ack;
        if (d >= npcp::kMovedMeters) ++moved;
        else if (a.win_req) ++still_with_req;
        if (a.road && active) {
            ++travellers;
            if (d >= npcp::kMovedMeters) ++travellers_moved;
            groups.add(a.requester, a.controller, d >= npcp::kMovedMeters, a.win_req);
            if (shown < npcp::kTravellersPerLine) {
                listed += std::format(" 0x{:08X}:{:.1f}m{}", key, d, a.win_req ? "/Req" : "");
                ++shown;
            }
        }
        a.win_req = a.win_ack = false;
        a.track[0].restart(); a.track[1].restart();
    }
    log::info("NpcProbe [{} movimiento-resumen] {} claves con Req {} con S>C {} desplazadas>={}m {} quietas-con-Req {} | viajeros activos {} desplazados {}:{}",
              side(),p8_when(now),req,ack,npcp::kMovedMeters,moved,still_with_req,travellers,travellers_moved,
              listed.empty() ? std::string(" ninguno") : listed);
    // S38 fase C: separados por quién los pidió y quién los controla (los «desplazados» de arriba mezclan ambos).
    if (travellers) {
        std::string by;
        for (size_t i = 0; i < groups.n; ++i) {
            const auto& g = groups.g[i];
            by += std::format(" | solicitante 0x{:08X} controlador 0x{:08X}: activos {} desplazados {} con-Req-propia {}",
                              g.requester, g.controller, g.active, g.moved, g.with_req);
        }
        if (groups.other_active) by += std::format(" | otros: activos {} desplazados {}", groups.other_active, groups.other_moved);
        log::info("NpcProbe [{} viajeros-por-solicitante] {}{}", side(), p8_when(now), by);
    }
}
void sample_some(uint64_t now) {
    const uint64_t started = p8_now_us();
    size_t sampled = 0;
    for (size_t visited = 0; visited < g_order.size() && sampled < kSamples; ++visited) {
        if (p8_now_us() - started >= kBudgetUs) { ++g_budget_hits; break; }
        if (g_cursor >= g_order.size()) g_cursor = 0;
        auto& a = g_book.at(g_order[g_cursor++]);
        if (a.removed || !a.time.received || (a.last_sample && now - a.last_sample < (a.time.first_req ? 2'000'000 : 250'000))) continue;
        a.last_sample = now; ++sampled;
        const Snapshot s = snapshot(a);
        if (s.flags & 1) a.time.see(now);
        if (!a.sampled || s != a.last) {
            const bool first = !a.sampled, milestone = a.sampled && milestone_changed(s, a.last);
            // Interesa seguirla: familia (pastor/ovejas), viajero o aún sin primera Req (¿cerebro? ¿horario?).
            const bool interesting = a.children_count || a.parent || a.road || !a.time.first_req;
            a.sampled = true; a.last = s;
            if (!npcp::status_line_wanted(first, milestone, interesting, a.status_lines)) { ++g_status_quiet; continue; }
            ++a.status_lines;
            {
                log::info("NpcProbe [{} estado] {} epoca 0x{:X} clave 0x{:08X} gen {} presentes actor {} padre {} hijos-mascara 0x{:08X}/{} AI {} cerebro {} handle-horario {} playdata {} stage {} escena 0x{:X}/0x{:X} stage178 {} espera3D9 {} actor5E {} C8 {} C2 {} CF {} controlador-memoria 0x{:08X} padre-memoria 0x{:08X} lecturas-validas 0x{:X} (0 presente: ausente o lectura fallida; muestra no atomica)",
                          side(),p8_when(now),g_epoch,a.key,a.generation,bool(s.flags & 1),bool(s.flags & 64),s.children_present,a.children_count,
                          bool(s.flags & 2),bool(s.flags & 4),bool(s.flags & 8),bool(s.flags & 16),bool(s.flags & 32),s.scene,s.bound_scene,
                          s.stage_active,s.wait_3d9,s.state,s.control_mode,s.ready_c2,s.ready_cf,s.controller,s.parent,s.valid);
            }
        }
    }
}
DWORD WINAPI worker(LPVOID) {
    try {
        g_book.reserve(kCapacity); g_order.reserve(kCapacity);
        while (WaitForSingleObject(g_stop,250) == WAIT_TIMEOUT) {
            p8p::Event ev; size_t n = 0;
            while (n++ < 8192 && g_events.pop(ev)) consume(ev);
            const uint64_t now = p8_now_us(); sample_some(now);
            if (now >= g_last_summary + npcp::kSummaryUs) {
                g_last_summary = now;
                log::info("NpcProbe [{} resumen] {} claves {} tabla-llena {} eventos-perdidos {} paquetes-malformados {} identidad-inestable {} lecturas-fallidas {} presupuesto-agotado {} | entregas: altas {} antiguas {} otras {} | altas repetidas {} estados sin linea {}",
                          side(),p8_when(now),g_book.size(),g_full,g_events.dropped(),g_bad.load(),g_reads_bad,g_read_fail,g_budget_hits,
                          g_handed_loads,g_handed_old,g_handed_other,g_repeat_loads,g_status_quiet);
                motion_summary(now);
            }
        }
    } catch (...) { log::error("NpcProbe: excepcion en el observador; se detiene sin modificar el juego"); }
    g_on.store(false);
    log::info("NpcProbe: observador cliente detenido");
    return 0;
}
}
void npc_probe_client_start(std::string_view activation, std::string_view family, std::string_view road,
                            bool guest, void* stop_event) {
    // S36: npc_family_control=1 es del anfitrión; aquí (cliente) cualquier valor distinto de 0 sólo observa.
    const auto a = npcact::parse_mode(activation,true), f = npcact::parse_mode(family,true),
               r = npcact::parse_mode(road,true);
    if (a.bad || f.bad || r.bad)
        log::warn("NpcProbe: valores no validos tratados como 0 (activacion {}, familia {}, viajeros {}); sintaxis literal 0/probe/1 (familia y viajeros 1 sólo actúan en el anfitrión; aquí observan)",
                  a.bad,f.bad,r.bad);
    const bool enabled = a.mode != npcact::Mode::Off || f.mode != npcact::Mode::Off || r.mode != npcact::Mode::Off;
    if (!enabled || g_thread) return;
    if (!stop_event) { log::warn("NpcProbe: falta evento de parada, observador apagado"); return; }
    g_guest = guest; g_stop = static_cast<HANDLE>(stop_event);
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint16_t mz{}; uint32_t pe_at{}, pe{}, stamp{};
    if (!rd(g_base,mz) || mz != 0x5A4D || !rd(g_base + 0x3C,pe_at) || pe_at > 0x100000 ||
        !rd(g_base + pe_at,pe) || pe != 0x4550 || !rd(g_base + pe_at + 8,stamp) || stamp != game::build_2976::kPeTimestamp) {
        log::warn("NpcProbe: build distinta de 2976 o cabecera ilegible; no se leen actores"); return;
    }
    g_on.store(true);
    g_thread = CreateThread(nullptr,0,worker,nullptr,0,nullptr);
    if (!g_thread) { g_on.store(false); log::error("NpcProbe: no se pudo crear el hilo de observacion"); return; }
    log::info("NpcProbe: cliente {} sólo lectura, sin hooks nuevos; maximo {} claves, {} muestras/ciclo, presupuesto {} us; entrega no equivale a aplicacion, cerebro no equivale a accion aceptada",
              side(),kCapacity,kSamples,kBudgetUs);
}
void npc_probe_client_stop() noexcept {
    g_on.store(false);
    if (g_stop) SetEvent(g_stop);
    if (g_thread) { WaitForSingleObject(g_thread,INFINITE); CloseHandle(g_thread); g_thread = nullptr; }
}
void npc_probe_client_sniff(const uint8_t* p, size_t n, uint64_t t, bool incoming) noexcept {
    if (!g_on.load(std::memory_order_acquire) || g_guest) return;
    wire({p,n},0,t,incoming,Captured,true);
}
void npc_probe_client_received(uint16_t op, std::span<const uint8_t> p, uint64_t epoch, uint64_t queued_us) noexcept {
    if (!g_on.load(std::memory_order_acquire) || !g_guest) return;
    g_live_epoch.store(epoch, std::memory_order_release);
    packet(op,p,epoch,p8_now_us(),true,Received,true,queued_us);
}
void npc_probe_client_delivered(std::span<const uint8_t> p, uint64_t epoch, uint64_t queued_us) noexcept {
    if (!g_on.load(std::memory_order_acquire) || !g_guest) return;
    wire(p,epoch,p8_now_us(),true,Handed,true,queued_us);
}
void npc_probe_client_out(std::span<const uint8_t> p, uint64_t epoch, bool transmitted) noexcept {
    if (!g_on.load(std::memory_order_acquire) || !g_guest) return;
    wire(p,epoch,p8_now_us(),false,Received,transmitted);
}
} // namespace cdmp::session2
