#include "mod/session2/npc_action_probe.h"
#include <safetyhook.hpp>
#include <algorithm>
#include <atomic>
#include <climits>
#include <intrin.h>
#include <utility>
#include <vector>
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/npc_activation_logic.h"
#include "mod/session2/offsets_s2.h"
#include "mod/session2/user2_stages.h"

namespace cdmp::session2 {
namespace {
std::atomic<bool> g_wanted{false};
uintptr_t g_base = 0;  // fijado antes de habilitar los hooks
SafetyHookInline g_action, g_node;
std::atomic<uint64_t> g_actions{0}, g_applied{0}, g_nodes{0}, g_vetoes{0}, g_unreadable{0};
// AOB únicas en el PE 2976. Ambas son entradas .pdata de .rsrc. Primer parche: un MOV de 5 bytes.
constexpr uint8_t kActionBytes[] = {0x48,0x89,0x5C,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
  0x48,0x8D,0xAC,0x24,0x50,0xFF,0xFF,0xFF,0x48,0x81,0xEC,0xB0,0x01,0x00,0x00,0x4D,0x8B,0xE9,0x4D,0x8B,0xE0,0x48,0x8B,0xF2};
constexpr uint8_t kNodeBytes[] = {0x48,0x89,0x5C,0x24,0x08,0x4C,0x89,0x4C,0x24,0x20,0x4C,0x89,0x44,0x24,0x18,0x55,
  0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24,0x80,0xE6};
// La pila TLS de llamadas en curso (npcact::PendingCall: reentrada anidada, H1b marca sólo la más interna).
thread_local npcact::PendingCall* t_pending = nullptr;
std::atomic<uint64_t> g_stale_tls{0};
// Revisión S35 (hallazgo 5): las lecturas del detour son las plantillas de npc_activation_logic.h (probadas con un
// marco simulado), con la política de lectura protegida del juego.
struct FastRead {
    bool operator()(uintptr_t at, void* out, size_t size) const noexcept { return game::fast_read(at, out, size); }
};
// Revisión S35 (hallazgo 14, previo a S35): con /EHsc una excepción SEH del juego que cruce este marco no ejecuta
// destructores C++, y t_pending quedaría apuntando a un marco muerto (H1b escribiría allí). El original se llama
// dentro de __try/__finally: el __finally restaura la pila TLS durante el desenrollado SIN tragarse la excepción
// (sigue su camino hasta los manejadores del juego). Sin objetos con destructor en esta función (C2712).
int* call_original(npcact::PendingCall* current, uintptr_t cc, int* result, uintptr_t layers, uintptr_t context,
                   uintptr_t block, uintptr_t extra) noexcept {
    int* returned = nullptr;
    t_pending = current;
    __try {
        returned = g_action.unsafe_call<int*>(cc, result, layers, context, block, extra);
    } __finally {
        t_pending = current->previous;
    }
    return returned;
}
// 0x2A78CA0: los seis argumentos se conservan. Ni retorno externo ni resultado 0 bastan:
// sólo H1b, alcanzado después de OnServer con éxito, marca applied para esta llamada TLS. La
// rama payload+0x281 (acción ficticia del secuenciador, 0x2a78ca0.c:45) se acredita aparte.
// Una clave sin registro sólo cuenta: sin más lecturas.
// S35 (AUDITORIA.md §4, INFORME-A4): `context` (param4) NO es la sesión. El nativo llama a vf+0x170(context) y
// compara la clave devuelta con actor+0x90 (0x2A78CF2 / 0x2A78CF8 / 0x2A78CFE, build 2976). El mod nunca llama a
// esa vtable: actor+0x90 se lee antes y después como controlador ESPERADO (npcact::classify). La lectura S34
// (context+0x90 -> +0x60, el «emisor» que salía 0 en 843/843 líneas) se conserva sólo para npc_action_evidence=0 /
// probe y para la línea (emisor_s34); es una lectura protegida de un objeto que no es una sesión.
int* __fastcall action_detour(uintptr_t cc, int* result, uintptr_t layers, uintptr_t context,
                             uintptr_t block, uintptr_t extra) noexcept {
    npcact::PendingCall current{};
    // Defensa adicional: un marco vivo exterior está siempre por ENCIMA (direcciones mayores) del nuestro; un t_pending
    // en esta dirección o por debajo sólo puede ser un marco muerto (una excepción que no pasó por el __finally).
    if (t_pending && reinterpret_cast<uintptr_t>(t_pending) <= reinterpret_cast<uintptr_t>(&current)) {
        t_pending = nullptr;
        g_stale_tls.fetch_add(1, std::memory_order_relaxed);
    }
    const FastRead rd;
    npcact::ActionEntry entry = npcact::read_action_key(rd, cc);
    const auto token = user2_stages_npc_birth_token(entry.key);
    if (token) npcact::read_action_entry(rd, cc, context, block, entry);
    current.component = cc;
    current.key = entry.key;
    current.previous = t_pending;
    g_actions.fetch_add(1, std::memory_order_relaxed);
    int* returned = call_original(&current, cc, result, layers, context, block, extra);
    if (!token) return returned;
    // Lecturas posteriores revalidadas: un resultado jamás pertenece a una clave reutilizada.
    const npcact::ActionExit exit = npcact::read_action_exit(rd, cc, entry, reinterpret_cast<uintptr_t>(returned));
    if (exit.same) {
        user2_stages_npc_action_result(entry.key, entry.s34_sender, entry.ctl, exit.ctl, current.applied, exit.internal,
                                       entry.flag, entry.restore, exit.restore, token);
    } else {
        g_unreadable.fetch_add(1, std::memory_order_relaxed);
    }
    return returned;
}
// 0x281CEE0 construye el descriptor (no el actor); sus ocho callers comprueban false.
// Todos pasan salida inicializada a null y liberan lo ya construido en sus rutas de fallo.
// El veto sólo devuelve false, sin escribir estructuras del juego ni cambiar otros nodos.
// Revisión: user2_stages decide con el dueño de la stage (+0x50) y el retorno del CALL: en un
// grupo nativo (0x2819E00 modo 3) o un sub-nodo (0x281D970) un false cancela más que el nodo; en
// ese grupo sólo se veta si todos sus nodos están agotados (prueba en un PC, 14-10).
bool __fastcall node_detour(uintptr_t stage,uintptr_t node,uintptr_t context,uintptr_t pose,
                           uintptr_t p5,uintptr_t p6,uint8_t p7,uintptr_t out) noexcept {
    g_nodes.fetch_add(1,std::memory_order_relaxed);
    // El hook inline salta (no llama): el retorno es el del CALL nativo a 0x281CEE0 y su ranura está en
    // el rsp que tenía el llamador menos 8 (su marco: el grupo nativo de 0x2819E00, npcact::read_group).
    const auto ret=reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uint32_t ret_rva=ret>g_base && ret-g_base<0xFFFFFFFFull ? static_cast<uint32_t>(ret-g_base) : 0;
    const uintptr_t caller_rsp=reinterpret_cast<uintptr_t>(_AddressOfReturnAddress())+8;
    uint32_t data=0,index=0;
    if (game::fast_read(node,&data,sizeof data) && game::fast_read(node+4,&index,sizeof index)) {
        if (user2_stages_npc_node_blocked(stage,data,index,ret_rva,node,caller_rsp)) {
            g_vetoes.fetch_add(1,std::memory_order_relaxed);
            return false;
        }
    } else g_unreadable.fetch_add(1,std::memory_order_relaxed);
    return g_node.unsafe_call<bool>(stage,node,context,pose,p5,p6,p7,out);
}
bool install(uintptr_t base,std::span<const gad::Section> sections,uint32_t rva,
             std::span<const uint8_t> bytes,void* detour,SafetyHookInline& target) {
    std::vector<uint8_t> actual(bytes.size());
    if (!gad::in_real_code(sections,rva,static_cast<uint32_t>(bytes.size())) ||
        !game::safe_read(base+rva,actual.data(),actual.size()) ||
        !std::equal(actual.begin(),actual.end(),bytes.begin())) {
        log::error("NpcActivation: entrada exe+0x{:X} rechazada: sección o firma no coincide",rva);
        return false;
    }
    auto hook=safetyhook::InlineHook::create(reinterpret_cast<void*>(base+rva),detour,safetyhook::InlineHook::StartDisabled);
    if (!hook || hook->original_bytes().size()!=5 ||
        !std::equal(hook->original_bytes().begin(),hook->original_bytes().end(),bytes.begin())) {
        log::error("NpcActivation: parche exe+0x{:X} rechazado: no ocupa el MOV de 5 bytes verificado",rva);
        return false;
    }
    target=std::move(*hook);
    if (!target.enable()) { target={}; return false; }
    return true;
}
} // namespace
void npc_action_probe_configure(bool wanted) { g_wanted.store(wanted); }
// Revisión: sólo con user2_stages (user2_stages_npc_probes_on lo comprueba también al arrancar U-1).
bool npc_action_probe_wanted() noexcept {return g_wanted.load() && user2_stages_npc_probes_on();}
void npc_action_probe_install(uintptr_t base,std::span<const gad::Section> sections,bool applied_mid_ready) {
    if (!npc_action_probe_wanted()) return;
    g_base=base;
    const bool action=install(base,sections,0x2A78CA0,kActionBytes,reinterpret_cast<void*>(&action_detour),g_action);
    const bool node=install(base,sections,0x281CEE0,kNodeBytes,reinterpret_cast<void*>(&node_detour),g_node);
    user2_stages_npc_action_probe_ready(action && applied_mid_ready);
    user2_stages_npc_node_probe_ready(node);
    log::info("NpcActivation: sondas entrada acción={} H1b={} constructor nodo={}; contención requiere las tres verificadas",
              action,applied_mid_ready,node);
}
void npc_action_probe_applied(uintptr_t component,uint32_t key) noexcept {
    if (npcact::mark_applied(t_pending,component,key)) g_applied.fetch_add(1,std::memory_order_relaxed);
}
void npc_action_probe_drain() {
    if (!npc_action_probe_wanted()) return;
    static unsigned ticks=0;
    if (++ticks%60) return;
    log::info("NpcActivation [entradas]: acciones={} OnServer comprobado={} constructores={} vetos locales={} lecturas sin validar={} "
              "pila TLS muerta limpiada={}",
              g_actions.load(),g_applied.load(),g_nodes.load(),g_vetoes.load(),g_unreadable.load(),g_stale_tls.load());
}
} // namespace cdmp::session2
