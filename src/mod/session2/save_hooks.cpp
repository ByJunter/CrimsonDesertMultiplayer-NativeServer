#include "mod/session2/save_hooks.h"

#include <windows.h>

#include <safetyhook.hpp>

#include <array>
#include <atomic>
#include <cwchar>
#include <format>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "common/save_guard.h"
#include "mod/log.h"
#include "mod/role_logic.h"
#include "mod/session2/host_save_export.h"

namespace cdmp::session2 {
namespace {

std::unique_ptr<saveguard::Guard> g_guard;
thread_local bool t_inside = false;  // our own logging may open files

SafetyHookInline g_create_w, g_create_a, g_create_2, g_delete_w, g_delete_a, g_move_ex_w,
    g_move_progress_w, g_copy_w, g_copy_ex_w, g_copy_2, g_attr_w, g_attr_a, g_rmdir_w, g_replace_w, g_mkdir_w,
    g_mkdir_a, g_mkdir_ex_w;
// save_guard=trace and redirect only: the queries the game makes around a save.
SafetyHookInline g_find_first_w, g_find_first_ex_w, g_get_attr_w, g_get_attr_ex_w;

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// As the A file APIs read their strings (the ANSI code page, or the OEM one after SetFileApisToOEM).
std::wstring widen(const char* a) {
    if (!a) return {};
    const UINT cp = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
    const int n = MultiByteToWideChar(cp, 0, a, -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(cp, 0, a, -1, w.data(), n);
    return w;
}

// Absolute form of what the caller passed (relative paths, "..").
std::wstring full_path(const wchar_t* p) {
    if (!p || !*p) return {};
    wchar_t small[MAX_PATH];
    DWORD n = GetFullPathNameW(p, MAX_PATH, small, nullptr);
    if (n == 0) return p;
    if (n < MAX_PATH) return std::wstring(small, n);
    std::vector<wchar_t> big(n + 1);
    n = GetFullPathNameW(p, n + 1, big.data(), nullptr);
    return n ? std::wstring(big.data(), n) : std::wstring(p);
}

void report(saveguard::Op op, const std::wstring& path) {
    const uint64_t n = g_guard->blocked();
    if (n <= 50 || n % 100 == 0) {
        log::warn("Session2 save guard: refused {} '{}' (refused so far: {})", saveguard::op_name(op),
                  narrow(path), n);
    }
}

// True = refuse. Cheap when the guard is off or the path is elsewhere. These
// run inside the game's file calls: nothing may throw out of them (an
// allocation failure lets the call through).
bool refuse_path(saveguard::Op op, const wchar_t* p) noexcept {
    if (!g_guard || !g_guard->active() || t_inside || !p) return false;
    t_inside = true;
    bool block = false;
    try {
        const std::wstring path = full_path(p);
        block = g_guard->block_path(op, path);
        if (block) report(op, path);
    } catch (...) {
    }
    t_inside = false;
    return block;
}

bool refuse_create(const wchar_t* p, DWORD access, DWORD disposition, DWORD flags) noexcept {
    if (!g_guard || !g_guard->active() || t_inside || !p) return false;
    t_inside = true;
    bool block = false;
    try {
        const std::wstring path = full_path(p);
        // OPEN_ALWAYS only creates when the file is missing: a read of an
        // existing save that way is still a read (review finding 17).
        const bool exists = disposition == OPEN_ALWAYS && saveguard::is_under(path, g_guard->dir()) &&
                            GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
        block = g_guard->block_create(path, access, disposition, flags, exists);
        if (block) report(saveguard::Op::Create, path);
    } catch (...) {
    }
    t_inside = false;
    return block;
}

bool refuse_create_a(const char* p, DWORD access, DWORD disposition, DWORD flags) noexcept {
    if (!p || !g_guard || !g_guard->active() || t_inside) return false;
    try {
        return refuse_create(widen(p).c_str(), access, disposition, flags);
    } catch (...) {
        return false;
    }
}

bool refuse_path_a(saveguard::Op op, const char* p) noexcept {
    if (!p || !g_guard || !g_guard->active() || t_inside) return false;
    try {
        return refuse_path(op, widen(p).c_str());
    } catch (...) {
        return false;
    }
}

HANDLE denied_handle() {
    SetLastError(ERROR_ACCESS_DENIED);
    return INVALID_HANDLE_VALUE;
}
BOOL denied_bool() {
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

// ---------------------------------------------------------------- save_guard=trace (session 24)
// Nothing is refused: the original runs first, then each operation whose
// path (any of them for move/copy/replace) is under the save folder gets one
// line, while the budget lasts. Reads and queries have a smaller share of it
// than writes, so the save list read at load cannot use up the save itself.

constexpr uint32_t kTraceLines = 5000;
constexpr uint32_t kTraceNonWriteLines = 2000;
std::atomic<bool> g_trace{false};
saveguard::TraceBudget g_budget{kTraceLines, kTraceNonWriteLines};
// The traced API this thread is inside while its original runs: a traced call
// made by it (CreateFileA -> CreateFileW, FindFirstFileW -> Ex) says so.
thread_local const char* t_outer = nullptr;

bool tracing() noexcept { return g_trace.load(std::memory_order_relaxed) && !t_inside; }

// What the original left behind, for its line.
struct Done {
    const char* api;
    const char* outer;  // the traced API this call was made from, or null
    DWORD error;        // GetLastError() right after the original
};

// Outside trace mode: only the original, as before session 24. In trace mode:
// the original with `api` marked as this thread's outer call, then
// `log_fn(result, done)` under the reentrancy guard; the caller gets the
// original's result and last error untouched (a failed line is dropped).
template <class R, class Call, class Log>
R call_traced(const char* api, Call&& call, Log&& log_fn) noexcept {
    if (!tracing()) return call();
    const char* outer = t_outer;
    t_outer = api;
    const R result = call();
    const DWORD error = GetLastError();
    t_outer = outer;
    t_inside = true;
    try {
        log_fn(result, Done{api, outer, error});
    } catch (...) {
    }
    t_inside = false;
    SetLastError(error);
    return result;
}

// Full path of `p` if it is under the save folder, else empty.
std::wstring traced_path(const wchar_t* p) {
    std::wstring path = full_path(p);
    return !path.empty() && saveguard::is_under(path, g_guard->dir()) ? path : std::wstring();
}

std::string quoted(const std::wstring& path) { return path.empty() ? "(none)" : "'" + narrow(path) + "'"; }

std::string handle_result(HANDLE h, DWORD error) {
    if (h == INVALID_HANDLE_VALUE) return std::format("INVALID_HANDLE_VALUE, error {}", error);
    return std::format("handle 0x{:X}", reinterpret_cast<uintptr_t>(h));
}

std::string bool_result(BOOL ok, DWORD error) { return ok ? "TRUE" : std::format("FALSE, error {}", error); }

// "Session2 save trace #N tid T <api> <body>[ (inside <outer>)]".
void emit(const Done& d, bool write, const std::string& body) {
    const saveguard::TraceBudget::Line line = g_budget.take(write);
    if (line.seq == 0) return;
    log::info("Session2 save trace #{} tid {} {} {}{}{}{}", line.seq, GetCurrentThreadId(), d.api, body,
              d.outer ? " (inside " : "", d.outer ? d.outer : "", d.outer ? ")" : "");
    if (line.last_non_write) {
        log::info("Session2 save trace: the {} lines for reads and queries are used up; from now on only writes",
                  kTraceNonWriteLines);
    }
    if (line.last) log::warn("Session2 save trace: cap of {} lines reached; nothing more is logged", kTraceLines);
}

void trace_create(const Done& d, HANDLE h, const std::wstring& path, DWORD access, DWORD share, DWORD disp,
                  DWORD flags) {
    if (path.empty()) return;
    // On success, CREATE_ALWAYS and OPEN_ALWAYS say whether the file was there.
    const bool told = h != INVALID_HANDLE_VALUE && (disp == CREATE_ALWAYS || disp == OPEN_ALWAYS);
    const bool existed = told && d.error == ERROR_ALREADY_EXISTS;
    const bool write = saveguard::create_may_write(access, disp, flags, existed);
    emit(d, write,
         std::format("{} {} access {} share {} disposition {} flags {} -> {}{}", write ? "WRITE" : "READ",
                     quoted(path), saveguard::access_text(access), saveguard::share_text(share),
                     saveguard::disposition_text(disp), saveguard::file_flags_text(flags), handle_result(h, d.error),
                     told ? (existed ? " (it existed)" : " (new file)") : ""));
}

// One path (delete, attributes, directories, queries). `args` may be empty.
void trace_one(const Done& d, bool write, const std::wstring& path, std::string_view args,
               const std::string& result) {
    if (path.empty()) return;
    emit(d, write, std::format("{}{}{} -> {}", quoted(path), args.empty() ? "" : " ", args, result));
}

// Move or copy: logged if either end is under the folder. A copy out of the
// folder only reads it; a move out of it changes it.
void trace_two(const Done& d, bool source_changes, const wchar_t* from, const wchar_t* to, std::string_view args,
               const std::string& result) {
    const std::wstring a = full_path(from), b = full_path(to);
    const bool a_in = !a.empty() && saveguard::is_under(a, g_guard->dir());
    const bool b_in = !b.empty() && saveguard::is_under(b, g_guard->dir());
    if (!a_in && !b_in) return;
    emit(d, b_in || source_changes, std::format("from {} to {} {} -> {}", quoted(a), quoted(b), args, result));
}

std::string find_result(HANDLE h, DWORD error, const void* data) {
    if (h == INVALID_HANDLE_VALUE || !data) return handle_result(h, error);
    const auto* fd = static_cast<const WIN32_FIND_DATAW*>(data);
    const std::wstring first(fd->cFileName, wcsnlen(fd->cFileName, MAX_PATH));
    return std::format("{} first {} attributes {}", handle_result(h, error), quoted(first),
                       saveguard::file_flags_text(fd->dwFileAttributes));
}

// The raw-string test that comes before GetFullPathNameW where every file the game touches passes (the trace's
// first try, without it, stalled the start-up: no frame in 3 minutes): only an absolute path naming "Pearl Abyss",
// or a relative one ("C:x" too), can be under the save folder.
template <class C>
bool maybe_save(const C* p) noexcept {
    if (!p || !*p) return false;
    const auto u = [](C c) { return static_cast<uint32_t>(static_cast<std::make_unsigned_t<C>>(c)); };
    const uint32_t first = u(p[0]) | 0x20;
    const bool drive = first >= 'a' && first <= 'z' && p[1] == C(':') && (p[2] == C('\\') || p[2] == C('/'));
    if (!(p[0] == C('\\') || p[0] == C('/') || drive)) return true;
    static constexpr char kName[] = "pearl abyss";
    for (const C* s = p; *s; ++s) {
        size_t i = 0;
        while (kName[i] && s[i] && (u(s[i]) < 0x80 ? u(s[i]) | 0x20 : u(s[i])) == static_cast<uint32_t>(kName[i])) {
            ++i;
        }
        if (!kName[i]) return true;
    }
    return false;
}

// ---------------------------------------------------------------- save_guard=redirect (session 24)
// Every path argument under the real save folder is rewritten to the same place under the sandbox (a copy of the
// whole folder made at install, outside Steam Auto-Cloud's folder) and the original runs with it: the game saves
// as usual and the real folder is never written. Other paths go through untouched, so the calls an original makes
// inside (CopyFileW -> CopyFileExW, SetFileAttributesA -> W) see the sandbox and pass. A path under the folder that
// cannot be rewritten (no memory) fails the call with ERROR_NOT_ENOUGH_MEMORY: it never reaches the real folder.

std::atomic<bool> g_redirect{false};
std::atomic<bool> g_guest_role{false};  // save_hooks_install_guest_role: the redirect goes to save_cdmp_guest
std::unique_ptr<saveguard::Redirector> g_redir;  // set before the hooks are enabled, never changed after
std::atomic<uint64_t> g_redir_writes{0}, g_redir_reads{0}, g_redir_queries{0}, g_redir_failed{0}, g_redir_lines{0};
constexpr uint64_t kRedirectLines = 200;  // "Session2 save redirect #N" lines, writes only
constexpr uint64_t kRedirectErrorLines = 20;

bool redirecting() noexcept { return g_redirect.load(std::memory_order_relaxed); }

// GetFullPathNameW of a path in a stack buffer (the heap only past MAX_PATH); the path itself if that fails.
class FullPath {
public:
    explicit FullPath(const wchar_t* p) noexcept : view_(p) {
        const DWORD n = GetFullPathNameW(p, MAX_PATH, small_, nullptr);
        if (n > 0 && n < MAX_PATH) {
            view_ = std::wstring_view(small_, n);
        } else if (n >= MAX_PATH) {
            big_.reset(new (std::nothrow) wchar_t[n + 1]);
            const DWORD m = big_ ? GetFullPathNameW(p, n + 1, big_.get(), nullptr) : 0;
            if (m > 0 && m <= n) {
                view_ = std::wstring_view(big_.get(), m);
            } else {
                failed_ = !big_;  // no memory: cannot tell where it points
            }
        }
    }
    std::wstring_view view() const { return view_; }
    bool failed() const { return failed_; }

private:
    wchar_t small_[MAX_PATH];
    std::unique_ptr<wchar_t[]> big_;
    std::wstring_view view_;
    bool failed_ = false;
};

// One path argument: what the original gets, and both spellings when it was under the folder.
struct Mapped {
    const wchar_t* given = nullptr;
    std::wstring real, sandbox;
    bool hit = false;     // under the real folder: the original gets `sandbox`
    bool failed = false;  // under it (or unknown) but not rewritten: the call fails
    // S24 review: a relative path outside the folder goes on resolved (`real`), so a SetCurrentDirectory on another
    // thread between this check and the call cannot turn it into a path under the real folder.
    bool resolved = false;
    const wchar_t* arg() const { return hit ? sandbox.c_str() : resolved ? real.c_str() : given; }
};

void map_arg(const wchar_t* p, Mapped& m) noexcept {
    m.given = p;
    if (!maybe_save(p)) return;
    const FullPath full(p);
    if (full.failed()) {
        m.failed = true;
        return;
    }
    if (!g_redir->under(full.view())) {
        const bool absolute =
            p[0] == L'\\' || p[0] == L'/' || (p[0] && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/'));
        if (absolute) return;
        try {
            m.real.assign(full.view());
            m.resolved = true;
        } catch (...) {
            m.failed = true;
        }
        return;
    }
    try {
        m.real.assign(full.view());
        m.sandbox = g_redir->map(full.view()).value();
        m.hit = true;
    } catch (...) {
        m.failed = true;
    }
}

enum class Kind : uint8_t { Write, Read, Query };

std::string redirect_arg_text(const char* label, const Mapped& m) {
    std::string s = *label ? std::string(label) + " " : std::string();
    if (m.hit) return s + "'" + narrow(m.real) + "' -> '" + narrow(m.sandbox) + "'";
    return s + (m.given ? "'" + narrow(std::wstring(m.given)) + "'" : std::string("(none)"));
}

// The original with each path argument under the folder rewritten (`call(args)` gets the paths to pass). If one
// was: `kind_of(mapped, result, error)` says what it was for the counts, and a write gets one line while
// kRedirectLines last, with `result_text(result, error)`. The original sees the caller's last error on entry and
// the caller gets the original's result and exactly its last error.
template <class R, size_t N, class Call, class KindOf, class Text>
R redirected(const char* api, const std::array<const wchar_t*, N>& paths, const std::array<const char*, N>& labels,
             R fail_value, Call&& call, KindOf&& kind_of, Text&& result_text) noexcept {
    const DWORD entry_error = GetLastError();
    std::array<Mapped, N> m;
    bool hit = false, failed = false;
    std::array<const wchar_t*, N> args{};
    for (size_t i = 0; i < N; ++i) {
        map_arg(paths[i], m[i]);
        hit = hit || m[i].hit;
        failed = failed || m[i].failed;
        args[i] = m[i].arg();
    }
    if (failed) {
        const uint64_t n = g_redir_failed.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= kRedirectErrorLines) {
            try {
                log::error("Session2 save redirect: {} has a path under the save folder that could not be rewritten "
                           "(no memory); the call fails (so far {})", api, n);
            } catch (...) {
            }
        }
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return fail_value;
    }
    SetLastError(entry_error);
    const R result = call(args);
    if (!hit) return result;
    const DWORD error = GetLastError();
    try {
        const Kind kind = kind_of(m, result, error);
        (kind == Kind::Write ? g_redir_writes : kind == Kind::Read ? g_redir_reads : g_redir_queries)
            .fetch_add(1, std::memory_order_relaxed);
        if (kind == Kind::Write) {
            const uint64_t n = g_redir_lines.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n <= kRedirectLines) {
                std::string text;
                for (size_t i = 0; i < N; ++i) text += (i ? ", " : "") + redirect_arg_text(labels[i], m[i]);
                log::info("Session2 save redirect #{} {} {} -> {}", n, api, text, result_text(result, error));
                if (n == kRedirectLines) {
                    log::warn("Session2 save redirect: cap of {} lines reached; the counts go on in the "
                              "'Session2:' line", kRedirectLines);
                }
            }
        }
    } catch (...) {
    }
    SetLastError(error);
    return result;
}

// The narrow APIs: a path under the folder is widened as the A API would and goes to the W original's
// trampoline as the sandbox path (counted once, under this API's name); any other path goes to the A original
// with the caller's own string.
template <class R, class CallW, class CallA, class KindOf, class Text>
R redirected_a(const char* api, const char* name, R fail_value, CallW&& call_w, CallA&& call_a, KindOf&& kind_of,
               Text&& result_text) noexcept {
    if (!maybe_save(name)) return call_a();
    const DWORD entry_error = GetLastError();
    std::wstring wide;
    try {
        wide = widen(name);
    } catch (...) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return fail_value;
    }
    SetLastError(entry_error);
    if (wide.empty()) return call_a();  // not convertible: the A original fails the same way
    const wchar_t* const given = wide.c_str();
    return redirected<R, 1>(
        api, {given}, {""}, fail_value,
        [&](const std::array<const wchar_t*, 1>& a) { return a[0] == given ? call_a() : call_w(a[0]); }, kind_of,
        result_text);
}

constexpr std::array<const char*, 1> kOne{""};
constexpr std::array<const char*, 2> kFromTo{"from", "to"};

constexpr auto kWrite = [](const auto&, auto, DWORD) { return Kind::Write; };
constexpr auto kQuery = [](const auto&, auto, DWORD) { return Kind::Query; };
// A copy, or a directory made from a template, changes only where it lands (the second path).
constexpr auto kWriteIfSecond = [](const auto& m, auto, DWORD) { return m[1].hit ? Kind::Write : Kind::Read; };

constexpr auto kBoolText = [](BOOL ok, DWORD error) { return bool_result(ok, error); };
constexpr auto kHandleText = [](HANDLE h, DWORD error) { return handle_result(h, error); };
constexpr auto kNoText = [](auto, DWORD) { return std::string(); };  // queries get no line

// CreateFile*: a write as create_may_write() says; on success OPEN_ALWAYS tells whether the file was there.
// P8 session 25 (guest_save_host.h): the guest slot's two files, as normalized sandbox paths. Set once, never
// freed (a detour may be reading it); the counts are of successful reads only.
struct Watch {
    std::wstring lobby, save;
};
std::atomic<const Watch*> g_watch{nullptr};
std::atomic<uint64_t> g_watch_lobby{0}, g_watch_save{0};

void watch_note(const Mapped& m) noexcept {
    const Watch* w = g_watch.load(std::memory_order_acquire);
    if (!w || !m.hit) return;
    try {
        const std::wstring n = saveguard::normalize(m.sandbox);
        if (n == w->lobby) {
            g_watch_lobby.fetch_add(1, std::memory_order_relaxed);
        } else if (n == w->save) {
            g_watch_save.fetch_add(1, std::memory_order_relaxed);
        }
    } catch (...) {
    }
}

auto create_kind(DWORD access, DWORD disp, DWORD flags) {
    return [=](const auto& m, HANDLE h, DWORD error) {
        const bool existed = h != INVALID_HANDLE_VALUE && disp == OPEN_ALWAYS && error == ERROR_ALREADY_EXISTS;
        const Kind kind = saveguard::create_may_write(access, disp, flags, existed) ? Kind::Write : Kind::Read;
        if (kind == Kind::Read && h != INVALID_HANDLE_VALUE) watch_note(m[0]);
        return kind;
    };
}

// ---------------------------------------------------------------- the sandbox (redirect, at install)
// Made before any hook exists, with the plain APIs.

struct CopyStats {
    uint64_t files = 0, dirs = 0, bytes = 0, time_failures = 0;
};

bool is_dots(const wchar_t* n) { return n[0] == L'.' && (n[1] == 0 || (n[1] == L'.' && n[2] == 0)); }

// The creation, access and write times of `fd` onto `path` (a file or a directory).
bool copy_times(const std::wstring& path, const WIN32_FIND_DATAW& fd) {
    HANDLE h = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const BOOL ok = SetFileTime(h, &fd.ftCreationTime, &fd.ftLastAccessTime, &fd.ftLastWriteTime);
    CloseHandle(h);
    return ok != FALSE;
}

// Deletes `root` and everything in it. A link (reparse point) is removed, never followed. Never anything that is
// the real save folder, inside it or around it.
bool delete_tree(const std::wstring& root, const std::wstring& save_dir) {
    if (saveguard::is_under(root, save_dir) || saveguard::is_under(save_dir, root)) return false;
    const DWORD attrs = GetFileAttributesW(root.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        const DWORD e = GetLastError();
        return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND;  // nothing to delete
    }
    if (attrs & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(root.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) return DeleteFileW(root.c_str()) != FALSE;
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) return RemoveDirectoryW(root.c_str()) != FALSE;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileExW((root + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    bool ok = true;
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (!is_dots(fd.cFileName)) ok = delete_tree(root + L"\\" + fd.cFileName, save_dir) && ok;
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    return RemoveDirectoryW(root.c_str()) && ok;
}

// Copies everything in the folder `from` into the existing folder `to`, times included. False at the first
// failure (logged): a link in the save folder is a failure too (not followed).
bool copy_tree(const std::wstring& from, const std::wstring& to, CopyStats& st) {
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileExW((from + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (f == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND) return true;
        log::error("Session2 save redirect: cannot list '{}' (error {})", narrow(from), e);
        return false;
    }
    bool ok = true;
    do {
        if (is_dots(fd.cFileName)) continue;
        const std::wstring src = from + L"\\" + fd.cFileName, dst = to + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            log::error("Session2 save redirect: '{}' is a link (reparse point); not followed", narrow(src));
            ok = false;
        } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!CreateDirectoryExW(src.c_str(), dst.c_str(), nullptr)) {
                log::error("Session2 save redirect: cannot create '{}' (error {})", narrow(dst), GetLastError());
                ok = false;
            } else {
                ++st.dirs;
                ok = copy_tree(src, dst, st);
                // After its contents: adding them changed its write time.
                if (ok && !copy_times(dst, fd)) ++st.time_failures;
            }
        } else if (!CopyFileW(src.c_str(), dst.c_str(), TRUE)) {
            log::error("Session2 save redirect: cannot copy '{}' (error {})", narrow(src), GetLastError());
            ok = false;
        } else {
            ++st.files;
            st.bytes += (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            if (!copy_times(dst, fd)) ++st.time_failures;
        }
    } while (ok && FindNextFileW(f, &fd));
    const DWORD end = ok ? GetLastError() : static_cast<DWORD>(ERROR_NO_MORE_FILES);
    FindClose(f);
    if (end != ERROR_NO_MORE_FILES) {
        log::error("Session2 save redirect: the listing of '{}' stopped early (error {})", narrow(from), end);
        return false;
    }
    return ok;
}

// This game start's sandbox: the previous one (if any) becomes <sandbox>_prev (an older _prev is deleted first),
// then the whole real folder is copied into a new one. False if any step fails (logged); a new sandbox left half
// made is deleted, so the next start still finds the _prev.
// P8 S35 phase E (host_save_export.h): with host_save=export or probe the sandbox, its _prev and any folder set
// aside earlier are first archived into a verified generation outside the save tree. If that fails nothing is
// deleted: the sandbox is renamed <sandbox>_unexported_<gen> (archived at a later start) and _prev stays.
bool prepare_sandbox(const std::wstring& save_dir, const std::wstring& sandbox, CopyStats& st) {
    const std::wstring prev = sandbox + L"_prev";
    const bool have = GetFileAttributesW(sandbox.c_str()) != INVALID_FILE_ATTRIBUTES;
    const HostSaveArchive archived = host_save_archive(save_dir, sandbox);
    const hsv::RebuildPlan plan = hsv::plan_rebuild(have, archived.outcome, archived.asides_existing);
    if (plan.fall_back_to_block) {
        log::error("Session2 save redirect: the export failed and {} sandboxes are already set aside (the limit): "
                   "nothing is moved or deleted; this run falls back to block. Fix the export (see the FAILED line) or "
                   "move the cdmp_save_sandbox_unexported_* folders away by hand",
                   archived.asides_existing);
        return false;
    }
    if (plan.set_sandbox_aside) {
        const size_t slash = sandbox.find_last_of(L"\\/");
        const std::wstring aside = archived.aside_name.empty() || slash == std::wstring::npos
                                       ? std::wstring()
                                       : sandbox.substr(0, slash + 1) + archived.aside_name;
        if (aside.empty() || !MoveFileExW(sandbox.c_str(), aside.c_str(), 0)) {
            log::error("Session2 save redirect: the export failed and '{}' cannot be set aside as '{}' (error {}); "
                       "nothing is deleted",
                       narrow(sandbox), narrow(aside), GetLastError());
            return false;
        }
        log::warn("Session2 host save: '{}' set aside as '{}' (not exported, NOT deleted); '{}' kept as it was",
                  narrow(sandbox), narrow(aside), narrow(prev));
    } else if (plan.rotate_sandbox_to_prev) {
        if (plan.delete_prev && !delete_tree(prev, save_dir)) {
            log::error("Session2 save redirect: cannot delete the old '{}'", narrow(prev));
            return false;
        }
        if (!MoveFileExW(sandbox.c_str(), prev.c_str(), 0)) {
            log::error("Session2 save redirect: cannot rename '{}' to '{}' (error {})", narrow(sandbox),
                       narrow(prev), GetLastError());
            return false;
        }
    }
    // Folders set aside by an earlier failed export that are in a verified generation now.
    for (const std::wstring& done : archived.archived_aside) {
        if (delete_tree(done, save_dir)) {
            log::info("Session2 host save: '{}' is in a verified generation now: deleted", narrow(done));
        } else {
            log::warn("Session2 host save: '{}' is in a verified generation but could not be deleted", narrow(done));
        }
    }
    if (!CreateDirectoryW(sandbox.c_str(), nullptr)) {
        log::error("Session2 save redirect: cannot create '{}' (error {})", narrow(sandbox), GetLastError());
        return false;
    }
    if (!copy_tree(save_dir, sandbox, st)) {
        if (!delete_tree(sandbox, save_dir)) {
            log::error("Session2 save redirect: the half-made '{}' could not be deleted; delete it before the next "
                       "start (or the start after that loses '{}')", narrow(sandbox), narrow(prev));
        }
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA root{};
    if (GetFileAttributesExW(save_dir.c_str(), GetFileExInfoStandard, &root)) {
        WIN32_FIND_DATAW fd{};
        fd.ftCreationTime = root.ftCreationTime;
        fd.ftLastAccessTime = root.ftLastAccessTime;
        fd.ftLastWriteTime = root.ftLastWriteTime;
        if (!copy_times(sandbox, fd)) ++st.time_failures;
    }
    host_save_run_started(sandbox);
    return true;
}

// ---------------------------------------------------------------- detours

HANDLE WINAPI create_file_w(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                            DWORD flags, HANDLE tmpl) {
    if (redirecting()) {
        return redirected<HANDLE, 1>(
            "CreateFileW", {name}, kOne, INVALID_HANDLE_VALUE,
            [&](const auto& a) {
                return g_create_w.unsafe_stdcall<HANDLE>(a[0], access, share, sa, disp, flags, tmpl);
            },
            create_kind(access, disp, flags), kHandleText);
    }
    if (refuse_create(name, access, disp, flags)) return denied_handle();
    return call_traced<HANDLE>(
        "CreateFileW", [&] { return g_create_w.unsafe_stdcall<HANDLE>(name, access, share, sa, disp, flags, tmpl); },
        [&](HANDLE h, const Done& d) { trace_create(d, h, traced_path(name), access, share, disp, flags); });
}

HANDLE WINAPI create_file_a(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                            DWORD flags, HANDLE tmpl) {
    if (redirecting()) {
        return redirected_a<HANDLE>(
            "CreateFileA", name, INVALID_HANDLE_VALUE,
            [&](const wchar_t* w) {
                return g_create_w.unsafe_stdcall<HANDLE>(w, access, share, sa, disp, flags, tmpl);
            },
            [&] { return g_create_a.unsafe_stdcall<HANDLE>(name, access, share, sa, disp, flags, tmpl); },
            create_kind(access, disp, flags), kHandleText);
    }
    if (refuse_create_a(name, access, disp, flags)) return denied_handle();
    return call_traced<HANDLE>(
        "CreateFileA", [&] { return g_create_a.unsafe_stdcall<HANDLE>(name, access, share, sa, disp, flags, tmpl); },
        [&](HANDLE h, const Done& d) {
            trace_create(d, h, traced_path(widen(name).c_str()), access, share, disp, flags);
        });
}

HANDLE WINAPI create_file_2(LPCWSTR name, DWORD access, DWORD share, DWORD disp,
                            LPCREATEFILE2_EXTENDED_PARAMETERS ext) {
    const DWORD flags = ext ? ext->dwFileFlags | ext->dwFileAttributes : 0;
    if (redirecting()) {
        return redirected<HANDLE, 1>(
            "CreateFile2", {name}, kOne, INVALID_HANDLE_VALUE,
            [&](const auto& a) { return g_create_2.unsafe_stdcall<HANDLE>(a[0], access, share, disp, ext); },
            create_kind(access, disp, flags), kHandleText);
    }
    if (refuse_create(name, access, disp, flags)) return denied_handle();
    return call_traced<HANDLE>(
        "CreateFile2", [&] { return g_create_2.unsafe_stdcall<HANDLE>(name, access, share, disp, ext); },
        [&](HANDLE h, const Done& d) { trace_create(d, h, traced_path(name), access, share, disp, flags); });
}

BOOL WINAPI delete_file_w(LPCWSTR name) {
    if (redirecting()) {
        return redirected<BOOL, 1>(
            "DeleteFileW", {name}, kOne, FALSE, [&](const auto& a) { return g_delete_w.unsafe_stdcall<BOOL>(a[0]); },
            kWrite, kBoolText);
    }
    if (refuse_path(saveguard::Op::Delete, name)) return denied_bool();
    return call_traced<BOOL>(
        "DeleteFileW", [&] { return g_delete_w.unsafe_stdcall<BOOL>(name); },
        [&](BOOL ok, const Done& d) { trace_one(d, true, traced_path(name), {}, bool_result(ok, d.error)); });
}

BOOL WINAPI delete_file_a(LPCSTR name) {
    if (redirecting()) {
        return redirected_a<BOOL>(
            "DeleteFileA", name, FALSE, [&](const wchar_t* w) { return g_delete_w.unsafe_stdcall<BOOL>(w); },
            [&] { return g_delete_a.unsafe_stdcall<BOOL>(name); }, kWrite, kBoolText);
    }
    if (refuse_path_a(saveguard::Op::Delete, name)) return denied_bool();
    return call_traced<BOOL>(
        "DeleteFileA", [&] { return g_delete_a.unsafe_stdcall<BOOL>(name); },
        [&](BOOL ok, const Done& d) {
            trace_one(d, true, traced_path(widen(name).c_str()), {}, bool_result(ok, d.error));
        });
}

bool refuse_move(LPCWSTR from, LPCWSTR to) {
    return refuse_path(saveguard::Op::MoveFrom, from) || refuse_path(saveguard::Op::MoveTo, to);
}

BOOL WINAPI move_file_ex_w(LPCWSTR from, LPCWSTR to, DWORD flags) {
    if (redirecting()) {
        return redirected<BOOL, 2>(
            "MoveFileExW", {from, to}, kFromTo, FALSE,
            [&](const auto& a) { return g_move_ex_w.unsafe_stdcall<BOOL>(a[0], a[1], flags); }, kWrite, kBoolText);
    }
    if (refuse_move(from, to)) return denied_bool();
    return call_traced<BOOL>(
        "MoveFileExW", [&] { return g_move_ex_w.unsafe_stdcall<BOOL>(from, to, flags); },
        [&](BOOL ok, const Done& d) {
            trace_two(d, true, from, to, "flags " + saveguard::move_flags_text(flags), bool_result(ok, d.error));
        });
}

BOOL WINAPI move_file_progress_w(LPCWSTR from, LPCWSTR to, LPPROGRESS_ROUTINE cb, LPVOID data, DWORD flags) {
    if (redirecting()) {
        return redirected<BOOL, 2>(
            "MoveFileWithProgressW", {from, to}, kFromTo, FALSE,
            [&](const auto& a) { return g_move_progress_w.unsafe_stdcall<BOOL>(a[0], a[1], cb, data, flags); },
            kWrite, kBoolText);
    }
    if (refuse_move(from, to)) return denied_bool();
    return call_traced<BOOL>(
        "MoveFileWithProgressW", [&] { return g_move_progress_w.unsafe_stdcall<BOOL>(from, to, cb, data, flags); },
        [&](BOOL ok, const Done& d) {
            trace_two(d, true, from, to, "flags " + saveguard::move_flags_text(flags), bool_result(ok, d.error));
        });
}

BOOL WINAPI copy_file_w(LPCWSTR from, LPCWSTR to, BOOL fail_if_exists) {
    if (redirecting()) {
        return redirected<BOOL, 2>(
            "CopyFileW", {from, to}, kFromTo, FALSE,
            [&](const auto& a) { return g_copy_w.unsafe_stdcall<BOOL>(a[0], a[1], fail_if_exists); }, kWriteIfSecond,
            kBoolText);
    }
    if (refuse_path(saveguard::Op::Copy, to)) return denied_bool();
    return call_traced<BOOL>(
        "CopyFileW", [&] { return g_copy_w.unsafe_stdcall<BOOL>(from, to, fail_if_exists); },
        [&](BOOL ok, const Done& d) {
            trace_two(d, false, from, to, fail_if_exists ? "fail if exists 1" : "fail if exists 0",
                      bool_result(ok, d.error));
        });
}

BOOL WINAPI copy_file_ex_w(LPCWSTR from, LPCWSTR to, LPPROGRESS_ROUTINE cb, LPVOID data, LPBOOL cancel,
                           DWORD flags) {
    if (redirecting()) {
        return redirected<BOOL, 2>(
            "CopyFileExW", {from, to}, kFromTo, FALSE,
            [&](const auto& a) { return g_copy_ex_w.unsafe_stdcall<BOOL>(a[0], a[1], cb, data, cancel, flags); },
            kWriteIfSecond, kBoolText);
    }
    if (refuse_path(saveguard::Op::Copy, to)) return denied_bool();
    return call_traced<BOOL>(
        "CopyFileExW", [&] { return g_copy_ex_w.unsafe_stdcall<BOOL>(from, to, cb, data, cancel, flags); },
        [&](BOOL ok, const Done& d) {
            trace_two(d, false, from, to, std::format("flags 0x{:X}", flags), bool_result(ok, d.error));
        });
}

HRESULT WINAPI copy_file_2(PCWSTR from, PCWSTR to, COPYFILE2_EXTENDED_PARAMETERS* ext) {
    if (redirecting()) {
        return redirected<HRESULT, 2>(
            "CopyFile2", {from, to}, kFromTo, HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY),
            [&](const auto& a) { return g_copy_2.unsafe_stdcall<HRESULT>(a[0], a[1], ext); }, kWriteIfSecond,
            [](HRESULT hr, DWORD) { return std::format("HRESULT 0x{:08X}", static_cast<uint32_t>(hr)); });
    }
    if (refuse_path(saveguard::Op::Copy, to)) return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    return call_traced<HRESULT>(
        "CopyFile2", [&] { return g_copy_2.unsafe_stdcall<HRESULT>(from, to, ext); },
        [&](HRESULT hr, const Done& d) {
            trace_two(d, false, from, to, std::format("flags 0x{:X}", ext ? ext->dwCopyFlags : 0),
                      std::format("HRESULT 0x{:08X}", static_cast<uint32_t>(hr)));
        });
}

BOOL WINAPI set_attributes_w(LPCWSTR name, DWORD attrs) {
    if (redirecting()) {
        return redirected<BOOL, 1>(
            "SetFileAttributesW", {name}, kOne, FALSE,
            [&](const auto& a) { return g_attr_w.unsafe_stdcall<BOOL>(a[0], attrs); }, kWrite, kBoolText);
    }
    if (refuse_path(saveguard::Op::Attributes, name)) return denied_bool();
    return call_traced<BOOL>(
        "SetFileAttributesW", [&] { return g_attr_w.unsafe_stdcall<BOOL>(name, attrs); },
        [&](BOOL ok, const Done& d) {
            trace_one(d, true, traced_path(name), "attributes " + saveguard::file_flags_text(attrs),
                      bool_result(ok, d.error));
        });
}

BOOL WINAPI set_attributes_a(LPCSTR name, DWORD attrs) {
    if (redirecting()) {
        return redirected_a<BOOL>(
            "SetFileAttributesA", name, FALSE,
            [&](const wchar_t* w) { return g_attr_w.unsafe_stdcall<BOOL>(w, attrs); },
            [&] { return g_attr_a.unsafe_stdcall<BOOL>(name, attrs); }, kWrite, kBoolText);
    }
    if (refuse_path_a(saveguard::Op::Attributes, name)) return denied_bool();
    return call_traced<BOOL>(
        "SetFileAttributesA", [&] { return g_attr_a.unsafe_stdcall<BOOL>(name, attrs); },
        [&](BOOL ok, const Done& d) {
            trace_one(d, true, traced_path(widen(name).c_str()), "attributes " + saveguard::file_flags_text(attrs),
                      bool_result(ok, d.error));
        });
}

BOOL WINAPI remove_directory_w(LPCWSTR name) {
    if (redirecting()) {
        return redirected<BOOL, 1>(
            "RemoveDirectoryW", {name}, kOne, FALSE,
            [&](const auto& a) { return g_rmdir_w.unsafe_stdcall<BOOL>(a[0]); }, kWrite, kBoolText);
    }
    if (refuse_path(saveguard::Op::RemoveDir, name)) return denied_bool();
    return call_traced<BOOL>(
        "RemoveDirectoryW", [&] { return g_rmdir_w.unsafe_stdcall<BOOL>(name); },
        [&](BOOL ok, const Done& d) { trace_one(d, true, traced_path(name), {}, bool_result(ok, d.error)); });
}

// B1 (2026-10-08): a refused autosave left empty slot107_new/slot107_backup
// folders behind; the game makes the folder before writing into it.
BOOL WINAPI create_directory_w(LPCWSTR name, LPSECURITY_ATTRIBUTES sa) {
    if (redirecting()) {
        return redirected<BOOL, 1>(
            "CreateDirectoryW", {name}, kOne, FALSE,
            [&](const auto& a) { return g_mkdir_w.unsafe_stdcall<BOOL>(a[0], sa); }, kWrite, kBoolText);
    }
    if (refuse_path(saveguard::Op::CreateDir, name)) return denied_bool();
    return call_traced<BOOL>(
        "CreateDirectoryW", [&] { return g_mkdir_w.unsafe_stdcall<BOOL>(name, sa); },
        [&](BOOL ok, const Done& d) { trace_one(d, true, traced_path(name), {}, bool_result(ok, d.error)); });
}

BOOL WINAPI create_directory_a(LPCSTR name, LPSECURITY_ATTRIBUTES sa) {
    if (redirecting()) {
        return redirected_a<BOOL>(
            "CreateDirectoryA", name, FALSE, [&](const wchar_t* w) { return g_mkdir_w.unsafe_stdcall<BOOL>(w, sa); },
            [&] { return g_mkdir_a.unsafe_stdcall<BOOL>(name, sa); }, kWrite, kBoolText);
    }
    if (refuse_path_a(saveguard::Op::CreateDir, name)) return denied_bool();
    return call_traced<BOOL>(
        "CreateDirectoryA", [&] { return g_mkdir_a.unsafe_stdcall<BOOL>(name, sa); },
        [&](BOOL ok, const Done& d) {
            trace_one(d, true, traced_path(widen(name).c_str()), {}, bool_result(ok, d.error));
        });
}

BOOL WINAPI create_directory_ex_w(LPCWSTR templ, LPCWSTR name, LPSECURITY_ATTRIBUTES sa) {
    if (redirecting()) {
        return redirected<BOOL, 2>(
            "CreateDirectoryExW", {templ, name}, {"template", "new"}, FALSE,
            [&](const auto& a) { return g_mkdir_ex_w.unsafe_stdcall<BOOL>(a[0], a[1], sa); }, kWriteIfSecond,
            kBoolText);
    }
    if (refuse_path(saveguard::Op::CreateDir, name)) return denied_bool();
    return call_traced<BOOL>(
        "CreateDirectoryExW", [&] { return g_mkdir_ex_w.unsafe_stdcall<BOOL>(templ, name, sa); },
        [&](BOOL ok, const Done& d) {
            trace_one(d, true, traced_path(name), "template " + quoted(full_path(templ)), bool_result(ok, d.error));
        });
}

BOOL WINAPI replace_file_w(LPCWSTR replaced, LPCWSTR replacement, LPCWSTR backup, DWORD flags, LPVOID e1,
                           LPVOID e2) {
    if (redirecting()) {
        return redirected<BOOL, 3>(
            "ReplaceFileW", {replaced, replacement, backup}, {"replaced", "replacement", "backup"}, FALSE,
            [&](const auto& a) { return g_replace_w.unsafe_stdcall<BOOL>(a[0], a[1], a[2], flags, e1, e2); }, kWrite,
            kBoolText);
    }
    if (refuse_path(saveguard::Op::MoveTo, replaced) || refuse_path(saveguard::Op::MoveFrom, replacement) ||
        refuse_path(saveguard::Op::Copy, backup)) {
        return denied_bool();
    }
    return call_traced<BOOL>(
        "ReplaceFileW",
        [&] { return g_replace_w.unsafe_stdcall<BOOL>(replaced, replacement, backup, flags, e1, e2); },
        [&](BOOL ok, const Done& d) {
            const std::wstring a = full_path(replaced), b = full_path(replacement), c = full_path(backup);
            const std::wstring& dir = g_guard->dir();
            if (!(!a.empty() && saveguard::is_under(a, dir)) && !(!b.empty() && saveguard::is_under(b, dir)) &&
                !(!c.empty() && saveguard::is_under(c, dir))) {
                return;
            }
            emit(d, true,
                 std::format("replaced {} replacement {} backup {} flags 0x{:X} -> {}", quoted(a), quoted(b),
                             quoted(c), flags, bool_result(ok, d.error)));
        });
}

// Trace and redirect modes: the folder's listing and existence checks. These run for every file the game touches
// (the first try stalled the start-up: no frame in 3 minutes), so maybe_save() comes first.
HANDLE WINAPI find_first_file_w(LPCWSTR name, LPWIN32_FIND_DATAW data) {
    if (!maybe_save(name)) return g_find_first_w.unsafe_stdcall<HANDLE>(name, data);
    if (redirecting()) {
        return redirected<HANDLE, 1>(
            "FindFirstFileW", {name}, kOne, INVALID_HANDLE_VALUE,
            [&](const auto& a) { return g_find_first_w.unsafe_stdcall<HANDLE>(a[0], data); }, kQuery, kNoText);
    }
    return call_traced<HANDLE>(
        "FindFirstFileW", [&] { return g_find_first_w.unsafe_stdcall<HANDLE>(name, data); },
        [&](HANDLE h, const Done& d) { trace_one(d, false, traced_path(name), {}, find_result(h, d.error, data)); });
}

HANDLE WINAPI find_first_file_ex_w(LPCWSTR name, FINDEX_INFO_LEVELS level, LPVOID data, FINDEX_SEARCH_OPS op,
                                   LPVOID filter, DWORD flags) {
    if (!maybe_save(name)) return g_find_first_ex_w.unsafe_stdcall<HANDLE>(name, level, data, op, filter, flags);
    if (redirecting()) {
        return redirected<HANDLE, 1>(
            "FindFirstFileExW", {name}, kOne, INVALID_HANDLE_VALUE,
            [&](const auto& a) {
                return g_find_first_ex_w.unsafe_stdcall<HANDLE>(a[0], level, data, op, filter, flags);
            },
            kQuery, kNoText);
    }
    return call_traced<HANDLE>(
        "FindFirstFileExW",
        [&] { return g_find_first_ex_w.unsafe_stdcall<HANDLE>(name, level, data, op, filter, flags); },
        [&](HANDLE h, const Done& d) {
            trace_one(d, false, traced_path(name),
                      std::format("info level {} search op {} flags 0x{:X}", static_cast<int>(level),
                                  static_cast<int>(op), flags),
                      find_result(h, d.error, data));
        });
}

DWORD WINAPI get_attributes_w(LPCWSTR name) {
    if (!maybe_save(name)) return g_get_attr_w.unsafe_stdcall<DWORD>(name);
    if (redirecting()) {
        return redirected<DWORD, 1>(
            "GetFileAttributesW", {name}, kOne, INVALID_FILE_ATTRIBUTES,
            [&](const auto& a) { return g_get_attr_w.unsafe_stdcall<DWORD>(a[0]); }, kQuery, kNoText);
    }
    return call_traced<DWORD>(
        "GetFileAttributesW", [&] { return g_get_attr_w.unsafe_stdcall<DWORD>(name); },
        [&](DWORD attrs, const Done& d) {
            trace_one(d, false, traced_path(name), {},
                      attrs == INVALID_FILE_ATTRIBUTES ? std::format("INVALID_FILE_ATTRIBUTES, error {}", d.error)
                                                       : saveguard::file_flags_text(attrs));
        });
}

BOOL WINAPI get_attributes_ex_w(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info) {
    if (!maybe_save(name)) return g_get_attr_ex_w.unsafe_stdcall<BOOL>(name, level, info);
    if (redirecting()) {
        return redirected<BOOL, 1>(
            "GetFileAttributesExW", {name}, kOne, FALSE,
            [&](const auto& a) { return g_get_attr_ex_w.unsafe_stdcall<BOOL>(a[0], level, info); }, kQuery, kNoText);
    }
    return call_traced<BOOL>(
        "GetFileAttributesExW", [&] { return g_get_attr_ex_w.unsafe_stdcall<BOOL>(name, level, info); },
        [&](BOOL ok, const Done& d) {
            std::string result = bool_result(ok, d.error);
            if (ok && level == GetFileExInfoStandard && info) {
                const auto* fa = static_cast<const WIN32_FILE_ATTRIBUTE_DATA*>(info);
                result += std::format(" attributes {} size {}", saveguard::file_flags_text(fa->dwFileAttributes),
                                      (static_cast<uint64_t>(fa->nFileSizeHigh) << 32) | fa->nFileSizeLow);
            }
            trace_one(d, false, traced_path(name), {}, result);
        });
}

struct HookSpec {
    const char* name;
    void* detour;
    SafetyHookInline* hook;
    bool required;
    bool query;  // installed only with save_guard=trace or redirect (required with redirect)
};

// Creates and enables every hook of the mode (the queries only with trace or redirect). False = one that is required
// could not go in: the others are disabled again and the redirect is off (logged). `missing`: optional ones absent.
bool enable_hooks(bool trace, bool redirect, HMODULE kb, std::string& missing) {
    static const HookSpec specs[] = {
        {"CreateFileW", reinterpret_cast<void*>(&create_file_w), &g_create_w, true, false},
        {"CreateFileA", reinterpret_cast<void*>(&create_file_a), &g_create_a, true, false},
        {"CreateFile2", reinterpret_cast<void*>(&create_file_2), &g_create_2, true, false},
        {"DeleteFileW", reinterpret_cast<void*>(&delete_file_w), &g_delete_w, true, false},
        {"DeleteFileA", reinterpret_cast<void*>(&delete_file_a), &g_delete_a, false, false},
        {"MoveFileExW", reinterpret_cast<void*>(&move_file_ex_w), &g_move_ex_w, true, false},
        {"MoveFileWithProgressW", reinterpret_cast<void*>(&move_file_progress_w), &g_move_progress_w, false, false},
        {"CopyFileW", reinterpret_cast<void*>(&copy_file_w), &g_copy_w, false, false},
        {"CopyFileExW", reinterpret_cast<void*>(&copy_file_ex_w), &g_copy_ex_w, false, false},
        {"CopyFile2", reinterpret_cast<void*>(&copy_file_2), &g_copy_2, false, false},
        {"SetFileAttributesW", reinterpret_cast<void*>(&set_attributes_w), &g_attr_w, true, false},
        {"SetFileAttributesA", reinterpret_cast<void*>(&set_attributes_a), &g_attr_a, false, false},
        {"RemoveDirectoryW", reinterpret_cast<void*>(&remove_directory_w), &g_rmdir_w, true, false},
        {"CreateDirectoryW", reinterpret_cast<void*>(&create_directory_w), &g_mkdir_w, true, false},
        {"CreateDirectoryA", reinterpret_cast<void*>(&create_directory_a), &g_mkdir_a, false, false},
        {"CreateDirectoryExW", reinterpret_cast<void*>(&create_directory_ex_w), &g_mkdir_ex_w, false, false},
        {"ReplaceFileW", reinterpret_cast<void*>(&replace_file_w), &g_replace_w, false, false},
        {"FindFirstFileW", reinterpret_cast<void*>(&find_first_file_w), &g_find_first_w, false, true},
        {"FindFirstFileExW", reinterpret_cast<void*>(&find_first_file_ex_w), &g_find_first_ex_w, false, true},
        {"GetFileAttributesW", reinterpret_cast<void*>(&get_attributes_w), &g_get_attr_w, false, true},
        {"GetFileAttributesExW", reinterpret_cast<void*>(&get_attributes_ex_w), &g_get_attr_ex_w, false, true},
    };
    for (const auto& s : specs) {
        if (s.query && !trace && !redirect) continue;
        void* target = reinterpret_cast<void*>(GetProcAddress(kb, s.name));
        // Disabled until assigned: the detours call the original through it.
        if (target) *s.hook = safetyhook::create_inline(target, s.detour, SafetyHookInline::StartDisabled);
        if (!target || !*s.hook || !s.hook->enable()) {
            // A redirect without its queries would show the game the real folder and write to the sandbox.
            // S24 review: in redirect every hook of an API this KernelBase exports is required (an unhooked CopyFileW
            // would write the real folder); an export that does not exist cannot be called by the game either.
            if (s.required || (s.query && redirect) || (redirect && target)) {
                // Disabled, not destroyed: another thread may be inside a detour.
                log::error("Session2 save guard: cannot hook {}; disabling the others, option D stays off", s.name);
                for (const auto& o : specs) {
                    if (*o.hook) (void)o.hook->disable();
                }
                g_redirect.store(false, std::memory_order_relaxed);
                return false;
            }
            missing += std::string(missing.empty() ? "" : ", ") + s.name;
        }
    }
    return true;
}

}  // namespace

bool save_hooks_install_guest_role() {
    if (g_guest_role.load()) return true;
    if (g_guard) {
        log::error("Role guest: the save hooks are already installed in another mode; the guest's folder cannot be "
                   "used");
        return false;
    }
    wchar_t local[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        log::error("Role guest: %LOCALAPPDATA% not found; the guest's saves cannot be redirected");
        return false;
    }
    const std::wstring dir = role::save_dir(local);
    const std::wstring target = role::guest_save_dir(local);
    HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
    if (!kb) {
        log::error("Role guest: kernelbase.dll not loaded; the guest's saves cannot be redirected");
        return false;
    }
    auto redir = std::make_unique<saveguard::Redirector>(dir, target);
    if (!redir->valid()) {
        log::error("Role guest: '{}' cannot be the redirect of '{}'", narrow(target), narrow(dir));
        return false;
    }
    // Made now, with the plain APIs (no hook exists yet). Never filled from the real folder: the launcher copies the
    // one slot the guest plays (scripts\launch-2inst.ps1).
    DWORD attrs = GetFileAttributesW(target.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        for (size_t at = target.find(L'\\', 3); at != std::wstring::npos; at = target.find(L'\\', at + 1)) {
            CreateDirectoryW(target.substr(0, at).c_str(), nullptr);
        }
        if (!CreateDirectoryW(target.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
            log::error("Role guest: cannot create '{}' (error {})", narrow(target), GetLastError());
            return false;
        }
        log::warn("Role guest: '{}' did NOT exist: created EMPTY. The guest's title screen shows no save of the real "
                  "folder (nothing is copied from it here); copy one slot with scripts\\launch-2inst.ps1 (-GuestSlot) "
                  "with both games closed",
                  narrow(target));
        attrs = GetFileAttributesW(target.c_str());
    }
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY) || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
        // A link could lead back into the real folder: never followed.
        log::error("Role guest: '{}' is not a plain folder (attributes 0x{:X}); the guest's saves cannot be redirected",
                   narrow(target), attrs);
        return false;
    }
    g_guard = std::make_unique<saveguard::Guard>(dir);
    g_redir = std::move(redir);
    // Before the first hook is enabled: a detour must never see the redirect half on.
    g_redirect.store(true, std::memory_order_relaxed);
    std::string missing;
    if (!enable_hooks(/*trace=*/false, /*redirect=*/true, kb, missing)) return false;
    g_guest_role.store(true);
    log::warn("Role guest: save REDIRECT ON: everything under '{}' goes to '{}' (same sub-folders; persistent, no copy, "
              "no rotation); the real folder is never written by this process ('Session2 save redirect #N' for the "
              "first {} writes){}",
              narrow(dir), narrow(target), kRedirectLines,
              missing.empty() ? "" : " (not hooked, absent here: " + missing + ")");
    return true;
}

bool save_hooks_guest_role() { return g_guest_role.load(); }

bool save_hooks_install(saveguard::Mode mode) {
    if (g_guest_role.load()) {
        // A guest-role process with the host's stages in its ini: they would save into the guest's folder.
        log::error("Session2 save guard: this process is CDMP_ROLE=guest (its saves go to save_cdmp_guest); the host's "
                   "stages that write cannot run here (save_guard={} ignored); option D stays off",
                   saveguard::mode_name(mode));
        return false;
    }
    if (g_guard) return g_guard->active() || g_trace.load(std::memory_order_relaxed) || redirecting();
    if (mode == saveguard::Mode::Off) return false;
    wchar_t local[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        log::error("Session2 save guard: %LOCALAPPDATA% not found; option D stays off");
        return false;
    }
    const std::wstring dir = std::wstring(local) + L"\\Pearl Abyss\\CD\\save";
    const DWORD attrs = GetFileAttributesW(dir.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        log::error("Session2 save guard: save folder '{}' not found; option D stays off", narrow(dir));
        return false;
    }
    HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
    if (!kb) {
        log::error("Session2 save guard: kernelbase.dll not loaded; option D stays off");
        return false;
    }
    g_guard = std::make_unique<saveguard::Guard>(dir);

    // Redirect: the sandbox is made now, before any hook exists. If anything fails the saves are protected the
    // P6 way (block): never a redirect to a half-made copy.
    const std::wstring sandbox = std::wstring(local) + L"\\Pearl Abyss\\CD\\cdmp_save_sandbox";
    CopyStats copied;
    if (mode == saveguard::Mode::Redirect) {
        auto redir = std::make_unique<saveguard::Redirector>(dir, sandbox);
        if (!redir->valid() || !prepare_sandbox(dir, sandbox, copied)) {
            log::error("Session2 save guard: the save folder could not be copied to '{}'; falling back to block "
                       "(writes refused, 'Failed to save data' will come back)", narrow(sandbox));
            mode = saveguard::Mode::Block;
        } else {
            g_redir = std::move(redir);
            // Before the first hook is enabled: a detour must never see the redirect half on.
            g_redirect.store(true, std::memory_order_relaxed);
        }
    }
    const bool trace = mode == saveguard::Mode::Trace;
    const bool redirect = mode == saveguard::Mode::Redirect;
    if (!redirect && hsv::archives(host_save_mode())) {
        log::warn("Session2 host save: host_save={} needs save_guard=redirect (the guard is {} now): nothing is "
                  "exported in this run",
                  hsv::mode_name(host_save_mode()), saveguard::mode_name(mode));
    }

    std::string missing;
    if (!enable_hooks(trace, redirect, kb, missing)) return false;
    const std::string absent = missing.empty() ? "" : " (not hooked, absent here: " + missing + ")";
    if (redirect) {
        log::warn("Session2 save guard REDIRECT: everything under '{}' goes to '{}', a copy made now ({} files in {} "
                  "folders, {} bytes{}; the previous copy, if any, is '{}_prev'); the real folder is not written "
                  "until the game closes ('Session2 save redirect #N' for the first {} writes){}",
                  narrow(dir), narrow(sandbox), copied.files, copied.dirs, copied.bytes,
                  copied.time_failures ? std::format(", {} times not copied", copied.time_failures) : "",
                  narrow(sandbox), kRedirectLines, absent);
        return true;
    }
    if (trace) {
        g_trace.store(true, std::memory_order_relaxed);
        log::warn("Session2 save guard TRACE: writes under '{}' are ALLOWED and logged ('Session2 save trace #N', "
                  "at most {} lines, {} of them reads and queries); the saves are NOT protected{}",
                  narrow(dir), kTraceLines, kTraceNonWriteLines, absent);
        return true;
    }
    g_guard->set_active(true);
    log::info("Session2 save guard ON (block): writes under '{}' are refused until the game closes{}", narrow(dir),
              absent);
    return true;
}

bool save_hooks_active() { return g_guard && g_guard->active(); }
bool save_hooks_tracing() { return g_trace.load(std::memory_order_relaxed); }
uint64_t save_hooks_blocked() { return g_guard ? g_guard->blocked() : 0; }
uint64_t save_hooks_reads() { return g_guard ? g_guard->allowed_reads() : 0; }

std::string save_hooks_trace_describe() {
    return std::format("{} lines logged (cap {}, reads and queries {}), {} not logged", g_budget.logged(),
                       kTraceLines, kTraceNonWriteLines, g_budget.skipped());
}

bool save_hooks_redirecting() { return redirecting(); }

std::wstring save_hooks_sandbox_dir() { return redirecting() && g_redir ? g_redir->sandbox() : std::wstring(); }

uint64_t save_hooks_redirect_failed() { return g_redir_failed.load(std::memory_order_relaxed); }

bool save_hooks_watch_folder(const std::wstring& sandbox_folder) {
    if (g_watch.load(std::memory_order_acquire) || sandbox_folder.empty()) return false;
    static std::atomic<bool> once{false};
    if (once.exchange(true)) return false;
    const Watch* w = new Watch{saveguard::normalize(sandbox_folder + L"\\lobby.save"),
                               saveguard::normalize(sandbox_folder + L"\\save.save")};
    g_watch.store(w, std::memory_order_release);
    return true;
}

uint64_t save_hooks_watch_reads(bool save_file) {
    return (save_file ? g_watch_save : g_watch_lobby).load(std::memory_order_relaxed);
}

std::string save_hooks_redirect_describe() {
    if (!g_redir) return "not redirected";
    const uint64_t failed = g_redir_failed.load(std::memory_order_relaxed);
    const std::string host = host_save_describe();
    return std::format("to '{}', writes {}, reads {}, queries {}{}{}", narrow(g_redir->sandbox()),
                       g_redir_writes.load(std::memory_order_relaxed), g_redir_reads.load(std::memory_order_relaxed),
                       g_redir_queries.load(std::memory_order_relaxed),
                       failed ? std::format(", FAILED {}", failed) : "", host.empty() ? "" : "; " + host);
}

}  // namespace cdmp::session2
