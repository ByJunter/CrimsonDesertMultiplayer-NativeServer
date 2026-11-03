// Pruebas puras; no cargan el juego ni el mod.
#include <cstdio>
#include <cstring>
#include <vector>
#include "mod/session2/npc_probe_client_logic.h"
#include "mod/session2/npc_activation_logic.h"
using namespace cdmp::session2;
namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FALLO %d: %s\n", __LINE__, #x); ++failures; } } while (0)
void put(std::vector<uint8_t>& v, size_t o, uint32_t x) { std::memcpy(v.data() + o, &x, 4); }
void fp(std::vector<uint8_t>& v, size_t o, float x) { std::memcpy(v.data() + o, &x, 4); }
}
int main() {
    // Mismo contrato literal en cliente y servidor; 1 nunca activa autoridad familiar/vial.
    CHECK(npcact::parse_mode("0",true).mode == npcact::Mode::Off);
    CHECK(npcact::parse_mode("probe",false).mode == npcact::Mode::Probe);
    CHECK(npcact::parse_mode("1",true).mode == npcact::Mode::On);
    CHECK(npcact::parse_mode("1",false).bad && npcact::parse_mode("1",false).mode == npcact::Mode::Off);
    CHECK(npcact::parse_mode("PROBE",true).bad);
    CHECK(npcact::parse_mode("probe ; nota",true).bad);
    CHECK(npcact::parse_mode("true",true).bad);
    npcp::Load l; std::vector<uint8_t> r(200, 0xFF);
    r[0] = 3; put(r, 1, 0xB01004C8); put(r, 0x12, 0x901001FB);
    put(r, 30, 0); put(r, 122, 0xA01001FC); put(r, 126, 0x101); put(r, 130, 0x3FD);
    put(r, 138, 0); r[142] = 14; r[143] = 0;
    for (uint32_t i = 0; i < 14; ++i) put(r, 144 + i * 4, 0xB01004C9 + i);
    CHECK(npcp::load(r,l)); CHECK(l.family_valid && l.parent == 0 && l.children_count == 14);
    CHECK(l.children[13] == 0xB01004D6 && l.controller == 0x901001FB);
    CHECK(l.road_fields && l.road == 0x3FD && l.requester == 0xA01001FC);
    r.pop_back(); CHECK(npcp::load(r,l)); CHECK(!l.family_valid);
    put(r,30,0xFFFFFFFF); CHECK(npcp::load(r,l)); CHECK(!l.road_fields);
    CHECK(!npcp::load(std::span<const uint8_t>(r).first(12),l));
    put(r,1,0xA01001FC); CHECK(!npcp::load(r,l));
    npcp::Motion m; std::vector<uint8_t> a(725); put(a,0,0xB0100450);
    fp(a,0x8E,-10588.9f); fp(a,0x92,599.5f); fp(a,0x96,-3903.7f);
    CHECK(npcp::motion(2223,a,m) && m.position && m.x == -10588.9f && m.z == -3903.7f);
    CHECK(npcp::motion(2223,std::span<const uint8_t>(a).first(10),m) && !m.position);
    put(a,0x8E,0x7FC00000); CHECK(npcp::motion(2223,a,m) && !m.position);
    CHECK(npcp::motion(2291,a,m) && !m.position); CHECK(!npcp::motion(2005,a,m));
    std::vector<uint8_t> mv(514); put(mv,0,0xB0100450); fp(mv,4,15); fp(mv,8,2); fp(mv,12,30);
    CHECK(npcp::motion(2556,mv,m) && m.position && m.z == 30);
    CHECK(npcp::motion(2697,mv,m) && m.position && m.x == 15);
    npcp::Timeline t; t.hand(200); CHECK(t.present == 0 && t.first_req == 0);
    t.receive(100); t.receive(110); t.see(230); t.request(500); t.request(510);
    CHECK(t.received == 100 && t.handed == 200 && t.present == 230 && t.first_req == 500);
    // Baja recibida y clave reutilizada antes de entregar la baja/alta viejas:
    // sus observaciones no pertenecen al nuevo nacimiento (encolado a 300).
    using Relation = npcp::DeliveryRelation;
    CHECK(npcp::delivery_relation(300,100,false) == Relation::Historical);
    CHECK(npcp::delivery_relation(300,200,false) == Relation::Historical);
    CHECK(npcp::delivery_relation(300,300,false) == Relation::Candidate);
    CHECK(npcp::delivery_relation(300,350,false) == Relation::Candidate);
    CHECK(npcp::delivery_relation(300,300,true) == Relation::Historical);
    CHECK(npcp::delivery_relation(0,100,false) == Relation::Unknown);
    CHECK(npcp::delivery_relation(300,0,false) == Relation::Unknown);
    // Revisión (volumen): primer estado siempre; después sólo hitos de claves de interés, con tope.
    CHECK(npcp::status_line_wanted(true,false,false,0));
    CHECK(!npcp::status_line_wanted(false,true,false,1)); // NPC ya activo y sin familia: sólo resumen
    CHECK(!npcp::status_line_wanted(false,false,true,1)); // bytes que oscilan no son hito
    CHECK(npcp::status_line_wanted(false,true,true,1));
    CHECK(!npcp::status_line_wanted(false,true,true,npcp::kMaxStatusLines));
    CHECK(!npcp::status_line_wanted(true,true,true,npcp::kMaxStatusLines));
    npcp::Track tr; CHECK(tr.moved() == 0.f);
    tr.add(10.f,20.f); tr.add(13.f,24.f); CHECK(tr.moved() == 5.f);
    tr.restart(); CHECK(tr.moved() == 0.f);
    tr.add(13.f,24.5f); CHECK(tr.moved() == 0.5f && tr.moved() < npcp::kMovedMeters);
    std::printf("Sonda NPC: %s\n", failures ? "FALLOS" : "correcta"); return failures ? 1 : 0;
}
