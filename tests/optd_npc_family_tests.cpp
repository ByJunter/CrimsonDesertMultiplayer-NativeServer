// S36 fase B (docs/research/s25/s36-fase-b.md): pruebas puras de npc_family_control. No carga el mod ni el juego:
// la memoria del juego es una arena sintética leída por la misma política `read` que usa el mod (lecturas
// protegidas), con las estructuras de la build 1.0.0.2976 (descriptor +0x2F0 padre, +0x2F8 vector {data, u32 count},
// envoltorio {vtable, inner}). Los contraejemplos de la auditoría se leen de su propio archivo
// (docs/research/s25/s34-investigaciones/datos/auditoria-codex-20261014/a2-familias/contraejemplos_diseno.json).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "mod/session2/npc_activation_logic.h"
#include "mod/session2/npc_family_logic.h"
#include "mod/session2/user2_stages_logic.h"

using namespace cdmp::session2;
using npcfam::Admit;
using npcfam::Cls;
using npcfam::Link;
using npcfam::MemberVerdict;
using npcfam::ScanVerdict;
using npcfam::SplitWhy;

namespace {
int failures = 0;
#define CHECK(x)                                                         \
    do {                                                                 \
        if (!(x)) {                                                      \
            std::printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #x);    \
            ++failures;                                                  \
        }                                                                \
    } while (0)

// ---------------------------------------------------------------- arena sintética
constexpr uintptr_t kBase = 0x140000000ull;   // la base fija de la build
constexpr uint64_t kImage = 0x8000000;
struct Arena {
    std::map<uintptr_t, uint8_t> bytes;
    std::map<uintptr_t, bool> bad;  // direcciones cuya lectura «falla» (fallo de acceso protegido)
    uintptr_t next = 0x10000000;
    template <class T> void put(uintptr_t at, T v) {
        uint8_t b[sizeof(T)];
        std::memcpy(b, &v, sizeof v);
        for (size_t i = 0; i < sizeof v; ++i) bytes[at + i] = b[i];
    }
    uintptr_t alloc(size_t n) {
        const uintptr_t at = next;
        next += (n + 0xFFF) & ~static_cast<uintptr_t>(0xFFF);
        for (size_t i = 0; i < n; ++i) bytes[at + i] = 0;
        return at;
    }
    bool read(uintptr_t at, void* out, size_t n) const {
        auto* o = static_cast<uint8_t*>(out);
        for (size_t i = 0; i < n; ++i) {
            if (bad.count(at + i)) return false;
            auto it = bytes.find(at + i);
            if (it == bytes.end()) return false;
            o[i] = it->second;
        }
        return true;
    }
    // Un descriptor (interior) con su vtable y su padre; vector de hijos vacío.
    uintptr_t desc(uint32_t vt_rva, uintptr_t parent) {
        const uintptr_t d = alloc(0x600);
        put<uintptr_t>(d, kBase + vt_rva);
        put<uintptr_t>(d + npcfam::kDesc_Parent, parent);
        return d;
    }
    // push_back(parent+0x2F8, envoltorio{wrap_vt, child}) como 0x43DB70 (vector de punteros de 8 B).
    uintptr_t link(uintptr_t parent, uint32_t wrap_rva, uintptr_t child) {
        uintptr_t data = 0;
        uint32_t count = 0;
        read(parent + npcfam::kDesc_Children, &data, 8);
        read(parent + npcfam::kDesc_ChildCount, &count, 4);
        if (!data) {
            data = alloc(64 * 8);
            put<uintptr_t>(parent + npcfam::kDesc_Children, data);
        }
        const uintptr_t w = alloc(0x10);
        put<uintptr_t>(w, kBase + wrap_rva);
        put<uintptr_t>(w + npcfam::kWrap_Inner, child);
        put<uintptr_t>(data + count * 8ull, w);
        put<uint32_t>(parent + npcfam::kDesc_ChildCount, count + 1);
        return w;
    }
};
struct ArenaRead {
    const Arena* a;
    bool operator()(uintptr_t at, void* out, size_t n) const { return a->read(at, out, n); }
};

// ---------------------------------------------------------------- el archivo de contraejemplos de la auditoría
std::string slurp(const char* rel) {
    std::ifstream f(std::string(CDMP_SOURCE_DIR) + "/" + rel, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
// Los enteros que siguen a `"name":` en orden de aparición.
std::vector<long long> ints_after(const std::string& s, const std::string& name) {
    std::vector<long long> out;
    const std::string pat = "\"" + name + "\":";
    for (size_t at = s.find(pat); at != std::string::npos; at = s.find(pat, at + 1)) {
        size_t i = at + pat.size();
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r')) ++i;
        if (i < s.size() && (s[i] == '-' || (s[i] >= '0' && s[i] <= '9'))) out.push_back(std::stoll(s.substr(i, 20)));
    }
    return out;
}
const char* kCounterexamples =
    "docs/research/s25/s34-investigaciones/datos/auditoria-codex-20261014/a2-familias/contraejemplos_diseno.json";

// ---------------------------------------------------------------- capacidades sintéticas (para reserve_unit)
struct Cap {
    size_t used = 0, cap = 64, reserved = 0;
    bool can_reserve(size_t n) const { return used + reserved + n <= cap; }
    bool reserve(size_t n) {
        if (!can_reserve(n)) return false;
        reserved += n;
        return true;
    }
    void unreserve(size_t n) { reserved -= n < reserved ? n : reserved; }
};

void types_and_reads() {
    CHECK(npcfam::parse_types("").mask == npcfam::kDefaultTypes && !npcfam::parse_types("").bad);
    CHECK(npcfam::parse_types("3,6").mask == (1u << 3 | 1u << 6));
    CHECK(npcfam::parse_types("3, 6, 7").mask == (1u << 3 | 1u << 6 | 1u << 7));
    CHECK(npcfam::parse_types("3").mask == 1u << 3);
    CHECK(npcfam::parse_types("4").bad && npcfam::parse_types("4").mask == npcfam::kDefaultTypes);
    CHECK(npcfam::parse_types("3,x").bad && npcfam::parse_types(",").bad);
    CHECK(!npcfam::type_in({}, 7) && npcfam::type_in({}, 6) && !npcfam::type_in({}, 200));
    // Tres estados: ilegible (Unknown) NUNCA es ausente (AUDITORIA.md §6.3; el mismo principio que pubg::Cc).
    Arena a;
    ArenaRead rd{&a};
    const uintptr_t root = a.desc(npcfam::kVtNpcSchedule, 0);
    const uintptr_t child = a.desc(npcfam::kVtNpcSchedule, root);
    CHECK(npcfam::read_parent(rd, root).state == Link::Absent);
    CHECK(npcfam::read_parent(rd, child).state == Link::Present && npcfam::read_parent(rd, child).parent == root);
    a.bad[child + npcfam::kDesc_Parent + 3] = true;
    CHECK(npcfam::read_parent(rd, child).state == Link::Unknown);  // lectura fallida: no es «raíz»
    const uintptr_t odd = a.desc(npcfam::kVtNpcSchedule, 0x1234);  // valor implausible
    CHECK(npcfam::read_parent(rd, odd).state == Link::Unknown);
    CHECK(npcfam::read_parent(rd, 0).state == Link::Unknown);
    CHECK(npcfam::read_matches(rd, 0x5, 1) == -1);
}

// Profundidad 2/3: B0100473 (tipo 3) -> B0100474 (tipo 3) -> B0100475 (tipo 7, InstantGimmick) del ensayo C, y una
// cabra (tipo 6) a profundidad 1.
struct Tree {
    Arena a;
    uintptr_t root = 0, c474 = 0, g475 = 0, goat = 0;
    explicit Tree(uintptr_t start = 0x10000000) {
        a.next = start;
        root = a.desc(npcfam::kVtNpcSchedule, 0);
        c474 = a.desc(npcfam::kVtNpcSchedule, root);
        goat = a.desc(npcfam::kVtNpcSchedule, root);
        g475 = a.desc(npcfam::kVtInstantGimmick, c474);
        a.link(root, npcfam::kVtWrapSimple, c474);
        a.link(root, npcfam::kVtWrapSimple, goat);
        a.link(c474, npcfam::kVtWrapSimple, g475);
    }
};
void scan_trees() {
    Tree t;
    ArenaRead rd{&t.a};
    npcfam::Scan s;
    npcfam::scan_subtree(rd, t.root, kBase, kImage, s);
    CHECK(s.v == ScanVerdict::Ok && s.n == 3 && s.max_depth == 2 && s.schedule == 2 && s.gimmick == 1);
    CHECK(s.nodes[0].desc == t.c474 && s.nodes[0].parent == npcfam::kNoIndex && s.nodes[0].depth == 1);
    CHECK(s.nodes[1].desc == t.goat && s.nodes[1].depth == 1);
    CHECK(s.nodes[2].desc == t.g475 && s.nodes[2].parent == 0 && s.nodes[2].depth == 2 && s.nodes[2].cls == Cls::Gimmick);
    // Profundidad 3 y 4 caben; 5 no (kMaxDepth = 4).
    Arena d;
    ArenaRead dr{&d};
    uintptr_t chain[6];
    chain[0] = d.desc(npcfam::kVtNpcSchedule, 0);
    for (int i = 1; i < 6; ++i) {
        chain[i] = d.desc(npcfam::kVtNpcSchedule, chain[i - 1]);
        d.link(chain[i - 1], npcfam::kVtWrapSimple, chain[i]);
    }
    npcfam::scan_subtree(dr, chain[2], kBase, kImage, s);  // tres niveles debajo de chain[2]: profundidad 3
    CHECK(s.v == ScanVerdict::Ok && s.n == 3 && s.max_depth == 3 && s.nodes[2].parent == 1 && s.nodes[1].parent == 0);
    npcfam::scan_subtree(dr, chain[0], kBase, kImage, s);
    CHECK(s.v == ScanVerdict::TooDeep);
    d.put<uint32_t>(chain[4] + npcfam::kDesc_ChildCount, 0);  // corta en profundidad 4
    npcfam::scan_subtree(dr, chain[0], kBase, kImage, s);
    CHECK(s.v == ScanVerdict::Ok && s.n == 4 && s.max_depth == 4);
    // Lectura fallida a mitad del subárbol: la unidad entera no se demuestra (nunca «sin hijos»).
    t.a.bad[t.g475 + npcfam::kDesc_Parent] = true;
    npcfam::scan_subtree(rd, t.root, kBase, kImage, s);
    CHECK(s.v == ScanVerdict::Unreadable);
    t.a.bad.clear();
    // Envoltorios excluidos / desconocidos y vínculos incoherentes resuelven la unidad ENTERA.
    {
        Tree x;
        ArenaRead xr{&x.a};
        x.a.link(x.goat, npcfam::kVtWrapRider, x.a.desc(npcfam::kVtNpcSchedule, x.goat));  // _Rider: sin ruta probada
        npcfam::scan_subtree(xr, x.root, kBase, kImage, s);
        CHECK(s.v == ScanVerdict::Excluded && s.bad_wrap == npcfam::kVtWrapRider);
    }
    {
        Tree x;
        ArenaRead xr{&x.a};
        x.a.link(x.goat, 0x5B04F00, x.a.desc(npcfam::kVtNpcSchedule, x.goat));
        npcfam::scan_subtree(xr, x.root, kBase, kImage, s);
        CHECK(s.v == ScanVerdict::UnknownWrapper);
    }
    {
        Tree x;
        ArenaRead xr{&x.a};
        x.a.link(x.goat, npcfam::kVtWrapSimple, x.a.desc(npcfam::kVtNpcSchedule, x.root));  // +0x2F0 no es su padre
        npcfam::scan_subtree(xr, x.root, kBase, kImage, s);
        CHECK(s.v == ScanVerdict::Inconsistent);
    }
    {
        Tree x;
        ArenaRead xr{&x.a};
        x.a.put<uint64_t>(x.goat + npcfam::kDesc_User, 0x1234);  // un usuario explícito en un hijo
        npcfam::scan_subtree(xr, x.root, kBase, kImage, s);
        CHECK(s.v == ScanVerdict::DescUser);
    }
    {
        Tree x;
        ArenaRead xr{&x.a};
        x.a.put<uint32_t>(x.root + npcfam::kDesc_ChildCount, 65);
        npcfam::scan_subtree(xr, x.root, kBase, kImage, s);
        CHECK(s.v == ScanVerdict::TooBig);
    }
    {
        Arena c;
        ArenaRead cr{&c};
        const uintptr_t r = c.desc(npcfam::kVtNpcSchedule, 0), k = c.desc(npcfam::kVtNpcSchedule, r);
        c.link(r, npcfam::kVtWrapSimple, k);
        c.link(r, npcfam::kVtWrapSimple, k);  // el mismo interior dos veces
        npcfam::scan_subtree(cr, r, kBase, kImage, s);
        CHECK(s.v == ScanVerdict::Cycle);
    }
    // Sin hijos: Ok con n 0 (la raíz sigue la regla de U-e de siempre).
    Arena e;
    ArenaRead er{&e};
    npcfam::scan_subtree(er, e.desc(npcfam::kVtNpcSchedule, 0), kBase, kImage, s);
    CHECK(s.v == ScanVerdict::Ok && s.n == 0);
    npcfam::AdmitIn none;
    CHECK(npcfam::admit_unit(none) == Admit::Childless);
    // Pertenencia de un tardío y su clase.
    uint32_t wrap = 0, inner = 0;
    CHECK(npcfam::in_vector(rd, t.c474, t.g475, kBase, kImage, &wrap, &inner) == 1 && wrap == npcfam::kVtWrapSimple &&
          inner == npcfam::kVtInstantGimmick);
    CHECK(npcfam::classify(wrap, inner) == Cls::Gimmick);
    CHECK(npcfam::in_vector(rd, t.c474, t.goat) == 0);
    t.a.bad[t.c474 + npcfam::kDesc_ChildCount] = true;
    CHECK(npcfam::in_vector(rd, t.c474, t.g475) == -1);
    CHECK(npcfam::classify(npcfam::kVtWrapAutoSpawnGimmick, npcfam::kVtInstantGimmick) == Cls::Gimmick);
    CHECK(npcfam::classify(npcfam::kVtWrapSimple, npcfam::kVtNpcSchedule) == Cls::Schedule);
    CHECK(npcfam::classify(npcfam::kVtWrapDocking, npcfam::kVtNpcSchedule) == Cls::Excluded);
    CHECK(npcfam::classify(npcfam::kVtWrapGroupActionMember, npcfam::kVtNpcSchedule) == Cls::Excluded);
}

// «63 entradas ocupadas + raíz con hijo» (contraejemplos_diseno.json, capacidad_no_reservada): reservar sólo la raíz
// la admite y el hijo ya no cabe; reservar la unidad entera la rechaza ENTERA y no toca nada.
void reservation_63_plus_2() {
    const std::string j = slurp(kCounterexamples);
    const auto occupied = ints_after(j, "ocupados"), subtree = ints_after(j, "subarbol_nodos");
    CHECK(!occupied.empty() && !subtree.empty());
    const size_t used = occupied.empty() ? 63 : static_cast<size_t>(occupied[0]);
    const size_t nodes = subtree.empty() ? 2 : static_cast<size_t>(subtree[0]);
    CHECK(used == 63 && nodes == 2);
    // El diseño literal: raíz admitida (63 < 64) y después el hijo no cabe -> raíz del invitado, hijo del anfitrión.
    CHECK(used < 64 && !(used + 1 < 64));
    Cap keys{used, 64, 0}, track{0, 1024, 0};
    npcfam::AdmitIn in;
    in.scan = ScanVerdict::Ok;
    in.nodes = static_cast<uint8_t>(nodes - 1);  // la raíz y un hijo
    const Admit a = npcfam::reserve_unit(in, nodes, nodes, keys, track, false);
    CHECK(a == Admit::KeysFull);
    CHECK(keys.reserved == 0 && track.reserved == 0);  // nada reservado, nada asignado
    // La segunda capacidad falla: la primera se devuelve.
    Cap k2{0, 64, 0}, t2{63, 64, 0};
    CHECK(npcfam::reserve_unit(in, nodes, nodes, k2, t2, false) == Admit::TrackFull && k2.reserved == 0 && t2.reserved == 0);
    // Con sitio: se reserva todo y cada alta consume una.
    Cap k3{0, 64, 0}, t3{0, 64, 0};
    CHECK(npcfam::reserve_unit(in, nodes, nodes, k3, t3, false) == Admit::Admit && k3.reserved == 2 && t3.reserved == 2);
    // probe: las mismas comprobaciones, sin reservar nada.
    Cap k4{used, 64, 0}, t4{0, 64, 0};
    CHECK(npcfam::reserve_unit(in, nodes, nodes, k4, t4, true) == Admit::KeysFull && k4.reserved == 0);
    Cap k5{0, 64, 0}, t5{0, 64, 0};
    CHECK(npcfam::reserve_unit(in, nodes, nodes, k5, t5, true) == Admit::Admit && k5.reserved == 0 && t5.reserved == 0);
    // Las tablas reales: KeyTable (3/4 de N para altas sin reserva) y la tabla de activación (N - N/8).
    u2s::KeyTable<16> kt;  // 12 huecos
    for (uint32_t i = 1; i <= 11; ++i) CHECK(kt.put(0xB0100000u + i, u2s::kKeyAssigned, 1));
    CHECK(!kt.reserve(2) && kt.reserved() == 0);  // 11 + 2 > 12: la unidad entera fuera
    CHECK(kt.reserve(1) && kt.reserved() == 1);
    CHECK(!kt.put(0xB0100100u, u2s::kKeyAssigned, 1));  // una alta sin reserva no gasta la reservada
    CHECK(kt.put(0xB0100101u, u2s::kKeyAssigned | u2s::kKeyFamily, 1, true) && kt.reserved() == 0);
    npcact::Table<64, 8> tt;  // max_count 56
    for (uint32_t i = 1; i <= 55; ++i) CHECK(tt.claim(0xB0200000u + i, i) != nullptr);
    CHECK(!tt.reserve(2) && tt.reserved == 0);
    CHECK(tt.reserve(1));
    CHECK(tt.claim(0xB0200100u, 100) == nullptr);  // sin reserva: no cabe
    CHECK(tt.claim(0xB0200101u, 101, true) != nullptr && tt.reserved == 0);
    // admit_unit: el orden de los motivos.
    npcfam::AdmitIn g;
    g.scan = ScanVerdict::Ok; g.nodes = 2; g.gimmicks = 1;
    CHECK(npcfam::admit_unit(g) == Admit::GimmickNeedsGate);
    g.gate_on = true;
    CHECK(npcfam::admit_unit(g) == Admit::Admit);
    g.gate_on = false; g.gimmicks_assignable = true;
    CHECK(npcfam::admit_unit(g) == Admit::Admit);
    g.node_split_before = true;
    CHECK(npcfam::admit_unit(g) == Admit::NodeSplitBefore);
    g.node_split_before = false; g.child_born_first = true;
    CHECK(npcfam::admit_unit(g) == Admit::ChildBornFirst);
    g.child_born_first = false; g.frame = false;
    CHECK(npcfam::admit_unit(g) == Admit::NoFrame);
    g.frame = true; g.frame_room = false;
    CHECK(npcfam::admit_unit(g) == Admit::FrameFull);
    g.scan = ScanVerdict::Unreadable;
    CHECK(npcfam::admit_unit(g) == Admit::ScanFailed);
    for (uint8_t i = 0; i < static_cast<uint8_t>(Admit::Count); ++i) CHECK(npcfam::admit_name(static_cast<Admit>(i))[0] != '?');
}

// Marcos anidados (contraejemplos_diseno.json, troc_anidado): un único t_fam pierde la raíz A al entrar en el Troc B y
// volver; la pila conserva el marco exterior y sus hijos lo encuentran desde el Troc anidado.
void nested_frames() {
    const std::string j = slurp(kCounterexamples);
    const auto trocs = ints_after(j, "troc");
    CHECK(!trocs.empty() && trocs[0] == 100);
    {  // el modelo literal del informe: se reproduce la pérdida
        struct One { uint64_t troc; int records; } fam{100, 1};
        fam = {101, 1};
        if (fam.troc != 100) fam = {100, 0};
        CHECK(fam.records == 0);
    }
    static npcfam::FramePool<4> pool;
    npcfam::Stack st;
    const auto frames = [](size_t i) -> const npcfam::Frame& { return pool.at(i); };
    Tree outer, inner(0x20000000);
    ArenaRead ro{&outer.a}, ri{&inner.a};
    npcfam::Scan so, si;
    npcfam::scan_subtree(ro, outer.root, kBase, kImage, so);
    npcfam::scan_subtree(ri, inner.root, kBase, kImage, si);
    // Troc A (id 100) a profundidad 1: su raíz y su subárbol.
    CHECK(npcfam::stack_begin(st, 1, 100) == 0);
    const int fa = pool.claim(11);
    CHECK(fa >= 0);
    npcfam::open_frame(pool.at(static_cast<size_t>(fa)), 100, 1);
    st.frame[1] = static_cast<uint8_t>(fa + 1);
    CHECK(npcfam::frame_room(pool.at(static_cast<size_t>(fa)), 1u + so.n));
    npcfam::add_unit(pool.at(static_cast<size_t>(fa)), outer.root, 0x7000, 0xB0100473, 20, so, Admit::Admit, true, false,
                     0x90100249);
    // Troc B anidado (id 101) a profundidad 2, con otra raíz.
    CHECK(npcfam::stack_begin(st, 2, 101) == 0);
    const int fb = pool.claim(11);
    CHECK(fb >= 0 && fb != fa);
    npcfam::open_frame(pool.at(static_cast<size_t>(fb)), 101, 2);
    st.frame[2] = static_cast<uint8_t>(fb + 1);
    npcfam::add_unit(pool.at(static_cast<size_t>(fb)), inner.root, 0x8000, 0xB0100500, 30, si, Admit::Admit, true, false,
                     0x90100249);
    // Un hijo de A nacido DENTRO de B se encuentra en el marco exterior.
    npcfam::Hit h = npcfam::stack_find(st, 2, frames, outer.goat);
    CHECK(h.frame == fa && h.rec > 0);
    h = npcfam::stack_find(st, 2, frames, inner.goat);
    CHECK(h.frame == fb);
    // Sale B: su marco se cierra; A sigue intacto y sus hijos lo encuentran a profundidad 1.
    CHECK(npcfam::stack_end(st, 2) == static_cast<uint8_t>(fb + 1));
    pool.release(fb, 11);
    h = npcfam::stack_find(st, 1, frames, outer.c474);
    CHECK(h.frame == fa && pool.at(static_cast<size_t>(fa)).r[h.rec].kind == npcfam::Kind::Reserved);
    CHECK(npcfam::stack_find(st, 1, frames, inner.goat).rec < 0);  // ningún puntero de B sobrevive
    // Un marco cuyo id no es el del Troc de esa profundidad no vale (otro Troc reutilizó la profundidad).
    st.id[1] = 999;
    CHECK(npcfam::stack_find(st, 1, frames, outer.c474).rec < 0);
    st.id[1] = 100;
    // Una salida perdida deja un índice: la siguiente entrada a esa profundidad lo devuelve para cerrarlo.
    CHECK(npcfam::stack_begin(st, 1, 102) == static_cast<uint8_t>(fa + 1));
    pool.release(fa, 11);
    CHECK(pool.claim(12) >= 0 && pool.claim(12) >= 0 && pool.claim(12) >= 0 && pool.claim(12) >= 0);
    CHECK(pool.claim(12) < 0);  // lleno: sin marco (la unidad no se admite)
    for (int i = 0; i < 4; ++i) pool.release(i, 12);
    CHECK(npcfam::stack_begin(st, 16, 1) == 0 && npcfam::stack_end(st, 0) == 0);  // fuera de rango: nada
}

// Generación del padre (contraejemplos_diseno.json, familia_clave_reutilizada): el hijo 11 era de la encarnación 10 de
// la clave 1; family_into sólo guardaba parent=1 y lo unía a la encarnación 20.
void parent_generation() {
    const std::string j = slurp(kCounterexamples);
    const auto keys = ints_after(j, "key"), births = ints_after(j, "birth"), parents = ints_after(j, "parent");
    CHECK(keys.size() >= 2 && births.size() >= 2 && parents.size() >= 2);
    const uint32_t root_key = keys.size() >= 2 ? static_cast<uint32_t>(keys[0]) : 1;
    const uint32_t child_key = keys.size() >= 2 ? static_cast<uint32_t>(keys[1]) : 2;
    const uint64_t root_birth = births.size() >= 2 ? static_cast<uint64_t>(births[0]) : 20;
    const uint64_t child_birth = births.size() >= 2 ? static_cast<uint64_t>(births[1]) : 11;
    CHECK(root_key == 1 && child_key == 2 && root_birth == 20 && child_birth == 11);
    const auto mk = [](uint32_t key, uint64_t birth, uint32_t parent) {
        npcact::Actor a;
        a.key = key; a.birth = birth; a.parent = parent; a.controller = 0x90100249; a.born_us = 1;
        a.node = {1, 0x3F8972FE, 0, 0x20000174, 100};
        a.assigned = true;
        return a;
    };
    {  // fase A (vínculo sólo por clave): el superviviente entra en la familia de otra encarnación
        npcact::Table<64, 8> t;
        t.born(mk(root_key, root_birth, 0));
        t.born(mk(child_key, child_birth, root_key));
        CHECK(t.family(root_key).size == 2);
    }
    {  // S36: su vínculo se registró en su mid con el nacimiento 10 de su padre: queda fuera de la encarnación 20
        npcact::Table<64, 8> t;
        t.born(mk(root_key, root_birth, 0));
        npcact::Actor c = mk(child_key, child_birth, root_key);
        c.parent_birth = 10;
        t.born(c);
        const auto f = t.family(root_key);
        CHECK(f.size == 1 && !f.contains(child_key));
        // ...y un hijo de ESTA encarnación sí entra (y, a profundidad 2, también su nieto).
        npcact::Actor ok = mk(3, 21, root_key);
        ok.parent_birth = root_birth;
        t.born(ok);
        npcact::Actor grand = mk(4, 22, 3);
        grand.parent_birth = 21;
        t.born(grand);
        npcact::Actor wrong_grand = mk(5, 23, 3);
        wrong_grand.parent_birth = 7;  // de otra encarnación de la clave 3
        t.born(wrong_grand);
        const auto g = t.family(root_key);
        CHECK(g.size == 3 && g.contains(3) && g.contains(4) && !g.contains(5) && !g.contains(child_key));
        // Un vínculo visto sólo en la publicación (parent_birth 0) se une por clave, como en la fase A.
        t.born(mk(6, 24, root_key));
        CHECK(t.family(root_key).contains(6));
    }
}

// La retirada de hojas a raíz en toda salida (AUDITORIA-2 cambio 7): B0100473 -> 474 -> 475.
void leaf_to_root_orders() {
    // (1) La contención: el orden de family_into es padre antes que hijo; al revés, hojas a raíz.
    npcact::Table<64, 8> t;
    const auto mk = [](uint32_t key, uint64_t birth, uint32_t parent, uint64_t pb) {
        npcact::Actor a;
        a.key = key; a.birth = birth; a.parent = parent; a.parent_birth = pb; a.controller = 0x90100249;
        a.node = {1, 0x3F8972FE, 0, 0x20000174, 100};
        return a;
    };
    // Se insertan en un orden de tabla desfavorable (el nieto primero).
    t.born(mk(0xB0100475, 3, 0xB0100474, 2));
    t.born(mk(0xB0100474, 2, 0xB0100473, 1));
    t.born(mk(0xB0100473, 1, 0, 0));
    const auto f = t.family(0xB0100473);
    CHECK(f.size == 3);
    std::vector<uint32_t> retire;
    for (size_t i = f.size; i > 0; --i) retire.push_back(f.actors[i - 1].key);
    CHECK(retire.size() == 3 && retire[0] == 0xB0100475 && retire[1] == 0xB0100474 && retire[2] == 0xB0100473);
    // (2) ctl_cleanup (drenaje, logout, su Troc): las N más profundas, la más profunda primero.
    npcfam::LeafFirst<64> lf;
    lf.add(0xB0100473, 0); lf.add(0xB0100474, 1); lf.add(0xB0100475, 2); lf.add(0xB0100800, 0);
    lf.sort();
    CHECK(lf.n == 4 && lf.key[0] == 0xB0100475 && lf.key[1] == 0xB0100474 && lf.key[2] == 0xB0100473 &&
          lf.key[3] == 0xB0100800);  // empates: orden de tabla
    // Tanda acotada: sólo caben 2 -> las dos más profundas; la raíz espera a la tanda siguiente.
    npcfam::LeafFirst<2> small;
    small.add(0xB0100473, 0); small.add(0xB0100474, 1); small.add(0xB0100800, 0); small.add(0xB0100475, 2);
    small.sort();
    CHECK(small.n == 2 && small.key[0] == 0xB0100475 && small.key[1] == 0xB0100474 && small.dropped == 2);
    // Sin unidades (profundidad 0 en todas): exactamente el orden de antes, las primeras N de la tabla.
    npcfam::LeafFirst<2> flat;
    flat.add(1, 0); flat.add(2, 0); flat.add(3, 0);
    flat.sort();
    CHECK(flat.n == 2 && flat.key[0] == 1 && flat.key[1] == 2);
    // (3) Retirada parcial: si 475 no se retira, ni 474 ni 473 en esta pasada.
    npcfam::RetireGuard g;
    std::vector<uint32_t> called;
    for (uint32_t key : retire) {
        const uint32_t parent = key == 0xB0100475 ? 0xB0100474 : key == 0xB0100474 ? 0xB0100473 : 0;
        if (g.is_blocked(key)) {
            g.block(parent);
            continue;
        }
        called.push_back(key);
        const bool done = key != 0xB0100475;  // la baja del nieto falla (Logout rechazado)
        if (!done) g.block(parent);
    }
    CHECK(called.size() == 1 && called[0] == 0xB0100475);
    CHECK(g.is_blocked(0xB0100474) && g.is_blocked(0xB0100473));
    // Un hermano de otra rama no se bloquea.
    CHECK(!g.is_blocked(0xB0100490));
    // Lo bloqueado es un fallo acotado de su limpieza (kCleanBlocked), nunca una baja.
    npcact::Table<16, 8> tb;
    npcact::Actor p = mk(0xB0100474, 2, 0xB0100473, 1);
    p.local_retired = true;
    tb.born(p);
    CHECK(tb.cleanup_result(p.key, p.birth, npcact::kCleanBlocked, 100));
    CHECK(!tb.find(p.key)->removed && tb.find(p.key)->cleanup_failures == 1);
}

// La unidad partida y los miembros (containment_due / no_progress).
void split_and_members() {
    npcact::Policy pol;
    npcact::Context ctx;
    ctx.now = 100'000'000;
    npcact::Actor root;
    root.key = 0xB01004BE; root.birth = 1; root.assigned = true; root.queued_us = 1; root.applied_us = 50;
    root.controller = 0x90100249; root.fam = npcact::kFamRoot; root.unit_key = root.key; root.unit_birth = 1;
    CHECK(npcact::containment_due(root, pol, ctx) == npcact::Due::No);  // con progreso: nada
    root.family_split = true;
    CHECK(npcact::containment_due(root, pol, ctx) == npcact::Due::Retire);  // partida: entera, sin plazo
    CHECK(npcact::no_progress(root, ctx.now));
    npcact::Actor sel = root;
    CHECK(npcact::recheck_root(root, sel, pol, ctx) == npcact::Recheck::Valid);
    npcact::Actor goat;
    goat.key = 0xB01004BF; goat.birth = 2; goat.assigned = true; goat.queued_us = 1; goat.controller = 0x90100249;
    goat.fam = npcact::kFamMember; goat.unit_key = root.key; goat.unit_birth = 1; goat.parent = root.key;
    goat.parent_birth = 1;
    CHECK(npcact::family_member(goat));
    CHECK(npcact::containment_due(goat, pol, ctx) == npcact::Due::No);  // un miembro nunca vence solo
    CHECK(!npcact::no_progress(goat, ctx.now));
    goat.orphaned = true;
    CHECK(npcact::containment_due(goat, pol, ctx) == npcact::Due::Retire);  // huérfano: como siempre
    // Sin familia (fam 0) todo es S35: la cabra asignada sin progreso vencería sola.
    npcact::Actor legacy = goat;
    legacy.orphaned = false; legacy.fam = 0; legacy.unit_key = 0;
    CHECK(npcact::containment_due(legacy, pol, ctx) == npcact::Due::Retire);
    // ctl_stats: un miembro no es 'unaccepted' (no dispara el aborto antiguo); la raíz sí cuenta como siempre.
    u2s::CtlStats s;
    u2s::ctl_stats_add(s, u2s::kKeyAssigned | u2s::kKeyFamily | u2s::kKeyMember, 0, 0, 10'000'000, 0);
    u2s::ctl_stats_add(s, u2s::kKeyAssigned | u2s::kKeyFamily, 0, 0, 10'000'000, 0);
    CHECK(s.members == 1 && s.unaccepted == 1);
}

// La decisión de un miembro en su nacimiento: por parentesco, no por tipo.
void member_matrix() {
    npcfam::MemberIn m;
    m.admitted = true; m.cls = Cls::Schedule; m.type = 6; m.link = Link::Present; m.link_matches = true;
    m.parent_alive = 1; m.parent_ctl = 1; m.old_ctl_known = true; m.desc_user_known = true; m.actor_user_known = true;
    m.host_field = true; m.owner_branch = true; m.key2 = 0x90100249; m.key_ok = true;
    CHECK(npcfam::member_verdict(m).v == MemberVerdict::Assign);  // la cabra / el perro tipo 6
    m.type = 3;
    CHECK(npcfam::member_verdict(m).v == MemberVerdict::Assign);  // un hijo de horario tipo 3
    const auto split = [&](npcfam::MemberIn x, SplitWhy w) {
        const auto o = npcfam::member_verdict(x);
        return o.v == MemberVerdict::Split && o.why == w;
    };
    npcfam::MemberIn x = m;
    x.type = 4;
    CHECK(split(x, SplitWhy::Type));  // tipo 4 (personaje con nombre) bajo _Simple+NPCSchedule: inesperado
    x.type = 5;
    CHECK(split(x, SplitWhy::Type));
    x.type = 0;
    CHECK(split(x, SplitWhy::Type));  // ilegible
    x = m; x.types.mask = 1u << 3;
    x.type = 6;
    CHECK(split(x, SplitWhy::Type));  // cohorte sólo 3
    // Tipo 7 (InstantGimmick): con el anfitrión por defecto; asignable sólo con 7 en la cohorte.
    x = m; x.cls = Cls::Gimmick; x.type = 7;
    CHECK(npcfam::member_verdict(x).v == MemberVerdict::HostKept);
    x.parent_ctl = 0;
    CHECK(npcfam::member_verdict(x).v == MemberVerdict::HostKept);  // no se escribe nada: el padre da igual
    x.parent_ctl = 1; x.types.mask = static_cast<uint16_t>(npcfam::kDefaultTypes | 1u << 7);
    CHECK(npcfam::member_verdict(x).v == MemberVerdict::Assign);
    x.type = 6;
    CHECK(split(x, SplitWhy::Type));  // un gimmick que no es tipo 7
    x = m; x.cls = Cls::Schedule; x.type = 7;
    CHECK(split(x, SplitWhy::Type));
    x = m; x.admitted = false;
    CHECK(npcfam::member_verdict(x).v == MemberVerdict::HostUnit);
    x = m; x.link = Link::Unknown;
    CHECK(split(x, SplitWhy::LinkUnknown));
    x = m; x.link_matches = false;
    CHECK(split(x, SplitWhy::LinkChanged));
    x = m; x.parent_alive = -1;
    CHECK(split(x, SplitWhy::ParentGone));
    x = m; x.parent_alive = 0;
    CHECK(split(x, SplitWhy::ParentGone));
    x = m; x.parent_ctl = -1;
    CHECK(split(x, SplitWhy::ParentController));
    x = m; x.old_ctl = 0x90100000;
    CHECK(split(x, SplitWhy::HasController));
    x = m; x.old_ctl_known = false;
    CHECK(split(x, SplitWhy::HasController));
    x = m; x.desc_user = 1;
    CHECK(split(x, SplitWhy::DescUser));
    x = m; x.actor_user_known = false;
    CHECK(split(x, SplitWhy::UserChild));
    x = m; x.host_field = false;
    CHECK(split(x, SplitWhy::NotHostField));
    x = m; x.owner_branch = false;
    CHECK(split(x, SplitWhy::NotOwnerBranch));
    x = m; x.key_ok = false;
    CHECK(split(x, SplitWhy::NoKey));
    x = m; x.stopped = true;
    CHECK(split(x, SplitWhy::Stopped));
    // Tardío (fuera del preexamen): sólo con pertenencia demostrada ahora y sitio.
    CHECK(npcfam::late_verdict(1, true, m).v == MemberVerdict::Assign);
    CHECK(npcfam::late_verdict(0, true, m).why == SplitWhy::LateNotInVector);
    CHECK(npcfam::late_verdict(-1, true, m).why == SplitWhy::LinkUnknown);
    CHECK(npcfam::late_verdict(1, false, m).why == SplitWhy::LateNoRoom);
    x = m; x.admitted = false;
    CHECK(npcfam::late_verdict(0, false, x).v == MemberVerdict::HostUnit);
    for (uint8_t i = 0; i < static_cast<uint8_t>(SplitWhy::Count); ++i) CHECK(npcfam::split_name(static_cast<SplitWhy>(i))[0] != '?');
}

// Censo por raíz: completa / partida / pendiente / con el anfitrión, con los registros del marco.
void census_and_frame() {
    static npcfam::FramePool<1> pool;
    npcfam::Frame& f = pool.at(0);
    npcfam::open_frame(f, 7, 1);
    Tree t;
    ArenaRead rd{&t.a};
    npcfam::Scan s;
    npcfam::scan_subtree(rd, t.root, kBase, kImage, s);
    const uint8_t ui = npcfam::add_unit(f, t.root, 0x7000, 0xB0100473, 20, s, Admit::Admit, true, false, 0x90100249);
    CHECK(f.n == 4 && f.u[ui].nodes == 3);
    CHECK(npcfam::census_of(f.u[ui]) == npcfam::Census::Pending);  // nadie nacido aún
    CHECK(npcfam::any_born(f, s) == false);
    const int c474 = npcfam::find_desc(f, t.c474), goat = npcfam::find_desc(f, t.goat), g475 = npcfam::find_desc(f, t.g475);
    CHECK(c474 == 1 && goat == 2 && g475 == 3 && f.r[g475].parent == c474 && f.r[c474].parent == 0);
    npcfam::born(f, f.r[c474], 0x7100, 0xB0100474, 21, {MemberVerdict::Assign, SplitWhy::None}, false);
    npcfam::born(f, f.r[goat], 0x7200, 0xB0100476, 22, {MemberVerdict::Assign, SplitWhy::None}, false);
    CHECK(npcfam::any_born(f, s));  // otra raíz con este subárbol ya no se admitiría (ChildBornFirst)
    CHECK(npcfam::census_of(f.u[ui]) == npcfam::Census::Pending);
    npcfam::born(f, f.r[g475], 0x7300, 0xB0100475, 23, {MemberVerdict::HostKept, SplitWhy::None}, false);
    CHECK(npcfam::census_of(f.u[ui]) == npcfam::Census::Complete && f.u[ui].members == 2 && f.u[ui].host_kept == 1);
    // Un tardío partido convierte la unidad en partida y deja su motivo.
    npcfam::Rec* late = npcfam::add_rec(f, 0x9999, npcfam::Kind::Reserved, ui, static_cast<uint8_t>(goat), 2, Cls::Schedule);
    CHECK(late != nullptr);
    npcfam::born(f, *late, 0x7400, 0xB0100477, 24, {MemberVerdict::Split, SplitWhy::LateNoRoom}, true);
    CHECK(npcfam::census_of(f.u[ui]) == npcfam::Census::Split && f.u[ui].late == 1 &&
          (f.u[ui].split_mask >> static_cast<uint8_t>(SplitWhy::LateNoRoom) & 1));
    // Una raíz del anfitrión (veredicto de U-e distinto de Assign) también se registra.
    const uint8_t hi = npcfam::add_host_root(f, 0xAAAA, 0x7500, 0xB0100600, 30, Admit::Count, 10, 2);
    CHECK(npcfam::census_of(f.u[hi]) == npcfam::Census::Rejected && f.r[f.u[hi].root_rec].kind == npcfam::Kind::HostRoot);
    // El marco lleno: los registros sueltos se cuentan como perdidos, nunca se escriben fuera.
    while (f.n < npcfam::kFrameRecs) npcfam::add_rec(f, 0x1, npcfam::Kind::Orphan, npcfam::kNoIndex, npcfam::kNoRec, 0, Cls::Unknown);
    CHECK(!npcfam::frame_room(f, 1) && npcfam::add_rec(f, 0x2, npcfam::Kind::Orphan, 0xFF, npcfam::kNoRec, 0, Cls::Unknown) == nullptr &&
          f.lost == 1);
    for (uint8_t i = 0; i < static_cast<uint8_t>(npcfam::Census::Count); ++i) CHECK(npcfam::census_name(static_cast<npcfam::Census>(i))[0] != '?');
}

// probe no cambia nada: sin reservas pendientes las tablas se comportan como antes, la unión sin parent_birth es la
// de la fase A, y una unidad «se admitiría» sin reservar.
void probe_changes_nothing() {
    u2s::KeyTable<16> a, b;
    for (uint32_t i = 1; i <= 14; ++i) {
        const bool x = a.put(0xB0300000u + i, u2s::kKeyAssigned, 1);
        const bool y = b.put(0xB0300000u + i, u2s::kKeyAssigned, 1, false);
        CHECK(x == y);
    }
    CHECK(a.used() == 12 && a.overflow() == 2 && a.reserved() == 0);
    npcact::Table<64, 8> t;
    CHECK(t.reserved == 0 && t.can_reserve(56) && !t.can_reserve(57));
    Cap k{10, 64, 0}, tr{10, 64, 0};
    npcfam::AdmitIn in;
    in.scan = ScanVerdict::Ok; in.nodes = 3;
    CHECK(npcfam::reserve_unit(in, 4, 4, k, tr, true) == Admit::Admit && k.reserved == 0 && tr.reserved == 0);
    // Un miembro sin fam (probe nunca lo fija) es exactamente S35.
    npcact::Actor x;
    x.key = 1; x.birth = 1; x.assigned = true; x.queued_us = 1;
    npcact::Policy pol;
    npcact::Context ctx;
    ctx.now = 10'000'000;
    CHECK(!npcact::family_member(x) && npcact::containment_due(x, pol, ctx) == npcact::Due::Retire);
}
// Segunda revisión (15-10), R1: un hijo que ya no es nuestro no bloquea a su padre; lo que una limpieza de familia deja
// vivo nunca dispara el aborto global LeftAfterDrain; pasadas y repeticiones acotadas.
void review2_cleanup() {
    using npcact::cleanup_blocks_parent;
    CHECK(!cleanup_blocks_parent(npcact::kCleanDone, false) && !cleanup_blocks_parent(npcact::kCleanGone, false));
    CHECK(!cleanup_blocks_parent(npcact::kCleanOther, true));   // renació con el anfitrión y la misma clave
    CHECK(cleanup_blocks_parent(npcact::kCleanOther, false));   // sigue con el usuario 2, otro padre nativo
    CHECK(cleanup_blocks_parent(npcact::kCleanFailed, false) && cleanup_blocks_parent(npcact::kCleanFault, false));
    CHECK(cleanup_blocks_parent(npcact::kCleanBlocked, true));
    // El escenario de R1: guardia 0xB01003A2 (usuario 2) y su perro 0xB01003A3 renacido con el anfitrión tras el
    // drenaje. Hojas a raíz: el perro da kCleanOther (ajeno) y la raíz sí se retira.
    npcfam::RetireGuard g;
    std::vector<uint32_t> called;
    const struct { uint32_t key, parent; int result; bool not_ours; } plan[] = {
        {0xB01003A3, 0xB01003A2, npcact::kCleanOther, true}, {0xB01003A2, 0, npcact::kCleanDone, false}};
    for (const auto& p : plan) {
        if (g.is_blocked(p.key)) { g.block(p.parent); continue; }
        called.push_back(p.key);
        if (cleanup_blocks_parent(p.result, p.not_ours)) g.block(p.parent);
    }
    CHECK(called.size() == 2 && called[1] == 0xB01003A2);
    // Una clave dejada viva por una pasada de familia (kKeyCleanBlocked) cuenta aparte, nunca como left_after_drain.
    u2s::CtlStats s;
    const uint16_t fam = u2s::kKeyAssigned | u2s::kKeyFamily;
    u2s::ctl_stats_add(s, fam | u2s::kKeyCleanBlocked, 100, 0, 20'000'000, 1'000);
    CHECK(s.left_after_drain == 0 && s.drain_blocked == 1);
    u2s::CtlAbortIn in;
    in.s = s; in.drain_checked = true;
    CHECK(u2s::ctl_abort(in) == u2s::CtlAbort::None);
    u2s::ctl_stats_add(s, fam, 100, 0, 20'000'000, 1'000);  // sin la marca: la red de siempre
    in.s = s;
    CHECK(s.left_after_drain == 1 && u2s::ctl_abort(in) == u2s::CtlAbort::LeftAfterDrain);
    // Modo 0: la marca nunca se pone; la tabla la limpia al reasignar la clave (una clave reutilizada empieza de 0).
    u2s::KeyTable<16> t;
    CHECK(t.put(0xB0100001, fam, 1) && t.mark(0xB0100001, u2s::kKeyCleanBlocked, 2));
    CHECK(t.put(0xB0100001, u2s::kKeyAssigned, 3) && !(t.flags(0xB0100001) & u2s::kKeyCleanBlocked));
    // Pasadas en la misma llamada (R10): sólo si la anterior estuvo llena y progresó; como mucho kCleanupPasses.
    npcact::CleanPass p;
    p.full = true; p.done = 3;
    CHECK(npcact::cleanup_next_pass(p, 1) && npcact::cleanup_next_pass(p, npcact::kCleanupPasses - 1));
    CHECK(!npcact::cleanup_next_pass(p, npcact::kCleanupPasses));
    p.done = 0; p.gone = 0;
    CHECK(!npcact::cleanup_next_pass(p, 1));  // sin progreso: nada cambiaría
    p.done = 1; p.full = false;
    CHECK(!npcact::cleanup_next_pass(p, 1));  // ya miró todas
    p.full = true; p.fault = true;
    CHECK(!npcact::cleanup_next_pass(p, 1));
    // Repeticiones tras un drenaje: con algo bloqueado (u ocupada), como mucho kAfterDrainReruns.
    npcact::CleanPass r;
    CHECK(!npcact::cleanup_rerun(r, 0));
    r.left = 2;
    CHECK(npcact::cleanup_rerun(r, 0) && npcact::cleanup_rerun(r, npcact::kAfterDrainReruns - 1));
    CHECK(!npcact::cleanup_rerun(r, npcact::kAfterDrainReruns));
    r.left = 0; r.busy = true;
    CHECK(npcact::cleanup_rerun(r, 0));
    r.fault = true;
    CHECK(!npcact::cleanup_rerun(r, 0));
    // R10: 64 miembros profundos y su raíz: la primera tanda (llena) se los lleva; la segunda, a la raíz.
    std::vector<uint32_t> alive;
    for (uint32_t i = 0; i < 64; ++i) alive.push_back(0xB0200000u + i);
    alive.push_back(0xB0100000u);  // la raíz, profundidad 0
    std::vector<uint32_t> tried;
    uint8_t passes = 0;
    for (;;) {
        npcfam::LeafFirst<64> lf;
        for (uint32_t k : alive) {
            if (std::find(tried.begin(), tried.end(), k) != tried.end()) continue;
            lf.add(k, k == 0xB0100000u ? 0 : 1);
        }
        lf.sort();
        npcact::CleanPass q;
        q.full = lf.dropped > 0;
        for (size_t i = 0; i < lf.n; ++i) {
            tried.push_back(lf.key[i]);
            alive.erase(std::find(alive.begin(), alive.end(), lf.key[i]));
            ++q.done;
        }
        ++passes;
        if (!npcact::cleanup_next_pass(q, passes)) break;
    }
    CHECK(alive.empty() && passes == 2);
}
// R2: la pausa de la degradación no parte miembros de unidades ya admitidas; el aborto de U-e sí.
void review2_pause() {
    CHECK(npcfam::root_stopped(false, true) && npcfam::root_stopped(true, false) && !npcfam::root_stopped(false, false));
    CHECK(!npcfam::member_stopped(false, true) && npcfam::member_stopped(true, false));
    npcact::Degrade d;
    d.activation_on = true; d.degradation_on = true; d.ready = false;
    CHECK(npcact::assignments_paused(d));
    npcfam::MemberIn m;
    m.admitted = true; m.cls = Cls::Schedule; m.type = 6; m.link = Link::Present; m.link_matches = true;
    m.parent_alive = 1; m.parent_ctl = 1; m.old_ctl_known = true; m.desc_user_known = true; m.actor_user_known = true;
    m.host_field = true; m.owner_branch = true; m.key2 = 0x90100065; m.key_ok = true;
    m.stopped = npcfam::member_stopped(false, npcact::assignments_paused(d));
    CHECK(npcfam::member_verdict(m).v == MemberVerdict::Assign);  // el perro sigue a su guardia ya admitido
    CHECK(npcfam::late_verdict(1, true, m).v == MemberVerdict::Assign);
    m.stopped = npcfam::member_stopped(true, npcact::assignments_paused(d));
    const auto o = npcfam::member_verdict(m);
    CHECK(o.v == MemberVerdict::Split && o.why == SplitWhy::Stopped);
}
// R4: sólo la pertenencia de descriptor (+0x2F0 presente en su nacimiento) parte una unidad por un tardío.
void review2_late_lineage() {
    using npcfam::LateWhat;
    CHECK(npcfam::late_child(true, true, false, Link::Present) == LateWhat::Split);
    CHECK(npcfam::late_child(true, true, false, Link::Absent) == LateWhat::NotLineage);   // montura del anfitrión
    CHECK(npcfam::late_child(true, true, false, Link::Unknown) == LateWhat::NotLineage);  // sin linaje demostrado
    CHECK(npcfam::late_child(true, true, true, Link::Present) == LateWhat::Kept7);
    CHECK(npcfam::late_child(true, false, false, Link::Present) == LateWhat::SameController);
    CHECK(npcfam::late_child(false, true, false, Link::Present) == LateWhat::SameController);
    static_assert(static_cast<int>(LateWhat::NotLineage) == 4 && static_cast<int>(LateWhat::Split) == 2);
}
// R5: marcas por clave reescritas en cada nacimiento y por época.
void review2_key_marks() {
    npcfam::KeyMarks<16> k;
    CHECK(!k.contains(0xB0100475, 1));
    k.insert(0xB0100475, 1);
    CHECK(k.contains(0xB0100475, 1) && !k.contains(0xB0100475, 2));  // otra época: sin marca
    k.erase(0xB0100475);                                              // renace fuera de un marco
    CHECK(!k.contains(0xB0100475, 1));
    k.insert(0xB0100475, 1);
    // Una colisión sólo pierde la marca, nunca la da a otra clave; erase de otra clave no la toca.
    uint32_t other = 0;
    for (uint32_t c = 1; c < 100000 && !other; ++c) {
        npcfam::KeyMarks<16> probe;
        probe.insert(0xB0100475, 1);
        probe.insert(0xB0300000 + c, 1);
        if (!probe.contains(0xB0100475, 1)) other = 0xB0300000 + c;  // misma casilla
    }
    CHECK(other != 0);
    k.erase(other);
    CHECK(k.contains(0xB0100475, 1));
    k.insert(other, 1);
    CHECK(!k.contains(0xB0100475, 1) && k.contains(other, 1));
    CHECK(!k.contains(0, 0));
}
// Prueba en dos copias 2 (15-10, kit 165c4aa, probe): las 21 raíces con hijos fallaron por «envoltorio excluido»; los
// hijos (interior NPCSchedule, +0x2F0 = la raíz, dentro de su vector) eran _NPCScheduleGroupActionMember, cuyo
// constructor 0x100E70A0 sólo une horario bajo horario. Guardia 0xB0100332 + perro 0xB0100333 (tipo 6), pastor
// 0xB0100334 + 0xB0100335..337 (tipo 6), grupo 0xB0100300 + 301..303 (tipo 3).
void group_action_members() {
    using npcfam::Wrap;
    // La clase: horario sólo con interior NPCSchedule bajo un padre NPCSchedule.
    CHECK(npcfam::classify(npcfam::kVtWrapGroupActionMember, npcfam::kVtNpcSchedule, npcfam::kVtNpcSchedule) ==
          Cls::Schedule);
    CHECK(npcfam::classify(npcfam::kVtWrapGroupActionMember, npcfam::kVtNpcSchedule) == Cls::Excluded);  // padre sin leer
    CHECK(npcfam::classify(npcfam::kVtWrapGroupActionMember, npcfam::kVtNpcSchedule, npcfam::kVtInstantGimmick) ==
          Cls::Excluded);
    CHECK(npcfam::classify(npcfam::kVtWrapGroupActionMember, npcfam::kVtNpcSchedule, npcfam::kVtDescBase) ==
          Cls::Excluded);
    CHECK(npcfam::classify(npcfam::kVtWrapGroupActionMember, npcfam::kVtInstantGimmick, npcfam::kVtNpcSchedule) ==
          Cls::Excluded);
    // Los demás excluidos siguen excluidos aunque el padre sea de horario (montura, vehículo, jinete, docking,
    // captura, guardado).
    for (uint32_t w : {npcfam::kVtWrapRider, npcfam::kVtWrapLinkedVehicle, npcfam::kVtWrapDocking,
                       npcfam::kVtWrapCatchTarget, npcfam::kVtWrapSavedAttached}) {
        CHECK(npcfam::classify(w, npcfam::kVtNpcSchedule, npcfam::kVtNpcSchedule) == Cls::Excluded);
    }
    CHECK(npcfam::classify(npcfam::kVtWrapSimple, npcfam::kVtNpcSchedule, npcfam::kVtDescBase) == Cls::Schedule);
    // Guardia + perro y pastor + tres ovejas: el preexamen es Ok y la unidad se admite entera (antes: Excluded).
    Arena a;
    ArenaRead rd{&a};
    npcfam::Scan s;
    const uintptr_t guard = a.desc(npcfam::kVtNpcSchedule, 0), dog = a.desc(npcfam::kVtNpcSchedule, guard);
    a.link(guard, npcfam::kVtWrapGroupActionMember, dog);
    npcfam::scan_subtree(rd, guard, kBase, kImage, s);
    CHECK(s.v == ScanVerdict::Ok && s.n == 1 && s.schedule == 1 && s.group == 1 && s.nodes[0].cls == Cls::Schedule);
    npcfam::AdmitIn ai;
    ai.scan = s.v; ai.nodes = s.n; ai.gimmicks = s.gimmick;
    CHECK(npcfam::admit_unit(ai) == Admit::Admit);
    const uintptr_t herder = a.desc(npcfam::kVtNpcSchedule, 0);
    for (int i = 0; i < 3; ++i) a.link(herder, npcfam::kVtWrapGroupActionMember, a.desc(npcfam::kVtNpcSchedule, herder));
    npcfam::scan_subtree(rd, herder, kBase, kImage, s);
    CHECK(s.v == ScanVerdict::Ok && s.n == 3 && s.group == 3 && s.max_depth == 1);
    // El perro (tipo 6) de una unidad admitida: miembro, con el controlador de su raíz.
    npcfam::MemberIn m;
    m.admitted = true; m.cls = s.nodes[0].cls; m.type = 6; m.link = Link::Present; m.link_matches = true;
    m.parent_alive = 1; m.parent_ctl = 1; m.old_ctl_known = true; m.desc_user_known = true; m.actor_user_known = true;
    m.host_field = true; m.owner_branch = true; m.key2 = 0x90100065; m.key_ok = true;
    CHECK(npcfam::member_verdict(m).v == MemberVerdict::Assign);
    m.type = 5;  // una montura tipo 5 metida en un grupo: la unidad queda partida (se retira entera), nunca asignada
    CHECK(npcfam::member_verdict(m).v == MemberVerdict::Split);
    // Un tardío del grupo (vector llenado después del mid de la raíz): la misma clase con la vtable del padre.
    uint32_t wrap = 0, inner = 0;
    CHECK(npcfam::in_vector(rd, guard, dog, kBase, kImage, &wrap, &inner) == 1);
    CHECK(npcfam::classify(wrap, inner, npcfam::kVtNpcSchedule) == Cls::Schedule);
    // Bajo un gimmick (contrario al constructor) o con un jinete debajo: excluida, con el nodo que falló.
    {
        Arena b;
        ArenaRead br{&b};
        const uintptr_t r = b.desc(npcfam::kVtNpcSchedule, 0), g = b.desc(npcfam::kVtInstantGimmick, r);
        b.link(r, npcfam::kVtWrapSimple, g);
        b.link(g, npcfam::kVtWrapGroupActionMember, b.desc(npcfam::kVtNpcSchedule, g));
        npcfam::scan_subtree(br, r, kBase, kImage, s);
        CHECK(s.v == ScanVerdict::Excluded && s.bad_wrap == npcfam::kVtWrapGroupActionMember &&
              s.bad_inner == npcfam::kVtNpcSchedule && s.bad_parent == npcfam::kVtInstantGimmick && s.bad_depth == 2);
    }
    {
        Arena b;
        ArenaRead br{&b};
        const uintptr_t r = b.desc(npcfam::kVtNpcSchedule, 0), d = b.desc(npcfam::kVtNpcSchedule, r);
        b.link(r, npcfam::kVtWrapGroupActionMember, d);
        b.link(d, npcfam::kVtWrapRider, b.desc(npcfam::kVtNpcSchedule, d));
        npcfam::scan_subtree(br, r, kBase, kImage, s);
        CHECK(s.v == ScanVerdict::Excluded && s.bad_wrap == npcfam::kVtWrapRider && s.bad_depth == 2);
        CHECK(npcfam::wrap_of(s.bad_wrap) == Wrap::Rider);
    }
    {
        // La vtable del padre ilegible: el preexamen no se demuestra (nunca «sin hijos» ni horario).
        Arena b;
        ArenaRead br{&b};
        const uintptr_t r = b.desc(npcfam::kVtNpcSchedule, 0);
        b.link(r, npcfam::kVtWrapGroupActionMember, b.desc(npcfam::kVtNpcSchedule, r));
        b.bad[r + 2] = true;
        npcfam::scan_subtree(br, r, kBase, kImage, s);
        CHECK(s.v == ScanVerdict::Unreadable);
    }
    // Diagnóstico por clase.
    CHECK(npcfam::wrap_of(npcfam::kVtWrapGroupActionMember) == Wrap::GroupActionMember &&
          npcfam::wrap_of(npcfam::kVtWrapSimple) == Wrap::Simple && npcfam::wrap_of(0x5B04F00) == Wrap::Other);
    CHECK(npcfam::orphan_class(npcfam::kVtDescBase) == npcfam::OrphanCls::Traveller);
    CHECK(npcfam::orphan_class(npcfam::kVtNpcSchedule) == npcfam::OrphanCls::Schedule);
    CHECK(npcfam::orphan_class(npcfam::kVtInstantGimmick) == npcfam::OrphanCls::Gimmick);
    CHECK(npcfam::orphan_class(0) == npcfam::OrphanCls::Other);
    npcfam::LineCap<3> cap;
    int travellers = 0, schedule = 0;
    for (int i = 0; i < 40; ++i) travellers += cap.take(0, 6);  // los viajeros no agotan el cupo de los demás
    for (int i = 0; i < 40; ++i) schedule += cap.take(1, 16);
    CHECK(travellers == 6 && schedule == 16 && !cap.take(3, 1));
}
// Prueba en dos copias 3 (15-10, kit ce5d47d): el armado metió 157 raíces de U-e en un solo Troc (01:54:14); 18 tenían
// hijos (4 admitidas + 14 marco_lleno). Con una unidad por raíz y 64 unidades por marco se perdían; ahora una raíz sin
// hijos ocupa sólo un registro y su unidad se crea con su primer tardío.
void frame_burst() {
    static npcfam::FramePool<1> pool;
    npcfam::Frame& f = pool.at(0);
    npcfam::open_frame(f, 9, 1);
    // El diseño anterior: una unidad por raíz (también sin hijos) -> la raíz 65 ya no cabía (64 unidades).
    Tree t;
    ArenaRead rd{&t.a};
    npcfam::Scan s3;
    npcfam::scan_subtree(rd, t.root, kBase, kImage, s3);
    uintptr_t d = 0x20000000;
    for (int i = 0; i < 157; ++i, d += 0x1000) {
        CHECK(npcfam::rec_room(f, 1));
        npcfam::Rec* r = npcfam::add_childless_root(f, d, d + 0x800, 0xB0100300u + i, 100u + i);
        CHECK(r && npcfam::lazy_root(*r) && r->kind == npcfam::Kind::Root && f.nu == 0);
    }
    // Las 18 raíces con hijos del mismo Troc (3 nodos cada una) y ~113 huérfanos (viajeros incluidos) caben.
    for (int i = 0; i < 18; ++i) {
        CHECK(npcfam::frame_room(f, 1u + s3.n));
        npcfam::add_unit(f, d + i * 0x1000, 0, 0xB0100500u + i, 400u + i, s3, Admit::Admit, false, true, 0x90100065);
    }
    for (int i = 0; i < 113; ++i) {
        npcfam::Rec* o = npcfam::add_rec(f, 0x30000000 + i * 0x1000, npcfam::Kind::Orphan, npcfam::kNoIndex,
                                         npcfam::kNoRec, 0, Cls::Unknown);
        CHECK(o != nullptr);
        if (o) o->oc = npcfam::OrphanCls::Traveller;
    }
    CHECK(f.n == 157 + 18 * 4 + 113 && f.nu == 18 && f.lost == 0 && f.n <= npcfam::kFrameRecs);
    // El primer tardío de una raíz sin hijos le da su unidad: «se admitiría» en probe, admitida en 1.
    npcfam::Rec& lazy = f.r[0];
    const uint8_t ui = npcfam::promote_root(f, lazy, false, 0x90100065, 0x1234);
    CHECK(ui == 18 && lazy.unit == ui && !npcfam::lazy_root(lazy) && f.u[ui].would && !f.u[ui].admitted &&
          f.u[ui].admit == Admit::Childless && f.u[ui].key == 0xB0100300 && f.u[ui].birth == 100 &&
          f.u[ui].root_rec == 0 && f.u[ui].controller == 0x90100065 && f.u[ui].root_node == 0x1234);
    CHECK(npcfam::promote_root(f, lazy, false, 0x90100065, 0) == npcfam::kNoIndex);  // ya tiene unidad
    const uint8_t u1 = npcfam::promote_root(f, f.r[1], true, 0x90100065, 0);
    CHECK(u1 != npcfam::kNoIndex && f.u[u1].admitted && !f.u[u1].would);
    // Su clave no se pudo registrar (KeysFull): su unidad, si llega, es del anfitrión.
    f.r[2].kind = npcfam::Kind::HostRoot;
    const uint8_t uh = npcfam::promote_root(f, f.r[2], true, 0x90100065, 0);
    CHECK(uh != npcfam::kNoIndex && !f.u[uh].admitted && !f.u[uh].would && f.u[uh].controller == 0 &&
          npcfam::census_of(f.u[uh]) == npcfam::Census::Rejected);
    // Un huérfano o un miembro nunca son raíz perezosa.
    CHECK(!npcfam::lazy_root(f.r[f.n - 1]) && !npcfam::lazy_root(f.r[157 + 1]));
    // Sin unidad libre: kNoIndex (el llamador parte la unidad en modo 1); un tardío sólo necesita registro.
    while (f.nu < npcfam::kFrameUnits) npcfam::add_host_root(f, 0x1, 0, 0, 0, Admit::Count, 0, 1);
    CHECK(!npcfam::frame_room(f, 1) && npcfam::rec_room(f, 1));
    CHECK(npcfam::promote_root(f, f.r[3], true, 0x90100065, 0) == npcfam::kNoIndex && npcfam::lazy_root(f.r[3]));
    // Lleno de registros: la raíz sin hijos no se registra (contada), nunca se escribe fuera.
    while (npcfam::rec_room(f, 1)) npcfam::add_rec(f, 0x1, npcfam::Kind::Orphan, npcfam::kNoIndex, npcfam::kNoRec, 0, Cls::Unknown);
    CHECK(f.n == npcfam::kFrameRecs && npcfam::add_childless_root(f, 0x5, 0, 1, 1) == nullptr);
    static_assert(npcfam::kFrameRecs < npcfam::kNoRec && npcfam::kFrameUnits < npcfam::kNoIndex);
}
}  // namespace

int main() {
    types_and_reads();
    scan_trees();
    reservation_63_plus_2();
    nested_frames();
    parent_generation();
    leaf_to_root_orders();
    split_and_members();
    member_matrix();
    census_and_frame();
    probe_changes_nothing();
    review2_cleanup();
    review2_pause();
    review2_late_lineage();
    review2_key_marks();
    group_action_members();
    frame_burst();
    std::printf("S36 familias NPC: %d fallos\n", failures);
    return failures ? 1 : 0;
}
