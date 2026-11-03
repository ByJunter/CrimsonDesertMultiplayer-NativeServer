// P8 review of 10-10, I4 (docs/research/pendientes/p8-revision-noche.md, "Correcciones"): safetyhook's
// trap handler as the project builds it (third_party/safetyhook-patch/os.windows.cpp). Upstream v0.7.0
// answered EVERY access violation on a page it had ever patched with EXCEPTION_CONTINUE_EXECUTION: a read
// of such a page made PAGE_NOACCESS by someone else, or a call into it while it is not executable, retried
// forever (this test then hangs and its watchdog ends it with exit code 3). With the patch:
//   - a read fault on a patched page reaches the caller's own __except (counted "read/write passed on");
//   - an execute fault on a patched page that is not executable, with no patch in progress, too;
//   - the hook itself still works (installed, called, removed).
// A real inline hook on a small function in a page of our own (RWX), never on the game.

#include <windows.h>

#include <safetyhook.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "mod/game/hook_traps.h"

namespace {

int g_failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

using Fn = int(__fastcall*)();

// mov eax, imm32 ; ret ; then int3 padding.
void put_fn(uint8_t* at, int value) {
    at[0] = 0xB8;
    std::memcpy(at + 1, &value, 4);
    at[5] = 0xC3;
    std::memset(at + 6, 0xCC, 10);
}

int __fastcall detour() { return 7; }

bool read_faults(const volatile uint8_t* p) {
    __try {
        (void)*p;
        return false;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                 : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
}

bool call_faults(Fn f, int& out) {
    __try {
        out = f();
        return false;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                 : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
}

DWORD WINAPI watchdog(LPVOID) {
    Sleep(20000);
    std::printf("FAILED: the trap handler retried a fault forever (watchdog after 20 s)\n");
    std::fflush(stdout);
    ExitProcess(3);
}

}  // namespace

int main() {
    CloseHandle(CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr));
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    CHECK(page != nullptr);
    if (!page) return 1;
    put_fn(page, 42);          // the hooked function
    put_fn(page + 0x200, 5);   // another function on the same page, never hooked
    const auto hooked = reinterpret_cast<Fn>(page);
    const auto other = reinterpret_cast<Fn>(page + 0x200);
    CHECK(hooked() == 42 && other() == 5);
    {
        auto hook = safetyhook::create_inline(reinterpret_cast<void*>(page), reinterpret_cast<void*>(&detour));
        CHECK(static_cast<bool>(hook));
        if (!hook) return 1;
        CHECK(hooked() == 7 && hook.call<int>() == 42 && other() == 5);
        const cdmp::game::HookTrapStats s0 = cdmp::game::hook_trap_stats();
        CHECK(s0.patching == 0);

        // 1. A read of the patched page while someone made it PAGE_NOACCESS: the caller's __except gets it.
        DWORD old = 0;
        CHECK(VirtualProtect(page, 4096, PAGE_NOACCESS, &old));
        CHECK(read_faults(page + 0x800));
        CHECK(VirtualProtect(page, 4096, PAGE_EXECUTE_READWRITE, &old));
        const cdmp::game::HookTrapStats s1 = cdmp::game::hook_trap_stats();
        CHECK(s1.passed_data == s0.passed_data + 1);

        // 2. A call into the patched page while it is not executable (no patch in progress): passed on too.
        CHECK(VirtualProtect(page, 4096, PAGE_READWRITE, &old));
        int r = 0;
        CHECK(call_faults(other, r));
        CHECK(VirtualProtect(page, 4096, PAGE_EXECUTE_READWRITE, &old));
        const cdmp::game::HookTrapStats s2 = cdmp::game::hook_trap_stats();
        CHECK(s2.passed_exec == s1.passed_exec + 1 && s2.retried == s1.retried);

        // The hook still works once the page is executable again.
        CHECK(hooked() == 7 && other() == 5);
        // hook goes out of scope: unhooked (trap_threads again; no fault left behind)
    }
    CHECK(hooked() == 42);
    CHECK(cdmp::game::hook_trap_stats().patching == 0);
    VirtualFree(page, 0, MEM_RELEASE);
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_hook_traps: all passed\n");
    return 0;
}
