#pragma once
// Reads of game memory from the mod's worker thread. The game frees and
// reallocates objects on its own thread (loading screens, fast travel), so
// every pointer we follow may be stale. Each read checks that the pages are
// committed and readable, and is additionally wrapped in SEH so a race that
// unmaps memory between the check and the copy cannot crash the game.

#include <cstddef>
#include <cstdint>
#include <optional>

namespace cdmp::game {

bool safe_read(uintptr_t addr, void* out, size_t size);

template <class T>
std::optional<T> read(uintptr_t addr) {
    T v{};
    if (!safe_read(addr, &v, sizeof v)) return std::nullopt;
    return v;
}

// Reads a pointer at addr and checks it is a plausible user-mode address.
std::optional<uintptr_t> read_ptr(uintptr_t addr);

// True if the object at `obj` starts with the expected vtable pointer.
bool has_vtable(uintptr_t obj, uintptr_t expected_vtable);

// SEH-guarded direct read, WITHOUT VirtualQuery: for hot paths on the game's
// threads over heap objects (actors, components, sectors, the SAM map). Same
// address bounds as safe_read; an access violation or in-page error (a stale
// pointer) is caught and returns false. A PAGE_GUARD page it touches (a stale
// pointer into another thread's stack guard) has its guard put back by the
// filter and the read returns false, as safe_read would (P8 review I2); any
// other exception is passed on. Not meant for thread stacks all the same.
bool fast_read(uintptr_t addr, void* out, size_t size) noexcept;

// How often a read touched a guard page: re-armed / could not be re-armed.
struct GuardStats {
    uint64_t rearmed = 0, failed = 0;
};
GuardStats guard_stats() noexcept;

template <class T>
std::optional<T> fast(uintptr_t addr) noexcept {
    T v{};
    if (!fast_read(addr, &v, sizeof v)) return std::nullopt;
    return v;
}
// fast() of a pointer, with read_ptr's plausibility check.
std::optional<uintptr_t> fast_ptr(uintptr_t addr) noexcept;

// Read policies for code written once for both kinds of reads:
// rd<T>(a) -> std::optional<T>, ptr(a) -> std::optional<uintptr_t>.
struct SafeReads {
    template <class T>
    std::optional<T> rd(uintptr_t a) const {
        return read<T>(a);
    }
    std::optional<uintptr_t> ptr(uintptr_t a) const { return read_ptr(a); }
};
struct FastReads {
    template <class T>
    std::optional<T> rd(uintptr_t a) const noexcept {
        return fast<T>(a);
    }
    std::optional<uintptr_t> ptr(uintptr_t a) const noexcept { return fast_ptr(a); }
};

}  // namespace cdmp::game
