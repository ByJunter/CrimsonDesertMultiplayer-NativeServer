// Which of the other player's characters a client shows (src/mod/session2/focus_follow_logic.h) and which hides the
// clan may make (src/mod/session2/remote_visible_logic.h). Keys of the live session of 2026-11-05: Kliff 0xA0100001,
// Damiane 0xB0100002, the guest's main 0xA01004D8.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "mod/session2/focus_follow_logic.h"
#include "mod/session2/remote_visible_logic.h"

using namespace cdmp::session2;

namespace {
int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++g_failures; } } while (0)

constexpr uint32_t kKliff = 0xA0100001, kDamiane = 0xB0100002, kOongka = 0xB0100003, kGuest = 0xA01004D8;
constexpr uintptr_t kLocal = 0x2FD85632A00, kOther = 0x2FE279B8A40;

ffl::Seen there(bool hidden, uint32_t type = ffl::kTypeNamed) {
    ffl::Seen s;
    s.exists = true;
    s.other_user = true;
    s.hidden = hidden;
    s.type = type;
    return s;
}

void test_wire_round_trip() {
    const ffl::Focus f{kDamiane, kKliff};
    const auto bytes = ffl::frame(f);
    CHECK(bytes.size() == ffl::kHeader + ffl::kPayloadSize);
    CHECK(ffl::is_frame(bytes));
    const auto back = ffl::decode(bytes);
    CHECK(back.has_value());
    CHECK(back && *back == f);
    CHECK(ffl::unpack(ffl::pack(f)) == f);
}

void test_wire_refuses_others() {
    auto bytes = ffl::frame({kDamiane, kKliff});
    auto other_state = bytes;
    other_state[ffl::kHeader] = 15;
    CHECK(!ffl::is_frame(other_state));
    auto other_version = bytes;
    other_version[ffl::kHeader + 1] = 2;
    CHECK(ffl::is_frame(other_version));  // ours: taken out of the stream
    CHECK(!ffl::decode(other_version).has_value());
    auto short_one = bytes;
    short_one.pop_back();
    CHECK(!ffl::is_frame(short_one));
    CHECK(!ffl::decode(ffl::frame({kDamiane, kDamiane})).has_value());  // a main character is class 0xA
    CHECK(!ffl::decode(ffl::frame({0x90100000, kKliff})).has_value());  // a user key
}

void test_clan_field() {
    CHECK(ffl::from_clan(kKliff, kKliff) == (ffl::Focus{kKliff, kKliff}));
    CHECK(ffl::from_clan(kDamiane, kKliff) == (ffl::Focus{kDamiane, kKliff}));
    CHECK(ffl::from_clan(0, kKliff) == (ffl::Focus{kKliff, kKliff}));  // before any change
}

void test_publisher() {
    ffl::Publisher p;
    const ffl::Focus a{kKliff, kKliff}, b{kDamiane, kKliff};
    CHECK(p.due(a, 1000));
    CHECK(!p.due(a, 1100));
    CHECK(p.due(b, 1200));  // a change goes at once
    CHECK(!p.due(b, 1200 + ffl::kResendMs - 1));
    CHECK(p.due(b, 1200 + ffl::kResendMs));  // and again, for a guest that joins later
    CHECK(!p.due(ffl::Focus{}, 90000));
}

// The report of 2026-11-05: the host goes to Damiane, the friend keeps seeing Kliff and never sees her.
void test_the_report() {
    ffl::Follower f;
    f.on_want({kKliff, kKliff}, 100, true);
    CHECK(f.left()[0].key == 0 && f.left()[1].key == 0);
    CHECK(ffl::act_on_focus(there(true, ffl::kTypeNamed)) == ffl::Act::Show);

    f.on_want({kDamiane, kKliff}, 200, false);
    CHECK(f.want().focus == kDamiane);
    CHECK(f.left()[0].key == kKliff);
    CHECK(f.act_on_left(0, there(false, ffl::kTypePlayer), 210) == ffl::Act::Hide);
    CHECK(f.left()[0].key == 0);  // once
    CHECK(f.act_on_left(0, there(false, ffl::kTypePlayer), 220) == ffl::Act::None);
}

void test_back_to_the_main_character() {
    ffl::Follower f;
    f.on_want({kDamiane, kKliff}, 100, true);
    (void)f.act_on_left(0, there(false, ffl::kTypePlayer), 110);
    f.on_want({kKliff, kKliff}, 200, false);
    CHECK(f.left()[0].key == kDamiane);
    CHECK(f.act_on_left(0, there(false), 210) == ffl::Act::Hide);
}

void test_one_to_another() {
    ffl::Follower f;
    f.on_want({kDamiane, kKliff}, 100, true);
    (void)f.act_on_left(0, there(true, ffl::kTypePlayer), 110);  // Kliff already hidden: nothing, gone from the list
    f.on_want({kOongka, kKliff}, 200, false);
    // The main character again (a copy that came back in range would already be hidden) and the one it left.
    CHECK(f.left()[0].key == kKliff);
    CHECK(f.left()[1].key == kDamiane);
    CHECK(f.act_on_left(0, there(true, ffl::kTypePlayer), 210) == ffl::Act::None);
    CHECK(f.act_on_left(1, there(false), 210) == ffl::Act::Hide);
}

void test_joins_after_the_switch() {
    ffl::Follower f;
    f.on_want({kDamiane, kKliff}, 100, true);  // first frame this connection: the main character is on screen
    CHECK(f.left()[0].key == kKliff);
    CHECK(f.left()[1].key == 0);
}

void test_quick_return_cancels_a_hide() {
    ffl::Follower f;
    f.on_want({kDamiane, kKliff}, 100, true);
    CHECK(f.left()[0].key == kKliff);
    f.on_want({kKliff, kKliff}, 150, false);  // back before a dispatch came
    CHECK(f.left()[0].key == kDamiane);       // Kliff is not hidden after all; she is
    CHECK(f.left()[1].key == 0);
}

void test_repeats_change_nothing() {
    ffl::Follower f;
    f.on_want({kDamiane, kKliff}, 100, true);
    (void)f.act_on_left(0, there(false, ffl::kTypePlayer), 110);
    const auto before = f.changes();
    f.on_want({kDamiane, kKliff}, 2100, false);  // the 2 s resend must not hide Kliff again
    CHECK(f.changes() == before);
    CHECK(f.left()[0].key == 0 && f.left()[1].key == 0);
}

void test_new_connection_forgets() {
    ffl::Follower f;
    f.on_want({kDamiane, kKliff}, 100, true);
    f.on_want({kGuest, kGuest}, 200, true);
    CHECK(f.want().focus == kGuest);
    CHECK(f.left()[0].key == 0 && f.left()[1].key == 0);
}

void test_what_a_client_may_touch() {
    ffl::Seen absent;
    CHECK(ffl::act_on_focus(absent) == ffl::Act::None);
    CHECK(ffl::act_on_focus(there(false)) == ffl::Act::None);  // visible already

    auto own = there(true);
    own.other_user = false;
    CHECK(ffl::act_on_focus(own) == ffl::Act::None);  // this client's own: the native routine's

    auto busy = there(true);
    busy.busy = true;
    CHECK(ffl::act_on_focus(busy) == ffl::Act::None);

    CHECK(ffl::act_on_focus(there(true, 5)) == ffl::Act::None);  // a mount is never a player's character
}

void test_left_waits_while_busy_then_gives_up() {
    ffl::Follower f;
    f.on_want({kDamiane, kKliff}, 100, true);
    auto busy = there(false, ffl::kTypePlayer);
    busy.busy = true;
    CHECK(f.act_on_left(0, busy, 200) == ffl::Act::None);
    CHECK(f.left()[0].key == kKliff);
    CHECK(f.act_on_left(0, busy, 100 + ffl::kBusyGiveUpMs) == ffl::Act::None);
    CHECK(f.left()[0].key == 0);
}

void test_left_not_here_is_native() {
    ffl::Follower f;
    f.on_want({kDamiane, kKliff}, 100, true);
    CHECK(f.act_on_left(0, ffl::Seen{}, 110) == ffl::Act::None);
    CHECK(f.left()[0].key == 0);
}

constexpr uint64_t kBit = rvis::kHiddenByClanBit;

void test_old_rule_for_the_server_copies() {
    CHECK(rvis::refuse_hide(kBit, true, kKliff, kOther, kLocal));
    CHECK(!rvis::refuse_hide(kBit, true, kDamiane, kOther, kLocal));
    CHECK(!rvis::refuse_hide(kBit, true, kKliff, kLocal, kLocal));
    CHECK(!rvis::refuse_hide(kBit, false, kKliff, kOther, kLocal));
    CHECK(!rvis::refuse_hide(0x40, true, kKliff, kOther, kLocal));
}

void test_client_rule_before_anything_is_known() {
    CHECK(rvis::refuse_client_hide(kBit, true, kKliff, kOther, kLocal, 0, 0));
    CHECK(!rvis::refuse_client_hide(kBit, true, kDamiane, kOther, kLocal, 0, 0));
}

void test_client_rule_main_in_control() {
    CHECK(rvis::refuse_client_hide(kBit, true, kKliff, kOther, kLocal, kKliff, kKliff));
    CHECK(!rvis::refuse_client_hide(kBit, true, kDamiane, kOther, kLocal, kKliff, kKliff));
}

void test_client_rule_after_the_switch() {
    // She loads again while in control: not hidden. Kliff, left behind: the clan's native hide goes through.
    CHECK(rvis::refuse_client_hide(kBit, true, kDamiane, kOther, kLocal, kDamiane, kKliff));
    CHECK(!rvis::refuse_client_hide(kBit, true, kKliff, kOther, kLocal, kDamiane, kKliff));
    CHECK(!rvis::refuse_client_hide(kBit, true, kOongka, kOther, kLocal, kDamiane, kKliff));
}

void test_client_rule_stale_after_a_new_guest() {
    // The last guest's focus is still stored when a new guest's character logs in: never hide it.
    CHECK(rvis::refuse_client_hide(kBit, true, kGuest, kOther, kLocal, kDamiane, kKliff));
}

void test_client_rule_own_character_is_native() {
    CHECK(!rvis::refuse_client_hide(kBit, true, kDamiane, kLocal, kLocal, kDamiane, kKliff));
    CHECK(!rvis::refuse_client_hide(kBit, true, kKliff, kLocal, kLocal, kKliff, kKliff));
    CHECK(!rvis::refuse_client_hide(kBit, false, kDamiane, kOther, kLocal, kDamiane, kKliff));
    CHECK(!rvis::refuse_client_hide(kBit, true, kDamiane, 0, kLocal, kDamiane, kKliff));
}

}  // namespace

int main() {
    test_wire_round_trip();
    test_wire_refuses_others();
    test_clan_field();
    test_publisher();
    test_the_report();
    test_back_to_the_main_character();
    test_one_to_another();
    test_joins_after_the_switch();
    test_quick_return_cancels_a_hide();
    test_repeats_change_nothing();
    test_new_connection_forgets();
    test_what_a_client_may_touch();
    test_left_waits_while_busy_then_gives_up();
    test_left_not_here_is_native();
    test_old_rule_for_the_server_copies();
    test_client_rule_before_anything_is_known();
    test_client_rule_main_in_control();
    test_client_rule_after_the_switch();
    test_client_rule_stale_after_a_new_guest();
    test_client_rule_own_character_is_native();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("focus follow: ok\n");
    return 0;
}
