// P8 session 10, freeze_watchdog (src/mod/session2/freeze_watchdog_logic.h;
// docs/research/pendientes/p8-impl-freeze-watchdog.md).
//
// SCOPE: offline logic only. The episode machine (start-up, the 300 ms stall, 5 samples 50 ms apart then one per
// second, the per-episode and per-session caps, the END with its duration), the "call right before" test of a
// return address on real x64 call encodings and on non-calls, and the per-thread hook marks (one mark per thread
// under concurrent registration, nesting, the table full); and freeze_sample.h's sample on a real thread of this exe
// (RIP, the stack copy, a return address found in it, the thread running on). Not the hooks or the game.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "mod/session2/freeze_sample.h"
#include "mod/session2/freeze_watchdog_logic.h"

using namespace cdmp::session2;
using fwd::FreezeWatch;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

// Polls every 25 ms from t0 to t1 with the counter fixed; returns the sample steps.
std::vector<FreezeWatch::Step> stall(FreezeWatch& w, uint64_t t0, uint64_t t1, uint64_t frames) {
    std::vector<FreezeWatch::Step> out;
    for (uint64_t t = t0; t <= t1; t += 25) {
        const auto s = w.observe(t, frames);
        if (s.kind == FreezeWatch::Kind::Sample) out.push_back(s);
        CHECK(s.kind != FreezeWatch::Kind::End);
    }
    return out;
}

void test_start_up() {
    FreezeWatch w;
    // Frames never advanced yet (start-up, title, a load before the first frame): no episode however long.
    CHECK(stall(w, 0, 10000, 0).empty());
    CHECK(!w.started() && w.episodes() == 0);
    // Frames advance: watched from now on.
    uint64_t t = 10025, f = 0;
    for (; t < 11000; t += 25) w.observe(t, ++f);
    CHECK(w.started());
}

void test_episode_samples_and_end() {
    FreezeWatch w;
    uint64_t t = 0, f = 0;
    for (; t <= 1000; t += 25) w.observe(t, ++f);
    const uint64_t last_frame = t - 25;
    // A 290 ms hiccup: nothing.
    CHECK(stall(w, t, last_frame + 300, f).empty());
    CHECK(w.observe(last_frame + 325, ++f).kind == FreezeWatch::Kind::None);
    // A 2.35 s freeze (P8 session 10 #1).
    const uint64_t frozen_at = last_frame + 325;
    const auto samples = stall(w, frozen_at + 25, frozen_at + 2350, f);
    // First sample once > 300 ms, then 50 ms apart (5 in all), then one per second: 325, 375, ..., 525, 1525.
    CHECK(samples.size() == 6);
    CHECK(samples.size() == 6 && samples[0].stalled_ms > 300 && samples[0].stalled_ms <= 325 && samples[0].sample == 1);
    CHECK(samples.size() == 6 && samples[1].stalled_ms - samples[0].stalled_ms == 50);
    CHECK(samples.size() == 6 && samples[4].stalled_ms - samples[3].stalled_ms == 50);
    CHECK(samples.size() == 6 && samples[5].stalled_ms - samples[4].stalled_ms == 1000);
    CHECK(samples.size() == 6 && samples[0].episode == 1 && samples[5].sample == 6);
    const auto end = w.observe(frozen_at + 2375, ++f);
    CHECK(end.kind == FreezeWatch::Kind::End && end.episode == 1 && end.stalled_ms == 2375 && end.samples == 6 &&
          end.log_end);
    CHECK(!w.in_episode());
    // Frames again: nothing more.
    CHECK(w.observe(frozen_at + 2400, ++f).kind == FreezeWatch::Kind::None);
}

void test_caps() {
    FreezeWatch w;
    uint64_t t = 0, f = 0;
    for (; t <= 500; t += 25) w.observe(t, ++f);
    // 14.4 s (session 10 #2): at most kMaxSamplesPerEpisode.
    const auto s = stall(w, t, t + 14400, f);
    CHECK(s.size() == 5 + 13);  // 5 fast (325..525 ms), then 1525, 2525, ..., 13525 ms: 13 slow ones
    // A 30 s freeze: capped at 20.
    FreezeWatch w2;
    for (t = 0; t <= 500; t += 25) w2.observe(t, ++f);
    CHECK(stall(w2, t, t + 30000, f).size() == fwd::kMaxSamplesPerEpisode);
    // Session budget on SUSPENSIONS (review of 10-10): past it no Sample step at all (nothing is suspended), the
    // skipped ones counted; END lines still come (their own cap) and say how many were taken in the episode.
    FreezeWatch w3;
    uint64_t frames = 1;
    t = 0;
    w3.observe(t, frames);
    w3.observe(t += 25, ++frames);
    uint32_t taken = 0, ends = 0, last_end_samples = 99;
    for (int ep = 0; ep < 400; ++ep) {
        taken += static_cast<uint32_t>(stall(w3, t + 25, t + 600, frames).size());  // 5 per episode while allowed
        t += 625;
        const auto e = w3.observe(t, ++frames);
        if (e.kind == FreezeWatch::Kind::End && e.log_end) {
            ++ends;
            last_end_samples = e.samples;
        }
    }
    CHECK(taken == fwd::kMaxSamplesPerSession && w3.samples_taken() == taken);
    CHECK(w3.samples_skipped() == 400 * 5 - taken);
    CHECK(ends == fwd::kMaxEpisodeLines);
    CHECK(last_end_samples == 0);  // episode 300: past the 600 suspensions (120 episodes x 5)
    CHECK(w3.episodes() == 400);
}

void test_call_before() {
    auto check = [](std::vector<uint8_t> tail, bool want) {
        uint8_t p[7] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        for (size_t i = 0; i < tail.size(); ++i) p[7 - tail.size() + i] = tail[i];
        return fwd::is_call_before(p) == want;
    };
    CHECK(check({0xE8, 0x10, 0x20, 0x30, 0x40}, true));               // call rel32
    CHECK(check({0xFF, 0xD0}, true));                                 // call rax
    CHECK(check({0x41, 0xFF, 0xD2}, true));                           // call r10
    CHECK(check({0xFF, 0x10}, true));                                 // call [rax]
    CHECK(check({0xFF, 0x50, 0x48}, true));                           // call [rax+0x48] (vf48)
    CHECK(check({0xFF, 0x14, 0xC8}, true));                           // call [rax+rcx*8]
    CHECK(check({0xFF, 0x54, 0xC8, 0x10}, true));                     // call [rax+rcx*8+0x10]
    CHECK(check({0x41, 0xFF, 0x50, 0x08}, true));                     // call [r8+8]
    CHECK(check({0xFF, 0x15, 0x01, 0x02, 0x03, 0x04}, true));         // call [rip+disp32]
    CHECK(check({0xFF, 0x90, 0x60, 0x01, 0x00, 0x00}, true));         // call [rax+0x160]
    CHECK(check({0xFF, 0x94, 0xC8, 0x60, 0x01, 0x00, 0x00}, true));   // call [rax+rcx*8+0x160]
    CHECK(check({0x48, 0xFF, 0x90, 0x60, 0x01, 0x00, 0x00}, true));   // REX.W call [rax+0x160]
    CHECK(check({0xFF, 0xE0}, false));                                // jmp rax (reg 4)
    CHECK(check({0xFF, 0xC0}, false));                                // inc eax (reg 0)
    CHECK(check({0x48, 0x8B, 0x45, 0xBF}, false));                    // mov rax,[rbp-0x41]
    CHECK(check({0x90}, false));
    CHECK(check({0xFF, 0x50}, false));  // call [rax+disp8] needs its displacement: 2 bytes is not it
}

void test_marks() {
    fwd::MarkTable<8> t;
    fwd::ThreadMark* a = t.get_or_add(100);
    CHECK(a && t.get_or_add(100) == a && t.find(100) == a && !t.find(200));
    // Nesting: an inner hook, then back to the outer one; leaving the outer one leaves "last".
    const uint16_t p0 = fwd::mark_enter(*a, 5, 1000);
    CHECK(p0 == 0 && a->in.load() == 5 && a->enter_ms.load() == 1000);
    const uint16_t p1 = fwd::mark_enter(*a, 9, 1010);
    CHECK(p1 == 5 && a->in.load() == 9 && a->enter_ms.load() == 1000);  // the outer entry time is kept
    fwd::mark_leave(*a, 9, p1, 1020);
    CHECK(a->in.load() == 5 && a->last.load() == 9);
    fwd::mark_leave(*a, 5, p0, 1030);
    CHECK(a->in.load() == 0 && a->last.load() == 5 && a->leave_ms.load() == 1030);
    // Concurrent registration: one mark per thread, the table full counted.
    fwd::MarkTable<64> big;
    std::vector<std::thread> ts;
    std::atomic<int> nulls{0};
    for (uint32_t i = 0; i < 80; ++i) {
        ts.emplace_back([&big, &nulls, i] {
            for (int k = 0; k < 100; ++k) {
                if (!big.get_or_add(1000 + i)) {
                    nulls.fetch_add(1);
                    break;
                }
            }
        });
    }
    for (auto& th : ts) th.join();
    int found = 0;
    for (uint32_t i = 0; i < 80; ++i) found += big.find(1000 + i) ? 1 : 0;
    CHECK(found == 64 && nulls.load() == 16 && big.full() == 16);
}

uint64_t g_fake_ms = 0;
uint64_t fake_clock() noexcept { return g_fake_ms; }

// The wrapper's mark is RAII (review of 10-10): left also when an exception goes through the hook; nested scopes.
void test_mark_scope() {
    using Scope = fwd::MarkScope<uint64_t (*)() noexcept>;
    fwd::MarkTable<4> t;
    fwd::ThreadMark* m = t.get_or_add(7);
    g_fake_ms = 100;
    try {
        const Scope outer(m, 3, &fake_clock);
        CHECK(m->in.load() == 3);
        {
            g_fake_ms = 110;
            const Scope inner(m, 4, &fake_clock);
            CHECK(m->in.load() == 4);
            g_fake_ms = 120;
            throw 1;
        }
    } catch (int) {
    }
    CHECK(m->in.load() == 0 && m->last.load() == 3 && m->leave_ms.load() == 120);
    // No mark (table full) or id 0: nothing happens.
    { const Scope none(nullptr, 5, &fake_clock); }
    { const Scope zero(m, 0, &fake_clock); CHECK(m->in.load() == 0); }
    CHECK(m->last.load() == 3);
}

// The main thread is the first one that ticks; another thread's ticks are not frames (review of 10-10).
void test_main_thread() {
    fwd::MainThread mt;
    mt.tick(100);
    mt.tick(100);
    mt.tick(200);  // e.g. a hook that some other thread also reaches
    mt.tick(100);
    CHECK(mt.tid.load() == 100 && mt.frames.load() == 3 && mt.foreign.load() == 1 && mt.foreign_tid.load() == 200);
    // Concurrent first ticks: exactly one thread wins, the others are foreign.
    fwd::MainThread race;
    std::vector<std::thread> ts;
    for (uint32_t i = 1; i <= 8; ++i) {
        ts.emplace_back([&race, i] {
            for (int k = 0; k < 1000; ++k) race.tick(i);
        });
    }
    for (auto& th : ts) th.join();
    CHECK(race.tid.load() >= 1 && race.tid.load() <= 8);
    CHECK(race.frames.load() == 1000 && race.foreign.load() == 7000);
}

// The sample itself on a real thread spinning inside this exe: RIP in the exe, a stack copy from rsp, and at least
// one value in it that is a return address into the exe (a call right before it); the thread runs on afterwards.
std::atomic<bool> g_spin_stop{false};
std::atomic<uintptr_t> g_spin_limit{0}, g_spin_base{0};
std::atomic<uint64_t> g_spin_count{0};

__declspec(noinline) void spin() {
    while (!g_spin_stop.load()) g_spin_count.fetch_add(1);
}
__declspec(noinline) void spin_runner() {
    const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    g_spin_limit.store(reinterpret_cast<uintptr_t>(tib->StackLimit));
    g_spin_base.store(reinterpret_cast<uintptr_t>(tib->StackBase));
    spin();
    g_spin_count.fetch_add(1);  // after the call: the return address is a real one
}

void test_sample_real_thread() {
    std::thread th(spin_runner);
    while (!g_spin_base.load() || g_spin_count.load() < 1000) std::this_thread::yield();
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, GetThreadId(th.native_handle()));
    CHECK(h != nullptr);
    const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(exe + dos->e_lfanew);
    const uintptr_t exe_end = exe + nt->OptionalHeader.SizeOfImage;
    static uint8_t buf[16384];
    const auto s = fwd::sample_stack(h, g_spin_limit.load(), g_spin_base.load(), buf, sizeof buf);
    CHECK(s.ok && !s.fail);
    CHECK(s.rip >= exe && s.rip < exe_end);
    CHECK(s.copied > 0 && s.copied <= sizeof buf && s.rsp >= g_spin_limit.load() && s.rsp < g_spin_base.load());
    size_t returns = 0;
    for (size_t i = 0; i + 8 <= s.copied; i += 8) {
        uint64_t v = 0;
        std::memcpy(&v, buf + i, 8);
        if (v < exe + 0x1000 || v >= exe_end) continue;
        uint8_t before[7];
        std::memcpy(before, reinterpret_cast<const void*>(v - 7), 7);
        if (fwd::is_call_before(before)) ++returns;
    }
    CHECK(returns >= 1);
    // Resumed: it still runs and stops when told.
    const uint64_t c0 = g_spin_count.load();
    while (g_spin_count.load() == c0) std::this_thread::yield();
    // No stack copy without the stack bounds.
    const auto r = fwd::sample_stack(h, 0, 0, buf, sizeof buf);
    CHECK(r.ok && r.copied == 0);
    g_spin_stop.store(true);
    th.join();
    CloseHandle(h);
}

}  // namespace

int main() {
    test_start_up();
    test_episode_samples_and_end();
    test_caps();
    test_call_before();
    test_marks();
    test_mark_scope();
    test_main_thread();
    test_sample_real_thread();
    if (g_failures) {
        std::printf("optd_freeze_watchdog: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("optd_freeze_watchdog: all passed\n");
    return 0;
}
