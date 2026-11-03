// S42: pruebas puras, no cargan el juego ni el mod.
#include "mod/session2/npc_family_vehicles_logic.h"
#include <cstdio>
using namespace cdmp::session2;
using npcfam::MemberVerdict;
using npcfam::SplitWhy;
static int failures = 0;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::printf("FALLO línea %d: %s\n", __LINE__, #x);                       \
      ++failures;                                                              \
    }                                                                          \
  } while (0)
static famveh::In sample() {
  famveh::In x;
  x.vt = npcfam::kVtNpcSchedule;
  x.wrap = npcfam::kVtWrapGroupActionMember;
  x.in_vector = 1;
  x.depth = 1;
  x.parent_type = 3;
  auto &m = x.member;
  m.admitted = true;
  m.cls = npcfam::Cls::Schedule;
  m.type = 5;
  m.link = npcfam::Link::Present;
  m.link_matches = true;
  m.parent_alive = 1;
  m.parent_ctl = 1;
  m.old_ctl_known = true;
  m.desc_user_known = true;
  m.actor_user_known = true;
  m.host_field = true;
  m.owner_branch = true;
  m.key2 = 0x90100065;
  m.key_ok = true;
  return x;
}
int main() {
  auto x = sample();
  CHECK(npcfam::member_verdict(x.member).v ==
        MemberVerdict::Split); // 0/probe: B anterior conserva el rechazo.
  CHECK(famveh::decide(x).member.v == MemberVerdict::Assign);
  CHECK(famveh::decide(x).kind == famveh::Kind::ScheduleVehicle);
  CHECK(famveh::scope(0x4C12CB783ull));
  CHECK(!famveh::scope(0xC12CB784));
  npcfam::UnitSum u{};
  u.admit = npcfam::Admit::NodeSplitBefore;
  CHECK(famveh::projected_root(
      u)); // también el árbol de reposición alojado por B.
  u.admit = npcfam::Admit::KeysFull;
  CHECK(!famveh::projected_root(u)); // no esconder una falta de reserva.
  x.member.old_ctl = 1;
  CHECK(famveh::decide(x).member.why == SplitWhy::HasController);
  x = sample();
  x.member.actor_user = 1;
  CHECK(famveh::decide(x).member.why == SplitWhy::UserChild);
  x = sample();
  x.member.desc_user_known = false;
  CHECK(famveh::decide(x).member.why == SplitWhy::DescUser);
  x = sample();
  x.member.parent_alive = -1;
  CHECK(famveh::decide(x).member.why == SplitWhy::ParentGone);
  x = sample();
  x.member.parent_ctl = 0;
  CHECK(famveh::decide(x).member.why == SplitWhy::ParentController);
  x = sample();
  x.player_rides = true;
  CHECK(famveh::decide(x).member.v == MemberVerdict::Split);
  x = sample();
  x.vt = npcfam::kVtDescBase;
  x.wrap = npcfam::kVtWrapLinkedVehicle;
  x.member.cls = npcfam::Cls::Excluded;
  x.parent_type = 5;
  x.parent_key_matches = true;
  CHECK(famveh::decide(x).member.v == MemberVerdict::Assign);
  CHECK(famveh::decide(x).kind == famveh::Kind::Linked);
  x.parent_key_matches = false;
  CHECK(famveh::decide(x).member.v == MemberVerdict::Split);
  x.parent_key_matches = true;
  x.parent_type = 3;
  CHECK(famveh::decide(x).member.v == MemberVerdict::Split);
  x.parent_type = 5;
  x.in_vector = -1;
  CHECK(famveh::decide(x).member.why == SplitWhy::LateNotInVector);
  x = sample();
  x.vt = npcfam::kVtInstantGimmick;
  x.member.type = 7;
  x.member.types.mask |=
      1u << 7; // ni una cohorte que incluya 7 concede IA a esta carga.
  CHECK(famveh::decide(x).member.v == MemberVerdict::HostKept);
  x.parent_kept = true;
  x.vt = npcfam::kVtDescBase;
  x.member.parent_ctl = 0;
  CHECK(famveh::decide(x).member.v == MemberVerdict::HostKept);
  CHECK(famveh::decide(x).kind == famveh::Kind::Cargo);
  x.member.type = 3;
  CHECK(famveh::decide(x).member.v == MemberVerdict::Split);
  x.member.type = 7;
  x.depth = npcfam::kMaxDepth + 1;
  CHECK(famveh::decide(x).member.why == SplitWhy::LateNoRoom);
  // Modos efectivos: ninguno altera la admisión de B hasta demostrar el ciclo
  // nativo de Logout/cargas.
  CHECK(famveh::effective_mode(famveh::Mode::Off) == famveh::Mode::Off);
  CHECK(famveh::effective_mode(famveh::Mode::Probe) == famveh::Mode::Probe);
  CHECK(famveh::effective_mode(famveh::Mode::On) == famveh::Mode::Probe);
  // Regresión revisión independiente: reservado válido + lectura sombra
  // fallida. Antes el probe podía partirlo.
  for (auto requested :
       {famveh::Mode::Off, famveh::Mode::Probe, famveh::Mode::On}) {
    CHECK(famveh::effective_mode(requested) != famveh::Mode::On);
    auto valid = sample();
    valid.member.type = 3;
    const auto original = npcfam::member_verdict(valid.member);
    valid.member.cls = famveh::actual_class(valid.member.cls,
                                            npcfam::Cls::Unknown, false, true);
    CHECK(npcfam::member_verdict(valid.member).v == original.v);
    CHECK(npcfam::member_verdict(valid.member).v == MemberVerdict::Assign);
  }
  CHECK(famveh::actual_class(npcfam::Cls::Schedule, npcfam::Cls::Unknown, true,
                             true) == npcfam::Cls::Unknown);
  CHECK(famveh::actual_class(npcfam::Cls::Schedule, npcfam::Cls::Unknown, true,
                             false) == npcfam::Cls::Schedule);
  // Marcas de nacimiento y memoria temporal de la sonda no sobreviven a otro
  // nacimiento/época.
  npcfam::KeyMarks<16> marks;
  marks.insert(0xB010163D, 7);
  CHECK(marks.contains(0xB010163D, 7));
  marks.erase(0xB010163D);
  CHECK(!marks.contains(0xB010163D, 7));
  marks.insert(0xB010163D, 8);
  CHECK(!marks.contains(0xB010163D, 7));
  // Un marco reutilizado inicializa la simulación, nunca hereda el control
  // hipotético del nacimiento anterior.
  static npcfam::Frame f;
  npcfam::open_frame(f, 1, 1);
  auto *r = npcfam::add_rec(f, 0x10000, npcfam::Kind::Reserved, 0, 0, 1,
                            npcfam::Cls::Schedule);
  r->projected = 3;
  r->type = 5;
  npcfam::open_frame(f, 2, 1);
  r = npcfam::add_rec(f, 0x10000, npcfam::Kind::Reserved, 0, 0, 1,
                      npcfam::Cls::Schedule);
  CHECK(r->projected == 0 && r->type == 0);
  std::printf("NpcFamilyVehicles: %s\n", failures ? "FALLO" : "OK");
  return failures ? 1 : 0;
}
