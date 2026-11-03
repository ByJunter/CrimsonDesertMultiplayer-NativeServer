// S38 fase C (docs/research/s25/s38-fase-c.md): pruebas puras de user2_road_control (user2_road_logic.h). No carga el
// mod ni el juego: la memoria del juego es una arena sintética leída por la misma política `read` del mod, con las
// estructuras de la build 1.0.0.2976 (descriptor +0x2F0 padre, +0x2F8 vector {data, u32 count}, envoltorio {vtable,
// inner}; procedencia +0x3B8 / +0x3C8 / +0x3CC / +0x3CD). Cubre la tabla de verdad de la raíz, los productores
// positivos y negativos (0x27EA160 en su Troc 2550, 0x28981B0, ActivateSpawningPool), la caravana entera, los miembros
// y tardíos, el registro antes de autoridad, la retirada estricta de hojas a raíz y su continuación sin Troc del
// invitado, las paradas y la exclusión con U-e.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "mod/session2/npc_activation_logic.h"
#include "mod/session2/npc_probe_client_logic.h"
#include "mod/session2/user2_road_logic.h"
#include "mod/session2/user2_stages_logic.h"

using namespace cdmp::session2;
using road::Call;
using road::Clean;
using road::MemberV;
using road::ScanV;
using road::Split;
using road::UState;
using road::Verdict;
using road::Why;

namespace {
int failures = 0;
#define CHECK(x)                                                         \
    do {                                                                 \
        if (!(x)) {                                                      \
            std::printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #x);    \
            ++failures;                                                  \
        }                                                                \
    } while (0)

constexpr uintptr_t kBase = 0x140000000ull;
constexpr uint64_t kImage = 0x8000000;
constexpr uint32_t kU2Char = 0xA01001D5, kHostChar = 0xA0100001, kKey2 = 0x901001D4, kHostUser = 0x90100000;
constexpr uintptr_t kU2Ptr = 0x7FF612340000ull;
constexpr uintptr_t kRootDesc = 0x0F000000;

struct Arena {
    std::map<uintptr_t, uint8_t> bytes;
    std::map<uintptr_t, bool> bad;
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
    // Un descriptor 0x4E8 como 0x278EA70 (vtable, campos a 0) con la procedencia que escribe su productor.
    uintptr_t desc(uint32_t vt, uintptr_t parent = 0) {
        const uintptr_t d = alloc(0x500);
        put<uintptr_t>(d, kBase + vt);
        put<uintptr_t>(d + road::kDesc_Parent, parent);
        return d;
    }
    // 0x27EA160: raíz (+0x3CC = primero del lote, +0x3CD = 1) o acompañante (word 0x100); 0x28981B0: word 0x101.
    void mark(uintptr_t d, uint32_t req_key, uintptr_t req_ptr, uint16_t word = 0x101, uint32_t road_id = 0x3FD) {
        put<uint16_t>(d + road::kDesc_First, word);
        put<uint32_t>(d + road::kDesc_ReqKey, req_key);
        put<uintptr_t>(d + road::kDesc_ReqActor, req_ptr);
        put<uint32_t>(d + road::kDesc_Road, road_id);
    }
    // push_back(parent+0x2F8, envoltorio{wrap, child}) como 0x43DB70; [child+0x2F0] = parent.
    uintptr_t link(uintptr_t parent, uint32_t wrap_rva, uintptr_t child, bool set_back = true) {
        uintptr_t data = 0;
        uint32_t count = 0;
        read(parent + road::kDesc_Children, &data, 8);
        read(parent + road::kDesc_ChildCount, &count, 4);
        if (!data) {
            data = alloc(64 * 8);
            put<uintptr_t>(parent + road::kDesc_Children, data);
        }
        const uintptr_t w = alloc(0x18);
        put<uintptr_t>(w, kBase + wrap_rva);
        put<uintptr_t>(w + road::kWrap_Inner, child);
        put<uintptr_t>(data + count * 8ull, w);
        put<uint32_t>(parent + road::kDesc_ChildCount, count + 1);
        if (set_back) put<uintptr_t>(child + road::kDesc_Parent, parent);
        return w;
    }
    // Un acompañante de carretera del invitado colgado como _Rider (0x100E63B0).
    uintptr_t rider(uintptr_t parent, uint32_t req_key = kU2Char, uintptr_t req_ptr = kU2Ptr) {
        const uintptr_t d = desc(road::kVtDescBase);
        mark(d, req_key, req_ptr, 0x100);
        link(parent, road::kVtWrapRider, d);
        return d;
    }
};
struct ArenaRead {
    const Arena* a;
    bool operator()(uintptr_t at, void* out, size_t n) const { return a->read(at, out, n); }
};

road::RootIn good_root(const road::Prov& p) {
    road::RootIn r;
    r.prov = p;
    r.host_char_key = kHostChar;
    r.u2_char_key = kU2Char;
    r.u2_char = kU2Ptr;
    r.in_road_troc = true;
    r.in_request = 1;
    r.old_ctl_known = true;
    r.desc_user_known = true;
    r.actor_user_known = true;
    r.host_field = true;
    r.eax = kHostUser;
    r.field_owner = kHostUser;
    r.type = 3;
    r.key2 = kKey2;
    r.session_key2 = kKey2;
    return r;
}
road::Prov prov_of(uint32_t req = kU2Char, uintptr_t ptr = kU2Ptr) {
    road::Prov p;
    p.ok = true; p.vt = road::kVtDescBase; p.first = 1; p.mark = 1; p.req_key = req; p.req_ptr = ptr; p.req_ptr_ok = true;
    return p;
}

// ---------------------------------------------------------------- 1. la raíz: tabla de verdad
void root_truth_table() {
    CHECK(road::root_verdict(good_root(prov_of())) == Verdict::Assign);
    for (const uint8_t t : std::initializer_list<uint8_t>{3, 5, 6}) {
        auto r = good_root(prov_of());
        r.type = t;
        CHECK(road::root_verdict(r) == Verdict::Assign);
    }
    for (const uint8_t t : std::initializer_list<uint8_t>{0, 1, 2, 4, 7, 8}) {
        auto r = good_root(prov_of());
        r.type = t;
        CHECK(road::root_verdict(r) == Verdict::Type);  // tipos 4 / 7 (gimmicks) nunca
    }
    auto r = good_root(prov_of());
    r.prov.vt = road::kVtDescBase + 0x10;
    CHECK(road::root_verdict(r) == Verdict::NotMarked);
    r = good_root(prov_of()); r.prov.mark = 0; CHECK(road::root_verdict(r) == Verdict::NotMarked);
    r = good_root(prov_of()); r.prov.ok = false; CHECK(road::root_verdict(r) == Verdict::NotMarked);
    r = good_root(prov_of(0xB0100001)); CHECK(road::root_verdict(r) == Verdict::NotMarked);  // solicitante no jugador
    // Los viajeros del ANFITRIÓN nunca, aunque todo lo demás se cumpla (y aunque estén en un Troc 2550).
    r = good_root(prov_of(kHostChar)); CHECK(road::root_verdict(r) == Verdict::HostRequester);
    r = good_root(prov_of(0xA0100777)); CHECK(road::root_verdict(r) == Verdict::OtherRequester);
    r = good_root(prov_of()); r.u2_char_key = 0; CHECK(road::root_verdict(r) == Verdict::OtherRequester);
    r = good_root(prov_of(kU2Char, kU2Ptr + 8)); CHECK(road::root_verdict(r) == Verdict::PtrMismatch);
    r = good_root(prov_of()); r.prov.req_ptr_ok = false; CHECK(road::root_verdict(r) == Verdict::PtrMismatch);
    r = good_root(prov_of()); r.u2_char = 0; CHECK(road::root_verdict(r) == Verdict::PtrMismatch);
    // Fuera de su Troc 2550: 0x28981B0 (reconstrucción), ActivateSpawningPool, creación diferida.
    r = good_root(prov_of()); r.in_road_troc = false; CHECK(road::root_verdict(r) == Verdict::NotRoadTroc);
    r = good_root(prov_of()); r.stopped = true; CHECK(road::root_verdict(r) == Verdict::Stopped);
    // Revisión §2: procedencia POSITIVA: su clase (+0x3D0) en la 2550 en curso; desconocida tampoco vale.
    r = good_root(prov_of()); r.in_request = 0; CHECK(road::root_verdict(r) == Verdict::NotInRequest);
    r = good_root(prov_of()); r.in_request = -1; CHECK(road::root_verdict(r) == Verdict::NotInRequest);
    // Revisión §3.2: clase en bucle de partidas; carros fuera del 1.
    r = good_root(prov_of()); r.road_blocked = true; CHECK(road::root_verdict(r) == Verdict::SplitLoop);
    r = good_root(prov_of()); r.type = 5; r.carts_allowed = false; CHECK(road::root_verdict(r) == Verdict::CartRoot);
    r = good_root(prov_of()); r.type = 3; r.carts_allowed = false; CHECK(road::root_verdict(r) == Verdict::Assign);
    r = good_root(prov_of()); r.old_ctl = kHostUser; CHECK(road::root_verdict(r) == Verdict::HasController);
    r = good_root(prov_of()); r.old_ctl_known = false; CHECK(road::root_verdict(r) == Verdict::HasController);
    r = good_root(prov_of()); r.desc_user = 0x1234; CHECK(road::root_verdict(r) == Verdict::DescUser);
    r = good_root(prov_of()); r.desc_user_known = false; CHECK(road::root_verdict(r) == Verdict::DescUser);
    // Montura / mascota / compañero de alguien (actor+0xA0): nunca, aunque sea tipo 5.
    r = good_root(prov_of()); r.type = 5; r.actor_user = 0x5678; CHECK(road::root_verdict(r) == Verdict::UserChild);
    r = good_root(prov_of()); r.actor_user_known = false; CHECK(road::root_verdict(r) == Verdict::UserChild);
    r = good_root(prov_of()); r.host_field = false; CHECK(road::root_verdict(r) == Verdict::NotHostField);
    r = good_root(prov_of()); r.eax = kKey2; CHECK(road::root_verdict(r) == Verdict::NotOwnerBranch);
    r = good_root(prov_of()); r.eax = 0; r.field_owner = 0; CHECK(road::root_verdict(r) == Verdict::NotOwnerBranch);
    r = good_root(prov_of()); r.key2 = 0; CHECK(road::root_verdict(r) == Verdict::NoKey);
    // Auditoría: una sesión desconocida nunca cuenta como coincidencia.
    r = good_root(prov_of()); r.session_key2 = 0; CHECK(road::root_verdict(r) == Verdict::SessionUnknown);
    r = good_root(prov_of()); r.session_key2 = kKey2 + 1; CHECK(road::root_verdict(r) == Verdict::KeyMismatch);
    for (size_t v = 0; v < static_cast<size_t>(Verdict::Count); ++v) {
        CHECK(std::strcmp(road::verdict_name(static_cast<Verdict>(v)), "?") != 0);
    }
    CHECK(road::cohort_type(3) && road::cohort_type(5) && road::cohort_type(6) && !road::cohort_type(7));
    CHECK(road::class_of(3) == 0 && road::class_of(5) == 1 && road::class_of(6) == 2 && road::class_of(7) == 3);
}

// ---------------------------------------------------------------- 2-3. lector de procedencia y productores
void provenance_reader() {
    Arena a;
    ArenaRead rd{&a};
    // 0x27EA160, raíz primera del lote.
    const uintptr_t d = a.desc(road::kVtDescBase);
    a.mark(d, kU2Char, kU2Ptr, 0x101);
    road::Prov p = road::read_prov(rd, d, kBase, kImage);
    CHECK(p.ok && p.vt == road::kVtDescBase && p.first == 1 && p.mark == 1 && p.req_key == kU2Char &&
          p.req_ptr_ok && p.req_ptr == kU2Ptr && road::marked(p));
    // Acompañante (word 0x100): +0x3CD = 1.
    const uintptr_t c = a.desc(road::kVtDescBase);
    a.mark(c, kU2Char, kU2Ptr, 0x100);
    p = road::read_prov(rd, c, kBase, kImage);
    CHECK(p.first == 0 && p.mark == 1 && road::marked(p));
    // Recién construido (0x278EA70): todo a 0, sin marca.
    const uintptr_t fresh = a.desc(road::kVtDescBase);
    CHECK(!road::marked(road::read_prov(rd, fresh, kBase, kImage)));
    // 0x28981B0 (contraejemplo de la auditoría): MISMA vtable y marca (word 0x101), mismo solicitante posible: el lector
    // la acepta como marcada; sólo el Troc 2550 la deja fuera.
    const uintptr_t restored = a.desc(road::kVtDescBase);
    a.mark(restored, kU2Char, kU2Ptr, 0x101);
    a.put<uint64_t>(restored + road::kDesc_Saved0, 0x1234);
    p = road::read_prov(rd, restored, kBase, kImage);
    CHECK(road::marked(p));
    auto in = good_root(p);
    in.in_road_troc = false;
    CHECK(road::root_verdict(in) == Verdict::NotRoadTroc);
    // NPCSchedule (U-e) nunca es de carretera; otra vtable tampoco; lecturas fallidas -> sin marca.
    const uintptr_t sched = a.desc(0x5B04CB0);
    a.mark(sched, kU2Char, kU2Ptr, 0x101);
    CHECK(!road::marked(road::read_prov(rd, sched, kBase, kImage)));
    a.bad[d + road::kDesc_ReqKey] = true;
    CHECK(!road::read_prov(rd, d, kBase, kImage).ok);
    CHECK(!road::read_prov(rd, 0x100, kBase, kImage).ok);
    // Fuera de la imagen: vtable 0.
    const uintptr_t foreign = a.alloc(0x500);
    a.put<uintptr_t>(foreign, 0x7FF600000000ull);
    CHECK(road::read_prov(rd, foreign, kBase, kImage).vt == 0);
    // Exclusión con U-e: un descriptor de carretera marcado nunca da Ctl::Assign (OtherDesc) y uno de horario nunca
    // da road Assign (NotMarked).
    u2s::CtlIn ci;
    ci.opcode = 2043; ci.own_mgr_troc = true; ci.armed = true; ci.desc_npc_schedule = false; ci.type = 3;
    ci.eax = kHostUser; ci.field_owner = kHostUser; ci.host_field = true; ci.key2 = kKey2;
    CHECK(u2s::ctl_verdict(ci) == u2s::Ctl::OtherDesc);
    road::Prov ps;
    ps.ok = true; ps.vt = 0x5B04CB0; ps.mark = 1; ps.req_key = kU2Char;
    CHECK(road::root_verdict(good_root(ps)) == Verdict::NotMarked);
    // El S33 de U-e en el Troc 2550 sale NotTimer: nunca asigna por U-e.
    ci.opcode = road::kOpAutoSpawnRoad;
    CHECK(u2s::ctl_verdict(ci) == u2s::Ctl::NotTimer);
}

// ---------------------------------------------------------------- 4-5. la caravana entera (preexamen)
void caravan_scan() {
    Arena a;
    ArenaRead rd{&a};
    // Carro (raíz) + conductor + caballo + 2 cabras colgadas del conductor (profundidad 2).
    const uintptr_t wagon = a.desc(road::kVtDescBase);
    a.mark(wagon, kU2Char, kU2Ptr, 0x101);
    const uintptr_t driver = a.rider(wagon);
    const uintptr_t horse = a.rider(wagon);
    const uintptr_t goat1 = a.rider(driver);
    const uintptr_t goat2 = a.rider(driver);
    road::Scan s;
    road::scan_unit(rd, wagon, kBase, kImage, kU2Char, kU2Ptr, s);
    CHECK(s.v == ScanV::Ok && s.n == 4 && s.max_depth == 2);
    CHECK(s.nodes[0].desc == driver && s.nodes[0].parent == road::kNoIndex && s.nodes[0].depth == 1);
    CHECK(s.nodes[1].desc == horse);
    CHECK(s.nodes[2].desc == goat1 && s.nodes[2].parent == 0 && s.nodes[2].depth == 2);
    CHECK(s.nodes[3].desc == goat2);
    bool rider = false;
    CHECK(road::in_vector(rd, driver, goat2, kBase, kImage, &rider) == 1 && rider);
    CHECK(road::in_vector(rd, wagon, goat2, kBase, kImage, &rider) == 0);
    // Fase C probe (15-10): una bandada / caminante con acompañantes _Simple (envoltorio=exe+0x5B04E80): una unidad.
    {
        Arena f;
        ArenaRead rf{&f};
        const uintptr_t bird = f.desc(road::kVtDescBase);
        f.mark(bird, kU2Char, kU2Ptr, 0x101, 0x1F5);
        for (int i = 0; i < 4; ++i) {
            const uintptr_t c = f.desc(road::kVtDescBase);
            f.mark(c, kU2Char, kU2Ptr, 0x100, 0x1F5);
            f.link(bird, road::kVtWrapSimple, c);
        }
        road::scan_unit(rf, bird, kBase, kImage, kU2Char, kU2Ptr, s);
        CHECK(s.v == ScanV::Ok && s.n == 4 && s.gimmicks == 0);
        CHECK(road::in_vector(rf, bird, s.nodes[3].desc, kBase, kImage, &rider) == 1 && rider);
        const uintptr_t other = f.desc(road::kVtDescBase);
        f.mark(other, kHostChar, 0x7FF600001000ull, 0x100);
        f.link(bird, road::kVtWrapSimple, other);  // _Simple de OTRO solicitante: fuera
        road::scan_unit(rf, bird, kBase, kImage, kU2Char, kU2Ptr, s);
        CHECK(s.v == ScanV::Unmarked);
    }
    // Sin hijos: unidad de 1.
    const uintptr_t lone = a.desc(road::kVtDescBase);
    a.mark(lone, kU2Char, kU2Ptr);
    road::scan_unit(rd, lone, kBase, kImage, kU2Char, kU2Ptr, s);
    CHECK(s.v == ScanV::Ok && s.n == 0);
    // Un «Game Mechanic» (InstantGimmick) en el vector: nodo host-kept (fase B §2.7); su carga no se recorre ni cuenta.
    {
        Arena b = a;
        ArenaRead rb{&b};
        const uintptr_t gm = b.desc(0x5B04C68);
        b.link(wagon, 0x5B04DA0, gm);  // _AutoSpawnGimmick
        for (int i = 0; i < 19; ++i) b.link(gm, road::kVtWrapSimple, b.desc(0x5B04C68));  // la carga del carro
        road::scan_unit(rb, wagon, kBase, kImage, kU2Char, kU2Ptr, s);
        CHECK(s.v == ScanV::Ok && s.n == 5 && s.gimmicks == 1 && s.nodes[2].gimmick && !s.nodes[0].gimmick);
        Arena bs = a;
        ArenaRead rbs{&bs};
        bs.link(horse, road::kVtWrapSimple, bs.desc(0x5B04C68));  // InstantGimmick bajo _Simple
        road::scan_unit(rbs, wagon, kBase, kImage, kU2Char, kU2Ptr, s);
        CHECK(s.v == ScanV::Ok && s.gimmicks == 1 && s.nodes[s.n - 1].gimmick && s.nodes[s.n - 1].depth == 2);
        // Otro envoltorio (_Docking): fuera, entera.
        Arena bd = a;
        ArenaRead rbd{&bd};
        const uintptr_t dk = bd.desc(road::kVtDescBase);
        bd.mark(dk, kU2Char, kU2Ptr, 0x100);
        bd.link(wagon, 0x5B04DE0, dk);
        road::scan_unit(rbd, wagon, kBase, kImage, kU2Char, kU2Ptr, s);
        CHECK(s.v == ScanV::NotRider && s.bad_wrap == 0x5B04DE0);
        Arena c2 = a;
        ArenaRead rc{&c2};
        const uintptr_t gm2 = c2.desc(0x5B04C68);
        c2.link(wagon, road::kVtWrapRider, gm2);
        road::scan_unit(rc, wagon, kBase, kImage, kU2Char, kU2Ptr, s);
        CHECK(s.v == ScanV::NotTraveller && s.bad_inner == 0x5B04C68);
    }
    // Un acompañante de OTRO solicitante (o sin marca): fuera.
    {
        Arena b = a;
        ArenaRead rb{&b};
        b.rider(horse, kHostChar, 0x7FF600001000ull);
        road::scan_unit(rb, wagon, kBase, kImage, kU2Char, kU2Ptr, s);
        CHECK(s.v == ScanV::Unmarked);
        Arena c2 = a;
        ArenaRead rc{&c2};
        const uintptr_t unmarked = c2.desc(road::kVtDescBase);
        c2.link(horse, road::kVtWrapRider, unmarked);
        road::scan_unit(rc, wagon, kBase, kImage, kU2Char, kU2Ptr, s);
        CHECK(s.v == ScanV::Unmarked);
    }
    // Vínculo incoherente, usuario explícito, ciclo, ilegible, demasiados, demasiado profundo.
    {
        Arena b = a;
        ArenaRead rb{&b};
        b.put<uintptr_t>(goat1 + road::kDesc_Parent, horse);
        road::scan_unit(rb, wagon, kBase, kImage, kU2Char, kU2Ptr, s);
        CHECK(s.v == ScanV::Inconsistent);
        Arena c2 = a;
        ArenaRead rc{&c2};
        c2.put<uint64_t>(goat1 + road::kDesc_User, 0x99);
        road::scan_unit(rc, wagon, kBase, kImage, kU2Char, kU2Ptr, s);
        CHECK(s.v == ScanV::DescUser);
        Arena d2 = a;
        ArenaRead rdd{&d2};
        d2.link(goat2, road::kVtWrapRider, driver, false);
        d2.put<uintptr_t>(driver + road::kDesc_Parent, goat2);
        road::scan_unit(rdd, wagon, kBase, kImage, kU2Char, kU2Ptr, s);
        CHECK(s.v == ScanV::Inconsistent || s.v == ScanV::Cycle);
        Arena e2 = a;
        ArenaRead re{&e2};
        e2.bad[horse + road::kDesc_ChildCount] = true;
        road::scan_unit(re, wagon, kBase, kImage, kU2Char, kU2Ptr, s);
        CHECK(s.v == ScanV::Unreadable);
    }
    {
        Arena b;
        ArenaRead rb{&b};
        const uintptr_t root = b.desc(road::kVtDescBase);
        b.mark(root, kU2Char, kU2Ptr);
        for (int i = 0; i < 16; ++i) b.rider(root);
        road::scan_unit(rb, root, kBase, kImage, kU2Char, kU2Ptr, s);
        CHECK(s.v == ScanV::TooBig);
        Arena c2;
        ArenaRead rc{&c2};
        const uintptr_t r2 = c2.desc(road::kVtDescBase);
        c2.mark(r2, kU2Char, kU2Ptr);
        for (int i = 0; i < 17; ++i) {
            const uintptr_t x = c2.desc(road::kVtDescBase);
            c2.mark(x, kU2Char, kU2Ptr, 0x100);
            c2.link(r2, road::kVtWrapRider, x);
        }
        road::scan_unit(rc, r2, kBase, kImage, kU2Char, kU2Ptr, s);
        CHECK(s.v == ScanV::TooBig);  // 17 hijos directos > kMaxVector
        Arena d2;
        ArenaRead rdd{&d2};
        uintptr_t at = d2.desc(road::kVtDescBase);
        const uintptr_t r3 = at;
        d2.mark(r3, kU2Char, kU2Ptr);
        for (int i = 0; i < 5; ++i) at = d2.rider(at);
        road::scan_unit(rdd, r3, kBase, kImage, kU2Char, kU2Ptr, s);
        CHECK(s.v == ScanV::TooDeep);
    }
    for (size_t v = 0; v < static_cast<size_t>(ScanV::Count); ++v) {
        CHECK(std::strcmp(road::scan_name(static_cast<ScanV>(v)), "?") != 0);
    }
}

// ---------------------------------------------------------------- 6. miembros y tardíos
road::MemberIn good_member() {
    road::MemberIn m;
    m.unit_live = true; m.reserved = true; m.link_present = true; m.link_matches = true; m.traveller = true;
    m.parent_alive = 1; m.parent_ctl = 1; m.old_ctl_known = true; m.desc_user_known = true; m.actor_user_known = true;
    m.host_field = true; m.owner_branch = true; m.type = 6; m.key2 = kKey2; m.unit_controller = kKey2;
    return m;
}
void member_matrix() {
    CHECK(road::member_verdict(good_member()).v == MemberV::Assign);
    auto m = good_member(); m.unit_live = false; CHECK(road::member_verdict(m).v == MemberV::HostUnit);
    const auto split = [](const road::MemberIn& x, Split w) {
        const auto o = road::member_verdict(x);
        return o.v == MemberV::Split && o.why == w;
    };
    m = good_member(); m.link_present = false; CHECK(split(m, Split::LinkUnknown));
    m = good_member(); m.link_matches = false; CHECK(split(m, Split::LinkChanged));
    m = good_member(); m.traveller = false; CHECK(split(m, Split::NotTraveller));
    m = good_member(); m.parent_alive = 0; CHECK(split(m, Split::ParentGone));
    m = good_member(); m.parent_alive = -1; CHECK(split(m, Split::ParentGone));
    m = good_member(); m.parent_ctl = 0; CHECK(split(m, Split::ParentController));
    m = good_member(); m.old_ctl = 1; CHECK(split(m, Split::HasController));
    m = good_member(); m.desc_user = 1; CHECK(split(m, Split::DescUser));
    m = good_member(); m.actor_user = 1; CHECK(split(m, Split::UserChild));
    m = good_member(); m.host_field = false; CHECK(split(m, Split::NotHostField));
    m = good_member(); m.owner_branch = false; CHECK(split(m, Split::NotOwnerBranch));
    m = good_member(); m.type = 7; CHECK(split(m, Split::Type));
    m = good_member(); m.type = 4; CHECK(split(m, Split::Type));
    m = good_member(); m.type = 5; CHECK(road::member_verdict(m).v == MemberV::Assign);  // carro / caballo de la caravana
    m = good_member(); m.key2 = 0; CHECK(split(m, Split::NoKey));
    m = good_member(); m.key2 = kKey2 + 1; CHECK(split(m, Split::NoKey));
    // Tardíos: pertenencia demostrada AHORA, envoltorio _Rider y sitio; si no, la unidad queda partida.
    m = good_member(); m.reserved = false; m.in_vector = 1; m.rider = true; m.room = true;
    CHECK(road::member_verdict(m).v == MemberV::Assign);
    auto l = m; l.in_vector = 0; CHECK(split(l, Split::LateNotInVector));
    l = m; l.in_vector = -1; CHECK(split(l, Split::LinkUnknown));
    l = m; l.rider = false; CHECK(split(l, Split::NotTraveller));
    l = m; l.room = false; CHECK(split(l, Split::LateNoRoom));
    for (size_t v = 0; v < static_cast<size_t>(Split::Count); ++v) {
        CHECK(std::strcmp(road::split_name(static_cast<Split>(v)), "?") != 0);
    }
    // Un hijo que no es de carretera: un gimmick tipo 7 se queda con el anfitrión; otro tipo u otro descriptor la parte.
    CHECK(road::gimmick_verdict(true, 7).v == MemberV::HostKept);
    CHECK(road::gimmick_verdict(true, 3).v == MemberV::Split && road::gimmick_verdict(true, 3).why == Split::Type);
    CHECK(road::gimmick_verdict(false, 7).v == MemberV::Split &&
          road::gimmick_verdict(false, 7).why == Split::NotTraveller);
}

// ---------------------------------------------------------------- 7. la tabla: registro antes de autoridad, ciclo
using SmallTable = road::Table<8, 64>;
road::Scan scan_of(std::initializer_list<std::pair<uintptr_t, uint8_t>> nodes) {  // (desc, parent index o kNoIndex)
    road::Scan s;
    s.n = 0;
    for (const auto& [d, p] : nodes) {
        s.nodes[s.n].desc = d;
        s.nodes[s.n].parent = p;
        s.nodes[s.n].depth = static_cast<uint8_t>(p == road::kNoIndex ? 1 : s.nodes[p].depth + 1);
        s.nodes[s.n].gimmick = false;
        ++s.n;
    }
    return s;
}
void table_lifecycle() {
    auto t = std::make_unique<SmallTable>();
    const uint64_t now = 1'000'000;
    // Raíz + 3 (conductor, caballo, cabra del conductor).
    const road::Scan s = scan_of({{0x1000, road::kNoIndex}, {0x2000, road::kNoIndex}, {0x3000, uint8_t{0}}});
    const int ui = t->open(kRootDesc, 0xB0100100, 11, 3, kU2Char, kU2Ptr, kKey2, true, 77, 1, now, s);
    CHECK(ui >= 0 && t->live == 1 && t->u[ui].state == UState::Open && t->u[ui].n == 4 && t->u[ui].nodes == 3);
    CHECK(t->find(0xB0100100).unit == ui && t->find(0xB0100100).rec == 0);
    // Fase C probe (15-10): sus hijos directos la encuentran por SU descriptor (antes nadie: 52 «sin unidad»).
    CHECK(t->find_desc(static_cast<size_t>(ui), kRootDesc) == 0);
    CHECK(t->born(static_cast<size_t>(ui), 0, 0xB0100100, 11, 5, true, now));
    CHECK(t->find_desc(static_cast<size_t>(ui), 0x3000) == 3 && t->u[ui].r[3].parent == 1 && t->u[ui].r[3].depth == 2);
    CHECK(t->born(static_cast<size_t>(ui), 1, 0xB0100101, 12, 3, true, now));
    CHECK(t->born(static_cast<size_t>(ui), 2, 0xB0100102, 13, 5, true, now));
    CHECK(t->born(static_cast<size_t>(ui), 3, 0xB0100103, 14, 6, true, now));
    CHECK(t->find(0xB0100103).rec == 3);
    // Un tardío del conductor.
    const int late = t->add_late(static_cast<size_t>(ui), 0x4000, 1);
    CHECK(late == 4 && t->u[ui].r[4].late && t->u[ui].r[4].depth == 2);
    CHECK(t->born(static_cast<size_t>(ui), 4, 0xB0100104, 15, 6, true, now));
    // Cierre: punteros olvidados; completa -> viva.
    CHECK(t->close(static_cast<size_t>(ui), now) == 0);
    CHECK(t->u[ui].state == UState::Live && t->find_desc(static_cast<size_t>(ui), 0x3000) == -1 &&
          t->u[ui].r[3].desc == 0 && t->u[ui].r[3].actor == 0 && t->u[ui].req_ptr == 0);
    // Su carga: la unidad asignada pasa a retirarse; nada más se toca.
    CHECK(t->request(Why::ItsLoad, now + 10) == 1);
    CHECK(t->u[ui].state == UState::Retire && t->u[ui].why == Why::ItsLoad && t->pending() && t->next_retire() == ui);
    // Una baja nativa durante la retirada cuenta como fuera.
    CHECK(t->on_logout(0xB0100103, 25));
    CHECK(t->u[ui].r[3].removed && t->u[ui].r[3].clean == Clean::Gone && t->u[ui].r[3].removed_reason == 25);
    CHECK(t->settle(static_cast<size_t>(ui), now) == UState::Retire);
    for (const uint8_t k : std::initializer_list<uint8_t>{0, 1, 2, 4}) t->u[ui].r[k].clean = Clean::Gone;
    CHECK(t->settle(static_cast<size_t>(ui), now + 20) == UState::Done && !t->pending());
    CHECK(t->prune(3, now + 30) == 1 && t->live == 0 && !t->find(0xB0100100));

    // probe (no asignada): nunca se retira, ni en la carga ni partida.
    const int up = t->open(kRootDesc, 0xB0100200, 21, 3, kU2Char, kU2Ptr, kKey2, false, 78, 1, now, scan_of({{0x5000, road::kNoIndex}}));
    CHECK(up >= 0);
    t->born(static_cast<size_t>(up), 0, 0xB0100200, 21, 3, false, now);
    t->born(static_cast<size_t>(up), 1, 0xB0100201, 22, 6, false, now);
    t->mark_split(static_cast<size_t>(up), Split::Type, now);
    t->close(static_cast<size_t>(up), now);
    CHECK(t->u[up].state == UState::Live && t->u[up].split);
    CHECK(t->request(Why::ItsLogout, now + 1) == 0 && !t->pending());
    // probe: borrada con otra época o pasados 10 min; con vivos y la misma época, no.
    CHECK(t->prune(3, now + 1) == 0);
    CHECK(t->prune(4, now + 2) == 1);

    // Modo 1: un miembro del preexamen que no nació en su Troc parte la unidad (nunca «ya vendrá») y se retira.
    const int u2 = t->open(kRootDesc, 0xB0100300, 31, 3, kU2Char, kU2Ptr, kKey2, true, 79, 1, now,
                           scan_of({{0x6000, road::kNoIndex}, {0x7000, road::kNoIndex}}));
    t->born(static_cast<size_t>(u2), 0, 0xB0100300, 31, 3, true, now);
    t->born(static_cast<size_t>(u2), 1, 0xB0100301, 32, 6, true, now);
    CHECK(t->close(static_cast<size_t>(u2), now) == 1);
    CHECK(t->u[u2].split && t->u[u2].split_why == Split::NotBorn && t->u[u2].state == UState::Retire &&
          t->u[u2].why == Why::Split);
    // Un gimmick del preexamen (host-kept) que no nace en el Troc no parte la unidad (puede nacer después).
    road::Scan sg = scan_of({{0x7100, road::kNoIndex}, {0x7200, road::kNoIndex}});
    sg.nodes[1].gimmick = true;
    const int ug = t->open(kRootDesc, 0xB0100350, 35, 3, kU2Char, kU2Ptr, kKey2, true, 79, 1, now, sg);
    t->born(static_cast<size_t>(ug), 0, 0xB0100350, 35, 3, true, now);
    t->born(static_cast<size_t>(ug), 1, 0xB0100351, 36, 6, true, now);
    CHECK(t->u[ug].r[2].host_kept && t->close(static_cast<size_t>(ug), now) == 0 && !t->u[ug].split &&
          t->u[ug].state == UState::Live);
    t->free_unit(static_cast<size_t>(ug));
    // Una retirada pedida mientras la unidad estaba abierta se ejecuta al cerrarse.
    const int u3 = t->open(kRootDesc, 0xB0100400, 41, 3, kU2Char, kU2Ptr, kKey2, true, 80, 1, now, scan_of({}));
    t->born(static_cast<size_t>(u3), 0, 0xB0100400, 41, 3, true, now);
    CHECK(t->request(Why::ItsLogout, now + 5) == 1 && t->u[u3].state == UState::Open);
    t->close(static_cast<size_t>(u3), now + 6);
    CHECK(t->u[u3].state == UState::Retire && t->u[u3].why == Why::ItsLogout);
    // Una unidad abierta DESPUÉS de la petición no se retira por ella.
    const int u4 = t->open(kRootDesc, 0xB0100500, 51, 3, kU2Char, kU2Ptr, kKey2, true, 81, 1, now + 100, scan_of({}));
    t->born(static_cast<size_t>(u4), 0, 0xB0100500, 51, 3, true, now + 100);
    t->close(static_cast<size_t>(u4), now + 100);
    CHECK(t->request(Why::ItsLoad, now + 50) == 0 && t->u[u4].state == UState::Live);
    // Otra época: las asignadas vivas de épocas anteriores se retiran.
    CHECK(t->request_epoch(4, now + 200) == 1 && t->u[u4].state == UState::Retire && t->u[u4].why == Why::Epoch);
    // Una unidad abierta cuyo Troc nunca terminó (salida perdida): el borrado la cierra pasado su plazo (sus punteros
    // se olvidan) y, asignada con un reservado sin nacer, se retira entera.
    auto t2 = std::make_unique<SmallTable>();
    const int u5 = t2->open(kRootDesc, 0xB0100600, 61, 3, kU2Char, kU2Ptr, kKey2, true, 82, 1, now,
                            scan_of({{0x8000, road::kNoIndex}}));
    t2->born(static_cast<size_t>(u5), 0, 0xB0100600, 61, 3, true, now);
    t2->prune(3, now + SmallTable::kOpenStaleUs - 1);
    CHECK(t2->u[u5].state == UState::Open && t2->find_desc(static_cast<size_t>(u5), 0x8000) == 1);
    t2->prune(3, now + SmallTable::kOpenStaleUs);
    CHECK(t2->u[u5].state == UState::Retire && t2->find_desc(static_cast<size_t>(u5), 0x8000) == -1 &&
          t2->u[u5].split_why == Split::NotBorn);
}
void table_capacity_and_reuse() {
    // Registro ANTES de autoridad: sin sitio, -1 (la raíz no se asigna); nada a medias.
    auto t = std::make_unique<road::Table<2, 16>>();
    const road::Scan none = scan_of({});
    CHECK(t->open(kRootDesc, 0xB0000001, 1, 1, kU2Char, kU2Ptr, kKey2, true, 1, 1, 0, none) == 0);
    CHECK(t->open(kRootDesc, 0xB0000002, 2, 1, kU2Char, kU2Ptr, kKey2, true, 1, 1, 0, none) == 1);
    CHECK(t->open(kRootDesc, 0xB0000003, 3, 1, kU2Char, kU2Ptr, kKey2, true, 1, 1, 0, none) == -1 && t->overflow == 1);
    CHECK(!t->find(0xB0000003));
    // Una unidad con más de 15 nodos no cabe (el preexamen ya la rechaza).
    road::Scan big;
    big.n = 16;
    t->free_unit(1);
    CHECK(t->open(kRootDesc, 0xB0000004, 4, 1, kU2Char, kU2Ptr, kKey2, true, 1, 1, 0, big) == -1);
    // Clave reutilizada: la nueva encarnación se encuentra en su sitio; borrar la vieja no la toca.
    CHECK(t->open(kRootDesc, 0xB0000001, 5, 1, kU2Char, kU2Ptr, kKey2, true, 1, 1, 0, none) == 1);
    CHECK(t->find(0xB0000001).unit == 1);
    t->free_unit(0);
    CHECK(t->find(0xB0000001).unit == 1);
    // Índice lleno: se reconstruye con las vivas.
    auto u = std::make_unique<road::Table<16, 16>>();
    for (uint32_t i = 0; i < 16; ++i) {
        const int k = u->open(kRootDesc, 0xB0001000 + i, i + 1, 1, kU2Char, kU2Ptr, kKey2, false, 1, 1, 0, none);
        if (k >= 0) {
            u->close(static_cast<size_t>(k), 0);
            if (i % 2) u->free_unit(static_cast<size_t>(k));
        }
    }
    size_t found = 0;
    for (uint32_t i = 0; i < 16; ++i) found += u->find(0xB0001000 + i) ? 1 : 0;
    CHECK(found >= 6 && u->live == found);
}

// ---------------------------------------------------------------- 8. retirada estricta, hojas a raíz
void strict_cleanup() {
    CHECK(road::logout_call(0, 0x55) == Call::Done);
    CHECK(road::logout_call(0x55, 0x55) == Call::AlreadyLeaving);
    // Auditoría: un error desconocido de Logout NUNCA es «hecho» (npcact::logout_result con strict=false sí lo era).
    CHECK(road::logout_call(0x77, 0x55) == Call::Refused);
    CHECK(road::logout_call(0x77, 0) == Call::Refused);
    CHECK(npcact::logout_result(0x77, 0x55, false) == npcact::kCleanDone);  // el comportamiento que NO se reutiliza
    road::Rec c;
    road::reset_rec(c);
    c.born = true;
    road::apply_call(c, Call::Refused, false, 100);
    CHECK(c.clean == Clean::Pending && c.tries == 1 && c.retry_us == 100 + road::kRetryUs);
    road::apply_call(c, Call::Refused, false, 200);
    road::apply_call(c, Call::Refused, false, 300);
    CHECK(c.clean == Clean::Failed);  // terminal, visible; nunca se finge la baja
    road::reset_rec(c);
    road::apply_call(c, Call::Unreadable, false, 1);
    CHECK(c.clean == Clean::Pending);  // ilegible no demuestra nada
    // Logout 0: saliendo; la desaparición se confirma (no está en el SAM) o queda SIN CONFIRMAR tras el plazo.
    road::reset_rec(c);
    road::apply_call(c, Call::Done, false, 1000);
    CHECK(c.clean == Clean::Leaving && c.leave_us == 1000);
    road::apply_call(c, Call::Present, true, 2000);
    CHECK(c.clean == Clean::Leaving && c.checks == 1);
    road::apply_call(c, Call::NotFound, true, 3000);
    CHECK(c.clean == Clean::Gone);
    road::reset_rec(c);
    road::apply_call(c, Call::AlreadyLeaving, false, 1000);
    road::apply_call(c, Call::Present, true, 1000 + road::kConfirmUs);
    CHECK(c.clean == Clean::Unconfirmed);
    road::reset_rec(c);
    road::apply_call(c, Call::NotOurs, false, 1);
    CHECK(c.clean == Clean::Gone);  // otra clave / controlador: ya no es nuestro, no se toca
    road::reset_rec(c);
    road::apply_call(c, Call::Fault, false, 1);
    CHECK(c.clean == Clean::Failed);
    // Hojas a raíz: carro <- conductor <- cabra; caballo <- carro.
    road::Unit x;
    x.n = 4;
    for (uint8_t k = 0; k < 4; ++k) {
        road::reset_rec(x.r[k]);
        x.r[k].born = true;
        x.r[k].key = 0xB0000010 + k;
    }
    x.r[1].parent = 0; x.r[1].depth = 1;  // conductor
    x.r[2].parent = 1; x.r[2].depth = 2;  // cabra
    x.r[3].parent = 0; x.r[3].depth = 1;  // caballo
    road::Plan p;
    road::leaves_first(x, p);
    CHECK(p.n == 4 && p.idx[0] == 2 && p.idx[3] == 0);
    CHECK(road::block_state(x, 0) == road::Block::Wait && road::block_state(x, 1) == road::Block::Wait &&
          road::block_state(x, 2) == road::Block::Free && road::block_state(x, 3) == road::Block::Free);
    x.r[2].clean = Clean::Leaving;  // la cabra sale pero sin confirmar: su padre espera
    CHECK(road::block_state(x, 1) == road::Block::Wait);
    x.r[2].clean = Clean::Gone;
    CHECK(road::block_state(x, 1) == road::Block::Free && road::block_state(x, 0) == road::Block::Wait);
    x.r[1].clean = Clean::Failed;  // el conductor no sale: el carro tampoco (terminal)
    x.r[3].clean = Clean::Gone;
    CHECK(road::block_state(x, 0) == road::Block::Terminal);
    // Un no nacido (reservado) no bloquea.
    x.r[1].born = false;
    CHECK(road::block_state(x, 0) == road::Block::Free);
    road::leaves_first(x, p);
    CHECK(p.n == 1 && p.idx[0] == 0);
}
// Una simulación de la limpieza: 65 / 91 / 128 unidades pendientes al irse el invitado, SIN ningún Troc suyo más: la
// continuación (cualquier Troc del servidor, 32 llamadas por pasada) las retira todas; con el SAM o el cerrojo
// ocupados no se finge nada.
struct FakeSam {
    std::map<uint32_t, uint32_t> alive;  // clave -> controlador
    std::map<uint32_t, int> leaving;     // clave -> comprobaciones que aún sigue en el SAM tras Logout
    Call call(uint32_t key, uint32_t ctl, bool logout) {
        auto it = alive.find(key);
        if (it == alive.end()) return Call::NotFound;
        if (it->second != ctl) return Call::NotOurs;
        if (!logout) {
            auto l = leaving.find(key);
            if (l != leaving.end() && l->second-- <= 0) {
                alive.erase(it);
                leaving.erase(l);
                return Call::NotFound;
            }
            return Call::Present;
        }
        if (leaving.count(key)) return Call::AlreadyLeaving;
        leaving[key] = 1;
        return Call::Done;
    }
};
size_t run_pass(road::Table<256, 8192>& t, FakeSam& sam, uint64_t now, bool busy) {
    if (busy) return 0;
    size_t calls = 0;
    int order[8];
    size_t no = 0;
    for (size_t i = 0; i < t.capacity() && no < 8; ++i) {
        if (t.u[i].state == UState::Retire) order[no++] = static_cast<int>(i);
    }
    for (size_t oi = 0; oi < no && calls < 32; ++oi) {
        road::Unit& u = t.u[order[oi]];
        road::Plan p;
        road::leaves_first(u, p);
        for (uint8_t i = 0; i < p.n && calls < 32; ++i) {
            road::Rec& c = u.r[p.idx[i]];
            const road::Block b = road::block_state(u, p.idx[i]);
            if (b == road::Block::Terminal) { c.clean = Clean::Failed; continue; }
            if (b == road::Block::Wait) continue;
            if (c.clean == Clean::Pending && now < c.retry_us) continue;
            const bool check = c.clean == Clean::Leaving;
            road::apply_call(c, sam.call(c.key, c.ctl, !check), check, now);
            ++calls;
        }
        t.settle(static_cast<size_t>(order[oi]), now);
    }
    return calls;
}
void continuation_without_guest_troc() {
    for (const size_t units : {65u, 91u, 128u}) {
        auto t = std::make_unique<road::Table<256, 8192>>();
        FakeSam sam;
        uint32_t key = 0xB0200000;
        for (size_t i = 0; i < units; ++i) {
            const road::Scan s = scan_of({{0x100, road::kNoIndex}});  // raíz + 1 miembro
            const int ui = t->open(kRootDesc, key, i + 1, 1, kU2Char, kU2Ptr, kKey2, true, 1, 1, 0, s);
            CHECK(ui >= 0);
            t->born(static_cast<size_t>(ui), 0, key, i + 1, 3, true, 0);
            t->u[ui].r[0].ctl = kKey2;
            sam.alive[key++] = kKey2;
            t->born(static_cast<size_t>(ui), 1, key, i + 1, 6, true, 0);
            t->u[ui].r[1].ctl = kKey2;
            sam.alive[key++] = kKey2;
            t->close(static_cast<size_t>(ui), 0);
        }
        CHECK(t->request(Why::ItsLogout, 1) == units);
        // Su Logout corrió UNA pasada (como mucho 32 llamadas); después, ningún Troc suyo: sólo los del anfitrión.
        size_t passes = 0;
        uint64_t now = 2;
        while (t->pending() && passes < 2000) {
            const bool busy = passes % 7 == 3;  // el cerrojo o el SAM a veces ocupados: nada se finge
            run_pass(*t, sam, now, busy);
            now += 100'000;
            ++passes;
        }
        CHECK(!t->pending());
        size_t done = 0;
        for (size_t i = 0; i < t->capacity(); ++i) done += t->u[i].state == UState::Done ? 1 : 0;
        CHECK(done == units && sam.alive.empty());
    }
    // Logout rechazado siempre: las unidades acaban FALLIDAS (terminales, visibles), no «hechas»; sus actores siguen.
    auto t = std::make_unique<road::Table<256, 8192>>();
    const int ui = t->open(kRootDesc, 0xB0300000, 1, 1, kU2Char, kU2Ptr, kKey2, true, 1, 1, 0, scan_of({{0x100, road::kNoIndex}}));
    t->born(static_cast<size_t>(ui), 0, 0xB0300000, 1, 3, true, 0);
    t->born(static_cast<size_t>(ui), 1, 0xB0300001, 2, 6, true, 0);
    t->close(static_cast<size_t>(ui), 0);
    t->request(Why::ItsLoad, 1);
    for (uint64_t now = 2; t->pending(); now += road::kRetryUs) {
        road::Unit& u = t->u[ui];
        road::Plan p;
        road::leaves_first(u, p);
        for (uint8_t i = 0; i < p.n; ++i) {
            road::Rec& c = u.r[p.idx[i]];
            const road::Block b = road::block_state(u, p.idx[i]);
            if (b == road::Block::Terminal) { c.clean = Clean::Failed; continue; }
            if (b == road::Block::Wait || (c.clean == Clean::Pending && now < c.retry_us)) continue;
            road::apply_call(c, Call::Refused, false, now);
        }
        t->settle(static_cast<size_t>(ui), now);
        if (now > 100 * road::kRetryUs) break;
    }
    CHECK(t->u[ui].state == UState::Failed && t->u[ui].r[0].clean == Clean::Failed &&
          t->u[ui].r[1].clean == Clean::Failed);
    // Fallida con vivos: no se borra (sigue visible); sin vivos (bajas nativas), sí.
    CHECK(t->prune(1, 10) == 0);
    t->on_logout(0xB0300000, 25);
    t->on_logout(0xB0300001, 25);
    CHECK(t->u[ui].r[0].clean == Clean::Failed);  // la baja nativa no reescribe un terminal...
    CHECK(t->prune(1, 11) == 1);                  // ...pero ya no queda nadie vivo
}

// ---------------------------------------------------------------- 9. paradas: 2005 y Req
void stops_and_checks() {
    using road::Check2005;
    CHECK(road::check_2005(false, kHostUser, kHostUser, false, 0) == Check2005::Ignore);  // probe: sólo se cuenta
    CHECK(road::check_2005(true, kKey2, kKey2, false, 0) == Check2005::Ok);               // al invitado, restore 0: normal
    CHECK(road::check_2005(true, kKey2, kHostUser, false, 1) == Check2005::BadController);
    CHECK(road::check_2005(true, kKey2, kHostUser, true, 1) == Check2005::BadController);
    CHECK(road::check_2005(true, kKey2, kKey2, true, 0) == Check2005::Restore0ToHost);
    CHECK(road::check_2005(true, kKey2, kKey2, true, 1) == Check2005::Ok);
    CHECK(road::check_2005(true, 0, 0, false, 1) == Check2005::BadController);
    // Doble control: sólo las Req que envía el controlador (S34: el no controlador no envió ninguna para 3/5/6).
    for (const uint16_t op : std::initializer_list<uint16_t>{2223, 2734, 2291, 2405, 2828}) CHECK(road::controller_only_req(op));
    CHECK(!road::controller_only_req(2556) && !road::controller_only_req(2550) && !road::controller_only_req(2005));
    for (size_t s = 0; s < static_cast<size_t>(road::Stop::Count); ++s) {
        CHECK(std::strcmp(road::stop_name(static_cast<road::Stop>(s)), "?") != 0);
    }
    // Posición de las Req del invitado: ¿se mueve?
    size_t off = 0;
    CHECK(road::req_position_offset(2223, off) && off == 0x8E);
    CHECK(road::req_position_offset(2556, off) && off == 4);
    CHECK(!road::req_position_offset(2734, off));
    road::Rec c;
    road::reset_rec(c);
    road::note_position(c, 100.f, 200.f);
    road::note_position(c, 100.5f, 200.f);
    CHECK(c.moved > 0.45f && c.moved < 0.55f);
    road::note_position(c, 103.f, 204.f);
    CHECK(c.moved > 4.99f && c.moved < 5.01f);
    road::note_position(c, std::numeric_limits<float>::quiet_NaN(), 0.f);
    road::note_position(c, 1e9f, 0.f);
    CHECK(c.moved > 4.99f && c.moved < 5.01f);
}

// ---------------------------------------------------------------- 10. claves y el resumen del invitado
void modes_and_guest_summary() {
    CHECK(npcact::parse_mode("1", true).mode == npcact::Mode::On);  // user2_road_control=1 se acepta (anfitrión)
    CHECK(npcact::parse_mode("probe", true).mode == npcact::Mode::Probe);
    CHECK(npcact::parse_mode("0", true).mode == npcact::Mode::Off && !npcact::parse_mode("0", true).bad);
    CHECK(npcact::parse_mode("2", true).bad);
    // El resumen del invitado separa por (solicitante, controlador): los viajeros del anfitrión (que se mueven) ya no
    // cuentan como «desplazados» del invitado.
    npcp::RoadGroups<2> g;
    g.add(kHostChar, kHostUser, true, false);
    g.add(kHostChar, kHostUser, true, false);
    g.add(kU2Char, kHostUser, false, false);
    g.add(kU2Char, kKey2, true, true);
    CHECK(g.n == 2 && g.g[0].requester == kHostChar && g.g[0].active == 2 && g.g[0].moved == 2);
    CHECK(g.g[1].requester == kU2Char && g.g[1].controller == kHostUser && g.g[1].moved == 0);
    CHECK(g.other_active == 1 && g.other_moved == 1);
}

// ---------------------------------------------------------------- 11. separación de U-e
void separate_from_ue() {
    // La tabla de U-e (u2s::KeyTable) y sus estadísticas nunca ven las claves de carretera: son estructuras distintas;
    // un drenaje / 'no aceptadas' / LeftAfterDrain de U-e recorre sólo su tabla.
    u2s::KeyTable<64> ue;
    auto t = std::make_unique<SmallTable>();
    const int ui = t->open(kRootDesc, 0xB0400000, 1, 1, kU2Char, kU2Ptr, kKey2, true, 1, 1, 0, scan_of({}));
    t->born(static_cast<size_t>(ui), 0, 0xB0400000, 1, 3, true, 0);
    const u2s::CtlStats st = u2s::ctl_stats(ue, u2s::kKeyAssigned, 10'000'000, 1);
    CHECK(st.tracked == 0 && st.unaccepted == 0 && st.left_after_drain == 0);
    CHECK(ue.flags(0xB0400000) == 0);
    // El tipo 7 / gimmick nunca entra en la cohorte de carretera.
    CHECK(!road::cohort_type(7) && !road::cohort_type(4));
}

// ---------------------------------------------------------------- revisión de la fase C
void review_fixes() {
    // §2: las clases de la 2550 en curso (0x29C4930: u8 + f32 + n x 24 B).
    Arena a;
    ArenaRead rd{&a};
    const uintptr_t pl = a.alloc(5 + 3 * 24);
    a.put<uint8_t>(pl, 4);
    const uint32_t classes[] = {0x1F5, 0x3FD, 0x1F5};
    for (size_t i = 0; i < 3; ++i) a.put<uint32_t>(pl + 5 + i * 24, classes[i]);
    road::Request q;
    CHECK(road::parse_request(rd, pl, 5 + 3 * 24, q) && q.n == 2);
    q.troc = 7;
    CHECK(road::in_request(q, 7, 0x3FD) == 1 && road::in_request(q, 7, 0xFA1) == 0);
    CHECK(road::in_request(q, 8, 0x3FD) == -1);  // otro Troc: desconocida
    CHECK(!road::parse_request(rd, pl, 5 + 3 * 24 + 1, q));  // tamaño que no es 5 + n x 24
    // §3.2: bucle de partidas por clase de carretera y global.
    auto g = std::make_unique<road::SplitGuard>();
    CHECK(!g->note(0xFA1, 1'000'000) && !g->blocked(0xFA1));
    CHECK(g->note(0xFA1, 2'000'000) && g->blocked(0xFA1) && !g->blocked(0x3FD));
    // Fuera de la ventana no cuentan.
    auto g2 = std::make_unique<road::SplitGuard>();
    g2->note(0x7D1, 1);
    CHECK(!g2->note(0x7D1, 1 + road::kSplitWindowUs) && !g2->blocked(0x7D1));
    auto g3 = std::make_unique<road::SplitGuard>();
    bool stopped = false;
    for (uint32_t i = 0; i < road::kGlobalSplits; ++i) {
        g3->note(0x100 + i, 1000 + i);
        stopped = g3->stop;
    }
    CHECK(stopped && g3->blocked(0x3FD));
    // §4.1: la cancelación negada se reintenta con espaciado y sólo falla pasado el presupuesto de tiempo.
    road::Unit x;
    x.pub_tries = 0; x.pub_first_us = 0; x.pub_next_us = 0;
    uint64_t t = 1'000'000;
    CHECK(road::pub_refused(x, t) == road::PubNext::Retry && x.pub_next_us == t + road::kRetryUs);
    for (int i = 0; i < 10; ++i) {
        t = x.pub_next_us;
        CHECK(road::pub_refused(x, t) == road::PubNext::Retry);
        CHECK(x.pub_next_us - t <= 5 * road::kRetryUs);
    }
    CHECK(road::pub_refused(x, x.pub_first_us + road::kPubBudgetUs) == road::PubNext::Failed);
    // §3.3: montado por un jugador: nunca Logout, reintento; terminal tras kMaxOccupied.
    road::Rec c;
    road::reset_rec(c);
    road::apply_call(c, Call::Occupied, false, 10);
    CHECK(c.clean == Clean::Pending && c.occupied == 1 && c.retry_us == 10 + road::kRetryUs && c.tries == 0);
    for (int i = 1; i < road::kMaxOccupied; ++i) road::apply_call(c, Call::Occupied, false, 10);
    CHECK(c.clean == Clean::Failed);
    // §3.2: un registro con un hijo sin sitio nunca se retira (terminal), y su padre tampoco.
    road::Unit u;
    u.n = 3;
    for (uint8_t k = 0; k < 3; ++k) {
        road::reset_rec(u.r[k]);
        u.r[k].born = true;
    }
    u.r[1].parent = 0; u.r[1].depth = 1; u.r[1].incomplete = true;
    u.r[2].parent = 1; u.r[2].depth = 2;
    CHECK(road::block_state(u, 1) == road::Block::Terminal);
    CHECK(road::block_state(u, 2) == road::Block::Free);
    u.r[2].clean = Clean::Gone;
    u.r[1].clean = Clean::Failed;
    CHECK(road::block_state(u, 0) == road::Block::Terminal);
    // §4.4: hojas a raíz también más allá de la profundidad 5 (tardíos de tardíos).
    road::Unit d;
    d.n = 8;
    for (uint8_t k = 0; k < 8; ++k) {
        road::reset_rec(d.r[k]);
        d.r[k].born = true;
        d.r[k].parent = k ? static_cast<uint8_t>(k - 1) : road::kNoIndex;
        d.r[k].depth = k;
    }
    road::Plan pl2;
    road::leaves_first(d, pl2);
    CHECK(pl2.n == 8 && pl2.idx[0] == 7 && pl2.idx[7] == 0);
    // Registro 47 de una unidad (S40: 48 por unidad) en el índice.
    CHECK(road::unpack_loc(road::pack_loc(255, 47)).unit == 255 && road::unpack_loc(road::pack_loc(255, 47)).rec == 47);
    CHECK(road::unpack_loc(road::pack_loc(1023, 47)).unit == 1023 && road::unpack_loc(road::pack_loc(1023, 47)).rec == 47);
    auto tb = std::make_unique<road::Table<4, 128>>();
    const int ui = tb->open(kRootDesc, 0xB0500000, 1, 1, kU2Char, kU2Ptr, kKey2, true, 1, 1, 0, scan_of({}));
    tb->born(static_cast<size_t>(ui), 0, 0xB0500000, 1, 5, true, 0);
    int last = -1;
    for (uint32_t k = 1; k < road::kMaxUnit; ++k) {
        last = tb->add_late(static_cast<size_t>(ui), 0x9000 + k, 0);
        CHECK(last == static_cast<int>(k) && tb->born(static_cast<size_t>(ui), k, 0xB0500000 + k, k + 1, 7, false, 0));
    }
    CHECK(tb->add_late(static_cast<size_t>(ui), 0x9999, 0) == -1);  // 49: sin sitio -> el padre queda incompleto
    CHECK(tb->find(0xB0500000 + 47).rec == 47);
}

// ---------------------------------------------------------------- S40 carros (docs/research/s25/s40-carros.md)
road::ChildIn good_linked() {
    road::ChildIn c;
    c.vt = road::kVtDescBase; c.type = 5; c.parent_host_kept = false; c.parent_type = 5;
    c.in_vector = 1; c.wrap = road::kVtWrapLinkedVehicle; c.fields_ok = true; c.mark = 0;
    c.req_key_match = true; c.road_match = true; c.parent_key_match = true; c.player_rides = false;
    c.m = good_member();
    c.m.reserved = false; c.m.room = true; c.m.type = 5; c.m.traveller = false;
    return c;
}
// La carreta medida (C probe 2, 0xB01002D8, carretera 0xFA1) por la tabla y los veredictos, tal como nace en el Troc.
struct Cart {
    std::unique_ptr<road::Table<8, 256>> t = std::make_unique<road::Table<8, 256>>();
    int ui = -1;
    // Un hijo sin marca: veredicto con su padre de la unidad, registro y nacimiento como road_foreign_child.
    road::ChildOut child(uint32_t key, uintptr_t desc, uintptr_t parent_desc, uint32_t vt, uint8_t type, bool linked) {
        road::Unit& u = t->u[ui];
        const int pi = t->find_desc(static_cast<size_t>(ui), parent_desc);
        CHECK(pi >= 0);
        road::ChildIn c = linked ? good_linked() : road::ChildIn{};
        c.vt = vt;
        c.type = type;
        if (!linked) c.in_vector = 0;
        c.parent_host_kept = u.r[pi].host_kept;
        c.parent_type = u.r[pi].type;
        c.m.room = u.n < road::kMaxUnit;
        const road::ChildOut o = road::child_verdict(c);
        const int ri = t->add_late(static_cast<size_t>(ui), desc, static_cast<uint8_t>(pi));
        const bool assigned = o.v == MemberV::Assign && u.assigned;
        if (ri >= 0 && t->born(static_cast<size_t>(ui), static_cast<size_t>(ri), key, key, type, assigned, 0)) {
            u.r[ri].host_kept = o.v == MemberV::HostKept;
            u.r[ri].split = o.v == MemberV::Split;
            u.r[ri].linked = o.kind == road::ChildKind::Linked && o.v == MemberV::Assign;
        } else {
            u.r[pi].incomplete = true;
        }
        if (o.v == MemberV::Split) t->mark_split(static_cast<size_t>(ui), o.why, 0);
        return o;
    }
};
void carts_s40() {
    // Las claves y el veredicto de la raíz.
    using road::Carts;
    CHECK(!road::root_writes(true, Carts::Off, 5) && !road::root_writes(true, Carts::Probe, 5) &&
          road::root_writes(true, Carts::On, 5));
    CHECK(road::root_writes(true, Carts::Off, 3) && road::root_writes(true, Carts::Probe, 6));
    CHECK(!road::root_writes(false, Carts::On, 5) && !road::root_writes(false, Carts::On, 3));
    CHECK(!road::carts_allowed(true, Carts::Off) && road::carts_allowed(true, Carts::Probe) &&
          road::carts_allowed(true, Carts::On) && road::carts_allowed(false, Carts::Off));
    CHECK(std::strcmp(road::carts_name(Carts::Probe), "probe") == 0);
    road::RootIn r = good_root(prov_of());
    r.type = 5;
    r.carts_allowed = false;
    CHECK(road::root_verdict(r) == Verdict::CartRoot);
    r.carts_allowed = true;
    CHECK(road::root_verdict(r) == Verdict::Assign);
    r.player_rides = true;  // un jugador ya lo monta: nunca
    CHECK(road::root_verdict(r) == Verdict::PlayerRides);
    r.type = 3;  // sólo cuenta para los tipo 5 (el mid sólo lo lee para ellos)
    CHECK(road::root_verdict(r) == Verdict::Assign);

    // child_verdict: carga, gimmicks, vehículo enlazado y ajenos.
    const auto is = [](const road::ChildOut& o, MemberV v, Split w, road::ChildKind k) {
        return o.v == v && o.why == w && o.kind == k;
    };
    road::ChildIn c;
    c.parent_host_kept = true; c.type = 7; c.vt = road::kVtDescBase;
    CHECK(is(road::child_verdict(c), MemberV::HostKept, Split::None, road::ChildKind::Cargo));
    c.vt = road::kVtInstantGimmick;
    CHECK(is(road::child_verdict(c), MemberV::HostKept, Split::None, road::ChildKind::Cargo));
    c.vt = 0x5B04CB0;  // NPCSchedule bajo un gimmick: no es carga
    CHECK(is(road::child_verdict(c), MemberV::Split, Split::CargoType, road::ChildKind::Cargo));
    c.vt = road::kVtDescBase; c.type = 3;
    CHECK(is(road::child_verdict(c), MemberV::Split, Split::CargoType, road::ChildKind::Cargo));
    c = road::ChildIn{};
    c.vt = road::kVtInstantGimmick; c.type = 7;
    CHECK(is(road::child_verdict(c), MemberV::HostKept, Split::None, road::ChildKind::Gimmick));
    c.type = 5;
    CHECK(is(road::child_verdict(c), MemberV::Split, Split::Type, road::ChildKind::Gimmick));
    c = road::ChildIn{};
    c.vt = road::kVtDescBase; c.type = 3; c.in_vector = 0;  // descriptor base sin marca que no es un vehículo enlazado
    CHECK(is(road::child_verdict(c), MemberV::Split, Split::NotTraveller, road::ChildKind::Foreign));
    c.in_vector = -1;
    CHECK(is(road::child_verdict(c), MemberV::Split, Split::LinkUnknown, road::ChildKind::Foreign));
    c = road::ChildIn{};
    c.vt = 0x5B04CB0; c.type = 3;
    CHECK(is(road::child_verdict(c), MemberV::Split, Split::NotTraveller, road::ChildKind::Foreign));
    c = good_linked();
    c.wrap = road::kVtWrapSimple;  // descriptor base de tipo 5 con otro envoltorio: ajeno
    CHECK(is(road::child_verdict(c), MemberV::Split, Split::NotTraveller, road::ChildKind::Foreign));
    // El vehículo enlazado: el contrato entero de 0x2797510 para ESTE padre.
    CHECK(is(road::child_verdict(good_linked()), MemberV::Assign, Split::None, road::ChildKind::Linked));
    const auto lk = [](void (*f)(road::ChildIn&), Split w) {
        road::ChildIn x = good_linked();
        f(x);
        const road::ChildOut o = road::child_verdict(x);
        return o.v == MemberV::Split && o.why == w && o.kind == road::ChildKind::Linked;
    };
    CHECK(lk([](road::ChildIn& x) { x.in_vector = 0; }, Split::LateNotInVector));
    CHECK(lk([](road::ChildIn& x) { x.in_vector = -1; }, Split::LateNotInVector));
    CHECK(lk([](road::ChildIn& x) { x.parent_type = 3; }, Split::LinkedMismatch));
    CHECK(lk([](road::ChildIn& x) { x.fields_ok = false; }, Split::LinkedMismatch));
    CHECK(lk([](road::ChildIn& x) { x.mark = 1; }, Split::LinkedMismatch));
    CHECK(lk([](road::ChildIn& x) { x.req_key_match = false; }, Split::LinkedMismatch));
    CHECK(lk([](road::ChildIn& x) { x.road_match = false; }, Split::LinkedMismatch));
    CHECK(lk([](road::ChildIn& x) { x.parent_key_match = false; }, Split::LinkedMismatch));
    CHECK(lk([](road::ChildIn& x) { x.type = 3; x.m.type = 3; }, Split::Type));
    CHECK(lk([](road::ChildIn& x) { x.player_rides = true; }, Split::PlayerRides));
    CHECK(lk([](road::ChildIn& x) { x.m.room = false; }, Split::LateNoRoom));
    CHECK(lk([](road::ChildIn& x) { x.m.parent_ctl = 0; }, Split::ParentController));
    CHECK(lk([](road::ChildIn& x) { x.m.parent_alive = 0; }, Split::ParentGone));
    CHECK(lk([](road::ChildIn& x) { x.m.old_ctl = 0x90100000; }, Split::HasController));
    CHECK(lk([](road::ChildIn& x) { x.m.actor_user = 1; }, Split::UserChild));
    CHECK(lk([](road::ChildIn& x) { x.m.owner_branch = false; }, Split::NotOwnerBranch));
    CHECK(lk([](road::ChildIn& x) { x.m.key2 = 0; }, Split::NoKey));
    {
        road::ChildIn x = good_linked();
        x.m.unit_live = false;
        CHECK(road::child_verdict(x).v == MemberV::HostUnit);
    }
    for (size_t k = 0; k < static_cast<size_t>(road::ChildKind::Count); ++k) {
        CHECK(std::strcmp(road::child_kind_name(static_cast<road::ChildKind>(k)), "?") != 0);
    }

    // in_vector da el envoltorio (_LinkedVehicle) y el preexamen sigue rechazando uno ya presente al nacer la raíz
    // (unidad entera con el anfitrión: seguro; en la build 2976 nace siempre después).
    {
        Arena a;
        ArenaRead rd{&a};
        const uintptr_t root = a.desc(road::kVtDescBase);
        a.mark(root, kU2Char, kU2Ptr, 0x101, 0xFA1);
        const uintptr_t lv = a.desc(road::kVtDescBase);
        a.put<uint16_t>(lv + road::kDesc_First, 0);
        a.put<uint32_t>(lv + road::kDesc_ReqKey, kU2Char);
        a.put<uint32_t>(lv + road::kDesc_Road, 0xFA1);
        a.link(root, road::kVtWrapLinkedVehicle, lv);
        uint32_t wrap = 0;
        bool rider = true;
        CHECK(road::in_vector(rd, root, lv, kBase, kImage, &rider, &wrap) == 1 && !rider &&
              wrap == road::kVtWrapLinkedVehicle);
        road::Scan s;
        road::scan_unit(rd, root, kBase, kImage, kU2Char, kU2Ptr, s);
        CHECK(s.v == ScanV::NotRider && s.bad_wrap == road::kVtWrapLinkedVehicle);
    }

    // La carreta medida, en el orden en que nace (C probe 2): raíz 0xB01002D8 (t5) con su conductor D9 (_Rider, del
    // preexamen); tardíos: DA (vehículo enlazado t5), DB (gimmick t7) con su carga DC..E1 (DD hija de DC), E2, E3.
    constexpr uintptr_t dRoot = 0x1000, dDrv = 0x1100, dLv = 0x1200, dGim = 0x1300, dE2 = 0x1400, dE3 = 0x1500;
    for (const bool assigned : {true, false}) {  // carros=1 / carros=probe (o user2_road_control=probe)
        Cart k;
        k.ui = k.t->open(dRoot, 0xB01002D8, 576, 1, kU2Char, kU2Ptr, kKey2, assigned, 9, 1, 0,
                         scan_of({{dDrv, road::kNoIndex}}));
        CHECK(k.ui >= 0);
        road::Unit& u = k.t->u[k.ui];
        u.road = 0xFA1;
        k.t->born(static_cast<size_t>(k.ui), 0, 0xB01002D8, 576, 5, assigned, 0);
        k.t->born(static_cast<size_t>(k.ui), 1, 0xB01002D9, 579, 3, assigned, 0);
        CHECK(is(k.child(0xB01002DA, dLv, dRoot, road::kVtDescBase, 5, true), MemberV::Assign, Split::None,
                 road::ChildKind::Linked));
        CHECK(is(k.child(0xB01002DB, dGim, dRoot, road::kVtInstantGimmick, 7, false), MemberV::HostKept, Split::None,
                 road::ChildKind::Gimmick));
        for (uint32_t i = 0; i < 5; ++i) {
            CHECK(is(k.child(0xB01002DC + i + (i ? 1 : 0), 0x2000 + i * 0x100, dGim, road::kVtDescBase, 7, false),
                     MemberV::HostKept, Split::None, road::ChildKind::Cargo));
        }
        CHECK(is(k.child(0xB01002DD, 0x3000, 0x2000, road::kVtDescBase, 7, false), MemberV::HostKept, Split::None,
                 road::ChildKind::Cargo));  // nieta de la carga (profundidad 3)
        CHECK(is(k.child(0xB01002E2, dE2, dRoot, road::kVtInstantGimmick, 7, false), MemberV::HostKept, Split::None,
                 road::ChildKind::Gimmick));
        CHECK(is(k.child(0xB01002E3, dE3, dRoot, road::kVtInstantGimmick, 7, false), MemberV::HostKept, Split::None,
                 road::ChildKind::Gimmick));
        // Antes (S38) esta unidad salía PARTIDA por DA y la carga (7 «viajero partido»): ahora completa, 12 registros.
        CHECK(u.n == 12 && !u.split && k.t->find(0xB01002DA).rec == 2);
        CHECK(u.r[2].assigned == assigned && u.r[2].linked && !u.r[3].assigned && u.r[3].host_kept);
        const road::Composition o = road::compose(u);
        CHECK(o.root_type == 5 && o.riders == 1 && o.linked == 1 && o.linked_assigned == (assigned ? 1 : 0) &&
              o.gimmicks == 3 && o.cargo == 6 && o.foreign == 0 && o.max_depth == 3 && !o.incomplete && o.born == 12);
        CHECK(k.t->close(static_cast<size_t>(k.ui), 0) == 0 && u.state == UState::Live);
        auto book = std::make_unique<road::CaravanBook>();
        book->note(u);
        CHECK(book->n == 1 && book->c[0].road == 0xFA1 && book->c[0].units == 1 &&
              book->c[0].assigned == (assigned ? 1u : 0u) && book->c[0].complete == 1 && book->c[0].split == 0 &&
              book->c[0].max_n == 12 && book->c[0].max_cargo == 6 && book->c[0].linked == 1 && book->c[0].riders == 1);
        // probe nunca se retira; asignada: su carga (Logout 25) va siempre antes que su gimmick, y éste antes que la
        // raíz; con la carga viva, el gimmick y la raíz esperan.
        CHECK(k.t->request(Why::ItsLoad, 1) == (assigned ? 1u : 0u));
        if (!assigned) continue;
        road::Plan p;
        road::leaves_first(u, p);
        CHECK(p.n == 12 && p.idx[0] == 9);  // DD, la más profunda
        CHECK(road::block_state(u, 3) == road::Block::Wait && road::block_state(u, 0) == road::Block::Wait);
        CHECK(road::block_state(u, 2) == road::Block::Free && road::block_state(u, 1) == road::Block::Free);
        for (uint8_t i = 4; i < 10; ++i) u.r[i].clean = Clean::Gone;  // la carga fuera
        CHECK(road::block_state(u, 3) == road::Block::Free && road::block_state(u, 0) == road::Block::Wait);
        for (uint8_t i = 1; i < 12; ++i) u.r[i].clean = Clean::Gone;
        CHECK(road::block_state(u, 0) == road::Block::Free);
        // Un jugador monta la raíz: nunca Logout; reintento y, a los ~120 s, terminal (visible).
        road::apply_call(u.r[0], Call::Occupied, false, 5);
        CHECK(u.r[0].clean == Clean::Pending && u.r[0].occupied == 1);
    }
    // La carreta más grande medida (fase B prueba 3, 0xB01002A6: 19 de carga): 25 registros, caben (48).
    {
        Cart k;
        k.ui = k.t->open(dRoot, 0xB01002A6, 1, 1, kU2Char, kU2Ptr, kKey2, true, 9, 1, 0,
                         scan_of({{dDrv, road::kNoIndex}}));
        k.t->u[k.ui].road = 0xFA1;
        k.t->born(static_cast<size_t>(k.ui), 0, 0xB01002A6, 1, 5, true, 0);
        k.t->born(static_cast<size_t>(k.ui), 1, 0xB01002A7, 2, 3, true, 0);
        k.child(0xB01002A8, dLv, dRoot, road::kVtDescBase, 5, true);
        k.child(0xB01002A9, dGim, dRoot, road::kVtInstantGimmick, 7, false);
        for (uint32_t i = 0; i < 19; ++i) {
            k.child(0xB01002AA + i, 0x2000 + i * 0x100, dGim, road::kVtDescBase, 7, false);
        }
        k.child(0xB01002BD, dE2, dRoot, road::kVtInstantGimmick, 7, false);
        k.child(0xB01002BE, dE3, dRoot, road::kVtInstantGimmick, 7, false);
        const road::Composition o = road::compose(k.t->u[k.ui]);
        CHECK(k.t->u[k.ui].n == 25 && !k.t->u[k.ui].split && o.cargo == 19 && o.gimmicks == 3 && !o.incomplete);
        // Una carga que no cabe deja a su gimmick incompleto: nunca se retira (ni la raíz): terminal, visible.
        for (uint32_t i = 0; i < 30; ++i) {
            k.child(0xB0100300 + i, 0x5000 + i * 0x100, dGim, road::kVtDescBase, 7, false);
        }
        CHECK(k.t->u[k.ui].n == road::kMaxUnit && road::compose(k.t->u[k.ui]).incomplete &&
              road::block_state(k.t->u[k.ui], 3) == road::Block::Terminal);
    }
    // Un caballo con jinete (carretera 0x7D1): raíz t5 + jinete t3 (_Rider del preexamen): completa, un jinete.
    {
        Cart k;
        k.ui = k.t->open(dRoot, 0xB01002BD, 1, 1, kU2Char, kU2Ptr, kKey2, true, 9, 1, 0,
                         scan_of({{dDrv, road::kNoIndex}}));
        k.t->u[k.ui].road = 0x7D1;
        k.t->born(static_cast<size_t>(k.ui), 0, 0xB01002BD, 1, 5, true, 0);
        k.t->born(static_cast<size_t>(k.ui), 1, 0xB01002BE, 2, 3, true, 0);
        const road::Composition o = road::compose(k.t->u[k.ui]);
        CHECK(o.riders == 1 && o.linked == 0 && o.gimmicks == 0 && o.born == 2);
        // Un hijo ajeno (otro descriptor) parte la unidad y el libro lo cuenta por su clase y motivo.
        k.child(0xB01002BF, 0x7000, dRoot, 0x5B04CB0, 3, false);
        CHECK(k.t->u[k.ui].split && k.t->u[k.ui].split_why == Split::NotTraveller);
        k.t->close(static_cast<size_t>(k.ui), 0);
        CHECK(k.t->u[k.ui].state == UState::Retire);  // asignada y partida: se retira entera
        auto book = std::make_unique<road::CaravanBook>();
        book->note(k.t->u[k.ui]);
        CHECK(book->c[0].road == 0x7D1 && book->c[0].split == 1 &&
              book->c[0].why[static_cast<size_t>(Split::NotTraveller)] == 1 && book->c[0].foreign == 1);
    }
    // El bucle por clase cubre los carros: dos carretas 0xFA1 partidas en 10 min paran su clase (y no la de los
    // caminantes); las de probe van a otro contador que nunca bloquea el real.
    {
        auto real = std::make_unique<road::SplitGuard>(), would = std::make_unique<road::SplitGuard>();
        for (int i = 0; i < 6; ++i) would->note(0xFA1, 1000 + i);
        CHECK(would->stop && !real->blocked(0xFA1) && !real->stop);
        real->note(0xFA1, 1);
        CHECK(real->note(0xFA1, 2) && real->blocked(0xFA1) && !real->blocked(0x3FD) && !real->stop);
    }
    // El libro: 8 clases y el resto contado aparte.
    {
        auto book = std::make_unique<road::CaravanBook>();
        auto x = std::make_unique<road::Unit>();
        x->n = 1; x->split = false; x->assigned = false; x->split_why = Split::None;
        road::reset_rec(x->r[0]);
        x->r[0].born = true; x->r[0].type = 6;
        for (uint32_t i = 0; i < 10; ++i) {
            x->road = 0x100 + i;
            book->note(*x);
        }
        CHECK(book->n == road::kCaravanClasses && book->other_units == 2);
    }
}

// ---------------------------------------------------------------- revisión de S40 (build/research/carros-revision.md)
// Un actor con su componente de vehículo como lo lee mount_sync: [actor+0x68] -> comps, [comps+0x48] -> vc,
// [vc+0x08] == actor; asientos vc+0x48, reservas vc+0x88, recuento vc+0xC8, vínculo [vc+0x18] (+0x08 / +0x10).
struct VehicleArena {
    Arena a;
    uintptr_t actor = 0, vc = 0;
    explicit VehicleArena(bool back_ok = true) {
        actor = a.alloc(0x100);
        const uintptr_t comps = a.alloc(0x100);
        vc = a.alloc(0x100);
        a.put<uintptr_t>(actor + road::kActor_Comps, comps);
        a.put<uintptr_t>(comps + road::kComps_Vehicle, vc);
        a.put<uintptr_t>(vc + road::kVc_Actor, back_ok ? actor : actor + 8);
    }
    void link(uint32_t cur, uint32_t res) {
        const uintptr_t l = a.alloc(0x20);
        a.put<uintptr_t>(vc + road::kVc_Link, l);
        a.put<uint32_t>(l + road::kLink_Vehicle, cur);
        a.put<uint32_t>(l + road::kLink_ResVehicle, res);
    }
};
void carts_review() {
    // §4: los asientos del vehículo (validados por el puntero de vuelta) y el vínculo de montura de un personaje.
    {
        VehicleArena v;
        ArenaRead rd{&v.a};
        road::SeatsRead s = road::read_seats(rd, v.actor);
        CHECK(s.ok && !s.player && s.count == 0);
        v.a.put<uint32_t>(v.vc + road::kVc_Seats + 4, 0xB01002D9);  // un conductor NPC: no es un jugador
        v.a.put<uint8_t>(v.vc + road::kVc_Count, 1);
        s = road::read_seats(rd, v.actor);
        CHECK(s.ok && !s.player && s.count == 1);
        v.a.put<uint32_t>(v.vc + road::kVc_Reserves + 8, kU2Char);  // el invitado reserva un asiento
        s = road::read_seats(rd, v.actor);
        CHECK(s.ok && s.player && s.who == kU2Char);
        v.a.put<uint8_t>(v.vc + road::kVc_Count, 17);  // recuento imposible: no se fía
        CHECK(!road::read_seats(rd, v.actor).ok);
        VehicleArena bad(false);  // puntero de vuelta distinto: no es la forma conocida
        ArenaRead rb{&bad.a};
        CHECK(!road::read_seats(rb, bad.actor).ok);
        CHECK(!road::read_seats(rd, 0).ok);
    }
    {
        VehicleArena c;
        ArenaRead rd{&c.a};
        road::CharLink l = road::read_char_link(rd, c.actor);
        CHECK(l.present && l.ok && l.cur == 0 && l.res == 0);  // sin vínculo: no monta nada
        c.link(0xB01002D8, 0);
        l = road::read_char_link(rd, c.actor);
        CHECK(l.ok && l.cur == 0xB01002D8);
        CHECK(!road::read_char_link(rd, 0).present);
        VehicleArena bad(false);
        ArenaRead rb{&bad.a};
        const road::CharLink lb = road::read_char_link(rb, bad.actor);
        CHECK(lb.present && !lb.ok);
    }
    // ride_check: cualquier fuente positiva monta; un tipo 5 sólo libre con asientos Y vínculos validados.
    {
        using road::Ride;
        road::SeatsRead seats_ok;
        seats_ok.ok = true;
        road::SeatsRead seats_bad;
        road::CharLink ok_free;
        ok_free.present = true; ok_free.ok = true;
        road::CharLink unread;
        unread.present = true;
        road::CharLink absent;
        road::CharLink rides = ok_free;
        rides.cur = 0xB01002D8;
        const uint32_t k = 0xB01002D8;
        const road::CharLink both_ok[2] = {ok_free, ok_free}, one_unread[2] = {ok_free, unread},
                             guest_rides[2] = {ok_free, rides}, none[2] = {absent, absent};
        CHECK(road::ride_check(k, true, false, false, both_ok, seats_ok) == Ride::Free);
        CHECK(road::ride_check(k, true, false, false, both_ok, seats_bad) == Ride::Unknown);  // asientos ilegibles
        CHECK(road::ride_check(k, true, false, false, one_unread, seats_ok) == Ride::Unknown);  // un vínculo ilegible
        CHECK(road::ride_check(k, true, false, false, none, seats_ok) == Ride::Free);  // nadie conectado
        CHECK(road::ride_check(k, true, false, false, guest_rides, seats_ok) == Ride::Rides);  // el invitado lo monta
        CHECK(road::ride_check(k, true, false, false, guest_rides, seats_bad) == Ride::Rides);
        CHECK(road::ride_check(k, true, true, false, both_ok, seats_ok) == Ride::Rides);  // +0xAC
        CHECK(road::ride_check(k, true, false, true, both_ok, seats_ok) == Ride::Rides);  // mount_sync
        road::SeatsRead seat_player = seats_ok;
        seat_player.player = true;
        CHECK(road::ride_check(k, true, false, false, both_ok, seat_player) == Ride::Rides);  // asiento / reserva
        rides.cur = 0; rides.res = k;  // reservado: a punto de montar
        const road::CharLink reserved[2] = {rides, ok_free};
        CHECK(road::ride_check(k, true, false, false, reserved, seats_ok) == Ride::Rides);
        // Otros tipos: sin prueba exigida (como antes), sólo las fuentes positivas.
        CHECK(road::ride_check(k, false, false, false, one_unread, seats_bad) == Ride::Free);
        CHECK(road::ride_check(k, false, true, false, one_unread, seats_bad) == Ride::Rides);
    }
    // RideUnknown: nunca Logout, reintento como Occupied y terminal acotado; KeptChanged: terminal (su padre espera).
    {
        road::Rec c;
        road::reset_rec(c);
        road::apply_call(c, Call::RideUnknown, false, 7);
        CHECK(c.clean == Clean::Pending && c.occupied == 1 && c.retry_us == 7 + road::kRetryUs);
        for (int i = 1; i < road::kMaxOccupied; ++i) road::apply_call(c, Call::RideUnknown, false, 7);
        CHECK(c.clean == Clean::Failed);
        CHECK(road::kept_result(Call::NotOurs, true) == Call::KeptChanged);
        CHECK(road::kept_result(Call::NotOurs, false) == Call::NotOurs);
        CHECK(road::kept_result(Call::NotFound, true) == Call::NotFound);
        road::Unit u;
        u.n = 2;
        road::reset_rec(u.r[0]); road::reset_rec(u.r[1]);
        u.r[0].born = u.r[1].born = true;
        u.r[1].parent = 0; u.r[1].depth = 1; u.r[1].host_kept = true;
        road::apply_call(u.r[1], road::kept_result(Call::NotOurs, true), true, 1);
        CHECK(u.r[1].clean == Clean::Failed && road::block_state(u, 0) == road::Block::Terminal);
        for (size_t i = 0; i < static_cast<size_t>(Call::Count); ++i) {
            CHECK(std::strcmp(road::call_name(static_cast<Call>(i)), "?") != 0);
        }
    }
    // §1: con user2_road_carts=0, child_verdict es exactamente la regla de ef5a6b5 (gimmick_verdict).
    {
        const auto old = [](uint32_t vt, uint8_t type) {
            return road::gimmick_verdict(vt == road::kVtInstantGimmick, type);
        };
        road::ChildIn cargo;
        cargo.carts_rules = false; cargo.parent_host_kept = true; cargo.vt = road::kVtDescBase; cargo.type = 7;
        road::ChildOut o = road::child_verdict(cargo);
        CHECK(o.v == old(cargo.vt, 7).v && o.why == old(cargo.vt, 7).why && o.v == MemberV::Split &&
              o.why == Split::NotTraveller);  // la carga parte la unidad, como antes
        road::ChildIn lv = good_linked();
        lv.carts_rules = false;
        o = road::child_verdict(lv);
        CHECK(o.v == MemberV::Split && o.why == Split::NotTraveller);  // el enlazado de un miembro t5 también
        road::ChildIn g;
        g.carts_rules = false; g.vt = road::kVtInstantGimmick; g.type = 7;
        CHECK(road::child_verdict(g).v == MemberV::HostKept);
        g.parent_host_kept = true;  // un InstantGimmick bajo un gimmick: host-kept, como antes
        CHECK(road::child_verdict(g).v == MemberV::HostKept);
    }
}
// Rotación nativa (build/research/churn-viajeros/informe.md §3): muestreo, paridad y rama del recolector.
void churn_hygiene() {
    // (1) Muestreo: las primeras 40 de cada carretera, luego 1 de cada 25; keep_all siempre; claves separadas.
    {
        CHECK(!road::sample_keep(0));
        CHECK(road::sample_keep(1) && road::sample_keep(40) && !road::sample_keep(41) && !road::sample_keep(64));
        CHECK(road::sample_keep(65) && road::sample_keep(90) && !road::sample_keep(91));
        auto s = std::make_unique<road::Sampler<32>>();
        uint32_t kept = 0, ord = 0;
        for (int i = 0; i < 2435; ++i) kept += s->take(road::sample_key(0x1F5, true), false, &ord);
        CHECK(ord == 2435);
        CHECK(kept == 40 + (2435 - 40) / 25);  // ~30 min de bandadas al ritmo nativo: 135 líneas, no 2435
        CHECK(s->kept.load() == kept && s->dropped.load() == 2435 - kept);
        // Otra carretera y la misma en probe empiezan su propia cuenta.
        CHECK(s->take(road::sample_key(0x3FD, true), false, &ord) && ord == 1);
        CHECK(s->take(road::sample_key(0x1F5, false), false, &ord) && ord == 1);
        CHECK(road::sample_key(0x1F5, true) != road::sample_key(0x1F5, false));
        // Tipo 5 (raíces y unidades de carros): todas.
        bool all5 = true;
        for (int i = 0; i < 100; ++i) all5 = all5 && s->take(road::sample_key(0xFA1, true), true);
        CHECK(all5);
        // Más carreteras que ranuras: un ordinal común, contado.
        auto t = std::make_unique<road::Sampler<4, 1, 2>>();
        for (uint32_t r = 1; r <= 4; ++r) CHECK(t->take(r, false, &ord) && ord == 1);
        CHECK(t->take(5, false, &ord) && ord == 1 && t->overflow.load() == 1);
        CHECK(!t->take(6, false, &ord) && ord == 2);
        CHECK(t->take(7, false, &ord) && ord == 3);
        t->take(0xFFFFFFFFu, false, &ord);  // clave + 1 = 0: sin ranura
        CHECK(ord == 4 && t->overflow.load() == 4);
    }
    // (2) Paridad: altas, bajas, vida mediana, ventana, contraparte y veredicto.
    {
        auto p = std::make_unique<road::Parity>();
        const uint64_t t0 = 1'000'000'000;
        // Invitado 0x1F5 t6: 80 altas en 60 s, cada una cae a los 29 s (motivo 25).
        for (uint32_t i = 0; i < 80; ++i) {
            CHECK(p->birth(0xB0100000 + i, road::Side::Guest, 0x1F5, 6, t0 + i * 750'000ull));
        }
        CHECK(!p->birth(0xB0100000, road::Side::Guest, 0x1F5, 6, t0) && p->again == 1);  // ya seguida
        CHECK(p->live_n == 80 && p->win_start_us == t0);
        const int g = p->find_class(road::Side::Guest, 0x1F5, 6);
        CHECK(g >= 0 && p->c[g].births == 80);
        CHECK(p->rate_x10(static_cast<size_t>(g), t0 + 60'000'000) == 800);  // 80,0 altas/min
        bool deaths_ok = true;
        for (uint32_t i = 0; i < 80; ++i) {
            const road::ParityDeath d = p->death(0xB0100000 + i, 25, t0 + i * 750'000ull + 29'000'000, true);
            deaths_ok = deaths_ok && d.found && d.side == road::Side::Guest && d.road == 0x1F5 && d.type == 6 &&
                        d.life_us == 29'000'000;
        }
        CHECK(deaths_ok);
        CHECK(p->live_n == 0 && p->c[g].deaths == 80 && p->c[g].r25 == 80 && p->c[g].ring_n == road::kParityRing);
        CHECK(p->median_ms(static_cast<size_t>(g)) == 29'000);
        CHECK(!p->death(0xB0100000, 25, t0, true).found);  // ya fuera
        // Nuestro propio Logout: se olvida sin muestra.
        CHECK(p->birth(0xB0200000, road::Side::Guest, 0x3FD, 3, t0));
        const road::ParityDeath ours = p->death(0xB0200000, 25, t0 + 1'000'000, false);
        const int w = p->find_class(road::Side::Guest, 0x3FD, 3);
        CHECK(ours.found && w >= 0 && p->c[w].deaths == 0 && p->c[w].ring_n == 0 && p->live_n == 0);
        // Mediana par: las dos centrales.
        const int hc = p->class_for(road::Side::Host, 0x1F5, 6);
        for (uint32_t i = 0; i < 4; ++i) {
            CHECK(p->birth(0xB0300000 + i, road::Side::Host, 0x1F5, 6, t0));
            p->death(0xB0300000 + i, 25, t0 + (10 + 10 * i) * 1'000'000ull, true);  // 10, 20, 30, 40 s
        }
        CHECK(hc >= 0 && p->median_ms(static_cast<size_t>(hc)) == 25'000);
        // Contraparte: misma carretera y tipo; si no, el mismo tipo con más vidas (caminantes 0x3FD / 0x3FE).
        bool same = false;
        CHECK(p->counterpart(static_cast<size_t>(g), &same) == hc && same);
        CHECK(p->counterpart(static_cast<size_t>(w), &same) == -1 && !same);
        const int hw = p->class_for(road::Side::Host, 0x3FE, 3);
        CHECK(p->counterpart(static_cast<size_t>(w), &same) == hw && !same);
        // Ventana nueva: las altas de la ventana a cero, los totales siguen.
        p->new_window(t0 + 120'000'000);
        CHECK(p->c[g].births_win == 0 && p->c[g].births == 80 && p->win_start_us == t0 + 120'000'000);
        // Olvidadas sin baja tras 20 min.
        CHECK(p->birth(0xB0400000, road::Side::Host, 0x1F5, 6, t0));
        CHECK(p->prune(t0 + road::kParityStaleUs - 1) == 0);
        CHECK(p->prune(t0 + road::kParityStaleUs) == 1 && p->c[hc].stale == 1 && p->live_n == 0);
        // Borrado con desplazamiento hacia atrás: tras borrar uno de cada tres, el resto se sigue encontrando.
        bool ins = true;
        for (uint32_t i = 0; i < 1000; ++i) ins = ins && p->birth(0xB0500000 + i * 2048, road::Side::Host, 0x1F5, 6, t0);
        CHECK(ins);
        bool del = true;
        for (uint32_t i = 0; i < 1000; i += 3) del = del && p->death(0xB0500000 + i * 2048, 25, t0 + 5, true).found;
        CHECK(del);
        bool all = true;
        for (uint32_t i = 0; i < 1000; ++i) all = all && ((p->find_live(0xB0500000 + i * 2048) >= 0) == (i % 3 != 0));
        CHECK(all);
        CHECK(p->live_n == 1000 - 334);
        // Tabla llena (3/4): contado; el alta cuenta igual para su clase.
        auto q = std::make_unique<road::Parity>();
        bool fill = true;
        for (uint32_t i = 0; i < road::kParityLive * 3 / 4; ++i) fill = fill && q->birth(0xB0000001 + i, road::Side::Guest, 1, 6, t0);
        CHECK(fill);
        CHECK(!q->birth(0xB00FFFFF, road::Side::Guest, 1, 6, t0) && q->live_full == 1);
        CHECK(q->c[q->find_class(road::Side::Guest, 1, 6)].births == road::kParityLive * 3 / 4 + 1);
        // Más clases que sitio: contado.
        auto r = std::make_unique<road::Parity>();
        for (uint32_t i = 0; i < road::kParityClasses; ++i) CHECK(r->birth(0xB0000001 + i, road::Side::Host, i, 6, t0));
        CHECK(!r->birth(0xB0000100, road::Side::Host, 999, 6, t0) && r->class_full == 1);
        // Veredicto: pocas muestras, suelo de 10 s, 1/3 del anfitrión.
        using road::ParityV;
        CHECK(road::parity_verdict(1'000, road::kParityMinSamples - 1, 29'000, 64) == ParityV::Few);
        CHECK(road::parity_verdict(9'999, road::kParityMinSamples, 0, 0) == ParityV::TooShort);
        CHECK(road::parity_verdict(29'300, 64, 28'800, 64) == ParityV::Ok);  // lo medido: paridad
        CHECK(road::parity_verdict(10'000, 64, 30'001, 64) == ParityV::ShortVsHost);
        CHECK(road::parity_verdict(10'000, 64, 30'000, 64) == ParityV::Ok);
        CHECK(road::parity_verdict(10'000, 64, 90'000, road::kParityMinSamples - 1) == ParityV::Ok);  // anfitrión sin muestras
        CHECK(std::strcmp(road::parity_name(ParityV::ShortVsHost), "?") != 0);
    }
    // (3) La rama del recolector (0x2C37340 caducidad +0x428, 0x28F9A20 radio +0x468).
    {
        using road::Gone;
        road::GoneIn g;
        CHECK(road::gone_branch(g) == Gone::Unknown);
        g.expiry_ok = true; g.expiry = 1000; g.now = 1001;
        CHECK(road::gone_branch(g) == Gone::Expired);
        g.expiry = 0;  // 0: sin caducidad
        g.pos_ok = true; g.radius_ok = true; g.radius = 80.0f;
        CHECK(road::gone_branch(g) == Gone::Unknown);  // ninguna distancia
        g.d_u2_ok = true; g.d_u2 = 85.0f;
        CHECK(road::gone_branch(g) == Gone::OutOfRadius);
        g.d_host_ok = true; g.d_host = 4000.0f;
        CHECK(road::gone_branch(g) == Gone::OutOfRadius);
        g.d_u2 = 79.9f;
        CHECK(road::gone_branch(g) == Gone::InsideAnchor);
        g.d_u2 = 80.0f;  // d >= r: fuera (0x28F9A20)
        CHECK(road::gone_branch(g) == Gone::OutOfRadius);
        g.expiry = 2000;  // aún no caduca
        CHECK(road::gone_branch(g) == Gone::OutOfRadius);
        g.radius = 0.0f;  // caja orientada / radio ilegible: desconocida
        CHECK(road::gone_branch(g) == Gone::Unknown);
        g.radius = 80.0f; g.pos_ok = false;
        CHECK(road::gone_branch(g) == Gone::Unknown);
        for (size_t i = 0; i < static_cast<size_t>(Gone::Count); ++i) {
            CHECK(std::strcmp(road::gone_name(static_cast<Gone>(i)), "?") != 0);
        }
    }
}
}  // namespace

int main() {
    review_fixes();
    root_truth_table();
    provenance_reader();
    caravan_scan();
    member_matrix();
    table_lifecycle();
    table_capacity_and_reuse();
    strict_cleanup();
    continuation_without_guest_troc();
    stops_and_checks();
    modes_and_guest_summary();
    separate_from_ue();
    carts_s40();
    carts_review();
    churn_hygiene();
    std::printf("S38 viajeros del invitado (fase C): %d fallos\n", failures);
    return failures ? 1 : 0;
}
