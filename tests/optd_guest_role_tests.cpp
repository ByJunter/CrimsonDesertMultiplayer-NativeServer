// Two copies on one PC (docs/research/dos-instancias.md): CDMP_ROLE=guest's save redirect with the real KernelBase
// hooks (save_hooks_install_guest_role), without the game. As in optd_hook_redirect_tests.cpp, %LOCALAPPDATA% points
// at a temporary folder for this process only. Everything a game would do under ...\CD\save (list it, read a slot,
// make the account and slot folders, the autosave's _new / _backup dance, copies, attributes, the A APIs) must land in
// ...\CD\save_cdmp_guest, the guest must see none of the real folder's slots, and the real folder must stay
// byte-identical: the guest NEVER writes the real save folder. Also: the guest folder is created empty when missing
// (and said loudly), an existing one is used as it is (no copy, no rotation), a guest-role process refuses the host's
// save_guard install, and a guest folder that is not a plain folder is refused.
//
// Every file API of this process is redirected after the install, so the real folder is read back by a child process
// (`snapshot`), and the other cases run in their own children.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "mod/log.h"
#include "mod/session2/save_hooks.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

constexpr wchar_t kPrefix[] = L"cdmp_optd_guestrole_";
constexpr wchar_t kId[] = L"\\1234567890";

bool write_file(const std::wstring& path, const char* text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    WriteFile(h, text, static_cast<DWORD>(std::strlen(text)), &w, nullptr);
    CloseHandle(h);
    return true;
}

std::string read_file(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "(missing)";
    std::string out;
    char buf[4096];
    DWORD r = 0;
    while (ReadFile(h, buf, sizeof buf, &r, nullptr) && r > 0) out.append(buf, r);
    CloseHandle(h);
    return out;
}

bool exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }
bool is_dir(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool has(const std::string& text, std::string_view what) { return text.find(what) != std::string::npos; }

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

std::vector<std::wstring> listing(const std::wstring& pattern) {
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pattern.c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return names;
    do {
        if (std::wstring_view(fd.cFileName) != L"." && std::wstring_view(fd.cFileName) != L"..") {
            names.emplace_back(fd.cFileName);
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return names;
}

bool listed(const std::vector<std::wstring>& names, const wchar_t* name) {
    for (const auto& n : names) {
        if (n == name) return true;
    }
    return false;
}

// ------------------------------------------------------------------ child: snapshot (no hooks)

void snapshot_walk(const std::wstring& dir, const std::wstring& rel, std::map<std::wstring, std::string>& out) {
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring path = dir + L"\\" + name, r = rel + L"\\" + name;
        const bool d = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        uint64_t hash = 1469598103934665603ull;
        if (!d) {
            for (const char c : read_file(path)) hash = (hash ^ static_cast<uint8_t>(c)) * 1099511628211ull;
        }
        // A folder's last-write time is left out (NTFS may stamp it late; optd_hook_redirect_tests.cpp).
        char line[256];
        std::snprintf(line, sizeof line, "%08lX %lu %08lX%08lX %08lX%08lX %016llX", fd.dwFileAttributes,
                      fd.nFileSizeLow, fd.ftCreationTime.dwHighDateTime, fd.ftCreationTime.dwLowDateTime,
                      d ? 0ul : fd.ftLastWriteTime.dwHighDateTime, d ? 0ul : fd.ftLastWriteTime.dwLowDateTime,
                      static_cast<unsigned long long>(hash));
        out[r] = line;
        if (d) snapshot_walk(path, r, out);
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

// `out` is outside the save folders: read here directly.
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

// The real folder: the host's slots (0 and the guest's test character 108) in one account.
void real_setup(const std::wstring& save) {
    make_dirs(save + kId + L"\\slot0");
    make_dirs(save + kId + L"\\slot108");
    CHECK(write_file(save + kId + L"\\slot0\\lobby.save", "host lobby0"));
    CHECK(write_file(save + kId + L"\\slot0\\save.save", "host save0"));
    CHECK(write_file(save + kId + L"\\slot108\\lobby.save", "real lobby108"));
    CHECK(write_file(save + kId + L"\\slot108\\save.save", "real save108"));
    CHECK(write_file(save + L"\\steam_autocloud.vdf", "cloud"));
}

// ------------------------------------------------------------------ child: an existing guest folder is used as it is

int child_existing() {
    const std::wstring root = temp_root(L"ex_");
    const std::wstring cd = root + L"\\Pearl Abyss\\CD", save = cd + L"\\save", guest = cd + L"\\save_cdmp_guest";
    real_setup(save);
    make_dirs(guest + kId + L"\\slot108");
    CHECK(write_file(guest + kId + L"\\slot108\\save.save", "guest save108"));
    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    cdmp::log::open(root + L"\\existing.log");
    CHECK(cdmp::session2::save_hooks_install_guest_role());
    CHECK(read_file(save + kId + L"\\slot108\\save.save") == "guest save108");  // the guest's, not the real one
    CHECK(read_file(save + kId + L"\\slot108\\lobby.save") == "(missing)");     // nothing copied from the real one
    CHECK(!exists(save + kId + L"\\slot0"));
    const std::string text = log_text();
    CHECK(has(text, "Role guest: save REDIRECT ON"));
    CHECK(!has(text, "did NOT exist"));
    if (g_failures == 0) std::printf("Existing guest folder: OK\n");
    return g_failures == 0 ? 0 : 1;
}

// ------------------------------------------------------------------ child: save_cdmp_guest is not a folder

int child_not_dir() {
    const std::wstring root = temp_root(L"nd_");
    const std::wstring cd = root + L"\\Pearl Abyss\\CD", save = cd + L"\\save";
    real_setup(save);
    CHECK(write_file(cd + L"\\save_cdmp_guest", "a file"));
    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    cdmp::log::open(root + L"\\notdir.log");
    CHECK(!cdmp::session2::save_hooks_install_guest_role());
    CHECK(!cdmp::session2::save_hooks_guest_role());
    CHECK(!cdmp::session2::save_hooks_redirecting());
    const std::string text = log_text();
    CHECK(has(text, "is not a plain folder"));
    if (g_failures == 0) std::printf("Guest folder that is a file: refused: OK\n");
    return g_failures == 0 ? 0 : 1;
}

// ------------------------------------------------------------------ the main test

int main_test() {
    const std::wstring root = temp_root(L"");
    const std::wstring cd = root + L"\\Pearl Abyss\\CD", save = cd + L"\\save", guest = cd + L"\\save_cdmp_guest";
    const std::wstring id = save + kId, gid = guest + kId;
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    real_setup(save);
    const std::wstring outside = root + L"\\outside.txt";
    CHECK(write_file(outside, "outside"));
    const std::string before = snapshot(save, root + L"\\snapshot_before.txt");
    CHECK(has(before, "slot108"));

    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    cdmp::log::open(root + L"\\guest.log");
    CHECK(!exists(guest));
    CHECK(cdmp::session2::save_hooks_install_guest_role());
    CHECK(cdmp::session2::save_hooks_guest_role());
    CHECK(cdmp::session2::save_hooks_redirecting());
    CHECK(cdmp::session2::save_hooks_install_guest_role());  // a second call: already on
    CHECK(is_dir(guest));

    // ---- the title screen: the guest's folder is empty, none of the real slots shows
    CHECK(listing(save + L"\\*").empty());
    CHECK(!exists(id + L"\\slot0\\save.save"));
    CHECK(read_file(id + L"\\slot108\\save.save") == "(missing)");

    // ---- the launcher's copy, as the game would see a save it already had: made through the redirect
    CHECK(CreateDirectoryW(id.c_str(), nullptr));
    CHECK(CreateDirectoryW((id + L"\\slot108").c_str(), nullptr));
    CHECK(write_file(id + L"\\slot108\\lobby.save", "guest lobby108"));
    CHECK(write_file(id + L"\\slot108\\save.save", "guest save108"));
    const auto acc = listing(save + L"\\*");
    CHECK(acc.size() == 1 && listed(acc, kId + 1));
    CHECK(listed(listing(id + L"\\*"), L"slot108") && !listed(listing(id + L"\\*"), L"slot0"));

    // ---- the autosave's sequence (backups/traza-guardado-20261011-223220.log), on the guest's slot 108
    CHECK(CreateDirectoryW((id + L"\\slot108_new").c_str(), nullptr));
    CHECK(write_file(id + L"\\slot108_new\\lobby.save", "guest lobby108 v2"));
    CHECK(write_file(id + L"\\slot108_new\\save.save", "guest save108 v2"));
    CHECK(MoveFileExW((id + L"\\slot108").c_str(), (id + L"\\slot108_backup").c_str(), 0));
    CHECK(MoveFileExW((id + L"\\slot108_new").c_str(), (id + L"\\slot108").c_str(), 0));
    CHECK(DeleteFileW((id + L"\\slot108_backup\\lobby.save").c_str()));
    CHECK(DeleteFileW((id + L"\\slot108_backup\\save.save").c_str()));
    CHECK(RemoveDirectoryW((id + L"\\slot108_backup").c_str()));

    // ---- the other APIs, A and W, relative paths, a copy out and in
    CHECK(SetFileAttributesW((id + L"\\slot108\\save.save").c_str(), FILE_ATTRIBUTE_NORMAL));
    CHECK(SetFileAttributesA(ansi(id + L"\\slot108\\lobby.save").c_str(), FILE_ATTRIBUTE_NORMAL));
    HANDLE h = CreateFileA(ansi(id + L"\\a.save").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    CHECK(CreateDirectoryA(ansi(id + L"\\dir_a").c_str(), nullptr));
    CHECK(CopyFileW(outside.c_str(), (id + L"\\in.save").c_str(), FALSE));
    CHECK(CopyFileExW((id + L"\\in.save").c_str(), (id + L"\\in2.save").c_str(), nullptr, nullptr, nullptr, 0));
    CHECK(SUCCEEDED(CopyFile2((id + L"\\in2.save").c_str(), (id + L"\\in3.save").c_str(), nullptr)));
    CHECK(ReplaceFileW((id + L"\\in.save").c_str(), (id + L"\\in3.save").c_str(), nullptr, 0, nullptr, nullptr));
    CHECK(DeleteFileA(ansi(id + L"\\in2.save").c_str()));
    CHECK(CopyFileW((id + L"\\slot108\\save.save").c_str(), (root + L"\\copied_out.save").c_str(), FALSE));
    CHECK(read_file(root + L"\\copied_out.save") == "guest save108 v2");
    wchar_t cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    CHECK(SetCurrentDirectoryW(id.c_str()));
    CHECK(write_file(L"slot108\\rel.save", "rel"));
    SetCurrentDirectoryW(cwd);
    // The \\?\ form, forward slashes and another case are the same folder.
    CHECK(write_file(L"\\\\?\\" + id + L"\\prefixed.save", "prefixed"));
    std::wstring slashed = id + L"\\slashed.save";
    for (wchar_t& c : slashed) {
        if (c == L'\\') c = L'/';
    }
    CHECK(write_file(slashed, "slashed"));

    // ---- a guest-role process never runs the host's save_guard (its stages would save into the guest's folder)
    CHECK(!cdmp::session2::save_hooks_install(cdmp::saveguard::Mode::Redirect));
    CHECK(!cdmp::session2::save_hooks_install(cdmp::saveguard::Mode::Block));
    CHECK(!exists(cd + L"\\cdmp_save_sandbox"));
    CHECK(cdmp::session2::save_hooks_guest_role() && cdmp::session2::save_hooks_redirecting());

    // ---- where everything landed. The guest folder is read through its own name (not under save\: untouched).
    CHECK(read_file(gid + L"\\slot108\\save.save") == "guest save108 v2");
    CHECK(read_file(gid + L"\\slot108\\lobby.save") == "guest lobby108 v2");
    CHECK(read_file(gid + L"\\slot108\\rel.save") == "rel");
    CHECK(!exists(gid + L"\\slot108_new") && !exists(gid + L"\\slot108_backup"));
    CHECK(read_file(gid + L"\\in.save") == "outside" && !exists(gid + L"\\in2.save") && !exists(gid + L"\\in3.save"));
    CHECK(exists(gid + L"\\a.save") && is_dir(gid + L"\\dir_a"));
    CHECK(read_file(gid + L"\\prefixed.save") == "prefixed" && read_file(gid + L"\\slashed.save") == "slashed");
    CHECK(read_file(outside) == "outside");
    const std::string describe = cdmp::session2::save_hooks_redirect_describe();
    std::printf("%s\n", describe.c_str());
    CHECK(has(describe, "save_cdmp_guest', writes ") && !has(describe, "writes 0,") && !has(describe, "FAILED"));

    // ---- the real folder is exactly as it was
    const std::string after = snapshot(save, root + L"\\snapshot_after.txt");
    CHECK(after == before);
    if (after != before) std::printf("before:\n%s\nafter:\n%s\n", before.c_str(), after.c_str());

    const std::string text = log_text();
    CHECK(has(text, "did NOT exist: created EMPTY"));
    CHECK(has(text, "Role guest: save REDIRECT ON: everything under '"));
    CHECK(has(text, "save_cdmp_guest' (same sub-folders"));
    CHECK(has(text, "Session2 save redirect #1 "));
    CHECK(has(text, "this process is CDMP_ROLE=guest"));

    CHECK(run_child(L"existing") == 0);
    CHECK(run_child(L"notdir") == 0);
    // The folders are left in %TEMP% (the hooks stay until the process ends, as in the game).
    if (g_failures == 0) std::printf("All guest-role save redirect tests passed\n");
    return g_failures == 0 ? 0 : 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring_view(argv[1]) == L"snapshot") return child_snapshot(argv[2], argv[3]);
    if (argc == 2 && std::wstring_view(argv[1]) == L"existing") return child_existing();
    if (argc == 2 && std::wstring_view(argv[1]) == L"notdir") return child_not_dir();
    return main_test();
}
