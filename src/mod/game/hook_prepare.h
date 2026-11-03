#pragma once
// Preparing an inline or mid hook on the game's exe (P8 session 26, docs/research/s25/montura-regresion.md
// sections 5 and 7.1).
//
// safetyhook gives an inline hook its 5-byte jump only when it finds trampoline memory within +-2 GB of the target;
// otherwise it moves 14 bytes (its "FF" fallback), which every installer of the mod refuses (bytes nobody verified),
// or the hook cannot be prepared at all (a relative jump at the target). In S26 step 1 the host's MountSync refused
// 0x2228260 that way once, never tried again, and the host published no mount all session. The search itself is
// fixed in the project's copy of safetyhook's allocator (third_party/safetyhook-patch/allocator.cpp); here:
//   - each attempt is InlineHook::create / MidHook::create with StartDisabled: nothing of the game is written;
//   - an attempt that failed, or whose relocated bytes the caller does not accept, is logged (WRN) with safetyhook's
//     error, a probe of the near memory now and what the allocator's last failed search examined, and repeated up to
//     kHookTries times, kHookRetryMs apart, on the CALLER's thread (module threads and the worker: never DllMain);
//   - the first call in the process logs the address space around the exe (where the trampolines can go), and so
//     does each failed attempt (at most kNeighbourhoodLines times in all).
// The caller keeps its own checks and refusal lines: it gets the last attempt exactly as safetyhook gave it.

#include <safetyhook.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace safetyhook {
// Defined in third_party/safetyhook-patch/allocator.cpp (built in place of upstream's file). See its comment for
// the layout of `out`: [failed searches, desired, window lo, window hi, size, steps, then 5 words per kept step].
size_t cdmp_near_last_failure(uint64_t* out, size_t n) noexcept;
}  // namespace safetyhook

namespace cdmp::game {

inline constexpr int kHookTries = 3;
inline constexpr uint32_t kHookRetryMs = 250;
inline constexpr int kNeighbourhoodLines = 4;

// After attempt `n` (1-based): another one? Only when it failed or was not accepted, and not past kHookTries.
inline bool hook_try_again(int n, bool prepared, bool accepted) { return !(prepared && accepted) && n < kHookTries; }

// The relocated bytes the caller verified (true: keep this attempt). Empty: any bytes are accepted.
using AcceptBytes = std::function<bool(const std::vector<uint8_t>&)>;

std::expected<safetyhook::InlineHook, safetyhook::InlineHook::Error> prepare_inline(uintptr_t target, void* detour,
                                                                                    const AcceptBytes& accept,
                                                                                    const char* tag, const char* what);
std::expected<safetyhook::MidHook, safetyhook::MidHook::Error> prepare_mid(uintptr_t target,
                                                                           safetyhook::MidHookFn fn,
                                                                           const AcceptBytes& accept, const char* tag,
                                                                           const char* what);

std::string inline_error_text(const safetyhook::InlineHook::Error& e);
std::string mid_error_text(const safetyhook::MidHook::Error& e);

// The patched allocator's failed near searches so far, and what the last one examined ("" when none failed).
uint64_t near_search_failures() noexcept;
std::string near_search_last_failure_text();

// The address space around this process' exe image within +-2 GB: free runs, the nearest 64 KB-aligned slot
// below / above the image, the regions right next to it. One text; read-only (VirtualQuery).
std::string exe_neighbourhood_text();

}  // namespace cdmp::game
