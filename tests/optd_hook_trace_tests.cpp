// Option D, session 24: the save guard's KernelBase hooks in save_guard=trace
// mode, for real, without the game. As in optd_hook_tests.cpp, %LOCALAPPDATA%
// points at a temporary folder for this process only. Here nothing may be
// refused, the caller must get the original's result and last error, and
// each operation under the fake save folder must get one trace line.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "mod/log.h"
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

bool write_file(const std::wstring& path, const char* text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    WriteFile(h, text, static_cast<DWORD>(std::strlen(text)), &w, nullptr);
    CloseHandle(h);
    return true;
}

std::string read_file(const std::wstring& path) {
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

bool has(const std::string& text, const char* what) { return text.find(what) != std::string::npos; }

// Trace lines: "Session2 save trace #" and a digit (the install line names the format with "#N").
size_t trace_lines(const std::string& text) {
    constexpr std::string_view kHead = "Session2 save trace #";
    size_t n = 0;
    for (size_t at = text.find(kHead); at != std::string::npos; at = text.find(kHead, at + 1)) {
        const size_t digit = at + kHead.size();
        if (digit < text.size() && text[digit] >= '0' && text[digit] <= '9') ++n;
    }
    return n;
}

}  // namespace

int main() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(tmp, ec)) {
        if (e.path().filename().wstring().rfind(L"cdmp_optd_tracetest_", 0) == 0) {
            std::filesystem::remove_all(e.path(), ec);
        }
    }
    const std::wstring root = std::wstring(tmp) + L"cdmp_optd_tracetest_" + std::to_wstring(GetCurrentProcessId());
    const std::wstring save = root + L"\\Pearl Abyss\\CD\\save";
    std::filesystem::create_directories(save + L"\\0", ec);
    const std::wstring slot = save + L"\\0\\save.save";
    const std::wstring outside = root + L"\\outside.txt";
    CHECK(write_file(slot, "original"));

    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    const std::wstring log_path = root + L"\\tracetest.log";
    cdmp::log::open(log_path);
    CHECK(cdmp::session2::save_hooks_install(cdmp::saveguard::Mode::Trace));
    CHECK(cdmp::session2::save_hooks_tracing());
    CHECK(!cdmp::session2::save_hooks_active());

    // Nothing refused, and the original's last error reaches the caller.
    SetLastError(0);
    HANDLE h = CreateFileW(slot.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    CHECK(GetLastError() == ERROR_ALREADY_EXISTS);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    SetLastError(0);
    CHECK(CreateFileW((save + L"\\0\\missing.save").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr) ==
          INVALID_HANDLE_VALUE);
    CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);
    // A save in the shape B1 saw: a new folder, a file in it, the old one aside, the new one in place.
    CHECK(CreateDirectoryW((save + L"\\107_new").c_str(), nullptr));
    CHECK(write_file(save + L"\\107_new\\save.save", "new"));
    CHECK(MoveFileExW((save + L"\\0").c_str(), (save + L"\\107_backup").c_str(), MOVEFILE_REPLACE_EXISTING));
    CHECK(MoveFileExW((save + L"\\107_new").c_str(), (save + L"\\0").c_str(), 0));
    CHECK(read_file(slot) == "new");
    CHECK(DeleteFileW((save + L"\\107_backup\\save.save").c_str()));
    CHECK(RemoveDirectoryW((save + L"\\107_backup").c_str()));
    CHECK(SetFileAttributesW(slot.c_str(), FILE_ATTRIBUTE_NORMAL));
    CHECK(CopyFileW(slot.c_str(), (root + L"\\copied_out.save").c_str(), FALSE));  // out of the folder: a read
    CHECK(GetFileAttributesW(slot.c_str()) != INVALID_FILE_ATTRIBUTES);
    SetLastError(0);
    CHECK(GetFileAttributesW((save + L"\\nothing").c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    CHECK(GetFileAttributesExW(slot.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow == 3);
    WIN32_FIND_DATAW fd{};
    HANDLE f = FindFirstFileW((save + L"\\*").c_str(), &fd);
    CHECK(f != INVALID_HANDLE_VALUE);
    if (f != INVALID_HANDLE_VALUE) FindClose(f);
    {
        // The narrow API, with the ANSI form of the path.
        std::string a(static_cast<size_t>(WideCharToMultiByte(CP_ACP, 0, slot.c_str(), -1, nullptr, 0, nullptr,
                                                              nullptr)), '\0');
        WideCharToMultiByte(CP_ACP, 0, slot.c_str(), -1, a.data(), static_cast<int>(a.size()), nullptr, nullptr);
        HANDLE ha = CreateFileA(a.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(ha != INVALID_HANDLE_VALUE);
        if (ha != INVALID_HANDLE_VALUE) CloseHandle(ha);
    }
    CHECK(write_file(outside, "outside"));  // elsewhere: no line

    // The caps: reads and queries stop at their share, writes go on to the total.
    for (int i = 0; i < 2100; ++i) (void)GetFileAttributesW(slot.c_str());
    for (int i = 0; i < 3100; ++i) (void)SetFileAttributesW(slot.c_str(), FILE_ATTRIBUTE_NORMAL);
    const std::string describe = cdmp::session2::save_hooks_trace_describe();
    std::printf("%s\n", describe.c_str());
    CHECK(describe.rfind("5000 lines logged", 0) == 0);

    const std::wstring dated = cdmp::log::current_path();  // this run's file (log_path is a link to it)
    cdmp::log::close();
    const std::string text = read_file(dated);
    CHECK(has(text, "Session2 save guard TRACE: writes under '"));
    CHECK(has(text, "are ALLOWED and logged"));
    CHECK(has(text, "Session2 save trace #1 tid "));
    CHECK(has(text, "CreateFileW WRITE '"));
    CHECK(has(text, "access 0x40000000 (GENERIC_WRITE) share 0x1 (FILE_SHARE_READ) disposition 2 (CREATE_ALWAYS) "
                    "flags 0x80 (FILE_ATTRIBUTE_NORMAL) -> handle 0x"));
    CHECK(has(text, "(it existed)"));
    CHECK(has(text, "CreateFileW READ '"));
    CHECK(has(text, "-> INVALID_HANDLE_VALUE, error 2"));
    CHECK(has(text, "CreateDirectoryW '"));
    CHECK(has(text, "MoveFileExW from '"));
    CHECK(has(text, "flags 0x1 (MOVEFILE_REPLACE_EXISTING) -> TRUE"));
    CHECK(has(text, "DeleteFileW '"));
    CHECK(has(text, "RemoveDirectoryW '"));
    CHECK(has(text, "CopyFileW from '"));
    CHECK(has(text, "GetFileAttributesW '"));
    CHECK(has(text, "-> INVALID_FILE_ATTRIBUTES, error 2"));
    CHECK(has(text, "GetFileAttributesExW '"));
    CHECK(has(text, "size 3"));
    CHECK(has(text, "FindFirstFile"));
    CHECK(has(text, "CreateFileA READ '"));
    CHECK(!has(text, "outside.txt"));
    CHECK(has(text, "the 2000 lines for reads and queries are used up"));
    CHECK(has(text, "cap of 5000 lines reached"));
    CHECK(has(text, "Session2 save trace #5000 "));
    CHECK(!has(text, "Session2 save trace #5001 "));
    CHECK(trace_lines(text) == 5000);

    // The folder is left in %TEMP% (the hooks stay until the process ends, as in the game).
    DeleteFileW(outside.c_str());
    if (g_failures == 0) std::printf("All option D trace hook tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
