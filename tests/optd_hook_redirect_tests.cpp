// Option D, session 24: the save guard's KernelBase hooks in save_guard=redirect
// mode, for real, without the game. As in optd_hook_tests.cpp, %LOCALAPPDATA%
// points at a temporary folder for this process only. The game's autosave
// sequence from the trace (backups/traza-guardado-20261011-223220.log), with
// its own path shapes, runs against the fake save folder: everything must
// land in the sandbox and the fake save folder must stay byte-identical.
//
// Every file API of this process is redirected after the install, so the
// fake save folder is read back by a child process (`snapshot`), and the
// fallback to block (a sandbox copy that fails) runs in another (`fallback`).

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "mod/log.h"
#include "mod/session2/host_save_export.h"
#include "mod/session2/save_hooks.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

constexpr wchar_t kPrefix[] = L"cdmp_optd_redirtest_";
constexpr wchar_t kId[] = L"\\1234567890";

bool write_file(const std::wstring& path, const char* text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    WriteFile(h, text, static_cast<DWORD>(std::strlen(text)), &w, nullptr);
    CloseHandle(h);
    return true;
}

std::string read_handle(HANDLE h) {
    std::string out;
    char buf[4096];
    DWORD r = 0;
    while (ReadFile(h, buf, sizeof buf, &r, nullptr) && r > 0) out.append(buf, r);
    return out;
}

// "(missing)" if it cannot be opened.
std::string read_file(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "(missing)";
    std::string s = read_handle(h);
    CloseHandle(h);
    return s;
}

bool exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }
bool is_dir(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool has(const std::string& text, const char* what) { return text.find(what) != std::string::npos; }

size_t count(const std::string& text, std::string_view what) {
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) ++n;
    return n;
}

std::string ansi(const std::wstring& w) {
    std::string a(static_cast<size_t>(WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr)),
                  '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, a.data(), static_cast<int>(a.size()), nullptr, nullptr);
    a.pop_back();
    return a;
}

std::string utf8(const std::wstring& w) {
    std::string s(static_cast<size_t>(WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr)),
                  '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), static_cast<int>(s.size()), nullptr, nullptr);
    s.pop_back();
    return s;
}

std::vector<std::wstring> list(HANDLE f, WIN32_FIND_DATAW& fd) {
    std::vector<std::wstring> names;
    if (f == INVALID_HANDLE_VALUE) return names;
    do names.emplace_back(fd.cFileName);
    while (FindNextFileW(f, &fd));
    FindClose(f);
    return names;
}

bool listed(const std::vector<std::wstring>& names, const wchar_t* name) {
    for (const auto& n : names) {
        if (n == name) return true;
    }
    return false;
}

void set_times(const std::wstring& path, uint64_t when) {
    HANDLE h = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    FILETIME ft{static_cast<DWORD>(when), static_cast<DWORD>(when >> 32)};
    SetFileTime(h, &ft, &ft, &ft);
    CloseHandle(h);
}

// Creation and write times (the access time moves with a read).
std::pair<uint64_t, uint64_t> times(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) return {0, 0};
    const auto u = [](FILETIME t) { return (static_cast<uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return {u(fa.ftCreationTime), u(fa.ftLastWriteTime)};
}

// ------------------------------------------------------------------ child: snapshot

// One line per entry under `dir`: relative path, attributes, size, creation and write times, content hash.
void snapshot_walk(const std::wstring& dir, const std::wstring& rel, std::map<std::wstring, std::string>& out) {
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring path = dir + L"\\" + name, r = rel + L"\\" + name;
        uint64_t hash = 1469598103934665603ull;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            for (const char c : read_file(path)) hash = (hash ^ static_cast<uint8_t>(c)) * 1099511628211ull;
        }
        // A folder's last-write time is left out: NTFS may stamp it late for entries the setup made just before
        // the first snapshot (seen flaky, the folder stamped with the setup's own file times); its entries, each
        // file's times and contents are compared.
        const bool is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        char line[256];
        std::snprintf(line, sizeof line, "%08lX %lu %08lX%08lX %08lX%08lX %016llX", fd.dwFileAttributes,
                      fd.nFileSizeLow, fd.ftCreationTime.dwHighDateTime, fd.ftCreationTime.dwLowDateTime,
                      is_dir ? 0ul : fd.ftLastWriteTime.dwHighDateTime, is_dir ? 0ul : fd.ftLastWriteTime.dwLowDateTime,
                      static_cast<unsigned long long>(hash));
        out[r] = line;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) snapshot_walk(path, r, out);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

int child_snapshot(const std::wstring& dir, const std::wstring& out_path) {
    std::map<std::wstring, std::string> entries;
    snapshot_walk(dir, L"", entries);
    std::string text;
    for (const auto& [rel, line] : entries) text += utf8(rel) + " " + line + "\n";
    return write_file(out_path, text.c_str()) ? 0 : 1;
}

std::wstring self_path() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return p;
}

int run_child(const std::wstring& args) {
    std::wstring cmd = L"\"" + self_path() + L"\" " + args;
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}

// The fake save folder as a child (no hooks) sees it. `out` is outside the save folder: read here directly.
std::string snapshot(const std::wstring& dir, const std::wstring& out) {
    if (run_child(L"snapshot \"" + dir + L"\" \"" + out + L"\"") != 0) return "(snapshot failed)";
    std::ifstream in(std::filesystem::path(out), std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

void make_dirs(const std::wstring& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
}

std::wstring temp_root(const wchar_t* tag) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    return std::wstring(tmp) + kPrefix + tag + std::to_wstring(GetCurrentProcessId());
}

std::string log_text() {
    const std::wstring dated = cdmp::log::current_path();
    cdmp::log::close();
    std::ifstream in(std::filesystem::path(dated), std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// ------------------------------------------------------------------ child: fallback to block

// A file of the save folder that cannot be read (held open with no sharing) makes the sandbox copy fail: the
// install must fall back to block, delete the half-made sandbox and keep the previous one as _prev.
int child_fallback() {
    const std::wstring root = temp_root(L"fb_");
    const std::wstring cd = root + L"\\Pearl Abyss\\CD", save = cd + L"\\save", box = cd + L"\\cdmp_save_sandbox";
    make_dirs(save + L"\\1");
    make_dirs(box);
    CHECK(write_file(save + L"\\1\\save.save", "x"));
    CHECK(write_file(save + L"\\1\\locked.save", "locked"));
    CHECK(write_file(box + L"\\old.txt", "old"));
    HANDLE lock = CreateFileW((save + L"\\1\\locked.save").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0,
                              nullptr);
    CHECK(lock != INVALID_HANDLE_VALUE);

    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    cdmp::log::open(root + L"\\fallback.log");
    CHECK(cdmp::session2::save_hooks_install(cdmp::saveguard::Mode::Redirect));
    CHECK(cdmp::session2::save_hooks_active());
    CHECK(!cdmp::session2::save_hooks_redirecting());
    CHECK(!exists(box));                                // the half-made copy is gone
    CHECK(read_file(box + L"_prev\\old.txt") == "old");  // the previous one is kept
    SetLastError(0);
    CHECK(!write_file(save + L"\\1\\save.save", "overwritten"));
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
    if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
    const std::string text = log_text();
    CHECK(has(text, "Session2 save redirect: cannot copy '"));
    CHECK(has(text, "falling back to block"));
    CHECK(has(text, "Session2 save guard ON (block)"));
    if (g_failures == 0) std::printf("Fallback to block: OK\n");
    return g_failures == 0 ? 0 : 1;
}

// ------------------------------------------------------------------ children: host_save=export (P8 S35 phase E)

// The folders inside `dir` whose names match `pred`.
template <class Pred>
std::vector<std::wstring> folders(const std::wstring& dir, Pred pred) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd{};
    for (const std::wstring& n : list(FindFirstFileW((dir + L"\\*").c_str(), &fd), fd)) {
        if (n != L"." && n != L".." && is_dir(dir + L"\\" + n) && pred(n)) out.push_back(n);
    }
    return out;
}
bool is_generation(const std::wstring& n) { return cdmp::session2::hsv::parse_generation_name(n).has_value(); }

// The previous start's sandbox (with the host's autosave in slot0) and its _prev, the start before.
void export_setup(const std::wstring& root) {
    const std::wstring cd = root + L"\\Pearl Abyss\\CD", save = cd + L"\\save", box = cd + L"\\cdmp_save_sandbox";
    make_dirs(save + kId + L"\\slot1");
    make_dirs(box + kId + L"\\slot0");
    make_dirs(box + L"_prev");
    CHECK(write_file(save + kId + L"\\slot1\\save.save", "real1"));
    CHECK(write_file(box + kId + L"\\slot0\\save.save", "host autosave"));
    CHECK(write_file(box + L"_prev\\older.txt", "older"));
}

// host_save=export: before the usual rotation the old sandbox and _prev go into one verified generation; then the
// rotation of today (old _prev deleted, sandbox -> _prev, a new copy of the real folder).
int child_export_ok() {
    const std::wstring root = temp_root(L"exp_");
    const std::wstring cd = root + L"\\Pearl Abyss\\CD", box = cd + L"\\cdmp_save_sandbox";
    const std::wstring exports = root + L"\\exports";
    export_setup(root);
    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    cdmp::log::open(root + L"\\export.log");
    cdmp::session2::host_save_configure({"export", exports, 5});
    CHECK(cdmp::session2::save_hooks_install(cdmp::saveguard::Mode::Redirect));
    CHECK(cdmp::session2::save_hooks_redirecting());
    const auto gens = folders(exports, is_generation);
    CHECK(gens.size() == 1);
    if (!gens.empty()) {
        const std::wstring g = exports + L"\\" + gens[0];
        CHECK(read_file(g + L"\\sandbox" + kId + L"\\slot0\\save.save") == "host autosave");
        CHECK(read_file(g + L"\\prev\\older.txt") == "older");
        CHECK(has(read_file(g + L"\\manifest.txt"), "state=complete"));
    }
    CHECK(read_file(box + L"_prev" + kId + L"\\slot0\\save.save") == "host autosave");
    CHECK(!exists(box + L"_prev\\older.txt"));
    CHECK(read_file(box + kId + L"\\slot1\\save.save") == "real1");
    CHECK(!exists(box + kId + L"\\slot0"));
    CHECK(folders(exports, [](const std::wstring& n) { return n == L"runs"; }).size() == 1);  // this run's journal
    CHECK(has(cdmp::session2::save_hooks_redirect_describe(), "host save export: generation "));
    const std::string text = log_text();
    CHECK(has(text, "Session2 host save EXPORT: generation '"));
    CHECK(has(text, "Session2 save guard REDIRECT"));
    if (g_failures == 0) std::printf("host_save=export: OK\n");
    return g_failures == 0 ? 0 : 1;
}

// The export cannot be made (an export folder inside %LOCALAPPDATA%\Pearl Abyss is refused): nothing is deleted,
// the old sandbox is set aside, _prev stays, the new sandbox is made and the redirect still runs.
int child_export_fail(const std::wstring& root) {
    const std::wstring cd = root + L"\\Pearl Abyss\\CD", box = cd + L"\\cdmp_save_sandbox";
    export_setup(root);
    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    cdmp::log::open(root + L"\\export_fail.log");
    cdmp::session2::host_save_configure({"export", cd + L"\\exports", 5});
    CHECK(cdmp::session2::save_hooks_install(cdmp::saveguard::Mode::Redirect));
    CHECK(cdmp::session2::save_hooks_redirecting());
    const auto aside = folders(cd, [](const std::wstring& n) {
        return cdmp::session2::hsv::parse_aside_name(L"cdmp_save_sandbox", n).has_value();
    });
    CHECK(aside.size() == 1);
    if (!aside.empty()) CHECK(read_file(cd + L"\\" + aside[0] + kId + L"\\slot0\\save.save") == "host autosave");
    CHECK(read_file(box + L"_prev\\older.txt") == "older");
    CHECK(read_file(box + kId + L"\\slot1\\save.save") == "real1");
    CHECK(!exists(cd + L"\\exports"));
    const std::string text = log_text();
    CHECK(has(text, "the export FAILED"));
    CHECK(has(text, "set aside as '"));
    if (g_failures == 0) std::printf("host_save=export, failed export: OK\n");
    return g_failures == 0 ? 0 : 1;
}

// The next start, with a good export folder: the folder set aside, the kept _prev and the sandbox are archived;
// the one set aside is deleted (it is in a verified generation now).
int child_export_retry(const std::wstring& root) {
    const std::wstring cd = root + L"\\Pearl Abyss\\CD", box = cd + L"\\cdmp_save_sandbox";
    const std::wstring exports = root + L"\\exports";
    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    cdmp::log::open(root + L"\\export_retry.log");
    cdmp::session2::host_save_configure({"probe", exports, 5});
    CHECK(cdmp::session2::save_hooks_install(cdmp::saveguard::Mode::Redirect));
    CHECK(cdmp::session2::save_hooks_redirecting());
    const auto gens = folders(exports, is_generation);
    CHECK(gens.size() == 1);
    if (!gens.empty()) {
        const std::wstring g = exports + L"\\" + gens[0];
        CHECK(read_file(g + L"\\prev\\older.txt") == "older");
        const auto asides = folders(g, [](const std::wstring& n) { return n.rfind(L"aside-", 0) == 0; });
        CHECK(asides.size() == 1);
        if (!asides.empty()) CHECK(read_file(g + L"\\" + asides[0] + kId + L"\\slot0\\save.save") == "host autosave");
        CHECK(has(read_file(g + L"\\manifest.txt"), "mode=probe"));
    }
    CHECK(folders(cd, [](const std::wstring& n) { return n.find(L"_unexported_") != std::wstring::npos; }).empty());
    CHECK(!exists(box + L"_prev\\older.txt"));
    CHECK(read_file(box + kId + L"\\slot1\\save.save") == "real1");
    const std::string text = log_text();
    CHECK(has(text, "is in a verified generation now: deleted"));
    CHECK(has(text, "Session2 host save probe: "));
    if (g_failures == 0) std::printf("host_save=probe after a failed export: OK\n");
    return g_failures == 0 ? 0 : 1;
}

// Review of phase E: the export fails AND the sandbox cannot be set aside (a file in it is held open without
// sharing: the copy cannot read it, the folder cannot be renamed): nothing is moved or deleted, the run blocks.
int child_export_block() {
    const std::wstring root = temp_root(L"expb_");
    const std::wstring cd = root + L"\\Pearl Abyss\\CD", box = cd + L"\\cdmp_save_sandbox";
    export_setup(root);
    HANDLE lock = CreateFileW((box + kId + L"\\slot0\\save.save").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0,
                              nullptr);
    CHECK(lock != INVALID_HANDLE_VALUE);
    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    cdmp::log::open(root + L"\\export_block.log");
    cdmp::session2::host_save_configure({"export", root + L"\\exports", 5});
    CHECK(cdmp::session2::save_hooks_install(cdmp::saveguard::Mode::Redirect));
    CHECK(cdmp::session2::save_hooks_active() && !cdmp::session2::save_hooks_redirecting());
    if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
    CHECK(read_file(box + kId + L"\\slot0\\save.save") == "host autosave");  // kept where it was
    CHECK(read_file(box + L"_prev\\older.txt") == "older");
    CHECK(folders(cd, [](const std::wstring& n) { return n.find(L"_unexported_") != std::wstring::npos; }).empty());
    const std::string text = log_text();
    CHECK(has(text, "the export FAILED"));
    CHECK(has(text, "cannot be set aside as '"));
    CHECK(has(text, "falling back to block"));
    if (g_failures == 0) std::printf("host_save=export, set-aside impossible -> block: OK\n");
    return g_failures == 0 ? 0 : 1;
}

// Review L1: with hsv::kMaxAside sandboxes already set aside, one more failed export blocks and touches nothing.
int child_export_limit() {
    const std::wstring root = temp_root(L"expl_");
    const std::wstring cd = root + L"\\Pearl Abyss\\CD", box = cd + L"\\cdmp_save_sandbox";
    export_setup(root);
    for (const wchar_t* g : {L"20261001-000000", L"20261002-000000", L"20261003-000000"}) {
        make_dirs(cd + L"\\cdmp_save_sandbox_unexported_" + g);
        CHECK(write_file(cd + L"\\cdmp_save_sandbox_unexported_" + g + L"\\x.txt", "kept"));
    }
    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    cdmp::log::open(root + L"\\export_limit.log");
    cdmp::session2::host_save_configure({"export", cd + L"\\exports", 5});  // refused: the export fails
    CHECK(cdmp::session2::save_hooks_install(cdmp::saveguard::Mode::Redirect));
    CHECK(cdmp::session2::save_hooks_active() && !cdmp::session2::save_hooks_redirecting());
    CHECK(read_file(box + kId + L"\\slot0\\save.save") == "host autosave");
    CHECK(read_file(box + L"_prev\\older.txt") == "older");
    CHECK(folders(cd, [](const std::wstring& n) { return n.find(L"_unexported_") != std::wstring::npos; }).size() == 3);
    const std::string text = log_text();
    CHECK(has(text, "sandboxes are already set aside (the limit)"));
    CHECK(has(text, "falling back to block"));
    if (g_failures == 0) std::printf("host_save=export, set-aside limit -> block: OK\n");
    return g_failures == 0 ? 0 : 1;
}

// ------------------------------------------------------------------ the main test

int main_test() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(tmp, ec)) {
        if (e.path().filename().wstring().rfind(kPrefix, 0) == 0) std::filesystem::remove_all(e.path(), ec);
    }
    const std::wstring root = temp_root(L"");
    const std::wstring cd = root + L"\\Pearl Abyss\\CD";
    const std::wstring save = cd + L"\\save", box = cd + L"\\cdmp_save_sandbox", prev = box + L"_prev";
    const std::wstring id = save + kId, bid = box + kId;
    make_dirs(id + L"\\slot0");
    make_dirs(id + L"\\slot1");
    CHECK(write_file(id + L"\\slot0\\lobby.save", "lobby0"));
    CHECK(write_file(id + L"\\slot0\\save.save", "save0"));
    CHECK(write_file(id + L"\\slot1\\save.save", "save1"));
    CHECK(write_file(id + L"\\steam_autocloud.vdf", "vdf"));
    CHECK(write_file(save + L"\\user_engine_option_save.xml", "<options/>"));
    set_times(id + L"\\slot0\\save.save", 0x01DC5B2A00000000ull);  // a date in 2026
    set_times(id + L"\\slot0", 0x01DC5B2B00000000ull);
    const auto save0_times = times(id + L"\\slot0\\save.save");
    const auto slot0_times = times(id + L"\\slot0");
    // The sandbox of the previous start, and an older _prev with a read-only file and a folder in it.
    make_dirs(box);
    make_dirs(prev + L"\\d");
    CHECK(write_file(box + L"\\old.txt", "old"));
    CHECK(write_file(prev + L"\\older.txt", "older"));
    CHECK(write_file(prev + L"\\d\\e.txt", "e"));
    CHECK(SetFileAttributesW((prev + L"\\older.txt").c_str(), FILE_ATTRIBUTE_READONLY));
    const std::wstring outside = root + L"\\outside.txt";
    CHECK(write_file(outside, "outside"));
    const std::string before = snapshot(save, root + L"\\snapshot_before.txt");
    CHECK(has(before, "slot0\\save.save"));

    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    cdmp::log::open(root + L"\\redirtest.log");
    CHECK(cdmp::session2::save_hooks_install(cdmp::saveguard::Mode::Redirect));
    CHECK(cdmp::session2::save_hooks_redirecting());
    CHECK(!cdmp::session2::save_hooks_active());
    CHECK(!cdmp::session2::save_hooks_tracing());
    // P8 session 25 (guest_save_host.cpp): the sandbox root, and the reads of one slot folder's two files counted
    // whatever the spelling of the folder given (upper case, '/').
    CHECK(cdmp::session2::save_hooks_sandbox_dir() == box);
    CHECK(cdmp::session2::save_hooks_redirect_failed() == 0);
    CHECK(cdmp::session2::save_hooks_watch_folder(bid + L"/SLOT0"));
    CHECK(!cdmp::session2::save_hooks_watch_folder(bid + L"\\slot1"));  // one folder per run
    CHECK(cdmp::session2::save_hooks_watch_reads(false) == 0 && cdmp::session2::save_hooks_watch_reads(true) == 0);

    // The sandbox: a full copy with its times; the previous one became _prev, the older _prev is gone.
    CHECK(read_file(bid + L"\\slot0\\save.save") == "save0");
    CHECK(read_file(bid + L"\\slot0\\lobby.save") == "lobby0");
    CHECK(read_file(box + L"\\user_engine_option_save.xml") == "<options/>");
    CHECK(times(bid + L"\\slot0\\save.save") == save0_times);
    CHECK(times(bid + L"\\slot0") == slot0_times);
    CHECK(!exists(box + L"\\old.txt"));
    CHECK(read_file(prev + L"\\old.txt") == "old");
    CHECK(!exists(prev + L"\\older.txt") && !exists(prev + L"\\d"));

    // ---- the autosave of the trace (slot0 already there), through the real paths
    WIN32_FIND_DATAW fd{};
    auto names = list(FindFirstFileW((id + L"\\*").c_str(), &fd), fd);
    CHECK(listed(names, L"slot0") && listed(names, L"slot1") && listed(names, L"steam_autocloud.vdf"));
    SetLastError(0);
    CHECK(GetFileAttributesW((id + L"\\slot0_new").c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);
    CHECK(GetFileAttributesW((id + L"\\slot0_backup").c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(is_dir(id + L"\\slot0"));
    CHECK(MoveFileExW((id + L"\\slot0").c_str(), (id + L"\\slot0_backup").c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED));
    names = list(FindFirstFileW((id + L"\\slot0_backup\\*.*").c_str(), &fd), fd);
    CHECK(listed(names, L"lobby.save") && listed(names, L"save.save"));
    CHECK(CreateDirectoryW((id + L"\\slot0_new").c_str(), nullptr));
    CHECK(GetFileAttributesW((id + L"\\slot0_new\\lobby.save").c_str()) == INVALID_FILE_ATTRIBUTES);
    // CopyFileW's source in all lower case, as the game passed it.
    std::wstring lower = id + L"\\slot0_backup\\lobby.save";
    for (wchar_t& c : lower) c = static_cast<wchar_t>(std::towlower(c));
    CHECK(CopyFileW(lower.c_str(), (id + L"\\slot0_new\\lobby.save").c_str(), FALSE));
    CHECK(GetFileAttributesW((id + L"\\slot0_new\\lobby.save").c_str()) != INVALID_FILE_ATTRIBUTES);
    CHECK(SetFileAttributesA(ansi(id + L"\\slot0_new\\lobby.save").c_str(), FILE_ATTRIBUTE_ARCHIVE));
    CHECK(GetFileAttributesW((id + L"\\slot0_new\\save.save").c_str()) == INVALID_FILE_ATTRIBUTES);
    SetLastError(77);
    HANDLE h = CreateFileW((id + L"\\slot0_new\\save.save").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    CHECK(GetLastError() == 0);  // a new file
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(h, "save0-new", 9, &w, nullptr);
        CloseHandle(h);
    }
    CHECK(MoveFileExW((id + L"\\slot0_new").c_str(), (id + L"\\slot0").c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED));
    CHECK(is_dir(id + L"\\slot0_backup"));
    const std::wstring doomed = id + L"\\slot0_backup_213942844459_delete";
    CHECK(MoveFileExW((id + L"\\slot0_backup").c_str(), doomed.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED));
    CHECK(is_dir(doomed));
    names = list(FindFirstFileW((doomed + L"\\*.*").c_str(), &fd), fd);
    CHECK(listed(names, L"lobby.save"));
    CHECK(SetFileAttributesA(ansi(doomed + L"\\lobby.save").c_str(), FILE_ATTRIBUTE_ARCHIVE));
    CHECK(DeleteFileW((doomed + L"\\lobby.save").c_str()));
    CHECK(DeleteFileW((doomed + L"\\save.save").c_str()));
    CHECK(RemoveDirectoryW(doomed.c_str()));

    // ---- the load: CreateFile2 reads, and the other spellings
    h = CreateFile2((id + L"\\slot0\\lobby.save").c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) {
        CHECK(read_handle(h) == "lobby0");
        CloseHandle(h);
    }
    h = CreateFile2((id + L"\\slot0\\save.save").c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) {
        CHECK(read_handle(h) == "save0-new");
        CloseHandle(h);
    }
    // The two reads above, and nothing else so far: the save's writes and the copy from the backup do not count.
    CHECK(cdmp::session2::save_hooks_watch_reads(false) == 1);
    CHECK(cdmp::session2::save_hooks_watch_reads(true) == 1);
    h = CreateFileA(ansi(id + L"\\slot1\\save.save").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0,
                    nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) {
        CHECK(read_handle(h) == "save1");
        CloseHandle(h);
    }
    CHECK(cdmp::session2::save_hooks_watch_reads(true) == 1);  // slot1 is not the watched folder
    CHECK(read_file(L"\\\\?\\" + id + L"\\slot0\\save.save") == "save0-new");
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    CHECK(GetFileAttributesExW((id + L"\\slot0\\save.save").c_str(), GetFileExInfoStandard, &fa) &&
          fa.nFileSizeLow == 9);
    names = list(FindFirstFileExW((id + L"\\slot0\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                  0),
                 fd);
    CHECK(listed(names, L"save.save") && listed(names, L"lobby.save"));
    // Relative paths: resolved, then redirected.
    wchar_t cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    CHECK(SetCurrentDirectoryW(id.c_str()));
    CHECK(GetFileAttributesW(L"slot0\\save.save") != INVALID_FILE_ATTRIBUTES);
    CHECK(write_file(L"slot1\\rel.save", "rel"));
    SetCurrentDirectoryW(cwd);

    // ---- the other APIs
    CHECK(CopyFileW((id + L"\\slot0\\save.save").c_str(), (root + L"\\copied_out.save").c_str(), FALSE));
    CHECK(read_file(root + L"\\copied_out.save") == "save0-new");  // read from the sandbox
    CHECK(CopyFileW(outside.c_str(), (id + L"\\in.save").c_str(), FALSE));
    CHECK(CopyFileExW((id + L"\\in.save").c_str(), (id + L"\\in2.save").c_str(), nullptr, nullptr, nullptr, 0));
    CHECK(SUCCEEDED(CopyFile2((id + L"\\in2.save").c_str(), (id + L"\\in4.save").c_str(), nullptr)));
    CHECK(MoveFileWithProgressW((id + L"\\in2.save").c_str(), (id + L"\\in3.save").c_str(), nullptr, nullptr, 0));
    CHECK(ReplaceFileW((id + L"\\in.save").c_str(), (id + L"\\in3.save").c_str(), nullptr, 0, nullptr, nullptr));
    CHECK(DeleteFileA(ansi(id + L"\\in.save").c_str()));
    CHECK(CreateDirectoryA(ansi(id + L"\\dir_a").c_str(), nullptr));
    CHECK(CreateDirectoryExW((id + L"\\slot0").c_str(), (id + L"\\dir_ex").c_str(), nullptr));
    h = CreateFileA(ansi(id + L"\\a.save").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    CHECK(read_file(id + L"\\in4.save") == "outside");  // seen through the redirect too

    // ---- last errors: exactly the original's
    SetLastError(0);
    CHECK(CreateFileW((id + L"\\missing.save").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr) ==
          INVALID_HANDLE_VALUE);
    CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);
    SetLastError(0);
    CHECK(CreateFileW((save + L"\\nodir\\x.save").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr) ==
          INVALID_HANDLE_VALUE);
    CHECK(GetLastError() == ERROR_PATH_NOT_FOUND);
    SetLastError(0);
    h = CreateFileW((id + L"\\slot0\\save.save").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0,
                    nullptr);
    CHECK(h != INVALID_HANDLE_VALUE && GetLastError() == ERROR_ALREADY_EXISTS);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    CHECK(write_file(id + L"\\slot0\\save.save", "save0-new"));
    SetLastError(0);
    CHECK(!MoveFileExW((id + L"\\slot0\\lobby.save").c_str(), (id + L"\\slot0\\save.save").c_str(), 0));
    CHECK(GetLastError() == ERROR_ALREADY_EXISTS);
    SetLastError(0);
    CHECK(!DeleteFileW((id + L"\\slot0\\nothing.save").c_str()));
    CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);
    // A success keeps whatever the original leaves: the same as for a path elsewhere.
    SetLastError(1234);
    (void)GetFileAttributesW(outside.c_str());
    const DWORD elsewhere = GetLastError();
    SetLastError(1234);
    (void)GetFileAttributesW((id + L"\\slot0\\save.save").c_str());
    CHECK(GetLastError() == elsewhere);
    SetLastError(1234);
    CHECK(SetFileAttributesW(outside.c_str(), FILE_ATTRIBUTE_NORMAL));
    const DWORD elsewhere_set = GetLastError();
    SetLastError(1234);
    CHECK(SetFileAttributesW((id + L"\\slot0\\save.save").c_str(), FILE_ATTRIBUTE_NORMAL));
    CHECK(GetLastError() == elsewhere_set);
    CHECK(write_file(outside, "outside"));  // elsewhere: untouched

    // ---- the line cap: writes get lines up to 200, the counts go on
    for (int i = 0; i < 250; ++i) (void)SetFileAttributesW((id + L"\\slot0\\save.save").c_str(), FILE_ATTRIBUTE_NORMAL);
    const std::string describe = cdmp::session2::save_hooks_redirect_describe();
    std::printf("%s\n", describe.c_str());
    CHECK(describe.rfind("to '", 0) == 0);
    CHECK(has(describe, "cdmp_save_sandbox', writes "));
    CHECK(!has(describe, "writes 0,") && !has(describe, "reads 0,") && !has(describe, "queries 0") &&
          !has(describe, "FAILED"));

    // ---- where everything landed: the sandbox has the new save, the real folder is as it was
    CHECK(read_file(bid + L"\\slot0\\save.save") == "save0-new");
    CHECK(read_file(bid + L"\\slot0\\lobby.save") == "lobby0");
    CHECK(!exists(bid + L"\\slot0_new") && !exists(bid + L"\\slot0_backup") &&
          !exists(bid + L"\\slot0_backup_213942844459_delete"));
    CHECK(read_file(bid + L"\\slot1\\rel.save") == "rel");
    // in2 moved to in3, in3 replaced in, in deleted; in4 is CopyFile2's.
    CHECK(read_file(bid + L"\\in4.save") == "outside");
    CHECK(!exists(bid + L"\\in.save") && !exists(bid + L"\\in2.save") && !exists(bid + L"\\in3.save"));
    CHECK(is_dir(bid + L"\\dir_a") && is_dir(bid + L"\\dir_ex") && exists(bid + L"\\a.save"));
    const std::string after = snapshot(save, root + L"\\snapshot_after.txt");
    CHECK(after == before);
    if (after != before) std::printf("before:\n%s\nafter:\n%s\n", before.c_str(), after.c_str());

    const std::string text = log_text();
    CHECK(has(text, "Session2 save guard REDIRECT: everything under '"));
    CHECK(has(text, "(5 files in 3 folders, 29 bytes; "));
    CHECK(has(text, "Session2 save redirect #1 "));
    CHECK(has(text, "MoveFileExW from '"));
    CHECK(has(text, "\\slot0_backup_213942844459_delete' -> TRUE"));
    CHECK(has(text, "cdmp_save_sandbox\\1234567890\\slot0_new\\lobby.save'"));
    CHECK(has(text, "CopyFileW from '"));
    CHECK(has(text, "SetFileAttributesA '"));
    CHECK(has(text, "CreateFileW '"));
    CHECK(has(text, "-> handle 0x"));
    CHECK(has(text, "CreateDirectoryExW template '"));
    CHECK(has(text, "ReplaceFileW replaced '"));
    // CopyFileW's inner CopyFileExW saw the sandbox paths: no line of its own. The copy out of the folder is a read.
    CHECK(count(text, " CopyFileW from '") == 2);
    CHECK(count(text, " CopyFileExW from '") == 1);
    CHECK(count(text, " CopyFile2 from '") == 1);
    CHECK(count(text, " SetFileAttributesW '") > 0 && count(text, " SetFileAttributesA '") == 2);
    CHECK(!has(text, "CreateFile2 '"));         // reads get no line
    CHECK(!has(text, "GetFileAttributesW '"));  // nor queries
    CHECK(has(text, "Session2 save redirect #200 "));
    CHECK(!has(text, "Session2 save redirect #201 "));
    CHECK(has(text, "cap of 200 lines reached"));

    CHECK(run_child(L"fallback") == 0);
    // P8 S35 phase E: host_save=export before the rotation; a failed export keeps everything; the next start archives it.
    CHECK(run_child(L"export_ok") == 0);
    const std::wstring retry = temp_root(L"expf_");
    CHECK(run_child(L"export_fail \"" + retry + L"\"") == 0);
    CHECK(run_child(L"export_retry \"" + retry + L"\"") == 0);
    CHECK(run_child(L"export_block") == 0);
    CHECK(run_child(L"export_limit") == 0);
    // The folders are left in %TEMP% (the hooks stay until the process ends, as in the game).
    if (g_failures == 0) std::printf("All option D redirect hook tests passed\n");
    return g_failures == 0 ? 0 : 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring_view(argv[1]) == L"snapshot") return child_snapshot(argv[2], argv[3]);
    if (argc == 2 && std::wstring_view(argv[1]) == L"fallback") return child_fallback();
    if (argc == 2 && std::wstring_view(argv[1]) == L"export_ok") return child_export_ok();
    if (argc == 2 && std::wstring_view(argv[1]) == L"export_block") return child_export_block();
    if (argc == 2 && std::wstring_view(argv[1]) == L"export_limit") return child_export_limit();
    if (argc == 3 && std::wstring_view(argv[1]) == L"export_fail") return child_export_fail(argv[2]);
    if (argc == 3 && std::wstring_view(argv[1]) == L"export_retry") return child_export_retry(argv[2]);
    return main_test();
}
