#pragma once
// A thread that runs one job when kicked, or on its own after `period_ms`
// without a kick (docs/research/pendientes/p8-hilo-red.md).
//
// Why: the P8 guest's diagnostics (the loading state's wait-list walk, the S3
// history, the restore lines) ran inside the network thread's 5 s report and
// took 4-8 s each time during a field load; the network thread then pumped
// Steam once every ~6 s and the host timed the guest out. They now run here.
// kick() is a SetEvent: it never waits for the job, a lock or the worker, so
// the network thread can call it from its report at no cost. If the network
// thread itself is stuck, the job still runs every period_ms.
//
// Runs never overlap (one thread) and never come closer than min_gap_ms (a
// kick right after a timed run is folded into it). The thread is never joined
// inside the game (like the mod's other worker threads); stop() is for tests.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <utility>

namespace cdmp {

class KickWorker {
public:
    struct Config {
        uint32_t period_ms = 5500;  // a run at least this often, kicked or not
        uint32_t min_gap_ms = 2000; // no two runs closer than this
        int priority = THREAD_PRIORITY_BELOW_NORMAL;
    };
    using Job = std::function<void()>;

    KickWorker() = default;
    KickWorker(const KickWorker&) = delete;
    KickWorker& operator=(const KickWorker&) = delete;
    ~KickWorker() { stop(); }

    // Once; false if the thread could not be created (nothing runs then).
    bool start(Job job, Config config) {
        if (started_.exchange(true)) return thread_ != nullptr;
        job_ = std::move(job);
        config_ = config;
        HANDLE k = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!k) return false;
        kick_.store(k);
        thread_ = CreateThread(nullptr, 0, &KickWorker::entry, this, 0, nullptr);
        if (!thread_) return false;
        SetThreadPriority(thread_, config_.priority);
        return true;
    }

    // Never blocks: an event set (or nothing before start()).
    void kick() noexcept {
        kicks_.fetch_add(1, std::memory_order_relaxed);
        if (HANDLE k = kick_.load(std::memory_order_acquire)) SetEvent(k);
    }

    // Tests only: waits for the current run to end.
    void stop() {
        if (!thread_) return;
        stop_.store(true);
        SetEvent(kick_.load());
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_);
        CloseHandle(kick_.exchange(nullptr));
        thread_ = nullptr;
    }

    uint64_t runs() const { return runs_.load(std::memory_order_relaxed); }
    uint64_t kicks() const { return kicks_.load(std::memory_order_relaxed); }
    uint64_t timed_runs() const { return timed_runs_.load(std::memory_order_relaxed); }
    bool running() const { return thread_ != nullptr; }

private:
    static DWORD WINAPI entry(LPVOID self) {
        static_cast<KickWorker*>(self)->loop();
        return 0;
    }

    void loop() {
        ULONGLONG last = 0;
        while (!stop_.load()) {
            const DWORD w = WaitForSingleObject(kick_.load(), config_.period_ms);
            if (stop_.load()) return;
            const ULONGLONG now = GetTickCount64();
            if (last && now - last < config_.min_gap_ms) continue;  // folded into the run just made
            last = now;
            if (w == WAIT_TIMEOUT) timed_runs_.fetch_add(1, std::memory_order_relaxed);
            runs_.fetch_add(1, std::memory_order_relaxed);
            try {
                job_();
            } catch (...) {
            }
        }
    }

    Job job_;
    Config config_;
    std::atomic<HANDLE> kick_{nullptr};
    HANDLE thread_ = nullptr;
    std::atomic<bool> started_{false}, stop_{false};
    std::atomic<uint64_t> runs_{0}, kicks_{0}, timed_runs_{0};
};

}  // namespace cdmp
