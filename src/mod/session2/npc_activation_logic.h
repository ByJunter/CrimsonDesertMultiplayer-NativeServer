#pragma once
// S33, build 1.0.0.2976: estado puro de activación. No lee ni escribe el juego.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace cdmp::session2::npcact {
enum class Mode : uint8_t { Off, Probe, On };
struct ParsedMode { Mode mode = Mode::Off; bool bad = false; };
inline ParsedMode parse_mode(std::string_view value, bool allow_on) noexcept {
    if (value.empty() || value == "0") return {};
    if (value == "probe") return {Mode::Probe, false};
    if (value == "1" && allow_on) return {Mode::On, false};
    return {Mode::Off, true};
}
inline const char* mode_name(Mode mode) noexcept {
    return mode == Mode::On ? "1" : mode == Mode::Probe ? "probe" : "0";
}
// S33: las 285 primeras peticiones tardaron como máximo 3,04 s desde el 2005 del invitado.
// Política de contención explícita, no prueba de invalidez: plazo desde publicación al
// controlador, nunca desde nacimiento ni desde el retorno externo 0.
inline constexpr uint64_t kNoProgressUs = 5'000'000;
inline constexpr uint8_t kMaxNodeFailures = 2;  // nacimiento inicial y un único reintento
inline constexpr size_t kMaxFamily = 64;
inline constexpr uint8_t kMaxCleanupFailures = 3;
inline constexpr uint64_t kCleanupRetryUs = 1'000'000;
// Revisión: una unidad rechazada (stage no demostrada, vínculo cambiado...) no se reintenta cada
// 250 ms para siempre: tras kMaxRefusals se abandona esa unidad (sólo ella, ver Table::abandon).
inline constexpr uint8_t kMaxRefusals = 3;
// Prueba en un PC (14-10, 15:42): un abandono es POR UNIDAD y nunca devuelve la red antigua global. Cota de
// cordura: pasado este número de unidades abandonadas en la sesión se avisa una vez (WRN), sin abortar.
inline constexpr uint64_t kAbandonWarn = 256;
inline bool abandon_warn(uint64_t abandoned_so_far) noexcept { return abandoned_so_far == kAbandonWarn + 1; }
// Lápidas de retiradas y registros viejos: se borran al cambiar de época o pasados 10 min.
inline constexpr uint64_t kStaleUs = 600'000'000;
struct Node {
    uint32_t epoch = 0, data = 0, index = 0;
    uint64_t stage = 0, generation = 0;
    bool operator==(const Node&) const = default;
    bool valid() const noexcept { return epoch && data && stage && generation; }
};
inline size_t node_hash(const Node& n) noexcept {
    uint64_t h = n.stage * 0x9E3779B97F4A7C15ull;
    h ^= (static_cast<uint64_t>(n.data) << 32 | n.index) * 0xC2B2AE3D27D4EB4Full;
    h ^= (n.generation + n.epoch) * 0x165667B19E3779F9ull;
    return static_cast<size_t>(h ^ (h >> 29));
}
struct Actor {
    uint32_t key = 0, controller = 0, parent = 0, descriptor = 0;
    uint8_t type = 0;
    uint64_t birth = 0, born_us = 0, queued_us = 0, first_req_us = 0, applied_us = 0;
    uint64_t handler_us = 0, retry_after_us = 0;
    // S35 (AUDITORIA.md §4): estados separados. applied_us = H1b autorizado y aplicado (sólo con la evidencia S35);
    // activated_us = rama payload+0x281 (activación, no movimiento ni misión); clock_us = inicio del reloj de
    // progreso (0 = queued_us; se reinicia tras un silencio/carga del invitado); version = sube con cada evidencia
    // nueva (primera Req, aplicación, activación, primer rechazo, encolado, vínculo): la ejecución de una retirada
    // compara la versión elegida con la actual.
    uint64_t activated_us = 0, clock_us = 0, postponed_us = 0, last_reject_us = 0;
    Node node{};
    bool assigned = false, removed = false, pending = false, diagnosed = false, orphaned = false, local_retired = false;
    bool plan_reported = false, logout_seen = false, gave_up = false, guard_noted = false, disagree_noted = false,
         exhausted_noted = false;
    uint8_t cleanup_failures = 0, refusals = 0, action_lines = 0, rejections = 0, postpones = 0;
    uint16_t guest_req_bits = 0, host_req_bits = 0, version = 0;
    // S36 fase B (npc_family_control=1, npc_family_logic.h; AUDITORIA.md §6.1): el nacimiento del PADRE con el que se
    // vinculó en su mid (0 = desconocido: vínculo visto sólo en la publicación) y la unidad (clave y nacimiento de su
    // raíz). fam: 0 ninguna, kFamRoot raíz de una unidad con hijos, kFamMember miembro (juzgado por su raíz, nunca
    // vence solo). family_split: la unidad quedó partida (un miembro no pudo seguir a su raíz): se retira entera.
    uint64_t parent_birth = 0, unit_birth = 0;
    uint32_t unit_key = 0;
    uint8_t fam = 0, fam_depth = 0;
    bool family_split = false, host_kept = false, member_noted = false;
};
inline constexpr uint8_t kFamRoot = 1, kFamMember = 2;
// Campo a campo, sin memset/memcpy del CRT: el nacimiento se registra desde el mid hook 0x2792A7B,
// donde una copia vectorizada AVX podría pisar la mitad alta de los YMM del juego (p8_probe_logic.h).
inline void reset_actor(Actor& a) noexcept {
    a.key = 0; a.controller = 0; a.parent = 0; a.descriptor = 0; a.type = 0;
    a.birth = 0; a.born_us = 0; a.queued_us = 0; a.first_req_us = 0; a.applied_us = 0;
    a.handler_us = 0; a.retry_after_us = 0;
    a.activated_us = 0; a.clock_us = 0; a.postponed_us = 0; a.last_reject_us = 0;
    a.node.epoch = 0; a.node.data = 0; a.node.index = 0; a.node.stage = 0; a.node.generation = 0;
    a.assigned = false; a.removed = false; a.pending = false; a.diagnosed = false; a.orphaned = false;
    a.local_retired = false; a.plan_reported = false; a.logout_seen = false; a.gave_up = false; a.guard_noted = false;
    a.disagree_noted = false; a.exhausted_noted = false;
    a.cleanup_failures = 0; a.refusals = 0; a.action_lines = 0; a.rejections = 0; a.postpones = 0;
    a.guest_req_bits = 0; a.host_req_bits = 0; a.version = 0;
    a.parent_birth = 0; a.unit_birth = 0; a.unit_key = 0; a.fam = 0; a.fam_depth = 0;
    a.family_split = false; a.host_kept = false; a.member_noted = false;
}
// S36: un miembro de una unidad (npc_family_control=1) no vence por sí solo: la unidad la juzga su raíz.
inline bool family_member(const Actor& a) noexcept { return a.fam == kFamMember && a.unit_key && a.unit_key != a.key; }
inline void bump(Actor& a) noexcept { a.version = static_cast<uint16_t>(a.version + 1); }
// Revisión: miembro compacto (24 B); una Family ocupa ~1,6 KB en la pila del servidor, no ~9 KB.
// S35: `version` cabe en el relleno tras `type` (sigue en 24 B): la ejecución compara la de cada miembro.
struct Member {
    uint32_t key = 0, controller = 0, parent = 0;
    uint8_t type = 0;
    uint16_t version = 0;
    uint64_t birth = 0;
};
struct Family {
    std::array<Member, kMaxFamily> actors{};
    size_t size = 0;
    bool overflow = false;
    bool contains(uint32_t key) const noexcept {
        for (size_t i = 0; i < size; ++i) if (actors[i].key == key) return true;
        return false;
    }
    int index_of(uint32_t key) const noexcept {
        for (size_t i = 0; i < size; ++i) if (actors[i].key == key) return static_cast<int>(i);
        return -1;
    }
};
inline bool no_progress(const Actor& a, uint64_t now) noexcept {
    if (a.removed || a.pending || a.gave_up || now < a.retry_after_us) return false;
    if (a.local_retired) return a.cleanup_failures < kMaxCleanupFailures;
    if (a.orphaned || a.family_split) return true;
    if (family_member(a)) return false;
    return a.assigned && !a.applied_us && a.queued_us && now >= a.queued_us && now - a.queued_us >= kNoProgressUs;
}
inline bool parent_requires_retirement(const Actor& parent) noexcept { return parent.local_retired; }
// Se comprueba antes de CADA llamada, también si el actor reaparece dentro del plan de su padre.
inline bool may_cleanup_member(const Actor& current, uint32_t key, uint64_t birth, uint64_t now) noexcept {
    return current.key == key && current.birth == birth && !current.removed &&
           current.cleanup_failures < kMaxCleanupFailures && now >= current.retry_after_us;
}

inline bool observe_logout_entry(Actor& a) noexcept {
    a.logout_seen = true;
    if (a.local_retired) return false; // una entrada no confirma el resultado de nuestra llamada
    a.removed = true; // contabilidad heredada de las bajas ajenas a la contención
    return true;
}
inline bool applied_to(const Actor& a, uint64_t birth, uint32_t sender, bool normal_applied) noexcept {
    return !a.removed && a.birth == birth && sender == a.controller && normal_applied;
}
// 0x2A78CA0 (Ghidra 0x2a78ca0.c:41-49): comprobado ya el controlador de la sesión, con payload+0x281
// distinto de 0 llama a 0xEE67EE0, que pone la acción del secuenciador
// "emptyname_for_sequencerdummayaction", y devuelve 0 sin OnServer ni difusión. Es una acción
// aplicada a petición del controlador: progreso, igual que H1b. 0xFF = byte ilegible (no cuenta).
inline bool action_progress(bool on_server_applied, uint8_t branch_flag, int32_t internal_result) noexcept {
    return on_server_applied || (branch_flag != 0 && branch_flag != 0xFF && internal_result == 0);
}

// ---------------------------------------------------------------- S35: evidencia de 0x2A78CA0 (fase A)
// AUDITORIA.md §4 e INFORME-A4 «Contrato de acción», build 2976: param4 NO es la sesión. 0x2A78CF2 llama a
// vf+0x170(param4, scratch) y 0x2A78CF8/0x2A78CFE comparan la clave devuelta con actor+0x90 (0x2a78ca0.c:39-44;
// llamador 0x296caa0.c:211-213). La S34 leía param4+0x90 -> user+0x60 y obtenía emisor 0 en 843/843 líneas: la
// contención retiró NPC sanos. Criterio H1b: cuenta una acción nativa, AUTORIZADA y APLICADA para la unidad:
//  - H1b (0x2A79060, después de OnServer con éxito) alcanzado por ESTA llamada: el Pending TLS de la llamada
//    (componente y clave) coincide (npc_action_probe.cpp); nunca un retorno 0 externo ni interno por sí solo;
//  - el nacimiento (token tomado antes de la llamada) es el de la unidad: un resultado no pasa a otra encarnación;
//  - actor+0x90 leído antes y después de la llamada vale el controlador registrado de la unidad. Es el
//    controlador ESPERADO (la comparación nativa ya lo exigió para llegar a H1b), no un emisor observado: el
//    emisor real (resultado de vf170) queda «no medido»; nunca se llama a vf170 desde el mod para medirlo;
//  - la unidad no está dada de baja ni en retirada local.
// Rama payload+0x281 (0x2a78ca0.c:45-49, sólo tras la comprobación nativa del controlador): su propio contrato,
// acción ficticia del secuenciador, resultado 0 sin OnServer. Acredita ACTIVACIÓN bajo esa condición nativa, no
// movimiento ni misión: se guarda aparte (activated_us). Byte ilegible (0xFF) o resultado ilegible: nada.
enum class Evidence : uint8_t {
    Applied,          // H1b de esta llamada, controlador y nacimiento de la unidad
    Branch281,        // rama payload+0x281 con resultado 0, sin H1b: activación
    Rejected,         // resultado interno distinto de 0 sin H1b (p. ej. NotControlledBySession)
    ReturnZeroOnly,   // resultado 0 sin H1b ni rama 281: no es evidencia
    OtherController,  // actor+0x90 al entrar o al salir distinto del controlador de la unidad (o cambió)
    OldBirth,         // token de otra encarnación de la clave
    Retired,          // la unidad está dada de baja o en retirada local
    Unreadable,       // actor+0x90 o el resultado interno ilegibles: no se acepta
    Count
};
inline const char* evidence_name(Evidence e) noexcept {
    switch (e) {
        case Evidence::Applied: return "aplicada";
        case Evidence::Branch281: return "rama281";
        case Evidence::Rejected: return "rechazada";
        case Evidence::ReturnZeroOnly: return "retorno0";
        case Evidence::OtherController: return "otro_controlador";
        case Evidence::OldBirth: return "otro_nacimiento";
        case Evidence::Retired: return "retirada";
        case Evidence::Unreadable: return "ilegible";
        default: return "?";
    }
}
inline constexpr int32_t kInternalUnreadable = INT32_MIN;
// Pila TLS de llamadas a 0x2A78CA0 en curso (npc_action_probe.cpp): cada llamada empuja la suya y la saca al
// volver; H1b sólo marca la llamada más interna, y sólo si componente y clave coinciden (una reentrada para otro
// actor nunca acredita a la exterior, ni la exterior hereda la marca de la interior).
struct PendingCall {
    uintptr_t component = 0;
    uint32_t key = 0;
    bool applied = false;
    PendingCall* previous = nullptr;
};
inline bool mark_applied(PendingCall* top, uintptr_t component, uint32_t key) noexcept {
    if (!top || !component || top->component != component || top->key != key) return false;
    top->applied = true;
    return true;
}
struct ActionCall {
    uint64_t birth = 0;                       // token tomado ANTES de la llamada
    bool h1b = false;                         // el Pending TLS de esta llamada llegó a H1b
    uint8_t branch_flag = 0xFF;               // payload+0x281 a la entrada (0xFF = ilegible)
    int32_t internal = kInternalUnreadable;   // *resultado tras la llamada
    uint32_t ctl_entry = 0, ctl_exit = 0;     // actor+0x90 antes / después (0 = ilegible)
};
// Revisión S35 (hallazgo 1): un H1b de ESTA llamada con actor+0x90 a la ENTRADA igual al controlador de la unidad es
// aplicado aunque la lectura de salida falle o cambie (la comparación nativa se hizo antes de llegar a H1b); esa
// discrepancia se cuenta y se registra aparte (exit_mismatch) y nunca convierte una aplicación en una falsa retirada.
// Sin H1b la lectura de salida sigue exigiéndose (rama 281, rechazos).
inline Evidence classify(const Actor& a, const ActionCall& c) noexcept {
    if (!c.birth || a.birth != c.birth) return Evidence::OldBirth;
    if (a.removed || a.local_retired) return Evidence::Retired;
    if (c.h1b && c.ctl_entry && c.ctl_entry == a.controller) return Evidence::Applied;
    if (!c.ctl_entry || !c.ctl_exit) return Evidence::Unreadable;
    if (c.ctl_entry != a.controller || c.ctl_exit != c.ctl_entry) return Evidence::OtherController;
    if (c.internal == kInternalUnreadable) return Evidence::Unreadable;
    if (c.branch_flag != 0 && c.branch_flag != 0xFF && c.internal == 0) return Evidence::Branch281;
    if (c.internal != 0) return Evidence::Rejected;
    return Evidence::ReturnZeroOnly;
}
// H1b aplicado cuya lectura de actor+0x90 a la salida falló (0) o cambió: sólo para la línea y el contador.
inline bool exit_mismatch(const ActionCall& c) noexcept { return c.h1b && c.ctl_entry && c.ctl_exit != c.ctl_entry; }

// Revisión S35 (hallazgo 5, AUDITORÍA §6.5): las lecturas de action_detour (npc_action_probe.cpp) con una política de
// lectura, para probarlas con un marco simulado (cc, actor, block+0x281, *resultado). read(dirección, destino, bytes)
// es una lectura protegida (en el juego, game::fast_read). Offsets build 2976: cc+0x08 actor (0x2A78CCB), actor+0x60
// clave, actor+0x90 controlador (0x2A78CF8), cc+0x40 -> +0x30 restore (0x2A78DAF / 0x2328894), payload+0x281 rama
// (0x2a78ca0.c:45), y la lectura S34 param4+0x90 -> +0x60 (conservada sólo para npc_action_evidence 0/probe).
struct ActionOffsets {
    uint32_t cc_actor = 0x08, actor_key = 0x60, actor_ctl = 0x90, cc_restore = 0x40, restore_byte = 0x30;
    uint32_t branch = 0x281, s34_user = 0x90, s34_user_key = 0x60;
};
template <class Read> uintptr_t read_ptr_with(Read&& read, uintptr_t at) noexcept {
    uintptr_t v = 0;
    if (at < 0x10000 || !read(at, &v, sizeof v)) return 0;
    return v >= 0x10000 && v < 0x7FFF'FFFF'0000ull ? v : 0;  // como fast_ptr: una dirección de usuario plausible
}
template <class T, class Read> T read_with(Read&& read, uintptr_t at, T fallback = {}) noexcept {
    T v{};
    return at >= 0x10000 && read(at, &v, sizeof v) ? v : fallback;
}
template <class Read> int restore_with(Read&& read, uintptr_t cc, const ActionOffsets& o = {}) noexcept {
    const uintptr_t state = read_ptr_with(read, cc + o.cc_restore);
    return state ? read_with<int8_t>(read, state + o.restore_byte, int8_t{-1}) : -1;
}
struct ActionEntry {
    uintptr_t actor = 0;
    uint32_t key = 0, ctl = 0, s34_sender = 0;
    uint8_t flag = 0xFF;
    int restore = -1;
};
// Antes de la llamada: actor y clave siempre (la búsqueda del token la hace el llamador); el resto sólo con token.
template <class Read> ActionEntry read_action_key(Read&& read, uintptr_t cc, const ActionOffsets& o = {}) noexcept {
    ActionEntry e;
    e.actor = read_ptr_with(read, cc + o.cc_actor);
    e.key = read_with<uint32_t>(read, e.actor + o.actor_key);
    return e;
}
template <class Read> void read_action_entry(Read&& read, uintptr_t cc, uintptr_t context, uintptr_t block,
                                             ActionEntry& e, const ActionOffsets& o = {}) noexcept {
    e.flag = read_with<uint8_t>(read, block + o.branch, uint8_t{0xFF});
    e.restore = restore_with(read, cc, o);
    e.ctl = read_with<uint32_t>(read, e.actor + o.actor_ctl);
    e.s34_sender = read_with<uint32_t>(read, read_ptr_with(read, context + o.s34_user) + o.s34_user_key);
}
struct ActionExit {
    bool same = false;  // cc+8 sigue siendo el mismo actor con la misma clave
    uint32_t ctl = 0;
    int32_t internal = kInternalUnreadable;
    int restore = -1;
};
template <class Read> ActionExit read_action_exit(Read&& read, uintptr_t cc, const ActionEntry& e, uintptr_t returned,
                                                  const ActionOffsets& o = {}) noexcept {
    ActionExit x;
    x.internal = read_with<int32_t>(read, returned, kInternalUnreadable);
    x.same = e.actor && read_ptr_with(read, cc + o.cc_actor) == e.actor &&
             read_with<uint32_t>(read, e.actor + o.actor_key) == e.key;
    if (!x.same) return x;
    x.ctl = read_with<uint32_t>(read, e.actor + o.actor_ctl);
    x.restore = restore_with(read, cc, o);
    return x;
}
inline ActionCall make_call(const ActionEntry& e, const ActionExit& x, bool h1b, uint64_t birth) noexcept {
    ActionCall c;
    c.birth = birth; c.h1b = h1b; c.branch_flag = e.flag; c.internal = x.internal;
    c.ctl_entry = e.ctl; c.ctl_exit = x.ctl;
    return c;
}
// npc_action_evidence: 0 = S34 (applied_to con el emisor mal leído, conservado para reproducirlo), probe = las dos
// se calculan y se registran pero decide la S34, 1 = decide el criterio H1b.
enum class EvidenceMode : uint8_t { S34, Probe, H1b };

// ---------------------------------------------------------------- S35: política de contención (fase A)
// AUDITORIA.md §4 «Cambios obligatorios» e INFORME-A4 puntos 1-8: no retirar por cola, carga o silencio del
// cliente; una Req suspende la decisión destructiva sólo un plazo acotado (nunca protege para siempre una
// operación rechazada); revalidar al EJECUTAR la retirada con la versión de la unidad. Plazos experimentales,
// configurables ([session2] npc_no_progress_ms / npc_req_grace_ms / npc_guest_silence_ms), no leyes del motor.
struct Policy {
    bool guard = false;                       // npc_containment_guard=1 (probe: sólo se registra)
    bool h1b = false;                         // npc_action_evidence=1: activated_us también es progreso
    uint64_t no_progress_us = kNoProgressUs;  // plazo desde el inicio del reloj (encolado al invitado)
    uint64_t req_grace_us = 0;                // una Req del invitado para la unidad pospone hasta plazo+gracia
    uint64_t silence_us = 0;                  // invitado sin ninguna Req de IA este tiempo: posponer
    // Revisión S35 (hallazgo 2): tope del tiempo total pospuesto por silencio/carga, contado desde el plazo vencido
    // (queued_us + no_progress_us). Agotado: se retira (con una línea WRN), no se espera para siempre. 0 = sin tope.
    uint64_t max_postpone_us = 0;
};
struct Context {
    uint64_t now = 0;
    uint64_t last_guest_req_us = 0;  // última Req de IA real del invitado, de cualquier clave (no temporizadores)
    bool guest_loading = false;      // el invitado está cargando (fase de movimiento de child_hold)
};
enum class Due : uint8_t { No, Retire, PostponeReq, PostponeSilence, PostponeLoading };
inline const char* due_name(Due d) noexcept {
    switch (d) {
        case Due::No: return "no";
        case Due::Retire: return "retirar";
        case Due::PostponeReq: return "req";
        case Due::PostponeSilence: return "silencio";
        case Due::PostponeLoading: return "carga";
    }
    return "?";
}
inline bool has_progress(const Actor& a, const Policy& p) noexcept {
    return a.applied_us || (p.h1b && a.activated_us);
}
// El tope de posposición por silencio/carga se agotó: desde el plazo original (queued_us + no_progress_us), no desde
// el reloj reiniciado, así que reiniciar el reloj nunca lo alarga.
inline bool postpone_exhausted(const Actor& a, const Policy& p, uint64_t now) noexcept {
    if (!p.max_postpone_us || !a.queued_us) return false;
    const uint64_t limit = a.queued_us + p.no_progress_us + p.max_postpone_us;
    return now >= limit;
}
// S34 (no_progress) intacto para guard=false: la decisión antigua. Con guard, además de no_progress:
// - progreso = H1b o rama281 (evidencia S35);
// - el invitado cargando o sin ninguna Req de IA en silence_us: se pospone y el reloj vuelve a empezar al salir;
// - una Req del invitado para esta unidad pospone hasta plazo + req_grace_us; después se retira (rechazo acotado).
inline Due containment_due(const Actor& a, const Policy& p, const Context& c) noexcept {
    if (a.removed || a.pending || a.gave_up || c.now < a.retry_after_us) return Due::No;
    if (a.local_retired) return a.cleanup_failures < kMaxCleanupFailures ? Due::Retire : Due::No;
    if (a.orphaned) return Due::Retire;
    // S36: una unidad partida (raíz del invitado con un miembro que no la siguió: el mecanismo del cierre S34) se
    // retira entera, de hojas a raíz, sin esperar plazo; un miembro nunca vence solo (lo juzga su raíz).
    if (a.family_split) return Due::Retire;
    if (family_member(a)) return Due::No;
    if (!a.assigned || !a.queued_us || has_progress(a, p)) return Due::No;
    const uint64_t start = p.guard && a.clock_us > a.queued_us ? a.clock_us : a.queued_us;
    if (c.now < start || c.now - start < p.no_progress_us) return Due::No;
    if (!p.guard) return Due::Retire;
    const bool exhausted = postpone_exhausted(a, p, c.now);
    if (c.guest_loading && !exhausted) return Due::PostponeLoading;
    if (p.silence_us && !exhausted &&
        (!c.last_guest_req_us || c.now < c.last_guest_req_us || c.now - c.last_guest_req_us >= p.silence_us)) {
        return Due::PostponeSilence;
    }
    if (a.first_req_us && p.req_grace_us && c.now - start < p.no_progress_us + p.req_grace_us) return Due::PostponeReq;
    return Due::Retire;
}
// Una posposición por silencio o carga reinicia el reloj (la unidad vuelve a tener el plazo entero cuando el
// invitado vuelve a hablar); por Req no (la gracia es acotada). Nunca consume rechazos ni presupuesto del nodo.
inline void postpone(Actor& a, Due why, uint64_t now, uint64_t retry_us = kCleanupRetryUs) noexcept {
    if (why == Due::PostponeSilence || why == Due::PostponeLoading) a.clock_us = now;
    if (!a.postponed_us) a.postponed_us = now;
    if (a.postpones < 0xFF) ++a.postpones;
    a.retry_after_us = now + retry_us;
    a.pending = false;
}
// Al EJECUTAR (user2_stages.cpp, después de ModLock -> barrera de Trocs, otra vez bajo ActivationLock): la unidad
// elegida sigue siendo la misma encarnación, su versión no cambió (ni Req nueva, ni aplicación, ni activación),
// la decisión fresca sigue siendo Retire y cada miembro conserva nacimiento y versión.
enum class Recheck : uint8_t { Valid, Gone, Progress, NewEvidence, Postpone };
inline const char* recheck_name(Recheck r) noexcept {
    switch (r) {
        case Recheck::Valid: return "válida";
        case Recheck::Gone: return "ya no está";
        case Recheck::Progress: return "progreso";
        case Recheck::NewEvidence: return "evidencia nueva";
        case Recheck::Postpone: return "posponer";
    }
    return "?";
}
inline Recheck recheck_root(const Actor& now_a, const Actor& selected, const Policy& p, const Context& c) noexcept {
    if (now_a.key != selected.key || now_a.birth != selected.birth || now_a.removed) return Recheck::Gone;
    if (now_a.orphaned || now_a.local_retired || now_a.family_split) return Recheck::Valid;  // retirada ya decidida
    if (has_progress(now_a, p)) return Recheck::Progress;
    if (!p.guard) return Recheck::Valid;
    if (now_a.version != selected.version) return Recheck::NewEvidence;
    Actor fresh = now_a;
    fresh.pending = false;  // la reserva propia no cuenta
    const Due d = containment_due(fresh, p, c);
    if (d == Due::Retire) return Recheck::Valid;
    return d == Due::No ? Recheck::NewEvidence : Recheck::Postpone;
}
// Cada miembro del plan: misma encarnación, no dado de baja y, con guard, la misma versión (un hijo que actuó o
// cambió de vínculo después de elegir el plan lo invalida: se vuelve a planificar, sin consumir rechazos).
inline Recheck recheck_member(const Actor& now_a, const Member& m, const Policy& p) noexcept {
    if (now_a.key != m.key || now_a.birth != m.birth || now_a.removed) return Recheck::Gone;
    if (p.guard && now_a.version != m.version) return Recheck::NewEvidence;
    return Recheck::Valid;
}
// Revisión S35 (hallazgo 5): la decisión de la ejecución de npc_activation_cleanup, pura. members_gone: un miembro
// del plan ya no está (S33: rechazo); root: recheck_root; member: el primer recheck_member distinto de Valid.
// Proceed: se retira. Cancel (sólo guard): se suelta la reserva y se vuelve a mirar, sin rechazo. Refuse: npc_refuse
// motivo 4 (S33/S34; con guard=0 también una aplicación entre elegir y ejecutar).
enum class Exec : uint8_t { Proceed, Cancel, Refuse };
struct ExecDecision {
    Exec exec = Exec::Proceed;
    Recheck why = Recheck::Valid;  // lo que se cuenta: el motivo real, no «ya no está» para un progreso
};
inline ExecDecision execution_decision(bool members_gone, Recheck root, Recheck member, bool guard) noexcept {
    if (members_gone || root == Recheck::Gone) return {Exec::Refuse, Recheck::Gone};
    const Recheck why = root != Recheck::Valid ? root : member;
    if (why == Recheck::Valid) return {Exec::Proceed, why};
    if (!guard) return {why == Recheck::Progress ? Exec::Refuse : Exec::Proceed, why};
    return {Exec::Cancel, why};
}
// La cancelación al ejecutar: suelta la reserva; si toca posponer (silencio/carga/Req) se pospone; en todo caso no se
// vuelve a mirar antes de kCleanupRetryUs. Devuelve la decisión fresca (para el contador de posposiciones).
inline Due cancel_execution(Actor& a, const Policy& p, const Context& c) noexcept {
    a.pending = false;
    const Due d = containment_due(a, p, c);
    if (d == Due::PostponeReq || d == Due::PostponeSilence || d == Due::PostponeLoading) postpone(a, d, c.now);
    if (a.retry_after_us < c.now + kCleanupRetryUs) a.retry_after_us = c.now + kCleanupRetryUs;
    return d;
}

// ---------------------------------------------------------------- S35: degradación (fase A)
// AUDITORIA.md §4: containment_ready falso reactivaba el vigilante global antiguo de 5 s (npc_activation_logic.h
// containment_ready + user2_stages.cpp npc_control_tick). Con npc_degradation=1 y npc_activation=1 elegido, una
// contención que no puede actuar (fallo de infraestructura: sonda no verificada, funciones de limpieza, fallo
// nativo) NO rearma ese aborto: U-e deja de asignar NPC nuevos (fail-closed) y no se retira nada en masa. Con
// npc_activation 0/probe el vigilante antiguo sigue siendo la única red (comportamiento previo).
struct Degrade {
    bool activation_on = false;   // npc_activation=1
    bool ready = false;           // containment_ready
    bool degradation_on = false;  // npc_degradation=1
};
inline bool legacy_watchdog_armed(const Degrade& d) noexcept {
    return !d.ready && !(d.activation_on && d.degradation_on);
}
inline bool assignments_paused(const Degrade& d) noexcept {
    return d.activation_on && d.degradation_on && !d.ready;
}
// ServerActor::Logout 0x2068340 en la limpieza: out=0 dado de baja; out igual al código global
// eErrNoUserAlreadyLogouting [exe+0x6CDB934] (estado +5E 0x20/0x40) = ya se iba: "ya no está" en
// todos los modos (c7ae132 lo contaba como baja). Otro código: fallo sólo en la retirada local
// (strict); la limpieza antigua conserva el comportamiento de c7ae132.
// kCleanBlocked (S36): no se llamó a Logout porque un descendiente suyo del plan no se retiró en esta pasada
// (npcfam::RetireGuard); cuenta como un fallo acotado de su limpieza.
enum CleanupResult : int { kCleanDone = 0, kCleanGone = 1, kCleanOther = 2, kCleanFault = 3, kCleanFailed = 4,
                           kCleanBlocked = 5 };
inline int logout_result(uint32_t out, uint32_t already_logouting, bool strict) noexcept {
    if (!out) return kCleanDone;
    if (already_logouting && out == already_logouting) return kCleanGone;
    return strict ? kCleanFailed : kCleanDone;
}
// Segunda revisión de la fase B (15-10, R1): en la limpieza de U-e (drenaje, logout, su Troc) un hijo bloquea a su
// padre sólo si sigue siendo del usuario 2 y no salió. kCleanOther porque el actor ya es de OTRA clave o controlador
// (p. ej. un miembro que renació con el anfitrión y la misma clave) no bloquea: ya no es nuestro. kCleanOther por un
// padre nativo distinto (validación local) sí bloquea: el hijo sigue con el usuario 2.
inline bool cleanup_blocks_parent(int result, bool not_ours) noexcept {
    if (result == kCleanDone || result == kCleanGone) return false;
    return !(result == kCleanOther && not_ours);
}
// R1/R10: una pasada de la limpieza de U-e. La siguiente pasada, en la MISMA llamada, sólo si ésta estuvo llena (había
// más claves que la tanda de 64: raíces detrás de sus miembros) y progresó; como mucho kCleanupPasses. Tras un drenaje,
// si quedó algo bloqueado (o la limpieza estaba ocupada), se repite en Trocs posteriores del usuario 2, como mucho
// kAfterDrainReruns veces y separadas por kCleanupRetryUs (lo bloqueado tiene su propio reintento a 1 s).
inline constexpr uint8_t kCleanupPasses = 4, kAfterDrainReruns = 3;
struct CleanPass {
    size_t n = 0, done = 0, gone = 0, left = 0;  // left: claves que la pasada dejó vivas (bloqueadas o con fallo)
    bool full = false, fault = false, busy = false;
};
inline bool cleanup_next_pass(const CleanPass& p, uint8_t passes_done) noexcept {
    return passes_done < kCleanupPasses && !p.fault && !p.busy && p.full && p.done + p.gone > 0;
}
inline bool cleanup_rerun(const CleanPass& p, uint8_t reruns_done) noexcept {
    return reruns_done < kAfterDrainReruns && !p.fault && (p.left > 0 || p.busy);
}
// La contención sustituye al aborto antiguo SÓLO mientras puede actuar; si no, sigue la red antigua.
// Prueba en un PC (14-10, 15:42): sólo cuentan las piezas de la propia contención (sondas y funciones
// verificadas, U-e=1, sin fallo nativo ni escrituras detenidas). Una unidad abandonada NO entra aquí.
struct Readiness {
    bool activation_on = false, action_probe = false, node_probe = false, cleanup_fns = false;
    bool ctl_on = false, fault = false;
};
inline bool containment_ready(const Readiness& r) noexcept {
    return r.activation_on && r.action_probe && r.node_probe && r.cleanup_fns && r.ctl_on && !r.fault;
}
// Retorno de cada CALL a 0x281CEE0, build 2976 (xrefs y desensamblado de Ghidra). Exact: un false
// cancela sólo ese nodo y lo que cuelga de él. Group: 0x2819E00 modo 3, líder (0x281A4B5) o miembro
// (0x281A589): un false cancela el grupo nativo entero; se veta sólo si TODOS sus nodos están agotados
// (read_group + Table::blocked_in). Parent: 0x28210B5 (sub-nodo del líder en 0x2820D80) y 0x281DB05
// (sub-nodo en 0x281D970): un false falla el nodo de arriba; ahí no se veta.
enum class VetoSite : uint8_t { Exact, Group, Parent, Unknown };
inline VetoSite veto_site(uint32_t return_rva) noexcept {
    switch (return_rva) {
        case 0x281994B: case 0x281A341: case 0x281A958: case 0x281DD65: return VetoSite::Exact;
        case 0x281A4B5: case 0x281A589: return VetoSite::Group;
        case 0x28210B5: case 0x281DB05: return VetoSite::Parent;
        default: return VetoSite::Unknown;
    }
}
// El grupo nativo de 0x2819E00 modo 3, leído en la entrada de 0x281CEE0 (build 2976, desensamblado del PE):
// prólogo de 0x2819E00 = 8 push, lea rbp,[rsp-0x1B8], sub rsp,0x2B8, así que rbp = rsp+0x100 y no hay push
// ni sub rsp en el cuerpo. El grupo actual (Ghidra local_270, también en r13) está en [rbp-0x78] =
// [rsp_llamador+0x88]: se carga en r13 en 0x281A40E y la ranura sólo se escribe en 0x2819E5F, 0x281A126 y
// 0x281A9AB (antes del bucle y al pasar de grupo), nunca entre los dos CALL (0x281A4B0, 0x281A584). Grupo
// (stride 0x28, decompilado 0x2819e00.c:111-181): +0x08 vector de entradas (8 B), +0x10 cantidad (u32),
// +0x18 entrada líder; el vector contiene también al líder. Cada entrada empieza por el puntero al nodo que
// 0x281CEE0 recibe en rdx ([nodo] recurso, [nodo+4] índice). Se exige el papel: en 0x281A4B5 el nodo es el
// del líder; en 0x281A589, el de otra entrada del vector. Cualquier lectura fallida o papel no demostrado:
// false (no se veta). read(dirección, destino, bytes) es una lectura protegida.
inline constexpr uint32_t kGroupLeaderRet = 0x281A4B5, kGroupMemberRet = 0x281A589;
inline constexpr uintptr_t kGroupFrameSlot = 0x88;
inline constexpr uint32_t kMaxGroup = 64;
struct GroupNode { uint32_t data = 0, index = 0; };
struct Group {
    std::array<GroupNode, kMaxGroup> nodes{};
    size_t size = 0;
};
template <class Read>
bool read_group(Read&& read, uintptr_t caller_rsp, uint32_t return_rva, uintptr_t node, Group& out) noexcept {
    out.size = 0;
    if (return_rva != kGroupLeaderRet && return_rva != kGroupMemberRet) return false;
    uintptr_t group = 0, vec = 0, leader = 0;
    uint32_t n = 0;
    if (!caller_rsp || !node || !read(caller_rsp + kGroupFrameSlot, &group, sizeof group) || !group) return false;
    if (!read(group + 0x08, &vec, sizeof vec) || !read(group + 0x10, &n, sizeof n) ||
        !read(group + 0x18, &leader, sizeof leader) || !vec || !leader || !n || n > kMaxGroup) return false;
    bool leader_in = false, role = false;
    for (uint32_t i = 0; i < n; ++i) {
        uintptr_t entry = 0, at = 0;
        GroupNode g;
        if (!read(vec + static_cast<uintptr_t>(i) * 8, &entry, sizeof entry) || !entry ||
            !read(entry, &at, sizeof at) || !at || !read(at, &g.data, sizeof g.data) ||
            !read(at + 4, &g.index, sizeof g.index)) {
            out.size = 0;
            return false;
        }
        const bool is_leader = entry == leader;
        leader_in = leader_in || is_leader;
        if (at == node && is_leader == (return_rva == kGroupLeaderRet)) role = true;
        out.nodes[out.size++] = g;
    }
    if (!leader_in || !role) out.size = 0;
    return out.size != 0;
}
struct NodeBudget {
    Node node{};
    uint8_t failures = 0;
    bool veto_logged = false, skip_logged = false;
    uint32_t vetoes = 0, skips = 0;
};
// Nodos ya registrados: 4 vías por conjunto, O(1) por construcción de descriptor.
template <size_t Sets, size_t Ways = 4> struct SeenCache {
    std::array<Node, Sets * Ways> slots{};
    std::array<uint8_t, Sets> next{};
    // true: nodo nuevo para la caché (el llamador lo registra una vez).
    bool insert(const Node& n) noexcept {
        const size_t set = node_hash(n) % Sets;
        for (size_t w = 0; w < Ways; ++w) if (slots[set * Ways + w] == n) return false;
        slots[set * Ways + next[set] % Ways] = n;
        next[set] = static_cast<uint8_t>((next[set] + 1) % Ways);
        return true;
    }
    bool contains(const Node& n) const noexcept {
        const size_t set = node_hash(n) % Sets;
        for (size_t w = 0; w < Ways; ++w) if (slots[set * Ways + w] == n) return true;
        return false;
    }
};
// Claves vistas, correspondencia directa: deduplicación aproximada y acotada de líneas de sonda.
template <size_t N> struct KeyCache {
    std::array<uint32_t, N> keys{};
    bool insert(uint32_t key) noexcept {
        uint32_t& slot = keys[(static_cast<size_t>(key) * 2654435761u) % N];
        if (slot == key) return false;
        slot = key;
        return true;
    }
};
// El dueño protege esta tabla con su cerrojo; no se llama al juego dentro de él.
// Revisión: direccionamiento abierto con borrado real (desplazamiento hacia atrás), sin lápidas de
// sondeo: una búsqueda fallida termina en el primer hueco. Las claves van en su propio vector para
// que los barridos periódicos lean 4 B por hueco.
template <size_t N, size_t B> struct Table {
    std::array<uint32_t, N> keys{};
    std::array<Actor, N> actors{};
    std::array<NodeBudget, B> budgets{};
    size_t count = 0, budget_count = 0;
    uint64_t overflow = 0, pruned = 0;
    size_t due_cursor = 0;
    static size_t home(uint32_t key) noexcept { return (static_cast<size_t>(key) * 2654435761u) % N; }
    static constexpr size_t max_count() noexcept { return N - N / 8; }
    // Un nacimiento ilegible/rechazado no monopoliza la limpieza de los demás.
    Actor* next_due(uint64_t now, uint32_t epoch, uint64_t generation) noexcept {
        return next_due_if(epoch, generation, [now](const Actor& a) { return no_progress(a, now); });
    }
    // S35: con la política (npc_containment_guard): también las unidades que toca posponer, para registrarlo.
    Actor* next_due(const Policy& p, const Context& c, uint32_t epoch, uint64_t generation) noexcept {
        return next_due_if(epoch, generation, [&p, &c](const Actor& a) { return containment_due(a, p, c) != Due::No; });
    }
    template <class Pred> Actor* next_due_if(uint32_t epoch, uint64_t generation, Pred&& due) noexcept {
        for (size_t i = 0; i < N; ++i) {
            const size_t at = (due_cursor + i) % N;
            if (!keys[at]) continue;
            Actor& a = actors[at];
            if (!due(a) || a.node.epoch != epoch || (!a.orphaned && !a.local_retired && a.node.generation != generation)) continue;
            due_cursor = (at + 1) % N;
            return &a;
        }
        return nullptr;
    }
    Actor* next_probe_plan(uint32_t epoch, uint64_t generation) noexcept {
        for (size_t i = 0; i < N; ++i) {
            const size_t at = (due_cursor + i) % N;
            if (!keys[at]) continue;
            Actor& a = actors[at];
            if (!a.assigned || a.removed || !a.queued_us || a.plan_reported || a.node.epoch != epoch ||
                a.node.generation != generation) continue;
            due_cursor = (at + 1) % N;
            return &a;
        }
        return nullptr;
    }
    Actor* find(uint32_t key) noexcept {
        if (!key) return nullptr;
        for (size_t i = 0, at = home(key); i < N; ++i, at = (at + 1) % N) {
            if (keys[at] == key) return &actors[at];
            if (!keys[at]) return nullptr;
        }
        return nullptr;
    }
    const Actor* find(uint32_t key) const noexcept { return const_cast<Table*>(this)->find(key); }
    // Registro de un nacimiento, con los campos a cero (campo a campo), o nullptr. Una encarnación
    // más nueva reutiliza su registro; una igual o más vieja no lo pisa.
    // S36 (AUDITORIA-2 cambio 1): reserva de registros para una unidad entera ANTES de asignar su raíz; claim con
    // use_reserved consume una (haya o no registro nuevo), y las que no se usen se devuelven al cerrar su marco.
    // Sin reservas pendientes (reserved == 0) todo es como antes.
    size_t reserved = 0;
    bool can_reserve(size_t n) const noexcept { return count + reserved + n <= max_count(); }
    bool reserve(size_t n) noexcept {
        if (!can_reserve(n)) return false;
        reserved += n;
        return true;
    }
    void unreserve(size_t n) noexcept { reserved -= n < reserved ? n : reserved; }
    Actor* claim(uint32_t key, uint64_t birth, bool use_reserved = false) noexcept {
        if (use_reserved) unreserve(1);
        if (!key || !birth) return nullptr;
        size_t at = home(key);
        for (size_t i = 0; i < N; ++i, at = (at + 1) % N) {
            if (keys[at] == key) {
                if (actors[at].birth >= birth) return nullptr;
                reset_actor(actors[at]);
                actors[at].key = key; actors[at].birth = birth;
                return &actors[at];
            }
            if (!keys[at]) break;
        }
        // Las reservas ajenas no las puede gastar una alta sin reserva.
        if (count + (use_reserved ? 0 : reserved) >= max_count() || keys[at]) { ++overflow; return nullptr; }
        keys[at] = key; ++count;
        reset_actor(actors[at]);
        actors[at].key = key; actors[at].birth = birth;
        return &actors[at];
    }
    Actor* born(const Actor& value) noexcept {
        if (Actor* current = find(value.key); current && current->birth >= value.birth) return current;
        Actor* a = claim(value.key, value.birth);
        if (a) *a = value;
        return a;
    }
    void erase_at(size_t hole) noexcept {
        keys[hole] = 0; reset_actor(actors[hole]); --count;
        for (size_t next = (hole + 1) % N; keys[next]; next = (next + 1) % N) {
            const size_t want = home(keys[next]);
            const bool stays = hole <= next ? (hole < want && want <= next) : (hole < want || want <= next);
            if (stays) continue;
            keys[hole] = keys[next]; actors[hole] = actors[next];
            keys[next] = 0; reset_actor(actors[next]);
            hole = next;
        }
    }
    bool erase(uint32_t key) noexcept {
        Actor* a = find(key);
        if (!a) return false;
        erase_at(static_cast<size_t>(a - actors.data()));
        return true;
    }
    // Hilo del módulo, una vez por segundo: bajas naturales, otras épocas y lápidas viejas. Los
    // registros vivos y viejos sólo se borran si la tabla pasa de la mitad.
    size_t prune(uint32_t epoch, uint64_t now) noexcept {
        size_t erased = 0;
        const bool crowded = count > N / 2;
        for (size_t i = 0; i < N;) {
            const Actor& a = actors[i];
            const bool old = a.born_us && now > a.born_us && now - a.born_us > kStaleUs;
            const bool drop = keys[i] && !a.pending &&
                ((a.removed && !a.local_retired) || a.node.epoch != epoch || (old && (a.removed || crowded)));
            if (!drop) { ++i; continue; }
            erase_at(i);  // lo desplazado a i se vuelve a mirar
            ++erased;
        }
        pruned += erased;
        return erased;
    }
    NodeBudget* budget(const Node& node, bool create) noexcept {
        if (!node.valid()) return nullptr;
        size_t at = node_hash(node) % B;
        for (size_t i = 0; i < B; ++i, at = (at + 1) % B) {
            if (budgets[at].node == node) return &budgets[at];
            if (!budgets[at].node.valid()) break;
        }
        if (!create) return nullptr;
        if (budget_count >= B - B / 4) {
            // Sólo valen la época y la generación de armado de este nodo: las demás se borran.
            for (size_t i = 0; i < B;) {
                const Node& n = budgets[i].node;
                if (n.valid() && (n.epoch != node.epoch || n.generation != node.generation)) erase_budget(i);
                else ++i;
            }
            if (budget_count >= B - B / 4) return nullptr;
            at = node_hash(node) % B;
            while (budgets[at].node.valid()) at = (at + 1) % B;
        }
        if (budgets[at].node.valid()) return nullptr;
        budgets[at] = {};
        budgets[at].node = node;
        ++budget_count;
        return &budgets[at];
    }
    void erase_budget(size_t hole) noexcept {
        budgets[hole] = {}; --budget_count;
        for (size_t next = (hole + 1) % B; budgets[next].node.valid(); next = (next + 1) % B) {
            const size_t want = node_hash(budgets[next].node) % B;
            const bool stays = hole <= next ? (hole < want && want <= next) : (hole < want || want <= next);
            if (stays) continue;
            budgets[hole] = budgets[next]; budgets[next] = {};
            hole = next;
        }
    }
    bool blocked(const Node& node) noexcept {
        const auto* b = budget(node, false);
        return b && b->failures >= kMaxNodeFailures;
    }
    // Nodos del grupo nativo agotados en la época, stage y generación de `base`. Vetar en un sitio de grupo
    // sólo es exacto si devuelve g.size: cancelar el grupo cancela entonces sólo unidades ya retiradas dos veces.
    size_t blocked_in(const Node& base, const Group& g) noexcept {
        size_t n = 0;
        for (size_t i = 0; i < g.size; ++i) {
            Node at = base;
            at.data = g.nodes[i].data; at.index = g.nodes[i].index;
            if (blocked(at)) ++n;
        }
        return n;
    }
    bool failed(const Node& node) noexcept {
        auto* b = budget(node, true);
        if (!b) return false;
        if (b->failures < kMaxNodeFailures) ++b->failures;
        return true;
    }
    Family family(uint32_t root) const noexcept {
        Family out;
        family_into(root, out);
        return out;
    }
    // Raíz y descendientes de su época, por pasadas sobre las claves ocupadas (no N por miembro).
    // Se llena en sitio: el hilo del servidor no copia la familia.
    // S36 (AUDITORIA.md §6.1, AUDITORIA-2 cambio 3, contraejemplos_diseno.py «familia_clave_reutilizada»): un hijo cuyo
    // vínculo se registró en su mid con el nacimiento de su padre (parent_birth) sólo se une a ESA encarnación; un
    // superviviente de otra encarnación de la misma clave queda fuera. parent_birth 0 (vínculo visto sólo en la
    // publicación, el de siempre) se une por clave como antes: con npc_family_control=0 nunca se fija, así que la
    // selección es exactamente la de la fase A. El orden de alta es padre antes que hijo: al revés, hojas a raíz.
    void family_into(uint32_t root, Family& out) const noexcept {
        out.size = 0;
        out.overflow = false;
        const Actor* r = find(root);
        if (!r || r->removed) return;
        const auto add = [&out](const Actor& a) {
            Member& m = out.actors[out.size++];
            m.key = a.key; m.controller = a.controller; m.parent = a.parent; m.type = a.type; m.birth = a.birth;
            m.version = a.version;
        };
        add(*r);
        const uint32_t epoch = r->node.epoch;
        for (bool grew = true; grew;) {
            grew = false;
            for (size_t i = 0; i < N; ++i) {
                if (!keys[i]) continue;
                const Actor& a = actors[i];
                if (a.removed || !a.parent || a.node.epoch != epoch || out.contains(a.key)) continue;
                const int p = out.index_of(a.parent);
                if (p < 0 || (a.parent_birth && a.parent_birth != out.actors[static_cast<size_t>(p)].birth)) continue;
                if (out.size == out.actors.size()) { out.overflow = true; return; }
                add(a);
                grew = true;
            }
        }
    }
    bool cleanup_result(uint32_t key, uint64_t birth, int result, uint64_t now) noexcept {
        Actor* a = find(key);
        if (!a || a->birth != birth) return false;
        a->pending = false;
        if (result == kCleanDone || result == kCleanGone) a->removed = true;  // lápida: hijos tardíos
        else {
            if (a->cleanup_failures < kMaxCleanupFailures) ++a->cleanup_failures;
            a->retry_after_us = now + kCleanupRetryUs;
        }
        return true;
    }
    // Abandono POR UNIDAD (prueba en un PC, 14-10): la raíz y su familia, por clave y nacimiento exactos, quedan
    // fuera de la contención (no_progress falso: ni retirada ni rechazo) y vivas con su controlador. `out` recibe
    // la familia para que el llamador excluya sus claves del vigilante antiguo. No toca ningún estado global.
    void abandon(uint32_t root, uint64_t birth, Family& out) noexcept {
        out.size = 0;
        out.overflow = false;
        Actor* r = find(root);
        if (!r || r->birth != birth) return;
        r->gave_up = true;
        r->pending = false;
        family_into(root, out);
        for (size_t i = 0; i < out.size; ++i) {
            if (Actor* a = find(out.actors[i].key); a && a->birth == out.actors[i].birth) a->gave_up = true;
        }
    }
    // true sólo en el rechazo que agota la unidad: el llamador lo registra una vez y abandona esa unidad.
    bool refused(uint32_t key, uint64_t birth) noexcept {
        Actor* a = find(key);
        if (!a || a->birth != birth) return false;
        a->pending = false;
        if (a->gave_up) return false;
        if (a->refusals < kMaxRefusals) ++a->refusals;
        if (a->refusals < kMaxRefusals) return false;
        a->gave_up = true;
        return true;
    }
    void queued(uint32_t key, uint32_t controller, uint64_t now) noexcept {
        if (auto* a = find(key); a && !a->removed && controller == a->controller && !a->queued_us) {
            a->queued_us = now;
            bump(*a);
        }
    }
    // S35: una llamada a 0x2A78CA0 clasificada por el criterio H1b (classify). Sólo la primera aplicación,
    // activación o rechazo cambian la versión: una retirada elegida antes se revalida al ejecutarse.
    Evidence record(uint32_t key, const ActionCall& call, uint64_t now) noexcept {
        auto* a = find(key);
        if (!a) return Evidence::OldBirth;
        const Evidence e = classify(*a, call);
        switch (e) {
            case Evidence::Applied:
                if (!a->applied_us) { a->applied_us = now; bump(*a); }
                break;
            case Evidence::Branch281:
                if (!a->activated_us) { a->activated_us = now; bump(*a); }
                break;
            case Evidence::Rejected:
                if (!a->rejections) bump(*a);
                if (a->rejections < 0xFF) ++a->rejections;
                a->last_reject_us = now;
                break;
            default: break;
        }
        return e;
    }
    // S35: la primera Req del invitado para la unidad (la gracia acotada de containment_due) también es versión.
    bool guest_req(uint32_t key, uint64_t now) noexcept {
        auto* a = find(key);
        if (!a || a->removed || a->first_req_us) return false;
        a->first_req_us = now;
        bump(*a);
        return true;
    }
    bool progress(uint32_t key, uint64_t birth, uint32_t sender, bool normal_applied, uint64_t now) noexcept {
        auto* a = find(key);
        if (!a || !applied_to(*a, birth, sender, normal_applied)) return false;
        if (!a->applied_us) a->applied_us = now;
        return true;
    }
};
struct Road { uint32_t requester = 0, id = 0, metadata = 0; bool valid = false; };
inline uint32_t u32(std::span<const uint8_t> bytes, size_t at) noexcept {
    return static_cast<uint32_t>(bytes[at]) | static_cast<uint32_t>(bytes[at + 1]) << 8 |
           static_cast<uint32_t>(bytes[at + 2]) << 16 | static_cast<uint32_t>(bytes[at + 3]) << 24;
}
// Metadata de AutoSpawnRoad del 2005, validada con S28/S31/S33. Sólo anotación.
inline Road road_metadata(std::span<const uint8_t> record) noexcept {
    if (record.size() < 34) return {};
    const uint32_t chars = u32(record, 30);
    if (chars > (record.size() - 34) / 2) return {};
    const size_t end = 34 + static_cast<size_t>(chars) * 2;
    if (end + 100 > record.size()) return {};
    return {u32(record, end + 88), u32(record, end + 96), u32(record, end + 92), true};
}
}  // namespace cdmp::session2::npcact
