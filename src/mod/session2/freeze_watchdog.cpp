#include "mod/session2/freeze_watchdog.h"

#include <windows.h>

#include <psapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <utility>

#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/render/dx12_hook.h"
#include "mod/session2/freeze_sample.h"
#include "mod/session2/freeze_watchdog_logic.h"

namespace cdmp::session2 {
namespace {

std::atomic<bool> g_wanted{false}, g_started{false};
bool g_guest = false;

// ---------------------------------------------------------------- the frame source (game's main thread)

fwd::MainThread g_main;  // ticks only from the first thread that ticked (fwd::MainThread)

// ---------------------------------------------------------------- the thread marks

fwd::MarkTable<256> g_marks;
thread_local fwd::ThreadMark* t_mark = nullptr;
thread_local bool t_mark_done = false;

fwd::ThreadMark* my_mark() noexcept {
    if (!t_mark_done) {
        t_mark_done = true;
        t_mark = g_marks.get_or_add(GetCurrentThreadId());
    }
    return t_mark;
}

// Hook ids: 1.. (0 = none). Wrapped mid hooks and registered inline detours share them.
constexpr size_t kIds = 192;
struct HookName {
    char text[64] = {};
};
std::array<HookName, kIds + 1> g_names{};
std::atomic<uint32_t> g_next_id{1};
std::array<safetyhook::MidHookFn, kIds + 1> g_fns{};  // by id, for the wrappers

uint16_t new_id(const char* tag, const char* what) {
    const uint32_t id = g_next_id.fetch_add(1);
    if (id > kIds) return 0;
    char* out = g_names[id].text;
    const int n = std::snprintf(out, sizeof g_names[id].text, "%s %s", tag ? tag : "?", what ? what : "?");
    if (n < 0) out[0] = 0;
    return static_cast<uint16_t>(id);
}

uint64_t tick_ms() noexcept { return GetTickCount64(); }
using Scope = fwd::MarkScope<uint64_t (*)() noexcept>;

template <size_t I>
void wrapped(safetyhook::Context& ctx) {
    constexpr auto id = static_cast<uint16_t>(I + 1);
    const Scope scope(my_mark(), id, &tick_ms);  // left by the destructor
    g_fns[id](ctx);
}
template <size_t... I>
constexpr std::array<safetyhook::MidHookFn, sizeof...(I)> make_wrappers(std::index_sequence<I...>) {
    return {&wrapped<I>...};
}
constexpr auto kWrappers = make_wrappers(std::make_index_sequence<kIds>{});

const char* name_of(uint16_t id) { return id && id <= kIds && g_names[id].text[0] ? g_names[id].text : "?"; }

// ---------------------------------------------------------------- modules and stubs (the watchdog's thread)

struct Range {
    uintptr_t base = 0, end = 0;
    char name[40] = {};
};
std::array<Range, 512> g_mods{};
size_t g_nmods = 0;
uintptr_t g_exe_base = 0, g_exe_end = 0, g_self_base = 0, g_self_end = 0;
std::array<Range, 256> g_stubs{};  // private executable regions (safetyhook's trampolines and mid-hook stubs)
size_t g_nstubs = 0;

// K32EnumProcessModules / GetModuleInformation / GetModuleBaseNameA read the loader's list without its lock.
void refresh_modules() {
    HMODULE list[512];
    DWORD need = 0;
    g_nmods = 0;
    if (!EnumProcessModules(GetCurrentProcess(), list, sizeof list, &need)) return;
    const size_t n = std::min<size_t>(need / sizeof(HMODULE), std::size(list));
    for (size_t i = 0; i < n && g_nmods < g_mods.size(); ++i) {
        MODULEINFO mi{};
        if (!GetModuleInformation(GetCurrentProcess(), list[i], &mi, sizeof mi)) continue;
        Range& r = g_mods[g_nmods++];
        r.base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
        r.end = r.base + mi.SizeOfImage;
        if (!GetModuleBaseNameA(GetCurrentProcess(), list[i], r.name, sizeof r.name)) strcpy_s(r.name, "?");
    }
}

void refresh_stubs() {
    g_nstubs = 0;
    uintptr_t a = 0x10000;
    MEMORY_BASIC_INFORMATION mbi{};
    for (int i = 0; i < 400000 && a < 0x7FFFFFFF0000ull; ++i) {
        if (VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof mbi) != sizeof mbi) break;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t end = base + mbi.RegionSize;
        constexpr DWORD kExec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && (mbi.Protect & kExec) && g_nstubs < g_stubs.size()) {
            g_stubs[g_nstubs++] = Range{base, end, "stub"};
        }
        if (end <= a) break;
        a = end;
    }
}

const Range* module_of(uintptr_t v) {
    for (size_t i = 0; i < g_nmods; ++i) {
        if (v >= g_mods[i].base && v < g_mods[i].end) return &g_mods[i];
    }
    return nullptr;
}
bool in_stub(uintptr_t v) {
    for (size_t i = 0; i < g_nstubs; ++i) {
        if (v >= g_stubs[i].base && v < g_stubs[i].end) return true;
    }
    return false;
}

std::string where(uintptr_t v) {
    if (v >= g_exe_base && v < g_exe_end) return std::format("exe+0x{:X}", v - g_exe_base);
    if (v >= g_self_base && v < g_self_end) return std::format("cdmp+0x{:X} [mod]", v - g_self_base);
    if (const Range* m = module_of(v)) return std::format("{}+0x{:X}", m->name, v - m->base);
    if (in_stub(v)) return std::format("0x{:X} [stub]", v);
    return std::format("0x{:X}", v);
}

// ---------------------------------------------------------------- the sample

struct ThreadBasicInfo {
    LONG exit_status;
    PVOID teb;
    HANDLE pid, tid;
    ULONG_PTR affinity;
    LONG priority, base_priority;
};
using NtQueryInformationThreadFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
NtQueryInformationThreadFn g_ntqit = nullptr;

struct Target {
    uint32_t tid = 0;
    HANDLE h = nullptr;
    uintptr_t teb = 0, stack_base = 0;
};
Target g_target;

bool target_for(uint32_t tid) {
    if (g_target.tid == tid && g_target.h) return true;
    if (g_target.h) CloseHandle(g_target.h);
    g_target = Target{};
    g_target.tid = tid;
    g_target.h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!g_target.h) return false;
    ThreadBasicInfo tbi{};
    if (g_ntqit && g_ntqit(g_target.h, 0, &tbi, sizeof tbi, nullptr) == 0) {
        g_target.teb = reinterpret_cast<uintptr_t>(tbi.teb);
        game::safe_read(g_target.teb + offsetof(NT_TIB, StackBase), &g_target.stack_base, sizeof g_target.stack_base);
    }
    return true;
}

constexpr size_t kStackCopy = 16384;
alignas(16) uint8_t g_stack[kStackCopy];

struct SampleOut {
    bool ok = false;
    const char* fail = nullptr;
    DWORD err = 0;
    uint64_t rip = 0, rsp = 0;
    size_t copied = 0;
    uint64_t suspended_us = 0;
};

uint64_t qpc_us() {
    static const uint64_t f = [] {
        LARGE_INTEGER q;
        QueryPerformanceFrequency(&q);
        return static_cast<uint64_t>(q.QuadPart);
    }();
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    const auto t = static_cast<uint64_t>(q.QuadPart);
    return t / f * 1000000ull + t % f * 1000000ull / f;
}

// While the thread is suspended (fwd::sample_stack): GetThreadContext and one memcpy of committed stack into a
// static buffer of the mod. No allocation, no lock, no log, no SEH (the thread may hold the heap's or the
// loader's lock).
SampleOut take_sample(uint32_t tid) {
    SampleOut o;
    if (!target_for(tid)) {
        o.fail = "OpenThread failed";
        o.err = GetLastError();
        return o;
    }
    uintptr_t limit = 0;
    if (g_target.teb) game::safe_read(g_target.teb + offsetof(NT_TIB, StackLimit), &limit, sizeof limit);
    const uint64_t t0 = qpc_us();
    const fwd::StackSample s = fwd::sample_stack(g_target.h, limit, g_target.stack_base, g_stack, kStackCopy);
    o.suspended_us = qpc_us() - t0;
    o.ok = s.ok;
    o.fail = s.fail;
    o.err = s.err;
    o.rip = s.rip;
    o.rsp = s.rsp;
    o.copied = s.copied;
    return o;
}

constexpr size_t kMaxReturns = 16;

// After the resume: stack values in the exe, cdmp.asi or a stub with a call right before them.
std::string returns_text(const SampleOut& o, bool& any_mod) {
    std::string s;
    size_t found = 0;
    for (size_t i = 0; i + 8 <= o.copied && found < kMaxReturns; i += 8) {
        uint64_t v = 0;
        std::memcpy(&v, g_stack + i, 8);
        const bool exe = v >= g_exe_base + 0x1000 && v < g_exe_end;
        const bool self = v >= g_self_base + 0x1000 && v < g_self_end;
        const bool stub = !exe && !self && in_stub(v);
        if (!exe && !self && !stub) continue;
        uint8_t before[7];
        if (!game::fast_read(v - 7, before, sizeof before) || !fwd::is_call_before(before)) continue;
        if (self || stub) any_mod = true;
        s += found ? ", " : "";
        s += where(v);
        ++found;
    }
    return found ? s : std::string("none found");
}

std::string hook_text(uint32_t tid) {
    const fwd::ThreadMark* m = g_marks.find(tid);
    if (!m) return "no mod hook seen on this thread";
    const uint64_t now = GetTickCount64();
    const uint16_t in = m->in.load(std::memory_order_relaxed);
    if (in) {
        return std::format("IN mod hook '{}' for {} ms", name_of(in), now - m->enter_ms.load(std::memory_order_relaxed));
    }
    const uint16_t last = m->last.load(std::memory_order_relaxed);
    if (!last) return "no mod hook seen on this thread";
    return std::format("not in a mod hook (last '{}', left {} ms ago)", name_of(last),
                       now - m->leave_ms.load(std::memory_order_relaxed));
}

// ---------------------------------------------------------------- the thread

enum class Source : uint8_t { None, Main, Present };

DWORD WINAPI watch_thread(LPVOID) {
    g_exe_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&watch_thread), &self);
    g_self_base = reinterpret_cast<uintptr_t>(self);
    MODULEINFO mi{};
    if (GetModuleInformation(GetCurrentProcess(), GetModuleHandleW(nullptr), &mi, sizeof mi)) {
        g_exe_end = g_exe_base + mi.SizeOfImage;
    }
    if (self && GetModuleInformation(GetCurrentProcess(), self, &mi, sizeof mi)) g_self_end = g_self_base + mi.SizeOfImage;
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll")) {
        g_ntqit = reinterpret_cast<NtQueryInformationThreadFn>(GetProcAddress(nt, "NtQueryInformationThread"));
    }
    // The module list and the stub regions are read here and refreshed only BETWEEN episodes (never while the
    // game is frozen: whatever lock the frozen thread holds, this thread does not touch the loader for it).
    // K32EnumProcessModules reads the loader's list through ReadProcessMemory without the loader lock [E].
    refresh_modules();
    refresh_stubs();
    log::info("FreezeWatch ON ({}): read only; main-loop frames from ui_pause's 0x35CD990 hook and guest_seq_play's "
              "0x8B6144 hook (the first thread that ticks them is the main thread), else Present; no frame for > {} "
              "ms: stack samples of that thread (SuspendThread + GetThreadContext + a copy of <= {} KB of its stack, "
              "nothing else while suspended), {} at {} ms then one per {} ms, <= {} suspensions per episode and {} per "
              "session | mid hooks marked: {}",
              g_guest ? "guest" : "host", fwd::kStallMs, kStackCopy / 1024, fwd::kFastSamples, fwd::kFastEveryMs,
              fwd::kSlowEveryMs, fwd::kMaxSamplesPerEpisode, fwd::kMaxSamplesPerSession,
              std::min<uint32_t>(g_next_id.load() - 1, kIds));

    fwd::FreezeWatch watch;
    Source src = Source::None;
    uint32_t tid = 0;
    const uint64_t t_start = GetTickCount64();
    uint64_t last_summary = t_start, longest = 0, last_refresh = t_start, foreign_seen = 0;
    uint32_t foreign_lines = 0;
    std::array<uint64_t, fwd::kMaxSamplesPerEpisode> rips{};
    size_t nrips = 0;
    bool episode_mod = false;
    for (;;) {
        Sleep(25);
        const uint64_t now = GetTickCount64();
        if (!watch.in_episode() && now - last_refresh >= 30000) {
            last_refresh = now;
            refresh_modules();
            refresh_stubs();
        }
        // A main-loop hook ticked from another thread: not counted (fwd::MainThread); said, a few times.
        if (const uint64_t f = g_main.foreign.load(std::memory_order_relaxed); f != foreign_seen) {
            foreign_seen = f;
            if (foreign_lines++ < 5) {
                log::warn("FreezeWatch: a main-loop hook ran on T{} (not the main thread T{}; {} such call(s)); "
                          "those are not counted as frames",
                          g_main.foreign_tid.load(), g_main.tid.load(), f);
            }
        }
        // The source: the main loop's hooks when they run; Present only if none has run for a minute.
        Source want = src;
        if (g_main.frames.load(std::memory_order_relaxed)) {
            want = Source::Main;
        } else if (src == Source::None && now - t_start > 60000 && render::status_presents()) {
            want = Source::Present;
        }
        if (want != src) {
            src = want;
            watch = fwd::FreezeWatch{};
            log::info("FreezeWatch: frame source {}", src == Source::Main
                                                          ? "the main loop's hooks (ui_pause 0x35CD990 / guest_seq_play "
                                                            "0x8B6144), their thread is sampled"
                                                          : "Present (no main-loop hook ran in 60 s: ui_pause_block=0 "
                                                            "and no guest_seq_play?); the present thread is sampled");
        }
        if (src == Source::None) continue;
        const uint64_t frames = src == Source::Main ? g_main.frames.load(std::memory_order_relaxed) : render::status_presents();
        if (!watch.in_episode()) {
            tid = src == Source::Main ? g_main.tid.load(std::memory_order_relaxed) : render::present_thread_id();
            nrips = 0;
            episode_mod = false;
        }
        // A Sample step is within the per-episode and per-session SUSPENSION caps (checked by the watch).
        const auto step = watch.observe(now, frames);
        if (step.kind == fwd::FreezeWatch::Kind::Sample && tid) {
            const SampleOut o = take_sample(tid);
            if (o.ok && nrips < rips.size()) rips[nrips++] = o.rip;
            if (!o.ok) {
                log::warn("FreezeWatch #{}.{} +{} ms T{}: sample failed ({}, error {})", step.episode, step.sample,
                          step.stalled_ms, tid, o.fail, o.err);
                continue;
            }
            bool any_mod = (o.rip >= g_self_base && o.rip < g_self_end) || in_stub(o.rip);
            const std::string rets = returns_text(o, any_mod);
            episode_mod = episode_mod || any_mod;
            log::warn("FreezeWatch #{}.{} +{} ms T{} ({}): rip {} rsp 0x{:X} ({} B of stack copied, suspended {} us) | "
                      "returns: {} | {}{}",
                      step.episode, step.sample, step.stalled_ms, tid, src == Source::Main ? "main loop" : "present",
                      where(o.rip), o.rsp, o.copied, o.suspended_us, rets, hook_text(tid),
                      any_mod ? " | MOD CODE on this stack ([mod]/[stub])" : "");
        } else if (step.kind == fwd::FreezeWatch::Kind::End) {
            longest = std::max(longest, step.stalled_ms);
            if (!step.log_end) continue;
            // The RIPs of the episode, most frequent first (up to 4).
            std::array<std::pair<uint64_t, uint32_t>, fwd::kMaxSamplesPerEpisode> cnt{};
            size_t nc = 0;
            for (size_t i = 0; i < nrips; ++i) {
                size_t j = 0;
                while (j < nc && cnt[j].first != rips[i]) ++j;
                if (j == nc) cnt[nc++] = {rips[i], 0};
                ++cnt[j].second;
            }
            std::sort(cnt.begin(), cnt.begin() + nc, [](const auto& a, const auto& b) { return a.second > b.second; });
            std::string top;
            for (size_t i = 0; i < nc && i < 4; ++i) top += std::format("{}{} x{}", i ? ", " : "", where(cnt[i].first), cnt[i].second);
            log::warn("FreezeWatch #{} END: T{} ({}) had no frame for {} ms; {} sample(s){}{}", step.episode, tid,
                      src == Source::Main ? "main loop" : "present", step.stalled_ms, step.samples,
                      top.empty() ? std::string() : " | rips: " + top, episode_mod ? " | mod code was on its stack" : "");
        }
        if (now - last_summary >= 600000) {
            last_summary = now;
            log::info("FreezeWatch summary: source {}, frames {}, episodes {} (longest {} ms), suspensions {} (not "
                      "taken, over the session cap: {}), foreign main-loop ticks {}, thread marks full {}",
                      src == Source::Main ? "main loop" : "present", frames, watch.episodes(), longest,
                      watch.samples_taken(), watch.samples_skipped(), g_main.foreign.load(), g_marks.full());
        }
    }
}

}  // namespace

void freeze_watchdog_start(bool on, bool guest) {
    if (!on || g_started.exchange(true)) return;
    g_guest = guest;
    g_wanted.store(true);
    CloseHandle(CreateThread(nullptr, 0, watch_thread, nullptr, 0, nullptr));
}

bool freeze_watchdog_wanted() noexcept { return g_wanted.load(std::memory_order_relaxed); }

void freeze_watchdog_frame() noexcept { g_main.tick(GetCurrentThreadId()); }

safetyhook::MidHookFn freeze_watchdog_wrap_mid(safetyhook::MidHookFn fn, const char* tag, const char* what) {
    if (!fn || !freeze_watchdog_wanted()) return fn;
    const uint16_t id = new_id(tag, what);
    if (!id) return fn;
    g_fns[id] = fn;
    return kWrappers[id - 1];
}

uint16_t freeze_watchdog_register(const char* tag, const char* what) {
    return freeze_watchdog_wanted() ? new_id(tag, what) : 0;
}

FreezeMark::FreezeMark(uint16_t id) noexcept : id_(id) {
    if (!id_) return;
    if (fwd::ThreadMark* m = my_mark()) {
        mark_ = m;
        prev_ = fwd::mark_enter(*m, id_, GetTickCount64());
    }
}

FreezeMark::~FreezeMark() {
    if (mark_) fwd::mark_leave(*static_cast<fwd::ThreadMark*>(mark_), id_, prev_, GetTickCount64());
}

}  // namespace cdmp::session2
