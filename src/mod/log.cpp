#include "mod/log.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "mod/log_names.h"
#include "mod/log_ring.h"

namespace cdmp::log {
namespace {

// The ring's lock: an SRW lock (constant-initialized; held only by producers, for one memcpy of a line).
struct SrwLock {
    SRWLOCK l = SRWLOCK_INIT;
    void lock() noexcept { AcquireSRWLockExclusive(&l); }
    void unlock() noexcept { ReleaseSRWLockExclusive(&l); }
};

// 4 MB of the mod's own memory (.bss): ~20 000 lines of 200 bytes, i.e. minutes of the busiest guest
// log; P8 session 8's 8.8 s stall queued a few hundred lines.
constexpr size_t kRingBytes = 4u << 20;
LineRing<kRingBytes, SrwLock> g_ring;

constexpr DWORD kWriterPeriodMs = 25;          // the writer looks at the ring at least this often
constexpr uint64_t kWakeAt = kRingBytes / 4;   // above this, producers wake it at once
constexpr uint64_t kSlowWriteMs = 250;         // a write this slow is counted
constexpr uint64_t kSlowWriteNoteMs = 1000;    // and this slow gets its own WRN line
constexpr size_t kPassBytes = 1u << 20;        // one writer pass writes at most this (whole lines)...
constexpr uint64_t kPassBudgetMs = 50;         // ...and passes follow each other for at most this long
constexpr uint64_t kStopDrainMs = 2000;        // the writer's last drain when it is stopped
constexpr uint64_t kExitWaitMs = 100;          // DllMain at exit: how long a live writer may finish its pass

std::atomic<HANDLE> g_file{INVALID_HANDLE_VALUE};
std::atomic<bool> g_open{false};
std::atomic<bool> g_stop{false};
std::atomic<bool> g_exiting{false};  // process exit: the writer starts no new pass (flush_at_exit drains)
SRWLOCK g_open_lock = SRWLOCK_INIT;  // open() / close() only, never a producer or a consumer
HANDLE g_wake = nullptr;             // auto-reset event: "there is work" / "stop"
HANDLE g_writer = nullptr;           // the writer thread (open/close only)
std::atomic<DWORD> g_writer_tid{0};
std::atomic<bool> g_writer_alive{false};

std::atomic<uint64_t> g_writes{0}, g_write_errors{0}, g_slow_writes{0}, g_slowest_ms{0}, g_last_slow_ms{0};
std::atomic<uint64_t> g_last_slow_bytes{0};

struct OpenGuard {
    OpenGuard() { AcquireSRWLockExclusive(&g_open_lock); }
    ~OpenGuard() { ReleaseSRWLockExclusive(&g_open_lock); }
    OpenGuard(const OpenGuard&) = delete;
    OpenGuard& operator=(const OpenGuard&) = delete;
};

const char* level_tag(Level level) {
    switch (level) {
        case Level::Debug: return "DBG";
        case Level::Info: return "INF";
        case Level::Warn: return "WRN";
        case Level::Error: return "ERR";
    }
    return "???";
}

// Fixed-width decimal, no CRT call.
char* put_dec(char* p, unsigned v, int width) {
    for (int i = width - 1; i >= 0; --i) {
        p[i] = static_cast<char>('0' + v % 10);
        v /= 10;
    }
    return p + width;
}

// "[hh:mm:ss.mmm] TAG [T<tid>] " into `out` (>= 48 bytes), all on the stack; returns its length.
size_t make_prefix(char* out, Level level) noexcept {
    char* p = out;
    SYSTEMTIME t;
    GetLocalTime(&t);
    *p++ = '[';
    p = put_dec(p, t.wHour, 2);
    *p++ = ':';
    p = put_dec(p, t.wMinute, 2);
    *p++ = ':';
    p = put_dec(p, t.wSecond, 2);
    *p++ = '.';
    p = put_dec(p, t.wMilliseconds, 3);
    *p++ = ']';
    *p++ = ' ';
    for (const char* s = level_tag(level); *s; ++s) *p++ = *s;
    *p++ = ' ';
    *p++ = '[';
    *p++ = 'T';
    char digits[16];
    int nd = 0;
    unsigned long tid = GetCurrentThreadId();
    do {
        digits[nd++] = static_cast<char>('0' + tid % 10);
        tid /= 10;
    } while (tid && nd < 16);
    while (nd) *p++ = digits[--nd];
    *p++ = ']';
    *p++ = ' ';
    return static_cast<size_t>(p - out);
}

uint64_t now_ms() noexcept { return GetTickCount64(); }

// The file: whole chunks; a failed write drops that chunk (counted) rather than looping on it.
void write_bytes(HANDLE f, const char* p, size_t n) noexcept {
    while (n) {
        const DWORD chunk = n < kPassBytes ? static_cast<DWORD>(n) : static_cast<DWORD>(kPassBytes);
        DWORD written = 0;
        if (!WriteFile(f, p, chunk, &written, nullptr) || written == 0) {
            g_write_errors.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        p += written;
        n -= written;
    }
}

// One pass by whoever holds the drain role: at most kPassBytes, whole lines, written WITHOUT any lock.
size_t drain_once() noexcept {
    const auto r = g_ring.readable(kPassBytes, /*whole_lines=*/true);
    const size_t n = r.size();
    if (!n) return 0;
    const HANDLE f = g_file.load(std::memory_order_acquire);
    const uint64_t t0 = now_ms();
    write_bytes(f, r.a.p, r.a.n);
    if (r.b.n) write_bytes(f, r.b.p, r.b.n);
    const uint64_t dt = now_ms() - t0;
    g_ring.consume(r.end);
    g_writes.fetch_add(1, std::memory_order_relaxed);
    if (dt > g_slowest_ms.load(std::memory_order_relaxed)) g_slowest_ms.store(dt, std::memory_order_relaxed);
    if (dt >= kSlowWriteMs) {
        g_slow_writes.fetch_add(1, std::memory_order_relaxed);
        if (dt >= kSlowWriteNoteMs) {
            g_last_slow_ms.store(dt, std::memory_order_relaxed);
            g_last_slow_bytes.store(n, std::memory_order_relaxed);
        }
    }
    return n;
}

void drop_note_now() noexcept {
    if (!g_ring.note_due()) return;
    char prefix[64];
    const size_t n = make_prefix(prefix, Level::Warn);
    g_ring.push_drop_note(std::string_view(prefix, n));
}

// Passes for at most `budget_ms` (each one bounded), stopping as soon as the process exits.
void drain_for(uint64_t budget_ms) noexcept {
    const uint64_t t0 = now_ms();
    while (!g_exiting.load() && drain_once()) {
        if (now_ms() - t0 >= budget_ms) break;
    }
}

DWORD WINAPI writer_thread(LPVOID) {
    g_writer_tid.store(GetCurrentThreadId());
    for (;;) {
        const bool stop = g_stop.load(std::memory_order_acquire);
        if (!g_exiting.load() && g_ring.try_begin_drain()) {
            drain_for(stop ? kStopDrainMs : kPassBudgetMs);
            drop_note_now();
            drain_for(kPassBudgetMs);
            g_ring.end_drain();
        }
        // A write that took >= 1 s: one line of its own (queued like any other; nobody waited for it).
        if (const uint64_t slow = g_last_slow_ms.exchange(0, std::memory_order_relaxed)) {
            warn("Log writer: one write of {} bytes to the log file took {} ms; the threads that log did not wait "
                 "(their lines queued in the ring: {} KB at most so far, {} line(s) dropped in all)",
                 g_last_slow_bytes.load(std::memory_order_relaxed), slow, g_ring.high_water() / 1024,
                 g_ring.dropped_total());
        }
        if (stop) break;
        WaitForSingleObject(g_wake, kWriterPeriodMs);
    }
    // Stopped (close()): the writer owns the handle; closed here, never under a write in progress.
    if (!g_exiting.load()) {
        const HANDLE f = g_file.exchange(INVALID_HANDLE_VALUE, std::memory_order_acq_rel);
        if (f != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(f);
            CloseHandle(f);
        }
    }
    g_writer_alive.store(false, std::memory_order_release);
    return 0;
}

std::wstring g_current;  // the dated file of this run (open/close only, under g_open_lock)

std::wstring stamp_of(const SYSTEMTIME& t) {
    return names::stamp(t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

// The first free <stem>_<stamp>[_N]<ext> in `dir`.
constexpr unsigned kMaxSameSecond = 999;

bool exists_error(DWORD e) { return e == ERROR_FILE_EXISTS || e == ERROR_ALREADY_EXISTS; }

// `path` (cdmp.log) as it was left: a hard link of an earlier run's dated file is just removed (that file
// stays); a plain file (a mod before 10-10, or a copy) keeps its content under a dated name of its own time.
void retire_alias(const std::wstring& path, const std::wstring& dir, const std::wstring& stem, const std::wstring& ext) {
    const HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;  // none
    BY_HANDLE_FILE_INFORMATION info{};
    const bool have = GetFileInformationByHandle(h, &info) != FALSE;
    CloseHandle(h);
    if (have && info.nNumberOfLinks > 1) {
        DeleteFileW(path.c_str());
        return;
    }
    SYSTEMTIME st{};
    FILETIME local{};
    if (!have || !FileTimeToLocalFileTime(&info.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &st)) {
        GetLocalTime(&st);
    }
    // MoveFileExW without REPLACE_EXISTING: never onto another file; the next _N when the name is taken.
    const std::wstring stmp = stamp_of(st);
    for (unsigned n = 1; n <= kMaxSameSecond; ++n) {
        if (MoveFileExW(path.c_str(), (dir + names::dated(stem, stmp, n, ext)).c_str(), 0)) return;
        if (!exists_error(GetLastError())) return;  // in use elsewhere, or no right: left as it is
    }
}

// Deletes `p` only if no other handle has it open: an open with DELETE and NO sharing fails on any file another
// process (an earlier game still running) is writing; FILE_FLAG_DELETE_ON_CLOSE removes it when closed.
bool delete_if_unused(const std::wstring& p) {
    const HANDLE h = CreateFileW(p.c_str(), DELETE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

// Keeps the newest names::kKeepLogs dated logs of `stem` in `dir` (never another file: cdmp_packets_* and the
// old cdmp.N.log are not ours to delete), never this run's (`current`), never one open elsewhere.
void prune(const std::wstring& dir, const std::wstring& stem, const std::wstring& ext, const std::wstring& current) {
    std::vector<std::wstring> found;
    WIN32_FIND_DATAW fd{};
    const HANDLE h = FindFirstFileW((dir + stem + L"_*" + ext).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) found.emplace_back(fd.cFileName);
    } while (FindNextFileW(h, &fd) && found.size() < 4096);
    FindClose(h);
    for (const std::wstring& old : names::to_delete(std::move(found), stem, ext, names::kKeepLogs, current)) {
        delete_if_unused(dir + old);
    }
}

HANDLE create_dated(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring dir = slash == std::wstring::npos ? L"" : path.substr(0, slash + 1);
    const std::wstring file = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const size_t dot = file.rfind(L'.');
    const std::wstring stem = dot == std::wstring::npos ? file : file.substr(0, dot);
    const std::wstring ext = dot == std::wstring::npos ? L"" : file.substr(dot);
    retire_alias(path, dir, stem, ext);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const std::wstring stmp = stamp_of(now);
    // CREATE_NEW: a name is taken atomically or the next _N is tried; an existing file is never truncated.
    std::wstring dated;
    HANDLE f = INVALID_HANDLE_VALUE;
    for (unsigned n = 1; n <= kMaxSameSecond; ++n) {
        const std::wstring p = dir + names::dated(stem, stmp, n, ext);
        f = CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, CREATE_NEW,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            dated = p;
            break;
        }
        if (!exists_error(GetLastError())) break;
    }
    if (f == INVALID_HANDLE_VALUE) {
        // No dated name possible: `path` itself, APPENDED to (never truncated), and no alias.
        f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return f;
        g_current = path;
        return f;
    }
    // The alias: same file, second name (no copy). Fails harmlessly on a file system without hard links or if an
    // earlier cdmp.log could not be retired (still open elsewhere): the dated file is the log then.
    CreateHardLinkW(path.c_str(), dated.c_str(), nullptr);
    g_current = dated;
    prune(dir, stem, ext, dated.substr(dir.size()));
    return f;
}

}  // namespace

std::wstring current_path() {
    OpenGuard guard;
    return g_current;
}

bool open(const std::wstring& path) {
    OpenGuard guard;
    if (g_open.load()) return true;
    // A previous log is still being closed by a writer stuck in a write: not reopened under it.
    if (g_file.load() != INVALID_HANDLE_VALUE || g_writer_alive.load()) return false;
    if (g_writer) {
        CloseHandle(g_writer);
        g_writer = nullptr;
    }
    // One log per run, never overwritten (P8 session 10 lost the guest's log of its freezes to a second launch):
    // <stem>_YYYYMMDD-HHMMSS[_N]<ext>, the newest names::kKeepLogs kept, and `path` (cdmp.log) a hard link to it.
    const HANDLE f = create_dated(path);
    if (f == INVALID_HANDLE_VALUE) return false;
    // Whatever an earlier log left queued (a close that timed out) is not this file's.
    g_ring.consume(g_ring.pushed_bytes());
    if (!g_wake) g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_stop.store(false);
    g_file.store(f, std::memory_order_release);
    g_writer_alive.store(true);
    g_writer = CreateThread(nullptr, 0, writer_thread, nullptr, 0, nullptr);
    if (!g_writer) {
        // Without the writer nothing is ever written: no log rather than a log that blocks callers.
        g_writer_alive.store(false);
        g_file.store(INVALID_HANDLE_VALUE);
        CloseHandle(f);
        return false;
    }
    g_open.store(true, std::memory_order_release);
    return true;
}

bool is_open() noexcept { return g_open.load(std::memory_order_relaxed); }

void close() {
    OpenGuard guard;
    if (!g_open.load()) return;
    flush(3000);
    g_open.store(false, std::memory_order_release);  // later lines are not queued any more
    g_stop.store(true, std::memory_order_release);
    if (g_wake) SetEvent(g_wake);
    // The writer drains what is left (bounded) and closes the file itself; a writer stuck inside a
    // WriteFile closes it when that write returns (open() refuses until then).
    if (g_writer && WaitForSingleObject(g_writer, 3000) == WAIT_OBJECT_0) {
        CloseHandle(g_writer);
        g_writer = nullptr;
    }
}

void write(Level level, std::string_view text) {
    if (!g_open.load(std::memory_order_acquire)) return;
    char prefix[64];
    const size_t n = make_prefix(prefix, level);
    char note_prefix[64];
    size_t nn = 0;
    if (g_ring.note_due()) nn = make_prefix(note_prefix, Level::Warn);
    g_ring.push(std::string_view(prefix, n), text, std::string_view(note_prefix, nn));
    if (g_ring.queued_bytes() >= kWakeAt && g_wake) SetEvent(g_wake);
}

bool flush(uint32_t timeout_ms) noexcept {
    // Wait only: the caller never writes and never takes a lock (crash and fault paths call this; the
    // faulting thread may hold anything, and the disk may hang). If the writer is gone, nothing is waited for.
    const uint64_t target = g_ring.pushed_bytes();
    if (g_ring.consumed_bytes() >= target) return true;
    if (!g_writer_alive.load(std::memory_order_acquire) ||
        GetCurrentThreadId() == g_writer_tid.load(std::memory_order_relaxed)) {
        return false;
    }
    const uint64_t t0 = now_ms();
    for (;;) {
        if (g_wake) SetEvent(g_wake);
        if (g_ring.consumed_bytes() >= target) return true;
        if (now_ms() - t0 >= timeout_ms || !g_writer_alive.load(std::memory_order_acquire)) {
            return g_ring.consumed_bytes() >= target;
        }
        Sleep(1);
    }
}

void flush_at_exit() noexcept {
    // DLL_PROCESS_DETACH during process exit. Normally every other thread is already gone, but this does
    // not rely on it: the writer is told to start no new pass, gets kExitWaitMs to finish the one it may be
    // in, and the queue is then written from here WITHOUT any lock (the consumer side needs none; the tail
    // only moves forward). A writer killed (or stuck) in the middle of a write may leave a few lines twice.
    g_exiting.store(true);  // seq_cst, then draining(): against the writer's check-then-take
    const HANDLE f = g_file.load(std::memory_order_acquire);
    if (f == INVALID_HANDLE_VALUE) return;
    const uint64_t t0 = now_ms();
    while (g_ring.draining() && now_ms() - t0 < kExitWaitMs) Sleep(1);
    for (int pass = 0; pass < 8; ++pass) {
        const auto r = g_ring.readable(kRingBytes);
        if (!r.size()) break;
        write_bytes(f, r.a.p, r.a.n);
        if (r.b.n) write_bytes(f, r.b.p, r.b.n);
        g_ring.consume(r.end);
    }
    if (const uint64_t lost = g_ring.dropped_pending()) {
        char line[160];
        size_t n = make_prefix(line, Level::Warn);
        const char msg[] = "Log: lines were dropped before the process exit (the log ring was full); count: ";
        for (const char* s = msg; *s && n < sizeof line - 24; ++s) line[n++] = *s;
        char digits[24];
        int nd = 0;
        uint64_t v = lost;
        do {
            digits[nd++] = static_cast<char>('0' + v % 10);
            v /= 10;
        } while (v && nd < 24);
        while (nd && n < sizeof line - 2) line[n++] = digits[--nd];
        line[n++] = '\r';
        line[n++] = '\n';
        write_bytes(f, line, n);
    }
}

void emergency(Level level, std::string_view text) noexcept {
    const HANDLE f = g_file.load(std::memory_order_acquire);  // read once; never any lock here
    if (f == INVALID_HANDLE_VALUE) return;
    // What was queued before this line goes first if the writer manages within the (short) bound; this
    // thread only waits, it never writes the queue (p8-impl-log-async.md §1.2).
    flush(kEmergencyFlushMs);
    // [hh:mm:ss.mmm] TAG [T<tid>] text\r\n, all on the stack.
    char line[1024];
    char* p = line + make_prefix(line, level);
    const size_t room = static_cast<size_t>(line + sizeof line - p) - 2;
    const size_t n = text.size() < room ? text.size() : room;
    for (size_t i = 0; i < n; ++i) *p++ = text[i];
    *p++ = '\r';
    *p++ = '\n';
    DWORD written = 0;
    WriteFile(f, line, static_cast<DWORD>(p - line), &written, nullptr);
}

std::string describe() {
    return std::format("lines {}, dropped {}, queued {} KB (max {} KB of {} KB), writes {} (slowest {} ms, >= {} ms {}), "
                       "write errors {}",
                       g_ring.lines(), g_ring.dropped_total(), g_ring.queued_bytes() / 1024, g_ring.high_water() / 1024,
                       kRingBytes / 1024, g_writes.load(), g_slowest_ms.load(), kSlowWriteMs, g_slow_writes.load(),
                       g_write_errors.load());
}

}  // namespace cdmp::log
