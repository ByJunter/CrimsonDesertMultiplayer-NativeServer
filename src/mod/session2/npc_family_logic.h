#pragma once
// S36 fase B (docs/research/s25/s36-fase-b.md; AUDITORIA.md §2 "Cambios obligatorios", §6 "Problemas adicionales"
// 1-3 y "Fronteras de autoridad", §8 punto 3; AUDITORIA-2-FAMILIAS.md "Cambios obligatorios" 1-11), build 1.0.0.2976
// (PE 0x6AB28F00, base 0x140000000): todo RVA de este archivo vale sólo para esa build.
//
// Pura: el juego sólo se lee a través de la política `read(dirección, destino, bytes)` del llamador (lecturas
// protegidas: game::fast_read en el mod), nada se escribe. Pensada para el mid hook 0x2792A7B (docked_gimmicks'
// create_mid, mov [r14+0x90],eax; rsi = descriptor interior, r13 = campo, r14 = actor): ningún memset/memcpy del CRT
// (una copia vectorizada AVX podría pisar la mitad alta de los YMM del juego, p8_probe_logic.h), ninguna copia
// agregada grande, ningún cerrojo; los arrays de los registros no tienen inicializadores por miembro.
//
// Unidad = raíz (descriptor con +0x2F0 == 0) + su subárbol de descriptores (vector +0x2F8 de envoltorios
// CreateChildServerActorDesc_*, cuyo interior +8 tiene +0x2F0 = el descriptor padre). Contrato estático (exports
// Ghidra de la build 2976, D:\RE\CrimsonDesert_2976\exports\decomp):
//  - 0x278EA70 (constructor base ICreateServerActorDesc, 0x278ea70.c:132-136): +0x2F0 = 0 (param_1[0x5e]),
//    +0x2F8 {T* data; u32 count +0x300; u32 cap +0x304} = 0 (param_1[0x5f..0x60]);
//  - 0x281DBD0 (0x281dbd0.c:104-121) y 0x2820D80 (0x2820d80.c:107-123): envoltorio de 0x10 B {vtable, inner},
//    [inner+0x2F0] = descriptor padre, push_back(padre+0x2F8, envoltorio) con 0x43DB70 (vector de punteros de 8 B);
//  - 0x281CEE0 (0x281cee0.c:385-396) llama a 0x281D970 y 0x281DBD0 con el MISMO descriptor recién construido: el
//    subárbol estático está completo cuando ServerField::vf17 0x2AF9AE0 crea el lote (antes de cualquier mid);
//  - 0x2791CD0 (0x2791cd0.c:625-672): el selector 0x2792A11..0x2792A7B y la escritura de actor+0x90; justo después
//    `call [rax+8]` con rcx = rsi = param_1 = el descriptor (AOB de AUDITORIA.md §7: 41 89 86 90 00 00 00 48 8B 06
//    4D 8B CE 4D 8B C5 48 8D 55 67 48 8B CE FF).
// Envoltorios y descriptores (RTTI leída del PE por la auditoría, a2-familias/rtti.json). _Rider NO tiene ruta de
// construcción hacia una familia de horario (AUDITORIA.md §2): su presencia excluye la unidad entera, igual que
// cualquier vtable desconocida.
// Prueba en dos copias 2 (15-10, kit 165c4aa, probe): las 21 raíces con hijos en su preexamen fallaron TODAS por
// «envoltorio excluido» (guardia + perro, pastor + ovejas, grupos de tipo 3), con hijos de interior NPCSchedule. Su
// constructor (sección .xtls, sin función en Ghidra; desensamblado del PE de la build 2976) es 0x100E70A0: exige
// [padre+0xA] == 0xC y [hijo+0xA] == 0xC (0xC = el tipo que el constructor de NPCSchedule 0x10102580 pasa al base
// 0x278EA70, que lo guarda en +0xA), crea el envoltorio de 0x10 B {vtable _NPCScheduleGroupActionMember, inner},
// [inner+0x2F0] = padre y push_back(padre+0x2F8) con 0x43DB70: el MISMO contrato que 0x281DBD0, sólo entre
// descriptores de horario. _NPCScheduleGroupActionMember con interior NPCSchedule bajo un padre NPCSchedule es por eso
// de clase horario; con cualquier otro interior o padre sigue excluido. El constructor de _Rider (.xtls, prólogo en
// 0x100E63BF) exige en cambio la clase 5 (la de vehículo que también comprueba _LinkedVehicle 0x2797510) en el padre
// y no en el hijo, y un asiento libre: jinete de vehículo/montura, queda excluido.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cdmp::session2::npcfam {

inline constexpr uint32_t kVtNpcSchedule = 0x5B04CB0;     // pa::CreateServerActorDesc_NPCSchedule
inline constexpr uint32_t kVtInstantGimmick = 0x5B04C68;  // pa::CreateServerActorDesc_InstantGimmick (tipo 7)
// pa::ICreateServerActorDesc, la clase base usada tal cual (la escribe 0x278EA70 en 0x278EAE1): los viajeros de
// carretera (AutoSpawnRoad) y sus animales/carros. Fuera de U-e (raíz tipo 6, otro descriptor): fase C.
inline constexpr uint32_t kVtDescBase = 0x5B04BE8;
inline constexpr uint32_t kVtWrapSimple = 0x5B04E80, kVtWrapCatchTarget = 0x5B04E20,
                          kVtWrapAutoSpawnGimmick = 0x5B04DA0, kVtWrapCombinationGimmick = 0x5B04D60,
                          kVtWrapSavedAttached = 0x5B04DC0, kVtWrapLinkedVehicle = 0x5B04D80,
                          kVtWrapDocking = 0x5B04DE0, kVtWrapRider = 0x5B04E60, kVtWrapGroupActionMember = 0x5B04E00;
inline constexpr uint32_t kDesc_Parent = 0x2F0, kDesc_Children = 0x2F8, kDesc_ChildCount = 0x300, kDesc_User = 0x40,
                          kWrap_Inner = 0x08;
inline constexpr uint32_t kActor_Key = 0x60, kActor_Ctl = 0x90;

inline constexpr size_t kMaxUnit = 64;               // == npcact::kMaxFamily: la contención retira como mucho 64
inline constexpr size_t kMaxNodes = kMaxUnit - 1;    // descendientes de una raíz
inline constexpr uint8_t kMaxDepth = 4;              // C tenía profundidad 2 (B0100473 -> 474 -> 475)
inline constexpr uint32_t kMaxVector = 64;           // hijos directos de un descriptor
inline constexpr uint8_t kNoIndex = 0xFF;
inline constexpr uint16_t kNoRec = 0xFFFF;   // índice de registro de un marco: ninguno (Rec::parent)

// ---------------------------------------------------------------- npc_family_types (la cohorte)
// Tipos de MIEMBRO admitidos (la raíz sigue la regla de U-e: tipo 3). Por defecto 3 y 6; 7 (InstantGimmick) sólo si
// se pide explícitamente: sin él, un hijo tipo 7 de una unidad admitida queda con el anfitrión (host-kept) y eso
// exige publication_gate=1 (AUDITORIA.md §2 "Tipo7").
inline constexpr uint16_t kDefaultTypes = 1u << 3 | 1u << 6;
struct Types {
    uint16_t mask = kDefaultTypes;
    bool bad = false;
};
inline bool type_in(Types t, uint8_t type) noexcept { return type < 16 && (t.mask >> type & 1u) != 0; }
// "3,6" | "3,6,7" | "3" ... Sólo 3, 6 y 7. Cualquier otra cosa (u otra lista vacía): bad y la cohorte por defecto.
inline Types parse_types(std::string_view s) noexcept {
    Types t;
    if (s.empty()) return t;
    uint16_t mask = 0;
    bool bad = false;
    size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == ',' || c == ' ' || c == '\t') { ++i; continue; }
        if (c < '0' || c > '9') { bad = true; break; }
        uint32_t v = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            v = v * 10 + static_cast<uint32_t>(s[i] - '0');
            if (v > 255) break;
            ++i;
        }
        if (v == 3 || v == 6 || v == 7) mask = static_cast<uint16_t>(mask | 1u << v);
        else { bad = true; break; }
    }
    if (bad || !mask) {
        t.bad = true;
        return t;
    }
    t.mask = mask;
    return t;
}

// ---------------------------------------------------------------- lecturas en tres estados
// AUDITORIA.md §6.3: lectura protegida no es lectura válida. Igual que pubg::Cc (fase A): Unknown (no se pudo leer o
// el valor no es plausible) NUNCA es Absent (leído y exactamente 0).
enum class Link : uint8_t { Unknown, Absent, Present };
struct LinkRead {
    Link state = Link::Unknown;
    uintptr_t parent = 0;
};
inline bool plausible(uintptr_t p) noexcept { return p >= 0x10000 && p < 0x7FFF'FFFF'0000ull; }
template <class Read>
LinkRead read_parent(Read&& read, uintptr_t desc) noexcept {
    LinkRead r;
    uintptr_t p = 0;
    if (!plausible(desc) || !read(desc + kDesc_Parent, &p, sizeof p)) return r;
    if (!p) {
        r.state = Link::Absent;
        return r;
    }
    if (!plausible(p)) return r;
    r.state = Link::Present;
    r.parent = p;
    return r;
}
// 1 sí, 0 no (leído y distinto), -1 desconocido.
template <class Read>
int read_matches(Read&& read, uintptr_t at, uint32_t expected) noexcept {
    uint32_t v = 0;
    if (!plausible(at) || !read(at, &v, sizeof v)) return -1;
    return v == expected ? 1 : 0;
}
// ¿El vector +0x2F8 de `parent` tiene un envoltorio cuyo interior es `child`? (1 / 0 / -1 ilegible). Con `wrap` /
// `inner` (RVA de las vtables, para clasificar un hijo tardío) si se piden.
template <class Read>
int in_vector(Read&& read, uintptr_t parent, uintptr_t child, uintptr_t base = 0, uint64_t image_size = 0,
              uint32_t* wrap = nullptr, uint32_t* inner_vt = nullptr) noexcept;

// ---------------------------------------------------------------- clase de un nodo (envoltorio, interior)
enum class Cls : uint8_t { Schedule, Gimmick, Excluded, Unknown };
inline const char* cls_name(Cls c) noexcept {
    switch (c) {
        case Cls::Schedule: return "horario";
        case Cls::Gimmick: return "gimmick";
        case Cls::Excluded: return "excluido";
        case Cls::Unknown: return "desconocido";
    }
    return "?";
}
// parent_inner_rva: la vtable del descriptor padre (0 = no leída: un _NPCScheduleGroupActionMember sigue excluido).
inline Cls classify(uint32_t wrap_rva, uint32_t inner_rva, uint32_t parent_inner_rva = 0) noexcept {
    switch (wrap_rva) {
        case kVtWrapGroupActionMember:
            // Prueba en dos copias 2: el contrato de su constructor 0x100E70A0 (horario bajo horario, cabecera).
            return inner_rva == kVtNpcSchedule && parent_inner_rva == kVtNpcSchedule ? Cls::Schedule : Cls::Excluded;
        case kVtWrapSimple:
            if (inner_rva == kVtNpcSchedule) return Cls::Schedule;
            return inner_rva == kVtInstantGimmick ? Cls::Gimmick : Cls::Unknown;
        case kVtWrapAutoSpawnGimmick:
        case kVtWrapCombinationGimmick:
            return inner_rva == kVtInstantGimmick ? Cls::Gimmick : Cls::Unknown;
        case kVtWrapCatchTarget:
        case kVtWrapSavedAttached:
        case kVtWrapLinkedVehicle:
        case kVtWrapDocking:
        case kVtWrapRider:
            return Cls::Excluded;  // monturas, vehículos, jinete, docking, captura, estado guardado
        default:
            return Cls::Unknown;
    }
}
// Diagnóstico por clase (prueba en dos copias 2): el envoltorio de un preexamen fallido y el padre de un huérfano.
enum class Wrap : uint8_t { Simple, CatchTarget, AutoSpawnGimmick, CombinationGimmick, SavedAttached, LinkedVehicle,
                            Docking, Rider, GroupActionMember, Other, Count };
inline Wrap wrap_of(uint32_t wrap_rva) noexcept {
    switch (wrap_rva) {
        case kVtWrapSimple: return Wrap::Simple;
        case kVtWrapCatchTarget: return Wrap::CatchTarget;
        case kVtWrapAutoSpawnGimmick: return Wrap::AutoSpawnGimmick;
        case kVtWrapCombinationGimmick: return Wrap::CombinationGimmick;
        case kVtWrapSavedAttached: return Wrap::SavedAttached;
        case kVtWrapLinkedVehicle: return Wrap::LinkedVehicle;
        case kVtWrapDocking: return Wrap::Docking;
        case kVtWrapRider: return Wrap::Rider;
        case kVtWrapGroupActionMember: return Wrap::GroupActionMember;
        default: return Wrap::Other;
    }
}
inline const char* wrap_name(Wrap w) noexcept {
    switch (w) {
        case Wrap::Simple: return "_Simple";
        case Wrap::CatchTarget: return "_CatchTarget";
        case Wrap::AutoSpawnGimmick: return "_AutoSpawnGimmick";
        case Wrap::CombinationGimmick: return "_CombinationGimmick";
        case Wrap::SavedAttached: return "_SavedAttached";
        case Wrap::LinkedVehicle: return "_LinkedVehicle";
        case Wrap::Docking: return "_Docking";
        case Wrap::Rider: return "_Rider";
        case Wrap::GroupActionMember: return "_NPCScheduleGroupActionMember";
        case Wrap::Other: return "otro";
        default: return "?";
    }
}
// Un hijo con +0x2F0 cuyo padre no está en ningún marco, por la vtable de ese padre. Los viajeros (padre base
// ICreateServerActorDesc) se cuentan aparte: no son raíces de U-e (fase C) y no deben tapar a los demás.
enum class OrphanCls : uint8_t { Traveller, Schedule, Gimmick, Other, Count };
inline OrphanCls orphan_class(uint32_t parent_vt_rva) noexcept {
    switch (parent_vt_rva) {
        case kVtDescBase: return OrphanCls::Traveller;
        case kVtNpcSchedule: return OrphanCls::Schedule;
        case kVtInstantGimmick: return OrphanCls::Gimmick;
        default: return OrphanCls::Other;
    }
}
inline const char* orphan_name(OrphanCls c) noexcept {
    switch (c) {
        case OrphanCls::Traveller: return "viajero (padre ICreateServerActorDesc, fase C)";
        case OrphanCls::Schedule: return "horario (padre NPCSchedule)";
        case OrphanCls::Gimmick: return "gimmick (padre InstantGimmick)";
        case OrphanCls::Other: return "otro padre";
        default: return "?";
    }
}
// Cupo de líneas de diagnóstico POR CLASE (atómico, sin cerrojo): cada clase sale al menos `per` veces aunque otra
// sea mucho más frecuente (en la prueba 2 los viajeros agotaron el cupo único de 64).
template <size_t N>
struct LineCap {
    std::atomic<uint32_t> n[N] = {};
    bool take(size_t cls, uint32_t per) noexcept {
        return cls < N && n[cls].fetch_add(1, std::memory_order_relaxed) < per;
    }
};
inline uint32_t rva_in(uintptr_t v, uintptr_t base, uint64_t image_size) noexcept {
    return base && v >= base && v - base < image_size ? static_cast<uint32_t>(v - base) : 0;
}
template <class Read>
int in_vector(Read&& read, uintptr_t parent, uintptr_t child, uintptr_t base, uint64_t image_size, uint32_t* wrap,
              uint32_t* inner_vt) noexcept {
    uintptr_t data = 0;
    uint32_t count = 0;
    if (!plausible(parent) || !read(parent + kDesc_Children, &data, sizeof data) ||
        !read(parent + kDesc_ChildCount, &count, sizeof count)) {
        return -1;
    }
    if (!count) return 0;
    if (count > kMaxVector || !plausible(data)) return -1;
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t w = 0, inner = 0;
        if (!read(data + static_cast<uintptr_t>(i) * 8, &w, sizeof w) || !plausible(w) ||
            !read(w + kWrap_Inner, &inner, sizeof inner)) {
            return -1;
        }
        if (inner != child) continue;
        if (wrap || inner_vt) {
            uintptr_t wvt = 0, ivt = 0;
            if (!read(w, &wvt, sizeof wvt) || !read(inner, &ivt, sizeof ivt)) return -1;
            if (wrap) *wrap = rva_in(wvt, base, image_size);
            if (inner_vt) *inner_vt = rva_in(ivt, base, image_size);
        }
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------- preexamen del subárbol (AUDITORIA-2 cambio 1)
// En el mid de la RAÍZ, antes de cambiar EAX: se recorre su subárbol entero (BFS, acotado) para reservar la unidad
// completa. Un nodo: envoltorio leído, su interior, la vtable de ambos, [interior+0x2F0] == su padre (coherencia del
// vínculo) y [interior+0x40] == 0 (ningún usuario explícito). Cualquier fallo resuelve la unidad ENTERA.
struct ScanNode {
    uintptr_t desc;
    uint8_t parent;  // índice del padre en nodes, kNoIndex = la raíz
    uint8_t depth;   // 1 = hijo directo
    Cls cls;
};
enum class ScanVerdict : uint8_t { Ok, Excluded, UnknownWrapper, Unreadable, TooBig, TooDeep, Inconsistent, DescUser,
                                   Cycle, Count };
inline const char* scan_name(ScanVerdict v) noexcept {
    switch (v) {
        case ScanVerdict::Ok: return "ok";
        case ScanVerdict::Excluded: return "envoltorio excluido (montura/vehículo/jinete/docking/captura/guardado/grupo no horario)";
        case ScanVerdict::UnknownWrapper: return "envoltorio o interior desconocido";
        case ScanVerdict::Unreadable: return "subárbol ilegible";
        case ScanVerdict::TooBig: return "más de 63 descendientes o 64 hijos directos";
        case ScanVerdict::TooDeep: return "más profundo que 4";
        case ScanVerdict::Inconsistent: return "interior+0x2F0 no es su padre";
        case ScanVerdict::DescUser: return "interior+0x40 nombra un usuario";
        case ScanVerdict::Cycle: return "ciclo";
        default: return "?";
    }
}
struct Scan {
    ScanVerdict v = ScanVerdict::Ok;
    uint8_t n = 0, max_depth = 0, schedule = 0, gimmick = 0;
    uint8_t group = 0;                     // nodos horario por _NPCScheduleGroupActionMember (incluidos en schedule)
    uint32_t bad_wrap = 0, bad_inner = 0;  // sólo para la línea
    uint32_t bad_parent = 0;               // vtable del padre del nodo que falló (sólo para la línea)
    uint8_t bad_depth = 0;
    ScanNode nodes[kMaxNodes];             // sin inicializar: sólo valen [0, n)
};
template <class Read>
void scan_subtree(Read&& read, uintptr_t root, uintptr_t base, uint64_t image_size, Scan& s) noexcept {
    s.v = ScanVerdict::Ok;
    s.n = 0; s.max_depth = 0; s.schedule = 0; s.gimmick = 0; s.group = 0; s.bad_wrap = 0; s.bad_inner = 0;
    s.bad_parent = 0; s.bad_depth = 0;
    if (!plausible(root)) { s.v = ScanVerdict::Unreadable; return; }
    for (int at = -1; at < static_cast<int>(s.n); ++at) {
        const uintptr_t parent = at < 0 ? root : s.nodes[at].desc;
        const uint8_t depth = at < 0 ? 0 : s.nodes[at].depth;
        uintptr_t data = 0;
        uint32_t count = 0;
        if (!read(parent + kDesc_Children, &data, sizeof data) || !read(parent + kDesc_ChildCount, &count, sizeof count)) {
            s.v = ScanVerdict::Unreadable;
            return;
        }
        if (!count) continue;
        // La vtable del padre (clasificación de _NPCScheduleGroupActionMember): sólo si tiene hijos.
        uintptr_t pvt = 0;
        if (!read(parent, &pvt, sizeof pvt)) {
            s.v = ScanVerdict::Unreadable;
            return;
        }
        const uint32_t pr = rva_in(pvt, base, image_size);
        if (count > kMaxVector) { s.v = ScanVerdict::TooBig; return; }
        if (!plausible(data)) { s.v = ScanVerdict::Unreadable; return; }
        if (depth + 1 > kMaxDepth) { s.v = ScanVerdict::TooDeep; return; }
        for (uint32_t i = 0; i < count; ++i) {
            if (s.n >= kMaxNodes) { s.v = ScanVerdict::TooBig; return; }
            uintptr_t w = 0, wvt = 0, inner = 0, ivt = 0, back = 0;
            uint64_t user = 0;
            if (!read(data + static_cast<uintptr_t>(i) * 8, &w, sizeof w) || !plausible(w) ||
                !read(w, &wvt, sizeof wvt) || !read(w + kWrap_Inner, &inner, sizeof inner) || !plausible(inner) ||
                !read(inner, &ivt, sizeof ivt) || !read(inner + kDesc_Parent, &back, sizeof back) ||
                !read(inner + kDesc_User, &user, sizeof user)) {
                s.v = ScanVerdict::Unreadable;
                return;
            }
            if (back != parent) { s.v = ScanVerdict::Inconsistent; return; }
            if (inner == root) { s.v = ScanVerdict::Cycle; return; }
            for (uint8_t j = 0; j < s.n; ++j) {
                if (s.nodes[j].desc == inner) { s.v = ScanVerdict::Cycle; return; }
            }
            const uint32_t wr = rva_in(wvt, base, image_size), ir = rva_in(ivt, base, image_size);
            const Cls c = classify(wr, ir, pr);
            if (c == Cls::Excluded || c == Cls::Unknown) {
                s.v = c == Cls::Excluded ? ScanVerdict::Excluded : ScanVerdict::UnknownWrapper;
                s.bad_wrap = wr;
                s.bad_inner = ir;
                s.bad_parent = pr;
                s.bad_depth = static_cast<uint8_t>(depth + 1);
                return;
            }
            if (user) { s.v = ScanVerdict::DescUser; return; }
            ScanNode& nd = s.nodes[s.n];
            nd.desc = inner;
            nd.parent = at < 0 ? kNoIndex : static_cast<uint8_t>(at);
            nd.depth = static_cast<uint8_t>(depth + 1);
            nd.cls = c;
            ++s.n;
            if (nd.depth > s.max_depth) s.max_depth = nd.depth;
            if (c == Cls::Schedule) ++s.schedule;
            else ++s.gimmick;
            if (wr == kVtWrapGroupActionMember) ++s.group;
        }
    }
}

// ---------------------------------------------------------------- admisión de la unidad (AUDITORIA-2 cambios 1, 5, 6)
enum class Admit : uint8_t {
    Admit,             // reservada entera: la raíz se asigna (1) / se asignaría (probe)
    Childless,         // sin subárbol: la raíz sigue la regla de U-e de siempre (no hace falta unidad)
    ScanFailed,        // el preexamen no demostró el subárbol (Scan::v)
    GimmickNeedsGate,  // un hijo gimmick quedaría con el anfitrión y publication_gate no está a 1
    NodeSplitBefore,   // su nodo ya partió una unidad en esta época/generación: se queda entera con el anfitrión
    ChildBornFirst,    // un descendiente ya nació (con el anfitrión) antes que la raíz
    NoFrame,           // sin marco de Troc (profundidad >= 16 o reserva de marcos agotada)
    FrameFull,         // el marco no tiene sitio para todos sus registros
    KeysFull,          // g_ctl_keys no puede reservar todas sus claves
    TrackFull,         // la tabla de activación no puede reservar todos sus registros
    Count
};
inline const char* admit_name(Admit a) noexcept {
    switch (a) {
        case Admit::Admit: return "admitida";
        case Admit::Childless: return "sin hijos";
        case Admit::ScanFailed: return "preexamen fallido";
        case Admit::GimmickNeedsGate: return "hijo gimmick sin publication_gate=1";
        case Admit::NodeSplitBefore: return "su nodo ya partió una unidad";
        case Admit::ChildBornFirst: return "un hijo nació antes que la raíz";
        case Admit::NoFrame: return "sin marco de Troc";
        case Admit::FrameFull: return "marco lleno";
        case Admit::KeysFull: return "tabla de claves sin sitio para la unidad";
        case Admit::TrackFull: return "tabla de activación sin sitio para la unidad";
        default: return "?";
    }
}
struct AdmitIn {
    ScanVerdict scan = ScanVerdict::Ok;
    uint8_t nodes = 0;      // descendientes (sin la raíz)
    uint8_t gimmicks = 0;   // descendientes de clase gimmick
    bool gimmicks_assignable = false;  // 7 está en npc_family_types
    bool gate_on = false;              // publication_gate=1
    bool node_split_before = false, child_born_first = false;
    bool frame = true, frame_room = true, keys_room = true, track_room = true;
};
inline Admit admit_unit(const AdmitIn& a) noexcept {
    if (a.scan != ScanVerdict::Ok) return Admit::ScanFailed;
    if (!a.nodes) return Admit::Childless;
    if (a.gimmicks && !a.gimmicks_assignable && !a.gate_on) return Admit::GimmickNeedsGate;
    if (a.node_split_before) return Admit::NodeSplitBefore;
    if (a.child_born_first) return Admit::ChildBornFirst;
    if (!a.frame) return Admit::NoFrame;
    if (!a.frame_room) return Admit::FrameFull;
    if (!a.keys_room) return Admit::KeysFull;
    if (!a.track_room) return Admit::TrackFull;
    return Admit::Admit;
}
// La reserva atómica de la unidad (AUDITORIA-2 cambio 1, contraejemplo «63 ocupados + raíz con hijo»): primero las
// condiciones que no reservan nada, después cada capacidad; si una falla, se devuelven las anteriores y NADA se
// asigna. probe: las mismas comprobaciones sin reservar (can_reserve). n = raíz + descendientes que se registran.
// Keys / Track: reserve(n) -> bool, unreserve(n), can_reserve(n) -> bool.
template <class Keys, class Track>
Admit reserve_unit(AdmitIn in, size_t n_keys, size_t n_track, Keys& keys, Track& track, bool probe) noexcept {
    in.keys_room = in.track_room = true;
    const Admit pre = admit_unit(in);
    if (pre != Admit::Admit) return pre;
    if (probe) {
        in.keys_room = keys.can_reserve(n_keys);
        in.track_room = track.can_reserve(n_track);
        return admit_unit(in);
    }
    if (!keys.reserve(n_keys)) return Admit::KeysFull;
    if (!track.reserve(n_track)) {
        keys.unreserve(n_keys);
        return Admit::TrackFull;
    }
    return Admit::Admit;
}

// ---------------------------------------------------------------- decisión de un miembro en su nacimiento
// Clasificación por PARENTESCO, no por tipo (AUDITORIA.md §2: `not type3` no identifica hijos de stage): un
// descriptor con +0x2F0 != 0 nunca usa su propio veredicto de U-e; su controlador es el de su unidad o el nativo.
enum class SplitWhy : uint8_t {
    None, LinkUnknown, LinkChanged, ParentGone, ParentController, HasController, DescUser, UserChild,
    NotHostField, NotOwnerBranch, Type, NoKey, Stopped, LateNotInVector, LateNoRoom, PutFailed, Count
};
inline const char* split_name(SplitWhy w) noexcept {
    switch (w) {
        case SplitWhy::None: return "ninguno";
        case SplitWhy::LinkUnknown: return "vínculo +0x2F0 ilegible";
        case SplitWhy::LinkChanged: return "vínculo +0x2F0 distinto del preexamen";
        case SplitWhy::ParentGone: return "padre no vivo o ilegible";
        case SplitWhy::ParentController: return "el padre no tiene el controlador de la unidad";
        case SplitWhy::HasController: return "actor+0x90 ya puesto o ilegible";
        case SplitWhy::DescUser: return "desc+0x40 puesto o ilegible";
        case SplitWhy::UserChild: return "actor+0xA0 puesto o ilegible";
        case SplitWhy::NotHostField: return "no es el campo del anfitrión";
        case SplitWhy::NotOwnerBranch: return "eax no es el del dueño del campo";
        case SplitWhy::Type: return "tipo fuera de la cohorte o inesperado";
        case SplitWhy::NoKey: return "clave del usuario 2 desconocida o distinta";
        case SplitWhy::Stopped: return "U-e abortado o en pausa";
        case SplitWhy::LateNotInVector: return "tardío: no está en el vector del padre";
        case SplitWhy::LateNoRoom: return "tardío: sin sitio para reservarlo";
        case SplitWhy::PutFailed: return "su clave no se pudo registrar";
        default: return "?";
    }
}
enum class MemberVerdict : uint8_t { Assign, HostKept, HostUnit, Split };
inline const char* member_name(MemberVerdict v) noexcept {
    switch (v) {
        case MemberVerdict::Assign: return "miembro";
        case MemberVerdict::HostKept: return "host-kept";
        case MemberVerdict::HostUnit: return "de unidad del anfitrión";
        case MemberVerdict::Split: return "PARTIDO";
    }
    return "?";
}
struct MemberIn {
    bool admitted = false;       // su unidad fue admitida (1) o lo habría sido (probe)
    Cls cls = Cls::Unknown;      // del preexamen (o del envoltorio leído, tardío)
    uint8_t type = 0;            // [[actor+0x88]+1] (0 ilegible)
    Types types{};
    Link link = Link::Unknown;   // [desc+0x2F0] releído en SU mid
    bool link_matches = false;   // == el descriptor del registro del padre
    int parent_alive = -1;       // [padre+0x60] == su clave (1 / 0 / -1)
    int parent_ctl = -1;         // [padre+0x90] == controlador de la unidad (probe: 1)
    bool old_ctl_known = false;
    uint32_t old_ctl = 0;        // [actor+0x90] antes de la escritura
    bool desc_user_known = false;
    uint64_t desc_user = 0;      // [desc+0x40]
    bool actor_user_known = false;
    uint64_t actor_user = 0;     // [actor+0xA0]
    bool host_field = false, owner_branch = false;
    uint32_t key2 = 0;
    bool key_ok = false;         // key2 coincide con la sesión (o sesión desconocida)
    bool stopped = false;        // U-e abortado o asignaciones en pausa (degradación)
};
struct MemberOut {
    MemberVerdict v = MemberVerdict::Split;
    SplitWhy why = SplitWhy::None;
};
inline MemberOut member_verdict(const MemberIn& m) noexcept {
    if (!m.admitted) return {MemberVerdict::HostUnit, SplitWhy::None};
    if (m.link != Link::Present) return {MemberVerdict::Split, SplitWhy::LinkUnknown};
    if (!m.link_matches) return {MemberVerdict::Split, SplitWhy::LinkChanged};
    if (m.parent_alive != 1) return {MemberVerdict::Split, SplitWhy::ParentGone};
    // Tipo 7 fuera de la cohorte: se queda con el anfitrión (host-kept), no se escribe nada; su publicación segura es
    // la de la fase A (admitido sólo con publication_gate=1).
    if (m.cls == Cls::Gimmick && m.type == 7 && !type_in(m.types, 7)) return {MemberVerdict::HostKept, SplitWhy::None};
    if (m.parent_ctl != 1) return {MemberVerdict::Split, SplitWhy::ParentController};
    if (!m.old_ctl_known || m.old_ctl) return {MemberVerdict::Split, SplitWhy::HasController};
    if (!m.desc_user_known || m.desc_user) return {MemberVerdict::Split, SplitWhy::DescUser};
    if (!m.actor_user_known || m.actor_user) return {MemberVerdict::Split, SplitWhy::UserChild};
    if (!m.host_field) return {MemberVerdict::Split, SplitWhy::NotHostField};
    if (!m.owner_branch) return {MemberVerdict::Split, SplitWhy::NotOwnerBranch};
    const bool expected = m.cls == Cls::Schedule ? m.type != 7 : m.cls == Cls::Gimmick && m.type == 7;
    if (!expected || !type_in(m.types, m.type)) return {MemberVerdict::Split, SplitWhy::Type};
    if (!m.key2 || !m.key_ok) return {MemberVerdict::Split, SplitWhy::NoKey};
    if (m.stopped) return {MemberVerdict::Split, SplitWhy::Stopped};
    return {MemberVerdict::Assign, SplitWhy::None};
}
// Segunda revisión (15-10, R2): la pausa de la degradación (npcact::assignments_paused: la contención no puede actuar)
// bloquea unidades NUEVAS (su raíz sale con el veredicto de U-e abortado), no los miembros de una unidad ya admitida:
// sus reservas ya existen y partirla dejaría una unidad partida que la contención, en pausa, no puede retirar. Sólo el
// aborto de U-e (g_ctl_aborted) para también a esos miembros.
inline bool root_stopped(bool ctl_aborted, bool paused) noexcept { return ctl_aborted || paused; }
inline bool member_stopped(bool ctl_aborted, bool /*paused*/) noexcept { return ctl_aborted; }

// ---------------------------------------------------------------- hijos tardíos fuera del marco
// Segunda revisión (R4): un hijo visto al nacer FUERA del marco de su unidad o en la publicación, con un padre que es
// raíz o miembro de una unidad y otro controlador que ella. Sólo la pertenencia de DESCRIPTOR (+0x2F0 leído presente
// en su nacimiento) parte la unidad. Un hijo sólo de jerarquía (+0x0AC / jerarquía nativa con +0x2F0 leído a 0, o sin
// linaje conocido: montura o vehículo del anfitrión, pieza tipo 4/5, accesorio que no es tipo 7) se cuenta y se
// registra, nunca parte ni retira la unidad (en la fase A sólo se añadía como miembro). Los códigos son los de la línea
// [tardío en la publicación].
enum class LateWhat : uint8_t { LinkMismatch = 0, Kept7 = 1, Split = 2, SameController = 3, NotLineage = 4 };
inline LateWhat late_child(bool unit_known, bool other_controller, bool keep7, Link lineage) noexcept {
    if (!unit_known || !other_controller) return LateWhat::SameController;
    if (keep7) return LateWhat::Kept7;
    return lineage == Link::Present ? LateWhat::Split : LateWhat::NotLineage;
}

// ---------------------------------------------------------------- marcas por clave y nacimiento
// Segunda revisión (R5, R4): conjunto directo y acotado de claves con la época en que se anotaron. CADA nacimiento de
// una clave lo reescribe (insert o erase): una clave reutilizada (las claves renacen) nunca hereda la marca de su
// encarnación anterior, ni de otra época. Atómico, sin cerrojo ni CRT (se llama desde el mid 0x2792A7B); una colisión
// sólo pierde una marca (vuelve a «sin marca»).
template <size_t N>
class KeyMarks {
    static_assert(N >= 16 && (N & (N - 1)) == 0, "N must be a power of two");

public:
    void insert(uint32_t key, uint32_t epoch) noexcept {
        if (key) s_[at(key)].store(pack(key, epoch), std::memory_order_relaxed);
    }
    void erase(uint32_t key) noexcept {
        if (!key) return;
        std::atomic<uint64_t>& s = s_[at(key)];
        uint64_t v = s.load(std::memory_order_relaxed);
        if (static_cast<uint32_t>(v) == key) s.compare_exchange_strong(v, 0, std::memory_order_relaxed);
    }
    bool contains(uint32_t key, uint32_t epoch) const noexcept {
        return key && s_[at(key)].load(std::memory_order_relaxed) == pack(key, epoch);
    }
    static constexpr size_t capacity() noexcept { return N; }

private:
    static uint64_t pack(uint32_t key, uint32_t epoch) noexcept { return static_cast<uint64_t>(epoch) << 32 | key; }
    static size_t at(uint32_t key) noexcept {
        return static_cast<size_t>((static_cast<uint64_t>(key) * 0x9E3779B97F4A7C15ull) >> 40) & (N - 1);
    }
    std::array<std::atomic<uint64_t>, N> s_{};
};
// Un hijo tardío (fuera del preexamen) cuyo padre está en el marco: política explícita de admisión posterior
// (AUDITORIA-2 cambio 1, último párrafo): sólo con pertenencia demostrada AHORA y sitio reservable; si no, la unidad
// queda partida (nunca «ya vendrá»).
inline MemberOut late_verdict(int in_parent_vector, bool room, const MemberIn& m) noexcept {
    if (!m.admitted) return {MemberVerdict::HostUnit, SplitWhy::None};
    if (in_parent_vector < 0) return {MemberVerdict::Split, SplitWhy::LinkUnknown};
    if (in_parent_vector == 0) return {MemberVerdict::Split, SplitWhy::LateNotInVector};
    const MemberOut o = member_verdict(m);
    if (o.v != MemberVerdict::Assign) return o;
    if (!room) return {MemberVerdict::Split, SplitWhy::LateNoRoom};
    return o;
}

// ---------------------------------------------------------------- marcos por Troc (AUDITORIA-2 cambio 2)
// Un marco por (hilo, profundidad de Troc del usuario 2, id de ese Troc): una pila, no un único t_fam. Un Troc
// anidado abre el suyo; el exterior sigue intacto y sus hijos lo encuentran (la búsqueda recorre la pila entera). Al
// terminar su Troc el marco se cierra (censo, reservas sin usar devueltas): ningún puntero sobrevive al Troc.
enum class Kind : uint8_t { Root, HostRoot, Reserved, Member, HostKept, Split, Orphan, HostMember };
inline const char* kind_name(Kind k) noexcept {
    switch (k) {
        case Kind::Root: return "raíz";
        case Kind::HostRoot: return "raíz del anfitrión";
        case Kind::Reserved: return "reservado";
        case Kind::Member: return "miembro";
        case Kind::HostKept: return "host-kept";
        case Kind::Split: return "partido";
        case Kind::Orphan: return "huérfano al nacer";
        case Kind::HostMember: return "de unidad del anfitrión";
    }
    return "?";
}
struct Rec {
    uintptr_t desc, actor;  // locales al proceso y al Troc: nunca cruzan red ni sobreviven al marco
    uint64_t birth;         // token de nacimiento (0 = aún no nacido)
    uint32_t key;
    Kind kind;
    uint8_t unit;           // índice de su unidad en Frame::u (kNoIndex: ninguna; raíz sin hijos: la unidad se crea
                            // al primer hijo tardío, promote_root)
    uint16_t parent;        // índice del registro padre en Frame::r (kNoRec: raíz o desconocido)
    uint8_t depth;
    Cls cls;
    SplitWhy why;
    uint8_t type, projected; // S42: tipo leído y simulación de la extensión; sólo dura el Troc
    OrphanCls oc;           // Kind::Orphan: la clase de su padre (un viajero la hereda a sus descendientes)
};
struct UnitSum {
    uint32_t key;                 // de la raíz
    uint64_t birth;               // de la raíz (token de unidad, AUDITORIA.md §6.1: parent/root birth)
    uint32_t controller;          // el de la unidad (key2) si admitida
    uint16_t root_rec;            // índice de la raíz en Frame::r
    Admit admit;
    bool admitted;                // modo 1: reservas tomadas, raíz asignada
    bool would;                   // probe: se habría admitido
    uint8_t nodes, born, members, host_kept, split, late, max_depth;
    uint16_t split_mask;          // bits de SplitWhy
    uint8_t keys_reserved, keys_used, track_reserved, track_used;
    uint64_t root_node;           // data | index << 32 del nodo de la raíz (para la memoria de unidades partidas)
    uint8_t ctl;                  // veredicto de U-e de la raíz (u2s::Ctl) para una raíz del anfitrión; 0xFF = n/a
};
// Prueba en dos copias 3 (15-10, kit ce5d47d): el armado metió 157 raíces de U-e en UN Troc (01:54:14); con 64
// unidades por marco (una por raíz, también sin hijos) se perdieron 14 raíces con hijos (marco_lleno) y sus hijos
// salieron huérfanos. Ahora una raíz sin hijos sólo ocupa un registro (su unidad se crea si le nace un hijo tardío) y
// los marcos crecen: 1023 registros (índice u16) y 128 unidades (raíces CON hijos o del anfitrión; medido: 18 en el
// Troc del armado). ~48 KB por marco, 8 marcos. Lleno sigue siendo fail-closed (la unidad no se admite).
inline constexpr size_t kFrameRecs = 1023, kFrameUnits = 128;
static_assert(kFrameRecs < kNoRec && kFrameUnits < kNoIndex, "los índices de registro/unidad reservan su centinela");
struct Frame {
    uint64_t troc;                // id del Troc dueño (0 = libre)
    uint16_t n;
    uint8_t nu;
    uint8_t depth;
    uint32_t lost;                // registros que no cupieron (huérfanos/miembros de unidades del anfitrión)
    Rec r[kFrameRecs];
    UnitSum u[kFrameUnits];
};
inline void open_frame(Frame& f, uint64_t troc, uint8_t depth) noexcept {
    f.troc = troc; f.n = 0; f.nu = 0; f.depth = depth; f.lost = 0;
}
inline int find_desc(const Frame& f, uintptr_t desc) noexcept {
    for (uint16_t i = 0; i < f.n; ++i) {
        if (f.r[i].desc == desc) return i;
    }
    return -1;
}
inline bool frame_room(const Frame& f, size_t recs) noexcept {
    return f.nu < kFrameUnits && static_cast<size_t>(f.n) + recs <= kFrameRecs;
}
// Sólo registros (un tardío, una raíz sin hijos): no necesita unidad libre.
inline bool rec_room(const Frame& f, size_t recs) noexcept { return static_cast<size_t>(f.n) + recs <= kFrameRecs; }
// Un registro suelto (huérfano, miembro de unidad del anfitrión, tardío). nullptr: no cabe (se cuenta en lost).
inline Rec* add_rec(Frame& f, uintptr_t desc, Kind kind, uint8_t unit, uint16_t parent, uint8_t depth, Cls cls) noexcept {
    if (f.n >= kFrameRecs) { ++f.lost; return nullptr; }
    Rec& r = f.r[f.n++];
    r.desc = desc; r.actor = 0; r.birth = 0; r.key = 0; r.kind = kind; r.unit = unit; r.parent = parent;
    r.depth = depth; r.cls = cls; r.why = SplitWhy::None; r.oc = OrphanCls::Other; r.type = 0; r.projected = 0;
    return &r;
}
// Una raíz de U-e sin hijos en su preexamen: sólo su registro (Kind::Root, sin unidad). Debe haberse comprobado
// rec_room(f, 1). nullptr: no cabe.
inline Rec* add_childless_root(Frame& f, uintptr_t desc, uintptr_t actor, uint32_t key, uint64_t birth) noexcept {
    Rec* r = add_rec(f, desc, Kind::Root, kNoIndex, kNoRec, 0, Cls::Schedule);
    if (r) { r->actor = actor; r->key = key; r->birth = birth; }
    return r;
}
// ¿Es la raíz sin hijos aún sin unidad (o la misma vuelta al anfitrión porque su clave no se registró)?
inline bool lazy_root(const Rec& r) noexcept {
    return r.unit == kNoIndex && r.parent == kNoRec && (r.kind == Kind::Root || r.kind == Kind::HostRoot);
}
// La unidad de una raíz: su registro y los de TODOS sus descendientes del preexamen (Reserved), de una vez. Debe
// haberse comprobado frame_room(f, 1 + scan.n). Devuelve el índice de la unidad.
inline uint8_t add_unit(Frame& f, uintptr_t root_desc, uintptr_t root_actor, uint32_t key, uint64_t birth,
                        const Scan& s, Admit admit, bool admitted, bool would, uint32_t controller) noexcept {
    const uint8_t ui = f.nu++;
    UnitSum& u = f.u[ui];
    const uint16_t root = f.n;
    u.key = key; u.birth = birth; u.controller = controller; u.root_rec = root; u.admit = admit;
    u.admitted = admitted; u.would = would; u.nodes = s.n; u.born = 0; u.members = 0; u.host_kept = 0; u.split = 0;
    u.late = 0; u.max_depth = s.max_depth; u.split_mask = 0; u.keys_reserved = 0; u.keys_used = 0;
    u.track_reserved = 0; u.track_used = 0; u.root_node = 0; u.ctl = 0xFF;
    Rec& r = f.r[f.n++];
    r.desc = root_desc; r.actor = root_actor; r.birth = birth; r.key = key;
    r.kind = admitted || would ? Kind::Root : Kind::HostRoot;
    r.unit = ui; r.parent = kNoRec; r.depth = 0; r.cls = Cls::Schedule; r.why = SplitWhy::None; r.oc = OrphanCls::Other; r.type = 0; r.projected = 0;
    for (uint8_t i = 0; i < s.n; ++i) {
        Rec& c = f.r[f.n++];
        c.desc = s.nodes[i].desc; c.actor = 0; c.birth = 0; c.key = 0; c.kind = Kind::Reserved; c.unit = ui;
        c.parent = static_cast<uint16_t>(s.nodes[i].parent == kNoIndex ? root : root + 1 + s.nodes[i].parent);
        c.depth = s.nodes[i].depth; c.cls = s.nodes[i].cls; c.why = SplitWhy::None; c.oc = OrphanCls::Other; c.type = 0; c.projected = 0;
    }
    return ui;
}
// La unidad de una raíz sin hijos (add_childless_root), creada al nacer su primer hijo tardío. Kind::Root: la unidad
// sin hijos de siempre (Admit::Childless; admitida en 1, «se admitiría» en probe); Kind::HostRoot (su clave no se pudo
// registrar): del anfitrión. kNoIndex: sin unidad libre (el llamador parte la unidad en modo 1: fail-closed).
inline uint8_t promote_root(Frame& f, Rec& r, bool on, uint32_t controller, uint64_t root_node) noexcept {
    if (!lazy_root(r) || f.nu >= kFrameUnits) return kNoIndex;
    const bool host = r.kind == Kind::HostRoot;
    const uint8_t ui = f.nu++;
    UnitSum& u = f.u[ui];
    u.key = r.key; u.birth = r.birth; u.controller = host ? 0 : controller;
    u.root_rec = static_cast<uint16_t>(&r - f.r); u.admit = host ? Admit::KeysFull : Admit::Childless;
    u.admitted = !host && on; u.would = !host && !on; u.nodes = 0; u.born = 0; u.members = 0; u.host_kept = 0;
    u.split = 0; u.late = 0; u.max_depth = 0; u.split_mask = 0; u.keys_reserved = 0; u.keys_used = 0;
    u.track_reserved = 0; u.track_used = 0; u.root_node = root_node; u.ctl = 0xFF;
    r.unit = ui;
    return ui;
}
// Una raíz con hijos que NO es una unidad del invitado (su veredicto de U-e no fue Assign, o el preexamen falló):
// sólo su registro, para que sus hijos se reconozcan como de una unidad del anfitrión (y el censo la cuente). Debe
// haberse comprobado frame_room(f, 1). AUDITORIA-2 cambio 5: las raíces no asignadas también entran en el registro.
inline uint8_t add_host_root(Frame& f, uintptr_t root_desc, uintptr_t root_actor, uint32_t key, uint64_t birth,
                             Admit admit, uint8_t ctl, uint8_t children) noexcept {
    const uint8_t ui = f.nu++;
    UnitSum& u = f.u[ui];
    u.key = key; u.birth = birth; u.controller = 0; u.root_rec = f.n; u.admit = admit; u.admitted = false;
    u.would = false; u.nodes = children; u.born = 0; u.members = 0; u.host_kept = 0; u.split = 0; u.late = 0;
    u.max_depth = 0; u.split_mask = 0; u.keys_reserved = 0; u.keys_used = 0; u.track_reserved = 0; u.track_used = 0;
    u.root_node = 0; u.ctl = ctl;
    Rec& r = f.r[f.n++];
    r.desc = root_desc; r.actor = root_actor; r.birth = birth; r.key = key; r.kind = Kind::HostRoot; r.unit = ui;
    r.parent = kNoRec; r.depth = 0; r.cls = Cls::Unknown; r.why = SplitWhy::None; r.oc = OrphanCls::Other; r.type = 0; r.projected = 0;
    return ui;
}
// ¿Algún descriptor del subárbol ya nació en este marco (antes que su raíz)?
inline bool any_born(const Frame& f, const Scan& s) noexcept {
    for (uint8_t i = 0; i < s.n; ++i) {
        const int at = find_desc(f, s.nodes[i].desc);
        if (at >= 0 && f.r[at].birth) return true;
    }
    return false;
}
// Al nacer un miembro: su registro (ya Reserved, o recién añadido como tardío) con su desenlace.
inline void born(Frame& f, Rec& r, uintptr_t actor, uint32_t key, uint64_t birth, MemberOut o, bool late) noexcept {
    r.actor = actor; r.key = key; r.birth = birth; r.why = o.why;
    switch (o.v) {
        case MemberVerdict::Assign: r.kind = Kind::Member; break;
        case MemberVerdict::HostKept: r.kind = Kind::HostKept; break;
        case MemberVerdict::HostUnit: r.kind = Kind::HostMember; break;
        case MemberVerdict::Split: r.kind = Kind::Split; break;
    }
    if (r.unit == kNoIndex || r.unit >= f.nu) return;
    UnitSum& u = f.u[r.unit];
    if (u.born < 0xFF) ++u.born;
    if (late && u.late < 0xFF) ++u.late;
    if (o.v == MemberVerdict::Assign && u.members < 0xFF) ++u.members;
    if (o.v == MemberVerdict::HostKept && u.host_kept < 0xFF) ++u.host_kept;
    if (o.v == MemberVerdict::Split) {
        if (u.split < 0xFF) ++u.split;
        u.split_mask = static_cast<uint16_t>(u.split_mask | 1u << static_cast<uint8_t>(o.why));
    }
}

// Pila por hilo: id de Troc e índice de marco (+1; 0 = ninguno) por profundidad (TrocMark de user2_stages.cpp).
inline constexpr uint32_t kDepths = 16;
struct Stack {
    uint64_t id[kDepths] = {};
    uint8_t frame[kDepths] = {};
};
// Entrada de un Troc a `depth` (ya incrementada): id nuevo; un índice que quedara de un Troc anterior a esa misma
// profundidad (una salida perdida) se devuelve para cerrarlo (0 = nada que cerrar).
inline uint8_t stack_begin(Stack& s, uint32_t depth, uint64_t id) noexcept {
    if (!depth || depth >= kDepths) return 0;
    const uint8_t stale = s.frame[depth];
    s.id[depth] = id;
    s.frame[depth] = 0;
    return stale;
}
// Salida del Troc de `depth` (antes de decrementarla): el marco a cerrar (0 = ninguno).
inline uint8_t stack_end(Stack& s, uint32_t depth) noexcept {
    if (!depth || depth >= kDepths) return 0;
    const uint8_t f = s.frame[depth];
    s.frame[depth] = 0;
    s.id[depth] = 0;
    return f;
}
struct Hit {
    int frame = -1;  // índice en el conjunto de marcos
    int rec = -1;
};
// Busca `desc` en la pila entera, de la profundidad actual hacia fuera; un marco sólo vale si su id es el del Troc de
// esa profundidad (frames(i) devuelve el marco i).
template <class Frames>
Hit stack_find(const Stack& s, uint32_t depth, Frames&& frames, uintptr_t desc) noexcept {
    Hit h;
    if (!desc) return h;
    for (uint32_t d = depth < kDepths ? depth : kDepths - 1; d >= 1; --d) {
        if (!s.frame[d]) continue;
        const Frame& f = frames(s.frame[d] - 1);
        if (f.troc != s.id[d]) continue;
        const int at = find_desc(f, desc);
        if (at >= 0) {
            h.frame = s.frame[d] - 1;
            h.rec = at;
            return h;
        }
    }
    return h;
}

// Conjunto de marcos compartido (no TLS: no multiplica su tamaño por cada hilo del juego). Un marco es de un hilo
// (owner = tid) desde que lo abre hasta que su Troc termina; sólo su dueño lo lee o escribe. Lleno: sin marco
// (la unidad no se admite: fail-closed).
template <size_t P>
struct FramePool {
    Frame frames[P];
    std::atomic<uint32_t> owner[P] = {};
    int claim(uint32_t tid) noexcept {
        for (size_t i = 0; i < P; ++i) {
            uint32_t z = 0;
            if (owner[i].compare_exchange_strong(z, tid, std::memory_order_acq_rel)) return static_cast<int>(i);
        }
        return -1;
    }
    void release(int i, uint32_t tid) noexcept {
        if (i < 0 || static_cast<size_t>(i) >= P) return;
        uint32_t t = tid;
        frames[i].troc = 0;
        owner[i].compare_exchange_strong(t, 0, std::memory_order_acq_rel);
    }
    Frame& at(size_t i) noexcept { return frames[i]; }
    static constexpr size_t size() noexcept { return P; }
};

// ---------------------------------------------------------------- censo por raíz (probe y 1)
enum class Census : uint8_t { Complete, Split, Pending, Rejected, Count };
inline const char* census_name(Census c) noexcept {
    switch (c) {
        case Census::Complete: return "completa";
        case Census::Split: return "PARTIDA";
        case Census::Pending: return "pendiente";
        case Census::Rejected: return "con el anfitrión";
        default: return "?";
    }
}
inline Census census_of(const UnitSum& u) noexcept {
    if (!u.admitted && !u.would) return Census::Rejected;
    if (u.split) return Census::Split;
    const unsigned reserved_born = u.born >= u.late ? static_cast<unsigned>(u.born - u.late) : 0u;
    if (reserved_born < u.nodes) return Census::Pending;
    return Census::Complete;
}

// ---------------------------------------------------------------- orden de hojas a raíz en todas las salidas
// AUDITORIA-2 cambio 7: B0100473 -> 474 -> 475 se retira 475, 474, 473. ctl_cleanup (drenaje, logout, su Troc) toma
// como mucho N claves por llamada: elige las N MÁS PROFUNDAS (empates: orden de la tabla), así ninguna clave sale
// antes que un descendiente suyo, ni en esta tanda ni en las siguientes. Profundidad desconocida = 0 (raíz).
template <size_t N>
struct LeafFirst {
    uint32_t key[N];
    uint8_t depth[N];
    uint32_t seq[N];
    size_t n = 0;
    uint32_t next = 0;
    size_t dropped = 0;
    void add(uint32_t k, uint8_t d) noexcept {
        const uint32_t s = next++;
        if (n < N) {
            key[n] = k; depth[n] = d; seq[n] = s; ++n;
            return;
        }
        // Llena: la más superficial (y, entre ellas, la última en orden de tabla) cede su sitio a una más profunda.
        size_t worst = 0;
        for (size_t i = 1; i < n; ++i) {
            if (depth[i] < depth[worst] || (depth[i] == depth[worst] && seq[i] > seq[worst])) worst = i;
        }
        ++dropped;
        if (d <= depth[worst]) return;
        key[worst] = k; depth[worst] = d; seq[worst] = s;
    }
    // Más profunda primero; empates en orden de tabla (con todas las profundidades 0 es el orden de antes).
    void sort() noexcept {
        for (size_t i = 1; i < n; ++i) {
            const uint32_t k = key[i], s = seq[i];
            const uint8_t d = depth[i];
            size_t j = i;
            while (j > 0 && (depth[j - 1] < d || (depth[j - 1] == d && seq[j - 1] > s))) {
                key[j] = key[j - 1]; depth[j] = depth[j - 1]; seq[j] = seq[j - 1];
                --j;
            }
            key[j] = k; depth[j] = d; seq[j] = s;
        }
    }
};
// Retirada parcial (AUDITORIA-2 cambio 7, «gestión explícita de retirada parcial»): recorriendo el plan de hojas a
// raíz, un miembro cuya baja no se confirmó bloquea a su padre en esta pasada (y el padre bloqueado, al suyo): nunca
// se retira un padre dejando vivo a un hijo suyo. Lo bloqueado cuenta como un fallo acotado de su limpieza.
struct RetireGuard {
    uint32_t blocked[kMaxUnit];
    size_t n = 0;
    void block(uint32_t parent) noexcept {
        if (!parent || is_blocked(parent)) return;
        if (n < kMaxUnit) blocked[n++] = parent;
    }
    bool is_blocked(uint32_t key) const noexcept {
        for (size_t i = 0; i < n; ++i) {
            if (blocked[i] == key) return true;
        }
        return false;
    }
};

}  // namespace cdmp::session2::npcfam
