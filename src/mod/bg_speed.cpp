#include "mod/bg_speed.h"

#include <windows.h>

#include <format>
#include <string>

#include "mod/log.h"

#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif

namespace cdmp::bgspeed {
namespace {

// gdi32.dll exports (d3dkmthk.h); the enum is a plain int.
using SetClassFn = LONG(APIENTRY*)(HANDLE, int32_t);
using GetClassFn = LONG(APIENTRY*)(HANDLE, int32_t*);

constexpr DWORD kPollMs = 250;
constexpr uint64_t kReportMs = 60000;

HANDLE g_thread = nullptr;
HANDLE g_stop = nullptr;
SetClassFn g_set = nullptr;
GetClassFn g_get = nullptr;
Controller g_ctl;
std::string g_power;  // what the power-throttling opt-out did, for the status line

// EcoQoS ("efficiency mode") and the timer-resolution clamp Windows 11 applies to processes whose windows are hidden:
// off for this process, whatever the window state. The timer flag needs Windows 11; without it, the first one alone.
std::string power_throttling_opt_out() {
    PROCESS_POWER_THROTTLING_STATE s{};
    s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    s.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    s.StateMask = 0;  // controlled and off: never throttled
    if (SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof s)) {
        return "power throttling off (execution speed and timer resolution)";
    }
    const DWORD e1 = GetLastError();
    s.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    if (SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof s)) {
        return std::format("power throttling off (execution speed only; the timer flag failed, error {})", e1);
    }
    return std::format("power throttling opt-out FAILED (errors {} / {})", e1, GetLastError());
}

Fg foreground_now() {
    const HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    return classify(fg == nullptr, pid, GetCurrentProcessId());
}

void apply(int32_t cls) {
    const LONG st = g_set(GetCurrentProcess(), cls);
    const bool ok = st >= 0;
    const int32_t before = g_ctl.applied();
    g_ctl.result(cls, ok);
    if (ok) {
        // Every change is a focus switch: rare (a person switching windows), so one line each.
        log::info("BgSpeed: GPU class {} -> {} ({})", class_name(before), class_name(cls),
                  g_ctl.background() ? "another window has the foreground" : "this copy has the foreground");
    } else {
        log::warn("BgSpeed: setting the GPU class {} failed (NTSTATUS {:#010x}){}", class_name(cls),
                  static_cast<uint32_t>(st),
                  g_ctl.boost() == kUnknown ? "; no lower boost left" : std::format("; next try {}",
                                                                                     class_name(g_ctl.boost())));
        if (g_ctl.gave_up()) log::warn("BgSpeed: {} failures: no more attempts ({})", g_ctl.set_failed(), g_ctl.describe());
    }
}

DWORD WINAPI watch(LPVOID) {
    uint64_t last_report = GetTickCount64();
    for (;;) {
        const int32_t want = g_ctl.poll(foreground_now(), GetTickCount64());
        if (want != kUnknown) apply(want);
        if (GetTickCount64() - last_report >= kReportMs) {
            last_report = GetTickCount64();
            log::info("BgSpeed: {} | {}", g_ctl.describe(), g_power);
        }
        if (WaitForSingleObject(g_stop, kPollMs) == WAIT_OBJECT_0) break;
    }
    if (g_ctl.applied() != g_ctl.original() && g_set(GetCurrentProcess(), g_ctl.original()) >= 0) {
        log::info("BgSpeed: stopped; GPU class back to {}", class_name(g_ctl.original()));
    }
    return 0;
}

}  // namespace

void start(Gate g, const char* role_name) {
    switch (g) {
        case Gate::Off: return;
        case Gate::IgnoredNoRole:
            log::warn("BgSpeed: background_full_speed=1 is only for two copies on one PC (CDMP_ROLE host|guest); "
                      "CDMP_ROLE is not set: ignored");
            return;
        case Gate::NotInGame:
            log::info("BgSpeed: background_full_speed=1 but not inside the game (test host): nothing done");
            return;
        case Gate::On: break;
    }
    const HMODULE gdi = LoadLibraryW(L"gdi32.dll");
    g_set = gdi ? reinterpret_cast<SetClassFn>(GetProcAddress(gdi, "D3DKMTSetProcessSchedulingPriorityClass")) : nullptr;
    g_get = gdi ? reinterpret_cast<GetClassFn>(GetProcAddress(gdi, "D3DKMTGetProcessSchedulingPriorityClass")) : nullptr;
    g_power = power_throttling_opt_out();
    if (!g_set) {
        log::warn("BgSpeed ({}): gdi32 has no D3DKMTSetProcessSchedulingPriorityClass: only {}", role_name, g_power);
        return;
    }
    int32_t original = kUnknown;
    if (g_get) {
        int32_t c = kUnknown;
        if (g_get(GetCurrentProcess(), &c) >= 0) original = c;
    }
    g_ctl = Controller(original);
    log::info("BgSpeed ON ({}): no hook, no game memory. The copy without the foreground drops to 2-4 fps because its "
              "main thread waits for its own GPU fence (0x3E00790 <- 0x38D9F20 <- 0x3837640, build 2976) while the "
              "foreground copy fills the GPU, not because of a throttle of the game; the game's own focus handling "
              "(input off 0x2DFB1A0, Reflex/frame generation off 0x14A8D6D0) is left as it is. So: the foreground "
              "window is checked every {} ms and, while it is another process's, this process's GPU scheduling class "
              "(D3DKMTSetProcessSchedulingPriorityClass) is {} (original {}{}); {}",
              role_name, kPollMs, class_name(g_ctl.boost()), class_name(g_ctl.original()),
              original == kUnknown ? ", unreadable: Normal assumed" : "", g_power);
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = g_stop ? CreateThread(nullptr, 0, watch, nullptr, 0, nullptr) : nullptr;
    if (!g_thread) log::error("BgSpeed: the watch thread could not be started (error {})", GetLastError());
}

void stop() {
    if (!g_thread) return;
    SetEvent(g_stop);
    WaitForSingleObject(g_thread, 5000);
    CloseHandle(g_thread);
    CloseHandle(g_stop);
    g_thread = nullptr;
    g_stop = nullptr;
}

}  // namespace cdmp::bgspeed
