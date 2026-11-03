// Option D, P6: the save guard's KernelBase hooks, for real, without the
// game. %LOCALAPPDATA% points at a temporary folder for this process only, so
// the guard protects <temp>\Pearl Abyss\CD\save; the real saves are never
// touched (the guard only refuses, and only inside the fake folder).

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

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

bool can_read(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char buf[16] = {};
    DWORD r = 0;
    const BOOL ok = ReadFile(h, buf, sizeof buf - 1, &r, nullptr);
    CloseHandle(h);
    return ok && r > 0;
}

}  // namespace

int main() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    // Folders of earlier runs (the guard keeps each run's folder until it exits).
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(tmp, ec)) {
        if (e.path().filename().wstring().rfind(L"cdmp_optd_hooktest_", 0) == 0) {
            std::filesystem::remove_all(e.path(), ec);
        }
    }
    const std::wstring root = std::wstring(tmp) + L"cdmp_optd_hooktest_" + std::to_wstring(GetCurrentProcessId());
    const std::wstring save = root + L"\\Pearl Abyss\\CD\\save";
    CreateDirectoryW(root.c_str(), nullptr);
    CreateDirectoryW((root + L"\\Pearl Abyss").c_str(), nullptr);
    CreateDirectoryW((root + L"\\Pearl Abyss\\CD").c_str(), nullptr);
    CreateDirectoryW(save.c_str(), nullptr);
    CreateDirectoryW((save + L"\\0").c_str(), nullptr);
    const std::wstring slot = save + L"\\0\\save.save";
    const std::wstring outside = root + L"\\outside.txt";
    CHECK(write_file(slot, "original"));  // before the guard: allowed

    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    cdmp::log::open(root + L"\\hooktest.log");
    CHECK(cdmp::session2::save_hooks_install());
    CHECK(cdmp::session2::save_hooks_active());

    // Writes under the save folder are refused with ACCESS_DENIED.
    SetLastError(0);
    CHECK(!write_file(slot, "overwritten"));
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
    CHECK(!write_file(save + L"\\0\\new.save", "x"));
    CHECK(!DeleteFileW(slot.c_str()));
    CHECK(!MoveFileExW(slot.c_str(), (root + L"\\moved.save").c_str(), MOVEFILE_REPLACE_EXISTING));
    CHECK(write_file(outside, "outside"));  // elsewhere: allowed
    CHECK(!MoveFileExW(outside.c_str(), (save + L"\\0\\from_outside.save").c_str(), 0));
    CHECK(!CopyFileW(outside.c_str(), (save + L"\\0\\copy.save").c_str(), FALSE));
    CHECK(!SetFileAttributesW(slot.c_str(), FILE_ATTRIBUTE_READONLY));
    CHECK(!RemoveDirectoryW((save + L"\\0").c_str()));
    // New folders under the save folder (B1: slot107_new / slot107_backup).
    SetLastError(0);
    CHECK(!CreateDirectoryW((save + L"\\107_new").c_str(), nullptr));
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
    CHECK(GetFileAttributesW((save + L"\\107_new").c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(!CreateDirectoryExW((save + L"\\0").c_str(), (save + L"\\107_backup").c_str(), nullptr));
    CHECK(GetFileAttributesW((save + L"\\107_backup").c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(CreateDirectoryW((root + L"\\outside_dir").c_str(), nullptr));  // elsewhere: allowed
    RemoveDirectoryW((root + L"\\outside_dir").c_str());
    // Relative paths and "..", forward slashes, other case.
    std::wstring sneaky = root + L"\\Pearl Abyss\\cd\\SAVE\\..\\save/0/save.save";
    CHECK(!write_file(sneaky, "x"));
    // Narrow API.
    // The ANSI form of the path, as a narrow caller would pass it.
    std::string narrow_slot(static_cast<size_t>(WideCharToMultiByte(CP_ACP, 0, slot.c_str(), -1, nullptr, 0,
                                                                     nullptr, nullptr)), '\0');
    WideCharToMultiByte(CP_ACP, 0, slot.c_str(), -1, narrow_slot.data(), static_cast<int>(narrow_slot.size()),
                        nullptr, nullptr);
    narrow_slot.resize(std::strlen(narrow_slot.c_str()));
    CHECK(GetFileAttributesA(narrow_slot.c_str()) != INVALID_FILE_ATTRIBUTES);  // the conversion found the file
    CHECK(CreateFileA(narrow_slot.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr) ==
          INVALID_HANDLE_VALUE);
    // CreateFile2.
    CHECK(CreateFile2(slot.c_str(), GENERIC_WRITE, 0, OPEN_EXISTING, nullptr) == INVALID_HANDLE_VALUE);
    // CreateDirectoryA, with the ANSI form of the folder.
    {
        const std::string narrow_dir = narrow_slot.substr(0, narrow_slot.rfind('\\')) + "\\new_a";
        CHECK(!CreateDirectoryA(narrow_dir.c_str(), nullptr));
        CHECK(GetFileAttributesA(narrow_dir.c_str()) == INVALID_FILE_ATTRIBUTES);
    }

    // Reads pass (the server loads the slot), and the content is intact.
    CHECK(can_read(slot));
    {
        // OPEN_ALWAYS read of an existing save: a read; of a missing file: a creation.
        HANDLE h = CreateFileW(slot.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
        CHECK(h != INVALID_HANDLE_VALUE);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        CHECK(CreateFileW((save + L"\\0\\missing.save").c_str(), GENERIC_READ, 0, nullptr, OPEN_ALWAYS, 0,
                          nullptr) == INVALID_HANDLE_VALUE);
    }
    {
        HANDLE h = CreateFileW(slot.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        char buf[16] = {};
        DWORD r = 0;
        if (h != INVALID_HANDLE_VALUE) {
            ReadFile(h, buf, sizeof buf - 1, &r, nullptr);
            CloseHandle(h);
        }
        CHECK(std::string(buf) == "original");
    }
    CHECK(cdmp::session2::save_hooks_blocked() >= 10);
    CHECK(cdmp::session2::save_hooks_reads() >= 2);

    // Clean up what we can (the guard stays on until the process ends, as in
    // the game): files outside the folder only; the folder is left in %TEMP%.
    DeleteFileW(outside.c_str());
    cdmp::log::close();
    if (g_failures == 0) std::printf("All option D hook tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
