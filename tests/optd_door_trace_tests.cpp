#include "mod/session2/door_trace_logic.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>
namespace dt = cdmp::session2::doortrace;
int failures = 0;
#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                  \
      ++failures;                                                              \
    }                                                                          \
  } while (0)
std::vector<uint8_t> framed(uint16_t op, const std::vector<uint8_t> &pl) {
  std::vector<uint8_t> f{
      static_cast<uint8_t>(op), static_cast<uint8_t>(op >> 8), 4,
      static_cast<uint8_t>(pl.size()), static_cast<uint8_t>(pl.size() >> 8)};
  f.insert(f.end(), pl.begin(), pl.end());
  return f;
}
int main() {
  CHECK(dt::observe_tap(false, true));
  CHECK(dt::observe_tap(false, false));
  CHECK(!dt::observe_tap(true, true));
  CHECK(dt::observe_tap(true, false));
  // Sólo se habilita el tramo de 5B medido. Una preparación existente con
  // parche mayor no está autorizada.
  const std::array<uint8_t, 5> head{0x48, 0x89, 0x5c, 0x24, 0x08};
  CHECK(dt::verified_patch(head));
  CHECK(!dt::verified_patch({}));
  for (size_t n = 0; n < head.size(); ++n)
    CHECK(!dt::verified_patch(std::span(head.data(), n)));
  auto bad_head = head;
  bad_head[4] ^= 1;
  CHECK(!dt::verified_patch(bad_head));
  std::vector<uint8_t> long_head(head.begin(), head.end());
  long_head.resize(15);
  CHECK(!dt::verified_patch(long_head));
  // Captura positiva H, offset 26130245: UUID exacto, resource DE7B52AA.
  const std::vector<uint8_t> req{4,    1,    0,    0,    0,    0xaa, 0x52,
                                 0x7b, 0xde, 0x4f, 0xb9, 0xee, 0xab, 0x67,
                                 0x0f, 0x74, 0x56, 0x3c, 0x34, 0x3d, 0x64,
                                 0x0c, 0,    0,    0};
  auto r = dt::frame(framed(2928, req));
  CHECK(r.valid && r.matched && r.kind == dt::Kind::Spawn);
  CHECK(r.items == 1 && r.resource == 0xDE7B52AA && r.subject == 0);
  // Ack positivo H offset26896307: active1. Nak S38 conserva actor+UUID, carece
  // del código de error.
  std::vector<uint8_t> ack{4, 1, 0, 0x10, 0xa0, 1};
  ack.insert(ack.end(), dt::kScene.begin(), dt::kScene.end());
  r = dt::frame(framed(2145, ack));
  CHECK(r.valid && r.matched && r.subject == 0xA0100001 && r.active == 1);
  std::vector<uint8_t> nak{4, 0x7b, 2, 0x10, 0xa0};
  nak.insert(nak.end(), dt::kScene.begin(), dt::kScene.end());
  r = dt::frame(framed(2953, nak));
  CHECK(r.valid && r.matched && r.subject == 0xA010027B &&
        r.kind == dt::Kind::Rejected);
  auto wrong = req;
  wrong.back() = 1;
  CHECK(dt::parse(2928, wrong).valid && !dt::parse(2928, wrong).matched);
  for (size_t n = 0; n < req.size(); ++n)
    CHECK(!dt::parse(2928, std::span(req.data(), n)).valid);
  wrong = req;
  wrong[1] = 0;
  CHECK(!dt::parse(2928, wrong).valid);
  wrong = req;
  wrong[1] = 2;
  CHECK(dt::parse(2928, wrong).valid);
  wrong = req;
  wrong.push_back(0);
  CHECK(!dt::parse(2928, wrong).valid);
  wrong = req;
  wrong[0] = 0;
  CHECK(!dt::parse(2928, wrong).valid);
  auto f = framed(2928, req);
  f[3]--;
  CHECK(!dt::frame(f).valid);
  f = framed(2928, req);
  f.push_back(0);
  CHECK(!dt::frame(f).valid);
  // Lote: se reconoce cada identidad de16B en su límite y no coincidencias
  // accidentales dentro del hash.
  wrong = req;
  wrong[1] = 2;
  wrong.insert(wrong.end(), dt::kScene.begin(), dt::kScene.end());
  CHECK(dt::parse(2928, wrong).valid && dt::parse(2928, wrong).items == 2);
  wrong[1] = 1;
  CHECK(dt::parse(2928, wrong).valid && dt::parse(2928, wrong).items == 2);
  CHECK(!dt::parse(2779, req).valid);
  CHECK(!dt::parse(2671, req).valid);
  std::vector<uint8_t> sync(615);
  sync[4] = 1;
  std::copy(dt::kScene.begin(), dt::kScene.end(), sync.begin() + 8);
  CHECK(dt::parse(2665, sync).matched);
  sync[4] = 2;
  CHECK(!dt::parse(2665, sync).valid);
  const std::array<uint32_t, 4> codes{11, 12, 13, 14};
  CHECK(dt::classify(true, 0, codes, false) == dt::Result::Success);
  CHECK(dt::classify(false, 0, codes, true) == dt::Result::Unreadable);
  CHECK(dt::classify(true, 11, codes, true) == dt::Result::ResourceMissing);
  CHECK(dt::classify(true, 12, codes, true) == dt::Result::ResourceNotReady);
  CHECK(dt::classify(true, 13, codes, true) == dt::Result::NoCandidates);
  CHECK(dt::classify(true, 14, codes, true) == dt::Result::CreationFailed);
  CHECK(dt::classify(true, 11, codes, false) == dt::Result::Other);
  CHECK(dt::classify(true, 99, codes, true) == dt::Result::Other);
  CHECK(dt::classify(true, 99, {}, true) == dt::Result::Other);
  CHECK(dt::classify(true, 11, {11, 11, 13, 14}, true) == dt::Result::Other);
  std::printf("door_trace: %s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
