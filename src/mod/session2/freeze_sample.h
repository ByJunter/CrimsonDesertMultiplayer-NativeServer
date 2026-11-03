#pragma once
// freeze_watchdog's stack sample of another thread (p8-impl-freeze-watchdog.md §2), header-only so the tests can
// run it on a real thread. While the thread is suspended: GetThreadContext and ONE memcpy of committed stack
// (rsp .. min(rsp + cap, StackBase), and only if StackLimit <= rsp < StackBase) into the caller's buffer. No
// allocation, no lock, no log, no SEH there: the suspended thread may hold the heap's or the loader's lock.

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cdmp::session2::fwd {

struct StackSample {
    bool ok = false;
    const char* fail = nullptr;
    DWORD err = 0;
    uint64_t rip = 0, rsp = 0;
    size_t copied = 0;
};

// `h`: THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT. `limit` / `base`: the thread's NT_TIB StackLimit / StackBase,
// read just before (0: no copy, only the registers).
inline StackSample sample_stack(HANDLE h, uintptr_t limit, uintptr_t base, uint8_t* buf, size_t cap) noexcept {
    StackSample o;
    alignas(16) CONTEXT c{};
    c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (SuspendThread(h) == static_cast<DWORD>(-1)) {
        o.fail = "SuspendThread failed";
        o.err = GetLastError();
        return o;
    }
    const bool got = GetThreadContext(h, &c) != FALSE;  // also waits until the suspension took effect
    size_t n = 0;
    if (got && limit && base && c.Rsp >= limit && c.Rsp < base) {
        n = static_cast<size_t>(base - c.Rsp) < cap ? static_cast<size_t>(base - c.Rsp) : cap;
        std::memcpy(buf, reinterpret_cast<const void*>(c.Rsp), n);
    }
    ResumeThread(h);
    if (!got) {
        o.fail = "GetThreadContext failed";
        o.err = GetLastError();
        return o;
    }
    o.ok = true;
    o.rip = c.Rip;
    o.rsp = c.Rsp;
    o.copied = n;
    return o;
}

}  // namespace cdmp::session2::fwd
