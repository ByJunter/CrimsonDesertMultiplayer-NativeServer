#include "mod/game/game_fault.h"

#include <windows.h>

#include <atomic>

#include "mod/log.h"

namespace cdmp::game {
namespace {
std::atomic<uint32_t> g_passed{0};
constexpr uint64_t kImageSpan = 0x20000000;  // the exe's mapped size is below this (build 2976)
}  // namespace

FaultFacts fault_facts(const _EXCEPTION_POINTERS* ep) noexcept {
    FaultFacts f;
    f.base = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
    f.image_size = kImageSpan;
    f.thread = GetCurrentThreadId();
    if (!ep || !ep->ExceptionRecord) return f;
    const EXCEPTION_RECORD* r = ep->ExceptionRecord;
    f.code = r->ExceptionCode;
    f.address = reinterpret_cast<uint64_t>(r->ExceptionAddress);
    f.nparams = r->NumberParameters;
    f.p0 = r->NumberParameters > 0 ? r->ExceptionInformation[0] : 0;
    f.p1 = r->NumberParameters > 1 ? r->ExceptionInformation[1] : 0;
    return f;
}

int game_fault_continue(const _EXCEPTION_POINTERS* ep, const char* who, const char* where) noexcept {
    g_passed.fetch_add(1, std::memory_order_relaxed);
    FixedLine l;
    describe_fault(l, who, where, fault_facts(ep));
    l.str(": NOT handled by the mod (the game's code may hold a lock or a half-built state): passed on to the "
          "game's handlers as a real crash");
    log::emergency(log::Level::Error, l.view());
    return EXCEPTION_CONTINUE_SEARCH;
}

uint32_t game_faults_passed() noexcept { return g_passed.load(std::memory_order_relaxed); }

}  // namespace cdmp::game
