#pragma once

#include <cstdint>

namespace cdmp::game::phase3527 {

// The movement phase selects a layer through [[CC+30]+1B], then consumes
// current(+10) and playback(+18) without consulting the layer's chart.
// See guest_phase_3527.json and the captured guest_fault_57692_native.json.
struct SelectedAction {
    bool readable = false;
    uintptr_t current = 0;
    uintptr_t playback = 0;
};

// Before the authoritative local key is readable, only unready user-owned
// actors wait. Ready actors continue, and resolving the key excludes the local
// actor immediately. This closes the discovery-thread polling window.
inline bool user_requires_readiness(uint32_t controller, uint32_t local) noexcept {
    return controller != 0 && (local == 0 || controller != local);
}

template <class Read>
SelectedAction selected_action(uintptr_t cc, Read&& read) noexcept {
    uintptr_t selector = 0, layer = 0;
    uint8_t index = 0;
    SelectedAction out;
    if (!cc || !read(cc + 0x30, selector) || !selector ||
        !read(selector + 0x1B, index) || index > 1 ||
        !read(cc + 0x88 + 8 * index, layer) || !layer ||
        !read(layer + 0x10, out.current) || !read(layer + 0x18, out.playback)) return {};
    out.readable = true;
    return out;
}

inline bool must_wait(const SelectedAction& state, bool needs_playback) noexcept {
    return !state.readable || !state.current || (needs_playback && !state.playback);
}

// The selector has one writer, 2518607 in the same application, and it copies the
// message's index. On a client that never installed the actor's first action, a
// remote message can select an empty layer 0 while layer 1 holds the action (both
// guest crashes, 25191F4 and 22485D5). 49 native sites then read the selected
// current without a check, because in a single-player world it always exists.
// Returns the layer the selector should name, or -1 to leave it alone.
template <class Read>
int repaired_selector(uintptr_t cc, Read&& read) noexcept {
    uintptr_t selector = 0, layer = 0, current[2]{}, playback[2]{};
    uint8_t index = 0;
    if (!cc || !read(cc + 0x30, selector) || !selector || !read(selector + 0x1B, index) || index > 1) return -1;
    for (int i = 0; i < 2; ++i) {
        if (!read(cc + 0x88 + 8 * i, layer) || !layer || !read(layer + 0x10, current[i]) ||
            !read(layer + 0x18, playback[i])) return -1;
    }
    if (current[index] && playback[index]) return -1;
    const int other = 1 - index;
    return current[other] && playback[other] ? other : -1;
}

// After applying, 2517A80 tells the actor's components about it (25194C1..25197AC).
// Those listeners take the applied vector's first element and the selected layer
// for granted: Status 18C91A0 -> 22485D5 and Docking A7ADDC both read a null action
// on a remote copy whose layer 0 was never filled. They have nothing to react to
// yet, and the next application tells them again, so the block is skipped whole.
inline bool listeners_unsafe(bool layer0_readable, uintptr_t layer0_current, const SelectedAction& selected) noexcept {
    return (layer0_readable && !layer0_current) || !selected.readable || !selected.current;
}

// The 2517A80 per-layer blend loop never uses the selected playback's flag
// when that playback is absent. Skip only the preceding eight-byte flag
// load; retain the native action application, blend loop and cleanup.
template <class Context>
bool skip_null_playback_flag(Context& ctx, uintptr_t resume, bool eligible) noexcept {
    if (!eligible || ctx.rcx || !resume) return false;
    ctx.rdi = 0;
    ctx.rip = resume;
    return true;
}

// The native actor-vf188(5) branch uses the same flag in 251A170, with
// playback in RDX and the zero-extended flag in EBP instead of EDI.
template <class Context>
bool skip_null_playback_secondary(Context& ctx, uintptr_t resume, bool eligible) noexcept {
    if (!eligible || ctx.rdx || !resume) return false;
    ctx.rbp = 0;
    ctx.rip = resume;
    return true;
}

}  // namespace cdmp::game::phase3527
