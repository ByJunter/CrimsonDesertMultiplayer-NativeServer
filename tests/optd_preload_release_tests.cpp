// The guest's repeat of the 2322 when its readiness byte was written back.
#include <cstdio>

#include "mod/session2/preload_release_logic.h"

using namespace cdmp::session2::prl;

namespace {

int g_failed = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAILED: %s\n", what);
        ++g_failed;
    }
}

constexpr uint32_t kOwn = 0xA0100269;

Facts waiting(uint64_t now, uint8_t marker = 1) {
    Facts f;
    f.readable = true;
    f.state = kStateWaitPreload;
    f.marker = marker;
    f.own_key = kOwn;
    f.field_is_own = true;
    f.now_ms = now;
    return f;
}

void normal_load_never_acts() {
    Release r;
    r.on_trip();
    r.on_delivered_2322(kOwn);
    // The marker is already 2 when the client reaches 0x11: nothing to do.
    for (uint64_t t = 0; t < 30000; t += 100) {
        check(r.decide(waiting(t, 2)) == Action::None, "marker 2 never repeats");
    }
    check(r.sent_total() == 0, "nothing sent on a normal load");
}

void other_states_never_act() {
    Release r;
    r.on_delivered_2322(kOwn);
    Facts f = waiting(1000);
    f.state = 0xF;
    check(r.decide(f) == Action::None, "a reset in 0xF is not repeated yet: it could be reset again");
    f.now_ms = 60000;
    check(r.decide(f) == Action::None, "only 0x11 waits for the marker");
    f.state = 0x12;
    check(r.decide(f) == Action::None, "0x12 is past the wait");
    f = waiting(61000);
    f.readable = false;
    check(r.decide(f) == Action::None, "an unreadable chain decides nothing");
}

void run9_repeats_after_settling() {
    Release r;
    r.on_trip();
    r.on_delivered_2322(kOwn);  // handled in 0xF, then written back to 1
    check(r.decide(waiting(5000)) == Action::None, "first look only starts the settle");
    check(r.decide(waiting(5000 + kSettleMs - 1)) == Action::None, "still settling");
    check(r.decide(waiting(5000 + kSettleMs)) == Action::Repeat, "the wait of run 9 is released");
    r.on_sent(5000 + kSettleMs);
    check(r.decide(waiting(5000 + kSettleMs + 1)) == Action::None, "not again at once");
    check(r.decide(waiting(5000 + kSettleMs + kRetryMs)) == Action::Repeat, "written back again: repeat again");
    r.on_sent(5000 + kSettleMs + kRetryMs);
    check(r.sent_total() == 2, "both counted");
    // The state moves on: the wait ends and its bookkeeping with it.
    check(r.decide(waiting(9000, 2)) == Action::None, "released");
    check(r.sent_this_wait() == 0, "a finished wait starts from zero");
}

void repeats_are_bounded() {
    Release r;
    r.on_delivered_2322(kOwn);
    uint32_t sent = 0;
    for (uint64_t t = 0; t < 600000; t += 100) {
        if (r.decide(waiting(t)) != Action::None) {
            r.on_sent(t);
            ++sent;
        }
    }
    check(sent == kMaxPerWait, "a client that never leaves 0x11 is not flooded");
}

void another_trip_needs_its_own_2322() {
    Release r;
    r.on_delivered_2322(kOwn);
    r.on_trip();  // a 2174: the previous trip's 2322 says nothing now
    check(r.decide(waiting(1000)) == Action::None, "no 2322 for this trip yet");
    check(r.decide(waiting(1000 + kSettleMs)) == Action::None, "the settle is not enough without one");
    r.on_delivered_2322(kOwn);
    check(r.decide(waiting(1000 + kSettleMs + 1)) == Action::Repeat, "its own 2322 allows the repeat");
}

void no_2322_at_all_synthesizes_late() {
    Release r;
    r.on_trip();
    check(r.decide(waiting(1000)) == Action::None, "starts the wait");
    check(r.decide(waiting(1000 + kSynthesizeAfterMs - 1)) == Action::None, "the owner's timer gets its time");
    check(r.decide(waiting(1000 + kSynthesizeAfterMs)) == Action::Synthesize, "a stalled load is released");
}

void another_character_does_not_count() {
    Release r;
    r.on_delivered_2322(0xA0100001);  // the host's character
    check(r.decide(waiting(1000)) == Action::None, "starts the wait");
    check(r.decide(waiting(1000 + kSettleMs)) == Action::None, "a 2322 for the host says nothing about this client");
}

void a_field_of_another_user_is_left_alone() {
    Release r;
    r.on_delivered_2322(kOwn);
    Facts f = waiting(1000);
    f.field_is_own = false;
    check(r.decide(f) == Action::None, "the handler would write another user's manager");
    f.now_ms = 100000;
    check(r.decide(f) == Action::None, "never, however long");
}

void reset_forgets_everything() {
    Release r;
    r.on_delivered_2322(kOwn);
    (void)r.decide(waiting(0));
    r.on_sent(1);
    r.reset();
    check(r.sent_total() == 0, "counters restart");
    check(r.decide(waiting(5000)) == Action::None, "wait restarts");
    check(r.decide(waiting(5000 + kSettleMs)) == Action::None, "the old 2322 is gone");
}

}  // namespace

int main() {
    normal_load_never_acts();
    other_states_never_act();
    run9_repeats_after_settling();
    repeats_are_bounded();
    another_trip_needs_its_own_2322();
    no_2322_at_all_synthesizes_late();
    another_character_does_not_count();
    a_field_of_another_user_is_left_alone();
    reset_forgets_everything();
    if (g_failed) {
        std::printf("%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("preload release: all checks passed\n");
    return 0;
}
