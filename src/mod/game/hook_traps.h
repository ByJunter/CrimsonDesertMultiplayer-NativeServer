#pragma once
// What safetyhook's trap handler did (the project's patched copy,
// third_party/safetyhook-patch/os.windows.cpp; P8 review of 10-10, I4). For
// diagnostics only: a thread that faults on a page the mod once patched.

#include <cstdint>

namespace safetyhook {
// Defined in third_party/safetyhook-patch/os.windows.cpp (built in place of upstream's file).
void cdmp_trap_stats(uint64_t* out5) noexcept;
}  // namespace safetyhook

namespace cdmp::game {

struct HookTrapStats {
    uint64_t redirected = 0;   // execute faults moved into a trampoline (a thread inside bytes being patched)
    uint64_t retried = 0;      // execute faults retried on a page while it was being patched
    uint64_t passed_exec = 0;  // execute faults passed on (page not executable, no patch in progress)
    uint64_t passed_data = 0;  // read/write faults on a patched page passed on (upstream retried these forever)
    uint64_t patching = 0;     // patches in progress now
};

inline HookTrapStats hook_trap_stats() noexcept {
    uint64_t v[5] = {};
    safetyhook::cdmp_trap_stats(v);
    return {v[0], v[1], v[2], v[3], v[4]};
}

}  // namespace cdmp::game
