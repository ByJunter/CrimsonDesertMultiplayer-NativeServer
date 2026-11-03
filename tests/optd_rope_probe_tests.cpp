// P8 session 27, the rope probes (docs/research/s25/gancho-cuerda.md section 13): the pure logic of rope_probe
// (src/mod/session2/rope_probe_logic.h): the device packets with S27's real bytes, the per-character counters and the
// self-consistency of the verified build-2976 bytes.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

#include "mod/session2/rope_probe_logic.h"

using namespace cdmp::session2;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(b.data() + at, &v, 4); }

void test_events() {
    CHECK(rpb::dev_event(0x003ECC59) == rpb::DevEvent::Deploy);
    CHECK(rpb::dev_event(0xDA667CC9) == rpb::DevEvent::Catch);
    CHECK(rpb::dev_event(0x92786A19) == rpb::DevEvent::E9278);
    CHECK(rpb::dev_event(0xA55A59FF) == rpb::DevEvent::EA55A);
    CHECK(rpb::dev_event(0x550D1ED6) == rpb::DevEvent::Release);
    CHECK(rpb::dev_event(0xEDFD0EBB) == rpb::DevEvent::Stow);
    CHECK(!rpb::dev_event(0x128B06E9));  // the gimmicks' init event, not the device's
    CHECK(!rpb::dev_event(0x677A2658));  // a trigger's enter event
    for (int i = 0; i < static_cast<int>(rpb::DevEvent::Count); ++i) {
        CHECK(std::strcmp(rpb::dev_event_name(static_cast<rpb::DevEvent>(i)), "?") != 0);
    }
}

void test_gimmick_req() {
    // H27-2 t 309.536: UpdateGimmickReq of the guest's device at its 2763 (the host's client sends it).
    const uint8_t pl[0x20] = {0xa1, 0x04, 0x10, 0xb0, 0xc9, 0x7c, 0x66, 0xda, 0xc7, 0x13, 0xfb,
                              0x85, 0x86, 0xd7, 0x74, 0x3f, 0x9f, 0xe1, 0x3b, 0xc4, 0x77, 0x03,
                              0x10, 0xa0, 0x77, 0x03, 0x10, 0xa0, 0x03, 0x25, 0x05, 0x00};
    const auto r = rpb::parse_gimmick_req(pl);
    CHECK(r.has_value());
    CHECK(r->gimmick == 0xB01004A1u);
    CHECK(r->hash == 0xDA667CC9u);
    CHECK(r->character == 0xA0100377u);
    CHECK(rpb::dev_event(r->hash) == rpb::DevEvent::Catch);
    CHECK(!rpb::parse_gimmick_req(std::span<const uint8_t>(pl, 0x17)));
}

std::vector<uint8_t> load2005(uint32_t key, uint32_t owner, uint32_t parent, uint32_t parent2, bool pivot,
                              bool twist) {
    std::vector<uint8_t> b(0x400, 0);
    b[0] = 0x1a;
    b[1] = 0x19;
    b[2] = 0x07;
    put32(b, rpb::k2005_Key, key);
    put32(b, rpb::k2005_Owner, owner);
    put32(b, rpb::k2005_Parent, parent);
    put32(b, rpb::k2005_Parent2, parent2);
    if (pivot) std::memcpy(b.data() + 0x300, "RemoteCatchPivot", 16);
    if (twist) std::memcpy(b.data() + 0x340, "HandTwist", 9);
    return b;
}

void test_2005() {
    // The guest's device in S27 step 2: key B01004A1, owner 0x90100000 (the host's user), docked to A0100377 at
    // +0xEA and +0x104.
    auto b = load2005(0xB01004A1, 0x90100000, 0xA0100377, 0xA0100377, true, true);
    auto l = rpb::parse_2005(b);
    CHECK(l.has_value());
    CHECK(l->key == 0xB01004A1u);
    CHECK(l->owner == 0x90100000u);
    CHECK(l->parent == 0xA0100377u);
    CHECK(l->device);
    // The host's lantern-like gimmick B0100155: the pivot, no HandTwist: not a device.
    b = load2005(0xB0100155, 0x90100000, 0xA0100001, 0xA0100001, true, false);
    CHECK(!rpb::parse_2005(b)->device);
    // The two parent copies differ: unknown.
    b = load2005(0xB01004A1, 0x90100000, 0xA0100377, 0x12345678, true, true);
    CHECK(rpb::parse_2005(b)->parent == 0);
    // Short payloads.
    CHECK(!rpb::parse_2005(std::span<const uint8_t>(b.data(), 0x10)));
    CHECK(rpb::parse_2005(std::span<const uint8_t>(b.data(), 0x40))->parent == 0);
    CHECK(rpb::contains(b, "RemoteCatchPivot"));
    CHECK(!rpb::contains(b, "NotThere"));
    CHECK(!rpb::contains(std::span<const uint8_t>(b.data(), 4), "RemoteCatchPivot"));
    CHECK(rpb::player_key(0xA0100377));
    CHECK(!rpb::player_key(0xB01004A1));
    CHECK(!rpb::player_key(0x90100000));
}

void test_key_table() {
    rpb::KeyTable<4> t;
    CHECK(t.get(0) == nullptr);
    rpb::KeyCount* a = t.get(0xA0100001);
    CHECK(a != nullptr);
    CHECK(t.get(0xA0100001) == a);
    a->calls.fetch_add(3);
    CHECK(t.find(0xA0100001)->calls.load() == 3);
    CHECK(t.index(a) < 4);
    CHECK(&t.at(t.index(a)) == a);
    CHECK(t.find(0xA0100377) == nullptr);
    CHECK(t.get(0xA0100377) != nullptr);
    CHECK(t.get(0xB0100001) != nullptr);
    CHECK(t.get(0xB0100002) != nullptr);
    CHECK(t.used() == 4);
    CHECK(t.get(0xB0100003) == nullptr);  // full
    CHECK(t.full() == 1);
    CHECK(t.find(0xB0100003) == nullptr);
    CHECK(t.find(0xA0100377) != nullptr);
    CHECK(a->ctl.load() == 0xFF);  // unread until the hook reads it
}

void test_device_table() {
    rpb::DeviceTable<2> t;
    rpb::Device* d = t.get(0xB01004A1);
    CHECK(d && d->key == 0xB01004A1u && !d->got_2005);
    CHECK(t.find(0xB01004A1) == d);
    CHECK(t.get(0xB0100156) != nullptr);
    CHECK(t.get(0xB0100157) == nullptr);
    CHECK(t.full() == 1);
    CHECK(t.size() == 2);
}

void test_pair_and_timer() {
    CHECK(!rpb::timer_running(-1.0f));
    CHECK(rpb::timer_running(0.0f));
    CHECK(rpb::timer_running(0.25f));
    CHECK(rpb::pair_complete(rpb::kPairComplete));
    CHECK(rpb::pair_complete(rpb::kPairComplete | rpb::kPairRead));
    CHECK(!rpb::pair_complete(rpb::kPairComplete & ~rpb::kPairLink));
    CHECK(!rpb::pair_complete(rpb::kPairHolder | rpb::kPairPair));
    // A push this frame: +0x638 written >= 0 and not a value carried over.
    CHECK(rpb::pull_applied(true, -1.0f, true, 0.05f));  // first push of a run
    CHECK(rpb::pull_applied(true, 0.05f, true, 0.06f));  // a new value
    CHECK(!rpb::pull_applied(true, 0.05f, true, 0.05f));  // carried over (nothing written this frame)
    CHECK(!rpb::pull_applied(true, 0.05f, true, -1.0f));  // the run's end wrote -1
    CHECK(!rpb::pull_applied(true, -1.0f, false, 0.0f));  // unreadable after
    CHECK(rpb::pull_applied(false, 0.0f, true, 0.0f));    // unreadable before: counted when >= 0
}

// The verified bytes say what the constants say.
void test_constants() {
    CHECK(rpb::kTreePullBoundaries[std::size(rpb::kTreePullBoundaries) - 1] == sizeof rpb::kTreePullHead);
    CHECK(rpb::kTreePullBoundaries[0] >= 5);
    static_assert(rpb::kTreePullSlot == rpb::kTreePullVtbl + 8);  // vf1
    // mov rax,[rsp+0x60] after push rdi + sub rsp,0x30: entry rsp + 0x28 = the 5th argument (the actor holder).
    CHECK(rpb::kTreePullHead[22] == 0x60);
    CHECK((rpb::kTreePullHead[22] - 0x38) / 8 == 5);
    CHECK(rpb::kPullApplyBoundaries[std::size(rpb::kPullApplyBoundaries) - 1] == sizeof rpb::kPullApplyHead);
    // The only accepted patch sizes are whole-instruction boundaries of at least 5 bytes (the 5-byte jump).
    const auto in = [](const auto& arr, uint32_t v) {
        for (const uint32_t x : arr) {
            if (x == v) return true;
        }
        return false;
    };
    CHECK(std::size(rpb::kTreePullPatch) == 1 && rpb::kTreePullPatch[0] == 5);
    CHECK(in(rpb::kTreePullBoundaries, rpb::kTreePullPatch[0]));
    CHECK(std::size(rpb::kPullApplyPatch) == 1 && rpb::kPullApplyPatch[0] == 11);
    CHECK(in(rpb::kPullApplyBoundaries, rpb::kPullApplyPatch[0]));
    CHECK(!in(rpb::kPullApplyBoundaries, 5));  // 5 bytes would split an instruction: the jump takes 11
    // The head ends with vmovss xmm2,[rcx+0x63C]: the timer.
    uint32_t disp = 0;
    std::memcpy(&disp, rpb::kPullApplyHead + 15, 4);
    CHECK(disp == rpb::kMc_Timer);
    // The caller: vmovaps xmm1,xmm10 (the dt), mov rcx,rsi, call rel32 -> 0x8A30C0; returns to 0x87B2CB.
    const uint8_t* c = rpb::kPullApplyCallBytes;
    CHECK(c[8] == 0xE8);
    int32_t rel = 0;
    std::memcpy(&rel, c + 9, 4);
    CHECK(rpb::kPullApplyCall + 8 + 5 + static_cast<int64_t>(rel) == static_cast<int64_t>(rpb::kPullApply));
    static_assert(rpb::kPullApplyReturn == rpb::kPullApplyCall + sizeof rpb::kPullApplyCallBytes);
    static_assert(rpb::kHeader == 5);
    static_assert(rpb::k2005_Parent == 234 && rpb::k2005_Parent2 == 260);
}

}  // namespace

int main() {
    test_events();
    test_gimmick_req();
    test_2005();
    test_key_table();
    test_device_table();
    test_pair_and_timer();
    test_constants();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_rope_probe: all passed\n");
    return 0;
}
