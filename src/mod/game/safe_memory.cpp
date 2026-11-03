#include "mod/game/safe_memory.h"

#include <windows.h>

#include <atomic>
#include <cstring>

namespace cdmp::game {
namespace {

constexpr uintptr_t kMinAddress = 0x10000;
constexpr uintptr_t kMaxAddress = 0x00007FFFFFFFFFFFull;

bool pages_readable(uintptr_t addr, size_t size) {
    uintptr_t cur = addr;
    const uintptr_t end = addr + size;
    while (cur < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(cur), &mbi, sizeof mbi) == 0) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        cur = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}

std::atomic<uint64_t> g_guard_rearmed{0}, g_guard_rearm_failed{0};

// The filter of the mod's own reads (P8 review I2, p8-revision-noche.md). A
// read of an unreadable address fails cleanly: an access violation or an
// in-page error. A read that touches a PAGE_GUARD page (e.g. the guard page of
// ANOTHER thread's stack, through a stale pointer) gets
// STATUS_GUARD_PAGE_VIOLATION (EXCEPTION_GUARD_PAGE), and the system has already removed the guard
// ("a guard page provides a one-shot alarm", Microsoft Learn, Creating Guard
// Pages): left so, that thread's stack can no longer grow and it later dies
// with an access violation somewhere unrelated. So the guard is put back on
// that page (its protection now, which is the old one without PAGE_GUARD, plus
// PAGE_GUARD) and the read fails as an unreadable one: the page ends as it was
// and the result is the one safe_read's VirtualQuery check gives (it refuses
// PAGE_GUARD pages without touching them). Anything else is not ours: passed on.
int read_filter(const EXCEPTION_POINTERS* ep) noexcept {
    const EXCEPTION_RECORD* r = ep ? ep->ExceptionRecord : nullptr;
    if (!r) return EXCEPTION_CONTINUE_SEARCH;
    switch (r->ExceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_IN_PAGE_ERROR: return EXCEPTION_EXECUTE_HANDLER;
        case EXCEPTION_GUARD_PAGE: {
            const auto addr = r->NumberParameters >= 2 ? reinterpret_cast<void*>(r->ExceptionInformation[1]) : nullptr;
            MEMORY_BASIC_INFORMATION mbi{};
            DWORD old = 0;
            if (addr && VirtualQuery(addr, &mbi, sizeof mbi) == sizeof mbi && mbi.State == MEM_COMMIT &&
                !(mbi.Protect & PAGE_GUARD) &&
                VirtualProtect(addr, 1, mbi.Protect | PAGE_GUARD, &old)) {
                g_guard_rearmed.fetch_add(1, std::memory_order_relaxed);
            } else {
                g_guard_rearm_failed.fetch_add(1, std::memory_order_relaxed);
            }
            return EXCEPTION_EXECUTE_HANDLER;
        }
        default: return EXCEPTION_CONTINUE_SEARCH;
    }
}

// Kept free of C++ objects with destructors so __try is allowed here.
bool guarded_copy(void* out, const void* src, size_t size) {
    __try {
        std::memcpy(out, src, size);
        return true;
    } __except (read_filter(GetExceptionInformation())) {
        return false;
    }
}

}  // namespace

bool safe_read(uintptr_t addr, void* out, size_t size) {
    if (addr < kMinAddress || addr > kMaxAddress - size) return false;
    if (!pages_readable(addr, size)) return false;
    return guarded_copy(out, reinterpret_cast<const void*>(addr), size);
}

std::optional<uintptr_t> read_ptr(uintptr_t addr) {
    auto p = read<uintptr_t>(addr);
    if (!p || *p < kMinAddress || *p > kMaxAddress) return std::nullopt;
    return p;
}

bool has_vtable(uintptr_t obj, uintptr_t expected_vtable) {
    auto vt = read<uintptr_t>(obj);
    return vt && *vt == expected_vtable;
}

bool fast_read(uintptr_t addr, void* out, size_t size) noexcept {
    if (addr < kMinAddress || size > kMaxAddress || addr > kMaxAddress - size) return false;
    __try {
        // Typed volatile loads for the usual sizes (one instruction, no CRT call).
        switch (size) {
            case 1: *static_cast<uint8_t*>(out) = *reinterpret_cast<const volatile uint8_t*>(addr); break;
            case 2: *static_cast<uint16_t*>(out) = *reinterpret_cast<const volatile uint16_t*>(addr); break;
            case 4: *static_cast<uint32_t*>(out) = *reinterpret_cast<const volatile uint32_t*>(addr); break;
            case 8: *static_cast<uint64_t*>(out) = *reinterpret_cast<const volatile uint64_t*>(addr); break;
            default:
                for (size_t i = 0; i < size; ++i) {
                    static_cast<uint8_t*>(out)[i] = reinterpret_cast<const volatile uint8_t*>(addr)[i];
                }
                break;
        }
        return true;
    } __except (read_filter(GetExceptionInformation())) {
        return false;
    }
}

GuardStats guard_stats() noexcept {
    return {g_guard_rearmed.load(std::memory_order_relaxed), g_guard_rearm_failed.load(std::memory_order_relaxed)};
}

std::optional<uintptr_t> fast_ptr(uintptr_t addr) noexcept {
    uintptr_t p = 0;
    if (!fast_read(addr, &p, sizeof p) || p < kMinAddress || p > kMaxAddress) return std::nullopt;
    return p;
}

}  // namespace cdmp::game
