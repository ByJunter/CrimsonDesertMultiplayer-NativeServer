// S33: decisiones puras. No carga el mod ni el ejecutable del juego.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include "mod/session2/npc_activation_logic.h"
using namespace cdmp::session2::npcact;
namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)
Node node(uint32_t index = 0, uint64_t generation = 100) { return {1, 0x3F8972FE, index, 0x20000174, generation}; }
Actor actor(uint32_t key, uint64_t birth, uint32_t parent = 0) {
    Actor a; a.key = key; a.birth = birth; a.parent = parent; a.controller = 0x901001FB;
    a.born_us = 1; a.node = node(); a.assigned = parent == 0; a.type = parent ? 6 : 3;
    return a;
}
void modes() {
    CHECK(parse_mode("", true).mode == Mode::Off);
    CHECK(parse_mode("0", true).mode == Mode::Off);
    CHECK(parse_mode("probe", false).mode == Mode::Probe);
    CHECK(parse_mode("1", true).mode == Mode::On);
    CHECK(parse_mode("1", false).bad && parse_mode("1", false).mode == Mode::Off);
    CHECK(parse_mode("yes", true).bad);
}
void phases_and_identity() {
    Table<16, 8> t;
    t.born(actor(0xB01004C8, 1));
    auto* a = t.find(0xB01004C8);
    CHECK(a && !no_progress(*a, 120'000'000)); // alto retenido: no empezar reloj en nacimiento
    t.queued(a->key, 0x90100000, 10);
    CHECK(!a->queued_us); // otro controlador nunca inicia ese plazo
    CHECK(!t.next_probe_plan(1, 100));
    t.queued(a->key, a->controller, 100);
    CHECK(t.next_probe_plan(1, 100) == a); // plan seco antes del plazo, sin marcar retirado
    CHECK(!a->removed && !a->pending && !a->local_retired);
    CHECK(!no_progress(*a, 5'000'099));
    CHECK(no_progress(*a, 5'000'100));
    a->handler_us = 500; // retorno externo 0 NO demuestra aceptación
    a->first_req_us = 400;
    CHECK(no_progress(*a, 6'000'000));
    CHECK(!t.progress(a->key, 2, a->controller, true, 600));
    CHECK(!t.progress(a->key, 1, 0x90100000, true, 600));
    CHECK(!t.progress(a->key, 1, a->controller, false, 600));
    CHECK(no_progress(*a, 6'000'000));
    CHECK(t.progress(a->key, 1, a->controller, true, 600));
    CHECK(a->applied_us == 600 && !no_progress(*a, 6'000'000));
    t.born(actor(a->key, 2));
    a = t.find(0xB01004C8);
    CHECK(a->birth == 2 && !a->applied_us && !a->queued_us);
    CHECK(!t.progress(a->key, 1, a->controller, true, 700)); // resultado atrasado de otra encarnación
    t.born(actor(a->key, 1));
    CHECK(t.find(a->key)->birth == 2);
    a->removed = true;
    t.queued(a->key, a->controller, 1000);
    CHECK(!a->queued_us && !no_progress(*a, 10'000'000));
}
void families() {
    Table<128, 8> t;
    auto root = actor(0xB01004C8, 1); t.born(root);
    for (uint32_t i = 1; i <= 14; ++i) {
        auto child = actor(root.key + i, i + 1, root.key);
        child.controller = 0x90100000;
        t.born(child);
    }
    const auto f = t.family(root.key);
    CHECK(f.size == 15 && !f.overflow);
    CHECK(f.actors[0].key == root.key);
    for (size_t i = 1; i < f.size; ++i) CHECK(f.actors[i].controller == 0x90100000);
    t.born(actor(0xB0100800, 30));
    CHECK(!f.contains(0xB0100800));
    auto late = actor(0xB01004D7, 40, root.key);
    late.orphaned = true; late.assigned = false;
    CHECK(no_progress(late, 41)); // hijo tardío retirado sin esperar otra ventana ni consumir reintento
    late.pending = true;
    CHECK(!no_progress(late, 50));
    t.find(root.key)->removed = true;
    CHECK(t.family(root.key).size == 0);
    // Una familia grande se rechaza entera: jamás se retira sólo la raíz de una lista truncada.
    Table<128, 8> large;
    large.born(root);
    for (uint32_t i = 1; i <= 64; ++i) large.born(actor(root.key + i, i + 1, root.key));
    CHECK(large.family(root.key).overflow);
}
void bounded_retry_and_independence() {
    Table<16, 8> t;
    auto failed = actor(0xB01004C8, 1);
    auto good = actor(0xB0100800, 2); good.node.index = 1;
    t.born(failed); t.born(good);
    CHECK(!t.blocked(failed.node));
    CHECK(t.failed(failed.node));
    CHECK(!t.blocked(failed.node)); // un único reintento tras primer fallo
    CHECK(t.failed(failed.node));
    CHECK(t.blocked(failed.node));
    for (int i = 0; i < 100; ++i) CHECK(t.failed(failed.node));
    CHECK(t.budget(failed.node, false)->failures == 2);
    CHECK(!t.blocked(good.node) && !t.find(good.key)->removed); // no aborto global
    Node next_stage = failed.node; ++next_stage.stage;
    CHECK(!t.blocked(next_stage));
    Node next_generation = failed.node; ++next_generation.generation;
    CHECK(!t.blocked(next_generation));
    Node next_epoch = failed.node; ++next_epoch.epoch;
    CHECK(!t.blocked(next_epoch));
    Node unproven{};
    CHECK(!t.failed(unproven) && !t.blocked(unproven));
    Table<4, 1> full;
    CHECK(full.failed(node()));
    CHECK(!full.failed(node(1))); // sin hueco: no inventar un presupuesto ni escribir al juego
}
void local_retirement_and_retry() {
    Actor natural = actor(0xB0100001, 1); natural.removed = true;
    CHECK(!parent_requires_retirement(natural)); // una muerte natural no condena a hijos sanos
    natural.local_retired = true;
    CHECK(parent_requires_retirement(natural));
    Table<16, 8> t;
    Actor a = actor(0xB0100002, 2); a.local_retired = true; a.pending = true;
    t.born(a);
    CHECK(!observe_logout_entry(*t.find(a.key)));
    CHECK(t.find(a.key)->logout_seen && !t.find(a.key)->removed);
    CHECK(t.cleanup_result(a.key, a.birth, 4, 100));
    auto* pending = t.find(a.key);
    CHECK(!pending->pending && !pending->removed && pending->cleanup_failures == 1);
    CHECK(!no_progress(*pending, 1000));
    CHECK(no_progress(*pending, 1'000'100));
    CHECK(t.cleanup_result(a.key, a.birth, 2, 1'000'100));
    CHECK(t.cleanup_result(a.key, a.birth, 4, 2'000'100));
    CHECK(pending->cleanup_failures == 3 && !pending->removed);
    CHECK(!no_progress(*pending, 100'000'000)); // fallo explícito local, jamás bucle infinito
    CHECK(!t.cleanup_result(a.key, a.birth + 1, 0, 100'000'001));
    CHECK(!pending->removed); // resultado de otra generación no lo borra
    t.born(actor(a.key, a.birth + 1));
    CHECK(!t.find(a.key)->local_retired && t.find(a.key)->cleanup_failures == 0);
}
void family_member_retry_is_individually_bounded() {
    Table<16, 8> t;
    Actor parent = actor(0xB0100200, 1); parent.local_retired = true;
    Actor child = actor(0xB0100201, 2, parent.key); child.local_retired = true;
    t.born(parent); t.born(child);
    unsigned parent_calls = 0, child_calls = 0;
    auto fail = [&](uint32_t key, uint64_t birth, uint64_t now, unsigned& calls) {
        auto* current = t.find(key);
        if (!current || !may_cleanup_member(*current, key, birth, now)) return;
        ++calls;
        CHECK(t.cleanup_result(key, birth, 4, now));
    };
    // El hijo agota su presupuesto por sí mismo; el padre apenas ha fallado una vez.
    fail(parent.key, parent.birth, 100, parent_calls);
    fail(child.key, child.birth, 100, child_calls);
    fail(child.key, child.birth, 1'000'100, child_calls);
    fail(child.key, child.birth, 2'000'100, child_calls);
    CHECK(parent_calls == 1 && child_calls == 3);
    // Aparece repetidamente dentro de planes nuevos de su padre. No recupera intentos.
    for (uint64_t round = 3; round < 12; ++round) {
        const auto family = t.family(parent.key);
        for (size_t i = family.size; i > 0; --i) {
            const auto& member = family.actors[i - 1];
            fail(member.key, member.birth, round * 1'000'000 + 100,
                 member.key == child.key ? child_calls : parent_calls);
        }
    }
    CHECK(parent_calls == 3 && child_calls == 3);
    CHECK(!t.find(parent.key)->removed && !t.find(child.key)->removed);
    // También respeta la espera, la generación vigente y una baja ya confirmada.
    t.born(actor(child.key, 3, parent.key));
    auto* fresh = t.find(child.key); fresh->local_retired = true;
    CHECK(!may_cleanup_member(*fresh, child.key, 2, 100'000'000));
    CHECK(may_cleanup_member(*fresh, child.key, 3, 100));
    CHECK(t.cleanup_result(child.key, 3, 4, 100));
    CHECK(!may_cleanup_member(*fresh, child.key, 3, 500));
    CHECK(t.cleanup_result(child.key, 3, 0, 1'000'100));
    CHECK(!may_cleanup_member(*fresh, child.key, 3, 100'000'000));
}
void refused_actor_does_not_starve_others() {
    Table<16, 8> t;
    auto slow1 = actor(0xB0100001, 1); slow1.queued_us = 1;
    auto slow2 = actor(0xB0100002, 2); slow2.queued_us = 1;
    t.born(slow1); t.born(slow2);
    auto* first = t.next_due(6'000'000, 1, 100);
    CHECK(first != nullptr);
    const uint32_t rejected = first ? first->key : 0;
    // El primer actor queda vivo (p. ej. su stage no se pudo demostrar).
    auto* second = t.next_due(6'000'000, 1, 100);
    CHECK(second && second->key != rejected);
    CHECK(!t.next_due(6'000'000, 2, 100)); // otra conexión
    CHECK(!t.next_due(6'000'000, 1, 101)); // otro armado
    t.find(slow1.key)->orphaned = true;
    CHECK(t.next_due(6'000'000, 1, 101) == t.find(slow1.key)); // el huérfano sigue pendiente al cambiar armado
}
void capacity_and_reuse() {
    Table<4, 2> t;
    for (uint32_t i = 1; i <= 4; ++i) CHECK(t.born(actor(i, i)) != nullptr);
    CHECK(!t.born(actor(5, 5)) && t.overflow == 1);
    t.find(2)->removed = true;   // baja natural: el hilo del módulo la borra
    CHECK(t.prune(1, 10) == 1);
    CHECK(t.born(actor(5, 5)) != nullptr);
    CHECK(t.find(5) && t.find(1) && t.find(3) && t.find(4));
    CHECK(!t.find(2));
}
template <size_t N, size_t B> size_t probes(const Table<N, B>& t, uint32_t key) {
    size_t n = 1;
    for (size_t at = Table<N, B>::home(key); t.keys[at] && t.keys[at] != key && n <= N; at = (at + 1) % N) ++n;
    return n;
}
// Revisión: miles de nacimientos/bajas no dejan lápidas de sondeo; una clave ajena acaba en el primer hueco.
void real_deletion_keeps_lookups_short() {
    Table<64, 8> t;
    for (uint32_t round = 0; round < 5000; ++round) {
        CHECK(t.born(actor(0xB0100000 + round, round + 1)) != nullptr);
        if (round >= 16) CHECK(t.erase(0xB0100000 + round - 16));
    }
    CHECK(t.count == 16 && t.overflow == 0);
    for (uint32_t round = 5000 - 16; round < 5000; ++round) CHECK(t.find(0xB0100000 + round) != nullptr);
    CHECK(!t.find(0xB0100000));
    size_t worst = 0;
    for (uint32_t miss = 0xA0000000; miss < 0xA0000000 + 1000; ++miss) worst = std::max(worst, probes(t, miss));
    CHECK(worst <= 17); // 16 ocupados de 64: nunca un barrido de la tabla
    // Racimo de colisiones con vuelta al principio: borrar en medio conserva las demás.
    Table<8, 2> c;
    std::vector<uint32_t> same;
    for (uint32_t k = 0xB0000001; same.size() < 5; ++k) if (Table<8, 2>::home(k) == 6) same.push_back(k);
    for (size_t i = 0; i < same.size(); ++i) CHECK(c.born(actor(same[i], i + 1)) != nullptr);
    CHECK(c.erase(same[1]) && c.erase(same[0]));
    for (size_t i = 2; i < same.size(); ++i) CHECK(c.find(same[i]) && c.find(same[i])->key == same[i]);
    CHECK(!c.find(same[0]) && !c.find(same[1]) && c.count == 3);
}
void prune_and_tombstones() {
    Table<64, 8> t;
    Actor natural = actor(0xB0100001, 1); natural.removed = true;
    Actor retired = actor(0xB0100002, 2); retired.removed = true; retired.local_retired = true;
    Actor pending = actor(0xB0100003, 3); pending.removed = true; pending.pending = true;
    Actor other_epoch = actor(0xB0100004, 4); other_epoch.node.epoch = 2;
    Actor alive = actor(0xB0100005, 5);
    for (const auto& a : {natural, retired, pending, other_epoch, alive}) t.born(a);
    CHECK(t.prune(1, 10) == 2);
    CHECK(!t.find(natural.key) && !t.find(other_epoch.key));
    CHECK(t.find(retired.key) && t.find(pending.key) && t.find(alive.key)); // lápida para hijos tardíos
    CHECK(t.prune(1, kStaleUs + 10) == 1 && !t.find(retired.key)); // lápida vieja
    CHECK(t.find(alive.key)); // vivo y viejo: sólo si la tabla se llena
}
void refusals_are_bounded() {
    Table<16, 8> t;
    Actor a = actor(0xB0100010, 1); a.queued_us = 1; a.pending = true;
    t.born(a);
    CHECK(!t.refused(a.key, a.birth) && !t.find(a.key)->pending);
    CHECK(no_progress(*t.find(a.key), 6'000'000));
    CHECK(!t.refused(a.key, 2)); // otra encarnación no cuenta
    CHECK(!t.refused(a.key, a.birth));
    CHECK(t.refused(a.key, a.birth)); // el que agota: una sola vez
    CHECK(t.find(a.key)->gave_up && !no_progress(*t.find(a.key), 60'000'000));
    CHECK(!t.refused(a.key, a.birth));
    CHECK(!t.next_due(60'000'000, 1, 100)); // abandonada: nunca vuelve a la cola de retirada
}
void decisions() {
    // Logout: "ya se iba" es baja en todos los modos; otro código sólo falla en la retirada local.
    constexpr uint32_t already = 0x1234ABCD;
    CHECK(logout_result(0, already, false) == kCleanDone && logout_result(0, already, true) == kCleanDone);
    CHECK(logout_result(already, already, false) == kCleanGone && logout_result(already, already, true) == kCleanGone);
    CHECK(logout_result(7, already, false) == kCleanDone); // c7ae132 en modos 0/probe
    CHECK(logout_result(7, already, true) == kCleanFailed);
    CHECK(logout_result(7, 0, true) == kCleanFailed); // código global ilegible: nunca "ya se iba"
    // Rama payload+0x281: acción ficticia del secuenciador aplicada por el controlador.
    CHECK(action_progress(true, 0, 5));
    CHECK(action_progress(false, 1, 0));
    CHECK(!action_progress(false, 1, 3));    // controlador rechazado
    CHECK(!action_progress(false, 0, 0));    // retorno 0 sin OnServer ni rama: no
    CHECK(!action_progress(false, 0xFF, 0)); // byte ilegible
    // La contención sustituye al aborto antiguo sólo lista; si falta una pieza o falla, vuelve la red antigua.
    Readiness r{true, true, true, true, true, false};
    CHECK(containment_ready(r));
    for (int piece = 0; piece < 6; ++piece) {
        Readiness x = r;
        bool* fields[] = {&x.activation_on, &x.action_probe, &x.node_probe, &x.cleanup_fns, &x.ctl_on, &x.fault};
        *fields[piece] = !*fields[piece];
        CHECK(!containment_ready(x));
    }
    // Sitios de 0x281CEE0: veto exacto; grupo nativo (sólo entero agotado); padre nativo y desconocido nunca.
    CHECK(veto_site(0x281994B) == VetoSite::Exact && veto_site(0x281A341) == VetoSite::Exact);
    CHECK(veto_site(0x281A958) == VetoSite::Exact && veto_site(0x281DD65) == VetoSite::Exact);
    CHECK(veto_site(0x281A4B5) == VetoSite::Group && veto_site(0x281A589) == VetoSite::Group);
    CHECK(veto_site(0x28210B5) == VetoSite::Parent && veto_site(0x281DB05) == VetoSite::Parent);
    CHECK(veto_site(0) == VetoSite::Unknown && veto_site(0x281A33C) == VetoSite::Unknown);
    static_assert(sizeof(Family) <= 2048, "la familia va en la pila del servidor");
}
// Prueba en un PC (14-10, 15:42): un abandono es por unidad. La raíz y su familia (nacimientos exactos) salen de la
// contención; otras unidades siguen; nada de esto cambia containment_ready (la red global).
void abandonment_is_per_unit() {
    Table<64, 8> t;
    Actor root = actor(0xB01008E1, 10); root.queued_us = 1;
    Actor kid = actor(0xB01008E2, 11, root.key);
    Actor other = actor(0xB01008F0, 12); other.queued_us = 1; other.node = node(4);
    for (const auto& a : {root, kid, other}) t.born(a);
    Readiness r{true, true, true, true, true, false};
    CHECK(containment_ready(r));
    Family f;
    t.abandon(root.key, 9, f);  // otra encarnación: nada
    CHECK(f.size == 0 && !t.find(root.key)->gave_up);
    t.abandon(root.key, root.birth, f);
    CHECK(f.size == 2 && f.contains(kid.key));
    CHECK(t.find(root.key)->gave_up && t.find(kid.key)->gave_up && !t.find(other.key)->gave_up);
    CHECK(!no_progress(*t.find(root.key), 60'000'000));                 // nunca se retira otra vez
    CHECK(!t.find(root.key)->removed && !t.find(root.key)->local_retired); // sigue viva con su controlador
    CHECK(t.next_due(60'000'000, 1, 100) == t.find(other.key));         // las demás unidades siguen
    CHECK(containment_ready(r));                                        // Readiness no tiene entrada de abandono
    // Una nueva encarnación de la misma clave tiene su propio presupuesto de abandono.
    Actor again = root; again.birth = 20;
    t.born(again);
    CHECK(!t.find(root.key)->gave_up);
    // Cota de cordura: un WRN una sola vez al pasarla, nunca un aborto.
    size_t warns = 0;
    for (uint64_t n = 1; n <= 3 * kAbandonWarn; ++n) warns += abandon_warn(n) ? 1 : 0;
    CHECK(warns == 1 && !abandon_warn(kAbandonWarn) && abandon_warn(kAbandonWarn + 1));
    // Sólo una contención inutilizable devuelve la red antigua (fallo nativo / escrituras detenidas, sonda sin verificar).
    Readiness broken = r; broken.fault = true;
    CHECK(!containment_ready(broken));
    broken = r; broken.node_probe = false;
    CHECK(!containment_ready(broken));
}
// Memoria sintética para read_group: un bloque con dirección base propia.
struct Mem {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(4096, 0);
    uintptr_t base() const { return reinterpret_cast<uintptr_t>(bytes.data()); }
    template <class T> void put(uintptr_t at, T v) { std::memcpy(bytes.data() + (at - base()), &v, sizeof v); }
    bool read(uintptr_t at, void* out, size_t n) const {
        if (at < base() || at + n > base() + bytes.size()) return false;
        std::memcpy(out, bytes.data() + (at - base()), n);
        return true;
    }
};
// El grupo nativo de 0x2819E00 modo 3: líder + miembros en [rsp+0x88]; veto sólo con TODOS sus nodos agotados.
void group_veto_only_when_whole_group_exhausted() {
    Mem m;
    const uintptr_t rsp = m.base(), group = m.base() + 0x200, vec = m.base() + 0x300, entries = m.base() + 0x400,
                    nodes = m.base() + 0x600;
    const uint32_t idx[] = {56, 60, 64};
    m.put<uintptr_t>(rsp + kGroupFrameSlot, group);
    m.put<uintptr_t>(group + 0x08, vec);
    m.put<uint32_t>(group + 0x10, 2);
    m.put<uintptr_t>(group + 0x18, entries);  // líder = entrada 0
    for (uint32_t i = 0; i < 3; ++i) {
        m.put<uintptr_t>(vec + 8 * i, entries + 0x38 * i);
        m.put<uintptr_t>(entries + 0x38 * i, nodes + 0x168 * i);
        m.put<uint32_t>(nodes + 0x168 * i, 0x6FF7E5A6);
        m.put<uint32_t>(nodes + 0x168 * i + 4, idx[i]);
    }
    const auto rd = [&m](uintptr_t at, void* out, size_t n) { return m.read(at, out, n); };
    Group g;
    CHECK(read_group(rd, rsp, kGroupLeaderRet, nodes, g) && g.size == 2);
    CHECK(g.nodes[0].index == 56 && g.nodes[1].index == 60);
    CHECK(read_group(rd, rsp, kGroupMemberRet, nodes + 0x168, g) && g.size == 2);
    // Papel no demostrado: el líder en el sitio de miembro, un nodo ajeno, otro sitio, marco o grupo ilegibles.
    CHECK(!read_group(rd, rsp, kGroupMemberRet, nodes, g) && g.size == 0);
    CHECK(!read_group(rd, rsp, kGroupLeaderRet, nodes + 0x168, g));
    CHECK(!read_group(rd, rsp, kGroupLeaderRet, nodes + 0x168 * 2, g));
    CHECK(!read_group(rd, rsp, 0x28210B5, nodes, g) && !read_group(rd, rsp, 0x281994B, nodes, g));
    CHECK(!read_group(rd, 0, kGroupLeaderRet, nodes, g));
    CHECK(!read_group(rd, m.base() + 8000, kGroupLeaderRet, nodes, g));
    m.put<uint32_t>(group + 0x10, kMaxGroup + 1);
    CHECK(!read_group(rd, rsp, kGroupLeaderRet, nodes, g));  // más de 64: no se demuestra el grupo entero
    m.put<uint32_t>(group + 0x10, 2);
    m.put<uintptr_t>(group + 0x18, entries + 0x38 * 2);       // líder fuera del vector
    CHECK(!read_group(rd, rsp, kGroupLeaderRet, nodes + 0x168 * 2, g));
    m.put<uintptr_t>(group + 0x18, entries);
    m.put<uintptr_t>(vec + 8, m.base() + 9000);               // entrada ilegible: nada a medias
    CHECK(!read_group(rd, rsp, kGroupLeaderRet, nodes, g) && g.size == 0);
    m.put<uintptr_t>(vec + 8, entries + 0x38);
    // Presupuestos: el líder agotado y el miembro con un solo fallo -> no se veta; con ambos agotados -> sí.
    Table<16, 16> t;
    const Node base{2, 0x6FF7E5A6, 56, 0x200002D4, 100};
    Node member = base; member.index = 60;
    CHECK(read_group(rd, rsp, kGroupLeaderRet, nodes, g));
    t.failed(base); t.failed(base);
    t.failed(member);
    CHECK(t.blocked(base) && t.blocked_in(base, g) == 1);  // grupo con un nodo vivo: omitir
    t.failed(member);
    CHECK(t.blocked_in(base, g) == g.size);                 // todos agotados: veto exacto
    Node other_gen = base; other_gen.generation = 101;
    CHECK(t.blocked_in(other_gen, g) == 0);                 // otra generación: presupuesto propio
    Node other_stage = base; other_stage.stage = 0x200002E5;
    CHECK(t.blocked_in(other_stage, g) == 0);               // otra stage, misma definición: nunca
    // El caso de 0x200002E5 nodo 0: grupo de un solo nodo (sus sub-nodos 4/8/12 cuelgan de él por 0x2820D80).
    m.put<uint32_t>(group + 0x10, 1);
    CHECK(read_group(rd, rsp, kGroupLeaderRet, nodes, g) && g.size == 1 && t.blocked_in(base, g) == 1);
    Group empty;
    CHECK(t.blocked_in(base, empty) == 0);                  // grupo no leído: nunca "todos agotados"
}
void caches_and_budgets() {
    SeenCache<4> seen;
    CHECK(seen.insert(node(1)) && !seen.insert(node(1)));
    for (uint32_t i = 2; i < 200; ++i) seen.insert(node(i));
    size_t again = 0;
    for (uint32_t i = 2; i < 200; ++i) again += seen.insert(node(i)) ? 1 : 0;
    CHECK(again > 0); // acotada: puede repetir líneas, nunca crece
    KeyCache<16> keys;
    CHECK(keys.insert(0xB0100450) && !keys.insert(0xB0100450));
    // Presupuestos: cuando se llena, los de otra generación dejan sitio; los vigentes no se pierden.
    Table<4, 8> t;
    for (uint32_t i = 0; i < 6; ++i) CHECK(t.failed(node(i, 100)));
    CHECK(!t.failed(node(6, 100))); // lleno con la generación vigente
    for (uint32_t i = 0; i < 6; ++i) CHECK(t.budget(node(i, 100), false) != nullptr);
    CHECK(t.failed(node(0, 101)) && t.budget_count == 1); // nuevo armado: los viejos se borran
    CHECK(!t.budget(node(1, 100), false) && t.budget(node(0, 101), false));
}
// ---------------------------------------------------------------- S35 fase A (docs/research/s25/s35-fase-a.md)
// Fixtures from the S34 runs (AUDITORIA.md §4, INFORME-A4): every one of the 843 OnServer lines had sender 0
// (param4+0x90 -> +0x60 is not a session), applied 1 and progress 0. C B0100391: queued, first Req +1.671541 s,
// H1b +1.705458 s, diagnosis +5.035230 s, retirement +5.056492 s (CH 7896/10055/10090/10903-10906).
constexpr uint32_t kGuest = 0x901001FB, kHost = 0x90100000;
ActionCall h1b_call(uint64_t birth, uint32_t ctl = kGuest) {
    ActionCall c;
    c.birth = birth; c.h1b = true; c.branch_flag = 0; c.internal = 0; c.ctl_entry = ctl; c.ctl_exit = ctl;
    return c;
}
void s35_fixture_c_b0100391() {
    Table<16, 8> t;
    Actor a = actor(0xB0100391, 7); a.controller = kGuest; t.born(a);
    auto* r = t.find(a.key);
    const uint64_t q = 68'000'000'000;  // queued to the guest
    t.queued(r->key, kGuest, q);
    CHECK(t.guest_req(r->key, q + 1'671'541));
    // S34: the action reached H1b, but the misread sender (0) never matched: no progress, a false retirement.
    CHECK(!t.progress(r->key, 7, /*sender=*/0, action_progress(true, 0, 0), q + 1'705'458));
    CHECK(no_progress(*r, q + 5'035'230));
    // S35 H1b: the same call counts (native, authorized by actor+0x90 == its controller, applied, same birth).
    CHECK(t.record(r->key, h1b_call(7), q + 1'705'458) == Evidence::Applied && r->applied_us == q + 1'705'458);
    Policy p; p.h1b = true;
    Context c; c.now = q + 5'035'230; c.last_guest_req_us = c.now;
    CHECK(containment_due(*r, p, c) == Due::No && !no_progress(*r, c.now));
}
void s35_fixture_a_slow_applications() {
    // A: 22 native applications > 5 s (5.080987..5.634932 s, AH 12458..12523): a 5 s deadline retires them.
    Table<16, 8> t;
    Actor a = actor(0xB0100500, 3); a.controller = kGuest; t.born(a);
    auto* r = t.find(a.key);
    t.queued(r->key, kGuest, 1000);
    Policy s34;  // the S34 defaults: guard off, 5 s
    Context c; c.now = 1000 + 5'080'987;
    CHECK(containment_due(*r, s34, c) == Due::Retire);
    Policy p; p.guard = true; p.h1b = true; p.no_progress_us = 8'000'000; p.silence_us = 2'000'000;
    c.last_guest_req_us = c.now - 100'000;
    CHECK(containment_due(*r, p, c) == Due::No);  // 8 s: still inside the experimental deadline
    CHECK(t.record(r->key, h1b_call(3), 1000 + 5'634'932) == Evidence::Applied);
    c.now = 1000 + 9'000'000;
    CHECK(containment_due(*r, p, c) == Due::No);
}
void s35_fixture_b_not_evidence() {
    // B-like (223 lines, max 3.34 s): a return 0 alone, a rejected owner, an old birth and an unknown read never count.
    Table<16, 8> t;
    Actor a = actor(0xB0100600, 5); a.controller = kGuest; t.born(a);
    auto* r = t.find(a.key);
    ActionCall zero = h1b_call(5); zero.h1b = false;  // internal 0, no H1b, no branch 281
    CHECK(t.record(r->key, zero, 10) == Evidence::ReturnZeroOnly && !r->applied_us && !r->activated_us);
    ActionCall other = h1b_call(5, kHost);  // actor+0x90 is the host's: not this unit's controller
    CHECK(t.record(r->key, other, 11) == Evidence::OtherController && !r->applied_us);
    ActionCall changed = h1b_call(5); changed.h1b = false; changed.ctl_exit = kHost;  // no H1b, controller changed
    CHECK(t.record(r->key, changed, 12) == Evidence::OtherController && !r->applied_us);
    ActionCall unread_entry_h1b = h1b_call(5); unread_entry_h1b.ctl_entry = 0;  // H1b, but entry unreadable: never
    CHECK(t.record(r->key, unread_entry_h1b, 12) == Evidence::Unreadable && !r->applied_us);
    CHECK(t.record(r->key, h1b_call(4), 13) == Evidence::OldBirth && !r->applied_us);  // another incarnation
    ActionCall unread = h1b_call(5); unread.ctl_entry = 0;
    CHECK(t.record(r->key, unread, 14) == Evidence::Unreadable && !r->applied_us);
    ActionCall noresult = zero; noresult.internal = kInternalUnreadable;
    CHECK(t.record(r->key, noresult, 15) == Evidence::Unreadable);
    ActionCall rejected = zero; rejected.internal = 0x1D;  // e.g. NotControlledBySession
    const uint16_t v0 = r->version;
    CHECK(t.record(r->key, rejected, 16) == Evidence::Rejected && r->rejections == 1 && r->version == v0 + 1);
    CHECK(t.record(r->key, rejected, 17) == Evidence::Rejected && r->rejections == 2 && r->version == v0 + 1);
    // Retired / removed units: nothing counts.
    r->local_retired = true;
    CHECK(t.record(r->key, h1b_call(5), 18) == Evidence::Retired && !r->applied_us);
    r->local_retired = false; r->removed = true;
    CHECK(t.record(r->key, h1b_call(5), 19) == Evidence::Retired && !r->applied_us);
}
void s35_h1b_exit_read_fails() {
    // Review finding 1: an applied H1b whose actor+0x90 EXIT read fails (0) or changed is still Applied (the native
    // comparison used the entry value before H1b); the discrepancy is flagged, never a false retirement.
    for (const uint32_t exit_ctl : {0u, kHost}) {
        Table<16, 8> t;
        Actor a = actor(0xB0100610, 6); a.controller = kGuest; t.born(a);
        t.queued(a.key, kGuest, 1);
        ActionCall c = h1b_call(6); c.ctl_exit = exit_ctl;
        CHECK(exit_mismatch(c));
        CHECK(t.record(a.key, c, 100) == Evidence::Applied && t.find(a.key)->applied_us == 100);
        CHECK(!no_progress(*t.find(a.key), 60'000'000));
    }
    CHECK(!exit_mismatch(h1b_call(6)));
    ActionCall other = h1b_call(6, kHost);  // H1b but the entry controller is another user's: not this unit's
    Actor b = actor(0xB0100611, 6); b.controller = kGuest;
    CHECK(classify(b, other) == Evidence::OtherController);
}
// Review finding 5 (AUDITORÍA §6.5): the detour's reads through a read policy, on a simulated frame.
struct Frame {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x2000, 0);
    uintptr_t base() const { return reinterpret_cast<uintptr_t>(bytes.data()); }
    template <class T> void put(uintptr_t at, T v) { std::memcpy(bytes.data() + (at - base()), &v, sizeof v); }
    bool read(uintptr_t at, void* out, size_t n) const {
        if (at < base() || at + n > base() + bytes.size()) return false;
        std::memcpy(out, bytes.data() + (at - base()), n);
        return true;
    }
    uintptr_t cc() const { return base() + 0x100; }
    uintptr_t actor() const { return base() + 0x400; }
    uintptr_t state() const { return base() + 0x700; }
    uintptr_t block() const { return base() + 0x800; }
    uintptr_t context() const { return base() + 0xC00; }
    uintptr_t result() const { return base() + 0xF00; }
    Frame(uint32_t key, uint32_t ctl, uint8_t flag, int32_t internal, int8_t restore) {
        put<uintptr_t>(cc() + 0x08, actor());
        put<uint32_t>(actor() + 0x60, key);
        put<uint32_t>(actor() + 0x90, ctl);
        put<uintptr_t>(cc() + 0x40, state());
        put<int8_t>(state() + 0x30, restore);
        put<uint8_t>(block() + 0x281, flag);
        put<int32_t>(result(), internal);
        // param4 is NOT a session: its +0x90 holds some other pointer (here 0: the S34 read gives 0, as in 843/843).
    }
};
void s35_detour_reads_on_a_frame() {
    const uint32_t key = 0xB0100391;
    Frame f(key, kGuest, 0, 0, 0);
    const auto rd = [&f](uintptr_t at, void* out, size_t n) { return f.read(at, out, n); };
    ActionEntry e = read_action_key(rd, f.cc());
    CHECK(e.actor == f.actor() && e.key == key);
    read_action_entry(rd, f.cc(), f.context(), f.block(), e);
    CHECK(e.flag == 0 && e.ctl == kGuest && e.restore == 0 && e.s34_sender == 0);
    f.put<int8_t>(f.state() + 0x30, 1);  // OnServer applied its first action
    ActionExit x = read_action_exit(rd, f.cc(), e, f.result());
    CHECK(x.same && x.ctl == kGuest && x.internal == 0 && x.restore == 1);
    Table<16, 8> t;
    Actor a = actor(key, 7); a.controller = kGuest; t.born(a);
    CHECK(t.record(key, make_call(e, x, /*h1b=*/true, 7), 10) == Evidence::Applied);  // sender 0 is irrelevant now
    // Same frame, no H1b: return 0 alone never counts.
    Actor b = actor(key + 1, 8); b.controller = kGuest; t.born(b);
    CHECK(t.record(key + 1, make_call(e, x, false, 8), 10) == Evidence::ReturnZeroOnly);
    // Branch 281 on the frame.
    f.put<uint8_t>(f.block() + 0x281, 1);
    read_action_entry(rd, f.cc(), f.context(), f.block(), e);
    CHECK(e.flag == 1 && t.record(key + 1, make_call(e, read_action_exit(rd, f.cc(), e, f.result()), false, 8), 11) ==
                             Evidence::Branch281);
    // The cc now holds another actor (a key reused under the same component): the result belongs to nobody.
    f.put<uintptr_t>(f.cc() + 0x08, f.actor() + 0x100);
    CHECK(!read_action_exit(rd, f.cc(), e, f.result()).same);
    f.put<uintptr_t>(f.cc() + 0x08, f.actor());
    // Unreadable result pointer, unreadable block, unreadable restore state: fallbacks, never guesses.
    CHECK(read_action_exit(rd, f.cc(), e, 0).internal == kInternalUnreadable);
    ActionEntry u = read_action_key(rd, f.cc());
    read_action_entry(rd, f.cc(), f.context(), 0, u);
    CHECK(u.flag == 0xFF);
    f.put<uintptr_t>(f.cc() + 0x40, uintptr_t{0x1234});
    CHECK(restore_with(rd, f.cc()) == -1);
    CHECK(read_action_key(rd, 0).key == 0 && read_action_key(rd, 0).actor == 0);
    // The S34 read with a context whose +0x90 points at a "user" with +0x60 = something: kept only for the line.
    f.put<uintptr_t>(f.context() + 0x90, f.base() + 0x1800);
    f.put<uint32_t>(f.base() + 0x1800 + 0x60, 0x12345678u);
    read_action_entry(rd, f.cc(), f.context(), f.block(), u);
    CHECK(u.s34_sender == 0x12345678u);
}
void s35_postpone_is_bounded() {
    // Review finding 2: silence / load postponement has a total bound from the original deadline; the clock restart
    // never extends it.
    Table<16, 8> t;
    Actor a = actor(0xB0100830, 4); a.controller = kGuest; t.born(a);
    auto* r = t.find(a.key);
    t.queued(r->key, kGuest, 1'000'000);
    Policy p; p.guard = true; p.h1b = true; p.no_progress_us = 8'000'000; p.silence_us = 2'000'000;
    p.max_postpone_us = 20'000'000;
    Context c; c.last_guest_req_us = 0;  // silent forever
    uint64_t retired_at = 0;
    for (uint64_t now = 9'000'000; now < 200'000'000 && !retired_at; now += 500'000) {
        c.now = now;
        const Due d = containment_due(*r, p, c);
        if (d == Due::PostponeSilence) postpone(*r, d, now);
        else if (d == Due::Retire) retired_at = now;
    }
    CHECK(retired_at >= 1'000'000 + 8'000'000 + 20'000'000 && retired_at < 1'000'000 + 8'000'000 + 20'000'000 + 9'000'000);
    CHECK(postpone_exhausted(*r, p, retired_at) && r->postpones >= 2);
    p.max_postpone_us = 0;  // no bound (not recommended): postponed again
    c.now = retired_at;
    CHECK(containment_due(*r, p, c) != Due::Retire);
    // Loading is bounded the same way.
    Actor b = actor(0xB0100831, 5); b.controller = kGuest; b.queued_us = 1'000'000; b.assigned = true;
    p.max_postpone_us = 20'000'000;
    Context l; l.guest_loading = true; l.last_guest_req_us = 1; l.now = 1'000'000 + 28'000'000;
    CHECK(containment_due(b, p, l) == Due::Retire);
    l.now = 1'000'000 + 27'000'000;
    CHECK(containment_due(b, p, l) == Due::PostponeLoading);
}
void s35_execution_decisions() {
    // Review finding 5: the execution step of npc_activation_cleanup (cancellation without refusal) as pure logic.
    using R = Recheck;
    CHECK(execution_decision(false, R::Valid, R::Valid, true).exec == Exec::Proceed);
    CHECK(execution_decision(true, R::Valid, R::Valid, true).exec == Exec::Refuse);   // a member is gone (S33)
    CHECK(execution_decision(false, R::Gone, R::Valid, true).exec == Exec::Refuse);
    for (R r : {R::Progress, R::NewEvidence, R::Postpone}) {
        const auto d = execution_decision(false, r, R::Valid, true);
        CHECK(d.exec == Exec::Cancel && d.why == r);
    }
    CHECK(execution_decision(false, R::Valid, R::NewEvidence, true).exec == Exec::Cancel);  // a member changed
    // guard=0: a progress between selection and execution is a refusal, counted as "progreso" (not "ya no está").
    const auto s34 = execution_decision(false, R::Progress, R::Valid, false);
    CHECK(s34.exec == Exec::Refuse && s34.why == R::Progress);
    CHECK(execution_decision(false, R::Valid, R::Valid, false).exec == Exec::Proceed);
    // The cancellation: reservation released, retried later, no refusal, postponed when silence says so.
    Table<16, 8> t;
    Actor a = actor(0xB0100840, 4); a.controller = kGuest; a.queued_us = 1; a.pending = true; t.born(a);
    auto* r = t.find(a.key);
    Policy p; p.guard = true; p.h1b = true; p.no_progress_us = 8'000'000; p.silence_us = 2'000'000;
    Context c; c.now = 10'000'000; c.last_guest_req_us = 0;
    CHECK(cancel_execution(*r, p, c) == Due::PostponeSilence);
    CHECK(!r->pending && r->refusals == 0 && r->postpones == 1 && r->retry_after_us >= c.now + kCleanupRetryUs);
    CHECK(containment_due(*r, p, c) == Due::No);  // not before its retry
    Actor b = actor(0xB0100841, 4); b.controller = kGuest; b.queued_us = 1; b.pending = true; t.born(b);
    c.last_guest_req_us = c.now;
    CHECK(cancel_execution(*t.find(b.key), p, c) == Due::Retire && t.find(b.key)->postpones == 0 &&
          !t.find(b.key)->pending && t.find(b.key)->retry_after_us == c.now + kCleanupRetryUs);
}
void s35_branch_281() {
    // payload+0x281 (0x2a78ca0.c:45-49): activation evidence only, after the native controller check.
    Table<16, 8> t;
    Actor a = actor(0xB0100700, 2); a.controller = kGuest; t.born(a);
    auto* r = t.find(a.key);
    ActionCall b = h1b_call(2); b.h1b = false; b.branch_flag = 1;
    CHECK(t.record(r->key, b, 50) == Evidence::Branch281 && r->activated_us == 50 && !r->applied_us);
    Policy off; Policy on; on.h1b = true;
    CHECK(!has_progress(*r, off) && has_progress(*r, on));  // counts as activation only with the H1b evidence
    ActionCall unreadable = b; unreadable.branch_flag = 0xFF; unreadable.birth = 3;
    Actor fresh = actor(0xB0100701, 3); fresh.controller = kGuest; t.born(fresh);
    CHECK(t.record(fresh.key, unreadable, 60) == Evidence::ReturnZeroOnly && !t.find(fresh.key)->activated_us);
    ActionCall refused = b; refused.internal = 7; refused.birth = 3;  // flag set but the call failed (e.g. not its controller)
    CHECK(t.record(fresh.key, refused, 61) == Evidence::Rejected && !t.find(fresh.key)->activated_us);
}
void s35_tls_reentry() {
    // The TLS stack of 0x2A78CA0 calls: H1b marks only the innermost, and only with its component and key.
    PendingCall outer{0x1000, 0xB0100001, false, nullptr};
    PendingCall inner{0x2000, 0xB0100002, false, &outer};
    PendingCall* top = &inner;
    CHECK(!mark_applied(top, 0x1000, 0xB0100001) && !outer.applied);  // the outer's H1b inside the inner: no
    CHECK(mark_applied(top, 0x2000, 0xB0100002) && inner.applied && !outer.applied);
    top = inner.previous;  // the inner returns
    CHECK(!mark_applied(top, 0x2000, 0xB0100002) && !outer.applied);  // a late H1b of the inner never moves up
    CHECK(mark_applied(top, 0x1000, 0xB0100001) && outer.applied);
    CHECK(!mark_applied(nullptr, 0x1000, 0xB0100001) && !mark_applied(top, 0, 0xB0100001));
    // Same actor re-entering: the inner one is its own call.
    PendingCall again{0x1000, 0xB0100001, false, &outer};
    outer.applied = false;
    CHECK(mark_applied(&again, 0x1000, 0xB0100001) && again.applied && !outer.applied);
}
void s35_req_between_selection_and_execution() {
    Table<16, 8> t;
    Actor a = actor(0xB0100800, 9); a.controller = kGuest; a.queued_us = 1; t.born(a);
    auto* r = t.find(a.key);
    Policy p; p.guard = true; p.h1b = true; p.no_progress_us = 8'000'000; p.silence_us = 2'000'000;
    p.req_grace_us = 8'000'000;
    Context c; c.now = 9'000'000; c.last_guest_req_us = c.now - 10;
    CHECK(containment_due(*r, p, c) == Due::Retire);
    const Actor selected = *r;  // the plan is chosen here
    CHECK(t.guest_req(r->key, 9'000'100));  // its first Req arrives before the plan runs
    r->pending = true;  // the reservation
    c.now = 9'000'200;
    CHECK(recheck_root(*r, selected, p, c) == Recheck::NewEvidence);
    // S34 (guard off): the Req did not cancel it.
    Policy s34;
    CHECK(recheck_root(*r, selected, s34, c) == Recheck::Valid);
    // Applied between selection and execution: progress, with or without guard.
    const Actor selected2 = *r;
    CHECK(t.record(r->key, h1b_call(9), 9'000'300) == Evidence::Applied);
    CHECK(recheck_root(*r, selected2, p, c) == Recheck::Progress && recheck_root(*r, selected2, s34, c) == Recheck::Progress);
    // Another incarnation behind the key: gone.
    Actor reborn = *r; reborn.birth = 10;
    CHECK(recheck_root(reborn, selected2, p, c) == Recheck::Gone);
}
void s35_req_grace_is_bounded() {
    // A Req suspends the destructive decision for a bounded grace; it never protects a rejected operation forever.
    Table<16, 8> t;
    Actor a = actor(0xB0100810, 4); a.controller = kGuest; t.born(a);
    auto* r = t.find(a.key);
    t.queued(r->key, kGuest, 1'000'000);
    t.guest_req(r->key, 2'000'000);
    ActionCall rejected = h1b_call(4); rejected.h1b = false; rejected.internal = 0x1D;
    t.record(r->key, rejected, 2'000'100);
    Policy p; p.guard = true; p.h1b = true; p.no_progress_us = 8'000'000; p.silence_us = 2'000'000;
    p.req_grace_us = 8'000'000;
    Context c; c.last_guest_req_us = 0;
    for (uint64_t now = 9'000'000; now < 17'000'000; now += 500'000) {
        c.now = now; c.last_guest_req_us = now;
        CHECK(containment_due(*r, p, c) == Due::PostponeReq);
    }
    c.now = 17'000'000; c.last_guest_req_us = c.now;
    CHECK(containment_due(*r, p, c) == Due::Retire);
    postpone(*r, Due::PostponeReq, c.now);  // a Req posponement does not restart the clock
    CHECK(r->clock_us == 0 && r->retry_after_us == c.now + kCleanupRetryUs && !r->pending);
    c.now += kCleanupRetryUs;
    CHECK(containment_due(*r, p, c) == Due::Retire);
}
void s35_silence_and_loading() {
    // No retirement because of the guest's silence or load; the clock restarts when it speaks again.
    Table<16, 8> t;
    Actor a = actor(0xB0100820, 4); a.controller = kGuest; t.born(a);
    auto* r = t.find(a.key);
    t.queued(r->key, kGuest, 1'000'000);
    Policy p; p.guard = true; p.h1b = true; p.no_progress_us = 8'000'000; p.silence_us = 2'000'000;
    Context c; c.now = 10'000'000;
    c.last_guest_req_us = 0;  // the guest never sent an AI Req (cdmp_peer)
    CHECK(containment_due(*r, p, c) == Due::PostponeSilence);
    c.last_guest_req_us = c.now - 2'000'000;
    CHECK(containment_due(*r, p, c) == Due::PostponeSilence);
    c.last_guest_req_us = c.now - 1'999'999;
    CHECK(containment_due(*r, p, c) == Due::Retire);
    c.guest_loading = true;
    CHECK(containment_due(*r, p, c) == Due::PostponeLoading);
    postpone(*r, Due::PostponeLoading, c.now);
    CHECK(r->clock_us == c.now && r->postpones == 1 && r->postponed_us == c.now && !r->refusals);
    c.guest_loading = false;
    c.now += kCleanupRetryUs; c.last_guest_req_us = c.now;
    CHECK(containment_due(*r, p, c) == Due::No);  // a full deadline again from the end of the load
    c.now = r->clock_us + 8'000'000; c.last_guest_req_us = c.now;
    CHECK(containment_due(*r, p, c) == Due::Retire);
    // S34 (guard off): silence does not matter (the previous behaviour).
    Policy s34; Context quiet; quiet.now = 10'000'000;
    Actor b = actor(0xB0100821, 5); b.controller = kGuest; b.queued_us = 1'000'000; b.assigned = true;
    CHECK(containment_due(b, s34, quiet) == Due::Retire);
}
void s35_guard_off_is_s34() {
    // With guard=0, H1b off and the 5 s default, containment_due is no_progress exactly.
    Policy p;
    for (int removed = 0; removed < 2; ++removed)
    for (int pending = 0; pending < 2; ++pending)
    for (int retired = 0; retired < 2; ++retired)
    for (int orphan = 0; orphan < 2; ++orphan)
    for (int applied = 0; applied < 2; ++applied)
    for (uint64_t now : {uint64_t{1}, uint64_t{4'999'999}, uint64_t{5'000'100}, uint64_t{20'000'000}}) {
        Actor a = actor(0xB0100900, 1);
        a.removed = removed != 0; a.pending = pending != 0; a.local_retired = retired != 0; a.orphaned = orphan != 0;
        a.applied_us = applied ? 50 : 0; a.queued_us = 100; a.activated_us = 60;
        Context c; c.now = now;
        CHECK((containment_due(a, p, c) == Due::Retire) == no_progress(a, now));
    }
}
void s35_family_member_versions() {
    // An incomplete or changed family at execution: a member that acted or changed its link re-plans the unit.
    Table<16, 8> t;
    Actor root = actor(0xB0100A00, 1); root.controller = kGuest; t.born(root);
    Actor kid = actor(0xB0100A01, 2, root.key); kid.controller = kHost; t.born(kid);
    const Family f = t.family(root.key);
    CHECK(f.size == 2);
    Policy guard; guard.guard = true; Policy s34;
    CHECK(recheck_member(*t.find(kid.key), f.actors[1], guard) == Recheck::Valid);
    bump(*t.find(kid.key));  // e.g. its link changed (user2_stages_npc_family_observed)
    CHECK(recheck_member(*t.find(kid.key), f.actors[1], guard) == Recheck::NewEvidence);
    CHECK(recheck_member(*t.find(kid.key), f.actors[1], s34) == Recheck::Valid);
    t.find(kid.key)->removed = true;
    CHECK(recheck_member(*t.find(kid.key), f.actors[1], guard) == Recheck::Gone);
    static_assert(sizeof(Member) == 24, "the version lives in the padding");
}
void s35_degradation() {
    // containment_ready false with npc_activation=1 and npc_degradation=1: the old global 5 s abort is NOT re-armed;
    // new assignments pause instead. With 0 (or npc_activation not 1) the old net stays (the previous behaviour).
    Degrade d{true, false, true};
    CHECK(!legacy_watchdog_armed(d) && assignments_paused(d));
    d.ready = true;
    CHECK(!legacy_watchdog_armed(d) && !assignments_paused(d));
    Degrade old{true, false, false};
    CHECK(legacy_watchdog_armed(old) && !assignments_paused(old));
    Degrade probe_activation{false, false, true};
    CHECK(legacy_watchdog_armed(probe_activation) && !assignments_paused(probe_activation));
    // An abandoned or postponed unit never reaches Readiness: only infrastructure does.
    Readiness r{true, true, true, true, true, false};
    CHECK(containment_ready(r));
    r.fault = true;
    Degrade faulted{true, containment_ready(r), true};
    CHECK(!legacy_watchdog_armed(faulted) && assignments_paused(faulted));
    for (uint8_t e = 0; e < static_cast<uint8_t>(Evidence::Count); ++e) CHECK(evidence_name(static_cast<Evidence>(e))[0] != '?');
}
void road_bounds() {
    CHECK(!road_metadata({}).valid);
    std::vector<uint8_t> rec(200, 0);
    auto write = [&](size_t at, uint32_t n) {
        for (unsigned i = 0; i < 4; ++i) rec[at + i] = static_cast<uint8_t>(n >> (8 * i));
    };
    write(30, 3); const size_t end = 40;
    write(end + 88, 0xA01001FC); write(end + 96, 0x3FD);
    rec[end + 92] = 1; rec[end + 93] = 2;
    const auto r = road_metadata(rec);
    CHECK(r.valid && r.requester == 0xA01001FC && r.id == 0x3FD && r.metadata == 0x201);
    CHECK(!road_metadata(std::span<const uint8_t>(rec.data(), end + 99)).valid);
    write(30, 0xFFFFFFFF);
    CHECK(!road_metadata(rec).valid); // longitud adversaria nunca desborda ni sale del registro
}
}
int main() {
    modes(); phases_and_identity(); families(); bounded_retry_and_independence(); capacity_and_reuse(); road_bounds(); refused_actor_does_not_starve_others(); local_retirement_and_retry(); family_member_retry_is_individually_bounded();
    real_deletion_keeps_lookups_short(); prune_and_tombstones(); refusals_are_bounded(); decisions(); caches_and_budgets();
    abandonment_is_per_unit(); group_veto_only_when_whole_group_exhausted();
    s35_fixture_c_b0100391(); s35_fixture_a_slow_applications(); s35_fixture_b_not_evidence(); s35_branch_281();
    s35_tls_reentry(); s35_req_between_selection_and_execution(); s35_req_grace_is_bounded(); s35_silence_and_loading();
    s35_guard_off_is_s34(); s35_family_member_versions(); s35_degradation(); s35_h1b_exit_read_fails();
    s35_detour_reads_on_a_frame(); s35_postpone_is_bounded(); s35_execution_decisions();
    std::printf("S33 activación NPC: %d fallos\n", failures);
    return failures ? 1 : 0;
}
