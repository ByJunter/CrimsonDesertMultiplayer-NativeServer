#pragma once
// S38 fase C (docs/research/s25/s38-fase-c.md; diseño 3-viajeros.md §4; cambios obligatorios AUDITORIA.md §3 y
// a3-viajeros/informe-a3.md §3-§4): los viajeros que pide el AutoSpawnRoadReq 2550 del INVITADO (raíz y caravana
// entera) nacen con el usuario 2 como controlador. [session2] user2_road_control = 0 | probe | 1 (anfitrión).
// Build 1.0.0.2976 (PE 0x6AB28F00, base 0x140000000): todo RVA de este archivo vale sólo para esa build.
//
// Pura: el juego sólo se lee por la política `read(dirección, destino, bytes)` del llamador (lecturas protegidas,
// game::fast_read en el mod); nada se escribe. Las partes que corren en el mid hook 0x2792A7B (docked_gimmicks'
// create_mid, mov [r14+0x90],eax; rsi = descriptor, r13 = campo, r14 = actor) no usan CRT (memset/memcpy), ni copias
// agregadas grandes, ni cerrojos propios: el llamador decide el cerrojo.
//
// Contrato estático (exports Ghidra de la build 2976, D:\RE\CrimsonDesert_2976\exports\decomp; desensamblado del PE):
//  - 0x29C4930 (handler del Troc 2550) -> 0x2C41E50 -> .xtls 0x10880460 -> 0x27E8C90 (AutoSpawnRoad) ->
//    0x27EA160:564. 0x27EA160 también lo llama ActivateSpawningPool 0x27EE3E0 (cuatro veces): la marca y el
//    solicitante NO identifican por sí solos una petición 2550; la identifica el Troc 2550 del usuario 2 en curso.
//  - 0x27EA160 raíz (:1433-1480): descriptor 0x4E8 con el constructor base 0x278EA70 (vtable ICreateServerActorDesc
//    0x5B04BE8, que pone +0x2F0/+0x2F8/+0x3B8/+0x3C8/+0x3CC a 0), +0x3B8 = solicitante (sólo si es un jugador),
//    +0x3C8 = clave del solicitante, +0x3CC = primero del lote, +0x3CD = 1, +0x3D0..+0x3E8 = carretera.
//  - 0x27EA160 acompañantes (:1660-1720): el mismo constructor, los mismos campos, word +0x3CC = 0x100 (+0x3CD = 1),
//    y `thunk 0x1500E63B0(raíz, &acompañante)`: .xtls 0x100E63B0 crea el envoltorio de 0x18 B con vtable
//    _Rider 0x5B04E60 (0x100E64D8: lea rax,[rip-0xA5E167F]), [inner+0x2F0] = raíz (0x100E64D1) y
//    push_back(raíz+0x2F8) con 0x43DB70 (0x100E650B). La caravana es raíz + subárbol de _Rider.
//  - 0x28981B0 (contraejemplo de la auditoría): el MISMO descriptor, +0x3B8, +0x3C8 desde AutoSpawnOwnerData guardado y
//    word +0x3CC = 0x101 (0x28995E9). Reconstrucción desde datos guardados, no una petición 2550: fuera por el Troc.
//    Sus campos de datos guardados (+0x280 / +0x298, rellenos por 0x2791B80 antes de su comprobación) se registran
//    como diagnóstico [H]; no deciden (sin medida).
//  - 0x2792A7B: selector del controlador (r14+0x90 / [[rsi+0x40]+0x60] / [[r14+0xA0]+0x60] / [r13+0x180]) y la
//    escritura; en un jugador solo el solicitante de 2550 es también el dueño del campo y su controlador.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace cdmp::session2::road {

inline constexpr uint32_t kVtDescBase = 0x5B04BE8;   // pa::ICreateServerActorDesc (0x278EA70, 0x278EAE1)
inline constexpr uint32_t kVtWrapRider = 0x5B04E60;  // CreateChildServerActorDesc_Rider (.xtls 0x100E63B0)
// Prueba en dos copias, fase C probe (15-10 noche, cdmp.host-Cprobe.log): los acompañantes de las bandadas (0x1F5) y de
// los caminantes (0x3FD) cuelgan de su raíz con el envoltorio _Simple (20 de 21 preexámenes fallidos «envoltorio no
// _Rider», todos envoltorio=exe+0x5B04E80 interior=exe+0x5B04BE8, op 2550, solicitante el invitado). Mismo contrato
// que 0x281DBD0 (fase B §2.1): {vtable, inner}, [inner+0x2F0] = padre, push_back(padre+0x2F8). _Rider queda para los
// conductores de vehículo (raíz clase 5).
inline constexpr uint32_t kVtWrapSimple = 0x5B04E80;
// Gimmicks hijos de una caravana («Game Mechanic», tipo 7): con el anfitrión (host-kept), la regla de la fase B §2.7
// (publication_gate=1, que el modo 1 exige). Su propio subárbol (la carga de un carro: hasta 19 tipo 7 en la prueba 3 de
// la fase B) no se recorre ni cuenta: es del gimmick, también del anfitrión.
inline constexpr uint32_t kVtInstantGimmick = 0x5B04C68, kVtWrapAutoSpawnGimmick = 0x5B04DA0,
                          kVtWrapCombinationGimmick = 0x5B04D60;
// S40 carros (docs/research/s25/s40-carros.md §2): el vehículo enlazado de un carro. 0x2797510 (sólo si la clase del
// descriptor padre, +0x68 de sus datos, es 5): hasta dos descriptores base 0x278EA70 por vehículo (bucle de 2, ranuras
// +0x82/+0x84 de los datos del vehículo), cada uno con +0x490 = la clave del ACTOR padre ([param_4+0x60]), los campos
// de carretera copiados de la información de aparición del actor padre ([[actor+0x68]+0x20]+0x278.. -> desc+0x3C8..,
// sólo si era de carretera) y word +0x3CC = 0 (SIN marca: +0x3CD = 0); +0x3B8 no se escribe. Envoltorio
// _LinkedVehicle 0x5B04D80 {vtable, inner, ranura u8 +0x10}, [inner+0x2F0] = descriptor padre, push_back(padre+0x2F8)
// (0x43DB70). Lo crea la creación del actor padre: nace DESPUÉS del mid de su padre (siempre tardío). Prueba en dos
// copias C probe 2 (cdmp.host-Cprobe2.log): la carreta 0xB01002D8 (tipo 5, carretera 0xFA1) con 0xB01002DA (tipo 5,
// sub 0x0, solicitante y carretera copiados) partía su unidad («no es un viajero del mismo solicitante»).
inline constexpr uint32_t kVtWrapLinkedVehicle = 0x5B04D80, kDesc_ParentActorKey = 0x490;
// Envoltorio de un viajero (interior de carretera) / de un gimmick (interior InstantGimmick).
inline bool traveller_wrap(uint32_t w) noexcept { return w == kVtWrapRider || w == kVtWrapSimple; }
inline bool gimmick_wrap(uint32_t w) noexcept {
    return w == kVtWrapSimple || w == kVtWrapAutoSpawnGimmick || w == kVtWrapCombinationGimmick;
}
inline constexpr uint32_t kDesc_User = 0x40, kDesc_Saved0 = 0x280, kDesc_Saved1 = 0x298, kDesc_Parent = 0x2F0,
                          kDesc_Children = 0x2F8, kDesc_ChildCount = 0x300, kDesc_ReqActor = 0x3B8,
                          kDesc_ReqKey = 0x3C8, kDesc_First = 0x3CC, kDesc_Mark = 0x3CD, kDesc_Road = 0x3D0,
                          kDesc_RoadB = 0x3D8, kDesc_RoadC = 0x3E0, kDesc_RoadD = 0x3E8, kWrap_Inner = 0x08;
inline constexpr uint32_t kActor_Key = 0x60, kActor_Ctl = 0x90, kActor_User = 0xA0, kField_Owner = 0x180;
inline constexpr uint16_t kOpAutoSpawnRoad = 2550;
// Una caravana medida en S34: conductor + carro + caballo(s) + cabras/ovejas. 16 registros por unidad (raíz + 15).
// Revisión de la fase C (§3.2): una carreta lleva un gimmick con su carga (19 tipo 7 en la prueba 3 de la fase B): 16 no
// cabía. 32 registros por unidad (raíz + preexamen de como mucho 15 + tardíos / host-kept / hijos retenidos).
// S40 carros: medido, la carreta más grande (fase B prueba 3, 0xB01002A6): raíz + conductor + vehículo enlazado +
// gimmick + 19 de carga + 2 gimmicks = 25 registros; la de C probe 2 (0xB01002D8): 12, con un nieto de carga
// (profundidad 3). 48 por unidad: casi el doble del máximo medido (un hijo sin sitio deja a su padre sin retirar).
inline constexpr size_t kMaxUnit = 48, kMaxNodes = 15;
inline constexpr uint8_t kMaxDepth = 4;
inline constexpr uint32_t kMaxVector = 16;
inline constexpr uint8_t kNoIndex = 0xFF;

// La cohorte: tipos 3 (personas), 5 (carros, caballos de tiro: SÓLO como parte de una unidad de carretera de
// 0x27EA160, nunca la montura de nadie: +0xA0 y desc+0x40 a 0) y 6 (animales, bandadas 0x1F5). Tipo 7 (gimmicks
// «Game Mechanic», sin metadatos ni marca) y 4 nunca: su presencia en la unidad la deja entera con el anfitrión.
inline bool cohort_type(uint8_t t) noexcept { return t == 3 || t == 5 || t == 6; }
// Clase de los contadores por clase: 3, 5, 6 y «otro».
inline constexpr size_t kClasses = 4;
inline size_t class_of(uint8_t type) noexcept { return type == 3 ? 0 : type == 5 ? 1 : type == 6 ? 2 : 3; }
inline const char* class_name(size_t c) noexcept {
    switch (c) {
        case 0: return "t3";
        case 1: return "t5";
        case 2: return "t6";
        default: return "otro";
    }
}

inline bool plausible(uintptr_t p) noexcept { return p >= 0x10000 && p < 0x7FFF'FFFF'0000ull; }
inline uint32_t rva_in(uintptr_t v, uintptr_t base, uint64_t image_size) noexcept {
    return base && v >= base && v - base < image_size ? static_cast<uint32_t>(v - base) : 0;
}

// ---------------------------------------------------------------- procedencia de un descriptor
// Lo que el mid lee de un descriptor (lecturas protegidas). ok = vtable, marca y solicitante leídos.
struct Prov {
    bool ok = false;
    uint32_t vt = 0;          // RVA de la vtable (0 = fuera de la imagen o ilegible)
    uint8_t first = 0, mark = 0;
    uint32_t req_key = 0;
    uintptr_t req_ptr = 0;    // local al proceso: sólo se compara, nunca cruza la red
    bool req_ptr_ok = false;
};
template <class Read>
Prov read_prov(Read&& read, uintptr_t desc, uintptr_t base, uint64_t image_size) noexcept {
    Prov p;
    uintptr_t vt = 0;
    if (!plausible(desc) || !read(desc, &vt, sizeof vt)) return p;
    p.vt = rva_in(vt, base, image_size);
    if (p.vt != kVtDescBase) return p;
    uint8_t m[2] = {0, 0};
    if (!read(desc + kDesc_First, m, 2) || !read(desc + kDesc_ReqKey, &p.req_key, sizeof p.req_key)) return p;
    p.first = m[0];
    p.mark = m[1];
    p.req_ptr_ok = read(desc + kDesc_ReqActor, &p.req_ptr, sizeof p.req_ptr);
    p.ok = true;
    return p;
}
// ¿Descriptor de carretera con solicitante jugador? (vtable base, +0x3CD == 1, clave 0xA...): el filtro barato.
inline bool marked(const Prov& p) noexcept { return p.ok && p.vt == kVtDescBase && p.mark == 1 && (p.req_key >> 28) == 0xA; }
// El mismo solicitante (clave y puntero local) que la raíz de su unidad.
inline bool same_requester(const Prov& p, uint32_t req_key, uintptr_t req_ptr) noexcept {
    return marked(p) && p.req_key == req_key && p.req_ptr_ok && p.req_ptr == req_ptr;
}

// ---------------------------------------------------------------- veredicto de una raíz (+0x2F0 leído a 0)
enum class Verdict : uint8_t {
    Assign,          // todo se cumple (1: eax := clave del usuario 2; probe: se asignaría)
    NotMarked,       // vtable / marca / solicitante no son de carretera (sólo se cuenta)
    HostRequester,   // la pidió el personaje del anfitrión: nunca (valida el lector en un PC)
    OtherRequester,  // otro jugador, o la clave del usuario 2 desconocida
    PtrMismatch,     // +0x3B8 no es el personaje del usuario 2 (o ilegible)
    NotRoadTroc,     // no nace dentro del Troc 2550 del usuario 2 (otro productor: 0x28981B0, ActivateSpawningPool, o
                     // creación diferida): la procedencia no está demostrada
    NotInRequest,    // revisión §2: su clase de carretera (+0x3D0) no está entre las de la 2550 en curso (procedencia
                     // POSITIVA: el descriptor viene de esta petición)
    Stopped,         // user2_road_control parado (doble control, restore 0 al anfitrión, fallo SEH, bucle): sólo raíces
    SplitLoop,       // revisión §3.2: su clase de carretera ya partió unidades asignadas (bucle nace/retira): no más
    CartRoot,        // revisión §3.2: raíz tipo 5 (carro / caballo) fuera del modo 1 salvo user2_road_carts=1
    HasController,   // actor+0x90 ya puesto o ilegible
    DescUser,        // desc+0x40 nombra un usuario (o ilegible)
    UserChild,       // actor+0xA0: hijo de un usuario (montura, mascota, compañero) o ilegible
    NotHostField,    // el campo no es del anfitrión
    NotOwnerBranch,  // eax no es [campo+0x180]
    Type,            // fuera de la cohorte 3/5/6
    NoKey,           // la clave de usuario del usuario 2 ([pj2+0x90]) desconocida
    SessionUnknown,  // la sesión del usuario 2 no se conoce (auditoría: desconocido nunca cuenta como coincidencia)
    KeyMismatch,     // [pj2+0x90] distinta de la sesión
    UnitRejected,    // su subárbol no es una caravana demostrable (preexamen)
    NoRoom,          // sin registro libre (tabla o marco): registro ANTES de autoridad
    PlayerRides,     // S40: raíz tipo 5 que un jugador ya monta (su padre +0xAC / su montura de mount_sync): nunca
    Count
};
inline const char* verdict_name(Verdict v) noexcept {
    switch (v) {
        case Verdict::Assign: return "asignada";
        case Verdict::NotMarked: return "sin marca de carretera";
        case Verdict::HostRequester: return "pedida por el anfitrión";
        case Verdict::OtherRequester: return "otro solicitante";
        case Verdict::PtrMismatch: return "+0x3B8 no es el personaje del usuario 2";
        case Verdict::NotRoadTroc: return "fuera del Troc 2550 del usuario 2";
        case Verdict::NotInRequest: return "su carretera (+0x3D0) no está en la 2550 en curso";
        case Verdict::SplitLoop: return "su carretera ya partió unidades (bucle)";
        case Verdict::CartRoot: return "raíz tipo 5 fuera del modo 1 (user2_road_carts=0)";
        case Verdict::Stopped: return "parada";
        case Verdict::HasController: return "actor+0x90 ya puesto";
        case Verdict::DescUser: return "desc+0x40 puesto";
        case Verdict::UserChild: return "hijo de un usuario (actor+0xA0)";
        case Verdict::NotHostField: return "no es el campo del anfitrión";
        case Verdict::NotOwnerBranch: return "eax no es el del dueño del campo";
        case Verdict::Type: return "tipo fuera de 3/5/6";
        case Verdict::NoKey: return "clave del usuario 2 desconocida";
        case Verdict::SessionUnknown: return "sesión del usuario 2 desconocida";
        case Verdict::KeyMismatch: return "clave del usuario 2 DISTINTA de la sesión";
        case Verdict::UnitRejected: return "unidad no demostrable";
        case Verdict::NoRoom: return "sin registro libre";
        case Verdict::PlayerRides: return "un jugador lo monta";
        default: return "?";
    }
}
struct RootIn {
    Prov prov;
    uint32_t host_char_key = 0, u2_char_key = 0;
    uintptr_t u2_char = 0;           // local: el puntero del personaje del usuario 2
    bool in_road_troc = false;       // sujeto del Troc = personaje del usuario 2 y su opcode 2550
    int in_request = -1;             // +0x3D0 entre las clases de la 2550 en curso: 1 / 0 / -1 desconocido (no leída)
    bool stopped = false;
    bool road_blocked = false;       // su clase de carretera está parada por bucle de partidas (SplitGuard)
    bool carts_allowed = true;       // modo 1 con user2_road_carts=0: false (probe: true, para medir)
    bool old_ctl_known = false;
    uint32_t old_ctl = 0;
    bool desc_user_known = false;
    uint64_t desc_user = 0;
    bool actor_user_known = false;
    uint64_t actor_user = 0;
    bool host_field = false;
    uint32_t eax = 0, field_owner = 0;
    uint8_t type = 0;
    uint32_t key2 = 0, session_key2 = 0;
    bool player_rides = false;       // S40: un jugador (anfitrión o invitado) ya lo monta (sólo se lee para tipo 5)
};
// S40 carros: [session2] user2_road_carts = 0 | probe | 1 (anfitrión, con user2_road_control=1). 0: raíces tipo 5
// fuera del modo 1 (CartRoot, como la revisión §3.2). probe: sus unidades se abren y se miden enteras («se asignaría»:
// ningún controlador escrito, nunca se retiran) mientras los demás viajeros se asignan. 1: se asignan enteras (raíz,
// jinete / conductor, vehículos enlazados; gimmicks y su carga con el anfitrión). Con user2_road_control=probe todo es
// probe (carros incluidos).
enum class Carts : uint8_t { Off, Probe, On };
inline const char* carts_name(Carts c) noexcept {
    return c == Carts::On ? "1" : c == Carts::Probe ? "probe" : "0";
}
// ¿Admite el veredicto una raíz tipo 5? (en probe de user2_road_control siempre: para medir)
inline bool carts_allowed(bool mode_on, Carts c) noexcept { return !mode_on || c != Carts::Off; }
// ¿Escribe el controlador esta raíz (y su unidad)? Modo 1 y, si es tipo 5, user2_road_carts=1.
inline bool root_writes(bool mode_on, Carts c, uint8_t type) noexcept {
    return mode_on && (type != 5 || c == Carts::On);
}
inline Verdict root_verdict(const RootIn& r) noexcept {
    if (!marked(r.prov)) return Verdict::NotMarked;
    if (r.host_char_key && r.prov.req_key == r.host_char_key) return Verdict::HostRequester;
    if (!r.u2_char_key || r.prov.req_key != r.u2_char_key) return Verdict::OtherRequester;
    if (!r.prov.req_ptr_ok || !r.u2_char || r.prov.req_ptr != r.u2_char) return Verdict::PtrMismatch;
    if (!r.in_road_troc) return Verdict::NotRoadTroc;
    if (r.in_request != 1) return Verdict::NotInRequest;  // desconocida tampoco demuestra nada
    if (r.stopped) return Verdict::Stopped;
    if (r.road_blocked) return Verdict::SplitLoop;
    if (!r.old_ctl_known || r.old_ctl) return Verdict::HasController;
    if (!r.desc_user_known || r.desc_user) return Verdict::DescUser;
    if (!r.actor_user_known || r.actor_user) return Verdict::UserChild;
    if (!r.host_field) return Verdict::NotHostField;
    if (!r.eax || r.eax != r.field_owner) return Verdict::NotOwnerBranch;
    if (!cohort_type(r.type)) return Verdict::Type;
    if (r.type == 5 && !r.carts_allowed) return Verdict::CartRoot;
    if (r.type == 5 && r.player_rides) return Verdict::PlayerRides;
    if (!r.key2) return Verdict::NoKey;
    if (!r.session_key2) return Verdict::SessionUnknown;
    if (r.session_key2 != r.key2) return Verdict::KeyMismatch;
    return Verdict::Assign;
}

// ---------------------------------------------------------------- preexamen de la caravana (antes de EAX)
// La unidad = raíz + su subárbol de envoltorios _Rider / _Simple cuyo interior es un descriptor de carretera del MISMO
// solicitante (clave y puntero), con [interior+0x2F0] == su padre y [interior+0x40] == 0; los gimmicks (InstantGimmick
// bajo _Simple / _AutoSpawnGimmick / _CombinationGimmick) son nodos host-kept y su subárbol no se recorre. Cualquier
// otra cosa (otro envoltorio, interior sin marca o de otro solicitante, lectura fallida) deja la unidad ENTERA con el
// anfitrión.
enum class ScanV : uint8_t { Ok, Unreadable, TooBig, TooDeep, Inconsistent, DescUser, Cycle, NotRider, NotTraveller,
                             Unmarked, BornFirst, Count };
inline const char* scan_name(ScanV v) noexcept {
    switch (v) {
        case ScanV::Ok: return "ok";
        case ScanV::Unreadable: return "subárbol ilegible";
        case ScanV::TooBig: return "más de 15 descendientes o 16 hijos directos";
        case ScanV::TooDeep: return "más profundo que 4";
        case ScanV::Inconsistent: return "interior+0x2F0 no es su padre";
        case ScanV::DescUser: return "interior+0x40 nombra un usuario";
        case ScanV::Cycle: return "ciclo";
        case ScanV::NotRider: return "envoltorio no admitido (ni _Rider/_Simple de viajero ni de gimmick)";
        case ScanV::NotTraveller: return "interior no es un descriptor de carretera";
        case ScanV::Unmarked: return "interior sin marca o de otro solicitante";
        case ScanV::BornFirst: return "un miembro nació antes que su raíz";
        default: return "?";
    }
}
struct ScanNode {
    uintptr_t desc;
    uint8_t parent;  // índice en nodes; kNoIndex = la raíz
    uint8_t depth;   // 1 = hijo directo
    bool gimmick;    // host-kept (su subárbol no se recorre)
};
struct Scan {
    ScanV v = ScanV::Ok;
    uint8_t n = 0, max_depth = 0, bad_depth = 0, gimmicks = 0;
    uint32_t bad_wrap = 0, bad_inner = 0;  // sólo para la línea
    ScanNode nodes[kMaxNodes];             // sin inicializar: sólo valen [0, n)
};
template <class Read>
void scan_unit(Read&& read, uintptr_t root, uintptr_t base, uint64_t image_size, uint32_t req_key, uintptr_t req_ptr,
               Scan& s) noexcept {
    s.v = ScanV::Ok;
    s.n = 0; s.max_depth = 0; s.bad_depth = 0; s.bad_wrap = 0; s.bad_inner = 0; s.gimmicks = 0;
    if (!plausible(root)) { s.v = ScanV::Unreadable; return; }
    for (int at = -1; at < static_cast<int>(s.n); ++at) {
        if (at >= 0 && s.nodes[at].gimmick) continue;  // la carga de un gimmick es suya (host-kept)
        const uintptr_t parent = at < 0 ? root : s.nodes[at].desc;
        const uint8_t depth = at < 0 ? 0 : s.nodes[at].depth;
        uintptr_t data = 0;
        uint32_t count = 0;
        if (!read(parent + kDesc_Children, &data, sizeof data) || !read(parent + kDesc_ChildCount, &count, sizeof count)) {
            s.v = ScanV::Unreadable;
            return;
        }
        if (!count) continue;
        if (count > kMaxVector) { s.v = ScanV::TooBig; return; }
        if (!plausible(data)) { s.v = ScanV::Unreadable; return; }
        if (depth + 1 > kMaxDepth) { s.v = ScanV::TooDeep; return; }
        for (uint32_t i = 0; i < count; ++i) {
            if (s.n >= kMaxNodes) { s.v = ScanV::TooBig; return; }
            uintptr_t w = 0, wvt = 0, inner = 0, back = 0;
            uint64_t user = 0;
            if (!read(data + static_cast<uintptr_t>(i) * 8, &w, sizeof w) || !plausible(w) ||
                !read(w, &wvt, sizeof wvt) || !read(w + kWrap_Inner, &inner, sizeof inner) || !plausible(inner) ||
                !read(inner + kDesc_Parent, &back, sizeof back) || !read(inner + kDesc_User, &user, sizeof user)) {
                s.v = ScanV::Unreadable;
                return;
            }
            const Prov p = read_prov(read, inner, base, image_size);
            const uint32_t wr = rva_in(wvt, base, image_size);
            const auto fail = [&](ScanV v) {
                s.v = v; s.bad_wrap = wr; s.bad_inner = p.vt; s.bad_depth = static_cast<uint8_t>(depth + 1);
            };
            const bool gimmick = p.vt == kVtInstantGimmick && gimmick_wrap(wr);
            if (!gimmick) {
                if (!traveller_wrap(wr)) { fail(ScanV::NotRider); return; }
                if (p.vt != kVtDescBase) { fail(p.vt || p.ok ? ScanV::NotTraveller : ScanV::Unreadable); return; }
                if (!p.ok) { fail(ScanV::Unreadable); return; }
                if (!same_requester(p, req_key, req_ptr)) { fail(ScanV::Unmarked); return; }
            }
            if (back != parent) { s.v = ScanV::Inconsistent; return; }
            if (inner == root) { s.v = ScanV::Cycle; return; }
            for (uint8_t j = 0; j < s.n; ++j) {
                if (s.nodes[j].desc == inner) { s.v = ScanV::Cycle; return; }
            }
            if (user) { s.v = ScanV::DescUser; return; }
            ScanNode& nd = s.nodes[s.n];
            nd.desc = inner;
            nd.parent = at < 0 ? kNoIndex : static_cast<uint8_t>(at);
            nd.depth = static_cast<uint8_t>(depth + 1);
            nd.gimmick = gimmick;
            if (gimmick) ++s.gimmicks;
            ++s.n;
            if (nd.depth > s.max_depth) s.max_depth = nd.depth;
        }
    }
}
// ¿El vector +0x2F8 de `parent` tiene un envoltorio cuyo interior es `child`? 1 / 0 / -1 ilegible; *rider = su
// envoltorio es de viajero (_Rider o _Simple); *wrap = la RVA de su envoltorio (S40: _LinkedVehicle).
template <class Read>
int in_vector(Read&& read, uintptr_t parent, uintptr_t child, uintptr_t base, uint64_t image_size,
              bool* rider, uint32_t* wrap = nullptr) noexcept {
    if (rider) *rider = false;
    if (wrap) *wrap = 0;
    uintptr_t data = 0;
    uint32_t count = 0;
    if (!plausible(parent) || !read(parent + kDesc_Children, &data, sizeof data) ||
        !read(parent + kDesc_ChildCount, &count, sizeof count)) {
        return -1;
    }
    if (!count) return 0;
    if (count > kMaxVector || !plausible(data)) return -1;
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t w = 0, inner = 0, wvt = 0;
        if (!read(data + static_cast<uintptr_t>(i) * 8, &w, sizeof w) || !plausible(w) ||
            !read(w + kWrap_Inner, &inner, sizeof inner)) {
            return -1;
        }
        if (inner != child) continue;
        if (!read(w, &wvt, sizeof wvt)) return -1;
        if (rider) *rider = traveller_wrap(rva_in(wvt, base, image_size));
        if (wrap) *wrap = rva_in(wvt, base, image_size);
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------- un miembro (descriptor con padre de su unidad)
enum class Split : uint8_t {
    None, LinkUnknown, LinkChanged, LateNotInVector, LateNoRoom, NotTraveller, ParentGone, ParentController,
    HasController, DescUser, UserChild, NotHostField, NotOwnerBranch, Type, NoKey, PutFailed, NotBorn,
    LinkedMismatch,  // S40: _LinkedVehicle cuyo descriptor no es el de 0x2797510 para ESTE padre (marca, solicitante,
                     // carretera, +0x490) o padre no tipo 5
    PlayerRides,     // S40: un jugador ya monta el vehículo enlazado
    CargoType,       // S40: hijo de un gimmick (carga) que no es tipo 7 o con otro descriptor
    Count
};
inline const char* split_name(Split w) noexcept {
    switch (w) {
        case Split::None: return "ninguno";
        case Split::LinkUnknown: return "vínculo +0x2F0 ilegible";
        case Split::LinkChanged: return "vínculo +0x2F0 distinto del preexamen";
        case Split::LateNotInVector: return "tardío: no está en el vector de su padre";
        case Split::LateNoRoom: return "tardío: sin registro libre en la unidad";
        case Split::NotTraveller: return "no es un viajero del mismo solicitante (p. ej. gimmick tipo 7)";
        case Split::ParentGone: return "padre no vivo o ilegible";
        case Split::ParentController: return "el padre no tiene el controlador de la unidad";
        case Split::HasController: return "actor+0x90 ya puesto o ilegible";
        case Split::DescUser: return "desc+0x40 puesto o ilegible";
        case Split::UserChild: return "actor+0xA0 puesto o ilegible";
        case Split::NotHostField: return "no es el campo del anfitrión";
        case Split::NotOwnerBranch: return "eax no es el del dueño del campo";
        case Split::Type: return "tipo fuera de 3/5/6";
        case Split::NoKey: return "clave del usuario 2 desconocida o distinta";
        case Split::PutFailed: return "su clave no se pudo registrar";
        case Split::NotBorn: return "un miembro del preexamen no nació en el Troc de su raíz";
        case Split::LinkedMismatch: return "vehículo enlazado que no es el de su padre (marca/solicitante/carretera/+0x490)";
        case Split::PlayerRides: return "un jugador lo monta";
        case Split::CargoType: return "hijo de un gimmick que no es carga tipo 7";
        default: return "?";
    }
}
enum class MemberV : uint8_t { Assign, HostUnit, Split, HostKept };
struct MemberIn {
    bool unit_live = false;      // su unidad está asignada (1) o se asignaría (probe)
    bool reserved = false;       // estaba en el preexamen de su raíz (si no: tardío)
    int in_vector = -1;          // tardío: en el vector de su padre (1 / 0 / -1)
    bool rider = false;          // tardío: su envoltorio es de viajero (_Rider o _Simple)
    bool room = false;           // tardío: queda un registro en la unidad
    bool link_present = false;   // [desc+0x2F0] leído y no 0
    bool link_matches = false;   // == el descriptor del registro de su padre
    bool traveller = false;      // same_requester(su descriptor, solicitante de la unidad)
    int parent_alive = -1;       // [padre+0x60] == su clave (1 / 0 / -1)
    int parent_ctl = -1;         // [padre+0x90] == el controlador de la unidad (probe: 1)
    bool old_ctl_known = false;
    uint32_t old_ctl = 0;
    bool desc_user_known = false;
    uint64_t desc_user = 0;
    bool actor_user_known = false;
    uint64_t actor_user = 0;
    bool host_field = false, owner_branch = false;
    uint8_t type = 0;
    uint32_t key2 = 0, unit_controller = 0;
};
struct MemberOut {
    MemberV v = MemberV::Split;
    Split why = Split::None;
};
inline MemberOut assignable(const MemberIn& m) noexcept;
inline MemberOut member_verdict(const MemberIn& m) noexcept {
    if (!m.unit_live) return {MemberV::HostUnit, Split::None};
    if (!m.link_present) return {MemberV::Split, Split::LinkUnknown};
    if (!m.link_matches) return {MemberV::Split, Split::LinkChanged};
    if (!m.reserved) {
        if (m.in_vector < 0) return {MemberV::Split, Split::LinkUnknown};
        if (m.in_vector == 0) return {MemberV::Split, Split::LateNotInVector};
        if (!m.rider) return {MemberV::Split, Split::NotTraveller};
    }
    if (!m.traveller) return {MemberV::Split, Split::NotTraveller};
    return assignable(m);
}
// S40: la cola común (miembro de carretera o vehículo enlazado): padre vivo con el controlador de la unidad, actor sin
// controlador ni dueño, descriptor sin usuario, campo y rama del anfitrión, cohorte, clave del usuario 2 y registro.
inline MemberOut assignable(const MemberIn& m) noexcept {
    if (m.parent_alive != 1) return {MemberV::Split, Split::ParentGone};
    if (m.parent_ctl != 1) return {MemberV::Split, Split::ParentController};
    if (!m.old_ctl_known || m.old_ctl) return {MemberV::Split, Split::HasController};
    if (!m.desc_user_known || m.desc_user) return {MemberV::Split, Split::DescUser};
    if (!m.actor_user_known || m.actor_user) return {MemberV::Split, Split::UserChild};
    if (!m.host_field) return {MemberV::Split, Split::NotHostField};
    if (!m.owner_branch) return {MemberV::Split, Split::NotOwnerBranch};
    if (!cohort_type(m.type)) return {MemberV::Split, Split::Type};
    if (!m.key2 || m.key2 != m.unit_controller) return {MemberV::Split, Split::NoKey};
    if (!m.reserved && !m.room) return {MemberV::Split, Split::LateNoRoom};
    return {MemberV::Assign, Split::None};
}
// Un gimmick (descriptor InstantGimmick) hijo de un registro de la unidad: con el anfitrión (host-kept, fase B §2.7) si
// es tipo 7; otro descriptor u otro tipo parte la unidad. Nunca se asigna.
inline MemberOut gimmick_verdict(bool instant_gimmick, uint8_t type) noexcept {
    if (!instant_gimmick) return {MemberV::Split, Split::NotTraveller};
    if (type != 7) return {MemberV::Split, Split::Type};
    return {MemberV::HostKept, Split::None};
}
// S40 carros: un hijo SIN marca de carretera (descriptor base sin +0x3CD o InstantGimmick) cuyo +0x2F0 es un registro de
// una unidad abierta. Composición medida (C probe 2, carreta 0xB01002D8 de carretera 0xFA1; fase B prueba 3, 0xB01002A6):
//   raíz tipo 5 (el vehículo que monta el conductor: MountSync rider 0xB01002D9 horse 0xB01002D8)
//   ├─ conductor tipo 3 (_Rider, acompañante marcado: miembro del preexamen)
//   ├─ vehículo enlazado tipo 5 (_LinkedVehicle de 0x2797510, sin marca, tardío)         -> Linked: sigue a la unidad
//   ├─ gimmick tipo 7 (InstantGimmick, tardío)                                            -> Gimmick: host-kept
//   │   └─ carga tipo 7 (descriptor base sin marca; hasta 19, alguno con un hijo propio)  -> Cargo: host-kept
//   └─ gimmicks tipo 7 (InstantGimmick)                                                   -> Gimmick: host-kept
// Gimmicks y su carga se quedan con el anfitrión ENTEROS (registrados para retirarse de hojas a raíz; nunca partidos):
// los tipo 7 no llevan IA (S34: el cliente no controlador sólo envía 2556 de posición para ellos) y los host-kept de los
// caminantes no impidieron que éstos se movieran (noche 8: todos los vivos desplazados). El vehículo enlazado sí lleva
// IA (el cliente del anfitrión, su controlador, envió 2734 para él como para la raíz y el conductor): con otro
// controlador que su raíz, un vehículo físicamente enlazado quedaría quieto lejos del anfitrión. Sigue a la unidad.
enum class ChildKind : uint8_t { Gimmick, Cargo, Linked, Foreign, Count };
inline const char* child_kind_name(ChildKind k) noexcept {
    switch (k) {
        case ChildKind::Gimmick: return "gimmick";
        case ChildKind::Cargo: return "carga";
        case ChildKind::Linked: return "vehículo enlazado";
        case ChildKind::Foreign: return "ajeno";
        default: return "?";
    }
}
struct ChildIn {
    bool carts_rules = true;      // user2_road_carts != 0: carga y vehículo enlazado; 0: la regla de la fase C
    uint32_t vt = 0;              // RVA de la vtable de su descriptor
    uint8_t type = 0;
    bool parent_host_kept = false;  // su padre de descriptor es un gimmick o carga de la unidad
    uint8_t parent_type = 0;
    // Sólo para un posible vehículo enlazado (descriptor base):
    int in_vector = -1;           // en el vector +0x2F8 de su padre (1 / 0 / -1 ilegible)
    uint32_t wrap = 0;            // RVA de su envoltorio en ese vector
    bool fields_ok = false;       // +0x3CC/+0x3CD, +0x3C8, +0x3D0 y +0x490 leídos
    uint8_t mark = 0xFF;          // +0x3CD (0x2797510 escribe word +0x3CC = 0)
    bool req_key_match = false;   // +0x3C8 == el solicitante de la unidad
    bool road_match = false;      // +0x3D0 == la carretera de la raíz
    bool parent_key_match = false;  // +0x490 == la clave del actor de su padre
    bool player_rides = false;
    MemberIn m;                   // la cola común (reserved = false: siempre tardío)
};
struct ChildOut {
    MemberV v = MemberV::Split;
    Split why = Split::None;
    ChildKind kind = ChildKind::Foreign;
};
inline ChildOut child_verdict(const ChildIn& c) noexcept {
    // S40 revisión §1: con user2_road_carts=0, exactamente la regla de la fase C (ef5a6b5): un InstantGimmick tipo 7 es
    // host-kept y todo lo demás (carga incluida, vehículos enlazados de miembros tipo 5 incluidos) parte la unidad.
    if (!c.carts_rules) {
        const MemberOut g = gimmick_verdict(c.vt == kVtInstantGimmick, c.type);
        return {g.v, g.why, c.vt == kVtInstantGimmick ? ChildKind::Gimmick : ChildKind::Foreign};
    }
    if (c.parent_host_kept) {  // la carga de un gimmick: entera con el anfitrión
        if (c.type != 7 || (c.vt != kVtDescBase && c.vt != kVtInstantGimmick)) {
            return {MemberV::Split, Split::CargoType, ChildKind::Cargo};
        }
        return {MemberV::HostKept, Split::None, ChildKind::Cargo};
    }
    if (c.vt == kVtInstantGimmick) {
        const MemberOut g = gimmick_verdict(true, c.type);
        return {g.v, g.why, ChildKind::Gimmick};
    }
    if (c.vt != kVtDescBase || c.wrap != kVtWrapLinkedVehicle) {
        if (c.vt == kVtDescBase && c.in_vector < 0) return {MemberV::Split, Split::LinkUnknown, ChildKind::Foreign};
        return {MemberV::Split, Split::NotTraveller, ChildKind::Foreign};
    }
    // _LinkedVehicle de 0x2797510: el contrato entero, para ESTE padre.
    if (c.in_vector != 1) return {MemberV::Split, Split::LateNotInVector, ChildKind::Linked};
    if (c.parent_type != 5 || !c.fields_ok || c.mark != 0 || !c.req_key_match || !c.road_match ||
        !c.parent_key_match) {
        return {MemberV::Split, Split::LinkedMismatch, ChildKind::Linked};
    }
    if (c.type != 5) return {MemberV::Split, Split::Type, ChildKind::Linked};
    if (c.player_rides) return {MemberV::Split, Split::PlayerRides, ChildKind::Linked};
    if (!c.m.unit_live) return {MemberV::HostUnit, Split::None, ChildKind::Linked};
    MemberIn m = c.m;
    m.reserved = false;
    const MemberOut a = assignable(m);
    return {a.v, a.why, ChildKind::Linked};
}

// ---------------------------------------------------------------- la tabla de unidades (propia, no la de U-e)
// Registro por unidad: raíz + miembros (reservados en el preexamen o tardíos), con época, nacimiento (token propio) y
// estado de limpieza. Las claves sólo identifican a un actor DENTRO de este proceso (servidor); entre PC sólo viaja la
// clave en los paquetes nativos. Un solo escritor a la vez (el llamador sostiene su cerrojo exclusivo).
enum class Clean : uint8_t {
    None,         // vivo (o por nacer)
    Pending,      // retirada pedida, aún sin llamada (o reintento tras un fallo)
    Leaving,      // Logout devolvió 0 o «ya saliendo»: falta confirmar que no está en el SAM
    Gone,         // confirmado fuera (no encontrado, otra clave / controlador, o su baja vista)
    Failed,       // Logout rechazado kMaxTries veces: terminal (visible), sigue asignado
    Unconfirmed,  // sigue en el SAM tras kMaxChecks comprobaciones: terminal (visible)
};
inline const char* clean_name(Clean c) noexcept {
    switch (c) {
        case Clean::None: return "vivo";
        case Clean::Pending: return "pendiente";
        case Clean::Leaving: return "saliendo";
        case Clean::Gone: return "fuera";
        case Clean::Failed: return "FALLIDA";
        case Clean::Unconfirmed: return "SIN CONFIRMAR";
    }
    return "?";
}
inline constexpr uint8_t kMaxTries = 3, kMaxChecks = 10, kMaxPubTries = 10;
struct Rec {
    uintptr_t desc, actor;  // locales, sólo mientras su unidad está abierta (su Troc); 0 después
    uint64_t birth;         // token de nacimiento (0 = reservado, aún no nacido)
    uint64_t born_us, q2005_us, req_us, leave_us, retry_us;
    float x0, z0, moved;    // primera posición de sus Req del invitado y el máximo desplazamiento X/Z (m)
    uint32_t key, ctl;      // ctl: el controlador escrito (el del usuario 2 si assigned; si no, el nativo)
    uint8_t parent;        // índice en Unit::r (kNoIndex = la raíz)
    uint8_t depth, type;
    bool born, late, assigned, removed, driven, pos, host_2005, split, host_kept;
    bool incomplete;       // un hijo suyo (registro ajeno o retenido) no cupo en la unidad: nunca se retira (terminal)
    bool linked;           // S40: vehículo enlazado (_LinkedVehicle de 0x2797510) de su padre
    uint8_t removed_reason;
    Clean clean;
    uint8_t tries, checks, occupied;
};
enum class UState : uint8_t { Free, Open, Live, Retire, Done, Failed };
inline const char* ustate_name(UState s) noexcept {
    switch (s) {
        case UState::Free: return "libre";
        case UState::Open: return "abierta";
        case UState::Live: return "viva";
        case UState::Retire: return "retirándose";
        case UState::Done: return "retirada";
        case UState::Failed: return "FALLIDA";
    }
    return "?";
}
enum class Why : uint8_t { None, ItsLoad, ItsLogout, Its2001, Split, Epoch, Count };
inline const char* why_name(Why w) noexcept {
    switch (w) {
        case Why::None: return "-";
        case Why::ItsLoad: return "su carga (viaje rápido / teletransporte / cambio de campo)";
        case Why::ItsLogout: return "su Logout";
        case Why::Its2001: return "su despedida 2001";
        case Why::Split: return "unidad partida";
        case Why::Epoch: return "otra época (reconexión)";
        default: return "?";
    }
}
struct Unit {
    uint64_t birth;        // token de la unidad (= nacimiento de su raíz)
    uint64_t troc;         // id del Troc que la abrió (0 = cerrada)
    uint64_t opened_us, retire_us, done_us;
    uint32_t epoch, req_key, controller;  // controller: la clave del usuario 2 escrita (1) / la que se escribiría
    uintptr_t req_ptr;     // local, sólo mientras está abierta
    uint32_t tid;
    UState state;
    Why why;
    bool assigned;         // modo 1: autoridad escrita (si no: probe, «se asignaría»)
    bool split, pub_cancelled, retire_on_close;
    uint8_t n, nodes, born, late, pub_tries;
    uint32_t road;         // clase de carretera de la raíz (+0x3D0)
    // Revisión §4.1: la cancelación de publicación se reintenta con espaciado y un presupuesto de TIEMPO; pass_us: su
    // última pasada (las pasadas eligen las menos recientes, no las primeras por índice).
    uint64_t pub_first_us, pub_next_us, pass_us;
    Split split_why;
    Rec r[kMaxUnit];
};
struct Loc {
    int unit = -1, rec = -1;
    explicit operator bool() const noexcept { return unit >= 0 && rec >= 0; }
};
inline void reset_rec(Rec& x) noexcept {
    x.desc = 0; x.actor = 0; x.birth = 0; x.born_us = 0; x.q2005_us = 0; x.req_us = 0; x.leave_us = 0; x.retry_us = 0;
    x.x0 = 0; x.z0 = 0; x.moved = 0; x.key = 0; x.ctl = 0;
    x.parent = kNoIndex; x.depth = 0; x.type = 0; x.born = false; x.late = false; x.assigned = false;
    x.removed = false; x.driven = false; x.pos = false; x.host_2005 = false; x.split = false; x.host_kept = false; x.incomplete = false; x.linked = false; x.removed_reason = 0;
    x.clean = Clean::None; x.tries = 0; x.checks = 0; x.occupied = 0;
}

// Índice clave -> (unidad, registro). Direccionamiento abierto; una clave nunca se borra de la cadena (su valor pasa a
// 0), así las cadenas siguen enteras; lleno, se reconstruye con las claves vivas (Table::rebuild).
template <size_t N>
struct Index {
    static_assert(N >= 16 && (N & (N - 1)) == 0, "N must be a power of two");
    uint32_t key[N];
    uint16_t val[N];  // (unidad << kRecBits | registro) + 1; 0 = ninguno
    size_t used = 0;
    void clear() noexcept {
        for (size_t i = 0; i < N; ++i) { key[i] = 0; val[i] = 0; }
        used = 0;
    }
    static size_t at(uint32_t k) noexcept {
        return static_cast<size_t>((static_cast<uint64_t>(k) * 0x9E3779B97F4A7C15ull) >> 40) & (N - 1);
    }
    uint16_t find(uint32_t k) const noexcept {
        if (!k) return 0;
        for (size_t p = 0, i = at(k); p < N; ++p, i = (i + 1) & (N - 1)) {
            if (key[i] == k) return val[i];
            if (!key[i]) return 0;
        }
        return 0;
    }
    bool full() const noexcept { return used >= N * 3 / 4; }
    bool put(uint32_t k, uint16_t v) noexcept {
        if (!k) return false;
        for (size_t p = 0, i = at(k); p < N; ++p, i = (i + 1) & (N - 1)) {
            if (key[i] == k) { val[i] = v; return true; }
            if (!key[i]) {
                if (full()) return false;
                key[i] = k; val[i] = v; ++used;
                return true;
            }
        }
        return false;
    }
    void erase(uint32_t k, uint16_t v) noexcept {  // sólo si todavía apunta a (unidad, registro)
        if (!k) return;
        for (size_t p = 0, i = at(k); p < N; ++p, i = (i + 1) & (N - 1)) {
            if (key[i] == k) { if (val[i] == v) val[i] = 0; return; }
            if (!key[i]) return;
        }
    }
};
// S40: 6 bits de registro (kMaxUnit 48) y 10 de unidad.
inline constexpr unsigned kRecBits = 6;
inline uint16_t pack_loc(size_t unit, size_t rec) noexcept {
    return static_cast<uint16_t>((unit << kRecBits | rec) + 1);
}
inline Loc unpack_loc(uint16_t v) noexcept {
    Loc l;
    if (!v) return l;
    l.unit = (v - 1) >> kRecBits;
    l.rec = (v - 1) & ((1u << kRecBits) - 1);
    return l;
}

template <size_t U, size_t K = 8192>
struct Table {
    static_assert(U <= 1023, "pack_loc: 10 bits de unidad");
    static_assert(kMaxUnit <= (1u << kRecBits), "pack_loc: 6 bits de registro");
    Unit u[U];
    Index<K> index;
    size_t live = 0;           // unidades no libres
    uint64_t overflow = 0;     // aperturas rechazadas por capacidad
    void reset() noexcept {
        for (size_t i = 0; i < U; ++i) { u[i].state = UState::Free; u[i].n = 0; u[i].troc = 0; }
        index.clear();
        live = 0;
    }
    Table() noexcept { reset(); }
    static constexpr size_t capacity() noexcept { return U; }
    // Registro ANTES de autoridad: la unidad entera (raíz + nodos del preexamen) o nada. -1: sin sitio.
    int open(uintptr_t root_desc, uint32_t root_key, uint64_t birth, uint32_t epoch, uint32_t req_key, uintptr_t req_ptr,
             uint32_t controller, bool assigned, uint64_t troc, uint32_t tid, uint64_t now, const Scan& s) noexcept {
        if (s.n > kMaxNodes || static_cast<size_t>(s.n) + 1 > kMaxUnit) return -1;
        if (index.full()) rebuild();
        if (index.full()) { ++overflow; return -1; }
        size_t i = 0;
        while (i < U && u[i].state != UState::Free) ++i;
        if (i == U) { ++overflow; return -1; }
        Unit& x = u[i];
        x.birth = birth; x.troc = troc; x.opened_us = now; x.retire_us = 0; x.done_us = 0; x.epoch = epoch;
        x.req_key = req_key; x.controller = controller; x.req_ptr = req_ptr; x.tid = tid; x.state = UState::Open;
        x.why = Why::None; x.assigned = assigned; x.split = false; x.pub_cancelled = false;
        x.retire_on_close = false; x.road = 0; x.pub_first_us = 0; x.pub_next_us = 0; x.pass_us = 0;
        x.n = static_cast<uint8_t>(1 + s.n); x.nodes = s.n; x.born = 0; x.late = 0; x.pub_tries = 0;
        x.split_why = Split::None;
        for (size_t k = 0; k < x.n; ++k) reset_rec(x.r[k]);
        // Fase C probe (15-10): sin el descriptor de la raíz ningún hijo directo la encontraba (52 «sin unidad»).
        x.r[0].desc = root_desc;
        x.r[0].key = root_key;
        x.r[0].depth = 0;
        for (uint8_t k = 0; k < s.n; ++k) {
            Rec& c = x.r[1 + k];
            c.desc = s.nodes[k].desc;
            c.parent = static_cast<uint8_t>(s.nodes[k].parent == kNoIndex ? 0 : 1 + s.nodes[k].parent);
            c.depth = s.nodes[k].depth;
            c.host_kept = s.nodes[k].gimmick;
        }
        if (!index.put(root_key, pack_loc(i, 0))) {
            x.state = UState::Free;
            ++overflow;
            return -1;
        }
        ++live;
        return static_cast<int>(i);
    }
    // Un tardío con su padre en la unidad (pertenencia ya demostrada). -1: sin registro.
    int add_late(size_t ui, uintptr_t desc, uint8_t parent) noexcept {
        Unit& x = u[ui];
        if (x.n >= kMaxUnit || parent >= x.n) return -1;
        Rec& c = x.r[x.n];
        reset_rec(c);
        c.desc = desc; c.parent = parent; c.depth = static_cast<uint8_t>(x.r[parent].depth + 1); c.late = true;
        ++x.late;
        return x.n++;
    }
    // Su nacimiento (antes de cambiar EAX: si la clave no cabe en el índice, no se asigna).
    bool born(size_t ui, size_t ri, uint32_t key, uint64_t birth, uint8_t type, bool assigned, uint64_t now) noexcept {
        Unit& x = u[ui];
        Rec& c = x.r[ri];
        if (ri && index.full()) rebuild();  // revisión §6: un miembro justo en el umbral no parte su unidad
        if (ri && !index.put(key, pack_loc(ui, ri))) return false;
        c.key = key; c.birth = birth; c.type = type; c.born = true; c.assigned = assigned; c.born_us = now;
        if (x.born < 0xFF) ++x.born;
        return true;
    }
    Loc find(uint32_t key) const noexcept {
        const Loc l = unpack_loc(index.find(key));
        if (!l || static_cast<size_t>(l.unit) >= U) return Loc{};
        const Unit& x = u[l.unit];
        if (x.state == UState::Free || l.rec >= x.n || x.r[l.rec].key != key) return Loc{};
        return l;
    }
    // Descriptor -> registro dentro de una unidad abierta (sólo su hilo, mientras su Troc vive).
    int find_desc(size_t ui, uintptr_t desc) const noexcept {
        const Unit& x = u[ui];
        if (!desc || x.state != UState::Open) return -1;
        for (uint8_t k = 0; k < x.n; ++k) {
            if (x.r[k].desc == desc) return k;
        }
        return -1;
    }
    // Fin de su Troc: ningún puntero sobrevive. Un miembro del preexamen que no nació en el Troc de su raíz deja la unidad
    // PARTIDA (nunca «ya vendrá»: nacería fuera, con el anfitrión). Una unidad partida (modo 1) o con una retirada pedida
    // mientras estaba abierta pasa a retirarse. Devuelve cuántos reservados no nacieron.
    size_t close(size_t ui, uint64_t now) noexcept {
        Unit& x = u[ui];
        if (x.state != UState::Open) return 0;
        size_t unborn = 0;
        for (uint8_t k = 0; k < x.n; ++k) {
            x.r[k].desc = 0;
            x.r[k].actor = 0;
            if (!x.r[k].born && !x.r[k].late && !x.r[k].host_kept) ++unborn;  // un gimmick puede nacer después
        }
        x.req_ptr = 0;
        x.troc = 0;
        x.state = UState::Live;
        if (unborn) {
            if (!x.split) x.split_why = Split::NotBorn;
            x.split = true;
        }
        if (x.assigned && (x.split || x.retire_on_close)) {
            x.state = UState::Retire;
            if (x.why == Why::None) x.why = Why::Split;
            if (!x.retire_us) x.retire_us = now;
        }
        return unborn;
    }
    void mark_split(size_t ui, Split why, uint64_t now) noexcept {
        Unit& x = u[ui];
        if (!x.split) x.split_why = why;
        x.split = true;
        if (x.assigned && x.state == UState::Live) {
            x.state = UState::Retire;
            x.why = Why::Split;
            x.retire_us = now;
        }
    }
    // Retirada pedida (su carga / su Logout / otra época): las unidades ASIGNADAS vivas (o abiertas: al cerrarse su
    // Troc) abiertas antes de `now`. Devuelve cuántas.
    size_t request(Why why, uint64_t now) noexcept {
        size_t n = 0;
        for (size_t i = 0; i < U; ++i) {
            Unit& x = u[i];
            if (!x.assigned || x.opened_us > now) continue;
            if (x.state == UState::Live) {
                x.state = UState::Retire;
            } else if (x.state == UState::Open) {
                x.retire_on_close = true;
            } else {
                continue;
            }
            x.why = why;
            x.retire_us = now;
            ++n;
        }
        return n;
    }
    // Otra época (reconexión): las unidades ASIGNADAS vivas de las épocas anteriores se retiran (su controlador es el
    // usuario 2 que se fue; la limpieza revalida clave y controlador).
    size_t request_epoch(uint32_t epoch, uint64_t now) noexcept {
        size_t n = 0;
        for (size_t i = 0; i < U; ++i) {
            Unit& x = u[i];
            if (!x.assigned || x.epoch == epoch || x.state != UState::Live) continue;
            x.state = UState::Retire;
            x.why = Why::Epoch;
            x.retire_us = now;
            ++n;
        }
        return n;
    }
    bool pending() const noexcept {
        for (size_t i = 0; i < U; ++i) {
            if (u[i].state == UState::Retire) return true;
        }
        return false;
    }
    // La unidad más antigua por retirar (o -1).
    int next_retire() const noexcept {
        int best = -1;
        for (size_t i = 0; i < U; ++i) {
            if (u[i].state != UState::Retire) continue;
            if (best < 0 || u[i].retire_us < u[best].retire_us) best = static_cast<int>(i);
        }
        return best;
    }
    // Una baja nativa (o la entrada de la nuestra: el llamador la ignora).
    Loc on_logout(uint32_t key, uint8_t reason) noexcept {
        const Loc l = find(key);
        if (!l) return l;
        Rec& c = u[l.unit].r[l.rec];
        if (!c.born || c.removed) return l;
        c.removed = true;
        c.removed_reason = reason;
        if (c.clean != Clean::Failed && c.clean != Clean::Unconfirmed) c.clean = Clean::Gone;
        return l;
    }
    // Una unidad terminada: todos sus nacidos fuera (Done) o alguno terminal (Failed). Devuelve el nuevo estado.
    UState settle(size_t ui, uint64_t now) noexcept {
        Unit& x = u[ui];
        if (x.state != UState::Retire) return x.state;
        bool all = true, failed = false;
        for (uint8_t k = 0; k < x.n; ++k) {
            const Rec& c = x.r[k];
            if (!c.born) continue;
            if (c.clean == Clean::Failed || c.clean == Clean::Unconfirmed) failed = true;
            else if (c.clean != Clean::Gone) all = false;
        }
        if (!all) return x.state;
        x.state = failed ? UState::Failed : UState::Done;
        x.done_us = now;
        return x.state;
    }
    void free_unit(size_t ui) noexcept {
        Unit& x = u[ui];
        if (x.state == UState::Free) return;
        for (uint8_t k = 0; k < x.n; ++k) {
            if (x.r[k].key) index.erase(x.r[k].key, pack_loc(ui, k));
        }
        x.state = UState::Free;
        x.n = 0;
        if (live) --live;
    }
    // Borrado (hilo del módulo, cada segundo). Nunca una unidad abierta o retirándose. Viva o FALLIDA: cuando ya no le
    // queda nadie vivo (bajas nativas); probe: además de otra época o pasados kStaleUs. Retirada: enseguida.
    static constexpr uint64_t kStaleUs = 600'000'000;
    static bool anyone_alive(const Unit& x) noexcept {
        for (uint8_t k = 0; k < x.n; ++k) {
            if (x.r[k].born && !x.r[k].removed && x.r[k].clean != Clean::Gone) return true;
        }
        return false;
    }
    // Una unidad abierta cuyo Troc nunca terminó (una excepción saltó su salida y su hilo no volvió a abrir otro a esa
    // profundidad): pasado este plazo se cierra como al final de su Troc (sus punteros se olvidan; se retira si toca).
    static constexpr uint64_t kOpenStaleUs = 60'000'000;
    size_t prune(uint32_t epoch, uint64_t now) noexcept {
        size_t n = 0;
        for (size_t i = 0; i < U; ++i) {
            Unit& x = u[i];
            if (x.state == UState::Open && now >= x.opened_us + kOpenStaleUs) close(i, now);
            bool drop = false;
            switch (x.state) {
                case UState::Free: case UState::Open: case UState::Retire: break;
                case UState::Done: drop = true; break;
                case UState::Live:
                case UState::Failed:
                    drop = !anyone_alive(x) ||
                           (!x.assigned && (x.epoch != epoch || now >= x.opened_us + kStaleUs));
                    break;
            }
            if (drop) { free_unit(i); ++n; }
        }
        return n;
    }
    // Las claves vivas otra vez en un índice limpio (el índice nunca borra claves de sus cadenas).
    void rebuild() noexcept {
        index.clear();
        for (size_t i = 0; i < U; ++i) {
            const Unit& x = u[i];
            if (x.state == UState::Free) continue;
            for (uint8_t k = 0; k < x.n; ++k) {
                if (x.r[k].key && (k == 0 || x.r[k].born)) index.put(x.r[k].key, pack_loc(i, k));
            }
        }
    }
};

// ---------------------------------------------------------------- S40: composición de una caravana (sonda por clase)
// Lo que nació de una unidad, por papel: jinetes / conductores (tipo 3 colgado de un tipo 5), vehículos enlazados,
// gimmicks (host-kept colgados de un viajero) y su carga (host-kept colgados de un host-kept), hijos ajenos (partidos).
struct Composition {
    uint8_t n = 0, born = 0, root_type = 0, riders = 0, linked = 0, linked_assigned = 0, gimmicks = 0, cargo = 0,
            foreign = 0, max_depth = 0;
    bool incomplete = false;
};
inline Composition compose(const Unit& x) noexcept {
    Composition o;
    o.n = x.n;
    for (uint8_t k = 0; k < x.n && k < kMaxUnit; ++k) {
        const Rec& c = x.r[k];
        if (!c.born) continue;
        ++o.born;
        if (c.incomplete) o.incomplete = true;
        if (c.depth > o.max_depth) o.max_depth = c.depth;
        if (k == 0) {
            o.root_type = c.type;
            continue;
        }
        const bool parent_ok = c.parent < x.n;
        const Rec* p = parent_ok ? &x.r[c.parent] : nullptr;
        if (c.host_kept) {
            ++(p && p->host_kept ? o.cargo : o.gimmicks);
        } else if (c.linked) {
            ++o.linked;
            if (c.assigned) ++o.linked_assigned;
        } else if (c.split) {
            ++o.foreign;
        } else if (c.type == 3 && p && p->type == 5 && !p->host_kept) {
            ++o.riders;
        }
    }
    return o;
}
// Por clase de carretera (+0x3D0 de la raíz), al cerrarse cada unidad (bajo el cerrojo de la tabla): unidades
// asignadas / que se asignarían, completas / partidas (por motivo), el máximo de registros y de carga, y sus papeles.
// Se llama también desde el mid (el sellado de un lote cierra las unidades del anterior): sin CRT. Las clases nunca se
// vacían ni se reutilizan, así que una clase nueva ya está a cero (el libro es estático o se crea inicializado a cero)
// y find() no pone nada a cero (un bucle de ceros podría volverse un memset).
inline constexpr size_t kCaravanClasses = 8;
struct CaravanClass {
    uint32_t road = 0;
    uint8_t root_type = 0;  // el tipo de su última raíz
    uint64_t units = 0, assigned = 0, complete = 0, split = 0, incomplete = 0;
    uint64_t why[static_cast<size_t>(Split::Count)] = {};
    uint32_t max_n = 0, max_cargo = 0, max_depth = 0;
    uint64_t riders = 0, linked = 0, linked_assigned = 0, gimmicks = 0, cargo = 0, foreign = 0;
};
struct CaravanBook {
    CaravanClass c[kCaravanClasses];
    uint8_t n = 0;
    uint64_t other_units = 0;  // clases de más
    CaravanClass* find(uint32_t road) noexcept {
        for (uint8_t i = 0; i < n; ++i) if (c[i].road == road) return &c[i];
        if (n >= kCaravanClasses) return nullptr;
        CaravanClass& x = c[n++];
        x.road = road;
        return &x;
    }
    void note(const Unit& u) noexcept {
        CaravanClass* x = find(u.road);
        if (!x) { ++other_units; return; }
        const Composition o = compose(u);
        x->root_type = o.root_type;
        ++x->units;
        if (u.assigned) ++x->assigned;
        if (u.split) {
            ++x->split;
            const size_t w = static_cast<size_t>(u.split_why);
            if (w < static_cast<size_t>(Split::Count)) ++x->why[w];
        } else {
            ++x->complete;
        }
        if (o.incomplete) ++x->incomplete;
        if (o.n > x->max_n) x->max_n = o.n;
        if (o.cargo > x->max_cargo) x->max_cargo = o.cargo;
        if (o.max_depth > x->max_depth) x->max_depth = o.max_depth;
        x->riders += o.riders; x->linked += o.linked; x->linked_assigned += o.linked_assigned;
        x->gimmicks += o.gimmicks; x->cargo += o.cargo; x->foreign += o.foreign;
    }
};

// ---------------------------------------------------------------- la retirada de una unidad (hojas a raíz)
// El plan de una pasada: los registros nacidos aún no fuera, el más profundo primero. Un registro que no salió en esta
// pasada (fallo, reintento, aún saliendo) bloquea a su padre: nunca se retira un padre con un hijo suyo vivo.
struct Plan {
    uint8_t idx[kMaxUnit];
    uint8_t n = 0;
};
inline void leaves_first(const Unit& x, Plan& p) noexcept {
    p.n = 0;
    uint8_t top = 0;  // revisión §4.4: la profundidad real (un tardío de un tardío puede pasar de 5)
    for (uint8_t k = 0; k < x.n; ++k) top = x.r[k].depth > top ? x.r[k].depth : top;
    for (uint16_t d = static_cast<uint16_t>(top) + 1; d-- > 0;) {
        for (uint8_t k = 0; k < x.n; ++k) {
            const Rec& c = x.r[k];
            if (c.depth != d || !c.born || c.clean == Clean::Gone || c.clean == Clean::Failed ||
                c.clean == Clean::Unconfirmed) {
                continue;
            }
            p.idx[p.n++] = k;
        }
    }
}
// ¿Puede retirarse `k` ya? Free: ningún descendiente nacido sigue sin salir. Wait: alguno vivo, pendiente o saliendo
// (otra pasada). Terminal: los que quedan son terminales (FALLIDA / SIN CONFIRMAR): `k` tampoco se retira nunca (nunca
// un padre retirado con un hijo suyo vivo) y queda terminal.
enum class Block : uint8_t { Free, Wait, Terminal };
inline Block block_state(const Unit& x, uint8_t k) noexcept {
    if (x.r[k].incomplete) return Block::Terminal;  // revisión §3.2: un hijo suyo sin registro: nunca se retira
    bool any = false, wait = false;
    for (uint8_t i = 0; i < x.n; ++i) {
        if (i == k || !x.r[i].born || x.r[i].clean == Clean::Gone) continue;
        bool desc = false;
        for (uint8_t a = x.r[i].parent, hops = 0; a != kNoIndex && a < x.n && hops < kMaxUnit; a = x.r[a].parent, ++hops) {
            if (a == k) { desc = true; break; }
        }
        if (!desc) continue;
        any = true;
        if (x.r[i].clean != Clean::Failed && x.r[i].clean != Clean::Unconfirmed) wait = true;
    }
    return !any ? Block::Free : wait ? Block::Wait : Block::Terminal;
}
inline bool blocked(const Unit& x, uint8_t k) noexcept { return block_state(x, k) != Block::Free; }
// Resultados de la llamada nativa (SAM::FindActor + ServerActor::Logout), ESTRICTOS (auditoría: un error desconocido
// de Logout nunca cuenta como hecho).
// S40 revisión: RideUnknown = un tipo 5 sin prueba de que nadie lo monta (ni los asientos del vehículo ni los vínculos
// de montura de los dos personajes legibles): nunca Logout, se reintenta como Occupied (acotado, terminal visible).
// KeptChanged = un hijo host-kept (gimmick / carga) que sigue vivo con otro controlador o dueño: no es nuestro, pero
// tampoco «fuera»: terminal, y su padre nunca se retira con él vivo.
enum class Call : uint8_t {
    Done, AlreadyLeaving, NotFound, NotOurs, Present, Unreadable, Refused, Fault, Occupied, RideUnknown, KeptChanged,
    Count
};
inline const char* call_name(Call c) noexcept {
    switch (c) {
        case Call::Done: return "Logout 0";
        case Call::AlreadyLeaving: return "ya saliendo";
        case Call::NotFound: return "no está en el SAM";
        case Call::NotOurs: return "otra clave o controlador";
        case Call::Present: return "sigue en el SAM";
        case Call::Unreadable: return "clave/controlador ilegibles";
        case Call::Refused: return "Logout rechazado";
        case Call::Fault: return "excepción";
        case Call::Occupied: return "un jugador lo monta";
        case Call::RideUnknown: return "tipo 5 sin prueba de que nadie lo monta";
        case Call::KeptChanged: return "host-kept vivo con otro controlador o dueño";
        default: return "?";
    }
}
// Un NotOurs de un registro host-kept (sigue en el SAM con su clave, pero con otro controlador o dueño): KeptChanged.
inline Call kept_result(Call r, bool host_kept) noexcept {
    return r == Call::NotOurs && host_kept ? Call::KeptChanged : r;
}
inline Call logout_call(uint32_t out, uint32_t already_logouting) noexcept {
    if (!out) return Call::Done;
    if (already_logouting && out == already_logouting) return Call::AlreadyLeaving;
    return Call::Refused;
}
// Confirmación: tras Logout 0 / «ya saliendo», el actor debe dejar de estar en el SAM; si sigue (con nuestra clave y
// controlador) pasado este plazo, la retirada queda SIN CONFIRMAR (terminal, visible). Plazo experimental.
inline constexpr uint64_t kConfirmUs = 30'000'000, kRetryUs = 1'000'000;
// Un jugador lo monta (revisión §3.3): nunca Logout; se reintenta cada segundo hasta kMaxOccupied veces (terminal).
inline constexpr uint8_t kMaxOccupied = 120;
// El efecto de una llamada (Logout) o de una comprobación (`check`: sólo SAM::FindActor) sobre el registro.
inline void apply_call(Rec& c, Call r, bool check, uint64_t now) noexcept {
    switch (r) {
        case Call::NotFound:
        case Call::NotOurs:
            c.clean = Clean::Gone;
            return;
        case Call::Done:
        case Call::AlreadyLeaving:
            if (c.clean != Clean::Leaving) c.leave_us = now;
            c.clean = Clean::Leaving;
            return;
        case Call::Present:
        case Call::Unreadable:
            if (check || c.clean == Clean::Leaving) {
                if (c.checks < 0xFF) ++c.checks;
                if (c.clean == Clean::Leaving && c.leave_us && now >= c.leave_us + kConfirmUs) c.clean = Clean::Unconfirmed;
                return;
            }
            [[fallthrough]];
        case Call::Refused:
            if (++c.tries >= kMaxTries) {
                c.clean = Clean::Failed;
            } else {
                c.clean = Clean::Pending;
                c.retry_us = now + kRetryUs;
            }
            return;
        case Call::Fault:
        case Call::KeptChanged:
            c.clean = Clean::Failed;
            return;
        case Call::Occupied:
        case Call::RideUnknown:
            if (++c.occupied >= kMaxOccupied) {
                c.clean = Clean::Failed;
            } else {
                if (c.clean != Clean::Leaving) c.clean = Clean::Pending;
                c.retry_us = now + kRetryUs;
            }
            return;
        default:
            return;
    }
}

// ---------------------------------------------------------------- S40 revisión: ¿lo monta un jugador? (antes de Logout)
// El componente de vehículo como lo lee mount_sync en el cliente (read_pair, build 2976): comps = [actor+0x68],
// vc = [comps+0x48] con [vc+0x08] == el actor (el puntero de vuelta valida la forma: sin él, «desconocido»), asientos u32
// en vc+0x48 (16), reservas u32 en vc+0x88 (16), recuento u8 en vc+0xC8; el vínculo del jinete [vc+0x18] con el
// vehículo reservado +0x08 y el actual +0x10. En el servidor la misma forma es [H] (CommonVehicle 0x2228260 es común;
// el +0x68 de componentes del actor del servidor sí está demostrado: jerarquía [[actor+0x68]+0x1B8]); por eso sólo
// cuenta lo que pasa la validación del puntero de vuelta, y lo que no la pasa es «desconocido», nunca «libre».
inline constexpr uint32_t kActor_Comps = 0x68, kComps_Vehicle = 0x48, kVc_Actor = 0x08, kVc_Link = 0x18,
                          kVc_Seats = 0x48, kVc_Reserves = 0x88, kVc_Count = 0xC8, kLink_ResVehicle = 0x08,
                          kLink_Vehicle = 0x10;
inline constexpr uint32_t kVcSeats = 16;
inline bool player_key(uint32_t k) noexcept { return (k >> 28) == 0xA; }
struct SeatsRead {
    bool ok = false;      // componente validado y asientos / reservas / recuento leídos
    bool player = false;  // algún asiento o reserva con una clave de personaje (0xA…)
    uint8_t count = 0;
    uint32_t who = 0;     // la primera clave de personaje vista
};
template <class Read>
SeatsRead read_seats(Read&& read, uintptr_t actor) noexcept {
    SeatsRead s;
    uintptr_t comps = 0, vc = 0, back = 0;
    if (!plausible(actor) || !read(actor + kActor_Comps, &comps, sizeof comps) || !plausible(comps) ||
        !read(comps + kComps_Vehicle, &vc, sizeof vc) || !plausible(vc) || !read(vc + kVc_Actor, &back, sizeof back) ||
        back != actor) {
        return s;
    }
    uint32_t seats[kVcSeats], res[kVcSeats];
    if (!read(vc + kVc_Seats, seats, sizeof seats) || !read(vc + kVc_Reserves, res, sizeof res) ||
        !read(vc + kVc_Count, &s.count, sizeof s.count) || s.count > kVcSeats) {
        return s;
    }
    s.ok = true;
    for (uint32_t i = 0; i < kVcSeats; ++i) {
        const uint32_t k = player_key(seats[i]) ? seats[i] : player_key(res[i]) ? res[i] : 0;
        if (k && !s.player) { s.player = true; s.who = k; }
    }
    return s;
}
// El vínculo de montura de un personaje (actor del servidor): ok = componente validado y vínculo leído (sin vínculo =
// no monta nada).
struct CharLink {
    bool present = false;  // hay personaje
    bool ok = false;
    uint32_t cur = 0, res = 0;
};
template <class Read>
CharLink read_char_link(Read&& read, uintptr_t chr) noexcept {
    CharLink l;
    l.present = chr != 0;
    if (!l.present) return l;
    uintptr_t comps = 0, vc = 0, back = 0, link = 0;
    if (!plausible(chr) || !read(chr + kActor_Comps, &comps, sizeof comps) || !plausible(comps) ||
        !read(comps + kComps_Vehicle, &vc, sizeof vc) || !plausible(vc) || !read(vc + kVc_Actor, &back, sizeof back) ||
        back != chr || !read(vc + kVc_Link, &link, sizeof link)) {
        return l;
    }
    if (link && (!plausible(link) || !read(link + kLink_Vehicle, &l.cur, sizeof l.cur) ||
                 !read(link + kLink_ResVehicle, &l.res, sizeof l.res))) {
        return l;
    }
    l.ok = true;
    return l;
}
// El veredicto antes del Logout de `key`. Cualquier fuente positiva -> Rides: el padre serializado (+0xAC) de un
// personaje, mount_sync (el par armado de este PC o el padre de la última acción del otro jugador), el vínculo de
// montura de un personaje o un asiento / reserva del propio vehículo. Un tipo 5 sólo es Free con prueba doble: los
// asientos del vehículo validados Y los vínculos de los personajes presentes validados (ninguno lo nombra). Si falta
// cualquiera de las dos, Unknown (nunca se supone libre; la sonda mide si las dos se leen antes de user2_road_carts=1).
enum class Ride : uint8_t { Free, Rides, Unknown };
inline Ride ride_check(uint32_t key, bool type5, bool parent_hit, bool mount_sync_hit, const CharLink (&chars)[2],
                       const SeatsRead& seats) noexcept {
    if (parent_hit || mount_sync_hit) return Ride::Rides;
    bool links_ok = true;
    for (const CharLink& l : chars) {
        if (!l.present) continue;
        if (l.ok && key && (l.cur == key || l.res == key)) return Ride::Rides;
        if (!l.ok) links_ok = false;
    }
    if (seats.ok && seats.player) return Ride::Rides;
    if (!type5) return Ride::Free;
    return seats.ok && links_ok ? Ride::Free : Ride::Unknown;
}

// ---------------------------------------------------------------- cancelación de publicación: espaciado y presupuesto
// Revisión §4.1: child_hold_retire_family se niega de forma atómica por motivos transitorios (una publicación en vuelo)
// o estructurales (un hijo retenido en observer_hold_family fuera de la lista). Antes: 10 negativas (~1 s) y la unidad
// FALLIDA sin ningún Logout. Ahora: espaciado creciente (1, 2, 4, 5, 5… s) y FALLIDA sólo tras kPubBudgetUs.
inline constexpr uint64_t kPubBudgetUs = 60'000'000;
enum class PubNext : uint8_t { Retry, Failed };
inline PubNext pub_refused(Unit& x, uint64_t now) noexcept {
    if (!x.pub_first_us) x.pub_first_us = now;
    if (x.pub_tries < 0xFF) ++x.pub_tries;
    if (now >= x.pub_first_us + kPubBudgetUs) return PubNext::Failed;
    const uint64_t shift = x.pub_tries > 3 ? 3 : x.pub_tries - 1u;
    uint64_t wait = kRetryUs << shift;
    if (wait > 5 * kRetryUs) wait = 5 * kRetryUs;
    x.pub_next_us = now + wait;
    return PubNext::Retry;
}

// ---------------------------------------------------------------- bucle de partidas (revisión §3.2)
// Una unidad asignada que se parte se retira entera; si su clase de carretera (+0x3D0) se parte siempre (la carreta con
// carga), el cliente la vuelve a pedir y se repite (el patrón de S31). Parada automática y local: una clase con
// kClassSplits partidas en kSplitWindowUs deja de asignarse; kGlobalSplits en total paran la pieza (Stop::SplitLoop).
// Umbrales experimentales (probe los mide como «pararía»).
inline constexpr uint64_t kSplitWindowUs = 600'000'000;
inline constexpr uint8_t kClassSplits = 2, kGlobalSplits = 6;
struct SplitGuard {
    struct Cls { uint32_t road; uint64_t t[kGlobalSplits]; uint8_t n; bool blocked; };
    Cls c[8];
    uint8_t nc = 0;
    uint64_t all[kGlobalSplits] = {};
    uint8_t na = 0;
    bool stop = false;
    static uint8_t recent(const uint64_t* t, uint8_t n, uint64_t now) noexcept {
        uint8_t k = 0;
        for (uint8_t i = 0; i < n; ++i) k = static_cast<uint8_t>(k + (now < t[i] + kSplitWindowUs ? 1 : 0));
        return k;
    }
    static void push(uint64_t* t, uint8_t& n, uint64_t now) noexcept {
        if (n < kGlobalSplits) { t[n++] = now; return; }
        for (uint8_t i = 1; i < kGlobalSplits; ++i) t[i - 1] = t[i];
        t[kGlobalSplits - 1] = now;
    }
    // Una unidad partida de la clase `road`. Devuelve true si ESTA partida bloquea su clase o para la pieza.
    bool note(uint32_t road, uint64_t now) noexcept {
        Cls* x = nullptr;
        for (uint8_t i = 0; i < nc; ++i) if (c[i].road == road) x = &c[i];
        if (!x && nc < 8) {
            x = &c[nc++];
            x->road = road; x->n = 0; x->blocked = false;
        }
        bool changed = false;
        if (x) {
            push(x->t, x->n, now);
            if (!x->blocked && recent(x->t, x->n, now) >= kClassSplits) { x->blocked = true; changed = true; }
        }
        push(all, na, now);
        if (!stop && recent(all, na, now) >= kGlobalSplits) { stop = true; changed = true; }
        return changed;
    }
    bool blocked(uint32_t road) const noexcept {
        if (stop) return true;
        for (uint8_t i = 0; i < nc; ++i) if (c[i].road == road && c[i].blocked) return true;
        return false;
    }
};

// ---------------------------------------------------------------- reglas de parada (sólo esta pieza)
// Ninguna es global: paran las asignaciones NUEVAS de viajeros; las unidades asignadas siguen con su controlador y
// su limpieza (carga, Logout) continúa.
enum class Stop : uint8_t { None, Bad2005, Restore0ToHost, DoubleControl, Fault, SplitLoop, Count };
inline const char* stop_name(Stop s) noexcept {
    switch (s) {
        case Stop::None: return "ninguna";
        case Stop::Bad2005: return "un 2005 de un viajero asignado nombra OTRO controlador";
        case Stop::Restore0ToHost: return "el cliente del anfitrión recibió un viajero del usuario 2 con restore 0";
        case Stop::DoubleControl: return "doble control: el cliente del anfitrión pide IA (2223/2734/2291/2405/2828) para un viajero asignado";
        case Stop::Fault: return "excepción nativa en la limpieza";
        case Stop::SplitLoop: return "bucle de partidas: demasiadas unidades asignadas partidas en 10 min";
        default: return "?";
    }
}
// Las Req de IA que sólo envía el controlador. S34 (3-viajeros.md §2.1): el cliente que NO controla a un viajero tipo
// 3/5/6 no envió ninguna de éstas (sólo 2556 para los «Game Mechanic» tipo 7): una de ellas desde el cliente del
// anfitrión para un viajero asignado es su IA corriendo también allí. 2556 sólo se cuenta (indicio).
inline bool controller_only_req(uint16_t op) noexcept {
    return op == 2223 || op == 2734 || op == 2291 || op == 2405 || op == 2828;
}
// Un 2005 de una clave de carretera registrada. to_host: el visto por el cliente del anfitrión (S>C capturado).
enum class Check2005 : uint8_t { Ignore, Ok, BadController, Restore0ToHost };
inline Check2005 check_2005(bool assigned, uint32_t expected, uint32_t controller, bool to_host, int restore) noexcept {
    if (!assigned) return Check2005::Ignore;
    if (!expected || controller != expected) return Check2005::BadController;
    if (to_host && restore == 0) return Check2005::Restore0ToHost;
    return Check2005::Ok;
}

// ---------------------------------------------------------------- la 2550 en curso (procedencia positiva)
// 0x29C4930: tras la cabecera de 5 B, u8 + f32 y n entradas de 24 B {u32 clase | u16 cupo | u8 | u8 tramo | f32 x y z |
// f32 giro} (p8-s14-viajeros.md §1.1; el bucle de lectura de 0x29C4930). 0x27EA160 copia la clase en +0x3D0 (la prueba
// de fase C: +0x3D0 = 0x1F5 / 0x3FD / 0x7D1 / 0xFA1, las clases de la 2550). Las clases de la petición del Troc en curso.
inline constexpr size_t kRequestClasses = 8;
struct Request {
    uint64_t troc = 0;  // id del Troc del usuario 2 que la lleva (0 = ninguna)
    uint32_t cls[kRequestClasses] = {};
    uint8_t n = 0;
    bool overflow = false;
};
// payload = los bytes tras la cabecera de 5 B; size = su longitud.
template <class Read>
bool parse_request(Read&& read, uintptr_t payload, size_t size, Request& r) noexcept {
    r.n = 0;
    r.overflow = false;
    if (size < 5 || (size - 5) % 24) return false;
    for (size_t at = 5; at + 24 <= size; at += 24) {
        uint32_t c = 0;
        if (!read(payload + at, &c, sizeof c)) return false;
        bool seen = false;
        for (uint8_t i = 0; i < r.n; ++i) seen = seen || r.cls[i] == c;
        if (seen) continue;
        if (r.n < kRequestClasses) r.cls[r.n++] = c;
        else r.overflow = true;
    }
    return true;
}
inline int in_request(const Request& r, uint64_t troc, uint32_t road) noexcept {
    if (!troc || r.troc != troc) return -1;
    for (uint8_t i = 0; i < r.n; ++i) {
        if (r.cls[i] == road) return 1;
    }
    return r.overflow ? -1 : 0;
}

// ---------------------------------------------------------------- posición de una Req (para «se mueve»)
// 2223: payload +0x8E; 2556: +4 (npc_probe_client_logic.h). Desplazamiento X/Z máximo desde la primera.
inline bool req_position_offset(uint16_t op, size_t& off) noexcept {
    if (op == 2223) { off = 0x8E; return true; }
    if (op == 2556) { off = 4; return true; }
    return false;
}
inline void note_position(Rec& c, float x, float z) noexcept {
    if (!(x == x) || !(z == z) || x > 1e7f || x < -1e7f || z > 1e7f || z < -1e7f) return;  // NaN / absurdo
    if (!c.pos) {
        c.pos = true; c.x0 = x; c.z0 = z;
        return;
    }
    const float dx = x - c.x0, dz = z - c.z0;
    const float d2 = dx * dx + dz * dz;
    if (d2 > c.moved * c.moved) {
        // raíz cuadrada sin <cmath> (Newton, suficiente para un umbral de 1 m)
        float r = d2 > 1.0f ? d2 / 2.0f : 1.0f;
        for (int i = 0; i < 20; ++i) r = 0.5f * (r + d2 / r);
        c.moved = r;
    }
}

// ================================================================ rotación nativa (higiene de la fase C)
// build/research/churn-viajeros/informe.md §3: con user2_road_control=1 las bandadas 0x1F5 del invitado siguen el ciclo
// de vida nativo (~80 altas/min, vida mediana ~29 s, motivo 25), igual que las del anfitrión: no es un fallo. Tres
// cambios de log: (1) muestreo de las líneas por nacimiento, (2) paridad de altas/min y vida mediana invitado frente al
// anfitrión por clase de carretera, (3) posición, distancias, radio +0x468 y caducidad +0x428 en cada baja.

// ---------------------------------------------------------------- (1) muestreo por clase de carretera
// Las primeras kSampleFirst de cada clase, después 1 de cada kSampleEvery; `keep_all` (raíces / unidades tipo 5) pasa
// siempre. Sin cerrojos (corre en el mid): ranuras atómicas por clave; sin ranura, un ordinal común de desbordamiento.
inline constexpr uint32_t kSampleFirst = 40, kSampleEvery = 25;
inline bool sample_keep(uint32_t ord, uint32_t first = kSampleFirst, uint32_t every = kSampleEvery) noexcept {
    return ord && (ord <= first || (every && (ord - first) % every == 0));
}
template <size_t N = 32, uint32_t First = kSampleFirst, uint32_t Every = kSampleEvery>
struct Sampler {
    static_assert(N >= 4 && (N & (N - 1)) == 0, "N must be a power of two");
    std::atomic<uint32_t> tag[N] = {};  // clave + 1 (0 = libre)
    std::atomic<uint32_t> n[N] = {};
    std::atomic<uint32_t> over_n{0};
    std::atomic<uint64_t> kept{0}, dropped{0}, overflow{0};
    // Ordinal (1..) de esta aparición en su clave.
    uint32_t count(uint32_t key) noexcept {
        const uint32_t t = key + 1;
        if (t) {
            const size_t h = static_cast<size_t>((static_cast<uint64_t>(t) * 0x9E3779B97F4A7C15ull) >> 40);
            for (size_t p = 0; p < N; ++p) {
                const size_t i = (h + p) & (N - 1);
                uint32_t cur = tag[i].load(std::memory_order_relaxed);
                if (!cur && tag[i].compare_exchange_strong(cur, t, std::memory_order_relaxed)) cur = t;
                if (cur == t) return n[i].fetch_add(1, std::memory_order_relaxed) + 1;
            }
        }
        overflow.fetch_add(1, std::memory_order_relaxed);
        return over_n.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    bool take(uint32_t key, bool keep_all, uint32_t* ord = nullptr) noexcept {
        const uint32_t k = count(key);
        if (ord) *ord = k;
        const bool yes = keep_all || sample_keep(k, First, Every);
        (yes ? kept : dropped).fetch_add(1, std::memory_order_relaxed);
        return yes;
    }
};
// La clave de muestreo de un nacimiento: su clase de carretera (+0x3D0) y si escribe controlador.
inline uint32_t sample_key(uint32_t road, bool write) noexcept { return (road & 0x7FFFFFFF) | (write ? 0x80000000u : 0); }

// ---------------------------------------------------------------- (2) paridad invitado / anfitrión
// Altas y bajas de los viajeros marcados (descriptor de carretera con solicitante jugador) de los dos solicitantes, por
// clase (lado, carretera +0x3D0, tipo). El llamador pone el cerrojo. Sin CRT en birth / death (corren en el mid y en
// el punto O): bucles simples.
enum class Side : uint8_t { Guest, Host };
inline const char* side_name(Side s) noexcept { return s == Side::Guest ? "invitado" : "anfitrión"; }
inline constexpr size_t kParityRing = 64, kParityClasses = 24, kParityLive = 2048;
inline constexpr uint32_t kParityMinSamples = 20, kParityFloorMs = 10'000;
inline constexpr uint64_t kParityStaleUs = 1'200'000'000;  // 20 min sin baja: se olvida (contado)
struct ParityClass {
    bool used;
    Side side;
    uint8_t type;
    uint32_t road;
    uint64_t births, births_win, deaths, r25, stale;
    uint32_t life_ms[kParityRing];  // las últimas kParityRing vidas (bajas nativas)
    uint8_t ring_n, ring_at;
};
struct ParityLive {
    uint32_t key;  // 0 = libre
    uint8_t cls;
    uint64_t born_us;
};
struct ParityDeath {
    bool found = false;
    Side side = Side::Guest;
    uint8_t type = 0;
    uint32_t road = 0;
    uint64_t life_us = 0;
};
struct Parity {
    static_assert((kParityLive & (kParityLive - 1)) == 0, "kParityLive must be a power of two");
    ParityClass c[kParityClasses];
    ParityLive live[kParityLive];
    size_t live_n;
    uint64_t live_full, class_full, again, win_start_us;
    static size_t slot(uint32_t k) noexcept {
        return static_cast<size_t>((static_cast<uint64_t>(k) * 0x9E3779B97F4A7C15ull) >> 40) & (kParityLive - 1);
    }
    int find_class(Side s, uint32_t road, uint8_t type) const noexcept {
        for (size_t i = 0; i < kParityClasses; ++i) {
            if (c[i].used && c[i].side == s && c[i].road == road && c[i].type == type) return static_cast<int>(i);
        }
        return -1;
    }
    int class_for(Side s, uint32_t road, uint8_t type) noexcept {
        const int f = find_class(s, road, type);
        if (f >= 0) return f;
        for (size_t i = 0; i < kParityClasses; ++i) {
            if (c[i].used) continue;
            ParityClass& x = c[i];
            x.used = true; x.side = s; x.road = road; x.type = type;
            x.births = 0; x.births_win = 0; x.deaths = 0; x.r25 = 0; x.stale = 0; x.ring_n = 0; x.ring_at = 0;
            return static_cast<int>(i);
        }
        ++class_full;
        return -1;
    }
    int find_live(uint32_t key) const noexcept {
        if (!key) return -1;
        for (size_t p = 0, i = slot(key); p < kParityLive; ++p, i = (i + 1) & (kParityLive - 1)) {
            if (live[i].key == key) return static_cast<int>(i);
            if (!live[i].key) return -1;
        }
        return -1;
    }
    // Borrado con desplazamiento hacia atrás: las cadenas siguen enteras sin lápidas.
    void erase_at(size_t i) noexcept {
        live[i].key = 0;
        if (live_n) --live_n;
        size_t j = i;
        for (;;) {
            j = (j + 1) & (kParityLive - 1);
            if (!live[j].key) return;
            const size_t h = slot(live[j].key);
            // ¿Está h cíclicamente en (i, j]? Entonces se queda; si no, baja a i.
            const bool stays = i <= j ? (i < h && h <= j) : (i < h || h <= j);
            if (stays) continue;
            live[i] = live[j];
            live[j].key = 0;
            i = j;
        }
    }
    // Un alta. false: sin clase o tabla llena (contados), o la clave ya seguida (again).
    bool birth(uint32_t key, Side s, uint32_t road, uint8_t type, uint64_t now) noexcept {
        if (!key) return false;
        if (!win_start_us) win_start_us = now;  // la primera ventana empieza con la primera alta
        if (find_live(key) >= 0) { ++again; return false; }
        const int ci = class_for(s, road, type);
        if (ci < 0) return false;
        ++c[ci].births;
        ++c[ci].births_win;
        if (live_n >= kParityLive * 3 / 4) { ++live_full; return false; }
        for (size_t p = 0, i = slot(key); p < kParityLive; ++p, i = (i + 1) & (kParityLive - 1)) {
            if (live[i].key) continue;
            live[i].key = key; live[i].cls = static_cast<uint8_t>(ci); live[i].born_us = now;
            ++live_n;
            return true;
        }
        ++live_full;
        return false;
    }
    // Una baja (Logout en el servidor). native=false (nuestro propio Logout): se olvida sin muestra de vida.
    ParityDeath death(uint32_t key, uint8_t reason, uint64_t now, bool native) noexcept {
        ParityDeath d;
        const int i = find_live(key);
        if (i < 0) return d;
        const ParityLive l = live[i];
        erase_at(static_cast<size_t>(i));
        ParityClass& x = c[l.cls];
        d.found = true; d.side = x.side; d.type = x.type; d.road = x.road;
        d.life_us = now >= l.born_us ? now - l.born_us : 0;
        if (!native) return d;
        ++x.deaths;
        if (reason == 25) ++x.r25;
        const uint64_t ms = d.life_us / 1000;
        x.life_ms[x.ring_at] = ms > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(ms);
        x.ring_at = static_cast<uint8_t>((x.ring_at + 1) % kParityRing);
        if (x.ring_n < kParityRing) ++x.ring_n;
        return d;
    }
    // Las seguidas desde hace más de kParityStaleUs (su baja no se vio): se olvidan. Devuelve cuántas.
    size_t prune(uint64_t now) noexcept {
        size_t n = 0;
        for (size_t i = 0; i < kParityLive;) {
            if (live[i].key && now >= live[i].born_us + kParityStaleUs) {
                ++c[live[i].cls].stale;
                erase_at(i);
                ++n;
                continue;  // erase_at puede haber bajado otra entrada a i
            }
            ++i;
        }
        return n;
    }
    uint32_t median_ms(size_t ci) const noexcept {
        const ParityClass& x = c[ci];
        if (!x.ring_n) return 0;
        uint32_t v[kParityRing];
        for (uint8_t k = 0; k < x.ring_n; ++k) v[k] = x.life_ms[k];
        uint32_t* mid = v + x.ring_n / 2;
        std::nth_element(v, mid, v + x.ring_n);
        if (x.ring_n % 2) return *mid;
        const uint32_t hi = *mid, lo = *std::max_element(v, mid);
        return static_cast<uint32_t>((static_cast<uint64_t>(lo) + hi) / 2);
    }
    // La clase del anfitrión con la que se compara una del invitado: la misma carretera y tipo; si no hay (los caminantes
    // del invitado 0x3FD frente a los 0x3FE del anfitrión), la del mismo tipo con más vidas. -1: ninguna.
    int counterpart(size_t gi, bool* same_road = nullptr) const noexcept {
        const ParityClass& g = c[gi];
        const int f = find_class(Side::Host, g.road, g.type);
        if (same_road) *same_road = f >= 0;
        if (f >= 0) return f;
        int best = -1;
        for (size_t i = 0; i < kParityClasses; ++i) {
            if (!c[i].used || c[i].side != Side::Host || c[i].type != g.type) continue;
            if (best < 0 || c[i].ring_n > c[best].ring_n) best = static_cast<int>(i);
        }
        return best;
    }
    // Altas por minuto de la ventana en curso, en décimas.
    uint64_t rate_x10(size_t ci, uint64_t now) const noexcept {
        const uint64_t span = now > win_start_us ? now - win_start_us : 0;
        return span ? c[ci].births_win * 600'000'000ull / span : 0;
    }
    void new_window(uint64_t now) noexcept {
        for (size_t i = 0; i < kParityClasses; ++i) c[i].births_win = 0;
        win_start_us = now;
    }
};
enum class ParityV : uint8_t { Few, Ok, TooShort, ShortVsHost };
inline const char* parity_name(ParityV v) noexcept {
    switch (v) {
        case ParityV::Few: return "pocas muestras";
        case ParityV::Ok: return "ok";
        case ParityV::TooShort: return "CORTA (< 10 s)";
        case ParityV::ShortVsHost: return "CORTA (< 1/3 de la del anfitrión)";
    }
    return "?";
}
// WRN sólo con muestras suficientes: vida mediana del invitado < 10 s, o < 1/3 de la del anfitrión (con las suyas).
inline ParityV parity_verdict(uint32_t guest_ms, uint32_t guest_n, uint32_t host_ms, uint32_t host_n) noexcept {
    if (guest_n < kParityMinSamples) return ParityV::Few;
    if (guest_ms < kParityFloorMs) return ParityV::TooShort;
    if (host_n >= kParityMinSamples && static_cast<uint64_t>(guest_ms) * 3 < host_ms) return ParityV::ShortVsHost;
    return ParityV::Ok;
}

// ---------------------------------------------------------------- (3) la rama del recolector en una baja
// 0x2C37340 (build 2976): caducidad `[spawn+0x428] != 0 && < ahora` (_time64, :340-343) -> Logout motivo 25; si no,
// 0x28F9A20: fuera cuando d² >= r², r = [[actor+0x68]+0x20]+0x468 (float), contra el ancla 1 (personaje del dueño del
// campo, el anfitrión) y el ancla 2 (user 2, [campo+0x2E4/+0x2E8]). Un actor con caja orientada no usa el radio: queda
// en «dentro» o «fuera» por el radio igualmente (aproximación, sólo diagnóstico).
inline constexpr uint32_t kComps_Spawn = 0x20, kSpawn_Expiry = 0x428, kSpawn_Radius = 0x468;  // i64 / float
enum class Gone : uint8_t { Unknown, Expired, OutOfRadius, InsideAnchor, Count };
inline const char* gone_name(Gone g) noexcept {
    switch (g) {
        case Gone::Unknown: return "desconocida";
        case Gone::Expired: return "caducidad (+0x428)";
        case Gone::OutOfRadius: return "distancia (fuera del radio de las dos anclas)";
        case Gone::InsideAnchor: return "dentro del radio de un ancla (otra rama)";
        default: return "?";
    }
}
struct GoneIn {
    bool expiry_ok = false, pos_ok = false, radius_ok = false, d_u2_ok = false, d_host_ok = false;
    int64_t expiry = 0, now = 0;
    float radius = 0, d_u2 = 0, d_host = 0;
};
inline Gone gone_branch(const GoneIn& g) noexcept {
    if (g.expiry_ok && g.expiry != 0 && g.expiry < g.now) return Gone::Expired;
    if (!g.pos_ok || !g.radius_ok || !(g.radius > 0.0f)) return Gone::Unknown;
    if (!g.d_u2_ok && !g.d_host_ok) return Gone::Unknown;
    const bool inside = (g.d_u2_ok && g.d_u2 < g.radius) || (g.d_host_ok && g.d_host < g.radius);
    return inside ? Gone::InsideAnchor : Gone::OutOfRadius;
}

}  // namespace cdmp::session2::road
