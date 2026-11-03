#pragma once
// Thread-safe ASYNCHRONOUS file logger (docs/research/pendientes/p8-impl-log-async.md).
//
// Callers (any thread, the game's too) never do I/O and never wait for the disk: a line is formatted
// (into a stack buffer when it fits in 1 KB, so most lines allocate nothing) and copied into a fixed
// ring of the mod (log_ring.h) under a lock held only for that copy. One writer thread of the mod
// writes the ring to the file. If the ring is full, the line is dropped and counted, and
// "Log: N line(s) dropped here" is written where they were lost. P8 session 8: a WriteFile of 8.8 s
// on Shadow held the old lock and stopped the network thread, the diagnostics worker and the game's
// present thread (p8-s8-congelacion.md §2).
//
// Crash and exit paths: emergency() first gives the writer a bounded time to write what is queued,
// then writes its own line directly; flush(ms) before any TerminateProcess of the mod; the process
// exit drains the ring from DllMain without any lock (flush_at_exit).

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace cdmp::log {

enum class Level { Debug, Info, Warn, Error };

// Opens a new log for this run: <stem>_YYYYMMDD-HHMMSS[_N]<ext> next to `path` (never overwrites an earlier
// run; the newest 10 are kept, log_names.h), with `path` itself (cdmp.log) a hard link to it. Starts the writer.
bool open(const std::wstring& path);
// The dated file of this run ("" before open()).
std::wstring current_path();
// Writes what is queued (bounded), stops the writer and closes the file (the test host's stop()).
void close();
bool is_open() noexcept;
// Queues one line (never waits for I/O; dropped and counted when the ring is full).
void write(Level level, std::string_view text);
// WAITS at most `timeout_ms` until every line queued before the call is written by the writer thread.
// The caller never writes the queue and never takes a lock (safe in exception filters and crash paths,
// where the faulting thread may hold anything and the disk may hang). If the writer is gone (or this is
// the writer), returns at once. True when everything was written.
bool flush(uint32_t timeout_ms) noexcept;
// DLL_PROCESS_DETACH during process exit: tells the writer to start no new pass, gives it a short bound to
// finish the one in progress, then writes the queued lines from here without any lock (the consumer side
// of the ring takes none; its tail only moves forward). A few lines may then appear twice, never torn.
void flush_at_exit() noexcept;
// For exception filters and crash paths (P8 review I1/M9): one line written with WriteFile only,
// WITHOUT allocating and without any lock (the faulting thread may hold anything; the kernel serializes
// writes on the synchronous handle). The prefix is formatted by hand on the stack. Before it, the queued
// lines get at most kEmergencyFlushMs to be written by the writer (so the order is usually kept).
void emergency(Level level, std::string_view text) noexcept;
// Per passed game fault (game_fault_continue): short, so a fault the game then handles costs ~nothing.
inline constexpr uint32_t kEmergencyFlushMs = 50;
// guest_s3's crash capture (its end) and its terminate line: together with kEmergencyFlushMs <= ~1 s.
inline constexpr uint32_t kCrashFlushMs = 400;
// Before the mod ends the process on purpose (the guest's goodbye without a host), not a fault path.
inline constexpr uint32_t kTerminateFlushMs = 1000;

// "lines N, dropped D, queued Q KB (max H KB), writes W (slowest X ms, >250 ms S), errors E".
std::string describe();

namespace detail {
inline constexpr size_t kStackLine = 1024;
template <class... Args>
void emit(Level level, std::format_string<Args...> fmt, Args&&... args) {
    if (!is_open()) return;
    char buf[kStackLine];
    const auto r = std::format_to_n(buf, sizeof buf, fmt, std::forward<Args>(args)...);
    if (r.size >= 0 && static_cast<size_t>(r.size) <= sizeof buf) {
        write(level, std::string_view(buf, static_cast<size_t>(r.size)));
    } else {
        // Longer than the stack buffer: formatted once more into a string (the arguments are only read).
        write(level, std::format(fmt, std::forward<Args>(args)...));
    }
}
}  // namespace detail

template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    detail::emit(Level::Info, fmt, std::forward<Args>(args)...);
}
template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    detail::emit(Level::Warn, fmt, std::forward<Args>(args)...);
}
template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    detail::emit(Level::Error, fmt, std::forward<Args>(args)...);
}

}  // namespace cdmp::log
