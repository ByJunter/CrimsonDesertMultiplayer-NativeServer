#include <array>
#include <cstdio>
#include <cstring>

#include "mod/game/character_phase_ready.h"
#include "mod/game/safe_memory.h"
#include "mod/session2/guest_actor_diag_logic.h"

namespace {
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::printf("FAIL line %d: %s\n", __LINE__, #expr); ++failures; } } while (0)
template <class T, size_t N>
void put(std::array<uint8_t, N>& object, size_t offset, T value) {
    std::memcpy(object.data() + offset, &value, sizeof value);
}
const auto read = []<class T>(uintptr_t address, T& out) noexcept {
    return cdmp::game::safe_read(address, &out, sizeof out);
};
struct Actor {
    std::array<uint8_t, 0x100> cc{};
    std::array<uint8_t, 0x30> selector{};
    std::array<std::array<uint8_t, 0x120>, 2> layer{};
    Actor() {
        put(cc, 0x30, reinterpret_cast<uintptr_t>(selector.data()));
        for (int i = 0; i < 2; ++i) put(cc, 0x88 + 8 * i, reinterpret_cast<uintptr_t>(layer[i].data()));
    }
    auto state() const { return cdmp::game::phase3527::selected_action(reinterpret_cast<uintptr_t>(cc.data()), read); }
};
}

int main() {
    using cdmp::game::phase3527::must_wait;
    using cdmp::game::phase3527::selected_action;
    namespace gad = cdmp::session2::gad;
    Actor actor;
    // Captured guest: selected layer0, current/playback0, chart present.
    put(actor.layer[0], 0x118, uintptr_t{0x12340000});
    const auto cc_before = actor.cc;
    const auto layer_before = actor.layer;
    CHECK(actor.state().readable);
    CHECK(must_wait(actor.state(), true));
    CHECK(must_wait(actor.state(), false));
    CHECK(actor.cc == cc_before && actor.layer == layer_before);
    // These native consumers ignore chart; a chart-based check is insufficient.
    put(actor.layer[0], 0x118, uintptr_t{0});
    CHECK(!gad::layer_lacks_action(0, 0, 0));
    CHECK(must_wait(actor.state(), true));
    // A received action permits the bool query; movement also needs playback.
    put(actor.layer[0], 0x10, uintptr_t{0x12345001});
    CHECK(!must_wait(actor.state(), false));
    CHECK(must_wait(actor.state(), true));
    put(actor.layer[0], 0x18, uintptr_t{0x12346000});
    CHECK(!must_wait(actor.state(), true));
    // No sticky skip state: independent native action restore re-enables phase.
    put(actor.layer[0], 0x10, uintptr_t{0});
    CHECK(must_wait(actor.state(), true));
    put(actor.layer[0], 0x10, uintptr_t{0x12345001});
    CHECK(!must_wait(actor.state(), true));
    // The selector, rather than the presence of any ready layer, governs access.
    actor.selector[0x1B] = 1;
    CHECK(must_wait(actor.state(), true));
    put(actor.layer[1], 0x10, uintptr_t{0x87654001});
    put(actor.layer[1], 0x18, uintptr_t{0x87655000});
    put(actor.layer[0], 0x10, uintptr_t{0});
    CHECK(!must_wait(actor.state(), true));
    for (const uint8_t invalid : {uint8_t{2}, uint8_t{255}}) {
        actor.selector[0x1B] = invalid;
        CHECK(!actor.state().readable && must_wait(actor.state(), true));
    }
    actor.selector[0x1B] = 1;
    put(actor.cc, 0x90, uintptr_t{0});
    CHECK(!actor.state().readable);
    put(actor.cc, 0x90, uintptr_t{1});
    CHECK(!actor.state().readable);
    put(actor.cc, 0x30, uintptr_t{0});
    CHECK(!actor.state().readable);
    CHECK(!selected_action(0, read).readable);
    CHECK(!selected_action(1, read).readable);
    CHECK(!gad::remote_user_controlled(0x90100000, 0));
    CHECK(!gad::remote_user_controlled(0, 0x90100323));
    CHECK(!gad::remote_user_controlled(0x90100323, 0x90100323));
    CHECK(gad::remote_user_controlled(0x90100000, 0x90100323));
    {
        // Both guest crashes: selected layer 0 is empty, the action went to layer 1.
        using cdmp::game::phase3527::repaired_selector;
        Actor a;
        const auto cc = reinterpret_cast<uintptr_t>(a.cc.data());
        CHECK(repaired_selector(cc, read) == -1);  // nothing anywhere: nothing to point at
        put(a.layer[1], 0x10, uintptr_t{0x87654001});
        put(a.layer[1], 0x18, uintptr_t{0x87655000});
        CHECK(repaired_selector(cc, read) == 1);
        put(a.layer[1], 0x18, uintptr_t{0});
        CHECK(repaired_selector(cc, read) == -1);  // the other layer is not complete either
        put(a.layer[1], 0x18, uintptr_t{0x87655000});
        put(a.layer[0], 0x10, uintptr_t{0x12345001});
        CHECK(repaired_selector(cc, read) == 1);  // selected has an action but no playback
        put(a.layer[0], 0x18, uintptr_t{0x12346000});
        CHECK(repaired_selector(cc, read) == -1);  // a healthy selection is never changed
        a.selector[0x1B] = 1;
        CHECK(repaired_selector(cc, read) == -1);
        put(a.layer[1], 0x10, uintptr_t{0});
        CHECK(repaired_selector(cc, read) == 0);  // and it works in the other direction
        a.selector[0x1B] = 2;
        CHECK(repaired_selector(cc, read) == -1);  // an unknown index is left alone
        a.selector[0x1B] = 0;
        put(a.cc, 0x90, uintptr_t{0});
        CHECK(repaired_selector(cc, read) == -1);  // a missing layer object: leave it
        CHECK(repaired_selector(0, read) == -1);
    }
    {
        using cdmp::game::phase3527::listeners_unsafe;
        cdmp::game::phase3527::SelectedAction healthy{true, 0x1000, 0x2000}, empty{true, 0, 0};
        CHECK(!listeners_unsafe(true, 0x1000, healthy));
        CHECK(listeners_unsafe(true, 0, healthy));   // second crash: first element empty, selection fine
        CHECK(listeners_unsafe(true, 0x1000, empty));  // first crash: the selected layer is the empty one
        CHECK(listeners_unsafe(false, 0, cdmp::game::phase3527::SelectedAction{}));
        CHECK(!listeners_unsafe(false, 0, healthy));  // no readable layer 0 is not, by itself, an empty one
    }
    using cdmp::game::phase3527::user_requires_readiness;
    CHECK(!user_requires_readiness(0, 0));
    CHECK(!user_requires_readiness(0, 0x90100323));
    CHECK(user_requires_readiness(0x90100323, 0));
    CHECK(user_requires_readiness(0x90100000, 0));
    CHECK(!user_requires_readiness(0x90100323, 0x90100323));
    CHECK(user_requires_readiness(0x90100000, 0x90100323));
    std::printf("character phase readiness: %s (%d failures)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
